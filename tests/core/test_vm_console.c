/*
 * @file test_vm_console.c
 * @brief Unit tests for Phase 85 Program Console buffer infrastructure.
 *
 * These tests verify the VM-owned Program Console stream without enabling any
 * Irvine32 output routine behavior. Simulator diagnostics remain outside this
 * buffer and are covered by source-run and browser formatter tests.
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
    if (vm_console_truncated(&console)) {
        failures += record_failure("new console should not be truncated");
    }

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
    if (vm_console_truncated(&console)) {
        failures += record_failure("console reset should clear truncation state");
    }

    failures += expect_console_status(vm_console_append(&console, NULL, 1U), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "nonzero NULL append should be rejected");
    failures += expect_console_status(vm_console_reset(NULL), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "NULL reset should be rejected");
    failures += expect_console_status(vm_console_append(NULL, "x", 1U), VM_CONSOLE_STATUS_INVALID_ARGUMENT, "NULL append target should be rejected");

    vm_console_deinit(&console);
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
    failures += test_vm_console_load_reset_and_step_preservation();

    if (failures != 0) {
        fprintf(stderr, "%d Program Console test failure(s).\n", failures);
        return 1;
    }

    puts("Program Console tests through Phase 85 stream infrastructure passed.");
    return 0;
}
