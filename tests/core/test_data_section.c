/*
 * @file test_data_section.c
 * @brief Tests for Milestone 15 .data declarations, symbols, PTR, and register-indirect memory operands, and TYPE, LENGTHOF, SIZEOF, and character-literal support.
 *
 * These tests cover the parser-level data image and symbol table, integration
 * with the existing VM executor, Wasm JSON output, and error paths for the new
 * data-section behavior without adding future control-flow or Irvine32 scope.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_exec.h"
#include "../../src/core/vm_memory.h"
#include "../../src/parser/parser.h"
#include "../../src/parser/symbols.h"
#include "../../src/wasm/wasm_api.h"

/// Number of lexer tokens available to each Milestone 15 parser test.
#define TEST_TOKEN_CAPACITY 256U

/// Number of lexer diagnostics available to each Milestone 15 parser test.
#define TEST_LEXER_DIAGNOSTIC_CAPACITY 32U

/// Number of parser diagnostics available to each Milestone 15 parser test.
#define TEST_PARSER_DIAGNOSTIC_CAPACITY 32U

/// Number of IR instructions available to each Milestone 15 parser test.
#define TEST_INSTRUCTION_CAPACITY 64U

/// Number of source-text bytes available to each Milestone 15 parser test.
#define TEST_SOURCE_TEXT_CAPACITY 1024U

/// Number of data symbols available to each Milestone 15 parser test.
#define TEST_SYMBOL_CAPACITY 32U

/// Number of data image bytes available to each Milestone 15 parser test.
#define TEST_DATA_IMAGE_CAPACITY 512U

/// Holds all caller-owned parser buffers for one Milestone 15 test.
typedef struct DataSectionTestBuffers {
    /// Lexer token buffer.
    VmLexerToken tokens[TEST_TOKEN_CAPACITY];
    /// Lexer diagnostic buffer.
    VmLexerDiagnostic lexer_diagnostics[TEST_LEXER_DIAGNOSTIC_CAPACITY];
    /// Parser diagnostic buffer.
    VmParserDiagnostic diagnostics[TEST_PARSER_DIAGNOSTIC_CAPACITY];
    /// Emitted IR instruction buffer.
    VmIrInstruction instructions[TEST_INSTRUCTION_CAPACITY];
    /// Emitted data symbols.
    VmSymbol symbols[TEST_SYMBOL_CAPACITY];
    /// Emitted .data image bytes.
    uint8_t data_image[TEST_DATA_IMAGE_CAPACITY];
    /// Null-terminated source-text copies used by emitted IR.
    char source_text[TEST_SOURCE_TEXT_CAPACITY];
} DataSectionTestBuffers;

/// Records a test failure.
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

/// Verifies that two unsigned 8-bit values are equal.
///
/// @param actual Actual value.
/// @param expected Expected value.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_u8(uint8_t actual, uint8_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%u expected=%u)\n", message, (unsigned int)actual, (unsigned int)expected);
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

/// Verifies that returned JSON contains a required fragment.
///
/// @param json JSON string to inspect.
/// @param expected Required substring.
/// @param message Failure message when missing.
/// @return Zero on success, otherwise one failure.
static int expect_json_contains(const char *json, const char *expected, const char *message) {
    if (json == NULL || strstr(json, expected) == NULL) {
        fprintf(stderr, "FAIL: %s\nExpected: %s\nJSON: %s\n", message, expected, json != NULL ? json : "(null)");
        return 1;
    }
    return 0;
}

/// Parses source with full Milestone 15 buffers.
///
/// @param source Source text to parse.
/// @param buffers Test buffers to use.
/// @param out_result Receives parse result.
/// @return Parser status.
static VmParserStatus parse_for_test(const char *source, DataSectionTestBuffers *buffers, VmParserResult *out_result) {
    VmParserConfig config;

    memset(buffers, 0, sizeof(*buffers));
    memset(&config, 0, sizeof(config));
    memset(out_result, 0, sizeof(*out_result));

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
    config.diagnostics = buffers->diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;

    return vm_parser_parse_program(&config, out_result);
}

/// Loads parser-produced data image bytes into VM memory for integration tests.
///
/// @param vm VM to initialize.
/// @param buffers Parser buffers containing the data image.
/// @param result Parser result containing data size.
/// @return true when all bytes were written.
static bool load_data_image_for_test(Vm *vm, const DataSectionTestBuffers *buffers, const VmParserResult *result) {
    size_t index = 0U;

    for (index = 0U; index < result->data_size; index += 1U) {
        if (!vm_memory_status_succeeded(vm_memory_write_u8(&vm->memory, VM_MEMORY_DEFAULT_DATA_BASE + (uint32_t)index, buffers->data_image[index], NULL))) {
            return false;
        }
    }
    vm_memory_clear_changes(&vm->memory);
    return true;
}

/// Verifies Milestone 15 data layout for scalar, string, DUP, ?, and QWORD declarations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_data_layout_symbols_and_initializers(void) {
    const char *source =
        ".data\n"
        "msg BYTE \"Hi\", 0\n"
        "var DWORD 10\n"
        "arr BYTE 3 DUP(?)\n"
        "qval QWORD 12345678h\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;
    const VmSymbol *msg = NULL;
    const VmSymbol *var = NULL;
    const VmSymbol *arr = NULL;
    const VmSymbol *qval = NULL;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "data layout sample should parse");
    failures += expect_size(result.symbol_count, 4U, "data layout sample should emit four symbols");
    failures += expect_size(result.data_size, 18U, "data layout sample should occupy expected bytes");

    msg = vm_symbol_find_by_name(buffers.symbols, result.symbol_count, "msg", 3U);
    var = vm_symbol_find_by_name(buffers.symbols, result.symbol_count, "var", 3U);
    arr = vm_symbol_find_by_name(buffers.symbols, result.symbol_count, "arr", 3U);
    qval = vm_symbol_find_by_name(buffers.symbols, result.symbol_count, "qval", 4U);

    failures += msg != NULL ? 0 : record_failure("msg symbol should exist");
    failures += var != NULL ? 0 : record_failure("var symbol should exist");
    failures += arr != NULL ? 0 : record_failure("arr symbol should exist");
    failures += qval != NULL ? 0 : record_failure("qval symbol should exist");

    if (msg != NULL) {
        failures += expect_u32(msg->address, VM_MEMORY_DEFAULT_DATA_BASE, "msg address should start at data base");
        failures += expect_u32(msg->element_count, 3U, "msg should have three BYTE elements");
    }
    if (var != NULL) {
        failures += expect_u32(var->address, VM_MEMORY_DEFAULT_DATA_BASE + 3U, "var address should follow msg bytes");
        failures += expect_u32(var->size_bytes, 4U, "var should occupy one DWORD");
    }
    if (arr != NULL) {
        failures += expect_u32(arr->address, VM_MEMORY_DEFAULT_DATA_BASE + 7U, "arr address should follow var bytes");
        failures += expect_u32(arr->element_count, 3U, "arr DUP should expand to three bytes");
        failures += arr->has_uninitialized_initializer ? 0 : record_failure("arr should remember uninitialized initializer metadata");
    }
    if (qval != NULL) {
        failures += expect_u32(qval->address, VM_MEMORY_DEFAULT_DATA_BASE + 10U, "qval address should follow arr bytes");
        failures += expect_u32(qval->size_bytes, 8U, "qval should occupy one QWORD");
    }

    failures += expect_u8(buffers.data_image[0], (uint8_t)'H', "msg byte 0 should be H");
    failures += expect_u8(buffers.data_image[1], (uint8_t)'i', "msg byte 1 should be i");
    failures += expect_u8(buffers.data_image[2], 0U, "msg terminator should be zero");
    failures += expect_u8(buffers.data_image[3], 10U, "var low byte should be 10");
    failures += expect_u8(buffers.data_image[10], 0x78U, "qval byte 0 should be little-endian 78h");
    failures += expect_u8(buffers.data_image[11], 0x56U, "qval byte 1 should be little-endian 56h");
    failures += expect_u8(buffers.data_image[12], 0x34U, "qval byte 2 should be little-endian 34h");
    failures += expect_u8(buffers.data_image[13], 0x12U, "qval byte 3 should be little-endian 12h");

    return failures;
}

/// Verifies OFFSET and direct symbol memory writes integrate with the executor.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_offset_and_direct_symbol_write_execute(void) {
    const char *source =
        ".data\n"
        "var BYTE 0\n"
        "msg BYTE \"A\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET msg\n"
        "    mov var, 100\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;
    uint8_t var_value = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "OFFSET/direct-symbol sample should parse");
    failures += expect_size(result.instruction_count, 2U, "sample should emit two instructions");
    failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "OFFSET source should become immediate");
    failures += expect_u32(buffers.instructions[0].source.immediate, VM_MEMORY_DEFAULT_DATA_BASE + 1U, "OFFSET msg should use msg address");
    failures += expect_u32(buffers.instructions[1].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "var destination should become memory operand");
    failures += expect_u32(buffers.instructions[1].destination.address, VM_MEMORY_DEFAULT_DATA_BASE, "var destination should use var address");
    failures += expect_u32(buffers.instructions[1].destination.width_bits, 8U, "var destination should use BYTE width");

    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("vm init should succeed");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("data image should load into VM memory");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("OFFSET mov should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed");
    failures += expect_u32(eax, VM_MEMORY_DEFAULT_DATA_BASE + 1U, "OFFSET mov should place msg address in EAX");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("symbol write should execute");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE, &var_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("var byte read should succeed");
    failures += expect_u8(var_value, 100U, "direct symbol write should update VM memory");
    vm_deinit(&vm);

    return failures;
}


/// Verifies constant symbol-offset operands parse to absolute memory operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_constant_symbol_offsets_parse_to_ir(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov nums[8], 100\n"
        "    mov eax, nums[8]\n"
        "    mov [nums + 12], 200\n"
        "    mov ebx, [nums + 12]\n"
        "    mov [nums], 300\n"
        "    mov ecx, [nums + 0]\n"
        "    mov nums[0], 400\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "constant symbol offsets should parse");
    failures += expect_size(result.instruction_count, 7U, "constant symbol offset sample should emit seven instructions");
    failures += expect_u32(buffers.instructions[0].destination.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "nums[8] destination should be memory");
    failures += expect_u32(buffers.instructions[0].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 8U, "nums[8] destination should use byte offset 8");
    failures += expect_u32(buffers.instructions[0].destination.width_bits, 32U, "nums[8] destination should infer DWORD width");
    failures += expect_u32(buffers.instructions[1].source.kind, VM_IR_OPERAND_MEMORY_ADDRESS, "nums[8] source should be memory");
    failures += expect_u32(buffers.instructions[1].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 8U, "nums[8] source should use byte offset 8");
    failures += expect_u32(buffers.instructions[2].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 12U, "[nums + 12] destination should use byte offset 12");
    failures += expect_u32(buffers.instructions[3].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 12U, "[nums + 12] source should use byte offset 12");
    failures += expect_u32(buffers.instructions[4].destination.address, VM_MEMORY_DEFAULT_DATA_BASE, "[nums] destination should use byte offset 0");
    failures += expect_u32(buffers.instructions[4].destination.width_bits, 32U, "[nums] destination should infer DWORD width");
    failures += expect_u32(buffers.instructions[5].source.address, VM_MEMORY_DEFAULT_DATA_BASE, "[nums + 0] source should use byte offset 0");
    failures += expect_u32(buffers.instructions[6].destination.address, VM_MEMORY_DEFAULT_DATA_BASE, "nums[0] destination should use byte offset 0");

    return failures;
}

/// Verifies the Milestone 15 acceptance program executes through parser and VM.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_constant_symbol_offsets_execute_acceptance_program(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov nums[8], 100\n"
        "    mov eax, nums[8]\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t memory_value = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Milestone 15 acceptance source should parse");
    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("vm init should succeed");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("data image should load");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("nums[8] write should execute");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("nums[8] read should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed");
    failures += expect_u32(eax, 100U, "EAX should receive the value read from nums[8]");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 8U, &memory_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("nums[8] memory read should succeed");
    failures += expect_u32(memory_value, 100U, "nums[8] should contain 100");
    vm_deinit(&vm);

    return failures;
}

/// Verifies parser diagnostics for invalid constant symbol offsets.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_constant_symbol_offset_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [missing + 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unknown bracketed symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "unknown bracketed symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [nums - 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "negative offset before the data image should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, "negative offset diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [missing]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unknown offset-zero bracketed symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "unknown offset-zero bracketed symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, nums[37]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "offset crossing the data image should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE, "crossing offset diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [nums +]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "malformed bracketed offset should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "malformed bracketed offset diagnostic should match");

    return failures;
}

/// Verifies edge offsets at the end of a symbol and unaligned offsets.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_constant_symbol_offset_edge_cases(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov nums[36], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "last aligned DWORD slot should parse");
    failures += expect_u32(buffers.instructions[0].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 36U, "last aligned slot should use byte offset 36");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov nums[9], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "unaligned DWORD offset should parse");
    failures += expect_u32(buffers.instructions[0].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 9U, "unaligned slot should use byte offset 9");
    failures += expect_u32(buffers.instructions[0].destination.width_bits, 32U, "unaligned slot should still infer DWORD width");

    failures += expect_parser_status(parse_for_test(".data\nhead DWORD 0\ntail DWORD 0\n.code\nmain PROC\nmov [tail - 4], 123\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "negative offset that remains inside .data should parse");
    failures += expect_u32(buffers.instructions[0].destination.address, VM_MEMORY_DEFAULT_DATA_BASE, "[tail - 4] should resolve to the previous DWORD address");
    failures += expect_u32(buffers.instructions[0].destination.width_bits, 32U, "[tail - 4] should infer the referenced symbol width");

    return failures;
}

/// Verifies DB/DW/DD/DQ aliases and mixed case declarations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_data_type_aliases_and_mixed_case(void) {
    const char *source =
        ".DaTa\n"
        "b Db 1\n"
        "w dW 0203h\n"
        "d Dd 04050607h\n"
        "q dQ 8\n"
        ".CoDe\n"
        "Main PROC\n"
        "Main ENDP\n"
        "END Main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "mixed-case type aliases should parse");
    failures += expect_size(result.symbol_count, 4U, "type alias sample should emit four symbols");
    failures += expect_size(result.data_size, 15U, "type alias sample should occupy 15 bytes");
    failures += expect_u8(buffers.data_image[0], 1U, "DB byte should be stored");
    failures += expect_u8(buffers.data_image[1], 0x03U, "DW low byte should be little-endian");
    failures += expect_u8(buffers.data_image[2], 0x02U, "DW high byte should be little-endian");
    failures += expect_u8(buffers.data_image[3], 0x07U, "DD low byte should be little-endian");
    failures += expect_u8(buffers.data_image[6], 0x04U, "DD high byte should be little-endian");
    failures += expect_u8(buffers.data_image[7], 8U, "DQ low byte should be stored");

    return failures;
}

/// Verifies TYPE symbol emits declared element-size immediates for supported declarations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_type_operator_parse_to_immediates(void) {
    const char *source =
        ".data\n"
        "b BYTE 0\n"
        "ba DB 2 DUP(0)\n"
        "w WORD 0\n"
        "wa DW 2 DUP(0)\n"
        "d DWORD 0\n"
        "da DD 2 DUP(0)\n"
        "q QWORD 0\n"
        "qa DQ 2 DUP(0)\n"
        "msg BYTE \"Hi\", 0\n"
        "dupw WORD 3 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE b\n"
        "    mov ebx, TYPE w\n"
        "    mov ecx, TYPE d\n"
        "    mov edx, TYPE q\n"
        "    mov eax, TYPE ba\n"
        "    mov ebx, TYPE wa\n"
        "    mov ecx, TYPE da\n"
        "    mov edx, TYPE qa\n"
        "    mov eax, tYpE msg\n"
        "    mov ebx, TYPE dupw\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "TYPE operator sample should parse");
    failures += expect_size(result.instruction_count, 10U, "TYPE sample should emit ten instructions");
    failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "TYPE b should emit an immediate");
    failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "TYPE BYTE should be 1");
    failures += expect_u32(buffers.instructions[1].source.immediate, 2U, "TYPE WORD should be 2");
    failures += expect_u32(buffers.instructions[2].source.immediate, 4U, "TYPE DWORD should be 4");
    failures += expect_u32(buffers.instructions[3].source.immediate, 8U, "TYPE QWORD should be 8");
    failures += expect_u32(buffers.instructions[4].source.immediate, 1U, "TYPE DB array should be 1");
    failures += expect_u32(buffers.instructions[5].source.immediate, 2U, "TYPE DW array should be 2");
    failures += expect_u32(buffers.instructions[6].source.immediate, 4U, "TYPE DD array should be 4");
    failures += expect_u32(buffers.instructions[7].source.immediate, 8U, "TYPE DQ array should be 8");
    failures += expect_u32(buffers.instructions[8].source.immediate, 1U, "mixed-case TYPE BYTE string should be 1");
    failures += expect_u32(buffers.instructions[9].source.immediate, 2U, "TYPE WORD DUP should be 2");
    failures += expect_u32(buffers.instructions[0].source.width_bits, 32U, "TYPE immediates should use 32-bit immediate width");

    return failures;
}

/// Verifies TYPE source operands execute through parser and VM integration.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_type_operator_executes_acceptance_program(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE nums\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "TYPE acceptance source should parse");
    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("VM should initialize for TYPE acceptance test");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("TYPE acceptance data image should load");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("TYPE acceptance program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("TYPE mov should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX should be readable after TYPE mov");
    failures += expect_u32(eax, 4U, "TYPE nums should leave EAX = 4");
    vm_deinit(&vm);

    return failures;
}

/// Verifies TYPE can supply an immediate value for a memory destination.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_type_operator_executes_memory_immediate_context(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        "outval DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov outval, TYPE nums\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t memory_value = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "TYPE memory-immediate source should parse");
    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("VM should initialize for TYPE memory-immediate test");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("TYPE memory-immediate data image should load");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("TYPE memory-immediate program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("TYPE memory-immediate mov should execute");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 40U, &memory_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("outval should be readable after TYPE memory-immediate mov");
    failures += expect_u32(memory_value, 4U, "mov outval, TYPE nums should write DWORD value 4");
    vm_deinit(&vm);

    return failures;
}

/// Verifies TYPE preserves existing OFFSET behavior in the same program.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_type_operator_preserves_offset_source_operands(void) {
    const char *source =
        ".data\n"
        "msg BYTE \"Hi\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET msg\n"
        "    mov ebx, TYPE msg\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "OFFSET and TYPE source operands should parse together");
    failures += expect_u32(buffers.instructions[0].source.immediate, VM_MEMORY_DEFAULT_DATA_BASE, "OFFSET msg should remain the symbol address");
    failures += expect_u32(buffers.instructions[1].source.immediate, 1U, "TYPE msg should return BYTE element size");

    return failures;
}

/// Verifies LENGTHOF symbol emits element-count immediates for supported declarations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_lengthof_operator_parse_to_immediates(void) {
    const char *source =
        ".data\n"
        "b BYTE 0\n"
        "ba DB 2 DUP(0)\n"
        "w WORD 0\n"
        "wa DW 3 DUP(0)\n"
        "d DWORD 0\n"
        "da DD 4 DUP(0)\n"
        "q QWORD 0\n"
        "qa DQ 5 DUP(0)\n"
        "msg BYTE \"Hi\", 0\n"
        "many BYTE 2 DUP(\"AB\")\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF b\n"
        "    mov ebx, LENGTHOF ba\n"
        "    mov ecx, LENGTHOF w\n"
        "    mov edx, LENGTHOF wa\n"
        "    mov eax, LENGTHOF d\n"
        "    mov ebx, LENGTHOF da\n"
        "    mov ecx, LENGTHOF q\n"
        "    mov edx, LENGTHOF qa\n"
        "    mov eax, lEnGtHoF msg\n"
        "    mov ebx, LENGTHOF many\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "LENGTHOF operator sample should parse");
    failures += expect_size(result.instruction_count, 10U, "LENGTHOF sample should emit ten instructions");
    failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "LENGTHOF b should emit an immediate");
    failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "LENGTHOF scalar BYTE should be 1");
    failures += expect_u32(buffers.instructions[1].source.immediate, 2U, "LENGTHOF DB DUP should be 2");
    failures += expect_u32(buffers.instructions[2].source.immediate, 1U, "LENGTHOF scalar WORD should be 1");
    failures += expect_u32(buffers.instructions[3].source.immediate, 3U, "LENGTHOF DW DUP should be 3");
    failures += expect_u32(buffers.instructions[4].source.immediate, 1U, "LENGTHOF scalar DWORD should be 1");
    failures += expect_u32(buffers.instructions[5].source.immediate, 4U, "LENGTHOF DD DUP should be 4");
    failures += expect_u32(buffers.instructions[6].source.immediate, 1U, "LENGTHOF scalar QWORD should be 1");
    failures += expect_u32(buffers.instructions[7].source.immediate, 5U, "LENGTHOF DQ DUP should be 5");
    failures += expect_u32(buffers.instructions[8].source.immediate, 3U, "mixed-case LENGTHOF BYTE string should count bytes including terminator");
    failures += expect_u32(buffers.instructions[9].source.immediate, 4U, "LENGTHOF flat DUP string should multiply byte count by repeat count");
    failures += expect_u32(buffers.instructions[0].source.width_bits, 32U, "LENGTHOF immediates should use 32-bit immediate width");

    return failures;
}

/// Verifies LENGTHOF source operands execute through parser and VM integration.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_lengthof_operator_executes_acceptance_program(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        "buf  BYTE \"Hello\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF nums\n"
        "    mov ebx, LENGTHOF buf\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "LENGTHOF acceptance source should parse");
    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("VM should initialize for LENGTHOF acceptance test");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("LENGTHOF acceptance data image should load");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("LENGTHOF acceptance program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("LENGTHOF nums mov should execute");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("LENGTHOF buf mov should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX should be readable after LENGTHOF mov");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX should be readable after LENGTHOF mov");
    failures += expect_u32(eax, 10U, "LENGTHOF nums should leave EAX = 10");
    failures += expect_u32(ebx, 6U, "LENGTHOF buf should leave EBX = 6");
    vm_deinit(&vm);

    return failures;
}

/// Verifies LENGTHOF can supply an immediate value for a memory destination.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_lengthof_operator_executes_memory_immediate_context(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        "outval DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov outval, LENGTHOF nums\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t memory_value = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "LENGTHOF memory-immediate source should parse");
    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("VM should initialize for LENGTHOF memory-immediate test");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("LENGTHOF memory-immediate data image should load");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("LENGTHOF memory-immediate program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("LENGTHOF memory-immediate mov should execute");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 40U, &memory_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("outval should be readable after LENGTHOF memory-immediate mov");
    failures += expect_u32(memory_value, 10U, "mov outval, LENGTHOF nums should write DWORD value 10");
    vm_deinit(&vm);

    return failures;
}

/// Verifies LENGTHOF preserves existing OFFSET and TYPE behavior in the same program.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_lengthof_operator_preserves_type_and_offset_source_operands(void) {
    const char *source =
        ".data\n"
        "msg BYTE \"Hi\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET msg\n"
        "    mov ebx, TYPE msg\n"
        "    mov ecx, LENGTHOF msg\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "OFFSET, TYPE, and LENGTHOF source operands should parse together");
    failures += expect_u32(buffers.instructions[0].source.immediate, VM_MEMORY_DEFAULT_DATA_BASE, "OFFSET msg should remain the symbol address");
    failures += expect_u32(buffers.instructions[1].source.immediate, 1U, "TYPE msg should return BYTE element size");
    failures += expect_u32(buffers.instructions[2].source.immediate, 3U, "LENGTHOF msg should return BYTE string element count");

    return failures;
}

/// Verifies SIZEOF symbol emits total-byte-size immediates for supported declarations.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sizeof_operator_parse_to_immediates(void) {
    const char *source =
        ".data\n"
        "b BYTE 0\n"
        "ba DB 2 DUP(0)\n"
        "w WORD 0\n"
        "wa DW 3 DUP(0)\n"
        "d DWORD 0\n"
        "da DD 4 DUP(0)\n"
        "q QWORD 0\n"
        "qa DQ 5 DUP(0)\n"
        "msg BYTE \"Hi\", 0\n"
        "many BYTE 2 DUP(\"AB\")\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF b\n"
        "    mov ebx, SIZEOF ba\n"
        "    mov ecx, SIZEOF w\n"
        "    mov edx, SIZEOF wa\n"
        "    mov eax, SIZEOF d\n"
        "    mov ebx, SIZEOF da\n"
        "    mov ecx, SIZEOF q\n"
        "    mov edx, SIZEOF qa\n"
        "    mov eax, sIzEoF msg\n"
        "    mov ebx, SIZEOF many\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "SIZEOF operator sample should parse");
    failures += expect_size(result.instruction_count, 10U, "SIZEOF sample should emit ten instructions");
    failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "SIZEOF b should emit an immediate");
    failures += expect_u32(buffers.instructions[0].source.immediate, 1U, "SIZEOF scalar BYTE should be 1 byte");
    failures += expect_u32(buffers.instructions[1].source.immediate, 2U, "SIZEOF DB DUP should be 2 bytes");
    failures += expect_u32(buffers.instructions[2].source.immediate, 2U, "SIZEOF scalar WORD should be 2 bytes");
    failures += expect_u32(buffers.instructions[3].source.immediate, 6U, "SIZEOF DW DUP should be 6 bytes");
    failures += expect_u32(buffers.instructions[4].source.immediate, 4U, "SIZEOF scalar DWORD should be 4 bytes");
    failures += expect_u32(buffers.instructions[5].source.immediate, 16U, "SIZEOF DD DUP should be 16 bytes");
    failures += expect_u32(buffers.instructions[6].source.immediate, 8U, "SIZEOF scalar QWORD should be 8 bytes");
    failures += expect_u32(buffers.instructions[7].source.immediate, 40U, "SIZEOF DQ DUP should be 40 bytes");
    failures += expect_u32(buffers.instructions[8].source.immediate, 3U, "mixed-case SIZEOF BYTE string should count total bytes including terminator");
    failures += expect_u32(buffers.instructions[9].source.immediate, 4U, "SIZEOF flat DUP string should multiply byte count by repeat count");
    failures += expect_u32(buffers.instructions[0].source.width_bits, 32U, "SIZEOF immediates should use 32-bit immediate width");

    return failures;
}

/// Verifies SIZEOF source operands and character literals execute through parser and VM integration.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sizeof_operator_and_character_literal_acceptance_program(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        "ch   BYTE 'A'\n"
        "pair WORD 'AB'\n"
        "tag  DWORD 'ABCD'\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF nums\n"
        "    mov bl, ch\n"
        "    mov cx, pair\n"
        "    mov edx, 'ABCD'\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "SIZEOF and packed character acceptance source should parse");
    failures += expect_u8(buffers.data_image[40], 65U, "BYTE 'A' should lay out ASCII 65 after nums");
    failures += expect_u8(buffers.data_image[41], 65U, "WORD 'AB' should store A as low byte");
    failures += expect_u8(buffers.data_image[42], 66U, "WORD 'AB' should store B as high byte");
    failures += expect_u8(buffers.data_image[43], 65U, "DWORD 'ABCD' should store A first");
    failures += expect_u8(buffers.data_image[46], 68U, "DWORD 'ABCD' should store D fourth");
    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("VM should initialize for SIZEOF acceptance test");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("SIZEOF acceptance data image should load");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("SIZEOF acceptance program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("SIZEOF nums mov should execute");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("mov bl, ch should execute");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("mov cx, pair should execute");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("mov edx, packed literal should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX should be readable after SIZEOF mov");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX should be readable after character mov");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_ECX, &ecx) ? 0 : record_failure("ECX should be readable after packed WORD mov");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EDX, &edx) ? 0 : record_failure("EDX should be readable after packed DWORD mov");
    failures += expect_u32(eax, 40U, "SIZEOF nums should leave EAX = 40");
    failures += expect_u32(ebx & 0xFFU, 65U, "mov bl, ch should leave BL = 65");
    failures += expect_u32(ecx & 0xFFFFU, 0x4241U, "mov cx, pair should leave CX = 4241h");
    failures += expect_u32(edx, 0x44434241U, "mov edx, 'ABCD' should pack little-endian");
    vm_deinit(&vm);

    return failures;
}

/// Verifies SIZEOF can supply an immediate value for a memory destination.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sizeof_operator_executes_memory_immediate_context(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        "outval DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov outval, SIZEOF nums\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t memory_value = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "SIZEOF memory-immediate source should parse");
    failures += vm_init(&vm, NULL) == VM_EXEC_STATUS_OK ? 0 : record_failure("VM should initialize for SIZEOF memory-immediate test");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("SIZEOF memory-immediate data image should load");
    failures += vm_load_program(&vm, buffers.instructions, result.instruction_count) == VM_EXEC_STATUS_OK ? 0 : record_failure("SIZEOF memory-immediate program should load");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("SIZEOF memory-immediate mov should execute");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 40U, &memory_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("outval should be readable after SIZEOF memory-immediate mov");
    failures += expect_u32(memory_value, 40U, "mov outval, SIZEOF nums should write DWORD value 40");
    vm_deinit(&vm);

    return failures;
}

/// Verifies character literals in byte-compatible contexts and related diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_character_literal_data_and_source_contexts(void) {
    const char *source =
        ".data\n"
        "a BYTE 'A'\n"
        "b DB '0'\n"
        "c BYTE '\\\\'\n"
        "d BYTE '\\''\n"
        "seq BYTE 'AB', 0\n"
        "pair WORD 'AB'\n"
        "short WORD 'A'\n"
        "tag DWORD 'ABCD'\n"
        "wide QWORD 'ABCDEFGH'\n"
        ".code\n"
        "main PROC\n"
        "mov bl, 'A'\n"
        "mov cx, 'AB'\n"
        "mov edx, 'ABCD'\n"
        "mov eax, 'A'\n"
        "mov eax, 'ABC'\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "single and packed character literals should parse");
    failures += expect_u8(buffers.data_image[0], 65U, "BYTE 'A' should encode ASCII A");
    failures += expect_u8(buffers.data_image[1], 48U, "DB '0' should encode ASCII zero");
    failures += expect_u8(buffers.data_image[2], 92U, "BYTE escaped backslash should encode ASCII backslash");
    failures += expect_u8(buffers.data_image[3], 39U, "BYTE escaped quote should encode ASCII apostrophe");
    failures += expect_u8(buffers.data_image[4], 65U, "BYTE 'AB' should emit byte A");
    failures += expect_u8(buffers.data_image[5], 66U, "BYTE 'AB' should emit byte B");
    failures += expect_u8(buffers.data_image[6], 0U, "BYTE 'AB', 0 should preserve explicit terminator");
    failures += expect_u8(buffers.data_image[7], 65U, "WORD 'AB' should store A as low byte");
    failures += expect_u8(buffers.data_image[8], 66U, "WORD 'AB' should store B as high byte");
    failures += expect_u8(buffers.data_image[9], 65U, "WORD 'A' should store A as low byte");
    failures += expect_u8(buffers.data_image[10], 0U, "WORD 'A' should zero-fill high byte");
    failures += expect_u8(buffers.data_image[11], 65U, "DWORD 'ABCD' should store A first");
    failures += expect_u8(buffers.data_image[14], 68U, "DWORD 'ABCD' should store D fourth");
    failures += expect_u8(buffers.data_image[15], 65U, "QWORD 'ABCDEFGH' should store A first");
    failures += expect_u8(buffers.data_image[22], 72U, "QWORD 'ABCDEFGH' should store H eighth");
    failures += expect_u32(buffers.instructions[0].source.kind, VM_IR_OPERAND_IMMEDIATE, "mov bl, 'A' should use an immediate source");
    failures += expect_u32(buffers.instructions[0].source.immediate, 65U, "mov bl, 'A' should encode ASCII A");
    failures += expect_u32(buffers.instructions[0].source.width_bits, 8U, "single-character literal immediates should record one decoded byte");
    failures += expect_u32(buffers.instructions[1].source.immediate, 0x4241U, "mov cx, 'AB' should pack little-endian");
    failures += expect_u32(buffers.instructions[1].source.width_bits, 16U, "two-byte character literal immediates should record two decoded bytes");
    failures += expect_u32(buffers.instructions[2].source.immediate, 0x44434241U, "mov edx, 'ABCD' should pack little-endian");
    failures += expect_u32(buffers.instructions[2].source.width_bits, 32U, "four-byte character literal immediates should record four decoded bytes");
    failures += expect_u32(buffers.instructions[3].source.immediate, 65U, "mov eax, 'A' should allow a one-byte literal in a wider destination");
    failures += expect_u32(buffers.instructions[3].source.width_bits, 32U, "mov eax, 'A' should normalize the character literal to the destination width");
    failures += expect_u32(buffers.instructions[4].source.immediate, 0x00434241U, "mov eax, 'ABC' should pack three decoded bytes little-endian");
    failures += expect_u32(buffers.instructions[4].source.width_bits, 32U, "mov eax, 'ABC' should execute using the destination width");

    failures += expect_parser_status(parse_for_test(".data\na BYTE ''\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "empty character data literal should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "empty character data literal diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\na BYTE '\\q'\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unsupported character escape should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "unsupported character escape diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nw WORD 'ABC'\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "WORD packed literal overflow should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "WORD packed literal overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nd DWORD 'ABCDE'\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DWORD packed literal overflow should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "DWORD packed literal overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nq QWORD 'ABCDEFGHI'\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "QWORD packed literal overflow should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "QWORD packed literal overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov al, 'AB'\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "two-byte literal should not fit AL");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "two-byte literal to AL diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov ax, 'ABC'\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "three-byte literal should not fit AX");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "three-byte literal to AX diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov eax, 'ABCDE'\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "five-byte literal should not fit current immediate width");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_CHARACTER_LITERAL, "five-byte immediate literal diagnostic should match");

    return failures;
}

/// Verifies SIZEOF preserves existing OFFSET, TYPE, and LENGTHOF behavior in the same program.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sizeof_operator_preserves_existing_data_operators(void) {
    const char *source =
        ".data\n"
        "msg BYTE \"Hi\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET msg\n"
        "    mov ebx, TYPE msg\n"
        "    mov ecx, LENGTHOF msg\n"
        "    mov edx, SIZEOF msg\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "OFFSET, TYPE, LENGTHOF, and SIZEOF source operands should parse together");
    failures += expect_u32(buffers.instructions[0].source.immediate, VM_MEMORY_DEFAULT_DATA_BASE, "OFFSET msg should remain the symbol address");
    failures += expect_u32(buffers.instructions[1].source.immediate, 1U, "TYPE msg should return BYTE element size");
    failures += expect_u32(buffers.instructions[2].source.immediate, 3U, "LENGTHOF msg should return BYTE string element count");
    failures += expect_u32(buffers.instructions[3].source.immediate, 3U, "SIZEOF msg should return BYTE string byte size");

    return failures;
}

/// Verifies structured parser diagnostics for unsupported SIZEOF and OFFSET expression forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_sizeof_operator_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, SIZEOF missing\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SIZEOF unknown symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "SIZEOF unknown symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, SIZEOF\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SIZEOF without symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "SIZEOF missing symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, SIZEOF nums + 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SIZEOF arithmetic expression should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION, "SIZEOF expression diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, SIZEOF [nums]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SIZEOF bracket expression should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SIZEOF_EXPRESSION, "SIZEOF bracket expression diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\narr BYTE 4 DUP(0)\n.code\nmain PROC\nmov eax, OFFSET arr + 4\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "OFFSET arithmetic expression should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "OFFSET expression diagnostic should match");

    return failures;
}

/// Verifies structured parser diagnostics for unsupported LENGTHOF expression forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_lengthof_operator_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, LENGTHOF missing\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LENGTHOF unknown symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "LENGTHOF unknown symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, LENGTHOF\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LENGTHOF without symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "LENGTHOF missing symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, LENGTHOF nums + 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LENGTHOF arithmetic expression should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION, "LENGTHOF expression diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, LENGTHOF [nums]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "LENGTHOF bracket expression should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION, "LENGTHOF bracket expression diagnostic should match");


    return failures;
}

/// Verifies structured parser diagnostics for unsupported TYPE expression forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_type_operator_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, TYPE missing\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TYPE unknown symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "TYPE unknown symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, TYPE\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TYPE without symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "TYPE missing symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, TYPE nums + 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TYPE arithmetic expression should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION, "TYPE expression diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, TYPE [nums]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "TYPE bracket expression should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION, "TYPE bracket expression diagnostic should match");


    return failures;
}

/// Verifies parser diagnostics for new Milestone 15 error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_data_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nvar BYTE 0\nvar BYTE 1\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "duplicate symbols should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DUPLICATE_SYMBOL, "duplicate symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar REAL4 0\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "backlog data type should fail with unsupported feature");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE, "backlog type diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar FOO 0\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unknown data type should still fail as unsupported data type");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_DATA_TYPE, "unknown type diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar BYTE 2 DUP\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "invalid DUP should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_DUP, "invalid DUP diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar BYTE 0\n.code\nmain PROC\nmov other, 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unknown symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "unknown symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nq QWORD 0\n.code\nmain PROC\nmov q, 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "QWORD direct writes should be deferred");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "QWORD direct write diagnostic should match");

    return failures;
}

/// Verifies direct symbol writes reject immediates too large for the symbol element width.
///
/// @return Zero on success, otherwise a positive failure count.
/// Verifies negative data initializers and direct symbol writes use signed range checks.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_negative_data_initializers_and_symbol_writes(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\na BYTE -1\nb WORD -2\nc DWORD -3\nd QWORD -4\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "negative data initializers should parse");
    failures += expect_u8(buffers.data_image[0], 0xFFU, "BYTE -1 should encode as FFh");
    failures += expect_u8(buffers.data_image[1], 0xFEU, "WORD -2 low byte should encode as FEh");
    failures += expect_u8(buffers.data_image[2], 0xFFU, "WORD -2 high byte should encode as FFh");
    failures += expect_u8(buffers.data_image[3], 0xFDU, "DWORD -3 low byte should encode as FDh");
    failures += expect_u8(buffers.data_image[6], 0xFFU, "DWORD -3 high byte should encode as FFh");
    failures += expect_u8(buffers.data_image[7], 0xFCU, "QWORD -4 low byte should encode as FCh");
    failures += expect_u8(buffers.data_image[14], 0xFFU, "QWORD -4 high byte should encode as FFh");

    failures += expect_parser_status(parse_for_test(".data\nvar BYTE 0\n.code\nmain PROC\nmov var, -128\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "BYTE symbol should accept -128");
    failures += expect_parser_status(parse_for_test(".data\nvar BYTE 0\n.code\nmain PROC\nmov var, -129\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "BYTE symbol should reject -129");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "BYTE negative write overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar BYTE -129\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "BYTE initializer should reject -129");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "BYTE negative initializer overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\narr BYTE 2 DUP(-1)\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "DUP should support negative inner initializers");
    failures += expect_u8(buffers.data_image[0], 0xFFU, "first DUP(-1) byte should be FFh");
    failures += expect_u8(buffers.data_image[1], 0xFFU, "second DUP(-1) byte should be FFh");

    failures += expect_parser_status(parse_for_test(".data\narr BYTE -2 DUP(0)\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "negative DUP repeat count should be rejected");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_DUP, "negative DUP repeat diagnostic should match");

    return failures;
}

static int test_symbol_write_immediate_range_errors(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nvar BYTE 0\n.code\nmain PROC\nmov var, 255\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "BYTE symbol should accept 255");
    failures += expect_parser_status(parse_for_test(".data\nvar BYTE 0\n.code\nmain PROC\nmov var, 256\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "BYTE symbol should reject 256");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "BYTE symbol overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar WORD 0\n.code\nmain PROC\nmov var, 65535\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "WORD symbol should accept 65535");
    failures += expect_parser_status(parse_for_test(".data\nvar WORD 0\n.code\nmain PROC\nmov var, 65536\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "WORD symbol should reject 65536");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "WORD symbol overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar DWORD 0\n.code\nmain PROC\nmov var, 4294967295\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "DWORD symbol should accept UINT32_MAX");

    return failures;
}

/// Verifies width checks also apply to data declaration aliases and hex literals.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_symbol_write_immediate_range_covers_type_aliases(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nvar DB 0\n.code\nmain PROC\nmov var, 0FFh\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "DB symbol should accept hex FFh");
    failures += expect_parser_status(parse_for_test(".data\nvar DB 0\n.code\nmain PROC\nmov var, 100h\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DB symbol should reject hex 100h");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "DB symbol overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar DW 0\n.code\nmain PROC\nmov var, 0FFFFh\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "DW symbol should accept hex FFFFh");
    failures += expect_parser_status(parse_for_test(".data\nvar DW 0\n.code\nmain PROC\nmov var, 10000h\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "DW symbol should reject hex 10000h");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "DW symbol overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nvar DD 0\n.code\nmain PROC\nmov var, 0FFFFFFFFh\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "DD symbol should accept hex FFFFFFFFh");

    return failures;
}

/// Verifies data-capacity handling with a deliberately small data image buffer.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_data_capacity_error(void) {
    const char *source = ".data\nvar DWORD 1\n.code\nmain PROC\nmain ENDP\nEND main\n";
    DataSectionTestBuffers buffers;
    VmParserConfig config;
    VmParserResult result;
    int failures = 0;

    memset(&buffers, 0, sizeof(buffers));
    memset(&config, 0, sizeof(config));
    memset(&result, 0, sizeof(result));

    config.source = source;
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
    config.data_image = buffers.data_image;
    config.data_image_capacity = 2U;
    config.diagnostics = buffers.diagnostics;
    config.diagnostic_capacity = TEST_PARSER_DIAGNOSTIC_CAPACITY;

    failures += expect_parser_status(vm_parser_parse_program(&config, &result), VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED, "small data image should report data-capacity status");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, "data capacity diagnostic should match");

    return failures;
}


/// Verifies explicit PTR width overrides parse to memory operands with requested widths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_ptr_width_overrides_parse_to_ir(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov BYTE PTR nums[3], 100\n"
        "    mov WORD PTR [nums + 4], 1234h\n"
        "    mov DWORD PTR nums[8], 12345678h\n"
        "    mov bl, BYTE PTR nums[3]\n"
        "    mov eax, DWORD PTR [nums + 8]\n"
        "    mov BYTE PTR [nums], 1\n"
        "    mov DWORD PTR [nums + 0], 2\n"
        "    mov BYTE PTR nums[0], 3\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "PTR width override sample should parse");
    failures += expect_size(result.instruction_count, 8U, "PTR width override sample should emit eight instructions");
    failures += expect_u32(buffers.instructions[0].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 3U, "BYTE PTR nums[3] should use byte offset 3");
    failures += expect_u32(buffers.instructions[0].destination.width_bits, 8U, "BYTE PTR destination should use 8-bit width");
    failures += expect_u32(buffers.instructions[1].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 4U, "WORD PTR [nums + 4] should use byte offset 4");
    failures += expect_u32(buffers.instructions[1].destination.width_bits, 16U, "WORD PTR destination should use 16-bit width");
    failures += expect_u32(buffers.instructions[2].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 8U, "DWORD PTR nums[8] should use byte offset 8");
    failures += expect_u32(buffers.instructions[2].destination.width_bits, 32U, "DWORD PTR destination should use 32-bit width");
    failures += expect_u32(buffers.instructions[3].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 3U, "BYTE PTR source should use byte offset 3");
    failures += expect_u32(buffers.instructions[3].source.width_bits, 8U, "BYTE PTR source should use 8-bit width");
    failures += expect_u32(buffers.instructions[4].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 8U, "DWORD PTR source should use byte offset 8");
    failures += expect_u32(buffers.instructions[4].source.width_bits, 32U, "DWORD PTR source should use 32-bit width");
    failures += expect_u32(buffers.instructions[5].destination.address, VM_MEMORY_DEFAULT_DATA_BASE, "BYTE PTR [nums] should use offset zero");
    failures += expect_u32(buffers.instructions[5].destination.width_bits, 8U, "BYTE PTR [nums] should use 8-bit width");
    failures += expect_u32(buffers.instructions[6].destination.width_bits, 32U, "DWORD PTR [nums + 0] should use 32-bit width");
    failures += expect_u32(buffers.instructions[7].destination.width_bits, 8U, "BYTE PTR nums[0] should use 8-bit width");

    return failures;
}

/// Verifies the Milestone 15 acceptance program executes explicit-width writes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_ptr_width_overrides_execute_acceptance_program(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov BYTE PTR nums[3], 100\n"
        "    mov DWORD PTR nums[8], 12345678h\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint8_t byte_value = 0U;
    uint32_t dword_value = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "PTR acceptance program should parse");
    failures += expect_u32(buffers.instructions[0].destination.width_bits, 8U, "first acceptance write should be BYTE width");
    failures += expect_u32(buffers.instructions[1].destination.width_bits, 32U, "second acceptance write should be DWORD width");
    failures += expect_u32(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for PTR acceptance program");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("data image should load for PTR acceptance program");
    failures += expect_u32(vm_load_program(&vm, buffers.instructions, result.instruction_count), VM_EXEC_STATUS_OK, "PTR acceptance program should load");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "BYTE PTR write should execute");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "DWORD PTR write should execute");
    failures += vm_memory_read_u8(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 3U, &byte_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("BYTE PTR memory read should succeed");
    failures += expect_u8(byte_value, 100U, "BYTE PTR nums[3] should write one byte");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 8U, &dword_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("DWORD PTR memory read should succeed");
    failures += expect_u32(dword_value, 0x12345678U, "DWORD PTR nums[8] should write the full dword");
    vm_deinit(&vm);

    return failures;
}

/// Verifies explicit-width memory sources execute with matching register widths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_ptr_width_overrides_source_reads_execute(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov BYTE PTR nums[3], 100\n"
        "    mov bl, BYTE PTR nums[3]\n"
        "    mov DWORD PTR nums[8], 12345678h\n"
        "    mov eax, DWORD PTR nums[8]\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "PTR source-read program should parse");
    failures += expect_u32(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for PTR source-read program");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("data image should load for PTR source-read program");
    failures += expect_u32(vm_load_program(&vm, buffers.instructions, result.instruction_count), VM_EXEC_STATUS_OK, "PTR source-read program should load");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "BYTE PTR write before read should execute");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "BYTE PTR source read should execute");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "DWORD PTR write before read should execute");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "DWORD PTR source read should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after DWORD PTR source read");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX read should succeed after BYTE PTR source read");
    failures += expect_u32(eax, 0x12345678U, "EAX should receive DWORD PTR source value");
    failures += expect_u32(ebx & 0xFFU, 100U, "BL should receive BYTE PTR source value");
    vm_deinit(&vm);

    return failures;
}

/// Verifies register-indirect memory operands parse to runtime-address IR operands.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_indirect_operands_parse_to_ir(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET nums\n"
        "    mov DWORD PTR [esi + 8], 100\n"
        "    mov eax, DWORD PTR [esi + 8]\n"
        "    mov [ebx], al\n"
        "    mov eax, [esi]\n"
        "    mov nums[esi], eax\n"
        "    mov eax, [nums + esi]\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "register-indirect sample should parse");
    failures += expect_size(result.instruction_count, 7U, "register-indirect sample should emit seven instructions");
    failures += expect_u32(buffers.instructions[1].destination.kind, VM_IR_OPERAND_MEMORY_REGISTER, "DWORD PTR [esi + 8] should emit register memory destination");
    failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_ESI, "DWORD PTR [esi + 8] should use ESI base");
    failures += expect_u32(buffers.instructions[1].destination.address, 0U, "plain [esi + 8] should not include a static symbol base");
    failures += expect_u32(buffers.instructions[1].destination.immediate, 8U, "DWORD PTR [esi + 8] should store byte displacement 8");
    failures += expect_u32(buffers.instructions[1].destination.width_bits, 32U, "DWORD PTR [esi + 8] should use explicit DWORD width");
    failures += expect_u32(buffers.instructions[2].source.kind, VM_IR_OPERAND_MEMORY_REGISTER, "DWORD PTR [esi + 8] source should emit register memory source");
    failures += expect_u32(buffers.instructions[3].destination.width_bits, 8U, "mov [ebx], al should infer BYTE destination width from AL");
    failures += expect_u32(buffers.instructions[4].source.width_bits, 32U, "mov eax, [esi] should infer DWORD source width from EAX");
    failures += expect_u32(buffers.instructions[5].destination.address, VM_MEMORY_DEFAULT_DATA_BASE, "nums[esi] should include nums symbol base");
    failures += expect_u32(buffers.instructions[5].destination.reg, VM_REGISTER_ESI, "nums[esi] should use ESI as runtime byte offset");
    failures += expect_u32(buffers.instructions[6].source.address, VM_MEMORY_DEFAULT_DATA_BASE, "[nums + esi] should include nums symbol base");

    return failures;
}

/// Verifies the Milestone 15 register-indirect acceptance program executes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_indirect_acceptance_program_executes(void) {
    const char *source =
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET nums\n"
        "    mov DWORD PTR [esi + 8], 100\n"
        "    mov eax, DWORD PTR [esi + 8]\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    Vm vm;
    uint32_t eax = 0U;
    uint32_t dword_value = 0U;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "register-indirect acceptance program should parse");
    failures += expect_u32(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "vm init should succeed for register-indirect acceptance program");
    failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("data image should load for register-indirect acceptance program");
    failures += expect_u32(vm_load_program(&vm, buffers.instructions, result.instruction_count), VM_EXEC_STATUS_OK, "register-indirect acceptance program should load");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "OFFSET load should execute");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "register-indirect write should execute");
    failures += expect_u32(vm_step(&vm), VM_EXEC_STATUS_OK, "register-indirect read should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX read should succeed after register-indirect program");
    failures += expect_u32(eax, 100U, "register-indirect acceptance program should set EAX to 100");
    failures += vm_memory_read_u32(&vm.memory, VM_MEMORY_DEFAULT_DATA_BASE + 8U, &dword_value, NULL) == VM_MEMORY_STATUS_OK ? 0 : record_failure("register-indirect DWORD memory read should succeed");
    failures += expect_u32(dword_value, 100U, "register-indirect write should update nums + 8");
    vm_deinit(&vm);

    return failures;
}

/// Verifies symbol/register addressing forms execute with byte-offset semantics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_symbol_register_memory_forms_execute(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, 8\n"
        "    mov DWORD PTR nums[esi], 100\n"
        "    mov eax, DWORD PTR [nums + esi]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":15", "response should identify Milestone 15");
    failures += expect_json_contains(json, "\"ok\":true", "symbol/register source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "symbol/register read should set EAX = 100");
    failures += expect_json_contains(json, "\"symbol\":\"nums\",\"address\":\"00500008h\"", "symbol/register write should resolve to nums + 8");
    failures += expect_json_contains(json, "\"elementIndex\":2", "symbol/register write should report aligned element index 2");

    return failures;
}

/// Verifies register-indirect diagnostics for unsupported and invalid forms.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_indirect_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, DWORD PTR [esi * 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "scaled-index register-indirect form should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX, "scaled-index diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, DWORD PTR [eax]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unsupported base register should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "unsupported base register diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov QWORD PTR [esi], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "QWORD PTR register-indirect should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "QWORD PTR register-indirect diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov [esi], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "immediate to register-indirect memory without PTR should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "ambiguous immediate-to-memory diagnostic should match");

    return failures;
}

/// Verifies register-indirect runtime memory errors are structured.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_register_indirect_runtime_error_path(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov esi, 0\n"
        "    mov eax, DWORD PTR [esi]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid register-indirect read should fail");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "invalid register-indirect read should be execution-error");
    failures += expect_json_contains(json, "memory-error", "invalid register-indirect read should include memory diagnostic");

    return failures;
}

/// Verifies unaligned register-indirect accesses execute with warnings.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_unaligned_register_indirect_reports_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET nums\n"
        "    mov DWORD PTR [esi + 9], 100\n"
        "    mov eax, DWORD PTR [esi + 9]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "unaligned register-indirect source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "unaligned register-indirect read should set EAX = 100");
    failures += expect_json_contains(json, "\"byteOffset\":9", "unaligned register-indirect change should report byte offset 9");
    failures += expect_json_contains(json, "\"code\":\"unaligned-memory-access\"", "unaligned register-indirect access should produce warning");
    failures += expect_json_contains(json, "Unaligned DWORD memory access", "unaligned warning should name DWORD width");

    return failures;
}

/// Verifies malformed or unsupported PTR width override diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_ptr_width_override_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov BYTE nums[3], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "missing PTR keyword should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "missing PTR diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov REAL4 PTR nums[0], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unsupported PTR width keyword should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "unsupported PTR width keyword diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov BYTE PTR, 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "missing PTR memory operand should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "missing PTR operand diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov BYTE PTR eax, 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "PTR applied to register should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_EXPECTED_OPERAND, "PTR register diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov QWORD PTR nums[0], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "QWORD PTR executable use should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "QWORD PTR diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov BYTE PTR nums[3], 300\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "BYTE PTR immediate overflow should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "BYTE PTR immediate overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, BYTE PTR nums[3]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "BYTE PTR source to EAX should fail width validation");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SYNTAX, "PTR source width mismatch diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov DWORD PTR [esi], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "register-indirect PTR form should now be supported");

    return failures;
}

/// Verifies browser-facing JSON reports PTR access widths and unaligned warnings.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_wasm_json_reports_ptr_width_memory_changes(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov BYTE PTR nums[3], 100\n"
        "    mov WORD PTR nums[5], 1234h\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":15", "response should identify Milestone 15");
    failures += expect_json_contains(json, "\"ok\":true", "PTR JSON source should execute");
    failures += expect_json_contains(json, "\"symbol\":\"nums\",\"address\":\"00500003h\",\"widthBits\":8,\"byteOffset\":3,\"dataType\":\"BYTE\"", "BYTE PTR change should report BYTE access width");
    failures += expect_json_contains(json, "\"symbol\":\"nums\",\"address\":\"00500005h\",\"widthBits\":16,\"byteOffset\":5,\"dataType\":\"WORD\"", "WORD PTR change should report WORD access width");
    failures += expect_json_contains(json, "\"code\":\"unaligned-memory-access\"", "unaligned WORD PTR write should emit a warning");
    failures += expect_json_contains(json, "Unaligned WORD memory access", "unaligned warning should name WORD width");

    return failures;
}

/// Verifies browser-facing JSON includes symbol-aware memory changes for the acceptance program.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_wasm_json_reports_symbolic_memory_change(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "var BYTE 0\n"
        ".code\n"
        "main PROC\n"
        "    mov var, 100\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":15", "response should identify Milestone 15");
    failures += expect_json_contains(json, "\"ok\":true", "acceptance source should execute");
    failures += expect_json_contains(json, "\"memoryChanges\":[{\"symbol\":\"var\"", "memory changes should include var symbol");
    failures += expect_json_contains(json, "\"oldHex\":\"00h\"", "memory change should include old byte hex");
    failures += expect_json_contains(json, "\"oldUnsigned\":0", "memory change should include old byte decimal");
    failures += expect_json_contains(json, "\"newHex\":\"64h\"", "memory change should include new byte hex");
    failures += expect_json_contains(json, "\"newUnsigned\":100", "memory change should include new byte decimal");

    return failures;
}

/// Test entry point.
///
/// @return Zero when all Milestone 15 tests pass.
int main(void) {
    int failures = 0;

    failures += test_data_layout_symbols_and_initializers();
    failures += test_offset_and_direct_symbol_write_execute();
    failures += test_constant_symbol_offsets_parse_to_ir();
    failures += test_constant_symbol_offsets_execute_acceptance_program();
    failures += test_constant_symbol_offset_error_paths();
    failures += test_constant_symbol_offset_edge_cases();
    failures += test_data_type_aliases_and_mixed_case();
    failures += test_type_operator_parse_to_immediates();
    failures += test_type_operator_executes_acceptance_program();
    failures += test_type_operator_executes_memory_immediate_context();
    failures += test_type_operator_preserves_offset_source_operands();
    failures += test_lengthof_operator_parse_to_immediates();
    failures += test_lengthof_operator_executes_acceptance_program();
    failures += test_lengthof_operator_executes_memory_immediate_context();
    failures += test_lengthof_operator_preserves_type_and_offset_source_operands();
    failures += test_sizeof_operator_parse_to_immediates();
    failures += test_sizeof_operator_and_character_literal_acceptance_program();
    failures += test_sizeof_operator_executes_memory_immediate_context();
    failures += test_character_literal_data_and_source_contexts();
    failures += test_sizeof_operator_preserves_existing_data_operators();
    failures += test_sizeof_operator_error_paths();
    failures += test_lengthof_operator_error_paths();
    failures += test_type_operator_error_paths();
    failures += test_data_error_paths();
    failures += test_negative_data_initializers_and_symbol_writes();
    failures += test_symbol_write_immediate_range_errors();
    failures += test_symbol_write_immediate_range_covers_type_aliases();
    failures += test_data_capacity_error();
    failures += test_ptr_width_overrides_parse_to_ir();
    failures += test_ptr_width_overrides_execute_acceptance_program();
    failures += test_ptr_width_overrides_source_reads_execute();
    failures += test_register_indirect_operands_parse_to_ir();
    failures += test_register_indirect_acceptance_program_executes();
    failures += test_symbol_register_memory_forms_execute();
    failures += test_register_indirect_error_paths();
    failures += test_register_indirect_runtime_error_path();
    failures += test_unaligned_register_indirect_reports_warning();
    failures += test_ptr_width_override_error_paths();
    failures += test_wasm_json_reports_ptr_width_memory_changes();
    failures += test_wasm_json_reports_symbolic_memory_change();

    if (failures != 0) {
        return 1;
    }

    puts("Milestone 15 data section, register-indirect, TYPE, LENGTHOF, SIZEOF, and character literal tests passed.");
    return 0;
}
