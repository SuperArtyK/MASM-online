/*
 * @file test_settings.mjs
 * @brief Unit tests for diagnostic and startup setting normalization.
 *
 * These tests cover browser-side serialization and mapping of UI settings to
 * the existing C/Wasm diagnostic-policy enums and Phase 57F/57G startup settings
 * without requiring DOM automation.
 */

import assert from "node:assert/strict";
import {
  COMPATIBILITY_NOTICES_OFF,
  COMPATIBILITY_NOTICES_ON,
  MEMORY_RANGE_DECLARED_OBJECT_STRICT,
  MEMORY_RANGE_DECLARED_OBJECT_WARN,
  MEMORY_RANGE_REGION_ONLY,
  MEMORY_RANGE_SECTION_CAPACITY_STRICT,
  MEMORY_RANGE_SECTION_CAPACITY_WARN,
  MEMORY_RANGE_SECTION_IMAGE_STRICT,
  MEMORY_RANGE_SECTION_IMAGE_WARN,
  ROOT_RET_MODE_MASM32_COMPATIBLE,
  ROOT_RET_MODE_STRICT_CALL_FRAME,
  PROCEDURE_FALLTHROUGH_POLICY_ERROR,
  PROCEDURE_FALLTHROUGH_POLICY_WARN,
  TEACHING_DIAGNOSTIC_OFF,
  TEACHING_DIAGNOSTIC_STRICT,
  TEACHING_DIAGNOSTIC_WARN,
  STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
  STARTUP_REGISTER_FLAG_ZERO,
  UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
  UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
  defaultDiagnosticSettings,
  diagnosticSettingsToBackendArguments,
  normalizeDiagnosticSettings,
  readDiagnosticSettingsFromControls
} from "../../web/src/settings.js";

/**
 * Runs one named settings test.
 *
 * @param {string} name Human-readable test name.
 * @param {() => void} body Test body.
 * @returns {void}
 */
function test(name, body) {
  body();
  console.log(`PASS ${name}`);
}

test("defaults match Phase 57G teaching and startup profile", () => {
  assert.deepEqual(defaultDiagnosticSettings(), {
    memoryRange: MEMORY_RANGE_REGION_ONLY,
    uninitializedReads: TEACHING_DIAGNOSTIC_WARN,
    undefinedFlagUse: TEACHING_DIAGNOSTIC_WARN,
    compatibilityNotices: COMPATIBILITY_NOTICES_ON,
    startupRegisterFlagMode: STARTUP_REGISTER_FLAG_ZERO,
    uninitializedStorageVisibleByteMode: UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
    startupStateSeed: 0,
    instructionLimit: 1000000,
    rootRetMode: ROOT_RET_MODE_MASM32_COMPATIBLE,
    procedureFallthroughPolicy: PROCEDURE_FALLTHROUGH_POLICY_WARN
  });

  const normalized = normalizeDiagnosticSettings(undefined);
  assert.equal(normalized.ok, true);
  assert.deepEqual(normalized.settings, defaultDiagnosticSettings());
  assert.deepEqual(normalized.backendSettings, {
    memoryRange: 0,
    uninitializedReads: 1,
    undefinedFlagUse: 1,
    compatibilityNotices: 1,
    startupRegisterFlagMode: 0,
    uninitializedStorageVisibleByteMode: 0,
    startupStateSeed: 0,
    instructionLimit: 1000000,
    rootRetMode: 0,
    procedureFallthroughPolicy: 1
  });
});

test("partial settings fill missing values from defaults", () => {
  const normalized = normalizeDiagnosticSettings({
    memoryRange: MEMORY_RANGE_SECTION_CAPACITY_WARN,
    compatibilityNotices: COMPATIBILITY_NOTICES_OFF
  });

  assert.equal(normalized.ok, true);
  assert.deepEqual(normalized.settings, {
    memoryRange: MEMORY_RANGE_SECTION_CAPACITY_WARN,
    uninitializedReads: TEACHING_DIAGNOSTIC_WARN,
    undefinedFlagUse: TEACHING_DIAGNOSTIC_WARN,
    compatibilityNotices: COMPATIBILITY_NOTICES_OFF,
    startupRegisterFlagMode: STARTUP_REGISTER_FLAG_ZERO,
    uninitializedStorageVisibleByteMode: UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
    startupStateSeed: 0,
    instructionLimit: 1000000,
    rootRetMode: ROOT_RET_MODE_MASM32_COMPATIBLE,
    procedureFallthroughPolicy: PROCEDURE_FALLTHROUGH_POLICY_WARN
  });
  assert.deepEqual(normalized.backendSettings, {
    memoryRange: 1,
    uninitializedReads: 1,
    undefinedFlagUse: 1,
    compatibilityNotices: 0,
    startupRegisterFlagMode: 0,
    uninitializedStorageVisibleByteMode: 0,
    startupStateSeed: 0,
    instructionLimit: 1000000,
    rootRetMode: 0,
    procedureFallthroughPolicy: 1
  });
});

test("all Phase 53E memory range settings map to backend enum values", () => {
  const expectations = [
    [MEMORY_RANGE_REGION_ONLY, 0],
    [MEMORY_RANGE_SECTION_CAPACITY_WARN, 1],
    [MEMORY_RANGE_SECTION_CAPACITY_STRICT, 2],
    [MEMORY_RANGE_SECTION_IMAGE_WARN, 3],
    [MEMORY_RANGE_SECTION_IMAGE_STRICT, 4],
    [MEMORY_RANGE_DECLARED_OBJECT_WARN, 5],
    [MEMORY_RANGE_DECLARED_OBJECT_STRICT, 6]
  ];

  for (const [memoryRange, expectedBackendValue] of expectations) {
    assert.deepEqual(diagnosticSettingsToBackendArguments({
      memoryRange,
      uninitializedReads: TEACHING_DIAGNOSTIC_WARN,
      undefinedFlagUse: TEACHING_DIAGNOSTIC_WARN,
      compatibilityNotices: COMPATIBILITY_NOTICES_ON
    }), {
      memoryRange: expectedBackendValue,
      uninitializedReads: 1,
      undefinedFlagUse: 1,
      compatibilityNotices: 1,
      startupRegisterFlagMode: 0,
      uninitializedStorageVisibleByteMode: 0,
      startupStateSeed: 0,
      instructionLimit: 1000000,
      rootRetMode: 0,
      procedureFallthroughPolicy: 1
    });
  }
});

test("all teaching diagnostic and compatibility notice settings map to backend enum values", () => {
  const expectations = [
    [TEACHING_DIAGNOSTIC_OFF, TEACHING_DIAGNOSTIC_OFF, COMPATIBILITY_NOTICES_OFF, 0, 0, 0],
    [TEACHING_DIAGNOSTIC_WARN, TEACHING_DIAGNOSTIC_WARN, COMPATIBILITY_NOTICES_ON, 1, 1, 1],
    [TEACHING_DIAGNOSTIC_STRICT, TEACHING_DIAGNOSTIC_STRICT, COMPATIBILITY_NOTICES_ON, 2, 2, 1]
  ];

  for (const [uninitializedReads, undefinedFlagUse, compatibilityNotices, expectedUninitialized, expectedUndefinedFlagUse, expectedCompatibility] of expectations) {
    assert.deepEqual(diagnosticSettingsToBackendArguments({
      memoryRange: MEMORY_RANGE_REGION_ONLY,
      uninitializedReads,
      undefinedFlagUse,
      compatibilityNotices
    }), {
      memoryRange: 0,
      uninitializedReads: expectedUninitialized,
      undefinedFlagUse: expectedUndefinedFlagUse,
      compatibilityNotices: expectedCompatibility,
      startupRegisterFlagMode: 0,
      uninitializedStorageVisibleByteMode: 0,
      startupStateSeed: 0,
      instructionLimit: 1000000,
      rootRetMode: 0,
      procedureFallthroughPolicy: 1
    });
  }
});

test("Phase 57F and Phase 57G seeded startup settings map to backend enum values", () => {
  const normalized = normalizeDiagnosticSettings({
    startupRegisterFlagMode: STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
    uninitializedStorageVisibleByteMode: UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
    startupStateSeed: "4294967295"
  });

  assert.equal(normalized.ok, true);
  assert.equal(normalized.settings.startupRegisterFlagMode, STARTUP_REGISTER_FLAG_SEEDED_RANDOM);
  assert.equal(normalized.settings.uninitializedStorageVisibleByteMode, UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM);
  assert.equal(normalized.settings.startupStateSeed, 4294967295);
  assert.equal(normalized.backendSettings.startupRegisterFlagMode, 1);
  assert.equal(normalized.backendSettings.uninitializedStorageVisibleByteMode, 1);
  assert.equal(normalized.backendSettings.startupStateSeed, 4294967295);
});

test("invalid Phase 57F startup mode returns startup ui-error diagnostic", () => {
  const normalized = normalizeDiagnosticSettings({ startupRegisterFlagMode: "all-random" });
  assert.equal(normalized.ok, false);
  assert.equal(normalized.diagnostic.kind, "ui-error");
  assert.equal(normalized.diagnostic.code, "invalid-startup-setting");
  assert.equal(normalized.diagnostic.setting, "startupRegisterFlagMode");
});

test("invalid Phase 57G uninitialized-storage startup mode returns startup ui-error diagnostic", () => {
  const normalized = normalizeDiagnosticSettings({ uninitializedStorageVisibleByteMode: "fresh-random" });
  assert.equal(normalized.ok, false);
  assert.equal(normalized.diagnostic.kind, "ui-error");
  assert.equal(normalized.diagnostic.code, "invalid-startup-setting");
  assert.equal(normalized.diagnostic.setting, "uninitializedStorageVisibleByteMode");
});

test("invalid Phase 57F startup seed returns startup ui-error diagnostic", () => {
  const normalized = normalizeDiagnosticSettings({ startupStateSeed: 4294967296 });
  assert.equal(normalized.ok, false);
  assert.equal(normalized.diagnostic.kind, "ui-error");
  assert.equal(normalized.diagnostic.code, "invalid-startup-setting");
  assert.equal(normalized.diagnostic.setting, "startupStateSeed");
});

test("invalid Phase 59 instruction limit returns source-run ui-error diagnostic", () => {
  for (const instructionLimit of [0, -1, 2.5, "2", 4294967296]) {
    const normalized = normalizeDiagnosticSettings({ instructionLimit });
    assert.equal(normalized.ok, false);
    assert.equal(normalized.diagnostic.kind, "ui-error");
    assert.equal(normalized.diagnostic.code, "invalid-instruction-limit-setting");
    assert.equal(normalized.diagnostic.setting, "instructionLimit");
  }
});


test("Phase 71A root RET mode maps to backend enum values", () => {
  const normalized = normalizeDiagnosticSettings({ rootRetMode: ROOT_RET_MODE_STRICT_CALL_FRAME });

  assert.equal(normalized.ok, true);
  assert.equal(normalized.settings.rootRetMode, ROOT_RET_MODE_STRICT_CALL_FRAME);
  assert.equal(normalized.backendSettings.rootRetMode, 1);

  const invalid = normalizeDiagnosticSettings({ rootRetMode: "strict-root-ret" });
  assert.equal(invalid.ok, false);
  assert.equal(invalid.diagnostic.kind, "ui-error");
  assert.equal(invalid.diagnostic.code, "invalid-diagnostic-setting");
  assert.equal(invalid.diagnostic.setting, "rootRetMode");
});

test("Phase 71D procedure fallthrough policy maps to backend enum values", () => {
  const normalized = normalizeDiagnosticSettings({ procedureFallthroughPolicy: PROCEDURE_FALLTHROUGH_POLICY_ERROR });

  assert.equal(normalized.ok, true);
  assert.equal(normalized.settings.procedureFallthroughPolicy, PROCEDURE_FALLTHROUGH_POLICY_ERROR);
  assert.equal(normalized.backendSettings.procedureFallthroughPolicy, 2);

  const invalid = normalizeDiagnosticSettings({ procedureFallthroughPolicy: "strict" });
  assert.equal(invalid.ok, false);
  assert.equal(invalid.diagnostic.kind, "ui-error");
  assert.equal(invalid.diagnostic.code, "invalid-diagnostic-setting");
  assert.equal(invalid.diagnostic.setting, "procedureFallthroughPolicy");
});

test("invalid memory range setting returns ui-error diagnostic", () => {
  const normalized = normalizeDiagnosticSettings({ memoryRange: "future-provenance-mode" });
  assert.equal(normalized.ok, false);
  assert.equal(normalized.diagnostic.kind, "ui-error");
  assert.equal(normalized.diagnostic.code, "invalid-diagnostic-setting");
  assert.equal(normalized.diagnostic.setting, "memoryRange");
  assert.match(normalized.diagnostic.message, /Invalid diagnostic setting 'memoryRange'/);
});

test("non-object settings payload returns ui-error diagnostic", () => {
  const normalized = normalizeDiagnosticSettings("warn");
  assert.equal(normalized.ok, false);
  assert.equal(normalized.diagnostic.kind, "ui-error");
  assert.equal(normalized.diagnostic.setting, "diagnosticSettings");
});


test("collapsed Diagnostic settings controls still produce RUN_SOURCE settings", () => {
  const hiddenBody = { hidden: true };
  const memoryRangeControl = { value: MEMORY_RANGE_DECLARED_OBJECT_WARN };
  const uninitializedReadsControl = { value: TEACHING_DIAGNOSTIC_STRICT };
  const undefinedFlagUseControl = { value: TEACHING_DIAGNOSTIC_OFF };
  const compatibilityNoticesControl = { value: COMPATIBILITY_NOTICES_OFF };
  const rootRetModeControl = { value: ROOT_RET_MODE_STRICT_CALL_FRAME };
  const procedureFallthroughPolicyControl = { value: PROCEDURE_FALLTHROUGH_POLICY_ERROR };

  const settings = readDiagnosticSettingsFromControls(
    memoryRangeControl,
    uninitializedReadsControl,
    undefinedFlagUseControl,
    compatibilityNoticesControl,
    rootRetModeControl,
    procedureFallthroughPolicyControl
  );

  assert.equal(hiddenBody.hidden, true);
  assert.deepEqual(settings, {
    memoryRange: MEMORY_RANGE_DECLARED_OBJECT_WARN,
    uninitializedReads: TEACHING_DIAGNOSTIC_STRICT,
    undefinedFlagUse: TEACHING_DIAGNOSTIC_OFF,
    compatibilityNotices: COMPATIBILITY_NOTICES_OFF,
    startupRegisterFlagMode: STARTUP_REGISTER_FLAG_ZERO,
    uninitializedStorageVisibleByteMode: UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
    startupStateSeed: 0,
    instructionLimit: 1000000,
    rootRetMode: ROOT_RET_MODE_STRICT_CALL_FRAME,
    procedureFallthroughPolicy: PROCEDURE_FALLTHROUGH_POLICY_ERROR
  });
  assert.deepEqual(diagnosticSettingsToBackendArguments(settings), {
    memoryRange: 5,
    uninitializedReads: 2,
    undefinedFlagUse: 0,
    compatibilityNotices: 0,
    startupRegisterFlagMode: 0,
    uninitializedStorageVisibleByteMode: 0,
    startupStateSeed: 0,
    instructionLimit: 1000000,
    rootRetMode: 1,
    procedureFallthroughPolicy: 2
  });
});
