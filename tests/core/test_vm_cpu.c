/*
 * @file test_vm_cpu.c
 * @brief Unit tests for the Milestone 1 MASM32 CPU register model.
 *
 * These tests cover canonical register storage, alias masking, invalid input
 * handling, and metadata helpers without introducing parser or VM execution.
 */

#include <stdio.h>
#include <stdint.h>
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
    failures += test_invalid_input_handling();

    if (failures != 0) {
        fprintf(stderr, "%d Milestone 1 CPU register test failure(s).\n", failures);
        return 1;
    }

    puts("Milestone 1 CPU register tests passed.");
    return 0;
}
