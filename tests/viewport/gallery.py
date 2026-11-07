"""Viewport gallery: a manual VP2 check of every rigControlNode shape. Not part of the suite.

The suite runs in mayapy, which has no viewport, and batch renders (ogsRender) skip UI drawables, so the draw
override can only be checked in a GUI session. ViewportGallery.cmd starts maya.exe with this script. It builds
every shape beside a -1..1 reference cube (black) with a red line along +X, playblasts several views, writes
log.txt and quits.

Environment (set by ViewportGallery.cmd; exec gives this script no __file__):
    RIG_GALLERY_PLUGIN  the .mll to load (a copy, so the build output stays unlocked)
    RIG_GALLERY_OUT     the folder for the images and log.txt

What to look for:
    01-04  unit scale. Box, Sphere, Circle and Square fill the cube. Capsule is 3 long, Cylinder 1 long, both
           along X. Flat shapes face +X: edge-on vertical lines in the front and top views.
    05-07  shapeTransform scale (2, 1, 0.5). Box and curve shapes stretch on every axis. Round shapes keep a
           round cross-section (radius from scale Y, Sphere from scale X).
    08     filled primitives. Box at wireframeAlpha 0.5 is translucent. Sphere is selected (green) and Circle
           is the lead selection (light).
    09     shapeTransform scale X -1. Asymmetric curve shapes (ArchedDoubleArrow, Dome) flip to -X.
"""
import os
import traceback

from maya import cmds
import maya.utils

PLUGIN = os.environ["RIG_GALLERY_PLUGIN"]
OUT = os.environ["RIG_GALLERY_OUT"]
LOG = os.path.join(OUT, "log.txt")

# Same order as the shapeType enum.
SHAPES = ["Box", "Sphere", "Circle", "Capsule", "Triangle", "Cylinder", "Square",
          "HalfCircleArrow", "ArchedDoubleArrow", "PointedCircle", "Diamond", "CircleArrow", "Dome",
          "SingleArrow", "FourArrow", "CircleStick", "Pivot"]

CUBE_POINTS = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1),
               (-1, 1, 1), (-1, -1, 1), (1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1), (-1, 1, -1)]


def log(text):
    with open(LOG, "a") as handle:
        handle.write(str(text) + "\n")


def reference(name, position):
    """A -1..1 cube and a +X line, so sizes and axes can be read off the image."""
    cube = cmds.curve(degree=1, point=CUBE_POINTS, name=name)
    axis = cmds.curve(degree=1, point=[(0, 0, 0), (1.6, 0, 0)], name=name + "_x")
    for node, color in ((cube, 2), (axis, 13)):   # dark grey, red
        cmds.setAttr(node + ".overrideEnabled", 1)
        cmds.setAttr(node + ".overrideColor", color)
        cmds.xform(node, translation=position)


def build_gallery(prefix, shape_transform=None, filled=False):
    """Every shape in a 6-wide grid, 4 units apart. Returns the controls in SHAPES order."""
    controls = []
    for index, shape in enumerate(SHAPES):
        position = ((index % 6) * 4.0, -(index // 6) * 4.0, 0.0)
        control = cmds.createNode("rigControlNode", name="%s_%s" % (prefix, shape))
        cmds.setAttr(control + ".shapeType", index)
        cmds.setAttr(control + ".filled", filled)
        cmds.setAttr(control + ".wireframeColor", 0.1, 0.6, 1.0, type="double3")
        if shape_transform:
            cmds.setAttr(control + ".shapeTransform", shape_transform, type="matrix")
        cmds.xform(control, translation=position)
        reference("ref_%s_%s" % (prefix, shape), position)
        controls.append(control)
    return controls


def snap(camera, name, frame=True):
    # playblast captures its own active editor, which need not be the first model panel. Set the camera there.
    editor = cmds.playblast(activeEditor=True)
    cmds.modelEditor(editor, edit=True, camera=camera, displayAppearance="wireframe", grid=False,
                     headsUpDisplay=False, selectionHiliteDisplay=True, allObjects=True)
    if frame:
        cmds.viewFit(camera, all=True)
    cmds.refresh(force=True)
    cmds.playblast(completeFilename=os.path.join(OUT, name + ".png"), format="image", compression="png", frame=[1],
                   viewer=False, showOrnaments=False, widthHeight=(1800, 1000), percent=100, offScreen=True,
                   forceOverwrite=True)
    log("wrote " + name)


def run():
    try:
        cmds.loadPlugin(PLUGIN)
        log("renderer: %s" % cmds.modelEditor(cmds.getPanel(type="modelPanel")[0], query=True, rendererName=True))

        cmds.file(new=True, force=True)
        build_gallery("unit")
        snap("front", "01_front_unit")
        snap("side", "02_side_unit")
        snap("persp", "03_persp_unit")
        snap("top", "04_top_unit")

        cmds.file(new=True, force=True)
        build_gallery("scaled", shape_transform=[2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 1])
        snap("front", "05_front_scale_2_1_05")
        snap("side", "06_side_scale_2_1_05")
        snap("top", "07_top_scale_2_1_05")

        cmds.file(new=True, force=True)
        controls = build_gallery("filled", filled=True)
        cmds.setAttr(controls[SHAPES.index("Box")] + ".wireframeAlpha", 0.5)
        cmds.viewFit("persp", all=True)
        cmds.select(controls[SHAPES.index("Sphere")], controls[SHAPES.index("Circle")])
        snap("persp", "08_persp_filled_selected", frame=False)

        cmds.file(new=True, force=True)
        build_gallery("mirrored", shape_transform=[-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
        snap("persp", "09_persp_mirrored")
        log("done")
    except Exception:
        log(traceback.format_exc())
    finally:
        cmds.quit(force=True)


# Deferred, so the main window and its model panels exist before anything runs.
maya.utils.executeDeferred(run)
