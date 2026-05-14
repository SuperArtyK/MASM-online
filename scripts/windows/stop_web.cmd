@echo off
REM /*
REM  * @file stop_web.cmd
REM  * @brief Stops the tracked local Python web server used by Visual Studio tools.
REM  *
REM  * The script reads the PID recorded by serve_web.cmd and terminates that
REM  * process. If the PID is stale or missing, the script reports a clear status
REM  * without affecting unrelated Python processes.
REM  */
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "ROOT_DIR=%%~fI"

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%stop_web.ps1" -RootDir "%ROOT_DIR%"
exit /b %ERRORLEVEL%
