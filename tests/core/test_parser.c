/*
 * @file test_parser.c
 * @brief Unit and integration tests for the minimal parser and implemented data extensions.
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
#define TEST_TOKEN_CAPACITY 128U

/// Number of lexer diagnostics available to each parser test.
#define TEST_LEXER_DIAGNOSTIC_CAPACITY 16U

/// Number of parser diagnostics available to each parser test.
#define TEST_PARSER_DIAGNOSTIC_CAPACITY 16U

/// Number of IR instructions available to each parser test.
#define TEST_INSTRUCTION_CAPACITY 32U

/// Number of source-text storage bytes available to each parser test.
#define TEST_SOURCE_TEXT_CAPACITY 512U

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
    config.diagnostics = buffers->diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;

    return vm_parser_parse_program(&config, out_result);
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

    failures += expect_parser_status(parse_for_test(".stack\n.code\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, ".stack before .code should be rejected by the minimal parser");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_CODE_DIRECTIVE, ".stack-before-code diagnostic should request .code");

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
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LEXER_FAILED, "lexer overflow diagnostic should be surfaced by parser");

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
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_LEXER_FAILED, "small token buffer should surface lexer diagnostic");

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
    if (vm_parser_status_name((VmParserStatus)999) != NULL) {
        failures += record_failure("invalid parser status name should be NULL");
    }
    if (vm_parser_diagnostic_code_name((VmParserDiagnosticCode)999) != NULL) {
        failures += record_failure("invalid parser diagnostic code name should be NULL");
    }

    return failures;
}

/// Runs all minimal parser regression tests.
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
    failures += test_immediate_range_matches_destination_width();
    failures += test_negative_immediate_range_matches_destination_width();
    failures += test_immediate_range_covers_register_alias_families();
    failures += test_capacity_and_invalid_arguments();
    failures += test_metadata_helpers();

    if (failures != 0) {
        fprintf(stderr, "Minimal parser tests failed: %d\n", failures);
        return 1;
    }

    printf("Minimal parser tests passed.\n");
    return 0;
}
