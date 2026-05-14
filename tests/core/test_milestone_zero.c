/*
 * @file test_milestone_zero.c
 * @brief Unit tests for the Phase 0 MASM32 simulator C scaffolding.
 *
 * These tests verify deterministic exports and edge-case handling before any VM
 * or parser behavior exists.
 */

#include <stdio.h>
#include <string.h>

#include "../../src/core/masm32_sim_api.h"
#include "../../src/wasm/wasm_api.h"

/// Records a test failure with file-local context.
///
/// @param message Human-readable failure description.
/// @return Always returns 1 so callers can accumulate failures.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies the deterministic Phase 0 sentinel exported by both C API layers.
///
/// @return Zero when the test passes, otherwise a positive failure count.
static int test_milestone_zero_value(void) {
    int failures = 0;

    if (masm_sim_milestone_zero_value() != 32) {
        failures += record_failure("core milestone value should be 32");
    }

    if (masm32_sim_wasm_test_value() != 32) {
        failures += record_failure("wasm milestone value should be 32");
    }

    return failures;
}

/// Verifies successful version copying.
///
/// @return Zero when the test passes, otherwise a positive failure count.
static int test_copy_version_success(void) {
    int failures = 0;
    char buffer[64];
    MasmSimStatus status = masm_sim_copy_version(buffer, sizeof(buffer));

    if (status != MASM_SIM_STATUS_OK) {
        failures += record_failure("version copy should succeed for large buffer");
    }

    if (strcmp(buffer, "masm32-sim-phase0") != 0) {
        failures += record_failure("version copy should produce expected string");
    }

    return failures;
}

/// Verifies null destination error handling.
///
/// @return Zero when the test passes, otherwise a positive failure count.
static int test_copy_version_null_argument(void) {
    if (masm_sim_copy_version(NULL, 16U) != MASM_SIM_STATUS_NULL_ARGUMENT) {
        return record_failure("version copy should reject NULL output buffer");
    }

    return 0;
}

/// Verifies version truncation and NUL termination for a short buffer.
///
/// @return Zero when the test passes, otherwise a positive failure count.
static int test_copy_version_truncated(void) {
    int failures = 0;
    char buffer[5] = {'X', 'X', 'X', 'X', 'X'};
    MasmSimStatus status = masm_sim_copy_version(buffer, sizeof(buffer));

    if (status != MASM_SIM_STATUS_TRUNCATED) {
        failures += record_failure("version copy should report truncation for short buffer");
    }

    if (buffer[4] != '\0') {
        failures += record_failure("truncated version copy should NUL-terminate the buffer");
    }

    if (strncmp(buffer, "masm", 4U) != 0) {
        failures += record_failure("truncated version copy should preserve leading bytes");
    }

    return failures;
}

/// Verifies zero-size buffer edge-case behavior.
///
/// @return Zero when the test passes, otherwise a positive failure count.
static int test_copy_version_zero_size(void) {
    char sentinel = 'Z';

    if (masm_sim_copy_version(&sentinel, 0U) != MASM_SIM_STATUS_TRUNCATED) {
        return record_failure("zero-size version copy should report truncation");
    }

    if (sentinel != 'Z') {
        return record_failure("zero-size version copy should not mutate destination memory");
    }

    return 0;
}

/// Runs all Phase 0 C unit tests.
///
/// @return Zero on success, non-zero when any test fails.
int main(void) {
    int failures = 0;

    failures += test_milestone_zero_value();
    failures += test_copy_version_success();
    failures += test_copy_version_null_argument();
    failures += test_copy_version_truncated();
    failures += test_copy_version_zero_size();

    if (failures != 0) {
        fprintf(stderr, "%d Phase 0 C test failure(s).\n", failures);
        return 1;
    }

    puts("Phase 0 C tests passed.");
    return 0;
}
