# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 57N - Zero-Operand NOP Audit, Repair, and Regression Hardening

Runtime/source-run MASM behavior phase:
Phase 57M - MASM Segment and Group Symbol Diagnostics

Phase 57N audits and hardens the existing zero-operand `nop` instruction path. Zero-operand `nop` remains an IR-level no-op that parses case-insensitively, executes without changing registers, flags, flag-validity metadata, memory, Program Console output, or memory-change rows, and counts as one executed instruction. Phase 57N also gives operand-bearing `nop` forms stable diagnostics that keep explicit-width NOP encoding-operand forms deferred to Phase 57O - Explicit-Width NOP Encoding-Operand Forms. Because zero-operand `nop` was already accepted before this hardening milestone, the runtime/source-run MASM behavior phase remains Phase 57M - MASM Segment and Group Symbol Diagnostics. Phase 57M segment/group-symbol diagnostics use `unsupported-segment-symbol` for MASM/object/linker names such as `_TEXT` and `DGROUP`; those names remain unsupported as simulator region aliases. Phase 57L `.code` memory-access diagnostics still include `unsupported-code-memory-access`. Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy `.CONST ?` / `.CONST DUP(?)` declaration diagnostics, Phase 57H final-register `[unchanged]` display markers, Phase 57G - Seeded Random Uninitialized Storage Mode seeded uninitialized-storage visible-byte settings, and Phase 57F seeded register/flag startup remain available. The instruction subset remains anchored by Phase 57 - Signed IDIV.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). In broad terms, that includes the C99 core VM/parser/executor, checked simulated memory, `.data`, `.DATA?`, `.CONST`, selected MASM compatibility directives, selected register and memory operands, selected arithmetic/logic/shift/rotate/multiply/divide instructions through signed `idiv`, diagnostic settings, the default `startup-state-notice`, opt-in seeded register/flag startup and opt-in seeded uninitialized-storage visible bytes for tests/configuration, final-register unchanged display markers, accepted read-only `.CONST ?` / `.CONST DUP(?)` storage with configurable declaration diagnostics, Phase 57M segment/group-symbol diagnostics, Phase 57L `.code` memory-access diagnostics, and the virtual Irvine32 `exit` terminator.

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
