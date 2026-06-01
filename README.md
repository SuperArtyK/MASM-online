# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 62 - CMP Register and Immediate Forms

Runtime/source-run MASM behavior phase:
Phase 62 - CMP Register and Immediate Forms

Status interpretation:
Phase 62 changes runtime/source-run MASM behavior by adding `cmp` for register/register and register/immediate forms. `cmp` computes subtraction flags only, updates the modeled `CF`, `ZF`, `SF`, and `OF` flags through the shared compare helper, preserves both operands, and produces no memory-change rows. Because `cmp` is now a recognized instruction mnemonic, it is also reserved for user-defined symbols by the Phase 61E - Reserved Word Symbol Diagnostics rule. Reserved-word diagnostics use `reserved-word-symbol`; `OPTION CASEMAP:NONE` does not make reserved words available as user-defined symbols, and `OPTION NOKEYWORD` remains unsupported until a later explicit keyword-control phase.

Phase 62 preserves Phase 61 - Direct JMP Runtime Execution and its direct `jmp label` forms. Phase 62 does not implement `cmp` memory operands; those are deferred to Phase 63 - CMP Memory Operand Forms. Direct `jmp label` forms that target executable code labels or procedure-entry labels still transfer to the resolved VM instruction index, count as one executed instruction, preserve modeled flags, produce no memory-change row, and respect the Phase 59 `instructionLimit` watchdog. Phase 61B clarified the Phase 59 instruction-count watchdog boundary. Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening clarifies that parser/source-run capacity diagnostics such as `token-capacity-exceeded`, `source-text-capacity-exceeded`, `code-label-capacity-exceeded`, and `data-capacity-exceeded` are pre-runtime capacity failures, not runtime `instructionLimit` failures such as `instruction-limit-exceeded`. Phase 61C - Branch Debugger Dependency Cleanup documented the debugger/editor boundary and does not implement debugger behavior. Active-time watchdog behavior is not part of Phase 61, Phase 61A, Phase 61B, Phase 61C, Phase 61D, Phase 61E, or Phase 62; it remains future work owned by Phase 200 - Active Time Watchdog and Worker Responsiveness. Conditional jumps, `loop`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, `call`, `ret`, stack behavior, procedure invocation, branch-distance/type overrides, debugger stepping, breakpoints, breakpoint binding, current-instruction highlighting, editor source navigation, CodeMirror gutter behavior, branch-target editor highlighting, and UI label navigation remain future work.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md): selected data sections, MASM compatibility directives, operands, arithmetic/logic/shift/rotate/multiply/divide instructions, `cmp` register/immediate comparisons, code-label metadata, direct JMP runtime execution, instruction-count limits, diagnostic settings, startup notices, display markers, unsupported-feature diagnostics, and the virtual Irvine32 `exit` terminator.

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
