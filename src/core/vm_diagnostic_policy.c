/*
 * @file vm_diagnostic_policy.c
 * @brief Shared diagnostic-policy registry metadata implementation.
 *
 * The registry keeps optional teaching diagnostic family names and the common
 * off/warn/error policy vocabulary in one C99 module. Phase 57D routes existing
 * configurable families through registry-backed compatibility helpers. Phase
 * 57E activates the startup-state notice family without changing startup
 * values or runtime/source-run MASM behavior metadata. Phase 57J activates
 * `.CONST` uninitialized-storage declaration diagnostics.
 */

#include "vm_diagnostic_policy.h"

#include <string.h>

/// Stores one policy value name mapping.
typedef struct VmDiagnosticPolicyValueInfo {
    /// Policy value identifier.
    VmDiagnosticPolicyValue value;
    /// Stable lowercase policy value name.
    const char *name;
} VmDiagnosticPolicyValueInfo;

/// Common diagnostic-policy value names.
static const VmDiagnosticPolicyValueInfo VM_DIAGNOSTIC_POLICY_VALUE_TABLE[] = {
    {VM_DIAGNOSTIC_POLICY_VALUE_OFF, "off"},
    {VM_DIAGNOSTIC_POLICY_VALUE_WARN, "warn"},
    {VM_DIAGNOSTIC_POLICY_VALUE_ERROR, "error"},
};

/// Shared registry entries for current and reserved diagnostic-policy families.
///
/// Future phases should add new optional diagnostic families here instead of
/// creating independent parser flags, source-run flags, UI-only checks, or
/// environment-variable-only settings. A family may be added as
/// VM_DIAGNOSTIC_POLICY_FAMILY_STATE_RESERVED_INACTIVE before behavior exists,
/// then changed to VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED when the owning
/// phase adds behavior and tests.
static const VmDiagnosticPolicyFamilyInfo VM_DIAGNOSTIC_POLICY_FAMILY_TABLE[] = {
    {
        VM_DIAGNOSTIC_POLICY_FAMILY_UNINITIALIZED_READ,
        "uninitialized-read",
        VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED,
        true,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN,
    },
    {
        VM_DIAGNOSTIC_POLICY_FAMILY_UNDEFINED_FLAG_USE,
        "undefined-flag-use",
        VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED,
        true,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN,
    },
    {
        VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE,
        "compatibility-notice",
        VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED,
        true,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN,
    },
    {
        VM_DIAGNOSTIC_POLICY_FAMILY_CONST_UNINITIALIZED_STORAGE,
        "const-uninitialized-storage",
        VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED,
        true,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN,
    },
    {
        VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE,
        "startup-state-notice",
        VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED,
        true,
        VM_DIAGNOSTIC_POLICY_VALUE_WARN,
    },
    {
        VM_DIAGNOSTIC_POLICY_FAMILY_CODE_IMAGE_READ,
        "code-image-read",
        VM_DIAGNOSTIC_POLICY_FAMILY_STATE_RESERVED_INACTIVE,
        false,
        VM_DIAGNOSTIC_POLICY_VALUE_OFF,
    },
};

const char *vm_diagnostic_policy_value_name(VmDiagnosticPolicyValue value) {
    size_t index = 0U;

    for (index = 0U; index < (sizeof(VM_DIAGNOSTIC_POLICY_VALUE_TABLE) / sizeof(VM_DIAGNOSTIC_POLICY_VALUE_TABLE[0])); ++index) {
        if (VM_DIAGNOSTIC_POLICY_VALUE_TABLE[index].value == value) {
            return VM_DIAGNOSTIC_POLICY_VALUE_TABLE[index].name;
        }
    }

    return NULL;
}

bool vm_diagnostic_policy_parse_value(const char *text, VmDiagnosticPolicyValue *out_value) {
    size_t index = 0U;

    if (text == NULL || out_value == NULL) {
        return false;
    }

    for (index = 0U; index < (sizeof(VM_DIAGNOSTIC_POLICY_VALUE_TABLE) / sizeof(VM_DIAGNOSTIC_POLICY_VALUE_TABLE[0])); ++index) {
        if (strcmp(text, VM_DIAGNOSTIC_POLICY_VALUE_TABLE[index].name) == 0) {
            *out_value = VM_DIAGNOSTIC_POLICY_VALUE_TABLE[index].value;
            return true;
        }
    }

    return false;
}

const char *vm_diagnostic_policy_family_name(VmDiagnosticPolicyFamily family) {
    const VmDiagnosticPolicyFamilyInfo *info = vm_diagnostic_policy_family_info(family);

    return info != NULL ? info->name : NULL;
}

bool vm_diagnostic_policy_parse_family(const char *text, VmDiagnosticPolicyFamily *out_family) {
    size_t index = 0U;

    if (text == NULL || out_family == NULL) {
        return false;
    }

    for (index = 0U; index < vm_diagnostic_policy_family_count(); ++index) {
        if (strcmp(text, VM_DIAGNOSTIC_POLICY_FAMILY_TABLE[index].name) == 0) {
            *out_family = VM_DIAGNOSTIC_POLICY_FAMILY_TABLE[index].family;
            return true;
        }
    }

    return false;
}

const VmDiagnosticPolicyFamilyInfo *vm_diagnostic_policy_family_info(VmDiagnosticPolicyFamily family) {
    size_t index = 0U;

    for (index = 0U; index < vm_diagnostic_policy_family_count(); ++index) {
        if (VM_DIAGNOSTIC_POLICY_FAMILY_TABLE[index].family == family) {
            return &VM_DIAGNOSTIC_POLICY_FAMILY_TABLE[index];
        }
    }

    return NULL;
}

size_t vm_diagnostic_policy_family_count(void) {
    return sizeof(VM_DIAGNOSTIC_POLICY_FAMILY_TABLE) / sizeof(VM_DIAGNOSTIC_POLICY_FAMILY_TABLE[0]);
}


bool vm_diagnostic_policy_family_is_implemented(VmDiagnosticPolicyFamily family) {
    const VmDiagnosticPolicyFamilyInfo *info = vm_diagnostic_policy_family_info(family);

    return info != NULL && info->state == VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED;
}

bool vm_diagnostic_policy_family_default_value(VmDiagnosticPolicyFamily family, VmDiagnosticPolicyValue *out_value) {
    const VmDiagnosticPolicyFamilyInfo *info = vm_diagnostic_policy_family_info(family);

    if (out_value == NULL || info == NULL || info->state != VM_DIAGNOSTIC_POLICY_FAMILY_STATE_IMPLEMENTED || !info->has_default_value) {
        return false;
    }

    *out_value = info->default_value;
    return true;
}

bool vm_diagnostic_policy_family_accepts_value(VmDiagnosticPolicyFamily family, VmDiagnosticPolicyValue value) {
    if (!vm_diagnostic_policy_family_is_implemented(family) ||
        value < VM_DIAGNOSTIC_POLICY_VALUE_OFF ||
        value >= VM_DIAGNOSTIC_POLICY_VALUE_COUNT) {
        return false;
    }

    if (family == VM_DIAGNOSTIC_POLICY_FAMILY_COMPATIBILITY_NOTICE ||
        family == VM_DIAGNOSTIC_POLICY_FAMILY_STARTUP_STATE_NOTICE) {
        return value == VM_DIAGNOSTIC_POLICY_VALUE_OFF || value == VM_DIAGNOSTIC_POLICY_VALUE_WARN;
    }

    return true;
}

bool vm_diagnostic_policy_family_is_reserved(VmDiagnosticPolicyFamily family) {
    const VmDiagnosticPolicyFamilyInfo *info = vm_diagnostic_policy_family_info(family);

    return info != NULL && info->state == VM_DIAGNOSTIC_POLICY_FAMILY_STATE_RESERVED_INACTIVE;
}
