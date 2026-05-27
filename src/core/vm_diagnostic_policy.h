/*
 * @file vm_diagnostic_policy.h
 * @brief Shared diagnostic-policy registry metadata for the MASM32 simulator.
 *
 * This module defines the central policy vocabulary and family registry used by
 * current and future optional teaching diagnostics. Existing configurable
 * diagnostic settings may use narrow compatibility adapters when their public
 * setting names predate the registry vocabulary.
 */

#ifndef MASM32_SIM_VM_DIAGNOSTIC_POLICY_H
#define MASM32_SIM_VM_DIAGNOSTIC_POLICY_H

#include <stdbool.h>
#include <stddef.h>

/// Identifies one common policy value for optional teaching diagnostics.
typedef enum VmDiagnosticPolicyValue {
    /// Do not emit this optional teaching diagnostic or notice.
    VM_DIAGNOSTIC_POLICY_VALUE_OFF = 0,
    /// Emit a non-fatal Simulator Message and continue when no lower-level fatal error occurs.
    VM_DIAGNOSTIC_POLICY_VALUE_WARN,
    /// Emit a fatal diagnostic and stop before affected runtime mutation when applicable.
    VM_DIAGNOSTIC_POLICY_VALUE_ERROR,
    /// Number of common diagnostic-policy values.
    VM_DIAGNOSTIC_POLICY_VALUE_COUNT
} VmDiagnosticPolicyValue;

/// Identifies one optional diagnostic or notice policy family.
typedef enum VmDiagnosticPolicyFamily {
    /// Memory reads that consume bytes still marked as uninitialized-origin storage.
    VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ = 0,
    /// Flag consumers that read modeled flags whose current values are architecturally undefined.
    VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE,
    /// Informational notices for accepted no-op, metadata-only, virtual-only, or limited MASM compatibility constructs.
    VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE,
    /// Reserved family for future `.CONST ?` and `.CONST DUP(?)` compatibility diagnostics.
    VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE,
    /// Informational notice explaining deterministic simulator startup state.
    VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE,
    /// Reserved family for future `.code` image memory-read diagnostics.
    VM_DIAGNOSTIC_POLICY_FAMILY_CODE_IMAGE_READ,
    /// Number of known diagnostic-policy families.
    VM_DIAGNOSTIC_POLICY_FAMILY_COUNT
} VmDiagnosticPolicyFamily;

/// Describes whether a registry family has behavior in the current repository state.
typedef enum VmDiagnosticPolicyFamilyState {
    /// The family has implemented behavior and may be configured directly or through a compatibility adapter.
    VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED = 0,
    /// The family name is reserved for a future phase and must not activate behavior yet.
    VM_DIAGNOSTIC_POLICY_FAMILY_STATE_RESERVED_INACTIVE
} VmDiagnosticPolicyFamilyState;

/// Describes one diagnostic-policy family registry entry.
typedef struct VmDiagnosticPolicyFamilyInfo {
    /// Stable family identifier.
    VmDiagnosticPolicyFamily family;
    /// Stable lowercase hyphenated family name used in documentation and future settings paths.
    const char *name;
    /// Current implementation state for this family.
    VmDiagnosticPolicyFamilyState state;
    /// True when @ref default_value has a meaningful current default.
    bool has_default_value;
    /// Current default policy value when @ref has_default_value is true.
    VmDiagnosticPolicyValue default_value;
} VmDiagnosticPolicyFamilyInfo;

/// Formats one common diagnostic-policy value.
///
/// @param value Policy value to format.
/// @return Stable lowercase string for known values, otherwise NULL.
const char *vm_diagnostic_policy_value_name(VmDiagnosticPolicyValue value);

/// Parses one common diagnostic-policy value.
///
/// Parsing is exact and lowercase. Compatibility aliases such as `strict` are
/// intentionally not accepted by the central vocabulary. Phase 57D compatibility
/// adapters map legacy public setting values to this vocabulary explicitly.
///
/// @param text Candidate policy value text.
/// @param out_value Destination for the parsed value.
/// @return true when @p text is a known policy value and @p out_value was written.
bool vm_diagnostic_policy_parse_value(const char *text, VmDiagnosticPolicyValue *out_value);

/// Formats one diagnostic-policy family identifier.
///
/// @param family Family identifier to format.
/// @return Stable lowercase hyphenated family name for known families, otherwise NULL.
const char *vm_diagnostic_policy_family_name(VmDiagnosticPolicyFamily family);

/// Parses one diagnostic-policy family name.
///
/// Reserved inactive families are intentionally parsed successfully so later
/// phases can discover reserved names without enabling their behavior.
///
/// @param text Candidate family name text.
/// @param out_family Destination for the parsed family identifier.
/// @return true when @p text is a known family name and @p out_family was written.
bool vm_diagnostic_policy_parse_family(const char *text, VmDiagnosticPolicyFamily *out_family);

/// Returns registry metadata for one diagnostic-policy family.
///
/// @param family Family identifier to inspect.
/// @return Pointer to static registry metadata for known families, otherwise NULL.
const VmDiagnosticPolicyFamilyInfo *vm_diagnostic_policy_family_info(VmDiagnosticPolicyFamily family);

/// Returns the number of known diagnostic-policy families.
///
/// @return Count of entries in the current registry table.
size_t vm_diagnostic_policy_family_count(void);


/// Reports whether a diagnostic-policy family has implemented behavior now.
///
/// @param family Family identifier to inspect.
/// @return true when the family is known and currently implemented, otherwise false.
bool vm_diagnostic_policy_family_is_implemented(VmDiagnosticPolicyFamily family);

/// Returns the current default value for one implemented diagnostic-policy family.
///
/// Reserved inactive families intentionally have no current default value.
///
/// @param family Family identifier to inspect.
/// @param out_value Destination for the current default value.
/// @return true when the family is known, implemented, and has a default value.
bool vm_diagnostic_policy_family_default_value(VmDiagnosticPolicyFamily family, VmDiagnosticPolicyValue *out_value);

/// Reports whether a policy value is accepted for an implemented family.
///
/// Implemented teaching-diagnostic families accept the shared off/warn/error
/// vocabulary when their current behavior includes a fatal mode. Compatibility
/// notices and startup-state notices currently accept only off/warn because
/// they are non-fatal notices. Reserved inactive families reject all values so
/// future diagnostics are not accidentally activated by name.
///
/// @param family Family identifier to inspect.
/// @param value Policy value to validate for that family.
/// @return true when the family is implemented and currently accepts @p value.
bool vm_diagnostic_policy_family_accepts_value(VmDiagnosticPolicyFamily family, VmDiagnosticPolicyValue value);

/// Reports whether a diagnostic-policy family is reserved for future behavior.
///
/// @param family Family identifier to inspect.
/// @return true when the family is known and reserved inactive, otherwise false.
bool vm_diagnostic_policy_family_is_reserved(VmDiagnosticPolicyFamily family);

#endif
