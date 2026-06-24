/*
 * @file vm_console.c
 * @brief Program Console output buffer implementation for the MASM32 educational VM.
 *
 * The Program Console stores simulated program output separately from
 * Simulator Messages. Phase 85 provides allocation, append, reset, and
 * inspection helpers without adding any Irvine32 output routine behavior.
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

    status = vm_console_ensure_capacity(console, 1U);
    if (status != VM_CONSOLE_STATUS_OK) {
        return status;
    }

    console->text[0] = '\0';
    console->byte_count = 0U;
    console->line_count = 0U;
    console->truncated = false;
    return VM_CONSOLE_STATUS_OK;
}

VmConsoleStatus vm_console_append(VmConsole *console, const char *text, size_t byte_count) {
    VmConsoleStatus status = VM_CONSOLE_STATUS_OK;
    size_t needed_capacity = 0U;

    if (console == NULL || (text == NULL && byte_count > 0U)) {
        return VM_CONSOLE_STATUS_INVALID_ARGUMENT;
    }
    if (byte_count == 0U) {
        return VM_CONSOLE_STATUS_OK;
    }
    if (!vm_console_append_size_fits(console->byte_count, byte_count)) {
        return VM_CONSOLE_STATUS_OUT_OF_MEMORY;
    }

    needed_capacity = console->byte_count + byte_count + 1U;
    status = vm_console_ensure_capacity(console, needed_capacity);
    if (status != VM_CONSOLE_STATUS_OK) {
        return status;
    }

    memcpy(console->text + console->byte_count, text, byte_count);
    console->byte_count += byte_count;
    console->text[console->byte_count] = '\0';
    console->line_count += vm_console_count_line_feeds(text, byte_count);
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

bool vm_console_truncated(const VmConsole *console) {
    return console != NULL && console->truncated;
}

const char *vm_console_status_name(VmConsoleStatus status) {
    switch (status) {
        case VM_CONSOLE_STATUS_OK:
            return "ok";
        case VM_CONSOLE_STATUS_INVALID_ARGUMENT:
            return "invalid-argument";
        case VM_CONSOLE_STATUS_OUT_OF_MEMORY:
            return "out-of-memory";
        default:
            return NULL;
    }
}
