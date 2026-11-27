"""setRigLook, the lookRoot node, its report and the fallback ladder (rigs-and-looks.md 4.1).

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
        self.build_standard("fkchain_three", "fk_three_parented")
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
        """setRigLook's report, parsed. Its lookRoot is the look root's name."""
        return json.loads(cmds.setRigLook(self.rig_root, mesh=meshes, file=files, asset=config.ASSET_ID, look=look, **kwargs))

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

    def edit_file(self, change, name):
        """The box weight file after change(data), written under a new name."""
        data = read_json(self.box_file)
        change(data)
        path = os.path.join(config.WORK_DIR, name).replace("\\", "/")
        write_json(path, data)
        return path

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
        result = self.set_look([box], [self.box_file], publishVersion="20261117T100000Z_bmorgan")["lookRoot"]
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

    def test_another_skeleton_is_stale_not_an_error(self):
        """The skeleton hash is a status: the look is applied, and the report says the look is stale."""
        rig_path = self.save_rig_skeleton("rig_skeleton.json")
        data = read_json(rig_path)
        data["joints"][1]["restLocal"][13] += 1.0   # one joint a unit higher
        look_path = os.path.join(config.WORK_DIR, "look_skeleton.json").replace("\\", "/")
        write_json(look_path, data)
        box = new_box()
        report = self.set_look([box], [self.box_file], skeleton=look_path, rigSkeleton=rig_path)
        self.assertTrue(report["skeletonStale"])
        self.assertEqual(self.members(), [box])
        self.assertIsNotNone(skin_cluster(box))

    def test_the_rigs_own_skeleton_is_not_stale(self):
        path = self.save_rig_skeleton("rig_skeleton.json")
        report = self.set_look([new_box()], [self.box_file], skeleton=path, rigSkeleton=path)
        self.assertFalse(report["skeletonStale"])


class WeightHashCheck(LookTestCase):
    """A weight file's skeletonHash is a mark, never a gate."""

    def test_a_file_for_another_skeleton_loads_and_is_marked_stale(self):
        bad = self.hash_file(self.box_file, ZERO_HASH, "look_box_bad_hash.json")
        box = new_box()
        report = self.set_look([box], [bad])
        self.assertIsNotNone(skin_cluster(box))
        mesh = report["meshes"][0]
        self.assertEqual((mesh["result"], mesh["staleSkeleton"], mesh["reasons"]), ("loaded", True, []))

    def test_a_file_with_the_rigs_hash_is_not_stale(self):
        mesh = self.set_look([new_box()], [self.box_file])["meshes"][0]
        self.assertEqual((mesh["result"], mesh["staleSkeleton"]), ("loaded", False))

    def test_a_file_with_no_hash_loads_and_is_not_stale(self):
        old = self.hash_file(self.box_file, "", "look_box_no_hash.json")
        box = new_box()
        mesh = self.set_look([box], [old])["meshes"][0]
        self.assertIsNotNone(skin_cluster(box))
        self.assertEqual((mesh["result"], mesh["staleSkeleton"]), ("loaded", False))

    def test_load_rig_skin_weights_loads_a_stale_file_too(self):
        bad = self.hash_file(self.box_file, ZERO_HASH, "look_box_bad_hash.json")
        box = new_box()
        cmds.loadRigSkinWeights(box, file=bad)
        self.assertIsNotNone(skin_cluster(box))

    def test_load_rig_skin_weights_is_exact_about_a_joint_that_is_gone(self):
        bad = self.edit_file(lambda data: data["influences"].__setitem__(0, ["Center", "gone"]), "look_box_gone.json")
        box = new_box()
        self.assertMayaError(lambda: cmds.loadRigSkinWeights(box, file=bad), "No joint in namespace")
        self.assertIsNone(skin_cluster(box))


class FallbackLadder(LookTestCase):
    """Each rung of the fallback ladder: loaded, folded, rigid. The file's skeleton is written by hand, so the parents are the test's."""

    def with_gone_joint(self, parent, extra_links=()):
        """The box file with its first influence renamed to a joint the rig doesn't have, whose parent is `parent`."""
        def change(data):
            data["influences"][0] = ["Center", "gone"]
            data["skeleton"] += [[["Center", "gone"], parent]] + [list(link) for link in extra_links]
        return self.edit_file(change, "look_box_gone.json")

    def influences(self, mesh):
        return sorted(cmds.skinCluster(skin_cluster(mesh), query=True, influence=True))

    def vertex_weights(self, mesh):
        cluster = skin_cluster(mesh)
        joints = (rig_name("a_00_M_JNT").lstrip(":"), rig_name("b_00_M_JNT").lstrip(":"))
        return [[round(cmds.skinPercent(cluster, "%s.vtx[%d]" % (mesh, v), transform=j, query=True), 6) for j in joints]
                for v in range(8)]

    def test_the_saved_file_carries_the_skeleton_structure(self):
        data = read_json(self.box_file)
        self.assertEqual(data["formatVersion"], 3)
        self.assertEqual(data["skeleton"], [[["Center", "a"], None], [["Center", "b"], ["Center", "a"]], [["Center", "c"], ["Center", "b"]]])

    def test_a_joint_that_is_gone_folds_into_its_parent(self):
        reference = new_box()
        self.set_look([reference], [self.box_file])
        expected = self.vertex_weights(reference)
        cmds.delete(reference)

        box = new_box()
        mesh = self.set_look([box], [self.with_gone_joint(["Center", "a"])], look="folded")["meshes"][0]
        self.assertEqual(mesh["result"], "folded")
        self.assertEqual(len(mesh["reasons"]), 1)
        self.assertIn("[Center, gone]", mesh["reasons"][0])
        self.assertIn("[Center, a]", mesh["reasons"][0])
        self.assertEqual(self.influences(box), sorted([rig_name("a_00_M_JNT").lstrip(":"), rig_name("b_00_M_JNT").lstrip(":")]))
        self.assertEqual(self.vertex_weights(box), expected)

    def test_a_chain_of_gone_joints_collapses_up_to_the_first_one_the_rig_has(self):
        path = self.with_gone_joint(["Center", "gone2"], extra_links=[(["Center", "gone2"], ["Center", "a"])])
        mesh = self.set_look([new_box()], [path])["meshes"][0]
        self.assertEqual(mesh["result"], "folded")
        self.assertIn("[Center, a]", mesh["reasons"][0])

    def test_a_parent_that_is_not_a_skin_joint_is_passed_through(self):
        """gone sits under b, b is in the rig but isn't a skin joint, so the weight goes on up to a."""
        def change(data):
            data["influences"] = [["Center", "gone"]]
            data["weights"] = [[0, 1.0] for _ in data["weights"]]
            data["skeleton"] += [[["Center", "gone"], ["Center", "b"]]]
        path = self.edit_file(change, "look_box_gone_under_b.json")
        cmds.rigJointMarkup(rig_name("b_00_M_JNT"), edit=True, skin=False)
        box = new_box()
        mesh = self.set_look([box], [path])["meshes"][0]
        self.assertEqual(mesh["result"], "folded")
        self.assertIn("went to [Center, a]", mesh["reasons"][0])
        self.assertEqual(self.influences(box), [rig_name("a_00_M_JNT").lstrip(":")])

    def test_a_gone_joint_with_no_skin_joint_above_it_goes_rigid(self):
        box = new_box()
        mesh = self.set_look([box], [self.with_gone_joint(None)])["meshes"][0]
        self.assertEqual(mesh["result"], "rigid")
        self.assertIn("no skin joint is above it", mesh["reasons"][0])
        self.assertEqual(self.influences(box), [rig_name("a_00_M_JNT").lstrip(":")])
        self.assertEqual(self.members(), [box])

    def test_a_file_with_no_skeleton_cannot_fold_and_goes_rigid(self):
        def change(data):
            data["formatVersion"] = 2
            data.pop("skeleton")
            data["influences"][0] = ["Center", "gone"]
        path = self.edit_file(change, "look_box_v2_gone.json")
        box = new_box()
        mesh = self.set_look([box], [path])["meshes"][0]
        self.assertEqual(mesh["result"], "rigid")
        self.assertIn("records no skeleton", mesh["reasons"][0])
        self.assertEqual(self.influences(box), [rig_name("a_00_M_JNT").lstrip(":")])

    def test_a_version_2_file_that_still_fits_loads_as_saved(self):
        def change(data):
            data["formatVersion"] = 2
            data.pop("skeleton")
        mesh = self.set_look([new_box()], [self.edit_file(change, "look_box_v2.json")])["meshes"][0]
        self.assertEqual(mesh["result"], "loaded")

    def test_a_changed_topology_goes_rigid_and_stays_in_the_look(self):
        box = cmds.polyCube(name=":%s:box_geo" % RIG, subdivisionsX=2, constructionHistory=False)[0]
        set_mesh_id(box, BOX_ID)
        mesh = self.set_look([box], [self.box_file])["meshes"][0]
        self.assertEqual((mesh["meshId"], mesh["result"]), (BOX_ID, "rigid"))
        self.assertIn("topology", mesh["reasons"][0])
        self.assertEqual(self.influences(box), [rig_name("a_00_M_JNT").lstrip(":")])
        self.assertEqual(self.members(), [box])

    def test_a_rigid_mesh_is_reported_with_its_reason(self):
        ball = new_ball()
        report = json.loads(cmds.setRigLook(self.rig_root, rigidMesh=[ball], asset=config.ASSET_ID, look="boxy"))
        self.assertEqual(report["meshes"], [{"meshId": BALL_ID, "meshName": "ball_geo", "result": "rigid",
                                             "reasons": ["the mesh has no weight file yet"], "staleSkeleton": False}])

    def test_the_report_lists_file_meshes_then_rigid_ones(self):
        box, ball = new_box(), new_ball()
        report = json.loads(cmds.setRigLook(self.rig_root, mesh=[box], file=[self.box_file], rigidMesh=[ball],
                                            asset=config.ASSET_ID, look="boxy"))
        self.assertEqual(report["lookRoot"], "%s:lookRoot" % RIG)
        self.assertEqual([(m["meshId"], m["result"]) for m in report["meshes"]], [(BOX_ID, "loaded"), (BALL_ID, "rigid")])

    def test_a_fallback_look_undoes_like_any_other(self):
        box = new_box()
        self.set_look([box], [self.with_gone_joint(["Center", "a"])])
        cmds.undo()
        self.assertNoLook()
        self.assertIsNone(skin_cluster(box))


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
        self.assertMayaError(lambda: self.set_look([ball], [self.box_file], look="ball"), "no weight file of its own")
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
    """A mesh with a meshId and no weight file yet is bound whole to the topmost skin joint: fkchain_three's a."""

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
