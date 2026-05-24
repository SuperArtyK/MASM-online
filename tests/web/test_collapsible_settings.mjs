/*
 * @file test_collapsible_settings.mjs
 * @brief Tests for the Phase 53E addendum collapsible Diagnostic settings panel.
 *
 * These tests validate the browser-side collapse helper without requiring a
 * full DOM automation environment.
 */

import assert from "node:assert/strict";
import { initializeCollapsiblePanel, setCollapsiblePanelExpanded } from "../../web/src/collapsible.js";

/**
 * Runs one named collapsible-settings test.
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
 * Creates a minimal fake HTMLElement for collapse-helper tests.
 *
 * @param {string} id Element identifier.
 * @param {Record<string, FakeElement>} elements Element registry.
 * @returns {FakeElement} Fake element object.
 */
function createElement(id, elements) {
  const element = new FakeElement(id, elements);
  elements[id] = element;
  return element;
}

/** Minimal DOM element model needed by the collapse helper. */
class FakeElement {
  /**
   * Creates one fake element.
   *
   * @param {string} id Element identifier.
   * @param {Record<string, FakeElement>} elements Element registry.
   */
  constructor(id, elements) {
    this.id = id;
    this.attributes = new Map();
    this.hidden = false;
    this.listeners = new Map();
    this.textContent = "";
    this.ownerDocument = {
      getElementById: (targetId) => elements[targetId] || null
    };
  }

  /**
   * Stores one attribute value.
   *
   * @param {string} name Attribute name.
   * @param {string} value Attribute value.
   * @returns {void}
   */
  setAttribute(name, value) {
    this.attributes.set(name, value);
  }

  /**
   * Reads one attribute value.
   *
   * @param {string} name Attribute name.
   * @returns {string | null} Stored attribute value or null.
   */
  getAttribute(name) {
    return this.attributes.has(name) ? this.attributes.get(name) : null;
  }

  /**
   * Registers an event listener.
   *
   * @param {string} name Event name.
   * @param {() => void} handler Event handler.
   * @returns {void}
   */
  addEventListener(name, handler) {
    this.listeners.set(name, handler);
  }

  /**
   * Dispatches a registered event.
   *
   * @param {string} name Event name.
   * @returns {void}
   */
  dispatchEvent(name) {
    const handler = this.listeners.get(name);
    if (handler) {
      handler();
    }
  }
}

test("Diagnostic settings collapse helper sets initial collapsed state", () => {
  const elements = {};
  const toggle = createElement("diagnostic-settings-toggle", elements);
  const body = createElement("diagnostic-settings-body", elements);
  const stateLabel = createElement("diagnostic-settings-toggle-state", elements);
  toggle.setAttribute("data-state-label-id", "diagnostic-settings-toggle-state");
  toggle.setAttribute("data-expanded-label", "Hide");
  toggle.setAttribute("data-collapsed-label", "Show");

  initializeCollapsiblePanel(toggle, body, false);

  assert.equal(toggle.getAttribute("aria-expanded"), "false");
  assert.equal(body.hidden, true);
  assert.equal(stateLabel.textContent, "Show");
});

test("Diagnostic settings collapse helper expands and collapses on click", () => {
  const elements = {};
  const toggle = createElement("diagnostic-settings-toggle", elements);
  const body = createElement("diagnostic-settings-body", elements);
  const stateLabel = createElement("diagnostic-settings-toggle-state", elements);
  toggle.setAttribute("data-state-label-id", "diagnostic-settings-toggle-state");
  toggle.setAttribute("data-expanded-label", "Hide");
  toggle.setAttribute("data-collapsed-label", "Show");

  initializeCollapsiblePanel(toggle, body, false);
  toggle.dispatchEvent("click");

  assert.equal(toggle.getAttribute("aria-expanded"), "true");
  assert.equal(body.hidden, false);
  assert.equal(stateLabel.textContent, "Hide");

  toggle.dispatchEvent("click");

  assert.equal(toggle.getAttribute("aria-expanded"), "false");
  assert.equal(body.hidden, true);
  assert.equal(stateLabel.textContent, "Show");
});

test("Diagnostic settings collapse does not reset selected control values", () => {
  const elements = {};
  const toggle = createElement("diagnostic-settings-toggle", elements);
  const body = createElement("diagnostic-settings-body", elements);
  const select = createElement("memory-range-setting", elements);
  select.value = "declared-object-strict";

  initializeCollapsiblePanel(toggle, body, false);
  toggle.dispatchEvent("click");
  toggle.dispatchEvent("click");

  assert.equal(body.hidden, true);
  assert.equal(select.value, "declared-object-strict");
});

test("Diagnostic settings body can be controlled directly for regression coverage", () => {
  const elements = {};
  const toggle = createElement("diagnostic-settings-toggle", elements);
  const body = createElement("diagnostic-settings-body", elements);

  setCollapsiblePanelExpanded(toggle, body, true);
  assert.equal(toggle.getAttribute("aria-expanded"), "true");
  assert.equal(body.hidden, false);

  setCollapsiblePanelExpanded(toggle, body, false);
  assert.equal(toggle.getAttribute("aria-expanded"), "false");
  assert.equal(body.hidden, true);
});
