"""Data-driven tests. Each JSON case file becomes one test method named after the file.

Three case types, each with a fixed schema (documented in tests/README.md):
  pose cases        fixtures/poses/<module>/*.json   pose controls, check world transforms
  validation cases  fixtures/validation/*.json       edit a template, check the build errors
  golden snapshots  golden/*.json                    a rig's public names, compared to a checked-in file

Expected values in pose cases are written by hand. Only golden snapshots are ever generated
(run_tests.py --update-golden), because recording a pose would turn a bug into the expected result.
"""
import difflib
import glob
import json
import os

from maya import cmds

from rigtest import config, fixtures
from rigtest.case import rig_name


def _add_case_methods(cls, pattern, run_case, keep=None):
    """Adds test_<file stem> to cls for every file matching pattern (and accepted by keep, if given)."""
    paths = [p for p in sorted(glob.glob(pattern)) if keep is None or keep(p)]
    if not paths:
        raise fixtures.FixtureError("no case files match %s" % pattern)
    for path in paths:
        stem = os.path.splitext(os.path.basename(path))[0]

        def test(self, path=path):
            run_case(self, path)

        test.__name__ = "test_" + stem
        test.__doc__ = None
        setattr(cls, test.__name__, test)
    return cls


# ---- pose cases ------------------------------------------------------------------------------

def _run_pose_case(test, path):
    case = fixtures.read_json(path)
    fixtures.check_keys(case, path, ["description", "skeleton", "template", "expect"], ["set", "tolerance"])
    tolerance = case.get("tolerance", 1e-4)
    test.build_standard(case["template"], case["skeleton"])
    for plug, value in case.get("set", {}).items():
        cmds.setAttr(rig_name(plug), value)
    for node, channels in case["expect"].items():
        fixtures.check_keys(channels, "%s expect.%s" % (path, node), [], ["translate", "rotate", "scale"])
        full = rig_name(node)
        test.assertExists(full)
        if "translate" in channels:
            test.assertWorldTranslation(full, channels["translate"], tolerance)
        if "rotate" in channels:
            test.assertWorldRotation(full, channels["rotate"], tolerance)
        if "scale" in channels:
            test.assertWorldScale(full, channels["scale"], tolerance)


def add_pose_cases(cls, module_folder):
    """Adds one test per fixtures/poses/<module_folder>/*.json."""
    return _add_case_methods(cls, os.path.join(config.FIXTURES_DIR, "poses", module_folder, "*.json"), _run_pose_case)


# ---- validation cases ------------------------------------------------------------------------

def _run_validation_case(test, path):
    case = fixtures.read_json(path)
    fixtures.check_keys(case, path, ["description", "base", "skeleton", "expectErrors"], ["edits"])
    template = fixtures.apply_edits(fixtures.load_template(case["base"]), case.get("edits", []))
    joints = test.make_skeleton(case["skeleton"])
    label = os.path.splitext(os.path.basename(path))[0]

    if not case["expectErrors"]:
        test.build(template, joints, label=label)
        test.assertExists(rig_name("layout"))
        return

    message = test.assertMayaError(lambda: test.build(template, joints, label=label), "Nothing was built")
    lines = message.splitlines()
    for index, expected in enumerate(case["expectErrors"]):
        fixtures.check_keys(expected, "%s expectErrors[%d]" % (path, index), ["contains"], ["path"])
        prefix = expected.get("path", "")
        matched = any(line.startswith(prefix) and expected["contains"] in line[len(prefix):] for line in lines)
        if not matched:
            want = ("an error at %r" % prefix if prefix else "an error") + " containing %r" % expected["contains"]
            test.fail("expected %s\n--- errors were ---\n%s" % (want, message))
    test.assertNothingBuilt()
    test.assertFalse(cmds.namespace(exists=":" + config.RIG_NAMESPACE),
                     "a failed validation must not create the rig namespace")


def _expects_errors(path):
    return bool(fixtures.read_json(path).get("expectErrors"))


def add_validation_cases(cls, expect_build):
    """Adds one test per fixtures/validation/*.json. Cases that expect errors never build a rig, so they
    run in the read-only tier. Cases with "expectErrors": [] build one, so they run in the build tier."""
    return _add_case_methods(cls, os.path.join(config.FIXTURES_DIR, "validation", "*.json"), _run_validation_case,
                             keep=lambda p: _expects_errors(p) != expect_build)


# ---- golden interface snapshots --------------------------------------------------------------

MODULE_TYPES = ["layoutModule", "fkChainModule", "aimModule"]


def _short(node):
    return node.split("|")[-1].split(":")[-1]


def _multi_strings(node, attr, child=None):
    values = []
    for index in cmds.getAttr("%s.%s" % (node, attr), multiIndices=True) or []:
        plug = "%s.%s[%d]" % (node, attr, index)
        values.append(cmds.getAttr(plug + "." + child if child else plug))
    return values


def rig_interface(namespace=config.RIG_NAMESPACE):
    """Every name animation, pose and export tools can depend on. Root nodes are left out:
    their names are numbered by Maya and are not public."""
    pattern = rig_name("*", namespace)
    modules = {}
    for node in cmds.ls(pattern, type=MODULE_TYPES) or []:
        modules[_short(node)] = {
            "type": cmds.nodeType(node),
            "sockets": [json.loads(text) for text in _multi_strings(node, "socketNames")],
            "childPlugs": _multi_strings(node, "childPlugs", "childPlugName"),
        }
    controls = {}
    for node in cmds.ls(pattern, type="rigControlNode") or []:
        entry = {"keyable": cmds.listAttr(node, keyable=True) or []}
        control_markup = json.loads(cmds.getAttr(node + ".controlMarkup"))
        entry["markup"] = [control_markup["side"], control_markup["usage"]]
        if cmds.attributeQuery("space", node=node, exists=True):
            entry["spaces"] = cmds.attributeQuery("space", node=node, listEnum=True)[0].split(":")
        controls[_short(node)] = entry
    constraints = {}
    for node in cmds.ls(pattern, type="rigConstraint") or []:
        constraints[_short(node)] = _multi_strings(node, "drivers", "driverLabel")
    return {
        "modules": modules,
        "controls": controls,
        "joints": sorted(_short(j) for j in cmds.ls(pattern, type="joint") or []),
        "constraints": constraints,
    }


def _readable_json(value, depth=0):
    """Objects one key per line, lists on one line, so a golden diff shows one changed name per line."""
    if isinstance(value, dict) and value:
        pad = "  " * (depth + 1)
        items = ["%s%s: %s" % (pad, json.dumps(k), _readable_json(v, depth + 1)) for k, v in value.items()]
        return "{\n%s\n%s}" % (",\n".join(items), "  " * depth)
    return json.dumps(value, separators=(", ", ": "))


def _run_golden_case(test, path):
    case = fixtures.read_json(path)
    fixtures.check_keys(case, path, ["description", "template", "skeleton"], ["interface"])
    test.build_standard(case["template"], case["skeleton"])
    actual = rig_interface()

    if config.UPDATE_GOLDEN:
        case["interface"] = actual
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(_readable_json(case) + "\n")
        return

    if "interface" not in case:
        test.fail("%s has no interface yet. Run RigTests.cmd --update-golden and review the result." % path)
    expected_text = json.dumps(case["interface"], indent=2, sort_keys=True).splitlines()
    actual_text = json.dumps(actual, indent=2, sort_keys=True).splitlines()
    if expected_text != actual_text:
        diff = "\n".join(difflib.unified_diff(expected_text, actual_text, "golden", "built", lineterm="", n=2))
        test.fail("public rig names changed. This breaks saved poses, animation and exports.\n"
                  "If the change is intended, run --update-golden and record it in design/decisions.md.\n" + diff)


def add_golden_cases(cls):
    """Adds one test per golden/*.json."""
    return _add_case_methods(cls, os.path.join(config.GOLDEN_DIR, "*.json"), _run_golden_case)


def _run_smoke_case(test, path):
    case = fixtures.read_json(path)
    test.build_standard(case["template"], case["skeleton"])
    test.assertExists(rig_name("layout"))


def add_smoke_cases(cls):
    """Adds one build-only test per golden/*.json, so every template and skeleton pair a golden file
    names is proven to build before any behaviour test runs."""
    return _add_case_methods(cls, os.path.join(config.GOLDEN_DIR, "*.json"), _run_smoke_case)
