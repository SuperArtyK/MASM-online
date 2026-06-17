/*
 * @file test_formatters.mjs
 * @brief Unit tests for pure browser formatting helpers.
 *
 * These tests cover register, memory-change, integer display, and diagnostic
 * formatting behavior without importing the browser entry point, which would
 * create DOM and Worker side effects.
 */

import assert from "node:assert/strict";
import {
  formatIntegerDisplay,
  formatMemoryChangeLine,
  formatMemoryChanges,
  formatRegisterLine,
  formatRegisters,
  formatSimulatorMessages
} from "../../web/src/formatters.js";

/**
 * Runs one named formatter test.
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
 * Returns one formatted register-display line by register row name.
 *
 * @param {string} formatted Full formatted register table.
 * @param {string} rowPrefix Expected line prefix, including indentation.
 * @returns {string} Matching line.
 */
function findRegisterLine(formatted, rowPrefix) {
  const line = formatted.split("\n").find((candidate) => candidate.startsWith(rowPrefix));
  assert.notEqual(line, undefined, `missing register line ${rowPrefix}`);
  return line;
}

test("formats Phase 52A signed integer boundary values", () => {
  assert.equal(formatIntegerDisplay({ hex: "00h", unsigned: 0 }, 8), "00h / u:0 / s:0");
  assert.equal(formatIntegerDisplay({ hex: "01h", unsigned: 1 }, 8), "01h / u:1 / s:1");
  assert.equal(formatIntegerDisplay({ hex: "7Fh", unsigned: 127 }, 8), "7Fh / u:127 / s:127");
  assert.equal(formatIntegerDisplay({ hex: "80h", unsigned: 128 }, 8), "80h / u:128 / s:-128");
  assert.equal(formatIntegerDisplay({ hex: "FFh", unsigned: 255 }, 8), "FFh / u:255 / s:-1");

  assert.equal(formatIntegerDisplay({ hex: "0000h", unsigned: 0 }, 16), "0000h / u:0 / s:0");
  assert.equal(formatIntegerDisplay({ hex: "0001h", unsigned: 1 }, 16), "0001h / u:1 / s:1");
  assert.equal(formatIntegerDisplay({ hex: "7FFFh", unsigned: 32767 }, 16), "7FFFh / u:32767 / s:32767");
  assert.equal(formatIntegerDisplay({ hex: "8000h", unsigned: 32768 }, 16), "8000h / u:32768 / s:-32768");
  assert.equal(formatIntegerDisplay({ hex: "FFFFh", unsigned: 65535 }, 16), "FFFFh / u:65535 / s:-1");

  assert.equal(formatIntegerDisplay({ hex: "00000000h", unsigned: 0 }, 32), "00000000h / u:0 / s:0");
  assert.equal(formatIntegerDisplay({ hex: "00000001h", unsigned: 1 }, 32), "00000001h / u:1 / s:1");
  assert.equal(formatIntegerDisplay({ hex: "7FFFFFFFh", unsigned: 2147483647 }, 32), "7FFFFFFFh / u:2147483647 / s:2147483647");
  assert.equal(formatIntegerDisplay({ hex: "80000000h", unsigned: 2147483648 }, 32), "80000000h / u:2147483648 / s:-2147483648");
  assert.equal(formatIntegerDisplay({ hex: "FFFFFFFFh", unsigned: 4294967295 }, 32), "FFFFFFFFh / u:4294967295 / s:-1");
});

test("does not guess signed display for unsupported widths or missing values", () => {
  assert.equal(formatIntegerDisplay({ hex: "FFFFFFFFFFFFFFFFh", unsigned: 0 }, 64), null);
  assert.equal(formatIntegerDisplay({ hex: "FFh" }, 8), "FFh / u:255 / s:-1");
  assert.equal(formatIntegerDisplay({}, 8), null);
});

test("normalizes known-width signed display hexadecimal text", () => {
  assert.equal(formatIntegerDisplay({ hex: "ffh" }, 8), "FFh / u:255 / s:-1");
  assert.equal(formatIntegerDisplay({ hex: "not-hex", unsigned: 255 }, 32), "000000FFh / u:255 / s:255");
});

test("formats a single register row with a known signed display width", () => {
  assert.equal(formatRegisterLine("EAX", { hex: "0000002Ah", unsigned: 42 }, 32), "EAX    | 0000002Ah / u: 42         / s:  42        ");
});

test("formats a single register row with the legacy fallback when width is unknown", () => {
  assert.equal(formatRegisterLine("EAX", { hex: "0000002Ah", unsigned: 42 }), "EAX    | 0000002Ah / 42");
});

test("formats Phase 69B register groups and aliases in stable order", () => {
  assert.equal(formatRegisters({
    EBX: { hex: "00000003h", unsigned: 3 },
    EAX: { hex: "0000002Ah", unsigned: 42 },
    EFLAGS: { hex: "00000040h", unsigned: 64 }
  }), [
    "EAX    | 0000002Ah / u: 42         / s:  42        ",
    "  AX   |     002Ah / u: 42         / s:  42        ",
    "    AH |     00h   / u: 0          / s:  0         ",
    "    AL |       2Ah / u: 42         / s:  42        ",
    "       |",
    "EBX    | 00000003h / u: 3          / s:  3         ",
    "  BX   |     0003h / u: 3          / s:  3         ",
    "    BH |     00h   / u: 0          / s:  0         ",
    "    BL |       03h / u: 3          / s:  3         ",
    "EFLAGS | 00000040h / 64                            ",
    "  CF   | 0",
    "  ZF   | 1",
    "  SF   | 0",
    "  OF   | 0"
  ].join("\n"));
});


test("formats Phase 64C modeled flag child rows under EFLAGS", () => {
  const formatted = formatRegisters({
    EFLAGS: { hex: "000008C1h", unsigned: 2241 }
  });

  assert.equal(formatted, [
    "EFLAGS | 000008C1h / 2241                          ",
    "  CF   | 1",
    "  ZF   | 1",
    "  SF   | 1",
    "  OF   | 1"
  ].join("\n"));
  assert.doesNotMatch(formatted, /PF|AF|DF|IF|TF/);
  assert.doesNotMatch(formatted, /undefined|architecturally/i);
});


test("formats Phase 64C modeled flag bits from EFLAGS hexadecimal fallback", () => {
  const formatted = formatRegisters({
    EFLAGS: { hex: "00000801h" }
  });

  assert.equal(findRegisterLine(formatted, "  CF"), "  CF   | 1");
  assert.equal(findRegisterLine(formatted, "  ZF"), "  ZF   | 0");
  assert.equal(findRegisterLine(formatted, "  SF"), "  SF   | 0");
  assert.equal(findRegisterLine(formatted, "  OF"), "  OF   | 1");
});


test("formats unchanged markers on canonical parent rows only", () => {
  const registers = {
    EAX: { hex: "00000000h", unsigned: 0 },
    EBX: { hex: "00000000h", unsigned: 0 },
    ECX: { hex: "00000000h", unsigned: 0 },
    EDX: { hex: "00000000h", unsigned: 0 },
    ESI: { hex: "00000000h", unsigned: 0 },
    EDI: { hex: "00000000h", unsigned: 0 },
    EBP: { hex: "00000000h", unsigned: 0 },
    ESP: { hex: "00000000h", unsigned: 0 },
    EIP: { hex: "00000000h", unsigned: 0 },
    EFLAGS: { hex: "00000000h", unsigned: 0 }
  };
  const registerWrites = {
    EAX: false,
    EBX: false,
    ECX: false,
    EDX: false,
    ESI: false,
    EDI: false,
    EBP: false,
    ESP: false,
    EIP: false,
    EFLAGS: false
  };
  const formatted = formatRegisters(registers, registerWrites);

  ["EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP", "EIP", "EFLAGS"].forEach((name) => {
    assert.match(findRegisterLine(formatted, name), /\[unchanged\]$/);
  });
  ["  AX", "    AH", "    AL", "  BX", "    BH", "    BL", "  CX", "    CH", "    CL", "  DX", "    DH", "    DL", "  SI", "  DI", "  BP", "  SP", "  CF", "  ZF", "  SF", "  OF"].forEach((name) => {
    assert.doesNotMatch(findRegisterLine(formatted, name), /\[unchanged\]/);
  });
});


test("formats Phase 68B displayed EIP as derived control state", () => {
  const formatted = formatRegisters({
    EIP: { hex: "00401000h", unsigned: 4198400 }
  }, {
    EIP: false
  }, {
    EIP: "derived-control-state"
  });

  assert.match(findRegisterLine(formatted, "EIP"), /\[derived control state\]$/);
  assert.doesNotMatch(findRegisterLine(formatted, "EIP"), /\[unchanged\]/);
});


test("aligns EFLAGS and adds readable marker spacing for wide values", () => {
  const formatted = formatRegisters({
    EAX: { hex: "80000000h", unsigned: 2147483648 },
    EFLAGS: { hex: "80000000h", unsigned: 2147483648 }
  }, {
    EAX: false,
    EFLAGS: false
  });

  assert.equal(
    findRegisterLine(formatted, "EAX"),
    "EAX    | 80000000h / u: 2147483648 / s: -2147483648     [unchanged]"
  );
  assert.equal(
    findRegisterLine(formatted, "EFLAGS"),
    "EFLAGS | 80000000h / 2147483648                         [unchanged]"
  );
  assert.equal(findRegisterLine(formatted, "  CF"), "  CF   | 0");
  assert.equal(findRegisterLine(formatted, "  ZF"), "  ZF   | 0");
  assert.equal(findRegisterLine(formatted, "  SF"), "  SF   | 0");
  assert.equal(findRegisterLine(formatted, "  OF"), "  OF   | 0");
});

test("omits unchanged marker for parent and alias register-family writes", () => {
  const registers = {
    EAX: { hex: "00000000h", unsigned: 0 },
    ESI: { hex: "00000000h", unsigned: 0 },
    ESP: { hex: "00000000h", unsigned: 0 }
  };
  const formatted = formatRegisters(registers, { EAX: true, ESI: true, ESP: true });

  assert.doesNotMatch(findRegisterLine(formatted, "EAX"), /\[unchanged\]/);
  assert.doesNotMatch(findRegisterLine(formatted, "ESI"), /\[unchanged\]/);
  assert.doesNotMatch(findRegisterLine(formatted, "ESP"), /\[unchanged\]/);
  assert.doesNotMatch(formatted, /^  AX.*\[unchanged\]$/m);
  assert.doesNotMatch(formatted, /^  SI.*\[unchanged\]$/m);
  assert.doesNotMatch(formatted, /^  SP.*\[unchanged\]$/m);
});

test("keeps legacy register formatting when write metadata is absent", () => {
  const formatted = formatRegisters({
    EAX: { hex: "00000000h", unsigned: 0 }
  });

  assert.doesNotMatch(formatted, /\[unchanged\]/);
  assert.equal(findRegisterLine(formatted, "EAX"), "EAX    | 00000000h / u: 0          / s:  0         ");
});


test("does not synthesize modeled flag rows when EFLAGS is unavailable", () => {
  const formatted = formatRegisters({
    EAX: { hex: "00000000h", unsigned: 0 }
  });

  assert.doesNotMatch(formatted, /^  (CF|ZF|SF|OF)\s+\|/m);
});

test("keeps general-register parent families in one Phase 69B group", () => {
  assert.equal(formatRegisters({
    EAX: { hex: "00000001h", unsigned: 1 },
    ECX: { hex: "00000002h", unsigned: 2 },
    EDX: { hex: "00000003h", unsigned: 3 }
  }), [
    "EAX    | 00000001h / u: 1          / s:  1         ",
    "  AX   |     0001h / u: 1          / s:  1         ",
    "    AH |     00h   / u: 0          / s:  0         ",
    "    AL |       01h / u: 1          / s:  1         ",
    "       |",
    "ECX    | 00000002h / u: 2          / s:  2         ",
    "  CX   |     0002h / u: 2          / s:  2         ",
    "    CH |     00h   / u: 0          / s:  0         ",
    "    CL |       02h / u: 2          / s:  2         ",
    "       |",
    "EDX    | 00000003h / u: 3          / s:  3         ",
    "  DX   |     0003h / u: 3          / s:  3         ",
    "    DH |     00h   / u: 0          / s:  0         ",
    "    DL |       03h / u: 3          / s:  3         "
  ].join("\n"));
});

test("formats register alias widths independently from parent register width", () => {
  assert.equal(formatRegisters({
    EAX: { hex: "000000FFh", unsigned: 255 }
  }), [
    "EAX    | 000000FFh / u: 255        / s:  255       ",
    "  AX   |     00FFh / u: 255        / s:  255       ",
    "    AH |     00h   / u: 0          / s:  0         ",
    "    AL |       FFh / u: 255        / s: -1         "
  ].join("\n"));

  assert.equal(formatRegisters({
    EAX: { hex: "FFFFFFFFh", unsigned: 4294967295 }
  }), [
    "EAX    | FFFFFFFFh / u: 4294967295 / s: -1         ",
    "  AX   |     FFFFh / u: 65535      / s: -1         ",
    "    AH |     FFh   / u: 255        / s: -1         ",
    "    AL |       FFh / u: 255        / s: -1         "
  ].join("\n"));
});

test("aligns high-byte aliases under the high byte of 16-bit aliases", () => {
  assert.equal(formatRegisters({
    EAX: { hex: "00001234h", unsigned: 4660 },
    EBX: { hex: "00005678h", unsigned: 22136 },
    ECX: { hex: "00009ABCh", unsigned: 39612 },
    EDX: { hex: "0000DEF0h", unsigned: 57072 }
  }), [
    "EAX    | 00001234h / u: 4660       / s:  4660      ",
    "  AX   |     1234h / u: 4660       / s:  4660      ",
    "    AH |     12h   / u: 18         / s:  18        ",
    "    AL |       34h / u: 52         / s:  52        ",
    "       |",
    "EBX    | 00005678h / u: 22136      / s:  22136     ",
    "  BX   |     5678h / u: 22136      / s:  22136     ",
    "    BH |     56h   / u: 86         / s:  86        ",
    "    BL |       78h / u: 120        / s:  120       ",
    "       |",
    "ECX    | 00009ABCh / u: 39612      / s:  39612     ",
    "  CX   |     9ABCh / u: 39612      / s: -25924     ",
    "    CH |     9Ah   / u: 154        / s: -102       ",
    "    CL |       BCh / u: 188        / s: -68        ",
    "       |",
    "EDX    | 0000DEF0h / u: 57072      / s:  57072     ",
    "  DX   |     DEF0h / u: 57072      / s: -8464      ",
    "    DH |     DEh   / u: 222        / s: -34        ",
    "    DL |       F0h / u: 240        / s: -16        "
  ].join("\n"));
});



test("inserts exact Phase 69B visual separators at canonical register boundaries", () => {
  const formatted = formatRegisters({
    EAX: { hex: "00000000h", unsigned: 0 },
    EBX: { hex: "00000000h", unsigned: 0 },
    ECX: { hex: "00000000h", unsigned: 0 },
    EDX: { hex: "00000000h", unsigned: 0 },
    ESI: { hex: "00000000h", unsigned: 0 },
    EDI: { hex: "00000000h", unsigned: 0 },
    EBP: { hex: "00000000h", unsigned: 0 },
    ESP: { hex: "00000000h", unsigned: 0 },
    EIP: { hex: "00401000h", unsigned: 4198400 },
    EFLAGS: { hex: "00000000h", unsigned: 0 }
  });
  const lines = formatted.split("\n");
  const parentSpacerIndexes = lines
    .map((line, index) => line === "       |" ? index : -1)
    .filter((index) => index >= 0);
  const majorDividerIndexes = lines
    .map((line, index) => line === "-------------------------------------------------------------------" ? index : -1)
    .filter((index) => index >= 0);

  assert.deepEqual(parentSpacerIndexes, [4, 9, 14, 22, 28, 33]);
  assert.deepEqual(majorDividerIndexes, [19, 25, 31]);
  assert.equal(majorDividerIndexes.length, 3);
  assert.notEqual(lines[0], "       |");
  assert.notEqual(lines[0], "-------------------------------------------------------------------");
  assert.notEqual(lines[lines.length - 1], "       |");
  assert.notEqual(lines[lines.length - 1], "-------------------------------------------------------------------");
  assert.equal(lines[3].startsWith("    AL"), true);
  assert.equal(lines[5].startsWith("EBX"), true);
  assert.equal(lines[18].startsWith("    DL"), true);
  assert.equal(lines[20].startsWith("ESI"), true);
  assert.equal(lines[24].startsWith("  DI"), true);
  assert.equal(lines[26].startsWith("EBP"), true);
  assert.equal(lines[30].startsWith("  SP"), true);
  assert.equal(lines[32].startsWith("EIP"), true);
  assert.equal(lines[34].startsWith("EFLAGS"), true);
});

test("formats missing registers as unavailable", () => {
  assert.equal(formatRegisters(undefined), "No register state available.");
});

test("formats simulator messages with line and column", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "assembly-error",
      code: "unsupported-section",
      line: 3,
      column: 1,
      message: "Unsupported section."
    }
  ]), "[assembly-error] unsupported-section line 3, column 1: Unsupported section.");
});

test("formats simulator messages with byte offset and span length", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "assembly-error",
      code: "invalid-hex-literal",
      line: 3,
      column: 14,
      byteOffset: 29,
      spanLength: 2,
      message: "Invalid hexadecimal literal."
    }
  ]), "[assembly-error] invalid-hex-literal line 3, column 14, byte offset 29, span length 2: Invalid hexadecimal literal.");
});

test("formats Phase 77 PROC USES stack diagnostics with procedure context", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "runtime-error",
      code: "stack-overflow",
      line: 5,
      column: 5,
      byteOffset: 64,
      spanLength: 11,
      procedure: "Helper",
      message: "Automatic PROC USES register save failed."
    }
  ]), "[runtime-error] stack-overflow line 5, column 5, byte offset 64, span length 11, procedure Helper: Automatic PROC USES register save failed.");
});

test("formats Phase 53E UI setting errors through Simulator Messages", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "ui-error",
      code: "invalid-diagnostic-setting",
      message: "Invalid diagnostic setting 'memoryRange'. Accepted values: region-only."
    }
  ]), "[ui-error] invalid-diagnostic-setting: Invalid diagnostic setting 'memoryRange'. Accepted values: region-only.");
});

test("formats Phase 57F startup setting errors through Simulator Messages", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "ui-error",
      code: "invalid-startup-setting",
      message: "Invalid startup setting 'startupStateSeed'. Accepted values: unsigned 32-bit integer."
    }
  ]), "[ui-error] invalid-startup-setting: Invalid startup setting 'startupStateSeed'. Accepted values: unsigned 32-bit integer.");
});

test("formats Phase 69B startup notice and completion as separate rendered groups", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "simulator-notice",
      code: "startup-state-notice",
      message: "Startup notice."
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]), "[simulator-notice] startup-state-notice: Startup notice.\n\n[info] execution-complete: Execution completed successfully.");
});

test("formats Phase 69B startup, runtime warning, and completion groups", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "simulator-notice",
      code: "startup-state-notice",
      message: "Startup notice."
    },
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Runtime warning."
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]), "[simulator-notice] startup-state-notice: Startup notice.\n\n[simulator-warning] uninitialized-read: Runtime warning.\n\n[info] execution-complete: Execution completed successfully.");
});

test("formats Phase 69B runtime warning and completion with startup notice suppressed", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Runtime warning."
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]), "[simulator-warning] uninitialized-read: Runtime warning.\n\n[info] execution-complete: Execution completed successfully.");
});

test("formats Phase 69B multiple runtime diagnostics without internal separators", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "simulator-notice",
      code: "startup-state-notice",
      message: "Startup notice."
    },
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Runtime warning."
    },
    {
      kind: "runtime-error",
      code: "invalid-address",
      message: "Runtime error."
    }
  ]), "[simulator-notice] startup-state-notice: Startup notice.\n\n[simulator-warning] uninitialized-read: Runtime warning.\n[runtime-error] invalid-address: Runtime error.");
});

test("formats Phase 69B pre-execution diagnostics without group separators", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "ui-error",
      code: "invalid-startup-setting",
      message: "Invalid startup setting."
    },
    {
      kind: "assembly-error",
      code: "ambiguous-memory-width",
      line: 3,
      column: 9,
      message: "Memory operand width is ambiguous."
    }
  ]), "[ui-error] invalid-startup-setting: Invalid startup setting.\n[assembly-error] ambiguous-memory-width line 3, column 9: Memory operand width is ambiguous.");
});

test("formats Phase 69B compatibility notices as a pre-execution group before final status", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "simulator-notice",
      code: "compatibility-no-op",
      line: 1,
      column: 1,
      message: ".386 is accepted."
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]), "[simulator-notice] compatibility-no-op line 1, column 1: .386 is accepted.\n\n[info] execution-complete: Execution completed successfully.");
});

test("formats Phase 69B startup, pre-execution, runtime, and final groups", () => {
  assert.equal(formatSimulatorMessages([
    {
      kind: "simulator-notice",
      code: "startup-state-notice",
      message: "Startup notice."
    },
    {
      kind: "assembly-warning",
      code: "casemap-policy-changed",
      message: "Pre-execution warning."
    },
    {
      kind: "simulator-warning",
      code: "uninitialized-read",
      message: "Runtime warning."
    },
    {
      kind: "info",
      code: "execution-complete",
      message: "Execution completed successfully."
    }
  ]), "[simulator-notice] startup-state-notice: Startup notice.\n\n[assembly-warning] casemap-policy-changed: Pre-execution warning.\n\n[simulator-warning] uninitialized-read: Runtime warning.\n\n[info] execution-complete: Execution completed successfully.");
});

test("formats empty simulator messages", () => {
  assert.equal(formatSimulatorMessages([]), "No simulator messages.");
});

test("formats partial simulator message objects defensively", () => {
  assert.equal(formatSimulatorMessages([{}]), "[message] unknown: ");
});


test("formats one symbol-aware memory change with signed display", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "var",
    widthBits: 8,
    oldHex: "00h",
    oldUnsigned: 0,
    newHex: "64h",
    newUnsigned: 100
  }), [
    "var",
    "  old |       00h / u: 0          / s:  0         ",
    "  new |       64h / u: 100        / s:  100       "
  ].join("\n"));
});

test("formats Phase 64D memory-change source line and text attribution", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "a",
    dataType: "DWORD",
    widthBits: 32,
    sourceLine: 10,
    sourceText: "inc a",
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000001h",
    newUnsigned: 1
  }), [
    "a DWORD | line 10: inc a",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | 00000001h / u: 1          / s:  1         "
  ].join("\n"));
});

test("formats Phase 64D memory-change source line without fabricated source text", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "a",
    dataType: "DWORD",
    widthBits: 32,
    sourceLine: 10,
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000001h",
    newUnsigned: 1
  }), [
    "a DWORD | line 10",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | 00000001h / u: 1          / s:  1         "
  ].join("\n"));
});

test("formats memory changes and empty memory changes", () => {
  assert.equal(formatMemoryChanges([{
    symbol: "var",
    widthBits: 8,
    oldHex: "00h",
    oldUnsigned: 0,
    newHex: "64h",
    newUnsigned: 100
  }, {
    symbol: "other",
    widthBits: 16,
    oldHex: "0000h",
    oldUnsigned: 0,
    newHex: "FFFFh",
    newUnsigned: 65535
  }]), [
    "var",
    "  old |       00h / u: 0          / s:  0         ",
    "  new |       64h / u: 100        / s:  100       ",
    "",
    "other",
    "  old |     0000h / u: 0          / s:  0         ",
    "  new |     FFFFh / u: 65535      / s: -1         "
  ].join("\n"));
  assert.equal(formatMemoryChanges([]), "No memory changes.");
});

test("formats offset-zero bracketed symbol changes as direct typed symbol blocks", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "nums",
    dataType: "DWORD",
    widthBits: 32,
    byteOffset: 0,
    elementIndex: 0,
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000064h",
    newUnsigned: 100
  }), [
    "nums DWORD",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | 00000064h / u: 100        / s:  100       "
  ].join("\n"));
});

test("formats symbol-offset memory changes with byte offset and element index", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "nums",
    dataType: "DWORD",
    widthBits: 32,
    byteOffset: 8,
    elementIndex: 2,
    sourceLine: 12,
    sourceText: "mov nums[8], 100",
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000064h",
    newUnsigned: 100
  }), [
    "nums + 8 DWORD | line 12: mov nums[8], 100",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | 00000064h / u: 100        / s:  100       ",
    "  info| byte offset +8, element index 2"
  ].join("\n"));
});

test("formats unaligned symbol-offset memory changes without element index", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "nums",
    dataType: "DWORD",
    widthBits: 32,
    byteOffset: 9,
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000064h",
    newUnsigned: 100
  }), [
    "nums + 9 DWORD",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | 00000064h / u: 100        / s:  100       ",
    "  info| byte offset +9"
  ].join("\n"));
});

test("formats DWORD and BYTE memory changes with signed interpretation by access width", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "value",
    dataType: "DWORD",
    widthBits: 32,
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "FFFFFFFFh",
    newUnsigned: 4294967295
  }), [
    "value DWORD",
    "  old | 00000000h / u: 0          / s:  0         ",
    "  new | FFFFFFFFh / u: 4294967295 / s: -1         "
  ].join("\n"));

  assert.equal(formatMemoryChangeLine({
    symbol: "b",
    dataType: "BYTE",
    widthBits: 8,
    oldHex: "00h",
    oldUnsigned: 0,
    newHex: "FFh",
    newUnsigned: 255
  }), [
    "b BYTE",
    "  old |       00h / u: 0          / s:  0         ",
    "  new |       FFh / u: 255        / s: -1         "
  ].join("\n"));
});

test("keeps legacy memory display when memory width is unavailable", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "unknownWidth",
    oldHex: "00h",
    oldUnsigned: 0,
    newHex: "FFh",
    newUnsigned: 255
  }), [
    "unknownWidth",
    "  old | 00h / 0",
    "  new | FFh / 255"
  ].join("\n"));
});
