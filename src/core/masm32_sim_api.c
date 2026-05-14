/*
 * @file masm32_sim_api.c
 * @brief Minimal Phase 0 implementation of the MASM32 simulator C API.
 *
 * This module deliberately contains only deterministic scaffolding. It does not
 * parse, assemble, execute, or simulate MASM instructions. Those behaviors are
 * reserved for later milestones.
 */

#include "masm32_sim_api.h"

/// Stable version label for the Phase 0 simulator core.
static const char MASM_SIM_VERSION[] = "masm32-sim-phase0";

int masm_sim_milestone_zero_value(void) {
    return 32;
}

MasmSimStatus masm_sim_copy_version(char *out_buffer, size_t out_buffer_size) {
    size_t index = 0;

    if (out_buffer == NULL) {
        return MASM_SIM_STATUS_NULL_ARGUMENT;
    }

    if (out_buffer_size == 0U) {
        return MASM_SIM_STATUS_TRUNCATED;
    }

    while (MASM_SIM_VERSION[index] != '\0' && index + 1U < out_buffer_size) {
        out_buffer[index] = MASM_SIM_VERSION[index];
        index += 1U;
    }

    out_buffer[index] = '\0';

    if (MASM_SIM_VERSION[index] != '\0') {
        return MASM_SIM_STATUS_TRUNCATED;
    }

    return MASM_SIM_STATUS_OK;
}
