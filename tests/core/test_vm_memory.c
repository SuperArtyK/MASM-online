/*
 * @file test_vm_memory.c
 * @brief Unit tests for the Milestone 3 simulated memory region model.
 *
 * These tests cover deterministic memory layout, checked reads and writes,
 * region permissions, invalid address reporting, unaligned-access warnings, raw
 * byte-change recording, and Milestone 27 read-only .const region behavior
 * without introducing parser or instruction execution.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_memory.h"

/// Records a memory test failure with file-local context.
///
/// @param message Human-readable failure description.
/// @return Always returns 1 so callers can accumulate failures.
static int record_failure(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

/// Verifies that a status has the expected value.
///
/// @param actual Status returned by the operation under test.
/// @param expected Expected status value.
/// @param message Failure message emitted when the status differs.
/// @return Zero on success, otherwise one failure.
static int expect_status(VmMemoryStatus actual, VmMemoryStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %s, got %s)\n", message, vm_memory_status_name(expected), vm_memory_status_name(actual));
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

/// Verifies that a 64-bit value has the expected value.
///
/// @param actual Actual value.
/// @param expected Expected value.
/// @param message Failure message emitted when the value differs.
/// @return Zero on success, otherwise one failure.
static int expect_u64(uint64_t actual, uint64_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }

    return 0;
}

/// Verifies that a size value has the expected value.
///
/// @param actual Actual size value.
/// @param expected Expected size value.
/// @param message Failure message emitted when the value differs.
/// @return Zero on success, otherwise one failure.
static int expect_size(size_t actual, size_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (expected %lu, got %lu)\n", message, (unsigned long)expected, (unsigned long)actual);
        return 1;
    }

    return 0;
}

/// Verifies metadata for one initialized memory region.
///
/// @param memory Memory object to inspect.
/// @param kind Region kind to check.
/// @param expected_base Expected inclusive base address.
/// @param expected_size Expected region size.
/// @param can_read Expected read permission.
/// @param can_write Expected write permission.
/// @param can_execute Expected execute permission.
/// @return Zero on success, otherwise a positive failure count.
static int expect_region_metadata(
    const VmMemory *memory,
    VmMemoryRegionKind kind,
    uint32_t expected_base,
    uint32_t expected_size,
    bool can_read,
    bool can_write,
    bool can_execute
) {
    int failures = 0;
    const VmMemoryRegion *region = vm_memory_get_region(memory, kind);

    if (region == NULL) {
        return record_failure("region metadata should exist");
    }

    failures += expect_u32(region->base, expected_base, "region base mismatch");
    failures += expect_u32(region->size, expected_size, "region size mismatch");
    failures += expect_bool(vm_memory_region_has_permission(region, VM_MEMORY_PERMISSION_READ), can_read, "read permission mismatch");
    failures += expect_bool(vm_memory_region_has_permission(region, VM_MEMORY_PERMISSION_WRITE), can_write, "write permission mismatch");
    failures += expect_bool(vm_memory_region_has_permission(region, VM_MEMORY_PERMISSION_EXECUTE), can_execute, "execute permission mismatch");

    return failures;
}

/// Verifies default region layout, permissions, and metadata helpers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_default_layout_and_metadata(void) {
    int failures = 0;
    VmMemory memory;
    VmMemoryConfig config = vm_memory_default_config();
    uint32_t stack_base = VM_MEMORY_DEFAULT_STACK_TOP - VM_MEMORY_DEFAULT_STACK_SIZE;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "default memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    failures += expect_u32(config.code_size, VM_MEMORY_DEFAULT_CODE_SIZE, "default code size should match constant");
    failures += expect_u32(config.data_size, VM_MEMORY_DEFAULT_DATA_SIZE, "default data size should match constant");
    failures += expect_u32(config.const_size, VM_MEMORY_DEFAULT_CONST_SIZE, "default const size should match constant");
    failures += expect_u32(config.heap_size, VM_MEMORY_DEFAULT_HEAP_SIZE, "default heap size should match constant");
    failures += expect_u32(config.stack_size, VM_MEMORY_DEFAULT_STACK_SIZE, "default stack size should match constant");

    failures += expect_region_metadata(&memory, VM_MEMORY_REGION_CODE, VM_MEMORY_DEFAULT_CODE_BASE, VM_MEMORY_DEFAULT_CODE_SIZE, true, false, true);
    failures += expect_region_metadata(&memory, VM_MEMORY_REGION_DATA, VM_MEMORY_DEFAULT_DATA_BASE, VM_MEMORY_DEFAULT_DATA_SIZE, true, true, false);
    failures += expect_region_metadata(&memory, VM_MEMORY_REGION_CONST, VM_MEMORY_DEFAULT_CONST_BASE, VM_MEMORY_DEFAULT_CONST_SIZE, true, false, false);
    failures += expect_region_metadata(&memory, VM_MEMORY_REGION_HEAP, VM_MEMORY_DEFAULT_HEAP_BASE, VM_MEMORY_DEFAULT_HEAP_SIZE, true, true, false);
    failures += expect_region_metadata(&memory, VM_MEMORY_REGION_STACK, stack_base, VM_MEMORY_DEFAULT_STACK_SIZE, true, true, false);

    if (strcmp(vm_memory_region_name(VM_MEMORY_REGION_DATA), ".data") != 0) {
        failures += record_failure("region name helper should return .data");
    }

    if (strcmp(vm_memory_region_name(VM_MEMORY_REGION_CONST), ".const") != 0) {
        failures += record_failure("region name helper should return .const");
    }

    if (vm_memory_region_name((VmMemoryRegionKind)-1) != NULL) {
        failures += record_failure("invalid region name should return NULL");
    }

    if (vm_memory_status_name((VmMemoryStatus)-1) != NULL) {
        failures += record_failure("invalid status name should return NULL");
    }

    if (vm_memory_get_region(&memory, (VmMemoryRegionKind)-1) != NULL) {
        failures += record_failure("invalid region lookup should return NULL");
    }

    vm_memory_deinit(&memory);
    vm_memory_deinit(NULL);

    return failures;
}

/// Verifies little-endian data reads, writes, and raw byte storage.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_data_read_write_little_endian(void) {
    int failures = 0;
    VmMemory memory;
    uint8_t value8 = 0U;
    uint16_t value16 = 0U;
    uint32_t value32 = 0U;
    uint64_t value64 = 0U;
    uint32_t base = VM_MEMORY_DEFAULT_DATA_BASE;
    const VmMemoryRegion *data_region = NULL;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    data_region = vm_memory_get_region(&memory, VM_MEMORY_REGION_DATA);
    if (data_region == NULL) {
        vm_memory_deinit(&memory);
        return record_failure("data region should exist");
    }

    failures += expect_status(vm_memory_write_u8(&memory, base, 0x12U, NULL), VM_MEMORY_STATUS_OK, "u8 write should succeed");
    failures += expect_status(vm_memory_read_u8(&memory, base, &value8, NULL), VM_MEMORY_STATUS_OK, "u8 read should succeed");
    failures += expect_u32(value8, 0x12U, "u8 value should round trip");

    failures += expect_status(vm_memory_write_u16(&memory, base + 2U, 0x3456U, NULL), VM_MEMORY_STATUS_OK, "u16 write should succeed");
    failures += expect_status(vm_memory_read_u16(&memory, base + 2U, &value16, NULL), VM_MEMORY_STATUS_OK, "u16 read should succeed");
    failures += expect_u32(value16, 0x3456U, "u16 value should round trip");
    failures += expect_u32(data_region->bytes[2], 0x56U, "u16 low byte should be stored first");
    failures += expect_u32(data_region->bytes[3], 0x34U, "u16 high byte should be stored second");

    failures += expect_status(vm_memory_write_u32(&memory, base + 4U, 0x78563412U, NULL), VM_MEMORY_STATUS_OK, "u32 write should succeed");
    failures += expect_status(vm_memory_read_u32(&memory, base + 4U, &value32, NULL), VM_MEMORY_STATUS_OK, "u32 read should succeed");
    failures += expect_u32(value32, 0x78563412U, "u32 value should round trip");
    failures += expect_u32(data_region->bytes[4], 0x12U, "u32 byte 0 should be little endian");
    failures += expect_u32(data_region->bytes[7], 0x78U, "u32 byte 3 should be little endian");

    failures += expect_status(vm_memory_write_u64(&memory, base + 8U, 0x1122334455667788ULL, NULL), VM_MEMORY_STATUS_OK, "u64 write should succeed");
    failures += expect_status(vm_memory_read_u64(&memory, base + 8U, &value64, NULL), VM_MEMORY_STATUS_OK, "u64 read should succeed");
    failures += expect_u64(value64, 0x1122334455667788ULL, "u64 value should round trip");
    failures += expect_u32(data_region->bytes[8], 0x88U, "u64 byte 0 should be little endian");
    failures += expect_u32(data_region->bytes[15], 0x11U, "u64 byte 7 should be little endian");

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies valid heap and stack accesses through the checked helpers.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_heap_and_stack_valid_access(void) {
    int failures = 0;
    VmMemory memory;
    uint32_t value32 = 0U;
    const VmMemoryRegion *heap_region = NULL;
    const VmMemoryRegion *stack_region = NULL;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    heap_region = vm_memory_get_region(&memory, VM_MEMORY_REGION_HEAP);
    stack_region = vm_memory_get_region(&memory, VM_MEMORY_REGION_STACK);
    if (heap_region == NULL || stack_region == NULL) {
        vm_memory_deinit(&memory);
        return record_failure("heap and stack regions should exist");
    }

    failures += expect_status(vm_memory_write_u32(&memory, heap_region->base, 0xAABBCCDDU, NULL), VM_MEMORY_STATUS_OK, "heap u32 write should succeed");
    failures += expect_status(vm_memory_read_u32(&memory, heap_region->base, &value32, NULL), VM_MEMORY_STATUS_OK, "heap u32 read should succeed");
    failures += expect_u32(value32, 0xAABBCCDDU, "heap value should round trip");

    failures += expect_status(vm_memory_write_u32(&memory, stack_region->base + stack_region->size - 4U, 0x01020304U, NULL), VM_MEMORY_STATUS_OK, "stack top-adjacent u32 write should succeed");
    failures += expect_status(vm_memory_read_u32(&memory, stack_region->base + stack_region->size - 4U, &value32, NULL), VM_MEMORY_STATUS_OK, "stack top-adjacent u32 read should succeed");
    failures += expect_u32(value32, 0x01020304U, "stack value should round trip");

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies code-region permission behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_code_write_fails_and_read_succeeds(void) {
    int failures = 0;
    VmMemory memory;
    VmMemoryDiagnostic diagnostic;
    uint32_t value32 = 0xFFFFFFFFU;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    failures += expect_status(vm_memory_read_u32(&memory, VM_MEMORY_DEFAULT_CODE_BASE, &value32, &diagnostic), VM_MEMORY_STATUS_OK, "code read should succeed");
    failures += expect_u32(value32, 0U, "code region should initialize to zero");
    failures += expect_bool(diagnostic.has_region, true, "code read diagnostic should identify region");
    failures += expect_u32((uint32_t)diagnostic.region, (uint32_t)VM_MEMORY_REGION_CODE, "code read diagnostic region mismatch");

    failures += expect_status(vm_memory_write_u32(&memory, VM_MEMORY_DEFAULT_CODE_BASE, 0xDEADBEEFU, &diagnostic), VM_MEMORY_STATUS_PERMISSION_DENIED, "code write should fail permission check");
    failures += expect_bool(diagnostic.has_region, true, "code write diagnostic should identify region");
    failures += expect_u32((uint32_t)diagnostic.access_type, (uint32_t)VM_MEMORY_ACCESS_WRITE, "code write diagnostic access type mismatch");
    failures += expect_size(vm_memory_change_count(&memory), 0U, "failed code write should not record changes");

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies invalid address and range-crossing diagnostics.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_invalid_address_errors_are_structured(void) {
    int failures = 0;
    VmMemory memory;
    VmMemoryDiagnostic diagnostic;
    uint8_t value8 = 0xABU;
    uint16_t value16 = 0xABCDU;
    const VmMemoryRegion *data_region = NULL;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    data_region = vm_memory_get_region(&memory, VM_MEMORY_REGION_DATA);
    if (data_region == NULL) {
        vm_memory_deinit(&memory);
        return record_failure("data region should exist");
    }

    failures += expect_status(vm_memory_read_u8(&memory, VM_MEMORY_DEFAULT_CODE_BASE - 1U, &value8, &diagnostic), VM_MEMORY_STATUS_INVALID_ADDRESS, "read before code base should fail");
    failures += expect_bool(diagnostic.has_region, false, "read before code base should not identify a region");
    failures += expect_u32(diagnostic.address, VM_MEMORY_DEFAULT_CODE_BASE - 1U, "invalid read diagnostic address mismatch");
    failures += expect_u32(diagnostic.size, 1U, "invalid read diagnostic size mismatch");
    failures += expect_u32(value8, 0xABU, "failed read should not overwrite output value");

    failures += expect_status(vm_memory_read_u16(&memory, data_region->base + data_region->size - 1U, &value16, &diagnostic), VM_MEMORY_STATUS_INVALID_ADDRESS, "range crossing data end should fail");
    failures += expect_bool(diagnostic.has_region, true, "range crossing should identify starting region");
    failures += expect_u32((uint32_t)diagnostic.region, (uint32_t)VM_MEMORY_REGION_DATA, "range crossing diagnostic region mismatch");
    failures += expect_u32(value16, 0xABCDU, "failed range read should not overwrite output value");

    failures += expect_status(vm_memory_write_u16(&memory, data_region->base + data_region->size - 1U, 0x1234U, &diagnostic), VM_MEMORY_STATUS_INVALID_ADDRESS, "range crossing write should fail");
    failures += expect_size(vm_memory_change_count(&memory), 0U, "failed range write should not record changes");

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies unaligned multi-byte accesses succeed with warning status.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_unaligned_access_succeeds_with_warning(void) {
    int failures = 0;
    VmMemory memory;
    VmMemoryDiagnostic diagnostic;
    uint32_t value32 = 0U;
    uint32_t address = VM_MEMORY_DEFAULT_DATA_BASE + 1U;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    failures += expect_status(vm_memory_write_u32(&memory, address, 0x12345678U, &diagnostic), VM_MEMORY_STATUS_OK_UNALIGNED, "unaligned DWORD write should succeed with warning");
    failures += expect_bool(diagnostic.is_unaligned, true, "unaligned write diagnostic should mark warning");
    failures += expect_bool(diagnostic.has_region, true, "unaligned write diagnostic should identify region");
    failures += expect_u32((uint32_t)diagnostic.region, (uint32_t)VM_MEMORY_REGION_DATA, "unaligned write region mismatch");

    failures += expect_status(vm_memory_read_u32(&memory, address, &value32, &diagnostic), VM_MEMORY_STATUS_OK_UNALIGNED, "unaligned DWORD read should succeed with warning");
    failures += expect_bool(diagnostic.is_unaligned, true, "unaligned read diagnostic should mark warning");
    failures += expect_u32(value32, 0x12345678U, "unaligned DWORD value should round trip");

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies boundary access at the last valid byte of a region.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_region_boundary_edge_cases(void) {
    int failures = 0;
    VmMemory memory;
    uint8_t value8 = 0U;
    const VmMemoryRegion *data_region = NULL;
    uint32_t last_data_byte = 0U;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    data_region = vm_memory_get_region(&memory, VM_MEMORY_REGION_DATA);
    if (data_region == NULL) {
        vm_memory_deinit(&memory);
        return record_failure("data region should exist");
    }

    last_data_byte = data_region->base + data_region->size - 1U;
    failures += expect_status(vm_memory_write_u8(&memory, last_data_byte, 0x5AU, NULL), VM_MEMORY_STATUS_OK, "last data byte write should succeed");
    failures += expect_status(vm_memory_read_u8(&memory, last_data_byte, &value8, NULL), VM_MEMORY_STATUS_OK, "last data byte read should succeed");
    failures += expect_u32(value8, 0x5AU, "last data byte should round trip");
    failures += expect_status(vm_memory_write_u16(&memory, last_data_byte, 0x1234U, NULL), VM_MEMORY_STATUS_INVALID_ADDRESS, "u16 at last byte should fail");

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies NULL argument and invalid configuration handling.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_invalid_arguments_and_config(void) {
    int failures = 0;
    VmMemory memory;
    VmMemoryConfig config = vm_memory_default_config();
    uint8_t value8 = 0x11U;
    uint16_t value16 = 0x2222U;
    uint32_t value32 = 0x33333333U;
    uint64_t value64 = 0x4444444444444444ULL;

    failures += expect_status(vm_memory_init(NULL, NULL), VM_MEMORY_STATUS_INVALID_ARGUMENT, "init should reject NULL memory");

    config.data_size = 0U;
    failures += expect_status(vm_memory_init(&memory, &config), VM_MEMORY_STATUS_INVALID_ARGUMENT, "init should reject zero data size");

    config = vm_memory_default_config();
    config.code_size = VM_MEMORY_DEFAULT_DATA_BASE - VM_MEMORY_DEFAULT_CODE_BASE + 1U;
    failures += expect_status(vm_memory_init(&memory, &config), VM_MEMORY_STATUS_INVALID_ARGUMENT, "init should reject overlapping code size");

    failures += expect_status(vm_memory_read_u8(NULL, VM_MEMORY_DEFAULT_DATA_BASE, &value8, NULL), VM_MEMORY_STATUS_INVALID_ARGUMENT, "read should reject NULL memory");
    failures += expect_status(vm_memory_read_u8(&memory, VM_MEMORY_DEFAULT_DATA_BASE, NULL, NULL), VM_MEMORY_STATUS_INVALID_ARGUMENT, "read should reject NULL output");
    failures += expect_status(vm_memory_read_u16(&memory, VM_MEMORY_DEFAULT_DATA_BASE, NULL, NULL), VM_MEMORY_STATUS_INVALID_ARGUMENT, "u16 read should reject NULL output");
    failures += expect_status(vm_memory_read_u32(&memory, VM_MEMORY_DEFAULT_DATA_BASE, NULL, NULL), VM_MEMORY_STATUS_INVALID_ARGUMENT, "u32 read should reject NULL output");
    failures += expect_status(vm_memory_read_u64(&memory, VM_MEMORY_DEFAULT_DATA_BASE, NULL, NULL), VM_MEMORY_STATUS_INVALID_ARGUMENT, "u64 read should reject NULL output");
    failures += expect_status(vm_memory_write_u8(NULL, VM_MEMORY_DEFAULT_DATA_BASE, 0U, NULL), VM_MEMORY_STATUS_INVALID_ARGUMENT, "write should reject NULL memory");

    failures += expect_u32(value8, 0x11U, "failed NULL-memory read should not mutate u8 output");
    failures += expect_u32(value16, 0x2222U, "failed NULL-output u16 baseline should remain local");
    failures += expect_u32(value32, 0x33333333U, "failed NULL-output u32 baseline should remain local");
    failures += expect_u64(value64, 0x4444444444444444ULL, "failed NULL-output u64 baseline should remain local");

    return failures;
}

/// Verifies raw byte-change recording and clear behavior.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_memory_change_recording(void) {
    int failures = 0;
    VmMemory memory;
    const VmMemoryByteChange *change = NULL;
    uint32_t base = VM_MEMORY_DEFAULT_DATA_BASE;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    failures += expect_size(vm_memory_change_count(&memory), 0U, "new memory should have no changes");
    failures += expect_status(vm_memory_write_u16(&memory, base, 0x1234U, NULL), VM_MEMORY_STATUS_OK, "u16 write should record changes");
    failures += expect_size(vm_memory_change_count(&memory), 2U, "u16 write should record two byte changes");

    change = vm_memory_get_change(&memory, 0U);
    if (change == NULL) {
        failures += record_failure("first byte change should exist");
    } else {
        failures += expect_u32(change->address, base, "first change address mismatch");
        failures += expect_u32((uint32_t)change->region, (uint32_t)VM_MEMORY_REGION_DATA, "first change region mismatch");
        failures += expect_u32(change->old_value, 0x00U, "first old byte mismatch");
        failures += expect_u32(change->new_value, 0x34U, "first new byte mismatch");
    }

    change = vm_memory_get_change(&memory, 1U);
    if (change == NULL) {
        failures += record_failure("second byte change should exist");
    } else {
        failures += expect_u32(change->address, base + 1U, "second change address mismatch");
        failures += expect_u32(change->old_value, 0x00U, "second old byte mismatch");
        failures += expect_u32(change->new_value, 0x12U, "second new byte mismatch");
    }

    failures += expect_status(vm_memory_write_u16(&memory, base, 0x1234U, NULL), VM_MEMORY_STATUS_OK, "same-value write should succeed");
    failures += expect_size(vm_memory_change_count(&memory), 2U, "same-value write should not add changes");

    if (vm_memory_get_change(&memory, 2U) != NULL) {
        failures += record_failure("out-of-range change lookup should return NULL");
    }

    vm_memory_clear_changes(&memory);
    failures += expect_size(vm_memory_change_count(&memory), 0U, "clear changes should reset count");
    vm_memory_clear_changes(NULL);

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies read-only .const region loading, reading, and write rejection.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_const_region_load_read_and_write_rejection(void) {
    int failures = 0;
    VmMemory memory;
    uint32_t value32 = 0U;
    const uint8_t bytes[4] = { 0x0AU, 0x00U, 0x00U, 0x00U };
    VmMemoryDiagnostic access_info;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    failures += expect_region_metadata(&memory, VM_MEMORY_REGION_CONST, VM_MEMORY_DEFAULT_CONST_BASE, VM_MEMORY_DEFAULT_CONST_SIZE, true, false, false);
    failures += expect_status(vm_memory_load_region_bytes(&memory, VM_MEMORY_REGION_CONST, 0U, bytes, 4U), VM_MEMORY_STATUS_OK, ".const loader should initialize read-only bytes");
    failures += expect_status(vm_memory_load_region_bytes(&memory, VM_MEMORY_REGION_CONST, VM_MEMORY_DEFAULT_CONST_SIZE - 1U, bytes, 4U), VM_MEMORY_STATUS_INVALID_ADDRESS, ".const loader should reject out-of-bounds image bytes");
    failures += expect_status(vm_memory_load_region_bytes(&memory, (VmMemoryRegionKind)-1, 0U, bytes, 4U), VM_MEMORY_STATUS_INVALID_ARGUMENT, ".const loader should reject invalid regions");
    failures += expect_status(vm_memory_load_region_bytes(&memory, VM_MEMORY_REGION_CONST, 0U, NULL, 0U), VM_MEMORY_STATUS_OK, ".const loader should allow empty NULL input");
    failures += expect_status(vm_memory_load_region_bytes(&memory, VM_MEMORY_REGION_CONST, 0U, NULL, 1U), VM_MEMORY_STATUS_INVALID_ARGUMENT, ".const loader should reject non-empty NULL input");
    failures += expect_size(vm_memory_change_count(&memory), 0U, ".const image loading should not record user memory changes");
    failures += expect_status(vm_memory_read_u32(&memory, VM_MEMORY_DEFAULT_CONST_BASE, &value32, &access_info), VM_MEMORY_STATUS_OK, ".const read should succeed");
    failures += expect_u32(value32, 10U, ".const read should decode initialized value");
    failures += expect_u32((uint32_t)access_info.region, (uint32_t)VM_MEMORY_REGION_CONST, ".const read should report const region");
    failures += expect_status(vm_memory_write_u32(&memory, VM_MEMORY_DEFAULT_CONST_BASE, 20U, &access_info), VM_MEMORY_STATUS_PERMISSION_DENIED, ".const checked write should be rejected");
    failures += expect_u32((uint32_t)access_info.region, (uint32_t)VM_MEMORY_REGION_CONST, ".const rejected write should report const region");

    vm_memory_deinit(&memory);
    return failures;
}

/// Verifies that change recording truncates without blocking valid writes.
///
/// @return Zero on success, otherwise a positive failure count.
static int test_memory_change_capacity_truncates_without_blocking_writes(void) {
    int failures = 0;
    VmMemory memory;
    size_t index = 0U;
    uint8_t value8 = 0U;
    uint32_t base = VM_MEMORY_DEFAULT_DATA_BASE;

    failures += expect_status(vm_memory_init(&memory, NULL), VM_MEMORY_STATUS_OK, "memory initialization should succeed");
    if (failures != 0) {
        return failures;
    }

    for (index = 0U; index < (size_t)VM_MEMORY_MAX_BYTE_CHANGES; index += 1U) {
        failures += expect_status(vm_memory_write_u8(&memory, base + (uint32_t)index, 0x01U, NULL), VM_MEMORY_STATUS_OK, "capacity fill byte write should succeed");
    }

    failures += expect_size(vm_memory_change_count(&memory), (size_t)VM_MEMORY_MAX_BYTE_CHANGES, "change recorder should be full");
    failures += expect_status(vm_memory_write_u8(&memory, base + VM_MEMORY_MAX_BYTE_CHANGES, 0x02U, NULL), VM_MEMORY_STATUS_OK, "write beyond change capacity should still succeed");
    failures += expect_bool(memory.change_overflowed, true, "truncated recording should set overflow flag");
    failures += expect_size(vm_memory_change_count(&memory), (size_t)VM_MEMORY_MAX_BYTE_CHANGES, "truncated recorder should retain fixed count");
    failures += expect_status(vm_memory_read_u8(&memory, base + VM_MEMORY_MAX_BYTE_CHANGES, &value8, NULL), VM_MEMORY_STATUS_OK, "read after truncated-recording write should succeed");
    failures += expect_u32(value8, 0x02U, "write should mutate memory even when change recording is full");

    vm_memory_clear_changes(&memory);
    failures += expect_bool(memory.change_overflowed, false, "clear changes should reset overflow flag");

    vm_memory_deinit(&memory);
    return failures;
}

/// Runs all memory-region tests through Milestone 27.
///
/// @return Zero when all tests pass, otherwise one.
int main(void) {
    int failures = 0;

    failures += test_default_layout_and_metadata();
    failures += test_data_read_write_little_endian();
    failures += test_heap_and_stack_valid_access();
    failures += test_code_write_fails_and_read_succeeds();
    failures += test_invalid_address_errors_are_structured();
    failures += test_unaligned_access_succeeds_with_warning();
    failures += test_region_boundary_edge_cases();
    failures += test_invalid_arguments_and_config();
    failures += test_memory_change_recording();
    failures += test_const_region_load_read_and_write_rejection();
    failures += test_memory_change_capacity_truncates_without_blocking_writes();

    if (failures != 0) {
        fprintf(stderr, "Milestone 3 memory region tests failed: %d failure(s)\n", failures);
        return 1;
    }

    printf("Memory region tests through Milestone 27 passed.\n");
    return 0;
}
