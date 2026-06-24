/*
 * @file vm_exec.h
 * @brief IR executor for the MASM32 educational VM core.
 *
 * This module executes caller-provided IR instruction arrays. It supports the
 * currently implemented mov, add, sub, cmp, movsx, movzx, cbw, cwde, cwd, cdq,
 * xchg, neg, nop, adc, sbb, clc, stc, cmc, test, inc, dec, and, or, xor,
 * not, shl, sal, shr, sar, rol, ror, lea, mul, imul, div, idiv, Phase 61
 * direct-JMP runtime transfer, Phase 64 equality conditional jumps, Phase 65
 * signed relational conditional jumps, direct CALL, Phase 72 call-depth resource protection, Phase 71 root/helper plain near RET, Phase 72A source-level PUSH/POP, Phase 73 LEAVE, Phase 74 RET imm16 cleanup,
 * Phase 71D configurable procedure-fallthrough diagnostics, Phase 71E
 * entry-procedure auto-stop compatibility, Phase 71C code-stream end-falloff
 * diagnostics, and Irvine32 exit forms over the currently supported
 * operand shapes. Phase 59
 * source-run code layers an instruction-count watchdog over this executor.
 * Unsigned relational conditional jumps are supported for direct labels.
 * Phase 68A initializes ESP from the active stack region at program startup;
 * source-level PUSH/POP, LEAVE, and RET imm16 cleanup are supported, and Phase 77
 * saves and restores PROC USES registers for direct CALL/RET paths. Phase 79
 * creates and releases automatic LOCAL stack frames for selected-entry and
 * direct-CALL procedure paths. Phase 80 resolves supported source-level LOCAL
 * operands against active automatic frames. Phase 84 captures and commits the
 * accepted same-file user-procedure INVOKE DWORD argument subset. ENTER, far
 * returns, general source-level ADDR outside accepted INVOKE arguments, and
 * non-exit Irvine32 routines remain later milestones; Phase 69 direct user-procedure CALL
 * performs its internal checked return-token stack write, Phase 70 helper RET
 * performs its internal checked return-token stack read, and Phase 71 treats a
 * root-code-stream RET as successful program termination by default, and
 * Phase 71A can optionally reject that root RET in strict teaching mode.
 */

#ifndef MASM32_SIM_VM_EXEC_H
#define MASM32_SIM_VM_EXEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm_cpu.h"
#include "vm_ir.h"
#include "vm_memory.h"
#include "vm_console.h"

/// Maximum canonical register changes retained in one step delta.
#define VM_EXEC_MAX_REGISTER_CHANGES 9U

/// Maximum named flag changes retained in one step delta.
#define VM_EXEC_MAX_FLAG_CHANGES 4U

/// Maximum raw byte memory changes retained in one step delta.
#define VM_EXEC_MAX_MEMORY_CHANGES 64U

/// Maximum checked memory accesses retained in one step delta.
#define VM_EXEC_MAX_MEMORY_ACCESSES 4U

/// Maximum procedure boundaries retained for root, helper-fallthrough, and code-falloff checks.
#define VM_EXEC_MAX_PROCEDURE_BOUNDARIES 128U

/// Maximum ordered registers stored for one Phase 77 PROC USES save/restore list.
#define VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY 6U

/// Maximum copied bytes retained for one procedure or LOCAL identifier.
#define VM_EXEC_LOCAL_NAME_CAPACITY 64U

/// Maximum Phase 78 LOCAL declarations retained for one procedure.
#define VM_EXEC_PROCEDURE_LOCAL_CAPACITY 32U

/// Maximum Phase 84 DWORD INVOKE arguments captured before one INVOKE commit.
#define VM_EXEC_INVOKE_ARGUMENT_CAPACITY 16U

/// Maximum Phase 79 runtime LOCAL descriptors retained by the executor.
#define VM_EXEC_LOCAL_DESCRIPTOR_CAPACITY VM_MAX_CALL_DEPTH_LIMIT

/// Default Phase 72 direct user-procedure CALL depth limit.
#define VM_DEFAULT_CALL_DEPTH_LIMIT 64u

/// Minimum accepted Phase 72 direct user-procedure CALL depth limit.
#define VM_MIN_CALL_DEPTH_LIMIT 1u

/// Maximum accepted Phase 72 direct user-procedure CALL depth limit.
#define VM_MAX_CALL_DEPTH_LIMIT 4096u

/// Canonical Phase 68B pseudo-code-address base for displayed EIP.
#define VM_EXEC_PSEUDO_EIP_BASE 0x00401000U

/// Canonical Phase 68B pseudo-code-address stride between lowered instructions.
#define VM_EXEC_PSEUDO_EIP_STRIDE 4U

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
    /// A lowered direct CALL target was malformed or outside the loaded program.
    VM_EXEC_STATUS_INVALID_CALL_TARGET,
    /// A checked RET return token did not map to an executable pseudo-EIP instruction target.
    VM_EXEC_STATUS_INVALID_RETURN_ADDRESS,
    /// A Phase 74 RET imm16 cleanup would leave ESP outside the active stack boundary.
    VM_EXEC_STATUS_RET_STACK_CLEANUP_OUT_OF_RANGE,
    /// Execution crossed or left a procedure range without an explicit supported terminator or transfer.
    VM_EXEC_STATUS_PROCEDURE_FELL_THROUGH,
    /// Execution reached the end of the lowered executable code stream without an explicit terminator.
    VM_EXEC_STATUS_CODE_FELL_OFF_END,
    /// Optional Phase 71A strict root-RET mode rejected a root-code-stream RET.
    VM_EXEC_STATUS_ROOT_RET_DISALLOWED_BY_MODE,
    /// A direct user-procedure CALL would exceed the configured Phase 72 call-depth limit.
    VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED,
    /// The VM detected an impossible root/helper termination state.
    VM_EXEC_STATUS_INVALID_ROOT_TERMINATION_STATE,
    /// Execution reached an accepted branch form whose runtime behavior is still explicitly deferred.
    VM_EXEC_STATUS_BRANCH_RUNTIME_DEFERRED,
    /// Execution attempted to enter a PROC USES procedure without a supported CALL-created USES frame.
    VM_EXEC_STATUS_UNSUPPORTED_PROC_USES_RUNTIME,
    /// Automatic PROC USES or LOCAL save could not reserve or write the required stack frame.
    VM_EXEC_STATUS_STACK_OVERFLOW,
    /// Automatic PROC USES restore could not read the saved register frame.
    VM_EXEC_STATUS_STACK_UNDERFLOW,
    /// Automatic LOCAL frame state was inconsistent at entry, release, or return.
    VM_EXEC_STATUS_INVALID_FRAME_STATE,
    /// Execution attempted to enter a LOCAL procedure without a supported automatic frame.
    VM_EXEC_STATUS_LOCAL_FRAME_ENTRY_UNSUPPORTED,
    /// A Phase 80 LOCAL operand executed without its owning active automatic frame.
    VM_EXEC_STATUS_LOCAL_OPERAND_NO_ACTIVE_FRAME,
    /// Program Console output would exceed the configured byte or line limit.
    VM_EXEC_STATUS_CONSOLE_OUTPUT_LIMIT_EXCEEDED
} VmExecStatus;

/// Describes the lifetime state for one Phase 79 automatic LOCAL frame or descriptor.
typedef enum VmExecLocalFrameState {
    /// The frame or descriptor slot is not in use.
    VM_EXEC_LOCAL_FRAME_STATE_NONE = 0,
    /// The frame or descriptor is active for the currently executing procedure body.
    VM_EXEC_LOCAL_FRAME_STATE_ACTIVE,
    /// The frame or descriptor has been released and must not be released again.
    VM_EXEC_LOCAL_FRAME_STATE_INACTIVE
} VmExecLocalFrameState;

/// Describes one lowered Phase 78 LOCAL declaration attached to a procedure boundary.
typedef struct VmExecProcedureLocal {
    /// Copied LOCAL identifier for diagnostics and descriptor metadata.
    char local_name[VM_EXEC_LOCAL_NAME_CAPACITY];
    /// One-based source line of the LOCAL declaration.
    uint32_t source_line;
    /// One-based source column of the LOCAL declaration.
    uint32_t source_column;
    /// Zero-based source byte offset of the LOCAL declaration.
    uint32_t source_byte_offset;
    /// Source span length of the LOCAL declaration.
    uint32_t source_span_length;
    /// Declared element size in bytes.
    uint32_t element_size_bytes;
    /// Declared element count.
    uint32_t element_count;
    /// Total visible byte size before procedure-frame rounding.
    uint32_t total_size_bytes;
    /// Negative EBP-relative byte offset.
    int32_t ebp_offset;
} VmExecProcedureLocal;

/// Describes one procedure boundary used by root RET, helper fallthrough, and code-falloff checks.
typedef struct VmExecProcedureBoundary {
    /// Zero-based first executable instruction index in the procedure body.
    size_t start_instruction_index;
    /// Exclusive instruction index of the procedure ENDP boundary.
    size_t end_instruction_index;
    /// Whether this procedure is the selected END entry procedure.
    bool is_selected_entry;
    /// Whether the procedure contains at least one executable instruction.
    bool has_executable_instruction;
    /// Number of Phase 77 PROC USES registers attached to this procedure.
    size_t uses_register_count;
    /// Ordered canonical Phase 77 PROC USES registers saved on CALL entry and restored on RET.
    VmRegister uses_registers[VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY];
    /// Copied procedure identifier used in Phase 79 diagnostics and descriptors.
    char procedure_name[VM_EXEC_LOCAL_NAME_CAPACITY];
    /// One-based source line of the PROC declaration.
    uint32_t source_line;
    /// One-based source column of the PROC declaration.
    uint32_t source_column;
    /// Zero-based source byte offset of the PROC declaration.
    uint32_t source_byte_offset;
    /// Source span length of the PROC declaration.
    uint32_t source_span_length;
    /// Number of Phase 78 LOCAL metadata entries attached to this procedure.
    size_t local_count;
    /// Rounded Phase 79 LOCAL storage byte count reserved below saved EBP.
    uint32_t local_frame_size_bytes;
    /// Ordered Phase 78 LOCAL metadata used to create runtime descriptors.
    VmExecProcedureLocal locals[VM_EXEC_PROCEDURE_LOCAL_CAPACITY];
} VmExecProcedureBoundary;

/// Captures one active automatic Phase 79 LOCAL stack frame.
typedef struct VmExecLocalFrame {
    /// Procedure body start instruction index that owns this frame.
    size_t procedure_start_instruction_index;
    /// Monotonic nonzero frame identity assigned at setup.
    uint32_t frame_id;
    /// Stack address containing the saved caller EBP.
    uint32_t saved_ebp_address;
    /// Caller EBP value saved in the frame.
    uint32_t saved_ebp_value;
    /// EBP value installed for the active procedure body.
    uint32_t frame_base_address;
    /// ESP value after LOCAL bytes were reserved.
    uint32_t frame_stack_pointer;
    /// Rounded LOCAL storage byte count.
    uint32_t local_frame_size_bytes;
    /// Lifetime state for this frame.
    VmExecLocalFrameState state;
} VmExecLocalFrame;

/// Describes one active or recently released runtime LOCAL object.
typedef struct VmExecLocalDescriptor {
    /// Copied procedure identifier that owns the LOCAL.
    char procedure_name[VM_EXEC_LOCAL_NAME_CAPACITY];
    /// Copied LOCAL identifier.
    char local_name[VM_EXEC_LOCAL_NAME_CAPACITY];
    /// One-based source line of the LOCAL declaration.
    uint32_t source_line;
    /// One-based source column of the LOCAL declaration.
    uint32_t source_column;
    /// Zero-based source byte offset of the LOCAL declaration.
    uint32_t source_byte_offset;
    /// Source span length of the LOCAL declaration.
    uint32_t source_span_length;
    /// Active frame identity that owns this object.
    uint32_t frame_id;
    /// Runtime address of the first byte of this LOCAL object.
    uint32_t runtime_base_address;
    /// Total visible byte size.
    uint32_t byte_size;
    /// Declared element size in bytes.
    uint32_t element_size_bytes;
    /// Declared element count.
    uint32_t element_count;
    /// Negative EBP-relative byte offset.
    int32_t ebp_offset;
    /// Lifetime state for this descriptor.
    VmExecLocalFrameState state;
} VmExecLocalDescriptor;

/// Captures one active CALL-created Phase 77 PROC USES save/restore frame.
typedef struct VmExecUsesFrame {
    /// Procedure body start instruction index that owns this USES frame.
    size_t procedure_start_instruction_index;
    /// Number of saved registers in @ref uses_registers.
    size_t uses_register_count;
    /// Ordered canonical registers saved in declared PROC USES order.
    VmRegister uses_registers[VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY];
} VmExecUsesFrame;


/// Selects how root-code-stream RET is handled when no helper return is pending.
typedef enum VmRootRetMode {
    /// Preserve Phase 71 MASM32 Educational Mode behavior: root RET terminates successfully.
    VM_ROOT_RET_MODE_MASM32_COMPATIBLE = 0,
    /// Reject root RET as an opt-in teaching check that requires an active helper call frame.
    VM_ROOT_RET_MODE_STRICT_CALL_FRAME
} VmRootRetMode;

/// Selects the Phase 71D diagnostic policy for ordinary procedure-boundary fallthrough.
typedef enum VmProcedureFallthroughPolicy {
    /// Suppress procedure-fell-through diagnostics and continue execution.
    VM_PROCEDURE_FALLTHROUGH_POLICY_OFF = 0,
    /// Emit a non-fatal procedure-fell-through warning and continue execution.
    VM_PROCEDURE_FALLTHROUGH_POLICY_WARN,
    /// Emit a runtime error before executing the destination procedure instruction.
    VM_PROCEDURE_FALLTHROUGH_POLICY_ERROR
} VmProcedureFallthroughPolicy;

/// Selects Phase 71E selected-entry ENDP boundary handling.
typedef enum VmEntryProcedureEndMode {
    /// Preserve realistic code-stream execution through selected-entry ENDP boundaries.
    VM_ENTRY_PROCEDURE_END_MODE_CODE_STREAM = 0,
    /// Stop successfully when ordinary execution reaches the selected entry procedure boundary.
    VM_ENTRY_PROCEDURE_END_MODE_STOP_AT_ENTRY_END
} VmEntryProcedureEndMode;

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
    /// Current direct user-procedure CALL depth before a rejected CALL.
    uint32_t current_call_depth;
    /// Attempted direct user-procedure CALL depth for a rejected CALL.
    uint32_t attempted_call_depth;
    /// Configured direct user-procedure CALL depth limit for a rejected CALL.
    uint32_t call_depth_limit;
    /// Whether @ref procedure_name contains a procedure identifier.
    bool has_procedure_name;
    /// Procedure identifier associated with a frame diagnostic.
    char procedure_name[VM_EXEC_LOCAL_NAME_CAPACITY];
    /// Whether @ref operation_stage contains a frame operation stage.
    bool has_operation_stage;
    /// Frame operation stage associated with a diagnostic.
    char operation_stage[VM_EXEC_LOCAL_NAME_CAPACITY];
    /// Whether @ref relevant_byte_count is meaningful.
    bool has_relevant_byte_count;
    /// Byte count associated with a frame diagnostic.
    uint32_t relevant_byte_count;
    /// Whether @ref relevant_address is meaningful.
    bool has_relevant_address;
    /// Address associated with a frame diagnostic.
    uint32_t relevant_address;
    /// Whether @ref local_name identifies a LOCAL operand.
    bool has_local_name;
    /// LOCAL identifier associated with a Phase 80 operand diagnostic.
    char local_name[VM_EXEC_LOCAL_NAME_CAPACITY];
} VmExecDiagnostic;

/// Captures the most recent Phase 71D procedure-fallthrough event.
typedef struct VmProcedureFallthroughDiagnostic {
    /// Whether this record contains a procedure-fell-through event.
    bool has_diagnostic;
    /// Status represented by this diagnostic.
    VmExecStatus status;
    /// Instruction index that caused the boundary crossing.
    uint32_t from_instruction_index;
    /// Instruction index reached by the boundary crossing, or program count at code end.
    uint32_t to_instruction_index;
    /// Whether @ref from_instruction contains a meaningful instruction copy.
    bool has_from_instruction;
    /// Instruction that caused the boundary crossing when available.
    VmIrInstruction from_instruction;
} VmProcedureFallthroughDiagnostic;

/// Owns CPU, memory, loaded IR program, and last-step diagnostics for VM execution.
typedef struct Vm {
    /// CPU register and flag state.
    VmCpu cpu;
    /// Checked simulated memory regions.
    VmMemory memory;
    /// Simulated Program Console output stream, separate from Simulator Messages.
    VmConsole program_console;
    /// Loaded IR instruction array owned by the caller.
    const VmIrInstruction *program;
    /// Number of instructions in @ref program.
    size_t program_count;
    /// Zero-based index of the next instruction to execute.
    size_t instruction_pointer;
    /// Total number of successfully executed IR instructions.
    uint64_t instruction_count;
    /// Whether the loaded program has halted through an explicit supported terminator.
    bool halted;
    /// Last-step observable changes.
    VmExecDelta last_delta;
    /// Last structured executor diagnostic.
    VmExecDiagnostic last_diagnostic;
    /// Last non-fatal or fatal procedure-fell-through diagnostic metadata.
    VmProcedureFallthroughDiagnostic last_procedure_fallthrough_diagnostic;
    /// Procedure boundaries used by Phase 71 root RET and fallthrough checks.
    VmExecProcedureBoundary procedure_boundaries[VM_EXEC_MAX_PROCEDURE_BOUNDARIES];
    /// Number of valid entries in @ref procedure_boundaries.
    size_t procedure_boundary_count;
    /// Whether @ref selected_entry_procedure_index identifies the selected entry procedure.
    bool has_selected_entry_procedure;
    /// Index in @ref procedure_boundaries for the selected entry procedure.
    size_t selected_entry_procedure_index;
    /// Internal count of committed helper return tokens currently pending.
    size_t active_helper_return_count;
    /// Phase 72 count of committed direct user-procedure CALL frames not yet returned by helper RET.
    size_t current_call_depth;
    /// Active Phase 77 PROC USES frames created by direct CALL entry.
    VmExecUsesFrame active_uses_frames[VM_MAX_CALL_DEPTH_LIMIT];
    /// Number of valid entries in @ref active_uses_frames.
    size_t active_uses_frame_count;
    /// Whether execution is currently in the selected-entry root code stream rather than an explicit helper CALL.
    bool root_code_stream_active;
    /// Whether Phase 71E selected-entry boundary auto-stop remains eligible for this run.
    bool selected_entry_end_stop_eligible;
    /// Selected Phase 71A root RET handling mode.
    VmRootRetMode root_ret_mode;
    /// Selected Phase 71D procedure-fallthrough diagnostic policy.
    VmProcedureFallthroughPolicy procedure_fallthrough_policy;
    /// Selected Phase 71E selected-entry procedure end behavior.
    VmEntryProcedureEndMode entry_procedure_end_mode;
    /// Configured Phase 72 direct user-procedure CALL depth limit.
    uint32_t call_depth_limit;
    /// Active Phase 79 automatic LOCAL frames.
    VmExecLocalFrame active_local_frames[VM_MAX_CALL_DEPTH_LIMIT];
    /// Number of active or LEAVE-released LOCAL frame slots.
    size_t active_local_frame_count;
    /// Phase 79 runtime LOCAL object descriptors.
    VmExecLocalDescriptor local_descriptors[VM_EXEC_LOCAL_DESCRIPTOR_CAPACITY];
    /// Number of LOCAL object descriptors currently retained.
    size_t local_descriptor_count;
    /// Next monotonic automatic LOCAL frame identity.
    uint32_t next_local_frame_id;
    /// Whether selected-entry automatic LOCAL frame setup has already been attempted.
    bool selected_entry_local_frame_created;
    /// Internal Phase 84 INVOKE DWORD argument values captured before stack mutation.
    uint32_t pending_invoke_arguments[VM_EXEC_INVOKE_ARGUMENT_CAPACITY];
    /// Validity bits for @ref pending_invoke_arguments entries.
    bool pending_invoke_argument_valid[VM_EXEC_INVOKE_ARGUMENT_CAPACITY];
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

/// Maps a lowered VM instruction index to its displayed pseudo-EIP value.
///
/// The returned value is a deterministic control-flow token, not a VM memory
/// address, native instruction pointer, PE RVA, or encoded instruction byte
/// offset.
///
/// @param instruction_index Zero-based lowered executable VM instruction index.
/// @param out_pseudo_eip Receives the pseudo-code-address display value.
/// @return true when the index can be represented without 32-bit overflow.
bool vm_exec_instruction_index_to_pseudo_eip(size_t instruction_index, uint32_t *out_pseudo_eip);

/// Maps a valid displayed pseudo-EIP value back to a loaded instruction index.
///
/// This Phase 68B reverse mapping is used by Phase 70 procedure-return
/// validation. It accepts only values aligned to the canonical pseudo-code
/// stride and inside the supplied executable instruction count.
///
/// @param pseudo_eip Displayed pseudo-code-address control token.
/// @param instruction_count Number of lowered executable VM instructions.
/// @param out_instruction_index Receives the zero-based instruction index.
/// @return true when @p pseudo_eip names an aligned loaded instruction.
bool vm_exec_pseudo_eip_to_instruction_index(uint32_t pseudo_eip, size_t instruction_count, size_t *out_instruction_index);

/// Synchronizes displayed EIP with the VM's current instruction pointer.
///
/// If the VM has no loaded executable instruction at the current pointer, the
/// display value is reset to @ref VM_EXEC_PSEUDO_EIP_BASE. The helper does not
/// mark EIP as a source-level register write.
///
/// @param vm VM instance to synchronize.
/// @return true when the display value was synchronized.
bool vm_sync_display_eip(Vm *vm);

/// Initializes ESP to the active stack region's documented empty-stack value.
///
/// Phase 68A defines the empty stack as the first address past the high end of
/// the active stack region. Phase 72A 32-bit PUSH computes ESP - 4 first
/// and writes the 4-byte value through checked stack memory.
/// This helper uses initialized memory-region metadata rather than fixed-layout
/// constants, so fixed, automatic, seeded-randomized, and future layout-policy
/// callers share the same startup contract.
///
/// @param vm VM whose initialized memory layout supplies the stack region.
/// @return VM_EXEC_STATUS_OK on success, or a status describing failure.
VmExecStatus vm_initialize_stack_pointer(Vm *vm);

/// Configures root, procedure-fallthrough, and code-falloff procedure metadata for a loaded VM.
///
/// The executor copies the supplied boundaries and uses them only to distinguish
/// root-code-stream RET success from ordinary helper RET token reads, apply
/// Phase 71D procedure-fallthrough policy, and retain enough selected-entry
/// boundary metadata for Phase 71C code-stream falloff handling. The count is not
/// a Phase 72 call-depth limit or public call trace. Passing zero boundaries
/// clears the metadata.
///
/// @param vm VM instance to mutate.
/// @param boundaries Procedure-boundary records to copy; may be NULL only when count is zero.
/// @param boundary_count Number of boundary records supplied.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT.
VmExecStatus vm_configure_procedure_boundaries(Vm *vm, const VmExecProcedureBoundary *boundaries, size_t boundary_count);

/// Configures optional Phase 71A root-code-stream RET strictness.
///
/// The default is @ref VM_ROOT_RET_MODE_MASM32_COMPATIBLE. Strict mode only
/// rejects root-code-stream RET with no helper return pending; ordinary
/// helper RET behavior and Phase 70 token validation are unchanged.
///
/// @param vm VM instance to mutate.
/// @param mode Root RET handling mode.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT for an invalid argument or mode.
VmExecStatus vm_set_root_ret_mode(Vm *vm, VmRootRetMode mode);

/// Configures Phase 71D procedure-boundary fallthrough diagnostics.
///
/// The default is @ref VM_PROCEDURE_FALLTHROUGH_POLICY_WARN. OFF mode
/// suppresses only procedure-fell-through, WARN mode records a non-fatal
/// event and continues, and ERROR mode stops before the destination procedure
/// instruction executes. Other diagnostics, including code-fell-off-end, are
/// independent.
///
/// @param vm VM instance to mutate.
/// @param policy Procedure-fallthrough diagnostic policy.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT for an invalid argument or policy.
VmExecStatus vm_set_procedure_fallthrough_policy(Vm *vm, VmProcedureFallthroughPolicy policy);


/// Configures Phase 71E selected-entry procedure end handling.
///
/// The default is @ref VM_ENTRY_PROCEDURE_END_MODE_CODE_STREAM. The
/// compatibility mode stops only when ordinary sequential execution reaches
/// the selected entry procedure ENDP boundary. It does not make ENDP an
/// executable instruction and does not suppress diagnostics after explicit
/// CALL, RET, or branch paths have left the selected entry procedure.
///
/// @param vm VM instance to mutate.
/// @param mode Entry procedure end behavior.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT for an invalid argument or mode.
VmExecStatus vm_set_entry_procedure_end_mode(Vm *vm, VmEntryProcedureEndMode mode);

/// Configures the Phase 72 direct user-procedure CALL depth limit.
///
/// Accepted values are @ref VM_MIN_CALL_DEPTH_LIMIT through
/// @ref VM_MAX_CALL_DEPTH_LIMIT inclusive. The default is
/// @ref VM_DEFAULT_CALL_DEPTH_LIMIT. Invalid values leave the previous limit
/// unchanged and set an invalid-argument diagnostic.
///
/// @param vm VM instance to mutate.
/// @param limit Maximum committed direct user-procedure CALL depth.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT.
VmExecStatus vm_set_call_depth_limit(Vm *vm, uint32_t limit);

/// Returns the configured Phase 72 direct user-procedure CALL depth limit.
///
/// @param vm VM instance to inspect.
/// @return Configured limit, or zero when @p vm is NULL.
uint32_t vm_call_depth_limit(const Vm *vm);

/// Returns the current Phase 72 direct user-procedure CALL depth.
///
/// @param vm VM instance to inspect.
/// @return Current depth, or zero when @p vm is NULL.
uint32_t vm_current_call_depth(const Vm *vm);

/// Returns the most recent Phase 71D procedure-fallthrough diagnostic metadata.
///
/// @param vm VM instance to inspect.
/// @return Pointer to the VM-owned diagnostic record, or NULL when @p vm is NULL.
const VmProcedureFallthroughDiagnostic *vm_last_procedure_fallthrough_diagnostic(const Vm *vm);

/// Returns the Program Console stream owned by a VM.
///
/// @param vm VM instance to inspect.
/// @return Pointer to the VM-owned Program Console, or NULL when @p vm is NULL.
const VmConsole *vm_program_console(const Vm *vm);

/// Appends simulated program output through the VM-owned Program Console.
///
/// Future Irvine32 output routines should use this helper so output-limit
/// failures become executor diagnostics instead of silently mutating the
/// console. The helper commits the complete span or no bytes at all.
///
/// @param vm VM instance whose Program Console receives output.
/// @param text Bytes to append; may be NULL only when @p byte_count is zero.
/// @param byte_count Number of bytes to append.
/// @param instruction Optional instruction associated with a limit diagnostic.
/// @return VM_EXEC_STATUS_OK on success, or an executor status describing failure.
VmExecStatus vm_append_program_console_output(Vm *vm, const char *text, size_t byte_count, const VmIrInstruction *instruction);

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
