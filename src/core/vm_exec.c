/*
 * @file vm_exec.c
 * @brief Executor for implemented MASM32 simulator IR programs.
 *
 * The executor intentionally supports only a tiny vertical slice: mov, add,
 * sub, movsx, movzx, cbw, cwde, cwd, and cdq over the currently supported
 * register and memory operand forms. It records last-step deltas by snapshotting CPU state and copying memory-module
 * byte changes after each successful step.
 */

#include "vm_exec.h"

#include <string.h>

/// Canonical registers captured for register deltas.
static const VmRegister VM_EXEC_CANONICAL_DELTA_REGISTERS[VM_EXEC_MAX_REGISTER_CHANGES] = {
    VM_REGISTER_EAX,
    VM_REGISTER_EBX,
    VM_REGISTER_ECX,
    VM_REGISTER_EDX,
    VM_REGISTER_ESI,
    VM_REGISTER_EDI,
    VM_REGISTER_EBP,
    VM_REGISTER_ESP,
    VM_REGISTER_EIP
};

/// Named flags captured for flag deltas.
static const VmFlag VM_EXEC_DELTA_FLAGS[VM_EXEC_MAX_FLAG_CHANGES] = {
    VM_FLAG_CF,
    VM_FLAG_ZF,
    VM_FLAG_SF,
    VM_FLAG_OF
};

/// Clears a step delta to its empty state.
///
/// @param delta Delta structure to clear.
static void vm_exec_clear_delta(VmExecDelta *delta) {
    if (delta == NULL) {
        return;
    }

    memset(delta, 0, sizeof(*delta));
}

/// Clears a diagnostic structure and records the supplied status.
///
/// @param diagnostic Diagnostic structure to clear.
/// @param status Status to store after clearing.
static void vm_exec_clear_diagnostic(VmExecDiagnostic *diagnostic, VmExecStatus status) {
    if (diagnostic == NULL) {
        return;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = status;
    diagnostic->memory_status = VM_MEMORY_STATUS_OK;
}

/// Records an executor diagnostic for an instruction-scoped status.
///
/// @param vm VM whose diagnostic should be updated.
/// @param status Executor status to store.
/// @param instruction Optional instruction associated with the status.
static void vm_exec_set_diagnostic(Vm *vm, VmExecStatus status, const VmIrInstruction *instruction) {
    if (vm == NULL) {
        return;
    }

    vm_exec_clear_diagnostic(&vm->last_diagnostic, status);
    if (instruction != NULL) {
        vm->last_diagnostic.has_instruction = true;
        vm->last_diagnostic.instruction = *instruction;
        vm->last_diagnostic.instruction_index = instruction->instruction_index;
    } else if (vm->instruction_pointer <= (size_t)UINT32_MAX) {
        vm->last_diagnostic.instruction_index = (uint32_t)vm->instruction_pointer;
    }
}

/// Records an executor diagnostic for a memory-access failure.
///
/// @param vm VM whose diagnostic should be updated.
/// @param instruction Instruction associated with the failed memory access.
/// @param memory_status Memory status returned by the checked memory helper.
/// @param memory_diagnostic Structured memory diagnostic to copy.
static void vm_exec_set_memory_diagnostic(
    Vm *vm,
    const VmIrInstruction *instruction,
    VmMemoryStatus memory_status,
    const VmMemoryDiagnostic *memory_diagnostic
) {
    vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_MEMORY_ERROR, instruction);
    if (vm == NULL) {
        return;
    }

    vm->last_diagnostic.memory_status = memory_status;
    if (memory_diagnostic != NULL) {
        vm->last_diagnostic.memory_diagnostic = *memory_diagnostic;
    }
}

/// Returns a bit mask for a supported execution width.
///
/// @param width_bits Width in bits; must be 8, 16, or 32.
/// @param out_mask Receives the value mask on success.
/// @return true when the width is supported.
static bool vm_exec_mask_for_width(uint8_t width_bits, uint32_t *out_mask) {
    if (out_mask == NULL) {
        return false;
    }

    switch (width_bits) {
        case 8U:
            *out_mask = 0x000000FFU;
            return true;
        case 16U:
            *out_mask = 0x0000FFFFU;
            return true;
        case 32U:
            *out_mask = 0xFFFFFFFFU;
            return true;
        default:
            *out_mask = 0U;
            return false;
    }
}

/// Determines the effective operand width used by the executor.
///
/// @param operand Operand whose width should be resolved.
/// @param out_width Receives 8, 16, or 32 on success.
/// @return true when a supported width can be resolved.
static bool vm_exec_operand_width(const VmIrOperand *operand, uint8_t *out_width) {
    uint8_t width = 0U;
    uint8_t register_width = 0U;

    if (operand == NULL || out_width == NULL) {
        return false;
    }

    width = operand->width_bits;
    if (operand->kind == VM_IR_OPERAND_REGISTER) {
        register_width = vm_cpu_register_width_bits(operand->reg);
        if (!vm_ir_width_is_supported(register_width)) {
            return false;
        }
        if (width == 0U) {
            width = register_width;
        } else if (width != register_width) {
            return false;
        }
    }

    if (!vm_ir_width_is_supported(width)) {
        return false;
    }

    *out_width = width;
    return true;
}

/// Returns whether an operand is any implemented memory operand.
///
/// @param operand Operand to inspect.
/// @return true for absolute and register-indirect memory operands.
static bool vm_exec_operand_is_memory(const VmIrOperand *operand) {
    return operand != NULL &&
           (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS || operand->kind == VM_IR_OPERAND_MEMORY_REGISTER);
}

/// Resolves an implemented memory operand to an effective 32-bit address.
///
/// Register-indirect addresses use 32-bit wrapping arithmetic, matching the
/// educational MASM32 flat address model. Bounds and permissions remain owned
/// by the checked memory module.
///
/// @param vm VM whose CPU state supplies runtime register values.
/// @param operand Memory operand to resolve.
/// @param out_address Receives the effective address.
/// @return true when the operand was a supported memory operand.
static bool vm_exec_resolve_memory_address(const Vm *vm, const VmIrOperand *operand, uint32_t *out_address) {
    uint32_t base_value = 0U;
    uint32_t address = 0U;

    if (vm == NULL || operand == NULL || out_address == NULL) {
        return false;
    }

    if (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS) {
        *out_address = operand->address;
        return true;
    }

    if (operand->kind != VM_IR_OPERAND_MEMORY_REGISTER || !vm_cpu_read_register(&vm->cpu, operand->reg, &base_value)) {
        return false;
    }

    address = operand->address + base_value;
    if ((int32_t)operand->immediate < 0) {
        uint32_t magnitude = (uint32_t)(-(int64_t)(int32_t)operand->immediate);
        address -= magnitude;
    } else {
        address += (uint32_t)(int32_t)operand->immediate;
    }

    *out_address = address;
    return true;
}

/// Records a checked memory access in the last-step delta when capacity allows it.
///
/// @param vm VM whose delta should receive the memory-access record.
/// @param kind Read or write access kind.
/// @param address Effective simulated address.
/// @param width_bits Access width in bits.
/// @param status Status returned by the checked memory helper.
static void vm_exec_record_memory_access(
    Vm *vm,
    VmExecMemoryAccessKind kind,
    uint32_t address,
    uint8_t width_bits,
    VmMemoryStatus status
) {
    VmExecMemoryAccess *access = NULL;

    if (vm == NULL || vm->last_delta.memory_access_count >= (size_t)VM_EXEC_MAX_MEMORY_ACCESSES) {
        return;
    }

    access = &vm->last_delta.memory_accesses[vm->last_delta.memory_access_count];
    access->kind = kind;
    access->address = address;
    access->width_bits = width_bits;
    access->status = status;
    vm->last_delta.memory_access_count += 1U;
}

/// Reads an operand value through CPU or memory helpers.
///
/// @param vm VM instance to inspect.
/// @param instruction Instruction associated with the read for diagnostics.
/// @param operand Operand to read.
/// @param width_bits Execution width in bits.
/// @param out_value Receives the masked value on success.
/// @return Executor status for the read.
static VmExecStatus vm_exec_read_operand(
    Vm *vm,
    const VmIrInstruction *instruction,
    const VmIrOperand *operand,
    uint8_t width_bits,
    uint32_t *out_value
) {
    uint32_t mask = 0U;
    uint32_t value32 = 0U;
    uint16_t value16 = 0U;
    uint8_t value8 = 0U;
    VmMemoryDiagnostic memory_diagnostic;
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL || operand == NULL || out_value == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    switch (operand->kind) {
        case VM_IR_OPERAND_IMMEDIATE:
            *out_value = operand->immediate & mask;
            return VM_EXEC_STATUS_OK;
        case VM_IR_OPERAND_REGISTER:
            if (!vm_cpu_read_register(&vm->cpu, operand->reg, &value32)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            *out_value = value32 & mask;
            return VM_EXEC_STATUS_OK;
        case VM_IR_OPERAND_MEMORY_ADDRESS:
        case VM_IR_OPERAND_MEMORY_REGISTER: {
            uint32_t effective_address = 0U;
            if (!vm_exec_resolve_memory_address(vm, operand, &effective_address)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            memset(&memory_diagnostic, 0, sizeof(memory_diagnostic));
            if (width_bits == 8U) {
                memory_status = vm_memory_read_u8(&vm->memory, effective_address, &value8, &memory_diagnostic);
                value32 = (uint32_t)value8;
            } else if (width_bits == 16U) {
                memory_status = vm_memory_read_u16(&vm->memory, effective_address, &value16, &memory_diagnostic);
                value32 = (uint32_t)value16;
            } else {
                memory_status = vm_memory_read_u32(&vm->memory, effective_address, &value32, &memory_diagnostic);
            }
            vm_exec_record_memory_access(vm, VM_EXEC_MEMORY_ACCESS_READ, effective_address, width_bits, memory_status);
            if (!vm_memory_status_succeeded(memory_status)) {
                vm_exec_set_memory_diagnostic(vm, instruction, memory_status, &memory_diagnostic);
                return VM_EXEC_STATUS_MEMORY_ERROR;
            }
            *out_value = value32 & mask;
            return VM_EXEC_STATUS_OK;
        }
        default:
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
}

/// Writes an operand value through CPU or memory helpers.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction associated with the write for diagnostics.
/// @param operand Destination operand to write.
/// @param width_bits Execution width in bits.
/// @param value Value to write before width masking.
/// @return Executor status for the write.
static VmExecStatus vm_exec_write_operand(
    Vm *vm,
    const VmIrInstruction *instruction,
    const VmIrOperand *operand,
    uint8_t width_bits,
    uint32_t value
) {
    uint32_t mask = 0U;
    uint32_t masked_value = 0U;
    VmMemoryDiagnostic memory_diagnostic;
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL || operand == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    masked_value = value & mask;
    switch (operand->kind) {
        case VM_IR_OPERAND_REGISTER:
            if (!vm_cpu_write_register(&vm->cpu, operand->reg, masked_value)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            return VM_EXEC_STATUS_OK;
        case VM_IR_OPERAND_MEMORY_ADDRESS:
        case VM_IR_OPERAND_MEMORY_REGISTER: {
            uint32_t effective_address = 0U;
            if (!vm_exec_resolve_memory_address(vm, operand, &effective_address)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            memset(&memory_diagnostic, 0, sizeof(memory_diagnostic));
            if (width_bits == 8U) {
                memory_status = vm_memory_write_u8(&vm->memory, effective_address, (uint8_t)masked_value, &memory_diagnostic);
            } else if (width_bits == 16U) {
                memory_status = vm_memory_write_u16(&vm->memory, effective_address, (uint16_t)masked_value, &memory_diagnostic);
            } else {
                memory_status = vm_memory_write_u32(&vm->memory, effective_address, masked_value, &memory_diagnostic);
            }
            vm_exec_record_memory_access(vm, VM_EXEC_MEMORY_ACCESS_WRITE, effective_address, width_bits, memory_status);
            if (!vm_memory_status_succeeded(memory_status)) {
                vm_exec_set_memory_diagnostic(vm, instruction, memory_status, &memory_diagnostic);
                return VM_EXEC_STATUS_MEMORY_ERROR;
            }
            return VM_EXEC_STATUS_OK;
        }
        default:
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
}

/// Returns whether an operand can be used as a destination by the current execution subset.
///
/// @param operand Operand to inspect.
/// @return true for register and memory-address operands.
static bool vm_exec_operand_is_destination(const VmIrOperand *operand) {
    if (operand == NULL) {
        return false;
    }

    return operand->kind == VM_IR_OPERAND_REGISTER || vm_exec_operand_is_memory(operand);
}

/// Returns whether a source/destination pair is supported by the current execution subset.
///
/// @param destination Destination operand to inspect.
/// @param source Source operand to inspect.
/// @return true when the executor can process the pair.
static bool vm_exec_operands_are_supported(const VmIrOperand *destination, const VmIrOperand *source) {
    if (!vm_exec_operand_is_destination(destination) || source == NULL) {
        return false;
    }

    if (source->kind != VM_IR_OPERAND_IMMEDIATE && source->kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(source)) {
        return false;
    }

    if (vm_exec_operand_is_memory(destination) && vm_exec_operand_is_memory(source)) {
        return false;
    }

    return true;
}

/// Returns whether a source operand width is compatible with an instruction width.
///
/// @param source Source operand to inspect.
/// @param instruction_width_bits Width selected by the destination operand.
/// @return true when the source can be read using @p instruction_width_bits.
static bool vm_exec_source_width_is_compatible(const VmIrOperand *source, uint8_t instruction_width_bits) {
    uint8_t source_width = 0U;

    if (source == NULL || !vm_ir_width_is_supported(instruction_width_bits)) {
        return false;
    }

    if (source->kind == VM_IR_OPERAND_IMMEDIATE) {
        return source->width_bits == 0U || source->width_bits == instruction_width_bits;
    }

    if (!vm_exec_operand_width(source, &source_width)) {
        return false;
    }

    return source_width == instruction_width_bits;
}

/// Executes one mov instruction.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_mov(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    uint32_t value = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    return vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, value);
}

/// Executes one add instruction.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_add(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t left = 0U;
    uint32_t right = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &left);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &right);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (!vm_cpu_update_add_flags(&vm->cpu, left, right, width_bits, &result)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}

/// Executes one sub instruction.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_sub(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t left = 0U;
    uint32_t right = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &left);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &right);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (!vm_cpu_update_sub_flags(&vm->cpu, left, right, width_bits, &result)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}

/// Sign-extends a masked value from an implemented source width to 32 bits.
///
/// @param value Source value before masking.
/// @param source_width_bits Source width in bits; must be 8 or 16.
/// @return Sign-extended 32-bit value, or zero for unsupported source widths.
static uint32_t vm_exec_sign_extend_value(uint32_t value, uint8_t source_width_bits) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t masked_value = 0U;

    if (source_width_bits == 8U) {
        mask = 0x000000FFU;
        sign_bit = 0x00000080U;
    } else if (source_width_bits == 16U) {
        mask = 0x0000FFFFU;
        sign_bit = 0x00008000U;
    } else {
        return 0U;
    }

    masked_value = value & mask;
    if ((masked_value & sign_bit) != 0U) {
        return masked_value | ~mask;
    }

    return masked_value;
}

/// Executes one MOVSX or MOVZX instruction.
///
/// MOVSX sign-extends and MOVZX zero-extends an 8-bit or 16-bit register or
/// memory source into a wider register destination. Memory reads remain routed
/// through the checked VM memory helpers.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param should_sign_extend true for MOVSX, false for MOVZX.
/// @return Executor status.
static VmExecStatus vm_exec_execute_movx(Vm *vm, const VmIrInstruction *instruction, bool should_sign_extend) {
    uint8_t destination_width = 0U;
    uint8_t source_width = 0U;
    uint32_t source_value = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (instruction->destination.kind != VM_IR_OPERAND_REGISTER) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (instruction->source.kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(&instruction->source)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->destination, &destination_width) || !vm_exec_operand_width(&instruction->source, &source_width)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if ((source_width != 8U && source_width != 16U) || destination_width <= source_width) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, source_width, &source_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    result = should_sign_extend ? vm_exec_sign_extend_value(source_value, source_width) : source_value;
    return vm_exec_write_operand(vm, instruction, &instruction->destination, destination_width, result);
}

/// Returns whether an instruction descriptor has no operands.
///
/// @param instruction Instruction to inspect.
/// @return true when both operand slots are empty.
static bool vm_exec_instruction_has_no_operands(const VmIrInstruction *instruction) {
    return instruction != NULL &&
           instruction->destination.kind == VM_IR_OPERAND_NONE &&
           instruction->source.kind == VM_IR_OPERAND_NONE;
}

/// Executes CBW by sign-extending AL into AX.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction descriptor used for operand validation.
/// @return Executor status.
static VmExecStatus vm_exec_execute_cbw(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t al = 0U;
    uint32_t ax = 0U;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_instruction_has_no_operands(instruction)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AL, &al)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    ax = vm_exec_sign_extend_value(al, 8U) & 0x0000FFFFU;
    return vm_cpu_write_register(&vm->cpu, VM_REGISTER_AX, ax) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
}

/// Executes CWDE by sign-extending AX into EAX.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction descriptor used for operand validation.
/// @return Executor status.
static VmExecStatus vm_exec_execute_cwde(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t ax = 0U;
    uint32_t eax = 0U;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_instruction_has_no_operands(instruction)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &ax)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    eax = vm_exec_sign_extend_value(ax, 16U);
    return vm_cpu_write_register(&vm->cpu, VM_REGISTER_EAX, eax) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
}

/// Executes CWD by sign-extending AX into DX:AX.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction descriptor used for operand validation.
/// @return Executor status.
static VmExecStatus vm_exec_execute_cwd(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t ax = 0U;
    uint32_t dx = 0U;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_instruction_has_no_operands(instruction)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &ax)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    dx = (ax & 0x00008000U) != 0U ? 0x0000FFFFU : 0x00000000U;
    return vm_cpu_write_register(&vm->cpu, VM_REGISTER_DX, dx) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
}

/// Executes CDQ by sign-extending EAX into EDX:EAX.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction descriptor used for operand validation.
/// @return Executor status.
static VmExecStatus vm_exec_execute_cdq(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t eax = 0U;
    uint32_t edx = 0U;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_instruction_has_no_operands(instruction)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_EAX, &eax)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    edx = (eax & 0x80000000U) != 0U ? 0xFFFFFFFFU : 0x00000000U;
    return vm_cpu_write_register(&vm->cpu, VM_REGISTER_EDX, edx) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
}

/// Captures canonical register changes after one successful instruction.
///
/// @param delta Delta structure to update.
/// @param before CPU snapshot captured before execution.
/// @param after CPU state after execution.
static void vm_exec_capture_register_changes(VmExecDelta *delta, const VmCpu *before, const VmCpu *after) {
    size_t index = 0U;

    if (delta == NULL || before == NULL || after == NULL) {
        return;
    }

    for (index = 0U; index < (size_t)VM_EXEC_MAX_REGISTER_CHANGES; index += 1U) {
        VmRegister reg = VM_EXEC_CANONICAL_DELTA_REGISTERS[index];
        uint32_t old_value = 0U;
        uint32_t new_value = 0U;

        if (!vm_cpu_read_register(before, reg, &old_value) || !vm_cpu_read_register(after, reg, &new_value)) {
            continue;
        }

        if (old_value != new_value && delta->register_change_count < (size_t)VM_EXEC_MAX_REGISTER_CHANGES) {
            VmExecRegisterChange *change = &delta->register_changes[delta->register_change_count];
            change->reg = reg;
            change->old_value = old_value;
            change->new_value = new_value;
            delta->register_change_count += 1U;
        }
    }
}

/// Captures named flag changes after one successful instruction.
///
/// @param delta Delta structure to update.
/// @param before CPU snapshot captured before execution.
/// @param after CPU state after execution.
static void vm_exec_capture_flag_changes(VmExecDelta *delta, const VmCpu *before, const VmCpu *after) {
    size_t index = 0U;

    if (delta == NULL || before == NULL || after == NULL) {
        return;
    }

    for (index = 0U; index < (size_t)VM_EXEC_MAX_FLAG_CHANGES; index += 1U) {
        VmFlag flag = VM_EXEC_DELTA_FLAGS[index];
        bool old_value = false;
        bool new_value = false;

        if (!vm_cpu_read_flag(before, flag, &old_value) || !vm_cpu_read_flag(after, flag, &new_value)) {
            continue;
        }

        if (old_value != new_value && delta->flag_change_count < (size_t)VM_EXEC_MAX_FLAG_CHANGES) {
            VmExecFlagChange *change = &delta->flag_changes[delta->flag_change_count];
            change->flag = flag;
            change->old_is_set = old_value;
            change->new_is_set = new_value;
            delta->flag_change_count += 1U;
        }
    }
}

/// Copies memory-module byte changes into the last-step delta.
///
/// @param delta Delta structure to update.
/// @param memory Memory object whose change recorder is copied.
static void vm_exec_capture_memory_changes(VmExecDelta *delta, const VmMemory *memory) {
    size_t index = 0U;
    size_t memory_change_count = 0U;

    if (delta == NULL || memory == NULL) {
        return;
    }

    memory_change_count = vm_memory_change_count(memory);
    for (index = 0U; index < memory_change_count && index < (size_t)VM_EXEC_MAX_MEMORY_CHANGES; index += 1U) {
        const VmMemoryByteChange *change = vm_memory_get_change(memory, index);
        if (change != NULL) {
            delta->memory_changes[delta->memory_change_count] = *change;
            delta->memory_change_count += 1U;
        }
    }

    delta->memory_change_overflowed = memory->change_overflowed || memory_change_count > (size_t)VM_EXEC_MAX_MEMORY_CHANGES;
}

/// Captures the final last-step delta after successful execution.
///
/// @param vm VM whose delta should be updated.
/// @param instruction Instruction that was executed.
/// @param before CPU snapshot captured before execution.
static void vm_exec_capture_delta(Vm *vm, const VmIrInstruction *instruction, const VmCpu *before) {
    if (vm == NULL || instruction == NULL || before == NULL) {
        return;
    }

    vm->last_delta.has_instruction = true;
    vm->last_delta.instruction = *instruction;
    vm_exec_capture_register_changes(&vm->last_delta, before, &vm->cpu);
    vm_exec_capture_flag_changes(&vm->last_delta, before, &vm->cpu);
    vm_exec_capture_memory_changes(&vm->last_delta, &vm->memory);
    vm->last_delta.instruction_count = vm->instruction_count;
}

/// Executes one already-fetched instruction.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @return Executor status.
static VmExecStatus vm_exec_execute_instruction(Vm *vm, const VmIrInstruction *instruction) {
    uint8_t width_bits = 0U;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    switch (instruction->opcode) {
        case VM_IR_OPCODE_MOV:
        case VM_IR_OPCODE_ADD:
        case VM_IR_OPCODE_SUB:
            if (!vm_exec_operands_are_supported(&instruction->destination, &instruction->source)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            if (!vm_exec_operand_width(&instruction->destination, &width_bits)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            if (!vm_exec_source_width_is_compatible(&instruction->source, width_bits)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            if (instruction->opcode == VM_IR_OPCODE_MOV) {
                return vm_exec_execute_mov(vm, instruction, width_bits);
            }
            if (instruction->opcode == VM_IR_OPCODE_ADD) {
                return vm_exec_execute_add(vm, instruction, width_bits);
            }
            return vm_exec_execute_sub(vm, instruction, width_bits);
        case VM_IR_OPCODE_MOVSX:
            return vm_exec_execute_movx(vm, instruction, true);
        case VM_IR_OPCODE_MOVZX:
            return vm_exec_execute_movx(vm, instruction, false);
        case VM_IR_OPCODE_CBW:
            return vm_exec_execute_cbw(vm, instruction);
        case VM_IR_OPCODE_CWDE:
            return vm_exec_execute_cwde(vm, instruction);
        case VM_IR_OPCODE_CWD:
            return vm_exec_execute_cwd(vm, instruction);
        case VM_IR_OPCODE_CDQ:
            return vm_exec_execute_cdq(vm, instruction);
        default:
            return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }
}

VmExecStatus vm_init(Vm *vm, const VmMemoryConfig *memory_config) {
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memset(vm, 0, sizeof(*vm));
    vm_cpu_init(&vm->cpu);
    memory_status = vm_memory_init(&vm->memory, memory_config);
    if (memory_status != VM_MEMORY_STATUS_OK) {
        vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_MEMORY_ERROR);
        vm->last_diagnostic.memory_status = memory_status;
        return VM_EXEC_STATUS_MEMORY_ERROR;
    }

    vm_exec_clear_delta(&vm->last_delta);
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    return VM_EXEC_STATUS_OK;
}

void vm_deinit(Vm *vm) {
    if (vm == NULL) {
        return;
    }

    vm_memory_deinit(&vm->memory);
    vm->program = NULL;
    vm->program_count = 0U;
    vm->instruction_pointer = 0U;
    vm->instruction_count = 0U;
    vm->halted = true;
    vm_exec_clear_delta(&vm->last_delta);
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_HALTED);
}

VmExecStatus vm_load_program(Vm *vm, const VmIrInstruction *program, size_t program_count) {
    if (vm == NULL || (program == NULL && program_count > 0U)) {
        if (vm != NULL) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        }
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm_cpu_init(&vm->cpu);
    vm_memory_clear_changes(&vm->memory);
    vm->program = program;
    vm->program_count = program_count;
    vm->instruction_pointer = 0U;
    vm->instruction_count = 0U;
    vm->halted = false;
    vm_exec_clear_delta(&vm->last_delta);
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);

    return VM_EXEC_STATUS_OK;
}

VmExecStatus vm_step(Vm *vm) {
    const VmIrInstruction *instruction = NULL;
    VmCpu before_cpu;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm_exec_clear_delta(&vm->last_delta);
    vm_memory_clear_changes(&vm->memory);

    if (vm->halted || vm->instruction_pointer >= vm->program_count) {
        vm->halted = true;
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_HALTED, NULL);
        return VM_EXEC_STATUS_HALTED;
    }

    instruction = &vm->program[vm->instruction_pointer];
    before_cpu = vm->cpu;
    status = vm_exec_execute_instruction(vm, instruction);
    if (status != VM_EXEC_STATUS_OK) {
        if (status != VM_EXEC_STATUS_MEMORY_ERROR) {
            vm_exec_set_diagnostic(vm, status, instruction);
        }
        return status;
    }

    vm->instruction_pointer += 1U;
    vm->instruction_count += 1U;
    if (vm->instruction_pointer >= vm->program_count) {
        vm->halted = true;
    }

    vm_exec_capture_delta(vm, instruction, &before_cpu);
    vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_OK, instruction);
    return VM_EXEC_STATUS_OK;
}

const VmExecDelta *vm_last_delta(const Vm *vm) {
    if (vm == NULL) {
        return NULL;
    }

    return &vm->last_delta;
}

const VmExecDiagnostic *vm_last_diagnostic(const Vm *vm) {
    if (vm == NULL) {
        return NULL;
    }

    return &vm->last_diagnostic;
}

const char *vm_exec_status_name(VmExecStatus status) {
    switch (status) {
        case VM_EXEC_STATUS_OK:
            return "ok";
        case VM_EXEC_STATUS_HALTED:
            return "halted";
        case VM_EXEC_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_EXEC_STATUS_INVALID_INSTRUCTION:
            return "invalid-instruction";
        case VM_EXEC_STATUS_UNSUPPORTED_OPERAND:
            return "unsupported-operand";
        case VM_EXEC_STATUS_MEMORY_ERROR:
            return "memory-error";
        default:
            return NULL;
    }
}

VmExecStatus vm_run_milestone4_hardcoded_program(uint32_t *out_eax) {
    Vm vm;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    uint32_t eax = 0U;
    const VmIrInstruction program[] = {
        {
            VM_IR_OPCODE_MOV,
            {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U},
            {VM_IR_OPERAND_IMMEDIATE, 32U, 20U, VM_REGISTER_COUNT, 0U},
            "milestone4.asm",
            1U,
            "mov eax, 20",
            0U
        },
        {
            VM_IR_OPCODE_ADD,
            {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U},
            {VM_IR_OPERAND_IMMEDIATE, 32U, 22U, VM_REGISTER_COUNT, 0U},
            "milestone4.asm",
            2U,
            "add eax, 22",
            1U
        }
    };

    if (out_eax == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    status = vm_init(&vm, NULL);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    status = vm_load_program(&vm, program, sizeof(program) / sizeof(program[0]));
    if (status == VM_EXEC_STATUS_OK) {
        while (!vm.halted && status == VM_EXEC_STATUS_OK) {
            status = vm_step(&vm);
        }
    }

    if (status == VM_EXEC_STATUS_OK && vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax)) {
        *out_eax = eax;
    }

    vm_deinit(&vm);
    return status;
}
