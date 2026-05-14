/*
 * @file test_wasm_source_run.c
 * @brief Tests for the Milestone 12 Wasm-facing source execution API.
 *
 * These tests verify the narrow browser-facing C export that parses and runs a
 * minimal `.code` and `.data` programs, reports final registers and memory changes as JSON, and returns
 * structured simulator messages for parse and argument errors.
 */

#include <stdio.h>
#include <string.h>

#include "../../src/wasm/wasm_api.h"

/// Records a source-run test failure.
///
/// @param message Human-readable failure description.
/// @return Always returns one failure.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that a returned JSON string contains a required fragment.
///
/// @param json JSON string to inspect.
/// @param expected Required substring.
/// @param message Failure message when the substring is missing.
/// @return Zero on success, otherwise one failure.
static int expect_json_contains(const char *json, const char *expected, const char *message) {
    if (json == NULL || strstr(json, expected) == NULL) {
        fprintf(stderr, "FAIL: %s\nExpected fragment: %s\nJSON: %s\n", message, expected, json != NULL ? json : "(null)");
        return 1;
    }

    return 0;
}

/// Verifies that a returned JSON string does not contain an unexpected fragment.
///
/// @param json JSON string to inspect.
/// @param unexpected Forbidden substring.
/// @param message Failure message when the substring is present.
/// @return Zero on success, otherwise one failure.
static int expect_json_not_contains(const char *json, const char *unexpected, const char *message) {
    if (json != NULL && strstr(json, unexpected) != NULL) {
        fprintf(stderr, "FAIL: %s\nUnexpected fragment: %s\nJSON: %s\n", message, unexpected, json);
        return 1;
    }

    return 0;
}

/// Verifies that the guide's minimal source execution sample executes to EAX = 42.
///
/// @return Number of failures.
static int test_minimal_source_runs_to_eax_42(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 20\n"
        "    add eax, 22\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":12", "response should identify Milestone 12");
    failures += expect_json_contains(json, "\"ok\":true", "successful source run should set ok true");
    failures += expect_json_contains(json, "\"status\":\"ok\"", "successful source run should report ok status");
    failures += expect_json_contains(json, "\"instructionCount\":2", "sample should execute two instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "sample should expose EAX = 42");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "success should include simulator completion message");
    failures += expect_json_not_contains(json, "Program Console", "source-run JSON should not merge Program Console text into simulator messages");

    return failures;
}

/// Verifies that a valid zero-instruction procedure halts successfully.
///
/// @return Number of failures.
static int test_zero_instruction_program_succeeds(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "zero-instruction program should succeed");
    failures += expect_json_contains(json, "\"instructionCount\":0", "zero-instruction program should execute no instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "zero-instruction program should expose zero EAX");

    return failures;
}

/// Verifies the Milestone 12 register-indirect source-run acceptance program.
///
/// @return Number of failures.
static int test_register_indirect_source_run_succeeds(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET nums\n"
        "    mov DWORD PTR [esi + 8], 100\n"
        "    mov eax, DWORD PTR [esi + 8]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":12", "response should identify Milestone 12");
    failures += expect_json_contains(json, "\"ok\":true", "register-indirect source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "register-indirect sample should execute three instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "register-indirect sample should expose EAX = 100");
    failures += expect_json_contains(json, "\"symbol\":\"nums\",\"address\":\"00500008h\"", "register-indirect memory change should resolve to nums + 8");
    failures += expect_json_contains(json, "\"elementIndex\":2", "register-indirect memory change should include element index 2");

    return failures;
}

/// Verifies the Milestone 12 TYPE acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_type_operator_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE nums\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":12", "response should identify Milestone 12");
    failures += expect_json_contains(json, "\"ok\":true", "TYPE acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":1", "TYPE acceptance source should execute one instruction");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "TYPE nums should expose EAX = 4");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "TYPE acceptance should complete successfully");

    return failures;
}

/// Verifies TYPE returns element sizes for strings, aliases, and DUP declarations.
///
/// @return Number of failures.
static int test_type_operator_source_run_element_sizes(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "msg BYTE \"Hi\", 0\n"
        "words DW 4 DUP(0)\n"
        "quad DQ 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE msg\n"
        "    mov ebx, TYPE words\n"
        "    mov ecx, TYPE quad\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "TYPE element-size source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "TYPE BYTE string should expose EAX = 1");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "TYPE DW DUP should expose EBX = 2");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "TYPE DQ should expose ECX = 8");

    return failures;
}

/// Verifies unsupported TYPE expressions surface structured source-run diagnostics.
///
/// @return Number of failures.
static int test_type_operator_source_run_rejects_expression_tail(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE nums + 1\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "TYPE expression tail should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "TYPE expression tail should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "TYPE expression tail should be categorized as unsupported feature");
    failures += expect_json_contains(json, "unsupported-type-expression", "TYPE expression tail should expose stable diagnostic code");
    failures += expect_json_contains(json, "Only TYPE symbol is supported", "TYPE expression diagnostic should be user-readable");

    return failures;
}

/// Verifies scaled-index source-run diagnostics are explicit unsupported-feature messages.
///
/// @return Number of failures.
static int test_scaled_index_source_run_returns_unsupported_feature(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR [esi * 4]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "scaled-index form should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "scaled-index form should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "scaled-index diagnostic should be categorized as unsupported-feature");
    failures += expect_json_contains(json, "unsupported-scaled-index", "scaled-index diagnostic should expose stable code");
    failures += expect_json_contains(json, "Scaled-index memory operands are not supported yet.", "scaled-index diagnostic should be user-readable");

    return failures;
}

/// Verifies that parser diagnostics are returned as assembly-error messages.
///
/// @return Number of failures.
static int test_parse_error_returns_structured_message(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".stack\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "parse failure should set ok false");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "parse failure should report parse-error status");
    failures += expect_json_contains(json, "\"kind\":\"assembly-error\"", "parse failure should produce assembly-error message");
    failures += expect_json_contains(json, "expected-code-directive", "parse failure should expose diagnostic code");
    failures += expect_json_contains(json, "\"line\":1", "parse failure should include source line");

    return failures;
}

/// Verifies narrow register immediate overflow is rejected before execution.
///
/// @return Number of failures.
static int test_narrow_register_immediate_overflow_returns_parse_error(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 9999\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "AL overflow should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "AL overflow should report parse-error status");
    failures += expect_json_contains(json, "\"kind\":\"assembly-error\"", "AL overflow should be an assembly error");
    failures += expect_json_contains(json, "immediate-out-of-range", "AL overflow should expose immediate range diagnostic");
    failures += expect_json_not_contains(json, "\"EAX\":{\"hex\":\"0000000Fh\"", "AL overflow should not truncate to 15");

    return failures;
}


/// Verifies the Milestone 12 constant symbol-offset acceptance program.
///
/// @return Number of failures.
static int test_constant_symbol_offset_source_run_succeeds(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov nums[8], 100\n"
        "    mov eax, nums[8]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":12", "response should identify Milestone 12");
    failures += expect_json_contains(json, "\"ok\":true", "constant symbol-offset source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":2", "constant symbol-offset sample should execute two instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "constant symbol-offset sample should expose EAX = 100");
    failures += expect_json_contains(json, "\"symbol\":\"nums\"", "memory changes should include nums symbol");
    failures += expect_json_contains(json, "\"byteOffset\":8", "memory changes should include byte offset 8");
    failures += expect_json_contains(json, "\"elementIndex\":2", "memory changes should include aligned element index 2");
    failures += expect_json_contains(json, "\"dataType\":\"DWORD\"", "memory changes should include DWORD type");
    failures += expect_json_contains(json, "\"newHex\":\"00000064h\"", "memory change should include DWORD new hex");

    return failures;
}

/// Verifies unaligned constant symbol-offset accesses execute with simulator warnings.
///
/// @return Number of failures.
static int test_unaligned_constant_symbol_offset_reports_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov nums[9], 100\n"
        "    mov eax, nums[9]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "unaligned constant symbol-offset source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "unaligned read should still load EAX = 100");
    failures += expect_json_contains(json, "\"byteOffset\":9", "memory change should include unaligned byte offset 9");
    failures += expect_json_not_contains(json, "\"elementIndex\"", "unaligned memory change should not report an aligned element index");
    failures += expect_json_contains(json, "\"kind\":\"simulator-warning\"", "unaligned access should produce a simulator warning");
    failures += expect_json_contains(json, "unaligned-memory-access", "unaligned warning should use a stable code");

    return failures;
}


/// Verifies a negative symbol offset executes when the final address remains inside .data.
///
/// @return Number of failures.
static int test_negative_symbol_offset_inside_data_image_succeeds(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "head DWORD 0\n"
        "tail DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov [tail - 4], 123\n"
        "    mov eax, head\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "negative symbol offset inside .data should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000007Bh\",\"unsigned\":123}", "negative symbol offset should update the previous DWORD");
    failures += expect_json_contains(json, "\"symbol\":\"head\"", "memory change should resolve to the containing data symbol");
    failures += expect_json_contains(json, "\"newHex\":\"0000007Bh\"", "memory change should include the updated value");

    return failures;
}

/// Verifies offset-zero bracketed symbol operands execute like direct symbol access.
///
/// @return Number of failures.
static int test_offset_zero_bracketed_symbol_operands_execute(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov [nums], 100\n"
        "    mov eax, [nums + 0]\n"
        "    mov nums[0], 101\n"
        "    mov ebx, nums[0]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "offset-zero bracketed symbol operands should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "offset-zero bracketed symbol sample should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "[nums + 0] should read the first DWORD");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000065h\",\"unsigned\":101}", "nums[0] should read the updated first DWORD");
    failures += expect_json_contains(json, "\"byteOffset\":0", "offset-zero memory changes should include byte offset 0");
    failures += expect_json_contains(json, "\"elementIndex\":0", "offset-zero aligned changes should include element index 0");

    return failures;
}

/// Verifies out-of-bounds constant symbol offsets return structured parse errors.
///
/// @return Number of failures.
static int test_constant_symbol_offset_out_of_bounds_returns_parse_error(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, nums[37]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "out-of-bounds symbol offset should fail");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "out-of-bounds symbol offset should be a parse error");
    failures += expect_json_contains(json, "symbol-offset-out-of-range", "out-of-bounds symbol offset should expose stable diagnostic code");
    failures += expect_json_contains(json, "\"line\":5", "out-of-bounds diagnostic should preserve source line");

    return failures;
}

/// Verifies negative immediates execute through the Wasm source-run API.
///
/// @return Number of failures.
static int test_negative_immediate_source_run_succeeds(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, -1\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "negative immediate source run should succeed");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000FFh\"", "mov al, -1 should set AL to FFh");

    return failures;
}

/// Verifies negative immediates outside the destination range are rejected through the Wasm API.
///
/// @return Number of failures.
static int test_negative_immediate_overflow_returns_parse_error(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, -129\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "negative AL overflow should fail source run");
    failures += expect_json_contains(json, "immediate-out-of-range", "negative AL overflow should expose immediate range diagnostic");

    return failures;
}

/// Verifies that NULL source is rejected as a structured source error.
///
/// @return Number of failures.
static int test_null_source_returns_invalid_argument_json(void) {
    const char *json = masm32_sim_wasm_run_source_json(NULL);
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "NULL source should set ok false");
    failures += expect_json_contains(json, "\"status\":\"invalid-argument\"", "NULL source should report invalid-argument");
    failures += expect_json_contains(json, "\"code\":\"invalid-source\"", "NULL source should include invalid-source code");

    return failures;
}


/// Verifies that empty source is rejected with structured parser diagnostics.
///
/// @return Number of failures.
static int test_empty_source_returns_parse_error_json(void) {
    const char *json = masm32_sim_wasm_run_source_json("");
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "empty source should set ok false");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "empty source should report parse-error");
    failures += expect_json_contains(json, "\"kind\":\"assembly-error\"", "empty source should include assembly-error message");

    return failures;
}

/// Verifies that the static JSON buffer is overwritten by subsequent calls.
///
/// @return Number of failures.
static int test_subsequent_calls_return_latest_result(void) {
    char first_copy[256];
    const char *first = masm32_sim_wasm_run_source_json(
        ".code\nmain PROC\n    mov eax, 1\nmain ENDP\nEND main\n"
    );
    const char *second = NULL;
    int failures = 0;

    if (first == NULL) {
        return record_failure("first source-run result should not be NULL");
    }
    (void)snprintf(first_copy, sizeof(first_copy), "%s", first);

    second = masm32_sim_wasm_run_source_json(
        ".code\nmain PROC\n    mov eax, 2\nmain ENDP\nEND main\n"
    );

    failures += expect_json_contains(first_copy, "\"unsigned\":1", "first copied result should retain EAX = 1");
    failures += expect_json_contains(second, "\"unsigned\":2", "second result should expose latest EAX = 2");

    return failures;
}

/// Test entry point.
///
/// @return Zero when all source-run API tests pass.
int main(void) {
    int failures = 0;

    failures += test_minimal_source_runs_to_eax_42();
    failures += test_zero_instruction_program_succeeds();
    failures += test_parse_error_returns_structured_message();
    failures += test_narrow_register_immediate_overflow_returns_parse_error();
    failures += test_negative_immediate_source_run_succeeds();
    failures += test_negative_immediate_overflow_returns_parse_error();
    failures += test_register_indirect_source_run_succeeds();
    failures += test_type_operator_source_run_acceptance_program();
    failures += test_type_operator_source_run_element_sizes();
    failures += test_type_operator_source_run_rejects_expression_tail();
    failures += test_scaled_index_source_run_returns_unsupported_feature();
    failures += test_constant_symbol_offset_source_run_succeeds();
    failures += test_unaligned_constant_symbol_offset_reports_warning();
    failures += test_negative_symbol_offset_inside_data_image_succeeds();
    failures += test_offset_zero_bracketed_symbol_operands_execute();
    failures += test_constant_symbol_offset_out_of_bounds_returns_parse_error();
    failures += test_null_source_returns_invalid_argument_json();
    failures += test_empty_source_returns_parse_error_json();
    failures += test_subsequent_calls_return_latest_result();

    if (failures != 0) {
        return 1;
    }

    puts("Milestone 12 source execution tests passed.");
    return 0;
}
