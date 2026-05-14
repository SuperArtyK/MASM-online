/*
 * @file parser.c
 * @brief Parser for MASM-like .data and minimal .code programs through Milestone 11.
 *
 * This implementation consumes the lexer token stream, lays out a small .data
 * image with symbols, and emits only the minimal IR supported by the current
 * executor. Control flow, stack behavior, scaled-index addressing, Irvine32 routines,
 * and full MASM expression parsing remain later milestones.
 */

#include "parser.h"

#include "vm_memory.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/// Stores the first parsed procedure name so END can validate the entry point.
typedef struct VmParserProcedureName {
    /// Pointer into the source buffer at the procedure name.
    const char *lexeme;
    /// Procedure-name length in bytes.
    size_t length;
    /// Whether a procedure name has been recorded.
    bool is_set;
} VmParserProcedureName;

/// Identifies the top-level section currently being parsed.
typedef enum VmParserSection {
    /// No .data or .code directive has been consumed yet.
    VM_PARSER_SECTION_NONE = 0,
    /// Parser is currently consuming .data declarations.
    VM_PARSER_SECTION_DATA,
    /// Parser is currently consuming .code statements.
    VM_PARSER_SECTION_CODE
} VmParserSection;

/// Owns mutable parser state for one parse operation.
typedef struct VmParserState {
    /// Caller-provided parse configuration.
    const VmParserConfig *config;
    /// Caller-provided result structure being populated.
    VmParserResult *result;
    /// Lexer result produced before parsing begins.
    VmLexerResult lexer_result;
    /// Current token index inside @ref VmParserConfig.tokens.
    size_t token_index;
    /// Current write offset into source-text storage.
    size_t source_text_offset;
    /// Current write offset into the .data image.
    uint32_t data_offset;
    /// Current top-level parser section.
    VmParserSection section;
    /// Recorded procedure name for END validation.
    VmParserProcedureName procedure_name;
    /// Whether the required .code directive has been consumed.
    bool saw_code_directive;
    /// Whether an optional .data directive has been consumed.
    bool saw_data_directive;
    /// Whether an END directive has been parsed.
    bool saw_end;
    /// Specific non-OK status requested by a hard failure, or OK when unset.
    VmParserStatus stop_status;
    /// Whether a required diagnostic could not be recorded.
    bool diagnostic_overflowed;
} VmParserState;

/// Describes one parsed PTR width override prefix.
typedef struct VmParserPtrWidth {
    /// Width in bits requested by BYTE/WORD/DWORD/QWORD PTR.
    uint8_t width_bits;
    /// Token containing the width keyword for diagnostics.
    const VmLexerToken *width_token;
    /// Token containing PTR for diagnostics.
    const VmLexerToken *ptr_token;
} VmParserPtrWidth;

/// Encodes a signed or unsigned lexer number token for an operand width.
///
/// Positive literals may use the full unsigned width. Negative literals must
/// fit the signed range and are encoded in two's-complement form.
///
/// @param token Number token to encode.
/// @param width_bits Destination width in bits.
/// @param out_value Receives the encoded 32-bit immediate value.
/// @return true when the token fits the requested width.
static bool vm_parser_encode_number_for_width(const VmLexerToken *token, uint8_t width_bits, uint32_t *out_value);

/// Returns whether a byte is an ASCII whitespace byte other than line endings.
///
/// @param ch Source byte to inspect.
/// @return true for spaces and tabs.
static bool vm_parser_is_horizontal_space(char ch) {
    return ch == ' ' || ch == '\t';
}

/// Converts an ASCII letter to lowercase without depending on locale.
///
/// @param ch Source byte to convert.
/// @return Lowercase ASCII letter or the original byte.
static char vm_parser_ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }

    return ch;
}

/// Compares a token lexeme to a null-terminated ASCII literal case-insensitively.
///
/// @param token Token whose lexeme should be compared.
/// @param literal Null-terminated literal.
/// @return true when the token lexeme and literal match ignoring ASCII case.
static bool vm_parser_token_equals(const VmLexerToken *token, const char *literal) {
    size_t index = 0U;
    size_t literal_length = 0U;

    if (token == NULL || literal == NULL) {
        return false;
    }

    literal_length = strlen(literal);
    if (token->lexeme_length != literal_length) {
        return false;
    }

    for (index = 0U; index < literal_length; index += 1U) {
        if (vm_parser_ascii_lower(token->lexeme[index]) != vm_parser_ascii_lower(literal[index])) {
            return false;
        }
    }

    return true;
}

/// Compares two lexer token lexemes case-insensitively.
///
/// @param left First token.
/// @param right Second token.
/// @return true when both lexemes match ignoring ASCII case.
static bool vm_parser_token_lexemes_equal(const VmLexerToken *left, const VmLexerToken *right) {
    size_t index = 0U;

    if (left == NULL || right == NULL || left->lexeme_length != right->lexeme_length) {
        return false;
    }

    for (index = 0U; index < left->lexeme_length; index += 1U) {
        if (vm_parser_ascii_lower(left->lexeme[index]) != vm_parser_ascii_lower(right->lexeme[index])) {
            return false;
        }
    }

    return true;
}

/// Returns the current parser token.
///
/// @param state Parser state to inspect.
/// @return Pointer to the current token, or NULL when unavailable.
static const VmLexerToken *vm_parser_current_token(const VmParserState *state) {
    if (state == NULL || state->config == NULL || state->config->tokens == NULL || state->token_index >= state->lexer_result.token_count) {
        return NULL;
    }

    return &state->config->tokens[state->token_index];
}

/// Returns a lookahead parser token.
///
/// @param state Parser state to inspect.
/// @param offset Number of tokens after the current token.
/// @return Pointer to the lookahead token, or NULL when unavailable.
static const VmLexerToken *vm_parser_peek_token(const VmParserState *state, size_t offset) {
    size_t index = 0U;

    if (state == NULL || state->config == NULL || state->config->tokens == NULL) {
        return NULL;
    }

    index = state->token_index + offset;
    if (index >= state->lexer_result.token_count) {
        return NULL;
    }

    return &state->config->tokens[index];
}

/// Advances the parser by one token when possible.
///
/// @param state Parser state to mutate.
static void vm_parser_advance(VmParserState *state) {
    if (state != NULL && state->token_index < state->lexer_result.token_count) {
        state->token_index += 1U;
    }
}

/// Returns whether a token is EOF or a newline.
///
/// @param token Token to inspect.
/// @return true for EOF and newline tokens.
static bool vm_parser_is_line_end_token(const VmLexerToken *token) {
    return token != NULL && (token->kind == VM_LEXER_TOKEN_EOF || token->kind == VM_LEXER_TOKEN_NEWLINE);
}

/// Skips consecutive newline tokens.
///
/// @param state Parser state to mutate.
static void vm_parser_skip_newlines(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);

    while (token != NULL && token->kind == VM_LEXER_TOKEN_NEWLINE) {
        vm_parser_advance(state);
        token = vm_parser_current_token(state);
    }
}

/// Records one parser diagnostic.
///
/// @param state Parser state whose diagnostic buffer should receive the entry.
/// @param code Diagnostic code.
/// @param token Optional token associated with the diagnostic.
/// @param message Static diagnostic message.
/// @return true when the diagnostic was recorded.
static bool vm_parser_add_diagnostic(
    VmParserState *state,
    VmParserDiagnosticCode code,
    const VmLexerToken *token,
    const char *message
) {
    VmParserDiagnostic *diagnostic = NULL;

    if (state == NULL || state->config == NULL || state->result == NULL) {
        return false;
    }

    if (state->result->diagnostic_count >= state->config->diagnostic_capacity || state->config->diagnostics == NULL) {
        state->diagnostic_overflowed = true;
        return false;
    }

    diagnostic = &state->config->diagnostics[state->result->diagnostic_count];
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->code = code;
    if (token != NULL) {
        diagnostic->location = token->location;
        diagnostic->lexeme = token->lexeme;
        diagnostic->lexeme_length = token->lexeme_length;
    }
    diagnostic->message = message;
    state->result->diagnostic_count += 1U;
    return true;
}

/// Copies a trimmed source line into parser-owned source-text storage.
///
/// @param state Parser state with source-text storage.
/// @param token Token on the line to copy.
/// @param out_text Receives a null-terminated line-text pointer on success.
/// @return true when the source line was copied.
static bool vm_parser_copy_source_line(VmParserState *state, const VmLexerToken *token, const char **out_text) {
    const char *source = NULL;
    const char *line_start = NULL;
    const char *line_end = NULL;
    const char *trim_start = NULL;
    const char *trim_end = NULL;
    size_t length = 0U;
    char *destination = NULL;

    if (state == NULL || state->config == NULL || token == NULL || out_text == NULL || state->config->source == NULL) {
        return false;
    }

    source = state->config->source;
    line_start = source + token->location.offset;
    while (line_start > source && line_start[-1] != '\n' && line_start[-1] != '\r') {
        line_start -= 1;
    }

    line_end = source + token->location.offset;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
        line_end += 1;
    }

    trim_start = line_start;
    while (trim_start < line_end && vm_parser_is_horizontal_space(*trim_start)) {
        trim_start += 1;
    }

    trim_end = line_end;
    while (trim_end > trim_start && vm_parser_is_horizontal_space(trim_end[-1])) {
        trim_end -= 1;
    }

    length = (size_t)(trim_end - trim_start);
    if (state->config->source_text_storage == NULL || state->source_text_offset + length + 1U > state->config->source_text_capacity) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SOURCE_TEXT_CAPACITY_EXCEEDED, token, "Instruction source-text storage capacity exceeded.");
        state->stop_status = VM_PARSER_STATUS_SOURCE_TEXT_CAPACITY_EXCEEDED;
        return false;
    }

    destination = state->config->source_text_storage + state->source_text_offset;
    if (length > 0U) {
        memcpy(destination, trim_start, length);
    }
    destination[length] = '\0';
    state->source_text_offset += length + 1U;
    *out_text = destination;
    return true;
}

/// Returns whether a data image can receive another byte.
///
/// @param state Parser state to inspect.
/// @param token Token used for diagnostics on failure.
/// @return true when a byte can be appended.
static bool vm_parser_data_has_capacity(VmParserState *state, const VmLexerToken *token) {
    if (state == NULL || state->config == NULL || state->config->data_image == NULL || state->data_offset >= state->config->data_image_capacity) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, token, ".data image capacity exceeded.");
        if (state != NULL) {
            state->stop_status = VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED;
        }
        return false;
    }

    return true;
}

/// Appends one byte to the .data image.
///
/// @param state Parser state to mutate.
/// @param value Byte value to append.
/// @param token Token used for diagnostics on failure.
/// @return true when the byte was appended.
static bool vm_parser_append_data_byte(VmParserState *state, uint8_t value, const VmLexerToken *token) {
    if (!vm_parser_data_has_capacity(state, token)) {
        return false;
    }

    state->config->data_image[state->data_offset] = value;
    state->data_offset += 1U;
    state->result->data_size = state->data_offset;
    return true;
}

/// Appends a little-endian integer element to the .data image.
///
/// @param state Parser state to mutate.
/// @param value Value to encode.
/// @param size_bytes Number of little-endian bytes to write.
/// @param token Token used for diagnostics on failure.
/// @return true when the integer was appended.
static bool vm_parser_append_data_integer(VmParserState *state, uint64_t value, uint8_t size_bytes, const VmLexerToken *token) {
    uint8_t index = 0U;

    for (index = 0U; index < size_bytes; index += 1U) {
        if (!vm_parser_append_data_byte(state, (uint8_t)((value >> (8U * index)) & 0xFFU), token)) {
            return false;
        }
    }

    return true;
}

/// Returns the maximum integer value representable by a data element size.
///
/// @param size_bytes Element size in bytes.
/// @return Maximum unsigned value accepted by the current data parser.
static uint64_t vm_parser_max_value_for_size(uint8_t size_bytes) {
    switch (size_bytes) {
        case 1U:
            return 0xFFULL;
        case 2U:
            return 0xFFFFULL;
        case 4U:
            return 0xFFFFFFFFULL;
        case 8U:
            return UINT64_MAX;
        default:
            return 0ULL;
    }
}

/// Returns the largest accepted negative magnitude for a signed data element.
///
/// @param size_bytes Element size in bytes.
/// @return Maximum accepted magnitude for a negative literal, or zero for invalid sizes.
static uint64_t vm_parser_max_negative_magnitude_for_size(uint8_t size_bytes) {
    switch (size_bytes) {
        case 1U:
            return 0x80ULL;
        case 2U:
            return 0x8000ULL;
        case 4U:
            return 0x80000000ULL;
        case 8U:
            return 0x8000000000000000ULL;
        default:
            return 0ULL;
    }
}

/// Encodes a signed or unsigned lexer number token for a data element size.
///
/// Positive literals may use the full unsigned width. Negative literals must
/// fit the signed range and are encoded in two's-complement form.
///
/// @param token Number token to encode.
/// @param size_bytes Destination size in bytes.
/// @param out_value Receives the encoded unsigned value.
/// @return true when the token fits the requested size.
static bool vm_parser_encode_number_for_size(const VmLexerToken *token, uint8_t size_bytes, uint64_t *out_value) {
    if (token == NULL || out_value == NULL || token->kind != VM_LEXER_TOKEN_NUMBER || size_bytes == 0U || size_bytes > 8U) {
        return false;
    }

    if (token->number_is_negative) {
        uint64_t max_magnitude = vm_parser_max_negative_magnitude_for_size(size_bytes);
        if (token->number_value > max_magnitude) {
            return false;
        }
        *out_value = 0ULL - token->number_value;
        return true;
    }

    if (token->number_value > vm_parser_max_value_for_size(size_bytes)) {
        return false;
    }
    *out_value = token->number_value;
    return true;
}

/// Decodes one simple escaped string byte.
///
/// @param ch Escaped byte after a backslash.
/// @return Decoded byte value.
static uint8_t vm_parser_decode_escaped_byte(char ch) {
    switch (ch) {
        case 'n':
            return (uint8_t)'\n';
        case 'r':
            return (uint8_t)'\r';
        case 't':
            return (uint8_t)'\t';
        case '"':
            return (uint8_t)'"';
        case '\\':
            return (uint8_t)'\\';
        default:
            return (uint8_t)ch;
    }
}

/// Appends bytes decoded from a lexer string token.
///
/// @param state Parser state to mutate.
/// @param string_token String token including surrounding quotes.
/// @param out_element_count Receives number of bytes appended.
/// @return true when the string was decoded into .data bytes.
static bool vm_parser_append_string_bytes(VmParserState *state, const VmLexerToken *string_token, uint32_t *out_element_count) {
    size_t index = 1U;
    size_t end = 0U;
    uint32_t count = 0U;

    if (state == NULL || string_token == NULL || out_element_count == NULL || string_token->lexeme_length < 2U) {
        return false;
    }

    end = string_token->lexeme_length - 1U;
    while (index < end) {
        uint8_t value = 0U;
        if (string_token->lexeme[index] == '\\' && index + 1U < end) {
            index += 1U;
            value = vm_parser_decode_escaped_byte(string_token->lexeme[index]);
        } else {
            value = (uint8_t)string_token->lexeme[index];
        }
        if (!vm_parser_append_data_byte(state, value, string_token)) {
            return false;
        }
        count += 1U;
        index += 1U;
    }

    *out_element_count = count;
    return true;
}

/// Parses one non-DUP initializer and appends its bytes.
///
/// @param state Parser state to mutate.
/// @param data_type Declared data type.
/// @param out_element_count Receives number of declared elements appended.
/// @param out_has_uninitialized Receives true when `?` was parsed.
/// @return true when the initializer was parsed.
static bool vm_parser_parse_single_data_initializer(
    VmParserState *state,
    VmSymbolDataType data_type,
    uint32_t *out_element_count,
    bool *out_has_uninitialized
) {
    const VmLexerToken *token = vm_parser_current_token(state);
    uint8_t element_size = vm_symbol_data_type_size_bytes(data_type);

    if (state == NULL || out_element_count == NULL || out_has_uninitialized == NULL || element_size == 0U) {
        return false;
    }

    *out_element_count = 0U;
    *out_has_uninitialized = false;

    if (token == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, token, "Expected .data initializer.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_NUMBER) {
        uint64_t encoded_value = 0U;
        if (!vm_parser_encode_number_for_size(token, element_size, &encoded_value)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, token, "Data initializer exceeds the declared element width.");
            return false;
        }
        if (!vm_parser_append_data_integer(state, encoded_value, element_size, token)) {
            return false;
        }
        vm_parser_advance(state);
        *out_element_count = 1U;
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_STRING) {
        if (data_type != VM_SYMBOL_DATA_TYPE_BYTE) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, token, "String initializers are supported only for BYTE and DB declarations.");
            return false;
        }
        if (!vm_parser_append_string_bytes(state, token, out_element_count)) {
            return false;
        }
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_QUESTION) {
        if (!vm_parser_append_data_integer(state, 0U, element_size, token)) {
            return false;
        }
        vm_parser_advance(state);
        *out_element_count = 1U;
        *out_has_uninitialized = true;
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, token, "Expected a number, string, DUP, or ? initializer.");
    return false;
}

/// Parses a COUNT DUP(initializer) data initializer.
///
/// @param state Parser state to mutate.
/// @param data_type Declared data type.
/// @param repeat_token Count token at the start of the DUP form.
/// @param out_element_count Receives total declared element count appended.
/// @param out_has_uninitialized Receives true when the inner initializer used `?`.
/// @return true when the DUP initializer was parsed and expanded.
static bool vm_parser_parse_dup_initializer(
    VmParserState *state,
    VmSymbolDataType data_type,
    const VmLexerToken *repeat_token,
    uint32_t *out_element_count,
    bool *out_has_uninitialized
) {
    size_t instance_start = 0U;
    size_t instance_size = 0U;
    uint64_t repeat_count = 0U;
    uint32_t instance_elements = 0U;
    bool instance_uninitialized = false;
    const VmLexerToken *token = NULL;
    uint64_t repeat_index = 0U;

    if (state == NULL || repeat_token == NULL || out_element_count == NULL || out_has_uninitialized == NULL) {
        return false;
    }

    repeat_count = repeat_token->number_value;
    if (repeat_token->number_is_negative || repeat_count == 0U || repeat_count > UINT32_MAX) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_DUP, repeat_token, "DUP repeat count must be between 1 and UINT32_MAX.");
        return false;
    }

    vm_parser_advance(state);
    token = vm_parser_current_token(state);
    if (token == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_parser_token_equals(token, "DUP")) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_DUP, token, "Expected DUP after repeat count.");
        return false;
    }
    vm_parser_advance(state);

    token = vm_parser_current_token(state);
    if (token == NULL || token->kind != VM_LEXER_TOKEN_LEFT_PAREN) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_DUP, token, "Expected '(' after DUP.");
        return false;
    }
    vm_parser_advance(state);

    instance_start = (size_t)state->data_offset;
    if (!vm_parser_parse_single_data_initializer(state, data_type, &instance_elements, &instance_uninitialized)) {
        return false;
    }
    instance_size = (size_t)state->data_offset - instance_start;

    token = vm_parser_current_token(state);
    if (token == NULL || token->kind != VM_LEXER_TOKEN_RIGHT_PAREN) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_DUP, token, "Expected ')' after DUP initializer.");
        return false;
    }
    vm_parser_advance(state);

    for (repeat_index = 1U; repeat_index < repeat_count; repeat_index += 1U) {
        size_t byte_index = 0U;
        for (byte_index = 0U; byte_index < instance_size; byte_index += 1U) {
            if (!vm_parser_append_data_byte(state, state->config->data_image[instance_start + byte_index], repeat_token)) {
                return false;
            }
        }
    }

    *out_element_count = (uint32_t)(repeat_count * (uint64_t)instance_elements);
    *out_has_uninitialized = instance_uninitialized;
    return true;
}

/// Parses one .data initializer, including flat DUP forms.
///
/// @param state Parser state to mutate.
/// @param data_type Declared data type.
/// @param out_element_count Receives number of declared elements appended.
/// @param out_has_uninitialized Receives true when `?` was parsed.
/// @return true when the initializer was parsed.
static bool vm_parser_parse_data_initializer(
    VmParserState *state,
    VmSymbolDataType data_type,
    uint32_t *out_element_count,
    bool *out_has_uninitialized
) {
    const VmLexerToken *token = vm_parser_current_token(state);
    const VmLexerToken *next = vm_parser_peek_token(state, 1U);

    if (token != NULL && next != NULL && token->kind == VM_LEXER_TOKEN_NUMBER && next->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(next, "DUP")) {
        return vm_parser_parse_dup_initializer(state, data_type, token, out_element_count, out_has_uninitialized);
    }

    return vm_parser_parse_single_data_initializer(state, data_type, out_element_count, out_has_uninitialized);
}

/// Parses one .data declaration line.
///
/// @param state Parser state to mutate.
/// @return true when the declaration was parsed and stored.
static bool vm_parser_parse_data_declaration(VmParserState *state) {
    const VmLexerToken *name_token = vm_parser_current_token(state);
    const VmLexerToken *type_token = vm_parser_peek_token(state, 1U);
    VmSymbolDataType data_type = VM_SYMBOL_DATA_TYPE_BYTE;
    VmSymbol symbol;
    uint32_t total_elements = 0U;
    bool has_uninitialized = false;
    uint32_t start_offset = 0U;
    uint32_t start_data_size = 0U;

    if (state == NULL || name_token == NULL || name_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_DECLARATION, name_token, "Expected symbol name in .data declaration.");
        return false;
    }

    if (type_token == NULL || type_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_symbol_parse_data_type(type_token->lexeme, type_token->lexeme_length, &data_type)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_DATA_TYPE, type_token, "Expected BYTE, WORD, DWORD, QWORD, DB, DW, DD, or DQ data type.");
        return false;
    }

    if (vm_symbol_find_by_name(state->config->symbols, state->result->symbol_count, name_token->lexeme, name_token->lexeme_length) != NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DUPLICATE_SYMBOL, name_token, "Duplicate data symbol name.");
        return false;
    }

    if (state->config->symbols == NULL || state->result->symbol_count >= state->config->symbol_capacity) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_CAPACITY_EXCEEDED, name_token, "Data symbol capacity exceeded.");
        state->stop_status = VM_PARSER_STATUS_SYMBOL_CAPACITY_EXCEEDED;
        return false;
    }

    memset(&symbol, 0, sizeof(symbol));
    if (!vm_symbol_set_name(&symbol, name_token->lexeme, name_token->lexeme_length)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_DECLARATION, name_token, "Symbol name is too long for the current fixed symbol table.");
        return false;
    }

    symbol.data_type = data_type;
    symbol.element_size_bytes = vm_symbol_data_type_size_bytes(data_type);
    symbol.address = VM_MEMORY_DEFAULT_DATA_BASE + state->data_offset;

    vm_parser_advance(state);
    vm_parser_advance(state);
    start_offset = state->data_offset;
    start_data_size = state->result->data_size;

    if (vm_parser_is_line_end_token(vm_parser_current_token(state))) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, type_token, "Expected at least one .data initializer.");
        return false;
    }

    while (true) {
        uint32_t initializer_elements = 0U;
        bool initializer_uninitialized = false;
        const VmLexerToken *separator = NULL;

        if (!vm_parser_parse_data_initializer(state, data_type, &initializer_elements, &initializer_uninitialized)) {
            state->data_offset = start_offset;
            state->result->data_size = start_data_size;
            return false;
        }
        total_elements += initializer_elements;
        has_uninitialized = has_uninitialized || initializer_uninitialized;

        separator = vm_parser_current_token(state);
        if (separator != NULL && separator->kind == VM_LEXER_TOKEN_COMMA) {
            vm_parser_advance(state);
            continue;
        }
        break;
    }

    if (!vm_parser_is_line_end_token(vm_parser_current_token(state))) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_LINE_END, vm_parser_current_token(state), "Expected end of line after .data declaration.");
        state->data_offset = start_offset;
        state->result->data_size = start_data_size;
        return false;
    }

    symbol.size_bytes = state->data_offset - start_offset;
    symbol.element_count = total_elements;
    symbol.has_uninitialized_initializer = has_uninitialized;
    state->config->symbols[state->result->symbol_count] = symbol;
    state->result->symbol_count += 1U;

    if (vm_parser_current_token(state) != NULL && vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_NEWLINE) {
        vm_parser_advance(state);
    }
    return true;
}

/// Parses a Milestone opcode token.
///
/// @param token Token containing a mnemonic.
/// @param out_opcode Receives the IR opcode on success.
/// @return true when the mnemonic is supported.
static bool vm_parser_parse_opcode(const VmLexerToken *token, VmIrOpcode *out_opcode) {
    if (token == NULL || out_opcode == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return false;
    }

    if (vm_parser_token_equals(token, "mov")) {
        *out_opcode = VM_IR_OPCODE_MOV;
        return true;
    }
    if (vm_parser_token_equals(token, "add")) {
        *out_opcode = VM_IR_OPCODE_ADD;
        return true;
    }
    if (vm_parser_token_equals(token, "sub")) {
        *out_opcode = VM_IR_OPCODE_SUB;
        return true;
    }

    return false;
}

/// Converts a numeric token into a signed byte offset.
///
/// Offset literals remain deliberately simple: one numeric token is accepted,
/// including lexer-supported negative numeric tokens. Full arithmetic expression
/// parsing remains a later milestone.
///
/// @param token Number token to convert.
/// @param out_offset Receives the signed byte offset.
/// @return true when the token fits in a signed 32-bit byte offset.
static bool vm_parser_number_to_i32_offset(const VmLexerToken *token, int32_t *out_offset) {
    if (token == NULL || out_offset == NULL || token->kind != VM_LEXER_TOKEN_NUMBER) {
        return false;
    }

    if (token->number_is_negative) {
        if (token->number_value > 0x80000000ULL) {
            return false;
        }
        if (token->number_value == 0x80000000ULL) {
            *out_offset = (int32_t)INT32_MIN;
        } else {
            *out_offset = -(int32_t)token->number_value;
        }
        return true;
    }

    if (token->number_value > 0x7FFFFFFFULL) {
        return false;
    }
    *out_offset = (int32_t)token->number_value;
    return true;
}

/// Resolves a data symbol token.
///
/// @param state Parser state to inspect.
/// @param token Symbol token to resolve.
/// @return Matching symbol, or NULL after recording a diagnostic.
static const VmSymbol *vm_parser_resolve_symbol(VmParserState *state, const VmLexerToken *token) {
    const VmSymbol *symbol = NULL;

    if (state == NULL || token == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return NULL;
    }

    symbol = vm_symbol_find_by_name(state->config->symbols, state->result->symbol_count, token->lexeme, token->lexeme_length);
    if (symbol == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, token, "Unknown data symbol.");
    }

    return symbol;
}

/// Creates a memory operand for a symbol plus constant byte offset.
///
/// The offset is validated against the current .data image byte span. Bracketed
/// offsets use MASM byte-offset semantics; they are not element indexes.
///
/// @param state Parser state to inspect.
/// @param symbol_token Symbol token used for diagnostics.
/// @param offset_token Token containing the offset, or NULL for direct symbol use.
/// @param offset Signed byte offset relative to the symbol address.
/// @param explicit_width_bits Optional PTR override width in bits, or zero to infer from the symbol.
/// @param width_token Optional width-token used for PTR diagnostics.
/// @param out_operand Receives a memory-address operand on success.
/// @return true when the operand was resolved and is executable by the current VM.
static bool vm_parser_build_symbol_offset_memory_operand(
    VmParserState *state,
    const VmLexerToken *symbol_token,
    const VmLexerToken *offset_token,
    int32_t offset,
    uint8_t explicit_width_bits,
    const VmLexerToken *width_token,
    VmIrOperand *out_operand
) {
    const VmSymbol *symbol = NULL;
    uint8_t width_bits = 0U;
    uint32_t width_bytes = 0U;
    uint32_t data_relative_offset = 0U;
    uint32_t final_address = 0U;
    int64_t final_address_signed = 0;
    uint64_t access_end = 0U;
    uint64_t data_end = 0U;

    if (state == NULL || symbol_token == NULL || out_operand == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return false;
    }

    symbol = vm_parser_resolve_symbol(state, symbol_token);
    if (symbol == NULL) {
        return false;
    }

    if (explicit_width_bits == 64U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, width_token != NULL ? width_token : symbol_token, "QWORD PTR execution is deferred until Extended 32-bit Mode.");
        return false;
    }

    if (explicit_width_bits == 0U && symbol->element_size_bytes > 4U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, symbol_token, "QWORD memory operands are deferred until Extended 32-bit Mode.");
        return false;
    }

    width_bytes = explicit_width_bits != 0U ? (uint32_t)(explicit_width_bits / 8U) : (uint32_t)symbol->element_size_bytes;
    if (width_bytes == 0U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token != NULL ? offset_token : symbol_token, "Symbol offset has an unsupported zero-width access.");
        return false;
    }

    final_address_signed = (int64_t)(uint64_t)symbol->address + (int64_t)offset;
    if (final_address_signed < (int64_t)(uint64_t)VM_MEMORY_DEFAULT_DATA_BASE || final_address_signed > (int64_t)UINT32_MAX) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token != NULL ? offset_token : symbol_token, "Symbol offset resolves outside the current .data image.");
        return false;
    }

    final_address = (uint32_t)final_address_signed;
    data_relative_offset = final_address - VM_MEMORY_DEFAULT_DATA_BASE;
    access_end = (uint64_t)data_relative_offset + (uint64_t)width_bytes;
    data_end = (uint64_t)state->result->data_size;
    if (access_end > data_end) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token != NULL ? offset_token : symbol_token, "Symbol offset access extends beyond the current .data image.");
        return false;
    }

    width_bits = explicit_width_bits != 0U ? explicit_width_bits : (uint8_t)(symbol->element_size_bytes * 8U);
    if (!vm_ir_width_is_supported(width_bits)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, width_token != NULL ? width_token : symbol_token, "PTR width override is not executable in the current MASM32 subset.");
        return false;
    }

    *out_operand = vm_ir_operand_memory(final_address, width_bits);
    return true;
}

/// Creates a memory operand for a direct data symbol token.
///
/// @param state Parser state to inspect.
/// @param token Symbol token to resolve.
/// @param out_operand Receives a memory-address operand on success.
/// @return true when the symbol was resolved and is executable by the current VM.
static bool vm_parser_parse_symbol_memory_operand(VmParserState *state, const VmLexerToken *token, VmIrOperand *out_operand) {
    return vm_parser_build_symbol_offset_memory_operand(state, token, NULL, 0, 0U, NULL, out_operand);
}

/// Returns whether a register is allowed as a Milestone 11 memory base.
///
/// @param token Register token to inspect.
/// @return true for ESI, EDI, EBX, and EBP.
static bool vm_parser_token_is_register_indirect_base(const VmLexerToken *token) {
    if (token == NULL || token->kind != VM_LEXER_TOKEN_REGISTER) {
        return false;
    }

    return token->register_id == VM_REGISTER_ESI ||
           token->register_id == VM_REGISTER_EDI ||
           token->register_id == VM_REGISTER_EBX ||
           token->register_id == VM_REGISTER_EBP;
}

/// Records the stable diagnostic for unsupported scaled-index memory syntax.
///
/// @param state Parser state whose diagnostic buffer should receive the entry.
/// @param token Token associated with the scaled-index operator.
static void vm_parser_add_scaled_index_diagnostic(VmParserState *state, const VmLexerToken *token) {
    vm_parser_add_diagnostic(
        state,
        VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX,
        token,
        "Scaled-index memory operands are not supported yet."
    );
}

/// Creates a register-indirect memory operand with optional symbol base.
///
/// When @p symbol_token is present, its symbol address contributes the static
/// base and the symbol declaration supplies the default width when no PTR width
/// was provided. When no symbol is present, the width may remain zero so later
/// parser validation can infer it from the opposite operand.
///
/// @param state Parser state to inspect.
/// @param base_token Register token used as the runtime base.
/// @param displacement Signed byte displacement added to the effective address.
/// @param symbol_token Optional data symbol that contributes a static base address.
/// @param explicit_width_bits Optional PTR width override in bits, or zero.
/// @param width_token Optional PTR width token used for diagnostics.
/// @param out_operand Receives the register-indirect memory operand.
/// @return true when the operand was parsed and width metadata is acceptable.
static bool vm_parser_build_register_memory_operand(
    VmParserState *state,
    const VmLexerToken *base_token,
    int32_t displacement,
    const VmLexerToken *symbol_token,
    uint8_t explicit_width_bits,
    const VmLexerToken *width_token,
    VmIrOperand *out_operand
) {
    const VmSymbol *symbol = NULL;
    uint32_t static_address = 0U;
    uint8_t width_bits = explicit_width_bits;

    if (state == NULL || base_token == NULL || out_operand == NULL || !vm_parser_token_is_register_indirect_base(base_token)) {
        return false;
    }

    if (explicit_width_bits == 64U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, width_token != NULL ? width_token : base_token, "QWORD PTR execution is deferred until Extended 32-bit Mode.");
        return false;
    }

    if (symbol_token != NULL) {
        symbol = vm_parser_resolve_symbol(state, symbol_token);
        if (symbol == NULL) {
            return false;
        }
        if (width_bits == 0U) {
            if (symbol->element_size_bytes > 4U) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, symbol_token, "QWORD memory operands are deferred until Extended 32-bit Mode.");
                return false;
            }
            width_bits = (uint8_t)(symbol->element_size_bytes * 8U);
        }
        static_address = symbol->address;
    }

    if (width_bits != 0U && !vm_ir_width_is_supported(width_bits)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, width_token != NULL ? width_token : base_token, "PTR width override is not executable in the current MASM32 subset.");
        return false;
    }

    *out_operand = vm_ir_operand_memory_register(base_token->register_id, displacement, static_address, width_bits);
    return true;
}

/// Parses an optional displacement after a bracketed register base.
///
/// Supported forms are no displacement, `+ number`, and `- number`. Scaled
/// forms using `*` are rejected with an explicit unsupported-feature diagnostic.
///
/// @param state Parser state positioned after the register token.
/// @param out_displacement Receives the signed byte displacement.
/// @return true when a valid suffix was parsed and the current token is `]`.
static bool vm_parser_parse_register_displacement_suffix(VmParserState *state, int32_t *out_displacement) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (state == NULL || out_displacement == NULL) {
        return false;
    }

    *out_displacement = 0;
    if (token != NULL && token->kind == VM_LEXER_TOKEN_RIGHT_BRACKET) {
        return true;
    }

    if (token != NULL && token->kind == VM_LEXER_TOKEN_ASTERISK) {
        vm_parser_add_scaled_index_diagnostic(state, token);
        return false;
    }

    if (token != NULL && (token->kind == VM_LEXER_TOKEN_PLUS || token->kind == VM_LEXER_TOKEN_MINUS)) {
        const VmLexerToken *operator_token = token;
        const VmLexerToken *number_token = NULL;
        int32_t magnitude = 0;

        vm_parser_advance(state);
        number_token = vm_parser_current_token(state);
        if (number_token == NULL || number_token->kind != VM_LEXER_TOKEN_NUMBER || number_token->number_is_negative) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, number_token != NULL ? number_token : operator_token, "Expected a non-negative numeric displacement after + or -.");
            return false;
        }
        if (!vm_parser_number_to_i32_offset(number_token, &magnitude)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, number_token, "Register displacement is outside the supported signed 32-bit range.");
            return false;
        }
        *out_displacement = operator_token->kind == VM_LEXER_TOKEN_MINUS ? -magnitude : magnitude;
        vm_parser_advance(state);
        if (vm_parser_current_token(state) != NULL && vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_ASTERISK) {
            vm_parser_add_scaled_index_diagnostic(state, vm_parser_current_token(state));
            return false;
        }
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected ], + displacement, or - displacement after register memory base.");
    return false;
}

/// Parses `[reg]`, `[reg + constant]`, and `[reg - constant]` memory operands.
///
/// @param state Parser state positioned at the left bracket.
/// @param width Optional PTR width metadata, or NULL when no PTR prefix was present.
/// @param out_operand Receives the register-indirect memory operand.
/// @return true when a supported register-indirect operand was parsed.
static bool vm_parser_parse_bracketed_register_memory_operand(
    VmParserState *state,
    const VmParserPtrWidth *width,
    VmIrOperand *out_operand
) {
    const VmLexerToken *left_token = vm_parser_current_token(state);
    const VmLexerToken *base_token = NULL;
    int32_t displacement = 0;

    if (state == NULL || out_operand == NULL || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    base_token = vm_parser_current_token(state);
    if (base_token == NULL || base_token->kind != VM_LEXER_TOKEN_REGISTER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, base_token != NULL ? base_token : left_token, "Expected a register memory base after '['.");
        return false;
    }
    if (!vm_parser_token_is_register_indirect_base(base_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, base_token, "Unsupported register-indirect base register. Use ESI, EDI, EBX, or EBP.");
        return false;
    }

    vm_parser_advance(state);
    if (!vm_parser_parse_register_displacement_suffix(state, &displacement)) {
        return false;
    }

    if (vm_parser_current_token(state) == NULL || vm_parser_current_token(state)->kind != VM_LEXER_TOKEN_RIGHT_BRACKET) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, vm_parser_current_token(state), "Expected ']' after register-indirect memory operand.");
        return false;
    }

    if (!vm_parser_build_register_memory_operand(
            state,
            base_token,
            displacement,
            NULL,
            width != NULL ? width->width_bits : 0U,
            width != NULL ? width->width_token : NULL,
            out_operand)) {
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Parses `symbol[reg]` memory operands.
///
/// @param state Parser state positioned at the symbol token.
/// @param width Optional PTR width metadata, or NULL when no PTR prefix was present.
/// @param out_operand Receives the symbol-plus-register memory operand.
/// @return true when the operand was parsed.
static bool vm_parser_parse_symbol_register_index_memory_operand(
    VmParserState *state,
    const VmParserPtrWidth *width,
    VmIrOperand *out_operand
) {
    const VmLexerToken *symbol_token = vm_parser_current_token(state);
    const VmLexerToken *left_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *base_token = NULL;

    if (state == NULL || out_operand == NULL || symbol_token == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    base_token = vm_parser_current_token(state);
    if (base_token == NULL || base_token->kind != VM_LEXER_TOKEN_REGISTER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, base_token != NULL ? base_token : left_token, "Expected a register inside symbol brackets.");
        return false;
    }
    if (!vm_parser_token_is_register_indirect_base(base_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, base_token, "Unsupported register index. Use ESI, EDI, EBX, or EBP.");
        return false;
    }

    vm_parser_advance(state);
    if (vm_parser_current_token(state) != NULL && vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_ASTERISK) {
        vm_parser_add_scaled_index_diagnostic(state, vm_parser_current_token(state));
        return false;
    }
    if (vm_parser_current_token(state) == NULL || vm_parser_current_token(state)->kind != VM_LEXER_TOKEN_RIGHT_BRACKET) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, vm_parser_current_token(state), "Expected ']' after symbol register operand.");
        return false;
    }

    if (!vm_parser_build_register_memory_operand(
            state,
            base_token,
            0,
            symbol_token,
            width != NULL ? width->width_bits : 0U,
            width != NULL ? width->width_token : NULL,
            out_operand)) {
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Parses `[symbol + reg]` memory operands.
///
/// @param state Parser state positioned at the left bracket.
/// @param width Optional PTR width metadata, or NULL when no PTR prefix was present.
/// @param out_operand Receives the symbol-plus-register memory operand.
/// @return true when the operand was parsed.
static bool vm_parser_parse_bracketed_symbol_register_memory_operand(
    VmParserState *state,
    const VmParserPtrWidth *width,
    VmIrOperand *out_operand
) {
    const VmLexerToken *left_token = vm_parser_current_token(state);
    const VmLexerToken *symbol_token = NULL;
    const VmLexerToken *plus_token = NULL;
    const VmLexerToken *base_token = NULL;

    if (state == NULL || out_operand == NULL || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    symbol_token = vm_parser_current_token(state);
    plus_token = vm_parser_peek_token(state, 1U);
    base_token = vm_parser_peek_token(state, 2U);
    if (symbol_token == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER || plus_token == NULL || plus_token->kind != VM_LEXER_TOKEN_PLUS || base_token == NULL || base_token->kind != VM_LEXER_TOKEN_REGISTER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, symbol_token != NULL ? symbol_token : left_token, "Expected [symbol + register] memory operand.");
        return false;
    }
    if (!vm_parser_token_is_register_indirect_base(base_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, base_token, "Unsupported register index. Use ESI, EDI, EBX, or EBP.");
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    vm_parser_advance(state);
    if (vm_parser_current_token(state) != NULL && vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_ASTERISK) {
        vm_parser_add_scaled_index_diagnostic(state, vm_parser_current_token(state));
        return false;
    }
    if (vm_parser_current_token(state) == NULL || vm_parser_current_token(state)->kind != VM_LEXER_TOKEN_RIGHT_BRACKET) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, vm_parser_current_token(state), "Expected ']' after symbol register operand.");
        return false;
    }

    if (!vm_parser_build_register_memory_operand(
            state,
            base_token,
            0,
            symbol_token,
            width != NULL ? width->width_bits : 0U,
            width != NULL ? width->width_token : NULL,
            out_operand)) {
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Parses an optional signed offset after a bracketed symbol token.
///
/// Supported forms are empty offset-zero brackets, `+ number`, `- number`,
/// and a negative number token used by compact forms such as `[nums-4]`. Full
/// arithmetic expression parsing remains a later milestone.
///
/// @param state Parser state positioned after the symbol token.
/// @param out_offset Receives the signed offset.
/// @param out_offset_token Receives the token most relevant for diagnostics.
/// @return true when an offset suffix was parsed.
static bool vm_parser_parse_bracket_symbol_offset_suffix(VmParserState *state, int32_t *out_offset, const VmLexerToken **out_offset_token) {
    const VmLexerToken *token = vm_parser_current_token(state);
    int32_t offset = 0;

    if (state == NULL || out_offset == NULL || out_offset_token == NULL) {
        return false;
    }

    *out_offset = 0;
    *out_offset_token = token;

    if (token != NULL && token->kind == VM_LEXER_TOKEN_RIGHT_BRACKET) {
        *out_offset = 0;
        *out_offset_token = NULL;
        return true;
    }

    if (token != NULL && token->kind == VM_LEXER_TOKEN_NUMBER && token->number_is_negative) {
        if (!vm_parser_number_to_i32_offset(token, &offset)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, token, "Symbol offset is outside the supported signed 32-bit range.");
            return false;
        }
        *out_offset = offset;
        *out_offset_token = token;
        vm_parser_advance(state);
        return true;
    }

    if (token != NULL && (token->kind == VM_LEXER_TOKEN_PLUS || token->kind == VM_LEXER_TOKEN_MINUS)) {
        const VmLexerToken *operator_token = token;
        const VmLexerToken *number_token = NULL;
        int32_t magnitude = 0;

        vm_parser_advance(state);
        number_token = vm_parser_current_token(state);
        if (number_token == NULL || number_token->kind != VM_LEXER_TOKEN_NUMBER || number_token->number_is_negative) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, number_token != NULL ? number_token : operator_token, "Expected a non-negative numeric symbol offset after + or -." );
            return false;
        }

        if (!vm_parser_number_to_i32_offset(number_token, &magnitude)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, number_token, "Symbol offset is outside the supported signed 32-bit range.");
            return false;
        }

        *out_offset = operator_token->kind == VM_LEXER_TOKEN_MINUS ? -magnitude : magnitude;
        *out_offset_token = number_token;
        vm_parser_advance(state);
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected + offset or - offset after bracketed data symbol.");
    return false;
}

/// Parses `[symbol]`, `[symbol + constant]`, and `[symbol - constant]` memory operands.
///
/// @param state Parser state positioned at the left bracket.
/// @param out_operand Receives a memory-address operand on success.
/// @return true when a bracketed symbol-offset operand was parsed.
static bool vm_parser_parse_bracketed_symbol_offset_memory_operand(VmParserState *state, VmIrOperand *out_operand) {
    const VmLexerToken *left_token = vm_parser_current_token(state);
    const VmLexerToken *symbol_token = NULL;
    const VmLexerToken *offset_token = NULL;
    int32_t offset = 0;

    if (state == NULL || out_operand == NULL || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    symbol_token = vm_parser_current_token(state);
    if (symbol_token == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, symbol_token != NULL ? symbol_token : left_token, "Expected a data symbol after '['.");
        return false;
    }
    vm_parser_advance(state);

    if (!vm_parser_parse_bracket_symbol_offset_suffix(state, &offset, &offset_token)) {
        return false;
    }

    if (vm_parser_current_token(state) == NULL || vm_parser_current_token(state)->kind != VM_LEXER_TOKEN_RIGHT_BRACKET) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, vm_parser_current_token(state), "Expected ']' after symbol offset operand.");
        return false;
    }

    if (!vm_parser_build_symbol_offset_memory_operand(state, symbol_token, offset_token, offset, 0U, NULL, out_operand)) {
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Parses `symbol[constant]` memory operands.
///
/// @param state Parser state positioned at the symbol token.
/// @param out_operand Receives a memory-address operand on success.
/// @return true when a symbol-indexed constant operand was parsed.
static bool vm_parser_parse_symbol_index_memory_operand(VmParserState *state, VmIrOperand *out_operand) {
    const VmLexerToken *symbol_token = vm_parser_current_token(state);
    const VmLexerToken *left_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *offset_token = NULL;
    int32_t offset = 0;

    if (state == NULL || out_operand == NULL || symbol_token == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    offset_token = vm_parser_current_token(state);
    if (offset_token == NULL || offset_token->kind != VM_LEXER_TOKEN_NUMBER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, offset_token != NULL ? offset_token : left_token, "Expected a constant byte offset inside symbol brackets.");
        return false;
    }

    if (!vm_parser_number_to_i32_offset(offset_token, &offset)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token, "Symbol offset is outside the supported signed 32-bit range.");
        return false;
    }
    vm_parser_advance(state);

    if (vm_parser_current_token(state) == NULL || vm_parser_current_token(state)->kind != VM_LEXER_TOKEN_RIGHT_BRACKET) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, vm_parser_current_token(state), "Expected ']' after symbol offset operand.");
        return false;
    }

    if (!vm_parser_build_symbol_offset_memory_operand(state, symbol_token, offset_token, offset, 0U, NULL, out_operand)) {
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Parses a MASM PTR width keyword.
///
/// @param token Token to inspect.
/// @param out_width_bits Receives 8, 16, 32, or 64 on success.
/// @return true when @p token is BYTE, WORD, DWORD, or QWORD.
static bool vm_parser_parse_ptr_width_keyword(const VmLexerToken *token, uint8_t *out_width_bits) {
    if (token == NULL || out_width_bits == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return false;
    }

    if (vm_parser_token_equals(token, "BYTE")) {
        *out_width_bits = 8U;
        return true;
    }
    if (vm_parser_token_equals(token, "WORD")) {
        *out_width_bits = 16U;
        return true;
    }
    if (vm_parser_token_equals(token, "DWORD")) {
        *out_width_bits = 32U;
        return true;
    }
    if (vm_parser_token_equals(token, "QWORD")) {
        *out_width_bits = 64U;
        return true;
    }

    return false;
}

/// Returns whether the current token begins a PTR width override prefix.
///
/// @param state Parser state to inspect.
/// @return true when the current token is a supported PTR width keyword.
static bool vm_parser_current_token_starts_ptr_width(const VmParserState *state) {
    uint8_t ignored_width = 0U;
    return vm_parser_parse_ptr_width_keyword(vm_parser_current_token(state), &ignored_width);
}

/// Returns whether the current token looks like an unsupported PTR width prefix.
///
/// This catches malformed forms such as `REAL4 PTR nums[0]` before the parser
/// misreports `REAL4` as an ordinary unknown data symbol.
///
/// @param state Parser state to inspect.
/// @return true when the current token is an identifier followed by PTR but is
/// not one of BYTE, WORD, DWORD, or QWORD.
static bool vm_parser_current_token_is_malformed_ptr_prefix(const VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);
    const VmLexerToken *next_token = vm_parser_peek_token(state, 1U);

    return token != NULL &&
           token->kind == VM_LEXER_TOKEN_IDENTIFIER &&
           next_token != NULL &&
           next_token->kind == VM_LEXER_TOKEN_IDENTIFIER &&
           vm_parser_token_equals(next_token, "PTR") &&
           !vm_parser_current_token_starts_ptr_width(state);
}

/// Parses a PTR width prefix without parsing its following memory operand.
///
/// @param state Parser state positioned at BYTE, WORD, DWORD, or QWORD.
/// @param out_width Receives the parsed width metadata.
/// @return true when WIDTH PTR was consumed.
static bool vm_parser_parse_ptr_width_prefix(VmParserState *state, VmParserPtrWidth *out_width) {
    const VmLexerToken *width_token = vm_parser_current_token(state);
    const VmLexerToken *ptr_token = vm_parser_peek_token(state, 1U);
    uint8_t width_bits = 0U;

    if (state == NULL || out_width == NULL || !vm_parser_parse_ptr_width_keyword(width_token, &width_bits)) {
        return false;
    }

    if (ptr_token == NULL || ptr_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_parser_token_equals(ptr_token, "PTR")) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, width_token, "Expected PTR after memory width override.");
        return false;
    }

    out_width->width_bits = width_bits;
    out_width->width_token = width_token;
    out_width->ptr_token = ptr_token;
    vm_parser_advance(state);
    vm_parser_advance(state);
    return true;
}

/// Creates a memory operand for a direct data symbol with an explicit width.
///
/// @param state Parser state to inspect.
/// @param token Symbol token to resolve.
/// @param width Parsed PTR width metadata.
/// @param out_operand Receives a memory-address operand on success.
/// @return true when the symbol was resolved and the width is executable.
static bool vm_parser_parse_symbol_memory_operand_with_width(
    VmParserState *state,
    const VmLexerToken *token,
    const VmParserPtrWidth *width,
    VmIrOperand *out_operand
) {
    return vm_parser_build_symbol_offset_memory_operand(
        state,
        token,
        NULL,
        0,
        width != NULL ? width->width_bits : 0U,
        width != NULL ? width->width_token : NULL,
        out_operand
    );
}

/// Parses `[symbol]`, `[symbol + constant]`, and `[symbol - constant]` with explicit width.
///
/// @param state Parser state positioned at the left bracket.
/// @param width Parsed PTR width metadata.
/// @param out_operand Receives a memory-address operand on success.
/// @return true when a bracketed symbol-offset operand was parsed.
static bool vm_parser_parse_bracketed_symbol_offset_memory_operand_with_width(
    VmParserState *state,
    const VmParserPtrWidth *width,
    VmIrOperand *out_operand
) {
    const VmLexerToken *left_token = vm_parser_current_token(state);
    const VmLexerToken *symbol_token = NULL;
    const VmLexerToken *offset_token = NULL;
    int32_t offset = 0;

    if (state == NULL || width == NULL || out_operand == NULL || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    symbol_token = vm_parser_current_token(state);
    if (symbol_token == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, symbol_token != NULL ? symbol_token : left_token, "Expected a data symbol after '['.");
        return false;
    }
    vm_parser_advance(state);

    if (!vm_parser_parse_bracket_symbol_offset_suffix(state, &offset, &offset_token)) {
        return false;
    }

    if (vm_parser_current_token(state) == NULL || vm_parser_current_token(state)->kind != VM_LEXER_TOKEN_RIGHT_BRACKET) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, vm_parser_current_token(state), "Expected ']' after symbol offset operand.");
        return false;
    }

    if (!vm_parser_build_symbol_offset_memory_operand(state, symbol_token, offset_token, offset, width->width_bits, width->width_token, out_operand)) {
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Parses `symbol[constant]` with an explicit PTR width.
///
/// @param state Parser state positioned at the symbol token.
/// @param width Parsed PTR width metadata.
/// @param out_operand Receives a memory-address operand on success.
/// @return true when a symbol-indexed constant operand was parsed.
static bool vm_parser_parse_symbol_index_memory_operand_with_width(
    VmParserState *state,
    const VmParserPtrWidth *width,
    VmIrOperand *out_operand
) {
    const VmLexerToken *symbol_token = vm_parser_current_token(state);
    const VmLexerToken *left_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *offset_token = NULL;
    int32_t offset = 0;

    if (state == NULL || width == NULL || out_operand == NULL || symbol_token == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    offset_token = vm_parser_current_token(state);
    if (offset_token == NULL || offset_token->kind != VM_LEXER_TOKEN_NUMBER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, offset_token != NULL ? offset_token : left_token, "Expected a constant byte offset inside symbol brackets.");
        return false;
    }

    if (!vm_parser_number_to_i32_offset(offset_token, &offset)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token, "Symbol offset is outside the supported signed 32-bit range.");
        return false;
    }
    vm_parser_advance(state);

    if (vm_parser_current_token(state) == NULL || vm_parser_current_token(state)->kind != VM_LEXER_TOKEN_RIGHT_BRACKET) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, vm_parser_current_token(state), "Expected ']' after symbol offset operand.");
        return false;
    }

    if (!vm_parser_build_symbol_offset_memory_operand(state, symbol_token, offset_token, offset, width->width_bits, width->width_token, out_operand)) {
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Parses a memory operand following a WIDTH PTR prefix.
///
/// @param state Parser state positioned after WIDTH PTR.
/// @param width Parsed PTR width metadata.
/// @param out_operand Receives the parsed memory operand.
/// @return true when the WIDTH PTR memory operand was parsed.
static bool vm_parser_parse_ptr_memory_operand(VmParserState *state, const VmParserPtrWidth *width, VmIrOperand *out_operand) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (state == NULL || width == NULL || out_operand == NULL) {
        return false;
    }

    if (token == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "PTR width override requires a memory operand.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_LEFT_BRACKET) {
        const VmLexerToken *inside_token = vm_parser_peek_token(state, 1U);
        const VmLexerToken *third_token = vm_parser_peek_token(state, 3U);
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_bracketed_register_memory_operand(state, width, out_operand);
        }
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_IDENTIFIER && third_token != NULL && third_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_bracketed_symbol_register_memory_operand(state, width, out_operand);
        }
        return vm_parser_parse_bracketed_symbol_offset_memory_operand_with_width(state, width, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_peek_token(state, 1U) != NULL && vm_parser_peek_token(state, 1U)->kind == VM_LEXER_TOKEN_LEFT_BRACKET) {
        const VmLexerToken *inside_token = vm_parser_peek_token(state, 2U);
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_symbol_register_index_memory_operand(state, width, out_operand);
        }
        return vm_parser_parse_symbol_index_memory_operand_with_width(state, width, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        if (!vm_parser_parse_symbol_memory_operand_with_width(state, token, width, out_operand)) {
            return false;
        }
        vm_parser_advance(state);
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "PTR width override requires a data symbol or supported symbol-offset memory operand.");
    return false;
}

/// Parses a register or direct-symbol destination operand.
///
/// @param state Parser state to mutate.
/// @param out_operand Receives the parsed IR operand.
/// @return true when a destination operand was parsed.
static bool vm_parser_parse_destination_operand(VmParserState *state, VmIrOperand *out_operand) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (out_operand == NULL) {
        return false;
    }

    if (token == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected a destination operand.");
        return false;
    }

    if (vm_parser_current_token_starts_ptr_width(state)) {
        VmParserPtrWidth width;
        memset(&width, 0, sizeof(width));
        if (!vm_parser_parse_ptr_width_prefix(state, &width)) {
            return false;
        }
        return vm_parser_parse_ptr_memory_operand(state, &width, out_operand);
    }

    if (vm_parser_current_token_is_malformed_ptr_prefix(state)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, token, "Unsupported PTR width override. Expected BYTE, WORD, DWORD, or QWORD before PTR.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_REGISTER) {
        *out_operand = vm_ir_operand_register(token->register_id, 0U);
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_LEFT_BRACKET) {
        const VmLexerToken *inside_token = vm_parser_peek_token(state, 1U);
        const VmLexerToken *third_token = vm_parser_peek_token(state, 3U);
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_bracketed_register_memory_operand(state, NULL, out_operand);
        }
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_IDENTIFIER && third_token != NULL && third_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_bracketed_symbol_register_memory_operand(state, NULL, out_operand);
        }
        return vm_parser_parse_bracketed_symbol_offset_memory_operand(state, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_peek_token(state, 1U) != NULL && vm_parser_peek_token(state, 1U)->kind == VM_LEXER_TOKEN_LEFT_BRACKET) {
        const VmLexerToken *inside_token = vm_parser_peek_token(state, 2U);
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_symbol_register_index_memory_operand(state, NULL, out_operand);
        }
        return vm_parser_parse_symbol_index_memory_operand(state, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        if (!vm_parser_parse_symbol_memory_operand(state, token, out_operand)) {
            return false;
        }
        vm_parser_advance(state);
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected a register, data symbol, constant symbol-offset, or register-indirect destination operand.");
    return false;
}

/// Parses a source operand that may be register, immediate, direct symbol, or OFFSET symbol.
///
/// @param state Parser state to mutate.
/// @param out_operand Receives the parsed IR operand.
/// @return true when a source operand was parsed.
static bool vm_parser_parse_source_operand(VmParserState *state, VmIrOperand *out_operand) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (out_operand == NULL) {
        return false;
    }

    if (token == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected a source operand.");
        return false;
    }

    if (vm_parser_current_token_starts_ptr_width(state)) {
        VmParserPtrWidth width;
        memset(&width, 0, sizeof(width));
        if (!vm_parser_parse_ptr_width_prefix(state, &width)) {
            return false;
        }
        return vm_parser_parse_ptr_memory_operand(state, &width, out_operand);
    }

    if (vm_parser_current_token_is_malformed_ptr_prefix(state)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, token, "Unsupported PTR width override. Expected BYTE, WORD, DWORD, or QWORD before PTR.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_REGISTER) {
        *out_operand = vm_ir_operand_register(token->register_id, 0U);
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_LEFT_BRACKET) {
        const VmLexerToken *inside_token = vm_parser_peek_token(state, 1U);
        const VmLexerToken *third_token = vm_parser_peek_token(state, 3U);
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_bracketed_register_memory_operand(state, NULL, out_operand);
        }
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_IDENTIFIER && third_token != NULL && third_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_bracketed_symbol_register_memory_operand(state, NULL, out_operand);
        }
        return vm_parser_parse_bracketed_symbol_offset_memory_operand(state, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_peek_token(state, 1U) != NULL && vm_parser_peek_token(state, 1U)->kind == VM_LEXER_TOKEN_LEFT_BRACKET) {
        const VmLexerToken *inside_token = vm_parser_peek_token(state, 2U);
        if (inside_token != NULL && inside_token->kind == VM_LEXER_TOKEN_REGISTER) {
            return vm_parser_parse_symbol_register_index_memory_operand(state, NULL, out_operand);
        }
        return vm_parser_parse_symbol_index_memory_operand(state, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_NUMBER) {
        uint32_t encoded_value = 0U;
        if (!vm_parser_encode_number_for_width(token, 32U, &encoded_value)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, token, "Immediate value exceeds the current 32-bit IR range.");
            return false;
        }
        *out_operand = vm_ir_operand_immediate(encoded_value, 0U);
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "OFFSET")) {
        const VmLexerToken *symbol_token = vm_parser_peek_token(state, 1U);
        const VmSymbol *symbol = NULL;
        if (symbol_token == NULL || symbol_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "OFFSET requires a following data symbol.");
            return false;
        }
        symbol = vm_symbol_find_by_name(state->config->symbols, state->result->symbol_count, symbol_token->lexeme, symbol_token->lexeme_length);
        if (symbol == NULL) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, symbol_token, "OFFSET references an unknown data symbol.");
            return false;
        }
        *out_operand = vm_ir_operand_immediate(symbol->address, 32U);
        vm_parser_advance(state);
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        if (!vm_parser_parse_symbol_memory_operand(state, token, out_operand)) {
            return false;
        }
        vm_parser_advance(state);
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected a register, immediate, data symbol, constant symbol-offset, register-indirect, or OFFSET source operand.");
    return false;
}

/// Resolves the parser-visible width for a register or memory operand.
///
/// @param operand Operand whose width should be resolved.
/// @param out_width_bits Receives the resolved width in bits on success.
/// @return true when the operand has a supported explicit width.
static bool vm_parser_resolve_operand_width(const VmIrOperand *operand, uint8_t *out_width_bits) {
    uint8_t width_bits = 0U;

    if (operand == NULL || out_width_bits == NULL) {
        return false;
    }

    if (operand->kind == VM_IR_OPERAND_REGISTER) {
        width_bits = operand->width_bits != 0U ? operand->width_bits : vm_cpu_register_width_bits(operand->reg);
    } else if (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS || operand->kind == VM_IR_OPERAND_MEMORY_REGISTER) {
        width_bits = operand->width_bits;
    } else {
        return false;
    }

    if (!vm_ir_width_is_supported(width_bits)) {
        return false;
    }

    *out_width_bits = width_bits;
    return true;
}

/// Returns whether an operand is any parser-supported memory operand.
///
/// @param operand Operand to inspect.
/// @return true for absolute and register-indirect memory operands.
static bool vm_parser_operand_is_memory(const VmIrOperand *operand) {
    return operand != NULL &&
           (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS || operand->kind == VM_IR_OPERAND_MEMORY_REGISTER);
}

/// Infers missing register-indirect memory widths from the opposite operand.
///
/// Register-only memory operands such as `[esi]` have no declaration width.
/// This helper keeps them usable in textbook forms such as `mov eax, [esi]`
/// and `mov [edi], al` without allowing ambiguous immediate-to-memory writes.
///
/// @param destination Destination operand to update if its width is inferable.
/// @param source Source operand to update if its width is inferable.
static void vm_parser_infer_register_memory_widths(VmIrOperand *destination, VmIrOperand *source) {
    uint8_t opposite_width = 0U;

    if (destination == NULL || source == NULL) {
        return;
    }

    if (destination->kind == VM_IR_OPERAND_MEMORY_REGISTER && destination->width_bits == 0U) {
        if (vm_parser_resolve_operand_width(source, &opposite_width)) {
            destination->width_bits = opposite_width;
        }
    }

    if (source->kind == VM_IR_OPERAND_MEMORY_REGISTER && source->width_bits == 0U) {
        if (vm_parser_resolve_operand_width(destination, &opposite_width)) {
            source->width_bits = opposite_width;
        }
    }
}

/// Returns the largest unsigned immediate value accepted for a destination width.
///
/// @param width_bits Destination width in bits.
/// @param out_max_value Receives the maximum accepted value.
/// @return true when @p width_bits is supported by the current parser slice.
static bool vm_parser_max_unsigned_for_width(uint8_t width_bits, uint32_t *out_max_value) {
    if (out_max_value == NULL) {
        return false;
    }

    switch (width_bits) {
        case 8U:
            *out_max_value = 0x000000FFU;
            return true;
        case 16U:
            *out_max_value = 0x0000FFFFU;
            return true;
        case 32U:
            *out_max_value = 0xFFFFFFFFU;
            return true;
        default:
            *out_max_value = 0U;
            return false;
    }
}

/// Returns the largest accepted negative magnitude for an operand width.
///
/// @param width_bits Destination width in bits.
/// @param out_max_magnitude Receives the accepted negative magnitude.
/// @return true when @p width_bits is supported by the current parser slice.
static bool vm_parser_max_negative_magnitude_for_width(uint8_t width_bits, uint32_t *out_max_magnitude) {
    if (out_max_magnitude == NULL) {
        return false;
    }

    switch (width_bits) {
        case 8U:
            *out_max_magnitude = 0x00000080U;
            return true;
        case 16U:
            *out_max_magnitude = 0x00008000U;
            return true;
        case 32U:
            *out_max_magnitude = 0x80000000U;
            return true;
        default:
            *out_max_magnitude = 0U;
            return false;
    }
}

/// Encodes a signed or unsigned lexer number token for an operand width.
///
/// Positive literals may use the full unsigned width. Negative literals must
/// fit the signed range and are encoded in two's-complement form.
///
/// @param token Number token to encode.
/// @param width_bits Destination width in bits.
/// @param out_value Receives the encoded 32-bit immediate value.
/// @return true when the token fits the requested width.
static bool vm_parser_encode_number_for_width(const VmLexerToken *token, uint8_t width_bits, uint32_t *out_value) {
    uint32_t unsigned_max = 0U;

    if (token == NULL || out_value == NULL || token->kind != VM_LEXER_TOKEN_NUMBER) {
        return false;
    }

    if (token->number_is_negative) {
        uint32_t max_magnitude = 0U;
        if (!vm_parser_max_negative_magnitude_for_width(width_bits, &max_magnitude) || token->number_value > (uint64_t)max_magnitude) {
            return false;
        }
        *out_value = 0U - (uint32_t)token->number_value;
        return true;
    }

    if (!vm_parser_max_unsigned_for_width(width_bits, &unsigned_max) || token->number_value > (uint64_t)unsigned_max) {
        return false;
    }
    *out_value = (uint32_t)token->number_value;
    return true;
}

/// Validates source width and immediate range for a two-operand instruction.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param destination Destination operand that selects the instruction width.
/// @param source Source operand to validate against the destination width.
/// @param source_token Token associated with the source operand for diagnostics.
/// @return true when the source can be represented without narrowing.
static bool vm_parser_validate_source_width(
    VmParserState *state,
    const VmIrOperand *destination,
    const VmIrOperand *source,
    const VmLexerToken *source_token
) {
    uint8_t destination_width = 0U;
    uint8_t source_width = 0U;
    uint32_t max_value = 0U;

    if (state == NULL || destination == NULL || source == NULL) {
        return false;
    }

    if (!vm_parser_resolve_operand_width(destination, &destination_width)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, source_token, "Destination operand width is unsupported by the current milestone.");
        return false;
    }

    if (source->kind == VM_IR_OPERAND_IMMEDIATE) {
        if (source->width_bits != 0U && source->width_bits != destination_width) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, source_token, "Immediate operand width does not match the destination operand width.");
            return false;
        }
        if (source_token != NULL && source_token->kind == VM_LEXER_TOKEN_NUMBER) {
            uint32_t encoded_value = 0U;
            if (!vm_parser_encode_number_for_width(source_token, destination_width, &encoded_value)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, source_token, "Immediate value does not fit the destination operand width.");
                return false;
            }
        } else if (!vm_parser_max_unsigned_for_width(destination_width, &max_value) || source->immediate > max_value) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, source_token, "Immediate value does not fit the destination operand width.");
            return false;
        }
        return true;
    }

    if (source->kind == VM_IR_OPERAND_REGISTER || vm_parser_operand_is_memory(source)) {
        if (!vm_parser_resolve_operand_width(source, &source_width) || source_width != destination_width) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, source_token, "Source operand width does not match the destination operand width.");
            return false;
        }
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, source_token, "Expected a register, immediate, data symbol, register-indirect memory operand, or OFFSET source operand.");
    return false;
}

/// Consumes an expected comma token.
///
/// @param state Parser state to mutate.
/// @return true when a comma was consumed.
static bool vm_parser_expect_comma(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (token == NULL || token->kind != VM_LEXER_TOKEN_COMMA) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, token, "Expected a comma between operands.");
        return false;
    }

    vm_parser_advance(state);
    return true;
}

/// Consumes a newline or EOF after a complete statement.
///
/// @param state Parser state to mutate.
/// @return true when the current token ends the statement.
static bool vm_parser_expect_line_end(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (!vm_parser_is_line_end_token(token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_LINE_END, token, "Expected end of line after statement.");
        return false;
    }

    if (token != NULL && token->kind == VM_LEXER_TOKEN_NEWLINE) {
        vm_parser_advance(state);
    }
    return true;
}

/// Emits one IR instruction into the caller-provided instruction buffer.
///
/// @param state Parser state to mutate.
/// @param opcode Opcode to emit.
/// @param destination Destination operand.
/// @param source Source operand.
/// @param mnemonic_token First token of the source instruction.
/// @return true when the instruction was emitted.
static bool vm_parser_emit_instruction(
    VmParserState *state,
    VmIrOpcode opcode,
    VmIrOperand destination,
    VmIrOperand source,
    const VmLexerToken *mnemonic_token
) {
    const char *source_text = NULL;
    VmIrInstruction *instruction = NULL;
    uint32_t instruction_index = 0U;

    if (state == NULL || state->config == NULL || state->result == NULL || mnemonic_token == NULL) {
        return false;
    }

    if (state->result->instruction_count >= state->config->instruction_capacity || state->config->instructions == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INSTRUCTION_CAPACITY_EXCEEDED, mnemonic_token, "IR instruction capacity exceeded.");
        state->stop_status = VM_PARSER_STATUS_INSTRUCTION_CAPACITY_EXCEEDED;
        return false;
    }

    if (!vm_parser_copy_source_line(state, mnemonic_token, &source_text)) {
        return false;
    }

    instruction_index = (uint32_t)state->result->instruction_count;
    instruction = &state->config->instructions[state->result->instruction_count];
    *instruction = vm_ir_instruction(
        opcode,
        destination,
        source,
        state->config->source_file,
        mnemonic_token->location.line,
        source_text,
        instruction_index
    );
    state->result->instruction_count += 1U;
    return true;
}

/// Parses one two-operand instruction.
///
/// @param state Parser state to mutate.
/// @return true when the instruction was parsed and emitted.
static bool vm_parser_parse_instruction(VmParserState *state) {
    const VmLexerToken *mnemonic_token = vm_parser_current_token(state);
    VmIrOpcode opcode = VM_IR_OPCODE_MOV;
    VmIrOperand destination = vm_ir_operand_none();
    VmIrOperand source = vm_ir_operand_none();
    const VmLexerToken *source_token = NULL;

    if (!vm_parser_parse_opcode(mnemonic_token, &opcode)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION, mnemonic_token, "Unsupported instruction for the current milestone.");
        return false;
    }

    vm_parser_advance(state);
    if (!vm_parser_parse_destination_operand(state, &destination)) {
        return false;
    }
    if (!vm_parser_expect_comma(state)) {
        return false;
    }
    source_token = vm_parser_current_token(state);
    if (!vm_parser_parse_source_operand(state, &source)) {
        return false;
    }
    vm_parser_infer_register_memory_widths(&destination, &source);
    if (!vm_parser_validate_source_width(state, &destination, &source, source_token)) {
        return false;
    }
    if (!vm_parser_expect_line_end(state)) {
        return false;
    }

    return vm_parser_emit_instruction(state, opcode, destination, source, mnemonic_token);
}

/// Parses a .code directive line.
///
/// @param state Parser state to mutate.
/// @return true when the directive line was accepted.
static bool vm_parser_parse_code_directive(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (token == NULL || token->kind != VM_LEXER_TOKEN_DIRECTIVE || !vm_parser_token_equals(token, ".code")) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, token, "Expected .code directive.");
        return false;
    }

    state->saw_code_directive = true;
    state->section = VM_PARSER_SECTION_CODE;
    vm_parser_advance(state);
    return vm_parser_expect_line_end(state);
}

/// Parses a .data directive line.
///
/// @param state Parser state to mutate.
/// @return true when the directive line was accepted.
static bool vm_parser_parse_data_directive(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (token == NULL || token->kind != VM_LEXER_TOKEN_DIRECTIVE || !vm_parser_token_equals(token, ".data")) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION, token, "Expected .data directive.");
        return false;
    }

    state->saw_data_directive = true;
    state->section = VM_PARSER_SECTION_DATA;
    vm_parser_advance(state);
    return vm_parser_expect_line_end(state);
}

/// Parses a procedure-start line such as `main PROC`.
///
/// @param state Parser state to mutate.
/// @return true when the line was accepted.
static bool vm_parser_parse_proc_line(VmParserState *state) {
    const VmLexerToken *name_token = vm_parser_current_token(state);
    const VmLexerToken *proc_token = vm_parser_peek_token(state, 1U);

    if (name_token == NULL || proc_token == NULL || name_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_parser_token_equals(proc_token, "PROC")) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_PROC, name_token, "Expected procedure name followed by PROC.");
        return false;
    }

    if (!state->procedure_name.is_set) {
        state->procedure_name.lexeme = name_token->lexeme;
        state->procedure_name.length = name_token->lexeme_length;
        state->procedure_name.is_set = true;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    return vm_parser_expect_line_end(state);
}

/// Parses a procedure-end line such as `main ENDP`.
///
/// @param state Parser state to mutate.
/// @return true when the line was accepted.
static bool vm_parser_parse_endp_line(VmParserState *state) {
    const VmLexerToken *name_token = vm_parser_current_token(state);
    const VmLexerToken *endp_token = vm_parser_peek_token(state, 1U);

    if (name_token == NULL || endp_token == NULL || name_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_parser_token_equals(endp_token, "ENDP")) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_ENDP, name_token, "Expected procedure name followed by ENDP.");
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    return vm_parser_expect_line_end(state);
}

/// Parses an END directive line.
///
/// @param state Parser state to mutate.
/// @return true when the END line was accepted.
static bool vm_parser_parse_end_line(VmParserState *state) {
    const VmLexerToken *end_token = vm_parser_current_token(state);
    const VmLexerToken *entry_token = vm_parser_peek_token(state, 1U);

    if (end_token == NULL || !vm_parser_token_equals(end_token, "END") || entry_token == NULL || entry_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_END, end_token, "Expected END followed by an entry-point name.");
        return false;
    }

    if (state->procedure_name.is_set) {
        VmLexerToken procedure_token;
        memset(&procedure_token, 0, sizeof(procedure_token));
        procedure_token.lexeme = state->procedure_name.lexeme;
        procedure_token.lexeme_length = state->procedure_name.length;
        if (!vm_parser_token_lexemes_equal(&procedure_token, entry_token)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_END_TARGET, entry_token, "END entry point does not match the parsed procedure name.");
            return false;
        }
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    if (!vm_parser_expect_line_end(state)) {
        return false;
    }

    state->saw_end = true;
    return true;
}

/// Returns whether only newlines or EOF remain after END.
///
/// @param state Parser state to inspect and mutate.
/// @return true when only newlines or EOF remain after END.
static bool vm_parser_expect_no_tokens_after_end(VmParserState *state) {
    const VmLexerToken *token = NULL;

    vm_parser_skip_newlines(state);
    token = vm_parser_current_token(state);
    if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, token, "Unexpected source after END directive.");
    return false;
}

/// Parses a label prefix and leaves the parser at any token after the label.
///
/// @param state Parser state to mutate.
/// @return true when a label prefix was consumed.
static bool vm_parser_parse_label_prefix(VmParserState *state) {
    const VmLexerToken *name_token = vm_parser_current_token(state);
    const VmLexerToken *colon_token = vm_parser_peek_token(state, 1U);

    if (name_token == NULL || colon_token == NULL || name_token->kind != VM_LEXER_TOKEN_IDENTIFIER || colon_token->kind != VM_LEXER_TOKEN_COLON) {
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    return true;
}

/// Parses one non-empty line inside the .code section.
///
/// @param state Parser state to mutate.
/// @return true when the line was accepted.
static bool vm_parser_parse_code_line(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);
    const VmLexerToken *next = vm_parser_peek_token(state, 1U);

    if (token == NULL) {
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION, token, "Only one optional .data section followed by .code is supported by this milestone.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "END")) {
        return vm_parser_parse_end_line(state);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && next != NULL && vm_parser_token_equals(next, "PROC")) {
        return vm_parser_parse_proc_line(state);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && next != NULL && vm_parser_token_equals(next, "ENDP")) {
        return vm_parser_parse_endp_line(state);
    }

    if (vm_parser_parse_label_prefix(state)) {
        token = vm_parser_current_token(state);
        if (vm_parser_is_line_end_token(token)) {
            return vm_parser_expect_line_end(state);
        }
    }

    token = vm_parser_current_token(state);
    if (token != NULL && token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        return vm_parser_parse_instruction(state);
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, token, "Unsupported syntax in .code section.");
    return false;
}

/// Parses all data declarations until the .code directive.
///
/// @param state Parser state to mutate.
/// @return true when the data section and following .code directive were parsed.
static bool vm_parser_parse_data_section(VmParserState *state) {
    const VmLexerToken *token = NULL;

    if (!vm_parser_parse_data_directive(state)) {
        return false;
    }

    while (true) {
        vm_parser_skip_newlines(state);
        token = vm_parser_current_token(state);
        if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, token, "Expected .code directive after .data declarations.");
            return false;
        }

        if (token->kind == VM_LEXER_TOKEN_DIRECTIVE) {
            if (vm_parser_token_equals(token, ".code")) {
                return vm_parser_parse_code_directive(state);
            }
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION, token, "Unsupported directive inside .data section.");
            return false;
        }

        if (!vm_parser_parse_data_declaration(state)) {
            return false;
        }
    }
}

/// Initializes a parser result to its empty state.
///
/// @param result Result structure to initialize.
/// @param status Initial status to assign.
static void vm_parser_init_result(VmParserResult *result, VmParserStatus status) {
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->status = status;
}

/// Returns the final status after parser traversal.
///
/// @param state Parser state to inspect.
/// @return Final parser status.
static VmParserStatus vm_parser_finalize_status(const VmParserState *state) {
    if (state == NULL || state->result == NULL) {
        return VM_PARSER_STATUS_INVALID_ARGUMENT;
    }

    if (state->diagnostic_overflowed) {
        return VM_PARSER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED;
    }

    if (state->stop_status != VM_PARSER_STATUS_OK) {
        return state->stop_status;
    }

    return state->result->diagnostic_count == 0U ? VM_PARSER_STATUS_OK : VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS;
}

/// Validates parser configuration before lexing begins.
///
/// @param config Parser configuration to validate.
/// @param out_result Result to initialize on failure.
/// @return true when parsing may proceed.
static bool vm_parser_config_is_valid(const VmParserConfig *config, VmParserResult *out_result) {
    if (config == NULL || out_result == NULL || config->source == NULL ||
        (config->tokens == NULL && config->token_capacity > 0U) ||
        (config->lexer_diagnostics == NULL && config->lexer_diagnostic_capacity > 0U) ||
        (config->instructions == NULL && config->instruction_capacity > 0U) ||
        (config->source_text_storage == NULL && config->source_text_capacity > 0U) ||
        (config->symbols == NULL && config->symbol_capacity > 0U) ||
        (config->data_image == NULL && config->data_image_capacity > 0U) ||
        (config->diagnostics == NULL && config->diagnostic_capacity > 0U)) {
        vm_parser_init_result(out_result, VM_PARSER_STATUS_INVALID_ARGUMENT);
        return false;
    }

    return true;
}

VmParserStatus vm_parser_parse_program(const VmParserConfig *config, VmParserResult *out_result) {
    VmParserState state;
    VmLexerStatus lexer_status = VM_LEXER_STATUS_OK;
    const VmLexerToken *token = NULL;

    if (!vm_parser_config_is_valid(config, out_result)) {
        return VM_PARSER_STATUS_INVALID_ARGUMENT;
    }

    memset(&state, 0, sizeof(state));
    state.config = config;
    state.result = out_result;
    vm_parser_init_result(out_result, VM_PARSER_STATUS_OK);

    if (config->symbols != NULL && config->symbol_capacity > 0U) {
        memset(config->symbols, 0, sizeof(config->symbols[0]) * config->symbol_capacity);
    }
    if (config->data_image != NULL && config->data_image_capacity > 0U) {
        memset(config->data_image, 0, config->data_image_capacity);
    }

    lexer_status = vm_lexer_tokenize(
        config->source,
        config->tokens,
        config->token_capacity,
        config->lexer_diagnostics,
        config->lexer_diagnostic_capacity,
        &state.lexer_result
    );
    out_result->token_count = state.lexer_result.token_count;
    out_result->lexer_diagnostic_count = state.lexer_result.diagnostic_count;

    if (lexer_status != VM_LEXER_STATUS_OK) {
        vm_parser_add_diagnostic(&state, VM_PARSER_DIAGNOSTIC_LEXER_FAILED, config->tokens, "Lexer failed or produced diagnostics before parsing.");
        out_result->status = state.diagnostic_overflowed ? VM_PARSER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED : VM_PARSER_STATUS_LEXER_FAILED;
        return out_result->status;
    }

    vm_parser_skip_newlines(&state);
    token = vm_parser_current_token(&state);
    if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
        vm_parser_add_diagnostic(&state, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, token, "Expected .code directive.");
        out_result->status = vm_parser_finalize_status(&state);
        return out_result->status;
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE && vm_parser_token_equals(token, ".data")) {
        if (!vm_parser_parse_data_section(&state)) {
            out_result->status = vm_parser_finalize_status(&state);
            return out_result->status;
        }
    } else if (!vm_parser_parse_code_directive(&state)) {
        out_result->status = vm_parser_finalize_status(&state);
        return out_result->status;
    }

    while (!state.saw_end) {
        vm_parser_skip_newlines(&state);
        token = vm_parser_current_token(&state);
        if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
            vm_parser_add_diagnostic(&state, VM_PARSER_DIAGNOSTIC_EXPECTED_END, token, "Expected END directive before end of file.");
            break;
        }

        if (!vm_parser_parse_code_line(&state)) {
            break;
        }
    }

    if (state.saw_end) {
        (void)vm_parser_expect_no_tokens_after_end(&state);
    }

    out_result->status = vm_parser_finalize_status(&state);
    return out_result->status;
}

const char *vm_parser_status_name(VmParserStatus status) {
    switch (status) {
        case VM_PARSER_STATUS_OK:
            return "ok";
        case VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS:
            return "ok-with-diagnostics";
        case VM_PARSER_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_PARSER_STATUS_LEXER_FAILED:
            return "lexer-failed";
        case VM_PARSER_STATUS_INSTRUCTION_CAPACITY_EXCEEDED:
            return "instruction-capacity-exceeded";
        case VM_PARSER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED:
            return "diagnostic-capacity-exceeded";
        case VM_PARSER_STATUS_SOURCE_TEXT_CAPACITY_EXCEEDED:
            return "source-text-capacity-exceeded";
        case VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED:
            return "data-capacity-exceeded";
        case VM_PARSER_STATUS_SYMBOL_CAPACITY_EXCEEDED:
            return "symbol-capacity-exceeded";
        default:
            return NULL;
    }
}

const char *vm_parser_diagnostic_code_name(VmParserDiagnosticCode code) {
    switch (code) {
        case VM_PARSER_DIAGNOSTIC_NONE:
            return "none";
        case VM_PARSER_DIAGNOSTIC_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_PARSER_DIAGNOSTIC_LEXER_FAILED:
            return "lexer-failed";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE:
            return "expected-code-directive";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION:
            return "unsupported-section";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION:
            return "unsupported-instruction";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND:
            return "expected-operand";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA:
            return "expected-comma";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_LINE_END:
            return "expected-line-end";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_PROC:
            return "expected-proc";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_ENDP:
            return "expected-endp";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_END:
            return "expected-end";
        case VM_PARSER_DIAGNOSTIC_INVALID_END_TARGET:
            return "invalid-end-target";
        case VM_PARSER_DIAGNOSTIC_INSTRUCTION_CAPACITY_EXCEEDED:
            return "instruction-capacity-exceeded";
        case VM_PARSER_DIAGNOSTIC_SOURCE_TEXT_CAPACITY_EXCEEDED:
            return "source-text-capacity-exceeded";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX:
            return "unsupported-syntax";
        case VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE:
            return "number-out-of-range";
        case VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE:
            return "immediate-out-of-range";
        case VM_PARSER_DIAGNOSTIC_DUPLICATE_SYMBOL:
            return "duplicate-symbol";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_DECLARATION:
            return "expected-data-declaration";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_DATA_TYPE:
            return "unsupported-data-type";
        case VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER:
            return "expected-data-initializer";
        case VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED:
            return "data-capacity-exceeded";
        case VM_PARSER_DIAGNOSTIC_SYMBOL_CAPACITY_EXCEEDED:
            return "symbol-capacity-exceeded";
        case VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL:
            return "unknown-symbol";
        case VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE:
            return "symbol-offset-out-of-range";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH:
            return "unsupported-ptr-width";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX:
            return "unsupported-scaled-index";
        case VM_PARSER_DIAGNOSTIC_INVALID_DUP:
            return "invalid-dup";
        default:
            return NULL;
    }
}
