/*
 * @file formatters.js
 * @brief Pure UI formatting helpers for Milestone 8 browser output.
 *
 * These helpers format final register state and simulator diagnostics returned
 * by the worker. They are isolated from DOM and Worker setup so they can be
 * covered by Node.js tests without browser automation.
 */

/** @typedef {{hex: string, unsigned: number}} RegisterValue */
/** @typedef {Record<string, RegisterValue>} RegisterMap */
/** @typedef {{kind?: string, code?: string, message?: string, line?: number, column?: number}} SimulatorMessage */
/** @typedef {{symbol?: string, oldHex?: string, oldUnsigned?: number, newHex?: string, newUnsigned?: number}} MemoryChange */

/** Canonical MASM32 register display order for the Milestone 8 final-state panel. */
const CANONICAL_REGISTER_DISPLAY_ORDER = ["EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP", "EIP", "EFLAGS"];

/**
 * Formats one register value for the Milestone 8 final-register panel.
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
    const location = message.line ? ` line ${message.line}${message.column ? `, column ${message.column}` : ""}` : "";
    return `[${message.kind || "message"}] ${message.code || "unknown"}${location}: ${message.message || ""}`;
  }).join("\n");
}


/**
 * Formats one symbol-aware memory change returned by the worker.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string} Human-readable memory-change row.
 */
export function formatMemoryChangeLine(change) {
  const symbol = change && change.symbol ? change.symbol : "<unknown>";
  const oldHex = change && change.oldHex ? change.oldHex : "??h";
  const oldUnsigned = change && Number.isFinite(change.oldUnsigned) ? change.oldUnsigned : "?";
  const newHex = change && change.newHex ? change.newHex : "??h";
  const newUnsigned = change && Number.isFinite(change.newUnsigned) ? change.newUnsigned : "?";
  return `${symbol}: ${oldHex} / ${oldUnsigned} -> ${newHex} / ${newUnsigned}`;
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
