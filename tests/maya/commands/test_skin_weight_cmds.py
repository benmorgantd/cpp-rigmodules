"""setRigMeshId, saveRigSkinWeights and loadRigSkinWeights (D35).

The mesh is a 2-unit-tall polyCube standing on the origin (box_geo), skinned to the fk_three joints a (y=0) and
b (y=2). polyCube's vertex order puts vertices 0, 1, 6 and 7 at the bottom and 2 to 5 at the top.
fixtures/weights/box_two_joints.json is a hand-written file for that cube with meshId "fixture-box".
"""
import json
import os
import struct

from maya import cmds
import maya.api.OpenMaya as om

from rigtest import config
from rigtest.case import RigTestCase, rig_name

FIXTURE_ID = "fixture-box"
FIXTURE_PATH = os.path.join(config.FIXTURES_DIR, "weights", "box_two_joints.json").replace("\\", "/")
# box_two_joints.json, per vertex, as [a, b]
FIXTURE_WEIGHTS = [[1, 0], [1, 0], [0.25, 0.75], [0.25, 0.75], [0.25, 0.75], [0.25, 0.75], [1, 0], [1, 0]]


def make_box(namespace):
    box = cmds.polyCube(name=":%s:box_geo" % namespace, height=2, constructionHistory=False)[0]
    cmds.move(0, 1, 0, box)
    cmds.makeIdentity(box, apply=True, translate=True)
    return box


def set_mesh_id(mesh, mesh_id):
    if not cmds.attributeQuery("meshId", node=mesh, exists=True):
        cmds.addAttr(mesh, longName="meshId", dataType="string")
    cmds.setAttr(mesh + ".meshId", mesh_id, type="string")


def skin_cluster(mesh):
    return (cmds.ls(cmds.listHistory(mesh), type="skinCluster") or [None])[0]


def weights(mesh, influences):
    """Per vertex, the weight of each influence in order, rounded to 6 decimals to compare with hand-written values.
    test_weights_round_trip_exactly checks the unrounded values."""
    cluster = skin_cluster(mesh)
    count = cmds.polyEvaluate(mesh, vertex=True)
    return [[round(cmds.skinPercent(cluster, "%s.vtx[%d]" % (mesh, v), transform=j, query=True), 6) for j in influences]
            for v in range(count)]


def topology_hash(mesh):
    """64-bit FNV-1a over the topology, written independently of the C++ in core/SkinWeightFile.cpp."""
    selection = om.MSelectionList()
    selection.add(mesh)
    fn_mesh = om.MFnMesh(selection.getDagPath(0))
    counts, connects = fn_mesh.getVertices()
    value = 14695981039346656037
    for number in [fn_mesh.numVertices, len(counts)] + list(counts) + list(connects):
        for byte in struct.pack("<I", number):
            value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % value


def world_point(mesh, index):
    return cmds.pointPosition("%s.vtx[%d]" % (mesh, index), world=True)


class SkinWeightTestCase(RigTestCase):
    """The fk_three skeleton in 'skel', with no rig, and box_geo beside it."""

    def setUp(self):
        super().setUp()
        self.joints = self.make_skeleton("fk_three")
        self.a, self.b, self.c = self.joints
        self.box = make_box(config.SKELETON_NAMESPACE)
        self.path = os.path.join(config.WORK_DIR, "box_weights.json").replace("\\", "/")
        if os.path.exists(self.path):
            os.remove(self.path)

    def bind(self, *joints):
        return cmds.skinCluster(*(joints or (self.a, self.b)), self.box, toSelectedBones=True)[0]

    def write_file(self, data):
        with open(self.path, "w", encoding="utf-8") as handle:
            json.dump(data, handle)

    def read_file(self):
        with open(self.path, encoding="utf-8") as handle:
            return json.load(handle)

    def fixture_data(self):
        with open(FIXTURE_PATH, encoding="utf-8") as handle:
            return json.load(handle)

    def save_bound_box(self):
        """Stamps an id, binds a and b and saves. Returns the weights Maya gave the bind."""
        cmds.setRigMeshId(self.box)
        self.bind()
        saved = weights(self.box, [self.a, self.b])
        cmds.saveRigSkinWeights(self.box, file=self.path)
        return saved


class MeshId(SkinWeightTestCase):

    def test_stamps_a_guid_once(self):
        first = cmds.setRigMeshId(self.box)
        self.assertEqual(len(first), 1)
        self.assertEqual(len(first[0]), 36)
        self.assertEqual(cmds.getAttr(self.box + ".meshId"), first[0])
        self.assertEqual(cmds.setRigMeshId(self.box), first)

    def test_new_id_replaces_the_old_one(self):
        first = cmds.setRigMeshId(self.box)
        second = cmds.setRigMeshId(self.box, newId=True)
        self.assertNotEqual(first, second)
        self.assertEqual(cmds.getAttr(self.box + ".meshId"), second[0])

    def test_shape_name_stamps_the_transform(self):
        shape = cmds.listRelatives(self.box, shapes=True, fullPath=True)[0]
        ids = cmds.setRigMeshId(shape)
        self.assertEqual(cmds.getAttr(self.box + ".meshId"), ids[0])

    def test_acts_on_selected_meshes_only(self):
        other = cmds.polyCube(name=":skel:other_geo", constructionHistory=False)[0]
        cmds.select(self.box, self.a, other)
        ids = cmds.setRigMeshId()
        self.assertEqual(ids, [cmds.getAttr(self.box + ".meshId"), cmds.getAttr(other + ".meshId")])
        self.assertFalse(cmds.attributeQuery("meshId", node=self.a, exists=True))

    def test_no_meshes_named_or_selected_fails(self):
        cmds.select(self.a)
        self.assertMayaError(lambda: cmds.setRigMeshId(), "needs meshes")

    def test_a_named_node_that_is_not_a_mesh_fails(self):
        self.assertMayaError(lambda: cmds.setRigMeshId(self.a), "is not a mesh")

    def test_a_missing_mesh_name_fails(self):
        self.assertMayaError(lambda: cmds.setRigMeshId("no_such_geo"), "No DAG node named no_such_geo")

    def test_undo_removes_the_attribute_and_redo_restores_it(self):
        ids = cmds.setRigMeshId(self.box)
        cmds.undo()
        self.assertFalse(cmds.attributeQuery("meshId", node=self.box, exists=True))
        cmds.redo()
        self.assertEqual(cmds.getAttr(self.box + ".meshId"), ids[0])


class SaveAndLoad(SkinWeightTestCase):

    def test_file_keys_influences_and_topology(self):
        mesh_id = cmds.setRigMeshId(self.box)[0]
        self.bind()
        self.assertEqual(cmds.saveRigSkinWeights(self.box, file=self.path), 2)
        data = self.read_file()
        self.assertEqual(data["formatVersion"], 1)
        self.assertEqual(data["meshId"], mesh_id)
        self.assertEqual(data["meshName"], "box_geo")
        self.assertEqual((data["vertexCount"], data["faceCount"]), (8, 6))
        self.assertEqual(data["topologyHash"], topology_hash(self.box))
        self.assertEqual(data["skinMethod"], "Linear")
        self.assertEqual(data["influences"], [["Center", "a"], ["Center", "b"]])
        self.assertNotIn("blendWeights", data)

    def test_weights_round_trip_exactly(self):
        cmds.setRigMeshId(self.box)
        cluster = self.bind()
        cmds.skinPercent(cluster, self.box + ".vtx[0]", transformValue=[(self.a, 1.0 / 3.0), (self.b, 2.0 / 3.0)])
        exact = [cmds.skinPercent(cluster, "%s.vtx[%d]" % (self.box, v), query=True, value=True) for v in range(8)]
        cmds.saveRigSkinWeights(self.box, file=self.path)
        self.assertEqual(self.read_file()["weights"][0], [0, exact[0][0], 1, exact[0][1]])
        cmds.delete(cluster)
        cluster = cmds.loadRigSkinWeights(self.box, file=self.path)
        self.assertEqual([cmds.skinPercent(cluster, "%s.vtx[%d]" % (self.box, v), query=True, value=True) for v in range(8)], exact)

    def test_influences_are_sorted_whatever_the_bind_order(self):
        cmds.setRigMeshId(self.box)
        self.bind(self.b, self.a)
        cmds.saveRigSkinWeights(self.box, file=self.path)
        self.assertEqual(self.read_file()["influences"], [["Center", "a"], ["Center", "b"]])

    def test_changing_one_vertex_changes_one_line(self):
        self.save_bound_box()
        with open(self.path, encoding="utf-8") as handle:
            before = handle.read().splitlines()
        cmds.skinPercent(skin_cluster(self.box), self.box + ".vtx[2]", transformValue=[(self.a, 0.5), (self.b, 0.5)])
        cmds.saveRigSkinWeights(self.box, file=self.path)
        with open(self.path, encoding="utf-8") as handle:
            after = handle.read().splitlines()
        self.assertEqual(len(before), len(after))
        changed = [i for i, (old, new) in enumerate(zip(before, after)) if old != new]
        self.assertEqual(len(changed), 1)
        self.assertEqual(json.loads(after[changed[0]].strip().rstrip(",")), [0, 0.5, 1, 0.5])

    def test_topology_hash_of_the_fixture_cube(self):
        self.assertEqual(topology_hash(self.box), self.fixture_data()["topologyHash"])

    def test_round_trip_onto_a_fresh_skin_cluster(self):
        saved = self.save_bound_box()
        cmds.delete(skin_cluster(self.box))
        self.assertTrue(cmds.loadRigSkinWeights(self.box, file=self.path))
        self.assertEqual(weights(self.box, [self.a, self.b]), saved)

    def test_hand_written_file_loads(self):
        set_mesh_id(self.box, FIXTURE_ID)
        cluster = cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH)
        self.assertEqual(cluster, skin_cluster(self.box))
        self.assertEqual(weights(self.box, [self.a, self.b]), FIXTURE_WEIGHTS)
        self.assertEqual(cmds.getAttr(cluster + ".skinningMethod"), 0)

    def test_existing_skin_cluster_is_updated_in_place(self):
        set_mesh_id(self.box, FIXTURE_ID)
        target = cmds.duplicate(self.box, name=":skel:box_target")[0]
        blend = cmds.blendShape(target, self.box)[0]
        cluster = self.bind()
        history = cmds.listHistory(self.box, pruneDagObjects=True)
        self.assertEqual(cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH), cluster)
        self.assertEqual(cmds.listHistory(self.box, pruneDagObjects=True), history)
        self.assertIn(blend, history)
        self.assertEqual(weights(self.box, [self.a, self.b]), FIXTURE_WEIGHTS)

    def test_influences_match_the_file_after_an_in_place_load(self):
        set_mesh_id(self.box, FIXTURE_ID)
        cluster = self.bind(self.a, self.c)
        cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH)
        self.assertEqual(sorted(cmds.skinCluster(cluster, query=True, influence=True)), ["skel:a_jnt", "skel:b_jnt"])
        self.assertEqual(weights(self.box, [self.a, self.b]), FIXTURE_WEIGHTS)

    def test_renamed_joint_still_loads(self):
        set_mesh_id(self.box, FIXTURE_ID)
        renamed = cmds.rename(self.b, ":skel:spine_jnt")
        cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH)
        self.assertEqual(weights(self.box, [self.a, renamed]), FIXTURE_WEIGHTS)

    def test_moved_vertex_still_loads(self):
        set_mesh_id(self.box, FIXTURE_ID)
        cmds.move(0, 0.5, 0, self.box + ".vtx[3]", relative=True)
        cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH)
        self.assertEqual(weights(self.box, [self.a, self.b]), FIXTURE_WEIGHTS)

    def test_weight_blended_saves_and_loads_blend_weights(self):
        cmds.setRigMeshId(self.box)
        cluster = self.bind()
        cmds.setAttr(cluster + ".skinningMethod", 2)
        for v in range(8):
            cmds.setAttr("%s.blendWeights[%d]" % (cluster, v), 0.5 if v < 4 else 0.25)
        cmds.saveRigSkinWeights(self.box, file=self.path)
        self.assertEqual(self.read_file()["blendWeights"], [0.5, 0.5, 0.5, 0.5, 0.25, 0.25, 0.25, 0.25])
        cmds.delete(cluster)
        cluster = cmds.loadRigSkinWeights(self.box, file=self.path)
        self.assertEqual(cmds.getAttr(cluster + ".skinningMethod"), 2)
        self.assertEqual([round(cmds.getAttr("%s.blendWeights[%d]" % (cluster, v)), 6) for v in range(8)],
                         [0.5, 0.5, 0.5, 0.5, 0.25, 0.25, 0.25, 0.25])


class UndoRedo(SkinWeightTestCase):

    def test_new_skin_cluster_goes_away_on_undo(self):
        set_mesh_id(self.box, FIXTURE_ID)
        cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH)
        cmds.undo()
        self.assertIsNone(skin_cluster(self.box))
        cmds.redo()
        self.assertEqual(weights(self.box, [self.a, self.b]), FIXTURE_WEIGHTS)

    def test_undo_restores_in_place_weights_and_influences(self):
        set_mesh_id(self.box, FIXTURE_ID)
        cluster = self.bind(self.a, self.c)
        before = weights(self.box, [self.a, self.c])
        cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH)
        cmds.undo()
        self.assertEqual(sorted(cmds.skinCluster(cluster, query=True, influence=True)), ["skel:a_jnt", "skel:c_jnt"])
        self.assertEqual(weights(self.box, [self.a, self.c]), before)
        cmds.redo()
        self.assertEqual(weights(self.box, [self.a, self.b]), FIXTURE_WEIGHTS)


class SaveErrors(SkinWeightTestCase):

    def assertNotSaved(self, fn, *contains):
        self.assertMayaError(fn, *contains)
        self.assertFalse(os.path.exists(self.path))

    def test_mesh_without_id(self):
        self.bind()
        self.assertNotSaved(lambda: cmds.saveRigSkinWeights(self.box, file=self.path), "has no meshId", "setRigMeshId")

    def test_mesh_sharing_its_id(self):
        cmds.setRigMeshId(self.box)
        cmds.duplicate(self.box, name=":skel:box_copy")
        self.bind()
        self.assertNotSaved(lambda: cmds.saveRigSkinWeights(self.box, file=self.path), "box_copy", "newId=True")

    def test_mesh_without_skin_cluster(self):
        cmds.setRigMeshId(self.box)
        self.assertNotSaved(lambda: cmds.saveRigSkinWeights(self.box, file=self.path), "has no skinCluster")

    def test_unmarked_influence(self):
        cmds.setRigMeshId(self.box)
        cmds.rigJointMarkup(self.b, remove=True)
        self.bind()
        self.assertNotSaved(lambda: cmds.saveRigSkinWeights(self.box, file=self.path), "b_jnt", "no joint markup")

    def test_influence_marked_skin_false(self):
        cmds.setRigMeshId(self.box)
        cmds.rigJointMarkup(self.b, edit=True, skin=False)
        self.bind()
        self.assertNotSaved(lambda: cmds.saveRigSkinWeights(self.box, file=self.path), "b_jnt", "skin: false")

    def test_weights_not_normalized(self):
        cmds.setRigMeshId(self.box)
        cluster = self.bind()
        cmds.setAttr(cluster + ".normalizeWeights", 0)
        cmds.skinPercent(cluster, self.box + ".vtx[5]", transformValue=[(self.a, 0.25), (self.b, 0.25)], normalize=False)
        self.assertNotSaved(lambda: cmds.saveRigSkinWeights(self.box, file=self.path),
                            "1 vertex is not normalized", "5 (sum 0.5)")
        self.assertEqual(weights(self.box, [self.a, self.b])[5], [0.25, 0.25])

    def test_missing_file_flag(self):
        cmds.setRigMeshId(self.box)
        self.bind()
        self.assertMayaError(lambda: cmds.saveRigSkinWeights(self.box), "needs -file")


class LoadErrors(SkinWeightTestCase):
    """Every failed load leaves the mesh without a skinCluster."""

    def assertNotLoaded(self, path, *contains):
        self.assertMayaError(lambda: cmds.loadRigSkinWeights(self.box, file=path), *contains)
        self.assertIsNone(skin_cluster(self.box))

    def write_fixture_with(self, **changes):
        data = self.fixture_data()
        data.update(changes)
        self.write_file(data)
        return self.path

    def test_other_mesh_id(self):
        set_mesh_id(self.box, "another-mesh")
        self.assertNotLoaded(FIXTURE_PATH, "fixture-box", "another-mesh")

    def test_mesh_without_id(self):
        self.assertNotLoaded(FIXTURE_PATH, "has no meshId")

    def test_changed_vertex_count(self):
        cmds.delete(self.box)
        self.box = cmds.polyCube(name=":skel:box_geo", subdivisionsX=2, constructionHistory=False)[0]
        set_mesh_id(self.box, FIXTURE_ID)
        self.assertNotLoaded(FIXTURE_PATH, "topology changed", "8 vertices", "12 and")

    def test_same_counts_with_reversed_faces(self):
        set_mesh_id(self.box, FIXTURE_ID)
        cmds.polyNormal(self.box, normalMode=0, constructionHistory=False)
        self.assertNotLoaded(FIXTURE_PATH, "topology changed", "8 vertices and 6 faces")

    def test_no_joint_with_an_influence_markup(self):
        set_mesh_id(self.box, FIXTURE_ID)
        cmds.rigJointMarkup(self.b, remove=True)
        self.assertNotLoaded(FIXTURE_PATH, "[Center, b]", ":skel")

    def test_unsupported_format_version(self):
        set_mesh_id(self.box, FIXTURE_ID)
        self.assertNotLoaded(self.write_fixture_with(formatVersion=2), "formatVersion", "unsupported")

    def test_unknown_key(self):
        set_mesh_id(self.box, FIXTURE_ID)
        self.assertNotLoaded(self.write_fixture_with(extra=1), "extra")

    def test_weights_list_shorter_than_vertex_count(self):
        set_mesh_id(self.box, FIXTURE_ID)
        self.assertNotLoaded(self.write_fixture_with(weights=[[0, 1]]), "weight file.weights", "one entry per vertex")

    def test_influence_index_out_of_range(self):
        set_mesh_id(self.box, FIXTURE_ID)
        data = self.fixture_data()
        data["weights"][0] = [2, 1]
        self.write_file(data)
        self.assertNotLoaded(self.path, "weights[0]", "not an influence index")

    def test_blend_weights_without_weight_blended(self):
        set_mesh_id(self.box, FIXTURE_ID)
        self.assertNotLoaded(self.write_fixture_with(blendWeights=[0] * 8), "blendWeights", "only for WeightBlended")

    def test_weights_not_normalized(self):
        set_mesh_id(self.box, FIXTURE_ID)
        data = self.fixture_data()
        data["weights"][3] = [0, 0.25, 1, 0.5]
        self.write_file(data)
        self.assertNotLoaded(self.path, "1 vertex is not normalized", "3 (sum 0.75)")

    def test_influence_twice_on_one_vertex(self):
        set_mesh_id(self.box, FIXTURE_ID)
        data = self.fixture_data()
        data["weights"][0] = [0, 0.5, 0, 0.5]
        self.write_file(data)
        self.assertNotLoaded(self.path, "weights[0]", "listed twice")

    def test_influence_listed_twice(self):
        set_mesh_id(self.box, FIXTURE_ID)
        self.assertNotLoaded(self.write_fixture_with(influences=[["Center", "a"], ["Center", "a"]]), "listed twice")


class PruneToMaxInfluences(SkinWeightTestCase):
    """box_geo bound to a, b and c, in that order, so their influence indices are 0, 1 and 2."""

    def setUp(self):
        super().setUp()
        self.cluster = self.bind(self.a, self.b, self.c)
        cmds.setAttr(self.cluster + ".normalizeWeights", 0)
        self.rows = {0: (0.5, 0.3, 0.2), 1: (0.25, 0.25, 0.5), 2: (0.7, 0.3, 0)}
        for vertex, (a, b, c) in self.rows.items():
            cmds.skinPercent(self.cluster, "%s.vtx[%d]" % (self.box, vertex),
                             transformValue=[(self.a, a), (self.b, b), (self.c, c)], normalize=False)
        cmds.setAttr(self.cluster + ".normalizeWeights", 1)

    def row(self, vertex):
        return cmds.skinPercent(self.cluster, "%s.vtx[%d]" % (self.box, vertex), query=True, value=True)

    def test_keeps_the_largest_and_normalizes(self):
        cmds.pruneToMaxInfluences(self.box, maxInfluences=2)
        self.assertEqual([round(w, 6) for w in self.row(0)], [0.625, 0.375, 0])

    def test_equal_weights_keep_the_lower_influence_index(self):
        cmds.pruneToMaxInfluences(self.box, maxInfluences=2)
        self.assertEqual([round(w, 6) for w in self.row(1)], [round(1 / 3, 6), 0, round(2 / 3, 6)])

    def test_vertices_within_the_limit_are_not_written(self):
        before = [self.row(v) for v in range(8)]
        changed = cmds.pruneToMaxInfluences(self.box, maxInfluences=2)
        after = [self.row(v) for v in range(8)]
        self.assertEqual([v for v in range(8) if before[v] != after[v]], [0, 1])
        self.assertEqual(changed, 2)

    def test_undo_restores_every_weight_exactly_and_redo_prunes_again(self):
        before = [self.row(v) for v in range(8)]
        cmds.pruneToMaxInfluences(self.box, maxInfluences=1)
        pruned = [self.row(v) for v in range(8)]
        cmds.undo()
        self.assertEqual([self.row(v) for v in range(8)], before)
        cmds.redo()
        self.assertEqual([self.row(v) for v in range(8)], pruned)

    def test_pruned_weights_save(self):
        cmds.pruneToMaxInfluences(self.box, maxInfluences=1)
        cmds.setRigMeshId(self.box)
        cmds.saveRigSkinWeights(self.box, file=self.path)
        self.assertTrue(all(len(pairs) == 2 for pairs in self.read_file()["weights"]))

    def test_acts_on_selected_meshes(self):
        cmds.select(self.box, self.a)
        self.assertEqual(cmds.pruneToMaxInfluences(maxInfluences=2), 2)

    def test_a_mesh_without_skin_cluster_changes_nothing(self):
        other = cmds.polyCube(name=":skel:other_geo", constructionHistory=False)[0]
        before = [self.row(v) for v in range(8)]
        self.assertMayaError(lambda: cmds.pruneToMaxInfluences(self.box, other, maxInfluences=2), "other_geo", "no skinCluster")
        self.assertEqual([self.row(v) for v in range(8)], before)

    def test_max_influences_is_required_and_at_least_one(self):
        self.assertMayaError(lambda: cmds.pruneToMaxInfluences(self.box), "maxInfluences")
        self.assertMayaError(lambda: cmds.pruneToMaxInfluences(self.box, maxInfluences=0), "at least 1")


class PythonSkinWeights(SkinWeightTestCase):
    """rigmodules.skinweights, the helpers pipeline tools loop over meshes with."""

    def setUp(self):
        super().setUp()
        import rigmodules.skinweights
        self.module = rigmodules.skinweights

    def test_get_mesh_id(self):
        self.assertIsNone(self.module.get_mesh_id(self.box))
        mesh_id = cmds.setRigMeshId(self.box)[0]
        self.assertEqual(self.module.get_mesh_id(self.box), mesh_id)

    def test_get_meshes_by_id_in_one_namespace(self):
        mesh_id = cmds.setRigMeshId(self.box)[0]
        cmds.polyCube(name=":skel:no_id_geo", constructionHistory=False)
        other = cmds.polyCube(name=":other_geo", constructionHistory=False)[0]
        cmds.setRigMeshId(other)
        self.assertEqual({k: v.split("|")[-1] for k, v in self.module.get_meshes("skel").items()}, {mesh_id: "skel:box_geo"})

    def test_get_meshes_fails_on_a_shared_id(self):
        cmds.setRigMeshId(self.box)
        cmds.duplicate(self.box, name=":skel:box_copy")
        with self.assertRaisesRegex(RuntimeError, "newId=True"):
            self.module.get_meshes("skel")

    def test_get_skinned_meshes(self):
        cmds.polyCube(name=":skel:unskinned_geo", constructionHistory=False)
        self.bind()
        self.assertEqual([m.split("|")[-1] for m in self.module.get_skinned_meshes("skel")], ["skel:box_geo"])

    def test_read_file_mesh_id_from_a_saved_file_and_a_reordered_one(self):
        mesh_id = self.save_bound_box() and cmds.getAttr(self.box + ".meshId")
        self.assertEqual(self.module.read_file_mesh_id(self.path), mesh_id)
        data = self.read_file()
        self.write_file(dict(sorted(data.items(), key=lambda item: item[0] != "weights")))   # weights first
        self.assertEqual(self.module.read_file_mesh_id(self.path), mesh_id)


class BindAtRest(RigTestCase):
    """Built into rig:, so a and b are driven by fkchain_1 and their rest comes from the module."""

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_three", "fk_three")
        self.box = make_box(config.RIG_NAMESPACE)
        set_mesh_id(self.box, FIXTURE_ID)

    def test_module_stores_each_joint_rest(self):
        module = rig_name("fkchain_1")
        self.assertEqual([round(v, 6) for v in cmds.getAttr(module + ".jointRestWorld[1]")],
                         [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 0, 1])

    def test_load_on_a_posed_rig_binds_the_build_pose(self):
        # Rotating a_ctrl_c 90 degrees about Z swings the whole chain rigidly about the origin, so every vertex
        # turns with it whatever its weights: (x, y, z) -> (-y, x, z). Binding at the posed joints would leave the
        # box upright instead.
        cmds.setAttr(rig_name("a_ctrl_c") + ".rotateZ", 90)
        cluster = cmds.loadRigSkinWeights(self.box, file=FIXTURE_PATH)
        for index, expected in ((0, (0, -0.5, 0.5)), (3, (-2, 0.5, 0.5))):
            self.assertEqual([round(v, 4) for v in world_point(self.box, index)], list(expected))
        self.assertEqual([round(v, 6) for v in cmds.getAttr(cluster + ".bindPreMatrix[1]")],
                         [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1])

        cmds.setAttr(rig_name("a_ctrl_c") + ".rotateZ", 0)
        self.assertEqual([round(v, 4) for v in world_point(self.box, 3)], [0.5, 2, 0.5])
