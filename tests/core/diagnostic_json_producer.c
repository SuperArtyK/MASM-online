/*
 * @file diagnostic_json_producer.c
 * @brief Native source-run JSON producer for diagnostic rendering tests.
 *
 * This utility calls the same source-run JSON API used by the browser Wasm
 * boundary, but it builds with the native C test toolchain and does not require
 * Emscripten. It prints only the raw JSON payload on stdout so Node tests can
 * feed the exact result through the browser Simulator Messages formatter.
 */

/*
 * Suppress Microsoft CRT deprecation annotations for fopen/getenv when this
 * portable C99 test utility is compiled by Clang against the MSVC/UCRT headers.
 * Some clang driver configurations include the UCRT headers without defining
 * _MSC_VER early enough for a guarded MSVC-only definition to be reliable. The
 * project warning policy still treats ordinary compiler warnings as errors.
 */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include "../../src/wasm/wasm_api.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Number of bytes read from fixture input per stream read operation.
#define DIAGNOSTIC_JSON_PRODUCER_READ_CHUNK_BYTES 4096U

/// Initial source-buffer capacity used before growing for larger fixtures.
#define DIAGNOSTIC_JSON_PRODUCER_INITIAL_CAPACITY 4096U

/// Reports one utility error to stderr.
///
/// @param message Human-readable error description.
/// @return Always returns failure status code 1.
static int diagnostic_json_producer_fail(const char *message) {
    fprintf(stderr, "diagnostic_json_producer: %s\n", message != NULL ? message : "unknown error");
    return 1;
}

/// Opens the requested fixture input stream.
///
/// @param argc Command-line argument count.
/// @param argv Command-line argument vector.
/// @param out_stream Receives the opened stream, or stdin when no path is supplied.
/// @param out_should_close Receives whether @p out_stream must be closed by the caller.
/// @return Zero on success, otherwise nonzero.
static int diagnostic_json_producer_open_input(int argc, char **argv, FILE **out_stream, int *out_should_close) {
    if (out_stream == NULL || out_should_close == NULL) {
        return diagnostic_json_producer_fail("invalid input stream output pointer");
    }

    *out_stream = NULL;
    *out_should_close = 0;

    if (argc > 2) {
        return diagnostic_json_producer_fail("usage: diagnostic_json_producer [source-file]");
    }

    if (argc == 2) {
        *out_stream = fopen(argv[1], "rb");
        if (*out_stream == NULL) {
            return diagnostic_json_producer_fail("could not open source fixture file");
        }
        *out_should_close = 1;
        return 0;
    }

    *out_stream = stdin;
    return 0;
}

/// Grows a dynamically allocated source buffer when additional capacity is needed.
///
/// @param buffer Current source buffer pointer.
/// @param capacity Current capacity in bytes.
/// @param required Minimum required capacity in bytes.
/// @return Grown source buffer pointer, or NULL on allocation failure.
static char *diagnostic_json_producer_grow_buffer(char *buffer, size_t *capacity, size_t required) {
    size_t new_capacity = 0U;
    char *grown = NULL;

    if (capacity == NULL) {
        free(buffer);
        return NULL;
    }

    new_capacity = *capacity == 0U ? (size_t)DIAGNOSTIC_JSON_PRODUCER_INITIAL_CAPACITY : *capacity;
    while (new_capacity < required) {
        if (new_capacity > ((size_t)-1) / 2U) {
            free(buffer);
            return NULL;
        }
        new_capacity *= 2U;
    }

    grown = (char *)realloc(buffer, new_capacity);
    if (grown == NULL) {
        free(buffer);
        return NULL;
    }

    *capacity = new_capacity;
    return grown;
}

/// Reads the complete fixture source from one stream.
///
/// @param stream Stream to read.
/// @param out_source Receives a null-terminated source buffer owned by the caller.
/// @return Zero on success, otherwise nonzero.
static int diagnostic_json_producer_read_source(FILE *stream, char **out_source) {
    char *source = NULL;
    size_t capacity = 0U;
    size_t length = 0U;

    if (stream == NULL || out_source == NULL) {
        return diagnostic_json_producer_fail("invalid source read argument");
    }

    source = diagnostic_json_producer_grow_buffer(source, &capacity, (size_t)DIAGNOSTIC_JSON_PRODUCER_INITIAL_CAPACITY);
    if (source == NULL) {
        return diagnostic_json_producer_fail("could not allocate source buffer");
    }

    for (;;) {
        size_t available = 0U;
        size_t bytes_read = 0U;

        if (capacity <= length + 1U) {
            source = diagnostic_json_producer_grow_buffer(source, &capacity, length + 2U);
            if (source == NULL) {
                return diagnostic_json_producer_fail("could not grow source buffer");
            }
        }

        available = capacity - length - 1U;
        if (available > (size_t)DIAGNOSTIC_JSON_PRODUCER_READ_CHUNK_BYTES) {
            available = (size_t)DIAGNOSTIC_JSON_PRODUCER_READ_CHUNK_BYTES;
        }

        bytes_read = fread(source + length, 1U, available, stream);
        length += bytes_read;

        if (bytes_read < available) {
            if (ferror(stream)) {
                free(source);
                return diagnostic_json_producer_fail("could not read source fixture");
            }
            break;
        }
    }

    source[length] = '\0';
    *out_source = source;
    return 0;
}


/// Parses an optional unsigned environment variable into a uint32_t value.
///
/// @param name Environment variable name.
/// @param out_value Receives parsed value when present and valid.
/// @param out_present Receives whether the variable was present.
/// @return Zero on success, otherwise nonzero.
static int diagnostic_json_producer_parse_u32_env(const char *name, uint32_t *out_value, int *out_present) {
    const char *text = NULL;
    char *end = NULL;
    unsigned long long value = 0ULL;

    if (name == NULL || out_value == NULL || out_present == NULL) {
        return diagnostic_json_producer_fail("invalid environment parse argument");
    }

    *out_present = 0;
    text = getenv(name);
    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    errno = 0;
    value = strtoull(text, &end, 0);
    if (end == text || end == NULL || *end != '\0' || errno == ERANGE || value > 0xFFFFFFFFULL) {
        return diagnostic_json_producer_fail("invalid unsigned environment value");
    }

    *out_value = (uint32_t)value;
    *out_present = 1;
    return 0;
}

/// Returns whether the producer should select automatic layout mode.
///
/// @return Nonzero when MASM32_DIAGNOSTIC_LAYOUT_MODE=automatic.
static int diagnostic_json_producer_use_automatic_layout(void) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_LAYOUT_MODE");
    return mode != NULL && strcmp(mode, "automatic") == 0;
}

/// Returns the requested memory validation mode from the diagnostic environment.
///
/// @param out_mode Receives the selected memory validation mode.
/// @return Nonzero when MASM32_DIAGNOSTIC_MEMORY_VALIDATION selects an explicit mode.
static int diagnostic_json_producer_get_memory_validation_mode(Masm32SimWasmMemoryValidationMode *out_mode) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_MEMORY_VALIDATION");

    if (out_mode == NULL) {
        return 0;
    }

    if (mode == NULL) {
        return 0;
    }

    *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    if (strcmp(mode, "off") == 0 || strcmp(mode, "region-only") == 0) {
        *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
        return 1;
    }
    if (strcmp(mode, "allocated-object-warnings") == 0) {
        *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_WARNINGS;
        return 1;
    }
    if (strcmp(mode, "allocated-object-strict") == 0) {
        *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_ALLOCATED_OBJECT_STRICT;
        return 1;
    }
    if (strcmp(mode, "uninitialized-read-warnings") == 0) {
        *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
        return 1;
    }
    if (strcmp(mode, "uninitialized-read-strict") == 0) {
        *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_STRICT;
        return 1;
    }

    return 0;
}

/// Parses one Phase 53B section-validation policy environment value.
///
/// @param name Environment variable name to read.
/// @param out_policy Receives the selected policy.
/// @return Nonzero when the environment variable selects a non-default policy.
static int diagnostic_json_producer_get_section_validation_policy(
    const char *name,
    Masm32SimWasmSectionValidationPolicy *out_policy
) {
    const char *mode = name != NULL ? getenv(name) : NULL;

    if (out_policy == NULL) {
        return 0;
    }
    *out_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "off") == 0) {
        return 0;
    }
    if (strcmp(mode, "warn") == 0 || strcmp(mode, "warnings") == 0) {
        *out_policy = MASM32_SIM_WASM_SECTION_VALIDATION_WARN;
        return 1;
    }
    if (strcmp(mode, "strict") == 0 || strcmp(mode, "error") == 0) {
        *out_policy = MASM32_SIM_WASM_SECTION_VALIDATION_STRICT;
        return 1;
    }

    return 0;
}


/// Returns the requested shift validation mode from the diagnostic environment.
///
/// @param out_mode Receives the selected shift validation mode.
/// @return Nonzero when MASM32_DIAGNOSTIC_SHIFT_VALIDATION selects strict mode.
static int diagnostic_json_producer_get_shift_validation_mode(Masm32SimWasmShiftValidationMode *out_mode) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_SHIFT_VALIDATION");

    if (out_mode == NULL) {
        return 0;
    }

    *out_mode = MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "strict") == 0) {
        *out_mode = MASM32_SIM_WASM_SHIFT_VALIDATION_STRICT;
        return 1;
    }
    if (strcmp(mode, "warnings") == 0) {
        *out_mode = MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS;
        return 1;
    }

    return 0;
}

/// Returns the requested undefined flag-use policy from the diagnostic environment.
///
/// @param out_policy Receives the selected Phase 50B consumer policy.
/// @return Nonzero when MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE selects a policy.
static int diagnostic_json_producer_get_undefined_flag_use_policy(Masm32SimWasmUndefinedFlagUsePolicy *out_policy) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE");

    if (out_policy == NULL) {
        return 0;
    }

    *out_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "off") == 0) {
        *out_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF;
        return 1;
    }
    if (strcmp(mode, "warn") == 0) {
        *out_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_WARN;
        return 1;
    }
    if (strcmp(mode, "error") == 0) {
        *out_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_ERROR;
        return 1;
    }

    return 0;
}


/// Returns the requested Phase 57J const-uninitialized-storage policy.
///
/// @param out_policy Receives the selected policy value.
/// @return Nonzero when MASM32_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE selects a policy.
static int diagnostic_json_producer_get_const_uninitialized_storage_policy(VmDiagnosticPolicyValue *out_policy) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_CONST_UNINITIALIZED_STORAGE");

    if (out_policy == NULL) {
        return 0;
    }

    *out_policy = VM_DIAGNOSTIC_POLICY_VALUE_WARN;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "off") == 0) {
        *out_policy = VM_DIAGNOSTIC_POLICY_VALUE_OFF;
        return 1;
    }
    if (strcmp(mode, "warn") == 0) {
        *out_policy = VM_DIAGNOSTIC_POLICY_VALUE_WARN;
        return 1;
    }
    if (strcmp(mode, "error") == 0) {
        *out_policy = VM_DIAGNOSTIC_POLICY_VALUE_ERROR;
        return 1;
    }

    return 0;
}

/// Returns the requested startup-state notice setting from the diagnostic environment.
///
/// @param out_setting Receives the selected Phase 57E startup-state notice setting.
/// @return Nonzero when MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE selects a setting.
static int diagnostic_json_producer_get_startup_state_notice_setting(Masm32SimWasmStartupStateNoticeSetting *out_setting) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_STARTUP_STATE_NOTICE");

    if (out_setting == NULL) {
        return 0;
    }

    *out_setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "off") == 0) {
        *out_setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_OFF;
        return 1;
    }
    if (strcmp(mode, "warn") == 0 || strcmp(mode, "on") == 0) {
        *out_setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON;
        return 1;
    }

    return 0;
}

/// Returns the requested Phase 57F register/flag startup mode from the diagnostic environment.
///
/// @param out_mode Receives the selected startup register/flag mode.
/// @return Nonzero when MASM32_DIAGNOSTIC_STARTUP_REGISTER_FLAG_MODE selects a setting.
static int diagnostic_json_producer_get_startup_register_flag_mode(Masm32SimWasmStartupRegisterFlagMode *out_mode) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_STARTUP_REGISTER_FLAG_MODE");

    if (out_mode == NULL) {
        return 0;
    }

    *out_mode = MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "zero") == 0) {
        *out_mode = MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO;
        return 1;
    }
    if (strcmp(mode, "seeded-random") == 0) {
        *out_mode = MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_SEEDED_RANDOM;
        return 1;
    }

    return 0;
}

/// Returns the requested Phase 57G uninitialized-storage visible-byte startup mode.
///
/// @param out_mode Receives the selected uninitialized-storage visible-byte mode.
/// @return Nonzero when MASM32_DIAGNOSTIC_UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE selects a setting.
static int diagnostic_json_producer_get_uninitialized_storage_visible_byte_mode(Masm32SimWasmUninitializedStorageVisibleByteMode *out_mode) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_UNINITIALIZED_STORAGE_VISIBLE_BYTE_MODE");

    if (out_mode == NULL) {
        return 0;
    }

    *out_mode = MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "zero") == 0) {
        *out_mode = MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO;
        return 1;
    }
    if (strcmp(mode, "seeded-random") == 0) {
        *out_mode = MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_SEEDED_RANDOM;
        return 1;
    }

    return 0;
}


/// Returns the requested Phase 71A root RET mode.
///
/// @param out_mode Receives the selected root RET mode.
/// @return Nonzero when MASM32_DIAGNOSTIC_ROOT_RET_MODE selects a setting.
static int diagnostic_json_producer_get_root_ret_mode(Masm32SimWasmRootRetMode *out_mode) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_ROOT_RET_MODE");

    if (out_mode == NULL) {
        return 0;
    }

    *out_mode = MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE;
    if (mode == NULL) {
        return 0;
    }
    if (strcmp(mode, "masm32-compatible") == 0) {
        *out_mode = MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE;
        return 1;
    }
    if (strcmp(mode, "strict-call-frame") == 0) {
        *out_mode = MASM32_SIM_WASM_ROOT_RET_MODE_STRICT_CALL_FRAME;
        return 1;
    }

    return 0;
}

/// Returns the requested Phase 71D procedure-fallthrough policy.
///
/// @param out_policy Receives the selected procedure-fallthrough policy.
/// @return Nonzero when MASM32_DIAGNOSTIC_PROCEDURE_FALLTHROUGH_POLICY selects a setting.
static int diagnostic_json_producer_get_procedure_fallthrough_policy(Masm32SimWasmProcedureFallthroughPolicy *out_policy) {
    const char *policy = getenv("MASM32_DIAGNOSTIC_PROCEDURE_FALLTHROUGH_POLICY");

    if (out_policy == NULL) {
        return 0;
    }

    *out_policy = MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN;
    if (policy == NULL) {
        return 0;
    }
    if (strcmp(policy, "off") == 0) {
        *out_policy = MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_OFF;
        return 1;
    }
    if (strcmp(policy, "warn") == 0) {
        *out_policy = MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN;
        return 1;
    }
    if (strcmp(policy, "error") == 0) {
        *out_policy = MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_ERROR;
        return 1;
    }

    return 0;
}

/// Applies optional automatic layout limit environment overrides.
///
/// @param policy Policy to mutate.
/// @return Zero on success, otherwise nonzero.
static int diagnostic_json_producer_apply_layout_env(VmLayoutPolicy *policy) {
    uint32_t value = 0U;
    int present = 0;

    if (policy == NULL) {
        return diagnostic_json_producer_fail("invalid layout policy argument");
    }

    if (diagnostic_json_producer_parse_u32_env("MASM32_DIAGNOSTIC_AUTO_DATA_LIMIT", &value, &present) != 0) {
        return 1;
    }
    if (present) {
        policy->regions[VM_LAYOUT_REGION_DATA].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = value;
    }

    if (diagnostic_json_producer_parse_u32_env("MASM32_DIAGNOSTIC_AUTO_STACK_LIMIT", &value, &present) != 0) {
        return 1;
    }
    if (present) {
        policy->regions[VM_LAYOUT_REGION_STACK].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = value;
    }

    if (diagnostic_json_producer_parse_u32_env("MASM32_DIAGNOSTIC_AUTO_HEAP_REQUEST", &value, &present) != 0) {
        return 1;
    }
    if (present) {
        policy->heap_size_request = value;
    }

    if (diagnostic_json_producer_parse_u32_env("MASM32_DIAGNOSTIC_AUTO_HEAP_LIMIT", &value, &present) != 0) {
        return 1;
    }
    if (present) {
        policy->regions[VM_LAYOUT_REGION_HEAP].maximum_size_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = value;
    }

    if (diagnostic_json_producer_parse_u32_env("MASM32_DIAGNOSTIC_AUTO_TOTAL_LIMIT", &value, &present) != 0) {
        return 1;
    }
    if (present) {
        policy->maximum_total_reservation_by_tier[VM_LAYOUT_SAFETY_TIER_BEGINNER] = value;
    }

    return 0;
}

/// Runs one source fixture through the source-run JSON path and prints raw JSON.
///
/// @param source Null-terminated source fixture text.
/// @return Zero on success, otherwise nonzero.
static int diagnostic_json_producer_emit_json(const char *source) {
    const char *json = NULL;
    VmLayoutPolicy policy;
    Masm32SimWasmMemoryValidationMode validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_UNINITIALIZED_READ_WARNINGS;
    Masm32SimWasmSectionValidationPolicy capacity_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmSectionValidationPolicy image_policy = MASM32_SIM_WASM_SECTION_VALIDATION_OFF;
    Masm32SimWasmShiftValidationMode shift_mode = MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS;
    Masm32SimWasmUndefinedFlagUsePolicy flag_use_policy = MASM32_SIM_WASM_UNDEFINED_FLAG_USE_OFF;
    VmDiagnosticPolicyValue const_uninitialized_storage_policy = VM_DIAGNOSTIC_POLICY_VALUE_WARN;
    Masm32SimWasmStartupStateNoticeSetting startup_state_notice_setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON;
    Masm32SimWasmStartupRegisterFlagMode startup_register_flag_mode = MASM32_SIM_WASM_STARTUP_REGISTER_FLAG_ZERO;
    Masm32SimWasmUninitializedStorageVisibleByteMode uninitialized_storage_visible_byte_mode = MASM32_SIM_WASM_UNINITIALIZED_STORAGE_VISIBLE_BYTE_ZERO;
    uint32_t startup_state_seed = 0U;
    int has_startup_register_flag_mode = 0;
    int has_uninitialized_storage_visible_byte_mode = 0;
    int has_startup_state_seed = 0;
    int has_memory_validation = 0;
    int has_section_capacity_validation = 0;
    int has_section_image_validation = 0;
    int has_const_uninitialized_storage_policy = 0;
    uint32_t instruction_limit = 0U;
    int has_instruction_limit = 0;
    Masm32SimWasmRootRetMode root_ret_mode = MASM32_SIM_WASM_ROOT_RET_MODE_MASM32_COMPATIBLE;
    int has_root_ret_mode = 0;
    Masm32SimWasmProcedureFallthroughPolicy procedure_fallthrough_policy = MASM32_SIM_WASM_PROCEDURE_FALLTHROUGH_WARN;
    int has_procedure_fallthrough_policy = 0;

    if (source == NULL) {
        return diagnostic_json_producer_fail("source fixture was not loaded");
    }

    has_memory_validation = diagnostic_json_producer_get_memory_validation_mode(&validation_mode);
    has_section_capacity_validation = diagnostic_json_producer_get_section_validation_policy("MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION", &capacity_policy);
    has_section_image_validation = diagnostic_json_producer_get_section_validation_policy("MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION", &image_policy);
    has_const_uninitialized_storage_policy = diagnostic_json_producer_get_const_uninitialized_storage_policy(&const_uninitialized_storage_policy);
    has_startup_register_flag_mode = diagnostic_json_producer_get_startup_register_flag_mode(&startup_register_flag_mode);
    has_uninitialized_storage_visible_byte_mode = diagnostic_json_producer_get_uninitialized_storage_visible_byte_mode(&uninitialized_storage_visible_byte_mode);
    has_root_ret_mode = diagnostic_json_producer_get_root_ret_mode(&root_ret_mode);
    has_procedure_fallthrough_policy = diagnostic_json_producer_get_procedure_fallthrough_policy(&procedure_fallthrough_policy);
    if (diagnostic_json_producer_parse_u32_env("MASM32_DIAGNOSTIC_STARTUP_STATE_SEED", &startup_state_seed, &has_startup_state_seed) != 0) {
        return 1;
    }
    if (diagnostic_json_producer_parse_u32_env("MASM32_DIAGNOSTIC_INSTRUCTION_LIMIT", &instruction_limit, &has_instruction_limit) != 0) {
        return 1;
    }

    if (has_root_ret_mode || has_procedure_fallthrough_policy) {
        json = masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_root_ret_and_procedure_fallthrough_settings(
            source,
            MASM32_SIM_WASM_MEMORY_RANGE_REGION_ONLY,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_TEACHING_DIAGNOSTIC_WARN,
            MASM32_SIM_WASM_COMPATIBILITY_NOTICES_ON,
            startup_register_flag_mode,
            uninitialized_storage_visible_byte_mode,
            startup_state_seed,
            has_instruction_limit ? instruction_limit : 1000000U,
            root_ret_mode,
            procedure_fallthrough_policy
        );
    } else if (has_instruction_limit) {
        json = masm32_sim_wasm_run_source_json_with_instruction_limit(source, instruction_limit);
    } else if (has_startup_register_flag_mode || has_uninitialized_storage_visible_byte_mode || has_startup_state_seed) {
        if (!diagnostic_json_producer_get_startup_state_notice_setting(&startup_state_notice_setting)) {
            startup_state_notice_setting = MASM32_SIM_WASM_STARTUP_STATE_NOTICE_ON;
        }
        json = masm32_sim_wasm_run_source_json_with_startup_modes(
            source,
            startup_register_flag_mode,
            uninitialized_storage_visible_byte_mode,
            startup_state_seed,
            startup_state_notice_setting
        );
    } else if (has_section_capacity_validation || has_section_image_validation) {
        json = masm32_sim_wasm_run_source_json_with_section_validation_modes(source, validation_mode, capacity_policy, image_policy);
    } else if (has_memory_validation) {
        json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, validation_mode);
    } else if (has_const_uninitialized_storage_policy) {
        json = masm32_sim_wasm_run_source_json_with_const_uninitialized_storage_policy(source, const_uninitialized_storage_policy);
    } else if (diagnostic_json_producer_use_automatic_layout()) {
        policy = vm_layout_default_policy();
        if (diagnostic_json_producer_apply_layout_env(&policy) != 0) {
            return 1;
        }
        json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(source, &policy);
    } else if (diagnostic_json_producer_get_shift_validation_mode(&shift_mode)) {
        json = masm32_sim_wasm_run_source_json_with_shift_validation_mode(source, shift_mode);
    } else if (diagnostic_json_producer_get_undefined_flag_use_policy(&flag_use_policy)) {
        json = masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy(source, flag_use_policy);
    } else if (diagnostic_json_producer_get_startup_state_notice_setting(&startup_state_notice_setting)) {
        json = masm32_sim_wasm_run_source_json_with_startup_state_notice_setting(source, startup_state_notice_setting);
    } else {
        json = masm32_sim_wasm_run_source_json(source);
    }

    if (json == NULL) {
        return diagnostic_json_producer_fail("source-run API returned NULL JSON");
    }

    fputs(json, stdout);
    fputc('\n', stdout);
    return ferror(stdout) ? diagnostic_json_producer_fail("could not write JSON to stdout") : 0;
}

/// Entrypoint for the native diagnostic JSON producer.
///
/// @param argc Command-line argument count.
/// @param argv Command-line argument vector.
/// @return Zero on success, otherwise nonzero.
int main(int argc, char **argv) {
    FILE *input = NULL;
    int should_close = 0;
    char *source = NULL;
    int status = 0;

    status = diagnostic_json_producer_open_input(argc, argv, &input, &should_close);
    if (status != 0) {
        return status;
    }

    status = diagnostic_json_producer_read_source(input, &source);
    if (should_close && fclose(input) != 0 && status == 0) {
        status = diagnostic_json_producer_fail("could not close source fixture file");
    }
    if (status == 0) {
        status = diagnostic_json_producer_emit_json(source);
    }

    free(source);
    return status;
}
