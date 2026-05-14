/*
 * @file main.js
 * @brief Browser UI wiring for Milestone 11 source execution.
 *
 * This module keeps parsing and VM work in the Web Worker. The main thread only
 * sends editor source, renders final register state, and displays structured
 * simulator messages returned by the worker.
 */

import { formatMemoryChanges, formatRegisters, formatSimulatorMessages } from "./formatters.js";

/**
 * Appends a line to a preformatted UI panel.
 *
 * @param {HTMLElement} element Destination panel.
 * @param {string} line Text to append.
 * @returns {void}
 */
function appendLine(element, line) {
  element.textContent += `${line}\n`;
}

/**
 * Replaces a preformatted UI panel with a single text block.
 *
 * @param {HTMLElement} element Destination panel.
 * @param {string} text Text to display.
 * @returns {void}
 */
function setPanelText(element, text) {
  element.textContent = text;
}

/**
 * Formats a structured worker message for display.
 *
 * @param {unknown} message Worker message payload.
 * @returns {string} JSON representation safe for display.
 */
function formatWorkerMessage(message) {
  return JSON.stringify(message, null, 2);
}

/**
 * Renders a source-run result from the worker.
 *
 * @param {Record<string, unknown>} payload RUN_RESULT payload.
 * @param {HTMLElement} simulatorMessages Simulator Messages panel.
 * @param {HTMLElement} finalRegisters Final Registers panel.
 * @param {HTMLElement} memoryChanges Memory Changes panel.
 * @returns {void}
 */
function renderRunResult(payload, simulatorMessages, finalRegisters, memoryChanges) {
  const messages = Array.isArray(payload.simulatorMessages) ? payload.simulatorMessages : [];
  const registers = payload.registers && typeof payload.registers === "object" ? payload.registers : undefined;
  const changes = Array.isArray(payload.memoryChanges) ? payload.memoryChanges : [];

  setPanelText(simulatorMessages, formatSimulatorMessages(messages));
  setPanelText(finalRegisters, formatRegisters(registers));
  setPanelText(memoryChanges, formatMemoryChanges(changes));
}

const simulatorMessages = document.getElementById("simulator-messages");
const programConsole = document.getElementById("program-console");
const finalRegisters = document.getElementById("final-registers");
const memoryChanges = document.getElementById("memory-changes");
const editor = document.getElementById("editor");
const pingButton = document.getElementById("ping-button");
const runButton = document.getElementById("run-button");

if (!simulatorMessages || !programConsole || !finalRegisters || !memoryChanges || !editor || !pingButton || !runButton) {
  throw new Error("Milestone 11 UI elements are missing.");
}

const worker = new Worker(new URL("./worker.js", import.meta.url), { type: "module" });

worker.addEventListener("message", (event) => {
  if (event.data && event.data.type === "PONG") {
    appendLine(programConsole, formatWorkerMessage(event.data));
    return;
  }

  if (event.data && event.data.type === "RUN_RESULT") {
    renderRunResult(event.data.payload || {}, simulatorMessages, finalRegisters, memoryChanges);
    return;
  }

  appendLine(simulatorMessages, formatWorkerMessage(event.data));
});

worker.addEventListener("error", (event) => {
  const message = event.message || "Worker failed to load or crashed before reporting a structured error.";
  appendLine(simulatorMessages, `Worker error: ${message}`);
});

pingButton.addEventListener("click", () => {
  worker.postMessage({
    type: "PING",
    payload: {
      source: "ui-button"
    }
  });
});

runButton.addEventListener("click", () => {
  setPanelText(simulatorMessages, "Running source...");
  setPanelText(programConsole, "");
  setPanelText(finalRegisters, "");
  setPanelText(memoryChanges, "");
  worker.postMessage({
    type: "RUN_SOURCE",
    payload: {
      source: editor.value
    }
  });
});

