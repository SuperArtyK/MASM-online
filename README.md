# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 57T - Playground Program Diagnostic-Recovery Smoke Fixtures

Runtime/source-run MASM behavior phase:
Phase 57S - Unsupported High-Level Flow Diagnostics

Phase 57T adds realistic MASM32 playground-program diagnostic-recovery smoke fixtures and documentation. It verifies that common AI/tutorial-style MASM32 samples containing host include paths, `INCLUDELIB`, `INVOKE`, `ADDR`, WinAPI/external routine names, high-level MASM flow, `CALL`, `RET`, `CMP`, and conditional jumps produce concise Simulator Messages diagnostics instead of low-level lexer/parser noise. Phase 57T does not implement any of those unsupported features, and runtime/source-run MASM behavior metadata remains Phase 57S.

Phase 57S adds clear parser/source-run diagnostics for unsupported high-level MASM flow constructs. `.IF`, `.ELSE`, `.ENDIF`, `.ELSEIF`, `.WHILE`, `.ENDW`, `.REPEAT`, `.UNTIL`, `.UNTILCXZ`, `.BREAK`, and `.CONTINUE` are recognized as unsupported high-level flow where encountered. They report `unsupported-high-level-if`, `unsupported-high-level-else`, `unsupported-high-level-endif`, `unsupported-high-level-while`, `unsupported-high-level-repeat`, or `unsupported-high-level-flow`; source containing these diagnostics refuses execution, emits no `execution-complete`, and does not write to Program Console. Phase 57S does not implement high-level-flow lowering, labels, branch execution, expression parsing for conditions, loop semantics, or block execution. Phase 57R `INVOKE`/`ADDR`/external-routine diagnostics, Phase 57Q `INCLUDELIB` diagnostics, Phase 57P host/path-like `INCLUDE` diagnostics, Phase 57O NOP encoding-operand behavior, Phase 57M segment/group-symbol diagnostics, Phase 57L `.code` memory-access diagnostics, Phase 57J `.CONST ?` / `.CONST DUP(?)` declaration diagnostics, Phase 57H final-register `[unchanged]` display markers, Phase 57G seeded uninitialized-storage visible-byte settings, and Phase 57F seeded register/flag startup remain available.

Preserved diagnostic/code-policy highlights: Phase 57P host/path-like `INCLUDE` diagnostics remain available with `unsupported-host-include-path`, `unsupported-windows-api-include`, and `unsupported-masm32-library-include`; Phase 57R invocation diagnostics remain available with `unsupported-invoke`, `unsupported-addr`, `unsupported-masm32-runtime-routine`, `unsupported-crt-routine`, and `unsupported-winapi-execution`; Phase 57Q library diagnostics remain available with `unsupported-includelib`, `unsupported-windows-api-library`, and `unsupported-masm32-library`. Zero-operand `nop` remains accepted. Phase 57O - Explicit-Width NOP Encoding-Operand Forms keeps NOP encoding-operand behavior available. Phase 57M segment/group diagnostics include `unsupported-segment-symbol` for names such as `_TEXT` and `DGROUP`; Phase 57L `.code` diagnostics include `unsupported-code-memory-access`; Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy remains current for `.CONST ?` / `.CONST DUP(?)` declaration diagnostics.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). In broad terms, that includes the C99 core VM/parser/executor, checked simulated memory, `.data`, `.DATA?`, `.CONST`, selected MASM compatibility directives, selected register and memory operands, selected arithmetic/logic/shift/rotate/multiply/divide instructions through Phase 57 - Signed IDIV, diagnostic settings, the default `startup-state-notice`, opt-in seeded register/flag startup and opt-in seeded uninitialized-storage visible bytes for tests/configuration, final-register unchanged display markers, Phase 57S high-level-flow diagnostics, Phase 57R INVOKE/ADDR/external-routine diagnostics, and Phase 57Q INCLUDELIB diagnostics, Phase 57P host include path diagnostics, Phase 57O NOP register and explicit-width memory-looking encoding operands, accepted read-only `.CONST ?` / `.CONST DUP(?)` storage with configurable declaration diagnostics, Phase 57M segment/group-symbol diagnostics, Phase 57L `.code` memory-access diagnostics, and the virtual Irvine32 `exit` terminator.

For exact accepted forms, rejected forms, diagnostics, and runtime-phase status, use [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md) instead of this README.

## Quick start

Serve the browser app over local HTTP:

```sh
python3 -m http.server 8000 --directory web
```

Then open:

```text
http://localhost:8000
```

Run aggregate verification:

```sh
python3 scripts/run_tests.py --all
```

Build WebAssembly artifacts when Emscripten `emcc` is available:

```sh
./scripts/build_wasm.sh
```

For detailed setup, Windows command files, Visual Studio notes, missing-`emcc` troubleshooting, focused test groups, and browser/Wasm smoke guidance, see [`docs/BUILDING_AND_DEVELOPMENT.md`](docs/BUILDING_AND_DEVELOPMENT.md).

## Documentation

- [`docs/FULL_IMPLEMENTATION_SPEC.md`](docs/FULL_IMPLEMENTATION_SPEC.md) - product boundaries, stable behavior, cross-cutting rules, and current/future/non-goal distinctions.
- [`docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`](docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md) - phase numbering, phase tasks, required tests, and acceptance criteria.
- [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md) - current accepted MASM32 Educational Mode subset and diagnostics.
- [`docs/TESTING_GUIDE.md`](docs/TESTING_GUIDE.md) - aggregate and focused test guidance.
- [`docs/MILESTONE_HISTORY.md`](docs/MILESTONE_HISTORY.md) - milestone history moved out of the README and kept as historical evidence.
- [`docs/BUILDING_AND_DEVELOPMENT.md`](docs/BUILDING_AND_DEVELOPMENT.md) - local serving, build commands, prerequisites, Visual Studio notes, and development workflow.

## Project boundaries

This project is:

- a static browser application;
- a C99 MASM-like parser plus internal VM compiled to WebAssembly;
- an educational MASM32/Irvine32-style console simulator.

This project is not:

- a full MASM compiler;
- a full x86 emulator;
- a Windows emulator;
- a PE loader or linker;
- a WinAPI simulator;
- a host include loader or full macro assembler.

Keep simulator execution in the Web Worker, keep core modules in C99 `.c` and `.h` files, and use the canonical specification and implementation guide for implementation decisions.
