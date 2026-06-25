/*
 * @file wasm_api.h
 * @brief WebAssembly export boundary for the MASM32 simulator C core.
 *
 * This boundary exposes deterministic APIs used by the browser worker and
 * command-line tests. Each export remains a narrow milestone slice rather than
 * a broad simulator subsystem.
 */

#ifndef MASM32_SIM_WASM_API_H
#define MASM32_SIM_WASM_API_H

#include "../core/vm_layout.h"
#include "../core/vm_diagnostic_policy.h"

#include <stdint.h>

/// Selects explicit memory validation behavior for source-run helpers.
typedef enum Masm32SimWasmMemoryValidationMode {
    /// Validate memory using region and permission checks only; this is the explicit opt-out for uninitialized-read diagnostics.
    MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY = 0,
    /// Emit non-fatal warnings for accesses that escape declared data objects.
    MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS,
    /// Stop execution when an access escapes declared data-object bounds.
    MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT,
    /// Emit non-fatal warnings when reads use uninitialized-origin bytes.
    MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS,
    /// Stop execution before reads that would use uninitialized-origin bytes.
    MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
} Masm32SimWasmMemoryValidationMode;

/// Selects the browser-facing Phase 53E memory range validation setting.
typedef enum Masm32SimWasmMemoryRangeSetting {
    /// Use only mandatory VM region, permission, and .CONST checks.
    MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY = 0,
    /// Emit non-fatal Level 2 section-capacity warnings.
    MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_WARN,
    /// Stop before mutation on Level 2 section-capacity violations.
    MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_STRICT,
    /// Emit non-fatal Level 3 section-image warnings.
    MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_WARN,
    /// Stop before mutation on Level 3 section-image violations.
    MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_STRICT,
    /// Emit non-fatal Level 4 declared-object bounds warnings.
    MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_WARN,
    /// Stop before mutation on Level 4 declared-object bounds violations.
    MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_STRICT
} Masm32SimWasmMemoryRangeSetting;

/// Selects a browser-facing Phase 53E teaching diagnostic setting.
typedef enum Masm32SimWasmTeachingDiagnosticSetting {
    /// Suppress the selected teaching diagnostic while preserving deterministic execution.
    MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_OFF = 0,
    /// Emit a non-fatal warning for the selected teaching diagnostic.
    MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
    /// Stop before mutation when the selected teaching diagnostic condition is reached.
    MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_STRICT
} Masm32SimWasmTeachingDiagnosticSetting;




/// Selects browser-facing Phase 71D procedure-fallthrough handling.
typedef enum Masm32SimWasmProcedureFallthroughPolicy {
    /// Suppress ordinary procedure-boundary fallthrough diagnostics while continuing execution.
    MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_OFF = 0,
    /// Emit a non-fatal warning for ordinary procedure-boundary fallthrough and continue execution.
    MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
    /// Stop before executing the destination procedure instruction on ordinary procedure-boundary fallthrough.
    MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_ERROR
} Masm32SimWasmProcedureFallthroughPolicy;

/// Selects browser-facing Phase 71E selected-entry procedure end handling.
typedef enum Masm32SimWasmEntryProcedureEndMode {
    /// Preserve realistic code-stream execution across the selected-entry ENDP boundary.
    MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM = 0,
    /// Stop successfully when ordinary execution reaches the selected entry procedure boundary.
    MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
} Masm32SimWasmEntryProcedureEndMode;

/// Selects browser-facing Phase 71A root-code-stream RET handling.
typedef enum Masm32SimWasmRootRetMode {
    /// Preserve default MASM32 Educational Mode behavior: root-code-stream RET terminates successfully.
    MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE = 0,
    /// Reject root-code-stream RET when no caller-supplied return address exists.
    MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME
} Masm32SimWasmRootRetMode;

/// Selects the Phase 57F register and modeled-flag startup behavior.
typedef enum Masm32SimWasmStartupRegisterFlagMode {
    /// Preserve deterministic zero startup for general-purpose registers and modeled flags.
    MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO = 0,
    /// Initialize general-purpose registers and modeled flags from startup_state_seed.
    MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM
} Masm32SimWasmStartupRegisterFlagMode;

/// Selects the Phase 57G visible-byte startup behavior for uninitialized-origin storage.
typedef enum Masm32SimWasmUninitializedStorageVisibleByteMode {
    /// Preserve deterministic zero visible bytes for `.DATA?`, `?`, and `DUP(?)` storage.
    MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO = 0,
    /// Initialize uninitialized-origin visible bytes from startup_state_seed while preserving metadata.
    MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM
} Masm32SimWasmUninitializedStorageVisibleByteMode;

/// Selects whether the Phase 57E startup-state notice is emitted.
typedef enum Masm32SimWasmStartupStateNoticeSetting {
    /// Suppress the deterministic startup-state notice while preserving startup values.
    MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF = 0,
    /// Emit the deterministic startup-state notice as a non-fatal Simulator Message.
    MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON
} Masm32SimWasmStartupStateNoticeSetting;

/// Selects whether Phase 53D compatibility notices are emitted.
typedef enum Masm32SimWasmCompatibilityNoticeSetting {
    /// Suppress accepted compatibility no-op, metadata-only, and limited-behavior notices.
    MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF = 0,
    /// Emit accepted compatibility no-op, metadata-only, and limited-behavior notices.
    MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
} Masm32SimWasmCompatibilityNoticeSetting;

/// Selects optional section-boundary validation behavior for source-run helpers.
typedef enum Masm32SimWasmSectionValidationPolicy {
    /// Do not diagnose section-boundary escapes.
    MASM32_SIM_WASM_SECTION_VALIDATION_OFF = 0,
    /// Emit non-fatal section-boundary warnings and continue execution.
    MASM32_SIM_WASM_SECTION_VALIDATION_WARN,
    /// Stop before mutating state when a section-boundary access would occur.
    MASM32_SIM_WASM_SECTION_VALIDATION_STRICT
} Masm32SimWasmSectionValidationPolicy;

/// Selects handling for shift counts whose modeled flags are architecturally undefined.
typedef enum Masm32SimWasmShiftValidationMode {
    /// Emit non-fatal warnings and preserve undefined modeled flags deterministically.
    MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS = 0,
    /// Stop execution before mutating state when a shift would make modeled flags undefined.
    MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT
} Masm32SimWasmShiftValidationMode;

/// Selects Phase 50B handling for consumers that read invalid modeled flags.
typedef enum Masm32SimWasmUndefinedFlagUsePolicy {
    /// Do not diagnose invalid flag consumption; use deterministic fallback bits.
    MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF = 0,
    /// Emit a non-fatal warning and continue using deterministic fallback bits.
    MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN,
    /// Stop before the consumer uses the invalid flag.
    MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR
} Masm32SimWasmUndefinedFlagUsePolicy;

/// Returns the Phase 0 sentinel through the WebAssembly export boundary.
///
/// @return The deterministic value returned by the C core Phase 0 helper.
int masm32_sim_wasm_test_value(void);

/// Runs the Milestone 4 hardcoded IR sample through the WebAssembly export boundary.
///
/// @return Final EAX value on success, or -1 on failure.
int masm32_sim_wasm_milestone4_hardcoded_result(void);

/// Parses and executes a MASM-like source string and returns JSON.
///
/// The browser-facing default uses teaching warning policies for
/// uninitialized reads and undefined modeled-flag consumption. Explicit helper
/// APIs can still select the region-only or undefined-flag-use off policies
/// when tests need old silent behavior.
///
/// The returned pointer refers to an internal static buffer that is overwritten
/// by each subsequent call. This is intended for single-request Web Worker use
/// through Emscripten `ccall` with a string return value.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json(const char *source);

/// Parses and executes source with an explicit Phase 59 instruction-count limit.
///
/// This source-run/test-facing helper selects the same defaults as
/// @ref masm32_sim_wasm_run_source_json while overriding the maximum number of
/// VM instructions that may execute. The limit must be positive.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param instruction_limit Positive maximum executed-VM-instruction count.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_instruction_limit(
    const char *source,
    uint32_t instruction_limit
);

/// Parses and executes source, then applies test-only synthetic Program Console output.
///
/// This helper is restricted to native tests for Program Console output-limit
/// serialization. It does not expose additional source-level printing syntax
/// and should not be used by browser UI code.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param first_output Optional first synthetic output span to commit.
/// @param second_output Optional second synthetic output span, often used to trigger a limit.
/// @param max_bytes Positive Program Console byte limit for this synthetic run.
/// @param max_lines Positive Program Console line-feed limit for this synthetic run.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_synthetic_console_output(
    const char *source,
    const char *first_output,
    const char *second_output,
    uint32_t max_bytes,
    uint32_t max_lines
);

/// Parses and executes source using diagnostics, startup settings, and an instruction limit.
///
/// This Phase 59 browser/test-facing export extends the Phase 57G settings
/// export with the source-run option named `instructionLimit`. Browser UI code
/// may continue to use the default limit when no explicit setting is supplied.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Phase 57G visible-byte startup mode.
/// @param startup_state_seed Deterministic startup-state seed.
/// @param instruction_limit Positive maximum executed-VM-instruction count.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit
);


/// Parses and executes source using diagnostics, startup settings, instruction limit, and root RET mode.
///
/// This Phase 71A browser/test-facing export extends the Phase 59 settings
/// export with the source-run option named `rootRetMode`. The default
/// `masm32-compatible` mode preserves Phase 71 root-code-stream RET
/// success; `strict-call-frame` rejects root-code-stream RET as an
/// opt-in teaching diagnostic.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Phase 57G visible-byte startup mode.
/// @param startup_state_seed Deterministic startup-state seed.
/// @param instruction_limit Positive maximum executed-VM-instruction count.
/// @param root_ret_mode Phase 71A root-code-stream RET handling mode.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode
);

/// Parses and executes source using diagnostics, startup settings, instruction limit, root RET mode, and procedure fallthrough policy.
///
/// This Phase 71D browser/test-facing export extends the Phase 71A root-RET
/// export with the source-run option named `procedureFallthroughPolicy`.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Phase 57G visible-byte startup mode.
/// @param startup_state_seed Deterministic startup-state seed.
/// @param instruction_limit Positive maximum executed-VM-instruction count.
/// @param root_ret_mode Phase 71A root-code-stream RET handling mode.
/// @param procedure_fallthrough_policy Phase 71D ordinary procedure-fallthrough diagnostic policy.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy
);

/// Parses and executes source using diagnostics, startup settings, instruction limit, root RET mode, procedure fallthrough policy, and entry procedure end mode.
///
/// This Phase 71E browser/test-facing export extends the Phase 71D procedure
/// fallthrough export with the source-run option named `entryProcedureEndMode`.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Phase 57G uninitialized-storage visible-byte mode.
/// @param startup_state_seed Deterministic startup-state seed.
/// @param instruction_limit Positive maximum executed-VM-instruction count.
/// @param root_ret_mode Phase 71A root-code-stream RET handling mode.
/// @param procedure_fallthrough_policy Phase 71D ordinary procedure-fallthrough diagnostic policy.
/// @param entry_procedure_end_mode Phase 71E selected-entry ENDP boundary behavior.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy,
    Masm32SimWasmEntryProcedureEndMode entry_procedure_end_mode
);

/// Parses and executes source using diagnostics, startup settings, instruction limit, root RET mode, procedure fallthrough policy, entry procedure end mode, and call-depth limit.
///
/// This Phase 72 browser/test-facing export extends the Phase 71E entry-end
/// export with the source-run option named `callDepthLimit`.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Phase 57G uninitialized-storage visible-byte mode.
/// @param startup_state_seed Deterministic startup-state seed.
/// @param instruction_limit Positive maximum executed-VM-instruction count.
/// @param root_ret_mode Phase 71A root-code-stream RET handling mode.
/// @param procedure_fallthrough_policy Phase 71D ordinary procedure-fallthrough diagnostic policy.
/// @param entry_procedure_end_mode Phase 71E selected-entry ENDP boundary behavior.
/// @param call_depth_limit Phase 72 direct user-procedure CALL depth limit.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy,
    Masm32SimWasmEntryProcedureEndMode entry_procedure_end_mode,
    uint32_t call_depth_limit
);

/// Parses and executes source using Phase 53E browser diagnostic settings.
///
/// This browser-facing export maps structured UI settings to already-existing
/// backend policies. It does not introduce new validation semantics; it only
/// selects existing region/object/section validation, uninitialized-read,
/// undefined-flag-use, and compatibility-notice policies.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting
);


/// Parses and executes source using diagnostics plus Phase 57F startup settings.
///
/// This compatibility export preserves the Phase 57F browser/test-facing
/// signature. Phase 57G callers that need explicit uninitialized-storage
/// visible-byte settings must use
/// @ref masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param startup_state_seed Deterministic startup-state seed.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_and_startup_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    uint32_t startup_state_seed
);

/// Parses and executes source using Phase 57G explicit startup storage settings.
///
/// This export has a Phase 57G-specific name so the browser worker can detect
/// stale Phase 57F Wasm artifacts before sending non-default uninitialized
/// storage visible-byte settings.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param memory_range_setting Browser memory range validation selection.
/// @param uninitialized_read_setting Browser uninitialized-read diagnostic selection.
/// @param undefined_flag_use_setting Browser undefined-flag-use diagnostic selection.
/// @param compatibility_notice_setting Browser compatibility-notice selection.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Phase 57G uninitialized-storage visible-byte mode.
/// @param startup_state_seed Deterministic startup-state seed.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed
);

/// Parses and executes source using automatic deterministic layout sizing.
///
/// This test/configuration-facing helper keeps the normal browser export in
/// fixed-layout mode while allowing native tests to select automatic
/// sizing and policy-driven stack/heap metadata. Passing NULL for @p base_policy
/// uses @ref vm_layout_default_policy.
/// The returned pointer refers to the same internal static buffer as
/// @ref masm32_sim_wasm_run_source_json.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param base_policy Optional policy supplying automatic layout limits/defaults.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_automatic_layout_policy(const char *source, const VmLayoutPolicy *base_policy);

/// Parses and executes source using seeded or fresh randomized layout placement.
///
/// This test/configuration-facing helper keeps the normal browser export in
/// fixed-layout mode while allowing native tests to verify randomized region
/// bases and relocation. @p randomized_mode must be
/// VM_LAYOUT_MODE_SEEDED_RANDOMIZED or VM_LAYOUT_MODE_FRESH_RANDOMIZED.
/// Passing NULL for @p base_policy uses @ref vm_layout_default_policy. The
/// returned JSON includes layout metadata for randomized runs.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param randomized_mode Randomized layout mode to select.
/// @param base_policy Optional policy supplying limits/defaults/range/seed.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_randomized_layout_policy(const char *source, VmLayoutMode randomized_mode, const VmLayoutPolicy *base_policy);

/// Parses and executes source with an explicit memory validation mode.
///
/// This test/configuration-facing helper allows native tests to select
/// allocated-object warnings from Phase 37, allocated-object strict validation
/// from Phase 38, uninitialized-read warning/strict validation from Phase 40,
/// or the explicit region-only opt-out for Phase 53C default teaching warnings.
/// The returned pointer refers to the same internal static buffer as
/// @ref masm32_sim_wasm_run_source_json.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param validation_mode Memory validation behavior to apply during execution.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_memory_validation_mode(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode
);

/// Parses and executes source with explicit section-boundary validation policies.
///
/// The normal browser export keeps Phase 53B section-capacity and section-image
/// validation off. This test/configuration-facing helper allows native tests to
/// select each policy independently without adding browser UI controls.
/// Existing memory-validation modes are still supplied through @p validation_mode.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param validation_mode Existing Level 4 object or uninitialized-read validation behavior.
/// @param capacity_policy Section-capacity validation behavior.
/// @param image_policy Section-image validation behavior.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_section_validation_modes(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy
);

/// Parses and executes source with automatic layout and explicit section validation.
///
/// This test/configuration-facing helper exists for Phase 53B capacity tests
/// that need a selected VM region larger than the parser image capacity.
/// Passing NULL for @p base_policy uses @ref vm_layout_default_policy.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param base_policy Optional policy supplying automatic layout limits/defaults.
/// @param validation_mode Existing Level 4 object or uninitialized-read validation behavior.
/// @param capacity_policy Section-capacity validation behavior.
/// @param image_policy Section-image validation behavior.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_automatic_layout_and_section_validation(
    const char *source,
    const VmLayoutPolicy *base_policy,
    Masm32SimWasmMemoryValidationMode validation_mode,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy
);

/// Parses and executes source with explicit shift-undefined-flag validation.
///
/// The normal browser export uses warning mode. This test/configuration-facing
/// helper allows native tests to verify strict shift behavior without adding
/// a browser UI setting in the same milestone.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param shift_mode Shift undefined modeled-flag validation behavior.
/// @return Pointer to the same static JSON buffer used by other source-run helpers.
const char *masm32_sim_wasm_run_source_json_with_shift_validation_mode(
    const char *source,
    Masm32SimWasmShiftValidationMode shift_mode
);

/// Parses and executes source with an explicit startup-state notice policy.
///
/// The normal browser export uses the startup-state notice default policy introduced in Phase 57E. This
/// test/configuration-facing helper allows native tests to verify notice
/// opt-out without adding a browser UI setting in this phase.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param setting Startup-state notice setting to apply.
/// @return Pointer to the same static JSON buffer used by other source-run helpers.
const char *masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(
    const char *source,
    Masm32SimWasmStartupStateNoticeSetting setting
);


/// Parses and executes source with explicit Phase 57F startup register/flag settings.
///
/// This compatibility helper preserves Phase 57F tests and callers while using
/// the Phase 57G default zero visible-byte mode for uninitialized storage.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param startup_register_flag_mode Register/flag startup mode to apply.
/// @param startup_state_seed Deterministic startup-state seed.
/// @param startup_state_notice_setting Whether startup-state notices are emitted.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
    const char *source,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    uint32_t startup_state_seed,
    Masm32SimWasmStartupStateNoticeSetting startup_state_notice_setting
);

/// Parses and executes source with explicit Phase 57F and Phase 57G startup modes.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param startup_register_flag_mode Register/flag startup mode to apply.
/// @param uninitialized_storage_visible_byte_mode Uninitialized-storage visible-byte startup mode to apply.
/// @param startup_state_seed Deterministic startup-state seed.
/// @param startup_state_notice_setting Whether startup-state notices are emitted.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_startup_modes(
    const char *source,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    Masm32SimWasmStartupStateNoticeSetting startup_state_notice_setting
);

/// Parses and executes source with explicit undefined-flag-use diagnostics.
///
/// The normal browser export uses Phase 53C warning defaults. This
/// test/configuration-facing helper allows native tests to verify off, warning,
/// and error consumer policies without adding a browser UI setting in this phase.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param policy Undefined-flag-use consumer policy.
/// @return Pointer to the same static JSON buffer used by other source-run helpers.
const char *masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
    const char *source,
    Masm32SimWasmUndefinedFlagUsePolicy policy
);


/// Parses and executes source with explicit Phase 57J `.CONST ?` declaration diagnostics.
///
/// This test/configuration-facing helper verifies the `const-uninitialized-storage`
/// policy without adding browser UI controls in Phase 57J.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param policy `const-uninitialized-storage` policy value: off, warn, or error.
/// @return Pointer to the same static JSON buffer used by other source-run helpers.
const char *masm32_sim_wasm_run_source_json_with_const_uninitialized_storage_policy(
    const char *source,
    VmDiagnosticPolicyValue policy
);

/// Parses and executes source with memory validation and test-only initialization metadata.
///
/// This helper is for native tests that need to verify how warning or strict
/// validation interacts with Phase 39 write tracking. The normal browser export
/// intentionally omits this test-only metadata while using Phase 53C teaching
/// warning defaults.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @param validation_mode Memory validation behavior to apply during execution.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode
);

/// Parses and executes source while appending test-only uninitialized-origin metadata.
///
/// The normal browser source-run export intentionally omits this metadata. This
/// helper is for native tests that need to inspect Phase 39 write tracking
/// without adding uninitialized-read warnings, strict errors, or UI output.
///
/// @param source Null-terminated MASM-like source text to parse and execute.
/// @return Pointer to a null-terminated JSON result string.
const char *masm32_sim_wasm_run_source_json_with_uninitialized_metadata(const char *source);

/// Copies the simulator version string through the WebAssembly export boundary.
///
/// @param out_buffer Destination buffer owned by the caller.
/// @param out_buffer_size Size of @p out_buffer in bytes.
/// @return Zero for success, or a non-zero C API status code.
int masm32_sim_wasm_copy_version(char *out_buffer, unsigned long out_buffer_size);


#endif
