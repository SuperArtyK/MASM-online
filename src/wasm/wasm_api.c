/*
 * @file wasm_api.c
 * @brief WebAssembly-facing exports for implemented simulator core milestones.
 *
 * This file bridges JavaScript worker requests to the C simulator core. The
 * source execution export parses numeric equates, extended
 * constant expressions, optional `.data`, `.DATA?`, and `.CONST`, initializes
 * simulated memory, runs the currently supported `.code` subset including
 * TYPE, LENGTHOF, SIZEOF, packed character literals, sign/zero-extension
 * instructions, accumulator conversions, exchange/negation/no-op
 * instructions, carry/borrow arithmetic, carry-flag control, TEST,
 * INC/DEC, bitwise logical instructions, shifts, ROL/ROR, the virtual Irvine32
 * `exit` terminator, and recovered unsupported-feature diagnostics, then
 * reports a compact JSON result for the UI.
 */

#include "wasm_api.h"

#include "../core/masm32_sim_api.h"
#include "../core/vm_cpu.h"
#include "../core/vm_exec.h"
#include "../core/vm_layout.h"
#include "../core/vm_memory.h"
#include "../parser/object_map.h"
#include "../parser/parser.h"
#include "../parser/symbols.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
/// Marks a C function as retained for Emscripten export.
#define MASM32_SIM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
/// Leaves native test builds unannotated when Emscripten is unavailable.
#define MASM32_SIM_EXPORT
#endif

/// Maximum lexer tokens accepted by the source-run API.
#define MASM32_SIM_WASM_MAX_RUN_TOKENS 512U

/// Maximum lexer diagnostics retained by the source-run API.
#define MASM32_SIM_WASM_MAX_RUN_LEXER_DIAGNOSTICS 64U

/// Maximum parser diagnostics retained by the source-run API.
#define MASM32_SIM_WASM_MAX_RUN_PARSER_DIAGNOSTICS 64U

/// Maximum IR instructions emitted and executed by the source-run API.
#define MASM32_SIM_WASM_MAX_RUN_INSTRUCTIONS 256U

/// Maximum data symbols retained by the source-run API.
#define MASM32_SIM_WASM_MAX_RUN_SYMBOLS 128U

/// Maximum declared-object map entries retained by one source run.
#define MASM32_SIM_WASM_MAX_OBJECT_MAP_ENTRIES MASM32_SIM_WASM_MAX_RUN_SYMBOLS

/// Mask value used for bytes that are currently initialized.
#define MASM32_SIM_WASM_DATA_BYTE_INITIALIZED 1U

/// Mask value used for bytes that remain uninitialized-origin.
#define MASM32_SIM_WASM_DATA_BYTE_UNINITIALIZED 0U

/// Maximum .data/.DATA? bytes laid out by the source-run API.
#define MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES VM_MEMORY_DEFAULT_DATA_SIZE

/// Maximum .CONST bytes laid out by the source-run API.
#define MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES VM_MEMORY_DEFAULT_CONST_SIZE

/// Maximum symbol-aware memory changes retained for one source run.
#define MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES 64U

/// Maximum simulator warnings retained for one source run.
#define MASM32_SIM_WASM_MAX_RUN_WARNINGS 64U

/// Maximum allocated-object warnings retained for one source run.
#define MASM32_SIM_WASM_MAX_OBJECT_WARNINGS 64U

/// Maximum uninitialized-read diagnostics retained for one source run.
#define MASM32_SIM_WASM_MAX_UNINITIALIZED_READ_WARNINGS 64U

/// Maximum undefined shift/rotate flag diagnostics retained for one source run.
#define MASM32_SIM_WASM_MAX_SHIFT_WARNINGS 64U

/// Maximum undefined flag-use diagnostics retained for one source run.
#define MASM32_SIM_WASM_MAX_FLAG_USE_WARNINGS 64U

/// Source-text storage bytes used by parser-emitted IR instruction metadata.
#define MASM32_SIM_WASM_RUN_SOURCE_TEXT_BYTES 8192U

/// Bytes available for the returned source-run JSON response.
#define MASM32_SIM_WASM_RUN_JSON_BYTES 32768U

/// Identifies the high-level source-run outcome used in JSON responses.
typedef enum Masm32SimWasmRunOutcome {
    /// Source parsed and executed successfully.
    MASM32_SIM_WASM_RUN_OUTCOME_OK = 0,
    /// The caller supplied invalid source input.
    MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
    /// Parsing failed or produced diagnostics.
    MASM32_SIM_WASM_RUN_OUTCOME_PARSE_ERROR,
    /// VM initialization, loading, or execution failed.
    MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR,
    /// Automatic deterministic layout exceeded configured resource limits.
    MASM32_SIM_WASM_RUN_OUTCOME_RESOURCE_LIMIT,
    /// A required response could not fit in the fixed JSON buffer.
    MASM32_SIM_WASM_RUN_OUTCOME_TRUNCATED
} Masm32SimWasmRunOutcome;

/// Appends compact JSON into a caller-provided fixed buffer.
typedef struct Masm32SimJsonWriter {
    /// Destination buffer for the JSON document.
    char *buffer;
    /// Total writable buffer size in bytes, including the null terminator.
    size_t capacity;
    /// Number of bytes that would have been written excluding the null terminator.
    size_t length;
    /// Whether any append operation exceeded @ref capacity.
    bool overflowed;
} Masm32SimJsonWriter;

/// Describes one symbol-aware memory change returned to the UI.
typedef struct Masm32SimSymbolicMemoryChange {
    /// Null-terminated symbol name associated with the changed address.
    const char *symbol_name;
    /// Canonical data type name associated with the changed symbol.
    const char *data_type_name;
    /// First simulated address changed by the logical memory write.
    uint32_t address;
    /// Byte offset from the start of the associated symbol.
    uint32_t byte_offset;
    /// Whether @ref element_index contains an aligned element index.
    bool has_element_index;
    /// Element index for aligned changes where the symbol has an element width.
    uint32_t element_index;
    /// Logical write width in bits.
    uint8_t width_bits;
    /// Decoded unsigned value before the write.
    uint32_t old_value;
    /// Decoded unsigned value after the write.
    uint32_t new_value;
} Masm32SimSymbolicMemoryChange;

/// Describes one unaligned memory access warning returned to the UI.
typedef struct Masm32SimWasmUnalignedWarning {
    /// Simulated address used by the unaligned access.
    uint32_t address;
    /// Width of the unaligned access in bits.
    uint8_t width_bits;
    /// Source line associated with the instruction, or zero when unknown.
    uint32_t source_line;
} Masm32SimWasmUnalignedWarning;

/// Describes one allocated-object memory validation diagnostic returned to the UI.
typedef struct Masm32SimWasmObjectBoundsDiagnostic {
    /// Stable classification assigned by the object-map range classifier.
    VmObjectMapRangeClass range_class;
    /// Whether the access read or wrote memory.
    VmExecMemoryAccessKind access_kind;
    /// First simulated address in the validated memory access range.
    uint32_t start_address;
    /// Inclusive final simulated address in the validated memory access range.
    uint32_t end_address;
    /// Number of bytes requested by the access.
    uint32_t size_bytes;
    /// Source line associated with the memory operand, or zero when unknown.
    uint32_t source_line;
    /// Source column associated with the memory operand, or zero when unknown.
    uint32_t source_column;
    /// Zero-based source byte offset of the memory operand.
    size_t source_byte_offset;
    /// Source span length of the memory operand in bytes.
    size_t source_span_length;
    /// Whether byte-offset and span-length fields identify a real source span.
    bool has_source_span;
} Masm32SimWasmObjectBoundsDiagnostic;

/// Describes one uninitialized-origin read diagnostic returned to the UI.
typedef struct Masm32SimWasmUninitializedReadDiagnostic {
    /// First simulated address in the read range.
    uint32_t start_address;
    /// Inclusive final simulated address in the read range.
    uint32_t end_address;
    /// Number of bytes read.
    uint32_t size_bytes;
    /// Number of bytes in the read range that remain uninitialized-origin.
    uint32_t uninitialized_byte_count;
    /// Number of bytes in the read range already marked initialized.
    uint32_t initialized_byte_count;
    /// Name of the containing declared data symbol when known.
    char symbol_name[VM_SYMBOL_NAME_CAPACITY];
    /// Whether @ref symbol_name identifies a containing declared object.
    bool has_symbol_name;
    /// Byte offset from the containing declared symbol base when known.
    uint32_t symbol_byte_offset;
    /// Source line associated with the memory operand, or zero when unknown.
    uint32_t source_line;
    /// Source column associated with the memory operand, or zero when unknown.
    uint32_t source_column;
    /// Zero-based source byte offset of the memory operand.
    size_t source_byte_offset;
    /// Source span length of the memory operand in bytes.
    size_t source_span_length;
    /// Whether byte-offset and span-length fields identify a real source span.
    bool has_source_span;
} Masm32SimWasmUninitializedReadDiagnostic;

/// Describes one shift or rotate undefined-modeled-flag diagnostic returned to the UI.
typedef struct Masm32SimWasmShiftDiagnostic {
    /// Stable instruction mnemonic associated with the diagnostic.
    const char *mnemonic;
    /// Destination width in bits.
    uint8_t width_bits;
    /// Raw shift count before the x86-compatible count mask.
    uint8_t raw_count;
    /// Effective count after applying raw_count & 31.
    uint8_t effective_count;
    /// One-based source line, or zero when unavailable.
    uint32_t source_line;
    /// One-based source column, or zero when unavailable.
    uint32_t source_column;
    /// Zero-based source byte offset of the instruction span.
    size_t source_byte_offset;
    /// Source span length in bytes.
    size_t source_span_length;
    /// Whether byte offset and span length identify a real source span.
    bool has_source_span;
} Masm32SimWasmShiftDiagnostic;

/// Describes one planned memory read before an instruction is stepped.
typedef struct Masm32SimWasmPlannedMemoryRead {
    /// Memory operand that will be read if execution proceeds.
    VmIrOperand operand;
    /// Width of the planned read in bits.
    uint8_t width_bits;
} Masm32SimWasmPlannedMemoryRead;

/// Describes one source-mapped layout failure message.
typedef struct Masm32SimWasmLayoutMessage {
    /// Stable diagnostic code used by Simulator Messages.
    const char *code;
    /// Human-readable diagnostic message.
    char message[256];
    /// One-based source line, or zero when unavailable.
    uint32_t line;
    /// One-based source column, or zero when unavailable.
    uint32_t column;
    /// Zero-based byte offset of the relevant source span.
    size_t byte_offset;
    /// Source span length in bytes.
    size_t span_length;
    /// Whether byte offset and span length are available.
    bool has_source_span;
} Masm32SimWasmLayoutMessage;

/// Stores all fixed buffers needed for one parse-and-run request.
typedef struct Masm32SimWasmRunStorage {
    /// Lexer tokens produced during parsing.
    VmLexerToken tokens[MASM32_SIM_WASM_MAX_RUN_TOKENS];
    /// Lexer diagnostics produced during parsing.
    VmLexerDiagnostic lexer_diagnostics[MASM32_SIM_WASM_MAX_RUN_LEXER_DIAGNOSTICS];
    /// Parser diagnostics produced during parsing.
    VmParserDiagnostic parser_diagnostics[MASM32_SIM_WASM_MAX_RUN_PARSER_DIAGNOSTICS];
    /// IR instructions emitted by the parser.
    VmIrInstruction instructions[MASM32_SIM_WASM_MAX_RUN_INSTRUCTIONS];
    /// Data symbols emitted by the parser.
    VmSymbol symbols[MASM32_SIM_WASM_MAX_RUN_SYMBOLS];
    /// Declared-object map entries built from final selected-layout symbols.
    VmObjectMapEntry object_map_entries[MASM32_SIM_WASM_MAX_OBJECT_MAP_ENTRIES];
    /// Number of valid declared-object map entries.
    size_t object_map_entry_count;
    /// Data image bytes emitted by the parser for `.data` and `.DATA?`.
    uint8_t data_image[MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES];
    /// Per-byte initialization mask for `.data` and `.DATA?` bytes.
    uint8_t data_initialized_mask[MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES];
    /// Number of `.data`/`.DATA?` bytes covered by @ref data_initialized_mask.
    size_t data_initialized_mask_size;
    /// Constant image bytes emitted by the parser for `.CONST`.
    uint8_t const_image[MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES];
    /// Symbol-aware memory changes collected during execution.
    Masm32SimSymbolicMemoryChange memory_changes[MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES];
    /// Number of valid entries in @ref memory_changes.
    size_t memory_change_count;
    /// Unaligned memory access warnings collected during execution.
    Masm32SimWasmUnalignedWarning warnings[MASM32_SIM_WASM_MAX_RUN_WARNINGS];
    /// Number of valid entries in @ref warnings.
    size_t warning_count;
    /// Allocated-object memory validation warnings collected during execution.
    Masm32SimWasmObjectBoundsDiagnostic object_warnings[MASM32_SIM_WASM_MAX_OBJECT_WARNINGS];
    /// Number of valid entries in @ref object_warnings.
    size_t object_warning_count;
    /// Fatal allocated-object strict diagnostic captured during execution.
    Masm32SimWasmObjectBoundsDiagnostic object_violation;
    /// Whether @ref object_violation contains a fatal strict-mode diagnostic.
    bool has_object_violation;
    /// Uninitialized-read warnings collected during execution.
    Masm32SimWasmUninitializedReadDiagnostic uninitialized_read_warnings[MASM32_SIM_WASM_MAX_UNINITIALIZED_READ_WARNINGS];
    /// Number of valid entries in @ref uninitialized_read_warnings.
    size_t uninitialized_read_warning_count;
    /// Fatal uninitialized-read strict diagnostic captured before execution.
    Masm32SimWasmUninitializedReadDiagnostic uninitialized_read_violation;
    /// Whether @ref uninitialized_read_violation contains a fatal strict-mode diagnostic.
    bool has_uninitialized_read_violation;
    /// Undefined shift/rotate flag warnings collected during execution.
    Masm32SimWasmShiftDiagnostic shift_warnings[MASM32_SIM_WASM_MAX_SHIFT_WARNINGS];
    /// Number of valid entries in @ref shift_warnings.
    size_t shift_warning_count;
    /// Fatal strict undefined shift-flag diagnostic captured before execution.
    Masm32SimWasmShiftDiagnostic shift_violation;
    /// Whether @ref shift_violation contains a fatal strict-mode diagnostic.
    bool has_shift_violation;
    /// Undefined flag-use warnings collected before flag-consuming instructions.
    VmFlagUseDiagnostic flag_use_warnings[MASM32_SIM_WASM_MAX_FLAG_USE_WARNINGS];
    /// Number of valid entries in @ref flag_use_warnings.
    size_t flag_use_warning_count;
    /// Fatal undefined flag-use diagnostic captured before a consumer instruction.
    VmFlagUseDiagnostic flag_use_violation;
    /// Whether @ref flag_use_violation contains a fatal consumer diagnostic.
    bool has_flag_use_violation;
    /// Original source text for calculating runtime diagnostic source spans.
    const char *source_text;
    /// Copied source text retained by IR instruction metadata.
    char source_text_storage[MASM32_SIM_WASM_RUN_SOURCE_TEXT_BYTES];
} Masm32SimWasmRunStorage;

/// Static response buffer returned to JavaScript by the source-run export.
static char g_masm32_sim_wasm_run_json[MASM32_SIM_WASM_RUN_JSON_BYTES];

/// Static parse-and-run storage reused by each single-threaded worker request.
static Masm32SimWasmRunStorage g_masm32_sim_wasm_run_storage;

/// Appends formatted text to a JSON writer.
///
/// @param writer Writer to mutate.
/// @param format printf-compatible format string.
/// @return true when the append fit without overflowing the buffer.
static bool masm32_sim_json_append(Masm32SimJsonWriter *writer, const char *format, ...) {
    va_list args;
    int written = 0;
    size_t remaining = 0U;

    if (writer == NULL || writer->buffer == NULL || writer->capacity == 0U || format == NULL) {
        return false;
    }

    remaining = writer->length < writer->capacity ? writer->capacity - writer->length : 0U;
    va_start(args, format);
    written = vsnprintf(writer->length < writer->capacity ? writer->buffer + writer->length : writer->buffer + writer->capacity - 1U, remaining, format, args);
    va_end(args);

    if (written < 0) {
        writer->overflowed = true;
        return false;
    }

    writer->length += (size_t)written;
    if ((size_t)written >= remaining) {
        writer->overflowed = true;
        writer->buffer[writer->capacity - 1U] = '\0';
        return false;
    }

    return true;
}

/// Appends a JSON string value with required escaping.
///
/// @param writer Writer to mutate.
/// @param value Null-terminated string to encode; NULL is encoded as an empty string.
/// @return true when the value fit without overflowing the buffer.
static bool masm32_sim_json_append_string(Masm32SimJsonWriter *writer, const char *value) {
    const unsigned char *cursor = (const unsigned char *)(value != NULL ? value : "");

    if (writer == NULL) {
        return false;
    }

    if (!masm32_sim_json_append(writer, "\"")) {
        return false;
    }

    while (*cursor != 0U) {
        unsigned char ch = *cursor;
        if (ch == '"' || ch == '\\') {
            if (!masm32_sim_json_append(writer, "\\%c", ch)) {
                return false;
            }
        } else if (ch == '\n') {
            if (!masm32_sim_json_append(writer, "\\n")) {
                return false;
            }
        } else if (ch == '\r') {
            if (!masm32_sim_json_append(writer, "\\r")) {
                return false;
            }
        } else if (ch == '\t') {
            if (!masm32_sim_json_append(writer, "\\t")) {
                return false;
            }
        } else if (ch < 0x20U) {
            if (!masm32_sim_json_append(writer, "\\u%04X", (unsigned int)ch)) {
                return false;
            }
        } else {
            if (!masm32_sim_json_append(writer, "%c", (char)ch)) {
                return false;
            }
        }
        cursor += 1U;
    }

    return masm32_sim_json_append(writer, "\"");
}

/// Returns a stable JSON status name for a high-level source-run outcome.
///
/// @param outcome Outcome to name.
/// @return Static lowercase status string.
static const char *masm32_sim_wasm_run_outcome_name(Masm32SimWasmRunOutcome outcome) {
    switch (outcome) {
        case MASM32_SIM_WASM_RUN_OUTCOME_OK:
            return "ok";
        case MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT:
            return "invalid-argument";
        case MASM32_SIM_WASM_RUN_OUTCOME_PARSE_ERROR:
            return "parse-error";
        case MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR:
            return "execution-error";
        case MASM32_SIM_WASM_RUN_OUTCOME_RESOURCE_LIMIT:
            return "resource-limit-exceeded";
        case MASM32_SIM_WASM_RUN_OUTCOME_TRUNCATED:
            return "response-truncated";
        default:
            return "internal-error";
    }
}

/// Appends one simulator message object to a JSON writer with optional source span metadata.
///
/// @param writer Writer to mutate.
/// @param kind Diagnostic category string.
/// @param code Stable diagnostic code string.
/// @param message Human-readable diagnostic summary.
/// @param line One-based source line, or 0 when not available.
/// @param column One-based source column, or 0 when not available.
/// @param byte_offset Zero-based source byte offset when @p has_source_span is true.
/// @param span_length Source span length in bytes when @p has_source_span is true.
/// @param has_source_span Whether byte-offset and span-length fields should be emitted.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_message_with_span(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const char *code,
    const char *message,
    uint32_t line,
    uint32_t column,
    size_t byte_offset,
    size_t span_length,
    bool has_source_span
) {
    if (!masm32_sim_json_append(writer, "{\"kind\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, kind)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"code\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, code)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"message\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, message)) {
        return false;
    }
    if (line > 0U) {
        if (!masm32_sim_json_append(writer, ",\"line\":%u", (unsigned int)line)) {
            return false;
        }
    }
    if (column > 0U) {
        if (!masm32_sim_json_append(writer, ",\"column\":%u", (unsigned int)column)) {
            return false;
        }
    }
    if (has_source_span) {
        if (!masm32_sim_json_append(writer, ",\"byteOffset\":%llu", (unsigned long long)byte_offset)) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ",\"spanLength\":%llu", (unsigned long long)span_length)) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
}

/// Appends one simulator message object to a JSON writer without source span metadata.
///
/// @param writer Writer to mutate.
/// @param kind Diagnostic category string.
/// @param code Stable diagnostic code string.
/// @param message Human-readable diagnostic summary.
/// @param line One-based source line, or 0 when not available.
/// @param column One-based source column, or 0 when not available.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_message(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const char *code,
    const char *message,
    uint32_t line,
    uint32_t column
) {
    return masm32_sim_json_append_message_with_span(writer, kind, code, message, line, column, 0U, 0U, false);
}

/// Appends the canonical 32-bit register object used by the UI.
///
/// @param writer Writer to mutate.
/// @param cpu CPU state to inspect.
/// @param reg Register to read.
/// @param is_first Whether this is the first object property in the register map.
/// @return true when the register object fit without overflowing the buffer.
static bool masm32_sim_json_append_register(Masm32SimJsonWriter *writer, const VmCpu *cpu, VmRegister reg, bool is_first) {
    uint32_t value = 0U;
    const char *name = vm_cpu_register_name(reg);

    if (writer == NULL || cpu == NULL || name == NULL || !vm_cpu_read_register(cpu, reg, &value)) {
        return false;
    }

    if (!is_first && !masm32_sim_json_append(writer, ",")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, name)) {
        return false;
    }

    return masm32_sim_json_append(
        writer,
        ":{\"hex\":\"%08Xh\",\"unsigned\":%u}",
        (unsigned int)value,
        (unsigned int)value
    );
}

/// Appends the current canonical MASM32 registers to the result JSON.
///
/// @param writer Writer to mutate.
/// @param cpu CPU state to inspect.
/// @return true when the register map fit without overflowing the buffer.
static bool masm32_sim_json_append_registers(Masm32SimJsonWriter *writer, const VmCpu *cpu) {
    static const VmRegister registers[] = {
        VM_REGISTER_EAX,
        VM_REGISTER_EBX,
        VM_REGISTER_ECX,
        VM_REGISTER_EDX,
        VM_REGISTER_ESI,
        VM_REGISTER_EDI,
        VM_REGISTER_EBP,
        VM_REGISTER_ESP,
        VM_REGISTER_EIP,
        VM_REGISTER_EFLAGS
    };
    size_t index = 0U;

    if (!masm32_sim_json_append(writer, "\"registers\":{")) {
        return false;
    }

    for (index = 0U; index < sizeof(registers) / sizeof(registers[0]); index += 1U) {
        if (!masm32_sim_json_append_register(writer, cpu, registers[index], index == 0U)) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
}


/// Returns a display-width-specific uppercase hexadecimal value for memory changes.
///
/// @param writer Writer to mutate.
/// @param value Value to format.
/// @param width_bits Logical value width in bits.
/// @return true when the value fit without overflowing the buffer.
static bool masm32_sim_json_append_memory_hex(Masm32SimJsonWriter *writer, uint32_t value, uint8_t width_bits) {
    if (width_bits == 8U) {
        return masm32_sim_json_append(writer, "\"%02Xh\"", (unsigned int)(value & 0xFFU));
    }
    if (width_bits == 16U) {
        return masm32_sim_json_append(writer, "\"%04Xh\"", (unsigned int)(value & 0xFFFFU));
    }
    return masm32_sim_json_append(writer, "\"%08Xh\"", (unsigned int)value);
}

/// Appends one symbol-aware memory change object to JSON.
///
/// @param writer Writer to mutate.
/// @param change Memory change to append.
/// @return true when the change object fit without overflowing the buffer.
static bool masm32_sim_json_append_memory_change(Masm32SimJsonWriter *writer, const Masm32SimSymbolicMemoryChange *change) {
    if (writer == NULL || change == NULL) {
        return false;
    }

    if (!masm32_sim_json_append(writer, "{\"symbol\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, change->symbol_name)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"address\":\"%08Xh\",\"widthBits\":%u,\"byteOffset\":%u", (unsigned int)change->address, (unsigned int)change->width_bits, (unsigned int)change->byte_offset)) {
        return false;
    }
    if (change->data_type_name != NULL) {
        if (!masm32_sim_json_append(writer, ",\"dataType\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, change->data_type_name)) {
            return false;
        }
    }
    if (change->has_element_index) {
        if (!masm32_sim_json_append(writer, ",\"elementIndex\":%u", (unsigned int)change->element_index)) {
            return false;
        }
    }
    if (!masm32_sim_json_append(writer, ",\"oldHex\":")) {
        return false;
    }
    if (!masm32_sim_json_append_memory_hex(writer, change->old_value, change->width_bits)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"oldUnsigned\":%u,\"newHex\":", (unsigned int)change->old_value)) {
        return false;
    }
    if (!masm32_sim_json_append_memory_hex(writer, change->new_value, change->width_bits)) {
        return false;
    }

    return masm32_sim_json_append(writer, ",\"newUnsigned\":%u}", (unsigned int)change->new_value);
}

/// Appends the symbol-aware memory change array to JSON.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing collected memory changes.
/// @return true when the array fit without overflowing the buffer.
static bool masm32_sim_json_append_memory_changes(Masm32SimJsonWriter *writer, const Masm32SimWasmRunStorage *storage) {
    size_t index = 0U;

    if (!masm32_sim_json_append(writer, "\"memoryChanges\":[")) {
        return false;
    }

    if (storage != NULL) {
        for (index = 0U; index < storage->memory_change_count; index += 1U) {
            if (index > 0U && !masm32_sim_json_append(writer, ",")) {
                return false;
            }
            if (!masm32_sim_json_append_memory_change(writer, &storage->memory_changes[index])) {
                return false;
            }
        }
    }

    return masm32_sim_json_append(writer, "]");
}

/// Loads parser-produced section image bytes into VM memory.
///
/// @param vm VM whose memory region should be initialized.
/// @param region Region that receives the parser-produced bytes.
/// @param image Parser-produced image bytes.
/// @param size Number of bytes to write.
/// @return Executor status representing the load result.
static VmExecStatus masm32_sim_wasm_load_section_image(Vm *vm, VmMemoryRegionKind region, const uint8_t *image, uint32_t size) {
    VmMemoryStatus memory_status = VM_MEMORY_STATUS_OK;

    if (vm == NULL || (image == NULL && size > 0U)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memory_status = vm_memory_load_region_bytes(&vm->memory, region, 0U, image, size);
    return vm_memory_status_succeeded(memory_status) ? VM_EXEC_STATUS_OK : VM_EXEC_STATUS_MEMORY_ERROR;
}

/// Combines little-endian bytes into a 32-bit unsigned value.
///
/// @param bytes Bytes to combine.
/// @param size Number of bytes to combine.
/// @return Decoded little-endian value.
static uint32_t masm32_sim_wasm_decode_u32(const uint8_t *bytes, uint8_t size) {
    uint8_t index = 0U;
    uint32_t value = 0U;

    if (bytes == NULL) {
        return 0U;
    }

    for (index = 0U; index < size; index += 1U) {
        value |= ((uint32_t)bytes[index]) << (8U * index);
    }

    return value;
}

/// Finds the effective address of the logical memory write in a step delta.
///
/// @param delta Step delta to inspect.
/// @param out_address Receives the effective write address.
/// @return true when a write access was recorded.
static bool masm32_sim_wasm_delta_write_address(const VmExecDelta *delta, uint32_t *out_address) {
    size_t index = 0U;

    if (delta == NULL || out_address == NULL) {
        return false;
    }

    for (index = 0U; index < delta->memory_access_count; index += 1U) {
        const VmExecMemoryAccess *access = &delta->memory_accesses[index];
        if (access->kind == VM_EXEC_MEMORY_ACCESS_WRITE && vm_memory_status_succeeded(access->status)) {
            *out_address = access->address;
            return true;
        }
    }

    return false;
}

/// Collects one symbol-aware logical memory write from the last-step delta.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose last delta should be inspected.
/// @param parser_result Parser result containing symbol count.
static void masm32_sim_wasm_collect_memory_change(Masm32SimWasmRunStorage *storage, const Vm *vm, const VmParserResult *parser_result) {
    const VmExecDelta *delta = NULL;
    const VmIrOperand *destination = NULL;
    const VmSymbol *symbol = NULL;
    uint32_t destination_address = 0U;
    uint8_t width_bytes = 0U;
    uint8_t old_bytes[4] = {0U, 0U, 0U, 0U};
    uint8_t new_bytes[4] = {0U, 0U, 0U, 0U};
    uint8_t byte_index = 0U;
    size_t change_index = 0U;
    bool has_changed_byte = false;

    if (storage == NULL || vm == NULL || parser_result == NULL || storage->memory_change_count >= (size_t)MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES) {
        return;
    }

    delta = vm_last_delta(vm);
    if (delta == NULL || !delta->has_instruction || delta->memory_change_count == 0U) {
        return;
    }

    destination = &delta->instruction.destination;
    if ((destination->kind != VM_IR_OPERAND_MEMORY_ADDRESS && destination->kind != VM_IR_OPERAND_MEMORY_REGISTER) ||
        (destination->width_bits != 8U && destination->width_bits != 16U && destination->width_bits != 32U)) {
        return;
    }

    if (!masm32_sim_wasm_delta_write_address(delta, &destination_address)) {
        return;
    }

    width_bytes = (uint8_t)(destination->width_bits / 8U);
    symbol = vm_symbol_find_by_address(storage->symbols, parser_result->symbol_count, destination_address);
    if (symbol == NULL || destination_address + width_bytes > symbol->address + symbol->size_bytes) {
        return;
    }

    for (byte_index = 0U; byte_index < width_bytes; byte_index += 1U) {
        uint8_t current_byte = 0U;
        (void)vm_memory_read_u8(&vm->memory, destination_address + byte_index, &current_byte, NULL);
        old_bytes[byte_index] = current_byte;
        new_bytes[byte_index] = current_byte;
    }

    for (change_index = 0U; change_index < delta->memory_change_count; change_index += 1U) {
        const VmMemoryByteChange *byte_change = &delta->memory_changes[change_index];
        if (byte_change->address >= destination_address && byte_change->address < destination_address + width_bytes) {
            uint8_t relative = (uint8_t)(byte_change->address - destination_address);
            old_bytes[relative] = byte_change->old_value;
            new_bytes[relative] = byte_change->new_value;
            has_changed_byte = true;
        }
    }

    if (has_changed_byte) {
        Masm32SimSymbolicMemoryChange *change = &storage->memory_changes[storage->memory_change_count];
        change->symbol_name = symbol->name;
        change->data_type_name = destination->width_bits == 8U ? "BYTE" : (destination->width_bits == 16U ? "WORD" : "DWORD");
        change->address = destination_address;
        change->byte_offset = destination_address - symbol->address;
        change->has_element_index = symbol->element_size_bytes != 0U && (change->byte_offset % (uint32_t)symbol->element_size_bytes) == 0U;
        change->element_index = change->has_element_index ? change->byte_offset / (uint32_t)symbol->element_size_bytes : 0U;
        change->width_bits = destination->width_bits;
        change->old_value = masm32_sim_wasm_decode_u32(old_bytes, width_bytes);
        change->new_value = masm32_sim_wasm_decode_u32(new_bytes, width_bytes);
        storage->memory_change_count += 1U;
    }
}

/// Returns a MASM data-width name for warning messages.
///
/// @param width_bits Width in bits to name.
/// @return Static width name used in simulator warnings.
static const char *masm32_sim_wasm_width_name(uint8_t width_bits) {
    if (width_bits == 8U) {
        return "BYTE";
    }
    if (width_bits == 16U) {
        return "WORD";
    }
    if (width_bits == 32U) {
        return "DWORD";
    }
    return "memory";
}

/// Returns whether a checked memory access was successful and unaligned.
///
/// @param access Checked memory access to inspect.
/// @return true when the memory module reported an unaligned successful access.
static bool masm32_sim_wasm_access_is_unaligned(const VmExecMemoryAccess *access) {
    return access != NULL && access->status == VM_MEMORY_STATUS_OK_UNALIGNED;
}

/// Returns whether a checked memory access completed successfully.
///
/// @param access Checked memory access to inspect.
/// @return true when the low-level memory helper allowed the access.
static bool masm32_sim_wasm_access_succeeded(const VmExecMemoryAccess *access) {
    return access != NULL && vm_memory_status_succeeded(access->status);
}

/// Returns whether an object-map classification escapes declared object bounds.
///
/// @param range_class Classification to inspect.
/// @return true when allocated-object validation should diagnose the access.
static bool masm32_sim_wasm_range_class_escapes_object_bounds(VmObjectMapRangeClass range_class) {
    return range_class == VM_OBJECT_MAP_RANGE_CLASS_REGION_GAP ||
           range_class == VM_OBJECT_MAP_RANGE_CLASS_STARTS_IN_OBJECT ||
           range_class == VM_OBJECT_MAP_RANGE_CLASS_ENDS_IN_OBJECT ||
           range_class == VM_OBJECT_MAP_RANGE_CLASS_SPANS_OBJECTS;
}

/// Returns a lowercase name for one memory access kind.
///
/// @param kind Executor memory access kind.
/// @return Static lowercase access kind name.
static const char *masm32_sim_wasm_exec_memory_access_kind_name(VmExecMemoryAccessKind kind) {
    switch (kind) {
        case VM_EXEC_MEMORY_ACCESS_READ:
            return "read";
        case VM_EXEC_MEMORY_ACCESS_WRITE:
            return "write";
        default:
            return "access";
    }
}

/// Finds the zero-based byte offset of a one-based source line.
///
/// @param source Original source text.
/// @param line One-based source line to locate.
/// @param out_offset Receives the line-start byte offset.
/// @return true when the line start was found.
static bool masm32_sim_wasm_find_line_start_offset(const char *source, uint32_t line, size_t *out_offset) {
    size_t offset = 0U;
    uint32_t current_line = 1U;

    if (source == NULL || out_offset == NULL || line == 0U) {
        return false;
    }

    if (line == 1U) {
        *out_offset = 0U;
        return true;
    }

    while (source[offset] != '\0') {
        if (source[offset] == '\n') {
            current_line += 1U;
            if (current_line == line) {
                *out_offset = offset + 1U;
                return true;
            }
        }
        offset += 1U;
    }

    return false;
}

/// Copies best-effort memory operand source-span metadata into a diagnostic.
///
/// The current IR preserves instruction source text but not token-level operand
/// spans. Strict object-bounds diagnostics therefore derive the span from the
/// bracketed memory operand in the original source line, which covers the
/// currently supported object-bounds strict cases without changing executor IR.
///
/// @param diagnostic Object-bounds diagnostic to mutate.
/// @param instruction Instruction that attempted the access.
/// @param source Original source text for byte-offset reconstruction.
static void masm32_sim_wasm_copy_object_diagnostic_source_span(
    Masm32SimWasmObjectBoundsDiagnostic *diagnostic,
    const VmIrInstruction *instruction,
    const char *source
) {
    const char *line_text = instruction != NULL ? instruction->source_text : NULL;
    const char *span_start = NULL;
    const char *span_end = NULL;
    size_t line_start_offset = 0U;

    if (diagnostic == NULL) {
        return;
    }

    diagnostic->source_line = instruction != NULL ? instruction->source_line : 0U;
    diagnostic->source_column = 0U;
    diagnostic->source_byte_offset = 0U;
    diagnostic->source_span_length = 0U;
    diagnostic->has_source_span = false;

    if (line_text == NULL || diagnostic->source_line == 0U ||
        !masm32_sim_wasm_find_line_start_offset(source, diagnostic->source_line, &line_start_offset)) {
        return;
    }

    span_start = strchr(line_text, '[');
    if (span_start == NULL) {
        return;
    }
    span_end = strchr(span_start, ']');
    if (span_end == NULL || span_end < span_start) {
        return;
    }

    {
        const char *source_line = source + line_start_offset;
        const char *source_line_end = strchr(source_line, '\n');
        const char *line_text_start = NULL;
        size_t line_text_offset = 0U;

        if (source_line_end == NULL) {
            source_line_end = source_line + strlen(source_line);
        }
        line_text_start = strstr(source_line, line_text);
        if (line_text_start != NULL && line_text_start <= source_line_end) {
            line_text_offset = (size_t)(line_text_start - source_line);
        }

        diagnostic->source_column = (uint32_t)(line_text_offset + (size_t)(span_start - line_text) + 1U);
        diagnostic->source_byte_offset = line_start_offset + (size_t)(diagnostic->source_column - 1U);
    }
    diagnostic->source_span_length = (size_t)(span_end - span_start) + 1U;
    diagnostic->has_source_span = true;
}

/// Populates one allocated-object diagnostic from a range classification.
///
/// @param diagnostic Diagnostic to populate.
/// @param instruction Instruction associated with the access.
/// @param access Checked access that was classified.
/// @param classification Object-map classification to copy.
/// @param size_bytes Access size in bytes.
/// @param source Original source text for byte-offset reconstruction.
static void masm32_sim_wasm_fill_object_bounds_diagnostic(
    Masm32SimWasmObjectBoundsDiagnostic *diagnostic,
    const VmIrInstruction *instruction,
    const VmExecMemoryAccess *access,
    const VmObjectMapRangeClassification *classification,
    uint32_t size_bytes,
    const char *source
) {
    if (diagnostic == NULL || access == NULL || classification == NULL) {
        return;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->range_class = classification->range_class;
    diagnostic->access_kind = access->kind;
    diagnostic->start_address = classification->start_address;
    diagnostic->end_address = classification->end_address;
    diagnostic->size_bytes = size_bytes;
    masm32_sim_wasm_copy_object_diagnostic_source_span(diagnostic, instruction, source);
}

/// Validates one memory access against the allocated-object mode.
///
/// Warning mode records a non-fatal warning. Strict mode records the first fatal
/// object-bounds violation and asks the execution loop to stop. Region and
/// permission failures are ignored here because lower-level diagnostics have
/// already taken precedence by making the access unsuccessful.
///
/// @param storage Source-run storage to mutate.
/// @param instruction Instruction associated with the access.
/// @param access Checked memory access that should be classified.
/// @param validation_mode Memory validation behavior selected for the run.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return true when execution may continue, false for strict violations.
static bool masm32_sim_wasm_validate_object_access(
    Masm32SimWasmRunStorage *storage,
    const VmIrInstruction *instruction,
    const VmExecMemoryAccess *access,
    Masm32SimWasmMemoryValidationMode validation_mode,
    const VmLayoutPolicy *layout_policy
) {
    VmObjectMapRangeClassification classification;
    VmObjectMapStatus status = VM_OBJECT_MAP_STATUS_OK;
    uint32_t size_bytes = 0U;

    if (storage == NULL || instruction == NULL || access == NULL ||
        validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY) {
        return true;
    }
    if (!masm32_sim_wasm_access_succeeded(access) || access->width_bits == 0U || (access->width_bits % 8U) != 0U) {
        return true;
    }

    size_bytes = (uint32_t)(access->width_bits / 8U);
    status = vm_object_map_classify_range(
        storage->object_map_entries,
        storage->object_map_entry_count,
        layout_policy,
        NULL,
        access->address,
        size_bytes,
        access->kind == VM_EXEC_MEMORY_ACCESS_WRITE,
        &classification
    );
    if (status != VM_OBJECT_MAP_STATUS_OK || !masm32_sim_wasm_range_class_escapes_object_bounds(classification.range_class)) {
        return true;
    }

    if (validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS) {
        if (storage->object_warning_count < (size_t)MASM32_SIM_WASM_MAX_OBJECT_WARNINGS) {
            masm32_sim_wasm_fill_object_bounds_diagnostic(
                &storage->object_warnings[storage->object_warning_count],
                instruction,
                access,
                &classification,
                size_bytes,
                storage->source_text
            );
            storage->object_warning_count += 1U;
        }
        return true;
    }

    if (validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT) {
        masm32_sim_wasm_fill_object_bounds_diagnostic(
            &storage->object_violation,
            instruction,
            access,
            &classification,
            size_bytes,
            storage->source_text
        );
        storage->has_object_violation = true;
        return false;
    }

    return true;
}


/// Marks one successful write access as initialized in the Phase 39 data mask.
///
/// @param storage Source-run storage containing the initialization mask.
/// @param access Successful checked memory write access to apply.
/// @param layout_policy Selected runtime layout policy, or NULL for the fixed default.
static void masm32_sim_wasm_mark_initialized_for_access(
    Masm32SimWasmRunStorage *storage,
    const VmExecMemoryAccess *access,
    const VmLayoutPolicy *layout_policy
) {
    VmLayoutPolicy default_policy;
    const VmLayoutPolicy *effective_policy = layout_policy;
    const VmLayoutRegionPolicy *data_region = NULL;
    uint32_t size_bytes = 0U;
    uint32_t offset = 0U;
    uint32_t index = 0U;

    if (storage == NULL || access == NULL || access->kind != VM_EXEC_MEMORY_ACCESS_WRITE ||
        !masm32_sim_wasm_access_succeeded(access) || access->width_bits == 0U || (access->width_bits % 8U) != 0U) {
        return;
    }

    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    data_region = &effective_policy->regions[VM_LAYOUT_REGION_DATA];
    size_bytes = (uint32_t)(access->width_bits / 8U);
    if (access->address < data_region->base || access->address >= data_region->limit) {
        return;
    }
    offset = access->address - data_region->base;
    if ((uint64_t)offset + (uint64_t)size_bytes > (uint64_t)storage->data_initialized_mask_size) {
        return;
    }

    for (index = 0U; index < size_bytes; index += 1U) {
        storage->data_initialized_mask[(size_t)offset + (size_t)index] = MASM32_SIM_WASM_DATA_BYTE_INITIALIZED;
    }
}

/// Marks successful writes from the last executed instruction as initialized.
///
/// @param storage Source-run storage containing the initialization mask.
/// @param vm VM whose last delta should be inspected.
/// @param layout_policy Selected runtime layout policy, or NULL for the fixed default.
static void masm32_sim_wasm_mark_initialized_writes(Masm32SimWasmRunStorage *storage, const Vm *vm, const VmLayoutPolicy *layout_policy) {
    const VmExecDelta *delta = NULL;
    size_t index = 0U;

    if (storage == NULL || vm == NULL) {
        return;
    }

    delta = vm_last_delta(vm);
    if (delta == NULL || !delta->has_instruction) {
        return;
    }

    for (index = 0U; index < delta->memory_access_count; index += 1U) {
        masm32_sim_wasm_mark_initialized_for_access(storage, &delta->memory_accesses[index], layout_policy);
    }
}

/// Applies allocated-object validation to all accesses from the last instruction.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose last delta should be inspected.
/// @param validation_mode Memory validation behavior selected for the run.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return VM_EXEC_STATUS_OK when execution may continue, or MEMORY_ERROR when strict mode fails.
static VmExecStatus masm32_sim_wasm_validate_object_accesses(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    Masm32SimWasmMemoryValidationMode validation_mode,
    const VmLayoutPolicy *layout_policy
) {
    const VmExecDelta *delta = NULL;
    size_t index = 0U;

    if (storage == NULL || vm == NULL || validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY) {
        return VM_EXEC_STATUS_OK;
    }

    delta = vm_last_delta(vm);
    if (delta == NULL || !delta->has_instruction) {
        return VM_EXEC_STATUS_OK;
    }

    for (index = 0U; index < delta->memory_access_count; index += 1U) {
        if (!masm32_sim_wasm_validate_object_access(storage, &delta->instruction, &delta->memory_accesses[index], validation_mode, layout_policy)) {
            return VM_EXEC_STATUS_MEMORY_ERROR;
        }
    }

    return VM_EXEC_STATUS_OK;
}

/// Returns whether a validation mode enables uninitialized-read diagnostics.
///
/// @param validation_mode Memory validation behavior selected for the run.
/// @return true for Phase 40 warning or strict uninitialized-read modes.
static bool masm32_sim_wasm_validation_checks_uninitialized_reads(Masm32SimWasmMemoryValidationMode validation_mode) {
    return validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS ||
        validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT;
}

/// Returns whether a validation mode stops on uninitialized-origin reads.
///
/// @param validation_mode Memory validation behavior selected for the run.
/// @return true when uninitialized reads should be fatal.
static bool masm32_sim_wasm_validation_strict_uninitialized_reads(Masm32SimWasmMemoryValidationMode validation_mode) {
    return validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT;
}

/// Returns whether an IR operand is a memory operand.
///
/// @param operand Operand to inspect.
/// @return true when @p operand describes an absolute or register-computed memory address.
static bool masm32_sim_wasm_operand_is_memory(const VmIrOperand *operand) {
    return operand != NULL &&
        (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS || operand->kind == VM_IR_OPERAND_MEMORY_REGISTER);
}

/// Resolves an operand width using IR metadata and register aliases.
///
/// @param operand Operand whose width should be resolved.
/// @param out_width_bits Receives the width in bits.
/// @return true when a supported 8-, 16-, or 32-bit width is available.
static bool masm32_sim_wasm_operand_width(const VmIrOperand *operand, uint8_t *out_width_bits) {
    uint8_t width_bits = 0U;

    if (out_width_bits != NULL) {
        *out_width_bits = 0U;
    }
    if (operand == NULL || out_width_bits == NULL) {
        return false;
    }

    if (operand->width_bits != 0U) {
        width_bits = operand->width_bits;
    } else if (operand->kind == VM_IR_OPERAND_REGISTER) {
        width_bits = vm_cpu_register_width_bits(operand->reg);
    }

    if (!vm_ir_width_is_supported(width_bits)) {
        return false;
    }

    *out_width_bits = width_bits;
    return true;
}

/// Resolves the effective address for a planned memory read.
///
/// This mirrors the executor's current register-indirect address arithmetic so
/// pre-step strict diagnostics can stop read-modify-write instructions before
/// write-back without broadening VM execution semantics.
///
/// @param vm VM whose register values should be inspected.
/// @param operand Memory operand to resolve.
/// @param out_address Receives the effective simulated address.
/// @return true when the operand is a supported memory operand.
static bool masm32_sim_wasm_resolve_memory_operand_address(const Vm *vm, const VmIrOperand *operand, uint32_t *out_address) {
    uint32_t base_value = 0U;
    uint32_t address = 0U;

    if (out_address != NULL) {
        *out_address = 0U;
    }
    if (vm == NULL || operand == NULL || out_address == NULL) {
        return false;
    }

    if (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS) {
        *out_address = operand->address;
        return true;
    }
    if (operand->kind != VM_IR_OPERAND_MEMORY_REGISTER) {
        return false;
    }
    if (!vm_cpu_read_register(&vm->cpu, operand->reg, &base_value)) {
        return false;
    }

    address = operand->address + base_value;
    if ((int32_t)operand->immediate < 0) {
        uint32_t magnitude = (uint32_t)(-(int64_t)(int32_t)operand->immediate);
        address -= magnitude;
    } else {
        address += (uint32_t)(int32_t)operand->immediate;
    }

    *out_address = address;
    return true;
}

/// Adds one planned read to a fixed probe list when the operand reads memory.
///
/// Duplicate ranges in the same instruction are suppressed so a construct such
/// as TEST over the same memory operand emits one warning for the read range.
///
/// @param reads Output planned-read array.
/// @param read_capacity Number of entries available in @p reads.
/// @param inout_read_count Current count to update.
/// @param operand Operand that may read memory.
/// @param width_bits Read width in bits.
static void masm32_sim_wasm_add_planned_read(
    Masm32SimWasmPlannedMemoryRead *reads,
    size_t read_capacity,
    size_t *inout_read_count,
    const VmIrOperand *operand,
    uint8_t width_bits
) {
    size_t index = 0U;

    if (reads == NULL || inout_read_count == NULL || operand == NULL || !masm32_sim_wasm_operand_is_memory(operand) ||
        !vm_ir_width_is_supported(width_bits)) {
        return;
    }

    for (index = 0U; index < *inout_read_count; index += 1U) {
        if (reads[index].operand.kind == operand->kind && reads[index].operand.address == operand->address &&
            reads[index].operand.immediate == operand->immediate && reads[index].operand.reg == operand->reg &&
            reads[index].width_bits == width_bits) {
            return;
        }
    }

    if (*inout_read_count >= read_capacity) {
        return;
    }

    reads[*inout_read_count].operand = *operand;
    reads[*inout_read_count].width_bits = width_bits;
    *inout_read_count += 1U;
}

/// Collects memory reads that an instruction will perform if stepped.
///
/// The collector is intentionally limited to opcodes already implemented by
/// the executor. Future memory-capable instructions should add their read
/// patterns here when their milestones implement the instruction semantics.
///
/// @param instruction Instruction to inspect.
/// @param reads Output planned-read array.
/// @param read_capacity Number of entries available in @p reads.
/// @return Number of planned reads collected.
static size_t masm32_sim_wasm_collect_planned_reads(
    const VmIrInstruction *instruction,
    Masm32SimWasmPlannedMemoryRead *reads,
    size_t read_capacity
) {
    size_t read_count = 0U;
    uint8_t width_bits = 0U;

    if (instruction == NULL || reads == NULL || read_capacity == 0U) {
        return 0U;
    }

    switch (instruction->opcode) {
        case VM_IR_OPCODE_MOV:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->source, width_bits);
            }
            break;
        case VM_IR_OPCODE_ADD:
        case VM_IR_OPCODE_SUB:
        case VM_IR_OPCODE_ADC:
        case VM_IR_OPCODE_SBB:
        case VM_IR_OPCODE_TEST:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->destination, width_bits);
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->source, width_bits);
            }
            break;
        case VM_IR_OPCODE_NEG:
        case VM_IR_OPCODE_NOT:
        case VM_IR_OPCODE_SHL:
        case VM_IR_OPCODE_SAL:
        case VM_IR_OPCODE_SHR:
        case VM_IR_OPCODE_SAR:
        case VM_IR_OPCODE_ROL:
        case VM_IR_OPCODE_ROR:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->destination, width_bits);
            }
            break;
        case VM_IR_OPCODE_XCHG:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->destination, width_bits);
            }
            if (masm32_sim_wasm_operand_width(&instruction->source, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->source, width_bits);
            }
            break;
        case VM_IR_OPCODE_MOVSX:
        case VM_IR_OPCODE_MOVZX:
            if (masm32_sim_wasm_operand_width(&instruction->source, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->source, width_bits);
            }
            break;
        default:
            /* TODO: Add future memory read-modify-write opcodes here when those instruction milestones implement them. */
            break;
    }

    return read_count;
}

/// Copies best-effort bracketed memory operand source-span metadata.
///
/// @param instruction Instruction associated with the memory operand.
/// @param source Original source text for byte-offset reconstruction.
/// @param out_column Receives a one-based source column, or zero.
/// @param out_byte_offset Receives the zero-based source byte offset.
/// @param out_span_length Receives source span length in bytes.
/// @param out_has_source_span Receives whether byte offset and span length are valid.
static void masm32_sim_wasm_copy_bracketed_memory_source_span(
    const VmIrInstruction *instruction,
    const char *source,
    uint32_t *out_column,
    size_t *out_byte_offset,
    size_t *out_span_length,
    bool *out_has_source_span
) {
    const char *line_text = instruction != NULL ? instruction->source_text : NULL;
    const char *span_start = NULL;
    const char *span_end = NULL;
    size_t line_start_offset = 0U;

    if (out_column != NULL) {
        *out_column = 0U;
    }
    if (out_byte_offset != NULL) {
        *out_byte_offset = 0U;
    }
    if (out_span_length != NULL) {
        *out_span_length = 0U;
    }
    if (out_has_source_span != NULL) {
        *out_has_source_span = false;
    }
    if (instruction == NULL || source == NULL || line_text == NULL || instruction->source_line == 0U ||
        out_column == NULL || out_byte_offset == NULL || out_span_length == NULL || out_has_source_span == NULL ||
        !masm32_sim_wasm_find_line_start_offset(source, instruction->source_line, &line_start_offset)) {
        return;
    }

    span_start = strchr(line_text, '[');
    if (span_start == NULL) {
        return;
    }
    span_end = strchr(span_start, ']');
    if (span_end == NULL || span_end < span_start) {
        return;
    }

    {
        const char *source_line = source + line_start_offset;
        const char *source_line_end = strchr(source_line, '\n');
        const char *line_text_start = NULL;
        size_t line_text_offset = 0U;

        if (source_line_end == NULL) {
            source_line_end = source_line + strlen(source_line);
        }
        line_text_start = strstr(source_line, line_text);
        if (line_text_start != NULL && line_text_start <= source_line_end) {
            line_text_offset = (size_t)(line_text_start - source_line);
        }

        *out_column = (uint32_t)(line_text_offset + (size_t)(span_start - line_text) + 1U);
        *out_byte_offset = line_start_offset + (size_t)(*out_column - 1U);
    }
    *out_span_length = (size_t)(span_end - span_start) + 1U;
    *out_has_source_span = true;
}

/// Copies best-effort full-instruction source-span metadata.
///
/// @param instruction Instruction associated with a runtime diagnostic.
/// @param source Original source text for byte-offset reconstruction.
/// @param out_column Receives a one-based source column, or zero.
/// @param out_byte_offset Receives the zero-based source byte offset.
/// @param out_span_length Receives source span length in bytes.
/// @param out_has_source_span Receives whether byte offset and span length are valid.
static void masm32_sim_wasm_copy_instruction_source_span(
    const VmIrInstruction *instruction,
    const char *source,
    uint32_t *out_column,
    size_t *out_byte_offset,
    size_t *out_span_length,
    bool *out_has_source_span
) {
    const char *line_text = instruction != NULL ? instruction->source_text : NULL;
    size_t line_start_offset = 0U;
    size_t line_text_length = line_text != NULL ? strlen(line_text) : 0U;

    if (out_column != NULL) {
        *out_column = 0U;
    }
    if (out_byte_offset != NULL) {
        *out_byte_offset = 0U;
    }
    if (out_span_length != NULL) {
        *out_span_length = 0U;
    }
    if (out_has_source_span != NULL) {
        *out_has_source_span = false;
    }
    if (instruction == NULL || source == NULL || line_text == NULL || instruction->source_line == 0U || line_text_length == 0U ||
        out_column == NULL || out_byte_offset == NULL || out_span_length == NULL || out_has_source_span == NULL ||
        !masm32_sim_wasm_find_line_start_offset(source, instruction->source_line, &line_start_offset)) {
        return;
    }

    {
        const char *source_line = source + line_start_offset;
        const char *source_line_end = strchr(source_line, '\n');
        const char *line_text_start = NULL;
        size_t line_text_offset = 0U;

        if (source_line_end == NULL) {
            source_line_end = source_line + strlen(source_line);
        }
        line_text_start = strstr(source_line, line_text);
        if (line_text_start != NULL && line_text_start <= source_line_end) {
            line_text_offset = (size_t)(line_text_start - source_line);
        }

        *out_column = (uint32_t)(line_text_offset + 1U);
        *out_byte_offset = line_start_offset + line_text_offset;
        *out_span_length = line_text_length;
        *out_has_source_span = true;
    }
}

/// Reads a raw shift count without mutating execution state.
///
/// @param vm VM whose CPU supplies CL when selected.
/// @param instruction Shift instruction to inspect.
/// @param out_raw_count Receives the unsigned raw count.
/// @return true when the count source is valid and readable.
static bool masm32_sim_wasm_read_shift_count(const Vm *vm, const VmIrInstruction *instruction, uint8_t *out_raw_count) {
    uint32_t value = 0U;

    if (vm == NULL || instruction == NULL || out_raw_count == NULL) {
        return false;
    }

    if (instruction->source.kind == VM_IR_OPERAND_IMMEDIATE && instruction->source.immediate <= 255U) {
        *out_raw_count = (uint8_t)instruction->source.immediate;
        return true;
    }

    if (instruction->source.kind == VM_IR_OPERAND_REGISTER && instruction->source.reg == VM_REGISTER_CL) {
        if (!vm_cpu_read_register(&vm->cpu, VM_REGISTER_CL, &value)) {
            return false;
        }
        *out_raw_count = (uint8_t)(value & 0xFFU);
        return true;
    }

    return false;
}

/// Fills an undefined shift/rotate modeled-flag diagnostic.
///
/// @param diagnostic Diagnostic to populate.
/// @param instruction Instruction associated with the diagnostic.
/// @param storage Source-run storage carrying original source text.
/// @param width_bits Destination width in bits.
/// @param raw_count Raw shift/rotate count before masking.
/// @param effective_count Effective count after raw_count & 31.
static void masm32_sim_wasm_fill_shift_diagnostic(
    Masm32SimWasmShiftDiagnostic *diagnostic,
    const VmIrInstruction *instruction,
    const Masm32SimWasmRunStorage *storage,
    uint8_t width_bits,
    uint8_t raw_count,
    uint8_t effective_count
) {
    if (diagnostic == NULL) {
        return;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    if (instruction != NULL && instruction->opcode == VM_IR_OPCODE_SAL) {
        diagnostic->mnemonic = "SAL";
    } else if (instruction != NULL && instruction->opcode == VM_IR_OPCODE_SHR) {
        diagnostic->mnemonic = "SHR";
    } else if (instruction != NULL && instruction->opcode == VM_IR_OPCODE_SAR) {
        diagnostic->mnemonic = "SAR";
    } else if (instruction != NULL && instruction->opcode == VM_IR_OPCODE_ROL) {
        diagnostic->mnemonic = "ROL";
    } else if (instruction != NULL && instruction->opcode == VM_IR_OPCODE_ROR) {
        diagnostic->mnemonic = "ROR";
    } else {
        diagnostic->mnemonic = "SHL";
    }
    diagnostic->width_bits = width_bits;
    diagnostic->raw_count = raw_count;
    diagnostic->effective_count = effective_count;
    diagnostic->source_line = instruction != NULL ? instruction->source_line : 0U;
    masm32_sim_wasm_copy_instruction_source_span(
        instruction,
        storage != NULL ? storage->source_text : NULL,
        &diagnostic->source_column,
        &diagnostic->source_byte_offset,
        &diagnostic->source_span_length,
        &diagnostic->has_source_span
    );
}

/// Determines whether a shift instruction makes modeled flags undefined.
///
/// @param storage Source-run storage used to populate diagnostics.
/// @param vm VM positioned before the instruction is stepped.
/// @param instruction Shift instruction to inspect.
/// @param out_diagnostic Optional diagnostic populated when the shift is undefined.
/// @return true when the instruction has an effective count that makes modeled flags undefined.
static bool masm32_sim_wasm_shift_has_undefined_modeled_flags(
    const Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    const VmIrInstruction *instruction,
    Masm32SimWasmShiftDiagnostic *out_diagnostic
) {
    uint8_t width_bits = 0U;
    uint8_t raw_count = 0U;
    uint8_t effective_count = 0U;

    if (vm == NULL || instruction == NULL || (instruction->opcode != VM_IR_OPCODE_SHL && instruction->opcode != VM_IR_OPCODE_SAL && instruction->opcode != VM_IR_OPCODE_SHR && instruction->opcode != VM_IR_OPCODE_SAR)) {
        return false;
    }

    if (!masm32_sim_wasm_operand_width(&instruction->destination, &width_bits) || !masm32_sim_wasm_read_shift_count(vm, instruction, &raw_count)) {
        return false;
    }

    effective_count = (uint8_t)(raw_count & 31U);
    if (effective_count <= 1U) {
        return false;
    }

    masm32_sim_wasm_fill_shift_diagnostic(out_diagnostic, instruction, storage, width_bits, raw_count, effective_count);
    return true;
}

/// Determines whether a rotate instruction makes OF undefined in the modeled flag set.
///
/// @param storage Source-run storage used to populate diagnostics.
/// @param vm VM positioned before the instruction is stepped.
/// @param instruction Rotate instruction to inspect.
/// @param out_diagnostic Optional diagnostic populated when OF is undefined.
/// @return true when the rotate instruction has a non-one nonzero effective count.
static bool masm32_sim_wasm_rotate_has_undefined_modeled_flags(
    const Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    const VmIrInstruction *instruction,
    Masm32SimWasmShiftDiagnostic *out_diagnostic
) {
    uint8_t width_bits = 0U;
    uint8_t raw_count = 0U;
    uint8_t effective_count = 0U;

    if (vm == NULL || instruction == NULL || (instruction->opcode != VM_IR_OPCODE_ROL && instruction->opcode != VM_IR_OPCODE_ROR)) {
        return false;
    }

    if (!masm32_sim_wasm_operand_width(&instruction->destination, &width_bits) || !masm32_sim_wasm_read_shift_count(vm, instruction, &raw_count)) {
        return false;
    }

    effective_count = (uint8_t)(raw_count & 31U);
    if (effective_count == 0U || effective_count == 1U) {
        return false;
    }

    masm32_sim_wasm_fill_shift_diagnostic(out_diagnostic, instruction, storage, width_bits, raw_count, effective_count);
    return true;
}

/// Counts bytes in a planned read that are still marked uninitialized-origin.
///
/// @param storage Source-run storage containing the initialization mask.
/// @param address Effective simulated read address.
/// @param size_bytes Number of bytes in the read range.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return Number of bytes in the read range that remain uninitialized-origin.
static uint32_t masm32_sim_wasm_count_uninitialized_read_bytes(
    const Masm32SimWasmRunStorage *storage,
    uint32_t address,
    uint32_t size_bytes,
    const VmLayoutPolicy *layout_policy
) {
    VmLayoutPolicy default_policy;
    const VmLayoutPolicy *effective_policy = layout_policy;
    const VmLayoutRegionPolicy *data_region = NULL;
    uint32_t offset = 0U;
    uint32_t index = 0U;

    if (storage == NULL || size_bytes == 0U) {
        return 0U;
    }
    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    data_region = &effective_policy->regions[VM_LAYOUT_REGION_DATA];
    if (address < data_region->base || address >= data_region->limit) {
        return 0U;
    }
    offset = address - data_region->base;
    if ((uint64_t)offset + (uint64_t)size_bytes > (uint64_t)storage->data_initialized_mask_size) {
        return 0U;
    }

    {
        uint32_t uninitialized_count = 0U;

        for (index = 0U; index < size_bytes; index += 1U) {
            if (storage->data_initialized_mask[(size_t)offset + (size_t)index] == MASM32_SIM_WASM_DATA_BYTE_UNINITIALIZED) {
                uninitialized_count += 1U;
            }
        }

        return uninitialized_count;
    }
}

/// Fills one uninitialized-read diagnostic from a planned read.
///
/// @param diagnostic Diagnostic to populate.
/// @param storage Source-run storage containing object metadata.
/// @param instruction Instruction associated with the planned read.
/// @param address Effective simulated read address.
/// @param size_bytes Number of bytes in the read range.
/// @param uninitialized_count Number of bytes in the range still uninitialized-origin.
static void masm32_sim_wasm_fill_uninitialized_read_diagnostic(
    Masm32SimWasmUninitializedReadDiagnostic *diagnostic,
    const Masm32SimWasmRunStorage *storage,
    const VmIrInstruction *instruction,
    uint32_t address,
    uint32_t size_bytes,
    uint32_t uninitialized_count
) {
    const VmObjectMapEntry *object = NULL;
    uint32_t end_address = address;

    if (diagnostic == NULL) {
        return;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    (void)vm_object_map_inclusive_end(address, size_bytes, &end_address);
    diagnostic->start_address = address;
    diagnostic->end_address = end_address;
    diagnostic->size_bytes = size_bytes;
    diagnostic->uninitialized_byte_count = uninitialized_count;
    diagnostic->initialized_byte_count = uninitialized_count <= size_bytes ? size_bytes - uninitialized_count : 0U;
    diagnostic->source_line = instruction != NULL ? instruction->source_line : 0U;

    if (storage != NULL) {
        object = vm_object_map_find_by_address(storage->object_map_entries, storage->object_map_entry_count, address);
    }
    if (object != NULL) {
        (void)snprintf(diagnostic->symbol_name, sizeof(diagnostic->symbol_name), "%s", object->symbol_name);
        diagnostic->has_symbol_name = true;
        diagnostic->symbol_byte_offset = address - object->base_address;
    }

    masm32_sim_wasm_copy_bracketed_memory_source_span(
        instruction,
        storage != NULL ? storage->source_text : NULL,
        &diagnostic->source_column,
        &diagnostic->source_byte_offset,
        &diagnostic->source_span_length,
        &diagnostic->has_source_span
    );
}

/// Validates one planned memory read against Phase 40 uninitialized-origin metadata.
///
/// Warning mode records a non-fatal diagnostic. Strict mode records the first
/// fatal diagnostic and asks the execution loop to stop before stepping the
/// instruction, preserving read-modify-write write-back state.
///
/// @param storage Source-run storage to mutate.
/// @param instruction Instruction associated with the planned read.
/// @param vm VM whose registers are used for effective-address calculation.
/// @param read Planned memory read to validate.
/// @param validation_mode Memory validation behavior selected for the run.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return true when execution may continue, false for strict uninitialized reads.
static bool masm32_sim_wasm_validate_uninitialized_read(
    Masm32SimWasmRunStorage *storage,
    const VmIrInstruction *instruction,
    const Vm *vm,
    const Masm32SimWasmPlannedMemoryRead *read,
    Masm32SimWasmMemoryValidationMode validation_mode,
    const VmLayoutPolicy *layout_policy
) {
    uint32_t address = 0U;
    uint32_t size_bytes = 0U;
    uint32_t uninitialized_count = 0U;

    if (storage == NULL || instruction == NULL || vm == NULL || read == NULL ||
        !masm32_sim_wasm_validation_checks_uninitialized_reads(validation_mode) ||
        read->width_bits == 0U || (read->width_bits % 8U) != 0U ||
        !masm32_sim_wasm_resolve_memory_operand_address(vm, &read->operand, &address)) {
        return true;
    }

    size_bytes = (uint32_t)(read->width_bits / 8U);
    uninitialized_count = masm32_sim_wasm_count_uninitialized_read_bytes(storage, address, size_bytes, layout_policy);
    if (uninitialized_count == 0U) {
        return true;
    }

    if (masm32_sim_wasm_validation_strict_uninitialized_reads(validation_mode)) {
        masm32_sim_wasm_fill_uninitialized_read_diagnostic(
            &storage->uninitialized_read_violation,
            storage,
            instruction,
            address,
            size_bytes,
            uninitialized_count
        );
        storage->has_uninitialized_read_violation = true;
        return false;
    }

    if (storage->uninitialized_read_warning_count < (size_t)MASM32_SIM_WASM_MAX_UNINITIALIZED_READ_WARNINGS) {
        masm32_sim_wasm_fill_uninitialized_read_diagnostic(
            &storage->uninitialized_read_warnings[storage->uninitialized_read_warning_count],
            storage,
            instruction,
            address,
            size_bytes,
            uninitialized_count
        );
        storage->uninitialized_read_warning_count += 1U;
    }

    return true;
}

/// Validates planned memory reads for the next instruction before stepping it.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose next instruction should be inspected.
/// @param validation_mode Memory validation behavior selected for the run.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return OK when execution may continue, or MEMORY_ERROR for strict failures.
static VmExecStatus masm32_sim_wasm_validate_uninitialized_reads_before_step(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    Masm32SimWasmMemoryValidationMode validation_mode,
    const VmLayoutPolicy *layout_policy
) {
    Masm32SimWasmPlannedMemoryRead reads[2];
    const VmIrInstruction *instruction = NULL;
    size_t read_count = 0U;
    size_t index = 0U;

    if (storage == NULL || vm == NULL || !masm32_sim_wasm_validation_checks_uninitialized_reads(validation_mode) ||
        vm->halted || vm->instruction_pointer >= vm->program_count || vm->program == NULL) {
        return VM_EXEC_STATUS_OK;
    }

    memset(reads, 0, sizeof(reads));
    instruction = &vm->program[vm->instruction_pointer];
    read_count = masm32_sim_wasm_collect_planned_reads(instruction, reads, sizeof(reads) / sizeof(reads[0]));
    for (index = 0U; index < read_count; index += 1U) {
        if (!masm32_sim_wasm_validate_uninitialized_read(storage, instruction, vm, &reads[index], validation_mode, layout_policy)) {
            return VM_EXEC_STATUS_MEMORY_ERROR;
        }
    }

    return VM_EXEC_STATUS_OK;
}

/// Validates shift undefined modeled-flag behavior before stepping.
///
/// Warning mode records a non-fatal message and allows execution. Strict mode
/// records a runtime diagnostic and stops before the instruction mutates state.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose next instruction should be inspected.
/// @param shift_mode Shift undefined-flag validation behavior.
/// @return OK when execution may continue, or INVALID_INSTRUCTION for strict failures.
static VmExecStatus masm32_sim_wasm_validate_shift_before_step(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    Masm32SimWasmShiftValidationMode shift_mode
) {
    Masm32SimWasmShiftDiagnostic diagnostic;
    const VmIrInstruction *instruction = NULL;

    if (storage == NULL || vm == NULL || vm->halted || vm->instruction_pointer >= vm->program_count || vm->program == NULL) {
        return VM_EXEC_STATUS_OK;
    }

    instruction = &vm->program[vm->instruction_pointer];
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (masm32_sim_wasm_shift_has_undefined_modeled_flags(storage, vm, instruction, &diagnostic)) {
        if (shift_mode == MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT) {
            storage->shift_violation = diagnostic;
            storage->has_shift_violation = true;
            return VM_EXEC_STATUS_INVALID_INSTRUCTION;
        }

        if (storage->shift_warning_count < (size_t)MASM32_SIM_WASM_MAX_SHIFT_WARNINGS) {
            storage->shift_warnings[storage->shift_warning_count] = diagnostic;
            storage->shift_warning_count += 1U;
        }
        return VM_EXEC_STATUS_OK;
    }

    if (masm32_sim_wasm_rotate_has_undefined_modeled_flags(storage, vm, instruction, &diagnostic) &&
        storage->shift_warning_count < (size_t)MASM32_SIM_WASM_MAX_SHIFT_WARNINGS) {
        storage->shift_warnings[storage->shift_warning_count] = diagnostic;
        storage->shift_warning_count += 1U;
    }

    return VM_EXEC_STATUS_OK;
}


/// Converts the Wasm-facing Phase 50B policy to the core helper policy.
///
/// @param policy Wasm-facing undefined-flag-use policy.
/// @param out_policy Receives the equivalent core policy.
/// @return true when @p policy is valid.
static bool masm32_sim_wasm_convert_flag_use_policy(
    Masm32SimWasmUndefinedFlagUsePolicy policy,
    VmUndefinedFlagUsePolicy *out_policy
);


/// Returns the modeled flags consumed by an already-implemented flag consumer.
///
/// @param instruction Instruction to inspect.
/// @param out_flags Receives consumed flags when non-NULL.
/// @param flag_capacity Number of entries available in @p out_flags.
/// @return Number of consumed flags required by the instruction.
static size_t masm32_sim_wasm_consumed_flags_for_instruction(
    const VmIrInstruction *instruction,
    VmFlag *out_flags,
    size_t flag_capacity
) {
    if (instruction == NULL) {
        return 0U;
    }

    switch (instruction->opcode) {
        case VM_IR_OPCODE_ADC:
        case VM_IR_OPCODE_SBB:
        case VM_IR_OPCODE_CMC:
            if (out_flags != NULL && flag_capacity > 0U) {
                out_flags[0] = VM_FLAG_CF;
            }
            return 1U;
        default:
            return 0U;
    }
}

/// Validates Phase 50B undefined-flag-use behavior before a consumer executes.
///
/// Warning mode records a non-fatal message and allows execution. Error mode
/// records a runtime diagnostic and stops before the instruction consumes flags.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose next instruction should be inspected.
/// @param policy Wasm-facing undefined-flag-use policy.
/// @return OK when execution may continue, or UNDEFINED_FLAG_USE for error failures.
static VmExecStatus masm32_sim_wasm_validate_flag_use_before_step(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    Masm32SimWasmUndefinedFlagUsePolicy policy
) {
    VmUndefinedFlagUsePolicy core_policy = VM_UNDEFINED_FLAG_USE_POLICY_OFF;
    VmFlag required_flags[VM_EXEC_MAX_CONSUMED_FLAGS];
    size_t required_flag_count = 0U;
    VmFlagUseDiagnostic diagnostic;
    const VmIrInstruction *instruction = NULL;
    VmExecStatus status = VM_EXEC_STATUS_OK;

    if (storage == NULL || vm == NULL || vm->halted || vm->instruction_pointer >= vm->program_count || vm->program == NULL) {
        return VM_EXEC_STATUS_OK;
    }
    if (!masm32_sim_wasm_convert_flag_use_policy(policy, &core_policy)) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (core_policy == VM_UNDEFINED_FLAG_USE_POLICY_OFF) {
        return VM_EXEC_STATUS_OK;
    }

    instruction = &vm->program[vm->instruction_pointer];
    memset(required_flags, 0, sizeof(required_flags));
    required_flag_count = masm32_sim_wasm_consumed_flags_for_instruction(instruction, required_flags, (size_t)VM_EXEC_MAX_CONSUMED_FLAGS);
    if (required_flag_count == 0U) {
        return VM_EXEC_STATUS_OK;
    }

    memset(&diagnostic, 0, sizeof(diagnostic));
    status = vm_check_flag_consumption(&vm->cpu, instruction, required_flags, required_flag_count, core_policy, &diagnostic);
    if (status == VM_EXEC_STATUS_UNDEFINED_FLAG_USE) {
        storage->flag_use_violation = diagnostic;
        storage->has_flag_use_violation = true;
        return status;
    }
    if (status != VM_EXEC_STATUS_OK) {
        return status;
    }
    if (diagnostic.invalid_flag_count > 0U && storage->flag_use_warning_count < (size_t)MASM32_SIM_WASM_MAX_FLAG_USE_WARNINGS) {
        storage->flag_use_warnings[storage->flag_use_warning_count] = diagnostic;
        storage->flag_use_warning_count += 1U;
    }

    return VM_EXEC_STATUS_OK;
}

/// Records one unaligned memory warning when capacity allows it.
///
/// @param storage Source-run storage to mutate.
/// @param instruction Instruction associated with the warning.
/// @param access Checked memory access that triggered the warning.
static void masm32_sim_wasm_collect_unaligned_warning_for_access(
    Masm32SimWasmRunStorage *storage,
    const VmIrInstruction *instruction,
    const VmExecMemoryAccess *access
) {
    Masm32SimWasmUnalignedWarning *warning = NULL;

    if (storage == NULL || instruction == NULL || access == NULL || storage->warning_count >= (size_t)MASM32_SIM_WASM_MAX_RUN_WARNINGS) {
        return;
    }

    if (!masm32_sim_wasm_access_is_unaligned(access)) {
        return;
    }

    warning = &storage->warnings[storage->warning_count];
    warning->address = access->address;
    warning->width_bits = access->width_bits;
    warning->source_line = instruction->source_line;
    storage->warning_count += 1U;
}

/// Collects unaligned memory warnings from one successful instruction.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose last delta should be inspected.
static void masm32_sim_wasm_collect_unaligned_warnings(Masm32SimWasmRunStorage *storage, const Vm *vm) {
    const VmExecDelta *delta = NULL;
    size_t index = 0U;

    if (storage == NULL || vm == NULL) {
        return;
    }

    delta = vm_last_delta(vm);
    if (delta == NULL || !delta->has_instruction) {
        return;
    }

    for (index = 0U; index < delta->memory_access_count; index += 1U) {
        masm32_sim_wasm_collect_unaligned_warning_for_access(storage, &delta->instruction, &delta->memory_accesses[index]);
    }
}

/// Appends collected simulator warnings to the simulatorMessages array.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing warnings.
/// @param inout_has_message Whether a prior message has already been appended.
/// @return true when warnings fit without overflowing the buffer.
static bool masm32_sim_json_append_warnings(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    bool *inout_has_message
) {
    size_t index = 0U;

    if (storage == NULL || inout_has_message == NULL) {
        return true;
    }

    for (index = 0U; index < storage->warning_count; index += 1U) {
        const Masm32SimWasmUnalignedWarning *warning = &storage->warnings[index];
        char message[128];
        (void)snprintf(
            message,
            sizeof(message),
            "Unaligned %s memory access at %08Xh.",
            masm32_sim_wasm_width_name(warning->width_bits),
            (unsigned int)warning->address
        );
        if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_message(writer, "simulator-warning", "unaligned-memory-access", message, warning->source_line, 0U)) {
            return false;
        }
        *inout_has_message = true;
    }

    return true;
}

/// Builds user-facing text for one allocated-object bounds diagnostic.
///
/// @param diagnostic Object-bounds diagnostic to describe.
/// @param buffer Destination buffer for the formatted message.
/// @param buffer_size Destination buffer size in bytes.
static void masm32_sim_wasm_format_object_bounds_message(
    const Masm32SimWasmObjectBoundsDiagnostic *diagnostic,
    char *buffer,
    size_t buffer_size
) {
    const char *access_name = diagnostic != NULL ? masm32_sim_wasm_exec_memory_access_kind_name(diagnostic->access_kind) : "access";
    const char *class_name = diagnostic != NULL ? vm_object_map_range_class_name(diagnostic->range_class) : NULL;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    if (class_name == NULL) {
        class_name = "unknown";
    }

    if (diagnostic == NULL) {
        (void)snprintf(buffer, buffer_size, "Memory access escaped declared object bounds.");
        return;
    }

    switch (diagnostic->range_class) {
        case VM_OBJECT_MAP_RANGE_CLASS_REGION_GAP:
            (void)snprintf(
                buffer,
                buffer_size,
                "Memory %s at %08Xh for %u byte%s is inside a valid region but outside any declared data object.",
                access_name,
                (unsigned int)diagnostic->start_address,
                (unsigned int)diagnostic->size_bytes,
                diagnostic->size_bytes == 1U ? "" : "s"
            );
            return;
        case VM_OBJECT_MAP_RANGE_CLASS_STARTS_IN_OBJECT:
            (void)snprintf(
                buffer,
                buffer_size,
                "Memory %s range %08Xh..%08Xh starts inside a declared data object and extends outside it (%s).",
                access_name,
                (unsigned int)diagnostic->start_address,
                (unsigned int)diagnostic->end_address,
                class_name
            );
            return;
        case VM_OBJECT_MAP_RANGE_CLASS_ENDS_IN_OBJECT:
            (void)snprintf(
                buffer,
                buffer_size,
                "Memory %s range %08Xh..%08Xh starts outside declared data objects and ends inside one (%s).",
                access_name,
                (unsigned int)diagnostic->start_address,
                (unsigned int)diagnostic->end_address,
                class_name
            );
            return;
        case VM_OBJECT_MAP_RANGE_CLASS_SPANS_OBJECTS:
            (void)snprintf(
                buffer,
                buffer_size,
                "Memory %s range %08Xh..%08Xh spans multiple declared data objects (%s).",
                access_name,
                (unsigned int)diagnostic->start_address,
                (unsigned int)diagnostic->end_address,
                class_name
            );
            return;
        default:
            (void)snprintf(
                buffer,
                buffer_size,
                "Memory %s range %08Xh..%08Xh crosses declared object bounds (%s).",
                access_name,
                (unsigned int)diagnostic->start_address,
                (unsigned int)diagnostic->end_address,
                class_name
            );
            return;
    }
}

/// Appends collected allocated-object warnings to the simulatorMessages array.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing object warnings.
/// @param inout_has_message Whether a prior message has already been appended.
/// @return true when warnings fit without overflowing the buffer.
static bool masm32_sim_json_append_object_warnings(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    bool *inout_has_message
) {
    size_t index = 0U;

    if (storage == NULL || inout_has_message == NULL) {
        return true;
    }

    for (index = 0U; index < storage->object_warning_count; index += 1U) {
        const Masm32SimWasmObjectBoundsDiagnostic *warning = &storage->object_warnings[index];
        char message[256];
        masm32_sim_wasm_format_object_bounds_message(warning, message, sizeof(message));
        if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_message(writer, "simulator-warning", "object-bounds-warning", message, warning->source_line, 0U)) {
            return false;
        }
        *inout_has_message = true;
    }

    return true;
}

/// Appends one allocated-object strict runtime diagnostic.
///
/// @param writer Writer to mutate.
/// @param diagnostic Strict object-bounds diagnostic to render.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_object_violation(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmObjectBoundsDiagnostic *diagnostic
) {
    char message[256];

    masm32_sim_wasm_format_object_bounds_message(diagnostic, message, sizeof(message));
    return masm32_sim_json_append_message_with_span(
        writer,
        "runtime-error",
        "object-bounds-violation",
        message,
        diagnostic != NULL ? diagnostic->source_line : 0U,
        diagnostic != NULL ? diagnostic->source_column : 0U,
        diagnostic != NULL ? diagnostic->source_byte_offset : 0U,
        diagnostic != NULL ? diagnostic->source_span_length : 0U,
        diagnostic != NULL && diagnostic->has_source_span
    );
}

/// Builds user-facing text for one uninitialized-read diagnostic.
///
/// @param diagnostic Uninitialized-read diagnostic to describe.
/// @param buffer Destination buffer for the formatted message.
/// @param buffer_size Destination buffer size in bytes.
static void masm32_sim_wasm_format_uninitialized_read_message(
    const Masm32SimWasmUninitializedReadDiagnostic *diagnostic,
    char *buffer,
    size_t buffer_size
) {
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    if (diagnostic == NULL) {
        (void)snprintf(buffer, buffer_size, "Read from uninitialized-origin storage.");
        return;
    }

    if (diagnostic->has_symbol_name) {
        (void)snprintf(
            buffer,
            buffer_size,
            "Memory read range %08Xh..%08Xh reads %u byte%s from %s + %u; %u of those byte%s still originated from uninitialized storage.",
            (unsigned int)diagnostic->start_address,
            (unsigned int)diagnostic->end_address,
            (unsigned int)diagnostic->size_bytes,
            diagnostic->size_bytes == 1U ? "" : "s",
            diagnostic->symbol_name,
            (unsigned int)diagnostic->symbol_byte_offset,
            (unsigned int)diagnostic->uninitialized_byte_count,
            diagnostic->uninitialized_byte_count == 1U ? "" : "s"
        );
        return;
    }

    (void)snprintf(
        buffer,
        buffer_size,
        "Memory read range %08Xh..%08Xh reads %u byte%s; %u of those byte%s still originated from uninitialized storage.",
        (unsigned int)diagnostic->start_address,
        (unsigned int)diagnostic->end_address,
        (unsigned int)diagnostic->size_bytes,
        diagnostic->size_bytes == 1U ? "" : "s",
        (unsigned int)diagnostic->uninitialized_byte_count,
        diagnostic->uninitialized_byte_count == 1U ? "" : "s"
    );
}

/// Appends one uninitialized-read message object with diagnostic metadata.
///
/// @param writer Writer to mutate.
/// @param kind Simulator-message kind to emit.
/// @param diagnostic Diagnostic to render.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_uninitialized_read_message(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const Masm32SimWasmUninitializedReadDiagnostic *diagnostic
) {
    char message[256];

    if (writer == NULL) {
        return false;
    }

    masm32_sim_wasm_format_uninitialized_read_message(diagnostic, message, sizeof(message));
    if (!masm32_sim_json_append(writer, "{\"kind\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, kind)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"code\":\"uninitialized-read\",\"message\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, message)) {
        return false;
    }
    if (diagnostic != NULL && diagnostic->source_line > 0U) {
        if (!masm32_sim_json_append(writer, ",\"line\":%u", (unsigned int)diagnostic->source_line)) {
            return false;
        }
    }
    if (diagnostic != NULL && diagnostic->source_column > 0U) {
        if (!masm32_sim_json_append(writer, ",\"column\":%u", (unsigned int)diagnostic->source_column)) {
            return false;
        }
    }
    if (diagnostic != NULL && diagnostic->has_source_span) {
        if (!masm32_sim_json_append(writer, ",\"byteOffset\":%llu", (unsigned long long)diagnostic->source_byte_offset)) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ",\"spanLength\":%llu", (unsigned long long)diagnostic->source_span_length)) {
            return false;
        }
    }
    if (diagnostic == NULL || diagnostic->source_line == 0U) {
        if (!masm32_sim_json_append(writer, ",\"sourceLocation\":null")) {
            return false;
        }
    } else {
        if (!masm32_sim_json_append(
                writer,
                ",\"sourceLocation\":{\"line\":%u,\"column\":",
                (unsigned int)diagnostic->source_line
            )) {
            return false;
        }
        if (diagnostic->source_column > 0U) {
            if (!masm32_sim_json_append(writer, "%u", (unsigned int)diagnostic->source_column)) {
                return false;
            }
        } else if (!masm32_sim_json_append(writer, "null")) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ",\"byteOffset\":")) {
            return false;
        }
        if (diagnostic->has_source_span) {
            if (!masm32_sim_json_append(writer, "%llu", (unsigned long long)diagnostic->source_byte_offset)) {
                return false;
            }
        } else if (!masm32_sim_json_append(writer, "null")) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ",\"spanLength\":")) {
            return false;
        }
        if (diagnostic->has_source_span) {
            if (!masm32_sim_json_append(writer, "%llu", (unsigned long long)diagnostic->source_span_length)) {
                return false;
            }
        } else if (!masm32_sim_json_append(writer, "null")) {
            return false;
        }
        if (!masm32_sim_json_append(writer, "}")) {
            return false;
        }
    }
    if (diagnostic != NULL) {
        if (!masm32_sim_json_append(writer, ",\"symbolName\":")) {
            return false;
        }
        if (diagnostic->has_symbol_name) {
            if (!masm32_sim_json_append_string(writer, diagnostic->symbol_name)) {
                return false;
            }
        } else if (!masm32_sim_json_append(writer, "null")) {
            return false;
        }
        if (!masm32_sim_json_append(
                writer,
                ",\"accessStartAddress\":\"%08Xh\",\"accessEndAddress\":\"%08Xh\",\"accessSizeBytes\":%u,\"uninitializedByteCount\":%u,\"initializedByteCount\":%u",
                (unsigned int)diagnostic->start_address,
                (unsigned int)diagnostic->end_address,
                (unsigned int)diagnostic->size_bytes,
                (unsigned int)diagnostic->uninitialized_byte_count,
                (unsigned int)diagnostic->initialized_byte_count
            )) {
            return false;
        }
        if (diagnostic->has_symbol_name) {
            if (!masm32_sim_json_append(writer, ",\"accessByteOffset\":%u", (unsigned int)diagnostic->symbol_byte_offset)) {
                return false;
            }
        } else if (!masm32_sim_json_append(writer, ",\"accessByteOffset\":null")) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
}

/// Appends collected uninitialized-read warnings to the simulatorMessages array.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing uninitialized-read warnings.
/// @param inout_has_message Whether a prior message has already been appended.
/// @return true when warnings fit without overflowing the buffer.
static bool masm32_sim_json_append_uninitialized_read_warnings(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    bool *inout_has_message
) {
    size_t index = 0U;

    if (storage == NULL || inout_has_message == NULL) {
        return true;
    }

    for (index = 0U; index < storage->uninitialized_read_warning_count; index += 1U) {
        if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_uninitialized_read_message(
                writer,
                "simulator-warning",
                &storage->uninitialized_read_warnings[index]
            )) {
            return false;
        }
        *inout_has_message = true;
    }

    return true;
}

/// Returns the English article for a shift destination width phrase.
///
/// @param width_bits Destination width in bits.
/// @return "an" for 8-bit destinations, otherwise "a".
static const char *masm32_sim_wasm_shift_width_article(uint8_t width_bits) {
    return width_bits == 8U ? "an" : "a";
}

/// Formats one undefined shift/rotate modeled-flag diagnostic message.
///
/// @param diagnostic Diagnostic to format.
/// @param buffer Destination buffer.
/// @param buffer_size Destination buffer size in bytes.
static void masm32_sim_wasm_format_shift_diagnostic(
    const Masm32SimWasmShiftDiagnostic *diagnostic,
    char *buffer,
    size_t buffer_size
) {
    const char *mnemonic = "SHL";
    const char *article = "a";

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    if (diagnostic == NULL) {
        (void)snprintf(buffer, buffer_size, "Shift or rotate count makes modeled flags undefined.");
        return;
    }

    mnemonic = diagnostic->mnemonic != NULL ? diagnostic->mnemonic : "SHL";
    article = masm32_sim_wasm_shift_width_article(diagnostic->width_bits);

    if (strcmp(mnemonic, "ROL") == 0 || strcmp(mnemonic, "ROR") == 0) {
        const char *cf_source = strcmp(mnemonic, "ROR") == 0 ? "most significant" : "least significant";
        const uint8_t rotate_amount = diagnostic->width_bits != 0U ? (uint8_t)(diagnostic->effective_count % diagnostic->width_bits) : 0U;
        (void)snprintf(
            buffer,
            buffer_size,
            "%s count %u has effective count %u and rotate amount %u for %s %u-bit destination. CF was updated from the %s bit of the rotated result. ZF and SF were preserved because %s does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.",
            mnemonic,
            (unsigned int)diagnostic->raw_count,
            (unsigned int)diagnostic->effective_count,
            (unsigned int)rotate_amount,
            article,
            (unsigned int)diagnostic->width_bits,
            cf_source,
            mnemonic
        );
        return;
    }

    if (diagnostic->effective_count >= diagnostic->width_bits) {
        (void)snprintf(
            buffer,
            buffer_size,
            "%s count %u has effective count %u for %s %u-bit destination. ZF and SF were updated from the result. CF is architecturally undefined because the effective count is greater than or equal to the destination width. OF is architecturally undefined because the effective count is not 1. The simulator preserved CF and OF deterministically.",
            mnemonic,
            (unsigned int)diagnostic->raw_count,
            (unsigned int)diagnostic->effective_count,
            article,
            (unsigned int)diagnostic->width_bits
        );
        return;
    }

    (void)snprintf(
        buffer,
        buffer_size,
        "%s count %u has effective count %u for %s %u-bit destination. CF, ZF, and SF were updated from the result. OF is architecturally undefined because the effective count is greater than 1. The simulator preserved OF deterministically.",
        mnemonic,
        (unsigned int)diagnostic->raw_count,
        (unsigned int)diagnostic->effective_count,
        article,
        (unsigned int)diagnostic->width_bits
    );
}

/// Appends one undefined shift/rotate modeled-flag message object.
///
/// @param writer Writer to mutate.
/// @param kind Simulator-message kind to emit.
/// @param diagnostic Diagnostic to render.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_shift_message(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const Masm32SimWasmShiftDiagnostic *diagnostic
) {
    char message[512];

    if (writer == NULL) {
        return false;
    }

    masm32_sim_wasm_format_shift_diagnostic(diagnostic, message, sizeof(message));
    return masm32_sim_json_append_message_with_span(
        writer,
        kind,
        diagnostic != NULL && diagnostic->mnemonic != NULL && (strcmp(diagnostic->mnemonic, "ROL") == 0 || strcmp(diagnostic->mnemonic, "ROR") == 0) ? "undefined-modeled-flag" : "undefined-shift-flag",
        message,
        diagnostic != NULL ? diagnostic->source_line : 0U,
        diagnostic != NULL ? diagnostic->source_column : 0U,
        diagnostic != NULL ? diagnostic->source_byte_offset : 0U,
        diagnostic != NULL ? diagnostic->source_span_length : 0U,
        diagnostic != NULL && diagnostic->has_source_span
    );
}

/// Appends collected undefined shift/rotate modeled-flag warnings to Simulator Messages.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing shift/rotate warnings.
/// @param inout_has_message Whether a prior message has already been appended.
/// @return true when warnings fit without overflowing the buffer.
static bool masm32_sim_json_append_shift_warnings(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    bool *inout_has_message
) {
    size_t index = 0U;

    if (storage == NULL || inout_has_message == NULL) {
        return true;
    }

    for (index = 0U; index < storage->shift_warning_count; index += 1U) {
        if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_shift_message(writer, "simulator-warning", &storage->shift_warnings[index])) {
            return false;
        }
        *inout_has_message = true;
    }

    return true;
}

/// Appends one strict undefined shift-flag runtime diagnostic.
///
/// @param writer Writer to mutate.
/// @param diagnostic Strict shift diagnostic to render.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_shift_violation(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmShiftDiagnostic *diagnostic
) {
    return masm32_sim_json_append_shift_message(writer, "runtime-error", diagnostic);
}


/// Converts the Wasm-facing Phase 50B policy to the core helper policy.
///
/// @param policy Wasm-facing undefined-flag-use policy.
/// @param out_policy Receives the equivalent core policy.
/// @return true when @p policy is valid.
static bool masm32_sim_wasm_convert_flag_use_policy(
    Masm32SimWasmUndefinedFlagUsePolicy policy,
    VmUndefinedFlagUsePolicy *out_policy
) {
    if (out_policy == NULL) {
        return false;
    }

    switch (policy) {
        case MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF:
            *out_policy = VM_UNDEFINED_FLAG_USE_POLICY_OFF;
            return true;
        case MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN:
            *out_policy = VM_UNDEFINED_FLAG_USE_POLICY_WARN;
            return true;
        case MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR:
            *out_policy = VM_UNDEFINED_FLAG_USE_POLICY_ERROR;
            return true;
        default:
            return false;
    }
}

/// Returns an uppercase display mnemonic for diagnostics.
///
/// @param mnemonic Lowercase or mixed-case mnemonic from IR metadata.
/// @return Static uppercase display mnemonic when known, otherwise @p mnemonic or "instruction".
static const char *masm32_sim_wasm_display_mnemonic(const char *mnemonic) {
    if (mnemonic == NULL) {
        return "instruction";
    }
    if (strcmp(mnemonic, "adc") == 0) {
        return "ADC";
    }
    if (strcmp(mnemonic, "sbb") == 0) {
        return "SBB";
    }
    if (strcmp(mnemonic, "cmc") == 0) {
        return "CMC";
    }
    if (strcmp(mnemonic, "shl") == 0) {
        return "SHL";
    }
    if (strcmp(mnemonic, "sal") == 0) {
        return "SAL";
    }
    if (strcmp(mnemonic, "shr") == 0) {
        return "SHR";
    }
    if (strcmp(mnemonic, "sar") == 0) {
        return "SAR";
    }
    if (strcmp(mnemonic, "rol") == 0) {
        return "ROL";
    }
    if (strcmp(mnemonic, "ror") == 0) {
        return "ROR";
    }

    return mnemonic;
}

/// Appends a comma-separated consumed flag list into a fixed string buffer.
///
/// @param diagnostic Flag-use diagnostic to inspect.
/// @param buffer Destination buffer.
/// @param buffer_size Destination buffer size in bytes.
static void masm32_sim_wasm_format_flag_use_list(
    const VmFlagUseDiagnostic *diagnostic,
    char *buffer,
    size_t buffer_size
) {
    size_t index = 0U;
    size_t length = 0U;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    buffer[0] = '\0';

    if (diagnostic == NULL || diagnostic->invalid_flag_count == 0U) {
        (void)snprintf(buffer, buffer_size, "flag");
        return;
    }

    for (index = 0U; index < diagnostic->invalid_flag_count; index += 1U) {
        const char *name = vm_cpu_flag_name(diagnostic->invalid_flags[index]);
        int written = 0;

        if (name == NULL) {
            name = "flag";
        }
        written = snprintf(
            buffer + length,
            length < buffer_size ? buffer_size - length : 0U,
            "%s%s",
            index == 0U ? "" : ", ",
            name
        );
        if (written < 0) {
            buffer[0] = '\0';
            return;
        }
        length += (size_t)written;
        if (length >= buffer_size) {
            buffer[buffer_size - 1U] = '\0';
            return;
        }
    }
}

/// Formats one undefined flag-use diagnostic message.
///
/// @param diagnostic Diagnostic to format.
/// @param is_error Whether this is a runtime-error message.
/// @param buffer Destination buffer.
/// @param buffer_size Destination buffer size in bytes.
static void masm32_sim_wasm_format_flag_use_diagnostic(
    const VmFlagUseDiagnostic *diagnostic,
    bool is_error,
    char *buffer,
    size_t buffer_size
) {
    char flag_list[64];
    const char *consumer_mnemonic = "instruction";
    const VmFlagValidityMetadata *metadata = NULL;
    const char *producer_mnemonic = NULL;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    masm32_sim_wasm_format_flag_use_list(diagnostic, flag_list, sizeof(flag_list));
    if (diagnostic != NULL && diagnostic->has_consumer_instruction) {
        consumer_mnemonic = masm32_sim_wasm_display_mnemonic(vm_ir_opcode_name(diagnostic->consumer_instruction.opcode));
    }

    if (diagnostic != NULL && diagnostic->invalid_flag_count > 0U) {
        metadata = &diagnostic->invalid_metadata[0];
    }
    producer_mnemonic = metadata != NULL ? masm32_sim_wasm_display_mnemonic(metadata->producer_mnemonic) : NULL;

    if (metadata != NULL && producer_mnemonic != NULL && metadata->producer_source_line > 0U) {
        (void)snprintf(
            buffer,
            buffer_size,
            "%s reads %s, but %s %s architecturally undefined from %s at line %u. %s",
            consumer_mnemonic,
            flag_list,
            flag_list,
            diagnostic->invalid_flag_count == 1U ? "is" : "are",
            producer_mnemonic,
            (unsigned int)metadata->producer_source_line,
            is_error ? "Execution stopped before using the undefined flag." : "The simulator preserved the flag deterministically; this flag-dependent behavior is not portable."
        );
        return;
    }

    (void)snprintf(
        buffer,
        buffer_size,
        "%s reads %s, but %s %s currently marked architecturally undefined. %s",
        consumer_mnemonic,
        flag_list,
        flag_list,
        diagnostic != NULL && diagnostic->invalid_flag_count == 1U ? "is" : "are",
        is_error ? "Execution stopped before using the deterministic simulator fallback." : "Execution continued using the deterministic simulator fallback."
    );
}

/// Appends a JSON array of invalid consumed flag names.
///
/// @param writer Writer to mutate.
/// @param diagnostic Diagnostic containing invalid flags.
/// @return true when the array fit without overflowing the buffer.
static bool masm32_sim_json_append_flag_use_flag_array(
    Masm32SimJsonWriter *writer,
    const VmFlagUseDiagnostic *diagnostic
) {
    size_t index = 0U;

    if (writer == NULL || !masm32_sim_json_append(writer, "[")) {
        return false;
    }
    if (diagnostic != NULL) {
        for (index = 0U; index < diagnostic->invalid_flag_count; index += 1U) {
            const char *name = vm_cpu_flag_name(diagnostic->invalid_flags[index]);
            if (index > 0U && !masm32_sim_json_append(writer, ",")) {
                return false;
            }
            if (!masm32_sim_json_append_string(writer, name != NULL ? name : "flag")) {
                return false;
            }
        }
    }

    return masm32_sim_json_append(writer, "]");
}

/// Appends one undefined flag-use diagnostic as a structured Simulator Message.
///
/// @param writer Writer to mutate.
/// @param kind Message category.
/// @param diagnostic Diagnostic to append.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_flag_use_message(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const VmFlagUseDiagnostic *diagnostic
) {
    char message[512];
    uint32_t column = 0U;
    size_t byte_offset = 0U;
    size_t span_length = 0U;
    bool has_source_span = false;
    const VmFlagValidityMetadata *metadata = NULL;

    if (writer == NULL) {
        return false;
    }

    masm32_sim_wasm_format_flag_use_diagnostic(diagnostic, kind != NULL && strcmp(kind, "runtime-error") == 0, message, sizeof(message));
    if (diagnostic != NULL && diagnostic->has_consumer_instruction) {
        masm32_sim_wasm_copy_instruction_source_span(
            &diagnostic->consumer_instruction,
            g_masm32_sim_wasm_run_storage.source_text,
            &column,
            &byte_offset,
            &span_length,
            &has_source_span
        );
    }
    if (diagnostic != NULL && diagnostic->invalid_flag_count > 0U) {
        metadata = &diagnostic->invalid_metadata[0];
    }

    if (!masm32_sim_json_append(writer, "{\"kind\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, kind)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"code\":\"undefined-flag-use\",\"message\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, message)) {
        return false;
    }
    if (diagnostic != NULL && diagnostic->has_consumer_instruction && diagnostic->consumer_instruction.source_line > 0U) {
        if (!masm32_sim_json_append(writer, ",\"line\":%u", (unsigned int)diagnostic->consumer_instruction.source_line)) {
            return false;
        }
    }
    if (column > 0U && !masm32_sim_json_append(writer, ",\"column\":%u", (unsigned int)column)) {
        return false;
    }
    if (has_source_span) {
        if (!masm32_sim_json_append(writer, ",\"byteOffset\":%llu,\"spanLength\":%llu", (unsigned long long)byte_offset, (unsigned long long)span_length)) {
            return false;
        }
    }
    if (!masm32_sim_json_append(writer, ",\"consumedFlags\":")) {
        return false;
    }
    if (!masm32_sim_json_append_flag_use_flag_array(writer, diagnostic)) {
        return false;
    }
    if (metadata != NULL) {
        if (!masm32_sim_json_append(writer, ",\"producerMnemonic\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, metadata->producer_mnemonic != NULL ? masm32_sim_wasm_display_mnemonic(metadata->producer_mnemonic) : "instruction")) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ",\"producerCode\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, metadata->undefined_code != NULL ? metadata->undefined_code : "undefined-modeled-flag")) {
            return false;
        }
        if (metadata->producer_source_line > 0U && !masm32_sim_json_append(writer, ",\"producerLine\":%u", (unsigned int)metadata->producer_source_line)) {
            return false;
        }
        if (metadata->producer_source_column > 0U && !masm32_sim_json_append(writer, ",\"producerColumn\":%u", (unsigned int)metadata->producer_source_column)) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
}

/// Appends collected undefined flag-use warnings to Simulator Messages.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing warnings.
/// @param inout_has_message Whether a prior message has already been appended.
/// @return true when warnings fit without overflowing the buffer.
static bool masm32_sim_json_append_flag_use_warnings(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    bool *inout_has_message
) {
    size_t index = 0U;

    if (storage == NULL || inout_has_message == NULL) {
        return true;
    }

    for (index = 0U; index < storage->flag_use_warning_count; index += 1U) {
        if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_flag_use_message(writer, "simulator-warning", &storage->flag_use_warnings[index])) {
            return false;
        }
        *inout_has_message = true;
    }

    return true;
}

/// Appends one undefined flag-use runtime error.
///
/// @param writer Writer to mutate.
/// @param diagnostic Diagnostic to append.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_flag_use_violation(
    Masm32SimJsonWriter *writer,
    const VmFlagUseDiagnostic *diagnostic
) {
    return masm32_sim_json_append_flag_use_message(writer, "runtime-error", diagnostic);
}

/// Appends one uninitialized-read strict runtime diagnostic.
///
/// @param writer Writer to mutate.
/// @param diagnostic Strict uninitialized-read diagnostic to render.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_uninitialized_read_violation(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmUninitializedReadDiagnostic *diagnostic
) {
    return masm32_sim_json_append_uninitialized_read_message(writer, "runtime-error", diagnostic);
}

/// Returns the simulator-message category for one parser diagnostic.
///
/// @param diagnostic Parser diagnostic to classify.
/// @return Stable simulator-message kind string.
static const char *masm32_sim_wasm_parser_diagnostic_kind(const VmParserDiagnostic *diagnostic) {
    VmParserDiagnosticCode code = diagnostic != NULL ? diagnostic->code : VM_PARSER_DIAGNOSTIC_NONE;

    if (diagnostic != NULL && diagnostic->severity == VM_PARSER_DIAGNOSTIC_SEVERITY_WARNING) {
        return "assembly-warning";
    }

    if (code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE) {
        return "unsupported-feature";
    }

    return "assembly-error";
}

/// Appends parser diagnostics to the simulatorMessages array.
///
/// @param writer Writer to mutate.
/// @param diagnostics Parser diagnostic array.
/// @param diagnostic_count Number of parser diagnostics available.
/// @return true when diagnostics fit without overflowing the buffer.
static bool masm32_sim_json_append_parser_messages(
    Masm32SimJsonWriter *writer,
    const VmParserDiagnostic *diagnostics,
    size_t diagnostic_count
) {
    size_t index = 0U;

    if (diagnostics == NULL || diagnostic_count == 0U) {
        return masm32_sim_json_append_message(writer, "assembly-error", "parse-failed", "Source could not be parsed.", 0U, 0U);
    }

    for (index = 0U; index < diagnostic_count; index += 1U) {
        const VmParserDiagnostic *diagnostic = &diagnostics[index];
        const char *code_name = vm_parser_diagnostic_code_name(diagnostic->code);
        if (index > 0U && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_message_with_span(
                writer,
                masm32_sim_wasm_parser_diagnostic_kind(diagnostic),
                code_name != NULL ? code_name : "parser-diagnostic",
                diagnostic->message != NULL ? diagnostic->message : "Parser diagnostic.",
                diagnostic->location.line,
                diagnostic->location.column,
                diagnostic->location.offset,
                diagnostic->lexeme_length,
                diagnostic->location.line > 0U
            )) {
            return false;
        }
    }

    return true;
}

/// Returns whether a parser diagnostic array contains any execution-blocking error.
///
/// @param diagnostics Diagnostics to inspect.
/// @param diagnostic_count Number of diagnostics available.
/// @return true when at least one parser diagnostic has error severity.
static bool masm32_sim_wasm_parser_diagnostics_have_errors(const VmParserDiagnostic *diagnostics, size_t diagnostic_count) {
    size_t index = 0U;

    if (diagnostics == NULL) {
        return false;
    }

    for (index = 0U; index < diagnostic_count; index += 1U) {
        if (vm_parser_diagnostic_is_error(&diagnostics[index])) {
            return true;
        }
    }

    return false;
}

/// Returns a stable lowercase name for a memory access type.
///
/// @param access_type Memory access type to describe.
/// @return Static lowercase access-type name.
static const char *masm32_sim_wasm_memory_access_name(VmMemoryAccessType access_type) {
    switch (access_type) {
        case VM_MEMORY_ACCESS_READ:
            return "read";
        case VM_MEMORY_ACCESS_WRITE:
            return "write";
        case VM_MEMORY_ACCESS_EXECUTE:
            return "execute";
        default:
            return "access";
    }
}

/// Builds a user-facing memory error summary from structured executor diagnostics.
///
/// @param diagnostic Executor diagnostic containing memory failure context.
/// @param buffer Destination buffer for the formatted message.
/// @param buffer_size Number of bytes available in @p buffer.
/// @return Stable diagnostic code to use for the simulator message.
static const char *masm32_sim_wasm_format_memory_error_message(
    const VmExecDiagnostic *diagnostic,
    char *buffer,
    size_t buffer_size
) {
    const char *memory_status_name = "memory-error";
    const char *access_name = "access";
    const VmMemoryDiagnostic *memory_diagnostic = NULL;

    if (buffer != NULL && buffer_size > 0U) {
        buffer[0] = '\0';
    }

    if (diagnostic == NULL || diagnostic->status != VM_EXEC_STATUS_MEMORY_ERROR) {
        if (buffer != NULL && buffer_size > 0U) {
            (void)snprintf(buffer, buffer_size, "Execution failed during a simulated memory access.");
        }
        return "memory-error";
    }

    memory_status_name = vm_memory_status_name(diagnostic->memory_status);
    if (memory_status_name == NULL) {
        memory_status_name = "memory-error";
    }

    memory_diagnostic = &diagnostic->memory_diagnostic;
    access_name = masm32_sim_wasm_memory_access_name(memory_diagnostic->access_type);

    if (diagnostic->memory_status == VM_MEMORY_STATUS_INVALID_ADDRESS) {
        if (buffer != NULL && buffer_size > 0U) {
            (void)snprintf(
                buffer,
                buffer_size,
                "Invalid memory %s at %08Xh for %u byte%s. The address is outside the simulator's configured memory regions.",
                access_name,
                (unsigned int)memory_diagnostic->address,
                (unsigned int)memory_diagnostic->size,
                memory_diagnostic->size == 1U ? "" : "s"
            );
        }
        return memory_status_name;
    }

    if (diagnostic->memory_status == VM_MEMORY_STATUS_PERMISSION_DENIED) {
        const char *region_name = memory_diagnostic->has_region ? vm_memory_region_name(memory_diagnostic->region) : NULL;
        if (buffer != NULL && buffer_size > 0U) {
            (void)snprintf(
                buffer,
                buffer_size,
                "Memory %s at %08Xh for %u byte%s is not permitted%s%s.",
                access_name,
                (unsigned int)memory_diagnostic->address,
                (unsigned int)memory_diagnostic->size,
                memory_diagnostic->size == 1U ? "" : "s",
                region_name != NULL ? " in " : "",
                region_name != NULL ? region_name : ""
            );
        }
        return memory_status_name;
    }

    if (buffer != NULL && buffer_size > 0U) {
        (void)snprintf(
            buffer,
            buffer_size,
            "Memory %s failed at %08Xh for %u byte%s.",
            access_name,
            (unsigned int)memory_diagnostic->address,
            (unsigned int)memory_diagnostic->size,
            memory_diagnostic->size == 1U ? "" : "s"
        );
    }

    return memory_status_name;
}

/// Appends an executor diagnostic to the simulatorMessages array.
///
/// @param writer Writer to mutate.
/// @param diagnostic Executor diagnostic to render.
/// @param status Status associated with the diagnostic.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_exec_message(Masm32SimJsonWriter *writer, const VmExecDiagnostic *diagnostic, VmExecStatus status) {
    const char *status_name = vm_exec_status_name(status);
    const char *message_code = status_name != NULL ? status_name : "execution-error";
    const char *message_text = "Execution failed while running the parsed program.";
    char memory_message[256];
    uint32_t line = 0U;

    if (diagnostic != NULL && diagnostic->has_instruction) {
        line = diagnostic->instruction.source_line;
    }

    if (diagnostic != NULL && diagnostic->status == VM_EXEC_STATUS_MEMORY_ERROR) {
        message_code = masm32_sim_wasm_format_memory_error_message(diagnostic, memory_message, sizeof(memory_message));
        message_text = memory_message;
    }

    return masm32_sim_json_append_message(
        writer,
        "runtime-error",
        message_code,
        message_text,
        line,
        0U
    );
}

/// Appends one layout region object to the source-run JSON response.
///
/// @param writer Writer to mutate.
/// @param name Stable region name.
/// @param region Region policy to serialize.
/// @return true when the region fit without overflowing the buffer.
static bool masm32_sim_json_append_layout_region(Masm32SimJsonWriter *writer, const char *name, const VmLayoutRegionPolicy *region) {
    uint32_t size = 0U;

    if (writer == NULL || region == NULL || !vm_layout_region_size(region, &size)) {
        return false;
    }

    if (!masm32_sim_json_append(writer, "\"") || !masm32_sim_json_append(writer, "%s", name) || !masm32_sim_json_append(writer, "\":{\"base\":%u,\"limit\":%u,\"size\":%u}", (unsigned int)region->base, (unsigned int)region->limit, (unsigned int)size)) {
        return false;
    }

    return true;
}

/// Appends optional randomized layout metadata to the source-run JSON response.
///
/// @param writer Writer to mutate.
/// @param layout_policy Selected randomized layout policy, or NULL to omit metadata.
/// @return true when metadata was omitted or appended successfully.
static bool masm32_sim_json_append_layout_metadata(Masm32SimJsonWriter *writer, const VmLayoutPolicy *layout_policy) {
    const char *mode_name = NULL;

    if (writer == NULL) {
        return false;
    }
    if (layout_policy == NULL) {
        return true;
    }

    mode_name = vm_layout_mode_name(layout_policy->mode);
    if (mode_name == NULL) {
        mode_name = "unknown";
    }

    if (!masm32_sim_json_append(writer, "\"layout\":{\"mode\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, mode_name)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"seed\":%u,\"hasSeed\":%s,\"regions\":{", (unsigned int)layout_policy->random_seed, layout_policy->has_random_seed ? "true" : "false")) {
        return false;
    }
    if (!masm32_sim_json_append_layout_region(writer, "code", &layout_policy->regions[VM_LAYOUT_REGION_CODE]) || !masm32_sim_json_append(writer, ",") ||
        !masm32_sim_json_append_layout_region(writer, "data", &layout_policy->regions[VM_LAYOUT_REGION_DATA]) || !masm32_sim_json_append(writer, ",") ||
        !masm32_sim_json_append_layout_region(writer, "const", &layout_policy->regions[VM_LAYOUT_REGION_CONST]) || !masm32_sim_json_append(writer, ",") ||
        !masm32_sim_json_append_layout_region(writer, "heap", &layout_policy->regions[VM_LAYOUT_REGION_HEAP]) || !masm32_sim_json_append(writer, ",") ||
        !masm32_sim_json_append_layout_region(writer, "stack", &layout_policy->regions[VM_LAYOUT_REGION_STACK])) {
        return false;
    }

    return masm32_sim_json_append(writer, "}},");
}


/// Appends a compact test-only view of per-object initialized-byte masks.
///
/// This metadata is intentionally omitted from the normal browser source-run
/// export. It exists so native tests can inspect Phase 39 uninitialized-origin
/// tracking without adding uninitialized-read diagnostics.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing object entries and byte masks.
/// @return true when the metadata was omitted or appended successfully.
static bool masm32_sim_json_append_uninitialized_metadata(Masm32SimJsonWriter *writer, const Masm32SimWasmRunStorage *storage) {
    size_t index = 0U;
    bool has_object = false;

    if (writer == NULL || storage == NULL) {
        return false;
    }

    if (!masm32_sim_json_append(writer, ",\"uninitializedOrigin\":{\"tracked\":true,\"objects\":[")) {
        return false;
    }

    for (index = 0U; index < storage->object_map_entry_count; index += 1U) {
        const VmObjectMapEntry *object = &storage->object_map_entries[index];
        uint32_t byte_index = 0U;
        uint32_t data_offset = 0U;

        if (object->section == VM_SYMBOL_SECTION_CONST) {
            continue;
        }
        if (object->base_address < VM_MEMORY_DEFAULT_DATA_BASE) {
            continue;
        }
        data_offset = object->base_address - VM_MEMORY_DEFAULT_DATA_BASE;
        if ((uint64_t)data_offset + (uint64_t)object->size_bytes > (uint64_t)storage->data_initialized_mask_size) {
            continue;
        }

        if (has_object && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        has_object = true;

        {
            uint32_t initialized_count = 0U;
            uint32_t uninitialized_count = 0U;
            for (byte_index = 0U; byte_index < object->size_bytes; byte_index += 1U) {
                if (storage->data_initialized_mask[(size_t)data_offset + (size_t)byte_index] != 0U) {
                    initialized_count += 1U;
                }
            }
            uninitialized_count = object->size_bytes - initialized_count;

            if (!masm32_sim_json_append(writer, "{\"symbol\":") ||
                !masm32_sim_json_append_string(writer, object->symbol_name) ||
                !masm32_sim_json_append(
                    writer,
                    ",\"state\":\"%s\",\"initializedByteCount\":%u,\"uninitializedByteCount\":%u,\"initializedMask\":\"",
                    vm_object_initialization_origin_state_name(object->initialization_origin_state),
                    (unsigned int)initialized_count,
                    (unsigned int)uninitialized_count
                )) {
                return false;
            }

            for (byte_index = 0U; byte_index < object->size_bytes; byte_index += 1U) {
                if (!masm32_sim_json_append(
                        writer,
                        "%c",
                        storage->data_initialized_mask[(size_t)data_offset + (size_t)byte_index] != 0U ? '1' : '0'
                    )) {
                    return false;
                }
            }
        }

        if (!masm32_sim_json_append(writer, "\"}")) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "]}");
}

/// Builds a full JSON response into the static source-run buffer.
///
/// @param outcome High-level result outcome.
/// @param vm Optional VM state for successful or failed execution.
/// @param parser_result Optional parser result for parse diagnostics.
/// @param parser_diagnostics Optional parser diagnostic array.
/// @param exec_status Executor status when relevant.
/// @param storage Optional source-run storage containing symbol-aware memory changes.
/// @param layout_policy Optional selected randomized layout policy to serialize.
/// @param layout_message Optional layout failure message to serialize.
/// @param include_uninitialized_metadata Whether to append test-only Phase 39 metadata.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_run_json(
    Masm32SimWasmRunOutcome outcome,
    const Vm *vm,
    const VmParserResult *parser_result,
    const VmParserDiagnostic *parser_diagnostics,
    VmExecStatus exec_status,
    const Masm32SimWasmRunStorage *storage,
    const VmLayoutPolicy *layout_policy,
    const Masm32SimWasmLayoutMessage *layout_message,
    bool include_uninitialized_metadata
) {
    Masm32SimJsonWriter writer;
    uint64_t instruction_count = vm != NULL ? vm->instruction_count : 0U;
    bool ok = outcome == MASM32_SIM_WASM_RUN_OUTCOME_OK;

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append(&writer, "{\"phase\":52,\"ok\":%s,\"status\":", ok ? "true" : "false");
    (void)masm32_sim_json_append_string(&writer, masm32_sim_wasm_run_outcome_name(outcome));
    (void)masm32_sim_json_append(&writer, ",\"instructionCount\":%llu,", (unsigned long long)instruction_count);
    (void)masm32_sim_json_append_layout_metadata(&writer, layout_policy);
    (void)masm32_sim_json_append(
        &writer,
        "\"virtualIncludes\":{\"irvine32\":%s,\"irvine32SymbolCount\":%lu},",
        parser_result != NULL && parser_result->has_irvine32_virtual_include ? "true" : "false",
        (unsigned long)(parser_result != NULL ? parser_result->irvine32_virtual_symbol_count : 0U)
    );

    if (vm != NULL) {
        (void)masm32_sim_json_append_registers(&writer, &vm->cpu);
        (void)masm32_sim_json_append(&writer, ",");
    }

    (void)masm32_sim_json_append_memory_changes(&writer, storage);
    if (include_uninitialized_metadata) {
        (void)masm32_sim_json_append_uninitialized_metadata(&writer, storage);
    }
    (void)masm32_sim_json_append(&writer, ",");

    (void)masm32_sim_json_append(&writer, "\"simulatorMessages\":[");
    if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_OK) {
        bool has_message = false;
        if (parser_result != NULL && parser_diagnostics != NULL && parser_result->diagnostic_count > 0U) {
            (void)masm32_sim_json_append_parser_messages(&writer, parser_diagnostics, parser_result->diagnostic_count);
            has_message = true;
        }
        (void)masm32_sim_json_append_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_object_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_uninitialized_read_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_shift_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_flag_use_warnings(&writer, storage, &has_message);
        if (has_message) {
            (void)masm32_sim_json_append(&writer, ",");
        }
        (void)masm32_sim_json_append_message(&writer, "info", "execution-complete", "Execution completed successfully.", 0U, 0U);
    } else if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT) {
        (void)masm32_sim_json_append_message(&writer, "assembly-error", "invalid-source", "Source text is required.", 0U, 0U);
    } else if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_PARSE_ERROR) {
        (void)masm32_sim_json_append_parser_messages(
            &writer,
            parser_diagnostics,
            parser_result != NULL ? parser_result->diagnostic_count : 0U
        );
    } else if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR) {
        if (storage != NULL && storage->has_object_violation) {
            (void)masm32_sim_json_append_object_violation(&writer, &storage->object_violation);
        } else if (storage != NULL && storage->has_uninitialized_read_violation) {
            (void)masm32_sim_json_append_uninitialized_read_violation(&writer, &storage->uninitialized_read_violation);
        } else if (storage != NULL && storage->has_shift_violation) {
            (void)masm32_sim_json_append_shift_violation(&writer, &storage->shift_violation);
        } else if (storage != NULL && storage->has_flag_use_violation) {
            (void)masm32_sim_json_append_flag_use_violation(&writer, &storage->flag_use_violation);
        } else {
            (void)masm32_sim_json_append_exec_message(&writer, vm != NULL ? vm_last_diagnostic(vm) : NULL, exec_status);
        }
    } else if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_RESOURCE_LIMIT) {
        const char *message = layout_message != NULL ? layout_message->message : "Automatic layout exceeded the configured resource limits.";
        const char *code = layout_message != NULL && layout_message->code != NULL ? layout_message->code : "resource-limit-exceeded";
        (void)masm32_sim_json_append_message_with_span(
            &writer,
            "resource-limit-error",
            code,
            message,
            layout_message != NULL ? layout_message->line : 0U,
            layout_message != NULL ? layout_message->column : 0U,
            layout_message != NULL ? layout_message->byte_offset : 0U,
            layout_message != NULL ? layout_message->span_length : 0U,
            layout_message != NULL && layout_message->has_source_span
        );
    } else {
        (void)masm32_sim_json_append_message(&writer, "internal-simulator-error", "response-truncated", "The simulator response exceeded its fixed buffer.", 0U, 0U);
    }
    (void)masm32_sim_json_append(&writer, "]}");

    if (writer.overflowed) {
        (void)snprintf(
            g_masm32_sim_wasm_run_json,
            sizeof(g_masm32_sim_wasm_run_json),
            "{\"phase\":52,\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}"
        );
    }

    return g_masm32_sim_wasm_run_json;
}

MASM32_SIM_EXPORT int masm32_sim_wasm_test_value(void) {
    return masm_sim_milestone_zero_value();
}

MASM32_SIM_EXPORT int masm32_sim_wasm_milestone4_hardcoded_result(void) {
    uint32_t eax = 0U;

    if (vm_run_milestone4_hardcoded_program(&eax) != VM_EXEC_STATUS_OK) {
        return -1;
    }

    return (int)eax;
}


/// Safely converts a size_t value to uint32_t with saturation.
///
/// @param value Size value to convert.
/// @return Converted value, or UINT32_MAX when @p value is too large.
static uint32_t masm32_sim_wasm_size_to_u32_saturating(size_t value) {
    return value > (size_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

/// Returns the layout-region name used in resource-limit messages.
///
/// @param region Layout region to name.
/// @return Static user-facing region name.
static const char *masm32_sim_wasm_layout_region_display_name(VmLayoutRegionKind region) {
    switch (region) {
        case VM_LAYOUT_REGION_CODE:
            return ".code";
        case VM_LAYOUT_REGION_DATA:
            return ".data/.DATA?";
        case VM_LAYOUT_REGION_CONST:
            return ".CONST";
        case VM_LAYOUT_REGION_HEAP:
            return ".heap";
        case VM_LAYOUT_REGION_STACK:
            return ".stack";
        default:
            return "memory";
    }
}

/// Returns whether a parser symbol belongs to a layout region.
///
/// @param symbol Symbol to inspect.
/// @param region Layout region being checked.
/// @return true when the symbol is stored in the selected layout region.
static bool masm32_sim_wasm_symbol_matches_layout_region(const VmSymbol *symbol, VmLayoutRegionKind region) {
    if (symbol == NULL) {
        return false;
    }

    if (region == VM_LAYOUT_REGION_DATA) {
        return symbol->section == VM_SYMBOL_SECTION_DATA || symbol->section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED;
    }

    if (region == VM_LAYOUT_REGION_CONST) {
        return symbol->section == VM_SYMBOL_SECTION_CONST;
    }

    return false;
}

/// Returns the symbol whose declared byte range first exceeds a layout limit.
///
/// @param result Parser result containing symbol count metadata.
/// @param storage Source-run storage containing symbols.
/// @param diagnostic Layout diagnostic containing region and limit.
/// @return Matching symbol with source location, or NULL when no symbol applies.
static const VmSymbol *masm32_sim_wasm_find_resource_limit_symbol(
    const VmParserResult *result,
    const Masm32SimWasmRunStorage *storage,
    const VmLayoutDiagnostic *diagnostic
) {
    size_t index = 0U;

    if (result == NULL || storage == NULL || diagnostic == NULL || !diagnostic->has_region) {
        return NULL;
    }

    for (index = 0U; index < result->symbol_count; index += 1U) {
        const VmSymbol *symbol = &storage->symbols[index];
        uint32_t section_offset = 0U;
        uint32_t section_end = 0U;

        if (!masm32_sim_wasm_symbol_matches_layout_region(symbol, diagnostic->region)) {
            continue;
        }

        if (symbol->section == VM_SYMBOL_SECTION_CONST) {
            section_offset = symbol->address - VM_MEMORY_DEFAULT_CONST_BASE;
        } else {
            section_offset = symbol->address - VM_MEMORY_DEFAULT_DATA_BASE;
        }

        if (symbol->size_bytes > UINT32_MAX - section_offset) {
            return symbol;
        }
        section_end = section_offset + symbol->size_bytes;
        if (section_end > diagnostic->limit) {
            return symbol;
        }
    }

    return NULL;
}

/// Counts parser-produced `.DATA?` bytes by summing uninitialized-storage symbols.
///
/// @param result Parser result containing symbol count metadata.
/// @param storage Source-run storage containing symbols.
/// @return Total `.DATA?` bytes, saturated to UINT32_MAX.
static uint32_t masm32_sim_wasm_count_uninitialized_data_bytes(const VmParserResult *result, const Masm32SimWasmRunStorage *storage) {
    uint64_t total = 0U;
    size_t index = 0U;

    if (result == NULL || storage == NULL) {
        return 0U;
    }

    for (index = 0U; index < result->symbol_count; index += 1U) {
        const VmSymbol *symbol = &storage->symbols[index];
        if (symbol->section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED) {
            total += (uint64_t)symbol->size_bytes;
            if (total > (uint64_t)UINT32_MAX) {
                return UINT32_MAX;
            }
        }
    }

    return (uint32_t)total;
}

/// Builds automatic-layout metadata from parser output.
///
/// @param result Parser result to inspect.
/// @param storage Source-run storage containing parser symbols.
/// @param out_metadata Receives automatic layout metadata.
static void masm32_sim_wasm_build_layout_metadata(
    const VmParserResult *result,
    const Masm32SimWasmRunStorage *storage,
    VmLayoutProgramMetadata *out_metadata
) {
    uint32_t total_data_size = 0U;
    uint32_t uninitialized_data_size = 0U;

    if (out_metadata == NULL) {
        return;
    }

    memset(out_metadata, 0, sizeof(*out_metadata));
    if (result == NULL) {
        return;
    }

    total_data_size = masm32_sim_wasm_size_to_u32_saturating(result->data_size);
    uninitialized_data_size = masm32_sim_wasm_count_uninitialized_data_bytes(result, storage);
    if (uninitialized_data_size > total_data_size) {
        uninitialized_data_size = 0U;
    }

    out_metadata->code_size = masm32_sim_wasm_size_to_u32_saturating(result->instruction_count);
    out_metadata->initialized_data_size = total_data_size - uninitialized_data_size;
    out_metadata->uninitialized_data_size = uninitialized_data_size;
    out_metadata->const_size = masm32_sim_wasm_size_to_u32_saturating(result->const_size);

    if (result->has_requested_stack_size) {
        out_metadata->has_stack_size_request = true;
        out_metadata->stack_size_request = result->requested_stack_size;
    }
    out_metadata->has_heap_size_request = false;
}

/// Builds a source-mapped resource-limit message from a layout diagnostic.
///
/// @param result Parser result used for source locations.
/// @param storage Source-run storage containing symbols.
/// @param diagnostic Layout diagnostic to render.
/// @param out_message Receives the source-mapped message.
static void masm32_sim_wasm_build_layout_message(
    const VmParserResult *result,
    const Masm32SimWasmRunStorage *storage,
    const VmLayoutDiagnostic *diagnostic,
    Masm32SimWasmLayoutMessage *out_message
) {
    const VmSymbol *symbol = NULL;
    const char *region_name = "memory";

    if (out_message == NULL) {
        return;
    }

    memset(out_message, 0, sizeof(*out_message));
    out_message->code = "resource-limit-exceeded";
    if (diagnostic == NULL) {
        (void)snprintf(out_message->message, sizeof(out_message->message), "Automatic layout exceeded the configured resource limits.");
        return;
    }

    if (diagnostic->status == VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED && diagnostic->has_region) {
        region_name = masm32_sim_wasm_layout_region_display_name(diagnostic->region);
        (void)snprintf(
            out_message->message,
            sizeof(out_message->message),
            "Automatic layout requested %s region size %u bytes, exceeding configured limit %u bytes.",
            region_name,
            (unsigned int)diagnostic->requested_size,
            (unsigned int)diagnostic->limit
        );
    } else if (diagnostic->status == VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED) {
        (void)snprintf(
            out_message->message,
            sizeof(out_message->message),
            "Automatic layout requested %u total bytes, exceeding configured total reservation limit %u bytes.",
            (unsigned int)diagnostic->total_size,
            (unsigned int)diagnostic->total_limit
        );
    } else if (diagnostic->status == VM_LAYOUT_STATUS_INTEGER_OVERFLOW) {
        (void)snprintf(out_message->message, sizeof(out_message->message), "Automatic layout size calculation overflowed.");
    } else if (diagnostic->status == VM_LAYOUT_STATUS_RANDOMIZATION_UNAVAILABLE) {
        out_message->code = "randomization-unavailable";
        (void)snprintf(
            out_message->message,
            sizeof(out_message->message),
            "Randomized layout could not place %u bytes inside the configured %u-byte address range.",
            (unsigned int)diagnostic->total_size,
            (unsigned int)diagnostic->total_limit
        );
    } else {
        (void)snprintf(out_message->message, sizeof(out_message->message), "Automatic layout policy was invalid.");
    }

    if (diagnostic != NULL && diagnostic->has_region && diagnostic->region == VM_LAYOUT_REGION_STACK &&
        result != NULL && result->has_stack_directive_source_span) {
        out_message->line = result->stack_directive_source_location.line;
        out_message->column = result->stack_directive_source_location.column;
        out_message->byte_offset = result->stack_directive_source_location.offset;
        out_message->span_length = result->stack_directive_source_span_length;
        out_message->has_source_span = true;
        return;
    }

    symbol = masm32_sim_wasm_find_resource_limit_symbol(result, storage, diagnostic);
    if (symbol != NULL && symbol->source_location.line > 0U) {
        out_message->line = symbol->source_location.line;
        out_message->column = symbol->source_location.column;
        out_message->byte_offset = symbol->source_location.offset;
        out_message->span_length = symbol->source_span_length;
        out_message->has_source_span = true;
    }
}

/// Returns fixed and selected bases for an IR relocation kind.
///
/// @param relocation Relocation kind to resolve.
/// @param policy Selected layout policy containing randomized bases.
/// @param out_fixed_base Receives the fixed-layout base encoded by the parser.
/// @param out_selected_base Receives the selected layout base.
/// @param out_selected_limit Receives the selected layout exclusive limit.
/// @return true when the relocation kind maps to a relocatable region.
static bool masm32_sim_wasm_get_relocation_bases(
    VmIrRelocationKind relocation,
    const VmLayoutPolicy *policy,
    uint32_t *out_fixed_base,
    uint32_t *out_selected_base,
    uint32_t *out_selected_limit
) {
    if (policy == NULL || out_fixed_base == NULL || out_selected_base == NULL || out_selected_limit == NULL) {
        return false;
    }

    if (relocation == VM_IR_RELOCATION_DATA) {
        *out_fixed_base = VM_MEMORY_DEFAULT_DATA_BASE;
        *out_selected_base = policy->regions[VM_LAYOUT_REGION_DATA].base;
        *out_selected_limit = policy->regions[VM_LAYOUT_REGION_DATA].limit;
        return true;
    }

    if (relocation == VM_IR_RELOCATION_CONST) {
        *out_fixed_base = VM_MEMORY_DEFAULT_CONST_BASE;
        *out_selected_base = policy->regions[VM_LAYOUT_REGION_CONST].base;
        *out_selected_limit = policy->regions[VM_LAYOUT_REGION_CONST].limit;
        return true;
    }

    return false;
}

/// Relocates a parser-fixed address into the selected layout region.
///
/// @param fixed_address Address produced by the parser against fixed bases.
/// @param fixed_base Fixed base corresponding to the address section.
/// @param selected_base Selected runtime base for the section.
/// @param selected_limit Selected runtime exclusive limit for the section.
/// @param out_address Receives the relocated address.
/// @return true when the relocated address fits inside the selected region.
static bool masm32_sim_wasm_relocate_address(
    uint32_t fixed_address,
    uint32_t fixed_base,
    uint32_t selected_base,
    uint32_t selected_limit,
    uint32_t *out_address
) {
    uint32_t offset = 0U;
    uint32_t relocated = 0U;

    if (out_address == NULL || fixed_address < fixed_base || selected_base >= selected_limit) {
        return false;
    }

    offset = fixed_address - fixed_base;
    if (offset > UINT32_MAX - selected_base) {
        return false;
    }

    relocated = selected_base + offset;
    if (relocated >= selected_limit) {
        return false;
    }

    *out_address = relocated;
    return true;
}

/// Applies selected-layout relocation metadata to one IR operand.
///
/// @param operand Operand to mutate.
/// @param policy Selected randomized layout policy.
/// @return true when the operand was unrelocated or relocated successfully.
static bool masm32_sim_wasm_relocate_operand(VmIrOperand *operand, const VmLayoutPolicy *policy) {
    uint32_t fixed_base = 0U;
    uint32_t selected_base = 0U;
    uint32_t selected_limit = 0U;
    uint32_t relocated = 0U;

    if (operand == NULL) {
        return false;
    }

    if (operand->relocation == VM_IR_RELOCATION_NONE) {
        return true;
    }

    if (!masm32_sim_wasm_get_relocation_bases(operand->relocation, policy, &fixed_base, &selected_base, &selected_limit)) {
        return false;
    }

    if (operand->kind == VM_IR_OPERAND_IMMEDIATE) {
        if (!masm32_sim_wasm_relocate_address(operand->immediate, fixed_base, selected_base, selected_limit, &relocated)) {
            return false;
        }
        operand->immediate = relocated;
        operand->relocation = VM_IR_RELOCATION_NONE;
        return true;
    }

    if (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS || operand->kind == VM_IR_OPERAND_MEMORY_REGISTER) {
        if (!masm32_sim_wasm_relocate_address(operand->address, fixed_base, selected_base, selected_limit, &relocated)) {
            return false;
        }
        operand->address = relocated;
        operand->relocation = VM_IR_RELOCATION_NONE;
        return true;
    }

    return false;
}

/// Relocates parser output from fixed symbolic bases to selected runtime bases.
///
/// @param result Parser result containing instructions and symbol count metadata.
/// @param storage Source-run storage containing emitted instructions and symbols.
/// @param policy Selected randomized layout policy.
/// @return true when all relocatable parser output was relocated successfully.
static bool masm32_sim_wasm_relocate_parser_output(
    const VmParserResult *result,
    Masm32SimWasmRunStorage *storage,
    const VmLayoutPolicy *policy
) {
    size_t index = 0U;

    if (result == NULL || storage == NULL || policy == NULL) {
        return false;
    }

    for (index = 0U; index < result->instruction_count; index += 1U) {
        VmIrInstruction *instruction = &storage->instructions[index];
        if (!masm32_sim_wasm_relocate_operand(&instruction->destination, policy) ||
            !masm32_sim_wasm_relocate_operand(&instruction->source, policy)) {
            return false;
        }
    }

    for (index = 0U; index < result->symbol_count; index += 1U) {
        VmSymbol *symbol = &storage->symbols[index];
        VmIrRelocationKind relocation = VM_IR_RELOCATION_NONE;
        uint32_t fixed_base = 0U;
        uint32_t selected_base = 0U;
        uint32_t selected_limit = 0U;
        uint32_t relocated = 0U;

        if (symbol->section == VM_SYMBOL_SECTION_CONST) {
            relocation = VM_IR_RELOCATION_CONST;
        } else if (symbol->section == VM_SYMBOL_SECTION_DATA || symbol->section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED) {
            relocation = VM_IR_RELOCATION_DATA;
        }

        if (relocation == VM_IR_RELOCATION_NONE) {
            continue;
        }

        if (!masm32_sim_wasm_get_relocation_bases(relocation, policy, &fixed_base, &selected_base, &selected_limit) ||
            !masm32_sim_wasm_relocate_address(symbol->address, fixed_base, selected_base, selected_limit, &relocated)) {
            return false;
        }
        symbol->address = relocated;
    }

    return true;
}

/// Builds declared-object map metadata from final selected-layout symbols.
///
/// The map is retained for tests/internal tooling and optional memory-validation
/// modes. It is intentionally not serialized into default source-run JSON, and
/// region-only execution remains unchanged unless a validation mode consumes it.
///
/// @param result Parser result containing symbol count metadata.
/// @param storage Source-run storage that owns symbols and object entries.
/// @param runtime_policy Selected runtime layout policy, or NULL for the fixed default.
/// @return true when object-map metadata was built successfully.
static bool masm32_sim_wasm_build_declared_object_map(
    const VmParserResult *result,
    Masm32SimWasmRunStorage *storage,
    const VmLayoutPolicy *runtime_policy
) {
    VmObjectMapStatus status = VM_OBJECT_MAP_STATUS_OK;

    if (result == NULL || storage == NULL) {
        return false;
    }

    status = vm_object_map_build_from_symbols_with_initialization_mask(
        storage->symbols,
        result->symbol_count,
        runtime_policy,
        storage->data_initialized_mask,
        storage->data_initialized_mask_size,
        storage->object_map_entries,
        (size_t)MASM32_SIM_WASM_MAX_OBJECT_MAP_ENTRIES,
        &storage->object_map_entry_count
    );

    return status == VM_OBJECT_MAP_STATUS_OK;
}

/// Parses, optionally applies policy-selected layout, and executes source.
///
/// @param source Source text to run.
/// @param requested_layout_mode Fixed, automatic, seeded-randomized, or fresh-randomized layout mode.
/// @param base_policy Optional base policy for non-fixed layout modes.
/// @param validation_mode Optional memory validation behavior for runtime accesses.
/// @return Pointer to the static JSON result buffer.
static const char *masm32_sim_wasm_run_source_json_internal(
    const char *source,
    VmLayoutMode requested_layout_mode,
    const VmLayoutPolicy *base_policy,
    Masm32SimWasmMemoryValidationMode validation_mode,
    Masm32SimWasmShiftValidationMode shift_mode,
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy,
    bool include_uninitialized_metadata
) {
    VmParserConfig config;
    VmParserResult parser_result;
    Vm vm;
    VmExecStatus exec_status = VM_EXEC_STATUS_OK;
    VmParserStatus parser_status = VM_PARSER_STATUS_OK;
    VmLayoutPolicy selected_policy;
    VmLayoutProgramMetadata layout_metadata;
    VmLayoutDiagnostic layout_diagnostic;
    Masm32SimWasmLayoutMessage layout_message;
    const VmLayoutPolicy *runtime_policy = NULL;
    const VmLayoutPolicy *json_layout_policy = NULL;
    bool vm_initialized = false;
    bool use_policy_layout = requested_layout_mode != VM_LAYOUT_MODE_FIXED;
    bool use_randomized_layout = requested_layout_mode == VM_LAYOUT_MODE_SEEDED_RANDOMIZED || requested_layout_mode == VM_LAYOUT_MODE_FRESH_RANDOMIZED;

    if (source == NULL) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, include_uninitialized_metadata);
    }

    memset(&g_masm32_sim_wasm_run_storage, 0, sizeof(g_masm32_sim_wasm_run_storage));
    memset(&config, 0, sizeof(config));
    memset(&parser_result, 0, sizeof(parser_result));
    memset(&vm, 0, sizeof(vm));
    memset(&selected_policy, 0, sizeof(selected_policy));
    memset(&layout_metadata, 0, sizeof(layout_metadata));
    memset(&layout_diagnostic, 0, sizeof(layout_diagnostic));
    memset(&layout_message, 0, sizeof(layout_message));
    g_masm32_sim_wasm_run_storage.source_text = source;

    config.source = source;
    config.source_file = "main.asm";
    config.tokens = g_masm32_sim_wasm_run_storage.tokens;
    config.token_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_TOKENS;
    config.lexer_diagnostics = g_masm32_sim_wasm_run_storage.lexer_diagnostics;
    config.lexer_diagnostic_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_LEXER_DIAGNOSTICS;
    config.instructions = g_masm32_sim_wasm_run_storage.instructions;
    config.instruction_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_INSTRUCTIONS;
    config.source_text_storage = g_masm32_sim_wasm_run_storage.source_text_storage;
    config.source_text_capacity = (size_t)MASM32_SIM_WASM_RUN_SOURCE_TEXT_BYTES;
    config.symbols = g_masm32_sim_wasm_run_storage.symbols;
    config.symbol_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_SYMBOLS;
    config.data_image = g_masm32_sim_wasm_run_storage.data_image;
    config.data_image_capacity = (size_t)MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES;
    config.data_initialized_mask = g_masm32_sim_wasm_run_storage.data_initialized_mask;
    config.data_initialized_mask_capacity = (size_t)MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES;
    config.const_image = g_masm32_sim_wasm_run_storage.const_image;
    config.const_image_capacity = (size_t)MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES;
    config.diagnostics = g_masm32_sim_wasm_run_storage.parser_diagnostics;
    config.diagnostic_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_PARSER_DIAGNOSTICS;

    parser_status = vm_parser_parse_program(&config, &parser_result);
    g_masm32_sim_wasm_run_storage.data_initialized_mask_size = parser_result.data_size;
    if ((parser_status != VM_PARSER_STATUS_OK && parser_status != VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS) ||
        masm32_sim_wasm_parser_diagnostics_have_errors(g_masm32_sim_wasm_run_storage.parser_diagnostics, parser_result.diagnostic_count)) {
        return masm32_sim_wasm_build_run_json(
            MASM32_SIM_WASM_RUN_OUTCOME_PARSE_ERROR,
            NULL,
            &parser_result,
            g_masm32_sim_wasm_run_storage.parser_diagnostics,
            VM_EXEC_STATUS_INVALID_ARGUMENT,
            &g_masm32_sim_wasm_run_storage,
            NULL,
            NULL,
            include_uninitialized_metadata
        );
    }

    if (use_policy_layout) {
        VmLayoutStatus layout_status = VM_LAYOUT_STATUS_OK;
        masm32_sim_wasm_build_layout_metadata(&parser_result, &g_masm32_sim_wasm_run_storage, &layout_metadata);
        if (use_randomized_layout) {
            layout_status = vm_layout_build_randomized_policy(base_policy, &layout_metadata, requested_layout_mode, &selected_policy, &layout_diagnostic);
            json_layout_policy = &selected_policy;
        } else {
            layout_status = vm_layout_build_automatic_policy(base_policy, &layout_metadata, &selected_policy, &layout_diagnostic);
        }

        if (!vm_layout_status_succeeded(layout_status)) {
            masm32_sim_wasm_build_layout_message(&parser_result, &g_masm32_sim_wasm_run_storage, &layout_diagnostic, &layout_message);
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_RESOURCE_LIMIT,
                NULL,
                &parser_result,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                &g_masm32_sim_wasm_run_storage,
                NULL,
                &layout_message,
                include_uninitialized_metadata
            );
        }

        if (use_randomized_layout && !masm32_sim_wasm_relocate_parser_output(&parser_result, &g_masm32_sim_wasm_run_storage, &selected_policy)) {
            return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata);
        }

        runtime_policy = &selected_policy;
        exec_status = vm_init_with_layout_policy(&vm, runtime_policy);
    } else {
        exec_status = vm_init(&vm, NULL);
    }

    if (exec_status != VM_EXEC_STATUS_OK) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata);
    }
    vm_initialized = true;

    if (!masm32_sim_wasm_build_declared_object_map(&parser_result, &g_masm32_sim_wasm_run_storage, runtime_policy)) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, VM_EXEC_STATUS_INVALID_ARGUMENT, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata);
        vm_deinit(&vm);
        return json;
    }

    exec_status = masm32_sim_wasm_load_section_image(&vm, VM_MEMORY_REGION_DATA, g_masm32_sim_wasm_run_storage.data_image, (uint32_t)parser_result.data_size);
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata);
        vm_deinit(&vm);
        return json;
    }

    exec_status = masm32_sim_wasm_load_section_image(&vm, VM_MEMORY_REGION_CONST, g_masm32_sim_wasm_run_storage.const_image, (uint32_t)parser_result.const_size);
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata);
        vm_deinit(&vm);
        return json;
    }

    vm_memory_clear_changes(&vm.memory);

    exec_status = vm_load_program(&vm, g_masm32_sim_wasm_run_storage.instructions, parser_result.instruction_count);
    while (exec_status == VM_EXEC_STATUS_OK && !vm.halted) {
        exec_status = masm32_sim_wasm_validate_uninitialized_reads_before_step(
            &g_masm32_sim_wasm_run_storage,
            &vm,
            validation_mode,
            runtime_policy
        );
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = masm32_sim_wasm_validate_shift_before_step(&g_masm32_sim_wasm_run_storage, &vm, shift_mode);
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = masm32_sim_wasm_validate_flag_use_before_step(&g_masm32_sim_wasm_run_storage, &vm, flag_use_policy);
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = vm_step(&vm);
        if (exec_status == VM_EXEC_STATUS_OK) {
            masm32_sim_wasm_collect_unaligned_warnings(&g_masm32_sim_wasm_run_storage, &vm);
            exec_status = masm32_sim_wasm_validate_object_accesses(&g_masm32_sim_wasm_run_storage, &vm, validation_mode, runtime_policy);
            if (exec_status == VM_EXEC_STATUS_OK) {
                masm32_sim_wasm_mark_initialized_writes(&g_masm32_sim_wasm_run_storage, &vm, runtime_policy);
                masm32_sim_wasm_collect_memory_change(&g_masm32_sim_wasm_run_storage, &vm, &parser_result);
            }
        }
    }

    if (exec_status == VM_EXEC_STATUS_HALTED) {
        exec_status = VM_EXEC_STATUS_OK;
    }

    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata);
        if (vm_initialized) {
            vm_deinit(&vm);
        }
        return json;
    }

    {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_OK, &vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, VM_EXEC_STATUS_OK, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata);
        if (vm_initialized) {
            vm_deinit(&vm);
        }
        return json;
    }
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json(const char *source) {
    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, false);
}

const char *masm32_sim_wasm_run_source_json_with_automatic_layout_policy(const char *source, const VmLayoutPolicy *base_policy) {
    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_AUTOMATIC, base_policy, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, false);
}

const char *masm32_sim_wasm_run_source_json_with_randomized_layout_policy(const char *source, VmLayoutMode randomized_mode, const VmLayoutPolicy *base_policy) {
    return masm32_sim_wasm_run_source_json_internal(source, randomized_mode, base_policy, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, false);
}

const char *masm32_sim_wasm_run_source_json_with_memory_validation_mode(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode
) {
    if (validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false);
    }

    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, validation_mode, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, false);
}

const char *masm32_sim_wasm_run_source_json_with_shift_validation_mode(
    const char *source,
    Masm32SimWasmShiftValidationMode shift_mode
) {
    if (shift_mode != MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS &&
        shift_mode != MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false);
    }

    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, shift_mode, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, false);
}

const char *masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
    const char *source,
    Masm32SimWasmUndefinedFlagUsePolicy policy
) {
    if (policy != MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF &&
        policy != MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN &&
        policy != MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false);
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        policy,
        false
    );
}

const char *masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode
) {
    if (validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS &&
        validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, true);
    }

    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, validation_mode, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, true);
}

const char *masm32_sim_wasm_run_source_json_with_uninitialized_metadata(const char *source) {
    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, true);
}

MASM32_SIM_EXPORT int masm32_sim_wasm_copy_version(char *out_buffer, unsigned long out_buffer_size) {
    return (int)masm_sim_copy_version(out_buffer, (size_t)out_buffer_size);
}
