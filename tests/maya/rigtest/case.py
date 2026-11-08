"""RigTestCase: a fresh scene per test plus assertions that print readable failures."""
import unittest

from maya import cmds
import maya.api.OpenMaya as om

from rigtest import config, fixtures

TOLERANCE = 1e-4


def rig_name(short, namespace=config.RIG_NAMESPACE):
    """'fkchain1_ctrl_c' -> ':rig:fkchain1_ctrl_c'. Root namespace ':' gives ':fkchain1_ctrl_c'."""
    if namespace in (":", ""):
        return ":" + short
    return ":%s:%s" % (namespace.strip(":"), short)


def fmt(values):
    return "(%s)" % ", ".join("%.4g" % v for v in values)


class RigTestCase(unittest.TestCase):
    """Every test starts from an empty scene with infinite undo."""

    longMessage = False

    def setUp(self):
        cmds.file(new=True, force=True)
        cmds.namespace(setNamespace=":")
        cmds.undoInfo(state=True, infinity=True)

    # ---- scene setup -------------------------------------------------------------------------

    def make_skeleton(self, name, namespace=config.SKELETON_NAMESPACE, mark=True):
        """Builds fixtures/skeletons/<name>.json and returns absolute joint names in file order."""
        if namespace and not cmds.namespace(exists=":" + namespace):
            cmds.namespace(add=":" + namespace)
        prefix = ":%s:" % namespace if namespace else ":"
        created = []
        for joint in fixtures.load_skeleton(name):
            cmds.select(clear=True)
            if joint["parent"]:
                cmds.select(prefix + joint["parent"])
            # position is world space whether or not the joint has a parent
            node = cmds.joint(name=prefix + joint["name"], position=joint["translate"])
            full = prefix + node.split(":")[-1].split("|")[-1]
            # World rotation, set before any child exists so children keep their world positions
            if any(joint["rotate"]):
                cmds.xform(full, worldSpace=True, rotation=joint["rotate"])
            if mark:
                cmds.rigJointMarkup(full, usage=joint["usage"], side=joint["side"],
                                    export=joint["export"], skin=joint["skin"])
            created.append(full)
        cmds.select(clear=True)
        return created

    def build(self, template, joints, namespace=None, label="template", asset=config.ASSET_ID):
        """Runs createRigFromTemplate. template is a fixture name, a 'RigTemplates/..' path or a dict."""
        if isinstance(template, dict):
            path = fixtures.write_template(template, label)
        else:
            path = fixtures.template_path(template).replace("\\", "/")
        kwargs = {"template": path, "asset": asset}
        if namespace is not None:
            kwargs["namespace"] = namespace
        return cmds.createRigFromTemplate(*joints, **kwargs)

    def build_standard(self, template, skeleton, namespace=None):
        """The common case: build skeleton in 'skel', then the template into the rig namespace."""
        joints = self.make_skeleton(skeleton)
        self.build(template, joints, namespace=namespace)
        return joints

    # ---- assertions --------------------------------------------------------------------------

    def assertMayaError(self, fn, *contains):
        """fn must raise a Maya command error whose text contains every string in contains."""
        try:
            fn()
        except (RuntimeError, TypeError) as error:
            message = str(error).strip()
        else:
            self.fail("expected a Maya error, but the command succeeded")
        for text in contains:
            if text not in message:
                self.fail("error text is missing %r\n--- error was ---\n%s" % (text, message))
        return message

    def assertNothingBuilt(self):
        leftovers = cmds.ls(type=config.RIG_NODE_TYPES) or []
        if leftovers:
            self.fail("expected no rig nodes, found: %s" % ", ".join(sorted(leftovers)))

    def assertExists(self, node):
        if not cmds.objExists(node):
            self.fail("node does not exist: %s" % node)

    def assertNotExists(self, node):
        if cmds.objExists(node):
            self.fail("node should not exist: %s" % node)

    def assertWorldTranslation(self, node, expected, tolerance=TOLERANCE):
        actual = cmds.xform(node, query=True, worldSpace=True, translation=True)
        if any(abs(a - e) > tolerance for a, e in zip(actual, expected)):
            self.fail("%s world translate: expected %s, got %s" % (node, fmt(expected), fmt(actual)))

    def assertWorldRotation(self, node, expected_degrees, tolerance=TOLERANCE):
        """Compares rotation matrices, so equivalent euler triples pass."""
        actual = world_rotation_matrix(node)
        wanted = om.MEulerRotation([om.MAngle(v, om.MAngle.kDegrees).asRadians() for v in expected_degrees]).asMatrix()
        if not actual.isEquivalent(wanted, tolerance):
            got = cmds.xform(node, query=True, worldSpace=True, rotation=True)
            self.fail("%s world rotate: expected %s, got %s" % (node, fmt(expected_degrees), fmt(got)))

    def assertWorldScale(self, node, expected, tolerance=TOLERANCE):
        actual = cmds.xform(node, query=True, worldSpace=True, scale=True)
        if any(abs(a - e) > tolerance for a, e in zip(actual, expected)):
            self.fail("%s world scale: expected %s, got %s" % (node, fmt(expected), fmt(actual)))

    def assertOpmDrivenBy(self, joint, module):
        sources = cmds.listConnections(joint + ".offsetParentMatrix", source=True, destination=False) or []
        if [s.split(":")[-1] for s in sources] != [module.split(":")[-1]]:
            self.fail("%s.offsetParentMatrix: expected driven by %s, got %s" % (joint, module, sources or "nothing"))

    def assertSource(self, plug, expected_source_plug):
        sources = cmds.listConnections(plug, plugs=True, source=True, destination=False) or []
        expected = expected_source_plug.lstrip(":")
        if [s.lstrip(":") for s in sources] != [expected]:
            self.fail("%s: expected input from %s, got %s" % (plug, expected, sources or "nothing"))


def world_rotation_matrix(node):
    path = om.MSelectionList().add(node).getDagPath(0)
    matrix = om.MTransformationMatrix(path.inclusiveMatrix())
    return matrix.rotation().asMatrix()
