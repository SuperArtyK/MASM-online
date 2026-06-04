# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 65 - Signed Relational Conditional Jumps

Runtime/source-run MASM behavior phase:
Phase 65 - Signed Relational Conditional Jumps

Status interpretation:
Phase 65 adds executable direct-label signed relational conditional jumps. Repository/archive status and runtime/source-run MASM behavior status now match because this phase adds accepted MASM syntax, parser lowering, VM execution behavior, source-run diagnostics, and runtime phase metadata.

The simulator accepts signed relational conditional jumps for executable code-label and procedure-entry targets: `jl label` / `jnge label`, `jle label` / `jng label`, `jg label` / `jnle label`, and `jge label` / `jnl label`. These jumps evaluate the modeled `ZF`, `SF`, and `OF` flags according to signed relational semantics, consume only the flags required by each mnemonic through the shared `undefined-flag-use` policy, count as one executed VM instruction, preserve registers, memory, modeled flags, Program Console output, and memory-change rows, and respect the Phase 59 `instructionLimit` instruction-count watchdog.

Phase 64 - Equality Conditional Jumps remains implemented. The simulator accepts executable equality conditional jumps: `je label`, `jz label`, `jne label`, and `jnz label`. These direct branch forms consume the modeled `ZF` flag through the shared undefined-flag-use policy path, branch to executable code-label or procedure-entry targets when their condition is true, fall through when their condition is false, count as one executed instruction, preserve registers, memory, and modeled flags, produce no memory-change rows, and respect the same instruction-count watchdog. Phase 61 - Direct JMP Runtime Execution and its direct `jmp label` forms remain implemented.

Phase 64D - Memory Change Source Attribution Display remains implemented. Memory-change rows show the source line that caused each successful mutation, for example `a DWORD | line 10: inc a` when preserved instruction source text is available. Source attribution points to the write-producing instruction, not to a data declaration or earlier address-loading line. Failed writes, strict planned-access failures, invalid writes, and read-only/no-write instructions still create no successful memory-change rows.

Phase 64C - Expanded EFLAGS Flag Display remains implemented. Final register display keeps the canonical `EFLAGS` parent row and adds subordinate `CF`, `ZF`, `SF`, and `OF` child rows with `0` or `1` values derived from the modeled EFLAGS bits. Phase 64C displays modeled flag bit values only. Flag-validity annotations remain future display work. Unmodeled x86 flags such as `PF`, `AF`, `DF`, `IF`, and `TF` are not displayed or implied.

Simulator Messages still render `startup-state-notice`, runtime diagnostics, and `execution-complete` as adjacent logical groups with exactly one blank rendered line between non-empty groups. `startup-state-notice=off` suppresses only the startup notice group and does not remove the separator between runtime warnings/notices and successful completion. Blank separators are formatter-only and do not appear in source-run JSON, diagnostics, Program Console output, or memory-change rows.

Existing memory-destination read-modify-write forms still route their destination memory reads through planned-read validation before bytes are consumed or write-back occurs. This includes current forms such as `inc mem`, `dec mem`, arithmetic/logical memory destinations, shifts/rotates, and `xchg` memory forms. Phase 63 - CMP Memory Operand Forms remains implemented: `cmp reg, mem`, `cmp mem, reg`, and `cmp mem, imm` read memory through checked helpers and planned-read validation before flags are updated.

Phase 61E - Reserved Word Symbol Diagnostics remains active through `reserved-word-symbol`: simulator-recognized reserved words such as `cmp`, `je`, `jz`, `jne`, `jnz`, `jl`, `jnge`, `jle`, `jng`, `jg`, `jnle`, `jge`, and `jnl` cannot be used as user-defined symbols. `OPTION CASEMAP:NONE` does not make reserved words available as user-defined symbols, and `OPTION NOKEYWORD` remains unsupported until a later explicit keyword-control phase. Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening clarifies that parser/source-run capacity diagnostics such as `token-capacity-exceeded`, `source-text-capacity-exceeded`, `code-label-capacity-exceeded`, and `data-capacity-exceeded` are pre-runtime capacity failures, not runtime `instructionLimit` failures such as `instruction-limit-exceeded`. Phase 61C - Branch Debugger Dependency Cleanup documented the debugger/editor boundary and does not implement debugger behavior. Active-time watchdog behavior is not part of Phase 61, Phase 61A, Phase 61B, Phase 61C, Phase 61D, Phase 61E, Phase 62, Phase 63, Phase 64, Phase 64A, Phase 64B, Phase 64C, Phase 64D, or Phase 65; it remains future work owned by Phase 200 - Active Time Watchdog and Worker Responsiveness. Unsigned relational jumps, `loop`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, `call`, `ret`, stack behavior, procedure invocation, branch-distance/type overrides, debugger stepping, breakpoints, breakpoint binding, current-instruction highlighting, editor source navigation, CodeMirror gutter behavior, branch-target editor highlighting, and UI label navigation remain future work.

## Current simulator scope

The current runtime supports the MASM32 Educational Mode subset documented in [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md): selected data sections, MASM compatibility directives, operands, arithmetic/logic/shift/rotate/multiply/divide instructions, `cmp` register/immediate and memory comparisons, code-label metadata, direct JMP runtime execution, equality conditional jumps, signed relational conditional jumps, instruction-count limits, diagnostic settings, startup notices, Simulator Messages grouping, memory-change source attribution display, modeled EFLAGS child display, display markers, unsupported-feature diagnostics, and the virtual Irvine32 `exit` terminator.

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
