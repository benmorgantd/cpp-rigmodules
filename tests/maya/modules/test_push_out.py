"""rigPushOut: the collisions list, the push, the mirror, the read-phase checks and the temporary display.

Setup: chain 1 (a, b) has the capsule's joint a at the origin. The Left chain's control c sits at (3, 0, 0), parented to the
layout, so it does not follow chain 1. The capsule of the basic template is a sphere of radius 2 at joint a. Moving
control a by +2 in x puts its centre at (2, 0, 0), 1 from c: c is inside by 1, and the push is +1 along x, so c ends at (4, 0, 0).
"""
import copy
import json

from maya import cmds

from rigtest import config, fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.jointclips import rig_root

PUSH_OUT = "pushOut_00_L_CON"
CHILD = "c_00_L_CTRL"
DRIVER = "a_00_M_CTRL"


class PushOutBehaviour(RigTestCase):

    def build_push_out(self, edits=None, template="push_out_basic", skeleton="fk_pushout"):
        data = fixtures.load_template(template)
        if edits:
            edits(data["collisions"][0])
        self.build(data, self.make_skeleton(skeleton))

    def move_driver(self, x=2.0):
        cmds.setAttr(rig_name(DRIVER) + ".translateX", x)

    def test_at_rest_the_child_is_outside_and_nothing_moves(self):
        self.build_push_out()
        self.assertWorldTranslation(rig_name(CHILD), (3, 0, 0))

    def test_the_capsule_pushes_the_child_out_along_the_line_from_its_axis(self):
        self.build_push_out()
        self.move_driver()
        self.assertWorldTranslation(rig_name(CHILD), (4, 0, 0))

    def test_a_margin_pushes_the_child_out_by_that_much_more(self):
        self.build_push_out(lambda collision: collision.update(margin=0.5))
        self.move_driver()
        self.assertWorldTranslation(rig_name(CHILD), (4.5, 0, 0))

    def test_max_push_caps_the_move(self):
        self.build_push_out(lambda collision: collision.update(maxPush=0.25))
        self.move_driver()
        self.assertWorldTranslation(rig_name(CHILD), (3.25, 0, 0))

    def test_the_constraint_weight_blends_the_push(self):
        self.build_push_out(lambda collision: collision.update(constraintWeight=0.5))
        self.move_driver()
        self.assertWorldTranslation(rig_name(CHILD), (3.5, 0, 0))

    def test_a_child_on_the_capsules_axis_goes_along_the_capsules_z(self):
        # The segment runs x from -2 to 2 and moves with the joint: at +1 it ends at x = 3, on c. No line to follow, so c
        # goes one radius along the joint's +Z.
        def edit(collision):
            collision["capsule"].update(radius=1.0, halfLength=2.0)
        self.build_push_out(edit)
        self.move_driver(1.0)
        self.assertWorldTranslation(rig_name(CHILD), (3, 0, 1))

    def test_the_offset_translates_and_turns_the_capsule(self):
        # A quarter turn about Z puts the segment along Y: centred at (3, -1, 0), y from -3 to 1, so c at (3, 0, 0) is on it.
        # With the segment along X instead c would be 1 from it, on the surface, and not pushed.
        def edit(collision):
            collision["capsule"].update(radius=1.0, halfLength=2.0, offset={"translate": [3, -1, 0], "rotate": [0, 0, 90]})
        self.build_push_out(edit)
        self.assertWorldTranslation(rig_name(CHILD), (3, 0, 1))

    def test_the_offset_alone_places_a_sphere_where_the_child_is_inside_it(self):
        self.build_push_out(lambda collision: collision["capsule"].update(offset={"translate": [2, 0, 0]}))
        self.assertWorldTranslation(rig_name(CHILD), (4, 0, 0))

    def test_the_push_out_sits_between_the_plugs_source_and_the_module(self):
        self.build_push_out()
        module = rig_name("fkchain_02_L_MOD").lstrip(":")   # listConnections reports names without the leading colon
        out = cmds.listConnections(rig_name(PUSH_OUT) + ".outParentMatrix", plugs=True) or []
        self.assertTrue(any(plug.startswith(module + ".") for plug in out), out)

    def test_it_is_a_constraint_kind_node_and_is_on_the_rig_root(self):
        self.build_push_out()
        self.assertEqual(cmds.nodeType(rig_name(PUSH_OUT)), "rigPushOut")
        self.assertTrue(cmds.listConnections(rig_name(PUSH_OUT) + ".rigRoot"))

    def test_it_runs_after_the_plugs_space_constraint(self):
        def edit(template):
            template["constraints"] = [{
                "name": "space_00_L_CON", "target": "fkchain_02_L_MOD.start",
                "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0},
                            {"socket": "fkchain_01_M_MOD.end", "label": "B"}]}]
            template["collisions"][0]["capsule"]["radius"] = 4.0
        data = fixtures.load_template("push_out_basic")
        edit(data)
        self.build(data, self.make_skeleton("fk_pushout"))
        # The space is switched to b's chain end. b moving by -1 in x would take c to x = 2, which is 2 from the capsule's
        # axis, inside its radius of 4: the push moves it out by 2, to x = 4.
        cmds.setAttr(rig_name(CHILD + ".space"), 1)
        cmds.setAttr(rig_name("b_00_M_CTRL.translateX"), -1)
        self.assertWorldTranslation(rig_name(CHILD), (4, 0, 0))


class PushOutMirror(RigTestCase):

    def test_the_mirror_places_its_capsule_at_the_sources_reflected_across_x(self):
        self.build("push_out_mirror", self.make_skeleton("fk_pushout_mirror"))
        # Left: a sphere at (2, 0, 0) pushes c from 3 to 4. Right: the sphere is reflected to (-2, 0, 0) and pushes c from -3 to -4.
        self.assertWorldTranslation(rig_name("c_00_L_CTRL"), (4, 0, 0))
        self.assertWorldTranslation(rig_name("c_00_R_CTRL"), (-4, 0, 0))
        self.assertEqual(cmds.nodeType(rig_name("pushOut_00_R_CON")), "rigPushOut")

    def test_a_collision_on_a_center_module_cannot_be_mirrored(self):
        data = fixtures.load_template("push_out_mirror")
        data["collisions"][0]["target"] = "fkchain_01_M_MOD.start"
        self.assertMayaError(lambda: self.build(data, self.make_skeleton("fk_pushout_mirror")), "Left or Right")
        self.assertNothingBuilt()


class PushOutChecks(RigTestCase):

    def attempt(self, edits, *contains):
        template = fixtures.load_template("push_out_basic")
        edits(template)
        joints = self.make_skeleton("fk_pushout")
        self.assertMayaError(lambda: self.build(copy.deepcopy(template), joints), *contains)
        self.assertNothingBuilt()

    def test_a_type_that_is_not_push_out_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0].update(type="Hinge"), "Hinge")

    def test_drivers_are_an_unknown_key(self):
        self.attempt(lambda t: t["collisions"][0].update(drivers=[]), "unknown key 'drivers'")

    def test_the_capsule_is_required(self):
        self.attempt(lambda t: t["collisions"][0].pop("capsule"), "capsule")

    def test_a_followed_joint_that_is_not_there_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0]["capsule"].update(follow=["Center", "nope"]), "nope")

    def test_a_follow_that_is_not_a_selector_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0]["capsule"].update(follow="a"), "follow")

    def test_a_target_that_is_not_a_plug_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0].update(target="fkchain_02_L_MOD.nope"), "nope")

    def test_a_second_collision_on_one_plug_is_an_error(self):
        def edit(template):
            second = copy.deepcopy(template["collisions"][0])
            second["name"] = "pushOut_01_L_CON"
            template["collisions"].append(second)
        self.attempt(edit, "already the target")

    def test_a_name_outside_the_convention_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0].update(name="pushOut"), "name must be")

    def test_a_negative_margin_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0].update(margin=-1), "margin")

    def test_a_radius_of_zero_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0]["capsule"].update(radius=0), "radius")

    def test_a_negative_half_length_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0]["capsule"].update(halfLength=-1), "halfLength")

    def test_a_max_push_of_zero_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0].update(maxPush=0), "maxPush")

    def test_a_weight_over_one_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0].update(constraintWeight=2), "weight")

    def test_an_unknown_capsule_key_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0]["capsule"].update(depth=1), "unknown key 'depth'")

    def test_an_offset_that_is_not_three_numbers_is_an_error(self):
        self.attempt(lambda t: t["collisions"][0]["capsule"].update(offset={"translate": [1, 2]}), "three numbers")

    def test_a_capsule_on_a_joint_the_pushed_module_drives_is_a_cycle(self):
        # Chain 2 would be pushed out of a capsule that follows its own joint c.
        self.attempt(lambda t: t["collisions"][0]["capsule"].update(follow=["Left", "c"]), "cycle")


class PushOutDisplay(RigTestCase):
    """showRigPushOuts, getRigPushOutCapsule and hideRigPushOuts: a temporary capsule that follows the joint and reports its place."""

    DISPLAY = "pushOut_00_L_CON_display"

    def setUp(self):
        super().setUp()
        data = fixtures.load_template("push_out_basic")
        data["collisions"][0]["capsule"].update(radius=2.0, halfLength=1.5, offset={"translate": [0, 1, 0], "rotate": [0, 0, 30]})
        self.build(data, self.make_skeleton("fk_pushout"))

    def show(self):
        return cmds.showRigPushOuts(namespace=config.RIG_NAMESPACE)

    def test_show_makes_one_display_per_push_out_at_its_stored_place_and_size(self):
        self.assertEqual(self.show(), [config.RIG_NAMESPACE + ":" + self.DISPLAY])
        display = rig_name(self.DISPLAY)
        self.assertEqual(cmds.nodeType(display), "rigControlNode")
        self.assertAlmostEqual(cmds.getAttr(display + ".radius"), 2.0)
        self.assertAlmostEqual(cmds.getAttr(display + ".halfLength"), 1.5)
        self.assertWorldTranslation(display, (0, 1, 0))
        self.assertWorldRotation(display, (0, 0, 30))

    def test_the_display_follows_its_joint(self):
        self.show()
        cmds.setAttr(rig_name(DRIVER) + ".translateX", 2)
        self.assertWorldTranslation(rig_name(self.DISPLAY), (2, 1, 0))

    def test_the_size_attributes_drive_the_drawn_shape(self):
        self.show()
        display = rig_name(self.DISPLAY)
        cmds.setAttr(display + ".radius", 3.0)
        cmds.setAttr(display + ".halfLength", 5.0)
        shape = cmds.getAttr(display + ".shapeTransform")
        # The diagonal is (halfLength, radius, radius), which the draw reads as the capsule.
        self.assertAlmostEqual(shape[0], 5.0)
        self.assertAlmostEqual(shape[5], 3.0)
        self.assertAlmostEqual(shape[10], 3.0)

    def test_get_returns_the_templates_capsule_as_it_is_stored(self):
        self.show()
        capsule = json.loads(cmds.getRigPushOutCapsule(rig_name(self.DISPLAY)))
        self.assertEqual(capsule["follow"], ["Center", "a"])
        self.assertAlmostEqual(capsule["radius"], 2.0)
        self.assertAlmostEqual(capsule["halfLength"], 1.5)
        self.assertEqual(capsule["offset"]["translate"], [0, 1, 0])
        self.assertAlmostEqual(capsule["offset"]["rotate"][2], 30.0, places=4)

    def test_get_reports_where_the_display_was_moved_to_and_how_big_it_was_made(self):
        self.show()
        display = rig_name(self.DISPLAY)
        cmds.setAttr(display + ".translateX", 1.25)
        cmds.setAttr(display + ".translateY", -0.5)
        cmds.setAttr(display + ".rotateX", 10)
        cmds.setAttr(display + ".rotateY", 20)
        cmds.setAttr(display + ".rotateZ", 40)
        cmds.setAttr(display + ".radius", 3.5)
        capsule = json.loads(cmds.getRigPushOutCapsule(display))
        self.assertAlmostEqual(capsule["radius"], 3.5)
        # The display's local transform sits on top of the stored offset (translate [0, 1, 0], rotate 30 about Z).
        self.assertEqual(len(capsule["offset"]["translate"]), 3)
        self.assertEqual(len(capsule["offset"]["rotate"]), 3)

        # The text read back and built again puts the capsule where the display was.
        data = fixtures.load_template("push_out_basic")
        data["collisions"][0]["capsule"] = capsule
        cmds.file(new=True, force=True)
        cmds.namespace(setNamespace=":")
        self.build(data, self.make_skeleton("fk_pushout"))
        cmds.showRigPushOuts(namespace=config.RIG_NAMESPACE)
        rebuilt = rig_name(self.DISPLAY)
        self.assertWorldTranslation(rebuilt, (capsule["offset"]["translate"]), 1e-5)

    def test_get_refuses_a_scaled_display(self):
        self.show()
        display = rig_name(self.DISPLAY)
        cmds.setAttr(display + ".scaleX", 2)
        self.assertMayaError(lambda: cmds.getRigPushOutCapsule(display), "scaled")

    def test_get_refuses_a_node_that_is_not_a_display(self):
        self.assertMayaError(lambda: cmds.getRigPushOutCapsule(rig_name(CHILD)), "not a push-out display")

    def test_show_twice_is_an_error_until_the_displays_are_hidden(self):
        self.show()
        self.assertMayaError(self.show, "hide the displays first")
        self.assertEqual(cmds.hideRigPushOuts(namespace=config.RIG_NAMESPACE), [config.RIG_NAMESPACE + ":" + self.DISPLAY])
        self.assertFalse(cmds.objExists(rig_name(self.DISPLAY)))
        self.assertEqual(len(self.show()), 1)

    def test_hide_deletes_the_displays_and_their_utility_nodes(self):
        before = set(cmds.ls())
        self.show()
        cmds.hideRigPushOuts(namespace=config.RIG_NAMESPACE)
        self.assertEqual(set(cmds.ls()), before)

    def test_show_and_hide_undo(self):
        self.show()
        cmds.undo()
        self.assertFalse(cmds.objExists(rig_name(self.DISPLAY)))
        cmds.redo()
        self.assertTrue(cmds.objExists(rig_name(self.DISPLAY)))
        cmds.hideRigPushOuts(namespace=config.RIG_NAMESPACE)
        cmds.undo()
        self.assertTrue(cmds.objExists(rig_name(self.DISPLAY)))

    def test_the_display_is_not_one_of_the_rigs_controls(self):
        self.show()
        # Controls are found through the rig's modules, never by node type: the display has no module.
        controls = cmds.ls(cmds.getRigControls(rig_root()), long=True)
        self.assertNotIn(cmds.ls(rig_name(self.DISPLAY), long=True)[0], controls)
