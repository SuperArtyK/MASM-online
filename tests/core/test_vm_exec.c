/*
 * @file test_vm_exec.c
 * @brief Unit tests for the VM executor through Milestone 50.
 *
 * These tests exercise the first vertical execution slice: hardcoded IR, VM
 * stepping, supported straight-line instruction semantics, CPU and memory
 * integration, and last-step delta capture. They intentionally avoid parser,
 * control-flow, stack, Irvine32 routine bodies, and browser UI behavior except
 * for the Phase 42 virtual exit terminator.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_exec.h"
#include "../../src/wasm/wasm_api.h"

/// Records a test failure with file-local context.
///
/// @param message Human-readable failure description.
/// @return Always returns 1 so callers can accumulate failures.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that two unsigned 32-bit values are equal.
///
/// @param actual Actual value produced by the test.
/// @param expected Expected value.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=0x%08X expected=0x%08X)\n", message, actual, expected);
        return 1;
    }

    return 0;
}

/// Verifies that two size_t values are equal.
///
/// @param actual Actual value produced by the test.
/// @param expected Expected value.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_size(size_t actual, size_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%lu expected=%lu)\n", message, (unsigned long)actual, (unsigned long)expected);
        return 1;
    }

    return 0;
}

/// Verifies that an executor status equals an expected value.
///
/// @param actual Actual status produced by the test.
/// @param expected Expected status.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_status(VmExecStatus actual, VmExecStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_exec_status_name(actual), vm_exec_status_name(expected));
        return 1;
    }

    return 0;
}

/// Verifies that a named flag has the expected value.
///
/// @param cpu CPU state to inspect.
/// @param flag Flag to read.
/// @param expected Expected flag state.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_flag(const VmCpu *cpu, VmFlag flag, bool expected, const char *message) {
    bool actual = false;

    if (!vm_cpu_read_flag(cpu, flag, &actual)) {
        return record_failure("flag read should succeed");
    }

    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%d expected=%d)\n", message, actual ? 1 : 0, expected ? 1 : 0);
        return 1;
    }

    return 0;
}

/// Finds a register change in a delta.
///
/// @param delta Delta to inspect.
/// @param reg Register to find.
/// @return Matching change, or NULL when not found.
static const VmExecRegisterChange *find_register_change(const VmExecDelta *delta, VmRegister reg) {
    size_t index = 0U;

    if (delta == NULL) {
        return NULL;
    }

    for (index = 0U; index < delta->register_change_count; index += 1U) {
        if (delta->register_changes[index].reg == reg) {
            return &delta->register_changes[index];
        }
    }

    return NULL;
}

/// Finds a named flag change in a delta.
///
/// @param delta Delta to inspect.
/// @param flag Flag to find.
/// @return Matching change, or NULL when not found.
static const VmExecFlagChange *find_flag_change(const VmExecDelta *delta, VmFlag flag) {
    size_t index = 0U;

    if (delta == NULL) {
        return NULL;
    }

    for (index = 0U; index < delta->flag_change_count; index += 1U) {
        if (delta->flag_changes[index].flag == flag) {
            return &delta->flag_changes[index];
        }
    }

    return NULL;
}

/// Verifies that the hardcoded Milestone 4 sample returns EAX = 42.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_hardcoded_program_result(void) {
    int failures = 0;
    uint32_t eax = 0U;

    failures += expect_status(vm_run_milestone4_hardcoded_program(&eax), VM_EXEC_STATUS_OK, "hardcoded program should run successfully");
    failures += expect_u32(eax, 42U, "hardcoded program should produce EAX = 42");

    if (masm32_sim_wasm_milestone4_hardcoded_result() != 42) {
        failures += record_failure("wasm-facing Milestone 4 test export should return 42");
    }

    failures += expect_status(vm_run_milestone4_hardcoded_program(NULL), VM_EXEC_STATUS_INVALID_ARGUMENT, "hardcoded program should reject NULL output");

    return failures;
}

/// Verifies step-by-step mov/add behavior and register deltas.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_step_mov_add_register_delta(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmExecDelta *delta = NULL;
    const VmExecRegisterChange *eax_change = NULL;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 20U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, 20", 0U},
        {VM_IR_OPCODE_ADD, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 22U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "add eax, 22", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "program load should succeed");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "first step should execute mov");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed"));
    failures += expect_u32(eax, 20U, "first step should set EAX to 20");
    delta = vm_last_delta(&vm);
    if (delta == NULL || !delta->has_instruction) {
        failures += record_failure("first step should produce an instruction delta");
    } else {
        failures += expect_u32(delta->instruction.source_line, 3U, "delta should preserve source line");
        if (delta->instruction.source_text == NULL || strcmp(delta->instruction.source_text, "mov eax, 20") != 0) {
            failures += record_failure("delta should preserve source text");
        }
        eax_change = find_register_change(delta, VM_REGISTER_EAX);
        if (eax_change == NULL) {
            failures += record_failure("mov delta should include EAX change");
        } else {
            failures += expect_u32(eax_change->old_value, 0U, "mov delta EAX old value should be 0");
            failures += expect_u32(eax_change->new_value, 20U, "mov delta EAX new value should be 20");
        }
        failures += expect_size(delta->flag_change_count, 0U, "mov should not report flag changes");
        failures += expect_size(delta->memory_change_count, 0U, "register mov should not report memory changes");
        failures += expect_u32((uint32_t)delta->instruction_count, 1U, "first step instruction count should be 1");
    }

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "second step should execute add");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after add"));
    failures += expect_u32(eax, 42U, "second step should set EAX to 42");
    delta = vm_last_delta(&vm);
    eax_change = find_register_change(delta, VM_REGISTER_EAX);
    if (eax_change == NULL) {
        failures += record_failure("add delta should include EAX change");
    } else {
        failures += expect_u32(eax_change->old_value, 20U, "add delta EAX old value should be 20");
        failures += expect_u32(eax_change->new_value, 42U, "add delta EAX new value should be 42");
    }
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "third step should report halted");

    vm_deinit(&vm);
    return failures;
}

/// Verifies sub flag behavior and last-step flag deltas.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sub_underflow_flags_and_delta(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmExecDelta *delta = NULL;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 0", 0U},
        {VM_IR_OPCODE_SUB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sub eax, 1", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for sub test");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "program load should succeed for sub test");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov zero should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "sub should execute");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after sub"));
    failures += expect_u32(eax, 0xFFFFFFFFU, "0 - 1 should wrap to 0xFFFFFFFF");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "sub underflow should set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "sub underflow should set SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "sub underflow should clear ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "sub underflow should clear OF");

    delta = vm_last_delta(&vm);
    if (find_flag_change(delta, VM_FLAG_CF) == NULL) {
        failures += record_failure("sub delta should include CF change");
    }
    if (find_flag_change(delta, VM_FLAG_SF) == NULL) {
        failures += record_failure("sub delta should include SF change");
    }

    vm_deinit(&vm);
    return failures;
}

/// Verifies memory destination/source operands and byte-level memory deltas.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_memory_operands_and_delta(void) {
    int failures = 0;
    Vm vm;
    uint32_t ebx = 0U;
    uint32_t stored = 0U;
    const VmExecDelta *delta = NULL;
    const uint32_t address = VM_MEMORY_DEFAULT_DATA_BASE;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov [data], 12345678h", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov ebx, [data]", 1U},
        {VM_IR_OPCODE_ADD, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "add [data], ebx", 2U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for memory test");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "memory program load should succeed");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "memory immediate mov should execute");
    delta = vm_last_delta(&vm);
    failures += expect_size(delta != NULL ? delta->memory_change_count : 0U, 4U, "DWORD memory write should record four byte changes");
    if (delta != NULL && delta->memory_change_count >= 4U) {
        failures += expect_u32(delta->memory_changes[0].address, address, "first memory byte address should match data base");
        failures += expect_u32(delta->memory_changes[0].new_value, 0x78U, "first memory byte should be little-endian low byte");
        failures += expect_u32(delta->memory_changes[3].address, address + 3U, "fourth memory byte address should match data base + 3");
        failures += expect_u32(delta->memory_changes[3].new_value, 0x12U, "fourth memory byte should be little-endian high byte");
    }

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "memory-to-register mov should execute");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed"));
    failures += expect_u32(ebx, 0x12345678U, "memory-to-register mov should load EBX");
    delta = vm_last_delta(&vm);
    failures += expect_size(delta != NULL ? delta->memory_change_count : 0U, 0U, "memory read should not record memory changes");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "memory add should execute");
    failures += (vm_memory_read_u32(&vm.memory, address, &stored, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("stored DWORD read should succeed"));
    failures += expect_u32(stored, 0x2468ACF0U, "memory add should update stored DWORD");

    vm_deinit(&vm);
    return failures;
}

/// Verifies register-indirect memory operands and access-status recording.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_indirect_memory_operand_and_access_delta(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t stored = 0U;
    const VmExecDelta *delta = NULL;
    const uint32_t unaligned_address = VM_MEMORY_DEFAULT_DATA_BASE + 1U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ESI, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, unaligned_address, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov esi, OFFSET data + 1", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_REGISTER, 32U, 0U, VM_REGISTER_ESI, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xAABBCCDDU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov DWORD PTR [esi], 0AABBCCDDh", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_REGISTER, 32U, 0U, VM_REGISTER_ESI, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, DWORD PTR [esi]", 2U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for register-indirect memory test");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "register-indirect program load should succeed");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ESI setup should execute");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "register-indirect DWORD write should execute");
    failures += (vm_memory_read_u32(&vm.memory, unaligned_address, &stored, NULL) == VM_MEMORY_STATUS_OK_UNALIGNED ? 0 : record_failure("unaligned stored DWORD read should report an unaligned status"));
    failures += expect_u32(stored, 0xAABBCCDDU, "register-indirect write should store the DWORD value");
    delta = vm_last_delta(&vm);
    failures += expect_size(delta != NULL ? delta->memory_change_count : 0U, 4U, "register-indirect write should record four byte changes");
    failures += expect_size(delta != NULL ? delta->memory_access_count : 0U, 1U, "register-indirect write should record one memory access");
    if (delta != NULL && delta->memory_access_count >= 1U) {
        failures += expect_u32(delta->memory_accesses[0].address, unaligned_address, "write access address should use ESI");
        failures += expect_u32((uint32_t)delta->memory_accesses[0].width_bits, 32U, "write access width should be DWORD");
        if (delta->memory_accesses[0].kind != VM_EXEC_MEMORY_ACCESS_WRITE) {
            failures += record_failure("write access should be marked as a write");
        }
        if (delta->memory_accesses[0].status != VM_MEMORY_STATUS_OK_UNALIGNED) {
            failures += record_failure("write access should preserve unaligned status");
        }
    }

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "register-indirect DWORD read should execute");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after register-indirect load"));
    failures += expect_u32(eax, 0xAABBCCDDU, "register-indirect read should load EAX");
    delta = vm_last_delta(&vm);
    failures += expect_size(delta != NULL ? delta->memory_access_count : 0U, 1U, "register-indirect read should record one memory access");
    if (delta != NULL && delta->memory_access_count >= 1U) {
        failures += expect_u32(delta->memory_accesses[0].address, unaligned_address, "read access address should use ESI");
        if (delta->memory_accesses[0].kind != VM_EXEC_MEMORY_ACCESS_READ) {
            failures += record_failure("read access should be marked as a read");
        }
        if (delta->memory_accesses[0].status != VM_MEMORY_STATUS_OK_UNALIGNED) {
            failures += record_failure("read access should preserve unaligned status");
        }
    }

    vm_deinit(&vm);
    return failures;
}

/// Verifies 8-bit register alias execution and flag width handling.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_alias_width_edge_case(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x123U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov al, 123h", 0U},
        {VM_IR_OPCODE_ADD, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0xFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "add al, 0FFh", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for alias test");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "alias program load should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "AL mov should execute");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after AL mov"));
    failures += expect_u32(eax, 0x00000023U, "AL mov should mask immediate to 8 bits");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "AL add should execute");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after AL add"));
    failures += expect_u32(eax, 0x00000022U, "AL add should wrap at 8 bits");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "AL add should set CF for 8-bit carry");

    vm_deinit(&vm);
    return failures;
}

/// Verifies zero-length program halt behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_zero_length_program_halts(void) {
    int failures = 0;
    Vm vm;

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for zero-length test");
    failures += expect_status(vm_load_program(&vm, NULL, 0U), VM_EXEC_STATUS_OK, "zero-length program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "zero-length program should halt on first step");
    failures += expect_size(vm_last_delta(&vm) != NULL ? vm_last_delta(&vm)->register_change_count : 1U, 0U, "halt should not retain register delta");

    vm_deinit(&vm);
    return failures;
}

/// Verifies executor error handling and diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_error_paths_and_diagnostics(void) {
    int failures = 0;
    Vm vm;
    const VmExecDiagnostic *diagnostic = NULL;
    const VmIrInstruction invalid_opcode[] = {
        {(VmIrOpcode)99, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "bad eax, 1", 0U}
    };
    const VmIrInstruction immediate_destination[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov 0, 1", 0U}
    };
    const VmIrInstruction invalid_memory_read[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0xDEADBEEFU, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, [bad]", 0U}
    };
    const VmIrInstruction code_write[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CODE_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "mov [code], 1", 0U}
    };
    const VmIrInstruction mismatched_register_width[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, al", 0U}
    };
    const VmIrInstruction invalid_register_width_override[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 32U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "mov al<32>, 1", 0U}
    };
    const VmIrInstruction mismatched_immediate_width[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "mov eax, imm8", 0U}
    };

    failures += expect_status(vm_step(NULL), VM_EXEC_STATUS_INVALID_ARGUMENT, "step should reject NULL VM");
    failures += expect_status(vm_load_program(NULL, NULL, 0U), VM_EXEC_STATUS_INVALID_ARGUMENT, "load should reject NULL VM");

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for error tests");
    failures += expect_status(vm_load_program(&vm, NULL, 1U), VM_EXEC_STATUS_INVALID_ARGUMENT, "load should reject NULL program with nonzero count");

    failures += expect_status(vm_load_program(&vm, invalid_opcode, 1U), VM_EXEC_STATUS_OK, "invalid opcode program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_INVALID_INSTRUCTION, "invalid opcode should fail during step");
    diagnostic = vm_last_diagnostic(&vm);
    if (diagnostic == NULL || !diagnostic->has_instruction || diagnostic->instruction.source_line != 1U) {
        failures += record_failure("invalid opcode diagnostic should preserve instruction context");
    }
    failures += expect_size(vm_last_delta(&vm) != NULL ? vm_last_delta(&vm)->register_change_count : 1U, 0U, "invalid opcode should clear previous delta");

    failures += expect_status(vm_load_program(&vm, immediate_destination, 1U), VM_EXEC_STATUS_OK, "immediate destination program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "immediate destination should be unsupported");

    failures += expect_status(vm_load_program(&vm, invalid_memory_read, 1U), VM_EXEC_STATUS_OK, "invalid memory read program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_MEMORY_ERROR, "invalid memory read should fail as memory error");
    diagnostic = vm_last_diagnostic(&vm);
    if (diagnostic == NULL || diagnostic->memory_status != VM_MEMORY_STATUS_INVALID_ADDRESS || diagnostic->memory_diagnostic.address != 0xDEADBEEFU) {
        failures += record_failure("invalid memory read diagnostic should include memory status and address");
    }

    failures += expect_status(vm_load_program(&vm, code_write, 1U), VM_EXEC_STATUS_OK, "code write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_MEMORY_ERROR, "write to code should fail as memory error");
    diagnostic = vm_last_diagnostic(&vm);
    if (diagnostic == NULL || diagnostic->memory_status != VM_MEMORY_STATUS_PERMISSION_DENIED) {
        failures += record_failure("code write diagnostic should report permission denied");
    }

    failures += expect_status(vm_load_program(&vm, mismatched_register_width, 1U), VM_EXEC_STATUS_OK, "mismatched register width program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "mov eax, al should be rejected as mismatched IR widths");
    diagnostic = vm_last_diagnostic(&vm);
    if (diagnostic == NULL || !diagnostic->has_instruction || diagnostic->instruction.source_line != 5U) {
        failures += record_failure("mismatched register width diagnostic should preserve instruction context");
    }

    failures += expect_status(vm_load_program(&vm, invalid_register_width_override, 1U), VM_EXEC_STATUS_OK, "invalid register width override program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "register operand width override should match actual alias width");

    failures += expect_status(vm_load_program(&vm, mismatched_immediate_width, 1U), VM_EXEC_STATUS_OK, "mismatched immediate width program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "immediate width metadata should match instruction width when present");

    vm_deinit(&vm);
    return failures;
}

/// Verifies MOVSX and MOVZX over register and memory sources.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_movsx_movzx_register_and_memory_sources(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov al, 80h", 0U},
        {VM_IR_OPCODE_MOVSX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "movsx eax, al", 1U},
        {VM_IR_OPCODE_MOVZX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "movzx ebx, al", 2U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0xFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "mov BYTE PTR [data], 0FFh", 3U},
        {VM_IR_OPCODE_MOVSX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ECX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "movsx ecx, BYTE PTR [data]", 4U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "mov WORD PTR [data+2], 8001h", 5U},
        {VM_IR_OPCODE_MOVSX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EDX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "movsx edx, WORD PTR [data+2]", 6U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for extension tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "extension program should load");
    while (!vm.halted) {
        failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "extension step should execute");
    }

    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after movsx"));
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed after movzx"));
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_ECX, &ecx) ? 0 : record_failure("ECX read should succeed after memory movsx"));
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EDX, &edx) ? 0 : record_failure("EDX read should succeed after word movsx"));
    failures += expect_u32(eax, 0xFFFFFF80U, "movsx eax, al should sign-extend 80h");
    failures += expect_u32(ebx, 0x00000080U, "movzx ebx, al should zero-extend 80h");
    failures += expect_u32(ecx, 0xFFFFFFFFU, "movsx ecx, byte memory should sign-extend FFh");
    failures += expect_u32(edx, 0xFFFF8001U, "movsx edx, word memory should sign-extend 8001h");

    vm_deinit(&vm);
    return failures;
}

/// Verifies CBW, CWDE, CWD, and CDQ accumulator conversions.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_accumulator_extension_instructions(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t edx = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 0", 0U},
        {VM_IR_OPCODE_SUB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sub eax, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov al, 80h", 2U},
        {VM_IR_OPCODE_CBW, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "cbw", 3U},
        {VM_IR_OPCODE_CWDE, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "cwde", 4U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EDX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "mov edx, 0", 5U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "mov ax, 8000h", 6U},
        {VM_IR_OPCODE_CWD, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 8U, "cwd", 7U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 9U, "mov eax, 80000000h", 8U},
        {VM_IR_OPCODE_CDQ, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 10U, "cdq", 9U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for accumulator tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "accumulator program should load");
    while (!vm.halted) {
        failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "accumulator step should execute");
    }

    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after cdq"));
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EDX, &edx) ? 0 : record_failure("EDX read should succeed after cdq"));
    failures += expect_u32(eax, 0x80000000U, "cdq should leave EAX unchanged");
    failures += expect_u32(edx, 0xFFFFFFFFU, "cdq should sign-extend negative EAX into EDX");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "conversion instructions should preserve existing CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "conversion instructions should preserve existing SF");

    vm_deinit(&vm);
    return failures;
}


/// Verifies 16-bit destinations and positive accumulator conversion edge cases.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_extension_instruction_edge_cases(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_BL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0xFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov bl, 0FFh", 0U},
        {VM_IR_OPCODE_MOVZX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_CX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_BL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "movzx cx, bl", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xFFFF007FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, 0FFFF007Fh", 2U},
        {VM_IR_OPCODE_CBW, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "cbw", 3U},
        {VM_IR_OPCODE_CWDE, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "cwde", 4U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EDX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xAAAAFFFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "mov edx, 0AAAAFFFFh", 5U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x0001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "mov ax, 1", 6U},
        {VM_IR_OPCODE_CWD, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 8U, "cwd", 7U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EDX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xFFFFFFFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 9U, "mov edx, 0FFFFFFFFh", 8U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 10U, "mov eax, 1", 9U},
        {VM_IR_OPCODE_CDQ, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 11U, "cdq", 10U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for extension edge-case tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "extension edge-case program should load");
    while (!vm.halted) {
        failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "extension edge-case step should execute");
    }

    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after positive cdq"));
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_ECX, &ecx) ? 0 : record_failure("ECX read should succeed after movzx cx, bl"));
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EDX, &edx) ? 0 : record_failure("EDX read should succeed after positive cdq"));
    failures += expect_u32(ecx, 0x000000FFU, "movzx cx, bl should zero-extend into a 16-bit destination alias");
    failures += expect_u32(eax, 0x00000001U, "positive cdq should leave EAX unchanged");
    failures += expect_u32(edx, 0x00000000U, "positive cdq should clear EDX");

    vm_deinit(&vm);
    return failures;
}

/// Verifies unsupported extension-instruction operand combinations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_extension_instruction_error_paths(void) {
    int failures = 0;
    Vm vm;
    const VmIrInstruction memory_destination[] = {
        {VM_IR_OPCODE_MOVSX, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "movsx [data], al", 0U}
    };
    const VmIrInstruction immediate_source[] = {
        {VM_IR_OPCODE_MOVZX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "movzx eax, 1", 0U}
    };
    const VmIrInstruction source_not_narrow[] = {
        {VM_IR_OPCODE_MOVSX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "movsx eax, ebx", 0U}
    };
    const VmIrInstruction destination_not_wider[] = {
        {VM_IR_OPCODE_MOVZX, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_BX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "movzx ax, bx", 0U}
    };
    const VmIrInstruction operand_on_cbw[] = {
        {VM_IR_OPCODE_CBW, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "cbw eax", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for extension error tests");
    failures += expect_status(vm_load_program(&vm, memory_destination, 1U), VM_EXEC_STATUS_OK, "memory destination program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "movsx memory destination should be unsupported");
    failures += expect_status(vm_load_program(&vm, immediate_source, 1U), VM_EXEC_STATUS_OK, "immediate source program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "movzx immediate source should be unsupported");
    failures += expect_status(vm_load_program(&vm, source_not_narrow, 1U), VM_EXEC_STATUS_OK, "wide source program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "movsx 32-bit source should be unsupported");
    failures += expect_status(vm_load_program(&vm, destination_not_wider, 1U), VM_EXEC_STATUS_OK, "same-width extension program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "movzx same-width operands should be unsupported");
    failures += expect_status(vm_load_program(&vm, operand_on_cbw, 1U), VM_EXEC_STATUS_OK, "cbw with operand program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "cbw with operand should be unsupported");

    vm_deinit(&vm);
    return failures;
}


/// Verifies XCHG register/register behavior and flag preservation.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_xchg_registers_preserves_flags(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_XCHG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "xchg eax, ebx", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for xchg register tests");
    failures += expect_status(vm_load_program(&vm, program, 1U), VM_EXEC_STATUS_OK, "xchg register program should load");
    failures += vm_cpu_write_register(&vm.cpu, VM_REGISTER_EAX, 5U) ? 0 : record_failure("EAX write should succeed before xchg");
    failures += vm_cpu_write_register(&vm.cpu, VM_REGISTER_EBX, 10U) ? 0 : record_failure("EBX write should succeed before xchg");
    failures += vm_cpu_set_flag(&vm.cpu, VM_FLAG_CF) ? 0 : record_failure("CF set should succeed before xchg");
    failures += vm_cpu_set_flag(&vm.cpu, VM_FLAG_ZF) ? 0 : record_failure("ZF set should succeed before xchg");
    failures += vm_cpu_clear_flag(&vm.cpu, VM_FLAG_SF) ? 0 : record_failure("SF clear should succeed before xchg");
    failures += vm_cpu_set_flag(&vm.cpu, VM_FLAG_OF) ? 0 : record_failure("OF set should succeed before xchg");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "xchg eax, ebx should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after xchg");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed after xchg");
    failures += expect_u32(eax, 10U, "xchg should move EBX into EAX");
    failures += expect_u32(ebx, 5U, "xchg should move EAX into EBX");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "xchg should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "xchg should preserve ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "xchg should preserve SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "xchg should preserve OF");
    failures += expect_size(vm_last_delta(&vm)->flag_change_count, 0U, "xchg should not report flag deltas");

    vm_deinit(&vm);
    return failures;
}

/// Verifies XCHG memory/register behavior and NOP last-step deltas.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_xchg_memory_and_nop_delta(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t mem_value = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_XCHG, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "xchg DWORD PTR value, eax", 0U},
        {VM_IR_OPCODE_NOP, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "nop", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for xchg memory tests");
    failures += expect_status(vm_load_program(&vm, program, 2U), VM_EXEC_STATUS_OK, "xchg memory program should load");
    failures += vm_cpu_write_register(&vm.cpu, VM_REGISTER_EAX, 0x11223344U) ? 0 : record_failure("EAX write should succeed before memory xchg");
    failures += vm_memory_write_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, 0xAABBCCDDU, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("data memory write should succeed before memory xchg");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "xchg memory, eax should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after memory xchg");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &mem_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("data memory read should succeed after memory xchg");
    failures += expect_u32(eax, 0xAABBCCDDU, "memory xchg should move old memory into EAX");
    failures += expect_u32(mem_value, 0x11223344U, "memory xchg should move old EAX into memory");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 4U, "memory xchg should record four changed bytes");
    failures += expect_size(vm_last_delta(&vm)->flag_change_count, 0U, "memory xchg should not change flags");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "nop should execute");
    failures += expect_size(vm_last_delta(&vm)->register_change_count, 0U, "nop should report no register changes");
    failures += expect_size(vm_last_delta(&vm)->flag_change_count, 0U, "nop should report no flag changes");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "nop should report no memory changes");
    failures += expect_size(vm_last_delta(&vm)->memory_access_count, 0U, "nop should report no memory accesses");

    vm_deinit(&vm);
    return failures;
}

/// Verifies NEG register and memory behavior plus arithmetic flag edge cases.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_neg_register_memory_and_flags(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint8_t mem8 = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov al, 1", 0U},
        {VM_IR_OPCODE_NEG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "neg al", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov al, 80h", 2U},
        {VM_IR_OPCODE_NEG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "neg al", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, 80000000h", 4U},
        {VM_IR_OPCODE_NEG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "neg eax", 5U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 5U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "mov BYTE PTR value, 5", 6U},
        {VM_IR_OPCODE_NEG, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 8U, "neg BYTE PTR value", 7U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 9U, "mov al, 0", 8U},
        {VM_IR_OPCODE_NEG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 10U, "neg al", 9U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for neg tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "neg program should load");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov al, 1 should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "neg al should execute for 1");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after neg al");
    failures += expect_u32(eax, 0x000000FFU, "neg al should produce FFh for input 1");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "neg nonzero should set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "neg one should clear ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "neg one at 8-bit width should set SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "neg one should clear OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov al, 80h should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "neg al should execute for 80h");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after neg 80h");
    failures += expect_u32(eax, 0x00000080U, "neg 80h at 8-bit width should remain 80h");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "neg most-negative 8-bit value should set OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov eax, 80000000h should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "neg eax should execute for 80000000h");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after neg eax");
    failures += expect_u32(eax, 0x80000000U, "neg most-negative 32-bit value should remain unchanged");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "neg most-negative 32-bit value should set OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov memory byte should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "neg memory byte should execute");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &mem8, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after neg");
    failures += expect_u32((uint32_t)mem8, 0xFBU, "neg BYTE PTR memory should store FBh for input 5");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 1U, "neg byte memory should record one changed byte");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov al, 0 should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "neg zero should execute");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "neg zero should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "neg zero should set ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "neg zero should clear SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "neg zero should clear OF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies ADC carry propagation and arithmetic flag updates.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_adc_register_carry_propagation(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xFFFFFFFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 0FFFFFFFFh", 0U},
        {VM_IR_OPCODE_ADD, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "add eax, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov ebx, 0", 2U},
        {VM_IR_OPCODE_ADC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "adc ebx, 0", 3U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for ADC register tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "ADC register program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov eax should execute before ADC");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "add should produce carry before ADC");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "add overflow should set CF before ADC");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov ebx should preserve carry before ADC");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "mov should preserve CF before ADC");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ADC should consume carry");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after ADC");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed after ADC");
    failures += expect_u32(eax, 0U, "acceptance program should leave EAX zero");
    failures += expect_u32(ebx, 1U, "ADC should propagate carry into EBX");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "ADC 0 + carry into zero should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "ADC result one should clear ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "ADC result one should clear SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "ADC result one should clear OF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies ADC edge cases for aliases, signed overflow, and memory destinations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_adc_alias_and_memory_edge_cases(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t memory_value = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x7FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov al, 7Fh", 1U},
        {VM_IR_OPCODE_ADC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "adc al, 0", 2U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xFFFFFFFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "mov DWORD PTR value, 0FFFFFFFFh", 3U},
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "stc", 4U},
        {VM_IR_OPCODE_ADC, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "adc DWORD PTR value, 0", 5U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for ADC edge tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "ADC edge program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before alias ADC");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov AL should execute before alias ADC");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ADC AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after alias ADC");
    failures += expect_u32(eax, 0x00000080U, "ADC AL should preserve upper EAX bits and write 80h");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "ADC 7Fh + carry should not set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "ADC 7Fh + carry should set OF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "ADC 80h result should set SF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov memory before memory ADC should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before memory ADC should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ADC memory destination should execute");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory read after ADC should succeed");
    failures += expect_u32(memory_value, 0U, "ADC memory should wrap FFFFFFFFh + carry to zero");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ADC memory wrap should set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "ADC memory wrap should set ZF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 4U, "ADC DWORD memory should record four byte changes");

    vm_deinit(&vm);
    return failures;
}

/// Verifies SBB borrow propagation for register and memory destinations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sbb_register_and_memory_borrow_propagation(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    uint8_t memory_value = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 0", 0U},
        {VM_IR_OPCODE_SUB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sub eax, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 5U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov ebx, 5", 2U},
        {VM_IR_OPCODE_SBB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 2U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "sbb ebx, 2", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov BYTE PTR value, 0", 4U},
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "stc", 5U},
        {VM_IR_OPCODE_SBB, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "sbb BYTE PTR value, 0", 6U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for SBB tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "SBB program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov eax before SBB should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "sub should set borrow before SBB");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "sub 0 - 1 should set CF before SBB");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov ebx should preserve borrow before SBB");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SBB register should consume borrow");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after SBB");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed after SBB");
    failures += expect_u32(eax, 0xFFFFFFFFU, "SBB setup should leave EAX at FFFFFFFFh");
    failures += expect_u32(ebx, 2U, "SBB should subtract source and incoming borrow");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "SBB 5 - 2 - 1 should clear CF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov memory before SBB should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before memory SBB should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SBB memory destination should execute");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory read after SBB should succeed");
    failures += expect_u32((uint32_t)memory_value, 0xFFU, "SBB memory should borrow to FFh");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "SBB memory borrow should set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "SBB memory FFh should set SF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies ADC and SBB can read compatible memory source operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_adc_sbb_memory_source_operands(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xFFFFFFFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov DWORD PTR a, 0FFFFFFFFh", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov DWORD PTR b, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, 0", 2U},
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "stc", 3U},
        {VM_IR_OPCODE_ADC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "adc eax, DWORD PTR a", 4U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 5U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "mov ebx, 5", 5U},
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "stc", 6U},
        {VM_IR_OPCODE_SBB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, "main.asm", 8U, "sbb ebx, DWORD PTR b", 7U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for memory-source ADC/SBB tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "memory-source ADC/SBB program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov first memory source should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov second memory source should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov eax before memory-source ADC should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before memory-source ADC should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ADC with memory source should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after memory-source ADC");
    failures += expect_u32(eax, 0U, "ADC with memory source should wrap to zero");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ADC with FFFFFFFFh memory source and carry should set CF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov ebx before memory-source SBB should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before memory-source SBB should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SBB with memory source should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed after memory-source SBB");
    failures += expect_u32(ebx, 3U, "SBB with memory source should subtract source and borrow");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "SBB memory source without borrow-out should clear CF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies SBB signed-overflow flag behavior for supported widths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sbb_signed_overflow_edge_case(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_CLC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "clc", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov al, 80h", 1U},
        {VM_IR_OPCODE_SBB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "sbb al, 1", 2U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for SBB overflow tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "SBB overflow program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "CLC before SBB overflow should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov AL before SBB overflow should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SBB signed overflow case should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after SBB overflow case");
    failures += expect_u32(eax, 0x0000007FU, "SBB 80h - 1 should produce 7Fh at 8-bit width");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "SBB 80h - 1 should not borrow in unsigned arithmetic");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "SBB 7Fh result should clear SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "SBB most-negative minus one should set OF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies carry-flag control instructions mutate only CF.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_carry_flag_control_instructions(void) {
    int failures = 0;
    Vm vm;
    const VmExecDelta *delta = NULL;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_CMC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "cmc", 1U},
        {VM_IR_OPCODE_CMC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "cmc", 2U},
        {VM_IR_OPCODE_CLC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "clc", 3U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for carry-control tests");
    failures += vm_cpu_set_flag(&vm.cpu, VM_FLAG_ZF) ? 0 : record_failure("ZF set should succeed before carry-control tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "carry-control program should load");
    failures += vm_cpu_set_flag(&vm.cpu, VM_FLAG_ZF) ? 0 : record_failure("ZF set should succeed after load for carry-control tests");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "STC should set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "STC should preserve ZF");
    delta = vm_last_delta(&vm);
    failures += expect_size(delta != NULL ? delta->flag_change_count : 99U, 1U, "STC should report only one flag change from initial state");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "CMC should clear set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "CMC should clear set CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "CMC should preserve ZF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "CMC should set clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "CMC should set clear CF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "CLC should execute");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "CLC should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "CLC should preserve ZF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies unsupported Phase 21 operand combinations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase21_error_paths(void) {
    int failures = 0;
    Vm vm;
    const VmIrInstruction adc_memory_memory[] = {
        {VM_IR_OPCODE_ADC, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "adc a, b", 0U}
    };
    const VmIrInstruction sbb_width_mismatch[] = {
        {VM_IR_OPCODE_SBB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sbb eax, al", 0U}
    };
    const VmIrInstruction clc_has_operand[] = {
        {VM_IR_OPCODE_CLC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "clc eax", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for Phase 21 error tests");
    failures += expect_status(vm_load_program(&vm, adc_memory_memory, 1U), VM_EXEC_STATUS_OK, "ADC memory-memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "ADC memory-memory should be unsupported");
    failures += expect_status(vm_load_program(&vm, sbb_width_mismatch, 1U), VM_EXEC_STATUS_OK, "SBB width mismatch program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "SBB width mismatch should be unsupported");
    failures += expect_status(vm_load_program(&vm, clc_has_operand, 1U), VM_EXEC_STATUS_OK, "CLC operand program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "CLC with operand should be unsupported");

    vm_deinit(&vm);
    return failures;
}

/// Verifies unsupported Phase 20 operand combinations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase20_error_paths(void) {
    int failures = 0;
    Vm vm;
    const VmIrInstruction xchg_immediate[] = {
        {VM_IR_OPCODE_XCHG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "xchg eax, 1", 0U}
    };
    const VmIrInstruction xchg_width_mismatch[] = {
        {VM_IR_OPCODE_XCHG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "xchg eax, al", 0U}
    };
    const VmIrInstruction xchg_memory_memory[] = {
        {VM_IR_OPCODE_XCHG, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "xchg a, b", 0U}
    };
    const VmIrInstruction neg_has_source[] = {
        {VM_IR_OPCODE_NEG, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "neg eax, ebx", 0U}
    };
    const VmIrInstruction nop_has_operand[] = {
        {VM_IR_OPCODE_NOP, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "nop eax", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for Phase 20 error tests");
    failures += expect_status(vm_load_program(&vm, xchg_immediate, 1U), VM_EXEC_STATUS_OK, "xchg immediate program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "xchg immediate source should be unsupported");
    failures += expect_status(vm_load_program(&vm, xchg_width_mismatch, 1U), VM_EXEC_STATUS_OK, "xchg width mismatch program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "xchg width mismatch should be unsupported");
    failures += expect_status(vm_load_program(&vm, xchg_memory_memory, 1U), VM_EXEC_STATUS_OK, "xchg memory-memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "xchg memory-memory should be unsupported");
    failures += expect_status(vm_load_program(&vm, neg_has_source, 1U), VM_EXEC_STATUS_OK, "neg with source program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "neg with source should be unsupported");
    failures += expect_status(vm_load_program(&vm, nop_has_operand, 1U), VM_EXEC_STATUS_OK, "nop with operand program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "nop with operand should be unsupported");

    vm_deinit(&vm);
    return failures;
}


/// Verifies TEST updates flags from a transient AND result without changing registers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_test_register_immediate_flags_and_non_mutation(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmExecDelta *delta = NULL;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x0F0F0F0FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "test eax, 0F0F0F0Fh", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for TEST register/immediate");
    failures += expect_status(vm_load_program(&vm, program, 1U), VM_EXEC_STATUS_OK, "TEST register/immediate program should load");
    failures += (vm_cpu_write_register(&vm.cpu, VM_REGISTER_EAX, 0xF0F0F0F0U) ? 0 : record_failure("EAX setup should succeed"));
    failures += (vm_cpu_set_flag(&vm.cpu, VM_FLAG_CF) ? 0 : record_failure("CF setup should succeed"));
    failures += (vm_cpu_set_flag(&vm.cpu, VM_FLAG_OF) ? 0 : record_failure("OF setup should succeed"));

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "TEST register/immediate should execute");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after TEST"));
    failures += expect_u32(eax, 0xF0F0F0F0U, "TEST should not modify register operand");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "zero TEST result should set ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "zero TEST result should clear SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "TEST should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "TEST should clear OF");

    delta = vm_last_delta(&vm);
    if (delta == NULL) {
        failures += record_failure("TEST should produce a delta");
    } else {
        failures += expect_size(delta->register_change_count, 0U, "TEST should not report register changes");
        failures += expect_size(delta->memory_change_count, 0U, "TEST register/immediate should not report memory changes");
        if (find_flag_change(delta, VM_FLAG_ZF) == NULL) {
            failures += record_failure("TEST delta should include ZF change");
        }
        if (find_flag_change(delta, VM_FLAG_CF) == NULL) {
            failures += record_failure("TEST delta should include CF clear");
        }
        if (find_flag_change(delta, VM_FLAG_OF) == NULL) {
            failures += record_failure("TEST delta should include OF clear");
        }
    }

    vm_deinit(&vm);
    return failures;
}

/// Verifies TEST handles 8-bit sign results without mutating aliases.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_test_alias_sign_flag_edge_case(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "test al, 80h", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for TEST alias");
    failures += expect_status(vm_load_program(&vm, program, 1U), VM_EXEC_STATUS_OK, "TEST alias program should load");
    failures += (vm_cpu_write_register(&vm.cpu, VM_REGISTER_EAX, 0x12345680U) ? 0 : record_failure("EAX setup should succeed for TEST alias"));
    failures += (vm_cpu_set_flag(&vm.cpu, VM_FLAG_CF) ? 0 : record_failure("CF setup should succeed for TEST alias"));
    failures += (vm_cpu_set_flag(&vm.cpu, VM_FLAG_OF) ? 0 : record_failure("OF setup should succeed for TEST alias"));

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "TEST alias should execute");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after TEST alias"));
    failures += expect_u32(eax, 0x12345680U, "TEST alias should not modify EAX");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "nonzero TEST alias result should clear ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "8-bit sign-bit TEST result should set SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "TEST alias should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "TEST alias should clear OF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies TEST register/memory and memory/immediate forms do not mutate memory.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_test_memory_forms_and_non_mutation(void) {
    int failures = 0;
    Vm vm;
    uint32_t value = 0U;
    const uint32_t address = VM_MEMORY_DEFAULT_DATA_BASE;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "test eax, ebx", 0U},
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, address, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "test eax, value", 1U},
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, address, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "test value, eax", 2U},
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, address, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "test value, 80000000h", 3U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for TEST memory forms");
    failures += expect_status(vm_memory_write_u32(&vm.memory, address, 0x80000001U, NULL) == VM_MEMORY_STATUS_OK ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_MEMORY_ERROR, VM_EXEC_STATUS_OK, "memory setup should succeed for TEST");
    vm_memory_clear_changes(&vm.memory);
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "TEST memory forms program should load");
    failures += (vm_cpu_write_register(&vm.cpu, VM_REGISTER_EAX, 0x80000001U) ? 0 : record_failure("EAX setup should succeed for TEST memory forms"));
    failures += (vm_cpu_write_register(&vm.cpu, VM_REGISTER_EBX, 0x00000001U) ? 0 : record_failure("EBX setup should succeed for TEST memory forms"));

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "TEST reg/reg should execute");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "TEST reg/reg should clear ZF for nonzero result");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "TEST reg/mem should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "TEST mem/reg should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "TEST mem/imm should execute");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "TEST mem/imm should set SF from bit 31");
    failures += expect_status(vm_memory_read_u32(&vm.memory, address, &value, NULL) == VM_MEMORY_STATUS_OK ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_MEMORY_ERROR, VM_EXEC_STATUS_OK, "memory read should succeed after TEST");
    failures += expect_u32(value, 0x80000001U, "TEST should not modify memory operand");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "TEST memory/immediate should not record memory writes");

    vm_deinit(&vm);
    return failures;
}

/// Verifies invalid hardcoded TEST operands are rejected by the executor.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_test_error_paths(void) {
    int failures = 0;
    Vm vm;
    const VmIrInstruction immediate_destination[] = {
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "test 1, eax", 0U}
    };
    const VmIrInstruction width_mismatch[] = {
        {VM_IR_OPCODE_TEST, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "test eax, al", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for TEST error paths");
    failures += expect_status(vm_load_program(&vm, immediate_destination, 1U), VM_EXEC_STATUS_OK, "TEST immediate-destination program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "TEST immediate destination should be unsupported");
    failures += expect_status(vm_load_program(&vm, width_mismatch, 1U), VM_EXEC_STATUS_OK, "TEST width-mismatch program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "TEST width mismatch should be unsupported");

    vm_deinit(&vm);
    return failures;
}


/// Verifies INC and DEC register edge cases and carry preservation.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_inc_dec_register_flags_and_carry_preservation(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x7FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov al, 7Fh", 1U},
        {VM_IR_OPCODE_INC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "inc al", 2U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "mov al, 80h", 3U},
        {VM_IR_OPCODE_DEC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "dec al", 4U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x7FFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "mov ax, 7FFFh", 5U},
        {VM_IR_OPCODE_INC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "inc ax", 6U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 8U, "mov ax, 8000h", 7U},
        {VM_IR_OPCODE_DEC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 9U, "dec ax", 8U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xFFFFFFFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 10U, "mov eax, 0FFFFFFFFh", 9U},
        {VM_IR_OPCODE_INC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 11U, "inc eax", 10U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 12U, "mov eax, 0", 11U},
        {VM_IR_OPCODE_DEC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 13U, "dec eax", 12U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for INC/DEC register tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "INC/DEC register program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before INC/DEC should execute");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov AL 7Fh should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "inc AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after inc AL");
    failures += expect_u32(eax, 0x00000080U, "inc AL should wrap 7Fh to 80h within the low byte");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "inc AL should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "inc AL 7Fh should set OF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "inc AL 7Fh should set SF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov AL 80h should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "dec AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after dec AL");
    failures += expect_u32(eax, 0x0000007FU, "dec AL should wrap 80h to 7Fh within the low byte");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "dec AL should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "dec AL 80h should set OF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "dec AL 80h should clear SF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov AX 7FFFh should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "inc AX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after inc AX");
    failures += expect_u32(eax, 0x00008000U, "inc AX should produce 8000h");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "inc AX should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "inc AX 7FFFh should set OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov AX 8000h should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "dec AX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after dec AX");
    failures += expect_u32(eax, 0x00007FFFU, "dec AX should produce 7FFFh");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "dec AX should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "dec AX 8000h should set OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov EAX FFFFFFFFh should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "inc EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after inc EAX");
    failures += expect_u32(eax, 0U, "inc EAX FFFFFFFFh should wrap to zero");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "inc EAX should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "inc EAX FFFFFFFFh should set ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "inc EAX FFFFFFFFh should clear OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "mov EAX zero should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "dec EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after dec EAX");
    failures += expect_u32(eax, 0xFFFFFFFFU, "dec EAX zero should wrap to FFFFFFFFh");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "dec EAX should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "dec EAX zero should set SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "dec EAX zero should clear ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "dec EAX zero should clear OF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies INC and DEC memory destinations use checked read-modify-write helpers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_inc_dec_memory_destinations_and_errors(void) {
    int failures = 0;
    Vm vm;
    uint8_t memory_byte = 0U;
    uint32_t memory_dword = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x7FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov BYTE PTR value, 7Fh", 0U},
        {VM_IR_OPCODE_INC, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "inc BYTE PTR value", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov DWORD PTR other, 0", 2U},
        {VM_IR_OPCODE_DEC, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "dec DWORD PTR other", 3U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_INC, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "inc DWORD PTR [const]", 1U}
    };
    const VmIrInstruction malformed[] = {
        {VM_IR_OPCODE_INC, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "inc eax, 1", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for INC/DEC memory tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "INC/DEC memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "memory byte setup should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "INC memory byte should execute");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after INC");
    failures += expect_u32((uint32_t)memory_byte, 0x80U, "INC memory byte should store 80h");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 1U, "INC memory byte should report one byte change");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "INC memory byte 7Fh should set OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "memory dword setup should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "DEC memory dword should execute");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after DEC");
    failures += expect_u32(memory_dword, 0xFFFFFFFFU, "DEC memory dword should wrap zero to FFFFFFFFh");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 4U, "DEC memory dword should report four byte changes");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "DEC memory dword zero should set SF");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for INC const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "INC const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed INC");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_MEMORY_ERROR, "INC .CONST memory should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed INC .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed INC .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for malformed INC test");
    failures += expect_status(vm_load_program(&vm, malformed, 1U), VM_EXEC_STATUS_OK, "malformed INC program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "INC with a source operand should be unsupported by executor");
    vm_deinit(&vm);

    return failures;
}

/// Verifies AND, OR, and XOR register and flag behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_logical_binary_register_flags(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x0000F0F0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov eax, 0F0F0h", 1U},
        {VM_IR_OPCODE_AND, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x000000FFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "and eax, 00FFh", 2U},
        {VM_IR_OPCODE_OR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000100U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "or eax, 0100h", 3U},
        {VM_IR_OPCODE_XOR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x0000000FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "xor eax, 000Fh", 4U},
        {VM_IR_OPCODE_XOR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "xor eax, eax", 5U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "mov eax, 80000001h", 6U},
        {VM_IR_OPCODE_AND, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 8U, "and eax, 80000000h", 7U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for logical register tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "logical register program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before logical tests should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "logical setup MOV should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "AND immediate should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "OR immediate should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "XOR immediate should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after acceptance logical sequence");
    failures += expect_u32(eax, 0x000001FFU, "acceptance logical sequence should leave EAX = 000001FFh");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "logical sequence should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "logical sequence should clear OF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "logical sequence should clear ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "logical sequence should clear SF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "XOR EAX,EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after XOR self");
    failures += expect_u32(eax, 0U, "XOR EAX,EAX should clear EAX");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "XOR EAX,EAX should set ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "XOR EAX,EAX should clear SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "XOR EAX,EAX should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "XOR EAX,EAX should clear OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "sign-bit setup MOV should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "AND sign-bit immediate should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after sign-bit AND");
    failures += expect_u32(eax, 0x80000000U, "AND sign-bit immediate should leave sign bit only");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "AND sign-bit result should set SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "AND sign-bit result should clear ZF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies AND, OR, and XOR alias and memory destination behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_logical_binary_alias_and_memory_destinations(void) {
    int failures = 0;
    Vm vm;
    uint8_t memory_byte = 0U;
    uint32_t memory_dword = 0U;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 12345678h", 0U},
        {VM_IR_OPCODE_XOR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0xFFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "xor al, 0FFh", 1U},
        {VM_IR_OPCODE_OR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "or ax, 8000h", 2U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0xF0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "mov BYTE PTR value, 0F0h", 3U},
        {VM_IR_OPCODE_AND, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x0FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "and BYTE PTR value, 0Fh", 4U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00FF00FFU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "mov DWORD PTR other, 00FF00FFh", 5U},
        {VM_IR_OPCODE_OR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 7U, "or DWORD PTR other, eax", 6U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for logical memory tests");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "logical memory program should load");
    while (!vm.halted) {
        VmExecStatus status = vm_step(&vm);
        if (status != VM_EXEC_STATUS_OK && status != VM_EXEC_STATUS_HALTED) {
            failures += expect_status(status, VM_EXEC_STATUS_OK, "logical memory program should execute without runtime errors");
            break;
        }
    }

    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after alias logical operations");
    failures += expect_u32(eax, 0x1234D687U, "logical alias operations should mutate only selected register widths");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after AND");
    failures += expect_u32((uint32_t)memory_byte, 0U, "AND memory byte should store zero");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after OR");
    failures += expect_u32(memory_dword, 0x12FFD6FFU, "OR memory dword should combine register bits");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "logical memory operations should clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "logical memory operations should clear OF");

    vm_deinit(&vm);
    return failures;
}

/// Verifies logical binary executor error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_logical_binary_error_paths(void) {
    int failures = 0;
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint32_t ebx = 0U;
    const VmIrInstruction memory_to_memory[] = {
        {VM_IR_OPCODE_AND, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "and value, other", 0U}
    };
    const VmIrInstruction invalid_destination[] = {
        {VM_IR_OPCODE_OR, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "or 1, eax", 0U}
    };
    const VmIrInstruction invalid_address[] = {
        {VM_IR_OPCODE_XOR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "xor DWORD PTR [0], 1", 0U}
    };
    const VmIrInstruction invalid_source_address[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov ebx, 12345678h", 1U},
        {VM_IR_OPCODE_AND, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "and ebx, DWORD PTR [0]", 2U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_OR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "or DWORD PTR [const], 1", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for logical memory-to-memory error test");
    failures += expect_status(vm_load_program(&vm, memory_to_memory, 1U), VM_EXEC_STATUS_OK, "logical memory-to-memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "logical memory-to-memory operands should be rejected by executor");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for logical invalid-destination test");
    failures += expect_status(vm_load_program(&vm, invalid_destination, 1U), VM_EXEC_STATUS_OK, "logical invalid-destination program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "logical immediate destination should be rejected by executor");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for logical invalid-address test");
    failures += expect_status(vm_load_program(&vm, invalid_address, 1U), VM_EXEC_STATUS_OK, "logical invalid-address program should load");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "logical invalid memory destination should fail through checked memory");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed logical invalid-address write should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for logical invalid source-address test");
    failures += expect_status(vm_load_program(&vm, invalid_source_address, sizeof(invalid_source_address) / sizeof(invalid_source_address[0])), VM_EXEC_STATUS_OK, "logical invalid source-address program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before logical invalid source-address should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before logical invalid source-address should execute");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "logical invalid source memory should fail through checked memory");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed after invalid logical source read");
    failures += expect_u32(ebx, 0x12345678U, "failed logical source read should not mutate destination register");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed logical source read should not clear CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed logical source read should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for logical const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "logical const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed logical const write");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_MEMORY_ERROR, "logical .CONST write should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed logical .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed logical .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    return failures;
}

/// Verifies NOT register destinations and exact flag preservation.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_not_register_destinations_preserve_flags(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12340000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 12340000h", 0U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "not al", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000F0FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, 0F0Fh", 2U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "not ax", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, 0", 4U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "not eax", 5U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for NOT register test");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "NOT register program should load");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup should succeed before NOT register test");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, true) ? 0 : record_failure("ZF setup should succeed before NOT register test");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup should succeed before NOT register test");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup should succeed before NOT register test");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before NOT AL should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before NOT AL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, true) ? 0 : record_failure("ZF setup before NOT AL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before NOT AL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before NOT AL should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "NOT AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after NOT AL should succeed");
    failures += expect_u32(eax, 0x123400FFU, "NOT AL should complement only the low byte");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "NOT AL should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "NOT AL should preserve ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "NOT AL should preserve SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "NOT AL should preserve OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before NOT AX should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, false) ? 0 : record_failure("CF setup before NOT AX should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, false) ? 0 : record_failure("ZF setup before NOT AX should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, false) ? 0 : record_failure("SF setup before NOT AX should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, false) ? 0 : record_failure("OF setup before NOT AX should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "NOT AX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after NOT AX should succeed");
    failures += expect_u32(eax, 0x0000F0F0U, "NOT AX should complement 0F0Fh to F0F0h");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "NOT AX should preserve clear CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "NOT AX should preserve clear ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "NOT AX should preserve clear SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "NOT AX should preserve clear OF");

    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before NOT EAX should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, true) ? 0 : record_failure("ZF setup before NOT EAX should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "NOT EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after NOT EAX should succeed");
    failures += expect_u32(eax, 0xFFFFFFFFU, "NOT EAX should complement 00000000h to FFFFFFFFh");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "NOT EAX should preserve ZF despite nonzero result");

    vm_deinit(&vm);
    return failures;
}

/// Verifies NOT memory destination behavior and runtime errors.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_not_memory_destinations_and_errors(void) {
    int failures = 0;
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint8_t memory_byte = 0U;
    uint16_t memory_word = 0U;
    uint32_t memory_dword = 0U;
    const VmIrInstruction memory_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x00U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov BYTE PTR b, 0", 0U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "not BYTE PTR b", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x0F0FU, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov WORD PTR w, 0F0Fh", 2U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "not WORD PTR w", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov DWORD PTR d, 0", 4U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "not DWORD PTR d", 5U}
    };
    const VmIrInstruction invalid_address[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "not DWORD PTR [0]", 1U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_NOT, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "not DWORD PTR [const]", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for NOT memory test");
    failures += expect_status(vm_load_program(&vm, memory_program, sizeof(memory_program) / sizeof(memory_program[0])), VM_EXEC_STATUS_OK, "NOT memory program should load");
    while (!vm.halted) {
        status = vm_step(&vm);
        if (status != VM_EXEC_STATUS_OK && status != VM_EXEC_STATUS_HALTED) {
            failures += expect_status(status, VM_EXEC_STATUS_OK, "NOT memory program should execute without runtime errors");
            break;
        }
    }
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after NOT");
    failures += expect_u32((uint32_t)memory_byte, 0xFFU, "NOT byte memory should store FFh");
    failures += vm_memory_read_u16(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 2U, &memory_word, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory word read should succeed after NOT");
    failures += expect_u32((uint32_t)memory_word, 0xF0F0U, "NOT word memory should store F0F0h");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after NOT");
    failures += expect_u32(memory_dword, 0xFFFFFFFFU, "NOT dword memory should store FFFFFFFFh");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for NOT invalid-address test");
    failures += expect_status(vm_load_program(&vm, invalid_address, sizeof(invalid_address) / sizeof(invalid_address[0])), VM_EXEC_STATUS_OK, "NOT invalid-address program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed NOT invalid-address read");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "NOT invalid memory destination should fail through checked memory read");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed NOT invalid-address read should preserve CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed NOT invalid-address read should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for NOT const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "NOT const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed NOT const write");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "NOT .CONST write should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed NOT .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed NOT .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    return failures;
}


/// Verifies SHL/SAL register destinations, counts, and modeled flags.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_shift_left_register_flags_and_counts(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction single_bit_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000080U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 80h", 0U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shl al, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00008000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, 8000h", 2U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "shl ax, 1", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, 80000000h", 4U},
        {VM_IR_OPCODE_SAL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "sal eax, 1", 5U}
    };
    const VmIrInstruction zero_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 12345678h", 0U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 32U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shl eax, 32", 1U}
    };
    const VmIrInstruction cl_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ECX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000102U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov ecx, 102h", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov eax, 1", 1U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 8U, 0U, VM_REGISTER_CL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "shl eax, cl", 2U}
    };
    const VmIrInstruction oversized_byte_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 1", 0U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 8U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shl al, 8", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for single-bit shift test");
    failures += expect_status(vm_load_program(&vm, single_bit_program, sizeof(single_bit_program) / sizeof(single_bit_program[0])), VM_EXEC_STATUS_OK, "single-bit shift program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before SHL AL should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHL AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SHL AL should succeed");
    failures += expect_u32(eax, 0x00000000U, "SHL AL should clear AL after shifting out bit 7");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "SHL AL by one should set CF from old sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "SHL AL by one should set ZF for zero result");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "SHL AL by one should clear SF for zero result");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "SHL AL by one should set OF to new sign XOR CF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before SHL AX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHL AX should execute");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "SHL AX by one should set CF from old sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "SHL AX by one should set OF to new sign XOR CF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before SAL EAX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAL EAX should execute as SHL alias");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SAL EAX should succeed");
    failures += expect_u32(eax, 0x00000000U, "SAL EAX should shift left exactly like SHL");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "SAL EAX by one should set CF from old sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "SAL EAX by one should set OF to new sign XOR CF");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for zero-count shift test");
    failures += expect_status(vm_load_program(&vm, zero_count_program, sizeof(zero_count_program) / sizeof(zero_count_program[0])), VM_EXEC_STATUS_OK, "zero-count shift program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before zero-count SHL should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before zero-count SHL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, false) ? 0 : record_failure("ZF setup before zero-count SHL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before zero-count SHL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before zero-count SHL should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHL EAX, 32 should be a count-zero no-op");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after zero-count SHL should succeed");
    failures += expect_u32(eax, 0x12345678U, "zero effective count should not change EAX");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "zero effective count should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "zero effective count should preserve ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "zero effective count should preserve SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "zero effective count should preserve OF");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for CL shift test");
    failures += expect_status(vm_load_program(&vm, cl_count_program, sizeof(cl_count_program) / sizeof(cl_count_program[0])), VM_EXEC_STATUS_OK, "CL shift program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV ECX before CL shift should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV EAX before CL shift should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before CL shift should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHL EAX, CL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SHL EAX, CL should succeed");
    failures += expect_u32(eax, 4U, "SHL EAX, CL should use only CL as count");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "multi-bit SHL should preserve undefined OF deterministically");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for oversized byte shift test");
    failures += expect_status(vm_load_program(&vm, oversized_byte_program, sizeof(oversized_byte_program) / sizeof(oversized_byte_program[0])), VM_EXEC_STATUS_OK, "oversized byte shift program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before oversized byte shift should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before oversized byte shift should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before oversized byte shift should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHL AL, 8 should execute in deterministic default semantics");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SHL AL, 8 should succeed");
    failures += expect_u32(eax, 0U, "SHL AL, 8 should deterministically clear AL");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "oversized byte shift should preserve undefined CF deterministically");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "oversized byte shift should preserve undefined OF deterministically");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "oversized byte shift should update ZF from result");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "oversized byte shift should update SF from result");
    vm_deinit(&vm);

    return failures;
}

/// Verifies SHL/SAL memory destinations and executor error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_shift_left_memory_destinations_and_errors(void) {
    int failures = 0;
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint8_t memory_byte = 0U;
    uint16_t memory_word = 0U;
    uint32_t memory_dword = 0U;
    const VmIrInstruction memory_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov BYTE PTR b, 80h", 0U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shl BYTE PTR b, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x0001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov WORD PTR w, 1", 2U},
        {VM_IR_OPCODE_SAL, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 4U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "sal WORD PTR w, 4", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x40000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov DWORD PTR d, 40000000h", 4U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "shl DWORD PTR d, 1", 5U}
    };
    const VmIrInstruction invalid_count[] = {
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 256U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "shl eax, 256", 0U}
    };
    const VmIrInstruction invalid_address[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shl DWORD PTR [0], 1", 1U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_SHL, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shl DWORD PTR [const], 1", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for shift memory test");
    failures += expect_status(vm_load_program(&vm, memory_program, sizeof(memory_program) / sizeof(memory_program[0])), VM_EXEC_STATUS_OK, "shift memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "shift memory byte initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "shift memory byte instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "shift memory word initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "shift memory word instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "shift memory dword initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "shift memory dword instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "shift memory program should halt after all instructions");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after SHL");
    failures += expect_u32((uint32_t)memory_byte, 0x00U, "SHL byte memory should store 00h");
    failures += vm_memory_read_u16(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 2U, &memory_word, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory word read should succeed after SAL");
    failures += expect_u32((uint32_t)memory_word, 0x0010U, "SAL word memory should shift by four");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after SHL");
    failures += expect_u32(memory_dword, 0x80000000U, "SHL dword memory should store shifted value");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid shift count test");
    failures += expect_status(vm_load_program(&vm, invalid_count, 1U), VM_EXEC_STATUS_OK, "invalid shift count program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "executor should reject malformed immediate shift count");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid shift address test");
    failures += expect_status(vm_load_program(&vm, invalid_address, sizeof(invalid_address) / sizeof(invalid_address[0])), VM_EXEC_STATUS_OK, "invalid shift address program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before failed shift should execute");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "SHL invalid memory destination should fail through checked memory read");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed SHL invalid-address read should preserve CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed SHL invalid-address read should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for shift const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "shift const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed shift const write");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "SHL .CONST write should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed SHL .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed SHL .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    return failures;
}


/// Verifies SHR register destinations, counts, and modeled flags.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_shift_right_register_flags_and_counts(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction single_bit_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 1", 0U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shr al, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00008000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, 8000h", 2U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "shr ax, 1", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, 80000000h", 4U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "shr eax, 1", 5U}
    };
    const VmIrInstruction zero_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 12345678h", 0U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 32U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shr eax, 32", 1U}
    };
    const VmIrInstruction cl_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ECX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000102U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov ecx, 102h", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov eax, 80000000h", 1U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 8U, 0U, VM_REGISTER_CL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "shr eax, cl", 2U}
    };
    const VmIrInstruction oversized_byte_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000080U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 80h", 0U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 8U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shr al, 8", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for single-bit SHR test");
    failures += expect_status(vm_load_program(&vm, single_bit_program, sizeof(single_bit_program) / sizeof(single_bit_program[0])), VM_EXEC_STATUS_OK, "single-bit SHR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before SHR AL should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SHR AL should succeed");
    failures += expect_u32(eax, 0x00000000U, "SHR AL should clear AL after shifting out bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "SHR AL by one should set CF from old bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "SHR AL by one should set ZF for zero result");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "SHR AL by one should clear SF for zero result");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "SHR AL by one should set OF from original sign bit");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before SHR AX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR AX should execute");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "SHR AX by one should set CF from old bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "SHR AX by one should set OF from original sign bit");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before SHR EAX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SHR EAX should succeed");
    failures += expect_u32(eax, 0x40000000U, "SHR EAX should fill high bit with zero");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "SHR EAX by one should set CF from old bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "SHR EAX by one should set OF from original sign bit");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for zero-count SHR test");
    failures += expect_status(vm_load_program(&vm, zero_count_program, sizeof(zero_count_program) / sizeof(zero_count_program[0])), VM_EXEC_STATUS_OK, "zero-count SHR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before zero-count SHR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before zero-count SHR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, false) ? 0 : record_failure("ZF setup before zero-count SHR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before zero-count SHR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before zero-count SHR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR EAX, 32 should be a count-zero no-op");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after zero-count SHR should succeed");
    failures += expect_u32(eax, 0x12345678U, "zero effective count should not change EAX for SHR");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "zero effective count should preserve CF for SHR");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "zero effective count should preserve ZF for SHR");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "zero effective count should preserve SF for SHR");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "zero effective count should preserve OF for SHR");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for CL SHR test");
    failures += expect_status(vm_load_program(&vm, cl_count_program, sizeof(cl_count_program) / sizeof(cl_count_program[0])), VM_EXEC_STATUS_OK, "CL SHR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV ECX before CL SHR should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV EAX before CL SHR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before CL SHR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR EAX, CL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SHR EAX, CL should succeed");
    failures += expect_u32(eax, 0x20000000U, "SHR EAX, CL should use only CL as count");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "multi-bit SHR should preserve undefined OF deterministically");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for oversized byte SHR test");
    failures += expect_status(vm_load_program(&vm, oversized_byte_program, sizeof(oversized_byte_program) / sizeof(oversized_byte_program[0])), VM_EXEC_STATUS_OK, "oversized byte SHR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before oversized byte SHR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before oversized byte SHR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before oversized byte SHR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR AL, 8 should execute in deterministic default semantics");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SHR AL, 8 should succeed");
    failures += expect_u32(eax, 0U, "SHR AL, 8 should deterministically clear AL");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "oversized byte SHR should preserve undefined CF deterministically");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "oversized byte SHR should preserve undefined OF deterministically");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "oversized byte SHR should update ZF from result");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, false, "oversized byte SHR should update SF from result");
    vm_deinit(&vm);

    return failures;
}

/// Verifies SHR memory destinations and executor error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_shift_right_memory_destinations_and_errors(void) {
    int failures = 0;
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint8_t memory_byte = 0U;
    uint16_t memory_word = 0U;
    uint32_t memory_dword = 0U;
    const VmIrInstruction memory_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov BYTE PTR b, 80h", 0U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shr BYTE PTR b, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov WORD PTR w, 8000h", 2U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 4U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "shr WORD PTR w, 4", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov DWORD PTR d, 80000000h", 4U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "shr DWORD PTR d, 1", 5U}
    };
    const VmIrInstruction invalid_count[] = {
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 256U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "shr eax, 256", 0U}
    };
    const VmIrInstruction invalid_address[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shr DWORD PTR [0], 1", 1U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_SHR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "shr DWORD PTR [const], 1", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for SHR memory test");
    failures += expect_status(vm_load_program(&vm, memory_program, sizeof(memory_program) / sizeof(memory_program[0])), VM_EXEC_STATUS_OK, "SHR memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR memory byte initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR memory byte instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR memory word initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR memory word instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR memory dword initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SHR memory dword instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "SHR memory program should halt after all instructions");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after SHR");
    failures += expect_u32((uint32_t)memory_byte, 0x40U, "SHR byte memory should store 40h");
    failures += vm_memory_read_u16(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 2U, &memory_word, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory word read should succeed after SHR");
    failures += expect_u32((uint32_t)memory_word, 0x0800U, "SHR word memory should shift by four");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after SHR");
    failures += expect_u32(memory_dword, 0x40000000U, "SHR dword memory should store shifted value");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid SHR count test");
    failures += expect_status(vm_load_program(&vm, invalid_count, 1U), VM_EXEC_STATUS_OK, "invalid SHR count program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "executor should reject malformed immediate SHR count");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid SHR address test");
    failures += expect_status(vm_load_program(&vm, invalid_address, sizeof(invalid_address) / sizeof(invalid_address[0])), VM_EXEC_STATUS_OK, "invalid SHR address program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before failed SHR should execute");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "SHR invalid memory destination should fail through checked memory read");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed SHR invalid-address read should preserve CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed SHR invalid-address read should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for SHR const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "SHR const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed SHR const write");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "SHR .CONST write should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed SHR .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed SHR .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    return failures;
}

/// Verifies SAR register destinations, counts, and modeled flags.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_shift_arithmetic_right_register_flags_and_counts(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction single_bit_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000080U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 80h", 0U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sar al, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov eax, 1", 2U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "sar al, 1", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, 80000000h", 4U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "sar eax, 1", 5U}
    };
    const VmIrInstruction zero_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 12345678h", 0U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 32U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sar eax, 32", 1U}
    };
    const VmIrInstruction cl_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ECX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000102U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov ecx, 102h", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov eax, 80000000h", 1U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 8U, 0U, VM_REGISTER_CL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "sar eax, cl", 2U}
    };
    const VmIrInstruction oversized_byte_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000080U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 80h", 0U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 8U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sar al, 8", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for single-bit SAR test");
    failures += expect_status(vm_load_program(&vm, single_bit_program, sizeof(single_bit_program) / sizeof(single_bit_program[0])), VM_EXEC_STATUS_OK, "single-bit SAR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before negative SAR AL should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR negative AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after negative SAR AL should succeed");
    failures += expect_u32(eax, 0x000000C0U, "SAR AL should fill high bits with the original sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, false, "SAR negative AL by one should set CF from old bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "SAR negative AL by one should clear ZF for nonzero result");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "SAR negative AL by one should set SF from result sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "SAR by one should clear OF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before positive SAR AL should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR positive AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after positive SAR AL should succeed");
    failures += expect_u32(eax, 0U, "SAR AL 01h by one should produce zero");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "SAR positive AL by one should set CF from old bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "SAR positive AL by one should set ZF for zero result");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "SAR positive AL by one should clear OF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before SAR EAX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SAR EAX should succeed");
    failures += expect_u32(eax, 0xC0000000U, "SAR EAX should sign-fill bit 31");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, false, "SAR EAX by one should clear OF");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for zero-count SAR test");
    failures += expect_status(vm_load_program(&vm, zero_count_program, sizeof(zero_count_program) / sizeof(zero_count_program[0])), VM_EXEC_STATUS_OK, "zero-count SAR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before zero-count SAR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before zero-count SAR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, false) ? 0 : record_failure("ZF setup before zero-count SAR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before zero-count SAR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before zero-count SAR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR EAX, 32 should be a count-zero no-op");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after zero-count SAR should succeed");
    failures += expect_u32(eax, 0x12345678U, "zero effective count should not change EAX for SAR");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "zero effective count should preserve CF for SAR");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "zero effective count should preserve ZF for SAR");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "zero effective count should preserve SF for SAR");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "zero effective count should preserve OF for SAR");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for CL SAR test");
    failures += expect_status(vm_load_program(&vm, cl_count_program, sizeof(cl_count_program) / sizeof(cl_count_program[0])), VM_EXEC_STATUS_OK, "CL SAR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV ECX before CL SAR should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV EAX before CL SAR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before CL SAR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR EAX, CL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SAR EAX, CL should succeed");
    failures += expect_u32(eax, 0xE0000000U, "SAR EAX, CL should use only CL as count and sign-fill");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "multi-bit SAR should preserve undefined OF deterministically");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for oversized byte SAR test");
    failures += expect_status(vm_load_program(&vm, oversized_byte_program, sizeof(oversized_byte_program) / sizeof(oversized_byte_program[0])), VM_EXEC_STATUS_OK, "oversized byte SAR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before oversized byte SAR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before oversized byte SAR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before oversized byte SAR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR AL, 8 should execute in deterministic default semantics");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after SAR AL, 8 should succeed");
    failures += expect_u32(eax, 0x000000FFU, "SAR AL, 8 should deterministically sign-fill AL");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "oversized byte SAR should preserve undefined CF deterministically");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "oversized byte SAR should preserve undefined OF deterministically");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, false, "oversized byte SAR should update ZF from result");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "oversized byte SAR should update SF from result");
    vm_deinit(&vm);

    return failures;
}

/// Verifies SAR memory destinations and executor error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_shift_arithmetic_right_memory_destinations_and_errors(void) {
    int failures = 0;
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint8_t memory_byte = 0U;
    uint16_t memory_word = 0U;
    uint32_t memory_dword = 0U;
    const VmIrInstruction memory_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov BYTE PTR b, 80h", 0U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sar BYTE PTR b, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov WORD PTR w, 8000h", 2U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 4U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "sar WORD PTR w, 4", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov DWORD PTR d, 80000000h", 4U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "sar DWORD PTR d, 1", 5U}
    };
    const VmIrInstruction invalid_count[] = {
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 256U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "sar eax, 256", 0U}
    };
    const VmIrInstruction invalid_address[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sar DWORD PTR [0], 1", 1U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_SAR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "sar DWORD PTR [const], 1", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for SAR memory test");
    failures += expect_status(vm_load_program(&vm, memory_program, sizeof(memory_program) / sizeof(memory_program[0])), VM_EXEC_STATUS_OK, "SAR memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR memory byte initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR memory byte instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR memory word initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR memory word instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR memory dword initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "SAR memory dword instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "SAR memory program should halt after all instructions");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after SAR");
    failures += expect_u32((uint32_t)memory_byte, 0xC0U, "SAR byte memory should store C0h");
    failures += vm_memory_read_u16(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 2U, &memory_word, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory word read should succeed after SAR");
    failures += expect_u32((uint32_t)memory_word, 0xF800U, "SAR word memory should shift by four with sign fill");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after SAR");
    failures += expect_u32(memory_dword, 0xC0000000U, "SAR dword memory should store shifted value");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid SAR count test");
    failures += expect_status(vm_load_program(&vm, invalid_count, 1U), VM_EXEC_STATUS_OK, "invalid SAR count program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "executor should reject malformed immediate SAR count");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid SAR address test");
    failures += expect_status(vm_load_program(&vm, invalid_address, sizeof(invalid_address) / sizeof(invalid_address[0])), VM_EXEC_STATUS_OK, "invalid SAR address program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before failed SAR should execute");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "SAR invalid memory destination should fail through checked memory read");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed SAR invalid-address read should preserve CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed SAR invalid-address read should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for SAR const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "SAR const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed SAR const write");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "SAR .CONST write should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed SAR .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed SAR .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    return failures;
}



/// Verifies ROL register destinations, count policy, and modeled flags.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_rotate_left_register_flags_and_counts(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction single_bit_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x80U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov al, 80h", 0U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "rol al, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov ax, 8001h", 2U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "rol ax, 1", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, 80000001h", 4U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "rol eax, 1", 5U}
    };
    const VmIrInstruction zero_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 12345678h", 0U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 32U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "rol eax, 32", 1U}
    };
    const VmIrInstruction cl_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ECX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000104U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov ecx, 104h", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x10000000U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov eax, 10000000h", 1U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 8U, 0U, VM_REGISTER_CL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "rol eax, cl", 2U}
    };
    const VmIrInstruction full_width_byte_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x81U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov al, 81h", 0U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 8U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "rol al, 8", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for single-bit ROL test");
    failures += expect_status(vm_load_program(&vm, single_bit_program, sizeof(single_bit_program) / sizeof(single_bit_program[0])), VM_EXEC_STATUS_OK, "single-bit ROL program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before ROL AL should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, true) ? 0 : record_failure("ZF setup before ROL AL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before ROL AL should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROL AL should succeed");
    failures += expect_u32(eax, 0x00000001U, "ROL AL should wrap the high bit into bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ROL AL by one should set CF from result bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "ROL AL by one should set OF from new sign xor CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "ROL AL should preserve ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "ROL AL should preserve SF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before ROL AX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL AX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROL AX should succeed");
    failures += expect_u32(eax, 0x00000003U, "ROL AX should rotate within AX width");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before ROL EAX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROL EAX should succeed");
    failures += expect_u32(eax, 0x00000003U, "ROL EAX should rotate within EAX width");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for zero-count ROL test");
    failures += expect_status(vm_load_program(&vm, zero_count_program, sizeof(zero_count_program) / sizeof(zero_count_program[0])), VM_EXEC_STATUS_OK, "zero-count ROL program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before zero-count ROL should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before zero-count ROL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, true) ? 0 : record_failure("ZF setup before zero-count ROL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before zero-count ROL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before zero-count ROL should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL EAX, 32 should be a complete no-op");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after zero-count ROL should succeed");
    failures += expect_u32(eax, 0x12345678U, "ROL EAX, 32 should preserve destination");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ROL EAX, 32 should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "ROL EAX, 32 should preserve ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "ROL EAX, 32 should preserve SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "ROL EAX, 32 should preserve OF");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for CL ROL test");
    failures += expect_status(vm_load_program(&vm, cl_count_program, sizeof(cl_count_program) / sizeof(cl_count_program[0])), VM_EXEC_STATUS_OK, "CL ROL program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV ECX before CL ROL should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV EAX before CL ROL should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before CL ROL should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL EAX, CL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROL EAX, CL should succeed");
    failures += expect_u32(eax, 0x00000001U, "ROL EAX, CL should use only CL as count");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "multi-bit ROL should preserve undefined OF deterministically");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for full-width byte ROL test");
    failures += expect_status(vm_load_program(&vm, full_width_byte_program, sizeof(full_width_byte_program) / sizeof(full_width_byte_program[0])), VM_EXEC_STATUS_OK, "full-width byte ROL program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before full-width byte ROL should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, false) ? 0 : record_failure("CF setup before full-width ROL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before full-width ROL should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL AL, 8 should execute with nonzero rotate flag behavior");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROL AL, 8 should succeed");
    failures += expect_u32(eax, 0x00000081U, "ROL AL, 8 should leave byte bits unchanged");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ROL AL, 8 should set CF from unchanged result bit 0");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "ROL AL, 8 should preserve undefined OF deterministically");
    vm_deinit(&vm);

    return failures;
}

/// Verifies ROL memory destinations and executor error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_rotate_left_memory_destinations_and_errors(void) {
    int failures = 0;
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint8_t memory_byte = 0U;
    uint16_t memory_word = 0U;
    uint32_t memory_dword = 0U;
    const VmIrInstruction memory_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x81U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov BYTE PTR b, 81h", 0U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "rol BYTE PTR b, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov WORD PTR w, 8001h", 2U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 4U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "rol WORD PTR w, 4", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov DWORD PTR d, 80000001h", 4U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "rol DWORD PTR d, 1", 5U}
    };
    const VmIrInstruction invalid_count[] = {
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 256U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "rol eax, 256", 0U}
    };
    const VmIrInstruction invalid_address[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "rol DWORD PTR [0], 1", 1U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_ROL, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "rol DWORD PTR [const], 1", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for ROL memory test");
    failures += expect_status(vm_load_program(&vm, memory_program, sizeof(memory_program) / sizeof(memory_program[0])), VM_EXEC_STATUS_OK, "ROL memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL memory byte initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL memory byte instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL memory word initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL memory word instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL memory dword initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROL memory dword instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "ROL memory program should halt after all instructions");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after ROL");
    failures += expect_u32((uint32_t)memory_byte, 0x03U, "ROL byte memory should store 03h");
    failures += vm_memory_read_u16(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 2U, &memory_word, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory word read should succeed after ROL");
    failures += expect_u32((uint32_t)memory_word, 0x0018U, "ROL word memory should rotate by four");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after ROL");
    failures += expect_u32(memory_dword, 0x00000003U, "ROL dword memory should store rotated value");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid ROL count test");
    failures += expect_status(vm_load_program(&vm, invalid_count, 1U), VM_EXEC_STATUS_OK, "invalid ROL count program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "executor should reject malformed immediate ROL count");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid ROL address test");
    failures += expect_status(vm_load_program(&vm, invalid_address, sizeof(invalid_address) / sizeof(invalid_address[0])), VM_EXEC_STATUS_OK, "invalid ROL address program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before failed ROL should execute");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "ROL invalid memory destination should fail through checked memory read");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed ROL invalid-address read should preserve CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed ROL invalid-address read should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for ROL const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "ROL const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed ROL const write");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "ROL .CONST write should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed ROL .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed ROL .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    return failures;
}

/// Verifies ROR register destinations, count policy, and modeled flags.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_rotate_right_register_flags_and_counts(void) {
    int failures = 0;
    Vm vm;
    uint32_t eax = 0U;
    const VmIrInstruction single_bit_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x01U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov al, 01h", 0U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "ror al, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x0003U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov ax, 0003h", 2U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "ror ax, 1", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000003U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov eax, 00000003h", 4U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "ror eax, 1", 5U}
    };
    const VmIrInstruction zero_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 12345678h", 0U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 32U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "ror eax, 32", 1U}
    };
    const VmIrInstruction cl_count_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ECX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000104U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov ecx, 104h", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x00000010U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "mov eax, 10h", 1U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_REGISTER, 8U, 0U, VM_REGISTER_CL, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "ror eax, cl", 2U}
    };
    const VmIrInstruction full_width_byte_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x81U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov al, 81h", 0U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 8U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "ror al, 8", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for single-bit ROR test");
    failures += expect_status(vm_load_program(&vm, single_bit_program, sizeof(single_bit_program) / sizeof(single_bit_program[0])), VM_EXEC_STATUS_OK, "single-bit ROR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before ROR AL should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, true) ? 0 : record_failure("ZF setup before ROR AL should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before ROR AL should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR AL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROR AL should succeed");
    failures += expect_u32(eax, 0x00000080U, "ROR AL should wrap bit 0 into the high bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ROR AL by one should set CF from result sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "ROR AL by one should set OF from result sign xor second sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "ROR AL should preserve ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "ROR AL should preserve SF");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before ROR AX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR AX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROR AX should succeed");
    failures += expect_u32(eax, 0x00008001U, "ROR AX should rotate within AX width");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before ROR EAX should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR EAX should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROR EAX should succeed");
    failures += expect_u32(eax, 0x80000001U, "ROR EAX should rotate within EAX width");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for zero-count ROR test");
    failures += expect_status(vm_load_program(&vm, zero_count_program, sizeof(zero_count_program) / sizeof(zero_count_program[0])), VM_EXEC_STATUS_OK, "zero-count ROR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before zero-count ROR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, true) ? 0 : record_failure("CF setup before zero-count ROR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_ZF, true) ? 0 : record_failure("ZF setup before zero-count ROR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_SF, true) ? 0 : record_failure("SF setup before zero-count ROR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before zero-count ROR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR EAX, 32 should be a complete no-op");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after zero-count ROR should succeed");
    failures += expect_u32(eax, 0x12345678U, "ROR EAX, 32 should preserve destination");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ROR EAX, 32 should preserve CF");
    failures += expect_flag(&vm.cpu, VM_FLAG_ZF, true, "ROR EAX, 32 should preserve ZF");
    failures += expect_flag(&vm.cpu, VM_FLAG_SF, true, "ROR EAX, 32 should preserve SF");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "ROR EAX, 32 should preserve OF");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for CL ROR test");
    failures += expect_status(vm_load_program(&vm, cl_count_program, sizeof(cl_count_program) / sizeof(cl_count_program[0])), VM_EXEC_STATUS_OK, "CL ROR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV ECX before CL ROR should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV EAX before CL ROR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before CL ROR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR EAX, CL should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROR EAX, CL should succeed");
    failures += expect_u32(eax, 0x00000001U, "ROR EAX, CL should use only CL as count");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "multi-bit ROR should preserve undefined OF deterministically");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for full-width byte ROR test");
    failures += expect_status(vm_load_program(&vm, full_width_byte_program, sizeof(full_width_byte_program) / sizeof(full_width_byte_program[0])), VM_EXEC_STATUS_OK, "full-width byte ROR program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before full-width byte ROR should execute");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_CF, false) ? 0 : record_failure("CF setup before full-width ROR should succeed");
    failures += vm_cpu_write_flag(&vm.cpu, VM_FLAG_OF, true) ? 0 : record_failure("OF setup before full-width ROR should succeed");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR AL, 8 should execute with nonzero rotate flag behavior");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read after ROR AL, 8 should succeed");
    failures += expect_u32(eax, 0x00000081U, "ROR AL, 8 should leave byte bits unchanged");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "ROR AL, 8 should set CF from unchanged result sign bit");
    failures += expect_flag(&vm.cpu, VM_FLAG_OF, true, "ROR AL, 8 should preserve undefined OF deterministically");
    vm_deinit(&vm);

    return failures;
}

/// Verifies ROR memory destinations and executor error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_rotate_right_memory_destinations_and_errors(void) {
    int failures = 0;
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint8_t memory_byte = 0U;
    uint16_t memory_word = 0U;
    uint32_t memory_dword = 0U;
    const VmIrInstruction memory_program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x81U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov BYTE PTR b, 81h", 0U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_MEMORY_ADDRESS, 8U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "ror BYTE PTR b, 1", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 16U, 0x8001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "mov WORD PTR w, 8001h", 2U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_MEMORY_ADDRESS, 16U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 2U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 4U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "ror WORD PTR w, 4", 3U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x80000001U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 5U, "mov DWORD PTR d, 80000001h", 4U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE + 4U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 6U, "ror DWORD PTR d, 1", 5U}
    };
    const VmIrInstruction invalid_count[] = {
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 256U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "ror eax, 256", 0U}
    };
    const VmIrInstruction invalid_address[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "ror DWORD PTR [0], 1", 1U}
    };
    const VmIrInstruction const_write[] = {
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "stc", 0U},
        {VM_IR_OPCODE_ROR, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CONST_BASE, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "ror DWORD PTR [const], 1", 1U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for ROR memory test");
    failures += expect_status(vm_load_program(&vm, memory_program, sizeof(memory_program) / sizeof(memory_program[0])), VM_EXEC_STATUS_OK, "ROR memory program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR memory byte initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR memory byte instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR memory word initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR memory word instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR memory dword initializer should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "ROR memory dword instruction should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "ROR memory program should halt after all instructions");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &memory_byte, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory byte read should succeed after ROR");
    failures += expect_u32((uint32_t)memory_byte, 0xC0U, "ROR byte memory should store C0h");
    failures += vm_memory_read_u16(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 2U, &memory_word, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory word read should succeed after ROR");
    failures += expect_u32((uint32_t)memory_word, 0x1800U, "ROR word memory should rotate by four");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 4U, &memory_dword, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("memory dword read should succeed after ROR");
    failures += expect_u32(memory_dword, 0xC0000000U, "ROR dword memory should store rotated value");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid ROR count test");
    failures += expect_status(vm_load_program(&vm, invalid_count, 1U), VM_EXEC_STATUS_OK, "invalid ROR count program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "executor should reject malformed immediate ROR count");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for invalid ROR address test");
    failures += expect_status(vm_load_program(&vm, invalid_address, sizeof(invalid_address) / sizeof(invalid_address[0])), VM_EXEC_STATUS_OK, "invalid ROR address program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before failed ROR should execute");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "ROR invalid memory destination should fail through checked memory read");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed ROR invalid-address read should preserve CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed ROR invalid-address read should not record memory changes");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for ROR const-write test");
    failures += expect_status(vm_load_program(&vm, const_write, sizeof(const_write) / sizeof(const_write[0])), VM_EXEC_STATUS_OK, "ROR const-write program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC should execute before failed ROR const write");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_MEMORY_ERROR, "ROR .CONST write should fail through checked memory");
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "failed ROR .CONST write should restore CF");
    failures += expect_size(vm_last_delta(&vm)->memory_change_count, 0U, "failed ROR .CONST write should not record successful memory changes");
    vm_deinit(&vm);

    return failures;
}


/// Verifies the Irvine32 exit IR opcode halts without state mutation.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_exit_terminator_halts_without_mutation(void) {
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    const VmExecDelta *delta = NULL;
    uint32_t eax = 0U;
    int failures = 0;
    const VmIrInstruction program[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 123U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "mov eax, 123", 0U},
        {VM_IR_OPCODE_STC, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "stc", 1U},
        {VM_IR_OPCODE_EXIT, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 3U, "exit", 2U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 999U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 4U, "mov eax, 999", 3U}
    };
    const VmIrInstruction malformed_exit[] = {
        {VM_IR_OPCODE_EXIT, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "exit eax", 0U}
    };

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "VM should initialize for EXIT test");
    failures += expect_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "EXIT program should load");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "MOV before EXIT should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "STC before EXIT should execute");
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_OK, "EXIT should execute successfully");
    if (!vm.halted) {
        failures += record_failure("EXIT should halt the VM");
    }
    failures += expect_size((size_t)vm.instruction_count, 3U, "EXIT should count as executed and skip later instructions");
    if (!vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax)) {
        failures += record_failure("EAX read after EXIT should succeed");
    } else {
        failures += expect_u32(eax, 123U, "instruction after EXIT should not execute");
    }
    failures += expect_flag(&vm.cpu, VM_FLAG_CF, true, "EXIT should preserve modeled flags");
    delta = vm_last_delta(&vm);
    if (delta == NULL || !delta->has_instruction || delta->instruction.opcode != VM_IR_OPCODE_EXIT) {
        failures += record_failure("last delta should identify EXIT instruction");
    } else {
        failures += expect_size(delta->register_change_count, 0U, "EXIT should not change registers");
        failures += expect_size(delta->flag_change_count, 0U, "EXIT should not change flags");
        failures += expect_size(delta->memory_change_count, 0U, "EXIT should not change memory");
    }
    failures += expect_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "step after EXIT should report halted");
    vm_deinit(&vm);

    failures += expect_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "VM should initialize for malformed EXIT test");
    failures += expect_status(vm_load_program(&vm, malformed_exit, 1U), VM_EXEC_STATUS_OK, "Malformed EXIT program should load");
    status = vm_step(&vm);
    failures += expect_status(status, VM_EXEC_STATUS_UNSUPPORTED_OPERAND, "Malformed EXIT operands should be rejected by executor");
    vm_deinit(&vm);

    return failures;
}

/// Verifies metadata helper edge cases.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_metadata_helpers(void) {
    int failures = 0;

    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_MOV), "mov") != 0) {
        failures += record_failure("MOV opcode name should be mov");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_INC), "inc") != 0) {
        failures += record_failure("INC opcode name should be inc");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_DEC), "dec") != 0) {
        failures += record_failure("DEC opcode name should be dec");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_AND), "and") != 0) {
        failures += record_failure("AND opcode name should be and");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_OR), "or") != 0) {
        failures += record_failure("OR opcode name should be or");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_XOR), "xor") != 0) {
        failures += record_failure("XOR opcode name should be xor");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_NOT), "not") != 0) {
        failures += record_failure("NOT opcode name should be not");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_SHL), "shl") != 0) {
        failures += record_failure("SHL opcode name should be shl");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_SAL), "sal") != 0) {
        failures += record_failure("SAL opcode name should be sal");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_SHR), "shr") != 0) {
        failures += record_failure("SHR opcode name should be shr");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_SAR), "sar") != 0) {
        failures += record_failure("SAR opcode name should be sar");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_ROL), "rol") != 0) {
        failures += record_failure("ROL opcode name should be rol");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_ROR), "ror") != 0) {
        failures += record_failure("ROR opcode name should be ror");
    }
    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_EXIT), "exit") != 0) {
        failures += record_failure("EXIT opcode name should be exit");
    }
    if (vm_ir_opcode_name((VmIrOpcode)99) != NULL) {
        failures += record_failure("invalid opcode name should be NULL");
    }
    if (!vm_ir_width_is_supported(8U) || !vm_ir_width_is_supported(16U) || !vm_ir_width_is_supported(32U)) {
        failures += record_failure("8/16/32-bit widths should be supported");
    }
    if (vm_ir_width_is_supported(64U)) {
        failures += record_failure("64-bit execution width should not be supported by the current MASM32 execution subset");
    }
    if (strcmp(vm_exec_status_name(VM_EXEC_STATUS_OK), "ok") != 0) {
        failures += record_failure("executor OK status name should be stable");
    }
    if (vm_exec_status_name((VmExecStatus)99) != NULL) {
        failures += record_failure("invalid executor status name should be NULL");
    }

    return failures;
}

/// Runs all executor tests through Milestone 50.
///
/// @return Zero on success, non-zero when any test fails.
int main(void) {
    int failures = 0;

    failures += test_hardcoded_program_result();
    failures += test_step_mov_add_register_delta();
    failures += test_sub_underflow_flags_and_delta();
    failures += test_memory_operands_and_delta();
    failures += test_register_indirect_memory_operand_and_access_delta();
    failures += test_alias_width_edge_case();
    failures += test_zero_length_program_halts();
    failures += test_error_paths_and_diagnostics();
    failures += test_movsx_movzx_register_and_memory_sources();
    failures += test_accumulator_extension_instructions();
    failures += test_extension_instruction_edge_cases();
    failures += test_extension_instruction_error_paths();
    failures += test_xchg_registers_preserves_flags();
    failures += test_xchg_memory_and_nop_delta();
    failures += test_neg_register_memory_and_flags();
    failures += test_phase20_error_paths();
    failures += test_adc_register_carry_propagation();
    failures += test_adc_alias_and_memory_edge_cases();
    failures += test_sbb_register_and_memory_borrow_propagation();
    failures += test_adc_sbb_memory_source_operands();
    failures += test_sbb_signed_overflow_edge_case();
    failures += test_carry_flag_control_instructions();
    failures += test_phase21_error_paths();
    failures += test_test_register_immediate_flags_and_non_mutation();
    failures += test_test_alias_sign_flag_edge_case();
    failures += test_test_memory_forms_and_non_mutation();
    failures += test_test_error_paths();
    failures += test_inc_dec_register_flags_and_carry_preservation();
    failures += test_inc_dec_memory_destinations_and_errors();
    failures += test_logical_binary_register_flags();
    failures += test_logical_binary_alias_and_memory_destinations();
    failures += test_logical_binary_error_paths();
    failures += test_not_register_destinations_preserve_flags();
    failures += test_not_memory_destinations_and_errors();
    failures += test_shift_left_register_flags_and_counts();
    failures += test_shift_left_memory_destinations_and_errors();
    failures += test_shift_right_register_flags_and_counts();
    failures += test_shift_right_memory_destinations_and_errors();
    failures += test_shift_arithmetic_right_register_flags_and_counts();
    failures += test_shift_arithmetic_right_memory_destinations_and_errors();
    failures += test_rotate_left_register_flags_and_counts();
    failures += test_rotate_left_memory_destinations_and_errors();
    failures += test_rotate_right_register_flags_and_counts();
    failures += test_rotate_right_memory_destinations_and_errors();
    failures += test_exit_terminator_halts_without_mutation();
    failures += test_metadata_helpers();

    if (failures != 0) {
        fprintf(stderr, "%d executor test failure(s).\n", failures);
        return 1;
    }

    puts("Executor tests through Milestone 50 passed.");
    return 0;
}
