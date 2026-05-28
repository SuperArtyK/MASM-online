/*
 * @file settings.js
 * @brief Browser setting normalization for the MASM32 educational simulator UI.
 *
 * Phase 53E exposes already-implemented backend validation and teaching
 * diagnostic policies through structured browser settings. Phase 57F adds
 * test/protocol-facing startup register/flag settings without adding browser
 * UI controls. This module keeps the accepted string values, defaults, and
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

/** @typedef {{memoryRange: string, uninitializedReads: string, undefinedFlagUse: string, compatibilityNotices: string, startupRegisterFlagMode: string, startupStateSeed: number}} DiagnosticSettings */
/** @typedef {{memoryRange: number, uninitializedReads: number, undefinedFlagUse: number, compatibilityNotices: number, startupRegisterFlagMode: number, startupStateSeed: number}} BackendDiagnosticSettings */

/** Default browser diagnostic and Phase 57F startup settings. */
export const DEFAULT_DIAGNOSTIC_SETTINGS = Object.freeze({
  memoryRange: MEMORY_RANGE_REGION_ONLY,
  uninitializedReads: TEACHING_DIAGNOSTIC_WARN,
  undefinedFlagUse: TEACHING_DIAGNOSTIC_WARN,
  compatibilityNotices: COMPATIBILITY_NOTICES_ON,
  startupRegisterFlagMode: STARTUP_REGISTER_FLAG_ZERO,
  startupStateSeed: 0
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
 * Reads diagnostic settings from browser select controls.
 *
 * Collapsed panels keep their controls in the DOM, so hidden presentation state
 * must not affect the settings payload sent with RUN_SOURCE messages.
 *
 * @param {{value?: string}} memoryRangeControl Memory range select control.
 * @param {{value?: string}} uninitializedReadsControl Uninitialized-read select control.
 * @param {{value?: string}} undefinedFlagUseControl Undefined-flag-use select control.
 * @param {{value?: string}} compatibilityNoticesControl Compatibility-notices select control.
 * @returns {DiagnosticSettings} Settings payload for RUN_SOURCE.
 */
export function readDiagnosticSettingsFromControls(
  memoryRangeControl,
  uninitializedReadsControl,
  undefinedFlagUseControl,
  compatibilityNoticesControl
) {
  const defaults = defaultDiagnosticSettings();
  return {
    memoryRange: memoryRangeControl.value || defaults.memoryRange,
    uninitializedReads: uninitializedReadsControl.value || defaults.uninitializedReads,
    undefinedFlagUse: undefinedFlagUseControl.value || defaults.undefinedFlagUse,
    compatibilityNotices: compatibilityNoticesControl.value || defaults.compatibilityNotices,
    startupRegisterFlagMode: defaults.startupRegisterFlagMode,
    startupStateSeed: defaults.startupStateSeed
  };
}

/**
 * Normalizes optional browser diagnostic settings.
 *
 * Missing settings use Phase 53E diagnostic defaults and Phase 57F startup
 * defaults. Invalid values produce a structured UI diagnostic intended for
 * Simulator Messages.
 *
 * @param {unknown} settings Candidate settings from a worker RUN_SOURCE payload.
 * @returns {{ok: true, settings: DiagnosticSettings, backendSettings: BackendDiagnosticSettings} | {ok: false, diagnostic: ReturnType<typeof createInvalidSettingDiagnostic> | ReturnType<typeof createInvalidStartupSettingDiagnostic>}} Normalization result.
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

  const startupStateSeed = normalizeStartupStateSeed(source);
  if (!startupStateSeed.ok) {
    return startupStateSeed;
  }

  const normalizedSettings = {
    memoryRange: memoryRange.value,
    uninitializedReads: uninitializedReads.value,
    undefinedFlagUse: undefinedFlagUse.value,
    compatibilityNotices: compatibilityNotices.value,
    startupRegisterFlagMode: startupRegisterFlagMode.value,
    startupStateSeed: startupStateSeed.value
  };

  return {
    ok: true,
    settings: normalizedSettings,
    backendSettings: diagnosticSettingsToBackendArguments(normalizedSettings)
  };
}

/**
 * Converts normalized settings to Phase 53E diagnostic and Phase 57F startup Wasm enum arguments.
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
    startupStateSeed: normalized.startupStateSeed >>> 0
  };
}
