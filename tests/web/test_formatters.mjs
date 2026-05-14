/*
 * @file test_formatters.mjs
 * @brief Unit tests for Milestone 14 UI formatting helpers.
 *
 * These tests cover pure formatting behavior without importing the browser entry
 * point, which would create DOM and Worker side effects.
 */

import assert from "node:assert/strict";
import { formatMemoryChangeLine, formatMemoryChanges, formatRegisterLine, formatRegisters, formatSimulatorMessages } from "../../web/src/formatters.js";

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

test("formats a single register row", () => {
  assert.equal(formatRegisterLine("EAX", { hex: "0000002Ah", unsigned: 42 }), "EAX    0000002Ah / 42");
});

test("formats canonical registers in stable order", () => {
  assert.equal(formatRegisters({
    EBX: { hex: "00000003h", unsigned: 3 },
    EAX: { hex: "0000002Ah", unsigned: 42 },
    EFLAGS: { hex: "00000040h", unsigned: 64 }
  }), "EAX    0000002Ah / 42\nEBX    00000003h / 3\nEFLAGS 00000040h / 64");
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

test("formats empty simulator messages", () => {
  assert.equal(formatSimulatorMessages([]), "No simulator messages.");
});

test("formats partial simulator message objects defensively", () => {
  assert.equal(formatSimulatorMessages([{}]), "[message] unknown: ");
});


test("formats one symbol-aware memory change", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "var",
    oldHex: "00h",
    oldUnsigned: 0,
    newHex: "64h",
    newUnsigned: 100
  }), "var: 00h / 0 -> 64h / 100");
});

test("formats memory changes and empty memory changes", () => {
  assert.equal(formatMemoryChanges([{
    symbol: "var",
    oldHex: "00h",
    oldUnsigned: 0,
    newHex: "64h",
    newUnsigned: 100
  }]), "var: 00h / 0 -> 64h / 100");
  assert.equal(formatMemoryChanges([]), "No memory changes.");
});

test("formats offset-zero bracketed symbol changes as direct symbol rows", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "nums",
    dataType: "DWORD",
    byteOffset: 0,
    elementIndex: 0,
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000064h",
    newUnsigned: 100
  }), "nums: 00000000h / 0 -> 00000064h / 100");
});

test("formats symbol-offset memory changes with byte offset and element index", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "nums",
    dataType: "DWORD",
    byteOffset: 8,
    elementIndex: 2,
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000064h",
    newUnsigned: 100
  }), "nums + 8 DWORD\n  byte offset: +8\n  element index: 2\n  00000000h / 0 -> 00000064h / 100");
});

test("formats unaligned symbol-offset memory changes without element index", () => {
  assert.equal(formatMemoryChangeLine({
    symbol: "nums",
    dataType: "DWORD",
    byteOffset: 9,
    oldHex: "00000000h",
    oldUnsigned: 0,
    newHex: "00000064h",
    newUnsigned: 100
  }), "nums + 9 DWORD\n  byte offset: +9\n  00000000h / 0 -> 00000064h / 100");
});
