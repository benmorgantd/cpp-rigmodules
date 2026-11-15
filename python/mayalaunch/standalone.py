"""The inside of a mayapy process: start Maya standalone with its output in a log, and end the process safely.
Everything but the Maya import is plain Python, and the Maya import happens inside the functions."""
import os
import sys
import traceback


class Redirect:
    """Sends fd 1 and 2 (Maya's C++ output and Python's print) to a log file until stop().
    With python_streams, sys.stdout and sys.stderr go there too: Maya's standalone stdout can fail with
    WinError 6 on flush when the OS handle is swapped under it."""

    def __init__(self, log_path, python_streams=False, mode="a"):
        sys.stdout.flush()
        sys.stderr.flush()
        self.saved_fds = (os.dup(1), os.dup(2))
        self.saved_streams = (sys.stdout, sys.stderr) if python_streams else None
        self.log = open(log_path, mode, encoding="utf-8", errors="replace")
        os.dup2(self.log.fileno(), 1)
        os.dup2(self.log.fileno(), 2)
        if python_streams:
            sys.stdout = sys.stderr = self.log

    def stop(self):
        self.log.flush()
        if self.saved_streams:
            sys.stdout, sys.stderr = self.saved_streams
        os.dup2(self.saved_fds[0], 1)
        os.dup2(self.saved_fds[1], 2)
        os.close(self.saved_fds[0])
        os.close(self.saved_fds[1])
        self.log.close()


def start_standalone(name="python", skip_usersetup=True):
    """Initializes maya.standalone. The crash dialog is off before Maya starts, because Maya reads the
    variable at startup. Variables already set by the parent are kept."""
    os.environ.setdefault("MAYA_DISABLE_CER", "1")
    if skip_usersetup:
        os.environ.setdefault("MAYA_SKIP_USERSETUP_PY", "1")
    import maya.standalone
    maya.standalone.initialize(name=name)


def stop_standalone():
    """Uninitializes Maya. Maya 2026 crashes at process exit without it."""
    import maya.standalone
    try:
        maya.standalone.uninitialize()
    except Exception:
        traceback.print_exc()


def finish_standalone(exit_code=0):
    """Ends a mayapy script: uninitialize, flush, then leave without running Maya's exit handlers, which
    crash in Maya 2026. Never returns."""
    stop_standalone()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(exit_code)
