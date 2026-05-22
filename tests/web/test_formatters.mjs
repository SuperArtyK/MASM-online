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

test("formats canonical registers and aliases in stable order", () => {
  assert.equal(formatRegisters({
    EBX: { hex: "00000003h", unsigned: 3 },
    EAX: { hex: "0000002Ah", unsigned: 42 },
    EFLAGS: { hex: "00000040h", unsigned: 64 }
  }), [
    "EAX    | 0000002Ah / u: 42         / s:  42        ",
    "  AX   |     002Ah / u: 42         / s:  42        ",
    "  AH   |     00h   / u: 0          / s:  0         ",
    "    AL |       2Ah / u: 42         / s:  42        ",
    "",
    "EBX    | 00000003h / u: 3          / s:  3         ",
    "  BX   |     0003h / u: 3          / s:  3         ",
    "  BH   |     00h   / u: 0          / s:  0         ",
    "    BL |       03h / u: 3          / s:  3         ",
    "",
    "EFLAGS | 00000040h / 64"
  ].join("\n"));
});

test("separates independent register groups with blank lines", () => {
  assert.equal(formatRegisters({
    EAX: { hex: "00000001h", unsigned: 1 },
    ECX: { hex: "00000002h", unsigned: 2 },
    EDX: { hex: "00000003h", unsigned: 3 }
  }), [
    "EAX    | 00000001h / u: 1          / s:  1         ",
    "  AX   |     0001h / u: 1          / s:  1         ",
    "  AH   |     00h   / u: 0          / s:  0         ",
    "    AL |       01h / u: 1          / s:  1         ",
    "",
    "ECX    | 00000002h / u: 2          / s:  2         ",
    "  CX   |     0002h / u: 2          / s:  2         ",
    "  CH   |     00h   / u: 0          / s:  0         ",
    "    CL |       02h / u: 2          / s:  2         ",
    "",
    "EDX    | 00000003h / u: 3          / s:  3         ",
    "  DX   |     0003h / u: 3          / s:  3         ",
    "  DH   |     00h   / u: 0          / s:  0         ",
    "    DL |       03h / u: 3          / s:  3         "
  ].join("\n"));
});

test("formats register alias widths independently from parent register width", () => {
  assert.equal(formatRegisters({
    EAX: { hex: "000000FFh", unsigned: 255 }
  }), [
    "EAX    | 000000FFh / u: 255        / s:  255       ",
    "  AX   |     00FFh / u: 255        / s:  255       ",
    "  AH   |     00h   / u: 0          / s:  0         ",
    "    AL |       FFh / u: 255        / s: -1         "
  ].join("\n"));

  assert.equal(formatRegisters({
    EAX: { hex: "FFFFFFFFh", unsigned: 4294967295 }
  }), [
    "EAX    | FFFFFFFFh / u: 4294967295 / s: -1         ",
    "  AX   |     FFFFh / u: 65535      / s: -1         ",
    "  AH   |     FFh   / u: 255        / s: -1         ",
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
    "  AH   |     12h   / u: 18         / s:  18        ",
    "    AL |       34h / u: 52         / s:  52        ",
    "",
    "EBX    | 00005678h / u: 22136      / s:  22136     ",
    "  BX   |     5678h / u: 22136      / s:  22136     ",
    "  BH   |     56h   / u: 86         / s:  86        ",
    "    BL |       78h / u: 120        / s:  120       ",
    "",
    "ECX    | 00009ABCh / u: 39612      / s:  39612     ",
    "  CX   |     9ABCh / u: 39612      / s: -25924     ",
    "  CH   |     9Ah   / u: 154        / s: -102       ",
    "    CL |       BCh / u: 188        / s: -68        ",
    "",
    "EDX    | 0000DEF0h / u: 57072      / s:  57072     ",
    "  DX   |     DEF0h / u: 57072      / s: -8464      ",
    "  DH   |     DEh   / u: 222        / s: -34        ",
    "    DL |       F0h / u: 240        / s: -16        "
  ].join("\n"));
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
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000064h",
    newUnsigned: 100
  }), [
    "nums + 8 DWORD",
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
