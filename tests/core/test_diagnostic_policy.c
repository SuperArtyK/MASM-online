/*
 * @file test_diagnostic_policy.c
 * @brief Unit tests for the diagnostic-policy registry and migration helpers.
 *
 * These tests verify the shared off/warn/error vocabulary, policy-family
 * registry metadata, Phase 57D behavior-preserving migration helpers, Phase
 * 57E startup-state notices, Phase 57J const-uninitialized-storage diagnostics,
 * and the Phase 57L mandatory unsupported-code-memory-access family.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_diagnostic_policy.h"

/// Records a diagnostic-policy test failure.
///
/// @param message Human-readable failure description.
/// @return Always returns one failure.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that two strings match exactly.
///
/// @param actual Actual string, or NULL.
/// @param expected Expected string.
/// @param message Failure message emitted when the strings differ.
/// @return Zero on success, otherwise one failure.
static int expect_string_equal(const char *actual, const char *expected, const char *message) {
    if (actual == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s (expected %s, got %s)\n", message, expected, actual != NULL ? actual : "NULL");
        return 1;
    }

    return 0;
}

/// Verifies that one boolean value is true.
///
/// @param value Value to inspect.
/// @param message Failure message emitted when @p value is false.
/// @return Zero on success, otherwise one failure.
static int expect_true(bool value, const char *message) {
    return value ? 0 : record_failure(message);
}

/// Verifies that one boolean value is false.
///
/// @param value Value to inspect.
/// @param message Failure message emitted when @p value is true.
/// @return Zero on success, otherwise one failure.
static int expect_false(bool value, const char *message) {
    return !value ? 0 : record_failure(message);
}

/// Verifies common policy value parse and format behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_policy_value_parse_and_format(void) {
    int failures = 0;
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_ERROR;

    failures += expect_string_equal(vm_diagnostic_policy_value_name(VM_DIAGNOSTIC_POLICY_VALUE_OFF), "off", "OFF should format as off");
    failures += expect_string_equal(vm_diagnostic_policy_value_name(VM_DIAGNOSTIC_POLICY_VALUE_WARN), "warn", "WARN should format as warn");
    failures += expect_string_equal(vm_diagnostic_policy_value_name(VM_DIAGNOSTIC_POLICY_VALUE_ERROR), "error", "ERROR should format as error");

    failures += expect_true(vm_diagnostic_policy_parse_value("off", &value), "parse off should succeed");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_OFF ? record_failure("parse off should return OFF enum") : 0;
    failures += expect_true(vm_diagnostic_policy_parse_value("warn", &value), "parse warn should succeed");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("parse warn should return WARN enum") : 0;
    failures += expect_true(vm_diagnostic_policy_parse_value("error", &value), "parse error should succeed");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_ERROR ? record_failure("parse error should return ERROR enum") : 0;

    return failures;
}

/// Verifies rejection of invalid central policy values and invalid pointers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_invalid_policy_value_rejection(void) {
    int failures = 0;
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_WARN;

    failures += expect_false(vm_diagnostic_policy_parse_value("", &value), "empty value should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_value("strict", &value), "strict alias should be rejected in central vocabulary");
    failures += expect_false(vm_diagnostic_policy_parse_value("on", &value), "on alias should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_value("WARN", &value), "uppercase value should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_value(NULL, &value), "NULL text should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_value("warn", NULL), "NULL output pointer should be rejected");

    if (vm_diagnostic_policy_value_name(VM_DIAGNOSTIC_POLICY_VALUE_COUNT) != NULL) {
        failures += record_failure("out-of-range value should not format");
    }

    return failures;
}

/// Verifies known diagnostic-policy family parse and format behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_policy_family_parse_and_format(void) {
    int failures = 0;
    VmDiagnosticPolicyFamily family = VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS;

    failures += expect_string_equal(vm_diagnostic_policy_family_name(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ), "uninitialized-read", "uninitialized-read family format");
    failures += expect_string_equal(vm_diagnostic_policy_family_name(VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE), "undefined-flag-use", "undefined-flag-use family format");
    failures += expect_string_equal(vm_diagnostic_policy_family_name(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE), "compatibility-notice", "compatibility-notice family format");
    failures += expect_string_equal(vm_diagnostic_policy_family_name(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE), "const-uninitialized-storage", "const-uninitialized-storage family format");
    failures += expect_string_equal(vm_diagnostic_policy_family_name(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE), "startup-state-notice", "startup-state-notice family format");
    failures += expect_string_equal(vm_diagnostic_policy_family_name(VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS), "unsupported-code-memory-access", "unsupported-code-memory-access family format");

    failures += expect_true(vm_diagnostic_policy_parse_family("uninitialized-read", &family), "parse uninitialized-read should succeed");
    failures += family != VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ ? record_failure("parse uninitialized-read enum") : 0;
    failures += expect_true(vm_diagnostic_policy_parse_family("undefined-flag-use", &family), "parse undefined-flag-use should succeed");
    failures += family != VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE ? record_failure("parse undefined-flag-use enum") : 0;
    failures += expect_true(vm_diagnostic_policy_parse_family("compatibility-notice", &family), "parse compatibility-notice should succeed");
    failures += family != VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE ? record_failure("parse compatibility-notice enum") : 0;
    failures += expect_true(vm_diagnostic_policy_parse_family("const-uninitialized-storage", &family), "parse const-uninitialized-storage family should succeed");
    failures += family != VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE ? record_failure("parse const-uninitialized-storage enum") : 0;
    failures += expect_true(vm_diagnostic_policy_parse_family("startup-state-notice", &family), "parse startup family should succeed");
    failures += family != VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE ? record_failure("parse startup-state-notice enum") : 0;
    failures += expect_true(vm_diagnostic_policy_parse_family("unsupported-code-memory-access", &family), "parse reserved code memory-access family should succeed");
    failures += family != VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS ? record_failure("parse unsupported-code-memory-access enum") : 0;

    return failures;
}

/// Verifies policy-family metadata for implemented and inactive reserved families.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_policy_family_metadata(void) {
    int failures = 0;
    const VmDiagnosticPolicyFamilyInfo *info = NULL;

    info = vm_diagnostic_policy_family_info(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ);
    failures += info == NULL ? record_failure("uninitialized-read metadata should exist") : 0;
    if (info != NULL) {
        failures += info->state != VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED ? record_failure("uninitialized-read should be implemented") : 0;
        failures += !info->has_default_value ? record_failure("uninitialized-read should have current default") : 0;
        failures += info->default_value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("uninitialized-read default should be warn") : 0;
    }

    info = vm_diagnostic_policy_family_info(VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE);
    failures += info == NULL ? record_failure("undefined-flag-use metadata should exist") : 0;
    if (info != NULL) {
        failures += info->state != VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED ? record_failure("undefined-flag-use should be implemented") : 0;
        failures += !info->has_default_value ? record_failure("undefined-flag-use should have current default") : 0;
        failures += info->default_value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("undefined-flag-use default should be warn") : 0;
    }

    info = vm_diagnostic_policy_family_info(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE);
    failures += info == NULL ? record_failure("compatibility-notice metadata should exist") : 0;
    if (info != NULL) {
        failures += info->state != VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED ? record_failure("compatibility-notice should be implemented") : 0;
        failures += !info->has_default_value ? record_failure("compatibility-notice should have current default") : 0;
        failures += info->default_value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("compatibility-notice default should be warn") : 0;
    }

    info = vm_diagnostic_policy_family_info(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE);
    failures += info == NULL ? record_failure("startup-state-notice metadata should exist") : 0;
    if (info != NULL) {
        failures += info->state != VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED ? record_failure("startup-state-notice should be implemented") : 0;
        failures += !info->has_default_value ? record_failure("startup-state-notice should have current default") : 0;
        failures += info->default_value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("startup-state-notice default should be warn") : 0;
    }

    info = vm_diagnostic_policy_family_info(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE);
    failures += info == NULL ? record_failure("const-uninitialized-storage metadata should exist") : 0;
    if (info != NULL) {
        failures += info->state != VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED ? record_failure("const-uninitialized-storage should be implemented for Phase 57J") : 0;
        failures += !info->has_default_value ? record_failure("const-uninitialized-storage should have current default") : 0;
        failures += info->default_value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("const-uninitialized-storage default should be warn") : 0;
    }

    failures += expect_false(vm_diagnostic_policy_family_is_reserved(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE), "const-uninitialized-storage should be implemented, not reserved");
    failures += expect_false(vm_diagnostic_policy_family_is_reserved(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE), "startup-state-notice should be implemented, not reserved");
    failures += expect_false(vm_diagnostic_policy_family_is_reserved(VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS), "unsupported-code-memory-access should be implemented, not reserved");
    failures += expect_false(vm_diagnostic_policy_family_is_reserved(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ), "uninitialized-read should not be reserved inactive");

    info = vm_diagnostic_policy_family_info(VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS);
    failures += info == NULL ? record_failure("unsupported-code-memory-access metadata should exist") : 0;
    if (info != NULL) {
        failures += info->state != VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED ? record_failure("unsupported-code-memory-access should be implemented for Phase 57L") : 0;
        failures += info->has_default_value ? record_failure("mandatory unsupported-code-memory-access should not have optional current default") : 0;
    }

    return failures;
}

/// Verifies invalid policy-family lookups and table integrity.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_policy_family_invalid_and_table_integrity(void) {
    int failures = 0;
    size_t index = 0U;
    size_t other = 0U;
    VmDiagnosticPolicyFamily family = VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ;

    failures += expect_false(vm_diagnostic_policy_parse_family("", &family), "empty family should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_family("section-image", &family), "non-Phase-57C family should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_family("UNINITIALIZED-READ", &family), "uppercase family should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_family(NULL, &family), "NULL family text should be rejected");
    failures += expect_false(vm_diagnostic_policy_parse_family("uninitialized-read", NULL), "NULL family output should be rejected");

    if (vm_diagnostic_policy_family_name(VM_DIAGNOSTIC_POLICY_FAMILY_COUNT) != NULL) {
        failures += record_failure("out-of-range family should not format");
    }

    if (vm_diagnostic_policy_family_info(VM_DIAGNOSTIC_POLICY_FAMILY_COUNT) != NULL) {
        failures += record_failure("out-of-range family should not have metadata");
    }

    failures += vm_diagnostic_policy_family_count() != (size_t)VM_DIAGNOSTIC_POLICY_FAMILY_COUNT ? record_failure("family count should match enum count") : 0;

    for (index = 0U; index < vm_diagnostic_policy_family_count(); ++index) {
        const VmDiagnosticPolicyFamilyInfo *info = vm_diagnostic_policy_family_info((VmDiagnosticPolicyFamily)index);
        if (info == NULL) {
            failures += record_failure("every family enum should have metadata");
            continue;
        }
        if (info->name == NULL || info->name[0] == '\0') {
            failures += record_failure("family metadata should have a non-empty name");
        }
        for (other = index + 1U; other < vm_diagnostic_policy_family_count(); ++other) {
            const VmDiagnosticPolicyFamilyInfo *other_info = vm_diagnostic_policy_family_info((VmDiagnosticPolicyFamily)other);
            if (other_info != NULL && strcmp(info->name, other_info->name) == 0) {
                failures += record_failure("family metadata names should be unique");
            }
        }
    }

    return failures;
}


/// Verifies implemented-family helper APIs used by Phase 57D migration adapters.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57d_implemented_family_helpers(void) {
    int failures = 0;
    VmDiagnosticPolicyValue value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;

    failures += expect_true(vm_diagnostic_policy_family_is_implemented(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ), "uninitialized-read should be implemented");
    failures += expect_true(vm_diagnostic_policy_family_is_implemented(VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE), "undefined-flag-use should be implemented");
    failures += expect_true(vm_diagnostic_policy_family_is_implemented(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE), "compatibility-notice should be implemented");
    failures += expect_true(vm_diagnostic_policy_family_is_implemented(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE), "const-uninitialized-storage should be implemented for Phase 57J");
    failures += expect_true(vm_diagnostic_policy_family_is_implemented(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE), "startup-state-notice should be implemented for Phase 57E");
    failures += expect_true(vm_diagnostic_policy_family_is_implemented(VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS), "unsupported-code-memory-access should be implemented for Phase 57L");
    failures += expect_false(vm_diagnostic_policy_family_is_implemented(VM_DIAGNOSTIC_POLICY_FAMILY_COUNT), "out-of-range family should not be implemented");

    failures += expect_true(vm_diagnostic_policy_family_default_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, &value), "uninitialized-read should expose a default");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("uninitialized-read default helper should return warn") : 0;
    value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
    failures += expect_true(vm_diagnostic_policy_family_default_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE, &value), "undefined-flag-use should expose a default");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("undefined-flag-use default helper should return warn") : 0;
    value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
    failures += expect_true(vm_diagnostic_policy_family_default_value(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE, &value), "compatibility-notice should expose a default");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("compatibility-notice default helper should return warn") : 0;

    value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
    failures += expect_true(vm_diagnostic_policy_family_default_value(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE, &value), "const-uninitialized-storage should expose a default");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("const-uninitialized-storage default helper should return warn") : 0;

    value = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
    failures += expect_true(vm_diagnostic_policy_family_default_value(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE, &value), "startup-state-notice should expose a default");
    failures += value != VM_DIAGNOSTIC_POLICY_VALUE_WARN ? record_failure("startup-state-notice default helper should return warn") : 0;
    failures += expect_false(vm_diagnostic_policy_family_default_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, NULL), "default helper should reject NULL output pointer");

    return failures;
}

/// Verifies Phase 57D and Phase 57E accepted-value checks preserve family-specific modes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_phase57d_family_value_acceptance(void) {
    int failures = 0;

    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, VM_DIAGNOSTIC_POLICY_VALUE_OFF), "uninitialized-read should accept off");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, VM_DIAGNOSTIC_POLICY_VALUE_WARN), "uninitialized-read should accept warn");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, VM_DIAGNOSTIC_POLICY_VALUE_ERROR), "uninitialized-read should accept error");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE, VM_DIAGNOSTIC_POLICY_VALUE_ERROR), "undefined-flag-use should accept error");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE, VM_DIAGNOSTIC_POLICY_VALUE_OFF), "const-uninitialized-storage should accept off");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE, VM_DIAGNOSTIC_POLICY_VALUE_WARN), "const-uninitialized-storage should accept warn");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE, VM_DIAGNOSTIC_POLICY_VALUE_ERROR), "const-uninitialized-storage should accept error");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE, VM_DIAGNOSTIC_POLICY_VALUE_WARN), "compatibility-notice should accept warn");
    failures += expect_false(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE, VM_DIAGNOSTIC_POLICY_VALUE_ERROR), "compatibility-notice should reject unsupported error mode");

    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE, VM_DIAGNOSTIC_POLICY_VALUE_OFF), "startup-state-notice should accept off");
    failures += expect_true(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE, VM_DIAGNOSTIC_POLICY_VALUE_WARN), "startup-state-notice should accept warn");
    failures += expect_false(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE, VM_DIAGNOSTIC_POLICY_VALUE_ERROR), "startup-state-notice should reject unsupported error mode");
    failures += expect_false(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS, VM_DIAGNOSTIC_POLICY_VALUE_OFF), "mandatory unsupported-code-memory-access should reject off");
    failures += expect_false(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS, VM_DIAGNOSTIC_POLICY_VALUE_WARN), "mandatory unsupported-code-memory-access should reject warn");
    failures += expect_false(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNSUPPORTED_CODE_MEMORY_ACCESS, VM_DIAGNOSTIC_POLICY_VALUE_ERROR), "mandatory unsupported-code-memory-access should reject error");
    failures += expect_false(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ, VM_DIAGNOSTIC_POLICY_VALUE_COUNT), "implemented family should reject out-of-range value");
    failures += expect_false(vm_diagnostic_policy_family_accepts_value(VM_DIAGNOSTIC_POLICY_FAMILY_COUNT, VM_DIAGNOSTIC_POLICY_VALUE_WARN), "out-of-range family should reject warn");

    return failures;
}

/// Runs the diagnostic-policy registry and migration tests.
///
/// @return Zero when all tests pass, otherwise nonzero.
int main(void) {
    int failures = 0;

    failures += test_policy_value_parse_and_format();
    failures += test_invalid_policy_value_rejection();
    failures += test_policy_family_parse_and_format();
    failures += test_policy_family_metadata();
    failures += test_policy_family_invalid_and_table_integrity();
    failures += test_phase57d_implemented_family_helpers();
    failures += test_phase57d_family_value_acceptance();

    if (failures != 0) {
        fprintf(stderr, "Diagnostic policy registry tests failed: %d\n", failures);
        return 1;
    }

    printf("Diagnostic policy registry, migration, startup notice, and const-uninitialized-storage tests passed.\n");
    return 0;
}
