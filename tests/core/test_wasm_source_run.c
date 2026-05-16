/*
 * @file test_wasm_source_run.c
 * @brief Tests for the Milestone 29 Wasm-facing source execution API.
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

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
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

/// Verifies the Milestone 29 register-indirect source-run acceptance program.
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

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "register-indirect source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "register-indirect sample should execute three instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "register-indirect sample should expose EAX = 100");
    failures += expect_json_contains(json, "\"symbol\":\"nums\",\"address\":\"00500008h\"", "register-indirect memory change should resolve to nums + 8");
    failures += expect_json_contains(json, "\"elementIndex\":2", "register-indirect memory change should include element index 2");

    return failures;
}

/// Verifies the Phase 26 acceptance program using EAX as a memory base.
///
/// @return Number of failures.
static int test_phase24_eax_base_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET nums\n"
        "    mov DWORD PTR [eax], 100\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "Phase 26 EAX-base response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 EAX-base acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 26 EAX-base acceptance source should execute three instructions");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "Phase 26 EAX-base acceptance source should set EBX = 100");
    failures += expect_json_contains(json, "\"symbol\":\"nums\",\"address\":\"00500000h\"", "Phase 26 EAX-base memory change should resolve to nums base");

    return failures;
}

/// Verifies all 32-bit general-purpose registers are accepted as memory bases through source-run.
///
/// @return Number of failures.
static int test_all_gpr_register_indirect_source_run_succeeds(void) {
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
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "all-GPR response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "all-GPR register-indirect source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000050h\",\"unsigned\":80}", "all-GPR register-indirect source should load 80 through ESP");
    failures += expect_json_contains(json, "\"address\":\"0050001Ch\"", "all-GPR register-indirect source should write through ESP + 28");

    return failures;
}

/// Verifies the Milestone 29 TYPE acceptance program through the source-run API.
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

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
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

/// Verifies the Milestone 29 LENGTHOF acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_lengthof_operator_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        "buf  BYTE \"Hello\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF nums\n"
        "    mov ebx, LENGTHOF buf\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "LENGTHOF acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":2", "LENGTHOF acceptance source should execute two instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000000Ah\",\"unsigned\":10}", "LENGTHOF nums should expose EAX = 10");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000006h\",\"unsigned\":6}", "LENGTHOF buf should expose EBX = 6");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "LENGTHOF acceptance should complete successfully");

    return failures;
}

/// Verifies LENGTHOF returns element counts for strings, aliases, and DUP declarations.
///
/// @return Number of failures.
static int test_lengthof_operator_source_run_element_counts(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "msg BYTE \"Hi\", 0\n"
        "words DW 4 DUP(0)\n"
        "quad DQ 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF msg\n"
        "    mov ebx, LENGTHOF words\n"
        "    mov ecx, LENGTHOF quad\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "LENGTHOF element-count source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "LENGTHOF BYTE string should expose EAX = 3");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "LENGTHOF DW DUP should expose EBX = 4");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "LENGTHOF DQ scalar should expose ECX = 1");

    return failures;
}

/// Verifies the Milestone 29 SIZEOF and character-literal acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_sizeof_operator_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
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
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "SIZEOF acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "SIZEOF acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000028h\",\"unsigned\":40}", "SIZEOF nums should expose EAX = 40");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000041h\",\"unsigned\":65}", "mov bl, ch should expose BL byte value through EBX = 65");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00004241h\",\"unsigned\":16961}", "mov cx, pair should expose packed WORD value");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"44434241h\",\"unsigned\":1145258561}", "mov edx, 'ABCD' should expose packed DWORD value");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "SIZEOF acceptance should complete successfully");

    return failures;
}

/// Verifies SIZEOF returns byte sizes for strings, aliases, and DUP declarations.
///
/// @return Number of failures.
static int test_sizeof_operator_source_run_byte_sizes(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "msg BYTE \"Hi\", 0\n"
        "words DW 4 DUP(0)\n"
        "quad DQ 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF msg\n"
        "    mov ebx, SIZEOF words\n"
        "    mov ecx, SIZEOF quad\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "SIZEOF byte-size source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "SIZEOF BYTE string should expose EAX = 3");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "SIZEOF DW DUP should expose EBX = 8");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "SIZEOF DQ should expose ECX = 8");

    return failures;
}

/// Verifies unsupported SIZEOF expressions surface structured source-run diagnostics.
///
/// @return Number of failures.
static int test_sizeof_operator_source_run_rejects_expression_tail(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF nums + 1\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "SIZEOF expression tail should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "SIZEOF expression tail should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "SIZEOF expression tail should be categorized as unsupported feature");
    failures += expect_json_contains(json, "unsupported-sizeof-expression", "SIZEOF expression tail should expose stable diagnostic code");
    failures += expect_json_contains(json, "Only SIZEOF symbol is supported", "SIZEOF expression diagnostic should be user-readable");

    return failures;
}

/// Verifies packed character literals that overflow the destination width surface diagnostics.
///
/// @return Number of failures.
static int test_character_literal_source_run_rejects_width_overflow(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 'AB'\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "too-wide character literal should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "too-wide character literal should be a parse error");
    failures += expect_json_contains(json, "invalid-character-literal", "too-wide character literal should expose stable diagnostic code");
    failures += expect_json_contains(json, "does not fit the destination width", "too-wide character literal should explain the width failure");

    return failures;
}

/// Verifies packed character literals execute when the destination width can hold all decoded bytes.
///
/// @return Number of failures.
static int test_character_literal_source_run_accepts_narrower_packed_immediates(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "slot DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 'ABC'\n"
        "    mov DWORD PTR slot, 'ABC'\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "three-byte packed character literal should execute in 32-bit contexts");
    failures += expect_json_contains(json, "\"instructionCount\":2", "packed character literal program should execute two instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00434241h\",\"unsigned\":4407873}", "mov eax, 'ABC' should expose packed little-endian value");
    failures += expect_json_contains(json, "\"symbol\":\"slot\"", "memory immediate character literal should update slot");
    failures += expect_json_contains(json, "\"newHex\":\"00434241h\"", "DWORD memory immediate should store the packed character literal");

    return failures;
}

/// Verifies unsupported LENGTHOF expressions surface structured source-run diagnostics.
///
/// @return Number of failures.
static int test_lengthof_operator_source_run_rejects_expression_tail(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF nums + 1\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "LENGTHOF expression tail should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "LENGTHOF expression tail should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "LENGTHOF expression tail should be categorized as unsupported feature");
    failures += expect_json_contains(json, "unsupported-lengthof-expression", "LENGTHOF expression tail should expose stable diagnostic code");
    failures += expect_json_contains(json, "Only LENGTHOF symbol is supported", "LENGTHOF expression diagnostic should be user-readable");

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
    const char *eax_scaled_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR [eax * 4]\n"
        "main ENDP\n"
        "END main\n"
    );
    const char *array_scaled_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR nums[esi * 4]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(eax_scaled_json, "\"ok\":false", "[eax * 4] scaled-index form should fail source run");
    failures += expect_json_contains(eax_scaled_json, "\"status\":\"parse-error\"", "[eax * 4] scaled-index form should be a parse error");
    failures += expect_json_contains(eax_scaled_json, "\"kind\":\"unsupported-feature\"", "[eax * 4] scaled-index diagnostic should be categorized as unsupported-feature");
    failures += expect_json_contains(eax_scaled_json, "unsupported-scaled-index", "[eax * 4] scaled-index diagnostic should expose stable code");
    failures += expect_json_contains(eax_scaled_json, "Scaled-index memory operands are not supported yet.", "[eax * 4] scaled-index diagnostic should be user-readable");

    failures += expect_json_contains(array_scaled_json, "\"ok\":false", "array[esi * 4] scaled-index form should fail source run");
    failures += expect_json_contains(array_scaled_json, "unsupported-scaled-index", "array[esi * 4] scaled-index diagnostic should expose stable code");

    return failures;
}

/// Verifies that parser diagnostics are returned as assembly-error messages.
///
/// @return Number of failures.
static int test_parse_error_returns_structured_message(void) {
    const char *json = masm32_sim_wasm_run_source_json(
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


/// Verifies the Milestone 29 constant symbol-offset acceptance program.
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

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
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


/// Verifies lexer diagnostics surface through source-run without generic lexer-failed replacement.
///
/// @return Number of failures.
static int test_source_run_invalid_hex_reports_specific_lexer_diagnostic(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0xZZ\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid hex source should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "invalid hex source should report parse-error");
    failures += expect_json_contains(json, "\"kind\":\"assembly-error\"", "invalid hex should be an assembly error");
    failures += expect_json_contains(json, "\"code\":\"invalid-hex-literal\"", "invalid hex should expose the lexer diagnostic code");
    failures += expect_json_contains(json, "hex literal", "invalid hex message should be specific");
    failures += expect_json_contains(json, "\"line\":3", "invalid hex should preserve source line");
    failures += expect_json_contains(json, "\"column\":14", "invalid hex should preserve source column");
    failures += expect_json_contains(json, "\"byteOffset\":29", "invalid hex should preserve byte offset");
    failures += expect_json_contains(json, "\"spanLength\":2", "invalid hex should preserve source span length");
    failures += expect_json_not_contains(json, "lexer-failed", "invalid hex should not collapse into lexer-failed");
    failures += expect_json_not_contains(json, "\"ok\":true", "invalid hex source should not execute");

    return failures;
}

/// Verifies unterminated strings surface the original lexer diagnostic through source-run.
///
/// @return Number of failures.
static int test_source_run_unterminated_string_reports_specific_lexer_diagnostic(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "msg BYTE \"Hello\n"
        ".code\n"
        "main PROC\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "unterminated string source should fail source run");
    failures += expect_json_contains(json, "\"code\":\"unterminated-string\"", "unterminated string should expose the lexer diagnostic code");
    failures += expect_json_contains(json, "unterminated string literal", "unterminated string message should be specific");
    failures += expect_json_contains(json, "\"line\":2", "unterminated string should preserve source line");
    failures += expect_json_contains(json, "\"column\":10", "unterminated string should preserve source column");
    failures += expect_json_contains(json, "\"byteOffset\":15", "unterminated string should preserve byte offset");
    failures += expect_json_contains(json, "\"spanLength\":6", "unterminated string should preserve source span length");
    failures += expect_json_not_contains(json, "lexer-failed", "unterminated string should not collapse into lexer-failed");

    return failures;
}

/// Verifies remaining Phase 17 lexer error cases surface as specific source-run diagnostics.
///
/// @return Number of failures.
static int test_source_run_other_lexer_diagnostics_are_specific(void) {
    char invalid_character_json[1024];
    char unterminated_character_json[1024];
    char overflow_json[1024];
    char malformed_numeric_json[1024];
    int failures = 0;

    (void)snprintf(invalid_character_json, sizeof(invalid_character_json), "%s", masm32_sim_wasm_run_source_json("#"));
    (void)snprintf(unterminated_character_json, sizeof(unterminated_character_json), "%s", masm32_sim_wasm_run_source_json("'A\n"));
    (void)snprintf(overflow_json, sizeof(overflow_json), "%s", masm32_sim_wasm_run_source_json("184467440737095516160"));
    (void)snprintf(malformed_numeric_json, sizeof(malformed_numeric_json), "%s", masm32_sim_wasm_run_source_json("123abc"));

    failures += expect_json_contains(invalid_character_json, "\"code\":\"unexpected-character\"", "invalid character should expose unexpected-character");
    failures += expect_json_contains(invalid_character_json, "\"byteOffset\":0", "invalid character should expose byte offset zero");
    failures += expect_json_not_contains(invalid_character_json, "lexer-failed", "invalid character should not collapse into lexer-failed");

    failures += expect_json_contains(unterminated_character_json, "\"code\":\"unterminated-character\"", "unterminated character should expose lexer diagnostic code");
    failures += expect_json_contains(unterminated_character_json, "unterminated character literal", "unterminated character message should be specific");
    failures += expect_json_not_contains(unterminated_character_json, "lexer-failed", "unterminated character should not collapse into lexer-failed");

    failures += expect_json_contains(overflow_json, "\"code\":\"number-overflow\"", "numeric overflow should expose number-overflow");
    failures += expect_json_contains(overflow_json, "does not fit in uint64_t", "numeric overflow message should be specific");
    failures += expect_json_not_contains(overflow_json, "lexer-failed", "numeric overflow should not collapse into lexer-failed");

    failures += expect_json_contains(malformed_numeric_json, "\"code\":\"unexpected-character\"", "malformed numeric text should expose lexer diagnostic code");
    failures += expect_json_contains(malformed_numeric_json, "invalid decimal literal suffix", "malformed numeric message should be specific");
    failures += expect_json_contains(malformed_numeric_json, "\"spanLength\":6", "malformed numeric diagnostic should expose source span length");
    failures += expect_json_not_contains(malformed_numeric_json, "lexer-failed", "malformed numeric should not collapse into lexer-failed");

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



/// Verifies the Phase 22 TEST acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_phase22_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    stc\n"
        "    test eax, eax\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "Phase 22 response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 22 TEST acceptance program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 22 TEST acceptance program should execute three instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "TEST acceptance should leave EAX zero");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000040h\",\"unsigned\":64}", "TEST acceptance should set only ZF among modeled flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 22 acceptance program should complete successfully");

    return failures;
}

/// Verifies TEST memory/immediate forms with explicit or inferable widths through source-run.
///
/// @return Number of failures.
static int test_phase22_memory_immediate_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 0F0F0F0Fh\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET nums\n"
        "    test value, 0FFh\n"
        "    test nums[0], 0FFh\n"
        "    test DWORD PTR [esi], 1\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 22 TEST memory/immediate program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 22 TEST memory/immediate program should execute four instructions");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000040h\",\"unsigned\":64}", "final TEST against zero memory should set ZF");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "TEST should not report memory changes because it stores no result");

    return failures;
}

/// Verifies source-run diagnostics for ambiguous TEST memory/immediate forms.
///
/// @return Number of failures.
static int test_phase22_source_run_error_paths(void) {
    const char *ambiguous_json = NULL;
    const char *ambiguous_displacement_json = NULL;
    const char *ambiguous_eax_json = NULL;
    const char *eax_invalid_memory_json = NULL;
    const char *invalid_memory_json = NULL;
    char ambiguous_copy[1024];
    char ambiguous_displacement_copy[1024];
    char ambiguous_eax_copy[1024];
    char eax_invalid_memory_copy[1024];
    char invalid_memory_copy[1024];
    int failures = 0;

    ambiguous_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    test [esi], 1\n"
        "main ENDP\n"
        "END main\n"
    );
    if (ambiguous_json == NULL) {
        return record_failure("TEST ambiguous memory result should not be NULL");
    }
    (void)snprintf(ambiguous_copy, sizeof(ambiguous_copy), "%s", ambiguous_json);

    ambiguous_displacement_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    test [esi + 4], 1\n"
        "main ENDP\n"
        "END main\n"
    );
    if (ambiguous_displacement_json == NULL) {
        return record_failure("TEST ambiguous displacement result should not be NULL");
    }
    (void)snprintf(ambiguous_displacement_copy, sizeof(ambiguous_displacement_copy), "%s", ambiguous_displacement_json);

    ambiguous_eax_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    test [eax], 1\n"
        "main ENDP\n"
        "END main\n"
    );
    if (ambiguous_eax_json == NULL) {
        return record_failure("TEST EAX ambiguous memory result should not be NULL");
    }
    (void)snprintf(ambiguous_eax_copy, sizeof(ambiguous_eax_copy), "%s", ambiguous_eax_json);

    eax_invalid_memory_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    test [eax], eax\n"
        "main ENDP\n"
        "END main\n"
    );
    if (eax_invalid_memory_json == NULL) {
        return record_failure("TEST EAX invalid memory result should not be NULL");
    }
    (void)snprintf(eax_invalid_memory_copy, sizeof(eax_invalid_memory_copy), "%s", eax_invalid_memory_json);

    invalid_memory_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov esi, 0\n"
        "    test DWORD PTR [esi], esi\n"
        "main ENDP\n"
        "END main\n"
    );
    if (invalid_memory_json == NULL) {
        return record_failure("TEST invalid register-indirect memory result should not be NULL");
    }
    (void)snprintf(invalid_memory_copy, sizeof(invalid_memory_copy), "%s", invalid_memory_json);

    failures += expect_json_contains(ambiguous_copy, "ambiguous-memory-width", "TEST [esi], imm should expose stable ambiguous-memory-width diagnostic");
    failures += expect_json_contains(ambiguous_copy, "Memory operand width is ambiguous", "TEST ambiguous diagnostic should describe ambiguous width");
    failures += expect_json_contains(ambiguous_copy, "\"column\":10", "TEST ambiguous diagnostic should point to the ambiguous memory operand");
    failures += expect_json_contains(ambiguous_copy, "BYTE PTR", "TEST ambiguous diagnostic should suggest explicit PTR widths");
    failures += expect_json_not_contains(ambiguous_copy, "unsupported by the current milestone", "TEST ambiguous diagnostic should not frame the MASM rule as a milestone limitation");

    failures += expect_json_contains(ambiguous_displacement_copy, "ambiguous-memory-width", "TEST [esi + 4], imm should expose stable ambiguous-memory-width diagnostic");
    failures += expect_json_contains(ambiguous_eax_copy, "ambiguous-memory-width", "TEST [eax], imm should expose stable ambiguous-memory-width diagnostic after EAX base support");
    failures += expect_json_not_contains(ambiguous_eax_copy, "unsupported-register-indirect-base", "TEST [eax], imm should not reject EAX as an unsupported base");

    failures += expect_json_contains(eax_invalid_memory_copy, "\"kind\":\"runtime-error\"", "TEST [eax], eax at address zero should be a runtime error");
    failures += expect_json_contains(eax_invalid_memory_copy, "\"code\":\"invalid-address\"", "TEST [eax], eax should expose memory status code");
    failures += expect_json_contains(eax_invalid_memory_copy, "Invalid memory read at 00000000h for 4 bytes", "TEST [eax], eax should describe address-zero invalid read");
    failures += expect_json_not_contains(eax_invalid_memory_copy, "unsupported-register-indirect-base", "TEST [eax], eax should not produce unsupported-register-indirect-base after Phase 26");

    failures += expect_json_contains(invalid_memory_copy, "\"kind\":\"runtime-error\"", "TEST invalid memory access should be a runtime error");
    failures += expect_json_contains(invalid_memory_copy, "\"code\":\"invalid-address\"", "TEST invalid memory access should expose memory status code");
    failures += expect_json_contains(invalid_memory_copy, "Invalid memory read at 00000000h for 4 bytes", "TEST invalid memory access should describe the address and width");
    failures += expect_json_contains(invalid_memory_copy, "configured memory regions", "TEST invalid memory access should explain why address zero is invalid");
    failures += expect_json_contains(invalid_memory_copy, "\"line\":4", "TEST invalid memory access should preserve source line");
    failures += expect_json_not_contains(invalid_memory_copy, "Execution failed while running the parsed program", "TEST invalid memory access should not use vague execution failure wording");

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

/// Verifies unsupported textbook MASM constructs surface as unsupported features.
///
/// @return Number of failures.
static int test_textbook_unsupported_features_return_unsupported_feature_messages(void) {
    char struct_json[2048];
    char invoke_json[2048];
    char dataq_json[2048];
    int failures = 0;

    (void)snprintf(struct_json, sizeof(struct_json), "%s", masm32_sim_wasm_run_source_json(
        "Point STRUCT\n"
        "x DWORD ?\n"
        "Point ENDS\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    ));
    (void)snprintf(invoke_json, sizeof(invoke_json), "%s", masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "INVOKE ExitProcess, 0\n"
        "main ENDP\n"
        "END main\n"
    ));
    (void)snprintf(dataq_json, sizeof(dataq_json), "%s", masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "buffer BYTE 64 DUP(?)\n"
        "\n"
        ".CONST\n"
        "limit DWORD 10\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(struct_json, "\"ok\":false", "STRUCT source should fail source run");
    failures += expect_json_contains(struct_json, "\"kind\":\"unsupported-feature\"", "STRUCT should be classified as unsupported feature");
    failures += expect_json_contains(struct_json, "\"code\":\"unsupported-feature\"", "STRUCT should use stable unsupported-feature code");
    failures += expect_json_contains(struct_json, "STRUCT declarations are not supported yet", "STRUCT message should be specific");

    failures += expect_json_contains(invoke_json, "\"kind\":\"unsupported-feature\"", "INVOKE should be classified as unsupported feature");
    failures += expect_json_contains(invoke_json, "INVOKE is not supported yet", "INVOKE message should be specific");

    failures += expect_json_contains(dataq_json, "\"ok\":true", ".DATA? and .CONST should now execute successfully");
    failures += expect_json_contains(dataq_json, "\"instructionCount\":0", ".DATA? and .CONST no-op body should execute zero instructions");
    failures += expect_json_contains(dataq_json, "execution-complete", ".DATA? and .CONST should complete execution");
    failures += expect_json_not_contains(dataq_json, "unsupported-feature", ".DATA? and .CONST should no longer be unsupported features");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".data\n"
        "Greeting TEXTEQU <Hello>\n"
        ".code\n"
        "main PROC\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", "TEXTEQU angle-bracket sample should be classified as unsupported feature");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "mov eax, 1\n"
        ".IF eax == 1\n"
        "mov ebx, 2\n"
        "main ENDP\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", ".IF comparison sample should be classified as unsupported feature");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "mov ecx, 3\n"
        ".WHILE ecx > 0\n"
        "sub ecx, 1\n"
        "main ENDP\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", ".WHILE comparison sample should be classified as unsupported feature");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        ".REPEAT\n"
        "mov eax, 1\n"
        ".UNTIL eax == 1\n"
        "main ENDP\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", ".REPEAT/.UNTIL sample should be classified as unsupported feature");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".data\n"
        "r REAL4 1.0\n"
        ".code\n"
        "main PROC\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", "REAL4 float sample should be classified as unsupported feature");

    return failures;
}

/// Verifies Milestone 29 source-run JSON reports multiple recovered unsupported features.
///
/// @return Number of failures.
static int test_multi_diagnostic_unsupported_feature_source_run_reports_all(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 0\n"
        "\n"
        "MyStruct STRUCT\n"
        "    a DWORD ?\n"
        "MyStruct ENDS\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    INVOKE SomeProc\n"
        "    .IF eax == 0\n"
        "        mov ebx, 1\n"
        "    .ENDIF\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":false", "unsupported recovery source should not execute");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "unsupported recovery source should be a parse error");
    failures += expect_json_contains(json, "STRUCT declarations", "source-run should include STRUCT diagnostic");
    failures += expect_json_contains(json, "INVOKE is not supported", "source-run should include INVOKE diagnostic");
    failures += expect_json_contains(json, "MASM .IF high-level flow", "source-run should include .IF diagnostic");
    failures += expect_json_contains(json, "\"line\":4", "STRUCT diagnostic line should be surfaced");
    failures += expect_json_contains(json, "\"line\":10", "INVOKE diagnostic line should be surfaced");
    failures += expect_json_contains(json, "\"line\":11", ".IF diagnostic line should be surfaced");
    failures += expect_json_contains(json, "\"column\":10", "STRUCT diagnostic column should be surfaced");
    failures += expect_json_contains(json, "\"column\":5", "recovered diagnostic columns should be surfaced");
    failures += expect_json_contains(json, "\"byteOffset\":26", "STRUCT diagnostic byte offset should be surfaced");
    failures += expect_json_contains(json, "\"byteOffset\":82", "INVOKE diagnostic byte offset should be surfaced");
    failures += expect_json_contains(json, "\"byteOffset\":102", ".IF diagnostic byte offset should be surfaced");
    failures += expect_json_contains(json, "\"spanLength\":6", "six-byte recovered diagnostic spans should be surfaced");
    failures += expect_json_contains(json, "\"spanLength\":3", ".IF recovered diagnostic span should be surfaced");
    failures += expect_json_not_contains(json, "execution-complete", "source-run should not execute when diagnostics exist");

    return failures;
}

/// Verifies the Milestone 29 signed declaration acceptance program.
///
/// @return Number of failures.
static int test_signed_integer_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "sb SBYTE -1\n"
        "sw SWORD -2\n"
        "sd SDWORD -3\n"
        "sq SQWORD -4\n"
        "arr SWORD 3 DUP(-1)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE sq\n"
        "    mov ebx, LENGTHOF arr\n"
        "    mov ecx, SIZEOF arr\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "signed acceptance program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "TYPE SQWORD should produce EAX = 8");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "LENGTHOF SWORD DUP should produce EBX = 3");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000006h\",\"unsigned\":6}", "SIZEOF SWORD DUP should produce ECX = 6");

    return failures;
}

/// Verifies unary-plus numeric literals through source-run execution.
///
/// @return Number of failures.
static int test_unary_plus_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "sb SBYTE +127\n"
        "sw SWORD +32767\n"
        "d  DWORD +42\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, +42\n"
        "    mov ebx, +0x2A\n"
        "    mov ecx, +2Ah\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "unary-plus source program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "mov eax, +42 should produce 42");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "mov ebx, +0x2A should produce 42");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "mov ecx, +2Ah should produce 42");

    return failures;
}

/// Verifies signed range failures and unary-plus overflow surface as diagnostics.
///
/// @return Number of failures.
static int test_signed_integer_source_run_error_paths(void) {
    const char *signed_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "sb SBYTE 128\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    );
    char signed_copy[1024];
    const char *plus_json = NULL;

    if (signed_json == NULL) {
        return record_failure("signed overflow result should not be NULL");
    }
    (void)snprintf(signed_copy, sizeof(signed_copy), "%s", signed_json);

    plus_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, +256\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(signed_copy, "\"ok\":false", "signed overflow should fail source-run");
    failures += expect_json_contains(signed_copy, "number-out-of-range", "signed overflow should report data range diagnostic");
    failures += expect_json_contains(plus_json, "\"ok\":false", "unary-plus register overflow should fail source-run");
    failures += expect_json_contains(plus_json, "immediate-out-of-range", "unary-plus register overflow should report immediate range diagnostic");

    return failures;
}


/// Verifies Milestone 29 signed PTR alias reads through the source-run API.
///
/// @return Number of failures.
static int test_signed_ptr_alias_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "b SBYTE -1\n"
        "w SWORD -2\n"
        "d SDWORD -3\n"
        ".code\n"
        "main PROC\n"
        "    mov al, SBYTE PTR b\n"
        "    mov bx, SWORD PTR w\n"
        "    mov ecx, SDWORD PTR d\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "signed PTR alias response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "signed PTR alias acceptance program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "SBYTE PTR b should load FFh into AL without sign extension");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000FFFEh\",\"unsigned\":65534}", "SWORD PTR w should load FFFEh into BX without sign extension");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"FFFFFFFDh\",\"unsigned\":4294967293}", "SDWORD PTR d should load raw FFFFFFFDh into ECX");

    return failures;
}

/// Verifies Milestone 29 signed PTR alias writes through register-indirect memory.
///
/// @return Number of failures.
static int test_signed_ptr_alias_source_run_write_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf BYTE 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET buf\n"
        "    mov SBYTE PTR [esi], -1\n"
        "    mov al, BYTE PTR [esi]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "signed PTR alias write program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "SBYTE PTR write should store FFh for later BYTE PTR read");
    failures += expect_json_contains(json, "\"memoryChanges\":[{\"symbol\":\"buf\"", "signed PTR write should report symbol-aware memory change");
    failures += expect_json_contains(json, "\"widthBits\":8", "signed PTR write should report an 8-bit memory width");
    failures += expect_json_contains(json, "\"newHex\":\"FFh\"", "signed PTR write should report FFh new value");

    return failures;
}

/// Verifies Milestone 29 signed PTR alias error diagnostics through source-run.
///
/// @return Number of failures.
static int test_signed_ptr_alias_source_run_error_paths(void) {
    const char *overflow_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf BYTE 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET buf\n"
        "    mov SBYTE PTR [esi], -129\n"
        "main ENDP\n"
        "END main\n"
    );
    char overflow_copy[1024];
    const char *sqword_json = NULL;
    int failures = 0;

    if (overflow_json == NULL) {
        return record_failure("signed PTR overflow source-run result should not be NULL");
    }
    (void)snprintf(overflow_copy, sizeof(overflow_copy), "%s", overflow_json);

    sqword_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "q SQWORD -1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SQWORD PTR q\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(overflow_copy, "\"ok\":false", "SBYTE PTR immediate overflow should fail");
    failures += expect_json_contains(overflow_copy, "immediate-out-of-range", "SBYTE PTR overflow should report immediate range diagnostic");
    failures += expect_json_contains(sqword_json, "\"ok\":false", "SQWORD PTR executable source should fail");
    failures += expect_json_contains(sqword_json, "unsupported-ptr-width", "SQWORD PTR source-run diagnostic should be unsupported-ptr-width");
    failures += expect_json_contains(sqword_json, "QWORD and SQWORD PTR execution is deferred until Extended 32-bit Mode", "SQWORD PTR diagnostic should explain 64-bit deferral");

    return failures;
}

/// Verifies the Milestone 29 sign/zero-extension acceptance program.
///
/// @return Number of failures.
static int test_extension_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x SBYTE -1\n"
        "y BYTE  0FFh\n"
        ".code\n"
        "main PROC\n"
        "    movsx eax, x\n"
        "    movzx ebx, y\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "extension acceptance program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "movsx eax, SBYTE -1 should sign-extend to FFFFFFFFh");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "movzx ebx, BYTE 0FFh should zero-extend to FFh");

    return failures;
}

/// Verifies accumulator conversion instructions through the source-run path.
///
/// @return Number of failures.
static int test_accumulator_extension_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    cbw\n"
        "    cwde\n"
        "    mov ax, 8000h\n"
        "    cwd\n"
        "    mov eax, 80000000h\n"
        "    cdq\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "accumulator extension program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":7", "accumulator extension program should execute seven instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"80000000h\",\"unsigned\":2147483648}", "cdq should leave EAX unchanged");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "cdq should sign-extend EAX into EDX");

    return failures;
}


/// Verifies MOVSX and MOVZX source-run behavior for PTR register-indirect memory sources.
///
/// @return Number of failures.
static int test_extension_register_indirect_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf BYTE 80h, 0FFh, 34h, 12h\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET buf\n"
        "    movsx eax, BYTE PTR [esi]\n"
        "    movzx ebx, WORD PTR [esi + 2]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "register-indirect extension source program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFF80h\",\"unsigned\":4294967168}", "movsx eax, BYTE PTR [esi] should sign-extend 80h");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00001234h\",\"unsigned\":4660}", "movzx ebx, WORD PTR [esi + 2] should zero-extend 1234h");

    return failures;
}

/// Verifies ordinary MOV does not silently widen signed byte memory into EAX.
///
/// The regression risk is an accidental fallback that treats signed byte memory
/// as a source for an implicit 32-bit sign-extending MOV. Ordinary MOV must
/// keep the existing width rules and require explicit MOVSX for widening.
///
/// @return Number of failures.
static int test_plain_mov_from_signed_memory_rejects_implicit_widening(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "sb SBYTE -1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, sb\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "plain mov eax, SBYTE memory should not execute");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "plain mov eax, SBYTE memory should be rejected before execution");
    failures += expect_json_contains(json, "operand-width-mismatch", "plain mov eax, SBYTE memory should report a structured width diagnostic");
    failures += expect_json_contains(json, "Source operand width does not match the destination operand width", "plain mov eax, SBYTE memory should keep explicit width validation");
    failures += expect_json_not_contains(json, "execution-complete", "plain mov eax, SBYTE memory should not execute successfully");

    return failures;
}


/// Verifies source-run edge cases for 16-bit MOVZX destinations and positive CDQ.
///
/// @return Number of failures.
static int test_extension_source_run_edge_cases(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov bl, 0FFh\n"
        "    movzx cx, bl\n"
        "    mov edx, 0FFFFFFFFh\n"
        "    mov eax, 1\n"
        "    cdq\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "extension edge-case program should execute");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "movzx cx, bl should update the low word of ECX");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "positive cdq should leave EAX unchanged");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "positive cdq should clear EDX");

    return failures;
}

/// Verifies source-run diagnostics for invalid extension instruction widths and operands.
///
/// @return Number of failures.
static int test_extension_source_run_error_paths(void) {
    const char *same_width_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    movsx ax, bx\n"
        "main ENDP\n"
        "END main\n"
    );
    char same_width_copy[1024];
    char cbw_operand_copy[1024];
    const char *cbw_operand_json = NULL;
    const char *ambiguous_memory_json = NULL;
    int failures = 0;

    if (same_width_json == NULL) {
        return record_failure("same-width MOVSX result should not be NULL");
    }
    (void)snprintf(same_width_copy, sizeof(same_width_copy), "%s", same_width_json);

    cbw_operand_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    cbw eax\n"
        "main ENDP\n"
        "END main\n"
    );
    if (cbw_operand_json == NULL) {
        return record_failure("CBW operand result should not be NULL");
    }
    (void)snprintf(cbw_operand_copy, sizeof(cbw_operand_copy), "%s", cbw_operand_json);

    ambiguous_memory_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov esi, 00500000h\n"
        "    movsx eax, [esi]\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(same_width_copy, "\"ok\":false", "same-width MOVSX should fail source-run");
    failures += expect_json_contains(same_width_copy, "operand-width-mismatch", "same-width MOVSX should report operand width mismatch");
    failures += expect_json_contains(same_width_copy, "wider than the source", "same-width MOVSX diagnostic should describe width rule");
    failures += expect_json_contains(cbw_operand_copy, "\"ok\":false", "CBW with operand should fail source-run");
    failures += expect_json_contains(cbw_operand_copy, "expected-line-end", "CBW with operand should report expected line end");
    failures += expect_json_contains(ambiguous_memory_json, "\"ok\":false", "MOVSX with ambiguous [esi] source should fail source-run");
    failures += expect_json_contains(ambiguous_memory_json, "operand-width-mismatch", "ambiguous MOVSX memory source should report operand width mismatch");
    failures += expect_json_contains(ambiguous_memory_json, "memory sources require a known 8-bit or 16-bit width", "ambiguous MOVSX memory source diagnostic should describe width requirement");

    return failures;
}


/// Verifies the Phase 20 acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_phase20_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    mov ebx, 10\n"
        "    xchg eax, ebx\n"
        "    neg eax\n"
        "    nop\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "source-run response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 20 acceptance program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 20 acceptance program should execute five instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFF6h\",\"unsigned\":4294967286}", "NEG after XCHG should leave EAX = FFFFFFF6h");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "XCHG should leave EBX = 5");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 20 acceptance program should complete successfully");

    return failures;
}

/// Verifies Phase 20 source-run memory exchange and memory negation behavior.
///
/// @return Number of failures.
static int test_phase20_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 5\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    xchg value, eax\n"
        "    neg value\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 20 memory program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "memory XCHG should move old memory into EAX");
    failures += expect_json_contains(json, "\"symbol\":\"value\"", "memory XCHG/NEG should report value memory changes");
    failures += expect_json_contains(json, "\"oldHex\":\"00000005h\",\"oldUnsigned\":5,\"newHex\":\"0000000Ah\",\"newUnsigned\":10", "memory XCHG should report value changed from 5 to 10");
    failures += expect_json_contains(json, "\"oldHex\":\"0000000Ah\",\"oldUnsigned\":10,\"newHex\":\"FFFFFFF6h\",\"newUnsigned\":4294967286", "memory NEG should report value changed from 10 to FFFFFFF6h");

    return failures;
}

/// Verifies source-run diagnostics for invalid Phase 20 operand forms.
///
/// @return Number of failures.
static int test_phase20_source_run_error_paths(void) {
    const char *xchg_mismatch_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    xchg eax, al\n"
        "main ENDP\n"
        "END main\n"
    );
    char xchg_mismatch_copy[1024];
    const char *nop_operand_json = NULL;
    char nop_operand_copy[1024];
    const char *neg_ambiguous_json = NULL;
    int failures = 0;

    if (xchg_mismatch_json == NULL) {
        return record_failure("XCHG width mismatch result should not be NULL");
    }
    (void)snprintf(xchg_mismatch_copy, sizeof(xchg_mismatch_copy), "%s", xchg_mismatch_json);

    nop_operand_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    nop eax\n"
        "main ENDP\n"
        "END main\n"
    );
    if (nop_operand_json == NULL) {
        return record_failure("NOP operand result should not be NULL");
    }
    (void)snprintf(nop_operand_copy, sizeof(nop_operand_copy), "%s", nop_operand_json);

    neg_ambiguous_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    neg [esi]\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(xchg_mismatch_copy, "\"ok\":false", "XCHG width mismatch should fail source-run");
    failures += expect_json_contains(xchg_mismatch_copy, "operand-width-mismatch", "XCHG width mismatch should report operand width mismatch");
    failures += expect_json_contains(xchg_mismatch_copy, "XCHG operand widths must match", "XCHG width mismatch diagnostic should describe width rule");
    failures += expect_json_contains(nop_operand_copy, "\"ok\":false", "NOP with operand should fail source-run");
    failures += expect_json_contains(nop_operand_copy, "unsupported-syntax", "NOP with operand should report unsupported syntax");
    failures += expect_json_contains(nop_operand_copy, "NOP does not take operands", "NOP with operand should describe no-operand rule");
    failures += expect_json_contains(neg_ambiguous_json, "\"ok\":false", "NEG ambiguous memory should fail source-run");
    failures += expect_json_contains(neg_ambiguous_json, "ambiguous-memory-width", "NEG ambiguous memory should report stable ambiguous-memory-width diagnostic");
    failures += expect_json_contains(neg_ambiguous_json, "Memory operand width is ambiguous", "NEG ambiguous memory diagnostic should describe width ambiguity");

    return failures;
}


/// Verifies the Phase 22 acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_phase21_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    add eax, 1\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "Phase 22 response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 22 acceptance program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 22 acceptance program should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 22 acceptance should leave EAX zero");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "ADC should carry into EBX");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 22 acceptance program should complete successfully");

    return failures;
}

/// Verifies Phase 22 memory operands and SBB borrow behavior through source-run.
///
/// @return Number of failures.
static int test_phase21_memory_and_borrow_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 0FFFFFFFFh\n"
        "other DWORD 1\n"
        "byteval BYTE 0\n"
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    adc value, 0\n"
        "    mov eax, 0\n"
        "    stc\n"
        "    adc eax, value\n"
        "    mov ebx, 5\n"
        "    stc\n"
        "    sbb ebx, other\n"
        "    stc\n"
        "    sbb byteval, 0\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 22 memory program should execute");
    failures += expect_json_contains(json, "\"symbol\":\"value\"", "ADC memory write should report value symbol");
    failures += expect_json_contains(json, "\"oldHex\":\"FFFFFFFFh\",\"oldUnsigned\":4294967295,\"newHex\":\"00000000h\",\"newUnsigned\":0", "ADC memory should wrap value to zero");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "ADC register destination should read updated memory source and carry into EAX");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "SBB register destination should read memory source and incoming borrow");
    failures += expect_json_contains(json, "\"symbol\":\"byteval\"", "SBB memory write should report byteval symbol");
    failures += expect_json_contains(json, "\"oldHex\":\"00h\",\"oldUnsigned\":0,\"newHex\":\"FFh\",\"newUnsigned\":255", "SBB memory should borrow byteval to FFh");

    return failures;
}

/// Verifies source-run diagnostics for invalid Phase 22 operand forms.
///
/// @return Number of failures.
static int test_phase21_source_run_error_paths(void) {
    const char *adc_overflow_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    adc al, 256\n"
        "main ENDP\n"
        "END main\n"
    );
    char adc_overflow_copy[1024];
    const char *clc_operand_json = NULL;
    int failures = 0;

    if (adc_overflow_json == NULL) {
        return record_failure("ADC overflow result should not be NULL");
    }
    (void)snprintf(adc_overflow_copy, sizeof(adc_overflow_copy), "%s", adc_overflow_json);

    clc_operand_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    clc eax\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(adc_overflow_copy, "\"ok\":false", "ADC immediate overflow should fail source-run");
    failures += expect_json_contains(adc_overflow_copy, "immediate-out-of-range", "ADC overflow should report immediate range diagnostic");
    failures += expect_json_contains(clc_operand_json, "\"ok\":false", "CLC with operand should fail source-run");
    failures += expect_json_contains(clc_operand_json, "unsupported-syntax", "CLC with operand should report unsupported syntax");
    failures += expect_json_contains(clc_operand_json, "CLC does not take operands", "CLC operand diagnostic should describe no-operand rule");

    return failures;
}


/// Verifies Phase 26 register-supplied memory-width execution through source-run.
///
/// @return Number of failures.
static int test_phase25_register_supplied_memory_width_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET buf\n"
        "    mov ebx, 5\n"
        "    mov [eax], ebx\n"
        "    add [eax], ebx\n"
        "    sub [eax], ebx\n"
        "    stc\n"
        "    adc [eax], ebx\n"
        "    clc\n"
        "    sbb [eax], ebx\n"
        "    mov cx, 1234h\n"
        "    xchg [eax], cx\n"
        "    mov dl, 34h\n"
        "    test [eax], dl\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "Phase 26 source-run response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 register-supplied memory-width source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":13", "Phase 26 register-supplied memory-width source should execute thirteen instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00500000h\",\"unsigned\":5242880}", "EAX should continue to hold the .data address");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000006h\",\"unsigned\":6}", "XCHG [eax], cx should exchange a WORD memory value with CX");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000034h\",\"unsigned\":52}", "DL should supply BYTE width for TEST [eax], dl");
    failures += expect_json_not_contains(json, "ambiguous-memory-width", "Register-supplied memory-width execution should not report ambiguity");

    return failures;
}

/// Verifies Phase 26 register destinations can supply width for register-indirect memory sources.
///
/// @return Number of failures.
static int test_phase25_register_supplied_source_memory_width_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf DWORD 7\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET buf\n"
        "    mov ebx, [eax]\n"
        "    add ebx, [eax]\n"
        "    sub bx, [eax]\n"
        "    test ebx, [eax]\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "Phase 26 source-memory response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 register-supplied source memory-width source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 26 source-memory program should execute five instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00500000h\",\"unsigned\":5242880}", "EAX should hold the .data address");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "Register destinations should supply source memory widths and produce EBX = 7");
    failures += expect_json_not_contains(json, "ambiguous-memory-width", "Register-supplied source memory-width execution should not report ambiguity");

    return failures;
}

/// Verifies browser-observed Phase 26 width and flag behavior stays stable.
///
/// @return Number of failures.
static int test_phase25_browser_observed_regressions(void) {
    char mismatch_json_copy[2048];
    const char *mismatch_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov ecx, 2\n"
        "    mov [buf + ecx], dx\n"
        "main ENDP\n"
        "END main\n"
    );
    const char *test_flags_json = NULL;
    int failures = 0;

    if (mismatch_json == NULL) {
        return record_failure("symbol-relative mismatch source-run result should not be NULL");
    }
    (void)snprintf(mismatch_json_copy, sizeof(mismatch_json_copy), "%s", mismatch_json);

    test_flags_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 0F0F0F0F0h\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET value\n"
        "    mov ebx, 0F0000000h\n"
        "    test [eax], ebx\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(mismatch_json_copy, "\"ok\":false", "Symbol-relative BYTE memory paired with DX should fail source-run");
    failures += expect_json_contains(mismatch_json_copy, "operand-width-mismatch", "Symbol metadata should produce a width mismatch, not register override");
    failures += expect_json_not_contains(mismatch_json_copy, "ambiguous-memory-width", "Known symbol-relative BYTE width should not be ambiguous");

    failures += expect_json_contains(test_flags_json, "\"ok\":true", "TEST [eax], ebx with a valid .data address should execute");
    failures += expect_json_contains(test_flags_json, "\"EFLAGS\":{\"hex\":\"00000080h\",\"unsigned\":128}", "TEST result with sign bit set should set SF and clear CF/OF/ZF");
    failures += expect_json_not_contains(test_flags_json, "ambiguous-memory-width", "Register-supplied TEST width should not report ambiguity");

    return failures;
}

/// Verifies explicit PTR can intentionally override symbol metadata in source-run.
///
/// @return Number of failures.
static int test_phase25_explicit_ptr_symbol_register_override_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "buf BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov ecx, 2\n"
        "    mov dx, 1234h\n"
        "    mov WORD PTR [buf + ecx], dx\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "Explicit PTR override source-run response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Explicit WORD PTR symbol/register program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Explicit PTR override program should execute three instructions");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00001234h\",\"unsigned\":4660}", "DX should retain the written WORD value");
    failures += expect_json_contains(json, "\"symbol\":\"buf\",\"address\":\"00500002h\",\"widthBits\":16", "Memory change should record a WORD access at buf + 2");
    failures += expect_json_not_contains(json, "operand-width-mismatch", "Explicit PTR override should not report symbol/register width mismatch");
    failures += expect_json_not_contains(json, "ambiguous-memory-width", "Explicit PTR override should not report ambiguous memory width");

    return failures;
}

/// Verifies one source-run program reports the stable ambiguous memory-width diagnostic.
///
/// @param source Source program expected to fail during assembly validation.
/// @param label Human-readable test label.
/// @return Number of failures.
static int expect_ambiguous_memory_width_source_run(const char *source, const char *label) {
    const char *json = masm32_sim_wasm_run_source_json(source);
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", label);
    failures += expect_json_contains(json, "ambiguous-memory-width", "Ambiguous source-run diagnostic should use stable code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "Ambiguous source-run diagnostic should describe width ambiguity");
    failures += expect_json_contains(json, "BYTE PTR", "Ambiguous source-run diagnostic should suggest explicit PTR widths");
    failures += expect_json_not_contains(json, "unsupported-feature", "Ambiguous memory width should not be classified as unsupported-feature");
    failures += expect_json_not_contains(json, "unsupported by the current milestone", "Ambiguous memory width should not be described as a milestone limitation");

    return failures;
}

/// Verifies Phase 26 ambiguous memory-width source-run diagnostics across memory-capable instructions.
///
/// @return Number of failures.
static int test_phase25_ambiguous_memory_width_source_run_error_paths(void) {
    int failures = 0;

    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    mov [eax], 1\nmain ENDP\nEND main\n", "MOV [eax], imm should fail source-run with ambiguous memory width");
    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    add [eax], 1\nmain ENDP\nEND main\n", "ADD [eax], imm should fail source-run with ambiguous memory width");
    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    sub [eax], 1\nmain ENDP\nEND main\n", "SUB [eax], imm should fail source-run with ambiguous memory width");
    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    adc [eax], 1\nmain ENDP\nEND main\n", "ADC [eax], imm should fail source-run with ambiguous memory width");
    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    sbb [eax], 1\nmain ENDP\nEND main\n", "SBB [eax], imm should fail source-run with ambiguous memory width");
    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    test [eax], 1\nmain ENDP\nEND main\n", "TEST [eax], imm should fail source-run with ambiguous memory width");
    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    test [eax + 4], 1\nmain ENDP\nEND main\n", "TEST [eax + 4], imm should fail source-run with ambiguous memory width");
    failures += expect_ambiguous_memory_width_source_run(".code\nmain PROC\n    neg [eax]\nmain ENDP\nEND main\n", "NEG [eax] should fail source-run with ambiguous memory width");

    return failures;
}

/// Verifies Phase 26 accepted headers execute the program body through source-run.
///
/// @return Number of failures.
static int test_phase26_header_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".386\n"
        ".model flat, stdcall\n"
        ".stack 4096\n"
        "OPTION CASEMAP:NONE\n"
        "INCLUDE Irvine32.inc\n"
        "INCLUDE Macros.inc\n"
        "TITLE Header Sample\n"
        "PAGE 60, 132\n"
        ".data\n"
        "msg BYTE \"Hello\", 0\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, OFFSET msg\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":29", "Phase 26 header response should identify Milestone 29");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 header source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":1", "Phase 26 header source should execute one instruction");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00500000h\",\"unsigned\":5242880}", "Phase 26 header source should set EDX to OFFSET msg");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 26 header source should complete execution");

    return failures;
}

/// Verifies Phase 26 unsupported header forms reach source-run diagnostics.
///
/// @return Number of failures.
static int test_phase26_header_source_run_error_paths(void) {
    int failures = 0;
    char model_copy[2048];
    char include_copy[2048];
    char option_copy[2048];
    char include_only_copy[2048];
    const char *model_json = masm32_sim_wasm_run_source_json(
        ".model small, c\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(model_copy, sizeof(model_copy), "%s", model_json != NULL ? model_json : "");

    const char *include_json = masm32_sim_wasm_run_source_json(
        "INCLUDE Windows.inc\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(include_copy, sizeof(include_copy), "%s", include_json != NULL ? include_json : "");

    const char *include_only_json = masm32_sim_wasm_run_source_json(
        "INCLUDE Windows.inc\n"
    );
    (void)snprintf(include_only_copy, sizeof(include_only_copy), "%s", include_only_json != NULL ? include_only_json : "");

    const char *option_json = masm32_sim_wasm_run_source_json(
        "OPTION NOKEYWORD:<IF>\n"
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(option_copy, sizeof(option_copy), "%s", option_json != NULL ? option_json : "");

    failures += expect_json_contains(model_copy, "\"ok\":false", "Unsupported .model source should fail before execution");
    failures += expect_json_contains(model_copy, "unsupported-model", "Unsupported .model source should expose unsupported-model code");
    failures += expect_json_contains(model_copy, "line\":1", "Unsupported .model diagnostic should preserve line");
    failures += expect_json_contains(model_copy, "byteOffset\":0", "Unsupported .model diagnostic should preserve byte offset");

    failures += expect_json_contains(include_copy, "\"ok\":false", "Unsupported include source should fail before execution");
    failures += expect_json_contains(include_copy, "unsupported-include", "Unsupported include source should expose unsupported-include code");
    failures += expect_json_contains(include_copy, "Only virtual INCLUDE Irvine32.inc and INCLUDE Macros.inc", "Unsupported include diagnostic should describe virtual include limit");
    failures += expect_json_contains(include_copy, "column\":9", "Unsupported include diagnostic should point at the include path");
    failures += expect_json_contains(include_only_copy, "unsupported-include", "Unsupported include-only source should expose unsupported-include code");
    failures += expect_json_not_contains(include_only_copy, "expected-code-directive", "Unsupported include-only source should not add a redundant expected-code diagnostic");

    failures += expect_json_contains(option_copy, "\"ok\":false", "Unsupported OPTION source should fail before execution");
    failures += expect_json_contains(option_copy, "unsupported-option", "Unsupported OPTION source should expose unsupported-option code");

    return failures;
}

/// Verifies .DATA? and .CONST source-run behavior under the current Milestone 29 source-run API.
///
/// @return Number of failures.
static int test_phase28_additional_data_sections_source_run_programs(void) {
    const char *acceptance_json = masm32_sim_wasm_run_source_json(
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
        "END main\n"
    );
    const char *direct_write_json = NULL;
    const char *indirect_write_json = NULL;
    const char *offset_write_json = NULL;
    int failures = 0;

    failures += expect_json_contains(acceptance_json, "\"phase\":29", "Phase 29 response should identify Milestone 29");
    failures += expect_json_contains(acceptance_json, "\"ok\":true", "Phase 29 acceptance source should execute");
    failures += expect_json_contains(acceptance_json, "\"EAX\":{\"hex\":\"00000010h\",\"unsigned\":16}", "Phase 29 acceptance source should set EAX to SIZEOF buf");
    failures += expect_json_contains(acceptance_json, "\"EBX\":{\"hex\":\"0000000Ah\",\"unsigned\":10}", "Phase 29 acceptance source should read .CONST limit");
    failures += expect_json_contains(acceptance_json, "\"code\":\"execution-complete\"", "Phase 29 acceptance source should complete execution");

    direct_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov limit, 20\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(direct_write_json, "\"ok\":false", "direct .CONST write should fail");
    failures += expect_json_contains(direct_write_json, "const-write", "direct .CONST write should use const-write diagnostic");

    indirect_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 20\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(indirect_write_json, "\"ok\":false", "indirect .CONST write should fail");
    failures += expect_json_contains(indirect_write_json, "permission-denied", "indirect .CONST write should fail through checked memory permissions");
    failures += expect_json_contains(indirect_write_json, ".const", "indirect .CONST write should identify .const region");

    offset_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov BYTE PTR [eax + 3], 0FFh\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(offset_write_json, "\"ok\":false", "calculated .CONST byte write should fail");
    failures += expect_json_contains(offset_write_json, "permission-denied", "calculated .CONST byte write should fail through checked memory permissions");

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
    failures += test_source_run_invalid_hex_reports_specific_lexer_diagnostic();
    failures += test_source_run_unterminated_string_reports_specific_lexer_diagnostic();
    failures += test_source_run_other_lexer_diagnostics_are_specific();
    failures += test_narrow_register_immediate_overflow_returns_parse_error();
    failures += test_negative_immediate_source_run_succeeds();
    failures += test_negative_immediate_overflow_returns_parse_error();
    failures += test_register_indirect_source_run_succeeds();
    failures += test_phase24_eax_base_acceptance_program();
    failures += test_all_gpr_register_indirect_source_run_succeeds();
    failures += test_type_operator_source_run_acceptance_program();
    failures += test_type_operator_source_run_element_sizes();
    failures += test_lengthof_operator_source_run_acceptance_program();
    failures += test_lengthof_operator_source_run_element_counts();
    failures += test_sizeof_operator_source_run_acceptance_program();
    failures += test_sizeof_operator_source_run_byte_sizes();
    failures += test_sizeof_operator_source_run_rejects_expression_tail();
    failures += test_character_literal_source_run_rejects_width_overflow();
    failures += test_character_literal_source_run_accepts_narrower_packed_immediates();
    failures += test_lengthof_operator_source_run_rejects_expression_tail();
    failures += test_type_operator_source_run_rejects_expression_tail();
    failures += test_scaled_index_source_run_returns_unsupported_feature();
    failures += test_constant_symbol_offset_source_run_succeeds();
    failures += test_unaligned_constant_symbol_offset_reports_warning();
    failures += test_negative_symbol_offset_inside_data_image_succeeds();
    failures += test_offset_zero_bracketed_symbol_operands_execute();
    failures += test_constant_symbol_offset_out_of_bounds_returns_parse_error();
    failures += test_textbook_unsupported_features_return_unsupported_feature_messages();
    failures += test_multi_diagnostic_unsupported_feature_source_run_reports_all();
    failures += test_signed_integer_source_run_acceptance_program();
    failures += test_unary_plus_source_run_acceptance_program();
    failures += test_signed_integer_source_run_error_paths();
    failures += test_signed_ptr_alias_source_run_acceptance_program();
    failures += test_signed_ptr_alias_source_run_write_program();
    failures += test_signed_ptr_alias_source_run_error_paths();
    failures += test_extension_source_run_acceptance_program();
    failures += test_accumulator_extension_source_run_program();
    failures += test_extension_source_run_edge_cases();
    failures += test_extension_register_indirect_memory_source_run_program();
    failures += test_plain_mov_from_signed_memory_rejects_implicit_widening();
    failures += test_extension_source_run_error_paths();
    failures += test_phase20_source_run_acceptance_program();
    failures += test_phase20_memory_source_run_program();
    failures += test_phase20_source_run_error_paths();
    failures += test_phase21_source_run_acceptance_program();
    failures += test_phase21_memory_and_borrow_source_run_program();
    failures += test_phase21_source_run_error_paths();
    failures += test_phase22_source_run_acceptance_program();
    failures += test_phase22_memory_immediate_source_run_program();
    failures += test_phase22_source_run_error_paths();
    failures += test_phase25_register_supplied_memory_width_source_run_program();
    failures += test_phase25_register_supplied_source_memory_width_source_run_program();
    failures += test_phase25_browser_observed_regressions();
    failures += test_phase25_explicit_ptr_symbol_register_override_source_run_program();
    failures += test_phase25_ambiguous_memory_width_source_run_error_paths();
    failures += test_phase26_header_source_run_acceptance_program();
    failures += test_phase26_header_source_run_error_paths();
    failures += test_phase28_additional_data_sections_source_run_programs();
    failures += test_null_source_returns_invalid_argument_json();
    failures += test_empty_source_returns_parse_error_json();
    failures += test_subsequent_calls_return_latest_result();

    if (failures != 0) {
        return 1;
    }

    puts("Milestone 29 source execution tests passed.");
    return 0;
}
