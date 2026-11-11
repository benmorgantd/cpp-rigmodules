"""Builds the performance scenes: many instances of one rig, animated, for playback-speed tests.

Run it with mayapy (set nothing else up: it loads a copy of the plugin and the python package itself):

    mayapy tools/perf/perf_scenes.py human   [--count 50] [--frames 96]
    mayapy tools/perf/perf_scenes.py complex [--count 50]
    mayapy tools/perf/perf_scenes.py bench   SCENE.mb [--passes 3]        (evaluation only, no viewport)
    mayapy tools/perf/perf_scenes.py gui     SCENE.mb [--seconds 6]       (real playback in a Maya window)
    mayapy tools/perf/perf_scenes.py profile SCENE.mb [--frames 20]       (where playback time goes, in a Maya window)
    mayapy tools/perf/perf_scenes.py check                                (the generated rig is valid; seconds)

human    imports the asset's skeleton `--count` times, builds the asset's template on each, and keys every
         control on every frame through a cycle of load, mirror, flip and zero poses.
complex  generates a 200 joint skeleton and a template with 51 constraints (Parent, Orient and Point) and Aim modules, builds
         it `--count` times, and keys a -90 to 90 range of motion on every control's rotate X, Y and Z.

Timing, on this machine: the scenes take about a minute each to build. `bench` takes about 3 minutes on the
complex scene with parallel only, and about 10 minutes with --all-modes (serial and DG are slow there). `gui`
runs five configurations of 8 seconds and takes about a minute plus the scene load. None of this is in the
suite (RigTests.cmd): run it by hand.

Scenes, the generated skeleton and the generated template go to tools/perf/out (ignored by git).
"""
import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

PERF_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(os.path.dirname(PERF_DIR))
DEFAULT_OUT = os.path.join(PERF_DIR, "out")
DEFAULT_PLUGIN = os.path.join(REPO_DIR, "cpp", "x64", "Release", "RigModules.mll")
DEFAULT_ASSET_DIR = r"C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped"
DEFAULT_MAYA = r"C:\Program Files\Autodesk\Maya2026\bin\maya.exe"

HUMAN_ASSET_ID = "test|character|biped"
COMPLEX_ASSET_ID = "test|character|complex"

# The poses a Human instance cycles through, one step per frame, offset by instance so no two instances
# in a frame are doing the same thing.
POSE_CYCLE = ["load", "mirrorLeftToRight", "flip", "zero", "load", "mirrorRightToLeft", "flip", "zero"]

ROM_DEGREES = 90.0
ROM_SEGMENT_FRAMES = 40
ROM_AXES = ("rotateX", "rotateY", "rotateZ")


# ---- the complex skeleton and template --------------------------------------------------------------
#
# One entry per chain, written for the Left side. A Left chain is built on both sides (Right is the
# reflection across X = 0) and gets "mirror": true in the template, so the template stays half the size.
# Aim sets (aim, target and up joints) are listed apart. 50 Center joints + 2 x 75 side joints = 200 joints.
# 7 Center + 2 x 22 side constraints = 51.
#
# Every module type and constraint type the plugin has appears here many times. A new module or constraint
# type adds a block of instances to this rig in the same change (CLAUDE.md, "Gate for new modules").

def _chain(base, side, count, at, offset, step, parent, constraint):
    """at: (base, joint index) the chain's first joint is placed relative to, None for the root chain.
    parent: (module base, socket) the chain's start attaches to, None for layout.end.
    constraint: (type, [(module base, socket, label), ...]) on the chain's start, None for none."""
    return {"base": base, "side": side, "count": count, "at": at, "offset": offset, "step": step,
            "parent": parent, "constraint": constraint}


def chain_specs():
    layout = ("layout", "end", "World")
    specs = [
        _chain("spine", "Center", 6, None, (0, 9, 0), (0, 0.7, 0), None, None),
        _chain("neck", "Center", 4, ("spine", 5), (0, 0.5, 0), (0, 0.45, 0), ("spine", "end"),
               ("Orient", [("spine", "end", "Chest"), layout])),
        _chain("jaw", "Center", 2, ("neck", 3), (0, 0, 0.1), (0, -0.1, 0.4), ("neck", "end"),
               ("Point", [("neck", "end", "Head"), layout])),
        _chain("tail", "Center", 14, ("spine", 0), (0, -0.2, -0.3), (0, -0.05, -0.45), ("spine", "start"),
               ("Orient", [("spine", "start", "Hips"), layout])),
    ]
    for k in range(4):
        specs.append(_chain("cape%d" % (k + 1), "Center", 6, ("spine", 4), (-0.45 + 0.3 * k, 0, -0.4),
                            (0, -0.7, -0.05), ("spine", "index_4"),
                            ("Parent", [("spine", "index_4", "Chest"), ("spine", "start", "Hips"), layout])))
    specs += [
        _chain("clavicle", "Left", 1, ("spine", 4), (0.4, 0, 0), (0.4, 0, 0), ("spine", "index_4"),
               ("Orient", [("spine", "index_4", "Chest"), layout])),
        _chain("arm", "Left", 3, ("clavicle", 0), (0.6, 0, 0), (1.3, -0.1, 0), ("clavicle", "end"),
               ("Parent", [("clavicle", "end", "Clavicle"), ("spine", "end", "Chest"), layout])),
        _chain("leg", "Left", 4, ("spine", 0), (0.4, -0.3, 0), (0.05, -1.0, 0), ("spine", "start"),
               ("Orient", [("spine", "start", "Hips"), layout])),
        _chain("pad", "Left", 3, ("clavicle", 0), (0.1, 0.2, 0), (0, 0.3, 0.05), ("clavicle", "end"),
               ("Point", [("clavicle", "end", "Clavicle"), ("spine", "end", "Chest")])),
    ]
    for k, finger in enumerate(["thumb", "index", "middle", "ring", "pinky"]):
        specs.append(_chain(finger, "Left", 4, ("arm", 2), (0.3, 0, 0.24 - 0.12 * k),
                            (0.3, -0.02, (k - 2) * 0.05), ("arm", "end"),
                            ("Parent", [("arm", "end", "Hand"), layout])))
        specs.append(_chain("toe%d" % (k + 1), "Left", 3, ("leg", 3), ((k - 2) * 0.12, -0.05, 0.2),
                            (0, -0.02, 0.25), ("leg", "end"),
                            ("Point", [("leg", "end", "Foot"), layout])))
    for k in range(5):
        specs.append(_chain("feather%d" % (k + 1), "Left", 4, ("arm", 1), (0.15 * k - 0.5, 0.1, -0.15),
                            (0.1, 0.05, -0.5), ("arm", "index_1"),
                            ("Orient" if k % 2 == 0 else "Parent",
                             [("arm", "index_1", "Arm"), ("spine", "end", "Chest")])))
    return specs


def aim_specs():
    """Aim modules, written for the Left side. Offsets are from the parent joint. The aim joint has no
    control, so each set has two controls (target and up) and a constraint on the target."""
    return [
        {"base": base, "parent": ("neck", 3), "aim": aim, "target": target, "up": up}
        for base, aim, target, up in [
            ("eye", (0.15, 0.1, 0.25), (0.15, 0.1, 2.5), (0.15, 1.1, 0.25)),
            ("brow", (0.3, 0.3, 0.2), (0.3, 0.3, 2.2), (0.3, 1.3, 0.2)),
            ("ear", (0.4, 0.0, -0.1), (2.4, 0.0, -0.1), (0.4, 1.0, -0.1)),
        ]
    ]


def side_suffix(side):
    return {"Center": "c", "Left": "l", "Right": "r"}[side]


def module_name(base, side):
    return base if side == "Center" else "%s_%s" % (base, side_suffix(side))


def joint_name(base, index, side):
    return "%s%d_%s" % (base, index + 1, side_suffix(side))


def aim_joint(base, role, side):
    """Name of an aim set's joint, role being aim, target or up. Without its side suffix it is the usage."""
    return "%s%s_%s" % (base, role.capitalize(), side_suffix(side))


def usage_name(base, index):
    return "%s%d" % (base, index + 1)


def _sides(spec):
    return ["Left", "Right"] if spec["side"] == "Left" else ["Center"]


def _flip(vector, side):
    return (-vector[0], vector[1], vector[2]) if side == "Right" else tuple(vector)


def joint_layout():
    """Every joint of the skeleton: a list of dicts with name, parent, position, markup, chain (the module's
    name) and the chain's orientation hint. Parents come before children."""
    specs = chain_specs()
    spec_side = {spec["base"]: spec["side"] for spec in specs}
    positions = {}   # (base, side) -> list of positions
    names = {}       # (base, side) -> list of joint names
    joints = []
    for spec in specs:
        for side in _sides(spec):
            if spec["at"] is None:
                parent_joint, origin = None, _flip(spec["offset"], side)
            else:
                at_base, at_index = spec["at"]
                at_side = side if spec_side[at_base] == "Left" else "Center"
                parent_joint = names[(at_base, at_side)][at_index]
                anchor = positions[(at_base, at_side)][at_index]
                offset = _flip(spec["offset"], side)
                origin = (anchor[0] + offset[0], anchor[1] + offset[1], anchor[2] + offset[2])
            step = _flip(spec["step"], side)
            length = math.sqrt(sum(c * c for c in step))
            secondary = "zup" if abs(step[1]) / length > 0.7 else "yup"
            chain_positions = []
            chain_names = []
            previous = parent_joint
            for i in range(spec["count"]):
                position = (origin[0] + step[0] * i, origin[1] + step[1] * i, origin[2] + step[2] * i)
                name = joint_name(spec["base"], i, side)
                joints.append({"name": name, "parent": previous, "position": position, "side": side,
                               "usage": usage_name(spec["base"], i), "chain": module_name(spec["base"], side), "aim_next": i < spec["count"] - 1,
                               "secondary": secondary})
                chain_positions.append(position)
                chain_names.append(name)
                previous = name
            positions[(spec["base"], side)] = chain_positions
            names[(spec["base"], side)] = chain_names
    for aim in aim_specs():
        for side in ("Left", "Right"):
            parent = names[(aim["parent"][0], "Center")][aim["parent"][1]]
            anchor = positions[(aim["parent"][0], "Center")][aim["parent"][1]]
            for role in ("aim", "target", "up"):
                offset = _flip(aim[role], side)
                name = aim_joint(aim["base"], role, side)
                joints.append({"name": name, "parent": parent, "side": side, "usage": name[:-2],
                               "chain": module_name(aim["base"], side), "aim_next": False, "secondary": "yup",
                               "position": (anchor[0] + offset[0], anchor[1] + offset[1], anchor[2] + offset[2])})
    return joints


def build_template():
    """The template as a dict. Left chains carry "mirror": true."""
    modules = []
    constraints = []
    specs = chain_specs()
    side_of = {spec["base"]: spec["side"] for spec in specs}
    for spec in specs:
        side = spec["side"]
        name = module_name(spec["base"], side)
        module = {
            "moduleType": "FkChain",
            "name": name,
            "side": side,
            "moduleArgs": {"joints": [[side, usage_name(spec["base"], i)] for i in range(spec["count"])]},
        }
        if side == "Left":
            module["mirror"] = True
        if spec["parent"] is not None:
            module["parentStructure"] = {"start": "%s.%s" % (module_name(spec["parent"][0], side_of[spec["parent"][0]]), spec["parent"][1])}
        modules.append(module)
        if spec["constraint"] is not None:
            constraint_type, drivers = spec["constraint"]
            entry = {
                "name": "%s_space%s" % (spec["base"], "_l" if side == "Left" else ""),
                "type": constraint_type,
                "target": "%s.start" % name,
                "drivers": [],
            }
            for index, (driver_base, socket, label) in enumerate(drivers):
                if driver_base == "layout":
                    socket_id = "layout.end"
                else:
                    socket_id = "%s.%s" % (module_name(driver_base, side_of[driver_base]), socket)
                driver = {"socket": socket_id, "label": label}
                if index == 0:
                    driver["weight"] = 1.0
                entry["drivers"].append(driver)
            if side == "Left":
                entry["mirror"] = True
            constraints.append(entry)
    for aim in aim_specs():
        name = module_name(aim["base"], "Left")
        modules.append({
            "moduleType": "Aim",
            "name": name,
            "side": "Left",
            "mirror": True,
            "moduleArgs": {role: ["Left", aim_joint(aim["base"], role, "Left")[:-2]] for role in ("aim", "target", "up")},
            "parentStructure": {role: "%s.end" % aim["parent"][0] for role in ("aim", "target", "up")},
        })
        constraints.append({
            "name": "%s_space_l" % aim["base"],
            "type": "Parent",
            "target": "%s.target" % name,
            "drivers": [{"socket": "%s.end" % aim["parent"][0], "label": "Head", "weight": 1.0},
                        {"socket": "layout.end", "label": "World"}],
            "mirror": True,
        })
    return {
        "formatVersion": 1,
        "rigName": "Complex200",
        "rigVersion": 1,
        "rigTemplateName": "Complex200",
        "rigAuthor": "perf_scenes.py",
        "rigModules": modules,
        "constraints": constraints,
    }


def built_constraint_count(template):
    return sum(2 if entry.get("mirror") else 1 for entry in template["constraints"])


# ---- Maya side ---------------------------------------------------------------------------------------

def start_maya(plugin):
    """Starts standalone Maya and loads a copy of the plugin (a loaded .mll locks the build output)."""
    os.environ["MAYA_DISABLE_CER"] = "1"
    os.environ["MAYA_SKIP_USERSETUP_PY"] = "1"
    import maya.standalone
    maya.standalone.initialize(name="python")
    from maya import cmds
    copy_dir = tempfile.mkdtemp(prefix="perf_plugin_")
    copy = os.path.join(copy_dir, os.path.basename(plugin))
    shutil.copyfile(plugin, copy)
    cmds.loadPlugin(copy)
    python_dir = os.path.join(REPO_DIR, "python")
    if python_dir not in sys.path:
        sys.path.insert(0, python_dir)
    cmds.undoInfo(state=False)


def finish():
    import maya.standalone
    maya.standalone.uninitialize()
    os._exit(0)


def build_skeleton(joints):
    """Creates the skeleton in the current scene, orients it so +X runs down each chain, and marks every
    joint. The joints are created in a fresh scene, at the origin of the world."""
    from maya import cmds
    for joint in joints:
        if joint["parent"] is None:
            cmds.select(clear=True)
        else:
            cmds.select(joint["parent"])
        cmds.joint(name=joint["name"], position=joint["position"], absolute=True, radius=0.2)
    for joint in joints:
        # Parents are oriented before children, and orienting a joint keeps its children in place.
        if joint["aim_next"]:
            cmds.joint(joint["name"], edit=True, orientJoint="xyz", secondaryAxisOrient=joint["secondary"],
                       zeroScaleOrient=True)
        else:
            cmds.joint(joint["name"], edit=True, orientJoint="none", zeroScaleOrient=True)
    for joint in joints:
        cmds.rigJointMarkup(joint["name"], usage=joint["usage"], side=joint["side"], export=True, skin=True)


def import_skeleton(skeleton_file, namespace):
    from maya import cmds
    cmds.file(skeleton_file, i=True, namespace=namespace, ignoreVersion=True)
    return cmds.ls(namespace + ":*", type="joint")


class RigInstance(object):
    def __init__(self, namespace, rig_root, controls):
        self.namespace = namespace
        self.rig_root = rig_root
        self.controls = controls   # every control except the layout's, which place the instance


def build_instances(skeleton_file, template_file, asset_id, prefix, count, spacing):
    """Imports the skeleton and builds the rig, `count` times, on a square grid `spacing` apart."""
    from maya import cmds
    import rigmodules.query
    columns = int(math.ceil(math.sqrt(count)))
    instances = []
    started = time.time()
    for index in range(count):
        namespace = "%s%03d" % (prefix, index + 1)
        joints = import_skeleton(skeleton_file, namespace)
        cmds.createRigFromTemplate(*joints, template=template_file, asset=asset_id, namespace=namespace)
        rig_root = rigmodules.query.get_rigs(namespace)[0].rig_root
        controls = rigmodules.query.get_rig_controls(rig_root)
        layout_control = [c.node for c in controls if c.module_type == "Layout" and c.usage == "layout_0"][0]
        cmds.setAttr(layout_control + ".translateX", (index % columns) * spacing)
        cmds.setAttr(layout_control + ".translateZ", (index // columns) * spacing)
        instances.append(RigInstance(namespace, rig_root, [c.node for c in controls if c.module_type != "Layout"]))
        if (index + 1) % 10 == 0:
            print("built %d/%d rigs (%.0f s)" % (index + 1, count, time.time() - started))
    return instances


def save_scene(path, frame_min, frame_max):
    from maya import cmds
    cmds.playbackOptions(minTime=frame_min, maxTime=frame_max, animationStartTime=frame_min,
                         animationEndTime=frame_max)
    cmds.currentTime(frame_min)
    cmds.file(rename=path)
    cmds.file(save=True, type="mayaBinary" if path.endswith(".mb") else "mayaAscii")
    print("saved %s (%.1f MB)" % (path, os.path.getsize(path) / 1048576.0))


# ---- the Human scene ---------------------------------------------------------------------------------

def apply_pose_step(step, controls, pose_file):
    from maya import cmds
    if step == "load":
        cmds.loadRigPose(*controls, file=pose_file)
    elif step == "mirrorLeftToRight":
        cmds.mirrorRigPose(*controls, leftToRight=True)
    elif step == "mirrorRightToLeft":
        cmds.mirrorRigPose(*controls, leftToRight=False)
    elif step == "flip":
        cmds.flipRigPose(*controls)
    elif step == "zero":
        cmds.zeroRigControls(*controls)
    else:
        raise ValueError(step)


def key_pose_series(instances, frames, pose_file):
    """One pose step and a full key (every channel of every control) per instance per frame."""
    from maya import cmds
    import rigmodules.pose
    started = time.time()
    keys = 0
    for frame in range(1, frames + 1):
        cmds.currentTime(frame)
        for index, instance in enumerate(instances):
            apply_pose_step(POSE_CYCLE[(frame + index) % len(POSE_CYCLE)], instance.controls, pose_file)
            keys += rigmodules.pose.key_rig(instance.rig_root, time=frame)
        if frame % 8 == 0 or frame == frames:
            print("keyed frame %d/%d (%.0f s)" % (frame, frames, time.time() - started))
    return keys


def make_human(args):
    from maya import cmds
    out = args.out
    os.makedirs(out, exist_ok=True)
    skeleton = os.path.join(args.asset_dir, "skeleton", "biped_skeleton.ma")
    template = os.path.join(args.asset_dir, "variants", "Human", "template.json")
    pose_file = os.path.join(args.asset_dir, "variants", "Human", "poses", "star_pose1.json")
    start_maya(args.plugin)
    instances = build_instances(skeleton.replace("\\", "/"), template.replace("\\", "/"), HUMAN_ASSET_ID, "biped",
                                args.count, 5.0)
    keys = key_pose_series(instances, args.frames, pose_file.replace("\\", "/"))
    print("%d instances, %d keys" % (len(instances), keys))
    save_scene(os.path.join(out, "human_x%d.mb" % args.count), 1, args.frames)
    finish()


# ---- the complex scene -------------------------------------------------------------------------------

def key_rom(instances, segment=ROM_SEGMENT_FRAMES, degrees=ROM_DEGREES):
    """Rotate X, then Y, then Z, on every control at once: 0 to -degrees to +degrees to 0 over `segment` frames
    each. Returns the last frame."""
    from maya import cmds
    for axis_index, axis in enumerate(ROM_AXES):
        start = 1 + axis_index * segment
        keys = [(start, 0.0), (start + segment * 0.25, -degrees), (start + segment * 0.75, degrees),
                (start + segment, 0.0)]
        for instance in instances:
            plugs = ["%s.%s" % (control, axis) for control in instance.controls]
            for frame, value in keys:
                cmds.setKeyframe(plugs, time=frame, value=value)
    return 1 + len(ROM_AXES) * segment


def make_complex(args):
    from maya import cmds
    out = args.out
    os.makedirs(out, exist_ok=True)
    template = build_template()
    constraints = built_constraint_count(template)
    joints = joint_layout()
    assert len(joints) == 200, len(joints)
    assert constraints >= 48, constraints
    template_file = os.path.join(out, "complex_template.json").replace("\\", "/")
    with open(template_file, "w", encoding="utf-8") as handle:
        json.dump(template, handle, indent=2)
    start_maya(args.plugin)
    build_skeleton(joints)
    skeleton_file = os.path.join(out, "complex_skeleton.ma").replace("\\", "/")
    cmds.file(rename=skeleton_file)
    cmds.file(save=True, type="mayaAscii")
    print("skeleton: %d joints, template: %d modules, %d constraints" % (len(joints), len(template["rigModules"]), constraints))
    cmds.file(new=True, force=True)
    instances = build_instances(skeleton_file, template_file, COMPLEX_ASSET_ID, "cx", args.count, 14.0)
    last = key_rom(instances)
    print("%d instances, %d controls each, ROM to frame %d" % (len(instances), len(instances[0].controls), last))
    save_scene(os.path.join(out, "complex_x%d.mb" % args.count), 1, last)
    finish()


# ---- benchmarks --------------------------------------------------------------------------------------

def bench_eval(args):
    """Evaluation speed with no viewport, against the playback target. For each evaluation manager mode: the
    time is stepped through the frame range with MAnimControl, and every chain tip's world matrix is pulled
    (that pull is what evaluates in "off", plain DG; in serial and parallel the time change evaluates the whole
    graph). One warm-up pass, then `--passes` timed passes, averaged. Parallel is the mode that counts (D36), so
    it is the only one run unless --all-modes is given. Exits 1 if parallel is under the target."""
    from maya import cmds
    import maya.api.OpenMaya
    import maya.api.OpenMayaAnim
    start_maya(args.plugin)
    cmds.file(args.scene, open=True, force=True)
    frame_min = int(cmds.playbackOptions(query=True, minTime=True))
    frame_max = int(cmds.playbackOptions(query=True, maxTime=True))
    tips = [j for j in cmds.ls(type="joint", long=True) if not cmds.listRelatives(j, children=True, type="joint")]
    selection = maya.api.OpenMaya.MSelectionList()
    for joint in tips:
        selection.add(joint + ".worldMatrix[0]")
    plugs = [selection.getPlug(i) for i in range(selection.length())]
    unit = maya.api.OpenMaya.MTime.uiUnit()
    times = [maya.api.OpenMaya.MTime(frame, unit) for frame in range(frame_min, frame_max + 1)]
    print("scene %s: %d joints, %d pulled tips, frames %d-%d, target %.0f fps" % (
        os.path.basename(args.scene), len(cmds.ls(type="joint")), len(plugs), frame_min, frame_max, args.target_fps))

    def one_pass():
        started = time.perf_counter()
        for mtime in times:
            maya.api.OpenMayaAnim.MAnimControl.setCurrentTime(mtime)
            for plug in plugs:
                plug.asMObject()
        return time.perf_counter() - started

    results = {}
    for mode in (("off", "serial", "parallel") if args.all_modes else ("parallel",)):
        cmds.evaluationManager(mode=mode)
        one_pass()
        elapsed = sum(one_pass() for _ in range(args.passes)) / args.passes
        results[mode] = len(times) / elapsed
        print("%-8s %7.1f fps  %6.1f ms/frame  %s" % (mode, results[mode], 1000.0 * elapsed / len(times),
              "meets target" if results[mode] >= args.target_fps else "BELOW TARGET"))
    ok = results["parallel"] >= args.target_fps
    print("RESULT: %s (parallel %.1f fps, target %.0f)" % ("PASS" if ok else "FAIL", results["parallel"], args.target_fps))
    import maya.standalone
    maya.standalone.uninitialize()
    os._exit(0 if ok else 1)


def run_check(args):
    """Runs check_perf_scene.py's tests in this Maya. They are not part of the suite."""
    import unittest
    import tempfile
    start_maya(args.plugin)
    sys.path.insert(0, PERF_DIR)
    import check_perf_scene
    check_perf_scene.config.WORK_DIR = tempfile.mkdtemp(prefix="perf_check_")
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromModule(check_perf_scene))
    import maya.standalone
    maya.standalone.uninitialize()
    os._exit(0 if result.wasSuccessful() else 1)


def run_gui_script(args, script_name, **settings):
    """Runs a script from this folder in a Maya window on a copy of the plugin and prints the log it writes.
    `settings` become PERF_<NAME> environment variables."""
    work = tempfile.mkdtemp(prefix="perf_gui_")
    plugin_copy = os.path.join(work, "RigModules.mll")
    shutil.copyfile(args.plugin, plugin_copy)
    env = dict(os.environ, PERF_SCENE=os.path.abspath(args.scene).replace("\\", "/"),
               PERF_PLUGIN=plugin_copy.replace("\\", "/"), PERF_OUT=work.replace("\\", "/"),
               MAYA_SKIP_USERSETUP_PY="1", MAYA_DISABLE_CER="1")
    env.update({"PERF_" + name.upper(): str(value) for name, value in settings.items()})
    script = os.path.join(PERF_DIR, script_name).replace("\\", "/")
    command = [args.maya, "-command", 'python("exec(open(\'%s\').read())")' % script]
    subprocess.run(command, env=env, timeout=args.timeout)
    log = os.path.join(work, "log.txt")
    if os.path.exists(log):
        with open(log, encoding="utf-8") as handle:
            print(handle.read())
    else:
        print("No log was written: Maya did not finish. Work folder: %s" % work)


def bench_gui(args):
    """Plays the scene in a Maya window through playback_gui.py."""
    run_gui_script(args, "playback_gui.py", seconds=args.seconds)


def profile_gui(args):
    """Records the profiler over playback in a Maya window through profile_gui.py."""
    run_gui_script(args, "profile_gui.py", frames=args.frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("human", "complex", "bench", "gui", "profile", "check"):
        p = sub.add_parser(name)
        p.add_argument("--plugin", default=DEFAULT_PLUGIN)
        if name in ("human", "complex"):
            p.add_argument("--count", type=int, default=50)
            p.add_argument("--out", default=DEFAULT_OUT)
        if name == "human":
            p.add_argument("--frames", type=int, default=96)
            p.add_argument("--asset-dir", default=DEFAULT_ASSET_DIR)
        if name in ("bench", "gui", "profile"):
            p.add_argument("scene")
        if name == "bench":
            p.add_argument("--passes", type=int, default=3)
            p.add_argument("--target-fps", type=float, default=30.0)
            p.add_argument("--all-modes", action="store_true", help="also time serial and DG evaluation (slow)")
        if name == "gui":
            p.add_argument("--seconds", type=float, default=6.0)
        if name == "profile":
            p.add_argument("--frames", type=int, default=20)
        if name in ("gui", "profile"):
            p.add_argument("--maya", default=DEFAULT_MAYA)
            p.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    {"human": make_human, "complex": make_complex, "bench": bench_eval, "gui": bench_gui,
     "profile": profile_gui, "check": run_check}[args.command](args)


if __name__ == "__main__":
    main()
