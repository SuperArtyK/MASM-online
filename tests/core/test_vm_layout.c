/*
 * @file test_vm_layout.c
 * @brief Unit tests for the Phase 33 memory layout policy object.
 *
 * These tests verify that the explicit layout policy preserves the fixed
 * educational memory layout exactly while adding automatic deterministic sizing
 * for test/configuration-selected layout policies.
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


/// Verifies automatic deterministic sizing uses aligned minimum regions for tiny programs.
///
/// @return Number of failures.
static int test_automatic_layout_uses_minimum_aligned_sizes(void) {
    int failures = 0;
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy policy;
    VmLayoutDiagnostic diagnostic;
    uint32_t size = 0U;

    memset(&metadata, 0, sizeof(metadata));
    memset(&policy, 0, sizeof(policy));
    memset(&diagnostic, 0, sizeof(diagnostic));

    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(NULL, &metadata, &policy, &diagnostic), (uint32_t)VM_LAYOUT_STATUS_OK, "automatic tiny layout should build");
    failures += expect_u32((uint32_t)policy.mode, (uint32_t)VM_LAYOUT_MODE_AUTOMATIC, "automatic policy should report automatic mode");
    failures += expect_bool(vm_layout_policy_is_valid(&policy), true, "automatic tiny policy should validate");
    failures += expect_bool(vm_layout_region_size(&policy.regions[VM_LAYOUT_REGION_CODE], &size), true, "automatic code size should be readable");
    failures += expect_u32(size, VM_LAYOUT_DEFAULT_REGION_ALIGNMENT, "automatic code region should use aligned minimum");
    failures += expect_bool(vm_layout_region_size(&policy.regions[VM_LAYOUT_REGION_DATA], &size), true, "automatic data size should be readable");
    failures += expect_u32(size, VM_LAYOUT_DEFAULT_REGION_ALIGNMENT, "automatic data region should use aligned minimum");
    failures += expect_bool(vm_layout_region_size(&policy.regions[VM_LAYOUT_REGION_CONST], &size), true, "automatic const size should be readable");
    failures += expect_u32(size, VM_LAYOUT_DEFAULT_REGION_ALIGNMENT, "automatic const region should use aligned minimum");
    failures += expect_bool(strcmp(vm_layout_mode_name(VM_LAYOUT_MODE_AUTOMATIC), "automatic") == 0, true, "automatic mode name should be stable");
    failures += expect_bool(strcmp(vm_layout_status_name(VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED), "resource-limit-exceeded") == 0, true, "resource-limit status name should be stable");

    return failures;
}

/// Verifies automatic deterministic sizing grows regions from metadata and is repeatable.
///
/// @return Number of failures.
static int test_automatic_layout_grows_deterministically_from_metadata(void) {
    int failures = 0;
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy first;
    VmLayoutPolicy second;
    uint32_t data_size = 0U;
    uint32_t const_size = 0U;

    memset(&metadata, 0, sizeof(metadata));
    metadata.code_size = 2U;
    metadata.initialized_data_size = 5000U;
    metadata.uninitialized_data_size = 100U;
    metadata.const_size = 13U;
    metadata.has_heap_size_request = true;
    metadata.heap_size_request = 1U;
    metadata.has_stack_size_request = true;
    metadata.stack_size_request = 1U;

    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(NULL, &metadata, &first, NULL), (uint32_t)VM_LAYOUT_STATUS_OK, "first automatic layout should build");
    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(NULL, &metadata, &second, NULL), (uint32_t)VM_LAYOUT_STATUS_OK, "second automatic layout should build");
    failures += expect_bool(vm_layout_region_size(&first.regions[VM_LAYOUT_REGION_DATA], &data_size), true, "automatic data size should be readable");
    failures += expect_u32(data_size, 8192U, "automatic data region should round combined .data/.DATA? bytes up to alignment");
    failures += expect_bool(vm_layout_region_size(&first.regions[VM_LAYOUT_REGION_CONST], &const_size), true, "automatic const size should be readable");
    failures += expect_u32(const_size, VM_LAYOUT_DEFAULT_REGION_ALIGNMENT, "automatic const region should use aligned minimum for tiny const image");
    failures += expect_u32(first.regions[VM_LAYOUT_REGION_DATA].base, second.regions[VM_LAYOUT_REGION_DATA].base, "automatic data base should be deterministic");
    failures += expect_u32(first.regions[VM_LAYOUT_REGION_DATA].limit, second.regions[VM_LAYOUT_REGION_DATA].limit, "automatic data limit should be deterministic");
    failures += expect_u32(first.regions[VM_LAYOUT_REGION_STACK].base, second.regions[VM_LAYOUT_REGION_STACK].base, "automatic stack base should be deterministic");

    return failures;
}

/// Verifies automatic layout reports per-region resource-limit failures.
///
/// @return Number of failures.
static int test_automatic_layout_rejects_region_resource_limit(void) {
    int failures = 0;
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy automatic_policy;
    VmLayoutDiagnostic diagnostic;

    memset(&metadata, 0, sizeof(metadata));
    memset(&automatic_policy, 0, sizeof(automatic_policy));
    memset(&diagnostic, 0, sizeof(diagnostic));

    base_policy.regions[VM_LAYOUT_REGION_DATA].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = 4096U;
    metadata.initialized_data_size = 4097U;
    metadata.has_heap_size_request = true;
    metadata.heap_size_request = 1U;
    metadata.has_stack_size_request = true;
    metadata.stack_size_request = 1U;

    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(&base_policy, &metadata, &automatic_policy, &diagnostic), (uint32_t)VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, "oversized automatic data region should fail");
    failures += expect_bool(diagnostic.has_region, true, "region resource diagnostic should identify a region");
    failures += expect_u32((uint32_t)diagnostic.region, (uint32_t)VM_LAYOUT_REGION_DATA, "resource diagnostic should identify data region");
    failures += expect_u32(diagnostic.requested_size, 8192U, "resource diagnostic should report aligned data size");
    failures += expect_u32(diagnostic.limit, 4096U, "resource diagnostic should report data limit");

    return failures;
}

/// Verifies heap and stack requests that would overlap are reported as resource limits.
///
/// @return Number of failures.
static int test_automatic_layout_rejects_heap_stack_overlap_resource_limits(void) {
    int failures = 0;
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy automatic_policy;
    VmLayoutDiagnostic diagnostic;

    memset(&metadata, 0, sizeof(metadata));
    memset(&automatic_policy, 0, sizeof(automatic_policy));
    memset(&diagnostic, 0, sizeof(diagnostic));

    metadata.has_heap_size_request = true;
    metadata.heap_size_request = VM_LAYOUT_FIXED_STACK_TOP - VM_LAYOUT_FIXED_HEAP_BASE;
    metadata.has_stack_size_request = true;
    metadata.stack_size_request = 1U;

    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(&base_policy, &metadata, &automatic_policy, &diagnostic), (uint32_t)VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, "automatic heap request overlapping stack should fail as resource limit");
    failures += expect_bool(diagnostic.has_region, true, "heap overlap diagnostic should identify a region");
    failures += expect_u32((uint32_t)diagnostic.region, (uint32_t)VM_LAYOUT_REGION_HEAP, "heap overlap diagnostic should identify heap region");
    failures += expect_u32(diagnostic.requested_size, VM_LAYOUT_FIXED_STACK_TOP - VM_LAYOUT_FIXED_HEAP_BASE, "heap overlap diagnostic should report aligned heap request");
    failures += expect_u32(diagnostic.limit, VM_LAYOUT_FIXED_STACK_TOP - VM_LAYOUT_DEFAULT_REGION_ALIGNMENT - VM_LAYOUT_FIXED_HEAP_BASE, "heap overlap diagnostic should report maximum heap before stack");

    memset(&metadata, 0, sizeof(metadata));
    memset(&automatic_policy, 0, sizeof(automatic_policy));
    memset(&diagnostic, 0, sizeof(diagnostic));

    metadata.has_heap_size_request = true;
    metadata.heap_size_request = 1U;
    metadata.has_stack_size_request = true;
    metadata.stack_size_request = VM_LAYOUT_FIXED_STACK_TOP;

    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(&base_policy, &metadata, &automatic_policy, &diagnostic), (uint32_t)VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, "automatic stack request overlapping heap should fail as resource limit");
    failures += expect_bool(diagnostic.has_region, true, "stack overlap diagnostic should identify a region");
    failures += expect_u32((uint32_t)diagnostic.region, (uint32_t)VM_LAYOUT_REGION_STACK, "stack overlap diagnostic should identify stack region");
    failures += expect_u32(diagnostic.requested_size, VM_LAYOUT_FIXED_STACK_TOP, "stack overlap diagnostic should report aligned stack request");
    failures += expect_u32(diagnostic.limit, VM_LAYOUT_FIXED_STACK_TOP - (VM_LAYOUT_FIXED_HEAP_BASE + VM_LAYOUT_DEFAULT_REGION_ALIGNMENT), "stack overlap diagnostic should report maximum stack after heap");

    return failures;
}

/// Verifies automatic layout reports total reservation resource-limit failures.
///
/// @return Number of failures.
static int test_automatic_layout_rejects_total_resource_limit(void) {
    int failures = 0;
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy automatic_policy;
    VmLayoutDiagnostic diagnostic;

    memset(&metadata, 0, sizeof(metadata));
    memset(&automatic_policy, 0, sizeof(automatic_policy));
    memset(&diagnostic, 0, sizeof(diagnostic));

    base_policy.maximum_total_reservation_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = 12288U;
    metadata.has_heap_size_request = true;
    metadata.heap_size_request = 1U;
    metadata.has_stack_size_request = true;
    metadata.stack_size_request = 1U;

    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(&base_policy, &metadata, &automatic_policy, &diagnostic), (uint32_t)VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, "oversized automatic total reservation should fail");
    failures += expect_bool(diagnostic.has_region, false, "total resource diagnostic should not identify a single region");
    failures += expect_u32(diagnostic.total_size, 20480U, "total resource diagnostic should report computed total");
    failures += expect_u32(diagnostic.total_limit, 12288U, "total resource diagnostic should report total limit");

    return failures;
}

/// Verifies automatic layout detects base-plus-size overflow before allocation.
///
/// @return Number of failures.
static int test_automatic_layout_rejects_address_overflow(void) {
    int failures = 0;
    VmLayoutPolicy base_policy = vm_layout_default_policy();
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy automatic_policy;
    VmLayoutDiagnostic diagnostic;

    memset(&metadata, 0, sizeof(metadata));
    memset(&automatic_policy, 0, sizeof(automatic_policy));
    memset(&diagnostic, 0, sizeof(diagnostic));

    base_policy.regions[VM_LAYOUT_REGION_CODE].base = UINT32_MAX - 10U;
    metadata.code_size = 20U;
    metadata.has_heap_size_request = true;
    metadata.heap_size_request = 1U;
    metadata.has_stack_size_request = true;
    metadata.stack_size_request = 1U;

    failures += expect_u32((uint32_t)vm_layout_build_automatic_policy(&base_policy, &metadata, &automatic_policy, &diagnostic), (uint32_t)VM_LAYOUT_STATUS_INTEGER_OVERFLOW, "automatic code range overflow should fail");
    failures += expect_bool(diagnostic.has_region, true, "overflow diagnostic should identify a region");
    failures += expect_u32((uint32_t)diagnostic.region, (uint32_t)VM_LAYOUT_REGION_CODE, "overflow diagnostic should identify code region");

    return failures;
}

/// Verifies automatically sized memory does not grow after load and preserves permissions.
///
/// @return Number of failures.
static int test_automatic_memory_initialization_preserves_bounds_and_permissions(void) {
    int failures = 0;
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy policy;
    VmMemory memory;
    VmMemoryDiagnostic memory_diagnostic;
    uint8_t byte = 0U;

    memset(&metadata, 0, sizeof(metadata));
    metadata.initialized_data_size = 1U;
    metadata.const_size = 1U;
    metadata.has_heap_size_request = true;
    metadata.heap_size_request = 1U;
    metadata.has_stack_size_request = true;
    metadata.stack_size_request = 1U;

    if (vm_layout_build_automatic_policy(NULL, &metadata, &policy, NULL) != VM_LAYOUT_STATUS_OK) {
        return record_failure("automatic memory policy should build");
    }
    if (vm_memory_init_with_layout_policy(&memory, &policy) != VM_MEMORY_STATUS_OK) {
        return record_failure("memory should initialize with automatic policy");
    }

    failures += expect_u32((uint32_t)vm_memory_read_u8(&memory, VM_LAYOUT_FIXED_DATA_BASE + VM_LAYOUT_DEFAULT_REGION_ALIGNMENT, &byte, &memory_diagnostic), (uint32_t)VM_MEMORY_STATUS_INVALID_ADDRESS, "automatic data region should not grow on invalid access");
    failures += expect_u32((uint32_t)vm_memory_write_u8(&memory, VM_LAYOUT_FIXED_CONST_BASE, 1U, &memory_diagnostic), (uint32_t)VM_MEMORY_STATUS_PERMISSION_DENIED, "automatic const region should remain read-only");

    vm_memory_deinit(&memory);
    return failures;
}

/// Test entry point for Phase 33 layout-policy tests.
///
/// @return Zero when all tests pass, otherwise one.
int main(void) {
    int failures = 0;

    failures += test_default_layout_policy_values();
    failures += test_invalid_layout_policies_are_rejected();
    failures += test_memory_initialization_uses_layout_policy();
    failures += test_vm_initialization_accepts_layout_policy();
    failures += test_automatic_layout_uses_minimum_aligned_sizes();
    failures += test_automatic_layout_grows_deterministically_from_metadata();
    failures += test_automatic_layout_rejects_region_resource_limit();
    failures += test_automatic_layout_rejects_heap_stack_overlap_resource_limits();
    failures += test_automatic_layout_rejects_total_resource_limit();
    failures += test_automatic_layout_rejects_address_overflow();
    failures += test_automatic_memory_initialization_preserves_bounds_and_permissions();

    if (failures != 0) {
        fprintf(stderr, "Phase 33 layout policy tests failed: %d failure(s)\n", failures);
        return 1;
    }

    puts("Phase 33 layout policy tests passed.");
    return 0;
}
