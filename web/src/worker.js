/*
 * @file worker.js
 * @brief Web Worker for browser-side MASM32 simulator execution.
 *
 * The worker owns Wasm loading and source execution so parsing and
 * VM execution do not run on the browser main thread.
 */

import { createReadyMessage, handleWorkerRequest } from "./protocol.js";

/** Path to the Emscripten-generated ES module, relative to this worker module. */
const WASM_MODULE_PATH = "../dist/masm32_sim_core.js";

/** @type {{runSource?: (source: string) => unknown}} */
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
 * @returns {(source: string) => unknown} Function that runs source and returns parsed JSON.
 */
function createRunSourceFunction(moduleInstance) {
  return (source) => {
    const json = moduleInstance.ccall(
      "masm32_sim_wasm_run_source_json",
      "string",
      ["string"],
      [source]
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
