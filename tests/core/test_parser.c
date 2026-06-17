/*
 * @file test_parser.c
 * @brief Unit and integration tests for parser behavior through Phase 78 LOCAL parser metadata coverage.
 *
 * These tests verify parsing of tiny .code programs into the existing IR,
 * Phase 58 code-label metadata and diagnostics, Phase 60 direct JMP
 * parsing and target classification, Phase 63 CMP memory operand parsing, Phase 64 equality conditional jump parsing,
 * Phase 67A procedure-range metadata, Phase 68 call-target classification
 * metadata, Phase 68B EIP source-operand restrictions, Phase 69 direct CALL,
 * Phase 70 plain near RET, Phase 72A source-level PUSH/POP, Phase 73
 * LEAVE syntax, Phase 74 RET imm16, Phase 75 PROC diagnostics, Phase 76 PROC USES metadata, Phase 78 LOCAL parser metadata, unsupported syntax, INCLUDELIB non-goal diagnostics,
 * INVOKE/ADDR external-routine diagnostics, and integration with the current executor
 * without adding future execution behavior.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_exec.h"
#include "../../src/parser/parser.h"

/// Number of lexer tokens available to each parser test.
#define TEST_TOKEN_CAPACITY 192U

/// Number of lexer diagnostics available to each parser test.
#define TEST_LEXER_DIAGNOSTIC_CAPACITY 16U

/// Number of parser diagnostics available to each parser test.
#define TEST_PARSER_DIAGNOSTIC_CAPACITY 16U

/// Number of IR instructions available to each parser test.
#define TEST_INSTRUCTION_CAPACITY 32U

/// Number of source-text storage bytes available to each parser test.
#define TEST_SOURCE_TEXT_CAPACITY 512U

/// Number of data symbols available to parser tests that include .data.
#define TEST_SYMBOL_CAPACITY 16U

/// Number of code labels available to parser tests.
#define TEST_CODE_LABEL_CAPACITY 16U

/// Number of procedure ranges available to parser tests.
#define TEST_PROCEDURE_RANGE_CAPACITY 16U

/// Number of .data/.DATA? image bytes available to parser tests.
#define TEST_DATA_IMAGE_CAPACITY 512U

/// Number of .CONST image bytes available to parser tests.
#define TEST_CONST_IMAGE_CAPACITY 512U

/// Holds all caller-owned parser buffers for one test.
typedef struct ParserTestBuffers {
    /// Lexer token buffer.
    VmLexerToken tokens[TEST_TOKEN_CAPACITY];
    /// Lexer diagnostic buffer.
    VmLexerDiagnostic lexer_diagnostics[TEST_LEXER_DIAGNOSTIC_CAPACITY];
    /// Parser diagnostic buffer.
    VmParserDiagnostic diagnostics[TEST_PARSER_DIAGNOSTIC_CAPACITY];
    /// Emitted IR instruction buffer.
    VmIrInstruction instructions[TEST_INSTRUCTION_CAPACITY];
    /// Null-terminated source-text copies used by emitted IR.
    char source_text[TEST_SOURCE_TEXT_CAPACITY];
    /// Data symbols emitted from optional .data declarations.
    VmSymbol symbols[TEST_SYMBOL_CAPACITY];
    /// Numeric equates emitted as metadata for classifier tests.
    VmNumericEquate numeric_equates[TEST_SYMBOL_CAPACITY];
    /// Code labels emitted from .code labels and PROC entries.
    VmCodeLabel code_labels[TEST_CODE_LABEL_CAPACITY];
    /// Procedure ranges emitted from PROC/ENDP boundaries.
    VmProcedureRange procedure_ranges[TEST_PROCEDURE_RANGE_CAPACITY];
    /// Data image emitted from optional .data/.DATA? declarations.
    uint8_t data_image[TEST_DATA_IMAGE_CAPACITY];
    /// Constant image emitted from optional .CONST declarations.
    uint8_t const_image[TEST_CONST_IMAGE_CAPACITY];
    /// Per-byte initialized-state mask for optional .CONST declarations.
    uint8_t const_initialized_mask[TEST_CONST_IMAGE_CAPACITY];
} ParserTestBuffers;

/// Records a parser test failure.
///
/// @param message Human-readable failure description.
/// @return Always returns one failure.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that a boolean condition is true.
///
/// @param condition Condition expected to be true.
/// @param message Failure message when condition is false.
/// @return Zero on success, otherwise one failure.
static int expect_bool(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }

    return 0;
}

/// Verifies that two size values are equal.
///
/// @param actual Actual value.
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
/// @param actual Actual value.
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

/// Verifies that two signed 32-bit values are equal.
///
/// @param actual Actual value.
/// @param expected Expected value.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_i32(int32_t actual, int32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%d expected=%d)\n", message, actual, expected);
        return 1;
    }

    return 0;
}

/// Verifies that two parser statuses are equal.
///
/// @param actual Actual parser status.
/// @param expected Expected parser status.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_parser_status(VmParserStatus actual, VmParserStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_parser_status_name(actual), vm_parser_status_name(expected));
        return 1;
    }

    return 0;
}

/// Verifies that two executor statuses are equal.
///
/// @param actual Actual executor status.
/// @param expected Expected executor status.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_exec_status(VmExecStatus actual, VmExecStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_exec_status_name(actual), vm_exec_status_name(expected));
        return 1;
    }

    return 0;
}

/// Verifies that two parser diagnostic codes are equal.
///
/// @param actual Actual diagnostic code.
/// @param expected Expected diagnostic code.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_parser_diagnostic_code(VmParserDiagnosticCode actual, VmParserDiagnosticCode expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_parser_diagnostic_code_name(actual), vm_parser_diagnostic_code_name(expected));
        return 1;
    }

    return 0;
}

/// Verifies that two parser diagnostic severities are equal.
///
/// @param actual Actual diagnostic severity.
/// @param expected Expected diagnostic severity.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_parser_diagnostic_severity(VmParserDiagnosticSeverity actual, VmParserDiagnosticSeverity expected, const char *message) {
    if (actual != expected) {
        fprintf(
            stderr,
            "FAIL: %s (actual=%s expected=%s)\n",
            message,
            vm_parser_diagnostic_severity_name(actual),
            vm_parser_diagnostic_severity_name(expected)
        );
        return 1;
    }

    return 0;
}

/// Verifies that two Phase 68 call-target classes are equal.
///
/// @param actual Actual classifier result.
/// @param expected Expected classifier result.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_call_target_class(VmParserCallTargetClass actual, VmParserCallTargetClass expected, const char *message) {
    if (actual != expected) {
        fprintf(
            stderr,
            "FAIL: %s (actual=%s expected=%s)\n",
            message,
            vm_parser_call_target_class_name(actual),
            vm_parser_call_target_class_name(expected)
        );
        return 1;
    }

    return 0;
}

/// Verifies that a C string equals an expected value.
///
/// @param actual Actual string pointer.
/// @param expected Expected null-terminated string.
/// @param message Failure message when strings differ.
/// @return Zero on success, otherwise one failure.
static int expect_string(const char *actual, const char *expected, const char *message) {
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }

    return 0;
}

/// Verifies that a C string contains an expected fragment.
///
/// @param actual Actual string pointer.
/// @param expected_fragment Fragment expected inside @p actual.
/// @param message Failure message when the fragment is absent.
/// @return Zero on success, otherwise one failure.
static int expect_string_contains(const char *actual, const char *expected_fragment, const char *message) {
    if (actual == NULL || expected_fragment == NULL || strstr(actual, expected_fragment) == NULL) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }

    return 0;
}

/// Verifies that a string does not contain an unexpected fragment.
///
/// @param actual Actual string pointer.
/// @param unexpected_fragment Fragment that must be absent from @p actual.
/// @param message Failure message when the fragment is present.
/// @return Zero on success, otherwise one failure.
static int expect_string_not_contains(const char *actual, const char *unexpected_fragment, const char *message) {
    if (actual != NULL && unexpected_fragment != NULL && strstr(actual, unexpected_fragment) != NULL) {
        fprintf(stderr, "FAIL: %s\nUnexpected fragment: %s\nActual: %s\n", message, unexpected_fragment, actual);
        return 1;
    }

    return 0;
}

/// Verifies that a diagnostic message avoids Phase 71B forbidden milestone wording.
///
/// @param actual Diagnostic message to inspect.
/// @param message Failure context for the diagnostic under test.
/// @return Zero on success, otherwise a positive failure count.
static int expect_no_phase71b_forbidden_diagnostic_wording(const char *actual, const char *message) {
    int failures = 0;

    failures += expect_string_not_contains(actual, "not supported in Phase", message);
    failures += expect_string_not_contains(actual, "outside Phase", message);
    failures += expect_string_not_contains(actual, "Phase 69 accepts only", message);
    failures += expect_string_not_contains(actual, "Phase 69 direct CALL accepts only", message);
    failures += expect_string_not_contains(actual, "Phase 70 implements only", message);
    failures += expect_string_not_contains(actual, "deferred to Phase", message);

    return failures;
}

/// Initializes parser buffers and parses source text.
///
/// @param source Source text to parse.
/// @param buffers Caller-owned test buffers.
/// @param out_result Receives parser result.
/// @return Parser status.
static VmParserStatus parse_for_test(const char *source, ParserTestBuffers *buffers, VmParserResult *out_result) {
    VmParserConfig config;

    memset(buffers, 0, sizeof(*buffers));
    memset(&config, 0, sizeof(config));
    config.source = source;
    config.source_file = "main.asm";
    config.tokens = buffers->tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers->lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers->instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers->source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
    config.symbols = buffers->symbols;
    config.symbol_capacity = TEST_SYMBOL_CAPACITY;
    config.numeric_equates = buffers->numeric_equates;
    config.numeric_equate_capacity = TEST_SYMBOL_CAPACITY;
    config.code_labels = buffers->code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.procedure_ranges = buffers->procedure_ranges;
    config.procedure_range_capacity = TEST_PROCEDURE_RANGE_CAPACITY;
    config.data_image = buffers->data_image;
    config.data_image_capacity = TEST_DATA_IMAGE_CAPACITY;
    config.const_image = buffers->const_image;
    config.const_image_capacity = TEST_CONST_IMAGE_CAPACITY;
    config.const_initialized_mask = buffers->const_initialized_mask;
    config.const_initialized_mask_capacity = TEST_CONST_IMAGE_CAPACITY;
    config.diagnostics = buffers->diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;

    return vm_parser_parse_program(&config, out_result);
}

/// Builds a Phase 68 call-target classifier context from parser test buffers.
///
/// @param buffers Parser buffers containing metadata tables.
/// @param result Parser result containing table counts.
/// @param policy Reference-time CASEMAP policy for the query.
/// @return Classifier context for direct helper calls.
static VmParserCallTargetContext call_target_context_for_test(
    const ParserTestBuffers *buffers,
    const VmParserResult *result,
    VmSymbolCasePolicy policy
) {
    VmParserCallTargetContext context;

    memset(&context, 0, sizeof(context));
    context.symbols = buffers->symbols;
    context.symbol_count = result->symbol_count;
    context.code_labels = buffers->code_labels;
    context.code_label_count = result->code_label_count;
    context.procedure_ranges = buffers->procedure_ranges;
    context.procedure_range_count = result->procedure_range_count;
    context.numeric_equates = buffers->numeric_equates;
    context.numeric_equate_count = result->numeric_equate_count;
    context.case_policy = policy;
    return context;
}

/// Verifies one source sample reports the generic unsupported-feature diagnostic.
///
/// @param source MASM-like source text expected to hit a recognized deferred feature.
/// @param expected_message_fragment Fragment expected in the diagnostic message.
/// @return Zero on success, otherwise a positive failure count.
static int expect_unsupported_feature_source(const char *source, const char *expected_message_fragment) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "recognized unsupported feature should produce parser diagnostics");
    if (result.diagnostic_count < 1U) {
        failures += record_failure("recognized unsupported feature should produce at least one parser diagnostic");
        return failures;
    }
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "recognized unsupported feature diagnostic code should match");
    failures += expect_string_contains(buffers.diagnostics[0].message, expected_message_fragment, "recognized unsupported feature diagnostic message should describe the feature");

    return failures;
}

/// Verifies one source sample reports a specific unsupported diagnostic.
///
/// @param source MASM-like source text expected to hit a recognized deferred feature.
/// @param expected_code Parser diagnostic code expected for the first diagnostic.
/// @param expected_message_fragment Fragment expected in the diagnostic message.
/// @return Zero on success, otherwise a positive failure count.
static int expect_specific_unsupported_source(
    const char *source,
    VmParserDiagnosticCode expected_code,
    const char *expected_message_fragment
) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "recognized unsupported feature should produce parser diagnostics");
    if (result.diagnostic_count < 1U) {
        failures += record_failure("recognized unsupported feature should produce at least one parser diagnostic");
        return failures;
    }
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, expected_code, "recognized unsupported feature diagnostic code should match");
    failures += expect_string_contains(buffers.diagnostics[0].message, expected_message_fragment, "recognized unsupported feature diagnostic message should describe the feature");

    return failures;
}

/// Verifies that the guide's minimal program parses into two IR instructions.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_minimal_program_parses_to_ir(void) {
    int failures = 0;
    const char *source = ".code\nmain PROC\n    mov eax, 20\n    add eax, 22\nmain ENDP\nEND main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "minimal program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "minimal program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 2U, "minimal program should emit two instructions");
    failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_MOV, "first instruction should be mov");
    failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "mov destination should be register");
    failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_EAX, "mov destination should be EAX");
    failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "mov source should be immediate");
    failures += expect_u32(buffers.instructions[0].source.immediate, 20U, "mov immediate should be 20");
    failures += expect_u32(buffers.instructions[0].source_line, 3U, "mov should preserve source line");
    failures += expect_string(buffers.instructions[0].source_file, "main.asm", "mov should preserve source file");
    failures += expect_string(buffers.instructions[0].source_text, "mov eax, 20", "mov should preserve trimmed source text");
    failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_ADD, "second instruction should be add");
    failures += expect_u32(buffers.instructions[1].source.immediate, 22U, "add immediate should be 22");
    failures += expect_u32(buffers.instructions[1].instruction_index, 1U, "second instruction index should be one");

    return failures;
}

/// Verifies parser output can run through the existing executor.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_parsed_ir_executes_to_eax_42(void) {
    int failures = 0;
    const char *source = ".code\nmain PROC\nmov eax, 20\nadd eax, 22\nmain ENDP\nEND main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "execution sample should parse");
    failures += expect_exec_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed");
    failures += expect_exec_status(vm_load_program(&vm, buffers.instructions, result.instruction_count), VM_EXEC_STATUS_OK, "parsed program should load");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_OK, "parsed mov should execute");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_OK, "parsed add should execute");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_CODE_FELL_OFF_END, "parsed program should report Phase 71C code-end falloff after two instructions");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed"));
    failures += expect_u32(eax, 42U, "parsed program should produce EAX = 42 before code-end falloff");
    vm_deinit(&vm);

    return failures;
}

/// Verifies register/register and mixed-case instruction parsing.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_register_and_mixed_case(void) {
    int failures = 0;
    const char *source = ".CODE\nMain PROC\nMoV eax, 5\nMoV ebx, eax\nSub ebx, 2\nMain ENDP\nEND Main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t ebx = 0U;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "mixed-case program should parse");
    failures += expect_size(result.instruction_count, 3U, "mixed-case program should emit three instructions");
    failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_REGISTER, "second mov should use register source");
    failures += expect_u32(buffers.instructions[1].source.reg, VM_REGISTER_EAX, "second mov source should be EAX");
    failures += expect_exec_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for mixed-case test");
    failures += expect_exec_status(vm_load_program(&vm, buffers.instructions, result.instruction_count), VM_EXEC_STATUS_OK, "mixed-case program should load");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_OK, "first mixed-case step should execute");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_OK, "second mixed-case step should execute");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_OK, "third mixed-case step should execute");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_CODE_FELL_OFF_END, "mixed-case program should report Phase 71C code-end falloff after three instructions");
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed"));
    failures += expect_u32(ebx, 3U, "register/register parsed program should produce EBX = 3");
    vm_deinit(&vm);

    return failures;
}

/// Verifies labels, blank lines, comments, and CRLF input are accepted.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_labels_blank_lines_comments_and_crlf(void) {
    int failures = 0;
    const char *source = ".code\r\n\r\nmain PROC\r\nstart:\r\nnext: mov eax, 7 ; comment\r\nmain ENDP\r\nEND main\r\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "labels and CRLF should parse successfully");
    failures += expect_size(result.instruction_count, 1U, "label sample should emit one instruction");
    failures += expect_u32(buffers.instructions[0].source_line, 5U, "instruction after labels should preserve line number");
    failures += expect_string(buffers.instructions[0].source_text, "next: mov eax, 7 ; comment", "label-line source text should be preserved");

    return failures;
}


/// Verifies Phase 58 code-label metadata for ordinary, procedure-entry, and no-target labels.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase58_code_label_metadata(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "start:\n"
        "    mov eax, 1\n"
        "done:\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 58 label metadata source should parse");
    failures += expect_size(result.instruction_count, 1U, "Phase 58 label metadata source should emit one instruction");
    failures += expect_size(result.code_label_count, 3U, "Phase 58 should record procedure, executable, and no-target labels");

    failures += expect_string(buffers.code_labels[0].name, "main", "PROC label should preserve spelling");
    failures += expect_u32((uint32_t)buffers.code_labels[0].declaration_kind, (uint32_t)VM_CODE_LABEL_DECLARATION_PROCEDURE_ENTRY, "main should be a procedure-entry declaration");
    failures += expect_u32((uint32_t)buffers.code_labels[0].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_PROCEDURE_ENTRY, "main should target the procedure entry instruction");
    failures += expect_size(buffers.code_labels[0].target_instruction_index, 0U, "main should target instruction index 0");
    failures += expect_u32(buffers.code_labels[0].source_location.line, 2U, "main label line should be recorded");
    failures += expect_u32(buffers.code_labels[0].source_location.column, 1U, "main label column should be recorded");
    failures += expect_size(buffers.code_labels[0].source_location.offset, 6U, "main label byte offset should be recorded");
    failures += expect_size(buffers.code_labels[0].source_span_length, 4U, "main label span should be recorded");

    failures += expect_string(buffers.code_labels[1].name, "start", "ordinary label should preserve spelling");
    failures += expect_u32((uint32_t)buffers.code_labels[1].declaration_kind, (uint32_t)VM_CODE_LABEL_DECLARATION_ORDINARY, "start should be an ordinary label declaration");
    failures += expect_u32((uint32_t)buffers.code_labels[1].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_EXECUTABLE_INSTRUCTION, "start should target an executable instruction");
    failures += expect_size(buffers.code_labels[1].target_instruction_index, 0U, "start should target instruction index 0");
    failures += expect_u32(buffers.code_labels[1].source_location.line, 3U, "start label line should be recorded");
    failures += expect_u32(buffers.code_labels[1].source_location.column, 1U, "start label column should be recorded");
    failures += expect_size(buffers.code_labels[1].source_location.offset, 16U, "start label byte offset should be recorded");
    failures += expect_size(buffers.code_labels[1].source_span_length, 5U, "start label span should be recorded");

    failures += expect_string(buffers.code_labels[2].name, "done", "no-target label should preserve spelling");
    failures += expect_u32((uint32_t)buffers.code_labels[2].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_NO_EXECUTABLE_TARGET, "done should have no executable target");
    failures += expect_bool(!buffers.code_labels[2].has_target_instruction_index, "done should not have a target instruction index");
    failures += expect_size(buffers.code_labels[2].source_location.offset, 38U, "done label byte offset should be recorded");

    return failures;
}

/// Verifies consecutive labels before one instruction point to the same target and are non-executable.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase58_multiple_labels_share_target(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "first:\n"
        "second:\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "consecutive label source should parse");
    failures += expect_size(result.instruction_count, 1U, "consecutive labels should not emit extra instructions");
    failures += expect_size(result.code_label_count, 3U, "procedure plus two labels should be recorded");
    failures += expect_size(buffers.code_labels[1].target_instruction_index, 0U, "first label should target instruction 0");
    failures += expect_size(buffers.code_labels[2].target_instruction_index, 0U, "second label should target instruction 0");
    failures += expect_u32((uint32_t)buffers.code_labels[1].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_EXECUTABLE_INSTRUCTION, "first label target kind should be executable");
    failures += expect_u32((uint32_t)buffers.code_labels[2].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_EXECUTABLE_INSTRUCTION, "second label target kind should be executable");

    return failures;
}

/// Verifies empty and adjacent procedures keep procedure-entry targets separate.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase58_empty_and_adjacent_procedure_labels(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "Empty PROC\n"
        "Empty ENDP\n"
        "Real PROC\n"
        "    mov eax, 1\n"
        "Real ENDP\n"
        "END Real\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "adjacent procedures should parse");
    failures += expect_size(result.instruction_count, 1U, "adjacent procedure source should emit one instruction");
    failures += expect_size(result.code_label_count, 2U, "adjacent procedures should record two procedure-entry labels");
    failures += expect_string(buffers.code_labels[0].name, "Empty", "empty procedure spelling should be preserved");
    failures += expect_u32((uint32_t)buffers.code_labels[0].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_NO_EXECUTABLE_TARGET, "empty procedure should have no executable target");
    failures += expect_bool(!buffers.code_labels[0].has_target_instruction_index, "empty procedure should not target later procedure instruction");
    failures += expect_string(buffers.code_labels[1].name, "Real", "real procedure spelling should be preserved");
    failures += expect_u32((uint32_t)buffers.code_labels[1].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_PROCEDURE_ENTRY, "real procedure should target its body instruction");
    failures += expect_size(buffers.code_labels[1].target_instruction_index, 0U, "real procedure should target instruction 0");

    return failures;
}

/// Verifies procedure bodies with accepted non-executable metadata do not create fake label targets.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase58_non_executable_procedure_metadata_has_no_target(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "Meta PROC\n"
        "OPTION CASEMAP:ALL\n"
        "Meta ENDP\n"
        "END Meta\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "metadata-only procedure body should parse");
    failures += expect_size(result.instruction_count, 0U, "metadata-only procedure body should emit no executable instructions");
    failures += expect_size(result.code_label_count, 1U, "metadata-only procedure should record one procedure-entry label");
    failures += expect_string(buffers.code_labels[0].name, "Meta", "metadata-only procedure spelling should be preserved");
    failures += expect_u32((uint32_t)buffers.code_labels[0].target_kind, (uint32_t)VM_CODE_LABEL_TARGET_NO_EXECUTABLE_TARGET, "metadata-only procedure should have no executable target");
    failures += expect_bool(!buffers.code_labels[0].has_target_instruction_index, "metadata-only procedure should not synthesize a target instruction");

    return failures;
}



/// Verifies Phase 67A procedure ranges and selected END-entry metadata.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase67a_procedure_range_metadata(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "helper PROC\n"
        "    mov ecx, 1\n"
        "helper ENDP\n"
        "main PROC\n"
        "    mov eax, 2\n"
        "main ENDP\n"
        "empty PROC\n"
        "empty ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 67A procedure-range source should parse");
    failures += expect_size(result.instruction_count, 2U, "Phase 67A procedure-range source should emit two instructions");
    failures += expect_size(result.procedure_range_count, 3U, "Phase 67A should record three procedure ranges");
    failures += expect_bool(result.has_selected_entry_procedure, "Phase 67A should record selected END entry procedure");
    failures += expect_size(result.selected_entry_procedure_index, 1U, "END main should select the main procedure range");
    failures += expect_size(result.selected_entry_start_instruction_index, 1U, "main should start at instruction index 1");
    failures += expect_size(result.selected_entry_end_instruction_index, 2U, "main should end before instruction index 2");

    failures += expect_string(buffers.procedure_ranges[0].name, "helper", "helper procedure spelling should be preserved");
    failures += expect_size(buffers.procedure_ranges[0].start_instruction_index, 0U, "helper should start at instruction 0");
    failures += expect_size(buffers.procedure_ranges[0].end_instruction_index, 1U, "helper should end at instruction 1");
    failures += expect_bool(buffers.procedure_ranges[0].has_executable_instruction, "helper should be marked executable");

    failures += expect_string(buffers.procedure_ranges[1].name, "main", "main procedure spelling should be preserved");
    failures += expect_size(buffers.procedure_ranges[1].start_instruction_index, 1U, "main should start at instruction 1");
    failures += expect_size(buffers.procedure_ranges[1].end_instruction_index, 2U, "main should end at instruction 2");
    failures += expect_bool(buffers.procedure_ranges[1].has_executable_instruction, "main should be marked executable");

    failures += expect_string(buffers.procedure_ranges[2].name, "empty", "empty procedure spelling should be preserved");
    failures += expect_size(buffers.procedure_ranges[2].start_instruction_index, 2U, "empty should start at current instruction count");
    failures += expect_size(buffers.procedure_ranges[2].end_instruction_index, 2U, "empty should end at same instruction index");
    failures += expect_bool(!buffers.procedure_ranges[2].has_executable_instruction, "empty should not be marked executable");

    return failures;
}

/// Verifies Phase 67A procedure-range capacity diagnostics remain structured.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase67a_procedure_range_capacity_diagnostic(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "first PROC\n"
        "first ENDP\n"
        "second PROC\n"
        "second ENDP\n"
        "END first\n";
    ParserTestBuffers buffers;
    VmParserConfig config;
    VmParserResult result;

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    memset(&result, 0, sizeof(result));
    config.source = source;
    config.source_file = "main.asm";
    config.tokens = buffers.tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
    config.symbols = buffers.symbols;
    config.symbol_capacity = TEST_SYMBOL_CAPACITY;
    config.numeric_equates = buffers.numeric_equates;
    config.numeric_equate_capacity = TEST_SYMBOL_CAPACITY;
    config.code_labels = buffers.code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.procedure_ranges = buffers.procedure_ranges;
    config.procedure_range_capacity = 1U;
    config.data_image = buffers.data_image;
    config.data_image_capacity = TEST_DATA_IMAGE_CAPACITY;
    config.const_image = buffers.const_image;
    config.const_image_capacity = TEST_CONST_IMAGE_CAPACITY;
    config.const_initialized_mask = buffers.const_initialized_mask;
    config.const_initialized_mask_capacity = TEST_CONST_IMAGE_CAPACITY;
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;

    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_PROCEDURE_CAPACITY_EXCEEDED, "small procedure-range table should produce a capacity status");
    failures += expect_size(result.procedure_range_count, 1U, "rejected procedure range should not be inserted after capacity failure");
    failures += expect_size(result.diagnostic_count, 1U, "procedure range capacity should emit one diagnostic");
    failures += expect_u32((uint32_t)buffers.diagnostics[0].code, (uint32_t)VM_PARSER_DIAGNOSTIC_PROCEDURE_CAPACITY_EXCEEDED, "procedure range capacity should use the structured diagnostic code");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Procedure range capacity exceeded", "procedure range capacity diagnostic should be descriptive");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "procedure capacity diagnostic should point at the second PROC name line");
    failures += expect_size(buffers.diagnostics[0].location.column, 1U, "procedure capacity diagnostic should point at the second PROC name column");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 6U, "procedure capacity diagnostic should span the second PROC name");

    return failures;
}

/// Verifies Phase 68 classifies future CALL targets without executing CALL.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase68_call_target_classifier_metadata(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserCallTargetContext context;
    VmParserCallTargetClassification classification;
    VmLexerToken malformed_token;
    VmLexerToken register_token;

    failures += expect_parser_status(parse_for_test(
        "COUNT = 4\n"
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "SomeLabel:\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "Helper PROC\n"
        "Helper ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 68 classifier fixture should parse without diagnostics");

    context = call_target_context_for_test(&buffers, &result, VM_SYMBOL_CASE_POLICY_ALL);
    failures += expect_size(result.numeric_equate_count, 1U, "accepted numeric equate should be published for classifier metadata");
    failures += expect_string(buffers.numeric_equates[0].name, "COUNT", "numeric equate metadata should preserve spelling");
    failures += expect_size((size_t)buffers.numeric_equates[0].value, 4U, "numeric equate metadata should preserve value");
    failures += expect_size(buffers.numeric_equates[0].source_location.line, 1U, "numeric equate metadata should preserve source line");

    classification = vm_parser_classify_call_target_name(&context, "Helper", strlen("Helper"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_USER_PROCEDURE_ENTRY, "Helper PROC should classify as user procedure entry");
    failures += expect_bool(classification.has_metadata_index, "Helper procedure classification should include a metadata index");

    classification = vm_parser_classify_call_target_name(&context, "SomeLabel", strlen("SomeLabel"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_CODE_LABEL, "ordinary code label should not classify as procedure entry");
    failures += expect_bool(classification.has_metadata_index, "ordinary code label classification should include a metadata index");

    classification = vm_parser_classify_call_target_name(&context, "value", strlen("value"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_DATA_SYMBOL, "data symbol should classify as data-symbol target");
    failures += expect_bool(classification.has_metadata_index, "data symbol classification should include a metadata index");

    classification = vm_parser_classify_call_target_name(&context, "COUNT", strlen("COUNT"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_NUMERIC_EQUATE, "numeric equate should classify as numeric-equate target");
    failures += expect_bool(classification.has_metadata_index, "numeric equate classification should include a metadata index");

    classification = vm_parser_classify_call_target_name(&context, "WriteString", strlen("WriteString"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_IRVINE32_PLANNED, "WriteString should classify as planned Irvine32 routine");
    failures += expect_u32((uint32_t)classification.irvine32_symbol_class, (uint32_t)VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE, "WriteString should expose the central registry class");

    classification = vm_parser_classify_call_target_name(&context, "writestring", strlen("writestring"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_IRVINE32_PLANNED, "Irvine32 lookup should be case-insensitive");

    classification = vm_parser_classify_call_target_name(&context, "exit", strlen("exit"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_IRVINE32_SUPPORTED, "exit should classify as supported virtual Irvine32 terminator");

    classification = vm_parser_classify_call_target_name(&context, "OpenInputFile", strlen("OpenInputFile"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_IRVINE32_UNSUPPORTED, "OpenInputFile should classify as unsupported v1 Irvine32 routine");

    classification = vm_parser_classify_call_target_name(&context, "ExitProcess", strlen("ExitProcess"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_EXTERNAL_NON_GOAL, "ExitProcess should classify as external/Windows API non-goal");

    classification = vm_parser_classify_call_target_name(&context, "_DATA", strlen("_DATA"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_EXTERNAL_NON_GOAL, "MASM linker segment symbols should classify as external non-goals");

    classification = vm_parser_classify_call_target_name(&context, "mov", strlen("mov"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_RESERVED_WORD, "instruction mnemonic should classify as reserved word");

    classification = vm_parser_classify_call_target_name(&context, "Missing", strlen("Missing"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_UNKNOWN_SYMBOL, "unknown identifier-shaped target should classify as unknown symbol");

    classification = vm_parser_classify_call_target_name(&context, "", 0U);
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_MALFORMED_EXPRESSION, "empty target should classify as malformed expression");

    memset(&malformed_token, 0, sizeof(malformed_token));
    malformed_token.kind = VM_LEXER_TOKEN_NUMBER;
    malformed_token.lexeme = "1234";
    malformed_token.lexeme_length = 4U;
    classification = vm_parser_classify_call_target_token(&context, &malformed_token);
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_MALFORMED_EXPRESSION, "numeric target token should classify as malformed expression");

    memset(&register_token, 0, sizeof(register_token));
    register_token.kind = VM_LEXER_TOKEN_REGISTER;
    register_token.lexeme = "eax";
    register_token.lexeme_length = 3U;
    classification = vm_parser_classify_call_target_token(&context, &register_token);
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_RESERVED_WORD, "register target token should classify as reserved word");

    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_MALFORMED_EXPRESSION), "malformed-target-expression", "call-target helper should name malformed expressions");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_USER_PROCEDURE_ENTRY), "user-procedure-entry", "call-target helper should name user procedure entries");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_CODE_LABEL), "code-label", "call-target helper should name code labels");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_IRVINE32_SUPPORTED), "irvine32-supported", "call-target helper should name supported Irvine32 targets");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_IRVINE32_PLANNED), "irvine32-planned", "call-target helper should name planned Irvine32 targets");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_IRVINE32_UNSUPPORTED), "irvine32-unsupported", "call-target helper should name unsupported Irvine32 targets");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_EXTERNAL_NON_GOAL), "external-non-goal", "call-target helper should name external non-goal targets");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_DATA_SYMBOL), "data-symbol", "call-target helper should name data symbols");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_NUMERIC_EQUATE), "numeric-equate", "call-target helper should name numeric equates");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_LOCAL_SYMBOL), "local-symbol", "call-target helper should name local symbols");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_RESERVED_WORD), "reserved-word", "call-target helper should name reserved words");
    failures += expect_string(vm_parser_call_target_class_name(VM_PARSER_CALL_TARGET_UNKNOWN_SYMBOL), "unknown-symbol", "call-target helper should name unknown symbols");

    if (vm_parser_call_target_class_name((VmParserCallTargetClass)999) != NULL) {
        failures += record_failure("invalid call target class name should be NULL");
    }

    return failures;
}

/// Verifies Phase 68 classifier and procedure metadata preserve CASEMAP policy.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase68_call_target_classifier_casemap_policy(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserCallTargetContext context;
    VmParserCallTargetClassification classification;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "Helper PROC\n"
        "Helper ENDP\n"
        "helper PROC\n"
        "helper ENDP\n"
        "END Helper\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "default CASEMAP should reject folded duplicate procedures");
    failures += expect_size(result.diagnostic_count, 1U, "folded duplicate procedure should emit one diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_PROCEDURE, "folded duplicate procedure should use duplicate-procedure diagnostic");
    failures += expect_string_contains(buffers.diagnostics[0].message, "prior procedure", "duplicate procedure diagnostic should identify prior procedure metadata");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "OPTION CASEMAP:NONE\n"
        "Helper PROC\n"
        "Helper ENDP\n"
        "helper PROC\n"
        "helper ENDP\n"
        "END Helper\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "CASEMAP:NONE should allow procedures that differ only by case");
    failures += expect_size(result.procedure_range_count, 2U, "CASEMAP:NONE fixture should record two procedure ranges");

    context = call_target_context_for_test(&buffers, &result, VM_SYMBOL_CASE_POLICY_NONE);
    classification = vm_parser_classify_call_target_name(&context, "Helper", strlen("Helper"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_USER_PROCEDURE_ENTRY, "exact Helper lookup should classify as procedure");
    failures += expect_size(classification.metadata_index, 0U, "exact Helper lookup should use first procedure metadata entry");

    classification = vm_parser_classify_call_target_name(&context, "helper", strlen("helper"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_USER_PROCEDURE_ENTRY, "exact helper lookup should classify as procedure");
    failures += expect_size(classification.metadata_index, 1U, "exact helper lookup should use second procedure metadata entry");

    classification = vm_parser_classify_call_target_name(&context, "HELPER", strlen("HELPER"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_UNKNOWN_SYMBOL, "CASEMAP:NONE classifier should not fold user procedure names");

    classification = vm_parser_classify_call_target_name(&context, "writestring", strlen("writestring"));
    failures += expect_call_target_class(classification.target_class, VM_PARSER_CALL_TARGET_IRVINE32_PLANNED, "CASEMAP:NONE should not make Irvine32 names case-sensitive");

    return failures;
}


/// Verifies Phase 69 lowers direct user-procedure CALL targets into executable IR.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase69_direct_call_to_user_procedure_parses_to_ir(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    call Helper\n"
        "    mov ebx, 2\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov ecx, 3\n"
        "Helper ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 69 direct CALL fixture should parse");
    failures += expect_size(result.instruction_count, 4U, "Phase 69 direct CALL fixture should lower four executable instructions");
    failures += expect_u32((uint32_t)buffers.instructions[1].opcode, (uint32_t)VM_IR_OPCODE_CALL, "direct CALL should lower to the CALL IR opcode");
    failures += expect_u32((uint32_t)buffers.instructions[1].destination.kind, (uint32_t)VM_IR_OPERAND_BRANCH_TARGET, "direct CALL should use branch-target metadata for the procedure entry");
    failures += expect_u32(buffers.instructions[1].destination.immediate, 3U, "direct CALL should target the first executable Helper instruction");
    failures += expect_u32((uint32_t)buffers.instructions[1].source.kind, (uint32_t)VM_IR_OPERAND_BRANCH_TARGET, "direct CALL should retain Phase 70 return-target metadata");
    failures += expect_u32(buffers.instructions[1].source.immediate, 2U, "direct CALL should return to the next executable instruction in the same procedure");
    failures += expect_size(result.diagnostic_count, 0U, "accepted direct CALL should not emit diagnostics");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call helper\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov ebx, 2\n"
        "Helper ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "default CASEMAP should resolve folded direct CALL procedure names");
    failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_CALL, "folded direct CALL should lower to CALL");
    failures += expect_u32(buffers.instructions[0].destination.immediate, 2U, "folded direct CALL should target Helper's executable entry");
    failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "folded direct CALL should return to the same-procedure successor");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov eax, 1\n"
        "Helper ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "terminal CALL fixture should parse without inventing a helper return successor");
    failures += expect_size(result.instruction_count, 2U, "terminal CALL fixture should lower only CALL and helper MOV");
    failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_CALL, "terminal direct CALL should lower to CALL");
    failures += expect_u32(buffers.instructions[0].destination.immediate, 1U, "terminal direct CALL should still target Helper's executable entry");
    failures += expect_u32(buffers.instructions[0].source.immediate, 2U, "terminal direct CALL should encode an invalid boundary return token instead of the helper body");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "OPTION CASEMAP:NONE\n"
        "main PROC\n"
        "    call helper\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov eax, 1\n"
        "Helper ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:NONE should not fold direct CALL user procedure names");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "CASEMAP:NONE mismatched CALL target should be unknown");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "CASEMAP:NONE CALL diagnostic should point to target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "CASEMAP:NONE CALL diagnostic should point to target column");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 6U, "CASEMAP:NONE CALL diagnostic should span target token");

    return failures;
}

/// Verifies Phase 69 rejects non-procedure direct CALL targets without enabling future forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase69_direct_call_target_rejections(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "LabelOnly:\n"
        "    call LabelOnly\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL ordinary code label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CALL_TARGET, "CALL ordinary label should use invalid-call-target");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "ordinary-label CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "ordinary-label CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 36U, "ordinary-label CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 9U, "ordinary-label CALL diagnostic should span target token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "This simulator accepts only user procedure entries", "ordinary-label CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "ordinary-label CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    call value\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CALL_TARGET, "CALL data symbol should use invalid-call-target");
    failures += expect_size(buffers.diagnostics[0].location.line, 5U, "data-symbol CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "data-symbol CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 45U, "data-symbol CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 5U, "data-symbol CALL diagnostic should span target token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "This simulator accepts only user procedure entries", "data-symbol CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "data-symbol CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        "COUNT = 4\n"
        ".code\n"
        "main PROC\n"
        "    call COUNT\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL numeric equate should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CALL_TARGET, "CALL numeric equate should use invalid-call-target");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "numeric-equate CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "numeric-equate CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 35U, "numeric-equate CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 5U, "numeric-equate CALL diagnostic should span target token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "This simulator accepts only user procedure entries", "numeric-equate CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "numeric-equate CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call ret\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL reserved-word target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CALL_TARGET, "CALL reserved-word target should use invalid-call-target");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "reserved-word CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "reserved-word CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 25U, "reserved-word CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 3U, "reserved-word CALL diagnostic should span target token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "This simulator accepts only user procedure entries", "reserved-word CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "reserved-word CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call NEAR PTR Helper\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL distance/type override target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_FORM, "CALL distance/type override target should use unsupported-call-form");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "distance-override CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "distance-override CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 25U, "distance-override CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 4U, "distance-override CALL diagnostic should span override token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "CALL distance and type overrides", "distance-override CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "distance-override CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call 1234\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL immediate target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_FORM, "CALL immediate target should use unsupported-call-form");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "immediate CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "immediate CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 25U, "immediate CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 4U, "immediate CALL diagnostic should span target token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "CALL expression and immediate targets", "immediate CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "immediate CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call .code\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL directive target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CALL_TARGET, "CALL directive target should use invalid-call-target");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "directive CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "directive CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 25U, "directive CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 5U, "directive CALL diagnostic should span target token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "This simulator accepts only user procedure entries", "directive CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "directive CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL register target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_FORM, "CALL register target should use unsupported-call-form");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "register CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "register CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 25U, "register CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 3U, "register CALL diagnostic should span target token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "direct user-procedure CALL targets", "register CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "register CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL memory target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_FORM, "CALL memory target should use unsupported-call-form");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "memory CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "memory CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 25U, "memory CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 1U, "memory CALL diagnostic should span opening bracket token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "direct user-procedure CALL targets", "memory CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "memory CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call OFFSET Helper\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov eax, 1\n"
        "Helper ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL OFFSET target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_FORM, "CALL OFFSET target should use unsupported-call-form");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "OFFSET CALL diagnostic should preserve target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 10U, "OFFSET CALL diagnostic should preserve target column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 25U, "OFFSET CALL diagnostic should preserve target byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 6U, "OFFSET CALL diagnostic should span OFFSET token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "direct user-procedure CALL targets", "OFFSET CALL diagnostic should use stable simulator boundary wording");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "OFFSET CALL diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call WriteString\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL Irvine32 target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE, "CALL Irvine32 target should remain deferred");

    failures += expect_parser_status(parse_for_test(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call ExitProcess\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL external target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_EXTERNAL_CALL, "CALL external target should use unsupported-external-call");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    call Missing\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CALL unknown target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "CALL unknown identifier should use unknown-symbol");

    return failures;
}


/// Verifies Phase 70 accepts plain near RET and lowers it into executable IR.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase70_plain_ret_parses_to_ir(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 70 lowercase RET fixture should parse");
    failures += expect_size(result.instruction_count, 1U, "plain RET should lower one executable instruction");
    failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_RET, "plain RET should lower to RET opcode");
    failures += expect_u32((uint32_t)buffers.instructions[0].destination.kind, (uint32_t)VM_IR_OPERAND_NONE, "plain RET destination should be empty");
    failures += expect_u32((uint32_t)buffers.instructions[0].source.kind, (uint32_t)VM_IR_OPERAND_NONE, "plain RET source should be empty");
    failures += expect_size(result.diagnostic_count, 0U, "plain RET should not emit parser diagnostics");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    RET\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 70 uppercase RET fixture should parse");
    failures += expect_size(result.instruction_count, 1U, "uppercase RET should lower one executable instruction");
    failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_RET, "uppercase RET should lower to RET opcode");

    return failures;
}

/// Verifies Phase 74 accepts RET imm16 and still rejects non-immediate return forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase74_ret_imm16_forms(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    static const struct {
        const char *source_line;
        uint32_t expected_value;
    } accepted_cases[] = {
        {"ret 0", 0U},
        {"ret 4", 4U},
        {"ret 8", 8U},
        {"ret 16", 16U},
        {"ret 0010h", 16U},
        {"ret 65535", 65535U}
    };
    static const struct {
        const char *source_line;
        VmParserDiagnosticCode expected_code;
        const char *message_fragment;
    } rejected_cases[] = {
        {"ret -1", VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "unsigned 16-bit"},
        {"ret 10000h", VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "unsigned 16-bit"},
        {"ret eax", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION_FORM, "unsigned 16-bit immediate"},
        {"ret DWORD PTR [esp]", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION_FORM, "register, memory, and far-return"},
        {"ret 4, 8", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "exactly one"},
        {"retf", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION_FORM, "Far RET forms are not implemented"},
        {"retf 4", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION_FORM, "Far RET forms are not implemented"}
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(accepted_cases) / sizeof(accepted_cases[0]); index += 1U) {
        char source[256];
        memset(&result, 0, sizeof(result));
        (void)snprintf(
            source,
            sizeof(source),
            ".code\nmain PROC\n    %s\nmain ENDP\nEND main\n",
            accepted_cases[index].source_line
        );
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "RET imm16 accepted form should parse");
        failures += expect_size(result.instruction_count, 1U, "RET imm16 accepted form should emit one instruction");
        if (result.instruction_count >= 1U) {
            failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_RET, "RET imm16 should lower to RET opcode");
            failures += expect_u32((uint32_t)buffers.instructions[0].destination.kind, (uint32_t)VM_IR_OPERAND_NONE, "RET imm16 destination should be empty");
            failures += expect_u32((uint32_t)buffers.instructions[0].source.kind, (uint32_t)VM_IR_OPERAND_IMMEDIATE, "RET imm16 source should be immediate");
            failures += expect_u32(buffers.instructions[0].source.width_bits, 16U, "RET imm16 source should be 16-bit metadata");
            failures += expect_u32(buffers.instructions[0].source.immediate, accepted_cases[index].expected_value, "RET imm16 immediate value should match source");
        }
    }

    for (index = 0U; index < sizeof(rejected_cases) / sizeof(rejected_cases[0]); index += 1U) {
        char source[256];
        memset(&result, 0, sizeof(result));
        (void)snprintf(
            source,
            sizeof(source),
            ".code\nmain PROC\n    %s\nmain ENDP\nEND main\n",
            rejected_cases[index].source_line
        );
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "invalid RET form should produce diagnostic status");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, rejected_cases[index].expected_code, "invalid RET diagnostic code should match");
        failures += expect_string_contains(buffers.diagnostics[0].message, rejected_cases[index].message_fragment, "invalid RET diagnostic should explain rejected form");
        failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "invalid RET diagnostic must not use milestone-relative wording");
    }

    return failures;
}

/// Verifies Phase 68 procedure-name diagnostics for reserved and malformed names.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase68_procedure_name_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    static const char *reserved_sources[] = {
        ".code\nWriteString PROC\nWriteString ENDP\nEND WriteString\n",
        ".code\nwritestring PROC\nwritestring ENDP\nEND writestring\n",
        ".code\nmov PROC\nmov ENDP\nEND mov\n",
        ".code\neax PROC\neax ENDP\nEND eax\n",
        ".code\nDWORD PROC\nDWORD ENDP\nEND DWORD\n",
        ".code\nPTR PROC\nPTR ENDP\nEND PTR\n",
        ".code\nINCLUDE PROC\nINCLUDE ENDP\nEND INCLUDE\n",
        ".code\nOFFSET PROC\nOFFSET ENDP\nEND OFFSET\n",
        ".code\nEQ PROC\nEQ ENDP\nEND EQ\n",
        ".code\nIrvine32 PROC\nIrvine32 ENDP\nEND Irvine32\n",
        ".code\nOPTION CASEMAP:NONE\nWriteString PROC\nWriteString ENDP\nEND WriteString\n",
        ".code\nOPTION CASEMAP:NONE\ninclude PROC\ninclude ENDP\nEND include\n"
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(reserved_sources) / sizeof(reserved_sources[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(reserved_sources[index], &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "reserved procedure name should diagnose");
        failures += expect_size(result.procedure_range_count, 0U, "reserved procedure name must not publish procedure metadata");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "reserved procedure name should use reserved-word-symbol");
    }

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "Thing DWORD 1\n"
        ".code\n"
        "Thing PROC\n"
        "Thing ENDP\n"
        "END Thing\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "procedure name should conflict with existing data symbol");
    failures += expect_size(result.procedure_range_count, 0U, "data/procedure conflict must not publish procedure metadata");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LABEL_SYMBOL_CONFLICT, "data/procedure conflict should use label-symbol-conflict");
    failures += expect_string_contains(buffers.diagnostics[0].message, "data symbol", "data/procedure conflict message should identify data-symbol category");

    failures += expect_parser_status(parse_for_test(
        "COUNT = 4\n"
        ".code\n"
        "COUNT PROC\n"
        "COUNT ENDP\n"
        "END COUNT\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "procedure name should conflict with existing numeric equate");
    failures += expect_size(result.procedure_range_count, 0U, "equate/procedure conflict must not publish procedure metadata");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LABEL_SYMBOL_CONFLICT, "equate/procedure conflict should use label-symbol-conflict");
    failures += expect_string_contains(buffers.diagnostics[0].message, "numeric equate", "equate/procedure conflict message should identify numeric-equate category");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "123 PROC\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "malformed procedure name should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_PROCEDURE_NAME, "malformed procedure name should use invalid-procedure-name");
    failures += expect_size(buffers.diagnostics[0].location.line, 2U, "malformed procedure name diagnostic should preserve line");
    failures += expect_size(buffers.diagnostics[0].location.column, 1U, "malformed procedure name diagnostic should preserve column");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 3U, "malformed procedure name diagnostic should preserve span");

    return failures;
}

/// Verifies Phase 68B rejects EIP as source-level operand and declaration state.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase68b_eip_control_state_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    static const char *operand_sources[] = {
        ".code\nmain PROC\n    mov eip, 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, eip\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    add eip, 4\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    sub eip, 4\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    cmp eip, eax\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    xchg eip, eax\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    lea eip, [eax]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    nop eip\nmain ENDP\nEND main\n",
        ".data\nvalue DWORD 1\n.code\nmain PROC\n    mov eax, [eip]\nmain ENDP\nEND main\n",
        ".data\nvalue DWORD 1\n.code\nmain PROC\n    mov eax, [eax + eip]\nmain ENDP\nEND main\n",
        ".data\nvalue DWORD 1\n.code\nmain PROC\n    mov [eip], eax\nmain ENDP\nEND main\n"
    };
    static const char *declaration_sources[] = {
        ".data\nEIP DWORD 1\n.code\nEND\n",
        "EIP = 4\n.code\nEND\n",
        ".code\nEIP:\n    mov eax, 1\nEND\n",
        ".code\nEIP PROC\nEND\n"
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(operand_sources) / sizeof(operand_sources[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(operand_sources[index], &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "EIP source operand should diagnose");
        failures += expect_size(result.diagnostic_count, 1U, "EIP source operand should emit one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_EIP_OPERAND, "EIP source operand should use invalid-eip-operand");
        failures += expect_string_contains(buffers.diagnostics[0].message, "displayed VM control state", "EIP diagnostic should explain control-state display");
        failures += expect_u32(buffers.diagnostics[0].location.line, 3U + (index >= 8U ? 2U : 0U), "EIP operand diagnostic should preserve source line");
        if (buffers.diagnostics[0].lexeme_length != 3U) {
            failures += record_failure("EIP operand diagnostic should span EIP");
        }
    }

    for (index = 0U; index < sizeof(declaration_sources) / sizeof(declaration_sources[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(declaration_sources[index], &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "EIP declaration should diagnose");
        if (result.diagnostic_count < 1U) {
            failures += record_failure("EIP declaration should emit a diagnostic");
            continue;
        }
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_EIP_OPERAND, "EIP declaration should use invalid-eip-operand");
        failures += expect_string_contains(buffers.diagnostics[0].message, "not a source-writable general-purpose register", "EIP declaration diagnostic should explain source restrictions");
        if (result.symbol_count != 0U || result.numeric_equate_count != 0U || result.code_label_count != 0U || result.procedure_range_count != 0U) {
            failures += record_failure("rejected EIP declaration should not publish user metadata");
        }
    }

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    mov eax, ip\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IP should remain unsupported as a built-in register alias");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "IP source operand should remain an unknown symbol, not a register alias");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    mov esp, 1\n    mov esp, eax\n    add esp, 4\n    sub esp, 4\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 68B should preserve supported explicit ESP writes");

    return failures;
}

static int test_phase58_label_casemap_policy(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\nSpin:\n    mov eax, 1\nspin:\n    mov ebx, 2\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "default CASEMAP should reject folded duplicate labels");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_LABEL, "folded duplicate label diagnostic should match");
    failures += expect_bool(buffers.diagnostics[0].has_related_location, "folded duplicate should include prior-definition metadata");
    failures += expect_u32(buffers.diagnostics[0].related_location.line, 3U, "folded duplicate prior line should point at Spin");
    failures += expect_string_contains(buffers.diagnostics[0].message, "case-insensitive", "folded duplicate message should mention CASEMAP behavior");
    failures += expect_size(result.code_label_count, 2U, "rejected folded duplicate should not be inserted");

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:ALL\n.code\nmain PROC\nSpin:\n    mov eax, 1\nspin:\n    mov ebx, 2\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "explicit CASEMAP:ALL should reject folded duplicate labels");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_LABEL, "explicit ALL folded duplicate diagnostic should match");

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n.code\nmain PROC\nSpin:\nspin:\n    MoV eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "CASEMAP:NONE should allow labels that differ only by case while keywords remain case-insensitive");
    failures += expect_size(result.code_label_count, 3U, "CASEMAP:NONE should record main, Spin, and spin labels");

    return failures;
}

/// Verifies Phase 58 duplicate and cross-symbol label conflict diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase58_label_conflict_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\nstart:\n    mov eax, 1\nstart:\n    mov ebx, 2\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "duplicate ordinary label should be diagnosed");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_LABEL, "duplicate ordinary label diagnostic should match");
    failures += expect_u32(buffers.diagnostics[0].location.line, 5U, "duplicate diagnostic should point at second start");
    failures += expect_u32(buffers.diagnostics[0].related_location.line, 3U, "duplicate diagnostic should record first start");
    failures += expect_size(result.code_label_count, 2U, "duplicate ordinary label should not be inserted");

    failures += expect_parser_status(parse_for_test(
        ".data\nValue DWORD 1\n.code\nmain PROC\nvalue:\n    mov eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "code label should conflict with folded data symbol");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LABEL_SYMBOL_CONFLICT, "data-symbol conflict diagnostic should match");
    failures += expect_u32(buffers.diagnostics[0].location.line, 5U, "data conflict diagnostic should point at label");
    failures += expect_u32(buffers.diagnostics[0].related_location.line, 2U, "data conflict prior metadata should point at data symbol");
    failures += expect_string_contains(buffers.diagnostics[0].message, "data symbol", "data conflict message should identify data-symbol category");

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n.data\nValue DWORD 1\n.code\nmain PROC\nvalue:\n    mov eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "CASEMAP:NONE should allow data symbol and label that differ by case");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\nmain:\n    mov eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ordinary label should conflict with existing PROC label");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_LABEL, "PROC then ordinary conflict diagnostic should match");
    failures += expect_u32(buffers.diagnostics[0].related_location.line, 2U, "PROC then ordinary prior metadata should point at PROC");

    failures += expect_parser_status(parse_for_test(
        ".code\nstart:\nstart PROC\n    mov eax, 1\nstart ENDP\nEND start\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "PROC label should conflict with existing ordinary label");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_LABEL, "ordinary then PROC conflict diagnostic should match");
    failures += expect_u32(buffers.diagnostics[0].related_location.line, 2U, "ordinary then PROC prior metadata should point at ordinary label");

    failures += expect_parser_status(parse_for_test(
        "COUNT = 4\n.code\nmain PROC\nCOUNT:\n    mov eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "code label should conflict with numeric equate");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LABEL_SYMBOL_CONFLICT, "numeric-equate conflict diagnostic should match");
    failures += expect_u32(buffers.diagnostics[0].related_location.line, 1U, "numeric-equate prior metadata should point at equate");
    failures += expect_string_contains(buffers.diagnostics[0].message, "numeric equate", "numeric-equate conflict message should identify category");

    return failures;
}

/// Verifies a zero-instruction procedure is accepted.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_zero_instruction_procedure(void) {
    int failures = 0;
    const char *source = ".code\nmain PROC\nmain ENDP\nEND main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "empty procedure should parse successfully");
    failures += expect_size(result.instruction_count, 0U, "empty procedure should emit no instructions");

    return failures;
}

/// Verifies unsupported and malformed syntax diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_error_path_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;


    failures += expect_parser_status(parse_for_test("main PROC\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "missing .code should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, "missing .code diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nfoo eax\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unsupported instruction should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION, "unsupported instruction diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax 20\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "missing comma should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, "missing comma diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov 20, eax\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "immediate destination should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "immediate destination diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax, 1\nmain ENDP\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "missing END should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_END, "missing END diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmain ENDP\nEND other\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "invalid END target should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_END_TARGET, "invalid END target diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmain ENDP\nEND main\nmov eax, 1\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "source after END should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "source after END diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax, 18446744073709551615\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "out-of-range immediate should produce parser diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "out-of-range immediate diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax, 184467440737095516160\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_LEXER_FAILED, "lexer numeric overflow should stop parser");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LEXER_NUMBER_OVERFLOW, "lexer overflow diagnostic should be surfaced by parser");

    return failures;
}

/// Verifies Phase 15 explicitly classifies common unsupported MASM directives.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_textbook_unsupported_directives_are_stable(void) {
    int failures = 0;

    failures += expect_specific_unsupported_source(".code\nmain PROC\nmov eax, 1\n.IF eax == 1\nmov ebx, 2\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_IF, ".IF");
    failures += expect_specific_unsupported_source(".code\nmain PROC\nmov ecx, 3\n.WHILE ecx > 0\nsub ecx, 1\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_WHILE, ".WHILE");
    failures += expect_specific_unsupported_source(".code\nmain PROC\n.REPEAT\nmov eax, 1\n.UNTIL eax == 1\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_REPEAT, ".REPEAT");
    failures += expect_specific_unsupported_source(".code\nmain PROC\n.BREAK\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW, ".BREAK");
    failures += expect_specific_unsupported_source(".code\nmain PROC\n.CONTINUE\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW, ".CONTINUE");

    return failures;
}

/// Verifies Phase 15 explicitly classifies common unsupported MASM keywords.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_textbook_unsupported_keywords_are_stable(void) {
    int failures = 0;

    failures += expect_unsupported_feature_source("NAME TEXTEQU <main>\n.code\nmain PROC\nmain ENDP\nEND main\n", "TEXTEQU");
    failures += expect_unsupported_feature_source("Point STRUCT\nx DWORD ?\nPoint ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n", "STRUCT");
    failures += expect_unsupported_feature_source("Choice UNION\nx DWORD ?\nChoice ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n", "UNION");
    failures += expect_unsupported_feature_source("Flags RECORD bit0:1\n.code\nmain PROC\nmain ENDP\nEND main\n", "RECORD");
    failures += expect_unsupported_feature_source("ExitProcess PROTO\n.code\nmain PROC\nmain ENDP\nEND main\n", "PROTO");
    failures += expect_unsupported_feature_source("EXTERN ExitProcess:PROC\n.code\nmain PROC\nmain ENDP\nEND main\n", "EXTERN");
    failures += expect_unsupported_feature_source("PUBLIC main\n.code\nmain PROC\nmain ENDP\nEND main\n", "PUBLIC");
    failures += expect_unsupported_feature_source("COMM buffer:BYTE:16\n.code\nmain PROC\nmain ENDP\nEND main\n", "COMM");
    failures += expect_unsupported_feature_source("m MACRO\nENDM\n.code\nmain PROC\nmain ENDP\nEND main\n", "macro definitions");

    return failures;
}

/// Verifies backlog-only data types remain classified without implementation.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_scheduled_and_backlog_data_types_are_documented_diagnostics(void) {
    int failures = 0;

    failures += expect_unsupported_feature_source(".data\nr REAL4 1.0\n.code\nmain PROC\nmain ENDP\nEND main\n", "backlog");
    failures += expect_unsupported_feature_source(".data\nr REAL8 1.0\n.code\nmain PROC\nmain ENDP\nEND main\n", "backlog");
    failures += expect_unsupported_feature_source(".data\nr REAL10 1.0\n.code\nmain PROC\nmain ENDP\nEND main\n", "backlog");
    failures += expect_unsupported_feature_source(".data\nt TBYTE 0\n.code\nmain PROC\nmain ENDP\nEND main\n", "backlog");
    failures += expect_unsupported_feature_source(".data\nf FWORD 0\n.code\nmain PROC\nmain ENDP\nEND main\n", "backlog");

    return failures;
}

/// Verifies Milestone 17 collects several safe unsupported-feature diagnostics in one parse.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_multi_diagnostic_unsupported_feature_recovery(void) {
    const char *source =
        ".data\n"
        "Point STRUCT\n"
        "    x DWORD ?\n"
        "Point ENDS\n"
        ".code\n"
        "main PROC\n"
        "    INVOKE SomeProc\n"
        "    .IF eax == 0\n"
        "        mov ebx, 1\n"
        "    .ENDIF\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "multiple unsupported constructs should recover with diagnostics");
    failures += expect_size(result.diagnostic_count, 4U, "STRUCT, INVOKE, .IF, and .ENDIF should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "STRUCT diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "INVOKE diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_IF, ".IF diagnostic code should match");
    failures += expect_string_contains(buffers.diagnostics[0].message, "STRUCT", "first diagnostic should describe STRUCT");
    failures += expect_string_contains(buffers.diagnostics[1].message, "INVOKE", "second diagnostic should describe INVOKE");
    failures += expect_string_contains(buffers.diagnostics[2].message, ".IF", "third diagnostic should describe .IF");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[3].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ENDIF, ".ENDIF diagnostic code should match");
    failures += expect_string_contains(buffers.diagnostics[3].message, ".ENDIF", "fourth diagnostic should describe .ENDIF");
    failures += expect_u32(buffers.diagnostics[0].location.line, 2U, "STRUCT diagnostic line should be preserved");
    failures += expect_u32(buffers.diagnostics[1].location.line, 7U, "INVOKE diagnostic line should be preserved");
    failures += expect_u32(buffers.diagnostics[2].location.line, 8U, ".IF diagnostic line should be preserved");
    failures += expect_u32(buffers.diagnostics[0].location.column, 7U, "STRUCT diagnostic column should be preserved");
    failures += expect_u32(buffers.diagnostics[1].location.column, 5U, "INVOKE diagnostic column should be preserved");
    failures += expect_u32(buffers.diagnostics[2].location.column, 5U, ".IF diagnostic column should be preserved");
    failures += expect_size(buffers.diagnostics[0].location.offset, 12U, "STRUCT diagnostic byte offset should be preserved");
    failures += expect_size(buffers.diagnostics[1].location.offset, 64U, "INVOKE diagnostic byte offset should be preserved");
    failures += expect_size(buffers.diagnostics[2].location.offset, 84U, ".IF diagnostic byte offset should be preserved");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 6U, "STRUCT diagnostic span length should be preserved");
    failures += expect_size(buffers.diagnostics[1].lexeme_length, 6U, "INVOKE diagnostic span length should be preserved");
    failures += expect_size(buffers.diagnostics[2].lexeme_length, 3U, ".IF diagnostic span length should be preserved");
    failures += expect_size(result.instruction_count, 0U, "instructions inside skipped unsupported blocks should not be emitted");

    return failures;
}

/// Verifies unsupported section recovery resumes at the following .code section.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_unsupported_section_recovery_resumes_at_code(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".FARDATA?\nx DWORD ?\n.code\nmain PROC\nmov eax, 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, ".FARDATA? should remain recoverable unsupported section");
    failures += expect_size(result.diagnostic_count, 1U, ".FARDATA? should produce one diagnostic");
    failures += expect_size(result.instruction_count, 1U, ".FARDATA? recovery should parse following code");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, ".FARDATA? diagnostic code should match");

    return failures;
}

/// Verifies implemented .DATA? and .CONST sections emit symbols and images.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_additional_data_sections_parse_successfully(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".DATA?\nbuf BYTE 16 DUP(?)\n.data\nx DWORD 1\n.CONST\nlimit DWORD 10\n.code\nmain PROC\nmov eax, SIZEOF buf\nmov ebx, limit\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, ".DATA? and .CONST should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "additional data sections should not produce diagnostics");
    failures += expect_size(result.symbol_count, 3U, "additional data sections should emit all symbols in source order");
    failures += expect_size(result.data_size, 20U, ".DATA? and .data should share writable data image ordering");
    failures += expect_size(result.const_size, 4U, ".CONST should emit a separate const image");
    failures += expect_u32(buffers.symbols[0].address, VM_MEMORY_DEFAULT_DATA_BASE, ".DATA? symbol should start at .data base");
    failures += expect_u32(buffers.symbols[1].address, VM_MEMORY_DEFAULT_DATA_BASE + 16U, ".data symbol should follow .DATA? bytes");
    failures += expect_u32(buffers.symbols[2].address, VM_MEMORY_DEFAULT_CONST_BASE, ".CONST symbol should start at .const base");
    failures += expect_u32((uint32_t)buffers.symbols[0].section, (uint32_t)VM_SYMBOL_SECTION_DATA_UNINITIALIZED, ".DATA? symbol metadata should preserve section");
    failures += expect_u32((uint32_t)buffers.symbols[2].section, (uint32_t)VM_SYMBOL_SECTION_CONST, ".CONST symbol metadata should preserve section");
    failures += expect_u32(buffers.data_image[0], 0U, ".DATA? bytes should be deterministic zero-filled");
    failures += expect_u32(buffers.const_image[0], 10U, ".CONST DWORD low byte should be initialized");

    return failures;
}

/// Verifies recovery skips unsupported block bodies without cascaded diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_unsupported_block_recovery_avoids_body_cascades(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    .IF eax == 0\n"
        "        badinstruction eax\n"
        "    .ENDIF\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unsupported .IF block should recover");
    failures += expect_size(result.diagnostic_count, 2U, ".IF body should not produce cascaded diagnostics beyond flow markers");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_IF, ".IF diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ENDIF, ".ENDIF diagnostic code should match");
    failures += expect_size(result.instruction_count, 0U, ".IF body instruction should not be emitted");

    return failures;
}

/// Verifies every Milestone 17 line-level unsupported construct can recover independently.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_line_level_unsupported_feature_recovery_covers_required_constructs(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    INVOKE SomeProc\n"
        "    SomeProc PROTO\n"
        "    Greeting TEXTEQU <Hello>\n"
        "    INCLUDELIB Irvine32.lib\n"
        "    EXTERN SomeProc:PROC\n"
        "    PUBLIC main\n"
        "    COMM shared:DWORD\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "all line-level unsupported constructs should recover");
    failures += expect_size(result.diagnostic_count, 7U, "all remaining line-level unsupported constructs should produce diagnostics");
    failures += expect_string_contains(buffers.diagnostics[0].message, "INVOKE", "line diagnostic should describe INVOKE");
    failures += expect_string_contains(buffers.diagnostics[1].message, "PROTO", "line diagnostic should describe PROTO");
    failures += expect_string_contains(buffers.diagnostics[2].message, "TEXTEQU", "line diagnostic should describe TEXTEQU");
    failures += expect_string_contains(buffers.diagnostics[3].message, "INCLUDELIB", "line diagnostic should describe INCLUDELIB");
    failures += expect_string_contains(buffers.diagnostics[4].message, "EXTERN", "line diagnostic should describe EXTERN");
    failures += expect_string_contains(buffers.diagnostics[5].message, "PUBLIC", "line diagnostic should describe PUBLIC");
    failures += expect_string_contains(buffers.diagnostics[6].message, "COMM", "line diagnostic should describe COMM");
    failures += expect_size(result.instruction_count, 0U, "unsupported line-level constructs should not emit instructions");

    return failures;
}

/// Verifies required Milestone 17 block terminators are recognized during recovery.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_block_recovery_covers_required_terminators(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        "MyUnion UNION\n"
        "    b BYTE ?\n"
        "MyUnion ENDS\n"
        "MyMacro MACRO\n"
        "    mov eax, 1\n"
        "ENDM\n"
        ".code\n"
        "main PROC\n"
        "    .WHILE ecx > 0\n"
        "        mov eax, 2\n"
        "    .ENDW\n"
        "    .REPEAT\n"
        "        mov ebx, 3\n"
        "    .UNTIL ebx == 3\n"
        "    .REPEAT\n"
        "        mov ecx, 4\n"
        "    .UNTILCXZ\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "required block constructs should recover");
    failures += expect_size(result.diagnostic_count, 8U, "UNION, MACRO, .WHILE/.ENDW, and both .REPEAT terminator pairs should produce diagnostics");
    failures += expect_string_contains(buffers.diagnostics[0].message, "UNION", "block diagnostic should describe UNION");
    failures += expect_string_contains(buffers.diagnostics[1].message, "macro", "block diagnostic should describe MACRO");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_WHILE, ".WHILE diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[3].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW, ".ENDW diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[4].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_REPEAT, "first .REPEAT diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[5].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW, ".UNTIL diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[6].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_REPEAT, "second .REPEAT diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[7].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW, ".UNTILCXZ diagnostic code should match");
    failures += expect_string_contains(buffers.diagnostics[2].message, ".WHILE", "block diagnostic should describe .WHILE");
    failures += expect_string_contains(buffers.diagnostics[3].message, ".ENDW", "block diagnostic should describe .ENDW");
    failures += expect_string_contains(buffers.diagnostics[4].message, ".REPEAT", "block diagnostic should describe .REPEAT .UNTIL");
    failures += expect_string_contains(buffers.diagnostics[5].message, ".UNTIL", "block diagnostic should describe .UNTIL");
    failures += expect_string_contains(buffers.diagnostics[6].message, ".REPEAT", "block diagnostic should describe .REPEAT .UNTILCXZ");
    failures += expect_string_contains(buffers.diagnostics[7].message, ".UNTILCXZ", "block diagnostic should describe .UNTILCXZ");
    failures += expect_size(result.instruction_count, 0U, "unsupported block bodies should not emit instructions");

    return failures;
}

/// Verifies an unterminated unsupported block remains non-executable and bounded.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_unterminated_unsupported_block_does_not_execute_body(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    .IF eax == 0\n"
        "        mov eax, 99\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unterminated unsupported block should still return diagnostics");
    failures += expect_size(result.diagnostic_count, 2U, "unterminated unsupported block should report unsupported feature and missing END");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_IF, "unterminated block first diagnostic should be unsupported high-level .IF");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_EXPECTED_END, "unterminated block should report missing END after recovery reaches EOF");
    failures += expect_size(result.instruction_count, 0U, "unterminated unsupported block body should not emit instructions");

    return failures;
}

/// Verifies diagnostic capacity remains fatal during recovery.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_recovery_diagnostic_capacity_failure_is_fatal(void) {
    ParserTestBuffers buffers;
    VmParserConfig config;
    VmParserResult result;
    int failures = 0;

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    memset(&result, 0, sizeof(result));
    config.source = ".code\nmain PROC\nINVOKE A\nPROTO B\nGreeting TEXTEQU <Hello>\nmain ENDP\nEND main\n";
    config.source_file = "main.asm";
    config.tokens = buffers.tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
    config.code_labels = buffers.code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = 2U;

    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED, "recovery diagnostic capacity exhaustion should be fatal");
    failures += expect_size(result.diagnostic_count, 2U, "diagnostic output should remain capped at configured capacity");

    return failures;
}

/// Verifies numeric immediates are checked against destination operand widths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_immediate_range_matches_destination_width(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov al, 255\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "AL should accept 255");
    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov al, 256\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "AL should reject 256");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "AL overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov bl, 9999\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "BL should reject 9999");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "BL overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov ax, 65535\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "AX should accept 65535");
    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov ax, 65536\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "AX should reject 65536");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "AX overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax, 4294967295\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "EAX should accept UINT32_MAX");

    return failures;
}

/// Verifies all byte and word register aliases reject overflowing immediates.
///
/// @return Zero on success, otherwise a positive failure count.
/// Verifies negative immediates are accepted only when they fit the destination signed range.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_negative_immediate_range_matches_destination_width(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov al, -1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "AL should accept -1");
    failures += expect_u32(buffers.instructions[0].source.immediate, 0xFFFFFFFFU, "-1 should be encoded as two's-complement IR immediate");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov al, -128\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "AL should accept -128");
    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov al, -129\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "AL should reject -129");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "AL negative overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov ax, -32768\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "AX should accept -32768");
    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov ax, -32769\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "AX should reject -32769");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "AX negative overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax, -2147483648\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "EAX should accept INT32_MIN");
    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax, -2147483649\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "EAX should reject below INT32_MIN");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "EAX negative IR overflow diagnostic should match");

    return failures;
}

static int test_immediate_range_covers_register_alias_families(void) {
    static const char *byte_registers[] = {"al", "ah", "bl", "bh", "cl", "ch", "dl", "dh"};
    static const char *word_registers[] = {"ax", "bx", "cx", "dx", "si", "di", "bp", "sp"};
    ParserTestBuffers buffers;
    VmParserResult result;
    char source[128];
    int failures = 0;

    for (size_t index = 0U; index < sizeof(byte_registers) / sizeof(byte_registers[0]); ++index) {
        (void)snprintf(source, sizeof(source), ".code\nmain PROC\nmov %s, 255\nmain ENDP\nEND main\n", byte_registers[index]);
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "8-bit register should accept 255");

        (void)snprintf(source, sizeof(source), ".code\nmain PROC\nmov %s, 256\nmain ENDP\nEND main\n", byte_registers[index]);
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "8-bit register should reject 256");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "8-bit register overflow diagnostic should match");

        (void)snprintf(source, sizeof(source), ".code\nmain PROC\nmov %s, 100h\nmain ENDP\nEND main\n", byte_registers[index]);
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "8-bit register should reject hex 100h");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "8-bit hex overflow diagnostic should match");
    }

    for (size_t index = 0U; index < sizeof(word_registers) / sizeof(word_registers[0]); ++index) {
        (void)snprintf(source, sizeof(source), ".code\nmain PROC\nmov %s, 65535\nmain ENDP\nEND main\n", word_registers[index]);
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "16-bit register should accept 65535");

        (void)snprintf(source, sizeof(source), ".code\nmain PROC\nmov %s, 65536\nmain ENDP\nEND main\n", word_registers[index]);
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "16-bit register should reject 65536");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "16-bit register overflow diagnostic should match");

        (void)snprintf(source, sizeof(source), ".code\nmain PROC\nmov %s, 10000h\nmain ENDP\nEND main\n", word_registers[index]);
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "16-bit register should reject hex 10000h");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "16-bit hex overflow diagnostic should match");
    }

    return failures;
}

/// Verifies parser capacity and invalid-argument handling.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_capacity_and_invalid_arguments(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserConfig config;
    VmParserResult result;
    const char *source = ".code\nmain PROC\nmov eax, 1\nmain ENDP\nEND main\n";

    failures += expect_parser_status(vm_parser_parse_program(NULL, &result), VM_PARSER_STATUS_INVALID_ARGUMENT, "NULL config should be rejected");
    memset(&config, 0, sizeof(config));
    config.source = source;
    failures += expect_parser_status(vm_parser_parse_program(&config, NULL), VM_PARSER_STATUS_INVALID_ARGUMENT, "NULL result should be rejected");

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    config.source = source;
    config.tokens = buffers.tokens;
    config.token_capacity = 4U;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
    config.code_labels = buffers.code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;
    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_LEXER_FAILED, "small token buffer should surface lexer failure");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LEXER_TOKEN_CAPACITY_EXCEEDED, "small token buffer should surface lexer diagnostic");

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    config.source = source;
    config.tokens = buffers.tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = 0U;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
    config.code_labels = buffers.code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;
    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_INSTRUCTION_CAPACITY_EXCEEDED, "small instruction buffer should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INSTRUCTION_CAPACITY_EXCEEDED, "instruction capacity diagnostic should match");

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    config.source = source;
    config.tokens = buffers.tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = 4U;
    config.code_labels = buffers.code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;
    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_SOURCE_TEXT_CAPACITY_EXCEEDED, "small source-text buffer should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_SOURCE_TEXT_CAPACITY_EXCEEDED, "source-text capacity diagnostic should match");

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    config.source = ".code\nmain PROC\nfoo eax\nmain ENDP\nEND main\n";
    config.tokens = buffers.tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
    config.code_labels = buffers.code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.diagnostics = NULL;
    config.diagnostic_capacity = 0U;
    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_DIAGNOSTIC_CAPACITY_EXCEEDED, "missing diagnostic capacity should be reported when needed");

    return failures;
}


/// Verifies lexer diagnostics are preserved as specific parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_lexer_diagnostics_are_preserved_by_parser(void) {
    typedef struct LexerDiagnosticCase {
        /// Source expected to fail before parsing because of a lexer diagnostic.
        const char *source;
        /// Parser diagnostic code expected after lexer diagnostic promotion.
        VmParserDiagnosticCode expected_code;
        /// Expected diagnostic line.
        uint32_t expected_line;
        /// Expected diagnostic column.
        uint32_t expected_column;
        /// Expected zero-based byte offset.
        size_t expected_offset;
        /// Expected source span length.
        size_t expected_span_length;
        /// Fragment expected in the user-facing message.
        const char *expected_message_fragment;
    } LexerDiagnosticCase;

    static const LexerDiagnosticCase cases[] = {
        {"#", VM_PARSER_DIAGNOSTIC_LEXER_UNEXPECTED_CHARACTER, 1U, 1U, 0U, 1U, "unexpected character"},
        {".code\nmain PROC\n    mov eax, 0xZZ\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_LEXER_INVALID_HEX_LITERAL, 3U, 14U, 29U, 2U, "hex literal"},
        {".data\nmsg BYTE \"Hello\n.code\nmain PROC\nEND main\n", VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_STRING, 2U, 10U, 15U, 6U, "unterminated string"},
        {"'A\n", VM_PARSER_DIAGNOSTIC_LEXER_UNTERMINATED_CHARACTER, 1U, 1U, 0U, 2U, "unterminated character"},
        {"184467440737095516160", VM_PARSER_DIAGNOSTIC_LEXER_NUMBER_OVERFLOW, 1U, 1U, 0U, 21U, "does not fit"},
        {"123abc", VM_PARSER_DIAGNOSTIC_LEXER_UNEXPECTED_CHARACTER, 1U, 1U, 0U, 6U, "invalid decimal literal suffix"}
    };
    int failures = 0;
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        ParserTestBuffers buffers;
        VmParserResult result;
        VmParserStatus status = parse_for_test(cases[index].source, &buffers, &result);

        failures += expect_parser_status(status, VM_PARSER_STATUS_LEXER_FAILED, "lexer diagnostic should stop parsing before execution");
        failures += expect_size(result.lexer_diagnostic_count, 1U, "lexer diagnostic count should be preserved in parser result");
        failures += expect_size(result.diagnostic_count, 1U, "one parser diagnostic should be produced from the lexer diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, cases[index].expected_code, "parser diagnostic code should preserve lexer reason");
        failures += expect_u32(buffers.diagnostics[0].location.line, cases[index].expected_line, "lexer diagnostic line should be preserved");
        failures += expect_u32(buffers.diagnostics[0].location.column, cases[index].expected_column, "lexer diagnostic column should be preserved");
        failures += expect_size(buffers.diagnostics[0].location.offset, cases[index].expected_offset, "lexer diagnostic byte offset should be preserved");
        failures += expect_size(buffers.diagnostics[0].lexeme_length, cases[index].expected_span_length, "lexer diagnostic source span length should be preserved");
        failures += expect_string_contains(buffers.diagnostics[0].message, cases[index].expected_message_fragment, "lexer diagnostic message should be preserved");
    }

    return failures;
}

/// Verifies fatal lexer capacity failures remain distinguishable from ordinary lexer diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_lexer_capacity_failure_is_distinct(void) {
    ParserTestBuffers buffers;
    VmParserConfig config;
    VmParserResult result;
    int failures = 0;

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    memset(&result, 0, sizeof(result));
    config.source = ".code\nmain PROC\n    mov eax, 1\nmain ENDP\nEND main\n";
    config.tokens = buffers.tokens;
    config.token_capacity = 1U;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
    config.code_labels = buffers.code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;

    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_LEXER_FAILED, "lexer token capacity should stop parsing");
    failures += expect_size(result.diagnostic_count, 1U, "lexer token capacity should produce one parser diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LEXER_TOKEN_CAPACITY_EXCEEDED, "lexer token capacity diagnostic should remain distinct");
    failures += expect_string_contains(buffers.diagnostics[0].message, "token buffer capacity", "lexer token capacity message should be preserved");

    return failures;
}

/// Verifies parser support for sign and zero extension opcodes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_extension_instructions_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "    movsx eax, al\n"
        "    movzx ebx, ax\n"
        "    cbw\n"
        "    cwde\n"
        "    cwd\n"
        "    cdq\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "extension instruction program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "extension instruction program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 6U, "extension instruction program should emit six instructions");
    failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_MOVSX, "first extension opcode should be MOVSX");
    failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_MOVZX, "second extension opcode should be MOVZX");
    failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_CBW, "third extension opcode should be CBW");
    failures += expect_u32(buffers.instructions[3].opcode, VM_IR_OPCODE_CWDE, "fourth extension opcode should be CWDE");
    failures += expect_u32(buffers.instructions[4].opcode, VM_IR_OPCODE_CWD, "fifth extension opcode should be CWD");
    failures += expect_u32(buffers.instructions[5].opcode, VM_IR_OPCODE_CDQ, "sixth extension opcode should be CDQ");
    failures += expect_u32(buffers.instructions[2].destination.kind, VM_IR_OPERAND_NONE, "CBW should emit no destination operand");
    failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_NONE, "CBW should emit no source operand");

    return failures;
}

/// Verifies parser diagnostics for malformed extension-instruction operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_extension_instruction_parse_error_paths(void) {
    int failures = 0;
    const char *same_width_source =
        ".code\n"
        "main PROC\n"
        "    movsx ax, bx\n"
        "main ENDP\n"
        "END main\n";
    const char *operand_on_cbw_source =
        ".code\n"
        "main PROC\n"
        "    cbw eax\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(same_width_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "same-width MOVSX should produce parser diagnostics");
    failures += expect_size(result.diagnostic_count, 1U, "same-width MOVSX should produce one diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "same-width MOVSX diagnostic should be operand width mismatch");
    failures += expect_string_contains(buffers.diagnostics[0].message, "wider than the source", "same-width MOVSX diagnostic should describe width rule");

    failures += expect_parser_status(parse_for_test(operand_on_cbw_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CBW with operand should produce parser diagnostics");
    failures += expect_size(result.diagnostic_count, 1U, "CBW with operand should produce one diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_LINE_END, "CBW with operand should expect line end");

    return failures;
}


/// Verifies XCHG, NEG, and NOP parse into expected IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase20_instructions_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "    xchg eax, ebx\n"
        "    xchg DWORD PTR [esi], eax\n"
        "    xchg bl, BYTE PTR [edi]\n"
        "    neg eax\n"
        "    neg BYTE PTR [edi]\n"
        "    nop\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 20 instruction program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 20 instruction program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 6U, "Phase 20 instruction program should emit six instructions");
    failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_XCHG, "first Phase 20 opcode should be XCHG");
    failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "xchg register destination should be register");
    failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_REGISTER, "xchg register source should be register");
    failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_XCHG, "second Phase 20 opcode should be XCHG");
    failures += expect_u32(buffers.instructions[1].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "xchg PTR [esi] destination should be register-indirect memory");
    failures += expect_u32(buffers.instructions[1].destination.width_bits, 32U, "xchg DWORD PTR [esi] should emit DWORD width");
    failures += expect_u32(buffers.instructions[2].source.width_bits, 8U, "xchg BYTE PTR [edi] source should emit BYTE width");
    failures += expect_u32(buffers.instructions[3].opcode, VM_IR_OPCODE_NEG, "fourth Phase 20 opcode should be NEG");
    failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_NONE, "NEG should emit no source operand");
    failures += expect_u32(buffers.instructions[4].destination.width_bits, 8U, "NEG BYTE PTR should emit 8-bit memory width");
    failures += expect_u32(buffers.instructions[5].opcode, VM_IR_OPCODE_NOP, "sixth Phase 20 opcode should be NOP");
    failures += expect_u32(buffers.instructions[5].destination.kind, VM_IR_OPERAND_NONE, "NOP should emit no destination operand");
    failures += expect_u32(buffers.instructions[5].source.kind, VM_IR_OPERAND_NONE, "NOP should emit no source operand");

    return failures;
}

/// Verifies parser diagnostics for malformed Phase 20 operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase20_instruction_parse_error_paths(void) {
    int failures = 0;
    const char *xchg_immediate_source =
        ".code\n"
        "main PROC\n"
        "    xchg eax, 1\n"
        "main ENDP\n"
        "END main\n";
    const char *xchg_width_mismatch_source =
        ".code\n"
        "main PROC\n"
        "    xchg eax, al\n"
        "main ENDP\n"
        "END main\n";
    const char *xchg_memory_memory_source =
        ".code\n"
        "main PROC\n"
        "    xchg DWORD PTR [esi], DWORD PTR [edi]\n"
        "main ENDP\n"
        "END main\n";
    const char *neg_extra_operand_source =
        ".code\n"
        "main PROC\n"
        "    neg eax, ebx\n"
        "main ENDP\n"
        "END main\n";
    const char *neg_ambiguous_memory_source =
        ".code\n"
        "main PROC\n"
        "    neg [esi]\n"
        "main ENDP\n"
        "END main\n";
    const char *nop_operand_source =
        ".code\n"
        "main PROC\n"
        "    nop al\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(xchg_immediate_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "XCHG immediate source should produce parser diagnostics");
    failures += expect_size(result.diagnostic_count, 1U, "XCHG immediate source should produce one diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "XCHG immediate diagnostic should be unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory", "XCHG immediate diagnostic should describe operand requirement");

    failures += expect_parser_status(parse_for_test(xchg_width_mismatch_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "XCHG width mismatch should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "XCHG width mismatch diagnostic should be operand width mismatch");
    failures += expect_string_contains(buffers.diagnostics[0].message, "widths must match", "XCHG width mismatch diagnostic should describe width rule");

    failures += expect_parser_status(parse_for_test(xchg_memory_memory_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "XCHG memory-memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "XCHG memory-memory diagnostic should be unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[0].message, "memory-to-memory", "XCHG memory-memory diagnostic should describe unsupported form");

    failures += expect_parser_status(parse_for_test(neg_extra_operand_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NEG extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "NEG extra operand diagnostic should be unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly one", "NEG extra operand diagnostic should describe operand count");

    failures += expect_parser_status(parse_for_test(neg_ambiguous_memory_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NEG ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "NEG ambiguous memory diagnostic should use stable ambiguous-memory-width code");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Memory operand width is ambiguous", "NEG ambiguous memory diagnostic should describe ambiguous width");

    failures += expect_parser_status(parse_for_test(nop_operand_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NOP operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "NOP 8-bit register diagnostic should be invalid operand size");
    failures += expect_string_contains(buffers.diagnostics[0].message, "no 8-bit encoding-operand form", "NOP operand diagnostic should describe valid register widths");

    return failures;
}

/// Verifies Phase 57N zero-operand NOP case-insensitive parsing and metadata.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57n_zero_operand_nop_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "    nop\n"
        "    NOP\n"
        "    NoP\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 57N NOP case variants should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 57N NOP case variants should not produce diagnostics");
    failures += expect_size(result.instruction_count, 3U, "Phase 57N NOP case variants should emit three instructions");
    failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_NOP, "lowercase nop should emit NOP opcode");
    failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_NOP, "uppercase NOP should emit NOP opcode");
    failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_NOP, "mixed-case NoP should emit NOP opcode");
    failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_NONE, "lowercase nop should emit no destination");
    failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_NONE, "uppercase NOP should emit no source");
    failures += expect_u32(buffers.instructions[2].source_line, 5U, "mixed-case NoP should preserve source line");
    failures += expect_string_contains(buffers.instructions[2].source_text, "NoP", "mixed-case NoP should preserve source text spelling");

    return failures;
}

/// Verifies Phase 57O accepts register and explicit-width NOP encoding operands as no-operand IR.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57o_nop_encoding_operand_parse_to_ir(void) {
    const char *source =
        ".data\n"
        "array DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    nop ax\n"
        "    nop bx\n"
        "    nop cx\n"
        "    nop dx\n"
        "    nop si\n"
        "    nop di\n"
        "    nop bp\n"
        "    nop sp\n"
        "    nop eax\n"
        "    nop ebx\n"
        "    nop ecx\n"
        "    nop edx\n"
        "    nop esi\n"
        "    nop edi\n"
        "    nop ebp\n"
        "    nop esp\n"
        "    nop WORD PTR [eax]\n"
        "    nop SWORD PTR [eax]\n"
        "    nop DWORD PTR [eax]\n"
        "    nop SDWORD PTR [eax]\n"
        "    NOP ax\n"
        "    NoP eax\n"
        "    NoP WORD PTR [esi]\n"
        "    nop DWORD PTR [esi]\n"
        "    nop WORD PTR [eax + 4]\n"
        "    nop DWORD PTR [eax + 4]\n"
        "    nop DWORD PTR [eax - 4]\n"
        "    nop DWORD PTR [array + esi]\n"
        "    nop DWORD PTR array[esi]\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    size_t index = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 57O NOP encoding operands should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 57O NOP encoding operands should not produce diagnostics");
    failures += expect_size(result.instruction_count, 29U, "Phase 57O NOP encoding operands should emit twenty-nine NOP instructions");
    for (index = 0U; index < result.instruction_count; index += 1U) {
        failures += expect_u32(buffers.instructions[index].opcode, VM_IR_OPCODE_NOP, "Phase 57O accepted encoding operand should emit NOP opcode");
        failures += expect_u32(buffers.instructions[index].destination.kind, VM_IR_OPERAND_NONE, "Phase 57O NOP encoding operand should not become destination operand");
        failures += expect_u32(buffers.instructions[index].source.kind, VM_IR_OPERAND_NONE, "Phase 57O NOP encoding operand should not become source operand");
    }

    return failures;
}

/// Verifies Phase 57O rejects unsupported NOP operand forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57o_nop_operand_rejections(void) {
    static const struct {
        const char *source;
        VmParserDiagnosticCode code;
        const char *message_fragment;
    } invalid_cases[] = {
        {".code\nmain PROC\n    nop al\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop ah\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop 1\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "does not accept an immediate"},
        {".code\nmain PROC\n    nop eax, ebx\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "at most one operand"},
        {".code\nmain PROC\n    nop [eax]\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "must have an explicit size"},
        {".code\nmain PROC\n    nop BYTE PTR [eax]\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop BYTE PTR [eax], 1\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop SBYTE PTR [eax]\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop QWORD PTR [eax]\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "QWORD/SQWORD are not supported"},
        {".code\nmain PROC\n    nop SQWORD PTR [eax]\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "QWORD/SQWORD are not supported"}
    };
    int failures = 0;
    size_t index = 0U;

    for (index = 0U; index < sizeof(invalid_cases) / sizeof(invalid_cases[0]); index += 1U) {
        ParserTestBuffers buffers;
        VmParserResult result;
        failures += expect_parser_status(parse_for_test(invalid_cases[index].source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57O invalid NOP form should produce parser diagnostics");
        failures += expect_size(result.diagnostic_count, 1U, "Phase 57O invalid NOP form should produce one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, invalid_cases[index].code, "Phase 57O invalid NOP diagnostic should use the expected code");
        failures += expect_string_contains(buffers.diagnostics[0].message, invalid_cases[index].message_fragment, "Phase 57O invalid NOP diagnostic should describe the specific invalid form");
    }

    return failures;
}


/// Verifies ADC, SBB, and carry-control instructions parse into expected IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase21_instructions_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    adc eax, 0\n"
        "    adc ebx, DWORD PTR [esi]\n"
        "    sbb DWORD PTR [esi], eax\n"
        "    clc\n"
        "    cmc\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 21 instruction program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 21 instruction program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 6U, "Phase 21 instruction program should emit six instructions");
    failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_STC, "first Phase 21 opcode should be STC");
    failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_NONE, "STC should emit no destination operand");
    failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_ADC, "second Phase 21 opcode should be ADC");
    failures += expect_u32(buffers.instructions[1].destination.kind, VM_IR_OPERAND_REGISTER, "ADC destination should be a register");
    failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_IMMEDIATE, "ADC source should be an immediate");
    failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_ADC, "third Phase 21 opcode should be ADC");
    failures += expect_u32(buffers.instructions[2].destination.kind, VM_IR_OPERAND_REGISTER, "ADC memory-source destination should be a register");
    failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "ADC memory-source operand should be register-indirect memory");
    failures += expect_u32(buffers.instructions[2].source.width_bits, 32U, "ADC DWORD PTR [esi] source should emit DWORD width");
    failures += expect_u32(buffers.instructions[3].opcode, VM_IR_OPCODE_SBB, "fourth Phase 21 opcode should be SBB");
    failures += expect_u32(buffers.instructions[3].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "SBB destination should be register-indirect memory");
    failures += expect_u32(buffers.instructions[3].destination.width_bits, 32U, "SBB DWORD PTR [esi] should emit DWORD width");
    failures += expect_u32(buffers.instructions[4].opcode, VM_IR_OPCODE_CLC, "fifth Phase 21 opcode should be CLC");
    failures += expect_u32(buffers.instructions[5].opcode, VM_IR_OPCODE_CMC, "sixth Phase 21 opcode should be CMC");

    return failures;
}

/// Verifies parser diagnostics for malformed Phase 21 operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase21_instruction_parse_error_paths(void) {
    int failures = 0;
    const char *adc_width_mismatch_source =
        ".code\n"
        "main PROC\n"
        "    adc eax, al\n"
        "main ENDP\n"
        "END main\n";
    const char *adc_immediate_overflow_source =
        ".code\n"
        "main PROC\n"
        "    adc al, 256\n"
        "main ENDP\n"
        "END main\n";
    const char *sbb_width_mismatch_source =
        ".code\n"
        "main PROC\n"
        "    sbb eax, BYTE PTR [esi]\n"
        "main ENDP\n"
        "END main\n";
    const char *clc_operand_source =
        ".code\n"
        "main PROC\n"
        "    clc eax\n"
        "main ENDP\n"
        "END main\n";
    const char *stc_operand_source =
        ".code\n"
        "main PROC\n"
        "    stc eax\n"
        "main ENDP\n"
        "END main\n";
    const char *cmc_operand_source =
        ".code\n"
        "main PROC\n"
        "    cmc eax\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(adc_width_mismatch_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ADC width mismatch should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "ADC width mismatch diagnostic should be operand width mismatch");
    failures += expect_string_contains(buffers.diagnostics[0].message, "does not match", "ADC width mismatch diagnostic should describe width rule");

    failures += expect_parser_status(parse_for_test(adc_immediate_overflow_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ADC immediate overflow should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "ADC immediate overflow diagnostic should be immediate out of range");

    failures += expect_parser_status(parse_for_test(sbb_width_mismatch_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SBB width mismatch should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "SBB width mismatch diagnostic should be operand width mismatch");

    failures += expect_parser_status(parse_for_test(clc_operand_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CLC operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "CLC operand diagnostic should be unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[0].message, "does not take operands", "CLC operand diagnostic should describe no-operand rule");

    failures += expect_parser_status(parse_for_test(stc_operand_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "STC operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "STC operand diagnostic should be unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[0].message, "does not take operands", "STC operand diagnostic should describe no-operand rule");

    failures += expect_parser_status(parse_for_test(cmc_operand_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMC operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "CMC operand diagnostic should be unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[0].message, "does not take operands", "CMC operand diagnostic should describe no-operand rule");

    return failures;
}


/// Verifies TEST instruction forms parse into expected IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase22_test_instruction_parses_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 0F0F0F0Fh\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET nums\n"
        "    test eax, eax\n"
        "    test eax, 0FFh\n"
        "    test eax, value\n"
        "    test value, eax\n"
        "    test value, 0FFh\n"
        "    test nums[0], 0FFh\n"
        "    test DWORD PTR [esi], 1\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "TEST instruction program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "TEST instruction program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 8U, "TEST instruction program should emit eight instructions");
    failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_TEST, "TEST reg/reg opcode should be emitted");
    failures += expect_u32(buffers.instructions[1].destination.kind, VM_IR_OPERAND_REGISTER, "TEST reg/reg destination should be register");
    failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_REGISTER, "TEST reg/reg source should be register");
    failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_TEST, "TEST reg/imm opcode should be emitted");
    failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_IMMEDIATE, "TEST reg/imm source should be immediate");
    failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "TEST reg/mem source should be direct memory");
    failures += expect_u32(buffers.instructions[4].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "TEST mem/reg destination should be direct memory");
    failures += expect_u32(buffers.instructions[5].destination.width_bits, 32U, "TEST direct symbol memory/immediate should infer DWORD width");
    failures += expect_u32(buffers.instructions[6].destination.width_bits, 32U, "TEST nums[0] memory/immediate should infer symbol width");
    failures += expect_u32(buffers.instructions[7].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "TEST PTR [esi] destination should be register-indirect memory");
    failures += expect_u32(buffers.instructions[7].destination.width_bits, 32U, "TEST DWORD PTR [esi] should emit DWORD width");

    return failures;
}

/// Verifies parser diagnostics for malformed TEST operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase22_test_instruction_parse_error_paths(void) {
    int failures = 0;
    const char *ambiguous_source =
        ".code\n"
        "main PROC\n"
        "    test [esi], 1\n"
        "main ENDP\n"
        "END main\n";
    const char *ambiguous_displacement_source =
        ".code\n"
        "main PROC\n"
        "    test [esi + 4], 1\n"
        "main ENDP\n"
        "END main\n";
    const char *width_mismatch_source =
        ".code\n"
        "main PROC\n"
        "    test eax, al\n"
        "main ENDP\n"
        "END main\n";
    const char *immediate_overflow_source =
        ".code\n"
        "main PROC\n"
        "    test al, 256\n"
        "main ENDP\n"
        "END main\n";
    const char *missing_comma_source =
        ".code\n"
        "main PROC\n"
        "    test eax eax\n"
        "main ENDP\n"
        "END main\n";
    const char *unsupported_base_source =
        ".code\n"
        "main PROC\n"
        "    test [ax], eax\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(ambiguous_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TEST [esi], imm should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "TEST [esi], imm diagnostic should be ambiguous memory width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 10U, "TEST [esi], imm diagnostic should point to the ambiguous memory operand");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Memory operand width is ambiguous", "TEST ambiguous diagnostic should describe width ambiguity");
    failures += expect_string_contains(buffers.diagnostics[0].message, "BYTE PTR", "TEST ambiguous diagnostic should suggest PTR widths");

    failures += expect_parser_status(parse_for_test(ambiguous_displacement_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TEST [esi + 4], imm should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "TEST [esi + 4], imm diagnostic should be ambiguous memory width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 10U, "TEST [esi + 4], imm diagnostic should point to the ambiguous memory operand");

    failures += expect_parser_status(parse_for_test(width_mismatch_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TEST width mismatch should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "TEST width mismatch diagnostic should be operand width mismatch");

    failures += expect_parser_status(parse_for_test(immediate_overflow_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TEST immediate overflow should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "TEST immediate overflow diagnostic should be immediate out of range");

    failures += expect_parser_status(parse_for_test(missing_comma_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TEST missing comma should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, "TEST missing comma diagnostic should be expected comma");

    failures += expect_parser_status(parse_for_test(unsupported_base_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TEST [ax], eax should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE, "TEST [ax], eax diagnostic should identify unsupported memory base register");
    failures += expect_u32(buffers.diagnostics[0].location.column, 11U, "TEST [ax], eax diagnostic should point to the unsupported base register");
    failures += expect_string_contains(buffers.diagnostics[0].message, "bracketed memory operands", "TEST unsupported base diagnostic should clarify bracketed memory use");
    failures += expect_string_contains(buffers.diagnostics[0].message, "remove the brackets", "TEST unsupported base diagnostic should clarify register operand use");

    return failures;
}

/// Verifies the shared Phase 26 memory-width resolver normalizes all current memory-capable instruction forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase25_global_memory_width_resolution_parses_to_ir(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    mov [eax], bl\n"
        "    add [eax], ebx\n"
        "    sub [eax], ax\n"
        "    adc [eax], al\n"
        "    sbb [eax], ebx\n"
        "    xchg [eax], cx\n"
        "    test [eax], eax\n"
        "    test [eax], ax\n"
        "    test [eax], al\n"
        "    test BYTE PTR [eax], 1\n"
        "    test WORD PTR [eax], 1\n"
        "    test DWORD PTR [eax], 1\n"
        "    mov ebx, [eax]\n"
        "    add ebx, [eax]\n"
        "    sub bx, [eax]\n"
        "    adc al, [eax]\n"
        "    sbb ebx, [eax]\n"
        "    xchg cx, [eax]\n"
        "    test eax, [eax]\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 26 register-supplied memory-width program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 26 register-supplied memory-width program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 20U, "Phase 26 program should emit twenty instructions");

    failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_MOV, "MOV [eax], bl opcode should be emitted");
    failures += expect_u32(buffers.instructions[1].destination.width_bits, 8U, "MOV [eax], bl should infer BYTE memory width from BL");
    failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_ADD, "ADD [eax], ebx opcode should be emitted");
    failures += expect_u32(buffers.instructions[2].destination.width_bits, 32U, "ADD [eax], ebx should infer DWORD memory width from EBX");
    failures += expect_u32(buffers.instructions[3].opcode, VM_IR_OPCODE_SUB, "SUB [eax], ax opcode should be emitted");
    failures += expect_u32(buffers.instructions[3].destination.width_bits, 16U, "SUB [eax], ax should infer WORD memory width from AX");
    failures += expect_u32(buffers.instructions[4].opcode, VM_IR_OPCODE_ADC, "ADC [eax], al opcode should be emitted");
    failures += expect_u32(buffers.instructions[4].destination.width_bits, 8U, "ADC [eax], al should infer BYTE memory width from AL");
    failures += expect_u32(buffers.instructions[5].opcode, VM_IR_OPCODE_SBB, "SBB [eax], ebx opcode should be emitted");
    failures += expect_u32(buffers.instructions[5].destination.width_bits, 32U, "SBB [eax], ebx should infer DWORD memory width from EBX");
    failures += expect_u32(buffers.instructions[6].opcode, VM_IR_OPCODE_XCHG, "XCHG [eax], cx opcode should be emitted");
    failures += expect_u32(buffers.instructions[6].destination.width_bits, 16U, "XCHG [eax], cx should infer WORD memory width from CX");
    failures += expect_u32(buffers.instructions[7].opcode, VM_IR_OPCODE_TEST, "TEST [eax], eax opcode should be emitted");
    failures += expect_u32(buffers.instructions[7].destination.width_bits, 32U, "TEST [eax], eax should infer DWORD memory width from EAX");
    failures += expect_u32(buffers.instructions[8].destination.width_bits, 16U, "TEST [eax], ax should infer WORD memory width from AX");
    failures += expect_u32(buffers.instructions[9].destination.width_bits, 8U, "TEST [eax], al should infer BYTE memory width from AL");
    failures += expect_u32(buffers.instructions[10].destination.width_bits, 8U, "TEST BYTE PTR [eax], 1 should preserve explicit BYTE width");
    failures += expect_u32(buffers.instructions[11].destination.width_bits, 16U, "TEST WORD PTR [eax], 1 should preserve explicit WORD width");
    failures += expect_u32(buffers.instructions[12].destination.width_bits, 32U, "TEST DWORD PTR [eax], 1 should preserve explicit DWORD width");
    failures += expect_u32(buffers.instructions[13].opcode, VM_IR_OPCODE_MOV, "MOV ebx, [eax] opcode should be emitted");
    failures += expect_u32(buffers.instructions[13].source.width_bits, 32U, "MOV ebx, [eax] should infer DWORD memory width from EBX");
    failures += expect_u32(buffers.instructions[14].opcode, VM_IR_OPCODE_ADD, "ADD ebx, [eax] opcode should be emitted");
    failures += expect_u32(buffers.instructions[14].source.width_bits, 32U, "ADD ebx, [eax] should infer DWORD memory width from EBX");
    failures += expect_u32(buffers.instructions[15].opcode, VM_IR_OPCODE_SUB, "SUB bx, [eax] opcode should be emitted");
    failures += expect_u32(buffers.instructions[15].source.width_bits, 16U, "SUB bx, [eax] should infer WORD memory width from BX");
    failures += expect_u32(buffers.instructions[16].opcode, VM_IR_OPCODE_ADC, "ADC al, [eax] opcode should be emitted");
    failures += expect_u32(buffers.instructions[16].source.width_bits, 8U, "ADC al, [eax] should infer BYTE memory width from AL");
    failures += expect_u32(buffers.instructions[17].opcode, VM_IR_OPCODE_SBB, "SBB ebx, [eax] opcode should be emitted");
    failures += expect_u32(buffers.instructions[17].source.width_bits, 32U, "SBB ebx, [eax] should infer DWORD memory width from EBX");
    failures += expect_u32(buffers.instructions[18].opcode, VM_IR_OPCODE_XCHG, "XCHG cx, [eax] opcode should be emitted");
    failures += expect_u32(buffers.instructions[18].source.width_bits, 16U, "XCHG cx, [eax] should infer WORD memory width from CX");
    failures += expect_u32(buffers.instructions[19].opcode, VM_IR_OPCODE_TEST, "TEST eax, [eax] opcode should be emitted");
    failures += expect_u32(buffers.instructions[19].source.width_bits, 32U, "TEST eax, [eax] should infer DWORD memory width from EAX");

    return failures;
}

/// Verifies symbol-relative metadata is not overridden by a same-instruction register operand.
///
/// Phase 26 allows register operands to supply width only for untyped memory
/// operands. Symbol-relative operands already carry declaration metadata, so a
/// BYTE symbol reference paired with DX remains a width mismatch.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase25_symbol_metadata_width_precedence(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "buf BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov ecx, 2\n"
        "    mov [buf + ecx], dx\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Symbol-relative BYTE memory paired with DX should fail parsing");
    failures += expect_size(result.diagnostic_count, 1U, "Symbol-relative width mismatch should produce one diagnostic");
    if (result.diagnostic_count > 0U) {
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "Symbol-relative BYTE memory should not let DX override symbol metadata");
        failures += expect_string_contains(buffers.diagnostics[0].message, "Source operand width does not match", "Symbol metadata precedence diagnostic should describe source width mismatch");
    }

    return failures;
}

/// Verifies explicit PTR overrides symbol metadata for register-relative symbol operands.
///
/// Phase 26 resolves width from explicit PTR first. A BYTE-declared symbol can
/// therefore be intentionally accessed as a WORD when the source register width
/// matches the explicit override.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase25_explicit_ptr_overrides_symbol_metadata(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "buf BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov ecx, 2\n"
        "    mov dx, 1234h\n"
        "    mov WORD PTR [buf + ecx], dx\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Explicit WORD PTR should override BYTE symbol metadata when paired with DX");
    failures += expect_size(result.diagnostic_count, 0U, "Explicit PTR override should not produce width diagnostics");
    failures += expect_size(result.instruction_count, 3U, "Explicit PTR override program should emit three instructions");
    if (result.instruction_count >= 3U) {
        failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_MOV, "Explicit PTR override should emit MOV");
        failures += expect_u32(buffers.instructions[2].destination.width_bits, 16U, "WORD PTR [buf + ecx] should resolve to a 16-bit memory destination");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_REGISTER, "Explicit PTR override source should remain a register");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_DX, "Explicit PTR override source should remain DX");
    }

    return failures;
}

/// Verifies ambiguous memory-width diagnostics are stable for all current memory-capable instructions.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase25_global_memory_width_resolution_error_paths(void) {
    int failures = 0;
    const char *sources[] = {
        ".code\nmain PROC\n    mov [eax], 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    add [eax], 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    sub [eax], 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    adc [eax], 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    sbb [eax], 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    test [eax], 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    test [eax + 4], 1\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    neg [eax]\nmain ENDP\nEND main\n"
    };
    const char *names[] = {
        "MOV [eax], imm",
        "ADD [eax], imm",
        "SUB [eax], imm",
        "ADC [eax], imm",
        "SBB [eax], imm",
        "TEST [eax], imm",
        "TEST [eax + 4], imm",
        "NEG [eax]"
    };
    size_t index = 0U;

    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); index += 1U) {
        ParserTestBuffers buffers;
        VmParserResult result;
        VmParserStatus status = parse_for_test(sources[index], &buffers, &result);

        failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, names[index]);
        failures += expect_size(result.diagnostic_count, 1U, "Ambiguous memory-width program should emit one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "Ambiguous memory-width diagnostic should use stable code");
        failures += expect_string_contains(buffers.diagnostics[0].message, "Memory operand width is ambiguous", "Ambiguous memory-width diagnostic should describe ambiguity");
        failures += expect_string_contains(buffers.diagnostics[0].message, "BYTE PTR", "Ambiguous memory-width diagnostic should suggest PTR widths");
    }

    return failures;
}

/// Verifies MASM32 textbook header directives are accepted before source sections.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase26_header_directives_parse_before_sections(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *source =
        ".386\n"
        ".486\n"
        ".586\n"
        ".686\n"
        ".model flat, stdcall\n"
        ".stack 1000h\n"
        "OPTION CASEMAP:NONE\n"
        "INCLUDE Irvine32.inc\n"
        "INCLUDE Macros.inc\n"
        "TITLE Example Program\n"
        "SUBTITLE Header compatibility\n"
        "PAGE 60, 132\n"
        ".data\n"
        "msg BYTE \"Hello\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, OFFSET msg\n"
        "main ENDP\n"
        "END main\n";
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 26 headers should parse before .data/.code with Phase 53D notices");
    failures += expect_size(result.diagnostic_count, 10U, "Phase 53D notice-producing accepted headers should emit ten notices");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP, "Processor directive notice should use compatibility-no-op");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[4].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED, ".model notice should use compatibility-limited");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[5].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_METADATA_ONLY, ".stack notice should use compatibility-metadata-only");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[6].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED, "Macros.inc notice should use compatibility-limited");
    failures += expect_parser_diagnostic_severity(buffers.diagnostics[0].severity, VM_PARSER_DIAGNOSTIC_SEVERITY_NOTICE, "Compatibility diagnostics should be notices");
    failures += expect_size(result.instruction_count, 1U, "Phase 26 header sample should emit one instruction");
    failures += expect_size(result.symbol_count, 1U, "Phase 26 header sample should retain data symbols");
    if (!result.has_requested_stack_size) {
        failures += record_failure(".stack size should be stored as parser metadata");
    }
    failures += expect_u32(result.requested_stack_size, 0x1000U, ".stack 1000h should store requested stack size metadata");

    return failures;
}

/// Verifies no-size and mixed-case header compatibility forms are accepted.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase26_header_directive_edge_cases(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *source =
        ".386\n"
        ".MODEL FLAT, STDCALL\n"
        ".stack\n"
        "option casemap:none\n"
        "include irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n";
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Mixed-case Phase 26 headers should parse with Phase 53D notices");
    failures += expect_size(result.diagnostic_count, 3U, "Mixed-case notice-producing headers should produce three notices");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP, "Mixed-case processor directive notice should use compatibility-no-op");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED, "Mixed-case .model notice should use compatibility-limited");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_METADATA_ONLY, "Mixed-case .stack notice should use compatibility-metadata-only");
    if (result.has_requested_stack_size) {
        failures += record_failure(".stack without size should not request a runtime stack size yet");
    }

    return failures;
}

/// Verifies unsupported Phase 26 header forms produce structured diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase26_header_directive_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(".model small, c\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Unsupported .model form should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MODEL, "Unsupported .model diagnostic code should match");
    failures += expect_size(buffers.diagnostics[0].location.line, 1U, "Unsupported .model diagnostic should preserve line");
    failures += expect_size(buffers.diagnostics[0].location.column, 1U, "Unsupported .model diagnostic should preserve column");

    failures += expect_parser_status(parse_for_test("INCLUDE Windows.inc\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Unsupported include should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDE, "Unsupported include diagnostic code should match");
    failures += expect_size(buffers.diagnostics[0].location.column, 9U, "Unsupported include diagnostic should point at the include path");

    failures += expect_parser_status(parse_for_test("OPTION CASEMAP:NOTPUBLIC\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Unsupported OPTION CASEMAP:NOTPUBLIC should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION, "Unsupported OPTION diagnostic code should match");

    failures += expect_parser_status(parse_for_test("OPTION CASEMAP:BOGUS\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Invalid OPTION CASEMAP value should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_OPTION_VALUE, "Invalid CASEMAP value diagnostic code should match");

    failures += expect_parser_status(parse_for_test(".stack -1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Invalid .stack size should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "Invalid .stack diagnostic code should match");

    failures += expect_parser_status(parse_for_test("ASSUME cs:code\n.STARTUP\n.LIST\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Unsupported directive families should be recovered together");
    failures += expect_size(result.diagnostic_count, 3U, "Unsupported directive families should report three diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "ASSUME should remain an unsupported-feature diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, ".STARTUP should remain an unsupported-feature diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, ".LIST should remain an unsupported-feature diagnostic");

    return failures;
}


/// Verifies Phase 53D compatibility notice diagnostics for accepted header constructs.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase53d_compatibility_notice_parser_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *source =
        ".686\n"
        ".model flat, stdcall\n"
        ".stack 4096\n"
        "INCLUDE Macros.inc\n"
        "TITLE Notice Sample\n"
        "SUBTITLE Notice Details\n"
        "PAGE 60, 132\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n";
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 53D accepted compatibility constructs should parse with notices");
    failures += expect_size(result.diagnostic_count, 7U, "Phase 53D sample should emit one notice per notice-producing construct");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP, ".686 should emit compatibility-no-op");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED, ".model should emit compatibility-limited");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_METADATA_ONLY, ".stack size should emit compatibility-metadata-only");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[3].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED, "Macros.inc should emit compatibility-limited");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[4].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP, "TITLE should emit compatibility-no-op");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[5].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP, "SUBTITLE should emit compatibility-no-op");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[6].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP, "PAGE should emit compatibility-no-op");
    failures += expect_parser_diagnostic_severity(buffers.diagnostics[0].severity, VM_PARSER_DIAGNOSTIC_SEVERITY_NOTICE, "Compatibility diagnostics should be informational notices");
    failures += expect_size(buffers.diagnostics[0].location.line, 1U, ".686 notice should preserve line");
    failures += expect_size(buffers.diagnostics[0].location.column, 1U, ".686 notice should preserve column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 0U, ".686 notice should preserve byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 4U, ".686 notice should preserve source span length");
    failures += expect_string_contains(buffers.diagnostics[0].message, ".686 is accepted", "Processor notice should name the accepted directive");
    failures += expect_string_contains(buffers.diagnostics[4].message, "TITLE is accepted", "Listing notice should name TITLE");
    failures += expect_string_contains(buffers.diagnostics[5].message, "SUBTITLE is accepted", "Listing notice should name SUBTITLE");
    failures += expect_string_contains(buffers.diagnostics[6].message, "PAGE is accepted", "Listing notice should name PAGE");
    failures += expect_string_contains(buffers.diagnostics[2].message, "source-level PUSH/POP stack transfers", ".stack notice should describe Phase 72A stack-transfer support");
    failures += expect_string_contains(buffers.diagnostics[3].message, "macro expansion", "Macros.inc notice should describe macro expansion limitation");

    return failures;
}

/// Verifies active semantic constructs do not receive generic Phase 53D no-op notices.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase53d_active_semantics_do_not_emit_noop_notices(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *irvine_source =
        "INCLUDE Irvine32.inc\n"
        "OPTION CASEMAP:ALL\n"
        ".DATA?\n"
        "buf DWORD ?\n"
        ".CONST\n"
        "limit DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n";
    const char *casemap_none_source =
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "Value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, Value\n"
        "main ENDP\n"
        "END main\n";

    failures += expect_parser_status(parse_for_test(irvine_source, &buffers, &result), VM_PARSER_STATUS_OK, "Active semantic constructs should parse without generic compatibility notices");
    failures += expect_size(result.diagnostic_count, 0U, "Irvine32 include, CASEMAP:ALL, .DATA?, .CONST, .code, PROC, ENDP, and END should not emit generic notices");
    if (!result.has_irvine32_virtual_include) {
        failures += record_failure("Irvine32 include should still populate virtual include metadata");
    }

    failures += expect_parser_status(parse_for_test(casemap_none_source, &buffers, &result), VM_PARSER_STATUS_OK, "CASEMAP:NONE active semantics should parse without generic compatibility notices");
    failures += expect_size(result.diagnostic_count, 0U, "OPTION CASEMAP:NONE should not emit a generic compatibility notice");

    return failures;
}

/// Verifies broader Phase 26 directive backlog forms produce unsupported-feature diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase26_broader_directive_backlog_diagnostics(void) {
    static const char *const sources[] = {
        "IFDEF DEBUG\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "ELSEIF 1\n.code\nmain PROC\nmain ENDP\nEND main\n",
        ".ERRNZ 1\n.code\nmain PROC\nmain ENDP\nEND main\n",
        ".387\n.code\nmain PROC\nmain ENDP\nEND main\n",
        ".MMX\n.code\nmain PROC\nmain ENDP\nEND main\n",
        ".SAFESEH handler\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "EXITM <0>\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "FOR item, <1,2>\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "PUSHCONTEXT\n.code\nmain PROC\nmain ENDP\nEND main\n"
    };
    static const char *const names[] = {
        "IFDEF conditional assembly should be unsupported-feature",
        "ELSEIF conditional assembly should be unsupported-feature",
        ".ERRNZ conditional error directive should be unsupported-feature",
        ".387 processor directive should be unsupported-feature",
        ".MMX processor directive should be unsupported-feature",
        ".SAFESEH object directive should be unsupported-feature",
        "EXITM macro directive should be unsupported-feature",
        "FOR macro repeat directive should be unsupported-feature",
        "PUSHCONTEXT assembler context directive should be unsupported-feature"
    };
    int failures = 0;
    size_t index = 0U;

    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); index += 1U) {
        ParserTestBuffers buffers;
        VmParserResult result;
        VmParserStatus status = parse_for_test(sources[index], &buffers, &result);

        failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, names[index]);
        failures += expect_size(result.diagnostic_count, 1U, "Broader Phase 26 directive backlog source should emit one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "Broader Phase 26 directive backlog diagnostic should use unsupported-feature");
        failures += expect_size(buffers.diagnostics[0].location.line, 1U, "Broader Phase 26 directive backlog diagnostic should preserve line");
        failures += expect_size(buffers.diagnostics[0].location.column, 1U, "Broader Phase 26 directive backlog diagnostic should preserve column");
        failures += expect_string_contains(buffers.diagnostics[0].message, "Unsupported feature", "Broader Phase 26 directive backlog diagnostic should explain unsupported feature");
    }

    return failures;
}


/// Verifies Phase 35A parser CASEMAP policy diagnostics and symbol insertion rules.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase35a_casemap_parser_policy(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".DATA?\n"
        "buf DWORD ?\n"
        "bUF DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Default CASEMAP:ALL should reject duplicate data symbols that differ only by case");
    failures += expect_size(result.symbol_count, 1U, "Rejected folded duplicate data symbol should not be inserted");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_SYMBOL, "Default folded duplicate should use duplicate-symbol");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "Duplicate-by-case diagnostic should point at second declaration line");
    failures += expect_size(buffers.diagnostics[0].location.column, 1U, "Duplicate-by-case diagnostic should point at second declaration column");
    if (buffers.diagnostics[0].severity != VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR) {
        failures += record_failure("Duplicate-by-case diagnostic should be an assembly error");
    }

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "buf DWORD 1\n"
        "bUF DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "CASEMAP:NONE should allow case-distinct data symbols");
    failures += expect_size(result.symbol_count, 2U, "CASEMAP:NONE should insert both exact-case data symbols");
    failures += expect_size(result.diagnostic_count, 0U, "CASEMAP:NONE case-distinct data symbols should not warn or error");

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:ALL\n"
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "buf DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Changing supported CASEMAP policy should produce a warning diagnostic");
    failures += expect_size(result.diagnostic_count, 1U, "Single supported CASEMAP policy change should emit one warning");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED, "Supported CASEMAP policy change should use warning code");
    if (buffers.diagnostics[0].severity != VM_PARSER_DIAGNOSTIC_SEVERITY_WARNING) {
        failures += record_failure("Supported CASEMAP policy change should be non-fatal warning severity");
    }
    if (vm_parser_diagnostic_is_error(&buffers.diagnostics[0])) {
        failures += record_failure("Supported CASEMAP policy change should not be fatal to source execution");
    }

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "buf DWORD 1\n"
        "bUF DWORD 2\n"
        "OPTION CASEMAP:ALL\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, buf\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:ALL folded lookup of exact-case duplicates should be ambiguous");
    failures += expect_size(result.diagnostic_count, 2U, "Ambiguous folded lookup should preserve policy warning and ambiguity error");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED, "Ambiguous folded lookup fixture should first warn about policy change");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL, "Ambiguous folded lookup should use ambiguous-symbol");
    if (buffers.diagnostics[1].severity != VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR) {
        failures += record_failure("Ambiguous folded lookup should be an assembly error");
    }

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NOTPUBLIC\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:NOTPUBLIC should be recognized but unsupported");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION, "CASEMAP:NOTPUBLIC should use unsupported-option");
    failures += expect_string_contains(buffers.diagnostics[0].message, "public/external linkage", "CASEMAP:NOTPUBLIC diagnostic should explain linkage dependency");

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:LOWER\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Invalid CASEMAP value should be rejected clearly");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_OPTION_VALUE, "Invalid CASEMAP value should use invalid-option-value");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Supported CASEMAP values: ALL, NONE", "Invalid CASEMAP diagnostic should list supported values");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "Main PROC\n"
        "mAIN ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Default CASEMAP:ALL should allow folded ENDP and END procedure-name lookup");
    failures += expect_size(result.diagnostic_count, 0U, "Default folded ENDP and END targets should not emit diagnostics");

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n"
        ".code\n"
        "Main PROC\n"
        "Main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:NONE should require exact END procedure-name spelling");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_END_TARGET, "CASEMAP:NONE mismatched END target should keep existing invalid-end-target diagnostic");
    failures += expect_size(buffers.diagnostics[0].location.line, 5U, "CASEMAP:NONE END diagnostic should point at END target line");
    failures += expect_size(buffers.diagnostics[0].location.column, 5U, "CASEMAP:NONE END diagnostic should point at entry-point token");
    if (buffers.diagnostics[0].severity != VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR) {
        failures += record_failure("CASEMAP:NONE mismatched END target should be an assembly error");
    }

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n"
        ".code\n"
        "Main PROC\n"
        "main ENDP\n"
        "END Main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:NONE should require exact ENDP procedure-name spelling");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_PROC_END_MISMATCH, "CASEMAP:NONE mismatched ENDP name should use proc-end-mismatch diagnostic");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "CASEMAP:NONE ENDP diagnostic should point at ENDP line");
    failures += expect_size(buffers.diagnostics[0].location.column, 1U, "CASEMAP:NONE ENDP diagnostic should point at ENDP procedure token");
    failures += expect_string_contains(buffers.diagnostics[0].message, "ENDP procedure name", "CASEMAP:NONE ENDP diagnostic should explain procedure-name mismatch");

    return failures;
}


/// Verifies Phase 75 keeps bare PROC metadata and adds targeted PROC declaration diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase75_proc_metadata_and_attribute_diagnostics(void) {
    static const char *const unsupported_sources[] = {
        ".code\nMyProc PROC arg1:DWORD\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC C\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC STDCALL\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC PUBLIC\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC PRIVATE\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC EXPORT\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC FRAME\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC NEAR\nMyProc ENDP\nEND MyProc\n",
        ".code\nMyProc PROC FAR\nMyProc ENDP\nEND MyProc\n"
    };
    int failures = 0;
    size_t index = 0U;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    mov eax, 75\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 75 bare PROC metadata fixture should parse");
    failures += expect_size(result.procedure_range_count, 1U, "Phase 75 bare PROC fixture should keep one procedure range");
    failures += expect_string(buffers.procedure_ranges[0].name, "main", "Phase 75 procedure metadata should preserve source name");
    failures += expect_size(buffers.procedure_ranges[0].source_location.line, 2U, "Phase 75 procedure declaration line should be preserved");
    failures += expect_size(buffers.procedure_ranges[0].source_location.column, 1U, "Phase 75 procedure declaration column should be preserved");
    failures += expect_size(buffers.procedure_ranges[0].start_instruction_index, 0U, "Phase 75 procedure body start should be preserved");
    failures += expect_size(buffers.procedure_ranges[0].end_instruction_index, 1U, "Phase 75 procedure body boundary should be preserved");
    failures += expect_bool(buffers.procedure_ranges[0].has_executable_instruction, "Phase 75 procedure metadata should retain executable-body flag");
    failures += expect_size(buffers.procedure_ranges[0].unsupported_attribute_count, 0U, "accepted bare PROC should not record unsupported attributes");

    for (index = 0U; index < sizeof(unsupported_sources) / sizeof(unsupported_sources[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(unsupported_sources[index], &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unsupported PROC tail should be rejected with diagnostics");
        failures += expect_size(result.diagnostic_count, 1U, "unsupported PROC tail should emit one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PROC_ATTRIBUTE, "unsupported PROC tail should use unsupported-proc-attribute");
        failures += expect_size(buffers.diagnostics[0].location.line, 2U, "unsupported PROC attribute diagnostic should point at PROC tail line");
        failures += expect_size(buffers.diagnostics[0].location.column, 13U, "unsupported PROC attribute diagnostic should point at PROC tail token");
        failures += expect_string_contains(buffers.diagnostics[0].message, "PROC attribute or parameter", "unsupported PROC diagnostic should explain PROC tail rejection");
        failures += expect_size(result.procedure_range_count, 0U, "rejected PROC declaration should not publish procedure metadata");
    }

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "MyProc PROC ,\n"
        "MyProc ENDP\n"
        "END MyProc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "malformed PROC declaration should be rejected");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_PROC_DECLARATION, "malformed PROC tail should use invalid-proc-declaration");
    failures += expect_size(buffers.diagnostics[0].location.line, 2U, "invalid PROC declaration line should be preserved");
    failures += expect_size(buffers.diagnostics[0].location.column, 13U, "invalid PROC declaration column should be preserved");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 1U, "invalid PROC declaration span should cover malformed token");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "MyProc PROC\n"
        "    mov eax, 1\n"
        "Other ENDP\n"
        "END MyProc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "mismatched ENDP name should be rejected");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_PROC_END_MISMATCH, "mismatched ENDP should use proc-end-mismatch");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "mismatched ENDP diagnostic should point at ENDP name line");
    failures += expect_size(buffers.diagnostics[0].location.column, 1U, "mismatched ENDP diagnostic should point at ENDP name column");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 5U, "mismatched ENDP diagnostic should span mismatched name");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "Helper PROC\n"
        "Helper ENDP\n"
        "Helper PROC\n"
        "Helper ENDP\n"
        "END Helper\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "duplicate procedure should be rejected");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_PROCEDURE, "duplicate procedure should use duplicate-procedure");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "duplicate procedure diagnostic should point at second declaration");
    failures += expect_size(buffers.diagnostics[0].related_location.line, 2U, "duplicate procedure diagnostic should carry prior declaration line");

    return failures;
}


/// Verifies Phase 76 accepts PROC USES metadata and rejects invalid USES lists.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase76_proc_uses_parsing_and_metadata(void) {
    typedef struct InvalidUsesCase {
        const char *source;
        VmParserDiagnosticCode code;
        uint32_t column;
        size_t span_length;
        const char *message_fragment;
    } InvalidUsesCase;
    static const InvalidUsesCase invalid_cases[] = {
        {".code\nMyProc PROC USES\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_EXPECTED_PROC_USES_REGISTER, 13U, 4U, "requires at least one register"},
        {".code\nMyProc PROC USES esp\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER, 18U, 3U, "accepted PROC USES registers"},
        {".code\nMyProc PROC USES ebp\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER, 18U, 3U, "ESP, EBP"},
        {".code\nMyProc PROC USES ax\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER, 18U, 2U, "16-bit aliases"},
        {".code\nMyProc PROC USES al\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER, 18U, 2U, "8-bit aliases"},
        {".code\nMyProc PROC USES unknownReg\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER, 18U, 10U, "unknown names"},
        {".code\nMyProc PROC USES eax EAX\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_DUPLICATE_PROC_USES_REGISTER, 22U, 3U, "Duplicate PROC USES register"},
        {".code\nMyProc PROC USES eax, ebx\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER, 21U, 1U, "punctuation"},
        {".code\nMyProc PROC USES eax C\nMyProc ENDP\nEND MyProc\n", VM_PARSER_DIAGNOSTIC_INVALID_PROC_USES_REGISTER, 22U, 1U, "Invalid PROC USES register"}
    };
    int failures = 0;
    size_t index = 0U;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "MyProc PROC USES eax ebx ecx\n"
        "    mov eax, 1\n"
        "MyProc ENDP\n"
        "END MyProc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 76 PROC USES register list should parse");
    failures += expect_size(result.diagnostic_count, 0U, "valid PROC USES fixture should emit no diagnostics");
    failures += expect_size(result.procedure_range_count, 1U, "valid PROC USES should publish procedure metadata");
    failures += expect_size(buffers.procedure_ranges[0].uses_register_count, 3U, "valid PROC USES should record three registers");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[0], (uint32_t)VM_REGISTER_EAX, "PROC USES should preserve first declared register");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[1], (uint32_t)VM_REGISTER_EBX, "PROC USES should preserve second declared register");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[2], (uint32_t)VM_REGISTER_ECX, "PROC USES should preserve third declared register");

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n"
        ".code\n"
        "MyProc PROC USES EAX ebx ESI EDI\n"
        "MyProc ENDP\n"
        "END MyProc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "PROC USES register recognition should be case-insensitive under CASEMAP:NONE");
    failures += expect_size(buffers.procedure_ranges[0].uses_register_count, 4U, "mixed-case PROC USES should record four registers");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[0], (uint32_t)VM_REGISTER_EAX, "mixed-case PROC USES should canonicalize EAX");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[1], (uint32_t)VM_REGISTER_EBX, "mixed-case PROC USES should canonicalize EBX");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[2], (uint32_t)VM_REGISTER_ESI, "mixed-case PROC USES should canonicalize ESI");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[3], (uint32_t)VM_REGISTER_EDI, "mixed-case PROC USES should canonicalize EDI");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "Bare PROC\n"
        "Bare ENDP\n"
        "END Bare\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "bare PROC should remain accepted after Phase 76");
    failures += expect_size(buffers.procedure_ranges[0].uses_register_count, 0U, "bare PROC should have no USES metadata");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "MyProc PROC USES eax ebx ecx edx esi edi\n"
        "MyProc ENDP\n"
        "END MyProc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "maximum Phase 76 PROC USES register list should parse");
    failures += expect_size(buffers.procedure_ranges[0].uses_register_count, 6U, "maximum PROC USES list should record all six accepted registers");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].uses_registers[5], (uint32_t)VM_REGISTER_EDI, "maximum PROC USES list should preserve last register");

    for (index = 0U; index < sizeof(invalid_cases) / sizeof(invalid_cases[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(invalid_cases[index].source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "invalid PROC USES fixture should be rejected");
        failures += expect_size(result.diagnostic_count, 1U, "invalid PROC USES fixture should emit one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, invalid_cases[index].code, "invalid PROC USES fixture should use targeted diagnostic code");
        failures += expect_size(buffers.diagnostics[0].location.line, 2U, "invalid PROC USES diagnostic should point at PROC line");
        failures += expect_size(buffers.diagnostics[0].location.column, invalid_cases[index].column, "invalid PROC USES diagnostic should point at offending token");
        failures += expect_size(buffers.diagnostics[0].lexeme_length, invalid_cases[index].span_length, "invalid PROC USES diagnostic should preserve offending token span");
        failures += expect_string_contains(buffers.diagnostics[0].message, invalid_cases[index].message_fragment, "invalid PROC USES diagnostic should explain rejection");
        failures += expect_size(result.procedure_range_count, 0U, "rejected PROC USES declaration should not publish procedure metadata");
    }

    return failures;
}

/// Verifies Phase 78 LOCAL declarations publish deterministic procedure metadata.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase78_local_declarations_parse_to_metadata(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    const VmProcedureRange *main_range = NULL;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        "COUNT EQU 16\n"
        ".data\n"
        "temp DWORD 1\n"
        ".code\n"
        "main PROC USES ebx\n"
        "    LOCAL temp:DWORD\n"
        "    LOCAL ch:BYTE, signedVal:SDWORD\n"
        "    LOCAL buf[COUNT]:BYTE\n"
        "    LOCAL literalBuf[16]:BYTE\n"
        "    LOCAL COUNT:WORD\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "Phase 78 LOCAL fixture should parse without diagnostics");
    failures += expect_size(result.diagnostic_count, 0U, "valid LOCAL fixture should emit no diagnostics");
    failures += expect_size(result.procedure_range_count, 1U, "valid LOCAL fixture should publish one procedure range");

    main_range = &buffers.procedure_ranges[0];
    failures += expect_size(main_range->uses_register_count, 1U, "LOCAL metadata should preserve existing PROC USES metadata");
    failures += expect_size(main_range->local_count, 6U, "valid LOCAL fixture should publish six local symbols");
    failures += expect_u32(main_range->local_frame_size_bytes, 48U, "LOCAL frame size should be rounded to four bytes");

    failures += expect_string(main_range->locals[0].name, "temp", "first LOCAL name should preserve source spelling");
    failures += expect_u32((uint32_t)main_range->locals[0].data_type, (uint32_t)VM_SYMBOL_DATA_TYPE_DWORD, "temp should be DWORD");
    failures += expect_u32(main_range->locals[0].element_count, 1U, "temp should be scalar");
    failures += expect_u32(main_range->locals[0].element_size_bytes, 4U, "temp element width should be four bytes");
    failures += expect_u32(main_range->locals[0].size_bytes, 4U, "temp should occupy four bytes");
    failures += expect_u32(main_range->locals[0].alignment_bytes, 4U, "temp should align to four bytes");
    failures += expect_i32(main_range->locals[0].ebp_offset, -4, "temp should live at EBP-4");

    failures += expect_string(main_range->locals[1].name, "ch", "second LOCAL name should preserve source spelling");
    failures += expect_u32((uint32_t)main_range->locals[1].data_type, (uint32_t)VM_SYMBOL_DATA_TYPE_BYTE, "ch should be BYTE");
    failures += expect_i32(main_range->locals[1].ebp_offset, -5, "ch should live at EBP-5 before padding for the following SDWORD");

    failures += expect_string(main_range->locals[2].name, "signedVal", "third LOCAL name should preserve source spelling");
    failures += expect_u32((uint32_t)main_range->locals[2].data_type, (uint32_t)VM_SYMBOL_DATA_TYPE_SDWORD, "signedVal should be SDWORD");
    failures += expect_i32(main_range->locals[2].ebp_offset, -12, "signedVal should be aligned to a four-byte slot");

    failures += expect_string(main_range->locals[3].name, "buf", "array LOCAL name should preserve source spelling");
    failures += expect_u32(main_range->locals[3].element_count, 16U, "buf should use the numeric equate array count");
    failures += expect_u32(main_range->locals[3].size_bytes, 16U, "buf should reserve sixteen bytes of metadata");
    failures += expect_i32(main_range->locals[3].ebp_offset, -28, "buf should follow previous locals in declaration order");

    failures += expect_string(main_range->locals[4].name, "literalBuf", "literal array LOCAL name should preserve source spelling");
    failures += expect_u32(main_range->locals[4].element_count, 16U, "literalBuf should use the literal array count");
    failures += expect_u32(main_range->locals[4].element_size_bytes, 1U, "literalBuf element width should be one byte");
    failures += expect_u32(main_range->locals[4].size_bytes, 16U, "literalBuf should reserve sixteen bytes of metadata");
    failures += expect_i32(main_range->locals[4].ebp_offset, -44, "literalBuf should follow the equate-count array in declaration order");

    failures += expect_string(main_range->locals[5].name, "COUNT", "LOCAL should be allowed to shadow a numeric equate inside the procedure");
    failures += expect_u32((uint32_t)main_range->locals[5].data_type, (uint32_t)VM_SYMBOL_DATA_TYPE_WORD, "COUNT local should be WORD");
    failures += expect_i32(main_range->locals[5].ebp_offset, -46, "COUNT local should be aligned to two bytes");
    failures += expect_size(main_range->locals[0].source_location.line, 6U, "first LOCAL source line should be preserved for debugger metadata");
    failures += expect_size(main_range->locals[0].source_location.column, 11U, "first LOCAL source column should target the local name");
    failures += expect_size(main_range->locals[0].source_span_length, 4U, "first LOCAL source span should cover the local name");

    return failures;
}

/// Verifies Phase 78 LOCAL declarations remain procedure-scoped.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase78_local_scoping_and_shadowing(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "first PROC\n"
        "    LOCAL value:DWORD\n"
        "    ret\n"
        "first ENDP\n"
        "second PROC\n"
        "    LOCAL value:BYTE\n"
        "    ret\n"
        "second ENDP\n"
        "END first\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "same LOCAL spelling in separate procedures and global shadowing should parse");
    failures += expect_size(result.diagnostic_count, 0U, "valid scoped LOCAL fixture should emit no diagnostics");
    failures += expect_size(result.procedure_range_count, 2U, "two procedure ranges should be published");
    failures += expect_size(buffers.procedure_ranges[0].local_count, 1U, "first procedure should have one local");
    failures += expect_size(buffers.procedure_ranges[1].local_count, 1U, "second procedure should have one local");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[0].locals[0].data_type, (uint32_t)VM_SYMBOL_DATA_TYPE_DWORD, "first local should retain DWORD type");
    failures += expect_u32((uint32_t)buffers.procedure_ranges[1].locals[0].data_type, (uint32_t)VM_SYMBOL_DATA_TYPE_BYTE, "second local should retain BYTE type");

    return failures;
}

/// Verifies Phase 78 LOCAL rejection paths use targeted diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase78_local_diagnostics(void) {
    /// Defines one invalid Phase 78 LOCAL parser fixture and its expected diagnostic.
    typedef struct LocalDiagnosticCase {
        const char *source;
        VmParserDiagnosticCode code;
        const char *message_fragment;
    } LocalDiagnosticCase;
    static const LocalDiagnosticCase cases[] = {
        {".code\nLOCAL temp:DWORD\nmain PROC\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_LOCAL_OUTSIDE_PROCEDURE, "inside a PROC body"},
        {".code\nmain PROC\nmov eax, 1\nLOCAL temp:DWORD\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_LOCAL_AFTER_INSTRUCTION, "before executable instructions"},
        {".code\nmain PROC\nLOCAL q:QWORD\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LOCAL_TYPE, "accepted types"},
        {".code\nmain PROC\nLOCAL sq:SQWORD\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LOCAL_TYPE, "SQWORD"},
        {".code\nmain PROC\nLOCAL r:REAL4\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LOCAL_TYPE, "REAL4"},
        {".code\nmain PROC\nLOCAL s:STRUCTTYPE\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LOCAL_TYPE, "STRUCTTYPE"},
        {".code\nmain PROC\nLOCAL x DWORD\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_LOCAL_DECLARATION, "name:TYPE"},
        {".code\nmain PROC\nLOCAL x:DWORD = 1\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_LOCAL_DECLARATION, "initializers"},
        {".code\nmain PROC\nLOCAL buf[0]:BYTE\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_LOCAL_COUNT, "positive"},
        {".code\nmain PROC\nLOCAL buf[-1]:BYTE\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_LOCAL_COUNT, "positive"},
        {"COUNT EQU 4\n.code\nmain PROC\nLOCAL buf[COUNT + eax]:BYTE\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_INVALID_LOCAL_COUNT, "compound"},
        {".code\nmain PROC\nLOCAL a:DWORD, a:BYTE\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_DUPLICATE_LOCAL_SYMBOL, "Duplicate LOCAL"},
        {".code\nmain PROC\nLOCAL eax:DWORD\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "reserved MASM register name"},
        {".code\nmain PROC\nLOCAL main:DWORD\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_DUPLICATE_LOCAL_SYMBOL, "conflicts with procedure"},
        {".code\nmain PROC\nagain:\nLOCAL again:DWORD\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_DUPLICATE_LOCAL_SYMBOL, "same-procedure label"},
        {".code\nmain PROC\nLOCAL again:DWORD\nagain:\nmain ENDP\nEND main\n", VM_PARSER_DIAGNOSTIC_DUPLICATE_LOCAL_SYMBOL, "same-procedure LOCAL"}
    };
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;
    size_t index = 0U;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(cases[index].source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "invalid LOCAL fixture should produce diagnostics");
        failures += expect_size(result.diagnostic_count, 1U, "invalid LOCAL fixture should emit one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, cases[index].code, "invalid LOCAL fixture should use the targeted diagnostic code");
        failures += expect_parser_diagnostic_severity(buffers.diagnostics[0].severity, VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR, "invalid LOCAL diagnostic should be an assembly error");
        failures += expect_string_contains(buffers.diagnostics[0].message, cases[index].message_fragment, "invalid LOCAL diagnostic should explain the rejection");
        if (buffers.diagnostics[0].location.line == 0U || buffers.diagnostics[0].lexeme_length == 0U) {
            failures += record_failure("invalid LOCAL diagnostic should preserve source location and span");
        }
    }

    return failures;
}

/// Verifies Phase 78 LOCAL metadata does not resolve local operands before Phase 80.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase78_local_operands_remain_deferred(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    LOCAL temp:DWORD\n"
        "    mov eax, temp\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LOCAL operands should remain unresolved before Phase 80");
    failures += expect_size(result.diagnostic_count, 1U, "deferred LOCAL operand fixture should emit one diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "LOCAL operand should still use existing unknown-symbol path before Phase 80");
    failures += expect_size(buffers.procedure_ranges[0].local_count, 1U, "LOCAL metadata should still be published before the operand diagnostic");

    return failures;
}


/// Verifies INCLUDE Irvine32.inc records virtual registry metadata.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase41_virtual_irvine32_include_records_registry(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *source =
        "include irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n";
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Irvine32 virtual include should parse without diagnostics");
    failures += expect_size(result.diagnostic_count, 0U, "Irvine32 virtual include should not produce diagnostics by itself");
    if (!result.has_irvine32_virtual_include) {
        failures += record_failure("Irvine32 virtual include should set parser metadata");
    }
    failures += expect_size(result.irvine32_virtual_symbol_count, vm_parser_irvine32_registry_symbol_count(), "Irvine32 virtual include should record registry count");
    if (result.irvine32_virtual_symbol_count == 0U) {
        failures += record_failure("Irvine32 virtual registry should contain known names");
    }

    failures += expect_u32(vm_parser_classify_irvine32_symbol("exit", 4U), VM_IRVINE32_SYMBOL_CLASS_SUPPORTED_VIRTUAL_INTRINSIC, "exit should be a supported virtual intrinsic in Phase 42");
    failures += expect_u32(vm_parser_classify_irvine32_symbol("WriteString", 11U), VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE, "WriteString should be a planned Irvine32 routine");
    failures += expect_u32(vm_parser_classify_irvine32_symbol("writestring", 11U), VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE, "Irvine32 routine lookup should be case-insensitive");
    failures += expect_u32(vm_parser_classify_irvine32_symbol("OpenInputFile", 13U), VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE, "file I/O routines should be known unsupported routines");
    failures += expect_u32(vm_parser_classify_irvine32_symbol("ExitProcess", 11U), VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL, "Windows API names should be classified separately");
    failures += expect_u32(vm_parser_classify_irvine32_symbol("UnknownRoutine", 14U), VM_IRVINE32_SYMBOL_CLASS_UNKNOWN, "unknown names should remain unknown");

    status = parse_for_test(
        "include macros.inc\n.code\nmain PROC\nmain ENDP\nEND main\n",
        &buffers,
        &result
    );
    failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Macros.inc virtual include should still parse with a Phase 53D notice");
    failures += expect_size(result.diagnostic_count, 1U, "Macros.inc should emit one compatibility notice");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED, "Macros.inc notice should use compatibility-limited");
    failures += expect_parser_diagnostic_severity(buffers.diagnostics[0].severity, VM_PARSER_DIAGNOSTIC_SEVERITY_NOTICE, "Macros.inc diagnostic should be informational");
    if (result.has_irvine32_virtual_include) {
        failures += record_failure("Macros.inc should not populate Irvine32 registry metadata");
    }
    failures += expect_size(result.irvine32_virtual_symbol_count, 0U, "Macros.inc should leave Irvine32 registry count at zero");

    return failures;
}


/// Verifies Phase 57P host/path-like INCLUDE diagnostics are parser-level diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57p_host_include_path_diagnostics(void) {
    typedef struct IncludePathCase {
        const char *source;
        VmParserDiagnosticCode code;
        const char *message_fragment;
    } IncludePathCase;
    static const IncludePathCase cases[] = {
        {"include \\masm32\\include\\masm32.inc\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY_INCLUDE, "local MASM32 SDK"},
        {"include \\masm32\\include\\kernel32.inc\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_INCLUDE, "Windows API execution"},
        {"include C:\\masm32\\include\\kernel32.inc\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_INCLUDE, "PE loading, imports, and WinAPI calls"},
        {"include ..\\include\\file.inc\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HOST_INCLUDE_PATH, "relative include paths"},
        {"include .\\local.inc\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HOST_INCLUDE_PATH, "supported virtual includes"},
        {"include /usr/local/include/file.inc\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HOST_INCLUDE_PATH, "include search paths"}
    };
    int failures = 0;
    size_t index = 0U;
    ParserTestBuffers buffers;
    VmParserResult result;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(cases[index].source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57P host include path should produce parser diagnostics");
        failures += expect_size(result.diagnostic_count, 1U, "Phase 57P host include path should produce one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, cases[index].code, "Phase 57P host include path should use the expected diagnostic code");
        failures += expect_parser_diagnostic_severity(buffers.diagnostics[0].severity, VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR, "Phase 57P host include path should be an assembly error");
        failures += expect_size(buffers.diagnostics[0].location.line, 1U, "Phase 57P host include path diagnostic should preserve line");
        failures += expect_size(buffers.diagnostics[0].location.column, 9U, "Phase 57P host include path diagnostic should point at path tail");
        failures += expect_size(buffers.diagnostics[0].location.offset, 8U, "Phase 57P host include path diagnostic should preserve byte offset");
        failures += expect_string_contains(buffers.diagnostics[0].message, cases[index].message_fragment, "Phase 57P host include path message should describe the specific unsupported boundary");
    }

    failures += expect_parser_status(parse_for_test(
        "include \\masm32\\include\\masm32.inc\n"
        "include \\masm32\\include\\kernel32.inc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57P multiple host include paths should produce parser diagnostics");
    failures += expect_size(result.diagnostic_count, 2U, "Phase 57P multiple host include paths should produce one diagnostic per include line");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY_INCLUDE, "Phase 57P first include path diagnostic should be MASM32 SDK specific");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_INCLUDE, "Phase 57P second include path diagnostic should be Windows/API specific");
    failures += expect_size(buffers.diagnostics[1].location.line, 2U, "Phase 57P second include path diagnostic should preserve line");
    failures += expect_size(buffers.diagnostics[1].location.column, 9U, "Phase 57P second include path diagnostic should point at path tail");

    failures += expect_parser_status(parse_for_test(
        "    InClUdE   ..\\include\\file.inc ; comment\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57P indented mixed-case host include path should produce parser diagnostics");
    failures += expect_size(result.diagnostic_count, 1U, "Phase 57P indented mixed-case include should produce one diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HOST_INCLUDE_PATH, "Phase 57P indented mixed-case include should use host include path diagnostic");
    failures += expect_size(buffers.diagnostics[0].location.column, 15U, "Phase 57P indented mixed-case include diagnostic should point at path tail");
    failures += expect_size(buffers.diagnostics[0].location.offset, 14U, "Phase 57P indented mixed-case include diagnostic should preserve byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 19U, "Phase 57P indented mixed-case include diagnostic should trim trailing whitespace and comment text");

    failures += expect_parser_status(parse_for_test(
        "mov eax, \\masm32\\include\\kernel32.inc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_LEXER_FAILED, "Phase 57P path outside INCLUDE context should still surface lexer diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LEXER_UNEXPECTED_CHARACTER, "Phase 57P should not make path separators valid outside INCLUDE lines");

    failures += expect_parser_status(parse_for_test(
        "INCLUDE Windows.inc\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57P basename-only unsupported include should remain on existing unsupported-include path");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDE, "Phase 57P should preserve basename-only unsupported include behavior");

    return failures;
}

/// Verifies Phase 57Q INCLUDELIB diagnostics are linker/library non-goal diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57q_includelib_diagnostics(void) {
    typedef struct IncludeLibCase {
        const char *source;
        VmParserDiagnosticCode code;
        const char *message_fragment;
        size_t column;
        size_t offset;
        size_t span;
    } IncludeLibCase;
    static const IncludeLibCase cases[] = {
        {"includelib \\masm32\\lib\\masm32.lib\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY, "external library linking", 12U, 11U, 22U},
        {"includelib \\masm32\\lib\\kernel32.lib\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_LIBRARY, "PE imports", 12U, 11U, 24U},
        {"includelib kernel32.lib\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_LIBRARY, "Windows import library", 12U, 11U, 12U},
        {"includelib masm32.lib\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY, "MASM32 library", 12U, 11U, 10U},
        {"includelib C:\\masm32\\lib\\kernel32.lib\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_LIBRARY, "PE imports", 12U, 11U, 26U},
        {"includelib customlib.lib\n", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDELIB, "does not link objects", 12U, 11U, 13U}
    };
    int failures = 0;
    size_t index = 0U;
    ParserTestBuffers buffers;
    VmParserResult result;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        failures += expect_parser_status(parse_for_test(cases[index].source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57Q INCLUDELIB should produce parser diagnostics");
        failures += expect_size(result.diagnostic_count, 1U, "Phase 57Q INCLUDELIB should produce one diagnostic");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, cases[index].code, "Phase 57Q INCLUDELIB should use the expected diagnostic code");
        failures += expect_parser_diagnostic_severity(buffers.diagnostics[0].severity, VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR, "Phase 57Q INCLUDELIB should be an assembly error");
        failures += expect_size(buffers.diagnostics[0].location.line, 1U, "Phase 57Q INCLUDELIB diagnostic should preserve line");
        failures += expect_size(buffers.diagnostics[0].location.column, cases[index].column, "Phase 57Q INCLUDELIB diagnostic should point at library operand");
        failures += expect_size(buffers.diagnostics[0].location.offset, cases[index].offset, "Phase 57Q INCLUDELIB diagnostic should preserve byte offset");
        failures += expect_size(buffers.diagnostics[0].lexeme_length, cases[index].span, "Phase 57Q INCLUDELIB diagnostic should preserve operand span");
        failures += expect_string_contains(buffers.diagnostics[0].message, cases[index].message_fragment, "Phase 57Q INCLUDELIB message should describe the specific linker boundary");
    }

    failures += expect_parser_status(parse_for_test(
        "includelib customlib.lib\n"
        "includelib kernel32.lib\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57Q multiple INCLUDELIB lines should produce parser diagnostics");
    failures += expect_size(result.diagnostic_count, 2U, "Phase 57Q multiple INCLUDELIB lines should produce one diagnostic per line");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDELIB, "Phase 57Q first INCLUDELIB diagnostic should be generic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_LIBRARY, "Phase 57Q second INCLUDELIB diagnostic should be Windows/API specific");
    failures += expect_size(buffers.diagnostics[1].location.line, 2U, "Phase 57Q second INCLUDELIB diagnostic should preserve line");
    failures += expect_size(buffers.diagnostics[1].location.column, 12U, "Phase 57Q second INCLUDELIB diagnostic should point at library operand");

    failures += expect_parser_status(parse_for_test(
        "mov eax, \\masm32\\lib\\kernel32.lib\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_LEXER_FAILED, "Phase 57Q path outside INCLUDELIB context should still surface lexer diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LEXER_UNEXPECTED_CHARACTER, "Phase 57Q should not make path separators valid outside directive lines");

    return failures;
}


/// Verifies Phase 57R classifies unsupported INVOKE, ADDR, and external routine lines.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57r_invoke_addr_external_routine_diagnostics(void) {
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;
    const char *stdout_source =
        ".data\n"
        "titleMsg BYTE \"Hello\", 0\n"
        ".code\n"
        "main PROC\n"
        "    invoke StdOut, addr titleMsg\n"
        "main ENDP\n"
        "END main\n";
    const char *crt_source =
        ".code\n"
        "main PROC\n"
        "    Invoke crt_printf, Addr numberFmt, counter\n"
        "main ENDP\n"
        "END main\n";
    const char *exitprocess_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    invoke ExitProcess, 0\n"
        "main ENDP\n"
        "END main\n";
    const char *registry_external_non_target_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    invoke MsgBox, 0\n"
        "main ENDP\n"
        "END main\n";
    const char *multiple_source =
        ".code\n"
        "main PROC\n"
        "    invoke StdOut, addr titleMsg\n"
        "    invoke crt_printf, addr numberFmt, counter\n"
        "    invoke ExitProcess, 0\n"
        "main ENDP\n"
        "END main\n";

    failures += expect_parser_status(parse_for_test(stdout_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57R StdOut INVOKE should produce diagnostics");
    failures += expect_size(result.diagnostic_count, 3U, "StdOut INVOKE should report INVOKE, ADDR, and MASM32 runtime diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "StdOut INVOKE should report unsupported-invoke first");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_ADDR, "StdOut INVOKE should report unsupported ADDR");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_RUNTIME_ROUTINE, "StdOut INVOKE should report MASM32 runtime routine");
    failures += expect_size(buffers.diagnostics[0].location.line, 5U, "INVOKE diagnostic should preserve line");
    failures += expect_size(buffers.diagnostics[0].location.column, 5U, "INVOKE diagnostic should point at INVOKE token");
    failures += expect_size(buffers.diagnostics[1].location.column, 20U, "ADDR diagnostic should point at ADDR token");
    failures += expect_size(buffers.diagnostics[2].location.column, 12U, "StdOut diagnostic should point at routine token");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 6U, "INVOKE diagnostic span should be six bytes");
    failures += expect_size(buffers.diagnostics[1].lexeme_length, 4U, "ADDR diagnostic span should be four bytes");
    failures += expect_size(buffers.diagnostics[2].lexeme_length, 6U, "StdOut diagnostic span should be six bytes");
    failures += expect_string_contains(buffers.diagnostics[0].message, "INVOKE syntax is not implemented", "INVOKE message should explain unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[1].message, "ADDR operands are not implemented", "ADDR message should explain unsupported operand operator");
    failures += expect_string_contains(buffers.diagnostics[2].message, "MASM32 runtime", "StdOut message should describe MASM32 runtime boundary");
    failures += expect_size(result.instruction_count, 0U, "unsupported INVOKE source should not emit instructions");

    failures += expect_parser_status(parse_for_test(crt_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57R CRT INVOKE should produce diagnostics");
    failures += expect_size(result.diagnostic_count, 3U, "CRT INVOKE should report INVOKE, ADDR, and CRT diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "CRT source should report unsupported-invoke");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_ADDR, "CRT source should report unsupported-addr");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CRT_ROUTINE, "CRT source should report unsupported CRT routine");
    failures += expect_string_contains(buffers.diagnostics[2].message, "C runtime", "CRT diagnostic should describe C runtime output");

    failures += expect_parser_status(parse_for_test(exitprocess_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57R ExitProcess INVOKE should produce diagnostics");
    failures += expect_size(result.diagnostic_count, 2U, "ExitProcess INVOKE should report INVOKE and WinAPI diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "ExitProcess source should report unsupported-invoke");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINAPI_EXECUTION, "ExitProcess source should report WinAPI non-goal");
    failures += expect_string_contains(buffers.diagnostics[1].message, "not the virtual Irvine32 exit", "ExitProcess message should distinguish virtual Irvine32 exit");
    failures += expect_size(buffers.diagnostics[1].location.line, 4U, "ExitProcess diagnostic should preserve line");
    failures += expect_size(buffers.diagnostics[1].location.column, 12U, "ExitProcess diagnostic should point at routine name");
    failures += expect_size(buffers.diagnostics[1].lexeme_length, 11U, "ExitProcess diagnostic span should match routine name");

    failures += expect_parser_status(parse_for_test(registry_external_non_target_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57R should not broaden WinAPI INVOKE diagnostics beyond named target routines");
    failures += expect_size(result.diagnostic_count, 1U, "non-target registry external INVOKE should report only unsupported-invoke in Phase 57R");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "non-target registry external should not receive ExitProcess-specific diagnostic");

    failures += expect_parser_status(parse_for_test(multiple_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57R multiple INVOKE lines should recover");
    failures += expect_size(result.diagnostic_count, 8U, "multiple INVOKE lines should report useful line diagnostics without token cascades");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "first line should start with unsupported-invoke");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_ADDR, "first line should include unsupported-addr");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_RUNTIME_ROUTINE, "first line should include MASM32 runtime routine");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[3].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "second line should start with unsupported-invoke");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[4].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_ADDR, "second line should include unsupported-addr");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[5].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CRT_ROUTINE, "second line should include CRT routine");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[6].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE, "third line should start with unsupported-invoke");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[7].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINAPI_EXECUTION, "third line should include WinAPI routine");

    failures += expect_string(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INVOKE), "unsupported-invoke", "unsupported-invoke code name should be stable");
    failures += expect_string(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_ADDR), "unsupported-addr", "unsupported-addr code name should be stable");
    failures += expect_string(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_RUNTIME_ROUTINE), "unsupported-masm32-runtime-routine", "MASM32 routine code name should be stable");
    failures += expect_string(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CRT_ROUTINE), "unsupported-crt-routine", "CRT routine code name should be stable");
    failures += expect_string(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_EXTERNAL_ROUTINE), "unsupported-external-routine", "generic external routine code name should be stable");
    failures += expect_string(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINAPI_EXECUTION), "unsupported-winapi-execution", "WinAPI code name should be stable");

    return failures;
}

/// Verifies known Irvine32 names produce specific diagnostics only after the virtual include.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase41_irvine32_routine_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *with_include =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    WriteString\n"
        "main ENDP\n"
        "END main\n";
    const char *without_include =
        ".code\n"
        "main PROC\n"
        "    WriteString\n"
        "main ENDP\n"
        "END main\n";
    const char *windows_name =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    ExitProcess\n"
        "main ENDP\n"
        "END main\n";
    const char *exit_line =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n";

    failures += expect_parser_status(parse_for_test(with_include, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Known Irvine32 routine should produce a specific diagnostic after include");
    if (!result.has_irvine32_virtual_include) {
        failures += record_failure("Known routine diagnostic program should retain include metadata");
    }
    failures += expect_size(result.diagnostic_count, 1U, "Known routine diagnostic program should emit one diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE, "Known Irvine32 routine diagnostic code should match");
    failures += expect_size(buffers.diagnostics[0].location.line, 4U, "Known Irvine32 routine diagnostic should point at executable line");
    failures += expect_size(buffers.diagnostics[0].location.column, 5U, "Known Irvine32 routine diagnostic should point at routine name");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Recognized Irvine32 routine", "Known Irvine32 routine diagnostic should explain registry classification");

    failures += expect_parser_status(parse_for_test(without_include, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Known Irvine32 name without include should remain an unsupported instruction");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION, "Irvine32 registry should not be implicit without include");
    if (result.has_irvine32_virtual_include) {
        failures += record_failure("Source without Irvine32 include must not set include metadata");
    }

    failures += expect_parser_status(parse_for_test(windows_name, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Windows/API name should produce a specific Irvine32-context diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE, "Windows/API bare-routine diagnostic should use the Irvine32 routine diagnostic before executable Irvine32 dispatch exists");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Windows/API", "Windows/API diagnostic should describe non-goal behavior");

    failures += expect_parser_status(parse_for_test(exit_line, &buffers, &result), VM_PARSER_STATUS_OK, "exit should parse as an Irvine32 virtual terminator in Phase 42");
    failures += expect_size(result.diagnostic_count, 0U, "exit terminator should not emit a Phase 41 unsupported-routine diagnostic anymore");
    failures += expect_size(result.instruction_count, 1U, "exit terminator should emit one IR instruction");
    if (result.instruction_count > 0U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_EXIT, "exit should lower to EXIT IR opcode");
    }

    return failures;
}


/// Verifies Phase 43 INC and DEC parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase43_inc_dec_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    inc al\n"
        "    dec ax\n"
        "    inc eax\n"
        "    inc BYTE PTR [esi]\n"
        "    inc WORD PTR [esi]\n"
        "    dec DWORD PTR [esi]\n"
        "    inc SBYTE PTR [esi]\n"
        "    dec SWORD PTR [esi]\n"
        "    inc SDWORD PTR [esi]\n"
        "    inc value\n"
        "    dec arr[8]\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 43 INC/DEC program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 43 INC/DEC program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 11U, "Phase 43 INC/DEC program should emit eleven instructions");
    if (result.instruction_count == 11U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_INC, "inc al should emit INC opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "inc al destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AL, "inc al destination should be AL");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_NONE, "INC should emit no source operand");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_DEC, "dec ax should emit DEC opcode");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_AX, "dec ax destination should be AX");
        failures += expect_u32(buffers.instructions[2].destination.reg, VM_REGISTER_EAX, "inc eax destination should be EAX");
        failures += expect_u32(buffers.instructions[3].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "inc BYTE PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[3].destination.width_bits, 8U, "inc BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[4].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "inc WORD PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[4].destination.width_bits, 16U, "inc WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[5].opcode, VM_IR_OPCODE_DEC, "dec DWORD PTR [esi] should emit DEC opcode");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 32U, "dec DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[6].opcode, VM_IR_OPCODE_INC, "inc SBYTE PTR [esi] should emit INC opcode");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 8U, "inc SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[7].opcode, VM_IR_OPCODE_DEC, "dec SWORD PTR [esi] should emit DEC opcode");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 16U, "dec SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 32U, "inc SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[9].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "inc value should emit direct memory destination");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 32U, "inc value should infer DWORD width");
        failures += expect_u32(buffers.instructions[10].destination.width_bits, 32U, "dec arr[8] should infer DWORD width");
    }

    return failures;
}

/// Verifies Phase 43 INC and DEC parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase43_inc_dec_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    inc 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "INC immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "INC immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "INC immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    dec 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DEC immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "DEC immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "DEC immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    inc eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "INC extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "INC extra operand should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly one", "INC extra operand diagnostic should describe operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    dec eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DEC extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "DEC extra operand should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly one", "DEC extra operand diagnostic should describe operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    inc [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "INC ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "INC ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "INC ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    dec [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DEC ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "DEC ambiguous memory diagnostic should be ambiguous-memory-width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    inc QWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "INC QWORD memory operation should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "INC QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD -1\n"
        ".code\n"
        "main PROC\n"
        "    dec SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DEC SQWORD memory operation should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "DEC SQWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    inc limit\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "INC direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "INC direct .CONST destination should use const-write diagnostic");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    dec limit\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DEC direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "DEC direct .CONST destination should use const-write diagnostic");

    return failures;
}

/// Verifies Phase 44 AND, OR, and XOR parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase44_logical_binary_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 0F0F0F0Fh\n"
        "bytes BYTE 1, 2\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    and eax, ebx\n"
        "    or eax, 0FFh\n"
        "    xor eax, value\n"
        "    and value, eax\n"
        "    or DWORD PTR [esi], 1\n"
        "    xor BYTE PTR bytes[1], al\n"
        "    and SBYTE PTR [esi], -1\n"
        "    or SWORD PTR [esi], ax\n"
        "    xor SDWORD PTR [esi], eax\n"
        "    and arr[4], 0FFFFFFFFh\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 44 logical binary program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 44 logical binary program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 10U, "Phase 44 logical binary program should emit ten instructions");
    if (result.instruction_count == 10U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_AND, "and eax, ebx should emit AND opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "and register destination should be register");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_REGISTER, "and register source should be register");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_OR, "or eax, imm should emit OR opcode");
        failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_IMMEDIATE, "or immediate source should be immediate");
        failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_XOR, "xor eax, value should emit XOR opcode");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "xor register, memory should use memory source");
        failures += expect_u32(buffers.instructions[3].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "and value, eax should use memory destination");
        failures += expect_u32(buffers.instructions[3].destination.width_bits, 32U, "and value, eax should infer DWORD width");
        failures += expect_u32(buffers.instructions[4].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "or DWORD PTR [esi], 1 should use register-indirect destination");
        failures += expect_u32(buffers.instructions[4].destination.width_bits, 32U, "or DWORD PTR [esi], 1 should use 32-bit width");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 8U, "xor BYTE PTR bytes[1], al should use 8-bit width");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 8U, "and SBYTE PTR [esi], -1 should use 8-bit width");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 16U, "or SWORD PTR [esi], ax should use 16-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 32U, "xor SDWORD PTR [esi], eax should use 32-bit width");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 32U, "and arr[4], imm should infer DWORD width");
    }

    return failures;
}

/// Verifies Phase 44 AND, OR, and XOR parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase44_logical_binary_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    and 1, eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "AND immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "AND immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "AND immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    or [eax], 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "OR ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "OR ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 8U, "OR ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "value DWORD 1\n"
        "other DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    xor value, other\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "XOR memory-to-memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "XOR memory-to-memory should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "memory-to-memory", "XOR memory-to-memory diagnostic should explain unsupported shape");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    xor eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "XOR missing operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, "XOR missing source currently uses expected-comma diagnostic");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    and eax, ebx, ecx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "AND extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "AND extra operand should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly two", "AND extra operand diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD -1\n"
        ".code\n"
        "main PROC\n"
        "    and SQWORD PTR q, eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "AND SQWORD memory operation should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "AND SQWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    or limit, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "OR direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "OR direct .CONST destination should use const-write diagnostic");

    return failures;
}

/// Verifies Milestone 45 NOT parser acceptance and IR shapes as a Phase 48 regression.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase45_not_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 0F0F0F0Fh\n"
        "bytes BYTE 1, 2\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    not al\n"
        "    NoT ax\n"
        "    not eax\n"
        "    not BYTE PTR [esi]\n"
        "    not WORD PTR [esi]\n"
        "    not DWORD PTR [esi]\n"
        "    not SBYTE PTR [esi]\n"
        "    not SWORD PTR [esi]\n"
        "    not SDWORD PTR [esi]\n"
        "    not value\n"
        "    not bytes[1]\n"
        "    not arr[4]\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Milestone 45 NOT regression program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Milestone 45 NOT regression program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 12U, "Milestone 45 NOT regression program should emit twelve instructions");
    if (result.instruction_count == 12U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_NOT, "not al should emit NOT opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "not al destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AL, "not al destination should be AL");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_NONE, "NOT should emit no source operand");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_NOT, "mixed-case NOT should emit NOT opcode");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_AX, "NoT ax destination should be AX");
        failures += expect_u32(buffers.instructions[2].destination.reg, VM_REGISTER_EAX, "not eax destination should be EAX");
        failures += expect_u32(buffers.instructions[3].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "not BYTE PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[3].destination.width_bits, 8U, "not BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[4].destination.width_bits, 16U, "not WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 32U, "not DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 8U, "not SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 16U, "not SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 32U, "not SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[9].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "not value should emit direct memory destination");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 32U, "not value should infer DWORD width");
        failures += expect_u32(buffers.instructions[10].destination.width_bits, 8U, "not bytes[1] should infer BYTE width");
        failures += expect_u32(buffers.instructions[11].destination.width_bits, 32U, "not arr[4] should infer DWORD width");
    }

    return failures;
}

/// Verifies Milestone 45 NOT parser diagnostics as a Phase 48 regression.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase45_not_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    not 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NOT immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "NOT immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "NOT immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    not eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NOT extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "NOT extra operand should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly one", "NOT extra operand diagnostic should describe operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    not [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NOT ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "NOT ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "NOT ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    not QWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NOT QWORD memory operation should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "NOT QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD -1\n"
        ".code\n"
        "main PROC\n"
        "    not SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NOT SQWORD memory operation should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "NOT SQWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    not limit\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "NOT direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "NOT direct .CONST destination should use const-write diagnostic");

    return failures;
}


/// Verifies Phase 46 SHL/SAL parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase46_shift_left_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "bytes BYTE 1, 2\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    shl al, 1\n"
        "    ShL ax, 2\n"
        "    shl eax, cl\n"
        "    sal eax, 32\n"
        "    shl BYTE PTR [esi], 1\n"
        "    shl WORD PTR [esi], cl\n"
        "    sal DWORD PTR [esi], 3\n"
        "    shl SBYTE PTR [esi], 1\n"
        "    shl SWORD PTR [esi], 1\n"
        "    shl SDWORD PTR [esi], 1\n"
        "    shl value, 1\n"
        "    sal bytes[1], 1\n"
        "    shl arr[4], cl\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 46 SHL/SAL program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 46 SHL/SAL program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 13U, "Phase 46 SHL/SAL program should emit thirteen instructions");
    if (result.instruction_count == 13U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_SHL, "shl al should emit SHL opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "shl al destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AL, "shl al destination should be AL");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "shl al source should be immediate count");
        failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "shl al immediate count should be one");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_SHL, "mixed-case ShL should emit SHL opcode");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_AX, "ShL ax destination should be AX");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_REGISTER, "shl eax, cl source should be register count");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_CL, "shl eax, cl should use CL count register");
        failures += expect_u32(buffers.instructions[3].opcode, VM_IR_OPCODE_SAL, "sal eax should emit SAL opcode");
        failures += expect_u32(buffers.instructions[4].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "shl BYTE PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[4].destination.width_bits, 8U, "shl BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 16U, "shl WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 32U, "sal DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 8U, "shl SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 16U, "shl SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 32U, "shl SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[10].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "shl value should emit direct memory destination");
        failures += expect_u32(buffers.instructions[10].destination.width_bits, 32U, "shl value should infer DWORD width");
        failures += expect_u32(buffers.instructions[11].destination.width_bits, 8U, "sal bytes[1] should infer BYTE width");
        failures += expect_u32(buffers.instructions[12].destination.width_bits, 32U, "shl arr[4] should infer DWORD width");
    }

    return failures;
}

/// Verifies Phase 46 SHL/SAL parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase46_shift_left_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shl 1, al\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHL immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHL immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "SHL immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shl eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHL invalid count register should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHL invalid count register should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "immediate byte count or CL", "SHL invalid count diagnostic should explain CL requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sal eax, cx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAL CX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SAL CX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shl [eax], 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHL ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "SHL ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "SHL ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shl eax, 256\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHL out-of-range count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHL out-of-range count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    shl QWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHL QWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "SHL QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    sal limit, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAL direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "SAL direct .CONST destination should use const-write diagnostic");

    return failures;
}


/// Verifies Phase 47 SHR parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase47_shr_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "bytes BYTE 1, 2\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    shr al, 1\n"
        "    ShR ax, 2\n"
        "    shr eax, cl\n"
        "    shr eax, 32\n"
        "    shr BYTE PTR [esi], 1\n"
        "    shr WORD PTR [esi], cl\n"
        "    shr DWORD PTR [esi], 3\n"
        "    shr SBYTE PTR [esi], 1\n"
        "    shr SWORD PTR [esi], 1\n"
        "    shr SDWORD PTR [esi], 1\n"
        "    shr value, 1\n"
        "    shr bytes[1], 1\n"
        "    shr arr[4], cl\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 47 SHR program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 47 SHR program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 13U, "Phase 47 SHR program should emit thirteen instructions");
    if (result.instruction_count == 13U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_SHR, "shr al should emit SHR opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "shr al destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AL, "shr al destination should be AL");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "shr al source should be immediate count");
        failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "shr al immediate count should be one");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_SHR, "mixed-case ShR should emit SHR opcode");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_AX, "ShR ax destination should be AX");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_REGISTER, "shr eax, cl source should be register count");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_CL, "shr eax, cl should use CL count register");
        failures += expect_u32(buffers.instructions[4].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "shr BYTE PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[4].destination.width_bits, 8U, "shr BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 16U, "shr WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 32U, "shr DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 8U, "shr SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 16U, "shr SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 32U, "shr SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[10].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "shr value should emit direct memory destination");
        failures += expect_u32(buffers.instructions[10].destination.width_bits, 32U, "shr value should infer DWORD width");
        failures += expect_u32(buffers.instructions[11].destination.width_bits, 8U, "shr bytes[1] should infer BYTE width");
        failures += expect_u32(buffers.instructions[12].destination.width_bits, 32U, "shr arr[4] should infer DWORD width");
    }

    return failures;
}

/// Verifies Phase 47 SHR parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase47_shr_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr 1, al\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHR immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "SHR immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR invalid count register should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHR invalid count register should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "immediate byte count or CL", "SHR invalid count diagnostic should explain CL requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr eax, cx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR CX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHR CX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr eax, ecx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR ECX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHR ECX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr [eax], 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "SHR ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "SHR ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr eax, 256\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR out-of-range count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHR out-of-range count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR missing count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, "SHR missing count currently uses expected-comma diagnostic");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    shr QWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR QWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "SHR QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    shr SQWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR SQWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "SHR SQWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    shr limit, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "SHR direct .CONST destination should use const-write diagnostic");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    shr eax, 1, 2\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SHR extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SHR extra operand should use invalid-instruction-operands");

    return failures;
}

/// Verifies Phase 48 SAR parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase48_sar_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "bytes BYTE 1, 2\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    sar al, 1\n"
        "    SaR ax, 2\n"
        "    sar eax, cl\n"
        "    sar eax, 32\n"
        "    sar BYTE PTR [esi], 1\n"
        "    sar WORD PTR [esi], cl\n"
        "    sar DWORD PTR [esi], 3\n"
        "    sar SBYTE PTR [esi], 1\n"
        "    sar SWORD PTR [esi], 1\n"
        "    sar SDWORD PTR [esi], 1\n"
        "    sar value, 1\n"
        "    sar bytes[1], 1\n"
        "    sar arr[4], cl\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 48 SAR program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 48 SAR program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 13U, "Phase 48 SAR program should emit thirteen instructions");
    if (result.instruction_count == 13U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_SAR, "sar al should emit SAR opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "sar al destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AL, "sar al destination should be AL");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "sar al source should be immediate count");
        failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "sar al immediate count should be one");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_SAR, "mixed-case SaR should emit SAR opcode");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_AX, "SaR ax destination should be AX");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_REGISTER, "sar eax, cl source should be register count");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_CL, "sar eax, cl should use CL count register");
        failures += expect_u32(buffers.instructions[4].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "sar BYTE PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[4].destination.width_bits, 8U, "sar BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 16U, "sar WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 32U, "sar DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 8U, "sar SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 16U, "sar SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 32U, "sar SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[10].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "sar value should emit direct memory destination");
        failures += expect_u32(buffers.instructions[10].destination.width_bits, 32U, "sar value should infer DWORD width");
        failures += expect_u32(buffers.instructions[11].destination.width_bits, 8U, "sar bytes[1] should infer BYTE width");
        failures += expect_u32(buffers.instructions[12].destination.width_bits, 32U, "sar arr[4] should infer DWORD width");
    }

    return failures;
}

/// Verifies Phase 48 SAR parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase48_sar_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar 1, al\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SAR immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "SAR immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR invalid count register should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SAR invalid count register should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "immediate byte count or CL", "SAR invalid count diagnostic should explain CL requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar eax, cx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR CX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SAR CX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar eax, ecx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR ECX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SAR ECX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar [eax], 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "SAR ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "SAR ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar eax, 256\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR out-of-range count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SAR out-of-range count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR missing count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, "SAR missing count currently uses expected-comma diagnostic");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    sar QWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR QWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "SAR QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    sar SQWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR SQWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "SAR SQWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    sar limit, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "SAR direct .CONST destination should use const-write diagnostic");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    sar eax, 1, 2\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SAR extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "SAR extra operand should use invalid-instruction-operands");

    return failures;
}

/// Verifies Phase 42 parser behavior for the Irvine32 exit terminator.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase42_irvine32_exit_terminator_parser_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *acceptance_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 123\n"
        "    ExIt\n"
        "    mov eax, 999\n"
        "main ENDP\n"
        "END main\n";
    const char *no_include_source =
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n";
    const char *operand_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    exit 0\n"
        "main ENDP\n"
        "END main\n";
    const char *macros_only_source =
        "INCLUDE Macros.inc\n"
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n";

    failures += expect_parser_status(parse_for_test(acceptance_source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 42 acceptance source should parse");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 42 acceptance source should not emit diagnostics");
    failures += expect_size(result.instruction_count, 3U, "Phase 42 acceptance source should retain instructions after exit in IR");
    if (result.instruction_count == 3U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_MOV, "First instruction should be MOV");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_EXIT, "Second instruction should be EXIT");
        failures += expect_u32(buffers.instructions[2].opcode, VM_IR_OPCODE_MOV, "Third instruction should remain parsed but not execute after EXIT");
    }

    failures += expect_parser_status(parse_for_test(no_include_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "exit without Irvine32 include should be an assembly diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_INSTRUCTION, "exit without include should use unknown-instruction");
    failures += expect_string(buffers.diagnostics[0].message, "Unknown instruction or virtual Irvine32 terminator. Add INCLUDE Irvine32.inc to use exit.", "exit without include should use the required diagnostic text");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "exit without include diagnostic should point at mnemonic line");
    failures += expect_size(buffers.diagnostics[0].location.column, 5U, "exit without include diagnostic should point at mnemonic column");

    failures += expect_parser_status(parse_for_test(operand_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "exit operands should be rejected");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "exit operands should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "does not take operands", "exit operand diagnostic should explain zero-operand form");

    failures += expect_parser_status(parse_for_test(macros_only_source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Macros.inc alone should not enable exit");
    failures += expect_size(result.diagnostic_count, 2U, "Macros.inc plus exit should emit one notice and one assembly diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED, "Macros.inc alone should emit its compatibility notice");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_INSTRUCTION, "Macros.inc alone should leave exit unknown");

    return failures;
}


/// Verifies Phase 49 ROL parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase49_rol_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "bytes BYTE 1, 2\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    rol al, 1\n"
        "    RoL ax, 2\n"
        "    rol eax, cl\n"
        "    rol eax, 32\n"
        "    rol BYTE PTR [esi], 1\n"
        "    rol WORD PTR [esi], cl\n"
        "    rol DWORD PTR [esi], 3\n"
        "    rol SBYTE PTR [esi], 1\n"
        "    rol SWORD PTR [esi], 1\n"
        "    rol SDWORD PTR [esi], 1\n"
        "    rol value, 1\n"
        "    rol bytes[1], 1\n"
        "    rol arr[4], cl\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 49 ROL program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 49 ROL program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 13U, "Phase 49 ROL program should emit thirteen instructions");
    if (result.instruction_count == 13U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_ROL, "rol al should emit ROL opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "rol al destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AL, "rol al destination should be AL");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "rol al source should be immediate count");
        failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "rol al immediate count should be one");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_ROL, "mixed-case RoL should emit ROL opcode");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_AX, "RoL ax destination should be AX");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_REGISTER, "rol eax, cl source should be register count");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_CL, "rol eax, cl should use CL count register");
        failures += expect_u32(buffers.instructions[4].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "rol BYTE PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[4].destination.width_bits, 8U, "rol BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 16U, "rol WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 32U, "rol DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 8U, "rol SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 16U, "rol SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 32U, "rol SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[10].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "rol value should emit direct memory destination");
        failures += expect_u32(buffers.instructions[10].destination.width_bits, 32U, "rol value should infer DWORD width");
        failures += expect_u32(buffers.instructions[11].destination.width_bits, 8U, "rol bytes[1] should infer BYTE width");
        failures += expect_u32(buffers.instructions[12].destination.width_bits, 32U, "rol arr[4] should infer DWORD width");
    }

    return failures;
}

/// Verifies Phase 49 ROL parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase49_rol_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol 1, al\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROL immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "ROL immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL invalid count register should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROL invalid count register should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "immediate byte count or CL", "ROL invalid count diagnostic should explain CL requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol eax, cx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL CX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROL CX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol eax, ecx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL ECX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROL ECX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol [eax], 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "ROL ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "ROL ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol eax, 256\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL out-of-range count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROL out-of-range count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL missing count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROL missing count should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly two operands", "ROL missing count diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    rol eax, 1, 2\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROL extra operand should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly two operands", "ROL extra operand diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    rol QWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL QWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "ROL QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    rol SQWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL SQWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "ROL SQWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    rol limit, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROL direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "ROL direct .CONST destination should use const-write diagnostic");

    return failures;
}

/// Verifies Phase 50 ROR parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase50_ror_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "bytes BYTE 1, 2\n"
        "arr DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    ror al, 1\n"
        "    RoR ax, 2\n"
        "    ror eax, cl\n"
        "    ror eax, 0\n"
        "    ror eax, 32\n"
        "    ror BYTE PTR [esi], 1\n"
        "    ror WORD PTR [esi], cl\n"
        "    ror DWORD PTR [esi], 3\n"
        "    ror SBYTE PTR [esi], 1\n"
        "    ror SWORD PTR [esi], 1\n"
        "    ror SDWORD PTR [esi], 1\n"
        "    ror value, 1\n"
        "    ror bytes[1], 1\n"
        "    ror arr[4], cl\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 50 ROR program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 50 ROR program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 14U, "Phase 50 ROR program should emit fourteen instructions");
    if (result.instruction_count == 14U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_ROR, "ror al should emit ROR opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "ror al destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AL, "ror al destination should be AL");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "ror al source should be immediate count");
        failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "ror al immediate count should be one");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_ROR, "mixed-case RoR should emit ROR opcode");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_AX, "RoR ax destination should be AX");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_REGISTER, "ror eax, cl source should be register count");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_CL, "ror eax, cl should use CL count register");
        failures += expect_u32(buffers.instructions[3].source.immediate, 0U, "ror eax, 0 immediate count should be zero");
        failures += expect_u32(buffers.instructions[4].source.immediate, 32U, "ror eax, 32 immediate count should be thirty-two");
        failures += expect_u32(buffers.instructions[5].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "ror BYTE PTR [esi] should emit register-indirect memory");
        failures += expect_u32(buffers.instructions[5].destination.width_bits, 8U, "ror BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[6].destination.width_bits, 16U, "ror WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[7].destination.width_bits, 32U, "ror DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[8].destination.width_bits, 8U, "ror SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[9].destination.width_bits, 16U, "ror SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[10].destination.width_bits, 32U, "ror SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[11].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "ror value should emit direct memory destination");
        failures += expect_u32(buffers.instructions[11].destination.width_bits, 32U, "ror value should infer DWORD width");
        failures += expect_u32(buffers.instructions[12].destination.width_bits, 8U, "ror bytes[1] should infer BYTE width");
        failures += expect_u32(buffers.instructions[13].destination.width_bits, 32U, "ror arr[4] should infer DWORD width");
    }

    return failures;
}

/// Verifies Phase 50 ROR parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase50_ror_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror 1, al\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR immediate destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROR immediate destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory destination", "ROR immediate diagnostic should explain destination requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR invalid count register should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROR invalid count register should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "immediate byte count or CL", "ROR invalid count diagnostic should explain CL requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror eax, cx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR CX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROR CX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror eax, ecx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR ECX count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROR ECX count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror [eax], 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR ambiguous memory should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "ROR ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "ROR ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror eax, 256\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR out-of-range count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROR out-of-range count should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR missing count should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROR missing count should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly two operands", "ROR missing count diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    ror eax, 1, 2\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "ROR extra operand should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly two operands", "ROR extra operand diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    ror QWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR QWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "ROR QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    ror SQWORD PTR q, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR SQWORD destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "ROR SQWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    ror limit, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ROR direct .CONST destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_CONST_WRITE, "ROR direct .CONST destination should use const-write diagnostic");

    return failures;
}


/// Verifies Phase 52 LEA accepted effective-address forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase52_lea_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        "bytes BYTE 4 DUP(0)\n"
        ".CONST\n"
        "limit QWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, nums\n"
        "    LeA ebx, nums[8]\n"
        "    lea ecx, [nums + 8]\n"
        "    lea edx, [esi]\n"
        "    lea edi, [esi + 4]\n"
        "    lea ebp, [esi - 4]\n"
        "    lea esp, nums[esi]\n"
        "    lea eax, [nums + esi]\n"
        "    lea ebx, limit\n"
        "    lea ecx, limit[esi]\n"
        "    lea edx, [limit + esi]\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 52 LEA accepted forms should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 52 LEA accepted forms should not produce diagnostics");
    failures += expect_size(result.instruction_count, 11U, "Phase 52 LEA accepted forms should emit eleven instructions");
    if (result.instruction_count == 11U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_LEA, "lea nums should emit LEA opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "LEA destination should be register");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_EAX, "LEA destination should preserve EAX");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "LEA direct symbol source should be address operand");
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_LEA, "mixed-case LeA should emit LEA opcode");
        failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "LEA symbol offset should carry displacement operand");
        failures += expect_u32(buffers.instructions[1].source.reg, VM_REGISTER_COUNT, "LEA symbol offset should not require runtime register");
        failures += expect_u32(buffers.instructions[1].source.immediate, 8U, "LEA nums[8] should preserve byte displacement");
        failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "LEA [nums + 8] should carry displacement operand");
        failures += expect_u32(buffers.instructions[2].source.immediate, 8U, "LEA [nums + 8] should preserve displacement");
        failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "LEA [esi] should emit register-derived address");
        failures += expect_u32(buffers.instructions[3].source.reg, VM_REGISTER_ESI, "LEA [esi] should use ESI base");
        failures += expect_u32(buffers.instructions[4].source.immediate, 4U, "LEA [esi + 4] should use positive displacement");
        failures += expect_u32(buffers.instructions[5].source.immediate, (uint32_t)-4, "LEA [esi - 4] should use negative displacement bits");
        failures += expect_u32(buffers.instructions[6].source.reg, VM_REGISTER_ESI, "LEA nums[esi] should use ESI runtime offset");
        failures += expect_u32(buffers.instructions[7].source.reg, VM_REGISTER_ESI, "LEA [nums + esi] should use ESI runtime offset");
        failures += expect_u32(buffers.instructions[8].source.relocation, VM_IR_RELOCATION_CONST, "LEA const symbol should retain const relocation metadata");
        failures += expect_u32(buffers.instructions[9].source.reg, VM_REGISTER_ESI, "LEA QWORD const symbol[esi] should use ESI runtime offset");
        failures += expect_u32(buffers.instructions[9].source.relocation, VM_IR_RELOCATION_CONST, "LEA QWORD const symbol[esi] should retain const relocation metadata without executable-width checks");
        failures += expect_u32(buffers.instructions[10].source.reg, VM_REGISTER_ESI, "LEA [QWORD const symbol + esi] should use ESI runtime offset");
        failures += expect_u32(buffers.instructions[10].source.relocation, VM_IR_RELOCATION_CONST, "LEA [QWORD const symbol + esi] should retain const relocation metadata without executable-width checks");
    }

    return failures;
}

/// Verifies Phase 52 LEA parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase52_lea_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea nums, nums\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA memory destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "LEA memory destination should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "32-bit register destination", "LEA memory destination diagnostic should explain destination rule");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea ax, nums\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA 16-bit destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "LEA 16-bit destination should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea al, nums\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA 8-bit destination should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "LEA 8-bit destination should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    lea eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA register source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_EFFECTIVE_ADDRESS_EXPRESSION, "LEA register source should report invalid effective-address expression");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, 123\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA immediate source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_EFFECTIVE_ADDRESS_EXPRESSION, "LEA immediate source should use invalid-effective-address-expression");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, OFFSET nums\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA OFFSET source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_EFFECTIVE_ADDRESS_EXPRESSION, "LEA OFFSET source should use invalid-effective-address-expression");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    lea eax, [eax * 4]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA scaled-index register source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX, "LEA scaled-index source should use unsupported-scaled-index");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, [nums + esi * 4]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA scaled-index symbol source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX, "LEA symbol scaled-index source should use unsupported-scaled-index");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    lea eax, [0]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA numeric memory expression should remain rejected");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_EFFECTIVE_ADDRESS_EXPRESSION, "LEA numeric memory expression should use invalid-effective-address-expression");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, [nums + 2147483648]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA bracketed displacement overflow should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_EFFECTIVE_ADDRESS_EXPRESSION, "LEA bracketed displacement overflow should use invalid-effective-address-expression");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, nums, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA extra operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "LEA extra operand should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LEA missing source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, "LEA missing source currently uses expected-comma diagnostic");

    return failures;
}


/// Verifies Phase 57-CORR2 compact negative register-displacement parser acceptance.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57corr2_compact_negative_register_displacement_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    mov DWORD PTR [eax-4], 10\n"
        "    mov DWORD PTR [esi-8], 10\n"
        "    mov DWORD PTR [ebp-10h], 10\n"
        "    mov DWORD PTR [ebp-0x10], 10\n"
        "    mov eax, DWORD PTR [ecx-4]\n"
        "    mov ax, WORD PTR [edx-2]\n"
        "    mov al, BYTE PTR [edi-1]\n"
        "    lea eax, [ebx-4]\n"
        "    mov DWORD PTR [eax], 10\n"
        "    mov DWORD PTR [eax+4], 10\n"
        "    mov DWORD PTR [eax + 4], 10\n"
        "    mov DWORD PTR [eax - 4], 10\n"
        "    lea eax, [ebx + 4]\n"
        "    lea eax, [ebx - 4]\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 57-CORR2 compact negative displacement forms should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 57-CORR2 compact negative displacement forms should not produce diagnostics");
    failures += expect_size(result.instruction_count, 15U, "Phase 57-CORR2 parser fixture should emit fifteen instructions");
    if (result.instruction_count == 15U) {
        failures += expect_u32(buffers.instructions[1].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "MOV DWORD PTR [eax-4] should emit register-derived memory destination");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_EAX, "MOV DWORD PTR [eax-4] should use EAX base");
        failures += expect_u32(buffers.instructions[1].destination.immediate, (uint32_t)-4, "MOV DWORD PTR [eax-4] should preserve -4 byte displacement");
        failures += expect_u32(buffers.instructions[2].destination.reg, VM_REGISTER_ESI, "MOV DWORD PTR [esi-8] should use ESI base");
        failures += expect_u32(buffers.instructions[2].destination.immediate, (uint32_t)-8, "MOV DWORD PTR [esi-8] should preserve -8 byte displacement");
        failures += expect_u32(buffers.instructions[3].destination.immediate, (uint32_t)-16, "MOV DWORD PTR [ebp-10h] should preserve -16 byte displacement");
        failures += expect_u32(buffers.instructions[4].destination.immediate, (uint32_t)-16, "MOV DWORD PTR [ebp-0x10] should preserve -16 byte displacement");
        failures += expect_u32(buffers.instructions[5].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "MOV eax, DWORD PTR [ecx-4] should emit register-derived memory source");
        failures += expect_u32(buffers.instructions[5].source.reg, VM_REGISTER_ECX, "MOV eax, DWORD PTR [ecx-4] should use ECX base");
        failures += expect_u32(buffers.instructions[5].source.immediate, (uint32_t)-4, "MOV eax, DWORD PTR [ecx-4] should preserve -4 byte displacement");
        failures += expect_u32(buffers.instructions[6].source.width_bits, 16U, "MOV ax, WORD PTR [edx-2] should preserve WORD width");
        failures += expect_u32(buffers.instructions[6].source.immediate, (uint32_t)-2, "MOV ax, WORD PTR [edx-2] should preserve -2 byte displacement");
        failures += expect_u32(buffers.instructions[7].source.width_bits, 8U, "MOV al, BYTE PTR [edi-1] should preserve BYTE width");
        failures += expect_u32(buffers.instructions[7].source.immediate, (uint32_t)-1, "MOV al, BYTE PTR [edi-1] should preserve -1 byte displacement");
        failures += expect_u32(buffers.instructions[8].opcode, VM_IR_OPCODE_LEA, "LEA [ebx-4] should emit LEA opcode");
        failures += expect_u32(buffers.instructions[8].source.reg, VM_REGISTER_EBX, "LEA [ebx-4] should use EBX base");
        failures += expect_u32(buffers.instructions[8].source.immediate, (uint32_t)-4, "LEA [ebx-4] should preserve -4 byte displacement");
        failures += expect_u32(buffers.instructions[9].destination.immediate, 0U, "MOV DWORD PTR [eax] should preserve zero displacement");
        failures += expect_u32(buffers.instructions[10].destination.immediate, 4U, "MOV DWORD PTR [eax+4] should preserve compact positive displacement");
        failures += expect_u32(buffers.instructions[11].destination.immediate, 4U, "MOV DWORD PTR [eax + 4] should preserve spaced positive displacement");
        failures += expect_u32(buffers.instructions[12].destination.immediate, (uint32_t)-4, "MOV DWORD PTR [eax - 4] should preserve spaced negative displacement");
        failures += expect_u32(buffers.instructions[13].source.immediate, 4U, "LEA [ebx + 4] should preserve spaced positive displacement");
        failures += expect_u32(buffers.instructions[14].source.immediate, (uint32_t)-4, "LEA [ebx - 4] should preserve spaced negative displacement");
    }

    return failures;
}

/// Verifies Phase 57-CORR2 does not accept advanced register-derived addressing.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57corr2_compact_negative_register_displacement_rejections(void) {
    int failures = 0;
    static const char *const rejected_sources[] = {
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax*4]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax * 4]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax+ebx]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax + ebx]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax+ebx*4]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax + ebx * 4]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax-4*2]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax - 4 * 2]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax-(4)]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax - (4)]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax--4]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax+-4]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax-]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax+]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax-80000000h]\nmain ENDP\nEND main\n",
        ".code\nmain PROC\n    mov eax, DWORD PTR [eax - 80000000h]\nmain ENDP\nEND main\n"
    };
    size_t i = 0U;

    for (i = 0U; i < sizeof(rejected_sources) / sizeof(rejected_sources[0]); ++i) {
        ParserTestBuffers buffers;
        VmParserResult result;
        VmParserStatus status = parse_for_test(rejected_sources[i], &buffers, &result);

        failures += expect_parser_status(status, VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57-CORR2 advanced register-derived address form should remain rejected");
        if (result.diagnostic_count == 0U) {
            failures += record_failure("Phase 57-CORR2 rejected address form should produce a parser diagnostic");
        }
    }

    return failures;
}

/// Verifies Phase 53 MUL parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase53_mul_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "arr DWORD 4 DUP(0)\n"
        ".CONST\n"
        "factor DWORD 20\n"
        ".code\n"
        "main PROC\n"
        "    mul al\n"
        "    mul ax\n"
        "    mul eax\n"
        "    mul BYTE PTR [esi]\n"
        "    mul WORD PTR [esi]\n"
        "    mul DWORD PTR [esi]\n"
        "    mul SBYTE PTR [esi]\n"
        "    mul SWORD PTR [esi]\n"
        "    mul SDWORD PTR [esi]\n"
        "    mul value\n"
        "    mul arr[8]\n"
        "    mul factor\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 53 MUL program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 53 MUL program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 12U, "Phase 53 MUL program should emit twelve instructions");
    if (result.instruction_count == 12U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_MUL, "mul al should emit MUL opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_NONE, "MUL should emit no explicit destination operand");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_REGISTER, "mul al source should be register");
        failures += expect_u32(buffers.instructions[0].source.reg, VM_REGISTER_AL, "mul al source should be AL");
        failures += expect_u32(buffers.instructions[1].source.reg, VM_REGISTER_AX, "mul ax source should be AX");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_EAX, "mul eax source should be EAX");
        failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "mul BYTE PTR [esi] should emit memory source");
        failures += expect_u32(buffers.instructions[3].source.width_bits, 8U, "mul BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[4].source.width_bits, 16U, "mul WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[5].source.width_bits, 32U, "mul DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[6].source.width_bits, 8U, "mul SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[7].source.width_bits, 16U, "mul SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[8].source.width_bits, 32U, "mul SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[9].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "mul value should emit direct memory source");
        failures += expect_u32(buffers.instructions[9].source.width_bits, 32U, "mul value should infer DWORD width");
        failures += expect_u32(buffers.instructions[10].source.width_bits, 32U, "mul arr[8] should infer DWORD width");
        failures += expect_u32(buffers.instructions[11].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "mul factor should accept readable .CONST source");
    }

    return failures;
}

/// Verifies Phase 53A keeps symbol-offset memory operands out of parser object-bound policy.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase53a_symbol_offset_cross_object_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".DATA?\n"
        "x DWORD ?\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    mul [x+1]\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 53A symbol-offset crossing should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 53A symbol-offset crossing should not produce parser diagnostics");
    failures += expect_size(result.instruction_count, 2U, "Phase 53A fixture should emit two instructions");
    if (result.instruction_count == 2U) {
        failures += expect_u32(buffers.instructions[1].opcode, VM_IR_OPCODE_MUL, "Phase 53A fixture should keep MUL opcode");
        failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "mul [x+1] should lower to absolute memory source");
        failures += expect_u32(buffers.instructions[1].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 1U, "mul [x+1] should preserve byte-offset address");
        failures += expect_u32(buffers.instructions[1].source.width_bits, 32U, "mul [x+1] should infer DWORD width from x");
    }

    return failures;
}

/// Verifies Phase 53 MUL parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase53_mul_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    mul 5\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "MUL immediate source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "MUL immediate source should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory source", "MUL immediate diagnostic should explain source requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    mul eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "MUL two-operand form should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "MUL two-operand form should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly one", "MUL two-operand diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    mul [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "MUL ambiguous memory source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "MUL ambiguous memory diagnostic should be ambiguous-memory-width");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "MUL ambiguous memory diagnostic should point at the memory operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    mul\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "MUL missing operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "MUL missing operand should use expected-operand");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    mul eax, ebx, ecx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "MUL extra operands should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "MUL extra operands should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mul QWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "MUL QWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "MUL QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mul SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "MUL SQWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "MUL SQWORD PTR should remain unsupported executable width");

    return failures;
}


/// Verifies Phase 56 DIV parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase56_div_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "arr DWORD 4 DUP(0)\n"
        ".CONST\n"
        "factor DWORD 20\n"
        ".code\n"
        "main PROC\n"
        "    div al\n"
        "    div ax\n"
        "    div eax\n"
        "    div BYTE PTR [esi]\n"
        "    div WORD PTR [esi]\n"
        "    div DWORD PTR [esi]\n"
        "    div SBYTE PTR [esi]\n"
        "    div SWORD PTR [esi]\n"
        "    div SDWORD PTR [esi]\n"
        "    div value\n"
        "    div arr[8]\n"
        "    div factor\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 56 DIV program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 56 DIV program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 12U, "Phase 56 DIV program should emit twelve instructions");
    if (result.instruction_count == 12U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_DIV, "div al should emit DIV opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_NONE, "DIV should emit no explicit destination operand");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_REGISTER, "div al source should be register");
        failures += expect_u32(buffers.instructions[0].source.reg, VM_REGISTER_AL, "div al source should be AL");
        failures += expect_u32(buffers.instructions[1].source.reg, VM_REGISTER_AX, "div ax source should be AX");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_EAX, "div eax source should be EAX");
        failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "div BYTE PTR [esi] should emit memory source");
        failures += expect_u32(buffers.instructions[3].source.width_bits, 8U, "div BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[4].source.width_bits, 16U, "div WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[5].source.width_bits, 32U, "div DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[6].source.width_bits, 8U, "div SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[7].source.width_bits, 16U, "div SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[8].source.width_bits, 32U, "div SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[9].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "div value should emit direct memory source");
        failures += expect_u32(buffers.instructions[9].source.width_bits, 32U, "div value should infer DWORD width");
        failures += expect_u32(buffers.instructions[10].source.width_bits, 32U, "div arr[8] should infer DWORD width");
        failures += expect_u32(buffers.instructions[11].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "div factor should accept readable .CONST source");
    }

    return failures;
}

/// Verifies Phase 56 DIV parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase56_div_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    div 5\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DIV immediate source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "DIV immediate source should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory source", "DIV immediate diagnostic should explain source requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    div eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DIV two-operand form should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "DIV two-operand form should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly one", "DIV two-operand diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    div [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DIV ambiguous memory source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "DIV ambiguous memory diagnostic should be ambiguous-memory-width");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    div\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DIV missing operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "DIV missing operand should use expected-operand");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    div QWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DIV QWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "DIV QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    div SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DIV SQWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "DIV SQWORD PTR should remain unsupported executable width");

    return failures;
}


/// Verifies Phase 57 IDIV parser acceptance and IR shapes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57_idiv_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value SDWORD -1\n"
        "arr SDWORD 4 DUP(0)\n"
        ".CONST\n"
        "factor SDWORD -7\n"
        ".code\n"
        "main PROC\n"
        "    idiv al\n"
        "    idiv ax\n"
        "    idiv eax\n"
        "    idiv BYTE PTR [esi]\n"
        "    idiv WORD PTR [esi]\n"
        "    idiv DWORD PTR [esi]\n"
        "    idiv SBYTE PTR [esi]\n"
        "    idiv SWORD PTR [esi]\n"
        "    idiv SDWORD PTR [esi]\n"
        "    idiv value\n"
        "    idiv arr[8]\n"
        "    idiv factor\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 57 IDIV program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 57 IDIV program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 12U, "Phase 57 IDIV program should emit twelve instructions");
    if (result.instruction_count == 12U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_IDIV, "idiv al should emit IDIV opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_NONE, "IDIV should emit no explicit destination operand");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_REGISTER, "idiv al source should be register");
        failures += expect_u32(buffers.instructions[0].source.reg, VM_REGISTER_AL, "idiv al source should be AL");
        failures += expect_u32(buffers.instructions[1].source.reg, VM_REGISTER_AX, "idiv ax source should be AX");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_EAX, "idiv eax source should be EAX");
        failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "idiv BYTE PTR [esi] should emit memory source");
        failures += expect_u32(buffers.instructions[3].source.width_bits, 8U, "idiv BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[4].source.width_bits, 16U, "idiv WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[5].source.width_bits, 32U, "idiv DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[6].source.width_bits, 8U, "idiv SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[7].source.width_bits, 16U, "idiv SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[8].source.width_bits, 32U, "idiv SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[9].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "idiv value should emit direct memory source");
        failures += expect_u32(buffers.instructions[9].source.width_bits, 32U, "idiv value should infer SDWORD width");
        failures += expect_u32(buffers.instructions[10].source.width_bits, 32U, "idiv arr[8] should infer SDWORD width");
        failures += expect_u32(buffers.instructions[11].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "idiv factor should accept readable .CONST source");
    }

    return failures;
}

/// Verifies Phase 57 IDIV parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57_idiv_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    idiv 5\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IDIV immediate source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "IDIV immediate source should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory source", "IDIV immediate diagnostic should explain source requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    idiv eax, ebx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IDIV two-operand form should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "IDIV two-operand form should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "exactly one", "IDIV two-operand diagnostic should explain operand count");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    idiv [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IDIV ambiguous memory source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "IDIV ambiguous memory diagnostic should be ambiguous-memory-width");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    idiv\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IDIV missing operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "IDIV missing operand should use expected-operand");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    idiv QWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IDIV QWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "IDIV QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    idiv SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IDIV SQWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "IDIV SQWORD PTR should remain unsupported executable width");

    return failures;
}

/// Verifies Phase 54 one-operand signed IMUL parser support.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase54_imul_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "value SDWORD -3\n"
        "arr DWORD 4 DUP(0)\n"
        ".CONST\n"
        "factor DWORD 20\n"
        ".code\n"
        "main PROC\n"
        "    imul al\n"
        "    imul ax\n"
        "    imul eax\n"
        "    imul BYTE PTR [esi]\n"
        "    imul WORD PTR [esi]\n"
        "    imul DWORD PTR [esi]\n"
        "    imul SBYTE PTR [esi]\n"
        "    imul SWORD PTR [esi]\n"
        "    imul SDWORD PTR [esi]\n"
        "    imul value\n"
        "    imul arr[8]\n"
        "    imul factor\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 54 IMUL program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 54 IMUL program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 12U, "Phase 54 IMUL program should emit twelve instructions");
    if (result.instruction_count == 12U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_IMUL, "imul al should emit IMUL opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_NONE, "IMUL should emit no explicit destination operand");
        failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_REGISTER, "imul al source should be register");
        failures += expect_u32(buffers.instructions[0].source.reg, VM_REGISTER_AL, "imul al source should be AL");
        failures += expect_u32(buffers.instructions[1].source.reg, VM_REGISTER_AX, "imul ax source should be AX");
        failures += expect_u32(buffers.instructions[2].source.reg, VM_REGISTER_EAX, "imul eax source should be EAX");
        failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "imul BYTE PTR [esi] should emit memory source");
        failures += expect_u32(buffers.instructions[3].source.width_bits, 8U, "imul BYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[4].source.width_bits, 16U, "imul WORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[5].source.width_bits, 32U, "imul DWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[6].source.width_bits, 8U, "imul SBYTE PTR [esi] should use 8-bit width");
        failures += expect_u32(buffers.instructions[7].source.width_bits, 16U, "imul SWORD PTR [esi] should use 16-bit width");
        failures += expect_u32(buffers.instructions[8].source.width_bits, 32U, "imul SDWORD PTR [esi] should use 32-bit width");
        failures += expect_u32(buffers.instructions[9].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "imul value should emit direct memory source");
        failures += expect_u32(buffers.instructions[9].source.width_bits, 32U, "imul value should infer SDWORD width");
        failures += expect_u32(buffers.instructions[10].source.width_bits, 32U, "imul arr[8] should infer DWORD width");
        failures += expect_u32(buffers.instructions[11].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "imul factor should accept readable .CONST source");
    }

    return failures;
}

/// Verifies Phase 54 one-operand signed IMUL parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase54_imul_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul 5\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL immediate source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "IMUL immediate source should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "register or memory source", "IMUL immediate diagnostic should explain source requirement");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL ambiguous memory source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "IMUL ambiguous memory diagnostic should be ambiguous-memory-width");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL missing operand should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "IMUL missing operand should use expected-operand");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    imul QWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL QWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "IMUL QWORD PTR should remain unsupported executable width");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    imul SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL SQWORD source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "IMUL SQWORD PTR should remain unsupported executable width");

    return failures;
}

/// Verifies Phase 55 explicit-destination IMUL parser support.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase55_imul_parse_to_ir(void) {
    int failures = 0;
    const char *source =
        ".data\n"
        "factor SDWORD -3\n"
        "values SDWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    imul ax, bx\n"
        "    imul eax, ebx\n"
        "    imul ax, WORD PTR [esi]\n"
        "    imul eax, DWORD PTR [esi]\n"
        "    imul eax, SDWORD PTR [esi]\n"
        "    imul eax, factor\n"
        "    imul eax, values[4]\n"
        "    imul ax, bx, -5\n"
        "    imul eax, ebx, -5\n"
        "    imul eax, DWORD PTR [esi], -5\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    VmParserStatus status = parse_for_test(source, &buffers, &result);

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 55 IMUL program should parse successfully");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 55 IMUL program should not produce diagnostics");
    failures += expect_size(result.instruction_count, 10U, "Phase 55 IMUL program should emit ten instructions");
    if (result.instruction_count == 10U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_IMUL, "imul ax, bx should emit IMUL opcode");
        failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_REGISTER, "two-operand IMUL should have register destination");
        failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_AX, "imul ax, bx destination should be AX");
        failures += expect_u32(buffers.instructions[0].source.reg, VM_REGISTER_BX, "imul ax, bx source should be BX");
        failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_EAX, "imul eax, ebx destination should be EAX");
        failures += expect_u32(buffers.instructions[1].source.reg, VM_REGISTER_EBX, "imul eax, ebx source should be EBX");
        failures += expect_u32(buffers.instructions[2].source.width_bits, 16U, "imul ax, WORD PTR [esi] should use 16-bit memory source");
        failures += expect_u32(buffers.instructions[3].source.width_bits, 32U, "imul eax, DWORD PTR [esi] should use 32-bit memory source");
        failures += expect_u32(buffers.instructions[4].source.width_bits, 32U, "imul eax, SDWORD PTR [esi] should use signed PTR alias width");
        failures += expect_u32(buffers.instructions[5].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "imul eax, factor should emit direct memory source");
        failures += expect_u32(buffers.instructions[6].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "imul eax, values[4] should emit symbol-offset memory source");
        failures += expect_u32(buffers.instructions[7].opcode, VM_IR_OPCODE_IMUL_IMMEDIATE, "three-operand IMUL should emit immediate opcode");
        failures += expect_u32(buffers.instructions[7].destination.immediate, 0x0000FFFBU, "16-bit immediate should be encoded as operand-width signed value bits");
        failures += expect_u32(buffers.instructions[8].destination.immediate, 0xFFFFFFFBU, "32-bit negative immediate should be encoded as signed value bits");
        failures += expect_u32(buffers.instructions[9].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "three-operand memory-source IMUL should keep memory source");
    }

    return failures;
}

/// Verifies Phase 55 explicit-destination IMUL parser diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase55_imul_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul al, bl\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "8-bit two-operand IMUL should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "8-bit two-operand IMUL should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul DWORD PTR [esi], eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "memory-destination IMUL should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "memory-destination IMUL should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul eax, 5\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL reg, imm should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "IMUL reg, imm should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "This IMUL form is not accepted", "IMUL reg, imm diagnostic should use stable simulator boundary wording");
    failures += expect_size(buffers.diagnostics[0].location.line, 3U, "IMUL reg, imm diagnostic should preserve source line");
    failures += expect_size(buffers.diagnostics[0].location.column, 15U, "IMUL reg, imm diagnostic should preserve immediate column");
    failures += expect_size(buffers.diagnostics[0].location.offset, 30U, "IMUL reg, imm diagnostic should preserve immediate byte offset");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 1U, "IMUL reg, imm diagnostic should span immediate token");
    failures += expect_no_phase71b_forbidden_diagnostic_wording(buffers.diagnostics[0].message, "IMUL reg, imm diagnostic must not use milestone-relative wording");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul eax, ebx, ecx\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL reg, reg, reg should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "IMUL third register operand should use invalid-instruction-operands");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul eax, ebx, 5, 6\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL extra operands should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "IMUL extra operand diagnostic should use invalid-instruction-operands");
    failures += expect_string_contains(buffers.diagnostics[0].message, "takes exactly three operands", "IMUL extra operand diagnostic should explain operand count");
    failures += expect_u32(buffers.diagnostics[0].location.line, 3U, "IMUL extra operand diagnostic should preserve line");
    failures += expect_u32(buffers.diagnostics[0].location.column, 21U, "IMUL extra operand diagnostic should point at extra comma");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul eax, ebx, 2147483648\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL out-of-range 32-bit immediate should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "IMUL out-of-range 32-bit immediate should use immediate-out-of-range");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    imul ax, bx, 32768\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL out-of-range 16-bit immediate should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "IMUL out-of-range 16-bit immediate should use immediate-out-of-range");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    imul eax, QWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "IMUL QWORD explicit source should produce parser diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "IMUL QWORD explicit source should remain unsupported executable width");

    return failures;
}




/// Verifies one source sample reports the Phase 57M unsupported segment/group symbol diagnostic.
///
/// @param source MASM-like source text expected to use an unsupported segment/group symbol.
/// @param expected_message_fragment Fragment expected in the diagnostic message.
/// @param expected_symbol_count Expected number of ordinary data symbols after recovery.
/// @return Zero on success, otherwise a positive failure count.
static int expect_phase57m_segment_symbol_diagnostic(
    const char *source,
    const char *expected_message_fragment,
    size_t expected_symbol_count
) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57M segment/group symbol should produce parser diagnostics");
    if (result.diagnostic_count < 1U) {
        failures += record_failure("Phase 57M segment/group symbol should produce at least one diagnostic");
        return failures;
    }
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SEGMENT_SYMBOL, "Phase 57M diagnostic code should be unsupported-segment-symbol");
    failures += expect_parser_diagnostic_severity(buffers.diagnostics[0].severity, VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR, "Phase 57M diagnostic should block execution");
    failures += expect_string_contains(buffers.diagnostics[0].message, expected_message_fragment, "Phase 57M diagnostic message should explain segment/group concept");
    failures += expect_size(result.symbol_count, expected_symbol_count, "Phase 57M rejected symbol should not create an ordinary data symbol");

    return failures;
}

/// Verifies Phase 57M targeted diagnostics for exact segment/group references.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57m_segment_symbol_reference_diagnostics(void) {
    int failures = 0;

    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET _TEXT\nmain ENDP\nEND main\n",
        "MASM/object segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET _DATA\nmain ENDP\nEND main\n",
        "MASM/object data-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET _BSS\nmain ENDP\nEND main\n",
        "MASM/object uninitialized-data segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET CONST\nmain ENDP\nEND main\n",
        "MASM/object constant-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET STACK\nmain ENDP\nEND main\n",
        "MASM/object stack-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET DGROUP\nmain ENDP\nEND main\n",
        "MASM memory-model group concept",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET FLAT\nmain ENDP\nEND main\n",
        "MASM memory-model group concept",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, DWORD PTR [_TEXT]\nmain ENDP\nEND main\n",
        "MASM/object segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, DWORD PTR [_DATA]\nmain ENDP\nEND main\n",
        "MASM/object data-segment symbol",
        0U
    );

    return failures;
}

/// Verifies Phase 57M targeted diagnostics for segment and group definition forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57m_segment_symbol_definition_diagnostics(void) {
    int failures = 0;

    failures += expect_phase57m_segment_symbol_diagnostic(
        "_TEXT SEGMENT\n_TEXT ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM/object segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "_TEXT ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM/object segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "_DATA SEGMENT\n_DATA ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM/object data-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "_DATA ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM/object data-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "CONST SEGMENT\nCONST ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM/object constant-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "_BSS SEGMENT\n_BSS ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM/object uninitialized-data segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "STACK SEGMENT\nSTACK ENDS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM/object stack-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "DGROUP GROUP _DATA, _BSS\n.code\nmain PROC\nmain ENDP\nEND main\n",
        "MASM memory-model group concept",
        0U
    );

    return failures;
}

/// Verifies Phase 57M CASEMAP behavior and ordinary symbol preservation.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57m_segment_symbol_casemap_and_regressions(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET _text\nmain ENDP\nEND main\n",
        "MASM/object segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "OPTION CASEMAP:ALL\n.code\nmain PROC\n    mov eax, OFFSET _data\nmain ENDP\nEND main\n",
        "MASM/object data-segment symbol",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        ".code\nmain PROC\n    mov eax, OFFSET dgroup\nmain ENDP\nEND main\n",
        "MASM memory-model group concept",
        0U
    );
    failures += expect_phase57m_segment_symbol_diagnostic(
        "OPTION CASEMAP:NONE\n.code\nmain PROC\n    mov eax, OFFSET _TEXT\nmain ENDP\nEND main\n",
        "MASM/object segment symbol",
        0U
    );

    failures += expect_parser_status(parse_for_test(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "_text DWORD 77\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET _text\n"
        "    mov ebx, _text\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "CASEMAP:NONE should permit non-exact segment-like ordinary symbols");
    failures += expect_size(result.diagnostic_count, 0U, "CASEMAP:NONE non-exact segment-like symbol should not diagnose");
    failures += expect_size(result.symbol_count, 1U, "CASEMAP:NONE non-exact segment-like data label should be inserted");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "_TEXT DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:ALL data-label collision should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SEGMENT_SYMBOL, "CASEMAP:ALL segment-like data label should use unsupported-segment-symbol");
    failures += expect_size(result.symbol_count, 0U, "CASEMAP:ALL segment-like data label should not be inserted");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET value\n"
        "    mov ebx, value\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "ordinary non-colliding data label should still parse");
    failures += expect_size(result.diagnostic_count, 0U, "ordinary non-colliding data label should produce no diagnostics");

    return failures;
}

/// Verifies metadata helper behavior.
///
/// @return Zero on success, otherwise a positive failure count.
/// Verifies Phase 72A source-level PUSH and POP accepted forms parse to IR.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72a_push_pop_parse_to_ir(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *source =
        ".data\n"
        "value DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    push eax\n"
        "    push 1234h\n"
        "    push -1\n"
        "    push DWORD PTR value\n"
        "    push DWORD PTR [esp]\n"
        "    pop eax\n"
        "    pop esp\n"
        "    pop DWORD PTR value\n"
        "    pop DWORD PTR [esp]\n"
        "main ENDP\n"
        "END main\n";

    memset(&result, 0, sizeof(result));
    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 72A PUSH/POP accepted forms should parse");
    failures += expect_size(result.instruction_count, 9U, "Phase 72A accepted source should emit nine instructions");
    if (result.instruction_count >= 9U) {
        failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_PUSH, "push eax should emit PUSH");
        failures += expect_u32((uint32_t)buffers.instructions[1].opcode, (uint32_t)VM_IR_OPCODE_PUSH, "push immediate should emit PUSH");
        failures += expect_u32(buffers.instructions[1].source.width_bits, 32U, "push immediate should normalize to 32-bit width");
        failures += expect_u32((uint32_t)buffers.instructions[4].opcode, (uint32_t)VM_IR_OPCODE_PUSH, "push DWORD PTR [esp] should emit PUSH");
        failures += expect_u32((uint32_t)buffers.instructions[5].opcode, (uint32_t)VM_IR_OPCODE_POP, "pop eax should emit POP");
        failures += expect_u32((uint32_t)buffers.instructions[6].opcode, (uint32_t)VM_IR_OPCODE_POP, "pop esp should emit POP");
        failures += expect_u32((uint32_t)buffers.instructions[8].opcode, (uint32_t)VM_IR_OPCODE_POP, "pop DWORD PTR [esp] should emit POP");
    }

    return failures;
}

/// Verifies Phase 72A rejects unsupported PUSH and POP forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72a_push_pop_parse_error_paths(void) {
    int failures = 0;
    size_t index = 0U;
    static const struct {
        const char *source_line;
        VmParserDiagnosticCode expected_code;
        const char *message_fragment;
    } cases[] = {
        {"push", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "one operand"},
        {"pop", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "one operand"},
        {"push ax", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"push al", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"push BYTE PTR [esp]", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"push WORD PTR [esp]", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"push QWORD PTR value", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, ""},
        {"push SQWORD PTR value", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, ""},
        {"push FAR PTR value", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, ""},
        {"pop ax", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"pop al", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"pop BYTE PTR [esp]", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"pop WORD PTR [esp]", VM_PARSER_DIAGNOSTIC_INVALID_OPERAND_SIZE, "32-bit"},
        {"pop QWORD PTR value", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, ""},
        {"pop SQWORD PTR value", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, ""},
        {"pop FAR PTR value", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, ""},
        {"pop 1234h", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, ""},
        {"pop OFFSET value", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, ""},
        {"pop [esp]", VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, ""}
    };

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index += 1U) {
        ParserTestBuffers buffers;
        VmParserResult result;
        char source[256];
        memset(&result, 0, sizeof(result));
        (void)snprintf(
            source,
            sizeof(source),
            ".data\nvalue DWORD 0\n.code\nmain PROC\n    %s\nmain ENDP\nEND main\n",
            cases[index].source_line
        );
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "invalid PUSH/POP form should produce diagnostic status");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, cases[index].expected_code, "invalid PUSH/POP diagnostic code should match");
        failures += expect_string_contains(buffers.diagnostics[0].message, cases[index].message_fragment, "invalid PUSH/POP diagnostic wording should explain the rejected form");
    }

    return failures;
}

/// Verifies Phase 73 LEAVE accepted and rejected parser forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase73_leave_parse_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *accepted_source =
        ".code\n"
        "main PROC\n"
        "    leave\n"
        "    LEAVE\n"
        "main ENDP\n"
        "END main\n";
    static const struct {
        const char *source_line;
        VmParserDiagnosticCode expected_code;
        const char *message_fragment;
    } rejected_cases[] = {
        {"leave eax", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "does not take operands"},
        {"leave eax, ebx", VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "does not take operands"},
        {"enter 8, 0", VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION, "Unsupported instruction"}
    };
    size_t index = 0U;

    memset(&result, 0, sizeof(result));
    failures += expect_parser_status(parse_for_test(accepted_source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 73 LEAVE accepted forms should parse");
    failures += expect_size(result.instruction_count, 2U, "Phase 73 accepted source should emit two LEAVE instructions");
    if (result.instruction_count >= 2U) {
        failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_LEAVE, "leave should emit LEAVE opcode");
        failures += expect_u32((uint32_t)buffers.instructions[1].opcode, (uint32_t)VM_IR_OPCODE_LEAVE, "uppercase LEAVE should emit LEAVE opcode");
    }

    for (index = 0U; index < sizeof(rejected_cases) / sizeof(rejected_cases[0]); index += 1U) {
        char source[256];
        memset(&result, 0, sizeof(result));
        (void)snprintf(
            source,
            sizeof(source),
            ".code\nmain PROC\n    %s\nmain ENDP\nEND main\n",
            rejected_cases[index].source_line
        );
        failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "invalid LEAVE/future frame form should produce diagnostic status");
        failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, rejected_cases[index].expected_code, "invalid LEAVE/future frame diagnostic code should match");
        failures += expect_string_contains(buffers.diagnostics[0].message, rejected_cases[index].message_fragment, "invalid LEAVE/future frame diagnostic wording should explain the rejected form");
    }

    return failures;
}

static int test_metadata_helpers(void) {
    int failures = 0;

    if (strcmp(vm_parser_status_name(VM_PARSER_STATUS_OK), "ok") != 0) {
        failures += record_failure("parser status helper should name OK");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION), "unsupported-instruction") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported-instruction");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION_FORM), "unsupported-instruction-form") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported-instruction-form");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE), "unsupported-feature") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported-feature");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNKNOWN_INSTRUCTION), "unknown-instruction") != 0) {
        failures += record_failure("parser diagnostic helper should name unknown-instruction");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_INVALID_EFFECTIVE_ADDRESS_EXPRESSION), "invalid-effective-address-expression") != 0) {
        failures += record_failure("parser diagnostic helper should name invalid effective-address diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS), "invalid-instruction-operands") != 0) {
        failures += record_failure("parser diagnostic helper should name invalid-instruction-operands");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SEGMENT_SYMBOL), "unsupported-segment-symbol") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported-segment-symbol");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_LEXER_INVALID_HEX_LITERAL), "invalid-hex-literal") != 0) {
        failures += record_failure("parser diagnostic helper should name surfaced lexer invalid hex diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH), "ambiguous-memory-width") != 0) {
        failures += record_failure("parser diagnostic helper should name ambiguous memory-width diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MODEL), "unsupported-model") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported .model diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDE), "unsupported-include") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported include diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HOST_INCLUDE_PATH), "unsupported-host-include-path") != 0) {
        failures += record_failure("parser diagnostic helper should name host include path diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_INCLUDE), "unsupported-windows-api-include") != 0) {
        failures += record_failure("parser diagnostic helper should name Windows/API include diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY_INCLUDE), "unsupported-masm32-library-include") != 0) {
        failures += record_failure("parser diagnostic helper should name MASM32 SDK include diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INCLUDELIB), "unsupported-includelib") != 0) {
        failures += record_failure("parser diagnostic helper should name INCLUDELIB diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_WINDOWS_API_LIBRARY), "unsupported-windows-api-library") != 0) {
        failures += record_failure("parser diagnostic helper should name Windows/API library diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_MASM32_LIBRARY), "unsupported-masm32-library") != 0) {
        failures += record_failure("parser diagnostic helper should name MASM32 library diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_IF), "unsupported-high-level-if") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported high-level IF diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ELSE), "unsupported-high-level-else") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported high-level ELSE diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_ENDIF), "unsupported-high-level-endif") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported high-level ENDIF diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_WHILE), "unsupported-high-level-while") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported high-level WHILE diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_REPEAT), "unsupported-high-level-repeat") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported high-level REPEAT diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_HIGH_LEVEL_FLOW), "unsupported-high-level-flow") != 0) {
        failures += record_failure("parser diagnostic helper should name generic unsupported high-level flow diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION), "unsupported-option") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported option diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE), "unsupported-register-indirect-base") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported register-indirect base diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_CONST_WRITE), "const-write") != 0) {
        failures += record_failure("parser diagnostic helper should name const-write diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL), "reserved-word-symbol") != 0) {
        failures += record_failure("parser diagnostic helper should name reserved-word symbol diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL), "ambiguous-symbol") != 0) {
        failures += record_failure("parser diagnostic helper should name ambiguous symbol diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED), "casemap-policy-changed") != 0) {
        failures += record_failure("parser diagnostic helper should name CASEMAP policy warning diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_INVALID_OPTION_VALUE), "invalid-option-value") != 0) {
        failures += record_failure("parser diagnostic helper should name invalid option value diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE), "unsupported-irvine32-routine") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported Irvine32 routine diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_COMPATIBILITY_NO_OP), "compatibility-no-op") != 0) {
        failures += record_failure("parser diagnostic helper should name compatibility no-op notices");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_COMPATIBILITY_METADATA_ONLY), "compatibility-metadata-only") != 0) {
        failures += record_failure("parser diagnostic helper should name compatibility metadata-only notices");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_COMPATIBILITY_LIMITED), "compatibility-limited") != 0) {
        failures += record_failure("parser diagnostic helper should name compatibility limited-behavior notices");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_INVALID_PROCEDURE_NAME), "invalid-procedure-name") != 0) {
        failures += record_failure("parser diagnostic helper should name invalid procedure-name diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CALL_TARGET), "unsupported-call-target") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported call-target diagnostics");
    }
    if (strcmp(vm_parser_irvine32_symbol_class_name(VM_IRVINE32_SYMBOL_CLASS_SUPPORTED_VIRTUAL_INTRINSIC), "supported-virtual-intrinsic") != 0) {
        failures += record_failure("Irvine32 symbol-class helper should name supported virtual intrinsics");
    }
    if (strcmp(vm_parser_irvine32_symbol_class_name(VM_IRVINE32_SYMBOL_CLASS_PLANNED_ROUTINE), "planned-routine") != 0) {
        failures += record_failure("Irvine32 symbol-class helper should name planned routines");
    }
    if (strcmp(vm_parser_irvine32_symbol_class_name(VM_IRVINE32_SYMBOL_CLASS_UNSUPPORTED_ROUTINE), "unsupported-routine") != 0) {
        failures += record_failure("Irvine32 symbol-class helper should name unsupported routines");
    }
    if (strcmp(vm_parser_irvine32_symbol_class_name(VM_IRVINE32_SYMBOL_CLASS_WINDOWS_API_OR_EXTERNAL), "windows-api-or-external") != 0) {
        failures += record_failure("Irvine32 symbol-class helper should name Windows/API external routines");
    }
    if (strcmp(vm_parser_irvine32_symbol_class_name(VM_IRVINE32_SYMBOL_CLASS_UNKNOWN), "unknown") != 0) {
        failures += record_failure("Irvine32 symbol-class helper should name unknown symbols");
    }
    if (vm_parser_irvine32_symbol_class_name((VmIrvine32SymbolClass)999) != NULL) {
        failures += record_failure("invalid Irvine32 symbol class name should be NULL");
    }
    if (strcmp(vm_parser_diagnostic_severity_name(VM_PARSER_DIAGNOSTIC_SEVERITY_NOTICE), "notice") != 0) {
        failures += record_failure("parser diagnostic helper should name notice severity");
    }
    if (strcmp(vm_parser_diagnostic_severity_name(VM_PARSER_DIAGNOSTIC_SEVERITY_WARNING), "warning") != 0) {
        failures += record_failure("parser diagnostic helper should name warning severity");
    }
    if (strcmp(vm_parser_diagnostic_severity_name(VM_PARSER_DIAGNOSTIC_SEVERITY_ERROR), "error") != 0) {
        failures += record_failure("parser diagnostic helper should name error severity");
    }
    if (vm_parser_status_name((VmParserStatus)999) != NULL) {
        failures += record_failure("invalid parser status name should be NULL");
    }
    if (vm_parser_diagnostic_code_name((VmParserDiagnosticCode)999) != NULL) {
        failures += record_failure("invalid parser diagnostic code name should be NULL");
    }

    return failures;
}

/// Runs all parser regression tests through Phase 60 direct JMP parsing and target lowering.
///
/// @return Zero on success, otherwise one.

/// Verifies Phase 60 direct JMP lowers code-label and procedure-entry targets.
///
/// @return Number of failures.
static int test_phase60_jmp_parse_to_ir(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    jmp target\n"
        "target:\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "other PROC\n"
        "    mov ebx, 2\n"
        "other ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 60 direct JMP source should parse");
    failures += expect_size(result.instruction_count, 3U, "Phase 60 source should emit JMP plus two MOV instructions");
    failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_JMP, "first instruction should be lowered JMP");
    failures += expect_u32((uint32_t)buffers.instructions[0].destination.kind, (uint32_t)VM_IR_OPERAND_BRANCH_TARGET, "JMP destination should be a branch target operand");
    failures += expect_u32(buffers.instructions[0].destination.immediate, 1U, "JMP should target the target label MOV instruction index");

    return failures;
}

/// Verifies Phase 60 direct JMP accepts procedure-entry labels as direct targets only.
///
/// @return Number of failures.
static int test_phase60_jmp_procedure_entry_target(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    jmp other\n"
        "main ENDP\n"
        "other PROC\n"
        "    mov eax, 7\n"
        "other ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "JMP to procedure entry should parse");
    failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_JMP, "first instruction should be JMP");
    failures += expect_u32(buffers.instructions[0].destination.immediate, 1U, "procedure-entry JMP should target the first executable instruction in the target procedure");

    return failures;
}

/// Verifies Phase 60 direct JMP target-class diagnostics.
///
/// @return Number of failures.
static int test_phase60_jmp_target_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    jmp value\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP to data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "data-symbol JMP target should use invalid-branch-target");
    failures += expect_u32(buffers.diagnostics[0].location.line, 5U, "data-symbol target diagnostic line should point at operand");
    failures += expect_u32(buffers.diagnostics[0].location.column, 9U, "data-symbol target diagnostic column should point at operand");

    failures += expect_parser_status(parse_for_test(
        "COUNT = 1\n"
        ".code\n"
        "main PROC\n"
        "    jmp COUNT\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP to equate should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "equate JMP target should use invalid-branch-target");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    jmp missing\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP to unknown label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "unknown JMP target should use invalid-branch-target");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "empty:\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "No-target label declaration alone should remain valid");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    jmp empty\n"
        "empty:\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP to no-target label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "no-target JMP label should use invalid-branch-target");

    return failures;
}

/// Verifies Phase 60 direct JMP rejects non-direct branch operand forms.
///
/// @return Number of failures.
static int test_phase60_jmp_form_rejections(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp eax\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP register target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "register JMP target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp DWORD PTR [eax]\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP memory target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "memory JMP target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp SHORT target\ntarget:\n    mov eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP SHORT distance override should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "SHORT distance override should use unsupported branch form");
    failures += expect_string_contains(buffers.diagnostics[0].message, "deferred to a later branch phase", "distance override diagnostic should use stable future-phase wording");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp NEAR PTR target\ntarget:\n    mov eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP NEAR PTR distance override should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "NEAR PTR distance override should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp FAR PTR target\ntarget:\n    mov eax, 1\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP FAR PTR distance override should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "FAR PTR distance override should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp 42\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP immediate numeric target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "immediate JMP target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp .data\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP directive-name target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "directive JMP target should use invalid branch target");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp mov\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP instruction-name target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "instruction-name JMP target should use invalid branch target");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP with missing target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "missing JMP target should use expected-operand");

    failures += expect_parser_status(parse_for_test(
        "INCLUDE Irvine32.inc\n.code\nmain PROC\n    jmp exit\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP to Irvine32 exit should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "Irvine32 JMP target should use invalid branch target");

    failures += expect_parser_status(parse_for_test(
        ".code\nmain PROC\n    jmp ExitProcess\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JMP to Windows/API external symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "Windows/API external JMP target should use invalid branch target");

    return failures;
}

/// Verifies Phase 61E rejects simulator-recognized reserved words as user symbols.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase61e_reserved_word_symbol_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "loop:\n"
        "    inc eax\n"
        "    jmp loop\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "reserved instruction mnemonic label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "loop label should use reserved-word-symbol");
    failures += expect_u32(buffers.diagnostics[0].location.line, 3U, "reserved label diagnostic should point at declaration line");
    failures += expect_u32(buffers.diagnostics[0].location.column, 1U, "reserved label diagnostic should point at declaration name column");
    failures += expect_size(buffers.diagnostics[0].lexeme_length, 4U, "reserved label diagnostic span should cover only loop");
    failures += expect_string_contains(buffers.diagnostics[0].message, "reserved MASM instruction mnemonic", "reserved label message should classify instruction mnemonic");
    failures += expect_size(result.code_label_count, 1U, "reserved loop label should not be inserted; only main PROC remains");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nLOOP:\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "uppercase LOOP label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "uppercase LOOP label should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nLoop:\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "mixed-case Loop label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "mixed-case Loop label should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test("OPTION CASEMAP:NONE\n.code\nmain PROC\nloop:\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:NONE should not permit reserved labels");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "CASEMAP:NONE reserved label should use reserved-word-symbol");
    if (strstr(buffers.diagnostics[0].message, "CASEMAP") != NULL) {
        failures += record_failure("reserved-word message should not blame CASEMAP");
    }

    failures += expect_parser_status(parse_for_test(".data\nmov DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "instruction mnemonic data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "mov data symbol should use reserved-word-symbol");
    failures += expect_size(result.symbol_count, 0U, "reserved data symbol should not be inserted");

    failures += expect_parser_status(parse_for_test(".data\neax DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "register data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "eax data symbol should use reserved-word-symbol");
    failures += expect_string_contains(buffers.diagnostics[0].message, "reserved MASM register name", "register data symbol should classify register name");

    failures += expect_parser_status(parse_for_test(".data\nDWORD DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "data type name data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "DWORD data symbol should use reserved-word-symbol");
    failures += expect_string_contains(buffers.diagnostics[0].message, "reserved MASM data type name", "data type symbol should classify data type");

    failures += expect_parser_status(parse_for_test(".data\nOFFSET DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "operator data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "OFFSET data symbol should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test("add EQU 2\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "instruction mnemonic equate should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "add equate should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".data\nPROC DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "procedure directive data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "PROC data symbol should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".code\nloop PROC\nloop ENDP\nEND loop\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "reserved procedure name should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "reserved procedure name should use reserved-word-symbol");
    failures += expect_size(result.code_label_count, 0U, "reserved procedure name should not be inserted");

    failures += expect_parser_status(parse_for_test("INCLUDE Irvine32.inc\n.code\nmain PROC\nexit:\n    nop\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Irvine32 registry label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "Irvine32 registry label should use reserved-word-symbol");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Irvine32 registry name", "Irvine32 label should use registry classification");

    failures += expect_parser_status(parse_for_test(".data\ncmp DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP data symbol should diagnose after Phase 62");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "CMP data symbol should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\ncmp:\n    nop\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP code label should diagnose after Phase 62");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "CMP code label should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".data\njl DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 65 signed jump mnemonic data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "JL data symbol should use reserved-word-symbol");
    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nJGE:\n    nop\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 65 signed jump mnemonic code label should diagnose case-insensitively");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "JGE code label should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(
        ".data\nloopCount DWORD 0\nagain DWORD 1\n.code\nmain PROC\n    mov eax, 0\nagain_label:\n    inc eax\n    jmp again_label\nmain ENDP\nEND main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK, "nearby non-reserved labels and data symbols should still parse");

    return failures;
}


/// Verifies Phase 64 equality conditional jumps lower direct branch targets.
///
/// @return Number of failures.
static int test_phase64_equality_jumps_parse_to_ir(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    je equal\n"
        "    jz equal\n"
        "    jne done\n"
        "    jnz done\n"
        "equal:\n"
        "    mov eax, 1\n"
        "done:\n"
        "    mov ebx, 2\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 64 equality jumps should parse");
    failures += expect_size(result.instruction_count, 6U, "Phase 64 equality-jump source should emit four jumps and two MOV instructions");
    failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_JE, "first instruction should be JE");
    failures += expect_u32((uint32_t)buffers.instructions[1].opcode, (uint32_t)VM_IR_OPCODE_JZ, "second instruction should be JZ");
    failures += expect_u32((uint32_t)buffers.instructions[2].opcode, (uint32_t)VM_IR_OPCODE_JNE, "third instruction should be JNE");
    failures += expect_u32((uint32_t)buffers.instructions[3].opcode, (uint32_t)VM_IR_OPCODE_JNZ, "fourth instruction should be JNZ");
    failures += expect_u32((uint32_t)buffers.instructions[0].destination.kind, (uint32_t)VM_IR_OPERAND_BRANCH_TARGET, "JE destination should be a branch target operand");
    failures += expect_u32(buffers.instructions[0].destination.immediate, 4U, "JE should target the equal label MOV instruction index");
    failures += expect_u32(buffers.instructions[1].destination.immediate, 4U, "JZ should target the equal label MOV instruction index");
    failures += expect_u32(buffers.instructions[2].destination.immediate, 5U, "JNE should target the done label MOV instruction index");
    failures += expect_u32(buffers.instructions[3].destination.immediate, 5U, "JNZ should target the done label MOV instruction index");

    return failures;
}

/// Verifies Phase 64 equality conditional jump target diagnostics.
///
/// @return Number of failures.
static int test_phase64_equality_jump_target_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    jnz value\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JNZ to data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "data-symbol JNZ target should use invalid-branch-target");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Direct conditional jumps accept only code labels", "data-symbol JNZ diagnostic should name conditional jump target rules");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    je missing\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JE to unknown label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "unknown JE target should use invalid-branch-target");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    je eax\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JE register target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "register JE target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    jz [eax]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JZ memory target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "memory JZ target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        ".code\n"
        "main PROC\n"
        "    jne 1234h\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JNE immediate target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "immediate JNE target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    je exit\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result
    ), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JE to Irvine32 exit should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "Irvine32 JE target should use invalid-branch-target");

    return failures;
}


/// Verifies Phase 66 unsigned relational conditional jumps lower direct branch targets.
///
/// @return Number of failures.
static int test_phase66_unsigned_relational_jumps_parse_to_ir(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    ja above\n"
        "    jnbe above\n"
        "    jae done\n"
        "    jnb done\n"
        "    jb below\n"
        "    jnae below\n"
        "    jbe done\n"
        "    jna done\n"
        "above:\n"
        "    mov eax, 1\n"
        "below:\n"
        "    mov ebx, 2\n"
        "done:\n"
        "    mov ecx, 3\n"
        "main ENDP\n"
        "END main\n";
    ParserTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 66 unsigned jumps should parse");
    failures += expect_size(result.instruction_count, 11U, "Phase 66 unsigned-jump source should emit eight jumps and three MOV instructions");
    if (result.instruction_count >= 11U) {
        failures += expect_u32((uint32_t)buffers.instructions[0].opcode, (uint32_t)VM_IR_OPCODE_JA, "first instruction should be JA");
        failures += expect_u32((uint32_t)buffers.instructions[1].opcode, (uint32_t)VM_IR_OPCODE_JNBE, "second instruction should be JNBE");
        failures += expect_u32((uint32_t)buffers.instructions[2].opcode, (uint32_t)VM_IR_OPCODE_JAE, "third instruction should be JAE");
        failures += expect_u32((uint32_t)buffers.instructions[3].opcode, (uint32_t)VM_IR_OPCODE_JNB, "fourth instruction should be JNB");
        failures += expect_u32((uint32_t)buffers.instructions[4].opcode, (uint32_t)VM_IR_OPCODE_JB, "fifth instruction should be JB");
        failures += expect_u32((uint32_t)buffers.instructions[5].opcode, (uint32_t)VM_IR_OPCODE_JNAE, "sixth instruction should be JNAE");
        failures += expect_u32((uint32_t)buffers.instructions[6].opcode, (uint32_t)VM_IR_OPCODE_JBE, "seventh instruction should be JBE");
        failures += expect_u32((uint32_t)buffers.instructions[7].opcode, (uint32_t)VM_IR_OPCODE_JNA, "eighth instruction should be JNA");
        failures += expect_u32((uint32_t)buffers.instructions[0].destination.kind, (uint32_t)VM_IR_OPERAND_BRANCH_TARGET, "JA destination should be a branch target operand");
        failures += expect_u32(buffers.instructions[0].destination.immediate, 8U, "JA should target the above label MOV instruction index");
        failures += expect_u32(buffers.instructions[4].destination.immediate, 9U, "JB should target the below label MOV instruction index");
        failures += expect_u32(buffers.instructions[6].destination.immediate, 10U, "JBE should target the done label MOV instruction index");
    }

    return failures;
}

/// Verifies Phase 66 unsigned relational jump target diagnostics and reserved words.
///
/// @return Number of failures.
static int test_phase66_unsigned_relational_jump_diagnostics(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(".data\nvalue DWORD 1\n.code\nmain PROC\n    ja value\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JA to data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "data-symbol JA target should use invalid-branch-target");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    jbe missing\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JBE to unknown label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "unknown JBE target should use invalid-branch-target");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    jb eax\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JB register target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "register JB target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    jnae [eax]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JNAE memory target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "memory JNAE target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    jnb 1234h\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JNB immediate target should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_BRANCH_TARGET_FORM, "immediate JNB target should use unsupported branch form");

    failures += expect_parser_status(parse_for_test("INCLUDE Irvine32.inc\n.code\nmain PROC\n    jna exit\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JNA to Irvine32 exit should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_BRANCH_TARGET, "Irvine32 JNA target should use invalid-branch-target");

    failures += expect_parser_status(parse_for_test(".data\nja DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JA data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "JA data symbol should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".data\njNbE DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "mixed-case JNBE data symbol should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "mixed-case JNBE data symbol should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nJBE:\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JBE code label should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "JBE code label should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test("OPTION CASEMAP:NONE\n.code\nmain PROC\njNa:\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CASEMAP:NONE should not permit unsigned jump label");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "CASEMAP:NONE unsigned jump label should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test("jae = 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JAE numeric equate should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "JAE numeric equate should use reserved-word-symbol");

    failures += expect_parser_status(parse_for_test(".code\njnb PROC\njnb ENDP\nEND jnb\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "JNB procedure name should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_RESERVED_WORD_SYMBOL, "JNB procedure name should use reserved-word-symbol");
    failures += expect_size(result.code_label_count, 0U, "reserved unsigned jump procedure name should not be inserted");

    return failures;
}

/// Verifies Phase 63 CMP register, immediate, and memory parsing.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase63_cmp_instruction_parses_to_ir(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;
    const char *source =
        ".data\n"
        "value DWORD 1\n"
        "nums DWORD 1, 2, 3\n"
        ".code\n"
        "main PROC\n"
        "    cmp al, bl\n"
        "    cmp ax, bx\n"
        "    cmp eax, ebx\n"
        "    cmp al, -1\n"
        "    cmp ax, 0FFFFh\n"
        "    cmp eax, 80000000h\n"
        "    cmp eax, value\n"
        "    cmp value, eax\n"
        "    cmp value, 1\n"
        "    cmp nums[4], eax\n"
        "    cmp eax, nums[4]\n"
        "    cmp DWORD PTR [eax], 1\n"
        "main ENDP\n"
        "END main\n";

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Phase 63 CMP forms should parse");
    failures += expect_size(result.instruction_count, 12U, "Phase 63 CMP source should emit twelve instructions");
    if (result.instruction_count >= 12U) {
        failures += expect_u32(buffers.instructions[0].opcode, VM_IR_OPCODE_CMP, "first CMP opcode should match");
        failures += expect_u32(buffers.instructions[3].source.kind, VM_IR_OPERAND_IMMEDIATE, "CMP AL immediate should parse");
        failures += expect_u32(buffers.instructions[6].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "CMP register, memory should parse");
        failures += expect_u32(buffers.instructions[7].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "CMP memory, register should parse");
        failures += expect_u32(buffers.instructions[8].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "CMP memory, immediate should parse");
        failures += expect_u32(buffers.instructions[9].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "CMP symbol-offset memory destination should parse");
        failures += expect_u32(buffers.instructions[10].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "CMP symbol-offset memory source should parse");
        failures += expect_u32(buffers.instructions[11].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "CMP PTR register-indirect memory destination should parse");
        failures += expect_u32(buffers.instructions[11].destination.width_bits, 32U, "CMP PTR register-indirect destination width should be DWORD");
    }

    return failures;
}

/// Verifies Phase 63 CMP parser diagnostics for invalid memory and width forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase63_cmp_instruction_parse_error_paths(void) {
    int failures = 0;
    ParserTestBuffers buffers;
    VmParserResult result;

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    cmp eax, al\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP width mismatch should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "CMP width mismatch diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    cmp al, 256\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP immediate overflow should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "CMP immediate overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    cmp eax eax\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP missing comma should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_COMMA, "CMP missing comma diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    cmp eax, ebx, ecx\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP extra operand should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "CMP extra operand diagnostic should match");
    failures += expect_string_contains(buffers.diagnostics[0].message, "CMP takes exactly two operands", "CMP extra operand message should name CMP");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\n    cmp [eax], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP memory/immediate ambiguous width should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "CMP memory/immediate ambiguous width diagnostic should match");
    failures += expect_string_contains(buffers.diagnostics[0].message, "Memory operand width is ambiguous", "CMP ambiguous width diagnostic should describe width ambiguity");

    failures += expect_parser_status(parse_for_test(".data\nleft DWORD 1\nright DWORD 2\n.code\nmain PROC\n    cmp left, right\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP memory-to-memory should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_INSTRUCTION_OPERANDS, "CMP memory-to-memory diagnostic should match");
    failures += expect_string_contains(buffers.diagnostics[0].message, "memory-to-memory", "CMP memory-to-memory message should describe unsupported pair");

    failures += expect_parser_status(parse_for_test(".data\nvalue DWORD 1\n.code\nmain PROC\n    cmp ax, value\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP register/memory width mismatch should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "CMP register/memory width mismatch diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nq QWORD 1\n.code\nmain PROC\n    cmp QWORD PTR q, 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP QWORD executable memory form should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "CMP QWORD executable memory diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nq SQWORD 1\n.code\nmain PROC\n    cmp SQWORD PTR q, 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "CMP SQWORD executable memory form should diagnose");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "CMP SQWORD executable memory diagnostic should match");

    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_minimal_program_parses_to_ir();
    failures += test_parsed_ir_executes_to_eax_42();
    failures += test_register_register_and_mixed_case();
    failures += test_labels_blank_lines_comments_and_crlf();
    failures += test_phase58_code_label_metadata();
    failures += test_phase58_multiple_labels_share_target();
    failures += test_phase58_empty_and_adjacent_procedure_labels();
    failures += test_phase58_non_executable_procedure_metadata_has_no_target();
    failures += test_phase67a_procedure_range_metadata();
    failures += test_phase67a_procedure_range_capacity_diagnostic();
    failures += test_phase68_call_target_classifier_metadata();
    failures += test_phase68_call_target_classifier_casemap_policy();
    failures += test_phase75_proc_metadata_and_attribute_diagnostics();
    failures += test_phase76_proc_uses_parsing_and_metadata();
    failures += test_phase78_local_declarations_parse_to_metadata();
    failures += test_phase78_local_scoping_and_shadowing();
    failures += test_phase78_local_diagnostics();
    failures += test_phase78_local_operands_remain_deferred();
    failures += test_phase69_direct_call_to_user_procedure_parses_to_ir();
    failures += test_phase69_direct_call_target_rejections();
    failures += test_phase70_plain_ret_parses_to_ir();
    failures += test_phase74_ret_imm16_forms();
    failures += test_phase68_procedure_name_diagnostics();
    failures += test_phase68b_eip_control_state_diagnostics();
    failures += test_phase58_label_casemap_policy();
    failures += test_phase58_label_conflict_diagnostics();
    failures += test_phase60_jmp_parse_to_ir();
    failures += test_phase60_jmp_procedure_entry_target();
    failures += test_phase60_jmp_target_diagnostics();
    failures += test_phase60_jmp_form_rejections();
    failures += test_phase64_equality_jumps_parse_to_ir();
    failures += test_phase64_equality_jump_target_diagnostics();
    failures += test_phase66_unsigned_relational_jumps_parse_to_ir();
    failures += test_phase66_unsigned_relational_jump_diagnostics();
    failures += test_phase61e_reserved_word_symbol_diagnostics();
    failures += test_zero_instruction_procedure();
    failures += test_error_path_diagnostics();
    failures += test_textbook_unsupported_directives_are_stable();
    failures += test_textbook_unsupported_keywords_are_stable();
    failures += test_scheduled_and_backlog_data_types_are_documented_diagnostics();
    failures += test_multi_diagnostic_unsupported_feature_recovery();
    failures += test_unsupported_section_recovery_resumes_at_code();
    failures += test_additional_data_sections_parse_successfully();
    failures += test_unsupported_block_recovery_avoids_body_cascades();
    failures += test_line_level_unsupported_feature_recovery_covers_required_constructs();
    failures += test_block_recovery_covers_required_terminators();
    failures += test_unterminated_unsupported_block_does_not_execute_body();
    failures += test_recovery_diagnostic_capacity_failure_is_fatal();
    failures += test_immediate_range_matches_destination_width();
    failures += test_negative_immediate_range_matches_destination_width();
    failures += test_immediate_range_covers_register_alias_families();
    failures += test_capacity_and_invalid_arguments();
    failures += test_lexer_diagnostics_are_preserved_by_parser();
    failures += test_lexer_capacity_failure_is_distinct();
    failures += test_extension_instructions_parse_to_ir();
    failures += test_extension_instruction_parse_error_paths();
    failures += test_phase20_instructions_parse_to_ir();
    failures += test_phase20_instruction_parse_error_paths();
    failures += test_phase57n_zero_operand_nop_parse_to_ir();
    failures += test_phase57o_nop_encoding_operand_parse_to_ir();
    failures += test_phase57o_nop_operand_rejections();
    failures += test_phase21_instructions_parse_to_ir();
    failures += test_phase21_instruction_parse_error_paths();
    failures += test_phase22_test_instruction_parses_to_ir();
    failures += test_phase22_test_instruction_parse_error_paths();
    failures += test_phase63_cmp_instruction_parses_to_ir();
    failures += test_phase63_cmp_instruction_parse_error_paths();
    failures += test_phase25_global_memory_width_resolution_parses_to_ir();
    failures += test_phase25_symbol_metadata_width_precedence();
    failures += test_phase25_explicit_ptr_overrides_symbol_metadata();
    failures += test_phase25_global_memory_width_resolution_error_paths();
    failures += test_phase26_header_directives_parse_before_sections();
    failures += test_phase26_header_directive_edge_cases();
    failures += test_phase26_header_directive_error_paths();
    failures += test_phase26_broader_directive_backlog_diagnostics();
    failures += test_phase53d_compatibility_notice_parser_paths();
    failures += test_phase53d_active_semantics_do_not_emit_noop_notices();
    failures += test_phase35a_casemap_parser_policy();
    failures += test_phase41_virtual_irvine32_include_records_registry();
    failures += test_phase57p_host_include_path_diagnostics();
    failures += test_phase57q_includelib_diagnostics();
    failures += test_phase57r_invoke_addr_external_routine_diagnostics();
    failures += test_phase41_irvine32_routine_diagnostics();
    failures += test_phase42_irvine32_exit_terminator_parser_paths();
    failures += test_phase43_inc_dec_parse_to_ir();
    failures += test_phase43_inc_dec_parse_error_paths();
    failures += test_phase44_logical_binary_parse_to_ir();
    failures += test_phase44_logical_binary_parse_error_paths();
    failures += test_phase45_not_parse_to_ir();
    failures += test_phase45_not_parse_error_paths();
    failures += test_phase46_shift_left_parse_to_ir();
    failures += test_phase46_shift_left_parse_error_paths();
    failures += test_phase47_shr_parse_to_ir();
    failures += test_phase47_shr_parse_error_paths();
    failures += test_phase48_sar_parse_to_ir();
    failures += test_phase48_sar_parse_error_paths();
    failures += test_phase49_rol_parse_to_ir();
    failures += test_phase49_rol_parse_error_paths();
    failures += test_phase50_ror_parse_to_ir();
    failures += test_phase50_ror_parse_error_paths();
    failures += test_phase52_lea_parse_to_ir();
    failures += test_phase52_lea_parse_error_paths();
    failures += test_phase57corr2_compact_negative_register_displacement_parse_to_ir();
    failures += test_phase57corr2_compact_negative_register_displacement_rejections();
    failures += test_phase53_mul_parse_to_ir();
    failures += test_phase53_mul_parse_error_paths();
    failures += test_phase54_imul_parse_to_ir();
    failures += test_phase54_imul_parse_error_paths();
    failures += test_phase55_imul_parse_to_ir();
    failures += test_phase55_imul_parse_error_paths();
    failures += test_phase56_div_parse_to_ir();
    failures += test_phase56_div_parse_error_paths();
    failures += test_phase57_idiv_parse_to_ir();
    failures += test_phase57_idiv_parse_error_paths();
    failures += test_phase57m_segment_symbol_reference_diagnostics();
    failures += test_phase57m_segment_symbol_definition_diagnostics();
    failures += test_phase57m_segment_symbol_casemap_and_regressions();
    failures += test_phase72a_push_pop_parse_to_ir();
    failures += test_phase72a_push_pop_parse_error_paths();
    failures += test_phase73_leave_parse_paths();
    failures += test_phase53a_symbol_offset_cross_object_parse_to_ir();
    failures += test_metadata_helpers();

    if (failures != 0) {
        fprintf(stderr, "Minimal parser tests failed: %d\n", failures);
        return 1;
    }

    printf("Parser tests through Phase 78 LOCAL parser metadata coverage passed.\n");
    return 0;
}
