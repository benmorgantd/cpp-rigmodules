"""Builds the performance scenes: many instances of one rig, animated, for playback-speed tests.

Run it with mayapy (set nothing else up: it loads a copy of the plugin and the python package itself):

    mayapy tools/perf/perf_scenes.py human   [--count 50] [--frames 96]
    mayapy tools/perf/perf_scenes.py complex [--count 50]
    mayapy tools/perf/perf_scenes.py floor   [--count 50]                  (the complex scene with no rig layer: D36's native floor)
    mayapy tools/perf/perf_scenes.py bench   SCENE.mb [--passes 3]        (evaluation only, no viewport)
    mayapy tools/perf/perf_scenes.py gui     SCENE.mb [--seconds 6]       (real playback in a Maya window)
    mayapy tools/perf/perf_scenes.py profile SCENE.mb [--frames 20]       (where playback time goes, in a Maya window)
    mayapy tools/perf/perf_scenes.py check                                (the generated rig is valid; seconds)

human    imports the asset's skeleton `--count` times, builds the asset's template on each, and keys every
         control on every frame through a cycle of load, mirror, flip and zero poses.
complex  generates a 272 joint skeleton and a template with 75 constraints (Parent, Orient and Point), Aim modules and
         TwoBoneIk limbs, builds it `--count` times, and keys a -90 to 90 range of motion on every control's rotate X, Y
         and Z, plus the limbs' handles, poles, roll and settings (blend, stretch, soft, pin) through every branch.

floor    builds the same controls, joints, hierarchy and keys as `complex` with native transforms and joints only, so
         `bench` on complex_xN.mb and on floor_xN.mb gives the 1.5 times ratio of D36 (run both in one session).

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
import sys
import tempfile
import time

PERF_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.path.dirname(os.path.dirname(PERF_DIR))
DEFAULT_OUT = os.path.join(PERF_DIR, "out")
DEFAULT_PLUGIN = os.path.join(REPO_DIR, "cpp", "x64", "Release", "RigModules.mll")
DEFAULT_ASSET_DIR = r"C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped"

# Starting and ending Maya is mayalaunch's job (D48).
sys.path.insert(0, os.path.join(REPO_DIR, "python"))
import mayalaunch  # noqa: E402

HUMAN_ASSET_ID = "test\\character\\biped"
COMPLEX_ASSET_ID = "test\\character\\complex"

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
# Aim sets (aim, target and up joints) and limbs (three limb joints and three marker joints) are listed apart.
# 50 Center joints + 2 x 111 side joints = 272 joints. 7 Center + 2 x 34 side constraints = 75.
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


def limb_specs():
    """TwoBoneIk limbs, written for the Left side and built on both. `at`: (chain base, joint index) the limb's root is
    placed from and parented to (a Left chain's joint is matched by side, a Center one is shared). `upper` and `lower`
    are the bone vectors, `ik`, `pole` and `settings` the marker joints' offsets from the end, the mid and the root joint.
    The pole sits on the bend side. `constraints`: the spaces on the start and the IK handle, as for a chain."""
    layout = ("layout", "end", "World")
    arm_start = ("Parent", [("clavicle", "end", "Clavicle"), ("spine", "end", "Chest"), layout])
    leg_start = ("Orient", [("spine", "start", "Hips"), layout])
    ik_space = ("Parent", [("spine", "end", "Chest"), ("spine", "start", "Hips"), layout])
    limbs = []
    for k, base in enumerate(["limbA", "limbB", "limbC"]):
        limbs.append({"base": base, "at": ("clavicle", 0), "parent": ("clavicle", "end"),
                      "offset": (0.3, 0.3 + 0.35 * k, 0.5 - 0.5 * k), "upper": (1.6, -0.2, -0.5), "lower": (1.6, -0.2, 0.5),
                      "ik": (0, 0, 0), "pole": (0, 0, -1.5), "settings": (0, 1.2, 0),
                      "constraints": {"start": arm_start, "ik": ik_space}})
    for k, base in enumerate(["limbD", "limbE", "limbF"]):
        limbs.append({"base": base, "at": ("spine", 0), "parent": ("spine", "start"),
                      "offset": (0.3 + 0.25 * k, -0.4, 0.3 - 0.3 * k), "upper": (0.05, -1.5, 0.45), "lower": (0.0, -1.5, -0.45),
                      "ik": (0, 0, 0), "pole": (0, 0, 1.5), "settings": (0.6, 0, 0),
                      "constraints": {"start": leg_start, "ik": ik_space}})
    return limbs


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
    """Name of an aim set's or a limb's marker joint, role being aim, target and up, or ik, pole and settings. Without
    its side suffix it is the usage."""
    return "%s%s_%s" % (base, role.capitalize(), side_suffix(side))


def usage_name(base, index):
    return "%s%d" % (base, index + 1)


def _sides(spec):
    return ["Left", "Right"] if spec["side"] == "Left" else ["Center"]


def _flip(vector, side):
    return (-vector[0], vector[1], vector[2]) if side == "Right" else tuple(vector)


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _secondary(step):
    """The secondary axis hint for a bone: Z up along a mostly vertical bone, else Y up."""
    length = math.sqrt(sum(c * c for c in step))
    return "zup" if abs(step[1]) / length > 0.7 else "yup"


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
            secondary = _secondary(step)
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
    for limb in limb_specs():
        at_base, at_index = limb["at"]
        for side in ("Left", "Right"):
            at_side = side if spec_side[at_base] == "Left" else "Center"
            parent = names[(at_base, at_side)][at_index]
            root = _add(positions[(at_base, at_side)][at_index], _flip(limb["offset"], side))
            upper, lower = _flip(limb["upper"], side), _flip(limb["lower"], side)
            mid = _add(root, upper)
            end = _add(mid, lower)
            chain = module_name(limb["base"], side)
            previous = parent
            for i, (position, step) in enumerate([(root, upper), (mid, lower), (end, None)]):
                name = joint_name(limb["base"], i, side)
                joints.append({"name": name, "parent": previous, "position": position, "side": side,
                               "usage": usage_name(limb["base"], i), "chain": chain, "aim_next": step is not None,
                               "secondary": _secondary(step) if step else "yup"})
                previous = name
            # The marker joints are the module's own: hung from the limb's parent, never exported or skinned.
            for role, anchor in (("ik", end), ("pole", mid), ("settings", root)):
                name = aim_joint(limb["base"], role, side)
                joints.append({"name": name, "parent": parent, "side": side, "usage": name[:-2], "chain": chain,
                               "aim_next": False, "secondary": "yup", "marker": True,
                               "position": _add(anchor, _flip(limb[role], side))})
    return joints


def _constraint_entry(name, constraint_type, target, drivers, side, side_of):
    """A template constraint. drivers: [(module base, socket, label), ...], the first at weight 1."""
    entry = {"name": name, "type": constraint_type, "target": target, "drivers": []}
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
    return entry


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
            constraints.append(_constraint_entry("%s_space%s" % (spec["base"], "_l" if side == "Left" else ""),
                                                 constraint_type, "%s.start" % name, drivers, side, side_of))
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
    for limb in limb_specs():
        name = module_name(limb["base"], "Left")
        parent_base, parent_socket = limb["parent"]
        modules.append({
            "moduleType": "TwoBoneIk",
            "name": name,
            "side": "Left",
            "shapeType": "Box",
            "mirror": True,
            "moduleArgs": {
                "joints": [["Left", usage_name(limb["base"], i)] for i in range(3)],
                **{role: ["Left", aim_joint(limb["base"], role, "Left")[:-2]] for role in ("ik", "pole", "settings")},
            },
            # The pole can't name the module's own socket (D17): it hangs from the layout, and its Ik space is switched later.
            "parentStructure": {"start": "%s.%s" % (module_name(parent_base, side_of[parent_base]), parent_socket),
                                "ik": "layout.end", "pole": "layout.end"},
        })
        for plug, (constraint_type, drivers) in limb["constraints"].items():
            constraints.append(_constraint_entry("%s_%s_space_l" % (limb["base"], plug), constraint_type,
                                                 "%s.%s" % (name, plug), drivers, "Left", side_of))
    return {
        "formatVersion": 1,
        "rigName": "complex272",
        "rigVersion": 1,
        "rigTemplateName": "Complex272",
        "rigAuthor": "perf_scenes.py",
        "rigModules": modules,
        "constraints": constraints,
    }


def built_constraint_count(template):
    return sum(2 if entry.get("mirror") else 1 for entry in template["constraints"])


# ---- Maya side ---------------------------------------------------------------------------------------

def start_maya(plugin):
    """Starts standalone Maya and loads a copy of the plugin (a loaded .mll locks the build output)."""
    mayalaunch.start_standalone()
    from maya import cmds
    cmds.loadPlugin(mayalaunch.copy_plugin(plugin, tempfile.mkdtemp(prefix="perf_plugin_")))
    python_dir = os.path.join(REPO_DIR, "python")
    if python_dir not in sys.path:
        sys.path.insert(0, python_dir)
    cmds.undoInfo(state=False)


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
        exported = not joint.get("marker", False)
        cmds.rigJointMarkup(joint["name"], usage=joint["usage"], side=joint["side"], export=exported, skin=exported)


def import_skeleton(skeleton_file, namespace):
    """The joints of a skeleton file in a namespace. A skeleton.json (the asset's only skeleton format, D54) is
    created by loadRigSkeleton, any other file is a scene and is imported."""
    from maya import cmds
    if skeleton_file.endswith(".json"):
        return cmds.loadRigSkeleton(file=skeleton_file, namespace=namespace)
    cmds.file(skeleton_file, i=True, namespace=namespace, ignoreVersion=True)
    return cmds.ls(namespace + ":*", type="joint")


class RigInstance(object):
    def __init__(self, namespace, rig_root, controls, limbs=()):
        self.namespace = namespace
        self.rig_root = rig_root
        self.controls = controls   # every control except the layout's, which place the instance
        self.limbs = list(limbs)   # one dict per TwoBoneIk limb: its six controls by role (LIMB_ROLES)


LIMB_ROLES = ("start", "lower", "end", "ik", "pole", "settings")


def find_limbs(controls):
    """The TwoBoneIk limbs among rigmodules.query controls: one dict of control names by role per module. The three
    limb joints' usages end in 1, 2 and 3 and the markers' in Ik, Pole and Settings (usage_name and aim_joint)."""
    by_module = {}
    for control in controls:
        if control.module_type != "TwoBoneIk":
            continue
        for role, suffix in zip(LIMB_ROLES, ("1", "2", "3", "Ik", "Pole", "Settings")):
            if control.usage.endswith(suffix):
                by_module.setdefault(control.module, {})[role] = control.node
    return list(by_module.values())


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
        instances.append(RigInstance(namespace, rig_root, [c.node for c in controls if c.module_type != "Layout"],
                                     find_limbs(controls)))
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
    # The asset layout of D54: the skeleton is its skeleton.json, and the rig's files are in rigs/human.
    skeleton = os.path.join(args.asset_dir, "skeletons", "default", "skeleton.json")
    template = os.path.join(args.asset_dir, "rigs", "human", "template.json")
    pose_file = os.path.join(args.asset_dir, "rigs", "human", "poses", "star_pose1.json")
    start_maya(args.plugin)
    instances = build_instances(skeleton.replace("\\", "/"), template.replace("\\", "/"), HUMAN_ASSET_ID, "biped",
                                args.count, 5.0)
    keys = key_pose_series(instances, args.frames, pose_file.replace("\\", "/"))
    print("%d instances, %d keys" % (len(instances), keys))
    save_scene(os.path.join(out, "human_x%d.mb" % args.count), 1, args.frames)
    mayalaunch.finish_standalone(0)


# ---- the complex scene -------------------------------------------------------------------------------

def _key_curve(plugs, keys):
    from maya import cmds
    for frame, value in keys:
        cmds.setKeyframe(plugs, time=frame, value=value)


def key_rom(instances, segment=ROM_SEGMENT_FRAMES, degrees=ROM_DEGREES):
    """Rotate X, then Y, then Z, on every control at once: 0 to -degrees to +degrees to 0 over `segment` frames
    each. A limb's pole and settings control have no rotation, so they are left out here, and key_limbs moves the
    limbs through their own channels. Returns the last frame."""
    for axis_index, axis in enumerate(ROM_AXES):
        start = 1 + axis_index * segment
        keys = [(start, 0.0), (start + segment * 0.25, -degrees), (start + segment * 0.75, degrees),
                (start + segment, 0.0)]
        for instance in instances:
            fixed = {limb[role] for limb in instance.limbs for role in ("pole", "settings")}
            _key_curve(["%s.%s" % (control, axis) for control in instance.controls if control not in fixed], keys)
    last = 1 + len(ROM_AXES) * segment
    key_limbs(instances, last)
    return last


# A limb's channels over the whole range, as fractions of it: (fraction, value). The blend walks IK (1), a blend (0.5)
# and FK (0), the only value that skips the IK solve. Stretch, soft and pin each get a stretch of frames in IK and in the
# blend, and the handle goes well past full reach, so the soft zone, the stretch and the clamp all run.
LIMB_CURVES = {
    ("settings", "ikFkBlend"): [(0, 1), (0.30, 1), (0.40, 0.5), (0.55, 0.5), (0.65, 0), (0.80, 0), (0.90, 1), (1, 1)],
    ("settings", "stretch"): [(0, 0), (0.10, 0), (0.25, 1), (0.55, 1), (0.65, 0), (1, 0)],
    ("settings", "soft"): [(0, 0), (0.10, 0.4), (0.40, 0.4), (0.50, 0), (1, 0)],
    ("pole", "pin"): [(0, 0), (0.30, 0), (0.42, 1), (0.50, 0), (0.90, 0), (0.95, 1), (1, 0)],
    ("ik", "roll"): [(0, 0), (0.25, 60), (0.5, -60), (0.75, 30), (1, 0)],
    ("ik", "translateX"): [(0, 0), (0.25, 2.5), (0.5, -2.0), (0.75, 1.0), (1, 0)],
    ("ik", "translateY"): [(0, 0), (0.25, -1.5), (0.5, 1.5), (0.75, -1.0), (1, 0)],
    ("ik", "translateZ"): [(0, 0), (0.25, 1.0), (0.5, -1.5), (0.75, 1.5), (1, 0)],
    ("pole", "translateX"): [(0, 0), (0.25, 1.0), (0.5, -1.0), (0.75, 0.5), (1, 0)],
    ("pole", "translateY"): [(0, 0), (0.25, -1.5), (0.5, 1.5), (0.75, -0.5), (1, 0)],
    ("pole", "translateZ"): [(0, 0), (0.25, 2.0), (0.5, -2.0), (0.75, 1.0), (1, 0)],
    ("start", "translateY"): [(0, 0), (0.5, 0.5), (1, 0)],
}
# The pole's space is an enum, so it steps: Parent, then Ik, then Parent.
LIMB_SPACE_KEYS = [(0, 0), (0.5, 1), (0.8, 0)]


def key_limbs(instances, last):
    """Keys every limb's channels (LIMB_CURVES and the pole's space) from frame 1 to `last`."""
    from maya import cmds
    span = last - 1
    for instance in instances:
        for limb in instance.limbs:
            for (role, channel), keys in LIMB_CURVES.items():
                _key_curve(["%s.%s" % (limb[role], channel)], [(1 + round(f * span), v) for f, v in keys])
            plug = "%s.%s" % (limb["pole"], "space")
            for f, value in LIMB_SPACE_KEYS:
                cmds.setKeyframe(plug, time=1 + round(f * span), value=value, outTangentType="step")


def make_complex(args):
    from maya import cmds
    out = args.out
    os.makedirs(out, exist_ok=True)
    template = build_template()
    constraints = built_constraint_count(template)
    joints = joint_layout()
    assert len(joints) == 272, len(joints)
    assert constraints == 75, constraints
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
    mayalaunch.finish_standalone(0)


# ---- the native floor --------------------------------------------------------------------------------

def build_floor_instance(joints, namespace):
    """One instance of the complex rig with no rig layer: a native transform for every control and a native joint,
    in the skeleton's hierarchy, for every joint, each driven 1:1 by its control's world matrix through
    offsetParentMatrix. Custom attributes stand in for the limbs' channels, so key_rom keys the same curves it keys on
    the rig. The aim joints have no control, as in the rig. Returns a RigInstance."""
    from maya import cmds
    limb_chains = {module_name(limb["base"], side) for limb in limb_specs() for side in ("Left", "Right")}
    aim_usages = {aim["base"] + "Aim" for aim in aim_specs()}
    controls = []
    limbs = {}
    for joint in joints:
        name = "%s:%s" % (namespace, joint["name"])
        parent = "%s:%s" % (namespace, joint["parent"]) if joint["parent"] else None
        cmds.createNode("joint", name=name, **({"parent": parent} if parent else {}))
        cmds.setAttr(name + ".inheritsTransform", 0)
        cmds.setAttr(name + ".translate", *joint["position"])
        if joint["usage"] in aim_usages:
            continue   # an aim joint has no control (it keeps its own position)
        control = cmds.createNode("transform", name=name + "_ctrl")
        cmds.setAttr(control + ".inheritsTransform", 0)
        cmds.setAttr(control + ".translate", *joint["position"])
        cmds.connectAttr(control + ".worldMatrix[0]", name + ".offsetParentMatrix")
        cmds.setAttr(name + ".translate", 0, 0, 0)
        controls.append(control)
        if joint["chain"] in limb_chains:
            for role, suffix in zip(LIMB_ROLES, ("1", "2", "3", "Ik", "Pole", "Settings")):
                if joint["usage"].endswith(suffix):
                    limbs.setdefault(joint["chain"], {})[role] = control
    for limb in limbs.values():
        for role, attributes in (("settings", ("ikFkBlend", "stretch", "soft")), ("ik", ("roll",)), ("pole", ("pin",))):
            for attribute in attributes:
                cmds.addAttr(limb[role], longName=attribute, attributeType="double", keyable=True)
        cmds.addAttr(limb["pole"], longName="space", attributeType="enum", enumName="Parent:Ik", keyable=True)
    return RigInstance(namespace, None, controls, limbs.values())


def make_floor(args):
    """The native floor of the complex scene (D36): the same controls, joints, hierarchy and keys with no rig layer, so
    `bench` on it and on complex_xN.mb gives the 1.5 times ratio. Run `bench` on both in the same session."""
    from maya import cmds
    os.makedirs(args.out, exist_ok=True)
    joints = joint_layout()
    start_maya(args.plugin)
    instances = []
    started = time.time()
    for index in range(args.count):
        instances.append(build_floor_instance(joints, "fl%03d" % (index + 1)))
        if (index + 1) % 10 == 0:
            print("built %d/%d floor rigs (%.0f s)" % (index + 1, args.count, time.time() - started))
    last = key_rom(instances)
    print("%d floor instances, %d controls each, ROM to frame %d" % (len(instances), len(instances[0].controls), last))
    save_scene(os.path.join(args.out, "floor_x%d.mb" % args.count), 1, last)
    mayalaunch.finish_standalone(0)


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
    mayalaunch.finish_standalone(0 if ok else 1)


def run_check(args):
    """Runs check_perf_scene.py's tests in this Maya. They are not part of the suite."""
    import unittest
    import tempfile
    start_maya(args.plugin)
    sys.path.insert(0, PERF_DIR)
    import check_perf_scene
    check_perf_scene.config.WORK_DIR = tempfile.mkdtemp(prefix="perf_check_")
    result = unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.loadTestsFromModule(check_perf_scene))
    mayalaunch.finish_standalone(0 if result.wasSuccessful() else 1)


def run_gui_script(args, script_name, **settings):
    """Runs a script from this folder in a Maya window on a copy of the plugin and prints the log it writes.
    `settings` become PERF_<NAME> environment variables."""
    work = tempfile.mkdtemp(prefix="perf_gui_")
    plugin_copy = mayalaunch.copy_plugin(args.plugin, work)
    extra = {"PERF_SCENE": os.path.abspath(args.scene).replace("\\", "/"),
             "PERF_PLUGIN": plugin_copy.replace("\\", "/"), "PERF_OUT": work.replace("\\", "/")}
    extra.update({"PERF_" + name.upper(): value for name, value in settings.items()})
    result = mayalaunch.run_gui(os.path.join(PERF_DIR, script_name), mayalaunch.child_environment(extra=extra),
                                args.timeout, args.maya_dir)
    log = os.path.join(work, "log.txt")
    if os.path.exists(log):
        with open(log, encoding="utf-8") as handle:
            print(handle.read())
    else:
        print("No log was written: Maya %s. Work folder: %s" % ("timed out" if result.timed_out else "did not finish", work))


def bench_gui(args):
    """Plays the scene in a Maya window through playback_gui.py."""
    run_gui_script(args, "playback_gui.py", seconds=args.seconds)


def profile_gui(args):
    """Records the profiler over playback in a Maya window through profile_gui.py."""
    run_gui_script(args, "profile_gui.py", frames=args.frames)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("human", "complex", "floor", "bench", "gui", "profile", "check"):
        p = sub.add_parser(name)
        p.add_argument("--plugin", default=DEFAULT_PLUGIN)
        if name in ("human", "complex", "floor"):
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
            p.add_argument("--maya-dir", default=None, help="Maya install folder (default: MAYA_LOCATION or Maya 2026)")
            p.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()
    {"human": make_human, "complex": make_complex, "floor": make_floor, "bench": bench_eval, "gui": bench_gui,
     "profile": profile_gui, "check": run_check}[args.command](args)


if __name__ == "__main__":
    main()
