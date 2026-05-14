/*
 * @file masm32_sim_api.h
 * @brief Minimal public C API for the MASM32 educational simulator core.
 *
 * This header contains only Phase 0 scaffolding needed to prove that the C
 * core can be compiled, tested, and exported to WebAssembly. Later milestones
 * should extend the API through focused modules instead of adding parser, VM,
 * or debugger behavior here.
 */

#ifndef MASM32_SIM_API_H
#define MASM32_SIM_API_H

#include <stddef.h>


/// Describes the status returned by Phase 0 C API helpers.
typedef enum MasmSimStatus {
    /// The operation completed successfully.
    MASM_SIM_STATUS_OK = 0,
    /// A required pointer argument was NULL.
    MASM_SIM_STATUS_NULL_ARGUMENT = 1,
    /// A caller-provided buffer was too small and the value was truncated.
    MASM_SIM_STATUS_TRUNCATED = 2
} MasmSimStatus;

/// Returns a deterministic sentinel used by Phase 0 tests and Wasm export checks.
///
/// @return The fixed value 32, representing the MASM32-oriented simulator target.
int masm_sim_milestone_zero_value(void);

/// Copies the current simulator core version string into a caller-provided buffer.
///
/// The output is always NUL-terminated when @p out_buffer is non-NULL and
/// @p out_buffer_size is greater than zero. A truncated copy is reported as
/// MASM_SIM_STATUS_TRUNCATED so tests can distinguish success from edge cases.
///
/// @param out_buffer Destination buffer that receives the version string.
/// @param out_buffer_size Size of @p out_buffer in bytes, including room for NUL.
/// @return MASM_SIM_STATUS_OK on full copy, or a status describing the failure.
MasmSimStatus masm_sim_copy_version(char *out_buffer, size_t out_buffer_size);


#endif
