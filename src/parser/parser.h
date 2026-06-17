/*
 * @file parser.h
 * @brief Parser API for the currently implemented MASM32 educational subset.
 *
 * This module converts the lexer token stream into data symbols, a .data image,
 * Phase 58 code-label metadata, direct branch lowering metadata for direct JMP and implemented conditional jumps, and
 * the minimal IR currently supported by the executor. It intentionally remains
 * limited to implemented writable,
 * uninitialized, and constant data declarations, numeric equates, extended
 * constant expressions, nested DUP initializers, OFFSET, direct symbol memory
 * operands, constant symbol-offset memory operands, signed and unsigned PTR
 * width overrides, register-indirect memory operands, TYPE, LENGTHOF, SIZEOF,
 * packed character literals, implemented instruction groups, INCLUDELIB
 * diagnostics, INVOKE/ADDR external-routine diagnostics, high-level-flow
 * diagnostics, explicit unsupported-feature diagnostics, safe recovery for
 * recognized MASM textbook constructs, specific surfaced lexer diagnostics,
 * virtual Irvine32 registry metadata, Phase 68 call-target classification
 * metadata used by Phase 69 direct user-procedure CALL lowering, Phase 70
 * plain near RET lowering, Phase 74 RET imm16 lowering, Phase 75
 * PROC metadata diagnostics, and Phase 76 PROC USES parsing metadata, and Phase 78 LOCAL declaration parser metadata.
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
    VM_PARSER_STATUS_SYMBOL_CAPACITY_EXCEEDED,
    /// Parsing stopped because the caller-provided code-label table was full.
    VM_PARSER_STATUS_CODE_LABEL_CAPACITY_EXCEEDED,
    /// Parsing stopped because the caller-provided procedure-range table was full.
    VM_PARSER_STATUS_PROCEDURE_CAPACITY_EXCEEDED
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
    /// A direct branch target resolved to a symbol or label that cannot be branched to.
    VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET,
    /// A direct branch operand used a form reserved for a later branch phase.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM,
    /// An accepted `.CONST ?` or `.CONST DUP(?)` declaration reserves uninitialized read-only storage.
    VM_PARSER_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE,
    /// A .model directive used a form outside `.model flat, stdcall`.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MODEL,
    /// An INCLUDE directive requested a file outside the simulator's virtual built-ins.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDE,
    /// An INCLUDE directive used a host filesystem, relative, or absolute path.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HOST_INCLUDE_PATH,
    /// An INCLUDE directive referenced Windows API declarations outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_INCLUDE,
    /// An INCLUDE directive referenced a local MASM32 SDK include path outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY_INCLUDE,
    /// An INCLUDELIB directive requested linker/import-library behavior outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDELIB,
    /// An INCLUDELIB directive referenced a Windows import library outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_LIBRARY,
    /// An INCLUDELIB directive referenced an external MASM32 library outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY,
    /// An INVOKE line used MASM procedure-call syntax that is not implemented.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE,
    /// An ADDR argument was used before procedure-argument lowering exists.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_ADDR,
    /// An INVOKE target names an external routine outside executable simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_EXTERNAL_ROUTINE,
    /// An INVOKE target names Windows API behavior outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINAPI_EXECUTION,
    /// An INVOKE target names an external MASM32 runtime routine outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_RUNTIME_ROUTINE,
    /// An INVOKE target names C runtime behavior outside simulator scope.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CRT_ROUTINE,
    /// A `.IF` or `.ELSEIF` directive used unsupported high-level MASM flow.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_IF,
    /// A `.ELSE` directive used unsupported high-level MASM flow.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ELSE,
    /// A `.ENDIF` directive used unsupported high-level MASM flow.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ENDIF,
    /// A `.WHILE` directive used unsupported high-level MASM flow.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_WHILE,
    /// A `.REPEAT` directive used unsupported high-level MASM flow.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_REPEAT,
    /// Another high-level MASM flow directive is recognized but unsupported.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW,
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
    /// A code label declaration duplicates an existing code label or procedure-entry label.
    VM_PARSER_DIAGNOSTIC_DUPLICATE_LABEL,
    /// A source operand or declaration attempted to use EIP as ordinary source state.
    VM_PARSER_DIAGNOSTIC_INVALID_EIP_OPERAND,
    /// A user-defined symbol declaration used a simulator-recognized MASM reserved word.
    VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL,
    /// A code label declaration conflicts with another user-defined symbol category.
    VM_PARSER_DIAGNOSTIC_LABEL_SYMBOL_CONFLICT,
    /// A procedure declaration used malformed procedure-name syntax.
    VM_PARSER_DIAGNOSTIC_INVALID_PROCEDURE_NAME,
    /// A PROC declaration used an unsupported attribute, parameter, language, visibility, frame, or distance token.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PROC_ATTRIBUTE,
    /// A PROC USES declaration omitted or malformed the required register list.
    VM_PARSER_DIAGNOSTIC_EXPECTED_PROC_USES_REGISTER,
    /// A PROC USES declaration named a register outside the Phase 76 accepted set.
    VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER,
    /// A PROC USES declaration repeated a register in the same save list.
    VM_PARSER_DIAGNOSTIC_DUPLICATE_PROC_USES_REGISTER,
    /// A PROC declaration used malformed syntax after the PROC marker.
    VM_PARSER_DIAGNOSTIC_INVALID_PROC_DECLARATION,
    /// An ENDP declaration named a procedure other than the currently open PROC.
    VM_PARSER_DIAGNOSTIC_PROC_END_MISMATCH,
    /// A procedure declaration conflicts with an earlier procedure declaration.
    VM_PARSER_DIAGNOSTIC_DUPLICATE_PROCEDURE,
    /// A direct CALL target resolved to a known symbol that is not a user procedure entry.
    VM_PARSER_DIAGNOSTIC_INVALID_CALL_TARGET,
    /// A direct CALL operand used a register, memory, far, OFFSET, or expression form reserved for a later phase.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_FORM,
    /// A direct CALL target names Windows/API, external, linker, import-library, or host-environment behavior.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_EXTERNAL_CALL,
    /// A LOCAL declaration appeared outside a procedure body.
    VM_PARSER_DIAGNOSTIC_LOCAL_OUTSIDE_PROCEDURE,
    /// A LOCAL declaration appeared after an executable instruction in the same procedure.
    VM_PARSER_DIAGNOSTIC_LOCAL_AFTER_INSTRUCTION,
    /// A LOCAL declaration used a type outside the Phase 78 accepted local subset.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LOCAL_TYPE,
    /// A LOCAL declaration was malformed or used unsupported initializer syntax.
    VM_PARSER_DIAGNOSTIC_INVALID_LOCAL_DECLARATION,
    /// A LOCAL symbol collided with another local, procedure name, parameter, or same-procedure label.
    VM_PARSER_DIAGNOSTIC_DUPLICATE_LOCAL_SYMBOL,
    /// A LOCAL array declaration used a missing, non-positive, or unsupported count expression.
    VM_PARSER_DIAGNOSTIC_INVALID_LOCAL_COUNT,
    /// A CALL/INVOKE classifier query identified an unsupported target.
    VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_TARGET,
    /// The caller-provided code-label table was full.
    VM_PARSER_DIAGNOSTIC_CODE_LABEL_CAPACITY_EXCEEDED,
    /// The caller-provided procedure-range table was full.
    VM_PARSER_DIAGNOSTIC_PROCEDURE_CAPACITY_EXCEEDED,
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


/// Identifies how a code label was declared in source.
typedef enum VmCodeLabelDeclarationKind {
    /// Ordinary `name:` code-label declaration.
    VM_CODE_LABEL_DECLARATION_ORDINARY = 0,
    /// Procedure-entry code label recorded from `name PROC`.
    VM_CODE_LABEL_DECLARATION_PROCEDURE_ENTRY
} VmCodeLabelDeclarationKind;

/// Identifies the Phase 58/60 target classification of a code label.
typedef enum VmCodeLabelTargetKind {
    /// The label targets an executable IR instruction.
    VM_CODE_LABEL_TARGET_EXECUTABLE_INSTRUCTION = 0,
    /// The label is a procedure entry targeting the procedure body's first executable instruction.
    VM_CODE_LABEL_TARGET_PROCEDURE_ENTRY,
    /// The label has no following executable instruction target in this phase.
    VM_CODE_LABEL_TARGET_NO_EXECUTABLE_TARGET
} VmCodeLabelTargetKind;

/// Describes one accepted code-label declaration used by label metadata and direct branches.
typedef struct VmCodeLabel {
    /// Null-terminated original source spelling of the label.
    char name[VM_SYMBOL_NAME_CAPACITY];
    /// How the label was declared in source.
    VmCodeLabelDeclarationKind declaration_kind;
    /// Target classification resolved by parser metadata.
    VmCodeLabelTargetKind target_kind;
    /// Active CASEMAP policy at the declaration location.
    VmSymbolCasePolicy case_policy;
    /// Zero-based target IR instruction index when a target exists.
    size_t target_instruction_index;
    /// Whether @ref target_instruction_index identifies a real IR instruction.
    bool has_target_instruction_index;
    /// Source location of the label name token.
    VmLexerSourceLocation source_location;
    /// Source span length of the label name in bytes.
    size_t source_span_length;
} VmCodeLabel;


/// Maximum number of Phase 76 PROC USES registers preserved on one procedure metadata record.
#define VM_PROCEDURE_USES_REGISTER_CAPACITY 6U

/// Maximum number of rejected PROC attributes preserved on one procedure metadata record.
#define VM_PROCEDURE_UNSUPPORTED_ATTRIBUTE_CAPACITY 4U

/// Maximum byte length, including the NUL terminator, for one rejected PROC attribute name.
#define VM_PROCEDURE_UNSUPPORTED_ATTRIBUTE_NAME_CAPACITY 32U

/// Maximum number of Phase 78 LOCAL declarations preserved on one procedure metadata record.
#define VM_PROCEDURE_LOCAL_CAPACITY 32U

/// Describes one accepted Phase 78 procedure-local declaration.
typedef struct VmProcedureLocalSymbol {
    /// Null-terminated original source spelling of the local symbol name.
    char name[VM_SYMBOL_NAME_CAPACITY];
    /// Declared scalar element type of the local symbol.
    VmSymbolDataType data_type;
    /// Source-order CASEMAP policy active at the local declaration.
    VmSymbolCasePolicy case_policy;
    /// Size in bytes of one declared element.
    uint8_t element_size_bytes;
    /// Alignment used for the local object, equal to min(element size, 4).
    uint8_t alignment_bytes;
    /// Number of elements represented by this local symbol.
    uint32_t element_count;
    /// Total bytes reserved by this local object before inter-object padding.
    uint32_t size_bytes;
    /// Negative EBP-relative byte offset of the first byte of this local object.
    int32_t ebp_offset;
    /// Source location of the local-name token.
    VmLexerSourceLocation source_location;
    /// Source span length of the local-name token in bytes.
    size_t source_span_length;
} VmProcedureLocalSymbol;

/// Describes one unsupported PROC attribute token retained for diagnostics and future metadata expansion.
typedef struct VmProcedureUnsupportedAttribute {
    /// Null-terminated original source spelling of the unsupported attribute or parameter token.
    char name[VM_PROCEDURE_UNSUPPORTED_ATTRIBUTE_NAME_CAPACITY];
    /// Source location of the unsupported attribute or parameter token.
    VmLexerSourceLocation source_location;
    /// Source span length of the unsupported attribute or parameter token in bytes.
    size_t source_span_length;
} VmProcedureUnsupportedAttribute;

/// Describes one accepted `PROC` / `ENDP` source procedure range.
typedef struct VmProcedureRange {
    /// Null-terminated original source spelling of the procedure name.
    char name[VM_SYMBOL_NAME_CAPACITY];
    /// Active CASEMAP policy at the procedure declaration location.
    VmSymbolCasePolicy case_policy;
    /// Zero-based first IR instruction index inside the procedure body.
    size_t start_instruction_index;
    /// Exclusive zero-based IR instruction index at the matching `ENDP` boundary.
    size_t end_instruction_index;
    /// Whether the procedure contains at least one executable IR instruction.
    bool has_executable_instruction;
    /// Source location of the procedure name token on the `PROC` line.
    VmLexerSourceLocation source_location;
    /// Source span length of the procedure name in bytes.
    size_t source_span_length;
    /// Number of Phase 76 accepted PROC USES registers stored in declared order.
    size_t uses_register_count;
    /// Canonical Phase 76 PROC USES register identities in declared order.
    VmRegister uses_registers[VM_PROCEDURE_USES_REGISTER_CAPACITY];
    /// Number of unsupported PROC attributes recorded for this procedure metadata entry.
    size_t unsupported_attribute_count;
    /// Reserved parser-owned unsupported-attribute metadata slots for future accepted PROC attribute phases.
    VmProcedureUnsupportedAttribute unsupported_attributes[VM_PROCEDURE_UNSUPPORTED_ATTRIBUTE_CAPACITY];
    /// Number of Phase 78 LOCAL declarations stored for this procedure.
    size_t local_count;
    /// Total frame byte count after declaration-order layout and final 4-byte rounding.
    uint32_t local_frame_size_bytes;
    /// Parser-owned Phase 78 LOCAL metadata slots in declaration order.
    VmProcedureLocalSymbol locals[VM_PROCEDURE_LOCAL_CAPACITY];
} VmProcedureRange;

/// Describes one accepted numeric equate declaration published for metadata consumers.
typedef struct VmNumericEquate {
    /// Null-terminated original source spelling of the equate name.
    char name[VM_SYMBOL_NAME_CAPACITY];
    /// Evaluated signed 64-bit equate value.
    int64_t value;
    /// Source-order CASEMAP policy active at the declaration.
    VmSymbolCasePolicy case_policy;
    /// Source location of the equate name token.
    VmLexerSourceLocation source_location;
    /// Source span length of the equate name in bytes.
    size_t source_span_length;
} VmNumericEquate;

/// Identifies one Phase 68/69 CALL/INVOKE target classification.
typedef enum VmParserCallTargetClass {
    /// Target syntax is absent or uses a non-identifier expression form.
    VM_PARSER_CALL_TARGET_MALFORMED_EXPRESSION = 0,
    /// Target names an accepted user procedure entry declared with `PROC`.
    VM_PARSER_CALL_TARGET_USER_PROCEDURE_ENTRY,
    /// Target names an ordinary executable code label, not a procedure entry.
    VM_PARSER_CALL_TARGET_CODE_LABEL,
    /// Target names a virtual Irvine32 intrinsic or terminator implemented now.
    VM_PARSER_CALL_TARGET_IRVINE32_SUPPORTED,
    /// Target names a recognized Irvine32 routine planned for a later phase.
    VM_PARSER_CALL_TARGET_IRVINE32_PLANNED,
    /// Target names a recognized Irvine32 routine explicitly unsupported in v1.
    VM_PARSER_CALL_TARGET_IRVINE32_UNSUPPORTED,
    /// Target names Windows API, external, linker, import-library, or host-environment behavior outside simulator scope.
    VM_PARSER_CALL_TARGET_EXTERNAL_NON_GOAL,
    /// Target names an accepted data symbol.
    VM_PARSER_CALL_TARGET_DATA_SYMBOL,
    /// Target names an accepted numeric equate.
    VM_PARSER_CALL_TARGET_NUMERIC_EQUATE,
    /// Target names a local symbol if future local-symbol metadata is supplied.
    VM_PARSER_CALL_TARGET_LOCAL_SYMBOL,
    /// Target names a reserved MASM/simulator word.
    VM_PARSER_CALL_TARGET_RESERVED_WORD,
    /// Target is identifier-shaped but has no known metadata entry.
    VM_PARSER_CALL_TARGET_UNKNOWN_SYMBOL
} VmParserCallTargetClass;

/// Supplies metadata tables used by the Phase 68 call-target classifier.
typedef struct VmParserCallTargetContext {
    /// Accepted data-symbol table.
    const VmSymbol *symbols;
    /// Number of accepted entries in @ref symbols.
    size_t symbol_count;
    /// Accepted code-label table.
    const VmCodeLabel *code_labels;
    /// Number of accepted entries in @ref code_labels.
    size_t code_label_count;
    /// Accepted procedure-range table.
    const VmProcedureRange *procedure_ranges;
    /// Number of accepted entries in @ref procedure_ranges.
    size_t procedure_range_count;
    /// Accepted numeric-equate metadata table.
    const VmNumericEquate *numeric_equates;
    /// Number of accepted entries in @ref numeric_equates.
    size_t numeric_equate_count;
    /// User-symbol CASEMAP policy active at the target reference.
    VmSymbolCasePolicy case_policy;
} VmParserCallTargetContext;

/// Describes the result of one Phase 68 call-target classification query.
typedef struct VmParserCallTargetClassification {
    /// Primary target class.
    VmParserCallTargetClass target_class;
    /// Irvine32 registry class when @ref target_class is an Irvine32-backed class.
    VmIrvine32SymbolClass irvine32_symbol_class;
    /// Index into the matching metadata table when @ref has_metadata_index is true.
    size_t metadata_index;
    /// Whether @ref metadata_index identifies a matching metadata entry.
    bool has_metadata_index;
} VmParserCallTargetClassification;

/// Returns a stable display name for a code-label declaration kind.
///
/// @param kind Declaration kind to inspect.
/// @return Static declaration-kind name, or NULL for invalid values.
const char *vm_code_label_declaration_kind_name(VmCodeLabelDeclarationKind kind);

/// Returns a stable display name for a code-label target kind.
///
/// @param kind Target kind to inspect.
/// @return Static target-kind name, or NULL for invalid values.
const char *vm_code_label_target_kind_name(VmCodeLabelTargetKind kind);

/// Returns a stable display name for a Phase 68 call-target class.
///
/// @param target_class Target class to inspect.
/// @return Static target-class name, or NULL for invalid values.
const char *vm_parser_call_target_class_name(VmParserCallTargetClass target_class);

/// Classifies an identifier-shaped CALL/INVOKE target by parser metadata.
///
/// The helper performs no lowering or execution. Direct CALL and future INVOKE
/// parser phases use it to distinguish user procedures, ordinary labels, data
/// symbols, numeric equates, Irvine32 registry names, external non-goals,
/// reserved words, malformed operands, and unknown symbols before committing
/// instruction-specific behavior.
///
/// @param context Metadata tables and reference-time CASEMAP policy.
/// @param target Source target bytes to classify as an identifier.
/// @param target_length Number of bytes in @p target.
/// @return Classification result.
VmParserCallTargetClassification vm_parser_classify_call_target_name(
    const VmParserCallTargetContext *context,
    const char *target,
    size_t target_length
);

/// Classifies a lexer token as a CALL/INVOKE target.
///
/// Non-identifier tokens classify as malformed target expressions. Identifier
/// tokens are classified using the same metadata and reserved-word policy as
/// @ref vm_parser_classify_call_target_name, while register tokens remain
/// distinguishable as reserved non-identifier target forms.
///
/// @param context Metadata tables and reference-time CASEMAP policy.
/// @param target_token Lexer token carrying the candidate target.
/// @return Classification result.
VmParserCallTargetClassification vm_parser_classify_call_target_token(
    const VmParserCallTargetContext *context,
    const VmLexerToken *target_token
);

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
    /// Source location of a prior related definition when available.
    VmLexerSourceLocation related_location;
    /// Source span length of the prior related definition in bytes.
    size_t related_span_length;
    /// Whether @ref related_location and @ref related_span_length identify a prior related definition.
    bool has_related_location;
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
    /// Caller-owned output code-label table. May be NULL only when code-label capacity is zero.
    VmCodeLabel *code_labels;
    /// Number of entries available in @ref code_labels.
    size_t code_label_capacity;
    /// Caller-owned output procedure-range table. May be NULL only when procedure-range capacity is zero.
    VmProcedureRange *procedure_ranges;
    /// Number of entries available in @ref procedure_ranges.
    size_t procedure_range_capacity;
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
    /// Optional caller-owned numeric-equate metadata output buffer.
    VmNumericEquate *numeric_equates;
    /// Number of entries available in @ref numeric_equates.
    size_t numeric_equate_capacity;
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
    /// Number of code labels written to the configured code-label buffer.
    size_t code_label_count;
    /// Number of procedure ranges written to the configured procedure-range buffer.
    size_t procedure_range_count;
    /// Number of numeric equates written to the configured numeric-equate metadata buffer.
    size_t numeric_equate_count;
    /// Whether @ref selected_entry_procedure_index identifies the `END entryName` procedure.
    bool has_selected_entry_procedure;
    /// Procedure-range index selected by the accepted `END entryName` directive.
    size_t selected_entry_procedure_index;
    /// Zero-based first instruction index where source-run execution should start.
    size_t selected_entry_start_instruction_index;
    /// Exclusive instruction index of the selected entry procedure's `ENDP` boundary.
    size_t selected_entry_end_instruction_index;
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
    /// Requested stack size in bytes from `.stack size`; Phase 72A source-level PUSH/POP uses this metadata for stack sizing, while procedure frames remain deferred.
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
/// overrides on supported memory operands. Direct JMP and implemented conditional-jump operands may target
/// executable code labels or procedure-entry labels and are lowered to runtime
/// branch metadata. Phase 69 direct CALL may target only user procedure entries
/// and is lowered to checked stack-write/transfer metadata. MASM32 header directives accepted in
/// Milestone 26 are parsed as no-ops or metadata and never load host files or
/// change source-level stack instruction behavior or procedure frames. `.DATA?`
/// storage is deterministic zero-filled with metadata; `.CONST` storage is
/// read-only once loaded into VM
/// memory. Code labels and procedure-entry labels are recorded as parser/source
/// metadata; direct branch lowering consumes that metadata, and the executor
/// transfers to resolved direct branch targets for implemented branch opcodes.
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
