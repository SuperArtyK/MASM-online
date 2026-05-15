# Online MASM32 Educational Simulator - Full Implementation Specification

## 1. Project Goal

Build a static, browser-based educational simulator for MASM32-style assembly programs. The project runs entirely on the client side and uses a C99 virtual machine compiled to WebAssembly with Emscripten.

The simulator is intended for learning, experimentation, debugging, and sharing small MASM32/Irvine32-style console programs.

The core product is not a native MASM compiler and not a full Windows/x86 emulator. It is a MASM-like source parser plus an internal virtual machine that models registers, flags, memory, stack behavior, control flow, selected Irvine32 routines, and resource limits.

A user should be able to:

- Type or paste MASM32-style source code in a rich browser editor with line numbers, indentation support, and later MASM syntax highlighting.
- Run the program in the browser.
- Interact with console input routines such as `ReadString`, `ReadInt`, and `ReadChar`.
- View program output separately from simulator diagnostics.
- Debug step-by-step.
- Inspect registers, flags, memory changes, stack usage, and last-step deltas.
- Share the project through an encoded URL.
- Configure memory and execution limits safely.

## 2. High-Level Product Definition

The simulator should be described as:

> A MASM32 educational simulator with Irvine32-style console support.

It should not be advertised as:

- A full MASM replacement.
- A full x86 emulator.
- A Windows emulator.
- A PE loader/linker.
- A real `ml.exe` or `ml64.exe` implementation.

## 3. Non-Goals

The following are explicit non-goals for the first complete version:

- No native x86 binary execution.
- No PE loading or real object-file linking.
- No Windows API execution.
- No GUI API simulation.
- No DLL imports.
- No threads.
- No cycle-accurate CPU simulation.
- No hardware buses, caches, paging, privilege rings, interrupts, or OS-level behavior.
- No full MASM macro system in the initial version.
- No full x64 MASM compatibility.
- No direct browser or host filesystem access from simulated programs.
- No network access from simulated programs.
- No arbitrary JavaScript execution from simulated programs.

## 4. Execution Modes

### 4.1 MASM32 Educational Mode

Default mode.

Features:

- MASM32-style syntax subset.
- 32-bit general-purpose registers.
- Flat memory model.
- Simulated `.code`, `.data`, heap, and stack regions.
- Irvine32-style console routines.
- Source-level debugging.

This mode targets beginner and intermediate MASM32/Irvine32 console programs.

### 4.2 Extended 32-bit Mode

Optional advanced mode.

This mode extends the MASM32 educational model with selected 64-bit features:

- 64-bit general-purpose registers.
- QWORD data and arithmetic.
- Selected 64-bit computations and instructions.
- 64-bit register aliases.

This is not full x64 MASM or `ml64` behavior. It should be labeled clearly as "Extended 32-bit Mode" to avoid implying real x64 Windows ABI support.

## 5. Architecture Overview

Recommended architecture:

```text
Browser main thread
  - CodeMirror 6 source editor UI
  - console UI
  - debugger UI
  - settings UI
  - URL import/export

Web Worker
  - WebAssembly module
  - parser/assembler front-end
  - IR generation
  - VM execution loop
  - Irvine32 runtime simulation
  - resource-limit enforcement
```

The main thread should never run long VM work directly. All parsing, loading, and execution should happen inside a Web Worker.

The Web Worker may be terminated by the UI if the simulator becomes unresponsive.

### 5.1 Source Editor Component

The browser source editor should eventually use **CodeMirror 6** rather than a raw `<textarea>`. CodeMirror 6 is the selected editor component for the polished editor experience because it is permissively licensed, modular, and designed for custom language extensions.

Editor integration goals:

- Line-number gutter for mapping diagnostics to source lines.
- Future breakpoint gutter using the same editor gutter system.
- Current-instruction line highlighting for debugging.
- Parser/VM diagnostic markers and clickable diagnostics.
- MASM syntax highlighting for directives, instructions, registers, data types, labels, comments, strings, numeric literals, and supported operators.
- Basic indentation support, including preserving the previous line's indentation and handling Tab / Shift+Tab predictably.
- Local-only dark/light editor themes coordinated with the site theme.
- Optional later autocomplete for instructions, registers, directives, data symbols, labels, and Irvine32 routines.

The CodeMirror integration must remain a UI layer. It must not become the source of truth for MASM semantics. Semantic validation, symbol resolution, execution, runtime errors, and authoritative diagnostics must continue to come from the C99 parser, assembler front-end, and VM.

The worker protocol should continue to accept and return plain source strings and structured diagnostics. Replacing the editor implementation must not require changes to the core C99 VM/parser APIs.

## 6. Implementation Language and Toolchain

The simulator core must be implemented in **C99**.

Required language policy:

- Use C source and header files (`.c` and `.h`) for the VM, parser, executor, memory model, Irvine32 runtime, and Wasm-facing API.
- Compile the native test build and Emscripten build as C99.
- Do not require C++ for the core implementation.
- Do not add C++ compatibility scaffolding such as `extern "C"` unless the project explicitly changes language policy later.
- Avoid C++ source files, C++ standard library dependencies, templates, classes, exceptions, and RTTI.
- Browser-side code may remain JavaScript or TypeScript.
- Public and module-level C APIs must continue to use Doxygen-style `///` comments, and each source/header file must keep a file-level block comment.

Recommended compiler posture:

```text
C standard: C99
Warnings: enabled and treated strictly where practical
Core ownership: explicit structs and functions
Allocation: deterministic and bounded where practical
Error handling: explicit status codes and structured diagnostics
```

## 7. Source-to-Execution Pipeline

Pipeline:

```text
Source files
  -> preprocess limited includes/directives
  -> parse supported MASM subset
  -> build symbols and data layout
  -> lower source to internal IR
  -> create simulated memory regions
  -> execute IR in VM
  -> stream console output and simulator diagnostics
```

The first version should execute internal IR rather than native x86 machine code.

Each IR instruction must preserve:

- Operation code.
- Operands.
- Source file.
- Source line.
- Original source text.
- Instruction address or VM instruction index.

This enables high-quality debugging and diagnostics.

## 8. Supported MASM Subset, Version 1

The numbered subsections in this section, such as `8.1` and `8.5`, are specification sections only. They are not implementation phase numbers. The incremental implementation guide owns phase numbering.


### 8.0 MASM Compatibility Classification Policy

Every MASM construct encountered by the parser should be classified explicitly. This prevents the implementation from accidentally treating a MASM-valid construct as a temporary limitation, or treating a simulator non-goal as a future promise.

Each feature should be documented as one of:

- **implemented**: fully parsed and executed or otherwise modeled by the simulator.
- **accepted no-op**: accepted for textbook compatibility but does not affect VM behavior, for example selected listing directives.
- **metadata-only**: parsed and stored for later behavior, for example `.stack size` before stack runtime behavior is active.
- **virtual built-in**: provided by the simulator without host file loading or linking, for example `INCLUDE Irvine32.inc` and supported Irvine32 intrinsics.
- **planned later**: recognized as important to textbook MASM/Irvine compatibility and assigned to a later implementation phase.
- **explicitly unsupported in v1**: recognized and diagnosed clearly, but not planned for the first complete educational version.
- **non-goal**: intentionally outside the simulator, for example real Windows API execution, PE loading, object linking, and host filesystem access.

Diagnostics should distinguish:

- MASM-invalid syntax, such as ambiguous memory width where real MASM also requires a `PTR` override.
- planned-but-not-yet-implemented syntax, reported as `unsupported-feature` or a more specific unsupported code.
- unsupported runtime behavior, such as executable QWORD memory operations in MASM32 Educational Mode.
- explicit non-goals, such as `INCLUDE Windows.inc` or `INCLUDELIB kernel32.lib`.

Core classification rule:

```text
Do not describe MASM-invalid code as "unsupported for now".
Do not describe simulator non-goals as if they are scheduled features.
Do not silently accept no-op compatibility constructs unless the spec says they are accepted no-ops.
```

### 8.1 Directives

The simulator supports a staged subset of MASM directives. Directives fall into these categories:

1. accepted structural directives;
2. accepted compatibility no-op directives;
3. metadata-only directives;
4. virtual built-in directives;
5. planned compatibility directives;
6. recognized unsupported directives with clear diagnostics;
7. explicit non-goal directives.

#### 8.1.1 Accepted Structural Directives

Initial supported structural directives:

- `.data`
- `.code`
- `PROC`
- `ENDP`
- `END`

These affect source structure, symbol layout, procedure boundaries, or entry-point validation.

`PROC` starts as a simple structural marker. Later procedure phases add `USES`, parameters, `LOCAL`, `PROTO`, `INVOKE`, and calling-convention metadata.

#### 8.1.2 Additional Data Sections

Textbook MASM compatibility should include these simplified sections as planned v1 features:

- `.DATA?`
- `.CONST`

Required behavior when implemented:

- `.DATA?` creates deterministic zero-filled storage while retaining metadata that the storage was originally uninitialized.
- `.CONST` creates initialized read-only data metadata or a read-only data region.
- Statically known writes to `.CONST` should produce assembly diagnostics when practical.
- Runtime writes to `.CONST` should fail through checked memory permission diagnostics if `.CONST` is modeled as a separate read-only region.
- Reads from originally uninitialized `.DATA?` storage may initially be allowed without warning; optional uninitialized-read warnings may be added later.

Until implemented, `.DATA?` and `.CONST` must be recognized and reported as planned unsupported features, not vague parse errors.

#### 8.1.3 Accepted MASM32 Header / Compatibility Directives

Common MASM32 textbook headers should be accepted so students do not need to delete standard setup lines.

Accepted as no-op, virtual, or metadata-only directives in MASM32 Educational Mode:

- `.386`
- `.486`
- `.586`
- `.686`
- `.model flat, stdcall`
- `.stack`
- `.stack size`
- `INCLUDE Irvine32.inc`
- `INCLUDE Macros.inc`
- `OPTION CASEMAP:NONE`
- `TITLE text`
- `SUBTITLE text`
- `PAGE`

Behavior:

- `.386`, `.486`, `.586`, and `.686` are accepted as processor-mode compatibility declarations. They do not change the VM execution model.
- `.model flat, stdcall` is accepted as MASM32 textbook compatibility syntax. Other `.model` forms produce structured diagnostics.
- `.stack` optionally records a requested stack size. Runtime stack behavior is applied by the later stack milestone.
- `INCLUDE Irvine32.inc` is accepted as a built-in virtual include. The simulator does not read the host filesystem.
- `INCLUDE Macros.inc` is accepted as a virtual no-op for paste compatibility. Macro invocations remain unsupported until selected Irvine macro compatibility is implemented.
- `OPTION CASEMAP:NONE` is accepted as a compatibility directive documenting the simulator's user-symbol case policy.
- `TITLE`, `SUBTITLE`, and `PAGE` are accepted as listing/documentation no-ops.

Default case policy:

- Instructions, registers, directives, operators, and data type names are case-insensitive.
- User-defined symbols are case-sensitive in MASM32 Educational Mode.
- `OPTION CASEMAP:NONE` is accepted as documenting that policy.
- Other `OPTION CASEMAP` forms are rejected unless a later phase explicitly adds them.

Examples:

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

#### 8.1.4 Recognized Unsupported or Deferred Directives

Unsupported or deferred directives should produce explicit diagnostics rather than generic syntax errors.

Recognized unsupported or deferred directives include:

- `.STARTUP`
- `.EXIT`
- `.DOSSEG`
- `.FARDATA`
- `.FARDATA?`
- `ASSUME`
- `ALIGN`
- `EVEN`
- `LABEL`
- `ORG`
- `EQU`, `=`, and `TEXTEQU` until the equate/expression phases
- `STRUCT`
- `UNION`
- `RECORD`
- `TYPEDEF`
- `INVOKE`
- `PROTO`
- `LOCAL`
- `INCLUDELIB`
- `EXTERN`
- `EXTERNDEF`
- `EXTRN`
- `PUBLIC`
- `COMM`
- `COMMENT`, until block-comment skipping is implemented
- `ECHO`, until listing/build-output behavior is defined
- `MACRO`
- `ENDM`
- `EXITM`
- `PURGE`
- `FOR`
- `FORC`
- `GOTO`
- `OPTION NOKEYWORD`
- `OPTION DOTNAME` and `OPTION NODOTNAME`
- unsupported `OPTION LANGUAGE` forms
- conditional assembly directives such as `IF`, `IF2`, `IFDEF`, `IFNDEF`, `IFE`, `IFB`, `IFNB`, `ELSE`, `ELSEIF`, and `ENDIF`
- conditional error directives such as `.ERR`, `.ERRB`, `.ERRDEF`, `.ERRE`, and `.ERRNZ`
- listing-control directives such as `.LIST`, `.NOLIST`, `.CREF`, `.NOCREF`, and `.TFCOND`, unless later accepted as no-ops
- advanced processor/vector directives such as `.387`, `.MMX`, `.XMM`, and `.K3D`
- safety/object-format directives such as `.SAFESEH`, `.FPO`, `PUSHCONTEXT`, and `POPCONTEXT`

Unsupported directive diagnostics should include the directive name, source line, column, byte offset, span length, and a short explanation.

#### 8.1.5 Explicit Directive Non-Goals

The following should not be implemented as real host or Windows behavior in v1:

- real host include-file loading;
- object-file linking;
- `INCLUDELIB` linking;
- Windows import libraries;
- Windows API declarations through `Windows.inc`;
- PE sections or loader behavior;
- true segmented or far-data behavior.

Such directives may be recognized only to produce clear non-goal diagnostics.

### 8.2 Data Declarations

Supported data declarations:

- `BYTE`
- `WORD`
- `DWORD`
- `QWORD`
- `SBYTE`
- `SWORD`
- `SDWORD`
- `SQWORD`
- `DB`
- `DW`
- `DD`
- `DQ`
- comma-separated initializers
- `DUP`, initially flat/non-nested
- string literals for byte-oriented data
- single-character literals such as `'A'`
- packed multi-character literals such as `'AB'` and `'ABCD'`, with width validation
- `?` uninitialized values, represented deterministically by the simulator

Examples:

```asm
.data
msg BYTE "Hello", 0
ch  BYTE 'A'
pair WORD 'AB'
tag DWORD 'ABCD'
var DWORD 10
arr BYTE 1, 2, 3, 4
buf BYTE 64 DUP(0)
qval QWORD 12345678h
sb   SBYTE -1
sw   SWORD -2
sd   SDWORD -3
sq   SQWORD -4
neg DWORD -1
```

Notes:

- `?` reserves storage. The initial deterministic simulator behavior may zero-fill the bytes while retaining metadata that the declaration was originally uninitialized.
- `SBYTE`, `SWORD`, `SDWORD`, and `SQWORD` are signed integer data declarations. They use the same byte sizes as `BYTE`, `WORD`, `DWORD`, and `QWORD`, but their initializers are validated against signed ranges.
- `QWORD` and `SQWORD` data declarations, layout, and metadata are supported in MASM32 Educational Mode. Executable 64-bit memory operations and 64-bit registers are deferred to Extended 32-bit Mode.
- Nested `DUP`, `.data?`, and complex initializer expressions are later compatibility features.

### 8.3 Numeric Literals

Supported numeric literal forms:

- decimal: `42`
- MASM-style hexadecimal with `h` suffix: `2Ah`, `0FFh`
- C-style hexadecimal with `0x` prefix: `0x2A`
- optional later MASM/debugger-style radix prefixes: `0n42`, `0y1010`, `0t52`
- optional later MASM-style binary/octal suffixes: `1010b`, `52o`, `52q`
- negative decimal: `-42`
- negative hexadecimal: `-2Ah`, `-0x2A`

Negative literals are accepted only where a numeric literal is already valid. They must be validated against the destination width and encoded as two's-complement values when they fit.

Examples:

```asm
mov al, -1          ; AL receives FFh
mov ax, -2          ; AX receives FFFEh
mov eax, -3         ; EAX receives FFFFFFFDh
.data
b BYTE -1
w WORD -2
d DWORD -3
q QWORD -4
sb SBYTE -1
sw SWORD -2
sd SDWORD -3
sq SQWORD -4
```

Required range behavior:

```text
8-bit unsigned destination/declaration:   unsigned 0..255, signed negative -128..-1 when negative literals are allowed
16-bit unsigned destination/declaration:  unsigned 0..65535, signed negative -32768..-1 when negative literals are allowed
32-bit unsigned destination/declaration:  unsigned 0..4294967295, signed negative -2147483648..-1 when negative literals are allowed
64-bit unsigned data layout:              unsigned QWORD values and negative QWORD initializers encoded as 64-bit data bytes
SBYTE declaration:                         -128..127
SWORD declaration:                         -32768..32767
SDWORD declaration:                        -2147483648..2147483647
SQWORD declaration:                        -9223372036854775808..9223372036854775807
```

For signed declarations, positive values must fit the signed positive range. For example, `SBYTE 127` succeeds but `SBYTE 128` fails. For unsigned declarations, the unsigned range remains available, and negative literals are encoded as two's-complement values only after width validation.

Out-of-range literals should produce structured assembly diagnostics instead of silent truncation.

Unary plus, parenthesized expressions, arithmetic expressions, binary/octal literals, radix-changing directives such as `.RADIX`, and symbolic expressions such as `OFFSET label - 4` are later expression-parser features.


### 8.3.1 Character and Packed Character Literals

Supported quoted character literal behavior:

- Single-character literals such as `'A'` are valid anywhere a byte-compatible numeric literal is valid.
- Multi-character literals such as `'AB'`, `'ABC'`, and `'ABCD'` are valid where the destination width can hold the decoded byte count.
- In instruction/immediate contexts, quoted character literals are converted to unsigned packed integer constants after decoding.
- Packing uses little-endian integer layout: the first decoded character becomes the least significant byte.
- Therefore `'A'` is `41h`, `'AB'` is `4241h`, and `'ABCD'` is `44434241h`.
- Width validation is required. For example, `mov al, 'AB'` must be rejected because two decoded bytes do not fit an 8-bit destination.
- `BYTE` / `DB` declarations may continue to treat quoted strings as byte sequences, so `msg BYTE 'AB', 0` emits `41h, 42h, 00h`.
- `WORD` / `DW`, `DWORD` / `DD`, `QWORD` / `DQ`, and signed equivalents such as `SWORD`, `SDWORD`, and `SQWORD` may treat quoted literals as packed scalar initializers when the decoded byte count fits the element width.

Examples:

```asm
mov al, 'A'       ; AL receives 41h
mov ax, 'AB'      ; AX receives 4241h
mov eax, 'ABCD'   ; EAX receives 44434241h
.data
ch   BYTE 'A'
pair WORD 'AB'
tag  DWORD 'ABCD'
```

Initial limitations:

- Empty character literals are rejected.
- Character literals larger than 8 decoded bytes are rejected until wider data/expression support exists.
- Escape handling should be explicit and tested. Unsupported escape forms must produce structured diagnostics rather than being silently misdecoded.


### 8.4 Operators and Type Overrides

Initial supported operators:

- `OFFSET`, initially `OFFSET symbol`
- `SIZEOF`
- `LENGTHOF`
- `TYPE`

Additional textbook operators are staged later:

- `ADDR`, with `INVOKE` and procedure-argument lowering;
- `LENGTH` and `SIZE`, as MASM compatibility aliases or explicitly diagnosed alternatives to `LENGTHOF` and `SIZEOF`;
- `HIGH`, `LOW`, `HIGHWORD`, and `LOWWORD`, with extended constant-expression support;
- `SHORT`, with control-flow and jump encoding diagnostics where applicable;
- `THIS`, with expression/type metadata support.

Memory type overrides supported in MASM32 Educational Mode:

- `BYTE PTR`
- `SBYTE PTR`
- `WORD PTR`
- `SWORD PTR`
- `DWORD PTR`
- `SDWORD PTR`
- `QWORD PTR`, recognized but executable 64-bit memory operations remain deferred in MASM32 Educational Mode
- `SQWORD PTR`, recognized but executable 64-bit memory operations remain deferred in MASM32 Educational Mode

Signed `PTR` aliases resolve to the same access widths as their unsigned counterparts:

```text
SBYTE PTR  -> 1 byte
SWORD PTR  -> 2 bytes
SDWORD PTR -> 4 bytes
SQWORD PTR -> 8 bytes, metadata only until Extended 32-bit Mode supports executable 64-bit memory operations
```

Signedness metadata may be preserved for diagnostics, type display, and future high-level constructs, but ordinary memory reads and writes do not sign-extend automatically. Use `movsx`, `movzx`, `cbw`, `cwde`, `cwd`, or `cdq` for explicit extension behavior.

Examples:

```asm
mov edx, OFFSET msg
mov ecx, LENGTHOF arr
mov ebx, TYPE arr
mov eax, SIZEOF arr
mov BYTE PTR nums[3], 100
mov SBYTE PTR [esi], -1
mov DWORD PTR [eax], 12345678h
```

`QWORD PTR` and `SQWORD PTR` should be recognized in MASM32 Educational Mode, but executable 64-bit memory reads/writes should produce structured unsupported-runtime diagnostics until Extended 32-bit Mode enables them.

#### 8.4.1 Expression and Equate Roadmap

The expression parser should be implemented in stages:

Stage A - simple constant expressions:

- numeric literals;
- equate symbols;
- unary `+` and `-`;
- parentheses;
- binary `+` and `-`.

Stage B - extended constant expressions:

- `*`, `/`, `MOD`;
- `SHL`, `SHR`;
- `AND`, `OR`, `XOR`, `NOT` as compile-time operators;
- `HIGH`, `LOW`, `HIGHWORD`, `LOWWORD`.

Stage C - runtime/high-level condition expressions:

- relational operators such as `==`, `!=`, `<`, `<=`, `>`, `>=`;
- logical operators such as `&&`, `||`, and `!` where MASM-compatible;
- condition predicates such as `ZERO?`, `CARRY?`, `SIGN?`, and `OVERFLOW?` if high-level MASM flow uses them.

Equates should also be staged:

- numeric `name = expression`;
- numeric `name EQU expression`;
- limited or explicit rejection for `TEXTEQU` until text substitution or macro compatibility exists.

### 8.5 Memory Operands and Addressing Forms

Memory operands should be implemented in stages so textbook MASM array code works before the simulator attempts full x86 addressing complexity.

Stage A - direct symbols:

```asm
mov var, 100
mov eax, var
mov edx, OFFSET msg
```

Stage B - symbol-relative and constant-indexed memory operands:

```asm
mov nums[8], 100
mov eax, nums[8]
mov BYTE PTR nums[3], 100
mov DWORD PTR [nums + 8], 100
mov eax, [nums]
mov eax, [nums + 0]
mov eax, nums[0]
```

For MASM-style source syntax in this simulator, bracketed array offsets are byte offsets. For example, `nums DWORD 10 DUP(0)` followed by `nums[8]` addresses byte offset `8`, which is DWORD element index `2`.

Stage C - simple register-indirect and displacement forms:

```asm
mov eax, [eax]
mov eax, [ebx]
mov eax, [ecx]
mov eax, [edx]
mov eax, [esi]
mov eax, [edi]
mov eax, [ebp]
mov eax, [esp]
mov eax, [eax + 4]
mov eax, [ecx - 4]
mov eax, [esp + 8]
mov [edi], al
mov array[esi], al
mov eax, [array + esi]
```

All 32-bit general-purpose registers are valid simple base registers in MASM32 Educational Mode:

```text
EAX EBX ECX EDX ESI EDI EBP ESP
```

`ESP` is valid as a base register. `ESP` remains invalid as an index register when scaled-index addressing is added later.

Stage D - later scaled-index forms:

```asm
mov eax, [base + index * scale + displacement]
mov eax, array[esi * 4]
```

Stage D is a later compatibility feature and should not block textbook examples that use constant byte offsets or simple base/displacement forms.

#### Memory Access Width Resolution

Memory operands must have a known access width before execution. Width resolution must be centralized and reused by every instruction parser that accepts memory operands.

A memory operand width may come from:

- an explicit `PTR` override, such as `BYTE PTR`, `SBYTE PTR`, `WORD PTR`, `SWORD PTR`, `DWORD PTR`, or `SDWORD PTR`;
- a declared data symbol, such as `value DWORD 0`;
- a symbol-relative operand whose base symbol has known metadata, such as `nums[8]`;
- a register operand in the same instruction when the instruction form unambiguously determines the memory width;
- an instruction-specific implicit width, when the instruction defines one.

The simulator must reject ambiguous memory forms instead of guessing.

Valid examples because width is explicit or inferable:

```asm
test eax, 1
test al, 1
test [eax], eax
test [eax], ax
test [eax], al
test DWORD PTR [eax], 1
test WORD PTR [eax], 1
test BYTE PTR [eax], 1
test value, 1
test nums[8], 1
mov [eax], bl
add [eax], ebx
xchg [eax], cx
```

Rejected examples because memory width is ambiguous:

```asm
test [eax], 1
test [eax + 4], 1
mov [eax], 1
add [eax], 1
sbb [eax], 1
```

Reason: an immediate operand does not determine memory access width, and an untyped register-indirect memory operand has no declaration metadata.

The diagnostic should classify this as `ambiguous-memory-width` or `invalid-instruction-operands`, not as a temporarily unsupported feature.

Suggested user-facing message:

```text
Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.
```

A future non-MASM convenience mode may choose to infer widths, but MASM32 Educational Mode should preserve MASM-compatible rejection.

Runtime-invalid addresses should be runtime errors, not assembly errors. For example:

```asm
.code
main PROC
    mov eax, 0
    test [eax], eax
main ENDP
END main
```

This is syntactically valid because `EAX` supplies DWORD width. It should parse, then fail at runtime because address `00000000h` is outside the simulated memory regions.

### 8.6 Instructions

The simulator should implement instructions in staged, testable groups. The goal is educational MASM32/Irvine32 compatibility, not full x86 coverage.

#### 8.6.1 Baseline and Early Textbook Instructions

Baseline arithmetic, data movement, control flow, stack, and address computation:

- `mov`
- `add`
- `sub`
- `inc`
- `dec`
- `cmp`
- `jmp`
- `je`, `jz`
- `jne`, `jnz`
- `jl`, `jnge`
- `jle`, `jng`
- `jg`, `jnle`
- `jge`, `jnl`
- `ja`, `jnbe`
- `jae`, `jnb`
- `jb`, `jnae`
- `jbe`, `jna`
- `loop`
- `push`
- `pop`
- `call`
- `ret`
- `and`
- `or`
- `xor`
- `not`
- `shl`
- `sal`, as an alias of `shl`
- `shr`
- `sar`
- `mul`
- `imul`
- `div`
- `idiv`
- `lea`

#### 8.6.2 Additional Textbook v1 Instructions

These instructions are common enough in introductory MASM coursework to include in the educational v1 roadmap.

Data movement and conversion:

- `movsx`
- `movzx`
- `cbw`
- `cwde`
- `cwd`
- `cdq`

Arithmetic and carry/borrow handling:

- `neg`
- `adc`
- `sbb`

Carry flag control:

- `clc`
- `stc`
- `cmc`

Logical/test and data exchange:

- `test`
- `xchg`
- `nop`

Rotate instructions:

- `rol`
- `ror`

#### 8.6.3 Later Planned Instruction Additions

These are useful but should be implemented after the core instruction, control-flow, and stack behavior is stable.

Extended loop helpers:

- `loope`, `loopz`
- `loopne`, `loopnz`
- `jcxz`
- `jecxz`

Stack/procedure convenience:

- `leave`
- `ret imm16`

Conditional byte set:

- `setcc` family, including:
  - `sete`, `setz`
  - `setne`, `setnz`
  - `setl`, `setle`, `setg`, `setge`
  - `seta`, `setae`, `setb`, `setbe`

#### 8.6.4 Deferred Instruction Families

These instruction families remain intentionally deferred because they require broader VM state, additional registers, string/prefix semantics, OS behavior, or substantially more compatibility work.

- String instructions and prefixes:
  - `movsb`, `movsw`, `movsd`
  - `stosb`, `stosw`, `stosd`
  - `lodsb`, `lodsw`, `lodsd`
  - `cmpsb`, `cmpsw`, `cmpsd`
  - `scasb`, `scasw`, `scasd`
  - `rep`, `repe`, `repne`
  - direction-flag behavior through `cld` and `std`
- FPU instructions such as `fld`, `fstp`, `fadd`, `fsub`, `fmul`, and `fdiv`.
- SSE/AVX instructions and vector registers.
- Segment registers, far pointers, and segment override behavior.
- Interrupt and hardware/OS instructions such as `int`, `iret`, `in`, `out`, `hlt`, `cli`, and `sti`.
- Atomic/concurrency instructions such as `lock`, `cmpxchg`, and `xadd`.
- BCD/ASCII adjust instructions such as `aaa`, `aas`, `daa`, `das`, `aam`, and `aad`.

Additional instructions may be added later as separate, tested compatibility milestones.

### 8.7 Unsupported Initially

Initially unsupported:

- Full MASM macro language.
- Full conditional assembly.
- `.IF`, `.ELSE`, `.ENDIF`, `.WHILE`, `.REPEAT` high-level MASM constructs.
- `INVOKE`, `PROTO`, and full calling-convention modeling.
- Full expression parsing, including arbitrary arithmetic expressions and parenthesized expressions.
- Nested `DUP` initializers.
- `.data?`, unless added as a later data-layout compatibility feature.
- Full scaled-index addressing, until the staged memory-operand milestones reach it.
- FPU instructions.
- SSE/AVX instructions.
- String instructions and `rep` prefixes, unless added in a later dedicated string-instruction milestone.
- Segment registers and segment override behavior.
- Interrupts.
- Windows API calls.

### 8.8 MASM Compatibility Coverage Notes

The current target is **educational MASM32/Irvine32 compatibility**, not full MASM. MASM includes many directive families and operators beyond the initial subset, including conditional assembly, high-level conditional control-flow directives, equates, macros, procedure/prototype directives, segment directives, structure/record directives, repeat blocks, listing controls, and simplified segment directives. These should be treated as staged roadmap items or explicit non-goals, not implicit behavior.

Important textbook/compatibility areas to track explicitly:

- Compatibility corrections for existing syntax: signed `PTR` aliases, all-GPR base registers, and global memory-width resolution.
- Equates and constants: `=`, `EQU`, limited `TEXTEQU`, and expression-backed constants.
- Additional data sections: `.DATA?` and `.CONST`, with deterministic simulator behavior for uninitialized storage and optional read-only metadata for constants.
- Additional non-integer data declarations: `REAL4`, `REAL8`, `REAL10`, `TBYTE`, and possibly `FWORD`. These remain deferred unless a floating-point/data-layout phase explicitly adds them.
- Nested `DUP` and initializer expressions.
- Structure support: `STRUCT`, `UNION`, `RECORD`, field access, `TYPEDEF`, `WIDTH`, `MASK`, and structure initializers.
- Procedure metadata: `USES`, `PROTO`, `INVOKE`, `LOCAL`, parameters, `ADDR`, calling-convention modeling, and root procedure termination.
- High-level MASM flow: `.IF`, `.ELSE`, `.ELSEIF`, `.ENDIF`, `.WHILE`, `.ENDW`, `.REPEAT`, `.UNTIL`, `.UNTILCXZ`, `.BREAK`, `.CONTINUE`.
- Anonymous labels: `@@`, `@B`, and `@F`.
- Conditional assembly: `IFDEF`, `IFNDEF`, `IFE`, `IFB`, `IFNB`, `ELSE`, `ENDIF`, and related compile-time directives.
- Macro system: `MACRO`, `ENDM`, macro parameters, `LOCAL`, `EXITM`, `PURGE`, repeat/for blocks, expansion limits, and recursion protection.
- Selected Irvine/Macros.inc convenience macros may be added as built-ins, but the full MASM macro language is not a v1 requirement.
- MASM32 header compatibility: `.386`, `.486`, `.586`, `.686`, `.model flat, stdcall`, `.stack`, `INCLUDE Irvine32.inc`, `INCLUDE Macros.inc`, `OPTION CASEMAP:NONE`, `TITLE`, `SUBTITLE`, and `PAGE` should be accepted as compatibility/header directives. They should not imply full processor, listing, object-file, or OS behavior.
- Include/library declarations: broader `INCLUDE`, `INCLUDELIB`, `EXTERN`, `EXTERNDEF`, `EXTRN`, `PUBLIC`, and `COMM` handling. Only built-in virtual includes are accepted initially.
- Expression parser: `+`, `-`, `*`, `/`, `MOD`, `SHL`, `SHR`, `AND`, `OR`, `XOR`, `NOT`, relational operators, parentheses, `HIGH`, `LOW`, `HIGHWORD`, `LOWWORD`, `SHORT`, `THIS`, and segment-related operators where applicable.
- Instruction prefixes and string instructions: `REP`, `REPE`, `REPNE`, `LOCK`, `movsb`, `movsd`, `stosb`, `stosd`, `lodsb`, `cmpsb`, `scasb`, and direction-flag behavior through `cld` and `std`.
- Extended flag model: `PF`, `AF`, and `DF`, with updated arithmetic/logical/test helpers and debugger/Irvine display.
- Irvine32 runtime compatibility: virtual include symbols, `exit`, output routines, input routines with flag semantics, debug routines, random routines, console-control policy, and explicit unsupported diagnostics for file routines and Windows-specific routines.

These features should not be silently accepted before they are implemented. Unsupported forms should produce explicit diagnostics with source location.

## 9. Register Model

### 9.1 32-bit Mode Canonical Registers

Canonical stored registers:

- `EAX`
- `EBX`
- `ECX`
- `EDX`
- `ESI`
- `EDI`
- `EBP`
- `ESP`
- `EIP`
- `EFLAGS`

Aliases should be derived dynamically, not stored independently.

Displayed aliases:

- `EAX`, `AX`, `AH`, `AL`
- `EBX`, `BX`, `BH`, `BL`
- `ECX`, `CX`, `CH`, `CL`
- `EDX`, `DX`, `DH`, `DL`
- `ESI`, `SI`
- `EDI`, `DI`
- `EBP`, `BP`
- `ESP`, `SP`
- `EIP`

`IP` may be optionally displayed in an advanced alias mode.

### 9.2 Extended 32-bit Canonical Registers

Canonical stored registers:

- `RAX`
- `RBX`
- `RCX`
- `RDX`
- `RSI`
- `RDI`
- `RBP`
- `RSP`
- `R8` through `R15`
- `RIP`
- `RFLAGS`

Displayed aliases:

- `RAX`, `EAX`, `AX`, `AH`, `AL`
- `RBX`, `EBX`, `BX`, `BH`, `BL`
- `RCX`, `ECX`, `CX`, `CH`, `CL`
- `RDX`, `EDX`, `DX`, `DH`, `DL`
- `RSI`, `ESI`, `SI`
- `RDI`, `EDI`, `DI`
- `RBP`, `EBP`, `BP`
- `RSP`, `ESP`, `SP`
- `R8`, `R8D`, `R8W`, `R8B`
- `R9`, `R9D`, `R9W`, `R9B`
- `R10`, `R10D`, `R10W`, `R10B`
- `R11`, `R11D`, `R11W`, `R11B`
- `R12`, `R12D`, `R12W`, `R12B`
- `R13`, `R13D`, `R13W`, `R13B`
- `R14`, `R14D`, `R14W`, `R14B`
- `R15`, `R15D`, `R15W`, `R15B`
- `RIP`

In Extended 32-bit Mode, writing to a 32-bit subregister such as `EAX` should zero-extend into the corresponding 64-bit register unless the project explicitly chooses a simplified non-x64 behavior. The default should be real x86-64-style zero-extension.

### 9.3 Register Display

The register table should group aliases:

```text
RAX  0000000000000014h / 20
  EAX 00000014h / 20
    AX 0014h / 20
      AH 00h / 0
      AL 14h / 20
```

The UI should support collapsing groups to avoid clutter.

Default number formats:

- Hexadecimal.
- Unsigned decimal.

Optional formats:

- Signed decimal.
- Binary.
- Character view for bytes.

## 10. Flag Model

Initial flags:

- `CF` carry flag
- `ZF` zero flag
- `SF` sign flag
- `OF` overflow flag

Later flags:

- `PF` parity flag
- `AF` auxiliary carry flag
- `DF` direction flag, especially if string instructions are added

Flag behavior must be tested carefully for arithmetic, comparisons, signed jumps, and unsigned jumps.

## 11. Memory Model

### 11.1 Regions

The VM should use deterministic simulated memory regions.

Example default layout:

```text
.code   base 0x00400000
.data   base 0x00500000
.heap   base 0x00600000
.stack  top  0x00800000, grows downward
```

Actual sizes are configurable through project settings and local safety limits.

### 11.2 Permissions

Recommended permissions:

```text
.code
  read: yes
  write: no
  execute: yes

.data
  read: yes
  write: yes
  execute: no

.heap
  read: yes
  write: yes
  execute: no

.stack
  read: yes
  write: yes
  execute: no
```

Writes to `.code` should fail unless a future advanced option enables self-modifying code.

Execution from `.data`, heap, or stack should fail by default.

### 11.3 Bounds Checking

Every memory access must go through checked helpers.

Examples:

```c
bool vm_read_u8(Vm *vm, uint32_t address, uint8_t *out_value);
bool vm_write_u32(Vm *vm, uint32_t address, uint32_t value);
```

Errors should include:

- Access type.
- Address.
- Size.
- Source line.
- Instruction text.
- Closest known symbol, if any.

### 11.4 Lazy Allocation

The simulator should support lazy page allocation, especially for large virtual memory settings.

Separate concepts:

- Virtual memory limit: the simulated address range available to the MASM program.
- Committed memory limit: real browser/Wasm memory actually allocated for touched pages.

This enables large virtual regions without immediately allocating huge browser memory.

### 11.5 Unaligned Access

Unaligned reads and writes should be simulated correctly for normal integer operations.

Example:

```asm
mov esi, OFFSET arr
inc esi
mov eax, DWORD PTR [esi]
```

The VM should perform the read but emit a simulator warning:

```text
Warning: unaligned DWORD read at 00500001h.
```

The warning should appear in Simulator Messages, not Program Console.

## 12. Stack Model

The stack is a memory region that grows downward.

Default stack size:

```text
64 KiB
```

`ESP` or `RSP` should be initialized to the top of the stack region.

Stack overflow occurs when stack movement or memory access crosses below the configured stack region.

The debugger should display:

- Current stack pointer.
- Current stack usage.
- Peak stack usage.
- Stack remaining.
- Optional warning when peak usage exceeds a threshold, such as 80% or 90%.

A separate call-depth watchdog may be provided for clearer recursion diagnostics, but the stack size remains the hard correctness boundary.

## 13. Irvine32 Runtime Support

Irvine32 routines should be simulated as VM intrinsics, not as real library code.

When the VM sees:

```asm
call WriteString
```

it should intercept the call and execute the corresponding simulated routine if `WriteString` is a supported Irvine32 intrinsic.

### 13.1 Virtual Irvine32 Include

`INCLUDE Irvine32.inc` is a built-in virtual include. It should not read host files. When this include is present, the parser should register known Irvine32 routine names as virtual intrinsic procedure symbols.

Known Irvine32 symbols should be classified as:

- supported intrinsic;
- known but unsupported Irvine32 routine;
- unsupported file I/O routine;
- unsupported Windows/console-control routine;
- unknown symbol.

Unsupported known Irvine32 routines should produce diagnostics such as `unsupported-irvine32-routine`, not generic `unknown-symbol` diagnostics.

`INCLUDE Macros.inc` is accepted as a virtual no-op for paste compatibility. Macro invocations remain unsupported until selected Irvine macro compatibility is implemented.

### 13.2 Program Termination and Irvine `exit`

Many Irvine32 textbook programs end with:

```asm
exit
main ENDP
END main
```

The simulator should treat `exit` as a built-in Irvine32 compatibility pseudo-instruction or virtual macro that terminates VM execution successfully.

`exit` must not imply real `ExitProcess`, Windows API execution, PE loading, or process behavior.

Program termination policy:

- execution starts at the `END` entry symbol;
- falling off the synthetic end of the entry procedure may terminate successfully in educational mode;
- `exit` terminates successfully;
- `RET` from the entry procedure should terminate successfully in educational mode once `RET` exists, unless a later phase deliberately chooses a root-return diagnostic policy;
- `RET` from a non-entry procedure must obey the call stack model;
- calls to `ExitProcess` or other Windows API routines remain unsupported unless explicitly shimmed later.

### 13.3 Supported Irvine32 Routine Groups

The implementation guide owns exact phase ordering. The v1 compatibility target should include these groups.

Basic output:

- `Crlf`
- `WriteString`
- `WriteChar`

Numeric output:

- `WriteInt`
- `WriteDec`
- `WriteHex`
- `WriteBin`

Input:

- `ReadChar`
- `ReadInt`
- `ReadDec`
- `ReadHex`
- `ReadString`

Debug and utility routines:

- `DumpRegs`
- `DumpMem`
- `Randomize`
- `RandomRange`
- `Random32`
- `WaitMsg`

Console-control routines should have an explicit policy:

- `Clrscr`: either clear Program Console or emit a deterministic console-control event.
- `Gotoxy`: either model cursor metadata or no-op with an informational simulator warning.
- `SetTextColor`: either model text-style metadata or no-op with an informational simulator warning.
- `Delay`: must not block the browser thread; simulate deterministically or no-op with a warning.

String helper routines may be added later:

- `Str_length`
- `Str_copy`
- `Str_compare`
- `Str_trim`
- `Str_ucase`

File-related Irvine32 routines are unsupported in v1 unless a virtual filesystem phase explicitly adds them:

- `OpenInputFile`
- `CreateOutputFile`
- `ReadFromFile`
- `WriteToFile`
- `CloseFile`

File-routine diagnostics should say that real host filesystem access is unavailable to simulated programs.

### 13.4 Routine Contracts

Each routine must have a documented contract.

Example: `WriteString`

```text
Input:
  EDX = address of null-terminated string

Effect:
  Reads bytes from simulated memory until a null terminator.
  Appends the decoded string to Program Console.

Errors:
  Invalid EDX pointer.
  Missing null terminator before readable memory ends.
  Output limit exceeded.
```

Example: `ReadString`

```text
Input:
  EDX = destination buffer
  ECX = maximum character count

Effect:
  VM enters WAITING_FOR_INPUT state.
  UI enables input box.
  Submitted input is written into simulated memory.
  EAX receives character count.

Errors:
  Invalid destination pointer.
  ECX too large for writable memory.
  Input cancelled by user.
```

Input routines must define flag behavior because textbook validation loops depend on flags.

Example: `ReadInt`

```text
Input:
  program console input line

Effect:
  Parses a signed 32-bit integer.
  EAX receives the parsed value on success.
  OF is cleared on valid input.
  OF is set on invalid or out-of-range input.
```

Example: `ReadDec`

```text
Input:
  program console input line

Effect:
  Parses an unsigned 32-bit integer.
  EAX receives the parsed value on success.
  CF is cleared on valid input.
  CF is set on invalid or out-of-range input.
```

`DumpRegs` and `DumpMem` write to Program Console, not Simulator Messages. `DumpRegs` should use current VM state and initially show the modeled flags `CF`, `ZF`, `SF`, and `OF`; after the extended flag phase it should also include newly modeled flags where appropriate.

## 14. Console and Input Model

### 14.1 Separate Output Streams

There must be two separate panels:

```text
Program Console
  User program output and interactive input.

Simulator Messages
  Assembly errors, warnings, runtime errors, resource-limit stops, unsupported-feature messages, and VM diagnostics.
```

The simulated program must not be able to print into Simulator Messages.

### 14.2 Input Handling

When an input routine is reached:

```text
RUNNING -> WAITING_FOR_INPUT
```

During input wait:

- Active execution timer pauses.
- Instruction counter stops increasing.
- Stop button remains available.
- Program Console input field becomes active.

When input is submitted:

```text
WAITING_FOR_INPUT -> RUNNING
```

Input cancellation should stop the program with a structured reason:

```text
Execution stopped: input cancelled by user.
```

### 14.3 Output Limits

Output limits should support both bytes and lines.

Default limits:

```text
Max output bytes: 1 MiB
Max output lines: 10,000
Default action: stop program
```

Supported actions when exceeded:

- Stop program.
- Keep latest output and discard oldest output.
- Pause and ask the user what to do.

A simple clear-and-continue action is not preferred because it hides the issue and can confuse users.

## 15. VM Execution State

VM states:

- `IDLE`
- `ASSEMBLING`
- `READY`
- `RUNNING`
- `WAITING_FOR_INPUT`
- `PAUSED`
- `BREAKPOINT_HIT`
- `STOPPED`
- `HALTED`
- `CRASHED`

Transitions should be explicit and testable.

## 16. Browser Execution and Worker Model

The VM should run inside a Web Worker.

Main thread responsibilities:

- Editor rendering.
- UI controls.
- Program Console display.
- Simulator Messages display.
- Debugger display.
- URL import/export.

Worker responsibilities:

- Loading WebAssembly.
- Parsing and assembling source.
- Running VM chunks.
- Enforcing instruction/time/output/memory limits.
- Sending structured events to the UI.

### 16.1 Stop Button

The Stop button should support:

1. Soft stop: send a stop request to the worker/VM.
2. Hard stop: terminate and recreate the worker if the worker does not respond.

The UI should always allow stopping, including while waiting for input.

## 17. Execution Limits

### 17.1 Instruction Limit

Default:

```text
Enabled: yes
Limit: 1,000,000 executed VM instructions
```

This counts executed VM instructions, not source lines.

When exceeded:

```text
Execution stopped: instruction limit exceeded.
Limit: 1,000,000 instructions
Stopped at line: <line>
Instruction: <instruction>
```

### 17.2 Active Time Limit

Default:

```text
Enabled: yes
Limit: 10 seconds active VM execution time
```

The timer pauses while the VM is waiting for user input.

### 17.3 Optional Input Wait Timeout

Default:

```text
Disabled
```

Useful for automated testing or classroom scenarios.

When exceeded:

```text
Execution stopped: input wait timeout.
```

### 17.4 Chunked Execution

The worker should run VM instructions in chunks rather than one unbroken loop.

Example:

```text
Run 10,000 instructions
Flush output/messages
Check stop request
Check time limit
Yield to worker event loop
Repeat
```

This keeps Stop responsive and allows periodic UI updates.

## 18. Memory Configuration and Safety Tiers

### 18.1 User-Configurable Memory Controls

Expose controls for:

- Stack size.
- `.data` limit.
- Heap size.

Use a dropdown plus exact byte textbox.

The textbox is the source of truth. The dropdown only populates common values and changes to `Custom` if the textbox value differs.

Suggested normal presets:

- 4 KiB
- 16 KiB
- 64 KiB
- 256 KiB
- 1 MiB
- 4 MiB
- 16 MiB
- 64 MiB
- 128 MiB
- Custom

Suggested extended presets:

- 256 MiB
- 512 MiB
- 1 GiB

### 18.2 Safety Tiers

Recommended tiers:

```text
Normal mode:
  max total virtual memory: 256 MiB

Extended memory mode:
  max total virtual memory: 1 GiB

Super-extended memory mode:
  max total virtual memory: up to 4 GiB
  local-only
  off by default
  never saved into shared project state
  requires confirmation when enabled
  requires confirmation before running oversized projects
```

### 18.3 Super-Extended Memory Confirmation

When enabling super-extended memory:

```text
Super-extended memory mode allows projects to request very large simulated memory regions.

This may slow down, freeze, or crash this browser tab if the program touches too much memory. This setting is local to this browser session and is not saved into shared project links.

Enable super-extended memory mode?
```

Require a checkbox:

```text
[ ] I understand that this may freeze or crash the tab.
```

When running a project that exceeds normal/extended limits:

```text
This project requests <N> of simulated memory.
Current local mode permits this, but running the program may allocate large amounts of real browser memory depending on what the program does.

Run anyway?
```

## 19. Debugger Model

### 19.1 Controls

Required controls:

- Run.
- Stop.
- Reset.
- Step Into.
- Step Over.
- Continue.
- Breakpoints.

Definitions:

```text
Run:
  Execute until halt, crash, breakpoint, input wait, stop request, or resource limit.

Step Into:
  Execute exactly one VM instruction.

Step Over:
  If current instruction is CALL, execute until the return address is reached or until a stop condition occurs.
  If not CALL, behaves like Step Into.

Continue:
  Resume from pause or breakpoint.

Stop:
  Request execution stop; hard-terminate worker if unresponsive.

Reset:
  Rebuild VM from source and clear runtime state.
```

### 19.2 Debugger Panels

Required panels:

- Source editor with current line highlight.
- Register table.
- Flags table.
- Program Console.
- Simulator Messages.
- Last Step Changes.
- Memory Changes.
- Stack summary.

Optional advanced panels:

- Raw memory viewer.
- Watch variables.
- Call stack.
- Symbol table.
- Instruction trace.

### 19.3 Last-Step Delta

Each step should produce a structured delta showing:

- Instruction executed.
- Registers changed.
- Register aliases changed.
- Flags changed.
- Memory changed.
- Program output produced.
- Simulator warnings produced.
- Instruction count.
- Active execution time.

Example:

```text
Last instruction:
  mov eax, 20

Register changes:
  EAX: 00000000h / 0 -> 00000014h / 20
  AX:  0000h / 0     -> 0014h / 20
  AL:  00h / 0       -> 14h / 20
```

Unchanged aliases should be hidden by default.

### 19.4 Step-Over Delta

For Step Over, the delta is the aggregate difference from before the call to after it returns.

Example:

```text
Step-over result:
  call SomeProc

Instructions executed inside call: 17

Register changes:
  EAX: 00000000h / 0 -> 0000002Ah / 42
  ECX: 00000005h / 5 -> 00000000h / 0

Memory changes:
  result DWORD: 00000000h / 0 -> 0000002Ah / 42
```

## 20. Memory Change Display

### 20.1 Default Display

By default, show only changed memory for the last step.

Example:

```asm
.data
var BYTE 0
.code
mov var, 100
```

Display:

```text
Memory changes:
  var BYTE
    address: 00500000h
    byte offset: +0
    00h / 0 -> 64h / 100
```

### 20.2 Arrays and Byte Offsets

MASM-style array offsets are byte offsets. Parsing and executing indexed operands such as `nums[8]` belongs to the staged memory-operand implementation; this section defines how those writes should be displayed once they are available.

Example:

```asm
.data
nums DWORD 10 DUP(0)
.code
mov nums[8], 100
```

Display:

```text
Memory changes:
  nums + 8 DWORD
    address: 00500008h
    byte offset: +8
    element index: 2
    00000000h / 0 -> 00000064h / 100
```

If the address is unaligned inside an element:

```text
nums + 9
  element: nums[2] + 1 byte
```

### 20.3 Logical and Byte-Level Views

Internally record raw byte changes. The UI may group them by logical write.

For a DWORD write:

```text
DWORD write at 00500001h
old: 05040302h
new: 12345678h
```

Expanded byte view:

```text
arr + 1 BYTE: 02h / 2 -> 78h / 120
arr + 2 BYTE: 03h / 3 -> 56h / 86
arr + 3 BYTE: 04h / 4 -> 34h / 52
arr + 4 BYTE: 05h / 5 -> 12h / 18
```

### 20.4 Strings

For byte arrays that look like strings, show a text interpretation.

Example:

```text
buffer BYTE[6]
  hex: 68 65 6C 6C 6F 00
  text: "hello"
```

Large string changes should be collapsed by default.

## 21. Error, Warning, and Diagnostic Model

All errors should be structured internally.

Example:

```json
{
  "kind": "runtime-error",
  "code": "invalid-memory-read",
  "message": "Invalid memory read at 0x0050FFFF.",
  "file": "main.asm",
  "line": 18,
  "instruction": "mov eax, [ebx]",
  "address": "0x0050FFFF",
  "registers": {
    "eax": "0x00000000",
    "ebx": "0x0050FFFF",
    "esp": "0x007FFFE8"
  }
}
```

Categories:

- `assembly-error`
- `link-load-error`
- `runtime-error`
- `resource-limit-error`
- `user-stopped`
- `unsupported-feature`
- `simulator-warning`
- `internal-simulator-error`

Unsupported features should be explicit:

```text
Unsupported feature: Windows API calls are not available in this simulator.
Line 14: invoke MessageBox, NULL, ADDR msg, ADDR title, MB_OK
```

Lexer diagnostics must not be collapsed into generic umbrella messages when the lexer can identify the actual problem. For example, the UI should prefer diagnostics such as:

```text
[assembly-error] invalid-hex-literal line 3, column 14: Invalid hexadecimal literal.
[assembly-error] unterminated-string line 7, column 18: Unterminated string literal.
[assembly-error] unexpected-character line 9, column 5: Unexpected character.
```

over a generic message such as:

```text
[assembly-error] lexer-failed line 1, column 1: Lexer failed or produced diagnostics before parsing.
```

A generic `lexer-failed` diagnostic may be retained internally as a summary/status code, but it must not be the only user-visible diagnostic when more specific lexer diagnostics are available.

The parser should eventually support diagnostic recovery for known unsupported constructs. Recovery must be conservative:

- Recover from known unsupported line-level constructs by skipping to the next line.
- Recover from known unsupported block constructs by skipping to the matching terminator where practical.
- Avoid cascading noise from inside skipped unsupported constructs.
- Cap the number of diagnostics reported in one pass.
- Never execute a program if any assembly diagnostic was produced.
- Stop immediately on fatal capacity, lexer state, or internal parser errors.

Recoverable unsupported constructs include common textbook/compiler forms such as `STRUCT`, `UNION`, `MACRO`, `INVOKE`, `.IF`, `.WHILE`, `.REPEAT`, `.DATA?`, `.CONST`, `EQU`, `TEXTEQU`, `PROTO`, `LOCAL`, `INCLUDELIB`, `EXTERN`, `PUBLIC`, and `COMM`.

## 22. Save and Share URL Format

Use compressed encoded project state in the URL fragment.

Recommended shape:

```text
https://example.com/#v=1&name=hello-world-v1&state=<compressed-base64url-json>
```

The readable `name` parameter is for humans. The trusted project name comes from the decoded state.

Recommended state shape:

```json
{
  "schema": 1,
  "name": "hello-world-v1",
  "files": {
    "main.asm": "..."
  },
  "settings": {
    "mode": "masm32-educational",
    "entryPoint": "main",
    "memory": {
      "stackSize": 65536,
      "dataSize": 1048576,
      "heapSize": 1048576
    },
    "execution": {
      "instructionLimitEnabled": true,
      "instructionLimit": 1000000,
      "timeLimitEnabled": true,
      "timeLimitMs": 10000
    },
    "console": {
      "outputLimitEnabled": true,
      "maxOutputBytes": 1048576,
      "maxOutputLines": 10000,
      "onLimitExceeded": "stop"
    },
    "debugger": {
      "breakpoints": []
    }
  }
}
```

Saved in shared project state:

- Project name.
- Source files.
- Execution mode.
- Entry point.
- Memory sizes requested by the project.
- Execution limits requested by the project.
- Console limits requested by the project.
- Breakpoints.

Not saved:

- Super-extended memory permission.
- Developer overrides.
- Local committed-memory cap.
- Current runtime state.
- Program output.
- Simulator messages.
- Temporary input text.
- UI theme and CodeMirror theme selection.
- Editor local preferences such as folded panels, font size, and local-only editor options.
- Scroll positions.

## 23. Project File Model

Version 1 may be single-file, but the internal state should support multiple files from the start.

Project:

```text
name
files
active file
settings
breakpoints
```

Built-in virtual includes:

- `Irvine32.inc`, simplified to declarations recognized by the simulator.

Future virtual project files:

- `main.asm`
- `.inc` files
- virtual input/output text files

## 24. Security and Safety Model

The simulator runs untrusted user code inside an emulated VM, not directly in the browser environment.

Safety rules:

- Simulated code cannot access real files.
- Simulated code cannot access network APIs.
- Simulated code cannot call browser APIs.
- Simulated code cannot execute JavaScript.
- All memory access is VM-checked.
- All execution occurs in a Web Worker.
- Worker can be terminated by the main UI.
- Resource limits are enforced by the VM and worker.
- Large memory modes require local consent.

## 25. Documentation Requirements

All C and TypeScript/JavaScript code should be documented consistently. The core implementation language is C99; examples in this section use C and should not be converted to C++.

### 25.1 File Header Documentation

Each source file should start with a block comment:

```c
/*
 * @file vm_memory.c
 * @brief Checked memory access and lazy page allocation for the MASM simulator VM.
 *
 * This file owns the simulated address-space model. All reads and writes from
 * VM instructions must pass through this module so bounds checks, permissions,
 * unaligned-access warnings, and memory-change recording remain centralized.
 */
```

### 25.2 Doxygen-Style Symbol Documentation

Use triple-slash documentation for functions, structs, enums, and public module APIs.

Example:

```c
/// Describes the result of a checked VM memory access.
typedef enum VmMemoryResult {
    VM_MEMORY_OK,
    VM_MEMORY_INVALID_ADDRESS,
    VM_MEMORY_PERMISSION_DENIED,
    VM_MEMORY_COMMITTED_LIMIT_EXCEEDED
} VmMemoryResult;

/// Reads a 32-bit little-endian value from simulated memory.
///
/// @param vm The VM instance that owns the simulated memory regions.
/// @param address The simulated address to read from.
/// @param out_value Receives the decoded 32-bit value on success.
/// @return VM_MEMORY_OK on success, or an error code describing the failure.
VmMemoryResult vm_memory_read_u32(Vm *vm, uint32_t address, uint32_t *out_value);
```

### 25.3 Documentation Policy

Every new public function, struct, enum, and module-level API must include Doxygen documentation.

Internal helpers should be documented when their behavior is non-obvious.

Comments should explain intent and invariants, not restate simple code.

## 26. Testing Requirements

Tests are required from the beginning.

Minimum groups:

- Parser tests.
- Data declaration tests.
- Numeric literal tests, including negative decimal and hexadecimal literals.
- Character literal tests once character literals are enabled, including single-character literals, packed multi-character literals, width-overflow cases, data declaration cases, and instruction immediate cases.
- Register alias tests.
- Instruction tests.
- Flag behavior tests.
- Direct symbol memory operand tests.
- Indexed and symbol-relative memory operand tests.
- `PTR`, `OFFSET`, `TYPE`, `LENGTHOF`, and `SIZEOF` tests.
- Signed and unsigned jump tests.
- Stack tests.
- Memory bounds tests.
- Lazy allocation tests.
- Irvine32 routine tests.
- Input waiting/resume tests.
- Output limit tests.
- Execution limit tests.
- URL encode/decode tests.
- Debugger step/delta tests.

Flag tests are especially important.

Example:

```asm
mov eax, 0FFFFFFFFh
add eax, 1
```

Expected:

```text
EAX = 00000000h
ZF = 1
CF = 1
OF = 0
```

## 27. Implementation Phasing Guidance

The incremental implementation guide intentionally splits large simulator/editor features into small numbered phases. This specification defines the final behavior; the guide defines the order and granularity of implementation.

Large multi-feature implementation passes should be avoided. Each implementation phase should remain independently testable and should preserve previous milestone behavior.

Important split areas:

- Compatibility corrections should be implemented before new instruction groups when they affect already-implemented syntax. Signed `PTR` aliases, all-GPR base registers, and global memory-width resolution are corrections to existing memory-operand behavior.
- Header directives should remain separate from memory/addressing corrections. `.386`, `.model`, `.stack`, `INCLUDE Irvine32.inc`, `OPTION CASEMAP:NONE`, `TITLE`, `SUBTITLE`, and `PAGE` are compatibility/header work, not instruction work.
- Data compatibility should be staged: `.DATA?` and `.CONST`, then equates and simple constant expressions, then extended expressions, then nested `DUP` and initializer expressions.
- Memory operand support should be implemented incrementally: constant symbol offsets, `PTR` width overrides, register-indirect operands, all-GPR bases, global width resolution, and later scaled-index addressing.
- Data operators and literals should be implemented incrementally: `TYPE`, then `LENGTHOF`, then `SIZEOF` together with single-character and packed multi-character literals, then compatibility aliases such as `LENGTH` and `SIZE`.
- Diagnostic quality should be implemented incrementally: first surface real lexer/parser diagnostics, then add conservative multi-diagnostic recovery for known unsupported constructs, then add feature-specific diagnostics for recognized planned compatibility features.
- Control flow should be implemented incrementally: labels/`JMP`, then `CMP` and equality jumps, then signed/unsigned jumps, then anonymous labels, then `SETcc`, then `LOOP` and instruction limits.
- Stack and procedure support should be implemented incrementally: stack initialization with `PUSH`/`POP`, then `CALL`/`RET`, then root termination and call-target classification, then `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, and `ADDR`.
- Irvine32 support should be implemented incrementally: virtual include symbols and `exit`, console infrastructure, basic text output, numeric output, debug/utilities, input protocol, simple input, then string input and buffer safety.
- Extended flags should be added before string instructions that depend on `DF`; logical/arithmetic/test helpers and debugger/Irvine displays should be updated together.
- High-level MASM flow should be implemented only after low-level control flow and expression parsing are stable.
- Structures and records should be implemented after data layout, expression support, and `TYPE`/`SIZEOF` behavior are stable.
- Debugger support should be implemented incrementally: Step Into backend, current-state UI, last-step delta UI, execution stats, breakpoints, Continue, Step Over backend, and Step Over aggregate delta display.
- CodeMirror editor support should be implemented incrementally: source editor replacement, MASM highlighting, indentation, dark/light local preferences, diagnostics integration, and debugger/breakpoint integration.

Every guide phase should specify:

- exact syntax accepted;
- exact syntax rejected;
- whether behavior is runtime, metadata-only, accepted no-op, or virtual built-in;
- expected diagnostic codes and wording category;
- acceptance programs;
- regression tests for previously implemented behavior.

## 28. Future Roadmap

The implementation guide now assigns most v1-relevant textbook MASM/Irvine32 features to concrete phases. Features below remain either late roadmap, optional post-v1 work, or explicit non-goals unless a later specification revision promotes them.

Concrete v1 roadmap themes:

- MASM compatibility corrections for existing memory syntax.
- MASM32 header directives.
- `.DATA?` and `.CONST` data sections.
- Numeric equates and constant expressions.
- Nested `DUP` and initializer expressions.
- Virtual Irvine32 include symbols and `exit`.
- Core instruction expansion.
- Control flow, anonymous labels, and loop helpers.
- Stack, calls, procedures, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, and `ADDR`.
- Program console and Irvine32 output/input/debug routines.
- Extended flags and string instructions.
- COMMENT/listing no-ops and compatibility operators.
- High-level MASM flow lowering.
- STRUCT/RECORD data modeling.
- Selected Irvine/Macros.inc convenience macros.
- Debugger and editor polish.

Optional or post-v1 roadmap:

- Full MASM macro language.
- Full conditional assembly.
- Full text substitution semantics for `TEXTEQU`.
- Complete listing file generation.
- Complete object/linkage model.
- FPU support.
- SSE/AVX subset.
- Watch variables.
- Data breakpoints/watchpoints.
- Raw memory hex editor.
- Multi-file project editor.
- Virtual filesystem.
- Download/upload project archives.
- Backend snippet sharing with short URLs.
- Optional UASM/JWasm investigation.

Explicit non-goals unless the project definition changes:

- native x86 binary execution;
- PE loading;
- real Windows API execution;
- real host filesystem access from simulated programs;
- object-file linking;
- full Windows process or console emulation;
- full x64/ml64 compatibility.
