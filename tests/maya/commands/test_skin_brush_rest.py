"""The rest-match skin brushes, below the mouse: skinBrushStroke with operation="restDelta" or "restEdge" moves the
weights of the vertices a dab reaches towards the blend of influences that puts each vertex where the rest shape wants it
in the current pose. Both operations share one solve and are tested through the same cases.

The mesh is a 5 x 5 vertex plane, 0.25 between vertices, flat at y = 0 (vertex = row * 5 + column, vertex 12 the center
at (0, 0, 0), face 5 touching it). Two joints, the second a child of the first at (1, 0, 0), joint one owning columns 0
and 1 and joint two columns 2 to 4 (so vertex 12, 13, 7 and 17 are joint two's and 11 is joint one's).

The hand-worked case: joint two moves 0.5 along +x. Vertex 12 (all joint two) is then 0.5 along x from rest, its left
neighbor 11 (all joint one) has not moved, and 7, 13 and 17 (joint two) are 0.5 along x. The plane is flat and regular, so
vertex 12's rest delta is 0 and its rest edges are 0.25 long. Where the rest shape wants vertex 12 is
  restDelta: the average of its neighbors, (0 + 0.5 + 0.5 + 0.5) / 4 = 0.375 along x;
  restEdge: for each neighbor, the point 0.25 from it in the direction of vertex 12. From 11 (at -0.25) that is 0. 13, 7
            and 17 already are 0.25 from vertex 12 (at 0.5 along x), so they give 0.5. The average is
            (0 + 0.5 + 0.5 + 0.5) / 4 = 0.375.
Both want 0.375, and with candidate positions 0 (joint one) and 0.5 (joint two) that is joint two's weight 0.75 and
joint one's 0.25. The solve prefers the starting weights by a thousandth of the spread, which moves that by under 2e-4.
"""
from maya import cmds

from rigtest.case import RigTestCase

CENTER = (0.0, 0.0, 0.0)
EDGE = 0.25


class RestMatchCases(object):
    """The cases both operations must pass. OPERATION is set by the test classes below."""

    OPERATION = None

    def setUp(self):
        super().setUp()
        self.hit = CENTER
        cmds.select(clear=True)
        self.joints = [cmds.joint(name="jointOne", position=(-1, 0, 0)), cmds.joint(name="jointTwo", position=(1, 0, 0))]
        cmds.select(clear=True)
        self.plane = cmds.polyPlane(name="plane_geo", subdivisionsX=4, subdivisionsY=4, constructionHistory=False)[0]
        self.cluster = cmds.skinCluster(self.joints, self.plane, toSelectedBones=True)[0]
        cmds.setAttr(self.cluster + ".normalizeWeights", 0)
        for vertex in range(25):
            one = 1.0 if vertex % 5 <= 1 else 0.0
            cmds.skinPercent(self.cluster, "%s.vtx[%d]" % (self.plane, vertex), normalize=False,
                             transformValue=[(self.joints[0], one), (self.joints[1], 1.0 - one)])
        cmds.setAttr(self.cluster + ".normalizeWeights", 1)

    def row(self, vertex):
        return cmds.skinPercent(self.cluster, "%s.vtx[%d]" % (self.plane, vertex), query=True, value=True)

    def rows(self):
        return [self.row(v) for v in range(25)]

    def stroke(self, radius=0.3, strength=1.0, smoothness=0.0, **flags):
        """smoothness is 0 unless a test asks, so the hand-worked cases are the plain solve."""
        return cmds.skinBrushStroke(operation=self.OPERATION, radius=radius, strength=strength, smoothness=smoothness,
                                    hitPoint=[(self.plane, 5) + self.hit], **flags)

    def stretch(self):
        cmds.xform(self.joints[1], translation=(0.5, 0, 0), relative=True, worldSpace=True)
        # Vertex 12 is on joint two, so it is now at x = 0.5, and the brush reaches along the deformed mesh from there.
        self.hit = (0.5, 0.0, 0.0)

    def edge_error(self):
        """How far every edge is from its rest length, squared and added up, in the current pose. Squared on purpose: the
        mesh is pinned at both ends (column 0 on joint one, column 4 on joint two), so the stretch of 0.5 across it cannot
        shrink in a few dabs, only spread over more edges, which leaves the plain sum alone and lowers the squares."""
        def point(vertex):
            return cmds.pointPosition("%s.vtx[%d]" % (self.plane, vertex), world=True)

        error = 0.0
        for vertex in range(25):
            for other in ((vertex + 1) if vertex % 5 < 4 else None, (vertex + 5) if vertex < 20 else None):
                if other is not None:
                    a, b = point(vertex), point(other)
                    error += (sum((a[i] - b[i]) ** 2 for i in range(3)) ** 0.5 - EDGE) ** 2
        return error

    def test_nothing_changes_at_the_rest_pose(self):
        before = self.rows()
        self.assertEqual(self.stroke(radius=0.6), 0)
        self.assertEqual(self.rows(), before)

    def test_a_rigid_move_of_the_whole_mesh_changes_nothing(self):
        # Both joints turn together, so every vertex is its rest position turned, whatever its weights: nothing to fix, and
        # a rest delta turned by a rotation that is not the identity is where the vertex already is.
        cmds.rotate(0, 0, 45, self.joints[0])
        before = self.rows()
        self.assertEqual(self.stroke(radius=0.6), 0)
        self.assertEqual(self.rows(), before)

    def test_a_stretched_vertex_goes_to_the_weights_that_put_it_where_its_neighbors_want_it(self):
        self.stretch()
        self.stroke()
        self.assertAlmostEqual(self.row(12)[0], 0.25, places=3)
        self.assertAlmostEqual(self.row(12)[1], 0.75, places=3)

    def test_strength_goes_part_of_the_way(self):
        self.stretch()
        self.stroke(strength=0.5)
        self.assertAlmostEqual(self.row(12)[0], 0.125, places=3)

    def test_weights_still_add_up_to_one_and_stay_between_0_and_1(self):
        self.stretch()
        self.stroke(radius=1.0)
        for vertex in range(25):
            weights = self.row(vertex)
            self.assertAlmostEqual(sum(weights), 1.0, places=6)
            self.assertTrue(all(w > -1e-9 for w in weights), "vertex %d: %s" % (vertex, weights))

    def test_strokes_bring_the_edges_back_towards_their_rest_lengths(self):
        self.stretch()
        before = self.edge_error()
        for _ in range(5):
            self.stroke(radius=1.0)
        self.assertLess(self.edge_error(), before)

    def test_an_influence_locked_everywhere_leaves_nothing_to_move(self):
        self.stretch()
        cmds.setAttr(self.joints[0] + ".lockInfluenceWeights", 1)
        before = self.rows()
        self.assertEqual(self.stroke(radius=0.6), 0)
        self.assertEqual(self.rows(), before)

    def test_a_maximum_of_one_influence_leaves_every_row_with_one(self):
        self.stretch()
        cmds.setAttr(self.cluster + ".maintainMaxInfluences", 1)
        cmds.setAttr(self.cluster + ".maxInfluences", 1)
        self.stroke(radius=1.0)
        for vertex in range(25):
            self.assertLessEqual(sum(1 for w in self.row(vertex) if w > 1e-9), 1, "vertex %d" % vertex)

    def test_undo_restores_the_starting_weights_exactly_and_redo_repeats_the_stroke(self):
        self.stretch()
        before = self.rows()
        self.stroke(radius=0.6)
        painted = self.rows()
        self.assertNotEqual(painted, before)
        cmds.undo()
        self.assertEqual(self.rows(), before)
        cmds.redo()
        self.assertEqual(self.rows(), painted)

    def test_nothing_changes_at_the_rest_pose_whatever_the_smoothness(self):
        # Smoothness blends the changes of neighbors, not their weights, so a pose that already fits stays put.
        before = self.rows()
        self.assertEqual(self.stroke(radius=0.6, smoothness=1.0), 0)
        self.assertEqual(self.rows(), before)

    def test_smoothness_changes_the_result_and_keeps_the_weights_valid(self):
        self.stretch()
        self.stroke(radius=1.0)
        plain = self.rows()
        cmds.undo()
        self.stroke(radius=1.0, smoothness=0.5)
        blended = self.rows()
        self.assertNotEqual(plain, blended)
        for weights in blended:
            self.assertAlmostEqual(sum(weights), 1.0, places=6)
            self.assertTrue(all(w > -1e-9 for w in weights), weights)

    def test_the_default_smoothness_is_0_15(self):
        self.stretch()
        cmds.skinBrushStroke(operation=self.OPERATION, radius=1.0, strength=1.0, hitPoint=[(self.plane, 5) + self.hit])
        by_default = self.rows()
        cmds.undo()
        self.stroke(radius=1.0, smoothness=0.15)
        self.assertEqual(self.rows(), by_default)

    def test_smoothness_must_be_from_0_to_1(self):
        self.assertMayaError(lambda: self.stroke(smoothness=1.5), "smoothness")
        self.assertMayaError(lambda: self.stroke(smoothness=-0.1), "smoothness")

    def test_dual_quaternion_skinning_is_refused(self):
        cmds.setAttr(self.cluster + ".skinningMethod", 1)
        self.assertMayaError(lambda: self.stroke(), "linear")


class SkinBrushRestDelta(RestMatchCases, RigTestCase):
    OPERATION = "restDelta"


class SkinBrushRestEdge(RestMatchCases, RigTestCase):
    OPERATION = "restEdge"
