"""Checks the complex performance rig (tools/perf/perf_scenes.py): a generated 296 joint skeleton, a template with
81 constraints, Aim modules, TwoBoneIk limbs and Ribbons, and a range of motion. They keep the benchmark's rig valid when the plugin changes,
so a slow result is never a broken build.

Not part of the suite, so RigTests.cmd never runs it. Run it on demand, after changing the rig generator or
adding modules to it:  mayapy tools/perf/perf_scenes.py check

Expectations are written by hand from the chain table in the tool, not read back from the rig.
"""
import json
import os
import sys

from maya import cmds

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tests", "maya"))
import perf_scenes  # noqa: E402
from rigtest import config  # noqa: E402
from rigtest.case import RigTestCase  # noqa: E402

NAMESPACE = "cx001"


class ComplexTemplateData(RigTestCase):
    """No scene needed: the generated data's shape."""

    def test_counts(self):
        template = perf_scenes.build_template()
        self.assertEqual(len(perf_scenes.joint_layout()), 296)      # 236 for the chains, aim sets and limbs, and 60 for the ribbons
        self.assertEqual(len(template["rigModules"]), 40)           # 8 Center chains, 19 Left chains, 3 Aim modules, 6 limbs, 1 Center and 3 Left ribbons
        self.assertEqual(perf_scenes.built_constraint_count(template), 81)   # 7 Center, 37 mirrored (22 + 12 limb spaces + 3 arm ribbon swings)

    def test_every_module_type_is_used(self):
        types = {module["moduleType"] for module in perf_scenes.build_template()["rigModules"]}
        self.assertEqual(types, {"FkChain", "Aim", "TwoBoneIk", "Ribbon"})

    def test_every_constraint_type_is_used(self):
        types = {entry["type"] for entry in perf_scenes.build_template()["constraints"]}
        self.assertEqual(types, {"Parent", "Orient", "Point", "Swing"})

    def test_limbs_are_twelve_with_three_joints_each(self):
        limbs = perf_scenes.limb_specs()
        self.assertEqual(len(limbs), 6)   # Left, each built on both sides
        usages = {j["usage"] for j in perf_scenes.joint_layout()}
        self.assertEqual({u for u in usages if u.endswith(("Aim", "Target", "Up"))}, {"%s%s" % (a["base"], r) for a in perf_scenes.aim_specs() for r in ("Aim", "Target", "Up")})   # the marker joints left are Aim's
        for limb in limbs:
            module = [m for m in perf_scenes.build_template()["rigModules"] if m["name"] == perf_scenes.module_name(limb["base"], "Left")][0]
            self.assertNotIn(perf_scenes.module_name(limb["base"], "Left") + ".", module["parentStructure"]["pole"])   # D17: not the module's own socket
            self.assertEqual(module["moduleVersion"], 2)
            self.assertEqual(module["moduleArgs"]["controlName"], limb["base"])
            self.assertEqual(set(module["moduleArgs"]), {"joints", "controlName", "poleOffset", "settingsOffset"})

    def test_limb_offsets_come_from_the_specs(self):
        # The arm limbA: bones (1.6, -0.2, -0.5) and (1.6, -0.2, 0.5), settings spec (0, 1.2, 0) from the root, so
        # (0, 1.2, 0) - (3.2, -0.4, 0) = (-3.2, 1.6, 0) from the end joint. The pole offset is the spec's.
        module = [m for m in perf_scenes.build_template()["rigModules"] if m["name"] == perf_scenes.module_name("limbA", "Left")][0]
        self.assertEqual(module["moduleArgs"]["poleOffset"], [0, 0, -1.5])
        for actual, expected in zip(module["moduleArgs"]["settingsOffset"], [-3.2, 1.6, 0.0]):
            self.assertAlmostEqual(actual, expected, places=6)

    def test_ribbons_have_no_control_joints(self):
        for module in perf_scenes.build_template()["rigModules"]:
            if module["moduleType"] != "Ribbon":
                continue
            self.assertEqual(module["moduleVersion"], 2)
            self.assertNotIn("controls", module["moduleArgs"])
        counts = {s["base"]: s["controls"] for s in perf_scenes.ribbon_specs()}
        self.assertEqual(counts, {"rbnTail": 4, "rbnArmA": 3, "rbnArmB": 3, "rbnArmC": 3})
        self.assertFalse([j["name"] for j in perf_scenes.joint_layout() if "Ctl" in j["name"]])

    def test_derived_controls_are_the_ones_without_joints(self):
        derived = perf_scenes.derived_controls()
        self.assertEqual(len(derived), 12 * 3 + (4 + 2 * 3 * 3))   # 12 limbs x (ik, pole, settings), the tail's 4 and 6 arm ribbons' 3
        # Left limbA: root (0.3+..) is the clavicle's first joint plus offset; the ik control sits on the end joint.
        end = [j for j in perf_scenes.joint_layout() if j["name"] == perf_scenes.joint_name("limbA", 2, "Left")][0]["position"]
        ik = [d for d in derived if d["chain"] == perf_scenes.module_name("limbA", "Left") and d["role"] == "ik"][0]
        self.assertEqual(ik["position"], end)
        # 12 limbs + 22 ribbon controls + 290 joint controls = 348 controls, as the rig has
        self.assertEqual(len(derived) + len(perf_scenes.joint_layout()) - 6, 348)

    def test_markup_is_unique(self):
        pairs = [(j["side"], j["usage"]) for j in perf_scenes.joint_layout()]
        self.assertEqual(len(pairs), len(set(pairs)))


class ComplexRig(RigTestCase):
    """One instance built the way the tool builds a hundred."""

    @classmethod
    def build_files(cls):
        template = perf_scenes.build_template()
        cls.template_file = os.path.join(config.WORK_DIR, "complex_template.json").replace("\\", "/")
        with open(cls.template_file, "w", encoding="utf-8") as handle:
            json.dump(template, handle)
        cls.skeleton_file = os.path.join(config.WORK_DIR, "complex_skeleton.ma").replace("\\", "/")

    def setUp(self):
        super().setUp()
        self.build_files()
        self.joints = perf_scenes.joint_layout()
        perf_scenes.build_skeleton(self.joints)
        cmds.file(rename=self.skeleton_file)
        cmds.file(save=True, type="mayaAscii")
        cmds.file(new=True, force=True)
        self.instances = perf_scenes.build_instances(self.skeleton_file, self.template_file,
                                                     perf_scenes.COMPLEX_ASSET_ID, "cx", 1, 14.0)

    def test_skeleton_joints_point_down_their_chains(self):
        for joint in self.joints:
            if not joint["aim_next"]:
                continue
            following = [j for j in self.joints if j["parent"] == joint["name"] and j["chain"] == joint["chain"]]
            self.assertEqual(len(following), 1, joint["name"])
            start = cmds.xform("%s:%s" % (NAMESPACE, joint["name"]), query=True, worldSpace=True, translation=True)
            end = cmds.xform("%s:%s" % (NAMESPACE, following[0]["name"]), query=True, worldSpace=True, translation=True)
            matrix = cmds.xform("%s:%s" % (NAMESPACE, joint["name"]), query=True, worldSpace=True, matrix=True)
            axis = matrix[0:3]
            direction = [e - s for e, s in zip(end, start)]
            length = sum(c * c for c in direction) ** 0.5
            dot = sum(a * d / length for a, d in zip(axis, direction))
            self.assertAlmostEqual(dot, 1.0, places=3, msg="%s +X does not point at %s" % (joint["name"], following[0]["name"]))

    def test_rig_at_rest_leaves_every_joint_in_place(self):
        # Layout control 0 moves the whole rig: the first instance sits at the origin.
        for joint in self.joints:
            actual = cmds.xform("%s:%s" % (NAMESPACE, joint["name"]), query=True, worldSpace=True, translation=True)
            for a, e in zip(actual, joint["position"]):
                self.assertAlmostEqual(a, e, places=3, msg=joint["name"])

    def test_builds_every_control_and_constraint(self):
        # 296 joints, less the 6 aim joints (no control), plus the 58 controls with no joint (12 limbs x 3, the ribbons' 22)
        self.assertEqual(len(self.instances[0].controls), 348)
        self.assertEqual(len(cmds.ls(NAMESPACE + ":*", type="rigConstraint")), 81)
        self.assertEqual(len(cmds.ls(NAMESPACE + ":*", type="aimModule")), 6)
        self.assertEqual(len(cmds.ls(NAMESPACE + ":*", type="twoBoneIkModule")), 12)
        self.assertEqual(len(cmds.ls(NAMESPACE + ":*", type="ribbonModule")), 7)

    def test_every_limb_is_built_with_six_controls_in_ik(self):
        limbs = self.instances[0].limbs
        self.assertEqual(len(limbs), 12)
        for limb in limbs:
            self.assertEqual(sorted(limb), sorted(perf_scenes.LIMB_ROLES))
            self.assertEqual(len(set(limb.values())), 6)
            self.assertEqual(cmds.getAttr(limb["settings"] + ".ikFkBlend"), 1.0)
            self.assertEqual(cmds.attributeQuery("space", node=limb["pole"], listEnum=True)[0], "Parent:Ik")

    def test_range_of_motion_keys(self):
        last = perf_scenes.key_rom(self.instances, segment=40, degrees=90.0)
        self.assertEqual(last, 121)
        control = self.instances[0].controls[0]
        expected = [
            ("rotateX", 11, -90.0), ("rotateX", 31, 90.0), ("rotateX", 41, 0.0),
            ("rotateY", 41, 0.0), ("rotateY", 51, -90.0), ("rotateY", 71, 90.0), ("rotateY", 81, 0.0),
            ("rotateZ", 91, -90.0), ("rotateZ", 111, 90.0), ("rotateZ", 121, 0.0),
        ]
        for attribute, frame, value in expected:
            actual = cmds.getAttr("%s.%s" % (control, attribute), time=frame)
            self.assertAlmostEqual(actual, value, places=3, msg="%s at %d" % (attribute, frame))
        # Every rotating control got the same keys: 3 axes x 4 keys.
        self.assertEqual(len(cmds.keyframe(control, query=True, attribute="rotateY")), 4)

    def test_range_of_motion_reaches_every_limb_branch(self):
        perf_scenes.key_rom(self.instances, segment=40, degrees=90.0)
        limb = self.instances[0].limbs[0]
        blend = limb["settings"] + ".ikFkBlend"
        values = {round(cmds.getAttr(blend, time=frame), 3) for frame in range(1, 122)}
        self.assertTrue({0.0, 0.5, 1.0} <= values, values)   # FK, a blend and IK
        # Stretch, soft and pin are each on somewhere the blend is above 0, and each returns to 0.
        for plug in (limb["settings"] + ".stretch", limb["settings"] + ".soft", limb["pole"] + ".pin"):
            on = [f for f in range(1, 122) if cmds.getAttr(plug, time=f) > 0.5 * max(cmds.keyframe(plug, query=True, valueChange=True))]
            self.assertTrue(on, plug)
            self.assertTrue(all(cmds.getAttr(blend, time=f) > 0 for f in on), plug)
            self.assertEqual(cmds.getAttr(plug, time=121), 0.0, plug)
        # The pole steps from Parent to Ik. Keys on locked channels would fail, so the pole and settings have no rotation keys.
        self.assertEqual(cmds.getAttr(limb["pole"] + ".space", time=61), 1)
        self.assertEqual(cmds.getAttr(limb["pole"] + ".space", time=121), 0)
        self.assertFalse(cmds.keyframe(limb["pole"], query=True, attribute="rotateX"))
        self.assertFalse(cmds.keyframe(limb["settings"], query=True, attribute="rotateX"))
        # The handle goes past the limb's full reach (about 3.3): the soft zone and stretch have something to do.
        self.assertGreater(cmds.getAttr(limb["ik"] + ".translateX", time=31), 2.0)
