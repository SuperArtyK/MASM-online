/*
 * @file test_wasm_source_run.c
 * @brief Tests for the Wasm-facing source execution API through Phase 77 PROC USES runtime save/restore coverage.
 *
 * These tests verify the narrow browser-facing C export that parses and runs
 * supported `.code` and data-section programs, reports final registers and
 * memory changes as JSON, and returns structured simulator messages for parse,
 * argument, layout, linker/library, label-diagnostic, capacity, planned stack
 * access, and runtime errors.
 */

#include <stdio.h>
#include <string.h>

#include "../../src/wasm/wasm_api.h"

/// Number of bytes reserved for local copies of source-run JSON results.
#define TEST_JSON_COPY_CAPACITY 8192U

/// Exact current source-run output-contract identifier expected in source-run JSON.
#define TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT "\"sourceRunOutputContract\":\"phase-77-proc-uses-runtime-output-contract-v1\""

/// Exact zero-startup notice wording expected in source-run JSON.
#define TEST_STARTUP_STATE_NOTICE_TEXT "The simulator starts modeled flags and all registers to 0, except ESP and EIP. ESP is set to the end of the active stack region, and EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register. Uninitialized storage bytes are also zero-filled, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values."

/// Exact Phase 57F seeded register/flag startup notice wording expected in source-run JSON.
#define TEST_SEEDED_STARTUP_NOTICE_TEXT "The simulator starts modeled flags and ordinary registers from the configured deterministic seed, except ESP and EIP. ESP is set to the end of the active stack region, and EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register. Uninitialized storage bytes remain zero-filled, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values."

/// Exact Phase 57G seeded uninitialized-storage startup notice wording expected in source-run JSON.
#define TEST_SEEDED_UNINITIALIZED_STORAGE_NOTICE_TEXT "The simulator starts modeled flags and all registers to 0, except ESP and EIP. ESP is set to the end of the active stack region, EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register, and visible bytes for uninitialized storage are initialized from the configured deterministic seed while uninitialized-origin metadata is preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register, flag, or storage startup values."

/// Exact notice wording expected when both Phase 57F and Phase 57G seeded startup axes are enabled.
#define TEST_SEEDED_REGISTER_AND_UNINITIALIZED_STORAGE_NOTICE_TEXT "The simulator starts modeled flags, ordinary registers, and visible bytes for uninitialized storage from the configured deterministic seed, except ESP and EIP. ESP is set to the end of the active stack region, and EIP is displayed as a derived VM pseudo-code address for the current execution position, not as a source-writable register. Uninitialized-origin metadata is preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register, flag, or storage startup values."

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


/// Copies a source-run JSON result out of the API static result buffer.
///
/// @param destination Destination character buffer.
/// @param destination_size Destination buffer size in bytes.
/// @param json Source JSON pointer returned by the source-run API.
static void copy_source_run_json(char *destination, size_t destination_size, const char *json) {
    if (destination == NULL || destination_size == 0U) {
        return;
    }

    (void)snprintf(destination, destination_size, "%s", json != NULL ? json : "");
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

/// Counts non-overlapping occurrences of a fragment in a JSON string.
///
/// @param json JSON string to inspect.
/// @param fragment Substring to count.
/// @return Number of non-overlapping occurrences.
static size_t count_json_fragment_occurrences(const char *json, const char *fragment) {
    size_t count = 0U;
    size_t fragment_length = 0U;
    const char *cursor = json;

    if (json == NULL || fragment == NULL || fragment[0] == '\0') {
        return 0U;
    }

    fragment_length = strlen(fragment);
    while ((cursor = strstr(cursor, fragment)) != NULL) {
        count += 1U;
        cursor += fragment_length;
    }

    return count;
}

/// Verifies that a returned JSON string contains a fragment exactly once.
///
/// @param json JSON string to inspect.
/// @param fragment Substring expected once.
/// @param message Failure message when the occurrence count differs.
/// @return Zero on success, otherwise one failure.
static int expect_json_contains_once(const char *json, const char *fragment, const char *message) {
    size_t count = count_json_fragment_occurrences(json, fragment);
    if (count != 1U) {
        fprintf(stderr, "FAIL: %s\nExpected exactly one occurrence of: %s\nActual count: %lu\nJSON: %s\n", message, fragment, (unsigned long)count, json != NULL ? json : "(null)");
        return 1;
    }

    return 0;
}

/// Verifies that one JSON fragment appears before another fragment.
///
/// @param json JSON string to inspect.
/// @param first Fragment expected earlier.
/// @param second Fragment expected later.
/// @param message Failure message when either fragment is missing or misordered.
/// @return Zero on success, otherwise one failure.
static int expect_json_fragment_order(const char *json, const char *first, const char *second, const char *message) {
    const char *first_position = json != NULL ? strstr(json, first) : NULL;
    const char *second_position = json != NULL ? strstr(json, second) : NULL;

    if (first_position == NULL || second_position == NULL || first_position >= second_position) {
        fprintf(stderr, "FAIL: %s\nExpected order: %s before %s\nJSON: %s\n", message, first, second, json != NULL ? json : "(null)");
        return 1;
    }

    return 0;
}

/// Appends one text fragment to a generated source buffer.
///
/// @param destination Destination buffer.
/// @param capacity Destination capacity in bytes.
/// @param length Current write offset, updated on success.
/// @param fragment Null-terminated fragment to append.
/// @return Zero on success, otherwise one failure.
static int append_source_fragment(char *destination, size_t capacity, size_t *length, const char *fragment) {
    size_t fragment_length = 0U;

    if (destination == NULL || length == NULL || fragment == NULL) {
        return record_failure("source fixture builder received an invalid argument");
    }

    fragment_length = strlen(fragment);
    if (*length + fragment_length + 1U > capacity) {
        return record_failure("source fixture builder capacity is too small");
    }

    memcpy(destination + *length, fragment, fragment_length);
    *length += fragment_length;
    destination[*length] = '\0';
    return 0;
}

/// Verifies one Phase 71D front-procedure notice matching fixture.
///
/// @param procedure_name Procedure name to place in both PROC and END.
/// @param expect_notice Whether the fixture must emit the-front-fell-off.
/// @param message Human-readable fixture description for failure output.
/// @return Number of failures.
static int expect_phase71c_front_notice_fixture(const char *procedure_name, int expect_notice, const char *message) {
    char source[384];
    char json[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    if (procedure_name == NULL || message == NULL) {
        return record_failure("Phase 71D front notice fixture received an invalid argument");
    }

    (void)snprintf(
        source,
        sizeof(source),
        ".code\n"
        "%s PROC\n"
        "    mov eax, 1\n"
        "%s ENDP\n"
        "END %s\n",
        procedure_name,
        procedure_name,
        procedure_name
    );
    copy_source_run_json(json, sizeof(json), masm32_sim_wasm_run_source_json(source));

    failures += expect_json_contains(json, "\"code\":\"code-fell-off-end\"", message);
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "front notice fixture should remain an execution-error");
    if (expect_notice) {
        failures += expect_json_contains(json, "\"kind\":\"simulator-notice\"", "front procedure notice should use notice severity");
        failures += expect_json_contains(json, "\"code\":\"the-front-fell-off\"", "front procedure name variant should emit the required notice");
        failures += expect_json_contains(json, "that's not very typical, I'd like to make that point", "front procedure notice should use the exact required text");
        failures += expect_json_fragment_order(
            json,
            "\"code\":\"code-fell-off-end\"",
            "\"code\":\"the-front-fell-off\"",
            "front notice should render after code-fell-off-end"
        );
    } else {
        failures += expect_json_not_contains(json, "the-front-fell-off", "non-matching procedure name should not emit the front notice");
    }

    return failures;
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "successful source run should set ok true");
    failures += expect_json_contains(json, "\"status\":\"ok\"", "successful source run should report ok status");
    failures += expect_json_contains(json, "\"instructionCount\":3", "sample should execute two instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "sample should expose EAX = 42");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "success should include simulator completion message");
    failures += expect_json_not_contains(json, "Program Console", "source-run JSON should not merge Program Console text into simulator messages");

    return failures;
}

/// Verifies Phase 71C reports code-end falloff for an empty selected-entry procedure.
///
/// @return Number of failures.
static int test_zero_instruction_program_reports_code_end_falloff(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "empty selected-entry program should fail under Phase 71C code-stream semantics");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "empty selected-entry program should report execution-error");
    failures += expect_json_contains(json, "\"instructionCount\":0", "empty selected-entry program should have no lowered instructions");
    failures += expect_json_contains(json, "\"executedInstructionCount\":0", "empty selected-entry program should report zero committed instructions");
    failures += expect_json_contains(json, "\"attemptedNextInstructionIndex\":null", "empty selected-entry program should not report an attempted blocked instruction");
    failures += expect_json_contains(json, "\"currentInstructionIndex\":null", "empty selected-entry program should report no last committed instruction");
    failures += expect_json_contains(json, "\"code\":\"code-fell-off-end\"", "empty selected-entry program should emit code-fell-off-end");
    failures += expect_json_contains(json, "\"procedure\":\"main\"", "empty selected-entry code-end falloff should identify main as responsible procedure");
    failures += expect_json_not_contains(json, "execution-complete", "empty selected-entry program should not report successful completion");

    return failures;
}


/// Verifies Phase 71C code-stream fallthrough and front-name notice behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase71c_code_stream_falloff_source_run_behavior(void) {
    char after_json[TEST_JSON_COPY_CAPACITY];
    char fallthrough_json[TEST_JSON_COPY_CAPACITY];
    char front_json[TEST_JSON_COPY_CAPACITY];
    char front_label_json[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    copy_source_run_json(after_json, sizeof(after_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "after PROC\n"
        "    mov eax, 4\n"
        "after ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(fallthrough_json, sizeof(fallthrough_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 2\n"
        "helper ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(front_json, sizeof(front_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "Front PROC\n"
        "    mov eax, 1\n"
        "Front ENDP\n"
        "END Front\n"
    ));
    copy_source_run_json(front_label_json, sizeof(front_label_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "front:\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(after_json, "\"ok\":false", "empty selected entry should fall through to later executable code and fail at code end");
    failures += expect_json_contains(after_json, "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "empty selected entry should execute the later procedure body");
    failures += expect_json_contains(after_json, "\"code\":\"code-fell-off-end\"", "empty selected entry later body should report code-fell-off-end");
    failures += expect_json_contains(after_json, "\"procedure\":\"after\"", "empty selected entry later body should identify the last executed procedure");
    failures += expect_json_not_contains(after_json, "execution-complete", "empty selected entry later body should not report completion");

    failures += expect_json_contains(fallthrough_json, "\"ok\":false", "selected entry without terminator should fall through into later procedure and fail at code end");
    failures += expect_json_contains(fallthrough_json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "selected entry body should execute before fallthrough");
    failures += expect_json_contains(fallthrough_json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "later procedure body should execute by ordinary code-stream fallthrough");
    failures += expect_json_contains(fallthrough_json, "\"procedure\":\"helper\"", "fallthrough should attribute code end to the last executed later procedure");

    failures += expect_json_contains(front_json, "\"code\":\"code-fell-off-end\"", "front procedure falloff should report code-fell-off-end first");
    failures += expect_json_contains(front_json, "\"code\":\"the-front-fell-off\"", "front procedure falloff should emit the required notice");
    failures += expect_json_contains(front_json, "that's not very typical, I'd like to make that point", "front procedure notice should use the exact required text");
    failures += expect_json_fragment_order(
        front_json,
        "\"code\":\"code-fell-off-end\"",
        "\"code\":\"the-front-fell-off\"",
        "front procedure notice should appear after code-fell-off-end"
    );

    failures += expect_json_contains(front_label_json, "\"code\":\"code-fell-off-end\"", "front label falloff should still report code-fell-off-end");
    failures += expect_json_not_contains(front_label_json, "the-front-fell-off", "front label should not trigger the front procedure notice");

    failures += expect_phase71c_front_notice_fixture("front", 1, "lowercase front procedure should emit code-fell-off-end");
    failures += expect_phase71c_front_notice_fixture("Front", 1, "titlecase Front procedure should emit code-fell-off-end");
    failures += expect_phase71c_front_notice_fixture("FRONT", 1, "uppercase FRONT procedure should emit code-fell-off-end");
    failures += expect_phase71c_front_notice_fixture("fRoNt", 1, "mixed-case fRoNt procedure should emit code-fell-off-end");
    failures += expect_phase71c_front_notice_fixture("frontier", 0, "frontier procedure should emit code-fell-off-end without the front notice");
    failures += expect_phase71c_front_notice_fixture("myfront", 0, "myfront procedure should emit code-fell-off-end without the front notice");
    failures += expect_phase71c_front_notice_fixture("front_", 0, "front_ procedure should emit code-fell-off-end without the front notice");

    return failures;
}

/// Verifies the Milestone 30 register-indirect source-run acceptance program.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "register-indirect source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "register-indirect sample should execute three instructions");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 26 EAX-base response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 EAX-base acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 26 EAX-base acceptance source should execute three instructions");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "all-GPR response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "all-GPR register-indirect source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000050h\",\"unsigned\":80}", "all-GPR register-indirect source should load 80 through ESP");
    failures += expect_json_contains(json, "\"address\":\"0050001Ch\"", "all-GPR register-indirect source should write through ESP + 28");

    return failures;
}

/// Verifies the Milestone 30 TYPE acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_type_operator_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE nums\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "TYPE acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":2", "TYPE acceptance source should execute one instruction");
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
        "    ret\n"
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

/// Verifies the Milestone 30 LENGTHOF acceptance program through the source-run API.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "LENGTHOF acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "LENGTHOF acceptance source should execute two instructions");
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
        "    ret\n"
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

/// Verifies the Milestone 30 SIZEOF and character-literal acceptance program through the source-run API.
///
/// @return Number of failures.
static int test_sizeof_operator_source_run_acceptance_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "SIZEOF acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "SIZEOF acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000028h\",\"unsigned\":40}", "SIZEOF nums should expose EAX = 40");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000041h\",\"unsigned\":65}", "mov bl, chr should expose BL byte value through EBX = 65");
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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "SIZEOF expression tail should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "SIZEOF expression tail should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "SIZEOF expression tail should be categorized as unsupported feature");
    failures += expect_json_contains(json, "unsupported-sizeof-expression", "SIZEOF expression tail should expose stable diagnostic code");
    failures += expect_json_contains(json, "Write SIZEOF followed by exactly one declared data symbol", "SIZEOF expression diagnostic should be user-readable");

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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "three-byte packed character literal should execute in 32-bit contexts");
    failures += expect_json_contains(json, "\"instructionCount\":3", "packed character literal program should execute two instructions");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "LENGTHOF expression tail should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "LENGTHOF expression tail should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "LENGTHOF expression tail should be categorized as unsupported feature");
    failures += expect_json_contains(json, "unsupported-lengthof-expression", "LENGTHOF expression tail should expose stable diagnostic code");
    failures += expect_json_contains(json, "Write LENGTHOF followed by exactly one declared data symbol", "LENGTHOF expression diagnostic should be user-readable");

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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "TYPE expression tail should fail source run");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "TYPE expression tail should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "TYPE expression tail should be categorized as unsupported feature");
    failures += expect_json_contains(json, "unsupported-type-expression", "TYPE expression tail should expose stable diagnostic code");
    failures += expect_json_contains(json, "Write TYPE followed by exactly one declared data symbol", "TYPE expression diagnostic should be user-readable");

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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    const char *array_scaled_json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR nums[esi * 4]\n"
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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


/// Verifies the Milestone 9 constant symbol-offset acceptance program.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "constant symbol-offset source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "constant symbol-offset sample should execute two instructions");
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "offset-zero bracketed symbol operands should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "offset-zero bracketed symbol sample should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "[nums + 0] should read the first DWORD");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000065h\",\"unsigned\":101}", "nums[0] should read the updated first DWORD");
    failures += expect_json_contains(json, "\"byteOffset\":0", "offset-zero memory changes should include byte offset 0");
    failures += expect_json_contains(json, "\"elementIndex\":0", "offset-zero aligned changes should include element index 0");

    return failures;
}

/// Verifies symbol offsets that cross the declared section image are runtime-controlled.
///
/// @return Number of failures.
static int test_constant_symbol_offset_crossing_section_image_is_not_parse_error(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, nums[37]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "symbol offset crossing section image should be runtime-controlled in default mode");
    failures += expect_json_contains(json, "\"status\":\"ok\"", "symbol offset crossing section image should not be a parse error");
    failures += expect_json_not_contains(json, "symbol-offset-out-of-range", "section-image crossing must not emit symbol-offset-out-of-range");
    failures += expect_json_contains(json, "unaligned-memory-access", "unaligned warning behavior should remain intact");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "region-valid section-image crossing should complete in default mode");

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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 22 response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 22 TEST acceptance program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 22 TEST acceptance program should execute three instructions");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 22 TEST memory/immediate program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 22 TEST memory/immediate program should execute four instructions");
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
    char ambiguous_copy[TEST_JSON_COPY_CAPACITY];
    char ambiguous_displacement_copy[TEST_JSON_COPY_CAPACITY];
    char ambiguous_eax_copy[TEST_JSON_COPY_CAPACITY];
    char eax_invalid_memory_copy[TEST_JSON_COPY_CAPACITY];
    char invalid_memory_copy[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    ambiguous_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    test [esi], 1\n"
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    (void)snprintf(invoke_json, sizeof(invoke_json), "%s", masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "INVOKE ExitProcess, 0\n"
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(struct_json, "\"ok\":false", "STRUCT source should fail source run");
    failures += expect_json_contains(struct_json, "\"kind\":\"unsupported-feature\"", "STRUCT should be classified as unsupported feature");
    failures += expect_json_contains(struct_json, "\"code\":\"unsupported-feature\"", "STRUCT should use stable unsupported-feature code");
    failures += expect_json_contains(struct_json, "STRUCT declarations are not supported yet", "STRUCT message should be specific");

    failures += expect_json_contains(invoke_json, "\"kind\":\"unsupported-feature\"", "INVOKE should be classified as unsupported feature");
    failures += expect_json_contains(invoke_json, "unsupported-invoke", "INVOKE should expose the Phase 57R unsupported-invoke code");
    failures += expect_json_contains(invoke_json, "INVOKE syntax is not implemented", "INVOKE message should be specific");
    failures += expect_json_contains(invoke_json, "unsupported-winapi-execution", "INVOKE ExitProcess should expose the Phase 57R WinAPI code");

    failures += expect_json_contains(dataq_json, "\"ok\":true", ".DATA? and .CONST should now execute successfully");
    failures += expect_json_contains(dataq_json, "\"instructionCount\":1", ".DATA? and .CONST no-op body should execute zero instructions");
    failures += expect_json_contains(dataq_json, "execution-complete", ".DATA? and .CONST should complete execution");
    failures += expect_json_not_contains(dataq_json, "unsupported-feature", ".DATA? and .CONST should no longer be unsupported features");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".data\n"
        "Greeting TEXTEQU <Hello>\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", "TEXTEQU angle-bracket sample should be classified as unsupported feature");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "mov eax, 1\n"
        ".IF eax == 1\n"
        "mov ebx, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", ".IF comparison sample should be classified as unsupported feature");
    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "mov eax, 1\n"
        ".IF eax == 1\n"
        "mov ebx, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ), "unsupported-high-level-if", ".IF comparison sample should expose Phase 57S high-level IF code");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "mov ecx, 3\n"
        ".WHILE ecx > 0\n"
        "sub ecx, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", ".WHILE comparison sample should be classified as unsupported feature");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        ".REPEAT\n"
        "mov eax, 1\n"
        ".UNTIL eax == 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", ".REPEAT/.UNTIL sample should be classified as unsupported feature");

    failures += expect_json_contains(masm32_sim_wasm_run_source_json(
        ".data\n"
        "r REAL4 1.0\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n"
    ), "\"kind\":\"unsupported-feature\"", "REAL4 float sample should be classified as unsupported feature");

    return failures;
}

/// Verifies Milestone 30 source-run JSON reports multiple recovered unsupported features.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":false", "unsupported recovery source should not execute");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "unsupported recovery source should be a parse error");
    failures += expect_json_contains(json, "STRUCT declarations", "source-run should include STRUCT diagnostic");
    failures += expect_json_contains(json, "unsupported-invoke", "source-run should include INVOKE diagnostic");
    failures += expect_json_contains(json, "INVOKE syntax is not implemented", "source-run should include INVOKE diagnostic text");
    failures += expect_json_contains(json, ".IF high-level MASM flow", "source-run should include .IF diagnostic");
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
    failures += expect_json_contains(json, "unsupported-high-level-endif", "source-run should include .ENDIF diagnostic code");
    failures += expect_json_not_contains(json, "execution-complete", "source-run should not execute when diagnostics exist");

    return failures;
}

/// Verifies Phase 57S source-run JSON reports high-level-flow markers and refuses execution.
///
/// @return Number of failures.
static int test_phase57s_high_level_flow_source_run_diagnostics(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    .IF eax == 1\n"
        "        mov ebx, 2\n"
        "    .ELSE\n"
        "        badinstruction eax\n"
        "    .ENDIF\n"
        "    mov ecx, 3\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "high-level flow source should refuse execution");
    failures += expect_json_contains(json, "unsupported-high-level-if", ".IF diagnostic code should be surfaced");
    failures += expect_json_contains(json, "unsupported-high-level-else", ".ELSE diagnostic code should be surfaced");
    failures += expect_json_contains(json, "unsupported-high-level-endif", ".ENDIF diagnostic code should be surfaced");
    failures += expect_json_contains(json, ".IF high-level MASM flow is not implemented", ".IF diagnostic message should be stable");
    failures += expect_json_not_contains(json, "badinstruction", "unsupported body instruction should not cascade");
    failures += expect_json_not_contains(json, "execution-complete", "high-level flow diagnostics should prevent execution");
    failures += expect_json_not_contains(json, "programConsole", "high-level flow diagnostics should not produce Program Console output");

    return failures;
}

/// Verifies the Milestone 18 signed declaration acceptance program through the current source-run API.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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


/// Verifies Milestone 23 signed PTR alias reads through the source-run API.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "signed PTR alias response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "signed PTR alias acceptance program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "SBYTE PTR b should load FFh into AL without sign extension");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000FFFEh\",\"unsigned\":65534}", "SWORD PTR w should load FFFEh into BX without sign extension");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"FFFFFFFDh\",\"unsigned\":4294967293}", "SDWORD PTR d should load raw FFFFFFFDh into ECX");

    return failures;
}

/// Verifies Milestone 23 signed PTR alias writes through register-indirect memory.
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
        "    ret\n"
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

/// Verifies Milestone 23 signed PTR alias error diagnostics through source-run.
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
        "    ret\n"
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
        "    ret\n"
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

/// Verifies the Milestone 19 sign/zero-extension acceptance program through the current source-run API.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "response should identify current runtime phase");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "accumulator extension program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":8", "accumulator extension program should execute seven instructions");
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    mov ebx, 10\n"
        "    xchg eax, ebx\n"
        "    neg eax\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "source-run response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 20 acceptance program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Phase 20 acceptance program should execute five instructions");
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
        "    ret\n"
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
        "    ret\n"
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
        "    nop al\n"
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(xchg_mismatch_copy, "\"ok\":false", "XCHG width mismatch should fail source-run");
    failures += expect_json_contains(xchg_mismatch_copy, "operand-width-mismatch", "XCHG width mismatch should report operand width mismatch");
    failures += expect_json_contains(xchg_mismatch_copy, "XCHG operand widths must match", "XCHG width mismatch diagnostic should describe width rule");
    failures += expect_json_contains(nop_operand_copy, "\"ok\":false", "NOP with operand should fail source-run");
    failures += expect_json_contains(nop_operand_copy, "invalid-operand-size", "NOP 8-bit register should report invalid operand size");
    failures += expect_json_contains(nop_operand_copy, "no 8-bit encoding-operand form", "NOP 8-bit register diagnostic should describe valid register widths");
    failures += expect_json_contains(neg_ambiguous_json, "\"ok\":false", "NEG ambiguous memory should fail source-run");
    failures += expect_json_contains(neg_ambiguous_json, "ambiguous-memory-width", "NEG ambiguous memory should report stable ambiguous-memory-width diagnostic");
    failures += expect_json_contains(neg_ambiguous_json, "Memory operand width is ambiguous", "NEG ambiguous memory diagnostic should describe width ambiguity");

    return failures;
}

/// Verifies Phase 57N zero-operand NOP source-run no-op behavior.
///
/// @return Number of failures.
static int test_phase57n_nop_source_run_hardening(void) {
    char between_copy[4096];
    char before_copy[4096];
    char after_copy[4096];
    char only_copy[4096];
    const char *between_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    nop\n"
        "    inc eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    const char *before_json = NULL;
    const char *after_json = NULL;
    const char *only_json = NULL;
    int failures = 0;

    if (between_json == NULL) {
        return record_failure("Phase 57N NOP between result should not be NULL");
    }
    (void)snprintf(between_copy, sizeof(between_copy), "%s", between_json);

    before_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    NOP\n"
        "    mov ebx, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    if (before_json == NULL) {
        return record_failure("Phase 57N NOP before result should not be NULL");
    }
    (void)snprintf(before_copy, sizeof(before_copy), "%s", before_json);

    after_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ecx, 3\n"
        "    NoP\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    if (after_json == NULL) {
        return record_failure("Phase 57N NOP after result should not be NULL");
    }
    (void)snprintf(after_copy, sizeof(after_copy), "%s", after_json);

    only_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    if (only_json == NULL) {
        return record_failure("Phase 57N NOP-only result should not be NULL");
    }
    (void)snprintf(only_copy, sizeof(only_copy), "%s", only_json);

    failures += expect_json_contains(between_copy, "\"ok\":true", "NOP between mutating instructions should execute");
    failures += expect_json_contains(between_copy, "\"instructionCount\":4", "NOP between mutating instructions should count as one instruction");
    failures += expect_json_contains(between_copy, "\"EAX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "NOP between MOV and INC should leave EAX = 2");
    failures += expect_json_contains(between_copy, "\"memoryChanges\":[]", "NOP register-only program should not create memory changes");
    failures += expect_json_not_contains(between_copy, "programConsole", "NOP source-run program should not produce Program Console output");
    failures += expect_json_not_contains(between_copy, "NOP operand form is not accepted", "valid NOP should not emit NOP diagnostics");

    failures += expect_json_contains(before_copy, "\"ok\":true", "NOP before another instruction should execute");
    failures += expect_json_contains(before_copy, "\"instructionCount\":3", "NOP before another instruction should count toward instruction count");
    failures += expect_json_contains(before_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "NOP before MOV should preserve later MOV behavior");

    failures += expect_json_contains(after_copy, "\"ok\":true", "NOP after another instruction should execute");
    failures += expect_json_contains(after_copy, "\"instructionCount\":3", "NOP after another instruction should count toward instruction count");
    failures += expect_json_contains(after_copy, "\"ECX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "NOP after MOV should preserve ECX");

    failures += expect_json_contains(only_copy, "\"ok\":true", "NOP-only program should execute");
    failures += expect_json_contains(only_copy, "\"instructionCount\":2", "NOP-only program should count one instruction");
    failures += expect_json_contains(only_copy, "\"memoryChanges\":[]", "NOP-only program should not create memory changes");
    failures += expect_json_not_contains(only_copy, "programConsole", "NOP-only program should not produce Program Console output");

    return failures;
}

/// Verifies Phase 57O explicit-width NOP encoding operands do not access memory.
///
/// @return Number of failures.
static int test_phase57o_nop_encoding_operand_source_run(void) {
    const char *invalid_address_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    nop DWORD PTR [eax]\n"
        "    mov ebx, 42\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    char invalid_address_copy[4096];
    const char *const_json = NULL;
    char const_copy[4096];
    const char *uninitialized_json = NULL;
    char uninitialized_copy[4096];
    const char *register_word_json = NULL;
    char register_word_copy[4096];
    int failures = 0;

    if (invalid_address_json == NULL) {
        return record_failure("Phase 57O invalid-address NOP result should not be NULL");
    }
    (void)snprintf(invalid_address_copy, sizeof(invalid_address_copy), "%s", invalid_address_json);

    const_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    nop DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    if (const_json == NULL) {
        return record_failure("Phase 57O .CONST-address NOP result should not be NULL");
    }
    (void)snprintf(const_copy, sizeof(const_copy), "%s", const_json);

    uninitialized_json = masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "buf DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET buf\n"
        "    nop DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    if (uninitialized_json == NULL) {
        return record_failure("Phase 57O .DATA? NOP result should not be NULL");
    }
    (void)snprintf(uninitialized_copy, sizeof(uninitialized_copy), "%s", uninitialized_json);

    register_word_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    nop ax\n"
        "    nop eax\n"
        "    nop WORD PTR [eax + 4]\n"
        "    nop SWORD PTR [eax + 4]\n"
        "    nop SDWORD PTR [eax + 4]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    if (register_word_json == NULL) {
        return record_failure("Phase 57O register/WORD NOP result should not be NULL");
    }
    (void)snprintf(register_word_copy, sizeof(register_word_copy), "%s", register_word_json);

    failures += expect_json_contains(invalid_address_copy, "\"ok\":true", "Phase 57O NOP encoding operand should not read invalid address zero");
    failures += expect_json_contains(invalid_address_copy, "\"instructionCount\":4", "Phase 57O NOP encoding operand should count as one instruction");
    failures += expect_json_contains(invalid_address_copy, "\"EBX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "Phase 57O NOP encoding operand should allow following MOV");
    failures += expect_json_contains(invalid_address_copy, "\"memoryChanges\":[]", "Phase 57O invalid-address NOP should not create memory changes");
    failures += expect_json_not_contains(invalid_address_copy, "invalid-memory", "Phase 57O NOP encoding operand should not emit invalid-memory diagnostics");
    failures += expect_json_not_contains(invalid_address_copy, "programConsole", "Phase 57O NOP encoding operand should not produce Program Console output");

    failures += expect_json_contains(const_copy, "\"ok\":true", "Phase 57O NOP encoding operand over .CONST address should execute");
    failures += expect_json_not_contains(const_copy, "permission-denied", "Phase 57O NOP encoding operand should not emit .CONST permission diagnostics");
    failures += expect_json_not_contains(const_copy, "unsupported-code-memory-access", "Phase 57O NOP encoding operand should not emit .code memory diagnostics");

    failures += expect_json_contains(uninitialized_copy, "\"ok\":true", "Phase 57O NOP encoding operand over .DATA? address should execute");
    failures += expect_json_not_contains(uninitialized_copy, "uninitialized-read", "Phase 57O NOP encoding operand should not emit uninitialized-read diagnostics");
    failures += expect_json_not_contains(uninitialized_copy, "declared-object", "Phase 57O NOP encoding operand should not emit declared-object diagnostics");
    failures += expect_json_not_contains(uninitialized_copy, "section-capacity", "Phase 57O NOP encoding operand should not emit section-capacity diagnostics");
    failures += expect_json_not_contains(uninitialized_copy, "section-image", "Phase 57O NOP encoding operand should not emit section-image diagnostics");

    failures += expect_json_contains(register_word_copy, "\"ok\":true", "Phase 57O register/WORD NOP encoding operands should execute");
    failures += expect_json_contains(register_word_copy, "\"instructionCount\":7", "Phase 57O register/WORD/SWORD/SDWORD NOP encoding operands should count as instructions");
    failures += expect_json_contains(register_word_copy, "\"EAX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "Phase 57O register-form NOP should not mutate EAX");
    failures += expect_json_not_contains(register_word_copy, "invalid-memory", "Phase 57O register/WORD NOP encoding operands should not read invalid address");

    return failures;
}

/// Verifies Phase 57O source-run diagnostics for unsupported NOP operand forms.
///
/// @return Number of failures.
static int test_phase57o_nop_source_run_rejections(void) {
    static const struct {
        const char *source;
        const char *code;
        const char *message_fragment;
    } invalid_cases[] = {
        {".code\nmain PROC\n    nop al\nmain ENDP\nEND main\n", "invalid-operand-size", "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop ah\nmain ENDP\nEND main\n", "invalid-operand-size", "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop 1\nmain ENDP\nEND main\n", "invalid-instruction-operands", "does not accept an immediate"},
        {".code\nmain PROC\n    nop eax, ebx\nmain ENDP\nEND main\n", "invalid-instruction-operands", "at most one operand"},
        {".code\nmain PROC\n    nop [eax]\nmain ENDP\nEND main\n", "ambiguous-memory-width", "must have an explicit size"},
        {".code\nmain PROC\n    nop BYTE PTR [eax]\nmain ENDP\nEND main\n", "invalid-operand-size", "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop SBYTE PTR [eax]\nmain ENDP\nEND main\n", "invalid-operand-size", "no 8-bit encoding-operand form"},
        {".code\nmain PROC\n    nop QWORD PTR [eax]\nmain ENDP\nEND main\n", "invalid-operand-size", "QWORD/SQWORD are not supported"},
        {".code\nmain PROC\n    nop SQWORD PTR [eax]\nmain ENDP\nEND main\n", "invalid-operand-size", "QWORD/SQWORD are not supported"}
    };
    int failures = 0;
    size_t index = 0U;

    for (index = 0U; index < sizeof(invalid_cases) / sizeof(invalid_cases[0]); index += 1U) {
        const char *json = masm32_sim_wasm_run_source_json(invalid_cases[index].source);
        failures += expect_json_contains(json, "\"ok\":false", "Phase 57O invalid NOP form should fail source-run");
        failures += expect_json_contains(json, invalid_cases[index].code, "Phase 57O invalid NOP form should use the expected diagnostic code");
        failures += expect_json_contains(json, invalid_cases[index].message_fragment, "Phase 57O invalid NOP form should describe the specific invalid form");
        failures += expect_json_not_contains(json, "execution-complete", "Phase 57O invalid NOP form should not complete execution");
        failures += expect_json_not_contains(json, "programConsole", "Phase 57O invalid NOP form should not produce Program Console output");
    }

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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 22 response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 22 acceptance program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 22 acceptance program should execute four instructions");
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 26 source-run response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 register-supplied memory-width source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":14", "Phase 26 register-supplied memory-width source should execute thirteen instructions");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 26 source-memory response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 register-supplied source memory-width source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Phase 26 source-memory program should execute five instructions");
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Explicit PTR override source-run response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Explicit WORD PTR symbol/register program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Explicit PTR override program should execute three instructions");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 26 header response should identify current runtime phase");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 26 header source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":2", "Phase 26 header source should execute one instruction");
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(model_copy, sizeof(model_copy), "%s", model_json != NULL ? model_json : "");

    const char *include_json = masm32_sim_wasm_run_source_json(
        "INCLUDE Windows.inc\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
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
        "    ret\n"
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


/// Verifies Phase 57P host include paths produce clear source-run diagnostics.
///
/// @return Number of failures.
static int test_phase57p_host_include_path_source_run_diagnostics(void) {
    int failures = 0;
    char masm32_copy[2048];
    char windows_copy[2048];
    char multi_copy[4096];
    char outside_context_copy[2048];
    const char *masm32_json = masm32_sim_wasm_run_source_json(
        "include \\masm32\\include\\masm32.inc\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(masm32_copy, sizeof(masm32_copy), "%s", masm32_json != NULL ? masm32_json : "");

    const char *windows_json = masm32_sim_wasm_run_source_json(
        "include C:\\masm32\\include\\kernel32.inc\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(windows_copy, sizeof(windows_copy), "%s", windows_json != NULL ? windows_json : "");

    const char *multi_json = masm32_sim_wasm_run_source_json(
        "include ..\\include\\file.inc\n"
        "include \\masm32\\include\\kernel32.inc\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(multi_copy, sizeof(multi_copy), "%s", multi_json != NULL ? multi_json : "");

    const char *outside_context_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, \\masm32\\include\\kernel32.inc\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(outside_context_copy, sizeof(outside_context_copy), "%s", outside_context_json != NULL ? outside_context_json : "");

    failures += expect_json_contains(masm32_copy, "\"ok\":false", "Phase 57P MASM32 SDK include path should fail before execution");
    failures += expect_json_contains(masm32_copy, "unsupported-masm32-library-include", "Phase 57P MASM32 SDK include path should expose specific diagnostic code");
    failures += expect_json_contains(masm32_copy, "local MASM32 SDK", "Phase 57P MASM32 SDK include diagnostic should explain SDK boundary");
    failures += expect_json_contains(masm32_copy, "\"column\":9", "Phase 57P MASM32 SDK include diagnostic should point at path tail");
    failures += expect_json_contains(masm32_copy, "\"byteOffset\":8", "Phase 57P MASM32 SDK include diagnostic should preserve byte offset");
    failures += expect_json_not_contains(masm32_copy, "unexpected-character", "Phase 57P MASM32 SDK include should suppress repeated lexer path diagnostics");
    failures += expect_json_not_contains(masm32_copy, "execution-complete", "Phase 57P MASM32 SDK include should not execute");

    failures += expect_json_contains(windows_copy, "unsupported-windows-api-include", "Phase 57P Windows/API include path should expose specific diagnostic code");
    failures += expect_json_contains(windows_copy, "Windows API execution", "Phase 57P Windows/API include diagnostic should explain WinAPI boundary");
    failures += expect_json_not_contains(windows_copy, "unexpected-character", "Phase 57P Windows/API include should suppress lexer path diagnostics");

    failures += expect_json_contains(multi_copy, "unsupported-host-include-path", "Phase 57P multi include fixture should include generic host path diagnostic");
    failures += expect_json_contains(multi_copy, "unsupported-windows-api-include", "Phase 57P multi include fixture should include Windows/API diagnostic");
    failures += expect_json_contains(multi_copy, "\"line\":2", "Phase 57P multi include fixture should preserve second diagnostic line");
    failures += expect_json_not_contains(multi_copy, "unexpected-character", "Phase 57P multi include fixture should suppress repeated path separator diagnostics");

    failures += expect_json_contains(outside_context_copy, "unexpected-character", "Phase 57P path separators outside INCLUDE context should remain lexer diagnostics");

    return failures;
}

/// Verifies Phase 57Q INCLUDELIB source-run diagnostics and no-execution behavior.
///
/// @return Number of failures.
static int test_phase57q_includelib_source_run_diagnostics(void) {
    int failures = 0;
    char masm32_copy[2048];
    char windows_copy[2048];
    char generic_copy[2048];
    char multi_copy[4096];
    char outside_context_copy[2048];
    const char *masm32_json = masm32_sim_wasm_run_source_json(
        "includelib \\masm32\\lib\\masm32.lib\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(masm32_copy, sizeof(masm32_copy), "%s", masm32_json != NULL ? masm32_json : "");

    const char *windows_json = masm32_sim_wasm_run_source_json(
        "includelib kernel32.lib\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(windows_copy, sizeof(windows_copy), "%s", windows_json != NULL ? windows_json : "");

    const char *generic_json = masm32_sim_wasm_run_source_json(
        "includelib customlib.lib\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(generic_copy, sizeof(generic_copy), "%s", generic_json != NULL ? generic_json : "");

    const char *multi_json = masm32_sim_wasm_run_source_json(
        "includelib customlib.lib\n"
        "includelib C:\\masm32\\lib\\kernel32.lib\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(multi_copy, sizeof(multi_copy), "%s", multi_json != NULL ? multi_json : "");

    const char *outside_context_json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, \\masm32\\lib\\kernel32.lib\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    (void)snprintf(outside_context_copy, sizeof(outside_context_copy), "%s", outside_context_json != NULL ? outside_context_json : "");

    failures += expect_json_contains(masm32_copy, "\"ok\":false", "Phase 57Q MASM32 INCLUDELIB should fail before execution");
    failures += expect_json_contains(masm32_copy, "unsupported-masm32-library", "Phase 57Q MASM32 INCLUDELIB should expose MASM32 library code");
    failures += expect_json_contains(masm32_copy, "external library linking", "Phase 57Q MASM32 INCLUDELIB should explain library boundary");
    failures += expect_json_contains(masm32_copy, "\"column\":12", "Phase 57Q MASM32 INCLUDELIB diagnostic should point at library operand");
    failures += expect_json_contains(masm32_copy, "\"byteOffset\":11", "Phase 57Q MASM32 INCLUDELIB diagnostic should preserve byte offset");
    failures += expect_json_not_contains(masm32_copy, "unexpected-character", "Phase 57Q MASM32 INCLUDELIB should suppress repeated path diagnostics");
    failures += expect_json_not_contains(masm32_copy, "execution-complete", "Phase 57Q MASM32 INCLUDELIB should not execute");
    failures += expect_json_not_contains(masm32_copy, "programConsole", "Phase 57Q INCLUDELIB should not produce Program Console output");

    failures += expect_json_contains(windows_copy, "unsupported-windows-api-library", "Phase 57Q Windows INCLUDELIB should expose Windows/API library code");
    failures += expect_json_contains(windows_copy, "Windows import library", "Phase 57Q Windows INCLUDELIB should explain import-library boundary");
    failures += expect_json_not_contains(windows_copy, "unexpected-character", "Phase 57Q basename INCLUDELIB should suppress lexer path diagnostics");

    failures += expect_json_contains(generic_copy, "unsupported-includelib", "Phase 57Q generic INCLUDELIB should expose generic code");
    failures += expect_json_contains(generic_copy, "does not link objects", "Phase 57Q generic INCLUDELIB should explain linker boundary");

    failures += expect_json_contains(multi_copy, "unsupported-includelib", "Phase 57Q multi INCLUDELIB fixture should include generic diagnostic");
    failures += expect_json_contains(multi_copy, "unsupported-windows-api-library", "Phase 57Q multi INCLUDELIB fixture should include Windows/API diagnostic");
    failures += expect_json_contains(multi_copy, "\"line\":2", "Phase 57Q multi INCLUDELIB fixture should preserve second diagnostic line");
    failures += expect_json_not_contains(multi_copy, "unexpected-character", "Phase 57Q multi INCLUDELIB fixture should suppress repeated path separator diagnostics");

    failures += expect_json_contains(outside_context_copy, "unexpected-character", "Phase 57Q path separators outside directive context should remain lexer diagnostics");

    return failures;
}

/// Verifies .DATA? and .CONST source-run behavior under the Milestone 30 source-run API.
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    const char *direct_write_json = NULL;
    const char *indirect_write_json = NULL;
    const char *offset_write_json = NULL;
    int failures = 0;

    failures += expect_json_contains(acceptance_json, "\"phase\":77", "Phase 30 response should identify current runtime phase");
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
        "    ret\n"
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
        "    ret\n"
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
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(offset_write_json, "\"ok\":false", "calculated .CONST byte write should fail");
    failures += expect_json_contains(offset_write_json, "permission-denied", "calculated .CONST byte write should fail through checked memory permissions");

    return failures;
}

/// Verifies Milestone 30 source-run behavior for comma-separated DUP initializer lists.
///
/// @return Number of failures.
static int test_phase30_dup_initializer_list_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "msg BYTE 2 DUP(\"Hi\", 0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, LENGTHOF msg\n"
        "    mov ebx, SIZEOF msg\n"
        "    mov cl, msg[0]\n"
        "    mov dl, msg[3]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "DUP initializer list source should report current runtime/source-run MASM behavior phase metadata");
    failures += expect_json_contains(json, "\"ok\":true", "DUP initializer list source should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000006h\",\"unsigned\":6}", "DUP initializer list source should set EAX to LENGTHOF msg");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000006h\",\"unsigned\":6}", "DUP initializer list source should set EBX to SIZEOF msg");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000048h\",\"unsigned\":72}", "DUP initializer list source should load first H into CL");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000048h\",\"unsigned\":72}", "DUP initializer list source should load repeated H into DL");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "DUP initializer list source should complete execution");

    return failures;
}

/// Verifies Milestone 30 DUP repeat-count diagnostics describe count and expansion constraints.
///
/// @return Number of failures.
static int test_phase30_dup_repeat_count_diagnostic_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "bad BYTE -1 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "negative DUP count source should fail");
    failures += expect_json_contains(json, "\"code\":\"invalid-dup\"", "negative DUP count source should report invalid-dup");
    failures += expect_json_contains(json, "1 or greater", "negative DUP count source should state the lower bound");
    failures += expect_json_contains(json, "active declaration image capacity", "negative DUP count source should mention configured capacity");
    failures += expect_json_contains(json, "1,048,576 bytes", "negative DUP count source should show browser/source-run capacity");
    failures += expect_json_not_contains(json, "UINT32_MAX", "negative DUP count source should not expose implementation labels");

    return failures;
}


/// Verifies Milestone 30 large syntactically valid DUP counts fail with capacity wording.
///
/// @return Number of failures.
static int test_phase30_large_dup_count_capacity_diagnostic_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "bad BYTE 4294967295 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "huge DUP count source should fail");
    failures += expect_json_contains(json, "\"code\":\"data-capacity-exceeded\"", "huge DUP count source should report data capacity");
    failures += expect_json_contains(json, "DUP expansion requires 4,294,967,295 bytes", "huge DUP count diagnostic should show required expansion size");
    failures += expect_json_contains(json, "only 1,048,576 bytes", "huge DUP count diagnostic should show source-run capacity");
    failures += expect_json_not_contains(json, "invalid-dup", "huge in-range numeric count should not report invalid-dup");

    return failures;
}

/// Verifies Phase 61D token-capacity failures surface as structured source-run diagnostics.
///
/// @return Number of failures.
static int test_phase61d_token_capacity_diagnostic_source_run_program(void) {
    char source[4096];
    size_t length = 0U;
    int failures = 0;
    int index = 0;
    const char *json = NULL;

    failures += append_source_fragment(source, sizeof(source), &length, ".code\nmain PROC\n");
    for (index = 0; index < 200; ++index) {
        failures += append_source_fragment(source, sizeof(source), &length, "    mov eax, 1\n");
    }
    failures += append_source_fragment(source, sizeof(source), &length, "main ENDP\nEND main\n");
    if (failures != 0) {
        return failures;
    }

    json = masm32_sim_wasm_run_source_json(source);
    failures += expect_json_contains(json, "\"ok\":false", "token-capacity source should fail");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "token-capacity source should report parse-error status");
    failures += expect_json_contains(json, "\"instructionCount\":0", "token-capacity source should not report lowered instructions");
    failures += expect_json_contains(json, "\"executedInstructionCount\":0", "token-capacity source should not execute instructions");
    failures += expect_json_contains(json, "\"kind\":\"assembly-error\"", "token-capacity source should report assembly-error");
    failures += expect_json_contains(json, "\"code\":\"token-capacity-exceeded\"", "token-capacity source should report stable capacity code");
    failures += expect_json_contains(json, "token buffer capacity exceeded", "token-capacity source should name the exhausted capacity");
    failures += expect_json_contains(json, "\"line\":104", "token-capacity source should preserve source line");
    failures += expect_json_contains(json, "\"column\":12", "token-capacity source should preserve source column");
    failures += expect_json_contains(json, "\"byteOffset\":1542", "token-capacity source should preserve byte offset");
    failures += expect_json_contains(json, "\"spanLength\":1", "token-capacity source should preserve span length");
    failures += expect_json_not_contains(json, "execution-complete", "token-capacity source should not complete execution");
    failures += expect_json_not_contains(json, "instruction-limit-exceeded", "token-capacity source should not be reported as runtime instruction limit");
    failures += expect_json_not_contains(json, "programConsole", "token-capacity source should not produce Program Console output");

    return failures;
}

/// Verifies Phase 61D source-text capacity failures are structured and pre-runtime.
///
/// @return Number of failures.
static int test_phase61d_source_text_capacity_diagnostic_source_run_program(void) {
    char source[9000];
    size_t length = 0U;
    size_t index = 0U;
    int failures = 0;
    const char *json = NULL;

    failures += append_source_fragment(source, sizeof(source), &length, ".code\nmain PROC\n    mov eax, 1 ; ");
    for (index = 0U; index < 8300U; ++index) {
        failures += append_source_fragment(source, sizeof(source), &length, "x");
    }
    failures += append_source_fragment(source, sizeof(source), &length, "\nmain ENDP\nEND main\n");
    if (failures != 0) {
        return failures;
    }

    json = masm32_sim_wasm_run_source_json(source);
    failures += expect_json_contains(json, "\"ok\":false", "source-text capacity source should fail");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "source-text capacity source should report parse-error status");
    failures += expect_json_contains(json, "\"instructionCount\":0", "source-text capacity source should not report lowered instructions");
    failures += expect_json_contains(json, "\"executedInstructionCount\":0", "source-text capacity source should not execute instructions");
    failures += expect_json_contains(json, "\"code\":\"source-text-capacity-exceeded\"", "source-text capacity source should report stable capacity code");
    failures += expect_json_contains(json, "Instruction source-text storage capacity exceeded.", "source-text capacity diagnostic should name exhausted storage");
    failures += expect_json_contains(json, "\"line\":3", "source-text capacity source should preserve source line");
    failures += expect_json_contains(json, "\"column\":5", "source-text capacity source should preserve source column");
    failures += expect_json_contains(json, "\"byteOffset\":20", "source-text capacity source should preserve byte offset");
    failures += expect_json_contains(json, "\"spanLength\":3", "source-text capacity source should preserve span length");
    failures += expect_json_not_contains(json, "execution-complete", "source-text capacity source should not complete execution");
    failures += expect_json_not_contains(json, "instruction-limit-exceeded", "source-text capacity source should not be reported as runtime instruction limit");
    failures += expect_json_not_contains(json, "programConsole", "source-text capacity source should not produce Program Console output");

    return failures;
}

/// Verifies Phase 61D code-label capacity failures are structured source-run diagnostics.
///
/// @return Number of failures.
static int test_phase61d_code_label_capacity_diagnostic_source_run_program(void) {
    char source[2048];
    char line[32];
    size_t length = 0U;
    int failures = 0;
    int index = 0;
    const char *json = NULL;

    failures += append_source_fragment(source, sizeof(source), &length, ".code\nmain PROC\n");
    for (index = 0; index < 130; ++index) {
        (void)snprintf(line, sizeof(line), "L%d:\n", index);
        failures += append_source_fragment(source, sizeof(source), &length, line);
    }
    failures += append_source_fragment(source, sizeof(source), &length, "main ENDP\nEND main\n");
    if (failures != 0) {
        return failures;
    }

    json = masm32_sim_wasm_run_source_json(source);
    failures += expect_json_contains(json, "\"ok\":false", "code-label capacity source should fail");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "code-label capacity source should report parse-error status");
    failures += expect_json_contains(json, "\"instructionCount\":0", "code-label capacity source should not report lowered instructions");
    failures += expect_json_contains(json, "\"executedInstructionCount\":0", "code-label capacity source should not execute instructions");
    failures += expect_json_contains(json, "\"code\":\"code-label-capacity-exceeded\"", "code-label capacity source should report stable capacity code");
    failures += expect_json_contains(json, "Code label capacity exceeded.", "code-label capacity diagnostic should name exhausted capacity");
    failures += expect_json_contains(json, "\"line\":130", "code-label capacity source should preserve first failing source line");
    failures += expect_json_contains(json, "\"column\":1", "code-label capacity source should preserve source column");
    failures += expect_json_contains(json, "\"byteOffset\":668", "code-label capacity source should preserve byte offset");
    failures += expect_json_contains(json, "\"spanLength\":4", "code-label capacity source should preserve span length");
    failures += expect_json_not_contains(json, "execution-complete", "code-label capacity source should not complete execution");
    failures += expect_json_not_contains(json, "instruction-limit-exceeded", "code-label capacity source should not be reported as runtime instruction limit");
    failures += expect_json_not_contains(json, "programConsole", "code-label capacity source should not produce Program Console output");

    return failures;
}

/// Verifies that the static JSON buffer is overwritten by subsequent calls.
///
/// @return Number of failures.
static int test_subsequent_calls_return_latest_result(void) {
    char first_copy[TEST_JSON_COPY_CAPACITY];
    const char *first = masm32_sim_wasm_run_source_json(
        ".code\nmain PROC\n    mov eax, 1\n    ret\nmain ENDP\nEND main\n"
    );
    const char *second = NULL;
    int failures = 0;

    if (first == NULL) {
        return record_failure("first source-run result should not be NULL");
    }
    (void)snprintf(first_copy, sizeof(first_copy), "%s", first);

    second = masm32_sim_wasm_run_source_json(
        ".code\nmain PROC\n    mov eax, 2\n    ret\nmain ENDP\nEND main\n"
    );

    failures += expect_json_contains(first_copy, "\"unsigned\":1", "first copied result should retain EAX = 1");
    failures += expect_json_contains(second, "\"unsigned\":2", "second result should expose latest EAX = 2");

    return failures;
}



/// Verifies Phase 35A default and explicit CASEMAP:ALL folded symbol lookup.
///
/// @return Number of failures.
static int test_phase35a_casemap_all_source_run_programs(void) {
    int failures = 0;
    char default_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(default_json, sizeof(default_json), masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "buf DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET bUF\n"
        "    mov DWORD PTR [eax], 77\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char explicit_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(explicit_json, sizeof(explicit_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:ALL\n"
        ".DATA?\n"
        "buf DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET bUF\n"
        "    mov DWORD PTR [eax], 77\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(default_json, "\"ok\":true", "Default CASEMAP:ALL source-run should execute folded data-symbol lookup");
    failures += expect_json_contains(default_json, "\"EBX\":{\"hex\":\"0000004Dh\",\"unsigned\":77}", "Default folded lookup should read back written value");
    failures += expect_json_contains(default_json, "\"code\":\"execution-complete\"", "Default folded lookup should complete execution");
    failures += expect_json_not_contains(default_json, "unknown-symbol", "Default folded lookup should not report unknown-symbol");

    failures += expect_json_contains(explicit_json, "\"ok\":true", "Explicit CASEMAP:ALL source-run should execute folded data-symbol lookup");
    failures += expect_json_contains(explicit_json, "\"EBX\":{\"hex\":\"0000004Dh\",\"unsigned\":77}", "Explicit CASEMAP:ALL folded lookup should read back written value");
    failures += expect_json_not_contains(explicit_json, "casemap-policy-changed", "First explicit CASEMAP:ALL should not warn");

    return failures;
}

/// Verifies Phase 35A CASEMAP:NONE exact-case lookup behavior for data sections and operators.
///
/// @return Number of failures.
static int test_phase35a_casemap_none_source_run_programs(void) {
    int failures = 0;
    char unknown_data_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(unknown_data_json, sizeof(unknown_data_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "Buffer DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET buffer\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char distinct_data_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(distinct_data_json, sizeof(distinct_data_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "buf DWORD 1\n"
        "bUF DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, buf\n"
        "    mov ebx, bUF\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char operator_exact_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(operator_exact_json, sizeof(operator_exact_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "Nums DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE Nums\n"
        "    mov ebx, LENGTHOF Nums\n"
        "    mov ecx, SIZEOF Nums\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char operator_unknown_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(operator_unknown_json, sizeof(operator_unknown_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "Nums DWORD 4 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, TYPE nums\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char const_unknown_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(const_unknown_json, sizeof(const_unknown_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".CONST\n"
        "Limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(unknown_data_json, "\"ok\":false", "CASEMAP:NONE should reject mismatched data symbol casing");
    failures += expect_json_contains(unknown_data_json, "unknown-symbol", "CASEMAP:NONE mismatched data symbol should report unknown-symbol");
    failures += expect_json_contains(unknown_data_json, "OFFSET references an unknown data symbol", "CASEMAP:NONE OFFSET diagnostic should be specific");

    failures += expect_json_contains(distinct_data_json, "\"ok\":true", "CASEMAP:NONE should allow case-distinct data symbols");
    failures += expect_json_contains(distinct_data_json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "CASEMAP:NONE exact lower-case symbol should load first value");
    failures += expect_json_contains(distinct_data_json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "CASEMAP:NONE exact mixed-case symbol should load second value");

    failures += expect_json_contains(operator_exact_json, "\"ok\":true", "CASEMAP:NONE exact operator symbol references should execute");
    failures += expect_json_contains(operator_exact_json, "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "CASEMAP:NONE exact TYPE should succeed");
    failures += expect_json_contains(operator_exact_json, "\"EBX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "CASEMAP:NONE exact LENGTHOF should succeed");
    failures += expect_json_contains(operator_exact_json, "\"ECX\":{\"hex\":\"00000010h\",\"unsigned\":16}", "CASEMAP:NONE exact SIZEOF should succeed");

    failures += expect_json_contains(operator_unknown_json, "\"ok\":false", "CASEMAP:NONE TYPE with wrong symbol case should fail");
    failures += expect_json_contains(operator_unknown_json, "unknown-symbol", "CASEMAP:NONE TYPE with wrong symbol case should report unknown-symbol");

    failures += expect_json_contains(const_unknown_json, "\"ok\":false", "CASEMAP:NONE should apply to .CONST symbols");
    failures += expect_json_contains(const_unknown_json, "unknown-symbol", "CASEMAP:NONE .CONST mismatch should report unknown-symbol");

    return failures;
}

/// Verifies Phase 35A CASEMAP policy for numeric equates.
///
/// @return Number of failures.
static int test_phase35a_casemap_equate_source_run_programs(void) {
    int failures = 0;
    char default_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(default_json, sizeof(default_json), masm32_sim_wasm_run_source_json(
        "COUNT = 5\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, count\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char duplicate_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(duplicate_json, sizeof(duplicate_json), masm32_sim_wasm_run_source_json(
        "COUNT = 5\n"
        "count = 6\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char none_unknown_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(none_unknown_json, sizeof(none_unknown_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        "COUNT = 5\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, count\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char none_distinct_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(none_distinct_json, sizeof(none_distinct_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        "COUNT = 5\n"
        "count = 6\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, COUNT\n"
        "    mov ebx, count\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char ambiguous_equate_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(ambiguous_equate_json, sizeof(ambiguous_equate_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        "COUNT = 5\n"
        "count = 6\n"
        "OPTION CASEMAP:ALL\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, COUNT\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(default_json, "\"ok\":true", "Default equate lookup should be case-insensitive");
    failures += expect_json_contains(default_json, "\"EAX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "Default folded equate lookup should use COUNT value");

    failures += expect_json_contains(duplicate_json, "\"ok\":false", "Default folded equate duplicate should fail");
    failures += expect_json_contains(duplicate_json, "duplicate-symbol", "Default folded equate duplicate should use duplicate-symbol");

    failures += expect_json_contains(none_unknown_json, "\"ok\":false", "CASEMAP:NONE equate mismatched case should fail");
    failures += expect_json_contains(none_unknown_json, "unknown-symbol", "CASEMAP:NONE equate mismatched case should report unknown-symbol");

    failures += expect_json_contains(none_distinct_json, "\"ok\":true", "CASEMAP:NONE should allow case-distinct equates");
    failures += expect_json_contains(none_distinct_json, "\"EAX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "CASEMAP:NONE exact COUNT should load first equate value");
    failures += expect_json_contains(none_distinct_json, "\"EBX\":{\"hex\":\"00000006h\",\"unsigned\":6}", "CASEMAP:NONE exact count should load second equate value");

    failures += expect_json_contains(ambiguous_equate_json, "\"ok\":false", "CASEMAP:ALL folded lookup over exact-case duplicate equates should fail");
    failures += expect_json_contains(ambiguous_equate_json, "ambiguous-symbol", "Ambiguous folded equate lookup should use ambiguous-symbol");
    failures += expect_json_contains(ambiguous_equate_json, "make the equate names distinct beyond case", "Ambiguous folded equate diagnostic should describe a clear fix");

    return failures;
}

/// Verifies Phase 35A CASEMAP policy changes, unsupported modes, and ambiguity diagnostics.
///
/// @return Number of failures.
static int test_phase35a_casemap_diagnostic_source_run_programs(void) {
    int failures = 0;
    char none_to_all_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(none_to_all_json, sizeof(none_to_all_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        "OPTION CASEMAP:ALL\n"
        ".data\n"
        "buf DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, bUF\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char all_to_none_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(all_to_none_json, sizeof(all_to_none_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:ALL\n"
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "buf DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, bUF\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char same_policy_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(same_policy_json, sizeof(same_policy_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:ALL\n"
        "OPTION CASEMAP:ALL\n"
        ".data\n"
        "buf DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, bUF\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char ambiguous_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(ambiguous_json, sizeof(ambiguous_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "buf DWORD 1\n"
        "bUF DWORD 2\n"
        "OPTION CASEMAP:ALL\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, buf\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char duplicate_then_unknown_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(duplicate_then_unknown_json, sizeof(duplicate_then_unknown_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:ALL\n"
        ".DATA?\n"
        "buf DWORD ?\n"
        "bUF DWORD ?\n"
        "OPTION CASEMAP:NONE\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET bUF\n"
        "    mov DWORD PTR [eax], 77\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char notpublic_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(notpublic_json, sizeof(notpublic_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NOTPUBLIC\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char invalid_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(invalid_json, sizeof(invalid_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:LOWER\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    char default_proc_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(default_proc_json, sizeof(default_proc_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "Main PROC\n"
        "    ret\n"
        "mAIN ENDP\n"
        "END main\n"
    ));
    char none_proc_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(none_proc_json, sizeof(none_proc_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".code\n"
        "Main PROC\n"
        "Main ENDP\n"
        "END main\n"
    ));
    char none_endp_json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(none_endp_json, sizeof(none_endp_json), masm32_sim_wasm_run_source_json(
        "OPTION CASEMAP:NONE\n"
        ".code\n"
        "Main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END Main\n"
    ));

    failures += expect_json_contains(none_to_all_json, "\"ok\":true", "CASEMAP:NONE to CASEMAP:ALL warning-only program should execute");
    failures += expect_json_contains(none_to_all_json, "\"kind\":\"assembly-warning\"", "CASEMAP policy change should be surfaced as warning");
    failures += expect_json_contains(none_to_all_json, "casemap-policy-changed", "CASEMAP policy change should use stable warning code");
    failures += expect_json_contains(none_to_all_json, "\"code\":\"execution-complete\"", "CASEMAP warning-only program should still complete execution");
    failures += expect_json_contains(none_to_all_json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "CASEMAP:NONE to ALL should apply folded lookup afterward");

    failures += expect_json_contains(all_to_none_json, "\"ok\":false", "CASEMAP:ALL to NONE then mismatched symbol should not execute");
    failures += expect_json_contains(all_to_none_json, "casemap-policy-changed", "CASEMAP:ALL to NONE should warn");
    failures += expect_json_contains(all_to_none_json, "unknown-symbol", "CASEMAP:ALL to NONE should apply exact-case lookup afterward");

    failures += expect_json_contains(same_policy_json, "\"ok\":true", "Repeated same CASEMAP value should execute");
    failures += expect_json_not_contains(same_policy_json, "casemap-policy-changed", "Repeated same CASEMAP value should not warn");

    failures += expect_json_contains(ambiguous_json, "\"ok\":false", "CASEMAP:ALL folded lookup over exact-case duplicates should fail");
    failures += expect_json_contains(ambiguous_json, "ambiguous-symbol", "Ambiguous folded lookup should use ambiguous-symbol");
    failures += expect_json_contains(ambiguous_json, "casemap-policy-changed", "Ambiguous folded lookup fixture should also preserve policy warning");

    failures += expect_json_contains(duplicate_then_unknown_json, "duplicate-symbol", "Duplicate under CASEMAP:ALL should be diagnosed");
    failures += expect_json_contains(duplicate_then_unknown_json, "unknown-symbol", "Rejected duplicate spelling should not be inserted for later CASEMAP:NONE lookup");
    failures += expect_json_contains(duplicate_then_unknown_json, "casemap-policy-changed", "Duplicate-then-unknown fixture should preserve policy warning");

    failures += expect_json_contains(notpublic_json, "\"ok\":false", "CASEMAP:NOTPUBLIC should fail before execution");
    failures += expect_json_contains(notpublic_json, "unsupported-option", "CASEMAP:NOTPUBLIC should use unsupported-option");
    failures += expect_json_contains(notpublic_json, "public/external linkage", "CASEMAP:NOTPUBLIC diagnostic should explain linkage dependency");

    failures += expect_json_contains(invalid_json, "\"ok\":false", "Invalid CASEMAP value should fail before execution");
    failures += expect_json_contains(invalid_json, "invalid-option-value", "Invalid CASEMAP value should use invalid-option-value");
    failures += expect_json_contains(invalid_json, "Supported CASEMAP values: ALL, NONE", "Invalid CASEMAP diagnostic should list supported values");

    failures += expect_json_contains(default_proc_json, "\"ok\":true", "Default CASEMAP:ALL should apply folded procedure ENDP and END target matching");
    failures += expect_json_contains(default_proc_json, "execution-complete", "Default folded procedure ENDP and END targets should execute");
    failures += expect_json_contains(none_proc_json, "\"ok\":false", "CASEMAP:NONE should require exact procedure END target spelling");
    failures += expect_json_contains(none_proc_json, "invalid-end-target", "CASEMAP:NONE mismatched procedure END target should use existing diagnostic");
    failures += expect_json_contains(none_proc_json, "active CASEMAP policy", "CASEMAP:NONE procedure END mismatch diagnostic should explain policy involvement");
    failures += expect_json_contains(none_endp_json, "\"ok\":false", "CASEMAP:NONE should require exact procedure ENDP name spelling");
    failures += expect_json_contains(none_endp_json, "proc-end-mismatch", "CASEMAP:NONE mismatched procedure ENDP name should use Phase 75 diagnostic");
    failures += expect_json_contains(none_endp_json, "ENDP procedure name", "CASEMAP:NONE ENDP mismatch diagnostic should explain procedure-name mismatch");

    return failures;
}

/// Verifies Phase 32 preserves fixed-layout source-run behavior from Milestones 27-30.
///
/// @return Number of failures.
static int test_phase32_fixed_layout_source_run_regression_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "COUNT = 2\n"
        ".DATA?\n"
        "buf BYTE COUNT DUP(?)\n"
        ".data\n"
        "arr DWORD COUNT DUP(COUNT DUP(0))\n"
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF buf\n"
        "    mov ebx, SIZEOF arr\n"
        "    mov ecx, limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 32 fixed-layout regression program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000002h\",\"unsigned\":2}", ".DATA? SIZEOF should remain two bytes");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000010h\",\"unsigned\":16}", "nested DUP SIZEOF should remain sixteen bytes");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"0000000Ah\",\"unsigned\":10}", ".CONST read should remain available");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 32 regression should complete normally");

    return failures;
}

/// Test entry point.
///

/// Verifies automatic layout can execute representative data/const programs.
///
/// @return Number of failures.
static int test_phase33_automatic_layout_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
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
        "    mov x, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "automatic layout source should execute");
    failures += expect_json_contains(json, "\"status\":\"ok\"", "automatic layout source should report ok status");
    failures += expect_json_contains(json, "\"instructionCount\":4", "automatic layout source should execute three instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000010h\",\"unsigned\":16}", "automatic layout should preserve .DATA? metadata");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000000Ah\",\"unsigned\":10}", "automatic layout should load .CONST bytes");
    failures += expect_json_contains(json, "\"symbol\":\"x\"", "automatic layout should report writable data memory changes");
    failures += expect_json_contains_once(json, "\"symbol\":\"x\"", "automatic layout should report the logical memory write once");

    return failures;
}

/// Verifies automatic layout preserves read-only `.CONST` runtime protection.
///
/// @return Number of failures.
static int test_phase33_automatic_layout_const_write_rejected(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "automatic .CONST write should fail");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "automatic .CONST write should be an execution error");
    failures += expect_json_contains(json, "\"kind\":\"runtime-error\"", "automatic .CONST write should return runtime diagnostic");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "automatic .CONST write should preserve permission diagnostic");
    failures += expect_json_not_contains(json, "\"symbol\":\"limit\"", "failed automatic .CONST write should not produce memory change row");

    return failures;
}

/// Verifies automatic layout does not grow regions on demand after program load.
///
/// @return Number of failures.
static int test_phase33_automatic_layout_invalid_access_does_not_grow(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".data\n"
        "x BYTE 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4096\n"
        "    mov bl, BYTE PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "automatic out-of-range access should fail");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "automatic out-of-range access should be execution error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "automatic out-of-range access should preserve invalid-address diagnostic");
    failures += expect_json_contains(json, "00501000h", "automatic out-of-range access should target one byte past aligned data region");

    return failures;
}

/// Verifies automatic layout returns source-mapped resource-limit JSON.
///
/// @return Number of failures.
static int test_phase33_automatic_layout_resource_limit_json(void) {
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    const char *json = NULL;
    int failures = 0;

    base_policy.regions[VM_LAYOUT_REGION_DATA].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = 4096U;
    json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".data\n"
        "big BYTE 4097 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &base_policy
    );

    failures += expect_json_contains(json, "\"ok\":false", "automatic resource-limit result should fail");
    failures += expect_json_contains(json, "\"status\":\"resource-limit-exceeded\"", "automatic resource-limit result should report resource status");
    failures += expect_json_contains(json, "\"kind\":\"resource-limit-error\"", "automatic resource-limit result should use resource-limit message kind");
    failures += expect_json_contains(json, "\"code\":\"resource-limit-exceeded\"", "automatic resource-limit result should use stable code");
    failures += expect_json_contains(json, "Automatic layout requested .data/.DATA? region size 8192 bytes, exceeding configured limit 4096 bytes.", "automatic resource-limit message should report aligned request and limit");
    failures += expect_json_contains(json, "\"line\":2", "automatic resource-limit message should point at oversized declaration line");
    failures += expect_json_contains(json, "\"column\":1", "automatic resource-limit message should point at oversized declaration column");
    failures += expect_json_contains(json, "\"byteOffset\":6", "automatic resource-limit message should include declaration byte offset");
    failures += expect_json_contains(json, "\"spanLength\":3", "automatic resource-limit message should include declaration span length");
    failures += expect_json_not_contains(json, "execution-complete", "automatic resource-limit result should not report execution complete");

    return failures;
}


/// Verifies parsed `.stack size` metadata affects automatic stack capacity only.
///
/// @return Number of failures.
static int test_phase34_automatic_layout_uses_stack_size_metadata(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".stack 4096\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 008F0000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "automatic .stack 4096 should shrink stack capacity relative to default");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "out-of-range stack write should be an execution error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "out-of-range stack write should report invalid address");
    failures += expect_json_contains(json, "008F0000h", "invalid stack access should mention the address below the requested stack region");
    failures += expect_json_not_contains(json, "execution-complete", "failed stack access should not report execution complete");

    return failures;
}

/// Verifies `.stack` without an operand keeps the documented automatic default stack size.
///
/// @return Number of failures.
static int test_phase34_automatic_layout_stack_without_operand_uses_default(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".stack\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 008F0000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "automatic .stack without operand should use default stack capacity");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000007Bh\",\"unsigned\":123}", "default automatic stack should allow access at default stack base");
    failures += expect_json_contains(json, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184}", "Phase 68A should initialize ESP from the default automatic stack region limit");
    failures += expect_json_contains(json, "execution-complete", "default stack metadata source should execute successfully");

    return failures;
}

/// Verifies `.stack` can use prior constant-expression support in automatic layout.
///
/// @return Number of failures.
static int test_phase34_automatic_layout_stack_expression_metadata(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        "COUNT = 4096\n"
        "EXTRA EQU 4096\n"
        ".stack COUNT + EXTRA\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 008FDFFCh\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "automatic .stack expression should affect stack capacity");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "address below expression-sized stack should fail");
    failures += expect_json_contains(json, "008FDFFCh", "invalid stack expression access should mention the failing address");

    return failures;
}

/// Verifies excessive parsed `.stack` sizes return source-mapped resource-limit diagnostics.
///
/// @return Number of failures.
static int test_phase34_automatic_layout_excessive_stack_size_json(void) {
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    const char *json = NULL;
    int failures = 0;

    base_policy.regions[VM_LAYOUT_REGION_STACK].maximum_size_by_tier[base_policy.safety_tier] = 4096U;
    json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".stack 8192\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &base_policy
    );

    failures += expect_json_contains(json, "\"ok\":false", "automatic oversized .stack should fail");
    failures += expect_json_contains(json, "\"status\":\"resource-limit-exceeded\"", "oversized .stack should report resource status");
    failures += expect_json_contains(json, "Automatic layout requested .stack region size 8192 bytes, exceeding configured limit 4096 bytes.", "oversized .stack message should report aligned request and policy limit");
    failures += expect_json_contains(json, "\"line\":1", "oversized .stack diagnostic should point at .stack line");
    failures += expect_json_contains(json, "\"column\":1", "oversized .stack diagnostic should point at .stack column");
    failures += expect_json_contains(json, "\"byteOffset\":0", "oversized .stack diagnostic should include .stack byte offset");
    failures += expect_json_contains(json, "\"spanLength\":6", "oversized .stack diagnostic should include .stack span length");
    failures += expect_json_not_contains(json, "execution-complete", "oversized .stack should not execute");

    return failures;
}

/// Verifies configured heap size influences automatic heap capacity metadata.
///
/// @return Number of failures.
static int test_phase34_automatic_layout_uses_configured_heap_size(void) {
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    const char *json = NULL;
    int failures = 0;

    base_policy.heap_size_request = 4096U;
    json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".code\n"
        "main PROC\n"
        "    mov eax, 00701000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &base_policy
    );

    failures += expect_json_contains(json, "\"ok\":false", "configured automatic heap size should bound heap capacity");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "one-past configured heap access should be execution error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "one-past configured heap access should report invalid address");
    failures += expect_json_contains(json, "00701000h", "configured heap diagnostic should mention one-past heap address");

    return failures;
}

/// Verifies excessive configured heap requests return structured resource-limit diagnostics.
///
/// @return Number of failures.
static int test_phase34_automatic_layout_excessive_heap_size_json(void) {
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    const char *json = NULL;
    int failures = 0;

    base_policy.heap_size_request = 8192U;
    base_policy.regions[VM_LAYOUT_REGION_HEAP].maximum_size_by_tier[base_policy.safety_tier] = 4096U;
    json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &base_policy
    );

    failures += expect_json_contains(json, "\"ok\":false", "automatic oversized heap request should fail");
    failures += expect_json_contains(json, "\"status\":\"resource-limit-exceeded\"", "oversized heap should report resource status");
    failures += expect_json_contains(json, "Automatic layout requested .heap region size 8192 bytes, exceeding configured limit 4096 bytes.", "oversized heap message should report configured request and policy limit");
    failures += expect_json_contains(json, "\"kind\":\"resource-limit-error\"", "oversized heap should use resource-limit message kind");
    failures += expect_json_not_contains(json, "execution-complete", "oversized heap should not execute");

    return failures;
}


/// Returns a deterministic randomized layout policy for source-run tests.
///
/// @param seed Seed to install in the returned policy.
/// @return Layout policy configured for seeded randomized tests.
static VmLayoutPolicy make_phase35_seeded_source_run_policy(uint32_t seed) {
    VmLayoutPolicy policy = vm_layout_default_policy();
    policy.has_random_seed = true;
    policy.random_seed = seed;
    return policy;
}

/// Verifies OFFSET-based source remains valid under seeded randomized layout.
///
/// @return Number of failures.
static int test_phase35_seeded_randomized_layout_offset_program_succeeds(void) {
    VmLayoutPolicy policy = make_phase35_seeded_source_run_policy(1U);
    const char *json = masm32_sim_wasm_run_source_json_with_randomized_layout_policy(
        ".data\n"
        "value DWORD 123\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET value\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        VM_LAYOUT_MODE_SEEDED_RANDOMIZED,
        &policy
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "seeded randomized OFFSET program should execute");
    failures += expect_json_contains(json, "\"layout\":{\"mode\":\"seeded-randomized\"", "seeded randomized result should expose layout metadata");
    failures += expect_json_contains(json, "\"seed\":1,\"hasSeed\":true", "seeded randomized result should expose selected seed");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000007Bh\",\"unsigned\":123}", "OFFSET program should load value through relocated symbol address");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "seeded randomized OFFSET program should complete");
    failures += expect_json_not_contains(json, "\"address\":\"00500000h\"", "randomized layout should not report fixed data base for relocated symbol access");

    return failures;
}

/// Verifies hardcoded fixed-layout data addresses are unreliable under randomized layout.
///
/// @return Number of failures.
static int test_phase35_seeded_randomized_layout_hardcoded_data_address_fails(void) {
    VmLayoutPolicy policy = make_phase35_seeded_source_run_policy(1U);
    const char *json = masm32_sim_wasm_run_source_json_with_randomized_layout_policy(
        ".code\n"
        "main PROC\n"
        "    mov eax, 00500000h\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        VM_LAYOUT_MODE_SEEDED_RANDOMIZED,
        &policy
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "hardcoded fixed data address should not be reliable under randomized layout");
    failures += expect_json_contains(json, "\"layout\":{\"mode\":\"seeded-randomized\"", "hardcoded-address failure should still expose randomized layout metadata");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "hardcoded-address program should fail at runtime, not parse time");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "hardcoded fixed data address should produce invalid-address under selected test seed");
    failures += expect_json_not_contains(json, "execution-complete", "hardcoded-address failure should not report execution complete");

    return failures;
}

/// Verifies .CONST permissions remain enforced after randomized relocation.
///
/// @return Number of failures.
static int test_phase35_seeded_randomized_layout_const_write_stays_read_only(void) {
    VmLayoutPolicy policy = make_phase35_seeded_source_run_policy(2U);
    const char *json = masm32_sim_wasm_run_source_json_with_randomized_layout_policy(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        VM_LAYOUT_MODE_SEEDED_RANDOMIZED,
        &policy
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "randomized .CONST computed write should fail");
    failures += expect_json_contains(json, "\"layout\":{\"mode\":\"seeded-randomized\"", "randomized .CONST failure should expose layout metadata");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "randomized .CONST write should fail through memory permissions");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "failed randomized .CONST write should not produce memory-change rows");

    return failures;
}

/// Verifies .DATA? remains writable and zero-filled after randomized relocation.
///
/// @return Number of failures.
static int test_phase35_seeded_randomized_layout_data_question_writable(void) {
    VmLayoutPolicy policy = make_phase35_seeded_source_run_policy(3U);
    const char *json = masm32_sim_wasm_run_source_json_with_randomized_layout_policy(
        ".DATA?\n"
        "buf BYTE 4 DUP(?)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, SIZEOF buf\n"
        "    mov ebx, OFFSET buf\n"
        "    mov BYTE PTR [ebx], 12h\n"
        "    mov ecx, 0\n"
        "    mov cl, BYTE PTR [ebx]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        VM_LAYOUT_MODE_SEEDED_RANDOMIZED,
        &policy
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "randomized .DATA? program should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "randomized .DATA? SIZEOF should remain valid");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000012h\",\"unsigned\":18}", "randomized .DATA? write/read should preserve writable zero-filled storage");
    failures += expect_json_contains(json, "\"symbol\":\"buf\"", "randomized .DATA? memory change should still resolve symbol metadata");
    failures += expect_json_not_contains(json, "\"address\":\"00500000h\"", "randomized .DATA? symbol row should not use fixed data base");

    return failures;
}


/// Verifies randomized source-run reports unavailable placement as a structured layout error.
///
/// @return Number of failures.
static int test_phase35_randomized_layout_unavailable_source_run_json(void) {
    VmLayoutPolicy policy = make_phase35_seeded_source_run_policy(4U);
    const char *json = NULL;
    int failures = 0;

    policy.random_base_min = 0x01000000U;
    policy.random_base_limit = policy.random_base_min + policy.region_alignment;
    json = masm32_sim_wasm_run_source_json_with_randomized_layout_policy(
        ".data\n"
        "value DWORD 123\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET value\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        VM_LAYOUT_MODE_SEEDED_RANDOMIZED,
        &policy
    );

    failures += expect_json_contains(json, "\"ok\":false", "unavailable randomized layout source-run should fail");
    failures += expect_json_contains(json, "\"status\":\"resource-limit-exceeded\"", "unavailable randomized layout should use resource-limit outcome");
    failures += expect_json_contains(json, "\"code\":\"randomization-unavailable\"", "unavailable randomized layout should expose a specific diagnostic code");
    failures += expect_json_contains(json, "Randomized layout could not place", "unavailable randomized layout should explain placement failure");
    failures += expect_json_contains(json, "\"kind\":\"resource-limit-error\"", "unavailable randomized layout should use resource-limit message kind");
    failures += expect_json_not_contains(json, "execution-complete", "unavailable randomized layout should not execute");

    return failures;
}

/// Verifies fresh randomized source-run returns generated seed metadata.
///
/// @return Number of failures.
static int test_phase35_fresh_randomized_layout_returns_seed_metadata(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_randomized_layout_policy(
        ".data\n"
        "value DWORD 77\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET value\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        VM_LAYOUT_MODE_FRESH_RANDOMIZED,
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "fresh randomized OFFSET program should execute");
    failures += expect_json_contains(json, "\"layout\":{\"mode\":\"fresh-randomized\"", "fresh randomized result should expose layout metadata");
    failures += expect_json_contains(json, "\"hasSeed\":true", "fresh randomized result should record generated seed availability");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000004Dh\",\"unsigned\":77}", "fresh randomized source-run should load value through relocated address");

    return failures;
}

/// Verifies Phase 37 default region-only mode does not emit object warnings.
///
/// @return Number of failures.
static int test_phase37_default_region_only_has_no_object_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "var1 DWORD 12345\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET var1\n"
        "    test [eax+40h], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "default region-only mode should execute valid-region gap access");
    failures += expect_json_contains(json, "\"instructionCount\":3", "default region-only mode should execute both instructions");
    failures += expect_json_not_contains(json, "object-bounds-warning", "default source-run should not emit object-bounds warning");

    return failures;
}

/// Verifies Phase 37 allocated-object warning mode warns and continues for data-region gaps.
///
/// @return Number of failures.
static int test_phase37_allocated_object_warning_mode_warns_and_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "var1 DWORD 12345\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET var1\n"
        "    test [eax+40h], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "allocated-object warning mode should continue execution after warning");
    failures += expect_json_contains(json, "\"instructionCount\":3", "allocated-object warning mode should execute both instructions");
    failures += expect_json_contains(json, "\"code\":\"object-bounds-warning\"", "allocated-object warning mode should emit object warning code");
    failures += expect_json_contains(json, "Memory read at 00500040h for 4 bytes is inside a valid region but outside any declared data object.", "gap warning should describe valid-region access outside objects");
    failures += expect_json_contains(json, "\"line\":6", "object warning should point at the instruction line");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "object warning mode should still complete execution");

    return failures;
}

/// Verifies Phase 37 permits access into another declared object without warning.
///
/// @return Number of failures.
static int test_phase37_access_into_another_object_has_no_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "var1 DWORD 12345\n"
        "arr1 DWORD 20 DUP(0ABCDEF12h)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET var1\n"
        "    test [eax+40h], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "access landing wholly inside arr1 should execute");
    failures += expect_json_contains(json, "\"instructionCount\":3", "access landing inside arr1 should execute both instructions");
    failures += expect_json_not_contains(json, "object-bounds-warning", "access wholly inside another object should not warn without provenance mode");

    return failures;
}

/// Verifies Phase 37 warns for an access that starts in an object and ends outside it.
///
/// @return Number of failures.
static int test_phase37_partial_overlap_starting_inside_object_warns(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "a DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET a\n"
        "    test DWORD PTR [eax+2], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "partial object overlap should warn but continue");
    failures += expect_json_contains(json, "object-bounds-warning", "partial object overlap should emit object warning");
    failures += expect_json_contains(json, "starts-in-object", "partial object overlap should include starts-in-object classification");
    failures += expect_json_contains(json, "Memory read range 00500002h..00500005h starts inside a declared data object and extends outside it (starts-in-object).", "partial object overlap warning should describe the boundary escape");
    failures += expect_json_contains(json, "unaligned-memory-access", "partial object overlap remains unaligned and should preserve unaligned warning");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "partial object overlap warning mode should still complete execution");

    return failures;
}

/// Verifies Phase 37 warning mode also classifies successful memory writes.
///
/// @return Number of failures.
static int test_phase37_write_to_region_gap_warns_and_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "var1 DWORD 12345\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET var1\n"
        "    mov DWORD PTR [eax+40h], 77\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "write to valid-region gap should warn but continue");
    failures += expect_json_contains(json, "object-bounds-warning", "write to valid-region gap should emit object warning");
    failures += expect_json_contains(json, "Memory write at 00500040h for 4 bytes is inside a valid region but outside any declared data object.", "gap write warning should identify write access");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "write warning mode should still complete execution");

    return failures;
}

/// Verifies Phase 37 preserves invalid-address runtime errors before object warnings.
///
/// @return Number of failures.
static int test_phase37_invalid_address_error_precedes_object_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    test [eax], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid address should remain a runtime error in object warning mode");
    failures += expect_json_contains(json, "invalid-address", "invalid address should preserve runtime memory diagnostic");
    failures += expect_json_not_contains(json, "object-bounds-warning", "invalid address should not be reclassified as object warning");

    return failures;
}

/// Verifies Phase 37 warns for an access spanning adjacent declared objects.
///
/// @return Number of failures.
static int test_phase37_spanning_adjacent_objects_warns(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "a DWORD 1\n"
        "b DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET a\n"
        "    test DWORD PTR [eax+2], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "spanning adjacent objects should warn but continue");
    failures += expect_json_contains(json, "object-bounds-warning", "spanning adjacent objects should emit object warning");
    failures += expect_json_contains(json, "spans-objects", "spanning warning should include range classification");
    failures += expect_json_contains(json, "unaligned-memory-access", "unaligned cross-object access should still keep unaligned warning");

    return failures;
}

/// Verifies Phase 37 does not warn for unaligned accesses wholly inside one object.
///
/// @return Number of failures.
static int test_phase37_unaligned_inside_object_has_no_object_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "arr BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET arr\n"
        "    test DWORD PTR [eax+1], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "unaligned access wholly inside one object should execute");
    failures += expect_json_contains(json, "unaligned-memory-access", "unaligned access wholly inside object may emit unaligned warning");
    failures += expect_json_not_contains(json, "object-bounds-warning", "unaligned access wholly inside one object should not emit object warning");

    return failures;
}

/// Verifies Phase 37 preserves permission diagnostics before object warnings.
///
/// @return Number of failures.
static int test_phase37_const_permission_error_precedes_object_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", ".CONST write should remain a runtime error in object warning mode");
    failures += expect_json_contains(json, "permission-denied", ".CONST write should preserve permission diagnostic");
    failures += expect_json_not_contains(json, "object-bounds-warning", ".CONST permission error should precede object warning classification");

    return failures;
}



/// Verifies Phase 38 strict mode stops for a valid-region gap outside declared objects.
///
/// @return Number of failures.
static int test_phase38_strict_gap_access_fails(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "var1 DWORD 12345\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET var1\n"
        "    test [eax+40h], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "strict mode should fail valid-region gap access");
    failures += expect_json_contains(json, "\"code\":\"object-bounds-violation\"", "strict mode should emit object-bounds-violation");
    failures += expect_json_contains(json, "Memory read at 00500040h for 4 bytes is inside a valid region but outside any declared data object.", "strict gap diagnostic should describe the escaped access");
    failures += expect_json_contains(json, "\"line\":6", "strict object diagnostic should preserve source line");
    failures += expect_json_not_contains(json, "execution-complete", "strict object diagnostic should stop execution without success message");

    return failures;
}

/// Verifies Phase 38 strict mode allows accesses wholly inside another object.
///
/// @return Number of failures.
static int test_phase38_strict_access_into_another_object_succeeds(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "var1 DWORD 12345\n"
        "arr1 DWORD 20 DUP(0ABCDEF12h)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET var1\n"
        "    test [eax+40h], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "strict mode should allow access that lands inside arr1");
    failures += expect_json_contains(json, "\"instructionCount\":3", "strict mode should execute both accepted instructions");
    failures += expect_json_not_contains(json, "object-bounds-violation", "strict mode should not reject access wholly inside another object");

    return failures;
}

/// Verifies Phase 38 strict mode fails an access that starts inside an object and ends outside it.
///
/// @return Number of failures.
static int test_phase38_strict_partial_overlap_starting_inside_object_fails(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "a DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET a\n"
        "    test DWORD PTR [eax+2], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "strict partial overlap should fail");
    failures += expect_json_contains(json, "object-bounds-violation", "strict partial overlap should emit object violation");
    failures += expect_json_contains(json, "starts-in-object", "strict partial overlap should include starts-in-object classification");
    failures += expect_json_contains(json, "Memory read range 00500002h..00500005h starts inside a declared data object and extends outside it (starts-in-object).", "strict partial overlap should describe the boundary escape");
    failures += expect_json_not_contains(json, "execution-complete", "strict partial overlap should stop execution");

    return failures;
}

/// Verifies Phase 38 strict mode fails an access spanning adjacent objects.
///
/// @return Number of failures.
static int test_phase38_strict_spanning_adjacent_objects_fails(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "a DWORD 1\n"
        "b DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET a\n"
        "    test DWORD PTR [eax+2], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "strict adjacent-object span should fail");
    failures += expect_json_contains(json, "object-bounds-violation", "strict adjacent-object span should emit object violation");
    failures += expect_json_contains(json, "spans-objects", "strict adjacent-object span should include spans-objects classification");
    failures += expect_json_not_contains(json, "execution-complete", "strict adjacent-object span should stop execution");

    return failures;
}

/// Verifies Phase 38 strict mode allows unaligned accesses wholly inside one object.
///
/// @return Number of failures.
static int test_phase38_strict_unaligned_inside_object_succeeds(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "arr BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET arr\n"
        "    test DWORD PTR [eax+1], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "unaligned access wholly inside one object should execute in strict mode");
    failures += expect_json_contains(json, "unaligned-memory-access", "unaligned inside-object access should preserve unaligned warning");
    failures += expect_json_not_contains(json, "object-bounds-violation", "unaligned access wholly inside one object should not be a strict object violation");

    return failures;
}

/// Verifies Phase 38 preserves lower-level invalid-address diagnostics before strict object validation.
///
/// @return Number of failures.
static int test_phase38_strict_invalid_address_precedes_object_violation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    test [eax], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid address should remain fatal in strict mode");
    failures += expect_json_contains(json, "invalid-address", "invalid address should preserve runtime memory diagnostic");
    failures += expect_json_not_contains(json, "object-bounds-violation", "invalid address should not be reclassified as object-bounds violation");

    return failures;
}

/// Verifies Phase 38 preserves .CONST permission diagnostics before strict object validation.
///
/// @return Number of failures.
static int test_phase38_strict_const_permission_error_precedes_object_violation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", ".CONST write should remain fatal in strict mode");
    failures += expect_json_contains(json, "permission-denied", ".CONST write should preserve permission diagnostic");
    failures += expect_json_not_contains(json, "object-bounds-violation", ".CONST permission error should precede strict object validation");

    return failures;
}


/// Verifies explicit region-only mode preserves Phase 39 zero-filled reads without warnings or metadata output.
///
/// @return Number of failures.
static int test_phase39_explicit_region_only_uninitialized_read_has_no_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "explicit region-only uninitialized-origin read should still execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "explicit region-only read from ? storage should return deterministic zero");
    failures += expect_json_not_contains(json, "uninitialized-read", "explicit region-only policy should not emit uninitialized-read diagnostics");
    failures += expect_json_not_contains(json, "uninitializedOrigin", "normal source-run JSON should not expose test-only metadata");

    return failures;
}

/// Verifies Phase 39 metadata distinguishes explicit initializers from ? storage.
///
/// @return Number of failures.
static int test_phase39_initial_uninitialized_origin_metadata(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_uninitialized_metadata(
        ".DATA?\n"
        "buf BYTE 2 DUP(2 DUP(?))\n"
        ".data\n"
        "x DWORD ?\n"
        "y DWORD 123\n"
        "mixed BYTE ?, 1\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "metadata inspection source should execute");
    failures += expect_json_contains(json, "\"uninitializedOrigin\":{\"tracked\":true", "metadata helper should expose test-only tracking object");
    failures += expect_json_contains(json, "\"symbol\":\"buf\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", ".DATA? nested DUP(?) bytes should start uninitialized-origin");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", "scalar DWORD ? should start uninitialized-origin");
    failures += expect_json_contains(json, "\"symbol\":\"y\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "explicit DWORD initializer should start initialized");
    failures += expect_json_contains(json, "\"symbol\":\"mixed\",\"state\":\"tracked\",\"initializedByteCount\":1,\"uninitializedByteCount\":1,\"initializedMask\":\"01\"", "mixed .data initializer should preserve byte-level origin");
    failures += expect_json_not_contains(json, "uninitialized-read", "metadata inspection must not emit read diagnostics");

    return failures;
}

/// Verifies Phase 39 partial and full writes update initialized-byte state.
///
/// @return Number of failures.
static int test_phase39_partial_and_full_writes_mark_initialized_bytes(void) {
    char partial_copy[TEST_JSON_COPY_CAPACITY];
    const char *partial_json = masm32_sim_wasm_run_source_json_with_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov BYTE PTR x, 12h\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    const char *full_json = NULL;
    int failures = 0;

    copy_source_run_json(partial_copy, sizeof(partial_copy), partial_json);
    full_json = masm32_sim_wasm_run_source_json_with_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov x, 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );

    failures += expect_json_contains(partial_copy, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":1,\"uninitializedByteCount\":3,\"initializedMask\":\"1000\"", "BYTE write should initialize only the written byte");
    failures += expect_json_contains(full_json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "DWORD write should initialize all destination bytes");

    return failures;
}

/// Verifies Phase 39 failed writes do not mark bytes initialized.
///
/// @return Number of failures.
static int test_phase39_failed_writes_do_not_mark_initialized(void) {
    char const_copy[TEST_JSON_COPY_CAPACITY];
    const char *const_json = masm32_sim_wasm_run_source_json_with_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
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
    const char *invalid_json = NULL;
    copy_source_run_json(const_copy, sizeof(const_copy), const_json);
    invalid_json = masm32_sim_wasm_run_source_json_with_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    mov DWORD PTR [eax], 20\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(const_copy, "\"ok\":false", ".CONST indirect write should fail");
    failures += expect_json_contains(const_copy, "permission-denied", ".CONST failed write should preserve permission diagnostic");
    failures += expect_json_contains(const_copy, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", ".CONST failed write should not initialize unrelated data bytes");
    failures += expect_json_contains(invalid_json, "\"ok\":false", "invalid-address write should fail");
    failures += expect_json_contains(invalid_json, "invalid-address", "invalid-address write should preserve memory diagnostic");
    failures += expect_json_contains(invalid_json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", "invalid-address failed write should not initialize data bytes");

    return failures;
}

/// Verifies Phase 40 warning mode reports reads from uninitialized-origin storage and continues.
///
/// @return Number of failures.
static int test_phase40_uninitialized_read_warning_mode_warns_and_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "warning mode should continue after uninitialized read");
    failures += expect_json_contains(json, "\"instructionCount\":2", "warning mode should execute the read instruction");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "warning mode should preserve deterministic zero read value");
    failures += expect_json_contains(json, "\"kind\":\"simulator-warning\"", "warning mode should emit a simulator warning");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "warning mode should use uninitialized-read code");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.", "warning message should describe the whole read range and symbol");
    failures += expect_json_contains(json, "\"symbolName\":\"x\"", "warning JSON should include symbolName");
    failures += expect_json_contains(json, "\"accessByteOffset\":0", "warning JSON should include symbol-relative byte offset");
    failures += expect_json_contains(json, "\"accessSizeBytes\":4", "warning JSON should include access width in bytes");
    failures += expect_json_contains(json, "\"line\":5", "warning JSON should preserve source line");
    failures += expect_json_contains(json, "\"sourceLocation\":{\"line\":5,\"column\":14,\"byteOffset\":45,\"spanLength\":1}", "warning JSON should include explicit sourceLocation metadata");
    failures += expect_json_contains(json, "execution-complete", "warning mode should still complete successfully");

    return failures;
}

/// Verifies Phase 40 strict mode stops before reading uninitialized-origin storage.
///
/// @return Number of failures.
static int test_phase40_uninitialized_read_strict_mode_stops(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "strict mode should fail uninitialized read");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "strict mode should report execution error");
    failures += expect_json_contains(json, "\"instructionCount\":0", "strict mode should stop before stepping the read instruction");
    failures += expect_json_contains(json, "\"kind\":\"runtime-error\"", "strict mode should emit runtime error");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "strict mode should use uninitialized-read code");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.", "strict message should describe the whole read range and symbol");
    failures += expect_json_contains(json, "\"symbolName\":\"x\"", "strict JSON should include symbolName");
    failures += expect_json_contains(json, "\"accessStartAddress\":\"00500000h\"", "strict JSON should include range start");
    failures += expect_json_contains(json, "\"sourceLocation\":{\"line\":5,\"column\":14,\"byteOffset\":45,\"spanLength\":1}", "strict JSON should include explicit sourceLocation metadata");
    failures += expect_json_not_contains(json, "execution-complete", "strict mode should not emit completion message");

    return failures;
}

/// Verifies Phase 40 full writes initialize all bytes before a later read.
///
/// @return Number of failures.
static int test_phase40_full_write_suppresses_uninitialized_read_diagnostic(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov x, 123\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "strict mode should allow read after full write");
    failures += expect_json_contains(json, "\"instructionCount\":3", "strict mode should execute full-write and read instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000007Bh\",\"unsigned\":123}", "read after full write should return written value");
    failures += expect_json_not_contains(json, "uninitialized-read", "full write should suppress uninitialized-read diagnostics");

    return failures;
}

/// Verifies Phase 40 partial writes leave the remaining bytes diagnosable.
///
/// @return Number of failures.
static int test_phase40_partial_write_then_multibyte_read_warns(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov BYTE PTR x, 12h\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "warning mode should continue after partial-write read");
    failures += expect_json_contains(json, "\"instructionCount\":3", "partial-write warning source should execute both instructions");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 3 of those bytes still originated from uninitialized storage.", "partial write should diagnose the remaining uninitialized bytes in the whole DWORD read range");
    failures += expect_json_contains(json, "\"uninitializedByteCount\":3", "partial-write diagnostic should report the remaining uninitialized byte count");
    failures += expect_json_contains(json, "\"initializedByteCount\":1", "partial-write diagnostic should report the initialized byte count inside the read range");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000012h\",\"unsigned\":18}", "partial-write read should preserve deterministic zero for unwritten bytes");

    return failures;
}

/// Verifies Phase 40 checks every byte in mixed initialized/uninitialized reads.
///
/// @return Number of failures.
static int test_phase40_mixed_initializer_multibyte_read_warns_for_whole_range(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "mixed BYTE 1, ?, 3, ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR mixed\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "mixed initializer warning source should execute");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from mixed + 0; 2 of those bytes still originated from uninitialized storage.", "mixed initializer read should diagnose whole read range");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00030001h\",\"unsigned\":196609}", "mixed initializer read should preserve initialized bytes and deterministic zeroes");

    return failures;
}

/// Verifies Phase 40 treats .DATA? storage as uninitialized-origin storage.
///
/// @return Number of failures.
static int test_phase40_data_question_section_warns(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".DATA?\n"
        "buf DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, buf\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", ".DATA? warning source should execute");
    failures += expect_json_contains(json, "uninitialized-read", ".DATA? read should emit uninitialized-read warning");
    failures += expect_json_contains(json, "\"symbolName\":\"buf\"", ".DATA? warning should include symbol name");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from buf + 0; 4 of those bytes still originated from uninitialized storage.", ".DATA? warning should describe whole read range");

    return failures;
}

/// Verifies Phase 40 warning mode reports each separate uninitialized-origin read.
///
/// @return Number of failures.
static int test_phase40_repeated_uninitialized_reads_emit_distinct_warnings(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    mov ebx, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "warning mode should continue after repeated uninitialized reads");
    if (count_json_fragment_occurrences(json, "\"code\":\"uninitialized-read\"") != 2U) {
        failures += record_failure("warning mode should emit one uninitialized-read diagnostic for each separate read");
    }
    failures += expect_json_contains(json, "\"line\":5", "first warning should preserve the first read source line");
    failures += expect_json_contains(json, "\"line\":6", "second warning should preserve the second read source line");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "second deterministic zero read should execute");

    return failures;
}

/// Verifies Phase 40 read-modify-write warnings occur before write tracking updates the mask.
///
/// @return Number of failures.
static int test_phase40_rmw_warning_then_writeback_marks_initialized(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    add x, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "RMW warning mode should continue");
    failures += expect_json_contains(json, "uninitialized-read", "RMW warning mode should diagnose the read before write-back");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "RMW write-back should mark all destination bytes initialized after warning");

    return failures;
}

/// Verifies Phase 40 strict read-modify-write stops before write-back.
///
/// @return Number of failures.
static int test_phase40_rmw_strict_stops_before_writeback(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    add x, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "RMW strict mode should fail before write-back");
    failures += expect_json_contains(json, "\"instructionCount\":0", "RMW strict mode should not step the instruction");
    failures += expect_json_contains(json, "uninitialized-read", "RMW strict mode should emit uninitialized-read error");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", "RMW strict mode should leave destination bytes uninitialized-origin");
    failures += expect_json_not_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":4", "RMW strict mode should not mark destination initialized");

    return failures;
}

/// Runs one Phase 64A read-modify-write planned-read policy fixture.
///
/// @param name Fixture name for failure messages.
/// @param setup_source Optional instructions to run before the checked instruction.
/// @param instruction_source Instruction that should read and then write `x`.
/// @param strict_instruction_count_fragment Expected strict-mode instruction count fragment.
/// @return Number of failures.
static int run_phase64a_rmw_planned_read_fixture(
    const char *name,
    const char *setup_source,
    const char *instruction_source,
    const char *strict_instruction_count_fragment
) {
    char source[512];
    char warning_message[192];
    char off_message[192];
    char strict_message[192];
    const char *json = NULL;
    int failures = 0;

    (void)snprintf(
        source,
        sizeof(source),
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "%s"
        "    %s\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        setup_source != NULL ? setup_source : "",
        instruction_source != NULL ? instruction_source : "nop"
    );
    (void)snprintf(warning_message, sizeof(warning_message), "Phase 64A %s warning mode should continue", name);
    (void)snprintf(off_message, sizeof(off_message), "Phase 64A %s off mode should suppress uninitialized-read only", name);
    (void)snprintf(strict_message, sizeof(strict_message), "Phase 64A %s strict mode should stop before write-back", name);

    json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        source,
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    failures += expect_json_contains(json, "\"ok\":true", warning_message);
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 64A RMW warning mode should emit uninitialized-read");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0", "Phase 64A RMW warning should describe the destination read range");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "Phase 64A RMW warning write-back should mark destination bytes initialized");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 64A RMW warning mode should complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        source,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY
    );
    failures += expect_json_contains(json, "\"ok\":true", off_message);
    failures += expect_json_not_contains(json, "uninitialized-read", "Phase 64A RMW off mode should not emit uninitialized-read");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "Phase 64A RMW off mode write-back should mark destination bytes initialized");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 64A RMW off mode should complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        source,
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", strict_message);
    failures += expect_json_contains(json, strict_instruction_count_fragment, "Phase 64A RMW strict mode should stop before executing the memory-reading instruction");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 64A RMW strict mode should emit uninitialized-read");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Phase 64A RMW strict mode should not create memory-change rows");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", "Phase 64A RMW strict mode should leave destination bytes uninitialized-origin");
    failures += expect_json_not_contains(json, "execution-complete", "Phase 64A RMW strict mode should not complete");

    return failures;
}

/// Verifies Phase 64A planned-read coverage for memory-destination RMW instructions.
///
/// @return Number of failures.
static int test_phase64a_rmw_planned_read_policy_for_existing_families(void) {
    int failures = 0;

    failures += run_phase64a_rmw_planned_read_fixture("inc mem", "", "inc x", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("dec mem", "", "dec x", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("add mem, imm", "", "add x, 1", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("sub mem, imm", "", "sub x, 1", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("adc mem, imm", "    stc\n", "adc x, 1", "\"instructionCount\":1");
    failures += run_phase64a_rmw_planned_read_fixture("sbb mem, imm", "    stc\n", "sbb x, 1", "\"instructionCount\":1");
    failures += run_phase64a_rmw_planned_read_fixture("and mem, imm", "", "and x, 1", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("or mem, imm", "", "or x, 1", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("xor mem, imm", "", "xor x, 1", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("not mem", "", "not x", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("neg mem", "", "neg x", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("shl mem, count", "", "shl x, 1", "\"instructionCount\":0");
    failures += run_phase64a_rmw_planned_read_fixture("xchg mem, reg", "    mov eax, 7\n", "xchg x, eax", "\"instructionCount\":1");

    return failures;
}

/// Verifies Phase 64A preserves already-covered memory-source planned-read behavior.
///
/// @return Number of failures.
static int test_phase64a_memory_source_planned_read_regression(void) {
    const char *source =
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    cmp x, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        source,
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 64A CMP memory-source warning mode should continue");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 64A CMP memory-source warning should emit uninitialized-read");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", "Phase 64A CMP memory-source read should not initialize source bytes");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 64A CMP memory-source warning mode should complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        source,
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 64A CMP memory-source strict mode should stop");
    failures += expect_json_contains(json, "\"instructionCount\":0", "Phase 64A CMP memory-source strict mode should stop before CMP");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 64A CMP memory-source strict mode should emit uninitialized-read");
    failures += expect_json_not_contains(json, "execution-complete", "Phase 64A CMP memory-source strict mode should not complete");

    return failures;
}

/// Verifies Phase 40 keeps lower-level invalid-address diagnostics ahead of uninitialized-read checks.
///
/// @return Number of failures.
static int test_phase40_invalid_address_precedes_uninitialized_read(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid address should remain fatal");
    failures += expect_json_contains(json, "invalid-address", "invalid address should keep its runtime diagnostic");
    failures += expect_json_not_contains(json, "uninitialized-read", "invalid address should not be reclassified as uninitialized read");

    return failures;
}

/// Verifies Phase 40 keeps allocated-object strict diagnostics independent and prior when selected.
///
/// @return Number of failures.
static int test_phase40_object_strict_regression_precedes_uninitialized_read_feature(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    test [eax+40h], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "allocated-object strict mode should still fail gap access");
    failures += expect_json_contains(json, "object-bounds-violation", "object strict regression should preserve object diagnostic");
    failures += expect_json_not_contains(json, "uninitialized-read", "object strict validation should not be replaced by uninitialized-read diagnostics");

    return failures;
}

/// Verifies Phase 39 intentionally does not track register-value taint.
///
/// @return Number of failures.
static int test_phase39_register_copy_marks_destination_initialized(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
        "y DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    mov y, eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "no-taint copy source should execute");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", "reading x should not mark x initialized");
    failures += expect_json_contains(json, "\"symbol\":\"y\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "storing register into y should mark y initialized even when EAX came from x");
    failures += expect_json_not_contains(json, "uninitialized-read", "Phase 39 must not implement read diagnostics");

    return failures;
}


/// Verifies Phase 41 records Irvine32 virtual include metadata without adding routine bodies.
///
/// @return Number of failures.
static int test_phase41_irvine32_virtual_include_metadata_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Irvine32 virtual include response should report current runtime/source-run MASM behavior phase metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Irvine32 include acceptance program should execute successfully");
    failures += expect_json_contains(json, "\"status\":\"ok\"", "Irvine32 include acceptance program should report ok status");
    failures += expect_json_contains(json, "\"instructionCount\":1", "Irvine32 include acceptance program should not synthesize routine execution");
    failures += expect_json_contains(json, "\"virtualIncludes\":{\"irvine32\":true,\"irvine32SymbolCount\":", "Irvine32 include should expose registry metadata");
    failures += expect_json_not_contains(json, "\"irvine32SymbolCount\":0", "Irvine32 registry should contain known virtual names");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Irvine32 include acceptance program should complete normally");
    failures += expect_json_not_contains(json, "Program Console", "Phase 41 must not add Program Console routine behavior");

    return failures;
}

/// Verifies Phase 41 keeps Macros.inc as a virtual no-op without Irvine32 registry metadata.
///
/// @return Number of failures.
static int test_phase41_macros_include_does_not_populate_irvine32_registry(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "INCLUDE Macros.inc\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Macros.inc virtual no-op should still execute the empty body");
    failures += expect_json_contains(json, "\"virtualIncludes\":{\"irvine32\":false,\"irvine32SymbolCount\":0}", "Macros.inc should not populate Irvine32 registry metadata");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Macros.inc virtual no-op should complete normally");

    return failures;
}

/// Verifies Phase 41 diagnoses executable use of a known but not-yet-executable Irvine32 name.
///
/// @return Number of failures.
static int test_phase41_irvine32_unsupported_routine_source_run_diagnostic(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    WriteString\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Known Irvine32 routine use should reject execution in Phase 41");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "Known Irvine32 routine use should remain an assembly diagnostic");
    failures += expect_json_contains(json, "\"virtualIncludes\":{\"irvine32\":true,\"irvine32SymbolCount\":", "Known routine diagnostic should retain registry metadata");
    failures += expect_json_contains(json, "\"kind\":\"unsupported-feature\"", "Known Irvine32 routine diagnostic should render as unsupported feature category");
    failures += expect_json_contains(json, "\"code\":\"unsupported-irvine32-routine\"", "Known Irvine32 routine diagnostic code should be stable");
    failures += expect_json_contains(json, "executable behavior for this routine is deferred to the routine-specific Irvine32 phases", "Known Irvine32 routine diagnostic should explain deferred routine execution");
    failures += expect_json_not_contains(json, "execution-complete", "Known Irvine32 routine diagnostic must prevent execution");

    return failures;
}

/// Verifies Phase 42 accepts Irvine32 exit as a successful virtual terminator.
///
/// @return Number of failures.
static int test_phase42_irvine32_exit_terminator_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 123\n"
        "    exit\n"
        "    mov eax, 999\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Irvine32 exit response should report current runtime/source-run MASM behavior phase metadata");
    failures += expect_json_contains(json, "\"ok\":true", "exit terminator source should execute successfully");
    failures += expect_json_contains(json, "\"status\":\"ok\"", "exit terminator source should report ok status");
    failures += expect_json_contains(json, "\"instructionCount\":2", "exit terminator should count MOV and EXIT only");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000007Bh\",\"unsigned\":123}", "instruction after exit must not execute");
    failures += expect_json_contains(json, "\"code\":\"startup-state-notice\"", "exit terminator should include startup-state notice");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "exit terminator should complete normally");
    failures += expect_json_not_contains(json, "programConsole", "exit must not create Program Console output");
    failures += expect_json_not_contains(json, "999", "instruction after exit should not affect observable state");

    return failures;
}

/// Verifies Phase 42 error paths for Irvine32 exit.
///
/// @return Number of failures.
static int test_phase42_irvine32_exit_terminator_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "exit without include should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "exit without include should be a parse error");
    failures += expect_json_contains(json, "\"kind\":\"assembly-error\"", "exit without include should be an assembly error");
    failures += expect_json_contains(json, "\"code\":\"unknown-instruction\"", "exit without include should use unknown-instruction");
    failures += expect_json_contains(json, "Unknown instruction or virtual Irvine32 terminator. Add INCLUDE Irvine32.inc to use exit.", "exit without include should use the required diagnostic message");
    failures += expect_json_not_contains(json, "execution-complete", "exit without include must not execute");

    json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    exit 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "exit with operands should fail assembly");
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "exit operands should use invalid-instruction-operands");
    failures += expect_json_contains(json, "exit does not take operands.", "exit operands should explain zero-operand form");

    json = masm32_sim_wasm_run_source_json(
        "INCLUDE Macros.inc\n"
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"unknown-instruction\"", "Macros.inc alone should not enable exit");

    json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call ExitProcess\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "call ExitProcess must not execute as Windows API behavior");
    failures += expect_json_contains(json, "\"code\":\"unsupported-external-call\"", "call ExitProcess should keep Windows API CALL unsupported as an external call");
    failures += expect_json_contains(json, "outside MASM32 Educational Mode", "call ExitProcess diagnostic should explain external host behavior is out of scope");
    failures += expect_json_not_contains(json, "\"status\":\"ok\"", "call ExitProcess should not complete successfully as Windows API behavior");
    failures += expect_json_not_contains(json, "execution-complete", "call ExitProcess must not execute as a Windows API");

    return failures;
}


/// Verifies Phase 43 INC and DEC register behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase43_inc_dec_register_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    inc eax\n"
        "    dec eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 43 INC/DEC response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 43 INC/DEC acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 43 INC/DEC acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "INC/DEC acceptance source should leave EAX at FFFFFFFFh");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000081h\",\"unsigned\":129}", "INC/DEC acceptance source should preserve CF and leave SF set");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 43 INC/DEC source should complete successfully");

    return failures;
}

/// Verifies Phase 43 INC and DEC memory destinations through source-run JSON.
///
/// @return Number of failures.
static int test_phase43_inc_dec_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 41\n"
        "buf BYTE 2, 2\n"
        ".code\n"
        "main PROC\n"
        "    inc value\n"
        "    dec BYTE PTR buf[1]\n"
        "    mov eax, value\n"
        "    mov bl, BYTE PTR buf[1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 43 INC/DEC memory source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 43 INC/DEC memory source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "INC value should update DWORD memory to 42");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "DEC BYTE PTR buf[1] should update memory byte to 1");
    failures += expect_json_contains(json, "\"oldHex\":\"00000029h\",\"oldUnsigned\":41,\"newHex\":\"0000002Ah\",\"newUnsigned\":42", "INC DWORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"02h\",\"oldUnsigned\":2,\"newHex\":\"01h\",\"newUnsigned\":1", "DEC BYTE memory change should be reported");

    return failures;
}

/// Verifies Phase 43 INC and DEC source-run diagnostic paths.
///
/// @return Number of failures.
static int test_phase43_inc_dec_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    inc [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "INC ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "INC ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "INC ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "INC ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "INC ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    dec eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "DEC extra operand should use invalid-instruction-operands");
    failures += expect_json_contains(json, "DEC takes exactly one register or memory operand.", "DEC extra operand diagnostic should explain operand count");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    dec 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "DEC immediate destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "DEC requires a register or memory destination.", "DEC immediate diagnostic should explain destination requirement");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    inc eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "INC extra operand should use invalid-instruction-operands");
    failures += expect_json_contains(json, "INC takes exactly one register or memory operand.", "INC extra operand diagnostic should explain operand count");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    inc DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed INC .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed INC .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed INC .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "Memory write at 00600000h for 4 bytes is not permitted in .const.", "Computed INC .CONST write diagnostic should be user-readable");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed INC .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    dec limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "Direct DEC .CONST write should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct DEC .CONST write should use const-write diagnostic");
    failures += expect_json_contains(json, "Constant data is read-only", "Direct DEC .CONST diagnostic should be user-readable");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    dec DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "DEC invalid address should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "DEC invalid address should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "DEC invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "DEC invalid address diagnostic should describe the failed read");
    failures += expect_json_not_contains(json, "execution-complete", "DEC invalid address must not report successful execution");

    return failures;
}

/// Verifies Phase 44 AND, OR, and XOR acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase44_logical_binary_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0F0F0h\n"
        "    and eax, 00FFh\n"
        "    or eax, 0100h\n"
        "    xor eax, 000Fh\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 44 logical response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 44 logical acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 44 logical acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000001FFh\",\"unsigned\":511}", "Phase 44 logical acceptance source should leave EAX at 000001FFh");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 44 logical acceptance source should clear modeled flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 44 logical source should complete successfully");

    return failures;
}

/// Verifies Phase 44 AND, OR, and XOR memory behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase44_logical_binary_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 0F0F0F0Fh\n"
        "bytes BYTE 0F0h, 0Fh\n"
        ".code\n"
        "main PROC\n"
        "    and value, 00FF00FFh\n"
        "    or BYTE PTR bytes[1], 0F0h\n"
        "    xor eax, eax\n"
        "    mov ebx, value\n"
        "    mov cl, BYTE PTR bytes[1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 44 logical memory source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Phase 44 logical memory source should execute five instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "XOR EAX,EAX should clear EAX");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"000F000Fh\",\"unsigned\":983055}", "AND value should update DWORD memory");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"000000FFh\",\"unsigned\":255}", "OR BYTE PTR bytes[1] should update byte memory");
    failures += expect_json_contains(json, "\"oldHex\":\"0F0F0F0Fh\",\"oldUnsigned\":252645135,\"newHex\":\"000F000Fh\",\"newUnsigned\":983055", "AND DWORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"0Fh\",\"oldUnsigned\":15,\"newHex\":\"FFh\",\"newUnsigned\":255", "OR BYTE memory change should be reported");

    return failures;
}

/// Verifies Phase 44 AND, OR, and XOR source-run diagnostic paths.
///
/// @return Number of failures.
static int test_phase44_logical_binary_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    and [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "AND ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "AND ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "AND ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "AND ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "AND ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    or 1, eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "OR immediate destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "OR requires a register or memory destination.", "OR immediate destination diagnostic should explain destination requirement");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 1\n"
        "other DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    xor value, other\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "XOR memory-to-memory should use invalid-instruction-operands");
    failures += expect_json_contains(json, "XOR does not support memory-to-memory operands.", "XOR memory-to-memory diagnostic should explain operand shape");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    and limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Direct AND .CONST write should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "Direct AND .CONST write should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct AND .CONST write should use const-write diagnostic");
    failures += expect_json_contains(json, "Cannot write to .CONST data. Constant data is read-only.", "Direct AND .CONST write diagnostic should be user-readable");
    failures += expect_json_not_contains(json, "execution-complete", "Direct AND .CONST write must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    or DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed OR .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed OR .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed OR .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "Memory write at 00600000h for 4 bytes is not permitted in .const.", "Computed OR .CONST write diagnostic should be user-readable");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed OR .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    and ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "AND invalid source address should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "AND invalid source address should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "AND invalid source address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "AND invalid source address diagnostic should describe the failed read");
    failures += expect_json_not_contains(json, "execution-complete", "AND invalid source address must not report successful execution");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    xor DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "XOR invalid address should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "XOR invalid address should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "XOR invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "XOR invalid address diagnostic should describe the failed read");
    failures += expect_json_not_contains(json, "execution-complete", "XOR invalid address must not report successful execution");

    return failures;
}


/// Verifies Milestone 45 NOT acceptance behavior through Phase 50 source-run JSON.
///
/// @return Number of failures.
static int test_phase45_not_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    test eax, eax\n"
        "    not eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Milestone 45 NOT regression response should report current runtime/source-run MASM behavior phase metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Milestone 45 NOT regression source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Milestone 45 NOT regression source should execute three instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "Milestone 45 NOT regression source should leave EAX at FFFFFFFFh");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000040h\",\"unsigned\":64}", "Milestone 45 NOT regression source should preserve TEST flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Milestone 45 NOT regression source should complete successfully");

    return failures;
}

/// Verifies Milestone 45 NOT memory behavior through Phase 50 source-run JSON.
///
/// @return Number of failures.
static int test_phase45_not_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 0F0F0F0Fh\n"
        "bytes BYTE 00h, 0Fh\n"
        "wordval WORD 0F0Fh\n"
        "dwordval DWORD 00000000h\n"
        ".code\n"
        "main PROC\n"
        "    not value\n"
        "    not BYTE PTR bytes[1]\n"
        "    not WORD PTR wordval\n"
        "    not DWORD PTR dwordval\n"
        "    mov eax, value\n"
        "    mov bl, BYTE PTR bytes[1]\n"
        "    mov cx, wordval\n"
        "    mov edx, dwordval\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Milestone 45 NOT memory regression source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":9", "Milestone 45 NOT memory regression source should execute eight instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"F0F0F0F0h\",\"unsigned\":4042322160}", "NOT value should update DWORD memory");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"000000F0h\",\"unsigned\":240}", "NOT BYTE PTR bytes[1] should update byte memory");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"0000F0F0h\",\"unsigned\":61680}", "NOT WORD PTR wordval should update word memory");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "NOT DWORD PTR dwordval should update dword memory");
    failures += expect_json_contains(json, "\"oldHex\":\"0F0F0F0Fh\",\"oldUnsigned\":252645135,\"newHex\":\"F0F0F0F0h\",\"newUnsigned\":4042322160", "NOT direct DWORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"0Fh\",\"oldUnsigned\":15,\"newHex\":\"F0h\",\"newUnsigned\":240", "NOT BYTE PTR memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"0F0Fh\",\"oldUnsigned\":3855,\"newHex\":\"F0F0h\",\"newUnsigned\":61680", "NOT WORD PTR memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"00000000h\",\"oldUnsigned\":0,\"newHex\":\"FFFFFFFFh\",\"newUnsigned\":4294967295", "NOT DWORD PTR memory change should be reported");

    return failures;
}

/// Verifies Milestone 45 NOT source-run diagnostic paths as Phase 50 regressions.
///
/// @return Number of failures.
static int test_phase45_not_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    not [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "NOT ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "NOT ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "NOT ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "NOT ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "NOT ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    not 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "NOT immediate destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "NOT requires a register or memory destination.", "NOT immediate destination diagnostic should explain destination requirement");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    not eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "NOT extra operand should use invalid-instruction-operands");
    failures += expect_json_contains(json, "NOT takes exactly one register or memory operand.", "NOT extra operand diagnostic should explain operand count");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    not limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Direct NOT .CONST write should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "Direct NOT .CONST write should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct NOT .CONST write should use const-write diagnostic");
    failures += expect_json_contains(json, "Cannot write to .CONST data. Constant data is read-only.", "Direct NOT .CONST write diagnostic should be user-readable");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    not DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed NOT .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed NOT .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed NOT .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "Memory write at 00600000h for 4 bytes is not permitted in .const.", "Computed NOT .CONST write diagnostic should be user-readable");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed NOT .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    not DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "NOT invalid address should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "NOT invalid address should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "NOT invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "NOT invalid address diagnostic should describe the failed read");
    failures += expect_json_not_contains(json, "execution-complete", "NOT invalid address must not report successful execution");

    return failures;
}


/// Verifies Phase 46 SHL/SAL acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase46_shift_left_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    shl eax, 1\n"
        "    mov ecx, 2\n"
        "    sal eax, cl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 46 SHL/SAL response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 46 SHL/SAL acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 46 SHL/SAL acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "SHL/SAL source should leave EAX at 8");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "SAL by CL=2 should emit default undefined-shift warning");
    failures += expect_json_contains(json, "SAL count 2 has effective count 2 for a 32-bit destination. CF, ZF, and SF were updated from the result. OF is architecturally undefined because the effective count is greater than 1. The simulator preserved OF deterministically.", "SAL by CL=2 warning should identify defined and undefined modeled flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 46 SHL/SAL source should complete successfully after warning");

    return failures;
}

/// Verifies Phase 46 SHL/SAL memory and count-zero behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase46_shift_left_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value BYTE 80h\n"
        "wordval WORD 0001h\n"
        "dwordval DWORD 40000000h\n"
        ".code\n"
        "main PROC\n"
        "    shl value, 1\n"
        "    sal WORD PTR wordval, 4\n"
        "    shl DWORD PTR dwordval, 1\n"
        "    shl DWORD PTR dwordval, 32\n"
        "    mov al, value\n"
        "    mov bx, wordval\n"
        "    mov ecx, dwordval\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 46 SHL/SAL memory source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":8", "Phase 46 SHL/SAL memory source should execute seven instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "SHL BYTE memory should leave AL at zero");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000010h\",\"unsigned\":16}", "SAL WORD memory should shift wordval by four");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"80000000h\",\"unsigned\":2147483648}", "SHL DWORD memory should shift dwordval and count-zero should not change it");
    failures += expect_json_contains(json, "\"oldHex\":\"80h\",\"oldUnsigned\":128,\"newHex\":\"00h\",\"newUnsigned\":0", "SHL BYTE memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"0001h\",\"oldUnsigned\":1,\"newHex\":\"0010h\",\"newUnsigned\":16", "SAL WORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"40000000h\",\"oldUnsigned\":1073741824,\"newHex\":\"80000000h\",\"newUnsigned\":2147483648", "SHL DWORD memory change should be reported");

    return failures;
}

/// Verifies Phase 46 SHL/SAL source-run diagnostic and strict-mode paths.
///
/// @return Number of failures.
static int test_phase46_shift_left_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    shl [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "SHL ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "SHL ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "SHL ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "SHL ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "SHL ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    shl eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "SHL EBX count should use invalid-instruction-operands");
    failures += expect_json_contains(json, "SHL count must be an immediate byte count or CL.", "SHL invalid count diagnostic should explain count operands");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    sal limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Direct SAL .CONST write should fail assembly");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct SAL .CONST write should use const-write diagnostic");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    shl DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed SHL .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed SHL .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed SHL .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed SHL .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    shl DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "SHL invalid address should fail execution");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "SHL invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "SHL invalid address diagnostic should describe the failed read");

    json = masm32_sim_wasm_run_source_json_with_shift_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "Strict shift mode should reject undefined modeled flags");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Strict shift mode should report execution error");
    failures += expect_json_contains(json, "\"instructionCount\":1", "Strict shift mode should stop before the shift instruction mutates state");
    failures += expect_json_contains(json, "\"kind\":\"runtime-error\"", "Strict shift mode should emit runtime error");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "Strict shift mode should use undefined-shift-flag code");
    failures += expect_json_contains(json, "CF is architecturally undefined because the effective count is greater than or equal to the destination width", "Strict shift mode diagnostic should identify undefined CF reason");
    failures += expect_json_contains(json, "The simulator preserved CF and OF deterministically.", "Strict shift mode diagnostic should name preserved undefined flags");
    failures += expect_json_not_contains(json, "execution-complete", "Strict shift mode must not report successful execution");

    return failures;
}


/// Verifies Phase 47 SHR acceptance behavior as a Phase 50 regression.
///
/// @return Number of failures.
static int test_phase47_shr_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 80000000h\n"
        "    shr eax, 1\n"
        "    mov ecx, 2\n"
        "    shr eax, cl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 47 SHR regression should report current runtime/source-run MASM behavior phase metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 47 SHR regression acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 47 SHR regression acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"10000000h\",\"unsigned\":268435456}", "SHR source should leave EAX at 10000000h");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "SHR by CL=2 should emit default undefined-shift warning");
    failures += expect_json_contains(json, "SHR count 2 has effective count 2 for a 32-bit destination. CF, ZF, and SF were updated from the result. OF is architecturally undefined because the effective count is greater than 1. The simulator preserved OF deterministically.", "SHR by CL=2 warning should identify defined and undefined modeled flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 47 SHR regression should complete successfully after warning");

    return failures;
}

/// Verifies Phase 47 SHR memory and count-zero behavior as a Phase 50 regression.
///
/// @return Number of failures.
static int test_phase47_shr_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value BYTE 80h\n"
        "wordval WORD 8000h\n"
        "dwordval DWORD 80000000h\n"
        ".code\n"
        "main PROC\n"
        "    shr value, 1\n"
        "    shr WORD PTR wordval, 4\n"
        "    shr DWORD PTR dwordval, 1\n"
        "    shr DWORD PTR dwordval, 32\n"
        "    mov al, value\n"
        "    mov bx, wordval\n"
        "    mov ecx, dwordval\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 47 SHR regression memory source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":8", "Phase 47 SHR regression memory source should execute seven instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000040h\",\"unsigned\":64}", "SHR BYTE memory should leave AL at 40h");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000800h\",\"unsigned\":2048}", "SHR WORD memory should shift wordval by four");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"40000000h\",\"unsigned\":1073741824}", "SHR DWORD memory should shift dwordval and count-zero should not change it");
    failures += expect_json_contains(json, "\"oldHex\":\"80h\",\"oldUnsigned\":128,\"newHex\":\"40h\",\"newUnsigned\":64", "SHR BYTE memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"8000h\",\"oldUnsigned\":32768,\"newHex\":\"0800h\",\"newUnsigned\":2048", "SHR WORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"80000000h\",\"oldUnsigned\":2147483648,\"newHex\":\"40000000h\",\"newUnsigned\":1073741824", "SHR DWORD memory change should be reported");

    return failures;
}

/// Verifies Phase 47 SHR diagnostic and strict-mode paths as a Phase 50 regression.
///
/// @return Number of failures.
static int test_phase47_shr_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    shr [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "SHR ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "SHR ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "SHR ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "SHR ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "SHR ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    shr eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "SHR EBX count should use invalid-instruction-operands");
    failures += expect_json_contains(json, "SHR count must be an immediate byte count or CL.", "SHR invalid count diagnostic should explain count operands");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    shr 1, al\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "SHR immediate destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "SHR requires a register or memory destination.", "SHR immediate destination diagnostic should explain destination requirement");
    failures += expect_json_not_contains(json, "execution-complete", "SHR immediate destination must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    shr eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"expected-comma\"", "SHR missing count should use expected-comma diagnostic");
    failures += expect_json_contains(json, "Expected a comma between operands.", "SHR missing count diagnostic should explain the comma requirement");
    failures += expect_json_not_contains(json, "execution-complete", "SHR missing count must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    shr limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Direct SHR .CONST write should fail assembly");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct SHR .CONST write should use const-write diagnostic");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    shr DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed SHR .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed SHR .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed SHR .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed SHR .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    shr DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "SHR invalid address should fail execution");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "SHR invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "SHR invalid address diagnostic should describe the failed read");

    json = masm32_sim_wasm_run_source_json_with_shift_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    shr al, 8\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "Strict SHR mode should reject undefined modeled flags");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Strict SHR mode should report execution error");
    failures += expect_json_contains(json, "\"instructionCount\":1", "Strict SHR mode should stop before the shift instruction mutates state");
    failures += expect_json_contains(json, "\"kind\":\"runtime-error\"", "Strict SHR mode should emit runtime error");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "Strict SHR mode should use undefined-shift-flag code");
    failures += expect_json_contains(json, "SHR count 8 has effective count 8 for an 8-bit destination", "Strict SHR mode diagnostic should name SHR and count details");
    failures += expect_json_contains(json, "The simulator preserved CF and OF deterministically.", "Strict SHR mode diagnostic should name preserved undefined flags");
    failures += expect_json_not_contains(json, "execution-complete", "Strict SHR mode must not report successful execution");

    return failures;
}

/// Verifies Phase 48 SAR acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase48_sar_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 80000000h\n"
        "    sar eax, 1\n"
        "    mov ecx, 2\n"
        "    sar eax, cl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 48 SAR response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 48 SAR acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 48 SAR acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"F0000000h\",\"unsigned\":4026531840}", "SAR source should leave EAX at F0000000h");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "SAR by CL=2 should emit default undefined-shift warning");
    failures += expect_json_contains(json, "SAR count 2 has effective count 2 for a 32-bit destination. CF, ZF, and SF were updated from the result. OF is architecturally undefined because the effective count is greater than 1. The simulator preserved OF deterministically.", "SAR by CL=2 warning should identify defined and undefined modeled flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 48 SAR source should complete successfully after warning");

    return failures;
}

/// Verifies Phase 48 SAR memory and count-zero behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase48_sar_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value BYTE 80h\n"
        "wordval WORD 8000h\n"
        "dwordval DWORD 80000000h\n"
        ".code\n"
        "main PROC\n"
        "    sar value, 1\n"
        "    sar WORD PTR wordval, 4\n"
        "    sar DWORD PTR dwordval, 1\n"
        "    sar DWORD PTR dwordval, 32\n"
        "    mov al, value\n"
        "    mov bx, wordval\n"
        "    mov ecx, dwordval\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 48 SAR memory source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":8", "Phase 48 SAR memory source should execute seven instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000C0h\",\"unsigned\":192}", "SAR BYTE memory should leave AL at C0h");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000F800h\",\"unsigned\":63488}", "SAR WORD memory should shift wordval by four with sign fill");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"C0000000h\",\"unsigned\":3221225472}", "SAR DWORD memory should shift dwordval and count-zero should not change it");
    failures += expect_json_contains(json, "\"oldHex\":\"80h\",\"oldUnsigned\":128,\"newHex\":\"C0h\",\"newUnsigned\":192", "SAR BYTE memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"8000h\",\"oldUnsigned\":32768,\"newHex\":\"F800h\",\"newUnsigned\":63488", "SAR WORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"80000000h\",\"oldUnsigned\":2147483648,\"newHex\":\"C0000000h\",\"newUnsigned\":3221225472", "SAR DWORD memory change should be reported");

    return failures;
}

/// Verifies Phase 48 SAR source-run diagnostic and strict-mode paths.
///
/// @return Number of failures.
static int test_phase48_sar_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    sar [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "SAR ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "SAR ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "SAR ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "SAR ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "SAR ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    sar eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "SAR EBX count should use invalid-instruction-operands");
    failures += expect_json_contains(json, "SAR count must be an immediate byte count or CL.", "SAR invalid count diagnostic should explain count operands");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    sar 1, al\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "SAR immediate destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "SAR requires a register or memory destination.", "SAR immediate destination diagnostic should explain destination requirement");
    failures += expect_json_not_contains(json, "execution-complete", "SAR immediate destination must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    sar eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"expected-comma\"", "SAR missing count should use expected-comma diagnostic");
    failures += expect_json_contains(json, "Expected a comma between operands.", "SAR missing count diagnostic should explain the comma requirement");
    failures += expect_json_not_contains(json, "execution-complete", "SAR missing count must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    sar limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Direct SAR .CONST write should fail assembly");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct SAR .CONST write should use const-write diagnostic");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    sar DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed SAR .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed SAR .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed SAR .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed SAR .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    sar DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "SAR invalid address should fail execution");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "SAR invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "SAR invalid address diagnostic should describe the failed read");

    json = masm32_sim_wasm_run_source_json_with_shift_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    sar al, 8\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "Strict SAR mode should reject undefined modeled flags");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Strict SAR mode should report execution error");
    failures += expect_json_contains(json, "\"instructionCount\":1", "Strict SAR mode should stop before the shift instruction mutates state");
    failures += expect_json_contains(json, "\"kind\":\"runtime-error\"", "Strict SAR mode should emit runtime error");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "Strict SAR mode should use undefined-shift-flag code");
    failures += expect_json_contains(json, "SAR count 8 has effective count 8 for an 8-bit destination", "Strict SAR mode diagnostic should name SAR and count details");
    failures += expect_json_contains(json, "The simulator preserved CF and OF deterministically.", "Strict SAR mode diagnostic should name preserved undefined flags");
    failures += expect_json_not_contains(json, "execution-complete", "Strict SAR mode must not report successful execution");

    return failures;
}



/// Verifies Phase 49 ROL acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase49_rol_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    rol al, 1\n"
        "    mov ecx, 2\n"
        "    rol eax, cl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 49 ROL response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 49 ROL acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 49 ROL acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "ROL source should leave EAX at 4 after one-bit then CL rotate");
    failures += expect_json_contains(json, "\"code\":\"undefined-modeled-flag\"", "ROL by CL=2 should emit default undefined-modeled-flag warning");
    failures += expect_json_contains(json, "ROL count 2 has effective count 2 and rotate amount 2 for a 32-bit destination. CF was updated from the least significant bit of the rotated result. ZF and SF were preserved because ROL does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.", "ROL by CL=2 warning should identify defined and undefined modeled flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 49 ROL source should complete successfully after warning");

    return failures;
}

/// Verifies Phase 49 ROL memory and count-zero behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase49_rol_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value BYTE 81h\n"
        "wordval WORD 8001h\n"
        "dwordval DWORD 80000001h\n"
        ".code\n"
        "main PROC\n"
        "    rol value, 1\n"
        "    rol WORD PTR wordval, 4\n"
        "    rol DWORD PTR dwordval, 1\n"
        "    rol DWORD PTR dwordval, 32\n"
        "    mov al, value\n"
        "    mov bx, wordval\n"
        "    mov ecx, dwordval\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 49 ROL memory source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":8", "Phase 49 ROL memory source should execute seven instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "ROL BYTE memory should leave AL at 03h");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000018h\",\"unsigned\":24}", "ROL WORD memory should rotate wordval by four");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "ROL DWORD memory should rotate dwordval and count-zero should not change it");
    failures += expect_json_contains(json, "\"oldHex\":\"81h\",\"oldUnsigned\":129,\"newHex\":\"03h\",\"newUnsigned\":3", "ROL BYTE memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"8001h\",\"oldUnsigned\":32769,\"newHex\":\"0018h\",\"newUnsigned\":24", "ROL WORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"80000001h\",\"oldUnsigned\":2147483649,\"newHex\":\"00000003h\",\"newUnsigned\":3", "ROL DWORD memory change should be reported");

    return failures;
}

/// Verifies Phase 49 ROL source-run diagnostic and warning paths.
///
/// @return Number of failures.
static int test_phase49_rol_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    rol [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "ROL ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "ROL ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "ROL ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "ROL ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "ROL ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    rol eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROL EBX count should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROL count must be an immediate byte count or CL.", "ROL invalid count diagnostic should explain count operands");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    rol 1, al\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROL immediate destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROL requires a register or memory destination.", "ROL immediate destination diagnostic should explain destination requirement");
    failures += expect_json_not_contains(json, "execution-complete", "ROL immediate destination must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    rol eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROL missing count should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROL takes exactly two operands.", "ROL missing count diagnostic should explain the operand count");
    failures += expect_json_not_contains(json, "execution-complete", "ROL missing count must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    rol eax, 1, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROL extra operand should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROL takes exactly two operands.", "ROL extra operand diagnostic should explain the operand count");
    failures += expect_json_not_contains(json, "execution-complete", "ROL extra operand must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    rol limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Direct ROL .CONST write should fail assembly");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct ROL .CONST write should use const-write diagnostic");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    rol DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed ROL .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed ROL .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed ROL .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed ROL .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    rol DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "ROL invalid address should fail execution");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "ROL invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "ROL invalid address diagnostic should describe the failed read");

    json = masm32_sim_wasm_run_source_json_with_shift_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    rol al, 8\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":true", "Strict shift mode should not reject ROL undefined OF producer warnings");
    failures += expect_json_contains(json, "\"code\":\"undefined-modeled-flag\"", "ROL undefined OF should use undefined-modeled-flag code even in strict shift mode");
    failures += expect_json_contains(json, "ROL count 8 has effective count 8 and rotate amount 0 for an 8-bit destination", "ROL warning should name count details");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "ROL undefined OF producer should still complete");

    return failures;
}

/// Verifies Phase 50 ROR acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase50_ror_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 01h\n"
        "    ror al, 1\n"
        "    mov ecx, 2\n"
        "    ror eax, cl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 50 ROR response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 50 ROR acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 50 ROR acceptance source should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000020h\",\"unsigned\":32}", "ROR source should leave EAX at 20h after one-bit then CL rotate");
    failures += expect_json_contains(json, "\"code\":\"undefined-modeled-flag\"", "ROR by CL=2 should emit default undefined-modeled-flag warning");
    failures += expect_json_contains(json, "ROR count 2 has effective count 2 and rotate amount 2 for a 32-bit destination. CF was updated from the most significant bit of the rotated result. ZF and SF were preserved because ROR does not define them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.", "ROR by CL=2 warning should identify defined and undefined modeled flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 50 ROR source should complete successfully after warning");

    return failures;
}

/// Verifies Phase 50 ROR memory and count-zero behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase50_ror_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value BYTE 81h\n"
        "wordval WORD 8001h\n"
        "dwordval DWORD 80000001h\n"
        ".code\n"
        "main PROC\n"
        "    ror value, 1\n"
        "    ror WORD PTR wordval, 4\n"
        "    ror DWORD PTR dwordval, 1\n"
        "    ror DWORD PTR dwordval, 32\n"
        "    mov al, value\n"
        "    mov bx, wordval\n"
        "    mov ecx, dwordval\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 50 ROR memory source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":8", "Phase 50 ROR memory source should execute seven instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000C0h\",\"unsigned\":192}", "ROR BYTE memory should leave AL at C0h");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00001800h\",\"unsigned\":6144}", "ROR WORD memory should rotate wordval by four");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"C0000000h\",\"unsigned\":3221225472}", "ROR DWORD memory should rotate dwordval and count-zero should not change it");
    failures += expect_json_contains(json, "\"oldHex\":\"81h\",\"oldUnsigned\":129,\"newHex\":\"C0h\",\"newUnsigned\":192", "ROR BYTE memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"8001h\",\"oldUnsigned\":32769,\"newHex\":\"1800h\",\"newUnsigned\":6144", "ROR WORD memory change should be reported");
    failures += expect_json_contains(json, "\"oldHex\":\"80000001h\",\"oldUnsigned\":2147483649,\"newHex\":\"C0000000h\",\"newUnsigned\":3221225472", "ROR DWORD memory change should be reported");

    return failures;
}

/// Verifies Phase 50 ROR source-run diagnostic and warning paths.
///
/// @return Number of failures.
static int test_phase50_ror_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ror [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "ROR ambiguous memory should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "ROR ambiguous memory should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "ROR ambiguous memory should use stable diagnostic code");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "ROR ambiguous memory diagnostic should explain the width rule");
    failures += expect_json_not_contains(json, "execution-complete", "ROR ambiguous memory must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ror eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROR EBX count should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROR count must be an immediate byte count or CL.", "ROR invalid count diagnostic should explain count operands");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ror 1, al\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROR immediate destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROR requires a register or memory destination.", "ROR immediate destination diagnostic should explain destination requirement");
    failures += expect_json_not_contains(json, "execution-complete", "ROR immediate destination must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ror eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROR missing count should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROR takes exactly two operands.", "ROR missing count diagnostic should explain the operand count");
    failures += expect_json_not_contains(json, "execution-complete", "ROR missing count must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ror eax, 1, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "ROR extra operand should use invalid-instruction-operands");
    failures += expect_json_contains(json, "ROR takes exactly two operands.", "ROR extra operand diagnostic should explain the operand count");
    failures += expect_json_not_contains(json, "execution-complete", "ROR extra operand must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    ror limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Direct ROR .CONST write should fail assembly");
    failures += expect_json_contains(json, "\"code\":\"const-write\"", "Direct ROR .CONST write should use const-write diagnostic");

    json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    ror DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Computed ROR .CONST write should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Computed ROR .CONST write should be a runtime error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Computed ROR .CONST write should use memory permission diagnostic");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Failed computed ROR .CONST write should not report memory changes");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    ror DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "ROR invalid address should fail execution");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "ROR invalid address should use the checked-memory diagnostic code");
    failures += expect_json_contains(json, "Invalid memory read at 00000000h for 4 bytes.", "ROR invalid address diagnostic should describe the failed read");

    json = masm32_sim_wasm_run_source_json_with_shift_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov al, 01h\n"
        "    ror al, 8\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":true", "Strict shift mode should not reject ROR undefined OF producer warnings");
    failures += expect_json_contains(json, "\"code\":\"undefined-modeled-flag\"", "ROR undefined OF should use undefined-modeled-flag code even in strict shift mode");
    failures += expect_json_contains(json, "ROR count 8 has effective count 8 and rotate amount 0 for an 8-bit destination", "ROR warning should name count details");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "ROR undefined OF producer should still complete");

    return failures;
}



/// Verifies Phase 52 LEA acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase52_lea_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 10 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov ebx, OFFSET nums\n"
        "    lea eax, nums[8]\n"
        "    lea ecx, [ebx + 4]\n"
        "    mov ebx, 0FFFFFFFFh\n"
        "    lea edx, [ebx + 4]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 52 LEA response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 52 LEA acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Phase 52 LEA acceptance source should execute five instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00500008h\",\"unsigned\":5242888}", "LEA nums[8] should compute OFFSET nums plus eight");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00500004h\",\"unsigned\":5242884}", "LEA [EBX + 4] should compute address without reading memory");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "LEA wraparound should produce 00000003h");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "LEA-only address computation should not create memory changes");
    failures += expect_json_not_contains(json, "invalid-address", "LEA should not report invalid memory address for address computation");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 52 LEA source should complete successfully");

    return failures;
}

/// Verifies Phase 52 LEA does not trigger memory validation for address-only sources.
///
/// @return Number of failures.
static int test_phase52_lea_no_memory_diagnostics_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".DATA?\n"
        "buf DWORD ?\n"
        ".CONST\n"
        "limit QWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov ebx, 0\n"
        "    lea eax, [ebx + 4]\n"
        "    lea ecx, limit\n"
        "    lea edx, [limit + 4]\n"
        "    lea esi, buf\n"
        "    lea edi, limit[ebx]\n"
        "    lea ebp, [limit + ebx]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "LEA should execute under strict uninitialized-read validation");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "LEA [0 + 4] should compute address four without invalid-address diagnostic");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00600000h\",\"unsigned\":6291456}", "LEA const symbol should compute .CONST address");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00600004h\",\"unsigned\":6291460}", "LEA const symbol offset should compute address without const diagnostics");
    failures += expect_json_contains(json, "\"ESI\":{\"hex\":\"00500000h\",\"unsigned\":5242880}", "LEA .DATA? symbol should compute address without uninitialized read");
    failures += expect_json_contains(json, "\"EDI\":{\"hex\":\"00600000h\",\"unsigned\":6291456}", "LEA QWORD const symbol[EBX] should compute address without executable-width diagnostics");
    failures += expect_json_contains(json, "\"EBP\":{\"hex\":\"00600000h\",\"unsigned\":6291456}", "LEA [QWORD const symbol + EBX] should compute address without executable-width diagnostics");
    failures += expect_json_not_contains(json, "invalid-address", "LEA should not validate the computed address as memory");
    failures += expect_json_not_contains(json, "uninitialized-read", "LEA should not trigger uninitialized-read diagnostics");
    failures += expect_json_not_contains(json, "permission-denied", "LEA should not trigger .CONST permission diagnostics");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "LEA validation source should not report memory changes");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "LEA validation source should complete");

    return failures;
}

/// Verifies Phase 52 LEA source-run diagnostic paths.
///
/// @return Number of failures.
static int test_phase52_lea_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, OFFSET nums\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "LEA OFFSET source should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "LEA OFFSET source should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"invalid-effective-address-expression\"", "LEA OFFSET source should use invalid-effective-address-expression");
    failures += expect_json_contains(json, "LEA source must be an effective-address expression", "LEA OFFSET diagnostic should explain address-expression rule");
    failures += expect_json_not_contains(json, "execution-complete", "LEA OFFSET source must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea ax, nums\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "LEA non-32-bit destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "LEA destination must be a 32-bit register", "LEA non-32-bit destination diagnostic should explain width rule");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    lea eax, [eax * 4]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"unsupported-scaled-index\"", "LEA scaled-index source should use unsupported-scaled-index");
    failures += expect_json_contains(json, "Scaled-index memory operands are not supported yet.", "LEA scaled-index diagnostic should preserve existing wording");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    lea eax, [0]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-effective-address-expression\"", "LEA numeric memory expression should use invalid-effective-address-expression");
    failures += expect_json_contains(json, "LEA does not support numeric-only memory expressions", "LEA numeric expression diagnostic should explain rejection");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "nums DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    lea eax, [nums + 2147483648]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-effective-address-expression\"", "LEA displacement overflow should use invalid-effective-address-expression");
    failures += expect_json_contains(json, "LEA address displacement is outside the supported signed 32-bit range", "LEA displacement overflow diagnostic should explain range rule");

    return failures;
}


/// Verifies Phase 57-CORR2 compact negative displacement writes through source-run.
///
/// @return Number of failures.
static int test_phase57corr2_compact_negative_displacement_write_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 0, 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    mov DWORD PTR [eax-4], 10\n"
        "    mov ebx, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 57-CORR2 write response should report current numeric runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "compact negative displacement write should execute");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000000Ah\",\"unsigned\":10}", "compact negative displacement write should update x and load EBX = 10");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"address\":\"00500000h\"", "compact negative displacement write should resolve memory change to x base");
    failures += expect_json_contains(json, "\"newHex\":\"0000000Ah\",\"newUnsigned\":10", "compact negative displacement write should record new DWORD value 10");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "compact negative displacement write should complete successfully");

    return failures;
}

/// Verifies Phase 57-CORR2 compact negative displacement reads through source-run.
///
/// @return Number of failures.
static int test_phase57corr2_compact_negative_displacement_read_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 10, 20\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    mov ebx, DWORD PTR [eax-4]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 57-CORR2 read response should report current numeric runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "compact negative displacement read should execute");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"0000000Ah\",\"unsigned\":10}", "compact negative displacement read should load EBX = 10");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "compact negative displacement read should complete successfully");

    return failures;
}

/// Verifies Phase 57-CORR2 compact negative displacement LEA remains address-only.
///
/// @return Number of failures.
static int test_phase57corr2_compact_negative_displacement_lea_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 0, 0\n"
        ".code\n"
        "main PROC\n"
        "    mov ebx, OFFSET x\n"
        "    add ebx, 4\n"
        "    lea eax, [ebx-4]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 57-CORR2 LEA response should report current numeric runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "compact negative displacement LEA should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00500000h\",\"unsigned\":5242880}", "compact negative displacement LEA should compute x base address");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "compact negative displacement LEA should not create memory-change rows");
    failures += expect_json_not_contains(json, "uninitialized-read", "compact negative displacement LEA should not perform a memory read");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "compact negative displacement LEA should complete successfully");

    return failures;
}

/// Verifies Phase 57-CORR2 keeps advanced compact addressing rejected through source-run.
///
/// @return Number of failures.
static int test_phase57corr2_compact_negative_displacement_advanced_rejection_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 0, 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    mov ebx, DWORD PTR [eax-4*2]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "advanced compact displacement expression should fail source-run");
    failures += expect_json_contains(json, "unsupported-scaled-index", "advanced compact displacement expression should use unsupported scaled-index diagnostic");
    failures += expect_json_not_contains(json, "execution-complete", "advanced compact displacement expression should not execute");
    failures += expect_json_not_contains(json, "programConsole", "advanced compact displacement expression should not produce Program Console output");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "advanced compact displacement expression should not create memory-change rows");

    return failures;
}


/// Verifies Phase 53 MUL acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase53_mul_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    mov ebx, 20\n"
        "    mul ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 53 MUL response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 53 MUL acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 53 MUL acceptance source should execute three instructions");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "MUL fitting 32-bit product should clear EDX");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000C8h\",\"unsigned\":200}", "MUL acceptance source should write EAX=200");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "MUL fitting product should clear CF and OF in EFLAGS");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "MUL register source should not create memory changes");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 53 MUL source should complete successfully");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 0FFh\n"
        "    mov bl, 2\n"
        "    mul bl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000001FEh\",\"unsigned\":510}", "8-bit MUL overflow should write AX=01FEh");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000801h\",\"unsigned\":2049}", "8-bit MUL overflow should set CF and OF in EFLAGS");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ax, 1234h\n"
        "    mov bx, 10h\n"
        "    mul bx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00002340h\",\"unsigned\":9024}", "16-bit MUL should write low AX");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "16-bit MUL should write high DX");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    mov ebx, 2\n"
        "    mul ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFFEh\",\"unsigned\":4294967294}", "32-bit MUL overflow should write low EAX");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "32-bit MUL overflow should write high EDX");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000801h\",\"unsigned\":2049}", "32-bit MUL overflow should set CF and OF in EFLAGS");

    return failures;
}

/// Verifies Phase 53 MUL memory-source behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase53_mul_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "values DWORD 5, 20\n"
        ".CONST\n"
        "factor DWORD 3\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    mul values[4]\n"
        "    mov esi, OFFSET factor\n"
        "    mov eax, 7\n"
        "    mul DWORD PTR [esi]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "MUL memory-source program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":6", "MUL memory-source program should execute five instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000015h\",\"unsigned\":21}", "MUL readable .CONST source should produce final EAX=21");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "MUL memory source fitting product should clear EDX");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "MUL memory-source reads should not create memory-change rows");
    failures += expect_json_not_contains(json, "permission-denied", "MUL readable .CONST source should not trigger permission diagnostics");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "MUL memory-source program should complete");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 20\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    mul x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000C8h\",\"unsigned\":200}", "MUL direct symbol source should read data memory");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "MUL direct symbol source should not write memory");

    return failures;
}

/// Verifies Phase 53 MUL participates in opt-in uninitialized-read diagnostics for memory sources.
///
/// @return Number of failures.
static int test_phase53_mul_uninitialized_memory_source_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 3\n"
        "    mul x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "MUL uninitialized-read warning mode should continue");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "MUL memory source should emit uninitialized-read warning in opt-in mode");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0", "MUL uninitialized-read warning should describe the memory source range");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "MUL deterministic zero read should execute after warning");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "MUL uninitialized source read should not write memory");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "MUL warning mode should still complete execution");

    return failures;
}


/// Verifies Phase 56 unsigned DIV source-run success behavior.
///
/// @return Number of failures.
static int test_phase56_div_source_run_programs(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov edx, 0\n"
        "    mov eax, 100\n"
        "    mov ebx, 7\n"
        "    div ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 56 DIV response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 56 32-bit DIV should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 56 32-bit DIV should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000000Eh\",\"unsigned\":14}", "100/7 DIV should write EAX quotient 14");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "100/7 DIV should write EDX remainder 2");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ax, 0014h\n"
        "    mov bl, 5\n"
        "    div bl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 56 8-bit DIV should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "8-bit DIV should write AL quotient and AH remainder");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "factor DWORD 7\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, 0\n"
        "    mov eax, 100\n"
        "    div factor\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 56 memory-source DIV should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000000Eh\",\"unsigned\":14}", "memory-source DIV should write quotient");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "memory-source DIV should write remainder");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "memory-source DIV should not write memory");

    return failures;
}

/// Verifies Phase 56 unsigned DIV source-run diagnostics.
///
/// @return Number of failures.
static int test_phase56_div_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 100\n"
        "    mov edx, 2\n"
        "    mov ebx, 0\n"
        "    div ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Phase 56 DIV divide by zero should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Phase 56 divide by zero should be execution error");
    failures += expect_json_contains(json, "\"code\":\"divide-by-zero\"", "Phase 56 divide by zero should use divide-by-zero diagnostic");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 56 divide by zero should stop before DIV executes");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "divide-by-zero should preserve EAX");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "divide-by-zero should preserve EDX");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov edx, 1\n"
        "    mov eax, 0\n"
        "    mov ebx, 1\n"
        "    div ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 56 DIV quotient overflow should fail execution");
    failures += expect_json_contains(json, "\"code\":\"quotient-overflow\"", "Phase 56 quotient overflow should use quotient-overflow diagnostic");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "quotient-overflow should preserve EAX");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "quotient-overflow should preserve EDX");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    div DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 56 invalid memory source should fail execution");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "Phase 56 invalid memory source should use checked memory diagnostics");
    failures += expect_json_contains(json, "\"instructionCount\":1", "Phase 56 invalid memory source should stop before DIV executes");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".DATA?\n"
        "factor DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, 0\n"
        "    mov eax, 100\n"
        "    div factor\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 56 strict uninitialized divisor read should fail execution");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 56 strict uninitialized divisor read should use uninitialized-read diagnostic");
    failures += expect_json_contains(json, "\"instructionCount\":2", "Phase 56 strict uninitialized divisor read should stop before DIV executes");
    failures += expect_json_not_contains(json, "\"code\":\"divide-by-zero\"", "Phase 56 strict uninitialized divisor read should stop before divide-by-zero evaluation");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    div [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "Phase 56 DIV ambiguous memory should use ambiguous-memory-width");

    return failures;
}


/// Verifies Phase 57 signed IDIV source-run success behavior.
///
/// @return Number of failures.
static int test_phase57_idiv_source_run_programs(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, -100\n"
        "    cdq\n"
        "    mov ebx, 7\n"
        "    idiv ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 57 IDIV response should report runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 57 32-bit IDIV should execute");
    failures += expect_json_contains(json, "\"instructionCount\":5", "Phase 57 32-bit IDIV should execute four instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFF2h\",\"unsigned\":4294967282}", "-100/7 IDIV should write EAX quotient -14");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"FFFFFFFEh\",\"unsigned\":4294967294}", "-100/7 IDIV should write EDX remainder -2");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "register IDIV should not write memory");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ax, 5\n"
        "    mov bl, -1\n"
        "    idiv bl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 57 8-bit IDIV should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000FBh\",\"unsigned\":251}", "8-bit IDIV should write AL quotient -5 and AH remainder 0");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "factor SDWORD -7\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, 0\n"
        "    mov eax, 100\n"
        "    idiv factor\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 57 memory-source IDIV should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFF2h\",\"unsigned\":4294967282}", "memory-source IDIV should write quotient");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "memory-source IDIV should write remainder with dividend sign");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "memory-source IDIV should not write memory");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "u DWORD 0FFFFFFFFh\n"
        "s SDWORD -1\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, 0\n"
        "    mov eax, 5\n"
        "    idiv u\n"
        "    mov ecx, eax\n"
        "    mov edx, 0\n"
        "    mov eax, 5\n"
        "    idiv s\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "IDIV declaration signedness independence fixture should execute");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"FFFFFFFBh\",\"unsigned\":4294967291}", "DWORD 0FFFFFFFFh divisor should be sign-interpreted as -1 by IDIV");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFFBh\",\"unsigned\":4294967291}", "SDWORD -1 divisor should match raw DWORD equivalent");

    return failures;
}

/// Verifies Phase 57 signed IDIV source-run diagnostics and planned-read integration.
///
/// @return Number of failures.
static int test_phase57_idiv_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 100\n"
        "    cdq\n"
        "    mov ebx, 0\n"
        "    idiv ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Phase 57 IDIV divide by zero should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Phase 57 divide by zero should be execution error");
    failures += expect_json_contains(json, "\"code\":\"divide-by-zero\"", "Phase 57 divide by zero should use divide-by-zero diagnostic");
    failures += expect_json_contains(json, "IDIV divisor operand EBX evaluated to zero", "Phase 57 divide-by-zero message should name IDIV and divisor operand");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 57 divide by zero should stop before IDIV executes");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "IDIV divide-by-zero should preserve EAX");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 80000000h\n"
        "    cdq\n"
        "    mov ebx, -1\n"
        "    idiv ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 57 IDIV quotient overflow should fail execution");
    failures += expect_json_contains(json, "\"code\":\"quotient-overflow\"", "Phase 57 quotient overflow should use quotient-overflow diagnostic");
    failures += expect_json_contains(json, "IDIV quotient is too large to fit in quotient register EAX", "Phase 57 quotient-overflow message should name IDIV and quotient register");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"80000000h\",\"unsigned\":2147483648}", "IDIV quotient overflow should preserve EAX");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    idiv [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "Phase 57 IDIV ambiguous memory should use ambiguous-memory-width");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".DATA?\n"
        "factor SDWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, 0\n"
        "    mov eax, 100\n"
        "    idiv factor\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 57 strict uninitialized IDIV divisor read should fail execution");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 57 strict uninitialized IDIV divisor read should use uninitialized-read diagnostic");
    failures += expect_json_contains(json, "\"instructionCount\":2", "Phase 57 strict uninitialized IDIV divisor read should stop before IDIV executes");
    failures += expect_json_not_contains(json, "\"code\":\"divide-by-zero\"", "Phase 57 strict uninitialized IDIV divisor read should stop before divide-by-zero evaluation");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".data\n"
        "first  DWORD 1\n"
        "second DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET first\n"
        "    mov edx, 0\n"
        "    mov eax, 10\n"
        "    idiv DWORD PTR [esi + 1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 57 strict object validation should stop IDIV before mutation");
    failures += expect_json_contains(json, "\"code\":\"object-bounds-violation\"", "Phase 57 strict object validation should see IDIV planned read");
    failures += expect_json_not_contains(json, "\"code\":\"divide-by-zero\"", "Phase 57 object strict stop should occur before division");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov esi, OFFSET x\n"
        "    add esi, 4\n"
        "    mov edx, 0\n"
        "    mov eax, 10\n"
        "    idiv DWORD PTR [esi]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 57 strict section-image validation should stop IDIV before mutation");
    failures += expect_json_contains(json, "\"code\":\"section-image-violation\"", "Phase 57 strict section-image validation should see IDIV planned read");
    failures += expect_json_contains(json, "\"accessKind\":\"read\"", "Phase 57 IDIV section-image diagnostic should be a planned read");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 57 IDIV section-image strict stop should occur before IDIV executes");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000000Ah\"", "Phase 57 IDIV section-image strict stop should preserve quotient register");
    failures += expect_json_not_contains(json, "\"code\":\"divide-by-zero\"", "Phase 57 section-image strict stop should occur before division");
    failures += expect_json_not_contains(json, "execution-complete", "Phase 57 section-image strict stop should not complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        ".DATA?\n"
        "factor SDWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov edx, 0\n"
        "    mov eax, 100\n"
        "    idiv factor\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 57 default-style warning should report uninitialized IDIV divisor");
    failures += expect_json_contains(json, "\"code\":\"divide-by-zero\"", "Phase 57 deterministic zero divisor should then produce divide-by-zero");
    failures += expect_json_not_contains(json, "\"code\":\"execution-complete\"", "Phase 57 fatal divide-by-zero should not render execution-complete");

    return failures;
}

/// Verifies Phase 53A does not reject a symbol-offset MUL source at assembly time.
///
/// @return Number of failures.
static int test_phase53a_mul_symbol_offset_crossing_object_is_runtime_controlled(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".DATA?\n"
        "x DWORD ?\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    mul [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 53A MUL symbol-offset fixture should execute in explicit region-only mode");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 53A MUL symbol-offset fixture should execute both instructions");
    failures += expect_json_not_contains(json, "symbol-offset-out-of-range", "Phase 53A must not reject cross-object symbol offset during parsing");
    failures += expect_json_not_contains(json, "object-bounds", "explicit region-only mode should not emit object-bound diagnostics");
    failures += expect_json_not_contains(json, "uninitialized-read", "explicit region-only mode should not emit uninitialized-read diagnostics");
    failures += expect_json_contains(json, "unaligned-memory-access", "MUL [x+1] should preserve existing unaligned warning behavior");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "MUL [x+1] should read deterministic zero-filled bytes");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 53A MUL symbol-offset fixture should complete");

    return failures;
}

/// Verifies Phase 53A default mode remains region-only for object-spanning reads.
///
/// @return Number of failures.
static int test_phase53a_default_object_spanning_read_has_no_object_diagnostic(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 0\n"
        "y DWORD 0\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "default region-only object-spanning read should execute");
    failures += expect_json_contains(json, "\"instructionCount\":2", "default object-spanning read should execute one instruction");
    failures += expect_json_not_contains(json, "object-bounds-warning", "default mode should not emit object-bound warning");
    failures += expect_json_not_contains(json, "object-bounds-violation", "default mode should not emit object-bound violation");
    failures += expect_json_contains(json, "unaligned-memory-access", "default object-spanning read should preserve unaligned warning");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "default object-spanning read should complete");

    return failures;
}

/// Verifies Phase 53A allocated-object warning mode remains Level 4 and continues.
///
/// @return Number of failures.
static int test_phase53a_object_spanning_read_warning_mode_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD 0\n"
        "y DWORD 0\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "allocated-object warning mode should continue object-spanning read");
    failures += expect_json_contains(json, "\"code\":\"object-bounds-warning\"", "allocated-object warning mode should emit object warning");
    failures += expect_json_contains(json, "spans multiple declared data objects", "object warning should describe adjacent-object span");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "allocated-object warning mode should complete");

    return failures;
}

/// Verifies Phase 53A allocated-object strict mode stops before instruction mutation.
///
/// @return Number of failures.
static int test_phase53a_object_spanning_read_strict_mode_stops_before_mutation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x DWORD 01020304h\n"
        "y DWORD 05060708h\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 777\n"
        "    mov eax, DWORD PTR [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "allocated-object strict mode should reject object-spanning read");
    failures += expect_json_contains(json, "\"instructionCount\":1", "strict object validation should stop before the second instruction mutates state");
    failures += expect_json_contains(json, "\"code\":\"object-bounds-violation\"", "strict object validation should emit object-bounds-violation");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000309h\",\"unsigned\":777}", "strict object validation should preserve pre-instruction EAX");
    failures += expect_json_not_contains(json, "08010203h", "strict object validation should not expose the value that the rejected load would have produced");
    failures += expect_json_not_contains(json, "execution-complete", "strict object validation should not complete execution");

    return failures;
}

/// Verifies Phase 53A lower-level runtime memory errors still precede object validation.
///
/// @return Number of failures.
static int test_phase53a_invalid_region_still_precedes_object_validation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid region should remain fatal in strict object mode");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "invalid region should preserve lower-level invalid-address diagnostic");
    failures += expect_json_not_contains(json, "object-bounds-violation", "invalid address should not be reclassified as object-bounds violation");

    return failures;
}

/// Verifies Phase 53A .CONST permission diagnostics keep precedence over object validation.
///
/// @return Number of failures.
static int test_phase53a_const_write_precedes_object_validation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".CONST\n"
        "c DWORD 123\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET c\n"
        "    mov DWORD PTR [eax], 456\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", ".CONST write should remain fatal in strict object mode");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", ".CONST write should preserve permission diagnostic");
    failures += expect_json_not_contains(json, "object-bounds-violation", ".CONST permission diagnostic should precede object validation");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", ".CONST write failure should not commit memory changes");

    return failures;
}

/// Verifies Phase 53A uninitialized-read modes remain orthogonal to object policy.
///
/// @return Number of failures.
static int test_phase53a_uninitialized_read_modes_on_symbol_offset(void) {
    const char *source =
        ".DATA?\n"
        "x DWORD ?\n"
        "y DWORD ?\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY);
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "explicit region-only mode should execute uninitialized-origin object-spanning read");
    failures += expect_json_not_contains(json, "uninitialized-read", "explicit region-only mode should not emit uninitialized-read diagnostic");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS);
    failures += expect_json_contains(json, "\"ok\":true", "uninitialized-read warning mode should continue object-spanning read");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "uninitialized-read warning mode should emit diagnostic");
    failures += expect_json_contains(json, "reads 4 bytes from x + 1", "uninitialized-read warning should describe symbol-offset range");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "uninitialized-read warning mode should complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT);
    failures += expect_json_contains(json, "\"ok\":false", "uninitialized-read strict mode should stop object-spanning read");
    failures += expect_json_contains(json, "\"instructionCount\":0", "uninitialized-read strict mode should stop before the read executes");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "uninitialized-read strict mode should emit diagnostic");
    failures += expect_json_not_contains(json, "execution-complete", "uninitialized-read strict mode should not complete");

    return failures;
}


/// Verifies Phase 53B keeps default execution free of section-boundary diagnostics.
///
/// @return Number of failures.
static int test_phase53b_default_section_validation_off(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "default section-validation policy should continue data slack read");
    failures += expect_json_not_contains(json, "section-image-violation", "default mode should not emit section-image diagnostics");
    failures += expect_json_not_contains(json, "section-capacity-violation", "default mode should not emit section-capacity diagnostics");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "default section-validation run should complete");

    return failures;
}

/// Verifies Phase 53B section-image warning mode diagnoses fixed-layout slack and continues.
///
/// @return Number of failures.
static int test_phase53b_section_image_warning_mode_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_WARN
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "section-image warning mode should continue data slack read");
    failures += expect_json_contains(json, "\"code\":\"section-image-violation\"", "section-image warning mode should emit diagnostic");
    failures += expect_json_contains(json, "leaves the section image range for .data/.DATA?", "section-image warning should name data image boundary");
    failures += expect_json_contains(json, "\"accessKind\":\"read\"", "section-image diagnostic should include access kind");
    failures += expect_json_contains(json, "\"accessStartAddress\":\"00500004h\"", "section-image diagnostic should include start address");
    failures += expect_json_contains(json, "\"boundaryEndAddress\":\"00500003h\"", "section-image diagnostic should include image end");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "section-image warning mode should complete");

    return failures;
}

/// Verifies Phase 53B section-image strict mode stops before mutation.
///
/// @return Number of failures.
static int test_phase53b_section_image_strict_mode_stops_before_mutation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "section-image strict mode should reject data slack write");
    failures += expect_json_contains(json, "\"instructionCount\":2", "section-image strict mode should stop before the write executes");
    failures += expect_json_contains(json, "\"code\":\"section-image-violation\"", "section-image strict mode should emit runtime diagnostic");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "section-image strict mode should not commit memory changes");
    failures += expect_json_not_contains(json, "execution-complete", "section-image strict mode should not complete");

    return failures;
}


/// Builds a Phase 53B automatic layout policy whose `.data` VM region is larger than parser section capacity.
///
/// @return Layout policy suitable for section-capacity boundary tests.
static VmLayoutPolicy phase53b_data_capacity_test_policy(void) {
    VmLayoutPolicy policy = vm_layout_default_policy();
    const uint32_t data_region_size = VM_LAYOUT_FIXED_DATA_SIZE + 0x1000U;
    const uint32_t const_base = VM_LAYOUT_FIXED_DATA_BASE + data_region_size + 0x1000U;
    const uint32_t heap_base = const_base + VM_LAYOUT_FIXED_CONST_SIZE + 0x1000U;

    policy.regions[VM_LAYOUT_REGION_DATA].minimum_size = data_region_size;
    policy.regions[VM_LAYOUT_REGION_DATA].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = data_region_size;
    policy.regions[VM_LAYOUT_REGION_DATA].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_DEBUG] = data_region_size;
    policy.regions[VM_LAYOUT_REGION_DATA].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_ROBUSTNESS] = data_region_size;

    policy.regions[VM_LAYOUT_REGION_CONST].base = const_base;
    policy.regions[VM_LAYOUT_REGION_CONST].limit = const_base + VM_LAYOUT_FIXED_CONST_SIZE;
    policy.regions[VM_LAYOUT_REGION_HEAP].base = heap_base;
    policy.regions[VM_LAYOUT_REGION_HEAP].limit = heap_base + VM_LAYOUT_FIXED_HEAP_SIZE;

    return policy;
}

/// Verifies Phase 53B section-capacity warning mode diagnoses a range outside known `.data` section capacity.
///
/// @return Number of failures.
static int test_phase53b_data_section_capacity_warning_mode_continues(void) {
    VmLayoutPolicy policy = phase53b_data_capacity_test_policy();
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_and_section_validation(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 00600000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &policy,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_WARN,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "data section-capacity warning mode should continue valid-region write outside section capacity");
    failures += expect_json_contains(json, "\"code\":\"section-capacity-violation\"", "data section-capacity warning mode should emit diagnostic");
    failures += expect_json_contains(json, "leaves the section capacity range for .data/.DATA?", "data section-capacity warning should name data capacity boundary");
    failures += expect_json_contains(json, "\"accessKind\":\"write\"", "data section-capacity diagnostic should include access kind");
    failures += expect_json_contains(json, "\"accessStartAddress\":\"00600000h\"", "data section-capacity diagnostic should include start address");
    failures += expect_json_contains(json, "\"boundaryEndAddress\":\"005FFFFFh\"", "data section-capacity diagnostic should include capacity end");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "data section-capacity warning mode should complete");

    return failures;
}

/// Verifies Phase 53B section-capacity strict mode stops before a `.data` section-capacity escape mutates memory.
///
/// @return Number of failures.
static int test_phase53b_data_section_capacity_strict_mode_stops_before_mutation(void) {
    VmLayoutPolicy policy = phase53b_data_capacity_test_policy();
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_and_section_validation(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 00600000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &policy,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "data section-capacity strict mode should reject valid-region write outside section capacity");
    failures += expect_json_contains(json, "\"instructionCount\":1", "data section-capacity strict mode should stop before the write executes");
    failures += expect_json_contains(json, "\"code\":\"section-capacity-violation\"", "data section-capacity strict mode should emit runtime diagnostic");
    failures += expect_json_contains(json, "leaves the section capacity range for .data/.DATA?", "data section-capacity strict diagnostic should name data capacity boundary");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "data section-capacity strict mode should not create memory changes");
    failures += expect_json_not_contains(json, "execution-complete", "data section-capacity strict mode should not complete");

    return failures;
}

/// Verifies Phase 53B section-capacity warning mode diagnoses non-section storage and continues.
///
/// @return Number of failures.
static int test_phase53b_section_capacity_warning_mode_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".code\n"
        "main PROC\n"
        "    mov eax, 00700000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_WARN,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "section-capacity warning mode should continue heap-region write");
    failures += expect_json_contains(json, "\"code\":\"section-capacity-violation\"", "section-capacity warning mode should emit diagnostic");
    failures += expect_json_contains(json, "does not start inside a known section capacity range for heap", "section-capacity warning should describe non-section storage");
    failures += expect_json_contains(json, "\"accessKind\":\"write\"", "section-capacity diagnostic should include access kind");
    failures += expect_json_contains(json, "\"ownerSection\":\"heap\"", "section-capacity diagnostic should include owning region when known");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "section-capacity warning mode should complete");

    return failures;
}

/// Verifies Phase 53B section-capacity strict mode stops before mutation.
///
/// @return Number of failures.
static int test_phase53b_section_capacity_strict_mode_stops_before_mutation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".code\n"
        "main PROC\n"
        "    mov eax, 00700000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "section-capacity strict mode should reject heap-region write");
    failures += expect_json_contains(json, "\"instructionCount\":1", "section-capacity strict mode should stop before the write executes");
    failures += expect_json_contains(json, "\"code\":\"section-capacity-violation\"", "section-capacity strict mode should emit runtime diagnostic");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "section-capacity strict mode should not create memory changes");
    failures += expect_json_not_contains(json, "execution-complete", "section-capacity strict mode should not complete");

    return failures;
}

/// Verifies Phase 53B keeps lower-level invalid-address diagnostics ahead of section validation.
///
/// @return Number of failures.
static int test_phase53b_invalid_region_precedes_section_validation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "invalid region should remain fatal with section validation enabled");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "invalid region should preserve lower-level diagnostic");
    failures += expect_json_not_contains(json, "section-capacity-violation", "invalid region should not be reclassified as section capacity");
    failures += expect_json_not_contains(json, "section-image-violation", "invalid region should not be reclassified as section image");

    return failures;
}

/// Verifies Phase 53B .CONST write permission diagnostics precede section validation.
///
/// @return Number of failures.
static int test_phase53b_const_write_precedes_section_validation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".CONST\n"
        "c DWORD 123\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET c\n"
        "    mov DWORD PTR [eax], 456\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", ".CONST write should remain fatal with section validation enabled");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", ".CONST write should preserve permission diagnostic");
    failures += expect_json_not_contains(json, "section-capacity-violation", ".CONST permission diagnostic should precede section capacity");
    failures += expect_json_not_contains(json, "section-image-violation", ".CONST permission diagnostic should precede section image");

    return failures;
}

/// Verifies Phase 53B section-image warnings precede declared-object warnings.
///
/// @return Number of failures.
static int test_phase53b_section_image_warning_precedes_object_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".data\n"
        "x DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_WARN
    );
    const char *section_position = strstr(json, "section-image-violation");
    const char *object_position = strstr(json, "object-bounds-warning");
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "combined section-image and object warnings should continue");
    failures += expect_json_contains(json, "section-image-violation", "combined warning mode should emit section-image warning");
    failures += expect_json_contains(json, "object-bounds-warning", "combined warning mode should emit declared-object warning");
    if (section_position == NULL || object_position == NULL || section_position > object_position) {
        failures += record_failure("section-image warning should be serialized before object-bounds warning");
    }
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "combined warning mode should complete");

    return failures;
}

/// Verifies Phase 53C default source-run behavior warns for uninitialized-origin reads.
///
/// @return Number of failures.
static int test_phase53c_default_uninitialized_read_warns_and_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 53C default uninitialized-read warning should continue execution");
    failures += expect_json_contains(json, "\"instructionCount\":2", "Phase 53C default warning should still execute the read instruction");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 53C default warning should preserve deterministic zero read value");
    failures += expect_json_contains_once(json, "\"code\":\"uninitialized-read\"", "Phase 53C default source-run should emit exactly one uninitialized-read warning");
    failures += expect_json_contains(json, "\"kind\":\"simulator-warning\"", "Phase 53C default uninitialized read should be a simulator warning");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage.", "Phase 53C default warning should explain uninitialized-origin storage");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 53C default warning should still report execution completion");
    failures += expect_json_not_contains(json, "uninitializedOrigin", "Phase 53C default source-run JSON should not expose test-only metadata");

    return failures;
}

/// Verifies Phase 53C keeps explicit uninitialized-read opt-out behavior available.
///
/// @return Number of failures.
static int test_phase53c_uninitialized_read_explicit_off_preserves_silent_behavior(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Explicit uninitialized-read off should continue execution");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Explicit uninitialized-read off should preserve deterministic zero read value");
    failures += expect_json_not_contains(json, "uninitialized-read", "Explicit uninitialized-read off should suppress default teaching warning");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Explicit uninitialized-read off should still report execution completion");

    return failures;
}

/// Verifies Phase 53C uninitialized-read warnings inspect overlapping bytes for indirect reads.
///
/// @return Number of failures.
static int test_phase53c_default_indirect_uninitialized_read_warns_for_tracked_overlap(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    mov ebx, DWORD PTR [eax+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 53C indirect uninitialized read should continue in warning mode");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 53C indirect uninitialized read should execute both instructions");
    failures += expect_json_contains_once(json, "\"code\":\"uninitialized-read\"", "Phase 53C indirect source should emit exactly one uninitialized-read warning");
    failures += expect_json_contains(json, "Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 3 of those bytes still originated from uninitialized storage.", "Phase 53C warning should report tracked uninitialized bytes even when the read extends beyond the tracked image");
    failures += expect_json_contains(json, "\"uninitializedByteCount\":3", "Phase 53C indirect warning should report the overlapping uninitialized byte count");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 53C indirect warning should still report execution completion");

    return failures;
}

/// Verifies Phase 53C default warnings cover Phase 53-style memory sources.
///
/// @return Number of failures.
static int test_phase53c_default_uninitialized_mul_symbol_offset_warns(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "x DWORD ?\n"
        "y DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    mul [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 53C MUL symbol-offset default warning should continue");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 53C MUL symbol-offset source should execute both instructions");
    failures += expect_json_contains_once(json, "\"code\":\"uninitialized-read\"", "Phase 53C MUL source should emit exactly one uninitialized-read warning");
    failures += expect_json_contains(json, "Memory read range 00500001h..00500004h reads 4 bytes from x + 1; 4 of those bytes still originated from uninitialized storage.", "Phase 53C MUL warning should describe the symbol-offset source range");
    failures += expect_json_not_contains(json, "section-image-violation", "Phase 53C default should not enable section-image validation");
    failures += expect_json_not_contains(json, "section-capacity-violation", "Phase 53C default should not enable section-capacity validation");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 53C MUL warning should still report execution completion");

    return failures;
}

/// Verifies Phase 53C default source-run behavior warns for undefined modeled-flag use.
///
/// @return Number of failures.
static int test_phase53c_default_undefined_flag_use_warns_and_continues(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 53C default undefined flag-use warning should continue execution");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Phase 53C default undefined flag-use warning should execute the ADC consumer");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "Phase 53C default warning should use deterministic preserved CF for ADC");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "Phase 53C default should preserve existing producer warning");
    failures += expect_json_contains_once(json, "\"code\":\"undefined-flag-use\"", "Phase 53C default should emit exactly one undefined-flag-use consumer warning");
    failures += expect_json_contains(json, "ADC reads CF, but CF is architecturally undefined from SHL at line 5", "Phase 53C default consumer warning should name consumer, flag, producer, and source line");
    failures += expect_json_contains(json, "\"consumedFlags\":[\"CF\"]", "Phase 53C default consumer warning should list consumed flags");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 53C default consumer warning should still report completion");

    return failures;
}

/// Verifies Phase 53C keeps explicit undefined-flag-use opt-out behavior available.
///
/// @return Number of failures.
static int test_phase53c_undefined_flag_use_explicit_off_preserves_silent_behavior(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Explicit undefined flag-use off should continue execution");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "Explicit undefined flag-use off should use deterministic CF fallback");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "Explicit undefined flag-use off should preserve producer warning");
    failures += expect_json_not_contains(json, "undefined-flag-use", "Explicit undefined flag-use off should suppress consumer diagnostics");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Explicit undefined flag-use off should still report completion");

    return failures;
}

/// Verifies Phase 53 MUL source-run diagnostic paths.
///
/// @return Number of failures.
static int test_phase53_mul_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mul 5\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "MUL immediate source should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "MUL immediate source should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "MUL immediate source should use invalid-instruction-operands");
    failures += expect_json_contains(json, "MUL requires a register or memory source", "MUL immediate source diagnostic should explain source rule");
    failures += expect_json_not_contains(json, "execution-complete", "MUL immediate source must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mul eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "MUL two-operand form should use invalid-instruction-operands");
    failures += expect_json_contains(json, "MUL takes exactly one register or memory operand", "MUL two-operand diagnostic should explain operand count");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mul [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "MUL ambiguous memory source should use ambiguous-memory-width");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "MUL ambiguous memory source diagnostic should explain PTR requirement");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mul QWORD PTR q\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"unsupported-ptr-width\"", "MUL QWORD source should remain unsupported in MASM32 mode");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    mul DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "MUL invalid memory source should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "MUL invalid memory source should be an execution error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "MUL invalid memory source should use checked memory diagnostics");
    failures += expect_json_contains(json, "\"instructionCount\":1", "MUL invalid memory source should stop before executing MUL");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "MUL invalid memory source should leave EAX unchanged");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "MUL invalid memory source should not create memory changes");
    failures += expect_json_not_contains(json, "execution-complete", "MUL invalid memory source must not report completion");

    return failures;
}

/// Verifies Phase 54 IMUL acceptance behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase54_imul_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, -2\n"
        "    mov ebx, 3\n"
        "    imul ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 54 IMUL response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 54 IMUL acceptance source should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 54 IMUL acceptance source should execute three instructions");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "IMUL fitting signed product should sign-extend EDX");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFFAh\",\"unsigned\":4294967290}", "IMUL acceptance source should write EAX=FFFFFFFAh");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "IMUL fitting product should clear CF and OF in EFLAGS");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "IMUL register source should not create memory changes");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 54 IMUL source should complete successfully");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 0FEh\n"
        "    mov bl, 3\n"
        "    imul bl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000FFFAh\",\"unsigned\":65530}", "8-bit IMUL -2*3 should write AX=FFFAh");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "8-bit fitting IMUL should clear CF and OF in EFLAGS");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 7Fh\n"
        "    mov bl, 2\n"
        "    imul bl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"000000FEh\",\"unsigned\":254}", "8-bit IMUL significant truncation should write AX=00FEh");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000801h\",\"unsigned\":2049}", "8-bit IMUL significant truncation should set CF and OF in EFLAGS");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ax, 0FFFEh\n"
        "    mov bx, 3\n"
        "    imul bx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000FFFAh\",\"unsigned\":65530}", "16-bit IMUL -2*3 should write low AX");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"0000FFFFh\",\"unsigned\":65535}", "16-bit IMUL -2*3 should write high DX");

    return failures;
}

/// Verifies Phase 54 IMUL memory-source behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase54_imul_memory_source_run_program(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "values SDWORD 5, -3\n"
        ".CONST\n"
        "factor SDWORD -4\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 2\n"
        "    imul values[4]\n"
        "    mov esi, OFFSET factor\n"
        "    mov eax, 7\n"
        "    imul SDWORD PTR [esi]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "IMUL memory-source program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":6", "IMUL memory-source program should execute five instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFE4h\",\"unsigned\":4294967268}", "IMUL readable .CONST source should produce final EAX=-28");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"FFFFFFFFh\",\"unsigned\":4294967295}", "IMUL memory source fitting product should sign-extend EDX");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "IMUL memory-source reads should not create memory-change rows");
    failures += expect_json_not_contains(json, "permission-denied", "IMUL readable .CONST source should not trigger permission diagnostics");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "IMUL memory-source program should complete");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "x SDWORD -3\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 2\n"
        "    imul x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFFAh\",\"unsigned\":4294967290}", "IMUL direct symbol source should read signed data memory");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "IMUL direct symbol source should not write memory");

    return failures;
}

/// Verifies Phase 54 IMUL participates in default and strict uninitialized-read diagnostics for memory sources.
///
/// @return Number of failures.
static int test_phase54_imul_uninitialized_memory_source_warning(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 3\n"
        "    imul x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "IMUL default uninitialized-read warning should continue");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "IMUL memory source should emit default uninitialized-read warning");
    failures += expect_json_contains(json, "Memory read range 00500000h..00500003h reads 4 bytes from x + 0", "IMUL uninitialized-read warning should describe the memory source range");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "IMUL deterministic zero read should execute after warning");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "IMUL uninitialized source read should not write memory");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "IMUL warning mode should still complete execution");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 3\n"
        "    imul x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "IMUL strict uninitialized-read should stop execution");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "IMUL strict mode should report uninitialized-read");
    failures += expect_json_contains(json, "\"instructionCount\":1", "IMUL strict mode should stop before executing IMUL");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "IMUL strict mode should preserve EAX before mutation");
    failures += expect_json_not_contains(json, "execution-complete", "IMUL strict uninitialized-read should not report completion");

    return failures;
}

/// Verifies Phase 54 IMUL source-run diagnostic paths.
///
/// @return Number of failures.
static int test_phase54_imul_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul 5\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "IMUL immediate source should fail assembly");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "IMUL immediate source should be a parse error");
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "IMUL immediate source should use invalid-instruction-operands");
    failures += expect_json_contains(json, "IMUL requires a register or memory source", "IMUL immediate source diagnostic should explain source rule");
    failures += expect_json_not_contains(json, "execution-complete", "IMUL immediate source must not execute");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"ambiguous-memory-width\"", "IMUL ambiguous memory source should use ambiguous-memory-width");
    failures += expect_json_contains(json, "Memory operand width is ambiguous", "IMUL ambiguous memory source diagnostic should explain PTR requirement");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "q QWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    imul QWORD PTR q\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"unsupported-ptr-width\"", "IMUL QWORD source should remain unsupported in MASM32 mode");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    imul DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "IMUL invalid memory source should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "IMUL invalid memory source should be an execution error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "IMUL invalid memory source should use checked memory diagnostics");
    failures += expect_json_contains(json, "\"instructionCount\":1", "IMUL invalid memory source should stop before executing IMUL");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "IMUL invalid memory source should leave EAX unchanged");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "IMUL invalid memory source should not create memory changes");
    failures += expect_json_not_contains(json, "execution-complete", "IMUL invalid memory source must not report completion");

    return failures;
}
/// Verifies Phase 55 explicit-destination IMUL source-run success behavior.
///
/// @return Number of failures.
static int test_phase55_imul_source_run_programs(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 3\n"
        "    mov ebx, 4\n"
        "    imul eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 55 IMUL response should report current runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 55 two-operand IMUL should execute");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Phase 55 two-operand IMUL should execute three instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000000Ch\",\"unsigned\":12}", "3*4 two-operand IMUL should write EAX=12");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "two-operand IMUL should preserve source register");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "fitting two-operand IMUL should clear CF and OF");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ebx, 4\n"
        "    imul eax, ebx, -5\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 55 three-operand IMUL should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFECh\",\"unsigned\":4294967276}", "4*-5 three-operand IMUL should write EAX=-20 bits");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "three-operand IMUL should preserve source register");

    json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "factor SDWORD -3\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 7\n"
        "    imul eax, factor\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 55 memory-source IMUL should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"FFFFFFEBh\",\"unsigned\":4294967275}", "7*-3 memory-source IMUL should write EAX=-21 bits");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Phase 55 memory-source IMUL should not write memory");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 80000000h\n"
        "    mov ebx, -1\n"
        "    imul eax, ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 55 overflow IMUL should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"80000000h\",\"unsigned\":2147483648}", "overflow IMUL should store low result only");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000801h\",\"unsigned\":2049}", "overflow IMUL should set CF and OF");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 12345678h\n"
        "    mov bx, 3\n"
        "    imul ax, bx, -2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 55 16-bit three-operand IMUL should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"1234FFFAh\",\"unsigned\":305463290}", "16-bit IMUL should update AX only and preserve high EAX bits");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ebx, 1\n"
        "    imul eax, ebx, -2147483648\n"
        "    imul ecx, ebx, 2147483647\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 55 32-bit IMUL immediate boundaries should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"80000000h\",\"unsigned\":2147483648}", "IMUL should accept signed 32-bit minimum immediate");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"7FFFFFFFh\",\"unsigned\":2147483647}", "IMUL should accept signed 32-bit maximum immediate");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "IMUL boundary immediates that fit should clear CF and OF");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov bx, 1\n"
        "    imul ax, bx, -32768\n"
        "    imul cx, bx, 32767\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 55 16-bit IMUL immediate boundaries should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00008000h\",\"unsigned\":32768}", "IMUL should accept signed 16-bit minimum immediate");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00007FFFh\",\"unsigned\":32767}", "IMUL should accept signed 16-bit maximum immediate");

    return failures;
}

/// Verifies Phase 55 explicit-destination IMUL source-run diagnostics.
///
/// @return Number of failures.
static int test_phase55_imul_source_run_error_paths(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul al, bl\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "8-bit two-operand IMUL should fail assembly");
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "8-bit two-operand IMUL should use invalid-instruction-operands");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul eax, 5\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "IMUL reg, imm should use invalid-instruction-operands");
    failures += expect_json_contains(json, "This IMUL form is not accepted", "IMUL reg, imm diagnostic should explain rejected shape");
    failures += expect_json_not_contains(json, "not supported in Phase", "IMUL reg, imm diagnostic should not use milestone-relative wording");
    failures += expect_json_not_contains(json, "Phase 55", "IMUL reg, imm diagnostic should not mention historical Phase 55 wording");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul DWORD PTR [esi], eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "IMUL memory destination should use invalid-instruction-operands");
    failures += expect_json_contains(json, "require a 16-bit or 32-bit register destination", "IMUL memory destination diagnostic should explain destination rule");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul eax, ebx, ecx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "IMUL reg, reg, reg should use invalid-instruction-operands");
    failures += expect_json_contains(json, "signed immediate constant expression", "IMUL third register diagnostic should explain immediate requirement");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul eax, ebx, 5, 6\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"invalid-instruction-operands\"", "IMUL extra operands should use invalid-instruction-operands");
    failures += expect_json_contains(json, "takes exactly three operands", "IMUL extra operand diagnostic should explain operand count");
    failures += expect_json_contains(json, "\"line\":3", "IMUL extra operand diagnostic should preserve line");
    failures += expect_json_contains(json, "\"column\":21", "IMUL extra operand diagnostic should preserve column");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul eax, ebx, 2147483648\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"immediate-out-of-range\"", "IMUL out-of-range immediate should use immediate-out-of-range");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul eax, ebx, -2147483649\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"code\":\"immediate-out-of-range\"", "IMUL below-range 32-bit immediate should use immediate-out-of-range");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    imul ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ok\":false", "Phase 55 invalid memory source should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Phase 55 invalid memory source should be execution error");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "Phase 55 invalid memory source should use checked memory diagnostics");
    failures += expect_json_contains(json, "\"instructionCount\":1", "Phase 55 invalid memory source should stop before IMUL executes");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 55 invalid memory source should not mutate destination");

    return failures;
}




/// Verifies Phase 53D default compatibility notices are emitted and execution continues.
///
/// @return Number of failures.
static int test_phase53d_default_compatibility_notices_continue_execution(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".686\n"
        ".model flat, stdcall\n"
        ".stack 4096\n"
        "INCLUDE Macros.inc\n"
        "TITLE Notice Sample\n"
        "PAGE 60, 132\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 42\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 53D compatibility notice source should continue execution");
    failures += expect_json_contains(json, "\"kind\":\"simulator-notice\"", "Compatibility diagnostics should be simulator notices");
    failures += expect_json_contains(json, "compatibility-no-op", "Processor/listing compatibility notices should use compatibility-no-op");
    failures += expect_json_contains(json, "compatibility-metadata-only", ".stack compatibility notice should use compatibility-metadata-only");
    failures += expect_json_contains(json, "compatibility-limited", ".model and Macros.inc compatibility notices should use compatibility-limited");
    failures += expect_json_contains(json, ".686 is accepted for MASM compatibility", "Processor notice should name the accepted directive");
    failures += expect_json_contains(json, "source-level PUSH/POP stack transfers", ".stack notice should explain Phase 72A stack-transfer support");
    failures += expect_json_contains(json, "macro expansion", "Macros.inc notice should explain macro expansion limitation");
    failures += expect_json_contains(json, "TITLE is accepted as a listing/documentation directive", "Listing notice should name TITLE");
    failures += expect_json_contains(json, "PAGE is accepted as a listing/documentation directive", "Listing notice should name PAGE");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "Execution should still mutate registers normally");
    failures += expect_json_contains(json, "execution-complete", "Compatibility notices should be followed by successful completion");
    failures += expect_json_contains_once(json, ".686 is accepted", "Processor notice should name the accepted directive");

    return failures;
}

/// Verifies active semantic constructs do not receive generic Phase 53D notices.
///
/// @return Number of failures.
static int test_phase53d_active_semantics_do_not_emit_compatibility_notices(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        "OPTION CASEMAP:ALL\n"
        ".DATA?\n"
        "buf DWORD ?\n"
        ".CONST\n"
        "limit DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Active semantic source should execute successfully");
    failures += expect_json_contains(json, "\"irvine32\":true", "Irvine32 include should retain active virtual include metadata");
    failures += expect_json_not_contains(json, "compatibility-no-op", "Active semantic constructs should not emit generic no-op notices");
    failures += expect_json_not_contains(json, "compatibility-metadata-only", "Active data sections should not emit metadata-only notices");
    failures += expect_json_not_contains(json, "compatibility-limited", "Irvine32 include should not emit a generic limited-behavior notice");
    failures += expect_json_contains(json, "execution-complete", "Active semantic source should still complete");

    return failures;
}

/// Verifies Phase 53D notices do not downgrade true assembly errors.
///
/// @return Number of failures.
static int test_phase53d_notice_plus_error_still_blocks_execution(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".686\n"
        ".model small, c\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "A compatibility notice must not make an unsupported .model form executable");
    failures += expect_json_contains(json, "compatibility-no-op", "Earlier accepted no-op should still be reported as a notice");
    failures += expect_json_contains(json, "unsupported-model", "Unsupported .model should remain an assembly error");
    failures += expect_json_contains(json, "\"kind\":\"assembly-error\"", "Unsupported .model should retain assembly-error category");
    failures += expect_json_not_contains(json, "execution-complete", "Programs with assembly errors must not execute");
    failures += expect_json_not_contains(json, "\"EAX\":{\"hex\":\"00000001h\"", "Blocked program should not expose mutated EAX");

    return failures;
}

/// Verifies Phase 53E browser-facing settings map to existing backend policies.
///
/// @return Number of failures.
static int test_phase53e_ui_settings_route_to_existing_policies(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".686\n"
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 53E default UI settings should allow execution");
    failures += expect_json_contains(json, "compatibility-no-op", "Phase 53E default UI settings should keep compatibility notices on");
    failures += expect_json_contains(json, "uninitialized-read", "Phase 53E default UI settings should keep uninitialized-read warnings on");
    failures += expect_json_contains(json, "execution-complete", "Phase 53E default UI settings should complete when only warnings/notices occur");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".686\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 7\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF
    );
    failures += expect_json_contains(json, "\"ok\":true", "Compatibility-notice opt-out should still execute valid source");
    failures += expect_json_not_contains(json, "compatibility-no-op", "Compatibility-notice opt-out should suppress no-op notices");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "Compatibility-notice opt-out should not affect execution");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_OFF,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":true", "Uninitialized-read off should continue execution");
    failures += expect_json_not_contains(json, "uninitialized-read", "Uninitialized-read off should suppress default teaching warning");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Uninitialized-read off should preserve deterministic zero-filled read");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":false", "Uninitialized-read strict UI setting should stop execution");
    failures += expect_json_contains(json, "\"kind\":\"runtime-error\"", "Uninitialized-read strict UI setting should produce runtime error");
    failures += expect_json_contains(json, "uninitialized-read", "Uninitialized-read strict UI setting should use existing diagnostic code");
    failures += expect_json_not_contains(json, "execution-complete", "Uninitialized-read strict UI setting must not report completion");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_OFF,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":true", "Undefined-flag-use off should continue execution");
    failures += expect_json_contains(json, "undefined-shift-flag", "Undefined-flag-use off should not suppress producer warnings");
    failures += expect_json_not_contains(json, "undefined-flag-use", "Undefined-flag-use off should suppress consumer diagnostics");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_STRICT,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":false", "Undefined-flag-use strict UI setting should stop execution");
    failures += expect_json_contains(json, "undefined-flag-use", "Undefined-flag-use strict UI setting should use existing diagnostic code");
    failures += expect_json_contains(json, "Execution stopped before using the undefined flag", "Undefined-flag-use strict UI setting should use existing error wording");
    failures += expect_json_not_contains(json, "execution-complete", "Undefined-flag-use strict UI setting must not report completion");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".code\n"
        "main PROC\n"
        "    mov eax, 00700000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":true", "Section-capacity warning UI setting should continue execution");
    failures += expect_json_contains(json, "section-capacity-violation", "Section-capacity warning UI setting should use existing diagnostic code");
    failures += expect_json_contains(json, "execution-complete", "Section-capacity warning UI setting should complete");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".code\n"
        "main PROC\n"
        "    mov eax, 00700000h\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_SECTION_CAPACITY_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":false", "Section-capacity strict UI setting should stop execution");
    failures += expect_json_contains(json, "section-capacity-violation", "Section-capacity strict UI setting should use existing diagnostic code");
    failures += expect_json_not_contains(json, "execution-complete", "Section-capacity strict UI setting must not report completion");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":true", "Section-image warning UI setting should continue execution");
    failures += expect_json_contains(json, "section-image-violation", "Section-image warning UI setting should use existing diagnostic code");
    failures += expect_json_contains(json, "execution-complete", "Section-image warning UI setting should complete");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    mov DWORD PTR [eax], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_SECTION_IMAGE_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":false", "Section-image strict UI setting should stop execution");
    failures += expect_json_contains(json, "section-image-violation", "Section-image strict UI setting should use existing diagnostic code");
    failures += expect_json_not_contains(json, "execution-complete", "Section-image strict UI setting must not report completion");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".data\n"
        "x DWORD 1\n"
        "y DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR [x+1]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":true", "Declared-object warning UI setting should continue execution");
    failures += expect_json_contains(json, "object-bounds-warning", "Declared-object warning UI setting should use existing object diagnostic");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".data\n"
        "x DWORD 1\n"
        "y DWORD 2\n"
        ".code\n"
        "main PROC\n"
        "    mov DWORD PTR [x+1], 123\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    failures += expect_json_contains(json, "\"ok\":false", "Declared-object strict UI setting should stop execution");
    failures += expect_json_contains(json, "object-bounds-violation", "Declared-object strict UI setting should use existing object diagnostic");
    failures += expect_json_not_contains(json, "execution-complete", "Declared-object strict UI setting must not report completion");

    return failures;
}


/// Verifies Phase 57D policy migration preserves independent family selection.
///
/// @return Number of failures.
static int test_phase57d_ui_policy_families_remain_independent(void) {
    const char *source =
        ".686\n"
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    const char *json = masm32_sim_wasm_run_source_json_with_ui_settings(
        source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_OFF,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 57D uninitialized-read off case should execute successfully");
    failures += expect_json_not_contains(json, "uninitialized-read", "Uninitialized-read off should not suppress other policy families");
    failures += expect_json_contains(json, "undefined-flag-use", "Undefined-flag-use warn should remain active when uninitialized reads are off");
    failures += expect_json_not_contains(json, "compatibility-no-op", "Compatibility-notice off should suppress compatibility notices only");
    failures += expect_json_contains(json, "execution-complete", "Independent warning families should still allow completion");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_OFF,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF
    );
    failures += expect_json_contains(json, "\"ok\":true", "Phase 57D undefined-flag-use off case should execute successfully");
    failures += expect_json_contains(json, "uninitialized-read", "Uninitialized-read warn should remain active when undefined-flag-use is off");
    failures += expect_json_contains(json, "undefined-shift-flag", "Producer warning should remain active when undefined-flag-use is off");
    failures += expect_json_not_contains(json, "undefined-flag-use", "Undefined-flag-use off should suppress only consumer diagnostics");
    failures += expect_json_not_contains(json, "compatibility-no-op", "Compatibility-notice off should remain independent");

    json = masm32_sim_wasm_run_source_json_with_ui_settings(
        source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF
    );
    failures += expect_json_contains(json, "uninitialized-read", "Compatibility-notice off should not suppress uninitialized-read warnings");
    failures += expect_json_contains(json, "undefined-flag-use", "Compatibility-notice off should not suppress undefined-flag-use warnings");
    failures += expect_json_not_contains(json, "compatibility-no-op", "Compatibility-notice off should suppress only compatibility notices");

    return failures;
}


/// Verifies Phase 57E emits the deterministic startup-state notice by default.
///
/// @return Number of failures.
static int test_phase57e_default_startup_state_notice_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 20\n"
        "    add eax, 22\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Startup-state notice regression should report current numeric phase metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Startup-state notice should not block execution");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "Startup-state notice should not change register results");
    failures += expect_json_contains(json, "\"kind\":\"simulator-notice\"", "Startup-state notice should render as simulator notice");
    failures += expect_json_contains(json, "\"code\":\"startup-state-notice\"", "Startup-state notice should use stable diagnostic code");
    failures += expect_json_contains(json, TEST_STARTUP_STATE_NOTICE_TEXT, "Startup-state notice should use stable exact wording");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Startup-state notice should still allow completion message");
    failures += expect_json_not_contains(json, "Program Console", "Startup-state notice must not be Program Console output");
    failures += expect_json_not_contains(json, "\"line\":", "Source-less startup-state notice fixture should not add line metadata");
    failures += expect_json_not_contains(json, "\"byteOffset\":", "Source-less startup-state notice fixture should not add byte offset metadata");

    return failures;
}

/// Verifies Phase 57E startup-state notice opt-out preserves execution behavior.
///
/// @return Number of failures.
static int test_phase57e_startup_state_notice_opt_out_source_run(void) {
    const char *source =
        ".code\n"
        "main PROC\n"
        "    mov eax, 7\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    const char *json = masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(
        source,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Startup-state notice opt-out should still execute successfully");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "Startup-state notice opt-out should not alter register results");
    failures += expect_json_not_contains(json, "startup-state-notice", "Startup-state notice opt-out should suppress only that notice");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Startup-state notice opt-out should preserve completion message");

    return failures;
}

/// Verifies Phase 57E startup-state notice opt-out is independent of other diagnostics.
///
/// @return Number of failures.
static int test_phase57e_startup_state_notice_opt_out_keeps_other_diagnostics(void) {
    const char *source =
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    const char *json = masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(
        source,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Startup-state notice opt-out should allow warning-only execution");
    failures += expect_json_not_contains(json, "startup-state-notice", "Startup-state opt-out should suppress the startup notice");
    failures += expect_json_contains(json, "uninitialized-read", "Startup-state opt-out should not suppress uninitialized-read diagnostics");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Warning-only run should still complete");

    return failures;
}

/// Verifies Phase 57E preserves uninitialized-origin metadata helpers.
///
/// @return Number of failures.
static int test_phase57e_startup_state_notice_preserves_uninitialized_origin_metadata(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_uninitialized_metadata(
        ".DATA?\n"
        "x DWORD ?\n"
        ".data\n"
        "y DWORD 5\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 57E metadata regression should execute successfully");
    failures += expect_json_contains(json, "\"uninitializedOrigin\":{\"tracked\":true", "Phase 57E should preserve test-only uninitialized-origin metadata output");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", "Phase 57E should preserve uninitialized-origin metadata for DWORD ? storage");
    failures += expect_json_contains(json, "\"symbol\":\"y\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "Phase 57E should preserve initialized-origin metadata for explicit DWORD storage");
    failures += expect_json_not_contains(json, "startup-state-notice", "metadata helper should preserve its test-facing startup-notice opt-out behavior");

    return failures;
}

/// Verifies Phase 57E startup-state notice settings reject invalid enum values.
///
/// @return Number of failures.
static int test_phase57e_invalid_startup_state_notice_setting(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        (Masm32SimWasmStartupStateNoticeSetting)99
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Invalid startup-state notice setting should fail");
    failures += expect_json_contains(json, "\"status\":\"invalid-argument\"", "Invalid startup-state notice setting should return invalid argument status");
    failures += expect_json_contains(json, "\"code\":\"invalid-source\"", "Invalid setting should use existing invalid-source diagnostic path");

    return failures;
}

/// Verifies Phase 57H reports no program register-family writes for a no-instruction run.
///
/// @return Number of failures.
static int test_phase57h_register_write_metadata_no_register_writes(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"registerWrites\":{\"EAX\":false", "No-instruction run should report EAX family as unwritten");
    failures += expect_json_contains(json, "\"EBX\":false", "No-instruction run should report EBX family as unwritten");
    failures += expect_json_contains(json, "\"ECX\":false", "No-instruction run should report ECX family as unwritten");
    failures += expect_json_contains(json, "\"EDX\":false", "No-instruction run should report EDX family as unwritten");
    failures += expect_json_contains(json, "\"ESI\":false", "No-instruction run should report ESI family as unwritten");
    failures += expect_json_contains(json, "\"EDI\":false", "No-instruction run should report EDI family as unwritten");
    failures += expect_json_contains(json, "\"EBP\":false", "No-instruction run should report EBP family as unwritten");
    failures += expect_json_contains(json, "\"ESP\":false", "No-instruction run should report ESP family as unwritten");
    failures += expect_json_contains(json, "\"EIP\":false", "No-instruction run should report EIP family as unwritten");
    failures += expect_json_contains(json, "\"EFLAGS\":false", "No-instruction run should report EFLAGS family as unwritten");
    failures += expect_json_contains(json, "\"code\":\"startup-state-notice\"", "Phase 57H metadata should not remove the startup-state notice");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 57H metadata should not remove execution-complete messages");
    failures += expect_json_not_contains(json, "programConsole", "Phase 57H metadata should not add Program Console output");

    return failures;
}

/// Verifies Phase 57H write tracking treats same-value parent writes as changed.
///
/// @return Number of failures.
static int test_phase57h_register_write_metadata_same_value_parent_write(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0\n"
        "    ret\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Same-value MOV should leave EAX final value at zero");
    failures += expect_json_contains(json, "\"registerWrites\":{\"EAX\":true", "Same-value MOV should mark EAX family as written");
    failures += expect_json_contains(json, "\"EBX\":false", "Unwritten EBX family should remain marked false");
    failures += expect_json_contains(json, "\"code\":\"startup-state-notice\"", "Same-value write metadata should preserve startup-state notice output");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Same-value write metadata should preserve execution-complete output");
    failures += expect_json_not_contains(json, "programConsole", "Same-value write metadata should not add Program Console output");

    return failures;
}

/// Verifies Phase 57H write tracking maps alias writes to canonical families.
///
/// @return Number of failures.
static int test_phase57h_register_write_metadata_alias_writes(void) {
    int failures = 0;
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ax, 0\n"
        "    ret\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"registerWrites\":{\"EAX\":true", "AX write should mark EAX family as written");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ah, 0\n"
        "    ret\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"registerWrites\":{\"EAX\":true", "AH write should mark EAX family as written");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov al, 0\n"
        "    ret\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"registerWrites\":{\"EAX\":true", "AL write should mark EAX family as written");

    json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov si, 0\n"
        "    mov sp, 0\n"
        "    ret\n"
        "END main\n"
    );
    failures += expect_json_contains(json, "\"ESI\":true", "SI write should mark ESI family as written");
    failures += expect_json_contains(json, "\"ESP\":true", "SP write should mark ESP family as written");
    failures += expect_json_contains(json, "\"EAX\":false", "Alias writes to SI/SP should not mark EAX");

    return failures;
}

/// Verifies Phase 57H startup initialization does not count as a program write.
///
/// @return Number of failures.
static int test_phase57h_seeded_startup_not_counted_as_program_write(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        123456789U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"registerWrites\":{\"EAX\":false", "Seeded startup should not mark EAX as program-written");
    failures += expect_json_contains(json, "\"EFLAGS\":false", "Seeded startup should not mark EFLAGS as program-written");
    failures += expect_json_not_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded startup fixture should still prove final values are not value-equality markers");

    return failures;
}


/// Verifies Phase 57R INVOKE/ADDR/external routine diagnostics through source-run JSON.
///
/// @return Number of failures.
static int test_phase57r_invoke_addr_external_routine_source_run_diagnostics(void) {
    char stdout_copy[4096];
    char crt_copy[4096];
    char exitprocess_copy[4096];
    char multi_copy[8192];
    char virtual_exit_copy[4096];
    int failures = 0;

    copy_source_run_json(stdout_copy, sizeof(stdout_copy), masm32_sim_wasm_run_source_json(
        ".data\n"
        "titleMsg BYTE \"Hello\", 0\n"
        ".code\n"
        "main PROC\n"
        "    invoke StdOut, addr titleMsg\n"
        "    mov eax, 42\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(crt_copy, sizeof(crt_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    invoke crt_printf, addr numberFmt, counter\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(exitprocess_copy, sizeof(exitprocess_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    invoke ExitProcess, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(multi_copy, sizeof(multi_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    invoke StdOut, addr titleMsg\n"
        "    invoke crt_printf, addr numberFmt, counter\n"
        "    invoke ExitProcess, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(virtual_exit_copy, sizeof(virtual_exit_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    exit\n"
        "    mov eax, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(stdout_copy, "\"ok\":false", "StdOut INVOKE should fail before execution");
    failures += expect_json_contains(stdout_copy, "unsupported-invoke", "StdOut source should expose unsupported-invoke");
    failures += expect_json_contains(stdout_copy, "unsupported-addr", "StdOut source should expose unsupported-addr");
    failures += expect_json_contains(stdout_copy, "unsupported-masm32-runtime-routine", "StdOut source should expose MASM32 runtime code");
    failures += expect_json_contains(stdout_copy, "MASM32 runtime", "StdOut source should describe MASM32 runtime boundary");
    failures += expect_json_contains(stdout_copy, "\"line\":5", "StdOut source should preserve INVOKE line");
    failures += expect_json_contains(stdout_copy, "\"column\":20", "StdOut source should point at ADDR token");
    failures += expect_json_not_contains(stdout_copy, "execution-complete", "StdOut INVOKE should not execute");
    failures += expect_json_not_contains(stdout_copy, "programConsole", "StdOut INVOKE should not produce Program Console output");

    failures += expect_json_contains(crt_copy, "unsupported-crt-routine", "CRT source should expose CRT routine code");
    failures += expect_json_contains(crt_copy, "C runtime", "CRT source should describe C runtime boundary");
    failures += expect_json_contains(crt_copy, "unsupported-addr", "CRT source should report ADDR");
    failures += expect_json_not_contains(crt_copy, "unknown-symbol", "CRT INVOKE should not cascade into unknown symbols");

    failures += expect_json_contains(exitprocess_copy, "unsupported-winapi-execution", "ExitProcess INVOKE should expose WinAPI code");
    failures += expect_json_contains(exitprocess_copy, "not the virtual Irvine32 exit", "ExitProcess message should distinguish virtual exit");
    failures += expect_json_not_contains(exitprocess_copy, "\"code\":\"unsupported-irvine32-routine\"", "ExitProcess INVOKE should not use bare Irvine32 routine diagnostic");
    failures += expect_json_not_contains(exitprocess_copy, "execution-complete", "ExitProcess INVOKE should not execute");

    failures += expect_json_contains(multi_copy, "unsupported-masm32-runtime-routine", "multi INVOKE should include MASM32 runtime diagnostic");
    failures += expect_json_contains(multi_copy, "unsupported-crt-routine", "multi INVOKE should include CRT diagnostic");
    failures += expect_json_contains(multi_copy, "unsupported-winapi-execution", "multi INVOKE should include WinAPI diagnostic");
    failures += expect_json_contains(multi_copy, "\"line\":3", "multi INVOKE should preserve first line");
    failures += expect_json_contains(multi_copy, "\"line\":4", "multi INVOKE should preserve second line");
    failures += expect_json_contains(multi_copy, "\"line\":5", "multi INVOKE should preserve third line");
    failures += expect_json_not_contains(multi_copy, "unknown-symbol", "multi INVOKE should avoid token cascades");

    failures += expect_json_contains(virtual_exit_copy, "\"ok\":true", "Virtual Irvine32 exit should remain executable");
    failures += expect_json_contains(virtual_exit_copy, "execution-complete", "Virtual Irvine32 exit should still complete");
    failures += expect_json_contains(virtual_exit_copy, "\"EAX\":{\"hex\":\"00000001h\"", "Virtual Irvine32 exit should stop before later instructions");
    failures += expect_json_not_contains(virtual_exit_copy, "unsupported-winapi-execution", "Virtual exit should not be classified as WinAPI");

    return failures;
}


/// Verifies Phase 57T realistic playground-program diagnostics stay concise.
///
/// @return Number of failures.
static int test_phase57t_playground_diagnostic_recovery_smoke_fixtures(void) {
    char playground_copy[12000];
    char call_copy[4096];
    char ret_copy[4096];
    char cmp_copy[4096];
    char conditional_jump_copy[4096];
    char exitprocess_copy[4096];
    int failures = 0;

    copy_source_run_json(playground_copy, sizeof(playground_copy), masm32_sim_wasm_run_source_json(
        ".386\n"
        ".model flat, stdcall\n"
        "option casemap:none\n"
        "\n"
        "include \\masm32\\include\\masm32.inc\n"
        "include \\masm32\\include\\kernel32.inc\n"
        "\n"
        "includelib \\masm32\\lib\\masm32.lib\n"
        "includelib \\masm32\\lib\\kernel32.lib\n"
        "\n"
        ".data\n"
        "    titleMsg   db \"=== MASM32 Playground ===\",13,10,0\n"
        "    startMsg   db \"Counting from 1 to 5\",13,10,0\n"
        "    evenMsg    db \" -> even number\",13,10,0\n"
        "    oddMsg     db \" -> odd number\",13,10,0\n"
        "    numberFmt  db \"Number: %d\",13,10,0\n"
        "    counter    dd 1\n"
        "    total      dd 0\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    invoke StdOut, addr titleMsg\n"
        "    invoke StdOut, addr startMsg\n"
        "    invoke crt_printf, addr numberFmt, counter\n"
        "\n"
        "    mov eax, counter\n"
        "    and eax, 1\n"
        "\n"
        "    .IF eax == 0\n"
        "        invoke StdOut, addr evenMsg\n"
        "    .ELSE\n"
        "        invoke StdOut, addr oddMsg\n"
        "    .ENDIF\n"
        "\n"
        "    mov eax, counter\n"
        "    call AddToTotal\n"
        "\n"
        "    inc counter\n"
        "\n"
        "    cmp counter, 6\n"
        "    jl main_loop\n"
        "\n"
        "    invoke crt_printf, addr numberFmt, total\n"
        "    invoke ExitProcess, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    copy_source_run_json(call_copy, sizeof(call_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call AddToTotal\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(ret_copy, sizeof(ret_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(cmp_copy, sizeof(cmp_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    cmp eax, 6\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(conditional_jump_copy, sizeof(conditional_jump_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    loop main_loop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    copy_source_run_json(exitprocess_copy, sizeof(exitprocess_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    invoke ExitProcess, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(playground_copy, "\"ok\":false", "Phase 57T playground fixture should refuse execution");
    failures += expect_json_contains(playground_copy, "unsupported-masm32-library-include", "Phase 57T playground fixture should classify MASM32 host include");
    failures += expect_json_contains(playground_copy, "unsupported-windows-api-include", "Phase 57T playground fixture should classify Windows host include");
    failures += expect_json_contains(playground_copy, "unsupported-masm32-library", "Phase 57T playground fixture should classify MASM32 INCLUDELIB");
    failures += expect_json_contains(playground_copy, "unsupported-windows-api-library", "Phase 57T playground fixture should classify Windows INCLUDELIB");
    failures += expect_json_contains(playground_copy, "does not link objects, load .lib files, process PE imports, or execute external routines", "Phase 57T INCLUDELIB wording should remain linker/import-library non-goal wording");
    failures += expect_json_contains(playground_copy, "unsupported-invoke", "Phase 57T playground fixture should classify INVOKE");
    failures += expect_json_contains(playground_copy, "unsupported-addr", "Phase 57T playground fixture should classify ADDR operands");
    failures += expect_json_contains(playground_copy, "unsupported-masm32-runtime-routine", "Phase 57T playground fixture should classify StdOut as external MASM32 runtime");
    failures += expect_json_contains(playground_copy, "unsupported-crt-routine", "Phase 57T playground fixture should classify crt_printf as CRT routine");
    failures += expect_json_contains(playground_copy, "unsupported-high-level-if", "Phase 57T playground fixture should classify .IF high-level flow");
    failures += expect_json_contains(playground_copy, "unsupported-high-level-else", "Phase 57T playground fixture should classify .ELSE high-level flow");
    failures += expect_json_contains(playground_copy, "unsupported-high-level-endif", "Phase 57T playground fixture should classify .ENDIF high-level flow");
    failures += expect_json_contains(playground_copy, "unknown-symbol", "Phase 57T playground fixture should keep unresolved CALL target diagnostics after Phase 69 direct CALL support");
    failures += expect_json_not_contains(playground_copy, "unexpected-character", "Phase 57T playground fixture should not flood path separators as character diagnostics");
    failures += expect_json_not_contains(playground_copy, "lexer-failed", "Phase 57T playground fixture should not collapse into lexer-failed");
    failures += expect_json_not_contains(playground_copy, "execution-complete", "Phase 57T playground fixture should not report execution completion");
    failures += expect_json_not_contains(playground_copy, "programConsole", "Phase 57T playground fixture should not produce Program Console output");
    failures += expect_json_not_contains(playground_copy, "missing local file", "Phase 57T INCLUDELIB diagnostics should not imply local file probing");

    failures += expect_json_contains(call_copy, "unknown-symbol", "Phase 57T unresolved CALL fixture should use current unknown-symbol classification");
    failures += expect_json_contains(call_copy, "CALL target is not a known user procedure entry.", "Phase 57T unresolved CALL fixture should report a missing user procedure target");
    failures += expect_json_not_contains(call_copy, "execution-complete", "Phase 57T CALL fixture should not execute");
    failures += expect_json_not_contains(call_copy, "programConsole", "Phase 57T CALL fixture should not produce Program Console output");

    failures += expect_json_contains(ret_copy, "\"ok\":true", "Phase 71 root RET fixture should complete");
    failures += expect_json_contains(ret_copy, "\"code\":\"execution-complete\"", "Phase 71 root RET fixture should emit completion");
    failures += expect_json_contains(ret_copy, "\"memoryChanges\":[]", "Phase 71 root RET fixture should not expose memory changes");
    failures += expect_json_not_contains(ret_copy, "invalid-address", "Phase 71 root RET fixture should not read empty-stack memory");
    failures += expect_json_not_contains(ret_copy, "invalid-return-address", "Phase 71 root RET fixture should not validate a return token");
    failures += expect_json_not_contains(ret_copy, "programConsole", "Phase 71 root RET fixture should not produce Program Console output");

    failures += expect_json_contains(cmp_copy, "\"ok\":true", "Phase 62 CMP register/immediate fixture should execute successfully");
    failures += expect_json_contains(cmp_copy, "\"instructionCount\":2", "Phase 62 CMP register/immediate fixture should emit one instruction");
    failures += expect_json_contains(cmp_copy, "\"EFLAGS\":{\"hex\":\"00000081h\",\"unsigned\":129}", "Phase 62 CMP register/immediate fixture should update subtraction flags");
    failures += expect_json_contains(cmp_copy, "execution-complete", "Phase 62 CMP register/immediate fixture should complete");
    failures += expect_json_not_contains(cmp_copy, "programConsole", "Phase 62 CMP register/immediate fixture should not produce Program Console output");

    failures += expect_json_contains(conditional_jump_copy, "unsupported-instruction", "Phase 57T loop fixture should use current unsupported-instruction classification");
    failures += expect_json_contains(conditional_jump_copy, "spanLength\":4", "Phase 57T LOOP fixture should preserve the loop mnemonic span");
    failures += expect_json_not_contains(conditional_jump_copy, "execution-complete", "Phase 57T loop fixture should not execute");
    failures += expect_json_not_contains(conditional_jump_copy, "programConsole", "Phase 57T loop fixture should not produce Program Console output");

    failures += expect_json_contains(exitprocess_copy, "unsupported-winapi-execution", "Phase 57T WinAPI fixture should classify ExitProcess as WinAPI boundary");
    failures += expect_json_contains(exitprocess_copy, "not the virtual Irvine32 exit", "Phase 57T WinAPI fixture should distinguish ExitProcess from virtual exit");
    failures += expect_json_not_contains(exitprocess_copy, "execution-complete", "Phase 57T WinAPI fixture should not execute");
    failures += expect_json_not_contains(exitprocess_copy, "programConsole", "Phase 57T WinAPI fixture should not produce Program Console output");

    return failures;
}


/// Verifies Phase 58 labels integrate with source-run without changing straight-line execution.
///
/// @return Number of failures.
static int test_phase58_label_source_run_behavior(void) {
    char labeled_copy[TEST_JSON_COPY_CAPACITY];
    char consecutive_copy[TEST_JSON_COPY_CAPACITY];
    char duplicate_copy[TEST_JSON_COPY_CAPACITY];
    char jmp_copy[TEST_JSON_COPY_CAPACITY];
    char loop_copy[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    copy_source_run_json(
        labeled_copy,
        sizeof(labeled_copy),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "start:\n"
            "    mov eax, 1\n"
            "next:\n"
            "    mov ebx, 2\n"
            "done:\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        consecutive_copy,
        sizeof(consecutive_copy),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "first:\n"
            "second:\n"
            "    mov eax, 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        duplicate_copy,
        sizeof(duplicate_copy),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "start:\n"
            "    mov eax, 1\n"
            "start:\n"
            "    mov ebx, 2\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        jmp_copy,
        sizeof(jmp_copy),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov eax, 1\n"
            "    jmp done\n"
            "    mov ebx, 2\n"
            "done:\n"
            "    mov ecx, 3\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        loop_copy,
        sizeof(loop_copy),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "start:\n"
            "    mov ecx, 1\n"
            "    loop start\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );

    failures += expect_json_contains(labeled_copy, "\"phase\":77", "Labeled source should report numeric current runtime phase metadata");
    failures += expect_json_contains(labeled_copy, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "Labeled source should report current runtime phase name");
    failures += expect_json_contains(labeled_copy, "\"ok\":true", "valid labeled source should execute");
    failures += expect_json_contains(labeled_copy, "\"instructionCount\":3", "labels should not emit extra executable instructions");
    failures += expect_json_contains(labeled_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1", "labeled source should set EAX");
    failures += expect_json_contains(labeled_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2", "labeled source should set EBX");
    failures += expect_json_contains(labeled_copy, "\"memoryChanges\":[]", "labels-only source should not create memory changes");
    failures += expect_json_not_contains(labeled_copy, "programConsole", "labels should not create Program Console output");

    failures += expect_json_contains(consecutive_copy, "\"ok\":true", "consecutive labels should execute");
    failures += expect_json_contains(consecutive_copy, "\"instructionCount\":2", "consecutive labels should execute following instruction once");
    failures += expect_json_contains(consecutive_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1", "consecutive labels should leave EAX = 1");

    failures += expect_json_contains(duplicate_copy, "\"ok\":false", "duplicate label source should not execute");
    failures += expect_json_contains(duplicate_copy, "duplicate-label", "duplicate label source should report duplicate-label diagnostic");
    failures += expect_json_contains(duplicate_copy, "first defined at line 3, column 1", "duplicate label message should include prior definition location");
    failures += expect_json_not_contains(duplicate_copy, "execution-complete", "duplicate label source should not report execution completion");
    failures += expect_json_not_contains(duplicate_copy, "programConsole", "duplicate label source should not create Program Console output");

    failures += expect_json_contains(jmp_copy, "\"ok\":true", "jmp label source should execute in Phase 61");
    failures += expect_json_contains(jmp_copy, "\"instructionCount\":4", "jmp label source should commit MOV, JMP, and target MOV");
    failures += expect_json_contains(jmp_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0", "jmp label source should skip fall-through instruction");
    failures += expect_json_contains(jmp_copy, "\"ECX\":{\"hex\":\"00000003h\",\"unsigned\":3", "jmp label source should execute branch target");
    failures += expect_json_not_contains(jmp_copy, "branch-runtime-deferred", "jmp label source should no longer use Phase 60 deferred branch diagnostic");
    failures += expect_json_not_contains(jmp_copy, "unsupported-instruction", "jmp label source should no longer use unsupported-instruction diagnostic");
    failures += expect_json_contains(jmp_copy, "execution-complete", "jmp label source should execute branch behavior");

    failures += expect_json_contains(loop_copy, "\"ok\":false", "LOOP instruction source should remain unsupported");
    failures += expect_json_contains(loop_copy, "unsupported-instruction", "LOOP instruction source should use existing unsupported-instruction diagnostic");
    failures += expect_json_not_contains(loop_copy, "execution-complete", "LOOP instruction source should not execute LOOP behavior");

    return failures;
}

/// Verifies Phase 64 equality conditional jumps execute with CMP-produced ZF.
///
/// @return Number of failures.
static int test_phase64_equality_jumps_source_run_programs(void) {
    char acceptance_json[8192];
    char not_taken_json[8192];
    char jne_json[8192];
    char alias_json[8192];
    int failures = 0;

    copy_source_run_json(acceptance_json, sizeof(acceptance_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    cmp eax, 5\n"
        "    je equal\n"
        "    mov ebx, 1\n"
        "    jmp done\n"
        "equal:\n"
        "    mov ebx, 2\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(not_taken_json, sizeof(not_taken_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    cmp eax, 6\n"
        "    je equal\n"
        "    mov ebx, 1\n"
        "    jmp done\n"
        "equal:\n"
        "    mov ebx, 2\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(jne_json, sizeof(jne_json), masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 6\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    cmp eax, value\n"
        "    jne notequal\n"
        "    mov ebx, 1\n"
        "    jmp done\n"
        "notequal:\n"
        "    mov ebx, 3\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(alias_json, sizeof(alias_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 7\n"
        "    cmp eax, 7\n"
        "    jz zero\n"
        "    mov ebx, 1\n"
        "zero:\n"
        "    cmp eax, 8\n"
        "    jnz done\n"
        "    mov ebx, 2\n"
        "done:\n"
        "    mov ecx, 4\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(acceptance_json, "\"ok\":true", "Phase 64 equality-jump acceptance program should execute");
    failures += expect_json_contains(acceptance_json, "\"phase\":77", "Equality-jump regression should report current numeric phase metadata");
    failures += expect_json_contains(acceptance_json, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "Equality-jump regression should report current runtime phase name");
    failures += expect_json_contains(acceptance_json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2", "taken JE should execute equal target");
    failures += expect_json_contains(acceptance_json, "\"instructionCount\":6", "taken JE acceptance path should commit MOV, CMP, JE, target MOV, and done NOP");
    failures += expect_json_contains(acceptance_json, "\"memoryChanges\":[]", "taken JE should not create memory changes");
    failures += expect_json_not_contains(acceptance_json, "branch-runtime-deferred", "valid JE should not emit deferred branch diagnostic");

    failures += expect_json_contains(not_taken_json, "\"ok\":true", "not-taken JE program should execute");
    failures += expect_json_contains(not_taken_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "not-taken JE should execute fall-through MOV");
    failures += expect_json_contains(jne_json, "\"ok\":true", "JNE after CMP memory source should execute");
    failures += expect_json_contains(jne_json, "\"EBX\":{\"hex\":\"00000003h\",\"unsigned\":3", "taken JNE should execute notequal target");
    failures += expect_json_contains(alias_json, "\"ok\":true", "JZ/JNZ alias program should execute");
    failures += expect_json_contains(alias_json, "\"ECX\":{\"hex\":\"00000004h\",\"unsigned\":4", "JZ and JNZ aliases should reach final target");
    failures += expect_json_not_contains(alias_json, "branch-runtime-deferred", "valid JZ/JNZ aliases should not emit deferred branch diagnostics");

    return failures;
}

/// Verifies Phase 64 backward conditional jump loops reach the instruction limit.
///
/// @return Number of failures.
static int test_phase64_equality_jump_instruction_limit(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "start:\n"
        "    cmp eax, eax\n"
        "    je start\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        5U
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "backward JE loop should stop at instruction limit");
    failures += expect_json_contains(json, "\"code\":\"instruction-limit-exceeded\"", "backward JE loop should report instruction-limit-exceeded");
    failures += expect_json_contains(json, "\"instructionCount\":5", "backward JE loop should commit exactly the configured instruction limit");
    failures += expect_json_contains(json, "\"attemptedNextInstructionIndex\":1", "instruction limit should identify blocked JE fetch after CMP");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "valid backward JE should not emit deferred diagnostic");
    failures += expect_json_not_contains(json, "execution-complete", "instruction-limit failure should not emit execution-complete");

    return failures;
}

/// Verifies Phase 64 equality conditional jump target diagnostics in source-run JSON.
///
/// @return Number of failures.
static int test_phase64_equality_jump_source_run_diagnostics(void) {
    char unknown_json[8192];
    char data_json[8192];
    char exit_json[8192];
    char register_json[8192];
    char memory_json[8192];
    char immediate_json[8192];
    int failures = 0;

    copy_source_run_json(unknown_json, sizeof(unknown_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    je missing\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(data_json, sizeof(data_json), masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    jnz value\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(exit_json, sizeof(exit_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    je exit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(register_json, sizeof(register_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    je eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(memory_json, sizeof(memory_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jz [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(immediate_json, sizeof(immediate_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jne 1234h\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(unknown_json, "\"ok\":false", "unknown JE target should fail parse/source-run");
    failures += expect_json_contains(unknown_json, "\"code\":\"invalid-branch-target\"", "unknown JE target should emit invalid-branch-target");
    failures += expect_json_contains(data_json, "Direct conditional jumps accept only code labels", "data-target JNZ diagnostic should name conditional jump target rules");
    failures += expect_json_contains(exit_json, "Direct conditional jumps accept only code labels", "exit-target JE diagnostic should reject Irvine32 target");
    failures += expect_json_contains(register_json, "\"code\":\"unsupported-branch-target-form\"", "register JE target should emit unsupported branch target form");
    failures += expect_json_not_contains(register_json, "execution-complete", "invalid JE target form should not execute");
    failures += expect_json_contains(memory_json, "\"code\":\"unsupported-branch-target-form\"", "memory JZ target should emit unsupported branch target form");
    failures += expect_json_contains(memory_json, "JZ memory targets are not supported", "memory JZ target diagnostic should explain indirect branch deferral");
    failures += expect_json_not_contains(memory_json, "execution-complete", "invalid JZ memory target should not execute");
    failures += expect_json_contains(immediate_json, "\"code\":\"unsupported-branch-target-form\"", "immediate JNE target should emit unsupported branch target form");
    failures += expect_json_contains(immediate_json, "JNE immediate numeric targets are not supported", "immediate JNE target diagnostic should explain label-only rule");
    failures += expect_json_not_contains(immediate_json, "execution-complete", "invalid JNE immediate target should not execute");

    return failures;
}


/// Verifies Phase 65 signed relational conditional jumps execute through source-run.
///
/// @return Number of failures.
static int test_phase65_signed_relational_jumps_source_run_programs(void) {
    char acceptance_json[8192];
    char not_taken_json[8192];
    char alias_json[8192];
    char primary_condition_json[8192];
    char raw_signed_json[8192];
    int failures = 0;

    copy_source_run_json(acceptance_json, sizeof(acceptance_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, -1\n"
        "    cmp eax, 1\n"
        "    jl less\n"
        "    mov ebx, 0\n"
        "    jmp done\n"
        "less:\n"
        "    mov ebx, 1\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(not_taken_json, sizeof(not_taken_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    cmp eax, 5\n"
        "    jl less\n"
        "    mov ebx, 2\n"
        "    jle equal_or_less\n"
        "    mov ecx, 9\n"
        "equal_or_less:\n"
        "    jg greater\n"
        "    mov edx, 4\n"
        "    jge done\n"
        "greater:\n"
        "    mov esi, 7\n"
        "less:\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(alias_json, sizeof(alias_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, -2\n"
        "    cmp eax, 3\n"
        "    jnge less\n"
        "    mov ebx, 9\n"
        "less:\n"
        "    mov ecx, 1\n"
        "    cmp ecx, 1\n"
        "    jng le_alias\n"
        "    mov ebx, 8\n"
        "le_alias:\n"
        "    mov edx, 4\n"
        "    cmp edx, 1\n"
        "    jnle greater_alias\n"
        "    mov ebx, 7\n"
        "greater_alias:\n"
        "    mov esi, 4\n"
        "    cmp esi, 4\n"
        "    jnl ge_alias\n"
        "    mov ebx, 6\n"
        "ge_alias:\n"
        "    mov edi, 5\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(primary_condition_json, sizeof(primary_condition_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, -3\n"
        "    cmp eax, 2\n"
        "    jle primary_less_or_equal\n"
        "    mov ebx, 9\n"
        "primary_less_or_equal:\n"
        "    mov ebx, 1\n"
        "    cmp ebx, -3\n"
        "    jg primary_greater\n"
        "    mov ecx, 9\n"
        "primary_greater:\n"
        "    mov ecx, 2\n"
        "    cmp ecx, -3\n"
        "    jge primary_greater_or_equal\n"
        "    mov edx, 9\n"
        "primary_greater_or_equal:\n"
        "    mov edx, 3\n"
        "    cmp eax, 2\n"
        "    jge skipped_for_less\n"
        "    mov esi, 4\n"
        "skipped_for_less:\n"
        "    mov edi, 5\n"
        "    cmp edi, 1\n"
        "    jle skipped_for_greater\n"
        "    mov ebp, 6\n"
        "skipped_for_greater:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(raw_signed_json, sizeof(raw_signed_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    cmp eax, 1\n"
        "    jl signed_less\n"
        "    mov ebx, 0\n"
        "    jmp done\n"
        "signed_less:\n"
        "    mov ebx, 1\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(acceptance_json, "\"ok\":true", "Phase 65 signed relational acceptance program should execute");
    failures += expect_json_contains(acceptance_json, "\"phase\":77", "current runtime acceptance program should report numeric metadata");
    failures += expect_json_contains(acceptance_json, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "current runtime acceptance program should report runtime phase name");
    failures += expect_json_contains(acceptance_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "taken JL should execute less target");
    failures += expect_json_contains(acceptance_json, "\"instructionCount\":6", "taken JL acceptance path should commit MOV, CMP, JL, target MOV, and done NOP");
    failures += expect_json_contains(acceptance_json, "\"memoryChanges\":[]", "taken JL should not create memory changes");
    failures += expect_json_not_contains(acceptance_json, "branch-runtime-deferred", "valid JL should not emit deferred branch diagnostic");

    failures += expect_json_contains(not_taken_json, "\"ok\":true", "not-taken and equal signed jumps should execute");
    failures += expect_json_contains(not_taken_json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2", "not-taken JL should execute fall-through MOV");
    failures += expect_json_contains(not_taken_json, "\"EDX\":{\"hex\":\"00000004h\",\"unsigned\":4", "not-taken JG should allow equal fall-through MOV");
    failures += expect_json_contains(not_taken_json, "\"ESI\":{\"hex\":\"00000000h\",\"unsigned\":0", "taken JGE should skip greater label body");

    failures += expect_json_contains(alias_json, "\"ok\":true", "signed relational alias program should execute");
    failures += expect_json_contains(alias_json, "\"ECX\":{\"hex\":\"00000001h\",\"unsigned\":1", "JNGE alias should reach less target");
    failures += expect_json_contains(alias_json, "\"EDX\":{\"hex\":\"00000004h\",\"unsigned\":4", "JNG alias should branch on equal");
    failures += expect_json_contains(alias_json, "\"ESI\":{\"hex\":\"00000004h\",\"unsigned\":4", "JNLE alias should branch on signed greater-than");
    failures += expect_json_contains(alias_json, "\"EDI\":{\"hex\":\"00000005h\",\"unsigned\":5", "JNL alias should branch on signed greater-or-equal");
    failures += expect_json_not_contains(alias_json, "branch-runtime-deferred", "valid signed alias jumps should not emit deferred branch diagnostics");

    failures += expect_json_contains(primary_condition_json, "\"ok\":true", "primary signed relational condition fixture should execute");
    failures += expect_json_contains(primary_condition_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "primary JLE should be taken for signed less-than");
    failures += expect_json_contains(primary_condition_json, "\"ECX\":{\"hex\":\"00000002h\",\"unsigned\":2", "primary JG should be taken for signed greater-than");
    failures += expect_json_contains(primary_condition_json, "\"EDX\":{\"hex\":\"00000003h\",\"unsigned\":3", "primary JGE should be taken for signed greater-than");
    failures += expect_json_contains(primary_condition_json, "\"ESI\":{\"hex\":\"00000004h\",\"unsigned\":4", "primary JGE should fall through for signed less-than");
    failures += expect_json_contains(primary_condition_json, "\"EBP\":{\"hex\":\"00000006h\",\"unsigned\":6", "primary JLE should fall through for signed greater-than");
    failures += expect_json_not_contains(primary_condition_json, "branch-runtime-deferred", "valid primary signed jumps should not emit deferred branch diagnostics");

    failures += expect_json_contains(raw_signed_json, "\"ok\":true", "raw signed comparison fixture should execute");
    failures += expect_json_contains(raw_signed_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "FFFFFFFFh should compare as signed -1 for JL");

    return failures;
}

/// Verifies Phase 65 signed relational conditional jump target diagnostics.
///
/// @return Number of failures.
static int test_phase65_signed_relational_jump_source_run_diagnostics(void) {
    char unknown_json[8192];
    char data_json[8192];
    char exit_json[8192];
    char register_json[8192];
    char memory_json[8192];
    char immediate_json[8192];
    int failures = 0;

    copy_source_run_json(unknown_json, sizeof(unknown_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jl missing\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(data_json, sizeof(data_json), masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    jge value\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(exit_json, sizeof(exit_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    jnl exit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(register_json, sizeof(register_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jl eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(memory_json, sizeof(memory_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jle [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(immediate_json, sizeof(immediate_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jg 1234h\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(unknown_json, "\"ok\":false", "unknown JL target should fail parse/source-run");
    failures += expect_json_contains(unknown_json, "\"code\":\"invalid-branch-target\"", "unknown JL target should emit invalid-branch-target");
    failures += expect_json_contains(data_json, "JGE target cannot be a data symbol", "data-target JGE diagnostic should name signed jump mnemonic");
    failures += expect_json_contains(exit_json, "JNL target cannot be an Irvine32 virtual routine", "exit-target JNL diagnostic should reject Irvine32 target");
    failures += expect_json_contains(register_json, "\"code\":\"unsupported-branch-target-form\"", "register JL target should emit unsupported branch target form");
    failures += expect_json_contains(register_json, "JL register targets are not supported", "register JL target diagnostic should explain indirect branch deferral");
    failures += expect_json_contains(memory_json, "JLE memory targets are not supported", "memory JLE target diagnostic should explain indirect branch deferral");
    failures += expect_json_contains(immediate_json, "JG immediate numeric targets are not supported", "immediate JG target diagnostic should explain label-only rule");
    failures += expect_json_not_contains(immediate_json, "execution-complete", "invalid JG immediate target should not execute");

    return failures;
}

/// Verifies Phase 65 signed relational jump loops use the instruction-count watchdog.
///
/// @return Number of failures.
static int test_phase65_signed_relational_jump_instruction_limit(void) {
    char loop_json[8192];
    char fallthrough_json[8192];

    copy_source_run_json(loop_json, sizeof(loop_json), masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "start:\n"
        "    cmp eax, eax\n"
        "    jge start\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        5U
    ));
    copy_source_run_json(fallthrough_json, sizeof(fallthrough_json), masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "    cmp eax, eax\n"
        "    jl skipped\n"
        "    mov ebx, 1\n"
        "skipped:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        5U
    ));
    int failures = 0;

    failures += expect_json_contains(loop_json, "\"ok\":false", "backward JGE loop should stop at instruction limit");
    failures += expect_json_contains(loop_json, "\"code\":\"instruction-limit-exceeded\"", "backward JGE loop should report instruction-limit-exceeded");
    failures += expect_json_contains(loop_json, "\"instructionCount\":5", "backward JGE loop should commit exactly the configured instruction limit");
    failures += expect_json_contains(loop_json, "\"attemptedNextInstructionIndex\":1", "instruction limit should identify blocked JGE fetch after CMP");
    failures += expect_json_not_contains(loop_json, "branch-runtime-deferred", "valid backward JGE should not emit deferred diagnostic");
    failures += expect_json_not_contains(loop_json, "execution-complete", "instruction-limit failure should not emit execution-complete");

    failures += expect_json_contains(fallthrough_json, "\"ok\":true", "not-taken JL should fall through without tripping watchdog");
    failures += expect_json_contains(fallthrough_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "fall-through after not-taken JL should execute MOV");
    failures += expect_json_not_contains(fallthrough_json, "instruction-limit-exceeded", "not-taken JL fixture should not falsely trip watchdog");

    return failures;
}

/// Verifies Phase 65 undefined-flag-use policy integration for signed relational jumps.
///
/// @return Number of failures.
static int test_phase65_signed_relational_undefined_flag_use_policy(void) {
    char warn_jl_json[8192];
    char warn_jg_json[8192];
    char off_json[8192];
    char error_json[8192];
    int failures = 0;

    copy_source_run_json(warn_jl_json, sizeof(warn_jl_json), masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    jl target\n"
        "target:\n"
        "    mov ebx, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN
    ));
    copy_source_run_json(warn_jg_json, sizeof(warn_jg_json), masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    jg target\n"
        "target:\n"
        "    mov ebx, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN
    ));
    copy_source_run_json(off_json, sizeof(off_json), masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    jl target\n"
        "target:\n"
        "    mov ebx, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF
    ));
    copy_source_run_json(error_json, sizeof(error_json), masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    jg target\n"
        "    mov ebx, 2\n"
        "target:\n"
        "    mov ebx, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR
    ));

    failures += expect_json_contains(warn_jl_json, "\"code\":\"undefined-flag-use\"", "JL warn policy should add undefined-flag-use diagnostic");
    failures += expect_json_contains(warn_jl_json, "JL reads OF", "JL diagnostic should identify signed relational consumer");
    failures += expect_json_contains(warn_jl_json, "\"kind\":\"simulator-warning\"", "JL warn policy should be non-fatal");
    failures += expect_json_contains(warn_jl_json, "\"ok\":true", "JL warning mode should continue");

    failures += expect_json_contains(warn_jg_json, "JG reads OF", "JG diagnostic should identify ZF/SF/OF consumer mnemonic");
    failures += expect_json_contains(warn_jg_json, "\"ok\":true", "JG warning mode should continue");

    failures += expect_json_contains(off_json, "\"ok\":true", "explicit off policy should execute successfully");
    failures += expect_json_not_contains(off_json, "undefined-flag-use", "explicit off policy should suppress signed jump consumer diagnostic");
    failures += expect_json_contains(off_json, "\"code\":\"execution-complete\"", "explicit off policy should still complete");

    failures += expect_json_contains(error_json, "\"ok\":false", "JG error policy should fail before branch decision");
    failures += expect_json_contains(error_json, "JG reads OF", "JG error diagnostic should identify invalid consumed OF");
    failures += expect_json_contains(error_json, "\"instructionCount\":2", "JG error should stop before executing the consumer");
    failures += expect_json_contains(error_json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0", "JG error should not mutate either branch path");
    failures += expect_json_contains(error_json, "\"memoryChanges\":[]", "JG error should not create memory-change rows");
    failures += expect_json_not_contains(error_json, "execution-complete", "JG undefined flag-use error should not complete");

    return failures;
}


/// Verifies Phase 66 unsigned relational conditional jumps execute through source-run.
///
/// @return Number of failures.
static int test_phase66_unsigned_relational_jumps_source_run_programs(void) {
    char acceptance_json[8192];
    char below_equal_json[8192];
    char alias_json[8192];
    char equality_not_taken_json[8192];
    char raw_unsigned_json[8192];
    int failures = 0;

    copy_source_run_json(acceptance_json, sizeof(acceptance_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    cmp eax, 1\n"
        "    ja above\n"
        "    mov ebx, 0\n"
        "    jmp done\n"
        "above:\n"
        "    mov ebx, 1\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(below_equal_json, sizeof(below_equal_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    cmp eax, 0FFFFFFFFh\n"
        "    jb below\n"
        "    mov ebx, 9\n"
        "below:\n"
        "    mov ebx, 1\n"
        "    cmp eax, eax\n"
        "    jbe equal_or_below\n"
        "    mov ecx, 9\n"
        "equal_or_below:\n"
        "    mov ecx, 2\n"
        "    cmp eax, 0FFFFFFFFh\n"
        "    jae not_below\n"
        "    mov edx, 3\n"
        "not_below:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(alias_json, sizeof(alias_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    cmp eax, 1\n"
        "    jnbe above_alias\n"
        "    mov ebx, 9\n"
        "above_alias:\n"
        "    mov ebx, 1\n"
        "    cmp eax, 5\n"
        "    jnb equal_alias\n"
        "    mov ecx, 9\n"
        "equal_alias:\n"
        "    mov ecx, 2\n"
        "    cmp eax, 6\n"
        "    jnae below_alias\n"
        "    mov edx, 9\n"
        "below_alias:\n"
        "    mov edx, 3\n"
        "    cmp eax, 5\n"
        "    jna below_equal_alias\n"
        "    mov esi, 9\n"
        "below_equal_alias:\n"
        "    mov esi, 4\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(equality_not_taken_json, sizeof(equality_not_taken_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 7\n"
        "    cmp eax, eax\n"
        "    ja above\n"
        "    mov ebx, 2\n"
        "above:\n"
        "    cmp eax, eax\n"
        "    jb below\n"
        "    mov ecx, 3\n"
        "below:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(raw_unsigned_json, sizeof(raw_unsigned_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    cmp eax, 1\n"
        "    jl signed_less\n"
        "    mov ebx, 0\n"
        "    jmp signed_done\n"
        "signed_less:\n"
        "    mov ebx, 1\n"
        "signed_done:\n"
        "    cmp eax, 1\n"
        "    ja unsigned_above\n"
        "    mov ecx, 0\n"
        "    jmp done\n"
        "unsigned_above:\n"
        "    mov ecx, 1\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(acceptance_json, "\"ok\":true", "Phase 66 unsigned relational acceptance program should execute");
    failures += expect_json_contains(acceptance_json, "\"phase\":77", "current runtime acceptance program should report numeric metadata");
    failures += expect_json_contains(acceptance_json, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "current runtime acceptance program should report runtime phase name");
    failures += expect_json_contains(acceptance_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "taken JA should execute above target");
    failures += expect_json_contains(acceptance_json, "\"memoryChanges\":[]", "taken JA should not create memory changes");
    failures += expect_json_not_contains(acceptance_json, "branch-runtime-deferred", "valid JA should not emit deferred branch diagnostic");

    failures += expect_json_contains(below_equal_json, "\"ok\":true", "below/equal unsigned jump fixture should execute");
    failures += expect_json_contains(below_equal_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "JB should be taken for unsigned below");
    failures += expect_json_contains(below_equal_json, "\"ECX\":{\"hex\":\"00000002h\",\"unsigned\":2", "JBE should be taken for equality");
    failures += expect_json_contains(below_equal_json, "\"EDX\":{\"hex\":\"00000003h\",\"unsigned\":3", "JAE should fall through when below is true");

    failures += expect_json_contains(alias_json, "\"ok\":true", "unsigned relational alias program should execute");
    failures += expect_json_contains(alias_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "JNBE alias should branch on unsigned above");
    failures += expect_json_contains(alias_json, "\"ECX\":{\"hex\":\"00000002h\",\"unsigned\":2", "JNB alias should branch on equality");
    failures += expect_json_contains(alias_json, "\"EDX\":{\"hex\":\"00000003h\",\"unsigned\":3", "JNAE alias should branch on unsigned below");
    failures += expect_json_contains(alias_json, "\"ESI\":{\"hex\":\"00000004h\",\"unsigned\":4", "JNA alias should branch on equality");
    failures += expect_json_not_contains(alias_json, "branch-runtime-deferred", "valid unsigned alias jumps should not emit deferred branch diagnostics");

    failures += expect_json_contains(equality_not_taken_json, "\"ok\":true", "unsigned equality not-taken fixture should execute");
    failures += expect_json_contains(equality_not_taken_json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2", "JA should not be taken for equal values");
    failures += expect_json_contains(equality_not_taken_json, "\"ECX\":{\"hex\":\"00000003h\",\"unsigned\":3", "JB should not be taken for equal values");

    failures += expect_json_contains(raw_unsigned_json, "\"ok\":true", "raw signed-vs-unsigned fixture should execute");
    failures += expect_json_contains(raw_unsigned_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "FFFFFFFFh should compare as signed -1 for JL");
    failures += expect_json_contains(raw_unsigned_json, "\"ECX\":{\"hex\":\"00000001h\",\"unsigned\":1", "FFFFFFFFh should compare as unsigned above for JA");

    return failures;
}

/// Verifies Phase 66 unsigned relational conditional jump diagnostics and watchdog behavior.
///
/// @return Number of failures.
static int test_phase66_unsigned_relational_jump_error_paths(void) {
    char data_json[8192];
    char exit_json[8192];
    char register_json[8192];
    char memory_json[8192];
    char immediate_json[8192];
    char loop_json[8192];
    char warn_json[8192];
    char off_json[8192];
    char error_json[8192];
    int failures = 0;

    copy_source_run_json(data_json, sizeof(data_json), masm32_sim_wasm_run_source_json(".data\nvalue DWORD 1\n.code\nmain PROC\n    ja value\nmain ENDP\nEND main\n"));
    copy_source_run_json(exit_json, sizeof(exit_json), masm32_sim_wasm_run_source_json("INCLUDE Irvine32.inc\n.code\nmain PROC\n    jna exit\nmain ENDP\nEND main\n"));
    copy_source_run_json(register_json, sizeof(register_json), masm32_sim_wasm_run_source_json(".code\nmain PROC\n    jb eax\nmain ENDP\nEND main\n"));
    copy_source_run_json(memory_json, sizeof(memory_json), masm32_sim_wasm_run_source_json(".code\nmain PROC\n    jbe [eax]\nmain ENDP\nEND main\n"));
    copy_source_run_json(immediate_json, sizeof(immediate_json), masm32_sim_wasm_run_source_json(".code\nmain PROC\n    jnb 1234h\nmain ENDP\nEND main\n"));
    copy_source_run_json(loop_json, sizeof(loop_json), masm32_sim_wasm_run_source_json_with_instruction_limit(".code\nmain PROC\nstart:\n    cmp eax, eax\n    jae start\nmain ENDP\nEND main\n", 5U));
    copy_source_run_json(warn_json, sizeof(warn_json), masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(".code\nmain PROC\n    mov al, 1\n    shl al, 8\n    ja target\ntarget:\n    mov ebx, 1\n    ret\nmain ENDP\nEND main\n", MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN));
    copy_source_run_json(off_json, sizeof(off_json), masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(".code\nmain PROC\n    mov al, 1\n    shl al, 8\n    ja target\n    mov ebx, 2\n    jmp done\ntarget:\n    mov ebx, 1\ndone:\n    nop\n    ret\nmain ENDP\nEND main\n", MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF));
    copy_source_run_json(error_json, sizeof(error_json), masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(".code\nmain PROC\n    mov al, 1\n    shl al, 8\n    jbe target\n    mov ebx, 2\ntarget:\n    mov ebx, 1\n    ret\nmain ENDP\nEND main\n", MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR));

    failures += expect_json_contains(data_json, "JA target cannot be a data symbol", "data-target JA diagnostic should name unsigned jump mnemonic");
    failures += expect_json_contains(exit_json, "JNA target cannot be an Irvine32 virtual routine", "exit-target JNA diagnostic should reject Irvine32 target");
    failures += expect_json_contains(register_json, "JB register targets are not supported", "register JB diagnostic should explain indirect branch deferral");
    failures += expect_json_contains(memory_json, "JBE memory targets are not supported", "memory JBE diagnostic should explain indirect branch deferral");
    failures += expect_json_contains(immediate_json, "JNB immediate numeric targets are not supported", "immediate JNB diagnostic should explain label-only rule");
    failures += expect_json_not_contains(immediate_json, "execution-complete", "invalid JNB immediate target should not execute");

    failures += expect_json_contains(loop_json, "\"code\":\"instruction-limit-exceeded\"", "backward JAE loop should report instruction-limit-exceeded");
    failures += expect_json_not_contains(loop_json, "branch-runtime-deferred", "valid backward JAE should not emit deferred diagnostic");
    failures += expect_json_not_contains(loop_json, "execution-complete", "instruction-limit failure should not emit execution-complete");

    failures += expect_json_contains(warn_json, "\"code\":\"undefined-flag-use\"", "JA warn policy should add undefined-flag-use diagnostic");
    failures += expect_json_contains(warn_json, "JA reads", "JA diagnostic should identify unsigned relational consumer");
    failures += expect_json_contains(warn_json, "\"kind\":\"simulator-warning\"", "JA warn policy should be non-fatal");
    failures += expect_json_contains(warn_json, "\"ok\":true", "JA warning mode should continue");

    failures += expect_json_contains(off_json, "\"ok\":true", "JA off policy should continue");
    failures += expect_json_not_contains(off_json, "undefined-flag-use", "JA off policy should suppress only undefined-flag-use diagnostics");
    failures += expect_json_contains(off_json, "undefined-shift-flag", "JA off policy should preserve producer diagnostics");
    failures += expect_json_contains(off_json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2", "JA off policy should use deterministic preserved flags");

    failures += expect_json_contains(error_json, "\"ok\":false", "JBE error policy should fail before branch decision");
    failures += expect_json_contains(error_json, "JBE reads", "JBE error diagnostic should identify unsigned relational consumer");
    failures += expect_json_contains(error_json, "\"instructionCount\":2", "JBE error should stop before executing the consumer");
    failures += expect_json_contains(error_json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0", "JBE error should not mutate either branch path");
    failures += expect_json_contains(error_json, "\"memoryChanges\":[]", "JBE error should not create memory-change rows");
    failures += expect_json_not_contains(error_json, "execution-complete", "JBE undefined flag-use error should not complete");

    return failures;
}


/// Verifies Phase 69 direct user-procedure CALL behavior through source-run JSON.
///
/// @return Number of failures.
static int test_phase69_direct_call_source_run_behavior(void) {
    char valid_copy[4096];
    char object_strict_copy[4096];
    char invalid_stack_copy[4096];
    char invalid_target_copy[4096];
    int failures = 0;

    copy_source_run_json(valid_copy, sizeof(valid_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov ebx, 2\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(valid_copy, "\"phase\":77", "Phase 69 CALL regression fixture should report current Phase 72 runtime metadata");
    failures += expect_json_contains(valid_copy, "\"ok\":false", "direct user-procedure CALL with helper fallthrough should now fail at the helper boundary");
    failures += expect_json_contains(valid_copy, "\"code\":\"procedure-fell-through\"", "CALL-reached helper fallthrough should use Phase 71 diagnostic");
    failures += expect_json_contains(valid_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "direct CALL should transfer to the helper procedure body before fallthrough diagnostic");
    failures += expect_json_contains(valid_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "direct CALL without an executed RET should not return to the instruction after CALL");
    failures += expect_json_contains(valid_copy, "\"ESP\":{\"hex\":\"008FFFFCh\",\"unsigned\":9437180}", "direct CALL should leave ESP decremented by the internal return-token write");
    failures += expect_json_contains(valid_copy, "\"memoryChanges\":[]", "direct CALL fallthrough diagnostic should not expose implicit stack memoryChanges rows");
    failures += expect_json_not_contains(valid_copy, "execution-complete", "CALL-reached helper fallthrough should not emit execution-complete");

    copy_source_run_json(object_strict_copy, sizeof(object_strict_copy), masm32_sim_wasm_run_source_json_with_ui_settings(
        ".data\n"
        "value DWORD 7\n"
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "    mov eax, value\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov ebx, value\n"
        "Helper ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF
    ));
    failures += expect_json_contains(object_strict_copy, "\"ok\":false", "valid CALL stack write should proceed to Phase 71 helper fallthrough under strict declared-object validation");
    failures += expect_json_contains(object_strict_copy, "\"code\":\"procedure-fell-through\"", "strict object validation CALL fixture should stop at helper fallthrough");
    failures += expect_json_contains(object_strict_copy, "\"ESP\":{\"hex\":\"008FFFFCh\",\"unsigned\":9437180}", "strict object validation should not reject CALL stack writes outside declared data objects");
    failures += expect_json_contains(object_strict_copy, "\"EBX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "strict object validation CALL fixture should execute the helper body");
    failures += expect_json_not_contains(object_strict_copy, "object-bounds-violation", "CALL return-token stack write must not be rejected as a declared-object violation");
    failures += expect_json_not_contains(object_strict_copy, "execution-complete", "strict object validation helper fallthrough should not complete");

    copy_source_run_json(invalid_stack_copy, sizeof(invalid_stack_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov esp, 0\n"
        "    call Helper\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov ebx, 2\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_stack_copy, "\"ok\":false", "CALL with invalid ESP should fail");
    failures += expect_json_contains(invalid_stack_copy, "\"status\":\"execution-error\"", "CALL with invalid ESP should fail at runtime");
    failures += expect_json_contains(invalid_stack_copy, "\"code\":\"invalid-address\"", "CALL stack write failure should use the central checked-memory diagnostic code");
    failures += expect_json_contains(invalid_stack_copy, "Invalid memory write at FFFFFFFCh for 4 bytes", "CALL stack write failure should report the checked destination range");
    failures += expect_json_contains(invalid_stack_copy, "\"ESP\":{\"hex\":\"00000000h\",\"unsigned\":0}", "failed CALL stack write should leave externally visible ESP unchanged after the CALL attempt");
    failures += expect_json_contains(invalid_stack_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "failed CALL should not execute the post-CALL instruction");
    failures += expect_json_contains(invalid_stack_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "failed CALL should not transfer to the helper body");
    failures += expect_json_contains(invalid_stack_copy, "\"memoryChanges\":[]", "failed CALL stack write should not report memory-change rows");
    failures += expect_json_not_contains(invalid_stack_copy, "execution-complete", "failed CALL should not emit execution-complete");
    failures += expect_json_not_contains(invalid_stack_copy, "programConsole", "failed CALL should not produce Program Console output");

    copy_source_run_json(invalid_target_copy, sizeof(invalid_target_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call WriteString\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_target_copy, "\"ok\":false", "CALL Irvine32 routine target should remain deferred");
    failures += expect_json_contains(invalid_target_copy, "\"code\":\"unsupported-irvine32-routine\"", "CALL Irvine32 routine target should use the registry-owned diagnostic");
    failures += expect_json_contains(invalid_target_copy, "CALL dispatch for Irvine32 routine names is deferred", "CALL Irvine32 routine diagnostic should explain deferred dispatch");
    failures += expect_json_not_contains(invalid_target_copy, "execution-complete", "deferred Irvine32 CALL should not execute");

    return failures;
}


/// Verifies Phase 71 CALL/RET source-run behavior returns to the CALL successor.
///
/// @return Number of failures.
static int test_phase71_call_ret_source_run_behavior(void) {
    char success_copy[8192];
    char invalid_token_copy[8192];
    char empty_stack_copy[8192];
    char terminal_call_copy[8192];
    char strict_copy[8192];
    int failures = 0;

    copy_source_run_json(success_copy, sizeof(success_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    call Helper\n"
        "    mov ebx, eax\n"
        "    exit\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov eax, 42\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(success_copy, "\"ok\":true", "Phase 71 CALL/RET acceptance program should execute");
    failures += expect_json_contains(success_copy, "\"phase\":77", "Phase 71 CALL/RET acceptance program should report numeric phase metadata");
    failures += expect_json_contains(success_copy, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "Phase 71 CALL/RET acceptance program should report runtime phase name");
    failures += expect_json_contains(success_copy, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "current source-run output contract should be reported");
    failures += expect_json_contains(success_copy, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "helper should leave EAX set to 42");
    failures += expect_json_contains(success_copy, "\"EBX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "post-RET instruction should observe helper result");
    failures += expect_json_contains(success_copy, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184}", "CALL/RET should restore ESP to the empty-stack top");
    failures += expect_json_contains(success_copy, "\"memoryChanges\":[]", "CALL/RET implicit stack token accesses should not create public memoryChanges rows");
    failures += expect_json_contains(success_copy, "\"code\":\"execution-complete\"", "successful CALL/RET program should emit execution-complete");
    failures += expect_json_not_contains(success_copy, "programConsole", "CALL/RET acceptance program should not produce Program Console output");

    copy_source_run_json(invalid_token_copy, sizeof(invalid_token_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov DWORD PTR [esp], 12345678h\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_token_copy, "\"ok\":false", "corrupted return-token fixture should fail");
    failures += expect_json_contains(invalid_token_copy, "\"status\":\"execution-error\"", "corrupted return-token fixture should fail at runtime");
    failures += expect_json_contains(invalid_token_copy, "\"code\":\"invalid-return-address\"", "corrupted return-token fixture should use invalid-return-address");
    failures += expect_json_contains(invalid_token_copy, "RET read a return token", "invalid-return-address message should describe RET token validation");
    failures += expect_json_contains(invalid_token_copy, "\"ESP\":{\"hex\":\"008FFFFCh\",\"unsigned\":9437180}", "failed RET token validation should not pop ESP");
    failures += expect_json_contains(invalid_token_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "failed RET token validation should not execute post-CALL instruction");
    failures += expect_json_not_contains(invalid_token_copy, "execution-complete", "invalid-return-address should not emit execution-complete");
    failures += expect_json_not_contains(invalid_token_copy, "programConsole", "invalid-return-address should not produce Program Console output");

    copy_source_run_json(empty_stack_copy, sizeof(empty_stack_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(empty_stack_copy, "\"ok\":true", "root RET at empty stack should complete in Phase 71");
    failures += expect_json_contains(empty_stack_copy, "\"code\":\"execution-complete\"", "root RET should emit execution-complete");
    failures += expect_json_contains(empty_stack_copy, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184}", "root RET should leave ESP unchanged");
    failures += expect_json_contains(empty_stack_copy, "\"memoryChanges\":[]", "root RET should not expose memoryChanges rows");
    failures += expect_json_not_contains(empty_stack_copy, "invalid-address", "root RET should not read empty-stack memory");
    failures += expect_json_not_contains(empty_stack_copy, "invalid-return-address", "root RET should not validate a return token");

    {
        char strict_root_copy[8192];
        copy_source_run_json(strict_root_copy, sizeof(strict_root_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings(
            ".code\n"
            "main PROC\n"
            "    ret\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
            MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
            MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
            0U,
            1000000U,
            MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME
        ));
        failures += expect_json_contains(strict_root_copy, "\"ok\":false", "strict root RET mode should reject selected-entry root RET");
        failures += expect_json_contains(strict_root_copy, "\"code\":\"root-ret-disallowed-by-mode\"", "strict root RET should use root-ret-disallowed-by-mode");
        failures += expect_json_contains(strict_root_copy, "RET cannot return because no caller supplied a return address", "strict root-code-stream RET diagnostic should explain the source-level cause");
        failures += expect_json_contains(strict_root_copy, "Use the simulator's MASM32-compatible root RET mode", "strict root RET diagnostic should suggest compatible mode without exposing the strict setting value");
        failures += expect_json_contains(strict_root_copy, "terminate explicitly with the supported Irvine32 exit routine", "strict root RET diagnostic should suggest explicit supported termination");
        failures += expect_json_not_contains(strict_root_copy, "rootRetMode = \"strict-call-frame\"", "strict root RET diagnostic should not repeat the configured strict-mode value");
        failures += expect_json_not_contains(strict_root_copy, "helper return token", "strict root RET diagnostic should not expose helper return-token internals");
        failures += expect_json_contains(strict_root_copy, "\"rootRetMode\":\"strict-call-frame\"", "strict root RET response should serialize active setting");
        failures += expect_json_contains(strict_root_copy, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184}", "strict root RET should leave ESP unchanged");
        failures += expect_json_contains(strict_root_copy, "\"memoryChanges\":[]", "strict root RET should not expose memoryChanges rows");
        failures += expect_json_not_contains(strict_root_copy, "invalid-address", "strict root RET should not read empty-stack memory");
        failures += expect_json_not_contains(strict_root_copy, "invalid-return-address", "strict root RET should not validate a return token");
        failures += expect_json_not_contains(strict_root_copy, "execution-complete", "strict root RET should not emit successful completion");
    }

    {
        char strict_root_uninitialized_copy[8192];
        copy_source_run_json(strict_root_uninitialized_copy, sizeof(strict_root_uninitialized_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings(
            ".code\n"
            "main PROC\n"
            "    ret\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_STRICT,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
            MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
            MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
            0U,
            1000000U,
            MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME
        ));
        failures += expect_json_contains(strict_root_uninitialized_copy, "\"code\":\"root-ret-disallowed-by-mode\"", "strict root RET should remain the first diagnostic even with strict uninitialized-read checks");
        failures += expect_json_not_contains(strict_root_uninitialized_copy, "uninitialized-read", "strict root RET must not run planned [ESP] uninitialized-read checks");
        failures += expect_json_not_contains(strict_root_uninitialized_copy, "invalid-address", "strict root RET must not probe stack memory before rejection");
        failures += expect_json_not_contains(strict_root_uninitialized_copy, "invalid-return-address", "strict root RET must not validate return tokens before rejection");
    }

    {
        char compatible_root_copy[8192];
        copy_source_run_json(compatible_root_copy, sizeof(compatible_root_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings(
            ".code\n"
            "main PROC\n"
            "    RET\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
            MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
            MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
            0U,
            1000000U,
            MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE
        ));
        failures += expect_json_contains(compatible_root_copy, "\"ok\":true", "explicit compatible root RET mode should preserve Phase 71 behavior");
        failures += expect_json_contains(compatible_root_copy, "\"rootRetMode\":\"masm32-compatible\"", "compatible root RET response should serialize active setting");
        failures += expect_json_contains(compatible_root_copy, "execution-complete", "compatible root RET should complete successfully");
    }

    {
        char helper_fallthrough_copy[8192];
        copy_source_run_json(helper_fallthrough_copy, sizeof(helper_fallthrough_copy), masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    call Helper\n"
            "    ret\n"
            "    ret\n"
        "main ENDP\n"
            "Helper PROC\n"
            "    mov eax, 7\n"
            "Helper ENDP\n"
            "END main\n"
        ));
        failures += expect_json_contains(helper_fallthrough_copy, "\"ok\":false", "helper fallthrough fixture should fail");
        failures += expect_json_contains(helper_fallthrough_copy, "\"code\":\"procedure-fell-through\"", "helper fallthrough fixture should report Phase 71 diagnostic");
        failures += expect_json_contains(helper_fallthrough_copy, "Execution fell through out of procedure", "helper fallthrough message should explain missing RET");
        failures += expect_json_contains(helper_fallthrough_copy, "\"EAX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "helper fallthrough fixture should execute helper body before stopping");
        failures += expect_json_not_contains(helper_fallthrough_copy, "execution-complete", "helper fallthrough fixture should not complete");
    }

    copy_source_run_json(terminal_call_copy, sizeof(terminal_call_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov eax, 42\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(terminal_call_copy, "\"ok\":false", "terminal CALL/RET fixture should fail through terminal-CALL invalid-return-token semantics");
    failures += expect_json_contains(terminal_call_copy, "\"code\":\"invalid-return-address\"", "terminal CALL return token should be rejected rather than returning into the helper body");
    failures += expect_json_contains(terminal_call_copy, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "terminal CALL fixture should execute helper body before failing at RET");
    failures += expect_json_contains(terminal_call_copy, "\"ESP\":{\"hex\":\"008FFFFCh\",\"unsigned\":9437180}", "terminal CALL invalid return should not pop ESP");
    failures += expect_json_not_contains(terminal_call_copy, "execution-complete", "terminal CALL invalid return should not emit execution-complete");

    copy_source_run_json(strict_copy, sizeof(strict_copy), masm32_sim_wasm_run_source_json_with_ui_settings(
        "INCLUDE Irvine32.inc\n"
        ".data\n"
        "value DWORD 7\n"
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "    mov eax, value\n"
        "    exit\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov ebx, value\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_DECLARED_OBJECT_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF
    ));
    failures += expect_json_contains(strict_copy, "\"ok\":true", "CALL/RET stack token read should execute under strict declared-object validation");
    failures += expect_json_contains(strict_copy, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184}", "strict declared-object CALL/RET fixture should restore ESP");
    failures += expect_json_not_contains(strict_copy, "object-bounds-violation", "RET return-token stack read must not be rejected as a declared-object violation");

    {
        char invalid_root_mode_copy[8192];
        copy_source_run_json(invalid_root_mode_copy, sizeof(invalid_root_mode_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings(
            ".code\n"
            "main PROC\n"
            "    ret\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
            MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
            MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
            0U,
            1000000U,
            (Masm32SimWasmRootRetMode)99
        ));
        failures += expect_json_contains(invalid_root_mode_copy, "\"ok\":false", "invalid root RET mode should fail before execution");
        failures += expect_json_contains(invalid_root_mode_copy, "invalid-root-ret-mode-setting", "invalid root RET mode should use settings diagnostic");
        failures += expect_json_contains(invalid_root_mode_copy, "\"setting\":\"rootRetMode\"", "invalid root RET mode should name the setting");
        failures += expect_json_not_contains(invalid_root_mode_copy, "execution-complete", "invalid root RET mode should not run source");
    }

    return failures;
}


/// Verifies Phase 71D source-run procedure-fallthrough policy serialization and behavior.
///
/// @return Number of failures.
static int test_phase71d_procedure_fallthrough_policy_source_run_behavior(void) {
    static const char *source =
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ecx, 2\n"
        "helper ENDP\n"
        "END main\n";
    char warn_copy[8192];
    char off_copy[8192];
    char error_copy[8192];
    char fallthrough_ret_copy[8192];
    char invalid_copy[4096];
    int failures = 0;

    copy_source_run_json(warn_copy, sizeof(warn_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
        source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN
    ));
    failures += expect_json_contains(warn_copy, "\"procedureFallthroughPolicy\":\"warn\"", "warn policy should serialize active procedure-fallthrough setting");
    failures += expect_json_contains(warn_copy, "\"entryProcedureEndMode\":\"code-stream\"", "Phase 71E default entry procedure end mode should serialize as code-stream");
    failures += expect_json_contains(warn_copy, "\"kind\":\"simulator-warning\"", "warn policy should emit a non-fatal procedure-fallthrough warning");
    failures += expect_json_contains(warn_copy, "\"code\":\"procedure-fell-through\"", "warn policy should use public procedure-fell-through code");
    failures += expect_json_contains(warn_copy, "Execution fell through from procedure", "warn policy should explain procedure-boundary crossing");
    failures += expect_json_contains(warn_copy, "\"ECX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "warn policy should continue into the destination procedure");
    failures += expect_json_contains(warn_copy, "\"code\":\"code-fell-off-end\"", "warn policy should preserve later code-fell-off-end diagnostics");

    copy_source_run_json(off_copy, sizeof(off_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
        source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_OFF
    ));
    failures += expect_json_contains(off_copy, "\"procedureFallthroughPolicy\":\"off\"", "off policy should serialize active procedure-fallthrough setting");
    failures += expect_json_not_contains(off_copy, "\"code\":\"procedure-fell-through\"", "off policy should suppress procedure-fell-through only");
    failures += expect_json_contains(off_copy, "\"ECX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "off policy should continue into the destination procedure");
    failures += expect_json_contains(off_copy, "\"code\":\"code-fell-off-end\"", "off policy must not suppress code-fell-off-end");

    copy_source_run_json(error_copy, sizeof(error_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
        source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_ERROR
    ));
    failures += expect_json_contains(error_copy, "\"procedureFallthroughPolicy\":\"error\"", "error policy should serialize active procedure-fallthrough setting");
    failures += expect_json_contains(error_copy, "\"kind\":\"runtime-error\"", "error policy should emit a runtime error");
    failures += expect_json_contains(error_copy, "\"code\":\"procedure-fell-through\"", "error policy should use public procedure-fell-through code");
    failures += expect_json_contains(error_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "error policy should commit the source procedure instruction");
    failures += expect_json_contains(error_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "error policy should stop before the destination procedure instruction");
    failures += expect_json_not_contains(error_copy, "\"code\":\"code-fell-off-end\"", "error policy should stop before later code-end diagnostics");



    copy_source_run_json(fallthrough_ret_copy, sizeof(fallthrough_ret_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
        ".code\n"
        "main PROC\n"
        "    mov eax, 7\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 7\n"
        "    ret\n"
        "helper ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN
    ));
    failures += expect_json_contains(fallthrough_ret_copy, "\"procedureFallthroughPolicy\":\"warn\"", "fallthrough RET fixture should serialize warn policy");
    failures += expect_json_contains(fallthrough_ret_copy, "\"ok\":true", "fallthrough RET fixture should complete successfully");
    failures += expect_json_contains(fallthrough_ret_copy, "\"status\":\"ok\"", "fallthrough RET fixture should report OK status");
    failures += expect_json_contains(fallthrough_ret_copy, "\"code\":\"procedure-fell-through\"", "fallthrough RET fixture should still warn at the procedure boundary");
    failures += expect_json_not_contains(fallthrough_ret_copy, "\"code\":\"invalid-address\"", "fallthrough RET fixture should not read an absent helper return token");
    failures += expect_json_not_contains(fallthrough_ret_copy, "\"code\":\"code-fell-off-end\"", "fallthrough RET fixture should terminate before code-end falloff");
    failures += expect_json_contains(fallthrough_ret_copy, "\"EAX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "fallthrough RET fixture should execute selected-entry MOV");
    failures += expect_json_contains(fallthrough_ret_copy, "\"EBX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "fallthrough RET fixture should execute helper MOV before RET");

    copy_source_run_json(invalid_copy, sizeof(invalid_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
        source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        (Masm32SimWasmProcedureFallthroughPolicy)99
    ));
    failures += expect_json_contains(invalid_copy, "invalid-procedure-fallthrough-policy-setting", "invalid procedure-fallthrough policy should produce a settings error");
    failures += expect_json_contains(invalid_copy, "\"setting\":\"procedureFallthroughPolicy\"", "invalid policy diagnostic should name the setting");
    failures += expect_json_not_contains(invalid_copy, "execution-complete", "invalid policy should not execute source");

    return failures;
}


/// Verifies Phase 71E source-run entry-procedure end mode serialization and behavior.
///
/// @return Number of failures.
static int test_phase71e_entry_procedure_end_mode_source_run_behavior(void) {
    static const char *fallthrough_source =
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 2\n"
        "    ret\n"
        "helper ENDP\n"
        "END main\n";
    static const char *empty_entry_source =
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n";
    static const char *empty_entry_with_successor_source =
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 3\n"
        "    ret\n"
        "helper ENDP\n"
        "END main\n";
    static const char *taken_branch_to_boundary_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    cmp eax, eax\n"
        "    je helper_body\n"
        "main ENDP\n"
        "helper PROC\n"
        "helper_body:\n"
        "    mov ebx, 2\n"
        "    exit\n"
        "helper ENDP\n"
        "END main\n";
    static const char *parser_error_source =
        ".code\n"
        "main PROC\n"
        "    definitely_not_an_instruction eax\n"
        "main ENDP\n"
        "END main\n";
    char code_stream_copy[8192];
    char stop_copy[8192];
    char empty_default_copy[8192];
    char empty_stop_copy[8192];
    char empty_successor_stop_copy[8192];
    char branch_stop_copy[8192];
    char parser_error_stop_copy[4096];
    char invalid_copy[4096];
    int failures = 0;

    copy_source_run_json(code_stream_copy, sizeof(code_stream_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(code_stream_copy, "\"entryProcedureEndMode\":\"code-stream\"", "code-stream mode should serialize explicitly");
    failures += expect_json_contains(code_stream_copy, "\"code\":\"procedure-fell-through\"", "code-stream mode should preserve Phase 71D procedure-fell-through warning");
    failures += expect_json_contains(code_stream_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "code-stream mode should execute later procedure text");
    failures += expect_json_contains(code_stream_copy, "\"ok\":true", "code-stream fallthrough RET fixture should still complete successfully");

    copy_source_run_json(stop_copy, sizeof(stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(stop_copy, "\"entryProcedureEndMode\":\"stop-at-entry-end\"", "stop-at-entry-end mode should serialize explicitly");
    failures += expect_json_contains(stop_copy, "\"ok\":true", "stop-at-entry-end should complete successfully at selected-entry ENDP");
    failures += expect_json_contains(stop_copy, "\"status\":\"ok\"", "stop-at-entry-end should report OK status");
    failures += expect_json_contains(stop_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "stop-at-entry-end should preserve selected-entry side effects");
    failures += expect_json_contains(stop_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "stop-at-entry-end should stop before later procedure text");
    failures += expect_json_not_contains(stop_copy, "\"code\":\"procedure-fell-through\"", "stop-at-entry-end should not warn for the stopped selected-entry boundary");
    failures += expect_json_not_contains(stop_copy, "\"code\":\"code-fell-off-end\"", "stop-at-entry-end should not report code-fell-off-end at selected-entry ENDP");

    copy_source_run_json(empty_default_copy, sizeof(empty_default_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        empty_entry_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(empty_default_copy, "\"entryProcedureEndMode\":\"code-stream\"", "empty selected entry should serialize default code-stream mode");
    failures += expect_json_contains(empty_default_copy, "\"code\":\"code-fell-off-end\"", "empty selected entry should keep Phase 71C code-fell-off-end in code-stream mode");

    copy_source_run_json(empty_stop_copy, sizeof(empty_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        empty_entry_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(empty_stop_copy, "\"entryProcedureEndMode\":\"stop-at-entry-end\"", "empty selected entry should serialize stop-at-entry-end mode");
    failures += expect_json_contains(empty_stop_copy, "\"ok\":true", "empty selected entry should succeed in stop-at-entry-end mode");
    failures += expect_json_contains(empty_stop_copy, "\"instructionCount\":0", "empty selected entry should not fabricate execution count in stop-at-entry-end mode");
    failures += expect_json_not_contains(empty_stop_copy, "\"code\":\"code-fell-off-end\"", "empty selected entry should not report code-fell-off-end in stop-at-entry-end mode");

    copy_source_run_json(empty_successor_stop_copy, sizeof(empty_successor_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        empty_entry_with_successor_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(empty_successor_stop_copy, "\"entryProcedureEndMode\":\"stop-at-entry-end\"", "empty selected entry followed by later text should serialize stop-at-entry-end mode");
    failures += expect_json_contains(empty_successor_stop_copy, "\"ok\":true", "empty selected entry followed by later text should succeed in stop-at-entry-end mode");
    failures += expect_json_contains(empty_successor_stop_copy, "\"instructionCount\":0", "empty selected entry followed by later text should not execute helper text in stop-at-entry-end mode");
    failures += expect_json_contains(empty_successor_stop_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "empty selected entry stop should leave later helper side effects absent");
    failures += expect_json_not_contains(empty_successor_stop_copy, "\"code\":\"procedure-fell-through\"", "empty selected entry stop should not warn before later helper text");
    failures += expect_json_not_contains(empty_successor_stop_copy, "\"code\":\"code-fell-off-end\"", "empty selected entry stop should not report code-fell-off-end before later helper text");

    copy_source_run_json(branch_stop_copy, sizeof(branch_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        taken_branch_to_boundary_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(branch_stop_copy, "\"entryProcedureEndMode\":\"stop-at-entry-end\"", "taken-branch fixture should serialize stop-at-entry-end mode");
    failures += expect_json_contains(branch_stop_copy, "\"ok\":true", "taken branch to selected-entry boundary should execute explicit transfer instead of compatibility-stopping");
    failures += expect_json_contains(branch_stop_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "taken branch to later helper text should execute the helper body");
    failures += expect_json_contains(branch_stop_copy, "execution-complete", "taken branch fixture should terminate through explicit Irvine32 exit");
    failures += expect_json_not_contains(branch_stop_copy, "\"code\":\"procedure-fell-through\"", "taken branch to helper text should not be reported as ordinary procedure fallthrough");

    copy_source_run_json(parser_error_stop_copy, sizeof(parser_error_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        parser_error_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(parser_error_stop_copy, "\"entryProcedureEndMode\":\"stop-at-entry-end\"", "parser-error fixture should still serialize stop-at-entry-end mode");
    failures += expect_json_contains(parser_error_stop_copy, "\"ok\":false", "stop-at-entry-end must not hide parser errors inside the selected entry procedure");
    failures += expect_json_contains(parser_error_stop_copy, "unsupported-instruction", "stop-at-entry-end parser-error fixture should expose the parser diagnostic");
    failures += expect_json_not_contains(parser_error_stop_copy, "execution-complete", "stop-at-entry-end parser-error fixture should not execute or complete");

    copy_source_run_json(invalid_copy, sizeof(invalid_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        (Masm32SimWasmEntryProcedureEndMode)99
    ));
    failures += expect_json_contains(invalid_copy, "invalid-entry-procedure-end-mode-setting", "invalid entry-procedure end mode should produce a settings error");
    failures += expect_json_contains(invalid_copy, "\"setting\":\"entryProcedureEndMode\"", "invalid entry-procedure end mode diagnostic should name the setting");
    failures += expect_json_contains(invalid_copy, "code-stream", "invalid entry-procedure end mode diagnostic should list code-stream");
    failures += expect_json_contains(invalid_copy, "stop-at-entry-end", "invalid entry-procedure end mode diagnostic should list stop-at-entry-end");
    failures += expect_json_not_contains(invalid_copy, "execution-complete", "invalid entry-procedure end mode should not execute source");

    return failures;
}


/// Verifies Phase 71F fallthrough migration and opposite-mode fixture coverage.
///
/// @return Number of failures.
static int test_phase71f_fallthrough_opposite_fixtures_source_run(void) {
    static const char *fallthrough_source =
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 2\n"
        "helper ENDP\n"
        "END main\n";
    static const char *empty_main_source =
        ".code\n"
        "main PROC\n"
        "main ENDP\n"
        "END main\n";
    static const char *exit_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    exit\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 9\n"
        "helper ENDP\n"
        "END main\n";
    static const char *root_ret_source =
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 9\n"
        "helper ENDP\n"
        "END main\n";
    char code_stream_copy[TEST_JSON_COPY_CAPACITY];
    char stop_copy[TEST_JSON_COPY_CAPACITY];
    char empty_code_stream_copy[TEST_JSON_COPY_CAPACITY];
    char empty_stop_copy[TEST_JSON_COPY_CAPACITY];
    char warn_copy[TEST_JSON_COPY_CAPACITY];
    char off_copy[TEST_JSON_COPY_CAPACITY];
    char error_copy[TEST_JSON_COPY_CAPACITY];
    char exit_code_stream_copy[TEST_JSON_COPY_CAPACITY];
    char exit_stop_copy[TEST_JSON_COPY_CAPACITY];
    char root_compatible_code_stream_copy[TEST_JSON_COPY_CAPACITY];
    char root_compatible_stop_copy[TEST_JSON_COPY_CAPACITY];
    char root_strict_code_stream_copy[TEST_JSON_COPY_CAPACITY];
    char root_strict_stop_copy[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    copy_source_run_json(code_stream_copy, sizeof(code_stream_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(code_stream_copy, "\"entryProcedureEndMode\":\"code-stream\"", "Phase 71F default opposite fixture should serialize code-stream mode");
    failures += expect_json_contains(code_stream_copy, "\"ok\":false", "Phase 71F code-stream opposite fixture should fail at code end without implicit ENDP success");
    failures += expect_json_contains(code_stream_copy, "\"code\":\"procedure-fell-through\"", "Phase 71F code-stream opposite fixture should warn at the main-to-helper boundary");
    failures += expect_json_contains(code_stream_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F code-stream opposite fixture should report code-fell-off-end after helper body");
    failures += expect_json_contains(code_stream_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "Phase 71F code-stream opposite fixture should execute later procedure text");

    copy_source_run_json(stop_copy, sizeof(stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(stop_copy, "\"entryProcedureEndMode\":\"stop-at-entry-end\"", "Phase 71F compatibility opposite fixture should serialize stop-at-entry-end mode");
    failures += expect_json_contains(stop_copy, "\"ok\":true", "Phase 71F stop-at-entry-end opposite fixture should complete at the selected-entry boundary");
    failures += expect_json_contains(stop_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 71F stop-at-entry-end opposite fixture should not execute later procedure text");
    failures += expect_json_not_contains(stop_copy, "\"code\":\"procedure-fell-through\"", "Phase 71F stop-at-entry-end opposite fixture should not warn at stopped selected-entry boundary");
    failures += expect_json_not_contains(stop_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F stop-at-entry-end opposite fixture should not report code-end falloff");

    copy_source_run_json(empty_code_stream_copy, sizeof(empty_code_stream_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        empty_main_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(empty_code_stream_copy, "\"ok\":false", "Phase 71F empty-main code-stream fixture should fail without implicit ENDP success");
    failures += expect_json_contains(empty_code_stream_copy, "\"instructionCount\":0", "Phase 71F empty-main code-stream fixture should have no lowered instructions");
    failures += expect_json_contains(empty_code_stream_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F empty-main code-stream fixture should report code-fell-off-end");

    copy_source_run_json(empty_stop_copy, sizeof(empty_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        empty_main_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(empty_stop_copy, "\"ok\":true", "Phase 71F empty-main stop-at-entry-end fixture should complete through named compatibility mode");
    failures += expect_json_contains(empty_stop_copy, "\"instructionCount\":0", "Phase 71F empty-main stop-at-entry-end fixture should not fabricate lowered instructions");
    failures += expect_json_not_contains(empty_stop_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F empty-main stop-at-entry-end fixture should not report code-fell-off-end");

    copy_source_run_json(warn_copy, sizeof(warn_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(warn_copy, "\"procedureFallthroughPolicy\":\"warn\"", "Phase 71F warn-policy opposite fixture should serialize warn mode");
    failures += expect_json_contains(warn_copy, "\"kind\":\"simulator-warning\"", "Phase 71F warn-policy opposite fixture should emit a warning");
    failures += expect_json_contains(warn_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "Phase 71F warn-policy opposite fixture should continue into the destination procedure");

    copy_source_run_json(off_copy, sizeof(off_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_OFF,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(off_copy, "\"procedureFallthroughPolicy\":\"off\"", "Phase 71F off-policy opposite fixture should serialize off mode");
    failures += expect_json_not_contains(off_copy, "\"code\":\"procedure-fell-through\"", "Phase 71F off-policy opposite fixture should suppress only the fallthrough warning");
    failures += expect_json_contains(off_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "Phase 71F off-policy opposite fixture should continue into the destination procedure");
    failures += expect_json_contains(off_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F off-policy opposite fixture should preserve code-fell-off-end");

    copy_source_run_json(error_copy, sizeof(error_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        fallthrough_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_ERROR,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(error_copy, "\"procedureFallthroughPolicy\":\"error\"", "Phase 71F error-policy opposite fixture should serialize error mode");
    failures += expect_json_contains(error_copy, "\"kind\":\"runtime-error\"", "Phase 71F error-policy opposite fixture should emit runtime-error fallthrough");
    failures += expect_json_contains(error_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 71F error-policy opposite fixture should stop before destination procedure instruction");
    failures += expect_json_not_contains(error_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F error-policy opposite fixture should stop before code-end falloff");

    copy_source_run_json(exit_code_stream_copy, sizeof(exit_code_stream_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        exit_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(exit_code_stream_copy, "\"ok\":true", "Phase 71F Irvine32 exit should terminate explicitly in code-stream mode");
    failures += expect_json_contains(exit_code_stream_copy, "\"EAX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "Phase 71F Irvine32 exit code-stream fixture should preserve earlier side effects");
    failures += expect_json_not_contains(exit_code_stream_copy, "\"code\":\"procedure-fell-through\"", "Phase 71F Irvine32 exit code-stream fixture should not fall through");
    failures += expect_json_not_contains(exit_code_stream_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F Irvine32 exit code-stream fixture should not report code-end falloff");

    copy_source_run_json(exit_stop_copy, sizeof(exit_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        exit_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(exit_stop_copy, "\"ok\":true", "Phase 71F Irvine32 exit should terminate explicitly in stop-at-entry-end mode");
    failures += expect_json_contains(exit_stop_copy, "\"EAX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "Phase 71F Irvine32 exit stop-mode fixture should preserve earlier side effects");
    failures += expect_json_not_contains(exit_stop_copy, "\"code\":\"procedure-fell-through\"", "Phase 71F Irvine32 exit stop-mode fixture should not fall through");
    failures += expect_json_not_contains(exit_stop_copy, "\"code\":\"code-fell-off-end\"", "Phase 71F Irvine32 exit stop-mode fixture should not report code-end falloff");

    copy_source_run_json(root_compatible_code_stream_copy, sizeof(root_compatible_code_stream_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        root_ret_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(root_compatible_code_stream_copy, "\"ok\":true", "Phase 71F compatible root RET should complete in code-stream mode");
    failures += expect_json_contains(root_compatible_code_stream_copy, "\"rootRetMode\":\"masm32-compatible\"", "Phase 71F compatible root RET code-stream fixture should serialize rootRetMode");
    failures += expect_json_not_contains(root_compatible_code_stream_copy, "invalid-address", "Phase 71F compatible root RET code-stream fixture should not read empty stack memory");

    copy_source_run_json(root_compatible_stop_copy, sizeof(root_compatible_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        root_ret_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(root_compatible_stop_copy, "\"ok\":true", "Phase 71F compatible root RET should complete in stop-at-entry-end mode");
    failures += expect_json_contains(root_compatible_stop_copy, "\"rootRetMode\":\"masm32-compatible\"", "Phase 71F compatible root RET stop fixture should serialize rootRetMode");
    failures += expect_json_not_contains(root_compatible_stop_copy, "invalid-address", "Phase 71F compatible root RET stop fixture should not read empty stack memory");

    copy_source_run_json(root_strict_code_stream_copy, sizeof(root_strict_code_stream_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        root_ret_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_CODE_STREAM
    ));
    failures += expect_json_contains(root_strict_code_stream_copy, "\"ok\":false", "Phase 71F strict root RET should reject in code-stream mode");
    failures += expect_json_contains(root_strict_code_stream_copy, "\"code\":\"root-ret-disallowed-by-mode\"", "Phase 71F strict root RET code-stream fixture should use rootRetMode diagnostic");
    failures += expect_json_not_contains(root_strict_code_stream_copy, "invalid-address", "Phase 71F strict root RET code-stream fixture should not read empty stack memory");

    copy_source_run_json(root_strict_stop_copy, sizeof(root_strict_stop_copy), masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_and_entry_end_settings(
        root_ret_source,
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        0U,
        1000000U,
        MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME,
        MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN,
        MASM32_SIM_WASM_ENTRY_PROCEDURE_END_STOP_AT_ENTRY_END
    ));
    failures += expect_json_contains(root_strict_stop_copy, "\"ok\":false", "Phase 71F strict root RET should reject in stop-at-entry-end mode");
    failures += expect_json_contains(root_strict_stop_copy, "\"code\":\"root-ret-disallowed-by-mode\"", "Phase 71F strict root RET stop fixture should use rootRetMode diagnostic");
    failures += expect_json_not_contains(root_strict_stop_copy, "invalid-address", "Phase 71F strict root RET stop fixture should not read empty stack memory");

    return failures;
}

/// Verifies current Phase 77 runtime/source-run status metadata is emitted by source-run JSON.
///
/// @return Number of failures.
static int test_current_source_run_phase_metadata(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Runtime metadata should report numeric Phase 77 metadata");
    failures += expect_json_contains(json, "\"phaseSuffix\":\"\"", "Phase 77 should report the suffix field");
    failures += expect_json_contains(json, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "Phase 77 should report the runtime phase name");
    failures += expect_json_contains(json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Phase 77 should report separate source-run output-contract metadata");
    failures += expect_json_contains(json, "\"entryProcedureEndMode\":\"code-stream\"", "Phase 71E should serialize the default entry procedure end mode");

    return failures;
}


/// Verifies Phase 71B source-run diagnostics use stable wording without milestone-number prose.
///
/// @return Number of failures.
static int test_phase71b_source_run_diagnostic_wording_cleanup(void) {
    int failures = 0;
    char imul_copy[TEST_JSON_COPY_CAPACITY];
    char call_label_copy[TEST_JSON_COPY_CAPACITY];
    char call_data_copy[TEST_JSON_COPY_CAPACITY];
    char call_reserved_copy[TEST_JSON_COPY_CAPACITY];
    char call_distance_copy[TEST_JSON_COPY_CAPACITY];
    char call_memory_copy[TEST_JSON_COPY_CAPACITY];
    char call_register_copy[TEST_JSON_COPY_CAPACITY];
    char call_immediate_copy[TEST_JSON_COPY_CAPACITY];
    char call_directive_copy[TEST_JSON_COPY_CAPACITY];
    char call_offset_copy[TEST_JSON_COPY_CAPACITY];
    char ret_operand_copy[TEST_JSON_COPY_CAPACITY];
    char retf_copy[TEST_JSON_COPY_CAPACITY];
    const char *const diagnostics[] = {
        imul_copy,
        call_label_copy,
        call_data_copy,
        call_reserved_copy,
        call_distance_copy,
        call_memory_copy,
        call_register_copy,
        call_immediate_copy,
        call_directive_copy,
        call_offset_copy,
        ret_operand_copy,
        retf_copy,
        NULL
    };
    size_t index = 0U;

    copy_source_run_json(imul_copy, sizeof(imul_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    imul eax, 5\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_label_copy, sizeof(call_label_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "LabelOnly:\n"
        "    call LabelOnly\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_data_copy, sizeof(call_data_copy), masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    call value\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_reserved_copy, sizeof(call_reserved_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call ret\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_distance_copy, sizeof(call_distance_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call NEAR PTR Helper\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_memory_copy, sizeof(call_memory_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_register_copy, sizeof(call_register_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_immediate_copy, sizeof(call_immediate_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call 1234\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_directive_copy, sizeof(call_directive_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call .code\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(call_offset_copy, sizeof(call_offset_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    call OFFSET Helper\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(ret_operand_copy, sizeof(ret_operand_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ret eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(retf_copy, sizeof(retf_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    retf\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(imul_copy, "\"code\":\"invalid-instruction-operands\"", "Phase 71B IMUL source-run diagnostic should preserve code");
    failures += expect_json_contains(imul_copy, "This IMUL form is not accepted", "Phase 71B IMUL source-run diagnostic should use stable wording");
    failures += expect_json_contains(imul_copy, "\"line\":3", "Phase 71B IMUL diagnostic should preserve line");
    failures += expect_json_contains(imul_copy, "\"column\":15", "Phase 71B IMUL diagnostic should preserve column");
    failures += expect_json_contains(imul_copy, "\"byteOffset\":30", "Phase 71B IMUL diagnostic should preserve byte offset");
    failures += expect_json_contains(imul_copy, "\"spanLength\":1", "Phase 71B IMUL diagnostic should preserve span");
    failures += expect_json_contains(imul_copy, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Phase 71B IMUL source-run diagnostic should report updated output contract");

    failures += expect_json_contains(call_label_copy, "\"code\":\"invalid-call-target\"", "Phase 71B ordinary-label CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_label_copy, "This simulator accepts only user procedure entries", "Phase 71B ordinary-label CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_label_copy, "\"line\":4", "Phase 71B ordinary-label CALL diagnostic should preserve line");
    failures += expect_json_contains(call_label_copy, "\"column\":10", "Phase 71B ordinary-label CALL diagnostic should preserve column");
    failures += expect_json_contains(call_label_copy, "\"byteOffset\":36", "Phase 71B ordinary-label CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_label_copy, "\"spanLength\":9", "Phase 71B ordinary-label CALL diagnostic should preserve span");

    failures += expect_json_contains(call_data_copy, "\"code\":\"invalid-call-target\"", "Phase 71B data-symbol CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_data_copy, "CALL target cannot be a data symbol", "Phase 71B data-symbol CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_data_copy, "This simulator accepts only user procedure entries", "Phase 71B data-symbol CALL source-run diagnostic should describe the accepted subset");
    failures += expect_json_contains(call_data_copy, "\"line\":5", "Phase 71B data-symbol CALL diagnostic should preserve line");
    failures += expect_json_contains(call_data_copy, "\"column\":10", "Phase 71B data-symbol CALL diagnostic should preserve column");
    failures += expect_json_contains(call_data_copy, "\"byteOffset\":45", "Phase 71B data-symbol CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_data_copy, "\"spanLength\":5", "Phase 71B data-symbol CALL diagnostic should preserve span");

    failures += expect_json_contains(call_reserved_copy, "\"code\":\"invalid-call-target\"", "Phase 71B reserved-word CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_reserved_copy, "CALL target cannot be a reserved MASM or simulator word", "Phase 71B reserved-word CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_reserved_copy, "This simulator accepts only user procedure entries", "Phase 71B reserved-word CALL source-run diagnostic should describe the accepted subset");
    failures += expect_json_contains(call_reserved_copy, "\"line\":3", "Phase 71B reserved-word CALL diagnostic should preserve line");
    failures += expect_json_contains(call_reserved_copy, "\"column\":10", "Phase 71B reserved-word CALL diagnostic should preserve column");
    failures += expect_json_contains(call_reserved_copy, "\"byteOffset\":25", "Phase 71B reserved-word CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_reserved_copy, "\"spanLength\":3", "Phase 71B reserved-word CALL diagnostic should preserve span");

    failures += expect_json_contains(call_distance_copy, "\"code\":\"unsupported-call-form\"", "Phase 71B distance-override CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_distance_copy, "CALL distance and type overrides", "Phase 71B distance-override CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_distance_copy, "direct user-procedure CALL targets", "Phase 71B distance-override CALL source-run diagnostic should describe the accepted subset");
    failures += expect_json_contains(call_distance_copy, "\"line\":3", "Phase 71B distance-override CALL diagnostic should preserve line");
    failures += expect_json_contains(call_distance_copy, "\"column\":10", "Phase 71B distance-override CALL diagnostic should preserve column");
    failures += expect_json_contains(call_distance_copy, "\"byteOffset\":25", "Phase 71B distance-override CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_distance_copy, "\"spanLength\":4", "Phase 71B distance-override CALL diagnostic should preserve span");

    failures += expect_json_contains(call_memory_copy, "\"code\":\"unsupported-call-form\"", "Phase 71B memory CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_memory_copy, "CALL memory targets are not implemented", "Phase 71B memory CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_memory_copy, "direct user-procedure CALL targets", "Phase 71B memory CALL source-run diagnostic should describe the accepted subset");
    failures += expect_json_contains(call_memory_copy, "\"line\":3", "Phase 71B memory CALL diagnostic should preserve line");
    failures += expect_json_contains(call_memory_copy, "\"column\":10", "Phase 71B memory CALL diagnostic should preserve column");
    failures += expect_json_contains(call_memory_copy, "\"byteOffset\":25", "Phase 71B memory CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_memory_copy, "\"spanLength\":5", "Phase 71B memory CALL diagnostic should preserve span");

    failures += expect_json_contains(call_register_copy, "\"code\":\"unsupported-call-form\"", "Phase 71B register CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_register_copy, "direct user-procedure CALL targets", "Phase 71B register CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_register_copy, "\"line\":3", "Phase 71B register CALL diagnostic should preserve line");
    failures += expect_json_contains(call_register_copy, "\"column\":10", "Phase 71B register CALL diagnostic should preserve column");
    failures += expect_json_contains(call_register_copy, "\"byteOffset\":25", "Phase 71B register CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_register_copy, "\"spanLength\":3", "Phase 71B register CALL diagnostic should preserve span");

    failures += expect_json_contains(call_immediate_copy, "\"code\":\"unsupported-call-form\"", "Phase 71B immediate CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_immediate_copy, "CALL expression and immediate targets are not implemented", "Phase 71B immediate CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_immediate_copy, "direct user-procedure CALL targets", "Phase 71B immediate CALL source-run diagnostic should describe the accepted subset");
    failures += expect_json_contains(call_immediate_copy, "\"line\":3", "Phase 71B immediate CALL diagnostic should preserve line");
    failures += expect_json_contains(call_immediate_copy, "\"column\":10", "Phase 71B immediate CALL diagnostic should preserve column");
    failures += expect_json_contains(call_immediate_copy, "\"byteOffset\":25", "Phase 71B immediate CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_immediate_copy, "\"spanLength\":4", "Phase 71B immediate CALL diagnostic should preserve span");

    failures += expect_json_contains(call_directive_copy, "\"code\":\"invalid-call-target\"", "Phase 71B directive CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_directive_copy, "CALL target cannot be a directive name", "Phase 71B directive CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_directive_copy, "This simulator accepts only user procedure entries", "Phase 71B directive CALL source-run diagnostic should describe the accepted subset");
    failures += expect_json_contains(call_directive_copy, "\"line\":3", "Phase 71B directive CALL diagnostic should preserve line");
    failures += expect_json_contains(call_directive_copy, "\"column\":10", "Phase 71B directive CALL diagnostic should preserve column");
    failures += expect_json_contains(call_directive_copy, "\"byteOffset\":25", "Phase 71B directive CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_directive_copy, "\"spanLength\":5", "Phase 71B directive CALL diagnostic should preserve span");

    failures += expect_json_contains(call_offset_copy, "\"code\":\"unsupported-call-form\"", "Phase 71B OFFSET CALL source-run diagnostic should preserve code");
    failures += expect_json_contains(call_offset_copy, "CALL OFFSET targets are not implemented", "Phase 71B OFFSET CALL source-run diagnostic should use stable wording");
    failures += expect_json_contains(call_offset_copy, "\"line\":3", "Phase 71B OFFSET CALL diagnostic should preserve line");
    failures += expect_json_contains(call_offset_copy, "\"column\":10", "Phase 71B OFFSET CALL diagnostic should preserve column");
    failures += expect_json_contains(call_offset_copy, "\"byteOffset\":25", "Phase 71B OFFSET CALL diagnostic should preserve byte offset");
    failures += expect_json_contains(call_offset_copy, "\"spanLength\":6", "Phase 71B OFFSET CALL diagnostic should preserve span");

    failures += expect_json_contains(ret_operand_copy, "\"code\":\"unsupported-instruction-form\"", "Phase 71B RET operand source-run diagnostic should preserve code");
    failures += expect_json_contains(ret_operand_copy, "unsigned 16-bit immediate", "Phase 74 RET register diagnostic should use stable wording");
    failures += expect_json_contains(ret_operand_copy, "\"line\":3", "Phase 71B RET operand diagnostic should preserve line");
    failures += expect_json_contains(ret_operand_copy, "\"column\":9", "Phase 71B RET operand diagnostic should preserve column");
    failures += expect_json_contains(ret_operand_copy, "\"byteOffset\":24", "Phase 71B RET operand diagnostic should preserve byte offset");
    failures += expect_json_contains(ret_operand_copy, "\"spanLength\":3", "Phase 74 RET register diagnostic should span register operand");
    failures += expect_json_contains(ret_operand_copy, "\"status\":\"parse-error\"", "Phase 71B RET operand source-run diagnostic should preserve terminal status");

    failures += expect_json_contains(retf_copy, "\"code\":\"unsupported-instruction-form\"", "Phase 71B RETF source-run diagnostic should preserve code");
    failures += expect_json_contains(retf_copy, "Far RET forms are not implemented", "Phase 71B RETF source-run diagnostic should use stable wording");
    failures += expect_json_contains(retf_copy, "\"line\":3", "Phase 71B RETF diagnostic should preserve line");
    failures += expect_json_contains(retf_copy, "\"column\":5", "Phase 71B RETF diagnostic should preserve column");
    failures += expect_json_contains(retf_copy, "\"byteOffset\":20", "Phase 71B RETF diagnostic should preserve byte offset");
    failures += expect_json_contains(retf_copy, "\"spanLength\":4", "Phase 71B RETF diagnostic should preserve span");

    for (index = 0U; diagnostics[index] != NULL; index += 1U) {
        failures += expect_json_not_contains(diagnostics[index], "not supported in Phase", "Phase 71B source-run diagnostics must not use not-supported-in-Phase wording");
        failures += expect_json_not_contains(diagnostics[index], "outside Phase", "Phase 71B source-run diagnostics must not use outside-Phase wording");
        failures += expect_json_not_contains(diagnostics[index], "Phase 69 accepts only", "Phase 71B source-run diagnostics must not use Phase 69 accepts-only wording");
        failures += expect_json_not_contains(diagnostics[index], "Phase 69 direct CALL accepts only", "Phase 71B source-run diagnostics must not use Phase 69 direct-CALL wording");
        failures += expect_json_not_contains(diagnostics[index], "Phase 70 implements only", "Phase 71B source-run diagnostics must not use Phase 70 implements-only wording");
        failures += expect_json_not_contains(diagnostics[index], "deferred to Phase", "Phase 71B source-run diagnostics must not use deferred-to-Phase wording");
        failures += expect_json_not_contains(diagnostics[index], "execution-complete", "Phase 71B parser diagnostics must not execute source");
    }

    return failures;
}


/// Verifies Phase 68B source-run EIP display and source-operand restrictions.
///
/// @return Number of failures.
static int test_phase68b_eip_pseudo_display_and_rejections(void) {
    int failures = 0;
    char fallthrough_copy[TEST_JSON_COPY_CAPACITY];
    char jmp_copy[TEST_JSON_COPY_CAPACITY];
    char empty_entry_copy[TEST_JSON_COPY_CAPACITY];
    char no_executable_copy[TEST_JSON_COPY_CAPACITY];
    char conditional_taken_copy[TEST_JSON_COPY_CAPACITY];
    char conditional_not_taken_copy[TEST_JSON_COPY_CAPACITY];
    char runtime_error_copy[TEST_JSON_COPY_CAPACITY];
    char watchdog_copy[TEST_JSON_COPY_CAPACITY];
    char invalid_copy[TEST_JSON_COPY_CAPACITY];
    char esp_copy[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(fallthrough_copy, sizeof(fallthrough_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    mov ebx, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(fallthrough_copy, "\"ok\":true", "Phase 68B fallthrough fixture should run");
    failures += expect_json_contains(fallthrough_copy, "\"EIP\":{\"hex\":\"00401008h\",\"unsigned\":4198408}", "selected-entry fallthrough should leave displayed EIP at the last executed pseudo-address");
    failures += expect_json_contains(fallthrough_copy, "\"registerRoles\":{\"EIP\":\"derived-control-state\",\"ESP\":\"stack-pointer\"}", "Phase 68B should expose register display-role metadata");
    failures += expect_json_contains(fallthrough_copy, "\"EIP\":false", "Phase 68B should not report displayed EIP as a source-written register");

    copy_source_run_json(jmp_copy, sizeof(jmp_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jmp target\n"
        "    mov eax, 1\n"
        "target:\n"
        "    mov eax, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(jmp_copy, "\"ok\":true", "Phase 68B direct JMP fixture should run");
    failures += expect_json_contains(jmp_copy, "\"EAX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "Phase 68B JMP fixture should execute the branch target");
    failures += expect_json_contains(jmp_copy, "\"EIP\":{\"hex\":\"0040100Ch\",\"unsigned\":4198412}", "Direct JMP fallthrough boundary should leave displayed EIP at target instruction pseudo-address");

    copy_source_run_json(empty_entry_copy, sizeof(empty_entry_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "helper PROC\n"
        "    mov eax, 1\n"
        "helper ENDP\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(empty_entry_copy, "\"ok\":true", "Phase 68B explicit root RET selected-entry fixture should complete");
    failures += expect_json_contains(empty_entry_copy, "\"executedInstructionCount\":1", "Phase 68B explicit root RET selected-entry fixture should execute the RET instruction");
    failures += expect_json_contains(empty_entry_copy, "\"currentInstructionIndex\":0", "Phase 68B explicit root RET selected-entry fixture should expose the RET instruction index");
    failures += expect_json_contains(empty_entry_copy, "\"attemptedNextInstructionIndex\":null", "Phase 68B explicit root RET selected-entry fixture should not expose attempted next instruction");
    failures += expect_json_contains(empty_entry_copy, "\"EIP\":{\"hex\":\"00401004h\",\"unsigned\":4198404}", "Phase 68B explicit root RET selected-entry fixture should display the RET pseudo-EIP");

    copy_source_run_json(no_executable_copy, sizeof(no_executable_copy), masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(no_executable_copy, "\"ok\":true", "Phase 68B explicit root RET data-plus-code fixture should complete");
    failures += expect_json_contains(no_executable_copy, "\"instructionCount\":1", "Phase 68B explicit root RET data-plus-code fixture should report one lowered instruction");
    failures += expect_json_contains(no_executable_copy, "\"executedInstructionCount\":1", "Phase 68B explicit root RET data-plus-code fixture should execute the RET instruction");
    failures += expect_json_contains(no_executable_copy, "\"currentInstructionIndex\":null", "Phase 68B explicit root RET data-plus-code fixture should expose no current instruction after halt");
    failures += expect_json_contains(no_executable_copy, "\"attemptedNextInstructionIndex\":null", "Phase 68B no-executable program should not expose attempted next instruction");
    failures += expect_json_contains(no_executable_copy, "\"EIP\":{\"hex\":\"00401000h\",\"unsigned\":4198400}", "Phase 68B explicit root RET data-plus-code fixture should display base pseudo-EIP");
    failures += expect_json_not_contains(no_executable_copy, "runtime-error", "Phase 68B no-executable program should not emit a runtime error");

    copy_source_run_json(conditional_taken_copy, sizeof(conditional_taken_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    cmp eax, 1\n"
        "    je target\n"
        "    mov ebx, 1\n"
        "target:\n"
        "    mov ecx, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(conditional_taken_copy, "\"ok\":true", "Phase 68B conditional taken fixture should run");
    failures += expect_json_contains(conditional_taken_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 68B taken JE should skip the fallthrough body");
    failures += expect_json_contains(conditional_taken_copy, "\"EIP\":{\"hex\":\"00401014h\",\"unsigned\":4198420}", "Phase 68B taken JE should leave EIP at target pseudo-address after boundary completion");

    copy_source_run_json(conditional_not_taken_copy, sizeof(conditional_not_taken_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    cmp eax, 2\n"
        "    je target\n"
        "    mov ebx, 3\n"
        "    jmp done\n"
        "target:\n"
        "    mov ebx, 99\n"
        "done:\n"
        "    mov ecx, 4\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(conditional_not_taken_copy, "\"ok\":true", "Phase 68B conditional not-taken fixture should run");
    failures += expect_json_contains(conditional_not_taken_copy, "\"EBX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "Phase 68B not-taken JE should execute the fallthrough instruction");
    failures += expect_json_contains(conditional_not_taken_copy, "\"ECX\":{\"hex\":\"00000004h\",\"unsigned\":4}", "Phase 68B not-taken fixture should complete at the shared done label");
    failures += expect_json_contains(conditional_not_taken_copy, "\"EIP\":{\"hex\":\"0040101Ch\",\"unsigned\":4198428}", "Phase 68B not-taken fixture should leave EIP at the final completed instruction pseudo-address");

    copy_source_run_json(runtime_error_copy, sizeof(runtime_error_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 10\n"
        "    mov ebx, 0\n"
        "    div ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(runtime_error_copy, "\"ok\":false", "Phase 68B runtime-error fixture should fail");
    failures += expect_json_contains(runtime_error_copy, "divide-by-zero", "Phase 68B runtime-error fixture should report divide-by-zero");
    failures += expect_json_contains(runtime_error_copy, "\"EIP\":{\"hex\":\"00401008h\",\"unsigned\":4198408}", "Phase 68B runtime error should leave EIP at the failing instruction pseudo-address");

    copy_source_run_json(watchdog_copy, sizeof(watchdog_copy), masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "start:\n"
        "    inc eax\n"
        "    jmp start\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        3U
    ));
    failures += expect_json_contains(watchdog_copy, "\"ok\":false", "Phase 68B watchdog fixture should fail");
    failures += expect_json_contains(watchdog_copy, "instruction-limit-exceeded", "Phase 68B watchdog fixture should report instruction-limit-exceeded");
    failures += expect_json_contains(watchdog_copy, "\"attemptedNextInstructionIndex\":1", "Phase 68B watchdog should identify the blocked next instruction index");
    failures += expect_json_contains(watchdog_copy, "\"EIP\":{\"hex\":\"00401004h\",\"unsigned\":4198404}", "Phase 68B watchdog stop should leave EIP at the instruction where execution stopped");

    copy_source_run_json(invalid_copy, sizeof(invalid_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, eip\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_copy, "\"ok\":false", "Phase 68B EIP source operand should fail parsing");
    failures += expect_json_contains(invalid_copy, "\"code\":\"invalid-eip-operand\"", "Phase 68B EIP operand should use structured diagnostic code");
    failures += expect_json_contains(invalid_copy, "displayed VM control state", "Phase 68B rendered diagnostic should explain EIP control state");
    failures += expect_json_contains(invalid_copy, "\"line\":3", "Phase 68B EIP diagnostic should preserve line");
    failures += expect_json_contains(invalid_copy, "\"column\":14", "Phase 68B EIP diagnostic should preserve column");
    failures += expect_json_contains(invalid_copy, "\"spanLength\":3", "Phase 68B EIP diagnostic should preserve span length");

    copy_source_run_json(esp_copy, sizeof(esp_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov esp, 1\n"
        "    add esp, 4\n"
        "    sub esp, 4\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(esp_copy, "\"ok\":true", "Phase 68B should preserve explicit supported ESP writes");
    failures += expect_json_contains(esp_copy, "\"ESP\":{\"hex\":\"00000001h\",\"unsigned\":1}", "Phase 68B explicit ESP writes should remain visible in final registers");

    return failures;
}


/// Verifies Phase 67A selected END-entry startup and procedure-boundary fallthrough.
///
/// @return Number of failures.
static int test_phase67a_entry_procedure_runtime_boundary_source_run(void) {
    int failures = 0;
    char before_copy[TEST_JSON_COPY_CAPACITY];
    char after_copy[TEST_JSON_COPY_CAPACITY];
    char empty_copy[TEST_JSON_COPY_CAPACITY];
    char selected_copy[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(before_copy, sizeof(before_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "helper PROC\n"
        "    mov ecx, 111\n"
        "helper ENDP\n"
        "main PROC\n"
        "    mov eax, 100\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(before_copy, "\"ok\":true", "Phase 67A helper-before-entry source should execute");
    failures += expect_json_contains(before_copy, "\"phase\":77", "Phase 67A helper-before-entry source should report numeric phase metadata");
    failures += expect_json_contains(before_copy, "\"phaseSuffix\":\"\"", "Helper-before-entry source should report current suffix metadata");
    failures += expect_json_contains(before_copy, "\"executedInstructionCount\":2", "Phase 67A should execute only the selected entry instruction");
    failures += expect_json_contains(before_copy, "\"EAX\":{\"hex\":\"00000064h\",\"unsigned\":100}", "selected main procedure should set EAX");
    failures += expect_json_contains(before_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "helper before main must not execute by source order");
    failures += expect_json_contains(before_copy, "\"code\":\"execution-complete\"", "selected main explicit RET should complete normally");

    copy_source_run_json(after_copy, sizeof(after_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ecx, 2\n"
        "helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(after_copy, "\"ok\":true", "Phase 67A helper-after-entry source should execute");
    failures += expect_json_contains(after_copy, "\"executedInstructionCount\":2", "Phase 67A explicit RET should stop before later procedure text");
    failures += expect_json_contains(after_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "selected main should set EAX once");
    failures += expect_json_contains(after_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "helper after main must not execute after explicit RET");
    failures += expect_json_contains_once(after_copy, "execution-complete", "selected-entry explicit RET should emit exactly one completion message");

    copy_source_run_json(empty_copy, sizeof(empty_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "helper PROC\n"
        "    mov ecx, 3\n"
        "helper ENDP\n"
        "main PROC\n"
        "    ret\n"
        "main ENDP\n"
        "after PROC\n"
        "    mov eax, 4\n"
        "after ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(empty_copy, "\"ok\":true", "Phase 67A explicit root RET entry source should complete successfully");
    failures += expect_json_contains(empty_copy, "\"executedInstructionCount\":1", "explicit root RET selected entry should execute the RET instruction");
    failures += expect_json_contains(empty_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "explicit root RET selected entry must not run later procedure");
    failures += expect_json_contains(empty_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "explicit root RET selected entry must not run earlier procedure");
    failures += expect_json_contains(empty_copy, "\"code\":\"execution-complete\"", "explicit root RET selected entry should complete normally");

    copy_source_run_json(selected_copy, sizeof(selected_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 9\n"
        "    ret\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov ebx, 7\n"
        "    ret\n"
        "helper ENDP\n"
        "END helper\n"
    ));
    failures += expect_json_contains(selected_copy, "\"ok\":true", "Phase 67A END helper source should execute");
    failures += expect_json_contains(selected_copy, "\"executedInstructionCount\":2", "END helper should execute only helper body");
    failures += expect_json_contains(selected_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "non-selected main procedure must not execute");
    failures += expect_json_contains(selected_copy, "\"EBX\":{\"hex\":\"00000007h\",\"unsigned\":7}", "selected helper procedure should execute");

    return failures;
}

/// Verifies Phase 68A source-run programs can observe the documented fixed-layout ESP startup value.
///
/// @return Number of failures.
static int test_phase68a_source_run_observes_fixed_layout_esp_startup(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, esp\n"
        "    mov ebx, 008FF000h\n"
        "    mov DWORD PTR [ebx], 456\n"
        "    mov ecx, DWORD PTR [ebx]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 68A ESP startup source-run fixture should succeed");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00900000h\",\"unsigned\":9437184", "Program should observe fixed-layout empty-stack ESP in EAX");
    failures += expect_json_contains(json, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184", "Final ESP should remain the fixed-layout empty-stack value");
    failures += expect_json_contains(json, "execution-complete", "Successful ESP startup fixture should emit completion");

    return failures;
}

/// Verifies Phase 68A keeps ESP on the stack contract after seeded register startup.
///
/// @return Number of failures.
static int test_phase68a_seeded_startup_preserves_stack_pointer_contract(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        123456789U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Seeded startup with Phase 68A ESP contract should execute");
    failures += expect_json_not_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0", "Seeded startup should still affect non-ESP registers");
    failures += expect_json_contains(json, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184", "Seeded startup should leave ESP on the stack startup contract");

    return failures;
}

/// Verifies Phase 68A automatic layout derives ESP from selected stack metadata.
///
/// @return Number of failures.
static int test_phase68a_automatic_layout_esp_uses_selected_stack_metadata(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(
        ".stack 4096\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, esp\n"
        "    mov ebx, 008FF000h\n"
        "    mov DWORD PTR [ebx], 456\n"
        "    mov ecx, DWORD PTR [ebx]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        NULL
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Automatic layout ESP fixture should succeed");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00900000h\",\"unsigned\":9437184", "Automatic layout should expose selected stack limit through ESP");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"000001C8h\",\"unsigned\":456", "Automatic layout should allow access at the requested 4096-byte stack base");
    failures += expect_json_contains(json, "\"ESP\":{\"hex\":\"00900000h\",\"unsigned\":9437184", "Automatic layout final ESP should match selected stack limit");

    return failures;
}

/// Verifies Phase 67A preserves fatal diagnostics and explicit transfers around entry boundaries.
///
/// @return Number of failures.
static int test_phase67a_entry_boundary_error_and_transfer_regressions(void) {
    int failures = 0;
    char fatal_copy[TEST_JSON_COPY_CAPACITY];
    char exit_copy[TEST_JSON_COPY_CAPACITY];
    char jmp_copy[TEST_JSON_COPY_CAPACITY];
    char conditional_copy[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(fatal_copy, sizeof(fatal_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "helper PROC\n"
        "    mov ecx, 55\n"
        "helper ENDP\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    mov ebx, 0\n"
        "    div ebx\n"
        "    ret\n"
        "main ENDP\n"
        "after PROC\n"
        "    mov edx, 99\n"
        "after ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(fatal_copy, "\"ok\":false", "Phase 67A fatal entry fixture should stop with runtime error");
    failures += expect_json_contains(fatal_copy, "\"code\":\"divide-by-zero\"", "Phase 67A fatal entry fixture should report divide-by-zero");
    failures += expect_json_not_contains(fatal_copy, "execution-complete", "fatal entry diagnostic must suppress completion");
    failures += expect_json_contains(fatal_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "helper before fatal entry must not execute");
    failures += expect_json_contains(fatal_copy, "\"EDX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "helper after fatal entry must not execute");

    copy_source_run_json(exit_copy, sizeof(exit_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "helper PROC\n"
        "    mov ecx, 77\n"
        "helper ENDP\n"
        "main PROC\n"
        "    mov eax, 123\n"
        "    exit\n"
        "    mov eax, 999\n"
        "    ret\n"
        "main ENDP\n"
        "after PROC\n"
        "    mov ebx, 1\n"
        "after ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(exit_copy, "\"ok\":true", "Phase 67A exit fixture should complete successfully");
    failures += expect_json_contains(exit_copy, "\"EAX\":{\"hex\":\"0000007Bh\",\"unsigned\":123}", "exit should stop before later selected-entry instruction");
    failures += expect_json_contains(exit_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "helper before exit entry must not execute");
    failures += expect_json_contains(exit_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "helper after exit entry must not execute");
    failures += expect_json_contains_once(exit_copy, "execution-complete", "exit fixture should emit one completion message");

    copy_source_run_json(jmp_copy, sizeof(jmp_copy), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    jmp helper\n"
        "    ret\n"
        "main ENDP\n"
        "helper PROC\n"
        "    mov eax, 42\n"
        "    exit\n"
        "helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(jmp_copy, "\"ok\":true", "Phase 67A should preserve explicit JMP to procedure-entry label");
    failures += expect_json_contains(jmp_copy, "\"executedInstructionCount\":3", "explicit JMP outside selected entry should execute target instruction");
    failures += expect_json_contains(jmp_copy, "\"EAX\":{\"hex\":\"0000002Ah\",\"unsigned\":42}", "explicit JMP target procedure should execute");

    copy_source_run_json(conditional_copy, sizeof(conditional_copy), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    cmp eax, eax\n"
        "    jne after\n"
        "    ret\n"
        "main ENDP\n"
        "after PROC\n"
        "    mov ecx, 8\n"
        "after ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(conditional_copy, "\"ok\":true", "Phase 67A conditional fallthrough fixture should complete");
    failures += expect_json_contains(conditional_copy, "\"executedInstructionCount\":3", "non-taken final conditional jump should count before boundary completion");
    failures += expect_json_contains(conditional_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "non-taken final Jcc must not fall through into later procedure");
    failures += expect_json_contains_once(conditional_copy, "execution-complete", "conditional fallthrough should emit one completion message");

    return failures;
}

/// Verifies Phase 67 combined arithmetic, branch, and watchdog source-run coverage.
///
/// @return Number of failures.
static int test_phase67_arithmetic_branch_watchdog_source_run_harness(void) {
    char combined_json[TEST_JSON_COPY_CAPACITY];
    char no_memory_json[TEST_JSON_COPY_CAPACITY];
    char watchdog_json[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    copy_source_run_json(combined_json, sizeof(combined_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 0FFFFFFFFh\n"
        "    cmp eax, 0FFFFFFFFh\n"
        "    je equal_path\n"
        "    mov ebx, 99\n"
        "equal_path:\n"
        "    mov ebx, 1\n"
        "    cmp eax, 1\n"
        "    jl signed_path\n"
        "    mov ecx, 99\n"
        "signed_path:\n"
        "    mov ecx, 2\n"
        "    cmp eax, 1\n"
        "    ja unsigned_path\n"
        "    mov edx, 99\n"
        "    jmp done\n"
        "unsigned_path:\n"
        "    mov edx, 3\n"
        "done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(no_memory_json, sizeof(no_memory_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov ebx, 8\n"
        "    lea eax, [ebx + 4]\n"
        "    cmp eax, 12\n"
        "    mul ebx\n"
        "    mov eax, 100\n"
        "    mov edx, 0\n"
        "    div ebx\n"
        "    mov eax, -12\n"
        "    cdq\n"
        "    mov ebx, 3\n"
        "    idiv ebx\n"
        "    mov eax, -2\n"
        "    imul ebx\n"
        "    cmp eax, 0FFFFFFFAh\n"
        "    je equal_done\n"
        "    mov esi, 99\n"
        "equal_done:\n"
        "    jb unsigned_done\n"
        "    jl signed_done\n"
        "signed_done:\n"
        "    jmp final_done\n"
        "unsigned_done:\n"
        "    mov edi, 99\n"
        "final_done:\n"
        "    nop\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    copy_source_run_json(watchdog_json, sizeof(watchdog_json), masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "again:\n"
        "    cmp eax, eax\n"
        "    je taken\n"
        "    mov ebx, 99\n"
        "taken:\n"
        "    jmp again\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        5U
    ));

    failures += expect_json_contains(combined_json, "\"ok\":true", "Phase 67 combined cmp/Jcc fixture should execute");
    failures += expect_json_contains(combined_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1", "Phase 67 equality Jcc should take the equal path");
    failures += expect_json_contains(combined_json, "\"ECX\":{\"hex\":\"00000002h\",\"unsigned\":2", "Phase 67 signed Jcc should take the signed less path");
    failures += expect_json_contains(combined_json, "\"EDX\":{\"hex\":\"00000003h\",\"unsigned\":3", "Phase 67 unsigned Jcc should take the unsigned above path");
    failures += expect_json_not_contains(combined_json, "branch-runtime-deferred", "Phase 67 combined branch fixture should not emit deferred branch diagnostics");
    failures += expect_json_contains(combined_json, "\"memoryChanges\":[]", "Phase 67 combined branch fixture should not create memory changes");

    failures += expect_json_contains(no_memory_json, "\"ok\":true", "Phase 67 no-memory-change arithmetic/branch fixture should execute");
    failures += expect_json_contains(no_memory_json, "\"memoryChanges\":[]", "LEA, CMP, MUL, IMUL, DIV, IDIV, JMP, and Jcc should not create memory-change rows in this fixture");
    failures += expect_json_contains(no_memory_json, "\"EAX\":{\"hex\":\"FFFFFFFAh\",\"unsigned\":4294967290", "Phase 67 arithmetic fixture should leave IMUL result in EAX");
    failures += expect_json_not_contains(no_memory_json, "branch-runtime-deferred", "Phase 67 no-memory-change fixture should not emit deferred branch diagnostics");

    failures += expect_json_contains(watchdog_json, "\"ok\":false", "Phase 67 branch-loop watchdog fixture should fail by instruction limit");
    failures += expect_json_contains(watchdog_json, "\"code\":\"instruction-limit-exceeded\"", "Phase 67 branch-loop watchdog fixture should emit instruction-limit-exceeded");
    failures += expect_json_contains(watchdog_json, "\"instructionCount\":5", "Phase 67 watchdog fixture should stop after the configured executed-instruction count");
    failures += expect_json_contains(watchdog_json, "\"memoryChanges\":[]", "Phase 67 watchdog fixture should not create memory changes");
    failures += expect_json_not_contains(watchdog_json, "branch-runtime-deferred", "Phase 67 watchdog fixture should not emit deferred branch diagnostics");
    failures += expect_json_not_contains(watchdog_json, "execution-complete", "Phase 67 watchdog fixture should not complete after fatal instruction limit");

    return failures;
}



/// Verifies Phase 63 CMP memory source, destination, symbol-offset, and .CONST reads.
///
/// @return Number of failures.
static int test_phase63_cmp_memory_source_run_programs(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 5\n"
        "other DWORD 3\n"
        "nums DWORD 1, 2, 3\n"
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    cmp eax, value\n"
        "    cmp other, eax\n"
        "    cmp nums[4], 2\n"
        "    cmp limit, 10\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 63 CMP memory source-run program should execute");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Phase 63 CMP memory program should execute five instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000005h\",\"unsigned\":5", "CMP memory comparisons should not mutate EAX");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000040h\",\"unsigned\":64}", "final .CONST CMP equality should set ZF only");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "CMP memory comparisons should not create memory-change rows");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 63 CMP memory program should complete");

    return failures;
}

/// Verifies Phase 63 CMP participates in uninitialized-read planned-read policy.
///
/// @return Number of failures.
static int test_phase63_cmp_uninitialized_read_policy(void) {
    const char *source =
        ".DATA?\n"
        "value DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    cmp value, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS);
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "CMP uninitialized-read warning mode should continue");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "CMP memory read should emit uninitialized-read warning");
    failures += expect_json_contains(json, "reads 4 bytes from value + 0", "CMP uninitialized-read warning should describe the memory source range");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000040h\",\"unsigned\":64}", "CMP warning mode should update flags from deterministic zero bytes");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "CMP uninitialized-read warning mode should complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT);
    failures += expect_json_contains(json, "\"ok\":false", "CMP uninitialized-read strict mode should stop");
    failures += expect_json_contains(json, "\"instructionCount\":1", "CMP uninitialized-read strict mode should stop before CMP executes");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "CMP strict mode should emit uninitialized-read error");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000001h\",\"unsigned\":1}", "CMP strict mode should preserve flags from prior STC");
    failures += expect_json_not_contains(json, "execution-complete", "CMP uninitialized-read strict mode should not complete");

    return failures;
}

/// Verifies Phase 63 CMP participates in section and object planned-read validation.
///
/// @return Number of failures.
static int test_phase63_cmp_section_and_object_planned_read_policy(void) {
    VmLayoutPolicy policy = phase53b_data_capacity_test_policy();
    const char *json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    cmp DWORD PTR [eax], 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_WARN
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "CMP section-image warning mode should continue");
    failures += expect_json_contains(json, "\"code\":\"section-image-violation\"", "CMP section-image warning should emit diagnostic");
    failures += expect_json_contains(json, "\"accessKind\":\"read\"", "CMP section-image diagnostic should be a read");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "CMP section-image warning mode should complete");

    json = masm32_sim_wasm_run_source_json_with_section_validation_modes(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    add eax, 4\n"
        "    stc\n"
        "    cmp DWORD PTR [eax], 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "CMP section-image strict mode should stop");
    failures += expect_json_contains(json, "\"instructionCount\":3", "CMP section-image strict mode should stop before CMP executes");
    failures += expect_json_contains(json, "\"code\":\"section-image-violation\"", "CMP section-image strict mode should emit diagnostic");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000001h\",\"unsigned\":1}", "CMP section-image strict mode should preserve flags from STC");
    failures += expect_json_not_contains(json, "execution-complete", "CMP section-image strict mode should not complete");

    json = masm32_sim_wasm_run_source_json_with_automatic_layout_and_section_validation(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 00600000h\n"
        "    cmp DWORD PTR [eax], 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &policy,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_WARN,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF
    );
    failures += expect_json_contains(json, "\"ok\":true", "CMP section-capacity warning mode should continue");
    failures += expect_json_contains(json, "\"code\":\"section-capacity-violation\"", "CMP section-capacity warning should emit diagnostic");
    failures += expect_json_contains(json, "\"accessKind\":\"read\"", "CMP section-capacity diagnostic should be a read");

    json = masm32_sim_wasm_run_source_json_with_automatic_layout_and_section_validation(
        ".data\n"
        "x DWORD 1\n"
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov eax, 00600000h\n"
        "    cmp DWORD PTR [eax], 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        &policy,
        MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY,
        MASM32_SIM_WASM_SECTION_VALIDATION_STRICT,
        MASM32_SIM_WASM_SECTION_VALIDATION_OFF
    );
    failures += expect_json_contains(json, "\"ok\":false", "CMP section-capacity strict mode should stop");
    failures += expect_json_contains(json, "\"code\":\"section-capacity-violation\"", "CMP section-capacity strict should emit diagnostic");
    failures += expect_json_contains(json, "\"accessKind\":\"read\"", "CMP section-capacity strict diagnostic should be a read");
    failures += expect_json_contains(json, "\"instructionCount\":2", "CMP section-capacity strict mode should stop before CMP executes");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000001h\",\"unsigned\":1}", "CMP section-capacity strict mode should preserve flags from STC");
    failures += expect_json_not_contains(json, "execution-complete", "CMP section-capacity strict mode should not complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x BYTE 1\n"
        "y BYTE 2\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    cmp WORD PTR [eax], 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS
    );
    failures += expect_json_contains(json, "\"ok\":true", "CMP declared-object warning mode should continue");
    failures += expect_json_contains(json, "object-bounds-warning", "CMP declared-object warning mode should emit object-bounds diagnostic");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "CMP declared-object warning mode should complete");

    json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "x BYTE 1\n"
        "y BYTE 2\n"
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov eax, OFFSET x\n"
        "    cmp WORD PTR [eax], 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    failures += expect_json_contains(json, "\"ok\":false", "CMP declared-object strict mode should stop");
    failures += expect_json_contains(json, "object-bounds-violation", "CMP declared-object strict mode should emit object-bounds diagnostic");
    failures += expect_json_contains(json, "\"instructionCount\":2", "CMP declared-object strict mode should stop before CMP executes");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000001h\",\"unsigned\":1}", "CMP declared-object strict mode should preserve flags from STC");

    return failures;
}

/// Verifies Phase 63 CMP invalid Level 1 memory reads stop before flags change.
///
/// @return Number of failures.
static int test_phase63_cmp_invalid_level1_read_preserves_flags(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov eax, 0\n"
        "    cmp DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "CMP invalid Level 1 read should fail");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "CMP invalid Level 1 read should report invalid-address");
    failures += expect_json_contains(json, "\"instructionCount\":2", "CMP invalid Level 1 read should stop before CMP executes");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000001h\",\"unsigned\":1}", "CMP invalid Level 1 read should preserve flags from STC");
    failures += expect_json_not_contains(json, "execution-complete", "CMP invalid Level 1 read should not complete");

    return failures;
}

/// Verifies Phase 59 instruction-limit execution accounting and diagnostics.
///
/// @return Number of failures.
static int test_phase59_instruction_limit_source_run(void) {
    char limit_copy[TEST_JSON_COPY_CAPACITY];
    char label_copy[TEST_JSON_COPY_CAPACITY];
    char memory_copy[TEST_JSON_COPY_CAPACITY];
    const char *invalid_json = NULL;
    int failures = 0;

    copy_source_run_json(
        limit_copy,
        sizeof(limit_copy),
        masm32_sim_wasm_run_source_json_with_instruction_limit(
            ".code\n"
            "main PROC\n"
            "    mov eax, 1\n"
            "    mov ebx, 2\n"
            "    mov ecx, 3\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            2U
        )
    );

    failures += expect_json_contains(limit_copy, "\"ok\":false", "Low instruction limit should fail the run");
    failures += expect_json_contains(limit_copy, "\"status\":\"execution-error\"", "Instruction-limit failure should use execution-error status");
    failures += expect_json_contains(limit_copy, "\"instructionLimit\":2", "Instruction-limit JSON should include configured limit");
    failures += expect_json_contains(limit_copy, "\"executedInstructionCount\":2", "Instruction-limit JSON should include executed count");
    failures += expect_json_contains(limit_copy, "\"attemptedNextInstructionIndex\":2", "Instruction-limit JSON should include blocked next instruction index");
    failures += expect_json_contains(limit_copy, "\"currentInstructionIndex\":1", "Instruction-limit JSON should include last completed instruction index");
    failures += expect_json_contains(limit_copy, "instruction-limit-exceeded", "Instruction-limit failure should emit stable diagnostic code");
    failures += expect_json_contains(limit_copy, "\"line\":5", "Instruction-limit diagnostic should point to the blocked instruction line");
    failures += expect_json_contains(limit_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1", "First instruction should remain committed");
    failures += expect_json_contains(limit_copy, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2", "Second instruction should remain committed");
    failures += expect_json_contains(limit_copy, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0", "Blocked third instruction should not mutate ECX");
    failures += expect_json_not_contains(limit_copy, "execution-complete", "Instruction-limit failure must not emit execution-complete");

    copy_source_run_json(
        label_copy,
        sizeof(label_copy),
        masm32_sim_wasm_run_source_json_with_instruction_limit(
            ".code\n"
            "main PROC\n"
            "start:\n"
            "    mov eax, 1\n"
            "next:\n"
            "    mov ebx, 2\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            1U
        )
    );

    failures += expect_json_contains(label_copy, "\"executedInstructionCount\":1", "Labels should not count as executed instructions");
    failures += expect_json_contains(label_copy, "\"attemptedNextInstructionIndex\":1", "Label fixture should block the second emitted instruction");
    failures += expect_json_contains(label_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1", "Instruction before the label-count limit should commit");
    failures += expect_json_contains(label_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0", "Instruction after the label-count limit should not execute");

    copy_source_run_json(
        memory_copy,
        sizeof(memory_copy),
        masm32_sim_wasm_run_source_json_with_instruction_limit(
            ".data\n"
            "value DWORD 0\n"
            ".code\n"
            "main PROC\n"
            "    mov value, 1\n"
            "    mov value, 2\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            1U
        )
    );

    failures += expect_json_contains(memory_copy, "\"newHex\":\"00000001h\"", "First memory write should be recorded before the limit");
    failures += expect_json_not_contains(memory_copy, "00000002h", "Blocked memory write must not create a memory-change row");

    invalid_json = masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        0U
    );
    failures += expect_json_contains(invalid_json, "\"ok\":false", "Zero instructionLimit should be rejected");
    failures += expect_json_contains(invalid_json, "invalid-instruction-limit-setting", "Zero instructionLimit should emit a structured setting diagnostic");
    failures += expect_json_contains(invalid_json, "instructionLimit", "Invalid instructionLimit response should identify the setting");
    failures += expect_json_contains(invalid_json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Invalid instructionLimit response should include current output-contract metadata");

    return failures;
}

/// Verifies Phase 57F zero mode remains unchanged even with a seed.
///
/// @return Number of failures.
static int test_phase57f_zero_mode_seed_does_not_randomize(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        4294967295U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Zero startup mode with a seed should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Zero startup mode should keep EAX zero");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Zero startup mode should keep EFLAGS zero");
    failures += expect_json_not_contains(json, "startup-state-notice", "Startup notice should remain independently suppressible");

    return failures;
}

/// Verifies Phase 57F seeded startup is deterministic for the same seed.
///
/// @return Number of failures.
static int test_phase57f_seeded_startup_is_deterministic(void) {
    char first_copy[4096];
    char second_copy[4096];
    int failures = 0;

    copy_source_run_json(first_copy, sizeof(first_copy), masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        123456789U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));
    copy_source_run_json(second_copy, sizeof(second_copy), masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        123456789U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));

    if (strcmp(first_copy, second_copy) != 0) {
        failures += record_failure("same Phase 57F seed should produce identical source-run JSON for a source that does not mutate startup state");
    }
    failures += expect_json_contains(first_copy, "\"ok\":true", "Seeded startup should execute successfully");
    failures += expect_json_not_contains(first_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded startup should expose a nonzero EAX for this fixture seed");
    failures += expect_json_contains(first_copy, "\"EIP\":{\"hex\":\"00401000h\",\"unsigned\":4198400}", "Seeded startup should display base pseudo-EIP");

    return failures;
}

/// Verifies different Phase 57F seeds affect the startup state.
///
/// @return Number of failures.
static int test_phase57f_different_seeds_change_startup_state(void) {
    char first_copy[4096];
    char second_copy[4096];

    copy_source_run_json(first_copy, sizeof(first_copy), masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        1U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));
    copy_source_run_json(second_copy, sizeof(second_copy), masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        2U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));

    if (strcmp(first_copy, second_copy) == 0) {
        return record_failure("different Phase 57F seeds should produce different source-run JSON for a source that does not mutate startup state");
    }

    return 0;
}

/// Verifies Phase 57F seeded startup emits a mode-accurate notice.
///
/// @return Number of failures.
static int test_phase57f_seeded_startup_notice_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        0U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"kind\":\"simulator-notice\"", "Seeded startup should emit a Simulator Messages notice when enabled");
    failures += expect_json_contains(json, "\"code\":\"startup-state-notice\"", "Seeded startup notice should use the startup diagnostic code");
    failures += expect_json_contains(json, TEST_SEEDED_STARTUP_NOTICE_TEXT, "Seeded startup notice should use exact Phase 57F wording");
    failures += expect_json_not_contains(json, "Program Console", "Seeded startup notice should not write Program Console output");

    return failures;
}

/// Verifies Phase 57F rejects invalid startup register/flag modes.
///
/// @return Number of failures.
static int test_phase57f_invalid_startup_register_flag_mode(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        (Masm32SimWasmStartupRegisterFlagMode)99,
        0U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Invalid startup register/flag mode should fail");
    failures += expect_json_contains(json, "\"status\":\"invalid-argument\"", "Invalid startup register/flag mode should report invalid argument");
    failures += expect_json_contains(json, "\"code\":\"invalid-startup-setting\"", "Invalid startup register/flag mode should use startup-setting diagnostic code");
    failures += expect_json_contains(json, "startup_register_flag_mode", "Invalid startup register/flag mode should name the setting");
    failures += expect_json_contains(json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Invalid startup register/flag mode response should include current output-contract metadata");
    failures += expect_json_not_contains(json, "execution-complete", "Invalid startup register/flag mode should not complete execution");

    return failures;
}

/// Verifies Phase 57F startup randomization does not randomize memory contents.
///
/// @return Number of failures.
static int test_phase57f_seeded_startup_preserves_memory_and_uninitialized_origin(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_register_flag_mode(
        ".DATA?\n"
        "x DWORD ?\n"
        ".data\n"
        "y DWORD 5\n"
        ".CONST\n"
        "c DWORD 9\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    mov ebx, y\n"
        "    mov ecx, c\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        123U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Seeded startup memory preservation fixture should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded startup should leave uninitialized storage visible bytes zero-filled");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "Seeded startup should preserve initialized .data bytes");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000009h\",\"unsigned\":9}", "Seeded startup should preserve initialized .CONST bytes");
    failures += expect_json_contains(json, "uninitialized-read", "Seeded startup should preserve uninitialized-origin diagnostics");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Seeded startup alone should not create memory-change rows");

    return failures;
}


/// Verifies Phase 57G zero uninitialized-storage mode preserves visible zero bytes.
///
/// @return Number of failures.
static int test_phase57g_zero_uninitialized_storage_mode_preserves_zero_bytes(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".DATA?\n"
        "q DWORD ?\n"
        ".data\n"
        "x DWORD ?\n"
        "arr BYTE 4 DUP(?)\n"
        "y DWORD 5\n"
        ".CONST\n"
        "c DWORD 9\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, q\n"
        "    mov ebx, x\n"
        "    mov ecx, DWORD PTR arr\n"
        "    mov edx, y\n"
        "    mov esi, c\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        4294967295U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Zero uninitialized-storage mode should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Zero mode should keep .DATA? visible bytes zero-filled");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Zero mode should keep scalar ? visible bytes zero-filled");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Zero mode should keep DUP(?) visible bytes zero-filled");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "Zero mode should preserve initialized .data bytes");
    failures += expect_json_contains(json, "\"ESI\":{\"hex\":\"00000009h\",\"unsigned\":9}", "Zero mode should preserve initialized .CONST bytes");
    failures += expect_json_contains(json, "uninitialized-read", "Zero mode should preserve uninitialized-origin diagnostics");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Startup initialization should not create memory-change rows");

    return failures;
}

/// Verifies Phase 57G seeded mode randomizes only uninitialized-origin visible bytes.
///
/// @return Number of failures.
static int test_phase57g_seeded_uninitialized_storage_randomizes_only_uninitialized_bytes(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".DATA?\n"
        "q DWORD ?\n"
        ".data\n"
        "x DWORD ?\n"
        "arr BYTE 4 DUP(?)\n"
        "y DWORD 5\n"
        ".CONST\n"
        "c DWORD 9\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, q\n"
        "    mov ebx, x\n"
        "    mov ecx, DWORD PTR arr\n"
        "    mov edx, y\n"
        "    mov esi, c\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        123U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Seeded uninitialized-storage mode should execute");
    failures += expect_json_not_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded mode should change .DATA? visible bytes for this fixture seed");
    failures += expect_json_not_contains(json, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded mode should change DUP(?) visible bytes for this fixture seed");
    failures += expect_json_contains(json, "\"EDX\":{\"hex\":\"00000005h\",\"unsigned\":5}", "Seeded mode should preserve initialized .data bytes");
    failures += expect_json_contains(json, "\"ESI\":{\"hex\":\"00000009h\",\"unsigned\":9}", "Seeded mode should preserve initialized .CONST bytes");
    failures += expect_json_contains(json, "uninitialized-read", "Seeded mode should preserve uninitialized-origin diagnostics");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Seeded startup should not create memory-change rows");

    return failures;
}

/// Verifies Phase 57G seeded uninitialized-storage mode is deterministic for the same seed.
///
/// @return Number of failures.
static int test_phase57g_seeded_uninitialized_storage_is_deterministic(void) {
    char first_copy[4096];
    char second_copy[4096];
    int failures = 0;
    const char *source =
        ".DATA?\n"
        "q DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, q\n"
        "    ret\n"
        "END main\n";

    copy_source_run_json(first_copy, sizeof(first_copy), masm32_sim_wasm_run_source_json_with_startup_modes(
        source,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        987654321U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));
    copy_source_run_json(second_copy, sizeof(second_copy), masm32_sim_wasm_run_source_json_with_startup_modes(
        source,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        987654321U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));

    if (strcmp(first_copy, second_copy) != 0) {
        failures += record_failure("same Phase 57G seed should produce identical source-run JSON for uninitialized storage");
    }
    failures += expect_json_contains(first_copy, "\"ok\":true", "Seeded uninitialized-storage deterministic fixture should execute");
    failures += expect_json_not_contains(first_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded uninitialized-storage deterministic fixture should expose nonzero bytes for this seed");

    return failures;
}

/// Verifies different Phase 57G seeds affect uninitialized-origin visible bytes.
///
/// @return Number of failures.
static int test_phase57g_different_seeds_change_uninitialized_storage(void) {
    char first_copy[4096];
    char second_copy[4096];
    const char *source =
        ".DATA?\n"
        "q DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, q\n"
        "    ret\n"
        "END main\n";

    copy_source_run_json(first_copy, sizeof(first_copy), masm32_sim_wasm_run_source_json_with_startup_modes(
        source,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        1U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));
    copy_source_run_json(second_copy, sizeof(second_copy), masm32_sim_wasm_run_source_json_with_startup_modes(
        source,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        2U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    ));

    if (strcmp(first_copy, second_copy) == 0) {
        return record_failure("different Phase 57G seeds should produce different source-run JSON for uninitialized storage");
    }

    return 0;
}

/// Verifies Phase 57G seeded mode preserves warnings for every current uninitialized storage form.
///
/// @return Number of failures.
static int test_phase57g_seeded_uninitialized_storage_preserves_each_origin_class(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".DATA?\n"
        "q DWORD ?\n"
        ".data\n"
        "x DWORD ?\n"
        "arr BYTE 4 DUP(?)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, q\n"
        "    mov ebx, x\n"
        "    mov ecx, DWORD PTR arr\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        123U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Seeded uninitialized-storage metadata fixture should execute");
    failures += expect_json_contains(json, "from q + 0; 4 of those bytes still originated from uninitialized storage", ".DATA? bytes should keep uninitialized-origin diagnostics after seeded visible-byte startup");
    failures += expect_json_contains(json, "from x + 0; 4 of those bytes still originated from uninitialized storage", "Scalar ? bytes should keep uninitialized-origin diagnostics after seeded visible-byte startup");
    failures += expect_json_contains(json, "from arr + 0; 4 of those bytes still originated from uninitialized storage", "DUP(?) bytes should keep uninitialized-origin diagnostics after seeded visible-byte startup");
    failures += expect_json_not_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", ".DATA? visible bytes should be seeded for this fixture seed");
    failures += expect_json_not_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Scalar ? visible bytes should be seeded for this fixture seed");
    failures += expect_json_not_contains(json, "\"ECX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "DUP(?) visible bytes should be seeded for this fixture seed");

    return failures;
}

/// Verifies Phase 57I accepts `.CONST ?` as read-only uninitialized-origin storage.
///
/// @return Number of failures.
static int test_phase57i_const_uninitialized_storage_acceptance_source_run(void) {
    char zero_copy[TEST_JSON_COPY_CAPACITY];
    char seeded_copy[TEST_JSON_COPY_CAPACITY];
    char direct_write_copy[TEST_JSON_COPY_CAPACITY];
    char computed_write_copy[TEST_JSON_COPY_CAPACITY];
    char strict_read_copy[TEST_JSON_COPY_CAPACITY];
    const char *zero_json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        ".CONST\n"
        "limit DWORD ?\n"
        "buf BYTE 4 DUP(?)\n"
        "ready DWORD 9\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, limit\n"
        "    mov ebx, DWORD PTR buf\n"
        "    mov ecx, ready\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    const char *seeded_json = NULL;
    const char *direct_write_json = NULL;
    const char *computed_write_json = NULL;
    const char *strict_read_json = NULL;
    copy_source_run_json(zero_copy, sizeof(zero_copy), zero_json);
    seeded_json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".CONST\n"
        "limit DWORD ?\n"
        "ready DWORD 9\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, limit\n"
        "    mov ebx, ready\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        123U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    copy_source_run_json(seeded_copy, sizeof(seeded_copy), seeded_json);
    direct_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov limit, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    copy_source_run_json(direct_write_copy, sizeof(direct_write_copy), direct_write_json);
    computed_write_json = masm32_sim_wasm_run_source_json(
        ".CONST\n"
        "limit DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    copy_source_run_json(computed_write_copy, sizeof(computed_write_copy), computed_write_json);
    strict_read_json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".CONST\n"
        "limit DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 1234h\n"
        "    mov ebx, limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    );
    copy_source_run_json(strict_read_copy, sizeof(strict_read_copy), strict_read_json);
    int failures = 0;

    failures += expect_json_contains(zero_copy, "\"ok\":true", "Phase 57I .CONST ? metadata fixture should execute");
    failures += expect_json_contains(zero_copy, "\"phaseSuffix\":\"\"", ".CONST ? fixture should report the current runtime phase suffix");
    failures += expect_json_contains(zero_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", ".CONST DWORD ? should read deterministic zero by default");
    failures += expect_json_contains(zero_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", ".CONST DUP(?) should read deterministic zero by default");
    failures += expect_json_contains(zero_copy, "\"ECX\":{\"hex\":\"00000009h\",\"unsigned\":9}", "Initialized .CONST should remain unchanged");
    failures += expect_json_contains(zero_copy, "from limit + 0; 4 of those bytes still originated from uninitialized storage", ".CONST ? read should emit existing uninitialized-read warning");
    failures += expect_json_contains(zero_copy, "from buf + 0; 4 of those bytes still originated from uninitialized storage", ".CONST DUP(?) read should emit existing uninitialized-read warning");
    failures += expect_json_contains(zero_copy, "\"symbol\":\"limit\",\"state\":\"tracked\",\"initializedByteCount\":0,\"uninitializedByteCount\":4,\"initializedMask\":\"0000\"", ".CONST ? should expose uninitialized-origin metadata in test-only JSON");
    failures += expect_json_contains(zero_copy, "\"symbol\":\"ready\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "Initialized .CONST should expose initialized metadata");
    failures += expect_json_contains(seeded_copy, "\"ok\":true", "Phase 57I seeded .CONST ? fixture should execute");
    failures += expect_json_not_contains(seeded_copy, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded uninitialized-storage mode should affect .CONST ? visible bytes");
    failures += expect_json_contains(seeded_copy, "\"EBX\":{\"hex\":\"00000009h\",\"unsigned\":9}", "Seeded mode should not affect initialized .CONST bytes");
    failures += expect_json_contains(seeded_copy, "from limit + 0; 4 of those bytes still originated from uninitialized storage", "Seeded .CONST ? read should preserve uninitialized-origin diagnostics");
    failures += expect_json_contains(direct_write_copy, "\"ok\":false", "Direct write to .CONST ? should fail");
    failures += expect_json_contains(direct_write_copy, "\"status\":\"parse-error\"", "Direct write to .CONST ? should fail during parsing when static detection applies");
    failures += expect_json_contains(direct_write_copy, "\"code\":\"const-write\"", "Direct write to .CONST ? should use existing const-write diagnostic");
    failures += expect_json_contains(direct_write_copy, "\"code\":\"const-uninitialized-storage\"", "Phase 57J direct write should also emit the declaration diagnostic");
    failures += expect_json_contains(computed_write_copy, "\"ok\":false", "Computed write to .CONST ? should fail");
    failures += expect_json_contains(computed_write_copy, "\"status\":\"execution-error\"", "Computed write to .CONST ? should fail at runtime");
    failures += expect_json_contains(computed_write_copy, "\"code\":\"const-uninitialized-storage\"", "Computed write to .CONST ? should preserve the Phase 57J declaration diagnostic");
    failures += expect_json_contains(computed_write_copy, "\"code\":\"permission-denied\"", "Computed write to .CONST ? should use existing permission diagnostic");
    failures += expect_json_contains(computed_write_copy, "\"memoryChanges\":[]", "Failed computed write to .CONST ? should not commit memory changes");
    failures += expect_json_not_contains(computed_write_copy, "uninitialized-read", "Computed write to .CONST ? should not be reclassified as an uninitialized read");
    failures += expect_json_contains(strict_read_copy, "\"ok\":false", "Strict .CONST ? read should stop execution");
    failures += expect_json_contains(strict_read_copy, "\"code\":\"uninitialized-read\"", "Strict .CONST ? read should use existing uninitialized-read diagnostic");
    failures += expect_json_contains(strict_read_copy, "\"EAX\":{\"hex\":\"00001234h\",\"unsigned\":4660}", "Strict .CONST ? read should preserve prior completed instruction state");
    failures += expect_json_contains(strict_read_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Strict .CONST ? read should stop before mutating consumer register");
    failures += expect_json_not_contains(strict_read_copy, "execution-complete", "Strict .CONST ? read should not emit execution-complete");

    return failures;
}

/// Verifies Phase 57J `.CONST ?` declaration diagnostic policies.
///
/// @return Number of failures.
static int test_phase57j_const_uninitialized_storage_policy_source_run(void) {
    const char *source =
        ".CONST\n"
        "limit DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, limit\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    const char *multi_source =
        ".CONST\n"
        "limit DWORD ?\n"
        "buf BYTE 4 DUP(?)\n"
        "ready DWORD 9\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, ready\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    char warn_copy[TEST_JSON_COPY_CAPACITY];
    char off_copy[TEST_JSON_COPY_CAPACITY];
    char error_copy[TEST_JSON_COPY_CAPACITY];
    char read_off_copy[TEST_JSON_COPY_CAPACITY];
    char multi_copy[TEST_JSON_COPY_CAPACITY];
    char invalid_policy_copy[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    copy_source_run_json(warn_copy, sizeof(warn_copy), masm32_sim_wasm_run_source_json(source));
    copy_source_run_json(
        off_copy,
        sizeof(off_copy),
        masm32_sim_wasm_run_source_json_with_const_uninitialized_storage_policy(source, VM_DIAGNOSTIC_POLICY_VALUE_OFF)
    );
    copy_source_run_json(
        error_copy,
        sizeof(error_copy),
        masm32_sim_wasm_run_source_json_with_const_uninitialized_storage_policy(source, VM_DIAGNOSTIC_POLICY_VALUE_ERROR)
    );
    copy_source_run_json(
        read_off_copy,
        sizeof(read_off_copy),
        masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY)
    );
    copy_source_run_json(multi_copy, sizeof(multi_copy), masm32_sim_wasm_run_source_json(multi_source));
    copy_source_run_json(
        invalid_policy_copy,
        sizeof(invalid_policy_copy),
        masm32_sim_wasm_run_source_json_with_const_uninitialized_storage_policy(source, VM_DIAGNOSTIC_POLICY_VALUE_COUNT)
    );

    failures += expect_json_contains(warn_copy, "\"ok\":true", "Default const-uninitialized-storage warning policy should continue execution");
    failures += expect_json_contains(warn_copy, "\"kind\":\"simulator-warning\",\"code\":\"const-uninitialized-storage\"", "Default policy should emit a simulator warning declaration diagnostic");
    failures += expect_json_contains(warn_copy, ".CONST declaration `limit` reserves uninitialized read-only storage", "Declaration diagnostic should name the .CONST symbol");
    failures += expect_json_contains(warn_copy, "\"line\":2,\"column\":1", "Declaration diagnostic should point at the .CONST declarator source location");
    failures += expect_json_contains(warn_copy, "\"spanLength\":5", "Declaration diagnostic should preserve declarator span length");
    failures += expect_json_contains(warn_copy, "\"code\":\"uninitialized-read\"", "Default read policy should still warn separately when .CONST ? is read");
    failures += expect_json_contains(warn_copy, "\"code\":\"execution-complete\"", "Warning policy should still emit execution-complete when execution succeeds");
    failures += expect_json_contains_once(warn_copy, "\"code\":\"const-uninitialized-storage\"", "Single .CONST ? declaration should produce one declaration warning");

    failures += expect_json_contains(off_copy, "\"ok\":true", "const-uninitialized-storage off policy should continue execution");
    failures += expect_json_not_contains(off_copy, "const-uninitialized-storage", "Off policy should suppress only the declaration diagnostic");
    failures += expect_json_contains(off_copy, "\"code\":\"uninitialized-read\"", "Off policy should not suppress read-time uninitialized-read diagnostics");
    failures += expect_json_contains(off_copy, "\"code\":\"execution-complete\"", "Off policy run should still complete");

    failures += expect_json_contains(error_copy, "\"ok\":false", "Error policy should reject .CONST ? declarations");
    failures += expect_json_contains(error_copy, "\"status\":\"parse-error\"", "Error policy should reject before runtime execution");
    failures += expect_json_contains(error_copy, "\"kind\":\"assembly-error\",\"code\":\"const-uninitialized-storage\"", "Error policy should emit an assembly error declaration diagnostic");
    failures += expect_json_not_contains(error_copy, "uninitialized-read", "Error policy should stop before read-time diagnostics");
    failures += expect_json_not_contains(error_copy, "execution-complete", "Error policy should not emit execution-complete");

    failures += expect_json_contains(read_off_copy, "\"ok\":true", "Region-only read policy should still execute .CONST ? reads");
    failures += expect_json_contains(read_off_copy, "\"code\":\"const-uninitialized-storage\"", "Region-only read policy should not suppress declaration diagnostics");
    failures += expect_json_not_contains(read_off_copy, "uninitialized-read", "Region-only read policy should suppress read-time uninitialized-read diagnostics");

    failures += expect_json_contains(multi_copy, "\"ok\":true", "Multiple .CONST declaration warning fixture should execute");
    if (count_json_fragment_occurrences(multi_copy, "\"code\":\"const-uninitialized-storage\"") != 2U) {
        failures += record_failure("Two .CONST declarations with uninitialized storage should emit exactly two declaration diagnostics");
    }
    failures += expect_json_not_contains(multi_copy, "from ready + 0", "Initialized .CONST declaration should not be treated as uninitialized-origin storage");

    failures += expect_json_contains(invalid_policy_copy, "\"ok\":false", "Invalid const-uninitialized-storage policy should fail");
    failures += expect_json_contains(invalid_policy_copy, "\"status\":\"invalid-argument\"", "Invalid const-uninitialized-storage policy should return invalid-argument status");

    return failures;
}

/// Verifies Phase 57G startup axes remain orthogonal.
///
/// @return Number of failures.
static int test_phase57g_startup_axes_are_orthogonal(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO,
        123U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Orthogonal startup-axis fixture should execute");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded register startup must not imply seeded uninitialized-storage bytes");
    failures += expect_json_not_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Seeded register startup should still affect untouched EBX for this seed");

    return failures;
}

/// Verifies Phase 57G combined seeded axes affect both registers and uninitialized storage.
///
/// @return Number of failures.
static int test_phase57g_combined_seeded_startup_axes_affect_registers_and_storage(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        123U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Combined seeded startup fixture should execute");
    failures += expect_json_not_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Combined seeded mode should seed the uninitialized storage value loaded into EAX");
    failures += expect_json_not_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Combined seeded mode should seed untouched EBX through the register/flag axis");
    failures += expect_json_contains(json, "uninitialized-read", "Combined seeded mode should preserve uninitialized-read diagnostics");

    return failures;
}

/// Verifies Phase 57G seeded storage preserves strict uninitialized-read stops.
///
/// @return Number of failures.
static int test_phase57g_seeded_uninitialized_storage_strict_read_stops(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_ui_and_startup_storage_settings(
        ".DATA?\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, x\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_STRICT,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        123U
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Strict uninitialized-read mode should stop seeded storage read");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Strict uninitialized-read mode should produce source-run execution-error status");
    failures += expect_json_contains(json, "uninitialized-read", "Strict seeded storage read should preserve uninitialized-origin diagnostic");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Strict read should stop before mutating EAX");
    failures += expect_json_not_contains(json, "execution-complete", "Strict read should not emit execution-complete");

    return failures;
}

/// Verifies Phase 57G seeded storage emits a mode-accurate startup notice.
///
/// @return Number of failures.
static int test_phase57g_seeded_uninitialized_storage_notice_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        0U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"kind\":\"simulator-notice\"", "Seeded uninitialized storage should emit a Simulator Messages notice when enabled");
    failures += expect_json_contains(json, "\"code\":\"startup-state-notice\"", "Seeded uninitialized storage notice should use the startup diagnostic code");
    failures += expect_json_contains(json, TEST_SEEDED_UNINITIALIZED_STORAGE_NOTICE_TEXT, "Seeded uninitialized storage notice should use exact Phase 57G wording");
    failures += expect_json_not_contains(json, "Program Console", "Seeded uninitialized storage notice should not write Program Console output");

    return failures;
}

/// Verifies Phase 57G notice wording when both seeded startup axes are enabled.
///
/// @return Number of failures.
static int test_phase57g_combined_seeded_startup_notice_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM,
        MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM,
        0U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON
    );
    int failures = 0;

    failures += expect_json_contains(json, TEST_SEEDED_REGISTER_AND_UNINITIALIZED_STORAGE_NOTICE_TEXT, "Combined seeded startup notice should use exact wording");
    failures += expect_json_contains_once(json, "startup-state-notice", "Combined seeded startup should emit one startup notice");

    return failures;
}

/// Verifies Phase 57G rejects invalid uninitialized-storage visible-byte modes.
///
/// @return Number of failures.
static int test_phase57g_invalid_uninitialized_storage_mode(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_startup_modes(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO,
        (Masm32SimWasmUninitializedStorageVisibleByteMode)99,
        0U,
        MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Invalid uninitialized-storage visible-byte mode should fail");
    failures += expect_json_contains(json, "\"status\":\"invalid-argument\"", "Invalid uninitialized-storage visible-byte mode should report invalid argument");
    failures += expect_json_contains(json, "\"code\":\"invalid-startup-setting\"", "Invalid uninitialized-storage visible-byte mode should use startup-setting diagnostic code");
    failures += expect_json_contains(json, "uninitialized_storage_visible_byte_mode", "Invalid uninitialized-storage visible-byte mode should name the setting");
    failures += expect_json_not_contains(json, "execution-complete", "Invalid uninitialized-storage visible-byte mode should not complete execution");

    return failures;
}

/// Verifies invalid Phase 53E C API setting values are rejected.
///
/// @return Number of failures.
static int test_phase53e_invalid_ui_settings_return_invalid_argument(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_ui_settings(
        ".code\n"
        "main PROC\n"
        "    ret\n"
        "END main\n",
        (Masm32SimWasmMemoryRangeSetting)99,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
        MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Invalid Phase 53E memory setting should fail");
    failures += expect_json_contains(json, "\"status\":\"invalid-argument\"", "Invalid Phase 53E memory setting should return invalid argument status");
    failures += expect_json_contains(json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Invalid Phase 53E setting response should include current output-contract metadata");

    return failures;
}

/// Verifies Phase 50B undefined-flag-use warning behavior for an existing CF consumer.
///
/// @return Number of failures.
static int test_phase50b_undefined_flag_use_warn_policy_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 50B source-run response should report current numeric runtime metadata");
    failures += expect_json_contains(json, "\"ok\":true", "Undefined flag-use warn policy should allow execution to continue");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Warn policy should execute the existing ADC consumer");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "Warn policy should use deterministic preserved CF for ADC");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "Producer warning should remain present");
    failures += expect_json_contains(json, "\"code\":\"undefined-flag-use\"", "Warn policy should add undefined-flag-use diagnostic");
    failures += expect_json_contains(json, "ADC reads CF, but CF is architecturally undefined from SHL at line 5", "Undefined flag-use message should name consumer, flag, producer, and producer line");
    failures += expect_json_contains(json, "\"consumedFlags\":[\"CF\"]", "Undefined flag-use JSON should list consumed invalid flags");
    failures += expect_json_contains(json, "\"producerMnemonic\":\"SHL\"", "Undefined flag-use JSON should include producer mnemonic");
    failures += expect_json_contains(json, "\"producerCode\":\"undefined-shift-flag\"", "Undefined flag-use JSON should include producer diagnostic code");
    failures += expect_json_contains(json, "\"producerLine\":5", "Undefined flag-use JSON should include producer line");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Warn policy should still report execution completion");

    return failures;
}

/// Verifies Phase 50B undefined-flag-use error behavior stops before mutation.
///
/// @return Number of failures.
static int test_phase50b_undefined_flag_use_error_policy_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Undefined flag-use error policy should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Undefined flag-use error should be an execution error");
    failures += expect_json_contains(json, "\"instructionCount\":4", "Error policy should stop before executing ADC");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Error policy should not mutate the ADC destination");
    failures += expect_json_contains(json, "\"code\":\"undefined-flag-use\"", "Error policy should emit undefined-flag-use diagnostic");
    failures += expect_json_contains(json, "Execution stopped before using the undefined flag", "Error diagnostic should explain that the consumer did not execute");
    failures += expect_json_contains(json, "\"consumedFlags\":[\"CF\"]", "Error diagnostic should list consumed invalid flags");
    failures += expect_json_not_contains(json, "execution-complete", "Undefined flag-use error should not report execution completion");

    return failures;
}


/// Verifies Phase 50B integration with SBB as an existing CF consumer.
///
/// @return Number of failures.
static int test_phase50b_undefined_flag_use_sbb_warn_policy_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 10\n"
        "    sbb ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "SBB undefined flag-use warn policy should allow execution to continue");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Warn policy should execute the existing SBB consumer");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000009h\",\"unsigned\":9}", "Warn policy should use deterministic preserved CF for SBB");
    failures += expect_json_contains(json, "\"code\":\"undefined-flag-use\"", "SBB warn policy should add undefined-flag-use diagnostic");
    failures += expect_json_contains(json, "SBB reads CF, but CF is architecturally undefined from SHL at line 5", "SBB diagnostic should identify CF consumption");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "SBB warn policy should still report execution completion");

    return failures;
}

/// Verifies Phase 50B explicit off behavior preserves prior deterministic execution.
///
/// @return Number of failures.
static int test_phase50b_undefined_flag_use_explicit_off_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    mov ebx, 0\n"
        "    adc ebx, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Explicit off policy should execute successfully");
    failures += expect_json_contains(json, "\"instructionCount\":6", "Explicit off policy should execute the ADC consumer");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "Explicit off policy should continue using deterministic CF fallback");
    failures += expect_json_contains(json, "\"code\":\"undefined-shift-flag\"", "Explicit off policy should preserve existing producer warning");
    failures += expect_json_not_contains(json, "undefined-flag-use", "Explicit off policy should suppress consumer diagnostics");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Explicit off policy should still complete");

    return failures;
}

/// Verifies Phase 50B integration with CMC as an existing CF consumer.
///
/// @return Number of failures.
static int test_phase50b_undefined_flag_use_cmc_error_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    cmc\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "CMC invalid flag use should fail in error policy");
    failures += expect_json_contains(json, "\"instructionCount\":3", "CMC error policy should stop before toggling CF");
    failures += expect_json_contains(json, "CMC reads CF, but CF is architecturally undefined from SHL at line 5", "CMC diagnostic should identify CF consumption");
    failures += expect_json_contains(json, "\"EFLAGS\":{\"hex\":\"00000041h\",\"unsigned\":65}", "CMC error should preserve pre-consumer EFLAGS bits");
    failures += expect_json_not_contains(json, "execution-complete", "CMC undefined flag-use error should not complete");

    return failures;
}


/// Verifies Phase 50B error policy stops before a memory-destination consumer mutates memory.
///
/// @return Number of failures.
static int test_phase50b_undefined_flag_use_memory_destination_error_source_run(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(
        ".data\n"
        "value DWORD 5\n"
        ".code\n"
        "main PROC\n"
        "    stc\n"
        "    mov al, 1\n"
        "    shl al, 8\n"
        "    adc value, 0\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "Memory-destination ADC invalid flag use should fail in error policy");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Memory-destination ADC error should stop before executing the consumer");
    failures += expect_json_contains(json, "ADC reads CF, but CF is architecturally undefined from SHL at line 7", "Memory-destination ADC diagnostic should identify the consumer and producer");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Undefined flag-use error should not create memory-change rows for the aborted memory destination");
    failures += expect_json_not_contains(json, "execution-complete", "Memory-destination undefined flag-use error should not complete");

    return failures;
}


/// Runs one named Phase 51 source-run success smoke fixture and reports it to stdout.
///
/// @param name Human-readable Phase 51 fixture name.
/// @param source MASM-like source text to execute through the default source-run API.
/// @param instruction_count_fragment Expected JSON instruction-count fragment.
/// @param register_fragment Expected JSON register fragment proving the fixture result.
/// @return Number of failures.
static int run_phase51_source_run_success_smoke(
    const char *name,
    const char *source,
    const char *instruction_count_fragment,
    const char *register_fragment
) {
    const char *json = NULL;
    int failures = 0;

    printf("PHASE 51 source-run program exercised: %s\n", name != NULL ? name : "(unnamed)");
    json = masm32_sim_wasm_run_source_json(source);
    failures += expect_json_contains(json, "\"ok\":true", "Phase 51 source-run smoke fixture should execute successfully");
    failures += expect_json_contains(json, "\"status\":\"ok\"", "Phase 51 source-run smoke fixture should report ok status");
    (void)instruction_count_fragment;
    failures += expect_json_contains(json, register_fragment, "Phase 51 source-run smoke fixture should report the expected final register value");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 51 source-run smoke fixture should include execution-complete");

    return failures;
}

/// Verifies Phase 51 fixed and automatic layout smoke behavior without asserting absolute addresses.
///
/// @return Number of failures.
static int test_phase51_fixed_and_automatic_layout_smoke_harness(void) {
    const char *source =
        ".DATA?\n"
        "scratch DWORD ?\n"
        ".data\n"
        "value DWORD 3\n"
        ".CONST\n"
        "mask DWORD 0Fh\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, value\n"
        "    inc eax\n"
        "    and eax, mask\n"
        "    shl eax, 1\n"
        "    mov scratch, eax\n"
        "    mov ebx, scratch\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n";
    VmLayoutPolicy automatic_policy = vm_layout_default_policy();
    const char *fixed_json = masm32_sim_wasm_run_source_json(source);
    const char *automatic_json = NULL;
    char fixed_copy[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    copy_source_run_json(fixed_copy, sizeof(fixed_copy), fixed_json);
    automatic_json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(source, &automatic_policy);

    printf("PHASE 51 source-run program exercised: phase51-layout-fixed-automatic-equivalence\n");
    failures += expect_json_contains(fixed_copy, "\"ok\":true", "Phase 51 fixed-layout smoke source should execute");
    failures += expect_json_contains(automatic_json, "\"ok\":true", "Phase 51 automatic-layout smoke source should execute");
    failures += expect_json_contains(fixed_copy, "\"instructionCount\":7", "Phase 51 fixed-layout smoke should execute six instructions");
    failures += expect_json_contains(automatic_json, "\"instructionCount\":7", "Phase 51 automatic-layout smoke should execute six instructions");
    failures += expect_json_contains(fixed_copy, "\"EAX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "Phase 51 fixed-layout smoke should leave EAX = 8");
    failures += expect_json_contains(automatic_json, "\"EAX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "Phase 51 automatic-layout smoke should leave EAX = 8");
    failures += expect_json_contains(fixed_copy, "\"EBX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "Phase 51 fixed-layout smoke should leave EBX = 8");
    failures += expect_json_contains(automatic_json, "\"EBX\":{\"hex\":\"00000008h\",\"unsigned\":8}", "Phase 51 automatic-layout smoke should leave EBX = 8");
    failures += expect_json_not_contains(fixed_copy, "resource-limit-exceeded", "Phase 51 fixed-layout smoke should not hit resource limits");
    failures += expect_json_not_contains(automatic_json, "resource-limit-exceeded", "Phase 51 automatic-layout smoke should not hit resource limits");

    return failures;
}


/// Verifies Phase 57L `.code` memory reads, writes, partial overlaps, and RMW failures.
///
/// @return Number of failures.
static int test_phase57l_code_memory_access_diagnostics_source_run(void) {
    int failures = 0;
    char byte_read_json[TEST_JSON_COPY_CAPACITY];
    char word_read_json[TEST_JSON_COPY_CAPACITY];
    char dword_read_json[TEST_JSON_COPY_CAPACITY];
    char before_read_json[TEST_JSON_COPY_CAPACITY];
    char inside_out_read_json[TEST_JSON_COPY_CAPACITY];
    char byte_write_json[TEST_JSON_COPY_CAPACITY];
    char word_write_json[TEST_JSON_COPY_CAPACITY];
    char dword_write_json[TEST_JSON_COPY_CAPACITY];
    char before_write_json[TEST_JSON_COPY_CAPACITY];
    char inside_out_write_json[TEST_JSON_COPY_CAPACITY];
    char rmw_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(
        byte_read_json,
        sizeof(byte_read_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 00400000h\n"
            "    mov al, BYTE PTR [ebx]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        word_read_json,
        sizeof(word_read_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 00400000h\n"
            "    mov ax, WORD PTR [ebx]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        dword_read_json,
        sizeof(dword_read_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 00400000h\n"
            "    mov eax, DWORD PTR [ebx]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        before_read_json,
        sizeof(before_read_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 003FFFFFh\n"
            "    mov ax, WORD PTR [ebx]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        inside_out_read_json,
        sizeof(inside_out_read_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 004FFFFFh\n"
            "    mov ax, WORD PTR [ebx]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        byte_write_json,
        sizeof(byte_write_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 00400000h\n"
            "    mov BYTE PTR [ebx], 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        word_write_json,
        sizeof(word_write_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 00400000h\n"
            "    mov WORD PTR [ebx], 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        dword_write_json,
        sizeof(dword_write_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 00400000h\n"
            "    mov DWORD PTR [ebx], 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        before_write_json,
        sizeof(before_write_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 003FFFFFh\n"
            "    mov WORD PTR [ebx], 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        inside_out_write_json,
        sizeof(inside_out_write_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 004FFFFFh\n"
            "    mov WORD PTR [ebx], 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        rmw_json,
        sizeof(rmw_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov ebx, 00400000h\n"
            "    inc DWORD PTR [ebx]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );

    printf("PHASE 57L source-run program exercised: phase57l-code-memory-access-diagnostics\n");

    failures += expect_json_contains(byte_read_json, "\"phaseSuffix\":\"\"", ".code read should report the current runtime phase suffix");
    failures += expect_json_contains(byte_read_json, "\"ok\":false", "Phase 57L BYTE .code read should fail");
    failures += expect_json_contains(byte_read_json, "\"instructionCount\":1", "Phase 57L BYTE .code read should stop after setup instruction");
    failures += expect_json_contains(byte_read_json, "\"code\":\"unsupported-code-memory-access\"", "Phase 57L BYTE .code read should use unsupported-code-memory-access");
    failures += expect_json_contains(byte_read_json, "Memory read at 00400000h for 1 byte overlaps .CODE/_TEXT", "Phase 57L BYTE .code read message should identify address and width");
    failures += expect_json_contains(byte_read_json, "simulator does not expose .CODE/_TEXT as an accessible memory region", "Phase 57L .code read message should explain no-access policy");
    failures += expect_json_contains(byte_read_json, "Program stopped before access", "Phase 57L .code read message should say read was not consumed");
    failures += expect_json_contains(byte_read_json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 57L failed .code read should not mutate EAX");
    failures += expect_json_contains(byte_read_json, "\"memoryChanges\":[]", "Phase 57L failed .code read should not record memory changes");
    failures += expect_json_not_contains(byte_read_json, "execution-complete", "Phase 57L failed .code read should not complete");

    failures += expect_json_contains(word_read_json, "Memory read at 00400000h for 2 bytes overlaps .CODE/_TEXT", "Phase 57L WORD .code read should be denied");
    failures += expect_json_contains(dword_read_json, "Memory read at 00400000h for 4 bytes overlaps .CODE/_TEXT", "Phase 57L DWORD .code read should be denied");

    failures += expect_json_contains(before_read_json, "\"code\":\"region-boundary-crossing\"", "Phase 57L read from before .code should use region-boundary-crossing");
    failures += expect_json_contains(before_read_json, "memory address range 003FFFFFh..00400000h crosses/overlaps a no-access memory region, .CODE/_TEXT, that starts at 00400000h", "Phase 57L read from before .code should describe .code overlap");
    failures += expect_json_contains(inside_out_read_json, "\"code\":\"region-boundary-crossing\"", "Phase 57L read from inside .code out should use region-boundary-crossing");
    failures += expect_json_contains(inside_out_read_json, "memory address range 004FFFFFh..00500000h crosses/overlaps a no-access memory region, .CODE/_TEXT, that starts at 00400000h", "Phase 57L read from inside .code out should describe .code overlap");

    failures += expect_json_contains(byte_write_json, "\"code\":\"unsupported-code-memory-access\"", "Phase 57L BYTE .code write should use unsupported-code-memory-access");
    failures += expect_json_contains(byte_write_json, "Memory write at 00400000h for 1 byte overlaps .CODE/_TEXT", "Phase 57L BYTE .code write message should identify address and width");
    failures += expect_json_contains(byte_write_json, "simulator does not expose .CODE/_TEXT as an accessible memory region", "Phase 57L .code write message should explain no-access policy");
    failures += expect_json_contains(byte_write_json, "Program stopped before access", "Phase 57L .code write message should say mutation did not happen");
    failures += expect_json_contains(byte_write_json, "\"memoryChanges\":[]", "Phase 57L failed .code write should not record memory changes");
    failures += expect_json_not_contains(byte_write_json, "execution-complete", "Phase 57L failed .code write should not complete");
    failures += expect_json_contains(word_write_json, "Memory write at 00400000h for 2 bytes overlaps .CODE/_TEXT", "Phase 57L WORD .code write should be denied");
    failures += expect_json_contains(dword_write_json, "Memory write at 00400000h for 4 bytes overlaps .CODE/_TEXT", "Phase 57L DWORD .code write should be denied");

    failures += expect_json_contains(before_write_json, "\"code\":\"region-boundary-crossing\"", "Phase 57L write from before .code should use region-boundary-crossing");
    failures += expect_json_contains(before_write_json, "memory address range 003FFFFFh..00400000h crosses/overlaps a no-access memory region, .CODE/_TEXT, that starts at 00400000h", "Phase 57L write from before .code should describe .code overlap");
    failures += expect_json_contains(before_write_json, "\"memoryChanges\":[]", "Phase 57L cross-region .code write should not record memory changes");
    failures += expect_json_contains(inside_out_write_json, "\"code\":\"region-boundary-crossing\"", "Phase 57L write from inside .code out should use region-boundary-crossing");
    failures += expect_json_contains(inside_out_write_json, "memory address range 004FFFFFh..00500000h crosses/overlaps a no-access memory region, .CODE/_TEXT, that starts at 00400000h", "Phase 57L write from inside .code out should describe .code overlap");
    failures += expect_json_contains(inside_out_write_json, "\"memoryChanges\":[]", "Phase 57L inside-out .code write should not record memory changes");

    failures += expect_json_contains(rmw_json, "\"code\":\"unsupported-code-memory-access\"", "Phase 57L .code read-modify-write should use unsupported-code-memory-access");
    failures += expect_json_contains(rmw_json, "Memory read at 00400000h for 4 bytes overlaps .CODE/_TEXT", "Phase 57L .code read-modify-write should stop at read stage");
    failures += expect_json_contains(rmw_json, "\"EFLAGS\":{\"hex\":\"00000000h\",\"unsigned\":0}", "Phase 57L failed .code RMW should not update flags");
    failures += expect_json_contains(rmw_json, "\"memoryChanges\":[]", "Phase 57L failed .code RMW should not write back");
    failures += expect_json_not_contains(rmw_json, "execution-complete", "Phase 57L failed .code RMW should not complete");

    return failures;
}

/// Verifies Phase 57M targeted source-run diagnostics for segment/group symbols.
///
/// @return Number of failures.
static int test_phase57m_segment_symbol_source_run(void) {
    char offset_text_json[TEST_JSON_COPY_CAPACITY];
    char ptr_data_json[TEST_JSON_COPY_CAPACITY];
    char offset_stack_json[TEST_JSON_COPY_CAPACITY];
    char offset_flat_json[TEST_JSON_COPY_CAPACITY];
    char segment_def_json[TEST_JSON_COPY_CAPACITY];
    char group_def_json[TEST_JSON_COPY_CAPACITY];
    char casemap_all_json[TEST_JSON_COPY_CAPACITY];
    char casemap_none_exact_json[TEST_JSON_COPY_CAPACITY];
    char casemap_none_ordinary_json[TEST_JSON_COPY_CAPACITY];
    char ordinary_label_json[TEST_JSON_COPY_CAPACITY];
    int failures = 0;

    copy_source_run_json(
        offset_text_json,
        sizeof(offset_text_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET _TEXT\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        ptr_data_json,
        sizeof(ptr_data_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov eax, DWORD PTR [_DATA]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        offset_stack_json,
        sizeof(offset_stack_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET STACK\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        offset_flat_json,
        sizeof(offset_flat_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET FLAT\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        segment_def_json,
        sizeof(segment_def_json),
        masm32_sim_wasm_run_source_json(
            "_TEXT SEGMENT\n"
            "_TEXT ENDS\n"
            ".code\n"
            "main PROC\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        group_def_json,
        sizeof(group_def_json),
        masm32_sim_wasm_run_source_json(
            "DGROUP GROUP _DATA, _BSS\n"
            ".code\n"
            "main PROC\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        casemap_all_json,
        sizeof(casemap_all_json),
        masm32_sim_wasm_run_source_json(
            "OPTION CASEMAP:ALL\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET _text\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        casemap_none_exact_json,
        sizeof(casemap_none_exact_json),
        masm32_sim_wasm_run_source_json(
            "OPTION CASEMAP:NONE\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET _TEXT\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        casemap_none_ordinary_json,
        sizeof(casemap_none_ordinary_json),
        masm32_sim_wasm_run_source_json(
            "OPTION CASEMAP:NONE\n"
            ".data\n"
            "_text DWORD 77\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET _text\n"
            "    mov ebx, _text\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        ordinary_label_json,
        sizeof(ordinary_label_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "value DWORD 1\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET value\n"
            "    mov ebx, value\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );

    printf("PHASE 57M source-run program exercised: phase57m-segment-group-symbol-diagnostics\n");

    failures += expect_json_contains(offset_text_json, "\"phaseSuffix\":\"\"", "segment diagnostics should report the current runtime phase suffix");
    failures += expect_json_contains(offset_text_json, "\"ok\":false", "OFFSET _TEXT should fail source-run");
    failures += expect_json_contains(offset_text_json, "\"status\":\"parse-error\"", "OFFSET _TEXT should be a parse-time assembly diagnostic");
    failures += expect_json_contains(offset_text_json, "\"kind\":\"unsupported-feature\"", "OFFSET _TEXT should render as unsupported feature category");
    failures += expect_json_contains(offset_text_json, "\"code\":\"unsupported-segment-symbol\"", "OFFSET _TEXT should use unsupported-segment-symbol code");
    failures += expect_json_contains(offset_text_json, "MASM/object segment symbol", "OFFSET _TEXT diagnostic should explain segment symbol");
    failures += expect_json_contains(offset_text_json, "does not expose linker segment symbols or readable `.code` / section images", "OFFSET _TEXT diagnostic should not expose .code alias");
    failures += expect_json_not_contains(offset_text_json, "execution-complete", "OFFSET _TEXT should not complete");
    failures += expect_json_not_contains(offset_text_json, "programConsole", "OFFSET _TEXT should not create Program Console output");

    failures += expect_json_contains(ptr_data_json, "\"code\":\"unsupported-segment-symbol\"", "DWORD PTR [_DATA] should use unsupported-segment-symbol code");
    failures += expect_json_contains(ptr_data_json, "Use declared data labels instead", "DWORD PTR [_DATA] diagnostic should recommend labels");
    failures += expect_json_not_contains(ptr_data_json, "unknown-symbol", "DWORD PTR [_DATA] should not fall through to unknown-symbol");
    failures += expect_json_contains(offset_stack_json, "\"code\":\"unsupported-segment-symbol\"", "OFFSET STACK should use unsupported-segment-symbol code");
    failures += expect_json_contains(offset_stack_json, "MASM/object stack-segment symbol", "OFFSET STACK diagnostic should explain stack segment symbol");
    failures += expect_json_contains(offset_flat_json, "\"code\":\"unsupported-segment-symbol\"", "OFFSET FLAT should use unsupported-segment-symbol code");
    failures += expect_json_contains(offset_flat_json, "MASM memory-model group concept", "OFFSET FLAT diagnostic should explain group concept");

    failures += expect_json_contains(segment_def_json, "\"code\":\"unsupported-segment-symbol\"", "_TEXT SEGMENT should use unsupported-segment-symbol code");
    failures += expect_json_contains(segment_def_json, "MASM/object segment symbol", "_TEXT SEGMENT diagnostic should explain segment symbol");
    failures += expect_json_not_contains(segment_def_json, "execution-complete", "_TEXT SEGMENT should not execute");
    failures += expect_json_not_contains(segment_def_json, "programConsole", "_TEXT SEGMENT should not create Program Console output");

    failures += expect_json_contains(group_def_json, "\"code\":\"unsupported-segment-symbol\"", "DGROUP GROUP should use unsupported-segment-symbol code");
    failures += expect_json_contains(group_def_json, "MASM memory-model group concept", "DGROUP GROUP diagnostic should explain group concept");
    failures += expect_json_not_contains(group_def_json, "execution-complete", "DGROUP GROUP should not execute");
    failures += expect_json_not_contains(group_def_json, "programConsole", "DGROUP GROUP should not create Program Console output");

    failures += expect_json_contains(casemap_all_json, "\"code\":\"unsupported-segment-symbol\"", "CASEMAP:ALL should diagnose _text as _TEXT");
    failures += expect_json_contains(casemap_none_exact_json, "\"code\":\"unsupported-segment-symbol\"", "CASEMAP:NONE exact _TEXT should still diagnose");
    failures += expect_json_contains(casemap_none_ordinary_json, "\"ok\":true", "CASEMAP:NONE non-exact _text ordinary label should run");
    failures += expect_json_contains(casemap_none_ordinary_json, "\"EAX\":{\"hex\":\"00500000h\",\"unsigned\":5242880}", "CASEMAP:NONE non-exact _text should get normal data address");
    failures += expect_json_contains(casemap_none_ordinary_json, "\"EBX\":{\"hex\":\"0000004Dh\",\"unsigned\":77}", "CASEMAP:NONE non-exact _text should read normal data value");
    failures += expect_json_not_contains(casemap_none_ordinary_json, "unsupported-segment-symbol", "CASEMAP:NONE non-exact _text should not diagnose");

    failures += expect_json_contains(ordinary_label_json, "\"ok\":true", "ordinary data label should continue to run");
    failures += expect_json_contains(ordinary_label_json, "\"EAX\":{\"hex\":\"00500000h\",\"unsigned\":5242880}", "ordinary data label OFFSET should still resolve");
    failures += expect_json_contains(ordinary_label_json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "ordinary data label read should still execute");

    return failures;
}

/// Verifies Phase 57-CORR1 reports `.CONST` context for a cross-region write overlap.
///
/// @return Number of failures.
static int test_phase57corr1_const_cross_region_write_diagnostic_context(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".const\n"
        "x BYTE 1\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    mov ebx, [eax]\n"
        "    sub eax, 2\n"
        "    mov DWORD PTR [eax], 0FFFFFFFFh\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    printf("PHASE 57-CORR1 source-run program exercised: phase57corr1-const-cross-region-write-overlap\n");
    failures += expect_json_contains(json, "\"phase\":77", "Phase 57-CORR1 regression should report current runtime/source-run phase metadata");
    failures += expect_json_contains(json, "\"ok\":false", "Phase 57-CORR1 cross-region .CONST write should fail");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Phase 57-CORR1 cross-region .CONST write should be an execution error");
    failures += expect_json_contains(json, "\"instructionCount\":3", "Phase 57-CORR1 failing write should stop after three completed instructions");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"005FFFFEh\",\"unsigned\":6291454}", "Phase 57-CORR1 EAX should retain completed SUB result");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "Phase 57-CORR1 EBX should retain the successful .CONST read value");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Phase 57-CORR1 failing write should not commit memory changes");
    failures += expect_json_contains(json, "\"code\":\"region-boundary-crossing\"", "Phase 57-CORR1 should use the region-boundary diagnostic code");
    failures += expect_json_contains(json, "Cross-region memory write at 005FFFFEh for 4 bytes", "Phase 57-CORR1 diagnostic should identify attempted write start and width");
    failures += expect_json_contains(json, "memory address range 005FFFFEh..00600001h crosses/overlaps a protected memory region, .CONST, that starts at 00600000h", "Phase 57-CORR1 diagnostic should explain the protected .CONST crossing");
    failures += expect_json_contains(json, "program stopped before access", "Phase 57-CORR1 diagnostic should explain that execution stopped before access");
    failures += expect_json_not_contains(json, "execution-complete", "Phase 57-CORR1 fatal write should not emit execution-complete");

    return failures;
}

/// Verifies Phase 57-CORR1 preserves direct `.CONST` runtime write rejection.
///
/// @return Number of failures.
static int test_phase57corr1_direct_const_write_still_permission_denied(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".const\n"
        "x BYTE 1\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    mov DWORD PTR [eax], 0FFFFFFFFh\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    printf("PHASE 57-CORR1 source-run program exercised: phase57corr1-direct-const-write-permission\n");
    failures += expect_json_contains(json, "\"ok\":false", "Phase 57-CORR1 direct .CONST write should fail");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Phase 57-CORR1 direct .CONST write should preserve permission-denied");
    failures += expect_json_contains(json, "Memory write at 00600000h for 4 bytes is not permitted in .const.", "Phase 57-CORR1 direct .CONST write should mention read-only .const storage");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Phase 57-CORR1 direct .CONST write should not commit memory changes");
    failures += expect_json_not_contains(json, "execution-complete", "Phase 57-CORR1 direct .CONST failure should not emit execution-complete");

    return failures;
}

/// Verifies Phase 57-CORR1 does not reclassify cross-region reads as `.CONST` writes.
///
/// @return Number of failures.
static int test_phase57corr1_const_cross_region_read_remains_region_failure(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".const\n"
        "x BYTE 1\n"
        "\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET x\n"
        "    sub eax, 2\n"
        "    mov ebx, DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    printf("PHASE 57-CORR1 source-run program exercised: phase57corr1-const-cross-region-read\n");
    failures += expect_json_contains(json, "\"ok\":false", "Phase 57-CORR1 cross-region read should fail");
    failures += expect_json_contains(json, "\"code\":\"region-boundary-crossing\"", "Phase 57-CORR1 cross-region read should use the region-boundary diagnostic code");
    failures += expect_json_contains(json, "Cross-region memory read at 005FFFFEh for 4 bytes", "Phase 57-CORR1 cross-region read should identify attempted read start and width");
    failures += expect_json_contains(json, "memory address range 005FFFFEh..00600001h crosses/overlaps a protected memory region, .CONST, that starts at 00600000h", "Phase 57-CORR1 cross-region read should explain the protected .CONST crossing");
    failures += expect_json_not_contains(json, "permission-denied", "Phase 57-CORR1 cross-region read must not become a .CONST permission failure");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Phase 57-CORR1 cross-region read should not commit memory changes");

    return failures;
}

/// Verifies ordinary non-protected-region range diagnostics remain unchanged.
///
/// @return Number of failures.
static int test_non_protected_cross_region_write_remains_ordinary_region_failure(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 007FFFFEh\n"
        "    mov DWORD PTR [eax], 12345678h\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    printf("PHASE 57L source-run program exercised: non-protected-cross-region-write\n");
    failures += expect_json_contains(json, "\"ok\":false", "non-protected cross-region write should fail");
    failures += expect_json_contains(json, "\"code\":\"invalid-address\"", "non-protected cross-region write should remain a region/range diagnostic");
    failures += expect_json_contains(json, "Invalid memory write at 007FFFFEh for 4 bytes", "non-protected cross-region write should identify attempted write start and width");
    failures += expect_json_not_contains(json, "protected memory region, .CONST", "non-protected cross-region write should not gain protected .CONST wording");
    failures += expect_json_not_contains(json, "no-access memory region, .CODE/_TEXT", "non-protected cross-region write should not gain protected .code wording");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "non-protected cross-region write should not commit memory changes");
    failures += expect_json_not_contains(json, "execution-complete", "non-protected cross-region write should not emit execution-complete");

    return failures;
}

/// Verifies Phase 51 keeps lower-level .CONST write rejection ahead of object-bound diagnostics.
///
/// @return Number of failures.
static int test_phase51_const_write_precedes_object_diagnostics(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    inc DWORD PTR [eax]\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    );
    int failures = 0;

    printf("PHASE 51 source-run program exercised: phase51-const-permission-precedence\n");
    failures += expect_json_contains(json, "\"ok\":false", "Phase 51 .CONST precedence smoke should fail execution");
    failures += expect_json_contains(json, "\"status\":\"execution-error\"", "Phase 51 .CONST precedence smoke should be an execution error");
    failures += expect_json_contains(json, "\"code\":\"permission-denied\"", "Phase 51 .CONST precedence smoke should preserve permission-denied");
    failures += expect_json_not_contains(json, "object-bounds-violation", "Phase 51 .CONST precedence smoke should not be reclassified as object-bounds");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "Phase 51 .CONST precedence smoke should not commit memory changes");

    return failures;
}

/// Verifies Phase 51 uninitialized-read warning mode catches a read-modify-write before write-back.
///
/// @return Number of failures.
static int test_phase51_uninitialized_rmw_smoke_harness(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_memory_validation_and_uninitialized_metadata(
        ".data\n"
        "x DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    add x, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS
    );
    int failures = 0;

    printf("PHASE 51 source-run program exercised: phase51-uninitialized-rmw-warning\n");
    failures += expect_json_contains(json, "\"ok\":true", "Phase 51 RMW warning smoke should continue");
    failures += expect_json_contains(json, "\"code\":\"uninitialized-read\"", "Phase 51 RMW warning smoke should diagnose the read before write-back");
    failures += expect_json_contains(json, "\"symbol\":\"x\",\"state\":\"tracked\",\"initializedByteCount\":4,\"uninitializedByteCount\":0,\"initializedMask\":\"1111\"", "Phase 51 RMW warning smoke should mark bytes initialized after write-back");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "Phase 51 RMW warning smoke should complete after warning");

    return failures;
}

/// Verifies Phase 51 Irvine32 exit case-insensitivity remains separate from user-symbol CASEMAP policy.
///
/// @return Number of failures.
static int test_phase51_irvine_exit_and_casemap_smoke_harness(void) {
    int failures = 0;

    failures += run_phase51_source_run_success_smoke(
        "phase51-irvine-exit-lowercase",
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 11\n"
        "    exit\n"
        "    mov eax, 99\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"0000000Bh\",\"unsigned\":11}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-irvine-exit-uppercase",
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 12\n"
        "    EXIT\n"
        "    mov eax, 99\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"0000000Ch\",\"unsigned\":12}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-irvine-exit-mixed-case-with-casemap-none",
        "INCLUDE Irvine32.inc\n"
        "OPTION CASEMAP:NONE\n"
        ".data\n"
        "Value DWORD 5\n"
        "value DWORD 9\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, Value\n"
        "    Exit\n"
        "    mov eax, value\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"00000005h\",\"unsigned\":5}"
    );

    return failures;
}

/// Verifies Phase 51 source-run smoke coverage for post-30 instruction families.
///
/// @return Number of failures.
static int test_phase51_instruction_family_source_run_smoke_harness(void) {
    int failures = 0;

    failures += run_phase51_source_run_success_smoke(
        "phase51-inc-dec-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    inc eax\n"
        "    dec eax\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":4",
        "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-and-or-xor-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov eax, 0F0h\n"
        "    and eax, 0Fh\n"
        "    or eax, 10h\n"
        "    xor eax, 1h\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":5",
        "\"EAX\":{\"hex\":\"00000011h\",\"unsigned\":17}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-not-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov al, 0F0h\n"
        "    not al\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"0000000Fh\",\"unsigned\":15}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-shl-sal-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov al, 1\n"
        "    shl al, 1\n"
        "    sal al, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":4",
        "\"EAX\":{\"hex\":\"00000004h\",\"unsigned\":4}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-shr-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    shr al, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"00000040h\",\"unsigned\":64}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-sar-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    sar al, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"000000C0h\",\"unsigned\":192}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-rol-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov al, 80h\n"
        "    rol al, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}"
    );

    failures += run_phase51_source_run_success_smoke(
        "phase51-ror-source-smoke",
        ".code\n"
        "main PROC\n"
        "    mov al, 1\n"
        "    ror al, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        "\"instructionCount\":3",
        "\"EAX\":{\"hex\":\"00000080h\",\"unsigned\":128}"
    );

    return failures;
}

/// @return Zero when all source-run API tests pass.

/// Verifies Phase 61 executes a lowered direct JMP after a prior instruction.
///
/// @return Number of failures.
static int test_phase61_jmp_executes_after_prior_instruction(void) {
    char json_copy[TEST_JSON_COPY_CAPACITY];
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    jmp done\n"
        "    mov ebx, 2\n"
        "done:\n"
        "    mov ecx, 3\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    copy_source_run_json(json_copy, sizeof(json_copy), json);
    failures += expect_json_contains(json_copy, "\"phase\":77", "JMP regression response should report current Phase 71 numeric phase metadata");
    failures += expect_json_contains(json_copy, "\"ok\":true", "valid direct JMP should execute successfully");
    failures += expect_json_contains(json_copy, "\"status\":\"ok\"", "valid direct JMP should report OK status");
    failures += expect_json_contains(json_copy, "\"instructionCount\":4", "JMP program should count committed MOV, JMP, and target MOV");
    failures += expect_json_contains(json_copy, "\"executedInstructionCount\":4", "direct JMP should count as one executed instruction");
    failures += expect_json_contains(json_copy, "\"attemptedNextInstructionIndex\":null", "successful JMP run should not report an attempted blocked instruction");
    failures += expect_json_contains(json_copy, "\"currentInstructionIndex\":3", "last committed instruction should be target MOV after JMP");
    failures += expect_json_contains(json_copy, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "instruction before JMP should commit EAX");
    failures += expect_json_contains(json_copy, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "instruction after JMP should not commit EBX");
    failures += expect_json_contains(json_copy, "\"ECX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "target instruction should run in Phase 61");
    failures += expect_json_not_contains(json_copy, "branch-runtime-deferred", "valid Phase 61 direct JMP should not emit branch-runtime-deferred");
    failures += expect_json_contains(json_copy, "\"code\":\"execution-complete\"", "valid direct JMP should emit execution-complete");

    return failures;
}

/// Verifies Phase 61 executes a first-instruction JMP with no fall-through mutation.
///
/// @return Number of failures.
static int test_phase61_jmp_executes_first_instruction(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jmp done\n"
        "    mov ebx, 2\n"
        "done:\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "first-instruction JMP should execute successfully");
    failures += expect_json_contains(json, "\"instructionCount\":3", "first-instruction JMP should commit JMP and target MOV");
    failures += expect_json_contains(json, "\"executedInstructionCount\":3", "first-instruction JMP should count as executed");
    failures += expect_json_contains(json, "\"attemptedNextInstructionIndex\":null", "successful first-instruction JMP should not report attempted next instruction");
    failures += expect_json_contains(json, "\"currentInstructionIndex\":2", "first-instruction JMP should finish at target MOV");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "first-instruction JMP should not produce memory changes");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "target instruction after first-instruction JMP should mutate EAX");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "fall-through instruction after first-instruction JMP should be skipped");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "valid first-instruction JMP should not emit deferred diagnostic");

    return failures;
}

/// Verifies Phase 61 can jump to the immediately following instruction.
///
/// @return Number of failures.
static int test_phase61_jmp_to_immediately_following_instruction(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    jmp next\n"
        "next:\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "JMP to immediately following target should execute successfully");
    failures += expect_json_contains(json, "\"instructionCount\":3", "JMP to following target should commit JMP and target MOV");
    failures += expect_json_contains(json, "\"executedInstructionCount\":3", "JMP to following target should count both instructions");
    failures += expect_json_contains(json, "\"currentInstructionIndex\":1", "JMP to following target should finish at target MOV");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000001h\",\"unsigned\":1}", "JMP to following target should execute target MOV");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "JMP to following target should not emit deferred diagnostic");

    return failures;
}

/// Verifies Phase 61 treats a procedure entry target as a direct branch only.
///
/// @return Number of failures.
static int test_phase61_jmp_to_procedure_entry_executes_as_direct_branch(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    jmp targetProc\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "targetProc PROC\n"
        "    mov ebx, 2\n"
        "    exit\n"
        "targetProc ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "procedure-entry direct JMP should execute successfully");
    failures += expect_json_contains(json, "\"instructionCount\":3", "procedure-entry direct JMP should commit JMP plus target MOV only");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0}", "procedure-entry direct JMP should skip fall-through MOV");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000002h\",\"unsigned\":2}", "procedure-entry direct JMP target body should execute");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "procedure-entry direct JMP should not create memory changes");
    failures += expect_json_not_contains(json, "call-depth-exceeded", "procedure-entry direct JMP should not report call-depth diagnostics");

    return failures;
}

/// Verifies Phase 61 backward JMP reaches the instruction-count watchdog deterministically.
///
/// @return Number of failures.
static int test_phase61_backward_jmp_reaches_instruction_limit(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "start:\n"
        "    inc eax\n"
        "    jmp start\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        5U
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "backward JMP loop should stop at instruction limit");
    failures += expect_json_contains(json, "\"code\":\"instruction-limit-exceeded\"", "backward JMP loop should report instruction-limit-exceeded");
    failures += expect_json_contains(json, "\"instructionCount\":5", "backward JMP loop should commit exactly the configured instruction limit");
    failures += expect_json_contains(json, "\"executedInstructionCount\":5", "backward JMP loop should report executed instruction count");
    failures += expect_json_contains(json, "\"attemptedNextInstructionIndex\":1", "instruction limit should block the next JMP after five committed instructions");
    failures += expect_json_contains(json, "\"currentInstructionIndex\":0", "last committed instruction before the limit should be INC");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000003h\",\"unsigned\":3}", "loop should have executed INC three times before limit");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "Phase 61 backward JMP should not emit deferred diagnostic");

    return failures;
}

/// Verifies Phase 61E rejects MASM-reserved instruction mnemonic labels before execution.
///
/// @return Number of failures.
static int test_phase61e_reserved_loop_label_source_run_diagnostic(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "\n"
        "mov eax, 0\n"
        "\n"
        "loop:\n"
        "inc eax\n"
        "jmp loop\n"
        "\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        5U
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "label named loop should fail before execution in current MASM-compatible scope");
    failures += expect_json_contains(json, "\"status\":\"parse-error\"", "label named loop should remain an assembly-time failure");
    failures += expect_json_contains(json, "\"code\":\"reserved-word-symbol\"", "loop declaration should be classified as a reserved-word symbol");
    failures += expect_json_contains(json, "reserved MASM instruction mnemonic", "reserved-word declaration diagnostic should be explicit");
    failures += expect_json_contains(json, "\"instructionCount\":0", "reserved-word target parse failure should not execute instructions");
    failures += expect_json_contains(json, "\"executedInstructionCount\":0", "reserved-word target parse failure should not commit instructions");
    failures += expect_json_not_contains(json, "instruction-limit-exceeded", "reserved-word declaration should not reach the instruction limit");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "reserved-word declaration should not emit deferred branch diagnostics");

    return failures;
}

/// Verifies Phase 59 instruction-limit precedence remains before a later Phase 61 JMP.
///
/// @return Number of failures.
static int test_phase61_jmp_respects_instruction_limit_precedence(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    jmp done\n"
        "done:\n"
        "    mov ebx, 2\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        1U
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"code\":\"instruction-limit-exceeded\"", "instruction limit should fire before fetching JMP");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "JMP should not be reached after instruction limit stops execution");
    failures += expect_json_contains(json, "\"attemptedNextInstructionIndex\":1", "instruction limit should report the JMP as the blocked next instruction");

    return failures;
}

/// Verifies Phase 61A direct-JMP hardening for skipped writes, console separation, and accounting.
///
/// @return Number of failures.
static int test_phase61a_jmp_skips_memory_write_and_keeps_console_empty(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    jmp done\n"
        "    mov value, 77\n"
        "done:\n"
        "    mov eax, value\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":true", "Phase 61A skipped-write JMP fixture should complete successfully");
    failures += expect_json_contains(json, "\"phase\":77", "current runtime should report numeric phase metadata");
    failures += expect_json_contains(json, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "Phase 77 should report the runtime phase name");
    failures += expect_json_contains(json, "\"instructionCount\":3", "JMP plus target read should count as two committed instructions");
    failures += expect_json_contains(json, "\"executedInstructionCount\":3", "committed direct JMP should be included in executedInstructionCount");
    failures += expect_json_contains(json, "\"attemptedNextInstructionIndex\":null", "successful skipped-write JMP run should not report a blocked instruction");
    failures += expect_json_contains(json, "\"currentInstructionIndex\":2", "last committed instruction should be the target MOV after the skipped write");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000000h\",\"unsigned\":0", "skipped memory write should leave value readable as zero");
    failures += expect_json_contains(json, "\"memoryChanges\":[]", "direct JMP fixture should have no successful memory changes from the skipped write");
    failures += expect_json_not_contains(json, "programConsole", "direct JMP should not create Program Console output");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "valid direct JMP should not emit deferred branch diagnostics");
    failures += expect_json_contains(json, "\"code\":\"execution-complete\"", "successful direct-JMP hardening fixture should emit execution-complete");

    return failures;
}

/// Verifies Phase 61A accounting when a backward direct-JMP loop stops at the watchdog.
///
/// @return Number of failures.
static int test_phase61a_backward_jmp_limit_blocks_next_fetch_without_mutation(void) {
    const char *json = masm32_sim_wasm_run_source_json_with_instruction_limit(
        ".code\n"
        "main PROC\n"
        "start:\n"
        "    inc eax\n"
        "    jmp start\n"
        "    mov ebx, 99\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        4U
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"ok\":false", "backward direct-JMP watchdog fixture should fail the run");
    failures += expect_json_contains(json, "\"code\":\"instruction-limit-exceeded\"", "backward direct-JMP watchdog fixture should use instruction-limit-exceeded");
    failures += expect_json_contains(json, "\"instructionCount\":4", "backward direct-JMP watchdog fixture should commit exactly four instructions");
    failures += expect_json_contains(json, "\"executedInstructionCount\":4", "executedInstructionCount should match committed instructions at the limit");
    failures += expect_json_contains(json, "\"attemptedNextInstructionIndex\":0", "instruction limit should identify the blocked next INC fetch");
    failures += expect_json_contains(json, "\"currentInstructionIndex\":1", "last committed instruction should be the backward JMP");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"00000002h\",\"unsigned\":2", "only committed INC instructions should mutate EAX");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"00000000h\",\"unsigned\":0", "unreached instruction after loop should not mutate EBX");
    failures += expect_json_not_contains(json, "execution-complete", "instruction-limit failure must not emit execution-complete");
    failures += expect_json_not_contains(json, "branch-runtime-deferred", "Phase 61A watchdog coverage should not regress to branch-runtime-deferred");

    return failures;
}

/// Verifies Phase 69B source-run message ordering contract for rendered groups.
///
/// The blank separator itself is a formatter-only concern tested by Node. These
/// structured checks prove the source-run JSON still emits ordinary messages,
/// keeps the startup notice first when execution begins, emits nonfatal
/// pre-execution diagnostics before runtime diagnostics, and does not add
/// synthetic separator diagnostics.
///
/// @return Number of failures.
static int test_phase69b_source_run_message_ordering_contract(void) {
    int failures = 0;
    char success_json[TEST_JSON_COPY_CAPACITY];
    char warning_json[TEST_JSON_COPY_CAPACITY];
    char warning_notice_off_json[TEST_JSON_COPY_CAPACITY];
    char casemap_success_json[TEST_JSON_COPY_CAPACITY];
    char compatibility_json[TEST_JSON_COPY_CAPACITY];
    char compatibility_notice_off_json[TEST_JSON_COPY_CAPACITY];
    char casemap_runtime_error_json[TEST_JSON_COPY_CAPACITY];
    char runtime_error_json[TEST_JSON_COPY_CAPACITY];
    char warning_then_error_json[TEST_JSON_COPY_CAPACITY];
    char multi_warning_json[TEST_JSON_COPY_CAPACITY];
    char parse_error_json[TEST_JSON_COPY_CAPACITY];
    char invalid_setting_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(
        success_json,
        sizeof(success_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov eax, 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        warning_json,
        sizeof(warning_json),
        masm32_sim_wasm_run_source_json(
            ".DATA?\n"
            "x DWORD ?\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, x\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        warning_notice_off_json,
        sizeof(warning_notice_off_json),
        masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(
            ".DATA?\n"
            "x DWORD ?\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, x\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF
        )
    );
    copy_source_run_json(
        casemap_success_json,
        sizeof(casemap_success_json),
        masm32_sim_wasm_run_source_json(
            "OPTION CASEMAP:NONE\n"
            "OPTION CASEMAP:ALL\n"
            ".data\n"
            "buf DWORD 1\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, bUF\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        compatibility_json,
        sizeof(compatibility_json),
        masm32_sim_wasm_run_source_json(
            ".686\n"
            ".stack 4096\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        compatibility_notice_off_json,
        sizeof(compatibility_notice_off_json),
        masm32_sim_wasm_run_source_json_with_ui_settings(
            ".686\n"
            ".stack 4096\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n",
            MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_COMPATIBILITY_NOTICES_OFF
        )
    );
    copy_source_run_json(
        casemap_runtime_error_json,
        sizeof(casemap_runtime_error_json),
        masm32_sim_wasm_run_source_json(
            "OPTION CASEMAP:NONE\n"
            "OPTION CASEMAP:ALL\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, 0\n"
            "    mov ebx, DWORD PTR [eax]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        runtime_error_json,
        sizeof(runtime_error_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov eax, 0\n"
            "    mov ebx, DWORD PTR [eax]\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        warning_then_error_json,
        sizeof(warning_then_error_json),
        masm32_sim_wasm_run_source_json(
            ".DATA?\n"
            "x DWORD ?\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, x\n"
            "    mov ebx, 0\n"
            "    div ebx\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        multi_warning_json,
        sizeof(multi_warning_json),
        masm32_sim_wasm_run_source_json(
            ".DATA?\n"
            "x DWORD ?\n"
            "y DWORD ?\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, x\n"
            "    mov ebx, y\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        parse_error_json,
        sizeof(parse_error_json),
        masm32_sim_wasm_run_source_json(
            ".code\n"
            "main PROC\n"
            "    mov [eax], 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        invalid_setting_json,
        sizeof(invalid_setting_json),
        masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(
            ".code\n"
            "main PROC\n"
            "    ret\n"
            "END main\n",
            (Masm32SimWasmStartupStateNoticeSetting)99
        )
    );

    failures += expect_json_contains(success_json, "\"ok\":true", "Phase 69B success fixture should execute");
    failures += expect_json_contains_once(success_json, "startup-state-notice", "Phase 69B success fixture should contain one startup notice");
    failures += expect_json_contains_once(success_json, "execution-complete", "Phase 69B success fixture should contain one completion message");
    failures += expect_json_fragment_order(success_json, "startup-state-notice", "execution-complete", "Phase 69B startup notice should precede completion");
    failures += expect_json_not_contains(success_json, "blank-line", "Phase 69B JSON should not contain formatter separator records");
    failures += expect_json_not_contains(success_json, "-------------------------------------------------------------------", "Phase 69B JSON should not contain major register divider records");
    failures += expect_json_not_contains(success_json, "       |", "Phase 69B JSON should not contain parent-family spacer records");

    failures += expect_json_contains(warning_json, "\"ok\":true", "Phase 69B warning fixture should execute");
    failures += expect_json_fragment_order(warning_json, "startup-state-notice", "uninitialized-read", "Phase 69B startup notice should precede runtime warning");
    failures += expect_json_fragment_order(warning_json, "uninitialized-read", "execution-complete", "Phase 69B runtime warning should precede completion");
    failures += expect_json_contains_once(warning_json, "uninitialized-read", "Phase 69B single-warning fixture should emit one runtime warning");
    failures += expect_json_not_contains(warning_json, "blank-line", "Phase 69B warning JSON should not contain formatter separator records");

    failures += expect_json_contains(warning_notice_off_json, "\"ok\":true", "Phase 69B startup-off warning fixture should execute");
    failures += expect_json_not_contains(warning_notice_off_json, "startup-state-notice", "Phase 69B startup-off fixture should suppress only startup notice");
    failures += expect_json_fragment_order(warning_notice_off_json, "uninitialized-read", "execution-complete", "Phase 69B startup-off warning should still precede completion");

    failures += expect_json_contains(casemap_success_json, "\"ok\":true", "Phase 69B CASEMAP warning fixture should execute");
    failures += expect_json_fragment_order(casemap_success_json, "startup-state-notice", "casemap-policy-changed", "Phase 69B startup notice should precede nonfatal CASEMAP warning");
    failures += expect_json_fragment_order(casemap_success_json, "casemap-policy-changed", "execution-complete", "Phase 69B nonfatal CASEMAP warning should precede completion");
    failures += expect_json_contains_once(casemap_success_json, "casemap-policy-changed", "Phase 69B CASEMAP success fixture should emit one policy warning");
    failures += expect_json_not_contains(casemap_success_json, "blank-line", "Phase 69B CASEMAP JSON should not contain formatter separator records");

    failures += expect_json_contains(compatibility_json, "\"ok\":true", "Phase 69B compatibility-notice fixture should execute");
    failures += expect_json_fragment_order(compatibility_json, "startup-state-notice", "compatibility-no-op", "Phase 69B startup notice should precede processor compatibility notice");
    failures += expect_json_fragment_order(compatibility_json, "startup-state-notice", "compatibility-metadata-only", "Phase 69B startup notice should precede .stack compatibility notice");
    failures += expect_json_fragment_order(compatibility_json, "compatibility-metadata-only", "execution-complete", "Phase 69B compatibility notices should precede completion");
    failures += expect_json_not_contains(compatibility_json, "blank-line", "Phase 69B compatibility JSON should not contain formatter separator records");

    failures += expect_json_contains(compatibility_notice_off_json, "\"ok\":true", "Phase 69B compatibility-off fixture should execute");
    failures += expect_json_not_contains(compatibility_notice_off_json, "compatibility-no-op", "Phase 69B compatibility-off fixture should suppress no-op notices");
    failures += expect_json_not_contains(compatibility_notice_off_json, "compatibility-metadata-only", "Phase 69B compatibility-off fixture should suppress metadata-only notices");
    failures += expect_json_fragment_order(compatibility_notice_off_json, "startup-state-notice", "execution-complete", "Phase 69B compatibility-off fixture should still emit startup before completion");

    failures += expect_json_contains(casemap_runtime_error_json, "\"ok\":false", "Phase 69B CASEMAP warning plus runtime error fixture should fail at runtime");
    failures += expect_json_fragment_order(casemap_runtime_error_json, "startup-state-notice", "casemap-policy-changed", "Phase 69B startup notice should precede nonfatal pre-execution warning before runtime error");
    failures += expect_json_fragment_order(casemap_runtime_error_json, "casemap-policy-changed", "runtime-error", "Phase 69B nonfatal pre-execution warning should precede runtime error");
    failures += expect_json_not_contains(casemap_runtime_error_json, "execution-complete", "Phase 69B CASEMAP warning plus runtime error should not complete");

    failures += expect_json_contains(runtime_error_json, "\"ok\":false", "Phase 69B runtime error fixture should fail at runtime");
    failures += expect_json_fragment_order(runtime_error_json, "startup-state-notice", "runtime-error", "Phase 69B startup notice should precede runtime error");
    failures += expect_json_not_contains(runtime_error_json, "execution-complete", "Phase 69B runtime error should not complete");

    failures += expect_json_contains(warning_then_error_json, "\"ok\":false", "Phase 69B warning-then-error fixture should fail at runtime");
    failures += expect_json_fragment_order(warning_then_error_json, "startup-state-notice", "uninitialized-read", "Phase 69B startup notice should precede warning before fatal error");
    failures += expect_json_fragment_order(warning_then_error_json, "uninitialized-read", "divide-by-zero", "Phase 69B runtime warning should remain before fatal runtime error");
    failures += expect_json_not_contains(warning_then_error_json, "execution-complete", "Phase 69B fatal runtime error should not complete");

    failures += expect_json_contains(multi_warning_json, "\"ok\":true", "Phase 69B multiple-warning fixture should execute");
    failures += expect_json_fragment_order(multi_warning_json, "startup-state-notice", "uninitialized-read", "Phase 69B startup notice should precede multiple warnings");
    failures += count_json_fragment_occurrences(multi_warning_json, "uninitialized-read") == 2U ? 0 : record_failure("Phase 69B multiple-warning fixture should emit two uninitialized-read warnings");
    failures += expect_json_fragment_order(multi_warning_json, "uninitialized-read", "execution-complete", "Phase 69B multiple-warning fixture should complete after warnings");

    failures += expect_json_contains(parse_error_json, "\"ok\":false", "Phase 69B parse-error fixture should fail before runtime");
    failures += expect_json_not_contains(parse_error_json, "startup-state-notice", "Phase 69B pre-execution diagnostics should not emit startup notice");
    failures += expect_json_not_contains(parse_error_json, "execution-complete", "Phase 69B pre-execution diagnostics should not complete");

    failures += expect_json_contains(invalid_setting_json, "\"ok\":false", "Phase 69B invalid setting fixture should fail before runtime");
    failures += expect_json_not_contains(invalid_setting_json, "startup-state-notice", "Phase 69B invalid setting should not emit startup notice");
    failures += expect_json_not_contains(invalid_setting_json, "execution-complete", "Phase 69B invalid setting should not complete");

    return failures;
}

/// Verifies Phase 64D source attribution metadata for memory-change rows.
///
/// @return Number of failures.
static int test_phase64d_memory_change_source_attribution(void) {
    int failures = 0;
    char direct_json[TEST_JSON_COPY_CAPACITY];
    char rmw_json[TEST_JSON_COPY_CAPACITY];
    char indirect_json[TEST_JSON_COPY_CAPACITY];
    char xchg_reg_mem_json[TEST_JSON_COPY_CAPACITY];
    char two_writes_json[TEST_JSON_COPY_CAPACITY];
    char same_value_json[TEST_JSON_COPY_CAPACITY];
    char const_failure_json[TEST_JSON_COPY_CAPACITY];
    char no_write_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(
        direct_json,
        sizeof(direct_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "a DWORD 0\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    mov a, 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        rmw_json,
        sizeof(rmw_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "a DWORD 0\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    inc a\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        indirect_json,
        sizeof(indirect_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "a DWORD 0\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, OFFSET a\n"
            "    mov DWORD PTR [eax], 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        xchg_reg_mem_json,
        sizeof(xchg_reg_mem_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "a DWORD 5\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    mov eax, 10\n"
            "    xchg eax, a\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        two_writes_json,
        sizeof(two_writes_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "a DWORD 0\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    mov a, 1\n"
            "    inc a\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        same_value_json,
        sizeof(same_value_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "a DWORD 0\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    mov a, 0\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        const_failure_json,
        sizeof(const_failure_json),
        masm32_sim_wasm_run_source_json(
            ".CONST\n"
            "a DWORD 0\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    mov a, 1\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );
    copy_source_run_json(
        no_write_json,
        sizeof(no_write_json),
        masm32_sim_wasm_run_source_json(
            ".data\n"
            "a DWORD 0\n"
            "\n"
            ".code\n"
            "main PROC\n"
            "    cmp a, 0\n"
            "    ret\n"
        "main ENDP\n"
            "END main\n"
        )
    );

    failures += expect_json_contains(direct_json, "\"ok\":true", "Phase 64D direct write fixture should execute");
    failures += expect_json_contains(direct_json, "\"sourceLine\":6,\"sourceText\":\"mov a, 1\"", "Phase 64D direct write should carry producing source line and text");

    failures += expect_json_contains(rmw_json, "\"ok\":true", "Phase 64D RMW fixture should execute");
    failures += expect_json_contains(rmw_json, "\"sourceLine\":6,\"sourceText\":\"inc a\"", "Phase 64D RMW row should point to the RMW instruction");

    failures += expect_json_contains(indirect_json, "\"ok\":true", "Phase 64D indirect write fixture should execute");
    failures += expect_json_contains(indirect_json, "\"sourceLine\":7,\"sourceText\":\"mov DWORD PTR [eax], 1\"", "Phase 64D indirect write should point to the write instruction");
    failures += expect_json_not_contains(indirect_json, "\"sourceLine\":6,\"sourceText\":\"mov eax, OFFSET a\"", "Phase 64D indirect write must not point to the address-loading line");

    failures += expect_json_contains(xchg_reg_mem_json, "\"ok\":true", "Phase 64D XCHG reg,mem fixture should execute");
    failures += expect_json_contains(xchg_reg_mem_json, "\"sourceLine\":7,\"sourceText\":\"xchg eax, a\"", "Phase 64D XCHG reg,mem write should point to the XCHG instruction");
    failures += expect_json_contains(xchg_reg_mem_json, "\"oldHex\":\"00000005h\",\"oldUnsigned\":5,\"newHex\":\"0000000Ah\",\"newUnsigned\":10", "Phase 64D XCHG reg,mem should report memory mutation");

    failures += expect_json_contains(two_writes_json, "\"ok\":true", "Phase 64D two-write fixture should execute");
    failures += expect_json_contains(two_writes_json, "\"sourceLine\":6,\"sourceText\":\"mov a, 1\"", "Phase 64D first write should carry first source line");
    failures += expect_json_contains(two_writes_json, "\"sourceLine\":7,\"sourceText\":\"inc a\"", "Phase 64D second write should carry second source line");
    failures += expect_json_fragment_order(two_writes_json, "\"sourceText\":\"mov a, 1\"", "\"sourceText\":\"inc a\"", "Phase 64D memory changes should preserve execution order");

    failures += expect_json_contains(same_value_json, "\"ok\":true", "Phase 64D same-value write fixture should execute");
    failures += expect_json_contains(same_value_json, "\"memoryChanges\":[]", "Phase 64D must preserve existing same-value memory-write row rules");

    failures += expect_json_contains(const_failure_json, "\"ok\":false", "Phase 64D failed .CONST write fixture should fail");
    failures += expect_json_contains(const_failure_json, "\"memoryChanges\":[]", "Phase 64D failed .CONST write should not create memory-change rows");
    failures += expect_json_contains(no_write_json, "\"ok\":true", "Phase 64D no-write fixture should execute");
    failures += expect_json_contains(no_write_json, "\"memoryChanges\":[]", "Phase 64D read-only CMP fixture should not create memory-change rows");

    return failures;
}

/// Identifies the stable source-run fixture family that owns one source-run test case.
typedef enum SourceRunTestFamily {
    /// Core parser/runtime source-run integration fixtures not owned by a narrower family.
    SOURCE_RUN_TEST_CORE,
    /// Source-run diagnostic and error-path fixtures.
    SOURCE_RUN_TEST_DIAGNOSTICS,
    /// Source-run settings, policy, CASEMAP, and startup-mode fixtures.
    SOURCE_RUN_TEST_SETTINGS,
    /// Source-run memory-layout, object, section, and uninitialized-storage fixtures.
    SOURCE_RUN_TEST_MEMORY_LAYOUT,
    /// Source-run label, branch, CALL, RET, entry, and watchdog fixtures.
    SOURCE_RUN_TEST_CONTROL_FLOW
} SourceRunTestFamily;

/// Describes one source-run test case and its official Phase 71B1 fixture-family owner.
typedef struct SourceRunTestCase {
    /// Stable C function name for failure and inventory output.
    const char *name;
    /// Official source-run fixture-family owner for subgroup selection.
    SourceRunTestFamily family;
    /// Test function that returns zero on success or a failure count on failure.
    int (*run)(void);
} SourceRunTestCase;

/// Maps one source-run fixture family to its public command-line selector.
///
/// @param family Fixture family identifier.
/// @return Public selector name without the `source-run-` runner prefix.
static const char *source_run_family_name(SourceRunTestFamily family) {
    switch (family) {
        case SOURCE_RUN_TEST_CORE:
            return "core";
        case SOURCE_RUN_TEST_DIAGNOSTICS:
            return "diagnostics";
        case SOURCE_RUN_TEST_SETTINGS:
            return "settings";
        case SOURCE_RUN_TEST_MEMORY_LAYOUT:
            return "memory-layout";
        case SOURCE_RUN_TEST_CONTROL_FLOW:
            return "control-flow";
        default:
            return "unknown";
    }
}

/// Parses a public source-run fixture-family selector.
///
/// @param text Selector text, such as `core` or `control-flow`.
/// @param family Receives the parsed fixture family on success.
/// @return Nonzero on success, or zero when the selector is unknown.
static int parse_source_run_family(const char *text, SourceRunTestFamily *family) {
    if (text == NULL || family == NULL) {
        return 0;
    }
    if (strcmp(text, "core") == 0) {
        *family = SOURCE_RUN_TEST_CORE;
        return 1;
    }
    if (strcmp(text, "diagnostics") == 0) {
        *family = SOURCE_RUN_TEST_DIAGNOSTICS;
        return 1;
    }
    if (strcmp(text, "settings") == 0) {
        *family = SOURCE_RUN_TEST_SETTINGS;
        return 1;
    }
    if (strcmp(text, "memory-layout") == 0) {
        *family = SOURCE_RUN_TEST_MEMORY_LAYOUT;
        return 1;
    }
    if (strcmp(text, "control-flow") == 0) {
        *family = SOURCE_RUN_TEST_CONTROL_FLOW;
        return 1;
    }
    return 0;
}

/// Reports accepted source-run fixture-family selectors for invalid harness usage.
///
/// @param executable_name Name of the current executable, as supplied by argv[0].
static void print_source_run_group_usage(const char *executable_name) {
    fprintf(stderr, "usage: %s [--group core|diagnostics|settings|memory-layout|control-flow] [--list-groups]\n", executable_name != NULL ? executable_name : "test_wasm_source_run");
}

/// Returns whether one test case should run under the requested family filter.
///
/// @param test_case Test case metadata.
/// @param has_filter Nonzero when a specific family was requested.
/// @param requested_family Requested family when has_filter is nonzero.
/// @return Nonzero when the test should run.
static int source_run_test_matches_filter(const SourceRunTestCase *test_case, int has_filter, SourceRunTestFamily requested_family) {
    if (test_case == NULL) {
        return 0;
    }
    return !has_filter || test_case->family == requested_family;
}

/// Prints the source-run fixture-family inventory as JSON for runner/static checks.
///
/// @param test_cases Test-case metadata table.
/// @param test_case_count Number of entries in test_cases.
static void print_source_run_group_inventory(const SourceRunTestCase *test_cases, size_t test_case_count) {
    size_t index = 0U;

    printf("{\"groups\":[\"core\",\"diagnostics\",\"settings\",\"memory-layout\",\"control-flow\"],\"tests\":[");
    for (index = 0U; index < test_case_count; index += 1U) {
        if (index != 0U) {
            printf(",");
        }
        printf("{\"group\":\"%s\",\"name\":\"%s\"}", source_run_family_name(test_cases[index].family), test_cases[index].name);
    }
    printf("]}\n");
}

/// Official Phase 71B1 source-run fixture-family inventory.

/// Runs source with default browser/source-run settings except for Phase 72 callDepthLimit.
///
/// @param source Source text to parse and execute.
/// @param call_depth_limit Configured Phase 72 call-depth limit.
/// @return JSON returned by the source-run API.
static const char *run_source_with_call_depth_limit(const char *source, unsigned int call_depth_limit) {
    return masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_procedure_fallthrough_entry_end_and_call_depth_settings(
        source,
        0U,
        1U,
        1U,
        1U,
        0U,
        0U,
        0U,
        1000000U,
        0U,
        1U,
        0U,
        call_depth_limit
    );
}

/// Verifies Phase 72A source-level PUSH/POP source-run semantics and memory-change rows.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72a_push_pop_source_run_stack_semantics(void) {
    int failures = 0;
    char json[TEST_JSON_COPY_CAPACITY];
    copy_source_run_json(json, sizeof(json), masm32_sim_wasm_run_source_json(
        ".data\n"
        "value DWORD 33333333h\n"
        "out DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 11111111h\n"
        "    push eax\n"
        "    push 22222222h\n"
        "    push DWORD PTR value\n"
        "    pop ecx\n"
        "    pop DWORD PTR out\n"
        "    pop ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));

    failures += expect_json_contains(json, "\"ok\":true", "Phase 72A PUSH/POP source-run program should complete");
    failures += expect_json_contains(json, "\"phaseSuffix\":\"\"", "Phase 72A PUSH/POP source-run should report suffix metadata");
    failures += expect_json_contains(json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Phase 72A PUSH/POP source-run should report current output contract");
    failures += expect_json_contains(json, "\"EAX\":{\"hex\":\"11111111h\"", "PUSH/POP source-run should leave EAX source value intact");
    failures += expect_json_contains(json, "\"EBX\":{\"hex\":\"11111111h\"", "PUSH/POP source-run should pop first pushed value last");
    failures += expect_json_contains(json, "\"ECX\":{\"hex\":\"33333333h\"", "PUSH/POP source-run should pop DWORD memory source value first");
    failures += expect_json_contains(json, "\"ESP\":{\"hex\":\"00900000h\"", "Balanced PUSH/POP source-run should restore ESP");
    failures += expect_json_contains(json, "\"symbol\":\"stack\"", "Source-level PUSH should expose stack memory-change rows");
    failures += expect_json_contains(json, "\"sourceText\":\"push eax\"", "PUSH stack memory-change row should preserve source text");
    failures += expect_json_contains(json, "\"sourceText\":\"push DWORD PTR value\"", "PUSH memory-source stack row should preserve source text");
    failures += expect_json_contains(json, "\"symbol\":\"out\"", "POP memory destination should expose destination memory-change row");
    failures += expect_json_contains(json, "\"sourceText\":\"pop DWORD PTR out\"", "POP memory destination row should preserve source text");

    return failures;
}

/// Verifies Phase 72A source-level PUSH/POP error paths preserve no-partial-mutation behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72a_push_pop_source_run_error_paths(void) {
    int failures = 0;
    char width_json[TEST_JSON_COPY_CAPACITY];
    char empty_pop_json[TEST_JSON_COPY_CAPACITY];
    char invalid_push_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(width_json, sizeof(width_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    push ax\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(width_json, "\"ok\":false", "unsupported PUSH width should fail source-run parsing");
    failures += expect_json_contains(width_json, "invalid-operand-size", "unsupported PUSH width should report structured diagnostic code");
    failures += expect_json_contains(width_json, "32-bit source-level stack transfers", "unsupported PUSH width should render explanatory diagnostic wording");

    copy_source_run_json(empty_pop_json, sizeof(empty_pop_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    pop eax\n"
        "    mov ebx, 12345678h\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(empty_pop_json, "\"ok\":false", "empty-stack POP should fail through checked memory");
    failures += expect_json_contains(empty_pop_json, "invalid-address", "empty-stack POP should report checked-memory diagnostic code");
    failures += expect_json_contains(empty_pop_json, "\"EAX\":{\"hex\":\"00000000h\"", "empty-stack POP should not mutate EAX");
    failures += expect_json_contains(empty_pop_json, "\"EBX\":{\"hex\":\"00000000h\"", "empty-stack POP should stop before following instruction");
    failures += expect_json_contains(empty_pop_json, "\"ESP\":{\"hex\":\"00900000h\"", "empty-stack POP should preserve ESP");
    failures += expect_json_contains(empty_pop_json, "\"memoryChanges\":[]", "empty-stack POP should not expose memory-change rows");

    copy_source_run_json(invalid_push_json, sizeof(invalid_push_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov esp, 0\n"
        "    mov eax, 12345678h\n"
        "    push eax\n"
        "    mov ebx, 87654321h\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_push_json, "\"ok\":false", "invalid-stack PUSH should fail through checked memory");
    failures += expect_json_contains(invalid_push_json, "invalid-address", "invalid-stack PUSH should report checked-memory diagnostic code");
    failures += expect_json_contains(invalid_push_json, "\"EAX\":{\"hex\":\"12345678h\"", "failed PUSH should preserve source register");
    failures += expect_json_contains(invalid_push_json, "\"EBX\":{\"hex\":\"00000000h\"", "failed PUSH should stop before following instruction");
    failures += expect_json_contains(invalid_push_json, "\"ESP\":{\"hex\":\"00000000h\"", "failed PUSH should preserve externally visible ESP");
    failures += expect_json_contains(invalid_push_json, "\"memoryChanges\":[]", "failed PUSH should not expose memory-change rows");

    return failures;
}

/// Verifies Phase 72A PUSH/POP participate in strict planned-access policies before mutation.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72a_push_pop_source_run_planned_access_policies(void) {
    int failures = 0;
    char push_uninitialized_json[TEST_JSON_COPY_CAPACITY];
    char stack_object_json[TEST_JSON_COPY_CAPACITY];
    char pop_object_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(push_uninitialized_json, sizeof(push_uninitialized_json), masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data?\n"
        "value DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    push DWORD PTR value\n"
        "    mov eax, 12345678h\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    ));
    failures += expect_json_contains(push_uninitialized_json, "\"ok\":false", "strict uninitialized PUSH source should stop before execution");
    failures += expect_json_contains(push_uninitialized_json, "uninitialized-read", "PUSH memory source should participate in planned-read diagnostics");
    failures += expect_json_contains(push_uninitialized_json, "\"instructionCount\":0", "strict PUSH planned-read failure should stop before stepping PUSH");
    failures += expect_json_contains(push_uninitialized_json, "\"EAX\":{\"hex\":\"00000000h\"", "strict PUSH planned-read failure should not execute following instruction");
    failures += expect_json_contains(push_uninitialized_json, "\"ESP\":{\"hex\":\"00900000h\"", "strict PUSH planned-read failure should preserve ESP");
    failures += expect_json_contains(push_uninitialized_json, "\"memoryChanges\":[]", "strict PUSH planned-read failure should not expose stack memory changes");

    copy_source_run_json(stack_object_json, sizeof(stack_object_json), masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".code\n"
        "main PROC\n"
        "    mov eax, 11223344h\n"
        "    push eax\n"
        "    pop ebx\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    ));
    failures += expect_json_contains(stack_object_json, "\"ok\":true", "strict object validation should not reject valid stack PUSH/POP transfers");
    failures += expect_json_not_contains(stack_object_json, "object-bounds-violation", "valid stack PUSH/POP should not reuse declared-object diagnostics as stack-object validation");
    failures += expect_json_contains(stack_object_json, "\"EBX\":{\"hex\":\"11223344h\"", "strict object validation should allow PUSH then POP to transfer the value");
    failures += expect_json_contains(stack_object_json, "\"ESP\":{\"hex\":\"00900000h\"", "strict object validation should allow balanced PUSH/POP to restore ESP");
    failures += expect_json_contains(stack_object_json, "\"symbol\":\"stack\"", "allowed stack PUSH should still expose a stack memory-change row");

    copy_source_run_json(pop_object_json, sizeof(pop_object_json), masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data\n"
        "slot DWORD 11223344h\n"
        "tiny BYTE 0\n"
        ".code\n"
        "main PROC\n"
        "    mov esp, OFFSET slot\n"
        "    pop DWORD PTR tiny\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT
    ));
    failures += expect_json_contains(pop_object_json, "\"ok\":false", "strict object POP destination should stop before mutation");
    failures += expect_json_contains(pop_object_json, "object-bounds-violation", "POP memory destination should participate in planned-write diagnostics");
    failures += expect_json_contains(pop_object_json, "\"instructionCount\":1", "strict POP planned-write failure should stop before stepping POP");
    failures += expect_json_contains(pop_object_json, "\"ESP\":{\"hex\":\"00500000h\"", "strict POP planned-write failure should leave ESP at original POP value");
    failures += expect_json_contains(pop_object_json, "\"EAX\":{\"hex\":\"00000000h\"", "strict POP planned-write failure should not execute following instruction");
    failures += expect_json_contains(pop_object_json, "\"memoryChanges\":[]", "strict POP planned-write failure should not expose destination memory change");
    failures += expect_json_not_contains(pop_object_json, "\"symbol\":\"tiny\"", "strict POP planned-write failure should not expose destination memory change");

    return failures;
}

/// Verifies Phase 73 LEAVE source-run semantics and diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase73_leave_source_run_semantics_and_diagnostics(void) {
    int failures = 0;
    char direct_json[TEST_JSON_COPY_CAPACITY];
    char acceptance_json[TEST_JSON_COPY_CAPACITY];
    char malformed_json[TEST_JSON_COPY_CAPACITY];
    char invalid_read_json[TEST_JSON_COPY_CAPACITY];
    char strict_read_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(direct_json, sizeof(direct_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".data\n"
        "saved DWORD 0\n"
        ".code\n"
        "main PROC\n"
        "    mov ebp, OFFSET saved\n"
        "    mov eax, 123\n"
        "    leave\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(direct_json, "\"ok\":true", "Phase 73 direct-frame LEAVE source-run program should complete");
    failures += expect_json_contains(direct_json, "\"phase\":77", "Phase 77 source-run should report numeric phase");
    failures += expect_json_contains(direct_json, "\"phaseSuffix\":\"\"", "Phase 77 source-run should report empty suffix metadata");
    failures += expect_json_contains(direct_json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Phase 77 source-run should report current output contract");
    failures += expect_json_contains(direct_json, "\"EAX\":{\"hex\":\"0000007Bh\"", "LEAVE source-run should preserve EAX acceptance value");
    failures += expect_json_contains(direct_json, "\"EBP\":{\"hex\":\"00000000h\"", "LEAVE source-run should restore EBP from saved DWORD");
    failures += expect_json_contains(direct_json, "\"ESP\":{\"hex\":\"00500004h\"", "LEAVE source-run should set ESP to old EBP plus four");
    failures += expect_json_contains(direct_json, "\"memoryChanges\":[]", "LEAVE source-run should not expose public memory-change rows");

    copy_source_run_json(acceptance_json, sizeof(acceptance_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    push ebp\n"
        "    mov ebp, esp\n"
        "    mov eax, 123\n"
        "    leave\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(acceptance_json, "\"ok\":true", "Phase 73 guide acceptance LEAVE source-run program should complete");
    failures += expect_json_contains(acceptance_json, "\"EAX\":{\"hex\":\"0000007Bh\"", "Phase 73 guide acceptance program should leave EAX at 123");
    failures += expect_json_contains(acceptance_json, "\"ESP\":{\"hex\":\"00900000h\"", "Phase 73 guide acceptance program should restore ESP to stack top");
    failures += expect_json_contains(acceptance_json, "\"EBP\":{\"hex\":\"00000000h\"", "Phase 73 guide acceptance program should restore the saved EBP value");

    copy_source_run_json(malformed_json, sizeof(malformed_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    leave eax\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(malformed_json, "\"ok\":false", "malformed LEAVE should fail source-run parsing");
    failures += expect_json_contains(malformed_json, "invalid-instruction-operands", "malformed LEAVE should report structured diagnostic code");
    failures += expect_json_contains(malformed_json, "LEAVE does not take operands", "malformed LEAVE should render exact diagnostic wording");

    copy_source_run_json(invalid_read_json, sizeof(invalid_read_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov ebp, 0\n"
        "    leave\n"
        "    mov eax, 12345678h\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_read_json, "\"ok\":false", "invalid [EBP] LEAVE should fail through checked memory");
    failures += expect_json_contains(invalid_read_json, "invalid-address", "invalid [EBP] LEAVE should report checked-memory diagnostic code");
    failures += expect_json_contains(invalid_read_json, "\"line\":5", "invalid [EBP] LEAVE diagnostic should point to the LEAVE line");
    failures += expect_json_contains(invalid_read_json, "\"EAX\":{\"hex\":\"00000000h\"", "invalid [EBP] LEAVE should stop before following MOV");
    failures += expect_json_contains(invalid_read_json, "\"EBP\":{\"hex\":\"00000000h\"", "invalid [EBP] LEAVE should leave EBP unchanged");
    failures += expect_json_contains(invalid_read_json, "\"ESP\":{\"hex\":\"00900000h\"", "invalid [EBP] LEAVE should leave ESP unchanged");
    failures += expect_json_contains(invalid_read_json, "\"memoryChanges\":[]", "invalid [EBP] LEAVE should not expose memory-change rows");

    copy_source_run_json(strict_read_json, sizeof(strict_read_json), masm32_sim_wasm_run_source_json_with_memory_validation_mode(
        ".data?\n"
        "saved DWORD ?\n"
        ".code\n"
        "main PROC\n"
        "    mov ebp, OFFSET saved\n"
        "    leave\n"
        "    mov eax, 1\n"
        "main ENDP\n"
        "END main\n",
        MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT
    ));
    failures += expect_json_contains(strict_read_json, "\"ok\":false", "strict uninitialized LEAVE read should stop before execution");
    failures += expect_json_contains(strict_read_json, "uninitialized-read", "LEAVE saved-EBP read should participate in planned-read diagnostics");
    failures += expect_json_contains(strict_read_json, "\"instructionCount\":1", "strict LEAVE planned-read failure should stop before stepping LEAVE");
    failures += expect_json_contains(strict_read_json, "\"EAX\":{\"hex\":\"00000000h\"", "strict LEAVE planned-read failure should not execute following instruction");
    failures += expect_json_contains(strict_read_json, "\"ESP\":{\"hex\":\"00900000h\"", "strict LEAVE planned-read failure should preserve ESP");
    failures += expect_json_contains(strict_read_json, "\"memoryChanges\":[]", "strict LEAVE planned-read failure should not expose memory changes");

    return failures;
}

/// Verifies current source-run metadata exposes Phase 77 and the default callDepthLimit.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase73_source_run_default_metadata_and_call_depth(void) {
    const char *json = masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n"
    );
    int failures = 0;

    failures += expect_json_contains(json, "\"phase\":77", "Phase 77 default source-run metadata should report numeric phase");
    failures += expect_json_contains(json, "\"phaseSuffix\":\"\"", "Phase 77 default source-run metadata should report suffix metadata");
    failures += expect_json_contains(json, "\"phaseName\":\"Phase 77 - PROC USES Runtime Save/Restore\"", "Phase 77 default source-run metadata should report full phase name");
    failures += expect_json_contains(json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Phase 77 default source-run metadata should report current output contract");
    failures += expect_json_contains(json, "\"callDepthLimit\":64", "default source-run JSON should expose callDepthLimit 64");
    failures += expect_json_not_contains(json, "call-trace-truncated", "default current source-run JSON should not emit call trace metadata");

    return failures;
}

/// Verifies Phase 74 RET imm16 source-run semantics and runtime diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase74_ret_imm16_source_run_semantics_and_diagnostics(void) {
    int failures = 0;
    char acceptance_json[TEST_JSON_COPY_CAPACITY];
    char invalid_immediate_json[TEST_JSON_COPY_CAPACITY];
    char invalid_token_json[TEST_JSON_COPY_CAPACITY];
    char cleanup_range_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(acceptance_json, sizeof(acceptance_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".stack 4096\n"
        ".code\n"
        "main PROC\n"
        "    push 2222h\n"
        "    push 1111h\n"
        "    call Callee\n"
        "    mov eax, esp\n"
        "    exit\n"
        "main ENDP\n"
        "Callee PROC\n"
        "    mov ebx, 5\n"
        "    ret 8\n"
        "Callee ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(acceptance_json, "\"ok\":true", "Phase 74 RET imm16 acceptance source-run program should complete");
    failures += expect_json_contains(acceptance_json, "\"phase\":77", "Phase 74 RET imm16 regression should report current numeric phase");
    failures += expect_json_contains(acceptance_json, TEST_SOURCE_RUN_OUTPUT_CONTRACT_FRAGMENT, "Phase 74 RET imm16 regression should report current output contract");
    failures += expect_json_contains(acceptance_json, "\"EBX\":{\"hex\":\"00000005h\"", "Phase 74 RET imm16 callee should leave EBX at 5");
    failures += expect_json_contains(acceptance_json, "\"EAX\":{\"hex\":\"00900000h\"", "Phase 74 RET imm16 caller should observe restored ESP in EAX");
    failures += expect_json_contains(acceptance_json, "\"ESP\":{\"hex\":\"00900000h\"", "Phase 74 RET imm16 should restore ESP to pre-argument-push value");

    copy_source_run_json(invalid_immediate_json, sizeof(invalid_immediate_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    ret 10000h\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_immediate_json, "\"ok\":false", "RET imm16 out-of-range immediate should fail parsing");
    failures += expect_json_contains(invalid_immediate_json, "\"code\":\"number-out-of-range\"", "RET imm16 out-of-range immediate should report number-out-of-range");
    failures += expect_json_contains(invalid_immediate_json, "\"line\":3", "RET imm16 invalid immediate diagnostic should preserve line");
    failures += expect_json_contains(invalid_immediate_json, "\"column\":9", "RET imm16 invalid immediate diagnostic should point at immediate column");
    failures += expect_json_contains(invalid_immediate_json, "\"spanLength\":6", "RET imm16 invalid immediate diagnostic should span immediate token");

    copy_source_run_json(invalid_token_json, sizeof(invalid_token_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    push 12345678h\n"
        "    call Callee\n"
        "    exit\n"
        "main ENDP\n"
        "Callee PROC\n"
        "    add esp, 4\n"
        "    ret 8\n"
        "Callee ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(invalid_token_json, "\"ok\":false", "RET imm16 invalid return token should fail execution");
    failures += expect_json_contains(invalid_token_json, "\"code\":\"invalid-return-address\"", "RET imm16 invalid return token should preserve runtime diagnostic code");
    failures += expect_json_contains(invalid_token_json, "\"line\":10", "RET imm16 invalid return token diagnostic should point at RET line");
    failures += expect_json_contains(invalid_token_json, "\"column\":5", "RET imm16 invalid return token diagnostic should point at RET column");

    copy_source_run_json(cleanup_range_json, sizeof(cleanup_range_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    push 1234h\n"
        "    call Callee\n"
        "    exit\n"
        "main ENDP\n"
        "Callee PROC\n"
        "    ret 65535\n"
        "Callee ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(cleanup_range_json, "\"ok\":false", "RET imm16 cleanup beyond stack boundary should fail execution");
    failures += expect_json_contains(cleanup_range_json, "\"code\":\"ret-stack-cleanup-out-of-range\"", "RET imm16 cleanup range should report dedicated runtime diagnostic code");
    failures += expect_json_contains(cleanup_range_json, "\"line\":9", "RET imm16 cleanup range diagnostic should point at RET line");
    failures += expect_json_contains(cleanup_range_json, "\"column\":5", "RET imm16 cleanup range diagnostic should point at RET column");
    failures += expect_json_contains(cleanup_range_json, "Execution stopped before changing ESP", "RET imm16 cleanup range diagnostic should describe rollback");

    return failures;
}

/// Verifies Phase 72 stops recursive direct CALL chains at callDepthLimit.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72_recursive_call_depth_limit_source_run(void) {
    const char *source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call Recur\n"
        "    exit\n"
        "main ENDP\n"
        "Recur PROC\n"
        "    call Recur\n"
        "    ret\n"
        "Recur ENDP\n"
        "END main\n";
    char first_copy[TEST_JSON_COPY_CAPACITY];
    const char *json = run_source_with_call_depth_limit(source, 1U);
    const char *second_json = NULL;
    int failures = 0;

    copy_source_run_json(first_copy, sizeof(first_copy), json);
    second_json = run_source_with_call_depth_limit(source, 1U);

    failures += expect_json_contains(first_copy, "\"ok\":false", "recursive source-run should fail when callDepthLimit is exceeded");
    failures += expect_json_contains(first_copy, "\"status\":\"execution-error\"", "recursive source-run should report execution error");
    failures += expect_json_contains(first_copy, "\"kind\":\"resource-limit-error\"", "recursive source-run should report resource-limit-error");
    failures += expect_json_contains(first_copy, "\"code\":\"call-depth-exceeded\"", "recursive source-run should report call-depth-exceeded");
    failures += expect_json_contains(first_copy, "\"currentCallDepth\":1", "recursive source-run diagnostic should include current depth");
    failures += expect_json_contains(first_copy, "\"attemptedCallDepth\":2", "recursive source-run diagnostic should include attempted depth");
    failures += expect_json_contains(first_copy, "\"callDepthLimit\":1", "recursive source-run diagnostic should include configured limit");
    failures += expect_json_contains(first_copy, "\"targetProcedure\":\"Recur\"", "recursive source-run diagnostic should include target procedure");
    failures += expect_json_contains(first_copy, "\"line\":8", "recursive source-run diagnostic should preserve source line");
    failures += expect_json_contains(first_copy, "\"column\":5", "recursive source-run diagnostic should preserve source column");
    failures += expect_json_contains(first_copy, "\"spanLength\":10", "recursive source-run diagnostic should preserve source span length");
    failures += expect_json_not_contains(first_copy, "\"code\":\"execution-complete\"", "call-depth failure should not emit execution-complete");
    failures += expect_json_not_contains(first_copy, "call-trace-truncated", "Phase 72 without trace metadata should not emit call-trace-truncated");
    failures += expect_json_contains(second_json, "\"code\":\"call-depth-exceeded\"", "second recursive source-run should fail the same way after depth reset");
    failures += expect_json_contains(second_json, "\"currentCallDepth\":1", "second recursive source-run should restart from call depth zero before first helper CALL");
    failures += expect_json_contains(second_json, "\"attemptedCallDepth\":2", "second recursive source-run should report the same attempted depth after reset");

    return failures;
}


/// Verifies Phase 72 accepts explicit source-run callDepthLimit bounds and helper RET into root RET.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72_valid_call_depth_limit_bounds_and_root_ret_source_run(void) {
    const char *root_ret_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "    ret\n"
        "main ENDP\n"
        "Helper PROC\n"
        "    mov eax, 72\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n";
    const char *exit_source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n";
    char limit_one_copy[TEST_JSON_COPY_CAPACITY];
    char limit_sixty_four_copy[TEST_JSON_COPY_CAPACITY];
    const char *limit_one_json = run_source_with_call_depth_limit(root_ret_source, 1U);
    const char *limit_sixty_four_json = NULL;
    const char *limit_max_json = NULL;
    int failures = 0;

    copy_source_run_json(limit_one_copy, sizeof(limit_one_copy), limit_one_json);
    limit_sixty_four_json = run_source_with_call_depth_limit(exit_source, 64U);
    copy_source_run_json(limit_sixty_four_copy, sizeof(limit_sixty_four_copy), limit_sixty_four_json);
    limit_max_json = run_source_with_call_depth_limit(exit_source, 4096U);

    failures += expect_json_contains(limit_one_copy, "\"ok\":true", "callDepthLimit one should allow one committed helper CALL");
    failures += expect_json_contains(limit_one_copy, "\"callDepthLimit\":1", "limit-one source-run should expose configured callDepthLimit");
    failures += expect_json_contains(limit_one_copy, "\"EAX\":{\"hex\":\"00000048h\",\"unsigned\":72}", "helper CALL followed by helper RET should return to caller before root RET completes");
    failures += expect_json_not_contains(limit_one_copy, "call-depth-exceeded", "single helper CALL at limit one should not exceed callDepthLimit");
    failures += expect_json_contains(limit_sixty_four_copy, "\"ok\":true", "explicit callDepthLimit sixty-four should be accepted");
    failures += expect_json_contains(limit_sixty_four_copy, "\"callDepthLimit\":64", "limit-sixty-four source-run should expose configured callDepthLimit");
    failures += expect_json_contains(limit_max_json, "\"ok\":true", "maximum callDepthLimit should be accepted");
    failures += expect_json_contains(limit_max_json, "\"callDepthLimit\":4096", "maximum source-run callDepthLimit should be serialized");

    return failures;
}

/// Verifies Phase 72 accepts exact-limit non-recursive CALL chains.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72_exact_call_depth_limit_source_run(void) {
    const char *source =
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call AProc\n"
        "    exit\n"
        "main ENDP\n"
        "AProc PROC\n"
        "    call BProc\n"
        "    ret\n"
        "AProc ENDP\n"
        "BProc PROC\n"
        "    mov eax, 72\n"
        "    ret\n"
        "BProc ENDP\n"
        "END main\n";
    char ok_copy[TEST_JSON_COPY_CAPACITY];
    const char *ok_json = run_source_with_call_depth_limit(source, 2U);
    const char *fail_json = NULL;
    int failures = 0;

    copy_source_run_json(ok_copy, sizeof(ok_copy), ok_json);
    fail_json = run_source_with_call_depth_limit(source, 1U);

    failures += expect_json_contains(ok_copy, "\"ok\":true", "chain at exact callDepthLimit should complete");
    failures += expect_json_contains(ok_copy, "\"EAX\":{\"hex\":\"00000048h\",\"unsigned\":72}", "chain at exact callDepthLimit should execute nested helper body");
    failures += expect_json_contains(ok_copy, "\"callDepthLimit\":2", "exact-limit success JSON should expose configured limit");
    failures += expect_json_not_contains(ok_copy, "call-depth-exceeded", "chain at exact limit should not emit call-depth diagnostic");
    failures += expect_json_contains(fail_json, "\"ok\":false", "chain beyond callDepthLimit should fail");
    failures += expect_json_contains(fail_json, "\"code\":\"call-depth-exceeded\"", "chain beyond callDepthLimit should emit call-depth diagnostic");
    failures += expect_json_contains(fail_json, "\"targetProcedure\":\"BProc\"", "chain beyond callDepthLimit should name rejected callee");

    return failures;
}

/// Verifies Phase 72 rejects invalid source-run callDepthLimit values before execution.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase72_invalid_call_depth_limit_source_run(void) {
    char low_copy[TEST_JSON_COPY_CAPACITY];
    const char *low_json = run_source_with_call_depth_limit(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n",
        0U
    );
    const char *high_json = NULL;
    int failures = 0;

    copy_source_run_json(low_copy, sizeof(low_copy), low_json);
    high_json = run_source_with_call_depth_limit(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 1\n"
        "    exit\n"
        "main ENDP\n"
        "END main\n",
        4097U
    );

    failures += expect_json_contains(low_copy, "\"ok\":false", "zero callDepthLimit should fail before execution");
    failures += expect_json_contains(low_copy, "\"status\":\"invalid-argument\"", "zero callDepthLimit should report invalid-argument status");
    failures += expect_json_contains(low_copy, "\"kind\":\"settings-error\"", "zero callDepthLimit should report settings-error kind");
    failures += expect_json_contains(low_copy, "\"code\":\"invalid-call-depth-limit\"", "zero callDepthLimit should report invalid-call-depth-limit");
    failures += expect_json_contains(low_copy, "\"setting\":\"callDepthLimit\"", "zero callDepthLimit should identify setting");
    failures += expect_json_contains(low_copy, "\"acceptedValues\":\"1..4096\"", "zero callDepthLimit should report accepted range");
    failures += expect_json_not_contains(low_copy, "\"EAX\":{\"hex\":\"00000001h\"", "zero callDepthLimit should prevent source execution");
    failures += expect_json_not_contains(low_copy, "\"code\":\"execution-complete\"", "zero callDepthLimit should not emit execution-complete");
    failures += expect_json_contains(high_json, "\"code\":\"invalid-call-depth-limit\"", "oversized callDepthLimit should report invalid-call-depth-limit");
    failures += expect_json_contains(high_json, "\"value\":4097", "oversized callDepthLimit should report rejected value");

    return failures;
}


/// Verifies Phase 75 PROC diagnostics and Phase 77 PROC USES source-run behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase77_proc_uses_source_run(void) {
    int failures = 0;
    char invalid_uses_json[TEST_JSON_COPY_CAPACITY];
    char metadata_only_json[TEST_JSON_COPY_CAPACITY];
    char runtime_success_json[TEST_JSON_COPY_CAPACITY];
    char eax_return_json[TEST_JSON_COPY_CAPACITY];
    char eax_preserve_json[TEST_JSON_COPY_CAPACITY];
    char stack_overflow_json[TEST_JSON_COPY_CAPACITY];
    char stack_underflow_json[TEST_JSON_COPY_CAPACITY];
    char selected_entry_uses_json[TEST_JSON_COPY_CAPACITY];
    char mismatch_json[TEST_JSON_COPY_CAPACITY];
    char duplicate_json[TEST_JSON_COPY_CAPACITY];

    copy_source_run_json(invalid_uses_json, sizeof(invalid_uses_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "MyProc PROC USES esp\n"
        "MyProc ENDP\n"
        "END MyProc\n"
    ));
    failures += expect_json_contains(invalid_uses_json, "\"ok\":false", "invalid PROC USES should fail source-run before execution");
    failures += expect_json_contains(invalid_uses_json, "\"status\":\"parse-error\"", "invalid PROC USES should be a parse error");
    failures += expect_json_contains(invalid_uses_json, "\"kind\":\"assembly-error\"", "invalid PROC USES should render as assembly error");
    failures += expect_json_contains(invalid_uses_json, "\"code\":\"invalid-proc-uses-register\"", "invalid PROC USES should use Phase 76 diagnostic code");
    failures += expect_json_contains(invalid_uses_json, "Invalid PROC USES register", "invalid PROC USES diagnostic should explain register rejection");
    failures += expect_json_contains(invalid_uses_json, "\"line\":2", "invalid PROC USES diagnostic should preserve line");
    failures += expect_json_contains(invalid_uses_json, "\"column\":18", "invalid PROC USES diagnostic should point at invalid register");
    failures += expect_json_contains(invalid_uses_json, "\"spanLength\":3", "invalid PROC USES diagnostic should span invalid register");
    failures += expect_json_not_contains(invalid_uses_json, "unsupported-proc-attribute", "invalid PROC USES should not use Phase 75 unsupported attribute code");
    failures += expect_json_not_contains(invalid_uses_json, "execution-complete", "invalid PROC USES should not execute");

    copy_source_run_json(metadata_only_json, sizeof(metadata_only_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC\n"
        "    mov eax, 76\n"
        "    ret\n"
        "main ENDP\n"
        "Unused PROC USES eax ebx\n"
        "    mov ebx, 1\n"
        "    ret\n"
        "Unused ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(metadata_only_json, "\"ok\":true", "unentered PROC USES metadata should not fail source-run");
    failures += expect_json_contains(metadata_only_json, "\"EAX\":{\"hex\":\"0000004Ch\",\"unsigned\":76}", "unentered PROC USES fixture should execute selected entry normally");
    failures += expect_json_not_contains(metadata_only_json, "unsupported-proc-attribute", "valid PROC USES metadata should not be a parse error");
    failures += expect_json_not_contains(metadata_only_json, "unsupported-proc-uses-runtime", "unentered PROC USES should not trigger runtime guard");

    copy_source_run_json(runtime_success_json, sizeof(runtime_success_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov ebx, 1111h\n"
        "    call Helper\n"
        "    mov eax, ebx\n"
        "    exit\n"
        "main ENDP\n"
        "Helper PROC USES ebx\n"
        "    mov ebx, 2222h\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(runtime_success_json, "\"ok\":true", "direct CALL into PROC USES should execute in Phase 77");
    failures += expect_json_contains(runtime_success_json, "\"status\":\"ok\"", "direct CALL into PROC USES should complete successfully");
    failures += expect_json_contains(runtime_success_json, "\"EAX\":{\"hex\":\"00001111h\",\"unsigned\":4369}", "caller should observe EBX restored after PROC USES");
    failures += expect_json_contains(runtime_success_json, "\"EBX\":{\"hex\":\"00001111h\",\"unsigned\":4369}", "listed EBX should be restored after helper RET");
    failures += expect_json_contains(runtime_success_json, "\"ESP\":{\"hex\":\"00900000h\"", "PROC USES CALL/RET should leave ESP balanced");
    failures += expect_json_not_contains(runtime_success_json, "unsupported-proc-uses-runtime", "supported direct CALL should not emit pre-77 runtime deferral");

    copy_source_run_json(eax_return_json, sizeof(eax_return_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 5\n"
        "    call Helper\n"
        "    exit\n"
        "main ENDP\n"
        "Helper PROC USES ebx\n"
        "    mov eax, 99\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(eax_return_json, "\"ok\":true", "unlisted EAX helper return source should complete");
    failures += expect_json_contains(eax_return_json, "\"EAX\":{\"hex\":\"00000063h\",\"unsigned\":99}", "unlisted EAX should remain available as helper return value");

    copy_source_run_json(eax_preserve_json, sizeof(eax_preserve_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, 1234h\n"
        "    call Helper\n"
        "    exit\n"
        "main ENDP\n"
        "Helper PROC USES eax\n"
        "    mov eax, 9999h\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(eax_preserve_json, "\"ok\":true", "listed EAX helper return source should complete");
    failures += expect_json_contains(eax_preserve_json, "\"EAX\":{\"hex\":\"00001234h\",\"unsigned\":4660}", "listed EAX should be restored by PROC USES");

    copy_source_run_json(stack_overflow_json, sizeof(stack_overflow_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    mov esp, 008F0008h\n"
        "    call Helper\n"
        "    exit\n"
        "main ENDP\n"
        "Helper PROC USES ebx esi\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(stack_overflow_json, "\"ok\":false", "USES save with insufficient stack should fail source-run");
    failures += expect_json_contains(stack_overflow_json, "\"status\":\"execution-error\"", "USES save overflow should be an execution error");
    failures += expect_json_contains(stack_overflow_json, "\"code\":\"stack-overflow\"", "USES save overflow should use stack-overflow code");
    failures += expect_json_contains(stack_overflow_json, "\"procedure\":\"Helper\"", "USES save overflow diagnostic should identify target procedure");
    failures += expect_json_contains(stack_overflow_json, "Automatic PROC USES register save", "USES save overflow diagnostic should explain automatic save failure");

    copy_source_run_json(stack_underflow_json, sizeof(stack_underflow_json), masm32_sim_wasm_run_source_json(
        "INCLUDE Irvine32.inc\n"
        ".code\n"
        "main PROC\n"
        "    call Helper\n"
        "    exit\n"
        "main ENDP\n"
        "Helper PROC USES ebx\n"
        "    mov ebx, 2222h\n"
        "    mov esp, 00900000h\n"
        "    ret\n"
        "Helper ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(stack_underflow_json, "\"ok\":false", "USES restore with missing saved slot should fail source-run");
    failures += expect_json_contains(stack_underflow_json, "\"status\":\"execution-error\"", "USES restore underflow should be an execution error");
    failures += expect_json_contains(stack_underflow_json, "\"code\":\"stack-underflow\"", "USES restore underflow should use stack-underflow code");
    failures += expect_json_contains(stack_underflow_json, "\"procedure\":\"Helper\"", "USES restore underflow diagnostic should identify active procedure");
    failures += expect_json_contains(stack_underflow_json, "Automatic PROC USES register restore", "USES restore underflow diagnostic should explain automatic restore failure");
    failures += expect_json_contains(stack_underflow_json, "\"EBX\":{\"hex\":\"00002222h\"", "USES restore underflow should not partially restore EBX");

    copy_source_run_json(selected_entry_uses_json, sizeof(selected_entry_uses_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "main PROC USES eax\n"
        "    mov eax, 1\n"
        "    ret\n"
        "main ENDP\n"
        "END main\n"
    ));
    failures += expect_json_contains(selected_entry_uses_json, "\"ok\":false", "selected-entry PROC USES should still require direct CALL entry");
    failures += expect_json_contains(selected_entry_uses_json, "\"status\":\"execution-error\"", "selected-entry PROC USES should be a runtime error");
    failures += expect_json_contains(selected_entry_uses_json, "\"code\":\"unsupported-proc-uses-runtime\"", "selected-entry PROC USES should use stable runtime diagnostic");
    failures += expect_json_contains(selected_entry_uses_json, "direct CALL entry", "selected-entry PROC USES diagnostic should explain supported entry mode");
    failures += expect_json_contains(selected_entry_uses_json, "\"line\":3", "selected-entry PROC USES runtime diagnostic should point at first lowered instruction");
    failures += expect_json_contains(selected_entry_uses_json, "\"column\":5", "selected-entry PROC USES runtime diagnostic should preserve first instruction column");
    failures += expect_json_not_contains(selected_entry_uses_json, "\"EAX\":{\"hex\":\"00000001h", "selected-entry PROC USES should stop before body mutates EAX");

    copy_source_run_json(mismatch_json, sizeof(mismatch_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "MyProc PROC\n"
        "    mov eax, 1\n"
        "Other ENDP\n"
        "END MyProc\n"
    ));
    failures += expect_json_contains(mismatch_json, "\"code\":\"proc-end-mismatch\"", "mismatched ENDP should use proc-end-mismatch");
    failures += expect_json_contains(mismatch_json, "\"line\":4", "mismatched ENDP diagnostic should preserve line");
    failures += expect_json_contains(mismatch_json, "\"column\":1", "mismatched ENDP diagnostic should point at ENDP name");
    failures += expect_json_contains(mismatch_json, "\"spanLength\":5", "mismatched ENDP diagnostic should span mismatched name");

    copy_source_run_json(duplicate_json, sizeof(duplicate_json), masm32_sim_wasm_run_source_json(
        ".code\n"
        "Helper PROC\n"
        "Helper ENDP\n"
        "Helper PROC\n"
        "Helper ENDP\n"
        "END Helper\n"
    ));
    failures += expect_json_contains(duplicate_json, "\"code\":\"duplicate-procedure\"", "duplicate procedure should use duplicate-procedure");
    failures += expect_json_contains(duplicate_json, "first defined at line 2, column 1", "duplicate procedure diagnostic should carry prior location text");

    return failures;
}

static const SourceRunTestCase SOURCE_RUN_TEST_CASES[] = {
    {"test_minimal_source_runs_to_eax_42", SOURCE_RUN_TEST_CORE, test_minimal_source_runs_to_eax_42},
    {"test_zero_instruction_program_reports_code_end_falloff", SOURCE_RUN_TEST_CORE, test_zero_instruction_program_reports_code_end_falloff},
    {"test_phase71c_code_stream_falloff_source_run_behavior", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase71c_code_stream_falloff_source_run_behavior},
    {"test_parse_error_returns_structured_message", SOURCE_RUN_TEST_DIAGNOSTICS, test_parse_error_returns_structured_message},
    {"test_phase77_proc_uses_source_run", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase77_proc_uses_source_run},
    {"test_source_run_invalid_hex_reports_specific_lexer_diagnostic", SOURCE_RUN_TEST_DIAGNOSTICS, test_source_run_invalid_hex_reports_specific_lexer_diagnostic},
    {"test_source_run_unterminated_string_reports_specific_lexer_diagnostic", SOURCE_RUN_TEST_DIAGNOSTICS, test_source_run_unterminated_string_reports_specific_lexer_diagnostic},
    {"test_source_run_other_lexer_diagnostics_are_specific", SOURCE_RUN_TEST_DIAGNOSTICS, test_source_run_other_lexer_diagnostics_are_specific},
    {"test_narrow_register_immediate_overflow_returns_parse_error", SOURCE_RUN_TEST_DIAGNOSTICS, test_narrow_register_immediate_overflow_returns_parse_error},
    {"test_negative_immediate_source_run_succeeds", SOURCE_RUN_TEST_CORE, test_negative_immediate_source_run_succeeds},
    {"test_negative_immediate_overflow_returns_parse_error", SOURCE_RUN_TEST_DIAGNOSTICS, test_negative_immediate_overflow_returns_parse_error},
    {"test_register_indirect_source_run_succeeds", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_register_indirect_source_run_succeeds},
    {"test_phase24_eax_base_acceptance_program", SOURCE_RUN_TEST_CORE, test_phase24_eax_base_acceptance_program},
    {"test_all_gpr_register_indirect_source_run_succeeds", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_all_gpr_register_indirect_source_run_succeeds},
    {"test_type_operator_source_run_acceptance_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_type_operator_source_run_acceptance_program},
    {"test_type_operator_source_run_element_sizes", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_type_operator_source_run_element_sizes},
    {"test_lengthof_operator_source_run_acceptance_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_lengthof_operator_source_run_acceptance_program},
    {"test_lengthof_operator_source_run_element_counts", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_lengthof_operator_source_run_element_counts},
    {"test_sizeof_operator_source_run_acceptance_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_sizeof_operator_source_run_acceptance_program},
    {"test_sizeof_operator_source_run_byte_sizes", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_sizeof_operator_source_run_byte_sizes},
    {"test_sizeof_operator_source_run_rejects_expression_tail", SOURCE_RUN_TEST_DIAGNOSTICS, test_sizeof_operator_source_run_rejects_expression_tail},
    {"test_character_literal_source_run_rejects_width_overflow", SOURCE_RUN_TEST_DIAGNOSTICS, test_character_literal_source_run_rejects_width_overflow},
    {"test_character_literal_source_run_accepts_narrower_packed_immediates", SOURCE_RUN_TEST_CORE, test_character_literal_source_run_accepts_narrower_packed_immediates},
    {"test_lengthof_operator_source_run_rejects_expression_tail", SOURCE_RUN_TEST_DIAGNOSTICS, test_lengthof_operator_source_run_rejects_expression_tail},
    {"test_type_operator_source_run_rejects_expression_tail", SOURCE_RUN_TEST_DIAGNOSTICS, test_type_operator_source_run_rejects_expression_tail},
    {"test_scaled_index_source_run_returns_unsupported_feature", SOURCE_RUN_TEST_DIAGNOSTICS, test_scaled_index_source_run_returns_unsupported_feature},
    {"test_constant_symbol_offset_source_run_succeeds", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_constant_symbol_offset_source_run_succeeds},
    {"test_unaligned_constant_symbol_offset_reports_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_unaligned_constant_symbol_offset_reports_warning},
    {"test_negative_symbol_offset_inside_data_image_succeeds", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_negative_symbol_offset_inside_data_image_succeeds},
    {"test_offset_zero_bracketed_symbol_operands_execute", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_offset_zero_bracketed_symbol_operands_execute},
    {"test_constant_symbol_offset_crossing_section_image_is_not_parse_error", SOURCE_RUN_TEST_DIAGNOSTICS, test_constant_symbol_offset_crossing_section_image_is_not_parse_error},
    {"test_textbook_unsupported_features_return_unsupported_feature_messages", SOURCE_RUN_TEST_DIAGNOSTICS, test_textbook_unsupported_features_return_unsupported_feature_messages},
    {"test_multi_diagnostic_unsupported_feature_source_run_reports_all", SOURCE_RUN_TEST_DIAGNOSTICS, test_multi_diagnostic_unsupported_feature_source_run_reports_all},
    {"test_signed_integer_source_run_acceptance_program", SOURCE_RUN_TEST_CORE, test_signed_integer_source_run_acceptance_program},
    {"test_unary_plus_source_run_acceptance_program", SOURCE_RUN_TEST_CORE, test_unary_plus_source_run_acceptance_program},
    {"test_signed_integer_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_signed_integer_source_run_error_paths},
    {"test_signed_ptr_alias_source_run_acceptance_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_signed_ptr_alias_source_run_acceptance_program},
    {"test_signed_ptr_alias_source_run_write_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_signed_ptr_alias_source_run_write_program},
    {"test_signed_ptr_alias_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_signed_ptr_alias_source_run_error_paths},
    {"test_extension_source_run_acceptance_program", SOURCE_RUN_TEST_CORE, test_extension_source_run_acceptance_program},
    {"test_accumulator_extension_source_run_program", SOURCE_RUN_TEST_CORE, test_accumulator_extension_source_run_program},
    {"test_extension_source_run_edge_cases", SOURCE_RUN_TEST_CORE, test_extension_source_run_edge_cases},
    {"test_extension_register_indirect_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_extension_register_indirect_memory_source_run_program},
    {"test_plain_mov_from_signed_memory_rejects_implicit_widening", SOURCE_RUN_TEST_DIAGNOSTICS, test_plain_mov_from_signed_memory_rejects_implicit_widening},
    {"test_extension_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_extension_source_run_error_paths},
    {"test_phase20_source_run_acceptance_program", SOURCE_RUN_TEST_CORE, test_phase20_source_run_acceptance_program},
    {"test_phase20_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase20_memory_source_run_program},
    {"test_phase20_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase20_source_run_error_paths},
    {"test_phase57n_nop_source_run_hardening", SOURCE_RUN_TEST_CORE, test_phase57n_nop_source_run_hardening},
    {"test_phase57o_nop_encoding_operand_source_run", SOURCE_RUN_TEST_CORE, test_phase57o_nop_encoding_operand_source_run},
    {"test_phase57o_nop_source_run_rejections", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57o_nop_source_run_rejections},
    {"test_phase21_source_run_acceptance_program", SOURCE_RUN_TEST_CORE, test_phase21_source_run_acceptance_program},
    {"test_phase21_memory_and_borrow_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase21_memory_and_borrow_source_run_program},
    {"test_phase21_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase21_source_run_error_paths},
    {"test_phase22_source_run_acceptance_program", SOURCE_RUN_TEST_CORE, test_phase22_source_run_acceptance_program},
    {"test_phase22_memory_immediate_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase22_memory_immediate_source_run_program},
    {"test_phase22_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase22_source_run_error_paths},
    {"test_phase25_register_supplied_memory_width_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase25_register_supplied_memory_width_source_run_program},
    {"test_phase25_register_supplied_source_memory_width_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase25_register_supplied_source_memory_width_source_run_program},
    {"test_phase25_browser_observed_regressions", SOURCE_RUN_TEST_CORE, test_phase25_browser_observed_regressions},
    {"test_phase25_explicit_ptr_symbol_register_override_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase25_explicit_ptr_symbol_register_override_source_run_program},
    {"test_phase25_ambiguous_memory_width_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase25_ambiguous_memory_width_source_run_error_paths},
    {"test_phase26_header_source_run_acceptance_program", SOURCE_RUN_TEST_CORE, test_phase26_header_source_run_acceptance_program},
    {"test_phase26_header_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase26_header_source_run_error_paths},
    {"test_phase57p_host_include_path_source_run_diagnostics", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57p_host_include_path_source_run_diagnostics},
    {"test_phase57q_includelib_source_run_diagnostics", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57q_includelib_source_run_diagnostics},
    {"test_phase57r_invoke_addr_external_routine_source_run_diagnostics", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57r_invoke_addr_external_routine_source_run_diagnostics},
    {"test_phase57s_high_level_flow_source_run_diagnostics", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57s_high_level_flow_source_run_diagnostics},
    {"test_phase57t_playground_diagnostic_recovery_smoke_fixtures", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57t_playground_diagnostic_recovery_smoke_fixtures},
    {"test_phase58_label_source_run_behavior", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase58_label_source_run_behavior},
    {"test_phase28_additional_data_sections_source_run_programs", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase28_additional_data_sections_source_run_programs},
    {"test_phase30_dup_initializer_list_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase30_dup_initializer_list_source_run_program},
    {"test_phase30_dup_repeat_count_diagnostic_source_run_program", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase30_dup_repeat_count_diagnostic_source_run_program},
    {"test_phase30_large_dup_count_capacity_diagnostic_source_run_program", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase30_large_dup_count_capacity_diagnostic_source_run_program},
    {"test_phase61d_token_capacity_diagnostic_source_run_program", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase61d_token_capacity_diagnostic_source_run_program},
    {"test_phase61d_source_text_capacity_diagnostic_source_run_program", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase61d_source_text_capacity_diagnostic_source_run_program},
    {"test_phase61d_code_label_capacity_diagnostic_source_run_program", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase61d_code_label_capacity_diagnostic_source_run_program},
    {"test_phase32_fixed_layout_source_run_regression_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase32_fixed_layout_source_run_regression_program},
    {"test_phase33_automatic_layout_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase33_automatic_layout_source_run_program},
    {"test_phase33_automatic_layout_const_write_rejected", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase33_automatic_layout_const_write_rejected},
    {"test_phase33_automatic_layout_invalid_access_does_not_grow", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase33_automatic_layout_invalid_access_does_not_grow},
    {"test_phase33_automatic_layout_resource_limit_json", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase33_automatic_layout_resource_limit_json},
    {"test_phase34_automatic_layout_uses_stack_size_metadata", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase34_automatic_layout_uses_stack_size_metadata},
    {"test_phase34_automatic_layout_stack_without_operand_uses_default", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase34_automatic_layout_stack_without_operand_uses_default},
    {"test_phase34_automatic_layout_stack_expression_metadata", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase34_automatic_layout_stack_expression_metadata},
    {"test_phase34_automatic_layout_excessive_stack_size_json", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase34_automatic_layout_excessive_stack_size_json},
    {"test_phase34_automatic_layout_uses_configured_heap_size", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase34_automatic_layout_uses_configured_heap_size},
    {"test_phase34_automatic_layout_excessive_heap_size_json", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase34_automatic_layout_excessive_heap_size_json},
    {"test_phase35_seeded_randomized_layout_offset_program_succeeds", SOURCE_RUN_TEST_SETTINGS, test_phase35_seeded_randomized_layout_offset_program_succeeds},
    {"test_phase35_seeded_randomized_layout_hardcoded_data_address_fails", SOURCE_RUN_TEST_SETTINGS, test_phase35_seeded_randomized_layout_hardcoded_data_address_fails},
    {"test_phase35_seeded_randomized_layout_const_write_stays_read_only", SOURCE_RUN_TEST_SETTINGS, test_phase35_seeded_randomized_layout_const_write_stays_read_only},
    {"test_phase35_seeded_randomized_layout_data_question_writable", SOURCE_RUN_TEST_SETTINGS, test_phase35_seeded_randomized_layout_data_question_writable},
    {"test_phase35_randomized_layout_unavailable_source_run_json", SOURCE_RUN_TEST_SETTINGS, test_phase35_randomized_layout_unavailable_source_run_json},
    {"test_phase35_fresh_randomized_layout_returns_seed_metadata", SOURCE_RUN_TEST_SETTINGS, test_phase35_fresh_randomized_layout_returns_seed_metadata},
    {"test_phase37_default_region_only_has_no_object_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase37_default_region_only_has_no_object_warning},
    {"test_phase37_allocated_object_warning_mode_warns_and_continues", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase37_allocated_object_warning_mode_warns_and_continues},
    {"test_phase37_access_into_another_object_has_no_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase37_access_into_another_object_has_no_warning},
    {"test_phase37_partial_overlap_starting_inside_object_warns", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase37_partial_overlap_starting_inside_object_warns},
    {"test_phase37_write_to_region_gap_warns_and_continues", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase37_write_to_region_gap_warns_and_continues},
    {"test_phase37_invalid_address_error_precedes_object_warning", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase37_invalid_address_error_precedes_object_warning},
    {"test_phase37_spanning_adjacent_objects_warns", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase37_spanning_adjacent_objects_warns},
    {"test_phase37_unaligned_inside_object_has_no_object_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase37_unaligned_inside_object_has_no_object_warning},
    {"test_phase37_const_permission_error_precedes_object_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase37_const_permission_error_precedes_object_warning},
    {"test_phase38_strict_gap_access_fails", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase38_strict_gap_access_fails},
    {"test_phase38_strict_access_into_another_object_succeeds", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase38_strict_access_into_another_object_succeeds},
    {"test_phase38_strict_partial_overlap_starting_inside_object_fails", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase38_strict_partial_overlap_starting_inside_object_fails},
    {"test_phase38_strict_spanning_adjacent_objects_fails", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase38_strict_spanning_adjacent_objects_fails},
    {"test_phase38_strict_unaligned_inside_object_succeeds", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase38_strict_unaligned_inside_object_succeeds},
    {"test_phase38_strict_invalid_address_precedes_object_violation", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase38_strict_invalid_address_precedes_object_violation},
    {"test_phase38_strict_const_permission_error_precedes_object_violation", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase38_strict_const_permission_error_precedes_object_violation},
    {"test_phase39_explicit_region_only_uninitialized_read_has_no_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase39_explicit_region_only_uninitialized_read_has_no_warning},
    {"test_phase39_initial_uninitialized_origin_metadata", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase39_initial_uninitialized_origin_metadata},
    {"test_phase39_partial_and_full_writes_mark_initialized_bytes", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase39_partial_and_full_writes_mark_initialized_bytes},
    {"test_phase39_failed_writes_do_not_mark_initialized", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase39_failed_writes_do_not_mark_initialized},
    {"test_phase40_uninitialized_read_warning_mode_warns_and_continues", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase40_uninitialized_read_warning_mode_warns_and_continues},
    {"test_phase40_uninitialized_read_strict_mode_stops", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase40_uninitialized_read_strict_mode_stops},
    {"test_phase40_full_write_suppresses_uninitialized_read_diagnostic", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase40_full_write_suppresses_uninitialized_read_diagnostic},
    {"test_phase40_partial_write_then_multibyte_read_warns", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase40_partial_write_then_multibyte_read_warns},
    {"test_phase40_mixed_initializer_multibyte_read_warns_for_whole_range", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase40_mixed_initializer_multibyte_read_warns_for_whole_range},
    {"test_phase40_data_question_section_warns", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase40_data_question_section_warns},
    {"test_phase40_repeated_uninitialized_reads_emit_distinct_warnings", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase40_repeated_uninitialized_reads_emit_distinct_warnings},
    {"test_phase40_rmw_warning_then_writeback_marks_initialized", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase40_rmw_warning_then_writeback_marks_initialized},
    {"test_phase40_rmw_strict_stops_before_writeback", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase40_rmw_strict_stops_before_writeback},
    {"test_phase64a_rmw_planned_read_policy_for_existing_families", SOURCE_RUN_TEST_SETTINGS, test_phase64a_rmw_planned_read_policy_for_existing_families},
    {"test_phase64a_memory_source_planned_read_regression", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase64a_memory_source_planned_read_regression},
    {"test_phase40_invalid_address_precedes_uninitialized_read", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase40_invalid_address_precedes_uninitialized_read},
    {"test_phase40_object_strict_regression_precedes_uninitialized_read_feature", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase40_object_strict_regression_precedes_uninitialized_read_feature},
    {"test_phase39_register_copy_marks_destination_initialized", SOURCE_RUN_TEST_CORE, test_phase39_register_copy_marks_destination_initialized},
    {"test_phase35a_casemap_all_source_run_programs", SOURCE_RUN_TEST_SETTINGS, test_phase35a_casemap_all_source_run_programs},
    {"test_phase35a_casemap_none_source_run_programs", SOURCE_RUN_TEST_SETTINGS, test_phase35a_casemap_none_source_run_programs},
    {"test_phase35a_casemap_equate_source_run_programs", SOURCE_RUN_TEST_SETTINGS, test_phase35a_casemap_equate_source_run_programs},
    {"test_phase35a_casemap_diagnostic_source_run_programs", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase35a_casemap_diagnostic_source_run_programs},
    {"test_phase41_irvine32_virtual_include_metadata_source_run", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase41_irvine32_virtual_include_metadata_source_run},
    {"test_phase41_macros_include_does_not_populate_irvine32_registry", SOURCE_RUN_TEST_CORE, test_phase41_macros_include_does_not_populate_irvine32_registry},
    {"test_phase41_irvine32_unsupported_routine_source_run_diagnostic", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase41_irvine32_unsupported_routine_source_run_diagnostic},
    {"test_phase42_irvine32_exit_terminator_source_run", SOURCE_RUN_TEST_CORE, test_phase42_irvine32_exit_terminator_source_run},
    {"test_phase42_irvine32_exit_terminator_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase42_irvine32_exit_terminator_error_paths},
    {"test_phase43_inc_dec_register_source_run_program", SOURCE_RUN_TEST_CORE, test_phase43_inc_dec_register_source_run_program},
    {"test_phase43_inc_dec_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase43_inc_dec_memory_source_run_program},
    {"test_phase43_inc_dec_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase43_inc_dec_source_run_error_paths},
    {"test_phase44_logical_binary_source_run_program", SOURCE_RUN_TEST_CORE, test_phase44_logical_binary_source_run_program},
    {"test_phase44_logical_binary_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase44_logical_binary_memory_source_run_program},
    {"test_phase44_logical_binary_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase44_logical_binary_source_run_error_paths},
    {"test_phase45_not_source_run_program", SOURCE_RUN_TEST_CORE, test_phase45_not_source_run_program},
    {"test_phase45_not_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase45_not_memory_source_run_program},
    {"test_phase45_not_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase45_not_source_run_error_paths},
    {"test_phase46_shift_left_source_run_program", SOURCE_RUN_TEST_CORE, test_phase46_shift_left_source_run_program},
    {"test_phase46_shift_left_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase46_shift_left_memory_source_run_program},
    {"test_phase46_shift_left_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase46_shift_left_source_run_error_paths},
    {"test_phase47_shr_source_run_program", SOURCE_RUN_TEST_CORE, test_phase47_shr_source_run_program},
    {"test_phase47_shr_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase47_shr_memory_source_run_program},
    {"test_phase47_shr_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase47_shr_source_run_error_paths},
    {"test_phase48_sar_source_run_program", SOURCE_RUN_TEST_CORE, test_phase48_sar_source_run_program},
    {"test_phase48_sar_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase48_sar_memory_source_run_program},
    {"test_phase48_sar_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase48_sar_source_run_error_paths},
    {"test_phase49_rol_source_run_program", SOURCE_RUN_TEST_CORE, test_phase49_rol_source_run_program},
    {"test_phase49_rol_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase49_rol_memory_source_run_program},
    {"test_phase49_rol_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase49_rol_source_run_error_paths},
    {"test_phase50_ror_source_run_program", SOURCE_RUN_TEST_CORE, test_phase50_ror_source_run_program},
    {"test_phase50_ror_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase50_ror_memory_source_run_program},
    {"test_phase50_ror_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase50_ror_source_run_error_paths},
    {"test_phase52_lea_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase52_lea_source_run_program},
    {"test_phase52_lea_no_memory_diagnostics_source_run", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase52_lea_no_memory_diagnostics_source_run},
    {"test_phase52_lea_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase52_lea_source_run_error_paths},
    {"test_phase57corr2_compact_negative_displacement_write_source_run", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57corr2_compact_negative_displacement_write_source_run},
    {"test_phase57corr2_compact_negative_displacement_read_source_run", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57corr2_compact_negative_displacement_read_source_run},
    {"test_phase57corr2_compact_negative_displacement_lea_source_run", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57corr2_compact_negative_displacement_lea_source_run},
    {"test_phase57corr2_compact_negative_displacement_advanced_rejection_source_run", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57corr2_compact_negative_displacement_advanced_rejection_source_run},
    {"test_phase53_mul_source_run_program", SOURCE_RUN_TEST_CORE, test_phase53_mul_source_run_program},
    {"test_phase53_mul_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53_mul_memory_source_run_program},
    {"test_phase53_mul_uninitialized_memory_source_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53_mul_uninitialized_memory_source_warning},
    {"test_phase53_mul_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53_mul_source_run_error_paths},
    {"test_phase54_imul_source_run_program", SOURCE_RUN_TEST_CORE, test_phase54_imul_source_run_program},
    {"test_phase54_imul_memory_source_run_program", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase54_imul_memory_source_run_program},
    {"test_phase54_imul_uninitialized_memory_source_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase54_imul_uninitialized_memory_source_warning},
    {"test_phase54_imul_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase54_imul_source_run_error_paths},
    {"test_phase55_imul_source_run_programs", SOURCE_RUN_TEST_CORE, test_phase55_imul_source_run_programs},
    {"test_phase55_imul_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase55_imul_source_run_error_paths},
    {"test_phase56_div_source_run_programs", SOURCE_RUN_TEST_CORE, test_phase56_div_source_run_programs},
    {"test_phase56_div_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase56_div_source_run_error_paths},
    {"test_phase57_idiv_source_run_programs", SOURCE_RUN_TEST_CORE, test_phase57_idiv_source_run_programs},
    {"test_phase57_idiv_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57_idiv_source_run_error_paths},
    {"test_phase57l_code_memory_access_diagnostics_source_run", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57l_code_memory_access_diagnostics_source_run},
    {"test_phase57m_segment_symbol_source_run", SOURCE_RUN_TEST_CORE, test_phase57m_segment_symbol_source_run},
    {"test_phase57corr1_const_cross_region_write_diagnostic_context", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57corr1_const_cross_region_write_diagnostic_context},
    {"test_phase57corr1_direct_const_write_still_permission_denied", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57corr1_direct_const_write_still_permission_denied},
    {"test_phase57corr1_const_cross_region_read_remains_region_failure", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57corr1_const_cross_region_read_remains_region_failure},
    {"test_non_protected_cross_region_write_remains_ordinary_region_failure", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_non_protected_cross_region_write_remains_ordinary_region_failure},
    {"test_phase53a_mul_symbol_offset_crossing_object_is_runtime_controlled", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53a_mul_symbol_offset_crossing_object_is_runtime_controlled},
    {"test_phase53a_default_object_spanning_read_has_no_object_diagnostic", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53a_default_object_spanning_read_has_no_object_diagnostic},
    {"test_phase53a_object_spanning_read_warning_mode_continues", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53a_object_spanning_read_warning_mode_continues},
    {"test_phase53a_object_spanning_read_strict_mode_stops_before_mutation", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53a_object_spanning_read_strict_mode_stops_before_mutation},
    {"test_phase53a_invalid_region_still_precedes_object_validation", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53a_invalid_region_still_precedes_object_validation},
    {"test_phase53a_const_write_precedes_object_validation", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53a_const_write_precedes_object_validation},
    {"test_phase53a_uninitialized_read_modes_on_symbol_offset", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53a_uninitialized_read_modes_on_symbol_offset},
    {"test_phase53b_default_section_validation_off", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53b_default_section_validation_off},
    {"test_phase53b_section_image_warning_mode_continues", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53b_section_image_warning_mode_continues},
    {"test_phase53b_section_image_strict_mode_stops_before_mutation", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53b_section_image_strict_mode_stops_before_mutation},
    {"test_phase53b_data_section_capacity_warning_mode_continues", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53b_data_section_capacity_warning_mode_continues},
    {"test_phase53b_data_section_capacity_strict_mode_stops_before_mutation", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53b_data_section_capacity_strict_mode_stops_before_mutation},
    {"test_phase53b_section_capacity_warning_mode_continues", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53b_section_capacity_warning_mode_continues},
    {"test_phase53b_section_capacity_strict_mode_stops_before_mutation", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53b_section_capacity_strict_mode_stops_before_mutation},
    {"test_phase53b_invalid_region_precedes_section_validation", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53b_invalid_region_precedes_section_validation},
    {"test_phase53b_const_write_precedes_section_validation", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53b_const_write_precedes_section_validation},
    {"test_phase53b_section_image_warning_precedes_object_warning", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53b_section_image_warning_precedes_object_warning},
    {"test_phase53c_default_uninitialized_read_warns_and_continues", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53c_default_uninitialized_read_warns_and_continues},
    {"test_phase53c_uninitialized_read_explicit_off_preserves_silent_behavior", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53c_uninitialized_read_explicit_off_preserves_silent_behavior},
    {"test_phase53c_default_indirect_uninitialized_read_warns_for_tracked_overlap", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53c_default_indirect_uninitialized_read_warns_for_tracked_overlap},
    {"test_phase53c_default_uninitialized_mul_symbol_offset_warns", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase53c_default_uninitialized_mul_symbol_offset_warns},
    {"test_phase53c_default_undefined_flag_use_warns_and_continues", SOURCE_RUN_TEST_SETTINGS, test_phase53c_default_undefined_flag_use_warns_and_continues},
    {"test_phase53c_undefined_flag_use_explicit_off_preserves_silent_behavior", SOURCE_RUN_TEST_SETTINGS, test_phase53c_undefined_flag_use_explicit_off_preserves_silent_behavior},
    {"test_phase53d_default_compatibility_notices_continue_execution", SOURCE_RUN_TEST_SETTINGS, test_phase53d_default_compatibility_notices_continue_execution},
    {"test_phase53d_active_semantics_do_not_emit_compatibility_notices", SOURCE_RUN_TEST_SETTINGS, test_phase53d_active_semantics_do_not_emit_compatibility_notices},
    {"test_phase53d_notice_plus_error_still_blocks_execution", SOURCE_RUN_TEST_SETTINGS, test_phase53d_notice_plus_error_still_blocks_execution},
    {"test_phase53e_ui_settings_route_to_existing_policies", SOURCE_RUN_TEST_SETTINGS, test_phase53e_ui_settings_route_to_existing_policies},
    {"test_phase57d_ui_policy_families_remain_independent", SOURCE_RUN_TEST_SETTINGS, test_phase57d_ui_policy_families_remain_independent},
    {"test_phase57e_default_startup_state_notice_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase57e_default_startup_state_notice_source_run},
    {"test_phase57e_startup_state_notice_opt_out_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase57e_startup_state_notice_opt_out_source_run},
    {"test_phase57e_startup_state_notice_opt_out_keeps_other_diagnostics", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57e_startup_state_notice_opt_out_keeps_other_diagnostics},
    {"test_phase57e_startup_state_notice_preserves_uninitialized_origin_metadata", SOURCE_RUN_TEST_SETTINGS, test_phase57e_startup_state_notice_preserves_uninitialized_origin_metadata},
    {"test_phase57e_invalid_startup_state_notice_setting", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57e_invalid_startup_state_notice_setting},
    {"test_phase69b_source_run_message_ordering_contract", SOURCE_RUN_TEST_CORE, test_phase69b_source_run_message_ordering_contract},
    {"test_phase64d_memory_change_source_attribution", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase64d_memory_change_source_attribution},
    {"test_phase57h_register_write_metadata_no_register_writes", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57h_register_write_metadata_no_register_writes},
    {"test_phase57h_register_write_metadata_same_value_parent_write", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57h_register_write_metadata_same_value_parent_write},
    {"test_phase57h_register_write_metadata_alias_writes", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57h_register_write_metadata_alias_writes},
    {"test_phase57h_seeded_startup_not_counted_as_program_write", SOURCE_RUN_TEST_SETTINGS, test_phase57h_seeded_startup_not_counted_as_program_write},
    {"test_phase64_equality_jumps_source_run_programs", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase64_equality_jumps_source_run_programs},
    {"test_phase64_equality_jump_instruction_limit", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase64_equality_jump_instruction_limit},
    {"test_phase64_equality_jump_source_run_diagnostics", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase64_equality_jump_source_run_diagnostics},
    {"test_phase65_signed_relational_jumps_source_run_programs", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase65_signed_relational_jumps_source_run_programs},
    {"test_phase65_signed_relational_jump_source_run_diagnostics", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase65_signed_relational_jump_source_run_diagnostics},
    {"test_phase65_signed_relational_jump_instruction_limit", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase65_signed_relational_jump_instruction_limit},
    {"test_phase65_signed_relational_undefined_flag_use_policy", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase65_signed_relational_undefined_flag_use_policy},
    {"test_phase66_unsigned_relational_jumps_source_run_programs", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase66_unsigned_relational_jumps_source_run_programs},
    {"test_phase66_unsigned_relational_jump_error_paths", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase66_unsigned_relational_jump_error_paths},
    {"test_phase69_direct_call_source_run_behavior", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase69_direct_call_source_run_behavior},
    {"test_phase71_call_ret_source_run_behavior", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase71_call_ret_source_run_behavior},
    {"test_phase71d_procedure_fallthrough_policy_source_run_behavior", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase71d_procedure_fallthrough_policy_source_run_behavior},
    {"test_phase71e_entry_procedure_end_mode_source_run_behavior", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase71e_entry_procedure_end_mode_source_run_behavior},
    {"test_phase71f_fallthrough_opposite_fixtures_source_run", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase71f_fallthrough_opposite_fixtures_source_run},
    {"test_phase72a_push_pop_source_run_stack_semantics", SOURCE_RUN_TEST_CORE, test_phase72a_push_pop_source_run_stack_semantics},
    {"test_phase72a_push_pop_source_run_error_paths", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase72a_push_pop_source_run_error_paths},
    {"test_phase72a_push_pop_source_run_planned_access_policies", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase72a_push_pop_source_run_planned_access_policies},
    {"test_phase73_leave_source_run_semantics_and_diagnostics", SOURCE_RUN_TEST_CORE, test_phase73_leave_source_run_semantics_and_diagnostics},
    {"test_phase73_source_run_default_metadata_and_call_depth", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase73_source_run_default_metadata_and_call_depth},
    {"test_phase74_ret_imm16_source_run_semantics_and_diagnostics", SOURCE_RUN_TEST_CORE, test_phase74_ret_imm16_source_run_semantics_and_diagnostics},
    {"test_phase72_recursive_call_depth_limit_source_run", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase72_recursive_call_depth_limit_source_run},
    {"test_phase72_valid_call_depth_limit_bounds_and_root_ret_source_run", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase72_valid_call_depth_limit_bounds_and_root_ret_source_run},
    {"test_phase72_exact_call_depth_limit_source_run", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase72_exact_call_depth_limit_source_run},
    {"test_phase72_invalid_call_depth_limit_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase72_invalid_call_depth_limit_source_run},
    {"test_current_source_run_phase_metadata", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_current_source_run_phase_metadata},
    {"test_phase71b_source_run_diagnostic_wording_cleanup", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase71b_source_run_diagnostic_wording_cleanup},
    {"test_phase68b_eip_pseudo_display_and_rejections", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase68b_eip_pseudo_display_and_rejections},
    {"test_phase67a_entry_procedure_runtime_boundary_source_run", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase67a_entry_procedure_runtime_boundary_source_run},
    {"test_phase68a_source_run_observes_fixed_layout_esp_startup", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase68a_source_run_observes_fixed_layout_esp_startup},
    {"test_phase68a_seeded_startup_preserves_stack_pointer_contract", SOURCE_RUN_TEST_SETTINGS, test_phase68a_seeded_startup_preserves_stack_pointer_contract},
    {"test_phase68a_automatic_layout_esp_uses_selected_stack_metadata", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase68a_automatic_layout_esp_uses_selected_stack_metadata},
    {"test_phase67a_entry_boundary_error_and_transfer_regressions", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase67a_entry_boundary_error_and_transfer_regressions},
    {"test_phase67_arithmetic_branch_watchdog_source_run_harness", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase67_arithmetic_branch_watchdog_source_run_harness},
    {"test_phase63_cmp_memory_source_run_programs", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase63_cmp_memory_source_run_programs},
    {"test_phase63_cmp_uninitialized_read_policy", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase63_cmp_uninitialized_read_policy},
    {"test_phase63_cmp_section_and_object_planned_read_policy", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase63_cmp_section_and_object_planned_read_policy},
    {"test_phase63_cmp_invalid_level1_read_preserves_flags", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase63_cmp_invalid_level1_read_preserves_flags},
    {"test_phase59_instruction_limit_source_run", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase59_instruction_limit_source_run},
    {"test_phase61_jmp_executes_after_prior_instruction", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61_jmp_executes_after_prior_instruction},
    {"test_phase61_jmp_executes_first_instruction", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61_jmp_executes_first_instruction},
    {"test_phase61_jmp_to_immediately_following_instruction", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61_jmp_to_immediately_following_instruction},
    {"test_phase61_jmp_to_procedure_entry_executes_as_direct_branch", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61_jmp_to_procedure_entry_executes_as_direct_branch},
    {"test_phase61_backward_jmp_reaches_instruction_limit", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61_backward_jmp_reaches_instruction_limit},
    {"test_phase61e_reserved_loop_label_source_run_diagnostic", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61e_reserved_loop_label_source_run_diagnostic},
    {"test_phase61_jmp_respects_instruction_limit_precedence", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61_jmp_respects_instruction_limit_precedence},
    {"test_phase61a_jmp_skips_memory_write_and_keeps_console_empty", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61a_jmp_skips_memory_write_and_keeps_console_empty},
    {"test_phase61a_backward_jmp_limit_blocks_next_fetch_without_mutation", SOURCE_RUN_TEST_CONTROL_FLOW, test_phase61a_backward_jmp_limit_blocks_next_fetch_without_mutation},
    {"test_phase57f_zero_mode_seed_does_not_randomize", SOURCE_RUN_TEST_SETTINGS, test_phase57f_zero_mode_seed_does_not_randomize},
    {"test_phase57f_seeded_startup_is_deterministic", SOURCE_RUN_TEST_SETTINGS, test_phase57f_seeded_startup_is_deterministic},
    {"test_phase57f_different_seeds_change_startup_state", SOURCE_RUN_TEST_SETTINGS, test_phase57f_different_seeds_change_startup_state},
    {"test_phase57f_seeded_startup_notice_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase57f_seeded_startup_notice_source_run},
    {"test_phase57f_invalid_startup_register_flag_mode", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57f_invalid_startup_register_flag_mode},
    {"test_phase57f_seeded_startup_preserves_memory_and_uninitialized_origin", SOURCE_RUN_TEST_SETTINGS, test_phase57f_seeded_startup_preserves_memory_and_uninitialized_origin},
    {"test_phase57g_zero_uninitialized_storage_mode_preserves_zero_bytes", SOURCE_RUN_TEST_SETTINGS, test_phase57g_zero_uninitialized_storage_mode_preserves_zero_bytes},
    {"test_phase57g_seeded_uninitialized_storage_randomizes_only_uninitialized_bytes", SOURCE_RUN_TEST_SETTINGS, test_phase57g_seeded_uninitialized_storage_randomizes_only_uninitialized_bytes},
    {"test_phase57g_seeded_uninitialized_storage_is_deterministic", SOURCE_RUN_TEST_SETTINGS, test_phase57g_seeded_uninitialized_storage_is_deterministic},
    {"test_phase57g_different_seeds_change_uninitialized_storage", SOURCE_RUN_TEST_SETTINGS, test_phase57g_different_seeds_change_uninitialized_storage},
    {"test_phase57g_seeded_uninitialized_storage_preserves_each_origin_class", SOURCE_RUN_TEST_SETTINGS, test_phase57g_seeded_uninitialized_storage_preserves_each_origin_class},
    {"test_phase57i_const_uninitialized_storage_acceptance_source_run", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase57i_const_uninitialized_storage_acceptance_source_run},
    {"test_phase57j_const_uninitialized_storage_policy_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase57j_const_uninitialized_storage_policy_source_run},
    {"test_phase57g_startup_axes_are_orthogonal", SOURCE_RUN_TEST_SETTINGS, test_phase57g_startup_axes_are_orthogonal},
    {"test_phase57g_combined_seeded_startup_axes_affect_registers_and_storage", SOURCE_RUN_TEST_SETTINGS, test_phase57g_combined_seeded_startup_axes_affect_registers_and_storage},
    {"test_phase57g_seeded_uninitialized_storage_strict_read_stops", SOURCE_RUN_TEST_SETTINGS, test_phase57g_seeded_uninitialized_storage_strict_read_stops},
    {"test_phase57g_seeded_uninitialized_storage_notice_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase57g_seeded_uninitialized_storage_notice_source_run},
    {"test_phase57g_combined_seeded_startup_notice_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase57g_combined_seeded_startup_notice_source_run},
    {"test_phase57g_invalid_uninitialized_storage_mode", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase57g_invalid_uninitialized_storage_mode},
    {"test_phase53e_invalid_ui_settings_return_invalid_argument", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase53e_invalid_ui_settings_return_invalid_argument},
    {"test_phase50b_undefined_flag_use_warn_policy_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase50b_undefined_flag_use_warn_policy_source_run},
    {"test_phase50b_undefined_flag_use_error_policy_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase50b_undefined_flag_use_error_policy_source_run},
    {"test_phase50b_undefined_flag_use_sbb_warn_policy_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase50b_undefined_flag_use_sbb_warn_policy_source_run},
    {"test_phase50b_undefined_flag_use_explicit_off_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase50b_undefined_flag_use_explicit_off_source_run},
    {"test_phase50b_undefined_flag_use_cmc_error_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase50b_undefined_flag_use_cmc_error_source_run},
    {"test_phase50b_undefined_flag_use_memory_destination_error_source_run", SOURCE_RUN_TEST_SETTINGS, test_phase50b_undefined_flag_use_memory_destination_error_source_run},
    {"test_phase51_fixed_and_automatic_layout_smoke_harness", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase51_fixed_and_automatic_layout_smoke_harness},
    {"test_phase51_const_write_precedes_object_diagnostics", SOURCE_RUN_TEST_DIAGNOSTICS, test_phase51_const_write_precedes_object_diagnostics},
    {"test_phase51_uninitialized_rmw_smoke_harness", SOURCE_RUN_TEST_MEMORY_LAYOUT, test_phase51_uninitialized_rmw_smoke_harness},
    {"test_phase51_irvine_exit_and_casemap_smoke_harness", SOURCE_RUN_TEST_SETTINGS, test_phase51_irvine_exit_and_casemap_smoke_harness},
    {"test_phase51_instruction_family_source_run_smoke_harness", SOURCE_RUN_TEST_CORE, test_phase51_instruction_family_source_run_smoke_harness},
    {"test_null_source_returns_invalid_argument_json", SOURCE_RUN_TEST_DIAGNOSTICS, test_null_source_returns_invalid_argument_json},
    {"test_empty_source_returns_parse_error_json", SOURCE_RUN_TEST_DIAGNOSTICS, test_empty_source_returns_parse_error_json},
    {"test_subsequent_calls_return_latest_result", SOURCE_RUN_TEST_CORE, test_subsequent_calls_return_latest_result},
};

int main(int argc, char **argv) {
    int failures = 0;
    int has_filter = 0;
    SourceRunTestFamily requested_family = SOURCE_RUN_TEST_CORE;
    size_t index = 0U;
    size_t selected_count = 0U;
    const size_t test_case_count = sizeof(SOURCE_RUN_TEST_CASES) / sizeof(SOURCE_RUN_TEST_CASES[0]);

    if (argc == 2 && strcmp(argv[1], "--list-groups") == 0) {
        print_source_run_group_inventory(SOURCE_RUN_TEST_CASES, test_case_count);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--group") == 0) {
        if (!parse_source_run_family(argv[2], &requested_family)) {
            fprintf(stderr, "FAIL: unknown source-run test group: %s\n", argv[2]);
            print_source_run_group_usage(argv[0]);
            return 2;
        }
        has_filter = 1;
    } else if (argc != 1) {
        print_source_run_group_usage(argv[0]);
        return 2;
    }

    for (index = 0U; index < test_case_count; index += 1U) {
        const SourceRunTestCase *test_case = &SOURCE_RUN_TEST_CASES[index];
        if (!source_run_test_matches_filter(test_case, has_filter, requested_family)) {
            continue;
        }
        selected_count += 1U;
        failures += test_case->run();
    }

    if (selected_count == 0U) {
        fprintf(stderr, "FAIL: source-run test group contains no fixtures: %s\n", source_run_family_name(requested_family));
        return 1;
    }

    if (failures != 0) {
        return 1;
    }

    if (has_filter) {
        printf("Source-run %s fixture-family tests passed (%lu fixtures).\n", source_run_family_name(requested_family), (unsigned long)selected_count);
    } else {
        puts("Source execution tests through Phase 77 PROC USES runtime save/restore behavior passed.");
    }
    return 0;
}
