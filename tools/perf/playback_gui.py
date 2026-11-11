"""Plays a scene in a Maya window and measures the frame rate. Started by `perf_scenes.py gui`, which
passes PERF_SCENE, PERF_PLUGIN, PERF_OUT and PERF_SECONDS. It is exec'd by maya.exe (so it has no __file__)
and writes log.txt to PERF_OUT, then quits Maya.

Each run is one evaluation manager mode with the viewport on or off. The time slider is stepped through the
frame range in a loop, one frame per event loop turn, and each step forces a redraw, so the rate is what
"play every frame" at no speed limit would do. (cmds.play does not advance in a script-started session.)
The first 1.5 seconds of each run are dropped.
"""
import os
import time

from maya import cmds
from maya import utils
from PySide6 import QtCore

SCENE = os.environ["PERF_SCENE"]
PLUGIN = os.environ["PERF_PLUGIN"]
OUT = os.environ["PERF_OUT"]
SECONDS = float(os.environ["PERF_SECONDS"])
WARMUP_MS = 1500

# (evaluation manager mode, viewport drawn). Direct DG evaluation is pulled by the draw, so with the
# viewport off it would measure nothing: that combination is left out.
RUNS = [("parallel", True), ("parallel", False), ("serial", True), ("serial", False), ("off", True)]

_state = {"frames": 0, "frame": 1, "min": 1, "max": 1, "running": False}


def log(message):
    print(message)
    with open(os.path.join(OUT, "log.txt"), "a", encoding="utf-8") as handle:
        handle.write(message + "\n")


def tick():
    if not _state["running"]:
        return
    frame = _state["frame"] + 1
    if frame > _state["max"]:
        frame = _state["min"]
    _state["frame"] = frame
    cmds.currentTime(frame)
    cmds.refresh(force=True)
    _state["frames"] += 1
    QtCore.QTimer.singleShot(0, tick)


def finish():
    log("done")
    cmds.quit(force=True)


def start_run(index, editor):
    if index >= len(RUNS):
        finish()
        return
    mode, viewport = RUNS[index]
    cmds.evaluationManager(mode=mode)
    cmds.modelEditor(editor, edit=True, allObjects=viewport)
    _state["frame"] = _state["min"]
    cmds.currentTime(_state["frame"])
    _state["frames"] = 0
    _state["running"] = True
    QtCore.QTimer.singleShot(0, tick)
    QtCore.QTimer.singleShot(WARMUP_MS, lambda: mark_start(index, editor))


def mark_start(index, editor):
    _state["start_frames"] = _state["frames"]
    _state["start_time"] = time.perf_counter()
    QtCore.QTimer.singleShot(int(SECONDS * 1000), lambda: end_run(index, editor))


def end_run(index, editor):
    elapsed = time.perf_counter() - _state["start_time"]
    frames = _state["frames"] - _state["start_frames"]
    _state["running"] = False
    mode, viewport = RUNS[index]
    fps = frames / elapsed
    log("%-8s viewport %-3s  %7.1f fps  %6.1f ms/frame  (%d frames in %.1f s)" % (
        mode, "on" if viewport else "off", fps, 1000.0 / fps if fps else 0.0, frames, elapsed))
    QtCore.QTimer.singleShot(200, lambda: start_run(index + 1, editor))


def run():
    cmds.loadPlugin(PLUGIN)
    cmds.file(SCENE, open=True, force=True)
    panel = cmds.getPanel(type="modelPanel")[0]
    editor = cmds.modelPanel(panel, query=True, modelEditor=True)
    cmds.lookThru(panel, "persp")
    cmds.viewFit("persp", allObjects=True)
    frame_min = cmds.playbackOptions(query=True, minTime=True)
    frame_max = cmds.playbackOptions(query=True, maxTime=True)
    _state["min"] = int(frame_min)
    _state["max"] = int(frame_max)
    log("scene %s, frames %d-%d, %d joints, %d rigControlNodes" % (
        os.path.basename(SCENE), frame_min, frame_max, len(cmds.ls(type="joint")), len(cmds.ls(type="rigControlNode"))))
    log("display: %s" % cmds.about(version=True))
    start_run(0, editor)


utils.executeDeferred(run)
