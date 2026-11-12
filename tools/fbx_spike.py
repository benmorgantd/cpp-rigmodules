import os
import sys
import traceback

os.environ["MAYA_DISABLE_CER"] = "1"
os.environ["MAYA_SKIP_USERSETUP_PY"] = "1"
import maya.standalone

maya.standalone.initialize(name="python")
import maya.api.OpenMaya as om
import maya.cmds as cmds
import maya.mel as mel

OUT = r"C:\Users\bmorgan\AppData\Local\Temp\claude\e319d1503bc55f9b\574d0ae8-8ce4-4df8-a49b-812b8b18bbf1\scratchpad\spike"
os.makedirs(OUT, exist_ok=True)
fbx = os.path.join(OUT, "walk.fbx").replace("\\", "/")


def log(*a):
    print("SPIKE:", *a)
    sys.stdout.flush()


def world(j, t):
    return om.MMatrix(cmds.getAttr(j + ".worldMatrix[0]", time=t))


try:
    log("maya", cmds.about(version=True))
    log("loadPlugin", cmds.loadPlugin("fbxmaya", quiet=True))
    log("FBXExport mel exists", mel.eval('exists "FBXExport"'))
    log("fbx plugin version", mel.eval("FBXPluginVersion") if mel.eval('exists "FBXPluginVersion"') else "n/a")

    cmds.file(new=True, force=True)
    cmds.currentUnit(time="ntsc")
    cmds.playbackOptions(min=1, max=10)
    cmds.select(clear=True)
    # plain scratch skeleton: root > spine > head, with translate, rotate and scale keys
    root = cmds.joint(name="root", position=(0, 0, 0))
    spine = cmds.joint(name="spine", position=(0, 10, 0))
    head = cmds.joint(name="head", position=(0, 10, 5))
    joints = [root, spine, head]
    for f in range(1, 11):
        cmds.setKeyframe(root, attribute="translateX", time=f, value=f * 2.0)
        cmds.setKeyframe(spine, attribute="rotateZ", time=f, value=f * 17.0)  # passes 90, to 153
        cmds.setKeyframe(spine, attribute="rotateX", time=f, value=f * 5.0)
        cmds.setKeyframe(head, attribute="rotateY", time=f, value=-f * 12.0)
        cmds.setKeyframe(head, attribute="scaleX", time=f, value=1.0 + f * 0.05)
    expected = {f: [world(j, f) for j in joints] for f in range(1, 11)}

    mel.eval("FBXResetExport")
    mel.eval("FBXExportBakeComplexAnimation -v true")
    mel.eval("FBXExportBakeComplexStart -v 1")
    mel.eval("FBXExportBakeComplexEnd -v 10")
    mel.eval("FBXExportBakeComplexStep -v 1")
    mel.eval("FBXExportUpAxis y")
    mel.eval("FBXExportInAscii -v false")
    cmds.select(root, hierarchy=True)
    mel.eval('FBXExport -f "%s" -s' % fbx)
    log("exported", os.path.getsize(fbx), "bytes")

    cmds.file(new=True, force=True)
    mel.eval("FBXResetImport")
    mel.eval("FBXImportMode -v add")
    mel.eval('FBXImport -f "%s"' % fbx)
    imported = cmds.ls(type="joint", long=True)
    log("imported joints", imported)
    log("time unit", cmds.currentUnit(query=True, time=True))
    log("anim range", cmds.playbackOptions(q=True, min=True), cmds.playbackOptions(q=True, max=True))
    log("up axis", cmds.upAxis(q=True, axis=True))
    by_leaf = {j.split("|")[-1]: j for j in imported}
    worst = 0.0
    for f in range(1, 11):
        for name, exp in zip(("root", "spine", "head"), expected[f]):
            got = world(by_leaf[name], f)
            worst = max(worst, max(abs(a - b) for a, b in zip(got, exp)))
    log("max matrix error over 10 frames, 3 joints:", worst)
    log("scale round trip", cmds.getAttr(by_leaf["head"] + ".scaleX", time=10))

    # namespace import and custom attribute survival
    cmds.file(new=True, force=True)
    cmds.select(clear=True)
    j = cmds.joint(name="marked")
    cmds.addAttr(j, longName="rigMarkup", dataType="string")
    cmds.setAttr(j + ".rigMarkup", '{"side":"Left"}', type="string")
    cmds.select(j)
    mel.eval("FBXResetExport")
    mel.eval("FBXExportInputConnections -v false")
    mel.eval("FBXExportAnimationOnly -v false")
    f2 = os.path.join(OUT, "attr.fbx").replace("\\", "/")
    mel.eval('FBXExport -f "%s" -s' % f2)
    cmds.file(new=True, force=True)
    mel.eval("FBXResetImport")
    mel.eval('FBXImport -f "%s"' % f2)
    jj = cmds.ls(type="joint")[0]
    log("custom string attr survives:", cmds.attributeQuery("rigMarkup", node=jj, exists=True))

    # does the exporter honor offsetParentMatrix?
    cmds.file(new=True, force=True)
    cmds.select(clear=True)
    a = cmds.joint(name="opmjoint")
    cmds.setAttr(a + ".offsetParentMatrix", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 7, 9, 1, type="matrix")
    log("opm world translate before export", cmds.xform(a, q=True, ws=True, t=True))
    cmds.select(a)
    f3 = os.path.join(OUT, "opm.fbx").replace("\\", "/")
    mel.eval("FBXResetExport")
    mel.eval('FBXExport -f "%s" -s' % f3)
    cmds.file(new=True, force=True)
    mel.eval("FBXResetImport")
    mel.eval('FBXImport -f "%s"' % f3)
    jj = cmds.ls(type="joint")[0]
    log("opm world translate after import", cmds.xform(jj, q=True, ws=True, t=True), "(5,7,9 = honored)")
except Exception:
    log("FAILED", traceback.format_exc())
finally:
    maya.standalone.uninitialize()
    os._exit(0)
