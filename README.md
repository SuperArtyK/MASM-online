# Online MASM32 Educational Simulator

Static browser-based educational simulator for small MASM32/Irvine32-style console programs. The project uses a C99 MASM-like parser plus an internal virtual machine compiled to WebAssembly with Emscripten, then runs through a JavaScript browser UI and Web Worker.

## Current repository status

Repository/archive milestone:

- Phase 71A - Optional Root RET Strictness Mode

Runtime/source-run MASM behavior phase:

- Phase 71A - Optional Root RET Strictness Mode

Source-run output-contract identifier naming:

- Current expected protocol token in this revision: `phase-71a-source-run-output-contract-v1`

Treat the token as a source-run output-contract version identifier, not as phase-status prose. A phase-looking prefix in such a token identifies the phase that introduced that specific output contract, not a separate repository/runtime status field.

Protocol/artifact compatibility policy:

- The browser UI expects the loaded Wasm artifact to report the exact runtime/source-run behavior metadata and exact source-run output-contract metadata required by the current UI.
- Older, newer, missing, malformed, or suffix-mismatched runtime/source-run behavior metadata is reported as a UI/Wasm artifact mismatch.
- Missing, malformed, or mismatched source-run output-contract metadata is reported as a UI/Wasm artifact mismatch.
- Artifact compatibility failures are not MASM source diagnostics. They indicate that the UI and loaded Wasm artifact are not a safe pair.
- Phase 70A changed artifact compatibility only. Phase 70B changed documentation and static checks only. Phase 71 changes root RET termination and non-entry procedure fallthrough runtime behavior. Phase 71A adds the optional `rootRetMode` strictness setting while preserving the MASM32-compatible default.

Canonical phase navigation:

- The next canonical guide phase is determined by `docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`.
- After Phase 71A, the next canonical guide phase is Phase 71A1 - Diagnostic Test Runner Subgroup Decomposition.
- Phase 71A has been accepted and completed as optional strictness-mode work.
- Phase 71A1 is test-runner infrastructure only. It must not be treated as a VM semantics phase.
- Phase 71B - User-Facing Diagnostic Milestone-Wording Cleanup follows Phase 71A1 as diagnostic-copy, source-run output-contract metadata, and test cleanup only. It must not be treated as a VM semantics phase.
- Phase 71B1 is conditional source-run/native control-flow test infrastructure only if broad source-run/native verification becomes timeout-prone before Phase 71C.
- Phase 71C is the next planned runtime/source-run MASM behavior phase after Phase 71A. It owns baseline code-stream procedure fallthrough and `code-fell-off-end`.
- Phase 72 - Call Depth Limit and Call Trace Diagnostics remains the later call-depth/call-trace resource-protection phase.

Current repository/archive status is Phase 71A: optional root RET strictness mode is implemented. The latest runtime/source-run MASM behavior is still Phase 71A.

Planned documentation/verification work before the next behavior changes:

- Phase 71A1 will split timeout-prone rendered diagnostic test groups into official subgroups. This is test infrastructure only.
- Phase 71B will clean user-facing diagnostic milestone wording. This is diagnostic-copy/output-contract cleanup only.
- Phase 71B1 is conditional. It will split source-run/native control-flow groups only if those broad groups time out before Phase 71C.

Planned fallthrough behavior corrections after that:

- Phase 71C will replace selected-entry `ENDP` auto-success with MASM/x86-like VM code-stream fallthrough and `code-fell-off-end`.
- Phase 71D will add configurable `procedure-fell-through` diagnostics.
- Phase 71E will add an explicit entry-procedure auto-stop compatibility setting.
- Phase 71F will migrate remaining fallthrough-sensitive fixtures if Phase 71C through Phase 71E do not finish that migration.

Until those phases are accepted, do not describe `code-fell-off-end`, configurable procedure-fallthrough policy, entry-procedure auto-stop compatibility, source-run/native subgrouping from Phase 71B1, or Phase 71C through Phase 71F runtime behavior as implemented.

Phase 69 implements direct near `call ProcedureName` when the target resolves to a user `PROC` entry. A successful direct user-procedure `CALL` writes the pseudo-EIP return token for the instruction after the call to `ESP - 4`, updates `ESP`, and transfers to the procedure entry. That return-token write is an implicit VM stack write: it is checked through the central memory helpers and tracked internally, but the current public source-run output contract does not expose it as a visible `memoryChanges` row. Failed internal stack writes use the central checked-memory diagnostic path and stop without committing the call transfer.

Phase 70 implements helper plain near `ret`/`RET` with no operands for active user-procedure calls. Helper `RET` reads a DWORD pseudo-EIP return token from `[ESP]` through the central checked-memory path, validates that the token maps to an executable lowered VM instruction, increments `ESP` by 4 on success, and transfers to the validated target. Invalid readable return tokens emit `invalid-return-address`; unreadable `[ESP]` uses the existing checked-memory diagnostics. The implicit return-token read is internal VM control-flow machinery and is not exposed as a public source-run `memoryChanges` row. Phase 71 adds selected-entry root `RET` termination: when the selected entry procedure executes `ret` with no helper return pending, execution completes successfully without reading `[ESP]`, mutating `ESP`, or emitting return-token diagnostics. Phase 71A preserves that MASM32-compatible default and adds optional `rootRetMode = "strict-call-frame"`; in strict mode, selected-entry root `RET` stops with `root-ret-disallowed-by-mode` before any stack read or mutation. Phase 71 also reports called non-entry procedure fallthrough with `non-root-procedure-fell-through`.

Irvine32 routine calls such as `call WriteString`, source-level `PUSH`/`POP`, procedure frames, `LOCAL`, `USES`, `PROTO`, `INVOKE`, `ADDR`, `leave`, and `ret imm16` remain deferred.

For implemented source-language behavior, see [`docs/SUPPORTED_SYNTAX.md`](docs/SUPPORTED_SYNTAX.md). For future phase ordering and acceptance criteria, see [`docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`](docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md). For historical milestone detail, see [`docs/MILESTONE_HISTORY.md`](docs/MILESTONE_HISTORY.md).

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
- plain near helper `ret`/`RET` with checked internal pseudo-EIP return-token stack reads and validated return transfer;
- selected-entry root `ret`/`RET` success by default without stack reads when no helper return is pending;
- optional `rootRetMode = "strict-call-frame"` teaching mode, which rejects selected-entry root `RET` with `root-ret-disallowed-by-mode`;
- `non-root-procedure-fell-through` diagnostics for called helper procedures that reach `ENDP` without `RET`;
- successful completion at the selected entry procedure's `ENDP` boundary;
- procedure-entry and call-target classification metadata for parser/tests;
- instruction-count watchdog behavior;
- modeled `CF`, `ZF`, `SF`, and `OF` behavior where implemented;
- structured diagnostics and rendered Simulator Messages;
- separate Program Console and Simulator Messages streams;
- virtual Irvine32 `exit` terminator behavior where `INCLUDE Irvine32.inc` is active.

Future/deferred simulator features include:

- `loop`;
- source-level `push` and `pop`;
- `leave` and `ret imm16`;
- stack frames, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, and `ADDR`;
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

Testing note: if `python3 scripts/run_tests.py --all --quiet` times out in a hosted assistant/container environment, do not treat the timeout as a pass. Run the documented broad groups or official subgroups, report the aggregate timeout explicitly, and list which commands passed, failed, timed out, were skipped, or were not run. Rendered diagnostic tests must not be weakened to smoke tests to avoid timeouts.

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
