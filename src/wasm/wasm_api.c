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
 * INC/DEC, bitwise logical instructions, shifts, ROL/ROR, LEA, Phase 61
 * direct JMP runtime execution, unsigned MUL, one-operand signed
 * IMUL, two- and three-operand signed IMUL, unsigned DIV, signed IDIV, the virtual Irvine32 `exit` terminator, Phase 67A selected END-entry
 * procedure startup and fallthrough boundaries, Phase 68 procedure-entry and
 * direct user-procedure CALL execution metadata, Phase 68A ESP stack startup,
 * Phase 68B displayed EIP pseudo-code-address control state, Phase 70
 * helper RET execution, Phase 71 root RET termination, Phase 71A optional
 * strict root-code-stream RET diagnostics, Phase 71C code-stream fallthrough
 * and code-end diagnostics, Phase 71D configurable procedure-fallthrough
 * diagnostics, Phase 72 call-depth resource-limit diagnostics, Phase 72A
 * source-level PUSH/POP stack transfers, Phase 73 LEAVE frame teardown, Phase 74
 * RET imm16 caller cleanup, Phase 75 PROC metadata diagnostics, Phase 76 PROC USES
 * parsing metadata, Phase 77 direct-CALL PROC USES runtime save/restore,
 * Phase 78A limited OPTION NOKEYWORD parser behavior and Phase 78 LOCAL
 * diagnostics, and recovered unsupported-feature diagnostics, then reports a
 * compact JSON result for the UI.
 */

#include "wasm_api.h"

#include "../core/masm32_sim_api.h"
#include "../core/vm_cpu.h"
#include "../core/vm_diagnostic_policy.h"
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

/// Maximum code labels retained by the source-run API.
#define MASM32_SIM_WASM_MAX_RUN_CODE_LABELS 128U

/// Maximum procedure ranges retained by the source-run API.
#define MASM32_SIM_WASM_MAX_RUN_PROCEDURE_RANGES 128U

/// Maximum declared-object map entries retained by one source run.
#define MASM32_SIM_WASM_MAX_OBJECT_MAP_ENTRIES MASM32_SIM_WASM_MAX_RUN_SYMBOLS

/// Mask value used for bytes that are currently initialized.
#define MASM32_SIM_WASM_DATA_BYTE_INITIALIZED 1U

/// Mask value used for bytes that remain uninitialized-origin.
#define MASM32_SIM_WASM_DATA_BYTE_UNINITIALIZED 0U

/// Numeric runtime/source-run behavior phase reported to JSON consumers.
#define MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER 78U

/// Suffix for the current Phase 78A runtime/source-run behavior phase.
#define MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX "A"

/// Full name of the current Phase 78A runtime/source-run behavior phase.
#define MASM32_SIM_WASM_RUNTIME_PHASE_NAME "Phase 78A - Limited OPTION NOKEYWORD Reserved-Word Opt-Out"

/// Browser/Wasm source-run JSON output-contract identifier for Phase 78A limited OPTION NOKEYWORD state.
#define MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT "phase-78a-nokeyword-output-contract-v1"

/// Default maximum number of VM instructions a source-run request may execute.
#define MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT 1000000U

/// Largest accepted test-facing instruction-limit value.
#define MASM32_SIM_WASM_MAX_INSTRUCTION_LIMIT UINT32_MAX

/// Default Phase 72 source-run call-depth limit.
#define MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT VM_DEFAULT_CALL_DEPTH_LIMIT

/// Minimum accepted Phase 72 source-run call-depth limit.
#define MASM32_SIM_WASM_MIN_CALL_DEPTH_LIMIT VM_MIN_CALL_DEPTH_LIMIT

/// Maximum accepted Phase 72 source-run call-depth limit.
#define MASM32_SIM_WASM_MAX_CALL_DEPTH_LIMIT VM_MAX_CALL_DEPTH_LIMIT

/// Stable diagnostic code for startup-state notices.
#define MASM32_SIM_WASM_STARTUP_STATE_NOTICE_CODE "startup-state-notice"

/// User-facing startup-state notice text for deterministic zero startup mode.
#define MASM32_SIM_WASM_STARTUP_STATE_ZERO_NOTICE_MESSAGE "The simulator starts modeled flags and all registers to 0, except ESP and EIP. ESP is set to the end of the active stack region, and EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register. Uninitialized storage bytes are also zero-filled, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values."

/// User-facing startup-state notice text for Phase 57F seeded register/flag startup mode.
#define MASM32_SIM_WASM_STARTUP_STATE_SEEDED_REGISTER_NOTICE_MESSAGE "The simulator starts modeled flags and ordinary registers from the configured deterministic seed, except ESP and EIP. ESP is set to the end of the active stack region, and EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register. Uninitialized storage bytes remain zero-filled, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values."

/// User-facing startup-state notice text for Phase 57G seeded uninitialized-storage visible bytes.
#define MASM32_SIM_WASM_STARTUP_STATE_SEEDED_UNINITIALIZED_NOTICE_MESSAGE "The simulator starts modeled flags and all registers to 0, except ESP and EIP. ESP is set to the end of the active stack region, EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register, and visible bytes for uninitialized storage are initialized from the configured deterministic seed while uninitialized-origin metadata is preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register, flag, or storage startup values."

/// User-facing startup-state notice text when both seeded startup axes are enabled.
#define MASM32_SIM_WASM_STARTUP_STATE_SEEDED_REGISTER_AND_UNINITIALIZED_NOTICE_MESSAGE "The simulator starts modeled flags, ordinary registers, and visible bytes for uninitialized storage from the configured deterministic seed, except ESP and EIP. ESP is set to the end of the active stack region, and EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register. Uninitialized-origin metadata is preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register, flag, or storage startup values."

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

/// Maximum section-boundary validation warnings retained for one source run.
#define MASM32_SIM_WASM_MAX_SECTION_WARNINGS 64U

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
    /// One-based source line of the instruction that caused the write, or zero when unavailable.
    uint32_t source_line;
    /// Original source text for the instruction that caused the write, or NULL when unavailable.
    const char *source_text;
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

/// Identifies the Phase 53B section-boundary level that produced a diagnostic.
typedef enum Masm32SimWasmSectionBoundaryLevel {
    /// Level 2 section-capacity validation.
    MASM32_SIM_WASM_SECTION_BOUNDARY_CAPACITY = 0,
    /// Level 3 section-image validation.
    MASM32_SIM_WASM_SECTION_BOUNDARY_IMAGE
} Masm32SimWasmSectionBoundaryLevel;

/// Describes one Phase 53B section-boundary validation diagnostic.
typedef struct Masm32SimWasmSectionBoundaryDiagnostic {
    /// Validation level that produced this diagnostic.
    Masm32SimWasmSectionBoundaryLevel level;
    /// Whether the access read or wrote memory.
    VmExecMemoryAccessKind access_kind;
    /// First simulated address in the validated memory access range.
    uint32_t start_address;
    /// Inclusive final simulated address in the validated memory access range.
    uint32_t end_address;
    /// Number of bytes requested by the access.
    uint32_t size_bytes;
    /// Stable owning section name when the start address maps to one.
    char owner_name[16];
    /// Whether @ref owner_name identifies a known owner.
    bool has_owner;
    /// First byte of the relevant section capacity or image range.
    uint32_t boundary_start;
    /// Inclusive final byte of the relevant section capacity or image range.
    uint32_t boundary_end;
    /// Whether boundary range fields are meaningful.
    bool has_boundary;
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
} Masm32SimWasmSectionBoundaryDiagnostic;

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

/// Describes one planned memory access before an instruction is stepped.
typedef struct Masm32SimWasmPlannedMemoryAccess {
    /// Memory operand that will be accessed if execution proceeds.
    VmIrOperand operand;
    /// Whether the planned access reads or writes memory.
    VmExecMemoryAccessKind kind;
    /// Width of the planned access in bits.
    uint8_t width_bits;
} Masm32SimWasmPlannedMemoryAccess;

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

/// Retains one Phase 71D procedure-fallthrough diagnostic for JSON rendering.
typedef struct Masm32SimWasmProcedureFallthroughDiagnostic {
    /// Whether this diagnostic slot contains valid data.
    bool has_diagnostic;
    /// Source instruction index that crossed or left a procedure boundary.
    uint32_t from_instruction_index;
    /// Destination instruction index reached by ordinary fallthrough.
    uint32_t to_instruction_index;
    /// One-based source line for the responsible instruction.
    uint32_t line;
    /// One-based source column for the responsible instruction.
    uint32_t column;
    /// Zero-based byte offset for the responsible instruction.
    size_t byte_offset;
    /// Source span length for the responsible instruction.
    size_t span_length;
    /// Whether byte-offset and span-length metadata is available.
    bool has_source_span;
    /// Whether @ref from_procedure contains a displayable procedure name.
    bool has_from_procedure;
    /// Source spelling of the procedure whose boundary was crossed.
    char from_procedure[VM_SYMBOL_NAME_CAPACITY];
    /// Whether @ref to_procedure contains a displayable procedure name.
    bool has_to_procedure;
    /// Source spelling of the destination procedure.
    char to_procedure[VM_SYMBOL_NAME_CAPACITY];
} Masm32SimWasmProcedureFallthroughDiagnostic;

/// Retains one Phase 72 call-depth-exceeded diagnostic for JSON rendering.
typedef struct Masm32SimWasmCallDepthDiagnostic {
    /// Whether this diagnostic slot contains valid data.
    bool has_diagnostic;
    /// Source line for the rejected CALL target or instruction.
    uint32_t line;
    /// Source column for the rejected CALL target or instruction.
    uint32_t column;
    /// Source byte offset for the rejected CALL target or instruction.
    size_t byte_offset;
    /// Source span length for the rejected CALL target or instruction.
    size_t span_length;
    /// Whether byte-offset and span-length metadata is available.
    bool has_source_span;
    /// Current depth before the rejected CALL.
    uint32_t current_depth;
    /// Attempted depth for the rejected CALL.
    uint32_t attempted_depth;
    /// Configured Phase 72 call-depth limit.
    uint32_t limit;
    /// Whether @ref target_procedure contains a displayable procedure name.
    bool has_target_procedure;
    /// Source spelling of the rejected CALL target procedure.
    char target_procedure[VM_SYMBOL_NAME_CAPACITY];
    /// Whether @ref selected_entry_procedure contains a displayable entry name.
    bool has_selected_entry_procedure;
    /// Source spelling of the selected entry procedure.
    char selected_entry_procedure[VM_SYMBOL_NAME_CAPACITY];
} Masm32SimWasmCallDepthDiagnostic;

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
    /// Code labels emitted by the parser.
    VmCodeLabel code_labels[MASM32_SIM_WASM_MAX_RUN_CODE_LABELS];
    /// Procedure ranges emitted by the parser for selected-entry runtime boundaries.
    VmProcedureRange procedure_ranges[MASM32_SIM_WASM_MAX_RUN_PROCEDURE_RANGES];
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
    /// Per-byte initialization mask for `.CONST` bytes.
    uint8_t const_initialized_mask[MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES];
    /// Number of `.CONST` bytes covered by @ref const_initialized_mask.
    size_t const_initialized_mask_size;
    /// Symbol-aware memory changes collected during execution.
    Masm32SimSymbolicMemoryChange memory_changes[MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES];
    /// Number of valid entries in @ref memory_changes.
    size_t memory_change_count;
    /// Unaligned memory access warnings collected during execution.
    Masm32SimWasmUnalignedWarning warnings[MASM32_SIM_WASM_MAX_RUN_WARNINGS];
    /// Number of valid entries in @ref warnings.
    size_t warning_count;
    /// Procedure-fallthrough warnings collected during execution.
    Masm32SimWasmProcedureFallthroughDiagnostic procedure_fallthrough_warnings[MASM32_SIM_WASM_MAX_RUN_WARNINGS];
    /// Number of valid procedure-fallthrough warning entries.
    size_t procedure_fallthrough_warning_count;
    /// Fatal procedure-fallthrough diagnostic captured during execution.
    Masm32SimWasmProcedureFallthroughDiagnostic procedure_fallthrough_violation;
    /// Whether @ref procedure_fallthrough_violation contains a fatal diagnostic.
    bool has_procedure_fallthrough_violation;
    /// Allocated-object memory validation warnings collected during execution.
    Masm32SimWasmObjectBoundsDiagnostic object_warnings[MASM32_SIM_WASM_MAX_OBJECT_WARNINGS];
    /// Number of valid entries in @ref object_warnings.
    size_t object_warning_count;
    /// Fatal allocated-object strict diagnostic captured during execution.
    Masm32SimWasmObjectBoundsDiagnostic object_violation;
    /// Whether @ref object_violation contains a fatal strict-mode diagnostic.
    bool has_object_violation;
    /// Section-boundary validation warnings collected during execution.
    Masm32SimWasmSectionBoundaryDiagnostic section_warnings[MASM32_SIM_WASM_MAX_SECTION_WARNINGS];
    /// Number of valid entries in @ref section_warnings.
    size_t section_warning_count;
    /// Fatal section-boundary strict diagnostic captured before execution.
    Masm32SimWasmSectionBoundaryDiagnostic section_violation;
    /// Whether @ref section_violation contains a fatal strict-mode diagnostic.
    bool has_section_violation;
    /// Number of bytes in the selected `.data`/`.DATA?` section image.
    uint32_t data_section_image_size;
    /// Number of bytes in the selected `.CONST` section image.
    uint32_t const_section_image_size;
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
    /// Configured Phase 59 source-run instruction-count limit.
    uint32_t instruction_limit;
    /// Configured Phase 71A root-code-stream RET handling mode.
    Masm32SimWasmRootRetMode root_ret_mode;
    /// Configured Phase 71D procedure-fallthrough diagnostic policy.
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy;
    /// Configured Phase 71E selected-entry procedure end mode.
    Masm32SimWasmEntryProcedureEndMode entry_procedure_end_mode;
    /// Configured Phase 72 direct user-procedure CALL depth limit.
    uint32_t call_depth_limit;
    /// Number of VM instructions fully executed and committed for this run.
    uint64_t executed_instruction_count;
    /// Instruction index that would have been fetched after the limit was reached.
    uint32_t attempted_next_instruction_index;
    /// Whether @ref attempted_next_instruction_index contains valid limit-failure metadata.
    bool has_attempted_next_instruction_index;
    /// Last fully executed instruction index for source-run accounting.
    uint32_t current_instruction_index;
    /// Whether @ref current_instruction_index contains a valid instruction index.
    bool has_current_instruction_index;
    /// Whether Phase 59 instruction-limit enforcement stopped this run.
    bool has_instruction_limit_violation;
    /// Fatal Phase 72 call-depth diagnostic captured during execution.
    Masm32SimWasmCallDepthDiagnostic call_depth_violation;
    /// Whether @ref call_depth_violation contains a fatal diagnostic.
    bool has_call_depth_violation;
    /// Source line for the blocked instruction-limit diagnostic.
    uint32_t instruction_limit_line;
    /// Source column for the blocked instruction-limit diagnostic.
    uint32_t instruction_limit_column;
    /// Source byte offset for the blocked instruction-limit diagnostic.
    size_t instruction_limit_byte_offset;
    /// Source span length for the blocked instruction-limit diagnostic.
    size_t instruction_limit_span_length;
    /// Whether source-span metadata is available for the blocked instruction.
    bool instruction_limit_has_source_span;
    /// Original source text for calculating runtime diagnostic source spans.
    const char *source_text;
    /// Copied source text retained by IR instruction metadata.
    char source_text_storage[MASM32_SIM_WASM_RUN_SOURCE_TEXT_BYTES];
    /// VM instance used during source-run execution; stored with run storage to avoid overflowing the smaller browser Wasm stack.
    Vm vm;
} Masm32SimWasmRunStorage;

/// Static response buffer returned to JavaScript by the source-run export.
static char g_masm32_sim_wasm_run_json[MASM32_SIM_WASM_RUN_JSON_BYTES];

/// Static parse-and-run storage reused by each single-threaded worker request.
static Masm32SimWasmRunStorage g_masm32_sim_wasm_run_storage;

/// Returns whether a Phase 57F startup register/flag mode enum value is accepted.
///
/// @param mode Startup mode value to inspect.
/// @return true when @p mode is valid.
static bool masm32_sim_wasm_startup_register_flag_mode_is_valid(Masm32SimWasmStartupRegisterFlagMode mode) {
    return mode == MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO ||
        mode == MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM;
}

/// Returns whether a Phase 57G uninitialized-storage visible-byte mode enum value is accepted.
///
/// @param mode Startup visible-byte mode value to inspect.
/// @return true when @p mode is valid.
static bool masm32_sim_wasm_uninitialized_storage_visible_byte_mode_is_valid(Masm32SimWasmUninitializedStorageVisibleByteMode mode) {
    return mode == MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO ||
        mode == MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM;
}

/// Returns whether a Phase 71A root RET mode enum value is accepted.
///
/// @param mode Root RET mode value to inspect.
/// @return true when @p mode is valid.
static bool masm32_sim_wasm_root_ret_mode_is_valid(Masm32SimWasmRootRetMode mode) {
    return mode == MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE ||
        mode == MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME;
}

/// Maps a browser-facing root RET mode to the core VM setting.
///
/// @param mode Browser-facing root RET mode.
/// @return Core VM root RET mode; invalid input maps to the compatible default.
static VmRootRetMode masm32_sim_wasm_map_root_ret_mode(Masm32SimWasmRootRetMode mode) {
    if (mode == MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME) {
        return VM_ROOT_RET_MODE_STRICT_CALL_FRAME;
    }
    return VM_ROOT_RET_MODE_MASM32_COMPATIBLE;
}

/// Returns the source-run JSON value for a root RET mode.
///
/// @param mode Root RET mode to serialize.
/// @return Stable setting string for accepted modes.
static const char *masm32_sim_wasm_root_ret_mode_name(Masm32SimWasmRootRetMode mode) {
    if (mode == MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME) {
        return "strict-call-frame";
    }
    return "masm32-compatible";
}

/// Returns whether a Phase 71D procedure-fallthrough policy enum value is accepted.
///
/// @param policy Procedure-fallthrough policy value to inspect.
/// @return true when @p policy is valid.
static bool masm32_sim_wasm_procedure_fallthrough_policy_is_valid(Masm32SimWasmProcedureFallthroughPolicy policy) {
    return policy == MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_OFF ||
        policy == MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN ||
        policy == MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_ERROR;
}

/// Maps a browser-facing procedure-fallthrough policy to the core VM setting.
///
/// @param policy Browser-facing procedure-fallthrough policy.
/// @return Core VM policy; invalid input maps to the default warning policy.
static VmProcedureFallthroughPolicy masm32_sim_wasm_map_procedure_fallthrough_policy(Masm32SimWasmProcedureFallthroughPolicy policy) {
    switch (policy) {
        case MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_OFF:
            return VM_PROCEDURE_FALLTHROUGH_POLICY_OFF;
        case MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_ERROR:
            return VM_PROCEDURE_FALLTHROUGH_POLICY_ERROR;
        case MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN:
        default:
            return VM_PROCEDURE_FALLTHROUGH_POLICY_WARN;
    }
}

/// Returns the source-run JSON value for a procedure-fallthrough policy.
///
/// @param policy Procedure-fallthrough policy to serialize.
/// @return Stable setting string for accepted modes.
static const char *masm32_sim_wasm_procedure_fallthrough_policy_name(Masm32SimWasmProcedureFallthroughPolicy policy) {
    switch (policy) {
        case MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_OFF:
            return "off";
        case MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_ERROR:
            return "error";
        case MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN:
        default:
            return "warn";
    }
}

/// Returns the Phase 71D default procedure-fallthrough policy.
///
/// @return Default warning policy.
static Masm32SimWasmProcedureFallthroughPolicy masm32_sim_wasm_default_procedure_fallthrough_policy(void) {
    return MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN;
}

/// Returns whether a Phase 71E entry procedure end mode enum value is accepted.
///
/// @param mode Entry procedure end mode value to inspect.
/// @return true when @p mode is valid.
static bool masm32_sim_wasm_entry_procedure_end_mode_is_valid(Masm32SimWasmEntryProcedureEndMode mode) {
    return mode == MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM ||
        mode == MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END;
}

/// Maps a browser-facing entry procedure end mode to the core VM setting.
///
/// @param mode Browser-facing entry procedure end mode.
/// @return Core VM mode; invalid input maps to the default code-stream mode.
static VmEntryProcedureEndMode masm32_sim_wasm_map_entry_procedure_end_mode(Masm32SimWasmEntryProcedureEndMode mode) {
    if (mode == MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END) {
        return VM_ENTRY_PROCEDURE_END_MODE_STOP_AT_ENTRY_END;
    }
    return VM_ENTRY_PROCEDURE_END_MODE_CODE_STREAM;
}

/// Returns the source-run JSON value for an entry procedure end mode.
///
/// @param mode Entry procedure end mode to serialize.
/// @return Stable setting string for accepted modes.
static const char *masm32_sim_wasm_entry_procedure_end_mode_name(Masm32SimWasmEntryProcedureEndMode mode) {
    if (mode == MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END) {
        return "stop-at-entry-end";
    }
    return "code-stream";
}

/// Returns the Phase 71E default entry procedure end mode.
///
/// @return Default code-stream mode.
static Masm32SimWasmEntryProcedureEndMode masm32_sim_wasm_default_entry_procedure_end_mode(void) {
    return MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM;
}

/// Returns whether a Phase 72 call-depth-limit value is accepted.
///
/// @param limit Source-run call-depth-limit value to inspect.
/// @return true when @p limit is in the inclusive Phase 72 range.
static bool masm32_sim_wasm_call_depth_limit_is_valid(uint32_t limit) {
    return limit >= (uint32_t)MASM32_SIM_WASM_MIN_CALL_DEPTH_LIMIT &&
        limit <= (uint32_t)MASM32_SIM_WASM_MAX_CALL_DEPTH_LIMIT;
}

/// Returns whether the next instruction is a strict-mode root-code-stream RET.
///
/// Source-run performs some teaching checks before calling @ref vm_step. This
/// helper mirrors the Phase 71 root RET eligibility rules closely enough to
/// prevent those pre-step checks from treating strict-mode root RET as an
/// ordinary helper RET stack read. The core executor remains authoritative for
/// the final runtime diagnostic.
///
/// @param vm VM whose next instruction and procedure metadata should be inspected.
/// @return true when strict root RET handling should stop before planned stack-read checks.
static bool masm32_sim_wasm_next_instruction_is_strict_root_ret(const Vm *vm) {
    const VmExecProcedureBoundary *root = NULL;

    if (vm == NULL || vm->root_ret_mode != VM_ROOT_RET_MODE_STRICT_CALL_FRAME ||
        vm->program == NULL || vm->instruction_pointer >= vm->program_count ||
        vm->program[vm->instruction_pointer].opcode != VM_IR_OPCODE_RET ||
        !vm->has_selected_entry_procedure || vm->active_helper_return_count != 0U ||
        vm->selected_entry_procedure_index >= vm->procedure_boundary_count) {
        return false;
    }

    root = &vm->procedure_boundaries[vm->selected_entry_procedure_index];
    if (!root->is_selected_entry ||
        root->start_instruction_index > root->end_instruction_index ||
        root->end_instruction_index > vm->program_count) {
        return false;
    }

    return vm->root_code_stream_active ||
        (vm->instruction_pointer >= root->start_instruction_index &&
        vm->instruction_pointer < root->end_instruction_index);
}

/// Advances the deterministic Phase 57G uninitialized-storage visible-byte stream.
///
/// This portable mixer intentionally mirrors the Phase 57F startup-state stream
/// without using host `rand()`, wall-clock state, process memory, or browser
/// nondeterminism. Keeping it local to source-run startup means generated bytes
/// do not create simulated memory-change rows.
///
/// @param state Mutable 32-bit stream state.
/// @return Next deterministic 32-bit value, or zero for a NULL state pointer.
static uint32_t masm32_sim_wasm_seeded_startup_next_u32(uint32_t *state) {
    uint32_t value = 0U;

    if (state == NULL) {
        return 0U;
    }

    *state += 0x9E3779B9U;
    value = *state;
    value = (value ^ (value >> 16U)) * 0x85EBCA6BU;
    value = (value ^ (value >> 13U)) * 0xC2B2AE35U;
    value ^= value >> 16U;
    return value;
}

/// Applies seeded visible bytes to one uninitialized-origin section image.
///
/// The initialization mask remains unchanged: only bytes whose mask value is 0
/// receive deterministic seeded visible values. Initialized bytes keep their
/// parser-encoded values.
///
/// @param image Section image bytes to mutate.
/// @param image_capacity Number of bytes available in @p image.
/// @param initialized_mask Per-byte initialized-state mask.
/// @param mask_size Number of bytes covered by @p initialized_mask.
/// @param state Mutable deterministic startup stream state.
/// @param value Cached 32-bit stream value.
/// @param byte_index Next byte index within @p value.
static void masm32_sim_wasm_seed_uninitialized_storage_image(
    uint8_t *image,
    size_t image_capacity,
    const uint8_t *initialized_mask,
    size_t mask_size,
    uint32_t *state,
    uint32_t *value,
    uint8_t *byte_index
) {
    size_t index = 0U;

    if (image == NULL || initialized_mask == NULL || state == NULL || value == NULL || byte_index == NULL) {
        return;
    }

    for (index = 0U; index < mask_size && index < image_capacity; index += 1U) {
        if (initialized_mask[index] != MASM32_SIM_WASM_DATA_BYTE_UNINITIALIZED) {
            continue;
        }
        if (*byte_index >= 4U) {
            *value = masm32_sim_wasm_seeded_startup_next_u32(state);
            *byte_index = 0U;
        }
        image[index] = (uint8_t)((*value >> (8U * (*byte_index))) & 0xFFU);
        *byte_index = (uint8_t)(*byte_index + 1U);
    }
}

/// Applies Phase 57G/57I seeded visible bytes to uninitialized-origin storage.
///
/// Phase 57G introduced seeded visible bytes for `.data`/`.DATA?`
/// uninitialized-origin storage. Phase 57I extends the same stream to accepted
/// `.CONST ?` and `.CONST DUP(?)` bytes while preserving read-only protection.
///
/// @param storage Source-run storage containing section images and initialization masks.
/// @param seed Deterministic startup-state seed.
static void masm32_sim_wasm_seed_uninitialized_storage_visible_bytes(Masm32SimWasmRunStorage *storage, uint32_t seed) {
    uint32_t state = seed;
    uint32_t value = 0U;
    uint8_t byte_index = 4U;

    if (storage == NULL) {
        return;
    }

    masm32_sim_wasm_seed_uninitialized_storage_image(
        storage->data_image,
        (size_t)MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES,
        storage->data_initialized_mask,
        storage->data_initialized_mask_size,
        &state,
        &value,
        &byte_index
    );
    masm32_sim_wasm_seed_uninitialized_storage_image(
        storage->const_image,
        (size_t)MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES,
        storage->const_initialized_mask,
        storage->const_initialized_mask_size,
        &state,
        &value,
        &byte_index
    );
}

/// Returns the startup-state notice message for Phase 57F and Phase 57G startup modes.
///
/// @param register_flag_mode Register/flag startup mode associated with the run.
/// @param uninitialized_mode Uninitialized-storage visible-byte mode associated with the run.
/// @return Stable user-facing notice text.
static const char *masm32_sim_wasm_startup_state_notice_message(
    Masm32SimWasmStartupRegisterFlagMode register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_mode
) {
    bool seeded_registers = register_flag_mode == MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM;
    bool seeded_uninitialized = uninitialized_mode == MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM;

    if (seeded_registers && seeded_uninitialized) {
        return MASM32_SIM_WASM_STARTUP_STATE_SEEDED_REGISTER_AND_UNINITIALIZED_NOTICE_MESSAGE;
    }
    if (seeded_registers) {
        return MASM32_SIM_WASM_STARTUP_STATE_SEEDED_REGISTER_NOTICE_MESSAGE;
    }
    if (seeded_uninitialized) {
        return MASM32_SIM_WASM_STARTUP_STATE_SEEDED_UNINITIALIZED_NOTICE_MESSAGE;
    }

    return MASM32_SIM_WASM_STARTUP_STATE_ZERO_NOTICE_MESSAGE;
}

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

/// Appends one simulator message object with optional source span and procedure metadata.
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
/// @param procedure_name Optional responsible procedure name to serialize.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_message_with_span_and_procedure(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const char *code,
    const char *message,
    uint32_t line,
    uint32_t column,
    size_t byte_offset,
    size_t span_length,
    bool has_source_span,
    const char *procedure_name
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
    if (procedure_name != NULL && procedure_name[0] != '\0') {
        if (!masm32_sim_json_append(writer, ",\"procedure\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, procedure_name)) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
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
    return masm32_sim_json_append_message_with_span_and_procedure(
        writer,
        kind,
        code,
        message,
        line,
        column,
        byte_offset,
        span_length,
        has_source_span,
        NULL
    );
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

/// Appends the source-less startup-state notice to Simulator Messages.
///
/// The notice is emitted first for Phase 69B runs that begin execution and opt
/// into startup-state notices. It remains an ordinary structured message; blank
/// group separators are added only by the browser/Node formatter.
///
/// @param writer Writer to mutate.
/// @param inout_has_message Whether a prior message has already been appended.
/// @param startup_register_flag_mode Active register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Active uninitialized-byte mode.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_startup_state_notice(
    Masm32SimJsonWriter *writer,
    bool *inout_has_message,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode
) {
    if (writer == NULL || inout_has_message == NULL) {
        return false;
    }
    if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
        return false;
    }
    if (!masm32_sim_json_append_message(
            writer,
            "simulator-notice",
            MASM32_SIM_WASM_STARTUP_STATE_NOTICE_CODE,
            masm32_sim_wasm_startup_state_notice_message(startup_register_flag_mode, uninitialized_storage_visible_byte_mode),
            0U,
            0U
        )) {
        return false;
    }
    *inout_has_message = true;
    return true;
}

/// Appends source-run compatibility metadata shared by every JSON result.
///
/// @param writer JSON writer to mutate.
/// @return true when metadata fit without overflowing the buffer.
static bool masm32_sim_json_append_source_run_metadata(Masm32SimJsonWriter *writer);

/// Builds a renderable invalid Phase 71A root RET mode JSON result.
///
/// @param root_ret_mode Invalid root RET mode value supplied by the caller.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_invalid_root_ret_mode_json(Masm32SimWasmRootRetMode root_ret_mode) {
    Masm32SimJsonWriter writer;
    const char *accepted_values = "masm32-compatible, strict-call-frame";
    char message[224];

    (void)snprintf(
        message,
        sizeof(message),
        "Invalid rootRetMode setting value %d. Accepted values: masm32-compatible, strict-call-frame.",
        (int)root_ret_mode
    );

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append_source_run_metadata(&writer);
    (void)masm32_sim_json_append(
        &writer,
        ",\"ok\":false,\"status\":\"invalid-argument\",\"instructionCount\":0,\"instructionLimit\":%u,\"rootRetMode\":\"masm32-compatible\",\"procedureFallthroughPolicy\":\"warn\",\"entryProcedureEndMode\":\"code-stream\",\"callDepthLimit\":%u,\"memoryChanges\":[],\"simulatorMessages\":[",
        (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
    );
    (void)masm32_sim_json_append_message(&writer, "ui-error", "invalid-root-ret-mode-setting", message, 0U, 0U);
    (void)masm32_sim_json_append(&writer, "],\"invalidSetting\":{");
    (void)masm32_sim_json_append(&writer, "\"setting\":\"rootRetMode\",\"value\":%d,\"acceptedValues\":", (int)root_ret_mode);
    (void)masm32_sim_json_append_string(&writer, accepted_values);
    (void)masm32_sim_json_append(&writer, "}}");

    if (writer.overflowed) {
        snprintf(
            g_masm32_sim_wasm_run_json,
            sizeof(g_masm32_sim_wasm_run_json),
            "{\"phase\":%u,\"phaseSuffix\":\"%s\",\"phaseName\":\"%s\",\"sourceRunOutputContract\":\"%s\",\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"instructionLimit\":%u,\"rootRetMode\":\"masm32-compatible\",\"procedureFallthroughPolicy\":\"warn\",\"entryProcedureEndMode\":\"code-stream\",\"callDepthLimit\":%u,\"memoryChanges\":[],\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER,
            MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX,
            MASM32_SIM_WASM_RUNTIME_PHASE_NAME,
            MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
        );
    }

    return g_masm32_sim_wasm_run_json;
}

/// Builds a renderable invalid Phase 71D procedure-fallthrough policy JSON result.
///
/// @param policy Invalid procedure-fallthrough policy value supplied by the caller.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_invalid_procedure_fallthrough_policy_json(Masm32SimWasmProcedureFallthroughPolicy policy) {
    Masm32SimJsonWriter writer;
    const char *accepted_values = "off, warn, error";
    char message[224];

    (void)snprintf(
        message,
        sizeof(message),
        "Invalid procedureFallthroughPolicy setting value %d. Accepted values: off, warn, error.",
        (int)policy
    );

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append_source_run_metadata(&writer);
    (void)masm32_sim_json_append(
        &writer,
        ",\"ok\":false,\"status\":\"invalid-argument\",\"instructionCount\":0,\"instructionLimit\":%u,\"rootRetMode\":\"masm32-compatible\",\"procedureFallthroughPolicy\":\"warn\",\"entryProcedureEndMode\":\"code-stream\",\"callDepthLimit\":%u,\"memoryChanges\":[],\"simulatorMessages\":[",
        (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
    );
    (void)masm32_sim_json_append_message(&writer, "ui-error", "invalid-procedure-fallthrough-policy-setting", message, 0U, 0U);
    (void)masm32_sim_json_append(&writer, "],\"invalidSetting\":{");
    (void)masm32_sim_json_append(&writer, "\"setting\":\"procedureFallthroughPolicy\",\"value\":%d,\"acceptedValues\":", (int)policy);
    (void)masm32_sim_json_append_string(&writer, accepted_values);
    (void)masm32_sim_json_append(&writer, "}}");

    if (writer.overflowed) {
        snprintf(
            g_masm32_sim_wasm_run_json,
            sizeof(g_masm32_sim_wasm_run_json),
            "{\"phase\":%u,\"phaseSuffix\":\"%s\",\"phaseName\":\"%s\",\"sourceRunOutputContract\":\"%s\",\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"instructionLimit\":%u,\"rootRetMode\":\"masm32-compatible\",\"procedureFallthroughPolicy\":\"warn\",\"entryProcedureEndMode\":\"code-stream\",\"callDepthLimit\":%u,\"memoryChanges\":[],\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER,
            MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX,
            MASM32_SIM_WASM_RUNTIME_PHASE_NAME,
            MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
        );
    }

    return g_masm32_sim_wasm_run_json;
}

/// Builds a renderable invalid Phase 71E entry procedure end mode JSON result.
///
/// @param mode Invalid entry procedure end mode supplied by the caller.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_invalid_entry_procedure_end_mode_json(Masm32SimWasmEntryProcedureEndMode mode) {
    Masm32SimJsonWriter writer;
    const char *accepted_values = "code-stream, stop-at-entry-end";
    char message[224];

    (void)snprintf(
        message,
        sizeof(message),
        "Invalid entryProcedureEndMode setting value %d. Accepted values: code-stream, stop-at-entry-end.",
        (int)mode
    );

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append_source_run_metadata(&writer);
    (void)masm32_sim_json_append(
        &writer,
        ",\"ok\":false,\"status\":\"invalid-argument\",\"instructionCount\":0,\"instructionLimit\":%u,\"rootRetMode\":\"masm32-compatible\",\"procedureFallthroughPolicy\":\"warn\",\"entryProcedureEndMode\":\"code-stream\",\"callDepthLimit\":%u,\"memoryChanges\":[],\"simulatorMessages\":[",
        (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
    );
    (void)masm32_sim_json_append_message(&writer, "ui-error", "invalid-entry-procedure-end-mode-setting", message, 0U, 0U);
    (void)masm32_sim_json_append(&writer, "],\"invalidSetting\":{");
    (void)masm32_sim_json_append(&writer, "\"setting\":\"entryProcedureEndMode\",\"value\":%d,\"acceptedValues\":", (int)mode);
    (void)masm32_sim_json_append_string(&writer, accepted_values);
    (void)masm32_sim_json_append(&writer, "}}");

    if (writer.overflowed) {
        snprintf(
            g_masm32_sim_wasm_run_json,
            sizeof(g_masm32_sim_wasm_run_json),
            "{\"phase\":%u,\"phaseSuffix\":\"%s\",\"phaseName\":\"%s\",\"sourceRunOutputContract\":\"%s\",\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"instructionLimit\":%u,\"rootRetMode\":\"masm32-compatible\",\"procedureFallthroughPolicy\":\"warn\",\"entryProcedureEndMode\":\"code-stream\",\"callDepthLimit\":%u,\"memoryChanges\":[],\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER,
            MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX,
            MASM32_SIM_WASM_RUNTIME_PHASE_NAME,
            MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
        );
    }

    return g_masm32_sim_wasm_run_json;
}


/// Appends source-run compatibility metadata shared by every JSON result.
///
/// The numeric phase fields remain tied to runtime/source-run MASM behavior.
/// The output-contract field is separate artifact metadata used by the browser
/// protocol to detect stale Wasm output formatting without advancing runtime
/// behavior metadata.
///
/// @param writer JSON writer to mutate.
/// @return true when the metadata append completed without a detected overflow.
static bool masm32_sim_json_append_source_run_metadata(Masm32SimJsonWriter *writer) {
    if (writer == NULL) {
        return false;
    }

    if (!masm32_sim_json_append(
            writer,
            "{\"phase\":%u,\"phaseSuffix\":",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER
        )) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"phaseName\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, MASM32_SIM_WASM_RUNTIME_PHASE_NAME)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"sourceRunOutputContract\":")) {
        return false;
    }
    return masm32_sim_json_append_string(writer, MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT);
}

/// Appends source-run compatibility metadata shared by every JSON result.
///
/// @param writer JSON writer to mutate.
/// @return true when metadata fit without overflowing the buffer.
static bool masm32_sim_json_append_source_run_metadata(Masm32SimJsonWriter *writer);

/// Builds a renderable invalid startup-setting JSON result.
///
/// @param setting_name Setting key that failed validation.
/// @param message Human-readable setting diagnostic message.
/// @param accepted_values Human-readable accepted value list.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_invalid_startup_setting_json(
    const char *setting_name,
    const char *message,
    const char *accepted_values
) {
    Masm32SimJsonWriter writer;

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append_source_run_metadata(&writer);
    (void)masm32_sim_json_append(
        &writer,
        ",\"ok\":false,\"status\":\"invalid-argument\",\"instructionCount\":0,\"memoryChanges\":[],\"simulatorMessages\":["
    );
    (void)masm32_sim_json_append_message(&writer, "ui-error", "invalid-startup-setting", message, 0U, 0U);
    (void)masm32_sim_json_append(&writer, "],\"invalidSetting\":{");
    (void)masm32_sim_json_append(&writer, "\"setting\":");
    (void)masm32_sim_json_append_string(&writer, setting_name);
    (void)masm32_sim_json_append(&writer, ",\"acceptedValues\":");
    (void)masm32_sim_json_append_string(&writer, accepted_values);
    (void)masm32_sim_json_append(&writer, "}}");

    if (writer.overflowed) {
        snprintf(
            g_masm32_sim_wasm_run_json,
            sizeof(g_masm32_sim_wasm_run_json),
            "{\"phase\":%u,\"phaseSuffix\":\"%s\",\"phaseName\":\"%s\",\"sourceRunOutputContract\":\"%s\",\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"memoryChanges\":[],\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER,
            MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX,
            MASM32_SIM_WASM_RUNTIME_PHASE_NAME,
            MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT
        );
    }

    return g_masm32_sim_wasm_run_json;
}


/// Builds a renderable invalid instruction-limit setting JSON result.
///
/// @param instruction_limit Invalid limit value supplied by the caller.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_invalid_instruction_limit_json(uint32_t instruction_limit) {
    Masm32SimJsonWriter writer;
    const char *accepted_values = "positive integer from 1 to 4294967295";
    char message[192];

    (void)snprintf(
        message,
        sizeof(message),
        "Invalid source-run setting 'instructionLimit'. Accepted values: %s.",
        accepted_values
    );

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append_source_run_metadata(&writer);
    (void)masm32_sim_json_append(
        &writer,
        ",\"ok\":false,\"status\":\"invalid-argument\",\"instructionCount\":0,\"instructionLimit\":%u,\"callDepthLimit\":%u,\"executedInstructionCount\":0,\"attemptedNextInstructionIndex\":null,\"currentInstructionIndex\":null,\"memoryChanges\":[],\"simulatorMessages\":[",
        (unsigned int)instruction_limit,
        (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
    );
    (void)masm32_sim_json_append_message(&writer, "ui-error", "invalid-instruction-limit-setting", message, 0U, 0U);
    (void)masm32_sim_json_append(&writer, "],\"invalidSetting\":{");
    (void)masm32_sim_json_append(&writer, "\"setting\":\"instructionLimit\",\"value\":%u,\"acceptedValues\":", (unsigned int)instruction_limit);
    (void)masm32_sim_json_append_string(&writer, accepted_values);
    (void)masm32_sim_json_append(&writer, "}}");

    if (writer.overflowed) {
        (void)snprintf(
            g_masm32_sim_wasm_run_json,
            sizeof(g_masm32_sim_wasm_run_json),
            "{\"phase\":%u,\"phaseSuffix\":\"%s\",\"phaseName\":\"%s\",\"sourceRunOutputContract\":\"%s\",\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"instructionLimit\":%u,\"callDepthLimit\":%u,\"executedInstructionCount\":0,\"attemptedNextInstructionIndex\":null,\"currentInstructionIndex\":null,\"memoryChanges\":[],\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER,
            MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX,
            MASM32_SIM_WASM_RUNTIME_PHASE_NAME,
            MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT,
            (unsigned int)instruction_limit,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
        );
    }

    return g_masm32_sim_wasm_run_json;
}

/// Builds a renderable invalid Phase 72 call-depth-limit setting JSON result.
///
/// @param call_depth_limit Invalid limit value supplied by the caller.
/// @return Pointer to the static JSON response buffer.
static const char *masm32_sim_wasm_build_invalid_call_depth_limit_json(uint32_t call_depth_limit) {
    Masm32SimJsonWriter writer;
    const char *accepted_values = "1..4096";
    char message[224];

    (void)snprintf(
        message,
        sizeof(message),
        "Invalid source-run setting 'callDepthLimit' value %u. Accepted values: 1..4096.",
        (unsigned int)call_depth_limit
    );

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append_source_run_metadata(&writer);
    (void)masm32_sim_json_append(
        &writer,
        ",\"ok\":false,\"status\":\"invalid-argument\",\"instructionCount\":0,\"instructionLimit\":%u,\"callDepthLimit\":%u,\"executedInstructionCount\":0,\"attemptedNextInstructionIndex\":null,\"currentInstructionIndex\":null,\"memoryChanges\":[],\"simulatorMessages\":[",
        (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        (unsigned int)call_depth_limit
    );
    (void)masm32_sim_json_append_message(&writer, "settings-error", "invalid-call-depth-limit", message, 0U, 0U);
    (void)masm32_sim_json_append(&writer, "],\"invalidSetting\":{");
    (void)masm32_sim_json_append(&writer, "\"setting\":\"callDepthLimit\",\"value\":%u,\"acceptedValues\":", (unsigned int)call_depth_limit);
    (void)masm32_sim_json_append_string(&writer, accepted_values);
    (void)masm32_sim_json_append(&writer, "}}");

    if (writer.overflowed) {
        (void)snprintf(
            g_masm32_sim_wasm_run_json,
            sizeof(g_masm32_sim_wasm_run_json),
            "{\"phase\":%u,\"phaseSuffix\":\"%s\",\"phaseName\":\"%s\",\"sourceRunOutputContract\":\"%s\",\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"instructionLimit\":%u,\"callDepthLimit\":%u,\"executedInstructionCount\":0,\"attemptedNextInstructionIndex\":null,\"currentInstructionIndex\":null,\"memoryChanges\":[],\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER,
            MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX,
            MASM32_SIM_WASM_RUNTIME_PHASE_NAME,
            MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT,
            (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
            (unsigned int)call_depth_limit
        );
    }

    return g_masm32_sim_wasm_run_json;
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


/// Appends register-family write metadata used by final-register display markers.
///
/// @param writer Writer to mutate.
/// @param cpu CPU state to inspect.
/// @return true when the metadata object fit without overflowing the buffer.
static bool masm32_sim_json_append_register_write_metadata(Masm32SimJsonWriter *writer, const VmCpu *cpu) {
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

    if (writer == NULL || cpu == NULL) {
        return false;
    }

    if (!masm32_sim_json_append(writer, "\"registerWrites\":{")) {
        return false;
    }

    for (index = 0U; index < sizeof(registers) / sizeof(registers[0]); index += 1U) {
        bool was_written = false;
        const char *name = vm_cpu_register_name(registers[index]);

        if (name == NULL || !vm_cpu_register_family_was_written(cpu, registers[index], &was_written)) {
            return false;
        }

        if (index != 0U && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, name)) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ":%s", was_written ? "true" : "false")) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
}


/// Appends register display-role metadata used by final-register rendering.
///
/// @param writer Writer to mutate.
/// @return true when the metadata object fit without overflowing the buffer.
static bool masm32_sim_json_append_register_role_metadata(Masm32SimJsonWriter *writer) {
    if (writer == NULL) {
        return false;
    }

    return masm32_sim_json_append(writer, "\"registerRoles\":{\"EIP\":\"derived-control-state\",\"ESP\":\"stack-pointer\"}");
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
    if (!masm32_sim_json_append(writer, ",\"newUnsigned\":%u", (unsigned int)change->new_value)) {
        return false;
    }
    if (change->source_line > 0U) {
        if (!masm32_sim_json_append(writer, ",\"sourceLine\":%u", (unsigned int)change->source_line)) {
            return false;
        }
    }
    if (change->source_text != NULL && change->source_text[0] != '\0') {
        if (!masm32_sim_json_append(writer, ",\"sourceText\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, change->source_text)) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
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

/// Collects a symbol-aware memory change for a specific instruction operand.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM containing the latest execution delta and memory state.
/// @param parser_result Parser result containing symbol count.
/// @param delta Latest execution delta containing changed memory bytes.
/// @param operand Instruction operand that may identify the written memory range.
/// @return true when a symbolic memory-change row was appended.
static bool masm32_sim_wasm_collect_memory_change_for_operand(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    const VmParserResult *parser_result,
    const VmExecDelta *delta,
    const VmIrOperand *operand
) {
    const VmSymbol *symbol = NULL;
    uint32_t write_address = 0U;
    uint8_t width_bytes = 0U;
    uint8_t old_bytes[4] = {0U, 0U, 0U, 0U};
    uint8_t new_bytes[4] = {0U, 0U, 0U, 0U};
    uint8_t byte_index = 0U;
    size_t change_index = 0U;
    bool has_changed_byte = false;

    if (storage == NULL || vm == NULL || parser_result == NULL || delta == NULL || operand == NULL ||
        storage->memory_change_count >= (size_t)MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES) {
        return false;
    }

    if ((operand->kind != VM_IR_OPERAND_MEMORY_ADDRESS && operand->kind != VM_IR_OPERAND_MEMORY_REGISTER) ||
        (operand->width_bits != 8U && operand->width_bits != 16U && operand->width_bits != 32U)) {
        return false;
    }

    if (!masm32_sim_wasm_delta_write_address(delta, &write_address)) {
        return false;
    }

    width_bytes = (uint8_t)(operand->width_bits / 8U);
    symbol = vm_symbol_find_by_address(storage->symbols, parser_result->symbol_count, write_address);
    if (symbol == NULL || write_address + width_bytes > symbol->address + symbol->size_bytes) {
        return false;
    }

    for (byte_index = 0U; byte_index < width_bytes; byte_index += 1U) {
        uint8_t current_byte = 0U;
        (void)vm_memory_read_u8(&vm->memory, write_address + byte_index, &current_byte, NULL);
        old_bytes[byte_index] = current_byte;
        new_bytes[byte_index] = current_byte;
    }

    for (change_index = 0U; change_index < delta->memory_change_count; change_index += 1U) {
        const VmMemoryByteChange *byte_change = &delta->memory_changes[change_index];
        if (byte_change->address >= write_address && byte_change->address < write_address + width_bytes) {
            uint8_t relative = (uint8_t)(byte_change->address - write_address);
            old_bytes[relative] = byte_change->old_value;
            new_bytes[relative] = byte_change->new_value;
            has_changed_byte = true;
        }
    }

    if (has_changed_byte) {
        Masm32SimSymbolicMemoryChange *change = &storage->memory_changes[storage->memory_change_count];
        change->symbol_name = symbol->name;
        change->data_type_name = operand->width_bits == 8U ? "BYTE" : (operand->width_bits == 16U ? "WORD" : "DWORD");
        change->address = write_address;
        change->byte_offset = write_address - symbol->address;
        change->has_element_index = symbol->element_size_bytes != 0U && (change->byte_offset % (uint32_t)symbol->element_size_bytes) == 0U;
        change->element_index = change->has_element_index ? change->byte_offset / (uint32_t)symbol->element_size_bytes : 0U;
        change->width_bits = operand->width_bits;
        change->old_value = masm32_sim_wasm_decode_u32(old_bytes, width_bytes);
        change->new_value = masm32_sim_wasm_decode_u32(new_bytes, width_bytes);
        change->source_line = delta->instruction.source_line;
        change->source_text = delta->instruction.source_text;
        storage->memory_change_count += 1U;
        return true;
    }

    return false;
}

/// Collects the source-level PUSH stack-write memory change from the last-step delta.
///
/// Internal CALL return-token writes remain intentionally hidden from public
/// source-run memory-change rows. Phase 72A exposes only explicit source-level
/// PUSH stack writes through this helper.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM containing the latest execution delta and memory state.
/// @param delta Latest execution delta containing changed memory bytes.
/// @return true when a stack memory-change row was appended.
static bool masm32_sim_wasm_collect_stack_push_memory_change(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    const VmExecDelta *delta
) {
    const VmMemoryRegion *stack_region = NULL;
    uint32_t write_address = 0U;
    uint8_t old_bytes[4] = {0U, 0U, 0U, 0U};
    uint8_t new_bytes[4] = {0U, 0U, 0U, 0U};
    uint8_t byte_index = 0U;
    size_t change_index = 0U;
    bool has_changed_byte = false;

    if (storage == NULL || vm == NULL || delta == NULL || delta->instruction.opcode != VM_IR_OPCODE_PUSH ||
        storage->memory_change_count >= (size_t)MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES ||
        !masm32_sim_wasm_delta_write_address(delta, &write_address)) {
        return false;
    }

    stack_region = vm_memory_get_region(&vm->memory, VM_MEMORY_REGION_STACK);
    if (stack_region == NULL || write_address < stack_region->base || write_address + 4U > stack_region->base + stack_region->size) {
        return false;
    }

    for (byte_index = 0U; byte_index < 4U; byte_index += 1U) {
        uint8_t current_byte = 0U;
        (void)vm_memory_read_u8(&vm->memory, write_address + byte_index, &current_byte, NULL);
        old_bytes[byte_index] = current_byte;
        new_bytes[byte_index] = current_byte;
    }

    for (change_index = 0U; change_index < delta->memory_change_count; change_index += 1U) {
        const VmMemoryByteChange *byte_change = &delta->memory_changes[change_index];
        if (byte_change->address >= write_address && byte_change->address < write_address + 4U) {
            uint8_t relative = (uint8_t)(byte_change->address - write_address);
            old_bytes[relative] = byte_change->old_value;
            new_bytes[relative] = byte_change->new_value;
            has_changed_byte = true;
        }
    }

    if (has_changed_byte) {
        Masm32SimSymbolicMemoryChange *change = &storage->memory_changes[storage->memory_change_count];
        change->symbol_name = "stack";
        change->data_type_name = "DWORD";
        change->address = write_address;
        change->byte_offset = write_address - stack_region->base;
        change->has_element_index = false;
        change->element_index = 0U;
        change->width_bits = 32U;
        change->old_value = masm32_sim_wasm_decode_u32(old_bytes, 4U);
        change->new_value = masm32_sim_wasm_decode_u32(new_bytes, 4U);
        change->source_line = delta->instruction.source_line;
        change->source_text = delta->instruction.source_text;
        storage->memory_change_count += 1U;
        return true;
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

    if (storage == NULL || vm == NULL || parser_result == NULL || storage->memory_change_count >= (size_t)MASM32_SIM_WASM_MAX_SYMBOLIC_MEMORY_CHANGES) {
        return;
    }

    delta = vm_last_delta(vm);
    if (delta == NULL || !delta->has_instruction || delta->memory_change_count == 0U) {
        return;
    }

    if (masm32_sim_wasm_collect_stack_push_memory_change(storage, vm, delta)) {
        return;
    }

    if (masm32_sim_wasm_collect_memory_change_for_operand(storage, vm, parser_result, delta, &delta->instruction.destination)) {
        return;
    }

    (void)masm32_sim_wasm_collect_memory_change_for_operand(storage, vm, parser_result, delta, &delta->instruction.source);
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

/// Returns whether a section-validation policy performs any checking.
///
/// @param policy Section-validation policy to inspect.
/// @return true when the policy is warning or strict.
static bool masm32_sim_wasm_section_policy_enabled(Masm32SimWasmSectionValidationPolicy policy) {
    return policy == MASM32_SIM_WASM_SECTION_VALIDATION_WARN || policy == MASM32_SIM_WASM_SECTION_VALIDATION_STRICT;
}

/// Returns whether a section-validation policy stops before mutation.
///
/// @param policy Section-validation policy to inspect.
/// @return true when the policy is strict.
static bool masm32_sim_wasm_section_policy_is_strict(Masm32SimWasmSectionValidationPolicy policy) {
    return policy == MASM32_SIM_WASM_SECTION_VALIDATION_STRICT;
}

/// Returns the diagnostic code for one section-boundary level.
///
/// @param level Section-boundary validation level.
/// @return Stable diagnostic code.
static const char *masm32_sim_wasm_section_boundary_code(Masm32SimWasmSectionBoundaryLevel level) {
    return level == MASM32_SIM_WASM_SECTION_BOUNDARY_CAPACITY
        ? "section-capacity-violation"
        : "section-image-violation";
}

/// Returns the user-facing name for one section-boundary level.
///
/// @param level Section-boundary validation level.
/// @return Static lowercase level name.
static const char *masm32_sim_wasm_section_boundary_level_name(Masm32SimWasmSectionBoundaryLevel level) {
    return level == MASM32_SIM_WASM_SECTION_BOUNDARY_CAPACITY ? "section capacity" : "section image";
}

/// Copies a stable section owner name into a fixed diagnostic buffer.
///
/// @param destination Destination character buffer.
/// @param destination_size Destination buffer size in bytes.
/// @param name Static name to copy.
static void masm32_sim_wasm_copy_section_owner_name(char *destination, size_t destination_size, const char *name) {
    if (destination == NULL || destination_size == 0U) {
        return;
    }
    (void)snprintf(destination, destination_size, "%s", name != NULL ? name : "unknown");
}

/// Safely calculates an exclusive limit from a base and size.
///
/// @param base First address in the range.
/// @param size Number of bytes in the range.
/// @param out_limit Receives the exclusive limit on success.
/// @return true when the range is representable.
static bool masm32_sim_wasm_exclusive_limit(uint32_t base, uint32_t size, uint32_t *out_limit) {
    if (out_limit == NULL || size == 0U || base > UINT32_MAX - size) {
        return false;
    }
    *out_limit = base + size;
    return true;
}

/// Copies source span metadata into a section-boundary diagnostic.
///
/// @param diagnostic Section-boundary diagnostic to mutate.
/// @param instruction Instruction associated with the access.
/// @param source Original source text for byte-offset reconstruction.
static void masm32_sim_wasm_copy_section_diagnostic_source_span(
    Masm32SimWasmSectionBoundaryDiagnostic *diagnostic,
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
        span_start = line_text;
        span_end = line_text + strlen(line_text);
    } else {
        span_end = strchr(span_start, ']');
        if (span_end == NULL || span_end < span_start) {
            span_end = span_start + strlen(span_start);
        } else {
            span_end += 1;
        }
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
    diagnostic->source_span_length = (size_t)(span_end - span_start);
    diagnostic->has_source_span = diagnostic->source_span_length > 0U;
}

/// Returns selected data and const image sizes from source-run storage.
///
/// @param storage Source-run storage to inspect.
/// @param region Region whose section image size should be returned.
/// @return Declared image size in bytes, or zero when no image exists.
static uint32_t masm32_sim_wasm_section_image_size(const Masm32SimWasmRunStorage *storage, VmLayoutRegionKind region) {
    if (storage == NULL) {
        return 0U;
    }
    if (region == VM_LAYOUT_REGION_DATA) {
        return storage->data_section_image_size;
    }
    if (region == VM_LAYOUT_REGION_CONST) {
        return storage->const_section_image_size;
    }
    return 0U;
}

/// Returns parser image-buffer capacity for one section-backed region.
///
/// @param region Region whose parser image capacity should be returned.
/// @return Parser image capacity in bytes, or zero when the region is not section-backed.
static uint32_t masm32_sim_wasm_section_capacity_size(VmLayoutRegionKind region) {
    if (region == VM_LAYOUT_REGION_DATA) {
        return (uint32_t)MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES;
    }
    if (region == VM_LAYOUT_REGION_CONST) {
        return (uint32_t)MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES;
    }
    return 0U;
}

/// Finds the selected layout region containing an access start address.
///
/// @param layout_policy Selected layout policy, or NULL for fixed default.
/// @param address Address to classify.
/// @param out_region Receives the containing region kind on success.
/// @param out_base Receives the containing region base on success.
/// @param out_limit Receives the containing region exclusive limit on success.
/// @return true when @p address starts inside a selected region.
static bool masm32_sim_wasm_find_start_region(
    const VmLayoutPolicy *layout_policy,
    uint32_t address,
    VmLayoutRegionKind *out_region,
    uint32_t *out_base,
    uint32_t *out_limit
) {
    VmLayoutPolicy default_policy;
    const VmLayoutPolicy *effective_policy = layout_policy;
    size_t index = 0U;

    if (out_region == NULL || out_base == NULL || out_limit == NULL) {
        return false;
    }
    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    for (index = 0U; index < (size_t)VM_LAYOUT_REGION_COUNT; index += 1U) {
        const VmLayoutRegionPolicy *region = &effective_policy->regions[index];
        if (address >= region->base && address < region->limit) {
            *out_region = (VmLayoutRegionKind)index;
            *out_base = region->base;
            *out_limit = region->limit;
            return true;
        }
    }

    return false;
}

/// Returns a stable source-level section name for one region.
///
/// @param region Layout region to name.
/// @return Section or section-like name.
static const char *masm32_sim_wasm_section_owner_name(VmLayoutRegionKind region) {
    switch (region) {
        case VM_LAYOUT_REGION_DATA:
            return ".data/.DATA?";
        case VM_LAYOUT_REGION_CONST:
            return ".CONST";
        case VM_LAYOUT_REGION_CODE:
            return ".code";
        case VM_LAYOUT_REGION_HEAP:
            return "heap";
        case VM_LAYOUT_REGION_STACK:
            return "stack";
        default:
            return "unknown";
    }
}

/// Determines whether an access violates one Phase 53B section-boundary level.
///
/// @param storage Source-run storage with image sizes.
/// @param layout_policy Selected layout policy, or NULL for fixed default.
/// @param level Section-boundary level to check.
/// @param address First byte of the access.
/// @param size_bytes Access size in bytes.
/// @param out_owner Receives the owning region when known.
/// @param out_boundary_start Receives the relevant boundary start when known.
/// @param out_boundary_end Receives the relevant inclusive boundary end when known.
/// @param out_has_boundary Receives whether boundary fields are meaningful.
/// @return true when the access violates the selected section-boundary level.
static bool masm32_sim_wasm_section_access_violates(
    const Masm32SimWasmRunStorage *storage,
    const VmLayoutPolicy *layout_policy,
    Masm32SimWasmSectionBoundaryLevel level,
    uint32_t address,
    uint32_t size_bytes,
    VmLayoutRegionKind *out_owner,
    uint32_t *out_boundary_start,
    uint32_t *out_boundary_end,
    bool *out_has_boundary
) {
    VmLayoutRegionKind region = VM_LAYOUT_REGION_COUNT;
    uint32_t region_base = 0U;
    uint32_t region_limit = 0U;
    uint32_t boundary_size = 0U;
    uint32_t boundary_limit = 0U;
    uint32_t access_end = 0U;

    if (out_has_boundary != NULL) {
        *out_has_boundary = false;
    }
    if (out_owner != NULL) {
        *out_owner = VM_LAYOUT_REGION_COUNT;
    }
    if (size_bytes == 0U || !vm_object_map_inclusive_end(address, size_bytes, &access_end)) {
        return false;
    }
    if (!masm32_sim_wasm_find_start_region(layout_policy, address, &region, &region_base, &region_limit)) {
        return true;
    }
    if (out_owner != NULL) {
        *out_owner = region;
    }

    if (level == MASM32_SIM_WASM_SECTION_BOUNDARY_CAPACITY) {
        boundary_size = masm32_sim_wasm_section_capacity_size(region);
    } else {
        boundary_size = masm32_sim_wasm_section_image_size(storage, region);
    }

    if (boundary_size == 0U || !masm32_sim_wasm_exclusive_limit(region_base, boundary_size, &boundary_limit)) {
        return true;
    }
    if (boundary_limit > region_limit) {
        boundary_limit = region_limit;
    }
    if (boundary_limit == 0U || boundary_limit <= region_base) {
        return true;
    }

    if (out_boundary_start != NULL) {
        *out_boundary_start = region_base;
    }
    if (out_boundary_end != NULL) {
        *out_boundary_end = boundary_limit - 1U;
    }
    if (out_has_boundary != NULL) {
        *out_has_boundary = true;
    }

    return address < region_base || access_end >= boundary_limit;
}

/// Populates one section-boundary diagnostic.
///
/// @param diagnostic Diagnostic to fill.
/// @param instruction Instruction associated with the access.
/// @param access Checked or planned memory access being diagnosed.
/// @param level Section-boundary level that failed.
/// @param size_bytes Access size in bytes.
/// @param owner Owning region when known.
/// @param boundary_start Relevant boundary start when available.
/// @param boundary_end Relevant inclusive boundary end when available.
/// @param has_boundary Whether boundary fields are meaningful.
/// @param source Original source text for byte-offset reconstruction.
static void masm32_sim_wasm_fill_section_boundary_diagnostic(
    Masm32SimWasmSectionBoundaryDiagnostic *diagnostic,
    const VmIrInstruction *instruction,
    const VmExecMemoryAccess *access,
    Masm32SimWasmSectionBoundaryLevel level,
    uint32_t size_bytes,
    VmLayoutRegionKind owner,
    uint32_t boundary_start,
    uint32_t boundary_end,
    bool has_boundary,
    const char *source
) {
    if (diagnostic == NULL || access == NULL) {
        return;
    }

    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->level = level;
    diagnostic->access_kind = access->kind;
    diagnostic->start_address = access->address;
    diagnostic->size_bytes = size_bytes;
    (void)vm_object_map_inclusive_end(access->address, size_bytes, &diagnostic->end_address);
    if (owner != VM_LAYOUT_REGION_COUNT) {
        masm32_sim_wasm_copy_section_owner_name(diagnostic->owner_name, sizeof(diagnostic->owner_name), masm32_sim_wasm_section_owner_name(owner));
        diagnostic->has_owner = true;
    }
    diagnostic->boundary_start = boundary_start;
    diagnostic->boundary_end = boundary_end;
    diagnostic->has_boundary = has_boundary;
    masm32_sim_wasm_copy_section_diagnostic_source_span(diagnostic, instruction, source);
}

/// Validates one access against one Phase 53B section-boundary policy.
///
/// @param storage Source-run storage to mutate.
/// @param instruction Instruction associated with the access.
/// @param access Memory access to classify.
/// @param policy Selected warning/strict/off policy.
/// @param level Section-boundary level being checked.
/// @param layout_policy Selected layout policy, or NULL for fixed default.
/// @return true when execution may continue, false for strict violations.
static bool masm32_sim_wasm_validate_section_access(
    Masm32SimWasmRunStorage *storage,
    const VmIrInstruction *instruction,
    const VmExecMemoryAccess *access,
    Masm32SimWasmSectionValidationPolicy policy,
    Masm32SimWasmSectionBoundaryLevel level,
    const VmLayoutPolicy *layout_policy
) {
    VmLayoutRegionKind owner = VM_LAYOUT_REGION_COUNT;
    uint32_t boundary_start = 0U;
    uint32_t boundary_end = 0U;
    uint32_t size_bytes = 0U;
    bool has_boundary = false;
    bool violates = false;

    if (storage == NULL || instruction == NULL || access == NULL || !masm32_sim_wasm_section_policy_enabled(policy)) {
        return true;
    }
    if (!masm32_sim_wasm_access_succeeded(access) || access->width_bits == 0U || (access->width_bits % 8U) != 0U) {
        return true;
    }

    size_bytes = (uint32_t)(access->width_bits / 8U);
    violates = masm32_sim_wasm_section_access_violates(
        storage,
        layout_policy,
        level,
        access->address,
        size_bytes,
        &owner,
        &boundary_start,
        &boundary_end,
        &has_boundary
    );
    if (!violates) {
        return true;
    }

    if (masm32_sim_wasm_section_policy_is_strict(policy)) {
        masm32_sim_wasm_fill_section_boundary_diagnostic(
            &storage->section_violation,
            instruction,
            access,
            level,
            size_bytes,
            owner,
            boundary_start,
            boundary_end,
            has_boundary,
            storage->source_text
        );
        storage->has_section_violation = true;
        return false;
    }

    if (storage->section_warning_count < (size_t)MASM32_SIM_WASM_MAX_SECTION_WARNINGS) {
        masm32_sim_wasm_fill_section_boundary_diagnostic(
            &storage->section_warnings[storage->section_warning_count],
            instruction,
            access,
            level,
            size_bytes,
            owner,
            boundary_start,
            boundary_end,
            has_boundary,
            storage->source_text
        );
        storage->section_warning_count += 1U;
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

/// Returns whether an access range is wholly inside the active stack region.
///
/// Allocated-object validation describes declared data objects. Until a later
/// phase defines stack-object metadata, strict declared-object validation must
/// not reject valid implicit stack accesses merely because they are outside
/// `.data`, `.DATA?`, or `.CONST` objects.
///
/// @param layout_policy Selected layout policy, or NULL for fixed default.
/// @param address First byte of the access range.
/// @param size_bytes Number of bytes in the access range.
/// @return true when the range starts and ends inside the stack region.
static bool masm32_sim_wasm_access_range_is_stack(
    const VmLayoutPolicy *layout_policy,
    uint32_t address,
    uint32_t size_bytes
) {
    VmLayoutRegionKind region = VM_LAYOUT_REGION_COUNT;
    uint32_t region_base = 0U;
    uint32_t region_limit = 0U;
    uint32_t access_end = 0U;

    if (size_bytes == 0U || !vm_object_map_inclusive_end(address, size_bytes, &access_end)) {
        return false;
    }
    if (!masm32_sim_wasm_find_start_region(layout_policy, address, &region, &region_base, &region_limit)) {
        return false;
    }

    (void)region_base;
    return region == VM_LAYOUT_REGION_STACK && access_end < region_limit;
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
        const VmExecMemoryAccess *access = &delta->memory_accesses[index];
        uint32_t size_bytes = 0U;

        if (delta->instruction.opcode == VM_IR_OPCODE_CALL || delta->instruction.opcode == VM_IR_OPCODE_RET) {
            continue;
        }
        if (access->width_bits != 0U && (access->width_bits % 8U) == 0U) {
            size_bytes = (uint32_t)(access->width_bits / 8U);
            if (masm32_sim_wasm_access_range_is_stack(layout_policy, access->address, size_bytes)) {
                continue;
            }
        }
        if (!masm32_sim_wasm_validate_object_access(storage, &delta->instruction, access, validation_mode, layout_policy)) {
            return VM_EXEC_STATUS_MEMORY_ERROR;
        }
    }

    return VM_EXEC_STATUS_OK;
}

/// Applies section-boundary warning validation to the last instruction accesses.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose last delta should be inspected.
/// @param capacity_policy Level 2 section-capacity validation behavior.
/// @param image_policy Level 3 section-image validation behavior.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return OK when execution may continue, or MEMORY_ERROR when a strict policy fails.
static VmExecStatus masm32_sim_wasm_validate_section_accesses(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy,
    const VmLayoutPolicy *layout_policy
) {
    const VmExecDelta *delta = NULL;
    size_t index = 0U;

    if (storage == NULL || vm == NULL ||
        (!masm32_sim_wasm_section_policy_enabled(capacity_policy) && !masm32_sim_wasm_section_policy_enabled(image_policy))) {
        return VM_EXEC_STATUS_OK;
    }

    delta = vm_last_delta(vm);
    if (delta == NULL || !delta->has_instruction) {
        return VM_EXEC_STATUS_OK;
    }

    for (index = 0U; index < delta->memory_access_count; index += 1U) {
        if (!masm32_sim_wasm_validate_section_access(storage, &delta->instruction, &delta->memory_accesses[index], capacity_policy, MASM32_SIM_WASM_SECTION_BOUNDARY_CAPACITY, layout_policy)) {
            return VM_EXEC_STATUS_MEMORY_ERROR;
        }
        if (!masm32_sim_wasm_validate_section_access(storage, &delta->instruction, &delta->memory_accesses[index], image_policy, MASM32_SIM_WASM_SECTION_BOUNDARY_IMAGE, layout_policy)) {
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

/// Resolves the effective address for a planned memory access.
///
/// This mirrors the executor's current register-indirect address arithmetic so
/// pre-step diagnostics can inspect the same final address that VM execution
/// would use without broadening VM execution semantics.
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
        case VM_IR_OPCODE_CMP:
        case VM_IR_OPCODE_ADC:
        case VM_IR_OPCODE_SBB:
        case VM_IR_OPCODE_AND:
        case VM_IR_OPCODE_OR:
        case VM_IR_OPCODE_XOR:
        case VM_IR_OPCODE_TEST:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->destination, width_bits);
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->source, width_bits);
            }
            break;
        case VM_IR_OPCODE_NEG:
        case VM_IR_OPCODE_NOT:
        case VM_IR_OPCODE_INC:
        case VM_IR_OPCODE_DEC:
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
        case VM_IR_OPCODE_MUL:
        case VM_IR_OPCODE_IMUL:
        case VM_IR_OPCODE_IMUL_IMMEDIATE:
        case VM_IR_OPCODE_DIV:
        case VM_IR_OPCODE_IDIV:
            if (masm32_sim_wasm_operand_width(&instruction->source, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->source, width_bits);
            }
            break;
        case VM_IR_OPCODE_PUSH:
            if (masm32_sim_wasm_operand_width(&instruction->source, &width_bits)) {
                masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &instruction->source, width_bits);
            }
            break;
        case VM_IR_OPCODE_POP: {
            VmIrOperand stack_read = vm_ir_operand_memory_register(VM_REGISTER_ESP, 0, 0U, 32U);
            masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &stack_read, 32U);
            break;
        }
        case VM_IR_OPCODE_RET: {
            VmIrOperand stack_read = vm_ir_operand_memory_register(VM_REGISTER_ESP, 0, 0U, 32U);
            masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &stack_read, 32U);
            break;
        }
        case VM_IR_OPCODE_LEAVE: {
            VmIrOperand frame_read = vm_ir_operand_memory_register(VM_REGISTER_EBP, 0, 0U, 32U);
            masm32_sim_wasm_add_planned_read(reads, read_capacity, &read_count, &frame_read, 32U);
            break;
        }
        default:
            /* TODO(Phase-owned future instruction milestones): add each future memory-reading opcode here when that opcode is implemented. */
            break;
    }

    return read_count;
}

/// Adds one planned object-validation access when the operand touches memory.
///
/// @param accesses Output planned-access array.
/// @param access_capacity Number of entries available in @p accesses.
/// @param inout_access_count Current count to update.
/// @param operand Operand that may access memory.
/// @param kind Read/write kind for the planned access.
/// @param width_bits Access width in bits.
static void masm32_sim_wasm_add_planned_object_access(
    Masm32SimWasmPlannedMemoryAccess *accesses,
    size_t access_capacity,
    size_t *inout_access_count,
    const VmIrOperand *operand,
    VmExecMemoryAccessKind kind,
    uint8_t width_bits
) {
    if (accesses == NULL || inout_access_count == NULL || operand == NULL || !masm32_sim_wasm_operand_is_memory(operand) ||
        !vm_ir_width_is_supported(width_bits) || *inout_access_count >= access_capacity) {
        return;
    }

    accesses[*inout_access_count].operand = *operand;
    accesses[*inout_access_count].kind = kind;
    accesses[*inout_access_count].width_bits = width_bits;
    *inout_access_count += 1U;
}

/// Collects memory accesses that strict object validation should check before stepping.
///
/// The collector mirrors the currently implemented instruction families closely
/// enough to stop strict declared-object violations before register, flag, memory,
/// or console mutation. It is intentionally scoped to existing opcodes; future
/// memory-capable opcodes must extend this collector in their owning phase.
///
/// @param instruction Instruction to inspect.
/// @param accesses Output planned-access array.
/// @param access_capacity Number of entries available in @p accesses.
/// @return Number of planned accesses collected.
static size_t masm32_sim_wasm_collect_planned_object_accesses(
    const VmIrInstruction *instruction,
    Masm32SimWasmPlannedMemoryAccess *accesses,
    size_t access_capacity
) {
    size_t access_count = 0U;
    uint8_t width_bits = 0U;

    if (instruction == NULL || accesses == NULL || access_capacity == 0U) {
        return 0U;
    }

    switch (instruction->opcode) {
        case VM_IR_OPCODE_MOV:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->source, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_WRITE, width_bits);
            }
            break;
        case VM_IR_OPCODE_ADD:
        case VM_IR_OPCODE_SUB:
        case VM_IR_OPCODE_ADC:
        case VM_IR_OPCODE_SBB:
        case VM_IR_OPCODE_AND:
        case VM_IR_OPCODE_OR:
        case VM_IR_OPCODE_XOR:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->source, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_WRITE, width_bits);
            }
            break;
        case VM_IR_OPCODE_CMP:
        case VM_IR_OPCODE_TEST:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->source, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
            }
            break;
        case VM_IR_OPCODE_NEG:
        case VM_IR_OPCODE_NOT:
        case VM_IR_OPCODE_INC:
        case VM_IR_OPCODE_DEC:
        case VM_IR_OPCODE_SHL:
        case VM_IR_OPCODE_SAL:
        case VM_IR_OPCODE_SHR:
        case VM_IR_OPCODE_SAR:
        case VM_IR_OPCODE_ROL:
        case VM_IR_OPCODE_ROR:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_WRITE, width_bits);
            }
            break;
        case VM_IR_OPCODE_XCHG:
            if (masm32_sim_wasm_operand_width(&instruction->destination, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->destination, VM_EXEC_MEMORY_ACCESS_WRITE, width_bits);
            }
            if (masm32_sim_wasm_operand_width(&instruction->source, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->source, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->source, VM_EXEC_MEMORY_ACCESS_WRITE, width_bits);
            }
            break;
        case VM_IR_OPCODE_MOVSX:
        case VM_IR_OPCODE_MOVZX:
        case VM_IR_OPCODE_MUL:
        case VM_IR_OPCODE_IMUL:
        case VM_IR_OPCODE_IMUL_IMMEDIATE:
        case VM_IR_OPCODE_DIV:
        case VM_IR_OPCODE_IDIV:
            if (masm32_sim_wasm_operand_width(&instruction->source, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->source, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
            }
            break;
        case VM_IR_OPCODE_PUSH: {
            VmIrOperand stack_write = vm_ir_operand_memory_register(VM_REGISTER_ESP, -4, 0U, 32U);
            if (masm32_sim_wasm_operand_width(&instruction->source, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &instruction->source, VM_EXEC_MEMORY_ACCESS_READ, width_bits);
            }
            masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &stack_write, VM_EXEC_MEMORY_ACCESS_WRITE, 32U);
            break;
        }
        case VM_IR_OPCODE_POP: {
            VmIrOperand stack_read = vm_ir_operand_memory_register(VM_REGISTER_ESP, 0, 0U, 32U);
            VmIrOperand destination = instruction->destination;
            masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &stack_read, VM_EXEC_MEMORY_ACCESS_READ, 32U);
            if (destination.kind == VM_IR_OPERAND_MEMORY_REGISTER && destination.reg == VM_REGISTER_ESP) {
                destination.immediate += 4U;
            }
            if (masm32_sim_wasm_operand_width(&destination, &width_bits)) {
                masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &destination, VM_EXEC_MEMORY_ACCESS_WRITE, width_bits);
            }
            break;
        }
        case VM_IR_OPCODE_CALL: {
            VmIrOperand stack_write = vm_ir_operand_memory_register(VM_REGISTER_ESP, -4, 0U, 32U);
            masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &stack_write, VM_EXEC_MEMORY_ACCESS_WRITE, 32U);
            break;
        }
        case VM_IR_OPCODE_RET: {
            VmIrOperand stack_read = vm_ir_operand_memory_register(VM_REGISTER_ESP, 0, 0U, 32U);
            masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &stack_read, VM_EXEC_MEMORY_ACCESS_READ, 32U);
            break;
        }
        case VM_IR_OPCODE_LEAVE: {
            VmIrOperand frame_read = vm_ir_operand_memory_register(VM_REGISTER_EBP, 0, 0U, 32U);
            masm32_sim_wasm_add_planned_object_access(accesses, access_capacity, &access_count, &frame_read, VM_EXEC_MEMORY_ACCESS_READ, 32U);
            break;
        }
        default:
            break;
    }

    return access_count;
}

/// Returns whether a planned access passes mandatory Level 1 region/permission checks.
///
/// @param vm VM whose initialized memory regions should be inspected.
/// @param address First byte of the planned access.
/// @param size_bytes Number of bytes in the planned access.
/// @param kind Read/write kind for the planned access.
/// @return true when the access is wholly inside one suitable VM region.
static bool masm32_sim_wasm_planned_access_passes_level1(
    const Vm *vm,
    uint32_t address,
    uint32_t size_bytes,
    VmExecMemoryAccessKind kind
) {
    VmMemoryPermission permission = VM_MEMORY_PERMISSION_READ;
    size_t index = 0U;
    uint64_t end = 0U;

    if (vm == NULL || size_bytes == 0U || (uint64_t)address + (uint64_t)size_bytes > (uint64_t)UINT32_MAX + 1ULL) {
        return false;
    }

    if (kind == VM_EXEC_MEMORY_ACCESS_WRITE) {
        permission = VM_MEMORY_PERMISSION_WRITE;
    }
    end = (uint64_t)address + (uint64_t)size_bytes;

    for (index = 0U; index < (size_t)VM_MEMORY_REGION_COUNT; index += 1U) {
        const VmMemoryRegion *region = vm_memory_get_region(&vm->memory, (VmMemoryRegionKind)index);
        uint64_t region_end = 0U;

        if (region == NULL || region->size == 0U || (uint64_t)region->base + (uint64_t)region->size > (uint64_t)UINT32_MAX + 1ULL) {
            continue;
        }
        region_end = (uint64_t)region->base + (uint64_t)region->size;
        if ((uint64_t)address >= (uint64_t)region->base && end <= region_end) {
            return vm_memory_region_has_permission(region, permission);
        }
    }

    return false;
}

/// Validates strict declared-object access policy before stepping an instruction.
///
/// Planned accesses that would already fail mandatory Level 1 region or
/// permission checks are ignored here so lower-level runtime memory diagnostics
/// keep precedence. When Level 1 succeeds, strict Level 4 object validation can
/// stop before the instruction mutates registers, flags, memory, or console
/// state.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose next instruction should be inspected.
/// @param validation_mode Memory validation behavior selected for the run.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return OK when execution may continue, or MEMORY_ERROR for strict violations.
static VmExecStatus masm32_sim_wasm_validate_object_accesses_before_step(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    Masm32SimWasmMemoryValidationMode validation_mode,
    const VmLayoutPolicy *layout_policy
) {
    Masm32SimWasmPlannedMemoryAccess accesses[VM_EXEC_MAX_MEMORY_ACCESSES];
    const VmIrInstruction *instruction = NULL;
    size_t access_count = 0U;
    size_t index = 0U;

    if (storage == NULL || vm == NULL || validation_mode != MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT ||
        vm->halted || vm->instruction_pointer >= vm->program_count || vm->program == NULL) {
        return VM_EXEC_STATUS_OK;
    }

    memset(accesses, 0, sizeof(accesses));
    instruction = &vm->program[vm->instruction_pointer];
    access_count = masm32_sim_wasm_collect_planned_object_accesses(instruction, accesses, sizeof(accesses) / sizeof(accesses[0]));
    for (index = 0U; index < access_count; index += 1U) {
        uint32_t address = 0U;
        uint32_t size_bytes = 0U;
        VmExecMemoryAccess access;

        if (instruction->opcode == VM_IR_OPCODE_CALL || instruction->opcode == VM_IR_OPCODE_RET) {
            continue;
        }
        if (accesses[index].width_bits == 0U || (accesses[index].width_bits % 8U) != 0U ||
            !masm32_sim_wasm_resolve_memory_operand_address(vm, &accesses[index].operand, &address)) {
            continue;
        }
        size_bytes = (uint32_t)(accesses[index].width_bits / 8U);
        if (masm32_sim_wasm_access_range_is_stack(layout_policy, address, size_bytes)) {
            continue;
        }
        if (!masm32_sim_wasm_planned_access_passes_level1(vm, address, size_bytes, accesses[index].kind)) {
            continue;
        }

        memset(&access, 0, sizeof(access));
        access.kind = accesses[index].kind;
        access.address = address;
        access.width_bits = accesses[index].width_bits;
        access.status = VM_MEMORY_STATUS_OK;
        if (!masm32_sim_wasm_validate_object_access(storage, instruction, &access, validation_mode, layout_policy)) {
            return VM_EXEC_STATUS_MEMORY_ERROR;
        }
    }

    return VM_EXEC_STATUS_OK;
}

/// Validates strict section-boundary policies before stepping an instruction.
///
/// Planned accesses that would already fail mandatory Level 1 region or
/// permission checks are ignored here so lower-level runtime memory diagnostics
/// keep precedence. Level 2 section-capacity strict validation is checked before
/// Level 3 section-image strict validation.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose next instruction should be inspected.
/// @param capacity_policy Level 2 section-capacity validation behavior.
/// @param image_policy Level 3 section-image validation behavior.
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return OK when execution may continue, or MEMORY_ERROR for strict violations.
static VmExecStatus masm32_sim_wasm_validate_section_accesses_before_step(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy,
    const VmLayoutPolicy *layout_policy
) {
    Masm32SimWasmPlannedMemoryAccess accesses[VM_EXEC_MAX_MEMORY_ACCESSES];
    const VmIrInstruction *instruction = NULL;
    size_t access_count = 0U;
    size_t index = 0U;

    if (storage == NULL || vm == NULL || vm->halted || vm->instruction_pointer >= vm->program_count || vm->program == NULL ||
        (!masm32_sim_wasm_section_policy_is_strict(capacity_policy) && !masm32_sim_wasm_section_policy_is_strict(image_policy))) {
        return VM_EXEC_STATUS_OK;
    }

    memset(accesses, 0, sizeof(accesses));
    instruction = &vm->program[vm->instruction_pointer];
    access_count = masm32_sim_wasm_collect_planned_object_accesses(instruction, accesses, sizeof(accesses) / sizeof(accesses[0]));
    for (index = 0U; index < access_count; index += 1U) {
        uint32_t address = 0U;
        uint32_t size_bytes = 0U;
        VmExecMemoryAccess access;

        if (accesses[index].width_bits == 0U || (accesses[index].width_bits % 8U) != 0U ||
            !masm32_sim_wasm_resolve_memory_operand_address(vm, &accesses[index].operand, &address)) {
            continue;
        }
        size_bytes = (uint32_t)(accesses[index].width_bits / 8U);
        if (!masm32_sim_wasm_planned_access_passes_level1(vm, address, size_bytes, accesses[index].kind)) {
            continue;
        }

        memset(&access, 0, sizeof(access));
        access.kind = accesses[index].kind;
        access.address = address;
        access.width_bits = accesses[index].width_bits;
        access.status = VM_MEMORY_STATUS_OK;
        if (!masm32_sim_wasm_validate_section_access(storage, instruction, &access, capacity_policy, MASM32_SIM_WASM_SECTION_BOUNDARY_CAPACITY, layout_policy)) {
            return VM_EXEC_STATUS_MEMORY_ERROR;
        }
        if (!masm32_sim_wasm_validate_section_access(storage, instruction, &access, image_policy, MASM32_SIM_WASM_SECTION_BOUNDARY_IMAGE, layout_policy)) {
            return VM_EXEC_STATUS_MEMORY_ERROR;
        }
    }

    return VM_EXEC_STATUS_OK;
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

/// Returns whether a byte can continue a MASM-like identifier.
///
/// @param ch Source byte to inspect.
/// @return true when @p ch can appear inside an identifier.
static bool masm32_sim_wasm_is_identifier_continue(char ch) {
    return ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '@' || ch == '$');
}

/// Converts one ASCII byte to uppercase for identifier comparison.
///
/// @param ch Source byte to convert.
/// @return Uppercase ASCII byte, or @p ch for non-lowercase bytes.
static char masm32_sim_wasm_ascii_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

/// Returns whether a source slice equals a symbol spelling case-insensitively.
///
/// @param text Source slice to compare.
/// @param symbol Symbol spelling to match.
/// @param symbol_length Length of @p symbol.
/// @return true when @p text matches @p symbol with ASCII case folding.
static bool masm32_sim_wasm_symbol_slice_equals(const char *text, const char *symbol, size_t symbol_length) {
    size_t index = 0U;

    if (text == NULL || symbol == NULL || symbol_length == 0U) {
        return false;
    }
    for (index = 0U; index < symbol_length; index += 1U) {
        if (masm32_sim_wasm_ascii_upper(text[index]) != masm32_sim_wasm_ascii_upper(symbol[index])) {
            return false;
        }
    }
    return true;
}

/// Copies best-effort direct-symbol memory operand source-span metadata.
///
/// @param instruction Instruction associated with the memory operand.
/// @param source Original source text for byte-offset reconstruction.
/// @param symbol_name Symbol name whose source reference should be located.
/// @param out_column Receives a one-based source column, or zero.
/// @param out_byte_offset Receives the zero-based source byte offset.
/// @param out_span_length Receives source span length in bytes.
/// @param out_has_source_span Receives whether byte offset and span length are valid.
static void masm32_sim_wasm_copy_symbol_memory_source_span(
    const VmIrInstruction *instruction,
    const char *source,
    const char *symbol_name,
    uint32_t *out_column,
    size_t *out_byte_offset,
    size_t *out_span_length,
    bool *out_has_source_span
) {
    const char *line_text = instruction != NULL ? instruction->source_text : NULL;
    const char *scan = line_text;
    const char *match = NULL;
    size_t line_start_offset = 0U;
    size_t symbol_length = symbol_name != NULL ? strlen(symbol_name) : 0U;

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
    if (instruction == NULL || source == NULL || line_text == NULL || symbol_name == NULL || symbol_length == 0U || instruction->source_line == 0U ||
        out_column == NULL || out_byte_offset == NULL || out_span_length == NULL || out_has_source_span == NULL ||
        !masm32_sim_wasm_find_line_start_offset(source, instruction->source_line, &line_start_offset)) {
        return;
    }

    while (scan != NULL && *scan != '\0') {
        size_t remaining_length = strlen(scan);
        bool before_is_identifier = scan > line_text && masm32_sim_wasm_is_identifier_continue(scan[-1]);
        bool after_is_identifier = remaining_length > symbol_length && masm32_sim_wasm_is_identifier_continue(scan[symbol_length]);

        if (remaining_length >= symbol_length && !before_is_identifier && !after_is_identifier &&
            masm32_sim_wasm_symbol_slice_equals(scan, symbol_name, symbol_length)) {
            match = scan;
            break;
        }
        scan += 1;
    }
    if (match == NULL) {
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

        *out_column = (uint32_t)(line_text_offset + (size_t)(match - line_text) + 1U);
        *out_byte_offset = line_start_offset + (size_t)(*out_column - 1U);
    }
    *out_span_length = symbol_length;
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

/// Copies a best-effort textual representation of a one-operand instruction source operand.
///
/// The parser preserves the original source line rather than token-level operand
/// spans. Runtime diagnostics use this helper to make single-source-operand
/// messages more concrete without changing the IR shape.
///
/// @param instruction Instruction whose source text should be inspected.
/// @param buffer Destination buffer for the operand text.
/// @param buffer_size Number of bytes available in @p buffer.
/// @return true when a non-empty operand text was copied.
static bool masm32_sim_wasm_copy_single_source_operand_text(const VmIrInstruction *instruction, char *buffer, size_t buffer_size) {
    const char *line_text = instruction != NULL ? instruction->source_text : NULL;
    const char *cursor = NULL;
    const char *operand_start = NULL;
    const char *operand_end = NULL;
    size_t length = 0U;

    if (buffer == NULL || buffer_size == 0U) {
        return false;
    }
    buffer[0] = '\0';
    if (line_text == NULL) {
        return false;
    }

    cursor = line_text;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor += 1;
    }
    while ((*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= 'a' && *cursor <= 'z')) {
        cursor += 1;
    }
    while (*cursor == ' ' || *cursor == '\t') {
        cursor += 1;
    }
    if (*cursor == '\0' || *cursor == ';') {
        return false;
    }

    operand_start = cursor;
    operand_end = operand_start;
    while (*operand_end != '\0' && *operand_end != ';') {
        operand_end += 1;
    }
    while (operand_end > operand_start && (operand_end[-1] == ' ' || operand_end[-1] == '\t' || operand_end[-1] == '\r')) {
        operand_end -= 1;
    }
    if (operand_end <= operand_start) {
        return false;
    }

    length = (size_t)(operand_end - operand_start);
    if (length >= buffer_size) {
        length = buffer_size - 1U;
    }
    memcpy(buffer, operand_start, length);
    buffer[length] = '\0';
    return length > 0U;
}

/// Returns the quotient register written by a successful DIV or IDIV.
///
/// @param width_bits DIV or IDIV divisor width in bits.
/// @return Static display text for the quotient register.
static const char *masm32_sim_wasm_div_quotient_register(uint8_t width_bits) {
    if (width_bits == 8U) {
        return "AL";
    }
    if (width_bits == 16U) {
        return "AX";
    }
    if (width_bits == 32U) {
        return "EAX";
    }
    return "AL/AX/EAX";
}

/// Returns the remainder register written by a successful DIV or IDIV.
///
/// @param width_bits DIV or IDIV divisor width in bits.
/// @return Static display text for the remainder register.
static const char *masm32_sim_wasm_div_remainder_register(uint8_t width_bits) {
    if (width_bits == 8U) {
        return "AH";
    }
    if (width_bits == 16U) {
        return "DX";
    }
    if (width_bits == 32U) {
        return "EDX";
    }
    return "AH/DX/EDX";
}

/// Returns the diagnostic mnemonic for one divide-family instruction.
///
/// @param instruction DIV or IDIV instruction associated with a diagnostic.
/// @return Static uppercase mnemonic text.
static const char *masm32_sim_wasm_divide_mnemonic(const VmIrInstruction *instruction) {
    if (instruction != NULL && instruction->opcode == VM_IR_OPCODE_IDIV) {
        return "IDIV";
    }
    return "DIV";
}

/// Copies display text for a DIV or IDIV divisor operand.
///
/// Register divisors use canonical uppercase register names. Memory operands
/// keep the original source operand text when available so diagnostics can
/// point at the specific symbol or memory expression the user wrote.
///
/// @param instruction DIV or IDIV instruction whose source operand should be displayed.
/// @param buffer Destination buffer.
/// @param buffer_size Number of bytes available in @p buffer.
/// @return true when an operand display string was produced.
static bool masm32_sim_wasm_copy_div_divisor_operand_display(
    const VmIrInstruction *instruction,
    char *buffer,
    size_t buffer_size
) {
    const char *register_name = NULL;

    if (buffer == NULL || buffer_size == 0U || instruction == NULL) {
        return false;
    }

    if (instruction->source.kind == VM_IR_OPERAND_REGISTER) {
        register_name = vm_cpu_register_name(instruction->source.reg);
        if (register_name == NULL) {
            return false;
        }
        (void)snprintf(buffer, buffer_size, "%s", register_name);
        return true;
    }

    return masm32_sim_wasm_copy_single_source_operand_text(instruction, buffer, buffer_size);
}

/// Formats the divide-by-zero runtime diagnostic message for DIV and IDIV.
///
/// The message names the divisor operand when source text is available and
/// names the quotient and remainder registers left unchanged.
///
/// @param instruction DIV or IDIV instruction associated with the diagnostic.
/// @param buffer Destination buffer.
/// @param buffer_size Number of bytes available in @p buffer.
/// @return Pointer to @p buffer.
static const char *masm32_sim_wasm_format_divide_by_zero_message(
    const VmIrInstruction *instruction,
    char *buffer,
    size_t buffer_size
) {
    char operand_text[96];
    uint8_t width_bits = instruction != NULL ? instruction->source.width_bits : 0U;
    const char *quotient_register = NULL;
    const char *remainder_register = NULL;
    const char *mnemonic = masm32_sim_wasm_divide_mnemonic(instruction);

    if (instruction != NULL && width_bits == 0U && instruction->source.kind == VM_IR_OPERAND_REGISTER) {
        width_bits = vm_cpu_register_width_bits(instruction->source.reg);
    }
    quotient_register = masm32_sim_wasm_div_quotient_register(width_bits);
    remainder_register = masm32_sim_wasm_div_remainder_register(width_bits);

    if (buffer == NULL || buffer_size == 0U) {
        return "DIV or IDIV divisor evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient and remainder registers.";
    }

    if (masm32_sim_wasm_copy_div_divisor_operand_display(instruction, operand_text, sizeof(operand_text))) {
        (void)snprintf(
            buffer,
            buffer_size,
            "%s divisor operand %s evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register %s and remainder register %s.",
            mnemonic,
            operand_text,
            quotient_register,
            remainder_register
        );
    } else {
        (void)snprintf(
            buffer,
            buffer_size,
            "%s divisor evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register %s and remainder register %s.",
            mnemonic,
            quotient_register,
            remainder_register
        );
    }

    return buffer;
}

/// Formats the quotient-overflow runtime diagnostic message for DIV and IDIV.
///
/// The message names the quotient and remainder registers for the selected DIV
/// or IDIV width so students can see exactly which result registers were protected from
/// partial mutation.
///
/// @param instruction DIV or IDIV instruction associated with the diagnostic.
/// @param buffer Destination buffer.
/// @param buffer_size Number of bytes available in @p buffer.
/// @return Pointer to @p buffer.
static const char *masm32_sim_wasm_format_div_quotient_overflow_message(
    const VmIrInstruction *instruction,
    char *buffer,
    size_t buffer_size
) {
    uint8_t width_bits = instruction != NULL ? instruction->source.width_bits : 0U;
    const char *quotient_register = NULL;
    const char *remainder_register = NULL;
    const char *mnemonic = masm32_sim_wasm_divide_mnemonic(instruction);

    if (instruction != NULL && width_bits == 0U && instruction->source.kind == VM_IR_OPERAND_REGISTER) {
        width_bits = vm_cpu_register_width_bits(instruction->source.reg);
    }
    quotient_register = masm32_sim_wasm_div_quotient_register(width_bits);
    remainder_register = masm32_sim_wasm_div_remainder_register(width_bits);

    if (buffer == NULL || buffer_size == 0U) {
        return "DIV or IDIV quotient is too large to fit in the quotient register. Execution stopped before updating the quotient and remainder registers.";
    }

    (void)snprintf(
        buffer,
        buffer_size,
        "%s quotient is too large to fit in quotient register %s. Execution stopped before updating the quotient register %s and remainder register %s.",
        mnemonic,
        quotient_register,
        quotient_register,
        remainder_register
    );

    return buffer;
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

/// Counts uninitialized-origin bytes in one section mask.
///
/// @param mask Per-byte initialized-state mask for the section.
/// @param mask_size Number of bytes available in @p mask.
/// @param offset Offset of the read start within the section.
/// @param size_bytes Number of bytes requested by the read.
/// @return Number of tracked bytes that remain uninitialized-origin.
static uint32_t masm32_sim_wasm_count_uninitialized_bytes_in_mask(
    const uint8_t *mask,
    size_t mask_size,
    uint32_t offset,
    uint32_t size_bytes
) {
    uint32_t index = 0U;
    uint32_t uninitialized_count = 0U;
    uint32_t tracked_size_bytes = size_bytes;

    if (mask == NULL || size_bytes == 0U || offset >= mask_size) {
        return 0U;
    }
    if ((uint64_t)offset + (uint64_t)tracked_size_bytes > (uint64_t)mask_size) {
        tracked_size_bytes = (uint32_t)(mask_size - (size_t)offset);
    }

    for (index = 0U; index < tracked_size_bytes; index += 1U) {
        if (mask[(size_t)offset + (size_t)index] == MASM32_SIM_WASM_DATA_BYTE_UNINITIALIZED) {
            uninitialized_count += 1U;
        }
    }

    return uninitialized_count;
}

/// Counts bytes in a planned read that are still marked uninitialized-origin.
///
/// @param storage Source-run storage containing initialization masks.
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
    const VmLayoutRegionPolicy *const_region = NULL;
    uint32_t offset = 0U;

    if (storage == NULL || size_bytes == 0U) {
        return 0U;
    }
    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    data_region = &effective_policy->regions[VM_LAYOUT_REGION_DATA];
    if (address >= data_region->base && address < data_region->limit) {
        offset = address - data_region->base;
        return masm32_sim_wasm_count_uninitialized_bytes_in_mask(
            storage->data_initialized_mask,
            storage->data_initialized_mask_size,
            offset,
            size_bytes
        );
    }

    const_region = &effective_policy->regions[VM_LAYOUT_REGION_CONST];
    if (address >= const_region->base && address < const_region->limit) {
        offset = address - const_region->base;
        return masm32_sim_wasm_count_uninitialized_bytes_in_mask(
            storage->const_initialized_mask,
            storage->const_initialized_mask_size,
            offset,
            size_bytes
        );
    }

    return 0U;
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
    if (!diagnostic->has_source_span && diagnostic->has_symbol_name) {
        masm32_sim_wasm_copy_symbol_memory_source_span(
            instruction,
            storage != NULL ? storage->source_text : NULL,
            diagnostic->symbol_name,
            &diagnostic->source_column,
            &diagnostic->source_byte_offset,
            &diagnostic->source_span_length,
            &diagnostic->has_source_span
        );
    }
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
    if (masm32_sim_wasm_next_instruction_is_strict_root_ret(vm)) {
        return VM_EXEC_STATUS_OK;
    }
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
        case VM_IR_OPCODE_JE:
        case VM_IR_OPCODE_JZ:
        case VM_IR_OPCODE_JNE:
        case VM_IR_OPCODE_JNZ:
            if (out_flags != NULL && flag_capacity > 0U) {
                out_flags[0] = VM_FLAG_ZF;
            }
            return 1U;
        case VM_IR_OPCODE_JL:
        case VM_IR_OPCODE_JNGE:
        case VM_IR_OPCODE_JGE:
        case VM_IR_OPCODE_JNL:
            if (out_flags != NULL && flag_capacity >= 2U) {
                out_flags[0] = VM_FLAG_SF;
                out_flags[1] = VM_FLAG_OF;
            }
            return 2U;
        case VM_IR_OPCODE_JLE:
        case VM_IR_OPCODE_JNG:
        case VM_IR_OPCODE_JG:
        case VM_IR_OPCODE_JNLE:
            if (out_flags != NULL && flag_capacity >= 3U) {
                out_flags[0] = VM_FLAG_ZF;
                out_flags[1] = VM_FLAG_SF;
                out_flags[2] = VM_FLAG_OF;
            }
            return 3U;
        case VM_IR_OPCODE_JAE:
        case VM_IR_OPCODE_JNB:
        case VM_IR_OPCODE_JB:
        case VM_IR_OPCODE_JNAE:
            if (out_flags != NULL && flag_capacity > 0U) {
                out_flags[0] = VM_FLAG_CF;
            }
            return 1U;
        case VM_IR_OPCODE_JA:
        case VM_IR_OPCODE_JNBE:
        case VM_IR_OPCODE_JBE:
        case VM_IR_OPCODE_JNA:
            if (out_flags != NULL && flag_capacity >= 2U) {
                out_flags[0] = VM_FLAG_CF;
                out_flags[1] = VM_FLAG_ZF;
            }
            return 2U;
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

/// Builds user-facing text for one section-boundary diagnostic.
///
/// @param diagnostic Section-boundary diagnostic to describe.
/// @param buffer Destination buffer for the formatted message.
/// @param buffer_size Destination buffer size in bytes.
static void masm32_sim_wasm_format_section_boundary_message(
    const Masm32SimWasmSectionBoundaryDiagnostic *diagnostic,
    char *buffer,
    size_t buffer_size
) {
    const char *access_name = diagnostic != NULL ? masm32_sim_wasm_exec_memory_access_kind_name(diagnostic->access_kind) : "access";
    const char *level_name = diagnostic != NULL ? masm32_sim_wasm_section_boundary_level_name(diagnostic->level) : "section boundary";
    const char *owner_name = diagnostic != NULL && diagnostic->has_owner ? diagnostic->owner_name : "unknown section";

    if (buffer == NULL || buffer_size == 0U) {
        return;
    }
    if (diagnostic == NULL) {
        (void)snprintf(buffer, buffer_size, "Memory access violates a section boundary.");
        return;
    }

    if (diagnostic->has_boundary) {
        (void)snprintf(
            buffer,
            buffer_size,
            "Memory %s at %08Xh for %u byte%s covers range %08Xh..%08Xh and leaves the %s range for %s (%08Xh..%08Xh).",
            access_name,
            (unsigned int)diagnostic->start_address,
            (unsigned int)diagnostic->size_bytes,
            diagnostic->size_bytes == 1U ? "" : "s",
            (unsigned int)diagnostic->start_address,
            (unsigned int)diagnostic->end_address,
            level_name,
            owner_name,
            (unsigned int)diagnostic->boundary_start,
            (unsigned int)diagnostic->boundary_end
        );
        return;
    }

    (void)snprintf(
        buffer,
        buffer_size,
        "Memory %s at %08Xh for %u byte%s covers range %08Xh..%08Xh but does not start inside a known %s range for %s.",
        access_name,
        (unsigned int)diagnostic->start_address,
        (unsigned int)diagnostic->size_bytes,
        diagnostic->size_bytes == 1U ? "" : "s",
        (unsigned int)diagnostic->start_address,
        (unsigned int)diagnostic->end_address,
        level_name,
        owner_name
    );
}

/// Appends one section-boundary message object with diagnostic metadata.
///
/// @param writer Writer to mutate.
/// @param kind Simulator-message kind to emit.
/// @param diagnostic Diagnostic to render.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_section_boundary_message(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const Masm32SimWasmSectionBoundaryDiagnostic *diagnostic
) {
    char message[384];

    if (writer == NULL) {
        return false;
    }

    masm32_sim_wasm_format_section_boundary_message(diagnostic, message, sizeof(message));
    if (!masm32_sim_json_append(writer, "{\"kind\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, kind)) {
        return false;
    }
    if (!masm32_sim_json_append(writer, ",\"code\":\"%s\",\"message\":", diagnostic != NULL ? masm32_sim_wasm_section_boundary_code(diagnostic->level) : "section-boundary-violation")) {
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
    if (diagnostic != NULL) {
        if (!masm32_sim_json_append(
                writer,
                ",\"accessKind\":\"%s\",\"accessStartAddress\":\"%08Xh\",\"accessEndAddress\":\"%08Xh\",\"accessSizeBytes\":%u,\"ownerSection\":",
                masm32_sim_wasm_exec_memory_access_kind_name(diagnostic->access_kind),
                (unsigned int)diagnostic->start_address,
                (unsigned int)diagnostic->end_address,
                (unsigned int)diagnostic->size_bytes
            )) {
            return false;
        }
        if (diagnostic->has_owner) {
            if (!masm32_sim_json_append_string(writer, diagnostic->owner_name)) {
                return false;
            }
        } else if (!masm32_sim_json_append(writer, "null")) {
            return false;
        }
        if (diagnostic->has_boundary) {
            if (!masm32_sim_json_append(
                    writer,
                    ",\"boundaryStartAddress\":\"%08Xh\",\"boundaryEndAddress\":\"%08Xh\"",
                    (unsigned int)diagnostic->boundary_start,
                    (unsigned int)diagnostic->boundary_end
                )) {
                return false;
            }
        } else if (!masm32_sim_json_append(writer, ",\"boundaryStartAddress\":null,\"boundaryEndAddress\":null")) {
            return false;
        }
    }

    return masm32_sim_json_append(writer, "}");
}

/// Appends collected section-boundary warnings to the simulatorMessages array.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing section warnings.
/// @param inout_has_message Whether a prior message has already been appended.
/// @return true when warnings fit without overflowing the buffer.
static bool masm32_sim_json_append_section_warnings(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    bool *inout_has_message
) {
    size_t index = 0U;

    if (storage == NULL || inout_has_message == NULL) {
        return true;
    }

    for (index = 0U; index < storage->section_warning_count; index += 1U) {
        if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_section_boundary_message(writer, "simulator-warning", &storage->section_warnings[index])) {
            return false;
        }
        *inout_has_message = true;
    }

    return true;
}

/// Appends one strict section-boundary runtime diagnostic.
///
/// @param writer Writer to mutate.
/// @param diagnostic Strict section-boundary diagnostic to render.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_section_violation(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmSectionBoundaryDiagnostic *diagnostic
) {
    return masm32_sim_json_append_section_boundary_message(writer, "runtime-error", diagnostic);
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
    if (strcmp(mnemonic, "je") == 0) {
        return "JE";
    }
    if (strcmp(mnemonic, "jz") == 0) {
        return "JZ";
    }
    if (strcmp(mnemonic, "jne") == 0) {
        return "JNE";
    }
    if (strcmp(mnemonic, "jnz") == 0) {
        return "JNZ";
    }
    if (strcmp(mnemonic, "jl") == 0) {
        return "JL";
    }
    if (strcmp(mnemonic, "jnge") == 0) {
        return "JNGE";
    }
    if (strcmp(mnemonic, "jle") == 0) {
        return "JLE";
    }
    if (strcmp(mnemonic, "jng") == 0) {
        return "JNG";
    }
    if (strcmp(mnemonic, "jg") == 0) {
        return "JG";
    }
    if (strcmp(mnemonic, "jnle") == 0) {
        return "JNLE";
    }
    if (strcmp(mnemonic, "jge") == 0) {
        return "JGE";
    }
    if (strcmp(mnemonic, "jnl") == 0) {
        return "JNL";
    }
    if (strcmp(mnemonic, "ja") == 0) {
        return "JA";
    }
    if (strcmp(mnemonic, "jnbe") == 0) {
        return "JNBE";
    }
    if (strcmp(mnemonic, "jae") == 0) {
        return "JAE";
    }
    if (strcmp(mnemonic, "jnb") == 0) {
        return "JNB";
    }
    if (strcmp(mnemonic, "jb") == 0) {
        return "JB";
    }
    if (strcmp(mnemonic, "jnae") == 0) {
        return "JNAE";
    }
    if (strcmp(mnemonic, "jbe") == 0) {
        return "JBE";
    }
    if (strcmp(mnemonic, "jna") == 0) {
        return "JNA";
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

    if (diagnostic != NULL && diagnostic->severity == VM_PARSER_DIAGNOSTIC_SEVERITY_NOTICE) {
        return "simulator-notice";
    }

    if (diagnostic != NULL && diagnostic->severity == VM_PARSER_DIAGNOSTIC_SEVERITY_WARNING) {
        return code == VM_PARSER_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE ? "simulator-warning" : "assembly-warning";
    }

    if (code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HOST_INCLUDE_PATH ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_INCLUDE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY_INCLUDE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDELIB ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_LIBRARY ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_ADDR ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_EXTERNAL_ROUTINE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINAPI_EXECUTION ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_RUNTIME_ROUTINE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CRT_ROUTINE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_IF ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ELSE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ENDIF ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_WHILE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_REPEAT ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE ||
        code == VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SEGMENT_SYMBOL ||
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

/// Appends nonfatal parser diagnostics after any prior Simulator Messages.
///
/// Phase 69B uses this helper after the startup-state notice for runs that
/// begin execution, so compatibility notices and accepted-construct teaching
/// diagnostics remain source-run objects but no longer precede startup state.
///
/// @param writer Writer to mutate.
/// @param diagnostics Parser diagnostic array.
/// @param diagnostic_count Number of parser diagnostics available.
/// @param has_message Whether earlier messages are already present; updated on append.
/// @return true when diagnostics fit without overflowing the buffer.
static bool masm32_sim_json_append_nonfatal_parser_messages(
    Masm32SimJsonWriter *writer,
    const VmParserDiagnostic *diagnostics,
    size_t diagnostic_count,
    bool *has_message
) {
    if (writer == NULL || has_message == NULL || diagnostics == NULL || diagnostic_count == 0U) {
        return true;
    }

    if (*has_message && !masm32_sim_json_append(writer, ",")) {
        return false;
    }
    if (!masm32_sim_json_append_parser_messages(writer, diagnostics, diagnostic_count)) {
        return false;
    }
    *has_message = true;
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

    if (diagnostic->memory_status == VM_MEMORY_STATUS_REGION_BOUNDARY_CROSSING) {
        uint32_t range_end = memory_diagnostic->address;
        bool has_range_end = memory_diagnostic->size > 0U && memory_diagnostic->size <= UINT32_MAX - memory_diagnostic->address;
        if (has_range_end) {
            range_end = memory_diagnostic->address + memory_diagnostic->size - 1U;
        }

        if (buffer != NULL && buffer_size > 0U) {
            if (has_range_end && memory_diagnostic->has_const_overlap) {
                (void)snprintf(
                    buffer,
                    buffer_size,
                    "Cross-region memory %s at %08Xh for %u byte%s. The memory address range %08Xh..%08Xh crosses/overlaps a protected memory region, .CONST, that starts at %08Xh. This is not allowed; program stopped before access.",
                    access_name,
                    (unsigned int)memory_diagnostic->address,
                    (unsigned int)memory_diagnostic->size,
                    memory_diagnostic->size == 1U ? "" : "s",
                    (unsigned int)memory_diagnostic->address,
                    (unsigned int)range_end,
                    (unsigned int)memory_diagnostic->const_region_start
                );
            } else if (has_range_end && memory_diagnostic->has_code_overlap) {
                (void)snprintf(
                    buffer,
                    buffer_size,
                    "Cross-region memory %s at %08Xh for %u byte%s. The memory address range %08Xh..%08Xh crosses/overlaps a no-access memory region, .CODE/_TEXT, that starts at %08Xh. This is not allowed; program stopped before access.",
                    access_name,
                    (unsigned int)memory_diagnostic->address,
                    (unsigned int)memory_diagnostic->size,
                    memory_diagnostic->size == 1U ? "" : "s",
                    (unsigned int)memory_diagnostic->address,
                    (unsigned int)range_end,
                    (unsigned int)memory_diagnostic->code_region_start
                );
            } else {
                (void)snprintf(
                    buffer,
                    buffer_size,
                    "Cross-region memory %s at %08Xh for %u byte%s. The requested memory address range cannot be contained in one simulator memory region.",
                    access_name,
                    (unsigned int)memory_diagnostic->address,
                    (unsigned int)memory_diagnostic->size,
                    memory_diagnostic->size == 1U ? "" : "s"
                );
            }
        }
        return memory_status_name;
    }

    if (diagnostic->memory_status == VM_MEMORY_STATUS_UNSUPPORTED_CODE_MEMORY_ACCESS) {
        if (buffer != NULL && buffer_size > 0U) {
            (void)snprintf(
                buffer,
                buffer_size,
                "Memory %s at %08Xh for %u byte%s overlaps .CODE/_TEXT. The simulator does not expose .CODE/_TEXT as an accessible memory region. Program stopped before access.",
                access_name,
                (unsigned int)memory_diagnostic->address,
                (unsigned int)memory_diagnostic->size,
                memory_diagnostic->size == 1U ? "" : "s"
            );
        }
        return memory_status_name;
    }

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

/// Returns whether two ASCII strings match case-insensitively.
///
/// This helper is intentionally local to source-run diagnostics so the Phase
/// 71C easter-egg comparison cannot affect ordinary symbol lookup, CASEMAP,
/// duplicate-name checks, or parser behavior.
///
/// @param actual Candidate source spelling.
/// @param expected Lowercase ASCII spelling to compare against.
/// @return true when both strings have the same length and ASCII letters match ignoring case.
static bool masm32_sim_wasm_ascii_equals_ignore_case(const char *actual, const char *expected) {
    size_t index = 0U;

    if (actual == NULL || expected == NULL) {
        return false;
    }

    while (actual[index] != '\0' && expected[index] != '\0') {
        unsigned char actual_char = (unsigned char)actual[index];
        unsigned char expected_char = (unsigned char)expected[index];
        if (actual_char >= (unsigned char)'A' && actual_char <= (unsigned char)'Z') {
            actual_char = (unsigned char)(actual_char - (unsigned char)'A' + (unsigned char)'a');
        }
        if (expected_char >= (unsigned char)'A' && expected_char <= (unsigned char)'Z') {
            expected_char = (unsigned char)(expected_char - (unsigned char)'A' + (unsigned char)'a');
        }
        if (actual_char != expected_char) {
            return false;
        }
        index += 1U;
    }

    return actual[index] == '\0' && expected[index] == '\0';
}

/// Finds the accepted procedure range containing a lowered instruction index.
///
/// @param parser_result Parser result containing the procedure count.
/// @param storage Source-run storage containing procedure ranges copied from the parser.
/// @param instruction_index Lowered executable instruction index to classify.
/// @return Containing procedure range, or NULL when none contains the instruction.
static const VmProcedureRange *masm32_sim_wasm_find_procedure_for_instruction(
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage,
    size_t instruction_index
) {
    size_t index = 0U;

    if (parser_result == NULL || storage == NULL) {
        return NULL;
    }

    for (index = 0U; index < parser_result->procedure_range_count; index += 1U) {
        const VmProcedureRange *range = &storage->procedure_ranges[index];
        if (instruction_index >= range->start_instruction_index && instruction_index < range->end_instruction_index) {
            return range;
        }
    }

    return NULL;
}


/// Finds the accepted procedure range that starts at one lowered instruction index.
///
/// @param parser_result Parser result containing the procedure count.
/// @param storage Source-run storage containing procedure ranges copied from the parser.
/// @param instruction_index Lowered executable instruction index to match.
/// @return Starting procedure range, or NULL when none starts there.
static const VmProcedureRange *masm32_sim_wasm_find_procedure_starting_at(
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage,
    size_t instruction_index
) {
    size_t index = 0U;

    if (parser_result == NULL || storage == NULL) {
        return NULL;
    }

    for (index = 0U; index < parser_result->procedure_range_count; index += 1U) {
        const VmProcedureRange *range = &storage->procedure_ranges[index];
        if (range->start_instruction_index == instruction_index) {
            return range;
        }
    }

    return NULL;
}

/// Determines the Phase 71C responsible procedure for a code-end falloff.
///
/// The rule mirrors the implementation-guide contract: use the procedure that
/// contains the last executed lowered instruction when one exists; otherwise use
/// the selected entry procedure for an empty-entry/no-successor falloff. Future
/// top-level executable code may legitimately return NULL instead of inventing a
/// fake procedure.
///
/// @param vm VM whose instruction count and last diagnostic describe the falloff.
/// @param parser_result Parser result containing selected-entry metadata.
/// @param storage Source-run storage containing accepted procedure ranges.
/// @return Responsible procedure range, or NULL when no procedure owns it.
static const VmProcedureRange *masm32_sim_wasm_code_falloff_responsible_procedure(
    const Vm *vm,
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage
) {
    const VmExecDiagnostic *diagnostic = vm != NULL ? vm_last_diagnostic(vm) : NULL;

    if (diagnostic != NULL && diagnostic->has_instruction) {
        const VmProcedureRange *range = masm32_sim_wasm_find_procedure_for_instruction(
            parser_result,
            storage,
            (size_t)diagnostic->instruction_index
        );
        if (range != NULL) {
            return range;
        }
    }

    if (vm != NULL && vm->instruction_count == 0U &&
        parser_result != NULL && parser_result->has_selected_entry_procedure &&
        storage != NULL && parser_result->selected_entry_procedure_index < parser_result->procedure_range_count) {
        return &storage->procedure_ranges[parser_result->selected_entry_procedure_index];
    }

    return NULL;
}

/// Appends the Phase 71C code-end falloff diagnostic and optional notice.
///
/// @param writer Writer to mutate.
/// @param vm VM whose last diagnostic and register state describe the falloff.
/// @param parser_result Parser result containing procedure metadata.
/// @param storage Source-run storage containing source text and procedure ranges.
/// @return true when both required messages fit without overflowing the buffer.
static bool masm32_sim_json_append_code_fell_off_end_messages(
    Masm32SimJsonWriter *writer,
    const Vm *vm,
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage
) {
    const char *message_text = "Execution reached the end of the executable code stream without an explicit program terminator. Did you forget to add RET or Irvine32 exit?";
    const VmExecDiagnostic *diagnostic = vm != NULL ? vm_last_diagnostic(vm) : NULL;
    const VmProcedureRange *responsible = masm32_sim_wasm_code_falloff_responsible_procedure(vm, parser_result, storage);
    const char *procedure_name = responsible != NULL ? responsible->name : NULL;
    uint32_t line = 0U;
    uint32_t column = 0U;
    size_t byte_offset = 0U;
    size_t span_length = 0U;
    bool has_source_span = false;

    if (diagnostic != NULL && diagnostic->has_instruction) {
        line = diagnostic->instruction.source_line;
        masm32_sim_wasm_copy_instruction_source_span(
            &diagnostic->instruction,
            storage != NULL ? storage->source_text : NULL,
            &column,
            &byte_offset,
            &span_length,
            &has_source_span
        );
    } else if (responsible != NULL) {
        line = responsible->source_location.line;
        column = responsible->source_location.column;
        byte_offset = responsible->source_location.offset;
        span_length = responsible->source_span_length;
        has_source_span = span_length > 0U;
    }

    if (!masm32_sim_json_append_message_with_span_and_procedure(
            writer,
            "runtime-error",
            "code-fell-off-end",
            message_text,
            line,
            column,
            byte_offset,
            span_length,
            has_source_span,
            procedure_name
        )) {
        return false;
    }

    if (masm32_sim_wasm_ascii_equals_ignore_case(procedure_name, "front")) {
        if (!masm32_sim_json_append(writer, ",")) {
            return false;
        }
        return masm32_sim_json_append_message_with_span_and_procedure(
            writer,
            "simulator-notice",
            "the-front-fell-off",
            "that's not very typical, I'd like to make that point",
            line,
            column,
            byte_offset,
            span_length,
            has_source_span,
            procedure_name
        );
    }

    return true;
}

/// Copies a procedure name into a Phase 71D diagnostic slot.
///
/// @param destination Fixed-size destination buffer.
/// @param capacity Destination buffer capacity.
/// @param source Source procedure name, or NULL.
/// @return true when a non-empty name was copied.
static bool masm32_sim_wasm_copy_procedure_name(char *destination, size_t capacity, const char *source) {
    if (destination == NULL || capacity == 0U) {
        return false;
    }
    destination[0] = '\0';
    if (source == NULL || source[0] == '\0') {
        return false;
    }
    (void)snprintf(destination, capacity, "%s", source);
    return destination[0] != '\0';
}


/// Finds the accepted procedure range that starts at a lowered instruction index.
///
/// @param parser_result Parser result containing the procedure count.
/// @param storage Source-run storage containing procedure ranges copied from the parser.
/// @param instruction_index Lowered executable instruction index to match against a procedure entry.
/// @return Matching procedure range, or NULL when none starts at @p instruction_index.
static const VmProcedureRange *masm32_sim_wasm_find_procedure_starting_at_instruction(
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage,
    size_t instruction_index
) {
    size_t index = 0U;

    if (parser_result == NULL || storage == NULL) {
        return NULL;
    }

    for (index = 0U; index < parser_result->procedure_range_count; index += 1U) {
        const VmProcedureRange *range = &storage->procedure_ranges[index];
        if (range->start_instruction_index == instruction_index) {
            return range;
        }
    }

    return NULL;
}

/// Collects a fatal Phase 72 call-depth-limit diagnostic from the VM.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose latest executor diagnostic should be inspected.
/// @param parser_result Parser result containing procedure metadata.
static void masm32_sim_wasm_collect_call_depth_violation(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    const VmParserResult *parser_result
) {
    const VmExecDiagnostic *core_diagnostic = vm != NULL ? vm_last_diagnostic(vm) : NULL;
    Masm32SimWasmCallDepthDiagnostic *out_diagnostic = NULL;
    const VmProcedureRange *target_range = NULL;

    if (storage == NULL || core_diagnostic == NULL ||
        core_diagnostic->status != VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED ||
        !core_diagnostic->has_instruction) {
        return;
    }

    out_diagnostic = &storage->call_depth_violation;
    memset(out_diagnostic, 0, sizeof(*out_diagnostic));
    out_diagnostic->has_diagnostic = true;
    out_diagnostic->line = core_diagnostic->instruction.source_line;
    out_diagnostic->current_depth = core_diagnostic->current_call_depth;
    out_diagnostic->attempted_depth = core_diagnostic->attempted_call_depth;
    out_diagnostic->limit = core_diagnostic->call_depth_limit;
    masm32_sim_wasm_copy_instruction_source_span(
        &core_diagnostic->instruction,
        storage->source_text,
        &out_diagnostic->column,
        &out_diagnostic->byte_offset,
        &out_diagnostic->span_length,
        &out_diagnostic->has_source_span
    );

    target_range = masm32_sim_wasm_find_procedure_starting_at_instruction(
        parser_result,
        storage,
        (size_t)core_diagnostic->instruction.destination.immediate
    );
    out_diagnostic->has_target_procedure = masm32_sim_wasm_copy_procedure_name(
        out_diagnostic->target_procedure,
        sizeof(out_diagnostic->target_procedure),
        target_range != NULL ? target_range->name : NULL
    );

    if (parser_result != NULL && parser_result->has_selected_entry_procedure &&
        parser_result->selected_entry_procedure_index < parser_result->procedure_range_count) {
        const VmProcedureRange *entry_range = &storage->procedure_ranges[parser_result->selected_entry_procedure_index];
        out_diagnostic->has_selected_entry_procedure = masm32_sim_wasm_copy_procedure_name(
            out_diagnostic->selected_entry_procedure,
            sizeof(out_diagnostic->selected_entry_procedure),
            entry_range->name
        );
    }

    storage->has_call_depth_violation = true;
}

/// Appends a fatal Phase 72 call-depth-limit diagnostic.
///
/// @param writer Writer to mutate.
/// @param diagnostic Diagnostic to append.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_call_depth_violation(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmCallDepthDiagnostic *diagnostic
) {
    char message[512];

    if (writer == NULL || diagnostic == NULL || !diagnostic->has_diagnostic) {
        return false;
    }

    if (diagnostic->has_target_procedure) {
        (void)snprintf(
            message,
            sizeof(message),
            "call-depth-exceeded: direct CALL to '%s' would enter call depth %u, exceeding callDepthLimit %u. Execution stopped before writing the return token or changing control flow.",
            diagnostic->target_procedure,
            (unsigned int)diagnostic->attempted_depth,
            (unsigned int)diagnostic->limit
        );
    } else {
        (void)snprintf(
            message,
            sizeof(message),
            "call-depth-exceeded: direct CALL would enter call depth %u, exceeding callDepthLimit %u. Execution stopped before writing the return token or changing control flow.",
            (unsigned int)diagnostic->attempted_depth,
            (unsigned int)diagnostic->limit
        );
    }

    if (!masm32_sim_json_append(writer, "{\"kind\":\"resource-limit-error\",\"code\":\"call-depth-exceeded\",\"message\":")) {
        return false;
    }
    if (!masm32_sim_json_append_string(writer, message)) {
        return false;
    }
    if (diagnostic->line > 0U && !masm32_sim_json_append(writer, ",\"line\":%u", (unsigned int)diagnostic->line)) {
        return false;
    }
    if (diagnostic->column > 0U && !masm32_sim_json_append(writer, ",\"column\":%u", (unsigned int)diagnostic->column)) {
        return false;
    }
    if (diagnostic->has_source_span) {
        if (!masm32_sim_json_append(writer, ",\"byteOffset\":%llu", (unsigned long long)diagnostic->byte_offset)) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ",\"spanLength\":%llu", (unsigned long long)diagnostic->span_length)) {
            return false;
        }
    }
    if (diagnostic->has_target_procedure) {
        if (!masm32_sim_json_append(writer, ",\"procedure\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, diagnostic->target_procedure)) {
            return false;
        }
        if (!masm32_sim_json_append(writer, ",\"targetProcedure\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, diagnostic->target_procedure)) {
            return false;
        }
    }
    if (diagnostic->has_selected_entry_procedure) {
        if (!masm32_sim_json_append(writer, ",\"selectedEntryProcedure\":")) {
            return false;
        }
        if (!masm32_sim_json_append_string(writer, diagnostic->selected_entry_procedure)) {
            return false;
        }
    }

    return masm32_sim_json_append(
        writer,
        ",\"currentCallDepth\":%u,\"attemptedCallDepth\":%u,\"callDepthLimit\":%u}",
        (unsigned int)diagnostic->current_depth,
        (unsigned int)diagnostic->attempted_depth,
        (unsigned int)diagnostic->limit
    );
}

/// Builds one renderable Phase 71D procedure-fallthrough diagnostic.
///
/// @param out_diagnostic Destination diagnostic slot.
/// @param core_diagnostic Core VM procedure-fallthrough diagnostic.
/// @param parser_result Parser result containing procedure range count.
/// @param storage Source-run storage containing source text and procedure ranges.
/// @return true when a diagnostic was copied.
static bool masm32_sim_wasm_collect_procedure_fallthrough_diagnostic(
    Masm32SimWasmProcedureFallthroughDiagnostic *out_diagnostic,
    const VmProcedureFallthroughDiagnostic *core_diagnostic,
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage
) {
    const VmProcedureRange *from_range = NULL;
    const VmProcedureRange *to_range = NULL;

    if (out_diagnostic == NULL || core_diagnostic == NULL || !core_diagnostic->has_diagnostic) {
        return false;
    }

    memset(out_diagnostic, 0, sizeof(*out_diagnostic));
    out_diagnostic->has_diagnostic = true;
    out_diagnostic->from_instruction_index = core_diagnostic->from_instruction_index;
    out_diagnostic->to_instruction_index = core_diagnostic->to_instruction_index;

    if (core_diagnostic->has_from_instruction) {
        out_diagnostic->line = core_diagnostic->from_instruction.source_line;
        masm32_sim_wasm_copy_instruction_source_span(
            &core_diagnostic->from_instruction,
            storage != NULL ? storage->source_text : NULL,
            &out_diagnostic->column,
            &out_diagnostic->byte_offset,
            &out_diagnostic->span_length,
            &out_diagnostic->has_source_span
        );
    }

    from_range = masm32_sim_wasm_find_procedure_for_instruction(
        parser_result,
        storage,
        (size_t)core_diagnostic->from_instruction_index
    );
    to_range = masm32_sim_wasm_find_procedure_for_instruction(
        parser_result,
        storage,
        (size_t)core_diagnostic->to_instruction_index
    );

    out_diagnostic->has_from_procedure = masm32_sim_wasm_copy_procedure_name(
        out_diagnostic->from_procedure,
        sizeof(out_diagnostic->from_procedure),
        from_range != NULL ? from_range->name : NULL
    );
    out_diagnostic->has_to_procedure = masm32_sim_wasm_copy_procedure_name(
        out_diagnostic->to_procedure,
        sizeof(out_diagnostic->to_procedure),
        to_range != NULL ? to_range->name : NULL
    );

    return true;
}

/// Collects a non-fatal procedure-fallthrough warning after a successful VM step.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose latest procedure-fallthrough diagnostic should be inspected.
/// @param parser_result Parser result containing procedure metadata.
static void masm32_sim_wasm_collect_procedure_fallthrough_warning(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    const VmParserResult *parser_result
) {
    if (storage == NULL || vm == NULL || storage->procedure_fallthrough_warning_count >= (size_t)MASM32_SIM_WASM_MAX_RUN_WARNINGS) {
        return;
    }

    if (masm32_sim_wasm_collect_procedure_fallthrough_diagnostic(
            &storage->procedure_fallthrough_warnings[storage->procedure_fallthrough_warning_count],
            vm_last_procedure_fallthrough_diagnostic(vm),
            parser_result,
            storage
        )) {
        storage->procedure_fallthrough_warning_count += 1U;
    }
}

/// Collects a fatal procedure-fallthrough diagnostic after strict-mode stop.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose latest procedure-fallthrough diagnostic should be inspected.
/// @param parser_result Parser result containing procedure metadata.
static void masm32_sim_wasm_collect_procedure_fallthrough_violation(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    const VmParserResult *parser_result
) {
    if (storage == NULL || vm == NULL) {
        return;
    }

    storage->has_procedure_fallthrough_violation = masm32_sim_wasm_collect_procedure_fallthrough_diagnostic(
        &storage->procedure_fallthrough_violation,
        vm_last_procedure_fallthrough_diagnostic(vm),
        parser_result,
        storage
    );
}

/// Formats a Phase 71D procedure-fallthrough diagnostic.
///
/// @param diagnostic Diagnostic to format.
/// @param buffer Destination message buffer.
/// @param capacity Destination message buffer capacity.
static void masm32_sim_wasm_format_procedure_fallthrough_message(
    const Masm32SimWasmProcedureFallthroughDiagnostic *diagnostic,
    char *buffer,
    size_t capacity
) {
    if (buffer == NULL || capacity == 0U) {
        return;
    }

    if (diagnostic != NULL && diagnostic->has_from_procedure && diagnostic->has_to_procedure) {
        (void)snprintf(
            buffer,
            capacity,
            "Execution fell through from procedure '%s' into procedure '%s' without RET, JMP, exit, or another explicit control-transfer or termination instruction.",
            diagnostic->from_procedure,
            diagnostic->to_procedure
        );
    } else if (diagnostic != NULL && diagnostic->has_from_procedure) {
        (void)snprintf(
            buffer,
            capacity,
            "Execution fell through out of procedure '%s' without RET, JMP, exit, or another explicit control-transfer or termination instruction.",
            diagnostic->from_procedure
        );
    } else {
        (void)snprintf(
            buffer,
            capacity,
            "Execution fell through across a procedure boundary without RET, JMP, exit, or another explicit control-transfer or termination instruction."
        );
    }
}

/// Appends one Phase 71D procedure-fallthrough message.
///
/// @param writer Writer to mutate.
/// @param kind Simulator-message kind to emit.
/// @param diagnostic Diagnostic to render.
/// @return true when the message fit without overflowing the buffer.
static bool masm32_sim_json_append_procedure_fallthrough_message(
    Masm32SimJsonWriter *writer,
    const char *kind,
    const Masm32SimWasmProcedureFallthroughDiagnostic *diagnostic
) {
    char message[384];

    masm32_sim_wasm_format_procedure_fallthrough_message(diagnostic, message, sizeof(message));
    return masm32_sim_json_append_message_with_span_and_procedure(
        writer,
        kind,
        "procedure-fell-through",
        message,
        diagnostic != NULL ? diagnostic->line : 0U,
        diagnostic != NULL ? diagnostic->column : 0U,
        diagnostic != NULL ? diagnostic->byte_offset : 0U,
        diagnostic != NULL ? diagnostic->span_length : 0U,
        diagnostic != NULL && diagnostic->has_source_span,
        diagnostic != NULL && diagnostic->has_from_procedure ? diagnostic->from_procedure : NULL
    );
}

/// Appends collected Phase 71D procedure-fallthrough warnings.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing warnings.
/// @param inout_has_message Whether a prior message has already been appended.
/// @return true when warnings fit without overflowing the buffer.
static bool masm32_sim_json_append_procedure_fallthrough_warnings(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    bool *inout_has_message
) {
    size_t index = 0U;

    if (storage == NULL || inout_has_message == NULL) {
        return true;
    }

    for (index = 0U; index < storage->procedure_fallthrough_warning_count; index += 1U) {
        if (*inout_has_message && !masm32_sim_json_append(writer, ",")) {
            return false;
        }
        if (!masm32_sim_json_append_procedure_fallthrough_message(
                writer,
                "simulator-warning",
                &storage->procedure_fallthrough_warnings[index]
            )) {
            return false;
        }
        *inout_has_message = true;
    }

    return true;
}

/// Appends one strict Phase 71D procedure-fallthrough runtime diagnostic.
///
/// @param writer Writer to mutate.
/// @param diagnostic Strict diagnostic to render.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_procedure_fallthrough_violation(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmProcedureFallthroughDiagnostic *diagnostic
) {
    return masm32_sim_json_append_procedure_fallthrough_message(writer, "runtime-error", diagnostic);
}

/// Appends an executor diagnostic to the simulatorMessages array.
///
/// @param writer Writer to mutate.
/// @param diagnostic Executor diagnostic to render.
/// @param status Status associated with the diagnostic.
/// @param parser_result Parser metadata used to resolve procedure names for diagnostics.
/// @param storage Source-run storage containing copied source text and procedure ranges.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_exec_message(
    Masm32SimJsonWriter *writer,
    const VmExecDiagnostic *diagnostic,
    VmExecStatus status,
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage
) {
    const char *status_name = vm_exec_status_name(status);
    const char *message_code = status_name != NULL ? status_name : "execution-error";
    const char *message_text = "Execution failed while running the parsed program.";
    char memory_message[512];
    char div_zero_message[384];
    char quotient_overflow_message[384];
    const VmProcedureRange *procedure_range = NULL;
    const char *procedure_name = NULL;
    uint32_t line = 0U;
    uint32_t column = 0U;
    size_t byte_offset = 0U;
    size_t span_length = 0U;
    bool has_source_span = false;

    if (diagnostic != NULL && diagnostic->has_instruction) {
        line = diagnostic->instruction.source_line;
        if (diagnostic->status == VM_EXEC_STATUS_MEMORY_ERROR && diagnostic->memory_status == VM_MEMORY_STATUS_INVALID_ADDRESS) {
            masm32_sim_wasm_copy_bracketed_memory_source_span(
                &diagnostic->instruction,
                g_masm32_sim_wasm_run_storage.source_text,
                &column,
                &byte_offset,
                &span_length,
                &has_source_span
            );
            if (!has_source_span) {
                masm32_sim_wasm_copy_instruction_source_span(
                    &diagnostic->instruction,
                    g_masm32_sim_wasm_run_storage.source_text,
                    &column,
                    &byte_offset,
                    &span_length,
                    &has_source_span
                );
            }
        } else if ((diagnostic->status == VM_EXEC_STATUS_MEMORY_ERROR &&
                    (diagnostic->memory_status == VM_MEMORY_STATUS_UNSUPPORTED_CODE_MEMORY_ACCESS ||
                     (diagnostic->memory_status == VM_MEMORY_STATUS_REGION_BOUNDARY_CROSSING && diagnostic->memory_diagnostic.has_code_overlap))) ||
                   status == VM_EXEC_STATUS_DIVIDE_BY_ZERO || status == VM_EXEC_STATUS_QUOTIENT_OVERFLOW ||
                   status == VM_EXEC_STATUS_INVALID_BRANCH_TARGET || status == VM_EXEC_STATUS_INVALID_CALL_TARGET ||
                   status == VM_EXEC_STATUS_INVALID_RETURN_ADDRESS ||
                   status == VM_EXEC_STATUS_RET_STACK_CLEANUP_OUT_OF_RANGE ||
                   status == VM_EXEC_STATUS_UNSUPPORTED_PROC_USES_RUNTIME ||
                   status == VM_EXEC_STATUS_STACK_OVERFLOW ||
                   status == VM_EXEC_STATUS_STACK_UNDERFLOW ||
                   status == VM_EXEC_STATUS_PROCEDURE_FELL_THROUGH ||
                   status == VM_EXEC_STATUS_CODE_FELL_OFF_END ||
                   status == VM_EXEC_STATUS_ROOT_RET_DISALLOWED_BY_MODE ||
                   status == VM_EXEC_STATUS_INVALID_ROOT_TERMINATION_STATE ||
                   status == VM_EXEC_STATUS_BRANCH_RUNTIME_DEFERRED) {
            masm32_sim_wasm_copy_instruction_source_span(
                &diagnostic->instruction,
                g_masm32_sim_wasm_run_storage.source_text,
                &column,
                &byte_offset,
                &span_length,
                &has_source_span
            );
        }
    }

    if (diagnostic != NULL && diagnostic->status == VM_EXEC_STATUS_MEMORY_ERROR) {
        message_code = masm32_sim_wasm_format_memory_error_message(diagnostic, memory_message, sizeof(memory_message));
        message_text = memory_message;
    } else if (status == VM_EXEC_STATUS_DIVIDE_BY_ZERO) {
        message_code = "divide-by-zero";
        message_text = masm32_sim_wasm_format_divide_by_zero_message(
            diagnostic != NULL && diagnostic->has_instruction ? &diagnostic->instruction : NULL,
            div_zero_message,
            sizeof(div_zero_message)
        );
    } else if (status == VM_EXEC_STATUS_QUOTIENT_OVERFLOW) {
        message_code = "quotient-overflow";
        message_text = masm32_sim_wasm_format_div_quotient_overflow_message(
            diagnostic != NULL && diagnostic->has_instruction ? &diagnostic->instruction : NULL,
            quotient_overflow_message,
            sizeof(quotient_overflow_message)
        );
    } else if (status == VM_EXEC_STATUS_INVALID_BRANCH_TARGET) {
        message_code = "invalid-branch-target";
        message_text = "Direct JMP target metadata is invalid for the loaded program. Execution stopped before applying the jump.";
    } else if (status == VM_EXEC_STATUS_INVALID_CALL_TARGET) {
        message_code = "invalid-call-target";
        message_text = "Direct CALL target metadata is invalid for the loaded program. Execution stopped before applying the call.";
    } else if (status == VM_EXEC_STATUS_INVALID_RETURN_ADDRESS) {
        message_code = "invalid-return-address";
        message_text = "RET read a return token that does not map to an executable pseudo-EIP instruction target. Execution stopped before changing ESP or transferring control.";
    } else if (status == VM_EXEC_STATUS_RET_STACK_CLEANUP_OUT_OF_RANGE) {
        message_code = "ret-stack-cleanup-out-of-range";
        message_text = "RET imm16 cleanup would leave ESP outside the active stack region or empty-stack boundary. Execution stopped before changing ESP or transferring control.";
    } else if (status == VM_EXEC_STATUS_UNSUPPORTED_PROC_USES_RUNTIME) {
        message_code = "unsupported-proc-uses-runtime";
        message_text = "PROC USES metadata requires direct CALL entry in this phase. Execution stopped before entering the procedure so the simulator does not silently ignore USES.";
    } else if (status == VM_EXEC_STATUS_STACK_OVERFLOW) {
        message_code = "stack-overflow";
        message_text = "Automatic PROC USES register save could not reserve or write the required stack slots. Execution stopped before entering the procedure.";
    } else if (status == VM_EXEC_STATUS_STACK_UNDERFLOW) {
        message_code = "stack-underflow";
        message_text = "Automatic PROC USES register restore could not read the saved register slots. Execution stopped before restoring registers or popping the return token.";
    } else if (status == VM_EXEC_STATUS_PROCEDURE_FELL_THROUGH) {
        message_code = "procedure-fell-through";
        message_text = "Execution fell through out of a procedure without RET, JMP, exit, or another explicit control-transfer or termination instruction.";
    } else if (status == VM_EXEC_STATUS_CODE_FELL_OFF_END) {
        message_code = "code-fell-off-end";
        message_text = "Execution reached the end of the executable code stream without an explicit program terminator. Did you forget to add RET or Irvine32 exit?";
    } else if (status == VM_EXEC_STATUS_ROOT_RET_DISALLOWED_BY_MODE) {
        message_code = "root-ret-disallowed-by-mode";
        message_text = "RET cannot return because no caller supplied a return address. Use the simulator's MASM32-compatible root RET mode, or terminate explicitly with the supported Irvine32 exit routine.";
    } else if (status == VM_EXEC_STATUS_INVALID_ROOT_TERMINATION_STATE) {
        message_code = "invalid-root-termination-state";
        message_text = "The VM detected inconsistent root/helper RET termination metadata. Execution stopped before mutating ESP or transferring control.";
    } else if (status == VM_EXEC_STATUS_BRANCH_RUNTIME_DEFERRED) {
        message_code = "branch-runtime-deferred";
        message_text = "A branch form was accepted for metadata, but runtime branch execution for that form is deferred to a later branch phase. Execution stopped before applying the branch.";
    }

    if (diagnostic != NULL && diagnostic->has_instruction) {
        if (status == VM_EXEC_STATUS_STACK_OVERFLOW && diagnostic->instruction.opcode == VM_IR_OPCODE_CALL &&
            diagnostic->instruction.destination.kind == VM_IR_OPERAND_BRANCH_TARGET) {
            procedure_range = masm32_sim_wasm_find_procedure_starting_at(
                parser_result,
                storage,
                (size_t)diagnostic->instruction.destination.immediate
            );
        } else if (status == VM_EXEC_STATUS_STACK_UNDERFLOW || status == VM_EXEC_STATUS_UNSUPPORTED_PROC_USES_RUNTIME) {
            procedure_range = masm32_sim_wasm_find_procedure_for_instruction(
                parser_result,
                storage,
                (size_t)diagnostic->instruction_index
            );
        }
    }
    procedure_name = procedure_range != NULL ? procedure_range->name : NULL;

    return masm32_sim_json_append_message_with_span_and_procedure(
        writer,
        "runtime-error",
        message_code,
        message_text,
        line,
        column,
        byte_offset,
        span_length,
        has_source_span,
        procedure_name
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
/// @param layout_policy Selected layout policy, or NULL for the fixed default.
/// @return true when the metadata was omitted or appended successfully.
static bool masm32_sim_json_append_uninitialized_metadata(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage,
    const VmLayoutPolicy *layout_policy
) {
    size_t index = 0U;
    bool has_object = false;
    VmLayoutPolicy default_policy;
    const VmLayoutPolicy *effective_policy = layout_policy;

    if (writer == NULL || storage == NULL) {
        return false;
    }
    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    if (!masm32_sim_json_append(writer, ",\"uninitializedOrigin\":{\"tracked\":true,\"objects\":[")) {
        return false;
    }

    for (index = 0U; index < storage->object_map_entry_count; index += 1U) {
        const VmObjectMapEntry *object = &storage->object_map_entries[index];
        const uint8_t *mask = NULL;
        size_t mask_size = 0U;
        uint32_t mask_base = 0U;
        uint32_t mask_offset = 0U;
        uint32_t byte_index = 0U;

        if (object->section == VM_SYMBOL_SECTION_CONST) {
            mask = storage->const_initialized_mask;
            mask_size = storage->const_initialized_mask_size;
            mask_base = effective_policy->regions[VM_LAYOUT_REGION_CONST].base;
        } else {
            mask = storage->data_initialized_mask;
            mask_size = storage->data_initialized_mask_size;
            mask_base = effective_policy->regions[VM_LAYOUT_REGION_DATA].base;
        }

        if (mask == NULL || object->base_address < mask_base) {
            continue;
        }
        mask_offset = object->base_address - mask_base;
        if ((uint64_t)mask_offset + (uint64_t)object->size_bytes > (uint64_t)mask_size) {
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
                if (mask[(size_t)mask_offset + (size_t)byte_index] != 0U) {
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
                        mask[(size_t)mask_offset + (size_t)byte_index] != 0U ? '1' : '0'
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

/// Appends Phase 59 instruction-limit accounting fields to a source-run JSON response.
///
/// @param writer Writer to mutate.
/// @param vm VM whose execution counters should be inspected, if available.
/// @param storage Source-run storage containing configured limit and limit-failure metadata.
/// @return true when all fields fit without overflowing the buffer.
static bool masm32_sim_json_append_instruction_limit_metadata(
    Masm32SimJsonWriter *writer,
    const Vm *vm,
    const Masm32SimWasmRunStorage *storage
) {
    uint32_t instruction_limit = storage != NULL && storage->instruction_limit != 0U
        ? storage->instruction_limit
        : (uint32_t)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT;
    uint32_t call_depth_limit = storage != NULL && storage->call_depth_limit != 0U
        ? storage->call_depth_limit
        : (uint32_t)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT;
    uint64_t executed_count = vm != NULL ? vm->instruction_count : 0U;
    bool has_current_index = false;
    uint32_t current_index = 0U;

    if (storage != NULL && (storage->has_instruction_limit_violation || storage->has_attempted_next_instruction_index)) {
        executed_count = storage->executed_instruction_count;
        has_current_index = storage->has_current_instruction_index;
        current_index = storage->current_instruction_index;
    } else if (vm != NULL && vm->program != NULL && vm->instruction_count > 0U && vm->instruction_pointer > 0U && (vm->instruction_pointer - 1U) < vm->program_count) {
        current_index = vm->program[vm->instruction_pointer - 1U].instruction_index;
        has_current_index = true;
    }

    if (!masm32_sim_json_append(
            writer,
            "\"instructionLimit\":%u,\"callDepthLimit\":%u,\"executedInstructionCount\":%llu,\"attemptedNextInstructionIndex\":",
            (unsigned int)instruction_limit,
            (unsigned int)call_depth_limit,
            (unsigned long long)executed_count
        )) {
        return false;
    }

    if (storage != NULL && storage->has_attempted_next_instruction_index) {
        if (!masm32_sim_json_append(writer, "%u", (unsigned int)storage->attempted_next_instruction_index)) {
            return false;
        }
    } else if (!masm32_sim_json_append(writer, "null")) {
        return false;
    }

    if (!masm32_sim_json_append(writer, ",\"currentInstructionIndex\":")) {
        return false;
    }
    if (has_current_index) {
        if (!masm32_sim_json_append(writer, "%u", (unsigned int)current_index)) {
            return false;
        }
    } else if (!masm32_sim_json_append(writer, "null")) {
        return false;
    }

    return masm32_sim_json_append(writer, ",");
}

/// Appends the Phase 59 instruction-limit failure diagnostic.
///
/// @param writer Writer to mutate.
/// @param storage Source-run storage containing limit-failure metadata.
/// @return true when the diagnostic fit without overflowing the buffer.
static bool masm32_sim_json_append_instruction_limit_violation(
    Masm32SimJsonWriter *writer,
    const Masm32SimWasmRunStorage *storage
) {
    char message[256];

    if (storage == NULL) {
        return masm32_sim_json_append_message_with_span(
            writer,
            "runtime-error",
            "instruction-limit-exceeded",
            "Execution stopped because the configured instruction limit was reached.",
            0U,
            0U,
            0U,
            0U,
            false
        );
    }

    const uint64_t display_instruction_number =
        (uint64_t)storage->attempted_next_instruction_index + 1ULL;

    (void)snprintf(
        message,
        sizeof(message),
        "Instruction limit exceeded: attempted to execute instruction #%llu (limit: %u). Program stopped before executing that instruction.",
        (unsigned long long)display_instruction_number,
        (unsigned int)storage->instruction_limit
    );

    return masm32_sim_json_append_message_with_span(
        writer,
        "runtime-error",
        "instruction-limit-exceeded",
        message,
        storage->instruction_limit_line,
        storage->instruction_limit_column,
        storage->instruction_limit_byte_offset,
        storage->instruction_limit_span_length,
        storage->instruction_limit_has_source_span
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
/// @param layout_policy Optional selected randomized layout policy to serialize.
/// @param layout_message Optional layout failure message to serialize.
/// @param include_uninitialized_metadata Whether to append test-only Phase 39 metadata.
/// @param emit_startup_state_notice Whether successful runs should include a startup-state notice.
/// @param startup_register_flag_mode Startup register/flag mode used for notice text.
/// @param uninitialized_storage_visible_byte_mode Uninitialized-storage visible-byte mode used for notice text.
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
    bool include_uninitialized_metadata,
    bool emit_startup_state_notice,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode
) {
    Masm32SimJsonWriter writer;
    uint64_t instruction_count = vm != NULL ? vm->instruction_count : 0U;
    bool ok = outcome == MASM32_SIM_WASM_RUN_OUTCOME_OK;

    memset(g_masm32_sim_wasm_run_json, 0, sizeof(g_masm32_sim_wasm_run_json));
    writer.buffer = g_masm32_sim_wasm_run_json;
    writer.capacity = sizeof(g_masm32_sim_wasm_run_json);
    writer.length = 0U;
    writer.overflowed = false;

    (void)masm32_sim_json_append_source_run_metadata(&writer);
    (void)masm32_sim_json_append(&writer, ",\"ok\":%s,\"status\":", ok ? "true" : "false");
    (void)masm32_sim_json_append_string(&writer, masm32_sim_wasm_run_outcome_name(outcome));
    (void)masm32_sim_json_append(&writer, ",\"instructionCount\":%llu,", (unsigned long long)instruction_count);
    (void)masm32_sim_json_append_instruction_limit_metadata(&writer, vm, storage);
    (void)masm32_sim_json_append(&writer, "\"rootRetMode\":");
    (void)masm32_sim_json_append_string(
        &writer,
        masm32_sim_wasm_root_ret_mode_name(storage != NULL ? storage->root_ret_mode : MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE)
    );
    (void)masm32_sim_json_append(&writer, ",\"procedureFallthroughPolicy\":");
    (void)masm32_sim_json_append_string(
        &writer,
        masm32_sim_wasm_procedure_fallthrough_policy_name(storage != NULL ? storage->procedure_fallthrough_policy : masm32_sim_wasm_default_procedure_fallthrough_policy())
    );
    (void)masm32_sim_json_append(&writer, ",\"entryProcedureEndMode\":");
    (void)masm32_sim_json_append_string(
        &writer,
        masm32_sim_wasm_entry_procedure_end_mode_name(storage != NULL ? storage->entry_procedure_end_mode : masm32_sim_wasm_default_entry_procedure_end_mode())
    );
    (void)masm32_sim_json_append(&writer, ",");
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
        (void)masm32_sim_json_append_register_write_metadata(&writer, &vm->cpu);
        (void)masm32_sim_json_append(&writer, ",");
        (void)masm32_sim_json_append_register_role_metadata(&writer);
        (void)masm32_sim_json_append(&writer, ",");
    }

    (void)masm32_sim_json_append_memory_changes(&writer, storage);
    if (include_uninitialized_metadata) {
        (void)masm32_sim_json_append_uninitialized_metadata(&writer, storage, layout_policy);
    }
    (void)masm32_sim_json_append(&writer, ",");

    (void)masm32_sim_json_append(&writer, "\"simulatorMessages\":[");
    if (outcome == MASM32_SIM_WASM_RUN_OUTCOME_OK) {
        bool has_message = false;
        if (emit_startup_state_notice) {
            (void)masm32_sim_json_append_startup_state_notice(
                &writer,
                &has_message,
                startup_register_flag_mode,
                uninitialized_storage_visible_byte_mode
            );
        }
        (void)masm32_sim_json_append_nonfatal_parser_messages(
            &writer,
            parser_diagnostics,
            parser_result != NULL ? parser_result->diagnostic_count : 0U,
            &has_message
        );
        (void)masm32_sim_json_append_section_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_object_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_uninitialized_read_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_shift_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_flag_use_warnings(&writer, storage, &has_message);
        (void)masm32_sim_json_append_procedure_fallthrough_warnings(&writer, storage, &has_message);
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
        bool has_message = false;
        if (emit_startup_state_notice) {
            (void)masm32_sim_json_append_startup_state_notice(
                &writer,
                &has_message,
                startup_register_flag_mode,
                uninitialized_storage_visible_byte_mode
            );
        }
        (void)masm32_sim_json_append_nonfatal_parser_messages(
            &writer,
            parser_diagnostics,
            parser_result != NULL ? parser_result->diagnostic_count : 0U,
            &has_message
        );
        if (exec_status == VM_EXEC_STATUS_DIVIDE_BY_ZERO || exec_status == VM_EXEC_STATUS_QUOTIENT_OVERFLOW) {
            (void)masm32_sim_json_append_uninitialized_read_warnings(&writer, storage, &has_message);
        }
        (void)masm32_sim_json_append_procedure_fallthrough_warnings(&writer, storage, &has_message);
        if (has_message) {
            (void)masm32_sim_json_append(&writer, ",");
        }
        if (exec_status == VM_EXEC_STATUS_CODE_FELL_OFF_END) {
            (void)masm32_sim_json_append_code_fell_off_end_messages(&writer, vm, parser_result, storage);
        } else if (storage != NULL && storage->has_section_violation) {
            (void)masm32_sim_json_append_section_violation(&writer, &storage->section_violation);
        } else if (storage != NULL && storage->has_object_violation) {
            (void)masm32_sim_json_append_object_violation(&writer, &storage->object_violation);
        } else if (storage != NULL && storage->has_uninitialized_read_violation) {
            (void)masm32_sim_json_append_uninitialized_read_violation(&writer, &storage->uninitialized_read_violation);
        } else if (storage != NULL && storage->has_shift_violation) {
            (void)masm32_sim_json_append_shift_violation(&writer, &storage->shift_violation);
        } else if (storage != NULL && storage->has_flag_use_violation) {
            (void)masm32_sim_json_append_flag_use_violation(&writer, &storage->flag_use_violation);
        } else if (storage != NULL && storage->has_call_depth_violation) {
            (void)masm32_sim_json_append_call_depth_violation(&writer, &storage->call_depth_violation);
        } else if (storage != NULL && storage->has_instruction_limit_violation) {
            (void)masm32_sim_json_append_instruction_limit_violation(&writer, storage);
        } else if (storage != NULL && storage->has_procedure_fallthrough_violation) {
            (void)masm32_sim_json_append_procedure_fallthrough_violation(&writer, &storage->procedure_fallthrough_violation);
        } else {
            (void)masm32_sim_json_append_exec_message(&writer, vm != NULL ? vm_last_diagnostic(vm) : NULL, exec_status, parser_result, storage);
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
            "{\"phase\":%u,\"phaseSuffix\":\"%s\",\"phaseName\":\"%s\",\"sourceRunOutputContract\":\"%s\",\"ok\":false,\"status\":\"response-truncated\",\"instructionCount\":0,\"instructionLimit\":%u,\"callDepthLimit\":%u,\"executedInstructionCount\":0,\"attemptedNextInstructionIndex\":null,\"currentInstructionIndex\":null,\"memoryChanges\":[],\"simulatorMessages\":[{\"kind\":\"internal-simulator-error\",\"code\":\"response-truncated\",\"message\":\"The simulator response exceeded its fixed buffer.\"}]}",
            (unsigned int)MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER,
            MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX,
            MASM32_SIM_WASM_RUNTIME_PHASE_NAME,
            MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT,
            storage != NULL && storage->instruction_limit != 0U
                ? (unsigned int)storage->instruction_limit
                : (unsigned int)MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
            storage != NULL && storage->call_depth_limit != 0U
                ? (unsigned int)storage->call_depth_limit
                : (unsigned int)MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT
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
        storage->const_initialized_mask,
        storage->const_initialized_mask_size,
        storage->object_map_entries,
        (size_t)MASM32_SIM_WASM_MAX_OBJECT_MAP_ENTRIES,
        &storage->object_map_entry_count
    );

    return status == VM_OBJECT_MAP_STATUS_OK;
}

/// Updates Phase 59 instruction accounting metadata from the current VM state.
///
/// @param storage Source-run storage to mutate.
/// @param vm VM whose execution state should be copied.
static void masm32_sim_wasm_update_instruction_accounting(Masm32SimWasmRunStorage *storage, const Vm *vm) {
    if (storage == NULL || vm == NULL) {
        return;
    }

    storage->executed_instruction_count = vm->instruction_count;
    if (vm->instruction_count > 0U && vm->last_delta.has_instruction) {
        storage->current_instruction_index = vm->last_delta.instruction.instruction_index;
        storage->has_current_instruction_index = true;
    } else {
        storage->has_current_instruction_index = false;
    }
}

/// Enforces the Phase 59 instruction-count limit before fetching the next instruction.
///
/// @param storage Source-run storage to mutate with diagnostic metadata.
/// @param vm VM whose next instruction would be fetched.
/// @param instruction_limit Positive maximum number of VM instructions to execute.
/// @return OK when execution may continue, or INSTRUCTION_LIMIT_EXCEEDED before the next instruction fetch.
static VmExecStatus masm32_sim_wasm_validate_instruction_limit_before_step(
    Masm32SimWasmRunStorage *storage,
    const Vm *vm,
    uint32_t instruction_limit
) {
    const VmIrInstruction *instruction = NULL;

    if (storage == NULL || vm == NULL || vm->halted || vm->instruction_pointer >= vm->program_count || vm->program == NULL) {
        return VM_EXEC_STATUS_OK;
    }

    storage->instruction_limit = instruction_limit;
    masm32_sim_wasm_update_instruction_accounting(storage, vm);
    if (vm->instruction_count < (uint64_t)instruction_limit) {
        return VM_EXEC_STATUS_OK;
    }

    instruction = &vm->program[vm->instruction_pointer];
    storage->has_instruction_limit_violation = true;
    storage->attempted_next_instruction_index = instruction->instruction_index;
    storage->has_attempted_next_instruction_index = true;
    storage->executed_instruction_count = vm->instruction_count;
    masm32_sim_wasm_copy_instruction_source_span(
        instruction,
        storage->source_text,
        &storage->instruction_limit_column,
        &storage->instruction_limit_byte_offset,
        &storage->instruction_limit_span_length,
        &storage->instruction_limit_has_source_span
    );
    storage->instruction_limit_line = instruction->source_line;

    return VM_EXEC_STATUS_INSTRUCTION_LIMIT_EXCEEDED;
}

/// Configures executor procedure boundaries from parser-owned procedure ranges.
///
/// @param vm VM instance already loaded with the lowered instruction program.
/// @param parser_result Parser result containing the selected entry metadata.
/// @param storage Source-run storage containing procedure ranges.
/// @return Executor status from the boundary configuration helper.
static VmExecStatus masm32_sim_wasm_configure_exec_procedure_boundaries(
    Vm *vm,
    const VmParserResult *parser_result,
    const Masm32SimWasmRunStorage *storage
) {
    VmExecProcedureBoundary boundaries[MASM32_SIM_WASM_MAX_RUN_PROCEDURE_RANGES];
    size_t index = 0U;

    if (vm == NULL || parser_result == NULL || storage == NULL) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }
    if (parser_result->procedure_range_count > (size_t)MASM32_SIM_WASM_MAX_RUN_PROCEDURE_RANGES) {
        return VM_EXEC_STATUS_INVALID_ARGUMENT;
    }

    memset(boundaries, 0, sizeof(boundaries));
    for (index = 0U; index < parser_result->procedure_range_count; index += 1U) {
        const VmProcedureRange *range = &storage->procedure_ranges[index];
        boundaries[index].start_instruction_index = range->start_instruction_index;
        boundaries[index].end_instruction_index = range->end_instruction_index;
        boundaries[index].has_executable_instruction = range->has_executable_instruction;
        boundaries[index].uses_register_count = range->uses_register_count;
        {
            size_t uses_index = 0U;
            for (uses_index = 0U; uses_index < range->uses_register_count && uses_index < (size_t)VM_EXEC_PROCEDURE_USES_REGISTER_CAPACITY; uses_index += 1U) {
                boundaries[index].uses_registers[uses_index] = range->uses_registers[uses_index];
            }
        }
        boundaries[index].is_selected_entry = parser_result->has_selected_entry_procedure &&
            index == parser_result->selected_entry_procedure_index;
    }

    return vm_configure_procedure_boundaries(vm, boundaries, parser_result->procedure_range_count);
}

/// Parses, optionally applies policy-selected layout, and executes source.
///
/// @param source Source text to run.
/// @param requested_layout_mode Fixed, automatic, seeded-randomized, or fresh-randomized layout mode.
/// @param base_policy Optional base policy for non-fixed layout modes.
/// @param object_validation_mode Optional Level 4 declared-object validation behavior.
/// @param uninitialized_validation_mode Optional uninitialized-read diagnostic behavior.
/// @param capacity_policy Optional Level 2 section-capacity validation behavior.
/// @param image_policy Optional Level 3 section-image validation behavior.
/// @param shift_mode Shift undefined modeled-flag validation behavior.
/// @param flag_use_policy Undefined flag-use consumer policy.
/// @param compatibility_notice_setting Whether parser compatibility notices are emitted.
/// @param startup_state_notice_setting Whether startup-state notices are emitted.
/// @param startup_register_flag_mode Phase 57F register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Phase 57G uninitialized-storage visible-byte mode.
/// @param startup_state_seed Deterministic shared startup seed.
/// @param instruction_limit Positive Phase 59 source-run instruction-count limit.
/// @param call_depth_limit Positive Phase 72 direct user-procedure CALL depth limit.
/// @param root_ret_mode Phase 71A root-code-stream RET handling mode.
/// @param procedure_fallthrough_policy Phase 71D procedure-fallthrough diagnostic policy.
/// @param include_uninitialized_metadata Whether to include test-only initialization metadata.
/// @return Pointer to the static JSON result buffer.
static const char *masm32_sim_wasm_run_source_json_internal_with_procedure_fallthrough_policy(
    const char *source,
    VmLayoutMode requested_layout_mode,
    const VmLayoutPolicy *base_policy,
    Masm32SimWasmMemoryValidationMode object_validation_mode,
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy,
    Masm32SimWasmShiftValidationMode shift_mode,
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    VmDiagnosticPolicyValue const_uninitialized_storage_policy,
    Masm32SimWasmStartupStateNoticeSetting startup_state_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    uint32_t call_depth_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy,
    Masm32SimWasmEntryProcedureEndMode entry_procedure_end_mode,
    bool include_uninitialized_metadata
) {
    VmParserConfig config;
    VmParserResult parser_result;
    Vm *vm = &g_masm32_sim_wasm_run_storage.vm;
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

    if (!masm32_sim_wasm_startup_register_flag_mode_is_valid(startup_register_flag_mode)) {
        return masm32_sim_wasm_build_invalid_startup_setting_json(
            "startup_register_flag_mode",
            "Invalid startup setting 'startup_register_flag_mode'. Accepted values: zero, seeded-random.",
            "zero, seeded-random"
        );
    }

    if (!masm32_sim_wasm_uninitialized_storage_visible_byte_mode_is_valid(uninitialized_storage_visible_byte_mode)) {
        return masm32_sim_wasm_build_invalid_startup_setting_json(
            "uninitialized_storage_visible_byte_mode",
            "Invalid startup setting 'uninitialized_storage_visible_byte_mode'. Accepted values: zero, seeded-random.",
            "zero, seeded-random"
        );
    }

    if (instruction_limit == 0U) {
        return masm32_sim_wasm_build_invalid_instruction_limit_json(instruction_limit);
    }

    if (!masm32_sim_wasm_call_depth_limit_is_valid(call_depth_limit)) {
        return masm32_sim_wasm_build_invalid_call_depth_limit_json(call_depth_limit);
    }

    if (!masm32_sim_wasm_root_ret_mode_is_valid(root_ret_mode)) {
        return masm32_sim_wasm_build_invalid_root_ret_mode_json(root_ret_mode);
    }
    if (!masm32_sim_wasm_procedure_fallthrough_policy_is_valid(procedure_fallthrough_policy)) {
        return masm32_sim_wasm_build_invalid_procedure_fallthrough_policy_json(procedure_fallthrough_policy);
    }
    if (!masm32_sim_wasm_entry_procedure_end_mode_is_valid(entry_procedure_end_mode)) {
        return masm32_sim_wasm_build_invalid_entry_procedure_end_mode_json(entry_procedure_end_mode);
    }

    if (source == NULL) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
    }

    memset(&g_masm32_sim_wasm_run_storage, 0, sizeof(g_masm32_sim_wasm_run_storage));
    memset(&config, 0, sizeof(config));
    memset(&parser_result, 0, sizeof(parser_result));
    memset(vm, 0, sizeof(*vm));
    memset(&selected_policy, 0, sizeof(selected_policy));
    memset(&layout_metadata, 0, sizeof(layout_metadata));
    memset(&layout_diagnostic, 0, sizeof(layout_diagnostic));
    memset(&layout_message, 0, sizeof(layout_message));
    g_masm32_sim_wasm_run_storage.source_text = source;
    g_masm32_sim_wasm_run_storage.instruction_limit = instruction_limit;
    g_masm32_sim_wasm_run_storage.call_depth_limit = call_depth_limit;
    g_masm32_sim_wasm_run_storage.root_ret_mode = root_ret_mode;
    g_masm32_sim_wasm_run_storage.procedure_fallthrough_policy = procedure_fallthrough_policy;
    g_masm32_sim_wasm_run_storage.entry_procedure_end_mode = entry_procedure_end_mode;

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
    config.code_labels = g_masm32_sim_wasm_run_storage.code_labels;
    config.code_label_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_CODE_LABELS;
    config.procedure_ranges = g_masm32_sim_wasm_run_storage.procedure_ranges;
    config.procedure_range_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_PROCEDURE_RANGES;
    config.data_image = g_masm32_sim_wasm_run_storage.data_image;
    config.data_image_capacity = (size_t)MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES;
    config.data_initialized_mask = g_masm32_sim_wasm_run_storage.data_initialized_mask;
    config.data_initialized_mask_capacity = (size_t)MASM32_SIM_WASM_RUN_DATA_IMAGE_BYTES;
    config.const_image = g_masm32_sim_wasm_run_storage.const_image;
    config.const_image_capacity = (size_t)MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES;
    config.const_initialized_mask = g_masm32_sim_wasm_run_storage.const_initialized_mask;
    config.const_initialized_mask_capacity = (size_t)MASM32_SIM_WASM_RUN_CONST_IMAGE_BYTES;
    config.diagnostics = g_masm32_sim_wasm_run_storage.parser_diagnostics;
    config.diagnostic_capacity = (size_t)MASM32_SIM_WASM_MAX_RUN_PARSER_DIAGNOSTICS;
    config.suppress_compatibility_notices = compatibility_notice_setting == MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF;
    config.const_uninitialized_storage_policy = const_uninitialized_storage_policy;

    parser_status = vm_parser_parse_program(&config, &parser_result);
    g_masm32_sim_wasm_run_storage.data_initialized_mask_size = parser_result.data_size;
    g_masm32_sim_wasm_run_storage.const_initialized_mask_size = parser_result.const_size;
    g_masm32_sim_wasm_run_storage.data_section_image_size = (uint32_t)parser_result.data_size;
    g_masm32_sim_wasm_run_storage.const_section_image_size = (uint32_t)parser_result.const_size;
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
            include_uninitialized_metadata,
            startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON,
            startup_register_flag_mode,
            uninitialized_storage_visible_byte_mode
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
                include_uninitialized_metadata,
                startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON,
                startup_register_flag_mode,
                uninitialized_storage_visible_byte_mode
            );
        }

        if (use_randomized_layout && !masm32_sim_wasm_relocate_parser_output(&parser_result, &g_masm32_sim_wasm_run_storage, &selected_policy)) {
            return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        }

        runtime_policy = &selected_policy;
        exec_status = vm_init_with_layout_policy(vm, runtime_policy);
    } else {
        exec_status = vm_init(vm, NULL);
    }

    if (exec_status != VM_EXEC_STATUS_OK) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
    }
    vm_initialized = true;

    exec_status = vm_set_root_ret_mode(vm, masm32_sim_wasm_map_root_ret_mode(root_ret_mode));
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        vm_deinit(vm);
        return json;
    }

    exec_status = vm_set_procedure_fallthrough_policy(vm, masm32_sim_wasm_map_procedure_fallthrough_policy(procedure_fallthrough_policy));
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        vm_deinit(vm);
        return json;
    }

    exec_status = vm_set_entry_procedure_end_mode(vm, masm32_sim_wasm_map_entry_procedure_end_mode(entry_procedure_end_mode));
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        vm_deinit(vm);
        return json;
    }

    exec_status = vm_set_call_depth_limit(vm, call_depth_limit);
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        vm_deinit(vm);
        return json;
    }

    if (!masm32_sim_wasm_build_declared_object_map(&parser_result, &g_masm32_sim_wasm_run_storage, runtime_policy)) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, VM_EXEC_STATUS_INVALID_ARGUMENT, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        vm_deinit(vm);
        return json;
    }

    if (uninitialized_storage_visible_byte_mode == MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM) {
        masm32_sim_wasm_seed_uninitialized_storage_visible_bytes(&g_masm32_sim_wasm_run_storage, startup_state_seed);
    }

    exec_status = masm32_sim_wasm_load_section_image(vm, VM_MEMORY_REGION_DATA, g_masm32_sim_wasm_run_storage.data_image, (uint32_t)parser_result.data_size);
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        vm_deinit(vm);
        return json;
    }

    exec_status = masm32_sim_wasm_load_section_image(vm, VM_MEMORY_REGION_CONST, g_masm32_sim_wasm_run_storage.const_image, (uint32_t)parser_result.const_size);
    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        vm_deinit(vm);
        return json;
    }

    vm_memory_clear_changes(&vm->memory);

    exec_status = vm_load_program(vm, g_masm32_sim_wasm_run_storage.instructions, parser_result.instruction_count);
    if (exec_status == VM_EXEC_STATUS_OK) {
        exec_status = masm32_sim_wasm_configure_exec_procedure_boundaries(vm, &parser_result, &g_masm32_sim_wasm_run_storage);
    }
    if (exec_status == VM_EXEC_STATUS_OK && parser_result.has_selected_entry_procedure) {
        if (parser_result.selected_entry_start_instruction_index > parser_result.instruction_count ||
            parser_result.selected_entry_end_instruction_index > parser_result.instruction_count ||
            parser_result.selected_entry_start_instruction_index > parser_result.selected_entry_end_instruction_index) {
            exec_status = VM_EXEC_STATUS_INVALID_ARGUMENT;
        } else {
            vm->instruction_pointer = parser_result.selected_entry_start_instruction_index;
            if (!vm_sync_display_eip(vm)) {
                exec_status = VM_EXEC_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    if (exec_status == VM_EXEC_STATUS_OK && startup_register_flag_mode == MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM) {
        vm_cpu_init_seeded_registers_and_flags(&vm->cpu, startup_state_seed);
        exec_status = vm_initialize_stack_pointer(vm);
        if (exec_status == VM_EXEC_STATUS_OK && !vm_sync_display_eip(vm)) {
            exec_status = VM_EXEC_STATUS_INVALID_ARGUMENT;
        }
    }

    while (exec_status == VM_EXEC_STATUS_OK && !vm->halted) {
        if (!vm_sync_display_eip(vm)) {
            exec_status = VM_EXEC_STATUS_INVALID_ARGUMENT;
            break;
        }

        exec_status = masm32_sim_wasm_validate_instruction_limit_before_step(
            &g_masm32_sim_wasm_run_storage,
            vm,
            instruction_limit
        );
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = masm32_sim_wasm_validate_section_accesses_before_step(
            &g_masm32_sim_wasm_run_storage,
            vm,
            capacity_policy,
            image_policy,
            runtime_policy
        );
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = masm32_sim_wasm_validate_object_accesses_before_step(
            &g_masm32_sim_wasm_run_storage,
            vm,
            object_validation_mode,
            runtime_policy
        );
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = masm32_sim_wasm_validate_uninitialized_reads_before_step(
            &g_masm32_sim_wasm_run_storage,
            vm,
            uninitialized_validation_mode,
            runtime_policy
        );
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = masm32_sim_wasm_validate_shift_before_step(&g_masm32_sim_wasm_run_storage, vm, shift_mode);
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = masm32_sim_wasm_validate_flag_use_before_step(&g_masm32_sim_wasm_run_storage, vm, flag_use_policy);
        if (exec_status != VM_EXEC_STATUS_OK) {
            break;
        }

        exec_status = vm_step(vm);
        if (exec_status == VM_EXEC_STATUS_OK) {
            masm32_sim_wasm_collect_procedure_fallthrough_warning(&g_masm32_sim_wasm_run_storage, vm, &parser_result);
            masm32_sim_wasm_collect_unaligned_warnings(&g_masm32_sim_wasm_run_storage, vm);
            exec_status = masm32_sim_wasm_validate_section_accesses(&g_masm32_sim_wasm_run_storage, vm, capacity_policy, image_policy, runtime_policy);
            if (exec_status == VM_EXEC_STATUS_OK) {
                exec_status = masm32_sim_wasm_validate_object_accesses(&g_masm32_sim_wasm_run_storage, vm, object_validation_mode, runtime_policy);
            }
            if (exec_status == VM_EXEC_STATUS_OK) {
                masm32_sim_wasm_mark_initialized_writes(&g_masm32_sim_wasm_run_storage, vm, runtime_policy);
                masm32_sim_wasm_collect_memory_change(&g_masm32_sim_wasm_run_storage, vm, &parser_result);
            }
        }
    }

    masm32_sim_wasm_update_instruction_accounting(&g_masm32_sim_wasm_run_storage, vm);
    if (exec_status == VM_EXEC_STATUS_PROCEDURE_FELL_THROUGH) {
        masm32_sim_wasm_collect_procedure_fallthrough_violation(&g_masm32_sim_wasm_run_storage, vm, &parser_result);
    }
    if (exec_status == VM_EXEC_STATUS_CALL_DEPTH_EXCEEDED) {
        masm32_sim_wasm_collect_call_depth_violation(&g_masm32_sim_wasm_run_storage, vm, &parser_result);
    }

    if (exec_status == VM_EXEC_STATUS_BRANCH_RUNTIME_DEFERRED &&
        vm->program != NULL &&
        vm->instruction_pointer < vm->program_count) {
        g_masm32_sim_wasm_run_storage.attempted_next_instruction_index = vm->program[vm->instruction_pointer].instruction_index;
        g_masm32_sim_wasm_run_storage.has_attempted_next_instruction_index = true;
        g_masm32_sim_wasm_run_storage.executed_instruction_count = vm->instruction_count;
    }

    if (exec_status == VM_EXEC_STATUS_HALTED) {
        exec_status = VM_EXEC_STATUS_OK;
    }

    if (exec_status != VM_EXEC_STATUS_OK) {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_EXEC_ERROR, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, exec_status, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        if (vm_initialized) {
            vm_deinit(vm);
        }
        return json;
    }

    {
        const char *json = masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_OK, vm, &parser_result, g_masm32_sim_wasm_run_storage.parser_diagnostics, VM_EXEC_STATUS_OK, &g_masm32_sim_wasm_run_storage, json_layout_policy, NULL, include_uninitialized_metadata, startup_state_notice_setting == MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON, startup_register_flag_mode, uninitialized_storage_visible_byte_mode);
        if (vm_initialized) {
            vm_deinit(vm);
        }
        return json;
    }
}


/// Runs source with the default Phase 71D procedure-fallthrough warning policy.
///
/// This compatibility wrapper preserves existing internal call sites while the
/// Phase 71D-specific export can pass an explicit procedure-fallthrough policy.
///
/// @param source Null-terminated source text.
/// @param layout_mode Layout mode selected by the caller.
/// @param base_policy Optional base layout policy.
/// @param object_validation_mode Allocated-object validation mode.
/// @param uninitialized_validation_mode Uninitialized-read validation mode.
/// @param capacity_policy Section-capacity validation policy.
/// @param image_policy Section-image validation policy.
/// @param shift_mode Undefined shift-flag validation mode.
/// @param flag_use_policy Undefined flag-use validation policy.
/// @param compatibility_notice_setting Compatibility notice setting.
/// @param const_uninitialized_storage_policy CONST uninitialized policy.
/// @param startup_state_notice_setting Startup-state notice setting.
/// @param startup_register_flag_mode Register/flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Uninitialized visible-byte mode.
/// @param startup_state_seed Deterministic startup seed.
/// @param instruction_limit Positive maximum instruction count.
/// @param root_ret_mode Root-code-stream RET mode.
/// @param include_uninitialized_metadata Whether to include test-only metadata.
/// @return Pointer to a static JSON response buffer.
static const char *masm32_sim_wasm_run_source_json_internal(
    const char *source,
    VmLayoutMode layout_mode,
    const VmLayoutPolicy *base_policy,
    Masm32SimWasmMemoryValidationMode object_validation_mode,
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy,
    Masm32SimWasmShiftValidationMode shift_mode,
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    VmDiagnosticPolicyValue const_uninitialized_storage_policy,
    Masm32SimWasmStartupStateNoticeSetting startup_state_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    bool include_uninitialized_metadata
) {
    return masm32_sim_wasm_run_source_json_internal_with_procedure_fallthrough_policy(
        source,
        layout_mode,
        base_policy,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        shift_mode,
        flag_use_policy,
        compatibility_notice_setting,
        const_uninitialized_storage_policy,
        startup_state_notice_setting,
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        instruction_limit,
        MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT,
        root_ret_mode,
        masm32_sim_wasm_default_procedure_fallthrough_policy(),
        masm32_sim_wasm_default_entry_procedure_end_mode(),
        include_uninitialized_metadata
    );
}


/// Returns the registry default value for one implemented diagnostic family.
///
/// @param family Diagnostic-policy family to inspect.
/// @param fallback Fallback used only if registry metadata is internally inconsistent.
/// @return Current registry default or @p fallback when unavailable.
static VmDiagnosticPolicyValue masm32_sim_wasm_registry_default_policy_value(
    VmDiagnosticPolicyFamily family,
    VmDiagnosticPolicyValue fallback
) {
    VmDiagnosticPolicyValue value = fallback;

    if (vm_diagnostic_policy_family_default_value(family, &value)) {
        return value;
    }

    return fallback;
}

/// Maps a registry value for uninitialized-read diagnostics to the legacy backend mode.
///
/// @param value Registry policy value for the uninitialized-read family.
/// @param out_mode Receives the legacy memory-validation mode.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_uninitialized_read_policy_value(
    VmDiagnosticPolicyValue value,
    Masm32SimWasmMemoryValidationMode *out_mode
) {
    if (out_mode == NULL ||
        !vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, value)) {
        return false;
    }

    switch (value) {
        case VM_DIAGNOSTIC_POLICY_VALUE_OFF:
            *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_WARN:
            *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_ERROR:
            *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT;
            return true;
        default:
            return false;
    }
}

/// Maps a registry value for undefined-flag-use diagnostics to the legacy backend policy.
///
/// @param value Registry policy value for the undefined-flag-use family.
/// @param out_policy Receives the legacy undefined-flag-use policy.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_undefined_flag_use_policy_value(
    VmDiagnosticPolicyValue value,
    Masm32SimWasmUndefinedFlagUsePolicy *out_policy
) {
    if (out_policy == NULL ||
        !vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE, value)) {
        return false;
    }

    switch (value) {
        case VM_DIAGNOSTIC_POLICY_VALUE_OFF:
            *out_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_WARN:
            *out_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_ERROR:
            *out_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR;
            return true;
        default:
            return false;
    }
}

/// Maps a registry value for compatibility notices to the legacy backend setting.
///
/// Compatibility notices render as simulator notices, not warnings. Registry
/// `warn` means this non-fatal notice family is emitted and execution continues.
///
/// @param value Registry policy value for the compatibility-notice family.
/// @param out_setting Receives the legacy compatibility-notice setting.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_compatibility_notice_policy_value(
    VmDiagnosticPolicyValue value,
    Masm32SimWasmCompatibilityNoticeSetting *out_setting
) {
    if (out_setting == NULL ||
        !vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE, value)) {
        return false;
    }

    switch (value) {
        case VM_DIAGNOSTIC_POLICY_VALUE_OFF:
            *out_setting = MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_WARN:
            *out_setting = MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_ERROR:
            return false;
        default:
            return false;
    }
}

/// Maps a registry value for startup-state notices to the legacy backend setting.
///
/// Startup-state notices render as simulator notices, not warnings. Registry
/// `warn` means this non-fatal notice family is emitted and execution continues.
///
/// @param value Registry policy value for the startup-state-notice family.
/// @param out_setting Receives the startup-state notice setting.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_startup_state_notice_policy_value(
    VmDiagnosticPolicyValue value,
    Masm32SimWasmStartupStateNoticeSetting *out_setting
) {
    if (out_setting == NULL ||
        !vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE, value)) {
        return false;
    }

    switch (value) {
        case VM_DIAGNOSTIC_POLICY_VALUE_OFF:
            *out_setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_WARN:
            *out_setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON;
            return true;
        case VM_DIAGNOSTIC_POLICY_VALUE_ERROR:
            return false;
        default:
            return false;
    }
}

/// Maps one Phase 53E teaching-diagnostic setting to the central registry vocabulary.
///
/// Public Phase 53E settings keep the legacy name `strict`; internally that
/// setting maps to registry `error` for the selected diagnostic family.
///
/// @param family Implemented diagnostic-policy family being configured.
/// @param setting Legacy Phase 53E teaching diagnostic setting.
/// @param out_value Receives the central registry policy value.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_teaching_setting_to_policy_value(
    VmDiagnosticPolicyFamily family,
    Masm32SimWasmTeachingDiagnosticSetting setting,
    VmDiagnosticPolicyValue *out_value
) {
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

    if (out_value == NULL) {
        return false;
    }

    switch (setting) {
        case MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_OFF:
            value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
            break;
        case MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN:
            value = VM_DIAGNOSTIC_POLICY_VALUE_WARN;
            break;
        case MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_STRICT:
            value = VM_DIAGNOSTIC_POLICY_VALUE_ERROR;
            break;
        default:
            return false;
    }

    if (!vm_diagnostic_policy_family_accepts_value(family, value)) {
        return false;
    }

    *out_value = value;
    return true;
}

/// Maps one Phase 53E compatibility-notice setting to the central registry vocabulary.
///
/// Public Phase 53E settings keep the legacy name `on`; internally that setting
/// maps to registry `warn` because the notice is non-fatal.
///
/// @param setting Legacy Phase 53E compatibility-notice setting.
/// @param out_value Receives the central registry policy value.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_compatibility_setting_to_policy_value(
    Masm32SimWasmCompatibilityNoticeSetting setting,
    VmDiagnosticPolicyValue *out_value
) {
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

    if (out_value == NULL) {
        return false;
    }

    switch (setting) {
        case MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF:
            value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
            break;
        case MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON:
            value = VM_DIAGNOSTIC_POLICY_VALUE_WARN;
            break;
        default:
            return false;
    }

    if (!vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE, value)) {
        return false;
    }

    *out_value = value;
    return true;
}

/// Returns the default uninitialized-read backend mode from the registry.
///
/// @return Legacy backend mode corresponding to the current registry default.
static Masm32SimWasmMemoryValidationMode masm32_sim_wasm_default_uninitialized_read_mode(void) {
    Masm32SimWasmMemoryValidationMode mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    VmDiagnosticPolicyValue value = masm32_sim_wasm_registry_default_policy_value(
        VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN
    );

    if (masm32_sim_wasm_map_uninitialized_read_policy_value(value, &mode)) {
        return mode;
    }

    return MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
}

/// Returns the default undefined-flag-use backend policy from the registry.
///
/// @return Legacy backend policy corresponding to the current registry default.
static Masm32SimWasmUndefinedFlagUsePolicy masm32_sim_wasm_default_undefined_flag_use_policy(void) {
    Masm32SimWasmUndefinedFlagUsePolicy policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;
    VmDiagnosticPolicyValue value = masm32_sim_wasm_registry_default_policy_value(
        VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN
    );

    if (masm32_sim_wasm_map_undefined_flag_use_policy_value(value, &policy)) {
        return policy;
    }

    return MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;
}

/// Returns the default compatibility-notice backend setting from the registry.
///
/// @return Legacy backend setting corresponding to the current registry default.
static Masm32SimWasmCompatibilityNoticeSetting masm32_sim_wasm_default_compatibility_notice_setting(void) {
    Masm32SimWasmCompatibilityNoticeSetting setting = MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON;
    VmDiagnosticPolicyValue value = masm32_sim_wasm_registry_default_policy_value(
        VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN
    );

    if (masm32_sim_wasm_map_compatibility_notice_policy_value(value, &setting)) {
        return setting;
    }

    return MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON;
}

/// Returns the default startup-state notice setting from the registry.
///
/// @return Backend setting corresponding to the current registry default.
static Masm32SimWasmStartupStateNoticeSetting masm32_sim_wasm_default_startup_state_notice_setting(void) {
    Masm32SimWasmStartupStateNoticeSetting setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON;
    VmDiagnosticPolicyValue value = masm32_sim_wasm_registry_default_policy_value(
        VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN
    );

    if (masm32_sim_wasm_map_startup_state_notice_policy_value(value, &setting)) {
        return setting;
    }

    return MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON;
}


/// Returns the default `.CONST` uninitialized-storage declaration policy from the registry.
///
/// @return Registry default for the Phase 57J const-uninitialized-storage family.
static VmDiagnosticPolicyValue masm32_sim_wasm_default_const_uninitialized_storage_policy(void) {
    return masm32_sim_wasm_registry_default_policy_value(
        VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN
    );
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json(const char *source) {
    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, masm32_sim_wasm_default_uninitialized_read_mode(), MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, masm32_sim_wasm_default_undefined_flag_use_policy(), masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), masm32_sim_wasm_default_startup_state_notice_setting(), MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, false);
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_instruction_limit(
    const char *source,
    uint32_t instruction_limit
) {
    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, masm32_sim_wasm_default_uninitialized_read_mode(), MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, masm32_sim_wasm_default_undefined_flag_use_policy(), masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), masm32_sim_wasm_default_startup_state_notice_setting(), MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, instruction_limit,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, false);
}

const char *masm32_sim_wasm_run_source_json_with_automatic_layout_policy(const char *source, const VmLayoutPolicy *base_policy) {
    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_AUTOMATIC, base_policy, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, masm32_sim_wasm_default_uninitialized_read_mode(), MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, masm32_sim_wasm_default_undefined_flag_use_policy(), masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, false);
}

const char *masm32_sim_wasm_run_source_json_with_randomized_layout_policy(const char *source, VmLayoutMode randomized_mode, const VmLayoutPolicy *base_policy) {
    return masm32_sim_wasm_run_source_json_internal(source, randomized_mode, base_policy, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, masm32_sim_wasm_default_uninitialized_read_mode(), MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, masm32_sim_wasm_default_undefined_flag_use_policy(), masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, false);
}

/// Returns whether a Phase 53E memory range setting enum value is accepted.
///
/// @param setting Browser memory range setting to inspect.
/// @return true when @p setting is valid.
static bool masm32_sim_wasm_memory_range_setting_is_valid(Masm32SimWasmMemoryRangeSetting setting) {
    return setting == MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY ||
        setting == MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_WARN ||
        setting == MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_STRICT ||
        setting == MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_WARN ||
        setting == MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_STRICT ||
        setting == MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_WARN ||
        setting == MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_STRICT;
}

/// Maps a Phase 53E memory range setting to existing backend policies.
///
/// @param setting Browser memory range setting to map.
/// @param out_object_mode Receives the Level 4 declared-object policy.
/// @param out_capacity_policy Receives the Level 2 section-capacity policy.
/// @param out_image_policy Receives the Level 3 section-image policy.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_memory_range_setting(
    Masm32SimWasmMemoryRangeSetting setting,
    Masm32SimWasmMemoryValidationMode *out_object_mode,
    Masm32SimWasmSectionValidationPolicy *out_capacity_policy,
    Masm32SimWasmSectionValidationPolicy *out_image_policy
) {
    if (out_object_mode == NULL || out_capacity_policy == NULL || out_image_policy == NULL ||
        !masm32_sim_wasm_memory_range_setting_is_valid(setting)) {
        return false;
    }

    *out_object_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    *out_capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    *out_image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;

    switch (setting) {
        case MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY:
            return true;
        case MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_WARN:
            *out_capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_WARN;
            return true;
        case MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_STRICT:
            *out_capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_STRICT;
            return true;
        case MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_WARN:
            *out_image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_WARN;
            return true;
        case MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_STRICT:
            *out_image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_STRICT;
            return true;
        case MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_WARN:
            *out_object_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS;
            return true;
        case MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_STRICT:
            *out_object_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT;
            return true;
        default:
            return false;
    }
}

/// Maps a Phase 53E uninitialized-read setting to existing backend policy.
///
/// @param setting Browser teaching diagnostic setting to map.
/// @param out_mode Receives the uninitialized-read validation policy.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_uninitialized_read_setting(
    Masm32SimWasmTeachingDiagnosticSetting setting,
    Masm32SimWasmMemoryValidationMode *out_mode
) {
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

    if (!masm32_sim_wasm_map_teaching_setting_to_policy_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, setting, &value)) {
        return false;
    }

    return masm32_sim_wasm_map_uninitialized_read_policy_value(value, out_mode);
}

/// Maps a Phase 53E undefined-flag-use setting to existing backend policy.
///
/// @param setting Browser teaching diagnostic setting to map.
/// @param out_policy Receives the undefined-flag-use consumer policy.
/// @return true when mapping succeeded.
static bool masm32_sim_wasm_map_undefined_flag_use_setting(
    Masm32SimWasmTeachingDiagnosticSetting setting,
    Masm32SimWasmUndefinedFlagUsePolicy *out_policy
) {
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

    if (!masm32_sim_wasm_map_teaching_setting_to_policy_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE, setting, &value)) {
        return false;
    }

    return masm32_sim_wasm_map_undefined_flag_use_policy_value(value, out_policy);
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting
) {
    return masm32_sim_wasm_run_source_json_with_ui_and_startup_settings(
        source,
        memory_range_setting,
        uninitialized_read_setting,
        undefined_flag_use_setting,
        compatibility_notice_setting,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        0U
    );
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_and_startup_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    uint32_t startup_state_seed
) {
    Masm32SimWasmMemoryValidationMode object_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;

    {
        VmDiagnosticPolicyValue compatibility_notice_value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

        if (!masm32_sim_wasm_map_memory_range_setting(memory_range_setting, &object_validation_mode, &capacity_policy, &image_policy) ||
            !masm32_sim_wasm_map_uninitialized_read_setting(uninitialized_read_setting, &uninitialized_validation_mode) ||
            !masm32_sim_wasm_map_undefined_flag_use_setting(undefined_flag_use_setting, &flag_use_policy) ||
            !masm32_sim_wasm_map_compatibility_setting_to_policy_value(compatibility_notice_setting, &compatibility_notice_value) ||
            !masm32_sim_wasm_map_compatibility_notice_policy_value(compatibility_notice_value, &compatibility_notice_setting)) {
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                false,
                false,
                MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
                MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
            );
        }
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        flag_use_policy,
        compatibility_notice_setting,
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        masm32_sim_wasm_default_startup_state_notice_setting(),
        startup_register_flag_mode,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        startup_state_seed,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
}


MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed
) {
    Masm32SimWasmMemoryValidationMode object_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;

    {
        VmDiagnosticPolicyValue compatibility_notice_value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

        if (!masm32_sim_wasm_map_memory_range_setting(memory_range_setting, &object_validation_mode, &capacity_policy, &image_policy) ||
            !masm32_sim_wasm_map_uninitialized_read_setting(uninitialized_read_setting, &uninitialized_validation_mode) ||
            !masm32_sim_wasm_map_undefined_flag_use_setting(undefined_flag_use_setting, &flag_use_policy) ||
            !masm32_sim_wasm_map_compatibility_setting_to_policy_value(compatibility_notice_setting, &compatibility_notice_value) ||
            !masm32_sim_wasm_map_compatibility_notice_policy_value(compatibility_notice_value, &compatibility_notice_setting)) {
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                false,
                false,
                MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
                MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
            );
        }
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        flag_use_policy,
        compatibility_notice_setting,
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        masm32_sim_wasm_default_startup_state_notice_setting(),
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_and_instruction_limit_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit
) {
    Masm32SimWasmMemoryValidationMode object_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;

    {
        VmDiagnosticPolicyValue compatibility_notice_value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

        if (!masm32_sim_wasm_map_memory_range_setting(memory_range_setting, &object_validation_mode, &capacity_policy, &image_policy) ||
            !masm32_sim_wasm_map_uninitialized_read_setting(uninitialized_read_setting, &uninitialized_validation_mode) ||
            !masm32_sim_wasm_map_undefined_flag_use_setting(undefined_flag_use_setting, &flag_use_policy) ||
            !masm32_sim_wasm_map_compatibility_setting_to_policy_value(compatibility_notice_setting, &compatibility_notice_value) ||
            !masm32_sim_wasm_map_compatibility_notice_policy_value(compatibility_notice_value, &compatibility_notice_setting)) {
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                false,
                false,
                MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
                MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
            );
        }
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        flag_use_policy,
        compatibility_notice_setting,
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        masm32_sim_wasm_default_startup_state_notice_setting(),
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        instruction_limit,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
}

MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode
) {
    Masm32SimWasmMemoryValidationMode object_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;

    {
        VmDiagnosticPolicyValue compatibility_notice_value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

        if (!masm32_sim_wasm_map_memory_range_setting(memory_range_setting, &object_validation_mode, &capacity_policy, &image_policy) ||
            !masm32_sim_wasm_map_uninitialized_read_setting(uninitialized_read_setting, &uninitialized_validation_mode) ||
            !masm32_sim_wasm_map_undefined_flag_use_setting(undefined_flag_use_setting, &flag_use_policy) ||
            !masm32_sim_wasm_map_compatibility_setting_to_policy_value(compatibility_notice_setting, &compatibility_notice_value) ||
            !masm32_sim_wasm_map_compatibility_notice_policy_value(compatibility_notice_value, &compatibility_notice_setting)) {
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                false,
                false,
                MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
                MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
            );
        }
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        flag_use_policy,
        compatibility_notice_setting,
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        masm32_sim_wasm_default_startup_state_notice_setting(),
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        instruction_limit,
        root_ret_mode,
        false
    );
}

/// Browser-facing Phase 71D source-run export with procedure-fallthrough policy.
///
/// @param source Null-terminated source text to parse and execute.
/// @param memory_range_setting Browser memory validation setting.
/// @param uninitialized_read_setting Browser uninitialized-read setting.
/// @param undefined_flag_use_setting Browser undefined-flag-use setting.
/// @param compatibility_notice_setting Browser compatibility-notice setting.
/// @param startup_register_flag_mode Register and flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Visible-byte startup mode for uninitialized storage.
/// @param startup_state_seed Deterministic startup seed.
/// @param instruction_limit Positive instruction-count limit.
/// @param root_ret_mode Root-code-stream RET mode.
/// @param procedure_fallthrough_policy Ordinary procedure-boundary fallthrough policy.
/// @return Pointer to static JSON result storage.
MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy
) {
    Masm32SimWasmMemoryValidationMode object_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;

    {
        VmDiagnosticPolicyValue compatibility_notice_value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

        if (!masm32_sim_wasm_map_memory_range_setting(memory_range_setting, &object_validation_mode, &capacity_policy, &image_policy) ||
            !masm32_sim_wasm_map_uninitialized_read_setting(uninitialized_read_setting, &uninitialized_validation_mode) ||
            !masm32_sim_wasm_map_undefined_flag_use_setting(undefined_flag_use_setting, &flag_use_policy) ||
            !masm32_sim_wasm_map_compatibility_setting_to_policy_value(compatibility_notice_setting, &compatibility_notice_value) ||
            !masm32_sim_wasm_map_compatibility_notice_policy_value(compatibility_notice_value, &compatibility_notice_setting)) {
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                false,
                false,
                MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
                MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
            );
        }
    }

    return masm32_sim_wasm_run_source_json_internal_with_procedure_fallthrough_policy(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        flag_use_policy,
        compatibility_notice_setting,
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        masm32_sim_wasm_default_startup_state_notice_setting(),
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        instruction_limit,
        MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT,
        root_ret_mode,
        procedure_fallthrough_policy,
        masm32_sim_wasm_default_entry_procedure_end_mode(),
        false
    );
}

/// Browser-facing Phase 71E source-run export with entry procedure end mode.
///
/// @param source Null-terminated source text to parse and execute.
/// @param memory_range_setting Browser memory validation setting.
/// @param uninitialized_read_setting Browser uninitialized-read setting.
/// @param undefined_flag_use_setting Browser undefined-flag-use setting.
/// @param compatibility_notice_setting Browser compatibility-notice setting.
/// @param startup_register_flag_mode Register and flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Visible-byte startup mode for uninitialized storage.
/// @param startup_state_seed Deterministic startup seed.
/// @param instruction_limit Positive instruction-count limit.
/// @param root_ret_mode Root-code-stream RET mode.
/// @param procedure_fallthrough_policy Ordinary procedure-boundary fallthrough policy.
/// @param entry_procedure_end_mode Selected-entry ENDP boundary compatibility mode.
/// @return Pointer to static JSON result storage.
MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy,
    Masm32SimWasmEntryProcedureEndMode entry_procedure_end_mode
) {
    Masm32SimWasmMemoryValidationMode object_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;

    {
        VmDiagnosticPolicyValue compatibility_notice_value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

        if (!masm32_sim_wasm_map_memory_range_setting(memory_range_setting, &object_validation_mode, &capacity_policy, &image_policy) ||
            !masm32_sim_wasm_map_uninitialized_read_setting(uninitialized_read_setting, &uninitialized_validation_mode) ||
            !masm32_sim_wasm_map_undefined_flag_use_setting(undefined_flag_use_setting, &flag_use_policy) ||
            !masm32_sim_wasm_map_compatibility_setting_to_policy_value(compatibility_notice_setting, &compatibility_notice_value) ||
            !masm32_sim_wasm_map_compatibility_notice_policy_value(compatibility_notice_value, &compatibility_notice_setting)) {
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                false,
                false,
                MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
                MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
            );
        }
    }

    return masm32_sim_wasm_run_source_json_internal_with_procedure_fallthrough_policy(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        flag_use_policy,
        compatibility_notice_setting,
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        masm32_sim_wasm_default_startup_state_notice_setting(),
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        instruction_limit,
        MASM32_SIM_WASM_DEFAULT_CALL_DEPTH_LIMIT,
        root_ret_mode,
        procedure_fallthrough_policy,
        entry_procedure_end_mode,
        false
    );
}
/// Browser-facing Phase 72 source-run export with entry procedure end mode and call-depth limit.
///
/// @param source Null-terminated source text to parse and execute.
/// @param memory_range_setting Browser memory validation setting.
/// @param uninitialized_read_setting Browser uninitialized-read setting.
/// @param undefined_flag_use_setting Browser undefined-flag-use setting.
/// @param compatibility_notice_setting Browser compatibility-notice setting.
/// @param startup_register_flag_mode Register and flag startup mode.
/// @param uninitialized_storage_visible_byte_mode Visible-byte startup mode for uninitialized storage.
/// @param startup_state_seed Deterministic startup seed.
/// @param instruction_limit Positive instruction-count limit.
/// @param root_ret_mode Root-code-stream RET mode.
/// @param procedure_fallthrough_policy Ordinary procedure-boundary fallthrough policy.
/// @param entry_procedure_end_mode Selected-entry ENDP boundary compatibility mode.
/// @param call_depth_limit Phase 72 direct user-procedure CALL depth limit.
/// @return Pointer to static JSON result storage.
MASM32_SIM_EXPORT const char *masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings(
    const char *source,
    Masm32SimWasmMemoryRangeSetting memory_range_setting,
    Masm32SimWasmTeachingDiagnosticSetting uninitialized_read_setting,
    Masm32SimWasmTeachingDiagnosticSetting undefined_flag_use_setting,
    Masm32SimWasmCompatibilityNoticeSetting compatibility_notice_setting,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    uint32_t instruction_limit,
    Masm32SimWasmRootRetMode root_ret_mode,
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy,
    Masm32SimWasmEntryProcedureEndMode entry_procedure_end_mode,
    uint32_t call_depth_limit
) {
    Masm32SimWasmMemoryValidationMode object_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmMemoryValidationMode uninitialized_validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;

    {
        VmDiagnosticPolicyValue compatibility_notice_value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

        if (!masm32_sim_wasm_map_memory_range_setting(memory_range_setting, &object_validation_mode, &capacity_policy, &image_policy) ||
            !masm32_sim_wasm_map_uninitialized_read_setting(uninitialized_read_setting, &uninitialized_validation_mode) ||
            !masm32_sim_wasm_map_undefined_flag_use_setting(undefined_flag_use_setting, &flag_use_policy) ||
            !masm32_sim_wasm_map_compatibility_setting_to_policy_value(compatibility_notice_setting, &compatibility_notice_value) ||
            !masm32_sim_wasm_map_compatibility_notice_policy_value(compatibility_notice_value, &compatibility_notice_setting)) {
            return masm32_sim_wasm_build_run_json(
                MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                VM_EXEC_STATUS_INVALID_ARGUMENT,
                NULL,
                NULL,
                NULL,
                false,
                false,
                MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
                MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
            );
        }
    }

    return masm32_sim_wasm_run_source_json_internal_with_procedure_fallthrough_policy(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        object_validation_mode,
        uninitialized_validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        flag_use_policy,
        compatibility_notice_setting,
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        masm32_sim_wasm_default_startup_state_notice_setting(),
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        instruction_limit,
        call_depth_limit,
        root_ret_mode,
        procedure_fallthrough_policy,
        entry_procedure_end_mode,
        false
    );
}

const char *masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(
    const char *source,
    Masm32SimWasmStartupStateNoticeSetting setting
) {
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

    switch (setting) {
        case MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF:
            value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
            break;
        case MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON:
            value = VM_DIAGNOSTIC_POLICY_VALUE_WARN;
            break;
        default:
            return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    if (!vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE, value)) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        masm32_sim_wasm_default_uninitialized_read_mode(),
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        masm32_sim_wasm_default_undefined_flag_use_policy(),
        masm32_sim_wasm_default_compatibility_notice_setting(),
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        setting,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
}

const char *masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
    const char *source,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    uint32_t startup_state_seed,
    Masm32SimWasmStartupStateNoticeSetting startup_state_notice_setting
) {
    return masm32_sim_wasm_run_source_json_with_startup_modes(
        source,
        startup_register_flag_mode,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        startup_state_seed,
        startup_state_notice_setting
    );
}

const char *masm32_sim_wasm_run_source_json_with_startup_modes(
    const char *source,
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode,
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode,
    uint32_t startup_state_seed,
    Masm32SimWasmStartupStateNoticeSetting startup_state_notice_setting
) {
    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        masm32_sim_wasm_default_uninitialized_read_mode(),
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        masm32_sim_wasm_default_undefined_flag_use_policy(),
        masm32_sim_wasm_default_compatibility_notice_setting(),
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        startup_state_notice_setting,
        startup_register_flag_mode,
        uninitialized_storage_visible_byte_mode,
        startup_state_seed,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
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
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, validation_mode, validation_mode, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, masm32_sim_wasm_default_undefined_flag_use_policy(), masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, false);
}

const char *masm32_sim_wasm_run_source_json_with_shift_validation_mode(
    const char *source,
    Masm32SimWasmShiftValidationMode shift_mode
) {
    if (shift_mode != MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS &&
        shift_mode != MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, masm32_sim_wasm_default_uninitialized_read_mode(), MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, shift_mode, masm32_sim_wasm_default_undefined_flag_use_policy(), masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, false);
}

const char *masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
    const char *source,
    Masm32SimWasmUndefinedFlagUsePolicy policy
) {
    if (policy != MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF &&
        policy != MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN &&
        policy != MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        masm32_sim_wasm_default_uninitialized_read_mode(),
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        policy,
        masm32_sim_wasm_default_compatibility_notice_setting(),
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
}

/// Returns whether a memory-validation mode enum value is accepted.
///
/// @param validation_mode Existing memory-validation mode to inspect.
/// @return true when the mode is valid.
static bool masm32_sim_wasm_memory_validation_mode_is_valid(Masm32SimWasmMemoryValidationMode validation_mode) {
    return validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY ||
        validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS ||
        validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT ||
        validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS ||
        validation_mode == MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT;
}

/// Returns whether a section-validation policy enum value is accepted.
///
/// @param policy Section-validation policy to inspect.
/// @return true when the policy is valid.
static bool masm32_sim_wasm_section_validation_policy_is_valid(Masm32SimWasmSectionValidationPolicy policy) {
    return policy == MASM32_SIM_WASM_SECTION_VALIDATION_OFF ||
        policy == MASM32_SIM_WASM_SECTION_VALIDATION_WARN ||
        policy == MASM32_SIM_WASM_SECTION_VALIDATION_STRICT;
}

const char *masm32_sim_wasm_run_source_json_with_section_validation_modes(
    const char *source,
    Masm32SimWasmMemoryValidationMode validation_mode,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy
) {
    if (!masm32_sim_wasm_memory_validation_mode_is_valid(validation_mode) ||
        !masm32_sim_wasm_section_validation_policy_is_valid(capacity_policy) ||
        !masm32_sim_wasm_section_validation_policy_is_valid(image_policy)) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        validation_mode,
        validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        masm32_sim_wasm_default_undefined_flag_use_policy(),
        masm32_sim_wasm_default_compatibility_notice_setting(),
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
}

const char *masm32_sim_wasm_run_source_json_with_automatic_layout_and_section_validation(
    const char *source,
    const VmLayoutPolicy *base_policy,
    Masm32SimWasmMemoryValidationMode validation_mode,
    Masm32SimWasmSectionValidationPolicy capacity_policy,
    Masm32SimWasmSectionValidationPolicy image_policy
) {
    if (!masm32_sim_wasm_memory_validation_mode_is_valid(validation_mode) ||
        !masm32_sim_wasm_section_validation_policy_is_valid(capacity_policy) ||
        !masm32_sim_wasm_section_validation_policy_is_valid(image_policy)) {
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, false, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_AUTOMATIC,
        base_policy,
        validation_mode,
        validation_mode,
        capacity_policy,
        image_policy,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        masm32_sim_wasm_default_undefined_flag_use_policy(),
        masm32_sim_wasm_default_compatibility_notice_setting(),
        masm32_sim_wasm_default_const_uninitialized_storage_policy(),
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        false
    );
}


const char *masm32_sim_wasm_run_source_json_with_const_uninitialized_storage_policy(
    const char *source,
    VmDiagnosticPolicyValue policy
) {
    if (!vm_diagnostic_policy_family_accepts_value(
            VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE,
            policy)) {
        return masm32_sim_wasm_build_run_json(
            MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT,
            NULL,
            NULL,
            NULL,
            VM_EXEC_STATUS_INVALID_ARGUMENT,
            NULL,
            NULL,
            NULL,
            false,
            false,
            MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
            MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO
        );
    }

    return masm32_sim_wasm_run_source_json_internal(
        source,
        VM_LAYOUT_MODE_FIXED,
        NULL,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        masm32_sim_wasm_default_uninitialized_read_mode(),
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS,
        masm32_sim_wasm_default_undefined_flag_use_policy(),
        masm32_sim_wasm_default_compatibility_notice_setting(),
        policy,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
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
        return masm32_sim_wasm_build_run_json(MASM32_SIM_WASM_RUN_OUTCOME_INVALID_ARGUMENT, NULL, NULL, NULL, VM_EXEC_STATUS_INVALID_ARGUMENT, NULL, NULL, NULL, true, false, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO);
    }

    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, validation_mode, validation_mode, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, masm32_sim_wasm_default_undefined_flag_use_policy(), masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, true);
}

const char *masm32_sim_wasm_run_source_json_with_uninitialized_metadata(const char *source) {
    return masm32_sim_wasm_run_source_json_internal(source, VM_LAYOUT_MODE_FIXED, NULL, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SECTION_VALIDATION_OFF, MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS, MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF, masm32_sim_wasm_default_compatibility_notice_setting(), masm32_sim_wasm_default_const_uninitialized_storage_policy(), MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF, MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO, MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO, 0U, MASM32_SIM_WASM_DEFAULT_INSTRUCTION_LIMIT,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE, true);
}

MASM32_SIM_EXPORT int masm32_sim_wasm_copy_version(char *out_buffer, unsigned long out_buffer_size) {
    return (int)masm_sim_copy_version(out_buffer, (size_t)out_buffer_size);
}
