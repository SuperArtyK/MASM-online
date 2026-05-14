@echo off
REM /*
REM  * @file serve_web.cmd
REM  * @brief Runs the local Python web server used by Visual Studio tools.
REM  *
REM  * The server is run as a tracked foreground process so Visual Studio can show
REM  * the live http.server log in the Output window. Use stop_web.cmd from a
REM  * second tool command to stop the recorded server process.
REM  */
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "ROOT_DIR=%%~fI"
set "PORT=%~1"
if "%PORT%"=="" set "PORT=8000"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%serve_web.ps1" -RootDir "%ROOT_DIR%" -Port %PORT%
exit /b %ERRORLEVEL%
