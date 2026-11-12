@echo off
rem Builds cpp\RigModules.vcxproj (Release x64). Close any Maya that has the plugin loaded first: it locks the .mll.
set "REPO=%~dp0.."
pushd "%REPO%"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" cpp\RigModules.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
set "RESULT=%ERRORLEVEL%"
popd
echo.
if "%RESULT%"=="0" (echo BUILD SUCCEEDED) else (echo BUILD FAILED, exit code %RESULT%)
pause
exit /b %RESULT%
