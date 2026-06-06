/*
 * @file vm_exec.h
 * @brief IR executor for the MASM32 educational VM core.
 *
 * This module executes caller-provided IR instruction arrays. It supports the
 * currently implemented mov, add, sub, cmp, movsx, movzx, cbw, cwde, cwd, cdq,
 * xchg, neg, nop, adc, sbb, clc, stc, cmc, test, inc, dec, and, or, xor,
 * not, shl, sal, shr, sar, rol, ror, lea, mul, imul, div, idiv, Phase 61
 * direct-JMP runtime transfer, Phase 64 equality conditional jumps, Phase 65
 * signed relational conditional jumps, and Irvine32 exit forms over the
 * currently supported operand shapes. Phase 59 source-run code layers an
 * instruction-count watchdog over this executor. Unsigned relational conditional
 * jumps are supported for direct labels. Phase 68A initializes ESP from the
 * active stack region at program startup; source-level stack instructions,
 * procedure frames, CALL/RET stack mutation, and non-exit Irvine32 routines
 * remain later milestones.
 */

#ifndef MASM32_SIM_VM_EXEC_H
#define MASM32_SIM_VM_EXEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm_cpu.h"
#include "vm_ir.h"
#include "vm_memory.h"

/// Maximum canonical register changes retained in one step delta.
#define VM_EXEC_MAX_REGISTER_CHANGES 9U

/// Maximum named flag changes retained in one step delta.
#define VM_EXEC_MAX_FLAG_CHANGES 4U

/// Maximum raw byte memory changes retained in one step delta.
#define VM_EXEC_MAX_MEMORY_CHANGES 64U

/// Maximum checked memory accesses retained in one step delta.
#define VM_EXEC_MAX_MEMORY_ACCESSES 4U

/// Maximum modeled flags that can be consumed by one flag-consuming instruction.
#define VM_EXEC_MAX_CONSUMED_FLAGS VM_FLAG_COUNT

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
    /// Operation failed because the operand combination is not supported by the current execution subset.
    VM_EXEC_STATUS_UNSUPPORTED_OPERAND,
    /// Operation failed because a checked memory access failed.
    VM_EXEC_STATUS_MEMORY_ERROR,
    /// Operation stopped because division attempted to divide by zero.
    VM_EXEC_STATUS_DIVIDE_BY_ZERO,
    /// Operation stopped because a division quotient does not fit the destination register.
    VM_EXEC_STATUS_QUOTIENT_OVERFLOW,
    /// Operation stopped before consuming an architecturally undefined modeled flag.
    VM_EXEC_STATUS_UNDEFINED_FLAG_USE,
    /// Source-run execution stopped before fetching the next instruction because the configured instruction limit was reached.
    VM_EXEC_STATUS_INSTRUCTION_LIMIT_EXCEEDED,
    /// A lowered direct branch target was malformed or outside the loaded program.
    VM_EXEC_STATUS_INVALID_BRANCH_TARGET,
    /// Execution reached an accepted branch form whose runtime behavior is still explicitly deferred.
    VM_EXEC_STATUS_BRANCH_RUNTIME_DEFERRED
} VmExecStatus;

/// Selects Phase 50B diagnostics for using architecturally undefined modeled flags.
typedef enum VmUndefinedFlagUsePolicy {
    /// Do not diagnose invalid flag consumption; use deterministic fallback bits.
    VM_UNDEFINED_FLAG_USE_POLICY_OFF = 0,
    /// Emit a non-fatal warning and continue using deterministic fallback bits.
    VM_UNDEFINED_FLAG_USE_POLICY_WARN,
    /// Emit a runtime error before the consumer uses the invalid flag.
    VM_UNDEFINED_FLAG_USE_POLICY_ERROR
} VmUndefinedFlagUsePolicy;

/// Describes one Phase 50B invalid flag-consumption diagnostic.
typedef struct VmFlagUseDiagnostic {
    /// Status represented by this diagnostic.
    VmExecStatus status;
    /// Whether @ref consumer_instruction contains a copied instruction.
    bool has_consumer_instruction;
    /// Instruction that attempted to consume the invalid flag value.
    VmIrInstruction consumer_instruction;
    /// Invalid consumed flags, in the order requested by the consumer.
    VmFlag invalid_flags[VM_EXEC_MAX_CONSUMED_FLAGS];
    /// Undefined-origin metadata for each invalid consumed flag.
    VmFlagValidityMetadata invalid_metadata[VM_EXEC_MAX_CONSUMED_FLAGS];
    /// Number of invalid consumed flags recorded in this diagnostic.
    size_t invalid_flag_count;
} VmFlagUseDiagnostic;


/// Identifies whether one checked memory access read or wrote memory.
typedef enum VmExecMemoryAccessKind {
    /// Memory access read bytes from a checked region.
    VM_EXEC_MEMORY_ACCESS_READ = 0,
    /// Memory access wrote bytes to a checked region.
    VM_EXEC_MEMORY_ACCESS_WRITE
} VmExecMemoryAccessKind;

/// Describes one checked memory access attempted by an executed instruction.
typedef struct VmExecMemoryAccess {
    /// Whether the access was a read or write.
    VmExecMemoryAccessKind kind;
    /// Effective simulated address used by the access.
    uint32_t address;
    /// Access width in bits.
    uint8_t width_bits;
    /// Status returned by the checked memory helper.
    VmMemoryStatus status;
} VmExecMemoryAccess;

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

/// Captures the observable effects of the last executed instruction.
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
    /// Checked memory accesses attempted by the instruction.
    VmExecMemoryAccess memory_accesses[VM_EXEC_MAX_MEMORY_ACCESSES];
    /// Number of valid entries in @ref memory_accesses.
    size_t memory_access_count;
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

/// Owns CPU, memory, loaded IR program, and last-step diagnostics for VM execution.
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

/// Initializes a VM instance for the currently implemented execution subset.
///
/// @param vm VM instance to initialize.
/// @param memory_config Optional memory-size configuration; NULL uses defaults.
/// @return VM_EXEC_STATUS_OK on success, or a status describing failure.
VmExecStatus vm_init(Vm *vm, const VmMemoryConfig *memory_config);

/// Initializes a VM instance from an explicit memory layout policy.
///
/// Passing NULL for @p layout_policy uses @ref vm_layout_default_policy. The
/// selected layout metadata also supplies the Phase 68A ESP startup value.
///
/// @param vm VM instance to initialize.
/// @param layout_policy Optional memory layout policy.
/// @return VM_EXEC_STATUS_OK on success, or a status describing failure.
VmExecStatus vm_init_with_layout_policy(Vm *vm, const VmLayoutPolicy *layout_policy);

/// Initializes ESP to the active stack region's documented empty-stack value.
///
/// Phase 68A defines the empty stack as the first address past the high end of
/// the active stack region. Future 32-bit push-like operations must compute
/// ESP - 4 first and then write the 4-byte value through checked stack memory.
/// This helper uses initialized memory-region metadata rather than fixed-layout
/// constants, so fixed, automatic, seeded-randomized, and future layout-policy
/// callers share the same startup contract.
///
/// @param vm VM whose initialized memory layout supplies the stack region.
/// @return VM_EXEC_STATUS_OK on success, or a status describing failure.
VmExecStatus vm_initialize_stack_pointer(Vm *vm);

/// Releases resources owned by a VM instance.
///
/// @param vm VM instance to release. NULL is ignored.
void vm_deinit(Vm *vm);

/// Loads a caller-owned IR instruction array into the VM.
///
/// Loading resets instruction position, instruction count, halted state, delta,
/// diagnostics, CPU state, ESP startup state, and memory-change recording.
/// Memory contents remain initialized but are not otherwise rewritten.
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

/// Checks whether a flag consumer is about to read architecturally undefined modeled flags.
///
/// In OFF mode, this helper returns success and emits no diagnostic even when
/// requested flags are invalid. In WARN mode, it fills @p out_diagnostic when
/// invalid flags are found but returns success. In ERROR mode, it fills
/// @p out_diagnostic and returns VM_EXEC_STATUS_UNDEFINED_FLAG_USE before the
/// caller makes a flag-dependent decision or mutation.
///
/// @param cpu CPU state whose flag-validity metadata should be inspected.
/// @param consumer_instruction Instruction consuming the requested flags.
/// @param required_flags Flags read by the consumer instruction.
/// @param required_flag_count Number of entries in @p required_flags.
/// @param policy Undefined-flag-use policy to apply.
/// @param out_diagnostic Optional diagnostic populated for WARN or ERROR findings.
/// @return VM_EXEC_STATUS_OK when execution may continue, VM_EXEC_STATUS_UNDEFINED_FLAG_USE for ERROR findings, or VM_EXEC_STATUS_INVALID_ARGUMENT for invalid inputs.
VmExecStatus vm_check_flag_consumption(
    const VmCpu *cpu,
    const VmIrInstruction *consumer_instruction,
    const VmFlag *required_flags,
    size_t required_flag_count,
    VmUndefinedFlagUsePolicy policy,
    VmFlagUseDiagnostic *out_diagnostic
);

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
