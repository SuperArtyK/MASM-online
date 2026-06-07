# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

Repository/archive milestone:
Phase 68B - EIP Pseudo-Code Address Display and Source-Operand Restrictions

Runtime/source-run MASM behavior phase:
Phase 68B - EIP Pseudo-Code Address Display and Source-Operand Restrictions

The current runtime displays `EIP` as derived VM pseudo-code-address control state. The displayed value starts at `00401000h` for lowered executable instruction index 0 and advances by 4 per lowered executable VM instruction. Labels, blank lines, comments, procedure markers, and non-executable directives do not receive pseudo-EIP addresses. Source code cannot read, write, address through, use as an instruction operand, or define `EIP`; such attempts produce `invalid-eip-operand`.

The runtime initializes `ESP` from the active stack region when a program is loaded. The empty-stack convention is the first address past the high end of the selected stack region; direct supported writes to `ESP` remain legal, and future 32-bit push-like operations must compute `ESP - 4` before writing through checked stack memory.

The runtime continues to honor `END entryName` as the source-run entry procedure. Execution starts inside the selected procedure, helper procedures before the selected entry do not run automatically, and ordinary fallthrough at the selected entry procedure boundary completes successfully without falling into later procedures.

Procedure entries remain callable-target metadata distinct from ordinary code labels for future `CALL` and `INVOKE` phases. The classifier distinguishes user procedure entries, ordinary executable labels, data symbols, numeric equates, central-registry Irvine32 names, external/API non-goals, reserved words, unknown names, and malformed target expressions without making `CALL` executable.

The current runtime also supports the implemented MASM32 Educational Mode subset through unsigned relational conditional jumps, plus earlier arithmetic, data, memory, diagnostics, layout, and Irvine32 virtual `exit` behavior.

Next recommended implementation work:
Phase 69 - Direct CALL to User Procedures

Phase 69 is the next procedure-execution phase after Phase 68B. Phase 69 may consume the Phase 68A `ESP` startup contract and the Phase 68B pseudo-EIP contract for direct user-procedure `CALL`. It must still not implement RET, source-level PUSH/POP, procedure frames, `LOCAL`, `USES`, `PROTO`, `INVOKE`, `ADDR`, or Irvine32 callable routine dispatch unless its own canonical phase explicitly says otherwise.

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
- selected-entry source-run startup from `END entryName`;
- `ESP` startup initialized from the active stack region empty-stack address;
- displayed `EIP` derived from VM pseudo-code-address control state and rejected as a source-level instruction operand or user symbol;
- successful completion at the selected entry procedure's `ENDP` boundary;
- procedure-entry and future-call target classification metadata for parser/tests;
- instruction-count watchdog behavior;
- modeled `CF`, `ZF`, `SF`, and `OF` behavior where implemented;
- structured diagnostics and rendered Simulator Messages;
- separate Program Console and Simulator Messages streams;
- virtual Irvine32 `exit` terminator behavior where `INCLUDE Irvine32.inc` is active.

Future/deferred simulator features include:

- `loop`;
- indirect/register/memory/immediate branch targets;
- branch distance/type overrides;
- source-level `push` and `pop`;
- `call`, `ret`, `leave`, `ret imm16`, stack frames, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, and `ADDR`;
- Irvine32 callable routines beyond already implemented virtual terminator behavior;
- active-time or wall-clock watchdog behavior;
- debugger/editor branch behavior;
- selected macro-compatibility conveniences explicitly assigned to later accepted milestones.

Permanent non-goals remain outside the simulator unless the canonical full specification and implementation guide are deliberately revised:

- full MASM macro expansion and full MASM macro compatibility;
- Windows API execution or modeling;
- PE loading;
- object-file linking;
- import-library behavior;
- host filesystem include loading;
- native x86 execution;
- full x86 emulation;
- Windows process, DLL, handle, or kernel behavior.

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
- [`docs/history/HISTORY_README.md`](docs/history/HISTORY_README.md) - archive index for curated audit/handoff reports and standalone milestone reports.
- [`docs/BUILDING_AND_DEVELOPMENT.md`](docs/BUILDING_AND_DEVELOPMENT.md) - local serving, build commands, prerequisites, Visual Studio notes, and development workflow.


Historical audit and milestone material is stored under `docs/history/` for curated audit and handoff reports and `docs/history/reports/` for standalone milestone reports. These files are preserved for provenance, stale-assumption detection, and regression-risk review. They do not replace the canonical specification, implementation guide, supported-syntax reference, latest repository state, or latest accepted milestone report.

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
