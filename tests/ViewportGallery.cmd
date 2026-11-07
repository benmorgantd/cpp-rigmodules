@echo off
rem Manual VP2 check of every rigControlNode shape (not part of the suite, see tests\viewport\gallery.py).
rem Starts Maya with a window, playblasts the shape gallery, quits, and opens the image folder.
rem   ViewportGallery.cmd                           uses cpp\x64\Release\RigModules.mll
rem   ViewportGallery.cmd path\to\RigModules.mll    tests another build
setlocal

if defined MAYA_LOCATION (
    set "MAYA_EXE=%MAYA_LOCATION%\bin\maya.exe"
) else (
    set "MAYA_EXE=C:\Program Files\Autodesk\Maya2026\bin\maya.exe"
)
if not exist "%MAYA_EXE%" (
    echo maya.exe not found at "%MAYA_EXE%". Set MAYA_LOCATION to the Maya install folder.
    set RESULT=2
    goto done
)

set "PLUGIN=%~1"
if "%PLUGIN%"=="" set "PLUGIN=%~dp0..\cpp\x64\Release\RigModules.mll"
if not exist "%PLUGIN%" (
    echo Plugin not found: "%PLUGIN%". Build Release first.
    set RESULT=2
    goto done
)

rem Load a copy, so a Maya that hangs can't lock the build output.
set "WORK=%TEMP%\RigViewportGallery"
if exist "%WORK%" rmdir /s /q "%WORK%"
mkdir "%WORK%\out"
copy /y "%PLUGIN%" "%WORK%\RigModules.mll" >nul

set "RIG_GALLERY_PLUGIN=%WORK%\RigModules.mll"
set "RIG_GALLERY_OUT=%WORK%\out"
set MAYA_SKIP_USERSETUP_PY=1
set MAYA_DISABLE_CER=1
set PYTHONDONTWRITEBYTECODE=1

rem MEL strings need forward slashes.
set "SCRIPT=%~dp0viewport\gallery.py"
set "SCRIPT=%SCRIPT:\=/%"

echo Starting Maya. It opens a window, playblasts, and closes by itself.
start "" /wait "%MAYA_EXE%" -command "python(\"exec(open('%SCRIPT%').read())\")"

type "%WORK%\out\log.txt" 2>nul
findstr /x "done" "%WORK%\out\log.txt" >nul 2>nul
if errorlevel 1 (
    echo Gallery did not finish. See the log above.
    set RESULT=1
    goto done
)
echo Images: %WORK%\out
explorer "%WORK%\out"
set RESULT=0

:done
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b %RESULT%
