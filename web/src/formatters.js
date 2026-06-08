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
/** @typedef {Record<string, boolean>} RegisterWriteMap */
/** @typedef {Record<string, string>} RegisterRoleMap */
/** @typedef {{kind?: string, code?: string, message?: string, line?: number, column?: number, byteOffset?: number, spanLength?: number}} SimulatorMessage */
/** @typedef {{symbol?: string, dataType?: string, widthBits?: number, byteOffset?: number, elementIndex?: number, oldHex?: string, oldUnsigned?: number, newHex?: string, newUnsigned?: number, sourceLine?: number, sourceText?: string}} MemoryChange */

/** Zero-based EFLAGS bit index for the currently modeled carry flag. */
const EFLAGS_CF_BIT = 0;

/** Zero-based EFLAGS bit index for the currently modeled zero flag. */
const EFLAGS_ZF_BIT = 6;

/** Zero-based EFLAGS bit index for the currently modeled sign flag. */
const EFLAGS_SF_BIT = 7;

/** Zero-based EFLAGS bit index for the currently modeled overflow flag. */
const EFLAGS_OF_BIT = 11;

/** Display-only spacer row inserted between Phase 69B adjacent parent-register families. */
const REGISTER_PARENT_FAMILY_SPACER = "       |";

/** Display-only divider row inserted between Phase 69B high-level final-register groups. */
const REGISTER_HIGH_LEVEL_GROUP_DIVIDER = "-------------------------------------------------------------------";

/** Register rows after which Phase 69B may insert a parent-family spacer. */
const REGISTER_PARENT_FAMILY_SPACER_AFTER_ROWS = new Set(["AL", "BL", "CL", "SI", "BP", "EIP"]);

/** Register rows after which Phase 69B may insert a high-level group divider. */
const REGISTER_HIGH_LEVEL_GROUP_DIVIDER_AFTER_ROWS = new Set(["DL", "DI", "SP"]);

/** Canonical MASM32 register display groups and aliases for the final-state panel. */
const REGISTER_DISPLAY_ROWS = [
  { name: "EAX", source: "EAX", widthBits: 32, group: "EAX", displayGroup: "general", indentLevel: 0 },
  { name: "AX", source: "EAX", widthBits: 16, group: "EAX", displayGroup: "general", indentLevel: 1 },
  { name: "AH", source: "EAX", widthBits: 8, shiftBits: 8, group: "EAX", displayGroup: "general", indentLevel: 2, hexPlacement: "high-byte" },
  { name: "AL", source: "EAX", widthBits: 8, group: "EAX", displayGroup: "general", indentLevel: 2 },
  { name: "EBX", source: "EBX", widthBits: 32, group: "EBX", displayGroup: "general", indentLevel: 0 },
  { name: "BX", source: "EBX", widthBits: 16, group: "EBX", displayGroup: "general", indentLevel: 1 },
  { name: "BH", source: "EBX", widthBits: 8, shiftBits: 8, group: "EBX", displayGroup: "general", indentLevel: 2, hexPlacement: "high-byte" },
  { name: "BL", source: "EBX", widthBits: 8, group: "EBX", displayGroup: "general", indentLevel: 2 },
  { name: "ECX", source: "ECX", widthBits: 32, group: "ECX", displayGroup: "general", indentLevel: 0 },
  { name: "CX", source: "ECX", widthBits: 16, group: "ECX", displayGroup: "general", indentLevel: 1 },
  { name: "CH", source: "ECX", widthBits: 8, shiftBits: 8, group: "ECX", displayGroup: "general", indentLevel: 2, hexPlacement: "high-byte" },
  { name: "CL", source: "ECX", widthBits: 8, group: "ECX", displayGroup: "general", indentLevel: 2 },
  { name: "EDX", source: "EDX", widthBits: 32, group: "EDX", displayGroup: "general", indentLevel: 0 },
  { name: "DX", source: "EDX", widthBits: 16, group: "EDX", displayGroup: "general", indentLevel: 1 },
  { name: "DH", source: "EDX", widthBits: 8, shiftBits: 8, group: "EDX", displayGroup: "general", indentLevel: 2, hexPlacement: "high-byte" },
  { name: "DL", source: "EDX", widthBits: 8, group: "EDX", displayGroup: "general", indentLevel: 2 },
  { name: "ESI", source: "ESI", widthBits: 32, group: "ESI", displayGroup: "index", indentLevel: 0 },
  { name: "SI", source: "ESI", widthBits: 16, group: "ESI", displayGroup: "index", indentLevel: 1 },
  { name: "EDI", source: "EDI", widthBits: 32, group: "EDI", displayGroup: "index", indentLevel: 0 },
  { name: "DI", source: "EDI", widthBits: 16, group: "EDI", displayGroup: "index", indentLevel: 1 },
  { name: "EBP", source: "EBP", widthBits: 32, group: "EBP", displayGroup: "stack", indentLevel: 0 },
  { name: "BP", source: "EBP", widthBits: 16, group: "EBP", displayGroup: "stack", indentLevel: 1 },
  { name: "ESP", source: "ESP", widthBits: 32, group: "ESP", displayGroup: "stack", indentLevel: 0 },
  { name: "SP", source: "ESP", widthBits: 16, group: "ESP", displayGroup: "stack", indentLevel: 1 },
  { name: "EIP", source: "EIP", widthBits: 32, group: "EIP", displayGroup: "control", indentLevel: 0 },
  { name: "EFLAGS", source: "EFLAGS", widthBits: 32, group: "EFLAGS", displayGroup: "control", indentLevel: 0, signedDisplay: false },
  { name: "CF", source: "EFLAGS", group: "EFLAGS", displayGroup: "control", indentLevel: 1, flagBit: EFLAGS_CF_BIT },
  { name: "ZF", source: "EFLAGS", group: "EFLAGS", displayGroup: "control", indentLevel: 1, flagBit: EFLAGS_ZF_BIT },
  { name: "SF", source: "EFLAGS", group: "EFLAGS", displayGroup: "control", indentLevel: 1, flagBit: EFLAGS_SF_BIT },
  { name: "OF", source: "EFLAGS", group: "EFLAGS", displayGroup: "control", indentLevel: 1, flagBit: EFLAGS_OF_BIT }
];

/** Number of leading spaces added for one register composition level. */
const REGISTER_ALIAS_INDENT_SPACES = 2;

/** Width of the final-register name column, including composition indentation. */
const REGISTER_NAME_COLUMN_WIDTH = 7;

/** Width of the hexadecimal value column in aligned integer display rows. */
const ALIGNED_HEX_COLUMN_WIDTH = 9;

/** Hexadecimal placement hint for AH/BH/CH/DH display rows. */
const HEX_PLACEMENT_HIGH_BYTE = "high-byte";

/** Width of the unsigned decimal column in aligned integer display rows. */
const ALIGNED_UNSIGNED_COLUMN_WIDTH = 10;

/** Width of the signed decimal column in aligned integer display rows. */
const ALIGNED_SIGNED_COLUMN_WIDTH = 11;

/** Compact marker appended to canonical parent rows for untouched register families. */
const REGISTER_UNCHANGED_MARKER = "[unchanged]";

/** Compact marker appended to the displayed EIP pseudo-control-state row. */
const REGISTER_DERIVED_CONTROL_STATE_MARKER = "[derived control state]";

/** Extra spacing before the unchanged marker so wide signed values stay readable. */
const REGISTER_UNCHANGED_MARKER_SPACING = "     ";

/** Width of the aligned value column before optional register display markers. */
const ALIGNED_REGISTER_VALUE_COLUMN_WIDTH =
  ALIGNED_HEX_COLUMN_WIDTH + 6 + ALIGNED_UNSIGNED_COLUMN_WIDTH + 6 + ALIGNED_SIGNED_COLUMN_WIDTH;


/** Valid display widths for Phase 52A signed integer formatting. */
const SUPPORTED_SIGNED_DISPLAY_WIDTHS = new Set([8, 16, 32]);

/** Stable diagnostic codes that belong to the Phase 64B runtime diagnostic group. */
const RUNTIME_DIAGNOSTIC_CODES = new Set([
  "divide-by-zero",
  "invalid-address",
  "invalid-branch-target",
  "object-bounds-violation",
  "object-bounds-warning",
  "permission-denied",
  "quotient-overflow",
  "region-boundary-crossing",
  "section-capacity-violation",
  "section-image-violation",
  "undefined-flag-use",
  "undefined-modeled-flag",
  "undefined-shift-flag",
  "unaligned-memory-access",
  "uninitialized-read",
  "unsupported-code-memory-access"
]);

/** Logical rendered Simulator Messages group names finalized by Phase 69B. */
const SIMULATOR_MESSAGE_GROUPS = {
  STARTUP: "startup",
  PRE_EXECUTION: "pre-execution",
  RUNTIME: "runtime",
  FINAL: "final"
};

/**
 * Returns a power-of-two modulus for one supported integer display width.
 *
 * @param {number} widthBits Display width in bits.
 * @returns {number} Modulus for the selected width.
 */
function modulusForWidth(widthBits) {
  return 2 ** widthBits;
}

/**
 * Returns the number of hexadecimal digits used for one display width.
 *
 * @param {number} widthBits Display width in bits.
 * @returns {number} Hexadecimal digit count.
 */
function hexDigitsForWidth(widthBits) {
  return widthBits / 4;
}

/**
 * Returns whether a width can use Phase 52A signed integer display.
 *
 * @param {number} widthBits Display width in bits.
 * @returns {boolean} true when the width is supported.
 */
function isSupportedSignedDisplayWidth(widthBits) {
  return Number.isInteger(widthBits) && SUPPORTED_SIGNED_DISPLAY_WIDTHS.has(widthBits);
}

/**
 * Normalizes a numeric value to the selected unsigned display width.
 *
 * @param {number} value Unsigned integer value to normalize.
 * @param {number} widthBits Display width in bits.
 * @returns {number | null} Normalized unsigned value, or null when invalid.
 */
function normalizeUnsignedForWidth(value, widthBits) {
  if (!isSupportedSignedDisplayWidth(widthBits) || !Number.isFinite(value)) {
    return null;
  }

  const modulus = modulusForWidth(widthBits);
  const truncated = Math.trunc(value);
  return ((truncated % modulus) + modulus) % modulus;
}

/**
 * Parses a MASM-style hexadecimal display string such as `000000FFh`.
 *
 * @param {string | undefined} hex Hexadecimal display text.
 * @returns {number | null} Parsed unsigned value, or null when invalid.
 */
function parseHexDisplay(hex) {
  if (typeof hex !== "string") {
    return null;
  }

  const match = /^([0-9A-Fa-f]+)h$/.exec(hex.trim());
  if (!match) {
    return null;
  }

  const value = Number.parseInt(match[1], 16);
  return Number.isFinite(value) ? value : null;
}

/**
 * Formats a normalized unsigned value as zero-padded MASM-style hexadecimal.
 *
 * @param {number} unsigned Normalized unsigned value.
 * @param {number} widthBits Display width in bits.
 * @returns {string} MASM-style hexadecimal display text.
 */
function formatHexForWidth(unsigned, widthBits) {
  return `${unsigned.toString(16).toUpperCase().padStart(hexDigitsForWidth(widthBits), "0")}h`;
}

/**
 * Converts a normalized unsigned integer to its signed interpretation.
 *
 * @param {number} unsigned Normalized unsigned value.
 * @param {number} widthBits Display width in bits.
 * @returns {number} Signed interpretation for the selected width.
 */
function signedValueForWidth(unsigned, widthBits) {
  const signBoundary = 2 ** (widthBits - 1);
  const modulus = modulusForWidth(widthBits);
  return unsigned >= signBoundary ? unsigned - modulus : unsigned;
}

/**
 * Formats one known-width integer value with hex, unsigned, and signed decimal.
 *
 * @param {{hex?: string, unsigned?: number}} value Integer value object.
 * @param {number} widthBits Display width in bits.
 * @returns {string | null} Formatted value, or null when width/value is unavailable.
 */
export function formatIntegerDisplay(value, widthBits) {
  if (!isSupportedSignedDisplayWidth(widthBits) || value === undefined || value === null) {
    return null;
  }

  const sourceUnsigned = Number.isFinite(value.unsigned) ? value.unsigned : parseHexDisplay(value.hex);
  const unsigned = normalizeUnsignedForWidth(sourceUnsigned, widthBits);
  if (unsigned === null) {
    return null;
  }

  const hex = formatHexForWidth(unsigned, widthBits);
  const signed = signedValueForWidth(unsigned, widthBits);
  return `${hex} / u:${unsigned} / s:${signed}`;
}

/**
 * Formats a MASM-style hexadecimal value inside the aligned hex column.
 *
 * @param {number} unsigned Normalized unsigned value.
 * @param {number} widthBits Display width in bits.
 * @param {string | undefined} [hexPlacement] Optional placement hint.
 * @returns {string} Aligned hexadecimal display text.
 */
function formatAlignedHexDisplay(unsigned, widthBits, hexPlacement) {
  const hex = formatHexForWidth(unsigned, widthBits);
  if (hexPlacement === HEX_PLACEMENT_HIGH_BYTE && widthBits === 8) {
    return hex
      .padStart(ALIGNED_HEX_COLUMN_WIDTH - 2, " ")
      .padEnd(ALIGNED_HEX_COLUMN_WIDTH, " ");
  }

  return hex.padStart(ALIGNED_HEX_COLUMN_WIDTH, " ");
}

/**
 * Formats one known-width integer value for aligned UI display.
 *
 * @param {{hex?: string, unsigned?: number}} value Integer value object.
 * @param {number} widthBits Display width in bits.
 * @param {string | undefined} [hexPlacement] Optional placement hint.
 * @returns {string | null} Aligned integer value, or null when width/value is unavailable.
 */
function formatAlignedIntegerDisplay(value, widthBits, hexPlacement) {
  if (!isSupportedSignedDisplayWidth(widthBits) || value === undefined || value === null) {
    return null;
  }

  const sourceUnsigned = Number.isFinite(value.unsigned) ? value.unsigned : parseHexDisplay(value.hex);
  const unsigned = normalizeUnsignedForWidth(sourceUnsigned, widthBits);
  if (unsigned === null) {
    return null;
  }

  const hex = formatAlignedHexDisplay(unsigned, widthBits, hexPlacement);
  const signed = signedValueForWidth(unsigned, widthBits);
  const unsignedColumn = `${unsigned}`.padEnd(ALIGNED_UNSIGNED_COLUMN_WIDTH, " ");
  const signedColumn = `${signed < 0 ? signed : ` ${signed}`}`.padEnd(ALIGNED_SIGNED_COLUMN_WIDTH, " ");
  return `${hex} / u: ${unsignedColumn} / s: ${signedColumn}`;
}

/**
 * Formats one integer value using the legacy hex/unsigned display shape.
 *
 * @param {string | undefined} hex Hexadecimal display text.
 * @param {number | undefined} unsigned Unsigned decimal value.
 * @returns {string} Legacy value display text.
 */
function formatLegacyUnsignedDisplay(hex, unsigned) {
  const safeHex = hex || "??h";
  const safeUnsigned = Number.isFinite(unsigned) ? unsigned : "?";
  return `${safeHex} / ${safeUnsigned}`;
}

/**
 * Formats one legacy hex/unsigned value inside the aligned register value column.
 *
 * This preserves the EFLAGS hex/unsigned-only display shape while keeping
 * optional parent-row markers aligned with regular signed register rows.
 *
 * @param {string | undefined} hex Hexadecimal display text.
 * @param {number | undefined} unsigned Unsigned decimal value.
 * @returns {string} Legacy value display padded to the register value width.
 */
function formatAlignedLegacyUnsignedDisplay(hex, unsigned) {
  return formatLegacyUnsignedDisplay(hex, unsigned).padEnd(ALIGNED_REGISTER_VALUE_COLUMN_WIDTH, " ");
}

/**
 * Formats one value object using signed display when the width is known.
 *
 * @param {{hex?: string, unsigned?: number}} value Integer value object.
 * @param {number | undefined} widthBits Display width in bits.
 * @returns {string} Human-readable value display.
 */
function formatIntegerDisplayOrFallback(value, widthBits) {
  const signedDisplay = formatIntegerDisplay(value, widthBits);
  if (signedDisplay !== null) {
    return signedDisplay;
  }

  return formatLegacyUnsignedDisplay(value && value.hex, value && value.unsigned);
}

/**
 * Extracts a register-alias value from a canonical 32-bit register payload.
 *
 * @param {RegisterValue} sourceValue Canonical 32-bit register value.
 * @param {number} widthBits Alias display width in bits.
 * @param {number} [shiftBits] Right shift used for high-byte aliases.
 * @returns {RegisterValue | null} Alias register value, or null when unavailable.
 */
function deriveRegisterAliasValue(sourceValue, widthBits, shiftBits = 0) {
  const sourceUnsigned = Number.isFinite(sourceValue && sourceValue.unsigned)
    ? sourceValue.unsigned
    : parseHexDisplay(sourceValue && sourceValue.hex);
  if (!Number.isFinite(sourceUnsigned)) {
    return null;
  }

  const shifted = normalizeUnsignedForWidth(Math.floor(sourceUnsigned / (2 ** shiftBits)), 32);
  if (shifted === null) {
    return null;
  }

  const aliasUnsigned = normalizeUnsignedForWidth(shifted, widthBits);
  if (aliasUnsigned === null) {
    return null;
  }

  return {
    hex: formatHexForWidth(aliasUnsigned, widthBits),
    unsigned: aliasUnsigned
  };
}


/**
 * Derives one modeled flag bit value from the canonical EFLAGS payload.
 *
 * Phase 64C displays only currently modeled flag bit values. It deliberately
 * avoids flag-validity annotations and does not expose unmodeled x86 flags.
 *
 * @param {RegisterValue} eflagsValue Canonical EFLAGS register value.
 * @param {number} flagBit Zero-based EFLAGS bit index.
 * @returns {number | null} Modeled flag bit as 0 or 1, or null when unavailable.
 */
function deriveModeledFlagBitValue(eflagsValue, flagBit) {
  const sourceUnsigned = Number.isFinite(eflagsValue && eflagsValue.unsigned)
    ? eflagsValue.unsigned
    : parseHexDisplay(eflagsValue && eflagsValue.hex);
  if (!Number.isFinite(sourceUnsigned) || !Number.isInteger(flagBit) || flagBit < 0 || flagBit > 31) {
    return null;
  }

  const normalized = normalizeUnsignedForWidth(sourceUnsigned, 32);
  if (normalized === null) {
    return null;
  }

  return Math.floor(normalized / (2 ** flagBit)) % 2;
}

/**
 * Formats one modeled flag child row for the final-register panel.
 *
 * @param {string} name Modeled flag display name.
 * @param {number} value Modeled flag bit value, either 0 or 1.
 * @returns {string} Human-readable modeled-flag row.
 */
function formatModeledFlagLine(name, value) {
  return `${name.padEnd(REGISTER_NAME_COLUMN_WIDTH, " ")}| ${value ? 1 : 0}`;
}

/**
 * Formats one register value for the final-register panel.
 *
 * @param {string} name Register display name.
 * @param {RegisterValue} value Register value object.
 * @param {number} [widthBits] Optional display width in bits.
 * @param {boolean} [signedDisplay] Whether signed decimal display is enabled.
 * @param {string | undefined} [hexPlacement] Optional hexadecimal placement hint.
 * @returns {string} Human-readable register row.
 */
export function formatRegisterLine(name, value, widthBits, signedDisplay = true, hexPlacement) {
  const display = signedDisplay ? formatAlignedIntegerDisplay(value, widthBits, hexPlacement) : null;
  const fallback = Number.isInteger(widthBits)
    ? formatAlignedLegacyUnsignedDisplay(value && value.hex, value && value.unsigned)
    : formatLegacyUnsignedDisplay(value && value.hex, value && value.unsigned);
  return `${name.padEnd(REGISTER_NAME_COLUMN_WIDTH, " ")}| ${display || fallback}`;
}

/**
 * Returns the logical rendered group for one Simulator Message.
 *
 * Phase 69B keeps startup notices first when execution begins, then groups
 * nonfatal pre-execution diagnostics, runtime diagnostics, and final success
 * status without representing blank separator lines in source-run JSON.
 *
 * @param {SimulatorMessage | undefined} message Message to classify.
 * @returns {string} One value from SIMULATOR_MESSAGE_GROUPS.
 */
function simulatorMessageRenderedGroup(message) {
  if (!message) {
    return SIMULATOR_MESSAGE_GROUPS.PRE_EXECUTION;
  }

  if (message.code === "startup-state-notice") {
    return SIMULATOR_MESSAGE_GROUPS.STARTUP;
  }

  if (message.code === "execution-complete") {
    return SIMULATOR_MESSAGE_GROUPS.FINAL;
  }

  if (message.kind === "runtime-error" || RUNTIME_DIAGNOSTIC_CODES.has(message.code)) {
    return SIMULATOR_MESSAGE_GROUPS.RUNTIME;
  }

  return SIMULATOR_MESSAGE_GROUPS.PRE_EXECUTION;
}

/**
 * Returns whether Phase 69B requires a blank separator between adjacent messages.
 *
 * Separators are rendering-only group boundaries. They are never represented as
 * source-run JSON diagnostics and are not inserted around adjacent messages that
 * belong to the same rendered group.
 *
 * @param {string} previousGroup Logical group for the preceding message.
 * @param {string} currentGroup Logical group for the current message.
 * @returns {boolean} true when a blank rendered line should be inserted.
 */
function shouldSeparateSimulatorMessageGroups(previousGroup, currentGroup) {
  return previousGroup !== currentGroup;
}

/**
 * Returns whether one canonical register row should show the unchanged marker.
 *
 * The formatter consumes explicit write-tracking metadata. It deliberately does
 * not infer unchanged status by comparing final values with startup values,
 * because same-value writes such as `mov eax, 0` must count as changed.
 *
 * @param {{name: string, source: string, indentLevel?: number}} row Register row descriptor.
 * @param {RegisterWriteMap | undefined} registerWrites Register-family write metadata.
 * @returns {boolean} true when the parent row should show `[unchanged]`.
 */
function shouldShowRegisterUnchangedMarker(row, registerWrites) {
  if (!registerWrites || row.name !== row.source || (row.indentLevel || 0) !== 0) {
    return false;
  }

  return Object.prototype.hasOwnProperty.call(registerWrites, row.source) && registerWrites[row.source] === false;
}

/**
 * Returns the role marker for one canonical register row when protocol metadata
 * classifies that row as displayed VM control state rather than source-writable
 * register state.
 *
 * @param {{name: string, source: string, indentLevel?: number}} row Register row descriptor.
 * @param {RegisterRoleMap | undefined} registerRoles Register-role metadata.
 * @returns {string | null} Marker text, or null when no role marker applies.
 */
function registerRoleMarker(row, registerRoles) {
  if (!registerRoles || row.name !== row.source || (row.indentLevel || 0) !== 0) {
    return null;
  }

  return registerRoles[row.source] === "derived-control-state"
    ? REGISTER_DERIVED_CONTROL_STATE_MARKER
    : null;
}

/**
 * Formats a register name with composition indentation for alias rows.
 *
 * @param {string} name Register display name.
 * @param {number} [indentLevel] Register composition indentation level.
 * @returns {string} Indented register name for the final-register panel.
 */
function formatRegisterDisplayName(name, indentLevel = 0) {
  return `${" ".repeat(Math.max(0, indentLevel) * REGISTER_ALIAS_INDENT_SPACES)}${name}`;
}

/**
 * Formats final registers returned by the worker, including supported aliases.
 *
 * @param {RegisterMap | undefined} registers Register map.
 * @param {RegisterWriteMap | undefined} registerWrites Register-family write metadata.
 * @param {RegisterRoleMap | undefined} registerRoles Register display-role metadata.
 * @returns {string} Human-readable register table.
 */
/**
 * Returns a rendered final-register row, or null when the row is unavailable.
 *
 * @param {object} row Register display-row descriptor.
 * @param {RegisterMap} registers Register map returned by the worker.
 * @param {RegisterWriteMap | undefined} registerWrites Register-family write metadata.
 * @param {RegisterRoleMap | undefined} registerRoles Register display-role metadata.
 * @returns {{row: object, line: string} | null} Rendered row data, or null when unavailable.
 */
function renderRegisterDisplayRow(row, registers, registerWrites, registerRoles) {
  if (!Object.prototype.hasOwnProperty.call(registers, row.source)) {
    return null;
  }

  const sourceValue = registers[row.source];
  const displayName = formatRegisterDisplayName(row.name, row.indentLevel || 0);
  let line = null;

  if (Number.isInteger(row.flagBit)) {
    const flagValue = deriveModeledFlagBitValue(sourceValue, row.flagBit);
    if (flagValue === null) {
      return null;
    }
    line = formatModeledFlagLine(displayName, flagValue);
  } else {
    const value = row.name === row.source
      ? sourceValue
      : deriveRegisterAliasValue(sourceValue, row.widthBits, row.shiftBits || 0);
    if (value === null) {
      return null;
    }
    line = formatRegisterLine(displayName, value, row.widthBits, row.signedDisplay !== false, row.hexPlacement);
  }

  const roleMarker = registerRoleMarker(row, registerRoles);
  if (roleMarker) {
    line = `${line}${REGISTER_UNCHANGED_MARKER_SPACING}${roleMarker}`;
  } else if (shouldShowRegisterUnchangedMarker(row, registerWrites)) {
    line = `${line}${REGISTER_UNCHANGED_MARKER_SPACING}${REGISTER_UNCHANGED_MARKER}`;
  }

  return { row, line };
}

/**
 * Returns the Phase 69B display-only separator after one rendered register row.
 *
 * @param {object} row Current register display-row descriptor.
 * @param {object | undefined} nextRow Next rendered register display-row descriptor.
 * @returns {string | null} Separator row text, or null when none is required.
 */
function registerSeparatorAfterRow(row, nextRow) {
  if (!nextRow) {
    return null;
  }

  if (REGISTER_HIGH_LEVEL_GROUP_DIVIDER_AFTER_ROWS.has(row.name) && row.displayGroup !== nextRow.displayGroup) {
    return REGISTER_HIGH_LEVEL_GROUP_DIVIDER;
  }

  if (
    REGISTER_PARENT_FAMILY_SPACER_AFTER_ROWS.has(row.name)
    && row.displayGroup === nextRow.displayGroup
    && row.group !== nextRow.group
  ) {
    return REGISTER_PARENT_FAMILY_SPACER;
  }

  return null;
}

export function formatRegisters(registers, registerWrites, registerRoles) {
  if (!registers) {
    return "No register state available.";
  }

  const renderedRows = REGISTER_DISPLAY_ROWS
    .map((row) => renderRegisterDisplayRow(row, registers, registerWrites, registerRoles))
    .filter((row) => row !== null);
  const lines = [];

  renderedRows.forEach((renderedRow, index) => {
    lines.push(renderedRow.line);

    const nextRow = renderedRows[index + 1]?.row;
    const separator = registerSeparatorAfterRow(renderedRow.row, nextRow);
    if (separator) {
      lines.push(separator);
    }
  });

  return lines.join("\n");
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

  const lines = [];
  let previousGroup = null;

  messages.forEach((message) => {
    const safeMessage = message || {};
    const group = simulatorMessageRenderedGroup(safeMessage);
    const sourceSpan = Number.isFinite(safeMessage.byteOffset)
      ? `, byte offset ${safeMessage.byteOffset}${Number.isFinite(safeMessage.spanLength) ? `, span length ${safeMessage.spanLength}` : ""}`
      : "";
    const location = safeMessage.line ? ` line ${safeMessage.line}${safeMessage.column ? `, column ${safeMessage.column}` : ""}${sourceSpan}` : "";
    if (previousGroup !== null && shouldSeparateSimulatorMessageGroups(previousGroup, group)) {
      lines.push("");
    }
    lines.push(`[${safeMessage.kind || "message"}] ${safeMessage.code || "unknown"}${location}: ${safeMessage.message || ""}`);
    previousGroup = group;
  });

  return lines.join("\n");
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
  const attribution = formatMemoryChangeSourceAttribution(change);
  const suffix = attribution ? ` | ${attribution}` : "";

  if (hasSymbolOffset(change)) {
    return `${symbol} + ${change.byteOffset}${dataType}${suffix}`;
  }

  return `${symbol}${dataType}${suffix}`;
}

/**
 * Formats the Phase 64D source attribution suffix for a memory-change row.
 *
 * Source text, when present, is the original parser-preserved instruction line;
 * this formatter does not reconstruct source from opcode or operand metadata.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string | null} Source attribution text, or null when unavailable.
 */
function formatMemoryChangeSourceAttribution(change) {
  if (!change || !Number.isInteger(change.sourceLine) || change.sourceLine <= 0) {
    return null;
  }

  if (typeof change.sourceText === "string" && change.sourceText.length > 0) {
    return `line ${change.sourceLine}: ${change.sourceText}`;
  }

  return `line ${change.sourceLine}`;
}

/**
 * Formats one memory value for the aligned before/after memory-change rows.
 *
 * @param {{hex?: string, unsigned?: number}} value Integer value object.
 * @param {number | undefined} widthBits Display width in bits.
 * @returns {string} Human-readable value display.
 */
function formatAlignedMemoryValue(value, widthBits) {
  const signedDisplay = formatAlignedIntegerDisplay(value, widthBits);
  if (signedDisplay !== null) {
    return signedDisplay;
  }

  return formatLegacyUnsignedDisplay(value && value.hex, value && value.unsigned);
}

/**
 * Formats the old and new scalar value rows from one memory change.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string[]} Human-readable old/new rows.
 */
function formatMemoryChangeValueRows(change) {
  const oldValue = {
    hex: change && change.oldHex,
    unsigned: change && change.oldUnsigned
  };
  const newValue = {
    hex: change && change.newHex,
    unsigned: change && change.newUnsigned
  };
  const oldDisplay = formatAlignedMemoryValue(oldValue, change && change.widthBits);
  const newDisplay = formatAlignedMemoryValue(newValue, change && change.widthBits);
  return [`  old | ${oldDisplay}`, `  new | ${newDisplay}`];
}

/**
 * Formats optional metadata for one memory-change block.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string | null} Metadata row, or null when there is no extra metadata.
 */
function formatMemoryChangeInfoRow(change) {
  if (!hasSymbolOffset(change)) {
    return null;
  }

  const parts = [`byte offset +${change.byteOffset}`];
  if (Number.isInteger(change.elementIndex)) {
    parts.push(`element index ${change.elementIndex}`);
  }
  return `  info| ${parts.join(", ")}`;
}

/**
 * Formats one symbol-aware memory change returned by the worker.
 *
 * Each memory change uses a source-attributed block with aligned old/new value rows.
 * Symbol-offset writes include an extra info row when the core reports byte
 * offset or element-index metadata.
 *
 * @param {MemoryChange} change Memory change object.
 * @returns {string} Human-readable memory-change block.
 */
export function formatMemoryChangeLine(change) {
  const label = formatMemoryChangeLabel(change);
  const lines = [label, ...formatMemoryChangeValueRows(change)];
  const infoRow = formatMemoryChangeInfoRow(change);
  if (infoRow !== null) {
    lines.push(infoRow);
  }
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

  return changes.map((change) => formatMemoryChangeLine(change || {})).join("\n\n");
}
