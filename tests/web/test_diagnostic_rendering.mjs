/*
 * @file test_diagnostic_rendering.mjs
 * @brief Native/Node diagnostic rendering harness tests for rendered Simulator Messages.
 *
 * These tests run the native C source-run JSON producer, parse the real JSON
 * emitted by the same API used by the Wasm boundary, and render Simulator
 * Messages with the browser formatter module. The harness intentionally avoids
 * duplicating UI formatting rules.
 */

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { formatSimulatorMessages } from "../../web/src/formatters.js";

/** Default native producer path built by scripts/run_tests.py before this harness runs. */
const DEFAULT_PRODUCER_PATH = "./build/tests/diagnostic_json_producer";

/** Native producer path used by this harness; override for direct local runs. */
const PRODUCER_PATH = process.env.MASM32_DIAGNOSTIC_JSON_PRODUCER ?? DEFAULT_PRODUCER_PATH;

/** @typedef {{kind?: string, code?: string, message?: string, line?: number, column?: number, byteOffset?: number, spanLength?: number}} ExpectedMessage */
/** @typedef {{source: string, reason: string}} DiagnosticFixture */

/**
 * Runs one named diagnostic-rendering test.
 *
 * @param {string} name Human-readable test name.
 * @param {() => void} body Test body.
 * @returns {void}
 */
function test(name, body) {
  body();
  console.log(`PASS ${name}`);
}

/**
 * Returns compact context for a fixture failure.
 *
 * @param {string} name Fixture name.
 * @param {string} source Fixture source text.
 * @param {string} rawJson Raw JSON emitted by the native producer.
 * @param {string} rendered Rendered Simulator Messages text.
 * @returns {string} Human-readable failure context.
 */
function fixtureContext(name, source, rawJson, rendered) {
  return [
    `Fixture: ${name}`,
    "Source:",
    source,
    "Raw JSON:",
    rawJson,
    "Rendered text:",
    rendered
  ].join("\n");
}

/**
 * Executes the native diagnostic JSON producer for a source fixture.
 *
 * @param {string} name Fixture name used in failure output.
 * @param {string} source MASM-like source text to pass on stdin.
 * @param {Record<string, string>} [extraEnv] Additional environment variables for the producer.
 * @returns {{json: any, rawJson: string, rendered: string}} Parsed and rendered result.
 */
function runFixture(name, source, extraEnv = {}) {
  const result = spawnSync(PRODUCER_PATH, { input: source, encoding: "utf8", env: { ...process.env, ...extraEnv } });
  assert.equal(result.status, 0, `producer failed for ${name}: ${result.stderr}`);
  assert.equal(result.stderr, "", `producer wrote unexpected stderr for ${name}`);
  assert.match(result.stdout, /^\{/, `producer stdout must contain raw JSON only for ${name}`);

  const rawJson = result.stdout.trimEnd();
  const json = JSON.parse(rawJson);
  const rendered = formatSimulatorMessages(json.simulatorMessages);
  return { json, rawJson, rendered };
}

/**
 * Executes the native diagnostic JSON producer using a fixture file path.
 *
 * @param {string} name Fixture name used in failure output.
 * @param {string} source MASM-like source text to write to the temporary file.
 * @returns {{json: any, rawJson: string, rendered: string}} Parsed and rendered result.
 */
function runFixtureFile(name, source) {
  const tempDirectory = mkdtempSync(join(tmpdir(), "masm32-diagnostic-fixture-"));
  const fixturePath = join(tempDirectory, `${name}.asm`);

  try {
    writeFileSync(fixturePath, source, "utf8");
    const result = spawnSync(PRODUCER_PATH, [fixturePath], { encoding: "utf8" });
    assert.equal(result.status, 0, `producer failed for ${name}: ${result.stderr}`);
    assert.equal(result.stderr, "", `producer wrote unexpected stderr for ${name}`);
    assert.match(result.stdout, /^\{/, `producer stdout must contain raw JSON only for ${name}`);

    const rawJson = result.stdout.trimEnd();
    const json = JSON.parse(rawJson);
    const rendered = formatSimulatorMessages(json.simulatorMessages);
    return { json, rawJson, rendered };
  } finally {
    rmSync(tempDirectory, { recursive: true, force: true });
  }
}

/**
 * Asserts one structured simulator message exactly matches expected fields.
 *
 * @param {object} actual Actual simulator message object.
 * @param {ExpectedMessage} expected Expected simulator message fields.
 * @returns {void}
 */
function assertMessageEquals(actual, expected) {
  assert.deepEqual(actual, expected);
}

/**
 * Asserts exact rendered Simulator Messages text with fixture context on failure.
 *
 * @param {string} name Fixture name.
 * @param {string} source Fixture source text.
 * @param {string} rawJson Raw JSON emitted by the native producer.
 * @param {string} rendered Actual rendered text.
 * @param {string} expected Expected rendered text.
 * @returns {void}
 */
function assertRenderedEquals(name, source, rawJson, rendered, expected) {
  try {
    assert.equal(rendered, expected);
  } catch (error) {
    error.message = `${error.message}\n${fixtureContext(name, source, rawJson, rendered)}\nExpected text:\n${expected}`;
    throw error;
  }
}

/**
 * Asserts no execution-complete message appears in an invalid fixture.
 *
 * @param {Array<object>} messages Simulator messages returned by source-run JSON.
 * @returns {void}
 */
function assertNoExecutionComplete(messages) {
  assert.equal(messages.some((message) => message && message.code === "execution-complete"), false);
}

/**
 * Asserts a source-run result has the expected high-level status fields.
 *
 * @param {object} json Parsed source-run JSON.
 * @param {boolean} ok Expected ok flag.
 * @param {string} status Expected status string.
 * @returns {void}
 */
function assertRunStatus(json, ok, status) {
  assert.equal(json.ok, ok);
  assert.equal(json.status, status);
}

/** @type {Record<string, DiagnosticFixture>} */
const fixtures = {
  invalidHex: {
    source: `.code
main PROC
    mov eax, 0xZZ
main ENDP
END main
`,
    reason: "Lexer invalid-hex diagnostic fixture."
  },
  unterminatedString: {
    source: `.data
msg BYTE "Hello
.code
main PROC
END main
`,
    reason: "Lexer unterminated-string diagnostic fixture."
  },
  unknownSymbol: {
    source: `.code
main PROC
    mov eax, missing
main ENDP
END main
`,
    reason: "Parser/source unknown-symbol diagnostic fixture."
  },
  unsupportedFeature: {
    source: `.code
main PROC
    INVOKE SomeProc
main ENDP
END main
`,
    reason: "Recognized deferred feature diagnostic fixture."
  },
  ambiguousMemoryWidth: {
    source: `.data
value DWORD 1

.code
main PROC
    mov eax, OFFSET value
    test [eax], 1
main ENDP
END main
`,
    reason: "MASM-invalid ambiguous memory width diagnostic fixture."
  },
  runtimeInvalidAddress: {
    source: `.code
main PROC
    mov eax, 0
    test [eax], eax
main ENDP
END main
`,
    reason: "Runtime invalid-address diagnostic fixture."
  },
  constRuntimeWrite: {
    source: `.CONST
limit DWORD 10

.code
main PROC
    mov eax, OFFSET limit
    mov DWORD PTR [eax], 20
main ENDP
END main
`,
    reason: "Runtime read-only .CONST write diagnostic fixture."
  },
  unalignedWarning: {
    source: `.data
nums DWORD 2 DUP(0)

.code
main PROC
    mov eax, DWORD PTR nums[1]
main ENDP
END main
`,
    reason: "Simulator warning plus successful completion fixture."
  },
  success: {
    source: `.code
main PROC
    mov eax, 42
main ENDP
END main
`,
    reason: "Successful execution-complete informational fixture."
  },
  automaticLayoutResourceLimit: {
    source: `.data
big BYTE 4097 DUP(0)
.code
main PROC
main ENDP
END main
`,
    reason: "Automatic layout resource-limit diagnostic fixture."
  },
  automaticLayoutStackResourceLimit: {
    source: `.stack 8192
.code
main PROC
main ENDP
END main
`,
    reason: "Automatic layout .stack resource-limit diagnostic fixture."
  },
  multiDiagnostic: {
    source: `.data
x DWORD 0

MyStruct STRUCT
    a DWORD ?
MyStruct ENDS

.code
main PROC
    INVOKE SomeProc
    .IF eax == 0
        mov ebx, 1
    .ENDIF
main ENDP
END main
`,
    reason: "Multi-diagnostic stable source-order fixture."
  },
  casemapPolicyChanged: {
    source: `OPTION CASEMAP:NONE
OPTION CASEMAP:ALL
.data
buf DWORD 1
.code
main PROC
    mov eax, bUF
main ENDP
END main
`,
    reason: "CASEMAP policy change warning plus successful execution fixture."
  },
  casemapAmbiguousSymbol: {
    source: `OPTION CASEMAP:NONE
.data
buf DWORD 1
bUF DWORD 2
OPTION CASEMAP:ALL
.code
main PROC
    mov eax, buf
main ENDP
END main
`,
    reason: "CASEMAP folded ambiguous symbol diagnostic fixture."
  },
  casemapAmbiguousEquate: {
    source: `OPTION CASEMAP:NONE
COUNT = 5
count = 6
OPTION CASEMAP:ALL
.code
main PROC
    mov eax, COUNT
main ENDP
END main
`,
    reason: "CASEMAP folded ambiguous numeric equate diagnostic fixture."
  },
  casemapInvalidValue: {
    source: `OPTION CASEMAP:LOWER
.code
main PROC
END main
`,
    reason: "Invalid OPTION CASEMAP value diagnostic fixture."
  },
  casemapNotPublic: {
    source: `OPTION CASEMAP:NOTPUBLIC
.code
main PROC
END main
`,
    reason: "Unsupported OPTION CASEMAP:NOTPUBLIC diagnostic fixture."
  }
};

/**
 * Returns one named fixture source after checking the fixture metadata.
 *
 * @param {string} name Fixture name.
 * @returns {string} Fixture source text.
 */
function fixtureSource(name) {
  const fixture = fixtures[name];
  assert.ok(fixture, `fixture must exist: ${name}`);
  assert.equal(typeof fixture.reason, "string", `fixture reason must be recorded for ${name}`);
  assert.notEqual(fixture.reason.length, 0, `fixture reason must not be empty for ${name}`);
  return fixture.source;
}

test("native producer accepts stdin and emits invalid hex JSON rendered by real formatter", () => {
  const name = "invalidHex";
  const source = fixtureSource("invalidHex");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.simulatorMessages.length, 1);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-hex-literal",
    message: "hex literal requires at least one digit",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 2
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-hex-literal line 3, column 14, byte offset 29, span length 2: hex literal requires at least one digit");
});

test("native producer accepts fixture file path for successful execution", () => {
  const name = "success";
  const source = fixtureSource("success");
  const { json, rawJson, rendered } = runFixtureFile(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("renders unterminated string diagnostic exactly", () => {
  const name = "unterminatedString";
  const source = fixtureSource("unterminatedString");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unterminated-string",
    message: "unterminated string literal",
    line: 2,
    column: 10,
    byteOffset: 15,
    spanLength: 6
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unterminated-string line 2, column 10, byte offset 15, span length 6: unterminated string literal");
});

test("renders unknown symbol diagnostic exactly", () => {
  const name = "unknownSymbol";
  const source = fixtureSource("unknownSymbol");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unknown-symbol",
    message: "Unknown data symbol.",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 7
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unknown-symbol line 3, column 14, byte offset 29, span length 7: Unknown data symbol.");
});

test("renders unsupported feature diagnostic exactly", () => {
  const name = "unsupportedFeature";
  const source = fixtureSource("unsupportedFeature");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-feature",
    message: "Unsupported feature: INVOKE is not supported yet; use CALL when available.",
    line: 3,
    column: 5,
    byteOffset: 20,
    spanLength: 6
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-feature line 3, column 5, byte offset 20, span length 6: Unsupported feature: INVOKE is not supported yet; use CALL when available.");
});

test("renders ambiguous memory width diagnostic exactly", () => {
  const name = "ambiguousMemoryWidth";
  const source = fixtureSource("ambiguousMemoryWidth");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 7,
    column: 10,
    byteOffset: 72,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 7, column 10, byte offset 72, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders runtime invalid address diagnostic exactly", () => {
  const name = "runtimeInvalidAddress";
  const source = fixtureSource("runtimeInvalidAddress");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "invalid-address",
    message: "Invalid memory read at 00000000h for 4 bytes. The address is outside the simulator's configured memory regions.",
    line: 4
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] invalid-address line 4: Invalid memory read at 00000000h for 4 bytes. The address is outside the simulator's configured memory regions.");
});

test("renders runtime CONST permission diagnostic exactly without memory change rows", () => {
  const name = "constRuntimeWrite";
  const source = fixtureSource("constRuntimeWrite");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.memoryChanges, []);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "permission-denied",
    message: "Memory write at 00600000h for 4 bytes is not permitted in .const.",
    line: 7
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] permission-denied line 7: Memory write at 00600000h for 4 bytes is not permitted in .const.");
});

test("renders unaligned warning followed by successful execution exactly", () => {
  const name = "unalignedWarning";
  const source = fixtureSource("unalignedWarning");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "unaligned-memory-access",
      message: "Unaligned DWORD memory access at 00500001h.",
      line: 6
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] unaligned-memory-access line 6: Unaligned DWORD memory access at 00500001h.\n[info] execution-complete: Execution completed successfully.");
});


test("renders automatic layout resource-limit diagnostic exactly", () => {
  const name = "automaticLayoutResourceLimit";
  const source = fixtureSource("automaticLayoutResourceLimit");
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_LAYOUT_MODE: "automatic",
    MASM32_DIAGNOSTIC_AUTO_DATA_LIMIT: "4096"
  });
  assertRunStatus(json, false, "resource-limit-exceeded");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "resource-limit-error",
      code: "resource-limit-exceeded",
      message: "Automatic layout requested .data/.DATA? region size 8192 bytes, exceeding configured limit 4096 bytes.",
      line: 2,
      column: 1,
      byteOffset: 6,
      spanLength: 3
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[resource-limit-error] resource-limit-exceeded line 2, column 1, byte offset 6, span length 3: Automatic layout requested .data/.DATA? region size 8192 bytes, exceeding configured limit 4096 bytes.");
});

test("renders automatic layout .stack resource-limit diagnostic exactly", () => {
  const name = "automaticLayoutStackResourceLimit";
  const source = fixtureSource("automaticLayoutStackResourceLimit");
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_LAYOUT_MODE: "automatic",
    MASM32_DIAGNOSTIC_AUTO_STACK_LIMIT: "4096"
  });
  assertRunStatus(json, false, "resource-limit-exceeded");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "resource-limit-error",
      code: "resource-limit-exceeded",
      message: "Automatic layout requested .stack region size 8192 bytes, exceeding configured limit 4096 bytes.",
      line: 1,
      column: 1,
      byteOffset: 0,
      spanLength: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[resource-limit-error] resource-limit-exceeded line 1, column 1, byte offset 0, span length 6: Automatic layout requested .stack region size 8192 bytes, exceeding configured limit 4096 bytes.");
});

test("renders multi-diagnostic ordering exactly without execution-complete", () => {
  const name = "multiDiagnostic";
  const source = fixtureSource("multiDiagnostic");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-feature",
      message: "Unsupported feature: STRUCT declarations are not supported yet.",
      line: 4,
      column: 10,
      byteOffset: 26,
      spanLength: 6
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-feature",
      message: "Unsupported feature: INVOKE is not supported yet; use CALL when available.",
      line: 10,
      column: 5,
      byteOffset: 82,
      spanLength: 6
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-feature",
      message: "Unsupported feature: MASM .IF high-level flow is not supported yet.",
      line: 11,
      column: 5,
      byteOffset: 102,
      spanLength: 3
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assert.equal(rendered.includes("mov ebx"), false);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-feature line 4, column 10, byte offset 26, span length 6: Unsupported feature: STRUCT declarations are not supported yet.\n[unsupported-feature] unsupported-feature line 10, column 5, byte offset 82, span length 6: Unsupported feature: INVOKE is not supported yet; use CALL when available.\n[unsupported-feature] unsupported-feature line 11, column 5, byte offset 102, span length 3: Unsupported feature: MASM .IF high-level flow is not supported yet.");
});


test("renders CASEMAP policy warning followed by successful execution exactly", () => {
  const name = "casemapPolicyChanged";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-warning",
      code: "casemap-policy-changed",
      message: "OPTION CASEMAP changed the active user-symbol case policy.",
      line: 2,
      column: 16,
      byteOffset: 35,
      spanLength: 3
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-warning] casemap-policy-changed line 2, column 16, byte offset 35, span length 3: OPTION CASEMAP changed the active user-symbol case policy.\n[info] execution-complete: Execution completed successfully.");
});

test("renders CASEMAP ambiguous symbol diagnostic exactly", () => {
  const name = "casemapAmbiguousSymbol";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-warning",
      code: "casemap-policy-changed",
      message: "OPTION CASEMAP changed the active user-symbol case policy.",
      line: 5,
      column: 16,
      byteOffset: 65,
      spanLength: 3
    },
    {
      kind: "assembly-error",
      code: "ambiguous-symbol",
      message: "Multiple data symbols match this reference under CASEMAP:ALL because their names differ only by letter case. Use OPTION CASEMAP:NONE and the exact symbol spelling, or make the symbol names distinct beyond case.",
      line: 8,
      column: 14,
      byteOffset: 98,
      spanLength: 3
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-warning] casemap-policy-changed line 5, column 16, byte offset 65, span length 3: OPTION CASEMAP changed the active user-symbol case policy.\n[assembly-error] ambiguous-symbol line 8, column 14, byte offset 98, span length 3: Multiple data symbols match this reference under CASEMAP:ALL because their names differ only by letter case. Use OPTION CASEMAP:NONE and the exact symbol spelling, or make the symbol names distinct beyond case.");
});

test("renders CASEMAP ambiguous numeric equate diagnostic exactly", () => {
  const name = "casemapAmbiguousEquate";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-warning",
      code: "casemap-policy-changed",
      message: "OPTION CASEMAP changed the active user-symbol case policy.",
      line: 4,
      column: 16,
      byteOffset: 55,
      spanLength: 3
    },
    {
      kind: "assembly-error",
      code: "ambiguous-symbol",
      message: "Multiple numeric equates match this reference under CASEMAP:ALL because their names differ only by letter case. Use OPTION CASEMAP:NONE and the exact equate spelling, or make the equate names distinct beyond case.",
      line: 7,
      column: 14,
      byteOffset: 88,
      spanLength: 5
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-warning] casemap-policy-changed line 4, column 16, byte offset 55, span length 3: OPTION CASEMAP changed the active user-symbol case policy.\n[assembly-error] ambiguous-symbol line 7, column 14, byte offset 88, span length 5: Multiple numeric equates match this reference under CASEMAP:ALL because their names differ only by letter case. Use OPTION CASEMAP:NONE and the exact equate spelling, or make the equate names distinct beyond case.");
});

test("renders invalid CASEMAP value diagnostic exactly", () => {
  const name = "casemapInvalidValue";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-error",
      code: "invalid-option-value",
      message: "Invalid OPTION CASEMAP value. Supported CASEMAP values: ALL, NONE. Recognized but unsupported: NOTPUBLIC.",
      line: 1,
      column: 16,
      byteOffset: 15,
      spanLength: 5
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-option-value line 1, column 16, byte offset 15, span length 5: Invalid OPTION CASEMAP value. Supported CASEMAP values: ALL, NONE. Recognized but unsupported: NOTPUBLIC.");
});

test("renders unsupported CASEMAP NOTPUBLIC diagnostic exactly", () => {
  const name = "casemapNotPublic";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-error",
      code: "unsupported-option",
      message: "OPTION CASEMAP:NOTPUBLIC is unsupported because public/external linkage semantics are not implemented.",
      line: 1,
      column: 16,
      byteOffset: 15,
      spanLength: 9
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-option line 1, column 16, byte offset 15, span length 9: OPTION CASEMAP:NOTPUBLIC is unsupported because public/external linkage semantics are not implemented.");
});
