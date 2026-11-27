"""mayalaunch tests (python/mayalaunch). Plain Python except MayapySmoke, which runs a real mayapy.
They start child processes, so they don't use the scene: each one is independent of the rig tests."""
import os
import sys
import tempfile
import time
import unittest

PYTHON_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), "python")
sys.path.insert(0, PYTHON_DIR)

import mayalaunch
from mayalaunch import gui, install, process, standalone


def fake_maya(root):
    os.makedirs(os.path.join(root, "bin"))
    for name in ("mayapy.exe", "maya.exe"):
        open(os.path.join(root, "bin", name), "w").close()
    return root


class InstallTests(unittest.TestCase):
    def test_argument_wins_over_environment(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            fake_maya(os.path.join(a, "m"))
            fake_maya(os.path.join(b, "m"))
            old = os.environ.get("MAYA_LOCATION")
            os.environ["MAYA_LOCATION"] = os.path.join(b, "m")
            try:
                self.assertEqual(install.find_maya_dir(os.path.join(a, "m")), os.path.join(a, "m"))
                self.assertEqual(install.find_maya_dir(), os.path.join(b, "m"))
            finally:
                if old is None:
                    del os.environ["MAYA_LOCATION"]
                else:
                    os.environ["MAYA_LOCATION"] = old

    def test_exe_paths(self):
        with tempfile.TemporaryDirectory() as root:
            fake_maya(os.path.join(root, "m"))
            self.assertEqual(install.mayapy_exe(os.path.join(root, "m")), os.path.join(root, "m", "bin", "mayapy.exe"))
            self.assertEqual(install.maya_exe(os.path.join(root, "m")), os.path.join(root, "m", "bin", "maya.exe"))

    def test_missing_install_names_what_was_tried(self):
        with tempfile.TemporaryDirectory() as root:
            missing = os.path.join(root, "nope")
            old = os.environ.pop("MAYA_LOCATION", None)
            try:
                with self.assertRaises(FileNotFoundError) as caught:
                    install.find_maya_dir(missing, version="1900")
            finally:
                if old is not None:
                    os.environ["MAYA_LOCATION"] = old
            self.assertIn(missing, str(caught.exception))
            self.assertIn("MAYA_LOCATION", str(caught.exception))

    def test_install_without_the_exe(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, "bin"))
            with self.assertRaises(FileNotFoundError):
                install.mayapy_exe(root)


class EnvironmentTests(unittest.TestCase):
    def test_defaults(self):
        env = process.child_environment()
        self.assertEqual(env["MAYA_DISABLE_CER"], "1")
        self.assertEqual(env["MAYA_SKIP_USERSETUP_PY"], "1")
        self.assertEqual(env["PYTHONDONTWRITEBYTECODE"], "1")

    def test_userSetup_can_stay_on(self):
        os.environ.pop("MAYA_SKIP_USERSETUP_PY", None)
        self.assertNotIn("MAYA_SKIP_USERSETUP_PY", process.child_environment(skip_usersetup=False))

    def test_work_dir_takes_the_temp_folders(self):
        with tempfile.TemporaryDirectory() as work:
            env = process.child_environment(work_dir=work)
            self.assertEqual(env["TEMP"], os.path.join(work, "maya_temp"))
            self.assertEqual(env["TMP"], env["TEMP"])
            self.assertTrue(os.path.isdir(env["TEMP"]))

    def test_extra_values_are_strings(self):
        self.assertEqual(process.child_environment(extra={"PERF_COUNT": 5})["PERF_COUNT"], "5")

    def test_does_not_change_this_process(self):
        os.environ.pop("MAYA_DISABLE_CER", None)
        process.child_environment()
        self.assertNotIn("MAYA_DISABLE_CER", os.environ)

    def test_copy_plugin(self):
        with tempfile.TemporaryDirectory() as root:
            source = os.path.join(root, "P.mll")
            with open(source, "w") as handle:
                handle.write("x")
            copy = process.copy_plugin(source, os.path.join(root, "out"))
            self.assertEqual(copy, os.path.join(root, "out", "P.mll"))
            self.assertTrue(os.path.isfile(source))


class RunWatchedTests(unittest.TestCase):
    def run_python(self, code, **kwargs):
        return process.run_watched([sys.executable, "-c", code], **kwargs)

    def test_success(self):
        result = self.run_python("pass")
        self.assertTrue(result.ok)
        self.assertEqual(result.closed_reporters, 0)

    def test_exit_code(self):
        result = self.run_python("import sys; sys.exit(3)")
        self.assertEqual(result.exit_code, 3)
        self.assertFalse(result.ok)

    def test_timeout_stops_the_process(self):
        started = time.time()
        result = self.run_python("import time; time.sleep(60)", timeout=1)
        self.assertTrue(result.timed_out)
        self.assertFalse(result.ok)
        self.assertLess(time.time() - started, 30)

    def test_timeout_stops_children_too(self):
        marker = tempfile.mktemp()
        grandchild = "import time; time.sleep(3); open(%r, 'w').close()" % marker
        code = ("import subprocess, sys, time\n"
                "subprocess.Popen([sys.executable, '-c', %r])\n"
                "time.sleep(60)" % grandchild)
        started = time.time()
        # Wait until the grandchild has started before the timeout kills the tree.
        self.run_python(code, timeout=2)
        self.assertLess(time.time() - started, 30)
        time.sleep(4)
        self.assertFalse(os.path.exists(marker))


class RedirectTests(unittest.TestCase):
    def test_captures_fd_output_and_restores(self):
        with tempfile.TemporaryDirectory() as root:
            log = os.path.join(root, "log.txt")
            redirect = standalone.Redirect(log, python_streams=True)
            try:
                os.write(1, b"from c++\n")
                print("from python")
            finally:
                redirect.stop()
            with open(log, encoding="utf-8") as handle:
                text = handle.read()
            self.assertIn("from c++", text)
            self.assertIn("from python", text)
            self.assertIsNot(sys.stdout, redirect.log)


class GuiTests(unittest.TestCase):
    def test_bootstrap_survives_mel_and_the_powershell_launch(self):
        self.assertNotIn("\\", gui.BOOTSTRAP)
        self.assertNotIn(" ", gui.BOOTSTRAP)
        self.assertIn(gui.SCRIPT_VARIABLE, gui.BOOTSTRAP)

    def test_command_has_no_script_path(self):
        with tempfile.TemporaryDirectory() as root:
            fake_maya(os.path.join(root, "m"))
            command = gui.gui_command(os.path.join(root, "m"))
            self.assertEqual(command[1], "-command")
            self.assertEqual(len(command), 3)


class PackageTests(unittest.TestCase):
    def test_every_exported_name_exists(self):
        for name in mayalaunch.__all__:
            self.assertTrue(hasattr(mayalaunch, name), name)


class MayapySmoke(unittest.TestCase):
    """A real mayapy runs a script through the launcher and the script ends with finish_standalone."""

    def setUp(self):
        try:
            self.mayapy = install.mayapy_exe()
        except FileNotFoundError:
            self.skipTest("no Maya install")

    def test_script_round_trip(self):
        with tempfile.TemporaryDirectory() as root:
            out = os.path.join(root, "out.txt").replace("\\", "/")
            script = os.path.join(root, "script.py")
            with open(script, "w", encoding="utf-8") as handle:
                handle.write(
                    "import sys\n"
                    "sys.path.insert(0, %r)\n"
                    "import mayalaunch\n"
                    "mayalaunch.start_standalone()\n"
                    "from maya import cmds\n"
                    "import os\n"
                    "open(%r, 'w').write(str(cmds.about(batch=True)) + os.environ['MAYA_DISABLE_CER'])\n"
                    "mayalaunch.finish_standalone(7)\n"
                    % (PYTHON_DIR, out))
            result = mayalaunch.run_mayapy(script, env=mayalaunch.child_environment(work_dir=root), timeout=180)
            self.assertFalse(result.timed_out)
            self.assertEqual(result.exit_code, 7)
            with open(out) as handle:
                self.assertEqual(handle.read(), "True1")


if __name__ == "__main__":
    unittest.main()
