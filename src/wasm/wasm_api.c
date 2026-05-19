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
 * instructions, carry/borrow arithmetic, carry-flag control, and recovered
 * unsupported-feature diagnostics, then reports a compact JSON result for the UI.
 */

#include "wasm_api.h"

#include "../core/masm32_sim_api.h"
#include "../core/vm_cpu.h"
#include "../core/vm_exec.h"
#include "../core/vm_layout.h"
#include "../core/vm_memory.h"
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

/// Maximum .data/.DATA? bytes laid out by the source-run API.
#define MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES VM_MEMORY_DEFAULT_DATA_SIZE

/// Maximum .CONST bytes laid out by the source-run API.
#define MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES VM_MEMORY_DEFAULT_CONST_SIZE

/// Maximum symbol-aware memory changes retained for one source run.
#define MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES 64U

/// Maximum simulator warnings retained for one source run.
#define MASM32_SIM_WASM_MAX_RUN_WARNINGS 64U

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

/// Describes one source-mapped automatic layout resource-limit message.
typedef struct Masm32SimWasmLayoutMessage {
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
    /// Data image bytes emitted by the parser for `.data` and `.DATA?`.
    uint8_t data_image[MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES];
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

/// Returns the simulator-message category for one parser diagnostic.
///
/// @param code Parser diagnostic code to classify.
/// @return Stable simulator-message kind string.
static const char *masm32_sim_wasm_parser_diagnostic_kind(VmParserDiagnosticCode code) {
    if (code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE) {
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
                masm32_sim_wasm_parser_diagnostic_kind(diagnostic->code),
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

/// Builds a full JSON response into the static source-run buffer.
///
/// @param outcome High-level result outcome.
/// @param vm Optional VM state for successful or failed execution.
/// @param parser_result Optional parser result for parse diagnostics.
/// @param parser_diagnostics Optional parser diagnostic array.
/// @param exec_status Executor status when relevant.
/// @param storage Optional source-run storage containing symbol-aware memory changes.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_run_json(
    Masm32SimWasmRunOutcome outcome,
    const Vm *vm,
    const VmParserResult *parser_result,
    const VmParserDiagnostic *parser_diagnostics,
    VmExecStatus exec_status,
    const Masm32SimWasmRunStorage *storage,
    const Masm32SimWasmLayoutMessage *layout_message
) {
    Masm32SimJsonWriter writer;
    uint64_t instruction_count = vm != NULL ? vm->instruction_count : 0U;
    bool ok = outcome == MASM32_SIM_WASM_RUN_OUTCOME_OK;

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append(&writer, "{\"phase\":30,\"ok\":%s,\"status\":", ok ? "true" : "false");
    (void)masm32_sim_json_append_string(&writer, masm32_sim_wasm_run_outcome_name(outcome));
    (void)masm32_sim_json_append(&writer, ",\"instructionCount\":%llu,", (unsigned long long)instruction_count);

    if (vm != NULL) {
        (void)masm32_sim_json_append_registers(&writer, &vm->cpu);
        (void)masm32_sim_json_append(&writer, ",");
    }

    (void)masm32_sim_json_append_memory_changes(&writer, storage);
    (void)masm32_sim_json_append(&writer, ",");

    (void)masm32_sim_json_append(&writer, "\"simulatorMessages\":[");
    if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_OK) {
        bool has_message = false;
        (void)masm32_sim_json_append_warnings(&writer, storage, &has_message);
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
        (void)masm32_sim_json_append_exec_message(&writer, vm != NULL ? vm_last_diagnostic(vm) : NULL, exec_status);
    } else if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_RESOURCE_LIMIT) {
        const char *message = layout_message != NULL ? layout_message->message : "Automatic layout exceeded the configured resource limits.";
        (void)masm32_sim_json_append_message_with_span(
            &writer,
            "resource-limit-error",
            "resource-limit-exceeded",
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
            "{\"phase\":30,\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}"
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

/// Parses, optionally applies automatic layout sizing, and executes source.
///
/// @param source Source text to run.
/// @param use_automatic_layout Whether to compute automatic layout sizes.
/// @param base_policy Optional base policy for automatic layout mode.
/// @return Pointer to the static JSON result buffer.
static const char *masm32_sim_wasm_run_source_json_internal(const char *source, bool use_automatic_layout, const VmLayoutPolicy *base_policy) {
    VmParserConfig config;
    VmParserResult parser_result;
    Vm vm;
    VmExecStatus exec_status = VM_EXEC_STATUS_OK;
    VmParserStatus parser_status = VM_PARSER_STATUS_OK;
    VmLayoutPolicy automatic_policy;
    VmLayoutProgramMetadata layout_metadata;
    VmLayoutDiagnostic layout_diagnostic;
    Masm32SimWasmLayoutMessage layout_message;
    bool vm_initialized = false;

    if (source == NULL) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL);
    }

    memset(&g_masm32_sim_wasm_run_storage, 0, sizeof(g_masm32_sim_wasm_run_storage));
    memset(&config, 0, sizeof(config));
    memset(&parser_result, 0, sizeof(parser_result));
    memset(&vm, 0, sizeof(vm));
    memset(&automatic_policy, 0, sizeof(automatic_policy));
    memset(&layout_metadata, 0, sizeof(layout_metadata));
    memset(&layout_diagnostic, 0, sizeof(layout_diagnostic));
    memset(&layout_message, 0, sizeof(layout_message));

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
    config.const_image = g_masm32_sim_wasm_run_storage.const_image;
    config.const_image_capacity = (size_t)MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES;
    config.diagnostics = g_masm32_sim_wasm_run_storage.parser_diagnostics;
    config.diagnostic_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_PARSER_DIAGNOSTICS;

    parser_status = vm_parser_parse_program(&config, &parser_result);
    if (parser_status != VM_PARSER_STATUS_OK || parser_result.diagnostic_count > 0U) {
        return masm32_sim_wasm_build_run_json(
            MASM32_SIM_WASM_RUN_OUTCOME_PARSE_ERROR,
            NULL,
            &parser_result,
            g_masm32_sim_wasm_run_storage.parser_diagnostics,
            VM_EXEC_STATUS_INVALID_ARGUMENT,
            &g_masm32_sim_wasm_run_storage,
            NULL
        );
    }

    if (use_automatic_layout) {
        VmLayoutStatus layout_status = VM_LAYOUT_STATUS_OK;
        masm32_sim_wasm_build_layout_metadata(&parser_result, &g_masm32_sim_wasm_run_storage, &layout_metadata);
        layout_status = vm_layout_build_automatic_policy(base_policy, &layout_metadata, &automatic_policy, &layout_diagnostic);
        if (!vm_layout_status_succeeded(layout_status)) {
            masm32_sim_wasm_build_layout_message(&parser_result, &g_masm32_sim_wasm_run_storage, &layout_diagnostic, &layout_message);
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_RESOURCE_LIMIT,
                NULL,
                &parser_result,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                &g_masm32_sim_wasm_run_storage,
                &layout_message
            );
        }
        exec_status = vm_init_with_layout_policy(&vm, &automatic_policy);
    } else {
        exec_status = vm_init(&vm, NULL);
    }

    if (exec_status != VM_EXEC_STATUS_OK) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, NULL, exec_status, &g_masm32_sim_wasm_run_storage, NULL);
    }
    vm_initialized = true;

    exec_status = masm32_sim_wasm_load_section_image(&vm, VM_MEMORY_REGION_DATA, g_masm32_sim_wasm_run_storage.data_image, (uint32_t)parser_result.data_size);
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, NULL, exec_status, &g_masm32_sim_wasm_run_storage, NULL);
        vm_deinit(&vm);
        return json;
    }

    exec_status = masm32_sim_wasm_load_section_image(&vm, VM_MEMORY_REGION_CONST, g_masm32_sim_wasm_run_storage.const_image, (uint32_t)parser_result.const_size);
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, NULL, exec_status, &g_masm32_sim_wasm_run_storage, NULL);
        vm_deinit(&vm);
        return json;
    }

    vm_memory_clear_changes(&vm.memory);

    exec_status = vm_load_program(&vm, g_masm32_sim_wasm_run_storage.instructions, parser_result.instruction_count);
    while (exec_status == VM_EXEC_STATUS_OK && !vm.halted) {
        exec_status = vm_step(&vm);
        if (exec_status == VM_EXEC_STATUS_OK) {
            masm32_sim_wasm_collect_unaligned_warnings(&g_masm32_sim_wasm_run_storage, &vm);
            masm32_sim_wasm_collect_memory_change(&g_masm32_sim_wasm_run_storage, &vm, &parser_result);
        }
    }

    if (exec_status == VM_EXEC_STATUS_HALTED) {
        exec_status = VM_EXEC_STATUS_OK;
    }

    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, &vm, &parser_result, NULL, exec_status, &g_masm32_sim_wasm_run_storage, NULL);
        if (vm_initialized) {
            vm_deinit(&vm);
        }
        return json;
    }

    {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_OK, &vm, &parser_result, NULL, VM_EXEC_STATUS_OK, &g_masm32_sim_wasm_run_storage, NULL);
        if (vm_initialized) {
            vm_deinit(&vm);
        }
        return json;
    }
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json(const char *source) {
    return masm32_sim_wasm_run_source_json_internal(source, false, NULL);
}

const char *masm32_sim_wasm_run_source_json_with_automatic_layout_policy(const char *source, const VmLayoutPolicy *base_policy) {
    return masm32_sim_wasm_run_source_json_internal(source, true, base_policy);
}

MASM32_SIM_EXPORT int masm32_sim_wasm_copy_version(char *out_buffer, unsigned long out_buffer_size) {
    return (int)masm_sim_copy_version(out_buffer, (size_t)out_buffer_size);
}
