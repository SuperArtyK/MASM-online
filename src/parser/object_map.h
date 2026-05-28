/*
 * @file object_map.h
 * @brief Declared-object allocation map metadata for MASM32 data storage.
 *
 * This module builds metadata entries for parser-declared `.data`, `.DATA?`,
 * and `.CONST` symbols. The map remains metadata-only by itself: callers
 * decide whether to keep baseline region-only execution, emit educational
 * object-bounds warnings, or apply later stricter validation modes.
 */

#ifndef MASM32_SIM_OBJECT_MAP_H
#define MASM32_SIM_OBJECT_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "symbols.h"
#include "vm_layout.h"
#include "vm_memory.h"

/// Identifies the construction or lookup status for a declared-object map.
typedef enum VmObjectMapStatus {
    /// Object-map operation completed successfully.
    VM_OBJECT_MAP_STATUS_OK = 0,
    /// Required argument, capacity, or range size was invalid.
    VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT,
    /// Caller-provided object-map entry capacity was insufficient.
    VM_OBJECT_MAP_STATUS_CAPACITY_EXCEEDED,
    /// An inclusive byte-range calculation overflowed uint32_t.
    VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW,
    /// The queried address or full range is not wholly contained in one object.
    VM_OBJECT_MAP_STATUS_NOT_FOUND
} VmObjectMapStatus;

/// Declares the object-map entry type used by range classification results.
typedef struct VmObjectMapEntry VmObjectMapEntry;

/// Describes the current initialization-origin tracking state for one object.
typedef enum VmObjectInitializationOriginState {
    /// Per-byte initialization origin is not available for this object-map entry.
    VM_OBJECT_INITIALIZATION_ORIGIN_NOT_TRACKED = 0,
    /// Per-object initialized/uninitialized-origin byte counts were computed.
    VM_OBJECT_INITIALIZATION_ORIGIN_TRACKED
} VmObjectInitializationOriginState;

/// Classifies how one full access range relates to declared objects and valid regions.
typedef enum VmObjectMapRangeClass {
    /// Range classification has not been computed.
    VM_OBJECT_MAP_RANGE_CLASS_INVALID = 0,
    /// The full range is wholly inside one declared object.
    VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OBJECT,
    /// The full range is wholly inside a declared object different from the optional intended object.
    VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OTHER_OBJECT,
    /// The full range is inside a valid region but outside every declared object.
    VM_OBJECT_MAP_RANGE_CLASS_REGION_GAP,
    /// The range starts inside a declared object and ends outside that object.
    VM_OBJECT_MAP_RANGE_CLASS_STARTS_IN_OBJECT,
    /// The range starts outside a declared object and ends inside one.
    VM_OBJECT_MAP_RANGE_CLASS_ENDS_IN_OBJECT,
    /// The range starts in one declared object and ends in a different declared object.
    VM_OBJECT_MAP_RANGE_CLASS_SPANS_OBJECTS,
    /// The range is not wholly contained in one valid layout region.
    VM_OBJECT_MAP_RANGE_CLASS_OUTSIDE_REGION,
    /// The range is a write that overlaps read-only `.CONST` storage.
    VM_OBJECT_MAP_RANGE_CLASS_CONST_READ_ONLY_OVERLAP
} VmObjectMapRangeClass;

/// Describes the result of classifying one full access range against the object map.
typedef struct VmObjectMapRangeClassification {
    /// Classification assigned to the queried full range.
    VmObjectMapRangeClass range_class;
    /// First address in the classified range.
    uint32_t start_address;
    /// Inclusive final address in the classified range.
    uint32_t end_address;
    /// Layout region that wholly contains the range, if one exists.
    VmLayoutRegionKind region;
    /// Object containing the range start, if any.
    const VmObjectMapEntry *start_object;
    /// Object containing the range end, if any.
    const VmObjectMapEntry *end_object;
    /// Object that wholly contains the range, if any.
    const VmObjectMapEntry *containing_object;
} VmObjectMapRangeClassification;

/// Describes one declared data object after layout selection.
struct VmObjectMapEntry {
    /// Null-terminated source symbol name.
    char symbol_name[VM_SYMBOL_NAME_CAPACITY];
    /// Section that owns the declared object.
    VmSymbolSection section;
    /// Final simulated address of the first byte after selected layout placement.
    uint32_t base_address;
    /// Total byte size of the declared object after nested DUP expansion.
    uint32_t size_bytes;
    /// Declared scalar data type.
    VmSymbolDataType data_type;
    /// Size in bytes of one declared element.
    uint8_t element_size_bytes;
    /// Number of declared elements represented by the object.
    uint32_t element_count;
    /// Whether the declared data type is signed.
    bool is_signed;
    /// Region permission bits effective for the object's backing storage.
    uint8_t permissions;
    /// Source location of the declaration symbol token.
    VmLexerSourceLocation source_location;
    /// Source span length of the declaration symbol token in bytes.
    size_t source_span_length;
    /// Initialization-origin tracking state for this object.
    VmObjectInitializationOriginState initialization_origin_state;
    /// Number of bytes currently marked initialized inside this object.
    uint32_t initialized_byte_count;
    /// Number of bytes that originated from uninitialized storage and remain uninitialized.
    uint32_t uninitialized_byte_count;
};

/// Builds declared-object entries from symbols that already contain final addresses.
///
/// Zero-size symbols are skipped. Each non-zero symbol becomes exactly one
/// object, so adjacent declarations remain separate entries even when their
/// address ranges are contiguous.
///
/// @param symbols Parser-emitted data symbols to convert.
/// @param symbol_count Number of valid symbols in @p symbols.
/// @param entries Caller-owned output entry buffer.
/// @param entry_capacity Number of entries available in @p entries.
/// @param out_entry_count Receives the number of object entries written.
/// @return Object-map construction status.
VmObjectMapStatus vm_object_map_build_from_symbols(
    const VmSymbol *symbols,
    size_t symbol_count,
    VmObjectMapEntry *entries,
    size_t entry_capacity,
    size_t *out_entry_count
);

/// Builds declared-object entries and relocates parser-fixed symbol addresses through a selected layout.
///
/// The parser emits symbolic addresses against the fixed educational bases. This
/// helper converts those addresses to the selected layout bases before writing
/// object entries, allowing tests and later loaders to query fixed, automatic,
/// seeded-randomized, or fresh-randomized layout metadata consistently.
///
/// @param symbols Parser-emitted data symbols using fixed-layout addresses.
/// @param symbol_count Number of valid symbols in @p symbols.
/// @param selected_policy Selected runtime layout policy; NULL uses the default policy.
/// @param entries Caller-owned output entry buffer.
/// @param entry_capacity Number of entries available in @p entries.
/// @param out_entry_count Receives the number of object entries written.
/// @return Object-map construction status.
VmObjectMapStatus vm_object_map_build_from_symbols_with_layout(
    const VmSymbol *symbols,
    size_t symbol_count,
    const VmLayoutPolicy *selected_policy,
    VmObjectMapEntry *entries,
    size_t entry_capacity,
    size_t *out_entry_count
);

/// Builds declared-object entries from symbols plus section-offset initialization masks.
///
/// The masks are indexed from the selected `.data`/`.DATA?` and `.CONST`
/// region bases. A mask byte of 1 means the corresponding storage byte is
/// initialized; 0 means it originated from accepted `?` or `DUP(?)` storage and
/// remains uninitialized-origin.
///
/// @param symbols Parser-emitted data symbols to convert.
/// @param symbol_count Number of valid symbols in @p symbols.
/// @param selected_policy Selected runtime layout policy; NULL uses the fixed default policy.
/// @param data_initialized_mask Optional `.data`/`.DATA?` initialization mask.
/// @param data_initialized_mask_size Number of bytes available in @p data_initialized_mask.
/// @param const_initialized_mask Optional `.CONST` initialization mask.
/// @param const_initialized_mask_size Number of bytes available in @p const_initialized_mask.
/// @param entries Caller-owned output entry buffer.
/// @param entry_capacity Number of entries available in @p entries.
/// @param out_entry_count Receives the number of object entries written.
/// @return Object-map construction status.
VmObjectMapStatus vm_object_map_build_from_symbols_with_initialization_mask(
    const VmSymbol *symbols,
    size_t symbol_count,
    const VmLayoutPolicy *selected_policy,
    const uint8_t *data_initialized_mask,
    size_t data_initialized_mask_size,
    const uint8_t *const_initialized_mask,
    size_t const_initialized_mask_size,
    VmObjectMapEntry *entries,
    size_t entry_capacity,
    size_t *out_entry_count
);

/// Finds the declared object that contains one address.
///
/// @param entries Object-map entries to inspect.
/// @param entry_count Number of valid entries in @p entries.
/// @param address Simulated address to find.
/// @return Containing object entry, or NULL when none exists.
const VmObjectMapEntry *vm_object_map_find_by_address(const VmObjectMapEntry *entries, size_t entry_count, uint32_t address);

/// Finds the declared object that wholly contains an inclusive byte range.
///
/// @param entries Object-map entries to inspect.
/// @param entry_count Number of valid entries in @p entries.
/// @param address First simulated address in the access range.
/// @param size Number of bytes in the access range; must be non-zero.
/// @param out_entry Receives the containing entry on VM_OBJECT_MAP_STATUS_OK; otherwise NULL.
/// @return Lookup status, including overflow and not-found cases.
VmObjectMapStatus vm_object_map_find_by_range(
    const VmObjectMapEntry *entries,
    size_t entry_count,
    uint32_t address,
    uint32_t size,
    const VmObjectMapEntry **out_entry
);


/// Classifies a full access range against declared objects and selected layout regions.
///
/// This helper is metadata-only by itself. It does not emit warnings, stop
/// execution, or replace central VM memory permission checks. Source-run
/// validation modes can use the returned classification to decide whether to
/// warn or fail after lower-level memory checks have completed.
///
/// @param entries Object-map entries to inspect.
/// @param entry_count Number of valid entries in @p entries.
/// @param layout_policy Selected layout policy; NULL uses the fixed default policy.
/// @param intended_object Optional object expected by provenance-aware callers.
/// @param address First simulated address in the access range.
/// @param size Number of bytes in the access range; must be non-zero.
/// @param is_write Whether the access intends to write memory.
/// @param out_classification Receives range classification details.
/// @return Lookup status, including overflow and invalid-argument cases.
VmObjectMapStatus vm_object_map_classify_range(
    const VmObjectMapEntry *entries,
    size_t entry_count,
    const VmLayoutPolicy *layout_policy,
    const VmObjectMapEntry *intended_object,
    uint32_t address,
    uint32_t size,
    bool is_write,
    VmObjectMapRangeClassification *out_classification
);

/// Returns a stable lowercase name for an object-map range classification.
///
/// @param range_class Classification to inspect.
/// @return Static classification name, or NULL for invalid values.
const char *vm_object_map_range_class_name(VmObjectMapRangeClass range_class);

/// Computes the inclusive end address for a non-empty byte range.
///
/// @param base First address in the range.
/// @param size Number of bytes in the range; must be non-zero.
/// @param out_end Receives the inclusive end address.
/// @return true when the range is non-empty and does not overflow uint32_t.
bool vm_object_map_inclusive_end(uint32_t base, uint32_t size, uint32_t *out_end);

/// Returns a stable lowercase name for an object-map status.
///
/// @param status Status to inspect.
/// @return Static status name, or NULL for invalid status values.
const char *vm_object_map_status_name(VmObjectMapStatus status);

/// Returns a stable lowercase name for an initialization-origin state.
///
/// @param state Initialization-origin state to inspect.
/// @return Static state name, or NULL for invalid state values.
const char *vm_object_initialization_origin_state_name(VmObjectInitializationOriginState state);

#endif
