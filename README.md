# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 57R - Unsupported INVOKE, ADDR, and External Routine Diagnostics

Runtime/source-run MASM behavior phase:
Phase 57R - Unsupported INVOKE, ADDR, and External Routine Diagnostics

Phase 57R diagnostic codes include `unsupported-invoke`, `unsupported-addr`, `unsupported-masm32-runtime-routine`, `unsupported-crt-routine`, and `unsupported-winapi-execution`. Phase 57P host/path-like `INCLUDE` diagnostics remain available with `unsupported-host-include-path`, `unsupported-windows-api-include`, and `unsupported-masm32-library-include`. Phase 57Q `INCLUDELIB` diagnostics remain available with `unsupported-includelib`, `unsupported-windows-api-library`, and `unsupported-masm32-library`. Zero-operand `nop` remains available; Phase 57O - Explicit-Width NOP Encoding-Operand Forms keeps NOP encoding-operand behavior available. Phase 57M segment/group-symbol diagnostics still use `unsupported-segment-symbol` for MASM/object/linker names such as `_TEXT` and `DGROUP`, and Phase 57L `.code` memory-access diagnostics still include `unsupported-code-memory-access`. Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy remains available.

Phase 57R adds clear parser/source-run diagnostics for unsupported `INVOKE` syntax, `ADDR` operands, and common external routine names. `invoke StdOut, addr titleMsg` reports `unsupported-invoke`, `unsupported-addr`, and `unsupported-masm32-runtime-routine`; `invoke crt_printf, addr numberFmt, counter` reports C runtime formatted-output diagnostics; and `invoke ExitProcess, 0` reports `unsupported-winapi-execution` rather than virtual Irvine32 `exit` behavior. Source containing these diagnostics does not execute and does not produce Program Console output. Phase 57R does not implement `INVOKE`, `ADDR`, stack setup, calling conventions, `StdOut`, `crt_printf`, `ExitProcess`, WinAPI execution, PE imports, linker behavior, or external routine execution. Phase 57Q `INCLUDELIB` linker/import-library diagnostics, Phase 57P host/path-like `INCLUDE` diagnostics, Phase 57O NOP encoding-operand behavior, Phase 57M segment/group-symbol diagnostics, Phase 57L `.code` memory-access diagnostics, Phase 57J `.CONST ?` / `.CONST DUP(?)` declaration diagnostics, Phase 57H final-register `[unchanged]` display markers, Phase 57G - Seeded Random Uninitialized Storage Mode seeded uninitialized-storage visible-byte settings, and Phase 57F seeded register/flag startup remain available.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). In broad terms, that includes the C99 core VM/parser/executor, checked simulated memory, `.data`, `.DATA?`, `.CONST`, selected MASM compatibility directives, selected register and memory operands, selected arithmetic/logic/shift/rotate/multiply/divide instructions through Phase 57 - Signed IDIV, diagnostic settings, the default `startup-state-notice`, opt-in seeded register/flag startup and opt-in seeded uninitialized-storage visible bytes for tests/configuration, final-register unchanged display markers, Phase 57R INVOKE/ADDR/external-routine diagnostics and Phase 57Q INCLUDELIB diagnostics, Phase 57P host include path diagnostics, Phase 57O NOP register and explicit-width memory-looking encoding operands, accepted read-only `.CONST ?` / `.CONST DUP(?)` storage with configurable declaration diagnostics, Phase 57M segment/group-symbol diagnostics, Phase 57L `.code` memory-access diagnostics, and the virtual Irvine32 `exit` terminator.

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
