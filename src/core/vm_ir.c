/*
 * @file vm_ir.c
 * @brief Constructors and metadata helpers for implemented IR operands.
 *
 * The helpers in this file keep hardcoded IR program construction and parser
 * emission consistent across implemented milestones.
 */

#include "vm_ir.h"

#include <stddef.h>

VmIrOperand vm_ir_operand_none(void) {
    VmIrOperand operand;

    operand.kind = VM_IR_OPERAND_NONE;
    operand.width_bits = 0U;
    operand.immediate = 0U;
    operand.reg = VM_REGISTER_COUNT;
    operand.address = 0U;

    return operand;
}

VmIrOperand vm_ir_operand_immediate(uint32_t value, uint8_t width_bits) {
    VmIrOperand operand = vm_ir_operand_none();

    operand.kind = VM_IR_OPERAND_IMMEDIATE;
    operand.width_bits = width_bits;
    operand.immediate = value;

    return operand;
}

VmIrOperand vm_ir_operand_register(VmRegister reg, uint8_t width_bits) {
    VmIrOperand operand = vm_ir_operand_none();

    operand.kind = VM_IR_OPERAND_REGISTER;
    operand.width_bits = width_bits;
    operand.reg = reg;

    return operand;
}

VmIrOperand vm_ir_operand_memory(uint32_t address, uint8_t width_bits) {
    VmIrOperand operand = vm_ir_operand_none();

    operand.kind = VM_IR_OPERAND_MEMORY_ADDRESS;
    operand.width_bits = width_bits;
    operand.address = address;

    return operand;
}

VmIrOperand vm_ir_operand_memory_register(VmRegister base_register, int32_t displacement, uint32_t static_address, uint8_t width_bits) {
    VmIrOperand operand = vm_ir_operand_none();

    operand.kind = VM_IR_OPERAND_MEMORY_REGISTER;
    operand.width_bits = width_bits;
    operand.immediate = (uint32_t)displacement;
    operand.reg = base_register;
    operand.address = static_address;

    return operand;
}

VmIrInstruction vm_ir_instruction(
    VmIrOpcode opcode,
    VmIrOperand destination,
    VmIrOperand source,
    const char *source_file,
    uint32_t source_line,
    const char *source_text,
    uint32_t instruction_index
) {
    VmIrInstruction instruction;

    instruction.opcode = opcode;
    instruction.destination = destination;
    instruction.source = source;
    instruction.source_file = source_file;
    instruction.source_line = source_line;
    instruction.source_text = source_text;
    instruction.instruction_index = instruction_index;

    return instruction;
}

const char *vm_ir_opcode_name(VmIrOpcode opcode) {
    switch (opcode) {
        case VM_IR_OPCODE_MOV:
            return "mov";
        case VM_IR_OPCODE_ADD:
            return "add";
        case VM_IR_OPCODE_SUB:
            return "sub";
        case VM_IR_OPCODE_MOVSX:
            return "movsx";
        case VM_IR_OPCODE_MOVZX:
            return "movzx";
        case VM_IR_OPCODE_CBW:
            return "cbw";
        case VM_IR_OPCODE_CWDE:
            return "cwde";
        case VM_IR_OPCODE_CWD:
            return "cwd";
        case VM_IR_OPCODE_CDQ:
            return "cdq";
        case VM_IR_OPCODE_XCHG:
            return "xchg";
        case VM_IR_OPCODE_NEG:
            return "neg";
        case VM_IR_OPCODE_NOP:
            return "nop";
        case VM_IR_OPCODE_ADC:
            return "adc";
        case VM_IR_OPCODE_SBB:
            return "sbb";
        case VM_IR_OPCODE_CLC:
            return "clc";
        case VM_IR_OPCODE_STC:
            return "stc";
        case VM_IR_OPCODE_CMC:
            return "cmc";
        default:
            return NULL;
    }
}

bool vm_ir_width_is_supported(uint8_t width_bits) {
    return width_bits == 8U || width_bits == 16U || width_bits == 32U;
}
