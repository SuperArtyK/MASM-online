/*
 * @file vm_cpu.c
 * @brief CPU register, alias, and flag helpers for MASM32 educational mode.
 *
 * This implementation stores only canonical 32-bit registers. Alias reads and
 * writes are derived from canonical storage, and named flags are manipulated
 * through checked helpers that preserve unrelated EFLAGS bits.
 */

#include "vm_cpu.h"

#include <stddef.h>

/// Identifies the canonical register storage used by a register descriptor.
typedef enum VmCpuStorageRegister {
    /// EAX canonical storage.
    VM_CPU_STORAGE_EAX = 0,
    /// EBX canonical storage.
    VM_CPU_STORAGE_EBX,
    /// ECX canonical storage.
    VM_CPU_STORAGE_ECX,
    /// EDX canonical storage.
    VM_CPU_STORAGE_EDX,
    /// ESI canonical storage.
    VM_CPU_STORAGE_ESI,
    /// EDI canonical storage.
    VM_CPU_STORAGE_EDI,
    /// EBP canonical storage.
    VM_CPU_STORAGE_EBP,
    /// ESP canonical storage.
    VM_CPU_STORAGE_ESP,
    /// EIP canonical storage.
    VM_CPU_STORAGE_EIP,
    /// EFLAGS canonical storage.
    VM_CPU_STORAGE_EFLAGS
} VmCpuStorageRegister;

/// Describes how a public register identifier maps to canonical storage.
typedef struct VmCpuRegisterMetadata {
    /// Canonical storage field containing the register bits.
    VmCpuStorageRegister storage;
    /// Number of low-order bits to shift before masking alias values.
    uint8_t shift_bits;
    /// Visible width of the register or alias in bits.
    uint8_t width_bits;
    /// Uppercase display name for diagnostics and debugger UI.
    const char *name;
} VmCpuRegisterMetadata;

/// Describes the EFLAGS bit represented by a public named flag identifier.
typedef struct VmCpuFlagMetadata {
    /// Bit mask for this flag inside EFLAGS.
    uint32_t mask;
    /// Uppercase flag name for diagnostics and future debugger UI.
    const char *name;
} VmCpuFlagMetadata;

/// Metadata table indexed by VmRegister values.
static const VmCpuRegisterMetadata VM_CPU_REGISTER_METADATA[VM_REGISTER_COUNT] = {
    {VM_CPU_STORAGE_EAX, 0U, 32U, "EAX"},
    {VM_CPU_STORAGE_EBX, 0U, 32U, "EBX"},
    {VM_CPU_STORAGE_ECX, 0U, 32U, "ECX"},
    {VM_CPU_STORAGE_EDX, 0U, 32U, "EDX"},
    {VM_CPU_STORAGE_ESI, 0U, 32U, "ESI"},
    {VM_CPU_STORAGE_EDI, 0U, 32U, "EDI"},
    {VM_CPU_STORAGE_EBP, 0U, 32U, "EBP"},
    {VM_CPU_STORAGE_ESP, 0U, 32U, "ESP"},
    {VM_CPU_STORAGE_EIP, 0U, 32U, "EIP"},
    {VM_CPU_STORAGE_EFLAGS, 0U, 32U, "EFLAGS"},
    {VM_CPU_STORAGE_EAX, 0U, 16U, "AX"},
    {VM_CPU_STORAGE_EAX, 8U, 8U, "AH"},
    {VM_CPU_STORAGE_EAX, 0U, 8U, "AL"},
    {VM_CPU_STORAGE_EBX, 0U, 16U, "BX"},
    {VM_CPU_STORAGE_EBX, 8U, 8U, "BH"},
    {VM_CPU_STORAGE_EBX, 0U, 8U, "BL"},
    {VM_CPU_STORAGE_ECX, 0U, 16U, "CX"},
    {VM_CPU_STORAGE_ECX, 8U, 8U, "CH"},
    {VM_CPU_STORAGE_ECX, 0U, 8U, "CL"},
    {VM_CPU_STORAGE_EDX, 0U, 16U, "DX"},
    {VM_CPU_STORAGE_EDX, 8U, 8U, "DH"},
    {VM_CPU_STORAGE_EDX, 0U, 8U, "DL"},
    {VM_CPU_STORAGE_ESI, 0U, 16U, "SI"},
    {VM_CPU_STORAGE_EDI, 0U, 16U, "DI"},
    {VM_CPU_STORAGE_EBP, 0U, 16U, "BP"},
    {VM_CPU_STORAGE_ESP, 0U, 16U, "SP"}
};

/// Metadata table indexed by VmFlag values.
static const VmCpuFlagMetadata VM_CPU_FLAG_METADATA[VM_FLAG_COUNT] = {
    {0x00000001U, "CF"},
    {0x00000040U, "ZF"},
    {0x00000080U, "SF"},
    {0x00000800U, "OF"}
};

/// Looks up metadata for a public register identifier.
///
/// @param reg Register identifier to resolve.
/// @param out_metadata Receives a pointer to static metadata on success.
/// @return true when @p reg is valid and @p out_metadata is non-NULL.
static bool vm_cpu_get_register_metadata(VmRegister reg, const VmCpuRegisterMetadata **out_metadata) {
    int register_index = (int)reg;

    if (out_metadata == NULL) {
        return false;
    }

    if (register_index < 0 || register_index >= (int)VM_REGISTER_COUNT) {
        return false;
    }

    *out_metadata = &VM_CPU_REGISTER_METADATA[register_index];
    return true;
}

/// Looks up metadata for a supported named flag.
///
/// @param flag Flag identifier to resolve.
/// @param out_metadata Receives a pointer to static metadata on success.
/// @return true when @p flag is valid and @p out_metadata is non-NULL.
static bool vm_cpu_get_flag_metadata(VmFlag flag, const VmCpuFlagMetadata **out_metadata) {
    int flag_index = (int)flag;

    if (out_metadata == NULL) {
        return false;
    }

    if (flag_index < 0 || flag_index >= (int)VM_FLAG_COUNT) {
        return false;
    }

    *out_metadata = &VM_CPU_FLAG_METADATA[flag_index];
    return true;
}

/// Returns whether a flag identifier indexes current validity metadata.
///
/// @param flag Flag identifier to inspect.
/// @return true when @p flag has a validity-metadata slot.
static bool vm_cpu_flag_has_validity_slot(VmFlag flag) {
    int flag_index = (int)flag;

    return flag_index >= 0 && flag_index < (int)VM_FLAG_COUNT;
}

/// Clears undefined-origin metadata for one validity slot and marks it valid.
///
/// @param metadata Metadata slot to mutate.
static void vm_cpu_clear_flag_origin(VmFlagValidityMetadata *metadata) {
    if (metadata == NULL) {
        return;
    }

    metadata->is_valid = true;
    metadata->undefined_code = NULL;
    metadata->producer_mnemonic = NULL;
    metadata->producer_source_file = NULL;
    metadata->producer_source_line = 0U;
    metadata->producer_source_column = 0U;
    metadata->producer_byte_offset = 0U;
    metadata->producer_span_length = 0U;
    metadata->producer_source_text = NULL;
    metadata->producer_instruction_index = 0U;
}

/// Marks all modeled flag metadata valid.
///
/// @param cpu CPU state to mutate.
static void vm_cpu_mark_all_flags_valid(VmCpu *cpu) {
    int flag_index = 0;

    if (cpu == NULL) {
        return;
    }

    for (flag_index = 0; flag_index < (int)VM_FLAG_COUNT; flag_index += 1) {
        vm_cpu_clear_flag_origin(&cpu->flag_validity[flag_index]);
    }
}

/// Reads a full 32-bit canonical storage value.
///
/// @param cpu CPU state to inspect.
/// @param storage Canonical storage field to read.
/// @param out_value Receives the 32-bit storage value.
/// @return true when all arguments are valid.
static bool vm_cpu_read_storage(const VmCpu *cpu, VmCpuStorageRegister storage, uint32_t *out_value) {
    if (cpu == NULL || out_value == NULL) {
        return false;
    }

    switch (storage) {
        case VM_CPU_STORAGE_EAX:
            *out_value = cpu->eax;
            return true;
        case VM_CPU_STORAGE_EBX:
            *out_value = cpu->ebx;
            return true;
        case VM_CPU_STORAGE_ECX:
            *out_value = cpu->ecx;
            return true;
        case VM_CPU_STORAGE_EDX:
            *out_value = cpu->edx;
            return true;
        case VM_CPU_STORAGE_ESI:
            *out_value = cpu->esi;
            return true;
        case VM_CPU_STORAGE_EDI:
            *out_value = cpu->edi;
            return true;
        case VM_CPU_STORAGE_EBP:
            *out_value = cpu->ebp;
            return true;
        case VM_CPU_STORAGE_ESP:
            *out_value = cpu->esp;
            return true;
        case VM_CPU_STORAGE_EIP:
            *out_value = cpu->eip;
            return true;
        case VM_CPU_STORAGE_EFLAGS:
            *out_value = cpu->eflags;
            return true;
        default:
            return false;
    }
}

/// Writes a full 32-bit canonical storage value.
///
/// @param cpu CPU state to mutate.
/// @param storage Canonical storage field to write.
/// @param value Full 32-bit value to store.
/// @return true when all arguments are valid.
static bool vm_cpu_write_storage(VmCpu *cpu, VmCpuStorageRegister storage, uint32_t value) {
    if (cpu == NULL) {
        return false;
    }

    switch (storage) {
        case VM_CPU_STORAGE_EAX:
            cpu->eax = value;
            return true;
        case VM_CPU_STORAGE_EBX:
            cpu->ebx = value;
            return true;
        case VM_CPU_STORAGE_ECX:
            cpu->ecx = value;
            return true;
        case VM_CPU_STORAGE_EDX:
            cpu->edx = value;
            return true;
        case VM_CPU_STORAGE_ESI:
            cpu->esi = value;
            return true;
        case VM_CPU_STORAGE_EDI:
            cpu->edi = value;
            return true;
        case VM_CPU_STORAGE_EBP:
            cpu->ebp = value;
            return true;
        case VM_CPU_STORAGE_ESP:
            cpu->esp = value;
            return true;
        case VM_CPU_STORAGE_EIP:
            cpu->eip = value;
            return true;
        case VM_CPU_STORAGE_EFLAGS:
            cpu->eflags = value;
            return true;
        default:
            return false;
    }
}

/// Builds a bit mask for a supported register or operand width.
///
/// @param width_bits Width in bits.
/// @return Mask with @p width_bits low bits set, or 0 for unsupported widths.
static uint32_t vm_cpu_width_mask(uint8_t width_bits) {
    if (width_bits == 32U) {
        return UINT32_MAX;
    }

    if (width_bits == 16U) {
        return 0x0000FFFFU;
    }

    if (width_bits == 8U) {
        return 0x000000FFU;
    }

    return 0U;
}

/// Builds the sign-bit mask for a supported operand width.
///
/// @param width_bits Width in bits.
/// @return Sign-bit mask for 8, 16, or 32 bits; otherwise 0.
static uint32_t vm_cpu_sign_bit_mask(uint8_t width_bits) {
    if (width_bits == 32U) {
        return 0x80000000U;
    }

    if (width_bits == 16U) {
        return 0x00008000U;
    }

    if (width_bits == 8U) {
        return 0x00000080U;
    }

    return 0U;
}

/// Applies the four Milestone 2 arithmetic flags to EFLAGS.
///
/// Unrelated EFLAGS bits are preserved. The named flags are computed before any
/// mutation so callers do not observe partial changes for valid arithmetic.
///
/// @param cpu CPU state to mutate.
/// @param carry_flag New carry flag value.
/// @param zero_flag New zero flag value.
/// @param sign_flag New sign flag value.
/// @param overflow_flag New overflow flag value.
/// @return true when all flag writes succeed.
static bool vm_cpu_apply_arithmetic_flags(
    VmCpu *cpu,
    bool carry_flag,
    bool zero_flag,
    bool sign_flag,
    bool overflow_flag
) {
    if (cpu == NULL) {
        return false;
    }

    return vm_cpu_write_flag(cpu, VM_FLAG_CF, carry_flag)
        && vm_cpu_write_flag(cpu, VM_FLAG_ZF, zero_flag)
        && vm_cpu_write_flag(cpu, VM_FLAG_SF, sign_flag)
        && vm_cpu_write_flag(cpu, VM_FLAG_OF, overflow_flag);
}

void vm_cpu_init(VmCpu *cpu) {
    if (cpu == NULL) {
        return;
    }

    cpu->eax = 0U;
    cpu->ebx = 0U;
    cpu->ecx = 0U;
    cpu->edx = 0U;
    cpu->esi = 0U;
    cpu->edi = 0U;
    cpu->ebp = 0U;
    cpu->esp = 0U;
    cpu->eip = 0U;
    cpu->eflags = 0U;
    vm_cpu_mark_all_flags_valid(cpu);
}

bool vm_cpu_read_register(const VmCpu *cpu, VmRegister reg, uint32_t *out_value) {
    const VmCpuRegisterMetadata *metadata = NULL;
    uint32_t storage_value = 0U;
    uint32_t mask = 0U;

    if (cpu == NULL || out_value == NULL) {
        return false;
    }

    if (!vm_cpu_get_register_metadata(reg, &metadata)) {
        return false;
    }

    if (!vm_cpu_read_storage(cpu, metadata->storage, &storage_value)) {
        return false;
    }

    mask = vm_cpu_width_mask(metadata->width_bits);
    if (mask == 0U) {
        return false;
    }

    *out_value = (storage_value >> metadata->shift_bits) & mask;
    return true;
}

bool vm_cpu_write_register(VmCpu *cpu, VmRegister reg, uint32_t value) {
    const VmCpuRegisterMetadata *metadata = NULL;
    uint32_t storage_value = 0U;
    uint32_t mask = 0U;
    uint32_t shifted_mask = 0U;
    uint32_t shifted_value = 0U;

    if (cpu == NULL) {
        return false;
    }

    if (!vm_cpu_get_register_metadata(reg, &metadata)) {
        return false;
    }

    if (!vm_cpu_read_storage(cpu, metadata->storage, &storage_value)) {
        return false;
    }

    mask = vm_cpu_width_mask(metadata->width_bits);
    if (mask == 0U) {
        return false;
    }

    shifted_mask = mask << metadata->shift_bits;
    shifted_value = (value & mask) << metadata->shift_bits;
    storage_value = (storage_value & ~shifted_mask) | shifted_value;

    if (!vm_cpu_write_storage(cpu, metadata->storage, storage_value)) {
        return false;
    }

    if (metadata->storage == VM_CPU_STORAGE_EFLAGS) {
        vm_cpu_mark_all_flags_valid(cpu);
    }

    return true;
}

uint8_t vm_cpu_register_width_bits(VmRegister reg) {
    const VmCpuRegisterMetadata *metadata = NULL;

    if (!vm_cpu_get_register_metadata(reg, &metadata)) {
        return 0U;
    }

    return metadata->width_bits;
}

const char *vm_cpu_register_name(VmRegister reg) {
    const VmCpuRegisterMetadata *metadata = NULL;

    if (!vm_cpu_get_register_metadata(reg, &metadata)) {
        return NULL;
    }

    return metadata->name;
}

bool vm_cpu_read_flag(const VmCpu *cpu, VmFlag flag, bool *out_is_set) {
    const VmCpuFlagMetadata *metadata = NULL;

    if (cpu == NULL || out_is_set == NULL) {
        return false;
    }

    if (!vm_cpu_get_flag_metadata(flag, &metadata)) {
        return false;
    }

    *out_is_set = (cpu->eflags & metadata->mask) != 0U;
    return true;
}

bool vm_cpu_write_flag(VmCpu *cpu, VmFlag flag, bool is_set) {
    const VmCpuFlagMetadata *metadata = NULL;

    if (cpu == NULL) {
        return false;
    }

    if (!vm_cpu_get_flag_metadata(flag, &metadata)) {
        return false;
    }

    if (is_set) {
        cpu->eflags |= metadata->mask;
    } else {
        cpu->eflags &= ~metadata->mask;
    }

    return vm_cpu_mark_flag_valid(cpu, flag);
}

bool vm_cpu_set_flag(VmCpu *cpu, VmFlag flag) {
    return vm_cpu_write_flag(cpu, flag, true);
}

bool vm_cpu_clear_flag(VmCpu *cpu, VmFlag flag) {
    return vm_cpu_write_flag(cpu, flag, false);
}

bool vm_cpu_read_flag_validity(const VmCpu *cpu, VmFlag flag, VmFlagValidityMetadata *out_metadata) {
    if (cpu == NULL || out_metadata == NULL || !vm_cpu_flag_has_validity_slot(flag)) {
        return false;
    }

    *out_metadata = cpu->flag_validity[(int)flag];
    return true;
}

bool vm_cpu_mark_flag_valid(VmCpu *cpu, VmFlag flag) {
    if (cpu == NULL || !vm_cpu_flag_has_validity_slot(flag)) {
        return false;
    }

    vm_cpu_clear_flag_origin(&cpu->flag_validity[(int)flag]);
    return true;
}

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
) {
    VmFlagValidityMetadata *metadata = NULL;

    if (cpu == NULL || !vm_cpu_flag_has_validity_slot(flag)) {
        return false;
    }

    metadata = &cpu->flag_validity[(int)flag];
    metadata->is_valid = false;
    metadata->undefined_code = undefined_code;
    metadata->producer_mnemonic = producer_mnemonic;
    metadata->producer_source_file = source_file;
    metadata->producer_source_line = source_line;
    metadata->producer_source_column = source_column;
    metadata->producer_byte_offset = byte_offset;
    metadata->producer_span_length = span_length;
    metadata->producer_source_text = source_text;
    metadata->producer_instruction_index = instruction_index;
    return true;
}

bool vm_cpu_update_add_flags(VmCpu *cpu, uint32_t left, uint32_t right, uint8_t width_bits, uint32_t *out_result) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t left_masked = 0U;
    uint32_t right_masked = 0U;
    uint32_t result = 0U;
    uint64_t wide_result = 0U;
    bool carry_flag = false;
    bool zero_flag = false;
    bool sign_flag = false;
    bool overflow_flag = false;

    if (cpu == NULL) {
        return false;
    }

    mask = vm_cpu_width_mask(width_bits);
    sign_bit = vm_cpu_sign_bit_mask(width_bits);
    if (mask == 0U || sign_bit == 0U) {
        return false;
    }

    left_masked = left & mask;
    right_masked = right & mask;
    wide_result = (uint64_t)left_masked + (uint64_t)right_masked;
    result = (uint32_t)wide_result & mask;

    carry_flag = wide_result > (uint64_t)mask;
    zero_flag = result == 0U;
    sign_flag = (result & sign_bit) != 0U;
    overflow_flag = ((~(left_masked ^ right_masked) & (left_masked ^ result) & sign_bit) != 0U);

    if (!vm_cpu_apply_arithmetic_flags(cpu, carry_flag, zero_flag, sign_flag, overflow_flag)) {
        return false;
    }

    if (out_result != NULL) {
        *out_result = result;
    }

    return true;
}

bool vm_cpu_update_sub_flags(VmCpu *cpu, uint32_t left, uint32_t right, uint8_t width_bits, uint32_t *out_result) {
    uint32_t mask = 0U;
    uint32_t sign_bit = 0U;
    uint32_t left_masked = 0U;
    uint32_t right_masked = 0U;
    uint32_t result = 0U;
    bool carry_flag = false;
    bool zero_flag = false;
    bool sign_flag = false;
    bool overflow_flag = false;

    if (cpu == NULL) {
        return false;
    }

    mask = vm_cpu_width_mask(width_bits);
    sign_bit = vm_cpu_sign_bit_mask(width_bits);
    if (mask == 0U || sign_bit == 0U) {
        return false;
    }

    left_masked = left & mask;
    right_masked = right & mask;
    result = (left_masked - right_masked) & mask;

    carry_flag = left_masked < right_masked;
    zero_flag = result == 0U;
    sign_flag = (result & sign_bit) != 0U;
    overflow_flag = (((left_masked ^ right_masked) & (left_masked ^ result) & sign_bit) != 0U);

    if (!vm_cpu_apply_arithmetic_flags(cpu, carry_flag, zero_flag, sign_flag, overflow_flag)) {
        return false;
    }

    if (out_result != NULL) {
        *out_result = result;
    }

    return true;
}

bool vm_cpu_update_cmp_flags(VmCpu *cpu, uint32_t left, uint32_t right, uint8_t width_bits) {
    return vm_cpu_update_sub_flags(cpu, left, right, width_bits, NULL);
}
