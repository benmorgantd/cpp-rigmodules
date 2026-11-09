"""Runs the RigModules test suite in tiers. Start it through RigTests.cmd.

Usage: mayapy run_tests.py [filter ...] [-v] [--keep-going] [--update-golden] [--plugin PATH]

Tiers run in the order of TIERS, in one mayapy process. A tier runs only if every earlier gating tier
passed, so a broken plugin or build is reported once at its root instead of as a failure in every later
test.

This script starts that mayapy process as a child and watches it. The child writes each outcome as it
happens and records the running test before it starts, so if Maya crashes the report still names the
test and keeps every earlier result. Maya's crash reporter is turned off for the child and its temp
files go to the run folder, so a crash leaves no dialog and nothing in the user's Temp folder.

A filter keeps tests whose id contains it (case-insensitive). Filtered runs do not run the earlier
tiers as prerequisites.
"""
import argparse
import glob
import json
import os
import shutil
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
DEFAULT_PLUGIN = os.path.join(CPP_DIR, "x64", "Release", "RigModules.mll")
# The rigmodules Python package, tested together with the plugin it wraps (D24).
PYTHON_DIR = os.path.join(REPO_DIR, "python")

# Every test file belongs to exactly one tier. Order is dependency order: a tier may assume every
# earlier tier passed. A file missing from this table fails the run, so new suites must be placed.
# (name, gates later tiers, test modules)
TIERS = [
    ("registration", True, [
        "concepts.test_plugin",
        "concepts.test_source_layout",     # core/ stays Maya-free; reads files, not the scene
    ]),
    ("read only", True, [                  # commands and checks that never build a rig
        "commands.test_joint_markup_cmd",
        "concepts.test_control_shapes",     # rigControlNode shapes and bounds, no rig
        "concepts.test_template_loading",
        "concepts.test_template_validation",
        "concepts.test_python_template",    # rigmodules.template reads a file, builds nothing
    ]),
    ("build smoke", True, [                # every fixture template builds
        "concepts.test_smoke",
        "commands.test_create_rig",
    ]),
    ("modules", True, [                    # what each module does, and the rig structure around it
        "modules.test_layout",
        "modules.test_fkchain",
        "modules.test_aim",
        "concepts.test_sockets",
        "concepts.test_naming",
        "concepts.test_undo_redo",
        "commands.test_rig_control_cmds",   # control markup, traversal and shape files on a built rig
        "commands.test_rig_info",           # getRigInfo and the rigmodules Python query
    ]),
    ("constraints", True, [
        "commands.test_constraint_cmds",
        "commands.test_constraint_types",  # Orient and Point (D31)
        "concepts.test_docs_examples",    # the README quick start ends with a space switch
        "concepts.test_mirror",            # mirrored templates include mirrored constraints (D28)
    ]),
    # Animation, then import/export tiers go here, after constraints.
    ("known bugs", False, [                # expected failures, never block anything
        "known.test_known_bugs",
    ]),
]

OUTCOME_CHARS = {"pass": ".", "FAIL": "F", "ERROR": "E", "xfail": "x", "XPASS": "X", "skip": "s"}
BAD_OUTCOMES = ("FAIL", "ERROR", "XPASS")
FILES = {"results": "results.json", "progress": "progress.txt", "outcomes": "outcomes.jsonl", "log": "maya_output.log"}

# Processes Maya's crash handler starts. Disabled through MAYA_DISABLE_CER, and closed after a crash in case.
CRASH_REPORTER_PROCESSES = ("senddmp.exe", "cer_dialog.exe")


def parse_args():
    parser = argparse.ArgumentParser(description="Run the RigModules mayapy test suite.")
    parser.add_argument("filters", nargs="*", help="keep tests whose id contains any of these")
    parser.add_argument("-v", "--verbose", action="store_true", help="print each test as it runs")
    parser.add_argument("--keep-going", action="store_true", help="run every tier even after one fails")
    parser.add_argument("--update-golden", action="store_true", help="rewrite golden interface snapshots")
    parser.add_argument("--plugin", default=DEFAULT_PLUGIN, help="plugin to test (default: Release build)")
    # Internal: the parent starts the Maya process with these.
    parser.add_argument("--child-from-tier", type=int, help=argparse.SUPPRESS)
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


def read_outcomes(path):
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


# ==== child: runs the tiers inside Maya ==========================================================

def group_of(test):
    """Report group: the test file name without its test_ prefix, e.g. 'fkchain'."""
    test = getattr(test, "test_case", test)   # a subTest reports under its parent test
    return type(test).__module__.split(".")[-1].replace("test_", "", 1)


class Redirect:
    """Sends fd 1 and 2 (Maya's C++ output and Python's print) to a log file until stop()."""

    def __init__(self, log_path):
        sys.stdout.flush()
        sys.stderr.flush()
        self.saved = (os.dup(1), os.dup(2))
        self.log = open(log_path, "a", encoding="utf-8", errors="replace")
        os.dup2(self.log.fileno(), 1)
        os.dup2(self.log.fileno(), 2)

    def stop(self):
        sys.stdout.flush()
        sys.stderr.flush()
        os.dup2(self.saved[0], 1)
        os.dup2(self.saved[1], 2)
        os.close(self.saved[0])
        os.close(self.saved[1])
        self.log.close()


class TierResult(unittest.TestResult):
    """Appends each outcome to the outcomes file as it happens (so a crash keeps every earlier result),
    writes the running test to the progress file, and streams one mark per test."""

    def __init__(self, out, files, tier, verbose):
        super().__init__()
        self.out = out
        self.files = files
        self.tier = tier
        self.verbose = verbose
        self.outcomes = []

    def _record(self, test, outcome, detail=""):
        item = {"tier": self.tier, "id": test.id(), "group": group_of(test), "outcome": outcome, "detail": detail}
        self.outcomes.append(item)
        append_outcome(self.files["outcomes"], item)
        self.out.write(("    %-8s %s\n" % (outcome, test.id())) if self.verbose else OUTCOME_CHARS[outcome])
        self.out.flush()

    def startTest(self, test):
        super().startTest(test)
        # Written before the test runs: if Maya crashes, the parent reads which test it was.
        with open(self.files["progress"], "w", encoding="utf-8") as handle:
            handle.write("%d\t%s" % (self.tier, test.id()))
        if self.verbose:
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


def load_tier_tests(tier, filters):
    """Returns (tests, load errors) for one tier. A module that fails to import is reported, not fatal."""
    loader = unittest.TestLoader()
    tests, errors = [], []
    for name in TIERS[tier][2]:
        try:
            suite = loader.loadTestsFromName(name)
        except Exception:
            errors.append({"tier": tier, "id": name, "group": name.split(".")[-1].replace("test_", "", 1),
                           "outcome": "ERROR", "detail": "could not load:\n" + traceback.format_exc()})
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


def configure_rigtest(work_dir, update_golden):
    for folder in (MAYA_TESTS_DIR, PYTHON_DIR):
        if folder not in sys.path:
            sys.path.insert(0, folder)
    from rigtest import config
    config.WORK_DIR = work_dir
    config.UPDATE_GOLDEN = update_golden


def run_tiers(report, files, first_tier, filters, verbose, keep_going):
    """Runs tiers in order from first_tier inside the current Maya, writing the report as it goes.
    A tier runs only if every earlier gating tier passed, unless keep_going."""
    blocked_by = None
    for tier in range(first_tier, len(TIERS)):
        label = tier_label(tier)
        if blocked_by:
            report.write("%sNOT RUN (blocked by %s)\n" % (label, blocked_by))
            continue
        report.write(label)
        report.flush()
        with open(files["progress"], "w", encoding="utf-8") as handle:
            handle.write("%d\tloading tests" % tier)
        tests, load_errors = load_tier_tests(tier, filters)
        for error in load_errors:
            append_outcome(files["outcomes"], error)
        result = TierResult(report, files, tier, verbose)
        unittest.TestSuite(tests).run(result)
        outcomes = load_errors + result.outcomes
        counts, text = summarize(outcomes)
        separator = "\n" + " " * len(label) if verbose else ("  " if outcomes else "")
        report.write("%s%s\n" % (separator, text if outcomes else "no tests"))
        write_failures(report, outcomes)
        report.flush()
        if any(counts.get(k) for k in BAD_OUTCOMES) and TIERS[tier][1] and not keep_going:
            blocked_by = "tier %d %s" % (tier + 1, TIERS[tier][0])


def write_total(out, outcomes, elapsed, log_path, stopped=0):
    """Writes the TOTAL line and returns True when nothing failed."""
    counts, _ = summarize(outcomes)
    summary = "TOTAL %d passed" % counts.get("pass", 0)
    for key, label in (("FAIL", "failed"), ("ERROR", "errors"), ("XPASS", "unexpected passes"),
                       ("xfail", "expected failures"), ("skip", "skipped")):
        if counts.get(key):
            summary += ", %d %s" % (counts[key], label)
    if stopped:
        summary += ", Maya stopped %d time(s)" % stopped
    out.write("\n%s   (%.1f s)\nMaya log: %s\n" % (summary, elapsed, log_path))
    out.flush()
    return not stopped and not any(counts.get(k) for k in BAD_OUTCOMES)


def run_in_current_maya(filters=(), verbose=False, keep_going=False):
    """Runs the whole suite inside a mayapy that is already running with the plugin loaded, for example
    at the end of a script that just checked a change, so Maya starts once. Returns True if nothing failed.

        sys.path.insert(0, r"<repo>\\tests")
        import run_tests
        ok = run_tests.run_in_current_maya()

    Call it last: every test starts with file -new, so the caller's scene is gone afterwards. There is
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

    work_dir = tempfile.mkdtemp(prefix="RigTests_")
    files = {k: os.path.join(work_dir, v) for k, v in FILES.items()}
    configure_rigtest(work_dir, update_golden=False)
    report = os.fdopen(os.dup(1), "w", encoding="utf-8", errors="replace")
    report.write("\nRigModules tests | in the current Maya session\n\n")
    start = time.time()
    redirect = Redirect(files["log"])
    try:
        run_tiers(report, files, 0, list(filters), verbose, keep_going)
    finally:
        redirect.stop()
    ok = write_total(report, read_outcomes(files["outcomes"]), time.time() - start, files["log"])
    report.close()
    return ok


def run_child(args):
    """Runs the tiers from --child-from-tier on and writes the results file. Maya output goes to the log,
    and stays redirected until the process exits so anything Maya prints on the way out is logged too."""
    files = {k: os.path.join(args.work_dir, v) for k, v in FILES.items()}
    report = os.fdopen(os.dup(1), "w", encoding="utf-8", errors="replace")
    Redirect(files["log"])

    def progress(text):
        with open(files["progress"], "w", encoding="utf-8") as handle:
            handle.write(text)

    results = {"startupError": None}
    try:
        progress("startup: initializing Maya")
        import maya.standalone
        maya.standalone.initialize(name="python")
        from maya import cmds
        progress("startup: loading the plugin")
        cmds.loadPlugin(args.plugin)
        configure_rigtest(args.work_dir, args.update_golden)
    except Exception:
        results["startupError"] = traceback.format_exc()
    else:
        run_tiers(report, files, args.child_from_tier, args.filters, args.verbose, args.keep_going)

    with open(files["results"], "w", encoding="utf-8") as handle:
        json.dump(results, handle)
    report.close()
    # Maya 2026 crashes at process exit without uninitialize().
    try:
        import maya.standalone
        maya.standalone.uninitialize()
    except Exception:
        traceback.print_exc()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0)


# ==== parent: starts Maya, watches it, and reports crashes =======================================

def check_tier_table():
    """Every test file is in exactly one tier, and every tier entry exists."""
    on_disk = set()
    for path in glob.glob(os.path.join(MAYA_TESTS_DIR, "*", "test_*.py")):
        rel = os.path.relpath(path, MAYA_TESTS_DIR)
        on_disk.add(os.path.splitext(rel)[0].replace(os.sep, "."))
    listed = [m for _, _, modules in TIERS for m in modules]
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


def mayapy_path():
    exe = sys.executable
    if "mayapy" in os.path.basename(exe).lower():
        return exe
    return os.path.join(os.path.dirname(exe), "mayapy.exe")


def child_environment(work_dir):
    env = dict(os.environ)
    # No crash report dialog, and crash dumps and Maya's temp files stay in the run folder.
    env["MAYA_DISABLE_CER"] = "1"
    maya_temp = os.path.join(work_dir, "maya_temp")
    os.makedirs(maya_temp, exist_ok=True)
    env["TEMP"] = maya_temp
    env["TMP"] = maya_temp
    return env


def close_crash_reporters(child_pid):
    """Closes crash reporter processes the crashed child started. Returns how many were closed.
    Matches on parent process id, so the user's own Maya sessions are never touched."""
    script = (
        "$ids = @(%d); $found = @();"
        "do { $next = Get-CimInstance Win32_Process | Where-Object { $ids -contains $_.ParentProcessId -and $found -notcontains $_.ProcessId };"
        " $found += $next.ProcessId; $ids = $next.ProcessId } while ($next);"
        "$found | Where-Object { $_ } | ForEach-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue } |"
        " Where-Object { @('%s') -contains ($_.ProcessName + '.exe') } |"
        " ForEach-Object { Stop-Process -Id $_.Id -Force; $_.Id }"
    ) % (child_pid, "','".join(CRASH_REPORTER_PROCESSES))
    output = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
                            capture_output=True, text=True).stdout
    return len([line for line in output.splitlines() if line.strip()])


def log_tail(path, lines=15):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return "".join(handle.readlines()[-lines:])
    except OSError:
        return "(no Maya log was written)"


def run_maya(args, work_dir, plugin_copy, from_tier):
    """Runs tiers from from_tier in one mayapy process.
    Returns (kind, tier, message): kind is "ok", "startup" (Maya or the plugin did not load) or "crash"."""
    files = {k: os.path.join(work_dir, v) for k, v in FILES.items()}
    for key in ("results", "progress"):
        if os.path.exists(files[key]):
            os.remove(files[key])
    command = [mayapy_path(), os.path.abspath(__file__), "--child-from-tier", str(from_tier),
               "--work-dir", work_dir, "--plugin", plugin_copy] + args.filters
    command += ["-v"] if args.verbose else []
    command += ["--keep-going"] if args.keep_going else []
    command += ["--update-golden"] if args.update_golden else []
    child = subprocess.Popen(command, env=child_environment(work_dir))
    exit_code = child.wait()

    if os.path.isfile(files["results"]):
        with open(files["results"], encoding="utf-8") as handle:
            error = json.load(handle)["startupError"]
        return ("startup", from_tier, error) if error else ("ok", None, None)

    closed = close_crash_reporters(child.pid)
    cleanup = "\nClosed %d crash reporter process(es)." % closed if closed else ""
    try:
        with open(files["progress"], encoding="utf-8") as handle:
            stage = handle.read().strip()
    except OSError:
        stage = "startup: before Maya began"
    if stage.startswith("startup"):
        return "startup", from_tier, "Maya exited (code %s) during %s.%s\n--- end of Maya log ---\n%s" % (
            exit_code, stage, cleanup, log_tail(files["log"]))
    tier, test_id = stage.split("\t", 1)
    return "crash", int(tier), "Maya stopped unexpectedly (exit code %s) during %s%s\nRerun just that test with RigTests.cmd -v %s" % (
        exit_code, test_id, cleanup, test_id.split(".")[-1])


def run_parent(args):
    out = sys.stdout
    problems = check_tier_table()
    if problems:
        out.write("Test tier table is out of date:\n" + "\n".join("  " + p for p in problems) + "\n")
        return 2
    if not os.path.isfile(args.plugin):
        out.write("Plugin not found: %s\nBuild the Release configuration first.\n" % args.plugin)
        return 2

    work_dir = tempfile.mkdtemp(prefix="RigTests_")
    # A loaded plugin locks its file, so load a copy and leave the build output free.
    plugin_copy = os.path.join(work_dir, "RigModules.mll")
    shutil.copy(args.plugin, plugin_copy)

    shown = os.path.abspath(args.plugin)
    if shown.lower().startswith(REPO_DIR.lower() + os.sep):
        shown = os.path.relpath(shown, REPO_DIR)
    out.write("RigModules tests | %s (built %s)\n" % (
        shown, time.strftime("%Y-%m-%d %H:%M", time.localtime(os.path.getmtime(args.plugin)))))
    stale = stale_sources(args.plugin)
    if stale:
        out.write("WARNING: sources newer than the plugin, rebuild first: %s\n" % ", ".join(stale))
    if args.filters:
        out.write("Filtered run: earlier tiers are not run as prerequisites.\n")
    out.write("\n")
    out.flush()

    start = time.time()
    stopped = 0
    from_tier = 0
    while from_tier < len(TIERS):
        kind, tier, problem = run_maya(args, work_dir, plugin_copy, from_tier)
        if kind == "ok":
            break
        stopped += 1
        out.write("startup  FAILED\n" if kind == "startup" else "  STOPPED\n")
        out.write(indent(problem, "  ") + "\n")
        out.flush()
        # A crash ends the Maya process. --keep-going starts a new one at the next tier.
        if kind == "crash" and args.keep_going and tier + 1 < len(TIERS):
            from_tier = tier + 1
            continue
        first_blocked, reason = (tier, "startup") if kind == "startup" else (tier + 1, "a crash in tier %d" % (tier + 1))
        for later in range(first_blocked, len(TIERS)):
            out.write("%sNOT RUN (blocked by %s)\n" % (tier_label(later), reason))
        break

    outcomes = read_outcomes(os.path.join(work_dir, FILES["outcomes"]))
    ok = write_total(out, outcomes, time.time() - start, os.path.join(work_dir, FILES["log"]), stopped)
    return 0 if ok else 1


if __name__ == "__main__":
    arguments = parse_args()
    if arguments.child_from_tier is not None:
        run_child(arguments)
    sys.exit(run_parent(arguments))
