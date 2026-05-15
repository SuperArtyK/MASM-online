/*
 * @file parser.h
 * @brief Parser for MASM-like .data and minimal .code programs through Milestone 23.
 *
 * This module converts the lexer token stream into data symbols, a .data image,
 * and the minimal IR currently supported by the executor. It intentionally
 * remains limited to implemented data declarations, OFFSET, direct symbol
 * memory operands, constant symbol-offset memory operands, signed and unsigned PTR width overrides,
 * register-indirect memory operands, TYPE, LENGTHOF, SIZEOF, packed character
 * literal expressions for mov/add/sub, sign and zero extension
 * instructions, and explicit unsupported-feature
 * diagnostics and safe recovery for recognized MASM textbook constructs, and
 * surfaced lexer diagnostics without collapsing them into umbrella parse errors.
 */

#ifndef MASM32_SIM_PARSER_H
#define MASM32_SIM_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include "lexer.h"
#include "symbols.h"
#include "vm_ir.h"

/// Identifies the final status of one parser attempt.
typedef enum VmParserStatus {
    /// Parsing completed without diagnostics.
    VM_PARSER_STATUS_OK = 0,
    /// Parsing completed or stopped after one or more diagnostics were recorded.
    VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS,
    /// Parsing could not start because required arguments were invalid.
    VM_PARSER_STATUS_INVALID_ARGUMENT,
    /// Parsing stopped because the lexer could not produce a usable token stream.
    VM_PARSER_STATUS_LEXER_FAILED,
    /// Parsing stopped because the caller-provided IR instruction buffer was full.
    VM_PARSER_STATUS_INSTRUCTION_CAPACITY_EXCEEDED,
    /// Parsing stopped because a required diagnostic could not be recorded.
    VM_PARSER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED,
    /// Parsing stopped because instruction source-text storage was full.
    VM_PARSER_STATUS_SOURCE_TEXT_CAPACITY_EXCEEDED,
    /// Parsing stopped because the caller-provided .data image buffer was full.
    VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED,
    /// Parsing stopped because the caller-provided symbol table was full.
    VM_PARSER_STATUS_SYMBOL_CAPACITY_EXCEEDED
} VmParserStatus;

/// Identifies one structured parser diagnostic code for the implemented grammar.
typedef enum VmParserDiagnosticCode {
    /// No diagnostic code; used only as a sentinel.
    VM_PARSER_DIAGNOSTIC_NONE = 0,
    /// The caller supplied invalid parser configuration.
    VM_PARSER_DIAGNOSTIC_INVALID_ARGUMENT,
    /// The lexer reported a diagnostic or failed before parsing could proceed.
    VM_PARSER_DIAGNOSTIC_LEXER_FAILED,
    /// The lexer rejected invalid caller arguments before parsing could proceed.
    VM_PARSER_DIAGNOSTIC_LEXER_INVALID_ARGUMENT,
    /// The lexer token buffer was exhausted before parsing could proceed.
    VM_PARSER_DIAGNOSTIC_LEXER_TOKEN_CAPACITY_EXCEEDED,
    /// The lexer diagnostic buffer was exhausted before parsing could proceed.
    VM_PARSER_DIAGNOSTIC_LEXER_DIAGNOSTIC_CAPACITY_EXCEEDED,
    /// The lexer found a byte outside the supported token set or a malformed numeric suffix.
    VM_PARSER_DIAGNOSTIC_LEXER_UNEXPECTED_CHARACTER,
    /// The lexer found a string literal without a terminating double quote.
    VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_STRING,
    /// The lexer found a character literal without a terminating single quote.
    VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_CHARACTER,
    /// The lexer found a numeric literal that does not fit in uint64_t.
    VM_PARSER_DIAGNOSTIC_LEXER_NUMBER_OVERFLOW,
    /// The lexer found a hexadecimal literal without required hex digits.
    VM_PARSER_DIAGNOSTIC_LEXER_INVALID_HEX_LITERAL,
    /// The source did not contain the required .code directive.
    VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE,
    /// The parser found a directive outside the implemented milestone scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SECTION,
    /// The parser found an instruction mnemonic outside the implemented milestone scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION,
    /// The parser expected an instruction operand.
    VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND,
    /// The parser expected a comma between operands.
    VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA,
    /// The parser expected a line ending or end of file.
    VM_PARSER_DIAGNOSTIC_EXPECTED_LINE_END,
    /// The parser expected a PROC marker after a procedure name.
    VM_PARSER_DIAGNOSTIC_EXPECTED_PROC,
    /// The parser expected an ENDP marker after a procedure name.
    VM_PARSER_DIAGNOSTIC_EXPECTED_ENDP,
    /// The parser expected an END directive with an entry-point name.
    VM_PARSER_DIAGNOSTIC_EXPECTED_END,
    /// The END entry-point name did not match the parsed procedure name.
    VM_PARSER_DIAGNOSTIC_INVALID_END_TARGET,
    /// The caller-provided IR instruction buffer was full.
    VM_PARSER_DIAGNOSTIC_INSTRUCTION_CAPACITY_EXCEEDED,
    /// The caller-provided source-text storage buffer was full.
    VM_PARSER_DIAGNOSTIC_SOURCE_TEXT_CAPACITY_EXCEEDED,
    /// The parser found syntax that is not supported by the implemented milestone scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX,
    /// Recognized operands have widths that cannot be combined by the current instruction.
    VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH,
    /// The parser recognized a MASM textbook construct that is intentionally deferred.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE,
    /// A numeric literal exceeded the 32-bit immediate range supported by the current IR.
    VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE,
    /// A numeric immediate does not fit the destination operand width.
    VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE,
    /// A data symbol name was declared more than once.
    VM_PARSER_DIAGNOSTIC_DUPLICATE_SYMBOL,
    /// A .data declaration was missing a symbol name, type, or initializer.
    VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_DECLARATION,
    /// A .data declaration used a type outside the implemented data subset.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_DATA_TYPE,
    /// A .data initializer was missing or malformed.
    VM_PARSER_DIAGNOSTIC_EXPECTED_DATA_INITIALIZER,
    /// The caller-provided .data image buffer was full.
    VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED,
    /// The caller-provided symbol table buffer was full.
    VM_PARSER_DIAGNOSTIC_SYMBOL_CAPACITY_EXCEEDED,
    /// A symbolic operand or OFFSET expression referenced an unknown symbol.
    VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL,
    /// A constant symbol-offset memory operand resolves outside the referenced symbol.
    VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE,
    /// A PTR width override is recognized but cannot execute in the current MASM32 subset.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH,
    /// A scaled-index addressing form was recognized but is not implemented yet.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX,
    /// A TYPE expression was recognized but is outside the implemented TYPE symbol form.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION,
    /// A LENGTHOF expression was recognized but is outside the implemented LENGTHOF symbol form.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION,
    /// A SIZEOF expression was recognized but is outside the implemented SIZEOF symbol form.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION,
    /// A character literal was used outside a supported byte-compatible context or had unsupported shape.
    VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL,
    /// A memory/immediate instruction form used register-indirect memory without explicit or inferable width.
    VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH,
    /// A bracketed memory operand used a valid register that is not yet supported as an address base.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE,
    /// A DUP initializer was malformed or unsupported.
    VM_PARSER_DIAGNOSTIC_INVALID_DUP,
    /// Number of parser diagnostic codes.
    VM_PARSER_DIAGNOSTIC_CODE_COUNT
} VmParserDiagnosticCode;

/// Describes one parser diagnostic with source context.
typedef struct VmParserDiagnostic {
    /// Diagnostic code.
    VmParserDiagnosticCode code;
    /// Source location associated with the diagnostic.
    VmLexerSourceLocation location;
    /// Pointer into the original source for the associated lexeme when available.
    const char *lexeme;
    /// Length of the associated lexeme in bytes.
    size_t lexeme_length;
    /// Static human-readable diagnostic summary.
    const char *message;
} VmParserDiagnostic;

/// Configures one parse operation and all caller-owned output storage.
typedef struct VmParserConfig {
    /// Null-terminated MASM-like source text to parse.
    const char *source;
    /// Source file name stored in emitted IR metadata; may be NULL.
    const char *source_file;
    /// Caller-owned lexer token buffer used internally by the parser.
    VmLexerToken *tokens;
    /// Number of entries available in @ref tokens.
    size_t token_capacity;
    /// Caller-owned lexer diagnostic buffer used internally by the parser.
    VmLexerDiagnostic *lexer_diagnostics;
    /// Number of entries available in @ref lexer_diagnostics.
    size_t lexer_diagnostic_capacity;
    /// Caller-owned output IR instruction buffer.
    VmIrInstruction *instructions;
    /// Number of entries available in @ref instructions.
    size_t instruction_capacity;
    /// Caller-owned storage for null-terminated instruction source text copies.
    char *source_text_storage;
    /// Number of bytes available in @ref source_text_storage.
    size_t source_text_capacity;
    /// Caller-owned output data-symbol table. May be NULL only when symbol capacity is zero.
    VmSymbol *symbols;
    /// Number of entries available in @ref symbols.
    size_t symbol_capacity;
    /// Caller-owned .data image bytes laid out from VM_MEMORY_DEFAULT_DATA_BASE.
    uint8_t *data_image;
    /// Number of bytes available in @ref data_image.
    size_t data_image_capacity;
    /// Caller-owned parser diagnostic output buffer.
    VmParserDiagnostic *diagnostics;
    /// Number of entries available in @ref diagnostics; this also bounds recovery diagnostics.
    size_t diagnostic_capacity;
} VmParserConfig;

/// Summarizes one parse attempt.
typedef struct VmParserResult {
    /// Final parser status.
    VmParserStatus status;
    /// Number of IR instructions written to the configured instruction buffer.
    size_t instruction_count;
    /// Number of parser diagnostics written.
    size_t diagnostic_count;
    /// Number of lexer tokens produced before parsing.
    size_t token_count;
    /// Number of lexer diagnostics produced before parsing.
    size_t lexer_diagnostic_count;
    /// Number of data symbols written to the configured symbol buffer.
    size_t symbol_count;
    /// Number of initialized bytes written to the configured .data image buffer.
    size_t data_size;
} VmParserResult;

/// Parses a MASM-like source file into implemented data layout and executable IR.
///
/// The parser accepts optional .data declarations before .code, emits data-symbol
/// metadata and a deterministic .data image, then parses the existing minimal
/// .code grammar. Source operands may use registers, immediates, direct symbols,
/// `OFFSET symbol`, `TYPE symbol`, `LENGTHOF symbol`, `SIZEOF symbol`, character literals, constant symbol-offset memory operands, register-indirect memory operands, or signed/unsigned PTR width
/// overrides on supported memory operands; destination operands may use
/// registers, direct symbols, constant symbol-offset memory operands, register-indirect memory operands, or signed/unsigned PTR
/// width overrides on supported memory operands.
///
/// @param config Parse configuration and caller-owned output buffers.
/// @param out_result Receives parse counts and final status.
/// @return Final parser status.
VmParserStatus vm_parser_parse_program(const VmParserConfig *config, VmParserResult *out_result);

/// Returns a stable display name for a parser status.
///
/// @param status Parser status to inspect.
/// @return Static status name, or NULL for invalid values.
const char *vm_parser_status_name(VmParserStatus status);

/// Returns a stable display name for a parser diagnostic code.
///
/// @param code Parser diagnostic code to inspect.
/// @return Static diagnostic code name, or NULL for invalid values.
const char *vm_parser_diagnostic_code_name(VmParserDiagnosticCode code);

#endif
