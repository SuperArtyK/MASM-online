# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 57Q - INCLUDELIB and External Library Diagnostics

Runtime/source-run MASM behavior phase:
Phase 57Q - INCLUDELIB and External Library Diagnostics

Phase 57Q adds clear parser/source-run diagnostics for `INCLUDELIB` linker and import-library directives. General library directives such as `includelib customlib.lib` report `unsupported-includelib`; MASM32 SDK libraries such as `includelib \masm32\lib\masm32.lib` or `includelib masm32.lib` report `unsupported-masm32-library`; Windows import libraries such as `includelib \masm32\lib\kernel32.lib`, `includelib C:\masm32\lib\kernel32.lib`, or `includelib kernel32.lib` report `unsupported-windows-api-library`. These diagnostics explain that MASM32 Educational Mode does not link object files, load `.lib` files, process PE imports, or execute external library routines. Phase 57P host/path-like `INCLUDE` diagnostics remain available for unsupported local include files with stable codes such as `unsupported-host-include-path`, `unsupported-windows-api-include`, and `unsupported-masm32-library-include`, while supported virtual includes such as `INCLUDE Irvine32.inc` and `INCLUDE Macros.inc` remain simulator-defined and do not load host files. Phase 57Q does not implement host filesystem access, library search paths, object files, import tables, PE loading, a linker, WinAPI execution, external routine execution, or macro expansion. Phase 57O - Explicit-Width NOP Encoding-Operand Forms keeps NOP encoding-operand behavior available: Zero-operand `nop` remains supported, alongside 16-bit/32-bit register encoding operands and explicit `WORD`/`SWORD`/`DWORD`/`SDWORD PTR` memory-looking encoding operands as source-level no-ops. Phase 57M segment/group-symbol diagnostics use `unsupported-segment-symbol` for MASM/object/linker names such as `_TEXT` and `DGROUP`; those names remain unsupported as simulator region aliases. Phase 57L `.code` memory-access diagnostics still include `unsupported-code-memory-access`. Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy `.CONST ?` / `.CONST DUP(?)` declaration diagnostics, Phase 57H final-register `[unchanged]` display markers, Phase 57G - Seeded Random Uninitialized Storage Mode seeded uninitialized-storage visible-byte settings, and Phase 57F seeded register/flag startup remain available. The instruction subset remains anchored by Phase 57 - Signed IDIV.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). In broad terms, that includes the C99 core VM/parser/executor, checked simulated memory, `.data`, `.DATA?`, `.CONST`, selected MASM compatibility directives, selected register and memory operands, selected arithmetic/logic/shift/rotate/multiply/divide instructions through signed `idiv`, diagnostic settings, the default `startup-state-notice`, opt-in seeded register/flag startup and opt-in seeded uninitialized-storage visible bytes for tests/configuration, final-register unchanged display markers, Phase 57Q INCLUDELIB diagnostics, Phase 57P host include path diagnostics, Phase 57O NOP register and explicit-width memory-looking encoding operands, accepted read-only `.CONST ?` / `.CONST DUP(?)` storage with configurable declaration diagnostics, Phase 57M segment/group-symbol diagnostics, Phase 57L `.code` memory-access diagnostics, and the virtual Irvine32 `exit` terminator.

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
