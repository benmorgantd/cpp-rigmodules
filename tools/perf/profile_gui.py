"""Records Maya's profiler in a Maya window and writes tables of where the time went. Started by
`perf_scenes.py profile`, which passes PERF_SCENE, PERF_PLUGIN, PERF_OUT and PERF_FRAMES. It is exec'd by
maya.exe (so it has no __file__), writes log.txt to PERF_OUT, then quits Maya.

Parallel evaluation, cached playback off. The parts:

check     edits one control's shape and redraws. That must log RigModules events, so an empty RigModules table
          later means the plugin wasn't called, not that its profiler category didn't record.
select    selects every control of one rig and then clears, PERF_FRAMES times, with a redraw after each. Then the
          same twice for every control in the scene. This is where the draw override's selection lookup runs (VP2
          re-prepares controls whose display status changed).
playback  steps PERF_FRAMES frames, one draw per frame (currentTime redraws the viewport in a GUI session):
          the viewport drawn, suspended (refresh suspend), drawn with joints hidden, and drawn with every control
          hidden. The differences split the draw time between controls, joints and the rest.

Each timed part runs once without the profiler for the time, then once recorded. Node evaluation events are
named after the node, so they are grouped by node type and the attribute in their description. Durations from
worker threads add up past the wall time: compare rows with each other, and read the time from the unprofiled run.
"""
import os
import time

import maya.OpenMaya
from maya import cmds
from maya import utils

SCENE = os.environ["PERF_SCENE"]
PLUGIN = os.environ["PERF_PLUGIN"]
OUT = os.environ["PERF_OUT"]
FRAMES = int(os.environ["PERF_FRAMES"])
BUFFER_MB = 1500
TOP_ROWS = 20


def log(message):
    print(message)
    with open(os.path.join(OUT, "log.txt"), "a", encoding="utf-8") as handle:
        handle.write(message + "\n")


def event_key(name, description, node_types):
    """A node's name becomes its node type, and a plug in the description keeps only its attribute, so 20000
    controls make one row per kind of event, not 20000."""
    if name not in node_types:
        node_types[name] = cmds.nodeType(name) if cmds.objExists(name) else None
    node_type = node_types[name]
    if not node_type:
        return name
    detail = (description or "").split(".", 1)[-1]
    return "%s %s" % (node_type, detail) if detail else node_type


def summarize():
    profiler = maya.OpenMaya.MProfiler
    rows = {}
    node_types = {}
    for i in range(profiler.getEventCount()):
        category = profiler.getCategoryName(profiler.getEventCategory(i))
        key = (category, event_key(profiler.getEventName(i), profiler.getDescription(i), node_types))
        count, total = rows.get(key, (0, 0))
        rows[key] = (count + 1, total + profiler.getEventDuration(i))
    return rows


def record(action):
    cmds.profiler(reset=True)
    cmds.profiler(sampling=True)
    action()
    cmds.profiler(sampling=False)
    return summarize()


def timed(action):
    start = time.perf_counter()
    action()
    return time.perf_counter() - start


def write_table(title, rows, per):
    log("  %s" % title)
    log("    %-14s %-50s %9s %9s %10s" % ("category", "event", "calls", "per step", "total ms"))
    for (category, name), (count, total) in rows:
        log("    %-14s %-50s %9d %9.1f %10.1f" % (category[:14], name[:50], count, count / float(per), total / 1000.0))


def write_tables(rows, per):
    ours = sorted(((k, v) for k, v in rows.items() if k[0] == "RigModules"), key=lambda kv: kv[0][1])
    write_table("RigModules events", ours, per)
    write_table("top %d events by total time" % TOP_ROWS, sorted(rows.items(), key=lambda kv: -kv[1][1])[:TOP_ROWS], per)
    log("")


def check_plugin_events_record():
    control = cmds.ls(type="rigControlNode")[0]
    alpha = cmds.getAttr(control + ".wireframeAlpha")

    def edit():
        cmds.setAttr(control + ".wireframeAlpha", 0.5 if alpha > 0.75 else 1.0)
        cmds.refresh(force=True)
        cmds.setAttr(control + ".wireframeAlpha", alpha)
        cmds.refresh(force=True)

    edit()   # the first draws after load prepare every control
    ours = sorted((k[1], v) for k, v in record(edit).items() if k[0] == "RigModules")
    log("check: a shape edit on %s and two redraws" % control)
    for name, (count, total) in ours:
        log("    %-50s %9d calls %10.2f ms" % (name, count, total / 1000.0))
    if not ours:
        log("    no RigModules events: the category is not recording, so the tables below can't show plugin calls")
    log("")


def run_selection(label, nodes, repeats):
    def toggle():
        for _ in range(repeats):
            cmds.select(nodes, replace=True)
            cmds.refresh(force=True)
            cmds.select(clear=True)
            cmds.refresh(force=True)

    toggle()   # warm-up
    elapsed = timed(toggle)
    steps = 2 * repeats
    log("select %s (%d controls), then clear, %d times: %.1f ms per select or clear with its redraw, unprofiled" % (
        label, len(nodes), repeats, 1000.0 * elapsed / steps))
    write_tables(record(toggle), steps)


def run_playback(label, frame_min, frame_max, suspend=False):
    def play():
        frame = frame_min
        for _ in range(FRAMES):
            cmds.currentTime(frame)
            frame = frame_min if frame >= frame_max else frame + 1

    cmds.refresh(suspend=suspend)
    try:
        play()   # warm-up
        elapsed = timed(play)
        log("playback, %s: %.1f ms/frame (%.1f fps) unprofiled, %d frames" % (
            label, 1000.0 * elapsed / FRAMES, FRAMES / elapsed, FRAMES))
        write_tables(record(play), FRAMES)
    finally:
        cmds.refresh(suspend=False)


def run():
    try:
        cmds.loadPlugin(PLUGIN)
        cmds.file(SCENE, open=True, force=True)
        cmds.evaluationManager(mode="parallel")
        # A saved scene can turn on cached playback, which restores results instead of evaluating the rig.
        cmds.evaluator(name="cache", enable=False)
        cmds.profiler(bufferSize=BUFFER_MB)
        # A category added by a plugin may start with recording off. A build from before the category has none.
        if maya.OpenMaya.MProfiler.getCategoryIndex("RigModules") >= 0:
            maya.OpenMaya.MProfiler.setCategoryRecording("RigModules", True)
        # The editor playblast and refresh draw, which is not always the first model panel.
        editor = cmds.playblast(activeEditor=True)
        cmds.modelEditor(editor, edit=True, camera="persp")
        cmds.viewFit("persp", allObjects=True)
        frame_min = int(cmds.playbackOptions(query=True, minTime=True))
        frame_max = int(cmds.playbackOptions(query=True, maxTime=True))
        log("scene %s, %d joints, %d rigControlNodes, parallel evaluation, cached playback off, %s" % (
            os.path.basename(SCENE), len(cmds.ls(type="joint")), len(cmds.ls(type="rigControlNode")),
            cmds.about(version=True)))
        log("")
        check_plugin_events_record()
        controls = cmds.ls(type="rigControlNode")
        rig_namespace = controls[0].rpartition(":")[0]
        run_selection("one rig", [c for c in controls if c.rpartition(":")[0] == rig_namespace], FRAMES)
        run_selection("every rig", controls, 2)
        run_playback("viewport drawn", frame_min, frame_max)
        run_playback("viewport suspended", frame_min, frame_max, suspend=True)
        # Which objects the draw time goes to. Both are restored, so the runs don't change each other.
        cmds.modelEditor(editor, edit=True, joints=False)
        run_playback("viewport drawn, joints hidden", frame_min, frame_max)
        cmds.modelEditor(editor, edit=True, joints=True)
        cmds.hide(controls)
        run_playback("viewport drawn, controls hidden", frame_min, frame_max)
        cmds.showHidden(controls)
        log("done")
    except Exception as error:
        log("failed: %s" % error)
    cmds.quit(force=True)


utils.executeDeferred(run)
