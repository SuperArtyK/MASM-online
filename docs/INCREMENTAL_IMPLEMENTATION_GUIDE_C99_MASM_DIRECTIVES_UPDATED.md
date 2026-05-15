# Online MASM32 Educational Simulator - Incremental Implementation Guide

## 1. Purpose

This guide breaks the simulator into small implementation phases suitable for incremental development with an AI coding assistant.

The goal is to avoid attempting full MASM compatibility at once. Each phase should produce a working, testable improvement.

The guide assumes the final target described in `FULL_IMPLEMENTATION_SPEC_C99_MASM_DIRECTIVES_UPDATED.md`.

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
    FULL_IMPLEMENTATION_SPEC_C99_MASM_DIRECTIVES_UPDATED.md
    INCREMENTAL_IMPLEMENTATION_GUIDE_C99_MASM_DIRECTIVES_UPDATED.md
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

Implement `test`, a common flag-setting instruction used before conditional jumps, while preserving the simulator's explicit/inferable memory-width rule.

### Tasks

1. Add parser, IR, and executor support for `test`.
2. Support register/register, register/immediate, register/memory, memory/register, and memory/immediate forms where existing width rules are unambiguous.
3. For `test mem, imm`, require an explicit or inferable memory width:
   - valid: `test value, 1` where `value` is a typed data symbol;
   - valid: `test nums[8], 1` where symbol metadata gives the width;
   - valid: `test BYTE PTR [esi], 1`;
   - valid: `test WORD PTR [esi], 1`;
   - valid: `test DWORD PTR [esi], 1`;
   - rejected: `test [esi], 1`;
   - rejected: `test [esi + 4], 1`.

   These rejections are MASM-compatible and must not be described as temporary milestone limitations. The diagnostic should explain that the memory access width is ambiguous and that the user should write `BYTE PTR`, `WORD PTR`, or `DWORD PTR`.
4. Compute bitwise AND for flag updates only; do not store the result.
5. Update `ZF` and `SF`.
6. Clear `CF` and `OF`.
7. Preserve source metadata and emit structured diagnostics for invalid widths or malformed operands.
8. Add tests proving that destination operands are not modified.

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

Additional tests must verify memory/immediate width behavior:

```asm
.data
value DWORD 0F0F0F0Fh
nums  DWORD 10 DUP(0)

.code
main PROC
    test value, 0FFh
    test nums[0], 0FFh
    test DWORD PTR [esi], 1
main ENDP
END main
```

The above forms are accepted when the memory address is valid.

These forms must produce structured assembly diagnostics:

```asm
test [esi], 1
test [esi + 4], 1
```

because the memory access width is ambiguous.

The diagnostic should use a stable code such as:

```text
ambiguous-memory-width
```

or:

```text
invalid-instruction-operands
```

It should not use `unsupported-syntax` with wording such as "unsupported by the current milestone."

## 27. Phase 23 - Signed PTR Width Aliases

### Goal

Correct the signed integer `PTR` width gap before adding more memory-capable instructions.

Signed data declarations already exist. MASM-compatible `PTR` handling must therefore accept signed integer type names anywhere their unsigned-width counterparts are legal.

### Tasks

1. Parse and accept signed `PTR` width overrides:
   - `SBYTE PTR`
   - `SWORD PTR`
   - `SDWORD PTR`
   - `SQWORD PTR`

2. Map signed `PTR` aliases to access widths:

   ```text
   SBYTE PTR  -> 1 byte
   SWORD PTR  -> 2 bytes
   SDWORD PTR -> 4 bytes
   SQWORD PTR -> 8 bytes
   ```

3. Preserve existing unsigned `PTR` behavior:
   - `BYTE PTR`
   - `WORD PTR`
   - `DWORD PTR`
   - `QWORD PTR`

4. Treat `SBYTE PTR`, `SWORD PTR`, and `SDWORD PTR` as executable 8-bit, 16-bit, and 32-bit memory-width overrides in MASM32 Educational Mode.

5. Continue recognizing `QWORD PTR` and `SQWORD PTR`, but reject executable 64-bit memory operations in MASM32 Educational Mode until Extended 32-bit Mode enables them.

6. Preserve signedness metadata where practical for diagnostics and later display, but do not make ordinary `mov` sign-extend.

7. Update unsupported-PTR diagnostics so valid signed width names are not reported as unsupported.

8. Add parser, source-run, and regression tests.

### Acceptance Criteria

This program executes and reads signed data bytes/words/dwords by width without implicit sign extension:

```asm
.data
b SBYTE -1
w SWORD -2
d SDWORD -3
.code
main PROC
    mov al, SBYTE PTR b
    mov bx, SWORD PTR w
    mov ecx, SDWORD PTR d
main ENDP
END main
```

Expected final state:

```text
AL = FFh
BX = FFFEh
ECX = FFFFFFFDh
```

This program writes through a signed `PTR` alias:

```asm
.data
buf BYTE 4 DUP(0)
.code
main PROC
    mov esi, OFFSET buf
    mov SBYTE PTR [esi], -1
    mov al, BYTE PTR [esi]
main ENDP
END main
```

Expected final state:

```text
AL = FFh
```

This remains rejected in MASM32 Educational Mode:

```asm
.data
q SQWORD -1
.code
main PROC
    mov eax, SQWORD PTR q
main ENDP
END main
```

Expected diagnostic category:

```text
unsupported-runtime-feature or unsupported-ptr-width
```

The message should explain that executable QWORD/SQWORD memory operations are deferred until Extended 32-bit Mode.

## 28. Phase 24 - All-GPR Register-Indirect Base Registers

### Goal

Expand simple register-indirect addressing from the earlier minimal subset to MASM-compatible 32-bit base registers.

### Tasks

1. Support all 32-bit general-purpose registers as simple memory base registers:
   - `[eax]`
   - `[ebx]`
   - `[ecx]`
   - `[edx]`
   - `[esi]`
   - `[edi]`
   - `[ebp]`
   - `[esp]`

2. Support simple displacement forms for all base registers:
   - `[eax + 4]`
   - `[ecx - 4]`
   - `[esp + 8]`

3. Preserve existing supported forms:
   - `[esi]`
   - `[edi]`
   - `[ebx]`
   - `[ebp]`
   - `[esi + 4]`
   - `[esi - 4]`
   - `array[esi]`
   - `[array + esi]`

4. Treat `ESP` as a valid base register.

5. Keep scaled-index forms rejected:
   - `[eax * 4]`
   - `[base + index * scale]`
   - `array[esi * 4]`

6. Preserve runtime memory safety. Invalid addresses produced by valid addressing syntax must fail at runtime through checked memory diagnostics, not at parse time.

7. Add parser, executor/source-run, and diagnostic tests for every newly accepted base register.

### Acceptance Criteria

This program executes:

```asm
.data
nums DWORD 4 DUP(0)
.code
main PROC
    mov eax, OFFSET nums
    mov DWORD PTR [eax], 100
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Expected final state:

```text
EBX = 00000064h / 100
```

This program parses but fails at runtime because address zero is invalid:

```asm
.code
main PROC
    mov eax, 0
    test [eax], eax
main ENDP
END main
```

Expected behavior:

```text
No assembly unsupported-register-indirect-base diagnostic.
Runtime memory diagnostic for address 00000000h.
```

This remains rejected as unsupported scaled-index syntax:

```asm
.code
main PROC
    mov eax, DWORD PTR [eax * 4]
main ENDP
END main
```

## 29. Phase 25 - Global Memory Width Resolution Rules

### Goal

Centralize memory-width resolution so all current and future instruction parsers use the same MASM-compatible rules.

### Tasks

1. Add or refactor a shared memory-width resolution helper for parser/instruction validation.

2. Use the helper for all currently implemented memory-capable instructions:
   - `mov`
   - `add`
   - `sub`
   - `adc`
   - `sbb`
   - `xchg`
   - `neg`
   - `test`

3. Define width sources:
   - explicit `PTR` override;
   - declared symbol metadata;
   - symbol-relative operand metadata;
   - register operand in the same instruction when the instruction form unambiguously supplies width;
   - instruction-specific implicit width where defined.

4. Reject ambiguous memory/immediate forms with a stable diagnostic code:

   ```text
   ambiguous-memory-width
   ```

5. Use user-facing wording similar to:

   ```text
   Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.
   ```

6. Ensure MASM-invalid ambiguous forms are not classified as `unsupported-feature` or `unsupported-syntax` caused by an implementation milestone.

7. Add regression tests for all current instructions that accept memory operands.

### Acceptance Criteria

These are accepted because a register operand supplies width:

```asm
.code
main PROC
    mov eax, 0
    test [eax], eax
    test [eax], ax
    test [eax], al
main ENDP
END main
```

The program may fail at runtime if `EAX` points outside simulated memory, but it must not fail assembly because of ambiguous memory width.

These are accepted because `PTR` supplies width:

```asm
.code
main PROC
    mov eax, 0
    test DWORD PTR [eax], 1
    test WORD PTR [eax], 1
    test BYTE PTR [eax], 1
main ENDP
END main
```

These are rejected as ambiguous:

```asm
.code
main PROC
    mov eax, 0
    test [eax], 1
    mov [eax], 1
    add [eax], 1
    sbb [eax], 1
main ENDP
END main
```

Expected diagnostic code:

```text
ambiguous-memory-width
```

## 30. Phase 26 - MASM Header Directives

### Goal

Accept common MASM32 textbook header directives and improve directive diagnostics before continuing with more instruction milestones.

This phase lets users paste ordinary MASM32/Irvine32 classroom programs without deleting standard setup lines such as `.386`, `.model flat, stdcall`, `.stack`, `OPTION CASEMAP:NONE`, and `INCLUDE Irvine32.inc`.

### Tasks

1. Parse and accept processor compatibility directives as no-ops:
   - `.386`
   - `.486`
   - `.586`
   - `.686`

2. Parse and accept:

   ```asm
   .model flat, stdcall
   ```

   as MASM32 Educational Mode compatibility syntax.

3. Reject unsupported `.model` forms with structured diagnostics.

4. Parse `.stack` with optional size:

   ```asm
   .stack
   .stack 4096
   .stack 1000h
   ```

5. Store requested stack size as parser/project metadata if practical. Do not apply runtime stack behavior in this phase.

6. Parse and accept built-in virtual includes:

   ```asm
   INCLUDE Irvine32.inc
   INCLUDE Macros.inc
   ```

7. Treat `INCLUDE Irvine32.inc` as accepted even before Irvine32 routines are executable.

8. Treat `INCLUDE Macros.inc` as accepted virtual no-op for paste compatibility. Macro invocations remain unsupported until the selected macro compatibility phase.

9. Reject unknown include files with structured diagnostics such as:

   ```text
   unsupported-include
   ```

10. Parse and accept:

   ```asm
   OPTION CASEMAP:NONE
   ```

   as a compatibility no-op documenting the simulator's user-symbol case policy.

11. Define case policy:
    - instructions, registers, directives, operators, and data types are case-insensitive;
    - user-defined symbols are case-sensitive in MASM32 Educational Mode;
    - other `OPTION CASEMAP` forms are rejected.

12. Reject unsupported `OPTION` forms with structured diagnostics:
    - `OPTION NOKEYWORD`
    - `OPTION DOTNAME`
    - `OPTION NODOTNAME`
    - unsupported `OPTION LANGUAGE` forms
    - any other unsupported option.

13. Parse and accept listing/documentation directives as no-ops:
    - `TITLE`
    - `SUBTITLE`
    - `PAGE`

14. Recognize but reject or defer:
    - `COMMENT`
    - `ECHO`
    - `.LIST`
    - `.NOLIST`
    - `.CREF`
    - `.NOCREF`
    - `.TFCOND`

15. Add explicit unsupported-feature diagnostics for:
    - `ASSUME`
    - `.STARTUP`
    - `.EXIT`
    - `.DOSSEG`
    - `.FARDATA`
    - `.FARDATA?`
    - `ALIGN`
    - `EVEN`
    - `LABEL`
    - `ORG`
    - `EXTERNDEF`
    - `EXTRN`
    - broader directive families listed in the full specification.

16. Preserve line, column, byte offset, and span length for every directive diagnostic.

17. Do not implement real include file loading, Windows API behavior, stack runtime behavior, object-file linkage, listing generation, macro expansion, segment assumptions, or conditional assembly semantics.

### Acceptance Criteria

This program parses and executes the body normally:

```asm
.386
.model flat, stdcall
.stack 4096
OPTION CASEMAP:NONE
INCLUDE Irvine32.inc

.data
msg BYTE "Hello", 0
.code
main PROC
    mov edx, OFFSET msg
main ENDP
END main
```

This program also parses because `Macros.inc` is virtual no-op:

```asm
.386
.model flat, stdcall
INCLUDE Irvine32.inc
INCLUDE Macros.inc
.code
main PROC
END main
```

This program is rejected:

```asm
.model small, c
.code
main PROC
END main
```

Expected diagnostic:

```text
unsupported-model
```

This program is rejected:

```asm
INCLUDE Windows.inc
.code
main PROC
END main
```

Expected diagnostic:

```text
unsupported-include
```

## 31. Phase 27 - Additional Data Sections: .DATA? and .CONST

### Goal

Support common MASM simplified data sections that are important for textbook paste compatibility.

### Tasks

1. Parse `.DATA?` as an uninitialized data section.

2. Store `.DATA?` declarations deterministically as zero-filled bytes while retaining metadata that the bytes were originally uninitialized.

3. Parse `.CONST` as an initialized constant data section.

4. Store `.CONST` symbols with read-only metadata or in a read-only memory region.

5. Reject statically known writes to `.CONST` symbols when practical.

6. Ensure runtime writes to `.CONST` fail through checked memory permissions if `.CONST` has a read-only region.

7. Preserve `.data` behavior and symbol ordering.

8. Add parser, data-layout, source-run, and memory-change tests.

### Acceptance Criteria

```asm
.DATA?
buf BYTE 16 DUP(?)
.data
x DWORD 1
.CONST
limit DWORD 10
.code
main PROC
    mov eax, SIZEOF buf
    mov ebx, limit
main ENDP
END main
```

Expected:

```text
EAX = 00000010h / 16
EBX = 0000000Ah / 10
```

This should be rejected or fail at runtime through read-only memory protection:

```asm
.CONST
limit DWORD 10
.code
main PROC
    mov limit, 20
main ENDP
END main
```

## 32. Phase 28 - Numeric Equates and Simple Constant Expressions

### Goal

Add numeric equates and the first real MASM constant-expression subset.

### Tasks

1. Parse numeric equates:

   ```asm
   COUNT = 10
   MAX_SIZE EQU 128
   ```

2. Store equates in a symbol table distinct from data labels.

3. Support simple constant expressions:
   - numeric literals;
   - equate identifiers;
   - unary `+` and `-`;
   - parentheses;
   - binary `+` and `-`.

4. Allow these expressions in:
   - instruction immediates;
   - data initializers;
   - `DUP` counts;
   - symbol offsets;
   - `.stack size`;
   - `OFFSET symbol + constant` where the result is static.

5. Reject text equates and macro-style substitution unless explicitly handled in a later phase.

6. Add diagnostics for recursive, unknown, or non-constant equates.

### Acceptance Criteria

```asm
COUNT = 4
EXTRA EQU 2
.data
arr DWORD COUNT DUP(0)
.code
main PROC
    mov eax, COUNT + EXTRA
    mov ebx, SIZEOF arr
main ENDP
END main
```

Expected:

```text
EAX = 00000006h / 6
EBX = 00000010h / 16
```

## 33. Phase 29 - Extended Constant Expressions

### Goal

Expand compile-time expression support for common MASM textbook constants.

### Tasks

1. Add constant-expression operators:
   - `*`
   - `/`
   - `MOD`
   - `SHL`
   - `SHR`
   - `AND`
   - `OR`
   - `XOR`
   - `NOT`

2. Add byte/word extraction operators:
   - `HIGH`
   - `LOW`
   - `HIGHWORD`
   - `LOWWORD`

3. Define precedence and associativity explicitly in tests.

4. Keep runtime high-level condition operators out of this phase.

5. Reject non-constant expressions where constants are required.

### Acceptance Criteria

```asm
COUNT = 4 * 3
MASK  EQU 1 SHL 7
.data
arr BYTE COUNT DUP(0)
.code
main PROC
    mov eax, COUNT
    mov ebx, MASK
    mov ecx, LOWWORD 12345678h
main ENDP
END main
```

Expected:

```text
EAX = 0000000Ch / 12
EBX = 00000080h / 128
ECX = 00005678h / 22136
```

## 34. Phase 30 - Nested DUP and Initializer Expressions

### Goal

Support nested `DUP` and expression-backed initializers for textbook array declarations.

### Tasks

1. Parse nested `DUP` expressions, for example:

   ```asm
   matrix DWORD 3 DUP(4 DUP(0))
   ```

2. Support equates and constant expressions in `DUP` counts.

3. Support constant expressions in data initializers.

4. Preserve deterministic `?` storage behavior.

5. Add capacity checks and diagnostics for excessive expansion.

6. Add tests for nested byte, word, dword, signed, string, and `?` initializers.

### Acceptance Criteria

```asm
ROWS = 3
COLS = 4
.data
matrix DWORD ROWS DUP(COLS DUP(0))
.code
main PROC
    mov eax, LENGTHOF matrix
    mov ebx, SIZEOF matrix
main ENDP
END main
```

Expected:

```text
EAX = 0000000Ch / 12
EBX = 00000030h / 48
```

## 35. Phase 31 - Virtual Irvine32 Include Symbols and Program Exit

### Goal

Make `INCLUDE Irvine32.inc` meaningful before full Irvine32 routine execution by registering known virtual symbols and accepting the common Irvine `exit` terminator.

### Tasks

1. When `INCLUDE Irvine32.inc` is present, register known Irvine32 intrinsic names.

2. Classify call targets as:
   - user procedure;
   - supported Irvine32 intrinsic;
   - known but unsupported Irvine32 intrinsic;
   - unsupported Windows/API/external symbol;
   - unknown symbol.

3. Add `exit` as a built-in Irvine32 compatibility pseudo-instruction or virtual macro that terminates execution successfully.

4. Ensure `exit` does not imply real `ExitProcess` or Windows API behavior.

5. Add diagnostics:
   - `unsupported-irvine32-routine`;
   - `unsupported-windows-api`;
   - `unknown-call-target`.

6. Do not implement real routine bodies yet except `exit` termination.

### Acceptance Criteria

```asm
INCLUDE Irvine32.inc
.code
main PROC
    exit
main ENDP
END main
```

Expected:

```text
Execution completed successfully.
```

This should produce a clear unsupported Windows/API diagnostic, not execute:

```asm
INCLUDE Irvine32.inc
.code
main PROC
    call ExitProcess
main ENDP
END main
```

## 36. Phase 32 - INC and DEC

### Goal

Implement `inc` and `dec` for register and memory destinations.

### Tasks

1. Parse:
   - `inc dest`
   - `dec dest`

2. Support register and memory destinations using global memory-width resolution.

3. Update `ZF`, `SF`, and `OF`.

4. Preserve `CF` for `inc` and `dec`.

5. Reject immediate operands and ambiguous memory widths.

6. Add parser, executor, source-run, and flag tests.

### Acceptance Criteria

```asm
.code
main PROC
    stc
    mov eax, 0FFFFFFFFh
    inc eax
    dec eax
main ENDP
END main
```

`CF` remains set across `inc` and `dec`.

## 37. Phase 33 - Bitwise Instructions

### Goal

Implement `and`, `or`, `xor`, and `not`.

### Tasks

1. Parse and execute:
   - `and dest, src`
   - `or dest, src`
   - `xor dest, src`
   - `not dest`

2. Support register and memory destinations where width is known.

3. For `and`, `or`, and `xor`, update `ZF` and `SF`, clear `CF` and `OF`, and update additional flags after the extended flag phase exists.

4. `not` must not modify flags.

5. Reject ambiguous memory/immediate forms.

### Acceptance Criteria

```asm
.code
main PROC
    mov eax, 0F0F0h
    and eax, 00FFh
    or  eax, 0100h
    xor eax, 000Fh
    not eax
main ENDP
END main
```

## 38. Phase 34 - Shift Instructions and SAL Alias

### Goal

Implement common shift instructions.

### Tasks

1. Parse and execute:
   - `shl`
   - `sal` as an alias of `shl`
   - `shr`
   - `sar`

2. Support count `1`, immediate counts, and `CL` counts.

3. Support register and memory destinations where width is known.

4. Update modeled flags according to the supported count behavior.

5. Add tests for 8-bit, 16-bit, and 32-bit operands.

## 39. Phase 35 - Rotate Instructions

### Goal

Implement rotate instructions common in textbook examples.

### Tasks

1. Parse and execute:
   - `rol`
   - `ror`

2. Support count `1`, immediate counts, and `CL` counts.

3. Support register and memory destinations where width is known.

4. Update modeled flags according to the supported count behavior.

## 40. Phase 36 - LEA

### Goal

Implement effective-address computation without reading memory.

### Tasks

1. Parse `lea reg32, memory-expression`.

2. Support memory-expression forms already supported by the addressing parser:
   - symbol;
   - symbol constant offset;
   - base register;
   - base plus displacement;
   - symbol plus base where supported.

3. Compute the effective address only.

4. Do not read memory.

5. Do not emit invalid-address or unaligned-memory diagnostics merely because the computed address is not currently mapped.

### Acceptance Criteria

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    lea eax, nums[8]
    mov ebx, OFFSET nums
main ENDP
END main
```

Expected:

```text
EAX = EBX + 8
```

## 41. Phase 37 - MUL and IMUL

### Goal

Implement unsigned and signed multiplication for common 32-bit textbook cases.

### Tasks

1. Implement one-operand `mul` forms using accumulator registers.

2. Implement staged `imul` forms:
   - one-operand signed multiply;
   - two-operand register destination form if practical;
   - three-operand immediate form only if explicitly scoped.

3. Update `CF` and `OF` according to whether the high half is significant.

4. Add division-by-zero-independent multiplication tests.

## 42. Phase 38 - DIV and IDIV

### Goal

Implement unsigned and signed division.

### Tasks

1. Implement `div` and `idiv` accumulator forms.

2. Support 8-bit, 16-bit, and 32-bit divisors where practical.

3. Detect divide-by-zero and quotient overflow with structured runtime diagnostics.

4. Preserve checked memory access for memory divisors.

## 43. Phase 39 - Labels and JMP

### Goal

Implement labels and unconditional jumps.

### Tasks

1. Build a code label table.

2. Resolve `jmp label`.

3. Reject unknown labels.

4. Add instruction-limit enforcement for loops if not already present.

## 44. Phase 40 - CMP and Equality Jumps

### Goal

Implement `cmp` and equality-based conditional jumps.

### Tasks

1. Parse and execute `cmp left, right`.

2. Add:
   - `je` / `jz`
   - `jne` / `jnz`

3. Use existing flag helpers.

4. Add tests for taken and not-taken branches.

## 45. Phase 41 - Signed and Unsigned Conditional Jumps

### Goal

Implement relational conditional jumps.

### Tasks

1. Add signed jumps:
   - `jl` / `jnge`
   - `jle` / `jng`
   - `jg` / `jnle`
   - `jge` / `jnl`

2. Add unsigned jumps:
   - `ja` / `jnbe`
   - `jae` / `jnb`
   - `jb` / `jnae`
   - `jbe` / `jna`

3. Add tests for signed and unsigned comparisons.

## 46. Phase 42 - Anonymous Labels @@, @B, and @F

### Goal

Support MASM anonymous labels after ordinary label resolution is stable.

### Tasks

1. Parse anonymous label definitions:

   ```asm
   @@:
   ```

2. Resolve `@B` to the nearest previous anonymous label.

3. Resolve `@F` to the nearest following anonymous label.

4. Add diagnostics for unresolved anonymous label references.

### Acceptance Criteria

```asm
.code
main PROC
    mov ecx, 3
@@:
    dec ecx
    jnz @B
main ENDP
END main
```

## 47. Phase 43 - SETcc Instructions

### Goal

Implement conditional byte-set instructions.

### Tasks

1. Implement common `setcc` aliases:
   - `sete` / `setz`
   - `setne` / `setnz`
   - signed condition forms;
   - unsigned condition forms.

2. Destination must be an 8-bit register or byte memory operand.

3. Add parser, executor, and flag-condition tests.

## 48. Phase 44 - LOOP, Extended LOOP, and Instruction Limit

### Goal

Implement loop helper instructions and resource-limit protection.

### Tasks

1. Implement:
   - `loop`
   - `loope` / `loopz`
   - `loopne` / `loopnz`
   - `jcxz`
   - `jecxz`

2. Decrement `ECX` for loop instructions.

3. Preserve flags except where instruction semantics require otherwise.

4. Enforce instruction-count limits and produce clear diagnostics for runaway loops.

## 49. Phase 45 - Stack Initialization, PUSH, and POP

### Goal

Initialize `ESP` and implement basic stack operations.

### Tasks

1. Initialize `ESP` to the top of the simulated stack region.

2. Respect `.stack size` metadata where available.

3. Implement `push` and `pop` for 16-bit and 32-bit values as scoped.

4. Detect stack overflow and underflow through checked memory helpers.

5. Add stack memory-change display support.

## 50. Phase 46 - CALL, RET, Root Procedure Termination, and Call Target Classification

### Goal

Implement user procedure calls and returns, with clear behavior for root procedure termination and external targets.

### Tasks

1. Implement `call label` for user procedures.

2. Implement `ret`.

3. Define root procedure termination:
   - falling off the entry procedure terminates successfully;
   - `ret` from the entry procedure terminates successfully in educational mode;
   - `exit` terminates successfully.

4. Classify call targets:
   - user procedure;
   - supported Irvine intrinsic;
   - known unsupported Irvine intrinsic;
   - unsupported Windows/API/external symbol;
   - unknown symbol.

5. Add call-depth diagnostics.

## 51. Phase 47 - Stack Frame Convenience: LEAVE and RET imm16

### Goal

Add common stack-frame convenience behavior.

### Tasks

1. Implement `leave` as `mov esp, ebp` followed by `pop ebp` behavior.

2. Implement `ret imm16` to pop the return address and then release argument bytes.

3. Add stack and call-depth tests.

## 52. Phase 48 - PROC Metadata and USES

### Goal

Support common textbook `PROC USES` syntax.

### Tasks

1. Parse:

   ```asm
   MyProc PROC USES eax ebx ecx
   ```

2. Store procedure metadata.

3. Lower `USES` to save/restore behavior once stack support exists.

4. Reject unsupported distance, language, FRAME, and x64-only procedure attributes with diagnostics.

## 53. Phase 49 - LOCAL Variables and Stack Frames

### Goal

Support simple procedure-local variables after stack frame support exists.

### Tasks

1. Parse `LOCAL` declarations inside procedures.

2. Allocate stack storage for supported scalar/local array declarations.

3. Resolve local variable references relative to the procedure frame.

4. Preserve diagnostics for unsupported local types or initializer forms.

## 54. Phase 50 - PROTO, INVOKE, and ADDR

### Goal

Support common MASM procedure invocation syntax without implementing Windows API behavior.

### Tasks

1. Parse `PROTO` metadata.

2. Parse `INVOKE target, args...`.

3. Parse `ADDR symbol` and lower it like `OFFSET symbol` in flat MASM32 Educational Mode.

4. Lower supported `INVOKE` calls to pushes and `call` according to supported calling-convention metadata.

5. Reject Windows API targets and unsupported external calls with clear diagnostics.

6. Preserve `INVOKE` source locations for diagnostics.

## 55. Phase 51 - Program Console Infrastructure

### Goal

Provide a runtime console stream separate from simulator diagnostics.

### Tasks

1. Add a VM console output buffer.

2. Add output byte and line limits.

3. Return Program Console text separately from Simulator Messages.

4. Ensure simulator diagnostics never go into Program Console.

## 56. Phase 52 - Basic Irvine32 Output Routines

### Goal

Implement basic text output intrinsics.

### Tasks

1. Implement:
   - `Crlf`
   - `WriteString`
   - `WriteChar`

2. Validate memory reads for `WriteString` through checked memory helpers.

3. Emit output to Program Console only.

4. Add source-run and browser tests.

## 57. Phase 53 - Numeric Irvine32 Output Routines

### Goal

Implement common numeric output routines.

### Tasks

1. Implement:
   - `WriteInt`
   - `WriteDec`
   - `WriteHex`
   - `WriteBin`

2. Define exact formatting for signed decimal, unsigned decimal, hexadecimal, and binary output.

3. Add tests for positive, negative, zero, and boundary values.

## 58. Phase 54 - Irvine32 Debug and Utility Routines

### Goal

Implement common debug and utility intrinsics.

### Tasks

1. Implement:
   - `DumpRegs`
   - `DumpMem`
   - `Randomize`
   - `RandomRange`
   - `Random32`
   - `WaitMsg`

2. `DumpRegs` and `DumpMem` write to Program Console.

3. Random behavior must be deterministic unless seeded through `Randomize` according to documented simulator rules.

4. `WaitMsg` must use the input/wait protocol or deterministic console behavior; it must not block the browser thread.

## 59. Phase 55 - WAITING_FOR_INPUT Protocol

### Goal

Add worker/UI protocol support for input waits.

### Tasks

1. Add VM state `WAITING_FOR_INPUT`.

2. Pause execution timer and instruction counter while waiting.

3. Enable Program Console input UI.

4. Add submit and cancel handling.

5. Preserve Stop button behavior.

## 60. Phase 56 - Irvine32 Input Routines

### Goal

Implement simple numeric and character input routines.

### Tasks

1. Implement:
   - `ReadChar`
   - `ReadInt`
   - `ReadDec`
   - `ReadHex`

2. Define flag behavior:
   - `ReadInt` clears `OF` on valid input and sets `OF` on invalid or out-of-range input.
   - `ReadDec` clears `CF` on valid input and sets `CF` on invalid or out-of-range input.
   - `ReadHex` should define and test invalid-input flag behavior explicitly.

3. Add WAITING_FOR_INPUT source-run tests and UI tests.

## 61. Phase 57 - Irvine32 ReadString and Buffer Safety

### Goal

Implement string input with checked memory writes.

### Tasks

1. Implement `ReadString`.

2. Use:

   ```text
   EDX = destination buffer
   ECX = maximum character count
   ```

3. Write submitted input to simulated memory through checked helpers.

4. Null-terminate where compatible with Irvine behavior.

5. Return character count in `EAX`.

6. Validate buffer bounds and cancellation.

## 62. Phase 58 - Extended Flag Model: PF, AF, and DF

### Goal

Add flags needed for fuller MASM/Irvine/debugger compatibility and string instructions.

### Tasks

1. Add modeled flags:
   - `PF`
   - `AF`
   - `DF`

2. Add helpers to read/write/display these flags.

3. Update arithmetic helpers to set `PF` and `AF` where applicable.

4. Update logical/test helpers to set `PF` and clear or leave undefined flags according to documented simulator policy.

5. Implement or prepare for `cld` and `std` in the string-instruction phase.

6. Update `DumpRegs` and debugger flag displays.

## 63. Phase 59 - String Instructions and Direction Flag Behavior

### Goal

Implement common string instructions and repeat prefixes.

### Tasks

1. Implement direction-flag instructions:
   - `cld`
   - `std`

2. Implement string instructions:
   - `movsb`, `movsw`, `movsd`
   - `stosb`, `stosw`, `stosd`
   - `lodsb`, `lodsw`, `lodsd`
   - `cmpsb`, `cmpsw`, `cmpsd`
   - `scasb`, `scasw`, `scasd`

3. Implement prefixes:
   - `rep`
   - `repe` / `repz`
   - `repne` / `repnz`

4. Route every memory access through checked memory helpers.

5. Respect `DF` for pointer increment/decrement.

## 64. Phase 60 - COMMENT Blocks and Listing/Documentation No-Ops

### Goal

Improve paste compatibility for listing and documentation constructs.

### Tasks

1. Implement MASM `COMMENT delimiter ... delimiter` as block-comment skipping with preserved source positions.

2. Accept or clearly diagnose:
   - `ECHO`
   - `.LIST`
   - `.NOLIST`
   - `.CREF`
   - `.NOCREF`
   - `.TFCOND`

3. Prefer accepting `.LIST` and `.NOLIST` as no-ops if simple.

4. Do not generate listing files.

## 65. Phase 61 - LENGTH and SIZE Compatibility Operators

### Goal

Support or clearly diagnose older MASM compatibility operators.

### Tasks

1. Implement `LENGTH` as a compatibility alias for `LENGTHOF` where unambiguous.

2. Implement `SIZE` as a compatibility alias for `SIZEOF` where unambiguous.

3. Add diagnostics for cases where MASM `SIZE` semantics would be ambiguous or unsupported.

4. Preserve existing `TYPE`, `LENGTHOF`, and `SIZEOF` behavior.

## 66. Phase 62 - High-Level MASM Flow Lowering

### Goal

Support common MASM high-level control-flow directives by lowering them to existing labels and jumps.

### Tasks

1. Implement:
   - `.IF`
   - `.ELSEIF`
   - `.ELSE`
   - `.ENDIF`
   - `.WHILE`
   - `.ENDW`
   - `.REPEAT`
   - `.UNTIL`
   - `.UNTILCXZ`
   - `.BREAK`
   - `.CONTINUE`

2. Support runtime condition expressions needed by these forms.

3. Lower to internal labels and conditional jumps.

4. Avoid cascaded diagnostics for malformed blocks.

## 67. Phase 63 - STRUCT Layout and Field Access

### Goal

Support basic MASM structure declarations and field access.

### Tasks

1. Parse:
   - `STRUCT`
   - fields
   - `ENDS`

2. Compute field offsets and total structure size.

3. Support structure variables in `.data`.

4. Support field access such as:

   ```asm
   item.field
   ```

5. Integrate with `TYPE`, `SIZEOF`, and offset computation.

6. Reject methods, macros, and unsupported initializer forms.

## 68. Phase 64 - RECORD, WIDTH, MASK, and TYPEDEF

### Goal

Add later structured-data compatibility after `STRUCT` is stable.

### Tasks

1. Add basic `TYPEDEF` metadata support.

2. Add `RECORD` layout support if practical.

3. Add `WIDTH` and `MASK` operators for records.

4. Keep unsupported record features diagnosed clearly.

## 69. Phase 65 - Selected Irvine/Macros.inc Convenience Macros

### Goal

Add selected classroom macro conveniences without implementing the full MASM macro language.

### Tasks

1. Keep full `MACRO` / `ENDM` expansion unsupported unless a later post-v1 roadmap changes that.

2. Optionally implement selected built-in Irvine-style macro conveniences, such as:
   - `mWrite`
   - `mWriteLn`
   - `mReadString`

3. Treat selected macros as virtual built-ins, not general macro expansion.

4. Add diagnostics for unsupported macro invocations.

## 70. Phase 66 - Step Into Backend

### Goal

Add backend stepping support after core execution behavior is mature.

### Tasks

1. Expose step-into execution through the worker/Wasm path.

2. Return current instruction metadata and VM state after each step.

3. Preserve existing Run behavior.

## 71. Phase 67 - Register and Flag Current-State UI

### Goal

Display current register and flag state during debugging.

### Tasks

1. Show canonical registers and aliases where useful.

2. Show modeled flags.

3. Highlight changed values after a step.

## 72. Phase 68 - Last-Step Delta UI

### Goal

Show user-visible deltas for the most recent step.

### Tasks

1. Display changed registers.

2. Display changed flags.

3. Display changed memory.

4. Display program output produced by the step.

## 73. Phase 69 - Execution Statistics and Stack Summary UI

### Goal

Expose execution metrics and stack usage.

### Tasks

1. Show instruction count.

2. Show elapsed active execution time.

3. Show stack usage, peak stack usage, and remaining stack.

## 74. Phase 70 - Source-Line Breakpoints

### Goal

Add source-line breakpoints.

### Tasks

1. Allow toggling breakpoints by source line.

2. Map source lines to IR instruction indexes.

3. Stop before executing a breakpointed instruction.

## 75. Phase 71 - Continue and Pause Behavior

### Goal

Add debugger continue/pause behavior.

### Tasks

1. Continue from current VM state.

2. Pause safely from the worker.

3. Preserve deterministic state and messages.

## 76. Phase 72 - Step Over Backend

### Goal

Add backend Step Over behavior.

### Tasks

1. Step over user procedure calls.

2. Treat Irvine intrinsics as one logical step.

3. Preserve aggregate deltas.

## 77. Phase 73 - Step Over Aggregate Delta Display

### Goal

Show aggregate effects of a step-over operation.

### Tasks

1. Combine register, flag, memory, console, and diagnostic changes from all internal steps.

2. Display a concise summary.

## 78. Phase 74 - Memory Visualization Improvements

### Goal

Improve symbol-aware and address-aware memory display.

### Tasks

1. Group memory changes by symbol when possible.

2. Show byte offset, element index, access width, and old/new values.

3. Add raw address display for register-indirect accesses without symbols.

4. Add uninitialized `.DATA?` metadata display if available.

## 79. Phase 75 - URL Save and Share

### Goal

Allow users to save and share simulator state through encoded URLs.

### Tasks

1. Encode source text and relevant settings.

2. Decode shared URLs safely.

3. Enforce URL size safeguards.

## 80. Phase 76 - Memory Settings UI

### Goal

Expose safe memory configuration to users.

### Tasks

1. Allow selected memory-size presets.

2. Validate against safety tiers.

3. Reinitialize VM state predictably.

## 81. Phase 77 - Super-Extended Memory Mode

### Goal

Add optional larger memory configurations.

### Tasks

1. Add gated memory presets.

2. Display memory-use warnings.

3. Preserve browser responsiveness.

## 82. Phase 78 - Extended 32-bit Mode

### Goal

Add selected 64-bit data/register behavior without claiming full x64 MASM compatibility.

### Tasks

1. Add selected 64-bit registers and aliases.

2. Enable executable QWORD/SQWORD operations where scoped.

3. Keep mode label clear: Extended 32-bit Mode, not ml64/x64 MASM.

## 83. Phase 79 - Resource Watchdogs and Diagnostics

### Goal

Harden execution resource limits.

### Tasks

1. Instruction count limits.

2. Active execution time limits.

3. Output limits.

4. Memory capacity limits.

5. Recovery diagnostics.

## 84. Phase 80 - Test Suite Expansion

### Goal

Expand regression coverage after most core behavior exists.

### Tasks

1. Add a curated supported textbook program suite.

2. Add a known-unsupported program suite with expected diagnostics.

3. Add fuzz-style parser tests where practical.

## 85. Phase 81 - Documentation and Examples

### Goal

Create user-facing examples and syntax references.

### Tasks

1. Supported syntax reference.

2. Runnable example programs.

3. Known unsupported examples.

4. Tutorial pages for registers, flags, memory, stack, Irvine32, and debugging.

## 86. Phase 82 - CodeMirror Source Editor Integration

### Goal

Replace the raw editor with CodeMirror 6.

### Tasks

1. Integrate CodeMirror 6.

2. Preserve worker protocol and source string ownership.

3. Keep C99 parser as source of truth.

## 87. Phase 83 - MASM Syntax Highlighting

### Goal

Add MASM-aware syntax highlighting.

### Tasks

1. Highlight directives, instructions, registers, symbols, literals, comments, and operators.

2. Keep semantic validation in the C99 core.

## 88. Phase 84 - Editor Indentation Behavior

### Goal

Improve editing ergonomics.

### Tasks

1. Preserve indentation.

2. Support Tab and Shift+Tab.

3. Add basic MASM-friendly indentation rules.

## 89. Phase 85 - Dark Mode and Local Editor Preferences

### Goal

Add local-only editor and appearance preferences.

### Tasks

1. Dark/light editor theme.

2. Local preference persistence.

3. No remote storage.

## 90. Phase 86 - Editor Diagnostics Integration

### Goal

Connect structured diagnostics to the editor.

### Tasks

1. Show diagnostic markers.

2. Make Simulator Messages clickable.

3. Highlight source spans using line/column/byte offset/span metadata.

## 91. Phase 87 - Debugger Editor Integration

### Goal

Connect debugger state to editor UI.

### Tasks

1. Current-instruction highlighting.

2. Breakpoint gutter.

3. Step/continue visual feedback.

## 92. Phase 88 - Final Polish and UX Hardening

### Goal

Polish the first complete educational version.

### Tasks

1. Accessibility pass.

2. Error wording review.

3. Mobile/responsive layout review.

4. Example program review.

5. Final regression suite.

## 93. Suggested AI Assistant Workflow

For each implementation session:

1. Read the full specification and implementation guide.
2. Confirm the current completed phase.
3. Implement exactly one phase unless the user explicitly asks for a compliance-only fix.
4. Do not implement future phases early.
5. Keep the C core C99-only.
6. Add tests for every accepted and rejected feature.
7. Preserve line, column, byte offset, and span length in diagnostics.
8. Run relevant tests and report commands/results.
9. Review documentation and Doxygen comments.
10. Provide end-user test programs only when the phase is visible through the browser Run path.

## 94. Definition of v1 Complete

The first complete v1 should allow a user to:

- paste ordinary small MASM32/Irvine32 classroom programs with standard headers;
- use supported `.data`, `.DATA?`, `.CONST`, and `.code` sections;
- use signed and unsigned integer declarations;
- use equates and common constant expressions;
- use common array declarations including nested `DUP`;
- run core integer, memory, control-flow, stack, and procedure instructions;
- use basic Irvine32 output and input routines;
- use the Irvine `exit` terminator;
- inspect Program Console output separately from Simulator Messages;
- receive clear diagnostics for unsupported MASM, unsupported Irvine32, unsupported Windows/API, and MASM-invalid ambiguous syntax;
- step through code and inspect registers, flags, memory changes, stack usage, and last-step deltas;
- use breakpoints and continue/pause behavior;
- share source/settings through a URL;
- read documentation and examples that match the actual implemented subset.

v1 is still not:

- a full MASM replacement;
- a full x86 emulator;
- a Windows emulator;
- a PE loader;
- a linker;
- a host filesystem bridge;
- a full macro assembler;
- a full x64/ml64 implementation.
