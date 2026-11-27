"""The skin weight brush, below the mouse: skinBrushStroke replays a stroke from hits or rays, and skinBrushCtx holds
the settings. Real mouse input, the drawn visualizer, Ctrl + drag resizing and the selection restriction live in the
context's events, so they are checked by hand in a GUI Maya, not here.

The mesh is a 5 x 5 vertex plane, 0.25 between vertices, flat at y = 0, so vertex 12 is the center (0, 0, 0) and its
neighbors 7, 11, 13 and 17 are 0.25 away along the surface. Face 5 touches vertex 12. It is skinned to two joints, joint
one owning columns 0 and 1 (the weights of the smoothing tests). A dab of radius 0.3 at the center reaches vertex 12 and the four
neighbors, and not the diagonals (0.5 along the edges), and the falloff is smoothstep:
  vertex 12, distance 0:      scale 1
  vertices 7, 11, 13, 17, distance 0.25: t = 5/6, s = 1/6, scale s * s * (3 - 2 * s) = 2/27
With strength 1 and one pass a vertex becomes itself plus scale * (the average of itself and its neighbors - itself):
  vertex 12 (0), neighbors 11 (1) and 7, 13, 17 (0): average 1/5, so 0.2
  vertex 11 (1), neighbors 6, 10, 16 (1) and 12 (0): average 4/5, so 1 - 2/27 * 1/5
  vertex 7 (0), neighbors 6 (1) and 2, 8, 12 (0): average 1/5, so 2/27 * 1/5
  vertex 17 (0), neighbors 16 (1) and 12, 18, 22 (0): average 1/5, so 2/27 * 1/5
  vertex 13 stays 0: its neighbors 8, 12, 14 and 18 are all 0

The rest-match brushes (SkinBrushRestDelta, SkinBrushRestEdge): skinBrushStroke with operation="restDelta" or "restEdge" moves the
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
joint one's 0.25. The solve prefers the starting weights by a thousandth of the spread, which moves that by under 2e-4."""
import unittest

from maya import cmds

from rigtest.case import RigTestCase


SCALE = 2.0 / 27.0
CENTER = (0.0, 0.0, 0.0)


class SkinBrushTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        cmds.select(clear=True)
        self.joints = [cmds.joint(name="jointOne", position=(-1, 0, 0)), cmds.joint(name="jointTwo", position=(1, 0, 0))]
        cmds.select(clear=True)
        self.plane, self.cluster = self.skinned_plane("plane_geo")

    def skinned_plane(self, name, height=0.0):
        plane = cmds.polyPlane(name=name, subdivisionsX=4, subdivisionsY=4, constructionHistory=False)[0]
        if height:
            cmds.move(0, height, 0, plane)
            cmds.makeIdentity(plane, apply=True, translate=True)
        cluster = cmds.skinCluster(self.joints, plane, toSelectedBones=True)[0]
        self.split(plane, cluster)
        return plane, cluster

    def split(self, mesh, cluster, first=0, count=25):
        """Joint one has columns 0 and 1 and joint two the rest, on count vertices from first."""
        cmds.setAttr(cluster + ".normalizeWeights", 0)
        for vertex in range(first, first + count):
            one = 1.0 if (vertex - first) % 5 <= 1 else 0.0
            cmds.skinPercent(cluster, "%s.vtx[%d]" % (mesh, vertex), normalize=False,
                             transformValue=[(self.joints[0], one), (self.joints[1], 1.0 - one)])
        cmds.setAttr(cluster + ".normalizeWeights", 1)

    def row(self, vertex, mesh=None, cluster=None):
        return cmds.skinPercent(cluster or self.cluster, "%s.vtx[%d]" % (mesh or self.plane, vertex), query=True, value=True)

    def rows(self, count=25, mesh=None, cluster=None, first=0):
        return [self.row(v, mesh, cluster) for v in range(first, first + count)]

    def stroke(self, *hits, **flags):
        flags.setdefault("operation", "smooth")
        flags.setdefault("radius", 0.3)
        flags.setdefault("strength", 1.0)
        return cmds.skinBrushStroke(hitPoint=list(hits), **flags)

    def center_hit(self, mesh=None):
        return (mesh or self.plane, 5) + CENTER

    def assertWeight(self, vertex, expected, places=5, mesh=None, cluster=None):
        actual = self.row(vertex, mesh, cluster)[0]
        self.assertAlmostEqual(actual, expected, places=places, msg="vertex %d: joint one %s != %s" % (vertex, actual, expected))


class SkinBrushDab(SkinBrushTestCase):

    def test_a_dab_smooths_the_vertices_within_the_radius_with_falloff(self):
        before = self.rows()
        self.assertEqual(self.stroke(self.center_hit()), 4)    # 13 does not move
        self.assertWeight(12, 0.2)
        self.assertWeight(11, 1.0 - SCALE * 0.2)
        self.assertWeight(7, SCALE * 0.2)
        self.assertWeight(17, SCALE * 0.2)
        after = self.rows()
        for vertex in range(25):
            if vertex not in (7, 11, 12, 17):
                self.assertEqual(after[vertex], before[vertex], "vertex %d" % vertex)

    def test_strength_goes_part_of_the_way(self):
        self.stroke(self.center_hit(), strength=0.5)
        self.assertWeight(12, 0.1)

    def test_a_bigger_radius_reaches_further_along_the_surface(self):
        # 0.6 reaches the diagonals (0.5) and the vertices two away (0.5), and not the ones three away (0.75)
        before = self.rows()
        self.stroke(self.center_hit(), radius=0.6)
        after = self.rows()
        moved = [v for v in range(25) if after[v] != before[v]]
        self.assertIn(6, moved)     # a diagonal of 12
        self.assertIn(2, moved)     # two up
        self.assertNotIn(0, moved)  # a corner, 1 away along the edges

    def test_weights_still_add_up_to_one(self):
        self.stroke(self.center_hit(), radius=0.6)
        for vertex in range(25):
            self.assertAlmostEqual(sum(self.row(vertex)), 1.0, places=6)

    def test_an_influence_locked_everywhere_is_never_changed(self):
        cmds.setAttr(self.joints[0] + ".lockInfluenceWeights", 1)
        before = self.rows()
        self.assertEqual(self.stroke(self.center_hit()), 0)
        self.assertEqual(self.rows(), before)


class SkinBrushPicking(SkinBrushTestCase):
    """A ray picks the hit, and the dab it makes is the one a hit at the same place makes."""

    def test_a_ray_down_onto_the_mesh_dabs_where_it_lands(self):
        cmds.skinBrushStroke(operation="smooth", radius=0.3, strength=1.0, mesh=[self.plane], viewRay=[(0, 5, 0, 0, -1, 0)])
        self.assertWeight(12, 0.2)
        self.assertWeight(11, 1.0 - SCALE * 0.2)
        self.assertWeight(7, SCALE * 0.2)

    def test_a_ray_that_misses_changes_nothing(self):
        before = self.rows()
        self.assertEqual(cmds.skinBrushStroke(radius=0.3, mesh=[self.plane], viewRay=[(5, 5, 5, 0, -1, 0)]), 0)
        self.assertEqual(self.rows(), before)

    def test_the_closest_surface_wins_across_meshes(self):
        high, high_cluster = self.skinned_plane("high_geo", 0.1)
        low_before = self.rows()
        cmds.skinBrushStroke(radius=0.3, strength=1.0, mesh=[self.plane, high], viewRay=[(0, 5, 0, 0, -1, 0)])
        self.assertEqual(self.rows(), low_before)
        self.assertWeight(12, 0.2, mesh=high, cluster=high_cluster)

    def test_through_mesh_dabs_every_surface_the_ray_crosses(self):
        high, high_cluster = self.skinned_plane("high_geo", 0.1)
        cmds.skinBrushStroke(radius=0.3, strength=1.0, throughMesh=True, mesh=[self.plane, high], viewRay=[(0, 5, 0, 0, -1, 0)])
        self.assertWeight(12, 0.2)
        self.assertWeight(12, 0.2, mesh=high, cluster=high_cluster)

    def test_a_stroke_across_two_meshes_is_one_undo_step(self):
        high, high_cluster = self.skinned_plane("high_geo", 0.1)
        low_before = self.rows()
        high_before = self.rows(mesh=high, cluster=high_cluster)
        self.assertEqual(cmds.skinBrushStroke(radius=0.3, strength=1.0, throughMesh=True, mesh=[self.plane, high], viewRay=[(0, 5, 0, 0, -1, 0)]), 8)
        cmds.undo()
        self.assertEqual(self.rows(), low_before)
        self.assertEqual(self.rows(mesh=high, cluster=high_cluster), high_before)
        cmds.redo()
        self.assertNotEqual(self.rows(), low_before)
        self.assertNotEqual(self.rows(mesh=high, cluster=high_cluster), high_before)


class SkinBrushSurface(RigTestCase):
    """The radius is a length over the surface, so a close surface the brush cannot walk to is left alone. With the
    volume falloff it is a distance in space, and that surface is reached."""

    def setUp(self):
        super().setUp()
        cmds.select(clear=True)
        joints = [cmds.joint(name="jointOne", position=(-1, 0, 0)), cmds.joint(name="jointTwo", position=(1, 0, 0))]
        low = cmds.polyPlane(name="low_geo", subdivisionsX=4, subdivisionsY=4, constructionHistory=False)[0]
        high = cmds.polyPlane(name="high_geo", subdivisionsX=4, subdivisionsY=4, constructionHistory=False)[0]
        cmds.move(0, 0.1, 0, high)
        cmds.makeIdentity(high, apply=True, translate=True)
        self.united = cmds.polyUnite(low, high, name="united_geo", constructionHistory=False)[0]   # vertices 0-24 low, 25-49 high
        self.cluster = cmds.skinCluster(joints, self.united, toSelectedBones=True)[0]
        cmds.setAttr(self.cluster + ".normalizeWeights", 0)
        for vertex in range(50):
            one = 1.0 if vertex % 5 <= 1 else 0.0
            cmds.skinPercent(self.cluster, "%s.vtx[%d]" % (self.united, vertex), normalize=False,
                             transformValue=[(joints[0], one), (joints[1], 1.0 - one)])
        cmds.setAttr(self.cluster + ".normalizeWeights", 1)

    def rows(self, first):
        return [cmds.skinPercent(self.cluster, "%s.vtx[%d]" % (self.united, v), query=True, value=True) for v in range(first, first + 25)]

    def test_the_brush_does_not_cross_to_another_shell_that_is_closer_in_space_than_in_length(self):
        high_before = self.rows(25)
        cmds.skinBrushStroke(radius=0.3, strength=1.0, hitPoint=[(self.united, 5, 0, 0, 0)])
        self.assertAlmostEqual(self.rows(0)[12][0], 0.2, places=5)       # the shell it landed on moved
        self.assertEqual(self.rows(25), high_before)                     # 0.1 above it in space, but no way there over the surface

    def test_the_volume_falloff_reaches_the_other_shell(self):
        # The vertex above the center is 0.1 away in a straight line: t = 1/3, scale (2/3)^2 * (3 - 4/3) = 20/27, and
        # its neighbors' average is 1/5 (the one in column 1 is joint one's), so it becomes 20/27 * 1/5 = 4/27.
        cmds.skinBrushStroke(radius=0.3, strength=1.0, falloff="volume", hitPoint=[(self.united, 5, 0, 0, 0)])
        self.assertAlmostEqual(self.rows(0)[12][0], 0.2, places=5)
        self.assertAlmostEqual(self.rows(25)[12][0], 4.0 / 27.0, places=5)

    def test_surface_is_the_default_falloff(self):
        high_before = self.rows(25)
        cmds.skinBrushStroke(radius=0.3, strength=1.0, falloff="surface", hitPoint=[(self.united, 5, 0, 0, 0)])
        self.assertEqual(self.rows(25), high_before)


class SkinBrushUndo(SkinBrushTestCase):

    def test_undo_restores_the_starting_weights_exactly_and_redo_repeats_the_stroke(self):
        before = self.rows()
        # Overlapping dabs: the weights from before the stroke are what undo must bring back, not those between dabs.
        self.stroke(self.center_hit(), self.center_hit(), (self.plane, 6, 0.1, 0.0, 0.0), radius=0.4)
        painted = self.rows()
        self.assertNotEqual(painted, before)
        cmds.undo()
        self.assertEqual(self.rows(), before)
        cmds.redo()
        self.assertEqual(self.rows(), painted)

    def test_a_stroke_that_changes_nothing_leaves_the_weights_alone(self):
        self.stroke(self.center_hit(), strength=1.0)
        settled = self.rows()
        cmds.setAttr(self.joints[0] + ".lockInfluenceWeights", 1)
        self.stroke(self.center_hit())
        self.assertEqual(self.rows(), settled)


class SkinBrushChecks(SkinBrushTestCase):

    def test_flags_are_checked(self):
        self.assertMayaError(lambda: self.stroke(self.center_hit(), radius=0), "radius")
        self.assertMayaError(lambda: self.stroke(self.center_hit(), strength=0), "strength")
        self.assertMayaError(lambda: self.stroke(self.center_hit(), strength=1.5), "strength")
        self.assertMayaError(lambda: self.stroke(self.center_hit(), operation="paint"), "paint", "operation")
        self.assertMayaError(lambda: self.stroke(self.center_hit(), falloff="smooth"), "smooth", "falloff")

    def test_a_stroke_needs_a_hit_or_a_ray(self):
        self.assertMayaError(lambda: cmds.skinBrushStroke(radius=0.3), "needs at least one")

    def test_a_mesh_without_a_skin_cluster_is_an_error(self):
        bare = cmds.polyCube(name="bare_geo", constructionHistory=False)[0]
        self.assertMayaError(lambda: self.stroke((bare, 0, 0, 0, 0)), "bare_geo", "no skinCluster")


@unittest.skipIf(cmds.about(batch=True), "Maya creates no contexts without a UI: run this file in a GUI Maya")
class SkinBrushContext(RigTestCase):

    def setUp(self):
        super().setUp()
        self.ctx = cmds.skinBrushCtx("skinBrushCtxTest")
        self.addCleanup(cmds.deleteUI, self.ctx, toolContext=True)

    def test_the_defaults(self):
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, operation=True), "smooth")
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, radius=True), 5.0)
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, strength=True), 0.5)
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, smoothness=True), 0.15)
        self.assertFalse(cmds.skinBrushCtx(self.ctx, query=True, throughMesh=True))
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, falloff=True), "surface")

    def test_the_settings_can_be_edited_and_queried(self):
        cmds.skinBrushCtx(self.ctx, edit=True, radius=8.0, strength=0.25, throughMesh=True, falloff="volume")
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, falloff=True), "volume")
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, radius=True), 8.0)
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, strength=True), 0.25)
        cmds.skinBrushCtx(self.ctx, edit=True, smoothness=0.4)
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, smoothness=True), 0.4)
        self.assertTrue(cmds.skinBrushCtx(self.ctx, query=True, throughMesh=True))

    def test_settings_that_are_not_valid_are_refused_and_keep_the_old_value(self):
        self.assertMayaError(lambda: cmds.skinBrushCtx(self.ctx, edit=True, radius=0), "radius")
        self.assertMayaError(lambda: cmds.skinBrushCtx(self.ctx, edit=True, strength=2), "strength")
        self.assertMayaError(lambda: cmds.skinBrushCtx(self.ctx, edit=True, smoothness=1.5), "smoothness")
        self.assertMayaError(lambda: cmds.skinBrushCtx(self.ctx, edit=True, operation="paint"), "paint")
        self.assertMayaError(lambda: cmds.skinBrushCtx(self.ctx, edit=True, falloff="smooth"), "smooth")
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, radius=True), 5.0)
        self.assertEqual(cmds.skinBrushCtx(self.ctx, query=True, strength=True), 0.5)


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
