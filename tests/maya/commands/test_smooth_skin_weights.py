"""smoothSkinWeights.

The mesh is a 5 x 5 vertex plane, so vertex 7 is row 1, column 2, and its grid neighbors are 2, 6, 8 and 12. It is skinned to
joints one and two (and three where a test needs it). The weights start as a hard split, joint one owning columns 0 and 1.
A vertex becomes the average of itself and its neighbors, so every expected number is worked out by hand:
  vertex 7 (0) with neighbors 6 (1), 2, 8 and 12 (0): 1/5
  vertex 6 (1) with neighbors 1, 5 and 11 (1) and 7 (0): 4/5
  vertex 1 (1), on the edge, with neighbors 0 (1), 2 (0) and 6 (1): 3/4
"""
from maya import cmds

from rigtest.case import RigTestCase


class SmoothSkinWeights(RigTestCase):

    def setUp(self):
        super().setUp()
        self.plane = cmds.polyPlane(name="plane_geo", subdivisionsX=4, subdivisionsY=4, constructionHistory=False)[0]
        cmds.select(clear=True)
        self.joints = [cmds.joint(name="jointOne", position=(-1, 0, 0)), cmds.joint(name="jointTwo", position=(1, 0, 0))]
        cmds.select(clear=True)
        self.joints.append(cmds.joint(name="jointThree", position=(0, 1, 0)))
        self.cluster = cmds.skinCluster(self.joints[:2], self.plane, toSelectedBones=True)[0]
        self.set_split()

    def set_split(self, exceptions=None):
        """Joint one has columns 0 and 1, joint two the rest. exceptions maps a vertex to its (one, two) weights."""
        cmds.setAttr(self.cluster + ".normalizeWeights", 0)
        for vertex in range(25):
            one = 1.0 if vertex % 5 <= 1 else 0.0
            one, two = (exceptions or {}).get(vertex, (one, 1.0 - one))
            cmds.skinPercent(self.cluster, self.vtx(vertex), normalize=False,
                             transformValue=[(self.joints[0], one), (self.joints[1], two)])
        cmds.setAttr(self.cluster + ".normalizeWeights", 1)

    def vtx(self, vertex, mesh=None):
        return "%s.vtx[%d]" % (mesh or self.plane, vertex)

    def row(self, vertex, cluster=None, mesh=None):
        return cmds.skinPercent(cluster or self.cluster, self.vtx(vertex, mesh), query=True, value=True)

    def one(self, vertices=range(25)):
        """The weight of joint one on each vertex."""
        return [self.row(v)[0] for v in vertices]

    def assertRow(self, vertex, expected, places=6):
        actual = self.row(vertex)
        self.assertEqual(len(actual), len(expected))
        for a, e in zip(actual, expected):
            self.assertAlmostEqual(a, e, places=places, msg="vertex %d: %s != %s" % (vertex, actual, expected))

    def test_a_vertex_becomes_the_average_of_itself_and_its_neighbors(self):
        before = self.one()
        changed = cmds.smoothSkinWeights(self.vtx(7), iterations=1)
        self.assertEqual(changed, 1)
        self.assertRow(7, [0.2, 0.8])
        after = self.one()
        self.assertEqual([a for v, a in enumerate(after) if v != 7], [b for v, b in enumerate(before) if v != 7])

    def test_default_is_three_passes_and_unselected_neighbors_stay_fixed(self):
        # 0.2, then (0.2 + 1) / 5, then (0.24 + 1) / 5
        cmds.smoothSkinWeights(self.vtx(7))
        self.assertRow(7, [0.248, 0.752])

    def test_a_mesh_smooths_every_vertex_from_the_weights_before_the_pass(self):
        self.assertEqual(cmds.smoothSkinWeights(self.plane, iterations=1), 10)  # columns 1 and 2
        self.assertRow(6, [0.8, 0.2])
        self.assertRow(7, [0.2, 0.8])
        self.assertRow(1, [0.75, 0.25])
        self.assertRow(0, [1, 0])    # the corner: itself, 1 and 5, all 1
        self.assertRow(4, [0, 1])

    def test_only_the_vertices_that_moved_are_written(self):
        before = self.one()
        changed = cmds.smoothSkinWeights(self.plane, iterations=1)
        after = self.one()
        self.assertEqual(changed, len([v for v in range(25) if before[v] != after[v]]))

    def test_weights_still_add_up_to_one(self):
        cmds.smoothSkinWeights(self.plane, iterations=3)
        for vertex in range(25):
            self.assertAlmostEqual(sum(self.row(vertex)), 1.0, places=6)

    def test_strength_goes_part_of_the_way(self):
        cmds.smoothSkinWeights(self.vtx(7), iterations=1, strength=0.5)
        self.assertRow(7, [0.1, 0.9])

    def test_a_face_counts_as_the_vertices_it_touches(self):
        before = self.one()
        cmds.smoothSkinWeights("%s.f[5]" % self.plane, iterations=1)   # row 1, column 1: vertices 6, 7, 11 and 12
        after = self.one()
        self.assertEqual([v for v in range(25) if before[v] != after[v]], [6, 7, 11, 12])

    def test_acts_on_the_selection_when_nothing_is_named(self):
        cmds.select(self.vtx(7), self.vtx(12))
        # 12 (0): neighbors 7, 13, 17 (0) and 11 (1): 1/5 as well
        self.assertEqual(cmds.smoothSkinWeights(iterations=1), 2)
        self.assertRow(7, [0.2, 0.8])
        self.assertRow(12, [0.2, 0.8])

    def test_a_locked_influence_keeps_its_weight_and_the_rest_keep_their_total(self):
        cmds.skinCluster(self.cluster, edit=True, addInfluence=self.joints[2], weight=0)
        cmds.setAttr(self.cluster + ".normalizeWeights", 0)
        cmds.skinPercent(self.cluster, self.vtx(7), normalize=False,
                         transformValue=[(self.joints[0], 0), (self.joints[1], 0.5), (self.joints[2], 0.5)])
        cmds.setAttr(self.cluster + ".normalizeWeights", 1)
        cmds.setAttr(self.joints[2] + ".lockInfluenceWeights", 1)
        # vertex 7 has one 0, two 0.5 and three 0.5 (locked). Unlocked: one (0 + 1 + 0 + 0 + 0) / 5 = 0.2 and
        # two (0.5 + 0 + 1 + 1 + 1) / 5 = 0.7, which add to 0.9 and are scaled to the 0.5 they had: 1/9 and 7/18
        cmds.smoothSkinWeights(self.vtx(7), iterations=1)
        self.assertRow(7, [1 / 9, 7 / 18, 0.5])

    def test_an_influence_locked_everywhere_is_untouched_on_every_vertex(self):
        cmds.setAttr(self.joints[0] + ".lockInfluenceWeights", 1)
        before = self.one()
        self.assertEqual(cmds.smoothSkinWeights(self.plane, iterations=3), 0)
        self.assertEqual(self.one(), before)

    def test_the_clusters_maximum_influences_are_kept(self):
        cmds.setAttr(self.cluster + ".maintainMaxInfluences", True)
        cmds.setAttr(self.cluster + ".maxInfluences", 1)
        cmds.smoothSkinWeights(self.vtx(7), iterations=1)
        self.assertRow(7, [0, 1])    # 0.2 and 0.8 keep the larger, scaled back to 1

    def test_undo_restores_the_starting_weights_exactly_and_redo_smooths_again(self):
        before = [self.row(v) for v in range(25)]
        cmds.smoothSkinWeights(self.plane, iterations=2)
        smoothed = [self.row(v) for v in range(25)]
        self.assertNotEqual(smoothed, before)
        cmds.undo()
        self.assertEqual([self.row(v) for v in range(25)], before)
        cmds.redo()
        self.assertEqual([self.row(v) for v in range(25)], smoothed)

    def test_two_meshes_are_one_undo_step(self):
        second = cmds.polyPlane(name="second_geo", subdivisionsX=4, subdivisionsY=4, constructionHistory=False)[0]
        cluster = cmds.skinCluster(self.joints[:2], second, toSelectedBones=True)[0]
        cmds.setAttr(cluster + ".normalizeWeights", 0)
        for vertex in range(25):
            one = 1.0 if vertex % 5 <= 1 else 0.0
            cmds.skinPercent(cluster, "%s.vtx[%d]" % (second, vertex), normalize=False,
                             transformValue=[(self.joints[0], one), (self.joints[1], 1.0 - one)])
        cmds.setAttr(cluster + ".normalizeWeights", 1)

        self.assertEqual(cmds.smoothSkinWeights(self.vtx(7), "%s.vtx[7]" % second, iterations=1), 2)
        self.assertAlmostEqual(self.row(7, cluster, second)[0], 0.2, places=6)
        cmds.undo()
        self.assertEqual(self.row(7)[0], 0.0)
        self.assertEqual(self.row(7, cluster, second)[0], 0.0)

    def test_a_mesh_without_a_skin_cluster_changes_nothing(self):
        bare = cmds.polyCube(name="bare_geo", constructionHistory=False)[0]
        before = self.one()
        self.assertMayaError(lambda: cmds.smoothSkinWeights(self.plane, bare), "bare_geo", "no skinCluster")
        self.assertEqual(self.one(), before)

    def test_a_name_that_is_not_a_mesh_is_an_error(self):
        self.assertMayaError(lambda: cmds.smoothSkinWeights(self.joints[0]), "jointOne", "not a mesh")

    def test_nothing_named_or_selected_is_an_error(self):
        cmds.select(self.joints[0])
        self.assertMayaError(lambda: cmds.smoothSkinWeights(), "needs meshes")

    def test_flags_are_checked(self):
        self.assertMayaError(lambda: cmds.smoothSkinWeights(self.plane, iterations=0), "iterations")
        self.assertMayaError(lambda: cmds.smoothSkinWeights(self.plane, strength=0), "strength")
        self.assertMayaError(lambda: cmds.smoothSkinWeights(self.plane, strength=1.5), "strength")

    def test_weights_that_are_already_smooth_change_nothing(self):
        self.set_split({v: (0.25, 0.75) for v in range(25)})
        self.assertEqual(cmds.smoothSkinWeights(self.plane), 0)


class SmoothSkinWeightsNeighborPaths(RigTestCase):
    """A subset asks the mesh for just its vertices' neighbors, and the whole mesh builds the map in bulk. On a cone,
    which has triangles and an n-gon cap, the two must agree: each vertex smoothed alone (then undone) equals the
    same vertex in one pass over the whole mesh."""

    def test_one_vertex_at_a_time_equals_the_whole_mesh_at_once(self):
        cone = cmds.polyCone(name="cone_geo", subdivisionsAxis=7, subdivisionsHeight=2, constructionHistory=False)[0]
        count = cmds.polyEvaluate(cone, vertex=True)
        cmds.select(clear=True)
        joints = [cmds.joint(name="c%d" % i, position=(0, i, 0)) for i in range(3)]
        cluster = cmds.skinCluster(joints, cone, toSelectedBones=True)[0]
        cmds.setAttr(cluster + ".normalizeWeights", 0)
        for v in range(count):
            a, b = ((v * 37) % 11) / 10.0, ((v * 13) % 7) / 7.0
            c = 0.0 if a + b > 0.95 else 1.0 - a - b
            cmds.skinPercent(cluster, "%s.vtx[%d]" % (cone, v), normalize=False,
                             transformValue=[(joints[0], a * 0.9), (joints[1], b * 0.9), (joints[2], c)])
        cmds.setAttr(cluster + ".normalizeWeights", 1)

        def rows():
            return [cmds.skinPercent(cluster, "%s.vtx[%d]" % (cone, v), query=True, value=True) for v in range(count)]

        start = rows()
        alone = []
        for v in range(count):
            cmds.smoothSkinWeights("%s.vtx[%d]" % (cone, v), iterations=1)
            alone.append(rows()[v])
            cmds.undo()
        self.assertEqual(rows(), start)
        cmds.smoothSkinWeights(cone, iterations=1)
        for v, (one, whole) in enumerate(zip(alone, rows())):
            for x, y in zip(one, whole):
                self.assertAlmostEqual(x, y, places=6, msg="vertex %d: %s != %s" % (v, one, whole))
