"""mayalaunch: the one place that starts Maya and ends it safely, shared by RigModules and mayadev.

Plain Python with no dependencies and no Maya import at module level, so any process can use it.

    install     find_maya_dir, mayapy_exe, maya_exe
    process     child_environment, copy_plugin, run_mayapy, start_mayapy, run_watched, close_crash_reporters,
                kill_process_tree
    standalone  start_standalone, finish_standalone, Redirect (inside a mayapy process)
    gui         run_gui (a script in a Maya window)
"""
from mayalaunch.gui import run_gui
from mayalaunch.install import find_maya_dir, maya_exe, mayapy_exe
from mayalaunch.process import (ProcessResult, child_environment, close_crash_reporters, copy_plugin,
                                kill_process_tree, run_mayapy, run_watched, start_mayapy)
from mayalaunch.standalone import Redirect, finish_standalone, start_standalone, stop_standalone

__all__ = ["ProcessResult", "Redirect", "child_environment", "close_crash_reporters", "copy_plugin",
           "find_maya_dir", "finish_standalone", "kill_process_tree", "maya_exe", "mayapy_exe", "run_gui",
           "run_mayapy", "run_watched", "start_mayapy", "start_standalone", "stop_standalone"]
