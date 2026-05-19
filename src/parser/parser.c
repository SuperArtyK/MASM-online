/*
 * @file parser.c
 * @brief Parser for the currently implemented MASM32 educational subset.
 *
 * This implementation consumes the lexer token stream, lays out small .data,
 * .DATA?, and .CONST images with symbols, and emits only the minimal IR supported by the current
 * executor. Control flow, stack behavior, scaled-index addressing, Irvine32 routine bodies,
 * and full MASM expression parsing remain later milestones. The parser records
 * virtual Irvine32 include metadata and routine classifications without loading
 * host files or linking external libraries. Recognizable textbook
 * MASM constructs outside the implemented subset are classified with explicit
 * unsupported-feature diagnostics, safely skipped when recoverable, and surfaced
 * lexer diagnostics remain specific instead of generic umbrella errors.
 */

#include "parser.h"

#include "vm_memory.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/// Maximum numeric equates retained during one parse operation.
#define VM_PARSER_EQUATE_CAPACITY 128U


/// Formats an unsigned integer with comma group separators for diagnostics.
///
/// @param value Value to format.
/// @param buffer Destination buffer.
/// @param buffer_size Number of bytes available in @p buffer.
static void vm_parser_format_u64_with_commas(uint64_t value, char *buffer, size_t buffer_size) {
    char raw[32];
    char grouped[48];
    size_t raw_length = 0U;
    size_t first_group = 0U;
    size_t raw_index = 0U;
    size_t grouped_index = 0U;

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    (void)snprintf(raw, sizeof(raw), "%llu", (unsigned long long)value);
    raw_length = strlen(raw);
    first_group = raw_length % 3U;
    if (first_group == 0U) {
        first_group = 3U;
    }

    for (raw_index = 0U; raw_index < raw_length && grouped_index + 1U < sizeof(grouped); raw_index += 1U) {
        if (raw_index > 0U && raw_index % 3U == first_group % 3U) {
            grouped[grouped_index] = ',';
            grouped_index += 1U;
            if (grouped_index + 1U >= sizeof(grouped)) {
                break;
            }
        }
        grouped[grouped_index] = raw[raw_index];
        grouped_index += 1U;
    }
    grouped[grouped_index] = '\0';

    (void)snprintf(buffer, buffer_size, "%s", grouped);
}

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
    /// Parser is currently consuming writable .data declarations.
    VM_PARSER_SECTION_DATA,
    /// Parser is currently consuming deterministic zero-filled .DATA? declarations.
    VM_PARSER_SECTION_DATA_UNINITIALIZED,
    /// Parser is currently consuming read-only .CONST declarations.
    VM_PARSER_SECTION_CONST,
    /// Parser is currently consuming .code statements.
    VM_PARSER_SECTION_CODE
} VmParserSection;

/// Describes the parser's active user-symbol case policy.
typedef enum VmParserUserSymbolCasePolicy {
    /// CASEMAP:ALL behavior: ASCII-folded user-symbol matching.
    VM_PARSER_USER_SYMBOL_CASEMAP_ALL = 0,
    /// CASEMAP:NONE behavior: exact-case user-symbol matching.
    VM_PARSER_USER_SYMBOL_CASEMAP_NONE
} VmParserUserSymbolCasePolicy;

/// Describes one numeric equate known during parsing.
typedef struct VmParserEquate {
    /// Null-terminated equate name copied from source.
    char name[VM_SYMBOL_NAME_CAPACITY];
    /// Evaluated signed 64-bit constant value.
    int64_t value;
    /// Whether this slot contains a valid equate.
    bool is_defined;
    /// Whether this name was seen but its equate expression was invalid.
    bool is_invalid;
    /// Whether this equate is currently being evaluated.
    bool is_resolving;
} VmParserEquate;

/// Carries the result of a compile-time constant-expression parse.
typedef struct VmParserConstantExpression {
    /// Evaluated signed 64-bit expression value.
    int64_t value;
    /// Token that started the expression for diagnostics.
    const VmLexerToken *start_token;
} VmParserConstantExpression;

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
    /// Current write offset into the .data/.DATA? image.
    uint32_t data_offset;
    /// Current write offset into the .CONST image.
    uint32_t const_offset;
    /// Current top-level parser section.
    VmParserSection section;
    /// Recorded procedure name for END validation.
    VmParserProcedureName procedure_name;
    /// Whether the required .code directive has been consumed.
    bool saw_code_directive;
    /// Whether an optional data-section directive has been consumed.
    bool saw_data_directive;
    /// Whether an END directive has been parsed.
    bool saw_end;
    /// Specific non-OK status requested by a hard failure, or OK when unset.
    VmParserStatus stop_status;
    /// Numeric equates parsed before or during source traversal.
    VmParserEquate equates[VM_PARSER_EQUATE_CAPACITY];
    /// Number of valid entries in @ref equates.
    size_t equate_count;
    /// Name token for the equate currently being parsed, or NULL outside equate parsing.
    const VmLexerToken *active_equate_name;
    /// Active source-order CASEMAP policy for user-defined symbols.
    VmParserUserSymbolCasePolicy user_symbol_case_policy;
    /// Whether a supported OPTION CASEMAP directive has been seen.
    bool has_explicit_casemap_policy;
    /// Whether a required diagnostic could not be recorded.
    bool diagnostic_overflowed;
} VmParserState;

/// Describes one parsed PTR width override prefix.
typedef struct VmParserPtrWidth {
    /// Width in bits requested by unsigned or signed BYTE/WORD/DWORD/QWORD PTR aliases.
    uint8_t width_bits;
    /// Token containing the width keyword for diagnostics.
    const VmLexerToken *width_token;
    /// Token containing PTR for diagnostics.
    const VmLexerToken *ptr_token;
} VmParserPtrWidth;

/// Describes one recognized but intentionally unsupported MASM construct.
typedef struct VmParserUnsupportedFeature {
    /// Keyword or directive spelling to match case-insensitively.
    const char *spelling;
    /// Stable diagnostic message explaining the unsupported construct.
    const char *message;
} VmParserUnsupportedFeature;

/// Describes one known name in the virtual Irvine32 registry.
typedef struct VmParserIrvine32RegistryEntry {
    /// Canonical Irvine32 routine, pseudo-instruction, or external name.
    const char *name;
    /// Milestone classification for the known name.
    VmIrvine32SymbolClass symbol_class;
} VmParserIrvine32RegistryEntry;

/// Describes the outcome of shared parser memory-width resolution.
typedef enum VmParserMemoryWidthResolutionStatus {
    /// All required memory widths are known or were inferred safely.
    VM_PARSER_MEMORY_WIDTH_RESOLVED = 0,
    /// A register-indirect memory operand has no explicit, symbol, or register-supplied width.
    VM_PARSER_MEMORY_WIDTH_AMBIGUOUS,
    /// The instruction shape cannot supply a supported memory access width.
    VM_PARSER_MEMORY_WIDTH_UNSUPPORTED
} VmParserMemoryWidthResolutionStatus;

static bool vm_parser_token_can_name_data_symbol(const VmLexerToken *token);

static const VmLexerToken *vm_parser_current_token(const VmParserState *state);

static const VmLexerToken *vm_parser_peek_token(const VmParserState *state, size_t offset);

static void vm_parser_advance(VmParserState *state);

static bool vm_parser_add_diagnostic(
    VmParserState *state,
    VmParserDiagnosticCode code,
    const VmLexerToken *token,
    const char *message
);

static bool vm_parser_add_warning(
    VmParserState *state,
    VmParserDiagnosticCode code,
    const VmLexerToken *token,
    const char *message
);

static bool vm_parser_add_lexer_diagnostic(
    VmParserState *state,
    const VmLexerDiagnostic *diagnostic
);

static bool vm_parser_add_lexer_status_diagnostic(
    VmParserState *state,
    VmLexerStatus status
);

static bool vm_parser_parse_constant_expression(VmParserState *state, VmParserConstantExpression *out_expression);

static bool vm_parser_parse_data_initializer(
    VmParserState *state,
    VmSymbolDataType data_type,
    uint32_t *out_element_count,
    bool *out_has_uninitialized,
    bool *out_is_fully_uninitialized
);

static bool vm_parser_parse_equate_line_if_recognized(VmParserState *state);

static bool vm_parser_parse_option_directive(VmParserState *state);

static bool vm_parser_has_conflicting_data_symbol(VmParserState *state, const VmLexerToken *token);

static VmSymbolLookupStatus vm_parser_data_symbol_lookup_status(VmParserState *state, const VmLexerToken *token);

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

static uint64_t vm_parser_signed_positive_max_for_size(uint8_t size_bytes);

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

/// Virtual Irvine32 names recognized once `INCLUDE Irvine32.inc` is active.
static const VmParserIrvine32RegistryEntry VM_PARSER_IRVINE32_REGISTRY[] = {
    {"exit", VM_IRVINE32_SYMBOL_CLASS_SUPPORTED_VIRTUAL_INTRINSIC},
    {"Crlf", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"WriteChar", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"WriteString", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"WriteDec", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"WriteInt", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"WriteHex", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"WriteBin", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"DumpRegs", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"DumpMem", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"Randomize", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"Random32", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"RandomRange", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"WaitMsg", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"ReadChar", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"ReadInt", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"ReadDec", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"ReadHex", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"ReadString", VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE},
    {"MsgBox", VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL},
    {"MsgBoxAsk", VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL},
    {"ExitProcess", VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL},
    {"OpenInputFile", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"CreateOutputFile", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"ReadFromFile", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"WriteToFile", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"CloseFile", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"Clrscr", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"Gotoxy", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"SetTextColor", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE},
    {"Delay", VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE}
};

/// Returns whether a source span matches a null-terminated literal ignoring ASCII case.
///
/// @param text Source bytes to inspect.
/// @param text_length Number of bytes available in @p text.
/// @param literal Null-terminated literal to match.
/// @return true when the source span and literal match ignoring ASCII case.
static bool vm_parser_span_equals_ascii_case_insensitive(const char *text, size_t text_length, const char *literal) {
    size_t index = 0U;
    size_t literal_length = 0U;

    if (text == NULL || literal == NULL) {
        return false;
    }

    literal_length = strlen(literal);
    if (text_length != literal_length) {
        return false;
    }

    for (index = 0U; index < literal_length; index += 1U) {
        if (vm_parser_ascii_lower(text[index]) != vm_parser_ascii_lower(literal[index])) {
            return false;
        }
    }

    return true;
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

/// Compares two lexer token lexemes by exact source spelling.
///
/// @param left First token.
/// @param right Second token.
/// @return true when both lexemes match exactly.
static bool vm_parser_token_lexemes_equal_exact(const VmLexerToken *left, const VmLexerToken *right) {
    return left != NULL && right != NULL &&
           left->lexeme_length == right->lexeme_length &&
           left->lexeme != NULL && right->lexeme != NULL &&
           memcmp(left->lexeme, right->lexeme, left->lexeme_length) == 0;
}

/// Converts the parser CASEMAP policy to the symbol-module policy.
///
/// @param policy Parser CASEMAP policy to convert.
/// @return Equivalent symbol helper policy.
static VmSymbolCasePolicy vm_parser_symbol_case_policy(VmParserUserSymbolCasePolicy policy) {
    return policy == VM_PARSER_USER_SYMBOL_CASEMAP_NONE ? VM_SYMBOL_CASE_POLICY_NONE : VM_SYMBOL_CASE_POLICY_ALL;
}

/// Compares a source token to an accepted user-symbol spelling using active CASEMAP policy.
///
/// @param state Parser state containing the active user-symbol policy.
/// @param left First token.
/// @param right Second token.
/// @return true when both tokens match under the active policy.
static bool vm_parser_user_symbol_tokens_equal(const VmParserState *state, const VmLexerToken *left, const VmLexerToken *right) {
    if (state != NULL && state->user_symbol_case_policy == VM_PARSER_USER_SYMBOL_CASEMAP_NONE) {
        return vm_parser_token_lexemes_equal_exact(left, right);
    }

    return vm_parser_token_lexemes_equal(left, right);
}

/// Returns whether a token can begin a compile-time constant expression.
///
/// @param token Token to inspect.
/// @return true for numbers, equate identifiers, unary signs, and left parentheses.
static bool vm_parser_token_starts_constant_expression(const VmLexerToken *token) {
    return token != NULL &&
           (token->kind == VM_LEXER_TOKEN_NUMBER ||
            token->kind == VM_LEXER_TOKEN_IDENTIFIER ||
            token->kind == VM_LEXER_TOKEN_PLUS ||
            token->kind == VM_LEXER_TOKEN_MINUS ||
            token->kind == VM_LEXER_TOKEN_LEFT_PAREN);
}

/// Compares an equate slot name with a source token using active CASEMAP policy.
///
/// @param equate Equate slot to inspect.
/// @param token Source token containing the candidate name.
/// @param policy Active user-symbol case policy.
/// @return true when the names match.
static bool vm_parser_equate_name_equals(const VmParserEquate *equate, const VmLexerToken *token, VmParserUserSymbolCasePolicy policy) {
    size_t index = 0U;

    if (equate == NULL || token == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return false;
    }

    if (policy == VM_PARSER_USER_SYMBOL_CASEMAP_NONE) {
        return strlen(equate->name) == token->lexeme_length && memcmp(equate->name, token->lexeme, token->lexeme_length) == 0;
    }

    while (index < token->lexeme_length && equate->name[index] != '\0') {
        if (vm_parser_ascii_lower(equate->name[index]) != vm_parser_ascii_lower(token->lexeme[index])) {
            return false;
        }
        index += 1U;
    }

    return index == token->lexeme_length && equate->name[index] == '\0';
}

/// Finds a numeric equate by source token name using active CASEMAP policy.
///
/// @param state Parser state whose equate table should be searched.
/// @param token Source token containing the equate name.
/// @param out_ambiguous Receives true when folded lookup matched multiple equates.
/// @return Matching equate slot, including invalid definitions retained for
/// diagnostic suppression, or NULL when no equate with that name was seen.
static VmParserEquate *vm_parser_find_equate_with_ambiguity(VmParserState *state, const VmLexerToken *token, bool *out_ambiguous) {
    size_t index = 0U;
    VmParserEquate *match = NULL;
    size_t match_count = 0U;

    if (out_ambiguous != NULL) {
        *out_ambiguous = false;
    }
    if (state == NULL || token == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return NULL;
    }

    for (index = 0U; index < state->equate_count; index += 1U) {
        if (vm_parser_equate_name_equals(&state->equates[index], token, state->user_symbol_case_policy)) {
            match = &state->equates[index];
            match_count += 1U;
        }
    }

    if (match_count > 1U) {
        if (out_ambiguous != NULL) {
            *out_ambiguous = true;
        }
        return NULL;
    }

    return match_count == 1U ? match : NULL;
}

/// Finds a numeric equate by source token name.
///
/// @param state Parser state whose equate table should be searched.
/// @param token Source token containing the equate name.
/// @return Matching equate slot, including invalid definitions retained for
/// diagnostic suppression, or NULL when no equate with that name was seen.
static VmParserEquate *vm_parser_find_equate(VmParserState *state, const VmLexerToken *token) {
    return vm_parser_find_equate_with_ambiguity(state, token, NULL);
}

/// Returns whether a token should be parsed as a numeric equate expression.
///
/// @param state Parser state whose equate table should be searched.
/// @param token Source token to inspect.
/// @return true when the token names a visible equate or an ambiguous folded
/// equate set that must be diagnosed by constant-expression parsing.
static bool vm_parser_token_names_equate_expression(VmParserState *state, const VmLexerToken *token) {
    bool ambiguous = false;

    if (vm_parser_find_equate_with_ambiguity(state, token, &ambiguous) != NULL) {
        return true;
    }

    return ambiguous;
}

/// Copies an equate name into an internal fixed-capacity slot.
///
/// @param equate Destination equate slot.
/// @param token Source name token.
/// @return true when the name was copied.
static bool vm_parser_set_equate_name(VmParserEquate *equate, const VmLexerToken *token) {
    if (equate == NULL || token == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER ||
        token->lexeme_length == 0U || token->lexeme_length >= (size_t)VM_SYMBOL_NAME_CAPACITY) {
        return false;
    }

    memcpy(equate->name, token->lexeme, token->lexeme_length);
    equate->name[token->lexeme_length] = '\0';
    return true;
}

/// Converts a lexer number token to a signed 64-bit expression value.
///
/// Compile-time constant expressions use signed 64-bit evaluation and leave final
/// storage-width validation to the consuming instruction or data declaration.
///
/// @param token Number token to convert.
/// @param out_value Receives the signed expression value.
/// @return true when the token fits signed 64-bit expression evaluation.
static bool vm_parser_number_token_to_i64_expression(const VmLexerToken *token, int64_t *out_value) {
    if (token == NULL || out_value == NULL || token->kind != VM_LEXER_TOKEN_NUMBER) {
        return false;
    }

    if (token->number_is_negative) {
        if (token->number_value > ((uint64_t)INT64_MAX + 1ULL)) {
            return false;
        }
        if (token->number_value == ((uint64_t)INT64_MAX + 1ULL)) {
            *out_value = INT64_MIN;
        } else {
            *out_value = -(int64_t)token->number_value;
        }
        return true;
    }

    if (token->number_value > (uint64_t)INT64_MAX) {
        return false;
    }

    *out_value = (int64_t)token->number_value;
    return true;
}

/// Adds two signed 64-bit expression values with overflow detection.
///
/// @param left Left operand.
/// @param right Right operand.
/// @param out_value Receives the sum.
/// @return true when no signed overflow occurred.
static bool vm_parser_add_i64_checked(int64_t left, int64_t right, int64_t *out_value) {
    if (out_value == NULL) {
        return false;
    }
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) {
        return false;
    }
    *out_value = left + right;
    return true;
}

/// Subtracts two signed 64-bit expression values with overflow detection.
///
/// @param left Left operand.
/// @param right Right operand.
/// @param out_value Receives the difference.
/// @return true when no signed overflow occurred.
static bool vm_parser_sub_i64_checked(int64_t left, int64_t right, int64_t *out_value) {
    if (out_value == NULL) {
        return false;
    }
    if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right)) {
        return false;
    }
    *out_value = left - right;
    return true;
}

/// Returns whether an identifier token names a unary compile-time operator.
///
/// @param token Token to inspect.
/// @return true for Phase 29 unary constant-expression operators.
static bool vm_parser_token_is_constant_unary_operator(const VmLexerToken *token) {
    return token != NULL && token->kind == VM_LEXER_TOKEN_IDENTIFIER &&
           (vm_parser_token_equals(token, "NOT") ||
            vm_parser_token_equals(token, "HIGH") ||
            vm_parser_token_equals(token, "LOW") ||
            vm_parser_token_equals(token, "HIGHWORD") ||
            vm_parser_token_equals(token, "LOWWORD"));
}

/// Returns whether an identifier token names a deferred high-level condition operator.
///
/// @param token Token to inspect.
/// @return true for MASM-style relational operators that are not Phase 29
/// compile-time operators.
static bool vm_parser_token_is_deferred_condition_operator(const VmLexerToken *token) {
    return token != NULL && token->kind == VM_LEXER_TOKEN_IDENTIFIER &&
           (vm_parser_token_equals(token, "EQ") ||
            vm_parser_token_equals(token, "NE") ||
            vm_parser_token_equals(token, "LT") ||
            vm_parser_token_equals(token, "LE") ||
            vm_parser_token_equals(token, "GT") ||
            vm_parser_token_equals(token, "GE"));
}

/// Multiplies two signed 64-bit expression values with overflow detection.
///
/// @param left Left operand.
/// @param right Right operand.
/// @param out_value Receives the product.
/// @return true when no signed overflow occurred.
static bool vm_parser_mul_i64_checked(int64_t left, int64_t right, int64_t *out_value) {
    if (out_value == NULL) {
        return false;
    }
    if (left == 0 || right == 0) {
        *out_value = 0;
        return true;
    }
    if ((left == INT64_MIN && right == -1) || (right == INT64_MIN && left == -1)) {
        return false;
    }
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) ||
            (right < 0 && right < INT64_MIN / left)) {
            return false;
        }
    } else {
        if ((right > 0 && left < INT64_MIN / right) ||
            (right < 0 && left < INT64_MAX / right)) {
            return false;
        }
    }
    *out_value = left * right;
    return true;
}

/// Divides two signed 64-bit expression values with error checks.
///
/// @param left Dividend.
/// @param right Divisor.
/// @param out_value Receives the quotient.
/// @return true when the division is valid for signed 64-bit values.
static bool vm_parser_div_i64_checked(int64_t left, int64_t right, int64_t *out_value) {
    if (out_value == NULL || right == 0 || (left == INT64_MIN && right == -1)) {
        return false;
    }
    *out_value = left / right;
    return true;
}

/// Computes a signed 64-bit remainder with error checks.
///
/// @param left Dividend.
/// @param right Divisor.
/// @param out_value Receives the remainder.
/// @return true when the modulo operation is valid for signed 64-bit values.
static bool vm_parser_mod_i64_checked(int64_t left, int64_t right, int64_t *out_value) {
    if (out_value == NULL || right == 0 || (left == INT64_MIN && right == -1)) {
        return false;
    }
    *out_value = left % right;
    return true;
}

/// Applies a logical shift to a folded 64-bit expression value.
///
/// @param left Value to shift, interpreted as an unsigned 64-bit bit pattern.
/// @param right Shift count, which must be in the range 0 through 63.
/// @param is_left_shift Whether to perform SHL instead of SHR.
/// @param out_value Receives the shifted bit pattern as a signed 64-bit value.
/// @return true when the shift count is supported.
static bool vm_parser_shift_i64_checked(int64_t left, int64_t right, bool is_left_shift, int64_t *out_value) {
    uint64_t bits = 0ULL;
    uint32_t count = 0U;

    if (out_value == NULL || right < 0 || right >= 64) {
        return false;
    }
    bits = (uint64_t)left;
    count = (uint32_t)right;
    bits = is_left_shift ? (bits << count) : (bits >> count);
    *out_value = (int64_t)bits;
    return true;
}

/// Parses one complete Phase 29 constant expression.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed.
static bool vm_parser_parse_constant_bitwise_or_expression(VmParserState *state, VmParserConstantExpression *out_expression);

/// Parses a primary constant-expression term.
///
/// @param state Parser state positioned at the term.
/// @param out_expression Receives the parsed value.
/// @return true when a term was parsed.
static bool vm_parser_parse_constant_primary_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (state == NULL || out_expression == NULL) {
        return false;
    }

    memset(out_expression, 0, sizeof(*out_expression));
    out_expression->start_token = token;

    if (token == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, token, "Expected a constant expression.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_NUMBER) {
        if (!vm_parser_number_token_to_i64_expression(token, &out_expression->value)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, token, "Constant expression value is outside the supported signed 64-bit range.");
            return false;
        }
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        VmParserEquate *equate = NULL;
        bool ambiguous_equate = false;
        if (state->active_equate_name != NULL && vm_parser_user_symbol_tokens_equal(state, state->active_equate_name, token)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_RECURSIVE_EQUATE, token, "Numeric equate cannot reference itself.");
            return false;
        }
        equate = vm_parser_find_equate_with_ambiguity(state, token, &ambiguous_equate);
        if (ambiguous_equate) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL, token, "Multiple numeric equates match this reference under CASEMAP:ALL because their names differ only by letter case. Use OPTION CASEMAP:NONE and the exact equate spelling, or make the equate names distinct beyond case.");
            return false;
        }
        if (equate == NULL) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNKNOWN_EQUATE, token, "Constant expression references an unknown numeric equate.");
            return false;
        }
        if (equate->is_resolving) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_RECURSIVE_EQUATE, token, "Recursive numeric equate reference is not allowed.");
            return false;
        }
        if (equate->is_invalid || !equate->is_defined) {
            return false;
        }
        out_expression->value = equate->value;
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_LEFT_PAREN) {
        vm_parser_advance(state);
        if (!vm_parser_parse_constant_bitwise_or_expression(state, out_expression)) {
            return false;
        }
        token = vm_parser_current_token(state);
        if (token == NULL || token->kind != VM_LEXER_TOKEN_RIGHT_PAREN) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, token, "Expected ')' after constant expression.");
            return false;
        }
        vm_parser_advance(state);
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, token, "Expected a numeric literal, equate identifier, unary operator, or parenthesized constant expression.");
    return false;
}

/// Parses a unary constant expression, including Phase 29 unary operators.
///
/// @param state Parser state positioned at the unary expression.
/// @param out_expression Receives the parsed value.
/// @return true when a unary expression was parsed.
static bool vm_parser_parse_constant_unary_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    const VmLexerToken *token = vm_parser_current_token(state);
    VmParserConstantExpression inner;

    if (state == NULL || out_expression == NULL) {
        return false;
    }

    if (token != NULL && (token->kind == VM_LEXER_TOKEN_PLUS || token->kind == VM_LEXER_TOKEN_MINUS || vm_parser_token_is_constant_unary_operator(token))) {
        memset(&inner, 0, sizeof(inner));
        vm_parser_advance(state);
        if (!vm_parser_parse_constant_unary_expression(state, &inner)) {
            return false;
        }
        *out_expression = inner;
        out_expression->start_token = token;
        if (token->kind == VM_LEXER_TOKEN_MINUS) {
            if (inner.value == INT64_MIN) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, token, "Constant expression unary minus overflowed signed 64-bit range.");
                return false;
            }
            out_expression->value = -inner.value;
        } else if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "NOT")) {
            out_expression->value = (int64_t)(~(uint64_t)inner.value);
        } else if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "LOW")) {
            out_expression->value = (int64_t)((uint64_t)inner.value & 0xFFULL);
        } else if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "HIGH")) {
            out_expression->value = (int64_t)(((uint64_t)inner.value >> 8U) & 0xFFULL);
        } else if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "LOWWORD")) {
            out_expression->value = (int64_t)((uint64_t)inner.value & 0xFFFFULL);
        } else if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "HIGHWORD")) {
            out_expression->value = (int64_t)(((uint64_t)inner.value >> 16U) & 0xFFFFULL);
        }
        return true;
    }

    return vm_parser_parse_constant_primary_expression(state, out_expression);
}

/// Parses multiplicative constant expressions: `*`, `/`, and `MOD`.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed.
static bool vm_parser_parse_constant_multiplicative_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    VmParserConstantExpression right;

    if (state == NULL || out_expression == NULL) {
        return false;
    }
    if (!vm_parser_parse_constant_unary_expression(state, out_expression)) {
        return false;
    }

    while (true) {
        const VmLexerToken *operator_token = vm_parser_current_token(state);
        int64_t combined = 0;
        bool is_mod = false;

        if (operator_token == NULL ||
            !(operator_token->kind == VM_LEXER_TOKEN_ASTERISK || operator_token->kind == VM_LEXER_TOKEN_SLASH ||
              (operator_token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(operator_token, "MOD")))) {
            break;
        }
        is_mod = operator_token->kind == VM_LEXER_TOKEN_IDENTIFIER;
        vm_parser_advance(state);
        memset(&right, 0, sizeof(right));
        if (!vm_parser_parse_constant_unary_expression(state, &right)) {
            return false;
        }

        if (operator_token->kind == VM_LEXER_TOKEN_ASTERISK) {
            if (!vm_parser_mul_i64_checked(out_expression->value, right.value, &combined)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, operator_token, "Constant expression multiplication overflowed signed 64-bit range.");
                return false;
            }
        } else if (is_mod) {
            if (!vm_parser_mod_i64_checked(out_expression->value, right.value, &combined)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, operator_token, "Constant expression MOD requires a non-zero divisor.");
                return false;
            }
        } else {
            if (!vm_parser_div_i64_checked(out_expression->value, right.value, &combined)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, operator_token, "Constant expression division requires a non-zero divisor and non-overflowing quotient.");
                return false;
            }
        }
        out_expression->value = combined;
    }

    return true;
}

/// Parses additive constant expressions: `+` and `-`.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed.
static bool vm_parser_parse_constant_additive_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    VmParserConstantExpression right;

    if (state == NULL || out_expression == NULL) {
        return false;
    }
    if (!vm_parser_parse_constant_multiplicative_expression(state, out_expression)) {
        return false;
    }

    while (true) {
        const VmLexerToken *operator_token = vm_parser_current_token(state);
        int64_t combined = 0;

        if (operator_token == NULL || (operator_token->kind != VM_LEXER_TOKEN_PLUS && operator_token->kind != VM_LEXER_TOKEN_MINUS)) {
            break;
        }

        vm_parser_advance(state);
        memset(&right, 0, sizeof(right));
        if (!vm_parser_parse_constant_multiplicative_expression(state, &right)) {
            return false;
        }

        if (operator_token->kind == VM_LEXER_TOKEN_PLUS) {
            if (!vm_parser_add_i64_checked(out_expression->value, right.value, &combined)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, operator_token, "Constant expression addition overflowed signed 64-bit range.");
                return false;
            }
        } else {
            if (!vm_parser_sub_i64_checked(out_expression->value, right.value, &combined)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, operator_token, "Constant expression subtraction overflowed signed 64-bit range.");
                return false;
            }
        }
        out_expression->value = combined;
    }

    return true;
}

/// Parses shift constant expressions: `SHL` and `SHR`.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed.
static bool vm_parser_parse_constant_shift_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    VmParserConstantExpression right;

    if (state == NULL || out_expression == NULL) {
        return false;
    }
    if (!vm_parser_parse_constant_additive_expression(state, out_expression)) {
        return false;
    }

    while (true) {
        const VmLexerToken *operator_token = vm_parser_current_token(state);
        int64_t combined = 0;
        bool is_left_shift = false;

        if (operator_token == NULL || operator_token->kind != VM_LEXER_TOKEN_IDENTIFIER ||
            !(vm_parser_token_equals(operator_token, "SHL") || vm_parser_token_equals(operator_token, "SHR"))) {
            break;
        }
        is_left_shift = vm_parser_token_equals(operator_token, "SHL");
        vm_parser_advance(state);
        memset(&right, 0, sizeof(right));
        if (!vm_parser_parse_constant_additive_expression(state, &right)) {
            return false;
        }
        if (!vm_parser_shift_i64_checked(out_expression->value, right.value, is_left_shift, &combined)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, operator_token, "Constant expression shift count must be in the range 0 through 63.");
            return false;
        }
        out_expression->value = combined;
    }

    return true;
}

/// Parses bitwise AND constant expressions.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed.
static bool vm_parser_parse_constant_bitwise_and_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    VmParserConstantExpression right;

    if (state == NULL || out_expression == NULL) {
        return false;
    }
    if (!vm_parser_parse_constant_shift_expression(state, out_expression)) {
        return false;
    }

    while (true) {
        const VmLexerToken *operator_token = vm_parser_current_token(state);
        if (operator_token == NULL || operator_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_parser_token_equals(operator_token, "AND")) {
            break;
        }
        vm_parser_advance(state);
        memset(&right, 0, sizeof(right));
        if (!vm_parser_parse_constant_shift_expression(state, &right)) {
            return false;
        }
        out_expression->value = (int64_t)((uint64_t)out_expression->value & (uint64_t)right.value);
    }

    return true;
}

/// Parses bitwise XOR constant expressions.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed.
static bool vm_parser_parse_constant_bitwise_xor_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    VmParserConstantExpression right;

    if (state == NULL || out_expression == NULL) {
        return false;
    }
    if (!vm_parser_parse_constant_bitwise_and_expression(state, out_expression)) {
        return false;
    }

    while (true) {
        const VmLexerToken *operator_token = vm_parser_current_token(state);
        if (operator_token == NULL || operator_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_parser_token_equals(operator_token, "XOR")) {
            break;
        }
        vm_parser_advance(state);
        memset(&right, 0, sizeof(right));
        if (!vm_parser_parse_constant_bitwise_and_expression(state, &right)) {
            return false;
        }
        out_expression->value = (int64_t)((uint64_t)out_expression->value ^ (uint64_t)right.value);
    }

    return true;
}

/// Parses bitwise OR constant expressions.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed.
static bool vm_parser_parse_constant_bitwise_or_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    VmParserConstantExpression right;

    if (state == NULL || out_expression == NULL) {
        return false;
    }
    if (!vm_parser_parse_constant_bitwise_xor_expression(state, out_expression)) {
        return false;
    }

    while (true) {
        const VmLexerToken *operator_token = vm_parser_current_token(state);
        if (operator_token == NULL || operator_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_parser_token_equals(operator_token, "OR")) {
            break;
        }
        vm_parser_advance(state);
        memset(&right, 0, sizeof(right));
        if (!vm_parser_parse_constant_bitwise_xor_expression(state, &right)) {
            return false;
        }
        out_expression->value = (int64_t)((uint64_t)out_expression->value | (uint64_t)right.value);
    }

    return true;
}

/// Parses a compile-time constant expression from the current token.
///
/// @param state Parser state positioned at the expression.
/// @param out_expression Receives the parsed value.
/// @return true when the expression was parsed and left at a natural terminator.
static bool vm_parser_parse_constant_expression(VmParserState *state, VmParserConstantExpression *out_expression) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (state == NULL || out_expression == NULL) {
        return false;
    }

    if (!vm_parser_token_starts_constant_expression(token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, token, "Expected a constant expression.");
        return false;
    }

    return vm_parser_parse_constant_bitwise_or_expression(state, out_expression);
}

/// Returns whether two tokens are contiguous bytes on the same source line.
///
/// @param left First token in source order.
/// @param right Token expected to begin immediately after @p left.
/// @return true when the second token starts immediately after the first token.
static bool vm_parser_tokens_are_contiguous(const VmLexerToken *left, const VmLexerToken *right) {
    if (left == NULL || right == NULL) {
        return false;
    }

    return left->location.line == right->location.line &&
           right->location.offset == left->location.offset + left->lexeme_length;
}

/// Returns whether a token pair spells the deferred `.DATA?` directive.
///
/// The lexer intentionally keeps `.data` and `?` as separate tokens because `?`
/// is already used by data initializers. This helper recognizes only the
/// contiguous directive form so ordinary `.data` followed by an initializer is
/// not misclassified.
///
/// @param directive_token Candidate `.data` directive token.
/// @param question_token Candidate question-mark token immediately after it.
/// @return true when the pair represents `.DATA?`.
static bool vm_parser_is_data_question_directive(const VmLexerToken *directive_token, const VmLexerToken *question_token) {
    return directive_token != NULL && question_token != NULL &&
           directive_token->kind == VM_LEXER_TOKEN_DIRECTIVE &&
           question_token->kind == VM_LEXER_TOKEN_QUESTION &&
           vm_parser_token_equals(directive_token, ".data") &&
           vm_parser_tokens_are_contiguous(directive_token, question_token);
}

/// Returns whether a token pair spells a specific deferred `.<name>?` directive.
///
/// @param directive_token Candidate dot-prefixed directive token.
/// @param question_token Candidate question-mark token immediately after it.
/// @param directive_spelling Dot-prefixed directive spelling without the trailing question mark.
/// @return true when the pair represents the requested question-suffixed directive.
static bool vm_parser_is_question_directive(
    const VmLexerToken *directive_token,
    const VmLexerToken *question_token,
    const char *directive_spelling
) {
    return directive_token != NULL && question_token != NULL && directive_spelling != NULL &&
           directive_token->kind == VM_LEXER_TOKEN_DIRECTIVE &&
           question_token->kind == VM_LEXER_TOKEN_QUESTION &&
           vm_parser_token_equals(directive_token, directive_spelling) &&
           vm_parser_tokens_are_contiguous(directive_token, question_token);
}

/// Looks up a static unsupported-feature message by exact token spelling.
///
/// @param token Candidate token to classify.
/// @param features Table of unsupported-feature descriptors.
/// @param feature_count Number of entries in @p features.
/// @return Static message for a recognized feature, or NULL.
static const char *vm_parser_find_unsupported_feature_message(
    const VmLexerToken *token,
    const VmParserUnsupportedFeature *features,
    size_t feature_count
) {
    size_t index = 0U;

    if (token == NULL || features == NULL) {
        return NULL;
    }

    for (index = 0U; index < feature_count; index += 1U) {
        if (vm_parser_token_equals(token, features[index].spelling)) {
            return features[index].message;
        }
    }

    return NULL;
}

/// Returns an unsupported-feature message for a recognized directive token.
///
/// @param token Candidate directive token.
/// @param next Next token, used only for `.DATA?` recognition.
/// @return Static unsupported-feature message, or NULL when not recognized.
static const char *vm_parser_unsupported_directive_message(const VmLexerToken *token, const VmLexerToken *next) {
    static const VmParserUnsupportedFeature directives[] = {
        {".if", "Unsupported feature: MASM .IF high-level flow is not supported yet."},
        {".while", "Unsupported feature: MASM .WHILE high-level flow is not supported yet."},
        {".repeat", "Unsupported feature: MASM .REPEAT high-level flow is not supported yet."},
        {".break", "Unsupported feature: MASM .BREAK high-level flow is not supported yet."},
        {".continue", "Unsupported feature: MASM .CONTINUE high-level flow is not supported yet."},
        {".startup", "Unsupported feature: .STARTUP is not supported yet."},
        {".exit", "Unsupported feature: .EXIT is not supported yet."},
        {".dosseg", "Unsupported feature: .DOSSEG segmented-layout compatibility is not supported."},
        {".fardata", "Unsupported feature: .FARDATA sections are not supported."},
        {".list", "Unsupported feature: .LIST listing control is not supported yet."},
        {".nolist", "Unsupported feature: .NOLIST listing control is not supported yet."},
        {".cref", "Unsupported feature: .CREF listing control is not supported yet."},
        {".nocref", "Unsupported feature: .NOCREF listing control is not supported yet."},
        {".tfcond", "Unsupported feature: .TFCOND listing control is not supported yet."},
        {".err", "Unsupported feature: .ERR conditional error directives are not supported yet."},
        {".errb", "Unsupported feature: .ERRB conditional error directives are not supported yet."},
        {".errdef", "Unsupported feature: .ERRDEF conditional error directives are not supported yet."},
        {".erre", "Unsupported feature: .ERRE conditional error directives are not supported yet."},
        {".errnz", "Unsupported feature: .ERRNZ conditional error directives are not supported yet."},
        {".mmx", "Unsupported feature: .MMX processor extensions are not modeled in MASM32 Educational Mode."},
        {".xmm", "Unsupported feature: .XMM processor extensions are not modeled in MASM32 Educational Mode."},
        {".k3d", "Unsupported feature: .K3D processor extensions are not modeled in MASM32 Educational Mode."},
        {".safeseh", "Unsupported feature: .SAFESEH object metadata is not supported."},
        {".fpo", "Unsupported feature: .FPO object metadata is not supported."}
    };

    if (vm_parser_is_question_directive(token, next, ".fardata")) {
        return "Unsupported feature: .FARDATA? sections are not supported.";
    }

    return vm_parser_find_unsupported_feature_message(token, directives, sizeof(directives) / sizeof(directives[0]));
}

/// Returns an unsupported-feature message for a recognized keyword token.
///
/// @param token Candidate identifier token.
/// @return Static unsupported-feature message, or NULL when not recognized.
static const char *vm_parser_unsupported_keyword_message(const VmLexerToken *token) {
    static const VmParserUnsupportedFeature keywords[] = {
        {"textequ", "Unsupported feature: TEXTEQU text constants are not supported yet."},
        {"struct", "Unsupported feature: STRUCT declarations are not supported yet."},
        {"union", "Unsupported feature: UNION declarations are not supported yet."},
        {"record", "Unsupported feature: RECORD declarations are not supported yet."},
        {"invoke", "Unsupported feature: INVOKE is not supported yet; use CALL when available."},
        {"proto", "Unsupported feature: PROTO procedure metadata is not supported yet."},
        {"local", "Unsupported feature: LOCAL procedure variables are not supported yet."},
        {"macro", "Unsupported feature: MASM macro definitions are not supported yet."},
        {"endm", "Unsupported feature: MASM macro definitions are not supported yet."},
        {"includelib", "Unsupported feature: INCLUDELIB linker declarations are not supported yet."},
        {"extern", "Unsupported feature: EXTERN declarations are not supported yet."},
        {"externdef", "Unsupported feature: EXTERNDEF declarations are not supported yet."},
        {"extrn", "Unsupported feature: EXTRN declarations are not supported yet."},
        {"public", "Unsupported feature: PUBLIC declarations are not supported yet."},
        {"comm", "Unsupported feature: COMM declarations are not supported yet."},
        {"assume", "Unsupported feature: ASSUME segment assumptions are not supported."},
        {"align", "Unsupported feature: ALIGN is not supported yet."},
        {"even", "Unsupported feature: EVEN alignment is not supported yet."},
        {"label", "Unsupported feature: LABEL declarations are not supported yet."},
        {"org", "Unsupported feature: ORG location control is not supported."},
        {"comment", "Unsupported feature: COMMENT block comments are not supported yet."},
        {"echo", "Unsupported feature: ECHO listing output is not supported yet."},
        {"if", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"if2", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"ifdef", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"ifndef", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"ife", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"ifb", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"ifnb", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"else", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"elseif", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"endif", "Unsupported feature: conditional assembly directives are not supported yet."},
        {"exitm", "Unsupported feature: MASM macro exit directives are not supported yet."},
        {"purge", "Unsupported feature: MASM macro purge directives are not supported yet."},
        {"for", "Unsupported feature: MASM macro repeat directives are not supported yet."},
        {"forc", "Unsupported feature: MASM macro repeat directives are not supported yet."},
        {"goto", "Unsupported feature: MASM macro GOTO directives are not supported yet."},
        {"pushcontext", "Unsupported feature: PUSHCONTEXT assembler context directives are not supported."},
        {"popcontext", "Unsupported feature: POPCONTEXT assembler context directives are not supported."}
    };

    return vm_parser_find_unsupported_feature_message(token, keywords, sizeof(keywords) / sizeof(keywords[0]));
}

/// Returns an unsupported-feature message for a deferred data type.
///
/// Signed integer declarations are implemented in Milestone 18. Non-integer
/// declaration families remain documented backlog items and are still classified
/// without implementing their storage semantics.
///
/// @param token Candidate data-type token.
/// @return Static unsupported-feature message, or NULL when the type is not recognized.
static const char *vm_parser_unsupported_data_type_message(const VmLexerToken *token) {
    static const VmParserUnsupportedFeature data_types[] = {
        {"real4", "Unsupported feature: REAL4 floating-point declarations are backlog work."},
        {"real8", "Unsupported feature: REAL8 floating-point declarations are backlog work."},
        {"real10", "Unsupported feature: REAL10 floating-point declarations are backlog work."},
        {"tbyte", "Unsupported feature: TBYTE declarations are backlog work."},
        {"fword", "Unsupported feature: FWORD declarations are backlog work."}
    };

    return vm_parser_find_unsupported_feature_message(token, data_types, sizeof(data_types) / sizeof(data_types[0]));
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
/// Records one parser diagnostic with explicit severity.
///
/// @param state Parser state whose diagnostic buffer should receive the entry.
/// @param code Diagnostic code.
/// @param severity Diagnostic severity.
/// @param token Optional token associated with the diagnostic.
/// @param message Static diagnostic message.
/// @return true when the diagnostic was recorded.
static bool vm_parser_add_diagnostic_with_severity(
    VmParserState *state,
    VmParserDiagnosticCode code,
    VmParserDiagnosticSeverity severity,
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
    diagnostic->severity = severity;
    if (token != NULL) {
        diagnostic->location = token->location;
        diagnostic->lexeme = token->lexeme;
        diagnostic->lexeme_length = token->lexeme_length;
    }
    diagnostic->message = message;
    state->result->diagnostic_count += 1U;
    return true;
}

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
    return vm_parser_add_diagnostic_with_severity(state, code, VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR, token, message);
}

/// Records one non-fatal parser warning.
///
/// @param state Parser state whose diagnostic buffer should receive the entry.
/// @param code Diagnostic code.
/// @param token Optional token associated with the warning.
/// @param message Static warning message.
/// @return true when the warning was recorded.
static bool vm_parser_add_warning(
    VmParserState *state,
    VmParserDiagnosticCode code,
    const VmLexerToken *token,
    const char *message
) {
    return vm_parser_add_diagnostic_with_severity(state, code, VM_PARSER_DIAGNOSTIC_SEVERITY_WARNING, token, message);
}

/// Records one parser diagnostic whose message includes parse-specific values.
///
/// @param state Parser state whose diagnostic buffer should receive the entry.
/// @param code Diagnostic code.
/// @param token Optional token associated with the diagnostic.
/// @param format printf-style message format.
/// @return true when the diagnostic was recorded.
static bool vm_parser_add_formatted_diagnostic(
    VmParserState *state,
    VmParserDiagnosticCode code,
    const VmLexerToken *token,
    const char *format,
    ...
) {
    VmParserDiagnostic *diagnostic = NULL;
    va_list args;

    if (state == NULL || state->config == NULL || state->result == NULL || format == NULL) {
        return false;
    }

    if (state->result->diagnostic_count >= state->config->diagnostic_capacity || state->config->diagnostics == NULL) {
        state->diagnostic_overflowed = true;
        return false;
    }

    diagnostic = &state->config->diagnostics[state->result->diagnostic_count];
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->code = code;
    diagnostic->severity = VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR;
    if (token != NULL) {
        diagnostic->location = token->location;
        diagnostic->lexeme = token->lexeme;
        diagnostic->lexeme_length = token->lexeme_length;
    }

    va_start(args, format);
    (void)vsnprintf(diagnostic->message_storage, sizeof(diagnostic->message_storage), format, args);
    va_end(args);
    diagnostic->message = diagnostic->message_storage;

    state->result->diagnostic_count += 1U;
    return true;
}

/// Skips tokens through the end of the current source line.
///
/// @param state Parser state to mutate.
static void vm_parser_recover_skip_line(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);

    while (token != NULL && token->kind != VM_LEXER_TOKEN_EOF && token->kind != VM_LEXER_TOKEN_NEWLINE) {
        vm_parser_advance(state);
        token = vm_parser_current_token(state);
    }

    if (token != NULL && token->kind == VM_LEXER_TOKEN_NEWLINE) {
        vm_parser_advance(state);
    }
}

/// Returns whether a token is an identifier or directive with a specific spelling.
///
/// @param token Token to inspect.
/// @param spelling Case-insensitive spelling to match.
/// @return true when @p token has the requested spelling.
static bool vm_parser_recover_token_has_spelling(const VmLexerToken *token, const char *spelling) {
    return token != NULL && spelling != NULL &&
           (token->kind == VM_LEXER_TOKEN_IDENTIFIER || token->kind == VM_LEXER_TOKEN_DIRECTIVE) &&
           vm_parser_token_equals(token, spelling);
}

/// Returns whether a token is one of up to three recovery terminators.
///
/// @param token Token to inspect.
/// @param first First case-insensitive terminator spelling.
/// @param second Optional second terminator spelling.
/// @param third Optional third terminator spelling.
/// @return true when @p token matches any supplied terminator.
static bool vm_parser_recover_is_terminator(
    const VmLexerToken *token,
    const char *first,
    const char *second,
    const char *third
) {
    return vm_parser_recover_token_has_spelling(token, first) ||
           vm_parser_recover_token_has_spelling(token, second) ||
           vm_parser_recover_token_has_spelling(token, third);
}

/// Skips an unsupported block-like construct and consumes its terminator line.
///
/// This recovery deliberately ignores all tokens inside the block so unsupported
/// bodies do not produce cascaded parser diagnostics. Missing terminators leave
/// the parser at EOF, where the normal final END check reports the remaining
/// fatal structure problem.
///
/// @param state Parser state to mutate.
/// @param first_terminator First accepted block terminator spelling.
/// @param second_terminator Optional second accepted block terminator spelling.
/// @param third_terminator Optional third accepted block terminator spelling.
static void vm_parser_recover_skip_block(
    VmParserState *state,
    const char *first_terminator,
    const char *second_terminator,
    const char *third_terminator
) {
    const VmLexerToken *token = NULL;

    if (state == NULL) {
        return;
    }

    vm_parser_recover_skip_line(state);
    token = vm_parser_current_token(state);
    while (token != NULL && token->kind != VM_LEXER_TOKEN_EOF) {
        if (vm_parser_recover_is_terminator(token, first_terminator, second_terminator, third_terminator)) {
            vm_parser_recover_skip_line(state);
            return;
        }
        vm_parser_advance(state);
        token = vm_parser_current_token(state);
    }
}

/// Skips an unsupported section until the next known section directive or EOF.
///
/// This keeps section recovery narrow: declarations inside an unsupported
/// section are ignored, but later supported data-like sections and `.code`
/// remain visible to the normal parser.
///
/// @param state Parser state to mutate.
static void vm_parser_recover_skip_unsupported_section(VmParserState *state) {
    const VmLexerToken *token = NULL;
    const VmLexerToken *next = NULL;

    if (state == NULL) {
        return;
    }

    vm_parser_recover_skip_line(state);
    token = vm_parser_current_token(state);
    while (token != NULL && token->kind != VM_LEXER_TOKEN_EOF) {
        next = vm_parser_peek_token(state, 1U);
        if (token->kind == VM_LEXER_TOKEN_DIRECTIVE &&
            (vm_parser_token_equals(token, ".code") ||
             vm_parser_token_equals(token, ".data") ||
             vm_parser_token_equals(token, ".const") ||
             vm_parser_is_data_question_directive(token, next))) {
            return;
        }
        vm_parser_advance(state);
        token = vm_parser_current_token(state);
    }
}

/// Adds a recognized unsupported-feature diagnostic and skips its safe recovery span.
///
/// @param state Parser state to mutate.
/// @return true when a known unsupported construct was diagnosed and skipped.
static bool vm_parser_recover_unsupported_feature_if_recognized(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);
    const VmLexerToken *next = vm_parser_peek_token(state, 1U);
    const char *message = NULL;
    const VmLexerToken *diagnostic_token = token;

    if (state == NULL || token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
        return false;
    }

    if (vm_parser_is_question_directive(token, next, ".fardata")) {
        message = vm_parser_unsupported_directive_message(token, next);
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, token, message);
        vm_parser_recover_skip_unsupported_section(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE && vm_parser_token_equals(token, ".if")) {
        message = vm_parser_unsupported_directive_message(token, next);
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, token, message);
        vm_parser_recover_skip_block(state, ".endif", NULL, NULL);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE && vm_parser_token_equals(token, ".while")) {
        message = vm_parser_unsupported_directive_message(token, next);
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, token, message);
        vm_parser_recover_skip_block(state, ".endw", NULL, NULL);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE && vm_parser_token_equals(token, ".repeat")) {
        message = vm_parser_unsupported_directive_message(token, next);
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, token, message);
        vm_parser_recover_skip_block(state, ".until", ".untilcxz", NULL);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE &&
        (vm_parser_token_equals(token, ".break") || vm_parser_token_equals(token, ".continue"))) {
        message = vm_parser_unsupported_directive_message(token, next);
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, token, message);
        vm_parser_recover_skip_line(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE) {
        message = vm_parser_unsupported_directive_message(token, next);
        if (message != NULL) {
            (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, token, message);
            vm_parser_recover_skip_line(state);
            return true;
        }
    }

    if (token->kind == VM_LEXER_TOKEN_DOT && next != NULL && next->kind == VM_LEXER_TOKEN_NUMBER &&
        !next->number_is_negative && next->number_value == 387U && vm_parser_tokens_are_contiguous(token, next)) {
        (void)vm_parser_add_diagnostic(
            state,
            VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE,
            token,
            "Unsupported feature: .387 floating-point processor compatibility is not modeled in MASM32 Educational Mode."
        );
        vm_parser_recover_skip_line(state);
        return true;
    }

    if ((token->kind == VM_LEXER_TOKEN_IDENTIFIER && (vm_parser_token_equals(token, "struct") || vm_parser_token_equals(token, "union"))) ||
        (next != NULL && next->kind == VM_LEXER_TOKEN_IDENTIFIER && (vm_parser_token_equals(next, "struct") || vm_parser_token_equals(next, "union")))) {
        diagnostic_token = (token->kind == VM_LEXER_TOKEN_IDENTIFIER && (vm_parser_token_equals(token, "struct") || vm_parser_token_equals(token, "union"))) ? token : next;
        message = vm_parser_unsupported_keyword_message(diagnostic_token);
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, diagnostic_token, message);
        vm_parser_recover_skip_block(state, "ends", NULL, NULL);
        return true;
    }

    if ((token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "macro")) ||
        (next != NULL && next->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(next, "macro"))) {
        diagnostic_token = (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "macro")) ? token : next;
        message = vm_parser_unsupported_keyword_message(diagnostic_token);
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, diagnostic_token, message);
        vm_parser_recover_skip_block(state, "endm", NULL, NULL);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        message = vm_parser_unsupported_keyword_message(token);
        diagnostic_token = token;
    }

    if (message == NULL && next != NULL && next->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        message = vm_parser_unsupported_keyword_message(next);
        diagnostic_token = next;
    }

    if (message == NULL && state->section == VM_PARSER_SECTION_DATA && vm_parser_token_can_name_data_symbol(token) &&
        next != NULL && next->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        message = vm_parser_unsupported_data_type_message(next);
        diagnostic_token = next;
    }

    if (message == NULL) {
        return false;
    }

    (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, diagnostic_token, message);
    vm_parser_recover_skip_line(state);
    return true;
}

/// Maps one lexer diagnostic code to the parser diagnostic namespace.
///
/// @param code Lexer diagnostic code produced before parsing.
/// @return Parser diagnostic code that preserves the lexer-specific reason.
static VmParserDiagnosticCode vm_parser_diagnostic_code_from_lexer(VmLexerDiagnosticCode code) {
    switch (code) {
        case VM_LEXER_DIAGNOSTIC_INVALID_ARGUMENT:
            return VM_PARSER_DIAGNOSTIC_LEXER_INVALID_ARGUMENT;
        case VM_LEXER_DIAGNOSTIC_TOKEN_CAPACITY_EXCEEDED:
            return VM_PARSER_DIAGNOSTIC_LEXER_TOKEN_CAPACITY_EXCEEDED;
        case VM_LEXER_DIAGNOSTIC_DIAGNOSTIC_CAPACITY_EXCEEDED:
            return VM_PARSER_DIAGNOSTIC_LEXER_DIAGNOSTIC_CAPACITY_EXCEEDED;
        case VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER:
            return VM_PARSER_DIAGNOSTIC_LEXER_UNEXPECTED_CHARACTER;
        case VM_LEXER_DIAGNOSTIC_UNTERMINATED_STRING:
            return VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_STRING;
        case VM_LEXER_DIAGNOSTIC_UNTERMINATED_CHARACTER:
            return VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_CHARACTER;
        case VM_LEXER_DIAGNOSTIC_NUMBER_OVERFLOW:
            return VM_PARSER_DIAGNOSTIC_LEXER_NUMBER_OVERFLOW;
        case VM_LEXER_DIAGNOSTIC_INVALID_HEX_LITERAL:
            return VM_PARSER_DIAGNOSTIC_LEXER_INVALID_HEX_LITERAL;
        case VM_LEXER_DIAGNOSTIC_NONE:
        default:
            return VM_PARSER_DIAGNOSTIC_LEXER_FAILED;
    }
}

/// Maps a fatal lexer status to a parser diagnostic when no lexer diagnostic exists.
///
/// @param status Lexer status returned by tokenization.
/// @return Parser diagnostic code that preserves capacity and infrastructure failure kind.
static VmParserDiagnosticCode vm_parser_diagnostic_code_from_lexer_status(VmLexerStatus status) {
    switch (status) {
        case VM_LEXER_STATUS_INVALID_ARGUMENT:
            return VM_PARSER_DIAGNOSTIC_LEXER_INVALID_ARGUMENT;
        case VM_LEXER_STATUS_TOKEN_CAPACITY_EXCEEDED:
            return VM_PARSER_DIAGNOSTIC_LEXER_TOKEN_CAPACITY_EXCEEDED;
        case VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED:
            return VM_PARSER_DIAGNOSTIC_LEXER_DIAGNOSTIC_CAPACITY_EXCEEDED;
        case VM_LEXER_STATUS_OK:
        case VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS:
        default:
            return VM_PARSER_DIAGNOSTIC_LEXER_FAILED;
    }
}

/// Returns a user-facing message for one fatal lexer status.
///
/// @param status Lexer status returned by tokenization.
/// @return Static diagnostic message explaining the fatal lexer status.
static const char *vm_parser_message_from_lexer_status(VmLexerStatus status) {
    switch (status) {
        case VM_LEXER_STATUS_INVALID_ARGUMENT:
            return "Lexer received invalid input or output buffers.";
        case VM_LEXER_STATUS_TOKEN_CAPACITY_EXCEEDED:
            return "Lexer token capacity exhausted before parsing could proceed.";
        case VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED:
            return "Lexer diagnostic capacity exhausted before all diagnostics could be preserved.";
        case VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS:
            return "Lexer produced diagnostics before parsing could proceed.";
        case VM_LEXER_STATUS_OK:
        default:
            return "Lexer failed before parsing could proceed.";
    }
}

/// Copies one lexer diagnostic into the parser diagnostic stream.
///
/// @param state Parser state whose diagnostic buffer should receive the entry.
/// @param source_diagnostic Lexer diagnostic to preserve.
/// @return true when the diagnostic was recorded.
static bool vm_parser_add_lexer_diagnostic(
    VmParserState *state,
    const VmLexerDiagnostic *source_diagnostic
) {
    VmParserDiagnostic *diagnostic = NULL;

    if (state == NULL || state->config == NULL || state->result == NULL || source_diagnostic == NULL) {
        return false;
    }

    if (state->result->diagnostic_count >= state->config->diagnostic_capacity || state->config->diagnostics == NULL) {
        state->diagnostic_overflowed = true;
        return false;
    }

    diagnostic = &state->config->diagnostics[state->result->diagnostic_count];
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->code = vm_parser_diagnostic_code_from_lexer(source_diagnostic->code);
    diagnostic->severity = VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR;
    diagnostic->location = source_diagnostic->location;
    diagnostic->lexeme = source_diagnostic->lexeme;
    diagnostic->lexeme_length = source_diagnostic->lexeme_length;
    diagnostic->message = source_diagnostic->message;
    state->result->diagnostic_count += 1U;
    return true;
}

/// Adds a parser diagnostic for a lexer failure that produced no diagnostic entry.
///
/// @param state Parser state whose diagnostic buffer should receive the entry.
/// @param status Lexer status returned by tokenization.
/// @return true when the status diagnostic was recorded.
static bool vm_parser_add_lexer_status_diagnostic(
    VmParserState *state,
    VmLexerStatus status
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
    diagnostic->code = vm_parser_diagnostic_code_from_lexer_status(status);
    diagnostic->severity = VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR;
    diagnostic->message = vm_parser_message_from_lexer_status(status);
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

/// Returns the active data image buffer for the current declaration section.
///
/// @param state Parser state to inspect.
/// @param out_image Receives the active image pointer.
/// @param out_capacity Receives active image capacity in bytes.
/// @param out_offset Receives the active image write offset pointer.
/// @param out_size Receives the active parser result size pointer.
/// @return true when an active data-like section is selected.
static bool vm_parser_get_active_data_image(
    VmParserState *state,
    uint8_t **out_image,
    size_t *out_capacity,
    uint32_t **out_offset,
    size_t **out_size
) {
    if (state == NULL || state->config == NULL || state->result == NULL ||
        out_image == NULL || out_capacity == NULL || out_offset == NULL || out_size == NULL) {
        return false;
    }

    if (state->section == VM_PARSER_SECTION_CONST) {
        *out_image = state->config->const_image;
        *out_capacity = state->config->const_image_capacity;
        *out_offset = &state->const_offset;
        *out_size = &state->result->const_size;
        return true;
    }

    if (state->section == VM_PARSER_SECTION_DATA || state->section == VM_PARSER_SECTION_DATA_UNINITIALIZED) {
        *out_image = state->config->data_image;
        *out_capacity = state->config->data_image_capacity;
        *out_offset = &state->data_offset;
        *out_size = &state->result->data_size;
        return true;
    }

    return false;
}

/// Returns whether the active data image can receive another byte.
///
/// @param state Parser state to inspect.
/// @param token Token used for diagnostics on failure.
/// @return true when a byte can be appended.
static bool vm_parser_data_has_capacity(VmParserState *state, const VmLexerToken *token) {
    uint8_t *image = NULL;
    size_t capacity = 0U;
    uint32_t *offset = NULL;
    size_t *size = NULL;

    if (!vm_parser_get_active_data_image(state, &image, &capacity, &offset, &size) || image == NULL || offset == NULL || *offset >= capacity) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, token, "Data image capacity exceeded.");
        if (state != NULL) {
            state->stop_status = VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED;
        }
        return false;
    }

    return true;
}

/// Returns the active .data/.DATA? initialization mask, if one was configured.
///
/// @param state Parser state to inspect.
/// @param out_mask Receives the mask pointer, or NULL when tracking is disabled.
/// @param out_capacity Receives the mask capacity in bytes.
/// @return true when the active section can use the optional mask metadata.
static bool vm_parser_get_active_data_initialized_mask(VmParserState *state, uint8_t **out_mask, size_t *out_capacity) {
    if (out_mask != NULL) {
        *out_mask = NULL;
    }
    if (out_capacity != NULL) {
        *out_capacity = 0U;
    }
    if (state == NULL || state->config == NULL || out_mask == NULL || out_capacity == NULL) {
        return false;
    }
    if (state->section == VM_PARSER_SECTION_CONST || state->config->data_initialized_mask == NULL) {
        return true;
    }

    *out_mask = state->config->data_initialized_mask;
    *out_capacity = state->config->data_initialized_mask_capacity;
    return true;
}

/// Appends one byte to the active data image and optional initialization mask.
///
/// @param state Parser state to mutate.
/// @param value Byte value to append.
/// @param token Token used for diagnostics on failure.
/// @param is_initialized Whether this byte came from an explicit initializer.
/// @return true when the byte was appended.
static bool vm_parser_append_data_byte_with_initialization(VmParserState *state, uint8_t value, const VmLexerToken *token, bool is_initialized) {
    uint8_t *image = NULL;
    uint8_t *mask = NULL;
    size_t capacity = 0U;
    size_t mask_capacity = 0U;
    uint32_t *offset = NULL;
    size_t *size = NULL;

    if (!vm_parser_data_has_capacity(state, token) ||
        !vm_parser_get_active_data_image(state, &image, &capacity, &offset, &size) || image == NULL || offset == NULL || size == NULL ||
        !vm_parser_get_active_data_initialized_mask(state, &mask, &mask_capacity)) {
        (void)capacity;
        return false;
    }

    if (mask != NULL && (size_t)(*offset) >= mask_capacity) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, token, "Data initialization metadata capacity exceeded.");
        state->stop_status = VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED;
        return false;
    }

    image[*offset] = value;
    if (mask != NULL) {
        mask[*offset] = is_initialized ? 1U : 0U;
    }
    *offset += 1U;
    *size = (size_t)(*offset);
    return true;
}

/// Appends one initialized byte to the active data image.
///
/// @param state Parser state to mutate.
/// @param value Byte value to append.
/// @param token Token used for diagnostics on failure.
/// @return true when the byte was appended.
static bool vm_parser_append_data_byte(VmParserState *state, uint8_t value, const VmLexerToken *token) {
    return vm_parser_append_data_byte_with_initialization(state, value, token, true);
}

/// Appends a little-endian integer element to the .data image with explicit initialization state.
///
/// @param state Parser state to mutate.
/// @param value Value to encode.
/// @param size_bytes Number of little-endian bytes to write.
/// @param token Token used for diagnostics on failure.
/// @param is_initialized Whether all bytes came from an explicit initializer.
/// @return true when the integer was appended.
static bool vm_parser_append_data_integer_with_initialization(VmParserState *state, uint64_t value, uint8_t size_bytes, const VmLexerToken *token, bool is_initialized) {
    uint8_t index = 0U;

    for (index = 0U; index < size_bytes; index += 1U) {
        if (!vm_parser_append_data_byte_with_initialization(state, (uint8_t)((value >> (8U * index)) & 0xFFU), token, is_initialized)) {
            return false;
        }
    }

    return true;
}

/// Appends a little-endian initialized integer element to the .data image.
///
/// @param state Parser state to mutate.
/// @param value Value to encode.
/// @param size_bytes Number of little-endian bytes to write.
/// @param token Token used for diagnostics on failure.
/// @return true when the integer was appended.
static bool vm_parser_append_data_integer(VmParserState *state, uint64_t value, uint8_t size_bytes, const VmLexerToken *token) {
    return vm_parser_append_data_integer_with_initialization(state, value, size_bytes, token, true);
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

/// Returns the largest positive value accepted by a signed data declaration.
///
/// @param size_bytes Element size in bytes.
/// @return Maximum signed positive value for that size, or zero for invalid sizes.
static uint64_t vm_parser_signed_positive_max_for_size(uint8_t size_bytes) {
    switch (size_bytes) {
        case 1U:
            return 0x7FULL;
        case 2U:
            return 0x7FFFULL;
        case 4U:
            return 0x7FFFFFFFULL;
        case 8U:
            return 0x7FFFFFFFFFFFFFFFULL;
        default:
            return 0ULL;
    }
}

/// Returns the magnitude of a negative signed 64-bit expression value.
///
/// @param value Negative expression value to inspect.
/// @return Absolute magnitude as an unsigned value.
static uint64_t vm_parser_negative_i64_magnitude(int64_t value) {
    return value == INT64_MIN ? 0x8000000000000000ULL : (uint64_t)(-value);
}

/// Encodes a signed 64-bit expression value for a data element size.
///
/// Positive values may use the full unsigned width. Negative values must fit
/// the signed negative range for the destination size and are encoded in
/// two's-complement form.
///
/// @param value Constant-expression value to encode.
/// @param size_bytes Destination element size in bytes.
/// @param out_value Receives the encoded storage value.
/// @return true when the value fits the destination size.
static bool vm_parser_encode_i64_for_size(int64_t value, uint8_t size_bytes, uint64_t *out_value) {
    if (out_value == NULL || size_bytes == 0U || size_bytes > 8U) {
        return false;
    }

    if (value < 0) {
        uint64_t magnitude = vm_parser_negative_i64_magnitude(value);
        if (magnitude > vm_parser_max_negative_magnitude_for_size(size_bytes)) {
            return false;
        }
        *out_value = 0ULL - magnitude;
        return true;
    }

    if ((uint64_t)value > vm_parser_max_value_for_size(size_bytes)) {
        return false;
    }
    *out_value = (uint64_t)value;
    return true;
}

/// Encodes a signed 64-bit expression value for a declared data type.
///
/// Signed declarations validate both positive and negative values against the
/// signed range. Unsigned declarations preserve existing two's-complement
/// negative-literal behavior after width validation.
///
/// @param value Constant-expression value to encode.
/// @param data_type Declared data type controlling validation.
/// @param out_value Receives the encoded storage value.
/// @return true when the expression fits the declaration type.
static bool vm_parser_encode_i64_for_data_type(int64_t value, VmSymbolDataType data_type, uint64_t *out_value) {
    uint8_t size_bytes = vm_symbol_data_type_size_bytes(data_type);

    if (out_value == NULL || size_bytes == 0U || size_bytes > 8U) {
        return false;
    }

    if (!vm_symbol_data_type_is_signed(data_type)) {
        return vm_parser_encode_i64_for_size(value, size_bytes, out_value);
    }

    if (value < 0) {
        uint64_t magnitude = vm_parser_negative_i64_magnitude(value);
        if (magnitude > vm_parser_max_negative_magnitude_for_size(size_bytes)) {
            return false;
        }
        *out_value = 0ULL - magnitude;
        return true;
    }

    if ((uint64_t)value > vm_parser_signed_positive_max_for_size(size_bytes)) {
        return false;
    }
    *out_value = (uint64_t)value;
    return true;
}

/// Encodes a signed 64-bit expression as a 32-bit immediate value.
///
/// @param value Constant-expression value to encode.
/// @param out_value Receives the encoded immediate value.
/// @return true when the value is representable in the current 32-bit IR.
static bool vm_parser_encode_i64_for_u32_immediate(int64_t value, uint32_t *out_value) {
    if (out_value == NULL) {
        return false;
    }

    if (value < 0) {
        uint64_t magnitude = vm_parser_negative_i64_magnitude(value);
        if (magnitude > 0x80000000ULL) {
            return false;
        }
        *out_value = 0U - (uint32_t)magnitude;
        return true;
    }

    if ((uint64_t)value > 0xFFFFFFFFULL) {
        return false;
    }

    *out_value = (uint32_t)value;
    return true;
}

/// Converts a signed 64-bit expression value to a signed 32-bit byte offset.
///
/// @param value Expression value to convert.
/// @param out_offset Receives the signed 32-bit offset.
/// @return true when @p value fits int32_t.
static bool vm_parser_i64_to_i32_offset(int64_t value, int32_t *out_offset) {
    if (out_offset == NULL || value < (int64_t)INT32_MIN || value > (int64_t)INT32_MAX) {
        return false;
    }
    *out_offset = (int32_t)value;
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
        case '\'':
            return (uint8_t)'\'';
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

/// Returns whether a character-literal escape form is supported.
///
/// @param ch Source byte after a backslash.
/// @return true when the escape has explicit parser semantics.
static bool vm_parser_is_supported_character_escape(char ch) {
    return ch == 'n' || ch == 'r' || ch == 't' || ch == '\'' || ch == '\\';
}

/// Decodes bytes from a single-quoted character literal token.
///
/// Character literals may contain one or more decoded bytes. The parser keeps
/// escape handling explicit so unsupported escape forms become structured
/// diagnostics instead of silently changing program meaning.
///
/// @param token Character token including surrounding single quotes.
/// @param out_bytes Receives decoded bytes.
/// @param byte_capacity Capacity of @p out_bytes in bytes.
/// @param out_byte_count Receives the number of decoded bytes.
/// @param out_unsupported_escape Receives true when an unknown escape was seen.
/// @return true when at least one byte was decoded and the token fits capacity.
static bool vm_parser_decode_character_literal_bytes(
    const VmLexerToken *token,
    uint8_t *out_bytes,
    uint32_t byte_capacity,
    uint32_t *out_byte_count,
    bool *out_unsupported_escape
) {
    size_t index = 1U;
    size_t end = 0U;
    uint32_t decoded_count = 0U;

    if (out_byte_count != NULL) {
        *out_byte_count = 0U;
    }
    if (out_unsupported_escape != NULL) {
        *out_unsupported_escape = false;
    }

    if (token == NULL || out_bytes == NULL || out_byte_count == NULL || out_unsupported_escape == NULL ||
        token->kind != VM_LEXER_TOKEN_CHARACTER || token->lexeme_length < 2U) {
        return false;
    }

    end = token->lexeme_length - 1U;
    while (index < end) {
        uint8_t decoded = 0U;
        if (token->lexeme[index] == '\\') {
            if (index + 1U >= end || !vm_parser_is_supported_character_escape(token->lexeme[index + 1U])) {
                *out_unsupported_escape = true;
                return false;
            }
            index += 1U;
            decoded = vm_parser_decode_escaped_byte(token->lexeme[index]);
        } else {
            decoded = (uint8_t)token->lexeme[index];
        }

        if (decoded_count >= byte_capacity) {
            *out_byte_count = decoded_count;
            return false;
        }
        out_bytes[decoded_count] = decoded;
        decoded_count += 1U;
        index += 1U;
    }

    if (decoded_count == 0U) {
        return false;
    }

    *out_byte_count = decoded_count;
    return true;
}

/// Packs decoded character-literal bytes as a little-endian integer.
///
/// The first decoded byte becomes the least significant byte, matching the
/// Phase 14 packed immediate rule for literals such as 'AB' and 'ABCD'.
///
/// @param bytes Decoded character-literal bytes.
/// @param byte_count Number of decoded bytes to pack.
/// @param out_value Receives the packed integer.
/// @return true when @p byte_count is between one and eight bytes.
static bool vm_parser_pack_character_bytes(const uint8_t *bytes, uint32_t byte_count, uint64_t *out_value) {
    uint32_t index = 0U;
    uint64_t value = 0U;

    if (bytes == NULL || out_value == NULL || byte_count == 0U || byte_count > 8U) {
        return false;
    }

    for (index = 0U; index < byte_count; index += 1U) {
        value |= ((uint64_t)bytes[index]) << (index * 8U);
    }

    *out_value = value;
    return true;
}

/// Adds a structured diagnostic for a rejected character literal.
///
/// @param state Parser state to mutate.
/// @param token Character literal token associated with the error.
/// @param unsupported_escape Whether the rejected literal contained an unknown escape.
/// @param too_wide Whether the literal was too wide for its destination context.
static void vm_parser_add_character_literal_diagnostic(
    VmParserState *state,
    const VmLexerToken *token,
    bool unsupported_escape,
    bool too_wide
) {
    if (unsupported_escape) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, token, "Unsupported escape sequence in character literal.");
    } else if (too_wide) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, token, "Character literal does not fit the destination width.");
    } else {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, token, "Character literal must contain at least one decoded byte.");
    }
}

/// Parses one non-DUP initializer and appends its bytes.
///
/// @param state Parser state to mutate.
/// @param data_type Declared data type.
/// @param out_element_count Receives number of declared elements appended.
/// @param out_has_uninitialized Receives true when `?` was parsed.
/// @param out_is_fully_uninitialized Receives true when the initializer emits only `?` storage.
/// @return true when the initializer was parsed.
static bool vm_parser_parse_single_data_initializer(
    VmParserState *state,
    VmSymbolDataType data_type,
    uint32_t *out_element_count,
    bool *out_has_uninitialized,
    bool *out_is_fully_uninitialized
) {
    const VmLexerToken *token = vm_parser_current_token(state);
    uint8_t element_size = vm_symbol_data_type_size_bytes(data_type);

    if (state == NULL || out_element_count == NULL || out_has_uninitialized == NULL || out_is_fully_uninitialized == NULL || element_size == 0U) {
        return false;
    }

    *out_element_count = 0U;
    *out_has_uninitialized = false;
    *out_is_fully_uninitialized = false;

    if (token == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, token, "Expected .data initializer.");
        return false;
    }

    if (vm_parser_token_starts_constant_expression(token)) {
        VmParserConstantExpression expression;
        uint64_t encoded_value = 0U;
        memset(&expression, 0, sizeof(expression));
        if (!vm_parser_parse_constant_expression(state, &expression)) {
            return false;
        }
        if (!vm_parser_encode_i64_for_data_type(expression.value, data_type, &encoded_value)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, expression.start_token, vm_symbol_data_type_is_signed(data_type) ? "Data initializer exceeds the signed range for the declared type." : "Data initializer exceeds the declared element width.");
            return false;
        }
        if (!vm_parser_append_data_integer(state, encoded_value, element_size, expression.start_token)) {
            return false;
        }
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

    if (token->kind == VM_LEXER_TOKEN_CHARACTER) {
        uint8_t bytes[8];
        uint32_t byte_count = 0U;
        bool unsupported_escape = false;
        uint64_t packed_value = 0U;
        uint32_t byte_index = 0U;

        if (!vm_parser_decode_character_literal_bytes(token, bytes, 8U, &byte_count, &unsupported_escape)) {
            vm_parser_add_character_literal_diagnostic(state, token, unsupported_escape, !unsupported_escape && byte_count >= 8U);
            return false;
        }

        if (data_type == VM_SYMBOL_DATA_TYPE_BYTE || data_type == VM_SYMBOL_DATA_TYPE_SBYTE) {
            for (byte_index = 0U; byte_index < byte_count; byte_index += 1U) {
                if (!vm_parser_append_data_byte(state, bytes[byte_index], token)) {
                    return false;
                }
            }
            vm_parser_advance(state);
            *out_element_count = byte_count;
            return true;
        }

        if (byte_count > (uint32_t)element_size || !vm_parser_pack_character_bytes(bytes, byte_count, &packed_value)) {
            vm_parser_add_character_literal_diagnostic(state, token, false, true);
            return false;
        }
        if (!vm_parser_append_data_integer(state, packed_value, element_size, token)) {
            return false;
        }
        vm_parser_advance(state);
        *out_element_count = 1U;
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_QUESTION) {
        if (!vm_parser_append_data_integer_with_initialization(state, 0U, element_size, token, false)) {
            return false;
        }
        vm_parser_advance(state);
        *out_element_count = 1U;
        *out_has_uninitialized = true;
        *out_is_fully_uninitialized = true;
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, token, "Expected a number, string, character literal, DUP, or ? initializer.");
    return false;
}

/// Parses a COUNT DUP(initializer) data initializer.
///
/// @param state Parser state to mutate.
/// @param data_type Declared data type.
/// @param repeat_token Count token at the start of the DUP form.
/// @param out_element_count Receives total declared element count appended.
/// @param out_has_uninitialized Receives true when the inner initializer used `?`.
/// @param out_is_fully_uninitialized Receives true when the DUP emits only `?` storage.
/// @return true when the DUP initializer was parsed and expanded.
static bool vm_parser_parse_dup_initializer(
    VmParserState *state,
    VmSymbolDataType data_type,
    const VmLexerToken *repeat_token,
    uint64_t repeat_count,
    uint32_t *out_element_count,
    bool *out_has_uninitialized,
    bool *out_is_fully_uninitialized
) {
    size_t instance_start = 0U;
    size_t instance_size = 0U;
    uint8_t *active_image = NULL;
    size_t active_capacity = 0U;
    uint32_t *active_offset = NULL;
    size_t *active_size = NULL;
    uint32_t instance_elements = 0U;
    bool instance_uninitialized = false;
    bool instance_fully_uninitialized = true;
    const VmLexerToken *token = NULL;
    uint64_t repeat_index = 0U;

    if (state == NULL || repeat_token == NULL || out_element_count == NULL || out_has_uninitialized == NULL || out_is_fully_uninitialized == NULL) {
        return false;
    }

    *out_has_uninitialized = false;
    *out_is_fully_uninitialized = false;

    if (!vm_parser_get_active_data_image(state, &active_image, &active_capacity, &active_offset, &active_size) || active_image == NULL || active_offset == NULL) {
        (void)active_capacity;
        (void)active_size;
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, repeat_token, "Data image capacity exceeded.");
        state->stop_status = VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED;
        return false;
    }

    if (repeat_count == 0U || repeat_count > UINT32_MAX) {
        char capacity_text[48];
        vm_parser_format_u64_with_commas((uint64_t)active_capacity, capacity_text, sizeof(capacity_text));
        vm_parser_add_formatted_diagnostic(
            state,
            VM_PARSER_DIAGNOSTIC_INVALID_DUP,
            repeat_token,
            "DUP repeat count must be 1 or greater. The active declaration image capacity is %s bytes; usable repeat counts also depend on initializer size and remaining capacity.",
            capacity_text
        );
        return false;
    }

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

    instance_start = (size_t)(*active_offset);
    while (true) {
        uint32_t initializer_elements = 0U;
        bool initializer_uninitialized = false;
        bool initializer_fully_uninitialized = false;
        const VmLexerToken *separator = NULL;

        if (!vm_parser_parse_data_initializer(state, data_type, &initializer_elements, &initializer_uninitialized, &initializer_fully_uninitialized)) {
            return false;
        }
        if (initializer_elements > UINT32_MAX - instance_elements) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, repeat_token, "DUP initializer expands to too many elements.");
            state->stop_status = VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED;
            return false;
        }
        instance_elements += initializer_elements;
        instance_uninitialized = instance_uninitialized || initializer_uninitialized;
        instance_fully_uninitialized = instance_fully_uninitialized && initializer_fully_uninitialized;

        separator = vm_parser_current_token(state);
        if (separator != NULL && separator->kind == VM_LEXER_TOKEN_COMMA) {
            vm_parser_advance(state);
            continue;
        }
        break;
    }
    instance_size = (size_t)(*active_offset) - instance_start;

    if (instance_elements == 0U || instance_size == 0U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_DUP, repeat_token, "DUP initializer must expand to at least one element.");
        return false;
    }
    if (repeat_count > UINT32_MAX / (uint64_t)instance_elements) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, repeat_token, "DUP expansion exceeds the maximum representable element count.");
        state->stop_status = VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED;
        return false;
    }
    if (repeat_count > 1U) {
        uint64_t remaining_bytes = (repeat_count - 1U) * (uint64_t)instance_size;
        if (remaining_bytes > (uint64_t)(active_capacity - (size_t)(*active_offset))) {
            char required_text[48];
            char available_text[48];
            char capacity_text[48];
            uint64_t required_bytes = repeat_count * (uint64_t)instance_size;
            uint64_t available_from_start = (uint64_t)(active_capacity - instance_start);
            vm_parser_format_u64_with_commas(required_bytes, required_text, sizeof(required_text));
            vm_parser_format_u64_with_commas(available_from_start, available_text, sizeof(available_text));
            vm_parser_format_u64_with_commas((uint64_t)active_capacity, capacity_text, sizeof(capacity_text));
            vm_parser_add_formatted_diagnostic(
                state,
                VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED,
                repeat_token,
                "DUP expansion requires %s bytes, but only %s bytes are available from this declaration position in the active declaration image (%s total bytes configured).",
                required_text,
                available_text,
                capacity_text
            );
            state->stop_status = VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED;
            return false;
        }
    }

    token = vm_parser_current_token(state);
    if (token == NULL || token->kind != VM_LEXER_TOKEN_RIGHT_PAREN) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_DUP, token, "Expected ')' after DUP initializer.");
        return false;
    }
    vm_parser_advance(state);

    for (repeat_index = 1U; repeat_index < repeat_count; repeat_index += 1U) {
        size_t byte_index = 0U;
        for (byte_index = 0U; byte_index < instance_size; byte_index += 1U) {
            uint8_t *active_mask = NULL;
            size_t active_mask_capacity = 0U;
            bool copied_byte_is_initialized = true;
            (void)vm_parser_get_active_data_initialized_mask(state, &active_mask, &active_mask_capacity);
            if (active_mask != NULL && instance_start + byte_index < active_mask_capacity) {
                copied_byte_is_initialized = active_mask[instance_start + byte_index] != 0U;
            }
            if (!vm_parser_append_data_byte_with_initialization(state, active_image[instance_start + byte_index], repeat_token, copied_byte_is_initialized)) {
                return false;
            }
        }
    }

    *out_element_count = (uint32_t)(repeat_count * (uint64_t)instance_elements);
    *out_has_uninitialized = instance_uninitialized;
    *out_is_fully_uninitialized = instance_fully_uninitialized;
    return true;
}

/// Parses one .data initializer, including nested DUP forms.
///
/// @param state Parser state to mutate.
/// @param data_type Declared data type.
/// @param out_element_count Receives number of declared elements appended.
/// @param out_has_uninitialized Receives true when `?` was parsed.
/// @param out_is_fully_uninitialized Receives true when the initializer emits only `?` storage.
/// @return true when the initializer was parsed.
static bool vm_parser_parse_data_initializer(
    VmParserState *state,
    VmSymbolDataType data_type,
    uint32_t *out_element_count,
    bool *out_has_uninitialized,
    bool *out_is_fully_uninitialized
) {
    const VmLexerToken *token = vm_parser_current_token(state);
    const VmLexerToken *next = vm_parser_peek_token(state, 1U);

    if (token != NULL && vm_parser_token_starts_constant_expression(token)) {
        VmParserConstantExpression expression;
        uint64_t repeat_count = 0U;
        memset(&expression, 0, sizeof(expression));
        if (!vm_parser_parse_constant_expression(state, &expression)) {
            return false;
        }
        if (vm_parser_current_token(state) != NULL && vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_IDENTIFIER &&
            vm_parser_token_equals(vm_parser_current_token(state), "DUP")) {
            if (expression.value <= 0 || (uint64_t)expression.value > UINT32_MAX) {
                uint8_t *active_image = NULL;
                size_t active_capacity = 0U;
                uint32_t *active_offset = NULL;
                size_t *active_size = NULL;
                char capacity_text[48];
                if (!vm_parser_get_active_data_image(state, &active_image, &active_capacity, &active_offset, &active_size)) {
                    active_capacity = state->config != NULL ? state->config->data_image_capacity : 0U;
                }
                (void)active_image;
                (void)active_offset;
                (void)active_size;
                vm_parser_format_u64_with_commas((uint64_t)active_capacity, capacity_text, sizeof(capacity_text));
                vm_parser_add_formatted_diagnostic(
                    state,
                    VM_PARSER_DIAGNOSTIC_INVALID_DUP,
                    expression.start_token,
                    "DUP repeat count must be 1 or greater. The active declaration image capacity is %s bytes; usable repeat counts also depend on initializer size and remaining capacity.",
                    capacity_text
                );
                return false;
            }
            repeat_count = (uint64_t)expression.value;
            return vm_parser_parse_dup_initializer(state, data_type, expression.start_token, repeat_count, out_element_count, out_has_uninitialized, out_is_fully_uninitialized);
        }

        /* Rewind is deliberately avoided: parse_single_data_initializer handles strings,
         * character literals, and ?, while this path has already appended no bytes. */
        {
            uint64_t encoded_value = 0U;
            uint8_t element_size = vm_symbol_data_type_size_bytes(data_type);
            if (!vm_parser_encode_i64_for_data_type(expression.value, data_type, &encoded_value)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, expression.start_token, vm_symbol_data_type_is_signed(data_type) ? "Data initializer exceeds the signed range for the declared type." : "Data initializer exceeds the declared element width.");
                return false;
            }
            if (!vm_parser_append_data_integer(state, encoded_value, element_size, expression.start_token)) {
                return false;
            }
            *out_element_count = 1U;
            *out_has_uninitialized = false;
            *out_is_fully_uninitialized = false;
            return true;
        }
    }

    (void)next;
    return vm_parser_parse_single_data_initializer(state, data_type, out_element_count, out_has_uninitialized, out_is_fully_uninitialized);
}

/// Returns the active symbol section for a data declaration.
///
/// @param state Parser state to inspect.
/// @return Symbol section matching the current parser section.
static VmSymbolSection vm_parser_active_symbol_section(const VmParserState *state) {
    if (state != NULL && state->section == VM_PARSER_SECTION_CONST) {
        return VM_SYMBOL_SECTION_CONST;
    }
    if (state != NULL && state->section == VM_PARSER_SECTION_DATA_UNINITIALIZED) {
        return VM_SYMBOL_SECTION_DATA_UNINITIALIZED;
    }
    return VM_SYMBOL_SECTION_DATA;
}

/// Returns the simulated base address for a symbol section.
///
/// @param section Symbol section to inspect.
/// @return Simulated region base address for the section.
static uint32_t vm_parser_symbol_section_base(VmSymbolSection section) {
    return section == VM_SYMBOL_SECTION_CONST ? VM_MEMORY_DEFAULT_CONST_BASE : VM_MEMORY_DEFAULT_DATA_BASE;
}

/// Returns the IR relocation marker associated with a parser symbol section.
///
/// @param section Symbol section to inspect.
/// @return Relocation marker for address-valued operands derived from that section.
static VmIrRelocationKind vm_parser_symbol_relocation_kind(VmSymbolSection section) {
    return section == VM_SYMBOL_SECTION_CONST ? VM_IR_RELOCATION_CONST : VM_IR_RELOCATION_DATA;
}

/// Returns the number of bytes emitted for a symbol section.
///
/// @param state Parser state to inspect.
/// @param section Symbol section to inspect.
/// @return Current section image size in bytes.
static size_t vm_parser_symbol_section_size(const VmParserState *state, VmSymbolSection section) {
    if (state == NULL || state->result == NULL) {
        return 0U;
    }

    return section == VM_SYMBOL_SECTION_CONST ? state->result->const_size : state->result->data_size;
}

/// Returns the active data-image write offset for declarations.
///
/// @param state Parser state to inspect.
/// @return Current active section offset in bytes.
static uint32_t vm_parser_active_data_offset(const VmParserState *state) {
    if (state != NULL && state->section == VM_PARSER_SECTION_CONST) {
        return state->const_offset;
    }
    return state != NULL ? state->data_offset : 0U;
}

/// Restores the active data-image write offset and result size after a failed declaration.
///
/// @param state Parser state to mutate.
/// @param offset Offset value to restore.
/// @param size Result section size to restore.
static void vm_parser_restore_active_data_offset(VmParserState *state, uint32_t offset, size_t size) {
    if (state == NULL || state->result == NULL) {
        return;
    }

    if (state->section == VM_PARSER_SECTION_CONST) {
        state->const_offset = offset;
        state->result->const_size = size;
    } else {
        state->data_offset = offset;
        state->result->data_size = size;
    }
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
    VmSymbolSection symbol_section = VM_SYMBOL_SECTION_DATA;
    uint32_t total_elements = 0U;
    bool has_uninitialized = false;
    bool all_initializers_uninitialized = true;
    uint32_t start_offset = 0U;
    size_t start_data_size = 0U;

    if (state == NULL || !vm_parser_token_can_name_data_symbol(name_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_DECLARATION, name_token, "Expected symbol name in .data declaration.");
        return false;
    }

    if (type_token == NULL || type_token->kind != VM_LEXER_TOKEN_IDENTIFIER || !vm_symbol_parse_data_type(type_token->lexeme, type_token->lexeme_length, &data_type)) {
        const char *unsupported_type_message = vm_parser_unsupported_data_type_message(type_token);
        if (unsupported_type_message != NULL) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, type_token, unsupported_type_message);
            return false;
        }
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_DATA_TYPE, type_token, "Expected BYTE, WORD, DWORD, QWORD, SBYTE, SWORD, SDWORD, SQWORD, DB, DW, DD, or DQ data type.");
        return false;
    }

    if (vm_parser_has_conflicting_data_symbol(state, name_token) ||
        vm_parser_find_equate(state, name_token) != NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DUPLICATE_SYMBOL, name_token, "Duplicate data symbol or numeric equate name.");
        vm_parser_recover_skip_line(state);
        return true;
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

    symbol_section = vm_parser_active_symbol_section(state);
    symbol.data_type = data_type;
    symbol.section = symbol_section;
    symbol.element_size_bytes = vm_symbol_data_type_size_bytes(data_type);
    symbol.address = vm_parser_symbol_section_base(symbol_section) + vm_parser_active_data_offset(state);
    symbol.source_location = name_token->location;
    symbol.source_span_length = name_token->lexeme_length;

    vm_parser_advance(state);
    vm_parser_advance(state);
    start_offset = vm_parser_active_data_offset(state);
    start_data_size = vm_parser_symbol_section_size(state, symbol_section);

    if (vm_parser_is_line_end_token(vm_parser_current_token(state))) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, type_token, "Expected at least one .data initializer.");
        return false;
    }

    while (true) {
        uint32_t initializer_elements = 0U;
        bool initializer_uninitialized = false;
        bool initializer_fully_uninitialized = false;
        const VmLexerToken *separator = NULL;

        if (!vm_parser_parse_data_initializer(state, data_type, &initializer_elements, &initializer_uninitialized, &initializer_fully_uninitialized)) {
            vm_parser_restore_active_data_offset(state, start_offset, start_data_size);
            return false;
        }
        total_elements += initializer_elements;
        has_uninitialized = has_uninitialized || initializer_uninitialized;
        all_initializers_uninitialized = all_initializers_uninitialized && initializer_fully_uninitialized;

        separator = vm_parser_current_token(state);
        if (separator != NULL && separator->kind == VM_LEXER_TOKEN_COMMA) {
            vm_parser_advance(state);
            continue;
        }
        break;
    }

    if (!vm_parser_is_line_end_token(vm_parser_current_token(state))) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_LINE_END, vm_parser_current_token(state), "Expected end of line after .data declaration.");
        vm_parser_restore_active_data_offset(state, start_offset, start_data_size);
        return false;
    }

    if (symbol_section == VM_SYMBOL_SECTION_CONST && has_uninitialized) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, name_token, ".CONST declarations require initialized values, not ?.");
        vm_parser_restore_active_data_offset(state, start_offset, start_data_size);
        return false;
    }

    if (symbol_section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED && !all_initializers_uninitialized) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER, name_token, ".DATA? declarations must use ? or DUP(?) uninitialized storage.");
        vm_parser_restore_active_data_offset(state, start_offset, start_data_size);
        return false;
    }

    if (symbol_section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED) {
        uint8_t *active_image = NULL;
        size_t active_capacity = 0U;
        uint32_t *active_offset = NULL;
        size_t *active_size = NULL;
        uint32_t zero_index = 0U;
        if (vm_parser_get_active_data_image(state, &active_image, &active_capacity, &active_offset, &active_size) && active_image != NULL && active_offset != NULL) {
            (void)active_capacity;
            (void)active_size;
            for (zero_index = start_offset; zero_index < *active_offset; zero_index += 1U) {
                active_image[zero_index] = 0U;
            }
        }
    }

    symbol.size_bytes = vm_parser_active_data_offset(state) - start_offset;
    symbol.element_count = total_elements;
    symbol.has_uninitialized_initializer = has_uninitialized;
    symbol.has_uninitialized_storage = symbol_section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED;
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
    if (vm_parser_token_equals(token, "movsx")) {
        *out_opcode = VM_IR_OPCODE_MOVSX;
        return true;
    }
    if (vm_parser_token_equals(token, "movzx")) {
        *out_opcode = VM_IR_OPCODE_MOVZX;
        return true;
    }
    if (vm_parser_token_equals(token, "cbw")) {
        *out_opcode = VM_IR_OPCODE_CBW;
        return true;
    }
    if (vm_parser_token_equals(token, "cwde")) {
        *out_opcode = VM_IR_OPCODE_CWDE;
        return true;
    }
    if (vm_parser_token_equals(token, "cwd")) {
        *out_opcode = VM_IR_OPCODE_CWD;
        return true;
    }
    if (vm_parser_token_equals(token, "cdq")) {
        *out_opcode = VM_IR_OPCODE_CDQ;
        return true;
    }
    if (vm_parser_token_equals(token, "xchg")) {
        *out_opcode = VM_IR_OPCODE_XCHG;
        return true;
    }
    if (vm_parser_token_equals(token, "neg")) {
        *out_opcode = VM_IR_OPCODE_NEG;
        return true;
    }
    if (vm_parser_token_equals(token, "nop")) {
        *out_opcode = VM_IR_OPCODE_NOP;
        return true;
    }
    if (vm_parser_token_equals(token, "adc")) {
        *out_opcode = VM_IR_OPCODE_ADC;
        return true;
    }
    if (vm_parser_token_equals(token, "sbb")) {
        *out_opcode = VM_IR_OPCODE_SBB;
        return true;
    }
    if (vm_parser_token_equals(token, "clc")) {
        *out_opcode = VM_IR_OPCODE_CLC;
        return true;
    }
    if (vm_parser_token_equals(token, "stc")) {
        *out_opcode = VM_IR_OPCODE_STC;
        return true;
    }
    if (vm_parser_token_equals(token, "cmc")) {
        *out_opcode = VM_IR_OPCODE_CMC;
        return true;
    }
    if (vm_parser_token_equals(token, "test")) {
        *out_opcode = VM_IR_OPCODE_TEST;
        return true;
    }
    if (vm_parser_token_equals(token, "inc")) {
        *out_opcode = VM_IR_OPCODE_INC;
        return true;
    }
    if (vm_parser_token_equals(token, "dec")) {
        *out_opcode = VM_IR_OPCODE_DEC;
        return true;
    }

    return false;
}

/// Returns whether an opcode uses no explicit source operands.
///
/// @param opcode Opcode to inspect.
/// @return true for accumulator conversion, NOP, and carry-control instructions.
static bool vm_parser_opcode_has_no_operands(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_CBW ||
           opcode == VM_IR_OPCODE_CWDE ||
           opcode == VM_IR_OPCODE_CWD ||
           opcode == VM_IR_OPCODE_CDQ ||
           opcode == VM_IR_OPCODE_NOP ||
           opcode == VM_IR_OPCODE_CLC ||
           opcode == VM_IR_OPCODE_STC ||
           opcode == VM_IR_OPCODE_CMC ||
           opcode == VM_IR_OPCODE_EXIT;
}

/// Returns whether an opcode uses sign/zero-extension width rules.
///
/// @param opcode Opcode to inspect.
/// @return true for MOVSX and MOVZX.
static bool vm_parser_opcode_is_extension_move(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_MOVSX || opcode == VM_IR_OPCODE_MOVZX;
}


/// Returns whether an opcode uses one mutable destination operand.
///
/// @param opcode Opcode to inspect.
/// @return true for single-operand destination instructions.
static bool vm_parser_opcode_is_single_destination_operand(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_NEG ||
           opcode == VM_IR_OPCODE_INC ||
           opcode == VM_IR_OPCODE_DEC;
}

/// Returns whether an opcode uses exchange operand validation rules.
///
/// @param opcode Opcode to inspect.
/// @return true for XCHG.
static bool vm_parser_opcode_is_exchange(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_XCHG;
}

/// Returns whether an opcode uses TEST operand validation rules.
///
/// @param opcode Opcode to inspect.
/// @return true for TEST.
static bool vm_parser_opcode_is_test(VmIrOpcode opcode) {
    return opcode == VM_IR_OPCODE_TEST;
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

/// Returns whether a numeric token was spelled with a leading unary plus.
///
/// The lexer stores unary-plus numeric literals as number tokens. Bracketed
/// offset parsers use this helper to preserve existing compact offset forms
/// such as `[esi+4]` and `[nums+8]` without accepting whitespace-only
/// expression forms such as `[nums 8]`.
///
/// @param token Token to inspect.
/// @return true when @p token is a non-negative number token whose lexeme starts with `+`.
static bool vm_parser_number_token_has_leading_plus(const VmLexerToken *token) {
    return token != NULL &&
           token->kind == VM_LEXER_TOKEN_NUMBER &&
           !token->number_is_negative &&
           token->lexeme != NULL &&
           token->lexeme_length > 0U &&
           token->lexeme[0] == '+';
}

/// Returns whether a token can carry a data symbol name.
///
/// The lexer classifies register-looking words such as `ch` as register tokens.
/// The character-literal acceptance program uses `ch` as a data symbol, so parser
/// symbol lookup accepts both identifier and register tokens as symbol names.
///
/// @param token Token to inspect.
/// @return true when the token can be used as a data symbol name.
static bool vm_parser_token_can_name_data_symbol(const VmLexerToken *token) {
    return token != NULL &&
           (token->kind == VM_LEXER_TOKEN_IDENTIFIER || token->kind == VM_LEXER_TOKEN_REGISTER);
}

/// Resolves a data symbol token using the active CASEMAP policy.
///
/// @param state Parser state to inspect.
/// @param token Symbol token to resolve.
/// @param message Diagnostic text for an unknown symbol.
/// @return Matching symbol, or NULL after recording a diagnostic.
static const VmSymbol *vm_parser_resolve_symbol_with_message(VmParserState *state, const VmLexerToken *token, const char *message) {
    const VmSymbol *symbol = NULL;
    VmSymbolLookupStatus lookup_status = VM_SYMBOL_LOOKUP_NOT_FOUND;

    if (state == NULL || !vm_parser_token_can_name_data_symbol(token)) {
        return NULL;
    }

    symbol = vm_symbol_find_by_name_with_policy(
        state->config->symbols,
        state->result->symbol_count,
        token->lexeme,
        token->lexeme_length,
        vm_parser_symbol_case_policy(state->user_symbol_case_policy),
        &lookup_status
    );
    if (lookup_status == VM_SYMBOL_LOOKUP_AMBIGUOUS) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL, token, "Multiple data symbols match this reference under CASEMAP:ALL because their names differ only by letter case. Use OPTION CASEMAP:NONE and the exact symbol spelling, or make the symbol names distinct beyond case.");
        return NULL;
    }
    if (symbol == NULL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, token, message != NULL ? message : "Unknown data symbol.");
    }

    return symbol;
}

/// Resolves a data symbol token using the active CASEMAP policy.
///
/// @param state Parser state to inspect.
/// @param token Symbol token to resolve.
/// @return Matching symbol, or NULL after recording a diagnostic.
static const VmSymbol *vm_parser_resolve_symbol(VmParserState *state, const VmLexerToken *token) {
    return vm_parser_resolve_symbol_with_message(state, token, "Unknown data symbol.");
}

/// Returns whether a symbol table already contains a declaration matching a token under active CASEMAP policy.
///
/// @param state Parser state to inspect.
/// @param token Declaration token to check.
/// @return true when an accepted data symbol conflicts with @p token.
static bool vm_parser_has_conflicting_data_symbol(VmParserState *state, const VmLexerToken *token) {
    VmSymbolLookupStatus lookup_status = VM_SYMBOL_LOOKUP_NOT_FOUND;

    if (state == NULL || token == NULL) {
        return false;
    }

    (void)vm_symbol_find_by_name_with_policy(
        state->config->symbols,
        state->result->symbol_count,
        token->lexeme,
        token->lexeme_length,
        vm_parser_symbol_case_policy(state->user_symbol_case_policy),
        &lookup_status
    );
    return lookup_status == VM_SYMBOL_LOOKUP_FOUND || lookup_status == VM_SYMBOL_LOOKUP_AMBIGUOUS;
}

/// Returns the active-policy lookup status for a candidate data symbol token.
///
/// @param state Parser state to inspect.
/// @param token Token to check.
/// @return Matching status for the current data-symbol table.
static VmSymbolLookupStatus vm_parser_data_symbol_lookup_status(VmParserState *state, const VmLexerToken *token) {
    VmSymbolLookupStatus lookup_status = VM_SYMBOL_LOOKUP_NOT_FOUND;

    if (state == NULL || token == NULL) {
        return VM_SYMBOL_LOOKUP_NOT_FOUND;
    }

    (void)vm_symbol_find_by_name_with_policy(
        state->config->symbols,
        state->result->symbol_count,
        token->lexeme,
        token->lexeme_length,
        vm_parser_symbol_case_policy(state->user_symbol_case_policy),
        &lookup_status
    );
    return lookup_status;
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
    uint32_t section_relative_offset = 0U;
    uint32_t section_base = 0U;
    uint32_t final_address = 0U;
    int64_t final_address_signed = 0;
    uint64_t access_end = 0U;
    uint64_t section_end = 0U;

    if (state == NULL || symbol_token == NULL || out_operand == NULL || !vm_parser_token_can_name_data_symbol(symbol_token)) {
        return false;
    }

    symbol = vm_parser_resolve_symbol(state, symbol_token);
    if (symbol == NULL) {
        return false;
    }

    if (explicit_width_bits == 64U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, width_token != NULL ? width_token : symbol_token, "QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.");
        return false;
    }

    if (explicit_width_bits == 0U && symbol->element_size_bytes > 4U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, symbol_token, "QWORD and SQWORD memory operands are deferred until Extended 32-bit Mode.");
        return false;
    }

    width_bytes = explicit_width_bits != 0U ? (uint32_t)(explicit_width_bits / 8U) : (uint32_t)symbol->element_size_bytes;
    if (width_bytes == 0U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token != NULL ? offset_token : symbol_token, "Symbol offset has an unsupported zero-width access.");
        return false;
    }

    section_base = vm_parser_symbol_section_base(symbol->section);
    final_address_signed = (int64_t)(uint64_t)symbol->address + (int64_t)offset;
    if (final_address_signed < (int64_t)(uint64_t)section_base || final_address_signed > (int64_t)UINT32_MAX) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token != NULL ? offset_token : symbol_token, "Symbol offset resolves outside the current data section image.");
        return false;
    }

    final_address = (uint32_t)final_address_signed;
    section_relative_offset = final_address - section_base;
    access_end = (uint64_t)section_relative_offset + (uint64_t)width_bytes;
    section_end = (uint64_t)vm_parser_symbol_section_size(state, symbol->section);
    if (access_end > section_end) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_token != NULL ? offset_token : symbol_token, "Symbol offset access extends beyond the current data section image.");
        return false;
    }

    width_bits = explicit_width_bits != 0U ? explicit_width_bits : (uint8_t)(symbol->element_size_bytes * 8U);
    if (!vm_ir_width_is_supported(width_bits)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, width_token != NULL ? width_token : symbol_token, "PTR width override is not executable in the current MASM32 subset.");
        return false;
    }

    *out_operand = vm_ir_operand_with_relocation(vm_ir_operand_memory(final_address, width_bits), vm_parser_symbol_relocation_kind(symbol->section));
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

/// Returns whether a register is allowed as a register-indirect memory base.
///
/// @param token Register token to inspect.
/// @return true for 32-bit general-purpose registers accepted as memory bases.
static bool vm_parser_token_is_register_indirect_base(const VmLexerToken *token) {
    if (token == NULL || token->kind != VM_LEXER_TOKEN_REGISTER) {
        return false;
    }

    return token->register_id == VM_REGISTER_EAX ||
           token->register_id == VM_REGISTER_EBX ||
           token->register_id == VM_REGISTER_ECX ||
           token->register_id == VM_REGISTER_EDX ||
           token->register_id == VM_REGISTER_ESI ||
           token->register_id == VM_REGISTER_EDI ||
           token->register_id == VM_REGISTER_EBP ||
           token->register_id == VM_REGISTER_ESP;
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
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, width_token != NULL ? width_token : base_token, "QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode.");
        return false;
    }

    if (symbol_token != NULL) {
        symbol = vm_parser_resolve_symbol(state, symbol_token);
        if (symbol == NULL) {
            return false;
        }
        if (width_bits == 0U) {
            if (symbol->element_size_bytes > 4U) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, symbol_token, "QWORD and SQWORD memory operands are deferred until Extended 32-bit Mode.");
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
    if (symbol != NULL) {
        *out_operand = vm_ir_operand_with_relocation(*out_operand, vm_parser_symbol_relocation_kind(symbol->section));
    }
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

    if (vm_parser_number_token_has_leading_plus(token)) {
        int32_t magnitude = 0;
        if (!vm_parser_number_to_i32_offset(token, &magnitude)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, token, "Register displacement is outside the supported signed 32-bit range.");
            return false;
        }
        *out_displacement = magnitude;
        vm_parser_advance(state);
        if (vm_parser_current_token(state) != NULL && vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_ASTERISK) {
            vm_parser_add_scaled_index_diagnostic(state, vm_parser_current_token(state));
            return false;
        }
        return true;
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
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE, base_token, "This register is not supported as a register-indirect memory base. Use a 32-bit general-purpose register such as EAX, EBX, ECX, EDX, ESI, EDI, EBP, or ESP for bracketed memory operands, or remove the brackets to use the register value directly.");
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

    if (state == NULL || out_operand == NULL || symbol_token == NULL || !vm_parser_token_can_name_data_symbol(symbol_token) || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
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
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, base_token, "Unsupported register index. Use a 32-bit general-purpose register such as EAX, EBX, ECX, EDX, ESI, EDI, EBP, or ESP.");
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
    if (symbol_token == NULL || !vm_parser_token_can_name_data_symbol(symbol_token) || plus_token == NULL || plus_token->kind != VM_LEXER_TOKEN_PLUS || base_token == NULL || base_token->kind != VM_LEXER_TOKEN_REGISTER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, symbol_token != NULL ? symbol_token : left_token, "Expected [symbol + register] memory operand.");
        return false;
    }
    if (!vm_parser_token_is_register_indirect_base(base_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, base_token, "Unsupported register index. Use a 32-bit general-purpose register such as EAX, EBX, ECX, EDX, ESI, EDI, EBP, or ESP.");
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

/// Parses an optional constant-expression offset after a bracketed symbol token.
///
/// Supported forms include empty offset-zero brackets, signed constant
/// expressions such as `+ 4`, `- 4`, compact negative number tokens such as
/// `[nums-4]`, and Milestone 29 compile-time expressions such as
/// `[nums + COUNT * 4]`.
///
/// @param state Parser state positioned after the symbol token.
/// @param out_offset Receives the signed offset.
/// @param out_offset_token Receives the token most relevant for diagnostics.
/// @return true when an offset suffix was parsed.
static bool vm_parser_parse_bracket_symbol_offset_suffix(VmParserState *state, int32_t *out_offset, const VmLexerToken **out_offset_token) {
    const VmLexerToken *token = vm_parser_current_token(state);
    VmParserConstantExpression expression;
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

    if (vm_parser_token_starts_constant_expression(token)) {
        memset(&expression, 0, sizeof(expression));
        if (!vm_parser_parse_constant_expression(state, &expression)) {
            return false;
        }
        if (!vm_parser_i64_to_i32_offset(expression.value, &offset)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, expression.start_token, "Symbol offset is outside the supported signed 32-bit range.");
            return false;
        }
        *out_offset = offset;
        *out_offset_token = expression.start_token;
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected a constant byte offset after bracketed data symbol.");
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
    if (symbol_token == NULL || !vm_parser_token_can_name_data_symbol(symbol_token)) {
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

    if (state == NULL || out_operand == NULL || symbol_token == NULL || !vm_parser_token_can_name_data_symbol(symbol_token) || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    offset_token = vm_parser_current_token(state);
    if (!vm_parser_token_starts_constant_expression(offset_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, offset_token != NULL ? offset_token : left_token, "Expected a constant byte offset inside symbol brackets.");
        return false;
    }
    {
        VmParserConstantExpression expression;
        memset(&expression, 0, sizeof(expression));
        if (!vm_parser_parse_constant_expression(state, &expression)) {
            return false;
        }
        if (!vm_parser_i64_to_i32_offset(expression.value, &offset)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, expression.start_token, "Symbol offset is outside the supported signed 32-bit range.");
            return false;
        }
        offset_token = expression.start_token;
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

/// Parses a MASM PTR width keyword.
///
/// @param token Token to inspect.
/// @param out_width_bits Receives 8, 16, 32, or 64 on success.
/// @return true when @p token is a supported signed or unsigned PTR width keyword.
static bool vm_parser_parse_ptr_width_keyword(const VmLexerToken *token, uint8_t *out_width_bits) {
    if (token == NULL || out_width_bits == NULL || token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return false;
    }

    if (vm_parser_token_equals(token, "BYTE") || vm_parser_token_equals(token, "SBYTE")) {
        *out_width_bits = 8U;
        return true;
    }
    if (vm_parser_token_equals(token, "WORD") || vm_parser_token_equals(token, "SWORD")) {
        *out_width_bits = 16U;
        return true;
    }
    if (vm_parser_token_equals(token, "DWORD") || vm_parser_token_equals(token, "SDWORD")) {
        *out_width_bits = 32U;
        return true;
    }
    if (vm_parser_token_equals(token, "QWORD") || vm_parser_token_equals(token, "SQWORD")) {
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
/// not one of BYTE/SBYTE, WORD/SWORD, DWORD/SDWORD, or QWORD/SQWORD.
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
/// @param state Parser state positioned at a signed or unsigned PTR width keyword.
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
    if (symbol_token == NULL || !vm_parser_token_can_name_data_symbol(symbol_token)) {
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

    if (state == NULL || width == NULL || out_operand == NULL || symbol_token == NULL || !vm_parser_token_can_name_data_symbol(symbol_token) || left_token == NULL || left_token->kind != VM_LEXER_TOKEN_LEFT_BRACKET) {
        return false;
    }

    vm_parser_advance(state);
    vm_parser_advance(state);
    offset_token = vm_parser_current_token(state);
    if (!vm_parser_token_starts_constant_expression(offset_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, offset_token != NULL ? offset_token : left_token, "Expected a constant byte offset inside symbol brackets.");
        return false;
    }
    {
        VmParserConstantExpression expression;
        memset(&expression, 0, sizeof(expression));
        if (!vm_parser_parse_constant_expression(state, &expression)) {
            return false;
        }
        if (!vm_parser_i64_to_i32_offset(expression.value, &offset)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, expression.start_token, "Symbol offset is outside the supported signed 32-bit range.");
            return false;
        }
        offset_token = expression.start_token;
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
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, token, "Unsupported PTR width override. Expected BYTE, SBYTE, WORD, SWORD, DWORD, SDWORD, QWORD, or SQWORD before PTR.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_REGISTER &&
        vm_parser_data_symbol_lookup_status(state, token) != VM_SYMBOL_LOOKUP_NOT_FOUND) {
        if (!vm_parser_parse_symbol_memory_operand(state, token, out_operand)) {
            return false;
        }
        vm_parser_advance(state);
        return true;
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

/// Parses a TYPE symbol expression as a 32-bit immediate element-size value.
///
/// The current milestone intentionally supports only the simple `TYPE symbol`
/// form in source-immediate contexts. Arithmetic expression
/// tails, bracketed operands, and other MASM expressions remain future phases.
///
/// @param state Parser state to mutate.
/// @param out_operand Receives the immediate operand containing the element size.
/// @return true when the TYPE expression was parsed successfully.
static bool vm_parser_parse_type_source_operand(VmParserState *state, VmIrOperand *out_operand) {
    const VmLexerToken *type_token = vm_parser_current_token(state);
    const VmLexerToken *symbol_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *tail_token = vm_parser_peek_token(state, 2U);
    const VmSymbol *symbol = NULL;

    if (state == NULL || out_operand == NULL || type_token == NULL) {
        return false;
    }

    if (symbol_token == NULL || vm_parser_is_line_end_token(symbol_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, type_token, "TYPE requires a following data symbol.");
        return false;
    }

    if (!vm_parser_token_can_name_data_symbol(symbol_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION, symbol_token, "Unsupported TYPE expression. Only TYPE symbol is supported by the current milestone.");
        return false;
    }

    if (tail_token != NULL && !vm_parser_is_line_end_token(tail_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION, tail_token, "Unsupported TYPE expression. Only TYPE symbol is supported by the current milestone.");
        return false;
    }

    symbol = vm_parser_resolve_symbol_with_message(state, symbol_token, "TYPE references an unknown data symbol.");
    if (symbol == NULL) {
        return false;
    }

    *out_operand = vm_ir_operand_immediate((uint32_t)symbol->element_size_bytes, 32U);
    vm_parser_advance(state);
    vm_parser_advance(state);
    return true;
}

/// Parses a LENGTHOF symbol expression as a 32-bit immediate element-count value.
///
/// The current milestone intentionally supports only the simple `LENGTHOF symbol`
/// form in source-immediate contexts. Arithmetic expression tails,
/// bracketed operands, and other MASM expressions remain future phases.
///
/// @param state Parser state to mutate.
/// @param out_operand Receives the immediate operand containing the element count.
/// @return true when the LENGTHOF expression was parsed successfully.
static bool vm_parser_parse_lengthof_source_operand(VmParserState *state, VmIrOperand *out_operand) {
    const VmLexerToken *lengthof_token = vm_parser_current_token(state);
    const VmLexerToken *symbol_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *tail_token = vm_parser_peek_token(state, 2U);
    const VmSymbol *symbol = NULL;

    if (state == NULL || out_operand == NULL || lengthof_token == NULL) {
        return false;
    }

    if (symbol_token == NULL || vm_parser_is_line_end_token(symbol_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, lengthof_token, "LENGTHOF requires a following data symbol.");
        return false;
    }

    if (!vm_parser_token_can_name_data_symbol(symbol_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION, symbol_token, "Unsupported LENGTHOF expression. Only LENGTHOF symbol is supported by the current milestone.");
        return false;
    }

    if (tail_token != NULL && !vm_parser_is_line_end_token(tail_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION, tail_token, "Unsupported LENGTHOF expression. Only LENGTHOF symbol is supported by the current milestone.");
        return false;
    }

    symbol = vm_parser_resolve_symbol_with_message(state, symbol_token, "LENGTHOF references an unknown data symbol.");
    if (symbol == NULL) {
        return false;
    }

    *out_operand = vm_ir_operand_immediate(symbol->element_count, 32U);
    vm_parser_advance(state);
    vm_parser_advance(state);
    return true;
}

/// Parses a SIZEOF symbol expression as a 32-bit immediate total-size value.
///
/// The current milestone intentionally supports only the simple `SIZEOF symbol`
/// form in source-immediate contexts. Arithmetic expression tails, bracketed
/// operands, and other MASM expressions remain future phases.
///
/// @param state Parser state to mutate.
/// @param out_operand Receives the immediate operand containing the total byte size.
/// @return true when the SIZEOF expression was parsed successfully.
static bool vm_parser_parse_sizeof_source_operand(VmParserState *state, VmIrOperand *out_operand) {
    const VmLexerToken *sizeof_token = vm_parser_current_token(state);
    const VmLexerToken *symbol_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *tail_token = vm_parser_peek_token(state, 2U);
    const VmSymbol *symbol = NULL;

    if (state == NULL || out_operand == NULL || sizeof_token == NULL) {
        return false;
    }

    if (symbol_token == NULL || vm_parser_is_line_end_token(symbol_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, sizeof_token, "SIZEOF requires a following data symbol.");
        return false;
    }

    if (!vm_parser_token_can_name_data_symbol(symbol_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION, symbol_token, "Unsupported SIZEOF expression. Only SIZEOF symbol is supported by the current milestone.");
        return false;
    }

    if (tail_token != NULL && !vm_parser_is_line_end_token(tail_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION, tail_token, "Unsupported SIZEOF expression. Only SIZEOF symbol is supported by the current milestone.");
        return false;
    }

    symbol = vm_parser_resolve_symbol_with_message(state, symbol_token, "SIZEOF references an unknown data symbol.");
    if (symbol == NULL) {
        return false;
    }

    *out_operand = vm_ir_operand_immediate(symbol->size_bytes, 32U);
    vm_parser_advance(state);
    vm_parser_advance(state);
    return true;
}

/// Parses a source operand that may be register, immediate, direct symbol, OFFSET symbol, TYPE symbol, LENGTHOF symbol, SIZEOF symbol, or character literal.
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
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, token, "Unsupported PTR width override. Expected BYTE, SBYTE, WORD, SWORD, DWORD, SDWORD, QWORD, or SQWORD before PTR.");
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "TYPE")) {
        return vm_parser_parse_type_source_operand(state, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "LENGTHOF")) {
        return vm_parser_parse_lengthof_source_operand(state, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "SIZEOF")) {
        return vm_parser_parse_sizeof_source_operand(state, out_operand);
    }

    if (token->kind == VM_LEXER_TOKEN_CHARACTER) {
        uint8_t bytes[4];
        uint32_t byte_count = 0U;
        bool unsupported_escape = false;
        uint64_t packed_value = 0U;

        if (!vm_parser_decode_character_literal_bytes(token, bytes, 4U, &byte_count, &unsupported_escape) ||
            !vm_parser_pack_character_bytes(bytes, byte_count, &packed_value)) {
            vm_parser_add_character_literal_diagnostic(state, token, unsupported_escape, !unsupported_escape && byte_count >= 4U);
            return false;
        }
        *out_operand = vm_ir_operand_immediate((uint32_t)packed_value, (uint8_t)(byte_count * 8U));
        vm_parser_advance(state);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_REGISTER &&
        vm_parser_data_symbol_lookup_status(state, token) != VM_SYMBOL_LOOKUP_NOT_FOUND) {
        if (!vm_parser_parse_symbol_memory_operand(state, token, out_operand)) {
            return false;
        }
        vm_parser_advance(state);
        return true;
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

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "OFFSET")) {
        const VmLexerToken *symbol_token = vm_parser_peek_token(state, 1U);
        const VmLexerToken *tail_token = vm_parser_peek_token(state, 2U);
        const VmSymbol *symbol = NULL;
        int64_t address_value = 0;
        uint32_t encoded_address = 0U;
        if (symbol_token == NULL || !vm_parser_token_can_name_data_symbol(symbol_token)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "OFFSET requires a following data symbol.");
            return false;
        }
        symbol = vm_symbol_find_by_name_with_policy(
            state->config->symbols,
            state->result->symbol_count,
            symbol_token->lexeme,
            symbol_token->lexeme_length,
            vm_parser_symbol_case_policy(state->user_symbol_case_policy),
            NULL
        );
        if (symbol == NULL) {
            VmSymbolLookupStatus offset_lookup_status = VM_SYMBOL_LOOKUP_NOT_FOUND;
            (void)vm_symbol_find_by_name_with_policy(
                state->config->symbols,
                state->result->symbol_count,
                symbol_token->lexeme,
                symbol_token->lexeme_length,
                vm_parser_symbol_case_policy(state->user_symbol_case_policy),
                &offset_lookup_status
            );
            if (offset_lookup_status == VM_SYMBOL_LOOKUP_AMBIGUOUS) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL, symbol_token, "Multiple data symbols match this reference under CASEMAP:ALL because their names differ only by letter case. Use OPTION CASEMAP:NONE and the exact symbol spelling, or make the symbol names distinct beyond case.");
            } else if (vm_parser_find_equate(state, symbol_token) != NULL) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, symbol_token, "OFFSET requires a data symbol, not a numeric equate.");
            } else {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, symbol_token, "OFFSET references an unknown data symbol.");
            }
            return false;
        }

        address_value = (int64_t)symbol->address;
        vm_parser_advance(state);
        vm_parser_advance(state);
        if (tail_token != NULL && !vm_parser_is_line_end_token(tail_token)) {
            VmParserConstantExpression offset_expression;
            memset(&offset_expression, 0, sizeof(offset_expression));
            if (!vm_parser_parse_constant_expression(state, &offset_expression)) {
                return false;
            }
            if (!vm_parser_add_i64_checked(address_value, offset_expression.value, &address_value)) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, offset_expression.start_token, "OFFSET expression is outside the supported 32-bit address range.");
                return false;
            }
        }
        if (address_value < 0 || address_value > (int64_t)UINT32_MAX) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, symbol_token, "OFFSET expression is outside the supported 32-bit address range.");
            return false;
        }
        encoded_address = (uint32_t)address_value;
        *out_operand = vm_ir_operand_with_relocation(vm_ir_operand_immediate(encoded_address, 32U), vm_parser_symbol_relocation_kind(symbol->section));
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_NUMBER || token->kind == VM_LEXER_TOKEN_PLUS || token->kind == VM_LEXER_TOKEN_MINUS ||
        token->kind == VM_LEXER_TOKEN_LEFT_PAREN ||
        (token->kind == VM_LEXER_TOKEN_IDENTIFIER &&
         (vm_parser_token_names_equate_expression(state, token) || vm_parser_token_is_constant_unary_operator(token)))) {
        VmParserConstantExpression expression;
        uint32_t encoded_value = 0U;
        memset(&expression, 0, sizeof(expression));
        if (!vm_parser_parse_constant_expression(state, &expression)) {
            return false;
        }
        if (!vm_parser_encode_i64_for_u32_immediate(expression.value, &encoded_value)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, expression.start_token, "Immediate constant expression exceeds the current 32-bit IR range.");
            return false;
        }
        *out_operand = vm_ir_operand_immediate(encoded_value, 0U);
        return true;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        if (!vm_parser_parse_symbol_memory_operand(state, token, out_operand)) {
            return false;
        }
        vm_parser_advance(state);
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, token, "Expected a register, immediate, data symbol, constant symbol-offset, register-indirect, OFFSET source operand, TYPE source operand, LENGTHOF source operand, SIZEOF source operand, or character literal.");
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

/// Returns whether an operand is a register-indirect memory operand with no known access width.
///
/// Direct symbols and symbol-relative operands already carry declaration
/// metadata when they are lowered. Pure register-indirect operands such as
/// `[eax]` need a PTR override or an unambiguous register operand in the same
/// instruction.
///
/// @param operand Operand to inspect.
/// @return true when the memory operand still needs parser width resolution.
static bool vm_parser_memory_operand_needs_width(const VmIrOperand *operand) {
    return operand != NULL && operand->kind == VM_IR_OPERAND_MEMORY_REGISTER && operand->width_bits == 0U;
}

/// Reports the stable MASM-compatible ambiguous memory-width diagnostic.
///
/// @param state Parser state to mutate.
/// @param token Token associated with the ambiguous memory operand.
static void vm_parser_report_ambiguous_memory_width(VmParserState *state, const VmLexerToken *token) {
    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, token, "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
}

/// Resolves memory operand widths for a two-operand instruction.
///
/// This shared helper implements the MASM32 Educational Mode rule that memory
/// width must come from an explicit PTR override, symbol metadata, a
/// symbol-relative operand, or an unambiguous register operand in the same
/// instruction. It deliberately refuses to infer widths from immediates.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param opcode Opcode whose operands are being validated.
/// @param destination Destination operand to update when inference is valid.
/// @param source Source operand to update when inference is valid.
/// @param destination_token Token associated with the destination operand.
/// @param source_token Token associated with the source operand.
/// @return Resolution status describing whether validation may continue.
static VmParserMemoryWidthResolutionStatus vm_parser_resolve_binary_memory_widths(
    VmParserState *state,
    VmIrOpcode opcode,
    VmIrOperand *destination,
    VmIrOperand *source,
    const VmLexerToken *destination_token,
    const VmLexerToken *source_token
) {
    uint8_t opposite_width = 0U;
    (void)opcode;
    (void)source_token;

    if (state == NULL || destination == NULL || source == NULL) {
        return VM_PARSER_MEMORY_WIDTH_UNSUPPORTED;
    }

    if (vm_parser_memory_operand_needs_width(destination)) {
        if (source->kind == VM_IR_OPERAND_REGISTER && vm_parser_resolve_operand_width(source, &opposite_width)) {
            destination->width_bits = opposite_width;
        } else if (source->kind == VM_IR_OPERAND_IMMEDIATE) {
            vm_parser_report_ambiguous_memory_width(state, destination_token);
            return VM_PARSER_MEMORY_WIDTH_AMBIGUOUS;
        }
    }

    if (vm_parser_memory_operand_needs_width(source)) {
        if (destination->kind == VM_IR_OPERAND_REGISTER && vm_parser_resolve_operand_width(destination, &opposite_width)) {
            source->width_bits = opposite_width;
        }
    }

    return VM_PARSER_MEMORY_WIDTH_RESOLVED;
}

/// Resolves memory operand width for a single-operand instruction.
///
/// Memory-only instructions such as `neg [eax]`, `inc [eax]`, and `dec [eax]`
/// have no same-instruction register source. They therefore require PTR or
/// symbol metadata instead of guessing a default width.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param opcode Opcode whose operand is being validated.
/// @param operand Operand to inspect.
/// @param operand_token Token associated with the operand.
/// @return Resolution status describing whether validation may continue.
static VmParserMemoryWidthResolutionStatus vm_parser_resolve_unary_memory_width(
    VmParserState *state,
    VmIrOpcode opcode,
    const VmIrOperand *operand,
    const VmLexerToken *operand_token
) {
    (void)opcode;

    if (state == NULL || operand == NULL) {
        return VM_PARSER_MEMORY_WIDTH_UNSUPPORTED;
    }

    if (vm_parser_memory_operand_needs_width(operand)) {
        vm_parser_report_ambiguous_memory_width(state, operand_token);
        return VM_PARSER_MEMORY_WIDTH_AMBIGUOUS;
    }

    return VM_PARSER_MEMORY_WIDTH_RESOLVED;
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
/// @param source Source operand to validate and normalize against the destination width.
/// @param source_token Token associated with the source operand for diagnostics.
/// @return true when the source can be represented without narrowing; character immediates are normalized to the selected execution width.
static bool vm_parser_validate_source_width(
    VmParserState *state,
    const VmIrOperand *destination,
    VmIrOperand *source,
    const VmLexerToken *source_token
) {
    uint8_t destination_width = 0U;
    uint8_t source_width = 0U;
    uint32_t max_value = 0U;

    if (state == NULL || destination == NULL || source == NULL) {
        return false;
    }

    if (!vm_parser_resolve_operand_width(destination, &destination_width)) {
        if (vm_parser_memory_operand_needs_width(destination) && source->kind == VM_IR_OPERAND_IMMEDIATE) {
            vm_parser_report_ambiguous_memory_width(state, source_token);
            return false;
        }
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, source_token, "Destination operand width is unsupported by the current milestone.");
        return false;
    }

    if (source->kind == VM_IR_OPERAND_IMMEDIATE) {
        if (source_token != NULL && source_token->kind == VM_LEXER_TOKEN_CHARACTER) {
            if (source->width_bits == 0U || source->width_bits > destination_width) {
                vm_parser_add_character_literal_diagnostic(state, source_token, false, true);
                return false;
            }
            source->width_bits = destination_width;
        } else if (source->width_bits != 0U && source->width_bits != destination_width) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, source_token, "Immediate operand width does not match the destination operand width.");
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
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, source_token, "Source operand width does not match the destination operand width.");
            return false;
        }
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, source_token, "Expected a register, immediate, data symbol, register-indirect memory operand, or OFFSET source operand.");
    return false;
}

/// Validates source and destination widths for MOVSX and MOVZX.
///
/// These instructions require a register destination and an 8-bit or 16-bit
/// register/memory source whose width is narrower than the destination. The
/// parser deliberately rejects ambiguous register-indirect memory sources when
/// no PTR or symbol width is available.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param destination Destination operand that must be a wider register.
/// @param source Source operand that must be 8-bit or 16-bit register/memory.
/// @param operand_token Token associated with the invalid operand for diagnostics.
/// @return true when the extension instruction operands are supported.
static bool vm_parser_validate_extension_widths(
    VmParserState *state,
    const VmIrOperand *destination,
    const VmIrOperand *source,
    const VmLexerToken *operand_token
) {
    uint8_t destination_width = 0U;
    uint8_t source_width = 0U;

    if (state == NULL || destination == NULL || source == NULL) {
        return false;
    }

    if (destination->kind != VM_IR_OPERAND_REGISTER) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, operand_token, "MOVSX and MOVZX require a register destination.");
        return false;
    }

    if (source->kind != VM_IR_OPERAND_REGISTER && !vm_parser_operand_is_memory(source)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, operand_token, "MOVSX and MOVZX require an 8-bit or 16-bit register or memory source.");
        return false;
    }

    if (!vm_parser_resolve_operand_width(destination, &destination_width)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, operand_token, "MOVSX and MOVZX destination width is unsupported.");
        return false;
    }

    if (!vm_parser_resolve_operand_width(source, &source_width)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, operand_token, "MOVSX and MOVZX memory sources require a known 8-bit or 16-bit width.");
        return false;
    }

    if (source_width != 8U && source_width != 16U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, operand_token, "MOVSX and MOVZX sources must be 8-bit or 16-bit operands.");
        return false;
    }

    if (destination_width <= source_width) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, operand_token, "MOVSX and MOVZX destinations must be wider than the source operand.");
        return false;
    }

    return true;
}


/// Validates source and destination widths for XCHG.
///
/// XCHG accepts register/register, register/memory, and memory/register forms
/// only. Memory/memory and immediate forms remain unsupported. Register-only
/// memory operands may inherit width from the opposite register operand.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param first First exchange operand.
/// @param second Second exchange operand.
/// @param operand_token Token associated with the second operand for diagnostics.
/// @return true when the XCHG operand pair is supported.
static bool vm_parser_validate_exchange_operands(
    VmParserState *state,
    const VmIrOperand *first,
    const VmIrOperand *second,
    const VmLexerToken *operand_token
) {
    uint8_t first_width = 0U;
    uint8_t second_width = 0U;

    if (state == NULL || first == NULL || second == NULL) {
        return false;
    }

    if ((first->kind != VM_IR_OPERAND_REGISTER && !vm_parser_operand_is_memory(first)) ||
        (second->kind != VM_IR_OPERAND_REGISTER && !vm_parser_operand_is_memory(second))) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, operand_token, "XCHG requires register or memory operands.");
        return false;
    }

    if (vm_parser_operand_is_memory(first) && vm_parser_operand_is_memory(second)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, operand_token, "XCHG does not support memory-to-memory operands.");
        return false;
    }

    if (!vm_parser_resolve_operand_width(first, &first_width) || !vm_parser_resolve_operand_width(second, &second_width)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, operand_token, "XCHG operands require known 8-bit, 16-bit, or 32-bit widths.");
        return false;
    }

    if (first_width != second_width) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, operand_token, "XCHG operand widths must match.");
        return false;
    }

    return true;
}

/// Validates source and destination widths for TEST.
///
/// TEST accepts register/register, register/immediate, register/memory,
/// memory/register, and memory/immediate forms when widths are known. Untyped
/// register-indirect memory with an immediate source remains ambiguous in
/// MASM32 Educational Mode and must be rejected with a width-specific
/// diagnostic rather than an unsupported-feature diagnostic.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param destination First TEST operand.
/// @param source Second TEST operand.
/// @param destination_token Token associated with the first operand for diagnostics.
/// @param source_token Token associated with the second operand for diagnostics.
/// @return true when the TEST operand pair is supported.
static bool vm_parser_validate_test_operands(
    VmParserState *state,
    const VmIrOperand *destination,
    VmIrOperand *source,
    const VmLexerToken *destination_token,
    const VmLexerToken *source_token
) {
    if (state == NULL || destination == NULL || source == NULL) {
        return false;
    }

    if (destination->kind != VM_IR_OPERAND_REGISTER && !vm_parser_operand_is_memory(destination)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, destination_token, "TEST requires a register or memory first operand.");
        return false;
    }

    if (source->kind != VM_IR_OPERAND_IMMEDIATE && source->kind != VM_IR_OPERAND_REGISTER && !vm_parser_operand_is_memory(source)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, source_token, "TEST requires a register, immediate, or memory second operand.");
        return false;
    }

    if (vm_parser_operand_is_memory(destination) && vm_parser_operand_is_memory(source)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, source_token, "TEST does not support memory-to-memory operands.");
        return false;
    }

    if (vm_parser_operand_is_memory(destination) && source->kind == VM_IR_OPERAND_IMMEDIATE && destination->width_bits == 0U) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, destination_token, "Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.");
        return false;
    }

    return vm_parser_validate_source_width(state, destination, source, source_token);
}

/// Validates the destination operand for a single-destination instruction.
///
/// NEG, INC, and DEC accept exactly one register or memory destination with a
/// known 8-bit, 16-bit, or 32-bit execution width. INC and DEC use the newer
/// invalid-instruction-operands diagnostic category for malformed operand
/// shapes; NEG keeps its existing diagnostic behavior for regression stability.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param opcode Single-destination opcode being validated.
/// @param destination Destination operand to validate.
/// @param operand_token Token associated with the operand for diagnostics.
/// @return true when the operand has a supported execution width.
static bool vm_parser_validate_single_destination_operand(
    VmParserState *state,
    VmIrOpcode opcode,
    const VmIrOperand *destination,
    const VmLexerToken *operand_token
) {
    uint8_t width_bits = 0U;
    const char *mnemonic = "instruction";
    VmParserDiagnosticCode invalid_code = VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX;

    if (state == NULL || destination == NULL) {
        return false;
    }

    if (opcode == VM_IR_OPCODE_NEG) {
        mnemonic = "NEG";
        invalid_code = VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX;
    } else if (opcode == VM_IR_OPCODE_INC) {
        mnemonic = "INC";
        invalid_code = VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS;
    } else if (opcode == VM_IR_OPCODE_DEC) {
        mnemonic = "DEC";
        invalid_code = VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS;
    }

    if (destination->kind != VM_IR_OPERAND_REGISTER && !vm_parser_operand_is_memory(destination)) {
        char message[96];
        (void)snprintf(message, sizeof(message), "%s requires a register or memory destination.", mnemonic);
        vm_parser_add_diagnostic(state, invalid_code, operand_token, message);
        return false;
    }

    if (!vm_parser_resolve_operand_width(destination, &width_bits)) {
        char message[128];
        (void)snprintf(message, sizeof(message), "%s destination requires a known 8-bit, 16-bit, or 32-bit width.", mnemonic);
        vm_parser_add_diagnostic(state, opcode == VM_IR_OPCODE_NEG ? VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH : VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, operand_token, message);
        return false;
    }

    return true;
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
        if (vm_parser_token_is_deferred_condition_operator(token)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, token,
                                     "High-level condition operators such as EQ are not supported in runtime instruction operands or Milestone 29 constant expressions.");
            return false;
        }
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_LINE_END, token, "Expected end of line after statement.");
        return false;
    }

    if (token != NULL && token->kind == VM_LEXER_TOKEN_NEWLINE) {
        vm_parser_advance(state);
    }
    return true;
}

/// Returns whether an operand denotes statically known read-only `.CONST` storage.
///
/// Absolute symbol memory operands are checked by containing address.
/// Symbol/register operands are treated as read-only when their static base
/// address belongs to a `.CONST` symbol; pure register-indirect operands are
/// left for runtime permission checks.
///
/// @param state Parser state whose symbol table should be inspected.
/// @param operand Operand to classify.
/// @return true when the operand is a statically known `.CONST` memory operand.
static bool vm_parser_operand_targets_const_storage(const VmParserState *state, const VmIrOperand *operand) {
    const VmSymbol *symbol = NULL;
    uint32_t address = 0U;

    if (state == NULL || state->config == NULL || state->result == NULL || operand == NULL) {
        return false;
    }

    if (operand->kind == VM_IR_OPERAND_MEMORY_ADDRESS) {
        address = operand->address;
    } else if (operand->kind == VM_IR_OPERAND_MEMORY_REGISTER && operand->address != 0U) {
        address = operand->address;
    } else {
        return false;
    }

    symbol = vm_symbol_find_by_address(state->config->symbols, state->result->symbol_count, address);
    return vm_symbol_is_read_only(symbol);
}

/// Rejects writes to statically known `.CONST` operands.
///
/// Direct and symbol-relative `.CONST` destinations are assembly-time errors.
/// Pure calculated-address writes are intentionally left to checked memory
/// permissions at runtime.
///
/// @param state Parser state to mutate when diagnostics are needed.
/// @param opcode Instruction opcode being validated.
/// @param destination Destination operand.
/// @param source Source operand.
/// @param destination_token Token associated with the destination.
/// @param source_token Token associated with the source.
/// @return true when the instruction does not statically write `.CONST`.
static bool vm_parser_reject_static_const_write(
    VmParserState *state,
    VmIrOpcode opcode,
    const VmIrOperand *destination,
    const VmIrOperand *source,
    const VmLexerToken *destination_token,
    const VmLexerToken *source_token
) {
    if (state == NULL || destination == NULL || source == NULL) {
        return false;
    }

    if (opcode == VM_IR_OPCODE_TEST) {
        return true;
    }

    if (vm_parser_operand_targets_const_storage(state, destination)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_CONST_WRITE, destination_token, "Cannot write to .CONST data. Constant data is read-only.");
        return false;
    }

    if (opcode == VM_IR_OPCODE_XCHG && vm_parser_operand_targets_const_storage(state, source)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_CONST_WRITE, source_token, "Cannot write to .CONST data. Constant data is read-only.");
        return false;
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

/// Returns whether a token names the Irvine32 `exit` virtual terminator.
///
/// @param token Token to inspect.
/// @return true when @p token is an identifier spelling `exit` case-insensitively.
static bool vm_parser_token_is_exit_terminator(const VmLexerToken *token) {
    return token != NULL &&
           token->kind == VM_LEXER_TOKEN_IDENTIFIER &&
           vm_parser_token_equals(token, "exit");
}

/// Emits an unsupported-routine diagnostic for a virtual Irvine32 symbol when possible.
///
/// The registry is active only after `INCLUDE Irvine32.inc`. CALL target
/// classification is deliberately deferred until CALL exists, so this helper
/// handles only bare mnemonic-like executable forms currently recognized by the
/// parser.
///
/// @param state Parser state to mutate.
/// @param mnemonic_token Candidate executable mnemonic token.
/// @return true when a diagnostic was emitted for a known Irvine32 name.
static bool vm_parser_diagnose_irvine32_symbol_use_if_known(VmParserState *state, const VmLexerToken *mnemonic_token) {
    VmIrvine32SymbolClass symbol_class = VM_IRVINE32_SYMBOL_CLASS_UNKNOWN;

    if (state == NULL || state->result == NULL || mnemonic_token == NULL ||
        !state->result->has_irvine32_virtual_include ||
        mnemonic_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return false;
    }

    symbol_class = vm_parser_classify_irvine32_symbol(mnemonic_token->lexeme, mnemonic_token->lexeme_length);
    if (symbol_class == VM_IRVINE32_SYMBOL_CLASS_UNKNOWN) {
        return false;
    }

    if (symbol_class == VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE, mnemonic_token, "Recognized external or Windows API name from Irvine32 context. Windows/API execution and linking are not supported.");
        return true;
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE, mnemonic_token, "Recognized Irvine32 routine, but executable Irvine32 routine behavior is deferred to a later milestone.");
    return true;
}

/// Parses one implemented instruction.
///
/// @param state Parser state to mutate.
/// @return true when the instruction was parsed and emitted.
static bool vm_parser_parse_instruction(VmParserState *state) {
    const VmLexerToken *mnemonic_token = vm_parser_current_token(state);
    VmIrOpcode opcode = VM_IR_OPCODE_MOV;
    VmIrOperand destination = vm_ir_operand_none();
    VmIrOperand source = vm_ir_operand_none();
    const VmLexerToken *destination_token = NULL;
    const VmLexerToken *source_token = NULL;

    if (!vm_parser_parse_opcode(mnemonic_token, &opcode)) {
        if (vm_parser_token_is_exit_terminator(mnemonic_token)) {
            if (state == NULL || state->result == NULL || !state->result->has_irvine32_virtual_include) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNKNOWN_INSTRUCTION, mnemonic_token, "Unknown instruction or virtual Irvine32 terminator. Add INCLUDE Irvine32.inc to use exit.");
                return false;
            }
            opcode = VM_IR_OPCODE_EXIT;
        } else {
            if (vm_parser_token_equals(mnemonic_token, "call")) {
                vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION, mnemonic_token, "CALL is not supported yet.");
                return false;
            }
            if (vm_parser_diagnose_irvine32_symbol_use_if_known(state, mnemonic_token)) {
                return false;
            }
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION, mnemonic_token, "Unsupported instruction for the current milestone.");
            return false;
        }
    }

    vm_parser_advance(state);
    if (vm_parser_opcode_has_no_operands(opcode)) {
        if (opcode == VM_IR_OPCODE_EXIT && !vm_parser_is_line_end_token(vm_parser_current_token(state))) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, vm_parser_current_token(state), "exit does not take operands.");
            return false;
        }
        if ((opcode == VM_IR_OPCODE_NOP || opcode == VM_IR_OPCODE_CLC || opcode == VM_IR_OPCODE_STC || opcode == VM_IR_OPCODE_CMC) &&
            !vm_parser_is_line_end_token(vm_parser_current_token(state))) {
            const char *message = "Instruction does not take operands.";
            if (opcode == VM_IR_OPCODE_NOP) {
                message = "NOP does not take operands.";
            } else if (opcode == VM_IR_OPCODE_CLC) {
                message = "CLC does not take operands.";
            } else if (opcode == VM_IR_OPCODE_STC) {
                message = "STC does not take operands.";
            } else if (opcode == VM_IR_OPCODE_CMC) {
                message = "CMC does not take operands.";
            }
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, vm_parser_current_token(state), message);
            return false;
        }
        if (!vm_parser_expect_line_end(state)) {
            return false;
        }
        return vm_parser_emit_instruction(state, opcode, destination, source, mnemonic_token);
    }

    destination_token = vm_parser_current_token(state);
    if ((opcode == VM_IR_OPCODE_INC || opcode == VM_IR_OPCODE_DEC) &&
        destination_token != NULL &&
        (destination_token->kind == VM_LEXER_TOKEN_NUMBER ||
         destination_token->kind == VM_LEXER_TOKEN_CHARACTER ||
         destination_token->kind == VM_LEXER_TOKEN_PLUS ||
         destination_token->kind == VM_LEXER_TOKEN_MINUS ||
         destination_token->kind == VM_LEXER_TOKEN_LEFT_PAREN)) {
        vm_parser_add_diagnostic(
            state,
            VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS,
            destination_token,
            opcode == VM_IR_OPCODE_INC ? "INC requires a register or memory destination." : "DEC requires a register or memory destination."
        );
        return false;
    }
    if (!vm_parser_parse_destination_operand(state, &destination)) {
        return false;
    }

    if (vm_parser_opcode_is_single_destination_operand(opcode)) {
        if (vm_parser_resolve_unary_memory_width(state, opcode, &destination, destination_token) != VM_PARSER_MEMORY_WIDTH_RESOLVED) {
            return false;
        }
        if (!vm_parser_validate_single_destination_operand(state, opcode, &destination, mnemonic_token)) {
            return false;
        }
        if (!vm_parser_reject_static_const_write(state, opcode, &destination, &source, destination_token, source_token)) {
            return false;
        }
        if (!vm_parser_is_line_end_token(vm_parser_current_token(state))) {
            const char *message = "NEG takes exactly one register or memory operand.";
            VmParserDiagnosticCode code = VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX;
            if (opcode == VM_IR_OPCODE_INC) {
                message = "INC takes exactly one register or memory operand.";
                code = VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS;
            } else if (opcode == VM_IR_OPCODE_DEC) {
                message = "DEC takes exactly one register or memory operand.";
                code = VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS;
            }
            vm_parser_add_diagnostic(state, code, vm_parser_current_token(state), message);
            return false;
        }
        if (!vm_parser_expect_line_end(state)) {
            return false;
        }
        return vm_parser_emit_instruction(state, opcode, destination, source, mnemonic_token);
    }

    if (!vm_parser_expect_comma(state)) {
        return false;
    }
    source_token = vm_parser_current_token(state);
    if (!vm_parser_parse_source_operand(state, &source)) {
        return false;
    }

    if (vm_parser_opcode_is_extension_move(opcode)) {
        if (!vm_parser_validate_extension_widths(state, &destination, &source, source_token)) {
            return false;
        }
    } else if (vm_parser_opcode_is_exchange(opcode)) {
        if (vm_parser_resolve_binary_memory_widths(state, opcode, &destination, &source, destination_token, source_token) != VM_PARSER_MEMORY_WIDTH_RESOLVED) {
            return false;
        }
        if (!vm_parser_validate_exchange_operands(state, &destination, &source, source_token)) {
            return false;
        }
    } else if (vm_parser_opcode_is_test(opcode)) {
        if (vm_parser_resolve_binary_memory_widths(state, opcode, &destination, &source, destination_token, source_token) != VM_PARSER_MEMORY_WIDTH_RESOLVED) {
            return false;
        }
        if (!vm_parser_validate_test_operands(state, &destination, &source, destination_token, source_token)) {
            return false;
        }
    } else {
        if (vm_parser_resolve_binary_memory_widths(state, opcode, &destination, &source, destination_token, source_token) != VM_PARSER_MEMORY_WIDTH_RESOLVED) {
            return false;
        }
        if (!vm_parser_validate_source_width(state, &destination, &source, source_token)) {
            return false;
        }
    }

    if (!vm_parser_reject_static_const_write(state, opcode, &destination, &source, destination_token, source_token)) {
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

/// Returns whether the current token sequence starts a supported data-like section.
///
/// @param state Parser state to inspect.
/// @return true for `.data`, `.DATA?`, or `.CONST`.
static bool vm_parser_current_token_starts_data_section(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);
    const VmLexerToken *next = vm_parser_peek_token(state, 1U);

    return token != NULL && token->kind == VM_LEXER_TOKEN_DIRECTIVE &&
           (vm_parser_token_equals(token, ".data") ||
            vm_parser_token_equals(token, ".const") ||
            vm_parser_is_data_question_directive(token, next));
}

/// Parses a data-like section directive line.
///
/// @param state Parser state to mutate.
/// @return true when the directive line was accepted.
static bool vm_parser_parse_data_directive(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);
    const VmLexerToken *next = vm_parser_peek_token(state, 1U);

    if (token == NULL || token->kind != VM_LEXER_TOKEN_DIRECTIVE) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION, token, "Expected .data, .DATA?, .CONST, or .code directive.");
        return false;
    }

    state->saw_data_directive = true;
    if (vm_parser_is_data_question_directive(token, next)) {
        state->section = VM_PARSER_SECTION_DATA_UNINITIALIZED;
        vm_parser_advance(state);
        vm_parser_advance(state);
        return vm_parser_expect_line_end(state);
    }

    if (vm_parser_token_equals(token, ".const")) {
        state->section = VM_PARSER_SECTION_CONST;
        vm_parser_advance(state);
        return vm_parser_expect_line_end(state);
    }

    if (vm_parser_token_equals(token, ".data")) {
        state->section = VM_PARSER_SECTION_DATA;
        vm_parser_advance(state);
        return vm_parser_expect_line_end(state);
    }

    vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION, token, "Expected .data, .DATA?, .CONST, or .code directive.");
    return false;
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

    if (state->procedure_name.is_set) {
        VmLexerToken procedure_token;
        memset(&procedure_token, 0, sizeof(procedure_token));
        procedure_token.lexeme = state->procedure_name.lexeme;
        procedure_token.lexeme_length = state->procedure_name.length;
        if (!vm_parser_user_symbol_tokens_equal(state, &procedure_token, name_token)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_END_TARGET, name_token, "ENDP procedure name does not match the parsed procedure name under the active CASEMAP policy.");
            return false;
        }
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
        if (!vm_parser_user_symbol_tokens_equal(state, &procedure_token, entry_token)) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_END_TARGET, entry_token, "END entry point does not match the parsed procedure name under the active CASEMAP policy.");
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

    if (vm_parser_parse_equate_line_if_recognized(state)) {
        return !state->diagnostic_overflowed;
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "option")) {
        return vm_parser_parse_option_directive(state) && !state->diagnostic_overflowed;
    }

    if (vm_parser_recover_unsupported_feature_if_recognized(state)) {
        return !state->diagnostic_overflowed;
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

/// Returns whether a token can be used as a header operand word.
///
/// @param token Token to inspect.
/// @param spelling Case-insensitive spelling to match.
/// @return true when the token exists and its lexeme matches @p spelling.
static bool vm_parser_header_token_equals(const VmLexerToken *token, const char *spelling) {
    return token != NULL && spelling != NULL && vm_parser_token_equals(token, spelling);
}

/// Consumes a fixed number of parser tokens.
///
/// @param state Parser state to mutate.
/// @param count Number of tokens to consume.
static void vm_parser_advance_many(VmParserState *state, size_t count) {
    size_t index = 0U;

    for (index = 0U; index < count; index += 1U) {
        vm_parser_advance(state);
    }
}

/// Parses a processor compatibility directive accepted as a no-op.
///
/// @param state Parser state to mutate.
/// @return true when the line was accepted.
static bool vm_parser_parse_processor_directive(VmParserState *state) {
    vm_parser_advance(state);
    if (!vm_parser_expect_line_end(state)) {
        vm_parser_recover_skip_line(state);
        return false;
    }

    return true;
}

/// Parses `.model flat, stdcall` as MASM32 compatibility syntax.
///
/// @param state Parser state to mutate.
/// @return true when a `.model` line was consumed; diagnostics may have been recorded.
static bool vm_parser_parse_model_directive(VmParserState *state) {
    const VmLexerToken *model_token = vm_parser_current_token(state);
    const VmLexerToken *flat_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *comma_token = vm_parser_peek_token(state, 2U);
    const VmLexerToken *stdcall_token = vm_parser_peek_token(state, 3U);
    const VmLexerToken *tail_token = vm_parser_peek_token(state, 4U);

    if (vm_parser_header_token_equals(flat_token, "flat") && comma_token != NULL && comma_token->kind == VM_LEXER_TOKEN_COMMA &&
        vm_parser_header_token_equals(stdcall_token, "stdcall") && vm_parser_is_line_end_token(tail_token)) {
        vm_parser_advance_many(state, 4U);
        (void)vm_parser_expect_line_end(state);
        return true;
    }

    (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MODEL, model_token, ".model form is unsupported. Use `.model flat, stdcall` in MASM32 Educational Mode.");
    vm_parser_recover_skip_line(state);
    return true;
}

/// Parses `.stack` with an optional size as metadata only.
///
/// @param state Parser state to mutate.
/// @return true when a `.stack` line was consumed; diagnostics may have been recorded.
static bool vm_parser_parse_stack_directive(VmParserState *state) {
    const VmLexerToken *stack_token = vm_parser_current_token(state);
    const VmLexerToken *size_token = vm_parser_peek_token(state, 1U);

    if (state != NULL && state->result != NULL && stack_token != NULL) {
        state->result->has_stack_directive_source_span = true;
        state->result->stack_directive_source_location = stack_token->location;
        state->result->stack_directive_source_span_length = stack_token->lexeme_length;
    }

    vm_parser_advance(state);
    if (vm_parser_is_line_end_token(size_token)) {
        return vm_parser_expect_line_end(state);
    }

    if (size_token == NULL || !vm_parser_token_starts_constant_expression(size_token)) {
        (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, size_token != NULL ? size_token : stack_token, ".stack accepts no operand or one non-negative 32-bit constant expression.");
        vm_parser_recover_skip_line(state);
        return true;
    }

    {
        VmParserConstantExpression expression;
        memset(&expression, 0, sizeof(expression));
        if (!vm_parser_parse_constant_expression(state, &expression) || expression.value < 0 || expression.value > (int64_t)UINT32_MAX) {
            if (state->result->diagnostic_count == 0U || state->config->diagnostics[state->result->diagnostic_count - 1U].location.offset != size_token->location.offset) {
                (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, size_token, ".stack size must be a non-negative 32-bit constant expression.");
            }
            vm_parser_recover_skip_line(state);
            return true;
        }
        state->result->has_requested_stack_size = true;
        state->result->requested_stack_size = (uint32_t)expression.value;
    }
    if (!vm_parser_expect_line_end(state)) {
        vm_parser_recover_skip_line(state);
    }

    return true;
}

/// Returns whether tokens after INCLUDE match a supported virtual include name.
///
/// @param state Parser state positioned at INCLUDE.
/// @param basename Expected include basename without `.inc`.
/// @return true when the next three tokens spell `<basename>.inc`.
static bool vm_parser_include_path_matches(const VmParserState *state, const char *basename) {
    const VmLexerToken *base_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *dot_or_extension_token = vm_parser_peek_token(state, 2U);
    const VmLexerToken *extension_token = vm_parser_peek_token(state, 3U);
    const VmLexerToken *tail_after_split_extension = vm_parser_peek_token(state, 4U);

    if (!vm_parser_header_token_equals(base_token, basename)) {
        return false;
    }

    if (dot_or_extension_token != NULL && dot_or_extension_token->kind == VM_LEXER_TOKEN_DIRECTIVE &&
        vm_parser_token_equals(dot_or_extension_token, ".inc") && vm_parser_is_line_end_token(extension_token)) {
        return true;
    }

    return dot_or_extension_token != NULL && dot_or_extension_token->kind == VM_LEXER_TOKEN_DOT &&
           vm_parser_header_token_equals(extension_token, "inc") && vm_parser_is_line_end_token(tail_after_split_extension);
}

/// Parses supported virtual INCLUDE directives and rejects real include files.
///
/// @param state Parser state to mutate.
/// @return true when an INCLUDE line was consumed; diagnostics may have been recorded.
static bool vm_parser_parse_include_directive(VmParserState *state) {
    const VmLexerToken *include_token = vm_parser_current_token(state);

    if (vm_parser_include_path_matches(state, "irvine32") || vm_parser_include_path_matches(state, "macros")) {
        bool is_irvine32_include = vm_parser_include_path_matches(state, "irvine32");
        const VmLexerToken *extension_token = vm_parser_peek_token(state, 2U);
        if (is_irvine32_include && state != NULL && state->result != NULL) {
            state->result->has_irvine32_virtual_include = true;
            state->result->irvine32_virtual_symbol_count = vm_parser_irvine32_registry_symbol_count();
        }
        if (extension_token != NULL && extension_token->kind == VM_LEXER_TOKEN_DIRECTIVE) {
            vm_parser_advance_many(state, 3U);
        } else {
            vm_parser_advance_many(state, 4U);
        }
        (void)vm_parser_expect_line_end(state);
        return true;
    }

    const VmLexerToken *path_token = vm_parser_peek_token(state, 1U);
    (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDE, path_token != NULL ? path_token : include_token, "Unsupported include file. Only virtual INCLUDE Irvine32.inc and INCLUDE Macros.inc are accepted.");
    vm_parser_recover_skip_line(state);
    return true;
}

/// Parses OPTION CASEMAP directives and updates source-order symbol policy.
///
/// @param state Parser state to mutate.
/// @return true when an OPTION line was consumed; diagnostics may have been recorded.
static bool vm_parser_parse_option_directive(VmParserState *state) {
    const VmLexerToken *option_token = vm_parser_current_token(state);
    const VmLexerToken *casemap_token = vm_parser_peek_token(state, 1U);
    const VmLexerToken *colon_token = vm_parser_peek_token(state, 2U);
    const VmLexerToken *value_token = vm_parser_peek_token(state, 3U);
    const VmLexerToken *tail_token = vm_parser_peek_token(state, 4U);
    VmParserUserSymbolCasePolicy new_policy = VM_PARSER_USER_SYMBOL_CASEMAP_ALL;
    bool is_supported_policy = false;

    if (state == NULL) {
        return false;
    }

    if (vm_parser_header_token_equals(casemap_token, "casemap") && colon_token != NULL && colon_token->kind == VM_LEXER_TOKEN_COLON &&
        value_token != NULL && vm_parser_is_line_end_token(tail_token)) {
        if (vm_parser_header_token_equals(value_token, "all")) {
            new_policy = VM_PARSER_USER_SYMBOL_CASEMAP_ALL;
            is_supported_policy = true;
        } else if (vm_parser_header_token_equals(value_token, "none")) {
            new_policy = VM_PARSER_USER_SYMBOL_CASEMAP_NONE;
            is_supported_policy = true;
        } else if (vm_parser_header_token_equals(value_token, "notpublic")) {
            (void)vm_parser_add_diagnostic(
                state,
                VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION,
                value_token,
                "OPTION CASEMAP:NOTPUBLIC is unsupported because public/external linkage semantics are not implemented."
            );
        } else {
            (void)vm_parser_add_diagnostic(
                state,
                VM_PARSER_DIAGNOSTIC_INVALID_OPTION_VALUE,
                value_token,
                "Invalid OPTION CASEMAP value. Supported CASEMAP values: ALL, NONE. Recognized but unsupported: NOTPUBLIC."
            );
        }

        if (is_supported_policy) {
            if (state->has_explicit_casemap_policy && state->user_symbol_case_policy != new_policy) {
                (void)vm_parser_add_warning(
                    state,
                    VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED,
                    value_token,
                    "OPTION CASEMAP changed the active user-symbol case policy."
                );
            }
            state->user_symbol_case_policy = new_policy;
            state->has_explicit_casemap_policy = true;
        }

        vm_parser_advance_many(state, 4U);
        (void)vm_parser_expect_line_end(state);
        return true;
    }

    (void)vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION, option_token, "Unsupported OPTION form. Supported CASEMAP values: ALL, NONE. Recognized but unsupported: NOTPUBLIC.");
    vm_parser_recover_skip_line(state);
    return true;
}

/// Parses listing/documentation directives accepted as no-ops.
///
/// @param state Parser state to mutate.
/// @return true after the directive line is consumed.
static bool vm_parser_parse_listing_noop_directive(VmParserState *state) {
    vm_parser_recover_skip_line(state);
    return true;
}

/// Parses a numeric equate declaration when the current line starts one.
///
/// Supported forms are `name = expression` and `name EQU expression`. The
/// resulting constants are stored in an internal table distinct from data
/// symbols.
///
/// @param state Parser state to mutate.
/// @return true when an equate-like line was consumed; false when the current
/// line does not start a numeric equate.
static bool vm_parser_parse_equate_line_if_recognized(VmParserState *state) {
    const VmLexerToken *name_token = vm_parser_current_token(state);
    const VmLexerToken *operator_token = vm_parser_peek_token(state, 1U);
    VmParserConstantExpression expression;
    VmParserEquate *equate = NULL;

    if (state == NULL || name_token == NULL || operator_token == NULL || name_token->kind != VM_LEXER_TOKEN_IDENTIFIER) {
        return false;
    }

    if (!(operator_token->kind == VM_LEXER_TOKEN_EQUALS ||
          (operator_token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(operator_token, "EQU")))) {
        return false;
    }

    if (vm_parser_find_equate(state, name_token) != NULL ||
        vm_parser_has_conflicting_data_symbol(state, name_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_DUPLICATE_SYMBOL, name_token, "Duplicate numeric equate or data symbol name.");
        vm_parser_recover_skip_line(state);
        return true;
    }

    if (state->equate_count >= (size_t)VM_PARSER_EQUATE_CAPACITY) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_EQUATE, name_token, "Numeric equate capacity exceeded.");
        vm_parser_recover_skip_line(state);
        return true;
    }

    equate = &state->equates[state->equate_count];
    memset(equate, 0, sizeof(*equate));
    if (!vm_parser_set_equate_name(equate, name_token)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_EQUATE, name_token, "Numeric equate name is too long for the current fixed table.");
        vm_parser_recover_skip_line(state);
        return true;
    }
    state->equate_count += 1U;

    vm_parser_advance(state);
    vm_parser_advance(state);
    if (vm_parser_is_line_end_token(vm_parser_current_token(state))) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_EQUATE, operator_token, "Numeric equate requires a constant expression.");
        equate->is_invalid = true;
        return true;
    }

    if (vm_parser_current_token(state) != NULL &&
        (vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_LESS_THAN ||
         vm_parser_current_token(state)->kind == VM_LEXER_TOKEN_STRING)) {
        vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_INVALID_EQUATE, vm_parser_current_token(state),
                                 "Text EQU constants are not supported in this milestone; use numeric constant expressions only.");
        equate->is_invalid = true;
        vm_parser_recover_skip_line(state);
        return true;
    }

    memset(&expression, 0, sizeof(expression));
    state->active_equate_name = name_token;
    equate->is_resolving = true;
    if (!vm_parser_parse_constant_expression(state, &expression)) {
        equate->is_resolving = false;
        equate->is_invalid = true;
        state->active_equate_name = NULL;
        vm_parser_recover_skip_line(state);
        return true;
    }
    equate->is_resolving = false;
    state->active_equate_name = NULL;

    if (!vm_parser_is_line_end_token(vm_parser_current_token(state))) {
        if (vm_parser_token_is_deferred_condition_operator(vm_parser_current_token(state))) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, vm_parser_current_token(state),
                                     "High-level condition operators such as EQ are not supported in Milestone 29 constant expressions.");
        } else {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, vm_parser_current_token(state),
                                     "Unsupported operator or trailing token in numeric equate expression.");
        }
        equate->is_invalid = true;
        vm_parser_recover_skip_line(state);
        return true;
    }
    if (!vm_parser_expect_line_end(state)) {
        equate->is_invalid = true;
        vm_parser_recover_skip_line(state);
        return true;
    }

    equate->value = expression.value;
    equate->is_defined = true;
    return true;
}

/// Parses one accepted MASM32 header compatibility line before `.data` or `.code`.
///
/// @param state Parser state to mutate.
/// @return true when a known header line was consumed; false when the line is not a Phase 26 header.
static bool vm_parser_parse_header_line_if_recognized(VmParserState *state) {
    const VmLexerToken *token = vm_parser_current_token(state);

    if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
        return false;
    }

    if (token->kind == VM_LEXER_TOKEN_DOT) {
        const VmLexerToken *processor_token = vm_parser_peek_token(state, 1U);
        if (processor_token != NULL && processor_token->kind == VM_LEXER_TOKEN_NUMBER && !processor_token->number_is_negative &&
            (processor_token->number_value == 386U || processor_token->number_value == 486U ||
             processor_token->number_value == 586U || processor_token->number_value == 686U)) {
            vm_parser_advance_many(state, 2U);
            if (!vm_parser_expect_line_end(state)) {
                vm_parser_recover_skip_line(state);
            }
            return true;
        }
    }

    if (token->kind == VM_LEXER_TOKEN_DIRECTIVE) {
        if (vm_parser_token_equals(token, ".386") || vm_parser_token_equals(token, ".486") ||
            vm_parser_token_equals(token, ".586") || vm_parser_token_equals(token, ".686")) {
            return vm_parser_parse_processor_directive(state);
        }
        if (vm_parser_token_equals(token, ".model")) {
            return vm_parser_parse_model_directive(state);
        }
        if (vm_parser_token_equals(token, ".stack")) {
            return vm_parser_parse_stack_directive(state);
        }
    }

    if (token->kind == VM_LEXER_TOKEN_IDENTIFIER) {
        if (vm_parser_token_equals(token, "include")) {
            return vm_parser_parse_include_directive(state);
        }
        if (vm_parser_token_equals(token, "option")) {
            return vm_parser_parse_option_directive(state);
        }
        if (vm_parser_token_equals(token, "title") || vm_parser_token_equals(token, "subtitle") ||
            vm_parser_token_equals(token, "page")) {
            return vm_parser_parse_listing_noop_directive(state);
        }
    }

    return false;
}

/// Parses all supported data-like declarations until the .code directive.
///
/// @param state Parser state to mutate.
/// @return true when the data-like sections and following .code directive were parsed.
static bool vm_parser_parse_data_sections(VmParserState *state) {
    const VmLexerToken *token = NULL;

    if (!vm_parser_parse_data_directive(state)) {
        return false;
    }

    while (true) {
        vm_parser_skip_newlines(state);
        token = vm_parser_current_token(state);
        if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, token, "Expected .code directive after data declarations.");
            return false;
        }

        if (vm_parser_parse_equate_line_if_recognized(state)) {
            if (state->diagnostic_overflowed) {
                return false;
            }
            continue;
        }

        if (token->kind == VM_LEXER_TOKEN_IDENTIFIER && vm_parser_token_equals(token, "option")) {
            if (!vm_parser_parse_option_directive(state) || state->diagnostic_overflowed) {
                return false;
            }
            continue;
        }

        if (vm_parser_recover_unsupported_feature_if_recognized(state)) {
            if (state->diagnostic_overflowed) {
                return false;
            }
            continue;
        }

        if (token->kind == VM_LEXER_TOKEN_DIRECTIVE) {
            if (vm_parser_token_equals(token, ".code")) {
                return vm_parser_parse_code_directive(state);
            }
            if (vm_parser_current_token_starts_data_section(state)) {
                if (!vm_parser_parse_data_directive(state)) {
                    return false;
                }
                continue;
            }
            vm_parser_add_diagnostic(state, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION, token, "Unsupported directive inside data section.");
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
        (config->const_image == NULL && config->const_image_capacity > 0U) ||
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
    state.user_symbol_case_policy = VM_PARSER_USER_SYMBOL_CASEMAP_ALL;
    state.config = config;
    state.result = out_result;
    vm_parser_init_result(out_result, VM_PARSER_STATUS_OK);

    if (config->symbols != NULL && config->symbol_capacity > 0U) {
        memset(config->symbols, 0, sizeof(config->symbols[0]) * config->symbol_capacity);
    }
    if (config->data_image != NULL && config->data_image_capacity > 0U) {
        memset(config->data_image, 0, config->data_image_capacity);
    }
    if (config->const_image != NULL && config->const_image_capacity > 0U) {
        memset(config->const_image, 0, config->const_image_capacity);
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
        size_t diagnostic_index = 0U;

        for (diagnostic_index = 0U; diagnostic_index < state.lexer_result.diagnostic_count; diagnostic_index += 1U) {
            (void)vm_parser_add_lexer_diagnostic(&state, &config->lexer_diagnostics[diagnostic_index]);
        }

        if (state.lexer_result.diagnostic_count == 0U) {
            (void)vm_parser_add_lexer_status_diagnostic(&state, lexer_status);
        } else if (lexer_status == VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED) {
            (void)vm_parser_add_lexer_status_diagnostic(&state, lexer_status);
        }

        out_result->status = state.diagnostic_overflowed ? VM_PARSER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED : VM_PARSER_STATUS_LEXER_FAILED;
        return out_result->status;
    }

    vm_parser_skip_newlines(&state);
    token = vm_parser_current_token(&state);
    if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
        if (out_result->diagnostic_count > 0U) {
            out_result->status = vm_parser_finalize_status(&state);
            return out_result->status;
        }
        vm_parser_add_diagnostic(&state, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, token, "Expected .code directive.");
        out_result->status = vm_parser_finalize_status(&state);
        return out_result->status;
    }

    while (true) {
        bool consumed_preamble_line = false;

        consumed_preamble_line = vm_parser_parse_equate_line_if_recognized(&state);
        if (!consumed_preamble_line) {
            consumed_preamble_line = vm_parser_parse_header_line_if_recognized(&state);
        }
        if (!consumed_preamble_line) {
            consumed_preamble_line = vm_parser_recover_unsupported_feature_if_recognized(&state);
        }

        if (!consumed_preamble_line) {
            break;
        }
        if (state.diagnostic_overflowed) {
            out_result->status = vm_parser_finalize_status(&state);
            return out_result->status;
        }
        vm_parser_skip_newlines(&state);
        token = vm_parser_current_token(&state);
        if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
            break;
        }
    }

    if (token == NULL || token->kind == VM_LEXER_TOKEN_EOF) {
        if (out_result->diagnostic_count > 0U) {
            out_result->status = vm_parser_finalize_status(&state);
            return out_result->status;
        }
        vm_parser_add_diagnostic(&state, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, token, "Expected .code directive.");
        out_result->status = vm_parser_finalize_status(&state);
        return out_result->status;
    }

    if (vm_parser_current_token_starts_data_section(&state)) {
        if (!vm_parser_parse_data_sections(&state)) {
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

VmIrvine32SymbolClass vm_parser_classify_irvine32_symbol(const char *name, size_t name_length) {
    size_t index = 0U;

    if (name == NULL) {
        return VM_IRVINE32_SYMBOL_CLASS_UNKNOWN;
    }

    for (index = 0U; index < sizeof(VM_PARSER_IRVINE32_REGISTRY) / sizeof(VM_PARSER_IRVINE32_REGISTRY[0]); index += 1U) {
        if (vm_parser_span_equals_ascii_case_insensitive(name, name_length, VM_PARSER_IRVINE32_REGISTRY[index].name)) {
            return VM_PARSER_IRVINE32_REGISTRY[index].symbol_class;
        }
    }

    return VM_IRVINE32_SYMBOL_CLASS_UNKNOWN;
}

size_t vm_parser_irvine32_registry_symbol_count(void) {
    return sizeof(VM_PARSER_IRVINE32_REGISTRY) / sizeof(VM_PARSER_IRVINE32_REGISTRY[0]);
}

const char *vm_parser_irvine32_symbol_class_name(VmIrvine32SymbolClass symbol_class) {
    switch (symbol_class) {
        case VM_IRVINE32_SYMBOL_CLASS_UNKNOWN:
            return "unknown";
        case VM_IRVINE32_SYMBOL_CLASS_SUPPORTED_VIRTUAL_INTRINSIC:
            return "supported-virtual-intrinsic";
        case VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE:
            return "planned-routine";
        case VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE:
            return "unsupported-routine";
        case VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL:
            return "windows-api-or-external";
        default:
            return NULL;
    }
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
        case VM_PARSER_DIAGNOSTIC_LEXER_INVALID_ARGUMENT:
            return "lexer-invalid-argument";
        case VM_PARSER_DIAGNOSTIC_LEXER_TOKEN_CAPACITY_EXCEEDED:
            return "token-capacity-exceeded";
        case VM_PARSER_DIAGNOSTIC_LEXER_DIAGNOSTIC_CAPACITY_EXCEEDED:
            return "diagnostic-capacity-exceeded";
        case VM_PARSER_DIAGNOSTIC_LEXER_UNEXPECTED_CHARACTER:
            return "unexpected-character";
        case VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_STRING:
            return "unterminated-string";
        case VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_CHARACTER:
            return "unterminated-character";
        case VM_PARSER_DIAGNOSTIC_LEXER_NUMBER_OVERFLOW:
            return "number-overflow";
        case VM_PARSER_DIAGNOSTIC_LEXER_INVALID_HEX_LITERAL:
            return "invalid-hex-literal";
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
        case VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH:
            return "operand-width-mismatch";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE:
            return "unsupported-feature";
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
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION:
            return "unsupported-type-expression";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION:
            return "unsupported-lengthof-expression";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION:
            return "unsupported-sizeof-expression";
        case VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL:
            return "invalid-character-literal";
        case VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH:
            return "ambiguous-memory-width";
        case VM_PARSER_DIAGNOSTIC_CONST_WRITE:
            return "const-write";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MODEL:
            return "unsupported-model";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDE:
            return "unsupported-include";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION:
            return "unsupported-option";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE:
            return "unsupported-register-indirect-base";
        case VM_PARSER_DIAGNOSTIC_INVALID_DUP:
            return "invalid-dup";
        case VM_PARSER_DIAGNOSTIC_INVALID_EQUATE:
            return "invalid-equate";
        case VM_PARSER_DIAGNOSTIC_UNKNOWN_EQUATE:
            return "unknown-equate";
        case VM_PARSER_DIAGNOSTIC_RECURSIVE_EQUATE:
            return "recursive-equate";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION:
            return "unsupported-constant-expression";
        case VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL:
            return "ambiguous-symbol";
        case VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED:
            return "casemap-policy-changed";
        case VM_PARSER_DIAGNOSTIC_INVALID_OPTION_VALUE:
            return "invalid-option-value";
        case VM_PARSER_DIAGNOSTIC_UNKNOWN_INSTRUCTION:
            return "unknown-instruction";
        case VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS:
            return "invalid-instruction-operands";
        case VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE:
            return "unsupported-irvine32-routine";
        default:
            return NULL;
    }
}

const char *vm_parser_diagnostic_severity_name(VmParserDiagnosticSeverity severity) {
    switch (severity) {
        case VM_PARSER_DIAGNOSTIC_SEVERITY_WARNING:
            return "warning";
        case VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR:
            return "error";
        default:
            return NULL;
    }
}

bool vm_parser_diagnostic_is_error(const VmParserDiagnostic *diagnostic) {
    return diagnostic != NULL && diagnostic->severity == VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR;
}
