"""Runs a script in a Maya window (maya.exe), for checks that need a viewport. Plain Python.

Maya's -command takes MEL, which parses the string before Python sees it, so a Windows path in the string
is mangled (a backslash is an MEL escape) and a space splits the argument. The script path therefore goes
through an environment variable, and the bootstrap has no path, no backslash and no space."""
import os

from mayalaunch import install, process

SCRIPT_VARIABLE = "MAYALAUNCH_SCRIPT"
# No spaces, no backslashes. Double quotes are only MEL's delimiters here: subprocess escapes them as \" for
# Windows, which is what Maya's command line parser expects.
BOOTSTRAP = "python(\"exec(open(__import__('os').environ['%s'],encoding='utf-8').read())\")" % SCRIPT_VARIABLE


def gui_command(maya_dir=None):
    """The argument list that starts Maya running the script named by SCRIPT_VARIABLE, which run_gui sets."""
    return [install.maya_exe(maya_dir), "-command", BOOTSTRAP]


def run_gui(script, env=None, timeout=300, maya_dir=None):
    """Runs a script in a Maya window and waits. The script should end with cmds.quit(force=True) and write
    a log line first, since a script that fails leaves Maya open until the timeout stops it. The process
    tree is stopped by pid on timeout, never by name, so the user's own Maya is untouched."""
    env = dict(env if env is not None else process.child_environment())
    env[SCRIPT_VARIABLE] = os.path.abspath(script)
    return process.run_watched(gui_command(maya_dir), env, timeout)
