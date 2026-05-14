/*
 * @file vm_exec.h
 * @brief Minimal Milestone 4 IR executor for the MASM32 educational VM core.
 *
 * This module executes a hardcoded or caller-provided array of minimal IR
 * instructions. It supports only mov, add, and sub over immediate, register,
 * and absolute memory-address operands. Parser, control flow, stack behavior,
 * Irvine32 routines, and browser integration are intentionally deferred.
 */

#ifndef MASM32_SIM_VM_EXEC_H
#define MASM32_SIM_VM_EXEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm_cpu.h"
#include "vm_ir.h"
#include "vm_memory.h"

/// Maximum canonical register changes retained in one Milestone 4 step delta.
#define VM_EXEC_MAX_REGISTER_CHANGES 9U

/// Maximum named flag changes retained in one Milestone 4 step delta.
#define VM_EXEC_MAX_FLAG_CHANGES 4U

/// Maximum raw byte memory changes retained in one Milestone 4 step delta.
#define VM_EXEC_MAX_MEMORY_CHANGES 64U

/// Describes the result of one executor operation.
typedef enum VmExecStatus {
    /// Operation completed and, for step, one instruction was executed.
    VM_EXEC_STATUS_OK = 0,
    /// Execution has reached the end of the loaded program.
    VM_EXEC_STATUS_HALTED,
    /// Operation failed because an argument was invalid.
    VM_EXEC_STATUS_INVALID_ARGUMENT,
    /// Operation failed because an opcode or instruction descriptor was invalid.
    VM_EXEC_STATUS_INVALID_INSTRUCTION,
    /// Operation failed because the operand combination is not supported in Milestone 4.
    VM_EXEC_STATUS_UNSUPPORTED_OPERAND,
    /// Operation failed because a checked memory access failed.
    VM_EXEC_STATUS_MEMORY_ERROR
} VmExecStatus;

/// Describes one canonical register value change produced by a step.
typedef struct VmExecRegisterChange {
    /// Register whose canonical value changed.
    VmRegister reg;
    /// Value before the instruction executed.
    uint32_t old_value;
    /// Value after the instruction executed.
    uint32_t new_value;
} VmExecRegisterChange;

/// Describes one named flag change produced by a step.
typedef struct VmExecFlagChange {
    /// Named flag whose value changed.
    VmFlag flag;
    /// Flag value before the instruction executed.
    bool old_is_set;
    /// Flag value after the instruction executed.
    bool new_is_set;
} VmExecFlagChange;

/// Captures the observable effects of the last executed Milestone 4 instruction.
typedef struct VmExecDelta {
    /// Whether @ref instruction contains an executed instruction.
    bool has_instruction;
    /// Copy of the IR instruction that was executed.
    VmIrInstruction instruction;
    /// Canonical register changes produced by the step.
    VmExecRegisterChange register_changes[VM_EXEC_MAX_REGISTER_CHANGES];
    /// Number of valid entries in @ref register_changes.
    size_t register_change_count;
    /// Named flag changes produced by the step.
    VmExecFlagChange flag_changes[VM_EXEC_MAX_FLAG_CHANGES];
    /// Number of valid entries in @ref flag_changes.
    size_t flag_change_count;
    /// Raw byte-level memory changes produced by the step.
    VmMemoryByteChange memory_changes[VM_EXEC_MAX_MEMORY_CHANGES];
    /// Number of valid entries in @ref memory_changes.
    size_t memory_change_count;
    /// Whether additional memory changes were dropped from the fixed delta buffer.
    bool memory_change_overflowed;
    /// Total executed instruction count after the step completed.
    uint64_t instruction_count;
} VmExecDelta;

/// Captures structured failure context from the last executor operation.
typedef struct VmExecDiagnostic {
    /// Status produced by the operation.
    VmExecStatus status;
    /// Instruction index associated with the status when available.
    uint32_t instruction_index;
    /// Whether @ref instruction contains a meaningful instruction copy.
    bool has_instruction;
    /// Instruction associated with the status when available.
    VmIrInstruction instruction;
    /// Memory status when @ref status is VM_EXEC_STATUS_MEMORY_ERROR.
    VmMemoryStatus memory_status;
    /// Structured memory diagnostic when a checked memory operation failed.
    VmMemoryDiagnostic memory_diagnostic;
} VmExecDiagnostic;

/// Owns Milestone 4 CPU, memory, loaded IR program, and last-step diagnostics.
typedef struct Vm {
    /// CPU register and flag state.
    VmCpu cpu;
    /// Checked simulated memory regions.
    VmMemory memory;
    /// Loaded IR instruction array owned by the caller.
    const VmIrInstruction *program;
    /// Number of instructions in @ref program.
    size_t program_count;
    /// Zero-based index of the next instruction to execute.
    size_t instruction_pointer;
    /// Total number of successfully executed IR instructions.
    uint64_t instruction_count;
    /// Whether the loaded program has halted by reaching its end.
    bool halted;
    /// Last-step observable changes.
    VmExecDelta last_delta;
    /// Last structured executor diagnostic.
    VmExecDiagnostic last_diagnostic;
} Vm;

/// Initializes a Milestone 4 VM instance.
///
/// @param vm VM instance to initialize.
/// @param memory_config Optional memory-size configuration; NULL uses defaults.
/// @return VM_EXEC_STATUS_OK on success, or a status describing failure.
VmExecStatus vm_init(Vm *vm, const VmMemoryConfig *memory_config);

/// Releases resources owned by a VM instance.
///
/// @param vm VM instance to release. NULL is ignored.
void vm_deinit(Vm *vm);

/// Loads a caller-owned IR instruction array into the VM.
///
/// Loading resets instruction position, instruction count, halted state, delta,
/// diagnostics, CPU state, and memory-change recording. Memory contents remain
/// initialized but are not otherwise rewritten.
///
/// @param vm VM instance to mutate.
/// @param program Caller-owned instruction array.
/// @param program_count Number of entries in @p program.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT.
VmExecStatus vm_load_program(Vm *vm, const VmIrInstruction *program, size_t program_count);

/// Executes exactly one IR instruction from the loaded program.
///
/// @param vm VM instance to step.
/// @return VM_EXEC_STATUS_OK when one instruction executes, VM_EXEC_STATUS_HALTED
/// when the program has ended, or an error status.
VmExecStatus vm_step(Vm *vm);

/// Returns the last-step delta for a VM.
///
/// @param vm VM instance to inspect.
/// @return Pointer to the last delta, or NULL for invalid input.
const VmExecDelta *vm_last_delta(const Vm *vm);

/// Returns the last executor diagnostic for a VM.
///
/// @param vm VM instance to inspect.
/// @return Pointer to the last diagnostic, or NULL for invalid input.
const VmExecDiagnostic *vm_last_diagnostic(const Vm *vm);

/// Returns a stable lowercase name for an executor status.
///
/// @param status Executor status to inspect.
/// @return Static status name, or NULL for invalid status values.
const char *vm_exec_status_name(VmExecStatus status);

/// Runs the Milestone 4 hardcoded IR sample program.
///
/// The program is equivalent to:
/// mov eax, 20
/// add eax, 22
///
/// @param out_eax Receives final EAX on success.
/// @return VM_EXEC_STATUS_OK on success, or an executor status describing failure.
VmExecStatus vm_run_milestone4_hardcoded_program(uint32_t *out_eax);

#endif
