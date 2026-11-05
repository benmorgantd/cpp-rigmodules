@echo off
rem Runs the RigModules mayapy test suite. Arguments go to run_tests.py, for example:
rem   RigTests.cmd                  every test
rem   RigTests.cmd fkchain naming   only tests whose id contains "fkchain" or "naming"
rem   RigTests.cmd -v               print each test as it runs
rem   RigTests.cmd --update-golden  rewrite golden interface snapshots (review the diff before committing)
setlocal

if defined MAYA_LOCATION (
    set "MAYAPY=%MAYA_LOCATION%\bin\mayapy.exe"
) else (
    set "MAYAPY=C:\Program Files\Autodesk\Maya2026\bin\mayapy.exe"
)
if not exist "%MAYAPY%" (
    echo mayapy not found at "%MAYAPY%". Set MAYA_LOCATION to the Maya install folder.
    set RESULT=2
    goto done
)

rem Keep the user's userSetup.py out of test runs.
set MAYA_SKIP_USERSETUP_PY=1
rem Keep __pycache__ folders out of the repo.
set PYTHONDONTWRITEBYTECODE=1
"%MAYAPY%" "%~dp0run_tests.py" %*
set RESULT=%ERRORLEVEL%

:done
rem Keep the window open when started by double-click, so the results stay readable.
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b %RESULT%
