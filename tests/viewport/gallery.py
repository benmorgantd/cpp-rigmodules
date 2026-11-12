"""Viewport gallery: a manual VP2 check of every rigControlNode shape. Not part of the suite.

The suite runs in mayapy, which has no viewport, and batch renders (ogsRender) skip UI drawables, so the draw
override can only be checked in a GUI session. ViewportGallery.cmd starts maya.exe with this script. It builds
every shape beside a -1..1 reference cube (black) with a red line along +X, playblasts several views, writes
log.txt and quits.

Environment (set by ViewportGallery.cmd; exec gives this script no __file__):
    RIG_GALLERY_PLUGIN  the .mll to load (a copy, so the build output stays unlocked)
    RIG_GALLERY_OUT     the folder for the images and log.txt

What to look for:
    01-04  unit scale. Every shape fits the cube. Capsule fills the cube like Box. Ring shapes
           (Circle, Square, Triangle, PointedCircle, the circle arrows) face +X: edge-on vertical lines in the
           front and top views. Pointer shapes (SingleArrow, FourArrow, CircleStick, ArchedDoubleArrow) point
           down +X along the red line and lie flat in XZ (edge-on in the front view).
    05-07  shapeTransform scale (2, 1, 0.5). Every shape stretches on every axis, round ones included.
    08     every shape filled: the fill at 0.75 of the wire's alpha, the wire on top. Box at wireframeAlpha 0.5
           is fainter. Sphere is selected (green) and Circle is the lead selection (light).
    09     shapeTransform scale X -1. Pointers point down -X, and Dome bulges toward -X.
    10     the filled scene from behind (-X, -Z). Flat fills must still show, so nothing is back-face culled.
    11-13  mirrorRigControlShapes on an arm built with "mirror": true from behavior-mirrored joints (D28). The Right
           arm's shapes (red, -X) are the Left arm's (green, +X) reflected across X = 0: the arrows on both
           clavicles point away from the chest, both upper arm arrows sit above the bone, both forearm domes bulge
           toward the hand, and both hand circles sit forward (+Z). Fills show on both sides.
"""
import json
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


def build_mirrored_arm():
    """A chest and a left arm, X down the bone, behavior-mirrored to the right like mayadev's joint tool, built with
    "mirror": true. The left controls get offset, filled shapes, then mirrorRigControlShapes copies them right."""
    cmds.select(clear=True)
    chest = cmds.joint(name="chest_jnt", position=(0, 5, 0))
    cmds.select(clear=True)
    clavicle = cmds.joint(name="clavicle_l_jnt", position=(0.5, 5.5, 0.5))
    cmds.joint(name="upperArm_l_jnt", position=(2, 5.5, 0))
    cmds.joint(name="lowerArm_l_jnt", position=(4.5, 4, 0.5))
    cmds.joint(name="hand_l_jnt", position=(6.5, 3, 1.5))
    cmds.joint(clavicle, edit=True, orientJoint="xyz", secondaryAxisOrient="yup", children=True, zeroScaleOrient=True)
    cmds.mirrorJoint(clavicle, mirrorYZ=True, mirrorBehavior=True, searchReplace=("_l_", "_r_"))

    cmds.rigJointMarkup(chest, usage="chest", side="Center", export=True, skin=True)
    joints = [chest]
    for suffix, side in (("l", "Left"), ("r", "Right")):
        for usage in ("clavicle", "upperArm", "lowerArm", "hand"):
            joint = "%s_%s_jnt" % (usage, suffix)
            cmds.rigJointMarkup(joint, usage=usage, side=side, export=True, skin=True)
            joints.append(joint)

    template = {
        "rigVersion": 1,
        "rigTemplateName": "MirrorGallery", "rigAuthor": "gallery",
        "rigModules": [
            {"moduleType": "FkChain", "name": "spine", "moduleArgs": {"joints": [["Center", "chest"]]}},
            {"moduleType": "FkChain", "name": "arm_l", "side": "Left", "mirror": True,
             "moduleArgs": {"joints": [["Left", "clavicle"], ["Left", "upperArm"], ["Left", "lowerArm"], ["Left", "hand"]]},
             "parentStructure": {"start": "spine.end"}},
        ],
    }
    path = os.path.join(OUT, "mirror_template.json").replace("\\", "/")
    with open(path, "w") as handle:
        json.dump(template, handle)
    cmds.createRigFromTemplate(*joints, template=path, asset="gallery\\rig")

    # shapeType index, shapeTransform, filled. Offsets and pointers make a wrong reflection obvious.
    left_shapes = {
        "clavicle": (SHAPES.index("SingleArrow"), [0.6, 0, 0, 0, 0, 0.6, 0, 0, 0, 0, 0.6, 0, 0.8, 0, 0, 1], True),
        "upperArm": (SHAPES.index("SingleArrow"), [0.5, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.5, 0, 0, 0.8, 0, 1], True),
        "lowerArm": (SHAPES.index("Dome"), [0.6, 0, 0, 0, 0, 0.6, 0, 0, 0, 0, 0.6, 0, 0.6, 0, 0, 1], True),
        "hand": (SHAPES.index("Circle"), [0.8, 0, 0, 0, 0, 0.8, 0, 0, 0, 0, 0.8, 0, 0, 0, 0.4, 1], False),
    }
    for usage, (shape_type, shape_transform, filled) in left_shapes.items():
        control = "rig:%s_l_ctrl_l" % usage
        cmds.setAttr(control + ".shapeType", shape_type)
        cmds.setAttr(control + ".shapeTransform", shape_transform, type="matrix")
        cmds.setAttr(control + ".filled", filled)
    changed = cmds.mirrorRigControlShapes(cmds.ls("rig:*", type="rigRootModule")[0])
    log("mirrored: %s" % ", ".join(changed))


def snap(camera, name, frame=True, fit_factor=1.0):
    # playblast captures its own active editor, which need not be the first model panel. Set the camera there.
    editor = cmds.playblast(activeEditor=True)
    cmds.modelEditor(editor, edit=True, camera=camera, displayAppearance="wireframe", grid=False,
                     headsUpDisplay=False, selectionHiliteDisplay=True, allObjects=True)
    if frame:
        cmds.viewFit(camera, all=True, fitFactor=fit_factor)
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
        back = cmds.camera(name="backCam")[0]
        cmds.xform(back, translation=(-20, 14, -24))
        cmds.viewLookAt(back, position=(10, -4, 0))
        snap(back, "10_back_filled")

        cmds.file(new=True, force=True)
        build_gallery("mirrored", shape_transform=[-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
        snap("persp", "09_persp_mirrored")

        cmds.file(new=True, force=True)
        build_mirrored_arm()
        # The playblast is wider than the viewport, so a full fit crops the top and bottom. Leave a margin.
        snap("front", "11_front_mirrored_arm", fit_factor=0.5)
        snap("top", "12_top_mirrored_arm", fit_factor=0.8)
        snap("persp", "13_persp_mirrored_arm", fit_factor=0.8)
        log("done")
    except Exception:
        log(traceback.format_exc())
    finally:
        cmds.quit(force=True)


# Deferred, so the main window and its model panels exist before anything runs.
maya.utils.executeDeferred(run)
