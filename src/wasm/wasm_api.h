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

/// Selects optional memory validation behavior for source-run helpers.
typedef enum Masm32SimWasmMemoryValidationMode {
    /// Validate memory using the default region and permission checks only.
    MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY = 0,
    /// Emit non-fatal warnings for accesses that escape declared data objects.
    MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS,
    /// Stop execution when an access escapes declared data-object bounds.
    MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT,
    /// Emit non-fatal warnings when reads use uninitialized-origin bytes.
    MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS,
    /// Stop execution before reads that would use uninitialized-origin bytes.
    MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
} Masm32SimWasmMemoryValidationMode;

/// Selects handling for shift counts whose modeled flags are architecturally undefined.
typedef enum Masm32SimWasmShiftValidationMode {
    /// Emit non-fatal warnings and preserve undefined modeled flags deterministically.
    MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS = 0,
    /// Stop execution before mutating state when a shift would make modeled flags undefined.
    MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT
} Masm32SimWasmShiftValidationMode;

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

/// Parses and executes source using seeded or fresh randomized layout placement.
///
/// This test/configuration-facing helper keeps the normal browser export in
/// fixed-layout mode while allowing native tests to verify randomized region
/// bases and relocation. @p randomized_mode must be
/// VM_LAYOUT_MODE_SEEDED_RANDOMIZED or VM_LAYOUT_MODE_FRESH_RANDOMIZED.
/// Passing NULL for @p base_policy uses @ref vm_layout_default_policy. The
/// returned JSON includes layout metadata for randomized runs.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param randomized_mode Randomized layout mode to select.
/// @param base_policy Optional policy supplying limits/defaults/range/seed.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_randomized_layout_policy(const char *source, VmLayoutMode randomized_mode, const VmLayoutPolicy *base_policy);

/// Parses and executes source with an explicit memory validation mode.
///
/// This test/configuration-facing helper keeps the browser default in
/// region-only validation while allowing native tests to enable allocated-object
/// warnings from Phase 37, allocated-object strict validation from Phase 38,
/// or uninitialized-read warning/strict validation from Phase 40.
/// The returned pointer refers to the same internal static buffer as
/// @ref masm32_sim_wasm_run_source_json.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param validation_mode Memory validation behavior to apply during execution.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_memory_validation_mode(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode
);

/// Parses and executes source with explicit shift-undefined-flag validation.
///
/// The normal browser export uses warning mode. This test/configuration-facing
/// helper allows native tests to verify strict Phase 46 behavior without adding
/// a browser UI setting in the same milestone.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param shift_mode Shift undefined modeled-flag validation behavior.
/// @return Pointer to the same static JSON buffer used by other source-run helpers.
const char *masm32_sim_wasm_run_source_json_with_shift_validation_mode(
    const char *source,
    Masm32SimWasmShiftValidationMode shift_mode
);

/// Parses and executes source with memory validation and test-only initialization metadata.
///
/// This helper is for native tests that need to verify how warning or strict
/// validation interacts with Phase 39 write tracking. The normal browser export
/// intentionally omits the metadata and keeps region-only validation.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param validation_mode Memory validation behavior to apply during execution.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode
);

/// Parses and executes source while appending test-only uninitialized-origin metadata.
///
/// The normal browser source-run export intentionally omits this metadata. This
/// helper is for native tests that need to inspect Phase 39 write tracking
/// without adding uninitialized-read warnings, strict errors, or UI output.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_uninitialized_metadata(const char *source);

/// Copies the simulator version string through the WebAssembly export boundary.
///
/// @param out_buffer Destination buffer owned by the caller.
/// @param out_buffer_size Size of @p out_buffer in bytes.
/// @return Zero for success, or a non-zero C API status code.
int masm32_sim_wasm_copy_version(char *out_buffer, unsigned long out_buffer_size);


#endif
