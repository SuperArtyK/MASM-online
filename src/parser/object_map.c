/*
 * @file object_map.c
 * @brief Declared-object allocation map construction and lookup helpers.
 *
 * The object map mirrors parser-declared storage symbols as final byte ranges.
 * It is metadata only in Phase 36 and deliberately does not participate in VM
 * memory validation, which remains owned by the checked memory region helpers.
 */

#include "object_map.h"

#include <stdio.h>
#include <string.h>

/// Returns the effective permissions for a declared object section.
///
/// @param section Symbol section to inspect.
/// @return VmMemoryPermission bitmask used by the object's backing region.
static uint8_t vm_object_map_permissions_for_section(VmSymbolSection section) {
    if (section == VM_SYMBOL_SECTION_CONST) {
        return (uint8_t)VM_MEMORY_PERMISSION_READ;
    }

    return (uint8_t)(VM_MEMORY_PERMISSION_READ | VM_MEMORY_PERMISSION_WRITE);
}

/// Returns the fixed parser base for a declared object section.
///
/// @param section Symbol section to inspect.
/// @param out_base Receives the fixed base on success.
/// @return true when @p section maps to a known parser-emitted region base.
static bool vm_object_map_fixed_base_for_section(VmSymbolSection section, uint32_t *out_base) {
    if (out_base == NULL) {
        return false;
    }

    if (section == VM_SYMBOL_SECTION_CONST) {
        *out_base = VM_MEMORY_DEFAULT_CONST_BASE;
        return true;
    }
    if (section == VM_SYMBOL_SECTION_DATA || section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED) {
        *out_base = VM_MEMORY_DEFAULT_DATA_BASE;
        return true;
    }

    return false;
}

/// Returns the selected runtime base and exclusive limit for a declared object section.
///
/// @param policy Selected layout policy to inspect.
/// @param section Symbol section to inspect.
/// @param out_base Receives the selected base on success.
/// @param out_limit Receives the selected exclusive limit on success.
/// @return true when the section maps to a selected region.
static bool vm_object_map_selected_bounds_for_section(
    const VmLayoutPolicy *policy,
    VmSymbolSection section,
    uint32_t *out_base,
    uint32_t *out_limit
) {
    VmLayoutPolicy default_policy;
    const VmLayoutPolicy *effective_policy = policy;
    VmLayoutRegionKind region = VM_LAYOUT_REGION_DATA;

    if (out_base == NULL || out_limit == NULL) {
        return false;
    }

    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    if (section == VM_SYMBOL_SECTION_CONST) {
        region = VM_LAYOUT_REGION_CONST;
    } else if (section == VM_SYMBOL_SECTION_DATA || section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED) {
        region = VM_LAYOUT_REGION_DATA;
    } else {
        return false;
    }

    *out_base = effective_policy->regions[region].base;
    *out_limit = effective_policy->regions[region].limit;
    return true;
}

/// Finds the selected layout region that wholly contains one inclusive range.
///
/// @param policy Selected layout policy; NULL selects the fixed default policy.
/// @param start First address in the range.
/// @param end Inclusive final address in the range.
/// @param out_region Receives the containing region kind on success.
/// @return true when one selected region wholly contains the range.
static bool vm_object_map_find_containing_region(
    const VmLayoutPolicy *policy,
    uint32_t start,
    uint32_t end,
    VmLayoutRegionKind *out_region
) {
    VmLayoutPolicy default_policy;
    const VmLayoutPolicy *effective_policy = policy;
    size_t index = 0U;

    if (out_region == NULL || start > end) {
        return false;
    }

    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    for (index = 0U; index < (size_t)VM_LAYOUT_REGION_COUNT; index += 1U) {
        const VmLayoutRegionPolicy *region = &effective_policy->regions[index];
        if (region->base <= start && end < region->limit) {
            *out_region = (VmLayoutRegionKind)index;
            return true;
        }
    }

    return false;
}

/// Tests whether one inclusive range overlaps the selected `.CONST` region.
///
/// @param policy Selected layout policy; NULL selects the fixed default policy.
/// @param start First address in the range.
/// @param end Inclusive final address in the range.
/// @return true when any byte in the range overlaps `.CONST` storage.
static bool vm_object_map_range_overlaps_const_region(const VmLayoutPolicy *policy, uint32_t start, uint32_t end) {
    VmLayoutPolicy default_policy;
    const VmLayoutPolicy *effective_policy = policy;
    const VmLayoutRegionPolicy *const_region = NULL;

    if (start > end) {
        return false;
    }

    if (effective_policy == NULL) {
        default_policy = vm_layout_default_policy();
        effective_policy = &default_policy;
    }

    const_region = &effective_policy->regions[VM_LAYOUT_REGION_CONST];
    return start < const_region->limit && end >= const_region->base;
}

bool vm_object_map_inclusive_end(uint32_t base, uint32_t size, uint32_t *out_end) {
    uint32_t delta = 0U;

    if (out_end == NULL || size == 0U) {
        return false;
    }

    delta = size - 1U;
    if (base > UINT32_MAX - delta) {
        return false;
    }

    *out_end = base + delta;
    return true;
}

/// Writes one object-map entry from one symbol and base address.
///
/// @param symbol Source symbol to copy.
/// @param base_address Final base address to store in the entry.
/// @param entry Output entry to populate.
/// @return Object-map construction status.
static VmObjectMapStatus vm_object_map_entry_from_symbol(const VmSymbol *symbol, uint32_t base_address, VmObjectMapEntry *entry) {
    uint32_t end_address = 0U;

    if (symbol == NULL || entry == NULL) {
        return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
    }
    if (symbol->size_bytes == 0U) {
        return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_object_map_inclusive_end(base_address, symbol->size_bytes, &end_address)) {
        (void)end_address;
        return VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW;
    }

    memset(entry, 0, sizeof(*entry));
    (void)snprintf(entry->symbol_name, sizeof(entry->symbol_name), "%s", symbol->name);
    entry->section = symbol->section;
    entry->base_address = base_address;
    entry->size_bytes = symbol->size_bytes;
    entry->data_type = symbol->data_type;
    entry->element_size_bytes = symbol->element_size_bytes;
    entry->element_count = symbol->element_count;
    entry->is_signed = vm_symbol_data_type_is_signed(symbol->data_type);
    entry->permissions = vm_object_map_permissions_for_section(symbol->section);
    entry->source_location = symbol->source_location;
    entry->source_span_length = symbol->source_span_length;
    entry->initialization_origin_state = VM_OBJECT_INITIALIZATION_ORIGIN_NOT_TRACKED;
    return VM_OBJECT_MAP_STATUS_OK;
}

VmObjectMapStatus vm_object_map_build_from_symbols(
    const VmSymbol *symbols,
    size_t symbol_count,
    VmObjectMapEntry *entries,
    size_t entry_capacity,
    size_t *out_entry_count
) {
    size_t index = 0U;
    size_t count = 0U;

    if (out_entry_count != NULL) {
        *out_entry_count = 0U;
    }
    if ((symbols == NULL && symbol_count > 0U) || entries == NULL || out_entry_count == NULL) {
        return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < symbol_count; index += 1U) {
        VmObjectMapStatus status = VM_OBJECT_MAP_STATUS_OK;
        if (symbols[index].size_bytes == 0U) {
            continue;
        }
        if (count >= entry_capacity) {
            return VM_OBJECT_MAP_STATUS_CAPACITY_EXCEEDED;
        }
        status = vm_object_map_entry_from_symbol(&symbols[index], symbols[index].address, &entries[count]);
        if (status != VM_OBJECT_MAP_STATUS_OK) {
            return status;
        }
        count += 1U;
    }

    *out_entry_count = count;
    return VM_OBJECT_MAP_STATUS_OK;
}

VmObjectMapStatus vm_object_map_build_from_symbols_with_layout(
    const VmSymbol *symbols,
    size_t symbol_count,
    const VmLayoutPolicy *selected_policy,
    VmObjectMapEntry *entries,
    size_t entry_capacity,
    size_t *out_entry_count
) {
    size_t index = 0U;
    size_t count = 0U;

    if (out_entry_count != NULL) {
        *out_entry_count = 0U;
    }
    if ((symbols == NULL && symbol_count > 0U) || entries == NULL || out_entry_count == NULL) {
        return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < symbol_count; index += 1U) {
        const VmSymbol *symbol = &symbols[index];
        VmObjectMapStatus status = VM_OBJECT_MAP_STATUS_OK;
        uint32_t fixed_base = 0U;
        uint32_t selected_base = 0U;
        uint32_t selected_limit = 0U;
        uint32_t offset = 0U;
        uint32_t final_base = 0U;
        uint32_t final_end = 0U;

        if (symbol->size_bytes == 0U) {
            continue;
        }
        if (count >= entry_capacity) {
            return VM_OBJECT_MAP_STATUS_CAPACITY_EXCEEDED;
        }
        if (!vm_object_map_fixed_base_for_section(symbol->section, &fixed_base) ||
            !vm_object_map_selected_bounds_for_section(selected_policy, symbol->section, &selected_base, &selected_limit) ||
            symbol->address < fixed_base) {
            return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
        }

        offset = symbol->address - fixed_base;
        if (selected_base > UINT32_MAX - offset) {
            return VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW;
        }
        final_base = selected_base + offset;
        if (!vm_object_map_inclusive_end(final_base, symbol->size_bytes, &final_end)) {
            return VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW;
        }
        if (final_base >= selected_limit || final_end >= selected_limit) {
            return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
        }

        status = vm_object_map_entry_from_symbol(symbol, final_base, &entries[count]);
        if (status != VM_OBJECT_MAP_STATUS_OK) {
            return status;
        }
        count += 1U;
    }

    *out_entry_count = count;
    return VM_OBJECT_MAP_STATUS_OK;
}

const VmObjectMapEntry *vm_object_map_find_by_address(const VmObjectMapEntry *entries, size_t entry_count, uint32_t address) {
    size_t index = 0U;

    if (entries == NULL) {
        return NULL;
    }

    for (index = 0U; index < entry_count; index += 1U) {
        uint32_t end_address = 0U;
        if (entries[index].size_bytes == 0U || !vm_object_map_inclusive_end(entries[index].base_address, entries[index].size_bytes, &end_address)) {
            continue;
        }
        if (address >= entries[index].base_address && address <= end_address) {
            return &entries[index];
        }
    }

    return NULL;
}

VmObjectMapStatus vm_object_map_find_by_range(
    const VmObjectMapEntry *entries,
    size_t entry_count,
    uint32_t address,
    uint32_t size,
    const VmObjectMapEntry **out_entry
) {
    size_t index = 0U;
    uint32_t range_end = 0U;

    if (out_entry != NULL) {
        *out_entry = NULL;
    }
    if (entries == NULL || out_entry == NULL || size == 0U) {
        return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_object_map_inclusive_end(address, size, &range_end)) {
        return VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW;
    }

    for (index = 0U; index < entry_count; index += 1U) {
        uint32_t object_end = 0U;
        if (entries[index].size_bytes == 0U || !vm_object_map_inclusive_end(entries[index].base_address, entries[index].size_bytes, &object_end)) {
            continue;
        }
        if (address >= entries[index].base_address && range_end <= object_end) {
            *out_entry = &entries[index];
            return VM_OBJECT_MAP_STATUS_OK;
        }
    }

    return VM_OBJECT_MAP_STATUS_NOT_FOUND;
}

VmObjectMapStatus vm_object_map_classify_range(
    const VmObjectMapEntry *entries,
    size_t entry_count,
    const VmLayoutPolicy *layout_policy,
    const VmObjectMapEntry *intended_object,
    uint32_t address,
    uint32_t size,
    bool is_write,
    VmObjectMapRangeClassification *out_classification
) {
    VmObjectMapRangeClassification classification;
    const VmObjectMapEntry *containing = NULL;
    VmObjectMapStatus containing_status = VM_OBJECT_MAP_STATUS_OK;
    uint32_t range_end = 0U;

    if (out_classification == NULL || (entries == NULL && entry_count > 0U) || size == 0U) {
        return VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT;
    }
    if (!vm_object_map_inclusive_end(address, size, &range_end)) {
        return VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW;
    }

    memset(&classification, 0, sizeof(classification));
    classification.range_class = VM_OBJECT_MAP_RANGE_CLASS_INVALID;
    classification.start_address = address;
    classification.end_address = range_end;
    classification.region = VM_LAYOUT_REGION_COUNT;
    classification.start_object = vm_object_map_find_by_address(entries, entry_count, address);
    classification.end_object = vm_object_map_find_by_address(entries, entry_count, range_end);

    if (!vm_object_map_find_containing_region(layout_policy, address, range_end, &classification.region)) {
        classification.range_class = VM_OBJECT_MAP_RANGE_CLASS_OUTSIDE_REGION;
        *out_classification = classification;
        return VM_OBJECT_MAP_STATUS_OK;
    }

    if (is_write && vm_object_map_range_overlaps_const_region(layout_policy, address, range_end)) {
        classification.range_class = VM_OBJECT_MAP_RANGE_CLASS_CONST_READ_ONLY_OVERLAP;
        *out_classification = classification;
        return VM_OBJECT_MAP_STATUS_OK;
    }

    containing_status = vm_object_map_find_by_range(entries, entry_count, address, size, &containing);
    if (containing_status == VM_OBJECT_MAP_STATUS_OK && containing != NULL) {
        classification.containing_object = containing;
        classification.range_class = (intended_object != NULL && containing != intended_object)
            ? VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OTHER_OBJECT
            : VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OBJECT;
        *out_classification = classification;
        return VM_OBJECT_MAP_STATUS_OK;
    }
    if (containing_status == VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW || containing_status == VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT) {
        return containing_status;
    }

    if (classification.start_object != NULL && classification.end_object != NULL && classification.start_object != classification.end_object) {
        classification.range_class = VM_OBJECT_MAP_RANGE_CLASS_SPANS_OBJECTS;
    } else if (classification.start_object != NULL) {
        classification.range_class = VM_OBJECT_MAP_RANGE_CLASS_STARTS_IN_OBJECT;
    } else if (classification.end_object != NULL) {
        classification.range_class = VM_OBJECT_MAP_RANGE_CLASS_ENDS_IN_OBJECT;
    } else {
        classification.range_class = VM_OBJECT_MAP_RANGE_CLASS_REGION_GAP;
    }

    *out_classification = classification;
    return VM_OBJECT_MAP_STATUS_OK;
}

const char *vm_object_map_range_class_name(VmObjectMapRangeClass range_class) {
    switch (range_class) {
        case VM_OBJECT_MAP_RANGE_CLASS_INVALID:
            return "invalid";
        case VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OBJECT:
            return "within-object";
        case VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OTHER_OBJECT:
            return "within-other-object";
        case VM_OBJECT_MAP_RANGE_CLASS_REGION_GAP:
            return "region-gap";
        case VM_OBJECT_MAP_RANGE_CLASS_STARTS_IN_OBJECT:
            return "starts-in-object";
        case VM_OBJECT_MAP_RANGE_CLASS_ENDS_IN_OBJECT:
            return "ends-in-object";
        case VM_OBJECT_MAP_RANGE_CLASS_SPANS_OBJECTS:
            return "spans-objects";
        case VM_OBJECT_MAP_RANGE_CLASS_OUTSIDE_REGION:
            return "outside-region";
        case VM_OBJECT_MAP_RANGE_CLASS_CONST_READ_ONLY_OVERLAP:
            return "const-read-only-overlap";
        default:
            return NULL;
    }
}

const char *vm_object_map_status_name(VmObjectMapStatus status) {
    switch (status) {
        case VM_OBJECT_MAP_STATUS_OK:
            return "ok";
        case VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_OBJECT_MAP_STATUS_CAPACITY_EXCEEDED:
            return "capacity-exceeded";
        case VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW:
            return "integer-overflow";
        case VM_OBJECT_MAP_STATUS_NOT_FOUND:
            return "not-found";
        default:
            return NULL;
    }
}

const char *vm_object_initialization_origin_state_name(VmObjectInitializationOriginState state) {
    switch (state) {
        case VM_OBJECT_INITIALIZATION_ORIGIN_NOT_TRACKED:
            return "not-tracked";
        default:
            return NULL;
    }
}
