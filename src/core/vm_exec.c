/*
 * @file vm_exec.c
 * @brief Executor for implemented MASM32 simulator IR programs.
 *
 * The executor intentionally supports only a staged vertical slice: mov, add,
 * sub, cmp, movsx, movzx, cbw, cwde, cwd, cdq, xchg, neg, nop, adc, sbb, clc, stc, cmc,
 * test, inc, dec, and, or, xor, not, shl, sal, shr, sar, rol, ror,
 * lea, mul, imul, div, idiv, Phase 61 direct-JMP runtime transfer,
 * Phase 64 equality conditional jumps, Phase 65 signed relational conditional
 * jumps, Phase 66 unsigned relational conditional jumps, Phase 69 direct CALL, Phase 72 call-depth resource protection,
 * Phase 70 helper plain near RET, Phase 71 root-code-stream RET termination, Phase 72A source-level PUSH/POP, Phase 73 LEAVE, Phase 74 RET imm16 cleanup,
 * Phase 71D configurable procedure-fallthrough diagnostics, Phase 71E
 * entry-procedure auto-stop compatibility, Phase 71C code-stream
 * end-falloff diagnostics, and Irvine32 exit
 * over the currently supported register and memory operand forms. It records
 * last-step deltas by snapshotting CPU state and copying memory-module byte
 * changes after each successful step. Phase 68A initializes ESP from the active
 * stack region at startup; source-level PUSH/POP, LEAVE, and RET imm16 cleanup are supported,
 * and Phase 77 saves and restores PROC USES registers on supported direct CALL/RET paths. Phase 79 creates automatic LOCAL frames for selected-entry and direct-CALL procedure paths, and Phase 80 resolves supported source-level LOCAL operands through active frame-relative storage. Phase 84 captures and commits accepted same-file user-procedure INVOKE DWORD arguments, including ADDR active-LOCAL arguments. ENTER, far returns, general source-level ADDR outside accepted INVOKE arguments, and non-exit
 * Irvine32 routines remain later milestones. Phase 69
 * implements direct user-procedure CALL as a checked internal stack write,
 * Phase 70 implements helper RET as a checked internal stack read, and Phase 71
 * treats an eligible root-code-stream RET as successful termination. Phase 68B
 * displays EIP as VM control-state pseudo-code address metadata instead of a
 * source-writable or delta-tracked register.
 */

#include "vm_exec.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/// Number of source-visible canonical registers captured for register deltas.
#define VM_EXEC_CANONICAL_DELTA_REGISTER_COUNT 8U

/// Validates a direct-CALL-style branch target before stack mutation.
static VmExecStatus vm_exec_validate_call_target(const Vm *vm, const VmIrInstruction *instruction);

/// Writes the internal direct-CALL return token, USES saves, and LOCAL frame.
static VmExecStatus vm_exec_write_call_and_uses_frame(
    Vm *vm,
    const VmIrInstruction *instruction,
    uint32_t return_token,
    const VmExecProcedureBoundary *target_boundary
);

/// Canonical source-visible registers captured for register deltas.
static const VmRegister VM_EXEC_CANONICAL_DELTA_REGISTERS[VM_EXEC_CANONICAL_DELTA_REGISTER_COUNT] = {
    VM_REGISTER_EAX,
    VM_REGISTER_EBX,
    VM_REGISTER_ECX,
    VM_REGISTER_EDX,
    VM_REGISTER_ESI,
    VM_REGISTER_EDI,
    VM_REGISTER_EBP,
    VM_REGISTER_ESP
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

/// Clears one Phase 71D procedure-fallthrough diagnostic to its empty state.
///
/// @param diagnostic Diagnostic structure to clear.
static void vm_exec_clear_procedure_fallthrough_diagnostic(VmProcedureFallthroughDiagnostic *diagnostic) {
    if (diagnostic == NULL) {
        return;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = VM_EXEC_STATUS_OK;
}

/// Returns whether a Phase 71D procedure-fallthrough policy is valid.
///
/// @param policy Policy value to inspect.
/// @return true when @p policy is accepted.
static bool vm_exec_procedure_fallthrough_policy_is_valid(VmProcedureFallthroughPolicy policy) {
    return policy == VM_PROCEDURE_FALLTHROUGH_POLICY_OFF ||
           policy == VM_PROCEDURE_FALLTHROUGH_POLICY_WARN ||
           policy == VM_PROCEDURE_FALLTHROUGH_POLICY_ERROR;
}

/// Returns whether a Phase 71E entry procedure end mode is valid.
///
/// @param mode Mode value to inspect.
/// @return true when @p mode is accepted.
static bool vm_exec_entry_procedure_end_mode_is_valid(VmEntryProcedureEndMode mode) {
    return mode == VM_ENTRY_PROCEDURE_END_MODE_CODE_STREAM ||
           mode == VM_ENTRY_PROCEDURE_END_MODE_STOP_AT_ENTRY_END;
}

/// Returns whether a Phase 72 call-depth limit is accepted.
///
/// @param limit Candidate limit to inspect.
/// @return true when @p limit is within the accepted inclusive range.
static bool vm_exec_call_depth_limit_is_valid(uint32_t limit) {
    return limit >= (uint32_t)VM_MIN_CALL_DEPTH_LIMIT && limit <= (uint32_t)VM_MAX_CALL_DEPTH_LIMIT;
}

/// Records one Phase 71D procedure-fallthrough event.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction that caused the event.
/// @param from_index Instruction index that caused the event.
/// @param to_index Destination instruction index, or program count at code end.
static void vm_exec_record_procedure_fallthrough_diagnostic(Vm *vm, const VmIrInstruction *instruction, size_t from_index, size_t to_index) {
    if (vm == NULL) {
        return;
    }

    vm_exec_clear_procedure_fallthrough_diagnostic(&vm->last_procedure_fallthrough_diagnostic);
    vm->last_procedure_fallthrough_diagnostic.has_diagnostic = true;
    vm->last_procedure_fallthrough_diagnostic.status = VM_EXEC_STATUS_PROCEDURE_FELL_THROUGH;
    vm->last_procedure_fallthrough_diagnostic.from_instruction_index = (uint32_t)from_index;
    vm->last_procedure_fallthrough_diagnostic.to_instruction_index = (uint32_t)to_index;
    if (instruction != NULL) {
        vm->last_procedure_fallthrough_diagnostic.has_from_instruction = true;
        vm->last_procedure_fallthrough_diagnostic.from_instruction = *instruction;
    }
}


/// Clears one Phase 50B flag-use diagnostic to its empty state.
///
/// @param diagnostic Diagnostic structure to clear.
/// @param status Status to store after clearing.
static void vm_exec_clear_flag_use_diagnostic(VmFlagUseDiagnostic *diagnostic, VmExecStatus status) {
    if (diagnostic == NULL) {
        return;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = status;
}

/// Returns whether a policy value is a supported Phase 50B flag-use policy.
///
/// @param policy Policy value to inspect.
/// @return true when @p policy is supported.
static bool vm_exec_flag_use_policy_is_valid(VmUndefinedFlagUsePolicy policy) {
    return policy == VM_UNDEFINED_FLAG_USE_POLICY_OFF ||
           policy == VM_UNDEFINED_FLAG_USE_POLICY_WARN ||
           policy == VM_UNDEFINED_FLAG_USE_POLICY_ERROR;
}

/// Returns whether a flag is already present in a flag-use diagnostic.
///
/// @param diagnostic Diagnostic to inspect.
/// @param flag Flag to find.
/// @return true when @p flag is already recorded.
static bool vm_exec_flag_use_diagnostic_contains_flag(const VmFlagUseDiagnostic *diagnostic, VmFlag flag) {
    size_t index = 0U;

    if (diagnostic == NULL) {
        return false;
    }

    for (index = 0U; index < diagnostic->invalid_flag_count; index += 1U) {
        if (diagnostic->invalid_flags[index] == flag) {
            return true;
        }
    }

    return false;
}

VmExecStatus vm_check_flag_consumption(
    const VmCpu *cpu,
    const VmIrInstruction *consumer_instruction,
    const VmFlag *required_flags,
    size_t required_flag_count,
    VmUndefinedFlagUsePolicy policy,
    VmFlagUseDiagnostic *out_diagnostic
) {
    VmFlagUseDiagnostic local_diagnostic;
    VmFlagUseDiagnostic *diagnostic = out_diagnostic != NULL ? out_diagnostic : &local_diagnostic;
    size_t index = 0U;

    vm_exec_clear_flag_use_diagnostic(diagnostic, VM_EXEC_STATUS_OK);

    if (cpu == NULL || !vm_exec_flag_use_policy_is_valid(policy) ||
        (required_flag_count > 0U && required_flags == NULL)) {
        vm_exec_clear_flag_use_diagnostic(diagnostic, VM_EXEC_STATUS_INVALID_ARGUMENT);
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (policy == VM_UNDEFINED_FLAG_USE_POLICY_OFF || required_flag_count == 0U) {
        return VM_EXEC_STATUS_OK;
    }

    if (consumer_instruction != NULL) {
        diagnostic->has_consumer_instruction = true;
        diagnostic->consumer_instruction = *consumer_instruction;
    }

    for (index = 0U; index < required_flag_count; index += 1U) {
        VmFlag flag = required_flags[index];
        VmFlagValidityMetadata metadata;

        if (!vm_cpu_read_flag_validity(cpu, flag, &metadata)) {
            vm_exec_clear_flag_use_diagnostic(diagnostic, VM_EXEC_STATUS_INVALID_ARGUMENT);
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }

        if (!metadata.is_valid &&
            diagnostic->invalid_flag_count < (size_t)VM_EXEC_MAX_CONSUMED_FLAGS &&
            !vm_exec_flag_use_diagnostic_contains_flag(diagnostic, flag)) {
            diagnostic->invalid_flags[diagnostic->invalid_flag_count] = flag;
            diagnostic->invalid_metadata[diagnostic->invalid_flag_count] = metadata;
            diagnostic->invalid_flag_count += 1U;
        }
    }

    if (diagnostic->invalid_flag_count == 0U) {
        return VM_EXEC_STATUS_OK;
    }

    if (policy == VM_UNDEFINED_FLAG_USE_POLICY_ERROR) {
        diagnostic->status = VM_EXEC_STATUS_UNDEFINED_FLAG_USE;
        return VM_EXEC_STATUS_UNDEFINED_FLAG_USE;
    }

    diagnostic->status = VM_EXEC_STATUS_OK;
    return VM_EXEC_STATUS_OK;
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

/// Records structured Phase 72 call-depth diagnostic metadata.
///
/// @param vm VM whose diagnostic should be updated.
/// @param instruction Rejected CALL instruction.
/// @param current_depth Depth before the rejected CALL.
/// @param attempted_depth Depth that the rejected CALL would have produced.
/// @param limit Configured call-depth limit.
static void vm_exec_set_call_depth_diagnostic(
    Vm *vm,
    const VmIrInstruction *instruction,
    uint32_t current_depth,
    uint32_t attempted_depth,
    uint32_t limit
) {
    vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED, instruction);
    if (vm == NULL) {
        return;
    }

    vm->last_diagnostic.current_call_depth = current_depth;
    vm->last_diagnostic.attempted_call_depth = attempted_depth;
    vm->last_diagnostic.call_depth_limit = limit;
}

/// Clears root, helper-fallthrough, and code-falloff procedure metadata.
///
/// @param vm VM instance to mutate. NULL is ignored.
static void vm_exec_clear_procedure_runtime_metadata(Vm *vm) {
    if (vm == NULL) {
        return;
    }

    memset(vm->procedure_boundaries, 0, sizeof(vm->procedure_boundaries));
    vm->procedure_boundary_count = 0U;
    vm->has_selected_entry_procedure = false;
    vm->selected_entry_procedure_index = 0U;
    vm->active_helper_return_count = 0U;
    vm->current_call_depth = 0U;
    memset(vm->active_uses_frames, 0, sizeof(vm->active_uses_frames));
    vm->active_uses_frame_count = 0U;
    vm->root_code_stream_active = false;
    vm->selected_entry_end_stop_eligible = true;
    memset(vm->active_local_frames, 0, sizeof(vm->active_local_frames));
    vm->active_local_frame_count = 0U;
    memset(vm->local_descriptors, 0, sizeof(vm->local_descriptors));
    vm->local_descriptor_count = 0U;
    vm->next_local_frame_id = 1U;
    vm->selected_entry_local_frame_created = false;
    vm_exec_clear_procedure_fallthrough_diagnostic(&vm->last_procedure_fallthrough_diagnostic);
}

/// Returns whether @p boundary has usable instruction bounds for @p vm.
///
/// @param vm VM instance containing the current program size.
/// @param boundary Procedure boundary to inspect.
/// @return true when the boundary is ordered and inside the loaded program.
static bool vm_exec_procedure_boundary_is_valid(const Vm *vm, const VmExecProcedureBoundary *boundary) {
    if (vm == NULL || boundary == NULL) {
        return false;
    }

    return boundary->start_instruction_index <= boundary->end_instruction_index &&
           boundary->end_instruction_index <= vm->program_count;
}

/// Returns whether an instruction index is inside a procedure boundary.
///
/// @param boundary Procedure boundary to inspect.
/// @param instruction_index Instruction index to classify.
/// @return true when @p instruction_index is inside the half-open boundary.
static bool vm_exec_index_is_inside_procedure_boundary(const VmExecProcedureBoundary *boundary, size_t instruction_index) {
    return boundary != NULL &&
           instruction_index >= boundary->start_instruction_index &&
           instruction_index < boundary->end_instruction_index;
}

/// Returns whether an opcode is a currently implemented conditional jump.
///
/// @param opcode Opcode to inspect.
/// @return true when @p opcode is one of the supported conditional jump opcodes.
static bool vm_exec_opcode_is_conditional_jump(VmIrOpcode opcode);

/// Returns whether a committed step moved by ordinary fallthrough.
///
/// @param instruction Instruction that just executed.
/// @param before_ip Instruction pointer before execution.
/// @param after_ip Instruction pointer after execution.
/// @param conditional_branch_taken Whether @p instruction was a taken conditional branch.
/// @return true when the movement was ordinary fallthrough rather than CALL/JMP/RET/EXIT/taken branch.
static bool vm_exec_step_was_fallthrough(const VmIrInstruction *instruction, size_t before_ip, size_t after_ip, bool conditional_branch_taken);

/// Returns whether selected-entry root metadata is internally inconsistent.
///
/// @param vm VM instance containing selected-entry metadata.
/// @return true when the selected-entry fields cannot identify one valid root boundary.
static bool vm_exec_root_metadata_is_inconsistent(const Vm *vm) {
    const VmExecProcedureBoundary *root = NULL;

    if (vm == NULL || !vm->has_selected_entry_procedure) {
        return false;
    }
    if (vm->selected_entry_procedure_index >= vm->procedure_boundary_count) {
        return true;
    }

    root = &vm->procedure_boundaries[vm->selected_entry_procedure_index];
    return !root->is_selected_entry || !vm_exec_procedure_boundary_is_valid(vm, root);
}

/// Returns the selected-entry procedure boundary when valid.
///
/// @param vm VM instance containing selected-entry metadata.
/// @return Pointer to the selected-entry boundary, or NULL when unavailable.
static const VmExecProcedureBoundary *vm_exec_selected_entry_boundary(const Vm *vm) {
    if (vm == NULL || !vm->has_selected_entry_procedure || vm_exec_root_metadata_is_inconsistent(vm)) {
        return NULL;
    }

    return &vm->procedure_boundaries[vm->selected_entry_procedure_index];
}

/// Returns whether an instruction index is inside the selected-entry procedure.
///
/// @param vm VM instance containing selected-entry metadata.
/// @param instruction_index Instruction index to classify.
/// @return true when @p instruction_index is inside the selected-entry procedure boundary.
static bool vm_exec_instruction_is_in_selected_entry(const Vm *vm, size_t instruction_index) {
    return vm_exec_index_is_inside_procedure_boundary(vm_exec_selected_entry_boundary(vm), instruction_index);
}

/// Returns whether Phase 71E compatibility mode should stop at the selected entry boundary.
///
/// This predicate is true for an empty selected entry before the first fetch and
/// after an ordinary sequential step reaches the selected entry ENDP boundary.
/// Explicit control-transfer paths clear the eligibility flag before this helper
/// is allowed to stop execution.
///
/// @param vm VM instance to inspect.
/// @return true when execution should halt successfully at the selected entry boundary.
static bool vm_exec_should_stop_at_selected_entry_end(const Vm *vm) {
    const VmExecProcedureBoundary *root = vm_exec_selected_entry_boundary(vm);

    return vm != NULL &&
           root != NULL &&
           vm->entry_procedure_end_mode == VM_ENTRY_PROCEDURE_END_MODE_STOP_AT_ENTRY_END &&
           vm->selected_entry_end_stop_eligible &&
           vm->active_helper_return_count == 0U &&
           vm->instruction_pointer == root->end_instruction_index;
}

/// Updates Phase 71E selected-entry path state after one committed selected-entry step.
///
/// Ordinary sequential movement preserves selected-entry ENDP auto-stop
/// eligibility and root-code-stream state. Explicit CALL/RET paths and explicit
/// transfers that leave the selected entry procedure clear that state. Explicit
/// jumps or taken conditional branches that remain inside the selected entry
/// procedure do not by themselves prevent later ordinary boundary completion.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction that just executed.
/// @param before_ip Instruction pointer before execution.
/// @param conditional_branch_taken Whether @p instruction was a taken conditional branch.
static void vm_exec_update_entry_end_stop_eligibility_after_step(Vm *vm, const VmIrInstruction *instruction, size_t before_ip, bool conditional_branch_taken) {
    if (vm == NULL || instruction == NULL) {
        return;
    }

    if (!vm_exec_instruction_is_in_selected_entry(vm, before_ip)) {
        return;
    }

    if (vm_exec_step_was_fallthrough(instruction, before_ip, vm->instruction_pointer, conditional_branch_taken)) {
        return;
    }

    if (instruction->opcode != VM_IR_OPCODE_CALL && instruction->opcode != VM_IR_OPCODE_RET &&
        vm_exec_instruction_is_in_selected_entry(vm, vm->instruction_pointer)) {
        return;
    }

    vm->selected_entry_end_stop_eligible = false;
    vm->root_code_stream_active = false;
}

/// Refreshes whether execution is in the no-helper selected-entry root code stream.
///
/// @param vm VM instance to mutate. NULL is ignored.
static void vm_exec_refresh_root_code_stream_state(Vm *vm) {
    if (vm == NULL) {
        return;
    }

    if (vm->active_helper_return_count == 0U &&
        vm_exec_instruction_is_in_selected_entry(vm, vm->instruction_pointer)) {
        vm->root_code_stream_active = true;
    }
}

/// Returns whether the current instruction is an eligible Phase 71 root-code-stream RET.
///
/// @param vm VM instance containing selected-entry and helper-return metadata.
/// @return true when no helper return is pending and RET is in the root code stream.
static bool vm_exec_current_instruction_is_root_ret(const Vm *vm) {
    if (vm == NULL || vm_exec_root_metadata_is_inconsistent(vm) || vm->active_helper_return_count != 0U) {
        return false;
    }

    return vm_exec_instruction_is_in_selected_entry(vm, vm->instruction_pointer) || vm->root_code_stream_active;
}

/// Finds the procedure boundary containing an instruction index.
///
/// @param vm VM instance containing configured procedure boundaries.
/// @param instruction_index Instruction index to classify.
/// @return Pointer to the containing boundary, or NULL when none matches.
static const VmExecProcedureBoundary *vm_exec_find_procedure_boundary(const Vm *vm, size_t instruction_index) {
    size_t index = 0U;

    if (vm == NULL) {
        return NULL;
    }

    for (index = 0U; index < vm->procedure_boundary_count; index += 1U) {
        const VmExecProcedureBoundary *boundary = &vm->procedure_boundaries[index];
        if (vm_exec_procedure_boundary_is_valid(vm, boundary) &&
            vm_exec_index_is_inside_procedure_boundary(boundary, instruction_index)) {
            return boundary;
        }
    }

    return NULL;
}

/// Finds the procedure boundary that starts at an instruction index.
///
/// @param vm VM instance containing configured procedure boundaries.
/// @param instruction_index Candidate procedure-entry instruction index.
/// @return Pointer to the matching boundary, or NULL when none starts there.
static const VmExecProcedureBoundary *vm_exec_find_procedure_boundary_starting_at(const Vm *vm, size_t instruction_index) {
    size_t index = 0U;

    if (vm == NULL) {
        return NULL;
    }

    for (index = 0U; index < vm->procedure_boundary_count; index += 1U) {
        const VmExecProcedureBoundary *boundary = &vm->procedure_boundaries[index];
        if (vm_exec_procedure_boundary_is_valid(vm, boundary) &&
            boundary->start_instruction_index == instruction_index) {
            return boundary;
        }
    }

    return NULL;
}

/// Returns whether the current instruction has an active CALL-created USES frame.
///
/// @param vm VM instance containing active Phase 77 USES frame metadata.
/// @param procedure_start_index Procedure body start instruction index to match.
/// @return true when the most recent active USES frame belongs to the procedure.
static bool vm_exec_active_uses_frame_matches_start(const Vm *vm, size_t procedure_start_index) {
    const VmExecUsesFrame *frame = NULL;

    if (vm == NULL || vm->active_uses_frame_count == 0U) {
        return false;
    }

    frame = &vm->active_uses_frames[vm->active_uses_frame_count - 1U];
    return frame->procedure_start_instruction_index == procedure_start_index;
}

/// Returns whether execution would enter a PROC USES body without a CALL-created frame.
///
/// Phase 77 supports automatic USES save/restore only for direct CALL entry.
/// This guard prevents selected-entry startup, direct branch targets, and
/// ordinary fallthrough from silently executing a USES procedure body without
/// a matching active save/restore frame.
///
/// @param vm VM instance containing configured procedure boundaries.
/// @return true when execution is positioned at an unsupported USES procedure entry.
static bool vm_exec_current_instruction_starts_uses_procedure_without_frame(const Vm *vm) {
    const VmExecProcedureBoundary *boundary = NULL;

    if (vm == NULL || vm->instruction_pointer >= vm->program_count) {
        return false;
    }

    boundary = vm_exec_find_procedure_boundary_starting_at(vm, vm->instruction_pointer);
    return boundary != NULL && boundary->uses_register_count > 0U &&
           !vm_exec_active_uses_frame_matches_start(vm, boundary->start_instruction_index);
}

/// Returns whether a committed step moved by ordinary fallthrough.
///
/// @param instruction Instruction that just executed.
/// @param before_ip Instruction pointer before execution.
/// @param after_ip Instruction pointer after execution.
/// @param conditional_branch_taken Whether @p instruction was a taken conditional branch.
/// @return true when the movement was ordinary fallthrough rather than CALL/JMP/RET/EXIT/taken branch.
static bool vm_exec_step_was_fallthrough(const VmIrInstruction *instruction, size_t before_ip, size_t after_ip, bool conditional_branch_taken) {
    if (instruction == NULL || after_ip != before_ip + 1U) {
        return false;
    }
    if (instruction->opcode == VM_IR_OPCODE_JMP || instruction->opcode == VM_IR_OPCODE_CALL || instruction->opcode == VM_IR_OPCODE_RET || instruction->opcode == VM_IR_OPCODE_EXIT) {
        return false;
    }
    if (conditional_branch_taken && vm_exec_opcode_is_conditional_jump(instruction->opcode)) {
        return false;
    }

    return true;
}

/// Applies procedure-boundary fallthrough diagnostics after one committed step.
///
/// Phase 71D applies only to ordinary sequential procedure-boundary crossings.
/// It maps the earlier called-helper fallthrough-at-code-end public path into
/// procedure-fell-through so active output has one procedure-fallthrough code.
///
/// @param vm VM instance to inspect and possibly halt.
/// @param instruction Instruction that just executed.
/// @param before_ip Instruction pointer before execution.
/// @param conditional_branch_taken Whether @p instruction was a taken conditional branch.
/// @return OK for normal continuation, or a terminal diagnostic status.
static VmExecStatus vm_exec_apply_procedure_fallthrough(Vm *vm, const VmIrInstruction *instruction, size_t before_ip, bool conditional_branch_taken) {
    const VmExecProcedureBoundary *from_boundary = NULL;
    const VmExecProcedureBoundary *to_boundary = NULL;
    bool crossed_into_other_procedure = false;
    bool helper_fell_out_of_code = false;

    if (vm == NULL || instruction == NULL || !vm_exec_step_was_fallthrough(instruction, before_ip, vm->instruction_pointer, conditional_branch_taken)) {
        return VM_EXEC_STATUS_OK;
    }

    from_boundary = vm_exec_find_procedure_boundary(vm, before_ip);
    if (from_boundary == NULL || vm->instruction_pointer != from_boundary->end_instruction_index) {
        return VM_EXEC_STATUS_OK;
    }

    if (vm->instruction_pointer < vm->program_count) {
        to_boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
        crossed_into_other_procedure = to_boundary != NULL && to_boundary != from_boundary;
    } else {
        helper_fell_out_of_code = !from_boundary->is_selected_entry && vm->active_helper_return_count > 0U;
    }

    if (!crossed_into_other_procedure && !helper_fell_out_of_code) {
        return VM_EXEC_STATUS_OK;
    }

    if (vm->procedure_fallthrough_policy == VM_PROCEDURE_FALLTHROUGH_POLICY_OFF) {
        return VM_EXEC_STATUS_OK;
    }

    vm_exec_record_procedure_fallthrough_diagnostic(vm, instruction, before_ip, vm->instruction_pointer);
    if (vm->procedure_fallthrough_policy == VM_PROCEDURE_FALLTHROUGH_POLICY_ERROR) {
        vm->halted = true;
        return VM_EXEC_STATUS_PROCEDURE_FELL_THROUGH;
    }

    return VM_EXEC_STATUS_OK;
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

    address = operand->address + base_value + operand->immediate;

    *out_address = address;
    return true;
}

/// Resolves an address expression without dereferencing memory.
///
/// LEA uses this helper so effective-address arithmetic can wrap modulo 2^32
/// without invoking checked memory read/write helpers or producing memory
/// diagnostics. VM_IR_OPERAND_MEMORY_REGISTER may omit a runtime register by
/// using VM_REGISTER_COUNT; this represents a symbol-plus-displacement address.
///
/// @param vm VM whose CPU state supplies optional runtime register values.
/// @param operand Address-expression operand to resolve.
/// @param out_address Receives the computed 32-bit effective address.
/// @return true when @p operand is a supported address expression.
static bool vm_exec_resolve_effective_address(const Vm *vm, const VmIrOperand *operand, uint32_t *out_address) {
    uint32_t base_value = 0U;
    uint32_t address = 0U;

    if (vm == NULL || operand == NULL || out_address == NULL) {
        return false;
    }

    if (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS) {
        *out_address = operand->address;
        return true;
    }

    if (operand->kind != VM_IR_OPERAND_MEMORY_REGISTER) {
        return false;
    }

    if (operand->reg != VM_REGISTER_COUNT && !vm_cpu_read_register(&vm->cpu, operand->reg, &base_value)) {
        return false;
    }

    address = operand->address + base_value + operand->immediate;

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

/// Returns whether an operand is a Phase 80 LOCAL memory operand.
///
/// @param operand Operand to inspect.
/// @return true when @p operand carries LOCAL relocation metadata.
static bool vm_exec_operand_is_local_memory(const VmIrOperand *operand);

/// Resolves a Phase 80 LOCAL operand to its current runtime stack address.
///
/// @param vm VM whose active frame stack should be inspected.
/// @param instruction Instruction associated with diagnostics.
/// @param operand LOCAL memory operand to resolve.
/// @param out_address Receives the runtime address on success.
/// @return OK when an active owning frame exists, otherwise a targeted LOCAL operand status.
static VmExecStatus vm_exec_resolve_local_memory_address(
    Vm *vm,
    const VmIrInstruction *instruction,
    const VmIrOperand *operand,
    uint32_t *out_address
);

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
            if (vm_exec_operand_is_local_memory(operand)) {
                VmExecStatus local_status = vm_exec_resolve_local_memory_address(vm, instruction, operand, &effective_address);
                if (local_status != VM_EXEC_STATUS_OK) {
                    return local_status;
                }
            } else if (!vm_exec_resolve_memory_address(vm, operand, &effective_address)) {
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
            if (vm_exec_operand_is_local_memory(operand)) {
                VmExecStatus local_status = vm_exec_resolve_local_memory_address(vm, instruction, operand, &effective_address);
                if (local_status != VM_EXEC_STATUS_OK) {
                    return local_status;
                }
            } else if (!vm_exec_resolve_memory_address(vm, operand, &effective_address)) {
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

/// Reads a checked 32-bit value from an absolute simulated memory address.
///
/// @param vm VM instance whose memory should be read.
/// @param instruction Instruction associated with diagnostics.
/// @param address Effective simulated address to read.
/// @param out_value Receives the DWORD value on success.
/// @return Executor status for the checked read.
static VmExecStatus vm_exec_read_u32_at_address(
    Vm *vm,
    const VmIrInstruction *instruction,
    uint32_t address,
    uint32_t *out_value
) {
    VmMemoryDiagnostic memory_diagnostic;
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL || out_value == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memset(&memory_diagnostic, 0, sizeof(memory_diagnostic));
    memory_status = vm_memory_read_u32(&vm->memory, address, out_value, &memory_diagnostic);
    vm_exec_record_memory_access(vm, VM_EXEC_MEMORY_ACCESS_READ, address, 32U, memory_status);
    if (!vm_memory_status_succeeded(memory_status)) {
        vm_exec_set_memory_diagnostic(vm, instruction, memory_status, &memory_diagnostic);
        return VM_EXEC_STATUS_MEMORY_ERROR;
    }

    return VM_EXEC_STATUS_OK;
}

/// Writes a checked 32-bit value to an absolute simulated memory address.
///
/// @param vm VM instance whose memory should be written.
/// @param instruction Instruction associated with diagnostics.
/// @param address Effective simulated address to write.
/// @param value DWORD value to write.
/// @return Executor status for the checked write.
static VmExecStatus vm_exec_write_u32_at_address(
    Vm *vm,
    const VmIrInstruction *instruction,
    uint32_t address,
    uint32_t value
) {
    VmMemoryDiagnostic memory_diagnostic;
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memset(&memory_diagnostic, 0, sizeof(memory_diagnostic));
    memory_status = vm_memory_write_u32(&vm->memory, address, value, &memory_diagnostic);
    vm_exec_record_memory_access(vm, VM_EXEC_MEMORY_ACCESS_WRITE, address, 32U, memory_status);
    if (!vm_memory_status_succeeded(memory_status)) {
        vm_exec_set_memory_diagnostic(vm, instruction, memory_status, &memory_diagnostic);
        return VM_EXEC_STATUS_MEMORY_ERROR;
    }

    return VM_EXEC_STATUS_OK;
}

/// Copies a short NUL-terminated identifier into a fixed executor buffer.
///
/// @param destination Destination buffer.
/// @param destination_size Destination buffer size in bytes.
/// @param source Optional source string.
static void vm_exec_copy_short_string(char *destination, size_t destination_size, const char *source) {
    if (destination == NULL || destination_size == 0U) {
        return;
    }
    destination[0] = '\0';
    if (source != NULL) {
        snprintf(destination, destination_size, "%s", source);
    }
}

/// Returns whether a procedure boundary declares Phase 78 LOCAL metadata.
///
/// @param boundary Procedure boundary to inspect.
/// @return true when Phase 79 must create an automatic LOCAL frame for it.
static bool vm_exec_procedure_boundary_has_locals(const VmExecProcedureBoundary *boundary) {
    return boundary != NULL && boundary->local_count > 0U && boundary->local_frame_size_bytes > 0U;
}

/// Records a Phase 79 frame diagnostic with procedure, stage, byte, and address context.
///
/// @param vm VM whose diagnostic should be updated.
/// @param status Executor status to record.
/// @param instruction Optional instruction associated with the failure.
/// @param boundary Optional procedure boundary for procedure context.
/// @param stage Optional operation stage string.
/// @param byte_count Optional relevant byte count; zero means absent.
/// @param address Optional relevant address.
/// @param has_address Whether @p address is meaningful.
static void vm_exec_set_local_frame_diagnostic(
    Vm *vm,
    VmExecStatus status,
    const VmIrInstruction *instruction,
    const VmExecProcedureBoundary *boundary,
    const char *stage,
    uint32_t byte_count,
    uint32_t address,
    bool has_address
) {
    vm_exec_set_diagnostic(vm, status, instruction);
    if (vm == NULL) {
        return;
    }
    if (boundary != NULL && boundary->procedure_name[0] != '\0') {
        vm->last_diagnostic.has_procedure_name = true;
        vm_exec_copy_short_string(vm->last_diagnostic.procedure_name, sizeof(vm->last_diagnostic.procedure_name), boundary->procedure_name);
    }
    if (stage != NULL && stage[0] != '\0') {
        vm->last_diagnostic.has_operation_stage = true;
        vm_exec_copy_short_string(vm->last_diagnostic.operation_stage, sizeof(vm->last_diagnostic.operation_stage), stage);
    }
    if (byte_count > 0U) {
        vm->last_diagnostic.has_relevant_byte_count = true;
        vm->last_diagnostic.relevant_byte_count = byte_count;
    }
    if (has_address) {
        vm->last_diagnostic.has_relevant_address = true;
        vm->last_diagnostic.relevant_address = address;
    }
}

/// Returns the LOCAL declaration metadata associated with one lowered LOCAL operand.
///
/// @param boundary Current procedure boundary that owns the operand.
/// @param operand LOCAL operand to match by EBP-relative object base.
/// @return Matching LOCAL declaration, or NULL when metadata is unavailable.
static const VmExecProcedureLocal *vm_exec_find_boundary_local_for_operand(
    const VmExecProcedureBoundary *boundary,
    const VmIrOperand *operand
) {
    size_t index = 0U;
    int32_t local_ebp_offset = 0;

    if (boundary == NULL || operand == NULL ||
        operand->kind != VM_IR_OPERAND_MEMORY_REGISTER ||
        operand->relocation != VM_IR_RELOCATION_LOCAL) {
        return NULL;
    }
    local_ebp_offset = (int32_t)operand->address;
    for (index = 0U; index < boundary->local_count; index += 1U) {
        if (boundary->locals[index].ebp_offset == local_ebp_offset) {
            return &boundary->locals[index];
        }
    }
    return NULL;
}

/// Records a Phase 80 LOCAL operand diagnostic with procedure and local context.
///
/// @param vm VM whose diagnostic should be updated.
/// @param status Executor status to record.
/// @param instruction Instruction associated with the failure.
/// @param operand LOCAL operand metadata associated with the failure.
/// @param stage Operation stage to record.
static void vm_exec_set_local_operand_diagnostic(
    Vm *vm,
    VmExecStatus status,
    const VmIrInstruction *instruction,
    const VmIrOperand *operand,
    const char *stage
) {
    const VmExecProcedureBoundary *boundary = NULL;
    const VmExecProcedureLocal *local = NULL;

    vm_exec_set_diagnostic(vm, status, instruction);
    if (vm == NULL || operand == NULL) {
        return;
    }
    boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
    local = vm_exec_find_boundary_local_for_operand(boundary, operand);
    if (boundary != NULL && boundary->procedure_name[0] != '\0') {
        vm->last_diagnostic.has_procedure_name = true;
        vm_exec_copy_short_string(vm->last_diagnostic.procedure_name, sizeof(vm->last_diagnostic.procedure_name), boundary->procedure_name);
    }
    if (local != NULL && local->local_name[0] != '\0') {
        vm->last_diagnostic.has_local_name = true;
        vm_exec_copy_short_string(vm->last_diagnostic.local_name, sizeof(vm->last_diagnostic.local_name), local->local_name);
    }
    if (stage != NULL && stage[0] != '\0') {
        vm->last_diagnostic.has_operation_stage = true;
        vm_exec_copy_short_string(vm->last_diagnostic.operation_stage, sizeof(vm->last_diagnostic.operation_stage), stage);
    }
}

/// Returns whether an operand is a Phase 80 LOCAL memory operand.
///
/// @param operand Operand to inspect.
/// @return true when @p operand carries LOCAL relocation metadata.
static bool vm_exec_operand_is_local_memory(const VmIrOperand *operand) {
    return operand != NULL && operand->kind == VM_IR_OPERAND_MEMORY_REGISTER && operand->relocation == VM_IR_RELOCATION_LOCAL;
}

/// Finds the top active automatic LOCAL frame required by one LOCAL operand.
///
/// @param vm VM containing active frame metadata.
/// @param operand LOCAL operand whose current procedure frame should be matched.
/// @return Matching active top frame, or NULL when the operand has no active frame.
static const VmExecLocalFrame *vm_exec_active_local_frame_for_operand(const Vm *vm, const VmIrOperand *operand) {
    const VmExecLocalFrame *frame = NULL;
    const VmExecProcedureBoundary *boundary = NULL;

    if (vm == NULL || operand == NULL || vm->active_local_frame_count == 0U || !vm_exec_operand_is_local_memory(operand)) {
        return NULL;
    }
    boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
    if (boundary == NULL || vm_exec_find_boundary_local_for_operand(boundary, operand) == NULL) {
        return NULL;
    }
    frame = &vm->active_local_frames[vm->active_local_frame_count - 1U];
    if (frame->state != VM_EXEC_LOCAL_FRAME_STATE_ACTIVE ||
        frame->procedure_start_instruction_index != boundary->start_instruction_index) {
        return NULL;
    }
    return frame;
}

/// Resolves a Phase 80 LOCAL operand to its current runtime stack address.
///
/// @param vm VM whose active frame stack should be inspected.
/// @param instruction Instruction associated with diagnostics.
/// @param operand LOCAL memory operand to resolve.
/// @param out_address Receives the runtime address on success.
/// @return OK when an active owning frame exists, otherwise a targeted LOCAL operand status.
static VmExecStatus vm_exec_resolve_local_memory_address(
    Vm *vm,
    const VmIrInstruction *instruction,
    const VmIrOperand *operand,
    uint32_t *out_address
) {
    const VmExecLocalFrame *frame = NULL;

    if (out_address != NULL) {
        *out_address = 0U;
    }
    if (vm == NULL || operand == NULL || out_address == NULL || !vm_exec_operand_is_local_memory(operand)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    frame = vm_exec_active_local_frame_for_operand(vm, operand);
    if (frame == NULL) {
        vm_exec_set_local_operand_diagnostic(vm, VM_EXEC_STATUS_LOCAL_OPERAND_NO_ACTIVE_FRAME, instruction, operand, "local-operand-address-resolution");
        return VM_EXEC_STATUS_LOCAL_OPERAND_NO_ACTIVE_FRAME;
    }
    *out_address = frame->frame_base_address + operand->immediate;
    return VM_EXEC_STATUS_OK;
}

/// Returns whether a byte stack range can be reserved without crossing the active stack region.
///
/// @param vm VM instance whose stack region should be inspected.
/// @param esp Stack pointer before reservation.
/// @param byte_count Number of bytes to reserve.
/// @return true when the complete byte range is writable stack storage.
static bool vm_exec_stack_can_reserve_bytes(const Vm *vm, uint32_t esp, uint32_t byte_count) {
    const VmMemoryRegion *stack_region = NULL;
    uint64_t stack_limit = 0U;
    uint64_t low_address = 0U;

    if (vm == NULL || byte_count == 0U) {
        return false;
    }
    stack_region = vm_memory_get_region(&vm->memory, VM_MEMORY_REGION_STACK);
    if (stack_region == NULL || stack_region->size == 0U ||
        !vm_memory_region_has_permission(stack_region, VM_MEMORY_PERMISSION_WRITE)) {
        return false;
    }
    stack_limit = (uint64_t)stack_region->base + (uint64_t)stack_region->size;
    if (stack_limit > (uint64_t)UINT32_MAX || (uint64_t)esp > stack_limit || (uint64_t)byte_count > (uint64_t)esp) {
        return false;
    }
    low_address = (uint64_t)esp - (uint64_t)byte_count;
    return low_address >= (uint64_t)stack_region->base;
}

/// Computes selected-entry automatic LOCAL frame footprint in bytes.
///
/// @param boundary Selected-entry procedure boundary.
/// @return Saved EBP plus rounded LOCAL storage byte count.
static uint32_t vm_exec_selected_entry_local_footprint(const VmExecProcedureBoundary *boundary) {
    if (!vm_exec_procedure_boundary_has_locals(boundary) || boundary->local_frame_size_bytes > UINT32_MAX - 4U) {
        return 0U;
    }
    return boundary->local_frame_size_bytes + 4U;
}

/// Computes direct CALL automatic stack footprint in bytes.
///
/// @param boundary Called procedure boundary.
/// @return Return token, USES slots, saved EBP, and rounded LOCAL byte count.
static uint32_t vm_exec_direct_call_combined_footprint(const VmExecProcedureBoundary *boundary) {
    uint32_t footprint = 4U;
    if (boundary == NULL) {
        return footprint;
    }
    if (boundary->uses_register_count > (size_t)VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY ||
        boundary->uses_register_count > ((size_t)UINT32_MAX / 4U)) {
        return 0U;
    }
    footprint += (uint32_t)(boundary->uses_register_count * 4U);
    if (vm_exec_procedure_boundary_has_locals(boundary)) {
        if (footprint > UINT32_MAX - 4U || footprint + 4U > UINT32_MAX - boundary->local_frame_size_bytes) {
            return 0U;
        }
        footprint += 4U + boundary->local_frame_size_bytes;
    }
    return footprint;
}

/// Returns the top retained LOCAL frame if it belongs to @p boundary.
///
/// @param vm VM instance containing active frame metadata.
/// @param boundary Procedure boundary to match.
/// @return Mutable top frame, or NULL when no matching frame exists.
static VmExecLocalFrame *vm_exec_top_local_frame_for_boundary(Vm *vm, const VmExecProcedureBoundary *boundary) {
    VmExecLocalFrame *frame = NULL;
    if (vm == NULL || boundary == NULL || vm->active_local_frame_count == 0U) {
        return NULL;
    }
    frame = &vm->active_local_frames[vm->active_local_frame_count - 1U];
    if (frame->procedure_start_instruction_index != boundary->start_instruction_index) {
        return NULL;
    }
    return frame;
}

/// Returns whether one active LOCAL frame matches the current procedure boundary.
///
/// @param frame Candidate active frame.
/// @param boundary Candidate procedure boundary.
/// @return true when the frame actively owns the boundary.
static bool vm_exec_local_frame_matches_boundary(const VmExecLocalFrame *frame, const VmExecProcedureBoundary *boundary) {
    return frame != NULL && boundary != NULL && frame->state == VM_EXEC_LOCAL_FRAME_STATE_ACTIVE &&
           frame->procedure_start_instruction_index == boundary->start_instruction_index;
}

/// Marks descriptors associated with one frame identity inactive.
///
/// @param vm VM instance containing descriptor metadata.
/// @param frame_id Frame identity to mark inactive.
static void vm_exec_deactivate_local_descriptors(Vm *vm, uint32_t frame_id) {
    size_t index = 0U;
    if (vm == NULL || frame_id == 0U) {
        return;
    }
    for (index = 0U; index < vm->local_descriptor_count; index += 1U) {
        if (vm->local_descriptors[index].frame_id == frame_id) {
            vm->local_descriptors[index].state = VM_EXEC_LOCAL_FRAME_STATE_INACTIVE;
        }
    }
}

/// Returns whether descriptor slots are available for one LOCAL frame.
///
/// @param vm VM instance containing descriptor metadata.
/// @param boundary Procedure metadata whose locals would receive descriptors.
/// @return true when setup can create every required descriptor before mutation.
static bool vm_exec_can_create_local_descriptors(const Vm *vm, const VmExecProcedureBoundary *boundary) {
    if (vm == NULL || boundary == NULL || boundary->local_count > (size_t)VM_EXEC_PROCEDURE_LOCAL_CAPACITY) {
        return false;
    }
    return boundary->local_count <= (size_t)VM_EXEC_LOCAL_DESCRIPTOR_CAPACITY - vm->local_descriptor_count;
}

/// Removes descriptors associated with one frame identity after the frame is no longer observable.
///
/// @param vm VM instance containing descriptor metadata.
/// @param frame_id Frame identity whose descriptors should be discarded.
static void vm_exec_remove_local_descriptors(Vm *vm, uint32_t frame_id) {
    size_t read_index = 0U;
    size_t write_index = 0U;
    if (vm == NULL || frame_id == 0U) {
        return;
    }
    for (read_index = 0U; read_index < vm->local_descriptor_count; read_index += 1U) {
        if (vm->local_descriptors[read_index].frame_id == frame_id) {
            continue;
        }
        if (write_index != read_index) {
            vm->local_descriptors[write_index] = vm->local_descriptors[read_index];
        }
        write_index += 1U;
    }
    {
        size_t new_count = write_index;
        while (write_index < vm->local_descriptor_count) {
            memset(&vm->local_descriptors[write_index], 0, sizeof(vm->local_descriptors[write_index]));
            write_index += 1U;
        }
        vm->local_descriptor_count = new_count;
    }
}

/// Pops the retained top LOCAL frame after RET/exit has acknowledged its release.
///
/// @param vm VM instance to mutate.
static void vm_exec_pop_released_local_frame(Vm *vm) {
    uint32_t frame_id = 0U;
    if (vm == NULL || vm->active_local_frame_count == 0U) {
        return;
    }
    frame_id = vm->active_local_frames[vm->active_local_frame_count - 1U].frame_id;
    vm_exec_remove_local_descriptors(vm, frame_id);
    memset(&vm->active_local_frames[vm->active_local_frame_count - 1U], 0, sizeof(vm->active_local_frames[vm->active_local_frame_count - 1U]));
    vm->active_local_frame_count -= 1U;
}

/// Creates runtime descriptors for one newly active automatic LOCAL frame.
///
/// @param vm VM instance to mutate.
/// @param boundary Procedure metadata that owns the locals.
/// @param frame Active frame receiving descriptors.
/// @return OK when all descriptors were created.
static VmExecStatus vm_exec_create_local_descriptors(Vm *vm, const VmExecProcedureBoundary *boundary, const VmExecLocalFrame *frame) {
    size_t index = 0U;
    if (vm == NULL || boundary == NULL || frame == NULL || !vm_exec_can_create_local_descriptors(vm, boundary)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < boundary->local_count; index += 1U) {
        const VmExecProcedureLocal *local = &boundary->locals[index];
        VmExecLocalDescriptor *descriptor = &vm->local_descriptors[vm->local_descriptor_count];
        int64_t runtime_address = (int64_t)frame->frame_base_address + (int64_t)local->ebp_offset;
        if (runtime_address < 0 || runtime_address > (int64_t)UINT32_MAX) {
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
        memset(descriptor, 0, sizeof(*descriptor));
        vm_exec_copy_short_string(descriptor->procedure_name, sizeof(descriptor->procedure_name), boundary->procedure_name);
        vm_exec_copy_short_string(descriptor->local_name, sizeof(descriptor->local_name), local->local_name);
        descriptor->source_line = local->source_line;
        descriptor->source_column = local->source_column;
        descriptor->source_byte_offset = local->source_byte_offset;
        descriptor->source_span_length = local->source_span_length;
        descriptor->frame_id = frame->frame_id;
        descriptor->runtime_base_address = (uint32_t)runtime_address;
        descriptor->byte_size = local->total_size_bytes;
        descriptor->element_size_bytes = local->element_size_bytes;
        descriptor->element_count = local->element_count;
        descriptor->ebp_offset = local->ebp_offset;
        descriptor->state = VM_EXEC_LOCAL_FRAME_STATE_ACTIVE;
        vm->local_descriptor_count += 1U;
    }
    return VM_EXEC_STATUS_OK;
}

/// Initializes visible LOCAL storage bytes to deterministic zero values.
///
/// @param vm VM instance whose memory should be written.
/// @param instruction Instruction associated with diagnostics.
/// @param boundary Procedure metadata that owns locals.
/// @param frame Active frame identifying the reserved byte range.
/// @return OK when all visible bytes were written.
static VmExecStatus vm_exec_initialize_local_visible_bytes(Vm *vm, const VmIrInstruction *instruction, const VmExecProcedureBoundary *boundary, const VmExecLocalFrame *frame) {
    uint32_t offset = 0U;
    if (vm == NULL || frame == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    for (offset = 0U; offset < frame->local_frame_size_bytes; offset += 1U) {
        uint32_t address = frame->frame_base_address - 1U - offset;
        VmMemoryDiagnostic memory_diagnostic;
        VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;
        memset(&memory_diagnostic, 0, sizeof(memory_diagnostic));
        memory_status = vm_memory_write_u8(&vm->memory, address, 0U, &memory_diagnostic);
        vm_exec_record_memory_access(vm, VM_EXEC_MEMORY_ACCESS_WRITE, address, 8U, memory_status);
        if (!vm_memory_status_succeeded(memory_status)) {
            vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction, boundary, "local-frame-byte-initialization", frame->local_frame_size_bytes, address, true);
            return VM_EXEC_STATUS_STACK_OVERFLOW;
        }
    }
    return VM_EXEC_STATUS_OK;
}

/// Sets up one automatic Phase 79 LOCAL frame after complete preflight.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction associated with diagnostics.
/// @param boundary Procedure whose locals should be allocated.
/// @param stage Diagnostic operation stage.
/// @return OK when saved EBP, new EBP/ESP, zeroed locals, and descriptors committed.
static VmExecStatus vm_exec_setup_local_frame(Vm *vm, const VmIrInstruction *instruction, const VmExecProcedureBoundary *boundary, const char *stage) {
    uint32_t esp = 0U;
    uint32_t ebp = 0U;
    uint32_t saved_ebp_address = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    VmExecLocalFrame frame;

    if (vm == NULL || instruction == NULL || !vm_exec_procedure_boundary_has_locals(boundary)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (vm->active_local_frame_count >= (size_t)VM_MAX_CALL_DEPTH_LIMIT || !vm_exec_can_create_local_descriptors(vm, boundary)) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_INVALID_FRAME_STATE, instruction, boundary, "local-descriptor-capacity", boundary->local_frame_size_bytes, 0U, false);
        return VM_EXEC_STATUS_INVALID_FRAME_STATE;
    }
    memset(&frame, 0, sizeof(frame));
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_ESP, &esp) || !vm_cpu_read_register(&vm->cpu, VM_REGISTER_EBP, &ebp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_exec_stack_can_reserve_bytes(vm, esp, boundary->local_frame_size_bytes + 4U)) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction, boundary, stage, boundary->local_frame_size_bytes + 4U, esp, true);
        return VM_EXEC_STATUS_STACK_OVERFLOW;
    }
    saved_ebp_address = esp - 4U;
    status = vm_exec_write_u32_at_address(vm, instruction, saved_ebp_address, ebp);
    if (status != VM_EXEC_STATUS_OK) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction, boundary, stage, boundary->local_frame_size_bytes + 4U, saved_ebp_address, true);
        return VM_EXEC_STATUS_STACK_OVERFLOW;
    }
    frame.procedure_start_instruction_index = boundary->start_instruction_index;
    frame.frame_id = vm->next_local_frame_id == 0U ? 1U : vm->next_local_frame_id;
    frame.saved_ebp_address = saved_ebp_address;
    frame.saved_ebp_value = ebp;
    frame.frame_base_address = saved_ebp_address;
    frame.frame_stack_pointer = saved_ebp_address - boundary->local_frame_size_bytes;
    frame.local_frame_size_bytes = boundary->local_frame_size_bytes;
    frame.state = VM_EXEC_LOCAL_FRAME_STATE_ACTIVE;

    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_EBP, frame.frame_base_address) ||
        !vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, frame.frame_stack_pointer)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    status = vm_exec_initialize_local_visible_bytes(vm, instruction, boundary, &frame);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    status = vm_exec_create_local_descriptors(vm, boundary, &frame);
    if (status != VM_EXEC_STATUS_OK) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_INVALID_FRAME_STATE, instruction, boundary, "local-descriptor-create", boundary->local_frame_size_bytes, frame.frame_base_address, true);
        return VM_EXEC_STATUS_INVALID_FRAME_STATE;
    }
    vm->active_local_frames[vm->active_local_frame_count] = frame;
    vm->active_local_frame_count += 1U;
    vm->next_local_frame_id = frame.frame_id + 1U;
    vm_memory_clear_changes(&vm->memory);
    return VM_EXEC_STATUS_OK;
}

/// Releases one active automatic LOCAL frame by restoring saved EBP and ESP.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction associated with diagnostics.
/// @param boundary Procedure whose active frame should be released.
/// @param stage Diagnostic operation stage.
/// @param pop_after_release Whether the retained frame slot should be popped immediately.
/// @return OK when the frame was released or acknowledged.
static VmExecStatus vm_exec_release_local_frame(Vm *vm, const VmIrInstruction *instruction, const VmExecProcedureBoundary *boundary, const char *stage, bool pop_after_release) {
    VmExecLocalFrame *frame = NULL;
    uint32_t ebp = 0U;
    uint32_t saved_ebp = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    if (vm == NULL || instruction == NULL || boundary == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    frame = vm_exec_top_local_frame_for_boundary(vm, boundary);
    if (frame == NULL) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_INVALID_FRAME_STATE, instruction, boundary, stage, 0U, 0U, false);
        return VM_EXEC_STATUS_INVALID_FRAME_STATE;
    }
    if (frame->state == VM_EXEC_LOCAL_FRAME_STATE_INACTIVE) {
        if (pop_after_release) {
            vm_exec_pop_released_local_frame(vm);
        }
        return VM_EXEC_STATUS_OK;
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_EBP, &ebp) || ebp != frame->frame_base_address) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_INVALID_FRAME_STATE, instruction, boundary, stage, 0U, frame->frame_base_address, true);
        return VM_EXEC_STATUS_INVALID_FRAME_STATE;
    }
    status = vm_exec_read_u32_at_address(vm, instruction, frame->saved_ebp_address, &saved_ebp);
    if (status != VM_EXEC_STATUS_OK || saved_ebp != frame->saved_ebp_value) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_INVALID_FRAME_STATE, instruction, boundary, stage, 0U, frame->saved_ebp_address, true);
        return VM_EXEC_STATUS_INVALID_FRAME_STATE;
    }
    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, frame->saved_ebp_address + 4U) ||
        !vm_cpu_write_register(&vm->cpu, VM_REGISTER_EBP, saved_ebp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    frame->state = VM_EXEC_LOCAL_FRAME_STATE_INACTIVE;
    vm_exec_deactivate_local_descriptors(vm, frame->frame_id);
    vm_memory_clear_changes(&vm->memory);
    if (pop_after_release) {
        vm_exec_pop_released_local_frame(vm);
    }
    return VM_EXEC_STATUS_OK;
}

/// Ensures the current instruction has an automatic LOCAL frame when required.
///
/// @param vm VM instance to inspect and possibly mutate.
/// @param instruction Current instruction used for diagnostics.
/// @return OK when execution may proceed, otherwise a Phase 79 frame status.
static VmExecStatus vm_exec_ensure_local_frame_before_current_instruction(Vm *vm, const VmIrInstruction *instruction) {
    const VmExecProcedureBoundary *boundary = NULL;
    VmExecLocalFrame *frame = NULL;
    uint32_t esp = 0U;
    uint32_t footprint = 0U;
    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
    if (!vm_exec_procedure_boundary_has_locals(boundary)) {
        return VM_EXEC_STATUS_OK;
    }
    frame = vm_exec_top_local_frame_for_boundary(vm, boundary);
    if (vm_exec_local_frame_matches_boundary(frame, boundary)) {
        return VM_EXEC_STATUS_OK;
    }
    if (frame != NULL && frame->state == VM_EXEC_LOCAL_FRAME_STATE_INACTIVE && instruction->opcode == VM_IR_OPCODE_RET) {
        return VM_EXEC_STATUS_OK;
    }
    if (boundary->is_selected_entry && vm->instruction_pointer == boundary->start_instruction_index && !vm->selected_entry_local_frame_created && vm->active_helper_return_count == 0U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_ESP, &esp)) {
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
        footprint = vm_exec_selected_entry_local_footprint(boundary);
        if (footprint == 0U || !vm_exec_stack_can_reserve_bytes(vm, esp, footprint)) {
            vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction, boundary, "selected-entry-local-frame-setup", footprint, esp, true);
            return VM_EXEC_STATUS_STACK_OVERFLOW;
        }
        vm->selected_entry_local_frame_created = true;
        return vm_exec_setup_local_frame(vm, instruction, boundary, "selected-entry-local-frame-setup");
    }
    vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_LOCAL_FRAME_ENTRY_UNSUPPORTED, instruction, boundary, "local-frame-entry", 0U, 0U, false);
    return VM_EXEC_STATUS_LOCAL_FRAME_ENTRY_UNSUPPORTED;
}

/// Resolves a memory operand while optionally substituting a post-POP ESP value.
///
/// Phase 72A POP memory destinations that use ESP as the base register compute
/// their effective address from post-pop ESP while remaining externally atomic.
/// This helper mirrors normal flat 32-bit effective-address arithmetic and
/// changes only the ESP base value used for that address computation.
///
/// @param vm VM whose CPU supplies non-ESP base registers.
/// @param operand Memory operand to resolve.
/// @param esp_override Value to use when @p operand is based on ESP.
/// @param use_esp_override Whether ESP should be substituted.
/// @param out_address Receives the effective address.
/// @return true when the address could be resolved.
static bool vm_exec_resolve_memory_address_with_esp_override(
    const Vm *vm,
    const VmIrOperand *operand,
    uint32_t esp_override,
    bool use_esp_override,
    uint32_t *out_address
) {
    uint32_t base_value = 0U;

    if (vm == NULL || operand == NULL || out_address == NULL) {
        return false;
    }

    if (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS) {
        *out_address = operand->address;
        return true;
    }

    if (operand->kind != VM_IR_OPERAND_MEMORY_REGISTER) {
        return false;
    }

    if (use_esp_override && operand->reg == VM_REGISTER_ESP) {
        base_value = esp_override;
    } else if (!vm_cpu_read_register(&vm->cpu, operand->reg, &base_value)) {
        return false;
    }

    *out_address = operand->address + base_value + operand->immediate;
    return true;
}

/// Returns whether a Phase 72A stack operand is an accepted 32-bit general register.
///
/// @param operand Operand to inspect.
/// @return true when @p operand is a 32-bit register operand.
static bool vm_exec_operand_is_32bit_register(const VmIrOperand *operand) {
    return operand != NULL &&
           operand->kind == VM_IR_OPERAND_REGISTER &&
           vm_cpu_register_width_bits(operand->reg) == 32U &&
           (operand->width_bits == 0U || operand->width_bits == 32U);
}

/// Returns whether a Phase 72A stack operand is an accepted 32-bit memory operand.
///
/// @param operand Operand to inspect.
/// @return true when @p operand is a memory operand with DWORD width metadata.
static bool vm_exec_operand_is_32bit_memory(const VmIrOperand *operand) {
    return vm_exec_operand_is_memory(operand) && operand->width_bits == 32U;
}

/// Executes one Phase 72A source-level PUSH instruction.
///
/// PUSH accepts a 32-bit register, immediate, or memory source. The pushed value
/// is resolved before the checked stack write, and ESP is committed only after
/// the write succeeds so fatal stack-write failures leave externally visible CPU
/// and memory state unchanged except for the recorded diagnostic metadata.
///
/// @param vm VM instance to mutate.
/// @param instruction PUSH instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_push(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t esp = 0U;
    uint32_t stack_address = 0U;
    uint32_t value = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!(vm_exec_operand_is_32bit_register(&instruction->source) ||
          vm_exec_operand_is_32bit_memory(&instruction->source) ||
          (instruction->source.kind == VM_IR_OPERAND_IMMEDIATE &&
           (instruction->source.width_bits == 0U || instruction->source.width_bits == 32U)))) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, 32U, &value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_ESP, &esp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    stack_address = esp - 4U;
    status = vm_exec_write_u32_at_address(vm, instruction, stack_address, value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, stack_address)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    return VM_EXEC_STATUS_OK;
}

/// Resolves a Phase 84 address-valued INVOKE argument without dereferencing it.
///
/// @param vm VM instance containing the active caller frame.
/// @param instruction Capture instruction associated with diagnostics.
/// @param operand Address-valued operand to resolve.
/// @param out_value Receives the 32-bit address value.
/// @return OK when the address was resolved, otherwise a targeted executor status.
static VmExecStatus vm_exec_resolve_invoke_address_argument(
    Vm *vm,
    const VmIrInstruction *instruction,
    const VmIrOperand *operand,
    uint32_t *out_value
) {
    if (vm == NULL || instruction == NULL || operand == NULL || out_value == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (operand->kind == VM_IR_OPERAND_IMMEDIATE && operand->relocation == VM_IR_RELOCATION_LOCAL) {
        VmIrOperand local_operand = vm_ir_operand_local_memory((int32_t)operand->address, 0, 32U);
        return vm_exec_resolve_local_memory_address(vm, instruction, &local_operand, out_value);
    }

    if (operand->kind == VM_IR_OPERAND_IMMEDIATE &&
        (operand->relocation == VM_IR_RELOCATION_DATA || operand->relocation == VM_IR_RELOCATION_CONST)) {
        *out_value = operand->immediate;
        return VM_EXEC_STATUS_OK;
    }

    return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
}

/// Captures one Phase 84 INVOKE DWORD argument before stack mutation begins.
///
/// Register and memory arguments are read through the ordinary checked operand
/// path; address-valued ADDR/OFFSET arguments compute an address and do not read
/// pointed-to bytes. The destination immediate selects the pending-argument
/// slot in source order.
///
/// @param vm VM instance to mutate.
/// @param instruction INVOKE capture instruction descriptor.
/// @return Executor status for the capture.
static VmExecStatus vm_exec_execute_invoke_capture_dword(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t index = 0U;
    uint32_t value = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || instruction->destination.kind != VM_IR_OPERAND_IMMEDIATE) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    index = instruction->destination.immediate;
    if (index >= (uint32_t)VM_EXEC_INVOKE_ARGUMENT_CAPACITY) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (instruction->source.kind == VM_IR_OPERAND_IMMEDIATE && instruction->source.relocation != VM_IR_RELOCATION_NONE) {
        status = vm_exec_resolve_invoke_address_argument(vm, instruction, &instruction->source, &value);
    } else {
        status = vm_exec_read_operand(vm, instruction, &instruction->source, 32U, &value);
    }
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    vm->pending_invoke_arguments[index] = value;
    vm->pending_invoke_argument_valid[index] = true;
    return VM_EXEC_STATUS_OK;
}

/// Clears all pending Phase 84 INVOKE argument capture slots.
///
/// @param vm VM instance to mutate. NULL is ignored.
static void vm_exec_clear_pending_invoke_arguments(Vm *vm) {
    if (vm == NULL) {
        return;
    }
    memset(vm->pending_invoke_arguments, 0, sizeof(vm->pending_invoke_arguments));
    memset(vm->pending_invoke_argument_valid, 0, sizeof(vm->pending_invoke_argument_valid));
}

/// Executes one Phase 84 INVOKE commit after every argument has been captured.
///
/// The commit preflights the complete argument-plus-CALL stack footprint, then
/// writes captured DWORD arguments right-to-left through checked memory helpers
/// before using the ordinary direct-CALL entry path for the return token,
/// PROC USES saves, and automatic LOCAL frame setup.
///
/// @param vm VM instance to mutate.
/// @param instruction INVOKE commit instruction descriptor.
/// @return Executor status for argument stack writes and CALL entry.
static VmExecStatus vm_exec_execute_invoke_commit(Vm *vm, const VmIrInstruction *instruction) {
    const VmExecProcedureBoundary *target_boundary = NULL;
    uint32_t arg_count = 0U;
    uint32_t arg_bytes = 0U;
    uint32_t call_bytes = 0U;
    uint32_t total_bytes = 0U;
    uint32_t esp = 0U;
    uint32_t current_esp = 0U;
    uint32_t current_depth = 0U;
    uint32_t attempted_depth = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    size_t reverse_index = 0U;
    uint32_t return_token = 0U;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    status = vm_exec_validate_call_target(vm, instruction);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    if (instruction->source.kind != VM_IR_OPERAND_BRANCH_TARGET) {
        return VM_EXEC_STATUS_INVALID_CALL_TARGET;
    }
    arg_count = (uint32_t)instruction->source.width_bits;
    if (arg_count == 0U || arg_count > (uint32_t)VM_EXEC_INVOKE_ARGUMENT_CAPACITY) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    for (reverse_index = 0U; reverse_index < (size_t)arg_count; reverse_index += 1U) {
        if (!vm->pending_invoke_argument_valid[reverse_index]) {
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
    }

    target_boundary = vm_exec_find_procedure_boundary_starting_at(vm, (size_t)instruction->destination.immediate);
    if (target_boundary != NULL && vm_exec_procedure_boundary_has_locals(target_boundary) &&
        (vm->active_local_frame_count >= (size_t)VM_MAX_CALL_DEPTH_LIMIT || !vm_exec_can_create_local_descriptors(vm, target_boundary))) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_INVALID_FRAME_STATE, instruction, target_boundary,
            "local-descriptor-capacity", target_boundary->local_frame_size_bytes, 0U, false);
        return VM_EXEC_STATUS_INVALID_FRAME_STATE;
    }
    current_depth = vm->current_call_depth > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)vm->current_call_depth;
    attempted_depth = current_depth == UINT32_MAX ? UINT32_MAX : current_depth + 1U;
    if (attempted_depth > vm->call_depth_limit) {
        vm_exec_set_call_depth_diagnostic(vm, instruction, current_depth, attempted_depth, vm->call_depth_limit);
        return VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED;
    }
    if (!vm_exec_instruction_index_to_pseudo_eip((size_t)instruction->source.immediate, &return_token)) {
        return VM_EXEC_STATUS_INVALID_CALL_TARGET;
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_ESP, &esp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (arg_count > UINT32_MAX / 4U) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    arg_bytes = arg_count * 4U;
    call_bytes = vm_exec_direct_call_combined_footprint(target_boundary);
    if (call_bytes == 0U || arg_bytes > UINT32_MAX - call_bytes) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    total_bytes = arg_bytes + call_bytes;
    if (!vm_exec_stack_can_reserve_bytes(vm, esp, total_bytes)) {
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction);
        return VM_EXEC_STATUS_STACK_OVERFLOW;
    }

    current_esp = esp;
    for (reverse_index = 0U; reverse_index < (size_t)arg_count; reverse_index += 1U) {
        size_t source_index = (size_t)arg_count - 1U - reverse_index;
        current_esp -= 4U;
        status = vm_exec_write_u32_at_address(vm, instruction, current_esp, vm->pending_invoke_arguments[source_index]);
        if (status != VM_EXEC_STATUS_OK) {
            return status;
        }
    }
    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, current_esp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    status = vm_exec_write_call_and_uses_frame(vm, instruction, return_token, target_boundary);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    vm_exec_clear_pending_invoke_arguments(vm);
    if (vm->active_helper_return_count < (size_t)UINT32_MAX) {
        vm->active_helper_return_count += 1U;
    }
    if (vm->current_call_depth < (size_t)UINT32_MAX) {
        vm->current_call_depth += 1U;
    }
    vm->root_code_stream_active = false;
    return VM_EXEC_STATUS_OK;
}

/// Executes one Phase 72A source-level POP instruction.
///
/// POP reads DWORD `[original ESP]` first, validates and commits the destination,
/// and then commits the appropriate final ESP value. Memory destinations that
/// use ESP as their base compute their address using post-pop ESP; `pop esp`
/// leaves ESP equal to the popped value. Fatal failures leave visible state
/// externally atomic.
///
/// @param vm VM instance to mutate.
/// @param instruction POP instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_pop(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t original_esp = 0U;
    uint32_t post_pop_esp = 0U;
    uint32_t popped_value = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!(vm_exec_operand_is_32bit_register(&instruction->destination) || vm_exec_operand_is_32bit_memory(&instruction->destination))) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_ESP, &original_esp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    post_pop_esp = original_esp + 4U;

    status = vm_exec_read_u32_at_address(vm, instruction, original_esp, &popped_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (instruction->destination.kind == VM_IR_OPERAND_REGISTER) {
        if (instruction->destination.reg == VM_REGISTER_ESP) {
            return vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, popped_value) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
        if (!vm_cpu_write_register(&vm->cpu, instruction->destination.reg, popped_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        return vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, post_pop_esp) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    {
        uint32_t destination_address = 0U;
        if (!vm_exec_resolve_memory_address_with_esp_override(
                vm,
                &instruction->destination,
                post_pop_esp,
                true,
                &destination_address)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        status = vm_exec_write_u32_at_address(vm, instruction, destination_address, popped_value);
        if (status != VM_EXEC_STATUS_OK) {
            return status;
        }
    }

    return vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, post_pop_esp) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_INVALID_ARGUMENT;
}

/// Executes one Phase 73 LEAVE stack-frame teardown instruction.
///
/// LEAVE is modeled as the atomic, validation-first equivalent of `mov esp,
/// ebp` followed by `pop ebp`: it checks DWORD `[EBP]` through the central
/// memory helper, and only after a successful read commits `ESP = EBP + 4` and
/// `EBP = savedEBP`. Flags, call-depth metadata, Program Console state, and
/// public memory-change rows are intentionally unchanged.
///
/// @param vm VM instance to mutate.
/// @param instruction LEAVE instruction descriptor.
/// @return Executor status for the checked saved-EBP read.
static VmExecStatus vm_exec_execute_leave(Vm *vm, const VmIrInstruction *instruction) {
    const VmExecProcedureBoundary *boundary = NULL;
    VmExecLocalFrame *frame = NULL;
    uint32_t ebp = 0U;
    uint32_t saved_ebp = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
    frame = vm_exec_top_local_frame_for_boundary(vm, boundary);
    if (vm_exec_local_frame_matches_boundary(frame, boundary)) {
        return vm_exec_release_local_frame(vm, instruction, boundary, "source-leave-local-frame-release", false);
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_EBP, &ebp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    status = vm_exec_read_u32_at_address(vm, instruction, ebp, &saved_ebp);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, ebp + 4U)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_EBP, saved_ebp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    return VM_EXEC_STATUS_OK;
}


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

/// Executes one CMP instruction and updates flags from a transient subtraction result.
///
/// CMP reads supported register, immediate, or memory operands at the selected
/// width, updates the modeled subtraction flags, and deliberately does not
/// write the transient result back to either operand. All memory reads must
/// succeed before flags are updated.
///
/// @param vm VM instance to mutate.
/// @param instruction Instruction to execute.
/// @param width_bits Execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_cmp(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    uint32_t left = 0U;
    uint32_t right = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || !vm_exec_operand_is_destination(&instruction->destination) ||
        instruction->source.kind == VM_IR_OPERAND_NONE ||
        (vm_exec_operand_is_memory(&instruction->destination) && vm_exec_operand_is_memory(&instruction->source))) {
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

    if (!vm_cpu_update_cmp_flags(&vm->cpu, left, right, width_bits)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_OK;
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

/// Returns the stable undefined-flag diagnostic code for one producer opcode.
///
/// @param opcode Opcode whose undefined flag behavior is being recorded.
/// @return Static diagnostic code, or NULL when the opcode is not a supported producer.
static const char *vm_exec_undefined_flag_code_for_opcode(VmIrOpcode opcode) {
    switch (opcode) {
        case VM_IR_OPCODE_SHL:
        case VM_IR_OPCODE_SAL:
        case VM_IR_OPCODE_SHR:
        case VM_IR_OPCODE_SAR:
            return "undefined-shift-flag";
        case VM_IR_OPCODE_ROL:
        case VM_IR_OPCODE_ROR:
            return "undefined-modeled-flag";
        default:
            return NULL;
    }
}

/// Marks one modeled flag undefined using the current producer instruction metadata.
///
/// @param cpu CPU state whose flag metadata should be updated.
/// @param flag Modeled flag left architecturally undefined by the producer.
/// @param instruction Producer instruction containing opcode and source metadata.
/// @return true when the metadata update succeeds.
static bool vm_exec_mark_flag_undefined_from_instruction(VmCpu *cpu, VmFlag flag, const VmIrInstruction *instruction) {
    const char *undefined_code = NULL;
    const char *producer_mnemonic = NULL;

    if (cpu == NULL || instruction == NULL) {
        return false;
    }

    undefined_code = vm_exec_undefined_flag_code_for_opcode(instruction->opcode);
    producer_mnemonic = vm_ir_opcode_name(instruction->opcode);
    if (undefined_code == NULL || producer_mnemonic == NULL) {
        return false;
    }

    return vm_cpu_mark_flag_undefined(
        cpu,
        flag,
        undefined_code,
        producer_mnemonic,
        instruction->source_file,
        instruction->source_line,
        0U,
        0U,
        0U,
        instruction->source_text,
        instruction->instruction_index
    );
}

/// Restores validity metadata for one modeled flag from an earlier CPU snapshot.
///
/// The EFLAGS bit itself is not changed. This is used by instructions such as
/// INC and DEC that architecturally preserve CF while defining other flags.
///
/// @param cpu CPU state whose metadata should be restored.
/// @param before Snapshot that owns the original flag metadata.
/// @param flag Flag metadata slot to restore.
/// @return true when the flag identifier is valid.
static bool vm_exec_restore_flag_validity_from_snapshot(VmCpu *cpu, const VmCpu *before, VmFlag flag) {
    if (cpu == NULL || before == NULL || (int)flag < 0 || (int)flag >= (int)VM_FLAG_COUNT) {
        return false;
    }

    cpu->flag_validity[(int)flag] = before->flag_validity[(int)flag];
    return true;
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
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, original_cf) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_CF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
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
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_OF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
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
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, original_cf) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_CF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
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
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_OF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
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
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, original_cf) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_CF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
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
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_OF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
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
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_OF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    }

    status = vm_exec_write_operand(vm, instruction, &instruction->destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
    }

    return status;
}


/// Executes one ROR instruction.
///
/// ROR rotates the selected destination width right. Count zero after the
/// standard raw_count & 31 mask is a complete no-op. For nonzero effective
/// counts, CF receives the most significant bit of the rotated result, ZF and
/// SF are preserved, and OF is defined only when the effective count is one.
/// Source-run code emits the corresponding undefined-modeled-flag warning for
/// non-one nonzero counts before this helper executes.
///
/// @param vm VM instance to mutate.
/// @param instruction ROR instruction to execute.
/// @param width_bits Destination execution width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_execute_rotate_right(Vm *vm, const VmIrInstruction *instruction, uint8_t width_bits) {
    VmCpu before_cpu;
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t second_sign_bit = 0U;
    uint32_t value = 0U;
    uint32_t result = 0U;
    uint8_t raw_count = 0U;
    uint8_t effective_count = 0U;
    uint8_t rotate_count = 0U;
    bool original_of = false;
    bool new_cf = false;
    bool new_sign = false;
    bool new_second_sign = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || !vm_exec_mask_for_width(width_bits, &mask)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (instruction->opcode != VM_IR_OPCODE_ROR) {
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
        result = ((result >> rotate_count) | (result << (width_bits - rotate_count))) & mask;
    }

    sign_bit = width_bits == 32U ? 0x80000000U : (1U << (width_bits - 1U));
    second_sign_bit = width_bits == 32U ? 0x40000000U : (1U << (width_bits - 2U));
    new_cf = (result & sign_bit) != 0U;
    new_sign = (result & sign_bit) != 0U;
    new_second_sign = (result & second_sign_bit) != 0U;
    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, new_cf)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (effective_count == 1U) {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, new_sign != new_second_sign)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else {
        if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, original_of) ||
            !vm_exec_mark_flag_undefined_from_instruction(&vm->cpu, VM_FLAG_OF, instruction)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
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

    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, carry_before) ||
        !vm_exec_restore_flag_validity_from_snapshot(&vm->cpu, &before_cpu, VM_FLAG_CF)) {
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

    for (index = 0U; index < (size_t)VM_EXEC_CANONICAL_DELTA_REGISTER_COUNT; index += 1U) {
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

/// Executes one unsigned MUL instruction with implicit accumulator operands.
///
/// MUL reads one register or memory source. The selected source width chooses
/// the implicit accumulator and result registers: AL*r/m8 writes AX, AX*r/m16
/// writes DX:AX, and EAX*r/m32 writes EDX:EAX. CF and OF are defined from the
/// upper product half; ZF and SF are preserved as deterministic educational
/// behavior for architecturally undefined flags.
///
/// @param vm VM instance to mutate.
/// @param instruction MUL instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_mul(Vm *vm, const VmIrInstruction *instruction) {
    VmCpu before_cpu;
    uint8_t width_bits = 0U;
    uint32_t source_value = 0U;
    uint32_t accumulator_value = 0U;
    uint64_t product = 0U;
    uint32_t low = 0U;
    uint32_t high = 0U;
    bool has_upper_half = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE ||
        (instruction->source.kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(&instruction->source))) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->source, &width_bits)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &source_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (width_bits == 8U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AL, &accumulator_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        product = (uint64_t)(accumulator_value & 0xFFU) * (uint64_t)(source_value & 0xFFU);
        low = (uint32_t)(product & 0xFFFFU);
        high = (uint32_t)((product >> 8U) & 0xFFU);
        has_upper_half = high != 0U;
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_AX, low)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 16U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &accumulator_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        product = (uint64_t)(accumulator_value & 0xFFFFU) * (uint64_t)(source_value & 0xFFFFU);
        low = (uint32_t)(product & 0xFFFFU);
        high = (uint32_t)((product >> 16U) & 0xFFFFU);
        has_upper_half = high != 0U;
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_AX, low) ||
            !vm_cpu_write_register(&vm->cpu, VM_REGISTER_DX, high)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 32U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_EAX, &accumulator_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        product = (uint64_t)accumulator_value * (uint64_t)source_value;
        low = (uint32_t)(product & 0xFFFFFFFFULL);
        high = (uint32_t)(product >> 32U);
        has_upper_half = high != 0U;
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_EAX, low) ||
            !vm_cpu_write_register(&vm->cpu, VM_REGISTER_EDX, high)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, has_upper_half) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, has_upper_half)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_OK;
}

/// Executes one unsigned DIV instruction with implicit accumulator operands.
///
/// DIV reads one register or memory divisor. The selected divisor width chooses
/// the implicit dividend and result registers: AX/r/m8 writes AL quotient and
/// AH remainder, DX:AX/r/m16 writes AX quotient and DX remainder, and
/// EDX:EAX/r/m32 writes EAX quotient and EDX remainder. Divide-by-zero and
/// quotient-overflow conditions stop before any implicit result register or
/// flag mutation.
///
/// @param vm VM instance to mutate.
/// @param instruction DIV instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_div(Vm *vm, const VmIrInstruction *instruction) {
    VmCpu before_cpu;
    uint8_t width_bits = 0U;
    uint32_t divisor_value = 0U;
    uint32_t low_value = 0U;
    uint32_t high_value = 0U;
    uint64_t dividend = 0ULL;
    uint64_t divisor = 0ULL;
    uint64_t quotient = 0ULL;
    uint64_t remainder = 0ULL;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE ||
        (instruction->source.kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(&instruction->source))) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->source, &width_bits)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &divisor_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (width_bits == 8U) {
        divisor = (uint64_t)(divisor_value & 0xFFU);
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &low_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        dividend = (uint64_t)(low_value & 0xFFFFU);
        if (divisor == 0ULL) {
            return VM_EXEC_STATUS_DIVIDE_BY_ZERO;
        }
        quotient = dividend / divisor;
        remainder = dividend % divisor;
        if (quotient > 0xFFULL) {
            return VM_EXEC_STATUS_QUOTIENT_OVERFLOW;
        }
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_AL, (uint32_t)quotient) ||
            !vm_cpu_write_register(&vm->cpu, VM_REGISTER_AH, (uint32_t)remainder)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 16U) {
        divisor = (uint64_t)(divisor_value & 0xFFFFU);
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &low_value) ||
            !vm_cpu_read_register(&vm->cpu, VM_REGISTER_DX, &high_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        dividend = ((uint64_t)(high_value & 0xFFFFU) << 16U) | (uint64_t)(low_value & 0xFFFFU);
        if (divisor == 0ULL) {
            return VM_EXEC_STATUS_DIVIDE_BY_ZERO;
        }
        quotient = dividend / divisor;
        remainder = dividend % divisor;
        if (quotient > 0xFFFFULL) {
            return VM_EXEC_STATUS_QUOTIENT_OVERFLOW;
        }
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_AX, (uint32_t)quotient) ||
            !vm_cpu_write_register(&vm->cpu, VM_REGISTER_DX, (uint32_t)remainder)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 32U) {
        divisor = (uint64_t)divisor_value;
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_EAX, &low_value) ||
            !vm_cpu_read_register(&vm->cpu, VM_REGISTER_EDX, &high_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        dividend = ((uint64_t)high_value << 32U) | (uint64_t)low_value;
        if (divisor == 0ULL) {
            return VM_EXEC_STATUS_DIVIDE_BY_ZERO;
        }
        quotient = dividend / divisor;
        remainder = dividend % divisor;
        if (quotient > 0xFFFFFFFFULL) {
            return VM_EXEC_STATUS_QUOTIENT_OVERFLOW;
        }
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_EAX, (uint32_t)quotient) ||
            !vm_cpu_write_register(&vm->cpu, VM_REGISTER_EDX, (uint32_t)remainder)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_OK;
}


/// Sign-interprets an unsigned two's-complement value at the requested width.
///
/// The helper avoids implementation-defined casts for the high bit by computing
/// the negative magnitude explicitly. Width 64 handles INT64_MIN as a special
/// case because its positive magnitude is not representable as int64_t.
///
/// @param value Raw unsigned bits to interpret.
/// @param width_bits Width of the signed integer, in bits.
/// @param out_value Receives the signed interpretation.
/// @return true when @p width_bits is 8, 16, 32, or 64.
static bool vm_exec_sign_interpret_u64(uint64_t value, uint8_t width_bits, int64_t *out_value) {
    uint64_t mask = 0ULL;
    uint64_t sign_bit = 0ULL;
    uint64_t raw = 0ULL;
    uint64_t magnitude = 0ULL;

    if (out_value == NULL) {
        return false;
    }
    if (width_bits == 64U) {
        mask = UINT64_MAX;
        sign_bit = 1ULL << 63U;
    } else if (width_bits == 32U) {
        mask = 0xFFFFFFFFULL;
        sign_bit = 1ULL << 31U;
    } else if (width_bits == 16U) {
        mask = 0xFFFFULL;
        sign_bit = 1ULL << 15U;
    } else if (width_bits == 8U) {
        mask = 0xFFULL;
        sign_bit = 1ULL << 7U;
    } else {
        return false;
    }

    raw = value & mask;
    if ((raw & sign_bit) == 0ULL) {
        *out_value = (int64_t)raw;
        return true;
    }

    if (width_bits == 64U && raw == sign_bit) {
        *out_value = INT64_MIN;
        return true;
    }

    magnitude = ((~raw) & mask) + 1ULL;
    *out_value = -(int64_t)magnitude;
    return true;
}

/// Returns the minimum signed quotient accepted for an IDIV divisor width.
///
/// @param width_bits Divisor width in bits.
/// @param out_min_value Receives the minimum signed quotient value.
/// @return true when @p width_bits is 8, 16, or 32.
static bool vm_exec_idiv_quotient_min(uint8_t width_bits, int64_t *out_min_value) {
    if (out_min_value == NULL) {
        return false;
    }
    if (width_bits == 8U) {
        *out_min_value = -128;
        return true;
    }
    if (width_bits == 16U) {
        *out_min_value = -32768;
        return true;
    }
    if (width_bits == 32U) {
        *out_min_value = -2147483648LL;
        return true;
    }
    return false;
}

/// Returns the maximum signed quotient accepted for an IDIV divisor width.
///
/// @param width_bits Divisor width in bits.
/// @param out_max_value Receives the maximum signed quotient value.
/// @return true when @p width_bits is 8, 16, or 32.
static bool vm_exec_idiv_quotient_max(uint8_t width_bits, int64_t *out_max_value) {
    if (out_max_value == NULL) {
        return false;
    }
    if (width_bits == 8U) {
        *out_max_value = 127;
        return true;
    }
    if (width_bits == 16U) {
        *out_max_value = 32767;
        return true;
    }
    if (width_bits == 32U) {
        *out_max_value = 2147483647LL;
        return true;
    }
    return false;
}

/// Writes successful IDIV quotient and remainder results to implicit registers.
///
/// @param cpu CPU state to mutate.
/// @param width_bits Divisor width in bits.
/// @param quotient Signed quotient known to fit the result register.
/// @param remainder Signed remainder known to fit the result register.
/// @return true when the selected implicit registers were writable.
static bool vm_exec_write_idiv_results(VmCpu *cpu, uint8_t width_bits, int64_t quotient, int64_t remainder) {
    uint32_t quotient_bits = (uint32_t)((uint64_t)quotient & 0xFFFFFFFFULL);
    uint32_t remainder_bits = (uint32_t)((uint64_t)remainder & 0xFFFFFFFFULL);

    if (cpu == NULL) {
        return false;
    }
    if (width_bits == 8U) {
        return vm_cpu_write_register(cpu, VM_REGISTER_AL, quotient_bits & 0xFFU) &&
               vm_cpu_write_register(cpu, VM_REGISTER_AH, remainder_bits & 0xFFU);
    }
    if (width_bits == 16U) {
        return vm_cpu_write_register(cpu, VM_REGISTER_AX, quotient_bits & 0xFFFFU) &&
               vm_cpu_write_register(cpu, VM_REGISTER_DX, remainder_bits & 0xFFFFU);
    }
    if (width_bits == 32U) {
        return vm_cpu_write_register(cpu, VM_REGISTER_EAX, quotient_bits) &&
               vm_cpu_write_register(cpu, VM_REGISTER_EDX, remainder_bits);
    }
    return false;
}

/// Executes one signed IDIV instruction with implicit accumulator operands.
///
/// IDIV reads one register or memory divisor. The selected divisor width chooses
/// the signed dividend and result registers: AX/r/m8 writes AL quotient and AH
/// remainder, DX:AX/r/m16 writes AX quotient and DX remainder, and EDX:EAX/r/m32
/// writes EAX quotient and EDX remainder. Divide-by-zero and quotient-overflow
/// conditions stop before quotient or remainder mutation. Modeled flag bits and
/// flag-validity metadata are preserved exactly.
///
/// @param vm VM instance to mutate.
/// @param instruction IDIV instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_idiv(Vm *vm, const VmIrInstruction *instruction) {
    VmCpu before_cpu;
    uint8_t width_bits = 0U;
    uint32_t divisor_value = 0U;
    uint32_t low_value = 0U;
    uint32_t high_value = 0U;
    uint64_t dividend_bits = 0ULL;
    int64_t dividend = 0;
    int64_t divisor = 0;
    int64_t quotient = 0;
    int64_t remainder = 0;
    int64_t min_quotient = 0;
    int64_t max_quotient = 0;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE ||
        (instruction->source.kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(&instruction->source))) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->source, &width_bits) ||
        !vm_exec_idiv_quotient_min(width_bits, &min_quotient) ||
        !vm_exec_idiv_quotient_max(width_bits, &max_quotient)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &divisor_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    if (!vm_exec_sign_interpret_u64((uint64_t)divisor_value, width_bits, &divisor)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (divisor == 0) {
        return VM_EXEC_STATUS_DIVIDE_BY_ZERO;
    }

    if (width_bits == 8U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &low_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        dividend_bits = (uint64_t)(low_value & 0xFFFFU);
        if (!vm_exec_sign_interpret_u64(dividend_bits, 16U, &dividend)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 16U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &low_value) ||
            !vm_cpu_read_register(&vm->cpu, VM_REGISTER_DX, &high_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        dividend_bits = ((uint64_t)(high_value & 0xFFFFU) << 16U) | (uint64_t)(low_value & 0xFFFFU);
        if (!vm_exec_sign_interpret_u64(dividend_bits, 32U, &dividend)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 32U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_EAX, &low_value) ||
            !vm_cpu_read_register(&vm->cpu, VM_REGISTER_EDX, &high_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
        dividend_bits = ((uint64_t)high_value << 32U) | (uint64_t)low_value;
        if (!vm_exec_sign_interpret_u64(dividend_bits, 64U, &dividend)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    if (dividend == INT64_MIN && divisor == -1) {
        return VM_EXEC_STATUS_QUOTIENT_OVERFLOW;
    }
    quotient = dividend / divisor;
    remainder = dividend % divisor;
    if (quotient < min_quotient || quotient > max_quotient) {
        return VM_EXEC_STATUS_QUOTIENT_OVERFLOW;
    }

    if (!vm_exec_write_idiv_results(&vm->cpu, width_bits, quotient, remainder)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_OK;
}

/// Executes the common explicit-destination signed IMUL writeback path.
///
/// The helper multiplies two signed operand-width values, writes the low
/// operand-width result to the register destination, sets CF/OF when signed
/// truncation loses significant bits, and preserves all other modeled flags.
///
/// @param vm VM instance to mutate.
/// @param instruction IMUL instruction descriptor used for write diagnostics.
/// @param destination Register destination to receive the low result.
/// @param left_value First signed input value before width interpretation.
/// @param right_value Second signed input value before width interpretation.
/// @param width_bits Operand width in bits.
/// @return Executor status.
static VmExecStatus vm_exec_finish_explicit_imul(
    Vm *vm,
    const VmIrInstruction *instruction,
    const VmIrOperand *destination,
    uint32_t left_value,
    uint32_t right_value,
    uint8_t width_bits
) {
    VmCpu before_cpu;
    int64_t left_signed = 0;
    int64_t right_signed = 0;
    int64_t product = 0;
    int64_t signed_min = 0;
    int64_t signed_max = 0;
    uint64_t product_bits = 0ULL;
    uint32_t result = 0U;
    bool significant_truncation = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL || destination == NULL || destination->kind != VM_IR_OPERAND_REGISTER ||
        !vm_exec_signed_min_for_width(width_bits, &signed_min) ||
        !vm_exec_signed_max_for_width(width_bits, &signed_max)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    left_signed = vm_exec_signed_value_for_width(left_value, width_bits);
    right_signed = vm_exec_signed_value_for_width(right_value, width_bits);
    product = left_signed * right_signed;
    product_bits = (uint64_t)product;
    significant_truncation = product < signed_min || product > signed_max;
    if (width_bits == 16U) {
        result = (uint32_t)(product_bits & 0xFFFFULL);
    } else if (width_bits == 32U) {
        result = (uint32_t)(product_bits & 0xFFFFFFFFULL);
    } else {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_write_operand(vm, instruction, destination, width_bits, result);
    if (status != VM_EXEC_STATUS_OK) {
        vm->cpu = before_cpu;
        return status;
    }
    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, significant_truncation) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, significant_truncation)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_OK;
}

/// Executes one explicit two-operand signed IMUL instruction.
///
/// This Phase 55 form multiplies a 16-bit or 32-bit register destination by a
/// same-width register or memory source, stores only the low destination-width
/// result back into the destination register, sets CF/OF on signed truncation,
/// and preserves ZF/SF.
///
/// @param vm VM instance to mutate.
/// @param instruction IMUL instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_explicit_imul(Vm *vm, const VmIrInstruction *instruction) {
    uint8_t width_bits = 0U;
    uint32_t destination_value = 0U;
    uint32_t source_value = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_REGISTER ||
        (instruction->source.kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(&instruction->source)) ||
        !vm_exec_operand_width(&instruction->destination, &width_bits) ||
        (width_bits != 16U && width_bits != 32U)) {
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

    return vm_exec_finish_explicit_imul(vm, instruction, &instruction->destination, destination_value, source_value, width_bits);
}

/// Executes one signed IMUL instruction.
///
/// With no explicit destination this preserves the Phase 54 implicit-accumulator
/// form. With a register destination this executes the Phase 55 two-operand
/// form. Three-operand immediate forms use @ref vm_exec_execute_imul_immediate.
///
/// @param vm VM instance to mutate.
/// @param instruction IMUL instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_imul(Vm *vm, const VmIrInstruction *instruction) {
    VmCpu before_cpu;
    uint8_t width_bits = 0U;
    uint32_t source_value = 0U;
    uint32_t accumulator_value = 0U;
    int64_t source_signed = 0;
    int64_t accumulator_signed = 0;
    int64_t product = 0;
    int64_t signed_min = 0;
    int64_t signed_max = 0;
    uint64_t product_bits = 0ULL;
    uint32_t low = 0U;
    uint32_t high = 0U;
    bool significant_truncation = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE) {
        return vm_exec_execute_explicit_imul(vm, instruction);
    }
    if (instruction->source.kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(&instruction->source)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_exec_operand_width(&instruction->source, &width_bits) ||
        !vm_exec_signed_min_for_width(width_bits, &signed_min) ||
        !vm_exec_signed_max_for_width(width_bits, &signed_max)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    before_cpu = vm->cpu;
    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &source_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }

    if (width_bits == 8U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AL, &accumulator_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 16U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_AX, &accumulator_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 32U) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_EAX, &accumulator_value)) {
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    source_signed = vm_exec_signed_value_for_width(source_value, width_bits);
    accumulator_signed = vm_exec_signed_value_for_width(accumulator_value, width_bits);
    product = accumulator_signed * source_signed;
    product_bits = (uint64_t)product;
    significant_truncation = product < signed_min || product > signed_max;

    if (width_bits == 8U) {
        low = (uint32_t)(product_bits & 0xFFFFULL);
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_AX, low)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else if (width_bits == 16U) {
        low = (uint32_t)(product_bits & 0xFFFFULL);
        high = (uint32_t)((product_bits >> 16U) & 0xFFFFULL);
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_AX, low) ||
            !vm_cpu_write_register(&vm->cpu, VM_REGISTER_DX, high)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    } else {
        low = (uint32_t)(product_bits & 0xFFFFFFFFULL);
        high = (uint32_t)((product_bits >> 32U) & 0xFFFFFFFFULL);
        if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_EAX, low) ||
            !vm_cpu_write_register(&vm->cpu, VM_REGISTER_EDX, high)) {
            vm->cpu = before_cpu;
            return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
        }
    }

    if (!vm_cpu_write_flag(&vm->cpu, VM_FLAG_CF, significant_truncation) ||
        !vm_cpu_write_flag(&vm->cpu, VM_FLAG_OF, significant_truncation)) {
        vm->cpu = before_cpu;
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    return VM_EXEC_STATUS_OK;
}

/// Executes one three-operand signed IMUL instruction with an immediate factor.
///
/// The parser stores the signed immediate's operand-width two's-complement bits
/// in the destination operand's immediate field. The executor multiplies the
/// register or memory source by that immediate and writes the low result to the
/// 16-bit or 32-bit register destination.
///
/// @param vm VM instance to mutate.
/// @param instruction IMUL immediate instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_imul_immediate(Vm *vm, const VmIrInstruction *instruction) {
    uint8_t width_bits = 0U;
    uint32_t source_value = 0U;
    uint32_t immediate_value = 0U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_REGISTER ||
        (instruction->source.kind != VM_IR_OPERAND_REGISTER && !vm_exec_operand_is_memory(&instruction->source)) ||
        !vm_exec_operand_width(&instruction->destination, &width_bits) ||
        (width_bits != 16U && width_bits != 32U)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

    status = vm_exec_read_operand(vm, instruction, &instruction->source, width_bits, &source_value);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    immediate_value = instruction->destination.immediate;

    return vm_exec_finish_explicit_imul(vm, instruction, &instruction->destination, source_value, immediate_value, width_bits);
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
    const VmExecProcedureBoundary *boundary = NULL;
    VmExecStatus status = VM_EXEC_STATUS_OK;
    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_NONE || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
    if (vm_exec_procedure_boundary_has_locals(boundary)) {
        status = vm_exec_release_local_frame(vm, instruction, boundary, "exit-local-frame-release", true);
        if (status != VM_EXEC_STATUS_OK) {
            return status;
        }
    }
    vm->halted = true;
    return VM_EXEC_STATUS_OK;
}


/// Validates one lowered direct branch target before instruction-pointer transfer.
///
/// A direct branch is valid only when parser lowering produced a branch-target
/// operand whose target index is inside the loaded instruction array. This
/// helper performs validation only; @ref vm_step applies any unconditional or
/// conditional transfer after it knows the instruction committed successfully and
/// can increment instruction accounting exactly once.
///
/// @param vm VM instance containing the loaded program bounds.
/// @param instruction Branch instruction descriptor to validate.
/// @return OK for a valid direct branch, otherwise an executor status.
static VmExecStatus vm_exec_validate_branch_target(const Vm *vm, const VmIrInstruction *instruction) {
    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_BRANCH_TARGET || instruction->source.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_INVALID_BRANCH_TARGET;
    }
    if ((size_t)instruction->destination.immediate >= vm->program_count) {
        return VM_EXEC_STATUS_INVALID_BRANCH_TARGET;
    }

    return VM_EXEC_STATUS_OK;
}

/// Validates one lowered direct CALL target before stack mutation.
///
/// A Phase 69 direct CALL uses the same compact branch-target operand metadata
/// as direct branches, but malformed CALL metadata reports its own runtime
/// status so later RET and call-depth phases can distinguish call failures from
/// branch failures.
///
/// @param vm VM instance containing the loaded program bounds.
/// @param instruction CALL instruction descriptor to validate.
/// @return OK for a valid direct CALL, otherwise an executor status.
static VmExecStatus vm_exec_validate_call_target(const Vm *vm, const VmIrInstruction *instruction) {
    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_BRANCH_TARGET ||
        (instruction->source.kind != VM_IR_OPERAND_NONE && instruction->source.kind != VM_IR_OPERAND_BRANCH_TARGET)) {
        return VM_EXEC_STATUS_INVALID_CALL_TARGET;
    }
    if ((size_t)instruction->destination.immediate >= vm->program_count) {
        return VM_EXEC_STATUS_INVALID_CALL_TARGET;
    }

    return VM_EXEC_STATUS_OK;
}



/// Pushes one active Phase 77 PROC USES runtime frame.
///
/// @param vm VM instance to mutate.
/// @param boundary Procedure boundary whose USES metadata was saved.
/// @return true when the active frame was recorded.
static bool vm_exec_push_active_uses_frame(Vm *vm, const VmExecProcedureBoundary *boundary) {
    VmExecUsesFrame *frame = NULL;
    size_t index = 0U;

    if (vm == NULL || boundary == NULL || boundary->uses_register_count > (size_t)VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY ||
        vm->active_uses_frame_count >= (size_t)VM_MAX_CALL_DEPTH_LIMIT) {
        return false;
    }

    frame = &vm->active_uses_frames[vm->active_uses_frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->procedure_start_instruction_index = boundary->start_instruction_index;
    frame->uses_register_count = boundary->uses_register_count;
    for (index = 0U; index < boundary->uses_register_count; index += 1U) {
        frame->uses_registers[index] = boundary->uses_registers[index];
    }
    vm->active_uses_frame_count += 1U;
    return true;
}

/// Writes the CALL return token and automatic PROC USES save slots.
///
/// The full CALL-plus-USES stack footprint is preflighted before any mutation so
/// save failure cannot branch into a procedure with a partially committed frame.
/// All committed DWORD writes still go through the checked memory helpers.
///
/// @param vm VM instance to mutate.
/// @param instruction CALL instruction associated with diagnostics.
/// @param return_token Pseudo-EIP token for the CALL successor.
/// @param target_boundary Optional called procedure boundary containing USES metadata.
/// @return OK when the stack frame was committed, stack-overflow on USES reservation/write failure, or another executor status.
static VmExecStatus vm_exec_write_call_and_uses_frame(
    Vm *vm,
    const VmIrInstruction *instruction,
    uint32_t return_token,
    const VmExecProcedureBoundary *target_boundary
) {
    uint32_t esp = 0U;
    uint32_t write_address = 0U;
    size_t uses_count = 0U;
    size_t index = 0U;
    uint32_t required_bytes = 4U;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (target_boundary != NULL) {
        if (target_boundary->uses_register_count > (size_t)VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY) {
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
        uses_count = target_boundary->uses_register_count;
        required_bytes = vm_exec_direct_call_combined_footprint(target_boundary);
        if (required_bytes == 0U) {
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_ESP, &esp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (target_boundary != NULL && vm_exec_procedure_boundary_has_locals(target_boundary) &&
        (vm->active_local_frame_count >= (size_t)VM_MAX_CALL_DEPTH_LIMIT || !vm_exec_can_create_local_descriptors(vm, target_boundary))) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_INVALID_FRAME_STATE, instruction, target_boundary,
            "local-descriptor-capacity", target_boundary->local_frame_size_bytes, 0U, false);
        return VM_EXEC_STATUS_INVALID_FRAME_STATE;
    }
    if (target_boundary != NULL && (uses_count > 0U || vm_exec_procedure_boundary_has_locals(target_boundary)) &&
        !vm_exec_stack_can_reserve_bytes(vm, esp, required_bytes)) {
        vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction, target_boundary,
            vm_exec_procedure_boundary_has_locals(target_boundary) ? "direct-call-local-combined-frame-setup" : "direct-call-uses-frame-setup",
            required_bytes, esp, true);
        return VM_EXEC_STATUS_STACK_OVERFLOW;
    }

    write_address = esp - 4U;
    status = vm_exec_write_u32_at_address(vm, instruction, write_address, return_token);
    if (status != VM_EXEC_STATUS_OK) {
        if (target_boundary != NULL && (uses_count > 0U || vm_exec_procedure_boundary_has_locals(target_boundary))) {
            vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction, target_boundary,
                vm_exec_procedure_boundary_has_locals(target_boundary) ? "direct-call-local-combined-frame-setup" : "direct-call-uses-frame-setup",
                required_bytes, write_address, true);
            return VM_EXEC_STATUS_STACK_OVERFLOW;
        }
        return status;
    }
    for (index = 0U; index < uses_count; index += 1U) {
        uint32_t saved_value = 0U;
        if (!vm_cpu_read_register(&vm->cpu, target_boundary->uses_registers[index], &saved_value)) {
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
        write_address -= 4U;
        status = vm_exec_write_u32_at_address(vm, instruction, write_address, saved_value);
        if (status != VM_EXEC_STATUS_OK) {
            vm_exec_set_local_frame_diagnostic(vm, VM_EXEC_STATUS_STACK_OVERFLOW, instruction, target_boundary,
                vm_exec_procedure_boundary_has_locals(target_boundary) ? "direct-call-local-combined-frame-setup" : "direct-call-uses-frame-setup",
                required_bytes, write_address, true);
            return VM_EXEC_STATUS_STACK_OVERFLOW;
        }
    }
    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, write_address)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (uses_count > 0U && !vm_exec_push_active_uses_frame(vm, target_boundary)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (vm_exec_procedure_boundary_has_locals(target_boundary)) {
        status = vm_exec_setup_local_frame(vm, instruction, target_boundary, "direct-call-local-combined-frame-setup");
        if (status != VM_EXEC_STATUS_OK) {
            return status;
        }
    }
    return VM_EXEC_STATUS_OK;
}


/// Returns the active USES frame owned by the current RET instruction, if any.
///
/// @param vm VM instance containing active USES frames and procedure metadata.
/// @return Active frame for the containing procedure, or NULL when the RET has no USES frame.
static const VmExecUsesFrame *vm_exec_current_ret_uses_frame(const Vm *vm) {
    const VmExecProcedureBoundary *boundary = NULL;

    if (vm == NULL || vm->active_uses_frame_count == 0U || vm->instruction_pointer >= vm->program_count) {
        return NULL;
    }
    boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
    if (boundary == NULL || !vm_exec_active_uses_frame_matches_start(vm, boundary->start_instruction_index)) {
        return NULL;
    }
    return &vm->active_uses_frames[vm->active_uses_frame_count - 1U];
}

/// Reads saved PROC USES register values without committing CPU mutation.
///
/// @param vm VM instance whose stack should be inspected.
/// @param instruction RET instruction associated with diagnostics.
/// @param frame Active USES frame to restore.
/// @param original_esp ESP at the start of RET execution.
/// @param out_values Receives saved values in declared USES order.
/// @param out_return_esp Receives ESP address where the return token starts after USES slots.
/// @return OK when all saved slots were readable, stack-underflow on restore failure, or invalid-argument.
static VmExecStatus vm_exec_read_uses_restore_values(
    Vm *vm,
    const VmIrInstruction *instruction,
    const VmExecUsesFrame *frame,
    uint32_t original_esp,
    uint32_t *out_values,
    uint32_t *out_return_esp
) {
    size_t reverse_index = 0U;

    if (vm == NULL || instruction == NULL || frame == NULL || out_values == NULL || out_return_esp == NULL ||
        frame->uses_register_count > (size_t)VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    for (reverse_index = 0U; reverse_index < frame->uses_register_count; reverse_index += 1U) {
        size_t declared_index = frame->uses_register_count - 1U - reverse_index;
        uint32_t address = original_esp + (uint32_t)(reverse_index * 4U);
        VmExecStatus status = vm_exec_read_u32_at_address(vm, instruction, address, &out_values[declared_index]);
        if (status != VM_EXEC_STATUS_OK) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_STACK_UNDERFLOW, instruction);
            return VM_EXEC_STATUS_STACK_UNDERFLOW;
        }
    }
    *out_return_esp = original_esp + (uint32_t)(frame->uses_register_count * 4U);
    return VM_EXEC_STATUS_OK;
}

/// Commits previously validated PROC USES restore register values.
///
/// @param vm VM instance to mutate.
/// @param frame Active USES frame whose registers should be restored.
/// @param values Saved register values in declared USES order.
/// @return OK when all registers were restored.
static VmExecStatus vm_exec_commit_uses_restore_values(Vm *vm, const VmExecUsesFrame *frame, const uint32_t *values) {
    size_t index = 0U;

    if (vm == NULL || frame == NULL || values == NULL || frame->uses_register_count > (size_t)VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < frame->uses_register_count; index += 1U) {
        if (!vm_cpu_write_register(&vm->cpu, frame->uses_registers[index], values[index])) {
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
    }
    if (vm->active_uses_frame_count > 0U) {
        vm->active_uses_frame_count -= 1U;
        memset(&vm->active_uses_frames[vm->active_uses_frame_count], 0, sizeof(vm->active_uses_frames[vm->active_uses_frame_count]));
    }
    return VM_EXEC_STATUS_OK;
}

/// Executes one direct user-procedure CALL and optional Phase 77 PROC USES save.
///
/// The helper validates target metadata before mutation, applies the Phase 72
/// call-depth limit before the implicit stack write, computes the return token
/// from parser-owned return-target metadata when present, falls back to the
/// next lowered instruction for hand-authored native fixtures, writes that token
/// to `ESP - 4` through checked memory, and saves any target PROC USES
/// registers before entry. ESP and call depth are updated only after the
/// complete CALL-plus-USES stack frame succeeds. The caller applies the
/// instruction-pointer transfer after successful execution so accounting and
/// deltas stay centralized in @ref vm_step.
///
/// @param vm VM instance to mutate.
/// @param instruction CALL instruction descriptor.
/// @return Executor status for the CALL stack frame write and optional USES save.
static VmExecStatus vm_exec_execute_call(Vm *vm, const VmIrInstruction *instruction) {
    VmExecStatus target_status = VM_EXEC_STATUS_OK;
    const VmExecProcedureBoundary *target_boundary = NULL;
    uint32_t return_token = 0U;
    uint32_t current_depth = 0U;
    uint32_t attempted_depth = 0U;
    VmExecStatus stack_status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    target_status = vm_exec_validate_call_target(vm, instruction);
    if (target_status != VM_EXEC_STATUS_OK) {
        return target_status;
    }
    target_boundary = vm_exec_find_procedure_boundary_starting_at(vm, (size_t)instruction->destination.immediate);

    current_depth = vm->current_call_depth > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)vm->current_call_depth;
    attempted_depth = current_depth == UINT32_MAX ? UINT32_MAX : current_depth + 1U;
    if (attempted_depth > vm->call_depth_limit) {
        vm_exec_set_call_depth_diagnostic(vm, instruction, current_depth, attempted_depth, vm->call_depth_limit);
        return VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED;
    }
    if (instruction->source.kind == VM_IR_OPERAND_BRANCH_TARGET) {
        if (!vm_exec_instruction_index_to_pseudo_eip((size_t)instruction->source.immediate, &return_token)) {
            return VM_EXEC_STATUS_INVALID_CALL_TARGET;
        }
    } else if (!vm_exec_instruction_index_to_pseudo_eip(vm->instruction_pointer + 1U, &return_token)) {
        return VM_EXEC_STATUS_INVALID_CALL_TARGET;
    }

    stack_status = vm_exec_write_call_and_uses_frame(vm, instruction, return_token, target_boundary);
    if (stack_status != VM_EXEC_STATUS_OK) {
        return stack_status;
    }

    if (vm->active_helper_return_count < (size_t)UINT32_MAX) {
        vm->active_helper_return_count += 1U;
    }
    if (vm->current_call_depth < (size_t)UINT32_MAX) {
        vm->current_call_depth += 1U;
    }
    vm->root_code_stream_active = false;

    return VM_EXEC_STATUS_OK;
}

/// Returns whether an ESP value is valid for the active stack region.
///
/// Phase 68A defines the empty stack as the first byte past the active stack
/// region. Phase 74 RET imm16 cleanup may therefore leave ESP either inside the
/// stack region or exactly at that exclusive high boundary.
///
/// @param vm VM whose active stack region should be inspected.
/// @param esp Candidate ESP value after stack cleanup.
/// @return true when ESP is inside the stack region or exactly at its high boundary.
static bool vm_exec_stack_pointer_is_inside_or_empty_boundary(const Vm *vm, uint32_t esp) {
    const VmMemoryRegion *stack_region = NULL;
    uint64_t stack_limit = 0U;

    if (vm == NULL) {
        return false;
    }
    stack_region = vm_memory_get_region(&vm->memory, VM_MEMORY_REGION_STACK);
    if (stack_region == NULL || stack_region->size == 0U) {
        return false;
    }
    stack_limit = (uint64_t)stack_region->base + (uint64_t)stack_region->size;
    if (stack_limit > (uint64_t)UINT32_MAX) {
        return false;
    }

    return esp == (uint32_t)stack_limit || (esp >= stack_region->base && esp < (uint32_t)stack_limit);
}

/// Executes one near RET root/helper operation.
///
/// A root-code-stream RET with no helper return pending uses the configured
/// Phase 71A root RET mode: the default mode terminates successfully before
/// reading `[ESP]`, while strict mode stops before stack access with a teaching
/// diagnostic. Every ordinary helper RET preserves the Phase 70 checked DWORD
/// pseudo-EIP token read and return-target validation, then Phase 74 applies an
/// optional unsigned imm16 cleanup only after the return token is valid.
///
/// @param vm VM instance to mutate.
/// @param instruction RET instruction descriptor.
/// @return Executor status for the RET read, return-target validation, and optional cleanup.
static VmExecStatus vm_exec_execute_ret(Vm *vm, const VmIrInstruction *instruction) {
    VmMemoryDiagnostic memory_diagnostic;
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;
    const VmExecUsesFrame *uses_frame = NULL;
    VmExecUsesFrame uses_frame_copy;
    const VmExecProcedureBoundary *boundary = NULL;
    uint32_t uses_restore_values[VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY];
    uint32_t original_esp = 0U;
    uint32_t return_esp = 0U;
    uint32_t cleanup_bytes = 0U;
    uint32_t final_esp = 0U;
    uint32_t return_token = 0U;
    size_t return_index = 0U;
    VmExecStatus restore_status = VM_EXEC_STATUS_OK;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    memset(&uses_frame_copy, 0, sizeof(uses_frame_copy));
    memset(uses_restore_values, 0, sizeof(uses_restore_values));
    if (instruction->destination.kind != VM_IR_OPERAND_NONE) {
        return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }
    if (instruction->source.kind != VM_IR_OPERAND_NONE) {
        if (instruction->source.kind != VM_IR_OPERAND_IMMEDIATE || instruction->source.width_bits != 16U || instruction->source.immediate > 65535U) {
            return VM_EXEC_STATUS_INVALID_INSTRUCTION;
        }
        cleanup_bytes = instruction->source.immediate;
    }
    if (vm_exec_root_metadata_is_inconsistent(vm)) {
        return VM_EXEC_STATUS_INVALID_ROOT_TERMINATION_STATE;
    }
    boundary = vm_exec_find_procedure_boundary(vm, vm->instruction_pointer);
    if (vm_exec_current_instruction_is_root_ret(vm)) {
        if (vm->root_ret_mode == VM_ROOT_RET_MODE_STRICT_CALL_FRAME) {
            return VM_EXEC_STATUS_ROOT_RET_DISALLOWED_BY_MODE;
        }
        if (vm_exec_procedure_boundary_has_locals(boundary)) {
            VmExecStatus status = vm_exec_release_local_frame(vm, instruction, boundary, "root-ret-local-frame-release", true);
            if (status != VM_EXEC_STATUS_OK) {
                return status;
            }
        }
        vm->halted = true;
        return VM_EXEC_STATUS_OK;
    }
    if (vm_exec_procedure_boundary_has_locals(boundary)) {
        VmExecStatus status = vm_exec_release_local_frame(vm, instruction, boundary, "helper-ret-local-frame-release", true);
        if (status != VM_EXEC_STATUS_OK) {
            return status;
        }
    }
    if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_ESP, &original_esp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    return_esp = original_esp;
    uses_frame = vm_exec_current_ret_uses_frame(vm);
    if (uses_frame != NULL) {
        uses_frame_copy = *uses_frame;
        restore_status = vm_exec_read_uses_restore_values(vm, instruction, uses_frame, original_esp, uses_restore_values, &return_esp);
        if (restore_status != VM_EXEC_STATUS_OK) {
            return restore_status;
        }
    }

    memset(&memory_diagnostic, 0, sizeof(memory_diagnostic));
    memory_status = vm_memory_read_u32(&vm->memory, return_esp, &return_token, &memory_diagnostic);
    vm_exec_record_memory_access(vm, VM_EXEC_MEMORY_ACCESS_READ, return_esp, 32U, memory_status);
    if (!vm_memory_status_succeeded(memory_status)) {
        vm_exec_set_memory_diagnostic(vm, instruction, memory_status, &memory_diagnostic);
        return VM_EXEC_STATUS_MEMORY_ERROR;
    }

    if (!vm_exec_pseudo_eip_to_instruction_index(return_token, vm->program_count, &return_index)) {
        return VM_EXEC_STATUS_INVALID_RETURN_ADDRESS;
    }

    final_esp = (uint32_t)(return_esp + 4U + cleanup_bytes);
    if (!vm_exec_stack_pointer_is_inside_or_empty_boundary(vm, final_esp)) {
        return VM_EXEC_STATUS_RET_STACK_CLEANUP_OUT_OF_RANGE;
    }

    if (uses_frame != NULL) {
        restore_status = vm_exec_commit_uses_restore_values(vm, &uses_frame_copy, uses_restore_values);
        if (restore_status != VM_EXEC_STATUS_OK) {
            return restore_status;
        }
    }
    if (!vm_cpu_write_register(&vm->cpu, VM_REGISTER_ESP, final_esp)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    vm->instruction_pointer = return_index;
    if (vm->active_helper_return_count > 0U) {
        vm->active_helper_return_count -= 1U;
    }
    if (vm->current_call_depth > 0U) {
        vm->current_call_depth -= 1U;
    }
    return VM_EXEC_STATUS_OK;
}


/// Executes LEA effective-address computation.
///
/// LEA writes the computed 32-bit address into a 32-bit register destination.
/// It does not read from memory, write to memory, validate the computed address
/// against mapped regions, or mutate modeled flags.
///
/// @param vm VM instance to mutate.
/// @param instruction LEA instruction descriptor.
/// @return Executor status.
static VmExecStatus vm_exec_execute_lea(Vm *vm, const VmIrInstruction *instruction) {
    uint32_t address = 0U;

    if (vm == NULL || instruction == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->destination.kind != VM_IR_OPERAND_REGISTER || vm_cpu_register_width_bits(instruction->destination.reg) != 32U) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (vm_exec_operand_is_local_memory(&instruction->source)) {
        VmExecStatus local_status = vm_exec_resolve_local_memory_address(vm, instruction, &instruction->source, &address);
        if (local_status != VM_EXEC_STATUS_OK) {
            return local_status;
        }
    } else if (!vm_exec_resolve_effective_address(vm, &instruction->source, &address)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }
    if (!vm_cpu_write_register(&vm->cpu, instruction->destination.reg, address)) {
        return VM_EXEC_STATUS_UNSUPPORTED_OPERAND;
    }

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
        case VM_IR_OPCODE_CMP:
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
            if (instruction->opcode == VM_IR_OPCODE_CMP) {
                return vm_exec_execute_cmp(vm, instruction, width_bits);
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
        case VM_IR_OPCODE_ROR:
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
            if (instruction->opcode == VM_IR_OPCODE_ROR) {
                return vm_exec_execute_rotate_right(vm, instruction, width_bits);
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
        case VM_IR_OPCODE_LEA:
            return vm_exec_execute_lea(vm, instruction);
        case VM_IR_OPCODE_JMP:
        case VM_IR_OPCODE_JE:
        case VM_IR_OPCODE_JZ:
        case VM_IR_OPCODE_JNE:
        case VM_IR_OPCODE_JNZ:
        case VM_IR_OPCODE_JL:
        case VM_IR_OPCODE_JNGE:
        case VM_IR_OPCODE_JLE:
        case VM_IR_OPCODE_JNG:
        case VM_IR_OPCODE_JG:
        case VM_IR_OPCODE_JNLE:
        case VM_IR_OPCODE_JGE:
        case VM_IR_OPCODE_JNL:
        case VM_IR_OPCODE_JA:
        case VM_IR_OPCODE_JNBE:
        case VM_IR_OPCODE_JAE:
        case VM_IR_OPCODE_JNB:
        case VM_IR_OPCODE_JB:
        case VM_IR_OPCODE_JNAE:
        case VM_IR_OPCODE_JBE:
        case VM_IR_OPCODE_JNA:
            return vm_exec_validate_branch_target(vm, instruction);
        case VM_IR_OPCODE_CALL:
            return vm_exec_execute_call(vm, instruction);
        case VM_IR_OPCODE_RET:
            return vm_exec_execute_ret(vm, instruction);
        case VM_IR_OPCODE_PUSH:
            return vm_exec_execute_push(vm, instruction);
        case VM_IR_OPCODE_POP:
            return vm_exec_execute_pop(vm, instruction);
        case VM_IR_OPCODE_INVOKE_CAPTURE_DWORD:
            return vm_exec_execute_invoke_capture_dword(vm, instruction);
        case VM_IR_OPCODE_INVOKE_COMMIT:
            return vm_exec_execute_invoke_commit(vm, instruction);
        case VM_IR_OPCODE_LEAVE:
            return vm_exec_execute_leave(vm, instruction);
        case VM_IR_OPCODE_MUL:
            return vm_exec_execute_mul(vm, instruction);
        case VM_IR_OPCODE_IMUL:
            return vm_exec_execute_imul(vm, instruction);
        case VM_IR_OPCODE_IMUL_IMMEDIATE:
            return vm_exec_execute_imul_immediate(vm, instruction);
        case VM_IR_OPCODE_DIV:
            return vm_exec_execute_div(vm, instruction);
        case VM_IR_OPCODE_IDIV:
            return vm_exec_execute_idiv(vm, instruction);
        case VM_IR_OPCODE_EXIT:
            return vm_exec_execute_exit(vm, instruction);
        default:
            return VM_EXEC_STATUS_INVALID_INSTRUCTION;
    }
}

bool vm_exec_instruction_index_to_pseudo_eip(size_t instruction_index, uint32_t *out_pseudo_eip) {
    uint64_t offset = 0U;
    uint64_t pseudo = 0U;

    if (out_pseudo_eip == NULL) {
        return false;
    }

    offset = (uint64_t)instruction_index * (uint64_t)VM_EXEC_PSEUDO_EIP_STRIDE;
    pseudo = (uint64_t)VM_EXEC_PSEUDO_EIP_BASE + offset;
    if (pseudo > (uint64_t)UINT32_MAX) {
        return false;
    }

    *out_pseudo_eip = (uint32_t)pseudo;
    return true;
}

bool vm_exec_pseudo_eip_to_instruction_index(uint32_t pseudo_eip, size_t instruction_count, size_t *out_instruction_index) {
    uint32_t delta = 0U;
    size_t index = 0U;

    if (out_instruction_index == NULL || pseudo_eip < VM_EXEC_PSEUDO_EIP_BASE) {
        return false;
    }

    delta = pseudo_eip - VM_EXEC_PSEUDO_EIP_BASE;
    if ((delta % VM_EXEC_PSEUDO_EIP_STRIDE) != 0U) {
        return false;
    }

    index = (size_t)(delta / VM_EXEC_PSEUDO_EIP_STRIDE);
    if (index >= instruction_count) {
        return false;
    }

    *out_instruction_index = index;
    return true;
}

bool vm_sync_display_eip(Vm *vm) {
    uint32_t pseudo_eip = VM_EXEC_PSEUDO_EIP_BASE;

    if (vm == NULL) {
        return false;
    }

    if (vm->program != NULL && vm->instruction_pointer < vm->program_count) {
        if (!vm_exec_instruction_index_to_pseudo_eip(vm->instruction_pointer, &pseudo_eip)) {
            return false;
        }
    }

    return vm_cpu_set_display_eip(&vm->cpu, pseudo_eip);
}

VmExecStatus vm_initialize_stack_pointer(Vm *vm) {
    const VmMemoryRegion *stack_region = NULL;
    uint64_t stack_limit = 0U;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    stack_region = vm_memory_get_region(&vm->memory, VM_MEMORY_REGION_STACK);
    if (stack_region == NULL || stack_region->size == 0U) {
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    stack_limit = (uint64_t)stack_region->base + (uint64_t)stack_region->size;
    if (stack_limit > (uint64_t)UINT32_MAX) {
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm->cpu.esp = (uint32_t)stack_limit;
    return VM_EXEC_STATUS_OK;
}

VmExecStatus vm_init_with_layout_policy(Vm *vm, const VmLayoutPolicy *layout_policy) {
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memset(vm, 0, sizeof(*vm));
    vm->root_ret_mode = VM_ROOT_RET_MODE_MASM32_COMPATIBLE;
    vm->procedure_fallthrough_policy = VM_PROCEDURE_FALLTHROUGH_POLICY_WARN;
    vm->entry_procedure_end_mode = VM_ENTRY_PROCEDURE_END_MODE_CODE_STREAM;
    vm->call_depth_limit = VM_DEFAULT_CALL_DEPTH_LIMIT;
    vm_cpu_init(&vm->cpu);
    if (vm_console_init(&vm->program_console) != VM_CONSOLE_STATUS_OK) {
        vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_INVALID_ARGUMENT);
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memory_status = vm_memory_init_with_layout_policy(&vm->memory, layout_policy);
    if (memory_status != VM_MEMORY_STATUS_OK) {
        vm_console_deinit(&vm->program_console);
        vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_MEMORY_ERROR);
        vm->last_diagnostic.memory_status = memory_status;
        return VM_EXEC_STATUS_MEMORY_ERROR;
    }

    {
        VmExecStatus stack_status = vm_initialize_stack_pointer(vm);
        if (stack_status != VM_EXEC_STATUS_OK) {
            vm_console_deinit(&vm->program_console);
            vm_memory_deinit(&vm->memory);
            return stack_status;
        }
    }

    (void)vm_sync_display_eip(vm);
    vm_exec_clear_delta(&vm->last_delta);
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    vm_exec_clear_procedure_fallthrough_diagnostic(&vm->last_procedure_fallthrough_diagnostic);
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
    vm->root_ret_mode = VM_ROOT_RET_MODE_MASM32_COMPATIBLE;
    vm->procedure_fallthrough_policy = VM_PROCEDURE_FALLTHROUGH_POLICY_WARN;
    vm->entry_procedure_end_mode = VM_ENTRY_PROCEDURE_END_MODE_CODE_STREAM;
    vm->call_depth_limit = VM_DEFAULT_CALL_DEPTH_LIMIT;
    vm_cpu_init(&vm->cpu);
    if (vm_console_init(&vm->program_console) != VM_CONSOLE_STATUS_OK) {
        vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_INVALID_ARGUMENT);
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memory_status = vm_memory_init(&vm->memory, memory_config);
    if (memory_status != VM_MEMORY_STATUS_OK) {
        vm_console_deinit(&vm->program_console);
        vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_MEMORY_ERROR);
        vm->last_diagnostic.memory_status = memory_status;
        return VM_EXEC_STATUS_MEMORY_ERROR;
    }

    {
        VmExecStatus stack_status = vm_initialize_stack_pointer(vm);
        if (stack_status != VM_EXEC_STATUS_OK) {
            vm_console_deinit(&vm->program_console);
            vm_memory_deinit(&vm->memory);
            return stack_status;
        }
    }

    (void)vm_sync_display_eip(vm);
    vm_exec_clear_delta(&vm->last_delta);
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    vm_exec_clear_procedure_fallthrough_diagnostic(&vm->last_procedure_fallthrough_diagnostic);
    return VM_EXEC_STATUS_OK;
}

VmExecStatus vm_configure_procedure_boundaries(Vm *vm, const VmExecProcedureBoundary *boundaries, size_t boundary_count) {
    size_t index = 0U;
    size_t selected_index = 0U;
    bool has_selected = false;

    if (vm == NULL || (boundaries == NULL && boundary_count > 0U) ||
        boundary_count > (size_t)VM_EXEC_MAX_PROCEDURE_BOUNDARIES) {
        if (vm != NULL) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        }
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm_exec_clear_procedure_runtime_metadata(vm);
    for (index = 0U; index < boundary_count; index += 1U) {
        const VmExecProcedureBoundary *boundary = &boundaries[index];
        if (!vm_exec_procedure_boundary_is_valid(vm, boundary) ||
            boundary->uses_register_count > (size_t)VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY ||
            boundary->local_count > (size_t)VM_EXEC_PROCEDURE_LOCAL_CAPACITY) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
            vm_exec_clear_procedure_runtime_metadata(vm);
            return VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
        if (boundary->is_selected_entry) {
            if (has_selected) {
                vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
                vm_exec_clear_procedure_runtime_metadata(vm);
                return VM_EXEC_STATUS_INVALID_ARGUMENT;
            }
            has_selected = true;
            selected_index = index;
        }
        vm->procedure_boundaries[index] = *boundary;
    }

    vm->procedure_boundary_count = boundary_count;
    vm->has_selected_entry_procedure = has_selected;
    vm->selected_entry_procedure_index = selected_index;
    vm->active_helper_return_count = 0U;
    vm->selected_entry_end_stop_eligible = true;
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    return VM_EXEC_STATUS_OK;
}

VmExecStatus vm_set_root_ret_mode(Vm *vm, VmRootRetMode mode) {
    if (vm == NULL ||
        (mode != VM_ROOT_RET_MODE_MASM32_COMPATIBLE && mode != VM_ROOT_RET_MODE_STRICT_CALL_FRAME)) {
        if (vm != NULL) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        }
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm->root_ret_mode = mode;
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    return VM_EXEC_STATUS_OK;
}


/// Configures the Phase 71D procedure-fallthrough diagnostic policy for a VM.
///
/// @param vm VM instance to mutate.
/// @param policy Policy to apply.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT.
VmExecStatus vm_set_procedure_fallthrough_policy(Vm *vm, VmProcedureFallthroughPolicy policy) {
    if (vm == NULL || !vm_exec_procedure_fallthrough_policy_is_valid(policy)) {
        if (vm != NULL) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        }
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm->procedure_fallthrough_policy = policy;
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    vm_exec_clear_procedure_fallthrough_diagnostic(&vm->last_procedure_fallthrough_diagnostic);
    return VM_EXEC_STATUS_OK;
}

/// Returns the most recent Phase 71D procedure-fallthrough diagnostic for a VM.
///
/// @param vm VM instance to inspect.
/// @return VM-owned diagnostic pointer, or NULL when @p vm is NULL.
const VmProcedureFallthroughDiagnostic *vm_last_procedure_fallthrough_diagnostic(const Vm *vm) {
    return vm != NULL ? &vm->last_procedure_fallthrough_diagnostic : NULL;
}


/// Configures the Phase 71E selected-entry procedure end mode for a VM.
///
/// @param vm VM instance to mutate.
/// @param mode Mode to apply.
/// @return VM_EXEC_STATUS_OK on success, or VM_EXEC_STATUS_INVALID_ARGUMENT.
VmExecStatus vm_set_entry_procedure_end_mode(Vm *vm, VmEntryProcedureEndMode mode) {
    if (vm == NULL || !vm_exec_entry_procedure_end_mode_is_valid(mode)) {
        if (vm != NULL) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        }
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm->entry_procedure_end_mode = mode;
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    return VM_EXEC_STATUS_OK;
}

VmExecStatus vm_set_call_depth_limit(Vm *vm, uint32_t limit) {
    if (vm == NULL || !vm_exec_call_depth_limit_is_valid(limit)) {
        if (vm != NULL) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        }
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    vm->call_depth_limit = limit;
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);
    return VM_EXEC_STATUS_OK;
}

uint32_t vm_call_depth_limit(const Vm *vm) {
    return vm != NULL ? vm->call_depth_limit : 0U;
}

uint32_t vm_current_call_depth(const Vm *vm) {
    if (vm == NULL) {
        return 0U;
    }
    return vm->current_call_depth > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)vm->current_call_depth;
}

const VmConsole *vm_program_console(const Vm *vm) {
    return vm != NULL ? &vm->program_console : NULL;
}

void vm_deinit(Vm *vm) {
    if (vm == NULL) {
        return;
    }

    vm_console_deinit(&vm->program_console);
    vm_memory_deinit(&vm->memory);
    vm->program = NULL;
    vm->program_count = 0U;
    vm->instruction_pointer = 0U;
    vm->instruction_count = 0U;
    vm->halted = true;
    vm_exec_clear_procedure_runtime_metadata(vm);
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
    {
        VmExecStatus stack_status = vm_initialize_stack_pointer(vm);
        if (stack_status != VM_EXEC_STATUS_OK) {
            return stack_status;
        }
    }
    (void)vm_console_reset(&vm->program_console);
    vm_memory_clear_changes(&vm->memory);
    vm->program = program;
    vm->program_count = program_count;
    vm->instruction_pointer = 0U;
    vm->instruction_count = 0U;
    vm->halted = false;
    vm_exec_clear_procedure_runtime_metadata(vm);
    (void)vm_sync_display_eip(vm);
    vm_exec_clear_delta(&vm->last_delta);
    vm_exec_clear_diagnostic(&vm->last_diagnostic, VM_EXEC_STATUS_OK);

    return VM_EXEC_STATUS_OK;
}


/// Returns whether an opcode is a Phase 64 equality conditional jump.
///
/// @param opcode Opcode to inspect.
/// @return true for JE, JZ, JNE, and JNZ.
static bool vm_exec_opcode_is_equality_conditional_jump(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_JE ||
           opcode == VM_IR_OPCODE_JZ ||
           opcode == VM_IR_OPCODE_JNE ||
           opcode == VM_IR_OPCODE_JNZ;
}

/// Returns whether an opcode is a Phase 65 signed relational conditional jump.
///
/// @param opcode Opcode to inspect.
/// @return true for signed relational conditional-jump mnemonics and aliases.
static bool vm_exec_opcode_is_signed_relational_conditional_jump(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_JL ||
           opcode == VM_IR_OPCODE_JNGE ||
           opcode == VM_IR_OPCODE_JLE ||
           opcode == VM_IR_OPCODE_JNG ||
           opcode == VM_IR_OPCODE_JG ||
           opcode == VM_IR_OPCODE_JNLE ||
           opcode == VM_IR_OPCODE_JGE ||
           opcode == VM_IR_OPCODE_JNL;
}

/// Returns whether an opcode is a Phase 66 unsigned relational conditional jump.
///
/// @param opcode Opcode to inspect.
/// @return true for unsigned relational conditional-jump mnemonics and aliases.
static bool vm_exec_opcode_is_unsigned_relational_conditional_jump(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_JA ||
           opcode == VM_IR_OPCODE_JNBE ||
           opcode == VM_IR_OPCODE_JAE ||
           opcode == VM_IR_OPCODE_JNB ||
           opcode == VM_IR_OPCODE_JB ||
           opcode == VM_IR_OPCODE_JNAE ||
           opcode == VM_IR_OPCODE_JBE ||
           opcode == VM_IR_OPCODE_JNA;
}

/// Returns whether an opcode is any implemented conditional branch.
///
/// @param opcode Opcode to inspect.
/// @return true when @p opcode is an implemented direct conditional jump.
static bool vm_exec_opcode_is_conditional_jump(VmIrOpcode opcode) {
    return vm_exec_opcode_is_equality_conditional_jump(opcode) ||
           vm_exec_opcode_is_signed_relational_conditional_jump(opcode) ||
           vm_exec_opcode_is_unsigned_relational_conditional_jump(opcode);
}

/// Evaluates whether a committed Phase 64 equality conditional jump is taken.
///
/// The caller is responsible for ensuring undefined-flag-use policy has already
/// run when such diagnostics are enabled. Native executor callers that use this
/// helper directly receive the deterministic preserved ZF bit.
///
/// @param cpu CPU state containing the ZF bit to inspect.
/// @param opcode Conditional-jump opcode.
/// @param out_taken Receives whether the branch condition is true.
/// @return true when @p opcode is a supported equality conditional jump and ZF
/// can be read.
static bool vm_exec_equality_conditional_jump_taken(const VmCpu *cpu, VmIrOpcode opcode, bool *out_taken) {
    bool zf_is_set = false;

    if (cpu == NULL || out_taken == NULL || !vm_exec_opcode_is_equality_conditional_jump(opcode)) {
        return false;
    }
    if (!vm_cpu_read_flag(cpu, VM_FLAG_ZF, &zf_is_set)) {
        return false;
    }

    if (opcode == VM_IR_OPCODE_JE || opcode == VM_IR_OPCODE_JZ) {
        *out_taken = zf_is_set;
    } else {
        *out_taken = !zf_is_set;
    }
    return true;
}

/// Evaluates whether a committed Phase 65 signed relational conditional jump is taken.
///
/// The caller is responsible for ensuring undefined-flag-use policy has already
/// run for the exact flag set consumed by @p opcode. Native executor callers
/// that use this helper directly receive deterministic preserved flag bits.
///
/// @param cpu CPU state containing ZF, SF, and OF bits to inspect.
/// @param opcode Signed relational conditional-jump opcode.
/// @param out_taken Receives whether the branch condition is true.
/// @return true when @p opcode is a supported signed relational conditional jump
/// and all required flag bits can be read.
static bool vm_exec_signed_relational_conditional_jump_taken(const VmCpu *cpu, VmIrOpcode opcode, bool *out_taken) {
    bool zf_is_set = false;
    bool sf_is_set = false;
    bool of_is_set = false;
    bool sf_differs_of = false;

    if (cpu == NULL || out_taken == NULL || !vm_exec_opcode_is_signed_relational_conditional_jump(opcode)) {
        return false;
    }
    if (!vm_cpu_read_flag(cpu, VM_FLAG_SF, &sf_is_set) ||
        !vm_cpu_read_flag(cpu, VM_FLAG_OF, &of_is_set)) {
        return false;
    }

    sf_differs_of = (sf_is_set != of_is_set);
    if (opcode == VM_IR_OPCODE_JL || opcode == VM_IR_OPCODE_JNGE) {
        *out_taken = sf_differs_of;
        return true;
    }
    if (opcode == VM_IR_OPCODE_JGE || opcode == VM_IR_OPCODE_JNL) {
        *out_taken = !sf_differs_of;
        return true;
    }

    if (!vm_cpu_read_flag(cpu, VM_FLAG_ZF, &zf_is_set)) {
        return false;
    }
    if (opcode == VM_IR_OPCODE_JLE || opcode == VM_IR_OPCODE_JNG) {
        *out_taken = zf_is_set || sf_differs_of;
        return true;
    }
    if (opcode == VM_IR_OPCODE_JG || opcode == VM_IR_OPCODE_JNLE) {
        *out_taken = !zf_is_set && !sf_differs_of;
        return true;
    }

    return false;
}

/// Evaluates whether a committed Phase 66 unsigned relational conditional jump is taken.
///
/// The caller is responsible for ensuring undefined-flag-use policy has already
/// run for the exact flag set consumed by @p opcode. Native executor callers
/// that use this helper directly receive deterministic preserved flag bits.
///
/// @param cpu CPU state containing CF and ZF bits to inspect.
/// @param opcode Unsigned relational conditional-jump opcode.
/// @param out_taken Receives whether the branch condition is true.
/// @return true when @p opcode is a supported unsigned relational conditional
/// jump and all required flag bits can be read.
static bool vm_exec_unsigned_relational_conditional_jump_taken(const VmCpu *cpu, VmIrOpcode opcode, bool *out_taken) {
    bool cf_is_set = false;
    bool zf_is_set = false;

    if (cpu == NULL || out_taken == NULL || !vm_exec_opcode_is_unsigned_relational_conditional_jump(opcode)) {
        return false;
    }
    if (!vm_cpu_read_flag(cpu, VM_FLAG_CF, &cf_is_set)) {
        return false;
    }

    if (opcode == VM_IR_OPCODE_JAE || opcode == VM_IR_OPCODE_JNB) {
        *out_taken = !cf_is_set;
        return true;
    }
    if (opcode == VM_IR_OPCODE_JB || opcode == VM_IR_OPCODE_JNAE) {
        *out_taken = cf_is_set;
        return true;
    }

    if (!vm_cpu_read_flag(cpu, VM_FLAG_ZF, &zf_is_set)) {
        return false;
    }
    if (opcode == VM_IR_OPCODE_JA || opcode == VM_IR_OPCODE_JNBE) {
        *out_taken = !cf_is_set && !zf_is_set;
        return true;
    }
    if (opcode == VM_IR_OPCODE_JBE || opcode == VM_IR_OPCODE_JNA) {
        *out_taken = cf_is_set || zf_is_set;
        return true;
    }

    return false;
}

/// Evaluates whether a committed direct conditional jump is taken.
///
/// @param cpu CPU state containing modeled flags.
/// @param opcode Conditional-jump opcode.
/// @param out_taken Receives whether the branch condition is true.
/// @return true when @p opcode is an implemented conditional jump and required
/// flag bits can be read.
static bool vm_exec_conditional_jump_taken(const VmCpu *cpu, VmIrOpcode opcode, bool *out_taken) {
    if (vm_exec_opcode_is_equality_conditional_jump(opcode)) {
        return vm_exec_equality_conditional_jump_taken(cpu, opcode, out_taken);
    }
    if (vm_exec_opcode_is_signed_relational_conditional_jump(opcode)) {
        return vm_exec_signed_relational_conditional_jump_taken(cpu, opcode, out_taken);
    }
    return vm_exec_unsigned_relational_conditional_jump_taken(cpu, opcode, out_taken);
}

VmExecStatus vm_step(Vm *vm) {
    const VmIrInstruction *instruction = NULL;
    VmCpu before_cpu;
    size_t instruction_pointer_before_step = 0U;
    bool conditional_branch_taken = false;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (vm == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (!vm_sync_display_eip(vm)) {
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL);
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    if (vm->halted) {
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_HALTED, NULL);
        return VM_EXEC_STATUS_HALTED;
    }
    if (vm_exec_should_stop_at_selected_entry_end(vm)) {
        vm->halted = true;
        vm_exec_clear_delta(&vm->last_delta);
        vm_exec_clear_procedure_fallthrough_diagnostic(&vm->last_procedure_fallthrough_diagnostic);
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_OK, NULL);
        return VM_EXEC_STATUS_OK;
    }
    if (vm->instruction_pointer >= vm->program_count) {
        const VmIrInstruction *falloff_instruction = vm->last_delta.has_instruction ? &vm->last_delta.instruction : NULL;
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_CODE_FELL_OFF_END, falloff_instruction);
        return VM_EXEC_STATUS_CODE_FELL_OFF_END;
    }

    vm_exec_clear_delta(&vm->last_delta);
    vm_memory_clear_changes(&vm->memory);
    vm_exec_clear_procedure_fallthrough_diagnostic(&vm->last_procedure_fallthrough_diagnostic);
    vm_exec_refresh_root_code_stream_state(vm);
    instruction_pointer_before_step = vm->instruction_pointer;
    instruction = &vm->program[vm->instruction_pointer];
    if (vm_exec_current_instruction_starts_uses_procedure_without_frame(vm)) {
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_UNSUPPORTED_PROC_USES_RUNTIME, instruction);
        return VM_EXEC_STATUS_UNSUPPORTED_PROC_USES_RUNTIME;
    }
    status = vm_exec_ensure_local_frame_before_current_instruction(vm, instruction);
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    before_cpu = vm->cpu;
    status = vm_exec_execute_instruction(vm, instruction);
    if (status != VM_EXEC_STATUS_OK) {
        if (status != VM_EXEC_STATUS_MEMORY_ERROR && status != VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED &&
            status != VM_EXEC_STATUS_STACK_OVERFLOW && status != VM_EXEC_STATUS_INVALID_FRAME_STATE &&
            status != VM_EXEC_STATUS_LOCAL_FRAME_ENTRY_UNSUPPORTED &&
            status != VM_EXEC_STATUS_LOCAL_OPERAND_NO_ACTIVE_FRAME) {
            vm_exec_set_diagnostic(vm, status, instruction);
        }
        return status;
    }

    if (instruction->opcode == VM_IR_OPCODE_JMP || instruction->opcode == VM_IR_OPCODE_CALL || instruction->opcode == VM_IR_OPCODE_INVOKE_COMMIT) {
        vm->instruction_pointer = (size_t)instruction->destination.immediate;
    } else if (instruction->opcode == VM_IR_OPCODE_RET) {
        /* vm_exec_execute_ret either halted a root RET or transferred to the validated pseudo-EIP target. */
    } else if (vm_exec_opcode_is_conditional_jump(instruction->opcode)) {
        if (!vm_exec_conditional_jump_taken(&vm->cpu, instruction->opcode, &conditional_branch_taken)) {
            vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_BRANCH_TARGET, instruction);
            return VM_EXEC_STATUS_INVALID_BRANCH_TARGET;
        }
        vm->instruction_pointer = conditional_branch_taken ? (size_t)instruction->destination.immediate : vm->instruction_pointer + 1U;
    } else {
        vm->instruction_pointer += 1U;
    }
    vm->instruction_count += 1U;
    vm_exec_update_entry_end_stop_eligibility_after_step(vm, instruction, instruction_pointer_before_step, conditional_branch_taken);
    if (vm_exec_should_stop_at_selected_entry_end(vm)) {
        vm->halted = true;
        vm_exec_capture_delta(vm, instruction, &before_cpu);
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_OK, instruction);
        return VM_EXEC_STATUS_OK;
    }
    status = vm_exec_apply_procedure_fallthrough(vm, instruction, instruction_pointer_before_step, conditional_branch_taken);
    if (status != VM_EXEC_STATUS_OK) {
        vm_exec_set_diagnostic(vm, status, instruction);
        return status;
    }
    if (vm->instruction_pointer < vm->program_count && !vm_sync_display_eip(vm)) {
        vm_exec_set_diagnostic(vm, VM_EXEC_STATUS_INVALID_ARGUMENT, instruction);
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
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
        case VM_EXEC_STATUS_DIVIDE_BY_ZERO:
            return "divide-by-zero";
        case VM_EXEC_STATUS_QUOTIENT_OVERFLOW:
            return "quotient-overflow";
        case VM_EXEC_STATUS_UNDEFINED_FLAG_USE:
            return "undefined-flag-use";
        case VM_EXEC_STATUS_INSTRUCTION_LIMIT_EXCEEDED:
            return "instruction-limit-exceeded";
        case VM_EXEC_STATUS_INVALID_BRANCH_TARGET:
            return "invalid-branch-target";
        case VM_EXEC_STATUS_INVALID_CALL_TARGET:
            return "invalid-call-target";
        case VM_EXEC_STATUS_INVALID_RETURN_ADDRESS:
            return "invalid-return-address";
        case VM_EXEC_STATUS_RET_STACK_CLEANUP_OUT_OF_RANGE:
            return "ret-stack-cleanup-out-of-range";
        case VM_EXEC_STATUS_PROCEDURE_FELL_THROUGH:
            return "procedure-fell-through";
        case VM_EXEC_STATUS_CODE_FELL_OFF_END:
            return "code-fell-off-end";
        case VM_EXEC_STATUS_ROOT_RET_DISALLOWED_BY_MODE:
            return "root-ret-disallowed-by-mode";
        case VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED:
            return "call-depth-exceeded";
        case VM_EXEC_STATUS_INVALID_ROOT_TERMINATION_STATE:
            return "invalid-root-termination-state";
        case VM_EXEC_STATUS_BRANCH_RUNTIME_DEFERRED:
            return "branch-runtime-deferred";
        case VM_EXEC_STATUS_UNSUPPORTED_PROC_USES_RUNTIME:
            return "unsupported-proc-uses-runtime";
        case VM_EXEC_STATUS_STACK_OVERFLOW:
            return "stack-overflow";
        case VM_EXEC_STATUS_STACK_UNDERFLOW:
            return "stack-underflow";
        case VM_EXEC_STATUS_INVALID_FRAME_STATE:
            return "invalid-frame-state";
        case VM_EXEC_STATUS_LOCAL_FRAME_ENTRY_UNSUPPORTED:
            return "local-frame-entry-unsupported";
        case VM_EXEC_STATUS_LOCAL_OPERAND_NO_ACTIVE_FRAME:
            return "local-operand-no-active-frame";
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
            vm_ir_operand_register(VM_REGISTER_EAX, 32U),
            vm_ir_operand_immediate(20U, 32U),
            "milestone4.asm",
            1U,
            "mov eax, 20",
            0U
        },
        {
            VM_IR_OPCODE_ADD,
            vm_ir_operand_register(VM_REGISTER_EAX, 32U),
            vm_ir_operand_immediate(22U, 32U),
            "milestone4.asm",
            2U,
            "add eax, 22",
            1U
        },
        {
            VM_IR_OPCODE_RET,
            vm_ir_operand_none(),
            vm_ir_operand_none(),
            "milestone4.asm",
            3U,
            "ret",
            2U
        }
    };
    const VmExecProcedureBoundary boundaries[] = {
        {.start_instruction_index = 0U, .end_instruction_index = 3U, .is_selected_entry = true, .has_executable_instruction = true, .uses_register_count = 0U, .uses_registers = {0}}
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
        status = vm_configure_procedure_boundaries(&vm, boundaries, sizeof(boundaries) / sizeof(boundaries[0]));
    }
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
