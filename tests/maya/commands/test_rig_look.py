"""setRigLook, the lookRoot node and the skeletonHash check of loadRigSkinWeights (D54, rigs-and-looks.md 4.1).

The rig is fkchain_three built into 'rig'. A look is a mesh in the rig's namespace plus a weight file saved from a skinned
copy of it, so the files carry the rig's real skeletonHash. Look A is a box (meshId look-box), look B a ball (look-ball).
"""
import json
import os

from maya import cmds

import rigmodules.query
from rigtest import config
from rigtest.case import RigTestCase, rig_name

from commands.test_skin_weight_cmds import make_box, set_mesh_id, skin_cluster

RIG = config.RIG_NAMESPACE
BOX_ID = "look-box"
BALL_ID = "look-ball"
ZERO_HASH = "0000000000000000"


def new_box(namespace=RIG):
    box = make_box(namespace)
    set_mesh_id(box, BOX_ID)
    return box


def new_ball(namespace=RIG):
    ball = cmds.polySphere(name=":%s:ball_geo" % namespace, radius=1, constructionHistory=False)[0]
    set_mesh_id(ball, BALL_ID)
    return ball


def read_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path, data):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle)


class LookTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_three", "fk_three")
        self.rig_root = cmds.ls(type="rigRootModule")[0]
        self.box_file = self.make_weight_file(new_box, "look_box.json")
        self.ball_file = self.make_weight_file(new_ball, "look_ball.json")

    def make_weight_file(self, factory, name):
        """A real weight file for the rig: the mesh is skinned to the rig's joints, saved, then deleted."""
        mesh = factory()
        cmds.skinCluster(rig_name("a_00_M_JNT"), rig_name("b_00_M_JNT"), mesh, toSelectedBones=True)
        path = os.path.join(config.WORK_DIR, name).replace("\\", "/")
        cmds.saveRigSkinWeights(mesh, file=path)
        cmds.delete(mesh)
        return path

    def set_look(self, meshes, files, look="boxy", **kwargs):
        return cmds.setRigLook(self.rig_root, mesh=meshes, file=files, asset=config.ASSET_ID, look=look, **kwargs)

    def apply_box(self, look="boxy", **kwargs):
        box = new_box()
        self.set_look([box], [self.box_file], look=look, **kwargs)
        return box

    def look_info(self):
        return json.loads(cmds.getRigInfo(self.rig_root))["look"]

    def look_root(self):
        return cmds.ls(type="lookRoot")

    def members(self):
        return sorted(cmds.listConnections(self.look_root()[0] + ".lookMembers", source=True, destination=False) or [])

    def assertNoLook(self):
        self.assertEqual(self.look_root(), [])
        self.assertIsNone(self.look_info())

    def hash_file(self, source, skeleton_hash, name):
        data = read_json(source)
        data["skeletonHash"] = skeleton_hash
        path = os.path.join(config.WORK_DIR, name).replace("\\", "/")
        write_json(path, data)
        return path


class ApplyLook(LookTestCase):

    def test_a_rig_has_no_look_until_one_is_applied(self):
        self.assertNoLook()
        self.assertIsNone(rigmodules.query.get_rig_info(self.rig_root).look)

    def test_first_look_creates_the_look_root(self):
        box = new_box()
        result = self.set_look([box], [self.box_file], publishVersion="20261117T100000Z_bmorgan")
        self.assertEqual(result, "%s:lookRoot" % RIG)
        self.assertEqual(self.look_root(), [result])
        self.assertEqual(cmds.listConnections(self.rig_root + ".look", source=False, destination=True), [result])
        self.assertEqual(cmds.listConnections(result + ".rigRoot", source=True, destination=False), [self.rig_root])
        self.assertEqual(self.members(), [box])
        self.assertEqual(self.look_info(), {"assetId": config.ASSET_ID, "name": "boxy", "publishVersion": "20261117T100000Z_bmorgan"})

    def test_the_mesh_is_skinned_to_the_rig(self):
        box = self.apply_box()
        cluster = skin_cluster(box)
        self.assertIsNotNone(cluster)
        influences = sorted(cmds.skinCluster(cluster, query=True, influence=True))
        self.assertEqual(influences, sorted([rig_name("a_00_M_JNT").lstrip(":"), rig_name("b_00_M_JNT").lstrip(":")]))

    def test_python_query_reads_the_look(self):
        self.apply_box()
        info = rigmodules.query.get_rig_info(self.rig_root)
        self.assertEqual(info.look, rigmodules.query.LookInfo(asset_id=config.ASSET_ID, name="boxy", publish_version=""))

    def test_the_look_may_come_from_another_asset(self):
        box = new_box()
        cmds.setRigLook(self.rig_root, mesh=[box], file=[self.box_file], asset="shooter\\props\\crate", look="wood")
        self.assertEqual(self.look_info()["assetId"], "shooter\\props\\crate")


class SwapLook(LookTestCase):

    def test_swap_replaces_the_previous_looks_meshes_and_history(self):
        box = self.apply_box()
        ball = new_ball()
        self.set_look([ball], [self.ball_file], look="ball")
        self.assertNotExists(box)
        self.assertEqual(self.members(), [ball])
        self.assertEqual(self.look_info()["name"], "ball")
        self.assertEqual(len(self.look_root()), 1)
        # The box's skinCluster went with it: only the ball's is left.
        self.assertEqual(cmds.ls(type="skinCluster"), [skin_cluster(ball)])

    def test_a_mesh_the_animator_added_survives(self):
        self.apply_box()
        extra = cmds.polyCube(name=":%s:extra_geo" % RIG, constructionHistory=False)[0]
        self.set_look([new_ball()], [self.ball_file], look="ball")
        self.assertExists(extra)

    def test_new_meshes_are_named_after_their_files(self):
        self.apply_box()
        imported = new_box()   # Maya names it box_geo1 while the first look's box exists
        self.assertNotEqual(imported, "%s:box_geo" % RIG)
        self.set_look([imported], [self.box_file], look="boxy2")
        self.assertEqual(self.members(), ["%s:box_geo" % RIG])
        self.assertExists("%s:box_geo" % RIG)
        self.assertNotExists("%s:box_geo1" % RIG)

    def test_reapplying_a_look_keeps_one_member(self):
        box = self.apply_box()
        self.set_look([box], [self.box_file])
        self.assertEqual(self.members(), [box])
        self.assertExists(box)

    def test_two_meshes_are_matched_to_their_files_by_meshid_not_order(self):
        box, ball = new_box(), new_ball()
        self.set_look([box, ball], [self.ball_file, self.box_file], look="both")
        self.assertEqual(sorted(self.members()), sorted([box, ball]))
        self.assertIsNotNone(skin_cluster(box))
        self.assertIsNotNone(skin_cluster(ball))

    def test_controls_and_animation_are_untouched(self):
        control = rig_name("a_00_M_CTRL")
        cmds.setKeyframe(control, attribute="rotateZ", time=1, value=0)
        cmds.setKeyframe(control, attribute="rotateZ", time=10, value=45)
        before = cmds.keyframe(control, query=True, attribute="rotateZ", valueChange=True)
        self.apply_box()
        self.set_look([new_ball()], [self.ball_file], look="ball")
        self.assertEqual(cmds.keyframe(control, query=True, attribute="rotateZ", valueChange=True), before)
        self.assertEqual(cmds.getAttr(control + ".rotateZ", time=10), 45)


class UndoRedo(LookTestCase):

    def test_first_look_undoes_to_no_look(self):
        box = new_box()
        self.set_look([box], [self.box_file])
        cmds.undo()
        self.assertNoLook()
        self.assertIsNone(skin_cluster(box))
        self.assertExists(box)
        cmds.redo()
        self.assertEqual(self.look_info()["name"], "boxy")
        self.assertIsNotNone(skin_cluster(box))

    def test_swap_undoes_to_the_previous_look(self):
        box = self.apply_box()
        ball = new_ball()
        self.set_look([ball], [self.ball_file], look="ball")
        cmds.undo()
        self.assertExists(box)
        self.assertIsNotNone(skin_cluster(box))
        self.assertEqual(self.members(), [box])
        self.assertEqual(self.look_info()["name"], "boxy")
        self.assertIsNone(skin_cluster(ball))
        self.assertExists(ball)
        cmds.redo()
        self.assertNotExists(box)
        self.assertEqual(self.members(), [ball])
        self.assertEqual(self.look_info()["name"], "ball")
        self.assertIsNotNone(skin_cluster(ball))


class SkeletonCheck(LookTestCase):

    def save_rig_skeleton(self, name):
        path = os.path.join(config.WORK_DIR, name).replace("\\", "/")
        cmds.saveRigSkeleton(rig_name("a_00_M_JNT"), rig_name("b_00_M_JNT"), rig_name("c_00_M_JNT"), file=path)
        return path

    def test_the_rigs_own_skeleton_matches_its_stamp(self):
        path = self.save_rig_skeleton("rig_skeleton.json")
        self.assertEqual(read_json(path)["skeletonHash"], cmds.getAttr(self.rig_root + ".skeletonHash"))
        box = new_box()
        self.set_look([box], [self.box_file], skeleton=path, rigSkeleton=path)
        self.assertEqual(self.members(), [box])

    def test_another_skeleton_names_the_differing_joints(self):
        rig_path = self.save_rig_skeleton("rig_skeleton.json")
        data = read_json(rig_path)
        data["joints"][1]["restLocal"][13] += 1.0   # one joint a unit higher
        look_path = os.path.join(config.WORK_DIR, "look_skeleton.json").replace("\\", "/")
        write_json(look_path, data)
        box = new_box()
        message = self.assertMayaError(
            lambda: self.set_look([box], [self.box_file], skeleton=look_path, rigSkeleton=rig_path),
            "is not the rig's", "a different rest transform")
        self.assertIn(data["joints"][1]["markup"][1], message)
        self.assertNoLook()
        self.assertIsNone(skin_cluster(box))

    def test_without_the_rigs_file_the_error_still_names_both_hashes(self):
        rig_path = self.save_rig_skeleton("rig_skeleton.json")
        data = read_json(rig_path)
        data["joints"][1]["restLocal"][13] += 1.0
        look_path = os.path.join(config.WORK_DIR, "look_skeleton.json").replace("\\", "/")
        write_json(look_path, data)
        self.assertMayaError(lambda: self.set_look([new_box()], [self.box_file], skeleton=look_path), "is not the rig's")
        self.assertNoLook()


class WeightHashCheck(LookTestCase):

    def test_a_file_for_another_skeleton_fails_and_changes_nothing(self):
        bad = self.hash_file(self.box_file, ZERO_HASH, "look_box_bad_hash.json")
        box = new_box()
        self.assertMayaError(lambda: self.set_look([box], [bad]), "weight file is for skeleton " + ZERO_HASH, "ignoreSkeletonHash")
        self.assertNoLook()
        self.assertIsNone(skin_cluster(box))

    def test_ignore_skeleton_hash_loads_it_anyway(self):
        bad = self.hash_file(self.box_file, ZERO_HASH, "look_box_bad_hash.json")
        box = new_box()
        self.set_look([box], [bad], ignoreSkeletonHash=True)
        self.assertIsNotNone(skin_cluster(box))

    def test_a_file_with_no_hash_loads_with_a_warning(self):
        old = self.hash_file(self.box_file, "", "look_box_no_hash.json")
        box = new_box()
        self.set_look([box], [old])
        self.assertIsNotNone(skin_cluster(box))

    def test_load_rig_skin_weights_checks_the_hash_too(self):
        bad = self.hash_file(self.box_file, ZERO_HASH, "look_box_bad_hash.json")
        box = new_box()
        self.assertMayaError(lambda: cmds.loadRigSkinWeights(box, file=bad), "weight file is for skeleton " + ZERO_HASH)
        self.assertIsNone(skin_cluster(box))
        cmds.loadRigSkinWeights(box, file=bad, ignoreSkeletonHash=True)
        self.assertIsNotNone(skin_cluster(box))

    def test_load_rig_skin_weights_accepts_a_matching_hash(self):
        box = new_box()
        cmds.loadRigSkinWeights(box, file=self.box_file)
        self.assertIsNotNone(skin_cluster(box))


class SetRigLookErrors(LookTestCase):

    def assertUntouched(self, box):
        self.assertNoLook()
        self.assertIsNone(skin_cluster(box))
        self.assertExists(box)

    def test_look_and_asset_are_required(self):
        box = new_box()
        self.assertMayaError(lambda: cmds.setRigLook(self.rig_root, mesh=[box], file=[self.box_file], look="boxy"), "-asset and -look")
        self.assertMayaError(lambda: cmds.setRigLook(self.rig_root, mesh=[box], file=[self.box_file], asset=config.ASSET_ID), "-asset and -look")
        self.assertUntouched(box)

    def test_look_name_follows_the_lowercase_rule(self):
        box = new_box()
        self.assertMayaError(lambda: self.set_look([box], [self.box_file], look="Boxy"), "Not a valid look name")
        self.assertUntouched(box)

    def test_asset_id_is_checked(self):
        box = new_box()
        self.assertMayaError(lambda: cmds.setRigLook(self.rig_root, mesh=[box], file=[self.box_file], asset="Bad Id", look="boxy"),
                             "Not a valid asset id")
        self.assertUntouched(box)

    def test_one_file_for_each_mesh(self):
        box = new_box()
        self.assertMayaError(lambda: self.set_look([box], [self.box_file, self.ball_file]), "one -file for each -mesh")
        self.assertUntouched(box)

    def test_a_mesh_outside_the_rigs_namespace_fails(self):
        cmds.namespace(add=":other")
        box = new_box("other")
        self.assertMayaError(lambda: self.set_look([box], [self.box_file]), "not in the rig's namespace")
        self.assertUntouched(box)

    def test_a_mesh_with_no_file_for_its_meshid_fails(self):
        ball = new_ball()
        self.assertMayaError(lambda: self.set_look([ball], [self.box_file]), "no weight file of its own")
        self.assertUntouched(ball)

    def test_a_failed_swap_keeps_the_previous_look(self):
        box = self.apply_box()
        ball = new_ball()
        bad = self.hash_file(self.ball_file, ZERO_HASH, "look_ball_bad_hash.json")
        self.assertMayaError(lambda: self.set_look([ball], [bad], look="ball"), "weight file is for skeleton")
        self.assertExists(box)
        self.assertIsNotNone(skin_cluster(box))
        self.assertEqual(self.look_info()["name"], "boxy")
        self.assertIsNone(skin_cluster(ball))

    def test_not_a_rig_root(self):
        box = new_box()
        self.assertMayaError(lambda: cmds.setRigLook(box, mesh=[box], file=[self.box_file], asset=config.ASSET_ID, look="boxy"), "rig root")

    def test_a_missing_weight_file_fails(self):
        box = new_box()
        self.assertMayaError(lambda: self.set_look([box], [os.path.join(config.WORK_DIR, "nothing.json").replace("\\", "/")]), "weight")
        self.assertUntouched(box)

    def test_a_call_needs_a_mesh_or_a_rigid_mesh(self):
        self.assertMayaError(lambda: cmds.setRigLook(self.rig_root, asset=config.ASSET_ID, look="boxy"), "at least one -mesh or -rigidMesh")
        self.assertNoLook()


class RigidMeshes(LookTestCase):
    """A mesh with a meshId and no weight file yet is bound whole to the topmost skin joint (D54): fkchain_three's a."""

    def set_rigid(self, rigid, meshes=(), files=(), look="boxy"):
        kwargs = {"mesh": list(meshes), "file": list(files)} if meshes else {}
        return cmds.setRigLook(self.rig_root, rigidMesh=rigid, asset=config.ASSET_ID, look=look, **kwargs)

    def influences(self, mesh):
        return sorted(cmds.skinCluster(skin_cluster(mesh), query=True, influence=True))

    def test_a_rigid_mesh_is_bound_to_the_top_skin_joint_and_is_a_member(self):
        ball = new_ball()
        self.set_rigid([ball])
        self.assertEqual(self.influences(ball), [rig_name("a_00_M_JNT").lstrip(":")])
        self.assertEqual(self.members(), [ball])
        self.assertEqual(ball, "%s:ball_geo" % RIG, "a rigid mesh keeps its own name")
        self.assertEqual(self.look_info()["name"], "boxy")

    def test_rigid_and_file_meshes_go_on_together(self):
        box, ball = new_box(), new_ball()
        self.set_rigid([ball], meshes=[box], files=[self.box_file])
        self.assertEqual(self.members(), sorted([box, ball]))
        self.assertEqual(len(self.influences(box)), 2)
        self.assertEqual(self.influences(ball), [rig_name("a_00_M_JNT").lstrip(":")])

    def test_a_non_skin_root_passes_to_the_first_skin_joint_below(self):
        cmds.rigJointMarkup(rig_name("a_00_M_JNT"), edit=True, skin=False)
        ball = new_ball()
        self.set_rigid([ball])
        self.assertEqual(self.influences(ball), [rig_name("b_00_M_JNT").lstrip(":")])

    def test_a_weight_save_writes_the_rigid_meshes_first_file(self):
        ball = new_ball()
        self.set_rigid([ball])
        path = os.path.join(config.WORK_DIR, "rigid_ball.json").replace("\\", "/")
        cmds.saveRigSkinWeights(ball, file=path)
        data = read_json(path)
        self.assertEqual((data["meshId"], data["influences"], data["skeletonHash"]),
                         (BALL_ID, [["Center", "a"]], cmds.getAttr(self.rig_root + ".skeletonHash")))

    def test_a_swap_deletes_a_rigid_member(self):
        ball = new_ball()
        self.set_rigid([ball])
        box = new_box()
        self.set_look([box], [self.box_file], look="box")
        self.assertNotExists(ball)
        self.assertEqual(self.members(), [box])

    def test_undo_takes_the_bind_and_the_look_back(self):
        ball = new_ball()
        self.set_rigid([ball])
        cmds.undo()
        self.assertNoLook()
        self.assertIsNone(skin_cluster(ball))
        cmds.redo()
        self.assertEqual(self.members(), [ball])

    def test_a_rigid_mesh_without_a_meshid_fails_and_changes_nothing(self):
        bare = cmds.polyCube(name=":%s:bare_geo" % RIG, constructionHistory=False)[0]
        self.assertMayaError(lambda: self.set_rigid([bare]), "has no meshId")
        self.assertNoLook()
        self.assertIsNone(skin_cluster(bare))

    def test_a_mesh_passed_twice_fails(self):
        box = new_box()
        self.assertMayaError(lambda: self.set_rigid([box], meshes=[box], files=[self.box_file]), "as a rigid mesh")
        self.assertNoLook()

    def test_no_skin_joint_fails_and_changes_nothing(self):
        for joint in ("a_00_M_JNT", "b_00_M_JNT", "c_00_M_JNT"):
            cmds.rigJointMarkup(rig_name(joint), edit=True, skin=False)
        ball = new_ball()
        self.assertMayaError(lambda: self.set_rigid([ball]), "is marked skin")
        self.assertNoLook()
        self.assertIsNone(skin_cluster(ball))
