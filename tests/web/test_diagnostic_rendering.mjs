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

/** Environment variables that control the native diagnostic producer. */
const PRODUCER_CONTROL_ENV_KEYS = [
  "MASM32_DIAGNOSTIC_MEMORY_VALIDATION",
  "MASM32_DIAGNOSTIC_LAYOUT_MODE",
  "MASM32_DIAGNOSTIC_AUTO_DATA_LIMIT",
  "MASM32_DIAGNOSTIC_AUTO_STACK_LIMIT",
  "MASM32_DIAGNOSTIC_AUTO_HEAP_REQUEST",
  "MASM32_DIAGNOSTIC_AUTO_HEAP_LIMIT",
  "MASM32_DIAGNOSTIC_AUTO_TOTAL_LIMIT",
  "MASM32_DIAGNOSTIC_SHIFT_VALIDATION",
  "MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE"
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

  return { ...env, ...extraEnv };
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
      message: "Recognized Irvine32 routine, but executable Irvine32 routine behavior is deferred to a later milestone.",
      line: 4,
      column: 5,
      byteOffset: 41,
      spanLength: 11
    }
  ]);
  assertNoExecutionComplete(json.simulatorMessages);
  assertRenderedEquals(name, source, rawJson, rendered, "[unsupported-feature] unsupported-irvine32-routine line 4, column 5, byte offset 41, span length 11: Recognized Irvine32 routine, but executable Irvine32 routine behavior is deferred to a later milestone.");
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
    "[simulator-warning] uninitialized-read line 5: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n[info] execution-complete: Execution completed successfully.",
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
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] unaligned-memory-access line 6: Unaligned DWORD memory access at 00500001h.\n[info] execution-complete: Execution completed successfully.");
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
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] object-bounds-warning line 6: Memory read at 00500040h for 4 bytes is inside a valid region but outside any declared data object.\n[info] execution-complete: Execution completed successfully.");
});



test("renders allocated-object strict violation exactly", () => {
  const name = "objectBoundsWarning";
  const source = fixtureSource(name);
  const { json, rawJson, rendered } = runFixture(name, source, {
    MASM32_DIAGNOSTIC_MEMORY_VALIDATION: "allocated-object-strict"
  });
  assertRunStatus(json, false, "execution-error");
  assert.equal(json.instructionCount, 2);
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
      sourceLocation: {
        line: 5,
        column: null,
        byteOffset: null,
        spanLength: null
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
  assertRenderedEquals(name, source, rawJson, rendered, "[simulator-warning] uninitialized-read line 5: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.\n[info] execution-complete: Execution completed successfully.");
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
    "value DWORD",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | FFFFFFFFh / u: 4294967295 / s: -1         ",
    "",
    "b BYTE",
    "  old |       00h / u: 0          / s:  0         ",
    "  new |       FFh / u: 255        / s: -1         "
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
