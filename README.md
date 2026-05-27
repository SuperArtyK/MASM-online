# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 57A - README Landing Page Cleanup

Runtime/source-run MASM behavior phase:
Phase 57 - Signed IDIV

Phase 57A is documentation and repository-hygiene work only. Runtime/source-run MASM behavior remains Phase 57 - Signed IDIV.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). In broad terms, that includes the C99 core VM/parser/executor, checked simulated memory, `.data`, `.DATA?`, `.CONST`, selected MASM compatibility directives, selected register and memory operands, selected arithmetic/logic/shift/rotate/multiply/divide instructions through signed `idiv`, diagnostic settings, and the virtual Irvine32 `exit` terminator.

For exact accepted forms, rejected forms, diagnostics, and runtime-phase status, use [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md) instead of this README.

## Quick start

### Serve the website

The browser app uses JavaScript modules and a Web Worker, so serve the `web` directory over local HTTP instead of opening it with `file://`.

POSIX shell:

```sh
python3 -m http.server 8000 --directory web
```

Windows command prompt:

```cmd
scripts\windows\serve_web.cmd
```

Then open:

```text
http://localhost:8000
```

To stop the Windows helper server:

```cmd
scripts\windows\stop_web.cmd
```

### Build the WebAssembly module

POSIX shell:

```sh
./scripts/build_wasm.sh
```

Windows command prompt:

```cmd
scripts\windows\build_wasm.cmd
```

Clean generated WebAssembly outputs on Windows:

```cmd
scripts\windows\clean_wasm.cmd
```

The build scripts require Emscripten `emcc`. If `emcc` is unavailable, native C and Node tests can still run, but browser/Wasm rebuild smoke checks are skipped by the test runner.

### Run tests

Aggregate verification:

```sh
python3 scripts/run_tests.py --all
```

Focused verification groups are available when the aggregate command is too large for the local or hosted environment:

```sh
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
```

For full runner policy, timeout guidance, and fixture inventory, see [`docs/TESTING_GUIDE.md`](docs/TESTING_GUIDE.md).

## Documentation

- [`docs/FULL_IMPLEMENTATION_SPEC.md`](docs/FULL_IMPLEMENTATION_SPEC.md) - product boundaries, stable behavior, cross-cutting rules, and current/future/non-goal distinctions.
- [`docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`](docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md) - phase numbering, phase tasks, required tests, and acceptance criteria.
- [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md) - current accepted MASM32 Educational Mode subset and diagnostics.
- [`docs/TESTING_GUIDE.md`](docs/TESTING_GUIDE.md) - aggregate and focused test guidance.
- [`docs/MILESTONE_HISTORY.md`](docs/MILESTONE_HISTORY.md) - milestone history moved out of the README during Phase 57A.

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
