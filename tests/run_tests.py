"""Runs the RigModules test suite in tiers. Start it through RigTests.cmd.

Usage: mayapy run_tests.py [filter ...] [-v] [--keep-going] [--jobs N] [--durations [N]] [--update-golden] [--plugin PATH]

Tiers run in the order of TIERS. A tier runs only if every earlier gating tier passed, so a broken plugin or build
is reported once at its root instead of as a failure in every later test.

Each tier's test files are shared out between --jobs mayapy workers, which start together, load the plugin once and
then run tier after tier: a worker takes the next unclaimed file of the tier, runs it, and takes another until none is
left. The next tier starts only when every worker has finished this one and it passed. The plain Python files run the
same way in their own Python workers, beside Maya, gating nothing.

Each worker writes each outcome as it happens and records the running test before it starts, so if a Maya crashes the
report still names the test and keeps every earlier result. Maya's crash reporter is turned off for the workers and
their temp files go to the run folder, so a crash leaves no dialog and nothing in the user's Temp folder.

A filter keeps tests whose id contains it (case-insensitive). Filtered runs still gate: a failed tier blocks later ones.
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile
import time
import traceback
import unittest

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
MAYA_TESTS_DIR = os.path.join(TESTS_DIR, "maya")
REPO_DIR = os.path.dirname(TESTS_DIR)
CPP_DIR = os.path.join(REPO_DIR, "cpp")

# Starting and ending Maya is mayalaunch's job, shared with the tools and with mayadev.
sys.path.insert(0, os.path.join(REPO_DIR, "python"))
import mayalaunch  # noqa: E402
DEFAULT_PLUGIN = os.path.join(CPP_DIR, "x64", "Release", "RigModules.mll")
# The rigmodules Python package, tested together with the plugin it wraps.
PYTHON_DIR = os.path.join(REPO_DIR, "python")

# Every test file belongs to exactly one tier. Order is dependency order: a tier may assume every
# earlier tier passed. A file missing from this table fails the run, so new suites must be placed.
# (name, gates later tiers, test modules)
TIERS = [
    ("registration", True, [
        "concepts.test_plugin",
    ]),
    ("read only", True, [                  # commands and checks that never build a rig
        "commands.test_joint_markup_cmd",
        "commands.test_skeleton_cmds",      # saveRigSkeleton, loadRigSkeleton and the skeleton hash
        "commands.test_marker_cmds",        # markers: the five commands, the build flag, the stamps, the stale status
        "concepts.test_control_shapes",     # rigControlNode shapes and bounds, no rig
        "concepts.test_template_loading",
        "concepts.test_template_validation",
    ]),
    ("build smoke", True, [                # every fixture template builds
        "concepts.test_smoke",
        "commands.test_create_rig",
        "commands.test_selection_sets",     # the selection sets and rigmodules.selection
    ]),
    ("modules", True, [                    # what each module does, and the rig structure around it
        "modules.test_layout",
        "modules.test_fkchain",
        "modules.test_fk_hierarchy",        # FkHierarchy: contract, pose cases, terminators, added joints, mirroring
        "modules.test_aim",
        "modules.test_two_bone_ik",         # TwoBoneIk: contract, pose cases, behaviour, the settings and pole channels
        "modules.test_ribbon",              # Ribbon: contract, pose cases, behaviour, the plan step checks
        "modules.test_limb",                # Limb: contract, pose cases, the sharp elbow, the plan step checks, DAG parenting
        "modules.test_reverse_foot",        # ReverseFoot: contract, pose cases worked from the core foot, channels, plugs, markers, the loop checks, the inverse, the toes and the toe FK control
        "modules.test_push_out",            # rigPushOut: the collisions list, the push, the mirror, the read-phase checks, the display
        "concepts.test_sockets",
        "concepts.test_naming",
        "concepts.test_undo_redo",
        "commands.test_rig_control_cmds",   # control markup, traversal and shape files on a built rig
        "commands.test_rig_info",           # getRigInfo and the rigmodules Python query
        "commands.test_skin_weight_cmds",   # weight files; binding at rest needs a built rig
        "commands.test_smooth_skin_weights",  # smoothSkinWeights: neighbor averaging, locks, max influences, undo
        "commands.test_skin_brush",         # the skin brush below the mouse: falloff, picking, undo, the context settings, and the rest-match brushes
        "commands.test_rig_look",           # setRigLook and the lookRoot
        "commands.test_attachment",         # parentRig and "parent:" ids: a rig that follows another rig
    ]),
    ("constraints", True, [
        "commands.test_constraints",       # the constraint commands and space switching, the types and constraintWeight
        "concepts.test_docs_examples",    # the README quick start ends with a space switch
        "concepts.test_mirror",            # mirrored templates include mirrored constraints
        "commands.test_rig_pose_cmds",     # the animation interface: channels, zero, mirror and flip pose
        "commands.test_inverse",           # poseRigFromJointPose and animateRigFromJointClip: the readers, the solve's round trips, the TwoBoneIk inverse
        "commands.test_joint_clip_capture",  # rigmodules.jointclip.capture_joint_clip
        "commands.test_joint_clip_fbx",    # FBX joint files, bone maps and the skeleton export
        "commands.test_two_bone_ik_cmds",  # setRigIkFkBlend, the matched IK/FK switch, and the pole's module-owned space switch: hand-worked values, no pop either way, warnings, undo
        "commands.test_rig_key_cmds",      # getRigKeys and setRigKeys, and animateRigFromJointClip's keys
        "commands.test_rig_animation_cmds",  # saveRigAnimation, loadRigAnimation and the keys payload's tangents
        "commands.test_rig_data_files",     # library meta in both file formats, getRigDataFileInfo, setRigDataFileMeta, mirror on load
    ]),
    # Animation, then import/export tiers go here, after constraints.
    ("known bugs", False, [                # expected failures, never block anything
        "known.test_known_bugs",
    ]),
]

# Test files that never touch the plugin or a scene. They run in plain Python workers, started before Maya and
# alongside it, so their time (mayalaunch starts real Mayas and waits on timeouts, and the core C++ build) overlaps
# Maya's instead of adding to it. They gate nothing: if the launcher is broken, the Maya tiers fail on their own, and
# the run reports both. Every file here must import no maya module. Slowest first, so the longest starts first.
PLAIN_PYTHON = [
    "concepts.test_mayalaunch",        # the launcher every script that starts Maya uses
    "concepts.test_core_cpp",          # tests/core: the Maya-free C++ of core/, built with cl.exe into one program and run
    "concepts.test_source_layout",     # core/ stays Maya-free; reads files, not the scene
    "concepts.test_python_template",   # rigmodules.template reads a file, builds nothing
]
PLAIN_TIER = "plain"
PLAIN_LABEL = "plain python    "

OUTCOME_CHARS = {"pass": ".", "FAIL": "F", "ERROR": "E", "xfail": "x", "XPASS": "X", "skip": "s"}
BAD_OUTCOMES = ("FAIL", "ERROR", "XPASS")

# A run that takes longer than this is a hung Maya. The suite takes about 35 s.
DEFAULT_TIMEOUT_SECONDS = 900
DEFAULT_DURATIONS = 10
# Maya workers. Each is a mayapy with the plugin loaded (about 10 s to start, all at once); past four, the start and
# the memory cost more than the tests they share out.
DEFAULT_JOBS = 4
# How often a waiting worker or the watching parent looks at the run folder.
POLL_SECONDS = 0.05
# How long the parent waits for workers to exit after the last tier before it stops them.
EXIT_GRACE_SECONDS = 60


def parse_args():
    parser = argparse.ArgumentParser(description="Run the RigModules mayapy test suite.")
    parser.add_argument("filters", nargs="*", help="keep tests whose id contains any of these")
    parser.add_argument("-v", "--verbose", action="store_true", help="print each test as it runs")
    parser.add_argument("--keep-going", action="store_true", help="run every tier even after one fails")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS,
                        help="Maya workers that share out each tier's test files (default: %(default)s)")
    parser.add_argument("--durations", nargs="?", type=int, const=DEFAULT_DURATIONS, default=0, metavar="N",
                        help="after the TOTAL line, list the N slowest test files and tests (default N: %d)"
                             % DEFAULT_DURATIONS)
    parser.add_argument("--update-golden", action="store_true", help="rewrite golden interface snapshots")
    parser.add_argument("--plugin", default=DEFAULT_PLUGIN, help="plugin to test (default: Release build)")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS,
                        help="seconds before a run that has not finished is stopped (default: %(default)s)")
    # Internal: the parent starts each worker with these.
    parser.add_argument("--worker", help=argparse.SUPPRESS)
    parser.add_argument("--plain", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--work-dir", help=argparse.SUPPRESS)
    return parser.parse_args()


# ==== shared report helpers ======================================================================

def tier_label(index):
    return "tier %d  %-14s " % (index + 1, TIERS[index][0])


def indent(text, prefix="       "):
    return "\n".join(prefix + line for line in text.strip().splitlines())


def summarize(outcomes):
    counts = {}
    for item in outcomes:
        counts[item["outcome"]] = counts.get(item["outcome"], 0) + 1
    parts = ["%d passed" % counts.get("pass", 0)]
    for key, label in (("FAIL", "FAILED"), ("ERROR", "ERRORS"), ("XPASS", "UNEXPECTED PASS"),
                       ("xfail", "expected failure"), ("skip", "skipped")):
        if counts.get(key):
            parts.append("%d %s" % (counts[key], label))
    return counts, ", ".join(parts)


def write_failures(out, outcomes):
    for item in outcomes:
        if item["outcome"] in BAD_OUTCOMES:
            out.write("  %s %s\n" % (item["outcome"], item["id"]))
            if item["detail"]:
                out.write(indent(item["detail"]) + "\n")


def append_outcome(path, outcome):
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(outcome) + "\n")


def read_json_file(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def write_json_file(path, value):
    """Writes through a temporary file and a rename, so a reader never sees half a file."""
    with open(path + ".tmp", "w", encoding="utf-8") as handle:
        json.dump(value, handle)
    os.replace(path + ".tmp", path)


def write_text(path, text):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def read_text(path, default=""):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return default


# ==== running tests (in a worker, or inline in the current Maya) =================================

def group_of(test):
    """Report group: the test file name without its test_ prefix, e.g. 'fkchain'."""
    test = getattr(test, "test_case", test)   # a subTest reports under its parent test
    return type(test).__module__.split(".")[-1].replace("test_", "", 1)


class TierResult(unittest.TestResult):
    """Appends each outcome to the outcomes file as it happens (so a crash keeps every earlier result) and writes the
    running test to the progress file. With out, it also streams one mark per test there."""

    def __init__(self, out, files, tier, verbose):
        super().__init__()
        self.out = out
        self.files = files
        self.tier = tier
        self.verbose = verbose
        self.outcomes = []
        self._mark = time.time()

    def _record(self, test, outcome, detail=""):
        # Seconds since the previous record: a test's setup and body, never counted twice, so a test
        # with subtests splits its time across them and the sum over all outcomes is the run's test time.
        now = time.time()
        item = {"tier": self.tier, "id": test.id(), "group": group_of(test), "outcome": outcome, "detail": detail,
                "seconds": round(now - self._mark, 4)}
        self._mark = now
        self.outcomes.append(item)
        append_outcome(self.files["outcomes"], item)
        if self.out:
            self.out.write(("    %-8s %s\n" % (outcome, test.id())) if self.verbose else OUTCOME_CHARS[outcome])
            self.out.flush()

    def startTest(self, test):
        super().startTest(test)
        # Written before the test runs: if Maya crashes, the parent reads which test it was.
        write_text(self.files["progress"], "%s\t%s" % (self.tier, test.id()))
        if self.out and self.verbose:
            self.out.write("\n    running  %s" % test.id())
            self.out.flush()

    def addSuccess(self, test):
        super().addSuccess(test)
        self._record(test, "pass")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._record(test, "FAIL", str(err[1]))

    def addError(self, test, err):
        super().addError(test, err)
        self._record(test, "ERROR", self._exc_info_to_string(err, test))

    def addSubTest(self, test, subtest, err):
        super().addSubTest(test, subtest, err)
        if err is not None:
            failed = issubclass(err[0], test.failureException)
            self._record(subtest, "FAIL" if failed else "ERROR",
                         str(err[1]) if failed else self._exc_info_to_string(err, test))

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self._record(test, "xfail")

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self._record(test, "XPASS", "A known bug test passed. If the bug is fixed, move the test out of known/.")

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._record(test, "skip", reason)


def load_modules(modules, tier, filters):
    """Returns (tests, load errors) for the named test modules, with the filters applied. A module that fails to
    import is reported, not fatal."""
    loader = unittest.TestLoader()
    tests, errors = [], []
    for name in modules:
        try:
            suite = loader.loadTestsFromName(name)
        except Exception:
            errors.append({"tier": tier, "id": name, "group": name.split(".")[-1].replace("test_", "", 1),
                           "outcome": "ERROR", "detail": "could not load:\n" + traceback.format_exc(), "seconds": 0.0})
            continue
        stack = [suite]
        while stack:
            item = stack.pop(0)
            if isinstance(item, unittest.TestSuite):
                stack[0:0] = list(item)
            else:
                tests.append(item)
    if filters:
        wanted = [f.lower() for f in filters]
        tests = [t for t in tests if any(f in t.id().lower() for f in wanted)]
    return tests, errors


def run_modules(modules, tier, files, filters, out=None, verbose=False):
    """Runs the named test modules, writing every outcome (load errors included) to the outcomes file. Returns them."""
    tests, load_errors = load_modules(modules, tier, filters)
    for error in load_errors:
        append_outcome(files["outcomes"], error)
    result = TierResult(out, files, tier, verbose)
    unittest.TestSuite(tests).run(result)
    return load_errors + result.outcomes


def configure_rigtest(work_dir, update_golden):
    for folder in (MAYA_TESTS_DIR, PYTHON_DIR):
        if folder not in sys.path:
            sys.path.insert(0, folder)
    os.makedirs(work_dir, exist_ok=True)
    from rigtest import config
    config.WORK_DIR = work_dir
    config.UPDATE_GOLDEN = update_golden


def tier_modules(tier):
    return PLAIN_PYTHON if tier == PLAIN_TIER else TIERS[tier][2]


# ==== worker: runs whole test files, tier after tier ==============================================
#
# The run folder holds what the parent and its workers share:
#   go.<tier>                      written by the parent: the tier may start. stop: no more tiers.
#   claims/<tier>.<module>         created by the worker that takes that file (O_EXCL, so exactly one does)
#   <worker>.outcomes.jsonl        every outcome, appended as it happens
#   <worker>.progress.txt          what the worker is doing now: "startup: ..." or "<tier>\t<test id>"
#   <worker>.startup.json          written once startup ended: {"error": null or the traceback}
#   <worker>.done.<tier>           the worker found no file of that tier left to take
#   <worker>.log                   the worker's stdout and stderr, Maya's own output included

def worker_files(work_dir, name):
    base = os.path.join(work_dir, name)
    return {"outcomes": base + ".outcomes.jsonl", "progress": base + ".progress.txt", "startup": base + ".startup.json",
            "done": base + ".done.%s", "log": base + ".log", "dir": base}


def go_path(work_dir, tier):
    return os.path.join(work_dir, "go.%s" % tier)


def stop_path(work_dir):
    return os.path.join(work_dir, "stop")


def claim(work_dir, tier, module):
    """True for exactly one worker per (tier, module)."""
    try:
        os.close(os.open(os.path.join(work_dir, "claims", "%s.%s" % (tier, module)), os.O_CREAT | os.O_EXCL | os.O_WRONLY))
        return True
    except FileExistsError:
        return False


def wait_for_go(work_dir, tier):
    """Waits until the parent starts tier (returns its modules) or stops the run (returns None)."""
    while True:
        if os.path.exists(go_path(work_dir, tier)):
            return read_json_file(go_path(work_dir, tier))["modules"]
        if os.path.exists(stop_path(work_dir)):
            return None
        time.sleep(POLL_SECONDS)


def run_worker(args):
    """A worker process: starts Maya (unless plain) with the plugin, then runs every tier the parent starts, taking
    test files from its queue until none is left. Its stdout and stderr go to its log through the parent."""
    files = worker_files(args.work_dir, args.worker)
    write_text(files["progress"], "startup: initializing Maya" if not args.plain else "startup")
    error = None
    try:
        if not args.plain:
            mayalaunch.start_standalone()
            from maya import cmds
            write_text(files["progress"], "startup: loading the plugin")
            cmds.loadPlugin(args.plugin)
        configure_rigtest(files["dir"], args.update_golden)
    except Exception:
        error = traceback.format_exc()
    write_json_file(files["startup"], {"error": error})

    if error is None:
        for tier in [PLAIN_TIER] if args.plain else range(len(TIERS)):
            modules = wait_for_go(args.work_dir, tier)
            if modules is None:
                break
            for module in modules:
                if claim(args.work_dir, tier, module):
                    write_text(files["progress"], "%s\tloading %s" % (tier, module))
                    run_modules([module], tier, files, args.filters)
            write_text(files["done"] % tier, "")
            write_text(files["progress"], "%s\twaiting" % tier)

    if args.plain:
        sys.stdout.flush()
        os._exit(0)
    mayalaunch.finish_standalone(0)


# ==== parent: starts the workers, shares out the tiers, and reports ===============================

def running_maya_dir():
    """The Maya install this script runs under, so the workers are the same mayapy. None outside mayapy, which
    leaves mayalaunch to find the install."""
    if "mayapy" in os.path.basename(sys.executable).lower():
        return os.path.dirname(os.path.dirname(sys.executable))
    return None


class Worker:
    """One worker process as the parent sees it: its files, and the outcomes it has written so far."""

    def __init__(self, name, args, work_dir, plugin_copy, plain):
        self.name = name
        self.files = worker_files(work_dir, name)
        self.read_bytes = 0
        self.crash_reported = False
        command = ["--worker", name, "--work-dir", work_dir] + list(args.filters)
        command += ["--plain"] if plain else ["--plugin", plugin_copy]
        command += ["--update-golden"] if args.update_golden else []
        env = mayalaunch.child_environment(self.files["dir"])
        if plain:
            log = open(self.files["log"], "w", encoding="utf-8")
            self.process = subprocess.Popen([sys.executable, os.path.abspath(__file__)] + command, env=env,
                                            stdout=log, stderr=subprocess.STDOUT)
            log.close()
        else:
            self.process = mayalaunch.start_mayapy(os.path.abspath(__file__), command, env, maya_dir=running_maya_dir(),
                                                   log_path=self.files["log"])

    def alive(self):
        return self.process.poll() is None

    def finished(self, tier):
        return os.path.exists(self.files["done"] % tier)

    def startup_error(self):
        """The Python error that stopped startup, or None (not started yet, or started fine)."""
        if not os.path.isfile(self.files["startup"]):
            return None
        return read_json_file(self.files["startup"])["error"]

    def started(self):
        return os.path.isfile(self.files["startup"]) and self.startup_error() is None

    def new_outcomes(self):
        """Outcomes written since the last call. Only whole lines count, so a line being written is read next time."""
        try:
            with open(self.files["outcomes"], "rb") as handle:
                handle.seek(self.read_bytes)
                data = handle.read()
        except OSError:
            return []
        whole = data[:data.rfind(b"\n") + 1]
        self.read_bytes += len(whole)
        return [json.loads(line) for line in whole.decode("utf-8").splitlines() if line.strip()]

    def stop(self):
        if self.alive():
            mayalaunch.kill_process_tree(self.process.pid)
            self.process.wait()

    def describe_exit(self, how):
        """What happened to a worker that ended without finishing its tier: (kind, message). kind is "startup" (Maya
        or the plugin did not load) or "crash"."""
        closed = mayalaunch.close_crash_reporters(self.process.pid)
        cleanup = "\nClosed %d crash reporter process(es)." % closed if closed else ""
        error = self.startup_error()
        if error:
            return "startup", error
        stage = read_text(self.files["progress"], "startup: before Maya began").strip()
        if stage.startswith("startup"):
            tail = "".join(read_text(self.files["log"], "(no Maya log was written)").splitlines(True)[-15:])
            return "startup", "Maya %s during %s.%s\n--- end of %s ---\n%s" % (how, stage, cleanup, self.files["log"], tail)
        test_id = stage.split("\t", 1)[1]
        return "crash", "Maya (%s) %s during %s%s\nRerun just that test with RigTests.cmd -v --jobs 1 %s" % (
            self.name, how, test_id, cleanup, test_id.split(".")[-1])


def run_tier(tier, workers, work_dir, out, verbose, deadline):
    """Starts tier on the workers and waits until every one of them has finished it, crashed or the deadline passed.
    Streams one mark per outcome. Returns (outcomes, problems), problems being (kind, message) pairs."""
    modules = tier_modules(tier)
    write_json_file(go_path(work_dir, tier), {"modules": modules})
    outcomes, problems = [], []
    pending = list(workers)

    def collect():
        for worker in workers:
            for item in worker.new_outcomes():
                if item["tier"] != tier:
                    continue
                outcomes.append(item)
                if out:
                    out.write(("    %-8s %s\n" % (item["outcome"], item["id"])) if verbose else OUTCOME_CHARS[item["outcome"]])
                    out.flush()

    while pending:
        collect()
        for worker in list(pending):
            if worker.finished(tier):
                pending.remove(worker)
            elif not worker.alive():
                pending.remove(worker)
                worker.crash_reported = True
                problems.append(worker.describe_exit("exited with code %s" % worker.process.returncode))
        if pending and time.time() > deadline:
            for worker in pending:
                stage = read_text(worker.files["progress"]).strip()
                worker.stop()
                worker.crash_reported = True
                problems.append(("crash", "Maya (%s) stopped after the run's timeout, during %s" % (worker.name, stage)))
            pending = []
        if pending:
            time.sleep(POLL_SECONDS)
    collect()

    claimed = {os.path.basename(p) for p in glob.glob(os.path.join(work_dir, "claims", "%s.*" % tier))}
    unrun = [m for m in modules if "%s.%s" % (tier, m) not in claimed]
    if unrun and problems:
        problems.append(("crash", "Not run, no worker was left to take them: %s" % ", ".join(unrun)))
    return outcomes, problems


def report_tier(out, label, outcomes, problems, verbose):
    counts, text = summarize(outcomes)
    if problems:
        text += "  STOPPED"
    separator = "\n" + " " * len(label) if verbose else ("  " if outcomes else "")
    out.write("%s%s\n" % (separator, text if outcomes or problems else "no tests"))
    write_failures(out, outcomes)
    for _, message in problems:
        out.write(indent(message, "  ") + "\n")
    out.flush()
    return any(counts.get(k) for k in BAD_OUTCOMES) or bool(problems)


def write_durations(out, outcomes, elapsed, count, jobs):
    """Where the time went: the slowest test files and the slowest tests. Each outcome's seconds run from the previous
    outcome's end in the same worker (see TierResult._record), so a test's own setup is counted against it."""
    tests_seconds = sum(item.get("seconds", 0.0) for item in outcomes if item["tier"] != PLAIN_TIER)
    plain_seconds = sum(item.get("seconds", 0.0) for item in outcomes if item["tier"] == PLAIN_TIER)
    out.write("\nTime: %.1f s of Maya tests shared between %d worker(s), %.1f s of plain Python tests alongside, "
              "%.1f s wall\n" % (tests_seconds, jobs, plain_seconds, elapsed))
    by_group = {}
    for item in outcomes:
        total, number = by_group.get(item["group"], (0.0, 0))
        by_group[item["group"]] = (total + item.get("seconds", 0.0), number + 1)
    out.write("Slowest test files:\n")
    for group, (total, number) in sorted(by_group.items(), key=lambda pair: -pair[1][0])[:count]:
        out.write("  %6.2f s  %4d tests  %s\n" % (total, number, group))
    out.write("Slowest tests:\n")
    for item in sorted(outcomes, key=lambda i: -i.get("seconds", 0.0))[:count]:
        out.write("  %6.2f s  %s\n" % (item.get("seconds", 0.0), item["id"]))
    out.flush()


def write_total(out, outcomes, elapsed, logs, stopped=0, durations=0, jobs=1):
    """Writes the TOTAL line and returns True when nothing failed. durations > 0 adds write_durations."""
    counts, _ = summarize(outcomes)
    summary = "TOTAL %d passed" % counts.get("pass", 0)
    for key, label in (("FAIL", "failed"), ("ERROR", "errors"), ("XPASS", "unexpected passes"),
                       ("xfail", "expected failures"), ("skip", "skipped")):
        if counts.get(key):
            summary += ", %d %s" % (counts[key], label)
    if stopped:
        summary += ", Maya stopped %d time(s)" % stopped
    out.write("\n%s   (%.1f s)\nLogs: %s\n" % (summary, elapsed, logs))
    if durations:
        write_durations(out, outcomes, elapsed, durations, jobs)
    out.flush()
    return not stopped and not any(counts.get(k) for k in BAD_OUTCOMES)


def check_tier_table():
    """Every test file is in exactly one tier, and every tier entry exists."""
    on_disk = set()
    for path in glob.glob(os.path.join(MAYA_TESTS_DIR, "*", "test_*.py")):
        rel = os.path.relpath(path, MAYA_TESTS_DIR)
        on_disk.add(os.path.splitext(rel)[0].replace(os.sep, "."))
    listed = [m for _, _, modules in TIERS for m in modules] + PLAIN_PYTHON
    problems = []
    problems += ["%s is not in any tier. Add it to TIERS in run_tests.py." % m for m in sorted(on_disk - set(listed))]
    problems += ["TIERS lists %s, which does not exist." % m for m in sorted(set(listed) - on_disk)]
    problems += ["%s is listed in more than one tier." % m for m in sorted({m for m in listed if listed.count(m) > 1})]
    return problems


def stale_sources(plugin):
    """Source files newer than the plugin: testing an old build has hidden changes before."""
    built = os.path.getmtime(plugin)
    sources = []
    for folder in ("core", "maya"):
        for extension in ("*.cpp", "*.h"):
            sources += glob.glob(os.path.join(CPP_DIR, folder, "**", extension), recursive=True)
    return sorted(os.path.relpath(s, CPP_DIR) for s in sources if os.path.getmtime(s) > built)


def new_run_folder():
    work_dir = tempfile.mkdtemp(prefix="RigTests_")
    os.makedirs(os.path.join(work_dir, "claims"))
    return work_dir


def start_plain_workers(args, work_dir):
    """One plain worker per plain file, so the slow ones (the launcher's real mayapy, the core C++ build) overlap."""
    workers = [Worker("plain%d" % (i + 1), args, work_dir, None, plain=True) for i in range(len(PLAIN_PYTHON))]
    write_json_file(go_path(work_dir, PLAIN_TIER), {"modules": PLAIN_PYTHON})
    return workers


def finish_plain(workers, work_dir, out, verbose, deadline):
    """Waits for the plain workers and writes their line. Returns their outcomes, plus an ERROR for a worker that
    died or hung, so a missing result is never a pass by absence."""
    outcomes, problems = run_tier(PLAIN_TIER, workers, work_dir, None, verbose, deadline)
    for kind, message in problems:
        outcomes.append({"tier": PLAIN_TIER, "id": "plain python worker", "group": "plain", "outcome": "ERROR",
                         "detail": message, "seconds": 0.0})
    out.write(PLAIN_LABEL)
    report_tier(out, PLAIN_LABEL, outcomes, [], verbose)
    return outcomes


def run_parent(args):
    out = sys.stdout
    problems = check_tier_table()
    if problems:
        out.write("Test tier table is out of date:\n" + "\n".join("  " + p for p in problems) + "\n")
        return 2
    if not os.path.isfile(args.plugin):
        out.write("Plugin not found: %s\nBuild the Release configuration first.\n" % args.plugin)
        return 2
    jobs = max(1, args.jobs)

    work_dir = new_run_folder()
    plugin_copy = mayalaunch.copy_plugin(args.plugin, work_dir)

    shown = os.path.abspath(args.plugin)
    if shown.lower().startswith(REPO_DIR.lower() + os.sep):
        shown = os.path.relpath(shown, REPO_DIR)
    out.write("RigModules tests | %s (built %s) | %d Maya worker(s)\n" % (
        shown, time.strftime("%Y-%m-%d %H:%M", time.localtime(os.path.getmtime(args.plugin))), jobs))
    stale = stale_sources(args.plugin)
    if stale:
        out.write("WARNING: sources newer than the plugin, rebuild first: %s\n" % ", ".join(stale))
    if args.filters:
        out.write("Filtered run: only matching tests run, and a failed tier still blocks the later ones.\n")
    out.write("\n")
    out.flush()

    start = time.time()
    deadline = start + args.timeout
    plain = start_plain_workers(args, work_dir)
    names = iter("maya%d" % i for i in range(1, 1000))
    workers = [Worker(next(names), args, work_dir, plugin_copy, plain=False) for _ in range(jobs)]
    all_workers = list(workers)
    outcomes, stopped, blocked_by = [], 0, None
    for tier in range(len(TIERS)):
        label = tier_label(tier)
        if blocked_by:
            out.write("%sNOT RUN (blocked by %s)\n" % (label, blocked_by))
            continue
        workers = [w for w in workers if not w.crash_reported]
        if not workers:   # --keep-going after every worker crashed: start new ones for the rest of the run
            # They pass through the earlier tiers quickly: every file there is claimed already.
            workers = [Worker(next(names), args, work_dir, plugin_copy, plain=False) for _ in range(jobs)]
            all_workers += workers
        out.write(label)
        out.flush()
        tier_outcomes, problems = run_tier(tier, workers, work_dir, out, args.verbose, deadline)
        outcomes += tier_outcomes
        if any(kind == "startup" for kind, _ in problems):
            out.write("STARTUP FAILED\n" + indent(next(m for k, m in problems if k == "startup"), "  ") + "\n")
            stopped += 1
            blocked_by = "startup"
            continue
        stopped += sum(1 for kind, message in problems if kind == "crash" and message.startswith("Maya"))
        failed = report_tier(out, label, tier_outcomes, problems, args.verbose)
        if failed and TIERS[tier][1] and not args.keep_going:
            blocked_by = "tier %d %s" % (tier + 1, TIERS[tier][0])

    write_text(stop_path(work_dir), "")
    outcomes += finish_plain(plain, work_dir, out, args.verbose, deadline)
    exit_by = time.time() + EXIT_GRACE_SECONDS
    for worker in all_workers:
        try:
            worker.process.wait(timeout=max(0.0, exit_by - time.time()))
        except subprocess.TimeoutExpired:
            worker.stop()
    ok = write_total(out, outcomes, time.time() - start, work_dir, stopped, args.durations, jobs)
    return 0 if ok else 1


def run_in_current_maya(filters=(), verbose=False, keep_going=False, durations=0):
    """Runs the whole suite inside a mayapy that is already running with the plugin loaded, for example
    at the end of a script that just checked a change, so Maya starts once. Returns True if nothing failed.

        sys.path.insert(0, r"<repo>\\tests")
        import run_tests
        ok = run_tests.run_in_current_maya()

    The Maya tiers run one after another in this process (no workers); the plain Python files run in their workers
    beside it. Call it last: every test starts with file -new, so the caller's scene is gone afterwards. There is
    no crash protection here, so a crash ends the calling script. RigTests.cmd is the protected run.
    Refuses to run in interactive Maya, where file -new would throw away the user's scene."""
    from maya import cmds, mel
    if not cmds.about(batch=True):
        raise RuntimeError("run_in_current_maya only runs in mayapy or maya -batch, never in interactive Maya.")
    if not mel.eval('exists "createRigFromTemplate"'):
        raise RuntimeError("Load the RigModules plugin before running the tests.")
    problems = check_tier_table()
    if problems:
        raise RuntimeError("Test tier table is out of date:\n" + "\n".join(problems))

    work_dir = new_run_folder()
    files = worker_files(work_dir, "current")
    configure_rigtest(files["dir"], update_golden=False)
    report = os.fdopen(os.dup(1), "w", encoding="utf-8", errors="replace")
    report.write("\nRigModules tests | in the current Maya session\n\n")
    start = time.time()

    class Args:
        pass
    args = Args()
    args.filters, args.update_golden = list(filters), False
    plain = start_plain_workers(args, work_dir)
    redirect = mayalaunch.Redirect(files["log"])
    outcomes, blocked_by = [], None
    try:
        for tier in range(len(TIERS)):
            label = tier_label(tier)
            if blocked_by:
                report.write("%sNOT RUN (blocked by %s)\n" % (label, blocked_by))
                continue
            report.write(label)
            report.flush()
            tier_outcomes = run_modules(TIERS[tier][2], tier, files, list(filters), report, verbose)
            outcomes += tier_outcomes
            if report_tier(report, label, tier_outcomes, [], verbose) and TIERS[tier][1] and not keep_going:
                blocked_by = "tier %d %s" % (tier + 1, TIERS[tier][0])
    finally:
        redirect.stop()
    outcomes += finish_plain(plain, work_dir, report, verbose, time.time() + DEFAULT_TIMEOUT_SECONDS)
    ok = write_total(report, outcomes, time.time() - start, work_dir, durations=durations)
    report.close()
    return ok


if __name__ == "__main__":
    arguments = parse_args()
    if arguments.worker:
        run_worker(arguments)
    sys.exit(run_parent(arguments))
