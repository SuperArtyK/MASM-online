/*
 * @file vm_cpu.h
 * @brief CPU register, alias, and flag model for MASM32 educational mode.
 *
 * This module owns canonical 32-bit register storage and exposes helpers for
 * reading and writing canonical registers, supported aliases, and the initial
 * EFLAGS bits required by arithmetic and jump behavior. It does not implement
 * instruction execution, parsing, memory access, or Extended 32-bit Mode.
 */

#ifndef MASM32_SIM_VM_CPU_H
#define MASM32_SIM_VM_CPU_H

#include <stdbool.h>
#include <stdint.h>

/// Identifies a canonical MASM32 register or supported register alias.
typedef enum VmRegister {
    /// Canonical 32-bit accumulator register.
    VM_REGISTER_EAX = 0,
    /// Canonical 32-bit base register.
    VM_REGISTER_EBX,
    /// Canonical 32-bit counter register.
    VM_REGISTER_ECX,
    /// Canonical 32-bit data register.
    VM_REGISTER_EDX,
    /// Canonical 32-bit source index register.
    VM_REGISTER_ESI,
    /// Canonical 32-bit destination index register.
    VM_REGISTER_EDI,
    /// Canonical 32-bit base pointer register.
    VM_REGISTER_EBP,
    /// Canonical 32-bit stack pointer register.
    VM_REGISTER_ESP,
    /// Canonical 32-bit instruction pointer register.
    VM_REGISTER_EIP,
    /// Canonical 32-bit flags register.
    VM_REGISTER_EFLAGS,
    /// Low 16-bit alias of EAX.
    VM_REGISTER_AX,
    /// High 8-bit alias of AX.
    VM_REGISTER_AH,
    /// Low 8-bit alias of AX.
    VM_REGISTER_AL,
    /// Low 16-bit alias of EBX.
    VM_REGISTER_BX,
    /// High 8-bit alias of BX.
    VM_REGISTER_BH,
    /// Low 8-bit alias of BX.
    VM_REGISTER_BL,
    /// Low 16-bit alias of ECX.
    VM_REGISTER_CX,
    /// High 8-bit alias of CX.
    VM_REGISTER_CH,
    /// Low 8-bit alias of CX.
    VM_REGISTER_CL,
    /// Low 16-bit alias of EDX.
    VM_REGISTER_DX,
    /// High 8-bit alias of DX.
    VM_REGISTER_DH,
    /// Low 8-bit alias of DX.
    VM_REGISTER_DL,
    /// Low 16-bit alias of ESI.
    VM_REGISTER_SI,
    /// Low 16-bit alias of EDI.
    VM_REGISTER_DI,
    /// Low 16-bit alias of EBP.
    VM_REGISTER_BP,
    /// Low 16-bit alias of ESP.
    VM_REGISTER_SP,
    /// Number of supported MASM32-mode register identifiers.
    VM_REGISTER_COUNT
} VmRegister;

/// Identifies one named EFLAGS bit supported by the Milestone 2 flag model.
typedef enum VmFlag {
    /// Carry flag, used for unsigned carry and borrow behavior.
    VM_FLAG_CF = 0,
    /// Zero flag, set when a masked arithmetic result is zero.
    VM_FLAG_ZF,
    /// Sign flag, set when the masked result's most significant bit is one.
    VM_FLAG_SF,
    /// Overflow flag, set when signed arithmetic overflows.
    VM_FLAG_OF,
    /// Number of supported named flags.
    VM_FLAG_COUNT
} VmFlag;

/// Describes validity and undefined-origin metadata for one modeled flag.
typedef struct VmFlagValidityMetadata {
    /// Whether the current deterministic flag bit is architecturally valid.
    bool is_valid;
    /// Stable diagnostic reason code for an invalid flag value, or NULL when valid.
    const char *undefined_code;
    /// Source mnemonic that produced the invalid flag value, or NULL when unavailable.
    const char *producer_mnemonic;
    /// Source file containing the producer instruction, or NULL when unavailable.
    const char *producer_source_file;
    /// One-based source line for the producer instruction, or zero when unavailable.
    uint32_t producer_source_line;
    /// One-based source column for the producer instruction, or zero when unavailable.
    uint32_t producer_source_column;
    /// Zero-based source byte offset for the producer instruction, or zero when unavailable.
    uint32_t producer_byte_offset;
    /// Source span length in bytes for the producer instruction, or zero when unavailable.
    uint32_t producer_span_length;
    /// Original source text for the producer instruction, or NULL when unavailable.
    const char *producer_source_text;
    /// VM instruction index of the producer instruction, or zero when unavailable.
    uint32_t producer_instruction_index;
} VmFlagValidityMetadata;

/// Stores canonical 32-bit MASM32 CPU registers, EFLAGS bits, and flag validity metadata.
typedef struct VmCpu {
    /// Canonical EAX storage.
    uint32_t eax;
    /// Canonical EBX storage.
    uint32_t ebx;
    /// Canonical ECX storage.
    uint32_t ecx;
    /// Canonical EDX storage.
    uint32_t edx;
    /// Canonical ESI storage.
    uint32_t esi;
    /// Canonical EDI storage.
    uint32_t edi;
    /// Canonical EBP storage.
    uint32_t ebp;
    /// Canonical ESP storage.
    uint32_t esp;
    /// Canonical EIP storage.
    uint32_t eip;
    /// Canonical EFLAGS storage containing supported and future flag bits.
    uint32_t eflags;
    /// Validity and undefined-origin metadata for currently modeled flags.
    VmFlagValidityMetadata flag_validity[VM_FLAG_COUNT];
} VmCpu;

/// Initializes all canonical CPU registers to zero.
///
/// @param cpu CPU state to initialize. A NULL pointer is ignored.
void vm_cpu_init(VmCpu *cpu);

/// Reads a canonical register or alias value from CPU state.
///
/// Alias reads return only the visible alias bits. For example, reading AL
/// returns bits 0-7 of EAX, and reading AH returns bits 8-15 of EAX.
///
/// @param cpu CPU state to read.
/// @param reg Register or alias identifier.
/// @param out_value Receives the register value on success.
/// @return true when the read succeeds; false for NULL pointers or invalid registers.
bool vm_cpu_read_register(const VmCpu *cpu, VmRegister reg, uint32_t *out_value);

/// Writes a canonical register or alias value into CPU state.
///
/// Alias writes are masked to the alias width and preserve all non-overlapping
/// bits of the canonical register. For example, writing AH updates bits 8-15 of
/// EAX only.
///
/// @param cpu CPU state to mutate.
/// @param reg Register or alias identifier.
/// @param value Value to write. The value is masked to the register width.
/// @return true when the write succeeds; false for a NULL CPU pointer or invalid register.
bool vm_cpu_write_register(VmCpu *cpu, VmRegister reg, uint32_t value);

/// Returns the width, in bits, of a canonical register or alias.
///
/// @param reg Register or alias identifier.
/// @return 32, 16, or 8 for supported registers; 0 for invalid registers.
uint8_t vm_cpu_register_width_bits(VmRegister reg);

/// Returns the uppercase display name for a canonical register or alias.
///
/// @param reg Register or alias identifier.
/// @return Static uppercase register name, or NULL for invalid registers.
const char *vm_cpu_register_name(VmRegister reg);

/// Reads one supported named flag from EFLAGS.
///
/// @param cpu CPU state to inspect.
/// @param flag Named flag identifier.
/// @param out_is_set Receives true when the flag bit is set.
/// @return true when the read succeeds; false for NULL pointers or invalid flags.
bool vm_cpu_read_flag(const VmCpu *cpu, VmFlag flag, bool *out_is_set);

/// Returns the uppercase display name for a supported named flag.
///
/// @param flag Named flag identifier.
/// @return Static uppercase flag name, or NULL for invalid flags.
const char *vm_cpu_flag_name(VmFlag flag);

/// Writes one supported named flag in EFLAGS.
///
/// Only the selected flag bit is changed. All other EFLAGS bits are preserved.
///
/// @param cpu CPU state to mutate.
/// @param flag Named flag identifier.
/// @param is_set Whether the flag should be set or cleared.
/// @return true when the write succeeds; false for a NULL CPU pointer or invalid flag.
bool vm_cpu_write_flag(VmCpu *cpu, VmFlag flag, bool is_set);

/// Sets one supported named flag in EFLAGS.
///
/// @param cpu CPU state to mutate.
/// @param flag Named flag identifier.
/// @return true when the flag is set; false for a NULL CPU pointer or invalid flag.
bool vm_cpu_set_flag(VmCpu *cpu, VmFlag flag);

/// Clears one supported named flag in EFLAGS.
///
/// @param cpu CPU state to mutate.
/// @param flag Named flag identifier.
/// @return true when the flag is cleared; false for a NULL CPU pointer or invalid flag.
bool vm_cpu_clear_flag(VmCpu *cpu, VmFlag flag);

/// Reads validity and undefined-origin metadata for one supported named flag.
///
/// @param cpu CPU state to inspect.
/// @param flag Named flag identifier.
/// @param out_metadata Receives a copy of the flag metadata.
/// @return true when the metadata is read; false for NULL pointers or invalid flags.
bool vm_cpu_read_flag_validity(const VmCpu *cpu, VmFlag flag, VmFlagValidityMetadata *out_metadata);

/// Marks one supported named flag architecturally valid and clears undefined-origin metadata.
///
/// The flag bit value itself is not changed. This helper is used when an API
/// writes raw EFLAGS or when an instruction makes a previously undefined flag
/// architecturally defined without changing other modeled flags.
///
/// @param cpu CPU state to mutate.
/// @param flag Named flag identifier.
/// @return true when metadata is updated; false for a NULL CPU pointer or invalid flag.
bool vm_cpu_mark_flag_valid(VmCpu *cpu, VmFlag flag);

/// Marks one supported named flag architecturally undefined without changing its bit value.
///
/// The deterministic EFLAGS bit is preserved. The supplied origin metadata is
/// retained for later diagnostics that may consume the flag. All string pointers
/// are borrowed from static diagnostic text or source buffers owned elsewhere.
///
/// @param cpu CPU state to mutate.
/// @param flag Named flag identifier.
/// @param undefined_code Stable diagnostic reason code, such as undefined-shift-flag.
/// @param producer_mnemonic Source mnemonic that made the flag undefined.
/// @param source_file Source file for the producer instruction, or NULL.
/// @param source_line One-based source line for the producer instruction, or zero.
/// @param source_column One-based source column for the producer instruction, or zero.
/// @param byte_offset Zero-based source byte offset for the producer instruction, or zero.
/// @param span_length Source span length in bytes for the producer instruction, or zero.
/// @param source_text Original source text for the producer instruction, or NULL.
/// @param instruction_index VM instruction index for the producer instruction.
/// @return true when metadata is updated; false for a NULL CPU pointer or invalid flag.
bool vm_cpu_mark_flag_undefined(
    VmCpu *cpu,
    VmFlag flag,
    const char *undefined_code,
    const char *producer_mnemonic,
    const char *source_file,
    uint32_t source_line,
    uint32_t source_column,
    uint32_t byte_offset,
    uint32_t span_length,
    const char *source_text,
    uint32_t instruction_index
);

/// Updates arithmetic flags for an addition operation.
///
/// The operands are masked to @p width_bits before flag calculation. Supported
/// widths are 8, 16, and 32 bits. When @p out_result is non-NULL, it receives
/// the masked addition result. Invalid widths fail without mutating EFLAGS.
///
/// @param cpu CPU state whose flags will be updated.
/// @param left Left operand before masking.
/// @param right Right operand before masking.
/// @param width_bits Operand width in bits; must be 8, 16, or 32.
/// @param out_result Optional receiver for the masked result.
/// @return true when flags are updated; false for a NULL CPU pointer or invalid width.
bool vm_cpu_update_add_flags(VmCpu *cpu, uint32_t left, uint32_t right, uint8_t width_bits, uint32_t *out_result);

/// Updates arithmetic flags for a subtraction operation.
///
/// The operands are masked to @p width_bits before flag calculation. Supported
/// widths are 8, 16, and 32 bits. When @p out_result is non-NULL, it receives
/// the masked subtraction result. Invalid widths fail without mutating EFLAGS.
///
/// @param cpu CPU state whose flags will be updated.
/// @param left Minuend operand before masking.
/// @param right Subtrahend operand before masking.
/// @param width_bits Operand width in bits; must be 8, 16, or 32.
/// @param out_result Optional receiver for the masked result.
/// @return true when flags are updated; false for a NULL CPU pointer or invalid width.
bool vm_cpu_update_sub_flags(VmCpu *cpu, uint32_t left, uint32_t right, uint8_t width_bits, uint32_t *out_result);

/// Updates arithmetic flags for a comparison operation.
///
/// This is equivalent to updating flags for @p left - @p right without exposing
/// or storing the subtraction result. Supported widths are 8, 16, and 32 bits.
/// Invalid widths fail without mutating EFLAGS.
///
/// @param cpu CPU state whose flags will be updated.
/// @param left Left comparison operand before masking.
/// @param right Right comparison operand before masking.
/// @param width_bits Operand width in bits; must be 8, 16, or 32.
/// @return true when flags are updated; false for a NULL CPU pointer or invalid width.
bool vm_cpu_update_cmp_flags(VmCpu *cpu, uint32_t left, uint32_t right, uint8_t width_bits);

#endif
