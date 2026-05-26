/*
 * @file vm_ir.h
 * @brief Internal IR instruction and operand types for the MASM32 simulator.
 *
 * This module defines the executable instruction representation used by the
 * MASM32 educational simulator core. Each IR instruction preserves source
 * metadata so diagnostics and debugger panels can report where hardcoded or
 * parsed instructions originated.
 */

#ifndef MASM32_SIM_VM_IR_H
#define MASM32_SIM_VM_IR_H

#include <stdbool.h>
#include <stdint.h>

#include "vm_cpu.h"

/// Identifies the currently implemented IR operation code.
typedef enum VmIrOpcode {
    /// Move a value from source operand to destination operand.
    VM_IR_OPCODE_MOV = 0,
    /// Add source operand to destination operand and update arithmetic flags.
    VM_IR_OPCODE_ADD,
    /// Subtract source operand from destination operand and update arithmetic flags.
    VM_IR_OPCODE_SUB,
    /// Sign-extend an 8-bit or 16-bit source operand into a wider register destination.
    VM_IR_OPCODE_MOVSX,
    /// Zero-extend an 8-bit or 16-bit source operand into a wider register destination.
    VM_IR_OPCODE_MOVZX,
    /// Sign-extend AL into AX.
    VM_IR_OPCODE_CBW,
    /// Sign-extend AX into EAX.
    VM_IR_OPCODE_CWDE,
    /// Sign-extend AX into DX:AX.
    VM_IR_OPCODE_CWD,
    /// Sign-extend EAX into EDX:EAX.
    VM_IR_OPCODE_CDQ,
    /// Exchange the values of two register or memory/register operands.
    VM_IR_OPCODE_XCHG,
    /// Compute the two's-complement negation of a register or memory operand.
    VM_IR_OPCODE_NEG,
    /// Advance execution without mutating VM state.
    VM_IR_OPCODE_NOP,
    /// Add source plus carry flag to destination and update arithmetic flags.
    VM_IR_OPCODE_ADC,
    /// Subtract source plus carry flag from destination and update arithmetic flags.
    VM_IR_OPCODE_SBB,
    /// Clear the carry flag without mutating other modeled flags.
    VM_IR_OPCODE_CLC,
    /// Set the carry flag without mutating other modeled flags.
    VM_IR_OPCODE_STC,
    /// Complement the carry flag without mutating other modeled flags.
    VM_IR_OPCODE_CMC,
    /// Compute bitwise AND for flags only without storing the result.
    VM_IR_OPCODE_TEST,
    /// Increment a register or memory destination and preserve CF.
    VM_IR_OPCODE_INC,
    /// Decrement a register or memory destination and preserve CF.
    VM_IR_OPCODE_DEC,
    /// Compute bitwise AND into a register or memory destination.
    VM_IR_OPCODE_AND,
    /// Compute bitwise OR into a register or memory destination.
    VM_IR_OPCODE_OR,
    /// Compute bitwise XOR into a register or memory destination.
    VM_IR_OPCODE_XOR,
    /// Compute the bitwise complement of a register or memory destination without changing flags.
    VM_IR_OPCODE_NOT,
    /// Shift a register or memory destination left, filling low bits with zero.
    VM_IR_OPCODE_SHL,
    /// Arithmetic-left-shift alias for SHL.
    VM_IR_OPCODE_SAL,
    /// Shift a register or memory destination right, filling high bits with zero.
    VM_IR_OPCODE_SHR,
    /// Shift a register or memory destination right, filling high bits with the original sign bit.
    VM_IR_OPCODE_SAR,
    /// Rotate a register or memory destination left within its selected width.
    VM_IR_OPCODE_ROL,
    /// Rotate a register or memory destination right within its selected width.
    VM_IR_OPCODE_ROR,
    /// Multiply the implicit unsigned accumulator by a register or memory source.
    VM_IR_OPCODE_MUL,
    /// Multiply the implicit signed accumulator, or multiply an explicit 16/32-bit register destination by a register or memory source.
    VM_IR_OPCODE_IMUL,
    /// Multiply an explicit 16/32-bit register destination by a register or memory source and signed immediate.
    VM_IR_OPCODE_IMUL_IMMEDIATE,
    /// Divide the implicit unsigned accumulator by a register or memory source.
    VM_IR_OPCODE_DIV,
    /// Divide the implicit signed accumulator by a register or memory source.
    VM_IR_OPCODE_IDIV,
    /// Compute an effective address into a 32-bit register without reading memory.
    VM_IR_OPCODE_LEA,
    /// Terminate execution successfully for Irvine32 `exit`.
    VM_IR_OPCODE_EXIT,
    /// Number of currently supported operation codes.
    VM_IR_OPCODE_COUNT
} VmIrOpcode;

/// Identifies the representation used by one IR operand.
typedef enum VmIrOperandKind {
    /// No operand is present.
    VM_IR_OPERAND_NONE = 0,
    /// Immediate unsigned integer operand.
    VM_IR_OPERAND_IMMEDIATE,
    /// CPU register or register alias operand.
    VM_IR_OPERAND_REGISTER,
    /// Absolute simulated memory address operand.
    VM_IR_OPERAND_MEMORY_ADDRESS,
    /// Runtime memory address computed from an optional static base, a base register, and displacement.
    VM_IR_OPERAND_MEMORY_REGISTER
} VmIrOperandKind;


/// Identifies the section base that should relocate an address-valued IR operand.
typedef enum VmIrRelocationKind {
    /// Operand contains no relocatable address and must be interpreted literally.
    VM_IR_RELOCATION_NONE = 0,
    /// Operand contains an address relative to the writable `.data` / `.DATA?` region.
    VM_IR_RELOCATION_DATA,
    /// Operand contains an address relative to the read-only `.CONST` region.
    VM_IR_RELOCATION_CONST
} VmIrRelocationKind;

/// Describes one minimal IR operand.
typedef struct VmIrOperand {
    /// Operand representation kind.
    VmIrOperandKind kind;
    /// Operand width in bits when known; valid execution widths are 8, 16, and 32.
    uint8_t width_bits;
    /// Immediate value for VM_IR_OPERAND_IMMEDIATE, or signed displacement bits for VM_IR_OPERAND_MEMORY_REGISTER.
    uint32_t immediate;
    /// Register identifier used when @ref kind is VM_IR_OPERAND_REGISTER or VM_IR_OPERAND_MEMORY_REGISTER.
    VmRegister reg;
    /// Absolute address for VM_IR_OPERAND_MEMORY_ADDRESS, or static base address for VM_IR_OPERAND_MEMORY_REGISTER.
    uint32_t address;
    /// Relocation section for address-valued operands that came from data symbols or OFFSET.
    VmIrRelocationKind relocation;
} VmIrOperand;

/// Describes one minimal IR instruction with source metadata.
typedef struct VmIrInstruction {
    /// Operation code to execute.
    VmIrOpcode opcode;
    /// Destination operand.
    VmIrOperand destination;
    /// Source operand.
    VmIrOperand source;
    /// Source file name when known, or NULL for generated instructions.
    const char *source_file;
    /// One-based source line number when known, or zero for generated instructions.
    uint32_t source_line;
    /// Original source text when known, or NULL for generated instructions.
    const char *source_text;
    /// VM instruction index within the loaded program.
    uint32_t instruction_index;
} VmIrInstruction;

/// Returns an absent IR operand.
///
/// @return Operand whose kind is VM_IR_OPERAND_NONE.
VmIrOperand vm_ir_operand_none(void);

/// Returns an immediate IR operand.
///
/// @param value Immediate value before execution-time masking.
/// @param width_bits Intended operand width in bits.
/// @return Immediate operand descriptor.
VmIrOperand vm_ir_operand_immediate(uint32_t value, uint8_t width_bits);

/// Returns a register IR operand.
///
/// If @p width_bits is zero, the executor derives the width from @p reg.
///
/// @param reg Register or alias used by the operand.
/// @param width_bits Optional operand width in bits.
/// @return Register operand descriptor.
VmIrOperand vm_ir_operand_register(VmRegister reg, uint8_t width_bits);

/// Returns an absolute memory-address IR operand.
///
/// @param address Simulated memory address.
/// @param width_bits Operand width in bits.
/// @return Memory-address operand descriptor.
VmIrOperand vm_ir_operand_memory(uint32_t address, uint8_t width_bits);

/// Returns a runtime register-indirect memory operand.
///
/// The effective address is computed during execution as static base address
/// plus the current base register value plus signed byte displacement. LEA may
/// use VM_REGISTER_COUNT when no runtime register contributes to the address.
///
/// @param base_register Register that contributes the runtime byte address or offset, or VM_REGISTER_COUNT for none.
/// @param displacement Signed byte displacement added to the runtime address.
/// @param static_address Static base address, normally zero or a data-symbol base.
/// @param width_bits Operand width in bits, or zero when parser validation will infer it.
/// @return Register-indirect memory operand descriptor.
VmIrOperand vm_ir_operand_memory_register(VmRegister base_register, int32_t displacement, uint32_t static_address, uint8_t width_bits);

/// Returns a copy of an operand marked with address relocation metadata.
///
/// The relocation marker is consumed by loader/source-run code before execution
/// when a non-fixed memory layout is selected. Literal immediates keep
/// VM_IR_RELOCATION_NONE so hardcoded addresses remain hardcoded.
///
/// @param operand Operand to copy and mark.
/// @param relocation Relocation section for the address-valued operand.
/// @return Operand copy with relocation metadata applied.
VmIrOperand vm_ir_operand_with_relocation(VmIrOperand operand, VmIrRelocationKind relocation);

/// Returns an IR instruction with preserved source metadata.
///
/// @param opcode Operation code to execute.
/// @param destination Destination operand.
/// @param source Source operand.
/// @param source_file Source file name when known.
/// @param source_line One-based source line number when known.
/// @param source_text Original source text when known.
/// @param instruction_index VM instruction index.
/// @return IR instruction descriptor.
VmIrInstruction vm_ir_instruction(
    VmIrOpcode opcode,
    VmIrOperand destination,
    VmIrOperand source,
    const char *source_file,
    uint32_t source_line,
    const char *source_text,
    uint32_t instruction_index
);

/// Returns a stable lowercase display name for an IR opcode.
///
/// @param opcode Operation code to inspect.
/// @return Static opcode name, or NULL for invalid values.
const char *vm_ir_opcode_name(VmIrOpcode opcode);

/// Returns whether an operand width is supported by the current MASM32 execution subset.
///
/// @param width_bits Width in bits to inspect.
/// @return true for 8, 16, and 32 bits.
bool vm_ir_width_is_supported(uint8_t width_bits);

#endif
