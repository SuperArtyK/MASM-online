/*
 * @file test_parser.c
 * @brief Unit and integration tests for the parser through Milestone 28.
 *
 * These tests verify parsing of tiny .code programs into the existing IR,
 * error diagnostics for unsupported syntax, and integration with the current
 * executor without adding browser-visible execution behavior.
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
    /// Data image emitted from optional .data/.DATA? declarations.
    uint8_t data_image[TEST_DATA_IMAGE_CAPACITY];
    /// Constant image emitted from optional .CONST declarations.
    uint8_t const_image[TEST_CONST_IMAGE_CAPACITY];
} ParserTestBuffers;

/// Records a parser test failure.
///
/// @param message Human-readable failure description.
/// @return Always returns one failure.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
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
    config.data_image = buffers->data_image;
    config.data_image_capacity = TEST_DATA_IMAGE_CAPACITY;
    config.const_image = buffers->const_image;
    config.const_image_capacity = TEST_CONST_IMAGE_CAPACITY;
    config.diagnostics = buffers->diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;

    return vm_parser_parse_program(&config, out_result);
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
    failures += (vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed"));
    failures += expect_u32(eax, 42U, "parsed program should produce EAX = 42");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_HALTED, "parsed program should halt after two instructions");
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

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\ninc eax\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unsupported instruction should produce diagnostic");
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

    failures += expect_unsupported_feature_source(".code\nmain PROC\nmov eax, 1\n.IF eax == 1\nmov ebx, 2\nmain ENDP\nEND main\n", ".IF");
    failures += expect_unsupported_feature_source(".code\nmain PROC\nmov ecx, 3\n.WHILE ecx > 0\nsub ecx, 1\nmain ENDP\nEND main\n", ".WHILE");
    failures += expect_unsupported_feature_source(".code\nmain PROC\n.REPEAT\nmov eax, 1\n.UNTIL eax == 1\nmain ENDP\nEND main\n", ".REPEAT");
    failures += expect_unsupported_feature_source(".code\nmain PROC\n.BREAK\nmain ENDP\nEND main\n", ".BREAK");
    failures += expect_unsupported_feature_source(".code\nmain PROC\n.CONTINUE\nmain ENDP\nEND main\n", ".CONTINUE");

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
    failures += expect_unsupported_feature_source("INCLUDELIB kernel32.lib\n.code\nmain PROC\nmain ENDP\nEND main\n", "INCLUDELIB");
    failures += expect_unsupported_feature_source("EXTERN ExitProcess:PROC\n.code\nmain PROC\nmain ENDP\nEND main\n", "EXTERN");
    failures += expect_unsupported_feature_source("PUBLIC main\n.code\nmain PROC\nmain ENDP\nEND main\n", "PUBLIC");
    failures += expect_unsupported_feature_source("COMM buffer:BYTE:16\n.code\nmain PROC\nmain ENDP\nEND main\n", "COMM");
    failures += expect_unsupported_feature_source("m MACRO\nENDM\n.code\nmain PROC\nmain ENDP\nEND main\n", "macro definitions");
    failures += expect_unsupported_feature_source(".code\nmain PROC\nINVOKE ExitProcess, 0\nmain ENDP\nEND main\n", "INVOKE");
    failures += expect_unsupported_feature_source(".code\nmain PROC\nLOCAL temp:DWORD\nmain ENDP\nEND main\n", "LOCAL");

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
    failures += expect_size(result.diagnostic_count, 3U, "STRUCT, INVOKE, and .IF should produce three diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "STRUCT diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "INVOKE diagnostic code should match");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, ".IF diagnostic code should match");
    failures += expect_string_contains(buffers.diagnostics[0].message, "STRUCT", "first diagnostic should describe STRUCT");
    failures += expect_string_contains(buffers.diagnostics[1].message, "INVOKE", "second diagnostic should describe INVOKE");
    failures += expect_string_contains(buffers.diagnostics[2].message, ".IF", "third diagnostic should describe .IF");
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
    failures += expect_size(result.diagnostic_count, 1U, ".IF body should not produce cascaded diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, ".IF diagnostic code should match");
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
        "    LOCAL temp:DWORD\n"
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
    failures += expect_size(result.diagnostic_count, 8U, "all required line-level constructs should produce diagnostics");
    failures += expect_string_contains(buffers.diagnostics[0].message, "INVOKE", "line diagnostic should describe INVOKE");
    failures += expect_string_contains(buffers.diagnostics[1].message, "PROTO", "line diagnostic should describe PROTO");
    failures += expect_string_contains(buffers.diagnostics[2].message, "LOCAL", "line diagnostic should describe LOCAL");
    failures += expect_string_contains(buffers.diagnostics[3].message, "TEXTEQU", "line diagnostic should describe TEXTEQU");
    failures += expect_string_contains(buffers.diagnostics[4].message, "INCLUDELIB", "line diagnostic should describe INCLUDELIB");
    failures += expect_string_contains(buffers.diagnostics[5].message, "EXTERN", "line diagnostic should describe EXTERN");
    failures += expect_string_contains(buffers.diagnostics[6].message, "PUBLIC", "line diagnostic should describe PUBLIC");
    failures += expect_string_contains(buffers.diagnostics[7].message, "COMM", "line diagnostic should describe COMM");
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
    failures += expect_size(result.diagnostic_count, 5U, "UNION, MACRO, .WHILE, and both .REPEAT blocks should produce diagnostics");
    failures += expect_string_contains(buffers.diagnostics[0].message, "UNION", "block diagnostic should describe UNION");
    failures += expect_string_contains(buffers.diagnostics[1].message, "macro", "block diagnostic should describe MACRO");
    failures += expect_string_contains(buffers.diagnostics[2].message, ".WHILE", "block diagnostic should describe .WHILE");
    failures += expect_string_contains(buffers.diagnostics[3].message, ".REPEAT", "block diagnostic should describe .REPEAT .UNTIL");
    failures += expect_string_contains(buffers.diagnostics[4].message, ".REPEAT", "block diagnostic should describe .REPEAT .UNTILCXZ");
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
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "unterminated block first diagnostic should be unsupported-feature");
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
    config.source = ".code\nmain PROC\nINVOKE A\nPROTO B\nLOCAL c:DWORD\nmain ENDP\nEND main\n";
    config.source_file = "main.asm";
    config.tokens = buffers.tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
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
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;
    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_SOURCE_TEXT_CAPACITY_EXCEEDED, "small source-text buffer should produce diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_SOURCE_TEXT_CAPACITY_EXCEEDED, "source-text capacity diagnostic should match");

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    config.source = ".code\nmain PROC\ninc eax\nmain ENDP\nEND main\n";
    config.tokens = buffers.tokens;
    config.token_capacity = TEST_TOKEN_CAPACITY;
    config.lexer_diagnostics = buffers.lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_LEXER_DIAGNOSTIC_CAPACITY;
    config.instructions = buffers.instructions;
    config.instruction_capacity = TEST_INSTRUCTION_CAPACITY;
    config.source_text_storage = buffers.source_text;
    config.source_text_capacity = TEST_SOURCE_TEXT_CAPACITY;
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
        "    nop eax\n"
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
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "NOP operand diagnostic should be unsupported syntax");
    failures += expect_string_contains(buffers.diagnostics[0].message, "does not take operands", "NOP operand diagnostic should describe no-operand rule");

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

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Phase 26 headers should parse before .data/.code");
    failures += expect_size(result.diagnostic_count, 0U, "Phase 26 accepted headers should not produce diagnostics");
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

    failures += expect_parser_status(status, VM_PARSER_STATUS_OK, "Mixed-case Phase 26 headers should parse");
    failures += expect_size(result.diagnostic_count, 0U, "Mixed-case Phase 26 headers should not produce diagnostics");
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

    failures += expect_parser_status(parse_for_test("OPTION CASEMAP:ALL\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Unsupported OPTION form should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION, "Unsupported OPTION diagnostic code should match");

    failures += expect_parser_status(parse_for_test(".stack -1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Invalid .stack size should produce diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "Invalid .stack diagnostic code should match");

    failures += expect_parser_status(parse_for_test("ASSUME cs:code\n.STARTUP\n.LIST\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Unsupported directive families should be recovered together");
    failures += expect_size(result.diagnostic_count, 3U, "Unsupported directive families should report three diagnostics");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "ASSUME should remain an unsupported-feature diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[1].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, ".STARTUP should remain an unsupported-feature diagnostic");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[2].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, ".LIST should remain an unsupported-feature diagnostic");

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

/// Verifies metadata helper behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_metadata_helpers(void) {
    int failures = 0;

    if (strcmp(vm_parser_status_name(VM_PARSER_STATUS_OK), "ok") != 0) {
        failures += record_failure("parser status helper should name OK");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_INSTRUCTION), "unsupported-instruction") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported-instruction");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE), "unsupported-feature") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported-feature");
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
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_OPTION), "unsupported-option") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported option diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE), "unsupported-register-indirect-base") != 0) {
        failures += record_failure("parser diagnostic helper should name unsupported register-indirect base diagnostics");
    }
    if (strcmp(vm_parser_diagnostic_code_name(VM_PARSER_DIAGNOSTIC_CONST_WRITE), "const-write") != 0) {
        failures += record_failure("parser diagnostic helper should name const-write diagnostics");
    }
    if (vm_parser_status_name((VmParserStatus)999) != NULL) {
        failures += record_failure("invalid parser status name should be NULL");
    }
    if (vm_parser_diagnostic_code_name((VmParserDiagnosticCode)999) != NULL) {
        failures += record_failure("invalid parser diagnostic code name should be NULL");
    }

    return failures;
}

/// Runs all parser regression tests through Milestone 28.
///
/// @return Zero on success, otherwise one.
int main(void) {
    int failures = 0;

    failures += test_minimal_program_parses_to_ir();
    failures += test_parsed_ir_executes_to_eax_42();
    failures += test_register_register_and_mixed_case();
    failures += test_labels_blank_lines_comments_and_crlf();
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
    failures += test_phase21_instructions_parse_to_ir();
    failures += test_phase21_instruction_parse_error_paths();
    failures += test_phase22_test_instruction_parses_to_ir();
    failures += test_phase22_test_instruction_parse_error_paths();
    failures += test_phase25_global_memory_width_resolution_parses_to_ir();
    failures += test_phase25_symbol_metadata_width_precedence();
    failures += test_phase25_explicit_ptr_overrides_symbol_metadata();
    failures += test_phase25_global_memory_width_resolution_error_paths();
    failures += test_phase26_header_directives_parse_before_sections();
    failures += test_phase26_header_directive_edge_cases();
    failures += test_phase26_header_directive_error_paths();
    failures += test_phase26_broader_directive_backlog_diagnostics();
    failures += test_metadata_helpers();

    if (failures != 0) {
        fprintf(stderr, "Minimal parser tests failed: %d\n", failures);
        return 1;
    }

    printf("Minimal parser tests passed.\n");
    return 0;
}
