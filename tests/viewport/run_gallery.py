"""Starts the manual VP2 shape gallery (gallery.py) in a Maya window and reports the result. Run it through
ViewportGallery.cmd. Not part of the suite: see tests/README.md.

Usage: mayapy run_gallery.py [PLUGIN]    (default: the Release build)
"""
import os
import shutil
import sys
import tempfile

TESTS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_DIR = os.path.dirname(TESTS_DIR)
DEFAULT_PLUGIN = os.path.join(REPO_DIR, "cpp", "x64", "Release", "RigModules.mll")
TIMEOUT_SECONDS = 300

sys.path.insert(0, os.path.join(REPO_DIR, "python"))
import mayalaunch  # noqa: E402


def main():
    plugin = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PLUGIN
    if not os.path.isfile(plugin):
        print("Plugin not found: %s. Build Release first." % plugin)
        return 2

    work = os.path.join(tempfile.gettempdir(), "RigViewportGallery")
    shutil.rmtree(work, ignore_errors=True)
    out = os.path.join(work, "out")
    os.makedirs(out)
    plugin_copy = mayalaunch.copy_plugin(plugin, work)

    env = mayalaunch.child_environment(extra={"RIG_GALLERY_PLUGIN": plugin_copy, "RIG_GALLERY_OUT": out})
    print("Starting Maya. It opens a window, playblasts, and closes by itself.")
    result = mayalaunch.run_gui(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gallery.py"),
                                env, TIMEOUT_SECONDS)

    log = os.path.join(out, "log.txt")
    lines = []
    if os.path.isfile(log):
        with open(log, encoding="utf-8") as handle:
            lines = handle.read().splitlines()
    print("\n".join(lines))
    if "done" not in lines:
        print("Gallery did not finish%s. See the log above." % (" (timed out)" if result.timed_out else ""))
        return 1
    print("Images: %s" % out)
    os.startfile(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
