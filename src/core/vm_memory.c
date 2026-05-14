/*
 * @file vm_memory.c
 * @brief Checked memory access implementation for the MASM32 educational VM.
 *
 * This file implements deterministic simulated .code, .data, heap, and stack
 * regions for Milestone 3. It centralizes region lookup, bounds checks,
 * permissions, unaligned-access warnings, little-endian integer storage, and
 * raw byte-change recording.
 */

#include "vm_memory.h"

#include <stdlib.h>
#include <string.h>

/// Converts a region kind into an array index after validation.
///
/// @param kind Region kind supplied by a caller.
/// @param out_index Receives the array index on success.
/// @return true when @p kind is valid and @p out_index was written.
static bool vm_memory_region_index(VmMemoryRegionKind kind, size_t *out_index) {
    if (out_index == NULL) {
        return false;
    }

    if (kind < VM_MEMORY_REGION_CODE || kind >= VM_MEMORY_REGION_COUNT) {
        return false;
    }

    *out_index = (size_t)kind;
    return true;
}

/// Computes the exclusive end address for a region.
///
/// @param region Region descriptor to inspect.
/// @param out_end Receives the exclusive end address on success.
/// @return true when the region is valid and the address range does not overflow.
static bool vm_memory_region_end(const VmMemoryRegion *region, uint32_t *out_end) {
    if (region == NULL || out_end == NULL) {
        return false;
    }

    if (region->size > UINT32_MAX - region->base) {
        return false;
    }

    *out_end = region->base + region->size;
    return true;
}

/// Initializes a diagnostic structure for an operation attempt.
///
/// @param diagnostic Optional diagnostic structure to initialize.
/// @param status Initial operation status.
/// @param access_type Access type attempted by the caller.
/// @param address First simulated address requested.
/// @param size Number of bytes requested.
static void vm_memory_fill_diagnostic(
    VmMemoryDiagnostic *diagnostic,
    VmMemoryStatus status,
    VmMemoryAccessType access_type,
    uint32_t address,
    uint32_t size
) {
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->status = status;
    diagnostic->access_type = access_type;
    diagnostic->address = address;
    diagnostic->size = size;
    diagnostic->region = VM_MEMORY_REGION_CODE;
    diagnostic->has_region = false;
    diagnostic->is_unaligned = false;
}

/// Adds region and warning details to a diagnostic structure.
///
/// @param diagnostic Optional diagnostic structure to update.
/// @param status Operation status to report.
/// @param region Region associated with the operation, or NULL.
/// @param is_unaligned Whether the operation was unaligned.
static void vm_memory_finish_diagnostic(
    VmMemoryDiagnostic *diagnostic,
    VmMemoryStatus status,
    const VmMemoryRegion *region,
    bool is_unaligned
) {
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->status = status;
    diagnostic->is_unaligned = is_unaligned;
    if (region != NULL) {
        diagnostic->region = region->kind;
        diagnostic->has_region = true;
    }
}

/// Returns true when an address is unaligned for the requested access size.
///
/// @param address First simulated address requested.
/// @param size Number of bytes requested.
/// @return true for multi-byte accesses whose address is not size-aligned.
static bool vm_memory_is_unaligned(uint32_t address, uint32_t size) {
    if (size <= 1U) {
        return false;
    }

    return (address % size) != 0U;
}

/// Returns the permission required for an access type.
///
/// @param access_type Access type to map.
/// @param out_permission Receives the required permission bit on success.
/// @return true when the access type is valid.
static bool vm_memory_permission_for_access(VmMemoryAccessType access_type, VmMemoryPermission *out_permission) {
    if (out_permission == NULL) {
        return false;
    }

    switch (access_type) {
        case VM_MEMORY_ACCESS_READ:
            *out_permission = VM_MEMORY_PERMISSION_READ;
            return true;
        case VM_MEMORY_ACCESS_WRITE:
            *out_permission = VM_MEMORY_PERMISSION_WRITE;
            return true;
        case VM_MEMORY_ACCESS_EXECUTE:
            *out_permission = VM_MEMORY_PERMISSION_EXECUTE;
            return true;
        default:
            return false;
    }
}

/// Finds the region containing the full requested address range.
///
/// @param memory Memory object to inspect.
/// @param address First simulated address requested.
/// @param size Number of bytes requested.
/// @return Const region containing the full range, or NULL when none exists.
static const VmMemoryRegion *vm_memory_find_region_const(const VmMemory *memory, uint32_t address, uint32_t size) {
    size_t index = 0U;
    uint32_t request_end = 0U;

    if (memory == NULL || size == 0U || size > UINT32_MAX - address) {
        return NULL;
    }

    request_end = address + size;

    for (index = 0U; index < (size_t)VM_MEMORY_REGION_COUNT; index += 1U) {
        const VmMemoryRegion *region = &memory->regions[index];
        uint32_t region_end = 0U;

        if (!vm_memory_region_end(region, &region_end)) {
            continue;
        }

        if (address >= region->base && request_end <= region_end) {
            return region;
        }
    }

    return NULL;
}

/// Finds a region containing only the starting address for diagnostics.
///
/// @param memory Memory object to inspect.
/// @param address First simulated address requested.
/// @return Region containing @p address, or NULL when none exists.
static const VmMemoryRegion *vm_memory_find_region_for_address(const VmMemory *memory, uint32_t address) {
    size_t index = 0U;

    if (memory == NULL) {
        return NULL;
    }

    for (index = 0U; index < (size_t)VM_MEMORY_REGION_COUNT; index += 1U) {
        const VmMemoryRegion *region = &memory->regions[index];
        uint32_t region_end = 0U;

        if (!vm_memory_region_end(region, &region_end)) {
            continue;
        }

        if (address >= region->base && address < region_end) {
            return region;
        }
    }

    return NULL;
}

/// Validates a checked access before bytes are read or written.
///
/// @param memory Memory object to inspect.
/// @param access_type Access type requested by the caller.
/// @param address First simulated address requested.
/// @param size Number of bytes requested.
/// @param out_region Receives the containing region on success.
/// @param out_is_unaligned Receives whether the access is unaligned.
/// @param out_diagnostic Optional diagnostic receiver.
/// @return Operation status for the validation step.
static VmMemoryStatus vm_memory_validate_access(
    const VmMemory *memory,
    VmMemoryAccessType access_type,
    uint32_t address,
    uint32_t size,
    const VmMemoryRegion **out_region,
    bool *out_is_unaligned,
    VmMemoryDiagnostic *out_diagnostic
) {
    const VmMemoryRegion *region = NULL;
    VmMemoryPermission permission = VM_MEMORY_PERMISSION_READ;
    bool is_unaligned = false;

    vm_memory_fill_diagnostic(out_diagnostic, VM_MEMORY_STATUS_INVALID_ARGUMENT, access_type, address, size);

    if (out_region != NULL) {
        *out_region = NULL;
    }
    if (out_is_unaligned != NULL) {
        *out_is_unaligned = false;
    }

    if (memory == NULL || out_region == NULL || out_is_unaligned == NULL || size == 0U) {
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    if (!vm_memory_permission_for_access(access_type, &permission)) {
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    region = vm_memory_find_region_const(memory, address, size);
    if (region == NULL) {
        const VmMemoryRegion *near_region = vm_memory_find_region_for_address(memory, address);
        vm_memory_finish_diagnostic(out_diagnostic, VM_MEMORY_STATUS_INVALID_ADDRESS, near_region, false);
        return VM_MEMORY_STATUS_INVALID_ADDRESS;
    }

    if (!vm_memory_region_has_permission(region, permission)) {
        vm_memory_finish_diagnostic(out_diagnostic, VM_MEMORY_STATUS_PERMISSION_DENIED, region, false);
        return VM_MEMORY_STATUS_PERMISSION_DENIED;
    }

    is_unaligned = vm_memory_is_unaligned(address, size);
    *out_region = region;
    *out_is_unaligned = is_unaligned;

    vm_memory_finish_diagnostic(out_diagnostic, is_unaligned ? VM_MEMORY_STATUS_OK_UNALIGNED : VM_MEMORY_STATUS_OK, region, is_unaligned);
    return is_unaligned ? VM_MEMORY_STATUS_OK_UNALIGNED : VM_MEMORY_STATUS_OK;
}

/// Validates memory sizes and default fixed region placement.
///
/// @param config Memory-size configuration to validate.
/// @return true when fixed region ranges are non-zero and non-overlapping.
static bool vm_memory_config_is_valid(const VmMemoryConfig *config) {
    uint32_t stack_base = 0U;

    if (config == NULL) {
        return false;
    }

    if (config->code_size == 0U || config->data_size == 0U || config->heap_size == 0U || config->stack_size == 0U) {
        return false;
    }

    if (config->code_size > VM_MEMORY_DEFAULT_DATA_BASE - VM_MEMORY_DEFAULT_CODE_BASE) {
        return false;
    }

    if (config->data_size > VM_MEMORY_DEFAULT_HEAP_BASE - VM_MEMORY_DEFAULT_DATA_BASE) {
        return false;
    }

    if (config->stack_size > VM_MEMORY_DEFAULT_STACK_TOP) {
        return false;
    }

    stack_base = VM_MEMORY_DEFAULT_STACK_TOP - config->stack_size;
    if (stack_base < VM_MEMORY_DEFAULT_HEAP_BASE) {
        return false;
    }

    if (config->heap_size > stack_base - VM_MEMORY_DEFAULT_HEAP_BASE) {
        return false;
    }

    return true;
}

/// Initializes one region descriptor and allocates zeroed backing storage.
///
/// @param region Region descriptor to initialize.
/// @param kind Region identifier.
/// @param base Inclusive base address.
/// @param size Region size in bytes.
/// @param permissions Permission bitmask.
/// @return Operation status.
static VmMemoryStatus vm_memory_init_region(
    VmMemoryRegion *region,
    VmMemoryRegionKind kind,
    uint32_t base,
    uint32_t size,
    uint8_t permissions
) {
    if (region == NULL || size == 0U) {
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    region->kind = kind;
    region->base = base;
    region->size = size;
    region->permissions = permissions;
    region->bytes = (uint8_t *)calloc((size_t)size, sizeof(uint8_t));

    if (region->bytes == NULL) {
        return VM_MEMORY_STATUS_OUT_OF_MEMORY;
    }

    return VM_MEMORY_STATUS_OK;
}

/// Computes a byte offset within a previously validated region.
///
/// @param region Region containing the address.
/// @param address Simulated address inside the region.
/// @return Zero-based byte offset from the region base.
static size_t vm_memory_region_offset(const VmMemoryRegion *region, uint32_t address) {
    return (size_t)(address - region->base);
}

/// Records one raw byte change.
///
/// @param memory Memory object whose recorder receives the change.
/// @param region Region containing the changed byte.
/// @param address Address of the changed byte.
/// @param old_value Value before the write.
/// @param new_value Value after the write.
static void vm_memory_record_byte_change(
    VmMemory *memory,
    const VmMemoryRegion *region,
    uint32_t address,
    uint8_t old_value,
    uint8_t new_value
) {
    VmMemoryByteChange *change = NULL;

    if (memory == NULL || region == NULL || old_value == new_value) {
        return;
    }

    if (memory->change_count >= (size_t)VM_MEMORY_MAX_BYTE_CHANGES) {
        memory->change_overflowed = true;
        return;
    }

    change = &memory->changes[memory->change_count];
    change->address = address;
    change->region = region->kind;
    change->old_value = old_value;
    change->new_value = new_value;
    memory->change_count += 1U;
}

/// Reads up to eight little-endian bytes from a validated memory region.
///
/// @param region Region containing the read range.
/// @param address First simulated address to read.
/// @param size Number of bytes to read.
/// @return Decoded unsigned integer value.
static uint64_t vm_memory_read_integer(const VmMemoryRegion *region, uint32_t address, uint32_t size) {
    uint64_t value = 0U;
    size_t offset = vm_memory_region_offset(region, address);
    uint32_t index = 0U;

    for (index = 0U; index < size; index += 1U) {
        value |= ((uint64_t)region->bytes[offset + (size_t)index]) << (8U * index);
    }

    return value;
}

/// Encodes an unsigned integer into up to eight little-endian bytes.
///
/// @param value Integer value to encode.
/// @param size Number of bytes to emit.
/// @param out_bytes Receives encoded bytes.
static void vm_memory_encode_integer(uint64_t value, uint32_t size, uint8_t *out_bytes) {
    uint32_t index = 0U;

    if (out_bytes == NULL) {
        return;
    }

    for (index = 0U; index < size; index += 1U) {
        out_bytes[index] = (uint8_t)((value >> (8U * index)) & 0xFFU);
    }
}

/// Writes encoded bytes into a validated memory region and records raw changes.
///
/// @param memory Memory object to mutate.
/// @param region Region containing the write range.
/// @param address First simulated address to write.
/// @param bytes Encoded write bytes.
/// @param size Number of bytes to write.
static void vm_memory_write_bytes(VmMemory *memory, VmMemoryRegion *region, uint32_t address, const uint8_t *bytes, uint32_t size) {
    size_t offset = vm_memory_region_offset(region, address);
    uint32_t index = 0U;

    for (index = 0U; index < size; index += 1U) {
        uint8_t old_value = region->bytes[offset + (size_t)index];
        uint8_t new_value = bytes[index];
        uint32_t byte_address = address + index;

        region->bytes[offset + (size_t)index] = new_value;
        vm_memory_record_byte_change(memory, region, byte_address, old_value, new_value);
    }
}

/// Reads an integer value through the common checked access path.
///
/// @param memory Memory object to inspect.
/// @param address First simulated address to read.
/// @param size Number of bytes to read.
/// @param out_value Receives decoded value on success.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
static VmMemoryStatus vm_memory_read_integer_checked(
    const VmMemory *memory,
    uint32_t address,
    uint32_t size,
    uint64_t *out_value,
    VmMemoryDiagnostic *out_diagnostic
) {
    const VmMemoryRegion *region = NULL;
    bool is_unaligned = false;
    VmMemoryStatus status = VM_MEMORY_STATUS_OK;

    if (out_value == NULL) {
        vm_memory_fill_diagnostic(out_diagnostic, VM_MEMORY_STATUS_INVALID_ARGUMENT, VM_MEMORY_ACCESS_READ, address, size);
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    status = vm_memory_validate_access(memory, VM_MEMORY_ACCESS_READ, address, size, &region, &is_unaligned, out_diagnostic);
    if (!vm_memory_status_succeeded(status)) {
        return status;
    }

    *out_value = vm_memory_read_integer(region, address, size);
    return is_unaligned ? VM_MEMORY_STATUS_OK_UNALIGNED : VM_MEMORY_STATUS_OK;
}

/// Writes an integer value through the common checked access path.
///
/// @param memory Memory object to mutate.
/// @param address First simulated address to write.
/// @param size Number of bytes to write.
/// @param value Value to encode.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
static VmMemoryStatus vm_memory_write_integer_checked(
    VmMemory *memory,
    uint32_t address,
    uint32_t size,
    uint64_t value,
    VmMemoryDiagnostic *out_diagnostic
) {
    const VmMemoryRegion *const_region = NULL;
    VmMemoryRegion *region = NULL;
    bool is_unaligned = false;
    VmMemoryStatus status = VM_MEMORY_STATUS_OK;
    uint8_t bytes[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    status = vm_memory_validate_access(memory, VM_MEMORY_ACCESS_WRITE, address, size, &const_region, &is_unaligned, out_diagnostic);
    if (!vm_memory_status_succeeded(status)) {
        return status;
    }

    region = &memory->regions[const_region->kind];

    vm_memory_encode_integer(value, size, bytes);
    vm_memory_write_bytes(memory, region, address, bytes, size);
    return is_unaligned ? VM_MEMORY_STATUS_OK_UNALIGNED : VM_MEMORY_STATUS_OK;
}

VmMemoryConfig vm_memory_default_config(void) {
    VmMemoryConfig config;

    config.code_size = VM_MEMORY_DEFAULT_CODE_SIZE;
    config.data_size = VM_MEMORY_DEFAULT_DATA_SIZE;
    config.heap_size = VM_MEMORY_DEFAULT_HEAP_SIZE;
    config.stack_size = VM_MEMORY_DEFAULT_STACK_SIZE;

    return config;
}

VmMemoryStatus vm_memory_init(VmMemory *memory, const VmMemoryConfig *config) {
    VmMemoryConfig effective_config;
    VmMemoryStatus status = VM_MEMORY_STATUS_OK;
    uint32_t stack_base = 0U;

    if (memory == NULL) {
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    effective_config = config != NULL ? *config : vm_memory_default_config();
    if (!vm_memory_config_is_valid(&effective_config)) {
        memset(memory, 0, sizeof(*memory));
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    memset(memory, 0, sizeof(*memory));
    stack_base = VM_MEMORY_DEFAULT_STACK_TOP - effective_config.stack_size;

    status = vm_memory_init_region(
        &memory->regions[VM_MEMORY_REGION_CODE],
        VM_MEMORY_REGION_CODE,
        VM_MEMORY_DEFAULT_CODE_BASE,
        effective_config.code_size,
        (uint8_t)(VM_MEMORY_PERMISSION_READ | VM_MEMORY_PERMISSION_EXECUTE)
    );
    if (status != VM_MEMORY_STATUS_OK) {
        vm_memory_deinit(memory);
        return status;
    }

    status = vm_memory_init_region(
        &memory->regions[VM_MEMORY_REGION_DATA],
        VM_MEMORY_REGION_DATA,
        VM_MEMORY_DEFAULT_DATA_BASE,
        effective_config.data_size,
        (uint8_t)(VM_MEMORY_PERMISSION_READ | VM_MEMORY_PERMISSION_WRITE)
    );
    if (status != VM_MEMORY_STATUS_OK) {
        vm_memory_deinit(memory);
        return status;
    }

    status = vm_memory_init_region(
        &memory->regions[VM_MEMORY_REGION_HEAP],
        VM_MEMORY_REGION_HEAP,
        VM_MEMORY_DEFAULT_HEAP_BASE,
        effective_config.heap_size,
        (uint8_t)(VM_MEMORY_PERMISSION_READ | VM_MEMORY_PERMISSION_WRITE)
    );
    if (status != VM_MEMORY_STATUS_OK) {
        vm_memory_deinit(memory);
        return status;
    }

    status = vm_memory_init_region(
        &memory->regions[VM_MEMORY_REGION_STACK],
        VM_MEMORY_REGION_STACK,
        stack_base,
        effective_config.stack_size,
        (uint8_t)(VM_MEMORY_PERMISSION_READ | VM_MEMORY_PERMISSION_WRITE)
    );
    if (status != VM_MEMORY_STATUS_OK) {
        vm_memory_deinit(memory);
        return status;
    }

    return VM_MEMORY_STATUS_OK;
}

void vm_memory_deinit(VmMemory *memory) {
    size_t index = 0U;

    if (memory == NULL) {
        return;
    }

    for (index = 0U; index < (size_t)VM_MEMORY_REGION_COUNT; index += 1U) {
        free(memory->regions[index].bytes);
        memory->regions[index].bytes = NULL;
        memory->regions[index].size = 0U;
        memory->regions[index].base = 0U;
        memory->regions[index].permissions = 0U;
        memory->regions[index].kind = (VmMemoryRegionKind)index;
    }

    memory->change_count = 0U;
    memory->change_overflowed = false;
}

bool vm_memory_status_succeeded(VmMemoryStatus status) {
    return status == VM_MEMORY_STATUS_OK || status == VM_MEMORY_STATUS_OK_UNALIGNED;
}

const char *vm_memory_status_name(VmMemoryStatus status) {
    switch (status) {
        case VM_MEMORY_STATUS_OK:
            return "ok";
        case VM_MEMORY_STATUS_OK_UNALIGNED:
            return "ok-unaligned";
        case VM_MEMORY_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_MEMORY_STATUS_INVALID_ADDRESS:
            return "invalid-address";
        case VM_MEMORY_STATUS_PERMISSION_DENIED:
            return "permission-denied";
        case VM_MEMORY_STATUS_OUT_OF_MEMORY:
            return "out-of-memory";
        default:
            return NULL;
    }
}

const char *vm_memory_region_name(VmMemoryRegionKind kind) {
    switch (kind) {
        case VM_MEMORY_REGION_CODE:
            return ".code";
        case VM_MEMORY_REGION_DATA:
            return ".data";
        case VM_MEMORY_REGION_HEAP:
            return ".heap";
        case VM_MEMORY_REGION_STACK:
            return ".stack";
        default:
            return NULL;
    }
}

const VmMemoryRegion *vm_memory_get_region(const VmMemory *memory, VmMemoryRegionKind kind) {
    size_t index = 0U;

    if (memory == NULL || !vm_memory_region_index(kind, &index)) {
        return NULL;
    }

    return &memory->regions[index];
}

bool vm_memory_region_has_permission(const VmMemoryRegion *region, VmMemoryPermission permission) {
    if (region == NULL) {
        return false;
    }

    return (region->permissions & (uint8_t)permission) != 0U;
}

void vm_memory_clear_changes(VmMemory *memory) {
    if (memory == NULL) {
        return;
    }

    memory->change_count = 0U;
    memory->change_overflowed = false;
}

size_t vm_memory_change_count(const VmMemory *memory) {
    if (memory == NULL) {
        return 0U;
    }

    return memory->change_count;
}

const VmMemoryByteChange *vm_memory_get_change(const VmMemory *memory, size_t index) {
    if (memory == NULL || index >= memory->change_count) {
        return NULL;
    }

    return &memory->changes[index];
}

VmMemoryStatus vm_memory_read_u8(const VmMemory *memory, uint32_t address, uint8_t *out_value, VmMemoryDiagnostic *out_diagnostic) {
    uint64_t value = 0U;
    VmMemoryStatus status = VM_MEMORY_STATUS_OK;

    if (out_value == NULL) {
        vm_memory_fill_diagnostic(out_diagnostic, VM_MEMORY_STATUS_INVALID_ARGUMENT, VM_MEMORY_ACCESS_READ, address, 1U);
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    status = vm_memory_read_integer_checked(memory, address, 1U, &value, out_diagnostic);
    if (vm_memory_status_succeeded(status)) {
        *out_value = (uint8_t)value;
    }

    return status;
}

VmMemoryStatus vm_memory_read_u16(const VmMemory *memory, uint32_t address, uint16_t *out_value, VmMemoryDiagnostic *out_diagnostic) {
    uint64_t value = 0U;
    VmMemoryStatus status = VM_MEMORY_STATUS_OK;

    if (out_value == NULL) {
        vm_memory_fill_diagnostic(out_diagnostic, VM_MEMORY_STATUS_INVALID_ARGUMENT, VM_MEMORY_ACCESS_READ, address, 2U);
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    status = vm_memory_read_integer_checked(memory, address, 2U, &value, out_diagnostic);
    if (vm_memory_status_succeeded(status)) {
        *out_value = (uint16_t)value;
    }

    return status;
}

VmMemoryStatus vm_memory_read_u32(const VmMemory *memory, uint32_t address, uint32_t *out_value, VmMemoryDiagnostic *out_diagnostic) {
    uint64_t value = 0U;
    VmMemoryStatus status = VM_MEMORY_STATUS_OK;

    if (out_value == NULL) {
        vm_memory_fill_diagnostic(out_diagnostic, VM_MEMORY_STATUS_INVALID_ARGUMENT, VM_MEMORY_ACCESS_READ, address, 4U);
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    status = vm_memory_read_integer_checked(memory, address, 4U, &value, out_diagnostic);
    if (vm_memory_status_succeeded(status)) {
        *out_value = (uint32_t)value;
    }

    return status;
}

VmMemoryStatus vm_memory_read_u64(const VmMemory *memory, uint32_t address, uint64_t *out_value, VmMemoryDiagnostic *out_diagnostic) {
    uint64_t value = 0U;
    VmMemoryStatus status = VM_MEMORY_STATUS_OK;

    if (out_value == NULL) {
        vm_memory_fill_diagnostic(out_diagnostic, VM_MEMORY_STATUS_INVALID_ARGUMENT, VM_MEMORY_ACCESS_READ, address, 8U);
        return VM_MEMORY_STATUS_INVALID_ARGUMENT;
    }

    status = vm_memory_read_integer_checked(memory, address, 8U, &value, out_diagnostic);
    if (vm_memory_status_succeeded(status)) {
        *out_value = value;
    }

    return status;
}

VmMemoryStatus vm_memory_write_u8(VmMemory *memory, uint32_t address, uint8_t value, VmMemoryDiagnostic *out_diagnostic) {
    return vm_memory_write_integer_checked(memory, address, 1U, value, out_diagnostic);
}

VmMemoryStatus vm_memory_write_u16(VmMemory *memory, uint32_t address, uint16_t value, VmMemoryDiagnostic *out_diagnostic) {
    return vm_memory_write_integer_checked(memory, address, 2U, value, out_diagnostic);
}

VmMemoryStatus vm_memory_write_u32(VmMemory *memory, uint32_t address, uint32_t value, VmMemoryDiagnostic *out_diagnostic) {
    return vm_memory_write_integer_checked(memory, address, 4U, value, out_diagnostic);
}

VmMemoryStatus vm_memory_write_u64(VmMemory *memory, uint32_t address, uint64_t value, VmMemoryDiagnostic *out_diagnostic) {
    return vm_memory_write_integer_checked(memory, address, 8U, value, out_diagnostic);
}
