/*
 * @file settings.js
 * @brief Browser setting normalization for the MASM32 educational simulator UI.
 *
 * Phase 53E exposes already-implemented backend validation and teaching
 * diagnostic policies through structured browser settings. Phases 57F and 57G
 * add test/protocol-facing startup settings, Phase 59 adds the
 * instructionLimit setting, and Phase 71A adds the optional browser-visible
 * rootRetMode teaching setting, Phase 71D adds procedureFallthroughPolicy, Phase 71E adds entryProcedureEndMode, and Phase 72 adds callDepthLimit.
 * This module keeps the accepted string values, defaults, and
 * Wasm argument mapping in one place so the main thread, worker protocol,
 * and Node tests use the same rules.
 */

/** Default memory range validation option. */
export const MEMORY_RANGE_REGION_ONLY = "region-only";
/** Section-capacity validation warning option. */
export const MEMORY_RANGE_SECTION_CAPACITY_WARN = "section-capacity-warn";
/** Section-capacity validation strict-stop option. */
export const MEMORY_RANGE_SECTION_CAPACITY_STRICT = "section-capacity-strict";
/** Section-image validation warning option. */
export const MEMORY_RANGE_SECTION_IMAGE_WARN = "section-image-warn";
/** Section-image validation strict-stop option. */
export const MEMORY_RANGE_SECTION_IMAGE_STRICT = "section-image-strict";
/** Declared-object validation warning option. */
export const MEMORY_RANGE_DECLARED_OBJECT_WARN = "declared-object-warn";
/** Declared-object validation strict-stop option. */
export const MEMORY_RANGE_DECLARED_OBJECT_STRICT = "declared-object-strict";

/** Diagnostic opt-out option. */
export const TEACHING_DIAGNOSTIC_OFF = "off";
/** Default diagnostic warning option. */
export const TEACHING_DIAGNOSTIC_WARN = "warn";
/** Diagnostic strict-stop option. */
export const TEACHING_DIAGNOSTIC_STRICT = "strict";

/** Compatibility notices enabled option. */
export const COMPATIBILITY_NOTICES_ON = "on";
/** Compatibility notices suppressed option. */
export const COMPATIBILITY_NOTICES_OFF = "off";

/** Default register/flag zero startup option. */
export const STARTUP_REGISTER_FLAG_ZERO = "zero";
/** Phase 57F deterministic seeded register/flag startup option. */
export const STARTUP_REGISTER_FLAG_SEEDED_RANDOM = "seeded-random";

/** Default uninitialized-storage visible-byte zero startup option. */
export const UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO = "zero";
/** Phase 57G deterministic seeded uninitialized-storage visible-byte startup option. */
export const UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM = "seeded-random";


/** Suppress Phase 71D ordinary procedure-fallthrough diagnostics. */
export const PROCEDURE_FALLTHROUGH_POLICY_OFF = "off";
/** Default Phase 71D ordinary procedure-fallthrough warning policy. */
export const PROCEDURE_FALLTHROUGH_POLICY_WARN = "warn";
/** Phase 71D ordinary procedure-fallthrough strict-stop policy. */
export const PROCEDURE_FALLTHROUGH_POLICY_ERROR = "error";

/** Default Phase 71E realistic selected-entry ENDP code-stream behavior. */
export const ENTRY_PROCEDURE_END_MODE_CODE_STREAM = "code-stream";
/** Phase 71E beginner-friendly selected-entry ENDP auto-stop behavior. */
export const ENTRY_PROCEDURE_END_MODE_STOP_AT_ENTRY_END = "stop-at-entry-end";

/** Default Phase 71A selected-entry root RET compatibility mode. */
export const ROOT_RET_MODE_MASM32_COMPATIBLE = "masm32-compatible";
/** Phase 71A strict selected-entry root RET teaching mode. */
export const ROOT_RET_MODE_STRICT_CALL_FRAME = "strict-call-frame";

/** Default Phase 59 source-run instruction-count limit. */
export const DEFAULT_INSTRUCTION_LIMIT = 1000000;
/** Maximum accepted Phase 59 source-run instruction-count limit. */
export const MAX_INSTRUCTION_LIMIT = 0xFFFFFFFF;
/** Default Phase 72 direct user-procedure CALL depth limit. */
export const DEFAULT_CALL_DEPTH_LIMIT = 64;
/** Minimum accepted Phase 72 direct user-procedure CALL depth limit. */
export const MIN_CALL_DEPTH_LIMIT = 1;
/** Maximum accepted Phase 72 direct user-procedure CALL depth limit. */
export const MAX_CALL_DEPTH_LIMIT = 4096;

/** @typedef {{memoryRange: string, uninitializedReads: string, undefinedFlagUse: string, compatibilityNotices: string, startupRegisterFlagMode: string, uninitializedStorageVisibleByteMode: string, startupStateSeed: number, instructionLimit: number, rootRetMode: string, procedureFallthroughPolicy: string, entryProcedureEndMode: string, callDepthLimit: number}} DiagnosticSettings */
/** @typedef {{memoryRange: number, uninitializedReads: number, undefinedFlagUse: number, compatibilityNotices: number, startupRegisterFlagMode: number, uninitializedStorageVisibleByteMode: number, startupStateSeed: number, instructionLimit: number, rootRetMode: number, procedureFallthroughPolicy: number, entryProcedureEndMode: number, callDepthLimit: number}} BackendDiagnosticSettings */

/** Default browser diagnostic and Phase 57F/57G startup settings. */
export const DEFAULT_DIAGNOSTIC_SETTINGS = Object.freeze({
  memoryRange: MEMORY_RANGE_REGION_ONLY,
  uninitializedReads: TEACHING_DIAGNOSTIC_WARN,
  undefinedFlagUse: TEACHING_DIAGNOSTIC_WARN,
  compatibilityNotices: COMPATIBILITY_NOTICES_ON,
  startupRegisterFlagMode: STARTUP_REGISTER_FLAG_ZERO,
  uninitializedStorageVisibleByteMode: UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
  startupStateSeed: 0,
  instructionLimit: DEFAULT_INSTRUCTION_LIMIT,
  rootRetMode: ROOT_RET_MODE_MASM32_COMPATIBLE,
  procedureFallthroughPolicy: PROCEDURE_FALLTHROUGH_POLICY_WARN,
  entryProcedureEndMode: ENTRY_PROCEDURE_END_MODE_CODE_STREAM,
  callDepthLimit: DEFAULT_CALL_DEPTH_LIMIT
});

/** Accepted memory range setting values. */
export const MEMORY_RANGE_VALUES = Object.freeze([
  MEMORY_RANGE_REGION_ONLY,
  MEMORY_RANGE_SECTION_CAPACITY_WARN,
  MEMORY_RANGE_SECTION_CAPACITY_STRICT,
  MEMORY_RANGE_SECTION_IMAGE_WARN,
  MEMORY_RANGE_SECTION_IMAGE_STRICT,
  MEMORY_RANGE_DECLARED_OBJECT_WARN,
  MEMORY_RANGE_DECLARED_OBJECT_STRICT
]);

/** Accepted teaching diagnostic setting values. */
export const TEACHING_DIAGNOSTIC_VALUES = Object.freeze([
  TEACHING_DIAGNOSTIC_OFF,
  TEACHING_DIAGNOSTIC_WARN,
  TEACHING_DIAGNOSTIC_STRICT
]);

/** Accepted compatibility notice setting values. */
export const COMPATIBILITY_NOTICE_VALUES = Object.freeze([
  COMPATIBILITY_NOTICES_ON,
  COMPATIBILITY_NOTICES_OFF
]);

/** Accepted Phase 57F register/flag startup setting values. */
export const STARTUP_REGISTER_FLAG_MODE_VALUES = Object.freeze([
  STARTUP_REGISTER_FLAG_ZERO,
  STARTUP_REGISTER_FLAG_SEEDED_RANDOM
]);

/** Accepted Phase 57G uninitialized-storage visible-byte startup setting values. */
export const UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE_VALUES = Object.freeze([
  UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
  UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM
]);

/** Accepted Phase 71E entry procedure end mode values. */
export const ENTRY_PROCEDURE_END_MODE_VALUES = Object.freeze([
  ENTRY_PROCEDURE_END_MODE_CODE_STREAM,
  ENTRY_PROCEDURE_END_MODE_STOP_AT_ENTRY_END
]);

/** Accepted Phase 71A root RET mode values. */
export const ROOT_RET_MODE_VALUES = Object.freeze([
  ROOT_RET_MODE_MASM32_COMPATIBLE,
  ROOT_RET_MODE_STRICT_CALL_FRAME
]);

/** Accepted Phase 71D procedure-fallthrough policy values. */
export const PROCEDURE_FALLTHROUGH_POLICY_VALUES = Object.freeze([
  PROCEDURE_FALLTHROUGH_POLICY_OFF,
  PROCEDURE_FALLTHROUGH_POLICY_WARN,
  PROCEDURE_FALLTHROUGH_POLICY_ERROR
]);

/** Phase 53E backend enum values for memory range validation. */
const BACKEND_MEMORY_RANGE = Object.freeze({
  [MEMORY_RANGE_REGION_ONLY]: 0,
  [MEMORY_RANGE_SECTION_CAPACITY_WARN]: 1,
  [MEMORY_RANGE_SECTION_CAPACITY_STRICT]: 2,
  [MEMORY_RANGE_SECTION_IMAGE_WARN]: 3,
  [MEMORY_RANGE_SECTION_IMAGE_STRICT]: 4,
  [MEMORY_RANGE_DECLARED_OBJECT_WARN]: 5,
  [MEMORY_RANGE_DECLARED_OBJECT_STRICT]: 6
});

/** Phase 53E backend enum values for teaching diagnostics. */
const BACKEND_TEACHING_DIAGNOSTIC = Object.freeze({
  [TEACHING_DIAGNOSTIC_OFF]: 0,
  [TEACHING_DIAGNOSTIC_WARN]: 1,
  [TEACHING_DIAGNOSTIC_STRICT]: 2
});

/** Phase 53E backend enum values for compatibility notices. */
const BACKEND_COMPATIBILITY_NOTICES = Object.freeze({
  [COMPATIBILITY_NOTICES_OFF]: 0,
  [COMPATIBILITY_NOTICES_ON]: 1
});

/** Phase 57F backend enum values for register/flag startup. */
const BACKEND_STARTUP_REGISTER_FLAG_MODE = Object.freeze({
  [STARTUP_REGISTER_FLAG_ZERO]: 0,
  [STARTUP_REGISTER_FLAG_SEEDED_RANDOM]: 1
});

/** Phase 57G backend enum values for uninitialized-storage visible bytes. */
const BACKEND_UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE = Object.freeze({
  [UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO]: 0,
  [UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM]: 1
});

/** Phase 71E backend enum values for entry procedure end behavior. */
const BACKEND_ENTRY_PROCEDURE_END_MODE = Object.freeze({
  [ENTRY_PROCEDURE_END_MODE_CODE_STREAM]: 0,
  [ENTRY_PROCEDURE_END_MODE_STOP_AT_ENTRY_END]: 1
});

/** Phase 71A backend enum values for root RET strictness. */
const BACKEND_ROOT_RET_MODE = Object.freeze({
  [ROOT_RET_MODE_MASM32_COMPATIBLE]: 0,
  [ROOT_RET_MODE_STRICT_CALL_FRAME]: 1
});

/** Phase 71D backend enum values for procedure-fallthrough diagnostics. */
const BACKEND_PROCEDURE_FALLTHROUGH_POLICY = Object.freeze({
  [PROCEDURE_FALLTHROUGH_POLICY_OFF]: 0,
  [PROCEDURE_FALLTHROUGH_POLICY_WARN]: 1,
  [PROCEDURE_FALLTHROUGH_POLICY_ERROR]: 2
});

/**
 * Returns a shallow copy of the default diagnostic settings.
 *
 * @returns {DiagnosticSettings} Default settings object safe for mutation by callers.
 */
export function defaultDiagnosticSettings() {
  return { ...DEFAULT_DIAGNOSTIC_SETTINGS };
}

/**
 * Creates a structured UI diagnostic for an invalid setting value.
 *
 * @param {string} field Field whose value was invalid.
 * @param {unknown} value Invalid value supplied by the caller.
 * @param {string[]} acceptedValues Accepted values for the setting.
 * @returns {{kind: string, code: string, message: string, setting: string, value: unknown, acceptedValues: string[]}} Structured UI diagnostic.
 */
function createInvalidSettingDiagnostic(field, value, acceptedValues) {
  return {
    kind: "ui-error",
    code: "invalid-diagnostic-setting",
    message: `Invalid diagnostic setting '${field}'. Accepted values: ${acceptedValues.join(", ")}.`,
    setting: field,
    value,
    acceptedValues
  };
}

/**
 * Creates a structured UI diagnostic for an invalid startup setting value.
 *
 * @param {string} field Field whose value was invalid.
 * @param {unknown} value Invalid value supplied by the caller.
 * @param {string[]} acceptedValues Accepted values for the setting.
 * @returns {{kind: string, code: string, message: string, setting: string, value: unknown, acceptedValues: string[]}} Structured UI diagnostic.
 */
function createInvalidStartupSettingDiagnostic(field, value, acceptedValues) {
  return {
    kind: "ui-error",
    code: "invalid-startup-setting",
    message: `Invalid startup setting '${field}'. Accepted values: ${acceptedValues.join(", ")}.`,
    setting: field,
    value,
    acceptedValues
  };
}

/**
 * Creates a structured UI diagnostic for an invalid Phase 59 instruction limit.
 *
 * @param {unknown} value Invalid value supplied by the caller.
 * @returns {{kind: string, code: string, message: string, setting: string, value: unknown, acceptedValues: string[]}} Structured UI diagnostic.
 */
function createInvalidInstructionLimitDiagnostic(value) {
  const acceptedValues = ["positive integer from 1 to 4294967295"];
  return {
    kind: "ui-error",
    code: "invalid-instruction-limit-setting",
    message: `Invalid source-run setting 'instructionLimit'. Accepted values: ${acceptedValues.join(", ")}.`,
    setting: "instructionLimit",
    value,
    acceptedValues
  };
}

/**
 * Creates a structured UI diagnostic for an invalid Phase 72 call-depth limit.
 *
 * @param {unknown} value Invalid value supplied by the caller.
 * @returns {{kind: string, code: string, message: string, setting: string, value: unknown, acceptedValues: string[]}} Structured UI diagnostic.
 */
function createInvalidCallDepthLimitDiagnostic(value) {
  const acceptedValues = [`${MIN_CALL_DEPTH_LIMIT}..${MAX_CALL_DEPTH_LIMIT}`];
  return {
    kind: "settings-error",
    code: "invalid-call-depth-limit",
    message: `Invalid source-run setting 'callDepthLimit' value ${String(value)}. Accepted values: ${acceptedValues.join(", ")}.`,
    setting: "callDepthLimit",
    value,
    acceptedValues
  };
}

/**
 * Validates and normalizes one setting field.
 *
 * @param {Record<string, unknown>} source Source settings object.
 * @param {string} field Field to read.
 * @param {string} defaultValue Value used when the field is omitted.
 * @param {string[]} acceptedValues Accepted string values.
 * @returns {{ok: true, value: string} | {ok: false, diagnostic: ReturnType<typeof createInvalidSettingDiagnostic>}} Normalization result.
 */
function normalizeField(source, field, defaultValue, acceptedValues) {
  const value = Object.prototype.hasOwnProperty.call(source, field) ? source[field] : defaultValue;
  if (typeof value !== "string" || !acceptedValues.includes(value)) {
    return {
      ok: false,
      diagnostic: createInvalidSettingDiagnostic(field, value, acceptedValues)
    };
  }

  return { ok: true, value };
}

/**
 * Normalizes the Phase 57F unsigned 32-bit startup seed.
 *
 * @param {Record<string, unknown>} source Source settings object.
 * @returns {{ok: true, value: number} | {ok: false, diagnostic: ReturnType<typeof createInvalidStartupSettingDiagnostic>}} Normalization result.
 */
function normalizeStartupStateSeed(source) {
  const value = Object.prototype.hasOwnProperty.call(source, "startupStateSeed")
    ? source.startupStateSeed
    : DEFAULT_DIAGNOSTIC_SETTINGS.startupStateSeed;
  const numericValue = typeof value === "string" && value.trim() !== "" ? Number(value) : value;

  if (typeof numericValue !== "number" || !Number.isInteger(numericValue) || numericValue < 0 || numericValue > 0xFFFFFFFF) {
    return {
      ok: false,
      diagnostic: createInvalidStartupSettingDiagnostic("startupStateSeed", value, ["unsigned 32-bit integer"])
    };
  }

  return { ok: true, value: numericValue >>> 0 };
}

/**
 * Normalizes the Phase 59 source-run instruction-count limit.
 *
 * @param {Record<string, unknown>} source Source settings object.
 * @returns {{ok: true, value: number} | {ok: false, diagnostic: ReturnType<typeof createInvalidInstructionLimitDiagnostic>}} Normalization result.
 */
function normalizeInstructionLimit(source) {
  const value = Object.prototype.hasOwnProperty.call(source, "instructionLimit")
    ? source.instructionLimit
    : DEFAULT_DIAGNOSTIC_SETTINGS.instructionLimit;

  if (typeof value !== "number" || !Number.isInteger(value) || value <= 0 || value > MAX_INSTRUCTION_LIMIT) {
    return {
      ok: false,
      diagnostic: createInvalidInstructionLimitDiagnostic(value)
    };
  }

  return { ok: true, value };
}

/**
 * Normalizes the Phase 72 source-run direct user-procedure CALL depth limit.
 *
 * @param {Record<string, unknown>} source Source settings object.
 * @returns {{ok: true, value: number} | {ok: false, diagnostic: ReturnType<typeof createInvalidCallDepthLimitDiagnostic>}} Normalization result.
 */
function normalizeCallDepthLimit(source) {
  const value = Object.prototype.hasOwnProperty.call(source, "callDepthLimit")
    ? source.callDepthLimit
    : DEFAULT_DIAGNOSTIC_SETTINGS.callDepthLimit;

  if (typeof value !== "number" || !Number.isInteger(value) || value < MIN_CALL_DEPTH_LIMIT || value > MAX_CALL_DEPTH_LIMIT) {
    return {
      ok: false,
      diagnostic: createInvalidCallDepthLimitDiagnostic(value)
    };
  }

  return { ok: true, value };
}

/**
 * Reads diagnostic settings from browser select controls.
 *
 * Collapsed panels keep their controls in the DOM, so hidden presentation state
 * must not affect the settings payload sent with RUN_SOURCE messages.
 *
 * @param {{value?: string}} memoryRangeControl Memory range select control.
 * @param {{value?: string}} uninitializedReadsControl Uninitialized-read select control.
 * @param {{value?: string}} undefinedFlagUseControl Undefined-flag-use select control.
 * @param {{value?: string}} compatibilityNoticesControl Compatibility-notices select control.
 * @param {{value?: string}=} rootRetModeControl Optional root RET mode select control.
 * @param {{value?: string}=} procedureFallthroughPolicyControl Optional procedure-fallthrough policy select control.
 * @param {{value?: string}=} entryProcedureEndModeControl Optional entry-procedure end mode select control.
 * @returns {DiagnosticSettings} Settings payload for RUN_SOURCE.
 */
export function readDiagnosticSettingsFromControls(
  memoryRangeControl,
  uninitializedReadsControl,
  undefinedFlagUseControl,
  compatibilityNoticesControl,
  rootRetModeControl,
  procedureFallthroughPolicyControl,
  entryProcedureEndModeControl
) {
  const defaults = defaultDiagnosticSettings();
  return {
    memoryRange: memoryRangeControl.value || defaults.memoryRange,
    uninitializedReads: uninitializedReadsControl.value || defaults.uninitializedReads,
    undefinedFlagUse: undefinedFlagUseControl.value || defaults.undefinedFlagUse,
    compatibilityNotices: compatibilityNoticesControl.value || defaults.compatibilityNotices,
    startupRegisterFlagMode: defaults.startupRegisterFlagMode,
    uninitializedStorageVisibleByteMode: defaults.uninitializedStorageVisibleByteMode,
    startupStateSeed: defaults.startupStateSeed,
    instructionLimit: defaults.instructionLimit,
    callDepthLimit: defaults.callDepthLimit,
    rootRetMode: rootRetModeControl && rootRetModeControl.value ? rootRetModeControl.value : defaults.rootRetMode,
    procedureFallthroughPolicy: procedureFallthroughPolicyControl && procedureFallthroughPolicyControl.value ? procedureFallthroughPolicyControl.value : defaults.procedureFallthroughPolicy,
    entryProcedureEndMode: entryProcedureEndModeControl && entryProcedureEndModeControl.value ? entryProcedureEndModeControl.value : defaults.entryProcedureEndMode
  };
}

/**
 * Normalizes optional browser diagnostic settings.
 *
 * Missing settings use Phase 53E diagnostic defaults and Phase 57F/57G startup
 * defaults. Invalid values produce a structured UI diagnostic intended for
 * Simulator Messages.
 *
 * @param {unknown} settings Candidate settings from a worker RUN_SOURCE payload.
 * @returns {{ok: true, settings: DiagnosticSettings, backendSettings: BackendDiagnosticSettings} | {ok: false, diagnostic: ReturnType<typeof createInvalidSettingDiagnostic> | ReturnType<typeof createInvalidStartupSettingDiagnostic> | ReturnType<typeof createInvalidInstructionLimitDiagnostic> | ReturnType<typeof createInvalidCallDepthLimitDiagnostic>}} Normalization result.
 */
export function normalizeDiagnosticSettings(settings) {
  const source = settings === undefined || settings === null ? {} : settings;
  if (typeof source !== "object" || Array.isArray(source)) {
    return {
      ok: false,
      diagnostic: createInvalidSettingDiagnostic("diagnosticSettings", settings, ["object"])
    };
  }

  const memoryRange = normalizeField(source, "memoryRange", DEFAULT_DIAGNOSTIC_SETTINGS.memoryRange, MEMORY_RANGE_VALUES);
  if (!memoryRange.ok) {
    return memoryRange;
  }

  const uninitializedReads = normalizeField(source, "uninitializedReads", DEFAULT_DIAGNOSTIC_SETTINGS.uninitializedReads, TEACHING_DIAGNOSTIC_VALUES);
  if (!uninitializedReads.ok) {
    return uninitializedReads;
  }

  const undefinedFlagUse = normalizeField(source, "undefinedFlagUse", DEFAULT_DIAGNOSTIC_SETTINGS.undefinedFlagUse, TEACHING_DIAGNOSTIC_VALUES);
  if (!undefinedFlagUse.ok) {
    return undefinedFlagUse;
  }

  const compatibilityNotices = normalizeField(source, "compatibilityNotices", DEFAULT_DIAGNOSTIC_SETTINGS.compatibilityNotices, COMPATIBILITY_NOTICE_VALUES);
  if (!compatibilityNotices.ok) {
    return compatibilityNotices;
  }

  const startupRegisterFlagMode = normalizeField(source, "startupRegisterFlagMode", DEFAULT_DIAGNOSTIC_SETTINGS.startupRegisterFlagMode, STARTUP_REGISTER_FLAG_MODE_VALUES);
  if (!startupRegisterFlagMode.ok) {
    return {
      ok: false,
      diagnostic: createInvalidStartupSettingDiagnostic("startupRegisterFlagMode", source.startupRegisterFlagMode, STARTUP_REGISTER_FLAG_MODE_VALUES)
    };
  }

  const uninitializedStorageVisibleByteMode = normalizeField(source, "uninitializedStorageVisibleByteMode", DEFAULT_DIAGNOSTIC_SETTINGS.uninitializedStorageVisibleByteMode, UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE_VALUES);
  if (!uninitializedStorageVisibleByteMode.ok) {
    return {
      ok: false,
      diagnostic: createInvalidStartupSettingDiagnostic("uninitializedStorageVisibleByteMode", source.uninitializedStorageVisibleByteMode, UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE_VALUES)
    };
  }

  const startupStateSeed = normalizeStartupStateSeed(source);
  if (!startupStateSeed.ok) {
    return startupStateSeed;
  }

  const instructionLimit = normalizeInstructionLimit(source);
  if (!instructionLimit.ok) {
    return instructionLimit;
  }

  const callDepthLimit = normalizeCallDepthLimit(source);
  if (!callDepthLimit.ok) {
    return callDepthLimit;
  }

  const rootRetMode = normalizeField(source, "rootRetMode", DEFAULT_DIAGNOSTIC_SETTINGS.rootRetMode, ROOT_RET_MODE_VALUES);
  if (!rootRetMode.ok) {
    return rootRetMode;
  }

  const procedureFallthroughPolicy = normalizeField(source, "procedureFallthroughPolicy", DEFAULT_DIAGNOSTIC_SETTINGS.procedureFallthroughPolicy, PROCEDURE_FALLTHROUGH_POLICY_VALUES);
  if (!procedureFallthroughPolicy.ok) {
    return procedureFallthroughPolicy;
  }

  const entryProcedureEndMode = normalizeField(source, "entryProcedureEndMode", DEFAULT_DIAGNOSTIC_SETTINGS.entryProcedureEndMode, ENTRY_PROCEDURE_END_MODE_VALUES);
  if (!entryProcedureEndMode.ok) {
    return entryProcedureEndMode;
  }

  const normalizedSettings = {
    memoryRange: memoryRange.value,
    uninitializedReads: uninitializedReads.value,
    undefinedFlagUse: undefinedFlagUse.value,
    compatibilityNotices: compatibilityNotices.value,
    startupRegisterFlagMode: startupRegisterFlagMode.value,
    uninitializedStorageVisibleByteMode: uninitializedStorageVisibleByteMode.value,
    startupStateSeed: startupStateSeed.value,
    instructionLimit: instructionLimit.value,
    callDepthLimit: callDepthLimit.value,
    rootRetMode: rootRetMode.value,
    procedureFallthroughPolicy: procedureFallthroughPolicy.value,
    entryProcedureEndMode: entryProcedureEndMode.value
  };

  return {
    ok: true,
    settings: normalizedSettings,
    backendSettings: diagnosticSettingsToBackendArguments(normalizedSettings)
  };
}

/**
 * Converts normalized settings to Phase 53E diagnostic, Phase 57F/57G startup, Phase 59 instruction-limit, Phase 71A root RET, Phase 71D procedure-fallthrough, Phase 71E entry-procedure end-mode, and Phase 72 call-depth-limit Wasm arguments.
 *
 * @param {DiagnosticSettings} settings Normalized diagnostic settings.
 * @returns {BackendDiagnosticSettings} Integer enum values passed to the C/Wasm export.
 */
export function diagnosticSettingsToBackendArguments(settings) {
  const normalized = {
    ...DEFAULT_DIAGNOSTIC_SETTINGS,
    ...(settings || {})
  };

  return {
    memoryRange: BACKEND_MEMORY_RANGE[normalized.memoryRange],
    uninitializedReads: BACKEND_TEACHING_DIAGNOSTIC[normalized.uninitializedReads],
    undefinedFlagUse: BACKEND_TEACHING_DIAGNOSTIC[normalized.undefinedFlagUse],
    compatibilityNotices: BACKEND_COMPATIBILITY_NOTICES[normalized.compatibilityNotices],
    startupRegisterFlagMode: BACKEND_STARTUP_REGISTER_FLAG_MODE[normalized.startupRegisterFlagMode],
    uninitializedStorageVisibleByteMode: BACKEND_UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE[normalized.uninitializedStorageVisibleByteMode],
    startupStateSeed: normalized.startupStateSeed >>> 0,
    instructionLimit: normalized.instructionLimit >>> 0,
    rootRetMode: BACKEND_ROOT_RET_MODE[normalized.rootRetMode],
    procedureFallthroughPolicy: BACKEND_PROCEDURE_FALLTHROUGH_POLICY[normalized.procedureFallthroughPolicy],
    entryProcedureEndMode: BACKEND_ENTRY_PROCEDURE_END_MODE[normalized.entryProcedureEndMode],
    callDepthLimit: normalized.callDepthLimit >>> 0
  };
}
