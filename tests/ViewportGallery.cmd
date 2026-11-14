@echo off
rem Manual VP2 check of every rigControlNode shape (not part of the suite, see tests\viewport\gallery.py).
rem Starts Maya with a window, playblasts the shape gallery, quits, and opens the image folder.
rem   ViewportGallery.cmd                           uses cpp\x64\Release\RigModules.mll
rem   ViewportGallery.cmd path\to\RigModules.mll    tests another build
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

rem The launch itself (plugin copy, environment, the maya.exe command line, timeout) is mayalaunch's.
"%MAYAPY%" "%~dp0viewport\run_gallery.py" %*
set RESULT=%ERRORLEVEL%

:done
echo %CMDCMDLINE% | find /i "%~nx0" >nul && pause
exit /b %RESULT%
