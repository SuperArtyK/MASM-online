# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 60 - Direct JMP Parsing and Target Lowering

Runtime/source-run MASM behavior phase:
Phase 60 - Direct JMP Parsing and Target Lowering

Phase 60 parses, classifies, and lowers direct `jmp label` forms that target executable code labels or procedure-entry labels. Runtime branch transfer remains deferred to Phase 61; when execution reaches a lowered direct `jmp`, the simulator emits `branch-runtime-deferred`, preserves already-committed state, and stops without reporting normal completion. Phase 59 `instructionLimit` watchdog behavior remains available and still reports `instruction-limit-exceeded` when a configured limit blocks the next instruction.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md): selected data sections, MASM compatibility directives, operands, arithmetic/logic/shift/rotate/multiply/divide instructions, code-label metadata, direct JMP target lowering, instruction-count limits, diagnostic settings, startup notices, display markers, unsupported-feature diagnostics, and the virtual Irvine32 `exit` terminator.

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
