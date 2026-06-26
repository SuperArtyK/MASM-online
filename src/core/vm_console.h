/*
 * @file vm_console.h
 * @brief Program Console output buffer for the MASM32 educational VM.
 *
 * The Program Console is the simulated program output stream. Simulator-owned
 * diagnostics, status text, worker pings, and UI errors must use Simulator
 * Messages instead. Phase 86 added deterministic Program Console byte and line
 * limits. Phase 87 routes virtual Irvine32 `Crlf` through this buffer, and
 * Phase 88 routes direct `WriteChar` output through the same buffer while
 * keeping non-target Irvine32 output routines deferred to later milestones.
 */

#ifndef MASM32_SIM_VM_CONSOLE_H
#define MASM32_SIM_VM_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>

/// Default Program Console byte limit for the canonical default_console_max_bytes setting.
#define VM_CONSOLE_DEFAULT_MAX_BYTES 1048576U

/// Default Program Console line-feed limit for the canonical default_console_max_lines setting.
#define VM_CONSOLE_DEFAULT_MAX_LINES 10000U

/// Describes which Program Console limit rejected an append.
typedef enum VmConsoleLimitKind {
    /// No Program Console limit has rejected output.
    VM_CONSOLE_LIMIT_KIND_NONE = 0,
    /// The byte-count limit rejected an append.
    VM_CONSOLE_LIMIT_KIND_BYTE,
    /// The line-feed-count limit rejected an append.
    VM_CONSOLE_LIMIT_KIND_LINE
} VmConsoleLimitKind;

/// Describes the result of a Program Console operation.
typedef enum VmConsoleStatus {
    /// Operation completed successfully.
    VM_CONSOLE_STATUS_OK = 0,
    /// Operation failed because a required argument was invalid.
    VM_CONSOLE_STATUS_INVALID_ARGUMENT,
    /// Operation failed because backing storage could not be allocated.
    VM_CONSOLE_STATUS_OUT_OF_MEMORY,
    /// Operation failed because appending the complete span would exceed an output limit.
    VM_CONSOLE_STATUS_OUTPUT_LIMIT_EXCEEDED
} VmConsoleStatus;

/// Owns the VM Program Console byte stream and rendered text view.
typedef struct VmConsole {
    /// NUL-terminated text buffer containing committed Program Console bytes.
    char *text;
    /// Allocated capacity of @ref text in bytes, including the NUL terminator.
    size_t capacity;
    /// Number of committed Program Console bytes excluding the NUL terminator.
    size_t byte_count;
    /// Number of committed line-feed bytes; CRLF contributes one line because only '\n' is counted.
    size_t line_count;
    /// Maximum committed bytes allowed before append operations fail.
    size_t max_bytes;
    /// Maximum committed line-feed bytes allowed before append operations fail.
    size_t max_lines;
    /// Whether any append has been rejected by the configured output limits.
    bool limit_exceeded;
    /// Most recent limit kind that rejected an append.
    VmConsoleLimitKind limit_kind;
    /// Whether output has been actually truncated by an explicit truncation mode.
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

/// Clears committed Program Console output while preserving reusable storage and configured limits.
///
/// @param console Console object to clear.
/// @return VM_CONSOLE_STATUS_OK on success, or VM_CONSOLE_STATUS_INVALID_ARGUMENT.
VmConsoleStatus vm_console_reset(VmConsole *console);

/// Configures deterministic Program Console output limits.
///
/// Both limits must be nonzero and must not be lower than already committed
/// output. The default stop-on-limit policy rejects an append before copying
/// any bytes when the complete append would exceed either limit.
///
/// @param console Console object to configure.
/// @param max_bytes Maximum committed Program Console bytes.
/// @param max_lines Maximum committed line-feed bytes.
/// @return VM_CONSOLE_STATUS_OK on success, or VM_CONSOLE_STATUS_INVALID_ARGUMENT.
VmConsoleStatus vm_console_configure_limits(VmConsole *console, size_t max_bytes, size_t max_lines);

/// Appends bytes to the Program Console stream.
///
/// Passing @p byte_count as zero is a no-op and does not require @p text to be
/// non-NULL. Phase 86 enforces a stop-on-limit policy: if the whole append
/// would exceed the byte or line limit, the call fails with no partial append.
/// Newline counting is byte-oriented and counts committed '\n' bytes only;
/// CRLF therefore counts as one line and standalone '\r' counts as zero.
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

/// Returns the configured Program Console byte limit.
///
/// @param console Console object to inspect.
/// @return Configured byte limit, or the default limit when unavailable.
size_t vm_console_max_bytes(const VmConsole *console);

/// Returns the configured Program Console line-feed limit.
///
/// @param console Console object to inspect.
/// @return Configured line-feed limit, or the default limit when unavailable.
size_t vm_console_max_lines(const VmConsole *console);

/// Returns whether a Program Console append has exceeded a configured limit.
///
/// @param console Console object to inspect.
/// @return true after an append was rejected by a byte or line limit.
bool vm_console_limit_exceeded(const VmConsole *console);

/// Returns the most recent Program Console limit kind that rejected an append.
///
/// @param console Console object to inspect.
/// @return Limit kind, or VM_CONSOLE_LIMIT_KIND_NONE when no limit has fired.
VmConsoleLimitKind vm_console_limit_kind(const VmConsole *console);

/// Returns whether Program Console output has been truncated.
///
/// The default Phase 86 policy stops before appending and therefore does not
/// truncate committed output. This accessor remains false unless a later
/// documented truncation mode is implemented.
///
/// @param console Console object to inspect.
/// @return true when an explicit truncation mode has truncated output.
bool vm_console_truncated(const VmConsole *console);

/// Returns a stable lowercase name for a Program Console limit kind.
///
/// @param kind Limit kind to inspect.
/// @return Static limit-kind name, or NULL for invalid values.
const char *vm_console_limit_kind_name(VmConsoleLimitKind kind);

/// Returns a stable lowercase name for a Program Console status.
///
/// @param status Console status to inspect.
/// @return Static status name, or NULL for invalid status values.
const char *vm_console_status_name(VmConsoleStatus status);

#endif
