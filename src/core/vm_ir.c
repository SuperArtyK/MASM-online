/*
 * @file vm_ir.c
 * @brief Constructors and metadata helpers for implemented IR operands.
 *
 * The helpers in this file keep hardcoded IR program construction and parser
 * emission consistent across implemented milestones.
 */

#include "vm_ir.h"

#include <stddef.h>
#include <string.h>

VmIrOperand vm_ir_operand_none(void) {
    VmIrOperand operand;

    memset(&operand, 0, sizeof(operand));
    operand.kind = VM_IR_OPERAND_NONE;
    operand.reg = VM_REGISTER_COUNT;
    operand.relocation = VM_IR_RELOCATION_NONE;

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

VmIrOperand vm_ir_operand_local_memory(int32_t local_ebp_offset, int32_t displacement, uint8_t width_bits) {
    VmIrOperand operand = vm_ir_operand_none();
    int64_t effective_displacement = (int64_t)local_ebp_offset + (int64_t)displacement;

    operand.kind = VM_IR_OPERAND_MEMORY_REGISTER;
    operand.width_bits = width_bits;
    operand.immediate = (uint32_t)(int32_t)effective_displacement;
    operand.reg = VM_REGISTER_COUNT;
    operand.address = (uint32_t)local_ebp_offset;
    operand.relocation = VM_IR_RELOCATION_LOCAL;

    return operand;
}

VmIrOperand vm_ir_operand_branch_target(uint32_t target_instruction_index) {
    VmIrOperand operand = vm_ir_operand_none();

    operand.kind = VM_IR_OPERAND_BRANCH_TARGET;
    operand.immediate = target_instruction_index;

    return operand;
}


/// Returns an operand copy with relocation metadata applied.
///
/// @param operand Operand to copy.
/// @param relocation Relocation marker to attach.
/// @return Relocatable operand copy.
VmIrOperand vm_ir_operand_with_relocation(VmIrOperand operand, VmIrRelocationKind relocation) {
    operand.relocation = relocation;
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
        case VM_IR_OPCODE_CMP:
            return "cmp";
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
        case VM_IR_OPCODE_TEST:
            return "test";
        case VM_IR_OPCODE_INC:
            return "inc";
        case VM_IR_OPCODE_DEC:
            return "dec";
        case VM_IR_OPCODE_AND:
            return "and";
        case VM_IR_OPCODE_OR:
            return "or";
        case VM_IR_OPCODE_XOR:
            return "xor";
        case VM_IR_OPCODE_NOT:
            return "not";
        case VM_IR_OPCODE_SHL:
            return "shl";
        case VM_IR_OPCODE_SAL:
            return "sal";
        case VM_IR_OPCODE_SHR:
            return "shr";
        case VM_IR_OPCODE_SAR:
            return "sar";
        case VM_IR_OPCODE_ROL:
            return "rol";
        case VM_IR_OPCODE_ROR:
            return "ror";
        case VM_IR_OPCODE_MUL:
            return "mul";
        case VM_IR_OPCODE_IMUL:
            return "imul";
        case VM_IR_OPCODE_IMUL_IMMEDIATE:
            return "imul";
        case VM_IR_OPCODE_DIV:
            return "div";
        case VM_IR_OPCODE_IDIV:
            return "idiv";
        case VM_IR_OPCODE_LEA:
            return "lea";
        case VM_IR_OPCODE_JMP:
            return "jmp";
        case VM_IR_OPCODE_JE:
            return "je";
        case VM_IR_OPCODE_JZ:
            return "jz";
        case VM_IR_OPCODE_JNE:
            return "jne";
        case VM_IR_OPCODE_JNZ:
            return "jnz";
        case VM_IR_OPCODE_JL:
            return "jl";
        case VM_IR_OPCODE_JNGE:
            return "jnge";
        case VM_IR_OPCODE_JLE:
            return "jle";
        case VM_IR_OPCODE_JNG:
            return "jng";
        case VM_IR_OPCODE_JG:
            return "jg";
        case VM_IR_OPCODE_JNLE:
            return "jnle";
        case VM_IR_OPCODE_JGE:
            return "jge";
        case VM_IR_OPCODE_JNL:
            return "jnl";
        case VM_IR_OPCODE_JA:
            return "ja";
        case VM_IR_OPCODE_JNBE:
            return "jnbe";
        case VM_IR_OPCODE_JAE:
            return "jae";
        case VM_IR_OPCODE_JNB:
            return "jnb";
        case VM_IR_OPCODE_JB:
            return "jb";
        case VM_IR_OPCODE_JNAE:
            return "jnae";
        case VM_IR_OPCODE_JBE:
            return "jbe";
        case VM_IR_OPCODE_JNA:
            return "jna";
        case VM_IR_OPCODE_CALL:
            return "call";
        case VM_IR_OPCODE_RET:
            return "ret";
        case VM_IR_OPCODE_PUSH:
            return "push";
        case VM_IR_OPCODE_POP:
            return "pop";
        case VM_IR_OPCODE_LEAVE:
            return "leave";
        case VM_IR_OPCODE_EXIT:
            return "exit";
        case VM_IR_OPCODE_IRVINE32_CRLF:
            return "crlf";
        default:
            return NULL;
    }
}

bool vm_ir_width_is_supported(uint8_t width_bits) {
    return width_bits == 8U || width_bits == 16U || width_bits == 32U;
}
