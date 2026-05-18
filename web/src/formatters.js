/*
 * @file formatters.js
 * @brief Pure UI formatting helpers for browser output and diagnostic tests.
 *
 * These helpers format final register state, simulator diagnostics, and
 * symbol-aware memory changes returned by the worker. They are isolated from
 * DOM and Worker setup so they can be covered by Node.js tests without browser
 * automation.
 */

/** @typedef {{hex: string, unsigned: number}} RegisterValue */
/** @typedef {Record<string, RegisterValue>} RegisterMap */
/** @typedef {{kind?: string, code?: string, message?: string, line?: number, column?: number, byteOffset?: number, spanLength?: number}} SimulatorMessage */
/** @typedef {{symbol?: string, dataType?: string, byteOffset?: number, elementIndex?: number, oldHex?: string, oldUnsigned?: number, newHex?: string, newUnsigned?: number}} MemoryChange */

/** Canonical MASM32 register display order for the final-state panel. */
const CANONICAL_REGISTER_DISPLAY_ORDER = ["EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP", "EIP", "EFLAGS"];

/**
 * Formats one register value for the final-register panel.
 *
 * @param {string} name Register display name.
 * @param {RegisterValue} value Register value object.
 * @returns {string} Human-readable register row.
 */
export function formatRegisterLine(name, value) {
  return `${name.padEnd(6, " ")} ${value.hex} / ${value.unsigned}`;
}

/**
 * Formats final canonical registers returned by the worker.
 *
 * @param {RegisterMap | undefined} registers Register map.
 * @returns {string} Human-readable register table.
 */
export function formatRegisters(registers) {
  if (!registers) {
    return "No register state available.";
  }

  return CANONICAL_REGISTER_DISPLAY_ORDER
    .filter((name) => Object.prototype.hasOwnProperty.call(registers, name))
    .map((name) => formatRegisterLine(name, registers[name]))
    .join("\n");
}

/**
 * Formats simulator messages returned by a RUN_RESULT response.
 *
 * @param {SimulatorMessage[] | undefined} messages Messages to render.
 * @returns {string} Human-readable simulator message block.
 */
export function formatSimulatorMessages(messages) {
  if (!messages || messages.length === 0) {
    return "No simulator messages.";
  }

  return messages.map((message) => {
    const sourceSpan = Number.isFinite(message.byteOffset)
      ? `, byte offset ${message.byteOffset}${Number.isFinite(message.spanLength) ? `, span length ${message.spanLength}` : ""}`
      : "";
    const location = message.line ? ` line ${message.line}${message.column ? `, column ${message.column}` : ""}${sourceSpan}` : "";
    return `[${message.kind || "message"}] ${message.code || "unknown"}${location}: ${message.message || ""}`;
  }).join("\n");
}

/**
 * Returns whether a memory-change object includes a nonzero byte offset.
 *
 * @param {MemoryChange} change Memory change to inspect.
 * @returns {boolean} true when the change should use the expanded offset view.
 */
function hasSymbolOffset(change) {
  return change && Number.isFinite(change.byteOffset) && change.byteOffset > 0;
}

/**
 * Formats a memory-change symbol label with MASM byte-offset notation.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string} Symbol label for display.
 */
function formatMemoryChangeLabel(change) {
  const symbol = change && change.symbol ? change.symbol : "<unknown>";
  const dataType = change && change.dataType ? ` ${change.dataType}` : "";

  if (hasSymbolOffset(change)) {
    return `${symbol} + ${change.byteOffset}${dataType}`;
  }

  return symbol;
}

/**
 * Formats old/new scalar values from one memory change.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string} Value transition string.
 */
function formatMemoryChangeValueTransition(change) {
  const oldHex = change && change.oldHex ? change.oldHex : "??h";
  const oldUnsigned = change && Number.isFinite(change.oldUnsigned) ? change.oldUnsigned : "?";
  const newHex = change && change.newHex ? change.newHex : "??h";
  const newUnsigned = change && Number.isFinite(change.newUnsigned) ? change.newUnsigned : "?";
  return `${oldHex} / ${oldUnsigned} -> ${newHex} / ${newUnsigned}`;
}

/**
 * Formats one symbol-aware memory change returned by the worker.
 *
 * Direct symbol writes keep the compact symbol row. Symbol-offset writes
 * use a small multi-line display that exposes MASM byte-offset semantics and
 * an aligned element index when the core reports one.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string} Human-readable memory-change row.
 */
export function formatMemoryChangeLine(change) {
  const label = formatMemoryChangeLabel(change);
  const transition = formatMemoryChangeValueTransition(change);

  if (!hasSymbolOffset(change)) {
    return `${label}: ${transition}`;
  }

  const lines = [label, `  byte offset: +${change.byteOffset}`];
  if (Number.isInteger(change.elementIndex)) {
    lines.push(`  element index: ${change.elementIndex}`);
  }
  lines.push(`  ${transition}`);
  return lines.join("\n");
}

/**
 * Formats symbol-aware memory changes returned by a RUN_RESULT response.
 *
 * @param {MemoryChange[] | undefined} changes Memory changes to render.
 * @returns {string} Human-readable memory-change block.
 */
export function formatMemoryChanges(changes) {
  if (!changes || changes.length === 0) {
    return "No memory changes.";
  }

  return changes.map((change) => formatMemoryChangeLine(change || {})).join("\n");
}
