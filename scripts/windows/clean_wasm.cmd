@echo off
REM /*
REM  * @file clean_wasm.cmd
REM  * @brief Removes generated Phase 0 Emscripten outputs on Windows.
REM  *
REM  * The script deletes only files produced by the Wasm build and leaves source
REM  * files, tests, and documentation untouched.
REM  */
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "ROOT_DIR=%%~fI"
set "OUT_DIR=%ROOT_DIR%\web\dist"

if exist "%OUT_DIR%\masm32_sim_core.js" del /f /q "%OUT_DIR%\masm32_sim_core.js"
if exist "%OUT_DIR%\masm32_sim_core.wasm" del /f /q "%OUT_DIR%\masm32_sim_core.wasm"
if exist "%OUT_DIR%\masm32_sim_core.js.map" del /f /q "%OUT_DIR%\masm32_sim_core.js.map"
if exist "%OUT_DIR%\masm32_sim_core.wasm.map" del /f /q "%OUT_DIR%\masm32_sim_core.wasm.map"

echo Cleaned Phase 0 Wasm outputs from "%OUT_DIR%".
exit /b 0
