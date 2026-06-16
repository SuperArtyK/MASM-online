# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current status

| Field | Current value |
|---|---|
| Current milestone | Phase 76 - PROC USES Parsing and Metadata |

Phase 76 accepts whitespace-separated `PROC USES` metadata for `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, and `EDI`, preserves declared order on procedure records, rejects invalid, duplicate, alias, comma, and malformed `USES` lists with targeted diagnostics, and reports `unsupported-proc-uses-runtime` when execution enters or calls a `USES` procedure before Phase 77 runtime save/restore.

For current accepted syntax, rejected forms, diagnostics, and future/deferred features, see [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). For build and artifact verification details, see [`docs/BUILDING_AND_DEVELOPMENT.md`](docs/BUILDING_AND_DEVELOPMENT.md). For milestone history, see [`docs/MILESTONE_HISTORY.md`](docs/MILESTONE_HISTORY.md).

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
- direct user-procedure `call ProcedureName` with checked internal pseudo-EIP return-token stack writes;
- configurable `callDepthLimit` for direct user-procedure `CALL` chains, default `64` and accepted range `1..4096`, with over-limit `call-depth-exceeded` before rejected `CALL` mutation;
- source-level 32-bit `push` for registers, immediates, and DWORD memory sources;
- source-level 32-bit `pop` for registers and DWORD memory destinations;
- source-level `leave`/`LEAVE` frame teardown shorthand with checked saved-EBP reads and no public `memoryChanges` rows;
- plain near helper `ret`/`RET` with checked internal pseudo-EIP return-token stack reads and validated return transfer when a helper call return is pending;
- near helper `ret imm16`/`RET imm16` with unsigned 16-bit caller-cleanup bytes applied only after successful return-token validation;
- root-code-stream `ret`/`RET` success by default without stack reads when no helper return is pending, including after ordinary fallthrough from the selected entry into later procedure text;
- optional `rootRetMode = "strict-call-frame"` teaching mode, which rejects root-code-stream `RET` with `root-ret-disallowed-by-mode`;
- configurable `procedureFallthroughPolicy` for ordinary procedure-boundary fallthrough;
- configurable `entryProcedureEndMode`, default `code-stream` and opt-in `stop-at-entry-end`, for selected-entry `ENDP` boundary compatibility;
- `procedure-fell-through` diagnostics for ordinary procedure-boundary fallthrough, including called helper procedures;
- baseline VM code-stream fallthrough across procedure boundaries when execution reaches later lowered instructions without an explicit terminator or transfer;
- `code-fell-off-end` runtime diagnostics when execution reaches the end of the executable stream without explicit `RET` or Irvine32 `exit`;
- procedure-entry and call-target classification metadata for parser/tests;
- `PROC USES` parsing metadata for `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, and `EDI`, stored in declared order;
- instruction-count watchdog behavior;
- modeled `CF`, `ZF`, `SF`, and `OF` behavior where implemented;
- structured diagnostics and rendered Simulator Messages;
- separate Program Console and Simulator Messages streams;
- virtual Irvine32 `exit` terminator behavior where `INCLUDE Irvine32.inc` is active.

Future/deferred simulator features include:

- `loop`;
- stack-frame creation, `PROC USES` runtime save/restore, `LOCAL`, `PROTO`, `INVOKE`, and `ADDR`;
- selected Irvine32 routine dispatch if an owning phase defines it;
- active-time or wall-clock watchdog behavior;
- debugger/editor branch behavior;
- selected macro-compatibility conveniences explicitly assigned to later accepted milestones.

Rejected simulator-owned CALL target forms remain rejected unless a later accepted phase explicitly changes that simulator-owned form. These include register, memory, indirect, immediate, `OFFSET`, ordinary-label, data-symbol, equate, unknown, malformed, and recognized-but-not-yet-executable Irvine32 routine targets.

External/API calls are different. They are non-goal target categories, not ordinary deferred CALL forms. `call ExitProcess`, PE/import/library targets, host callbacks, native procedures, and other external/API targets remain outside the simulator unless the canonical full specification and implementation guide deliberately revise the project boundary.

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

Testing note: if `python3 scripts/run_tests.py --all --quiet` times out in a hosted assistant/container environment, do not treat the timeout as a pass. Run the documented broad groups or official subgroups, report the aggregate timeout explicitly, and list which commands passed, failed, timed out, were skipped, or were not run. Rendered diagnostic tests must not be weakened to smoke tests to avoid timeouts.

Build WebAssembly artifacts when Emscripten `emcc` is available:

```sh
./scripts/build_wasm.sh
```

For detailed setup, Windows command files, Visual Studio notes, missing-`emcc` troubleshooting, focused test groups, and browser/Wasm smoke guidance, see [`docs/BUILDING_AND_DEVELOPMENT.md`](docs/BUILDING_AND_DEVELOPMENT.md).

## Documentation

- [`docs/FULL_IMPLEMENTATION_SPEC.md`](docs/FULL_IMPLEMENTATION_SPEC.md) - final product behavior, product boundaries, stable cross-cutting rules, permanent non-goals, and user-visible behavior contracts.
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
