/*
 * @file test_protocol.mjs
 * @brief Unit tests for the worker message protocol and source-run error path.
 *
 * The protocol tests run in Node.js and avoid browser automation while covering
 * readiness, ping/pong, source-run dispatch, and structured error responses.
 */

import assert from "node:assert/strict";
import { IMPLEMENTED_PHASE, createReadyMessage, handleWorkerRequest } from "../../web/src/protocol.js";

/**
 * Runs one named protocol test.
 *
 * @param {string} name Human-readable test name.
 * @param {() => void} body Test body.
 * @returns {void}
 */
function test(name, body) {
  body();
  console.log(`PASS ${name}`);
}

test("ready message includes implemented phase and loaded wasm status", () => {
  assert.equal(IMPLEMENTED_PHASE, 44);
  assert.deepEqual(createReadyMessage({ status: "loaded", testValue: 32, sourceExecution: "available" }), {
    type: "READY",
    payload: {
      wasm: {
        status: "loaded",
        testValue: 32,
        sourceExecution: "available"
      },
      wasmTestValue: 32,
      phase: 44
    }
  });
});

test("ready message supports not-built wasm status", () => {
  assert.deepEqual(createReadyMessage({ status: "not-built", testValue: null, sourceExecution: "unavailable", message: "missing" }), {
    type: "READY",
    payload: {
      wasm: {
        status: "not-built",
        testValue: null,
        sourceExecution: "unavailable",
        message: "missing"
      },
      wasmTestValue: null,
      phase: 44
    }
  });
});

test("PING returns PONG and echoes payload", () => {
  assert.deepEqual(handleWorkerRequest({ type: "PING", payload: { source: "unit" } }), {
    type: "PONG",
    payload: {
      receivedPayload: { source: "unit" }
    }
  });
});

test("PING without payload returns null receivedPayload", () => {
  assert.deepEqual(handleWorkerRequest({ type: "PING" }), {
    type: "PONG",
    payload: {
      receivedPayload: null
    }
  });
});

test("RUN_SOURCE dispatches to runtime and returns RUN_RESULT", () => {
  const response = handleWorkerRequest(
    { type: "RUN_SOURCE", payload: { source: ".code\nmain PROC\nEND main\n" } },
    {
      runSource(source) {
        assert.equal(source.includes(".code"), true);
        return {
          ok: true,
          registers: {
            EAX: { hex: "0000002Ah", unsigned: 42 }
          },
          simulatorMessages: []
        };
      }
    }
  );

  assert.deepEqual(response, {
    type: "RUN_RESULT",
    payload: {
      ok: true,
      registers: {
        EAX: { hex: "0000002Ah", unsigned: 42 }
      },
      simulatorMessages: []
    }
  });
});

test("RUN_SOURCE marks stale Wasm artifacts", () => {
  const response = handleWorkerRequest(
    { type: "RUN_SOURCE", payload: { source: "COUNT = 4 * 3" } },
    {
      runSource() {
        return {
          phase: 29,
          ok: false,
          simulatorMessages: [
            { kind: "assembly-error", code: "unsupported-constant-expression", message: "old parser" }
          ]
        };
      }
    }
  );

  assert.equal(response.type, "RUN_RESULT");
  assert.equal(response.payload.phase, 29);
  assert.equal(response.payload.simulatorMessages[0].code, "stale-wasm-artifact");
  assert.match(response.payload.simulatorMessages[0].message, /reports Milestone 29/);
  assert.equal(response.payload.simulatorMessages[1].code, "unsupported-constant-expression");
});

test("RUN_SOURCE without source returns structured error", () => {
  assert.deepEqual(handleWorkerRequest({ type: "RUN_SOURCE", payload: {} }, { runSource() {} }), {
    type: "ERROR",
    payload: {
      code: "invalid-run-source-request",
      message: "RUN_SOURCE requires a payload with a source string."
    }
  });
});

test("RUN_SOURCE without runtime returns unavailable error", () => {
  assert.deepEqual(handleWorkerRequest({ type: "RUN_SOURCE", payload: { source: "" } }), {
    type: "ERROR",
    payload: {
      code: "wasm-run-source-unavailable",
      message: "The Wasm source execution export is unavailable. Rebuild the Wasm artifact after the latest C/Wasm changes."
    }
  });
});

test("RUN_SOURCE runtime exception returns structured error", () => {
  assert.deepEqual(handleWorkerRequest({ type: "RUN_SOURCE", payload: { source: "" } }, { runSource() { throw new Error("boom"); } }), {
    type: "ERROR",
    payload: {
      code: "run-source-failed",
      message: "The worker failed while running source code."
    }
  });
});

test("unknown message returns structured error", () => {
  assert.deepEqual(handleWorkerRequest({ type: "RUN" }), {
    type: "ERROR",
    payload: {
      code: "unsupported-message",
      message: "Unsupported worker message type.",
      receivedType: "RUN"
    }
  });
});

test("lowercase ping does not match PING", () => {
  assert.equal(handleWorkerRequest({ type: "ping" }).type, "ERROR");
});

test("empty message returns structured error", () => {
  assert.deepEqual(handleWorkerRequest(null), {
    type: "ERROR",
    payload: {
      code: "unsupported-message",
      message: "Unsupported worker message type.",
      receivedType: null
    }
  });
});

test("missing message type returns structured error", () => {
  assert.deepEqual(handleWorkerRequest({ payload: "no type" }), {
    type: "ERROR",
    payload: {
      code: "unsupported-message",
      message: "Unsupported worker message type.",
      receivedType: null
    }
  });
});
