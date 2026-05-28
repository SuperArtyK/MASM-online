/*
 * @file test_vm_cpu.c
 * @brief Unit tests for the MASM32 CPU register and modeled-flag metadata model.
 *
 * These tests cover canonical register storage, alias masking, invalid input,
 * flag validity metadata, and metadata helpers without introducing parser or VM
 * execution.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../../src/core/vm_cpu.h"

/// Records a CPU test failure with file-local context.
///
/// @param message Human-readable failure description.
/// @return Always returns 1 so callers can accumulate failures.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies a register read result.
///
/// @param cpu CPU state to inspect.
/// @param reg Register identifier to read.
/// @param expected Expected register value.
/// @param message Failure message emitted when the value differs.
/// @return Zero on success, otherwise one failure.
static int expect_register_value(const VmCpu *cpu, VmRegister reg, uint32_t expected, const char *message) {
    uint32_t actual = UINT32_MAX;

    if (!vm_cpu_read_register(cpu, reg, &actual)) {
        return record_failure(message);
    }

    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: expected 0x%08X, got 0x%08X\n", message, expected, actual);
        return 1;
    }

    return 0;
}

/// Verifies register-family write-tracking state.
///
/// @param cpu CPU state to inspect.
/// @param reg Register or alias whose family should be queried.
/// @param expected Expected write-tracking state.
/// @param message Failure message when state differs.
/// @return Zero on success, otherwise one failure.
static int expect_register_family_written(const VmCpu *cpu, VmRegister reg, bool expected, const char *message) {
    bool actual = false;

    if (!vm_cpu_register_family_was_written(cpu, reg, &actual)) {
        return record_failure("register write-tracking query should succeed");
    }

    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: expected %d, got %d\n", message, expected ? 1 : 0, actual ? 1 : 0);
        return 1;
    }

    return 0;
}

/// Verifies that a register has the expected metadata.
///
/// @param reg Register identifier to inspect.
/// @param expected_width Expected register width in bits.
/// @param expected_name Expected uppercase register name.
/// @return Zero on success, otherwise a positive failure count.
static int expect_register_metadata(VmRegister reg, uint8_t expected_width, const char *expected_name) {
    int failures = 0;
    const char *actual_name = vm_cpu_register_name(reg);

    if (vm_cpu_register_width_bits(reg) != expected_width) {
        failures += record_failure("register width metadata mismatch");
    }

    if (actual_name == NULL || strcmp(actual_name, expected_name) != 0) {
        failures += record_failure("register name metadata mismatch");
    }

    return failures;
}

/// Verifies that one flag has the expected validity metadata.
///
/// @param cpu CPU state to inspect.
/// @param flag Flag identifier to read.
/// @param expected_valid Expected validity state.
/// @param expected_code Expected undefined-origin code, or NULL.
/// @param expected_mnemonic Expected producer mnemonic, or NULL.
/// @param message Failure message when metadata differs.
/// @return Zero on success, otherwise a positive failure count.
static int expect_flag_validity(
    const VmCpu *cpu,
    VmFlag flag,
    int expected_valid,
    const char *expected_code,
    const char *expected_mnemonic,
    const char *message
) {
    VmFlagValidityMetadata metadata;

    if (!vm_cpu_read_flag_validity(cpu, flag, &metadata)) {
        return record_failure("flag validity read should succeed");
    }

    if ((metadata.is_valid ? 1 : 0) != expected_valid) {
        fprintf(stderr, "FAIL: %s validity mismatch (actual=%d expected=%d)\n", message, metadata.is_valid ? 1 : 0, expected_valid);
        return 1;
    }

    if ((metadata.undefined_code == NULL) != (expected_code == NULL) ||
        (metadata.undefined_code != NULL && strcmp(metadata.undefined_code, expected_code) != 0)) {
        fprintf(stderr, "FAIL: %s undefined-code mismatch\n", message);
        return 1;
    }

    if ((metadata.producer_mnemonic == NULL) != (expected_mnemonic == NULL) ||
        (metadata.producer_mnemonic != NULL && strcmp(metadata.producer_mnemonic, expected_mnemonic) != 0)) {
        fprintf(stderr, "FAIL: %s producer-mnemonic mismatch\n", message);
        return 1;
    }

    return 0;
}

/// Verifies zero initialization across canonical and alias registers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_initialization_zeroes_registers(void) {
    int failures = 0;
    VmCpu cpu;
    int reg_index = 0;

    cpu.eax = 0xFFFFFFFFU;
    cpu.ebx = 0xFFFFFFFFU;
    cpu.ecx = 0xFFFFFFFFU;
    cpu.edx = 0xFFFFFFFFU;
    cpu.esi = 0xFFFFFFFFU;
    cpu.edi = 0xFFFFFFFFU;
    cpu.ebp = 0xFFFFFFFFU;
    cpu.esp = 0xFFFFFFFFU;
    cpu.eip = 0xFFFFFFFFU;
    cpu.eflags = 0xFFFFFFFFU;

    vm_cpu_init(&cpu);

    for (reg_index = 0; reg_index < (int)VM_REGISTER_COUNT; reg_index += 1) {
        failures += expect_register_value(&cpu, (VmRegister)reg_index, 0U, "initialized register should read as zero");
    }

    vm_cpu_init(NULL);

    return failures;
}

/// Verifies canonical register writes and reads.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_canonical_register_reads_and_writes(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EAX, 0x11111111U) ? record_failure("write EAX should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EBX, 0x22222222U) ? record_failure("write EBX should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ECX, 0x33333333U) ? record_failure("write ECX should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EDX, 0x44444444U) ? record_failure("write EDX should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ESI, 0x55555555U) ? record_failure("write ESI should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EDI, 0x66666666U) ? record_failure("write EDI should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EBP, 0x77777777U) ? record_failure("write EBP should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ESP, 0x88888888U) ? record_failure("write ESP should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EIP, 0x99999999U) ? record_failure("write EIP should succeed") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EFLAGS, 0xAAAAAAAAU) ? record_failure("write EFLAGS should succeed") : 0;

    failures += expect_register_value(&cpu, VM_REGISTER_EAX, 0x11111111U, "EAX canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_EBX, 0x22222222U, "EBX canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_ECX, 0x33333333U, "ECX canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_EDX, 0x44444444U, "EDX canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_ESI, 0x55555555U, "ESI canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_EDI, 0x66666666U, "EDI canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_EBP, 0x77777777U, "EBP canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_ESP, 0x88888888U, "ESP canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_EIP, 0x99999999U, "EIP canonical read");
    failures += expect_register_value(&cpu, VM_REGISTER_EFLAGS, 0xAAAAAAAAU, "EFLAGS canonical read");

    return failures;
}

/// Verifies alias reads for EAX, EBX, ECX, and EDX families.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_general_purpose_alias_reads(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EAX, 0x12345678U) ? record_failure("write EAX for aliases") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_AX, 0x5678U, "AX alias should read low 16 bits");
    failures += expect_register_value(&cpu, VM_REGISTER_AH, 0x56U, "AH alias should read bits 8-15");
    failures += expect_register_value(&cpu, VM_REGISTER_AL, 0x78U, "AL alias should read low 8 bits");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EBX, 0xA1B2C3D4U) ? record_failure("write EBX for aliases") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_BX, 0xC3D4U, "BX alias should read low 16 bits");
    failures += expect_register_value(&cpu, VM_REGISTER_BH, 0xC3U, "BH alias should read bits 8-15");
    failures += expect_register_value(&cpu, VM_REGISTER_BL, 0xD4U, "BL alias should read low 8 bits");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ECX, 0x01020304U) ? record_failure("write ECX for aliases") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_CX, 0x0304U, "CX alias should read low 16 bits");
    failures += expect_register_value(&cpu, VM_REGISTER_CH, 0x03U, "CH alias should read bits 8-15");
    failures += expect_register_value(&cpu, VM_REGISTER_CL, 0x04U, "CL alias should read low 8 bits");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EDX, 0xFFEEDDCCU) ? record_failure("write EDX for aliases") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_DX, 0xDDCCU, "DX alias should read low 16 bits");
    failures += expect_register_value(&cpu, VM_REGISTER_DH, 0xDDU, "DH alias should read bits 8-15");
    failures += expect_register_value(&cpu, VM_REGISTER_DL, 0xCCU, "DL alias should read low 8 bits");

    return failures;
}

/// Verifies index and pointer low-word alias reads.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_index_and_pointer_alias_reads(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ESI, 0x11112222U) ? record_failure("write ESI for SI") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EDI, 0x33334444U) ? record_failure("write EDI for DI") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EBP, 0x55556666U) ? record_failure("write EBP for BP") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ESP, 0x77778888U) ? record_failure("write ESP for SP") : 0;

    failures += expect_register_value(&cpu, VM_REGISTER_SI, 0x2222U, "SI alias should read low 16 bits");
    failures += expect_register_value(&cpu, VM_REGISTER_DI, 0x4444U, "DI alias should read low 16 bits");
    failures += expect_register_value(&cpu, VM_REGISTER_BP, 0x6666U, "BP alias should read low 16 bits");
    failures += expect_register_value(&cpu, VM_REGISTER_SP, 0x8888U, "SP alias should read low 16 bits");

    return failures;
}

/// Verifies alias writes preserve non-overlapping canonical bits.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_alias_writes_preserve_bits(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EAX, 0x12345678U) ? record_failure("write EAX before AL") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_AL, 0x000001FFU) ? record_failure("write AL should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EAX, 0x123456FFU, "AL write should update only low byte");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_AH, 0x000001EEU) ? record_failure("write AH should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EAX, 0x1234EEFFU, "AH write should update bits 8-15 only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_AX, 0x0001ABCDU) ? record_failure("write AX should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EAX, 0x1234ABCDU, "AX write should update low word only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ESP, 0xAAAABBBBU) ? record_failure("write ESP before SP") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_SP, 0x00012345U) ? record_failure("write SP should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_ESP, 0xAAAA2345U, "SP write should update low word only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_BH, 0x12345678U) ? record_failure("write BH should mask to 8 bits") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_BH, 0x78U, "BH write should be masked to 8 bits");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EBX, 0x01020304U) ? record_failure("write EBX before BL/BH/BX") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_BL, 0x000000AAU) ? record_failure("write BL should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EBX, 0x010203AAU, "BL write should update only low byte");
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_BH, 0x000000BBU) ? record_failure("write BH should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EBX, 0x0102BBAAU, "BH write should update bits 8-15 only");
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_BX, 0x0000CCDDU) ? record_failure("write BX should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EBX, 0x0102CCDDU, "BX write should update low word only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ECX, 0x11121314U) ? record_failure("write ECX before CL/CH/CX") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_CL, 0x00000022U) ? record_failure("write CL should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_ECX, 0x11121322U, "CL write should update only low byte");
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_CH, 0x00000033U) ? record_failure("write CH should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_ECX, 0x11123322U, "CH write should update bits 8-15 only");
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_CX, 0x00004455U) ? record_failure("write CX should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_ECX, 0x11124455U, "CX write should update low word only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EDX, 0x21222324U) ? record_failure("write EDX before DL/DH/DX") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_DL, 0x00000066U) ? record_failure("write DL should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EDX, 0x21222366U, "DL write should update only low byte");
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_DH, 0x00000077U) ? record_failure("write DH should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EDX, 0x21227766U, "DH write should update bits 8-15 only");
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_DX, 0x00008899U) ? record_failure("write DX should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EDX, 0x21228899U, "DX write should update low word only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_ESI, 0xABCDEF01U) ? record_failure("write ESI before SI") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_SI, 0x00002345U) ? record_failure("write SI should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_ESI, 0xABCD2345U, "SI write should update low word only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EDI, 0xABCDEF01U) ? record_failure("write EDI before DI") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_DI, 0x00003456U) ? record_failure("write DI should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EDI, 0xABCD3456U, "DI write should update low word only");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EBP, 0xABCDEF01U) ? record_failure("write EBP before BP") : 0;
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_BP, 0x00004567U) ? record_failure("write BP should succeed") : 0;
    failures += expect_register_value(&cpu, VM_REGISTER_EBP, 0xABCD4567U, "BP write should update low word only");

    return failures;
}

/// Verifies metadata helpers for representative registers and aliases.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_metadata(void) {
    int failures = 0;

    failures += expect_register_metadata(VM_REGISTER_EAX, 32U, "EAX");
    failures += expect_register_metadata(VM_REGISTER_EFLAGS, 32U, "EFLAGS");
    failures += expect_register_metadata(VM_REGISTER_AX, 16U, "AX");
    failures += expect_register_metadata(VM_REGISTER_AH, 8U, "AH");
    failures += expect_register_metadata(VM_REGISTER_AL, 8U, "AL");
    failures += expect_register_metadata(VM_REGISTER_SP, 16U, "SP");

    if (vm_cpu_register_width_bits((VmRegister)-1) != 0U) {
        failures += record_failure("invalid register width should be zero");
    }

    if (vm_cpu_register_name((VmRegister)-1) != NULL) {
        failures += record_failure("invalid register name should be NULL");
    }

    if (vm_cpu_register_width_bits((VmRegister)VM_REGISTER_COUNT) != 0U) {
        failures += record_failure("out-of-range register width should be zero");
    }

    if (vm_cpu_register_name((VmRegister)VM_REGISTER_COUNT) != NULL) {
        failures += record_failure("out-of-range register name should be NULL");
    }

    return failures;
}

/// Verifies default modeled-flag validity metadata after CPU initialization.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_initialization_marks_modeled_flags_valid(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += expect_flag_validity(&cpu, VM_FLAG_CF, 1, NULL, NULL, "initialized CF should be valid");
    failures += expect_flag_validity(&cpu, VM_FLAG_ZF, 1, NULL, NULL, "initialized ZF should be valid");
    failures += expect_flag_validity(&cpu, VM_FLAG_SF, 1, NULL, NULL, "initialized SF should be valid");
    failures += expect_flag_validity(&cpu, VM_FLAG_OF, 1, NULL, NULL, "initialized OF should be valid");

    return failures;
}

/// Verifies modeled-flag validity helper behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_flag_validity_metadata_helpers(void) {
    int failures = 0;
    VmCpu cpu;
    VmFlagValidityMetadata metadata;

    vm_cpu_init(&cpu);
    failures += vm_cpu_write_flag(&cpu, VM_FLAG_OF, 1) ? 0 : record_failure("OF write should succeed before undefined metadata");
    failures += vm_cpu_mark_flag_undefined(&cpu, VM_FLAG_OF, "undefined-modeled-flag", "rol", "main.asm", 4U, 5U, 40U, 10U, "rol eax, 2", 3U) ? 0 : record_failure("mark OF undefined should succeed");
    failures += expect_flag_validity(&cpu, VM_FLAG_OF, 0, "undefined-modeled-flag", "rol", "OF should be invalid after metadata helper");
    failures += vm_cpu_read_flag_validity(&cpu, VM_FLAG_OF, &metadata) ? 0 : record_failure("OF metadata read should succeed");
    if (metadata.producer_source_line != 4U ||
        metadata.producer_source_column != 5U ||
        metadata.producer_byte_offset != 40U ||
        metadata.producer_span_length != 10U ||
        metadata.producer_instruction_index != 3U) {
        failures += record_failure("undefined metadata should preserve producer source span and instruction index");
    }

    failures += vm_cpu_clear_flag(&cpu, VM_FLAG_OF) ? 0 : record_failure("OF clear should succeed");
    failures += expect_flag_validity(&cpu, VM_FLAG_OF, 1, NULL, NULL, "OF write should clear undefined metadata");

    failures += vm_cpu_mark_flag_undefined(&cpu, VM_FLAG_CF, "undefined-shift-flag", "shl", "main.asm", 2U, 5U, 20U, 9U, "shl al, 8", 1U) ? 0 : record_failure("mark CF undefined should succeed");
    failures += expect_flag_validity(&cpu, VM_FLAG_CF, 0, "undefined-shift-flag", "shl", "CF should be invalid before raw EFLAGS write");
    failures += vm_cpu_write_register(&cpu, VM_REGISTER_EFLAGS, 0U) ? 0 : record_failure("raw EFLAGS write should succeed");
    failures += expect_flag_validity(&cpu, VM_FLAG_CF, 1, NULL, NULL, "raw EFLAGS write should mark CF valid");
    failures += expect_flag_validity(&cpu, VM_FLAG_OF, 1, NULL, NULL, "raw EFLAGS write should mark OF valid");

    if (vm_cpu_read_flag_validity(NULL, VM_FLAG_CF, &metadata)) {
        failures += record_failure("flag validity read should reject NULL CPU");
    }
    if (vm_cpu_read_flag_validity(&cpu, VM_FLAG_CF, NULL)) {
        failures += record_failure("flag validity read should reject NULL output");
    }
    if (vm_cpu_mark_flag_valid(&cpu, (VmFlag)VM_FLAG_COUNT)) {
        failures += record_failure("mark flag valid should reject invalid flag");
    }
    if (vm_cpu_mark_flag_undefined(NULL, VM_FLAG_CF, "undefined-shift-flag", "shl", NULL, 0U, 0U, 0U, 0U, NULL, 0U)) {
        failures += record_failure("mark flag undefined should reject NULL CPU");
    }

    return failures;
}


/// Verifies deterministic seeded startup for registers and modeled flags.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_seeded_register_flag_startup_is_deterministic(void) {
    int failures = 0;
    VmCpu first;
    VmCpu second;
    int reg_index = 0;
    int flag_index = 0;

    vm_cpu_init_seeded_registers_and_flags(&first, 12345U);
    vm_cpu_init_seeded_registers_and_flags(&second, 12345U);

    for (reg_index = 0; reg_index < (int)VM_REGISTER_COUNT; reg_index += 1) {
        uint32_t first_value = 0U;
        uint32_t second_value = 1U;
        failures += !vm_cpu_read_register(&first, (VmRegister)reg_index, &first_value) ? record_failure("first seeded register read should succeed") : 0;
        failures += !vm_cpu_read_register(&second, (VmRegister)reg_index, &second_value) ? record_failure("second seeded register read should succeed") : 0;
        if (first_value != second_value) {
            failures += record_failure("same seed should produce same register values");
        }
    }

    for (flag_index = 0; flag_index < (int)VM_FLAG_COUNT; flag_index += 1) {
        bool first_value = false;
        bool second_value = true;
        failures += !vm_cpu_read_flag(&first, (VmFlag)flag_index, &first_value) ? record_failure("first seeded flag read should succeed") : 0;
        failures += !vm_cpu_read_flag(&second, (VmFlag)flag_index, &second_value) ? record_failure("second seeded flag read should succeed") : 0;
        if (first_value != second_value) {
            failures += record_failure("same seed should produce same modeled flag values");
        }
        failures += expect_flag_validity(&first, (VmFlag)flag_index, 1, NULL, NULL, "seeded startup should mark modeled flags valid");
    }

    failures += expect_register_value(&first, VM_REGISTER_EIP, 0U, "seeded startup should leave EIP zero");

    if ((first.eflags & ~0x000008C1U) != 0U) {
        failures += record_failure("seeded startup should not set unmodeled EFLAGS bits");
    }

    vm_cpu_init_seeded_registers_and_flags(NULL, 12345U);

    return failures;
}

/// Verifies different seeds produce at least one different register or modeled flag.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_seeded_register_flag_startup_changes_by_seed(void) {
    VmCpu first;
    VmCpu second;
    int reg_index = 0;
    int flag_index = 0;
    bool saw_difference = false;

    vm_cpu_init_seeded_registers_and_flags(&first, 1U);
    vm_cpu_init_seeded_registers_and_flags(&second, 2U);

    for (reg_index = 0; reg_index < (int)VM_REGISTER_COUNT; reg_index += 1) {
        uint32_t first_value = 0U;
        uint32_t second_value = 0U;
        (void)vm_cpu_read_register(&first, (VmRegister)reg_index, &first_value);
        (void)vm_cpu_read_register(&second, (VmRegister)reg_index, &second_value);
        if (first_value != second_value) {
            saw_difference = true;
        }
    }

    for (flag_index = 0; flag_index < (int)VM_FLAG_COUNT; flag_index += 1) {
        bool first_value = false;
        bool second_value = false;
        (void)vm_cpu_read_flag(&first, (VmFlag)flag_index, &first_value);
        (void)vm_cpu_read_flag(&second, (VmFlag)flag_index, &second_value);
        if (first_value != second_value) {
            saw_difference = true;
        }
    }

    if (!saw_difference) {
        return record_failure("different seeds should produce at least one different register or modeled flag");
    }

    return 0;
}


/// Verifies Phase 57H register-family write tracking for canonical and alias writes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_family_write_tracking(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init(&cpu);

    failures += expect_register_family_written(&cpu, VM_REGISTER_EAX, false, "EAX should start unwritten");
    failures += expect_register_family_written(&cpu, VM_REGISTER_EFLAGS, false, "EFLAGS should start unwritten");

    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_AL, 0U) ? record_failure("same-value AL write should succeed") : 0;
    failures += expect_register_family_written(&cpu, VM_REGISTER_EAX, true, "AL write should mark EAX family written");
    failures += expect_register_family_written(&cpu, VM_REGISTER_AX, true, "AX query should see EAX family written");
    failures += expect_register_family_written(&cpu, VM_REGISTER_EBX, false, "EBX should remain unwritten");

    failures += !vm_cpu_write_flag(&cpu, VM_FLAG_ZF, true) ? record_failure("flag write should succeed") : 0;
    failures += expect_register_family_written(&cpu, VM_REGISTER_EFLAGS, true, "flag write should mark EFLAGS family written");

    vm_cpu_clear_register_write_tracking(&cpu);
    failures += expect_register_family_written(&cpu, VM_REGISTER_EAX, false, "clear should reset EAX family tracking");
    failures += expect_register_family_written(&cpu, VM_REGISTER_EFLAGS, false, "clear should reset EFLAGS family tracking");

    return failures;
}

/// Verifies startup initialization is excluded from Phase 57H write tracking.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_startup_initialization_clears_write_tracking(void) {
    int failures = 0;
    VmCpu cpu;

    vm_cpu_init_seeded_registers_and_flags(&cpu, 123456789U);

    failures += expect_register_family_written(&cpu, VM_REGISTER_EAX, false, "seeded EAX startup should not count as program write");
    failures += expect_register_family_written(&cpu, VM_REGISTER_ESP, false, "seeded ESP startup should not count as program write");
    failures += expect_register_family_written(&cpu, VM_REGISTER_EFLAGS, false, "seeded flag startup should not count as program write");

    return failures;
}

/// Verifies invalid and NULL argument handling.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_invalid_input_handling(void) {
    int failures = 0;
    VmCpu cpu;
    uint32_t value = 0xCAFEBABEU;

    vm_cpu_init(&cpu);
    failures += !vm_cpu_write_register(&cpu, VM_REGISTER_EAX, 0x12345678U) ? record_failure("initial EAX write") : 0;

    if (vm_cpu_read_register(NULL, VM_REGISTER_EAX, &value)) {
        failures += record_failure("read should reject NULL CPU pointer");
    }

    if (vm_cpu_read_register(&cpu, VM_REGISTER_EAX, NULL)) {
        failures += record_failure("read should reject NULL output pointer");
    }

    value = 0xCAFEBABEU;
    if (vm_cpu_read_register(&cpu, (VmRegister)-1, &value)) {
        failures += record_failure("read should reject negative invalid register");
    }

    if (value != 0xCAFEBABEU) {
        failures += record_failure("invalid read should not modify output value");
    }

    if (vm_cpu_write_register(NULL, VM_REGISTER_EAX, 0U)) {
        failures += record_failure("write should reject NULL CPU pointer");
    }

    if (vm_cpu_write_register(&cpu, (VmRegister)VM_REGISTER_COUNT, 0U)) {
        failures += record_failure("write should reject out-of-range register");
    }

    failures += expect_register_value(&cpu, VM_REGISTER_EAX, 0x12345678U, "invalid write should not mutate EAX");

    return failures;
}

/// Runs all Milestone 1 CPU register tests.
///
/// @return Zero on success, non-zero when any test fails.
int main(void) {
    int failures = 0;

    failures += test_initialization_zeroes_registers();
    failures += test_canonical_register_reads_and_writes();
    failures += test_general_purpose_alias_reads();
    failures += test_index_and_pointer_alias_reads();
    failures += test_alias_writes_preserve_bits();
    failures += test_register_metadata();
    failures += test_initialization_marks_modeled_flags_valid();
    failures += test_flag_validity_metadata_helpers();
    failures += test_seeded_register_flag_startup_is_deterministic();
    failures += test_seeded_register_flag_startup_changes_by_seed();
    failures += test_register_family_write_tracking();
    failures += test_startup_initialization_clears_write_tracking();
    failures += test_invalid_input_handling();

    if (failures != 0) {
        fprintf(stderr, "%d CPU register and flag metadata test failure(s).\n", failures);
        return 1;
    }

    puts("CPU register and flag metadata tests passed.");
    return 0;
}
