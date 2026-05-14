/*
 * @file lexer.c
 * @brief Tokenizer implementation for MASM-like source files.
 *
 * The lexer recognizes the token set needed by the current parser: identifiers,
 * MASM32 registers, signed decimal and hexadecimal numbers, strings, commas,
 * square brackets, parentheses, question marks, colons, comments, directives,
 * and line endings.
 */

#include "lexer.h"

#include <limits.h>
#include <string.h>

/// Tracks the current scan position within the source buffer.
typedef struct VmLexerCursor {
    /// Source text being scanned.
    const char *source;
    /// Current byte offset.
    size_t offset;
    /// Current one-based line.
    uint32_t line;
    /// Current one-based column.
    uint32_t column;
} VmLexerCursor;

/// Carries mutable output state while tokenizing.
typedef struct VmLexerWriter {
    /// Caller-provided token output buffer.
    VmLexerToken *tokens;
    /// Number of token entries available.
    size_t token_capacity;
    /// Number of tokens already written.
    size_t token_count;
    /// Caller-provided diagnostic output buffer.
    VmLexerDiagnostic *diagnostics;
    /// Number of diagnostic entries available.
    size_t diagnostic_capacity;
    /// Number of diagnostics already written.
    size_t diagnostic_count;
    /// Whether a diagnostic could not be recorded because the buffer was full.
    bool diagnostic_overflowed;
} VmLexerWriter;

/// Describes one case-insensitive register spelling recognized by the lexer.
typedef struct VmLexerRegisterName {
    /// Register spelling without surrounding whitespace.
    const char *name;
    /// Register identifier produced when the spelling matches.
    VmRegister reg;
} VmLexerRegisterName;

/// Returns whether a byte is an ASCII decimal digit.
///
/// @param ch Source byte to inspect.
/// @return true when @p ch is between '0' and '9'.
static bool lexer_is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

/// Returns whether a byte is an ASCII alphabetic character.
///
/// @param ch Source byte to inspect.
/// @return true when @p ch is an ASCII letter.
static bool lexer_is_alpha(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

/// Returns whether a byte can start a MASM-like identifier.
///
/// @param ch Source byte to inspect.
/// @return true when @p ch can start an identifier.
static bool lexer_is_identifier_start(char ch) {
    return lexer_is_alpha(ch) || ch == '_' || ch == '@' || ch == '$';
}

/// Returns whether a byte can continue a MASM-like identifier.
///
/// @param ch Source byte to inspect.
/// @return true when @p ch can continue an identifier.
static bool lexer_is_identifier_continue(char ch) {
    return lexer_is_identifier_start(ch) || lexer_is_digit(ch);
}

/// Returns whether a byte is an ASCII hexadecimal digit.
///
/// @param ch Source byte to inspect.
/// @return true when @p ch is a hexadecimal digit.
static bool lexer_is_hex_digit(char ch) {
    return lexer_is_digit(ch) || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
}

/// Converts an ASCII hexadecimal digit into its integer value.
///
/// @param ch Hexadecimal digit to convert.
/// @return Value from 0 through 15, or 0 for invalid input.
static uint32_t lexer_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (uint32_t)(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F') {
        return (uint32_t)(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f') {
        return (uint32_t)(ch - 'a' + 10);
    }

    return 0U;
}

/// Converts an ASCII letter to lowercase without using locale-sensitive APIs.
///
/// @param ch Source byte to convert.
/// @return Lowercase ASCII equivalent when applicable.
static char lexer_to_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }

    return ch;
}

/// Compares a source slice with a null-terminated ASCII string ignoring case.
///
/// @param text Source slice start.
/// @param length Source slice length.
/// @param expected Null-terminated expected spelling.
/// @return true when both texts match ignoring ASCII case.
static bool lexer_slice_equals_ignore_case(const char *text, size_t length, const char *expected) {
    size_t index = 0U;

    if (text == NULL || expected == NULL) {
        return false;
    }

    while (index < length && expected[index] != '\0') {
        if (lexer_to_lower(text[index]) != lexer_to_lower(expected[index])) {
            return false;
        }
        index += 1U;
    }

    return index == length && expected[index] == '\0';
}

/// Returns the source byte at the current cursor position.
///
/// @param cursor Cursor to inspect.
/// @return Current source byte, or NUL at end-of-file.
static char lexer_peek(const VmLexerCursor *cursor) {
    return cursor->source[cursor->offset];
}

/// Returns the source byte at an offset from the current cursor position.
///
/// @param cursor Cursor to inspect.
/// @param lookahead Additional bytes to look ahead.
/// @return Requested source byte, or NUL at end-of-file.
static char lexer_peek_ahead(const VmLexerCursor *cursor, size_t lookahead) {
    return cursor->source[cursor->offset + lookahead];
}

/// Returns the current source location represented by a cursor.
///
/// @param cursor Cursor to inspect.
/// @return Current source location.
static VmLexerSourceLocation lexer_location(const VmLexerCursor *cursor) {
    VmLexerSourceLocation location;

    location.line = cursor->line;
    location.column = cursor->column;
    location.offset = cursor->offset;

    return location;
}

/// Advances the cursor by one non-newline source byte.
///
/// @param cursor Cursor to advance.
static void lexer_advance_byte(VmLexerCursor *cursor) {
    if (lexer_peek(cursor) != '\0') {
        cursor->offset += 1U;
        cursor->column += 1U;
    }
}

/// Advances the cursor over a newline sequence.
///
/// @param cursor Cursor positioned at '\r' or '\n'.
/// @return Number of source bytes consumed.
static size_t lexer_advance_newline(VmLexerCursor *cursor) {
    size_t consumed = 0U;

    if (lexer_peek(cursor) == '\r') {
        lexer_advance_byte(cursor);
        consumed += 1U;
        if (lexer_peek(cursor) == '\n') {
            lexer_advance_byte(cursor);
            consumed += 1U;
        }
    } else if (lexer_peek(cursor) == '\n') {
        lexer_advance_byte(cursor);
        consumed += 1U;
    }

    cursor->line += 1U;
    cursor->column = 1U;

    return consumed;
}

/// Adds one token to the output buffer.
///
/// @param writer Output writer to mutate.
/// @param kind Token kind to write.
/// @param location Token start location.
/// @param source Source buffer start.
/// @param length Token source length.
/// @param number_value Parsed number magnitude for number tokens.
/// @param number_base Parsed number base for number tokens.
/// @param number_is_negative Whether the token had a leading minus sign.
/// @param register_id Register identifier for register tokens.
/// @return true when the token was written; false when token capacity is full.
static bool lexer_add_token(
    VmLexerWriter *writer,
    VmLexerTokenKind kind,
    VmLexerSourceLocation location,
    const char *source,
    size_t length,
    uint64_t number_value,
    uint32_t number_base,
    bool number_is_negative,
    VmRegister register_id
) {
    VmLexerToken *token = NULL;

    if (writer->token_count >= writer->token_capacity) {
        return false;
    }

    token = &writer->tokens[writer->token_count];
    token->kind = kind;
    token->location = location;
    token->lexeme = source;
    token->lexeme_length = length;
    token->number_value = number_value;
    token->number_base = number_base;
    token->number_is_negative = number_is_negative;
    token->register_id = register_id;
    writer->token_count += 1U;

    return true;
}

/// Adds one structured diagnostic to the output buffer.
///
/// @param writer Output writer to mutate.
/// @param code Diagnostic code to write.
/// @param location Source location for the diagnostic.
/// @param source Source slice associated with the diagnostic.
/// @param length Source slice length.
/// @param message Static human-readable diagnostic summary.
/// @return true when the diagnostic was written; false when capacity is full.
static bool lexer_add_diagnostic(
    VmLexerWriter *writer,
    VmLexerDiagnosticCode code,
    VmLexerSourceLocation location,
    const char *source,
    size_t length,
    const char *message
) {
    VmLexerDiagnostic *diagnostic = NULL;

    if (writer->diagnostic_count >= writer->diagnostic_capacity) {
        writer->diagnostic_overflowed = true;
        return false;
    }

    diagnostic = &writer->diagnostics[writer->diagnostic_count];
    diagnostic->code = code;
    diagnostic->location = location;
    diagnostic->lexeme = source;
    diagnostic->lexeme_length = length;
    diagnostic->message = message;
    writer->diagnostic_count += 1U;

    return true;
}

/// Emits a token-capacity diagnostic when possible.
///
/// @param writer Output writer to mutate.
/// @param location Source location where capacity was exhausted.
/// @param source Source text associated with the failure.
/// @return Token capacity exceeded status.
static VmLexerStatus lexer_token_capacity_failure(VmLexerWriter *writer, VmLexerSourceLocation location, const char *source) {
    (void)lexer_add_diagnostic(
        writer,
        VM_LEXER_DIAGNOSTIC_TOKEN_CAPACITY_EXCEEDED,
        location,
        source,
        1U,
        "token buffer capacity exceeded"
    );
    if (writer->diagnostic_overflowed) {
        return VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED;
    }

    return VM_LEXER_STATUS_TOKEN_CAPACITY_EXCEEDED;
}

/// Looks up a register identifier from a source slice.
///
/// @param text Source slice start.
/// @param length Source slice length.
/// @param out_register Receives the register identifier on success.
/// @return true when the slice names a supported MASM32 register or alias.
static bool lexer_lookup_register(const char *text, size_t length, VmRegister *out_register) {
    static const VmLexerRegisterName registers[] = {
        {"eax", VM_REGISTER_EAX}, {"ebx", VM_REGISTER_EBX}, {"ecx", VM_REGISTER_ECX}, {"edx", VM_REGISTER_EDX},
        {"esi", VM_REGISTER_ESI}, {"edi", VM_REGISTER_EDI}, {"ebp", VM_REGISTER_EBP}, {"esp", VM_REGISTER_ESP},
        {"eip", VM_REGISTER_EIP}, {"eflags", VM_REGISTER_EFLAGS},
        {"ax", VM_REGISTER_AX}, {"ah", VM_REGISTER_AH}, {"al", VM_REGISTER_AL},
        {"bx", VM_REGISTER_BX}, {"bh", VM_REGISTER_BH}, {"bl", VM_REGISTER_BL},
        {"cx", VM_REGISTER_CX}, {"ch", VM_REGISTER_CH}, {"cl", VM_REGISTER_CL},
        {"dx", VM_REGISTER_DX}, {"dh", VM_REGISTER_DH}, {"dl", VM_REGISTER_DL},
        {"si", VM_REGISTER_SI}, {"di", VM_REGISTER_DI}, {"bp", VM_REGISTER_BP}, {"sp", VM_REGISTER_SP}
    };
    size_t index = 0U;

    if (out_register == NULL) {
        return false;
    }

    for (index = 0U; index < sizeof(registers) / sizeof(registers[0]); index += 1U) {
        if (lexer_slice_equals_ignore_case(text, length, registers[index].name)) {
            *out_register = registers[index].reg;
            return true;
        }
    }

    return false;
}

/// Parses an unsigned integer slice in the requested base.
///
/// @param text Source slice start.
/// @param length Source slice length.
/// @param base Numeric base, either 10 or 16.
/// @param out_value Receives the parsed value.
/// @return true when the value fits in uint64_t; false on overflow.
static bool lexer_parse_uint64(const char *text, size_t length, uint32_t base, uint64_t *out_value) {
    size_t index = 0U;
    uint64_t value = 0U;

    if (out_value == NULL || text == NULL || (base != 10U && base != 16U)) {
        return false;
    }

    for (index = 0U; index < length; index += 1U) {
        uint32_t digit = (base == 16U) ? lexer_hex_value(text[index]) : (uint32_t)(text[index] - '0');
        if (value > (UINT64_MAX - (uint64_t)digit) / (uint64_t)base) {
            return false;
        }
        value = (value * (uint64_t)base) + (uint64_t)digit;
    }

    *out_value = value;
    return true;
}

/// Scans a decimal or hexadecimal number token.
///
/// @param cursor Cursor positioned at the first digit or a leading minus sign.
/// @param writer Output writer to mutate.
/// @return Lexer status after scanning this token.
static VmLexerStatus lexer_scan_number(VmLexerCursor *cursor, VmLexerWriter *writer) {
    VmLexerSourceLocation start = lexer_location(cursor);
    size_t value_offset = cursor->offset;
    size_t value_length = 0U;
    size_t token_length = 0U;
    uint32_t base = 10U;
    uint64_t value = 0U;
    bool is_negative = false;
    bool overflowed = false;
    bool invalid_hex = false;

    if (lexer_peek(cursor) == '-') {
        is_negative = true;
        lexer_advance_byte(cursor);
        value_offset = cursor->offset;
    }

    if (lexer_peek(cursor) == '0' && (lexer_peek_ahead(cursor, 1U) == 'x' || lexer_peek_ahead(cursor, 1U) == 'X')) {
        base = 16U;
        lexer_advance_byte(cursor);
        lexer_advance_byte(cursor);
        value_offset = cursor->offset;
        while (lexer_is_hex_digit(lexer_peek(cursor))) {
            lexer_advance_byte(cursor);
        }
        value_length = cursor->offset - value_offset;
        invalid_hex = value_length == 0U;
    } else {
        while (lexer_is_hex_digit(lexer_peek(cursor))) {
            lexer_advance_byte(cursor);
        }
        if (lexer_peek(cursor) == 'h' || lexer_peek(cursor) == 'H') {
            base = 16U;
            value_length = cursor->offset - value_offset;
            lexer_advance_byte(cursor);
        } else {
            base = 10U;
            while (lexer_is_identifier_continue(lexer_peek(cursor))) {
                lexer_advance_byte(cursor);
            }
            value_length = cursor->offset - value_offset;
            if (value_length == 0U) {
                value_length = 1U;
            }
        }
    }

    token_length = cursor->offset - start.offset;

    if (invalid_hex) {
        (void)lexer_add_diagnostic(writer, VM_LEXER_DIAGNOSTIC_INVALID_HEX_LITERAL, start, cursor->source + start.offset, token_length, "hex literal requires at least one digit");
    } else if (base == 10U) {
        size_t decimal_digit_length = 0U;
        while (decimal_digit_length < value_length && lexer_is_digit(cursor->source[value_offset + decimal_digit_length])) {
            decimal_digit_length += 1U;
        }
        if (decimal_digit_length != value_length) {
            (void)lexer_add_diagnostic(writer, VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER, start, cursor->source + start.offset, token_length, "invalid decimal literal suffix");
        }
        if (!lexer_parse_uint64(cursor->source + value_offset, decimal_digit_length, base, &value)) {
            overflowed = true;
        }
    } else if (!lexer_parse_uint64(cursor->source + value_offset, value_length, base, &value)) {
        overflowed = true;
    }

    if (overflowed) {
        (void)lexer_add_diagnostic(writer, VM_LEXER_DIAGNOSTIC_NUMBER_OVERFLOW, start, cursor->source + start.offset, token_length, "number literal does not fit in uint64_t");
    }

    if (writer->diagnostic_overflowed) {
        return VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED;
    }

    if (!lexer_add_token(writer, VM_LEXER_TOKEN_NUMBER, start, cursor->source + start.offset, token_length, value, base, is_negative, VM_REGISTER_COUNT)) {
        return lexer_token_capacity_failure(writer, start, cursor->source + start.offset);
    }

    return VM_LEXER_STATUS_OK;
}

/// Scans an identifier or register token.
///
/// @param cursor Cursor positioned at an identifier start.
/// @param writer Output writer to mutate.
/// @return Lexer status after scanning this token.
static VmLexerStatus lexer_scan_identifier(VmLexerCursor *cursor, VmLexerWriter *writer) {
    VmLexerSourceLocation start = lexer_location(cursor);
    VmRegister reg = VM_REGISTER_COUNT;
    VmLexerTokenKind kind = VM_LEXER_TOKEN_IDENTIFIER;
    size_t length = 0U;

    while (lexer_is_identifier_continue(lexer_peek(cursor))) {
        lexer_advance_byte(cursor);
    }

    length = cursor->offset - start.offset;
    if (lexer_lookup_register(cursor->source + start.offset, length, &reg)) {
        kind = VM_LEXER_TOKEN_REGISTER;
    }

    if (!lexer_add_token(writer, kind, start, cursor->source + start.offset, length, 0U, 0U, false, reg)) {
        return lexer_token_capacity_failure(writer, start, cursor->source + start.offset);
    }

    return VM_LEXER_STATUS_OK;
}

/// Scans a dot-prefixed directive token.
///
/// @param cursor Cursor positioned at '.'.
/// @param writer Output writer to mutate.
/// @return Lexer status after scanning this token.
static VmLexerStatus lexer_scan_directive(VmLexerCursor *cursor, VmLexerWriter *writer) {
    VmLexerSourceLocation start = lexer_location(cursor);
    size_t length = 0U;

    lexer_advance_byte(cursor);
    if (!lexer_is_identifier_start(lexer_peek(cursor))) {
        (void)lexer_add_diagnostic(writer, VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER, start, cursor->source + start.offset, 1U, "directive must include a name after '.'");
        if (writer->diagnostic_overflowed) {
            return VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED;
        }
    } else {
        while (lexer_is_identifier_continue(lexer_peek(cursor))) {
            lexer_advance_byte(cursor);
        }
    }

    length = cursor->offset - start.offset;
    if (!lexer_add_token(writer, VM_LEXER_TOKEN_DIRECTIVE, start, cursor->source + start.offset, length, 0U, 0U, false, VM_REGISTER_COUNT)) {
        return lexer_token_capacity_failure(writer, start, cursor->source + start.offset);
    }

    return VM_LEXER_STATUS_OK;
}

/// Scans a double-quoted string literal.
///
/// @param cursor Cursor positioned at the opening quote.
/// @param writer Output writer to mutate.
/// @return Lexer status after scanning this token.
static VmLexerStatus lexer_scan_string(VmLexerCursor *cursor, VmLexerWriter *writer) {
    VmLexerSourceLocation start = lexer_location(cursor);
    bool terminated = false;
    size_t length = 0U;

    lexer_advance_byte(cursor);
    while (lexer_peek(cursor) != '\0') {
        if (lexer_peek(cursor) == '"') {
            lexer_advance_byte(cursor);
            terminated = true;
            break;
        }
        if (lexer_peek(cursor) == '\\' && lexer_peek_ahead(cursor, 1U) != '\0') {
            lexer_advance_byte(cursor);
            if (lexer_peek(cursor) != '\r' && lexer_peek(cursor) != '\n') {
                lexer_advance_byte(cursor);
                continue;
            }
        }
        if (lexer_peek(cursor) == '\r' || lexer_peek(cursor) == '\n') {
            break;
        }
        lexer_advance_byte(cursor);
    }

    length = cursor->offset - start.offset;
    if (!terminated) {
        (void)lexer_add_diagnostic(writer, VM_LEXER_DIAGNOSTIC_UNTERMINATED_STRING, start, cursor->source + start.offset, length, "unterminated string literal");
        if (writer->diagnostic_overflowed) {
            return VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED;
        }
    }

    if (!lexer_add_token(writer, VM_LEXER_TOKEN_STRING, start, cursor->source + start.offset, length, 0U, 0U, false, VM_REGISTER_COUNT)) {
        return lexer_token_capacity_failure(writer, start, cursor->source + start.offset);
    }

    return VM_LEXER_STATUS_OK;
}

/// Skips a semicolon comment, stopping before the line ending.
///
/// @param cursor Cursor positioned at ';'.
static void lexer_skip_comment(VmLexerCursor *cursor) {
    while (lexer_peek(cursor) != '\0' && lexer_peek(cursor) != '\r' && lexer_peek(cursor) != '\n') {
        lexer_advance_byte(cursor);
    }
}

/// Emits one single-character token and advances the cursor.
///
/// @param cursor Cursor positioned at the token character.
/// @param writer Output writer to mutate.
/// @param kind Token kind to emit.
/// @return Lexer status after emitting the token.
static VmLexerStatus lexer_emit_single_character_token(VmLexerCursor *cursor, VmLexerWriter *writer, VmLexerTokenKind kind) {
    VmLexerSourceLocation start = lexer_location(cursor);
    lexer_advance_byte(cursor);
    if (!lexer_add_token(writer, kind, start, cursor->source + start.offset, 1U, 0U, 0U, false, VM_REGISTER_COUNT)) {
        return lexer_token_capacity_failure(writer, start, cursor->source + start.offset);
    }

    return VM_LEXER_STATUS_OK;
}

/// Initializes a lexer result object.
///
/// @param result Result object to initialize.
/// @param status Initial status value.
static void lexer_init_result(VmLexerResult *result, VmLexerStatus status) {
    if (result != NULL) {
        result->status = status;
        result->token_count = 0U;
        result->diagnostic_count = 0U;
    }
}

VmLexerStatus vm_lexer_tokenize(
    const char *source,
    VmLexerToken *tokens,
    size_t token_capacity,
    VmLexerDiagnostic *diagnostics,
    size_t diagnostic_capacity,
    VmLexerResult *out_result
) {
    VmLexerCursor cursor;
    VmLexerWriter writer;
    VmLexerStatus status = VM_LEXER_STATUS_OK;

    lexer_init_result(out_result, VM_LEXER_STATUS_INVALID_ARGUMENT);

    if (source == NULL || out_result == NULL || (tokens == NULL && token_capacity > 0U) || (diagnostics == NULL && diagnostic_capacity > 0U)) {
        return VM_LEXER_STATUS_INVALID_ARGUMENT;
    }

    writer.tokens = tokens;
    writer.token_capacity = token_capacity;
    writer.token_count = 0U;
    writer.diagnostics = diagnostics;
    writer.diagnostic_capacity = diagnostic_capacity;
    writer.diagnostic_count = 0U;
    writer.diagnostic_overflowed = false;

    cursor.source = source;
    cursor.offset = 0U;
    cursor.line = 1U;
    cursor.column = 1U;

    while (lexer_peek(&cursor) != '\0') {
        char ch = lexer_peek(&cursor);

        if (ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f') {
            lexer_advance_byte(&cursor);
            continue;
        }
        if (ch == ';') {
            lexer_skip_comment(&cursor);
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            VmLexerSourceLocation start = lexer_location(&cursor);
            const char *newline_start = cursor.source + cursor.offset;
            size_t newline_length = lexer_advance_newline(&cursor);
            if (!lexer_add_token(&writer, VM_LEXER_TOKEN_NEWLINE, start, newline_start, newline_length, 0U, 0U, false, VM_REGISTER_COUNT)) {
                status = lexer_token_capacity_failure(&writer, start, newline_start);
                break;
            }
            continue;
        }
        if (lexer_is_identifier_start(ch)) {
            status = lexer_scan_identifier(&cursor, &writer);
        } else if (lexer_is_digit(ch) || (ch == '-' && lexer_is_digit(lexer_peek_ahead(&cursor, 1U)))) {
            status = lexer_scan_number(&cursor, &writer);
        } else if (ch == '.') {
            status = lexer_scan_directive(&cursor, &writer);
        } else if (ch == '"') {
            status = lexer_scan_string(&cursor, &writer);
        } else if (ch == ',') {
            status = lexer_emit_single_character_token(&cursor, &writer, VM_LEXER_TOKEN_COMMA);
        } else if (ch == '[') {
            status = lexer_emit_single_character_token(&cursor, &writer, VM_LEXER_TOKEN_LEFT_BRACKET);
        } else if (ch == ']') {
            status = lexer_emit_single_character_token(&cursor, &writer, VM_LEXER_TOKEN_RIGHT_BRACKET);
        } else if (ch == '(') {
            status = lexer_emit_single_character_token(&cursor, &writer, VM_LEXER_TOKEN_LEFT_PAREN);
        } else if (ch == ')') {
            status = lexer_emit_single_character_token(&cursor, &writer, VM_LEXER_TOKEN_RIGHT_PAREN);
        } else if (ch == '?') {
            status = lexer_emit_single_character_token(&cursor, &writer, VM_LEXER_TOKEN_QUESTION);
        } else if (ch == ':') {
            status = lexer_emit_single_character_token(&cursor, &writer, VM_LEXER_TOKEN_COLON);
        } else {
            VmLexerSourceLocation start = lexer_location(&cursor);
            (void)lexer_add_diagnostic(&writer, VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER, start, cursor.source + cursor.offset, 1U, "unexpected character");
            lexer_advance_byte(&cursor);
            status = writer.diagnostic_overflowed ? VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED : VM_LEXER_STATUS_OK;
        }

        if (status == VM_LEXER_STATUS_TOKEN_CAPACITY_EXCEEDED || status == VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED) {
            break;
        }
    }

    if (status == VM_LEXER_STATUS_OK && !writer.diagnostic_overflowed) {
        VmLexerSourceLocation eof_location = lexer_location(&cursor);
        if (!lexer_add_token(&writer, VM_LEXER_TOKEN_EOF, eof_location, cursor.source + cursor.offset, 0U, 0U, 0U, false, VM_REGISTER_COUNT)) {
            status = lexer_token_capacity_failure(&writer, eof_location, cursor.source + cursor.offset);
        }
    }

    if (status == VM_LEXER_STATUS_OK && writer.diagnostic_count > 0U) {
        status = VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS;
    }
    if (writer.diagnostic_overflowed && status == VM_LEXER_STATUS_OK) {
        status = VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED;
    }

    out_result->status = status;
    out_result->token_count = writer.token_count;
    out_result->diagnostic_count = writer.diagnostic_count;

    return status;
}

const char *vm_lexer_token_kind_name(VmLexerTokenKind kind) {
    static const char *const names[] = {
        "EOF",
        "NEWLINE",
        "IDENTIFIER",
        "DIRECTIVE",
        "REGISTER",
        "NUMBER",
        "STRING",
        "COMMA",
        "LEFT_BRACKET",
        "RIGHT_BRACKET",
        "LEFT_PAREN",
        "RIGHT_PAREN",
        "QUESTION",
        "COLON"
    };

    if ((uint32_t)kind >= (uint32_t)VM_LEXER_TOKEN_KIND_COUNT) {
        return NULL;
    }

    return names[(uint32_t)kind];
}

const char *vm_lexer_diagnostic_code_name(VmLexerDiagnosticCode code) {
    static const char *const names[] = {
        "none",
        "invalid-argument",
        "token-capacity-exceeded",
        "diagnostic-capacity-exceeded",
        "unexpected-character",
        "unterminated-string",
        "number-overflow",
        "invalid-hex-literal"
    };

    if ((uint32_t)code >= (uint32_t)VM_LEXER_DIAGNOSTIC_CODE_COUNT) {
        return NULL;
    }

    return names[(uint32_t)code];
}

const char *vm_lexer_status_name(VmLexerStatus status) {
    static const char *const names[] = {
        "ok",
        "ok-with-diagnostics",
        "invalid-argument",
        "token-capacity-exceeded",
        "diagnostic-capacity-exceeded"
    };

    if ((uint32_t)status > (uint32_t)VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED) {
        return NULL;
    }

    return names[(uint32_t)status];
}
