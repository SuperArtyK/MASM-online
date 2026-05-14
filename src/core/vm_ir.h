/*
 * @file vm_ir.h
 * @brief Minimal internal IR instruction and operand types for Milestone 4.
 *
 * This module defines the smallest executable instruction representation used
 * by the MASM32 educational simulator core before a parser exists. Each IR
 * instruction preserves source metadata so later diagnostics and debugger
 * panels can report where hardcoded or parsed instructions originated.
 */

#ifndef MASM32_SIM_VM_IR_H
#define MASM32_SIM_VM_IR_H

#include <stdbool.h>
#include <stdint.h>

#include "vm_cpu.h"

/// Identifies the minimal Milestone 4 IR operation code.
typedef enum VmIrOpcode {
    /// Move a value from source operand to destination operand.
    VM_IR_OPCODE_MOV = 0,
    /// Add source operand to destination operand and update arithmetic flags.
    VM_IR_OPCODE_ADD,
    /// Subtract source operand from destination operand and update arithmetic flags.
    VM_IR_OPCODE_SUB,
    /// Number of supported Milestone 4 operation codes.
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
    VM_IR_OPERAND_MEMORY_ADDRESS
} VmIrOperandKind;

/// Describes one minimal IR operand.
typedef struct VmIrOperand {
    /// Operand representation kind.
    VmIrOperandKind kind;
    /// Operand width in bits when known; valid execution widths are 8, 16, and 32.
    uint8_t width_bits;
    /// Immediate value used when @ref kind is VM_IR_OPERAND_IMMEDIATE.
    uint32_t immediate;
    /// Register identifier used when @ref kind is VM_IR_OPERAND_REGISTER.
    VmRegister reg;
    /// Absolute simulated address used when @ref kind is VM_IR_OPERAND_MEMORY_ADDRESS.
    uint32_t address;
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

/// Returns whether an operand width is supported by Milestone 4 execution.
///
/// @param width_bits Width in bits to inspect.
/// @return true for 8, 16, and 32 bits.
bool vm_ir_width_is_supported(uint8_t width_bits);

#endif
