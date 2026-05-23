# Online MASM32 Educational Simulator - Full Implementation Specification

> **Canonical source-of-truth note:** This file is paired with `INCREMENTAL_IMPLEMENTATION_GUIDE.md`. Together they are the active post-Milestone-30 overhauled source-of-truth documents unless superseded by a later reviewed canonical pair. This specification owns product boundaries and stable behavior; the paired implementation guide owns phase numbering, implementation tasks, and required tests.


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
- Inspect registers, flags, memory changes, stack usage, and last-step deltas, including width-aware hexadecimal, unsigned decimal, and signed decimal interpretations for displayed integer register and memory values where the display width is known.
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

Required compiler posture for core and release builds:

```text
C standard: C99
Warnings: enabled and treated strictly for C99 core and test builds; browser-only tooling exceptions must be documented in the release report
Core ownership: explicit structs and functions
Allocation: deterministic and bounded in the C99 core; UI/browser wrapper allocations must have documented limits or release-report exceptions
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

### 7.1 Post-Milestone-30 Roadmap Integration Policy

The implementation guide owns the canonical post-Milestone-30 phase sequence. After Milestone 30, all guide phases are renumbered sequentially starting at Phase 31 and supersede the older post-30 roadmap wording.

The full specification owns stable behavior. If this specification gives a product-level rule and the implementation guide gives a phase-level task, both apply. If legacy roadmap text conflicts with the post-30 guide, the post-30 guide wins for phase sequencing and the thematic requirements in this specification win for behavior.

Post-30 implementation sessions must follow these rules:

- one focused implementation task per phase;
- no future-phase syntax or runtime behavior implemented as convenience work;
- accepted syntax, rejected syntax, diagnostics, tests, and non-goals stated per phase;
- structured diagnostics and final rendered Simulator Messages tests for every new user-visible diagnostic path;
- no silent no-op compatibility behavior unless explicitly listed as accepted no-op, metadata-only, or virtual built-in;
- no broad MASM, x64, WinAPI, PE, linker, or macro behavior implied by local compatibility features.

Phase-reference hygiene rule:

```text
When referencing another guide phase, include both the phase number and the phase title whenever practical.
```

Examples:

```text
Preferred: Phase 68 - Call Target Classification and Procedure Entry Metadata
Avoid:     Phase 51
```

The guide has gone through roadmap renumbering. A bare phase number in a later section can become stale. If a cross-reference is found to point to the wrong feature, treat it as a documentation defect and correct the reference without changing implementation behavior.

Future assistant rule:

```text
Do not implement a feature merely because a stale cross-reference names the wrong phase. Use the phase title, local scope, and current canonical guide sequence to determine the intended dependency.
```

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


Accepted no-op, metadata-only, and limited virtual compatibility constructs should produce default informational notices when the construct has meaningful real MASM behavior that the simulator intentionally does not perform.

The notice must explain the simulator behavior in concrete terms.

Examples:

```text
`.686` is accepted for MASM compatibility but does not change the simulator CPU mode.
`.model flat, stdcall` is accepted for MASM32 textbook compatibility but does not enable real linker, object-file, or Windows calling-convention behavior.
`.stack 4096` records stack-size metadata where implemented, but it does not by itself execute stack instructions or create procedure frames.
`INCLUDE Macros.inc` is accepted as a virtual compatibility include; general MASM macro expansion remains unsupported until a later macro phase.
`TITLE`, `SUBTITLE`, and `PAGE` are accepted as listing/documentation directives and do not affect VM execution.
```

These notices must be emitted through Simulator Messages, not Program Console.

They must not be assembly errors.

They must not be used for constructs with active simulator semantics unless the notice describes a real limitation. For example, `OPTION CASEMAP:NONE` has semantic behavior and should not be described as a no-op.

Diagnostics should distinguish:

- MASM-invalid syntax, such as ambiguous memory width where real MASM also requires a `PTR` override.
- planned-but-not-yet-implemented syntax, reported as `unsupported-feature` or a more specific unsupported code.
- unsupported runtime behavior, such as executable QWORD memory operations in MASM32 Educational Mode.
- explicit non-goals, such as `INCLUDE Windows.inc` or `INCLUDELIB kernel32.lib`.

Core classification rule:

```text
Do not describe MASM-invalid code as a temporary unsupported feature.
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

`.DATA?` and `.CONST` are implemented v1 MASM compatibility sections.

`.DATA?` behavior is mandatory:

- `.DATA?` creates writable storage for declarations whose initializers are exclusively `?` or `DUP(?)` after nested `DUP` support is available.
- Runtime bytes are deterministic zero at program load.
- Every byte allocated from `.DATA?` must retain uninitialized-origin metadata until overwritten by the simulated program.
- Reads and writes are allowed in default MASM32 Educational Mode.
- `OFFSET`, `TYPE`, `LENGTHOF`, `SIZEOF`, constant offsets, and later expression-backed offsets must work for `.DATA?` symbols using the same symbol metadata rules as `.data`.
- Initialized declarations in `.DATA?`, such as `x DWORD 5` or `buf BYTE ?, 1`, must produce structured assembly diagnostics. They must not be silently converted to zero-filled declarations.

`.CONST` behavior is mandatory:

- `.CONST` creates initialized read-only storage.
- `.CONST` must be protected by address range, not only by symbol metadata.
- The preferred implementation is a dedicated read-only `.const` VM memory region.
- An acceptable implementation may use a protected-range table, but every central VM memory write helper must check that table before committing writes.
- Every write whose final effective byte range overlaps `.CONST` storage must fail, even if only one byte overlaps.
- The check must apply after effective-address calculation and before memory mutation.
- The check must apply to direct symbol writes, symbol-offset writes, bracketed symbol writes, `OFFSET`-derived indirect writes, displacement writes such as `[eax + 3]`, arithmetic-derived addresses, and numeric/computed addresses that happen to land in `.CONST` storage.
- Parser/static diagnostics are required for obvious `.CONST` destinations such as `mov limit, 20`, `mov limits[4], 99`, `mov DWORD PTR [limit], 20`, `add limit, 1`, `neg limit`, and `xchg eax, limit`.
- Parser/static diagnostics are not sufficient. Runtime write protection must still be enforced by the memory write path.
- Reads from `.CONST` must work normally.
- Failed `.CONST` writes must not create successful memory-change rows.
- `.CONST` declarations must be initialized. `?` and `DUP(?)` belong in `.DATA?`, not `.CONST`.

Required runtime write algorithm:

```text
1. Resolve the final effective address for the memory operand.
2. Determine the write width in bytes.
3. Compute the inclusive write range [address, address + width - 1] with overflow checks.
4. Reject address-range overflow.
5. Reject accesses not fully contained in a valid memory region.
6. Reject missing write permission.
7. Reject any overlap with read-only `.CONST` storage.
8. Only then mutate memory and record memory changes.
```

Examples that must fail:

```asm
.CONST
limit DWORD 10

.code
main PROC
    mov limit, 20                 ; static diagnostic
    mov eax, OFFSET limit
    mov DWORD PTR [eax], 20        ; runtime diagnostic
main ENDP
END main
```

```asm
.CONST
pair DWORD 10, 20

.code
main PROC
    mov eax, OFFSET pair
    mov DWORD PTR [eax + 3], 99    ; runtime partial-overlap diagnostic
main ENDP
END main
```

```asm
.CONST
limit DWORD 10

.code
main PROC
    mov ebx, 500000h
    mov DWORD PTR [ebx + 500000h], 20 ; diagnostic if final range lands in `.CONST`
main ENDP
END main
```

Uninitialized-origin tracking is mandatory for storage created by `?`, `DUP(?)`, and `.DATA?`.

After the default teaching-diagnostics phase, uninitialized reads warn by default while still returning deterministic zero-filled bytes. Users may opt out of the warning through an explicit `off` policy, and strict/debug mode may stop execution before consuming uninitialized-origin bytes.

This warning policy does not make uninitialized storage random or nondeterministic. It reports that the bytes still carry uninitialized-origin metadata because the simulated program has not successfully written them yet.

#### 8.1.3 Accepted MASM32 Header / Compatibility Directives

Common MASM32 textbook headers should be accepted so students do not need to delete standard setup lines.

Accepted as no-op, virtual, metadata-only, or semantic compatibility directives in MASM32 Educational Mode:

- `.386`
- `.486`
- `.586`
- `.686`
- `.model flat, stdcall`
- `.stack`
- `.stack size`
- `INCLUDE Irvine32.inc`
- `INCLUDE Macros.inc`
- `OPTION CASEMAP:ALL`
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
- `OPTION CASEMAP:ALL` is accepted as an explicit selection of the default user-symbol case-insensitive policy.
- `OPTION CASEMAP:NONE` is accepted as a semantic compatibility directive that switches user-defined symbols to exact-case matching from that directive forward.
- `TITLE`, `SUBTITLE`, and `PAGE` are accepted as listing/documentation no-ops.

Default case policy:

- Instructions, registers, register aliases, directives, operators, data type names, `PTR` width names, virtual include names, and recognized Irvine32 routine names are case-insensitive.
- User-defined symbols are case-insensitive by default in MASM32 Educational Mode.
- User-defined symbol categories include data labels, numeric equates, code labels, procedure names, and later user-defined types, fields, aliases, and macros when those features are implemented.
- Case-insensitive user-symbol matching uses ASCII identifier folding only. The simulator must not use locale-sensitive or Unicode case mapping.
- In the default policy, user-symbol definitions whose names differ only by ASCII case are duplicates.
- In the default policy, user-symbol references may use any casing.

Supported `OPTION CASEMAP` forms:

- `OPTION CASEMAP:ALL` is accepted. It explicitly selects the default user-symbol policy: user-defined symbols are matched case-insensitively from that directive forward.
- `OPTION CASEMAP:NONE` is accepted. It selects exact-case user-symbol policy from that directive forward: user-defined symbols are matched case-sensitively.
- `OPTION CASEMAP:NOTPUBLIC` is recognized as a real MASM case-mapping mode, but it is not supported in v1 because the simulator does not yet implement `PUBLIC`, `EXTERN`, object-file linkage, or public/external name export semantics.
- Other `OPTION CASEMAP` values are invalid and must produce a structured diagnostic.

`OPTION CASEMAP` affects only user-defined symbols. It must not make instructions, registers, directives, operators, data type names, `PTR` width names, virtual include names, or recognized Irvine32 routine names case-sensitive.

The active `CASEMAP` policy is source-order based:

1. The parser starts in `CASEMAP:ALL` behavior.
2. A supported `OPTION CASEMAP` directive changes the active user-symbol lookup policy for declarations and references parsed after that directive.
3. The directive does not retroactively re-key, rename, merge, split, or remove symbols already accepted into symbol tables.
4. Each accepted symbol keeps its original source spelling for display and exact-case lookup.
5. Each lookup is resolved using the active policy at the reference location.

When `CASEMAP:ALL` is active:

- a new declaration conflicts with any already-accepted symbol in the same namespace whose name has the same ASCII-folded spelling;
- a reference resolves by ASCII-folded spelling;
- if the folded lookup matches exactly one accepted symbol, that symbol is used;
- if the folded lookup matches more than one accepted exact-case symbol, the simulator must emit `ambiguous-symbol` rather than choosing one.

When `CASEMAP:NONE` is active:

- a new declaration conflicts only with an already-accepted symbol in the same namespace whose spelling matches exactly;
- a reference resolves only by exact spelling;
- if no exact spelling match exists, the simulator must emit `unknown-symbol`.

Rejected declarations are not inserted into any symbol table. If a duplicate declaration is rejected under `CASEMAP:ALL`, a later `CASEMAP:NONE` reference to that rejected spelling may produce `unknown-symbol` if parser recovery continues. This diagnostic sequence is valid and not contradictory.

Changing between supported `CASEMAP:ALL` and `CASEMAP:NONE` directives is accepted for MASM compatibility. If a supported `CASEMAP` directive changes a previously selected supported `CASEMAP` policy, the simulator must emit a non-fatal warning with code:

```text
casemap-policy-changed
```

A `casemap-policy-changed` warning alone must not block execution. As usual, any assembly error emitted in the same run prevents execution.

Diagnostic classification:

- `OPTION CASEMAP:ALL`: supported.
- `OPTION CASEMAP:NONE`: supported.
- `OPTION CASEMAP:NOTPUBLIC`: recognized but unsupported; emit `unsupported-option`.
- Any other `CASEMAP` value: invalid; emit `invalid-option-value` if available, otherwise `unsupported-option` with wording that distinguishes invalid values from recognized-but-unsupported `NOTPUBLIC`.

Suggested invalid-value wording:

```text
Invalid CASEMAP value 'LOWER'. Supported values: ALL, NONE. Recognized but unsupported value: NOTPUBLIC.
```

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

- `?` reserves storage. Runtime bytes are deterministic zero-filled at program load while retaining metadata that the declaration was originally uninitialized until overwritten by the simulated program. Default MASM32 Educational Mode does not warn on these reads unless an explicit uninitialized-read warning or strict mode is enabled.
- `SBYTE`, `SWORD`, `SDWORD`, and `SQWORD` are signed integer data declarations. They use the same byte sizes as `BYTE`, `WORD`, `DWORD`, and `QWORD`, but their initializers are validated against signed ranges.
- `QWORD` and `SQWORD` data declarations, layout, and metadata are supported in MASM32 Educational Mode. Executable 64-bit memory operations and 64-bit registers remain deferred to Extended 32-bit Mode unless a later phase explicitly enables selected behavior.
- Flat `DUP`, nested `DUP`, `.DATA?`, `.CONST`, numeric equates, constant expressions, and expression-backed initializer values are part of the staged v1 roadmap and must be documented according to their current implemented phase status. They must not be described as permanently unsupported or future-only after their implementation phases are complete.
- Remaining deferred data-declaration families include non-integer or non-scalar types such as `REAL4`, `REAL8`, `REAL10`, `TBYTE`, `FWORD`, structures, records, unions, and typed fields unless a later phase explicitly implements them.
- Supported syntax documentation must reflect the latest completed milestone. Historical notes may say a feature was unsupported initially, but current support sections must not classify already-implemented behavior as unsupported.

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


#### 8.4.2 Constant Expression and Equate Evaluation Contract

Expression and equate phases must use one documented evaluator rather than ad hoc parsing in each feature.

Mandatory evaluator rules:

- Expressions are compile-time only unless a later runtime/high-level control-flow phase explicitly says otherwise.
- Numeric expression evaluation must use a signed 64-bit intermediate plus explicit overflow checks unless a later phase adopts a different documented width. Final values must still be validated against the destination context.
- Positive literals are allowed up to the unsigned width accepted by the destination context. Negative expression values are allowed only where the context already accepts negative values or signed declarations, and must be encoded with the existing two's-complement rules after range validation.
- Expression evaluation must not silently truncate.
- Division and `MOD` by zero must produce a structured diagnostic and must invalidate the equate/expression result so later uses do not cascade into misleading `unknown-symbol` or generic parse diagnostics.
- Unsupported operators must produce specific unsupported-expression diagnostics, not generic expected-line-end diagnostics.
- Forward references to numeric equates are not supported until a future multi-pass expression phase. A use of an equate before its definition must produce a structured unknown-equate diagnostic.
- Recursive or self-referential equates must produce a structured recursive-equate or invalid-equate diagnostic.
- Duplicate names across data symbols, labels, procedures, and equates must be rejected unless a later phase explicitly introduces separate scopes. The diagnostic must name the earlier symbol category when practical.
- `name = expression` may be redefined only if the implementation phase explicitly says redefinition is supported. Until then, redefinition must be rejected consistently.
- `name EQU expression` is non-redefinable in the v1 numeric-equate subset.
- `TEXTEQU` and text `EQU <...>` remain unsupported until the text-equate or selected macro phase.
- `OFFSET symbol + constant` is a static address expression. The symbol must be a data symbol, not a numeric equate.
- `$`, segment arithmetic, `THIS`, `SHORT`, high-level condition operators, and macro-time text operators are unsupported until their assigned phases.

Mandatory precedence and associativity for implemented constant-expression operators:

```text
Highest:
  unary +, unary -, NOT, HIGH, LOW, HIGHWORD, LOWWORD
  *, /, MOD
  +, -
  SHL, SHR
  AND
  XOR
  OR
Lowest
```

Binary operators are left-associative unless a later phase explicitly documents an exception. Parentheses override precedence. Every expression diagnostic must preserve line, column, byte offset, and span length for the operator or operand that caused the failure.

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

Any future non-MASM convenience mode must be separately named and must not change MASM32 Educational Mode. MASM32 Educational Mode preserves MASM-compatible rejection for ambiguous memory-width forms.

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

#### Parser Versus Runtime Boundary for Symbol-Relative Memory Operands

Symbol-relative memory operands use byte offsets. Examples:

```asm
mov eax, nums[8]
mov eax, [nums + 8]
mov eax, DWORD PTR [nums + 1]
```

The parser must validate memory operand syntax, symbol identity, constant-expression evaluation, and memory width. It must not reject a valid symbol-relative memory operand solely because the inferred access range crosses a declared-object boundary, section-image boundary, section-capacity boundary, or fixed-layout slack boundary.

The final byte range is checked at runtime by checked VM memory helpers and by the selected memory-validation policy.

This distinction is mandatory:

```text
Assembly-time invalid:
  The operand cannot be parsed, has ambiguous width, uses an unsupported addressing mode, refers to an unknown symbol, uses an unsupported executable width, has a constant-expression failure, or cannot be represented in the simulator address model.

Runtime invalid or warning:
  The operand is syntactically valid, but the final byte range is outside a VM region, violates permissions, overlaps `.CONST` on write, crosses a section boundary under an enabled section validation policy, crosses an object boundary under an enabled object validation policy, reads uninitialized-origin bytes under an enabled uninitialized-read policy, or is unaligned.
```

A diagnostic named `symbol-offset-out-of-range` may be retained only for cases where the symbol-relative constant expression cannot be represented in the simulator address model or cannot be lowered to a valid effective address. It must not mean "the access crosses the source symbol's declared-object bounds" or "the access crosses the source symbol's section-image bounds."

Examples of valid memory syntax that must not be rejected merely by parser-time object or image bounds:

```asm
.data
x DWORD ?
.code
main PROC
    mov eax, DWORD PTR [x + 1]
main ENDP
END main
```

```asm
.DATA?
x DWORD ?
.code
main PROC
    mov eax, 10
    mul [x+1]
main ENDP
END main
```

These examples have valid memory operand syntax and inferable width. They may warn or stop at runtime only according to the selected memory-validation policy and final VM memory range.

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

#### 8.6.1.1 Direct Branch Target Classification

Direct branch instructions must distinguish executable code targets from other known symbols.

A symbol being known is not sufficient to make it a valid direct branch target. The parser must classify branch target operands by symbol kind and source context before lowering them to IR.

For direct branch forms such as `jmp label` and later direct conditional branches, accepted targets are:

- executable code labels declared with `name:` when the label resolves to an executable target instruction;
- procedure-entry labels declared with `name PROC`, but only as a direct branch to the procedure entry instruction and without implying `CALL`, `RET`, stack-frame behavior, argument passing, or calling-convention semantics.

Rejected direct branch targets include:

- data symbols;
- numeric equates;
- Irvine32 virtual routine names such as `exit`;
- external or non-goal symbols;
- unknown symbols;
- empty target operands;
- labels that do not resolve to an executable target instruction;
- unsupported indirect, register, memory, `SHORT`, `NEAR PTR`, or `FAR PTR` branch forms until a later phase explicitly implements them.

Diagnostics for rejected branch targets must point at the target operand, not merely at the branch mnemonic. When the target resolves to a known non-code symbol, structured diagnostics should include the resolved symbol kind so the rendered Simulator Messages can explain why the symbol is not a valid branch target.

Direct branch target classification must not be confused with procedure call target classification. A `PROC` name may be a valid direct branch target for `jmp`, but that does not implement or imply procedure-call semantics.

This classification applies only to direct source-level branch operands. It does not define indirect branch target validation.

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

#### 8.6.5 Post-30 Instruction Semantics Contract

Post-30 instruction implementations must use the following contract unless a later phase explicitly overrides it:

- flag behavior is specified per instruction and per operand width;
- undefined or underspecified architectural flag results are converted into deterministic educational-mode behavior only when the phase says so;
- all memory operands use central checked reads and writes;
- validation-first instructions must not mutate registers, flags, memory, console state, or debugger deltas when validation fails;
- partial-progress instructions, such as repeated string operations under watchdog interruption, must state exactly which committed effects remain visible;
- read-only instructions such as `cmp`, `test`, `lea`, jumps, and many string comparisons must not create successful memory-change rows;
- QWORD/SQWORD executable memory operations remain rejected in MASM32 Educational Mode until Extended 32-bit Mode phases enable selected behavior.

Core expansion groups must remain distinct:

- `inc/dec`, logical operations, `not`, shifts, and rotates are separate milestones because their flag rules differ;
- `mul`, one-operand `imul`, two-/three-operand `imul`, `div`, and `idiv` are separate milestones because their implicit registers and failure behavior differ;
- label metadata, instruction watchdogs, direct `jmp`, `cmp`, equality jumps, signed relational jumps, and unsigned relational jumps are separately staged;
- string instruction families are split by data movement, accumulator behavior, comparison behavior, and REP/REPE/REPNE repetition semantics.

#### 8.6.6 Shift Count, Rotate Count, and Undefined-Flag Compatibility Policy

Runtime shift and rotate instructions must prefer MASM/x86-compatible execution over rejecting MASM-valid source.

This policy applies to:

```text
shl
sal
shr
sar
rol
ror
```

`SHL`/`SAL`, `SHR`, and `SAR` are shift instructions. `ROL` and `ROR` are rotate instructions. Shifts and rotates have different flag rules and must remain separate implementation milestones.

#### Count handling

In MASM32 Educational Mode:

- immediate counts are accepted when they fit the encoded immediate-count form selected by the instruction phase;
- `CL` counts use the low 8 bits of `ECX` as the raw count;
- for 8-bit, 16-bit, and 32-bit destinations, the effective count is:

  ```text
  effective_count = raw_count & 31
  ```

- effective count `0` is a full no-op: destination and all currently modeled flags remain unchanged;
- nonzero shift counts execute using the destination width;
- nonzero rotate counts rotate by `effective_count % operand_width`;
- MASM-valid counts must not be rejected solely because the effective count is greater than or equal to the destination width.

#### Shift undefined-flag handling

For `SHL`/`SAL`, `SHR`, and `SAR`:

- when an architectural rule defines `CF`, `OF`, `ZF`, or `SF`, update the modeled flag according to that rule;
- when an architectural rule leaves a modeled flag undefined, preserve that flag's previous simulator value as a deterministic fallback;
- default mode may emit an eager producer warning for undefined modeled flags;
- a future smart mode should warn or error only when a later instruction consumes an invalid flag;
- `unsupported-shift-count` must not be used for MASM-valid shift counts.

Legacy note:

```text
Phase 46 through Phase 48 may contain a test/API-only strict undefined-shift validation mode that stops before mutation at the producer instruction. This mode is historical compatibility for existing tests. It is not the preferred long-term educational strict mode.
```

#### Rotate undefined-flag handling

For `ROL` and `ROR`:

- effective count `0` preserves destination and all modeled flags;
- nonzero effective counts update `CF` according to the rotate instruction;
- one-bit rotates define `OF` according to the rotate instruction;
- other nonzero rotate counts leave `OF` architecturally undefined;
- when `OF` is architecturally undefined, preserve prior simulator `OF` as the deterministic fallback;
- default Phase 49/50 behavior should use eager warnings for undefined `OF` unless the phase explicitly selects quiet validity tracking;
- do not add strict-before-mutation producer errors for `ROL` or `ROR`;
- future smart mode should warn or error when later code consumes invalid `OF`.

For nonzero effective count where `rotate_count == 0`, do not treat the instruction as a full no-op. The destination bits are unchanged, but nonzero-count rotate flag behavior still applies: `CF` is updated according to the rotate result, `ZF` and `SF` are preserved, and `OF` is undefined unless the effective count is exactly `1`.

#### Diagnostic codes

Preferred general producer warning code:

```text
undefined-modeled-flag
```

Existing shift-specific code:

```text
undefined-shift-flag
```

Recommended rotate producer warning wording:

```text
ROL/ROR with this effective count leaves OF architecturally undefined. The simulator executed the MASM-compatible rotate and preserved OF deterministically.
```

Recommended consumer warning/error code:

```text
undefined-flag-use
```

Consumer diagnostics should be introduced by the flag-validity and flag-consumer phases, not by the rotate instruction phases unless those phases explicitly include the smart-mode infrastructure.

### 8.7 Historical Initial Limitations and Current Unsupported Families

This section records early-version limitations and current non-goals. It must not be read as the current supported-syntax list.

The authoritative current support state is determined by:

1. the latest completed milestone;
2. `docs/SUPPORTED_SYNTAX.md`;
3. the current implementation guide phase ledger;
4. passing tests for that feature.

Features implemented by later phases, such as `.DATA?`, `.CONST`, signed integer declarations, signed `PTR` aliases, all-GPR register-indirect addressing, numeric equates, constant expressions, extended constant expressions, and nested `DUP`, must not remain listed as current unsupported behavior.

Still unsupported or deferred in v1 unless a later guide phase explicitly implements them:

- full MASM macro language;
- full conditional assembly;
- high-level MASM flow directives such as `.IF`, `.ELSE`, `.ENDIF`, `.WHILE`, `.REPEAT`, `.BREAK`, and `.CONTINUE` until their assigned lowering phases;
- full `INVOKE`, `PROTO`, `LOCAL`, parameter, and calling-convention modeling until their assigned procedure phases;
- text-substitution equates and full `TEXTEQU` behavior unless a later macro/text-equate phase implements them;
- full scaled-index addressing until the staged memory-operand roadmap reaches it;
- `STRUCT`, `UNION`, `RECORD`, fields, field initializers, and user-defined types until their assigned phases;
- FPU instructions;
- SSE/AVX instructions;
- string instructions and `REP`/`REPE`/`REPNE` prefixes until their dedicated string-instruction phases;
- segment registers and segment override behavior;
- interrupts;
- Windows API calls;
- PE loading, object linking, import libraries, and host include-file loading;
- true x64 MASM / `ml64` behavior.

If the previous version of this section contained still-valid unsupported families not listed above, preserve them under this current unsupported/deferred list. Do not preserve entries for features already implemented or explicitly scheduled as implemented by completed phases.

Classification rule:

```text
Historical unsupported wording is not enough to reject a construct. Before emitting an unsupported diagnostic, check whether a later phase implemented that construct or intentionally reclassified it.
```

Documentation rule:

```text
If this section conflicts with a later specific feature section, the later specific feature section wins for stable behavior. If it conflicts with the implementation guide's current phase ledger, update this section rather than treating the guide as wrong.
```

### 8.8 MASM Compatibility Coverage Notes

The current target is **educational MASM32/Irvine32 compatibility**, not full MASM. MASM includes many directive families and operators beyond the initial subset, including conditional assembly, high-level conditional control-flow directives, equates, macros, procedure/prototype directives, segment directives, structure/record directives, repeat blocks, listing controls, and simplified segment directives. These should be treated as staged roadmap items or explicit non-goals, not implicit behavior.

Important textbook/compatibility areas to track explicitly:

- Compatibility corrections for existing syntax: signed `PTR` aliases, all-GPR base registers, and global memory-width resolution.
- Equates and constants: numeric `=` and numeric `EQU` plus staged constant-expression support are v1 roadmap features and must be documented according to their completed phase status. Text-substitution `TEXTEQU` and macro-time text behavior remain deferred unless a later phase explicitly implements them.
- Additional data sections: `.DATA?` and `.CONST` are v1 MASM compatibility sections. `.DATA?` uses deterministic zero-filled storage plus uninitialized-origin metadata. `.CONST` is read-only by final effective address range through central memory-write checks, not only by static symbol metadata.
- Additional non-integer data declarations: `REAL4`, `REAL8`, `REAL10`, `TBYTE`, and possibly `FWORD`. These remain deferred unless a floating-point/data-layout phase explicitly adds them.
- Nested `DUP` and initializer expressions: nested `DUP` plus expression-backed data initializers are staged v1 features and must not be listed as current unsupported behavior after their implementation phases are complete.
- Native diagnostic rendering harness for exact Simulator Messages text.
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
- Extended flag model: `PF`, `AF`, and `DF`. `PF`/`AF` integration must be split by instruction-family behavior rather than implemented as one large catch-all phase: arithmetic/compare helpers, logical/TEST helpers, shift/rotate helpers, multiply/divide preservation or undefined-policy checks, flag-preserving instruction regressions, and debugger/Irvine/UI display updates. `DF` remains a later prerequisite for string instructions and `CLD`/`STD`.
- Irvine32 runtime compatibility: virtual include symbols, `exit`, output routines, input routines with flag semantics, debug routines, random routines, console-control policy, and explicit unsupported diagnostics for file routines and Windows-specific routines.

These features should not be silently accepted before they are implemented. Unsupported forms should produce explicit diagnostics with source location.

### 8.9 Post-30 Supported-Subset Expansion Rules

The post-Milestone-30 roadmap expands the supported MASM subset through tightly staged instruction, control-flow, procedure, Irvine32, string, STRUCT/RECORD, macro-convenience, debugger, settings, and editor milestones.

The following staging rules are normative:

- scalar instruction groups must be split by distinct flag behavior and operand shape;
- extended flag integration must also be split by distinct flag behavior. `PF`/`AF` storage, arithmetic updates, logical/test updates, shift/rotate policy, multiply/divide preservation, flag-preserving regression coverage, and display/Irvine/debugger integration are separate implementation slices. A future assistant must not treat `PF`/`AF` support as one broad executor-wide patch;
- memory-capable instruction phases must state width-resolution sources and ambiguous-width diagnostics;
- branch phases must separate target classification from runtime instruction-pointer mutation when needed;
- CALL/RET/procedure phases must use simulator return tokens, not native addresses;
- high-level MASM flow is lowered to ordinary IR, labels, comparisons, and conditional branches rather than executed by a separate high-level interpreter;
- selected `Macros.inc` conveniences are virtual built-ins, not general MASM macro expansion;
- STRUCT, TYPEDEF, and RECORD support starts as explicit metadata and layout rules, not a full MASM type system;
- listing/documentation no-ops and COMMENT skipping remain compatibility features, not object/listing generation.

Unsupported or deferred constructs must continue to receive explicit diagnostics that distinguish MASM-invalid syntax, planned-later features, explicit v1 non-goals, and simulator-mode restrictions.

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

### 9.4 Extended 32-bit Register and Address-Size Boundaries

Extended 32-bit Mode remains an educational extension, not x64 MASM and not `ml64` behavior.

When enabled by the corresponding phases:

- existing 32-bit registers gain 64-bit parents such as `RAX`, `RBX`, `RCX`, `RDX`, `RSI`, `RDI`, `RBP`, and `RSP`;
- `R8` through `R15` are added only by the dedicated extended-register phase;
- aliases update parent registers using simulator alias rules rather than machine-code REX encoding rules;
- `AH`, `BH`, `CH`, and `DH` remain valid aliases because the simulator is not modeling machine-code encoding restrictions;
- `R8B`, `R8W`, and `R8D` style aliases are accepted only if the extended-register phase explicitly implements them;
- `RIP` is debugger/display metadata only unless a later phase defines source-level use;
- `RFLAGS` stores modeled flags only; unmodeled bits are zero or explicitly reserved.

Memory effective addresses remain 32-bit VM addresses in Extended 32-bit Mode. A computed effective address above `0xFFFFFFFF` must report `address-size-exceeded` unless a future post-v1 phase explicitly introduces a 64-bit VM address space.

## 10. Flag Model

The simulator models only named flags that have been explicitly implemented by the roadmap. The VM must not imply that unimplemented EFLAGS bits have meaningful x86 behavior.

### 10.1 Initially Modeled Flags

The initial modeled flags are:

- `CF` carry flag
- `ZF` zero flag
- `SF` sign flag
- `OF` overflow flag

These flags are stored in the VM CPU/EFLAGS model and are displayed by the debugger, final register/flag output, and Irvine32 `DumpRegs` once the corresponding display phases exist.

### 10.2 Extended Modeled Flags

The extended flag roadmap adds:

- `PF` parity flag
- `AF` auxiliary carry flag
- `DF` direction flag

`PF` and `AF` must be added before any feature depends on full textbook `EFLAGS` display or auxiliary/parity behavior. `DF` must be added before string instructions and `CLD`/`STD` behavior.

Adding storage for a flag is not the same as updating every instruction. The implementation guide owns the phase split for storage, instruction-helper updates, preservation regression tests, and UI/Irvine display integration.

### 10.3 PF Definition

When an instruction defines `PF`, the simulator must set `PF` from the low 8 bits of the instruction result.

Definition:

```text
PF = 1 if the low byte of the result contains an even number of one bits.
PF = 0 if the low byte of the result contains an odd number of one bits.
```

Examples:

```text
result low byte = 00h -> PF = 1
result low byte = 01h -> PF = 0
result low byte = 03h -> PF = 1
result low byte = FFh -> PF = 1
```

If an instruction does not produce a result value, the phase text for that instruction family must explicitly say whether `PF` is updated, preserved, cleared, or handled by a deterministic undefined-flag policy.

### 10.4 AF Definition

When an arithmetic instruction defines `AF`, the simulator must set `AF` according to carry or borrow across bit 3 into bit 4.

For addition-family instructions:

```text
AF = 1 if adding the low nibbles carries out of bit 3.
AF = 0 otherwise.
```

For subtraction-family instructions:

```text
AF = 1 if subtracting the low nibbles borrows across bit 4.
AF = 0 otherwise.
```

Implementation helpers may use the standard bit expression:

```text
AF = ((lhs ^ rhs ^ result) & 10h) != 0
```

provided the helper is validated separately for addition, subtraction, compare, `NEG`, `INC`, and `DEC` semantics.

`NEG destination` must compute `AF` as `0 - destination`.

`INC` and `DEC` must update `AF` from the increment/decrement operation while preserving `CF`, matching their existing `CF` contract.

### 10.5 Undefined or Architecturally Unspecified Modeled Flag Results

Some real x86 instructions leave specific flag results architecturally undefined. The simulator must not expose nondeterministic, host-dependent, or accidental C implementation values for modeled flags.

The simulator must distinguish two events:

```text
producer event:
  An instruction executes and leaves one or more modeled flags architecturally undefined.

consumer event:
  A later instruction reads, branches on, displays as a dependency, or otherwise semantically consumes a modeled flag whose current value is marked architecturally undefined.
```

A MASM-valid producer instruction must not be treated as MASM-invalid syntax merely because it creates an undefined flag result.

#### Stable deterministic value policy

When an instruction makes a modeled flag architecturally undefined:

- the instruction still executes in default MASM32 Educational Mode;
- the destination/register/memory effects still commit if all ordinary validation succeeds;
- the simulator preserves the previous deterministic value of each undefined modeled flag;
- the simulator records metadata that the preserved flag value is architecturally undefined;
- the simulator records the producer instruction source span, mnemonic, diagnostic code, and flag list where practical.

The preserved value is a simulator fallback value only. It is not a portable real-x86 guarantee.

#### Undefined-flag reporting modes

The simulator supports these conceptual reporting modes. The implementation guide owns the exact phase that introduces each mode and any API/settings exposure.

```text
eager-warning:
  Execute the producer instruction.
  Preserve undefined modeled flag values deterministically.
  Mark affected flags as invalid/undefined-origin if flag-validity metadata exists.
  Emit a warning at the producer instruction.

use-warning:
  Execute the producer instruction.
  Preserve undefined modeled flag values deterministically.
  Mark affected flags as invalid/undefined-origin.
  Do not warn at the producer instruction.
  Emit a warning only if a later instruction consumes an invalid flag.

use-error:
  Execute the producer instruction.
  Preserve undefined modeled flag values deterministically.
  Mark affected flags as invalid/undefined-origin.
  Stop with a runtime error only if a later instruction attempts to consume an invalid flag.
```

`use-error` is the preferred strict educational/grading model. It rejects dependence on an undefined flag, not the MASM-valid instruction that produced the undefined flag.

Default behavior must not change merely because flag-validity metadata or consumer diagnostics are added. A metadata phase adds tracking. A consumer-diagnostic phase adds testable policy hooks. A later settings/diagnostics phase may choose a browser default.

#### Legacy producer-error mode

Earlier shift phases may expose a legacy test/API-only mode where a shift instruction with undefined modeled flags reports an error before mutation. This mode exists for regression stability around the Phase 46-48 shift implementation.

Rules for legacy producer-error mode:

- It must not be described as the preferred long-term strict educational model.
- It must not be expanded to new instruction families unless a later phase explicitly chooses that behavior.
- It must not be exposed as the main browser UI strict setting without a deliberate settings-phase decision.
- New undefined-flag work should prefer `eager-warning`, `use-warning`, and `use-error`.

#### Validity propagation policy

Each modeled flag has both a value and a validity state once flag-validity metadata is implemented.

```text
flag value:
  The deterministic simulator bit value currently stored for CF, ZF, SF, OF, and later PF/AF/DF where applicable.

flag validity:
  Whether that value is architecturally valid for the current program state.
```

When an instruction defines a flag architecturally:

- set the flag value according to the instruction;
- mark the flag valid;
- clear any previous undefined-origin metadata for that flag.

When an instruction clears or sets a flag by contract:

- set the flag value;
- mark the flag valid;
- clear any previous undefined-origin metadata for that flag.

When an instruction preserves a flag architecturally:

- preserve the flag value;
- preserve the flag validity metadata exactly.

When an instruction makes a modeled flag architecturally undefined:

- preserve the flag value as a deterministic fallback;
- mark the flag invalid;
- record undefined-origin metadata for that flag.

#### Consumer diagnostics

A flag-consuming instruction must check validity for every modeled flag it reads.

Examples of flag consumers include:

- conditional jumps;
- future `SETcc`, `CMOVcc`, or equivalent flag-dependent instructions if implemented;
- any future instruction or debugger operation that makes a semantic decision from a flag value.

If a consumer reads an invalid flag:

- `use-warning` emits a warning and then continues using the deterministic preserved flag value;
- `use-error` emits a runtime error and stops before making the flag-dependent decision;
- diagnostics should point primarily to the consumer instruction and mention the producer instruction location when available.

Recommended diagnostic code:

```text
undefined-flag-use
```

Recommended warning wording:

```text
<CONSUMER> reads <FLAG>, but <FLAG> is architecturally undefined from <PRODUCER> at line <line>. The simulator preserved a deterministic fallback value; this flag-dependent behavior is not portable.
```

Recommended runtime-error wording:

```text
<CONSUMER> reads <FLAG>, but <FLAG> is architecturally undefined from <PRODUCER> at line <line>. Execution stopped before using the undefined flag.
```

#### Producer diagnostics

When the reporting mode uses eager producer warnings, producer diagnostics should use a general code unless a legacy stable code already exists.

Preferred general producer diagnostic code:

```text
undefined-modeled-flag
```

Legacy shift diagnostic code:

```text
undefined-shift-flag
```

`undefined-shift-flag` may remain for Phase 46-48 compatibility. New non-shift instruction families should prefer `undefined-modeled-flag` unless a phase intentionally defines a more specific code.

#### Testing requirements

Every phase that introduces undefined modeled flag behavior must include:

- a successful default-mode execution test;
- a rendered Simulator Messages test for eager-warning mode if that mode is active;
- a validity-metadata test proving the affected flag is marked invalid;
- a later consumer test if a real flag consumer exists at that point;
- no-partial-mutation tests for runtime error paths;
- regression tests proving MASM-valid source is not rejected as syntax solely because of undefined flags.

If no flag-consuming instruction exists yet, a phase may add native/helper tests for the consumer-validation helper and defer source-level consumer tests until conditional jumps or another consumer family exists.

### 10.6 PF/AF Policy by Instruction Family

The implementation guide owns phase numbers, but the stable behavior target for `PF` and `AF` is defined here.

#### Arithmetic and compare instructions

These instructions must update both `PF` and `AF` once their `PF`/`AF` integration phase is complete:

```text
add
adc
sub
sbb
cmp
neg
inc
dec
```

Rules:

- `PF` is computed from the low byte of the arithmetic result.
- `AF` is computed from carry/borrow across bit 3.
- `CMP` updates flags but does not mutate the destination.
- `INC` and `DEC` update `PF` and `AF` but preserve `CF`.
- Failed validation or failed memory write paths must not partially mutate `PF` or `AF`.

#### Logical and TEST instructions

These instructions must update `PF` once their `PF`/`AF` integration phase is complete:

```text
and
or
xor
test
```

Rules:

- `PF` is computed from the low byte of the logical result.
- `CF` and `OF` retain their existing logical-instruction behavior.
- `TEST` must not mutate the destination.
- The simulator uses the guide-defined deterministic policy for `AF` on these logical/test instructions. The phase text must state that policy explicitly and test it.

The required v1 policy is:

```text
AF is cleared to 0 for AND, OR, XOR, and TEST as a deterministic educational simplification.
```

Do not emit undefined-flag warnings for `AF` on `AND`, `OR`, `XOR`, or `TEST`. This is a deliberate simulator contract. Programs must not rely on it as portable real-x86 behavior.

#### Shift instructions

These instructions must be covered by the `PF`/`AF` shift integration phase:

```text
shl
sal
shr
sar
```

Rules:

- If the effective count is zero, `PF` and `AF` are preserved because the instruction has no effect.
- If the effective count is nonzero, `PF` is computed from the low byte of the shifted result.
- `AF` is architecturally undefined for nonzero shift counts.
- When `AF` becomes modeled, nonzero shifts must preserve the previous simulator `AF` value as the deterministic fallback and mark `AF` invalid if flag-validity metadata exists.
- Adding `AF` to the modeled flag set must not expand eager undefined-flag warnings merely because `AF` is now modeled.
- The eager warning trigger set remains the one selected by the shift-count compatibility phase unless a later phase explicitly revises it.
- The smart `use-warning` / `use-error` mode may diagnose later use of invalid `AF` if a future instruction consumes `AF`.
- The `PF`/`AF` integration phase must not reintroduce `unsupported-shift-count` for MASM-valid shift counts.
- The `PF`/`AF` integration phase must not change already-defined `CF`, `OF`, `ZF`, or `SF` behavior for shifts except where a bug fix is explicitly documented.

#### Rotate instructions

These instructions must be covered by the `PF`/`AF` rotate integration phase:

```text
rol
ror
```

Rules:

- `PF` and `AF` are preserved for `ROL` and `ROR`.
- `SF` and `ZF` are also preserved for `ROL` and `ROR`.
- `CF` and `OF` follow the rotate phase contract.
- If the effective count is zero, all modeled flags are preserved and all flag-validity metadata is preserved.
- If a nonzero multi-bit rotate makes `OF` architecturally undefined, the rotate phase must preserve previous simulator `OF` and mark `OF` invalid if flag-validity metadata exists.
- Adding `PF` or `AF` must not create new rotate warnings by itself.
- `ROL` and `ROR` must not compute `PF` from the rotate result.
- Future `use-warning` / `use-error` mode may diagnose later use of invalid `OF`; the rotate producer instruction itself remains MASM-compatible and executable.

#### Multiply and divide instructions

These instruction families must be covered by a separate `PF`/`AF` preservation or undefined-policy phase:

```text
mul
imul
div
idiv
```

Rules:

- Only already-implemented multiply/divide forms are in scope for that phase.
- `PF` and `AF` must be preserved unless the instruction family phase explicitly documented a different deterministic undefined-flag policy.
- Divide-error paths must not mutate `PF` or `AF`.
- Multiply overflow/result-size behavior must preserve the instruction family’s existing `CF`/`OF` contract.
- The `PF`/`AF` phase for multiply/divide must not change implicit-register behavior, memory behavior, quotient/remainder behavior, or divide-error behavior.

#### Flag-preserving instructions

These instructions and instruction families must preserve `PF` and `AF` unless their own phase explicitly documents flag mutation:

```text
mov
movsx
movzx
cbw
cwde
cwd
cdq
lea
xchg
nop
clc
stc
cmc
jmp and conditional jumps
loop-family instructions
push
pop
call
ret
leave
ret imm16
exit
Irvine32 routines unless a specific routine contract says otherwise
```

`CLC`, `STC`, and `CMC` mutate `CF` only. They must preserve `PF` and `AF`.

### 10.7 Display Requirements for Extended Flags

After `PF` and `AF` are implemented and integrated:

- final register/flag output must include `PF` and `AF`;
- debugger flags table must include `PF` and `AF`;
- last-step flag deltas must show `PF` and `AF` changes when they change;
- `DumpRegs` must include `PF` and `AF` in its Program Console output once its update phase is reached;
- source-run JSON and worker protocol payloads must remain structured-clone-safe and JSON-compatible;
- rendered output must remain deterministic and testable.

Display phases must not change instruction semantics. They only expose already-modeled state.

Before the display integration phase, `PF` and `AF` may be stored internally and validated by native/helper/source-run tests without being shown in every final UI, debugger, or Irvine32 display, unless an earlier phase explicitly chooses to expose them.

### 10.8 Testing Requirements for Flag Changes

Every phase that changes modeled flag behavior must include:

- native CPU/helper tests for the flag formulas;
- executor tests for register and memory operand paths where applicable;
- source-run JSON tests for representative programs;
- rendered Simulator Messages tests for any new warning or runtime-error diagnostic;
- regression tests proving existing `CF`, `ZF`, `SF`, and `OF` behavior did not change accidentally;
- no-partial-mutation tests for failed memory reads, failed memory writes, permission failures, strict validation failures, and divide errors where applicable.

A flag phase must not advance metadata unless all implemented instruction families listed in that phase have explicit pass/fail coverage.

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


### 11.6 Automatic Memory Region Sizing

Automatic memory region sizing is a deterministic layout mode. It must not be random by default.

In automatic deterministic layout mode, the loader computes region sizes from program metadata and user settings:

```text
.code size  = IR/code metadata size + configured guard margin, rounded up to alignment
.data size  = initialized `.data` + `.DATA?` + `.CONST` storage as separate regions or protected subranges, rounded up to alignment
.heap size  = configured heap size or default heap setting
.stack size = `.stack` metadata if supplied, otherwise the configured default stack size
```

Requirements:

- Region sizes must have documented minimums, maximums, alignment, and guard-gap policy.
- Automatic sizing must be deterministic for the same source and settings.
- Automatic sizing must preserve the central memory-helper rule: all accesses still go through checked read/write helpers.
- Automatic sizing must not hide out-of-bounds bugs by silently expanding regions after program load.
- If a program requests more memory than the configured safety tier permits, loading must fail with a structured resource-limit diagnostic.

### 11.7 Memory Layout Modes

The simulator must distinguish layout policy from memory validation policy.

Supported layout modes:

```text
Fixed educational layout:
  Uses stable documented region bases such as `.code = 00400000h` and `.data = 00500000h`.
  This is the default for beginner use, tests, documentation, and screenshots.

Automatic deterministic layout:
  Computes sizes from source metadata but uses deterministic bases and alignment.

Seeded randomized layout:
  Computes or selects randomized region bases from an explicit seed.
  The same source, settings, and seed must produce the same layout.
  The seed must be visible in the UI and saved in project/share state when this mode is active.

Fresh randomized layout:
  Generates a new seed per run/session.
  This mode is for anti-hardcoding demonstrations and must be clearly labeled non-deterministic unless the generated seed is captured.
```

Randomized layout exists to discourage hardcoded simulator addresses such as `00500000h`. Correct MASM-style programs should use labels and `OFFSET`, not fixed implementation addresses.

Randomization must never change MASM semantics. It changes simulated placement only. It must not change instruction results except when a program incorrectly depends on fixed implementation addresses.

Memory layout capacity and memory validation are related but distinct.

Fixed educational layout may allocate section or region capacity larger than the declared section image. Automatic deterministic layout may also round capacity upward because of minimum sizes, alignment, or guard policy.

Memory validation levels interpret those ranges as follows:

- Region-only validation uses allocated VM memory regions.
- Section-capacity validation uses allocated section capacity.
- Section-image validation uses bytes produced by declarations, excluding extra capacity.
- Declared-object validation uses individual data declaration object ranges.

Changing layout mode must not silently change the meaning of a validation level. If automatic sizing makes section capacity equal to section image for a given program, that is a consequence of the selected layout, not a redefinition of the levels.

### 11.8 Declared Object Allocation Map

The parser/data-layout layer must be able to produce a declared-object allocation map for `.data`, `.DATA?`, and `.CONST` storage.

Each declared object entry should include:

- symbol name;
- section kind: `.data`, `.DATA?`, or `.CONST`;
- base address after layout;
- byte size;
- declared element size;
- element count;
- signedness/type metadata;
- initialization-origin metadata for each byte or byte range;
- read/write permissions;
- source location of the declaration.

This object map is required for strict object-bounds diagnostics, provenance diagnostics, memory visualization, and uninitialized-origin read diagnostics. It must not replace the lower-level region permission checks.

### 11.9 Memory Access Validation Levels

The simulator must distinguish MASM-style syntax validity from runtime memory validation.

A memory operand can be valid MASM-style syntax even if the final byte range would be suspicious, unsafe, outside a declared object, outside a section image, or outside a VM region at runtime. The parser must not reject valid memory syntax merely because a later runtime validation level would warn or stop.

Memory validation is evaluated on the final byte range:

```text
[address, address + access_width - 1]
```

The range calculation must use overflow checks before classification.

The simulator uses these validation concepts:

```text
VM memory region:
  A concrete allocated memory region in the VM memory system, with base address, size, and permissions.
  Examples may include code, data, const, heap, and stack regions depending on the selected layout implementation.

Section capacity:
  The allocated capacity reserved for one MASM data section or section-like storage area, such as `.data`, `.DATA?`, or `.CONST`.
  Section capacity may be larger than the declared bytes in that section because of fixed educational layout, minimum region sizes, alignment, guard policy, or deterministic automatic sizing.

Section image:
  The bytes actually produced by declarations in one MASM data section after initializer expansion, `DUP` expansion, and layout.
  Section image excludes extra fixed-layout slack, minimum-size tail capacity, guard gaps, and other reserved-but-not-declared bytes.
  If a future explicit alignment feature emits padding as part of a section image, that feature must document whether the padding is section-image-owned.

Declared object:
  One declared data object created by one data declaration after expansion.
  Adjacent declarations remain separate declared objects even if their byte ranges are contiguous.
```

The validation levels are:

```text
Level 1 - Region-only validation:
  Default behavior. Closest to native MASM execution.
  A read succeeds if the final byte range is wholly inside one readable VM memory region.
  A write succeeds if the final byte range is wholly inside one writable VM memory region and does not overlap read-only `.CONST` storage.
  Invalid address ranges, address overflow, accesses outside allocated VM regions, and permission failures are runtime errors.

Level 2 - Section-capacity validation:
  Optional educational warning/strict mode.
  The final byte range must remain inside the allocated capacity of the owning section or section-like storage area.
  This level may allow access to reserved section slack that is inside the allocated capacity but beyond the declared section image.
  Default mode does not enable this validation.

Level 3 - Section-image validation:
  Optional educational warning/strict mode.
  The final byte range must remain inside the declared section image.
  This level rejects or warns on accesses that remain inside allocated section capacity but go beyond bytes produced by declarations.
  In automatic deterministic layout this may often match section capacity, but it is still a separate rule because automatic layout may round capacity upward for alignment, minimum size, or guard policy.
  Default mode does not enable this validation.

Level 4 - Declared-object validation:
  Optional educational warning/strict mode.
  The final byte range must be wholly inside one declared object.
  Accesses outside every declared object, partially overlapping an object boundary, or spanning adjacent declared objects warn or stop depending on policy.
  Access wholly inside another declared object is not an object-bound violation by itself.
  This is the layer implemented by the allocated-object warning/strict modes.
  Default mode does not enable this validation.
```

For Level 2 and Level 3, the owning section is determined by the starting address of the access. If the starting address is not inside any known section capacity or section image, the access violates the relevant enabled section validation level after Level 1 region validation passes.

Declared-object validation checks only whether the final byte range is wholly contained in one declared object. It does not check whether the expression's base symbol, if any, intended that object. A future provenance/intent validation mode may warn when an expression based on one symbol lands wholly inside a different symbol, but that is not Level 4.

For Levels 2, 3, and 4, the policy may be one of:

```text
off:
  Do not emit diagnostics for that level.

warn:
  Emit a non-fatal simulator warning and continue if the level's condition is violated.

strict:
  Emit a runtime error and stop before mutation if the level's condition is violated.
```

Level 1 is not optional. It is always enforced by checked VM memory helpers.

Recommended diagnostic codes:

```text
section-capacity-violation
section-image-violation
object-bounds-violation
```

Do not use `symbol-offset-out-of-range` for runtime section or object validation. A `symbol-offset-out-of-range` diagnostic may be retained only for expression/address representability failures, not for crossing symbol/object bounds.

### 11.9.1 Cross-Region, Cross-Section, and `.CONST` Rules

A single memory access must not be stitched across two independent VM memory regions.

If the final byte range is not wholly contained in one suitable VM memory region, execution must stop with the ordinary runtime memory diagnostic. This is true even in default mode. It is not a warning-only educational diagnostic.

If `.data`, `.DATA?`, and `.CONST` are represented as subranges inside one shared allocated VM memory region, crossing from one section subrange into another is not automatically a low-level region error. It is classified by the enabled section-capacity or section-image validation level.

Default behavior:

```text
- Crossing section boundaries may execute if the final byte range is inside one readable/writable allocated VM region.
- Crossing declared-object boundaries may execute if the final byte range is inside one readable/writable allocated VM region.
- `.CONST` write overlap still fails.
```

Warning behavior:

```text
- Section-capacity warning mode warns when an access leaves the owning section capacity but remains otherwise valid.
- Section-image warning mode warns when an access leaves the declared section image but remains otherwise valid.
- Declared-object warning mode warns when an access leaves declared-object bounds but remains otherwise valid.
```

Strict behavior:

```text
- Section-capacity strict mode stops before mutation on section-capacity violation.
- Section-image strict mode stops before mutation on section-image violation.
- Declared-object strict mode stops before mutation on declared-object violation.
```

`.CONST` write protection is mandatory and is not a warning-only teaching diagnostic.

Any write whose final byte range overlaps read-only `.CONST` storage must fail as a runtime permission/read-only diagnostic before section-capacity, section-image, declared-object, uninitialized-read, or unaligned-access diagnostics.

Reads from `.CONST` are allowed if the full range is otherwise valid. Reads crossing `.CONST` are not permission failures merely because `.CONST` is read-only. They fail only if they cross independent VM regions or violate an enabled section/object strict policy.

### 11.9.2 Diagnostic Precedence for Memory Accesses

Memory diagnostics must be ordered so lower-level safety failures are not hidden by educational warnings.

Recommended precedence:

```text
1. Address arithmetic overflow while computing [address, address + width - 1].
2. Final range not wholly contained in one allocated VM memory region.
3. Permission failure, including `.CONST` write overlap.
4. Section-capacity violation, if section-capacity validation is enabled.
5. Section-image violation, if section-image validation is enabled.
6. Declared-object violation, if declared-object validation is enabled.
7. Uninitialized-read warning or strict error, if uninitialized-read validation is enabled.
8. Non-fatal unaligned-access warning.
```

A strict diagnostic stops execution before mutation. If execution stops at an earlier strict or fatal diagnostic, later warning-only diagnostics for the same access do not need to be emitted.

Default rule: emit at most one diagnostic per enabled validation level for one memory access, in precedence order. Do not combine separate validation-level diagnostics unless the relevant phase explicitly defines a combined diagnostic shape and tests it.

### 11.9.3 Parser Versus Runtime Boundary for Memory Operands

The parser owns syntax, operand shape, width inference, constant-expression evaluation, symbol lookup, and diagnostics for malformed or ambiguous operands.

The runtime memory helpers own final byte-range validation.

The parser must not emit an assembly error merely because a valid MASM-style memory operand may cross a declared-object boundary, section-image boundary, section-capacity boundary, or fixed-layout slack boundary at runtime.

Examples of valid memory syntax that must not be rejected merely by parser-time object or image bounds:

```asm
.data
x DWORD ?
.code
main PROC
    mov eax, DWORD PTR [x + 1]
main ENDP
END main
```

```asm
.DATA?
x DWORD ?
.code
main PROC
    mov eax, 10
    mul [x+1]
main ENDP
END main
```

These examples have valid memory operand syntax and inferable width. They may warn or stop at runtime only according to the selected memory-validation policy and final VM memory range.

Parser/static diagnostics remain appropriate for:

```text
- malformed address syntax;
- unknown symbols;
- ambiguous memory width;
- unsupported addressing modes such as scaled-index forms before they are implemented;
- unsupported executable memory widths such as QWORD/SQWORD in MASM32 Educational Mode;
- constant-expression failures;
- address-expression values that cannot be represented in the simulator's address model;
- obviously illegal `.CONST` direct writes where the instruction form statically names a read-only destination, while preserving mandatory runtime `.CONST` protection for computed writes.
```

Static `.CONST` direct-write diagnostics are an optimization and user-experience improvement. They do not replace runtime `.CONST` permission checks.

### 11.10 Uninitialized-Origin Byte Tracking and Read Diagnostics

`?` and `.DATA?` storage are deterministic zero at program load but must retain metadata that the bytes originated from uninitialized declarations.

Required model:

- Bytes emitted from explicit initializers start initialized.
- Bytes emitted from `?` or `DUP(?)` start uninitialized-origin and runtime-zero-filled.
- Every successful program write marks the written bytes initialized.
- Multi-byte writes initialize every byte in the written range.
- Multi-byte reads in strict/debug mode must check every byte read.
- Default educational mode allows reads from uninitialized-origin bytes without warning.
- Strict/debug modes may warn or error on reads from any uninitialized-origin byte that has not yet been written by the simulated program.

This feature must not change the default runtime value of `?` storage. The default value remains deterministic zero.

### 11.11 Invalid Memory Access Handling and Diagnostic Precedence

When a memory access could trigger multiple diagnostics, the simulator must select exactly one primary fatal diagnostic in this order:

```text
1. Effective-address calculation overflow.
2. Full access range is not contained in any valid memory region.
3. Region permission violation or `.CONST` read-only write.
4. Stack overflow/underflow classification, if the access is in or near the stack region.
5. Allocated-object strict violation.
6. Provenance strict violation.
7. Uninitialized-origin read strict violation.
8. Unaligned-access warning.
9. Provenance warning.
10. Uninitialized-origin read warning.
```

Fatal diagnostics suppress lower-priority warnings unless a lower-priority warning is necessary to explain the fatal error. Successful accesses may emit warnings such as unaligned access, provenance escape, or uninitialized-origin read according to the active validation mode.

### 11.12 Post-30 Memory Layout, Validation, and Metadata Requirements

Post-30 memory work is split into explicit slices: layout policy objects, automatic deterministic sizing, stack/heap metadata, seeded or fresh randomized layout, declared-object maps, allocated-object warning mode, allocated-object strict mode, uninitialized-origin tracking, and uninitialized-read diagnostics.

The first implementation must use named configuration fields rather than hardcoded layout constants. The layout policy must include region bases or placement rules, region sizes, alignment, guard gaps, data/const/data? image sizes, stack and heap requested sizes, validation modes, and deterministic seed state.

Object and uninitialized-origin metadata must have explicit capacity behavior:

- metadata capacity exhaustion is a structured assembly/setup diagnostic;
- the simulator must not silently disable object-bound or uninitialized-origin tracking;
- memory mutation must not occur after metadata setup failure;
- JSON output must include whether a metadata feature is enabled, disabled, or failed setup.

Object-bound classification is based on the full access byte range `[address, address + width - 1]`, with overflow checks. The classifier must distinguish wholly inside object, outside all objects, partial overlap, spanning adjacent objects, padding/gap access, outside all regions, and permission failure.

Diagnostic precedence for memory failures is:

1. address arithmetic overflow or address-size violation;
2. region containment failure;
3. missing permission or `.CONST` overlap;
4. object-bound strict failure;
5. uninitialized-read strict failure;
6. unaligned access warning;
7. object-bound or uninitialized-read warnings in warning modes.


### 11.10 Default Teaching Diagnostics Policy

The simulator should use beginner-friendly teaching diagnostics by default while preserving MASM-compatible execution where safe.

Default teaching diagnostics are warning or notice diagnostics. They must not change the deterministic VM value read, the instruction result, the Program Console output, or the hard runtime safety rules unless their policy explicitly says `strict` or `error`.

The default policy after the teaching-diagnostics default phase is:

```text
uninitialized-read policy: warn
undefined-flag-use policy: warn
meaningful compatibility no-op notices: on
```

The following policies remain available for tests and later settings:

```text
uninitialized-read policy:
  off
  warn
  strict

undefined-flag-use policy:
  off
  warn
  error

compatibility no-op notices:
  off
  on
```

The default `warn` policy for uninitialized reads means:

- bytes from `?`, `DUP(?)`, and `.DATA?` remain deterministic zero-filled at program load;
- reading those bytes before a successful simulated program write emits `uninitialized-read`;
- the read still returns the deterministic zero-filled bytes;
- execution continues unless the user selected strict mode;
- a later successful write initializes the written bytes and suppresses future uninitialized-read diagnostics for those bytes.

The default `warn` policy for undefined flag use means:

- producer instructions still execute under their existing producer-warning policy;
- flag-validity metadata still records which modeled flag values are architecturally invalid;
- a later flag-consuming instruction emits `undefined-flag-use` if it reads an invalid modeled flag;
- the consumer continues using the simulator's deterministic preserved flag value;
- execution stops only if the user selected error mode.

Meaningful compatibility no-op notices are informational diagnostics for MASM constructs that are accepted for paste compatibility but do not perform their real MASM behavior in the simulator. These notices must be non-fatal and must not block execution.

Examples include:

```text
.386 / .486 / .586 / .686
.model flat, stdcall
.stack size, when it records metadata but does not itself execute stack behavior
INCLUDE Macros.inc, while macro expansion remains unsupported
TITLE / SUBTITLE / PAGE
```

Do not emit compatibility no-op notices for constructs whose simulator behavior is already meaningful and user-visible unless there is a specific limitation to explain. For example:

- `INCLUDE Irvine32.inc` should not receive a generic no-op notice merely because it is virtual; it enables the Irvine32 virtual symbol/routine registry.
- `OPTION CASEMAP:ALL` and `OPTION CASEMAP:NONE` should not receive generic no-op notices because they change user-symbol lookup behavior.

Default teaching diagnostics must remain separate from hard errors.

These remain hard errors regardless of diagnostic profile:

- lexer errors;
- parser errors;
- malformed instruction operands;
- ambiguous memory width;
- unsupported instruction forms;
- unsupported executable QWORD/SQWORD memory operations in MASM32 Educational Mode;
- address arithmetic overflow;
- access outside allocated VM memory regions;
- missing memory permission;
- `.CONST` write overlap;
- runtime resource-limit failures.

These remain opt-in unless a later reviewed phase deliberately changes them:

- allocated-object warning/strict validation;
- section-capacity warning/strict validation;
- section-image warning/strict validation;
- provenance or source-intent diagnostics;
- strict uninitialized-read mode;
- undefined-flag-use error mode;
- strict undefined-shift validation;
- broad static-analysis warnings such as dead stores, register-alias hints, or signedness hints.

Default user-facing source-run and browser behavior must use:

```text
uninitialized_read_policy = warn
undefined_flag_use_policy = warn
compatibility_notices = on
```

Low-level unit tests may still construct explicit policies directly. Any user-facing run path that omits a policy must use the teaching defaults.

The opt-out behavior must require an explicit policy value of `off`; missing or omitted policy fields must not silently mean `off` after the default teaching-diagnostics phase.

Tests that need old silent behavior must pass explicit `uninitialized_read_policy = off` or `undefined_flag_use_policy = off`.

This policy does not change default producer warnings for `undefined-shift-flag` or `undefined-modeled-flag`; those remain as already implemented.

### 11.11 Future Diagnostics Audit Checkpoint

The project should not finalize broad diagnostic presets before the implemented MASM subset is mature enough to evaluate warning noise and educational value.

A dedicated diagnostics audit should occur after the major instruction, control-flow, stack/procedure, and Irvine32 routine milestones are substantially implemented, and before a polished diagnostic-settings or warning-preset UI is finalized.

That audit should review:

- default-on teaching diagnostics;
- opt-in warnings;
- strict-stop diagnostics;
- notices;
- diagnostic tags;
- warning noise;
- beginner usefulness;
- MASM compatibility expectations;
- rendered Simulator Messages wording;
- UI grouping.

Until that audit is performed, do not add a broad GCC/Clang-style warning preset taxonomy beyond the narrow default-policy changes explicitly assigned to current phases.

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

### 12.1 Post-30 Stack, Call, Frame, and Procedure Contract

CALL, RET, USES, LOCAL, LEAVE, RET imm16, PROTO, INVOKE, and ADDR depend on completed stack initialization plus `push` and `pop` behavior.

The simulator uses 32-bit VM return tokens for call/return flow. Return tokens are not native addresses. The root return sentinel is `VM_RETURN_TOKEN_ROOT = 0xFFFFFFFFu`, reserved and never emitted as a normal instruction index.

The educational frame model is explicit:

- frame-owning procedures reserve `EBP` as frame pointer;
- `USES` rejects `ESP` and `EBP`;
- LOCAL storage is allocated in declaration order at negative `EBP` offsets;
- local frame size is rounded to a 4-byte boundary;
- procedure epilogue order is LOCAL/frame release, USES restore, return-token pop/validation, and then RET imm16 caller-argument cleanup when applicable;
- failed frame, return-token, or stack validation must not partially mutate state.

`INVOKE` is deterministic in v1: supported user-procedure INVOKE lowering uses DWORD arguments and requires cleanup behavior defined by procedure metadata and RET imm16 phases. Full MASM calling-convention inference, external procedure invocation, Windows ABI behavior, and WinAPI calls remain out of scope.

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


### 13.5 Exact Irvine32 Routine Contracts Required Before Implementation

Before implementing an Irvine32 routine phase, the guide must state exact register, memory, output, input, and flag behavior. Implementations must not infer behavior from routine names alone.

Output routines:

```text
Crlf:
  Appends CRLF or the simulator's documented newline sequence to Program Console.
  Does not write Simulator Messages.
  Register and flag effects must be documented before implementation.

WriteString:
  Input: EDX = address of a null-terminated byte string.
  Reads bytes through checked VM memory helpers until 00h.
  Stops with a runtime diagnostic if no terminator appears before unreadable memory or the configured scan limit.
  Decodes bytes using the simulator's documented console byte policy, initially ASCII-compatible byte-to-character mapping.

WriteChar:
  Input: AL is the character byte. If EAX is referenced in UI text, only AL is consumed.
  Appends exactly one character/control byte according to the console byte policy.

WriteInt:
  Input: EAX interpreted as signed 32-bit integer.
  Appends decimal signed text with a leading minus sign for negative values.

WriteDec:
  Input: EAX interpreted as unsigned 32-bit integer.
  Appends unsigned decimal text.

WriteHex:
  Input: EAX.
  Appends eight uppercase hexadecimal digits by default unless a later phase documents a different width policy.

WriteBin:
  Input: EAX.
  Appends 32 binary digits by default unless a later phase documents grouping or width policy.
```

Input routines:

```text
ReadChar:
  Enters WAITING_FOR_INPUT if no character is buffered.
  On completion, AL receives the character byte.
  Upper EAX byte behavior must be specified by the implementation phase and tested.
  Echo behavior must be specified; default should be no implicit echo unless the phase says otherwise.

ReadInt:
  Parses a signed 32-bit decimal integer from the submitted input line.
  On valid input, EAX receives the value and OF is cleared.
  On invalid or out-of-range input, OF is set and EAX behavior must be documented and tested.
  Whitespace handling, optional sign handling, and newline consumption must be specified.

ReadDec:
  Parses an unsigned 32-bit decimal integer.
  On valid input, EAX receives the value and CF is cleared.
  On invalid or out-of-range input, CF is set and EAX behavior must be documented and tested.

ReadHex:
  Parses an unsigned hexadecimal integer according to the phase contract.
  The phase must specify whether h-suffix, 0x-prefix, bare hexadecimal digits, or mixed forms are accepted.
  Error flag behavior must be documented before implementation.

ReadString:
  Input: EDX = destination buffer, ECX = maximum non-null character count unless the phase explicitly chooses total buffer bytes.
  Writes submitted characters through checked memory helpers.
  Writes a null terminator if space permits.
  EAX receives the number of characters written, excluding the terminator.
  Long input must have documented behavior: truncate with warning, reject, or accept prefix. The chosen behavior must be tested.
```

Debug and utility routines:

```text
DumpRegs:
  Writes to Program Console, not Simulator Messages.
  Must include EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, EIP, EFLAGS, and modeled flags.

DumpMem:
  Must document required input registers before implementation.
  Every byte read must go through checked memory helpers.
  Invalid memory must produce a runtime diagnostic, not partial silent output.

Randomize / RandomRange / Random32:
  Must use deterministic seeded simulator RNG unless the user explicitly opts into nondeterministic behavior.
  Shared URLs must preserve seeds when reproducibility is expected.

WaitMsg:
  Must not block the browser thread.
  It may map to the input-wait protocol or emit a deterministic prompt, according to the phase contract.
```

Unsupported Irvine32 file routines must produce `unsupported-irvine32-file-io` or an equivalent specific diagnostic. They must not attempt host filesystem access.

### 13.6 Post-30 Irvine32 and Macros.inc Virtual Built-In Rules

Irvine32 routines and selected `Macros.inc` conveniences are virtual built-ins provided by the simulator. They do not load host include files, link libraries, expand general MASM macros, or call Windows APIs.

The following implementation policies are mandatory:

- `exit`, `Exit`, and `EXIT` are accepted only through the virtual Irvine32 symbol registry and follow the documented case policy;
- output routines validate output limits before appending when their formatted output is known up front;
- `WriteString` and `DumpMem` use validation-first behavior and append no partial output on memory failure;
- formatted output routines preserve registers and flags unless a routine contract says otherwise;
- input routines enter the shared `WAITING_FOR_INPUT` VM state and resume only through the input protocol;
- invalid numeric input resumes execution with documented flags/register results and does not implicitly re-enter wait;
- deterministic random routines use the named simulator PRNG and seed policy from the guide.

Selected virtual macros such as `mWrite`, `mWriteLn`, and `mReadString` remain dedicated built-ins. Known but deferred macro names receive `unsupported-macro-invocation`; unknown macro-like syntax is not treated as full MASM macro expansion.

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

### 14.4 Post-30 Console, Input, and Byte Encoding Policy

The Program Console is a byte-oriented output stream with deterministic rendering. The first v1 policy is ASCII-focused:

- bytes `00h..7Fh` map to ASCII rendering rules defined by the Program Console byte policy;
- `WriteString` stops before the first `00h` terminator and does not append the terminator;
- output limits are measured against the raw Program Console byte buffer before rendering;
- Simulator Messages have a separate message limit and a reserved final truncation diagnostic;
- UI strings, local preferences, and share URLs must not be treated as Program Console bytes.

Input payload normalization is shared by `ReadChar`, `ReadInt`, `ReadDec`, `ReadHex`, `ReadString`, `WaitMsg`, and macro input built-ins. Submitted text is normalized deterministically before being converted to routine-specific bytes or numbers. Empty input, stale input request IDs, duplicate submission, submit-after-cancel, submit-after-reset, and wrong request kinds must produce stable diagnostics.

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

### 15.1 Post-30 VM State, Waiting, and Debugger Interaction

The VM state model must include enough state to distinguish ready, running, paused, stopped at breakpoint, waiting for input, halted, faulted, and terminated sessions.

When the VM is `WAITING_FOR_INPUT`:

- input submit, input cancel, reset, stop, and breakpoint edits are permitted according to the debugger state matrix;
- Step Into, Step Over, and Continue return a stable `waiting-for-input` debugger error;
- current registers, flags, stack summary, and current source highlight remain inspectable;
- entering wait counts as one executed logical instruction for watchdog and debugger statistics.

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

### 16.2 Post-30 Worker Protocol Determinism

Worker protocol payloads must be structured-clone-safe and JSON-compatible unless a later phase explicitly introduces a binary transfer type. Functions, DOM nodes, cyclic objects, `Map`, `Set`, `BigInt`, `undefined` fields, and binary buffers are rejected or prevented at the protocol boundary in v1.

Signed display fields are allowed only when they remain JSON-compatible and deterministic.

For 8-bit, 16-bit, and 32-bit values, signed decimal display values may be represented as JSON numbers or formatted strings. Existing unsigned numeric fields must not be repurposed as signed fields.

Phase 52A does not implement signed QWORD/SQWORD decimal display. Signed 64-bit display is deferred to a later lossless 64-bit display/protocol phase. Do not use JavaScript `Number` for signed 64-bit decimal conversion. Do not put JavaScript `BigInt` values directly into worker protocol payloads.

Hard worker termination invalidates all sessions, command IDs, input request IDs, breakpoint bindings, run generations, pending VM references, and stale response routing. A fresh worker initialization is required before new Run or Debug commands are accepted.

Debugger commands use a named state-transition matrix. Invalid transitions return stable debugger errors and rendered Simulator Messages when user-visible.

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

### 17.5 Post-30 Resource Accounting

The simulator distinguishes:

- `runInstructionCount`: total logical instructions executed in the current run/debug session;
- `commandInstructionCount`: logical instructions executed by the current Step/Continue/Step Over command;
- repeated string instruction element counts when a phase defines per-element watchdog accounting;
- active wall-clock time measured in worker builds using `performance.now()` and in native tests using an injectable fake monotonic clock;
- raw Program Console byte length before rendering;
- rendered Simulator Messages byte or message limits according to the configured message-limit policy.

Resource-limit diagnostics must be deterministic and must not leave hidden partial state except for explicitly defined partial-progress instructions.

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


### 18.4 Memory Layout and Safety Presets UI

The UI must expose memory layout and validation settings only after the core modes are implemented.

Required controls:

```text
Memory layout:
  Fixed educational layout
  Automatic deterministic layout
  Seeded randomized layout
  Fresh randomized layout

Memory validation:
  Region-only
  Allocated-object warnings
  Allocated-object strict
  Provenance warnings
  Provenance strict
  Uninitialized-read warnings
  Uninitialized-read strict

Invalid memory access handling:
  Stop execution with runtime diagnostic
  Warn and continue only for non-fatal warning modes
```

Required presets:

```text
Beginner/default:
  fixed educational layout
  region-only validation
  fatal invalid region/permission errors

Debug:
  fixed or automatic deterministic layout
  allocated-object warnings
  uninitialized-read warnings

Robustness:
  seeded randomized layout
  allocated-object strict
  provenance warnings
  uninitialized-read warnings
```

Seed requirements:

- Seeded randomized mode must display the active seed.
- Shared URLs must include the seed if deterministic reproduction is expected.
- Fresh randomized mode must display the generated seed after each run so a failing run can be reproduced.
- Tests must use fixed or explicitly seeded layout. They must not depend on fresh random mode.

These controls belong in the later UI/settings phases, not in the core memory-layout phases.

### 18.5 Post-30 Settings, Preferences, and Memory Capability Policy

Simulator settings and local UI preferences are separate.

Simulator settings affect VM behavior, share URLs, and reproducibility. They include memory sizes, layout mode, validation modes, execution mode, watchdog limits, output limits, and safety tiers. Simulator settings have a schema version and migrate or reject atomically.

Local preferences affect only local UI appearance and editor behavior. They are stored best-effort under the exact key defined by the guide and must not block Run, Debug, share import, or source execution if storage is unavailable.

Memory presets use binary units: `1 KiB = 1024 bytes`, `1 MiB = 1048576 bytes`, and `1 GiB = 1073741824 bytes`. Extended and super-extended memory settings require capability probing and local confirmation. Shared URLs requesting local-only or unsupported super-extended settings are rejected atomically rather than silently downgraded.

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
  EAX: 00000000h / u:0 / s:0 -> 00000014h / u:20 / s:20
  AX:  0000h / u:0 / s:0     -> 0014h / u:20 / s:20
  AL:  00h / u:0 / s:0       -> 14h / u:20 / s:20
```

Unchanged aliases should be hidden by default.

### 19.3.1 Integer Value Display Contract

Integer value display must distinguish stored bits from human-readable interpretations.

The VM stores integer register and memory values as fixed-width bit patterns. The browser UI may display those same bits in multiple textual forms. Adding a signed decimal display is presentation-only. It must not change:

- parser behavior;
- IR generation;
- instruction execution;
- register storage;
- memory storage;
- flag behavior;
- sign-extension behavior;
- diagnostic behavior;
- Program Console output;
- Simulator Messages output;
- source-run success or failure;
- runtime memory-change semantics.

Unless a later phase explicitly changes the display policy, integer display rows should show these forms when the displayed width is known:

```text
<hex> / u:<unsigned decimal> / s:<signed decimal>
```

Definitions:

- `hex` is the zero-padded hexadecimal representation for the displayed width.
- `u` is the unsigned decimal interpretation of the same bits.
- `s` is the signed two's-complement decimal interpretation of the same bits at the displayed width.

The signed interpretation is width-aware:

```text
8-bit:   unsigned range 0..255,        signed range -128..127
16-bit:  unsigned range 0..65535,      signed range -32768..32767
32-bit:  unsigned range 0..4294967295, signed range -2147483648..2147483647
```

For register display:

- canonical 32-bit general-purpose registers such as `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`, and `ESP` use 32-bit interpretation;
- 16-bit aliases such as `AX`, `BX`, `CX`, `DX`, `SI`, `DI`, `BP`, and `SP` use 16-bit interpretation;
- 8-bit aliases such as `AL`, `AH`, `BL`, `BH`, `CL`, `CH`, `DL`, and `DH` use 8-bit interpretation;
- `EIP` may use 32-bit signed interpretation only if it is already displayed as a generic integer row;
- `EFLAGS` is not required to show signed decimal. It may continue to show hexadecimal, unsigned decimal, and named modeled flag bits;
- the signed value of an alias is computed from the alias value and alias width, not from the full parent register.

Examples:

```text
EAX = FFFFFFFFh / u:4294967295 / s:-1
AX  = FFFFh / u:65535 / s:-1
AL  = FFh / u:255 / s:-1
```

```text
EAX = 000000FFh / u:255 / s:255
AL  = FFh / u:255 / s:-1
```

The second example is intentional. `EAX` and `AL` display the same low byte, but they have different displayed widths.

For memory-change display:

- the signed interpretation uses the memory row's displayed access width;
- a `BYTE` or `SBYTE` row uses 8-bit interpretation;
- a `WORD` or `SWORD` row uses 16-bit interpretation;
- a `DWORD` or `SDWORD` row uses 32-bit interpretation;
- signed display uses the displayed or access width only. It must not use the declaration's signedness to decide whether to show a signed value;
- both `BYTE` and `SBYTE` rows show signed 8-bit interpretation;
- both `DWORD` and `SDWORD` rows show signed 32-bit interpretation.

Examples:

```text
value DWORD
  FFFFFFFFh / u:4294967295 / s:-1
```

```text
byteValue BYTE
  FFh / u:255 / s:-1
```

This display contract must not imply implicit sign extension. Ordinary `mov` from signed memory still reads the resolved operand width and does not automatically sign-extend. Explicit sign-extension instructions such as `movsx`, `cbw`, `cwde`, `cwd`, and `cdq` own sign-extension behavior.

Normal Simulator Messages diagnostic lines must remain unchanged. Do not add signed register or memory display text to assembly errors, runtime errors, simulator warnings, or execution-complete messages. This display contract changes only existing register and memory value display surfaces.

Source-run JSON and worker protocol policy:

- Existing numeric JSON fields must not be reinterpreted from unsigned to signed.
- If display-ready signed values are added to source-run JSON, they must use explicit field names such as `signedDecimal` or `displaySigned`.
- For 8-bit, 16-bit, and 32-bit values, signed decimal values may be represented as ordinary JSON numbers because they are exactly representable.
- Existing unsigned values and hex strings must remain present unless a later protocol migration explicitly replaces them.
- The worker protocol must remain structured-clone-safe and JSON-compatible.

QWORD/SQWORD and future 64-bit policy:

- Phase 52A does not implement signed QWORD/SQWORD decimal display.
- Signed 64-bit display is deferred to a later lossless 64-bit display/protocol phase.
- Do not use JavaScript `Number` for signed 64-bit decimal conversion.
- Do not put JavaScript `BigInt` values directly into worker protocol payloads.

The display format should be consistent across existing value-display surfaces, including:

- final register state;
- debugger current-state register rows, if implemented;
- last-step register deltas, if implemented;
- step-over aggregate register deltas, if implemented;
- memory-change rows;
- raw memory viewer rows, when implemented and when width is known;
- any future watch-variable display that reuses register or memory value formatting.

If a display context cannot determine the value width, it must not guess a signed interpretation. It should either omit the signed field or mark the width as unknown.

### 19.4 Step-Over Delta

For Step Over, the delta is the aggregate difference from before the call to after it returns.

Example:

```text
Step-over result:
  call SomeProc

Instructions executed inside call: 17

Register changes:
  EAX: 00000000h / u:0 / s:0 -> 0000002Ah / u:42 / s:42
  ECX: 00000005h / u:5 / s:5 -> 00000000h / u:0 / s:0

Memory changes:
  result DWORD: 00000000h / u:0 / s:0 -> 0000002Ah / u:42 / s:42
```

### 19.5 Post-30 Debugger Protocol and UI Contract

Debugger behavior is split into backend state, protocol, UI rendering, breakpoints, continue/pause, step-over, aggregate deltas, memory visualization, and integration smoke phases.

The debugger must define:

- stable session IDs, run generations, command IDs, and stale-response rejection;
- exact `currentInstructionIndex` behavior for normal completion, root return, `exit`, runtime fault, breakpoint stop, and waiting-for-input;
- Step Into behavior from `stopped-at-breakpoint`, including executing the stopped instruction once without re-triggering the same breakpoint;
- Continue chunk size and pause-latency behavior;
- global stop-reason precedence shared by Step Into, Continue, Step Over, breakpoints, input waits, and limits;
- Step Over recursion and early-stop behavior;
- final-diff aggregate register/flag deltas plus ordered memory and console event streams.

Breakpoint binding is source-line based in v1. Breakpoints do not track moved code across edits; after source edits, bindings become unbound until the next successful load/rebind pass.

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
    00h / u:0 / s:0 -> 64h / u:100 / s:100
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
    00000000h / u:0 / s:0 -> 00000064h / u:100 / s:100
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
arr + 1 BYTE: 02h / u:2 / s:2 -> 78h / u:120 / s:120
arr + 2 BYTE: 03h / u:3 / s:3 -> 56h / u:86 / s:86
arr + 3 BYTE: 04h / u:4 / s:4 -> 34h / u:52 / s:52
arr + 4 BYTE: 05h / u:5 / s:5 -> 12h / u:18 / s:18
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

### 20.5 Post-30 Memory Visualization Row Contract

Memory-change visualization uses ordered row objects with stable identity. Each row includes sequence, row ID, region name, address, width, old bytes, new bytes, source instruction index or null, symbol name or null, byte offset when known, and display classification.

Overlapping writes are displayed in execution order by default. Grouped views must not reorder rows unless the grouping is explicitly labeled. Failed writes and validation-first failures produce diagnostics but no successful memory-change rows.

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
- Recover from known unsupported block constructs by skipping to the matching terminator when the terminator is present; if no terminator is present, emit a structured unterminated-unsupported-block diagnostic and stop recovery for that block.
- Avoid cascading noise from inside skipped unsupported constructs.
- Cap the number of diagnostics reported in one pass.
- Never execute a program if any assembly diagnostic was produced.
- Stop immediately on fatal capacity, lexer state, or internal parser errors.

Recoverable unsupported constructs include common textbook/compiler forms such as `STRUCT`, `UNION`, `MACRO`, `INVOKE`, `.IF`, `.WHILE`, `.REPEAT`, `.DATA?`, `.CONST`, `EQU`, `TEXTEQU`, `PROTO`, `LOCAL`, `INCLUDELIB`, `EXTERN`, `PUBLIC`, and `COMM`.


### 21.0 Diagnostic Precision and Precedence Requirements

Diagnostic wording is part of the user-facing product. Feature phases must not be considered complete if they only prove that execution failed; they must also prove that the failure is classified correctly.

Mandatory requirements:

- Diagnostics must preserve line, column, byte offset, and span length whenever the error is tied to source text.
- MASM-invalid code must receive an invalid-syntax or invalid-operands diagnostic, not `unsupported-feature`.
- Planned-but-not-yet-implemented MASM features must receive `unsupported-feature` or a more specific planned-feature diagnostic.
- Explicit non-goals must say they are outside the simulator, not imply future implementation.
- Runtime memory diagnostics must classify invalid address, permission violation, `.CONST` read-only write, stack overflow/underflow, strict object-bounds failure, provenance failure, and uninitialized-origin read failure distinctly when those modes exist.
- A failed access should emit one primary fatal diagnostic according to the memory diagnostic precedence ladder in the Memory Model.
- A parser diagnostic that intentionally invalidates an equate or expression must suppress misleading follow-up diagnostics from later uses of that invalid symbol.
- Multi-diagnostic recovery must report diagnostics in stable source order.
- Source-run must not emit `execution-complete` if lexer, parser, assembly, unsupported-feature, or fatal runtime diagnostics prevented successful completion.

### 21.1 Native Diagnostic Rendering Harness

The project must provide a native-testable diagnostic rendering path that verifies the final user-facing **Simulator Messages** text without requiring an Emscripten build or manual browser session.

This requirement exists because AI-assisted implementation environments may be able to compile and run native C tests but may not be able to build the WebAssembly artifact with Emscripten. Native tests already exercise the C source-run JSON path, but diagnostic quality is not fully covered unless the final browser-style message text is also rendered and compared.

The harness must use **Option A: a Node-based formatter test over real C source-run JSON**.

Required architecture:

```text
native C diagnostic JSON producer
  -> calls the same C source-run entry point used by the Wasm export
  -> prints or returns raw source-run JSON
  -> Node test parses the JSON
  -> Node test calls the same pure web formatter used by the browser UI
  -> test compares exact rendered Simulator Messages text
```

Mandatory rules:

- The native JSON producer must link the same C parser/source-run/Wasm-facing API code used by the browser export path, including `src/wasm/wasm_api.c` or its current equivalent.
- Emscripten export annotations must compile as harmless no-ops in native builds.
- The diagnostic formatter used by the browser must be exposed as a pure JavaScript/TypeScript function that can be imported by Node tests without creating DOM elements, starting a Worker, loading Wasm, or mutating browser state.
- The Node harness must call that browser formatter directly. It must not maintain a second, divergent copy of the message-formatting rules.
- If the current UI formatter is embedded in DOM bootstrap code, it must be extracted into a side-effect-free formatter module before this phase is considered complete.
- The harness must check both the structured diagnostic JSON and the final rendered message text.
- Exact text comparison is required for stable diagnostic examples. Substring-only checks are allowed only for volatile details that are explicitly documented as volatile.
- The harness must preserve and verify diagnostic ordering for multi-diagnostic programs.
- The harness must verify that source-run does not emit `execution-complete` when lexer, parser, unsupported-feature, or assembly diagnostics prevent execution.
- The harness must distinguish native-source-run failures from stale browser/Wasm artifacts. It must not claim that the served web app was verified unless a browser/Wasm test was actually run.

The harness must cover representative diagnostics from each current diagnostic layer:

- lexer error: invalid hexadecimal literal;
- lexer error: unterminated string;
- parser/source error: unknown symbol;
- unsupported feature: unsupported directive or recognized deferred construct;
- MASM-invalid syntax: ambiguous memory width;
- runtime error: invalid address;
- runtime error: read-only `.CONST` write;
- simulator warning: unaligned memory access;
- successful execution: `execution-complete` informational message.

For every golden diagnostic case, the test fixture should record:

- source program;
- raw source-run JSON;
- expected rendered Simulator Messages text;
- reason the diagnostic belongs to the chosen category.

Failure output from the harness must print:

- the source program;
- the raw JSON returned by the native C source-run path;
- the rendered Simulator Messages text;
- the expected text;
- a diff or enough context to identify the mismatch.

Manual browser verification remains required when any of these change:

- Emscripten build scripts;
- `web/dist` artifact generation;
- Worker loading or worker protocol shape;
- UI DOM rendering;
- CodeMirror diagnostic integration;
- source-run JSON schema;
- formatter module import path or public API.

Manual browser verification is not a substitute for this harness. The harness is a permanent regression test for diagnostic message quality in native/Node environments.

### 21.2 Post-30 Editor Diagnostic and Source Mapping Contract

Backend byte offsets are authoritative. Browser editor integrations must convert backend UTF-8 byte offsets and span lengths into CodeMirror UTF-16 document offsets through a tested mapping utility tied to the exact source snapshot parsed by the backend.

Editor diagnostics project into CodeMirror lint diagnostics with `from`, `to`, `severity`, and `message` fields. The projection also carries simulator metadata such as diagnostic code, category, source snapshot hash, diagnostic ID, execution-blocking status, and navigation availability.

When source changes after diagnostics were produced, editor markers are removed immediately. Simulator Messages remain visible but are marked stale and non-navigable. Attempting to navigate a stale diagnostic emits `ui-diagnostic-source-stale`.

Diagnostic marker caps, gutter marker caps, and summary diagnostics are mandatory to avoid unbounded UI work.

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

### 22.1 Post-30 Share URL and Import Contract

Share URLs are mandatory v1 behavior. They encode share-safe project state only: source text, simulator settings that affect execution, breakpoints if included by schema, and schema metadata. They must not encode Program Console output, Simulator Messages, runtime memory, pending input, register state, debugger history, local UI preferences, or private transient state.

Encoding uses stable JSON key order, UTF-8 bytes, the named compression codec wrapper, and strict canonical no-padding base64url. The decoder rejects duplicate parameters, non-canonical base64url, unsupported versions, malformed compressed data, unsafe settings, local-only settings, and schema violations. Import is atomic: validation failure leaves current editor text, settings, breakpoints, and run state unchanged.

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

### 23.1 CodeMirror Editor, Preferences, and Accessibility Contract

CodeMirror integration is mandatory v1 behavior and remains UI-only. The C99 parser/VM remains the semantic source of truth.

Editor and preference behavior must include:

- local preference schema versioning and atomic validation/apply behavior;
- best-effort `localStorage` persistence with stable UI diagnostics on failure;
- theme reconfiguration without editor destruction, preserving source text, selection, undo history, diagnostics, breakpoints, current-instruction highlight, focus, and worker state;
- `system` appearance mode using `matchMedia("(prefers-color-scheme: dark)")` when present and deterministic fallback when absent;
- MASM highlighting and indentation as editor extensions only;
- visible keyboard focus indicators, accessible labels, live-region state feedback, and keyboard-only end-to-end flows.

Final release requires accessibility checks, responsive/mobile layout checks, current-instruction highlighting, breakpoint gutter interaction, diagnostic click-to-source navigation, and release artifact/hash verification.

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
- Native diagnostic rendering should be implemented immediately after nested `DUP` support. It is test infrastructure, not MASM syntax, and must make final Simulator Messages text testable without Emscripten by using the real C source-run JSON path plus the browser formatter in Node.
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


### 27.1 Instruction Phase Contract Template

Every future instruction phase must define the following before implementation. AI-assisted coding prompts should include this table explicitly.

```text
Instruction(s):
Accepted operand forms:
Rejected operand forms:
Width resolution rules:
Immediate range rules:
Destination mutation:
Source mutation:
Memory read behavior:
Memory write behavior:
Flags read:
Flags written:
Modeled flags deliberately unchanged:
Real x86 flags not yet modeled:
Runtime errors:
Assembly diagnostics:
Source locations to report:
Required parser tests:
Required executor tests:
Required source-run JSON tests:
Required rendered Simulator Messages tests:
Default browser/manual smoke program:
Future behavior explicitly not implemented:
```

High-risk instruction groups such as `MUL`, `IMUL`, `DIV`, `IDIV`, `CALL`, `RET`, `INVOKE`, string instructions, and high-level MASM flow must not be implemented from informal descriptions. Their phase text must specify exact register operands, implicit operands, flags, overflow/divide errors, and memory behavior.

### 27.2 Post-30 Release, Documentation, and Regression Gate

The v1 release gate must run these required categories:

- native C unit and integration tests;
- parser tests;
- source-run JSON tests;
- Node diagnostic-rendering tests;
- browser/worker smoke tests;
- Wasm/Emscripten build and source-run validation;
- static documentation and supported-syntax checks;
- release artifact inventory and SHA-256 hash manifest.

Environment-dependent suites may be skipped only with an explicit reason in the release report. Required suites failing or missing cause the release gate to fail.

User-facing documentation must be generated from, or mechanically checked against, the implemented feature/test manifest. Examples referencing unsupported future features are permitted only in the known-unsupported/non-goal corpus.

## 28. Future Roadmap

This section is a high-level product roadmap only. It does not override the canonical post-30 implementation sequence in the implementation guide or the integration status in Section 29.

The implementation guide assigns v1-relevant textbook MASM/Irvine32 features to concrete phases. Features below remain either late roadmap, optional post-v1 work, or explicit non-goals unless a later specification revision promotes them.

Concrete v1 roadmap themes:

- MASM compatibility corrections for existing memory syntax.
- MASM32 header directives.
- `.DATA?` and `.CONST` data sections.
- Numeric equates and constant expressions.
- Nested `DUP` and initializer expressions.
- Native diagnostic rendering harness for exact Simulator Messages text.
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

## 29. Canonical Post-30 Roadmap and Thematic Integration Status

The post-Milestone-30 roadmap is now integrated into the thematic sections above and into the canonical implementation guide. The implementation guide owns exact phase numbers and phase-level tasks. This specification owns product boundaries and stable behavior.

The following policies are final for v1:

- Extended 32-bit Mode remains before the v1 release gate, but true x64 MASM, `ml64`, Windows ABI, PE loading, object linking, and WinAPI execution remain non-goals.
- CodeMirror/editor integration, local preferences, share URLs, debugger UI, accessibility, and release-gate validation are mandatory v1 behavior.
- Local preference storage is best-effort and non-blocking; share URL import/export is mandatory and deterministic.
- The supported syntax reference must reflect implemented behavior only, not future roadmap behavior.
- External references are used to inform mirrored or intentionally divergent behavior; the simulator spec and implementation guide remain the source of truth.

Completed Phases 0-30 are preserved. Post-30 phases are renumbered sequentially from Phase 31 in the canonical implementation guide; old planning-batch labels are intentionally omitted.
