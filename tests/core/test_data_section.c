/*
 * @file test_data_section.c
 * @brief Tests data declarations, symbols, memory operands, metadata operators, constant expressions, nested DUP, and later source-run metadata regressions.
 *
 * These tests cover the parser-level data image and symbol table, integration
 * with the existing VM executor, Wasm JSON output, and error paths for the new
 * data-section behavior without adding future control-flow, linker, or Irvine32 scope.
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

/// Number of lexer tokens available to each Milestone 30 parser test.
#define TEST_TOKEN_CAPACITY 256U

/// Number of lexer diagnostics available to each Milestone 30 parser test.
#define TEST_LEXER_DIAGNOSTIC_CAPACITY 32U

/// Number of parser diagnostics available to each Milestone 30 parser test.
#define TEST_PARSER_DIAGNOSTIC_CAPACITY 32U

/// Number of IR instructions available to each Milestone 30 parser test.
#define TEST_INSTRUCTION_CAPACITY 64U

/// Number of source-text bytes available to each Milestone 30 parser test.
#define TEST_SOURCE_TEXT_CAPACITY 1024U

/// Number of data symbols available to each Milestone 30 parser test.
#define TEST_SYMBOL_CAPACITY 32U

/// Number of code labels available to each Milestone 30 parser test.
#define TEST_CODE_LABEL_CAPACITY 32U

/// Number of data image bytes available to each Milestone 30 parser test.
#define TEST_DATA_IMAGE_CAPACITY 512U

/// Number of const image bytes available to each Milestone 30 parser test.
#define TEST_CONST_IMAGE_CAPACITY 512U

/// Holds all caller-owned parser buffers for one Milestone 30 test.
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
    /// Emitted code labels.
    VmCodeLabel code_labels[TEST_CODE_LABEL_CAPACITY];
    /// Emitted .data/.DATA? image bytes.
    uint8_t data_image[TEST_DATA_IMAGE_CAPACITY];
    /// Emitted .CONST image bytes.
    uint8_t const_image[TEST_CONST_IMAGE_CAPACITY];
    /// Per-byte initialized-state mask for emitted .CONST image bytes.
    uint8_t const_initialized_mask[TEST_CONST_IMAGE_CAPACITY];
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

/// Verifies that two VM execution statuses are equal.
///
/// @param actual Actual execution status.
/// @param expected Expected execution status.
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

/// Verifies that a string contains an expected fragment.
///
/// @param actual Actual string to inspect.
/// @param expected_fragment Fragment that must be present.
/// @param message Failure message when the fragment is missing.
/// @return Zero on success, otherwise one failure.
static int expect_string_contains(const char *actual, const char *expected_fragment, const char *message) {
    if (actual == NULL || expected_fragment == NULL || strstr(actual, expected_fragment) == NULL) {
        fprintf(stderr, "FAIL: %s\nExpected fragment: %s\nActual: %s\n", message, expected_fragment != NULL ? expected_fragment : "(null)", actual != NULL ? actual : "(null)");
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


/// Verifies that returned JSON does not contain a forbidden fragment.
///
/// @param json JSON string to inspect.
/// @param unexpected Forbidden substring.
/// @param message Failure message when present.
/// @return Zero on success, otherwise one failure.
static int expect_json_not_contains(const char *json, const char *unexpected, const char *message) {
    if (json != NULL && strstr(json, unexpected) != NULL) {
        fprintf(stderr, "FAIL: %s\nUnexpected: %s\nJSON: %s\n", message, unexpected, json);
        return 1;
    }
    return 0;
}

/// Parses source with full Milestone 30 buffers.
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
    config.code_labels = buffers->code_labels;
    config.code_label_capacity = TEST_CODE_LABEL_CAPACITY;
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

/// Loads parser-produced data image bytes into VM memory for integration tests.
///
/// @param vm VM to initialize.
/// @param buffers Parser buffers containing the data image.
/// @param result Parser result containing data size.
/// @return true when all bytes were written.
static bool load_data_image_for_test(Vm *vm, const DataSectionTestBuffers *buffers, const VmParserResult *result) {
    if (vm == NULL || buffers == NULL || result == NULL) {
        return false;
    }

    if (!vm_memory_status_succeeded(vm_memory_load_region_bytes(&vm->memory, VM_MEMORY_REGION_DATA, 0U, buffers->data_image, (uint32_t)result->data_size))) {
        return false;
    }
    if (!vm_memory_status_succeeded(vm_memory_load_region_bytes(&vm->memory, VM_MEMORY_REGION_CONST, 0U, buffers->const_image, (uint32_t)result->const_size))) {
        return false;
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
        "    mov edx, [nums+16]\n"
        "    mov DWORD PTR [nums +20], 500\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "constant symbol offsets should parse");
    failures += expect_size(result.instruction_count, 9U, "constant symbol offset sample should emit nine instructions");
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
    failures += expect_u32(buffers.instructions[7].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 16U, "[nums+16] source should accept compact unary-plus offset spelling");
    failures += expect_u32(buffers.instructions[8].destination.address, VM_MEMORY_DEFAULT_DATA_BASE + 20U, "DWORD PTR [nums +20] destination should accept unary-plus offset token");

    return failures;
}

/// Verifies the Milestone 29 acceptance program executes through parser and VM.
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

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "Milestone 29 acceptance source should parse");
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

/// Verifies parser diagnostics for malformed symbol offsets and Phase 53A runtime-owned ranges.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_constant_symbol_offset_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [missing + 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unknown bracketed symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "unknown bracketed symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [nums - 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "negative offset before the data image should parse for runtime validation");
    failures += expect_u32(buffers.instructions[0].source.address, VM_MEMORY_DEFAULT_DATA_BASE - 4U, "negative offset should preserve the represented 32-bit address");
    failures += expect_u32(buffers.instructions[0].source.width_bits, 32U, "negative offset should infer DWORD width from the symbol");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [missing]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "unknown offset-zero bracketed symbol should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNKNOWN_SYMBOL, "unknown offset-zero bracketed symbol diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, nums[37]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "offset crossing the data image should parse for runtime validation");
    failures += expect_u32(buffers.instructions[0].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 37U, "crossing offset should preserve the final address");
    failures += expect_u32(buffers.instructions[0].source.width_bits, 32U, "crossing offset should infer DWORD width from the symbol");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, [nums +]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "malformed bracketed offset should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_CONSTANT_EXPRESSION, "malformed bracketed offset diagnostic should match");

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
        "chr  BYTE 'A'\n"
        "pair WORD 'AB'\n"
        "tag  DWORD 'ABCD'\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF nums\n"
        "    mov bl, chr\n"
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
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("mov bl, chr should execute");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("mov cx, pair should execute");
    failures += vm_step(&vm) == VM_EXEC_STATUS_OK ? 0 : record_failure("mov edx, packed literal should execute");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax) ? 0 : record_failure("EAX should be readable after SIZEOF mov");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx) ? 0 : record_failure("EBX should be readable after character mov");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_ECX, &ecx) ? 0 : record_failure("ECX should be readable after packed WORD mov");
    failures += vm_cpu_read_register(&vm.cpu, VM_REGISTER_EDX, &edx) ? 0 : record_failure("EDX should be readable after packed DWORD mov");
    failures += expect_u32(eax, 40U, "SIZEOF nums should leave EAX = 40");
    failures += expect_u32(ebx & 0xFFU, 65U, "mov bl, chr should leave BL = 65");
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
        "shortVal WORD 'A'\n"
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

    failures += expect_parser_status(parse_for_test(".data\narr BYTE 4 DUP(0)\n.code\nmain PROC\nmov eax, OFFSET arr + 4\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "OFFSET arithmetic expression should parse in Phase 29");
    failures += expect_u32(buffers.instructions[0].source.immediate, VM_MEMORY_DEFAULT_DATA_BASE + 4U, "OFFSET arr + 4 should produce a static address immediate");

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

/// Verifies parser diagnostics for new Milestone 29 error paths.
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

/// Verifies the Milestone 29 acceptance program executes explicit-width writes.
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
        "    mov DWORD PTR [esi+12], 7\n"
        "    mov ebx, DWORD PTR [esi +16]\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "register-indirect sample should parse");
    failures += expect_size(result.instruction_count, 9U, "register-indirect sample should emit nine instructions");
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
    failures += expect_u32(buffers.instructions[7].destination.immediate, 12U, "DWORD PTR [esi+12] should accept compact unary-plus displacement spelling");
    failures += expect_u32(buffers.instructions[8].source.immediate, 16U, "DWORD PTR [esi +16] should accept unary-plus displacement token");

    return failures;
}

/// Verifies the Milestone 29 register-indirect acceptance program executes.
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

/// Verifies all 32-bit general-purpose registers parse as memory bases.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_all_gpr_register_indirect_bases_parse_to_ir(void) {
    const char *source =
        ".data\n"
        "nums DWORD 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov DWORD PTR [eax], 1\n"
        "    mov DWORD PTR [ebx + 4], 2\n"
        "    mov DWORD PTR [ecx - 4], 3\n"
        "    mov DWORD PTR [edx + 8], 4\n"
        "    mov DWORD PTR [esi], 5\n"
        "    mov DWORD PTR [edi + 12], 6\n"
        "    mov DWORD PTR [ebp - 8], 7\n"
        "    mov DWORD PTR [esp + 16], 8\n"
        "    mov DWORD PTR nums[eax], 9\n"
        "    mov eax, DWORD PTR [nums + ecx]\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "all-GPR register-indirect sample should parse");
    failures += expect_size(result.instruction_count, 10U, "all-GPR register-indirect sample should emit ten instructions");
    failures += expect_u32(buffers.instructions[0].destination.reg, VM_REGISTER_EAX, "[eax] should use EAX as base");
    failures += expect_u32(buffers.instructions[1].destination.reg, VM_REGISTER_EBX, "[ebx + 4] should use EBX as base");
    failures += expect_u32(buffers.instructions[1].destination.immediate, 4U, "[ebx + 4] should store displacement 4");
    failures += expect_u32(buffers.instructions[2].destination.reg, VM_REGISTER_ECX, "[ecx - 4] should use ECX as base");
    failures += expect_u32(buffers.instructions[2].destination.immediate, (uint32_t)-4, "[ecx - 4] should store displacement -4");
    failures += expect_u32(buffers.instructions[3].destination.reg, VM_REGISTER_EDX, "[edx + 8] should use EDX as base");
    failures += expect_u32(buffers.instructions[4].destination.reg, VM_REGISTER_ESI, "[esi] should remain supported");
    failures += expect_u32(buffers.instructions[5].destination.reg, VM_REGISTER_EDI, "[edi + 12] should remain supported");
    failures += expect_u32(buffers.instructions[6].destination.reg, VM_REGISTER_EBP, "[ebp - 8] should remain supported");
    failures += expect_u32(buffers.instructions[6].destination.immediate, (uint32_t)-8, "[ebp - 8] should store displacement -8");
    failures += expect_u32(buffers.instructions[7].destination.reg, VM_REGISTER_ESP, "[esp + 16] should use ESP as base");
    failures += expect_u32(buffers.instructions[8].destination.reg, VM_REGISTER_EAX, "nums[eax] should use EAX as runtime byte offset");
    failures += expect_u32(buffers.instructions[8].destination.address, VM_MEMORY_DEFAULT_DATA_BASE, "nums[eax] should include the symbol base");
    failures += expect_u32(buffers.instructions[9].source.reg, VM_REGISTER_ECX, "[nums + ecx] should use ECX as runtime byte offset");
    failures += expect_u32(buffers.instructions[9].source.address, VM_MEMORY_DEFAULT_DATA_BASE, "[nums + ecx] should include the symbol base");

    return failures;
}

/// Verifies all 32-bit general-purpose register bases execute through source-run.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_all_gpr_register_indirect_bases_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET nums\n"
        "    mov ebx, OFFSET nums\n"
        "    mov ecx, OFFSET nums\n"
        "    mov edx, OFFSET nums\n"
        "    mov esi, OFFSET nums\n"
        "    mov edi, OFFSET nums\n"
        "    mov ebp, OFFSET nums\n"
        "    mov esp, OFFSET nums\n"
        "    mov DWORD PTR [eax], 10\n"
        "    mov DWORD PTR [ebx + 4], 20\n"
        "    mov DWORD PTR [ecx + 8], 30\n"
        "    mov DWORD PTR [edx + 12], 40\n"
        "    mov DWORD PTR [esi + 16], 50\n"
        "    mov DWORD PTR [edi + 20], 60\n"
        "    mov DWORD PTR [ebp + 24], 70\n"
        "    mov DWORD PTR [esp + 28], 80\n"
        "    mov eax, DWORD PTR [esp + 28]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":75", "all-GPR response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "all-GPR register-indirect source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000050h\",\"unsigned\":80}", "all-GPR register-indirect read should set EAX = 80");
    failures += expect_json_contains(json, "\"address\":\"0050001Ch\"", "ESP-based write should reach nums + 28");

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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":75", "response should identify current runtime phase");
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

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, DWORD PTR [eax * 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "scaled-index EAX register-indirect form should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX, "scaled-index EAX diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, DWORD PTR nums[esi * 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "scaled-index array register form should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX, "scaled-index array diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, DWORD PTR [nums + esi * 4]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "scaled-index symbol-plus-register form should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX, "scaled-index symbol-plus-register diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov eax, DWORD PTR [ax]\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "non-32-bit base register should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_REGISTER_INDIRECT_BASE, "non-32-bit base register diagnostic should match");

    failures += expect_parser_status(parse_for_test(".data\nnums DWORD 10 DUP(0)\n.code\nmain PROC\nmov QWORD PTR [esi], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "QWORD PTR register-indirect should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "QWORD PTR register-indirect diagnostic should match");

    failures += expect_parser_status(parse_for_test(".code\nmain PROC\nmov [esi], 1\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "immediate to register-indirect memory without PTR should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_AMBIGUOUS_MEMORY_WIDTH, "ambiguous immediate-to-memory diagnostic should match");

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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid register-indirect read should fail");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "invalid register-indirect read should be execution-error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "invalid register-indirect read should include memory status diagnostic");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes", "invalid register-indirect read should describe address and width");
    failures += expect_json_contains(json, "\"line\":4", "invalid register-indirect read should preserve source line");
    failures += expect_json_contains(json, "\"column\":24", "invalid register-indirect read should highlight bracketed memory operand");
    failures += expect_json_contains(json, "\"spanLength\":5", "invalid register-indirect read should report bracketed operand span length");
    failures += expect_json_not_contains(json, "Execution failed while running the parsed program", "invalid register-indirect read should not use vague execution failure wording");

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
        "    ret\n"
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
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "PTR source width mismatch diagnostic should match");

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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":75", "response should identify current runtime phase");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":75", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "acceptance source should execute");
    failures += expect_json_contains(json, "\"memoryChanges\":[{\"symbol\":\"var\"", "memory changes should include var symbol");
    failures += expect_json_contains(json, "\"oldHex\":\"00h\"", "memory change should include old byte hex");
    failures += expect_json_contains(json, "\"oldUnsigned\":0", "memory change should include old byte decimal");
    failures += expect_json_contains(json, "\"newHex\":\"64h\"", "memory change should include new byte hex");
    failures += expect_json_contains(json, "\"newUnsigned\":100", "memory change should include new byte decimal");

    return failures;
}


/// Verifies signed integer declarations lay out data and metadata correctly.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_signed_integer_data_declarations_layout_and_metadata(void) {
    const char *source =
        ".data\n"
        "sb SBYTE -1, 127\n"
        "sw SWORD -2\n"
        "sd SDWORD -3\n"
        "sq SQWORD -4\n"
        "arr SWORD 3 DUP(-1)\n"
        "plus SDWORD +42\n"
        "pair SWORD 'AB'\n"
        "uninit SQWORD ?\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "signed integer declarations should parse");
    failures += expect_size(result.symbol_count, 8U, "signed declaration sample should create eight symbols");
    failures += expect_u8(buffers.data_image[0], 0xFFU, "SBYTE -1 should encode FF");
    failures += expect_u8(buffers.data_image[1], 0x7FU, "SBYTE 127 should encode 7F");
    failures += expect_u8(buffers.data_image[2], 0xFEU, "SWORD -2 low byte should encode FE");
    failures += expect_u8(buffers.data_image[3], 0xFFU, "SWORD -2 high byte should encode FF");
    failures += expect_u8(buffers.data_image[4], 0xFDU, "SDWORD -3 low byte should encode FD");
    failures += expect_u8(buffers.data_image[8], 0xFCU, "SQWORD -4 low byte should encode FC");
    failures += expect_u8(buffers.data_image[16], 0xFFU, "SWORD DUP(-1) first byte should encode FF");
    failures += expect_u8(buffers.data_image[22], 0x2AU, "SDWORD +42 low byte should encode 2A");
    failures += expect_u8(buffers.data_image[26], 0x41U, "SWORD 'AB' low byte should encode A");
    failures += expect_u8(buffers.data_image[27], 0x42U, "SWORD 'AB' high byte should encode B");
    failures += expect_u8(buffers.symbols[0].element_size_bytes, 1U, "SBYTE symbol should have one-byte elements");
    failures += expect_u32(buffers.symbols[0].element_count, 2U, "SBYTE comma initializer should count two elements");
    failures += expect_u8(buffers.symbols[4].element_size_bytes, 2U, "SWORD DUP symbol should have two-byte elements");
    failures += expect_u32(buffers.symbols[4].element_count, 3U, "SWORD DUP symbol should count repeated elements");
    failures += expect_u32(buffers.symbols[7].size_bytes, 8U, "SQWORD ? should reserve eight bytes");
    if (!buffers.symbols[7].has_uninitialized_initializer) {
        failures += record_failure("SQWORD ? should retain uninitialized metadata");
    }
    if (!vm_symbol_data_type_is_signed(buffers.symbols[0].data_type) || !vm_symbol_data_type_is_signed(buffers.symbols[3].data_type)) {
        failures += record_failure("signed symbol data types should be marked signed");
    }

    return failures;
}

/// Verifies signed declaration range validation rejects values outside signed bounds.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_signed_integer_range_errors(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(".data\nsb SBYTE 127\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SBYTE 127 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsb SBYTE -128\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SBYTE -128 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsb SBYTE 128\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SBYTE 128 should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "SBYTE positive overflow diagnostic should match");
    failures += expect_parser_status(parse_for_test(".data\nsb SBYTE -129\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SBYTE -129 should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "SBYTE negative overflow diagnostic should match");
    failures += expect_parser_status(parse_for_test(".data\nsw SWORD 32767\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SWORD 32767 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsw SWORD -32768\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SWORD -32768 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsw SWORD 32768\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SWORD positive overflow should fail");
    failures += expect_parser_status(parse_for_test(".data\nsw SWORD -32769\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SWORD negative overflow should fail");
    failures += expect_parser_status(parse_for_test(".data\nsd SDWORD 2147483647\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SDWORD 2147483647 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsd SDWORD -2147483648\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SDWORD -2147483648 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsd SDWORD 2147483648\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SDWORD positive overflow should fail");
    failures += expect_parser_status(parse_for_test(".data\nsd SDWORD -2147483649\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SDWORD negative overflow should fail");
    failures += expect_parser_status(parse_for_test(".data\nsq SQWORD 9223372036854775807\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SQWORD 9223372036854775807 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsq SQWORD -9223372036854775808\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK, "SQWORD -9223372036854775808 should parse");
    failures += expect_parser_status(parse_for_test(".data\nsq SQWORD 9223372036854775808\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SQWORD positive overflow should fail");
    failures += expect_parser_status(parse_for_test(".data\nsq SQWORD -9223372036854775809\n.code\nmain PROC\nmain ENDP\nEND main\n", &buffers, &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SQWORD negative overflow should fail");

    return failures;
}

/// Verifies signed metadata operators and 64-bit execution diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_signed_integer_metadata_and_64bit_execution_limits(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "sq SQWORD -4\n"
        "arr SWORD 3 DUP(-1)\n"
        ".code\n"
        "main PROC\n"
        "mov eax, TYPE sq\n"
        "mov ebx, LENGTHOF arr\n"
        "mov ecx, SIZEOF arr\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK, "signed metadata operator program should parse");
    failures += expect_u32(buffers.instructions[0].source.immediate, 8U, "TYPE SQWORD should produce 8");
    failures += expect_u32(buffers.instructions[1].source.immediate, 3U, "LENGTHOF SWORD DUP should produce 3");
    failures += expect_u32(buffers.instructions[2].source.immediate, 6U, "SIZEOF SWORD DUP should produce 6");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "sq SQWORD -1\n"
        ".code\n"
        "main PROC\n"
        "mov SQWORD PTR sq, 1\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SQWORD PTR executable use should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "SQWORD PTR diagnostic should match");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "sb SBYTE -1\n"
        ".code\n"
        "main PROC\n"
        "mov eax, sb\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "ordinary mov eax, SBYTE memory should not auto sign-extend");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_OPERAND_WIDTH_MISMATCH, "ordinary mov width mismatch should remain explicit");

    return failures;
}


/// Verifies signed PTR aliases parse as executable 8-, 16-, and 32-bit memory widths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_signed_ptr_width_aliases_parse_to_ir(void) {
    const char *source =
        ".data\n"
        "b SBYTE -1\n"
        "w SWORD -2\n"
        "d SDWORD -3\n"
        "buf BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov al, sbyte ptr b\n"
        "    mov bx, SwOrD Ptr w\n"
        "    mov ecx, sdword ptr d\n"
        "    mov sbyte ptr buf[0], -1\n"
        "    mov SWORD PTR buf[2], -2\n"
        "    mov SDWORD PTR buf[4], -3\n"
        "main ENDP\n"
        "END main\n";
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "signed PTR alias source should parse");
    failures += expect_size(result.instruction_count, 6U, "signed PTR source should emit six instructions");
    failures += expect_u32(buffers.instructions[0].source.width_bits, 8U, "SBYTE PTR source should emit 8-bit width");
    failures += expect_u32(buffers.instructions[1].source.width_bits, 16U, "SWORD PTR source should emit 16-bit width");
    failures += expect_u32(buffers.instructions[2].source.width_bits, 32U, "SDWORD PTR source should emit 32-bit width");
    failures += expect_u32(buffers.instructions[3].destination.width_bits, 8U, "SBYTE PTR destination should emit 8-bit width");
    failures += expect_u32(buffers.instructions[4].destination.width_bits, 16U, "SWORD PTR destination should emit 16-bit width");
    failures += expect_u32(buffers.instructions[5].destination.width_bits, 32U, "SDWORD PTR destination should emit 32-bit width");

    return failures;
}

/// Verifies signed PTR aliases execute through the source-run path.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_signed_ptr_width_aliases_source_run_programs(void) {
    const char *read_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "b SBYTE -1\n"
        "w SWORD -2\n"
        "d SDWORD -3\n"
        ".code\n"
        "main PROC\n"
        "    mov al, SBYTE PTR b\n"
        "    mov bx, SWORD PTR w\n"
        "    mov ecx, SDWORD PTR d\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    char read_copy[1024];
    const char *write_json = NULL;
    int failures = 0;

    if (read_json == NULL) {
        return record_failure("signed PTR read source-run result should not be NULL");
    }
    (void)snprintf(read_copy, sizeof(read_copy), "%s", read_json);

    write_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf BYTE 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET buf\n"
        "    mov SBYTE PTR [esi], -1\n"
        "    mov al, BYTE PTR [esi]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(read_copy, "\"phase\":75", "signed PTR read response should identify current runtime phase");
    failures += expect_json_contains(read_copy, "\"ok\":true", "signed PTR read source should execute");
    failures += expect_json_contains(read_copy, "\"EAX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "SBYTE PTR read into AL should not sign-extend");
    failures += expect_json_contains(read_copy, "\"EBX\":{\"hex\":\"0000FFFEh\",\"unsigned\":65534}", "SWORD PTR read into BX should preserve raw 16-bit value");
    failures += expect_json_contains(read_copy, "\"ECX\":{\"hex\":\"FFFFFFFDh\",\"unsigned\":4294967293}", "SDWORD PTR read into ECX should preserve raw 32-bit value");
    failures += expect_json_contains(write_json, "\"ok\":true", "signed PTR write source should execute");
    failures += expect_json_contains(write_json, "\"EAX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "SBYTE PTR write should be readable as BYTE PTR");
    failures += expect_json_contains(write_json, "\"symbol\":\"buf\",\"address\":\"00500000h\",\"widthBits\":8", "signed PTR write should report 8-bit memory change");
    failures += expect_json_contains(write_json, "\"newHex\":\"FFh\"", "signed PTR write should store FFh");

    return failures;
}

/// Verifies `.DATA?` and `.CONST` layout, execution, and read-only diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_additional_data_sections_layout_and_const_protection(void) {
    DataSectionTestBuffers buffers;
    DataSectionTestBuffers scalar_buffers;
    DataSectionTestBuffers const_uninitialized_buffers;
    DataSectionTestBuffers malformed_const_buffers;
    DataSectionTestBuffers unsupported_const_buffers;
    VmParserResult result;
    VmParserResult scalar_result;
    VmParserResult const_uninitialized_result;
    VmParserResult malformed_const_result;
    VmParserResult unsupported_const_result;
    Vm vm;
    VmExecStatus exec_status = VM_EXEC_STATUS_OK;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    const char *acceptance_json = NULL;
    const char *direct_write_json = NULL;
    const char *indirect_write_json = NULL;
    const char *direct_offset_write_json = NULL;
    const char *bracketed_const_write_json = NULL;
    const char *xchg_const_write_json = NULL;
    const char *calculated_offset_write_json = NULL;
    const char *partial_overlap_const_write_json = NULL;
    const char *initialized_data_question_json = NULL;
    const char *mixed_data_question_json = NULL;
    const char *const_uninitialized_json = NULL;
    const char *add_const_write_json = NULL;
    const char *neg_const_write_json = NULL;
    int failures = 0;

    const char *source =
        ".DATA?\n"
        "buf BYTE 16 DUP(?)\n"
        ".data\n"
        "x DWORD 1\n"
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF buf\n"
        "    mov ebx, limit\n"
        "main ENDP\n"
        "END main\n";

    failures += expect_parser_status(parse_for_test(source, &buffers, &result), VM_PARSER_STATUS_OK, "additional data sections should parse");
    failures += expect_size(result.symbol_count, 3U, "additional data sections should preserve source-order symbols");
    failures += expect_size(result.data_size, 20U, ".DATA? and .data should share writable data image bytes");
    failures += expect_size(result.const_size, 4U, ".CONST should emit separate const image bytes");
    failures += expect_u32((uint32_t)buffers.symbols[0].section, (uint32_t)VM_SYMBOL_SECTION_DATA_UNINITIALIZED, "buf should be marked .DATA?");
    failures += expect_u32((uint32_t)buffers.symbols[1].section, (uint32_t)VM_SYMBOL_SECTION_DATA, "x should be marked .data");
    failures += expect_u32((uint32_t)buffers.symbols[2].section, (uint32_t)VM_SYMBOL_SECTION_CONST, "limit should be marked .CONST");
    failures += expect_u32(buffers.symbols[0].has_uninitialized_storage ? 1U : 0U, 1U, ".DATA? symbol should retain uninitialized metadata");
    failures += expect_u32(vm_symbol_is_uninitialized_storage(&buffers.symbols[0]) ? 1U : 0U, 1U, ".DATA? helper should identify uninitialized storage");
    failures += expect_u32(vm_symbol_is_read_only(&buffers.symbols[2]) ? 1U : 0U, 1U, ".CONST helper should identify read-only symbols");
    if (strcmp(vm_symbol_section_name(VM_SYMBOL_SECTION_DATA_UNINITIALIZED), ".DATA?") != 0) {
        failures += record_failure("section helper should name .DATA?");
    }
    if (strcmp(vm_symbol_section_name(VM_SYMBOL_SECTION_CONST), ".CONST") != 0) {
        failures += record_failure("section helper should name .CONST");
    }
    failures += expect_u32(buffers.data_image[0], 0U, ".DATA? storage should be deterministic zero-filled");
    failures += expect_u32(buffers.data_image[16], 1U, ".data bytes should follow .DATA? bytes");
    failures += expect_u32(buffers.const_image[0], 10U, ".CONST bytes should be initialized");
    failures += expect_u32(buffers.symbols[2].address, VM_MEMORY_DEFAULT_CONST_BASE, ".CONST symbol should use const region base");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "x DWORD ?\n"
        "buf BYTE 16 DUP(?)\n"
        "words WORD 4 DUP(?)\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &const_uninitialized_buffers,
        &const_uninitialized_result), VM_PARSER_STATUS_OK, "Phase 57I .CONST ? and DUP(?) storage should parse");
    failures += expect_size(const_uninitialized_result.symbol_count, 3U, "Phase 57I .CONST source should emit three symbols");
    failures += expect_size(const_uninitialized_result.const_size, 28U, "Phase 57I .CONST uninitialized storage should occupy expected bytes");
    failures += expect_u32((uint32_t)const_uninitialized_buffers.symbols[0].section, (uint32_t)VM_SYMBOL_SECTION_CONST, ".CONST ? symbol should be in .CONST");
    failures += expect_u32(const_uninitialized_buffers.symbols[0].has_uninitialized_storage ? 1U : 0U, 1U, ".CONST ? symbol should retain uninitialized-origin metadata");
    failures += expect_u32(const_uninitialized_buffers.symbols[1].element_count, 16U, ".CONST BYTE DUP(?) should count elements");
    failures += expect_u32(const_uninitialized_buffers.symbols[2].element_count, 4U, ".CONST WORD DUP(?) should count elements");
    failures += expect_u8(const_uninitialized_buffers.const_image[0], 0U, ".CONST ? visible bytes should default to zero");
    failures += expect_u8(const_uninitialized_buffers.const_initialized_mask[0], 0U, ".CONST ? mask should mark uninitialized-origin bytes");
    failures += expect_u8(const_uninitialized_buffers.const_initialized_mask[27], 0U, ".CONST DUP(?) mask should mark final byte uninitialized-origin");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "bad BYTE 0 DUP(?)\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &malformed_const_buffers,
        &malformed_const_result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57I malformed .CONST DUP(?) should still be rejected");
    failures += expect_parser_diagnostic_code(malformed_const_buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_DUP, "Malformed .CONST DUP(?) diagnostic should remain invalid-dup");

    failures += expect_parser_status(parse_for_test(
        ".CONST\n"
        "bad SBYTE 128\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &unsupported_const_buffers,
        &unsupported_const_result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 57I unsupported .CONST initializer values should still be rejected");
    failures += expect_parser_diagnostic_code(unsupported_const_buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "Unsupported .CONST initializer diagnostic should remain number-out-of-range");

    failures += expect_parser_status(parse_for_test(
        ".DATA?\n"
        "first BYTE ?\n"
        "second WORD ?, ?\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &scalar_buffers,
        &scalar_result), VM_PARSER_STATUS_OK, ".DATA? should accept scalar ? and comma-separated ? initializers");
    failures += expect_size(scalar_result.symbol_count, 2U, ".DATA? scalar initializer source should emit two symbols");
    failures += expect_size(scalar_result.data_size, 5U, ".DATA? scalar and comma initializers should reserve expected bytes");
    failures += expect_u32(scalar_buffers.symbols[0].element_count, 1U, ".DATA? scalar ? should count one element");
    failures += expect_u32(scalar_buffers.symbols[1].element_count, 2U, ".DATA? comma ? should count two WORD elements");
    failures += expect_u32(vm_symbol_is_uninitialized_storage(&scalar_buffers.symbols[1]) ? 1U : 0U, 1U, ".DATA? comma ? symbol should retain uninitialized metadata");

    memset(&vm, 0, sizeof(vm));
    exec_status = vm_init(&vm, NULL);
    failures += expect_exec_status(exec_status, VM_EXEC_STATUS_OK, "VM should initialize for additional data section execution");
    if (exec_status == VM_EXEC_STATUS_OK) {
        failures += load_data_image_for_test(&vm, &buffers, &result) ? 0 : record_failure("additional data images should load into VM memory");
        exec_status = vm_load_program(&vm, buffers.instructions, result.instruction_count);
        failures += expect_exec_status(exec_status, VM_EXEC_STATUS_OK, "additional data program should load");
        while (exec_status == VM_EXEC_STATUS_OK && !vm.halted) {
            exec_status = vm_step(&vm);
        }
        failures += expect_exec_status(exec_status, VM_EXEC_STATUS_CODE_FELL_OFF_END, "additional data program should report Phase 71C code-end falloff after executing accepted instructions");
        (void)vm_cpu_read_register(&vm.cpu, VM_REGISTER_EAX, &eax);
        (void)vm_cpu_read_register(&vm.cpu, VM_REGISTER_EBX, &ebx);
        failures += expect_u32(eax, 16U, "SIZEOF .DATA? buffer should be 16");
        failures += expect_u32(ebx, 10U, ".CONST read should load value 10");
        vm_deinit(&vm);
    }

    acceptance_json = masm32_sim_wasm_run_source_json(source);
    failures += expect_json_contains(acceptance_json, "\"phase\":75", "additional data source-run should identify current runtime phase");
    failures += expect_json_contains(acceptance_json, "\"EAX\":{\"hex\":\"00000010h\",\"unsigned\":16}", "source-run should report SIZEOF buf in EAX");
    failures += expect_json_contains(acceptance_json, "\"EBX\":{\"hex\":\"0000000Ah\",\"unsigned\":10}", "source-run should report .CONST read in EBX");

    direct_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov limit, 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(direct_write_json, "\"ok\":false", "direct .CONST write should fail assembly");
    failures += expect_json_contains(direct_write_json, "const-write", "direct .CONST write should use const-write diagnostic");

    direct_offset_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov BYTE PTR limit[0], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(direct_offset_write_json, "const-write", "direct .CONST offset write should use const-write diagnostic");

    bracketed_const_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov DWORD PTR [limit], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(bracketed_const_write_json, "const-write", "bracketed .CONST write should use const-write diagnostic");

    xchg_const_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 20\n"
        "    xchg eax, limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(xchg_const_write_json, "const-write", "xchg with .CONST memory should use const-write diagnostic because xchg writes both operands");

    add_const_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    add limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(add_const_write_json, "const-write", "add with .CONST destination should use const-write diagnostic");

    neg_const_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    neg limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(neg_const_write_json, "const-write", "neg with .CONST destination should use const-write diagnostic");

    indirect_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(indirect_write_json, "\"ok\":false", "indirect .CONST write should fail at runtime");
    failures += expect_json_contains(indirect_write_json, "permission-denied", "indirect .CONST write should fail through memory permissions");
    failures += expect_json_contains(indirect_write_json, ".const", "indirect .CONST write diagnostic should name .const region");

    calculated_offset_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov BYTE PTR [eax + 3], 0FFh\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(calculated_offset_write_json, "permission-denied", "calculated .CONST offset write should fail through memory permissions");

    partial_overlap_const_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax - 1], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(partial_overlap_const_write_json, "\"ok\":false", "partial-overlap write crossing into .CONST should fail");
    failures += expect_json_contains(partial_overlap_const_write_json, "region-boundary-crossing", "partial-overlap write crossing into .CONST should use protected-region boundary diagnostic");
    failures += expect_json_contains(partial_overlap_const_write_json, "Cross-region memory write", "partial-overlap .CONST diagnostic should identify a cross-region write");
    failures += expect_json_contains(partial_overlap_const_write_json, "\"memoryChanges\":[]", "failed partial-overlap .CONST write should not report successful memory changes");

    initialized_data_question_json = masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "buf DWORD 5\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(initialized_data_question_json, "\"ok\":false", ".DATA? initialized numeric storage should be rejected");
    failures += expect_json_contains(initialized_data_question_json, ".DATA? declarations must use ? or DUP(?)", ".DATA? numeric rejection should explain uninitialized syntax");

    mixed_data_question_json = masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "buf BYTE ?, 1\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(mixed_data_question_json, "\"ok\":false", ".DATA? mixed initialized storage should be rejected");
    failures += expect_json_contains(mixed_data_question_json, ".DATA? declarations must use ? or DUP(?)", ".DATA? mixed rejection should explain uninitialized syntax");

    const_uninitialized_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD ?\n"
        "buf BYTE 4 DUP(?)\n"
        "words WORD 2 DUP(?)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, limit\n"
        "    mov bl, buf\n"
        "    mov cx, words\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(const_uninitialized_json, "\"ok\":true", ".CONST uninitialized storage should now be accepted");
    failures += expect_json_contains(const_uninitialized_json, "\"phaseSuffix\":\"\"", ".CONST uninitialized source-run should report the current runtime phase suffix");
    failures += expect_json_contains(const_uninitialized_json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", ".CONST DWORD ? read should return deterministic zero by default");
    failures += expect_json_contains(const_uninitialized_json, "\"code\":\"uninitialized-read\"", ".CONST ? read should preserve uninitialized-origin warning metadata");
    failures += expect_json_contains(const_uninitialized_json, "reads 4 bytes from limit + 0", ".CONST ? read warning should identify the symbol");

    return failures;
}


/// Verifies Phase 28 numeric equates and simple constant-expression behavior remains covered as a regression.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase28_numeric_equates_and_constant_expressions_regression(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    const char *json = NULL;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        "COUNT = 4\n"
        "EXTRA EQU 2\n"
        ".stack COUNT + EXTRA\n"
        ".data\n"
        "arr DWORD COUNT DUP(0)\n"
        "value DWORD (COUNT + EXTRA)\n"
        "more BYTE (COUNT + EXTRA) DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, COUNT + EXTRA\n"
        "    mov ebx, SIZEOF arr\n"
        "    mov ecx, OFFSET arr + COUNT\n"
        "    mov edx, [arr + COUNT]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Phase 29 parser acceptance source should parse with Phase 53D .stack metadata notice");
    failures += expect_size(result.diagnostic_count, 1U, "Phase 29 parser acceptance source should emit one .stack compatibility notice");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_METADATA_ONLY, ".stack expression should emit compatibility-metadata-only notice");
    failures += expect_size(result.symbol_count, 3U, "numeric equates should not be stored as data symbols");
    failures += expect_u32(result.has_requested_stack_size ? result.requested_stack_size : 0U, 6U, ".stack should accept a constant expression");
    failures += expect_u32(buffers.symbols[0].element_count, 4U, "DUP count should resolve through an equate");
    failures += expect_u32(buffers.symbols[0].size_bytes, 16U, "arr DWORD COUNT DUP(0) should occupy 16 bytes");
    failures += expect_u32(buffers.symbols[2].element_count, 6U, "parenthesized expression DUP count should resolve to six BYTE elements");
    failures += expect_u32(buffers.instructions[0].source.immediate, 6U, "instruction immediate expression should fold to 6");
    failures += expect_u32(buffers.instructions[2].source.immediate, VM_MEMORY_DEFAULT_DATA_BASE + 4U, "OFFSET symbol + constant should fold to static address");
    failures += expect_u32(buffers.instructions[3].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 4U, "symbol-offset memory operand should accept a constant expression");
    failures += expect_u8(buffers.data_image[16], 6U, "data initializer expression should encode value 6");

    json = masm32_sim_wasm_run_source_json(
        "COUNT = 4\n"
        "EXTRA EQU 2\n"
        ".data\n"
        "arr DWORD COUNT DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, COUNT + EXTRA\n"
        "    mov ebx, SIZEOF arr\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 29 acceptance source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000006h\",\"unsigned\":6}", "Phase 29 acceptance source should set EAX = 6");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000010h\",\"unsigned\":16}", "Phase 29 acceptance source should set EBX = SIZEOF arr");

    return failures;
}

/// Verifies Phase 29 expression and equate error paths remain structured.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase29_expression_and_equate_error_paths(void) {
    const char *text_equ_json = NULL;
    const char *recursive_json = NULL;
    const char *unknown_json = NULL;
    const char *offset_equate_json = NULL;
    const char *unsupported_operator_json = NULL;
    int failures = 0;

    text_equ_json = masm32_sim_wasm_run_source_json(
        "GREETING EQU <Hello>\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(text_equ_json, "\"ok\":false", "text EQU should be rejected");
    failures += expect_json_contains(text_equ_json, "invalid-equate", "text EQU should report a structured invalid-equate diagnostic");
    failures += expect_json_contains(text_equ_json, "Text EQU constants are not accepted", "text EQU diagnostic should explain numeric-only support");

    recursive_json = masm32_sim_wasm_run_source_json(
        "COUNT EQU COUNT + 1\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(recursive_json, "\"ok\":false", "recursive equate should be rejected");
    failures += expect_json_contains(recursive_json, "recursive-equate", "recursive equate should use a structured diagnostic");

    unknown_json = masm32_sim_wasm_run_source_json(
        "COUNT EQU MISSING + 1\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(unknown_json, "\"ok\":false", "unknown equate dependency should be rejected");
    failures += expect_json_contains(unknown_json, "unknown-equate", "unknown equate dependency should use a structured diagnostic");

    offset_equate_json = masm32_sim_wasm_run_source_json(
        "COUNT = 4\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET COUNT\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(offset_equate_json, "\"ok\":false", "OFFSET equate should be rejected");
    failures += expect_json_contains(offset_equate_json, "OFFSET requires a data symbol", "OFFSET equate diagnostic should explain data-symbol requirement");

    unsupported_operator_json = masm32_sim_wasm_run_source_json(
        "COUNT = 4 EQ 2\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(unsupported_operator_json, "\"ok\":false", "unsupported high-level condition expression operator should be rejected");
    failures += expect_json_contains(unsupported_operator_json, "unsupported-constant-expression", "unsupported high-level condition operator should use a structured diagnostic");

    return failures;
}


/// Verifies Milestone 29 extended compile-time expression operators.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase29_extended_constant_expressions(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    const char *json = NULL;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        "COUNT = 4 * 3\n"
        "MASK EQU 1 SHL 7\n"
        "A EQU 2 + 3 * 4\n"
        "B EQU (2 + 3) * 4\n"
        "C EQU 32 / 4 / 2\n"
        "D EQU 17 MOD 5\n"
        "E EQU 1 SHL 3 + 1\n"
        "F EQU 64 SHR 2 + 1\n"
        "G EQU 0F0h AND 33h OR 4\n"
        "H EQU 1 OR 2 XOR 3\n"
        "I EQU NOT 0 AND 0FFh\n"
        "J EQU LOW 1234h\n"
        "K EQU HIGH 1234h\n"
        "L EQU LOWWORD 12345678h\n"
        "M EQU HIGHWORD 12345678h\n"
        ".stack MASK / 32\n"
        ".data\n"
        "arr BYTE COUNT DUP(0)\n"
        "value DWORD MASK / 2\n"
        "wordval WORD LOWWORD 12345678h\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, A\n"
        "    mov eax, B\n"
        "    mov eax, C\n"
        "    mov eax, D\n"
        "    mov eax, E\n"
        "    mov eax, F\n"
        "    mov eax, G\n"
        "    mov eax, H\n"
        "    mov eax, I\n"
        "    mov eax, J\n"
        "    mov eax, K\n"
        "    mov eax, L\n"
        "    mov eax, M\n"
        "    mov ebx, OFFSET arr + 1 SHL 2\n"
        "    mov dl, [arr + 1 SHL 1]\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "Milestone 29 extended expression source should parse with Phase 53D .stack metadata notice");
    failures += expect_size(result.diagnostic_count, 1U, "Milestone 29 extended expression source should produce one .stack compatibility notice");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_COMPATIBILITY_METADATA_ONLY, "Extended expression .stack should emit compatibility-metadata-only notice");
    failures += expect_u32(result.has_requested_stack_size ? result.requested_stack_size : 0U, 4U, ".stack should accept Milestone 29 expressions");
    failures += expect_u32(buffers.symbols[0].element_count, 12U, "DUP count should accept multiplication expression");
    failures += expect_u8(buffers.data_image[12], 0x40U, "data initializer should accept division over equates");
    failures += expect_u8(buffers.data_image[16], 0x78U, "WORD initializer should accept LOWWORD low byte");
    failures += expect_u8(buffers.data_image[17], 0x56U, "WORD initializer should accept LOWWORD high byte");
    failures += expect_u32(buffers.instructions[0].source.immediate, 14U, "multiplication should bind tighter than addition");
    failures += expect_u32(buffers.instructions[1].source.immediate, 20U, "parentheses should override precedence");
    failures += expect_u32(buffers.instructions[2].source.immediate, 4U, "division should be left-associative");
    failures += expect_u32(buffers.instructions[3].source.immediate, 2U, "MOD should compute remainder");
    failures += expect_u32(buffers.instructions[4].source.immediate, 16U, "additive expression should bind tighter than SHL right operand");
    failures += expect_u32(buffers.instructions[5].source.immediate, 8U, "SHR should accept additive shift count");
    failures += expect_u32(buffers.instructions[6].source.immediate, 0x34U, "AND should bind tighter than OR");
    failures += expect_u32(buffers.instructions[7].source.immediate, 1U, "XOR should bind tighter than OR");
    failures += expect_u32(buffers.instructions[8].source.immediate, 0xFFU, "NOT should bind as unary before AND");
    failures += expect_u32(buffers.instructions[9].source.immediate, 0x34U, "LOW should extract low byte");
    failures += expect_u32(buffers.instructions[10].source.immediate, 0x12U, "HIGH should extract high byte");
    failures += expect_u32(buffers.instructions[11].source.immediate, 0x5678U, "LOWWORD should extract low word");
    failures += expect_u32(buffers.instructions[12].source.immediate, 0x1234U, "HIGHWORD should extract high word");
    failures += expect_u32(buffers.instructions[13].source.immediate, VM_MEMORY_DEFAULT_DATA_BASE + 4U, "OFFSET symbol + expression should fold to a static address");
    failures += expect_u32(buffers.instructions[14].source.address, VM_MEMORY_DEFAULT_DATA_BASE + 2U, "symbol-offset operand should accept a Milestone 29 expression");

    json = masm32_sim_wasm_run_source_json(
        "COUNT = 4 * 3\n"
        "MASK  EQU 1 SHL 7\n"
        ".data\n"
        "arr BYTE COUNT DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, COUNT\n"
        "    mov ebx, MASK\n"
        "    mov ecx, LOWWORD 12345678h\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Milestone 29 acceptance source should execute");
    failures += expect_json_contains(json, "\"phase\":75", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000000Ch\",\"unsigned\":12}", "COUNT should fold to 12");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000080h\",\"unsigned\":128}", "MASK should fold to 128");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00005678h\",\"unsigned\":22136}", "LOWWORD should fold to 5678h");

    return failures;
}

/// Verifies Milestone 30 nested DUP expansion for dword arrays and metadata operators.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase30_nested_dup_acceptance_program(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    const char *json = NULL;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        "ROWS = 3\n"
        "COLS = 4\n"
        ".data\n"
        "matrix DWORD ROWS DUP(COLS DUP(0))\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF matrix\n"
        "    mov ebx, SIZEOF matrix\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK, "Milestone 30 nested DUP acceptance source should parse");
    failures += expect_size(result.diagnostic_count, 0U, "nested DUP acceptance source should not produce diagnostics");
    failures += expect_u32(buffers.symbols[0].element_count, 12U, "nested DUP should expand ROWS * COLS elements");
    failures += expect_u32(buffers.symbols[0].size_bytes, 48U, "nested DWORD DUP should report expanded byte size");
    failures += expect_size(result.data_size, 48U, "nested DWORD DUP should emit 48 data bytes");
    failures += expect_u32(buffers.instructions[0].source.immediate, 12U, "LENGTHOF should see expanded nested DUP element count");
    failures += expect_u32(buffers.instructions[1].source.immediate, 48U, "SIZEOF should see expanded nested DUP byte size");

    json = masm32_sim_wasm_run_source_json(
        "ROWS = 3\n"
        "COLS = 4\n"
        ".data\n"
        "matrix DWORD ROWS DUP(COLS DUP(0))\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF matrix\n"
        "    mov ebx, SIZEOF matrix\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Milestone 30 acceptance source should execute");
    failures += expect_json_contains(json, "\"phase\":75", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000000Ch\",\"unsigned\":12}", "LENGTHOF matrix should be 12");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000030h\",\"unsigned\":48}", "SIZEOF matrix should be 48");

    return failures;
}

/// Verifies Milestone 30 nested DUP handles byte strings, WORD data, signed data, and uninitialized storage.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase30_nested_dup_initializer_variants(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "bytes BYTE 2 DUP(\"AB\")\n"
        "chars BYTE 2 DUP('CD')\n"
        "uninit BYTE 2 DUP(3 DUP(?))\n"
        "signed SWORD 2 DUP(3 DUP(-1))\n"
        "mix BYTE 1, 2 DUP(3 DUP(4)), 5\n"
        "words WORD 2 DUP(3 DUP(1234h))\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK, "nested DUP initializer variants should parse");
    failures += expect_size(result.diagnostic_count, 0U, "nested DUP initializer variants should not produce diagnostics");
    failures += expect_size(result.symbol_count, 6U, "nested DUP variant source should emit six symbols");

    failures += expect_u32(buffers.symbols[0].element_count, 4U, "string DUP should expand byte element count");
    failures += expect_u8(buffers.data_image[0], 'A', "string DUP first byte should be A");
    failures += expect_u8(buffers.data_image[1], 'B', "string DUP second byte should be B");
    failures += expect_u8(buffers.data_image[2], 'A', "string DUP repeated first byte should be A");
    failures += expect_u8(buffers.data_image[3], 'B', "string DUP repeated second byte should be B");

    failures += expect_u32(buffers.symbols[1].element_count, 4U, "packed character DUP should expand byte element count for BYTE");
    failures += expect_u8(buffers.data_image[4], 'C', "character DUP first byte should be C");
    failures += expect_u8(buffers.data_image[5], 'D', "character DUP second byte should be D");
    failures += expect_u8(buffers.data_image[6], 'C', "character DUP repeated first byte should be C");
    failures += expect_u8(buffers.data_image[7], 'D', "character DUP repeated second byte should be D");

    failures += expect_u32(buffers.symbols[2].element_count, 6U, "nested ? DUP should expand six bytes");
    failures += expect_u32(buffers.symbols[2].has_uninitialized_initializer ? 1U : 0U, 1U, "nested ? DUP should retain uninitialized metadata");
    failures += expect_u8(buffers.data_image[8], 0U, "nested ? DUP should zero-fill first byte");
    failures += expect_u8(buffers.data_image[13], 0U, "nested ? DUP should zero-fill last byte");

    failures += expect_u32(buffers.symbols[3].element_count, 6U, "signed nested DUP should expand six SWORD elements");
    failures += expect_u32(buffers.symbols[3].size_bytes, 12U, "signed nested DUP should occupy twelve bytes");
    failures += expect_u8(buffers.data_image[14], 0xFFU, "signed nested DUP should encode -1 low byte");
    failures += expect_u8(buffers.data_image[15], 0xFFU, "signed nested DUP should encode -1 high byte");
    failures += expect_u8(buffers.data_image[25], 0xFFU, "signed nested DUP should encode repeated -1 high byte");

    failures += expect_u32(buffers.symbols[4].element_count, 8U, "mixed declaration should include elements around nested DUP");
    failures += expect_u8(buffers.data_image[26], 1U, "mixed initializer first scalar should remain first");
    failures += expect_u8(buffers.data_image[27], 4U, "mixed initializer nested DUP first byte should follow scalar");
    failures += expect_u8(buffers.data_image[32], 4U, "mixed initializer nested DUP last repeated byte should be present");
    failures += expect_u8(buffers.data_image[33], 5U, "mixed initializer trailing scalar should remain last");

    failures += expect_u32(buffers.symbols[5].element_count, 6U, "unsigned WORD nested DUP should expand six elements");
    failures += expect_u32(buffers.symbols[5].size_bytes, 12U, "unsigned WORD nested DUP should occupy twelve bytes");
    failures += expect_u8(buffers.data_image[34], 0x34U, "unsigned WORD nested DUP should encode low byte");
    failures += expect_u8(buffers.data_image[35], 0x12U, "unsigned WORD nested DUP should encode high byte");
    failures += expect_u8(buffers.data_image[45], 0x12U, "unsigned WORD nested DUP should repeat final high byte");

    return failures;
}

/// Verifies Milestone 30 DUP initializer lists expand each comma-separated item.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase30_nested_dup_initializer_lists(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "msg BYTE 2 DUP(\"Hi\", 0)\n"
        "nested BYTE 2 DUP(1, 2 DUP(3), 4)\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK, "DUP initializer lists should parse");
    failures += expect_size(result.diagnostic_count, 0U, "DUP initializer lists should not produce diagnostics");
    failures += expect_size(result.symbol_count, 2U, "DUP initializer list source should emit two symbols");

    failures += expect_u32(buffers.symbols[0].element_count, 6U, "string plus NUL DUP should expand six BYTE elements");
    failures += expect_u32(buffers.symbols[0].size_bytes, 6U, "string plus NUL DUP should occupy six bytes");
    failures += expect_u8(buffers.data_image[0], 'H', "string plus NUL DUP first byte should be H");
    failures += expect_u8(buffers.data_image[1], 'i', "string plus NUL DUP second byte should be i");
    failures += expect_u8(buffers.data_image[2], 0U, "string plus NUL DUP third byte should be NUL");
    failures += expect_u8(buffers.data_image[3], 'H', "string plus NUL DUP repeated first byte should be H");
    failures += expect_u8(buffers.data_image[4], 'i', "string plus NUL DUP repeated second byte should be i");
    failures += expect_u8(buffers.data_image[5], 0U, "string plus NUL DUP repeated third byte should be NUL");

    failures += expect_u32(buffers.symbols[1].element_count, 8U, "mixed DUP initializer list should expand eight elements");
    failures += expect_u8(buffers.data_image[6], 1U, "mixed DUP list first scalar should be present");
    failures += expect_u8(buffers.data_image[7], 3U, "mixed DUP list nested DUP first byte should be present");
    failures += expect_u8(buffers.data_image[8], 3U, "mixed DUP list nested DUP second byte should be present");
    failures += expect_u8(buffers.data_image[9], 4U, "mixed DUP list trailing scalar should be present");
    failures += expect_u8(buffers.data_image[13], 4U, "mixed DUP list final repeated scalar should be present");

    return failures;
}

/// Verifies Milestone 30 nested DUP error paths and regressions.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase30_nested_dup_error_paths_and_regressions(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "bad BYTE (-1) DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "negative DUP count should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_DUP, "negative DUP count diagnostic should be invalid-dup");
    failures += expect_string_contains(buffers.diagnostics[0].message, "1 or greater", "negative DUP count diagnostic should state the lower bound");
    failures += expect_string_contains(buffers.diagnostics[0].message, "active declaration image capacity", "negative DUP count diagnostic should mention configured capacity");
    failures += expect_string_contains(buffers.diagnostics[0].message, "512 bytes", "negative DUP count diagnostic should show parser test capacity");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "bad BYTE 0 DUP(1)\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "zero DUP count should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_DUP, "zero DUP count diagnostic should be invalid-dup");
    failures += expect_string_contains(buffers.diagnostics[0].message, "1 or greater", "zero DUP count diagnostic should state the lower bound");
    failures += expect_string_contains(buffers.diagnostics[0].message, "active declaration image capacity", "zero DUP count diagnostic should mention configured capacity");
    failures += expect_string_contains(buffers.diagnostics[0].message, "512 bytes", "zero DUP count diagnostic should show parser test capacity");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "bad BYTE 2 DUP(3 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "malformed nested DUP should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_INVALID_DUP, "malformed nested DUP diagnostic should be invalid-dup");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "huge BYTE 20 DUP(30 DUP(0))\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_DATA_CAPACITY_EXCEEDED, "excessive nested DUP expansion should fail with data capacity status");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_DATA_CAPACITY_EXCEEDED, "excessive nested DUP diagnostic should be data-capacity-exceeded");
    failures += expect_string_contains(buffers.diagnostics[0].message, "DUP expansion requires", "excessive nested DUP diagnostic should show required byte count");
    failures += expect_string_contains(buffers.diagnostics[0].message, "only 512 bytes", "excessive nested DUP diagnostic should show available parser capacity");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "bad SBYTE 2 DUP(3 DUP(128))\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "out-of-range nested DUP initializer should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_NUMBER_OUT_OF_RANGE, "out-of-range nested DUP diagnostic should be number-out-of-range");

    failures += expect_parser_status(parse_for_test(
        "COUNT = 2 + 2\n"
        ".data\n"
        "arr BYTE COUNT DUP(1 + 1)\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK, "flat DUP with expression count and initializer should remain supported");
    failures += expect_u32(buffers.symbols[0].element_count, 4U, "flat DUP regression should keep expression count");
    failures += expect_u8(buffers.data_image[0], 2U, "flat DUP regression should keep expression initializer");
    failures += expect_u8(buffers.data_image[3], 2U, "flat DUP regression should repeat expression initializer");

    return failures;
}

/// Verifies Milestone 29 extended expression error paths.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase29_extended_expression_error_paths(void) {
    const char *division_json = NULL;
    const char *mod_json = NULL;
    const char *shift_json = NULL;
    const char *condition_json = NULL;
    const char *nonconstant_json = NULL;
    int failures = 0;

    division_json = masm32_sim_wasm_run_source_json(
        "BAD = 10 / 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, BAD\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(division_json, "\"ok\":false", "division by zero should be rejected");
    failures += expect_json_contains(division_json, "unsupported-constant-expression", "division by zero should use a structured diagnostic");
    failures += expect_json_contains(division_json, "division requires a non-zero divisor", "division diagnostic should explain the error");
    failures += expect_json_not_contains(division_json, "unknown-symbol", "invalid equate references should not cascade into unknown data-symbol diagnostics");

    mod_json = masm32_sim_wasm_run_source_json(
        "COUNT = 4 MOD 0\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(mod_json, "\"ok\":false", "MOD by zero should be rejected");
    failures += expect_json_contains(mod_json, "MOD requires a non-zero divisor", "MOD diagnostic should explain the error");

    shift_json = masm32_sim_wasm_run_source_json(
        "COUNT = 1 SHL 64\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(shift_json, "\"ok\":false", "oversized shift count should be rejected");
    failures += expect_json_contains(shift_json, "shift count must be in the range 0 through 63", "shift diagnostic should explain supported range");

    condition_json = masm32_sim_wasm_run_source_json(
        "COUNT = 1 EQ 1\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(condition_json, "\"ok\":false", "high-level condition operators should remain outside Milestone 29");
    failures += expect_json_contains(condition_json, "unsupported-constant-expression", "high-level condition operator should leave a structured trailing-token diagnostic");
    failures += expect_json_contains(condition_json, "High-level condition operators", "high-level condition diagnostic should explain the unsupported operator family");

    condition_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, eax EQ ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(condition_json, "\"ok\":false", "runtime high-level condition operators should remain outside Milestone 29");
    failures += expect_json_contains(condition_json, "unsupported-constant-expression", "runtime high-level condition operator should use a structured diagnostic");
    failures += expect_json_contains(condition_json, "High-level condition operators", "runtime high-level condition diagnostic should explain the unsupported operator family");
    failures += expect_json_not_contains(condition_json, "expected-line-end", "runtime high-level condition operator should not degrade to a generic line-end diagnostic");

    nonconstant_json = masm32_sim_wasm_run_source_json(
        "COUNT = eax + 1\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(nonconstant_json, "\"ok\":false", "register names should not be accepted as constants");
    failures += expect_json_contains(nonconstant_json, "unsupported-constant-expression", "non-constant register expression should be rejected as an unsupported constant expression");

    return failures;
}

/// Verifies signed PTR alias error paths remain structured.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_signed_ptr_width_alias_error_paths(void) {
    DataSectionTestBuffers buffers;
    VmParserResult result;
    const char *sqword_json = NULL;
    int failures = 0;

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "buf BYTE 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "mov SBYTE PTR [esi], -129\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SBYTE PTR negative overflow should fail");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_IMMEDIATE_OUT_OF_RANGE, "SBYTE PTR overflow diagnostic should match");

    failures += expect_parser_status(parse_for_test(
        ".data\n"
        "q SQWORD -1\n"
        ".code\n"
        "main PROC\n"
        "mov eax, SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n",
        &buffers,
        &result), VM_PARSER_STATUS_OK_WITH_DIAGNOSTICS, "SQWORD PTR source use should fail in MASM32 mode");
    failures += expect_parser_diagnostic_code(buffers.diagnostics[0].code, VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH, "SQWORD PTR diagnostic should match");

    sqword_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "q SQWORD -1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SQWORD PTR q\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(sqword_json, "\"ok\":false", "SQWORD PTR source-run should fail");
    failures += expect_json_contains(sqword_json, "unsupported-ptr-width", "SQWORD PTR source-run diagnostic should be unsupported-ptr-width");
    failures += expect_json_contains(sqword_json, "QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode", "SQWORD PTR diagnostic should mention Extended 32-bit Mode deferral");

    return failures;
}

/// Test entry point.
///
/// @return Zero when all Milestone 29 tests pass.
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
    failures += test_all_gpr_register_indirect_bases_parse_to_ir();
    failures += test_all_gpr_register_indirect_bases_source_run();
    failures += test_symbol_register_memory_forms_execute();
    failures += test_register_indirect_error_paths();
    failures += test_register_indirect_runtime_error_path();
    failures += test_unaligned_register_indirect_reports_warning();
    failures += test_ptr_width_override_error_paths();
    failures += test_wasm_json_reports_ptr_width_memory_changes();
    failures += test_wasm_json_reports_symbolic_memory_change();
    failures += test_signed_integer_data_declarations_layout_and_metadata();
    failures += test_signed_integer_range_errors();
    failures += test_signed_integer_metadata_and_64bit_execution_limits();
    failures += test_additional_data_sections_layout_and_const_protection();
    failures += test_signed_ptr_width_aliases_parse_to_ir();
    failures += test_signed_ptr_width_aliases_source_run_programs();
    failures += test_signed_ptr_width_alias_error_paths();
    failures += test_phase28_numeric_equates_and_constant_expressions_regression();
    failures += test_phase29_expression_and_equate_error_paths();
    failures += test_phase29_extended_constant_expressions();
    failures += test_phase29_extended_expression_error_paths();
    failures += test_phase30_nested_dup_acceptance_program();
    failures += test_phase30_nested_dup_initializer_variants();
    failures += test_phase30_nested_dup_initializer_lists();
    failures += test_phase30_nested_dup_error_paths_and_regressions();

    if (failures != 0) {
        return 1;
    }

    puts("Data section, .DATA?/.CONST, signed PTR alias, all-GPR register-indirect, TYPE, LENGTHOF, SIZEOF, character literal, extended constant-expression, and nested DUP tests through Milestone 30 passed.");
    return 0;
}
