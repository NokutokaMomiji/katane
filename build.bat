@echo off
setlocal

if not defined KATANE_TOOLCHAIN set "KATANE_TOOLCHAIN=auto"

python "%~dp0build.py" --mode release --toolchain %KATANE_TOOLCHAIN% %*
exit /b %ERRORLEVEL%
