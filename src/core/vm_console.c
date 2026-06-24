/*
 * @file vm_console.c
 * @brief Program Console output buffer implementation for the MASM32 educational VM.
 *
 * The Program Console stores simulated program output separately from
 * Simulator Messages. Phase 86 enforces deterministic byte and line limits
 * without adding any Irvine32 output routine behavior.
 */

#include "vm_console.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// Initial reusable Program Console capacity, including the NUL terminator.
#define VM_CONSOLE_INITIAL_CAPACITY 64U

/// Returns whether @p a + @p b + one NUL terminator fits in size_t.
///
/// @param a Existing byte count.
/// @param b Bytes to append.
/// @return true when the append size can be represented.
static bool vm_console_append_size_fits(size_t a, size_t b) {
    return b <= SIZE_MAX - 1U && a <= SIZE_MAX - 1U - b;
}

/// Returns whether @p a + @p b fits in size_t.
///
/// @param a Existing count.
/// @param b Additional count.
/// @return true when the sum can be represented.
static bool vm_console_count_sum_fits(size_t a, size_t b) {
    return a <= SIZE_MAX - b;
}

/// Counts line-feed bytes in one byte span.
///
/// @param text Text bytes to inspect.
/// @param byte_count Number of bytes in @p text.
/// @return Number of '\n' bytes in the span.
static size_t vm_console_count_line_feeds(const char *text, size_t byte_count) {
    size_t index = 0U;
    size_t count = 0U;

    if (text == NULL) {
        return 0U;
    }

    for (index = 0U; index < byte_count; index += 1U) {
        if (text[index] == '\n') {
            count += 1U;
        }
    }

    return count;
}

/// Sets the rejected-limit status for a Program Console append.
///
/// @param console Console object to mutate.
/// @param kind Limit kind that rejected the append.
/// @return VM_CONSOLE_STATUS_OUTPUT_LIMIT_EXCEEDED.
static VmConsoleStatus vm_console_reject_limit(VmConsole *console, VmConsoleLimitKind kind) {
    if (console != NULL) {
        console->limit_exceeded = true;
        console->limit_kind = kind;
    }
    return VM_CONSOLE_STATUS_OUTPUT_LIMIT_EXCEEDED;
}

/// Ensures configured limits contain valid Phase 86 defaults.
///
/// @param console Console object to normalize.
static void vm_console_ensure_default_limits(VmConsole *console) {
    if (console == NULL) {
        return;
    }
    if (console->max_bytes == 0U) {
        console->max_bytes = VM_CONSOLE_DEFAULT_MAX_BYTES;
    }
    if (console->max_lines == 0U) {
        console->max_lines = VM_CONSOLE_DEFAULT_MAX_LINES;
    }
}

/// Ensures the Program Console has at least @p needed_capacity bytes available.
///
/// @param console Console object to grow.
/// @param needed_capacity Required capacity including the NUL terminator.
/// @return VM_CONSOLE_STATUS_OK on success, or VM_CONSOLE_STATUS_OUT_OF_MEMORY.
static VmConsoleStatus vm_console_ensure_capacity(VmConsole *console, size_t needed_capacity) {
    size_t new_capacity = VM_CONSOLE_INITIAL_CAPACITY;
    char *new_text = NULL;

    if (console == NULL) {
        return VM_CONSOLE_STATUS_INVALID_ARGUMENT;
    }

    if (console->capacity >= needed_capacity && console->text != NULL) {
        return VM_CONSOLE_STATUS_OK;
    }

    if (console->capacity > new_capacity) {
        new_capacity = console->capacity;
    }
    while (new_capacity < needed_capacity) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = needed_capacity;
            break;
        }
        new_capacity *= 2U;
    }

    new_text = (char *)realloc(console->text, new_capacity);
    if (new_text == NULL) {
        return VM_CONSOLE_STATUS_OUT_OF_MEMORY;
    }

    console->text = new_text;
    console->capacity = new_capacity;
    return VM_CONSOLE_STATUS_OK;
}

VmConsoleStatus vm_console_init(VmConsole *console) {
    if (console == NULL) {
        return VM_CONSOLE_STATUS_INVALID_ARGUMENT;
    }

    memset(console, 0, sizeof(*console));
    console->max_bytes = VM_CONSOLE_DEFAULT_MAX_BYTES;
    console->max_lines = VM_CONSOLE_DEFAULT_MAX_LINES;
    return vm_console_reset(console);
}

void vm_console_deinit(VmConsole *console) {
    if (console == NULL) {
        return;
    }

    free(console->text);
    memset(console, 0, sizeof(*console));
}

VmConsoleStatus vm_console_reset(VmConsole *console) {
    VmConsoleStatus status = VM_CONSOLE_STATUS_OK;

    if (console == NULL) {
        return VM_CONSOLE_STATUS_INVALID_ARGUMENT;
    }

    vm_console_ensure_default_limits(console);
    status = vm_console_ensure_capacity(console, 1U);
    if (status != VM_CONSOLE_STATUS_OK) {
        return status;
    }

    console->text[0] = '\0';
    console->byte_count = 0U;
    console->line_count = 0U;
    console->limit_exceeded = false;
    console->limit_kind = VM_CONSOLE_LIMIT_KIND_NONE;
    console->truncated = false;
    return VM_CONSOLE_STATUS_OK;
}

VmConsoleStatus vm_console_configure_limits(VmConsole *console, size_t max_bytes, size_t max_lines) {
    if (console == NULL || max_bytes == 0U || max_lines == 0U) {
        return VM_CONSOLE_STATUS_INVALID_ARGUMENT;
    }
    if (console->byte_count > max_bytes || console->line_count > max_lines) {
        return VM_CONSOLE_STATUS_INVALID_ARGUMENT;
    }

    console->max_bytes = max_bytes;
    console->max_lines = max_lines;
    console->limit_exceeded = false;
    console->limit_kind = VM_CONSOLE_LIMIT_KIND_NONE;
    return VM_CONSOLE_STATUS_OK;
}

VmConsoleStatus vm_console_append(VmConsole *console, const char *text, size_t byte_count) {
    VmConsoleStatus status = VM_CONSOLE_STATUS_OK;
    size_t needed_capacity = 0U;
    size_t append_line_count = 0U;

    if (console == NULL || (text == NULL && byte_count > 0U)) {
        return VM_CONSOLE_STATUS_INVALID_ARGUMENT;
    }
    vm_console_ensure_default_limits(console);
    if (byte_count == 0U) {
        return VM_CONSOLE_STATUS_OK;
    }
    if (!vm_console_append_size_fits(console->byte_count, byte_count)) {
        return VM_CONSOLE_STATUS_OUT_OF_MEMORY;
    }

    append_line_count = vm_console_count_line_feeds(text, byte_count);
    if (!vm_console_count_sum_fits(console->line_count, append_line_count)) {
        return VM_CONSOLE_STATUS_OUT_OF_MEMORY;
    }
    if (console->byte_count + byte_count > console->max_bytes) {
        return vm_console_reject_limit(console, VM_CONSOLE_LIMIT_KIND_BYTE);
    }
    if (console->line_count + append_line_count > console->max_lines) {
        return vm_console_reject_limit(console, VM_CONSOLE_LIMIT_KIND_LINE);
    }

    needed_capacity = console->byte_count + byte_count + 1U;
    status = vm_console_ensure_capacity(console, needed_capacity);
    if (status != VM_CONSOLE_STATUS_OK) {
        return status;
    }

    memcpy(console->text + console->byte_count, text, byte_count);
    console->byte_count += byte_count;
    console->text[console->byte_count] = '\0';
    console->line_count += append_line_count;
    return VM_CONSOLE_STATUS_OK;
}

const char *vm_console_text(const VmConsole *console) {
    return console != NULL && console->text != NULL ? console->text : "";
}

size_t vm_console_byte_count(const VmConsole *console) {
    return console != NULL ? console->byte_count : 0U;
}

size_t vm_console_line_count(const VmConsole *console) {
    return console != NULL ? console->line_count : 0U;
}

size_t vm_console_max_bytes(const VmConsole *console) {
    return console != NULL && console->max_bytes != 0U ? console->max_bytes : VM_CONSOLE_DEFAULT_MAX_BYTES;
}

size_t vm_console_max_lines(const VmConsole *console) {
    return console != NULL && console->max_lines != 0U ? console->max_lines : VM_CONSOLE_DEFAULT_MAX_LINES;
}

bool vm_console_limit_exceeded(const VmConsole *console) {
    return console != NULL && console->limit_exceeded;
}

VmConsoleLimitKind vm_console_limit_kind(const VmConsole *console) {
    return console != NULL ? console->limit_kind : VM_CONSOLE_LIMIT_KIND_NONE;
}

bool vm_console_truncated(const VmConsole *console) {
    return console != NULL && console->truncated;
}

const char *vm_console_limit_kind_name(VmConsoleLimitKind kind) {
    switch (kind) {
        case VM_CONSOLE_LIMIT_KIND_NONE:
            return "none";
        case VM_CONSOLE_LIMIT_KIND_BYTE:
            return "byte";
        case VM_CONSOLE_LIMIT_KIND_LINE:
            return "line";
        default:
            return NULL;
    }
}

const char *vm_console_status_name(VmConsoleStatus status) {
    switch (status) {
        case VM_CONSOLE_STATUS_OK:
            return "ok";
        case VM_CONSOLE_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_CONSOLE_STATUS_OUT_OF_MEMORY:
            return "out-of-memory";
        case VM_CONSOLE_STATUS_OUTPUT_LIMIT_EXCEEDED:
            return "output-limit-exceeded";
        default:
            return NULL;
    }
}
