@echo off
setlocal

if not defined KATANE_TOOLCHAIN set "KATANE_TOOLCHAIN=auto"

python "%~dp0build.py" --mode debug --toolchain %KATANE_TOOLCHAIN%
if errorlevel 1 exit /b %ERRORLEVEL%

pushd "%~dp0bin"
if errorlevel 1 exit /b %ERRORLEVEL%

katane.exe %*
set "RUN_EXIT=%ERRORLEVEL%"

popd
exit /b %RUN_EXIT%
