/*
 * @file vm_layout.h
 * @brief Memory layout policy defaults for the MASM32 educational VM.
 *
 * This module centralizes fixed educational memory layout defaults and the
 * policy object that later layout phases can extend. Phase 32 uses only the
 * fixed layout mode and deliberately keeps automatic sizing, randomized bases,
 * object-bounds diagnostics, stack behavior, and heap allocation out of scope.
 */

#ifndef MASM32_SIM_VM_LAYOUT_H
#define MASM32_SIM_VM_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

/// Fixed educational .code region base address.
#define VM_LAYOUT_FIXED_CODE_BASE 0x00400000U

/// Fixed educational .data/.DATA? region base address.
#define VM_LAYOUT_FIXED_DATA_BASE 0x00500000U

/// Fixed educational read-only .const region base address.
#define VM_LAYOUT_FIXED_CONST_BASE 0x00600000U

/// Fixed educational heap region base address.
#define VM_LAYOUT_FIXED_HEAP_BASE 0x00700000U

/// Fixed educational stack exclusive top address.
#define VM_LAYOUT_FIXED_STACK_TOP 0x00900000U

/// Fixed educational .code region size in bytes.
#define VM_LAYOUT_FIXED_CODE_SIZE 0x00100000U

/// Fixed educational .data/.DATA? region size in bytes.
#define VM_LAYOUT_FIXED_DATA_SIZE 0x00100000U

/// Fixed educational read-only .const region size in bytes.
#define VM_LAYOUT_FIXED_CONST_SIZE 0x00100000U

/// Fixed educational heap region size in bytes.
#define VM_LAYOUT_FIXED_HEAP_SIZE 0x00100000U

/// Fixed educational stack region size in bytes.
#define VM_LAYOUT_FIXED_STACK_SIZE 0x00010000U

/// Named default stack-size request used before stack runtime behavior exists.
#define VM_LAYOUT_DEFAULT_STACK_SIZE_REQUEST VM_LAYOUT_FIXED_STACK_SIZE

/// Named default heap-size request used before heap allocation behavior exists.
#define VM_LAYOUT_DEFAULT_HEAP_SIZE_REQUEST VM_LAYOUT_FIXED_HEAP_SIZE

/// Minimum accepted code-region size in fixed layout compatibility mode.
#define VM_LAYOUT_MIN_CODE_SIZE 1U

/// Minimum accepted data-region size in fixed layout compatibility mode.
#define VM_LAYOUT_MIN_DATA_SIZE 1U

/// Minimum accepted const-region size in fixed layout compatibility mode.
#define VM_LAYOUT_MIN_CONST_SIZE 1U

/// Minimum accepted heap-region size in fixed layout compatibility mode.
#define VM_LAYOUT_MIN_HEAP_SIZE 1U

/// Minimum accepted stack-region size in fixed layout compatibility mode.
#define VM_LAYOUT_MIN_STACK_SIZE 1U

/// Region alignment advertised by the fixed educational layout policy.
#define VM_LAYOUT_DEFAULT_REGION_ALIGNMENT 0x00001000U

/// Guard-gap size for the current fixed educational layout policy.
#define VM_LAYOUT_DEFAULT_GUARD_GAP_SIZE 0U

/// Placeholder deterministic seed value reserved for future randomized layouts.
#define VM_LAYOUT_DEFAULT_RANDOM_SEED 0U

/// Maximum fixed-layout total reservation in the default safety tier.
#define VM_LAYOUT_DEFAULT_MAX_TOTAL_RESERVATION 0x02000000U

/// Identifies a selectable memory layout mode.
typedef enum VmLayoutMode {
    /// Current fixed educational layout mode.
    VM_LAYOUT_MODE_FIXED = 0
} VmLayoutMode;

/// Identifies a memory-layout safety tier.
typedef enum VmLayoutSafetyTier {
    /// Default beginner-friendly safety tier used by current tests and source-run.
    VM_LAYOUT_SAFETY_TIER_BEGINNER = 0,
    /// Debug-oriented future tier placeholder.
    VM_LAYOUT_SAFETY_TIER_DEBUG,
    /// Robustness-oriented future tier placeholder.
    VM_LAYOUT_SAFETY_TIER_ROBUSTNESS,
    /// Number of layout safety tiers.
    VM_LAYOUT_SAFETY_TIER_COUNT
} VmLayoutSafetyTier;

/// Identifies one policy-managed memory region.
typedef enum VmLayoutRegionKind {
    /// Simulated .code region.
    VM_LAYOUT_REGION_CODE = 0,
    /// Simulated .data and .DATA? region.
    VM_LAYOUT_REGION_DATA,
    /// Simulated read-only .const region.
    VM_LAYOUT_REGION_CONST,
    /// Simulated heap region.
    VM_LAYOUT_REGION_HEAP,
    /// Simulated stack region.
    VM_LAYOUT_REGION_STACK,
    /// Number of policy-managed regions.
    VM_LAYOUT_REGION_COUNT
} VmLayoutRegionKind;

/// Describes bounds and size policy for one memory region.
typedef struct VmLayoutRegionPolicy {
    /// Region kind described by this entry.
    VmLayoutRegionKind kind;
    /// Inclusive fixed-layout base address for this region.
    uint32_t base;
    /// Exclusive fixed-layout limit address for this region.
    uint32_t limit;
    /// Minimum accepted region size in bytes.
    uint32_t minimum_size;
    /// Named default region size in bytes.
    uint32_t default_size;
    /// Maximum allowed size for each safety tier.
    uint32_t maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_COUNT];
} VmLayoutRegionPolicy;

/// Describes the selected VM memory layout policy.
typedef struct VmLayoutPolicy {
    /// Selected layout mode. Phase 32 supports only fixed layout.
    VmLayoutMode mode;
    /// Selected safety tier used for maximum-size validation.
    VmLayoutSafetyTier safety_tier;
    /// Requested stack size in bytes, or the default request when unset by source metadata.
    uint32_t stack_size_request;
    /// Requested heap size in bytes, or the default request when unset by configuration.
    uint32_t heap_size_request;
    /// Required region alignment in bytes for future layout modes.
    uint32_t region_alignment;
    /// Required guard gap in bytes between regions for future layout modes.
    uint32_t guard_gap_size;
    /// Whether @ref random_seed contains a caller-selected seed.
    bool has_random_seed;
    /// Optional seed placeholder for future seeded randomized layout phases.
    uint32_t random_seed;
    /// Region bounds, defaults, minimums, and maximums indexed by VmLayoutRegionKind.
    VmLayoutRegionPolicy regions[VM_LAYOUT_REGION_COUNT];
    /// Maximum total reserved memory allowed by each safety tier.
    uint32_t maximum_total_reservation_by_tier[VM_LAYOUT_SAFETY_TIER_COUNT];
} VmLayoutPolicy;

/// Returns the default fixed educational layout policy.
///
/// @return Fixed educational memory layout policy used by default source-run.
VmLayoutPolicy vm_layout_default_policy(void);

/// Retrieves one region entry from a layout policy.
///
/// @param policy Layout policy to inspect.
/// @param kind Region kind to retrieve.
/// @return Region policy entry, or NULL for invalid input.
const VmLayoutRegionPolicy *vm_layout_policy_get_region(const VmLayoutPolicy *policy, VmLayoutRegionKind kind);

/// Computes the byte size of a region policy entry.
///
/// @param region Region policy to inspect.
/// @param out_size Receives the byte size on success.
/// @return true when the range is valid and @p out_size was written.
bool vm_layout_region_size(const VmLayoutRegionPolicy *region, uint32_t *out_size);

/// Validates a layout policy for the currently supported fixed layout mode.
///
/// @param policy Layout policy to validate.
/// @return true when the policy can initialize VM memory safely.
bool vm_layout_policy_is_valid(const VmLayoutPolicy *policy);

/// Returns a stable lowercase name for a layout mode.
///
/// @param mode Layout mode to inspect.
/// @return Static mode name, or NULL for invalid mode values.
const char *vm_layout_mode_name(VmLayoutMode mode);

/// Returns a stable lowercase name for a layout safety tier.
///
/// @param tier Safety tier to inspect.
/// @return Static tier name, or NULL for invalid tier values.
const char *vm_layout_safety_tier_name(VmLayoutSafetyTier tier);

#endif
