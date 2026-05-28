/*
 * @file symbols.c
 * @brief Helpers for implemented .data symbols.
 *
 * The parser uses this module to keep data-symbol name handling and data-type
 * metadata consistent while data layout remains intentionally small and static.
 */

#include "symbols.h"

#include <string.h>

/// Returns the stable display name for a symbol storage section.
const char *vm_symbol_section_name(VmSymbolSection section) {
    switch (section) {
        case VM_SYMBOL_SECTION_DATA:
            return ".data";
        case VM_SYMBOL_SECTION_DATA_UNINITIALIZED:
            return ".DATA?";
        case VM_SYMBOL_SECTION_CONST:
            return ".CONST";
        default:
            return NULL;
    }
}

/// Returns whether a symbol is stored in read-only `.CONST` storage.
bool vm_symbol_is_read_only(const VmSymbol *symbol) {
    return symbol != NULL && symbol->section == VM_SYMBOL_SECTION_CONST;
}

/// Returns whether a symbol originated in accepted uninitialized-origin storage.
bool vm_symbol_is_uninitialized_storage(const VmSymbol *symbol) {
    return symbol != NULL && symbol->has_uninitialized_storage;
}

/// Converts an ASCII byte to uppercase without depending on locale.
///
/// @param ch Source byte to convert.
/// @return Uppercase ASCII byte, or the original byte when not lowercase ASCII.
static char vm_symbol_ascii_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }

    return ch;
}

/// Compares a source slice with a literal using ASCII case-insensitive matching.
///
/// @param text Source slice to compare.
/// @param length Number of bytes in @p text.
/// @param literal Null-terminated literal string.
/// @return true when the values match ignoring ASCII case.
static bool vm_symbol_slice_equals(const char *text, size_t length, const char *literal) {
    size_t index = 0U;

    if (text == NULL || literal == NULL) {
        return false;
    }

    while (index < length && literal[index] != '\0') {
        if (vm_symbol_ascii_upper(text[index]) != vm_symbol_ascii_upper(literal[index])) {
            return false;
        }
        index += 1U;
    }

    return index == length && literal[index] == '\0';
}

uint8_t vm_symbol_data_type_size_bytes(VmSymbolDataType data_type) {
    switch (data_type) {
        case VM_SYMBOL_DATA_TYPE_BYTE:
            return 1U;
        case VM_SYMBOL_DATA_TYPE_WORD:
            return 2U;
        case VM_SYMBOL_DATA_TYPE_DWORD:
            return 4U;
        case VM_SYMBOL_DATA_TYPE_QWORD:
        case VM_SYMBOL_DATA_TYPE_SQWORD:
            return 8U;
        case VM_SYMBOL_DATA_TYPE_SBYTE:
            return 1U;
        case VM_SYMBOL_DATA_TYPE_SWORD:
            return 2U;
        case VM_SYMBOL_DATA_TYPE_SDWORD:
            return 4U;
        default:
            return 0U;
    }
}

const char *vm_symbol_data_type_name(VmSymbolDataType data_type) {
    switch (data_type) {
        case VM_SYMBOL_DATA_TYPE_BYTE:
            return "BYTE";
        case VM_SYMBOL_DATA_TYPE_WORD:
            return "WORD";
        case VM_SYMBOL_DATA_TYPE_DWORD:
            return "DWORD";
        case VM_SYMBOL_DATA_TYPE_QWORD:
            return "QWORD";
        case VM_SYMBOL_DATA_TYPE_SBYTE:
            return "SBYTE";
        case VM_SYMBOL_DATA_TYPE_SWORD:
            return "SWORD";
        case VM_SYMBOL_DATA_TYPE_SDWORD:
            return "SDWORD";
        case VM_SYMBOL_DATA_TYPE_SQWORD:
            return "SQWORD";
        default:
            return NULL;
    }
}

bool vm_symbol_parse_data_type(const char *text, size_t length, VmSymbolDataType *out_data_type) {
    if (text == NULL || out_data_type == NULL) {
        return false;
    }

    if (vm_symbol_slice_equals(text, length, "BYTE") || vm_symbol_slice_equals(text, length, "DB")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_BYTE;
        return true;
    }
    if (vm_symbol_slice_equals(text, length, "WORD") || vm_symbol_slice_equals(text, length, "DW")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_WORD;
        return true;
    }
    if (vm_symbol_slice_equals(text, length, "DWORD") || vm_symbol_slice_equals(text, length, "DD")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_DWORD;
        return true;
    }
    if (vm_symbol_slice_equals(text, length, "QWORD") || vm_symbol_slice_equals(text, length, "DQ")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_QWORD;
        return true;
    }
    if (vm_symbol_slice_equals(text, length, "SBYTE")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_SBYTE;
        return true;
    }
    if (vm_symbol_slice_equals(text, length, "SWORD")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_SWORD;
        return true;
    }
    if (vm_symbol_slice_equals(text, length, "SDWORD")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_SDWORD;
        return true;
    }
    if (vm_symbol_slice_equals(text, length, "SQWORD")) {
        *out_data_type = VM_SYMBOL_DATA_TYPE_SQWORD;
        return true;
    }

    return false;
}

bool vm_symbol_data_type_is_signed(VmSymbolDataType data_type) {
    return data_type == VM_SYMBOL_DATA_TYPE_SBYTE ||
           data_type == VM_SYMBOL_DATA_TYPE_SWORD ||
           data_type == VM_SYMBOL_DATA_TYPE_SDWORD ||
           data_type == VM_SYMBOL_DATA_TYPE_SQWORD;
}

bool vm_symbol_set_name(VmSymbol *symbol, const char *text, size_t length) {
    if (symbol == NULL || text == NULL || length == 0U || length >= (size_t)VM_SYMBOL_NAME_CAPACITY) {
        return false;
    }

    memcpy(symbol->name, text, length);
    symbol->name[length] = '\0';
    return true;
}

bool vm_symbol_name_equals_exact(const VmSymbol *symbol, const char *text, size_t length) {
    if (symbol == NULL || text == NULL) {
        return false;
    }

    return strlen(symbol->name) == length && memcmp(symbol->name, text, length) == 0;
}

bool vm_symbol_name_equals(const VmSymbol *symbol, const char *text, size_t length) {
    size_t index = 0U;

    if (symbol == NULL || text == NULL) {
        return false;
    }

    while (index < length && symbol->name[index] != '\0') {
        if (vm_symbol_ascii_upper(symbol->name[index]) != vm_symbol_ascii_upper(text[index])) {
            return false;
        }
        index += 1U;
    }

    return index == length && symbol->name[index] == '\0';
}

bool vm_symbol_name_equals_with_policy(const VmSymbol *symbol, const char *text, size_t length, VmSymbolCasePolicy policy) {
    if (policy == VM_SYMBOL_CASE_POLICY_NONE) {
        return vm_symbol_name_equals_exact(symbol, text, length);
    }

    return vm_symbol_name_equals(symbol, text, length);
}

const VmSymbol *vm_symbol_find_by_name(const VmSymbol *symbols, size_t symbol_count, const char *text, size_t length) {
    return vm_symbol_find_by_name_with_policy(symbols, symbol_count, text, length, VM_SYMBOL_CASE_POLICY_ALL, NULL);
}

const VmSymbol *vm_symbol_find_by_name_with_policy(
    const VmSymbol *symbols,
    size_t symbol_count,
    const char *text,
    size_t length,
    VmSymbolCasePolicy policy,
    VmSymbolLookupStatus *out_status
) {
    size_t index = 0U;
    const VmSymbol *match = NULL;
    size_t match_count = 0U;

    if (out_status != NULL) {
        *out_status = VM_SYMBOL_LOOKUP_NOT_FOUND;
    }
    if (symbols == NULL || text == NULL || length == 0U) {
        return NULL;
    }

    for (index = 0U; index < symbol_count; index += 1U) {
        if (vm_symbol_name_equals_with_policy(&symbols[index], text, length, policy)) {
            match = &symbols[index];
            match_count += 1U;
        }
    }

    if (match_count == 1U) {
        if (out_status != NULL) {
            *out_status = VM_SYMBOL_LOOKUP_FOUND;
        }
        return match;
    }
    if (match_count > 1U) {
        if (out_status != NULL) {
            *out_status = VM_SYMBOL_LOOKUP_AMBIGUOUS;
        }
        return NULL;
    }

    return NULL;
}

const VmSymbol *vm_symbol_find_by_address(const VmSymbol *symbols, size_t symbol_count, uint32_t address) {
    size_t index = 0U;

    if (symbols == NULL) {
        return NULL;
    }

    for (index = 0U; index < symbol_count; index += 1U) {
        const VmSymbol *symbol = &symbols[index];
        if (address >= symbol->address && address < symbol->address + symbol->size_bytes) {
            return symbol;
        }
    }

    return NULL;
}
