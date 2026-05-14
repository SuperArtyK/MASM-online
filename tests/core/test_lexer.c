/*
 * @file test_lexer.c
 * @brief Unit tests for the MASM-like lexer.
 *
 * These tests cover tokenization success cases, source positions, diagnostics,
 * edge cases, and buffer-capacity behavior without introducing parser or IR
 * generation behavior.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/parser/lexer.h"

/// Records a lexer test failure.
///
/// @param message Human-readable failure description.
/// @return Always returns one failure.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that two size values are equal.
///
/// @param actual Actual value produced by the test.
/// @param expected Expected value.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_size(size_t actual, size_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%lu expected=%lu)\n", message, (unsigned long)actual, (unsigned long)expected);
        return 1;
    }

    return 0;
}

/// Verifies that two unsigned 32-bit values are equal.
///
/// @param actual Actual value produced by the test.
/// @param expected Expected value.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%u expected=%u)\n", message, actual, expected);
        return 1;
    }

    return 0;
}

/// Verifies that two unsigned 64-bit values are equal.
///
/// @param actual Actual value produced by the test.
/// @param expected Expected value.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_u64(uint64_t actual, uint64_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }

    return 0;
}

/// Verifies that a lexer status matches an expected status.
///
/// @param actual Actual status produced by the lexer.
/// @param expected Expected status.
/// @param message Failure message when statuses differ.
/// @return Zero on success, otherwise one failure.
static int expect_status(VmLexerStatus actual, VmLexerStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_lexer_status_name(actual), vm_lexer_status_name(expected));
        return 1;
    }

    return 0;
}

/// Verifies that a token kind matches an expected token kind.
///
/// @param actual Actual token kind.
/// @param expected Expected token kind.
/// @param message Failure message when token kinds differ.
/// @return Zero on success, otherwise one failure.
static int expect_token_kind(VmLexerTokenKind actual, VmLexerTokenKind expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_lexer_token_kind_name(actual), vm_lexer_token_kind_name(expected));
        return 1;
    }

    return 0;
}

/// Verifies that a diagnostic code matches an expected code.
///
/// @param actual Actual diagnostic code.
/// @param expected Expected diagnostic code.
/// @param message Failure message when codes differ.
/// @return Zero on success, otherwise one failure.
static int expect_diagnostic_code(VmLexerDiagnosticCode actual, VmLexerDiagnosticCode expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_lexer_diagnostic_code_name(actual), vm_lexer_diagnostic_code_name(expected));
        return 1;
    }

    return 0;
}

/// Verifies that a token lexeme matches an expected string.
///
/// @param token Token to inspect.
/// @param expected Expected lexeme text.
/// @param message Failure message when text differs.
/// @return Zero on success, otherwise one failure.
static int expect_lexeme(const VmLexerToken *token, const char *expected, const char *message) {
    size_t expected_length = strlen(expected);

    if (token == NULL) {
        return record_failure("token pointer should not be NULL");
    }
    if (token->lexeme_length != expected_length || strncmp(token->lexeme, expected, expected_length) != 0) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }

    return 0;
}

/// Tokenizes source into fixed test buffers.
///
/// @param source Source text to tokenize.
/// @param tokens Token buffer to fill.
/// @param token_capacity Token buffer capacity.
/// @param diagnostics Diagnostic buffer to fill.
/// @param diagnostic_capacity Diagnostic buffer capacity.
/// @param out_result Receives lexer result metadata.
/// @return Final lexer status.
static VmLexerStatus tokenize_for_test(
    const char *source,
    VmLexerToken *tokens,
    size_t token_capacity,
    VmLexerDiagnostic *diagnostics,
    size_t diagnostic_capacity,
    VmLexerResult *out_result
) {
    return vm_lexer_tokenize(source, tokens, token_capacity, diagnostics, diagnostic_capacity, out_result);
}

/// Verifies the guide.s representative lexer source snippet.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_guide_snippet_tokenizes(void) {
    int failures = 0;
    const char *source = ".data\nmsg BYTE \"Hello\", 0\n.code\nmov eax, 20 ; comment\n";
    VmLexerToken tokens[32];
    VmLexerDiagnostic diagnostics[8];
    VmLexerResult result;
    VmLexerStatus status = tokenize_for_test(source, tokens, 32U, diagnostics, 8U, &result);

    failures += expect_status(status, VM_LEXER_STATUS_OK, "guide snippet should tokenize without diagnostics");
    failures += expect_size(result.diagnostic_count, 0U, "guide snippet should not produce diagnostics");
    failures += expect_token_kind(tokens[0].kind, VM_LEXER_TOKEN_DIRECTIVE, ".data should be a directive");
    failures += expect_lexeme(&tokens[0], ".data", ".data lexeme should be preserved");
    failures += expect_token_kind(tokens[1].kind, VM_LEXER_TOKEN_NEWLINE, "newline should be preserved after .data");
    failures += expect_token_kind(tokens[2].kind, VM_LEXER_TOKEN_IDENTIFIER, "msg should be an identifier");
    failures += expect_lexeme(&tokens[2], "msg", "msg lexeme should be preserved");
    failures += expect_token_kind(tokens[3].kind, VM_LEXER_TOKEN_IDENTIFIER, "BYTE should be an identifier token");
    failures += expect_token_kind(tokens[4].kind, VM_LEXER_TOKEN_STRING, "string literal should be tokenized");
    failures += expect_lexeme(&tokens[4], "\"Hello\"", "string lexeme should include quotes");
    failures += expect_token_kind(tokens[5].kind, VM_LEXER_TOKEN_COMMA, "comma should be tokenized");
    failures += expect_token_kind(tokens[6].kind, VM_LEXER_TOKEN_NUMBER, "0 should be a number");
    failures += expect_u64(tokens[6].number_value, 0U, "0 should parse as zero");
    failures += expect_token_kind(tokens[8].kind, VM_LEXER_TOKEN_DIRECTIVE, ".code should be a directive");
    failures += expect_token_kind(tokens[10].kind, VM_LEXER_TOKEN_IDENTIFIER, "mov should be an identifier token");
    failures += expect_token_kind(tokens[11].kind, VM_LEXER_TOKEN_REGISTER, "eax should be a register token");
    if (tokens[11].register_id != VM_REGISTER_EAX) {
        failures += record_failure("eax register token should identify VM_REGISTER_EAX");
    }
    failures += expect_token_kind(tokens[13].kind, VM_LEXER_TOKEN_NUMBER, "20 should be a number");
    failures += expect_u64(tokens[13].number_value, 20U, "20 should parse as decimal 20");
    failures += expect_token_kind(tokens[result.token_count - 1U].kind, VM_LEXER_TOKEN_EOF, "last token should be EOF");

    return failures;
}

/// Verifies source line and column tracking across newlines and comments.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_source_positions_and_comments(void) {
    int failures = 0;
    const char *source = "; first comment\r\nlabel: mov ebx, 2\n";
    VmLexerToken tokens[16];
    VmLexerDiagnostic diagnostics[4];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test(source, tokens, 16U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "comments and CRLF should tokenize cleanly");
    failures += expect_token_kind(tokens[0].kind, VM_LEXER_TOKEN_NEWLINE, "comment-only line should preserve newline");
    failures += expect_size(tokens[0].lexeme_length, 2U, "CRLF should be one newline token with length 2");
    failures += expect_token_kind(tokens[1].kind, VM_LEXER_TOKEN_IDENTIFIER, "label should be identifier after comment newline");
    failures += expect_lexeme(&tokens[1], "label", "label lexeme should be preserved");
    failures += expect_u32(tokens[1].location.line, 2U, "label should start on line 2");
    failures += expect_u32(tokens[1].location.column, 1U, "label should start in column 1");
    failures += expect_token_kind(tokens[2].kind, VM_LEXER_TOKEN_COLON, "colon after label should tokenize");
    failures += expect_u32(tokens[2].location.column, 6U, "colon should be in column 6");
    failures += expect_token_kind(tokens[4].kind, VM_LEXER_TOKEN_REGISTER, "ebx should be register token");
    if (tokens[4].register_id != VM_REGISTER_EBX) {
        failures += record_failure("ebx register token should identify VM_REGISTER_EBX");
    }
    failures += expect_u32(tokens[4].location.column, 12U, "ebx should have expected column");

    return failures;
}

/// Verifies decimal, 0x-prefixed hex, and h-suffixed MASM hex literals.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_number_forms(void) {
    int failures = 0;
    const char *source = "42 0x2A 2Ah 0FFFFFFFFh";
    VmLexerToken tokens[8];
    VmLexerDiagnostic diagnostics[4];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test(source, tokens, 8U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "number forms should tokenize cleanly");
    failures += expect_u64(tokens[0].number_value, 42U, "decimal 42 should parse correctly");
    failures += expect_u32(tokens[0].number_base, 10U, "decimal token should have base 10");
    failures += expect_u64(tokens[1].number_value, 42U, "0x2A should parse correctly");
    failures += expect_u32(tokens[1].number_base, 16U, "0x token should have base 16");
    failures += expect_u64(tokens[2].number_value, 42U, "2Ah should parse correctly");
    failures += expect_u64(tokens[3].number_value, 0xFFFFFFFFULL, "0FFFFFFFFh should parse correctly");
    failures += expect_u32(tokens[3].number_base, 16U, "h-suffixed token should have base 16");

    return failures;
}

/// Verifies signed decimal and hexadecimal number literals.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_signed_number_forms(void) {
    int failures = 0;
    const char *source = "-42 -0x2A -2Ah";
    VmLexerToken tokens[8];
    VmLexerDiagnostic diagnostics[4];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test(source, tokens, 8U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "signed number forms should tokenize cleanly");
    failures += expect_token_kind(tokens[0].kind, VM_LEXER_TOKEN_NUMBER, "-42 should be a number");
    failures += expect_u64(tokens[0].number_value, 42U, "-42 should preserve magnitude 42");
    failures += expect_u32(tokens[0].number_base, 10U, "-42 should have decimal base");
    if (!tokens[0].number_is_negative) {
        failures += record_failure("-42 should be marked negative");
    }
    failures += expect_u64(tokens[1].number_value, 42U, "-0x2A should preserve magnitude 42");
    failures += expect_u32(tokens[1].number_base, 16U, "-0x2A should have hex base");
    if (!tokens[1].number_is_negative) {
        failures += record_failure("-0x2A should be marked negative");
    }
    failures += expect_u64(tokens[2].number_value, 42U, "-2Ah should preserve magnitude 42");
    failures += expect_u32(tokens[2].number_base, 16U, "-2Ah should have hex base");
    if (!tokens[2].number_is_negative) {
        failures += record_failure("-2Ah should be marked negative");
    }

    return failures;
}

/// Verifies square brackets and comma tokens used by later memory operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_brackets_and_commas(void) {
    int failures = 0;
    const char *source = "mov eax, [ebx]";
    VmLexerToken tokens[12];
    VmLexerDiagnostic diagnostics[4];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test(source, tokens, 12U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "bracketed source should tokenize cleanly");
    failures += expect_token_kind(tokens[2].kind, VM_LEXER_TOKEN_COMMA, "comma after eax should tokenize");
    failures += expect_token_kind(tokens[3].kind, VM_LEXER_TOKEN_LEFT_BRACKET, "left bracket should tokenize");
    failures += expect_token_kind(tokens[4].kind, VM_LEXER_TOKEN_REGISTER, "ebx inside brackets should be register");
    failures += expect_token_kind(tokens[5].kind, VM_LEXER_TOKEN_RIGHT_BRACKET, "right bracket should tokenize");

    return failures;
}


/// Verifies plus and standalone minus tokens used by constant symbol offsets.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_symbol_offset_operator_tokens(void) {
    int failures = 0;
    const char *source = "[nums + 8] nums-label [esi * 4]";
    VmLexerToken tokens[16];
    VmLexerDiagnostic diagnostics[4];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test(source, tokens, 16U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "symbol offset operators should tokenize cleanly");
    failures += expect_token_kind(tokens[2].kind, VM_LEXER_TOKEN_PLUS, "+ should tokenize as PLUS");
    failures += expect_token_kind(tokens[3].kind, VM_LEXER_TOKEN_NUMBER, "8 should tokenize as a number after +");
    failures += expect_token_kind(tokens[5].kind, VM_LEXER_TOKEN_IDENTIFIER, "nums should tokenize before standalone minus");
    failures += expect_token_kind(tokens[6].kind, VM_LEXER_TOKEN_MINUS, "standalone - should tokenize as MINUS");
    failures += expect_token_kind(tokens[7].kind, VM_LEXER_TOKEN_IDENTIFIER, "label should tokenize after standalone minus");
    failures += expect_token_kind(tokens[10].kind, VM_LEXER_TOKEN_ASTERISK, "* should tokenize as ASTERISK for scaled-index diagnostics");

    return failures;
}

/// Verifies empty and whitespace-only source behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_empty_and_whitespace_sources(void) {
    int failures = 0;
    VmLexerToken tokens[4];
    VmLexerDiagnostic diagnostics[4];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test("", tokens, 4U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "empty source should tokenize cleanly");
    failures += expect_size(result.token_count, 1U, "empty source should only emit EOF");
    failures += expect_token_kind(tokens[0].kind, VM_LEXER_TOKEN_EOF, "empty source token should be EOF");

    failures += expect_status(tokenize_for_test(" \t\n", tokens, 4U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "whitespace source should tokenize cleanly");
    failures += expect_size(result.token_count, 2U, "whitespace with newline should emit newline and EOF");
    failures += expect_token_kind(tokens[0].kind, VM_LEXER_TOKEN_NEWLINE, "whitespace newline should be preserved");

    return failures;
}

/// Verifies string tokenization, including escaped quote/backslash scanning.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_string_literals(void) {
    int failures = 0;
    const char *source = "\"Hello\" \"A\\\"B\" \"C\\\\D\"";
    VmLexerToken tokens[8];
    VmLexerDiagnostic diagnostics[4];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test(source, tokens, 8U, diagnostics, 4U, &result), VM_LEXER_STATUS_OK, "strings should tokenize cleanly");
    failures += expect_lexeme(&tokens[0], "\"Hello\"", "simple string should preserve lexeme");
    failures += expect_lexeme(&tokens[1], "\"A\\\"B\"", "escaped quote should not terminate the string early");
    failures += expect_lexeme(&tokens[2], "\"C\\\\D\"", "escaped backslash string should preserve lexeme");

    return failures;
}

/// Verifies diagnostics for malformed source that can still produce tokens.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_error_diagnostics(void) {
    int failures = 0;
    VmLexerToken tokens[16];
    VmLexerDiagnostic diagnostics[8];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test("mov @", tokens, 16U, diagnostics, 8U, &result), VM_LEXER_STATUS_OK, "@ is a valid identifier start in this lexer");

    failures += expect_status(tokenize_for_test("mov #", tokens, 16U, diagnostics, 8U, &result), VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS, "unexpected character should produce diagnostics");
    failures += expect_size(result.diagnostic_count, 1U, "unexpected character should record one diagnostic");
    failures += expect_diagnostic_code(diagnostics[0].code, VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER, "unexpected character diagnostic code should match");
    failures += expect_u32(diagnostics[0].location.column, 5U, "unexpected character diagnostic should preserve column");

    failures += expect_status(tokenize_for_test("\"unterminated", tokens, 16U, diagnostics, 8U, &result), VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS, "unterminated string should produce diagnostic");
    failures += expect_diagnostic_code(diagnostics[0].code, VM_LEXER_DIAGNOSTIC_UNTERMINATED_STRING, "unterminated string diagnostic code should match");

    failures += expect_status(tokenize_for_test("0x ", tokens, 16U, diagnostics, 8U, &result), VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS, "invalid hex should produce diagnostic");
    failures += expect_diagnostic_code(diagnostics[0].code, VM_LEXER_DIAGNOSTIC_INVALID_HEX_LITERAL, "invalid hex diagnostic code should match");

    failures += expect_status(tokenize_for_test("123abc", tokens, 16U, diagnostics, 8U, &result), VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS, "invalid decimal suffix should produce diagnostic");
    failures += expect_diagnostic_code(diagnostics[0].code, VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER, "invalid decimal suffix diagnostic code should match");
    failures += expect_u64(tokens[0].number_value, 123U, "invalid decimal suffix should preserve leading numeric value only");

    failures += expect_status(tokenize_for_test("184467440737095516160", tokens, 16U, diagnostics, 8U, &result), VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS, "overflowing number should produce diagnostic");
    failures += expect_diagnostic_code(diagnostics[0].code, VM_LEXER_DIAGNOSTIC_NUMBER_OVERFLOW, "overflow diagnostic code should match");

    return failures;
}

/// Verifies token and diagnostic buffer capacity handling.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_capacity_and_invalid_arguments(void) {
    int failures = 0;
    VmLexerToken tokens[2];
    VmLexerDiagnostic diagnostics[2];
    VmLexerResult result;

    failures += expect_status(tokenize_for_test(NULL, tokens, 2U, diagnostics, 2U, &result), VM_LEXER_STATUS_INVALID_ARGUMENT, "NULL source should be rejected");
    failures += expect_status(tokenize_for_test("eax", NULL, 1U, diagnostics, 2U, &result), VM_LEXER_STATUS_INVALID_ARGUMENT, "NULL token buffer with nonzero capacity should be rejected");
    failures += expect_status(tokenize_for_test("eax", tokens, 2U, NULL, 1U, &result), VM_LEXER_STATUS_INVALID_ARGUMENT, "NULL diagnostic buffer with nonzero capacity should be rejected");
    failures += expect_status(vm_lexer_tokenize("eax", tokens, 2U, diagnostics, 2U, NULL), VM_LEXER_STATUS_INVALID_ARGUMENT, "NULL result pointer should be rejected");

    failures += expect_status(tokenize_for_test("eax ebx", tokens, 2U, diagnostics, 2U, &result), VM_LEXER_STATUS_TOKEN_CAPACITY_EXCEEDED, "small token buffer should stop tokenization");
    failures += expect_size(result.token_count, 2U, "small token buffer should fill two tokens");
    failures += expect_diagnostic_code(diagnostics[0].code, VM_LEXER_DIAGNOSTIC_TOKEN_CAPACITY_EXCEEDED, "token capacity diagnostic should be recorded");

    failures += expect_status(tokenize_for_test("#", tokens, 2U, NULL, 0U, &result), VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED, "diagnostic overflow should be reported");

    return failures;
}

/// Verifies token and diagnostic metadata name helpers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_metadata_helpers(void) {
    int failures = 0;

    if (strcmp(vm_lexer_token_kind_name(VM_LEXER_TOKEN_REGISTER), "REGISTER") != 0) {
        failures += record_failure("token kind name helper should name REGISTER");
    }
    if (strcmp(vm_lexer_diagnostic_code_name(VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER), "unexpected-character") != 0) {
        failures += record_failure("diagnostic code name helper should name unexpected-character");
    }
    if (strcmp(vm_lexer_status_name(VM_LEXER_STATUS_OK), "ok") != 0) {
        failures += record_failure("status name helper should name ok");
    }
    if (vm_lexer_token_kind_name((VmLexerTokenKind)999) != NULL) {
        failures += record_failure("invalid token kind name should be NULL");
    }
    if (vm_lexer_diagnostic_code_name((VmLexerDiagnosticCode)999) != NULL) {
        failures += record_failure("invalid diagnostic code name should be NULL");
    }
    if (vm_lexer_status_name((VmLexerStatus)999) != NULL) {
        failures += record_failure("invalid status name should be NULL");
    }

    return failures;
}

/// Runs all lexer tests.
///
/// @return Zero on success, otherwise one.
int main(void) {
    int failures = 0;

    failures += test_guide_snippet_tokenizes();
    failures += test_source_positions_and_comments();
    failures += test_number_forms();
    failures += test_signed_number_forms();
    failures += test_brackets_and_commas();
    failures += test_symbol_offset_operator_tokens();
    failures += test_empty_and_whitespace_sources();
    failures += test_string_literals();
    failures += test_error_diagnostics();
    failures += test_capacity_and_invalid_arguments();
    failures += test_metadata_helpers();

    if (failures != 0) {
        fprintf(stderr, "Lexer tests failed: %d\n", failures);
        return 1;
    }

    printf("Lexer tests passed.\n");
    return 0;
}
