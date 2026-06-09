/*
 * @file protocol.js
 * @brief Worker message protocol for the MASM32 educational simulator.
 *
 * The protocol supports readiness, ping/pong diagnostics, and the
 * source-run request. Execution remains in the worker/Wasm path; this module
 * only validates request shape and formats structured worker responses.
 */

import { normalizeDiagnosticSettings } from "./settings.js";

/** @typedef {{type: string, payload?: unknown}} WorkerRequest */
/** @typedef {{type: string, payload?: unknown}} WorkerResponse */
/** @typedef {{status: string, testValue: number | null, message?: string, sourceExecution?: string}} WasmLoadInfo */
/** @typedef {{source: string, diagnosticSettings?: unknown}} RunSourcePayload */
/** @typedef {{runSource?: (source: string, backendSettings: import("./settings.js").BackendDiagnosticSettings) => unknown}} WorkerRuntime */

/** Latest numeric MASM source-run phase announced through worker readiness. */
export const IMPLEMENTED_PHASE = 70;

/** Latest suffixed runtime/source-run behavior phase announced through worker readiness. */
export const IMPLEMENTED_PHASE_SUFFIX = "";

/** Full latest runtime/source-run behavior phase name announced through worker readiness. */
export const IMPLEMENTED_PHASE_NAME = "Phase 70 - RET Execution and Return Address Validation";

/** Source-run JSON output-contract identifier expected by the Phase 69C browser/protocol layer. */
export const SOURCE_RUN_OUTPUT_CONTRACT = "phase-69c-source-run-output-contract-v1";

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
      phase: IMPLEMENTED_PHASE,
      phaseSuffix: IMPLEMENTED_PHASE_SUFFIX,
      phaseName: IMPLEMENTED_PHASE_NAME,
      sourceRunOutputContract: SOURCE_RUN_OUTPUT_CONTRACT
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
 * Creates a source-run-style result for a UI settings validation failure.
 *
 * @param {unknown} diagnostic Structured UI diagnostic to render.
 * @returns {WorkerResponse} RUN_RESULT response with Simulator Messages payload.
 */
function createInvalidDiagnosticSettingsRunResult(diagnostic) {
  return {
    type: "RUN_RESULT",
    payload: {
      ok: false,
      status: "ui-error",
      simulatorMessages: [diagnostic],
      registers: {},
      memoryChanges: []
    }
  };
}

/**
 * Creates a diagnostic for stale runtime/source-run behavior metadata.
 *
 * @param {number | null} reportedPhase Runtime/source-run phase reported by the artifact.
 * @param {string} reportedSuffix Runtime/source-run phase suffix reported by the artifact.
 * @returns {{kind: string, code: string, message: string}} Structured Simulator Messages diagnostic.
 */
function createStaleRuntimePhaseDiagnostic(reportedPhase, reportedSuffix) {
  const reportedLabel = reportedPhase === null
    ? "unknown"
    : `Phase ${reportedPhase}${reportedSuffix}`;
  return {
    kind: "internal-simulator-error",
    code: "stale-wasm-artifact",
    message: `The loaded Wasm artifact reports runtime/source-run MASM behavior ${reportedLabel}, but the UI/source files expect ${IMPLEMENTED_PHASE_NAME}. Rebuild web/dist with the Emscripten build script.`
  };
}

/**
 * Creates a diagnostic for stale or missing Phase 69C output-contract metadata.
 *
 * @param {unknown} reportedContract Source-run output contract reported by the artifact.
 * @returns {{kind: string, code: string, message: string}} Structured Simulator Messages diagnostic.
 */
function createStaleOutputContractDiagnostic(reportedContract) {
  if (typeof reportedContract !== "string") {
    return {
      kind: "internal-simulator-error",
      code: "stale-wasm-output-contract",
      message: "The loaded Wasm artifact does not report the Phase 69C source-run output-contract identifier required by the current UI/source files. Rebuild web/dist with the Emscripten build script."
    };
  }

  return {
    kind: "internal-simulator-error",
    code: "stale-wasm-output-contract",
    message: `The loaded Wasm artifact reports source-run output contract "${reportedContract}", but the UI/source files expect "${SOURCE_RUN_OUTPUT_CONTRACT}". Rebuild web/dist with the Emscripten build script.`
  };
}

/**
 * Adds diagnostics when a generated Wasm artifact does not match UI expectations.
 *
 * Runtime/source-run behavior phase metadata and Phase 69C output-contract
 * metadata are checked separately so output-only stale artifacts are visible
 * without advancing the runtime behavior phase.
 *
 * @param {unknown} runResult Parsed source-run result from the Wasm export.
 * @returns {unknown} The original result, with warning messages inserted when stale.
 */
function addStaleWasmDiagnosticIfNeeded(runResult) {
  if (!runResult || typeof runResult !== "object") {
    return runResult;
  }

  const diagnostics = [];
  const reportedPhase = typeof runResult.phase === "number" ? runResult.phase : null;
  const reportedSuffix = typeof runResult.phaseSuffix === "string" ? runResult.phaseSuffix : "";
  const isCurrentPhase = reportedPhase === IMPLEMENTED_PHASE && reportedSuffix === IMPLEMENTED_PHASE_SUFFIX;
  const isNewerNumericPhase = reportedPhase !== null && reportedPhase > IMPLEMENTED_PHASE;

  if (!isCurrentPhase && !isNewerNumericPhase) {
    diagnostics.push(createStaleRuntimePhaseDiagnostic(reportedPhase, reportedSuffix));
  }

  if (runResult.sourceRunOutputContract !== SOURCE_RUN_OUTPUT_CONTRACT) {
    diagnostics.push(createStaleOutputContractDiagnostic(runResult.sourceRunOutputContract));
  }

  if (diagnostics.length === 0) {
    return runResult;
  }

  const messages = Array.isArray(runResult.simulatorMessages) ? runResult.simulatorMessages : [];
  return {
    ...runResult,
    simulatorMessages: [...diagnostics, ...messages]
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
  const normalizedSettings = payload && typeof payload === "object"
    ? normalizeDiagnosticSettings(payload.diagnosticSettings)
    : normalizeDiagnosticSettings(undefined);

  if (!payload || typeof payload !== "object" || typeof payload.source !== "string") {
    return createInvalidRunSourceError();
  }

  if (!normalizedSettings.ok) {
    return createInvalidDiagnosticSettingsRunResult(normalizedSettings.diagnostic);
  }

  if (!runtime || typeof runtime.runSource !== "function") {
    return createRunSourceUnavailableError();
  }

  try {
    return {
      type: "RUN_RESULT",
      payload: addStaleWasmDiagnosticIfNeeded(runtime.runSource(payload.source, normalizedSettings.backendSettings))
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
