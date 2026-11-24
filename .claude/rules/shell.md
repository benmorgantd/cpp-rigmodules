# Shell rules: commands that kept failing

Mined from 163 sessions (about 6,500 Bash and PowerShell calls, 2026-11-07 to 2026-11-22). Each rule names the failure it prevents and how often it happened. Every failed call costs its whole command twice: once sent, once resent.

## Writing files and editing

- **Never put more than about 7 KB in a heredoc.** Commands under 7 KB with a heredoc ran about 1,550 times with no failure. Failures start near 7.2 KB, and over 8 KB 48 of 50 failed with `unexpected EOF while looking for matching` a quote, whatever the quoting. Write the file with the Write tool (into the scratchpad for a throwaway script), then run it.
- **Edit files with the Edit tool, not a Python heredoc.** Edit keeps CRLF on source files (checked: a multi-line match and its replacement stay CRLF), so a script that converts `\n` to `\r\n` is never needed. The Python route caused 70 assertion misses (an `old` string that did not match) and 81 invalid-escape warnings across the 1,143 uses. Use the Edit tool for several sites in one file with several calls.
- **`sed -i` strips the CR from every line it touches** (GNU sed 4.9 here, checked). Never use it on a CRLF file.
- **If a Python script is unavoidable** (a bulk mechanical rewrite), put it in a file with the Write tool and:
  - Write any text with a backslash as a raw string. `"tests\core\run.bat"` is silently `tests` + CR + `ore...`, which corrupted `CLAUDE.md` once. A `SyntaxWarning: invalid escape sequence` in the output means a file was written wrong: stop and check it.
  - Open files with `open(p, encoding="utf-8", newline="")`. The default is cp1252 on Windows and failed 4 times on a UTF-8 file.

## PowerShell

- **Use absolute paths.** The working directory persists between calls, so a relative path breaks after an earlier `Set-Location`: `MSB1009 Project file does not exist` (4 times), and `tests\tests\run_tests.py` (a doubled folder). The build command in `CLAUDE.md` is relative to the repo root: prefix it with `Set-Location <repo root>;` or use full paths.
- **No `2>&1` on a native exe** (mayapy, MSBuild). It wraps each stderr line in `NativeCommandError` plus an `At line:1 char:...` trace and sets a failing exit code (42 noisy results of 539 uses). To run mayapy and trim its output, use the Bash tool: `"/c/Program Files/Autodesk/Maya2026/bin/mayapy.exe" script.py 2>&1 | tail -40`.
- **Call an exe from a quoted path with `&`** (`& "C:\...\MSBuild.exe" ...`). Without it PowerShell reports `Unexpected token` through `Invoke-Expression`.
- **Cmdlet traps seen:** `Select-String -NotMatch` (there is no `-NotMatchPattern`); `-replace` takes exactly two operands, so a pattern with a comma needs the pair in parentheses.
- **Long output:** end every build or test command with `| Select-Object -Last 40`.

## Bash (Git Bash)

- **A `.bat` or `.exe` in the current folder needs `./`** (`./build.bat`, `./probe.exe`). Bash does not search the current folder (`build.bat is not recognized`, `probe.exe is not recognized`).
- **A throwaway C++ probe built with `vcvars64.bat` needs `vswhere` on PATH.** It failed with `'vswhere.exe' is not recognized` 4 times. Put `set "PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer"` before the `call ...vcvars64.bat` line of the `.cmd`.
- **Do not `sleep` and then poll.** The harness blocks it (8 times). Use `run_in_background`, or the Monitor tool with an until-loop.
- **Scratch files go in the session scratchpad directory**, not `/tmp` and not the repo.

## Output that wastes tokens

Filter these out of mayapy results (`grep -v -e internalTimeUnitStringToDisplayFPSString -e '^\[bmaya\]'`). The first repeats once per scene open and appeared in 29 results, and the startup lines come from the author's `userSetup.py`:

- `Error: line 1: Cannot find procedure "internalTimeUnitStringToDisplayFPSString"`: harmless in standalone Maya.
- `Press any key to continue . . .` at the end of `RigTests.cmd` run through `cmd /c`: harmless, stdin is closed.

## MEL strings

`maya.exe -command` and `mel.eval` strings are covered in `.claude/agents/maya-runner.md` (use `mayalaunch.run_gui`, pass paths through environment variables, no backslashes or inner double quotes). No MEL failure has happened since that was written.

## Enforcement

`.claude/hooks/check_shell.py`, registered in `.claude/settings.json`, blocks a Bash call with a heredoc over 7,000 characters, a Python heredoc that converts `\n` to `\r\n` or has an invalid escape, `sed -i` on a CRLF file, and `2>&1` on mayapy or MSBuild in PowerShell. The message names the fix. Run `python .claude/hooks/check_shell.py` with hook JSON on stdin to test a change to it.
