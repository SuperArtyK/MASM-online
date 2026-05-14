# Online MASM32 Educational Simulator - Incremental Implementation Guide

## 1. Purpose

This guide breaks the simulator into small implementation phases suitable for incremental development with an AI coding assistant.

The goal is to avoid attempting full MASM compatibility at once. Each phase should produce a working, testable improvement.

The guide assumes the final target described in `FULL_IMPLEMENTATION_SPEC_C99_MASM_DIAGNOSTIC_RECOVERY.md`.

## 2. General Development Rules

### 2.0 Core Language Policy

The VM core, parser, executor, memory model, Irvine32 runtime, and Wasm-facing API must be implemented in **C99**.

Rules:

- Use `.c` and `.h` files for core modules.
- Compile native tests and Emscripten builds as C99.
- Do not introduce C++ source files for the core.
- Do not use C++ standard library features, templates, classes, exceptions, RTTI, or `extern "C"` compatibility wrappers.
- Keep API boundaries plain C: structs, enums, explicit status codes, and documented functions.
- Browser UI code may remain JavaScript or TypeScript.
- The polished source editor should use CodeMirror 6 as the selected editor component; the core C99 parser/VM remains the source of truth for MASM semantics.

### 2.1 Build Vertically

Prefer small vertical slices over broad incomplete systems.

A good slice:

```text
Parse `mov eax, 20`
Execute it
Show EAX changed in debugger
Test it
```

A bad slice:

```text
Create 50 parser classes before anything can run
```

### 2.2 Keep the VM Deterministic

The same source, settings, and input should produce the same result.

Random behavior should be seeded.

### 2.3 Centralize Safety Checks

All memory reads and writes must go through the VM memory module.

All console output must go through the console module.

All execution must go through the VM run/step functions.

### 2.4 Preserve Source Locations

Every parsed instruction and generated IR instruction must retain:

- Source file.
- Source line.
- Source column if practical.
- Original source text.

Diagnostics and debugger output depend on this.

### 2.5 Documentation Requirement

Every source file must have a file header block comment.

Every public function, struct, enum, and module-level API must use Doxygen-style triple-slash documentation.

Example file header:

```c
/*
 * @file vm_cpu.c
 * @brief CPU register and flag model for the MASM simulator VM.
 *
 * This module owns canonical register storage and exposes helpers for reading
 * and writing full registers and aliases such as AX, AH, and AL.
 */
```

Example symbol documentation:

```c
/// Writes a value to a register or register alias.
///
/// @param cpu CPU state to mutate.
/// @param reg Register identifier, including aliases such as AL or AX.
/// @param value Value to write. The value is masked to the register width.
/// @return true if the register was valid and the write succeeded.
bool vm_cpu_write_register(VmCpu *cpu, VmRegister reg, uint64_t value);
```

## 3. Recommended Repository Structure

Suggested structure:

```text
/project-root
  /src
    /core
      vm_cpu.c
      vm_cpu.h
      vm_flags.c
      vm_flags.h
      vm_memory.c
      vm_memory.h
      vm_ir.c
      vm_ir.h
      vm_exec.c
      vm_exec.h
      vm_debug.c
      vm_debug.h
      vm_console.c
      vm_console.h
      irvine32.c
      irvine32.h
    /parser
      lexer.c
      lexer.h
      parser.c
      parser.h
      symbols.c
      symbols.h
      data_layout.c
      data_layout.h
    /wasm
      wasm_api.c
      wasm_api.h
  /web
    index.html
    src/main.ts
    src/worker.ts
    src/state.ts
    src/url_state.ts
    src/editor/
      codemirror_setup.ts
      masm_language.ts
      editor_theme.ts
    src/ui/
  /tests
    core/
    parser/
    programs/
  /docs
    FULL_IMPLEMENTATION_SPEC_C99_MASM_DIAGNOSTIC_RECOVERY.md
    INCREMENTAL_IMPLEMENTATION_GUIDE_C99_MASM_DIAGNOSTIC_RECOVERY.md
```

Actual file names may vary, but module boundaries should remain clear.

## 4. Phase 0 - Project Skeleton

### Goal

Create a minimal static web project with a WebAssembly-ready C99 core and a basic browser UI.

### Tasks

1. Create repository structure.
2. Add build system for C99 to WebAssembly using Emscripten.
3. Add basic HTML/CSS/JS or TypeScript frontend.
4. Add Web Worker that loads the Wasm module.
5. Add a simple message protocol between UI and worker.
6. Add placeholder editor, Run button, Program Console, and Simulator Messages panel.
7. Add test runner setup.
8. Add Doxygen comment conventions to initial files.

### Acceptance Criteria

- Page loads locally.
- Worker initializes.
- UI can send a `PING` message to worker.
- Worker replies with `PONG`.
- Wasm module exports at least one test function.
- Tests can be run from command line.

## 5. Phase 1 - Core CPU Register Model

### Goal

Implement canonical register storage and alias read/write behavior.

### Tasks

1. Define CPU state for MASM32 mode.
2. Add canonical registers:
   - `EAX`, `EBX`, `ECX`, `EDX`
   - `ESI`, `EDI`, `EBP`, `ESP`
   - `EIP`
   - `EFLAGS`
3. Add alias support:
   - `AX`, `AH`, `AL`
   - `BX`, `BH`, `BL`
   - `CX`, `CH`, `CL`
   - `DX`, `DH`, `DL`
   - `SI`, `DI`, `BP`, `SP`
4. Implement register read/write helpers.
5. Add unit tests for aliases.

### Acceptance Criteria

- Writing `EAX = 0x12345678` gives:
  - `AX = 0x5678`
  - `AH = 0x56`
  - `AL = 0x78`
- Writing `AL = 0xFF` updates only the low byte of `EAX`.
- Writing `AH = 0xEE` updates bits 8-15 only.
- Register helpers are documented with Doxygen comments.

## 6. Phase 2 - Flags Model

### Goal

Implement initial flags needed for arithmetic and jumps.

### Tasks

1. Define flag bits:
   - `CF`
   - `ZF`
   - `SF`
   - `OF`
2. Add helpers to set, clear, and read flags.
3. Add arithmetic helper functions for common flag updates:
   - addition
   - subtraction
   - comparison
4. Add tests for edge cases.

### Acceptance Criteria

Test cases pass for:

```asm
0xFFFFFFFF + 1
0x7FFFFFFF + 1
0 - 1
5 - 5
```

Expected flags must be verified for `CF`, `ZF`, `SF`, and `OF`.

## 7. Phase 3 - Simulated Memory Regions

### Goal

Implement checked memory access for `.code`, `.data`, heap, and stack regions.

### Tasks

1. Define memory region structure:
   - base address
   - size
   - permissions
   - backing storage
2. Add default layout.
3. Implement checked read/write helpers for:
   - 8-bit
   - 16-bit
   - 32-bit
   - 64-bit storage, even if QWORD execution is later
4. Enforce bounds and permissions.
5. Detect unaligned reads/writes and return warnings while still allowing them.
6. Add memory-change recording hooks.
7. Add tests for valid and invalid access.

### Acceptance Criteria

- Reading/writing valid `.data` memory succeeds.
- Writing `.code` fails.
- Reading invalid addresses fails with structured error.
- Unaligned DWORD read succeeds and emits a warning.
- All memory APIs have Doxygen documentation.

## 8. Phase 4 - Minimal IR and Executor

### Goal

Execute a tiny hardcoded program through internal IR.

### Tasks

1. Define IR instruction structure.
2. Define operand types:
   - immediate
   - register
   - memory address
3. Implement VM step function.
4. Implement initial instructions:
   - `mov`
   - `add`
   - `sub`
5. Add last-step delta capture:
   - changed registers
   - changed flags
   - changed memory
6. Expose one test API from Wasm that runs a hardcoded IR program.

### Acceptance Criteria

Hardcoded program:

```asm
mov eax, 20
add eax, 22
```

produces:

```text
EAX = 42
```

Debugger delta reports:

```text
EAX: 0 -> 20
EAX: 20 -> 42
```

## 9. Phase 5 - Lexer

### Goal

Tokenize a small MASM-like source file.

### Tasks

1. Implement lexer for:
   - identifiers
   - registers
   - numbers, including decimal, hexadecimal, and signed forms
   - signed numeric tokens such as `-42`, `-0x2A`, and `-2Ah`
   - strings
   - quoted character/string literal tokens such as `'A'` and `'AB'`, with final literal semantics enabled in Phase 14
   - commas
   - brackets
   - plus and minus tokens where not part of a numeric literal
   - colons
   - comments beginning with `;`
   - line endings
2. Preserve source positions.
3. Add lexer diagnostics.
4. Add tests.

### Acceptance Criteria

Lexer handles:

```asm
.data
msg BYTE "Hello", 0
.code
mov eax, -1 ; comment
```

with correct token positions and signed numeric metadata.

## 10. Phase 6 - Parser for Minimal Code

### Goal

Parse a tiny `.code` program into IR.

### Tasks

1. Parse `.code` section.
2. Parse labels.
3. Parse simple instructions:
   - register/immediate
   - register/register
4. Parse `END main`.
5. Preserve line information.
6. Report unsupported syntax cleanly.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 20
    add eax, 22
main ENDP
END main
```

runs and produces `EAX = 42`.

## 11. Phase 7 - Basic UI Execution

### Goal

Run a small parsed program from the browser UI.

### Tasks

1. Editor text area.
2. Run button.
3. Worker receives source code.
4. Worker parses and executes.
5. UI displays final register state.
6. UI displays Simulator Messages.

### Acceptance Criteria

A user can paste:

```asm
.code
main PROC
    mov eax, 20
    add eax, 22
main ENDP
END main
```

click Run, and see `EAX = 42`.

## 12. Phase 8 - Data Section and Symbols

### Goal

Support `.data` declarations and symbolic memory references.

### Tasks

1. Parse `.data` section.
2. Support:
   - `BYTE`
   - `WORD`
   - `DWORD`
   - `QWORD`
   - `DB`
   - `DW`
   - `DD`
   - `DQ`
   - comma-separated initializers
   - strings
   - flat `DUP`
   - `?`
   - negative numeric initializers using two's-complement encoding after width validation
3. Build symbol table.
4. Lay out `.data` memory.
5. Support `OFFSET symbol`.
6. Support direct symbol memory writes:
   - `mov var, 100`
7. Add memory-change display by symbol.
8. Reject out-of-range immediates instead of silently truncating them.

### Acceptance Criteria

Program:

```asm
.data
var BYTE 0
.code
main PROC
    mov var, 100
main ENDP
END main
```

shows:

```text
var: 00h / 0 -> 64h / 100
```


## 13. Phase 9 - Constant Symbol Offsets

### Goal

Support constant byte-offset forms for data symbols before control flow is implemented.

This phase makes common textbook MASM array forms executable while preserving MASM byte-offset semantics.

### Tasks

1. Parse symbol-relative constant operands:
   - `nums[8]`
   - `[nums + 8]`
   - `[nums - 4]` where the resulting address remains valid
2. Treat bracketed symbol offsets as byte offsets, not element indexes.
3. Resolve the final absolute address through the existing symbol table.
4. Infer access width from the symbol declaration when no `PTR` override is present.
5. Support both source and destination memory operands where the executor already supports the width.
6. Return symbol-aware memory changes with byte offset and element index when aligned.
7. Preserve source line/column metadata in diagnostics.
8. Add tests for aligned, unaligned, negative, and out-of-bounds symbol offsets.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov nums[8], 100
    mov eax, nums[8]
main ENDP
END main
```

executes successfully and reports:

```text
EAX = 00000064h / 100
Memory Changes:
  nums + 8 DWORD
    byte offset: +8
    element index: 2
    00000000h / 0 -> 00000064h / 100
```

`nums[9]` for a DWORD access should execute as an unaligned access and emit a simulator warning.

## 14. Phase 10 - PTR Width Overrides

### Goal

Support explicit MASM memory-width overrides for memory operands.

### Tasks

1. Parse:
   - `BYTE PTR`
   - `WORD PTR`
   - `DWORD PTR`
   - `QWORD PTR`, as scaffolded layout/Extended-mode support until 64-bit execution is available
2. Apply the explicit width to symbol-relative memory operands.
3. Validate immediate ranges against the explicit width.
4. Ensure explicit width overrides do not silently conflict with unsupported execution widths.
5. Emit structured diagnostics for malformed or unsupported `PTR` forms.
6. Add tests for valid and invalid width overrides.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov BYTE PTR nums[3], 100
    mov DWORD PTR nums[8], 12345678h
main ENDP
END main
```

executes supported writes using the explicit access widths.

Invalid forms such as unsupported `QWORD PTR` execution in MASM32 mode produce structured diagnostics instead of truncation or crashes.

## 15. Phase 11 - Register-Indirect Memory Operands

### Goal

Support simple register-indirect addressing forms used by textbook array and buffer code.

### Tasks

1. Parse register-indirect operands:
   - `[esi]`
   - `[edi]`
   - `[ebx]`
   - `[ebp]`
2. Parse simple displacement forms:
   - `[esi + 4]`
   - `[esi - 4]`
3. Parse simple symbol/register forms where practical:
   - `array[esi]`
   - `[array + esi]`
4. Use `PTR` or known destination/source width to determine access size.
5. Route all reads/writes through the checked memory module.
6. Emit unaligned-access warnings through the existing warning path.
7. Reject unsupported scaled-index forms with explicit diagnostics.
8. Add tests for valid, invalid, unaligned, and out-of-bounds register-indirect accesses.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov esi, OFFSET nums
    mov DWORD PTR [esi + 8], 100
    mov eax, DWORD PTR [esi + 8]
main ENDP
END main
```

executes successfully and sets:

```text
EAX = 00000064h / 100
```

Unsupported scaled forms such as `[esi * 4]` produce explicit unsupported-feature diagnostics.

## 16. Phase 12 - TYPE Operator

### Goal

Implement the `TYPE` operator for data symbols.

### Tasks

1. Parse `TYPE symbol` in immediate contexts.
2. Return the declared element size in bytes.
3. Support `BYTE`, `WORD`, `DWORD`, `QWORD`, and their aliases.
4. Preserve existing `OFFSET symbol` behavior.
5. Add tests for scalar declarations, arrays, strings, and `DUP` declarations.
6. Emit structured diagnostics for unsupported `TYPE` expressions.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov eax, TYPE nums
main ENDP
END main
```

produces:

```text
EAX = 00000004h / 4
```

## 17. Phase 13 - LENGTHOF Operator

### Goal

Implement the `LENGTHOF` operator for data symbols.

### Tasks

1. Parse `LENGTHOF symbol` in immediate contexts.
2. Return the symbol element count.
3. Define and test behavior for scalar declarations, arrays, strings, and flat `DUP` declarations.
4. Preserve source diagnostics for unknown or unsupported symbols.
5. Add tests for byte, word, dword, qword, string, and `DUP` declarations.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
buf  BYTE "Hello", 0
.code
main PROC
    mov eax, LENGTHOF nums
    mov ebx, LENGTHOF buf
main ENDP
END main
```

produces:

```text
EAX = 0000000Ah / 10
EBX = 00000006h / 6
```

## 18. Phase 14 - SIZEOF Operator and Character Literals

### Goal

Implement `SIZEOF` and add single-character plus packed multi-character literals for byte-compatible and wider immediate/data contexts.

### Tasks

1. Parse `SIZEOF symbol` in immediate contexts.
2. Return total symbol byte size.
3. Finalize single-character literals such as `'A'` and `'0'` where a byte-compatible numeric literal is valid.
4. Implement packed multi-character literals such as `'AB'`, `'ABC'`, and `'ABCD'` where the destination width can hold the decoded bytes.
5. Use little-endian integer packing for immediate contexts: the first decoded character is the least significant byte, so `'AB'` becomes `4241h` and `'ABCD'` becomes `44434241h`.
6. Preserve byte-string behavior for `BYTE` / `DB` declarations, so `msg BYTE 'AB', 0` emits `41h, 42h, 00h`.
7. Allow packed scalar initializers for `WORD` / `DW`, `DWORD` / `DD`, and `QWORD` / `DQ` when the decoded byte count fits the element width.
8. Reject empty character literals, literals too wide for the destination width, and unsupported escape forms with structured diagnostics.
9. Add tests for `SIZEOF` with scalar declarations, strings, arrays, and flat `DUP`.
10. Add tests for character literals in data declarations, register immediates, memory immediates, accepted width boundaries, and rejected width-overflow cases.
11. Add diagnostics for unsupported expression forms such as `OFFSET arr + 4` until general expression parsing exists.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
ch   BYTE 'A'
pair WORD 'AB'
tag  DWORD 'ABCD'
.code
main PROC
    mov eax, SIZEOF nums
    mov bl, ch
    mov cx, pair
    mov edx, 'ABCD'
main ENDP
END main
```

produces:

```text
EAX = 00000028h / 40
BL = 41h / 65
CX = 4241h / 16961
EDX = 44434241h / 1145258561
```

`mov al, 'AB'` must fail with a structured assembly diagnostic because two decoded bytes do not fit an 8-bit destination.

## 19. Phase 15 - Textbook MASM Compatibility Backlog Checkpoint

### Goal

Before moving into control flow, explicitly classify common MASM features as implemented now, scheduled soon, or intentionally deferred. This prevents unsupported textbook constructs from appearing as vague syntax errors.

### Tasks

1. Add or update a supported-syntax reference for the current implemented subset.
2. Add explicit unsupported-feature diagnostics for common-but-not-yet-supported constructs:
   - `.DATA?`
   - `.CONST`
   - `EQU` and `TEXTEQU`
   - `STRUCT`, `UNION`, and `RECORD`
   - `INVOKE`, `PROTO`, and `LOCAL`
   - `.IF`, `.WHILE`, `.REPEAT`, `.BREAK`, `.CONTINUE`
   - `MACRO` / `ENDM`
   - `INCLUDELIB`, `EXTERN`, `PUBLIC`, `COMM`
3. Add targeted parser tests that these unsupported constructs produce stable `unsupported-feature` diagnostics instead of confusing parse errors.
4. Note that signed integer data types `SBYTE`, `SWORD`, `SDWORD`, and `SQWORD` are scheduled after the diagnostic-cleanup phases and should not be treated as permanently unsupported.
5. Add backlog notes for additional non-integer data types: `REAL4`, `REAL8`, `REAL10`, `TBYTE`, and `FWORD`.
6. Add backlog notes for expression parser expansion: arithmetic, logical, relational, parenthesized, binary/octal literals, `.RADIX`, `HIGH`, `LOW`, `HIGHWORD`, `LOWWORD`, `SHORT`, and `THIS`.
7. Keep this as a documentation/diagnostics milestone; do not implement the full feature set here.

### Acceptance Criteria

Unsupported but recognizable MASM textbook constructs produce explicit diagnostics such as:

```text
Unsupported feature: STRUCT declarations are not supported yet.
Unsupported feature: INVOKE is not supported yet; use CALL when available.
Unsupported feature: MASM macro definitions are not supported yet.
```

The user-facing supported-syntax reference accurately reflects the current implementation.


## 20. Phase 16 - Diagnostic Plumbing and Lexer Error Surfacing

### Goal

Expose the real lexer and parser diagnostics through source-run and the browser UI instead of collapsing them into broad umbrella errors such as `lexer-failed`.

This phase is a diagnostics-quality milestone. It must not add new MASM syntax or instruction behavior.

### Tasks

1. Preserve all lexer diagnostics when lexing fails.
2. Convert lexer diagnostics into source-run `assembly-error` messages with:
   - diagnostic code,
   - line,
   - column,
   - byte offset where available,
   - source span where available,
   - clear user-facing message.
3. Replace generic messages such as:
   - `lexer-failed line 1, column 1: Lexer failed or produced diagnostics before parsing.`
   with the original lexer diagnostic location and reason.
4. Keep fatal capacity and infrastructure errors distinct:
   - token capacity exhausted,
   - diagnostic capacity exhausted,
   - source-text buffer exhausted,
   - internal parser invariant failure.
5. Surface parser diagnostics through the same structured message path without losing their specific codes.
6. Ensure source-run refuses execution when lexer or parser diagnostics exist.
7. Ensure Simulator Messages displays each surfaced diagnostic as a separate message.
8. Add tests for:
   - invalid character,
   - unterminated string,
   - unterminated character literal,
   - invalid hexadecimal literal,
   - numeric overflow,
   - malformed numeric text such as `123abc`,
   - parser diagnostics that should not be hidden behind generic messages.
9. Preserve existing Phase 15 unsupported-feature diagnostics.
10. Do not implement multi-diagnostic parser recovery in this phase beyond preserving already-produced diagnostics.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0xZZ
main ENDP
END main
```

produces a specific diagnostic similar to:

```text
[assembly-error] invalid-hex-literal line 3, column 14: Invalid hexadecimal literal.
```

Program:

```asm
.data
msg BYTE "Hello
.code
main PROC
END main
```

produces a specific diagnostic similar to:

```text
[assembly-error] unterminated-string line 2, column 10: Unterminated string literal.
```

The UI must not show `lexer-failed` as the only diagnostic when the lexer has already produced a more specific diagnostic.

## 21. Phase 17 - Multi-Diagnostic Unsupported-Feature Recovery

### Goal

Collect multiple recoverable unsupported-feature diagnostics in one pass without executing invalid programs.

This phase improves editor/compiler feedback. It should continue scanning after known unsupported constructs only when the parser can safely resynchronize.

### Tasks

1. Add a parser diagnostic-recovery mode for known unsupported constructs.
2. Continue scanning after recoverable line-level unsupported constructs, such as:
   - `INVOKE`,
   - `PROTO`,
   - `LOCAL`,
   - `EQU`,
   - `TEXTEQU`,
   - `INCLUDELIB`,
   - `EXTERN`,
   - `PUBLIC`,
   - `COMM`.
3. Add construct-aware block recovery for unsupported block-like constructs:
   - `STRUCT` / `ENDS`,
   - `UNION` / `ENDS`,
   - `MACRO` / `ENDM`,
   - `.IF` / `.ENDIF`,
   - `.WHILE` / `.ENDW` where recognized,
   - `.REPEAT` / `.UNTIL` or `.UNTILCXZ` where recognized.
4. Add section-aware recovery for unsupported sections such as:
   - `.DATA?`,
   - `.CONST`.
5. Define a maximum diagnostic count to prevent unbounded error output.
6. Preserve line, column, byte offset, and source span for each recovered diagnostic.
7. Keep fatal errors fatal. Do not attempt recovery after:
   - token capacity exhaustion,
   - diagnostic capacity exhaustion,
   - data image capacity exhaustion,
   - instruction buffer exhaustion,
   - unrecoverable lexer state,
   - internal parser invariant failure.
8. Ensure source-run refuses execution if any assembly diagnostic exists.
9. Add tests where one source file contains several unsupported constructs and the result reports all safely recoverable diagnostics.
10. Avoid noisy cascaded diagnostics from inside skipped unsupported blocks.

### Acceptance Criteria

Program:

```asm
.data
x DWORD 0

MyStruct STRUCT
    a DWORD ?
MyStruct ENDS

.code
main PROC
    INVOKE SomeProc
    .IF eax == 0
        mov ebx, 1
    .ENDIF
main ENDP
END main
```

reports multiple diagnostics, for example:

```text
unsupported-struct at line 4
unsupported-invoke at line 10
unsupported-high-level-if at line 11
```

No execution occurs.

The parser should not emit misleading extra diagnostics for the skipped body of `STRUCT`, `MACRO`, or high-level flow blocks.

## 22. Phase 18 - Signed Integer Data Types

### Goal

Implement textbook signed integer data declarations without enabling executable 64-bit operations in MASM32 Educational Mode.

### Tasks

1. Add parser/data-layout support for:
   - `SBYTE`
   - `SWORD`
   - `SDWORD`
   - `SQWORD`
2. Define storage sizes:
   - `SBYTE` = 1 byte
   - `SWORD` = 2 bytes
   - `SDWORD` = 4 bytes
   - `SQWORD` = 8 bytes
3. Validate initializers against signed ranges:
   - `SBYTE`: -128..127
   - `SWORD`: -32768..32767
   - `SDWORD`: -2147483648..2147483647
   - `SQWORD`: -9223372036854775808..9223372036854775807
4. Encode accepted negative values using two's-complement little-endian storage.
5. Support comma-separated initializers, flat `DUP`, `?`, and packed character literals where the decoded literal fits the signed declaration width.
6. Integrate signed types with existing symbol metadata so `TYPE`, `LENGTHOF`, and `SIZEOF` work correctly.
7. Keep `QWORD` and `SQWORD` data declarations, layout, and metadata available in MASM32 Educational Mode.
8. Continue rejecting executable `QWORD PTR` and `SQWORD PTR` memory operations in MASM32 Educational Mode until Extended 32-bit Mode enables them.
9. Do not add automatic sign-extension on ordinary `mov` from signed memory. Sign-extension instructions such as `movsx` are a later feature.
10. Add tests for valid initializers, range failures, metadata operators, `DUP`, packed literals, and unsupported executable 64-bit operations.

### Acceptance Criteria

Program:

```asm
.data
sb SBYTE -1
sw SWORD -2
sd SDWORD -3
sq SQWORD -4
arr SWORD 3 DUP(-1)
.code
main PROC
    mov eax, TYPE sq
    mov ebx, LENGTHOF arr
    mov ecx, SIZEOF arr
main ENDP
END main
```

produces:

```text
EAX = 00000008h / 8
EBX = 00000003h / 3
ECX = 00000006h / 6
```

`SBYTE 127` succeeds, `SBYTE 128` fails, `SBYTE -128` succeeds, and `SBYTE -129` fails.

`mov eax, sb` must not automatically sign-extend `SBYTE` storage. Ordinary `mov` uses the resolved operand width; explicit sign-extension instructions are added later.

## 23. Phase 19 - Sign and Zero Extension Instructions

### Goal

Implement the sign-extension and zero-extension instructions needed for signed integer data declarations and common textbook MASM programs.

### Tasks

1. Add parser, IR, and executor support for:
   - `movsx`
   - `movzx`
   - `cbw`
   - `cwde`
   - `cwd`
   - `cdq`
2. Support `movsx` and `movzx` with register destinations and 8-bit or 16-bit register/memory sources where the destination is wider than the source.
3. Implement accumulator conversion instructions:
   - `cbw`: sign-extend `AL` into `AX`
   - `cwde`: sign-extend `AX` into `EAX`
   - `cwd`: sign-extend `AX` into `DX:AX`
   - `cdq`: sign-extend `EAX` into `EDX:EAX`
4. Ensure ordinary `mov` from signed memory still does not sign-extend automatically.
5. Preserve source metadata and structured diagnostics for invalid widths and malformed operands.
6. Add tests for signed data symbols, unsigned byte data, register aliases, memory sources, and invalid destination/source widths.

### Acceptance Criteria

Program:

```asm
.data
x SBYTE -1
y BYTE  0FFh
.code
main PROC
    movsx eax, x
    movzx ebx, y
main ENDP
END main
```

produces:

```text
EAX = FFFFFFFFh / 4294967295
EBX = 000000FFh / 255
```

Additional tests verify `cbw`, `cwde`, `cwd`, and `cdq` using negative accumulator values.

## 24. Phase 20 - Exchange, Negation, and NOP

### Goal

Implement small but common utility instructions used in introductory assembly examples and debugger-oriented programs.

### Tasks

1. Add parser, IR, and executor support for:
   - `xchg`
   - `neg`
   - `nop`
2. Support `xchg reg, reg`, `xchg reg, mem`, and `xchg mem, reg` where operand widths match and existing memory rules make the memory access unambiguous.
3. Implement `neg` for register and memory destinations using arithmetic-width-aware flag behavior.
4. Define `nop` as an instruction that advances execution but does not mutate registers, flags, memory, or console state.
5. Ensure `xchg` does not modify flags.
6. Add tests for register exchange, memory exchange, negation edge cases, flag behavior, and no-op last-step deltas.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 5
    mov ebx, 10
    xchg eax, ebx
    neg eax
    nop
main ENDP
END main
```

leaves:

```text
EAX = FFFFFFF6h / 4294967286
EBX = 00000005h / 5
```

Tests verify that `xchg` and `nop` do not modify tracked flags.

## 25. Phase 21 - Carry/Borrow Arithmetic and Carry Flag Control

### Goal

Implement carry-aware arithmetic and explicit carry flag manipulation for multi-word arithmetic examples.

### Tasks

1. Add parser, IR, and executor support for:
   - `adc`
   - `sbb`
   - `clc`
   - `stc`
   - `cmc`
2. Support `adc` and `sbb` for register destinations and memory destinations where existing width rules are unambiguous.
3. Support register, immediate, and memory sources where compatible with the destination width.
4. Use the current `CF` as an input to `adc` and `sbb`.
5. Update `CF`, `ZF`, `SF`, and `OF` according to the supported operand width.
6. Implement `clc`, `stc`, and `cmc` as carry-flag-only instructions.
7. Add tests for carry propagation, borrow propagation, flag-control instructions, immediate ranges, and memory operands.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0FFFFFFFFh
    add eax, 1
    mov ebx, 0
    adc ebx, 0
main ENDP
END main
```

leaves:

```text
EAX = 00000000h / 0
EBX = 00000001h / 1
```

Separate tests verify `stc`, `clc`, and `cmc` behavior.

## 26. Phase 22 - TEST Instruction

### Goal

Implement `test`, a common flag-setting instruction used before conditional jumps.

### Tasks

1. Add parser, IR, and executor support for `test`.
2. Support register/register, register/immediate, register/memory, memory/register, and memory/immediate forms where existing width rules are unambiguous.
3. Compute bitwise AND for flag updates only; do not store the result.
4. Update `ZF` and `SF`.
5. Clear `CF` and `OF`.
6. Preserve source metadata and emit structured diagnostics for invalid widths or malformed operands.
7. Add tests proving that destination operands are not modified.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0
    test eax, eax
main ENDP
END main
```

executes successfully and sets:

```text
ZF = 1
```

A separate test verifies that `test eax, 0FFh` does not change `EAX`.

## 27. Phase 23 - INC and DEC

### Goal

Implement single-operand increment and decrement instructions before control-flow milestones rely on common counter-update patterns.

### Tasks

1. Add parser and IR opcode support for:
   - `inc`
   - `dec`
2. Support register destinations for 8-bit, 16-bit, and 32-bit operands.
3. Support memory destinations where the existing memory-operand and width-inference rules make the access unambiguous.
4. Update flags according to x86-style behavior for the supported widths:
   - update `ZF`
   - update `SF`
   - update `OF`
   - do not modify `CF`
5. Preserve source line, source text, and operand metadata in diagnostics and last-step deltas.
6. Add tests for register operands, memory operands, overflow/underflow edge cases, and `CF` preservation.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0
    inc eax
    dec eax
main ENDP
END main
```

executes successfully and leaves:

```text
EAX = 00000000h / 0
ZF = 1
```

A test verifies that `inc` and `dec` do not modify `CF`.

## 28. Phase 24 - Bitwise Instructions

### Goal

Implement the core bitwise instructions used by textbook MASM programs.

### Tasks

1. Add parser, IR, and executor support for:
   - `and`
   - `or`
   - `xor`
   - `not`
2. Support register destinations and memory destinations where existing memory-operand width rules are unambiguous.
3. Support register, immediate, and memory sources where compatible with the destination width.
4. Update flags for `and`, `or`, and `xor`:
   - update `ZF`
   - update `SF`
   - clear `CF`
   - clear `OF`
5. Define `not` as a bitwise complement that does not modify flags.
6. Validate immediate values against destination width.
7. Add tests for all supported widths, memory operands, flag behavior, and immediate range failures.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0FF00h
    and eax, 00FFh
    xor eax, 00AAh
    or  eax, 0001h
main ENDP
END main
```

executes successfully with deterministic register and flag results.

A separate test verifies that `not eax` changes `EAX` but leaves the tracked flags unchanged.

## 29. Phase 25 - Shift Instructions and SAL Alias

### Goal

Implement the basic shift instructions needed for bit manipulation examples, including `sal` as an alias of `shl`.

### Tasks

1. Add parser, IR, and executor support for:
   - `shl`
   - `sal`
   - `shr`
   - `sar`
2. Treat `sal` as an alias of `shl` for supported operands and flag behavior.
3. Support register destinations first.
4. Support memory destinations only where existing memory-operand width rules are unambiguous.
5. Support shift counts from:
   - immediate literals
   - `CL`
6. Implement and test 8-bit, 16-bit, and 32-bit behavior.
7. Update flags for the supported behavior and document any intentional simplifications.
8. Reject unsupported counts, invalid widths, and malformed operands with structured diagnostics.
9. Add tests for zero, one-bit, multi-bit, sign-preserving, edge-count shifts, and `sal`/`shl` equivalence.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 1
    sal eax, 3
    shr eax, 1
main ENDP
END main
```

executes successfully and leaves:

```text
EAX = 00000004h / 4
```

A `sar` test verifies sign-preserving right-shift behavior for a negative 32-bit value.

## 30. Phase 26 - Rotate Instructions

### Goal

Implement basic rotate instructions used in bit-manipulation examples.

### Tasks

1. Add parser, IR, and executor support for:
   - `rol`
   - `ror`
2. Support register destinations first.
3. Support memory destinations only where existing memory-operand width rules are unambiguous.
4. Support rotate counts from:
   - immediate literals
   - `CL`
5. Implement and test 8-bit, 16-bit, and 32-bit behavior.
6. Update `CF` for the last bit rotated out. Define and document the supported `OF` behavior for one-bit rotates; for multi-bit rotates, either preserve or mark `OF` according to the simulator's documented simplification.
7. Reject unsupported counts, invalid widths, and malformed operands with structured diagnostics.
8. Add tests for zero, one-bit, multi-bit, and wraparound rotates.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov al, 81h
    rol al, 1
    ror al, 1
main ENDP
END main
```

executes successfully and leaves:

```text
AL = 81h / 129
```

## 31. Phase 27 - LEA

### Goal

Implement `lea` as effective-address computation without reading memory.

### Tasks

1. Add parser, IR, and executor support for:
   - `lea reg, memory_operand`
2. Reuse the supported memory-address parsing forms from earlier milestones:
   - direct symbols
   - constant symbol offsets
   - offset-zero bracketed forms
   - register-indirect forms
   - symbol/register forms
3. Compute the effective address and write it to the destination register.
4. Do not read from the computed address.
5. Do not emit unaligned-access warnings for `lea`, because no memory access occurs.
6. Reject memory destinations and immediate destinations for `lea`.
7. Add tests proving that `lea` computes addresses without requiring the target address to be readable.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    lea esi, nums[8]
    mov eax, esi
main ENDP
END main
```

executes successfully and leaves:

```text
EAX = OFFSET nums + 8
```

No memory read is recorded for the `lea` instruction.

## 32. Phase 28 - MUL and IMUL

### Goal

Implement basic one-operand unsigned and signed multiplication.

### Tasks

1. Add parser, IR, and executor support for:
   - `mul r/m8`
   - `mul r/m16`
   - `mul r/m32`
   - `imul r/m8`
   - `imul r/m16`
   - `imul r/m32`
2. Implement implicit accumulator behavior:
   - 8-bit: `AL * r/m8 -> AX`
   - 16-bit: `AX * r/m16 -> DX:AX`
   - 32-bit: `EAX * r/m32 -> EDX:EAX`
3. Support register and memory source operands.
4. Define and test `CF` and `OF` behavior based on whether the high half of the product is significant.
5. Leave other tracked flags unchanged or documented as undefined/unchanged according to the simulator's chosen rule.
6. Reject unsupported two-operand and three-operand `imul` forms until explicitly implemented later.
7. Add tests for small products, high-half products, signed negative products, and memory operands.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 6
    mov ebx, 7
    mul ebx
main ENDP
END main
```

executes successfully and leaves:

```text
EDX:EAX = 00000000h:0000002Ah
EAX = 0000002Ah / 42
```

A signed `imul` test verifies multiplication involving a negative operand.

## 33. Phase 29 - DIV and IDIV

### Goal

Implement basic unsigned and signed division with runtime diagnostics for invalid division cases.

### Tasks

1. Add parser, IR, and executor support for:
   - `div r/m8`
   - `div r/m16`
   - `div r/m32`
   - `idiv r/m8`
   - `idiv r/m16`
   - `idiv r/m32`
2. Implement implicit dividend and result behavior:
   - 8-bit: `AX / r/m8 -> AL quotient, AH remainder`
   - 16-bit: `DX:AX / r/m16 -> AX quotient, DX remainder`
   - 32-bit: `EDX:EAX / r/m32 -> EAX quotient, EDX remainder`
3. Support register and memory divisor operands.
4. Detect division by zero and stop execution with a structured runtime diagnostic.
5. Detect quotient overflow and stop execution with a structured runtime diagnostic.
6. Preserve source line and instruction text in diagnostics.
7. Add tests for valid division, nonzero remainders, division by zero, quotient overflow, signed division, and memory divisors.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov edx, 0
    mov eax, 42
    mov ebx, 7
    div ebx
main ENDP
END main
```

executes successfully and leaves:

```text
EAX = 00000006h / 6
EDX = 00000000h / 0
```

A divide-by-zero program stops with a structured runtime diagnostic that includes the source line and instruction text.

## 34. Phase 30 - Labels and JMP

### Goal

Implement basic label resolution and unconditional jumps.

### Tasks

1. Build a label table for executable instructions.
2. Resolve `jmp label` to an instruction index or VM instruction address.
3. Define and test EIP/instruction-index behavior for straight-line and jump execution.
4. Preserve source line and instruction text in diagnostics.
5. Reject jumps to unknown labels with structured assembly diagnostics.
6. Add tests for forward and backward jumps.

### Acceptance Criteria

A program using `jmp done` skips instructions correctly and halts normally at `END main`.

## 35. Phase 31 - CMP and Equality Jumps

### Goal

Implement `cmp` and equality-based conditional jumps.

### Tasks

1. Implement `cmp` using existing subtraction flag semantics without storing the result.
2. Implement:
   - `je`, `jz`
   - `jne`, `jnz`
3. Add parser and executor support for the new opcodes.
4. Add tests for equal and not-equal branches.
5. Ensure source-line diagnostics remain correct for invalid operands.

### Acceptance Criteria

A program using `cmp eax, ebx` followed by `je equalLabel` branches correctly based on `ZF`.

## 36. Phase 32 - Signed and Unsigned Conditional Jumps

### Goal

Implement signed and unsigned relational jumps.

### Tasks

1. Implement signed jumps:
   - `jl`, `jnge`
   - `jle`, `jng`
   - `jg`, `jnle`
   - `jge`, `jnl`
2. Implement unsigned jumps:
   - `ja`, `jnbe`
   - `jae`, `jnb`
   - `jb`, `jnae`
   - `jbe`, `jna`
3. Add tests that distinguish signed and unsigned branch behavior.
4. Add edge-case tests for overflow, carry, zero, and sign interactions.

### Acceptance Criteria

Signed and unsigned comparison programs branch differently where expected, for example comparing `-1` and `1` as signed versus unsigned values.

## 37. Phase 33 - SETcc Instructions

### Goal

Implement conditional byte-set instructions after comparison and conditional-jump semantics are stable.

### Tasks

1. Add parser, IR, and executor support for a focused `setcc` family:
   - `sete`, `setz`
   - `setne`, `setnz`
   - `setl`, `setle`, `setg`, `setge`
   - `seta`, `setae`, `setb`, `setbe`
2. Support 8-bit register destinations and byte memory destinations where the access width is unambiguous.
3. Evaluate the same flag predicates used by the corresponding conditional jumps.
4. Write `1` when the condition is true and `0` when false.
5. Do not modify flags.
6. Add tests for equality, signed comparisons, unsigned comparisons, register destinations, memory destinations, and flag preservation.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 5
    cmp eax, 10
    setl al
main ENDP
END main
```

executes successfully and leaves:

```text
AL = 01h / 1
```

## 38. Phase 34 - LOOP and Instruction Limit

### Goal

Implement `loop` and protect execution from infinite loops.

### Tasks

1. Implement `loop label` using `ECX` decrement semantics.
2. Add instruction counter to source execution.
3. Add default instruction-limit enforcement.
4. Stop execution with structured diagnostics when the limit is exceeded.
5. Include source line and instruction text in instruction-limit diagnostics.
6. Add tests for finite loops and infinite loops.

### Acceptance Criteria

A finite loop program executes correctly.

An infinite loop stops with:

```text
Execution stopped: instruction limit exceeded.
```

The message includes source line and instruction text.

## 39. Phase 35 - Extended LOOP and JECXZ Helpers

### Goal

Add common loop helper instructions that appear in textbook examples after the base `loop` and conditional-jump behavior is available.

### Tasks

1. Add parser, IR, and executor support for:
   - `loope`, `loopz`
   - `loopne`, `loopnz`
   - `jcxz`
   - `jecxz`
2. Implement `loope` / `loopz` as `ECX--` then jump when `ECX != 0` and `ZF == 1`.
3. Implement `loopne` / `loopnz` as `ECX--` then jump when `ECX != 0` and `ZF == 0`.
4. Implement `jecxz` using `ECX == 0`.
5. Implement `jcxz` using `CX == 0` in MASM32 Educational Mode unless a later compatibility decision changes this behavior.
6. Preserve instruction-limit enforcement and source-line diagnostics.
7. Add tests for taken and not-taken cases, zero-count behavior, and aliases.

### Acceptance Criteria

Programs using `loope`, `loopne`, `jcxz`, and `jecxz` execute deterministically and preserve the same source-line diagnostic quality as existing jumps and `loop`.

## 40. Phase 36 - Stack Initialization, PUSH, and POP

### Goal

Initialize the simulated stack and implement basic stack data movement.

### Tasks

1. Initialize `ESP` to the top of the stack region.
2. Implement `push`.
3. Implement `pop`.
4. Track stack usage and peak stack usage.
5. Detect stack overflow and invalid stack reads.
6. Add tests for register, immediate, and memory push/pop forms where supported.

### Acceptance Criteria

A program can push a value, pop it into another register, and preserve expected stack pointer behavior.

Stack overflow stops execution with a structured diagnostic.

## 41. Phase 37 - CALL and RET

### Goal

Support procedure calls and returns.

### Tasks

1. Implement `call label` and `call ProcedureName`.
2. Implement `ret`.
3. Push and pop return addresses through the checked stack memory path.
4. Support nested calls.
5. Track procedure metadata where practical for diagnostics.
6. Add tests for simple and nested calls.

### Acceptance Criteria

A program with `call SomeProc` and `ret` works correctly and returns to the instruction after the call.

## 42. Phase 38 - Stack Frame Convenience

### Goal

Implement common stack-frame convenience instructions after basic `CALL` and `RET` behavior exists.

### Tasks

1. Add parser, IR, and executor support for:
   - `leave`
   - `ret imm16`
2. Implement `leave` as:
   - `mov esp, ebp`
   - `pop ebp`
3. Implement `ret imm16` as normal `ret` plus unsigned stack cleanup by the immediate byte count.
4. Preserve checked stack memory access, stack overflow/underflow diagnostics, and source metadata.
5. Add tests for standard frame teardown, nested calls, valid `ret imm16`, and invalid stack cleanup behavior.

### Acceptance Criteria

Program fragments using:

```asm
push ebp
mov ebp, esp
leave
ret
```

restore `ESP` and `EBP` according to the simulated stack model.

A separate test verifies that `ret 8` pops the return address and then advances `ESP` by eight additional bytes.

## 43. Phase 39 - Call-Depth Diagnostics

### Goal

Add clearer diagnostics for runaway recursion and excessive call nesting.

### Tasks

1. Add optional call-depth tracking.
2. Add a configurable or fixed initial call-depth limit.
3. Report possible infinite recursion before or alongside stack overflow when applicable.
4. Include current procedure and source line where known.
5. Add tests for infinite recursion and deep-but-valid calls.

### Acceptance Criteria

Infinite recursion stops with either:

```text
Runtime error: maximum call depth exceeded.
```

or stack overflow, with line and procedure information when available.

## 44. Phase 40 - Program Console Infrastructure

### Goal

Create the program-output stream that Irvine32 routines will use.

### Tasks

1. Implement Program Console stream in the worker/source-run result path.
2. Keep Program Console separate from Simulator Messages.
3. Add output byte and line counters.
4. Add default output byte and line limits.
5. Add output-limit diagnostics.
6. Add tests for output buffering and limit handling.

### Acceptance Criteria

Program output data can be returned separately from simulator warnings/errors, and output limits stop or report runaway output deterministically.

## 45. Phase 41 - Basic Irvine32 Output Routines

### Goal

Implement basic text/character Irvine32-style output routines.

### Tasks

1. Intercept calls to:
   - `Crlf`
   - `WriteChar`
   - `WriteString`
2. Define routine contracts in source comments and user-facing docs.
3. Validate string pointers and null terminators.
4. Route output only to Program Console.
5. Add tests for valid and invalid pointer cases.

### Acceptance Criteria

Program:

```asm
.data
msg BYTE "Hello", 0
.code
main PROC
    mov edx, OFFSET msg
    call WriteString
    call Crlf
main ENDP
END main
```

prints `Hello` in Program Console.

Simulator warnings/errors do not appear in Program Console.

## 46. Phase 42 - Numeric Irvine32 Output Routines

### Goal

Implement integer-formatting Irvine32-style output routines.

### Tasks

1. Intercept calls to:
   - `WriteInt`
   - `WriteDec`
   - `WriteHex`
2. Define signed/unsigned formatting behavior.
3. Respect output byte and line limits.
4. Add tests for zero, positive, negative, maximum, and minimum values.

### Acceptance Criteria

Programs using `WriteInt`, `WriteDec`, and `WriteHex` print expected textual values in Program Console.

## 47. Phase 43 - Diagnostic Irvine32 Output Routines and RandomRange

### Goal

Add diagnostic-style Irvine32 routines and deterministic randomness.

### Tasks

1. Implement `DumpRegs`.
2. Implement `DumpMem`.
3. Implement `RandomRange` using deterministic seeded randomness.
4. Add tests for output shape and deterministic random behavior.
5. Document any deviations from the real Irvine32 library.

### Acceptance Criteria

`DumpRegs` and `DumpMem` produce useful educational output, and `RandomRange` is deterministic when the seed is fixed.

## 48. Phase 44 - WAITING_FOR_INPUT Protocol

### Goal

Add the VM and worker protocol state needed for interactive input.

### Tasks

1. Add VM state `WAITING_FOR_INPUT`.
2. Add worker messages for input request, input submission, and input cancellation.
3. Pause active execution timer while waiting for input.
4. Keep Stop available while waiting for input.
5. Add input cancellation behavior.
6. Add tests using simulated input events.

### Acceptance Criteria

A program can pause execution waiting for input, the UI can submit or cancel input, and active execution time does not count human input wait time.

## 49. Phase 45 - ReadChar and ReadInt

### Goal

Implement simple Irvine32 input routines for character and integer input.

### Tasks

1. Implement `ReadChar`.
2. Implement `ReadInt`.
3. Define parsing behavior for whitespace, signs, invalid input, and cancellation.
4. Resume VM correctly after input is provided.
5. Add tests with simulated input values.

### Acceptance Criteria

Programs can call `ReadChar` and `ReadInt`, wait for browser input, resume, and use the returned values.

## 50. Phase 46 - ReadString

### Goal

Implement Irvine32-style string input.

### Tasks

1. Implement `ReadString`.
2. Validate `EDX` destination pointer and `ECX` maximum character count.
3. Write submitted input into simulated memory with null termination.
4. Return character count in `EAX`.
5. Add tests for valid input, truncation, invalid buffers, and cancellation.

### Acceptance Criteria

A program can call `ReadString`, wait for browser input, resume, and use the resulting buffer.

## 51. Phase 47 - Step Into Backend

### Goal

Expose backend stepping for debugger use.

### Tasks

1. Add a worker/API path for Step Into.
2. Execute exactly one VM instruction per step.
3. Return current instruction metadata.
4. Return source line and source text.
5. Preserve current VM state between steps.
6. Add tests for stepping through a small program.

### Acceptance Criteria

A test can step through multiple instructions and receive the correct instruction metadata for each step.

## 52. Phase 48 - Register and Flag Current-State UI

### Goal

Display current registers, aliases, and flags in the browser UI.

### Tasks

1. Add current register table.
2. Display grouped aliases.
3. Display current flags.
4. Show hex and unsigned decimal by default.
5. Keep signed decimal optional or deferred.
6. Add formatter tests.

### Acceptance Criteria

The UI displays canonical registers and aliases in grouped rows, with current flag values.

## 53. Phase 49 - Last-Step Delta UI

### Goal

Display what changed during the last executed instruction.

### Tasks

1. Display last-step register changes.
2. Display alias changes derived from canonical register changes.
3. Display flag changes.
4. Display memory changes.
5. Hide unchanged aliases by default.
6. Add tests for register, alias, flag, and memory delta formatting.

### Acceptance Criteria

For:

```asm
mov eax, 20
```

Last Step panel shows:

```text
EAX: 00000000h / 0 -> 00000014h / 20
AX:  0000h / 0     -> 0014h / 20
AL:  00h / 0       -> 14h / 20
```

## 54. Phase 50 - Execution Statistics and Stack Summary UI

### Goal

Show runtime execution metrics useful during debugging.

### Tasks

1. Display instruction count.
2. Display active execution time.
3. Display stack pointer, current stack usage, peak stack usage, and remaining stack.
4. Add warning display for high stack usage if available.
5. Add formatter tests.

### Acceptance Criteria

The debugger UI shows instruction count, active execution time, and stack summary after running or stepping a program.

## 55. Phase 51 - Source-Line Breakpoints

### Goal

Add basic source-line breakpoints.

### Tasks

1. Allow users to set and clear source-line breakpoints.
2. Store breakpoints in project state when appropriate.
3. Stop before executing an instruction whose source line has a breakpoint.
4. Return structured breakpoint-hit state.
5. Add tests for breakpoint hit and continue behavior.

### Acceptance Criteria

A breakpoint pauses execution before the associated instruction executes.

## 56. Phase 52 - Continue and Pause Behavior

### Goal

Make debugger run control practical after breakpoints and stepping.

### Tasks

1. Implement Continue from paused or breakpoint state.
2. Preserve VM state across pause/continue cycles.
3. Keep Stop responsive.
4. Define behavior when Continue reaches input wait, halt, crash, or resource limit.
5. Add tests for pause/continue transitions.

### Acceptance Criteria

A program stopped at a breakpoint can continue to the next stop condition without rebuilding VM state.

## 57. Phase 53 - Step Over Backend

### Goal

Implement backend Step Over behavior.

### Tasks

1. If current instruction is not `call`, Step Over behaves like Step Into.
2. If current instruction is `call`, execute until the return address is reached or another stop condition occurs.
3. Track the number of instructions executed during Step Over.
4. Preserve aggregate state needed for delta reporting.
5. Add tests for simple and nested calls.

### Acceptance Criteria

Step Over on `call SomeProc` runs until the call returns and stops on the instruction after the call.

## 58. Phase 54 - Step Over Aggregate Delta Display

### Goal

Display aggregate changes produced during a Step Over operation.

### Tasks

1. Compute register, flag, memory, and output deltas from before the call to after return.
2. Display number of instructions executed during Step Over.
3. Show aggregate memory changes without flooding the UI.
4. Add formatter tests.

### Acceptance Criteria

Step Over on `call SomeProc` shows all changed registers, flags, memory, and instruction count for the call.

## 59. Phase 55 - Memory Visualization Improvements

### Goal

Make memory deltas educational and symbol-aware beyond the basic display added in the data and indexed-memory phases.

Parsing and execution of `nums[8]`, `[esi + 4]`, and `PTR` forms should already exist before this phase. This phase improves grouping, display, and explanatory output.

### Tasks

1. Resolve memory changes to nearest symbol when not already available from the executor result.
2. Show byte offsets using MASM-style byte offsets.
3. Show element index when the symbol has an element size.
4. Show unaligned access warnings with the exact bytes read or written.
5. Add logical write grouping and expandable byte-level view.
6. Add string interpretation for byte arrays.
7. Collapse large string/buffer changes by default while allowing expansion.

### Acceptance Criteria

For:

```asm
.data
nums DWORD 10 DUP(0)
.code
mov nums[8], 100
```

Memory Changes shows:

```text
nums + 8 DWORD
  byte offset: +8
  element index: 2
  00000000h / 0 -> 00000064h / 100
```

## 60. Phase 56 - URL Save and Share

### Goal

Allow users to share projects through encoded URLs.

### Tasks

1. Define project JSON schema.
2. Implement encode:
   - JSON stringify
   - compress
   - base64url encode
   - write to URL fragment
3. Implement decode on page load.
4. Include visible `name` parameter in URL.
5. Validate schema version.
6. Handle invalid or corrupted links gracefully.
7. Add tests for encode/decode stability.

### Acceptance Criteria

A user can create a project, copy a share URL, reload/open it, and recover source plus settings.

Super-extended memory permission is not saved.

## 61. Phase 57 - Memory Settings UI

### Goal

Expose configurable stack, `.data`, and heap sizes safely.

### Tasks

1. Add dropdown presets.
2. Add exact byte textbox.
3. Make textbox the source of truth.
4. Add Custom dropdown state.
5. Add extended presets checkbox.
6. Add validation and user-friendly errors.
7. Save requested memory settings in URL project state.
8. Enforce local safety caps.

### Acceptance Criteria

A project requesting too much memory does not run automatically.

The UI reports:

```text
This project requests <N> of simulated memory. Your local limit is <M>.
```

## 62. Phase 58 - Super-Extended Memory Mode

### Goal

Allow advanced users to opt into very large virtual memory requests without making shared links dangerous.

### Tasks

1. Add local-only setting for super-extended memory.
2. Keep it off by default.
3. Do not save it in project state.
4. Add enable confirmation dialog with required checkbox.
5. Add run confirmation for oversized projects.
6. Add committed-memory cap.
7. Add lazy page allocation if not already complete.
8. Add tests for local-only behavior.

### Acceptance Criteria

A shared project can request 3 GiB, but it cannot force the recipient's browser to enable super-extended mode.

The recipient must manually enable it and confirm before running.

## 63. Phase 59 - Extended 32-bit Mode

### Goal

Add partial 64-bit support without claiming full x64 MASM compatibility.

### Tasks

1. Add execution mode selector.
2. Extend CPU state with 64-bit canonical registers.
3. Add aliases:
   - `RAX`/`EAX`/`AX`/`AH`/`AL`
   - `R8`/`R8D`/`R8W`/`R8B`
   - etc.
4. Implement QWORD data support if not already complete.
5. Implement selected 64-bit instructions and arithmetic.
6. Ensure 32-bit writes zero-extend into 64-bit canonical registers by default.
7. Update debugger register table.
8. Add tests.

### Acceptance Criteria

Extended mode supports:

```asm
mov rax, 10
add rax, 20
```

and displays:

```text
RAX = 000000000000001Eh / 30
EAX = 0000001Eh / 30
AX  = 001Eh / 30
AL  = 1Eh / 30
```

## 64. Phase 60 - Resource Watchdogs and Diagnostics

### Goal

Harden the simulator against runaway programs and expensive assembly inputs.

### Tasks

1. Add active execution time limit.
2. Add optional input wait timeout.
3. Add output actions:
   - stop
   - keep latest output
   - pause and ask
4. Add committed-memory cap.
5. Add call-depth diagnostics.
6. Add generated-instruction count limit.
7. Prepare future include/macro depth limits.

### Acceptance Criteria

Runaway execution, runaway output, and excessive memory allocation stop with structured diagnostics instead of freezing the page.

## 65. Phase 61 - Test Suite Expansion

### Goal

Convert example programs and bug cases into regression tests.

### Tasks

1. Add tests for every implemented instruction.
2. Add tests for numeric literals, including negative decimal and hexadecimal literals.
3. Add tests for register aliases.
4. Add tests for flag behavior.
5. Add tests for direct symbol memory operands.
6. Add tests for indexed and symbol-relative memory operands.
7. Add tests for `PTR`, `OFFSET`, `TYPE`, `LENGTHOF`, and `SIZEOF`.
8. Add tests for signed and unsigned jumps.
9. Add tests for memory permissions.
10. Add tests for unaligned access.
11. Add tests for Irvine32 routines.
12. Add tests for debugger deltas.
13. Add tests for URL schema migration.
14. Add tests for worker stop behavior where practical.

### Acceptance Criteria

All known supported behavior is covered by automated tests.

Bug fixes must include regression tests.

## 66. Phase 62 - Documentation and Examples

### Goal

Make the project understandable for users and future contributors.

### Tasks

1. Add user-facing quickstart.
2. Add examples:
   - hello world
   - arithmetic
   - loop
   - array write
   - ReadString
   - procedure call
3. Add supported syntax reference.
4. Add unsupported-feature list.
5. Add developer architecture notes.
6. Generate Doxygen documentation for the C99 core.
7. Ensure all public APIs have Doxygen comments.

### Acceptance Criteria

A new developer can understand the architecture and add a simple instruction by following documentation.

A new user can run an example without reading the implementation.

## 67. Phase 63 - CodeMirror Source Editor Integration

### Goal

Replace the plain source textarea with CodeMirror 6 without changing simulator semantics.

### Tasks

1. Add CodeMirror 6 dependencies to the web project.
2. Create an editor wrapper module that exposes get/set source text.
3. Preserve existing Run behavior and worker protocol.
4. Enable line numbers.
5. Keep diagnostics and source execution based on plain strings passed to the worker.
6. Add tests where practical and manual verification steps for browser behavior.

### Acceptance Criteria

The user can edit and run the same programs as before, now through CodeMirror with line numbers.

## 68. Phase 64 - MASM Syntax Highlighting

### Goal

Add MASM-aware syntax highlighting to the CodeMirror editor.

### Tasks

1. Highlight directives.
2. Highlight instructions.
3. Highlight registers and aliases.
4. Highlight data types and operators.
5. Highlight strings, character literals, numbers, comments, and labels.
6. Keep highlighting lightweight and independent from semantic validation.
7. Add fixture tests for tokenizer/highlighting helpers where practical.

### Acceptance Criteria

The editor visually distinguishes MASM directives, instructions, registers, data declarations, labels, comments, strings, and numeric literals.

## 69. Phase 65 - Editor Indentation Behavior

### Goal

Add predictable basic indentation behavior for assembly editing.

### Tasks

1. Preserve the previous line's indentation when Enter is pressed.
2. Configure Tab to insert spaces.
3. Configure Shift+Tab to dedent one indentation level.
4. Keep MASM-aware auto-indentation conservative.
5. Add tests for indentation helpers where practical.

### Acceptance Criteria

When the user indents procedure instructions, new lines keep that indentation until the user changes it.

## 70. Phase 66 - Dark Mode and Local Editor Preferences

### Goal

Add local-only visual preferences for the editor and site.

### Tasks

1. Add dark/light mode toggle.
2. Coordinate site theme and CodeMirror theme.
3. Store the preference in local storage only.
4. Do not save the preference in URL project state.
5. Add tests for state serialization exclusion where practical.

### Acceptance Criteria

The user's dark/light preference persists locally but is not included in shared links.

## 71. Phase 67 - Editor Diagnostics Integration

### Goal

Connect parser/VM diagnostics to CodeMirror visual markers.

### Tasks

1. Map structured diagnostics with line/column to editor decorations.
2. Add clickable Simulator Messages that jump to the source location.
3. Highlight diagnostic lines or ranges without altering source text.
4. Clear stale diagnostics on successful runs or new runs.
5. Add tests for diagnostic mapping helpers.

### Acceptance Criteria

A parser error shown in Simulator Messages can be clicked to move the editor to the relevant line and column.

## 72. Phase 68 - Debugger Editor Integration

### Goal

Integrate the editor with debugger execution state.

### Tasks

1. Highlight the current execution line.
2. Add breakpoint gutter support using CodeMirror gutters.
3. Keep breakpoints synchronized with project state.
4. Display breakpoint markers without interfering with line numbers.
5. Add tests for breakpoint state mapping where practical.

### Acceptance Criteria

The current execution line is highlighted during debugging, and users can set/clear breakpoints through the editor gutter.

## 73. Phase 69 - Final Polish and UX Hardening

### Goal

Improve quality, clarity, and resilience after the major editor, debugger, and simulator features are in place.

### Tasks

1. Improve error wording.
2. Add copyable diagnostics.
3. Add collapsible debugger panels.
4. Add compact and expanded register views.
5. Add signed decimal display option.
6. Add memory watch panel.
7. Add raw memory viewer.
8. Add project reset confirmation.
9. Add corrupted URL recovery flow.
10. Add UI state persistence for harmless local preferences.

### Acceptance Criteria

The simulator is comfortable for real educational use.

Errors are actionable and do not look like internal crashes unless the simulator itself failed.

## 72. Suggested AI Assistant Workflow

When using an AI assistant to implement the project, use small prompts tied to one phase at a time.

Good prompt style:

```text
Implement Phase 3 only. Add checked memory regions with read/write helpers, permissions, unaligned-access warning reporting, and unit tests. Follow the Doxygen documentation rules in the spec. Do not implement the parser yet.
```

Avoid prompts like:

```text
Build the whole MASM simulator.
```

For each phase, ask the assistant to:

1. Summarize the intended changes.
2. Modify only the necessary files.
3. Add or update tests.
4. Keep public APIs documented.
5. Run the test suite.
6. Explain any limitations.

## 73. Definition of v1 Complete

Version 1 is complete when a user can:

1. Paste or edit a small MASM32/Irvine32 console program in a CodeMirror-based source editor with line numbers.
2. Run it in the browser.
3. Provide input when requested.
4. See program output in Program Console.
5. See simulator warnings/errors in Simulator Messages.
6. Step through instructions.
7. Inspect registers, aliases, flags, stack summary, and changed memory.
8. Use breakpoints and Step Over.
9. Configure memory, execution, and output limits.
10. Share the project with a compressed URL.

The v1 target is educational quality and predictable behavior, not full MASM compatibility.
