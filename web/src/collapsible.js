/*
 * @file collapsible.js
 * @brief Accessible collapsible-panel helpers for the MASM32 simulator UI.
 *
 * This module owns small DOM utilities for sections whose contents can be
 * hidden without changing their underlying form-control state.
 */

/**
 * Sets the expanded state for an accessible collapsible panel.
 *
 * @param {HTMLElement} toggle Button or control that exposes aria-expanded.
 * @param {HTMLElement} body Panel body controlled by the toggle.
 * @param {boolean} expanded Whether the panel body should be visible.
 * @returns {void}
 */
export function setCollapsiblePanelExpanded(toggle, body, expanded) {
  toggle.setAttribute("aria-expanded", expanded ? "true" : "false");
  body.hidden = !expanded;

  const expandedLabel = toggle.getAttribute("data-expanded-label") || "Hide";
  const collapsedLabel = toggle.getAttribute("data-collapsed-label") || "Show";
  const stateLabelId = toggle.getAttribute("data-state-label-id");
  if (stateLabelId && toggle.ownerDocument) {
    const stateLabel = toggle.ownerDocument.getElementById(stateLabelId);
    if (stateLabel) {
      stateLabel.textContent = expanded ? expandedLabel : collapsedLabel;
    }
  }
}

/**
 * Initializes a collapsible panel and wires click handling for the toggle.
 *
 * @param {HTMLElement} toggle Button or control that expands/collapses the panel.
 * @param {HTMLElement} body Panel body controlled by the toggle.
 * @param {boolean} [initiallyExpanded=false] Initial expanded state.
 * @returns {void}
 */
export function initializeCollapsiblePanel(toggle, body, initiallyExpanded = false) {
  setCollapsiblePanelExpanded(toggle, body, initiallyExpanded);
  toggle.addEventListener("click", () => {
    const isExpanded = toggle.getAttribute("aria-expanded") === "true";
    setCollapsiblePanelExpanded(toggle, body, !isExpanded);
  });
}
