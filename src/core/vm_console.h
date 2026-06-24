/*
 * @file vm_console.h
 * @brief Program Console output buffer for the MASM32 educational VM.
 *
 * The Program Console is the simulated program output stream. Simulator-owned
 * diagnostics, status text, worker pings, and UI errors must use Simulator
 * Messages instead. Phase 85 introduces this stream as infrastructure only;
 * later Irvine32 routine phases append user program output through this module.
 */

#ifndef MASM32_SIM_VM_CONSOLE_H
#define MASM32_SIM_VM_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>

/// Describes the result of a Program Console operation.
typedef enum VmConsoleStatus {
    /// Operation completed successfully.
    VM_CONSOLE_STATUS_OK = 0,
    /// Operation failed because a required argument was invalid.
    VM_CONSOLE_STATUS_INVALID_ARGUMENT,
    /// Operation failed because backing storage could not be allocated.
    VM_CONSOLE_STATUS_OUT_OF_MEMORY
} VmConsoleStatus;

/// Owns the VM Program Console byte stream and rendered text view.
typedef struct VmConsole {
    /// NUL-terminated text buffer containing committed Program Console bytes.
    char *text;
    /// Allocated capacity of @ref text in bytes, including the NUL terminator.
    size_t capacity;
    /// Number of committed Program Console bytes excluding the NUL terminator.
    size_t byte_count;
    /// Number of line-feed bytes committed to the Program Console stream.
    size_t line_count;
    /// Whether output has been truncated by a future output-limit policy.
    bool truncated;
} VmConsole;

/// Initializes a Program Console buffer.
///
/// @param console Console object to initialize.
/// @return VM_CONSOLE_STATUS_OK on success, or a status describing failure.
VmConsoleStatus vm_console_init(VmConsole *console);

/// Releases storage owned by a Program Console buffer.
///
/// @param console Console object to release. NULL is ignored.
void vm_console_deinit(VmConsole *console);

/// Clears committed Program Console output while preserving reusable storage.
///
/// @param console Console object to clear.
/// @return VM_CONSOLE_STATUS_OK on success, or VM_CONSOLE_STATUS_INVALID_ARGUMENT.
VmConsoleStatus vm_console_reset(VmConsole *console);

/// Appends bytes to the Program Console stream.
///
/// Passing @p byte_count as zero is a no-op and does not require @p text to be
/// non-NULL. Phase 85 does not enforce output limits, so @ref truncated remains
/// false unless Phase 86 Program Console output limits deliberately add a limit policy.
///
/// @param console Console object to mutate.
/// @param text Bytes to append; may be NULL only when @p byte_count is zero.
/// @param byte_count Number of bytes to append.
/// @return VM_CONSOLE_STATUS_OK on success, or a status describing failure.
VmConsoleStatus vm_console_append(VmConsole *console, const char *text, size_t byte_count);

/// Returns the committed Program Console text.
///
/// @param console Console object to inspect.
/// @return NUL-terminated text, or an empty string when unavailable.
const char *vm_console_text(const VmConsole *console);

/// Returns the committed Program Console byte count.
///
/// @param console Console object to inspect.
/// @return Number of committed bytes, or zero when unavailable.
size_t vm_console_byte_count(const VmConsole *console);

/// Returns the committed Program Console line-feed count.
///
/// @param console Console object to inspect.
/// @return Number of committed line-feed bytes, or zero when unavailable.
size_t vm_console_line_count(const VmConsole *console);

/// Returns whether Program Console output has been truncated.
///
/// @param console Console object to inspect.
/// @return true when a future truncation policy has truncated output.
bool vm_console_truncated(const VmConsole *console);

/// Returns a stable lowercase name for a Program Console status.
///
/// @param status Console status to inspect.
/// @return Static status name, or NULL for invalid status values.
const char *vm_console_status_name(VmConsoleStatus status);

#endif
