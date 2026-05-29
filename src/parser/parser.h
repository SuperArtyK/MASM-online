/*
 * @file parser.h
 * @brief Parser API for the currently implemented MASM32 educational subset.
 *
 * This module converts the lexer token stream into data symbols, a .data image,
 * and the minimal IR currently supported by the executor. It intentionally
 * remains limited to implemented writable, uninitialized, and constant data
 * declarations, numeric equates, extended constant expressions, nested DUP
 * initializers, OFFSET, direct symbol memory operands, constant symbol-offset
 * memory operands, signed and unsigned PTR width overrides, register-indirect
 * memory operands, TYPE, LENGTHOF, SIZEOF, packed character literals,
 * implemented instruction groups, explicit unsupported-feature diagnostics,
 * safe recovery for recognized MASM textbook constructs, specific surfaced
 * lexer diagnostics, and virtual Irvine32 registry metadata.
 */

#ifndef MASM32_SIM_PARSER_H
#define MASM32_SIM_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lexer.h"
#include "symbols.h"
#include "vm_ir.h"
#include "../core/vm_diagnostic_policy.h"

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
    /// The parser found a recognized instruction using a deferred operand form.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION_FORM,
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
    /// An instruction operand used a size that the instruction does not accept.
    VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE,
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
    /// A statically known instruction destination attempts to write read-only `.CONST` storage.
    VM_PARSER_DIAGNOSTIC_CONST_WRITE,
    /// An accepted `.CONST ?` or `.CONST DUP(?)` declaration reserves uninitialized read-only storage.
    VM_PARSER_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE,
    /// A .model directive used a form outside `.model flat, stdcall`.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MODEL,
    /// An INCLUDE directive requested a file outside the simulator's virtual built-ins.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDE,
    /// An OPTION directive used a form outside the accepted compatibility subset.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION,
    /// A bracketed memory operand used a valid register that is not yet supported as an address base.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE,
    /// A DUP initializer was malformed or unsupported.
    VM_PARSER_DIAGNOSTIC_INVALID_DUP,
    /// A numeric equate declaration was malformed or duplicated.
    VM_PARSER_DIAGNOSTIC_INVALID_EQUATE,
    /// A constant expression referenced an unknown equate symbol.
    VM_PARSER_DIAGNOSTIC_UNKNOWN_EQUATE,
    /// A numeric equate references itself while being evaluated.
    VM_PARSER_DIAGNOSTIC_RECURSIVE_EQUATE,
    /// A constant expression used syntax outside the implemented constant-expression subset.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION,
    /// A case-insensitive user-symbol lookup matched more than one exact-case declaration.
    VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL,
    /// A supported OPTION CASEMAP directive changed the active user-symbol policy.
    VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED,
    /// An OPTION CASEMAP directive used an invalid value.
    VM_PARSER_DIAGNOSTIC_INVALID_OPTION_VALUE,
    /// A mnemonic is unknown in the current include/context.
    VM_PARSER_DIAGNOSTIC_UNKNOWN_INSTRUCTION,
    /// An instruction effective-address expression was malformed or unsupported.
    VM_PARSER_DIAGNOSTIC_INVALID_EFFECTIVE_ADDRESS_EXPRESSION,
    /// An instruction used operands outside the valid shape for that instruction.
    VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS,
    /// A MASM/object/linker segment or group name was used as an addressable symbol or segment definition.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SEGMENT_SYMBOL,
    /// A recognized Irvine32 virtual symbol was used before its routine implementation exists.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE,
    /// An accepted compatibility construct has no runtime effect in the simulator.
    VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP,
    /// An accepted compatibility construct records metadata without full runtime behavior.
    VM_PARSER_DIAGNOSTIC_COMPATIBILITY_METADATA_ONLY,
    /// An accepted compatibility construct provides limited virtual behavior only.
    VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED,
    /// Number of parser diagnostic codes.
    VM_PARSER_DIAGNOSTIC_CODE_COUNT
} VmParserDiagnosticCode;


/// Classifies one known or unknown Irvine32 virtual symbol name.
typedef enum VmIrvine32SymbolClass {
    /// The name is not part of the simulator's known Irvine32 registry.
    VM_IRVINE32_SYMBOL_CLASS_UNKNOWN = 0,
    /// The name is registered as a virtual intrinsic symbol available to current metadata consumers.
    VM_IRVINE32_SYMBOL_CLASS_SUPPORTED_VIRTUAL_INTRINSIC,
    /// The name is recognized as a planned Irvine32 routine for a later milestone.
    VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE,
    /// The name is recognized but explicitly unsupported in the v1 simulator target.
    VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE,
    /// The name represents Windows API, external-linkage, or host-environment behavior.
    VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL
} VmIrvine32SymbolClass;

/// Identifies whether a parser diagnostic blocks execution.
typedef enum VmParserDiagnosticSeverity {
    /// Informational notice diagnostic; execution may continue if no errors exist.
    VM_PARSER_DIAGNOSTIC_SEVERITY_NOTICE = 0,
    /// Warning diagnostic; execution may continue if no errors exist.
    VM_PARSER_DIAGNOSTIC_SEVERITY_WARNING,
    /// Error diagnostic; execution must not start.
    VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR
} VmParserDiagnosticSeverity;

/// Number of bytes reserved inside each diagnostic for formatted parser messages.
#define VM_PARSER_DIAGNOSTIC_MESSAGE_CAPACITY 256U

/// Describes one parser diagnostic with source context.
typedef struct VmParserDiagnostic {
    /// Diagnostic code.
    VmParserDiagnosticCode code;
    /// Diagnostic severity used to decide whether execution may continue.
    VmParserDiagnosticSeverity severity;
    /// Source location associated with the diagnostic.
    VmLexerSourceLocation location;
    /// Pointer into the original source for the associated lexeme when available.
    const char *lexeme;
    /// Length of the associated lexeme in bytes.
    size_t lexeme_length;
    /// Human-readable diagnostic summary. Points to static storage or @ref message_storage.
    const char *message;
    /// Caller-owned storage used when a diagnostic needs parse-specific values.
    char message_storage[VM_PARSER_DIAGNOSTIC_MESSAGE_CAPACITY];
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
    /// Caller-owned .data/.DATA? image bytes laid out from VM_MEMORY_DEFAULT_DATA_BASE.
    uint8_t *data_image;
    /// Number of bytes available in @ref data_image.
    size_t data_image_capacity;
    /// Optional caller-owned per-byte initialization mask for .data/.DATA? bytes.
    ///
    /// A value of 1 means the byte is initialized by an explicit initializer.
    /// A value of 0 means the byte originated from `?` or `.DATA?` storage and
    /// remains deterministic zero-filled until a successful program write marks
    /// it initialized. NULL disables Phase 39 metadata emission.
    uint8_t *data_initialized_mask;
    /// Number of bytes available in @ref data_initialized_mask.
    size_t data_initialized_mask_capacity;
    /// Optional caller-owned per-byte initialization mask for .CONST bytes.
    ///
    /// A value of 1 means the byte is initialized by an explicit initializer.
    /// A value of 0 means the byte originated from Phase 57I accepted `.CONST ?`
    /// or `.CONST DUP(?)` storage. NULL disables `.CONST` uninitialized-origin
    /// metadata emission while still allowing deterministic visible bytes.
    uint8_t *const_initialized_mask;
    /// Number of bytes available in @ref const_initialized_mask.
    size_t const_initialized_mask_capacity;
    /// Caller-owned .CONST image bytes laid out from VM_MEMORY_DEFAULT_CONST_BASE.
    uint8_t *const_image;
    /// Number of bytes available in @ref const_image.
    size_t const_image_capacity;
    /// Caller-owned parser diagnostic output buffer.
    VmParserDiagnostic *diagnostics;
    /// Number of entries available in @ref diagnostics; this also bounds recovery diagnostics.
    size_t diagnostic_capacity;
    /// Suppresses accepted compatibility no-op, metadata-only, and limited-behavior notices.
    bool suppress_compatibility_notices;
    /// Phase 57J `.CONST ?` and `.CONST DUP(?)` declaration diagnostic policy.
    VmDiagnosticPolicyValue const_uninitialized_storage_policy;
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
    /// Number of bytes written to the configured .data/.DATA? image buffer.
    size_t data_size;
    /// Number of bytes written to the configured .CONST image buffer.
    size_t const_size;
    /// Whether a `.stack` directive source span was parsed.
    bool has_stack_directive_source_span;
    /// Source location of the parsed `.stack` directive when available.
    VmLexerSourceLocation stack_directive_source_location;
    /// Source span length of the parsed `.stack` directive in bytes.
    size_t stack_directive_source_span_length;
    /// Whether a `.stack size` directive requested a specific stack size.
    bool has_requested_stack_size;
    /// Requested stack size in bytes from `.stack size`; runtime stack behavior is deferred.
    uint32_t requested_stack_size;
    /// Whether `INCLUDE Irvine32.inc` registered the virtual Irvine32 symbol table.
    bool has_irvine32_virtual_include;
    /// Number of known virtual Irvine32 symbols registered by `INCLUDE Irvine32.inc`.
    size_t irvine32_virtual_symbol_count;
} VmParserResult;

/// Parses a MASM-like source file into implemented data layout and executable IR.
///
/// The parser accepts optional MASM32 header compatibility directives, numeric
/// equates, optional .data, .DATA?, and .CONST declarations before .code, emits
/// data-symbol metadata and deterministic data images, then parses the existing
/// minimal .code grammar. Source operands may use registers, immediates, simple
/// constant expressions, direct symbols, `OFFSET symbol + constant`, `TYPE
/// symbol`, `LENGTHOF symbol`, `SIZEOF symbol`, character literals, constant
/// symbol-offset memory operands, register-indirect memory operands, or
/// signed/unsigned PTR width overrides on supported memory operands; destination
/// operands may use registers, direct symbols, constant symbol-offset memory
/// operands, register-indirect memory operands, or signed/unsigned PTR width
/// overrides on supported memory operands. MASM32 header directives accepted in
/// Milestone 26 are parsed as no-ops or metadata and never load host files or
/// change runtime stack behavior. `.DATA?` storage is deterministic zero-filled
/// storage with metadata; `.CONST` storage is read-only once loaded into VM
/// memory.
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

/// Returns a stable display name for a parser diagnostic severity.
///
/// @param severity Parser diagnostic severity to inspect.
/// @return Static severity name, or NULL for invalid values.
const char *vm_parser_diagnostic_severity_name(VmParserDiagnosticSeverity severity);

/// Returns whether a parser diagnostic is fatal to source execution.
///
/// @param diagnostic Diagnostic to inspect.
/// @return true when @p diagnostic is non-NULL and has error severity.
bool vm_parser_diagnostic_is_error(const VmParserDiagnostic *diagnostic);

/// Classifies one Irvine32 routine or external name using the virtual registry.
///
/// Lookup is ASCII case-insensitive and does not require `INCLUDE Irvine32.inc`;
/// callers decide whether the virtual include is active before using the result
/// to affect parser diagnostics or execution.
///
/// @param name Routine or external name bytes to classify.
/// @param name_length Number of bytes in @p name.
/// @return Registry classification, or @ref VM_IRVINE32_SYMBOL_CLASS_UNKNOWN.
VmIrvine32SymbolClass vm_parser_classify_irvine32_symbol(const char *name, size_t name_length);

/// Returns the number of known names in the virtual Irvine32 registry.
///
/// @return Number of registry entries available after `INCLUDE Irvine32.inc`.
size_t vm_parser_irvine32_registry_symbol_count(void);

/// Returns a stable display name for one Irvine32 symbol class.
///
/// @param symbol_class Symbol class to inspect.
/// @return Static symbol-class name, or NULL for invalid values.
const char *vm_parser_irvine32_symbol_class_name(VmIrvine32SymbolClass symbol_class);

#endif
