/*
 * @file test_vm_console.c
 * @brief Unit tests for Phase 86 Program Console output limits.
 *
 * These tests verify the VM-owned Program Console stream and deterministic
 * stop-on-limit behavior without enabling any Irvine32 output routine behavior.
 * Simulator diagnostics remain outside this buffer and are covered by source-run
 * and browser formatter tests.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_console.h"
#include "../../src/core/vm_exec.h"

/// Records a test failure with file-local context.
///
/// @param message Human-readable failure description.
/// @return Always returns 1 so callers can accumulate failures.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that two size_t values are equal.
///
/// @param actual Actual value produced by the test.
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


/// Verifies that a boolean value equals the expected value.
///
/// @param actual Actual boolean produced by the test.
/// @param expected Expected boolean.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_bool(bool actual, bool expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, actual ? "true" : "false", expected ? "true" : "false");
        return 1;
    }
    return 0;
}

/// Verifies that a Program Console limit kind equals the expected value.
///
/// @param actual Actual limit kind produced by the test.
/// @param expected Expected limit kind.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_limit_kind(VmConsoleLimitKind actual, VmConsoleLimitKind expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_console_limit_kind_name(actual), vm_console_limit_kind_name(expected));
        return 1;
    }
    return 0;
}

/// Verifies that two strings are equal.
///
/// @param actual Actual text produced by the test.
/// @param expected Expected text.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_string(const char *actual, const char *expected, const char *message) {
    const char *safe_actual = actual != NULL ? actual : "";
    const char *safe_expected = expected != NULL ? expected : "";
    if (strcmp(safe_actual, safe_expected) != 0) {
        fprintf(stderr, "FAIL: %s (actual=\"%s\" expected=\"%s\")\n", message, safe_actual, safe_expected);
        return 1;
    }
    return 0;
}

/// Verifies that a Program Console status equals the expected value.
///
/// @param actual Actual status produced by the test.
/// @param expected Expected status.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_console_status(VmConsoleStatus actual, VmConsoleStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_console_status_name(actual), vm_console_status_name(expected));
        return 1;
    }
    return 0;
}

/// Verifies that a VM executor status equals the expected value.
///
/// @param actual Actual status produced by the test.
/// @param expected Expected status.
/// @param message Failure message when values differ.
/// @return Zero on success, otherwise one failure.
static int expect_exec_status(VmExecStatus actual, VmExecStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_exec_status_name(actual), vm_exec_status_name(expected));
        return 1;
    }
    return 0;
}

/// Verifies deterministic append, zero-length append, and reset behavior.
///
/// @return Zero on success, otherwise number of failures.
static int test_console_append_zero_length_and_reset(void) {
    int failures = 0;
    VmConsole console;

    failures += expect_console_status(vm_console_init(&console), VM_CONSOLE_STATUS_OK, "console init should succeed");
    failures += expect_string(vm_console_text(&console), "", "new console should be empty");
    failures += expect_size(vm_console_byte_count(&console), 0U, "new console byte count should be zero");
    failures += expect_size(vm_console_line_count(&console), 0U, "new console line count should be zero");
    failures += expect_size(vm_console_max_bytes(&console), VM_CONSOLE_DEFAULT_MAX_BYTES, "new console should use the default byte limit");
    failures += expect_size(vm_console_max_lines(&console), VM_CONSOLE_DEFAULT_MAX_LINES, "new console should use the default line limit");
    failures += expect_bool(vm_console_limit_exceeded(&console), false, "new console should not have a limit violation");
    failures += expect_limit_kind(vm_console_limit_kind(&console), VM_CONSOLE_LIMIT_KIND_NONE, "new console should not have a rejected limit kind");
    failures += expect_bool(vm_console_truncated(&console), false, "new console should not be truncated");

    failures += expect_console_status(vm_console_append(&console, "ABC", 3U), VM_CONSOLE_STATUS_OK, "first append should succeed");
    failures += expect_console_status(vm_console_append(&console, "\nD", 2U), VM_CONSOLE_STATUS_OK, "second append should succeed");
    failures += expect_string(vm_console_text(&console), "ABC\nD", "console appends should concatenate deterministically");
    failures += expect_size(vm_console_byte_count(&console), 5U, "console byte count should track committed bytes");
    failures += expect_size(vm_console_line_count(&console), 1U, "console line count should track line-feed bytes");

    failures += expect_console_status(vm_console_append(&console, NULL, 0U), VM_CONSOLE_STATUS_OK, "zero-length append should be a no-op");
    failures += expect_string(vm_console_text(&console), "ABC\nD", "zero-length append should not mutate text");
    failures += expect_size(vm_console_byte_count(&console), 5U, "zero-length append should not mutate byte count");

    failures += expect_console_status(vm_console_reset(&console), VM_CONSOLE_STATUS_OK, "console reset should succeed");
    failures += expect_string(vm_console_text(&console), "", "console reset should clear text");
    failures += expect_size(vm_console_byte_count(&console), 0U, "console reset should clear byte count");
    failures += expect_size(vm_console_line_count(&console), 0U, "console reset should clear line count");
    failures += expect_bool(vm_console_limit_exceeded(&console), false, "console reset should clear limit-exceeded state");
    failures += expect_limit_kind(vm_console_limit_kind(&console), VM_CONSOLE_LIMIT_KIND_NONE, "console reset should clear rejected limit kind");
    failures += expect_bool(vm_console_truncated(&console), false, "console reset should clear truncation state");

    failures += expect_console_status(vm_console_append(&console, NULL, 1U), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "nonzero NULL append should be rejected");
    failures += expect_console_status(vm_console_reset(NULL), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "NULL reset should be rejected");
    failures += expect_console_status(vm_console_append(NULL, "x", 1U), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "NULL append target should be rejected");

    vm_console_deinit(&console);
    return failures;
}

/// Verifies deterministic Program Console byte-limit enforcement.
///
/// @return Zero on success, otherwise number of failures.
static int test_console_byte_limits(void) {
    int failures = 0;
    VmConsole console;

    failures += expect_console_status(vm_console_init(&console), VM_CONSOLE_STATUS_OK, "console init should succeed for byte-limit tests");
    failures += expect_console_status(vm_console_configure_limits(&console, 5U, 10U), VM_CONSOLE_STATUS_OK, "small byte limit should configure");
    failures += expect_console_status(vm_console_append(&console, "AB", 2U), VM_CONSOLE_STATUS_OK, "append under byte limit should succeed");
    failures += expect_string(vm_console_text(&console), "AB", "under-limit append should commit text");
    failures += expect_size(vm_console_byte_count(&console), 2U, "under-limit append should update byte count");

    failures += expect_console_status(vm_console_append(&console, "CDE", 3U), VM_CONSOLE_STATUS_OK, "append exactly to byte limit should succeed");
    failures += expect_string(vm_console_text(&console), "ABCDE", "exact-limit append should commit text");
    failures += expect_size(vm_console_byte_count(&console), 5U, "exact-limit append should update byte count");

    failures += expect_console_status(vm_console_append(&console, "F", 1U), VM_CONSOLE_STATUS_OUTPUT_LIMIT_EXCEEDED, "append over byte limit should fail");
    failures += expect_string(vm_console_text(&console), "ABCDE", "failed byte-limit append should not partially append");
    failures += expect_size(vm_console_byte_count(&console), 5U, "failed byte-limit append should not mutate byte count");
    failures += expect_bool(vm_console_limit_exceeded(&console), true, "failed byte-limit append should set limit-exceeded state");
    failures += expect_limit_kind(vm_console_limit_kind(&console), VM_CONSOLE_LIMIT_KIND_BYTE, "failed byte-limit append should record byte limit kind");
    failures += expect_bool(vm_console_truncated(&console), false, "stop-on-limit byte failure should not truncate output");

    failures += expect_console_status(vm_console_append(&console, NULL, 0U), VM_CONSOLE_STATUS_OK, "zero-length append at byte limit should remain a no-op");
    failures += expect_string(vm_console_text(&console), "ABCDE", "zero-length append at byte limit should not mutate text");

    failures += expect_console_status(vm_console_configure_limits(&console, 4U, 10U), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "limit below committed bytes should be rejected");

    failures += expect_console_status(vm_console_reset(&console), VM_CONSOLE_STATUS_OK, "reset before combined-limit test should succeed");
    failures += expect_console_status(vm_console_configure_limits(&console, 3U, 0U), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "zero line limit should be rejected");
    failures += expect_console_status(vm_console_configure_limits(&console, 3U, 1U), VM_CONSOLE_STATUS_OK, "combined-limit fixture should configure");
    failures += expect_console_status(vm_console_append(&console, "abc", 3U), VM_CONSOLE_STATUS_OK, "combined-limit fixture should reach byte limit");
    failures += expect_console_status(vm_console_append(&console, "d\n", 2U), VM_CONSOLE_STATUS_OUTPUT_LIMIT_EXCEEDED, "append exceeding byte and line limits should fail deterministically");
    failures += expect_limit_kind(vm_console_limit_kind(&console), VM_CONSOLE_LIMIT_KIND_BYTE, "byte limit should take deterministic precedence when both limits would be exceeded");
    failures += expect_string(vm_console_text(&console), "abc", "combined-limit failure should not partially append");

    vm_console_deinit(&console);
    return failures;
}

/// Verifies deterministic Program Console line-limit and newline-counting policy.
///
/// @return Zero on success, otherwise number of failures.
static int test_console_line_limits_and_newline_policy(void) {
    int failures = 0;
    VmConsole console;

    failures += expect_console_status(vm_console_init(&console), VM_CONSOLE_STATUS_OK, "console init should succeed for line-limit tests");
    failures += expect_console_status(vm_console_configure_limits(&console, 64U, 2U), VM_CONSOLE_STATUS_OK, "small line limit should configure");
    failures += expect_console_status(vm_console_append(&console, "A\rB", 3U), VM_CONSOLE_STATUS_OK, "standalone carriage return should not count as a line");
    failures += expect_size(vm_console_line_count(&console), 0U, "standalone carriage return should add zero lines");

    failures += expect_console_status(vm_console_append(&console, "\r\nC", 3U), VM_CONSOLE_STATUS_OK, "CRLF should count as one line because LF is counted");
    failures += expect_size(vm_console_line_count(&console), 1U, "CRLF append should add one line");
    failures += expect_console_status(vm_console_append(&console, "\n", 1U), VM_CONSOLE_STATUS_OK, "append exactly to line limit should succeed");
    failures += expect_size(vm_console_line_count(&console), 2U, "exact line limit should be committed");
    failures += expect_string(vm_console_text(&console), "A\rB\r\nC\n", "line-limit exact appends should commit in order");

    failures += expect_console_status(vm_console_append(&console, "D\n", 2U), VM_CONSOLE_STATUS_OUTPUT_LIMIT_EXCEEDED, "append over line limit should fail");
    failures += expect_string(vm_console_text(&console), "A\rB\r\nC\n", "failed line-limit append should not partially append");
    failures += expect_size(vm_console_byte_count(&console), 7U, "failed line-limit append should not mutate byte count");
    failures += expect_size(vm_console_line_count(&console), 2U, "failed line-limit append should not mutate line count");
    failures += expect_bool(vm_console_limit_exceeded(&console), true, "failed line-limit append should set limit-exceeded state");
    failures += expect_limit_kind(vm_console_limit_kind(&console), VM_CONSOLE_LIMIT_KIND_LINE, "failed line-limit append should record line limit kind");
    failures += expect_bool(vm_console_truncated(&console), false, "stop-on-limit line failure should not truncate output");
    failures += expect_console_status(vm_console_configure_limits(&console, 64U, 1U), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "limit below committed line count should be rejected");

    vm_console_deinit(&console);
    return failures;
}

/// Verifies that VM-owned Program Console appends map limit failures to executor diagnostics.
///
/// @return Zero on success, otherwise number of failures.
static int test_vm_console_append_limit_diagnostic(void) {
    int failures = 0;
    Vm vm;
    const VmExecDiagnostic *diagnostic = NULL;

    failures += expect_exec_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "VM init should succeed for console diagnostic test");
    failures += expect_console_status(vm_console_configure_limits(&vm.program_console, 3U, 10U), VM_CONSOLE_STATUS_OK, "VM console limit should configure");
    failures += expect_exec_status(vm_append_program_console_output(&vm, "abc", 3U, NULL), VM_EXEC_STATUS_OK, "VM console append exactly to limit should succeed");
    failures += expect_exec_status(vm_append_program_console_output(&vm, "d", 1U, NULL), VM_EXEC_STATUS_CONSOLE_OUTPUT_LIMIT_EXCEEDED, "VM console append over limit should map to executor status");
    failures += expect_string(vm_console_text(vm_program_console(&vm)), "abc", "VM console limit failure should not partially append");
    diagnostic = vm_last_diagnostic(&vm);
    if (diagnostic == NULL || diagnostic->status != VM_EXEC_STATUS_CONSOLE_OUTPUT_LIMIT_EXCEEDED) {
        failures += record_failure("VM console limit failure should record executor diagnostic status");
    }

    vm_deinit(&vm);
    return failures;
}

/// Verifies that the VM clears Program Console output on load but preserves it across stepping.
///
/// @return Zero on success, otherwise number of failures.
static int test_vm_console_load_reset_and_step_preservation(void) {
    int failures = 0;
    Vm vm;
    VmIrInstruction program[] = {
        {VM_IR_OPCODE_NOP, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 1U, "nop", 0U},
        {VM_IR_OPCODE_EXIT, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, {VM_IR_OPERAND_NONE, 0U, 0U, VM_REGISTER_COUNT, 0U, VM_IR_RELOCATION_NONE}, "main.asm", 2U, "exit", 1U}
    };

    failures += expect_exec_status(vm_init(&vm, NULL), VM_EXEC_STATUS_OK, "VM init should succeed");
    failures += expect_console_status(vm_console_append(&vm.program_console, "kept", 4U), VM_CONSOLE_STATUS_OK, "test append before stepping should succeed");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_CODE_FELL_OFF_END, "unloaded VM step should stop without clearing console");
    failures += expect_string(vm_console_text(vm_program_console(&vm)), "kept", "stepping should not clear Program Console output");

    failures += expect_exec_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "load program should succeed");
    failures += expect_string(vm_console_text(vm_program_console(&vm)), "", "loading a new program should clear Program Console output");
    failures += expect_console_status(vm_console_append(&vm.program_console, "persist", 7U), VM_CONSOLE_STATUS_OK, "test append after load should succeed");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_OK, "NOP step should succeed");
    failures += expect_string(vm_console_text(vm_program_console(&vm)), "persist", "ordinary stepping should preserve Program Console output");
    failures += expect_exec_status(vm_step(&vm), VM_EXEC_STATUS_OK, "EXIT step should succeed");
    failures += expect_string(vm_console_text(vm_program_console(&vm)), "persist", "terminating step should preserve Program Console output until rerun/reset");

    failures += expect_exec_status(vm_load_program(&vm, program, sizeof(program) / sizeof(program[0])), VM_EXEC_STATUS_OK, "rerun load should succeed");
    failures += expect_string(vm_console_text(vm_program_console(&vm)), "", "rerun load should clear prior Program Console output");

    vm_deinit(&vm);
    return failures;
}

int main(void) {
    int failures = 0;

    failures += test_console_append_zero_length_and_reset();
    failures += test_console_byte_limits();
    failures += test_console_line_limits_and_newline_policy();
    failures += test_vm_console_append_limit_diagnostic();
    failures += test_vm_console_load_reset_and_step_preservation();

    if (failures != 0) {
        fprintf(stderr, "%d Program Console test failure(s).\n", failures);
        return 1;
    }

    puts("Program Console tests through Phase 86 output limits passed.");
    return 0;
}
