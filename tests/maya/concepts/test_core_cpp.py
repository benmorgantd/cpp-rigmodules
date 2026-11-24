"""core/ tests: the Maya-free C++ in cpp/core, built into one executable and run (D20, D73).

core/ has no Maya headers, so its tests are plain C++ in tests/core (CoreTests.h is the harness). This file builds them
with the Visual Studio compiler and runs the program, which prints one PASS or FAIL line per test. The program is cached
in cpp/x64/CoreTests and rebuilt only when a source or header it reads is newer. This test imports no maya module and
runs in the plain Python process beside the Maya tiers.
"""
import glob
import os
import subprocess
import unittest

REPO_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CPP_DIR = os.path.join(REPO_DIR, "cpp")
CORE_DIR = os.path.join(CPP_DIR, "core")
TESTS_DIR = os.path.join(REPO_DIR, "tests", "core")
BUILD_DIR = os.path.join(CPP_DIR, "x64", "CoreTests")
EXE = os.path.join(BUILD_DIR, "CoreTests.exe")
VSWHERE = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                       "Microsoft Visual Studio", "Installer", "vswhere.exe")


def sources():
    core = [path for path in glob.glob(os.path.join(CORE_DIR, "*.cpp")) if not path.endswith("CorePch.cpp")]
    return sorted(core + glob.glob(os.path.join(TESTS_DIR, "*.cpp")))


def inputs():
    return sources() + glob.glob(os.path.join(CORE_DIR, "*.h")) + glob.glob(os.path.join(TESTS_DIR, "*.h"))


def is_current():
    if not os.path.exists(EXE):
        return False
    built = os.path.getmtime(EXE)
    return all(os.path.getmtime(path) <= built for path in inputs())


def vcvars():
    installation = subprocess.run([VSWHERE, "-latest", "-products", "*", "-property", "installationPath"],
                                  capture_output=True, text=True, check=True).stdout.strip()
    return os.path.join(installation, "VC", "Auxiliary", "Build", "vcvars64.bat")


def build():
    os.makedirs(BUILD_DIR, exist_ok=True)
    script = os.path.join(BUILD_DIR, "build.cmd")
    lines = [
        "@echo off",
        'set "PATH=%PATH%;{}"'.format(os.path.dirname(VSWHERE)),
        'call "{}" >nul'.format(vcvars()),
        'cl /nologo /std:c++20 /EHsc /MP /O1 /W3 /FI "{pch}" /I "{cpp}" /I "{include}" /I "{tests}" /Fo"{out}\\\\" /Fe"{exe}" {files}'.format(
            pch=os.path.join(CORE_DIR, "CorePch.h"), cpp=CPP_DIR, include=os.path.join(CPP_DIR, "include"),
            tests=TESTS_DIR, out=BUILD_DIR, exe=EXE, files=" ".join('"{}"'.format(path) for path in sources())),
    ]
    with open(script, "w", encoding="utf-8") as handle:
        handle.write("\r\n".join(lines) + "\r\n")
    return subprocess.run(["cmd", "/c", script], capture_output=True, text=True)


class CoreCpp(unittest.TestCase):

    def test_core_tests_pass(self):
        if not is_current():
            result = build()
            self.assertEqual(result.returncode, 0, "building the core tests failed:\n" + result.stdout[-4000:] + result.stderr[-2000:])
        run = subprocess.run([EXE], capture_output=True, text=True)
        lines = run.stdout.splitlines()
        failures = [line for line in lines if line.startswith("FAIL")]
        self.assertEqual(failures, [], "\n".join(failures))
        total = [line for line in lines if line.startswith("CORE TOTAL")]
        self.assertEqual(len(total), 1, run.stdout[-2000:])
        self.assertEqual(run.returncode, 0)


if __name__ == "__main__":
    unittest.main()
