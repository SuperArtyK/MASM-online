/*
 * @file vm_layout.c
 * @brief Memory layout policy defaults for the MASM32 educational VM.
 *
 * This file centralizes fixed educational layout addresses, sizes, safety-tier
 * limits, alignment, guard-gap, stack-size, heap-size, automatic deterministic
 * sizing, and random-seed policy defaults so the loader can consume one
 * explicit layout policy object.
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

/// Clears an automatic-layout diagnostic to a known status.
///
/// @param diagnostic Optional diagnostic to clear.
/// @param status Status value to store.
static void vm_layout_clear_diagnostic(VmLayoutDiagnostic *diagnostic, VmLayoutStatus status) {
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->status = status;
    diagnostic->region = VM_LAYOUT_REGION_CODE;
    diagnostic->has_region = false;
    diagnostic->requested_size = 0U;
    diagnostic->limit = 0U;
    diagnostic->total_size = 0U;
    diagnostic->total_limit = 0U;
}

/// Fills an automatic-layout resource diagnostic for one region.
///
/// @param diagnostic Optional diagnostic to fill.
/// @param status Status value to store.
/// @param region Region associated with the failure.
/// @param requested_size Computed requested region size.
/// @param limit Configured region limit.
static void vm_layout_set_region_diagnostic(
    VmLayoutDiagnostic *diagnostic,
    VmLayoutStatus status,
    VmLayoutRegionKind region,
    uint32_t requested_size,
    uint32_t limit
) {
    vm_layout_clear_diagnostic(diagnostic, status);
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->region = region;
    diagnostic->has_region = true;
    diagnostic->requested_size = requested_size;
    diagnostic->limit = limit;
}

/// Fills an automatic-layout total-reservation diagnostic.
///
/// @param diagnostic Optional diagnostic to fill.
/// @param total_size Computed total reservation size.
/// @param total_limit Configured total reservation limit.
static void vm_layout_set_total_diagnostic(VmLayoutDiagnostic *diagnostic, uint32_t total_size, uint32_t total_limit) {
    vm_layout_clear_diagnostic(diagnostic, VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED);
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->total_size = total_size;
    diagnostic->total_limit = total_limit;
}

/// Adds two unsigned 32-bit values with overflow detection.
///
/// @param left First addend.
/// @param right Second addend.
/// @param out_value Receives the sum on success.
/// @return true when the addition did not overflow.
static bool vm_layout_add_u32(uint32_t left, uint32_t right, uint32_t *out_value) {
    if (out_value == NULL || right > UINT32_MAX - left) {
        return false;
    }

    *out_value = left + right;
    return true;
}

/// Rounds a size up to a requested alignment with overflow checks.
///
/// @param value Size before alignment.
/// @param alignment Required nonzero alignment.
/// @param out_value Receives the aligned size on success.
/// @return true when the alignment operation succeeded.
static bool vm_layout_align_up_u32(uint32_t value, uint32_t alignment, uint32_t *out_value) {
    uint32_t remainder = 0U;
    uint32_t padding = 0U;

    if (out_value == NULL || alignment == 0U) {
        return false;
    }

    remainder = value % alignment;
    if (remainder == 0U) {
        *out_value = value;
        return true;
    }

    padding = alignment - remainder;
    if (padding > UINT32_MAX - value) {
        return false;
    }

    *out_value = value + padding;
    return true;
}

/// Applies minimum-size and alignment rules to one automatic region size.
///
/// @param raw_size Unaligned requested size.
/// @param minimum_size Minimum region size from the policy.
/// @param alignment Required region alignment.
/// @param out_size Receives the aligned size on success.
/// @return true when the calculation did not overflow.
static bool vm_layout_compute_aligned_region_size(uint32_t raw_size, uint32_t minimum_size, uint32_t alignment, uint32_t *out_size) {
    uint32_t size = raw_size;

    if (out_size == NULL) {
        return false;
    }

    if (size < minimum_size) {
        size = minimum_size;
    }

    return vm_layout_align_up_u32(size, alignment, out_size);
}

/// Computes one region base/limit pair for an automatic policy.
///
/// @param base Inclusive region base.
/// @param size Aligned region size in bytes.
/// @param out_limit Receives the exclusive limit.
/// @return true when base + size did not overflow and size is nonzero.
static bool vm_layout_compute_limit(uint32_t base, uint32_t size, uint32_t *out_limit) {
    if (out_limit == NULL || size == 0U || size > UINT32_MAX - base) {
        return false;
    }

    *out_limit = base + size;
    return true;
}

/// Applies one computed size to an automatic region policy.
///
/// @param policy Policy to mutate.
/// @param kind Region to update.
/// @param size Aligned region size.
/// @param out_diagnostic Optional failure diagnostic.
/// @return Layout status for the update.
static VmLayoutStatus vm_layout_apply_automatic_region_size(
    VmLayoutPolicy *policy,
    VmLayoutRegionKind kind,
    uint32_t size,
    VmLayoutDiagnostic *out_diagnostic
) {
    VmLayoutRegionPolicy *region = NULL;
    uint32_t maximum = 0U;
    uint32_t limit = 0U;

    if (policy == NULL || !vm_layout_region_kind_is_valid(kind) || !vm_layout_safety_tier_is_valid(policy->safety_tier)) {
        vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
        return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
    }

    region = &policy->regions[kind];
    maximum = region->maximum_size_by_tier[policy->safety_tier];
    if (size > maximum) {
        vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, kind, size, maximum);
        return VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED;
    }

    if (kind == VM_LAYOUT_REGION_STACK) {
        if (size > region->limit) {
            vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INTEGER_OVERFLOW, kind, size, region->limit);
            return VM_LAYOUT_STATUS_INTEGER_OVERFLOW;
        }
        region->base = region->limit - size;
        return VM_LAYOUT_STATUS_OK;
    }

    if (!vm_layout_compute_limit(region->base, size, &limit)) {
        vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INTEGER_OVERFLOW, kind, size, maximum);
        return VM_LAYOUT_STATUS_INTEGER_OVERFLOW;
    }

    region->limit = limit;
    return VM_LAYOUT_STATUS_OK;
}

/// Verifies that automatically sized regions remain non-overlapping.
///
/// @param policy Policy whose computed automatic ranges should be checked.
/// @param out_diagnostic Optional resource-limit diagnostic for the first overlap.
/// @return VM_LAYOUT_STATUS_OK when region ranges are ordered and separated.
static VmLayoutStatus vm_layout_check_automatic_region_order(VmLayoutPolicy *policy, VmLayoutDiagnostic *out_diagnostic) {
    size_t index = 0U;

    if (policy == NULL) {
        vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
        return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index + 1U < (size_t)VM_LAYOUT_REGION_COUNT; index += 1U) {
        VmLayoutRegionPolicy *previous = &policy->regions[index];
        VmLayoutRegionPolicy *next = &policy->regions[index + 1U];
        uint32_t previous_size = 0U;
        uint32_t allowed_size = 0U;

        if (!vm_layout_region_size(previous, &previous_size)) {
            vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
            return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
        }

        if ((uint64_t)previous->limit + (uint64_t)policy->guard_gap_size <= (uint64_t)next->base) {
            continue;
        }

        if (index == (size_t)VM_LAYOUT_REGION_HEAP) {
            uint32_t stack_size = 0U;
            uint32_t stack_limit = next->limit;
            uint32_t heap_allowed_size = 0U;
            uint32_t stack_allowed_size = 0U;

            if (!vm_layout_region_size(next, &stack_size)) {
                vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
                return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
            }

            if ((uint64_t)next->base < (uint64_t)previous->base) {
                if ((uint64_t)previous->limit + (uint64_t)policy->guard_gap_size <= (uint64_t)stack_limit) {
                    stack_allowed_size = stack_limit - previous->limit - policy->guard_gap_size;
                }
                vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, VM_LAYOUT_REGION_STACK, stack_size, stack_allowed_size);
                return VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED;
            }

            if ((uint64_t)next->base >= (uint64_t)previous->base + (uint64_t)policy->guard_gap_size) {
                heap_allowed_size = next->base - previous->base - policy->guard_gap_size;
            }
            if (previous_size > heap_allowed_size) {
                vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, VM_LAYOUT_REGION_HEAP, previous_size, heap_allowed_size);
                return VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED;
            }

            if ((uint64_t)previous->limit + (uint64_t)policy->guard_gap_size <= (uint64_t)stack_limit) {
                stack_allowed_size = stack_limit - previous->limit - policy->guard_gap_size;
            }
            vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, VM_LAYOUT_REGION_STACK, stack_size, stack_allowed_size);
            return VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED;
        }

        if ((uint64_t)next->base >= (uint64_t)previous->base + (uint64_t)policy->guard_gap_size) {
            allowed_size = next->base - previous->base - policy->guard_gap_size;
        }
        vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED, previous->kind, previous_size, allowed_size);
        return VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED;
    }

    return VM_LAYOUT_STATUS_OK;
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

    if (policy == NULL || (policy->mode != VM_LAYOUT_MODE_FIXED && policy->mode != VM_LAYOUT_MODE_AUTOMATIC) || !vm_layout_safety_tier_is_valid(policy->safety_tier)) {
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


VmLayoutStatus vm_layout_build_automatic_policy(
    const VmLayoutPolicy *base_policy,
    const VmLayoutProgramMetadata *metadata,
    VmLayoutPolicy *out_policy,
    VmLayoutDiagnostic *out_diagnostic
) {
    VmLayoutPolicy policy;
    VmLayoutStatus status = VM_LAYOUT_STATUS_OK;
    uint32_t data_size = 0U;
    uint32_t computed_sizes[VM_LAYOUT_REGION_COUNT];
    uint32_t total_size = 0U;
    uint32_t total_limit = 0U;
    size_t index = 0U;

    vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_OK);

    if (metadata == NULL || out_policy == NULL) {
        vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
        return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
    }

    policy = base_policy != NULL ? *base_policy : vm_layout_default_policy();
    if (!vm_layout_safety_tier_is_valid(policy.safety_tier) || policy.region_alignment == 0U) {
        vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
        return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
    }

    if (base_policy != NULL && (base_policy->mode != VM_LAYOUT_MODE_FIXED && base_policy->mode != VM_LAYOUT_MODE_AUTOMATIC)) {
        vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
        return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
    }

    if (!vm_layout_add_u32(metadata->initialized_data_size, metadata->uninitialized_data_size, &data_size)) {
        vm_layout_set_region_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INTEGER_OVERFLOW, VM_LAYOUT_REGION_DATA, UINT32_MAX, policy.regions[VM_LAYOUT_REGION_DATA].maximum_size_by_tier[policy.safety_tier]);
        return VM_LAYOUT_STATUS_INTEGER_OVERFLOW;
    }

    if (!vm_layout_compute_aligned_region_size(metadata->code_size, policy.regions[VM_LAYOUT_REGION_CODE].minimum_size, policy.region_alignment, &computed_sizes[VM_LAYOUT_REGION_CODE]) ||
        !vm_layout_compute_aligned_region_size(data_size, policy.regions[VM_LAYOUT_REGION_DATA].minimum_size, policy.region_alignment, &computed_sizes[VM_LAYOUT_REGION_DATA]) ||
        !vm_layout_compute_aligned_region_size(metadata->const_size, policy.regions[VM_LAYOUT_REGION_CONST].minimum_size, policy.region_alignment, &computed_sizes[VM_LAYOUT_REGION_CONST]) ||
        !vm_layout_compute_aligned_region_size(metadata->has_heap_size_request ? metadata->heap_size_request : policy.heap_size_request, policy.regions[VM_LAYOUT_REGION_HEAP].minimum_size, policy.region_alignment, &computed_sizes[VM_LAYOUT_REGION_HEAP]) ||
        !vm_layout_compute_aligned_region_size(metadata->has_stack_size_request ? metadata->stack_size_request : policy.stack_size_request, policy.regions[VM_LAYOUT_REGION_STACK].minimum_size, policy.region_alignment, &computed_sizes[VM_LAYOUT_REGION_STACK])) {
        vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INTEGER_OVERFLOW);
        return VM_LAYOUT_STATUS_INTEGER_OVERFLOW;
    }

    policy.mode = VM_LAYOUT_MODE_AUTOMATIC;
    policy.heap_size_request = metadata->has_heap_size_request ? metadata->heap_size_request : policy.heap_size_request;
    policy.stack_size_request = metadata->has_stack_size_request ? metadata->stack_size_request : policy.stack_size_request;

    for (index = 0U; index < (size_t)VM_LAYOUT_REGION_COUNT; index += 1U) {
        status = vm_layout_apply_automatic_region_size(&policy, (VmLayoutRegionKind)index, computed_sizes[index], out_diagnostic);
        if (status != VM_LAYOUT_STATUS_OK) {
            return status;
        }

        if (!vm_layout_add_u32(total_size, computed_sizes[index], &total_size)) {
            vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INTEGER_OVERFLOW);
            return VM_LAYOUT_STATUS_INTEGER_OVERFLOW;
        }
    }

    total_limit = policy.maximum_total_reservation_by_tier[policy.safety_tier];
    if (total_size > total_limit) {
        vm_layout_set_total_diagnostic(out_diagnostic, total_size, total_limit);
        return VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED;
    }

    status = vm_layout_check_automatic_region_order(&policy, out_diagnostic);
    if (status != VM_LAYOUT_STATUS_OK) {
        return status;
    }

    if (!vm_layout_policy_is_valid(&policy)) {
        vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_INVALID_ARGUMENT);
        return VM_LAYOUT_STATUS_INVALID_ARGUMENT;
    }

    *out_policy = policy;
    vm_layout_clear_diagnostic(out_diagnostic, VM_LAYOUT_STATUS_OK);
    return VM_LAYOUT_STATUS_OK;
}

bool vm_layout_status_succeeded(VmLayoutStatus status) {
    return status == VM_LAYOUT_STATUS_OK;
}

const char *vm_layout_status_name(VmLayoutStatus status) {
    switch (status) {
        case VM_LAYOUT_STATUS_OK:
            return "ok";
        case VM_LAYOUT_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_LAYOUT_STATUS_INTEGER_OVERFLOW:
            return "integer-overflow";
        case VM_LAYOUT_STATUS_RESOURCE_LIMIT_EXCEEDED:
            return "resource-limit-exceeded";
        default:
            return NULL;
    }
}

const char *vm_layout_mode_name(VmLayoutMode mode) {
    switch (mode) {
        case VM_LAYOUT_MODE_FIXED:
            return "fixed";
        case VM_LAYOUT_MODE_AUTOMATIC:
            return "automatic";
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
