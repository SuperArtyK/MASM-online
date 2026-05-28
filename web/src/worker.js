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
    const usesNonDefaultStartupSettings = usesNonDefaultRegisterFlagStartupSettings || usesNonDefaultUninitializedStorageStartupSettings;

    if (!hasStartupStorageSettingsExport && usesNonDefaultUninitializedStorageStartupSettings) {
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

    if (!hasStartupSettingsExport && !hasStartupStorageSettingsExport && usesNonDefaultRegisterFlagStartupSettings) {
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

    if (!hasStartupStorageSettingsExport && !hasStartupSettingsExport && !hasUiSettingsExport && usesNonDefaultDiagnosticSettings) {
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

    const exportName = hasStartupStorageSettingsExport
      ? "masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings"
      : hasStartupSettingsExport
        ? "masm32_sim_wasm_run_source_json_with_ui_and_startup_settings"
        : hasUiSettingsExport
          ? "masm32_sim_wasm_run_source_json_with_ui_settings"
          : "masm32_sim_wasm_run_source_json";
    const argTypes = exportName === "masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings"
      ? ["string", "number", "number", "number", "number", "number", "number", "number"]
      : exportName === "masm32_sim_wasm_run_source_json_with_ui_and_startup_settings"
        ? ["string", "number", "number", "number", "number", "number", "number"]
        : exportName === "masm32_sim_wasm_run_source_json_with_ui_settings"
          ? ["string", "number", "number", "number", "number"]
          : ["string"];
    const args = exportName === "masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings"
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
