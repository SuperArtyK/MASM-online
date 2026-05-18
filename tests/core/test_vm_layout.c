/*
 * @file test_vm_layout.c
 * @brief Unit tests for the Phase 32 memory layout policy object.
 *
 * These tests verify that the explicit layout policy preserves the fixed
 * educational memory layout exactly while providing documented extension points
 * for later automatic and randomized layout phases.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_exec.h"
#include "../../src/core/vm_layout.h"
#include "../../src/core/vm_memory.h"

/// Records a layout test failure.
///
/// @param message Human-readable failure description.
/// @return Always returns one failure.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that a 32-bit value has the expected value.
///
/// @param actual Actual value.
/// @param expected Expected value.
/// @param message Failure message emitted when the value differs.
/// @return Zero on success, otherwise one failure.
static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected 0x%08X, got 0x%08X)\n", message, expected, actual);
        return 1;
    }

    return 0;
}

/// Verifies that a boolean value has the expected value.
///
/// @param actual Actual boolean value.
/// @param expected Expected boolean value.
/// @param message Failure message emitted when the value differs.
/// @return Zero on success, otherwise one failure.
static int expect_bool(bool actual, bool expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %s, got %s)\n", message, expected ? "true" : "false", actual ? "true" : "false");
        return 1;
    }

    return 0;
}

/// Verifies that a layout region has expected fixed-layout bounds.
///
/// @param policy Policy containing the region.
/// @param kind Region kind to inspect.
/// @param expected_base Expected inclusive base address.
/// @param expected_size Expected byte size.
/// @param message Failure context.
/// @return Zero on success, otherwise a positive failure count.
static int expect_policy_region(const VmLayoutPolicy *policy, VmLayoutRegionKind kind, uint32_t expected_base, uint32_t expected_size, const char *message) {
    int failures = 0;
    const VmLayoutRegionPolicy *region = vm_layout_policy_get_region(policy, kind);
    uint32_t size = 0U;

    if (region == NULL) {
        return record_failure(message);
    }

    failures += expect_u32(region->base, expected_base, "region base mismatch");
    failures += expect_bool(vm_layout_region_size(region, &size), true, "region size should be computable");
    failures += expect_u32(size, expected_size, "region size mismatch");
    failures += expect_u32(region->limit, expected_base + expected_size, "region limit mismatch");
    failures += expect_bool(region->minimum_size > 0U, true, "region minimum should be nonzero");
    failures += expect_bool(region->maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] >= expected_size, true, "beginner tier should allow default region size");

    return failures;
}

/// Verifies default policy metadata and fixed region bounds.
///
/// @return Number of failures.
static int test_default_layout_policy_values(void) {
    int failures = 0;
    VmLayoutPolicy policy = vm_layout_default_policy();

    failures += expect_u32((uint32_t)policy.mode, (uint32_t)VM_LAYOUT_MODE_FIXED, "default policy should use fixed layout mode");
    failures += expect_u32((uint32_t)policy.safety_tier, (uint32_t)VM_LAYOUT_SAFETY_TIER_BEGINNER, "default safety tier should be beginner");
    failures += expect_u32(policy.stack_size_request, VM_LAYOUT_DEFAULT_STACK_SIZE_REQUEST, "default stack request mismatch");
    failures += expect_u32(policy.heap_size_request, VM_LAYOUT_DEFAULT_HEAP_SIZE_REQUEST, "default heap request mismatch");
    failures += expect_u32(policy.region_alignment, VM_LAYOUT_DEFAULT_REGION_ALIGNMENT, "default region alignment mismatch");
    failures += expect_u32(policy.guard_gap_size, VM_LAYOUT_DEFAULT_GUARD_GAP_SIZE, "default guard gap mismatch");
    failures += expect_bool(policy.has_random_seed, false, "default policy should not have caller random seed");
    failures += expect_u32(policy.random_seed, VM_LAYOUT_DEFAULT_RANDOM_SEED, "default random seed placeholder mismatch");
    failures += expect_bool(vm_layout_policy_is_valid(&policy), true, "default policy should validate");

    failures += expect_policy_region(&policy, VM_LAYOUT_REGION_CODE, VM_LAYOUT_FIXED_CODE_BASE, VM_LAYOUT_FIXED_CODE_SIZE, ".code region policy should exist");
    failures += expect_policy_region(&policy, VM_LAYOUT_REGION_DATA, VM_LAYOUT_FIXED_DATA_BASE, VM_LAYOUT_FIXED_DATA_SIZE, ".data region policy should exist");
    failures += expect_policy_region(&policy, VM_LAYOUT_REGION_CONST, VM_LAYOUT_FIXED_CONST_BASE, VM_LAYOUT_FIXED_CONST_SIZE, ".const region policy should exist");
    failures += expect_policy_region(&policy, VM_LAYOUT_REGION_HEAP, VM_LAYOUT_FIXED_HEAP_BASE, VM_LAYOUT_FIXED_HEAP_SIZE, ".heap region policy should exist");
    failures += expect_policy_region(&policy, VM_LAYOUT_REGION_STACK, VM_LAYOUT_FIXED_STACK_TOP - VM_LAYOUT_FIXED_STACK_SIZE, VM_LAYOUT_FIXED_STACK_SIZE, ".stack region policy should exist");

    if (strcmp(vm_layout_mode_name(VM_LAYOUT_MODE_FIXED), "fixed") != 0) {
        failures += record_failure("fixed layout mode name should be stable");
    }
    if (strcmp(vm_layout_safety_tier_name(VM_LAYOUT_SAFETY_TIER_BEGINNER), "beginner") != 0) {
        failures += record_failure("beginner safety tier name should be stable");
    }
    if (vm_layout_mode_name((VmLayoutMode)99) != NULL) {
        failures += record_failure("invalid layout mode name should return NULL");
    }
    if (vm_layout_safety_tier_name((VmLayoutSafetyTier)99) != NULL) {
        failures += record_failure("invalid safety tier name should return NULL");
    }

    return failures;
}

/// Verifies invalid policy shapes are rejected before memory initialization.
///
/// @return Number of failures.
static int test_invalid_layout_policies_are_rejected(void) {
    int failures = 0;
    VmLayoutPolicy policy = vm_layout_default_policy();
    uint32_t size = 0U;

    failures += expect_bool(vm_layout_policy_is_valid(NULL), false, "NULL policy should be invalid");
    failures += expect_bool(vm_layout_policy_get_region(NULL, VM_LAYOUT_REGION_DATA) == NULL, true, "NULL policy region lookup should fail");
    failures += expect_bool(vm_layout_policy_get_region(&policy, (VmLayoutRegionKind)99) == NULL, true, "invalid region lookup should fail");
    failures += expect_bool(vm_layout_region_size(NULL, &size), false, "NULL region size lookup should fail");
    failures += expect_bool(vm_layout_region_size(&policy.regions[VM_LAYOUT_REGION_DATA], NULL), false, "NULL size output should fail");

    policy = vm_layout_default_policy();
    policy.mode = (VmLayoutMode)99;
    failures += expect_bool(vm_layout_policy_is_valid(&policy), false, "unsupported layout mode should be invalid");

    policy = vm_layout_default_policy();
    policy.safety_tier = (VmLayoutSafetyTier)99;
    failures += expect_bool(vm_layout_policy_is_valid(&policy), false, "invalid safety tier should be invalid");

    policy = vm_layout_default_policy();
    policy.region_alignment = 0U;
    failures += expect_bool(vm_layout_policy_is_valid(&policy), false, "zero alignment should be invalid");

    policy = vm_layout_default_policy();
    policy.regions[VM_LAYOUT_REGION_DATA].base = VM_LAYOUT_FIXED_CODE_BASE;
    failures += expect_bool(vm_layout_policy_is_valid(&policy), false, "overlapping fixed regions should be invalid");

    policy = vm_layout_default_policy();
    policy.regions[VM_LAYOUT_REGION_DATA].limit = policy.regions[VM_LAYOUT_REGION_DATA].base;
    failures += expect_bool(vm_layout_policy_is_valid(&policy), false, "empty region should be invalid");

    policy = vm_layout_default_policy();
    policy.maximum_total_reservation_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = 1U;
    failures += expect_bool(vm_layout_policy_is_valid(&policy), false, "too-small total reservation limit should be invalid");

    return failures;
}

/// Verifies memory initialization consumes the layout policy and preserves fixed regions.
///
/// @return Number of failures.
static int test_memory_initialization_uses_layout_policy(void) {
    int failures = 0;
    VmMemory memory;
    VmLayoutPolicy policy = vm_layout_default_policy();
    const VmMemoryRegion *region = NULL;

    if (vm_memory_init_with_layout_policy(&memory, &policy) != VM_MEMORY_STATUS_OK) {
        return record_failure("memory should initialize from default layout policy");
    }

    region = vm_memory_get_region(&memory, VM_MEMORY_REGION_DATA);
    if (region == NULL) {
        failures += record_failure("data region should exist after policy init");
    } else {
        failures += expect_u32(region->base, VM_LAYOUT_FIXED_DATA_BASE, "policy init should preserve data base");
        failures += expect_u32(region->size, VM_LAYOUT_FIXED_DATA_SIZE, "policy init should preserve data size");
    }

    region = vm_memory_get_region(&memory, VM_MEMORY_REGION_CONST);
    if (region == NULL) {
        failures += record_failure("const region should exist after policy init");
    } else {
        failures += expect_u32(region->base, VM_LAYOUT_FIXED_CONST_BASE, "policy init should preserve const base");
        failures += expect_bool(vm_memory_region_has_permission(region, VM_MEMORY_PERMISSION_WRITE), false, "policy init should preserve const read-only permissions");
    }

    vm_memory_deinit(&memory);

    policy = vm_layout_default_policy();
    policy.regions[VM_LAYOUT_REGION_HEAP].base = VM_LAYOUT_FIXED_DATA_BASE;
    failures += expect_bool(vm_memory_init_with_layout_policy(&memory, &policy) == VM_MEMORY_STATUS_INVALID_ARGUMENT, true, "invalid policy should not initialize memory");

    return failures;
}

/// Verifies VM initialization can select the explicit fixed layout policy.
///
/// @return Number of failures.
static int test_vm_initialization_accepts_layout_policy(void) {
    int failures = 0;
    Vm vm;
    VmLayoutPolicy policy = vm_layout_default_policy();
    const VmMemoryRegion *stack_region = NULL;

    if (vm_init_with_layout_policy(&vm, &policy) != VM_EXEC_STATUS_OK) {
        return record_failure("VM should initialize from default layout policy");
    }

    stack_region = vm_memory_get_region(&vm.memory, VM_MEMORY_REGION_STACK);
    if (stack_region == NULL) {
        failures += record_failure("VM stack region should exist");
    } else {
        failures += expect_u32(stack_region->base, VM_LAYOUT_FIXED_STACK_TOP - VM_LAYOUT_FIXED_STACK_SIZE, "VM policy init should preserve stack base");
        failures += expect_u32(stack_region->size, VM_LAYOUT_FIXED_STACK_SIZE, "VM policy init should preserve stack size");
    }

    vm_deinit(&vm);
    failures += expect_bool(vm_init_with_layout_policy(NULL, &policy) == VM_EXEC_STATUS_INVALID_ARGUMENT, true, "VM policy init should reject NULL VM");

    return failures;
}

/// Test entry point for Phase 32 layout-policy tests.
///
/// @return Zero when all tests pass, otherwise one.
int main(void) {
    int failures = 0;

    failures += test_default_layout_policy_values();
    failures += test_invalid_layout_policies_are_rejected();
    failures += test_memory_initialization_uses_layout_policy();
    failures += test_vm_initialization_accepts_layout_policy();

    if (failures != 0) {
        fprintf(stderr, "Phase 32 layout policy tests failed: %d failure(s)\n", failures);
        return 1;
    }

    puts("Phase 32 layout policy tests passed.");
    return 0;
}
