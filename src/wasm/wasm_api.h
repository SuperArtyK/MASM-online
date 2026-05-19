/*
 * @file wasm_api.h
 * @brief WebAssembly export boundary for the MASM32 simulator C core.
 *
 * This boundary exposes deterministic APIs used by the browser worker and
 * command-line tests. Each export remains a narrow milestone slice rather than
 * a broad simulator subsystem.
 */

#ifndef MASM32_SIM_WASM_API_H
#define MASM32_SIM_WASM_API_H

#include "../core/vm_layout.h"

/// Returns the Phase 0 sentinel through the WebAssembly export boundary.
///
/// @return The deterministic value returned by the C core Phase 0 helper.
int masm32_sim_wasm_test_value(void);

/// Runs the Milestone 4 hardcoded IR sample through the WebAssembly export boundary.
///
/// @return Final EAX value on success, or -1 on failure.
int masm32_sim_wasm_milestone4_hardcoded_result(void);

/// Parses and executes a MASM-like source string and returns JSON.
///
/// The returned pointer refers to an internal static buffer that is overwritten
/// by each subsequent call. This is intended for single-request Web Worker use
/// through Emscripten `ccall` with a string return value.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json(const char *source);

/// Parses and executes source using automatic deterministic layout sizing.
///
/// This test/configuration-facing helper keeps the normal browser export in
/// fixed-layout mode while allowing native tests to select automatic
/// sizing and policy-driven stack/heap metadata. Passing NULL for @p base_policy
/// uses @ref vm_layout_default_policy.
/// The returned pointer refers to the same internal static buffer as
/// @ref masm32_sim_wasm_run_source_json.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param base_policy Optional policy supplying automatic layout limits/defaults.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_automatic_layout_policy(const char *source, const VmLayoutPolicy *base_policy);

/// Copies the simulator version string through the WebAssembly export boundary.
///
/// @param out_buffer Destination buffer owned by the caller.
/// @param out_buffer_size Size of @p out_buffer in bytes.
/// @return Zero for success, or a non-zero C API status code.
int masm32_sim_wasm_copy_version(char *out_buffer, unsigned long out_buffer_size);


#endif
