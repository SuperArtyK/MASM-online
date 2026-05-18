/*
 * @file vm_layout.c
 * @brief Memory layout policy defaults for the MASM32 educational VM.
 *
 * This file centralizes fixed educational layout addresses, sizes, safety-tier
 * limits, alignment, guard-gap, stack-size, heap-size, and random-seed policy
 * defaults so the loader can consume one explicit layout policy object.
 */

#include "vm_layout.h"

#include <stddef.h>

/// Initializes maximum-size values for every safety tier.
///
/// @param values Output array indexed by VmLayoutSafetyTier.
/// @param maximum Maximum value to store for every tier.
static void vm_layout_fill_tier_maximums(uint32_t values[VM_LAYOUT_SAFETY_TIER_COUNT], uint32_t maximum) {
    size_t index = 0U;

    if (values == NULL) {
        return;
    }

    for (index = 0U; index < (size_t)VM_LAYOUT_SAFETY_TIER_COUNT; index += 1U) {
        values[index] = maximum;
    }
}

/// Creates one region-policy entry.
///
/// @param kind Region kind described by the entry.
/// @param base Inclusive base address.
/// @param size Region size in bytes.
/// @param minimum_size Minimum accepted size in bytes.
/// @param maximum_size Maximum accepted size in bytes for every current tier.
/// @return Initialized region policy entry.
static VmLayoutRegionPolicy vm_layout_make_region_policy(
    VmLayoutRegionKind kind,
    uint32_t base,
    uint32_t size,
    uint32_t minimum_size,
    uint32_t maximum_size
) {
    VmLayoutRegionPolicy region;

    region.kind = kind;
    region.base = base;
    region.limit = base + size;
    region.minimum_size = minimum_size;
    region.default_size = size;
    vm_layout_fill_tier_maximums(region.maximum_size_by_tier, maximum_size);

    return region;
}

/// Returns true when @p tier is a valid layout safety-tier value.
///
/// @param tier Safety tier to validate.
/// @return true when the safety tier can index policy limits.
static bool vm_layout_safety_tier_is_valid(VmLayoutSafetyTier tier) {
    return tier >= VM_LAYOUT_SAFETY_TIER_BEGINNER && tier < VM_LAYOUT_SAFETY_TIER_COUNT;
}

/// Returns true when @p kind is a valid layout region value.
///
/// @param kind Region kind to validate.
/// @return true when the region kind can index policy arrays.
static bool vm_layout_region_kind_is_valid(VmLayoutRegionKind kind) {
    return kind >= VM_LAYOUT_REGION_CODE && kind < VM_LAYOUT_REGION_COUNT;
}

VmLayoutPolicy vm_layout_default_policy(void) {
    VmLayoutPolicy policy;

    policy.mode = VM_LAYOUT_MODE_FIXED;
    policy.safety_tier = VM_LAYOUT_SAFETY_TIER_BEGINNER;
    policy.stack_size_request = VM_LAYOUT_DEFAULT_STACK_SIZE_REQUEST;
    policy.heap_size_request = VM_LAYOUT_DEFAULT_HEAP_SIZE_REQUEST;
    policy.region_alignment = VM_LAYOUT_DEFAULT_REGION_ALIGNMENT;
    policy.guard_gap_size = VM_LAYOUT_DEFAULT_GUARD_GAP_SIZE;
    policy.has_random_seed = false;
    policy.random_seed = VM_LAYOUT_DEFAULT_RANDOM_SEED;

    policy.regions[VM_LAYOUT_REGION_CODE] = vm_layout_make_region_policy(
        VM_LAYOUT_REGION_CODE,
        VM_LAYOUT_FIXED_CODE_BASE,
        VM_LAYOUT_FIXED_CODE_SIZE,
        VM_LAYOUT_MIN_CODE_SIZE,
        VM_LAYOUT_FIXED_DATA_BASE - VM_LAYOUT_FIXED_CODE_BASE
    );
    policy.regions[VM_LAYOUT_REGION_DATA] = vm_layout_make_region_policy(
        VM_LAYOUT_REGION_DATA,
        VM_LAYOUT_FIXED_DATA_BASE,
        VM_LAYOUT_FIXED_DATA_SIZE,
        VM_LAYOUT_MIN_DATA_SIZE,
        VM_LAYOUT_FIXED_CONST_BASE - VM_LAYOUT_FIXED_DATA_BASE
    );
    policy.regions[VM_LAYOUT_REGION_CONST] = vm_layout_make_region_policy(
        VM_LAYOUT_REGION_CONST,
        VM_LAYOUT_FIXED_CONST_BASE,
        VM_LAYOUT_FIXED_CONST_SIZE,
        VM_LAYOUT_MIN_CONST_SIZE,
        VM_LAYOUT_FIXED_HEAP_BASE - VM_LAYOUT_FIXED_CONST_BASE
    );
    policy.regions[VM_LAYOUT_REGION_HEAP] = vm_layout_make_region_policy(
        VM_LAYOUT_REGION_HEAP,
        VM_LAYOUT_FIXED_HEAP_BASE,
        VM_LAYOUT_FIXED_HEAP_SIZE,
        VM_LAYOUT_MIN_HEAP_SIZE,
        VM_LAYOUT_FIXED_STACK_TOP - VM_LAYOUT_FIXED_HEAP_BASE
    );
    policy.regions[VM_LAYOUT_REGION_STACK] = vm_layout_make_region_policy(
        VM_LAYOUT_REGION_STACK,
        VM_LAYOUT_FIXED_STACK_TOP - VM_LAYOUT_FIXED_STACK_SIZE,
        VM_LAYOUT_FIXED_STACK_SIZE,
        VM_LAYOUT_MIN_STACK_SIZE,
        VM_LAYOUT_FIXED_STACK_TOP
    );

    vm_layout_fill_tier_maximums(policy.maximum_total_reservation_by_tier, VM_LAYOUT_DEFAULT_MAX_TOTAL_RESERVATION);

    return policy;
}

const VmLayoutRegionPolicy *vm_layout_policy_get_region(const VmLayoutPolicy *policy, VmLayoutRegionKind kind) {
    if (policy == NULL || !vm_layout_region_kind_is_valid(kind)) {
        return NULL;
    }

    return &policy->regions[kind];
}

bool vm_layout_region_size(const VmLayoutRegionPolicy *region, uint32_t *out_size) {
    if (region == NULL || out_size == NULL || region->limit <= region->base) {
        return false;
    }

    *out_size = region->limit - region->base;
    return true;
}

bool vm_layout_policy_is_valid(const VmLayoutPolicy *policy) {
    uint64_t total_size = 0U;
    size_t index = 0U;

    if (policy == NULL || policy->mode != VM_LAYOUT_MODE_FIXED || !vm_layout_safety_tier_is_valid(policy->safety_tier)) {
        return false;
    }

    if (policy->region_alignment == 0U) {
        return false;
    }

    for (index = 0U; index < (size_t)VM_LAYOUT_REGION_COUNT; index += 1U) {
        const VmLayoutRegionPolicy *region = &policy->regions[index];
        uint32_t size = 0U;
        uint32_t maximum = 0U;

        if (region->kind != (VmLayoutRegionKind)index || !vm_layout_region_size(region, &size)) {
            return false;
        }

        maximum = region->maximum_size_by_tier[policy->safety_tier];
        if (size < region->minimum_size || size > maximum) {
            return false;
        }

        total_size += (uint64_t)size;
        if (total_size > (uint64_t)policy->maximum_total_reservation_by_tier[policy->safety_tier]) {
            return false;
        }
    }

    if ((uint64_t)policy->regions[VM_LAYOUT_REGION_CODE].limit + (uint64_t)policy->guard_gap_size > (uint64_t)policy->regions[VM_LAYOUT_REGION_DATA].base) {
        return false;
    }
    if ((uint64_t)policy->regions[VM_LAYOUT_REGION_DATA].limit + (uint64_t)policy->guard_gap_size > (uint64_t)policy->regions[VM_LAYOUT_REGION_CONST].base) {
        return false;
    }
    if ((uint64_t)policy->regions[VM_LAYOUT_REGION_CONST].limit + (uint64_t)policy->guard_gap_size > (uint64_t)policy->regions[VM_LAYOUT_REGION_HEAP].base) {
        return false;
    }
    if ((uint64_t)policy->regions[VM_LAYOUT_REGION_HEAP].limit + (uint64_t)policy->guard_gap_size > (uint64_t)policy->regions[VM_LAYOUT_REGION_STACK].base) {
        return false;
    }

    return true;
}

const char *vm_layout_mode_name(VmLayoutMode mode) {
    switch (mode) {
        case VM_LAYOUT_MODE_FIXED:
            return "fixed";
        default:
            return NULL;
    }
}

const char *vm_layout_safety_tier_name(VmLayoutSafetyTier tier) {
    switch (tier) {
        case VM_LAYOUT_SAFETY_TIER_BEGINNER:
            return "beginner";
        case VM_LAYOUT_SAFETY_TIER_DEBUG:
            return "debug";
        case VM_LAYOUT_SAFETY_TIER_ROBUSTNESS:
            return "robustness";
        default:
            return NULL;
    }
}
