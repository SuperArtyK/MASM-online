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
    unsigned long value = 0UL;

    if (name == NULL || out_value == NULL || out_present == NULL) {
        return diagnostic_json_producer_fail("invalid environment parse argument");
    }

    *out_present = 0;
    text = getenv(name);
    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    value = strtoul(text, &end, 0);
    if (end == text || end == NULL || *end != '\0' || value > 0xFFFFFFFFUL) {
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
/// @return Nonzero when MASM32_DIAGNOSTIC_MEMORY_VALIDATION selects a non-default mode.
static int diagnostic_json_producer_get_memory_validation_mode(Masm32SimWasmMemoryValidationMode *out_mode) {
    const char *mode = getenv("MASM32_DIAGNOSTIC_MEMORY_VALIDATION");

    if (out_mode == NULL) {
        return 0;
    }

    *out_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    if (mode == NULL) {
        return 0;
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
    Masm32SimWasmMemoryValidationMode validation_mode = MASM32_SIM_WASM_MEMORY_VALIDATION_REGION_ONLY;
    Masm32SimWasmShiftValidationMode shift_mode = MASM32_SIM_WASM_SHIFT_VALIDATION_WARNINGS;

    if (source == NULL) {
        return diagnostic_json_producer_fail("source fixture was not loaded");
    }

    if (diagnostic_json_producer_get_memory_validation_mode(&validation_mode)) {
        json = masm32_sim_wasm_run_source_json_with_memory_validation_mode(source, validation_mode);
    } else if (diagnostic_json_producer_use_automatic_layout()) {
        policy = vm_layout_default_policy();
        if (diagnostic_json_producer_apply_layout_env(&policy) != 0) {
            return 1;
        }
        json = masm32_sim_wasm_run_source_json_with_automatic_layout_policy(source, &policy);
    } else if (diagnostic_json_producer_get_shift_validation_mode(&shift_mode)) {
        json = masm32_sim_wasm_run_source_json_with_shift_validation_mode(source, shift_mode);
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
