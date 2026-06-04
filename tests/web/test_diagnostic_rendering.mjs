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
import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { formatMemoryChanges, formatRegisters, formatSimulatorMessages } from "../../web/src/formatters.js";

/** Default native producer path built by scripts/run_tests.py before this harness runs. */
const DEFAULT_PRODUCER_PATH = "./build/tests/diagnostic_json_producer";

/**
 * Resolves the native producer path used by this harness.
 *
 * Windows clang toolchains commonly append `.exe` even when the runner passes
 * an extensionless `-o` path, so the harness probes that suffix before
 * spawning the producer.
 *
 * @returns {string} Native producer executable path.
 */
function resolveProducerPath() {
  const override = process.env.MASM32_DIAGNOSTIC_JSON_PRODUCER;
  if (override !== undefined && override.length > 0) {
    return override;
  }

  const windowsProducerPath = `${DEFAULT_PRODUCER_PATH}.exe`;
  if (process.platform === "win32") {
    if (existsSync(windowsProducerPath)) {
      return windowsProducerPath;
    }
    return windowsProducerPath;
  }

  if (existsSync(DEFAULT_PRODUCER_PATH)) {
    return DEFAULT_PRODUCER_PATH;
  }

  return DEFAULT_PRODUCER_PATH;
}

/** Native producer path used by this harness; override for direct local runs. */
const PRODUCER_PATH = resolveProducerPath();

/** Exact zero-startup notice text. */
const STARTUP_STATE_NOTICE_TEXT = "The simulator starts registers and modeled flags at 0. Uninitialized storage bytes are also zero-filled, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values.";

/** Exact Phase 57F seeded startup notice text. */
const SEEDED_STARTUP_STATE_NOTICE_TEXT = "The simulator started general-purpose registers and modeled flags from the configured deterministic seed. Uninitialized storage bytes remain zero-filled, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values.";

/** Exact Phase 57G seeded uninitialized-storage startup notice text. */
const SEEDED_UNINITIALIZED_STORAGE_NOTICE_TEXT = "The simulator starts registers and modeled flags at 0. Visible bytes for uninitialized storage were initialized from the configured deterministic seed, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values.";

/** Exact notice text when both seeded startup axes are enabled. */
const SEEDED_REGISTER_AND_UNINITIALIZED_STORAGE_NOTICE_TEXT = "The simulator started general-purpose registers, modeled flags, and visible bytes for uninitialized storage from the configured deterministic seed. Uninitialized-origin metadata is preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values.";

/** Exact rendered zero-startup notice line. */
const STARTUP_STATE_NOTICE_RENDERED = `[simulator-notice] startup-state-notice: ${STARTUP_STATE_NOTICE_TEXT}`;

/** Exact rendered Phase 57F seeded startup notice line. */
const SEEDED_STARTUP_STATE_NOTICE_RENDERED = `[simulator-notice] startup-state-notice: ${SEEDED_STARTUP_STATE_NOTICE_TEXT}`;

/** Exact rendered Phase 57G seeded uninitialized-storage startup notice line. */
const SEEDED_UNINITIALIZED_STORAGE_NOTICE_RENDERED = `[simulator-notice] startup-state-notice: ${SEEDED_UNINITIALIZED_STORAGE_NOTICE_TEXT}`;

/** Exact rendered combined seeded startup notice line. */
const SEEDED_REGISTER_AND_UNINITIALIZED_STORAGE_NOTICE_RENDERED = `[simulator-notice] startup-state-notice: ${SEEDED_REGISTER_AND_UNINITIALIZED_STORAGE_NOTICE_TEXT}`;

/** Environment variables that control the native diagnostic producer. */
const PRODUCER_CONTROL_ENV_KEYS = [
  "MASM32_DIAGNOSTIC_MEMORY_VALIDATION",
  "MASM32_DIAGNOSTIC_LAYOUT_MODE",
  "MASM32_DIAGNOSTIC_AUTO_DATA_LIMIT",
  "MASM32_DIAGNOSTIC_AUTO_STACK_LIMIT",
  "MASM32_DIAGNOSTIC_AUTO_HEAP_REQUEST",
  "MASM32_DIAGNOSTIC_AUTO_HEAP_LIMIT",
  "MASM32_DIAGNOSTIC_AUTO_TOTAL_LIMIT",
  "MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION",
  "MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION",
  "MASM32_DIAGNOSTIC_SHIFT_VALIDATION",
  "MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE",
  "MASM32_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE",
  "MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE",
  "MASM32_DIAGNOSTIC_STARTUP_REGISTER_FLAG_MODE",
  "MASM32_DIAGNOSTIC_UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE",
  "MASM32_DIAGNOSTIC_STARTUP_STATE_SEED",
  "MASM32_DIAGNOSTIC_INSTRUCTION_LIMIT"
];

/**
 * Deletes an environment variable by case-insensitive name.
 *
 * Windows treats environment variable names case-insensitively, but Node.js can
 * expose differently-cased keys in `process.env`. Removing every matching key
 * prevents stale shell variables from changing fixture behavior.
 *
 * @param {NodeJS.ProcessEnv} env Environment object to mutate.
 * @param {string} key Environment key to delete case-insensitively.
 * @returns {void}
 */
function deleteEnvKeyCaseInsensitive(env, key) {
  const lowerKey = key.toLowerCase();
  for (const existingKey of Object.keys(env)) {
    if (existingKey.toLowerCase() === lowerKey) {
      delete env[existingKey];
    }
  }
}

/**
 * Builds a child-process environment with deterministic producer controls.
 *
 * Manual environment variables such as
 * `MASM32_DIAGNOSTIC_MEMORY_VALIDATION=uninitialized-read-warnings` are useful
 * for one-off producer runs, but they must not affect this test harness. This
 * helper removes all producer-control variables before applying per-fixture
 * overrides, while preserving unrelated variables such as PATH and the producer
 * executable override.
 *
 * @param {Record<string, string>} extraEnv Additional environment variables for the producer.
 * @returns {NodeJS.ProcessEnv} Environment object for `spawnSync`.
 */
function buildChildEnv(extraEnv = {}) {
  const env = { ...process.env };

  for (const key of PRODUCER_CONTROL_ENV_KEYS) {
    deleteEnvKeyCaseInsensitive(env, key);
  }

  return { ...env, MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE: "off", ...extraEnv };
}

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
  const result = spawnSync(PRODUCER_PATH, { input: source, encoding: "utf8", env: buildChildEnv(extraEnv) });
  assert.equal(result.status, 0, `producer failed for ${name}: ${result.error?.message ?? result.stderr ?? "unknown spawn failure"}`);
  assert.equal(result.stderr, "", `producer wrote unexpected stderr for ${name}`);
  assert.match(result.stdout, /^\{/, `producer stdout must contain raw JSON only for ${name}`);

  const rawJson = result.stdout.trimEnd();
  const json = JSON.parse(rawJson);
  const rendered = formatSimulatorMessages(json.simulatorMessages);
  return { json, rawJson, rendered };
}

/**
 * Executes the native diagnostic JSON producer and expects utility failure.
 *
 * @param {string} name Fixture name used in failure output.
 * @param {string} source MASM-like source text to pass on stdin.
 * @param {Record<string, string>} [extraEnv] Additional environment variables for the producer.
 * @returns {{stdout: string, stderr: string, status: number | null}} Producer process result summary.
 */
function runFixtureExpectFailure(name, source, extraEnv = {}) {
  const result = spawnSync(PRODUCER_PATH, { input: source, encoding: "utf8", env: buildChildEnv(extraEnv) });
  assert.notEqual(result.status, 0, `producer unexpectedly succeeded for ${name}: ${result.stdout}`);
  return { stdout: result.stdout, stderr: result.stderr, status: result.status };
}

/**
 * Normalizes process output line endings so exact stderr assertions remain
 * stable across Unix-like shells and Windows CRT text output.
 *
 * @param {string} text Process output text.
 * @returns {string} Text with CRLF and CR line endings normalized to LF.
 */
function normalizeProcessOutput(text) {
  return text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
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
    const result = spawnSync(PRODUCER_PATH, [fixturePath], { encoding: "utf8", env: buildChildEnv() });
    assert.equal(result.status, 0, `producer failed for ${name}: ${result.error?.message ?? result.stderr ?? "unknown spawn failure"}`);
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
 * Asserts no message with a specific diagnostic code appears.
 *
 * @param {Array<object>} messages Simulator messages returned by source-run JSON.
 * @param {string} code Diagnostic code that must be absent.
 * @returns {void}
 */
function assertNoMessageWithCode(messages, code) {
  assert.equal(messages.some((message) => message && message.code === code), false);
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

/**
 * Asserts one Phase 64C modeled flag child row is present with the expected value.
 *
 * @param {string} formatted Full formatted register table.
 * @param {string} flagName Modeled flag name.
 * @param {number} expected Expected modeled bit value.
 * @returns {void}
 */
function assertModeledFlagLine(formatted, flagName, expected) {
  assert.match(formatted, new RegExp(`^  ${flagName}\\s+\\| ${expected}$`, "m"));
}

/**
 * Asserts Phase 64C modeled flag rows are present and future-owned display text is absent.
 *
 * @param {string} formatted Full formatted register table.
 * @param {{CF: number, ZF: number, SF: number, OF: number}} expected Expected modeled flags.
 * @returns {void}
 */
function assertModeledFlags(formatted, expected) {
  assert.match(formatted, /^EFLAGS \| [0-9A-F]{8}h \/ [0-9]+\s*$/m);
  assertModeledFlagLine(formatted, "CF", expected.CF);
  assertModeledFlagLine(formatted, "ZF", expected.ZF);
  assertModeledFlagLine(formatted, "SF", expected.SF);
  assertModeledFlagLine(formatted, "OF", expected.OF);
  assert.doesNotMatch(formatted, /^  (PF|AF|DF|IF|TF)\s+\|/m);
  assert.doesNotMatch(formatted, /undefined|architecturally/i);
}


/**
 * Runs one Phase 51 rendered diagnostic smoke fixture and reports the expected line.
 *
 * @param {string} fixtureName Fixture key in the fixture table.
 * @param {string} expectedRendered Exact expected rendered Simulator Messages text.
 * @param {Record<string, string>} [extraEnv] Additional producer environment variables.
 * @returns {void}
 */
function runPhase51RenderedDiagnosticSmoke(fixtureName, expectedRendered, extraEnv = {}) {
  const source = fixtureSource(fixtureName);
  const { json, rawJson, rendered } = runFixture(fixtureName, source, extraEnv);
  console.log(`PHASE 51 expected rendered diagnostic line: ${expectedRendered}`);
  assert.equal(json.simulatorMessages.length >= 1, true);
  assertRenderedEquals(fixtureName, source, rawJson, rendered, expectedRendered);
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
  phase57pHostIncludePath: {
    source: `include \\masm32\\include\\masm32.inc
.code
main PROC
main ENDP
END main
`,
    reason: "Phase 57P unsupported MASM32 SDK host INCLUDE path fixture."
  },
  phase57pWindowsApiIncludePath: {
    source: `include C:\\masm32\\include\\kernel32.inc
.code
main PROC
main ENDP
END main
`,
    reason: "Phase 57P unsupported Windows/API INCLUDE path fixture."
  },
  phase57pMultipleHostIncludePaths: {
    source: `include ..\\include\\file.inc
include \\masm32\\include\\kernel32.inc
.code
main PROC
main ENDP
END main
`,
    reason: "Phase 57P multi-diagnostic host INCLUDE path fixture."
  },
  phase57qGenericIncludelib: {
    source: `includelib customlib.lib
.code
main PROC
main ENDP
END main
`,
    reason: "Phase 57Q generic unsupported INCLUDELIB fixture."
  },
  phase57qMasm32Library: {
    source: `includelib \\masm32\\lib\\masm32.lib
.code
main PROC
main ENDP
END main
`,
    reason: "Phase 57Q unsupported MASM32 library fixture."
  },
  phase57qWindowsApiLibrary: {
    source: `includelib C:\\masm32\\lib\\kernel32.lib
.code
main PROC
main ENDP
END main
`,
    reason: "Phase 57Q unsupported Windows API library fixture."
  },
  phase57qMultipleIncludelib: {
    source: `includelib customlib.lib
includelib kernel32.lib
.code
main PROC
main ENDP
END main
`,
    reason: "Phase 57Q multi-diagnostic INCLUDELIB fixture."
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
  typeExpressionTail: {
    source: `.data
x DWORD 0
.code
main PROC
    mov eax, TYPE x + 1
main ENDP
END main
`,
    reason: "Phase 56B stable TYPE expression diagnostic wording fixture."
  },
  lengthofExpressionTail: {
    source: `.data
x DWORD 0
.code
main PROC
    mov eax, LENGTHOF x + 1
main ENDP
END main
`,
    reason: "Phase 56B stable LENGTHOF expression diagnostic wording fixture."
  },
  sizeofExpressionTail: {
    source: `.data
x DWORD 0
.code
main PROC
    mov eax, SIZEOF x + 1
main ENDP
END main
`,
    reason: "Phase 56B stable SIZEOF expression diagnostic wording fixture."
  },
  unsupportedInstruction: {
    source: `.code
main PROC
    bswap ebx
main ENDP
END main
`,
    reason: "Phase 56B stable unsupported-instruction diagnostic wording fixture."
  },
  postCodeSection: {
    source: `.code
main PROC
.data
main ENDP
END main
`,
    reason: "Phase 56B stable post-code section diagnostic wording fixture."
  },
  textEquate: {
    source: `NAME EQU <text>
.code
main PROC
END main
`,
    reason: "Phase 56B stable text EQU diagnostic wording fixture."
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
  constCrossRegionWriteOverlap: {
    source: `.const
x BYTE 1

.code
main PROC
    mov eax, OFFSET x
    mov ebx, [eax]
    sub eax, 2
    mov DWORD PTR [eax], 0FFFFFFFFh
main ENDP
END main
`,
    reason: "Phase 57-CORR1 cross-region .CONST write-overlap diagnostic fixture."
  },
  constCrossRegionReadOverlap: {
    source: `.const
x BYTE 1

.code
main PROC
    mov eax, OFFSET x
    sub eax, 2
    mov ebx, DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 57-CORR1 cross-region .CONST read-overlap diagnostic fixture."
  },
  incImmediateDestination: {
    source: `.code
main PROC
    inc 1
main ENDP
END main
`,
    reason: "Phase 43 INC immediate-destination diagnostic fixture."
  },
  incExtraOperand: {
    source: `.code
main PROC
    inc eax, ebx
main ENDP
END main
`,
    reason: "Phase 43 INC extra-operand diagnostic fixture."
  },
  incAmbiguousMemoryWidth: {
    source: `.code
main PROC
    inc [eax]
main ENDP
END main
`,
    reason: "Phase 43 INC ambiguous memory-width diagnostic fixture."
  },
  decConstStaticWrite: {
    source: `.CONST
limit DWORD 10
.code
main PROC
    dec limit
main ENDP
END main
`,
    reason: "Phase 43 DEC direct .CONST write diagnostic fixture."
  },
  incConstRuntimeWrite: {
    source: `.CONST
limit DWORD 10

.code
main PROC
    mov eax, OFFSET limit
    inc DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 43 INC computed .CONST write diagnostic fixture."
  },
  decInvalidAddress: {
    source: `.code
main PROC
    mov eax, 0
    dec DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 43 DEC invalid-address runtime diagnostic fixture."
  },
  andAmbiguousMemoryWidth: {
    source: `.code
main PROC
    and [eax], 1
main ENDP
END main
`,
    reason: "Phase 44 AND ambiguous memory-width diagnostic fixture."
  },
  orImmediateDestination: {
    source: `.code
main PROC
    or 1, eax
main ENDP
END main
`,
    reason: "Phase 44 OR immediate-destination diagnostic fixture."
  },
  xorMemoryToMemory: {
    source: `.data
value DWORD 1
other DWORD 2
.code
main PROC
    xor value, other
main ENDP
END main
`,
    reason: "Phase 44 XOR memory-to-memory diagnostic fixture."
  },
  andConstDirectWrite: {
    source: `.CONST
limit DWORD 10
.code
main PROC
    and limit, 1
main ENDP
END main
`,
    reason: "Phase 44 AND direct .CONST write diagnostic fixture."
  },
  orConstRuntimeWrite: {
    source: `.CONST
limit DWORD 10
.code
main PROC
    mov eax, OFFSET limit
    or DWORD PTR [eax], 1
main ENDP
END main
`,
    reason: "Phase 44 OR computed .CONST write diagnostic fixture."
  },
  andInvalidSourceAddress: {
    source: `.code
main PROC
    mov eax, 0
    and ebx, DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 44 AND invalid source-address runtime diagnostic fixture."
  },
  xorInvalidAddress: {
    source: `.code
main PROC
    mov eax, 0
    xor DWORD PTR [eax], 1
main ENDP
END main
`,
    reason: "Phase 44 XOR invalid destination-address runtime diagnostic fixture."
  },

  shlAmbiguousMemoryWidth: {
    source: `.code
main PROC
    shl [eax], 1
main ENDP
END main
`,
    reason: "Phase 46 SHL ambiguous memory-width diagnostic fixture."
  },
  shlInvalidCountRegister: {
    source: `.code
main PROC
    shl eax, ebx
main ENDP
END main
`,
    reason: "Phase 46 SHL invalid count-register diagnostic fixture."
  },
  shlUndefinedWarning: {
    source: `.code
main PROC
    mov al, 1
    shl al, 8
main ENDP
END main
`,
    reason: "Phase 46 SHL undefined modeled flag warning fixture."
  },
  salUndefinedWarning: {
    source: `.code
main PROC
    mov ecx, 00000102h
    mov eax, 00000003h
    sal eax, cl
main ENDP
END main
`,
    reason: "Phase 46 SAL undefined modeled flag warning fixture where CF remains defined."
  },
  shlUndefinedStrict: {
    source: `.code
main PROC
    mov al, 1
    shl al, 8
main ENDP
END main
`,
    reason: "Phase 46 SHL strict undefined modeled flag diagnostic fixture."
  },

  shrAmbiguousMemoryWidth: {
    source: `.code
main PROC
    shr [eax], 1
main ENDP
END main
`,
    reason: "Phase 47 SHR ambiguous memory-width diagnostic fixture."
  },
  shrInvalidCountRegister: {
    source: `.code
main PROC
    shr eax, ebx
main ENDP
END main
`,
    reason: "Phase 47 SHR invalid count-register diagnostic fixture."
  },
  shrUndefinedWarning: {
    source: `.code
main PROC
    mov al, 80h
    shr al, 8
main ENDP
END main
`,
    reason: "Phase 47 SHR undefined modeled flag warning fixture."
  },
  shrUndefinedStrict: {
    source: `.code
main PROC
    mov al, 80h
    shr al, 8
main ENDP
END main
`,
    reason: "Phase 47 SHR strict undefined modeled flag diagnostic fixture."
  },

  sarAmbiguousMemoryWidth: {
    source: `.code
main PROC
    sar [eax], 1
main ENDP
END main
`,
    reason: "Phase 48 SAR ambiguous memory-width diagnostic fixture."
  },
  sarInvalidCountRegister: {
    source: `.code
main PROC
    sar eax, ebx
main ENDP
END main
`,
    reason: "Phase 48 SAR invalid count-register diagnostic fixture."
  },
  sarUndefinedWarning: {
    source: `.code
main PROC
    mov al, 80h
    sar al, 8
main ENDP
END main
`,
    reason: "Phase 48 SAR undefined modeled flag warning fixture."
  },
  sarUndefinedStrict: {
    source: `.code
main PROC
    mov al, 80h
    sar al, 8
main ENDP
END main
`,
    reason: "Phase 48 SAR strict undefined modeled flag diagnostic fixture."
  },

  rolAmbiguousMemoryWidth: {
    source: `.code
main PROC
    rol [eax], 1
main ENDP
END main
`,
    reason: "Phase 49 ROL ambiguous memory-width diagnostic fixture."
  },
  rolInvalidCountRegister: {
    source: `.code
main PROC
    rol eax, ebx
main ENDP
END main
`,
    reason: "Phase 49 ROL invalid count-register diagnostic fixture."
  },
  rolMissingCount: {
    source: `.code
main PROC
    rol eax
main ENDP
END main
`,
    reason: "Phase 49 ROL missing count diagnostic fixture."
  },
  rolUndefinedWarning: {
    source: `.code
main PROC
    mov al, 80h
    rol al, 8
main ENDP
END main
`,
    reason: "Phase 49 ROL undefined modeled flag warning fixture."
  },
  rolUndefinedStrictStillWarns: {
    source: `.code
main PROC
    mov al, 80h
    rol al, 8
main ENDP
END main
`,
    reason: "Phase 49 ROL remains warning-only under strict shift validation."
  },
  rorAmbiguousMemoryWidth: {
    source: `.code
main PROC
    ror [eax], 1
main ENDP
END main
`,
    reason: "Phase 50 ROR ambiguous memory-width diagnostic fixture."
  },
  rorInvalidCountRegister: {
    source: `.code
main PROC
    ror eax, ebx
main ENDP
END main
`,
    reason: "Phase 50 ROR invalid count-register diagnostic fixture."
  },
  rorMissingCount: {
    source: `.code
main PROC
    ror eax
main ENDP
END main
`,
    reason: "Phase 50 ROR missing count diagnostic fixture."
  },
  rorSuccess: {
    source: `.code
main PROC
    mov al, 01h
    ror al, 1
main ENDP
END main
`,
    reason: "Phase 50 ROR one-bit acceptance fixture."
  },
  rorRuntimeInvalidAddress: {
    source: `.code
main PROC
    mov eax, 0
    ror DWORD PTR [eax], 1
main ENDP
END main
`,
    reason: "Phase 50 ROR invalid memory destination runtime diagnostic fixture."
  },
  rorUndefinedWarning: {
    source: `.code
main PROC
    mov al, 80h
    ror al, 8
main ENDP
END main
`,
    reason: "Phase 50 ROR undefined modeled flag warning fixture."
  },
  rorUndefinedStrictStillWarns: {
    source: `.code
main PROC
    mov al, 80h
    ror al, 8
main ENDP
END main
`,
    reason: "Phase 50 ROR remains warning-only under strict shift validation."
  },
  leaOffsetSource: {
    source: `.data
nums DWORD 0
.code
main PROC
    lea eax, OFFSET nums
main ENDP
END main
`,
    reason: "Phase 52 LEA rejects OFFSET source operands."
  },
  leaNarrowDestination: {
    source: `.data
nums DWORD 0
.code
main PROC
    lea ax, nums
main ENDP
END main
`,
    reason: "Phase 52 LEA requires a 32-bit register destination."
  },
  leaScaledIndex: {
    source: `.code
main PROC
    lea eax, [eax * 4]
main ENDP
END main
`,
    reason: "Phase 52 LEA keeps scaled-index addressing unsupported."
  },
  leaRegisterSource: {
    source: `.code
main PROC
    lea eax, ebx
main ENDP
END main
`,
    reason: "Phase 52 LEA rejects register source operands."
  },
  leaDisplacementOverflow: {
    source: `.data
nums DWORD 1
.code
main PROC
    lea eax, [nums + 2147483648]
main ENDP
END main
`,
    reason: "Phase 52 LEA reports static address displacement overflow as an invalid effective-address expression."
  },
  mulAmbiguousMemoryWidth: {
    source: `.code
main PROC
    mul [eax]
main ENDP
END main
`,
    reason: "Phase 53 MUL ambiguous memory-width diagnostic fixture."
  },
  mulImmediateSource: {
    source: `.code
main PROC
    mul 5
main ENDP
END main
`,
    reason: "Phase 53 MUL immediate-source diagnostic fixture."
  },
  mulExtraOperand: {
    source: `.code
main PROC
    mul eax, ebx
main ENDP
END main
`,
    reason: "Phase 53 MUL extra-operand diagnostic fixture."
  },
  mulRuntimeInvalidAddress: {
    source: `.code
main PROC
    mov eax, 0
    mul DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 53 MUL invalid memory source runtime diagnostic fixture."
  },
  mulQwordSource: {
    source: `.data
q QWORD 1
.code
main PROC
    mul QWORD PTR q
main ENDP
END main
`,
    reason: "Phase 53 MUL executable QWORD source diagnostic fixture."
  },
  imulAmbiguousMemoryWidth: {
    source: `.code
main PROC
    imul [eax]
main ENDP
END main
`,
    reason: "Phase 54 IMUL ambiguous memory-width diagnostic fixture."
  },
  imulImmediateSource: {
    source: `.code
main PROC
    imul 5
main ENDP
END main
`,
    reason: "Phase 54 IMUL immediate-source diagnostic fixture."
  },
  imulRegImmediate: {
    source: `.code
main PROC
    imul eax, 5
main ENDP
END main
`,
    reason: "Phase 55 IMUL rejected reg, imm diagnostic fixture."
  },
  imulImmediateOutOfRange: {
    source: `.code
main PROC
    imul eax, ebx, 2147483648
main ENDP
END main
`,
    reason: "Phase 55 IMUL immediate range diagnostic fixture."
  },
  imulRuntimeInvalidAddress: {
    source: `.code
main PROC
    mov eax, 0
    imul DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 54 IMUL invalid memory source runtime diagnostic fixture."
  },
  imulQwordSource: {
    source: `.data
q QWORD 1
.code
main PROC
    imul QWORD PTR q
main ENDP
END main
`,
    reason: "Phase 54 IMUL executable QWORD source diagnostic fixture."
  },
  imulUninitializedRead: {
    source: `.DATA?
x DWORD ?
.code
main PROC
    mov eax, 3
    imul x
main ENDP
END main
`,
    reason: "Phase 54 IMUL default uninitialized-read warning fixture."
  },
  undefinedFlagUseAdc: {
    source: `.code
main PROC
    stc
    mov al, 1
    shl al, 8
    mov ebx, 0
    adc ebx, 0
main ENDP
END main
`,
    reason: "Phase 50B undefined flag-use diagnostic fixture for ADC consuming CF."
  },
  notAmbiguousMemoryWidth: {
    source: `.code
main PROC
    not [eax]
main ENDP
END main
`,
    reason: "Milestone 45 NOT ambiguous memory-width diagnostic regression fixture under Phase 50."
  },
  notImmediateDestination: {
    source: `.code
main PROC
    not 1
main ENDP
END main
`,
    reason: "Milestone 45 NOT immediate-destination diagnostic regression fixture under Phase 50."
  },
  notExtraOperand: {
    source: `.code
main PROC
    not eax, ebx
main ENDP
END main
`,
    reason: "Milestone 45 NOT extra-operand diagnostic regression fixture under Phase 50."
  },
  notConstDirectWrite: {
    source: `.CONST
limit DWORD 10
.code
main PROC
    not limit
main ENDP
END main
`,
    reason: "Milestone 45 NOT direct .CONST write diagnostic regression fixture under Phase 50."
  },
  notConstRuntimeWrite: {
    source: `.CONST
limit DWORD 10
.code
main PROC
    mov eax, OFFSET limit
    not DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Milestone 45 NOT computed .CONST write diagnostic regression fixture under Phase 50."
  },
  notInvalidAddress: {
    source: `.code
main PROC
    mov eax, 0
    not DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Milestone 45 NOT invalid destination-address runtime diagnostic regression fixture under Phase 50."
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
  objectBoundsWarning: {
    source: `.data
var1 DWORD 12345
.code
main PROC
    mov eax, OFFSET var1
    test [eax+40h], eax
main ENDP
END main
`,
    reason: "Allocated-object warning mode diagnostic fixture."
  },
  phase53aObjectSpan: {
    source: `.data
x DWORD 0
y DWORD 0

.code
main PROC
    mov eax, DWORD PTR [x+1]
main ENDP
END main
`,
    reason: "Phase 53A object-spanning symbol-offset memory read fixture."
  },
  phase53aObjectSpanStrictMutation: {
    source: `.data
x DWORD 01020304h
y DWORD 05060708h

.code
main PROC
    mov eax, 777
    mov eax, DWORD PTR [x+1]
main ENDP
END main
`,
    reason: "Phase 53A strict object validation no-partial-mutation fixture."
  },
  phase53aUninitializedObjectSpan: {
    source: `.DATA?
x DWORD ?
y DWORD ?

.code
main PROC
    mov eax, DWORD PTR [x+1]
main ENDP
END main
`,
    reason: "Phase 53A uninitialized-read symbol-offset memory read fixture."
  },
  phase53bSectionImageWarning: {
    source: `.data
x DWORD 1
.code
main PROC
    mov eax, OFFSET x
    add eax, 4
    mov ebx, DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 53B section-image warning fixture for fixed-layout data slack."
  },
  phase53bSectionImageStrict: {
    source: `.data
x DWORD 1
.code
main PROC
    mov eax, OFFSET x
    add eax, 4
    mov DWORD PTR [eax], 123
main ENDP
END main
`,
    reason: "Phase 53B section-image strict fixture for rejected data slack write."
  },
  phase53bSectionCapacityWarning: {
    source: `.code
main PROC
    mov eax, 00700000h
    mov DWORD PTR [eax], 123
main ENDP
END main
`,
    reason: "Phase 53B section-capacity warning fixture for valid non-section heap storage."
  },
  phase53bSectionCapacityStrict: {
    source: `.code
main PROC
    mov eax, 00700000h
    mov DWORD PTR [eax], 123
main ENDP
END main
`,
    reason: "Phase 53B section-capacity strict fixture for valid non-section heap storage."
  },
  uninitializedRead: {
    source: `.data
x DWORD ?
.code
main PROC
    mov eax, x
main ENDP
END main
`,
    reason: "Uninitialized-read warning and strict diagnostic fixture."
  },
  uninitializedReadBracketed: {
    source: `.data
x DWORD ?
.code
main PROC
    mov eax, OFFSET x
    mov ebx, DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Bracketed uninitialized-read strict source-span fixture."
  },
  uninitializedReadIndirectOverlap: {
    source: `.DATA?
x DWORD ?
.code
main PROC
    mov eax, OFFSET x
    mov ebx, DWORD PTR [eax+1]
main ENDP
END main
`,
    reason: "Indirect uninitialized-read fixture whose final range overlaps tracked .DATA? bytes and fixed-layout slack."
  },
  irvine32UnsupportedRoutine: {
    source: `INCLUDE Irvine32.inc
.code
main PROC
    WriteString
main ENDP
END main
`,
    reason: "Known Irvine32 routine diagnostic fixture."
  },
  exitWithoutIrvine32Include: {
    source: `.code
main PROC
    exit
main ENDP
END main
`,
    reason: "Phase 42 exit without Irvine32 include diagnostic fixture."
  },
  exitWithOperand: {
    source: `INCLUDE Irvine32.inc
.code
main PROC
    exit 0
main ENDP
END main
`,
    reason: "Phase 42 invalid exit operand diagnostic fixture."
  },
  phase51LayoutAndInstructionSmoke: {
    source: `.DATA?
scratch DWORD ?
.data
value DWORD 3
.CONST
mask DWORD 0Fh
.code
main PROC
    mov eax, value
    inc eax
    and eax, mask
    shl eax, 1
    mov scratch, eax
    mov ebx, scratch
main ENDP
END main
`,
    reason: "Phase 51 native diagnostic JSON plus Node renderer smoke fixture for automatic layout and post-30 instructions."
  },
  phase51ConstPrecedence: {
    source: `.CONST
limit DWORD 10
.code
main PROC
    mov eax, OFFSET limit
    inc DWORD PTR [eax]
main ENDP
END main
`,
    reason: "Phase 51 .CONST permission precedence rendered diagnostic smoke fixture."
  },
  phase51UninitializedRmw: {
    source: `.data
x DWORD ?
.code
main PROC
    add x, 1
main ENDP
END main
`,
    reason: "Phase 51 read-modify-write uninitialized-read rendered diagnostic smoke fixture."
  },
  phase51IncDecDiagnosticSmoke: {
    source: `.code
main PROC
    dec [eax]
main ENDP
END main
`,
    reason: "Phase 51 INC/DEC rendered diagnostic smoke fixture."
  },
  phase51AndOrXorDiagnosticSmoke: {
    source: `.code
main PROC
    xor [eax], 1
main ENDP
END main
`,
    reason: "Phase 51 AND/OR/XOR rendered diagnostic smoke fixture."
  },
  phase51NotDiagnosticSmoke: {
    source: `.code
main PROC
    not [eax]
main ENDP
END main
`,
    reason: "Phase 51 NOT rendered diagnostic smoke fixture."
  },
  phase51ShlSalDiagnosticSmoke: {
    source: `.code
main PROC
    sal [eax], 1
main ENDP
END main
`,
    reason: "Phase 51 SHL/SAL rendered diagnostic smoke fixture."
  },
  phase51ShrDiagnosticSmoke: {
    source: `.code
main PROC
    shr [eax], 1
main ENDP
END main
`,
    reason: "Phase 51 SHR rendered diagnostic smoke fixture."
  },
  phase51SarDiagnosticSmoke: {
    source: `.code
main PROC
    sar [eax], 1
main ENDP
END main
`,
    reason: "Phase 51 SAR rendered diagnostic smoke fixture."
  },
  phase51RolDiagnosticSmoke: {
    source: `.code
main PROC
    rol [eax], 1
main ENDP
END main
`,
    reason: "Phase 51 ROL rendered diagnostic smoke fixture."
  },
  phase51RorDiagnosticSmoke: {
    source: `.code
main PROC
    ror [eax], 1
main ENDP
END main
`,
    reason: "Phase 51 ROR rendered diagnostic smoke fixture."
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
  highLevelFlowMarkers: {
    source: `.code
main PROC
    .IF eax == 0
        mov ebx, 1
    .ELSE
        badinstruction eax
    .ENDIF
main ENDP
END main
`,
    reason: "Phase 57S high-level flow marker recovery fixture."
  },
  phase57tPlaygroundDiagnosticRecovery: {
    source: `.386
.model flat, stdcall
option casemap:none

include \\masm32\\include\\masm32.inc
include \\masm32\\include\\kernel32.inc

includelib \\masm32\\lib\\masm32.lib
includelib \\masm32\\lib\\kernel32.lib

.data
    titleMsg   db "=== MASM32 Playground ===",13,10,0
    startMsg   db "Counting from 1 to 5",13,10,0
    evenMsg    db " -> even number",13,10,0
    oddMsg     db " -> odd number",13,10,0
    numberFmt  db "Number: %d",13,10,0
    counter    dd 1
    total      dd 0

.code
main PROC
    invoke StdOut, addr titleMsg
    invoke StdOut, addr startMsg
    invoke crt_printf, addr numberFmt, counter

    mov eax, counter
    and eax, 1

    .IF eax == 0
        invoke StdOut, addr evenMsg
    .ELSE
        invoke StdOut, addr oddMsg
    .ENDIF

    mov eax, counter
    call AddToTotal

    inc counter

    cmp counter, 6
    loop main_loop

    invoke crt_printf, addr numberFmt, total
    invoke ExitProcess, 0
main ENDP
END main
`,
    reason: "Phase 57T realistic MASM32 playground diagnostic-recovery fixture."
  },
  phase57tRetUnsupported: {
    source: `.code
main PROC
    ret
main ENDP
END main
`,
    reason: "Phase 57T RET unsupported-instruction diagnostic fixture."
  },
  phase62CmpRegisterImmediateSuccess: {
    source: `.code
main PROC
    cmp eax, 6
main ENDP
END main
`,
    reason: "Current Phase 66 runtime metadata with Phase 63 CMP register/immediate success fixture."
  },
  phase57tLoopUnsupported: {
    source: `.code
main PROC
    loop main_loop
main ENDP
END main
`,
    reason: "Phase 57T loop unsupported-instruction diagnostic fixture retained after Phase 66 unsigned jumps."
  },
  phase57tExitProcessUnsupported: {
    source: `INCLUDE Irvine32.inc
.code
main PROC
    invoke ExitProcess, 0
main ENDP
END main
`,
    reason: "Phase 57T WinAPI ExitProcess unsupported-feature diagnostic fixture."
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
  phase58DuplicateLabel: {
    source: `.code
main PROC
start:
    mov eax, 1
start:
    mov ebx, 2
main ENDP
END main
`,
    reason: "Phase 58 duplicate code-label diagnostic fixture."
  },
  phase58FoldedDuplicateLabel: {
    source: `.code
main PROC
Spin:
    mov eax, 1
spin:
    mov ebx, 2
main ENDP
END main
`,
    reason: "Phase 58 folded CASEMAP code-label diagnostic fixture."
  },
  phase58LabelDataConflict: {
    source: `.data
value DWORD 1
.code
main PROC
value:
    mov eax, 1
main ENDP
END main
`,
    reason: "Phase 58 code-label versus data-symbol conflict diagnostic fixture."
  },
  phase58ProcedureThenOrdinaryLabelConflict: {
    source: `.code
main PROC
main:
    mov eax, 1
main ENDP
END main
`,
    reason: "Phase 58 procedure-entry versus later ordinary-label conflict diagnostic fixture."
  },
  phase58ProcedureLabelConflict: {
    source: `.code
main PROC
other:
main ENDP
other PROC
other ENDP
END main
`,
    reason: "Phase 58 procedure-entry versus ordinary-label conflict diagnostic fixture."
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

test("Phase 57P renders unsupported MASM32 SDK include path exactly", () => {
  const name = "phase57pHostIncludePath";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-masm32-library-include",
      message: "Host filesystem include path '\\masm32\\include\\masm32.inc' is not supported. This browser simulator does not read the local MASM32 SDK; use supported virtual includes only.",
      line: 1,
      column: 9,
      byteOffset: 8,
      spanLength: 26
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-masm32-library-include line 1, column 9, byte offset 8, span length 26: Host filesystem include path '\\masm32\\include\\masm32.inc' is not supported. This browser simulator does not read the local MASM32 SDK; use supported virtual includes only.");
});

test("Phase 57P renders unsupported Windows API include path exactly", () => {
  const name = "phase57pWindowsApiIncludePath";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-windows-api-include",
      message: "Windows API include path 'C:\\masm32\\include\\kernel32.inc' is not supported. Windows API execution is outside this simulator; PE loading, imports, and WinAPI calls are not performed.",
      line: 1,
      column: 9,
      byteOffset: 8,
      spanLength: 30
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-windows-api-include line 1, column 9, byte offset 8, span length 30: Windows API include path 'C:\\masm32\\include\\kernel32.inc' is not supported. Windows API execution is outside this simulator; PE loading, imports, and WinAPI calls are not performed.");
});

test("Phase 57P renders multiple host include diagnostics exactly", () => {
  const name = "phase57pMultipleHostIncludePaths";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-host-include-path",
      message: "Host filesystem include path '..\\include\\file.inc' is not supported. This browser simulator does not read local include files, relative include paths, or include search paths; use supported virtual includes only.",
      line: 1,
      column: 9,
      byteOffset: 8,
      spanLength: 19
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-windows-api-include",
      message: "Windows API include path '\\masm32\\include\\kernel32.inc' is not supported. Windows API execution is outside this simulator; PE loading, imports, and WinAPI calls are not performed.",
      line: 2,
      column: 9,
      byteOffset: 36,
      spanLength: 28
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-host-include-path line 1, column 9, byte offset 8, span length 19: Host filesystem include path '..\\include\\file.inc' is not supported. This browser simulator does not read local include files, relative include paths, or include search paths; use supported virtual includes only.\n[unsupported-feature] unsupported-windows-api-include line 2, column 9, byte offset 36, span length 28: Windows API include path '\\masm32\\include\\kernel32.inc' is not supported. Windows API execution is outside this simulator; PE loading, imports, and WinAPI calls are not performed.");
});


test("Phase 57Q renders unsupported INCLUDELIB exactly", () => {
  const name = "phase57qGenericIncludelib";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-includelib",
      message: "INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Library operand 'customlib.lib' cannot be used; execution stops before program start.",
      line: 1,
      column: 12,
      byteOffset: 11,
      spanLength: 13
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-includelib line 1, column 12, byte offset 11, span length 13: INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Library operand 'customlib.lib' cannot be used; execution stops before program start.");
});

test("Phase 57Q renders unsupported MASM32 library exactly", () => {
  const name = "phase57qMasm32Library";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-masm32-library",
      message: "INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. MASM32 library '\\masm32\\lib\\masm32.lib' requires external library linking.",
      line: 1,
      column: 12,
      byteOffset: 11,
      spanLength: 22
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-masm32-library line 1, column 12, byte offset 11, span length 22: INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. MASM32 library '\\masm32\\lib\\masm32.lib' requires external library linking.");
});

test("Phase 57Q renders unsupported Windows API library exactly", () => {
  const name = "phase57qWindowsApiLibrary";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-windows-api-library",
      message: "INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Windows import library 'C:\\masm32\\lib\\kernel32.lib' requires PE imports and WinAPI execution.",
      line: 1,
      column: 12,
      byteOffset: 11,
      spanLength: 26
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-windows-api-library line 1, column 12, byte offset 11, span length 26: INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Windows import library 'C:\\masm32\\lib\\kernel32.lib' requires PE imports and WinAPI execution.");
});

test("Phase 57Q renders multiple INCLUDELIB diagnostics exactly", () => {
  const name = "phase57qMultipleIncludelib";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-includelib",
      message: "INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Library operand 'customlib.lib' cannot be used; execution stops before program start.",
      line: 1,
      column: 12,
      byteOffset: 11,
      spanLength: 13
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-windows-api-library",
      message: "INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Windows import library 'kernel32.lib' requires PE imports and WinAPI execution.",
      line: 2,
      column: 12,
      byteOffset: 36,
      spanLength: 12
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-includelib line 1, column 12, byte offset 11, span length 13: INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Library operand 'customlib.lib' cannot be used; execution stops before program start.\n[unsupported-feature] unsupported-windows-api-library line 2, column 12, byte offset 36, span length 12: INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Windows import library 'kernel32.lib' requires PE imports and WinAPI execution.");
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

test("renders Phase 57E startup-state notice exactly", () => {
  const name = "phase57eStartupStateNotice";
  const source = fixtureSource("success");
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE: "warn"
  });
  assertRunStatus(json, true, "ok");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "simulator-notice",
    code: "startup-state-notice",
    message: STARTUP_STATE_NOTICE_TEXT
  });
  assertMessageEquals(json.simulatorMessages[1], {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals(name, source, rawJson, rendered, `${STARTUP_STATE_NOTICE_RENDERED}\n\n[info] execution-complete: Execution completed successfully.`);
});

test("renders Phase 57F seeded startup-state notice exactly", () => {
  const name = "phase57fSeededStartupStateNotice";
  const source = fixtureSource("success");
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_STARTUP_REGISTER_FLAG_MODE: "seeded-random",
    MASM32_DIAGNOSTIC_STARTUP_STATE_SEED: "123",
    MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE: "warn"
  });
  assertRunStatus(json, true, "ok");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "simulator-notice",
    code: "startup-state-notice",
    message: SEEDED_STARTUP_STATE_NOTICE_TEXT
  });
  assertMessageEquals(json.simulatorMessages[1], {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals(name, source, rawJson, rendered, `${SEEDED_STARTUP_STATE_NOTICE_RENDERED}\n\n[info] execution-complete: Execution completed successfully.`);
});

test("renders Phase 57G seeded uninitialized-storage startup-state notice exactly", () => {
  const name = "phase57gSeededUninitializedStorageNotice";
  const source = fixtureSource("success");
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE: "seeded-random",
    MASM32_DIAGNOSTIC_STARTUP_STATE_SEED: "123",
    MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE: "warn"
  });
  assertRunStatus(json, true, "ok");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "simulator-notice",
    code: "startup-state-notice",
    message: SEEDED_UNINITIALIZED_STORAGE_NOTICE_TEXT
  });
  assertRenderedEquals(name, source, rawJson, rendered, `${SEEDED_UNINITIALIZED_STORAGE_NOTICE_RENDERED}

[info] execution-complete: Execution completed successfully.`);
});

test("renders combined Phase 57F and Phase 57G seeded startup-state notice exactly", () => {
  const name = "phase57gCombinedSeededStartupNotice";
  const source = fixtureSource("success");
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_STARTUP_REGISTER_FLAG_MODE: "seeded-random",
    MASM32_DIAGNOSTIC_UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE: "seeded-random",
    MASM32_DIAGNOSTIC_STARTUP_STATE_SEED: "123",
    MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE: "warn"
  });
  assertRunStatus(json, true, "ok");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "simulator-notice",
    code: "startup-state-notice",
    message: SEEDED_REGISTER_AND_UNINITIALIZED_STORAGE_NOTICE_TEXT
  });
  assertRenderedEquals(name, source, rawJson, rendered, `${SEEDED_REGISTER_AND_UNINITIALIZED_STORAGE_NOTICE_RENDERED}

[info] execution-complete: Execution completed successfully.`);
});


test("renders Phase 57G invalid uninitialized-storage startup setting ui-error exactly", () => {
  const rendered = formatSimulatorMessages([
    {
      kind: "ui-error",
      code: "invalid-startup-setting",
      message: "Invalid startup setting 'uninitializedStorageVisibleByteMode'. Accepted values: zero, seeded-random."
    }
  ]);

  assert.equal(rendered, "[ui-error] invalid-startup-setting: Invalid startup setting 'uninitializedStorageVisibleByteMode'. Accepted values: zero, seeded-random.");
});

test("rejects Phase 57F startup-state seed values outside uint32 range", () => {
  const name = "phase57fStartupStateSeedOverflow";
  const source = fixtureSource("success");
  const result = runFixtureExpectFailure(name, source, {
    MASM32_DIAGNOSTIC_STARTUP_REGISTER_FLAG_MODE: "seeded-random",
    MASM32_DIAGNOSTIC_STARTUP_STATE_SEED: "4294967296"
  });
  assert.equal(result.stdout, "", `producer stdout must be empty for ${name}`);
  assert.equal(normalizeProcessOutput(result.stderr), "diagnostic_json_producer: invalid unsigned environment value\n");
});

test("renders Phase 57E startup-state notice opt-out exactly", () => {
  const name = "phase57eStartupStateNoticeOff";
  const source = fixtureSource("success");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.simulatorMessages.some((message) => message.code === "startup-state-notice"), false);
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


test("renders TYPE expression diagnostic with stable wording exactly", () => {
  const name = "typeExpressionTail";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-type-expression",
    message: "Unsupported TYPE expression. Write TYPE followed by exactly one declared data symbol, for example TYPE nums. Arithmetic, bracketed operands, and nested expressions are not accepted in TYPE operands.",
    line: 5,
    column: 21,
    byteOffset: 52,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-type-expression line 5, column 21, byte offset 52, span length 1: Unsupported TYPE expression. Write TYPE followed by exactly one declared data symbol, for example TYPE nums. Arithmetic, bracketed operands, and nested expressions are not accepted in TYPE operands.");
});

test("renders LENGTHOF expression diagnostic with stable wording exactly", () => {
  const name = "lengthofExpressionTail";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-lengthof-expression",
    message: "Unsupported LENGTHOF expression. Write LENGTHOF followed by exactly one declared data symbol, for example LENGTHOF nums. Arithmetic, bracketed operands, and nested expressions are not accepted in LENGTHOF operands.",
    line: 5,
    column: 25,
    byteOffset: 56,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-lengthof-expression line 5, column 25, byte offset 56, span length 1: Unsupported LENGTHOF expression. Write LENGTHOF followed by exactly one declared data symbol, for example LENGTHOF nums. Arithmetic, bracketed operands, and nested expressions are not accepted in LENGTHOF operands.");
});

test("renders SIZEOF expression diagnostic with stable wording exactly", () => {
  const name = "sizeofExpressionTail";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-sizeof-expression",
    message: "Unsupported SIZEOF expression. Write SIZEOF followed by exactly one declared data symbol, for example SIZEOF nums. Arithmetic, bracketed operands, and nested expressions are not accepted in SIZEOF operands.",
    line: 5,
    column: 23,
    byteOffset: 54,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-sizeof-expression line 5, column 23, byte offset 54, span length 1: Unsupported SIZEOF expression. Write SIZEOF followed by exactly one declared data symbol, for example SIZEOF nums. Arithmetic, bracketed operands, and nested expressions are not accepted in SIZEOF operands.");
});

test("renders unsupported instruction diagnostic with stable wording exactly", () => {
  const name = "unsupportedInstruction";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-instruction",
    message: "Unsupported instruction. This mnemonic has no executable behavior in MASM32 Educational Mode; use an implemented instruction listed in docs/SUPPORTED_SYNTAX.md.",
    line: 3,
    column: 5,
    byteOffset: 20,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-instruction line 3, column 5, byte offset 20, span length 5: Unsupported instruction. This mnemonic has no executable behavior in MASM32 Educational Mode; use an implemented instruction listed in docs/SUPPORTED_SYNTAX.md.");
});

test("renders Phase 59 instruction-limit diagnostic under Phase 64 exactly", () => {
  const name = "phase59InstructionLimit";
  const source = `.code
main PROC
    mov eax, 1
    mov ebx, 2
    mov ecx, 3
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, { MASM32_DIAGNOSTIC_INSTRUCTION_LIMIT: "2" });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.phase, 66);
  assert.equal(json.instructionCount, 2);
  assert.equal(json.instructionLimit, 2);
  assert.equal(json.executedInstructionCount, 2);
  assert.equal(json.attemptedNextInstructionIndex, 2);
  assert.equal(json.currentInstructionIndex, 1);
  assert.equal(json.registers.EAX.hex, "00000001h");
  assert.equal(json.registers.EBX.hex, "00000002h");
  assert.equal(json.registers.ECX.hex, "00000000h");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "simulator-notice",
    code: "startup-state-notice",
    message: STARTUP_STATE_NOTICE_TEXT
  });
  assertMessageEquals(json.simulatorMessages[1], {
    kind: "runtime-error",
    code: "instruction-limit-exceeded",
    message: "Instruction limit exceeded: attempted to execute instruction #3 (limit: 2). Program stopped before executing that instruction.",
    line: 5,
    column: 5,
    byteOffset: 50,
    spanLength: 10
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, `${STARTUP_STATE_NOTICE_RENDERED}\n\n[runtime-error] instruction-limit-exceeded line 5, column 5, byte offset 50, span length 10: Instruction limit exceeded: attempted to execute instruction #3 (limit: 2). Program stopped before executing that instruction.`);
});


test("renders Phase 61E reserved code-label diagnostic exactly", () => {
  const name = "phase61eReservedCodeLabel";
  const source = `.code
main PROC
loop:
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assert.equal(json.instructionCount, 0);
  assertNoExecutionComplete(json.simulatorMessages);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "reserved-word-symbol",
    message: "'loop' is a reserved MASM instruction mnemonic and cannot be used as a code label.",
    line: 3,
    column: 1,
    byteOffset: 16,
    spanLength: 4
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] reserved-word-symbol line 3, column 1, byte offset 16, span length 4: 'loop' is a reserved MASM instruction mnemonic and cannot be used as a code label.");
});

test("renders Phase 61E reserved data-symbol diagnostic exactly", () => {
  const name = "phase61eReservedDataSymbol";
  const source = `.data
mov DWORD 1
.code
main PROC
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.instructionCount, 0);
  assertNoExecutionComplete(json.simulatorMessages);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "reserved-word-symbol",
    message: "'mov' is a reserved MASM instruction mnemonic and cannot be used as a data symbol.",
    line: 2,
    column: 1,
    byteOffset: 6,
    spanLength: 3
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] reserved-word-symbol line 2, column 1, byte offset 6, span length 3: 'mov' is a reserved MASM instruction mnemonic and cannot be used as a data symbol.");
});

test("renders Phase 61E CASEMAP:NONE reserved-label diagnostic exactly", () => {
  const name = "phase61eCasemapNoneReservedLabel";
  const source = `OPTION CASEMAP:NONE
.code
main PROC
LOOP:
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assertNoExecutionComplete(json.simulatorMessages);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "reserved-word-symbol",
    message: "'LOOP' is a reserved MASM instruction mnemonic and cannot be used as a code label.",
    line: 4,
    column: 1,
    byteOffset: 36,
    spanLength: 4
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] reserved-word-symbol line 4, column 1, byte offset 36, span length 4: 'LOOP' is a reserved MASM instruction mnemonic and cannot be used as a code label.");
});

test("renders Phase 61E reserved equate diagnostic exactly", () => {
  const name = "phase61eReservedEquate";
  const source = `add EQU 2
.code
main PROC
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assertNoExecutionComplete(json.simulatorMessages);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "reserved-word-symbol",
    message: "'add' is a reserved MASM instruction mnemonic and cannot be used as a numeric equate.",
    line: 1,
    column: 1,
    byteOffset: 0,
    spanLength: 3
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] reserved-word-symbol line 1, column 1, byte offset 0, span length 3: 'add' is a reserved MASM instruction mnemonic and cannot be used as a numeric equate.");
});

test("renders Phase 61E reserved procedure-name diagnostic exactly", () => {
  const name = "phase61eReservedProcedureName";
  const source = `.code
loop PROC
loop ENDP
END loop
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assertNoExecutionComplete(json.simulatorMessages);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "reserved-word-symbol",
    message: "'loop' is a reserved MASM instruction mnemonic and cannot be used as a procedure name.",
    line: 2,
    column: 1,
    byteOffset: 6,
    spanLength: 4
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] reserved-word-symbol line 2, column 1, byte offset 6, span length 4: 'loop' is a reserved MASM instruction mnemonic and cannot be used as a procedure name.");
});

test("renders Phase 61E OPTION NOKEYWORD remains unsupported with reserved label", () => {
  const name = "phase61eOptionNoKeywordStillUnsupported";
  const source = `OPTION NOKEYWORD:<LOOP>
.code
main PROC
loop:
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assertNoExecutionComplete(json.simulatorMessages);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-error",
      code: "unsupported-option",
      message: "Unsupported OPTION form. Supported CASEMAP values: ALL, NONE. Recognized but unsupported: NOTPUBLIC.",
      line: 1,
      column: 1,
      byteOffset: 0,
      spanLength: 6
    },
    {
      kind: "assembly-error",
      code: "reserved-word-symbol",
      message: "'loop' is a reserved MASM instruction mnemonic and cannot be used as a code label.",
      line: 4,
      column: 1,
      byteOffset: 40,
      spanLength: 4
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-option line 1, column 1, byte offset 0, span length 6: Unsupported OPTION form. Supported CASEMAP values: ALL, NONE. Recognized but unsupported: NOTPUBLIC.\n[assembly-error] reserved-word-symbol line 4, column 1, byte offset 40, span length 4: 'loop' is a reserved MASM instruction mnemonic and cannot be used as a code label.");
});

test("renders Phase 61 direct JMP success exactly", () => {
  const name = "phase61DirectJmpSuccess";
  const source = `.code
main PROC
    mov eax, 1
    jmp done
    mov ebx, 2
done:
    mov ecx, 3
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.phase, 66);
  assert.equal(json.instructionCount, 3);
  assert.equal(json.executedInstructionCount, 3);
  assert.equal(json.attemptedNextInstructionIndex, null);
  assert.equal(json.currentInstructionIndex, 3);
  assert.equal(json.registers.EAX.hex, "00000001h");
  assert.equal(json.registers.EBX.hex, "00000000h");
  assert.equal(json.registers.ECX.hex, "00000003h");
  assert.equal(json.memoryChanges.length, 0, rawJson);
  assertNoMessageWithCode(json.simulatorMessages, "branch-runtime-deferred");
  assertMessageEquals(json.simulatorMessages.at(-1), {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("renders Phase 61A backward direct JMP instruction-limit diagnostic exactly", () => {
  const name = "phase61aBackwardDirectJmpInstructionLimit";
  const source = `.code
main PROC
start:
    inc eax
    jmp start
    mov ebx, 99
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, { MASM32_DIAGNOSTIC_INSTRUCTION_LIMIT: "4" });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assert.equal(json.instructionCount, 4);
  assert.equal(json.instructionLimit, 4);
  assert.equal(json.executedInstructionCount, 4);
  assert.equal(json.attemptedNextInstructionIndex, 0);
  assert.equal(json.currentInstructionIndex, 1);
  assert.equal(json.registers.EAX.hex, "00000002h");
  assert.equal(json.registers.EBX.hex, "00000000h");
  assertNoMessageWithCode(json.simulatorMessages, "branch-runtime-deferred");
  assertNoExecutionComplete(json.simulatorMessages);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "simulator-notice",
    code: "startup-state-notice",
    message: STARTUP_STATE_NOTICE_TEXT
  });
  assertMessageEquals(json.simulatorMessages[1], {
    kind: "runtime-error",
    code: "instruction-limit-exceeded",
    message: "Instruction limit exceeded: attempted to execute instruction #1 (limit: 4). Program stopped before executing that instruction.",
    line: 4,
    column: 5,
    byteOffset: 27,
    spanLength: 7
  });
  assertRenderedEquals(name, source, rawJson, rendered, `${STARTUP_STATE_NOTICE_RENDERED}\n\n[runtime-error] instruction-limit-exceeded line 4, column 5, byte offset 27, span length 7: Instruction limit exceeded: attempted to execute instruction #1 (limit: 4). Program stopped before executing that instruction.`);
});



test("renders Phase 64 equality conditional jump success exactly", () => {
  const name = "phase64EqualityConditionalJumpSuccess";
  const source = `.code
main PROC
    mov eax, 5
    cmp eax, 5
    je equal
    mov ebx, 1
equal:
    mov ebx, 2
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assert.equal(json.instructionCount, 4);
  assert.equal(json.executedInstructionCount, 4);
  assert.equal(json.registers.EBX.hex, "00000002h");
  assert.equal(json.memoryChanges.length, 0, rawJson);
  assertNoMessageWithCode(json.simulatorMessages, "branch-runtime-deferred");
  assertMessageEquals(json.simulatorMessages.at(-1), {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("renders Phase 64 equality conditional jump data-target diagnostic exactly", () => {
  const name = "phase64ConditionalJumpDataTarget";
  const source = `.data
value DWORD 1
.code
main PROC
    jnz value
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-branch-target",
    message: "JNZ target cannot be a data symbol. Direct conditional jumps accept only code labels with executable instruction targets.",
    line: 5,
    column: 9,
    byteOffset: 44,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-branch-target line 5, column 9, byte offset 44, span length 5: JNZ target cannot be a data symbol. Direct conditional jumps accept only code labels with executable instruction targets.");
});

test("renders Phase 64 equality conditional jump register-target diagnostic exactly", () => {
  const name = "phase64ConditionalJumpRegisterTarget";
  const source = `.code
main PROC
    je eax
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-branch-target-form",
    message: "JE register targets are not supported. Indirect branch behavior is deferred to a later branch phase.",
    line: 3,
    column: 8,
    byteOffset: 23,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-branch-target-form line 3, column 8, byte offset 23, span length 3: JE register targets are not supported. Indirect branch behavior is deferred to a later branch phase.");
});

test("renders Phase 64 equality conditional jump unknown-target diagnostic exactly", () => {
  const name = "phase64ConditionalJumpUnknownTarget";
  const source = `.code
main PROC
    je missing
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-branch-target",
    message: "JE target is not a known code label or procedure-entry label.",
    line: 3,
    column: 8,
    byteOffset: 23,
    spanLength: 7
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-branch-target line 3, column 8, byte offset 23, span length 7: JE target is not a known code label or procedure-entry label.");
});

test("renders Phase 64 equality conditional jump Irvine32-target diagnostic exactly", () => {
  const name = "phase64ConditionalJumpIrvineTarget";
  const source = `INCLUDE Irvine32.inc
.code
main PROC
    je exit
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-branch-target",
    message: "JE target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct conditional jumps accept only code labels.",
    line: 4,
    column: 8,
    byteOffset: 44,
    spanLength: 4
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-branch-target line 4, column 8, byte offset 44, span length 4: JE target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct conditional jumps accept only code labels.");
});

test("renders Phase 64 equality conditional jump memory-target diagnostic exactly", () => {
  const name = "phase64ConditionalJumpMemoryTarget";
  const source = `.code
main PROC
    jz [eax]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-branch-target-form",
    message: "JZ memory targets are not supported. Indirect branch behavior is deferred to a later branch phase.",
    line: 3,
    column: 8,
    byteOffset: 23,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-branch-target-form line 3, column 8, byte offset 23, span length 1: JZ memory targets are not supported. Indirect branch behavior is deferred to a later branch phase.");
});

test("renders Phase 64 equality conditional jump immediate-target diagnostic exactly", () => {
  const name = "phase64ConditionalJumpImmediateTarget";
  const source = `.code
main PROC
    jne 1234h
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-branch-target-form",
    message: "JNE immediate numeric targets are not supported. Use a direct code label target.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-branch-target-form line 3, column 9, byte offset 24, span length 5: JNE immediate numeric targets are not supported. Use a direct code label target.");
});


test("renders Phase 65 signed conditional jump memory-target diagnostic exactly", () => {
  const name = "phase65SignedConditionalJumpMemoryTarget";
  const source = `.code
main PROC
    jle [eax]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-branch-target-form",
    message: "JLE memory targets are not supported. Indirect branch behavior is deferred to a later branch phase.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-branch-target-form line 3, column 9, byte offset 24, span length 1: JLE memory targets are not supported. Indirect branch behavior is deferred to a later branch phase.");
});



test("renders Phase 66 unsigned conditional jump memory-target diagnostic exactly", () => {
  const name = "phase66UnsignedConditionalJumpMemoryTarget";
  const source = `.code
main PROC
    ja [eax]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-branch-target-form",
    message: "JA memory targets are not supported. Indirect branch behavior is deferred to a later branch phase.",
    line: 3,
    column: 8,
    byteOffset: 23,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-branch-target-form line 3, column 8, byte offset 23, span length 1: JA memory targets are not supported. Indirect branch behavior is deferred to a later branch phase.");
});

test("renders Phase 65 signed conditional jump Irvine32-target diagnostic exactly", () => {
  const name = "phase65SignedConditionalJumpIrvineTarget";
  const source = `INCLUDE Irvine32.inc
.code
main PROC
    jg exit
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-branch-target",
    message: "JG target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct conditional jumps accept only code labels.",
    line: 4,
    column: 8,
    byteOffset: 44,
    spanLength: 4
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-branch-target line 4, column 8, byte offset 44, span length 4: JG target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct conditional jumps accept only code labels.");
});

test("renders Phase 61 invalid direct JMP data-target diagnostic exactly", () => {
  const name = "phase61InvalidBranchDataTarget";
  const source = `.data
value DWORD 1
.code
main PROC
    jmp value
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-branch-target",
    message: "JMP target cannot be a data symbol. Direct JMP accepts only code labels with executable instruction targets.",
    line: 5,
    column: 9,
    byteOffset: 44,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-branch-target line 5, column 9, byte offset 44, span length 5: JMP target cannot be a data symbol. Direct JMP accepts only code labels with executable instruction targets.");
});


test("renders Phase 61 unsupported direct JMP register-target diagnostic exactly", () => {
  const name = "phase61UnsupportedBranchRegisterTarget";
  const source = `.code
main PROC
    jmp eax
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-branch-target-form",
    message: "JMP register targets are not supported for direct JMP. Indirect branch behavior is deferred to a later branch phase.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-branch-target-form line 3, column 9, byte offset 24, span length 3: JMP register targets are not supported for direct JMP. Indirect branch behavior is deferred to a later branch phase.");
});

test("renders Phase 61 missing direct JMP target diagnostic exactly", () => {
  const name = "phase61MissingBranchTarget";
  const source = `.code
main PROC
    jmp
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.phase, 66);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "expected-operand",
    message: "JMP requires a direct code-label target operand.",
    line: 3,
    column: 8,
    byteOffset: 23,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] expected-operand line 3, column 8, byte offset 23, span length 1: JMP requires a direct code-label target operand.");
});


test("renders every Phase 61 rejected direct JMP target class exactly", () => {
  const cases = [
    {
      name: "phase61InvalidBranchEquateTarget",
      source: `COUNT = 1
.code
main PROC
    jmp COUNT
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "invalid-branch-target",
        message: "JMP target cannot be a numeric equate or constant symbol. Direct JMP accepts only code labels.",
        line: 4,
        column: 9,
        byteOffset: 34,
        spanLength: 5
      },
      rendered: "[assembly-error] invalid-branch-target line 4, column 9, byte offset 34, span length 5: JMP target cannot be a numeric equate or constant symbol. Direct JMP accepts only code labels."
    },
    {
      name: "phase61InvalidBranchUnknownTarget",
      source: `.code
main PROC
    jmp missing
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "invalid-branch-target",
        message: "JMP target is not a known code label or procedure-entry label.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 7
      },
      rendered: "[assembly-error] invalid-branch-target line 3, column 9, byte offset 24, span length 7: JMP target is not a known code label or procedure-entry label."
    },
    {
      name: "phase61InvalidBranchNoExecutableTarget",
      source: `.code
main PROC
    jmp empty
empty:
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "invalid-branch-target",
        message: "JMP target label has no executable instruction target.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 5
      },
      rendered: "[assembly-error] invalid-branch-target line 3, column 9, byte offset 24, span length 5: JMP target label has no executable instruction target."
    },
    {
      name: "phase61InvalidBranchIrvineTarget",
      source: `INCLUDE Irvine32.inc
.code
main PROC
    jmp exit
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "invalid-branch-target",
        message: "JMP target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct JMP accepts only code labels.",
        line: 4,
        column: 9,
        byteOffset: 45,
        spanLength: 4
      },
      rendered: "[assembly-error] invalid-branch-target line 4, column 9, byte offset 45, span length 4: JMP target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct JMP accepts only code labels."
    },
    {
      name: "phase61UnsupportedBranchMemoryTarget",
      source: `.code
main PROC
    jmp DWORD PTR [eax]
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "unsupported-branch-target-form",
        message: "JMP memory targets are not supported for direct JMP. Indirect branch behavior is deferred to a later branch phase.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 5
      },
      rendered: "[assembly-error] unsupported-branch-target-form line 3, column 9, byte offset 24, span length 5: JMP memory targets are not supported for direct JMP. Indirect branch behavior is deferred to a later branch phase."
    },
    {
      name: "phase61UnsupportedBranchDistanceTarget",
      source: `.code
main PROC
    jmp SHORT target
target:
    mov eax, 1
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "unsupported-branch-target-form",
        message: "JMP distance and type overrides such as SHORT, NEAR PTR, and FAR PTR are deferred to a later branch phase. Use a plain code label target.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 5
      },
      rendered: "[assembly-error] unsupported-branch-target-form line 3, column 9, byte offset 24, span length 5: JMP distance and type overrides such as SHORT, NEAR PTR, and FAR PTR are deferred to a later branch phase. Use a plain code label target."
    },
    {
      name: "phase61UnsupportedBranchImmediateTarget",
      source: `.code
main PROC
    jmp 42
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "unsupported-branch-target-form",
        message: "JMP immediate numeric targets are not supported. Use a direct code label target.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 2
      },
      rendered: "[assembly-error] unsupported-branch-target-form line 3, column 9, byte offset 24, span length 2: JMP immediate numeric targets are not supported. Use a direct code label target."
    },
    {
      name: "phase61InvalidBranchDirectiveTarget",
      source: `.code
main PROC
    jmp .data
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "invalid-branch-target",
        message: "JMP target cannot be a directive name. Use a code label with an executable target instruction.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 5
      },
      rendered: "[assembly-error] invalid-branch-target line 3, column 9, byte offset 24, span length 5: JMP target cannot be a directive name. Use a code label with an executable target instruction."
    },
    {
      name: "phase61InvalidBranchInstructionTarget",
      source: `.code
main PROC
    jmp mov
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "invalid-branch-target",
        message: "JMP target cannot be an instruction mnemonic. Use a non-reserved code label with an executable target instruction.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 3
      },
      rendered: "[assembly-error] invalid-branch-target line 3, column 9, byte offset 24, span length 3: JMP target cannot be an instruction mnemonic. Use a non-reserved code label with an executable target instruction."
    },
    {
      name: "phase61InvalidBranchWindowsApiTarget",
      source: `.code
main PROC
    jmp ExitProcess
main ENDP
END main
`,
      expected: {
        kind: "assembly-error",
        code: "invalid-branch-target",
        message: "JMP target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct JMP accepts only code labels.",
        line: 3,
        column: 9,
        byteOffset: 24,
        spanLength: 11
      },
      rendered: "[assembly-error] invalid-branch-target line 3, column 9, byte offset 24, span length 11: JMP target cannot be an Irvine32 virtual routine, virtual terminator, Windows/API name, or external symbol. Direct JMP accepts only code labels."
    }
  ];

  for (const item of cases) {
    const { json, rawJson, rendered } = runFixture(item.name, item.source);
    assertRunStatus(json, false, "parse-error");
    assert.equal(json.phase, 66);
    assertMessageEquals(json.simulatorMessages[0], item.expected);
    assertNoExecutionComplete(json.simulatorMessages);
    assertRenderedEquals(item.name, item.source, rawJson, rendered, item.rendered);
  }
});


test("renders Phase 58 duplicate and conflicting code-label diagnostics exactly", () => {
  const duplicateName = "phase58DuplicateLabel";
  const duplicateSource = fixtureSource(duplicateName);
  const duplicateResult = runFixture(duplicateName, duplicateSource);
  assertRunStatus(duplicateResult.json, false, "parse-error");
  assert.equal(duplicateResult.json.phase, 66);
  assertMessageEquals(duplicateResult.json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "duplicate-label",
    message: "Duplicate code label `start`; first defined at line 3, column 1.",
    line: 5,
    column: 1,
    byteOffset: 38,
    spanLength: 5
  });
  assertNoExecutionComplete(duplicateResult.json.simulatorMessages);
  assertRenderedEquals(
    duplicateName,
    duplicateSource,
    duplicateResult.rawJson,
    duplicateResult.rendered,
    "[assembly-error] duplicate-label line 5, column 1, byte offset 38, span length 5: Duplicate code label `start`; first defined at line 3, column 1."
  );

  const foldedDuplicateName = "phase58FoldedDuplicateLabel";
  const foldedDuplicateSource = fixtureSource(foldedDuplicateName);
  const foldedDuplicateResult = runFixture(foldedDuplicateName, foldedDuplicateSource);
  assertRunStatus(foldedDuplicateResult.json, false, "parse-error");
  assertMessageEquals(foldedDuplicateResult.json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "duplicate-label",
    message: "code label `spin` conflicts with `Spin` because user-defined symbols are case-insensitive under the active CASEMAP policy; first defined at line 3, column 1.",
    line: 5,
    column: 1,
    byteOffset: 37,
    spanLength: 4
  });
  assertNoExecutionComplete(foldedDuplicateResult.json.simulatorMessages);
  assertRenderedEquals(
    foldedDuplicateName,
    foldedDuplicateSource,
    foldedDuplicateResult.rawJson,
    foldedDuplicateResult.rendered,
    "[assembly-error] duplicate-label line 5, column 1, byte offset 37, span length 4: code label `spin` conflicts with `Spin` because user-defined symbols are case-insensitive under the active CASEMAP policy; first defined at line 3, column 1."
  );

  const dataConflictName = "phase58LabelDataConflict";
  const dataConflictSource = fixtureSource(dataConflictName);
  const dataConflictResult = runFixture(dataConflictName, dataConflictSource);
  assertRunStatus(dataConflictResult.json, false, "parse-error");
  assertMessageEquals(dataConflictResult.json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "label-symbol-conflict",
    message: "code label `value` conflicts with existing data symbol `value` defined at line 2, column 1.",
    line: 5,
    column: 1,
    byteOffset: 36,
    spanLength: 5
  });
  assertNoExecutionComplete(dataConflictResult.json.simulatorMessages);
  assertRenderedEquals(
    dataConflictName,
    dataConflictSource,
    dataConflictResult.rawJson,
    dataConflictResult.rendered,
    "[assembly-error] label-symbol-conflict line 5, column 1, byte offset 36, span length 5: code label `value` conflicts with existing data symbol `value` defined at line 2, column 1."
  );

  const procedureThenOrdinaryName = "phase58ProcedureThenOrdinaryLabelConflict";
  const procedureThenOrdinarySource = fixtureSource(procedureThenOrdinaryName);
  const procedureThenOrdinaryResult = runFixture(procedureThenOrdinaryName, procedureThenOrdinarySource);
  assertRunStatus(procedureThenOrdinaryResult.json, false, "parse-error");
  assertMessageEquals(procedureThenOrdinaryResult.json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "duplicate-label",
    message: "Duplicate code label `main`; first defined at line 2, column 1.",
    line: 3,
    column: 1,
    byteOffset: 16,
    spanLength: 4
  });
  assertNoExecutionComplete(procedureThenOrdinaryResult.json.simulatorMessages);
  assertRenderedEquals(
    procedureThenOrdinaryName,
    procedureThenOrdinarySource,
    procedureThenOrdinaryResult.rawJson,
    procedureThenOrdinaryResult.rendered,
    "[assembly-error] duplicate-label line 3, column 1, byte offset 16, span length 4: Duplicate code label `main`; first defined at line 2, column 1."
  );

  const procedureConflictName = "phase58ProcedureLabelConflict";
  const procedureConflictSource = fixtureSource(procedureConflictName);
  const procedureConflictResult = runFixture(procedureConflictName, procedureConflictSource);
  assertRunStatus(procedureConflictResult.json, false, "parse-error");
  assertMessageEquals(procedureConflictResult.json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "duplicate-label",
    message: "Duplicate procedure-entry label `other`; first defined at line 3, column 1.",
    line: 5,
    column: 1,
    byteOffset: 33,
    spanLength: 5
  });
  assertNoExecutionComplete(procedureConflictResult.json.simulatorMessages);
  assertRenderedEquals(
    procedureConflictName,
    procedureConflictSource,
    procedureConflictResult.rawJson,
    procedureConflictResult.rendered,
    "[assembly-error] duplicate-label line 5, column 1, byte offset 33, span length 5: Duplicate procedure-entry label `other`; first defined at line 3, column 1."
  );
});

test("renders post-code section diagnostic with stable wording exactly", () => {
  const name = "postCodeSection";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "unsupported-section",
    message: "Unsupported section order. Place optional .data, .DATA?, and .CONST declarations together before .code; do not repeat a data-section directive after code has started.",
    line: 3,
    column: 1,
    byteOffset: 16,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unsupported-section line 3, column 1, byte offset 16, span length 5: Unsupported section order. Place optional .data, .DATA?, and .CONST declarations together before .code; do not repeat a data-section directive after code has started.");
});

test("renders text EQU diagnostic with stable wording exactly", () => {
  const name = "textEquate";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-equate",
    message: "Text EQU constants are not accepted. Define numeric equates with NAME EQU constant-expression, for example COUNT EQU 4; text substitution forms such as NAME EQU <text> and TEXTEQU are not implemented.",
    line: 1,
    column: 10,
    byteOffset: 9,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-equate line 1, column 10, byte offset 9, span length 1: Text EQU constants are not accepted. Define numeric equates with NAME EQU constant-expression, for example COUNT EQU 4; text substitution forms such as NAME EQU <text> and TEXTEQU are not implemented.");
});

test("renders known Irvine32 routine diagnostic exactly", () => {
  const name = "irvine32UnsupportedRoutine";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.virtualIncludes.irvine32, true);
  assert.ok(json.virtualIncludes.irvine32SymbolCount > 0);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-irvine32-routine",
      message: "Recognized Irvine32 routine, but executable behavior for this routine is deferred to the routine-specific Irvine32 phases.",
      line: 4,
      column: 5,
      byteOffset: 41,
      spanLength: 11
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-irvine32-routine line 4, column 5, byte offset 41, span length 11: Recognized Irvine32 routine, but executable behavior for this routine is deferred to the routine-specific Irvine32 phases.");
});

test("renders exit without Irvine32 include diagnostic exactly", () => {
  const name = "exitWithoutIrvine32Include";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-error",
      code: "unknown-instruction",
      message: "Unknown instruction or virtual Irvine32 terminator. Add INCLUDE Irvine32.inc to use exit.",
      line: 3,
      column: 5,
      byteOffset: 20,
      spanLength: 4
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] unknown-instruction line 3, column 5, byte offset 20, span length 4: Unknown instruction or virtual Irvine32 terminator. Add INCLUDE Irvine32.inc to use exit.");
});

test("renders exit operand diagnostic exactly", () => {
  const name = "exitWithOperand";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.equal(json.virtualIncludes.irvine32, true);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-error",
      code: "invalid-instruction-operands",
      message: "exit does not take operands.",
      line: 4,
      column: 10,
      byteOffset: 46,
      spanLength: 1
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 4, column 10, byte offset 46, span length 1: exit does not take operands.");
});

test("Phase 57O renders invalid NOP operand diagnostics exactly", () => {
  const cases = [
    {
      name: "phase57o-nop-byte-register-operand",
      line: "    nop al",
      code: "invalid-operand-size",
      message: "NOP encoding operand size is invalid. NOP has no 8-bit encoding-operand form. Did you mean to use the ordinary, zero-operand \"NOP\"?",
      column: 9,
      byteOffset: 24,
      spanLength: 2
    },
    {
      name: "phase57o-nop-high-byte-register-operand",
      line: "    nop ah",
      code: "invalid-operand-size",
      message: "NOP encoding operand size is invalid. NOP has no 8-bit encoding-operand form. Did you mean to use the ordinary, zero-operand \"NOP\"?",
      column: 9,
      byteOffset: 24,
      spanLength: 2
    },
    {
      name: "phase57o-nop-immediate-operand",
      line: "    nop 1",
      code: "invalid-instruction-operands",
      message: "NOP does not accept an immediate operand. Use zero-operand \"NOP\", or a \"NOP\" with a 16-bit/32-bit register, or WORD/SWORD/DWORD/SDWORD PTR.",
      column: 9,
      byteOffset: 24,
      spanLength: 1
    },
    {
      name: "phase57o-nop-two-operands",
      line: "    nop eax, ebx",
      code: "invalid-instruction-operands",
      message: "NOP accepts at most one operand.",
      column: 12,
      byteOffset: 27,
      spanLength: 1
    },
    {
      name: "phase57o-nop-untyped-memory-operand",
      line: "    nop [eax]",
      code: "ambiguous-memory-width",
      message: "NOP memory encoding operand must have an explicit size. Use zero-operand \"NOP\", or a \"NOP\" with a 16-bit/32-bit register, or WORD/SWORD/DWORD/SDWORD PTR.",
      column: 9,
      byteOffset: 24,
      spanLength: 1
    },
    {
      name: "phase57o-nop-byte-ptr-operand",
      line: "    nop BYTE PTR [eax]",
      code: "invalid-operand-size",
      message: "NOP encoding operand size is invalid. NOP has no 8-bit encoding-operand form. Did you mean to use the ordinary, zero-operand \"NOP\"?",
      column: 9,
      byteOffset: 24,
      spanLength: 4
    },
    {
      name: "phase57o-nop-sbyte-ptr-operand",
      line: "    nop SBYTE PTR [eax]",
      code: "invalid-operand-size",
      message: "NOP encoding operand size is invalid. NOP has no 8-bit encoding-operand form. Did you mean to use the ordinary, zero-operand \"NOP\"?",
      column: 9,
      byteOffset: 24,
      spanLength: 5
    },


    {
      name: "phase57o-nop-qword-ptr-operand",
      line: "    nop QWORD PTR [eax]",
      code: "invalid-operand-size",
      message: "NOP encoding operand size is invalid. QWORD/SQWORD are not supported in MASM32 Educational Mode. Use zero-operand \"NOP\", or a \"NOP\" with a 16-bit/32-bit register, or WORD/SWORD/DWORD/SDWORD PTR.",
      column: 9,
      byteOffset: 24,
      spanLength: 5
    },
    {
      name: "phase57o-nop-sqword-ptr-operand",
      line: "    nop SQWORD PTR [eax]",
      code: "invalid-operand-size",
      message: "NOP encoding operand size is invalid. QWORD/SQWORD are not supported in MASM32 Educational Mode. Use zero-operand \"NOP\", or a \"NOP\" with a 16-bit/32-bit register, or WORD/SWORD/DWORD/SDWORD PTR.",
      column: 9,
      byteOffset: 24,
      spanLength: 6
    }
  ];

  for (const diagnosticCase of cases) {
    const source = `.code
main PROC
${diagnosticCase.line}
main ENDP
END main
`;
    const { json, rawJson, rendered } = runFixture(diagnosticCase.name, source);
    assertRunStatus(json, false, "parse-error");
    assert.deepEqual(json.simulatorMessages, [
      {
        kind: "assembly-error",
        code: diagnosticCase.code,
        message: diagnosticCase.message,
        line: 3,
        column: diagnosticCase.column,
        byteOffset: diagnosticCase.byteOffset,
        spanLength: diagnosticCase.spanLength
      }
    ]);
    assertNoExecutionComplete(json.simulatorMessages);
    assertRenderedEquals(diagnosticCase.name, source, rawJson, rendered, `[assembly-error] ${diagnosticCase.code} line 3, column ${diagnosticCase.column}, byte offset ${diagnosticCase.byteOffset}, span length ${diagnosticCase.spanLength}: ${diagnosticCase.message}`);
  }
});

test("renders unsupported INVOKE diagnostic exactly", () => {
  const name = "unsupportedFeature";
  const source = fixtureSource("unsupportedFeature");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-invoke",
    message: "INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.",
    line: 3,
    column: 5,
    byteOffset: 20,
    spanLength: 6
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-invoke line 3, column 5, byte offset 20, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.");
});

test("Phase 57R renders INVOKE, ADDR, and MASM32 runtime diagnostics exactly", () => {
  const name = "phase57r-invoke-stdout";
  const source = `.data
titleMsg BYTE "Hello", 0
.code
main PROC
    invoke StdOut, addr titleMsg
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-invoke",
      message: "INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.",
      line: 5,
      column: 5,
      byteOffset: 51,
      spanLength: 6
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-addr",
      message: "ADDR operands are not implemented; ADDR depends on INVOKE/procedure argument lowering and future calling-convention support.",
      line: 5,
      column: 20,
      byteOffset: 66,
      spanLength: 4
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-masm32-runtime-routine",
      message: "StdOut is an external MASM32 runtime-style routine. MASM32 Educational Mode does not link MASM32 runtime libraries or execute external routines.",
      line: 5,
      column: 12,
      byteOffset: 58,
      spanLength: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-invoke line 5, column 5, byte offset 51, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.\n[unsupported-feature] unsupported-addr line 5, column 20, byte offset 66, span length 4: ADDR operands are not implemented; ADDR depends on INVOKE/procedure argument lowering and future calling-convention support.\n[unsupported-feature] unsupported-masm32-runtime-routine line 5, column 12, byte offset 58, span length 6: StdOut is an external MASM32 runtime-style routine. MASM32 Educational Mode does not link MASM32 runtime libraries or execute external routines.");
});

test("Phase 57R renders CRT routine diagnostic exactly", () => {
  const name = "phase57r-invoke-crt";
  const source = `.code
main PROC
    invoke crt_printf, addr numberFmt, counter
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-invoke",
      message: "INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.",
      line: 3,
      column: 5,
      byteOffset: 20,
      spanLength: 6
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-addr",
      message: "ADDR operands are not implemented; ADDR depends on INVOKE/procedure argument lowering and future calling-convention support.",
      line: 3,
      column: 24,
      byteOffset: 39,
      spanLength: 4
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-crt-routine",
      message: "crt_printf is a C runtime formatted-output routine. MASM32 Educational Mode does not link or execute CRT routines.",
      line: 3,
      column: 12,
      byteOffset: 27,
      spanLength: 10
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-invoke line 3, column 5, byte offset 20, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.\n[unsupported-feature] unsupported-addr line 3, column 24, byte offset 39, span length 4: ADDR operands are not implemented; ADDR depends on INVOKE/procedure argument lowering and future calling-convention support.\n[unsupported-feature] unsupported-crt-routine line 3, column 12, byte offset 27, span length 10: crt_printf is a C runtime formatted-output routine. MASM32 Educational Mode does not link or execute CRT routines.");
});

test("Phase 57R renders WinAPI ExitProcess diagnostic exactly", () => {
  const name = "phase57r-invoke-exitprocess";
  const source = `INCLUDE Irvine32.inc
.code
main PROC
    invoke ExitProcess, 0
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-invoke",
      message: "INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.",
      line: 4,
      column: 5,
      byteOffset: 41,
      spanLength: 6
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-winapi-execution",
      message: "ExitProcess is WinAPI/external process termination behavior. MASM32 Educational Mode does not execute Windows API calls; this is not the virtual Irvine32 exit terminator.",
      line: 4,
      column: 12,
      byteOffset: 48,
      spanLength: 11
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-invoke line 4, column 5, byte offset 41, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.\n[unsupported-feature] unsupported-winapi-execution line 4, column 12, byte offset 48, span length 11: ExitProcess is WinAPI/external process termination behavior. MASM32 Educational Mode does not execute Windows API calls; this is not the virtual Irvine32 exit terminator.");
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

test("renders Phase 57-CORR1 cross-region CONST overlap diagnostic exactly", () => {
  const name = "constCrossRegionWriteOverlap";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.phase, 66);
  assert.equal(json.instructionCount, 3);
  assert.deepEqual(json.memoryChanges, []);
  assert.equal(json.registers.EAX.hex, "005FFFFEh");
  assert.equal(json.registers.EBX.hex, "00000001h");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "region-boundary-crossing",
    message: "Cross-region memory write at 005FFFFEh for 4 bytes. The memory address range 005FFFFEh..00600001h crosses/overlaps a protected memory region, .CONST, that starts at 00600000h. This is not allowed; program stopped before access.",
    line: 9
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] region-boundary-crossing line 9: Cross-region memory write at 005FFFFEh for 4 bytes. The memory address range 005FFFFEh..00600001h crosses/overlaps a protected memory region, .CONST, that starts at 00600000h. This is not allowed; program stopped before access.");
});


test("renders Phase 57-CORR1 cross-region CONST read diagnostic exactly", () => {
  const name = "constCrossRegionReadOverlap";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.phase, 66);
  assert.deepEqual(json.memoryChanges, []);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "region-boundary-crossing",
    message: "Cross-region memory read at 005FFFFEh for 4 bytes. The memory address range 005FFFFEh..00600001h crosses/overlaps a protected memory region, .CONST, that starts at 00600000h. This is not allowed; program stopped before access.",
    line: 8
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] region-boundary-crossing line 8: Cross-region memory read at 005FFFFEh for 4 bytes. The memory address range 005FFFFEh..00600001h crosses/overlaps a protected memory region, .CONST, that starts at 00600000h. This is not allowed; program stopped before access.");
});

test("renders INC immediate-destination diagnostic exactly", () => {
  const name = "incImmediateDestination";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "INC requires a register or memory destination.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 9, byte offset 24, span length 1: INC requires a register or memory destination.");
});

test("renders INC extra-operand diagnostic exactly", () => {
  const name = "incExtraOperand";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "INC takes exactly one register or memory operand.",
    line: 3,
    column: 12,
    byteOffset: 27,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 12, byte offset 27, span length 1: INC takes exactly one register or memory operand.");
});

test("renders INC ambiguous memory width diagnostic exactly", () => {
  const name = "incAmbiguousMemoryWidth";
  const source = fixtureSource("incAmbiguousMemoryWidth");
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders DEC direct CONST write diagnostic exactly", () => {
  const name = "decConstStaticWrite";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "const-write",
    message: "Cannot write to .CONST data. Constant data is read-only.",
    line: 5,
    column: 9,
    byteOffset: 46,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] const-write line 5, column 9, byte offset 46, span length 5: Cannot write to .CONST data. Constant data is read-only.");
});

test("renders INC runtime CONST permission diagnostic exactly without memory change rows", () => {
  const name = "incConstRuntimeWrite";
  const source = fixtureSource("incConstRuntimeWrite");
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

test("renders DEC invalid-address diagnostic exactly", () => {
  const name = "decInvalidAddress";
  const source = fixtureSource("decInvalidAddress");
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

test("renders AND ambiguous memory width diagnostic exactly", () => {
  const name = "andAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders OR immediate-destination diagnostic exactly", () => {
  const name = "orImmediateDestination";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "OR requires a register or memory destination.",
    line: 3,
    column: 8,
    byteOffset: 23,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 8, byte offset 23, span length 1: OR requires a register or memory destination.");
});

test("renders XOR memory-to-memory diagnostic exactly", () => {
  const name = "xorMemoryToMemory";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "XOR does not support memory-to-memory operands.",
    line: 6,
    column: 16,
    byteOffset: 65,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 6, column 16, byte offset 65, span length 5: XOR does not support memory-to-memory operands.");
});

test("renders AND direct CONST write diagnostic exactly", () => {
  const name = "andConstDirectWrite";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "const-write",
    message: "Cannot write to .CONST data. Constant data is read-only.",
    line: 5,
    column: 9,
    byteOffset: 46,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] const-write line 5, column 9, byte offset 46, span length 5: Cannot write to .CONST data. Constant data is read-only.");
});

test("renders OR runtime CONST permission diagnostic exactly without memory change rows", () => {
  const name = "orConstRuntimeWrite";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "execution-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "permission-denied",
    message: "Memory write at 00600000h for 4 bytes is not permitted in .const.",
    line: 6
  });
  assert.equal(json.memoryChanges.length, 0);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] permission-denied line 6: Memory write at 00600000h for 4 bytes is not permitted in .const.");
});

test("renders AND invalid source-address diagnostic exactly", () => {
  const name = "andInvalidSourceAddress";
  const source = fixtureSource(name);
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

test("renders XOR invalid destination-address diagnostic exactly", () => {
  const name = "xorInvalidAddress";
  const source = fixtureSource(name);
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


test("renders SHL ambiguous memory-width diagnostic exactly", () => {
  const name = "shlAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders SHL invalid count-register diagnostic exactly", () => {
  const name = "shlInvalidCountRegister";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "SHL count must be an immediate byte count or CL.",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 14, byte offset 29, span length 3: SHL count must be an immediate byte count or CL.");
});

test("renders SHL undefined modeled flag warning exactly", () => {
  const name = "shlUndefinedWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
      line: 4,
      column: 5,
      byteOffset: 34,
      spanLength: 9
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 4, column 5, byte offset 34, span length 9: SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders SAL undefined modeled flag warning exactly", () => {
  const name = "salUndefinedWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 3);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SAL count 2 has effective count 2 for a 32-bit destination. CF, ZF, and SF were updated from the result. OF is architecturally undefined because the effective count is greater than 1. The simulator preserved OF deterministically.",
      line: 5,
      column: 5,
      byteOffset: 66,
      spanLength: 11
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 5, column 5, byte offset 66, span length 11: SAL count 2 has effective count 2 for a 32-bit destination. CF, ZF, and SF were updated from the result. OF is architecturally undefined because the effective count is greater than 1. The simulator preserved OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders SHL strict undefined modeled flag diagnostic exactly", () => {
  const name = "shlUndefinedStrict";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SHIFT_VALIDATION: "strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "undefined-shift-flag",
    message: "SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    line: 4,
    column: 5,
    byteOffset: 34,
    spanLength: 9
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] undefined-shift-flag line 4, column 5, byte offset 34, span length 9: SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.");
});


test("renders SHR ambiguous memory-width diagnostic exactly", () => {
  const name = "shrAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders SHR invalid count-register diagnostic exactly", () => {
  const name = "shrInvalidCountRegister";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "SHR count must be an immediate byte count or CL.",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 14, byte offset 29, span length 3: SHR count must be an immediate byte count or CL.");
});

test("renders SHR undefined modeled flag warning exactly", () => {
  const name = "shrUndefinedWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SHR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
      line: 4,
      column: 5,
      byteOffset: 36,
      spanLength: 9
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 4, column 5, byte offset 36, span length 9: SHR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders SHR strict undefined modeled flag diagnostic exactly", () => {
  const name = "shrUndefinedStrict";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SHIFT_VALIDATION: "strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "undefined-shift-flag",
    message: "SHR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    line: 4,
    column: 5,
    byteOffset: 36,
    spanLength: 9
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] undefined-shift-flag line 4, column 5, byte offset 36, span length 9: SHR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.");
});

test("renders SAR ambiguous memory-width diagnostic exactly", () => {
  const name = "sarAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders SAR invalid count-register diagnostic exactly", () => {
  const name = "sarInvalidCountRegister";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "SAR count must be an immediate byte count or CL.",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 14, byte offset 29, span length 3: SAR count must be an immediate byte count or CL.");
});

test("renders SAR undefined modeled flag warning exactly", () => {
  const name = "sarUndefinedWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SAR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
      line: 4,
      column: 5,
      byteOffset: 36,
      spanLength: 9
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 4, column 5, byte offset 36, span length 9: SAR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders SAR strict undefined modeled flag diagnostic exactly", () => {
  const name = "sarUndefinedStrict";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SHIFT_VALIDATION: "strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "undefined-shift-flag",
    message: "SAR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    line: 4,
    column: 5,
    byteOffset: 36,
    spanLength: 9
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] undefined-shift-flag line 4, column 5, byte offset 36, span length 9: SAR count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.");
});


test("renders ROL ambiguous memory-width diagnostic exactly", () => {
  const name = "rolAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders ROL invalid count-register diagnostic exactly", () => {
  const name = "rolInvalidCountRegister";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "ROL count must be an immediate byte count or CL.",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 14, byte offset 29, span length 3: ROL count must be an immediate byte count or CL.");
});

test("renders ROL missing count diagnostic exactly", () => {
  const name = "rolMissingCount";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "ROL takes exactly two operands.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 9, byte offset 24, span length 3: ROL takes exactly two operands.");
});


test("renders ROL undefined modeled flag warning exactly", () => {
  const name = "rolUndefinedWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-modeled-flag",
      message: "ROL count 8 has effective count 8 and rotate amount 0 for an 8-bit destination. CF was updated from the least significant bit of the rotated result. ZF and SF were preserved because ROL does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.",
      line: 4,
      column: 5,
      byteOffset: 36,
      spanLength: 9
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-modeled-flag line 4, column 5, byte offset 36, span length 9: ROL count 8 has effective count 8 and rotate amount 0 for an 8-bit destination. CF was updated from the least significant bit of the rotated result. ZF and SF were preserved because ROL does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders ROL warning under strict shift validation without runtime error", () => {
  const name = "rolUndefinedStrictStillWarns";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SHIFT_VALIDATION: "strict"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.equal(json.simulatorMessages[0].code, "undefined-modeled-flag");
  assert.equal(json.simulatorMessages[1].code, "execution-complete");
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-modeled-flag line 4, column 5, byte offset 36, span length 9: ROL count 8 has effective count 8 and rotate amount 0 for an 8-bit destination. CF was updated from the least significant bit of the rotated result. ZF and SF were preserved because ROL does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders ROR ambiguous memory-width diagnostic exactly", () => {
  const name = "rorAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders ROR invalid count-register diagnostic exactly", () => {
  const name = "rorInvalidCountRegister";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "ROR count must be an immediate byte count or CL.",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 14, byte offset 29, span length 3: ROR count must be an immediate byte count or CL.");
});

test("renders ROR missing count diagnostic exactly", () => {
  const name = "rorMissingCount";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "ROR takes exactly two operands.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 9, byte offset 24, span length 3: ROR takes exactly two operands.");
});


test("renders successful ROR execution exactly", () => {
  const name = "rorSuccess";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assert.equal(json.registers.EAX.hex, "00000080h");
  assertRenderedEquals(name, source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("renders ROR invalid destination-address diagnostic exactly", () => {
  const name = "rorRuntimeInvalidAddress";
  const source = fixtureSource(name);
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


test("renders MUL ambiguous memory-width diagnostic exactly", () => {
  const name = "mulAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders MUL immediate-source diagnostic exactly", () => {
  const name = "mulImmediateSource";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "MUL requires a register or memory source.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 9, byte offset 24, span length 1: MUL requires a register or memory source.");
});

test("renders MUL extra-operand diagnostic exactly", () => {
  const name = "mulExtraOperand";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "MUL takes exactly one register or memory operand.",
    line: 3,
    column: 12,
    byteOffset: 27,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 12, byte offset 27, span length 1: MUL takes exactly one register or memory operand.");
});

test("renders MUL invalid source-address diagnostic exactly", () => {
  const name = "mulRuntimeInvalidAddress";
  const source = fixtureSource(name);
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

test("renders MUL QWORD source diagnostic exactly", () => {
  const name = "mulQwordSource";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-ptr-width",
    message: "QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.",
    line: 5,
    column: 9,
    byteOffset: 40,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-ptr-width line 5, column 9, byte offset 40, span length 5: QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.");
});

test("renders IMUL ambiguous memory-width diagnostic exactly", () => {
  const name = "imulAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 10,
    byteOffset: 25,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 10, byte offset 25, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders IMUL immediate-source diagnostic exactly", () => {
  const name = "imulImmediateSource";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "IMUL requires a register or memory source.",
    line: 3,
    column: 10,
    byteOffset: 25,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 10, byte offset 25, span length 1: IMUL requires a register or memory source.");
});

test("renders IMUL reg-immediate rejection diagnostic exactly", () => {
  const name = "imulRegImmediate";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "IMUL reg, imm is not supported in Phase 55; use a register or memory source, or the three-operand reg, r/m, imm form.",
    line: 3,
    column: 15,
    byteOffset: 30,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 15, byte offset 30, span length 1: IMUL reg, imm is not supported in Phase 55; use a register or memory source, or the three-operand reg, r/m, imm form.");
});

test("renders IMUL immediate range diagnostic exactly", () => {
  const name = "imulImmediateOutOfRange";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "immediate-out-of-range",
    message: "IMUL immediate value does not fit the signed destination operand width.",
    line: 3,
    column: 20,
    byteOffset: 35,
    spanLength: 10
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] immediate-out-of-range line 3, column 20, byte offset 35, span length 10: IMUL immediate value does not fit the signed destination operand width.");
});

test("renders IMUL invalid source-address diagnostic exactly", () => {
  const name = "imulRuntimeInvalidAddress";
  const source = fixtureSource(name);
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

test("renders IMUL QWORD source diagnostic exactly", () => {
  const name = "imulQwordSource";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-ptr-width",
    message: "QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.",
    line: 5,
    column: 10,
    byteOffset: 41,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-ptr-width line 5, column 10, byte offset 41, span length 5: QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.");
});

test("renders IMUL default uninitialized-read warning exactly", () => {
  const name = "imulUninitializedRead";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 6,
      column: 10,
      byteOffset: 57,
      spanLength: 1,
      sourceLocation: { line: 6, column: 10, byteOffset: 57, spanLength: 1 },
      symbolName: "x",
      accessStartAddress: "00500000h",
      accessEndAddress: "00500003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 6, column 10, byte offset 57, span length 1: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.");
});

test("renders Phase 57J CONST declaration warning and read warning exactly", () => {
  const name = "phase57jConstUninitializedDeclarationWarn";
  const source = `.CONST
limit DWORD ?
.code
main PROC
    mov eax, limit
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "const-uninitialized-storage",
      message: ".CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.",
      line: 2,
      column: 1,
      byteOffset: 7,
      spanLength: 5
    },
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00600000h..00600003h reads 4 bytes from limit + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 5,
      column: 14,
      byteOffset: 50,
      spanLength: 5,
      sourceLocation: { line: 5, column: 14, byteOffset: 50, spanLength: 5 },
      symbolName: "limit",
      accessStartAddress: "00600000h",
      accessEndAddress: "00600003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] const-uninitialized-storage line 2, column 1, byte offset 7, span length 5: .CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.\n[simulator-warning] uninitialized-read line 5, column 14, byte offset 50, span length 5: Memory read range 00600000h..00600003h reads 4 bytes from limit + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.");
});

test("renders Phase 57J CONST declaration error policy exactly", () => {
  const name = "phase57jConstUninitializedDeclarationError";
  const source = `.CONST
limit DWORD ?
.code
main PROC
    mov eax, limit
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE: "error"
  });
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "assembly-error",
      code: "const-uninitialized-storage",
      message: ".CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.",
      line: 2,
      column: 1,
      byteOffset: 7,
      spanLength: 5
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] const-uninitialized-storage line 2, column 1, byte offset 7, span length 5: .CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.");
});

test("renders Phase 57J CONST declaration off policy without suppressing read warning", () => {
  const name = "phase57jConstUninitializedDeclarationOff";
  const source = `.CONST
limit DWORD ?
.code
main PROC
    mov eax, limit
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE: "off"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00600000h..00600003h reads 4 bytes from limit + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 5,
      column: 14,
      byteOffset: 50,
      spanLength: 5,
      sourceLocation: { line: 5, column: 14, byteOffset: 50, spanLength: 5 },
      symbolName: "limit",
      accessStartAddress: "00600000h",
      accessEndAddress: "00600003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 5, column 14, byte offset 50, span length 5: Memory read range 00600000h..00600003h reads 4 bytes from limit + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.");
});

test("renders Phase 57J direct CONST uninitialized write diagnostics exactly", () => {
  const name = "phase57jConstUninitializedDirectWrite";
  const source = `.CONST
limit DWORD ?
.code
main PROC
    mov limit, 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "const-uninitialized-storage",
      message: ".CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.",
      line: 2,
      column: 1,
      byteOffset: 7,
      spanLength: 5
    },
    {
      kind: "assembly-error",
      code: "const-write",
      message: "Cannot write to .CONST data. Constant data is read-only.",
      line: 5,
      column: 9,
      byteOffset: 45,
      spanLength: 5
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] const-uninitialized-storage line 2, column 1, byte offset 7, span length 5: .CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.\n[assembly-error] const-write line 5, column 9, byte offset 45, span length 5: Cannot write to .CONST data. Constant data is read-only.");
});

test("renders Phase 57J computed CONST uninitialized write diagnostics exactly", () => {
  const name = "phase57jConstUninitializedComputedWrite";
  const source = `.CONST
limit DWORD ?
.code
main PROC
    mov eax, OFFSET limit
    mov DWORD PTR [eax], 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "const-uninitialized-storage",
      message: ".CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.",
      line: 2,
      column: 1,
      byteOffset: 7,
      spanLength: 5
    },
    {
      kind: "runtime-error",
      code: "permission-denied",
      message: "Memory write at 00600000h for 4 bytes is not permitted in .const.",
      line: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] const-uninitialized-storage line 2, column 1, byte offset 7, span length 5: .CONST declaration `limit` reserves uninitialized read-only storage. The simulator accepts it for compatibility, gives bytes deterministic values, and preserves uninitialized-origin metadata. Do not rely on the reserved value.\n[runtime-error] permission-denied line 6: Memory write at 00600000h for 4 bytes is not permitted in .const.");
});

test("renders ROR undefined modeled flag warning exactly", () => {
  const name = "rorUndefinedWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-modeled-flag",
      message: "ROR count 8 has effective count 8 and rotate amount 0 for an 8-bit destination. CF was updated from the most significant bit of the rotated result. ZF and SF were preserved because ROR does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.",
      line: 4,
      column: 5,
      byteOffset: 36,
      spanLength: 9
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-modeled-flag line 4, column 5, byte offset 36, span length 9: ROR count 8 has effective count 8 and rotate amount 0 for an 8-bit destination. CF was updated from the most significant bit of the rotated result. ZF and SF were preserved because ROR does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders ROR warning under strict shift validation without runtime error", () => {
  const name = "rorUndefinedStrictStillWarns";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SHIFT_VALIDATION: "strict"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.equal(json.simulatorMessages[0].code, "undefined-modeled-flag");
  assert.equal(json.simulatorMessages[1].code, "execution-complete");
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-modeled-flag line 4, column 5, byte offset 36, span length 9: ROR count 8 has effective count 8 and rotate amount 0 for an 8-bit destination. CF was updated from the most significant bit of the rotated result. ZF and SF were preserved because ROR does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});


test("renders LEA OFFSET-source diagnostic exactly", () => {
  const name = "leaOffsetSource";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-effective-address-expression",
    message: "LEA source must be an effective-address expression, not OFFSET symbol.",
    line: 5,
    column: 14,
    byteOffset: 48,
    spanLength: 6
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-effective-address-expression line 5, column 14, byte offset 48, span length 6: LEA source must be an effective-address expression, not OFFSET symbol.");
});


test("renders LEA narrow-destination diagnostic exactly", () => {
  const name = "leaNarrowDestination";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "LEA destination must be a 32-bit register.",
    line: 5,
    column: 9,
    byteOffset: 43,
    spanLength: 2
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 5, column 9, byte offset 43, span length 2: LEA destination must be a 32-bit register.");
});


test("renders LEA scaled-index diagnostic exactly", () => {
  const name = "leaScaledIndex";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-scaled-index",
    message: "Scaled-index memory operands are not supported yet.",
    line: 3,
    column: 19,
    byteOffset: 34,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-scaled-index line 3, column 19, byte offset 34, span length 1: Scaled-index memory operands are not supported yet.");
});


test("renders LEA register-source diagnostic exactly", () => {
  const name = "leaRegisterSource";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-effective-address-expression",
    message: "LEA source must be a supported effective-address expression.",
    line: 3,
    column: 14,
    byteOffset: 29,
    spanLength: 3
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-effective-address-expression line 3, column 14, byte offset 29, span length 3: LEA source must be a supported effective-address expression.");
});


test("renders LEA displacement-overflow diagnostic exactly", () => {
  const name = "leaDisplacementOverflow";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-effective-address-expression",
    message: "LEA address displacement is outside the supported signed 32-bit range.",
    line: 5,
    column: 20,
    byteOffset: 54,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-effective-address-expression line 5, column 20, byte offset 54, span length 1: LEA address displacement is outside the supported signed 32-bit range.");
});


test("renders undefined flag-use warning exactly", () => {
  const name = "undefinedFlagUseAdc";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE: "warn"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 5);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
      line: 5,
      column: 5,
      byteOffset: 42,
      spanLength: 9
    },
    {
      kind: "simulator-warning",
      code: "undefined-flag-use",
      message: "ADC reads CF, but CF is architecturally undefined from SHL at line 5. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
      line: 7,
      column: 5,
      byteOffset: 71,
      spanLength: 10,
      consumedFlags: ["CF"],
      producerMnemonic: "SHL",
      producerCode: "undefined-shift-flag",
      producerLine: 5
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assert.equal(json.registers.EBX.hex, "00000001h");
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 5, column 5, byte offset 42, span length 9: SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    "[simulator-warning] undefined-flag-use line 7, column 5, byte offset 71, span length 10: ADC reads CF, but CF is architecturally undefined from SHL at line 5. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("Phase 53C renders default undefined flag-use warning exactly", () => {
  const name = "undefinedFlagUseAdc";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 5);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
      line: 5,
      column: 5,
      byteOffset: 42,
      spanLength: 9
    },
    {
      kind: "simulator-warning",
      code: "undefined-flag-use",
      message: "ADC reads CF, but CF is architecturally undefined from SHL at line 5. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
      line: 7,
      column: 5,
      byteOffset: 71,
      spanLength: 10,
      consumedFlags: ["CF"],
      producerMnemonic: "SHL",
      producerCode: "undefined-shift-flag",
      producerLine: 5
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assert.equal(json.registers.EBX.hex, "00000001h");
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 5, column 5, byte offset 42, span length 9: SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    "[simulator-warning] undefined-flag-use line 7, column 5, byte offset 71, span length 10: ADC reads CF, but CF is architecturally undefined from SHL at line 5. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders undefined flag-use runtime error exactly", () => {
  const name = "undefinedFlagUseAdc";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE: "error"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 4);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "undefined-flag-use",
      message: "ADC reads CF, but CF is architecturally undefined from SHL at line 5. Execution stopped before using the undefined flag.",
      line: 7,
      column: 5,
      byteOffset: 71,
      spanLength: 10,
      consumedFlags: ["CF"],
      producerMnemonic: "SHL",
      producerCode: "undefined-shift-flag",
      producerLine: 5
    }
  ]);
  assert.equal(json.registers.EBX.hex, "00000000h");
  assert.equal(json.registers.EFLAGS.hex, "00000041h");
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] undefined-flag-use line 7, column 5, byte offset 71, span length 10: ADC reads CF, but CF is architecturally undefined from SHL at line 5. Execution stopped before using the undefined flag.");
});




test("renders Phase 65 signed conditional jump undefined flag-use warning exactly", () => {
  const name = "phase65SignedConditionalJumpUndefinedFlagWarning";
  const source = `.code
main PROC
    mov al, 1
    shl al, 8
    jl target
    mov ebx, 1
target:
    nop
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE: "warn"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assert.equal(json.instructionCount, 5);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
      line: 4,
      column: 5,
      byteOffset: 34,
      spanLength: 9
    },
    {
      kind: "simulator-warning",
      code: "undefined-flag-use",
      message: "JL reads OF, but OF is architecturally undefined from SHL at line 4. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
      line: 5,
      column: 5,
      byteOffset: 48,
      spanLength: 9,
      consumedFlags: ["OF"],
      producerMnemonic: "SHL",
      producerCode: "undefined-shift-flag",
      producerLine: 4
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assert.equal(json.registers.EBX.hex, "00000001h");
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 4, column 5, byte offset 34, span length 9: SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    "[simulator-warning] undefined-flag-use line 5, column 5, byte offset 48, span length 9: JL reads OF, but OF is architecturally undefined from SHL at line 4. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});



test("renders Phase 66 unsigned conditional jump undefined flag-use warning exactly", () => {
  const name = "phase66UnsignedConditionalJumpUndefinedFlagWarning";
  const source = `.code
main PROC
    mov al, 1
    shl al, 8
    ja target
    mov ebx, 1
target:
    nop
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE: "warn"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assert.equal(json.instructionCount, 5);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "undefined-shift-flag",
      message: "SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
      line: 4,
      column: 5,
      byteOffset: 34,
      spanLength: 9
    },
    {
      kind: "simulator-warning",
      code: "undefined-flag-use",
      message: "JA reads CF, but CF is architecturally undefined from SHL at line 4. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
      line: 5,
      column: 5,
      byteOffset: 48,
      spanLength: 9,
      consumedFlags: ["CF"],
      producerMnemonic: "SHL",
      producerCode: "undefined-shift-flag",
      producerLine: 4
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assert.equal(json.registers.EBX.hex, "00000001h");
  assertRenderedEquals(name, source, rawJson, rendered, [
    "[simulator-warning] undefined-shift-flag line 4, column 5, byte offset 34, span length 9: SHL count 8 has effective count 8 for an 8-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
    "[simulator-warning] undefined-flag-use line 5, column 5, byte offset 48, span length 9: JA reads CF, but CF is architecturally undefined from SHL at line 4. The simulator preserved the flag deterministically; this flag-dependent behavior is not portable.",
    "",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("renders Phase 65 signed conditional jump undefined flag-use runtime error exactly", () => {
  const name = "phase65SignedConditionalJumpUndefinedFlagError";
  const source = `.code
main PROC
    mov al, 1
    shl al, 8
    jl target
    mov ebx, 1
target:
    nop
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE: "error"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.phase, 66);
  assert.equal(json.phaseSuffix, "");
  assert.equal(json.phaseName, "Phase 66 - Unsigned Relational Conditional Jumps");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "undefined-flag-use",
      message: "JL reads OF, but OF is architecturally undefined from SHL at line 4. Execution stopped before using the undefined flag.",
      line: 5,
      column: 5,
      byteOffset: 48,
      spanLength: 9,
      consumedFlags: ["OF"],
      producerMnemonic: "SHL",
      producerCode: "undefined-shift-flag",
      producerLine: 4
    }
  ]);
  assert.equal(json.registers.EBX.hex, "00000000h");
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] undefined-flag-use line 5, column 5, byte offset 48, span length 9: JL reads OF, but OF is architecturally undefined from SHL at line 4. Execution stopped before using the undefined flag.");
});

test("renders NOT ambiguous memory-width diagnostic exactly", () => {
  const name = "notAmbiguousMemoryWidth";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "ambiguous-memory-width",
    message: "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
});

test("renders NOT immediate-destination diagnostic exactly", () => {
  const name = "notImmediateDestination";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "NOT requires a register or memory destination.",
    line: 3,
    column: 9,
    byteOffset: 24,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 9, byte offset 24, span length 1: NOT requires a register or memory destination.");
});

test("renders NOT extra-operand diagnostic exactly", () => {
  const name = "notExtraOperand";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "invalid-instruction-operands",
    message: "NOT takes exactly one register or memory operand.",
    line: 3,
    column: 12,
    byteOffset: 27,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] invalid-instruction-operands line 3, column 12, byte offset 27, span length 1: NOT takes exactly one register or memory operand.");
});

test("renders NOT direct CONST write diagnostic exactly", () => {
  const name = "notConstDirectWrite";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "const-write",
    message: "Cannot write to .CONST data. Constant data is read-only.",
    line: 5,
    column: 9,
    byteOffset: 46,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[assembly-error] const-write line 5, column 9, byte offset 46, span length 5: Cannot write to .CONST data. Constant data is read-only.");
});

test("renders NOT runtime CONST permission diagnostic exactly without memory change rows", () => {
  const name = "notConstRuntimeWrite";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "execution-error");
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "permission-denied",
    message: "Memory write at 00600000h for 4 bytes is not permitted in .const.",
    line: 6
  });
  assert.equal(json.memoryChanges.length, 0);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] permission-denied line 6: Memory write at 00600000h for 4 bytes is not permitted in .const.");
});

test("renders NOT invalid destination-address diagnostic exactly", () => {
  const name = "notInvalidAddress";
  const source = fixtureSource(name);
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


test("Phase 51 renders automatic-layout instruction smoke success exactly", () => {
  const name = "phase51LayoutAndInstructionSmoke";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_LAYOUT_MODE: "automatic"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 6);
  assert.equal(json.registers.EAX.hex, "00000008h");
  assert.equal(json.registers.EBX.hex, "00000008h");
  console.log("PHASE 51 expected rendered diagnostic line: [info] execution-complete: Execution completed successfully.");
  assertRenderedEquals(name, source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("Phase 51 renders CONST precedence smoke diagnostic exactly", () => {
  const name = "phase51ConstPrecedence";
  const source = fixtureSource(name);
  const expected = "[runtime-error] permission-denied line 6: Memory write at 00600000h for 4 bytes is not permitted in .const.";
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "allocated-object-strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.simulatorMessages.some((message) => message.code === "object-bounds-violation"), false);
  assert.deepEqual(json.memoryChanges, []);
  console.log(`PHASE 51 expected rendered diagnostic line: ${expected}`);
  assertRenderedEquals(name, source, rawJson, rendered, expected);
});

test("Phase 51 renders uninitialized RMW smoke diagnostic exactly", () => {
  runPhase51RenderedDiagnosticSmoke(
    "phase51UninitializedRmw",
    "[simulator-warning] uninitialized-read line 5, column 9, byte offset 40, span length 1: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.",
    { MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-warnings" }
  );
});

test("Phase 51 renders instruction-family diagnostic smoke lines exactly", () => {
  const ambiguousWidthLine = "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.";
  const fixtureNames = [
    "phase51IncDecDiagnosticSmoke",
    "phase51AndOrXorDiagnosticSmoke",
    "phase51NotDiagnosticSmoke",
    "phase51ShlSalDiagnosticSmoke",
    "phase51ShrDiagnosticSmoke",
    "phase51SarDiagnosticSmoke",
    "phase51RolDiagnosticSmoke",
    "phase51RorDiagnosticSmoke"
  ];

  for (const fixtureName of fixtureNames) {
    runPhase51RenderedDiagnosticSmoke(fixtureName, ambiguousWidthLine);
  }
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
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] unaligned-memory-access line 6: Unaligned DWORD memory access at 00500001h.\n\n[info] execution-complete: Execution completed successfully.");
});

test("renders allocated-object warning followed by successful execution exactly", () => {
  const name = "objectBoundsWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "allocated-object-warnings"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "object-bounds-warning",
      message: "Memory read at 00500040h for 4 bytes is inside a valid region but outside any declared data object.",
      line: 6
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] object-bounds-warning line 6: Memory read at 00500040h for 4 bytes is inside a valid region but outside any declared data object.\n\n[info] execution-complete: Execution completed successfully.");
});



test("renders allocated-object strict violation exactly", () => {
  const name = "objectBoundsWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "allocated-object-strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "object-bounds-violation",
      message: "Memory read at 00500040h for 4 bytes is inside a valid region but outside any declared data object.",
      line: 6,
      column: 10,
      byteOffset: 73,
      spanLength: 9
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] object-bounds-violation line 6, column 10, byte offset 73, span length 9: Memory read at 00500040h for 4 bytes is inside a valid region but outside any declared data object.");
});

test("Phase 53A renders default region-only object-spanning read without object diagnostic", () => {
  const name = "phase53aObjectSpan";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.equal(json.simulatorMessages.some((message) => message.code === "object-bounds-warning"), false);
  assert.equal(json.simulatorMessages.some((message) => message.code === "object-bounds-violation"), false);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "unaligned-memory-access",
      message: "Unaligned DWORD memory access at 00500001h.",
      line: 7
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] unaligned-memory-access line 7: Unaligned DWORD memory access at 00500001h.\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 53A renders object-spanning warning before unaligned warning", () => {
  const name = "phase53aObjectSpan";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "allocated-object-warnings"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "object-bounds-warning",
      message: "Memory read range 00500001h..00500004h spans multiple declared data objects (spans-objects).",
      line: 7
    },
    {
      kind: "simulator-warning",
      code: "unaligned-memory-access",
      message: "Unaligned DWORD memory access at 00500001h.",
      line: 7
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] object-bounds-warning line 7: Memory read range 00500001h..00500004h spans multiple declared data objects (spans-objects).\n[simulator-warning] unaligned-memory-access line 7: Unaligned DWORD memory access at 00500001h.\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 53A renders strict object violation before rejected instruction mutation", () => {
  const name = "phase53aObjectSpanStrictMutation";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "allocated-object-strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assert.equal(json.registers.EAX.hex, "00000309h");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "object-bounds-violation",
      message: "Memory read range 00500001h..00500004h spans multiple declared data objects (spans-objects).",
      line: 8,
      column: 24,
      byteOffset: 99,
      spanLength: 5
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] object-bounds-violation line 8, column 24, byte offset 99, span length 5: Memory read range 00500001h..00500004h spans multiple declared data objects (spans-objects).");
});

test("Phase 53A renders uninitialized-read before unaligned warning for symbol-offset read", () => {
  const name = "phase53aUninitializedObjectSpan";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-warnings"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 4 of those bytes still originated from uninitialized storage.",
      line: 7,
      column: 24,
      byteOffset: 67,
      spanLength: 5,
      sourceLocation: {
        line: 7,
        column: 24,
        byteOffset: 67,
        spanLength: 5
      },
      symbolName: "x",
      accessStartAddress: "00500001h",
      accessEndAddress: "00500004h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 1
    },
    {
      kind: "simulator-warning",
      code: "unaligned-memory-access",
      message: "Unaligned DWORD memory access at 00500001h.",
      line: 7
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 7, column 24, byte offset 67, span length 5: Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 4 of those bytes still originated from uninitialized storage.\n[simulator-warning] unaligned-memory-access line 7: Unaligned DWORD memory access at 00500001h.\n\n[info] execution-complete: Execution completed successfully.");
});


test("Phase 53B renders section-image warning exactly", () => {
  const name = "phase53bSectionImageWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION: "warn"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 3);
  assert.deepEqual(json.simulatorMessages[0], {
    kind: "simulator-warning",
    code: "section-image-violation",
    message: "Memory read at 00500004h for 4 bytes covers range 00500004h..00500007h and leaves the section image range for .data/.DATA? (00500000h..00500003h).",
    line: 7,
    column: 24,
    byteOffset: 92,
    spanLength: 5,
    accessKind: "read",
    accessStartAddress: "00500004h",
    accessEndAddress: "00500007h",
    accessSizeBytes: 4,
    ownerSection: ".data/.DATA?",
    boundaryStartAddress: "00500000h",
    boundaryEndAddress: "00500003h"
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] section-image-violation line 7, column 24, byte offset 92, span length 5: Memory read at 00500004h for 4 bytes covers range 00500004h..00500007h and leaves the section image range for .data/.DATA? (00500000h..00500003h).\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 53B renders section-image strict violation exactly", () => {
  const name = "phase53bSectionImageStrict";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION: "strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.memoryChanges, []);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "section-image-violation",
      message: "Memory write at 00500004h for 4 bytes covers range 00500004h..00500007h and leaves the section image range for .data/.DATA? (00500000h..00500003h).",
      line: 7,
      column: 19,
      byteOffset: 87,
      spanLength: 5,
      accessKind: "write",
      accessStartAddress: "00500004h",
      accessEndAddress: "00500007h",
      accessSizeBytes: 4,
      ownerSection: ".data/.DATA?",
      boundaryStartAddress: "00500000h",
      boundaryEndAddress: "00500003h"
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] section-image-violation line 7, column 19, byte offset 87, span length 5: Memory write at 00500004h for 4 bytes covers range 00500004h..00500007h and leaves the section image range for .data/.DATA? (00500000h..00500003h).");
});

test("Phase 53B renders section-capacity warning exactly", () => {
  const name = "phase53bSectionCapacityWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION: "warn"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages[0], {
    kind: "simulator-warning",
    code: "section-capacity-violation",
    message: "Memory write at 00700000h for 4 bytes covers range 00700000h..00700003h but does not start inside a known section capacity range for heap.",
    line: 4,
    column: 19,
    byteOffset: 57,
    spanLength: 5,
    accessKind: "write",
    accessStartAddress: "00700000h",
    accessEndAddress: "00700003h",
    accessSizeBytes: 4,
    ownerSection: "heap",
    boundaryStartAddress: null,
    boundaryEndAddress: null
  });
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] section-capacity-violation line 4, column 19, byte offset 57, span length 5: Memory write at 00700000h for 4 bytes covers range 00700000h..00700003h but does not start inside a known section capacity range for heap.\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 53B renders section-capacity strict violation exactly", () => {
  const name = "phase53bSectionCapacityStrict";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION: "strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.memoryChanges, []);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "section-capacity-violation",
      message: "Memory write at 00700000h for 4 bytes covers range 00700000h..00700003h but does not start inside a known section capacity range for heap.",
      line: 4,
      column: 19,
      byteOffset: 57,
      spanLength: 5,
      accessKind: "write",
      accessStartAddress: "00700000h",
      accessEndAddress: "00700003h",
      accessSizeBytes: 4,
      ownerSection: "heap",
      boundaryStartAddress: null,
      boundaryEndAddress: null
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] section-capacity-violation line 4, column 19, byte offset 57, span length 5: Memory write at 00700000h for 4 bytes covers range 00700000h..00700003h but does not start inside a known section capacity range for heap.");
});

test("Phase 53A renders strict uninitialized-read before mutation", () => {
  const name = "phase53aUninitializedObjectSpan";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 0);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "uninitialized-read",
      message: "Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 4 of those bytes still originated from uninitialized storage.",
      line: 7,
      column: 24,
      byteOffset: 67,
      spanLength: 5,
      sourceLocation: {
        line: 7,
        column: 24,
        byteOffset: 67,
        spanLength: 5
      },
      symbolName: "x",
      accessStartAddress: "00500001h",
      accessEndAddress: "00500004h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 1
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] uninitialized-read line 7, column 24, byte offset 67, span length 5: Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 4 of those bytes still originated from uninitialized storage.");
});

test("renders uninitialized-read warning followed by successful execution exactly", () => {
  const name = "uninitializedRead";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-warnings"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 5,
      column: 14,
      byteOffset: 45,
      spanLength: 1,
      sourceLocation: {
        line: 5,
        column: 14,
        byteOffset: 45,
        spanLength: 1
      },
      symbolName: "x",
      accessStartAddress: "00500000h",
      accessEndAddress: "00500003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 5, column 14, byte offset 45, span length 1: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 53C renders default uninitialized-read warning exactly", () => {
  const name = "uninitializedRead";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 5,
      column: 14,
      byteOffset: 45,
      spanLength: 1,
      sourceLocation: {
        line: 5,
        column: 14,
        byteOffset: 45,
        spanLength: 1
      },
      symbolName: "x",
      accessStartAddress: "00500000h",
      accessEndAddress: "00500003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 5, column 14, byte offset 45, span length 1: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 53C keeps default uninitialized-read warnings when only section validation is explicit", () => {
  const name = "uninitializedRead";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION: "warn"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 5,
      column: 14,
      byteOffset: 45,
      spanLength: 1,
      sourceLocation: {
        line: 5,
        column: 14,
        byteOffset: 45,
        spanLength: 1
      },
      symbolName: "x",
      accessStartAddress: "00500000h",
      accessEndAddress: "00500003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 5, column 14, byte offset 45, span length 1: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 53C renders indirect overlapping uninitialized-read warning exactly", () => {
  const name = "uninitializedReadIndirectOverlap";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 2);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 3 of those bytes still originated from uninitialized storage.",
      line: 6,
      column: 24,
      byteOffset: 78,
      spanLength: 7,
      sourceLocation: {
        line: 6,
        column: 24,
        byteOffset: 78,
        spanLength: 7
      },
      symbolName: "x",
      accessStartAddress: "00500001h",
      accessEndAddress: "00500004h",
      accessSizeBytes: 4,
      uninitializedByteCount: 3,
      initializedByteCount: 1,
      accessByteOffset: 1
    },
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
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 6, column 24, byte offset 78, span length 7: Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 3 of those bytes still originated from uninitialized storage.\n[simulator-warning] unaligned-memory-access line 6: Unaligned DWORD memory access at 00500001h.\n\n[info] execution-complete: Execution completed successfully.");
});

test("renders uninitialized-read strict violation with source span exactly", () => {
  const name = "uninitializedReadBracketed";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 1);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "uninitialized-read",
      message: "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 6,
      column: 24,
      byteOffset: 77,
      spanLength: 5,
      sourceLocation: {
        line: 6,
        column: 24,
        byteOffset: 77,
        spanLength: 5
      },
      symbolName: "x",
      accessStartAddress: "00500000h",
      accessEndAddress: "00500003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] uninitialized-read line 6, column 24, byte offset 77, span length 5: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.");
});

test("Phase 64A renders RMW planned-read warning and completion exactly", () => {
  const name = "phase64aRmwUninitializedReadWarning";
  const source = `.DATA?
x DWORD ?
.code
main PROC
    inc x
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-warnings"
  });
  assertRunStatus(json, true, "ok");
  assert.equal(json.instructionCount, 1);
  assert.equal(json.simulatorMessages[0].kind, "simulator-warning");
  assert.equal(json.simulatorMessages[0].code, "uninitialized-read");
  assert.equal(json.simulatorMessages[0].line, 5);
  assert.equal(json.simulatorMessages[0].column, 9);
  assert.equal(json.simulatorMessages[0].byteOffset, 41);
  assert.equal(json.simulatorMessages[0].spanLength, 1);
  assert.equal(json.simulatorMessages[0].message, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.");
  assert.equal(json.simulatorMessages[1].kind, "info");
  assert.equal(json.simulatorMessages[1].code, "execution-complete");
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 5, column 9, byte offset 41, span length 1: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n\n[info] execution-complete: Execution completed successfully.");
});

test("Phase 64A renders RMW planned-read strict stop exactly", () => {
  const name = "phase64aRmwUninitializedReadStrict";
  const source = `.DATA?
x DWORD ?
.code
main PROC
    inc x
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 0);
  assert.equal(json.memoryChanges.length, 0);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "uninitialized-read",
      message: "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 5,
      column: 9,
      byteOffset: 41,
      spanLength: 1,
      sourceLocation: {
        line: 5,
        column: 9,
        byteOffset: 41,
        spanLength: 1
      },
      symbolName: "x",
      accessStartAddress: "00500000h",
      accessEndAddress: "00500003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[runtime-error] uninitialized-read line 5, column 9, byte offset 41, span length 1: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.");
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
      code: "unsupported-invoke",
      message: "INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.",
      line: 10,
      column: 5,
      byteOffset: 82,
      spanLength: 6
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-high-level-if",
      message: ".IF high-level MASM flow is not implemented; the simulator does not lower high-level conditions into labels or branches.",
      line: 11,
      column: 5,
      byteOffset: 102,
      spanLength: 3
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-high-level-endif",
      message: ".ENDIF closes unsupported high-level MASM flow; the simulator does not lower high-level conditions into labels or branches.",
      line: 13,
      column: 5,
      byteOffset: 138,
      spanLength: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assert.equal(rendered.includes("mov ebx"), false);
  assertRenderedEquals(name, source, rawJson, rendered, `[unsupported-feature] unsupported-feature line 4, column 10, byte offset 26, span length 6: Unsupported feature: STRUCT declarations are not supported yet.
[unsupported-feature] unsupported-invoke line 10, column 5, byte offset 82, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.
[unsupported-feature] unsupported-high-level-if line 11, column 5, byte offset 102, span length 3: .IF high-level MASM flow is not implemented; the simulator does not lower high-level conditions into labels or branches.
[unsupported-feature] unsupported-high-level-endif line 13, column 5, byte offset 138, span length 6: .ENDIF closes unsupported high-level MASM flow; the simulator does not lower high-level conditions into labels or branches.`);
});


test("renders Phase 57S high-level flow markers exactly", () => {
  const name = "highLevelFlowMarkers";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "unsupported-feature",
      code: "unsupported-high-level-if",
      message: ".IF high-level MASM flow is not implemented; the simulator does not lower high-level conditions into labels or branches.",
      line: 3,
      column: 5,
      byteOffset: 20,
      spanLength: 3
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-high-level-else",
      message: ".ELSE high-level MASM flow is not implemented; the simulator does not lower high-level alternatives into labels or branches.",
      line: 5,
      column: 5,
      byteOffset: 56,
      spanLength: 5
    },
    {
      kind: "unsupported-feature",
      code: "unsupported-high-level-endif",
      message: ".ENDIF closes unsupported high-level MASM flow; the simulator does not lower high-level conditions into labels or branches.",
      line: 7,
      column: 5,
      byteOffset: 93,
      spanLength: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assert.equal(rendered.includes("badinstruction"), false);
  assertRenderedEquals(name, source, rawJson, rendered, `[unsupported-feature] unsupported-high-level-if line 3, column 5, byte offset 20, span length 3: .IF high-level MASM flow is not implemented; the simulator does not lower high-level conditions into labels or branches.
[unsupported-feature] unsupported-high-level-else line 5, column 5, byte offset 56, span length 5: .ELSE high-level MASM flow is not implemented; the simulator does not lower high-level alternatives into labels or branches.
[unsupported-feature] unsupported-high-level-endif line 7, column 5, byte offset 93, span length 6: .ENDIF closes unsupported high-level MASM flow; the simulator does not lower high-level conditions into labels or branches.`);
});




test("Phase 57T renders realistic playground diagnostics exactly", () => {
  const name = "phase57tPlaygroundDiagnosticRecovery";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source);
  assertRunStatus(json, false, "parse-error");
  assertNoExecutionComplete(json.simulatorMessages);
  assert.equal(JSON.stringify(json).includes("programConsole"), false);
  assert.equal(rendered.includes("unexpected-character"), false);
  assert.equal(rendered.includes("lexer-failed"), false);
  assert.equal(rendered.includes("missing local file"), false);
  assertRenderedEquals(name, source, rawJson, rendered, `[simulator-notice] compatibility-no-op line 1, column 1, byte offset 0, span length 4: .386 is accepted for MASM compatibility but does not change the simulator CPU mode.
[simulator-notice] compatibility-limited line 2, column 1, byte offset 5, span length 6: .model flat, stdcall is accepted for MASM32 textbook compatibility but does not enable real object-file, linker, Windows calling-convention, or WinAPI behavior.
[unsupported-feature] unsupported-masm32-library-include line 5, column 9, byte offset 55, span length 26: Host filesystem include path '\\masm32\\include\\masm32.inc' is not supported. This browser simulator does not read the local MASM32 SDK; use supported virtual includes only.
[unsupported-feature] unsupported-windows-api-include line 6, column 9, byte offset 90, span length 28: Windows API include path '\\masm32\\include\\kernel32.inc' is not supported. Windows API execution is outside this simulator; PE loading, imports, and WinAPI calls are not performed.
[unsupported-feature] unsupported-masm32-library line 8, column 12, byte offset 131, span length 22: INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. MASM32 library '\\masm32\\lib\\masm32.lib' requires external library linking.
[unsupported-feature] unsupported-windows-api-library line 9, column 12, byte offset 165, span length 24: INCLUDELIB is not supported in MASM32 Educational Mode; the simulator does not link objects, load .lib files, process PE imports, or execute external routines. Windows import library '\\masm32\\lib\\kernel32.lib' requires PE imports and WinAPI execution.
[unsupported-feature] unsupported-invoke line 22, column 5, byte offset 487, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.
[unsupported-feature] unsupported-addr line 22, column 20, byte offset 502, span length 4: ADDR operands are not implemented; ADDR depends on INVOKE/procedure argument lowering and future calling-convention support.
[unsupported-feature] unsupported-masm32-runtime-routine line 22, column 12, byte offset 494, span length 6: StdOut is an external MASM32 runtime-style routine. MASM32 Educational Mode does not link MASM32 runtime libraries or execute external routines.
[unsupported-feature] unsupported-invoke line 23, column 5, byte offset 520, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.
[unsupported-feature] unsupported-addr line 23, column 20, byte offset 535, span length 4: ADDR operands are not implemented; ADDR depends on INVOKE/procedure argument lowering and future calling-convention support.
[unsupported-feature] unsupported-masm32-runtime-routine line 23, column 12, byte offset 527, span length 6: StdOut is an external MASM32 runtime-style routine. MASM32 Educational Mode does not link MASM32 runtime libraries or execute external routines.
[unsupported-feature] unsupported-invoke line 24, column 5, byte offset 553, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.
[unsupported-feature] unsupported-addr line 24, column 24, byte offset 572, span length 4: ADDR operands are not implemented; ADDR depends on INVOKE/procedure argument lowering and future calling-convention support.
[unsupported-feature] unsupported-crt-routine line 24, column 12, byte offset 560, span length 10: crt_printf is a C runtime formatted-output routine. MASM32 Educational Mode does not link or execute CRT routines.
[unsupported-feature] unsupported-high-level-if line 29, column 5, byte offset 638, span length 3: .IF high-level MASM flow is not implemented; the simulator does not lower high-level conditions into labels or branches.
[unsupported-feature] unsupported-high-level-else line 31, column 5, byte offset 691, span length 5: .ELSE high-level MASM flow is not implemented; the simulator does not lower high-level alternatives into labels or branches.
[unsupported-feature] unsupported-high-level-endif line 33, column 5, byte offset 736, span length 6: .ENDIF closes unsupported high-level MASM flow; the simulator does not lower high-level conditions into labels or branches.
[assembly-error] unsupported-instruction line 36, column 5, byte offset 769, span length 4: CALL is not supported yet.`);
});

test("Phase 57T renders RET, loop, and WinAPI unsupported diagnostics exactly", () => {
  const retName = "phase57tRetUnsupported";
  const retSource = fixtureSource(retName);
  const retResult = runFixture(retName, retSource);
  assertRunStatus(retResult.json, false, "parse-error");
  assertNoExecutionComplete(retResult.json.simulatorMessages);
  assertRenderedEquals(retName, retSource, retResult.rawJson, retResult.rendered, "[assembly-error] unsupported-instruction line 3, column 5, byte offset 20, span length 3: Unsupported instruction. This mnemonic has no executable behavior in MASM32 Educational Mode; use an implemented instruction listed in docs/SUPPORTED_SYNTAX.md.");

  const jumpName = "phase57tLoopUnsupported";
  const jumpSource = fixtureSource(jumpName);
  const jumpResult = runFixture(jumpName, jumpSource);
  assertRunStatus(jumpResult.json, false, "parse-error");
  assertNoExecutionComplete(jumpResult.json.simulatorMessages);
  assertRenderedEquals(jumpName, jumpSource, jumpResult.rawJson, jumpResult.rendered, "[assembly-error] unsupported-instruction line 3, column 5, byte offset 20, span length 4: Unsupported instruction. This mnemonic has no executable behavior in MASM32 Educational Mode; use an implemented instruction listed in docs/SUPPORTED_SYNTAX.md.");

  const exitProcessName = "phase57tExitProcessUnsupported";
  const exitProcessSource = fixtureSource(exitProcessName);
  const exitProcessResult = runFixture(exitProcessName, exitProcessSource);
  assertRunStatus(exitProcessResult.json, false, "parse-error");
  assertNoExecutionComplete(exitProcessResult.json.simulatorMessages);
  assertRenderedEquals(exitProcessName, exitProcessSource, exitProcessResult.rawJson, exitProcessResult.rendered, "[unsupported-feature] unsupported-invoke line 4, column 5, byte offset 41, span length 6: INVOKE syntax is not implemented in MASM32 Educational Mode; the simulator does not lower procedure arguments, set up calling conventions, or call routines.\n[unsupported-feature] unsupported-winapi-execution line 4, column 12, byte offset 48, span length 11: ExitProcess is WinAPI/external process termination behavior. MASM32 Educational Mode does not execute Windows API calls; this is not the virtual Irvine32 exit terminator.");
});


test("Phase 63 renders CMP register/immediate success exactly", () => {
  const cmpName = "phase62CmpRegisterImmediateSuccess";
  const cmpSource = fixtureSource(cmpName);
  const cmpResult = runFixture(cmpName, cmpSource);
  assertRunStatus(cmpResult.json, true, "ok");
  assert.deepEqual(cmpResult.json.memoryChanges, []);
  assert.equal(cmpResult.json.registers.EAX.hex, "00000000h");
  assert.equal(cmpResult.json.registers.EFLAGS.hex, "00000081h");
  assertRenderedEquals(cmpName, cmpSource, cmpResult.rawJson, cmpResult.rendered, "[info] execution-complete: Execution completed successfully.");
});

test("Phase 63 renders CMP diagnostics exactly", () => {
  const widthName = "phase63CmpWidthMismatch";
  const widthSource = `.code
main PROC
    cmp eax, al
main ENDP
END main
`;
  const widthResult = runFixture(widthName, widthSource);
  assertRunStatus(widthResult.json, false, "parse-error");
  assertNoExecutionComplete(widthResult.json.simulatorMessages);
  assertRenderedEquals(widthName, widthSource, widthResult.rawJson, widthResult.rendered, "[assembly-error] operand-width-mismatch line 3, column 14, byte offset 29, span length 2: Source operand width does not match the destination operand width.");

  const memName = "phase63CmpMemorySourceSuccess";
  const memSource = `.data
value DWORD 1
.code
main PROC
    mov eax, 1
    cmp eax, value
main ENDP
END main
`;
  const memResult = runFixture(memName, memSource);
  assertRunStatus(memResult.json, true, "ok");
  assert.deepEqual(memResult.json.memoryChanges, []);
  assert.equal(memResult.json.registers.EFLAGS.hex, "00000040h");
  assertRenderedEquals(memName, memSource, memResult.rawJson, memResult.rendered, "[info] execution-complete: Execution completed successfully.");

  const ambiguousName = "phase63CmpAmbiguousMemoryImmediate";
  const ambiguousSource = `.code
main PROC
    cmp [eax], 1
main ENDP
END main
`;
  const ambiguousResult = runFixture(ambiguousName, ambiguousSource);
  assertRunStatus(ambiguousResult.json, false, "parse-error");
  assertNoExecutionComplete(ambiguousResult.json.simulatorMessages);
  assertRenderedEquals(ambiguousName, ambiguousSource, ambiguousResult.rawJson, ambiguousResult.rendered, "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");

  const extraName = "phase63CmpExtraOperand";
  const extraSource = `.code
main PROC
    cmp eax, ebx, ecx
main ENDP
END main
`;
  const extraResult = runFixture(extraName, extraSource);
  assertRunStatus(extraResult.json, false, "parse-error");
  assertNoExecutionComplete(extraResult.json.simulatorMessages);
  assertRenderedEquals(extraName, extraSource, extraResult.rawJson, extraResult.rendered, "[assembly-error] invalid-instruction-operands line 3, column 17, byte offset 32, span length 1: CMP takes exactly two operands.");

  const qwordName = "phase63CmpQwordPtrRejected";
  const qwordSource = `.data
q QWORD 1
.code
main PROC
    cmp QWORD PTR q, 1
main ENDP
END main
`;
  const qwordResult = runFixture(qwordName, qwordSource);
  assertRunStatus(qwordResult.json, false, "parse-error");
  assertNoExecutionComplete(qwordResult.json.simulatorMessages);
  assertRenderedEquals(qwordName, qwordSource, qwordResult.rawJson, qwordResult.rendered, "[unsupported-feature] unsupported-ptr-width line 5, column 9, byte offset 40, span length 5: QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.");

  const sqwordName = "phase63CmpSqwordPtrRejected";
  const sqwordSource = `.data
q SQWORD 1
.code
main PROC
    cmp SQWORD PTR q, 1
main ENDP
END main
`;
  const sqwordResult = runFixture(sqwordName, sqwordSource);
  assertRunStatus(sqwordResult.json, false, "parse-error");
  assertNoExecutionComplete(sqwordResult.json.simulatorMessages);
  assertRenderedEquals(sqwordName, sqwordSource, sqwordResult.rawJson, sqwordResult.rendered, "[unsupported-feature] unsupported-ptr-width line 5, column 9, byte offset 41, span length 6: QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.");
});

test("Phase 63 renders CMP planned-read warning and strict diagnostics exactly", () => {
  const source = `.DATA?
value DWORD ?
.code
main PROC
    stc
    cmp value, 0
main ENDP
END main
`;
  const warnName = "phase63CmpUninitializedReadWarning";
  const warnResult = runFixture(warnName, source, { MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-warnings" });
  assertRunStatus(warnResult.json, true, "ok");
  assertRenderedEquals(warnName, source, warnResult.rawJson, warnResult.rendered, `[simulator-warning] uninitialized-read line 6, column 9, byte offset 53, span length 5: Memory read range 00500000h..00500003h reads 4 bytes from value + 0; 4 of those bytes still originated from uninitialized storage.

[info] execution-complete: Execution completed successfully.`);

  const strictName = "phase63CmpUninitializedReadStrict";
  const strictResult = runFixture(strictName, source, { MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "uninitialized-read-strict" });
  assertRunStatus(strictResult.json, false, "execution-error");
  assertNoExecutionComplete(strictResult.json.simulatorMessages);
  assertRenderedEquals(strictName, source, strictResult.rawJson, strictResult.rendered, "[runtime-error] uninitialized-read line 6, column 9, byte offset 53, span length 5: Memory read range 00500000h..00500003h reads 4 bytes from value + 0; 4 of those bytes still originated from uninitialized storage.");
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


test("Phase 53D renders compatibility notices exactly", () => {
  const source = `.686
.model flat, stdcall
.stack 4096
INCLUDE Macros.inc
TITLE Notice Sample
PAGE 60, 132
.code
main PROC
    mov eax, 42
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase53d-compatibility-notices", source);
  assertRunStatus(json, true, "ok");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-notice",
      code: "compatibility-no-op",
      message: ".686 is accepted for MASM compatibility but does not change the simulator CPU mode.",
      line: 1,
      column: 1,
      byteOffset: 0,
      spanLength: 4
    },
    {
      kind: "simulator-notice",
      code: "compatibility-limited",
      message: ".model flat, stdcall is accepted for MASM32 textbook compatibility but does not enable real object-file, linker, Windows calling-convention, or WinAPI behavior.",
      line: 2,
      column: 1,
      byteOffset: 5,
      spanLength: 6
    },
    {
      kind: "simulator-notice",
      code: "compatibility-metadata-only",
      message: ".stack size is recorded as parser metadata, but runtime stack instructions and procedure frames remain deferred.",
      line: 3,
      column: 1,
      byteOffset: 26,
      spanLength: 6
    },
    {
      kind: "simulator-notice",
      code: "compatibility-limited",
      message: "INCLUDE Macros.inc is accepted as a virtual compatibility include; general MASM macro expansion remains unsupported until a later macro phase.",
      line: 4,
      column: 1,
      byteOffset: 38,
      spanLength: 7
    },
    {
      kind: "simulator-notice",
      code: "compatibility-no-op",
      message: "TITLE is accepted as a listing/documentation directive for MASM compatibility but does not affect VM execution.",
      line: 5,
      column: 1,
      byteOffset: 57,
      spanLength: 5
    },
    {
      kind: "simulator-notice",
      code: "compatibility-no-op",
      message: "PAGE is accepted as a listing/documentation directive for MASM compatibility but does not affect VM execution.",
      line: 6,
      column: 1,
      byteOffset: 77,
      spanLength: 4
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals("phase53d-compatibility-notices", source, rawJson, rendered, [
    "[simulator-notice] compatibility-no-op line 1, column 1, byte offset 0, span length 4: .686 is accepted for MASM compatibility but does not change the simulator CPU mode.",
    "[simulator-notice] compatibility-limited line 2, column 1, byte offset 5, span length 6: .model flat, stdcall is accepted for MASM32 textbook compatibility but does not enable real object-file, linker, Windows calling-convention, or WinAPI behavior.",
    "[simulator-notice] compatibility-metadata-only line 3, column 1, byte offset 26, span length 6: .stack size is recorded as parser metadata, but runtime stack instructions and procedure frames remain deferred.",
    "[simulator-notice] compatibility-limited line 4, column 1, byte offset 38, span length 7: INCLUDE Macros.inc is accepted as a virtual compatibility include; general MASM macro expansion remains unsupported until a later macro phase.",
    "[simulator-notice] compatibility-no-op line 5, column 1, byte offset 57, span length 5: TITLE is accepted as a listing/documentation directive for MASM compatibility but does not affect VM execution.",
    "[simulator-notice] compatibility-no-op line 6, column 1, byte offset 77, span length 4: PAGE is accepted as a listing/documentation directive for MASM compatibility but does not affect VM execution.",
    "[info] execution-complete: Execution completed successfully."
  ].join("\n"));
});

test("Phase 53D active semantic constructs do not render compatibility notices", () => {
  const source = `INCLUDE Irvine32.inc
OPTION CASEMAP:ALL
.DATA?
buf DWORD ?
.CONST
limit DWORD 1
.code
main PROC
    exit
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase53d-active-semantics-no-notices", source);
  assertRunStatus(json, true, "ok");
  assert.equal(json.virtualIncludes.irvine32, true, rawJson);
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]);
  assertRenderedEquals("phase53d-active-semantics-no-notices", source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("Phase 53D renders compatibility notice before blocking assembly error", () => {
  const source = `.686
.model small, c
.code
main PROC
    mov eax, 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase53d-notice-plus-error", source);
  assertRunStatus(json, false, "parse-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-notice",
      code: "compatibility-no-op",
      message: ".686 is accepted for MASM compatibility but does not change the simulator CPU mode.",
      line: 1,
      column: 1,
      byteOffset: 0,
      spanLength: 4
    },
    {
      kind: "assembly-error",
      code: "unsupported-model",
      message: ".model form is unsupported. Use `.model flat, stdcall` in MASM32 Educational Mode.",
      line: 2,
      column: 1,
      byteOffset: 5,
      spanLength: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase53d-notice-plus-error", source, rawJson, rendered, "[simulator-notice] compatibility-no-op line 1, column 1, byte offset 0, span length 4: .686 is accepted for MASM compatibility but does not change the simulator CPU mode.\n[assembly-error] unsupported-model line 2, column 1, byte offset 5, span length 6: .model form is unsupported. Use `.model flat, stdcall` in MASM32 Educational Mode.");
});



test("Phase 57 renders IDIV divide-by-zero runtime error", () => {
  const source = `.code
main PROC
    mov eax, 100
    cdq
    mov ebx, 0
    idiv ebx
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57-idiv-divide-by-zero", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "divide-by-zero",
      message: "IDIV divisor operand EBX evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.",
      line: 6,
      column: 5,
      byteOffset: 60,
      spanLength: 8
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57-idiv-divide-by-zero", source, rawJson, rendered, "[runtime-error] divide-by-zero line 6, column 5, byte offset 60, span length 8: IDIV divisor operand EBX evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.");
});

test("Phase 57 renders memory IDIV divide-by-zero with uninitialized warning composition", () => {
  const source = `.DATA?
factor SDWORD ?
.code
main PROC
    mov edx, 0
    mov eax, 100
    idiv factor
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57-idiv-uninitialized-then-divide-by-zero", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Memory read range 00500000h..00500003h reads 4 bytes from factor + 0; 4 of those bytes still originated from uninitialized storage.",
      line: 7,
      column: 10,
      byteOffset: 80,
      spanLength: 6,
      sourceLocation: {
        line: 7,
        column: 10,
        byteOffset: 80,
        spanLength: 6
      },
      symbolName: "factor",
      accessStartAddress: "00500000h",
      accessEndAddress: "00500003h",
      accessSizeBytes: 4,
      uninitializedByteCount: 4,
      initializedByteCount: 0,
      accessByteOffset: 0
    },
    {
      kind: "runtime-error",
      code: "divide-by-zero",
      message: "IDIV divisor operand factor evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.",
      line: 7,
      column: 5,
      byteOffset: 75,
      spanLength: 11
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57-idiv-uninitialized-then-divide-by-zero", source, rawJson, rendered, "[simulator-warning] uninitialized-read line 7, column 10, byte offset 80, span length 6: Memory read range 00500000h..00500003h reads 4 bytes from factor + 0; 4 of those bytes still originated from uninitialized storage.\n[runtime-error] divide-by-zero line 7, column 5, byte offset 75, span length 11: IDIV divisor operand factor evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.");
});

test("Phase 57 renders IDIV quotient-overflow runtime error", () => {
  const source = `.code
main PROC
    mov eax, 80000000h
    cdq
    mov ebx, -1
    idiv ebx
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57-idiv-quotient-overflow", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "quotient-overflow",
      message: "IDIV quotient is too large to fit in quotient register EAX. Execution stopped before updating the quotient register EAX and remainder register EDX.",
      line: 6,
      column: 5,
      byteOffset: 67,
      spanLength: 8
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57-idiv-quotient-overflow", source, rawJson, rendered, "[runtime-error] quotient-overflow line 6, column 5, byte offset 67, span length 8: IDIV quotient is too large to fit in quotient register EAX. Execution stopped before updating the quotient register EAX and remainder register EDX.");
});

test("Phase 57 renders IDIV section-image strict planned-read runtime error", () => {
  const source = `.data
x DWORD 1
.code
main PROC
    mov esi, OFFSET x
    add esi, 4
    mov edx, 0
    mov eax, 10
    idiv DWORD PTR [esi]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57-idiv-section-image-strict", source, { MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION: "strict" });
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "section-image-violation",
      message: "Memory read at 00500004h for 4 bytes covers range 00500004h..00500007h and leaves the section image range for .data/.DATA? (00500000h..00500003h).",
      line: 9,
      column: 20,
      byteOffset: 119,
      spanLength: 5,
      accessKind: "read",
      accessStartAddress: "00500004h",
      accessEndAddress: "00500007h",
      accessSizeBytes: 4,
      ownerSection: ".data/.DATA?",
      boundaryStartAddress: "00500000h",
      boundaryEndAddress: "00500003h"
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57-idiv-section-image-strict", source, rawJson, rendered, "[runtime-error] section-image-violation line 9, column 20, byte offset 119, span length 5: Memory read at 00500004h for 4 bytes covers range 00500004h..00500007h and leaves the section image range for .data/.DATA? (00500000h..00500003h).");
});

test("Phase 56 renders DIV divide-by-zero runtime error", () => {
  const source = `.code
main PROC
    mov eax, 100
    mov edx, 2
    mov ebx, 0
    div ebx
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase56-divide-by-zero", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "divide-by-zero",
      message: "DIV divisor operand EBX evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.",
      line: 6,
      column: 5,
      byteOffset: 67,
      spanLength: 7
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase56-divide-by-zero", source, rawJson, rendered, "[runtime-error] divide-by-zero line 6, column 5, byte offset 67, span length 7: DIV divisor operand EBX evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.");
});

test("Phase 56 renders 8-bit DIV divide-by-zero runtime error with result register names", () => {
  const source = `.code
main PROC
    mov ax, 0017h
    mov bl, 0
    div bl
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase56-divide-by-zero-8bit", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "divide-by-zero",
      message: "DIV divisor operand BL evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register AL and remainder register AH.",
      line: 5,
      column: 5,
      byteOffset: 52,
      spanLength: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase56-divide-by-zero-8bit", source, rawJson, rendered, "[runtime-error] divide-by-zero line 5, column 5, byte offset 52, span length 6: DIV divisor operand BL evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register AL and remainder register AH.");
});

test("Phase 56 renders memory DIV divide-by-zero runtime error with divisor operand text", () => {
  const source = `.data
divisor DWORD 0
.code
main PROC
    mov edx, 0
    mov eax, 100
    div divisor
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase56-divide-by-zero-memory", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "divide-by-zero",
      message: "DIV divisor operand divisor evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.",
      line: 7,
      column: 5,
      byteOffset: 74,
      spanLength: 11
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase56-divide-by-zero-memory", source, rawJson, rendered, "[runtime-error] divide-by-zero line 7, column 5, byte offset 74, span length 11: DIV divisor operand divisor evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.");
});

test("Phase 56 renders DIV quotient-overflow runtime error", () => {
  const source = `.code
main PROC
    mov edx, 1
    mov eax, 0
    mov ebx, 1
    div ebx
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase56-quotient-overflow", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "quotient-overflow",
      message: "DIV quotient is too large to fit in quotient register EAX. Execution stopped before updating the quotient register EAX and remainder register EDX.",
      line: 6,
      column: 5,
      byteOffset: 65,
      spanLength: 7
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase56-quotient-overflow", source, rawJson, rendered, "[runtime-error] quotient-overflow line 6, column 5, byte offset 65, span length 7: DIV quotient is too large to fit in quotient register EAX. Execution stopped before updating the quotient register EAX and remainder register EDX.");
});

test("Phase 56 renders 8-bit DIV quotient-overflow runtime error with result register names", () => {
  const source = `.code
main PROC
    mov ax, 0100h
    mov bl, 1
    div bl
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase56-quotient-overflow-8bit", source);
  assertRunStatus(json, false, "execution-error");
  assert.deepEqual(json.simulatorMessages, [
    {
      kind: "runtime-error",
      code: "quotient-overflow",
      message: "DIV quotient is too large to fit in quotient register AL. Execution stopped before updating the quotient register AL and remainder register AH.",
      line: 5,
      column: 5,
      byteOffset: 52,
      spanLength: 6
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase56-quotient-overflow-8bit", source, rawJson, rendered, "[runtime-error] quotient-overflow line 5, column 5, byte offset 52, span length 6: DIV quotient is too large to fit in quotient register AL. Execution stopped before updating the quotient register AL and remainder register AH.");
});

test("Phase 52A formats source-run register signed display from existing JSON", () => {
  const source = `.code
main PROC
    mov eax, 0FFFFFFFFh
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase52a-register-signed-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  const registers = formatRegisters(json.registers);
  assert.match(registers, /^EAX    \| FFFFFFFFh \/ u: 4294967295 \/ s: -1\s*$/m);
  assert.match(registers, /^  AX   \|     FFFFh \/ u: 65535\s+\/ s: -1\s*$/m);
  assert.match(registers, /^    AL \|       FFh \/ u: 255\s+\/ s: -1\s*$/m);
});

test("Phase 52A formats register aliases using alias display width", () => {
  const source = `.code
main PROC
    mov eax, 000000FFh
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase52a-register-alias-width-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  const registers = formatRegisters(json.registers);
  assert.match(registers, /^EAX    \| 000000FFh \/ u: 255\s+\/ s:  255\s*$/m);
  assert.match(registers, /^    AL \|       FFh \/ u: 255\s+\/ s: -1\s*$/m);
});

test("Phase 52A formats source-run memory changes with signed display", () => {
  const source = `.data
value DWORD 0
b BYTE 0
.code
main PROC
    mov value, 0FFFFFFFFh
    mov b, 0FFh
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase52a-memory-signed-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(formatMemoryChanges(json.memoryChanges), [
    "value DWORD | line 6: mov value, 0FFFFFFFFh",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | FFFFFFFFh / u: 4294967295 / s: -1         ",
    "",
    "b BYTE | line 7: mov b, 0FFh",
    "  old |       00h / u: 0          / s:  0         ",
    "  new |       FFh / u: 255        / s: -1         "
  ].join("\n"));
});

test("Phase 64D renders memory-change source attribution for direct and RMW writes", () => {
  const source = `.data
a DWORD 0
.code
main PROC
    mov a, 1
    inc a
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64d-memory-source-attribution-direct-rmw", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(formatMemoryChanges(json.memoryChanges), [
    "a DWORD | line 5: mov a, 1",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | 00000001h / u: 1          / s:  1         ",
    "",
    "a DWORD | line 6: inc a",
    "  old | 00000001h / u: 1          / s:  1         ",
    "  new | 00000002h / u: 2          / s:  2         "
  ].join("\n"));
});

test("Phase 64D renders memory-change source attribution for indirect writes", () => {
  const source = `.data
a DWORD 0
.code
main PROC
    mov eax, OFFSET a
    mov DWORD PTR [eax], 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64d-memory-source-attribution-indirect", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(formatMemoryChanges(json.memoryChanges), [
    "a DWORD | line 6: mov DWORD PTR [eax], 1",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | 00000001h / u: 1          / s:  1         "
  ].join("\n"));
  assert.doesNotMatch(formatMemoryChanges(json.memoryChanges), /OFFSET a/);
});

test("Phase 64D renders memory-change source attribution for XCHG reg,mem writes", () => {
  const source = `.data
a DWORD 5
.code
main PROC
    mov eax, 10
    xchg eax, a
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64d-memory-source-attribution-xchg-reg-mem", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(formatMemoryChanges(json.memoryChanges), [
    "a DWORD | line 6: xchg eax, a",
    "  old | 00000005h / u: 5          / s:  5         ",
    "  new | 0000000Ah / u: 10         / s:  10        "
  ].join("\n"));
});

test("Phase 52A preserves ordinary MOV signed-memory semantics", () => {
  const source = `.data
sd SDWORD -1
sb SBYTE -1
.code
main PROC
    mov al, sb
    mov eax, sd
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase52a-signed-declaration-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  const registers = formatRegisters(json.registers);
  assert.match(registers, /^EAX    \| FFFFFFFFh \/ u: 4294967295 \/ s: -1\s*$/m);
  assert.match(registers, /^    AL \|       FFh \/ u: 255\s+\/ s: -1\s*$/m);
});


test("Phase 64C formats modeled flag rows after XOR sets ZF", () => {
  const source = `.code
main PROC
    xor eax, eax
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64c-xor-zf-flag-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(json.registers.EFLAGS.hex, "00000040h");
  assertModeledFlags(formatRegisters(json.registers), { CF: 0, ZF: 1, SF: 0, OF: 0 });
});


test("Phase 64C formats modeled flag rows after ADD sets CF", () => {
  const source = `.code
main PROC
    mov al, 0FFh
    add al, 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64c-add-cf-flag-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(json.registers.EFLAGS.hex, "00000041h");
  assertModeledFlags(formatRegisters(json.registers), { CF: 1, ZF: 1, SF: 0, OF: 0 });
});


test("Phase 64C formats modeled flag rows after ADD sets SF and OF", () => {
  const source = `.code
main PROC
    mov al, 7Fh
    add al, 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64c-add-sf-of-flag-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(json.registers.EFLAGS.hex, "00000880h");
  assertModeledFlags(formatRegisters(json.registers), { CF: 0, ZF: 0, SF: 1, OF: 1 });
});


test("Phase 64C formats modeled flag rows after flag-control instructions", () => {
  const source = `.code
main PROC
    stc
    cmc
    stc
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64c-flag-control-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(json.registers.EFLAGS.hex, "00000001h");
  assertModeledFlags(formatRegisters(json.registers), { CF: 1, ZF: 0, SF: 0, OF: 0 });
});


test("Phase 64C formats modeled flag rows after TEST clears CF and OF", () => {
  const source = `.code
main PROC
    mov eax, 80000000h
    stc
    test eax, eax
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64c-test-flag-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(json.registers.EFLAGS.hex, "00000080h");
  assertModeledFlags(formatRegisters(json.registers), { CF: 0, ZF: 0, SF: 1, OF: 0 });
});


test("Phase 64C preserves equality jump behavior while displaying modeled flags", () => {
  const source = `.code
main PROC
    mov eax, 5
    cmp eax, 5
    je equal
    mov ebx, 99
equal:
    mov ecx, 2
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase64c-equality-jump-flag-display", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(rendered, "[info] execution-complete: Execution completed successfully.");
  assert.equal(json.registers.EBX.unsigned, 0);
  assert.equal(json.registers.ECX.unsigned, 2);
  assert.equal(json.registers.EFLAGS.hex, "00000040h");
  assertModeledFlags(formatRegisters(json.registers), { CF: 0, ZF: 1, SF: 0, OF: 0 });
});


test("Phase 57M renders _TEXT OFFSET segment-symbol diagnostic exactly", () => {
  const source = `.code
main PROC
    mov eax, OFFSET _TEXT
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-offset-text-segment-symbol", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`_TEXT` is a MASM/object segment symbol. MASM32 Educational Mode does not expose linker segment symbols or readable `.code` / section images.",
    line: 3,
    column: 21,
    byteOffset: 36,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-offset-text-segment-symbol", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 3, column 21, byte offset 36, span length 5: `_TEXT` is a MASM/object segment symbol. MASM32 Educational Mode does not expose linker segment symbols or readable `.code` / section images.");
});

test("Phase 57M renders _DATA OFFSET segment-symbol diagnostic exactly", () => {
  const source = `.code
main PROC
    mov eax, OFFSET _DATA
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-offset-data-segment-symbol", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`_DATA` is a MASM/object data-segment symbol. Use declared data labels instead; MASM32 Educational Mode does not expose linker segment symbols.",
    line: 3,
    column: 21,
    byteOffset: 36,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-offset-data-segment-symbol", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 3, column 21, byte offset 36, span length 5: `_DATA` is a MASM/object data-segment symbol. Use declared data labels instead; MASM32 Educational Mode does not expose linker segment symbols.");
});

test("Phase 57M renders _BSS OFFSET segment-symbol diagnostic exactly", () => {
  const source = `.code
main PROC
    mov eax, OFFSET _BSS
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-offset-bss-segment-symbol", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`_BSS` is a MASM/object uninitialized-data segment symbol. Use declared data labels in `.DATA?` instead; MASM32 Educational Mode does not expose linker segment symbols.",
    line: 3,
    column: 21,
    byteOffset: 36,
    spanLength: 4
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-offset-bss-segment-symbol", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 3, column 21, byte offset 36, span length 4: `_BSS` is a MASM/object uninitialized-data segment symbol. Use declared data labels in `.DATA?` instead; MASM32 Educational Mode does not expose linker segment symbols.");
});

test("Phase 57M renders CONST OFFSET segment-symbol diagnostic exactly", () => {
  const source = `.code
main PROC
    mov eax, OFFSET CONST
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-offset-const-segment-symbol", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`CONST` is a MASM/object constant-segment symbol. Use declared `.CONST` labels instead; MASM32 Educational Mode does not expose linker segment symbols as addressable symbols.",
    line: 3,
    column: 21,
    byteOffset: 36,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-offset-const-segment-symbol", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 3, column 21, byte offset 36, span length 5: `CONST` is a MASM/object constant-segment symbol. Use declared `.CONST` labels instead; MASM32 Educational Mode does not expose linker segment symbols as addressable symbols.");
});

test("Phase 57M renders STACK OFFSET segment-symbol diagnostic exactly", () => {
  const source = `.code
main PROC
    mov eax, OFFSET STACK
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-offset-stack-segment-symbol", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`STACK` is a MASM/object stack-segment symbol. MASM32 Educational Mode does not expose the simulator stack region as an addressable linker segment symbol.",
    line: 3,
    column: 21,
    byteOffset: 36,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-offset-stack-segment-symbol", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 3, column 21, byte offset 36, span length 5: `STACK` is a MASM/object stack-segment symbol. MASM32 Educational Mode does not expose the simulator stack region as an addressable linker segment symbol.");
});

test("Phase 57M renders FLAT OFFSET group-symbol diagnostic exactly", () => {
  const source = `.code
main PROC
    mov eax, OFFSET FLAT
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-offset-flat-symbol", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`FLAT` is a MASM memory-model group concept. MASM32 Educational Mode uses simulator-defined flat memory regions and does not expose linker groups as addressable symbols.",
    line: 3,
    column: 21,
    byteOffset: 36,
    spanLength: 4
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-offset-flat-symbol", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 3, column 21, byte offset 36, span length 4: `FLAT` is a MASM memory-model group concept. MASM32 Educational Mode uses simulator-defined flat memory regions and does not expose linker groups as addressable symbols.");
});

test("Phase 57M renders DGROUP OFFSET group-symbol diagnostic exactly", () => {
  const source = `.code
main PROC
    mov eax, OFFSET DGROUP
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-offset-dgroup-symbol", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`DGROUP` is a MASM memory-model group concept. MASM32 Educational Mode uses simulator-defined flat memory regions and does not expose linker groups as addressable symbols.",
    line: 3,
    column: 21,
    byteOffset: 36,
    spanLength: 6
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-offset-dgroup-symbol", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 3, column 21, byte offset 36, span length 6: `DGROUP` is a MASM memory-model group concept. MASM32 Educational Mode uses simulator-defined flat memory regions and does not expose linker groups as addressable symbols.");
});

test("Phase 57M renders _TEXT SEGMENT definition diagnostic exactly", () => {
  const source = `_TEXT SEGMENT
_TEXT ENDS
.code
main PROC
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57m-text-segment-definition", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-segment-symbol",
    message: "`_TEXT` is a MASM/object segment symbol. MASM32 Educational Mode does not expose linker segment symbols or readable `.code` / section images.",
    line: 1,
    column: 1,
    byteOffset: 0,
    spanLength: 5
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57m-text-segment-definition", source, rawJson, rendered, "[unsupported-feature] unsupported-segment-symbol line 1, column 1, byte offset 0, span length 5: `_TEXT` is a MASM/object segment symbol. MASM32 Educational Mode does not expose linker segment symbols or readable `.code` / section images.");
});

test("Phase 61D renders token capacity diagnostic exactly", () => {
  const source = `.code
main PROC
${"    mov eax, 1\n".repeat(200)}main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase61d-token-capacity", source);
  assert.equal(json.ok, false, rawJson);
  assert.equal(json.instructionCount, 0, rawJson);
  assert.equal(json.executedInstructionCount, 0, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "assembly-error",
    code: "token-capacity-exceeded",
    message: "token buffer capacity exceeded",
    line: 104,
    column: 12,
    byteOffset: 1542,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assert.equal(rawJson.includes("instruction-limit-exceeded"), false, rawJson);
  assert.equal(rawJson.includes("programConsole"), false, rawJson);
  assertRenderedEquals("phase61d-token-capacity", source, rawJson, rendered, "[assembly-error] token-capacity-exceeded line 104, column 12, byte offset 1542, span length 1: token buffer capacity exceeded");
});

test("Phase 57L renders code memory read diagnostic exactly", () => {
  const source = `.code
main PROC
    mov ebx, 00400000h
    mov al, BYTE PTR [ebx]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57l-code-read", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "unsupported-code-memory-access",
    message: "Memory read at 00400000h for 1 byte overlaps .CODE/_TEXT. The simulator does not expose .CODE/_TEXT as an accessible memory region. Program stopped before access.",
    line: 4,
    column: 5,
    byteOffset: 43,
    spanLength: 22
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57l-code-read", source, rawJson, rendered, "[runtime-error] unsupported-code-memory-access line 4, column 5, byte offset 43, span length 22: Memory read at 00400000h for 1 byte overlaps .CODE/_TEXT. The simulator does not expose .CODE/_TEXT as an accessible memory region. Program stopped before access.");
});

test("Phase 57L renders code memory write diagnostic exactly", () => {
  const source = `.code
main PROC
    mov ebx, 00400000h
    mov BYTE PTR [ebx], 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57l-code-write", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "unsupported-code-memory-access",
    message: "Memory write at 00400000h for 1 byte overlaps .CODE/_TEXT. The simulator does not expose .CODE/_TEXT as an accessible memory region. Program stopped before access.",
    line: 4,
    column: 5,
    byteOffset: 43,
    spanLength: 21
  });
  assert.equal(json.memoryChanges.length, 0, rawJson);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57l-code-write", source, rawJson, rendered, "[runtime-error] unsupported-code-memory-access line 4, column 5, byte offset 43, span length 21: Memory write at 00400000h for 1 byte overlaps .CODE/_TEXT. The simulator does not expose .CODE/_TEXT as an accessible memory region. Program stopped before access.");
});

test("Phase 57L renders code partial-overlap diagnostic exactly", () => {
  const source = `.code
main PROC
    mov ebx, 003FFFFFh
    mov WORD PTR [ebx], 1
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57l-code-partial-overlap", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "runtime-error",
    code: "region-boundary-crossing",
    message: "Cross-region memory write at 003FFFFFh for 2 bytes. The memory address range 003FFFFFh..00400000h crosses/overlaps a no-access memory region, .CODE/_TEXT, that starts at 00400000h. This is not allowed; program stopped before access.",
    line: 4,
    column: 5,
    byteOffset: 43,
    spanLength: 21
  });
  assert.equal(json.memoryChanges.length, 0, rawJson);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57l-code-partial-overlap", source, rawJson, rendered, "[runtime-error] region-boundary-crossing line 4, column 5, byte offset 43, span length 21: Cross-region memory write at 003FFFFFh for 2 bytes. The memory address range 003FFFFFh..00400000h crosses/overlaps a no-access memory region, .CODE/_TEXT, that starts at 00400000h. This is not allowed; program stopped before access.");
});

test("Phase 57-CORR2 renders compact negative displacement write success", () => {
  const source = `.data
x DWORD 0, 0
.code
main PROC
    mov eax, OFFSET x
    add eax, 4
    mov DWORD PTR [eax-4], 10
    mov ebx, x
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57corr2-compact-negative-write", source);
  assert.equal(json.ok, true, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals("phase57corr2-compact-negative-write", source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("Phase 57-CORR2 renders compact negative displacement read success", () => {
  const source = `.data
x DWORD 10, 20
.code
main PROC
    mov eax, OFFSET x
    add eax, 4
    mov ebx, DWORD PTR [eax-4]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57corr2-compact-negative-read", source);
  assert.equal(json.ok, true, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals("phase57corr2-compact-negative-read", source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("Phase 57-CORR2 renders compact negative displacement LEA success", () => {
  const source = `.data
x DWORD 0, 0
.code
main PROC
    mov ebx, OFFSET x
    add ebx, 4
    lea eax, [ebx-4]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57corr2-compact-negative-lea", source);
  assert.equal(json.ok, true, rawJson);
  assert.equal(json.memoryChanges.length, 0, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "info",
    code: "execution-complete",
    message: "Execution completed successfully."
  });
  assertRenderedEquals("phase57corr2-compact-negative-lea", source, rawJson, rendered, "[info] execution-complete: Execution completed successfully.");
});

test("Phase 57-CORR2 renders compact malformed advanced address diagnostic", () => {
  const source = `.data
x DWORD 0, 0
.code
main PROC
    mov eax, OFFSET x
    mov ebx, DWORD PTR [eax-4*2]
main ENDP
END main
`;
  const { json, rawJson, rendered } = runFixture("phase57corr2-compact-negative-advanced-rejection", source);
  assert.equal(json.ok, false, rawJson);
  assertMessageEquals(json.simulatorMessages[0], {
    kind: "unsupported-feature",
    code: "unsupported-scaled-index",
    message: "Scaled-index memory operands are not supported yet.",
    line: 6,
    column: 30,
    byteOffset: 86,
    spanLength: 1
  });
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals("phase57corr2-compact-negative-advanced-rejection", source, rawJson, rendered, "[unsupported-feature] unsupported-scaled-index line 6, column 30, byte offset 86, span length 1: Scaled-index memory operands are not supported yet.");
});
