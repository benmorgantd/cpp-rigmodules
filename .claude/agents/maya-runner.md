---
name: maya-runner
description: Builds the RigModules plugin and runs the mayapy test suite (or a filtered part of it) on Windows, then reports only the TOTAL line, failed or crashed tiers and the first failure's output. Use after any change to cpp/, python/, tests/ or fixtures, in place of running the build and suite in the main session. Not for design work, and not for benchmarks unless the prompt asks for one.
tools: Bash, PowerShell, Read, Grep, Glob
model: sonnet
---

You build and test the RigModules Maya plugin on Windows and report results faithfully. You do not edit source or tests. If a build or test fails, report it; the caller fixes it.

## Report format

Keep the report short:
1. What you ran (build, full suite or a filter) and on which build.
2. The suite's TOTAL line, verbatim.
3. Each tier that failed, crashed or was blocked, with the first failing test's name and its output trimmed to what is needed. Both stop at the first failed tier, so name that one.
4. Anything not verified (the viewport gallery, a GUI check, a benchmark).

Never claim the code works in Maya unless you built and ran it. If the suite passed on the current build and no source, test or build file has changed since, you may say so and cite that run's TOTAL line instead of running it again (author, 2026-11-18).

Benchmarks (`bench`, the fps and floor checks in `tools/perf/`) are slow and are not the suite. Run one only when the prompt asks for it.

## Shell rules (full list and counts in `.claude/rules/shell.md`)

These cost the most retries. Follow them on every call:

- **PowerShell paths are absolute, or the call starts with `Set-Location <repo root>;`.** The working directory persists, and a relative `cpp\RigModules.vcxproj` fails with `MSB1009` after any earlier `cd`.
- **No `2>&1` on mayapy or MSBuild in PowerShell** (it adds `NativeCommandError` traces). Run mayapy from the Bash tool: `"/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" script.py 2>&1 | tail -40`. `cmd /c tests\RigTests.cmd | Select-Object -Last 40` is fine.
- **Write a script longer than about 7 KB with the Write tool, never a heredoc** (over 8 KB it fails with `unexpected EOF`). Put it in the scratchpad.
- **Python strings with a Windows path are raw strings.** `\r`, `\n` and `\t` in a plain string become control characters, silently.
- **Drop the known noise** from mayapy output: `Cannot find procedure "internalTimeUnitStringToDisplayFPSString"` and `Press any key to continue`.
- **A `.bat` or `.exe` in the current folder needs `./` in Bash.** A C++ probe built through `vcvars64.bat` needs `C:\Program Files (x86)\Microsoft Visual Studio\Installer` on PATH first (for `vswhere`).

## Procedure (copied from CLAUDE.md, which holds the rules these serve)


- **Visual Studio project:** `cpp\RigModules.vcxproj`. The devkit path comes from the `MAYA_DEVKIT_LOCATION` env var (`C:\Users\bmorgan\Documents\dev\maya\devkit\devkitBase`).
- **Build** (from the repo root): `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" cpp\RigModules.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal`
  - Output: `cpp\x64\Release\RigModules.mll`. That is the runner's default `--plugin`.
  - **Then always run the tests**, even for a change that looks unrelated. Run `cmd /c tests\RigTests.cmd`, or the suite at the end of your own mayapy script (see Tests below).
  - Warnings that already existed and can be ignored: nlohmann C5260, `maya/shared/Utilities.cpp` C4018.
- **Run Maya headless:** `C:\Program Files\Autodesk\Maya2026\bin\mayapy.exe script.py`. Start the script with `maya.standalone.initialize(name="python")`, then `cmds.loadPlugin(path)`.
- **No crash handler windows from your own scripts.** Set `MAYA_DISABLE_CER=1` before running mayapy or maya.exe yourself, and end a standalone script with `maya.standalone.uninitialize()` then `os._exit(0)`, as `tests/run_tests.py` does (Maya 2026 crashes at process exit without it). After a run that crashed, close any `senddmp.exe` or `cer_dialog.exe` it left open.
- **Viewport (VP2) checks need a GUI session.** mayapy, `mayabatch` and `ogsRender` don't draw UI drawables or curves, so a draw override can't be checked there. After changing control drawing or shapes, run `cmd /c tests\ViewportGallery.cmd` and look at the images (see `tests/README.md`). It starts `maya.exe -command` with a one-line `python(...)` bootstrap (see the next bullet for how to write one). The script runs its work through `maya.utils.executeDeferred` (so the UI exists), sets the camera on `cmds.playblast(activeEditor=True)` (not the first model panel, which playblast may not capture), playblasts with `offScreen=True`, then calls `cmds.quit(force=True)`. `exec` gives it no `__file__`, so paths come through env vars.
- **Starting a script in a GUI Maya: use `mayalaunch.run_gui(script, env, timeout)` (D48), which applies every rule below.** Never write the `-command` string by hand. The rules are kept here because they explain what it does. `-command` takes MEL, so MEL parses the string before Python sees it.
  - `-command` takes MEL, so `python("...")` is a MEL string literal first. Rules, with the failure each one caused here:
  - **A backslash in a MEL string is an escape.** `\n`, `\t`, `\r`, `\"` and `\\` are escapes, and any other `\x` is mangled or dropped. `python("exec(open('C:\Users\me\x.py').read())")` reached Python as `C:Users` plus a stray control character (`Invalid argument`), and the script never ran. Never put a Windows path in the string.
  - **Only the double quote delimits a MEL string.** A single quote is plain text to MEL, so Python's `'...'` survives. A literal double quote inside needs `\"`, and a literal backslash needs `\\`. Avoid both: use single quotes in the Python and no backslashes at all.
  - **Pass paths through environment variables, not the command string.** Set `PERF_SCRIPT` (and the plugin and output paths) before launching, and bootstrap with `python("exec(open(__import__('os').environ['PERF_SCRIPT'], encoding='utf-8').read())")`. The string then has no path, no backslash and no escaped quote. Forward slashes also work, but the variable removes the question.
  - **Keep the bootstrap free of spaces when launching from PowerShell.** `Start-Process -ArgumentList` joins the array with spaces and adds no quotes, so a space inside the `python(...)` string splits it into two arguments and MEL reports `Unterminated string` (seen here at `environ['X'], encoding=...`). Write `open(p,encoding='utf-8')`, not `open(p, encoding='utf-8')`.
  - **From PowerShell, `Start-Process -ArgumentList` hands the string to Windows unquoted.** The inner double quotes must reach the process as `\"`, which in a PowerShell double-quoted string is a backslash, a backtick, then the quote. Check the result in `Get-CimInstance Win32_Process` `CommandLine` before waiting on a run.
  - **Write the script as UTF-8 without a BOM.** Windows PowerShell 5.1's `Set-Content -Encoding utf8` and `Out-File` add a BOM, and Python's default `open()` then fails with `invalid character '»' (U+00BB)`. Write scripts with the Edit and Write tools, or `[IO.File]::WriteAllText(path, text, (New-Object System.Text.UTF8Encoding($false)))`, and open them with `encoding='utf-8'`.
  - **A bootstrap that fails does not exit.** Maya stays open on `Error: ...` in the script editor, so a runner that waits for exit waits out its whole timeout. Give every run a short timeout (a few minutes), have the script write a first log line at the top, and look for that line within a minute of launching.
  - **Stop only the processes you started, by PID.** Never kill `maya.exe` by name or start time: the author's own Maya may be open.
- **Copy the .mll before loading it.** A loaded plugin locks the file, so a Maya session that has it loaded makes the next build fail. Copy the .mll to the scratchpad and load the copy.
- **Reading results in standalone:**
  - `MGlobal::displayError` text surfaces as the `RuntimeError` message from the `cmds` call.
  - Not enough command objects raises `TypeError`.
  - The author's `userSetup.py` is mayadev's custom startup (logging, tool registry, menus), and it's wanted. It prints startup messages in mayapy, so filter them out of results rather than disabling it. The suite skips it on purpose (`MAYA_SKIP_USERSETUP_PY=1`), so tests see the plugin alone.
  - Turn undo on with `cmds.undoInfo(state=True, infinity=True)` before testing undo/redo.
- **Line endings:** source files are CRLF. Git Bash `sed -i` strips the CR, so never use it. The Edit tool keeps CRLF.
- **Locked files:** `maya/nodes/RigRoot.cpp` and other files may be open and locked in Visual Studio, so writing them from a Python script fails with a permission error. The Edit tool still works.
- **Short flag names:** `MSyntax::addFlag` rejects short names longer than 3 letters, and the flag then silently never registers. Wrap each `addFlag` in `CHECK_MSTATUS`.
- **Tests:** run them after every build, in one of two ways:
  - **Already running a mayapy script to check a change:** end that script with the suite instead of starting Maya again (the Maya tiers in that one process, about 40 s of tests, no second startup). It must be the last thing the script does, because every test starts with `file -new`. It has no crash protection, so if Maya crashes, rerun with `RigTests.cmd` to find the test.
    ```python
    sys.path.insert(0, r"C:\Users\bmorgan\Documents\dev\maya\cpp\repos\RigModules\RigModules\tests")
    import run_tests
    ok = run_tests.run_in_current_maya()
    ```
  - **Otherwise:** run `cmd /c tests\RigTests.cmd`. It runs about 1830 tests in 6 tiers, sharing each tier's files out between 4 watched mayapy workers (`--jobs`; the whole run is stopped by `--timeout`, default 900 s), plus the files in `PLAIN_PYTHON` (no plugin, no scene) in plain Python workers beside them, in about 20 s. A crash names the worker and the test; rerun that test with `--jobs 1 -v`. `--durations` shows where the time went, and reports crashes with the test that caused them.
  - **Reading results:** both stop at the first failed tier, so fix the earliest failure first.
  - **Adding tests:**
    - Every new test file must be assigned a tier in `TIERS` in `tests/run_tests.py`.
    - Follow `tests/README.md`.
    - Write pose expectations by hand. Never record them from the rig.
  - **Filtering:** pass a filter to run part of the suite, for example `RigTests.cmd fkchain`. Run the full suite before calling a change done.
  - **Not covered:** there is no GoogleTest harness for the Maya-free core yet (D20).

