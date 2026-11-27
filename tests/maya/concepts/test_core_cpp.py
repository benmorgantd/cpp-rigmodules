"""core/ tests: the Maya-free C++ in cpp/core, built into one executable and run.

core/ has no Maya headers, so its tests are plain C++ in tests/core (CoreTests.h is the harness). This file builds them
with the Visual Studio compiler and runs the program, which prints one PASS or FAIL line per test. The objects and the program are
cached in cpp/x64/CoreTests: only a source newer than its object is compiled again (every source, after a header
change), then everything is linked. This test imports no maya module and
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


def headers():
    return glob.glob(os.path.join(CORE_DIR, "*.h")) + glob.glob(os.path.join(TESTS_DIR, "*.h"))


def object_path(source):
    """Each source's object file. Core and test sources go in their own folders, so a name in both never clashes."""
    folder = "tests" if os.path.dirname(source) == TESTS_DIR else "core"
    return os.path.join(BUILD_DIR, folder, os.path.splitext(os.path.basename(source))[0] + ".obj")


def stale_sources():
    """Sources whose object is missing or older than the source. A changed header makes every source stale, because
    nothing here tracks which source includes it."""
    newest_header = max(os.path.getmtime(path) for path in headers())
    stale = []
    for source in sources():
        obj = object_path(source)
        if not os.path.exists(obj) or os.path.getmtime(obj) < max(os.path.getmtime(source), newest_header):
            stale.append(source)
    return stale


def is_current():
    if not os.path.exists(EXE) or stale_sources():
        return False
    built = os.path.getmtime(EXE)
    return all(os.path.getmtime(object_path(source)) <= built for source in sources())


def vcvars():
    installation = subprocess.run([VSWHERE, "-latest", "-products", "*", "-property", "installationPath"],
                                  capture_output=True, text=True, check=True).stdout.strip()
    return os.path.join(installation, "VC", "Auxiliary", "Build", "vcvars64.bat")


def build():
    """Compiles only the stale sources (in parallel, /MP), then links every object into the program."""
    lines = [
        "@echo off",
        'set "PATH=%PATH%;{}"'.format(os.path.dirname(VSWHERE)),
        'call "{}" >nul'.format(vcvars()),
    ]
    stale = stale_sources()
    for folder in ("core", "tests"):
        os.makedirs(os.path.join(BUILD_DIR, folder), exist_ok=True)
        files = [path for path in stale if os.path.dirname(object_path(path)).endswith(folder)]
        if files:
            lines.append('cl /nologo /c /std:c++20 /EHsc /MP /O1 /W3 /FI "{pch}" /I "{cpp}" /I "{include}" /I "{tests}" /Fo"{out}\\\\" {files} || exit /b 1'.format(
                pch=os.path.join(CORE_DIR, "CorePch.h"), cpp=CPP_DIR, include=os.path.join(CPP_DIR, "include"),
                tests=TESTS_DIR, out=os.path.join(BUILD_DIR, folder), files=" ".join('"{}"'.format(path) for path in files)))
    lines.append('cl /nologo /Fe"{exe}" {objects}'.format(
        exe=EXE, objects=" ".join('"{}"'.format(object_path(path)) for path in sources())))
    script = os.path.join(BUILD_DIR, "build.cmd")
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
