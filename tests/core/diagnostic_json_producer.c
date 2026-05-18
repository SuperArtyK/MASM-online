/*
 * @file diagnostic_json_producer.c
 * @brief Native source-run JSON producer for diagnostic rendering tests.
 *
 * This utility calls the same source-run JSON API used by the browser Wasm
 * boundary, but it builds with the native C test toolchain and does not require
 * Emscripten. It prints only the raw JSON payload on stdout so Node tests can
 * feed the exact result through the browser Simulator Messages formatter.
 */

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

/// Runs one source fixture through the source-run JSON path and prints raw JSON.
///
/// @param source Null-terminated source fixture text.
/// @return Zero on success, otherwise nonzero.
static int diagnostic_json_producer_emit_json(const char *source) {
    const char *json = NULL;

    if (source == NULL) {
        return diagnostic_json_producer_fail("source fixture was not loaded");
    }

    json = masm32_sim_wasm_run_source_json(source);
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
