/*
 * @file test_settings.mjs
 * @brief Unit tests for Phase 53E diagnostic setting normalization.
 *
 * These tests cover browser-side serialization and mapping of UI settings to
 * the existing C/Wasm diagnostic policy enums without requiring DOM automation.
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
  TEACHING_DIAGNOSTIC_OFF,
  TEACHING_DIAGNOSTIC_STRICT,
  TEACHING_DIAGNOSTIC_WARN,
  defaultDiagnosticSettings,
  diagnosticSettingsToBackendArguments,
  normalizeDiagnosticSettings
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

test("defaults match Phase 53E teaching profile", () => {
  assert.deepEqual(defaultDiagnosticSettings(), {
    memoryRange: MEMORY_RANGE_REGION_ONLY,
    uninitializedReads: TEACHING_DIAGNOSTIC_WARN,
    undefinedFlagUse: TEACHING_DIAGNOSTIC_WARN,
    compatibilityNotices: COMPATIBILITY_NOTICES_ON
  });

  const normalized = normalizeDiagnosticSettings(undefined);
  assert.equal(normalized.ok, true);
  assert.deepEqual(normalized.settings, defaultDiagnosticSettings());
  assert.deepEqual(normalized.backendSettings, {
    memoryRange: 0,
    uninitializedReads: 1,
    undefinedFlagUse: 1,
    compatibilityNotices: 1
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
    compatibilityNotices: COMPATIBILITY_NOTICES_OFF
  });
  assert.deepEqual(normalized.backendSettings, {
    memoryRange: 1,
    uninitializedReads: 1,
    undefinedFlagUse: 1,
    compatibilityNotices: 0
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
      compatibilityNotices: 1
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
      compatibilityNotices: expectedCompatibility
    });
  }
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
