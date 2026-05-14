/*
 * @file test_vm_flags.c
 * @brief Unit tests for the Milestone 2 MASM32 CPU flag model.
 *
 * These tests cover named EFLAGS helpers and arithmetic flag updates without
 * introducing instruction execution, parser behavior, or VM memory behavior.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../../src/core/vm_cpu.h"

/// Records a flag test failure with file-local context.
///
/// @param message Human-readable failure description.
/// @return Always returns 1 so callers can accumulate failures.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that a flag has the expected boolean value.
///
/// @param cpu CPU state to inspect.
/// @param flag Named flag identifier to read.
/// @param expected Expected flag value.
/// @param message Failure message emitted when the flag differs.
/// @return Zero on success, otherwise one failure.
static int expect_flag_value(const VmCpu *cpu, VmFlag flag, bool expected, const char *message) {
    bool actual = false;

    if (!vm_cpu_read_flag(cpu, flag, &actual)) {
        return record_failure(message);
    }

    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %s, got %s)\n", message, expected ? "set" : "clear", actual ? "set" : "clear");
        return 1;
    }

    return 0;
}

/// Verifies that a register has the expected value.
///
/// @param cpu CPU state to inspect.
/// @param reg Register identifier to read.
/// @param expected Expected register value.
/// @param message Failure message emitted when the value differs.
/// @return Zero on success, otherwise one failure.
static int expect_register_value(const VmCpu *cpu, VmRegister reg, uint32_t expected, const char *message) {
    uint32_t actual = 0U;

    if (!vm_cpu_read_register(cpu, reg, &actual)) {
        return record_failure(message);
    }

    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected 0x%08X, got 0x%08X)\n", message, expected, actual);
        return 1;
    }

    return 0;
}

/// Verifies the four arithmetic flags at once.
///
/// @param cpu CPU state to inspect.
/// @param cf Expected carry flag value.
/// @param zf Expected zero flag value.
/// @param sf Expected sign flag value.
/// @param of Expected overflow flag value.
/// @param context Human-readable context for failure messages.
/// @return Zero on success, otherwise a positive failure count.
static int expect_arithmetic_flags(const VmCpu *cpu, bool cf, bool zf, bool sf, bool of, const char *context) {
    int failures = 0;

    failures += expect_flag_value(cpu, VM_FLAG_CF, cf, context);
    failures += expect_flag_value(cpu, VM_FLAG_ZF, zf, context);
    failures += expect_flag_value(cpu, VM_FLAG_SF, sf, context);
    failures += expect_flag_value(cpu, VM_FLAG_OF, of, context);

    return failures;
}

/// Verifies success-path named flag set, clear, write, and read behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_flag_set_clear_write_and_read(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += expect_arithmetic_flags(&cpu, false, false, false, false, "initialized flags should be clear");

    failures += !vm_cpu_set_flag(&cpu, VM_FLAG_CF) ? record_failure("set CF should succeed") : 0;
    failures += expect_flag_value(&cpu, VM_FLAG_CF, true, "CF should be set");
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0x00000001U, "setting CF should set bit 0");

    failures += !vm_cpu_set_flag(&cpu, VM_FLAG_ZF) ? record_failure("set ZF should succeed") : 0;
    failures += expect_flag_value(&cpu, VM_FLAG_ZF, true, "ZF should be set");
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0x00000041U, "setting ZF should set bit 6 without clearing CF");

    failures += !vm_cpu_clear_flag(&cpu, VM_FLAG_CF) ? record_failure("clear CF should succeed") : 0;
    failures += expect_flag_value(&cpu, VM_FLAG_CF, false, "CF should be clear");
    failures += expect_flag_value(&cpu, VM_FLAG_ZF, true, "clearing CF should preserve ZF");
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0x00000040U, "clearing CF should preserve unrelated flag bits");

    failures += !vm_cpu_write_flag(&cpu, VM_FLAG_SF, true) ? record_failure("write SF true should succeed") : 0;
    failures += !vm_cpu_write_flag(&cpu, VM_FLAG_OF, true) ? record_failure("write OF true should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0x000008C0U, "writing SF and OF should set bits 7 and 11");

    failures += !vm_cpu_write_flag(&cpu, VM_FLAG_ZF, false) ? record_failure("write ZF false should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0x00000880U, "writing ZF false should only clear bit 6");

    return failures;
}

/// Verifies that flag helpers preserve unrelated EFLAGS bits.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_flag_helpers_preserve_unrelated_eflags_bits(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EFLAGS, 0xA5A50000U) ? record_failure("write baseline EFLAGS should succeed") : 0;

    failures += !vm_cpu_set_flag(&cpu, VM_FLAG_OF) ? record_failure("set OF should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0xA5A50800U, "set OF should preserve non-flag bits");

    failures += !vm_cpu_clear_flag(&cpu, VM_FLAG_OF) ? record_failure("clear OF should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0xA5A50000U, "clear OF should preserve non-flag bits");

    return failures;
}

/// Verifies invalid and NULL argument handling for named flag helpers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_flag_invalid_input_handling(void) {
    int failures = 0;
    VmCpu cpu;
    bool flag_value = true;

    vm_cpu_init(&cpu);
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EFLAGS, 0x12345678U) ? record_failure("write baseline EFLAGS") : 0;

    if (vm_cpu_read_flag(NULL, VM_FLAG_CF, &flag_value)) {
        failures += record_failure("read flag should reject NULL CPU pointer");
    }

    if (vm_cpu_read_flag(&cpu, VM_FLAG_CF, NULL)) {
        failures += record_failure("read flag should reject NULL output pointer");
    }

    flag_value = true;
    if (vm_cpu_read_flag(&cpu, (VmFlag)-1, &flag_value)) {
        failures += record_failure("read flag should reject invalid flag");
    }

    if (!flag_value) {
        failures += record_failure("invalid flag read should not mutate output value");
    }

    if (vm_cpu_set_flag(NULL, VM_FLAG_CF)) {
        failures += record_failure("set flag should reject NULL CPU pointer");
    }

    if (vm_cpu_set_flag(&cpu, (VmFlag)VM_FLAG_COUNT)) {
        failures += record_failure("set flag should reject out-of-range flag");
    }

    if (vm_cpu_clear_flag(NULL, VM_FLAG_CF)) {
        failures += record_failure("clear flag should reject NULL CPU pointer");
    }

    if (vm_cpu_clear_flag(&cpu, (VmFlag)-1)) {
        failures += record_failure("clear flag should reject invalid flag");
    }

    if (vm_cpu_write_flag(NULL, VM_FLAG_CF, true)) {
        failures += record_failure("write flag should reject NULL CPU pointer");
    }

    if (vm_cpu_write_flag(&cpu, (VmFlag)-1, true)) {
        failures += record_failure("write flag should reject invalid flag");
    }

    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0x12345678U, "invalid flag operations should not mutate EFLAGS");

    return failures;
}

/// Verifies 32-bit addition flag edge cases required by Milestone 2.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_add_flags_required_32_bit_edges(void) {
    int failures = 0;
    VmCpu cpu;
    uint32_t result = 0xCAFEBABEU;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_update_add_flags(&cpu, 0xFFFFFFFFU, 1U, 32U, &result) ? record_failure("FFFFFFFF + 1 should update flags") : 0;
    if (result != 0x00000000U) {
        failures += record_failure("FFFFFFFF + 1 should produce zero result");
    }
    failures += expect_arithmetic_flags(&cpu, true, true, false, false, "FFFFFFFF + 1 flags");

    failures += !vm_cpu_update_add_flags(&cpu, 0x7FFFFFFFU, 1U, 32U, &result) ? record_failure("7FFFFFFF + 1 should update flags") : 0;
    if (result != 0x80000000U) {
        failures += record_failure("7FFFFFFF + 1 should produce 80000000h result");
    }
    failures += expect_arithmetic_flags(&cpu, false, false, true, true, "7FFFFFFF + 1 flags");

    failures += !vm_cpu_update_add_flags(&cpu, 1U, 2U, 32U, &result) ? record_failure("1 + 2 should update flags") : 0;
    if (result != 3U) {
        failures += record_failure("1 + 2 should produce 3");
    }
    failures += expect_arithmetic_flags(&cpu, false, false, false, false, "1 + 2 flags");

    return failures;
}

/// Verifies 32-bit subtraction flag edge cases required by Milestone 2.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sub_flags_required_32_bit_edges(void) {
    int failures = 0;
    VmCpu cpu;
    uint32_t result = 0U;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_update_sub_flags(&cpu, 0U, 1U, 32U, &result) ? record_failure("0 - 1 should update flags") : 0;
    if (result != 0xFFFFFFFFU) {
        failures += record_failure("0 - 1 should produce FFFFFFFFh result");
    }
    failures += expect_arithmetic_flags(&cpu, true, false, true, false, "0 - 1 flags");

    failures += !vm_cpu_update_sub_flags(&cpu, 5U, 5U, 32U, &result) ? record_failure("5 - 5 should update flags") : 0;
    if (result != 0U) {
        failures += record_failure("5 - 5 should produce zero result");
    }
    failures += expect_arithmetic_flags(&cpu, false, true, false, false, "5 - 5 flags");

    failures += !vm_cpu_update_sub_flags(&cpu, 0x80000000U, 1U, 32U, &result) ? record_failure("80000000h - 1 should update flags") : 0;
    if (result != 0x7FFFFFFFU) {
        failures += record_failure("80000000h - 1 should produce 7FFFFFFFh result");
    }
    failures += expect_arithmetic_flags(&cpu, false, false, false, true, "80000000h - 1 flags");

    return failures;
}

/// Verifies arithmetic helpers for narrower operand widths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_arithmetic_flags_for_8_and_16_bit_widths(void) {
    int failures = 0;
    VmCpu cpu;
    uint32_t result = 0U;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_update_add_flags(&cpu, 0xFFU, 1U, 8U, &result) ? record_failure("8-bit FFh + 1 should update flags") : 0;
    if (result != 0U) {
        failures += record_failure("8-bit FFh + 1 should wrap to 0");
    }
    failures += expect_arithmetic_flags(&cpu, true, true, false, false, "8-bit FFh + 1 flags");

    failures += !vm_cpu_update_add_flags(&cpu, 0x7FU, 1U, 8U, &result) ? record_failure("8-bit 7Fh + 1 should update flags") : 0;
    if (result != 0x80U) {
        failures += record_failure("8-bit 7Fh + 1 should produce 80h");
    }
    failures += expect_arithmetic_flags(&cpu, false, false, true, true, "8-bit 7Fh + 1 flags");

    failures += !vm_cpu_update_sub_flags(&cpu, 0x0000U, 1U, 16U, &result) ? record_failure("16-bit 0 - 1 should update flags") : 0;
    if (result != 0xFFFFU) {
        failures += record_failure("16-bit 0 - 1 should produce FFFFh");
    }
    failures += expect_arithmetic_flags(&cpu, true, false, true, false, "16-bit 0 - 1 flags");

    failures += !vm_cpu_update_sub_flags(&cpu, 0x8000U, 1U, 16U, &result) ? record_failure("16-bit 8000h - 1 should update flags") : 0;
    if (result != 0x7FFFU) {
        failures += record_failure("16-bit 8000h - 1 should produce 7FFFh");
    }
    failures += expect_arithmetic_flags(&cpu, false, false, false, true, "16-bit 8000h - 1 flags");

    return failures;
}

/// Verifies comparison helper behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_cmp_flags_match_subtraction_without_result_output(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_update_cmp_flags(&cpu, 5U, 5U, 32U) ? record_failure("cmp 5, 5 should update flags") : 0;
    failures += expect_arithmetic_flags(&cpu, false, true, false, false, "cmp 5, 5 flags");

    failures += !vm_cpu_update_cmp_flags(&cpu, 3U, 5U, 8U) ? record_failure("8-bit cmp 3, 5 should update flags") : 0;
    failures += expect_arithmetic_flags(&cpu, true, false, true, false, "8-bit cmp 3, 5 flags");

    failures += !vm_cpu_update_cmp_flags(&cpu, 0x80000000U, 1U, 32U) ? record_failure("cmp 80000000h, 1 should update flags") : 0;
    failures += expect_arithmetic_flags(&cpu, false, false, false, true, "cmp 80000000h, 1 flags");

    return failures;
}

/// Verifies invalid arithmetic helper inputs do not mutate EFLAGS or output.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_arithmetic_invalid_input_handling(void) {
    int failures = 0;
    VmCpu cpu;
    uint32_t result = 0xCAFEBABEU;

    vm_cpu_init(&cpu);
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EFLAGS, 0x12345678U) ? record_failure("write baseline EFLAGS") : 0;

    if (vm_cpu_update_add_flags(NULL, 1U, 2U, 32U, &result)) {
        failures += record_failure("add flags should reject NULL CPU pointer");
    }

    if (vm_cpu_update_add_flags(&cpu, 1U, 2U, 0U, &result)) {
        failures += record_failure("add flags should reject zero width");
    }

    if (result != 0xCAFEBABEU) {
        failures += record_failure("invalid add width should not mutate output result");
    }

    if (vm_cpu_update_sub_flags(&cpu, 1U, 2U, 7U, &result)) {
        failures += record_failure("sub flags should reject unsupported width");
    }

    if (vm_cpu_update_cmp_flags(&cpu, 1U, 2U, 64U)) {
        failures += record_failure("cmp flags should reject unsupported width");
    }

    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0x12345678U, "invalid arithmetic helpers should not mutate EFLAGS");

    if (!vm_cpu_update_add_flags(&cpu, 1U, 1U, 32U, NULL)) {
        failures += record_failure("add flags should allow NULL output result");
    }
    failures += expect_arithmetic_flags(&cpu, false, false, false, false, "1 + 1 with NULL output flags");

    return failures;
}

/// Runs all Milestone 2 flag model tests.
///
/// @return Zero on success, non-zero when any test fails.
int main(void) {
    int failures = 0;

    failures += test_flag_set_clear_write_and_read();
    failures += test_flag_helpers_preserve_unrelated_eflags_bits();
    failures += test_flag_invalid_input_handling();
    failures += test_add_flags_required_32_bit_edges();
    failures += test_sub_flags_required_32_bit_edges();
    failures += test_arithmetic_flags_for_8_and_16_bit_widths();
    failures += test_cmp_flags_match_subtraction_without_result_output();
    failures += test_arithmetic_invalid_input_handling();

    if (failures != 0) {
        fprintf(stderr, "%d Milestone 2 flag model test failure(s).\n", failures);
        return 1;
    }

    puts("Milestone 2 flag model tests passed.");
    return 0;
}
