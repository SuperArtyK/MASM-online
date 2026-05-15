/*
 * @file symbols.h
 * @brief Symbol metadata for MASM32 simulator parser data declarations.
 *
 * This module defines the small symbol model used for .data layout
 * and direct symbolic memory operands. It is intentionally limited to data
 * symbols; label targets and procedure metadata remain later milestones.
 */

#ifndef MASM32_SIM_SYMBOLS_H
#define MASM32_SIM_SYMBOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Maximum bytes retained for one symbol name, including the null terminator.
#define VM_SYMBOL_NAME_CAPACITY 64U

/// Identifies the simplified data section that owns a symbol.
typedef enum VmSymbolSection {
    /// Initialized writable `.data` storage.
    VM_SYMBOL_SECTION_DATA = 0,
    /// Deterministic zero-filled `.DATA?` storage that was originally uninitialized.
    VM_SYMBOL_SECTION_DATA_UNINITIALIZED,
    /// Initialized read-only `.CONST` storage.
    VM_SYMBOL_SECTION_CONST,
    /// Number of supported symbol sections.
    VM_SYMBOL_SECTION_COUNT
} VmSymbolSection;

/// Identifies the scalar storage type of one MASM data declaration.
typedef enum VmSymbolDataType {
    /// 8-bit BYTE or DB data element.
    VM_SYMBOL_DATA_TYPE_BYTE = 0,
    /// 16-bit WORD or DW data element.
    VM_SYMBOL_DATA_TYPE_WORD,
    /// 32-bit DWORD or DD data element.
    VM_SYMBOL_DATA_TYPE_DWORD,
    /// 64-bit QWORD or DQ data element.
    VM_SYMBOL_DATA_TYPE_QWORD,
    /// Signed 8-bit SBYTE data element.
    VM_SYMBOL_DATA_TYPE_SBYTE,
    /// Signed 16-bit SWORD data element.
    VM_SYMBOL_DATA_TYPE_SWORD,
    /// Signed 32-bit SDWORD data element.
    VM_SYMBOL_DATA_TYPE_SDWORD,
    /// Signed 64-bit SQWORD data element.
    VM_SYMBOL_DATA_TYPE_SQWORD,
    /// Number of supported data types.
    VM_SYMBOL_DATA_TYPE_COUNT
} VmSymbolDataType;

/// Describes one data symbol laid out in the simulated .data region.
typedef struct VmSymbol {
    /// Null-terminated symbol name copied from source.
    char name[VM_SYMBOL_NAME_CAPACITY];
    /// Scalar element type declared for the symbol.
    VmSymbolDataType data_type;
    /// Simplified data section that owns the symbol.
    VmSymbolSection section;
    /// Simulated address of the first byte of the symbol.
    uint32_t address;
    /// Total bytes occupied by this symbol.
    uint32_t size_bytes;
    /// Size in bytes of one declared element.
    uint8_t element_size_bytes;
    /// Number of declared elements represented by this symbol.
    uint32_t element_count;
    /// Whether any initializer used MASM's uninitialized marker `?`.
    bool has_uninitialized_initializer;
    /// Whether storage came from `.DATA?` and was originally uninitialized.
    bool has_uninitialized_storage;
} VmSymbol;

/// Returns the stable source spelling for a symbol section.
///
/// @param section Section identifier to inspect.
/// @return Static section name, or NULL for invalid values.
const char *vm_symbol_section_name(VmSymbolSection section);

/// Returns whether a symbol belongs to read-only `.CONST` storage.
///
/// @param symbol Symbol to inspect.
/// @return true when @p symbol is non-NULL and read-only.
bool vm_symbol_is_read_only(const VmSymbol *symbol);

/// Returns whether a symbol came from originally uninitialized `.DATA?` storage.
///
/// @param symbol Symbol to inspect.
/// @return true when @p symbol is non-NULL and came from `.DATA?`.
bool vm_symbol_is_uninitialized_storage(const VmSymbol *symbol);

/// Returns the byte width of a supported data type.
///
/// @param data_type Data type to inspect.
/// @return Element size in bytes, or zero for invalid values.
uint8_t vm_symbol_data_type_size_bytes(VmSymbolDataType data_type);

/// Returns the canonical MASM spelling for a supported data type.
///
/// @param data_type Data type to inspect.
/// @return Static type name, or NULL for invalid values.
const char *vm_symbol_data_type_name(VmSymbolDataType data_type);

/// Parses a MASM data type token spelling.
///
/// @param text Source spelling of the type token.
/// @param length Number of bytes in @p text.
/// @param out_data_type Receives the parsed type on success.
/// @return true when the spelling is a supported integer data declaration type.
bool vm_symbol_parse_data_type(const char *text, size_t length, VmSymbolDataType *out_data_type);

/// Returns whether a data type validates numeric initializers as signed.
///
/// @param data_type Data type to inspect.
/// @return true for SBYTE, SWORD, SDWORD, and SQWORD.
bool vm_symbol_data_type_is_signed(VmSymbolDataType data_type);

/// Copies a source symbol name into a fixed symbol slot.
///
/// @param symbol Destination symbol to mutate.
/// @param text Source symbol-name bytes.
/// @param length Number of bytes in @p text.
/// @return true when the name fit and was copied.
bool vm_symbol_set_name(VmSymbol *symbol, const char *text, size_t length);

/// Compares a symbol name with a source slice using ASCII case-insensitive matching.
///
/// @param symbol Symbol whose name should be compared.
/// @param text Source slice to compare.
/// @param length Number of bytes in @p text.
/// @return true when the names match case-insensitively.
bool vm_symbol_name_equals(const VmSymbol *symbol, const char *text, size_t length);

/// Finds a data symbol by case-insensitive name.
///
/// @param symbols Symbol array to inspect.
/// @param symbol_count Number of valid entries in @p symbols.
/// @param text Source name slice to find.
/// @param length Number of bytes in @p text.
/// @return Matching symbol, or NULL when none exists.
const VmSymbol *vm_symbol_find_by_name(const VmSymbol *symbols, size_t symbol_count, const char *text, size_t length);

/// Finds the symbol whose byte range contains an address.
///
/// @param symbols Symbol array to inspect.
/// @param symbol_count Number of valid entries in @p symbols.
/// @param address Simulated address to resolve.
/// @return Containing symbol, or NULL when none exists.
const VmSymbol *vm_symbol_find_by_address(const VmSymbol *symbols, size_t symbol_count, uint32_t address);

#endif
