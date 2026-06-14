#!/usr/bin/env bash
#
# @file build_wasm.sh
# @brief Builds the implemented C99 simulator core into a browser-loadable WebAssembly module.
#
# The script intentionally compiles C99 files only. It requires Emscripten's emcc
# on PATH and writes the generated module to web/dist. Keep this source list in
# step with implemented simulator milestones.

set -eu

ROOT_DIR="$(cd "$(dirname "${0}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/web/dist"

mkdir -p "${OUT_DIR}"

emcc \
  "${ROOT_DIR}/src/core/masm32_sim_api.c" \
  "${ROOT_DIR}/src/core/vm_cpu.c" \
  "${ROOT_DIR}/src/core/vm_memory.c" \
  "${ROOT_DIR}/src/core/vm_layout.c" \
  "${ROOT_DIR}/src/core/vm_ir.c" \
  "${ROOT_DIR}/src/core/vm_exec.c" \
  "${ROOT_DIR}/src/core/vm_diagnostic_policy.c" \
  "${ROOT_DIR}/src/parser/lexer.c" \
  "${ROOT_DIR}/src/parser/parser.c" \
  "${ROOT_DIR}/src/parser/symbols.c" \
  "${ROOT_DIR}/src/parser/object_map.c" \
  "${ROOT_DIR}/src/wasm/wasm_api.c" \
  -I"${ROOT_DIR}/src/core" \
  -I"${ROOT_DIR}/src/parser" \
  -I"${ROOT_DIR}/src/wasm" \
  -std=c99 \
  -Wall \
  -Wextra \
  -O2 \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sENVIRONMENT=web,worker \
  -sEXPORTED_FUNCTIONS='["_masm32_sim_wasm_test_value","_masm32_sim_wasm_milestone4_hardcoded_result","_masm32_sim_wasm_run_source_json","_masm32_sim_wasm_run_source_json_with_instruction_limit","_masm32_sim_wasm_run_source_json_with_ui_settings","_masm32_sim_wasm_run_source_json_with_ui_and_startup_settings","_masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings","_masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings","_masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings","_masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings","_masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings","_masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings","_masm32_sim_wasm_copy_version"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -o "${OUT_DIR}/masm32_sim_core.js"
