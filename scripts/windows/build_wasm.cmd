@echo off
REM /*
REM  * @file build_wasm.cmd
REM  * @brief Builds the implemented C99 simulator core into the browser WebAssembly artifact on Windows.
REM  *
REM  * This script is intended for Visual Studio External Tools, Visual Studio
REM  * Makefile Projects, and standard Windows Command Prompt sessions. It uses
REM  * EMSDK_ROOT when available, then falls back to emcc already being on PATH.
REM  * Keep this C99 source list in step with implemented simulator milestones.
REM  */
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "ROOT_DIR=%%~fI"
set "OUT_DIR=%ROOT_DIR%\web\dist"
set "OUT_JS=%OUT_DIR%\masm32_sim_core.js"

if defined EMSDK_ROOT (
    if exist "%EMSDK_ROOT%\emsdk_env.bat" (
        call "%EMSDK_ROOT%\emsdk_env.bat"
        if errorlevel 1 exit /b 1
    ) else (
        echo ERROR: EMSDK_ROOT is set but emsdk_env.bat was not found: "%EMSDK_ROOT%\emsdk_env.bat"
        exit /b 1
    )
)

where emcc >nul 2>nul
if errorlevel 1 (
    echo ERROR: emcc was not found.
    echo Set EMSDK_ROOT to the emsdk directory or run this from an Emscripten command prompt.
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

emcc ^
  "%ROOT_DIR%\src\core\masm32_sim_api.c" ^
  "%ROOT_DIR%\src\core\vm_cpu.c" ^
  "%ROOT_DIR%\src\core\vm_memory.c" ^
  "%ROOT_DIR%\src\core\vm_layout.c" ^
  "%ROOT_DIR%\src\core\vm_ir.c" ^
  "%ROOT_DIR%\src\core\vm_exec.c" ^
  "%ROOT_DIR%\src\core\vm_diagnostic_policy.c" ^
  "%ROOT_DIR%\src\parser\lexer.c" ^
  "%ROOT_DIR%\src\parser\parser.c" ^
  "%ROOT_DIR%\src\parser\symbols.c" ^
  "%ROOT_DIR%\src\parser\object_map.c" ^
  "%ROOT_DIR%\src\wasm\wasm_api.c" ^
  -I"%ROOT_DIR%\src\core" ^
  -I"%ROOT_DIR%\src\parser" ^
  -I"%ROOT_DIR%\src\wasm" ^
  -std=c99 ^
  -Wall ^
  -Wextra ^
  -O2 ^
  -sMODULARIZE=1 ^
  -sEXPORT_ES6=1 ^
  -sENVIRONMENT=web,worker ^
  -sEXPORTED_FUNCTIONS="['_masm32_sim_wasm_test_value','_masm32_sim_wasm_milestone4_hardcoded_result','_masm32_sim_wasm_run_source_json','_masm32_sim_wasm_run_source_json_with_instruction_limit','_masm32_sim_wasm_run_source_json_with_ui_settings','_masm32_sim_wasm_run_source_json_with_ui_and_startup_settings','_masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings','_masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings','_masm32_sim_wasm_copy_version']" ^
  -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap']" ^
  -o "%OUT_JS%"

if errorlevel 1 exit /b 1

echo Built "%OUT_JS%".
exit /b 0
