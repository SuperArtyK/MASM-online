/*
 * @file test_vm_exec.c
 * @brief Unit tests for the VM executor through Milestone 14.
 *
 * These tests exercise the first vertical execution slice: hardcoded IR, VM
 * stepping, mov/add/sub semantics, CPU and memory integration, and last-step
 * delta capture. They intentionally avoid parser, control-flow, stack, Irvine32,
 * and browser UI behavior.
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
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 20U, VM_REGISTER_COUNT, 0U}, "main.asm", 3U, "mov eax, 20", 0U},
        {VM_IR_OPCODE_ADD, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 22U, VM_REGISTER_COUNT, 0U}, "main.asm", 4U, "add eax, 22", 1U}
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
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U}, "main.asm", 1U, "mov eax, 0", 0U},
        {VM_IR_OPCODE_SUB, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U}, "main.asm", 2U, "sub eax, 1", 1U}
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
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0x12345678U, VM_REGISTER_COUNT, 0U}, "main.asm", 1U, "mov [data], 12345678h", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE}, "main.asm", 2U, "mov ebx, [data]", 1U},
        {VM_IR_OPCODE_ADD, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_DATA_BASE}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EBX, 0U}, "main.asm", 3U, "add [data], ebx", 2U}
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
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_ESI, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, unaligned_address, VM_REGISTER_COUNT, 0U}, "main.asm", 1U, "mov esi, OFFSET data + 1", 0U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_REGISTER, 32U, 0U, VM_REGISTER_ESI, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 0xAABBCCDDU, VM_REGISTER_COUNT, 0U}, "main.asm", 2U, "mov DWORD PTR [esi], 0AABBCCDDh", 1U},
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_MEMORY_REGISTER, 32U, 0U, VM_REGISTER_ESI, 0U}, "main.asm", 3U, "mov eax, DWORD PTR [esi]", 2U}
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
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0x123U, VM_REGISTER_COUNT, 0U}, "main.asm", 1U, "mov al, 123h", 0U},
        {VM_IR_OPCODE_ADD, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U}, {VM_IR_OPERAND_IMMEDIATE, 8U, 0xFFU, VM_REGISTER_COUNT, 0U}, "main.asm", 2U, "add al, 0FFh", 1U}
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
        {(VmIrOpcode)99, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U}, "main.asm", 1U, "bad eax, 1", 0U}
    };
    const VmIrInstruction immediate_destination[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_IMMEDIATE, 32U, 0U, VM_REGISTER_COUNT, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U}, "main.asm", 2U, "mov 0, 1", 0U}
    };
    const VmIrInstruction invalid_memory_read[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, 0xDEADBEEFU}, "main.asm", 3U, "mov eax, [bad]", 0U}
    };
    const VmIrInstruction code_write[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_MEMORY_ADDRESS, 32U, 0U, VM_REGISTER_COUNT, VM_MEMORY_DEFAULT_CODE_BASE}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U}, "main.asm", 4U, "mov [code], 1", 0U}
    };
    const VmIrInstruction mismatched_register_width[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_AL, 0U}, "main.asm", 5U, "mov eax, al", 0U}
    };
    const VmIrInstruction invalid_register_width_override[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 32U, 0U, VM_REGISTER_AL, 0U}, {VM_IR_OPERAND_IMMEDIATE, 32U, 1U, VM_REGISTER_COUNT, 0U}, "main.asm", 6U, "mov al<32>, 1", 0U}
    };
    const VmIrInstruction mismatched_immediate_width[] = {
        {VM_IR_OPCODE_MOV, {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U}, {VM_IR_OPERAND_IMMEDIATE, 8U, 1U, VM_REGISTER_COUNT, 0U}, "main.asm", 7U, "mov eax, imm8", 0U}
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

/// Verifies metadata helper edge cases.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_metadata_helpers(void) {
    int failures = 0;

    if (strcmp(vm_ir_opcode_name(VM_IR_OPCODE_MOV), "mov") != 0) {
        failures += record_failure("MOV opcode name should be mov");
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

/// Runs all executor tests through Milestone 14.
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
    failures += test_metadata_helpers();

    if (failures != 0) {
        fprintf(stderr, "%d executor test failure(s).\n", failures);
        return 1;
    }

    puts("Executor tests through Milestone 14 passed.");
    return 0;
}
