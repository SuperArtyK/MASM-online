/*
 * @file worker.js
 * @brief Web Worker for browser-side MASM32 simulator execution.
 *
 * The worker owns Wasm loading and source execution so parsing and
 * VM execution do not run on the browser main thread.
 */

import { createReadyMessage, handleWorkerRequest } from "./protocol.js";
import { defaultDiagnosticSettings, diagnosticSettingsToBackendArguments } from "./settings.js";

/** Path to the Emscripten-generated ES module, relative to this worker module. */
const WASM_MODULE_PATH = "../dist/masm32_sim_core.js";

/** @type {{runSource?: (source: string, backendSettings: import("./settings.js").BackendDiagnosticSettings) => unknown}} */
const workerRuntime = {};

/**
 * Creates a structured description of a missing Wasm artifact.
 *
 * @returns {import("./protocol.js").WasmLoadInfo} Status for an artifact that has not been built yet.
 */
function createNotBuiltWasmInfo() {
  return {
    status: "not-built",
    testValue: null,
    sourceExecution: "unavailable",
    message: "Wasm artifact not found. Run the Emscripten build script to create web/dist/masm32_sim_core.js."
  };
}

/**
 * Creates a JavaScript source-runner around the C source-run export.
 *
 * @param {object} moduleInstance Initialized Emscripten module instance.
 * @returns {(source: string, backendSettings: import("./settings.js").BackendDiagnosticSettings) => unknown} Function that runs source and returns parsed JSON.
 */
function createRunSourceFunction(moduleInstance) {
  const defaultBackendSettings = diagnosticSettingsToBackendArguments(defaultDiagnosticSettings());
  return (source, backendSettings) => {
    const settings = {
      ...defaultBackendSettings,
      ...(backendSettings || {})
    };
    const hasCallDepthSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings === "function";
    const hasEntryProcedureEndSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings === "function";
    const hasProcedureFallthroughSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings === "function";
    const hasRootRetModeSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings === "function";
    const hasInstructionLimitSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings === "function";
    const hasStartupStorageSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings === "function";
    const hasStartupSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_and_startup_settings === "function";
    const hasUiSettingsExport = typeof moduleInstance._masm32_sim_wasm_run_source_json_with_ui_settings === "function";
    const usesNonDefaultDiagnosticSettings = settings.memoryRange !== defaultBackendSettings.memoryRange ||
      settings.uninitializedReads !== defaultBackendSettings.uninitializedReads ||
      settings.undefinedFlagUse !== defaultBackendSettings.undefinedFlagUse ||
      settings.compatibilityNotices !== defaultBackendSettings.compatibilityNotices;
    const usesNonDefaultRegisterFlagStartupSettings = settings.startupRegisterFlagMode !== defaultBackendSettings.startupRegisterFlagMode ||
      settings.startupStateSeed !== defaultBackendSettings.startupStateSeed;
    const usesNonDefaultUninitializedStorageStartupSettings = settings.uninitializedStorageVisibleByteMode !== defaultBackendSettings.uninitializedStorageVisibleByteMode;
    const usesNonDefaultInstructionLimit = settings.instructionLimit !== defaultBackendSettings.instructionLimit;
    const usesNonDefaultRootRetMode = settings.rootRetMode !== defaultBackendSettings.rootRetMode;
    const usesNonDefaultProcedureFallthroughPolicy = settings.procedureFallthroughPolicy !== defaultBackendSettings.procedureFallthroughPolicy;
    const usesNonDefaultEntryProcedureEndMode = settings.entryProcedureEndMode !== defaultBackendSettings.entryProcedureEndMode;
    const usesNonDefaultCallDepthLimit = settings.callDepthLimit !== defaultBackendSettings.callDepthLimit;
    const usesNonDefaultStartupSettings = usesNonDefaultRegisterFlagStartupSettings || usesNonDefaultUninitializedStorageStartupSettings;

    if (!hasCallDepthSettingsExport && usesNonDefaultCallDepthLimit) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 72 call-depth-limit settings required by the current Phase 72 protocol. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    if (!hasEntryProcedureEndSettingsExport && !hasCallDepthSettingsExport && usesNonDefaultEntryProcedureEndMode) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 71E entry procedure end mode settings required by the current Phase 71E UI. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    if (!hasProcedureFallthroughSettingsExport && !hasEntryProcedureEndSettingsExport && !hasCallDepthSettingsExport && usesNonDefaultProcedureFallthroughPolicy) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 71D procedure fallthrough policy settings required by the current Phase 71D UI. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    if (!hasRootRetModeSettingsExport && !hasCallDepthSettingsExport && usesNonDefaultRootRetMode) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 71A root RET mode settings required by the current Phase 71A UI. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    if (!hasInstructionLimitSettingsExport && !hasRootRetModeSettingsExport && !hasProcedureFallthroughSettingsExport && !hasEntryProcedureEndSettingsExport && !hasCallDepthSettingsExport && usesNonDefaultInstructionLimit) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 59 instruction-limit settings required by the current Phase 61 UI. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    if (!hasStartupStorageSettingsExport && !hasInstructionLimitSettingsExport && !hasRootRetModeSettingsExport && !hasProcedureFallthroughSettingsExport && !hasEntryProcedureEndSettingsExport && !hasCallDepthSettingsExport && usesNonDefaultUninitializedStorageStartupSettings) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 57G uninitialized-storage startup settings. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    if (!hasStartupSettingsExport && !hasStartupStorageSettingsExport && !hasInstructionLimitSettingsExport && !hasRootRetModeSettingsExport && !hasProcedureFallthroughSettingsExport && !hasEntryProcedureEndSettingsExport && !hasCallDepthSettingsExport && usesNonDefaultRegisterFlagStartupSettings) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 57F startup register/flag settings. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    if (!hasStartupStorageSettingsExport && !hasStartupSettingsExport && !hasInstructionLimitSettingsExport && !hasRootRetModeSettingsExport && !hasProcedureFallthroughSettingsExport && !hasEntryProcedureEndSettingsExport && !hasCallDepthSettingsExport && !hasUiSettingsExport && usesNonDefaultDiagnosticSettings) {
      return {
        ok: false,
        status: "ui-error",
        simulatorMessages: [
          {
            kind: "ui-error",
            code: "stale-wasm-artifact",
            message: "The loaded Wasm artifact does not expose Phase 53E diagnostic settings. Rebuild web/dist with the Emscripten build script."
          }
        ],
        registers: {},
        memoryChanges: []
      };
    }

    const exportName = hasCallDepthSettingsExport
      ? "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings"
      : hasEntryProcedureEndSettingsExport
      ? "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings"
      : hasProcedureFallthroughSettingsExport
      ? "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings"
      : hasRootRetModeSettingsExport
      ? "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings"
      : hasInstructionLimitSettingsExport
      ? "masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings"
      : hasStartupStorageSettingsExport
        ? "masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings"
        : hasStartupSettingsExport
        ? "masm32_sim_wasm_run_source_json_with_ui_and_startup_settings"
        : hasUiSettingsExport
          ? "masm32_sim_wasm_run_source_json_with_ui_settings"
          : "masm32_sim_wasm_run_source_json";
    const argTypes = exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings"
      ? ["string", "number", "number", "number", "number", "number", "number", "number", "number", "number", "number", "number", "number"]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings"
      ? ["string", "number", "number", "number", "number", "number", "number", "number", "number", "number", "number", "number"]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings"
      ? ["string", "number", "number", "number", "number", "number", "number", "number", "number", "number", "number"]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings"
      ? ["string", "number", "number", "number", "number", "number", "number", "number", "number", "number"]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings"
      ? ["string", "number", "number", "number", "number", "number", "number", "number", "number"]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings"
        ? ["string", "number", "number", "number", "number", "number", "number", "number"]
        : exportName === "masm32_sim_wasm_run_source_json_with_ui_and_startup_settings"
        ? ["string", "number", "number", "number", "number", "number", "number"]
        : exportName === "masm32_sim_wasm_run_source_json_with_ui_settings"
          ? ["string", "number", "number", "number", "number"]
          : ["string"];
    const args = exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings"
      ? [
          source,
          settings.memoryRange,
          settings.uninitializedReads,
          settings.undefinedFlagUse,
          settings.compatibilityNotices,
          settings.startupRegisterFlagMode,
          settings.uninitializedStorageVisibleByteMode,
          settings.startupStateSeed,
          settings.instructionLimit,
          settings.rootRetMode,
          settings.procedureFallthroughPolicy,
          settings.entryProcedureEndMode,
          settings.callDepthLimit
        ]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings"
      ? [
          source,
          settings.memoryRange,
          settings.uninitializedReads,
          settings.undefinedFlagUse,
          settings.compatibilityNotices,
          settings.startupRegisterFlagMode,
          settings.uninitializedStorageVisibleByteMode,
          settings.startupStateSeed,
          settings.instructionLimit,
          settings.rootRetMode,
          settings.procedureFallthroughPolicy,
          settings.entryProcedureEndMode
        ]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings"
      ? [
          source,
          settings.memoryRange,
          settings.uninitializedReads,
          settings.undefinedFlagUse,
          settings.compatibilityNotices,
          settings.startupRegisterFlagMode,
          settings.uninitializedStorageVisibleByteMode,
          settings.startupStateSeed,
          settings.instructionLimit,
          settings.rootRetMode,
          settings.procedureFallthroughPolicy
        ]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings"
      ? [
          source,
          settings.memoryRange,
          settings.uninitializedReads,
          settings.undefinedFlagUse,
          settings.compatibilityNotices,
          settings.startupRegisterFlagMode,
          settings.uninitializedStorageVisibleByteMode,
          settings.startupStateSeed,
          settings.instructionLimit,
          settings.rootRetMode
        ]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings"
      ? [
          source,
          settings.memoryRange,
          settings.uninitializedReads,
          settings.undefinedFlagUse,
          settings.compatibilityNotices,
          settings.startupRegisterFlagMode,
          settings.uninitializedStorageVisibleByteMode,
          settings.startupStateSeed,
          settings.instructionLimit
        ]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings"
        ? [
            source,
            settings.memoryRange,
            settings.uninitializedReads,
            settings.undefinedFlagUse,
            settings.compatibilityNotices,
            settings.startupRegisterFlagMode,
            settings.uninitializedStorageVisibleByteMode,
            settings.startupStateSeed
          ]
        : exportName === "masm32_sim_wasm_run_source_json_with_ui_and_startup_settings"
        ? [
            source,
            settings.memoryRange,
            settings.uninitializedReads,
            settings.undefinedFlagUse,
            settings.compatibilityNotices,
            settings.startupRegisterFlagMode,
            settings.startupStateSeed
          ]
        : exportName === "masm32_sim_wasm_run_source_json_with_ui_settings"
        ? [
            source,
            settings.memoryRange,
            settings.uninitializedReads,
            settings.undefinedFlagUse,
            settings.compatibilityNotices
          ]
        : [source];
    const json = moduleInstance.ccall(
      exportName,
      "string",
      argTypes,
      args
    );
    return JSON.parse(json);
  };
}

/**
 * Loads the Wasm module and installs available runtime callbacks.
 *
 * @returns {Promise<import("./protocol.js").WasmLoadInfo>} Structured Wasm load status.
 */
async function loadWasmRuntime() {
  let wasmModuleFactory;

  try {
    const importedModule = await import(WASM_MODULE_PATH);
    wasmModuleFactory = importedModule.default;
  } catch (error) {
    return createNotBuiltWasmInfo();
  }

  if (typeof wasmModuleFactory !== "function") {
    return {
      status: "unavailable",
      testValue: null,
      sourceExecution: "unavailable",
      message: "Wasm artifact loaded, but it did not expose an Emscripten module factory."
    };
  }

  let moduleInstance;
  try {
    moduleInstance = await wasmModuleFactory({
      locateFile(path) {
        return new URL(`../dist/${path}`, import.meta.url).href;
      }
    });
  } catch (error) {
    return {
      status: "unavailable",
      testValue: null,
      sourceExecution: "unavailable",
      message: "Wasm artifact was found, but Emscripten module initialization failed."
    };
  }

  if (!moduleInstance || typeof moduleInstance._masm32_sim_wasm_test_value !== "function") {
    return {
      status: "unavailable",
      testValue: null,
      sourceExecution: "unavailable",
      message: "Wasm artifact loaded, but the Phase 0 test export was not found."
    };
  }

  const testValue = moduleInstance._masm32_sim_wasm_test_value();
  if (typeof moduleInstance.ccall !== "function" || typeof moduleInstance._masm32_sim_wasm_run_source_json !== "function") {
    return {
      status: "loaded",
      testValue,
      sourceExecution: "unavailable",
      message: "Wasm artifact loaded, but the source execution export was not found. Rebuild the Wasm artifact."
    };
  }

  workerRuntime.runSource = createRunSourceFunction(moduleInstance);
  return {
    status: "loaded",
    testValue,
    sourceExecution: "available"
  };
}

/**
 * Posts a structured message from the worker to the UI thread.
 *
 * @param {import("./protocol.js").WorkerResponse} response Message to send.
 * @returns {void}
 */
function postWorkerResponse(response) {
  self.postMessage(response);
}

/**
 * Initializes the worker and announces readiness.
 *
 * @returns {Promise<void>} Resolves when readiness has been posted.
 */
async function initializeWorker() {
  const wasmLoadInfo = await loadWasmRuntime();
  postWorkerResponse(createReadyMessage(wasmLoadInfo));
}

self.addEventListener("message", (event) => {
  postWorkerResponse(handleWorkerRequest(event.data, workerRuntime));
});

initializeWorker().catch(() => {
  postWorkerResponse({
    type: "ERROR",
    payload: {
      code: "worker-initialization-failed",
      message: "Worker failed to initialize."
    }
  });
});
