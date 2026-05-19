/*
 * @file test_object_map.c
 * @brief Phase 36 declared-object allocation map tests.
 *
 * These tests verify that declared `.data`, `.DATA?`, and `.CONST` symbols are
 * mirrored into metadata-only object ranges without changing runtime memory
 * validation or source-run JSON output.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/vm_layout.h"
#include "../../src/core/vm_memory.h"
#include "../../src/core/vm_ir.h"
#include "../../src/parser/lexer.h"
#include "../../src/parser/object_map.h"
#include "../../src/parser/parser.h"
#include "../../src/parser/symbols.h"
#include "../../src/wasm/wasm_api.h"

/// Maximum tokens used by object-map parser fixtures.
#define TEST_OBJECT_TOKENS 512U
/// Maximum lexer diagnostics used by object-map parser fixtures.
#define TEST_OBJECT_LEXER_DIAGNOSTICS 64U
/// Maximum parser diagnostics used by object-map parser fixtures.
#define TEST_OBJECT_DIAGNOSTICS 64U
/// Maximum emitted instructions used by object-map parser fixtures.
#define TEST_OBJECT_INSTRUCTIONS 128U
/// Maximum symbols used by object-map parser fixtures.
#define TEST_OBJECT_SYMBOLS 64U
/// Maximum object-map entries used by object-map tests.
#define TEST_OBJECTS 64U
/// Source text storage bytes used by object-map parser fixtures.
#define TEST_OBJECT_SOURCE_TEXT 4096U
/// Writable data image bytes used by object-map parser fixtures.
#define TEST_OBJECT_DATA_BYTES VM_MEMORY_DEFAULT_DATA_SIZE
/// Read-only const image bytes used by object-map parser fixtures.
#define TEST_OBJECT_CONST_BYTES VM_MEMORY_DEFAULT_CONST_SIZE
/// Capacity for copied source-run JSON payloads.
#define TEST_JSON_COPY_CAPACITY 32768U

/// Groups fixed parser buffers used by object-map tests.
typedef struct ObjectMapTestBuffers {
    /// Lexer tokens produced during parsing.
    VmLexerToken tokens[TEST_OBJECT_TOKENS];
    /// Lexer diagnostics produced during parsing.
    VmLexerDiagnostic lexer_diagnostics[TEST_OBJECT_LEXER_DIAGNOSTICS];
    /// Parser diagnostics produced during parsing.
    VmParserDiagnostic diagnostics[TEST_OBJECT_DIAGNOSTICS];
    /// IR instructions emitted during parsing.
    VmIrInstruction instructions[TEST_OBJECT_INSTRUCTIONS];
    /// Data symbols emitted during parsing.
    VmSymbol symbols[TEST_OBJECT_SYMBOLS];
    /// Declared-object map entries emitted during tests.
    VmObjectMapEntry objects[TEST_OBJECTS];
    /// Parser-owned instruction source text copies.
    char source_text[TEST_OBJECT_SOURCE_TEXT];
    /// Writable data image.
    uint8_t data_image[TEST_OBJECT_DATA_BYTES];
    /// Read-only const image.
    uint8_t const_image[TEST_OBJECT_CONST_BYTES];
} ObjectMapTestBuffers;

/// Reports an expectation failure and returns one failure count when false.
static int expect_bool(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

/// Reports an integer expectation failure and returns one failure count on mismatch.
static int expect_u32(uint32_t actual, uint32_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%u expected=%u)\n", message, actual, expected);
        return 1;
    }
    return 0;
}

/// Reports a size expectation failure and returns one failure count on mismatch.
static int expect_size(size_t actual, size_t expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%zu expected=%zu)\n", message, actual, expected);
        return 1;
    }
    return 0;
}

/// Reports a string expectation failure and returns one failure count on mismatch.
static int expect_string(const char *actual, const char *expected, const char *message) {
    if (actual == NULL || expected == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, actual != NULL ? actual : "<null>", expected != NULL ? expected : "<null>");
        return 1;
    }
    return 0;
}

/// Reports a string-containment expectation failure and returns one failure count when absent.
static int expect_string_contains(const char *text, const char *expected, const char *message) {
    if (text == NULL || strstr(text, expected) == NULL) {
        fprintf(stderr, "FAIL: %s (missing=%s)\n", message, expected);
        return 1;
    }
    return 0;
}

/// Reports a negative string-containment expectation failure and returns one failure count when present.
static int expect_string_not_contains(const char *text, const char *unexpected, const char *message) {
    if (text != NULL && strstr(text, unexpected) != NULL) {
        fprintf(stderr, "FAIL: %s (unexpected=%s)\n", message, unexpected);
        return 1;
    }
    return 0;
}

/// Reports an object-map status mismatch and returns one failure count on mismatch.
static int expect_object_status(VmObjectMapStatus actual, VmObjectMapStatus expected, const char *message) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (actual=%s expected=%s)\n", message, vm_object_map_status_name(actual), vm_object_map_status_name(expected));
        return 1;
    }
    return 0;
}

/// Reports an object-map range-class mismatch and returns one failure count on mismatch.
static int expect_range_class(VmObjectMapRangeClass actual, VmObjectMapRangeClass expected, const char *message) {
    if (actual != expected) {
        fprintf(
            stderr,
            "FAIL: %s (actual=%s expected=%s)\n",
            message,
            vm_object_map_range_class_name(actual),
            vm_object_map_range_class_name(expected)
        );
        return 1;
    }
    return 0;
}

/// Parses one fixture and emits symbol metadata for object-map construction.
static VmParserStatus parse_for_object_map_test(const char *source, ObjectMapTestBuffers *buffers, VmParserResult *out_result) {
    VmParserConfig config;

    memset(buffers, 0, sizeof(*buffers));
    memset(&config, 0, sizeof(config));
    memset(out_result, 0, sizeof(*out_result));

    config.source = source;
    config.source_file = "object_map_test.asm";
    config.tokens = buffers->tokens;
    config.token_capacity = TEST_OBJECT_TOKENS;
    config.lexer_diagnostics = buffers->lexer_diagnostics;
    config.lexer_diagnostic_capacity = TEST_OBJECT_LEXER_DIAGNOSTICS;
    config.instructions = buffers->instructions;
    config.instruction_capacity = TEST_OBJECT_INSTRUCTIONS;
    config.source_text_storage = buffers->source_text;
    config.source_text_capacity = TEST_OBJECT_SOURCE_TEXT;
    config.symbols = buffers->symbols;
    config.symbol_capacity = TEST_OBJECT_SYMBOLS;
    config.data_image = buffers->data_image;
    config.data_image_capacity = TEST_OBJECT_DATA_BYTES;
    config.const_image = buffers->const_image;
    config.const_image_capacity = TEST_OBJECT_CONST_BYTES;
    config.diagnostics = buffers->diagnostics;
    config.diagnostic_capacity = TEST_OBJECT_DIAGNOSTICS;

    return vm_parser_parse_program(&config, out_result);
}

/// Parses one fixture and builds an object map from parser-emitted final addresses.
static int build_default_object_map(const char *source, ObjectMapTestBuffers *buffers, VmParserResult *out_result, size_t *out_count) {
    VmParserStatus parser_status = parse_for_object_map_test(source, buffers, out_result);
    VmObjectMapStatus object_status = VM_OBJECT_MAP_STATUS_OK;

    if (parser_status != VM_PARSER_STATUS_OK) {
        fprintf(stderr, "FAIL: parser should succeed for object-map fixture (status=%s diagnostics=%zu)\n", vm_parser_status_name(parser_status), out_result->diagnostic_count);
        return 1;
    }

    object_status = vm_object_map_build_from_symbols(buffers->symbols, out_result->symbol_count, buffers->objects, TEST_OBJECTS, out_count);
    return expect_object_status(object_status, VM_OBJECT_MAP_STATUS_OK, "object map should build from parsed symbols");
}

/// Finds an object-map entry by symbol name.
static const VmObjectMapEntry *find_object_by_name(const VmObjectMapEntry *entries, size_t entry_count, const char *name) {
    size_t index = 0U;
    for (index = 0U; index < entry_count; index += 1U) {
        if (strcmp(entries[index].symbol_name, name) == 0) {
            return &entries[index];
        }
    }
    return NULL;
}

/// Verifies scalar, array, nested DUP, .DATA?, and .CONST object metadata.
static int test_object_map_records_declared_objects(void) {
    static const char *source =
        ".DATA?\n"
        "buf BYTE 4 DUP(?)\n"
        ".data\n"
        "scalar DWORD 123\n"
        "arr WORD 3 DUP(7)\n"
        "matrix DWORD 2 DUP(3 DUP(0))\n"
        ".CONST\n"
        "limit SDWORD -1\n"
        ".code\n"
        "main PROC\n"
        "END main\n";
    static ObjectMapTestBuffers buffers;
    VmParserResult result;
    size_t object_count = 0U;
    const VmObjectMapEntry *buf = NULL;
    const VmObjectMapEntry *scalar = NULL;
    const VmObjectMapEntry *arr = NULL;
    const VmObjectMapEntry *matrix = NULL;
    const VmObjectMapEntry *limit = NULL;
    int failures = build_default_object_map(source, &buffers, &result, &object_count);

    failures += expect_size(object_count, 5U, "object map should emit one object per declared symbol");

    buf = find_object_by_name(buffers.objects, object_count, "buf");
    scalar = find_object_by_name(buffers.objects, object_count, "scalar");
    arr = find_object_by_name(buffers.objects, object_count, "arr");
    matrix = find_object_by_name(buffers.objects, object_count, "matrix");
    limit = find_object_by_name(buffers.objects, object_count, "limit");

    failures += expect_bool(buf != NULL, "buf object should exist");
    failures += expect_bool(scalar != NULL, "scalar object should exist");
    failures += expect_bool(arr != NULL, "arr object should exist");
    failures += expect_bool(matrix != NULL, "matrix object should exist");
    failures += expect_bool(limit != NULL, "limit object should exist");

    if (buf != NULL) {
        failures += expect_u32(buf->base_address, VM_MEMORY_DEFAULT_DATA_BASE, ".DATA? object should start at data base");
        failures += expect_u32(buf->size_bytes, 4U, "buf byte size should reflect DUP expansion");
        failures += expect_u32(buf->element_size_bytes, 1U, "buf element size should be BYTE");
        failures += expect_u32(buf->element_count, 4U, "buf element count should reflect DUP count");
        failures += expect_bool(buf->section == VM_SYMBOL_SECTION_DATA_UNINITIALIZED, "buf section should be .DATA?");
        failures += expect_bool((buf->permissions & VM_MEMORY_PERMISSION_WRITE) != 0U, "buf should be writable");
        failures += expect_bool(!buf->is_signed, "buf should be unsigned");
        failures += expect_bool(buf->source_location.line > 0U && buf->source_location.column > 0U, "buf source location should be present");
        failures += expect_size(buf->source_span_length, strlen("buf"), "buf source span should match symbol length");
        failures += expect_bool(buf->initialization_origin_state == VM_OBJECT_INITIALIZATION_ORIGIN_NOT_TRACKED, "buf initialization origin should be not tracked");
    }
    if (scalar != NULL) {
        failures += expect_u32(scalar->base_address, VM_MEMORY_DEFAULT_DATA_BASE + 4U, "scalar should follow .DATA? bytes");
        failures += expect_u32(scalar->size_bytes, 4U, "scalar size should be DWORD");
        failures += expect_u32(scalar->element_size_bytes, 4U, "scalar element size should be DWORD");
        failures += expect_u32(scalar->element_count, 1U, "scalar element count should be one");
        failures += expect_bool(scalar->section == VM_SYMBOL_SECTION_DATA, "scalar section should be .data");
    }
    if (arr != NULL) {
        failures += expect_u32(arr->base_address, VM_MEMORY_DEFAULT_DATA_BASE + 8U, "arr should follow scalar");
        failures += expect_u32(arr->size_bytes, 6U, "arr size should be three WORDs");
        failures += expect_u32(arr->element_size_bytes, 2U, "arr element size should be WORD");
        failures += expect_u32(arr->element_count, 3U, "arr element count should be three");
    }
    if (matrix != NULL) {
        failures += expect_u32(matrix->base_address, VM_MEMORY_DEFAULT_DATA_BASE + 14U, "matrix should follow arr");
        failures += expect_u32(matrix->size_bytes, 24U, "matrix size should reflect nested DUP expansion");
        failures += expect_u32(matrix->element_size_bytes, 4U, "matrix element size should be DWORD");
        failures += expect_u32(matrix->element_count, 6U, "matrix element count should reflect nested DUP expansion");
    }
    if (limit != NULL) {
        failures += expect_u32(limit->base_address, VM_MEMORY_DEFAULT_CONST_BASE, ".CONST object should use const base");
        failures += expect_u32(limit->size_bytes, 4U, "limit size should be SDWORD");
        failures += expect_bool(limit->section == VM_SYMBOL_SECTION_CONST, "limit section should be .CONST");
        failures += expect_bool(limit->is_signed, "limit should retain signedness metadata");
        failures += expect_bool((limit->permissions & VM_MEMORY_PERMISSION_WRITE) == 0U, "limit should not be writable");
        failures += expect_bool((limit->permissions & VM_MEMORY_PERMISSION_READ) != 0U, "limit should be readable");
    }

    failures += expect_string(vm_object_initialization_origin_state_name(VM_OBJECT_INITIALIZATION_ORIGIN_NOT_TRACKED), "not-tracked", "initialization-origin status name should be stable");
    return failures;
}

/// Verifies adjacent declarations remain separate objects and lookup uses full ranges.
static int test_adjacent_objects_are_not_merged(void) {
    static const char *source =
        ".data\n"
        "a BYTE 1\n"
        "b BYTE 2\n"
        ".code\n"
        "main PROC\n"
        "END main\n";
    static ObjectMapTestBuffers buffers;
    VmParserResult result;
    const VmObjectMapEntry *found = NULL;
    const VmObjectMapEntry *a = NULL;
    const VmObjectMapEntry *b = NULL;
    size_t object_count = 0U;
    int failures = build_default_object_map(source, &buffers, &result, &object_count);

    failures += expect_size(object_count, 2U, "adjacent symbols should create separate object entries");
    a = find_object_by_name(buffers.objects, object_count, "a");
    b = find_object_by_name(buffers.objects, object_count, "b");
    failures += expect_bool(a != NULL && b != NULL, "both adjacent objects should exist");
    if (a != NULL && b != NULL) {
        failures += expect_u32(a->base_address + a->size_bytes, b->base_address, "adjacent byte objects should be contiguous");
        failures += expect_object_status(vm_object_map_find_by_range(buffers.objects, object_count, a->base_address, 1U, &found), VM_OBJECT_MAP_STATUS_OK, "single-byte range should find first object");
        failures += expect_bool(found == a, "single-byte range should return first object");
        failures += expect_object_status(vm_object_map_find_by_range(buffers.objects, object_count, a->base_address, 2U, &found), VM_OBJECT_MAP_STATUS_NOT_FOUND, "range spanning adjacent objects should not be reported as one object");
        failures += expect_bool(vm_object_map_find_by_address(buffers.objects, object_count, b->base_address) == b, "address lookup should return second object");
    }
    return failures;
}

/// Verifies range lookup handles unaligned contained ranges, escaping ranges, and overflow.
static int test_full_range_lookup_and_overflow(void) {
    static const char *source =
        ".data\n"
        "arr BYTE 8 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "END main\n";
    static ObjectMapTestBuffers buffers;
    VmParserResult result;
    const VmObjectMapEntry *arr = NULL;
    const VmObjectMapEntry *found = NULL;
    uint32_t end = 0U;
    size_t object_count = 0U;
    int failures = build_default_object_map(source, &buffers, &result, &object_count);

    arr = find_object_by_name(buffers.objects, object_count, "arr");
    failures += expect_bool(arr != NULL, "arr object should exist");
    if (arr != NULL) {
        failures += expect_object_status(vm_object_map_find_by_range(buffers.objects, object_count, arr->base_address + 1U, 4U, &found), VM_OBJECT_MAP_STATUS_OK, "unaligned range wholly inside one object should still find that object");
        failures += expect_bool(found == arr, "unaligned contained range should return arr");
        failures += expect_object_status(vm_object_map_find_by_range(buffers.objects, object_count, arr->base_address + 6U, 4U, &found), VM_OBJECT_MAP_STATUS_NOT_FOUND, "partial range escaping object should not be found");
        failures += expect_bool(vm_object_map_inclusive_end(arr->base_address, arr->size_bytes, &end), "valid object range should produce inclusive end");
        failures += expect_u32(end, arr->base_address + 7U, "inclusive end should be base plus size minus one");
    }
    failures += expect_object_status(vm_object_map_find_by_range(buffers.objects, object_count, UINT32_MAX, 2U, &found), VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW, "range end overflow should be reported");
    failures += expect_bool(!vm_object_map_inclusive_end(UINT32_MAX, 2U, &end), "overflowing inclusive end should fail");
    failures += expect_object_status(vm_object_map_find_by_range(buffers.objects, object_count, VM_MEMORY_DEFAULT_DATA_BASE, 0U, &found), VM_OBJECT_MAP_STATUS_INVALID_ARGUMENT, "zero-length lookup should be invalid");
    return failures;
}

/// Verifies full-range classification covers Phase 36 object-map categories without emitting diagnostics.
static int test_full_range_classification_categories(void) {
    static const char *source =
        ".data\n"
        "a DWORD 1\n"
        "b DWORD 2\n"
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "END main\n";
    static ObjectMapTestBuffers buffers;
    VmParserResult result;
    VmObjectMapRangeClassification classification;
    const VmObjectMapEntry *a = NULL;
    const VmObjectMapEntry *b = NULL;
    const VmObjectMapEntry *limit = NULL;
    size_t object_count = 0U;
    int failures = build_default_object_map(source, &buffers, &result, &object_count);

    a = find_object_by_name(buffers.objects, object_count, "a");
    b = find_object_by_name(buffers.objects, object_count, "b");
    limit = find_object_by_name(buffers.objects, object_count, "limit");
    failures += expect_bool(a != NULL && b != NULL && limit != NULL, "classification fixture objects should exist");

    if (a != NULL && b != NULL && limit != NULL) {
        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, NULL, a->base_address, 4U, false, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "within-object classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OBJECT, "range wholly inside object should classify as within-object");
        failures += expect_bool(classification.containing_object == a, "within-object classification should return containing object");

        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, a, b->base_address, 4U, false, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "within-other classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_WITHIN_OTHER_OBJECT, "range wholly inside another object should classify as within-other-object");

        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, NULL, VM_MEMORY_DEFAULT_DATA_BASE + 32U, 4U, false, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "region-gap classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_REGION_GAP, "valid region outside every object should classify as region-gap");
        failures += expect_bool(classification.region == VM_LAYOUT_REGION_DATA, "region-gap classification should preserve containing region");

        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, NULL, b->base_address + 2U, 4U, false, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "starts-in-object classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_STARTS_IN_OBJECT, "range starting inside object and ending in gap should classify as starts-in-object");

        failures += expect_object_status(
            vm_object_map_classify_range(b, 1U, NULL, NULL, b->base_address - 2U, 4U, false, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "ends-in-object classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_ENDS_IN_OBJECT, "range starting in gap and ending inside object should classify as ends-in-object");

        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, NULL, a->base_address + 2U, 4U, false, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "spans-objects classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_SPANS_OBJECTS, "range spanning adjacent objects should classify as spans-objects");

        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, NULL, 0U, 4U, false, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "outside-region classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_OUTSIDE_REGION, "range outside all regions should classify as outside-region");

        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, NULL, limit->base_address, 4U, true, &classification),
            VM_OBJECT_MAP_STATUS_OK,
            "const write-overlap classification should succeed"
        );
        failures += expect_range_class(classification.range_class, VM_OBJECT_MAP_RANGE_CLASS_CONST_READ_ONLY_OVERLAP, "write overlapping .CONST should preserve const permission precedence");

        failures += expect_object_status(
            vm_object_map_classify_range(buffers.objects, object_count, NULL, NULL, UINT32_MAX, 2U, false, &classification),
            VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW,
            "classification should report range overflow"
        );
    }

    failures += expect_string(vm_object_map_range_class_name(VM_OBJECT_MAP_RANGE_CLASS_SPANS_OBJECTS), "spans-objects", "range-class status name should be stable");
    return failures;
}

/// Verifies construction skips zero-size symbols, enforces capacity, and rejects overflowing object ranges.
static int test_builder_edge_cases(void) {
    VmSymbol symbols[2];
    VmObjectMapEntry entries[2];
    size_t entry_count = 0U;
    int failures = 0;

    memset(symbols, 0, sizeof(symbols));
    memset(entries, 0, sizeof(entries));
    (void)vm_symbol_set_name(&symbols[0], "zero", strlen("zero"));
    symbols[0].section = VM_SYMBOL_SECTION_DATA;
    symbols[0].data_type = VM_SYMBOL_DATA_TYPE_BYTE;
    symbols[0].address = VM_MEMORY_DEFAULT_DATA_BASE;
    symbols[0].size_bytes = 0U;
    symbols[0].element_size_bytes = 1U;
    symbols[0].element_count = 0U;
    failures += expect_object_status(vm_object_map_build_from_symbols(symbols, 1U, entries, 2U, &entry_count), VM_OBJECT_MAP_STATUS_OK, "zero-size symbol should be skipped rather than emitted");
    failures += expect_size(entry_count, 0U, "zero-size symbol should not create an object entry");

    (void)vm_symbol_set_name(&symbols[0], "one", strlen("one"));
    symbols[0].size_bytes = 1U;
    symbols[0].element_count = 1U;
    symbols[1] = symbols[0];
    (void)vm_symbol_set_name(&symbols[1], "two", strlen("two"));
    symbols[1].address = VM_MEMORY_DEFAULT_DATA_BASE + 1U;
    failures += expect_object_status(vm_object_map_build_from_symbols(symbols, 2U, entries, 1U, &entry_count), VM_OBJECT_MAP_STATUS_CAPACITY_EXCEEDED, "insufficient object-map capacity should fail");

    symbols[0].address = UINT32_MAX;
    symbols[0].size_bytes = 2U;
    failures += expect_object_status(vm_object_map_build_from_symbols(symbols, 1U, entries, 2U, &entry_count), VM_OBJECT_MAP_STATUS_INTEGER_OVERFLOW, "overflowing object range should fail construction");
    return failures;
}

/// Verifies object maps can be relocated through automatic and randomized selected layouts.
static int test_object_map_uses_selected_layout_bases(void) {
    static const char *source =
        ".data\n"
        "value DWORD 123\n"
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "END main\n";
    static ObjectMapTestBuffers buffers;
    VmParserResult result;
    VmLayoutProgramMetadata metadata;
    VmLayoutPolicy automatic_policy;
    VmLayoutPolicy randomized_base;
    VmLayoutPolicy randomized_policy;
    VmLayoutDiagnostic diagnostic;
    VmObjectMapEntry automatic_objects[TEST_OBJECTS];
    VmObjectMapEntry randomized_objects[TEST_OBJECTS];
    const VmObjectMapEntry *automatic_value = NULL;
    const VmObjectMapEntry *automatic_limit = NULL;
    const VmObjectMapEntry *randomized_value = NULL;
    size_t automatic_count = 0U;
    size_t randomized_count = 0U;
    int failures = 0;

    if (parse_for_object_map_test(source, &buffers, &result) != VM_PARSER_STATUS_OK) {
        fprintf(stderr, "FAIL: parser should succeed for selected-layout fixture\n");
        return 1;
    }

    memset(&metadata, 0, sizeof(metadata));
    metadata.code_size = (uint32_t)result.instruction_count;
    metadata.initialized_data_size = (uint32_t)result.data_size;
    metadata.const_size = (uint32_t)result.const_size;

    failures += expect_bool(vm_layout_status_succeeded(vm_layout_build_automatic_policy(NULL, &metadata, &automatic_policy, &diagnostic)), "automatic policy should build");
    failures += expect_object_status(vm_object_map_build_from_symbols_with_layout(buffers.symbols, result.symbol_count, &automatic_policy, automatic_objects, TEST_OBJECTS, &automatic_count), VM_OBJECT_MAP_STATUS_OK, "automatic object map should build");
    failures += expect_size(automatic_count, 2U, "automatic object map should include data and const objects");
    automatic_value = find_object_by_name(automatic_objects, automatic_count, "value");
    automatic_limit = find_object_by_name(automatic_objects, automatic_count, "limit");
    if (automatic_value != NULL && automatic_limit != NULL) {
        failures += expect_u32(automatic_value->base_address, automatic_policy.regions[VM_LAYOUT_REGION_DATA].base, "automatic data object should use selected data base");
        failures += expect_u32(automatic_limit->base_address, automatic_policy.regions[VM_LAYOUT_REGION_CONST].base, "automatic const object should use selected const base");
    } else {
        failures += expect_bool(false, "automatic selected-layout objects should be discoverable");
    }

    randomized_base = vm_layout_default_policy();
    randomized_base.mode = VM_LAYOUT_MODE_SEEDED_RANDOMIZED;
    randomized_base.has_random_seed = true;
    randomized_base.random_seed = 2U;
    failures += expect_bool(vm_layout_status_succeeded(vm_layout_build_randomized_policy(&randomized_base, &metadata, VM_LAYOUT_MODE_SEEDED_RANDOMIZED, &randomized_policy, &diagnostic)), "randomized policy should build");
    failures += expect_object_status(vm_object_map_build_from_symbols_with_layout(buffers.symbols, result.symbol_count, &randomized_policy, randomized_objects, TEST_OBJECTS, &randomized_count), VM_OBJECT_MAP_STATUS_OK, "randomized object map should build");
    failures += expect_size(randomized_count, 2U, "randomized object map should include data and const objects");
    randomized_value = find_object_by_name(randomized_objects, randomized_count, "value");
    if (randomized_value != NULL) {
        failures += expect_u32(randomized_value->base_address, randomized_policy.regions[VM_LAYOUT_REGION_DATA].base, "randomized data object should use selected randomized base");
        failures += expect_bool(randomized_value->base_address != VM_MEMORY_DEFAULT_DATA_BASE || randomized_policy.regions[VM_LAYOUT_REGION_DATA].base == VM_MEMORY_DEFAULT_DATA_BASE, "randomized object base should follow selected policy exactly");
    } else {
        failures += expect_bool(false, "randomized selected-layout object should be discoverable");
    }
    return failures;
}

/// Verifies Phase 36 metadata does not change existing runtime behavior or diagnostics.
static int test_runtime_behavior_is_unchanged(void) {
    static const char *unaligned_source =
        ".data\n"
        "nums DWORD 2 DUP(0)\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, DWORD PTR nums[1]\n"
        "main ENDP\n"
        "END main\n";
    static const char *const_write_source =
        ".CONST\n"
        "limit DWORD 10\n"
        ".code\n"
        "main PROC\n"
        "    mov eax, OFFSET limit\n"
        "    mov DWORD PTR [eax], 20\n"
        "main ENDP\n"
        "END main\n";
    char unaligned_copy[TEST_JSON_COPY_CAPACITY];
    const char *unaligned_json = masm32_sim_wasm_run_source_json(unaligned_source);
    const char *const_json = NULL;
    int failures = 0;

    snprintf(unaligned_copy, sizeof(unaligned_copy), "%s", unaligned_json != NULL ? unaligned_json : "");
    const_json = masm32_sim_wasm_run_source_json(const_write_source);

    failures += expect_string_contains(unaligned_copy, "\"ok\":true", "unaligned access should still execute successfully");
    failures += expect_string_contains(unaligned_copy, "unaligned-memory-access", "unaligned warning should still be emitted");
    failures += expect_string_not_contains(unaligned_copy, "objectMap", "object map should not destabilize default source-run JSON");
    failures += expect_string_contains(const_json, "\"ok\":false", ".CONST write should still fail");
    failures += expect_string_contains(const_json, "permission-denied", ".CONST write should still be a checked-memory permission diagnostic");
    failures += expect_string_not_contains(const_json, "object-bounds", "Phase 36 should not add object-bounds diagnostics");
    return failures;
}

/// Runs all Phase 36 declared-object map tests.
int main(void) {
    int failures = 0;

    failures += test_object_map_records_declared_objects();
    failures += test_adjacent_objects_are_not_merged();
    failures += test_full_range_lookup_and_overflow();
    failures += test_full_range_classification_categories();
    failures += test_builder_edge_cases();
    failures += test_object_map_uses_selected_layout_bases();
    failures += test_runtime_behavior_is_unchanged();

    if (failures != 0) {
        fprintf(stderr, "test_object_map failed with %d failure(s)\n", failures);
        return 1;
    }

    printf("test_object_map passed\n");
    return 0;
}
