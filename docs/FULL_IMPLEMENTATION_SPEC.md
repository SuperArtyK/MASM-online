# Online MASM32 Educational Simulator - Full Implementation Specification

> **Canonical source-of-truth note:** This file is paired with `INCREMENTAL_IMPLEMENTATION_GUIDE.md`. Together they are the current reviewed source-of-truth revision. This specification owns product boundaries, stable behavior, stable cross-cutting rules, current/future/non-goal distinctions, and product-level diagnostic policy. It does not own phase numbering, per-phase task lists, required tests, or acceptance criteria; those remain in the paired implementation guide.


## 1. Project Goal

Build a static, browser-based educational simulator for MASM32-style assembly programs. The project runs entirely on the client side and uses a C99 virtual machine compiled to WebAssembly with Emscripten.

The simulator is intended for learning, experimentation, debugging, and sharing small MASM32/Irvine32-style console programs.

The core product is not a native MASM compiler and not a full Windows/x86 emulator. It is a MASM-like source parser plus an internal virtual machine.

The current implementation supports only the MASM32 Educational Mode subset completed through the latest accepted implementation milestone. A feature is current behavior only when it has been implemented, tested, and documented by the canonical implementation guide and the corresponding repository state.

The intended complete v1 roadmap incrementally models registers, currently selected flags, memory, selected stack behavior, selected control flow, selected Irvine32 routines, resource limits, and educational diagnostics. Roadmap language in this specification must not be read as saying that stack behavior, control flow, Irvine32 routines, debugger behavior, URL sharing, input routines, or other future systems are already implemented.

When describing current project status, future assistants must distinguish these three categories:

- implemented behavior: behavior completed by the latest accepted milestone and present in the current repository archive;
- planned v1 behavior: behavior specified for a later implementation-guide phase but not yet implemented;
- non-goal behavior: behavior outside the simulator boundary, such as native x86 execution, Windows API execution, PE loading, object linking, host filesystem access, or full MASM macro compatibility.

If the current repository, latest milestone report, and guide disagree about whether a feature is implemented, the assistant must treat the feature as not confirmed until it verifies the repository tests or explicitly reports the uncertainty.

### 1.1 Current-status Surface Hygiene

The project has two related but distinct status values:

- **Repository/archive milestone**: the latest accepted project state, including runtime work, parser work, UI work, diagnostics work, documentation work, test-runner work, verification-ergonomics work, or repository maintenance.
- **Runtime/source-run MASM behavior phase**: the latest implemented MASM/source execution behavior that is reported through source-run JSON, worker protocol results, browser runtime-status text, supported-syntax current-status text, Wasm/source-run status fields, or tests that assert runtime behavior metadata.

These two values are usually the same for runtime behavior phases. They may differ after maintenance, documentation, display-only, test-runner-only, verification-ergonomics, diagnostic-copy, or repository-cleanup phases.

Current-status surfaces must not collapse these two values into vague phrases such as:

```text
current milestone
this milestone
implemented through the current milestone
unsupported in this milestone
not supported by the current milestone
```

Current-status surfaces include, at minimum:

- `README.md` current-scope and current-status text;
- `docs/SUPPORTED_SYNTAX.md` current-status and expected-diagnostic text;
- browser runtime-status text;
- worker/protocol status text;
- source-run JSON phase/status fields and any human-readable status strings carried in source-run JSON payloads;
- Wasm/source-run status fields;
- test assertions that describe runtime/source-run metadata;
- user-facing diagnostics rendered in Simulator Messages;
- worker-generated `ui-error` messages;
- newly created milestone reports and current handoff/status summaries, while historical milestone reports remain historical evidence and do not need retroactive cleanup unless the user explicitly asks for historical report cleanup.

When the repository/archive milestone and runtime/source-run MASM behavior phase differ, current-status documentation, newly created milestone reports, and current handoff/status summaries must state both values explicitly.

Use this label format:

```text
Repository/archive milestone:
Phase <N or suffix> - <phase title>

Runtime/source-run MASM behavior phase:
Phase <M> - <runtime behavior phase title>
```

Example after a maintenance-only phase:

```text
Repository/archive milestone:
Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics

Runtime/source-run MASM behavior phase:
Phase 56 - Unsigned DIV
```

Do not update runtime/source-run phase metadata merely because a maintenance-only phase has advanced the repository/archive milestone. Runtime/source-run phase metadata advances only when the target phase explicitly changes runtime-visible MASM/source behavior or explicitly requires metadata advancement.

For split feature families, current-status wording must identify which layer is implemented.

Examples:

- A parser/lowering phase may make syntax accepted and lower metadata without making the runtime behavior executable.
- A later runtime phase may execute already-lowered metadata without adding new syntax.
- A maintenance phase may add tests, documentation, or report hardening for an already implemented behavior without advancing runtime/source-run MASM behavior phase metadata.
- A debugger/editor phase may add source navigation, current-instruction highlighting, breakpoint binding, or editor marker behavior for an already executable instruction without changing the MASM syntax subset.

Do not collapse these layers into a broad feature label.

For branch/control-flow features, avoid vague statements such as:

```text
control flow is supported
control flow is unsupported
branches are implemented
branches are not implemented
```

Use behavior-specific wording instead. For example:

```text
Direct `jmp label` parser/lowering is implemented.
Direct `jmp label` runtime execution is implemented.
Conditional jumps remain future work.
Debugger breakpoint binding for branch target lines remains future work.
```

This rule prevents assistants from treating parser support, runtime execution, debugger behavior, and editor navigation as the same milestone.


Historical milestone reports may retain milestone-relative wording because they document what was true at the time. Current documentation, current supported-syntax text, live diagnostics, current browser status text, current protocol status text, current handoff/status summaries, and current tests must use stable behavior-specific wording.

Target complete-v1 user capabilities, not necessarily current implementation:

The following list describes the intended complete v1 user experience. It is not a statement that every listed capability is implemented in the current repository. Current support remains limited to the latest accepted repository/archive milestone and, for MASM/source execution behavior, the latest runtime/source-run MASM behavior phase. Future assistants must not claim that an item in this list is currently supported unless the current canonical guide, current repository state, tests, or latest milestone evidence confirm that support.

For complete v1, a user should be able to:

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

This section describes the intended complete v1 MASM32 Educational Mode capability set. It is not a statement that every listed capability is implemented in the current repository state.

Current project behavior is only the subset completed by the latest accepted repository/archive milestone, present in the current repository archive, and verified by the current tests.

Current MASM syntax and execution behavior are only the subset completed by the latest runtime/source-run MASM behavior phase. If the latest repository/archive milestone and the runtime/source-run MASM behavior phase differ, use the runtime/source-run MASM behavior phase to describe supported MASM syntax, parser behavior, VM behavior, executor behavior, runtime/source-run phase metadata, and supported-syntax documentation.

Target v1 capabilities:

- MASM32-style syntax subset.
- 32-bit general-purpose registers.
- Flat simulated memory model.
- Simulated `.code`, `.data`, `.DATA?`, `.CONST`, heap, and stack regions.
- Stack execution behavior only after the relevant stack phases are implemented.
- Selected Irvine32-style console routines only after their specific routine phases are implemented.
- Source-level debugging only after the debugger phases are implemented.
- Width-aware register, flag, memory-change, stack, and execution-state display where the relevant data is implemented and available.

This mode targets beginner and intermediate MASM32/Irvine32 console programs while remaining a simulator-defined educational subset. It must not be described as a native MASM compiler, full x86 emulator, Windows emulator, PE loader/linker, WinAPI simulator, host-filesystem environment, or full MASM macro assembler.

Future assistants must not infer implementation status from this target capability list. Before claiming that a capability is implemented, verify that the capability is present in the latest accepted repository/archive state, covered by the implementation guide phase history, and covered by tests or milestone evidence.

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

The complete-v1 browser source editor target is **CodeMirror 6**, not a raw `<textarea>`, for the polished editor experience. CodeMirror 6 is selected because it is permissively licensed, modular, and designed for custom language extensions. Until the CodeMirror implementation phases are completed and accepted, a repository may still contain an earlier editor shell; that earlier shell must not be treated as the final editor contract.

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

`.CONST` behavior is mandatory and phase-sensitive:

- Phase 57I - .CONST Uninitialized Storage Acceptance accepts `.CONST ?` and `.CONST DUP(?)` as read-only `.CONST` storage with deterministic visible bytes and preserved uninitialized-origin metadata.
- `.CONST` declarations using explicit initialized values remain accepted as initialized read-only storage.
- Uninitialized-origin storage includes `.DATA?`, compatible `.data` `?` / `DUP(?)` declarations, and Phase 57I accepted `.CONST ?` / `.CONST DUP(?)` declarations.
- Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy owns configurable declaration diagnostics for `.CONST ?` and `.CONST DUP(?)`; its default behavior is a non-fatal warning.
- `.CONST` creates read-only storage in every implementation state.
- `.CONST` must be protected by final effective address range, not only by symbol metadata.
- The preferred implementation is a dedicated read-only `.const` VM memory region.
- An acceptable implementation may use a protected-range table, but every central VM memory write helper must check that table before committing writes.
- Every write whose final effective byte range overlaps `.CONST` storage must fail, even if only one byte overlaps.
- The check must apply after effective-address calculation and before memory mutation.
- The check must apply to direct symbol writes, symbol-offset writes, bracketed symbol writes, `OFFSET`-derived indirect writes, displacement writes such as `[eax + 3]`, arithmetic-derived addresses, and numeric/computed addresses that happen to land in `.CONST` storage.
- Parser/static diagnostics are required for obvious `.CONST` destinations such as `mov limit, 20`, `mov limits[4], 99`, `mov DWORD PTR [limit], 20`, `add limit, 1`, `neg limit`, and `xchg eax, limit`.
- Parser/static diagnostics are not sufficient. Runtime write protection must still be enforced by the memory write path.
- Reads from `.CONST` must work normally.
- Failed `.CONST` writes must not create successful memory-change rows.

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


#### 8.1.3A Reserved Words and User-Defined Symbols

MASM reserved words are not valid user-defined symbols by default.

This rule is separate from `OPTION CASEMAP`.

`OPTION CASEMAP` controls case-sensitive or case-insensitive lookup for accepted user-defined symbols. It does not make reserved words available as user-defined symbols. It also does not make instruction mnemonics, directives, registers, operators, data type names, `PTR` width names, virtual include names, or recognized Irvine32 routine names case-sensitive.

Reserved-word matching is case-insensitive. For example, `loop:`, `LOOP:`, and `Loop:` all conflict with the `LOOP` instruction mnemonic.

This specification does not require the simulator to import or fully reproduce the complete MASM reserved-word table in one step. Instead, the simulator must reject names that conflict with words it already recognizes as reserved.

Reserved words include, at minimum:

- every instruction mnemonic currently implemented by the simulator;
- every planned instruction mnemonic that the current parser already recognizes as an instruction keyword, branch keyword, or unsupported instruction-family keyword;
- registers and register aliases;
- directives recognized by the parser, including accepted, metadata-only, no-op, unsupported, deferred, and non-goal directives;
- operators recognized by the parser;
- data type names;
- `PTR` width names and signed `PTR` width aliases;
- virtual include names where parsed as include targets;
- recognized Irvine32 routine and terminator names classified by the Phase 41 - Virtual Irvine32 Symbol Registry or its direct successor as simulator-visible routine names;
- other MASM or simulator-recognized keywords that the parser treats as reserved for diagnostics or compatibility classification.

A future phase that adds a new recognized keyword, instruction mnemonic, directive, operator, data type, `PTR` width name, virtual include name, or Irvine32 registry name must update the reserved-word classification path at the same time unless the phase explicitly documents a different MASM-compatible rule.

Do not hardcode a second independent Irvine32 reserved-name list in the parser. Reserved-name checks for Irvine32 routine and terminator names must use the centralized Phase 41 - Virtual Irvine32 Symbol Registry or a small documented reserved-word query wrapper over that registry.

By default, these declarations must be rejected:

```asm
.code
main PROC
loop:
main ENDP
END main
```

```asm
.data
mov DWORD 1
eax DWORD 1
DWORD DWORD 1
OFFSET DWORD 1
```

The diagnostic should point at the declaration token whenever possible and explain that the name is reserved.

Suggested diagnostic code:

```text
reserved-word-symbol
```

Suggested wording shape:

```text
'<name>' is a reserved MASM <reserved-kind> and cannot be used as a <symbol-kind>.
```

Examples:

```text
'loop' is a reserved MASM instruction mnemonic and cannot be used as a code label.
'eax' is a reserved MASM register name and cannot be used as a data symbol.
'OFFSET' is a reserved MASM operator and cannot be used as an equate name.
```

A reserved-word declaration must not be inserted into any user-symbol table. If parser recovery continues, later references to that spelling may produce follow-on diagnostics such as `unknown-symbol`, `invalid-branch-target`, or parser-specific keyword diagnostics. The declaration-site `reserved-word-symbol` diagnostic is the primary diagnostic and should be present whenever the parser can identify the declaration.

Normal non-reserved labels and symbols remain valid:

```asm
.code
main PROC
again:
    inc eax
    jmp again
main ENDP
END main
```

`OPTION CASEMAP:NONE` does not make reserved words available as symbols. These remain rejected:

```asm
OPTION CASEMAP:NONE

.code
main PROC
loop:
main ENDP
END main
```

```asm
OPTION CASEMAP:NONE

.code
main PROC
LOOP:
main ENDP
END main
```

`OPTION NOKEYWORD` remains unsupported until a later phase explicitly implements it. Until such a phase is accepted, `OPTION NOKEYWORD` must not be treated as silently enabling reserved-word identifiers.

A future `OPTION NOKEYWORD` phase must explicitly define all of the following before changing this behavior:

- accepted `OPTION NOKEYWORD` syntax;
- source-order behavior;
- whether disabled keywords can still be used as instructions, directives, or operators;
- whether disabled keywords can be used only as user-defined symbols;
- interaction with `OPTION CASEMAP:ALL`;
- interaction with `OPTION CASEMAP:NONE`;
- interaction with code labels, procedure names, data symbols, equates, macros, user-defined types, and future local labels;
- diagnostics for ambiguous parsing after a keyword is disabled;
- whether keyword recognition can be restored;
- whether disabling one keyword affects aliases or related instructions;
- exact structured and rendered diagnostics.

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
- `OPTION NOKEYWORD`, including forms such as `OPTION NOKEYWORD:<LOOP>`, until a future keyword-control phase explicitly implements reserved-word disabling behavior.
- `OPTION DOTNAME` and `OPTION NODOTNAME`
- unsupported `OPTION LANGUAGE` forms
- conditional assembly directives such as `IF`, `IF2`, `IFDEF`, `IFNDEF`, `IFE`, `IFB`, `IFNB`, `ELSE`, `ELSEIF`, and `ENDIF`
- conditional error directives such as `.ERR`, `.ERRB`, `.ERRDEF`, `.ERRE`, and `.ERRNZ`
- listing-control directives such as `.LIST`, `.NOLIST`, `.CREF`, `.NOCREF`, and `.TFCOND`, unless later accepted as no-ops
- advanced processor/vector directives such as `.387`, `.MMX`, `.XMM`, and `.K3D`
- safety/object-format directives such as `.SAFESEH`, `.FPO`, `PUSHCONTEXT`, and `POPCONTEXT`


`OPTION NOKEYWORD` is broader than ordinary option parsing because it can change whether a reserved word is recognized as a keyword or accepted as a user-defined symbol. Until a future phase explicitly implements that behavior, the simulator must continue to reject reserved-word user-symbol declarations by default.

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

- `?` reserves storage. Runtime bytes are deterministic zero-filled at program load while retaining metadata that the declaration was originally uninitialized until overwritten by the simulated program. After Phase 53C - Default Teaching Diagnostics for Existing Warning Modes, omitted/default user-facing source-run behavior warns on these reads; explicit `off` preserves the older silent deterministic-zero behavior, and explicit `strict` stops before consuming uninitialized-origin bytes.
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
- Duplicate names across data symbols, labels, procedures, and equates must be rejected unless a later phase explicitly introduces separate scopes. The diagnostic must identify the earlier symbol category when that category is available. If the implementation cannot determine the earlier category, the diagnostic must state that the name conflicts with an existing symbol without guessing.
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

Detailed examples for this parser/runtime validation boundary are centralized in Section 11.9.3 - Parser Versus Runtime Boundary for Memory Operands. Do not duplicate those examples here unless the new example is specifically about addressing-form syntax rather than memory-validation policy.

### 8.6 Instructions

The simulator should implement instructions in staged, testable groups. The goal is educational MASM32/Irvine32 compatibility, not full x86 coverage.

#### 8.6.1 Baseline and Early Textbook Instructions in the v1 Roadmap

This subsection lists baseline and early textbook instruction families that belong in the v1 roadmap. It is not the current implemented-instruction list.

The authoritative current implementation status for any instruction is determined by all of the following:

1. the latest completed milestone report and repository state as evidence of what has been implemented;
2. `docs/SUPPORTED_SYNTAX.md`;
3. the current implementation guide phase ledger;
4. passing parser, source-run, executor, structured diagnostic, and rendered Simulator Messages tests for that instruction.

Milestone reports and repository state are evidence of what has been implemented. They do not override the canonical spec and guide when behavior must be corrected intentionally.

A future implementation assistant must not implement, document as supported, or assume working behavior for an instruction merely because it appears in this roadmap list. Each instruction must still be implemented only in its owning guide phase, with that phase's accepted syntax, rejected syntax, diagnostics, non-goals, and tests.

Baseline arithmetic, data movement, control flow, stack, and address computation roadmap items:

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

#### Multiply and divide family flag-validity policy

The simulator deliberately separates deterministic displayed flag bits from architectural flag validity metadata. For most instructions whose owning phase says a modeled flag is architecturally undefined, the simulator should preserve a deterministic bit value and mark that modeled flag invalid in Phase 50A flag-validity metadata.

The multiply/divide family has an explicit exception when its owning implementation-guide phase says so. This exception preserves the policies from Phase 53 - Unsigned MUL, Phase 54 - One-Operand Signed IMUL, Phase 55 - Two- and Three-Operand IMUL Forms, Phase 56 - Unsigned DIV, and Phase 57 - Signed IDIV. The affected instruction families are:

- `mul`;
- one-operand `imul`;
- two- and three-operand `imul`;
- `div`;
- `idiv`.

If the owning implementation-guide phase for one of these instructions explicitly says that the instruction preserves currently modeled flags or preserves Phase 50A flag-validity metadata, that phase-specific MASM32 Educational Mode rule is authoritative.

Future assistants must not infer from real x86 documentation alone that multiply or divide instructions should mark `CF`, `ZF`, `SF`, or `OF` invalid. Any change to multiply/divide flag-validity behavior must be a deliberately reviewed flag-policy phase. Such a phase must include:

- native executor tests;
- source-run tests;
- flag-validity metadata tests where metadata is observable through tests;
- no-partial-mutation tests for failing memory/divide paths;
- exact rendered Simulator Messages tests if any user-visible warning or error text changes.

Until such a future flag-policy phase exists, successful `div` and successful `idiv` preserve the currently modeled flag bits and preserve existing flag-validity metadata exactly as specified by their owning phases.

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

### 11.9.1 Cross-Region, Cross-Section, Protected-Region, and `.CONST` Rules

A single source-level memory access must not be stitched across two independent VM memory regions.

A memory access is evaluated on its final inclusive byte range:

```text
[address, address + access_width - 1]
```

The range calculation must be checked for unsigned address overflow before region lookup or diagnostic classification.

A memory access is a **cross-region access** when the final inclusive byte range is not wholly contained in one allocated VM memory region. Cross-region access is a mandatory Level 1 runtime memory failure. It is not an optional educational warning and it must not be controlled by section-capacity, section-image, declared-object, uninitialized-read, unaligned-access, browser diagnostic settings, or any other opt-in diagnostic policy.

A cross-region access must stop execution before the access is completed. For a failing write, no bytes may be written. For a failing read, the read value must not be consumed by the instruction. The simulator must not split the access into smaller per-region operations. The simulator must not partially execute the access to produce a more specific diagnostic.

A **protected memory region** is a VM memory region, or a protected VM-region subrange, that the current access is not allowed to enter or span for the current access kind. Protection is access-kind-sensitive.

Current `.CONST` protection rules:

- `.CONST` is protected for writes because `.CONST` storage is read-only.
- `.CONST` is not protected against ordinary reads merely because it is read-only.
- A wholly-contained read from `.CONST` is allowed unless another mandatory validation or enabled strict validation rejects it.
- A wholly-contained write whose final byte range overlaps `.CONST` fails as a permission/read-only diagnostic.
- A cross-region read or write whose final byte range intersects the `.CONST` byte range fails as a cross-region diagnostic. For a cross-region read, this does not mean `.CONST` is unreadable; it means one memory access cannot span independent VM regions.

For this section, **overlaps**, **crosses/overlaps**, and **intersects** mean that the requested final inclusive byte range shares at least one byte with the protected region's byte range. Implementations and tests should use this exact byte-range intersection rule rather than relying on the starting address alone.

When a cross-region access intersects a protected memory region and layout metadata can identify that protected region, the primary diagnostic code must be:

```text
region-boundary-crossing
```

The rendered message must identify:

- the access kind, `read` or `write`;
- the starting address of the access;
- the access width in bytes;
- the inclusive final byte range;
- the protected region name;
- the runtime start address of the protected region;
- that the access is not allowed;
- that the program stopped before access.

Canonical rendered-message shape:

```text
Cross-region memory <read|write> at <address> for <N> bytes. The memory address range <start>..<end> crosses/overlaps a protected memory region, <region-name>, that starts at <region-start>. This is not allowed; program stopped before access.
```

For `.CONST` cross-region diagnostics, `<region-name>` must be:

```text
.CONST
```

The `<region-start>` value must come from the active runtime layout metadata. It must not be hardcoded to the fixed-layout default address. This requirement applies to fixed educational layout, automatic deterministic layout, seeded randomized layout, and fresh randomized layout.

A cross-region access that does not intersect a known protected region remains a mandatory Level 1 runtime memory failure, but it does not use the protected-region message shape unless another protected region is involved. It may use the ordinary Level 1 region/range diagnostic for the implementation's existing invalid-address or invalid-region path.

A write whose final byte range is wholly contained in one VM memory region but overlaps read-only `.CONST` storage must fail as a runtime permission/read-only diagnostic. The expected structured diagnostic code for the wholly-contained `.CONST` write path remains:

```text
permission-denied
```

This means `.CONST` write and `.CONST` cross-region diagnostics are intentionally split by the low-level failure shape:

```text
Wholly-contained write overlapping `.CONST`:
  permission-denied

Cross-region read or write whose final range intersects `.CONST`:
  region-boundary-crossing

Cross-region access that does not intersect `.CONST` or another known protected region:
  ordinary Level 1 region/range diagnostic
```

Do not classify a diagnostic by static symbol identity alone. The final effective address, access width, final byte range, active VM region metadata, permissions, and active layout metadata are authoritative.

If `.data`, `.DATA?`, and `.CONST` are represented as subranges inside one shared allocated VM memory region, crossing from one section subrange into another is not automatically a Level 1 cross-region error. In that layout, the access is classified by mandatory region permissions and by the enabled optional section-capacity, section-image, or declared-object validation levels.

Default behavior:

```text
- Crossing section boundaries may execute if the final byte range is inside one readable/writable allocated VM region and permissions allow the access.
- Crossing declared-object boundaries may execute if the final byte range is inside one readable/writable allocated VM region and permissions allow the access.
- Writes overlapping `.CONST` still fail.
- Cross-region accesses still fail.
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

`.CONST` write protection and cross-region access validation are mandatory Level 1 behavior. They are not warning-only teaching diagnostics.

Reads from `.CONST` are allowed if the full range is otherwise valid. A read that intersects `.CONST` fails only when it crosses independent VM regions, violates address/range validation, or violates another mandatory or enabled strict validation. It must not be rejected merely because `.CONST` is read-only.

Phase 57L `.code` access-denial diagnostics reuse the protected-region diagnostic model when applicable. When a cross-region access intersects `.code`, the diagnostic identifies the no-access `.CODE/_TEXT` region and uses the runtime `.code` base address from layout metadata. Implementations must not hardcode fixed-layout addresses. The `.CODE/_TEXT` wording is diagnostic copy only; `_TEXT` remains an unsupported MASM/object segment symbol and is not an addressable alias for the simulator's internal `.code` region.

Required no-partial-mutation behavior for fatal cross-region, protected-region-overlap, or `.CONST` write failures:

```text
- registers are unchanged except for instructions that completed before the failing instruction;
- modeled flags are unchanged by the failing instruction;
- flag-validity metadata is unchanged by the failing instruction;
- memory is unchanged by the failing instruction;
- Program Console output is unchanged by the failing instruction;
- no successful memory-change row is created for the failing instruction;
- no execution-complete message is emitted after the fatal diagnostic.
```

### 11.9.2 Authoritative Diagnostic Precedence for Memory Accesses

Memory diagnostics must be ordered so lower-level safety failures are not hidden by educational warnings or optional validation policies.

This subsection is the authoritative current memory-access diagnostic precedence for the implemented post-30 memory-validation model. Later summaries in this specification must not override this order. If a future phase adds a new memory validation layer, that phase must either update this subsection directly or add a clearly named superseding subsection with tests proving the new precedence.

For every runtime memory read or write, compute the final effective address and access width first. The final inclusive byte range is:

```text
[address, address + access_width - 1]
```

Apply memory diagnostics in this order:

```text
1. Address arithmetic overflow while computing [address, address + width - 1].

2. Region-boundary failure:
   - the final byte range is not wholly contained in one allocated VM memory region.
   - if the crossed range intersects a known protected VM memory region for the current access kind, report region-boundary-crossing and include protected-region context.
   - if the crossed range does not intersect a known protected VM memory region, report the ordinary Level 1 region/range diagnostic.

3. Permission failure inside the containing VM region:
   - includes wholly-contained writes whose final byte range overlaps read-only `.CONST`.
   - direct or wholly-contained `.CONST` write failures remain permission-denied.

4. Section-capacity violation, if section-capacity validation is enabled.

5. Section-image violation, if section-image validation is enabled.

6. Declared-object violation, if declared-object validation is enabled.

7. Uninitialized-read warning or strict error, if uninitialized-read validation is enabled and the access is a read.

8. Non-fatal unaligned-access warning.
```

A fatal or strict diagnostic stops execution before mutation. If execution stops at an earlier fatal or strict diagnostic, later warning-only diagnostics for the same memory access do not need to be emitted.

Default rule: emit at most one primary fatal diagnostic for one failed memory access. Do not combine separate validation-level diagnostics unless the relevant phase explicitly defines a combined diagnostic shape and adds structured diagnostic tests plus exact rendered Simulator Messages tests.

`region-boundary-crossing` is a Level 1 diagnostic. It does not introduce a new validation mode. It does not make cross-region access optional, configurable, warning-only, or dependent on browser diagnostic settings.

The protected-region context for `region-boundary-crossing` is diagnostic context attached to a region-boundary failure. It is not the same thing as permission validation. The simulator must not perform a partial read or partial write in order to prove the overlap. It may report protected-region context only when existing layout metadata can prove that the requested final byte range intersects the protected region byte range.

For `.CONST`:

```text
Cross-region access intersecting `.CONST`:
  region-boundary-crossing

Wholly-contained write overlapping `.CONST`:
  permission-denied

Wholly-contained read from `.CONST`:
  allowed, unless another mandatory or enabled strict validation rejects it
```

For cross-region `.CONST` reads, the diagnostic code `region-boundary-crossing` means the access crosses independent VM memory-region boundaries. It must not be interpreted as a general `.CONST` read prohibition.

Mandatory Level 1 checks are always active:

- address arithmetic overflow;
- region containment and region-boundary crossing;
- memory permission;
- `.CONST` write overlap.

Optional educational validation levels are active only when selected by source-run/test policy or browser settings:

- section-capacity validation;
- section-image validation;
- declared-object validation;
- uninitialized-read strictness beyond the default warning policy.

Unaligned-access diagnostics remain warning-only unless a later reviewed phase explicitly changes that policy.

The diagnostic code, source line, source column, byte offset, span length, JSON fields, and rendered Simulator Messages text must remain stable enough for exact tests.

### 11.9.2A Standing Rule: Memory-Capable Features Must Participate in Both Mandatory VM Safety and Optional Educational Validation

Every simulator feature that performs a simulated memory read or simulated memory write must participate in two distinct validation layers.

#### 1. Mandatory VM memory safety

The final effective address and final byte range must be checked through the central VM memory helpers.

Mandatory VM memory safety includes, at minimum:

- address arithmetic overflow detection;
- final byte-range validation;
- containment in one readable or writable VM memory region, as appropriate;
- region permission enforcement;
- mandatory `.CONST` write protection;
- mandatory invalid-address, invalid-range, invalid-region, and permission diagnostics.

Mandatory VM memory safety is always enforced. It is not an optional teaching setting.

Mandatory `.CONST` write protection remains a Level 1 permission/read-only failure. It must not be reclassified as section-capacity, section-image, declared-object, or uninitialized-read validation.

#### 2. Optional educational validation policies

Source-run, Wasm, and browser execution must also be able to apply optional policy-level checks before a memory value is consumed or before an instruction mutates visible state.

Optional educational validation includes, where applicable:

- Level 2 section-capacity validation;
- Level 3 section-image validation;
- Level 4 declared-object validation;
- uninitialized-origin read diagnostics;
- future source-intent, provenance, taint, or bounds policies, if later implemented.

A future feature must not satisfy this rule merely by calling a checked memory read or write helper inside the executor. If a warning or strict policy is supposed to apply before the instruction consumes a memory value or mutates state, the feature must expose its planned read or planned write to the source-run/Wasm/browser policy path before committing the instruction.

This rule applies to:

- runtime instructions with explicit memory operands;
- runtime instructions with implicit memory accesses;
- Irvine32 routines that read or write simulated memory;
- stack operations;
- string or buffer operations;
- debugger actions that read or write simulated memory;
- future procedure, call-frame, or runtime-library features that touch simulated memory.

This rule does not make optional policies mandatory in default mode. Default behavior remains whatever the canonical implementation guide specifies for the relevant policy. The rule only requires that every memory-capable feature route through the same validation architecture so selected warning and strict modes behave consistently.

Strict policy failures must preserve the simulator's no-partial-mutation guarantee:

- all registers retain the values they had immediately before the failing instruction began;
- modeled flag bits retain the values they had immediately before the failing instruction began;
- Phase 50A flag-validity metadata retains the state it had immediately before the failing instruction began;
- memory is unchanged;
- Program Console output is unchanged;
- no new memory-change row is created;
- no `execution-complete` message is emitted after a fatal runtime diagnostic.

The paired implementation guide owns the exhaustive phase-level test matrix for future memory-capable phases. This specification owns the stable behavior those tests must prove: mandatory checked access, optional planned-access policy participation where applicable, structured diagnostics, rendered Simulator Messages for user-visible diagnostics, and no partial mutation on strict or fatal failures.

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


### 11.9.4 Simple Register-Displacement Whitespace Policy

The parser must treat whitespace as insignificant for the currently supported simple register-indirect plus-or-minus constant displacement forms.

This rule applies to the existing simple register-displacement subset only. It does not implement advanced addressing.

For a supported 32-bit base register and a constant displacement representable as a signed 32-bit byte offset, these forms are equivalent:

```asm
[reg32 + constant]
[reg32+constant]
[reg32 - constant]
[reg32-constant]
```

Examples that must be treated as equivalent:

```asm
DWORD PTR [eax - 4]
DWORD PTR [eax-4]

DWORD PTR [esi - 8]
DWORD PTR [esi-8]

DWORD PTR [ebp - 10h]
DWORD PTR [ebp-10h]

BYTE PTR [edi - 1]
BYTE PTR [edi-1]

WORD PTR [edx - 2]
WORD PTR [edx-2]
```

The displacement is always a byte displacement. It is not an element index.

This policy exists because the lexer may tokenize compact negative numeric text such as `-4`, `-10h`, or `-0x10` as a single signed numeric token. A parser path that accepts `[eax - 4]` must also accept `[eax-4]` when both forms represent the same simple base-minus-constant displacement.

The parser must not make valid simple displacement syntax depend on spaces around the minus sign.

This policy applies to memory operands used by executable memory-accessing instructions and to address-only instructions that reuse the same supported effective-address subset, such as `lea` after its implementation phase.

Accepted examples:

```asm
mov DWORD PTR [eax-4], 10
mov eax, DWORD PTR [ecx-4]
mov ax, WORD PTR [edx-2]
mov al, BYTE PTR [edi-1]
lea eax, [ebx-4]
```

Each compact form above is equivalent to the corresponding spaced form:

```asm
mov DWORD PTR [eax - 4], 10
mov eax, DWORD PTR [ecx - 4]
mov ax, WORD PTR [edx - 2]
mov al, BYTE PTR [edi - 1]
lea eax, [ebx - 4]
```

This rule does not enable any of the following:

- scaled-index addressing;
- base-plus-index addressing;
- SIB addressing;
- parenthesized effective-address expressions;
- general arithmetic inside memory brackets;
- register-plus-register effective addresses;
- symbolic arithmetic beyond forms already implemented by the current source-of-truth guide.

The following forms remain unsupported until explicit future phases implement them:

```asm
[eax * 4]
[eax*4]
[eax + ebx]
[eax+ebx]
[eax + ebx * 4]
[eax+ebx*4]
[eax - 4 * 2]
[eax-4*2]
[eax - (4)]
[eax-(4)]
```

A compact negative displacement must be accepted only when the signed numeric token is the complete displacement and the following token closes the memory operand.

For example, this is a valid simple displacement:

```asm
mov eax, DWORD PTR [eax-4]
```

This is not a valid simple displacement:

```asm
mov eax, DWORD PTR [eax-4*2]
```

The parser must not silently accept `[eax-4*2]` as `[eax-4]`. Extra arithmetic tokens after a compact negative displacement make the operand an unsupported advanced effective-address expression.

Parser/static diagnostics remain appropriate for malformed or unsupported address syntax. Runtime memory helpers remain authoritative for final byte-range validation, region containment, permissions, `.CONST` write protection, cross-region protected-region diagnostics, and optional memory-validation policies.

### 11.10 Uninitialized-Origin Byte Tracking and Read Diagnostics

`?` and `.DATA?` storage are deterministic zero at program load but must retain metadata that the bytes originated from uninitialized declarations.

Required model:

- Bytes emitted from explicit initializers start initialized.
- Bytes emitted from `?` or `DUP(?)` start uninitialized-origin and runtime-zero-filled.
- Every successful program write marks the written bytes initialized.
- Multi-byte writes initialize every byte in the written range.
- Multi-byte reads must check every byte read when the active uninitialized-read policy is `warn` or `strict`.
- Default user-facing educational source-run mode warns on reads from uninitialized-origin bytes after the default teaching-diagnostics phase.
- Explicit `off` mode preserves the older silent deterministic-zero behavior, and `strict` mode may stop execution before consuming any uninitialized-origin byte that has not yet been written by the simulated program.

This feature must not change the default runtime value of `?` storage. The default value remains deterministic zero.

### 11.11 Invalid Memory Access Handling and Diagnostic Precedence

Current memory-access diagnostic precedence is defined authoritatively in §11.9.2. This subsection does not define a second independent precedence order.

Future stack-specific, provenance-specific, source-intent, or advanced validation diagnostics must not reorder current memory diagnostics unless their owning implementation-guide phase explicitly updates §11.9.2 or adds a clearly marked superseding precedence subsection with tests.

Fatal diagnostics suppress lower-priority warnings unless a lower-priority warning is necessary to explain the fatal error. Successful accesses may emit non-fatal warnings such as unaligned access, provenance escape, or uninitialized-origin read according to the active validation mode, but those warnings must remain subordinate to the authoritative order in §11.9.2.

### 11.12 Post-30 Memory Layout, Validation, and Metadata Requirements

Post-30 memory work is split into explicit slices: layout policy objects, automatic deterministic sizing, stack/heap metadata, seeded or fresh randomized layout, declared-object maps, allocated-object warning mode, allocated-object strict mode, uninitialized-origin tracking, and uninitialized-read diagnostics.

The first implementation must use named configuration fields rather than hardcoded layout constants. The layout policy must include region bases or placement rules, region sizes, alignment, guard gaps, data/const/data? image sizes, stack and heap requested sizes, validation modes, and deterministic seed state.

Object and uninitialized-origin metadata must have explicit capacity behavior:

- metadata capacity exhaustion is a structured assembly/setup diagnostic;
- the simulator must not silently disable object-bound or uninitialized-origin tracking;
- memory mutation must not occur after metadata setup failure;
- JSON output must include whether a metadata feature is enabled, disabled, or failed setup.

Object-bound classification is based on the full access byte range `[address, address + width - 1]`, with overflow checks. The classifier must distinguish wholly inside object, outside all objects, partial overlap, spanning adjacent objects, padding/gap access, outside all regions, and permission failure.

Current memory-access diagnostic precedence is defined authoritatively in §11.9.2. Do not introduce a second independent memory-precedence list in this post-30 layout summary. Future validation layers must update §11.9.2 or add a clearly marked superseding subsection when they become implemented.


### 11.13 Default Teaching Diagnostics Policy

The simulator should use beginner-friendly teaching diagnostics by default while preserving MASM-compatible execution where safe.

Default teaching diagnostics are warning or notice diagnostics. They must not change the deterministic VM value read, the instruction result, the Program Console output, or the hard runtime safety rules unless their policy explicitly says `strict` or `error`.

The default policy after Phase 53C - Default Teaching Diagnostics for Existing Warning Modes, Phase 53D - Compatibility No-Op and Limited-Behavior Notices, and Phase 53E - Memory Validation and Teaching Diagnostic UI Settings is:

```text
uninitialized-read policy: warn
undefined-flag-use policy: warn
compatibility notices: on
memory range validation: region-only
section-capacity validation: off unless selected
section-image validation: off unless selected
declared-object validation: off unless selected
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

Compatibility notices are implemented current behavior after Phase 53D - Compatibility No-Op and Limited-Behavior Notices and Phase 53E - Memory Validation and Teaching Diagnostic UI Settings.

Compatibility notices are emitted through Simulator Messages as non-fatal `simulator-notice` diagnostics. They must not be emitted through Program Console.

Compatibility notices are for accepted compatibility constructs whose real MASM behavior is intentionally not performed, metadata-only, or limited in this simulator. The supported notice diagnostic codes are:

```text
compatibility-no-op
compatibility-metadata-only
compatibility-limited
```

The browser/source-run settings path may explicitly suppress compatibility notices. Suppressing compatibility notices must not change parsing, symbol resolution, execution, warnings, runtime errors, `.CONST` protection, unsupported-feature diagnostics, Program Console output, memory changes, or final register/flag state.

When compatibility notices are disabled:

- accepted no-op, metadata-only, and limited-behavior compatibility constructs still parse according to their implemented behavior;
- no `compatibility-no-op`, `compatibility-metadata-only`, or `compatibility-limited` notice diagnostics are emitted;
- real assembly errors remain errors;
- runtime errors remain errors;
- simulator warnings remain warnings;
- unsupported features and explicit non-goals remain diagnosed normally.

Constructs with active simulator semantics must not receive generic no-op notices. Examples of active semantic constructs include:

- `OPTION CASEMAP:ALL`;
- `OPTION CASEMAP:NONE`;
- `.DATA?`;
- `.CONST`;
- `.data`;
- `.code`;
- `PROC`;
- `ENDP`;
- `END`;
- `INCLUDE Irvine32.inc`;
- any implemented executable instruction;
- any implemented Irvine32 virtual intrinsic.

Compatibility notices are explanatory teaching diagnostics. They are not feature gates and must not be used to make unsupported behavior appear implemented.

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

Default user-facing source-run and browser behavior after Phase 53C, Phase 53D, and Phase 53E must use:

```text
uninitialized_read_policy = warn
undefined_flag_use_policy = warn
compatibility_notices = on
memory_range_validation = region_only
section_capacity_validation = off unless selected
section_image_validation = off unless selected
declared_object_validation = off unless selected
```

Low-level unit tests may still construct explicit policies directly. Any user-facing run path that omits a policy must use the teaching defaults.

The opt-out behavior must require an explicit policy value of `off`; missing or omitted policy fields must not silently mean `off` after the default teaching-diagnostics phase.

Tests that need old silent behavior must pass explicit `uninitialized_read_policy = off` or `undefined_flag_use_policy = off`.

This policy does not change default producer warnings for `undefined-shift-flag` or `undefined-modeled-flag`; those remain as already implemented.

### 11.14 Future Diagnostics Audit Checkpoint

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
- calls to `ExitProcess` or other Windows API routines remain unsupported in v1. A later reviewed phase may define a narrow virtual educational compatibility diagnostic or a narrow virtual terminator contract for a specific textbook pattern, but that phase must not execute Windows API behavior, load imports, model a PE process, simulate process handles, simulate DLL linkage, or imply general WinAPI support. Any such compatibility handling must be documented as simulator-owned virtual behavior, not as real `ExitProcess`, real `kernel32`, real Windows process termination, or a WinAPI simulator. Any future phase that recognizes this pattern must include tests proving that the behavior is not generalized to arbitrary WinAPI names, arbitrary imports, host include files, PE metadata, or linker behavior.

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

The central unsupported-file-routine diagnostic rule is in Section 13.6 - Post-30 Irvine32 and Macros.inc Virtual Built-In Rules.

### 13.6 Post-30 Irvine32 and Macros.inc Virtual Built-In Rules

Irvine32 routines and selected `Macros.inc` conveniences are virtual built-ins provided by the simulator. They do not load host include files, link libraries, expand the general MASM macro system, call Windows APIs, execute native code, or access the host filesystem.

`INCLUDE Irvine32.inc` enables a known virtual Irvine32 namespace. It must not be implemented as host filesystem access and must not imply real MASM include-file loading, object linking, import-library behavior, Windows API execution, native procedure calls, or PE loading.

Recognized Irvine32 names must be classified through the existing Phase 41 - Virtual Irvine32 Symbol Registry or a direct successor to that registry. Do not create a second independent Irvine32 classification table.

Each recognized Irvine32 name must map to one of the Phase 41 categories:

- supported virtual intrinsic now;
- planned Irvine32 routine later;
- known but explicitly unsupported in v1;
- Windows/API/external symbol;
- unknown symbol.

If the implementation uses internal enum names, those names must map clearly to the Phase 41 categories in comments, documentation, or tests. For example:

- an internal `implemented_virtual` status maps to "supported virtual intrinsic now";
- an internal `planned_later` status maps to "planned Irvine32 routine later";
- an internal `unsupported_v1` status maps to "known but explicitly unsupported in v1";
- an internal `windows_or_external` status maps to "Windows/API/external symbol";
- an internal `unknown` status maps to "unknown symbol".

Diagnostics for recognized-but-unimplemented Irvine32 routines must be stable and behavior-specific. They must not say:

```text
unsupported by the current milestone
not implemented in this milestone
not supported in this milestone
unsupported in this phase
```

If a precise future phase is known, the diagnostic should name both the phase number and the phase title. If no precise phase is assigned, the diagnostic should say that the routine is deferred to later Irvine32 routine phases.

Acceptable diagnostic wording examples:

```text
WriteString is a recognized Irvine32 routine, but executable Irvine32 output routines are deferred to Phase <N> - <phase title>.
```

```text
ReadString is a recognized Irvine32 routine, but Irvine32 input routines are deferred to later Irvine32 routine phases.
```

```text
Irvine32 file I/O routines are not supported because simulated programs cannot access the host filesystem.
```

Diagnostics for recognized Irvine32 routines must not imply any of these behaviors unless a later phase explicitly implements them:

- host include-file loading;
- Windows API execution;
- PE loading;
- object-file linking;
- import-library behavior;
- real procedure linkage;
- host filesystem access;
- native x86 execution.

The following implementation policies are mandatory:

- `exit`, `Exit`, and `EXIT` are accepted only through the virtual Irvine32 symbol registry and follow the documented case policy;
- instruction mnemonics, directives, virtual include names, and recognized Irvine32 routine names remain case-insensitive even under `OPTION CASEMAP:NONE`;
- user-defined symbol lookup remains controlled by the documented `OPTION CASEMAP` policy and must not be conflated with Irvine32 routine-name lookup;
- output routines validate output limits before appending when their formatted output is known up front;
- `WriteString` and `DumpMem` use validation-first behavior and append no partial output on memory failure;
- formatted output routines preserve registers and flags unless a routine contract says otherwise;
- input routines enter the shared `WAITING_FOR_INPUT` VM state and resume only through the input protocol;
- invalid numeric input resumes execution with documented flags/register results and does not implicitly re-enter wait;
- deterministic random routines use the named simulator PRNG and seed policy from the guide.

Unsupported Irvine32 file routines must produce `unsupported-irvine32-file-io` or an equivalent specific diagnostic. They must not attempt host filesystem access.

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



### 17.6 Parser, Lowering, and Source-Run Capacity Limits

The simulator must keep deterministic bounded behavior across lexing, parsing, lowering, loading, execution, diagnostics, and JSON/result formatting.

Runtime execution limits are not the only resource limits. The implementation may also impose bounded capacities before execution begins, including but not limited to:

- source text bytes;
- lexer tokens;
- parser diagnostics;
- symbol-table entries;
- data symbols;
- code labels;
- equates;
- lowered IR instructions;
- data image bytes;
- source-run JSON/result bytes;
- rendered diagnostic/message counts where applicable.

These parser/source-run capacities are distinct from the runtime instruction-count watchdog.

A parser/source-run capacity failure occurs before VM execution begins or before a result can be fully produced. It must not be reported as an instruction-limit failure unless the runtime instruction-count watchdog actually fired during execution.

Capacity failures under simulator control should produce structured diagnostics or structured infrastructure errors with stable codes. The diagnostic should identify the exhausted capacity and include source line, column, byte offset, and span length when the failing source location is known.

Examples of capacity diagnostic families include:

```text
token-capacity-exceeded
diagnostic-capacity-exceeded
source-text-capacity-exceeded
instruction-capacity-exceeded
symbol-capacity-exceeded
label-capacity-exceeded
equate-capacity-exceeded
data-capacity-exceeded
json-capacity-exceeded
```

The exact public code names are owned by the implementation and diagnostic tests. Existing stable public codes must not be renamed merely to match this list.

A parser/source-run capacity failure must preserve these user-visible guarantees where applicable:

- no Program Console output from a program that never began execution;
- no `execution-complete` message;
- no hidden partial VM execution;
- no successful memory-change rows caused by a program that did not run;
- a clear Simulator Messages entry or structured worker/source-run error;
- no browser crash or unbounded diagnostic output for capacity failures that are detected by the simulator.

The simulator is an educational small-program environment. The existence of source-run capacity limits is not a defect by itself. However, generic worker failures should be replaced with structured capacity diagnostics whenever the failure is caused by a known simulator-owned capacity.

Large-program support must be implemented through explicit roadmap phases. Future assistants must not silently remove bounded capacities, add unbounded allocation, or expand browser memory use without an owning phase, tests, and documentation.

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


### 18.4 Memory Layout, Memory Range Validation, and Teaching Diagnostic UI

The UI must treat memory layout, memory range validation, and teaching diagnostics as separate setting dimensions.

Do not collapse these settings into one generic `memory validation` enum. In particular, selecting `Region-only` memory range validation must not disable uninitialized-read warnings, undefined-flag-use warnings, compatibility notices, or any other independent teaching diagnostic.

The UI must expose a setting only after the corresponding backend behavior exists. Until a setting is implemented end-to-end through the browser UI, worker protocol, source-run/backend option mapping, tests, and documentation, it must remain absent or visibly disabled. Do not display a setting as active merely because a future roadmap phase mentions it.

Do not add browser controls, worker protocol fields, source-run settings, URL state, or tests for settings that are not already implemented or explicitly owned by the current target phase.

Required memory layout controls, once the relevant layout modes are implemented:

```text
Memory layout:
  Fixed educational layout
  Automatic deterministic layout
  Seeded randomized layout
  Fresh randomized layout
```

Memory layout controls select address placement policy only. They must not change parser behavior, instruction semantics, memory permissions, uninitialized-origin metadata, object-bound diagnostics, or teaching diagnostic policies.

Required memory range validation controls, once the relevant validation policies are implemented:

```text
Memory range validation:
  Region-only
  Section capacity: warn
  Section capacity: strict stop
  Section image: warn
  Section image: strict stop
  Declared object bounds: warn
  Declared object bounds: strict stop
```

Memory range validation controls only optional educational range checks layered above mandatory checked VM memory access.

`Region-only` means:

- mandatory VM region/range/permission checks remain enabled;
- address overflow checks remain enabled;
- `.CONST` write protection remains enabled;
- invalid simulated addresses remain runtime errors;
- object-bound diagnostics are not emitted unless a declared-object-bounds mode is selected;
- section-capacity diagnostics are not emitted unless a section-capacity mode is selected;
- section-image diagnostics are not emitted unless a section-image mode is selected.

`Region-only` does not mean "all diagnostics off." It does not disable uninitialized-read diagnostics, undefined-flag-use diagnostics, compatibility notices, unaligned-access warnings, invalid-memory diagnostics, output-limit diagnostics, or syntax diagnostics.

Required teaching diagnostic controls, once exposed in the UI:

```text
Uninitialized reads:
  Warn
  Off / I know what I am doing
  Strict stop

Undefined flag use:
  Warn
  Off / I know what I am doing
  Strict stop

Compatibility notices:
  On
  Off
```

Teaching diagnostic controls are independent of memory range validation.

Default user-facing behavior after Phase 53C - Default Teaching Diagnostics for Existing Warning Modes and Phase 53D - Compatibility No-Op and Limited-Behavior Notices is:

```text
Memory layout:
  Fixed educational layout, unless a later implemented UI phase explicitly changes the default.

Memory range validation:
  Region-only.

Uninitialized reads:
  Warn.

Undefined flag use:
  Warn.

Compatibility notices:
  On.
```

The default profile must be interpreted as:

- use mandatory region/range/permission safety checks;
- warn when a read consumes bytes that still carry uninitialized-origin metadata;
- warn when an implemented flag consumer reads a modeled flag whose deterministic value is architecturally invalid;
- show compatibility notices for accepted no-op, metadata-only, or limited-behavior MASM compatibility constructs;
- continue execution after non-fatal warnings and notices unless a later fatal diagnostic occurs.

Invalid memory access handling is not a separate user-selectable policy for mandatory VM safety failures. Mandatory invalid address, address-overflow, region, and permission failures must stop execution with a runtime diagnostic. Warn-and-continue behavior is allowed only for explicitly non-fatal warning policies.

Required preset descriptions, once presets are exposed:

```text
Beginner/default:
  fixed educational layout
  region-only memory range validation
  uninitialized-read warnings
  undefined-flag-use warnings
  compatibility notices on
  fatal invalid region/permission errors

Debug:
  fixed or automatic deterministic layout
  declared-object-bounds warnings
  uninitialized-read warnings
  undefined-flag-use warnings
  compatibility notices on

Robustness:
  seeded randomized layout
  declared-object-bounds strict stop
  section-image or section-capacity warnings where implemented
  uninitialized-read warnings or strict stop according to the selected teaching profile
  undefined-flag-use warnings or strict stop according to the selected teaching profile
```

Seed requirements:

- Seeded randomized mode must display the active seed.
- Shared URLs must include the seed if deterministic reproduction is expected and share URLs have been implemented for simulator settings.
- Fresh randomized mode must display the generated seed after each run so a failing run can be reproduced.
- Tests must use fixed or explicitly seeded layout. They must not depend on fresh random mode.

Future provenance validation must be introduced as an explicitly named policy. Do not silently merge future provenance checks into declared-object-bounds validation, section-image validation, or uninitialized-read diagnostics.

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


## 20A. Simulator State, Diagnostics, and Memory Image Policy

This section defines stable simulator policy for startup state, configurable teaching diagnostics, final-state display markers, `.CONST` uninitialized storage, and `.code` memory-image behavior.

The purpose is to keep MASM32 Educational Mode explicit about the difference between:

- deterministic simulator behavior;
- real MASM/Windows behavior;
- configurable teaching diagnostics;
- future compatibility work;
- explicit non-goals such as native x86 execution, PE loading, object linking, host filesystem access, and Windows API execution.

### 20A.1 Deterministic Startup State

The simulator must remain deterministic by default.

Default startup behavior:

- modeled registers, including EIP, start at zero;
- modeled flag bits start cleared;
- declared memory bytes are deterministic: initialized declarations use their encoded initializer bytes, and uninitialized storage is zero-filled for visible byte values;
- `.DATA?`, `?`, and `DUP(?)` storage preserve uninitialized-origin metadata even when their visible byte value is deterministic zero;
- repeated runs with the same source, settings, and input produce the same result.

The simulator must not imply that deterministic startup state is the same as arbitrary real-machine process state. Real MASM programs must not rely on arbitrary register or flag startup values unless the operating environment or calling convention explicitly defines those values.

Randomized startup modes are current behavior only when an implementation-guide phase explicitly implements the specific startup category. Phase 57F - Seeded Random Register and Flag Startup Mode implements opt-in seeded startup for general-purpose registers and modeled flags. Phase 57G - Seeded Random Uninitialized Storage Mode implements opt-in seeded visible-byte initialization for bytes that still carry uninitialized-origin metadata. All randomized startup modes must be deterministic pseudo-random modes selected by settings. They must be seedable and reproducible. The same source, same settings, same seed, and same input must produce the same result.

Randomized startup modes must not use host CPU registers, host process memory, unseeded wall-clock randomness, browser-global nondeterminism, or operating-system process state as simulator state.

### 20A.2 Uninitialized-Origin Metadata Is Separate from Visible Bytes

The simulator must distinguish the visible byte value from uninitialized-origin metadata.

A byte may have a deterministic visible value and still be marked as originating from uninitialized storage.

This distinction applies to supported uninitialized storage forms such as:

```asm
.DATA?
x DWORD ?

.data
y DWORD ?

.data
buf BYTE 16 DUP(?)
```

When `.CONST` uninitialized storage compatibility is implemented, the same distinction also applies to `.CONST ?` and `.CONST DUP(?)`.

Uninitialized-origin diagnostics must be controlled by the relevant diagnostic policy. They must not weaken mandatory memory safety checks.

### 20A.3 Modular Diagnostic Policy Model

Teaching diagnostics and compatibility notices should use a modular diagnostic policy model.

Common policy states:

```text
off
warn
error
```

Meanings:

- `off`: do not emit that optional teaching diagnostic or notice.
- `warn`: emit a non-fatal Simulator Message and continue when no lower-level fatal error occurs.
- `error`: emit a fatal assembly or runtime diagnostic, depending on the diagnostic family, and stop before the affected operation mutates visible state when the diagnostic is runtime-facing.

A diagnostic policy must not weaken mandatory safety checks.

Mandatory errors that must not be converted to warnings merely by teaching-policy settings include:

- invalid address;
- invalid byte range;
- address overflow;
- invalid region;
- permission failure;
- mandatory `.CONST` write protection;
- parser capacity exhaustion;
- internal invariant failure;
- unsupported simulator non-goals such as host include loading, PE loading, object linking, native x86 execution, or Windows API execution.

Diagnostic families that should use the modular policy model include, when implemented:

```text
uninitialized-read
undefined-flag-use
compatibility-notice
const-uninitialized-storage
startup-state-notice
unsupported-code-memory-access
```

Future diagnostic families should be added through the same policy mechanism rather than one-off parser flags, source-run flags, environment variables, or UI-only checks.

Phase 57C introduces the behavior-preserving C99 registry skeleton for this model. Phase 57D routes existing configurable families through the registry where practical while preserving legacy public setting names through compatibility adapters. Phase 57E activates startup-state notices as a non-fatal notice family while preserving deterministic startup values. Reserved families in the registry are names only; they do not make their future diagnostics active behavior.

User-visible diagnostics controlled by this model must still satisfy the project diagnostic rules:

- structured diagnostic code;
- severity/kind;
- source line when applicable;
- source column when applicable;
- byte offset when applicable;
- span length when applicable;
- exact rendered Simulator Messages tests for new or changed wording;
- no Program Console output for simulator diagnostics;
- no milestone-relative wording such as "unsupported by the current milestone."

### 20A.4 Final Register Display and Unchanged Markers

The final register display may show which canonical register families were left unmodified by the executed program.

A register family is the canonical 32-bit register plus its displayed aliases.

Examples:

```text
EAX family: EAX, AX, AH, AL
EBX family: EBX, BX, BH, BL
ECX family: ECX, CX, CH, CL
EDX family: EDX, DX, DH, DL
ESI family: ESI, SI
EDI family: EDI, DI
EBP family: EBP, BP
ESP family: ESP, SP
EIP family: EIP
EFLAGS family: EFLAGS
```

When a canonical register family is unchanged by the executed program, the display may append a compact marker to the canonical parent row only:

```text
EAX    | 00000000h / u: 0 / s: 0        [unchanged]
  AX   |     0000h / u: 0 / s: 0
    AH |       00h / u: 0 / s: 0
    AL |       00h / u: 0 / s: 0
```

Rules:

- The marker appears only on the canonical parent row.
- The marker is not repeated on alias rows.
- Alias rows inherit the parent-family status visually.
- If any alias or subpart of a canonical register changes, the parent family is considered changed.
- If the display cannot preserve alignment in a narrow viewport, the formatter may omit the marker rather than wrapping rows or breaking numeric alignment.
- The marker must not alter VM state, source-run numeric values, Program Console output, Simulator Messages diagnostics, or memory-change rows.

This is display-only metadata. It is not a new CPU semantic feature.

### 20A.5 `.CONST` Uninitialized Storage

The simulator may accept MASM-compatible uninitialized storage forms in `.CONST` when the implementation guide phase for this compatibility feature is completed.

Phase 57I - .CONST Uninitialized Storage Acceptance is implemented and accepted. `.CONST ?` and `.CONST DUP(?)` are accepted compatibility forms. Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy is implemented and adds configurable declaration diagnostics for these forms.

Supported forms include, at minimum:

```asm
.CONST
x DWORD ?
buf BYTE 16 DUP(?)
```

Semantics:

- storage is allocated in `.CONST`;
- bytes are deterministic by default;
- bytes carry uninitialized-origin metadata until overwritten, although normal simulated program writes to `.CONST` remain blocked;
- the storage remains read-only for simulated program writes;
- direct and computed writes to the final byte range remain blocked by mandatory `.CONST` protection;
- reads from the storage may trigger uninitialized-read diagnostics according to the active uninitialized-read policy.

Because `.CONST ?` is suspicious in educational code, Phase 57J provides this configurable diagnostic family:

```text
const-uninitialized-storage
```

Default policy:

```text
warn
```

The warning explains that the simulator accepts the declaration for compatibility, initializes bytes deterministically, and preserves uninitialized-origin metadata. The warning appears in Simulator Messages, not Program Console. The `off` policy suppresses only this declaration diagnostic. The `error` policy reports an assembly error and refuses execution before runtime. This declaration policy is separate from read-time `uninitialized-read` diagnostics.

### 20A.6 `.CODE` Memory Access Policy

This section defines the implemented v1 `.code` memory-access policy after Phase 57L - .CODE Memory Access Diagnostics. Phase 57K - .CODE and MASM Segment Symbol Access Policy owns the policy audit and source-of-truth cleanup for `.code` access behavior. Phase 57L owns and implements runtime/source-run `.code` read/write denial diagnostics.

Phase 57L implementation result for the current VM memory layer and source-run path:

- fixed-layout `.code` base: `00400000h`;
- fixed-layout `.code` capacity: `00100000h` bytes;
- low-level permissions: read and execute are present, write is absent;
- checked source-level memory reads wholly inside `.code` fail with `unsupported-code-memory-access`;
- checked source-level memory writes wholly inside `.code` fail with `unsupported-code-memory-access`;
- cross-region memory reads or writes whose final byte range intersects `.code` fail with `region-boundary-crossing` and `.code` protected-region context;
- diagnostics use the active runtime `.code` region base rather than hardcoded fixed-layout addresses;
- `.code` backing bytes are not a section image, not PE `.text` bytes, not x86 opcode bytes, not emitted IR bytes, and not a supported source-level contract;
- source programs can syntactically produce `.code` addresses through existing register-indirect or displacement memory forms after loading a `.code` address into a register, but those memory accesses fail before reading a value or committing mutation.

The simulator has a `.code` source section and may have parser, IR, source-location, and execution metadata associated with executable source lines. That does not mean `.code` is user-readable or user-writable simulated program memory.

In MASM32 Educational Mode, `.code` is an internal source/IR execution area, not a modeled PE `.text` byte image and not a region of user-addressable opcode bytes.

Implemented v1 policy after Phase 57L:

```text
unsupported-code-memory-access
```

This is the only planned v1 `.code` memory-access policy.

Do not offer, document, or implement a deterministic simulator code-image policy in v1 unless a later reviewed spec/guide revision deliberately changes this section.

All simulated source-level memory accesses whose final byte range overlaps `.code` must fail with a structured diagnostic.

This includes:

- reads wholly inside `.code`;
- writes wholly inside `.code`;
- reads that partially overlap `.code`;
- writes that partially overlap `.code`;
- direct absolute-address forms, if supported;
- register-indirect forms;
- displacement forms;
- symbol-derived forms, if any future feature can produce them;
- computed addresses that happen to land in `.code`.

The diagnostic must explain that `.CODE/_TEXT` is not exposed by MASM32 Educational Mode as an accessible memory region. The `.CODE/_TEXT` wording is diagnostic copy only; `_TEXT` remains an unsupported MASM/object segment symbol and is not an addressable alias for the simulator's internal `.code` region.

The diagnostic must not say or imply that real MASM/PE programs have empty `.text` sections. The issue is simulator scope: this project executes internal IR and does not expose real x86 instruction bytes.

Required behavior for fatal `.code` memory-access diagnostics:

- stop before the instruction consumes a read value;
- stop before the instruction commits a write;
- preserve all registers;
- preserve modeled flags;
- preserve flag-validity metadata;
- preserve memory;
- preserve Program Console output;
- create no successful memory-change row;
- emit no `execution-complete` message after the fatal diagnostic.

Diagnostic precedence for `.code` memory-access work must preserve the mandatory Level 1 memory rules from Section 11.9.2 and the protected-region rule introduced by Phase 57-CORR1.

Mandatory lower-level memory failures still take precedence when the simulator cannot classify the access as overlapping a known protected region. Examples include:

- malformed memory operands;
- ambiguous memory width;
- unsupported executable QWORD/SQWORD memory width;
- address arithmetic overflow;
- final byte ranges plainly outside all VM regions without intersecting a known protected region.

The Phase 57-CORR1 `region-boundary-crossing` rule is also mandatory Level 1 diagnostic behavior. It is not an optional teaching diagnostic.

When a single source-level memory access is not wholly contained in one VM memory region and the active runtime layout metadata proves that the requested final inclusive byte range intersects a known protected region, the diagnostic must be:

```text
region-boundary-crossing
```

For current `.CONST` behavior:

- a cross-region read or write whose requested range intersects `.CONST` reports `region-boundary-crossing`;
- a direct or wholly-contained write overlapping `.CONST` reports `permission-denied`;
- a wholly-contained read from `.CONST` is allowed unless another mandatory or enabled strict validation rejects it;
- a cross-region read intersecting `.CONST` must not be described as a general `.CONST` read prohibition.

For current Phase 57L `.code` memory-access-denial behavior:

- a wholly-contained read or write overlapping `.code` reports the `.code` memory-access diagnostic selected by Phase 57L;
- a cross-region access that intersects `.code` must use `region-boundary-crossing` if runtime layout metadata can identify `.code` as the protected region involved;
- the rendered message must use the runtime `.code` base address from active layout metadata, not a hardcoded fixed-layout address, and should identify the no-access `.CODE/_TEXT` region without making `_TEXT` an addressable symbol alias.

The simulator must not split, stitch, partially perform, or partially diagnose one source-level memory access across independent VM memory regions.

Do not implement any of the following merely to support `.code` memory access:

- real x86 opcode emission;
- real multi-byte instruction encoding;
- PE `.text` section layout;
- object-file generation;
- linking;
- relocation records;
- import tables;
- disassembly;
- raw byte emission into `.code`;
- host loader behavior;
- native execution.

Future change rule:

```text
Do not revisit `.code` memory readability or writability unless a later reviewed specification and guide revision deliberately replaces this policy.
```

### 20A.7 MASM Segment and Group Symbol Policy

MASM and object/linker workflows may expose or use segment and group names such as:

```text
_TEXT
_DATA
_BSS
CONST
STACK
DGROUP
FLAT
```

In MASM32 Educational Mode, these names are not user-addressable simulator symbols.

They must not become aliases for internal simulator regions.

They must not be accepted as a way to read `.code`, `.data`, `.DATA?`, `.CONST`, stack, heap, or any other internal VM region.

They must not be accepted as object/linker metadata that implies PE, COFF, OMF, import-library, relocation, or linker behavior.

Examples that must be rejected with targeted diagnostics when recognized:

```asm
mov eax, OFFSET _TEXT
mov eax, OFFSET _DATA
mov eax, OFFSET _BSS
mov eax, OFFSET CONST
mov eax, OFFSET STACK
mov eax, OFFSET DGROUP
mov eax, OFFSET FLAT

mov eax, DWORD PTR [_TEXT]
mov eax, DWORD PTR [_DATA]
mov eax, DWORD PTR [_BSS]

_TEXT SEGMENT
_TEXT ENDS
_DATA SEGMENT
_DATA ENDS
```

Preferred diagnostic family:

```text
unsupported-segment-symbol
```

Phase 57M - MASM Segment and Group Symbol Diagnostics implements the parser/source-run diagnostic for this policy. Current implementations must report `unsupported-segment-symbol` for recognized segment/group names used as addressable symbols or segment/group definitions, without creating aliases for simulator internal regions.

The diagnostic should explain that the name is a MASM/object/linker segment or group concept and is not exposed as an addressable simulator symbol.

Example diagnostic wording for `_TEXT`:

```text
`_TEXT` is a MASM/object segment symbol. MASM32 Educational Mode does not expose linker segment symbols or readable `.code` / section images.
```

Example diagnostic wording for `_DATA`:

```text
`_DATA` is a MASM/object data-segment symbol. Use declared data labels instead; MASM32 Educational Mode does not expose linker segment symbols.
```

Example diagnostic wording for `DGROUP`:

```text
`DGROUP` is a MASM memory-model group concept. MASM32 Educational Mode uses simulator-defined flat memory regions and does not expose linker groups as addressable symbols.
```

This policy must not block ordinary user-defined symbols merely because their spelling resembles a segment name under `OPTION CASEMAP:NONE`, unless the spelling exactly matches a reserved unsupported segment/group name under the active user-symbol lookup policy.

Ordinary data labels remain supported:

```asm
.data
value DWORD 1

.code
main PROC
    mov eax, OFFSET value
main ENDP
END main
```

The simulator should recommend declared labels instead of segment/group names when the user appears to be trying to access data.

### 20A.8 Source-Level `NOP` Policy

The simulator may support source-level `nop` instruction forms as internal IR no-op instructions.

`NOP` support is split into two conceptual levels:

1. **Zero-operand `nop`**.
2. **NOP encoding-operand forms**, such as `nop eax` and `nop DWORD PTR [eax]`.

The zero-operand form may be implemented first. Encoding-operand forms may be implemented later.

Supporting source-level `nop` means:

- the parser recognizes supported `nop` source forms;
- the IR may contain a no-op instruction;
- the executor steps over that IR instruction;
- the instruction performs no simulated register mutation;
- the instruction performs no modeled flag mutation;
- the instruction performs no flag-validity metadata mutation;
- the instruction performs no memory read;
- the instruction performs no memory write;
- the instruction emits no Program Console output;
- the instruction creates no memory-change row;
- the instruction continues execution normally;
- the instruction counts as one executed instruction for execution-limit and future stepping purposes.

NOP encoding-operand forms may use either register operands or explicit-width
memory-looking operands:

```asm
nop eax
nop ax
nop WORD PTR [eax]
nop SWORD PTR [eax]
nop DWORD PTR [eax]
nop SDWORD PTR [eax]
```

Phase 57O accepts MASM-compatible 16-bit and 32-bit NOP encoding operands:
16-bit and 32-bit register operands, plus explicit `WORD PTR`, `SWORD PTR`, `DWORD PTR`, and `SDWORD PTR`
memory-looking operands where the address syntax is already supported by the
current parser. It does not accept 8-bit, signed-byte, 64-bit, or untyped
memory-looking NOP operands. In particular, `nop BYTE PTR [eax]`,
`nop SBYTE PTR [eax]`, `nop QWORD PTR [eax]`, and `nop [eax]` are rejected.
This matches real MASM behavior observed during Phase 57O user triage:
`nop eax` and `nop DWORD PTR [eax]` assemble, while `nop [eax]` requires an
operand size and byte/signed-byte/QWORD/SQWORD forms report invalid operand size.

When accepted, such operands are **encoding operands only**. They are parsed
only to recognize the source-level no-op form. Accepted register-form NOP
operands are not executable register reads or writes. Accepted memory-looking
NOP operands are not executable memory operands.

For accepted `nop` encoding operands:

- do not read the value of accepted register operands;
- do not write accepted register operands;
- do not evaluate the final effective address at runtime;
- do not call checked memory read helpers;
- do not call checked memory write helpers;
- do not perform planned-read validation;
- do not perform planned-write validation;
- do not emit uninitialized-read diagnostics;
- do not emit declared-object diagnostics;
- do not emit section-capacity diagnostics;
- do not emit section-image diagnostics;
- do not emit `.CONST` permission diagnostics;
- do not create memory-change rows.

This policy is specific to accepted `nop` encoding operands. It must not be generalized to ordinary memory operands of other instructions.

Supporting source-level `nop` must not imply:

- real x86 opcode emission;
- real multi-byte NOP byte generation;
- `.code` byte-image generation;
- PE `.text` layout;
- object-file generation;
- linker behavior;
- relocation behavior;
- disassembly;
- alignment directive behavior;
- native execution.

If a later phase implements real `.code` byte-image behavior, it must explicitly define whether source-level `nop` forms contribute bytes to that image. Until such a phase exists, `nop` remains an IR-level no-op only.

### 20A.9 Host Include, Library, and MASM32 Path Diagnostics

The simulator must distinguish virtual compatibility includes from host filesystem includes and linker/library directives.

Virtual includes are simulator-defined names, such as `INCLUDE Irvine32.inc` where implemented. They do not read the host filesystem.

Host include paths are outside the browser simulator boundary. Examples include:

```asm
include \masm32\include\masm32.inc
include C:\masm32\include\kernel32.inc
include ..\include\file.inc
include .\local.inc
```

The simulator must not attempt to load those paths from the user's machine, browser sandbox, server, project repository, or host filesystem.

`INCLUDELIB` is outside the simulator boundary because it implies object/library linking. Examples include:

```asm
includelib \masm32\lib\masm32.lib
includelib \masm32\lib\kernel32.lib
includelib kernel32.lib
```

The simulator must not treat `INCLUDELIB` as a future promise of real linker behavior unless the project deliberately changes scope.

Diagnostics for these constructs should be specific and educational. They should explain:

- what construct was recognized;
- why it is outside the current simulator boundary;
- whether a virtual include exists for a supported subset;
- that host filesystem access, object linking, PE loading, and WinAPI execution are not performed.

These diagnostics must be emitted through Simulator Messages, not Program Console.

The lexer must not report repeated low-level `unexpected-character` diagnostics for path separators when a whole unsupported include or library directive can be recognized and diagnosed as a higher-level unsupported construct.

### 20A.10 Unsupported MASM32 Invocation and Flow Diagnostics

The simulator should recognize common MASM32 source constructs well enough to produce useful unsupported-feature diagnostics, even when those constructs are not executable yet.

Common unsupported or future-owned constructs include:

```asm
invoke StdOut, addr titleMsg
invoke crt_printf, addr numberFmt, counter
invoke ExitProcess, 0

.IF eax == 0
.ELSE
.ENDIF
```

Recognition does not imply implementation.

The simulator should classify these constructs using stable diagnostic categories:

- `INVOKE` and invocation-style calls;
- `ADDR` operands;
- known MASM32 library routines;
- known C runtime-style routines such as `crt_printf`;
- WinAPI/external routines such as `ExitProcess`;
- high-level MASM flow such as `.IF`, `.ELSE`, `.ENDIF`, `.WHILE`, `.ENDW`, `.REPEAT`, and `.UNTIL`.

Diagnostics should state whether the construct is:

- planned for a later phase;
- unsupported in v1;
- outside the simulator boundary;
- unavailable because linker, WinAPI, PE, stack, procedure, or macro behavior is not implemented.

These diagnostics must not implement the construct. They must not lower high-level flow into labels or branches. They must not execute `INVOKE`, call external routines, or simulate WinAPI.

### 20A.11 Realistic Playground Program Recovery Policy

Users and AI tools often produce normal MASM32/Windows examples that combine many unsupported constructs in one file.

The simulator should recover far enough to produce useful unsupported-feature diagnostics for common MASM32 playground programs without pretending to execute them.

A realistic unsupported program should produce a concise set of structured diagnostics identifying unsupported feature families, such as:

- host include paths;
- `INCLUDELIB`;
- `INVOKE`;
- `ADDR`;
- WinAPI calls such as `ExitProcess`;
- MASM32 library or C runtime routines such as `StdOut` or `crt_printf`;
- high-level MASM flow such as `.IF`, `.ELSE`, and `.ENDIF`;
- `call` and `ret` before procedure execution is implemented;
- `cmp` and conditional jumps before control flow is implemented.

The simulator should avoid flooding the user with repeated character-level diagnostics when a more meaningful unsupported-feature diagnostic is possible.

This policy does not require the simulator to implement those features. It requires the simulator to classify and explain them.

Phase 57T - Playground Program Diagnostic-Recovery Smoke Fixtures verifies this policy through source-run and rendered Simulator Messages regression fixtures. That phase is fixture/documentation coverage unless a narrow diagnostic-recovery defect is discovered and corrected deliberately.

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

The implementation guide assigns diagnostic recovery for known unsupported constructs to dedicated phases. When those phases are implemented and accepted, recovery must be conservative:

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


### 21.3 Test Runner Decomposition, Fixture Size, and Timeout-Safe Verification Policy

This section owns the stable project testing policy. The implementation guide owns phase-specific required tests, focused runner group names, milestone-report acceptance criteria, and any phase-local verification checklist.

The project test suite must remain runnable in both local developer environments and constrained assistant/container environments.

The full aggregate test command remains required. However, as the simulator grows, the aggregate command may become too long or too verbose for hosted assistant tool environments. A hosted assistant/container timeout is not automatically a project test failure.

The project must distinguish these cases clearly:

- **Project test failure:** a test command completes and reports a failing assertion, nonzero exit code, build error, parser/executor mismatch, rendered-message mismatch, malformed JSON output, unsupported syntax mismatch, or another real failure.
- **Project hang:** a local or focused test command repeatedly stops making progress at the same fixture, same source-run program, same diagnostic-rendering fixture, or same phase.
- **Assistant/container timeout:** a hosted execution environment stops a long command because of wall-time, output-size, process-management, or tool-session limits, without evidence that the underlying project command failed.
- **Unavailable dependency:** a required optional tool, such as Emscripten, is unavailable in the current environment.

Assistant/container timeouts must be reported as verification-environment limitations unless a focused rerun proves an actual project failure.

The test infrastructure must support decomposition at three levels:

1. **Runner group level:** the full test suite can be split into focused runner groups such as native tests, source-run tests, web/Node tests, rendered diagnostic tests, protocol tests, and static checks.
2. **Test file level:** unusually large test files can be split by behavior family when that improves maintainability or focused execution.
3. **Individual fixture/program level:** unusually large MASM source fixtures or test programs can be split, table-driven, moved to named external fixture files, or explicitly labeled as integration smoke fixtures.

These decomposition levels exist to improve verification reliability. They must not weaken coverage.

The project must preserve exact rendered Simulator Messages coverage for user-visible diagnostics. Test-runner decomposition must not remove exact diagnostic-rendering tests, replace exact rendered-message assertions with weaker substring checks, stop using the native diagnostic JSON producer, or stop using the real browser formatter module from Node tests.

Individual MASM source fixtures should normally test one behavior family or one regression. A large "kitchen sink" source program is allowed only when it is deliberately labeled as an integration smoke fixture and is not the only coverage for the behavior it exercises.

Preferred source fixture categories:

```text
focused success fixture
focused error fixture
focused warning/notice fixture
edge-case fixture
regression fixture
integration smoke fixture
```

The preferred verification model is:

1. Run the aggregate command locally or in CI when the environment permits.
2. If the aggregate command times out or output is truncated in a hosted assistant/container environment, rerun focused groups individually.
3. If a focused group is still too large, rerun its documented subgroups or specific fixture families.
4. Report exactly which groups, subgroups, files, or fixtures passed, failed, or were skipped.
5. Report whether the aggregate command completed in the available environment.
6. Report unavailable dependencies, such as missing `emcc`, separately from test failures.

Milestone reports must use precise wording for test verification status. Acceptable examples:

```text
Aggregate test command completed and passed.
```

```text
Aggregate test command timed out in the assistant/container environment while producing long output. Focused groups were rerun individually and passed: structure, native, source-run, web, diagnostics, protocol, static.
```

```text
The source-run group timed out in the assistant/container environment. Source-run subgroups were rerun individually and passed: memory-layout, instructions, diagnostic-policies, settings, regressions.
```

```text
Browser/Wasm rebuild smoke was not run in this environment because `emcc` was unavailable. Native, source-run, Node, protocol, static, and diagnostic-rendering tests passed.
```

```text
Focused group `diagnostics` failed. This is a real project test failure, not an assistant timeout.
```

Milestone reports must not describe a hosted assistant/container timeout as a project failure unless a focused command, subgroup command, fixture command, or local run reproduces a real failing test.

Milestone reports must not describe a partial focused rerun as full aggregate verification unless the aggregate command actually completed in that environment.

The default aggregate runner output should be compact enough to review. Verbose fixture-level output should remain available through an explicit verbose mode and should always be shown, or summarized with enough context, when a failure occurs.

Release-gate implication:

The final v1 release gate must be runnable through both the aggregate command and documented focused groups. Focused groups are not lesser tests; they are the decomposed form of the same verification obligations.

A release report may use focused group results as evidence only when it lists every required group and every required group passed or was explicitly skipped for a documented environment reason.

A release report must not treat `--quick` as full release verification.

A release report must not treat unavailable Emscripten/browser smoke as a native/core test failure. It must report that limitation separately and must still run all native, source-run, Node, protocol, static, and diagnostic-rendering groups that are available in the environment.

A release report must also identify any intentionally preserved large integration fixtures. Large integration fixtures are allowed only when they are labeled, purposeful, and supported by smaller focused tests for the same behavior.

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

Diagnostic UI settings persistence policy:

Phase 53E - Memory Validation and Teaching Diagnostic UI Settings introduced browser controls for memory range validation, uninitialized-read diagnostics, undefined-flag-use diagnostics, and compatibility notices. In the Phase 53E implementation, these diagnostic UI settings are local page-session preferences. They are not currently encoded in share URLs.

Until a later share/settings phase explicitly changes this policy, these settings must be treated as local-only:

- memory range validation selection;
- uninitialized-read diagnostic policy;
- undefined-flag-use diagnostic policy;
- compatibility-notice visibility;
- collapsed or expanded state of the Diagnostic settings panel;
- other diagnostic-panel presentation preferences.

Diagnostic settings may affect whether teaching diagnostics are emitted as notices, warnings, or strict stops. They must not be described as changing MASM language semantics or adding/removing supported syntax.

A future share URL or settings-persistence phase may deliberately promote selected diagnostic settings into share-safe project state, but it must do so explicitly. That future phase must state:

1. which diagnostic settings are share-safe project semantics;
2. which diagnostic settings remain local-only teaching or UI preferences;
3. how omitted fields are interpreted during import;
4. how imported settings interact with default teaching diagnostics;
5. how stale or unknown setting values are rejected or downgraded;
6. which structured diagnostics or UI errors are emitted for invalid imported settings;
7. which parser/source-run/browser tests prove the persistence boundary.

Do not silently include or silently exclude Phase 53E diagnostic settings in share URLs. The share/import behavior must be deliberate, documented, and tested.

This policy does not change the runtime default diagnostic behavior. After Phase 53C and Phase 53D, default user-facing runs still warn for uninitialized reads, warn for undefined flag use, and show compatibility notices unless the user explicitly selects another local setting.

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
