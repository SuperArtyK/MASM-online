/*
 * @file lexer.h
 * @brief Tokenizer for the MASM-like source lexer.
 *
 * This module converts a source buffer into a flat token stream while
 * preserving source locations. It intentionally performs lexical analysis
 * only; parsing, symbol resolution, IR generation, and execution remain
 * owned by later pipeline modules.
 */

#ifndef MASM32_SIM_LEXER_H
#define MASM32_SIM_LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm_cpu.h"

/// Identifies one token emitted by the MASM-like lexer.
typedef enum VmLexerTokenKind {
    /// End-of-file marker inserted after the final source token.
    VM_LEXER_TOKEN_EOF = 0,
    /// Source line ending. CRLF is reported as one newline token.
    VM_LEXER_TOKEN_NEWLINE,
    /// MASM-like identifier that is not recognized as a register.
    VM_LEXER_TOKEN_IDENTIFIER,
    /// Dot-prefixed directive such as .data or .code.
    VM_LEXER_TOKEN_DIRECTIVE,
    /// Recognized MASM32-mode register or register alias.
    VM_LEXER_TOKEN_REGISTER,
    /// Decimal or hexadecimal integer literal.
    VM_LEXER_TOKEN_NUMBER,
    /// Double-quoted string literal.
    VM_LEXER_TOKEN_STRING,
    /// Comma separator.
    VM_LEXER_TOKEN_COMMA,
    /// Left square bracket.
    VM_LEXER_TOKEN_LEFT_BRACKET,
    /// Right square bracket.
    VM_LEXER_TOKEN_RIGHT_BRACKET,
    /// Left parenthesis used by DUP initializers.
    VM_LEXER_TOKEN_LEFT_PAREN,
    /// Right parenthesis used by DUP initializers.
    VM_LEXER_TOKEN_RIGHT_PAREN,
    /// Question mark used by uninitialized data declarations.
    VM_LEXER_TOKEN_QUESTION,
    /// Plus sign used by constant symbol-offset memory operands.
    VM_LEXER_TOKEN_PLUS,
    /// Minus sign used by constant symbol-offset memory operands when not part of a signed number.
    VM_LEXER_TOKEN_MINUS,
    /// Colon used by labels.
    VM_LEXER_TOKEN_COLON,
    /// Number of lexer token kinds.
    VM_LEXER_TOKEN_KIND_COUNT
} VmLexerTokenKind;

/// Identifies one structured lexer diagnostic code.
typedef enum VmLexerDiagnosticCode {
    /// No diagnostic code; used only as a sentinel.
    VM_LEXER_DIAGNOSTIC_NONE = 0,
    /// The caller supplied invalid pointers or buffer metadata.
    VM_LEXER_DIAGNOSTIC_INVALID_ARGUMENT,
    /// The caller-provided token buffer could not hold another token.
    VM_LEXER_DIAGNOSTIC_TOKEN_CAPACITY_EXCEEDED,
    /// The caller-provided diagnostic buffer could not hold another diagnostic.
    VM_LEXER_DIAGNOSTIC_DIAGNOSTIC_CAPACITY_EXCEEDED,
    /// The lexer found a byte that is not part of the supported lexer token set.
    VM_LEXER_DIAGNOSTIC_UNEXPECTED_CHARACTER,
    /// A string literal reached end-of-file before a closing quote.
    VM_LEXER_DIAGNOSTIC_UNTERMINATED_STRING,
    /// A numeric literal could not be represented as a 64-bit unsigned value.
    VM_LEXER_DIAGNOSTIC_NUMBER_OVERFLOW,
    /// A hexadecimal literal was missing hexadecimal digits.
    VM_LEXER_DIAGNOSTIC_INVALID_HEX_LITERAL,
    /// Number of lexer diagnostic codes.
    VM_LEXER_DIAGNOSTIC_CODE_COUNT
} VmLexerDiagnosticCode;

/// Describes the final status of one tokenization attempt.
typedef enum VmLexerStatus {
    /// Tokenization completed without diagnostics.
    VM_LEXER_STATUS_OK = 0,
    /// Tokenization completed and one or more diagnostics were recorded.
    VM_LEXER_STATUS_OK_WITH_DIAGNOSTICS,
    /// Tokenization could not start because arguments were invalid.
    VM_LEXER_STATUS_INVALID_ARGUMENT,
    /// Tokenization stopped because the token buffer was full.
    VM_LEXER_STATUS_TOKEN_CAPACITY_EXCEEDED,
    /// Tokenization stopped because a required diagnostic could not be recorded.
    VM_LEXER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED
} VmLexerStatus;

/// Identifies a one-based source position and zero-based byte offset.
typedef struct VmLexerSourceLocation {
    /// One-based line number.
    uint32_t line;
    /// One-based column number measured in source bytes.
    uint32_t column;
    /// Zero-based byte offset from the start of the source buffer.
    size_t offset;
} VmLexerSourceLocation;

/// Describes one token emitted by the lexer.
typedef struct VmLexerToken {
    /// Token kind.
    VmLexerTokenKind kind;
    /// Source location of the first token byte.
    VmLexerSourceLocation location;
    /// Pointer into the original source buffer at the first token byte.
    const char *lexeme;
    /// Token length in source bytes.
    size_t lexeme_length;
    /// Parsed integer magnitude when @ref kind is VM_LEXER_TOKEN_NUMBER.
    uint64_t number_value;
    /// Numeric base used by a number token, normally 10 or 16.
    uint32_t number_base;
    /// Whether the number token had a leading minus sign.
    bool number_is_negative;
    /// Register identifier when @ref kind is VM_LEXER_TOKEN_REGISTER.
    VmRegister register_id;
} VmLexerToken;

/// Describes one lexer diagnostic with source context.
typedef struct VmLexerDiagnostic {
    /// Diagnostic code.
    VmLexerDiagnosticCode code;
    /// Source location associated with the diagnostic.
    VmLexerSourceLocation location;
    /// Pointer into the source buffer for the offending text when available.
    const char *lexeme;
    /// Length of the offending source text in bytes.
    size_t lexeme_length;
    /// Static human-readable summary for test and diagnostic use.
    const char *message;
} VmLexerDiagnostic;

/// Summarizes tokenization output counts and status.
typedef struct VmLexerResult {
    /// Final lexer status.
    VmLexerStatus status;
    /// Number of tokens written to the token buffer.
    size_t token_count;
    /// Number of diagnostics written to the diagnostic buffer.
    size_t diagnostic_count;
} VmLexerResult;

/// Tokenizes a MASM-like source buffer into caller-provided output buffers.
///
/// The lexer writes an EOF token on successful completion when token capacity
/// permits it. Comments beginning with semicolon are skipped, while newline
/// tokens are preserved for later parser phases.
///
/// @param source Null-terminated source text to tokenize.
/// @param tokens Token output buffer, or NULL only when @p token_capacity is zero.
/// @param token_capacity Number of token entries available in @p tokens.
/// @param diagnostics Diagnostic output buffer, or NULL only when @p diagnostic_capacity is zero.
/// @param diagnostic_capacity Number of diagnostic entries available in @p diagnostics.
/// @param out_result Receives tokenization counts and final status.
/// @return Final lexer status.
VmLexerStatus vm_lexer_tokenize(
    const char *source,
    VmLexerToken *tokens,
    size_t token_capacity,
    VmLexerDiagnostic *diagnostics,
    size_t diagnostic_capacity,
    VmLexerResult *out_result
);

/// Returns a stable uppercase display name for a lexer token kind.
///
/// @param kind Token kind to inspect.
/// @return Static token kind name, or NULL for invalid values.
const char *vm_lexer_token_kind_name(VmLexerTokenKind kind);

/// Returns a stable display name for a lexer diagnostic code.
///
/// @param code Diagnostic code to inspect.
/// @return Static diagnostic code name, or NULL for invalid values.
const char *vm_lexer_diagnostic_code_name(VmLexerDiagnosticCode code);

/// Returns a stable display name for a lexer status.
///
/// @param status Lexer status to inspect.
/// @return Static status name, or NULL for invalid values.
const char *vm_lexer_status_name(VmLexerStatus status);

#endif
