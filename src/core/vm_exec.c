/*
 * @file vm_exec.c
 * @brief Executor for implemented MASM32 simulator IR programs.
 *
 * The executor intentionally supports only a staged vertical slice: mov, add,
 * sub, movsx, movzx, cbw, cwde, cwd, cdq, xchg, neg, nop, adc, sbb, clc, stc, cmc,
 * test, inc, dec, and, or, xor, not, shl, sal, shr, sar, and rol over the currently supported register and memory operand forms. It records last-step
 * deltas by snapshotting CPU state and copying memory-module byte changes after
 * each successful step.
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

/// Executes one TEST instruction and updates flags from a bitwise AND result.
///
/// TEST reads both operands at the selected execution width, computes the
/// masked bitwise AND, updates ZF and SF from that transient result, clears CF
/// and OF, and deliberately does not write the result back to either operand.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_test(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t left = 0U;
    uint32_t right = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &left);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &right);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    result = (left & right) & mask;

    if (!vm_cpu_clear_flag(&vm->cpu, VM_FLAG_CF) ||
        !vm_cpu_clear_flag(&vm->cpu, VM_FLAG_OF) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_ZF, result == 0U) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_SF, (result & sign_bit) != 0U)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_OK;
}


/// Updates modeled flags for a logical binary result.
///
/// Logical instructions set ZF and SF from the masked result and clear CF and
/// OF. Other architectural flags remain outside the current educational model.
///
/// @param cpu CPU state whose modeled flags should be updated.
/// @param result Unmasked logical result.
/// @param width_bits Operand width in bits.
/// @return true when all flag updates succeeded.
static bool vm_exec_update_logical_flags(VmCpu *cpu, uint32_t result, uint8_t width_bits) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t masked_result = 0U;

    if (cpu == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return false;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    masked_result = result & mask;
    return vm_cpu_write_flag(cpu, VM_FLAG_ZF, masked_result == 0U) &&
           vm_cpu_write_flag(cpu, VM_FLAG_SF, (masked_result & sign_bit) != 0U) &&
           vm_cpu_clear_flag(cpu, VM_FLAG_CF) &&
           vm_cpu_clear_flag(cpu, VM_FLAG_OF);
}

/// Executes one AND, OR, or XOR instruction.
///
/// The destination may be a register or memory operand and the source may be a
/// register, immediate, or memory operand. Reads and writes use the central
/// checked operand helpers, and CPU state is restored if the destination write
/// fails after flag computation.
///
/// @param vm VM instance to mutate.
/// @param instruction Logical binary instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_logical_binary(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t left = 0U;
    uint32_t right = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &left);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &right);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (instruction->opcode == VM_IR_OPCODE_AND) {
        result = left & right;
    } else if (instruction->opcode == VM_IR_OPCODE_OR) {
        result = left | right;
    } else if (instruction->opcode == VM_IR_OPCODE_XOR) {
        result = left ^ right;
    } else {
        return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }

    if (!vm_exec_update_logical_flags(&vm->cpu, result, width_bits)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}

/// Reads the raw shift count for shift instructions.
///
/// Counts are either an encoded immediate byte or the current low 8 bits of
/// CL. The parser normally enforces these shapes; this helper keeps the
/// executor defensive for hand-built IR tests.
///
/// @param vm VM whose CPU supplies CL when used.
/// @param source Count source operand.
/// @param out_raw_count Receives the unsigned raw count.
/// @return Executor status for the count operand.
static VmExecStatus vm_exec_read_shift_count(const Vm *vm, const VmIrOperand *source, uint8_t *out_raw_count) {
    uint32_t register_value = 0U;

    if (vm == NULL || source == NULL || out_raw_count == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (source->kind == VM_IR_OPERAND_IMMEDIATE && source->immediate <= 255U) {
        *out_raw_count = (uint8_t)source->immediate;
        return VM_EXEC_STATUS_OK;
    }

    if (source->kind == VM_IR_OPERAND_REGISTER && source->reg == VM_REGISTER_CL) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_CL, &register_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        *out_raw_count = (uint8_t)(register_value & 0xFFU);
        return VM_EXEC_STATUS_OK;
    }

    return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
}

/// Executes one SHL or SAL instruction.
///
/// SAL is an alias for SHL in the supported MASM32 subset. The effective count
/// is raw_count & 31. Count zero is a complete no-op, including for memory
/// destinations. Multi-bit and oversized counts preserve modeled flags that
/// the source specification classifies as undefined; source-run code emits the
/// corresponding warning or strict diagnostic.
///
/// @param vm VM instance to mutate.
/// @param instruction Shift-left instruction to execute.
/// @param width_bits Destination execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_shift_left(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    uint8_t raw_count = 0U;
    uint8_t effective_count = 0U;
    uint8_t index = 0U;
    bool original_cf = false;
    bool original_of = false;
    bool shifted_out = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (instruction->opcode != VM_IR_OPCODE_SHL && instruction->opcode != VM_IR_OPCODE_SAL) {
        return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }

    status = vm_exec_read_shift_count(vm, &instruction->source, &raw_count);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    effective_count = (uint8_t)(raw_count & 31U);
    if (effective_count == 0U) {
        return VM_EXEC_STATUS_OK;
    }

    before_cpu = vm->cpu;
    if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &original_cf) || !vm_cpu_read_flag(&vm->cpu, VM_FLAG_OF, &original_of)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    result = value & mask;
    for (index = 0U; index < effective_count; index += 1U) {
        shifted_out = (result & sign_bit) != 0U;
        result = (result << 1U) & mask;
    }

    if (effective_count < width_bits) {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, shifted_out)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, original_cf)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_ZF, result == 0U) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_SF, (result & sign_bit) != 0U)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (effective_count == 1U) {
        bool new_sign = (result & sign_bit) != 0U;
        bool new_cf = false;
        if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &new_cf) || !vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, new_sign != new_cf)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}

/// Executes one SHR instruction.
///
/// SHR shifts the destination right logically, filling high bits with zero. The
/// effective count is raw_count & 31. Count zero is a complete no-op. Multi-bit
/// and oversized counts preserve modeled flags that the source specification
/// classifies as undefined; source-run code emits the corresponding warning or
/// strict diagnostic before this helper executes.
///
/// @param vm VM instance to mutate.
/// @param instruction SHR instruction to execute.
/// @param width_bits Destination execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_shift_right(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    uint8_t raw_count = 0U;
    uint8_t effective_count = 0U;
    uint8_t index = 0U;
    bool original_cf = false;
    bool original_of = false;
    bool original_sign = false;
    bool shifted_out = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (instruction->opcode != VM_IR_OPCODE_SHR) {
        return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }

    status = vm_exec_read_shift_count(vm, &instruction->source, &raw_count);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    effective_count = (uint8_t)(raw_count & 31U);
    if (effective_count == 0U) {
        return VM_EXEC_STATUS_OK;
    }

    before_cpu = vm->cpu;
    if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &original_cf) || !vm_cpu_read_flag(&vm->cpu, VM_FLAG_OF, &original_of)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    result = value & mask;
    original_sign = (result & sign_bit) != 0U;
    for (index = 0U; index < effective_count; index += 1U) {
        shifted_out = (result & 1U) != 0U;
        result = (result >> 1U) & mask;
    }

    if (effective_count < width_bits) {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, shifted_out)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, original_cf)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_ZF, result == 0U) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_SF, (result & sign_bit) != 0U)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (effective_count == 1U) {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_sign)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}


/// Executes one SAR instruction.
///
/// SAR shifts the destination right arithmetically, filling high bits with the
/// original sign bit. The effective count is raw_count & 31. Count zero is a
/// complete no-op. Multi-bit and oversized counts preserve modeled flags that
/// the source specification classifies as undefined; source-run code emits the
/// corresponding warning or strict diagnostic before this helper executes.
///
/// @param vm VM instance to mutate.
/// @param instruction SAR instruction to execute.
/// @param width_bits Destination execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_shift_arithmetic_right(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    uint8_t raw_count = 0U;
    uint8_t effective_count = 0U;
    uint8_t index = 0U;
    bool original_cf = false;
    bool original_of = false;
    bool original_sign = false;
    bool shifted_out = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (instruction->opcode != VM_IR_OPCODE_SAR) {
        return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }

    status = vm_exec_read_shift_count(vm, &instruction->source, &raw_count);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    effective_count = (uint8_t)(raw_count & 31U);
    if (effective_count == 0U) {
        return VM_EXEC_STATUS_OK;
    }

    before_cpu = vm->cpu;
    if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &original_cf) || !vm_cpu_read_flag(&vm->cpu, VM_FLAG_OF, &original_of)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    result = value & mask;
    original_sign = (result & sign_bit) != 0U;
    for (index = 0U; index < effective_count; index += 1U) {
        shifted_out = (result & 1U) != 0U;
        result = (result >> 1U) & mask;
        if (original_sign) {
            result |= sign_bit;
        }
    }

    if (effective_count < width_bits) {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, shifted_out)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, original_cf)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_ZF, result == 0U) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_SF, (result & sign_bit) != 0U)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (effective_count == 1U) {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, false)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}

/// Executes one ROL instruction.
///
/// ROL rotates the selected destination width left. Count zero after the
/// standard raw_count & 31 mask is a complete no-op. For nonzero effective
/// counts, CF receives the least significant bit of the rotated result, ZF and
/// SF are preserved, and OF is defined only when the effective count is one.
/// Source-run code emits the corresponding undefined-modeled-flag warning for
/// non-one nonzero counts before this helper executes.
///
/// @param vm VM instance to mutate.
/// @param instruction ROL instruction to execute.
/// @param width_bits Destination execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_rotate_left(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    uint8_t raw_count = 0U;
    uint8_t effective_count = 0U;
    uint8_t rotate_count = 0U;
    bool original_of = false;
    bool new_cf = false;
    bool new_sign = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (instruction->opcode != VM_IR_OPCODE_ROL) {
        return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }

    status = vm_exec_read_shift_count(vm, &instruction->source, &raw_count);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    effective_count = (uint8_t)(raw_count & 31U);
    if (effective_count == 0U) {
        return VM_EXEC_STATUS_OK;
    }

    before_cpu = vm->cpu;
    if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_OF, &original_of)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    result = value & mask;
    rotate_count = (uint8_t)(effective_count % width_bits);
    if (rotate_count != 0U) {
        result = ((result << rotate_count) | (result >> (width_bits - rotate_count))) & mask;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    new_cf = (result & 1U) != 0U;
    new_sign = (result & sign_bit) != 0U;
    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, new_cf)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (effective_count == 1U) {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, new_sign != new_cf)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}



/// Converts a masked operand value to a signed 64-bit value for a supported width.
///
/// @param value Operand value before masking.
/// @param width_bits Operand width in bits.
/// @return Sign-extended value, or zero for unsupported widths.
static int64_t vm_exec_signed_value_for_width(uint32_t value, uint8_t width_bits) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t masked_value = 0U;

    if (!vm_exec_mask_for_width(width_bits, &mask)) {
        return 0;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    masked_value = value & mask;
    if ((masked_value & sign_bit) == 0U) {
        return (int64_t)masked_value;
    }

    return (int64_t)masked_value - ((int64_t)mask + 1);
}

/// Returns the smallest signed value representable by a supported width.
///
/// @param width_bits Operand width in bits.
/// @param out_min_value Receives the minimum signed value.
/// @return true when @p width_bits is supported.
static bool vm_exec_signed_min_for_width(uint8_t width_bits, int64_t *out_min_value) {
    if (out_min_value == NULL) {
        return false;
    }

    switch (width_bits) {
        case 8U:
            *out_min_value = -128;
            return true;
        case 16U:
            *out_min_value = -32768;
            return true;
        case 32U:
            *out_min_value = -2147483648LL;
            return true;
        default:
            *out_min_value = 0;
            return false;
    }
}

/// Returns the largest signed value representable by a supported width.
///
/// @param width_bits Operand width in bits.
/// @param out_max_value Receives the maximum signed value.
/// @return true when @p width_bits is supported.
static bool vm_exec_signed_max_for_width(uint8_t width_bits, int64_t *out_max_value) {
    if (out_max_value == NULL) {
        return false;
    }

    switch (width_bits) {
        case 8U:
            *out_max_value = 127;
            return true;
        case 16U:
            *out_max_value = 32767;
            return true;
        case 32U:
            *out_max_value = 2147483647LL;
            return true;
        default:
            *out_max_value = 0;
            return false;
    }
}

/// Applies arithmetic flags for ADC using the incoming carry bit.
///
/// @param cpu CPU state whose flags should be updated.
/// @param left Destination value before addition.
/// @param right Source value before addition.
/// @param carry_in Current carry flag value used as an addend.
/// @param width_bits Operand width in bits.
/// @param out_result Receives the masked result on success.
/// @return true when flags and result were computed for a supported width.
static bool vm_exec_update_adc_flags(VmCpu *cpu, uint32_t left, uint32_t right, bool carry_in, uint8_t width_bits, uint32_t *out_result) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t left_masked = 0U;
    uint32_t right_masked = 0U;
    uint32_t result = 0U;
    uint64_t wide_result = 0U;
    int64_t signed_sum = 0;
    int64_t signed_min = 0;
    int64_t signed_max = 0;

    if (cpu == NULL || out_result == NULL || !vm_exec_mask_for_width(width_bits, &mask) ||
        !vm_exec_signed_min_for_width(width_bits, &signed_min) ||
        !vm_exec_signed_max_for_width(width_bits, &signed_max)) {
        return false;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    left_masked = left & mask;
    right_masked = right & mask;
    wide_result = (uint64_t)left_masked + (uint64_t)right_masked + (carry_in ? 1ULL : 0ULL);
    result = (uint32_t)wide_result & mask;
    signed_sum = vm_exec_signed_value_for_width(left_masked, width_bits) +
                 vm_exec_signed_value_for_width(right_masked, width_bits) +
                 (carry_in ? 1 : 0);

    if (!vm_cpu_write_flag(cpu, VM_FLAG_CF, wide_result > (uint64_t)mask) ||
        !vm_cpu_write_flag(cpu, VM_FLAG_ZF, result == 0U) ||
        !vm_cpu_write_flag(cpu, VM_FLAG_SF, (result & sign_bit) != 0U) ||
        !vm_cpu_write_flag(cpu, VM_FLAG_OF, signed_sum < signed_min || signed_sum > signed_max)) {
        return false;
    }

    *out_result = result;
    return true;
}

/// Applies arithmetic flags for SBB using the incoming carry bit as borrow.
///
/// @param cpu CPU state whose flags should be updated.
/// @param left Destination value before subtraction.
/// @param right Source value before subtraction.
/// @param borrow_in Current carry flag value used as borrow.
/// @param width_bits Operand width in bits.
/// @param out_result Receives the masked result on success.
/// @return true when flags and result were computed for a supported width.
static bool vm_exec_update_sbb_flags(VmCpu *cpu, uint32_t left, uint32_t right, bool borrow_in, uint8_t width_bits, uint32_t *out_result) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t left_masked = 0U;
    uint32_t right_masked = 0U;
    uint32_t result = 0U;
    uint64_t subtrahend = 0U;
    int64_t signed_difference = 0;
    int64_t signed_min = 0;
    int64_t signed_max = 0;

    if (cpu == NULL || out_result == NULL || !vm_exec_mask_for_width(width_bits, &mask) ||
        !vm_exec_signed_min_for_width(width_bits, &signed_min) ||
        !vm_exec_signed_max_for_width(width_bits, &signed_max)) {
        return false;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    left_masked = left & mask;
    right_masked = right & mask;
    subtrahend = (uint64_t)right_masked + (borrow_in ? 1ULL : 0ULL);
    result = (left_masked - right_masked - (borrow_in ? 1U : 0U)) & mask;
    signed_difference = vm_exec_signed_value_for_width(left_masked, width_bits) -
                        vm_exec_signed_value_for_width(right_masked, width_bits) -
                        (borrow_in ? 1 : 0);

    if (!vm_cpu_write_flag(cpu, VM_FLAG_CF, (uint64_t)left_masked < subtrahend) ||
        !vm_cpu_write_flag(cpu, VM_FLAG_ZF, result == 0U) ||
        !vm_cpu_write_flag(cpu, VM_FLAG_SF, (result & sign_bit) != 0U) ||
        !vm_cpu_write_flag(cpu, VM_FLAG_OF, signed_difference < signed_min || signed_difference > signed_max)) {
        return false;
    }

    *out_result = result;
    return true;
}

/// Executes one ADC instruction.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_adc(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    bool carry_in = false;
    uint32_t left = 0U;
    uint32_t right = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    before_cpu = vm->cpu;
    if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &carry_in)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &left);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &right);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (!vm_exec_update_adc_flags(&vm->cpu, left, right, carry_in, width_bits, &result)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}

/// Executes one SBB instruction.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_sbb(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    bool borrow_in = false;
    uint32_t left = 0U;
    uint32_t right = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    before_cpu = vm->cpu;
    if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &borrow_in)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &left);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &right);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (!vm_exec_update_sbb_flags(&vm->cpu, left, right, borrow_in, width_bits, &result)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}

/// Executes a carry-flag control instruction.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to validate.
/// @param opcode Carry-control opcode to execute.
/// @return Executor status.
static VmExecStatus vm_exec_execute_carry_control(Vm *vm, const VmIrInstruction *instruction, VmIrOpcode opcode) {
    bool carry = false;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (opcode == VM_IR_OPCODE_CLC) {
        return vm_cpu_clear_flag(&vm->cpu, VM_FLAG_CF) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (opcode == VM_IR_OPCODE_STC) {
        return vm_cpu_set_flag(&vm->cpu, VM_FLAG_CF) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (opcode == VM_IR_OPCODE_CMC) {
        if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &carry)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        return vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, !carry) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_INVALID_INSTRUCTION;
}

/// Returns whether operands are valid for XCHG and resolves their shared width.
///
/// @param destination First exchange operand.
/// @param source Second exchange operand.
/// @param out_width_bits Receives the shared operand width on success.
/// @return true when both operands are register or memory operands with matching supported widths.
static bool vm_exec_xchg_operands_are_supported(const VmIrOperand *destination, const VmIrOperand *source, uint8_t *out_width_bits) {
    uint8_t destination_width = 0U;
    uint8_t source_width = 0U;

    if (destination == NULL || source == NULL || out_width_bits == NULL) {
        return false;
    }
    if (!vm_exec_operand_is_destination(destination) || !vm_exec_operand_is_destination(source)) {
        return false;
    }
    if (vm_exec_operand_is_memory(destination) && vm_exec_operand_is_memory(source)) {
        return false;
    }
    if (!vm_exec_operand_width(destination, &destination_width) || !vm_exec_operand_width(source, &source_width)) {
        return false;
    }
    if (destination_width != source_width) {
        return false;
    }

    *out_width_bits = destination_width;
    return true;
}

/// Executes one XCHG instruction without modifying flags.
///
/// The implementation reads both operands before writing either operand. When
/// one operand is memory, the memory write is attempted before the register
/// write so checked-memory failures do not leave a register half-exchanged.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @return Executor status.
static VmExecStatus vm_exec_execute_xchg(Vm *vm, const VmIrInstruction *instruction) {
    uint8_t width_bits = 0U;
    uint32_t destination_value = 0U;
    uint32_t source_value = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_xchg_operands_are_supported(&instruction->destination, &instruction->source, &width_bits)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &destination_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &source_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (vm_exec_operand_is_memory(&instruction->source)) {
        status = vm_exec_write_operand(vm, instruction, &instruction->source, width_bits, destination_value);
        if (status != VM_EXEC_STATUS_OK) {
            return status;
        }
        return vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, source_value);
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, source_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    return vm_exec_write_operand(vm, instruction, &instruction->source, width_bits, destination_value);
}

/// Executes one NEG instruction and updates arithmetic flags.
///
/// NEG is modeled as subtraction from zero at the destination operand width.
/// This reuses the existing CF, ZF, SF, and OF update behavior and avoids
/// broadening the flag model beyond the flags currently tracked by the VM.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @return Executor status.
static VmExecStatus vm_exec_execute_neg(Vm *vm, const VmIrInstruction *instruction) {
    VmCpu before_cpu;
    uint8_t width_bits = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_operand_is_destination(&instruction->destination) || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->destination, &width_bits)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    if (!vm_cpu_update_sub_flags(&vm->cpu, 0U, value, width_bits, &result)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }
    return status;
}

/// Executes one INC or DEC instruction and preserves the carry flag.
///
/// INC and DEC are read-modify-write operations for memory destinations. The
/// arithmetic helper updates ZF, SF, and OF at the selected width; this wrapper
/// restores the incoming CF exactly as required by x86-compatible educational
/// behavior. If the final destination write fails, the CPU snapshot is restored
/// so validation failures leave registers and flags unchanged.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param is_increment true for INC, false for DEC.
/// @return Executor status.
static VmExecStatus vm_exec_execute_inc_dec(Vm *vm, const VmIrInstruction *instruction, bool is_increment) {
    VmCpu before_cpu;
    bool carry_before = false;
    uint8_t width_bits = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_operand_is_destination(&instruction->destination) || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->destination, &width_bits)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    if (!vm_cpu_read_flag(&vm->cpu, VM_FLAG_CF, &carry_before)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (is_increment) {
        if (!vm_cpu_update_add_flags(&vm->cpu, value, 1U, width_bits, &result)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else {
        if (!vm_cpu_update_sub_flags(&vm->cpu, value, 1U, width_bits, &result)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    }

    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, carry_before)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }
    return status;
}

/// Executes one NOT instruction without changing modeled flags.
///
/// NOT is a read-modify-write operation for memory destinations. It computes
/// the bitwise complement at the destination width and stores the result while
/// preserving CF, ZF, SF, and OF exactly. If the destination write fails, the
/// CPU snapshot is restored so validation failures leave registers and flags
/// unchanged.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @return Executor status.
static VmExecStatus vm_exec_execute_not(Vm *vm, const VmIrInstruction *instruction) {
    VmCpu before_cpu;
    uint8_t width_bits = 0U;
    uint32_t mask = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_operand_is_destination(&instruction->destination) || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->destination, &width_bits) || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->destination, width_bits, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    result = (~value) & mask;
    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }
    return status;
}

/// Executes one NOP instruction.
///
/// @param instruction Instruction to validate.
/// @return OK when both operand slots are empty, otherwise unsupported operand.
static VmExecStatus vm_exec_execute_nop(const VmIrInstruction *instruction) {
    if (instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    return instruction->destination.kind == VM_IR_OPERAND_NONE && instruction->source.kind == VM_IR_OPERAND_NONE ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
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

/// Executes the Irvine32 `exit` virtual terminator.
///
/// The instruction marks the VM halted without mutating registers, flags,
/// memory, or console state. The caller still records the instruction count and
/// last-step delta for the terminator itself.
///
/// @param vm VM instance to halt.
/// @param instruction EXIT instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_exit(Vm *vm, const VmIrInstruction *instruction) {
    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    vm->halted = true;
    return VM_EXEC_STATUS_OK;
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
        case VM_IR_OPCODE_ADC:
        case VM_IR_OPCODE_SBB:
        case VM_IR_OPCODE_TEST:
        case VM_IR_OPCODE_AND:
        case VM_IR_OPCODE_OR:
        case VM_IR_OPCODE_XOR:
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
            if (instruction->opcode == VM_IR_OPCODE_SUB) {
                return vm_exec_execute_sub(vm, instruction, width_bits);
            }
            if (instruction->opcode == VM_IR_OPCODE_ADC) {
                return vm_exec_execute_adc(vm, instruction, width_bits);
            }
            if (instruction->opcode == VM_IR_OPCODE_SBB) {
                return vm_exec_execute_sbb(vm, instruction, width_bits);
            }
            if (instruction->opcode == VM_IR_OPCODE_TEST) {
                return vm_exec_execute_test(vm, instruction, width_bits);
            }
            return vm_exec_execute_logical_binary(vm, instruction, width_bits);
        case VM_IR_OPCODE_SHL:
        case VM_IR_OPCODE_SAL:
        case VM_IR_OPCODE_SHR:
        case VM_IR_OPCODE_SAR:
        case VM_IR_OPCODE_ROL:
            if (!vm_exec_operands_are_supported(&instruction->destination, &instruction->source)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            if (!vm_exec_operand_width(&instruction->destination, &width_bits)) {
                return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
            }
            if (instruction->opcode == VM_IR_OPCODE_SHR) {
                return vm_exec_execute_shift_right(vm, instruction, width_bits);
            }
            if (instruction->opcode == VM_IR_OPCODE_SAR) {
                return vm_exec_execute_shift_arithmetic_right(vm, instruction, width_bits);
            }
            if (instruction->opcode == VM_IR_OPCODE_ROL) {
                return vm_exec_execute_rotate_left(vm, instruction, width_bits);
            }
            return vm_exec_execute_shift_left(vm, instruction, width_bits);
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
        case VM_IR_OPCODE_XCHG:
            return vm_exec_execute_xchg(vm, instruction);
        case VM_IR_OPCODE_NEG:
            return vm_exec_execute_neg(vm, instruction);
        case VM_IR_OPCODE_INC:
            return vm_exec_execute_inc_dec(vm, instruction, true);
        case VM_IR_OPCODE_DEC:
            return vm_exec_execute_inc_dec(vm, instruction, false);
        case VM_IR_OPCODE_NOT:
            return vm_exec_execute_not(vm, instruction);
        case VM_IR_OPCODE_NOP:
            return vm_exec_execute_nop(instruction);
        case VM_IR_OPCODE_CLC:
        case VM_IR_OPCODE_STC:
        case VM_IR_OPCODE_CMC:
            return vm_exec_execute_carry_control(vm, instruction, instruction->opcode);
        case VM_IR_OPCODE_EXIT:
            return vm_exec_execute_exit(vm, instruction);
        default:
            return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }
}

VmExecStatus vm_init_with_layout_policy(Vm *vm, const VmLayoutPolicy *layout_policy) {
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memset(vm, 0, sizeof(*vm));
    vm_cpu_init(&vm->cpu);
    memory_status = vm_memory_init_with_layout_policy(&vm->memory, layout_policy);
    if (memory_status != VM_MEMORY_STATUS_OK) {
        vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_MEMORY_ERROR);
        vm->last_diagnostic.memory_status = memory_status;
        return VM_EXEC_STATUS_MEMORY_ERROR;
    }

    vm_exec_clear_delta(&vm->last_delta);
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    return VM_EXEC_STATUS_OK;
}

VmExecStatus vm_init(Vm *vm, const VmMemoryConfig *memory_config) {
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (memory_config == NULL) {
        return vm_init_with_layout_policy(vm, NULL);
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
            {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE},
            {VM_IR_OPERAND_IMMEDIATE, 32U, 20U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE},
            "milestone4.asm",
            1U,
            "mov eax, 20",
            0U
        },
        {
            VM_IR_OPCODE_ADD,
            {VM_IR_OPERAND_REGISTER, 0U, 0U, VM_REGISTER_EAX, 0U, VM_IR_RELOCATION_NONE},
            {VM_IR_OPERAND_IMMEDIATE, 32U, 22U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE},
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
