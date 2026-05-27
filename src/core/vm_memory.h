/*
 * @file vm_memory.h
 * @brief Checked simulated memory regions for the MASM32 educational VM.
 *
 * This module owns deterministic .code, .data, .const, heap, and stack regions for
 * MASM32 educational mode. All VM memory reads and writes should pass through
 * these helpers so bounds checks, permissions, unaligned-access warnings, and
 * raw byte-change recording remain centralized.
 */

#ifndef MASM32_SIM_VM_MEMORY_H
#define MASM32_SIM_VM_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm_layout.h"

/// Default base address for the simulated .code region.
#define VM_MEMORY_DEFAULT_CODE_BASE VM_LAYOUT_FIXED_CODE_BASE

/// Default base address for the simulated .data region.
#define VM_MEMORY_DEFAULT_DATA_BASE VM_LAYOUT_FIXED_DATA_BASE

/// Default base address for the simulated read-only .const region.
#define VM_MEMORY_DEFAULT_CONST_BASE VM_LAYOUT_FIXED_CONST_BASE

/// Default base address for the simulated heap region.
#define VM_MEMORY_DEFAULT_HEAP_BASE VM_LAYOUT_FIXED_HEAP_BASE

/// Default exclusive top address for the simulated downward-growing stack.
#define VM_MEMORY_DEFAULT_STACK_TOP VM_LAYOUT_FIXED_STACK_TOP

/// Default simulated .code region size in bytes.
#define VM_MEMORY_DEFAULT_CODE_SIZE VM_LAYOUT_FIXED_CODE_SIZE

/// Default simulated .data region size in bytes.
#define VM_MEMORY_DEFAULT_DATA_SIZE VM_LAYOUT_FIXED_DATA_SIZE

/// Default simulated read-only .const region size in bytes.
#define VM_MEMORY_DEFAULT_CONST_SIZE VM_LAYOUT_FIXED_CONST_SIZE

/// Default simulated heap region size in bytes.
#define VM_MEMORY_DEFAULT_HEAP_SIZE VM_LAYOUT_FIXED_HEAP_SIZE

/// Default simulated stack region size in bytes.
#define VM_MEMORY_DEFAULT_STACK_SIZE VM_LAYOUT_FIXED_STACK_SIZE

/// Maximum number of raw byte changes retained by the fixed recorder.
#define VM_MEMORY_MAX_BYTE_CHANGES 1024U

/// Identifies one deterministic simulated memory region.
typedef enum VmMemoryRegionKind {
    /// Simulated .code region, readable and executable but not writable.
    VM_MEMORY_REGION_CODE = 0,
    /// Simulated .data region, readable and writable but not executable.
    VM_MEMORY_REGION_DATA,
    /// Simulated .const region, readable but not writable or executable.
    VM_MEMORY_REGION_CONST,
    /// Simulated heap region, readable and writable but not executable.
    VM_MEMORY_REGION_HEAP,
    /// Simulated stack region, readable and writable but not executable.
    VM_MEMORY_REGION_STACK,
    /// Number of simulated memory regions.
    VM_MEMORY_REGION_COUNT
} VmMemoryRegionKind;

/// Describes one memory permission bit for a simulated region.
typedef enum VmMemoryPermission {
    /// Region can be read by checked memory helpers.
    VM_MEMORY_PERMISSION_READ = 1 << 0,
    /// Region can be written by checked memory helpers.
    VM_MEMORY_PERMISSION_WRITE = 1 << 1,
    /// Region can be executed by future instruction-fetch helpers.
    VM_MEMORY_PERMISSION_EXECUTE = 1 << 2
} VmMemoryPermission;

/// Identifies the type of checked memory access being attempted.
typedef enum VmMemoryAccessType {
    /// Checked memory read access.
    VM_MEMORY_ACCESS_READ = 0,
    /// Checked memory write access.
    VM_MEMORY_ACCESS_WRITE,
    /// Checked execution/fetch access reserved for future executor use.
    VM_MEMORY_ACCESS_EXECUTE
} VmMemoryAccessType;

/// Describes the result of a checked VM memory operation.
typedef enum VmMemoryStatus {
    /// Operation succeeded without warnings.
    VM_MEMORY_STATUS_OK = 0,
    /// Operation succeeded but used an unaligned multi-byte address.
    VM_MEMORY_STATUS_OK_UNALIGNED,
    /// Operation failed because a required pointer or argument was invalid.
    VM_MEMORY_STATUS_INVALID_ARGUMENT,
    /// Operation failed because the address range is outside all matching regions.
    VM_MEMORY_STATUS_INVALID_ADDRESS,
    /// Operation failed because one access crossed a protected memory-region boundary.
    VM_MEMORY_STATUS_REGION_BOUNDARY_CROSSING,
    /// Operation failed because the matching region lacks the required permission.
    VM_MEMORY_STATUS_PERMISSION_DENIED,
    /// Operation failed because backing storage could not be allocated.
    VM_MEMORY_STATUS_OUT_OF_MEMORY
} VmMemoryStatus;

/// Configures simulated memory region sizes for initialization.
typedef struct VmMemoryConfig {
    /// Requested .code region size in bytes.
    uint32_t code_size;
    /// Requested .data region size in bytes.
    uint32_t data_size;
    /// Requested .const region size in bytes.
    uint32_t const_size;
    /// Requested heap region size in bytes.
    uint32_t heap_size;
    /// Requested stack region size in bytes.
    uint32_t stack_size;
} VmMemoryConfig;

/// Describes one initialized simulated memory region.
typedef struct VmMemoryRegion {
    /// Region identifier.
    VmMemoryRegionKind kind;
    /// Inclusive base address of the region.
    uint32_t base;
    /// Region size in bytes.
    uint32_t size;
    /// Bitmask of VmMemoryPermission values.
    uint8_t permissions;
    /// Backing storage for committed simulated memory bytes.
    uint8_t *bytes;
} VmMemoryRegion;

/// Describes one raw byte change recorded by a checked write helper.
typedef struct VmMemoryByteChange {
    /// Address of the changed byte.
    uint32_t address;
    /// Region containing the changed byte.
    VmMemoryRegionKind region;
    /// Byte value before the write.
    uint8_t old_value;
    /// Byte value after the write.
    uint8_t new_value;
} VmMemoryByteChange;

/// Describes structured status details for one checked memory operation.
typedef struct VmMemoryDiagnostic {
    /// Status returned by the operation.
    VmMemoryStatus status;
    /// Type of memory access attempted.
    VmMemoryAccessType access_type;
    /// First simulated address requested by the operation.
    uint32_t address;
    /// Number of bytes requested by the operation.
    uint32_t size;
    /// Region associated with the operation when one was found.
    VmMemoryRegionKind region;
    /// Whether @ref region contains a meaningful value.
    bool has_region;
    /// Whether the failed range overlaps protected `.CONST` storage.
    bool has_const_overlap;
    /// Runtime base address of the active `.CONST` region.
    uint32_t const_region_start;
    /// Inclusive first address in the attempted range that overlaps `.CONST`.
    uint32_t const_overlap_start;
    /// Inclusive last address in the attempted range that overlaps `.CONST`.
    uint32_t const_overlap_end;
    /// Whether the operation used an unaligned multi-byte address.
    bool is_unaligned;
} VmMemoryDiagnostic;

/// Owns all simulated memory regions and raw byte-change records.
typedef struct VmMemory {
    /// Initialized region descriptors indexed by VmMemoryRegionKind.
    VmMemoryRegion regions[VM_MEMORY_REGION_COUNT];
    /// Raw byte changes recorded since the last clear call.
    VmMemoryByteChange changes[VM_MEMORY_MAX_BYTE_CHANGES];
    /// Number of valid entries in @ref changes.
    size_t change_count;
    /// Whether later writes produced more byte changes than the fixed recorder can retain.
    bool change_overflowed;
} VmMemory;

/// Returns the default fixed-layout memory-size configuration.
///
/// @return Default memory-size configuration for .code, .data, .const, heap, and stack.
VmMemoryConfig vm_memory_default_config(void);

/// Initializes deterministic simulated memory regions.
///
/// Passing NULL for @p config uses @ref vm_memory_default_config. The caller must
/// later call @ref vm_memory_deinit on successfully initialized memory.
///
/// @param memory Memory object to initialize.
/// @param config Optional memory-size configuration.
/// @return VM_MEMORY_STATUS_OK on success, or an error status on failure.
VmMemoryStatus vm_memory_init(VmMemory *memory, const VmMemoryConfig *config);

/// Initializes simulated memory regions from an explicit layout policy.
///
/// Passing NULL for @p policy uses @ref vm_layout_default_policy. The current
/// policy path supports fixed layout plus automatic deterministic sizing metadata
/// while preserving default behavior.
///
/// @param memory Memory object to initialize.
/// @param policy Optional layout policy.
/// @return VM_MEMORY_STATUS_OK on success, or an error status on failure.
VmMemoryStatus vm_memory_init_with_layout_policy(VmMemory *memory, const VmLayoutPolicy *policy);

/// Releases backing storage owned by initialized simulated memory.
///
/// @param memory Memory object to release. NULL is ignored.
void vm_memory_deinit(VmMemory *memory);

/// Returns whether a memory status represents a successful operation.
///
/// @param status Memory operation status to inspect.
/// @return true for VM_MEMORY_STATUS_OK and VM_MEMORY_STATUS_OK_UNALIGNED.
bool vm_memory_status_succeeded(VmMemoryStatus status);

/// Returns a stable lowercase name for a memory status.
///
/// @param status Memory status to inspect.
/// @return Static status name, or NULL for invalid status values.
const char *vm_memory_status_name(VmMemoryStatus status);

/// Returns a stable section-like name for a memory region.
///
/// @param kind Memory region kind to inspect.
/// @return Static region name, or NULL for invalid region values.
const char *vm_memory_region_name(VmMemoryRegionKind kind);

/// Retrieves metadata for one initialized memory region.
///
/// @param memory Memory object to inspect.
/// @param kind Region kind to retrieve.
/// @return Pointer to the region descriptor, or NULL for invalid input.
const VmMemoryRegion *vm_memory_get_region(const VmMemory *memory, VmMemoryRegionKind kind);

/// Checks whether a region has a specific permission bit.
///
/// @param region Region descriptor to inspect.
/// @param permission Permission bit to check.
/// @return true when the region is non-NULL and contains the permission bit.
bool vm_memory_region_has_permission(const VmMemoryRegion *region, VmMemoryPermission permission);

/// Loads initial bytes into a region without requiring write permission or recording changes.
///
/// This helper is intended for parser/data-layout loading before execution,
/// including read-only `.CONST` storage. It performs bounds checks against the
/// selected region but deliberately bypasses normal write permissions.
///
/// @param memory Memory object to mutate.
/// @param kind Region receiving bytes.
/// @param offset Byte offset within the region.
/// @param bytes Source bytes to copy; may be NULL only when @p size is zero.
/// @param size Number of bytes to copy.
/// @return Operation status.
VmMemoryStatus vm_memory_load_region_bytes(VmMemory *memory, VmMemoryRegionKind kind, uint32_t offset, const uint8_t *bytes, uint32_t size);

/// Clears the raw byte-change recorder.
///
/// @param memory Memory object whose change recorder should be reset.
void vm_memory_clear_changes(VmMemory *memory);

/// Returns the number of recorded raw byte changes.
///
/// @param memory Memory object to inspect.
/// @return Number of recorded byte changes, or zero for NULL input.
size_t vm_memory_change_count(const VmMemory *memory);

/// Returns one recorded raw byte change.
///
/// @param memory Memory object to inspect.
/// @param index Zero-based change index.
/// @return Pointer to the recorded change, or NULL for invalid input.
const VmMemoryByteChange *vm_memory_get_change(const VmMemory *memory, size_t index);

/// Reads an unsigned 8-bit value from simulated memory.
///
/// @param memory Memory object to inspect.
/// @param address Simulated address to read.
/// @param out_value Receives the byte on success.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_read_u8(const VmMemory *memory, uint32_t address, uint8_t *out_value, VmMemoryDiagnostic *out_diagnostic);

/// Reads an unsigned 16-bit little-endian value from simulated memory.
///
/// @param memory Memory object to inspect.
/// @param address First simulated address to read.
/// @param out_value Receives the decoded value on success.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_read_u16(const VmMemory *memory, uint32_t address, uint16_t *out_value, VmMemoryDiagnostic *out_diagnostic);

/// Reads an unsigned 32-bit little-endian value from simulated memory.
///
/// @param memory Memory object to inspect.
/// @param address First simulated address to read.
/// @param out_value Receives the decoded value on success.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_read_u32(const VmMemory *memory, uint32_t address, uint32_t *out_value, VmMemoryDiagnostic *out_diagnostic);

/// Reads an unsigned 64-bit little-endian value from simulated memory.
///
/// @param memory Memory object to inspect.
/// @param address First simulated address to read.
/// @param out_value Receives the decoded value on success.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_read_u64(const VmMemory *memory, uint32_t address, uint64_t *out_value, VmMemoryDiagnostic *out_diagnostic);

/// Writes an unsigned 8-bit value to simulated memory.
///
/// @param memory Memory object to mutate.
/// @param address Simulated address to write.
/// @param value Byte value to store.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_write_u8(VmMemory *memory, uint32_t address, uint8_t value, VmMemoryDiagnostic *out_diagnostic);

/// Writes an unsigned 16-bit little-endian value to simulated memory.
///
/// @param memory Memory object to mutate.
/// @param address First simulated address to write.
/// @param value Value to encode.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_write_u16(VmMemory *memory, uint32_t address, uint16_t value, VmMemoryDiagnostic *out_diagnostic);

/// Writes an unsigned 32-bit little-endian value to simulated memory.
///
/// @param memory Memory object to mutate.
/// @param address First simulated address to write.
/// @param value Value to encode.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_write_u32(VmMemory *memory, uint32_t address, uint32_t value, VmMemoryDiagnostic *out_diagnostic);

/// Writes an unsigned 64-bit little-endian value to simulated memory.
///
/// @param memory Memory object to mutate.
/// @param address First simulated address to write.
/// @param value Value to encode.
/// @param out_diagnostic Optional structured diagnostic receiver.
/// @return Operation status.
VmMemoryStatus vm_memory_write_u64(VmMemory *memory, uint32_t address, uint64_t value, VmMemoryDiagnostic *out_diagnostic);

#endif
