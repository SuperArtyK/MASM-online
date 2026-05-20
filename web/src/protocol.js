/*
 * @file protocol.js
 * @brief Worker message protocol for the MASM32 educational simulator.
 *
 * The protocol supports readiness, ping/pong diagnostics, and the
 * source-run request. Execution remains in the worker/Wasm path; this module
 * only validates request shape and formats structured worker responses.
 */

/** @typedef {{type: string, payload?: unknown}} WorkerRequest */
/** @typedef {{type: string, payload?: unknown}} WorkerResponse */
/** @typedef {{status: string, testValue: number | null, message?: string, sourceExecution?: string}} WasmLoadInfo */
/** @typedef {{source: string}} RunSourcePayload */
/** @typedef {{runSource?: (source: string) => unknown}} WorkerRuntime */

/** Latest user-visible MASM source-run phase announced through worker readiness. */
export const IMPLEMENTED_PHASE = 48;

/**
 * Creates the initial worker readiness response.
 *
 * @param {WasmLoadInfo} wasmLoadInfo Structured status for the Wasm artifact.
 * @returns {WorkerResponse} Structured readiness message.
 */
export function createReadyMessage(wasmLoadInfo) {
  return {
    type: "READY",
    payload: {
      wasm: wasmLoadInfo,
      wasmTestValue: wasmLoadInfo.testValue,
      phase: IMPLEMENTED_PHASE
    }
  };
}

/**
 * Creates a structured unsupported-message response.
 *
 * @param {unknown} receivedType Message type received from the UI, if any.
 * @returns {WorkerResponse} Structured error response for the UI thread.
 */
function createUnsupportedMessageError(receivedType) {
  return {
    type: "ERROR",
    payload: {
      code: "unsupported-message",
      message: "Unsupported worker message type.",
      receivedType: receivedType ?? null
    }
  };
}

/**
 * Creates a structured invalid-run-source response.
 *
 * @returns {WorkerResponse} Structured error response for invalid Run requests.
 */
function createInvalidRunSourceError() {
  return {
    type: "ERROR",
    payload: {
      code: "invalid-run-source-request",
      message: "RUN_SOURCE requires a payload with a source string."
    }
  };
}

/**
 * Creates a structured unavailable-runner response.
 *
 * @returns {WorkerResponse} Structured error response when Wasm execution is unavailable.
 */
function createRunSourceUnavailableError() {
  return {
    type: "ERROR",
    payload: {
      code: "wasm-run-source-unavailable",
      message: "The Wasm source execution export is unavailable. Rebuild the Wasm artifact after the latest C/Wasm changes."
    }
  };
}

/**
 * Adds a diagnostic when a stale generated Wasm artifact runs newer UI source.
 *
 * @param {unknown} runResult Parsed source-run result from the Wasm export.
 * @returns {unknown} The original result, with a warning message inserted when stale.
 */
function addStaleWasmDiagnosticIfNeeded(runResult) {
  if (!runResult || typeof runResult !== "object") {
    return runResult;
  }

  if (typeof runResult.phase !== "number" || runResult.phase >= IMPLEMENTED_PHASE) {
    return runResult;
  }

  const diagnostic = {
    kind: "internal-simulator-error",
    code: "stale-wasm-artifact",
    message: `The loaded Wasm artifact reports Milestone ${runResult.phase}, but the UI/source files expect Milestone ${IMPLEMENTED_PHASE}. Rebuild web/dist with the Emscripten build script.`
  };

  const messages = Array.isArray(runResult.simulatorMessages) ? runResult.simulatorMessages : [];
  return {
    ...runResult,
    simulatorMessages: [diagnostic, ...messages]
  };
}

/**
 * Handles one RUN_SOURCE request after validating the payload.
 *
 * @param {WorkerRequest} request Message received from the UI thread.
 * @param {WorkerRuntime} runtime Runtime callbacks supplied by the worker.
 * @returns {WorkerResponse} Structured response for the UI thread.
 */
function handleRunSourceRequest(request, runtime) {
  const payload = request.payload;

  if (!payload || typeof payload !== "object" || typeof payload.source !== "string") {
    return createInvalidRunSourceError();
  }

  if (!runtime || typeof runtime.runSource !== "function") {
    return createRunSourceUnavailableError();
  }

  try {
    return {
      type: "RUN_RESULT",
      payload: addStaleWasmDiagnosticIfNeeded(runtime.runSource(payload.source))
    };
  } catch (error) {
    return {
      type: "ERROR",
      payload: {
        code: "run-source-failed",
        message: "The worker failed while running source code."
      }
    };
  }
}

/**
 * Handles one worker request.
 *
 * @param {WorkerRequest} request Message received from the UI thread.
 * @param {WorkerRuntime} [runtime] Runtime callbacks supplied by the worker.
 * @returns {WorkerResponse} Structured response for the UI thread.
 */
export function handleWorkerRequest(request, runtime = {}) {
  if (!request || typeof request.type !== "string") {
    return createUnsupportedMessageError(request && Object.prototype.hasOwnProperty.call(request, "type") ? request.type : null);
  }

  if (request.type === "PING") {
    return {
      type: "PONG",
      payload: {
        receivedPayload: Object.prototype.hasOwnProperty.call(request, "payload") ? request.payload : null
      }
    };
  }

  if (request.type === "RUN_SOURCE") {
    return handleRunSourceRequest(request, runtime);
  }

  return createUnsupportedMessageError(request.type);
}
