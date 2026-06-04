# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 66A - Current-Status Documentation De-Cluttering

Runtime/source-run MASM behavior phase:
Phase 66 - Unsigned Relational Conditional Jumps

Phase 66 is the latest runtime/source-run MASM behavior phase. Phase 66A is documentation cleanup only and does not add MASM syntax or runtime behavior.

For the complete current syntax list, rejected forms, diagnostics, and future/deferred features, see [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). For historical milestone detail, see [`docs/MILESTONE_HISTORY.md`](docs/MILESTONE_HISTORY.md).

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md).

At a high level, the current subset includes:

- selected MASM32-style data sections and declarations;
- selected MASM compatibility directives;
- checked simulated memory with layout, validation, `.DATA?`, `.CONST`, and uninitialized-origin teaching diagnostics;
- selected register, memory, immediate, and expression operands;
- selected arithmetic, bitwise, shift, rotate, multiply, divide, compare, and branch instructions;
- direct `jmp`;
- equality conditional jumps;
- signed relational conditional jumps;
- unsigned relational conditional jumps;
- instruction-count watchdog behavior;
- modeled `CF`, `ZF`, `SF`, and `OF` behavior where implemented;
- structured diagnostics and rendered Simulator Messages;
- separate Program Console and Simulator Messages streams;
- virtual Irvine32 `exit` terminator behavior where `INCLUDE Irvine32.inc` is active.

Still future or unsupported unless a later accepted milestone says otherwise:

- `loop`;
- indirect/register/memory/immediate branch targets;
- branch distance/type overrides;
- source-level `push` and `pop`;
- `call`, `ret`, `leave`, `ret imm16`, stack frames, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, and `ADDR`;
- Irvine32 callable routines beyond already implemented virtual terminator behavior;
- active-time or wall-clock watchdog behavior;
- debugger/editor branch behavior;
- full MASM macro expansion;
- Windows API execution;
- PE loading or object-file linking;
- host filesystem include loading;
- native x86 execution.

This README is intentionally concise. Exact accepted forms, rejected forms, diagnostic codes, examples, and future/deferred syntax belong in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md), not in this README.

When this section changes, revise existing feature categories in place instead of adding changelog bullets.

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
