# Milestone History

This file preserves long-form milestone-history material outside the README. Phase 57B updated it into a navigation layer for humans and AI assistants while keeping historical detail available.

The README is intentionally concise. This file carries the longer milestone history that had previously made the README difficult to use as a landing page.

Source-of-truth rule:

- [`FULL_IMPLEMENTATION_SPEC.md`](FULL_IMPLEMENTATION_SPEC.md) remains the canonical source for final product behavior, product boundaries, stable cross-cutting rules, permanent non-goals, and user-visible behavior contracts.
- [`INCREMENTAL_IMPLEMENTATION_GUIDE.md`](INCREMENTAL_IMPLEMENTATION_GUIDE.md) remains the canonical source for phase numbering, phase tasks, required tests, and acceptance criteria.
- [`SUPPORTED_SYNTAX.md`](SUPPORTED_SYNTAX.md) remains the current reference for accepted MASM32 Educational Mode syntax and diagnostics.
- [`BUILDING_AND_DEVELOPMENT.md`](BUILDING_AND_DEVELOPMENT.md) owns detailed local serving, build, prerequisite, Visual Studio, and development workflow guidance.
- Milestone reports, archived repository states, and this history file are historical evidence. They do not replace or override the canonical specification and implementation guide.
- Curated audit and handoff reports are stored under [`history/`](history/), and standalone milestone reports are stored under [`history/reports/`](history/reports/). These archived files are historical evidence, not current behavior authority.

Recent milestone detail in this file may be listed most-recent-first for handoff convenience. That ordering is historical/navigation ordering only. It is not the canonical implementation order.

The canonical implementation order, phase numbering, phase tasks, required tests, and acceptance criteria remain in `docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`. Future assistants must not infer phase dependencies or next implementation work from the order of recent-history paragraphs in this file when the guide states a different order.

Latest recorded completed milestone in this history file:
Phase 78 - LOCAL Declaration Parser and Frame Layout Metadata

Latest recorded runtime/source-run MASM behavior phase in this history file:
Phase 78 - LOCAL Declaration Parser and Frame Layout Metadata

This history file records completed milestones and audit evidence. It is not the phase-order authority and not a replacement for `docs/FULL_IMPLEMENTATION_SPEC.md`, `docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`, `docs/SUPPORTED_SYNTAX.md`, current repository code, or current tests. If this history file is not updated during a later milestone, its `latest recorded` lines may be older than the active repository state. Use the implementation guide and latest accepted milestone evidence to determine the next canonical implementation phase.

Forward-looking phase navigation is guide-owned. At the time this history entry was updated, Phase 78 had been implemented as LOCAL declaration parser and frame layout metadata. That statement is historical navigation for this history entry, not an implementation permission by itself and not a substitute for reading the current implementation guide.

Corrective artifact-evidence note for Phase 71B: the latest Phase 71B repository archive's checked-in `web/dist/masm32_sim_core.wasm` contains `phase-71b-source-run-output-contract-v1`. This corrects the stale artifact-token warning preserved in `docs/history/reports/Milestone 71B report.md`, which stated that the checked-in Wasm still contained the earlier Phase 71A output-contract token. The historical report should remain period evidence unless the project owner explicitly requests historical report correction, but future audits should treat the archive's artifact-content scan as the stronger evidence for the checked-in Wasm token.

Phase 69B was output/message-ordering cleanup only. At its completion it kept Phase 69 as the latest runtime/source-run MASM behavior phase while adding explicit final-register display separators and ensuring the startup-state notice is serialized/rendered first whenever execution begins and startup notices are enabled. Phase 70 has since advanced the runtime/source-run MASM behavior phase.

Phase 69C is artifact/test-infrastructure cleanup only. It adds the separate `sourceRunOutputContract` JSON field, corresponding browser/protocol stale-output-contract detection, and tests for matching, missing, and stale contract metadata. The latest recorded source tree for this history entry now expects `phase-78-local-metadata-output-contract-v1`; historical Phase 69C evidence may still mention older contract tokens from the milestone when they were current. Phase 69C does not change accepted MASM syntax, rejected MASM syntax, parser behavior, VM instruction semantics, Program Console output, memory values, register values, public source-run memory-change rows, or the runtime/source-run MASM behavior phase. Phase 70A builds on that protocol/artifact boundary by requiring exact runtime/source-run behavior metadata; older, newer, missing, malformed, or suffix-mismatched runtime metadata now produces the existing stale-artifact diagnostic by default.

Phase 69 implements direct near `CALL` instructions to user `PROC` entries. Successful direct user-procedure `CALL` writes the pseudo-EIP return token to the simulated stack through checked VM memory helpers, updates `ESP`, preserves modeled flags and flag-validity metadata, and transfers control to the target procedure entry. That stack write is internal VM execution state; current public source-run JSON does not expose it as a visible `memoryChanges` row. Failed internal stack writes use the central checked-memory diagnostic path and stop before committing the call transfer.

Phase 70 implements plain near `RET` with no operands. Successful RET reads the DWORD pseudo-EIP return token from `[ESP]` through checked VM memory helpers, validates that the token maps to an executable lowered VM instruction, increments `ESP` by 4, preserves modeled flags and flag-validity metadata, and transfers control to the validated target. Invalid readable tokens stop with `invalid-return-address`; unreadable `[ESP]` keeps using the central checked-memory diagnostics. The implicit RET return-token read is internal VM control-flow state and is not exposed as a public source-run `memoryChanges` row.

Phase 70A - Runtime Metadata Exact-Match Compatibility Check is complete as protocol/artifact compatibility cleanup. It does not change MASM source syntax, parser behavior, VM execution semantics, CALL behavior, RET behavior, Program Console output, or public `memoryChanges` rows. It makes newer-runtime and older-runtime Wasm metadata mismatches visible by default and leaves forward-compatible runtime acceptance to a later explicitly specified compatibility phase.

Phase 70B - Canonical Documentation Alignment and Compatibility Test Matrix Cleanup is complete as documentation/static-test cleanup. At its completion, it updated the canonical spec and guide, active status docs, historical navigation, and static documentation checks so future assistants distinguish repository/archive status, runtime/source-run MASM behavior phase, source-run output-contract identifier, protocol/artifact compatibility policy, next canonical guide phase, and next runtime/source-run MASM behavior phase. Phase 70B itself did not change accepted MASM syntax, parser behavior, VM behavior, instruction behavior, Irvine32 behavior, memory behavior, source-run output shape, runtime/source-run MASM behavior metadata, Program Console output, or Simulator Messages behavior. Phase 71 has since advanced the runtime/source-run MASM behavior phase.

Phase 68B displayed `EIP` behavior remains active: displayed `EIP` is derived VM pseudo-code-address control state beginning at `00401000h` and advancing by 4 per lowered executable VM instruction. Source code cannot read, write, address through, use as an instruction operand, or define `EIP`; such forms produce `invalid-eip-operand`.

Phase 68A `ESP` startup remains active: `ESP` initializes from the active stack region when a program is loaded, direct supported writes to `ESP` remain legal, and direct user-procedure `CALL` uses the active `ESP` value for its checked return-token stack write.

Phase 70 preserves Phase 67A selected-entry source-run behavior, Phase 68 call-target metadata, Phase 68B pseudo-EIP display tokens, and Phase 69 direct CALL mechanics. Phase 71 adds selected-entry root RET termination and called non-entry procedure fallthrough diagnostics. Phase 72A adds source-level 32-bit PUSH/POP stack transfers. Phase 73 adds source-level LEAVE frame teardown. Phase 74 adds near `RET imm16` caller cleanup. Phase 75 adds targeted `PROC` declaration diagnostics while preserving bare procedure metadata. Phase 76 adds accepted `PROC USES` parsing metadata. Phase 77 adds direct-CALL runtime save/restore for `PROC USES`. Phase 78 adds parser-only `LOCAL` declaration metadata and deterministic local frame layout metadata. Runtime local stack allocation, local operand resolution, procedure-frame creation, Irvine32 routine dispatch beyond virtual `exit`, far returns, `INVOKE`, `PROTO`, and `ADDR` remain future-owned.



## Phase 78 - LOCAL Declaration Parser and Frame Layout Metadata

Phase 78 is complete as a parser/source-run metadata phase. It accepts supported `LOCAL` declarations inside procedure bodies before executable instructions, including scalar declarations, array declarations with positive constant counts, and comma-separated declarators using `BYTE`, `SBYTE`, `WORD`, `SWORD`, `DWORD`, and `SDWORD`.

Phase 78 stores procedure-scoped local metadata, declaration order, element count, element size, alignment, source location, deterministic negative `EBP` offsets, and total local-frame size rounded to 4 bytes. It allows locals to shadow global data and numeric equates inside the procedure while rejecting duplicate locals, procedure-name collisions, and same-procedure label collisions.

Phase 78 advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-78-local-metadata-output-contract-v1` because accepted syntax, structured diagnostics, rendered Simulator Messages, and browser protocol interpretation changed. Runtime local stack allocation, local operand resolution/addressing, `ADDR`, `PROTO`, `INVOKE`, parameters, calling conventions, automatic stack frames, and Irvine32 callable routine dispatch remain future-owned.

## Phase 77 - PROC USES Runtime Save/Restore

Phase 77 is complete as a runtime/source-run behavior phase. It uses the Phase 76 ordered `PROC USES` metadata for supported direct `CALL` entry, saves listed `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, and `EDI` registers on the checked simulated stack in declared order, and restores them in reverse order on `RET` before the pseudo-EIP return token is popped and validated.

Phase 77 preserves modeled flags, keeps direct CALL/RET stack balance, preserves listed registers including `EAX`, and leaves unlisted `EAX` available as a helper return-value register. Automatic USES save failure reports `stack-overflow` before partial CALL/USES mutation; automatic USES restore failure reports `stack-underflow` before partial register restore or return mutation. Selected-entry startup, direct branch transfer, and ordinary fallthrough into a `PROC USES` body still report `unsupported-proc-uses-runtime` because no CALL-created preservation frame exists.

Phase 77 advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-77-proc-uses-runtime-output-contract-v1` because runtime semantics, structured diagnostics, rendered Simulator Messages, and browser protocol interpretation changed. `LOCAL`, `PROTO`, `INVOKE`, `ADDR`, parameters, calling conventions, automatic stack frames, and Irvine32 callable routine dispatch remain future-owned.


## Phase 76 - PROC USES Parsing and Metadata

Phase 76 is complete as a parser/source-run metadata phase. It accepts `name PROC USES reglist` when `reglist` is a whitespace-separated list of `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, and `EDI`, stores the canonical registers in declared order on procedure metadata, and preserves accepted bare `PROC` behavior from Phase 75.

Phase 76 rejects missing, malformed, comma-separated, duplicate, alias, `ESP`, `EBP`, and unknown `USES` registers with targeted structured diagnostics and rendered Simulator Messages. Runtime register save/restore remains Phase 77-owned; entering a procedure with `USES` metadata, including direct `CALL`, selected-entry startup, direct branch transfer, or ordinary fallthrough, reports `unsupported-proc-uses-runtime` before executing the procedure body so the simulator does not silently ignore saved-register metadata. Direct `CALL` reports this diagnostic before stack mutation.

Phase 76 advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-76-proc-uses-metadata-output-contract-v1` because accepted parser metadata, source-run diagnostics, rendered Simulator Messages, and browser protocol interpretation changed. `LOCAL`, `PROTO`, `INVOKE`, `ADDR`, parameters, calling conventions, automatic stack frames, Irvine32 callable routine dispatch, and `PROC USES` runtime save/restore remain future-owned.

## Phase 75 - PROC Metadata Baseline and Attribute Diagnostics

Phase 75 is complete as a parser/source-run diagnostic and metadata phase. It preserves accepted bare `name PROC`/`name ENDP` procedure ranges, user-symbol `CASEMAP` behavior, source locations, executable body boundaries, and runtime behavior while adding targeted diagnostics for unsupported `PROC` attributes or parameters, malformed declarations, mismatched `ENDP` names, and duplicate procedures.

Phase 75 advanced runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-75-proc-metadata-output-contract-v1` because structured diagnostics and rendered Simulator Messages changed. At Phase 75 completion, accepted `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, `ADDR`, parameters, calling conventions, automatic stack frames, and `USES` save/restore were still future-owned. Phase 76 later accepted `PROC USES` parsing metadata while leaving runtime save/restore future-owned.

## Phase 74 - RET imm16 Instruction

Phase 74 is complete as a runtime/source-run behavior phase. It implements near `ret imm16`/`RET imm16` as explicit caller-cleanup helper return behavior. The executor reads and validates the pseudo-EIP return token exactly like plain helper `RET`, applies the unsigned 16-bit immediate cleanup only after validation succeeds, validates the final `ESP` against the active stack region or documented empty-stack boundary, preserves modeled flags, and keeps skipped argument bytes internal and unmodified.

Phase 74 advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-74-ret-imm16-output-contract-v1` because accepted syntax, runtime return-cleanup semantics, structured diagnostics, rendered Simulator Messages, and browser protocol interpretation changed. `RETF`, `ENTER`, automatic frame creation, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, `ADDR`, calling conventions, and Irvine32 callable routine dispatch remain future-owned.

## Phase 73 - LEAVE Instruction

Phase 73 is complete as a runtime/source-run behavior phase. It implements source-level `leave`/`LEAVE` as validation-first frame teardown shorthand. The executor reads saved `EBP` from DWORD `[EBP]` through checked VM memory before mutation, then commits `ESP = old EBP + 4` and `EBP = saved EBP` only after the read succeeds. LEAVE preserves modeled flags, creates no public `memoryChanges` rows, and preserves no-partial-mutation behavior on fatal checked-memory failures.

Phase 73 advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-73-leave-output-contract-v1` because accepted syntax, runtime stack-read semantics, structured diagnostics, rendered Simulator Messages, and browser protocol interpretation changed. Phase 72A source-level PUSH/POP, helper CALL/RET, root RET, procedure fallthrough settings, call-depth limits, and Irvine32 `exit` behavior remain unchanged. `RET imm16`, `ENTER`, `RETF`, automatic frame creation, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, `ADDR`, calling conventions, and Irvine32 callable routine dispatch remain future-owned.

## Phase 72A - PUSH and POP Stack Instructions

Phase 72A implements the first source-level 32-bit stack-transfer subset. `push` accepts 32-bit registers, 32-bit immediates, and DWORD memory sources; `pop` accepts 32-bit registers and DWORD memory destinations. The executor routes stack reads/writes through checked VM memory helpers, preserves modeled flags, handles `push esp`, `pop esp`, and `pop DWORD PTR [esp]` timing explicitly, and preserves no-partial-mutation behavior on fatal failures. Source-run metadata advanced to `phase-72a-push-pop-stack-output-contract-v1`; successful source-level PUSH stack writes and POP memory-destination writes are exposed as public `memoryChanges`, while internal CALL/RET return-token stack mechanics remain hidden. At Phase 72A completion, procedure frames, `RET imm16`, `LEAVE`, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, `ADDR`, calling conventions, and Irvine32 routine dispatch remained future-owned.

## Phase 72 - Call Depth Limit and Call Trace Diagnostics

Phase 72 is complete as a runtime/source-run behavior phase. It adds deterministic call-depth resource protection for direct user-procedure `CALL` chains through the source-run setting `callDepthLimit`, which defaults to `64` and accepts integer values from `1` through `4096`. Invalid settings stop before execution with `invalid-call-depth-limit`.

The VM tracks call depth separately from the Phase 71 root/helper terminal-state metadata. A committed direct user-procedure helper `CALL` increments depth only after target validation, depth-limit validation, and successful checked return-token stack write. A successfully validated ordinary helper `RET` decrements depth. When a direct `CALL` would exceed the configured limit, execution reports `call-depth-exceeded` before writing the return token, mutating `ESP`, changing instruction pointer, changing flags, creating memory-change rows, writing Program Console output, or reporting successful terminal status.

Phase 72 advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-72-call-depth-limit-output-contract-v1` because public settings, runtime diagnostics, structured source-run JSON, rendered Simulator Messages, and browser protocol interpretation changed. It preserves helper `CALL`/`RET`, root `RET`, procedure fallthrough, selected-entry entry-end compatibility, and Irvine32 `exit` behavior, and it intentionally emits no call-trace metadata or `call-trace-truncated` diagnostic.


## Phase 71F - Fallthrough Test Migration and Opposite Fixtures

Phase 71F is complete as a test and documentation cleanup phase. It migrates fallthrough-sensitive source-run fixture wording away from the old selected-entry `ENDP` auto-success model, adds explicit opposite fixture coverage for default `code-stream` behavior versus opt-in `stop-at-entry-end`, and documents the inventory requirement that successful examples use explicit `RET`, supported Irvine32 `exit`, or the named compatibility setting.

Runtime/source-run MASM behavior metadata remains Phase 71E - Entry-Procedure Auto-Stop Compatibility Setting. Phase 71F does not change accepted syntax, parser behavior, terminal success/failure behavior, browser settings, source-run JSON schema, source-run output-contract metadata, Program Console output, or Simulator Messages wording. It does include a narrow conformance fix so Irvine32 `exit` is treated as an explicit terminator for procedure-fallthrough diagnostics, matching the already accepted Phase 71E behavior contract.

## Phase 71E - Entry-Procedure Auto-Stop Compatibility Setting

Phase 71E is complete as a runtime/source-run behavior phase. It adds the configurable `entryProcedureEndMode` source-run and browser setting with accepted values `code-stream` and `stop-at-entry-end`, defaulting to `code-stream`. Default `code-stream` preserves the Phase 71C/71D realistic VM code stream: selected-entry `ENDP` is not an implicit successful terminator, `procedureFallthroughPolicy` still governs ordinary procedure-boundary crossings, and `code-fell-off-end` still reports executable code that ends without an explicit terminator.

Opt-in `stop-at-entry-end` terminates successfully only when ordinary sequential execution reaches the selected entry procedure's `ENDP` boundary, including empty selected-entry procedures. It suppresses `procedure-fell-through` only for that stopped selected-entry boundary and does not suppress diagnostics after execution has left the selected entry through `CALL`/`RET`, `JMP`, or another explicit supported path. Root `RET`, helper `CALL`/`RET`, invalid return-token, parser, memory, unsupported-syntax, and source-run schema diagnostics remain independent.

Phase 71E advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-71e-entry-procedure-end-mode-output-contract-v1` because public settings, terminal behavior, rendered Simulator Messages coverage, and source-run JSON serialization changed.

## Phase 71D - Configurable Procedure-Fallthrough Diagnostic Policy

Phase 71D is complete as a runtime/source-run behavior phase. It adds the configurable `procedureFallthroughPolicy` source-run and browser setting with accepted values `off`, `warn`, and `error`, defaulting to `warn`. Ordinary sequential execution that crosses from one procedure range into another now uses the public diagnostic code `procedure-fell-through`. In `warn` mode the simulator emits a non-fatal warning and continues; in `off` mode it suppresses only that procedure-fallthrough diagnostic and continues; in `error` mode it emits a runtime error and stops before executing the first instruction in the destination procedure.

The older public called-helper fallthrough path is mapped into `procedure-fell-through` so the active output does not expose two different public diagnostics for the same procedure-boundary code smell. `code-fell-off-end`, root `RET` behavior, invalid return-token diagnostics, parser diagnostics, memory diagnostics, and unsupported-feature diagnostics remain independent.

Phase 71D advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-71d-procedure-fallthrough-policy-output-contract-v1` because public settings, runtime diagnostics, rendered Simulator Messages, and source-run JSON serialization changed.

## Phase 71C - Baseline Code-Stream Procedure Fallthrough and Code-End Runtime Diagnostic

Phase 71C is complete as a runtime/source-run behavior phase. It removes the pre-71C selected-entry `ENDP` auto-success simplification, starts empty selected entries at the source-order successor executable slot, and lets ordinary VM sequential execution follow the lowered executable code stream across later procedure ranges when no explicit terminator or transfer changes control flow.

Programs that reach the end of the lowered executable code stream without explicit `RET`, virtual Irvine32 `exit`, or another supported terminator now fail with runtime diagnostic `code-fell-off-end`. The diagnostic is simulator-owned and keeps Program Console output separate from Simulator Messages. When the responsible procedure name is exactly `front` under ASCII case-insensitive comparison, source-run appends the notice diagnostic `the-front-fell-off` after the runtime error. Root `RET` behavior remains governed by `rootRetMode`, helper `CALL`/`RET` checked return-token behavior is preserved, and virtual Irvine32 `exit` remains an explicit successful terminator.

Phase 71C advances runtime/source-run MASM behavior metadata and source-run output-contract metadata to `phase-71c-code-stream-falloff-output-contract-v1` because public terminal diagnostics, rendered Simulator Messages, and source-run outcomes changed.

## Phase 71B2 - Source-of-Truth Role Separation and Stale Milestone Context Cleanup

Phase 71B2 is complete as documentation and static-check cleanup only. It clarifies the responsibility split between the full specification and implementation guide, removes stale-prone active milestone-context wording from source-of-truth sections, keeps compact current-status surfaces replacement-based, and extends static documentation checks for stale active milestone-context phrases.

It does not change accepted MASM syntax, rejected syntax categories, parser behavior, VM execution behavior, memory/register/flag behavior, Program Console output, Simulator Messages, source-run JSON schema, diagnostic codes, diagnostic severities, diagnostic source spans, terminal success/failure behavior, runtime/source-run behavior metadata, source-run output-contract metadata, Wasm API behavior, or browser simulator behavior. Runtime/source-run MASM behavior remains Phase 71A - Optional Root RET Strictness Mode.

## Phase 71B1 - Source-Run and Native Control-Flow Subgroup Preflight

Phase 71B1 is complete as test-runner infrastructure only. It adds official native subgroup commands (`--native-parser`, `--native-exec`, `--native-memory-layout`, `--native-diagnostics-policy`, and `--native-control-flow`) and official source-run subgroup commands (`--source-run-core`, `--source-run-diagnostics`, `--source-run-settings`, `--source-run-memory-layout`, and `--source-run-control-flow`). The broad `--native` and `--source-run` groups preserve complete coverage by invoking the implemented subgroups internally. Static tests verify runner-help and `docs/TESTING_GUIDE.md` consistency for the new subgroup families.

The phase was implemented proactively by project-owner direction even though broad `--native` and `--source-run` completed in the active container during preflight. It does not change accepted MASM syntax, rejected syntax categories, parser behavior, VM execution behavior, memory/register/flag behavior, Program Console output, Simulator Messages, source-run JSON schema, diagnostic codes, diagnostic severities, diagnostic source spans, terminal success/failure behavior, runtime/source-run behavior metadata, or source-run output-contract metadata. Runtime/source-run MASM behavior remains Phase 71A - Optional Root RET Strictness Mode.

## Phase 71B - User-Facing Diagnostic Milestone-Wording Cleanup

Phase 71B is complete as diagnostic-copy cleanup only. It rewrites active IMUL, CALL, RET, and RETF user-facing diagnostics so they explain stable simulator boundaries rather than historical milestone numbers. It advances the source-run output-contract identifier because exact diagnostic wording is source-run-visible, updates parser, source-run, rendered Simulator Messages, protocol, and static regression tests, and preserves the Phase 71A runtime/source-run MASM behavior phase. It does not change accepted MASM syntax, rejected syntax categories, parser recovery behavior, VM execution behavior, memory/register/flag behavior, Program Console output, source-run JSON schema, diagnostic codes, diagnostic severities, diagnostic source spans, or terminal success/failure behavior.

## Phase 71A1 - Diagnostic Test Runner Subgroup Decomposition

Phase 71A1 is complete as test-runner infrastructure only. It adds official diagnostic subgroup commands for native diagnostic JSON checks and rendered Simulator Messages fixture families, keeps the broad `--diagnostics` group as complete diagnostic coverage, and adds static runner/help/documentation and subgroup-inventory checks. It does not change accepted MASM syntax, parser behavior, VM execution behavior, memory behavior, Irvine32 behavior, Wasm API behavior, browser UI behavior, source-run JSON schema, diagnostic codes, diagnostic severity, diagnostic source spans, diagnostic text, rendered Simulator Messages output, Program Console output, runtime/source-run behavior metadata, or source-run output-contract metadata. Runtime/source-run MASM behavior remains Phase 71A - Optional Root RET Strictness Mode.

## Concise milestone ledger

- Phase 73 implements source-level `leave`/`LEAVE` as validation-first frame teardown shorthand, advances runtime/source-run metadata and source-run output-contract metadata, preserves flags, exposes no public `memoryChanges` rows, and preserves no-partial-mutation behavior on failed checked `[EBP]` reads.
- Phase 72A implements source-level 32-bit `PUSH`/`POP` stack transfers and exposes successful source-level PUSH stack writes and POP memory-destination writes as public `memoryChanges`.
- Phase 72 adds configurable `callDepthLimit` handling for direct user-procedure `CALL` chains, reports `invalid-call-depth-limit` before execution for invalid settings, reports `call-depth-exceeded` before rejected over-limit `CALL` mutation, advances runtime/source-run metadata and source-run output-contract metadata, and intentionally emits no call-trace metadata.
- Phase 71F migrates fallthrough-sensitive fixtures and adds opposite fixtures while preserving Phase 71E runtime/source-run behavior metadata.
- Phase 71D adds configurable `procedureFallthroughPolicy` handling for ordinary procedure-boundary fallthrough, maps the older helper-fallthrough public path to `procedure-fell-through`, and advances runtime/source-run metadata and source-run output-contract metadata.
- Phase 71C implements baseline VM code-stream fallthrough across procedure boundaries, reports `code-fell-off-end` when executable code ends without explicit terminator, adds the diagnostic-only `the-front-fell-off` notice, and advances runtime/source-run metadata and source-run output-contract metadata.
- Phase 71B2 separates source-of-truth responsibilities, removes stale-prone active milestone-context wording, keeps status surfaces compact, extends static documentation checks, and preserves Phase 71A runtime/source-run MASM behavior and the Phase 71B source-run output-contract metadata.
- Phase 71B1 adds official source-run and native subgroup runner commands, keeps broad source-run/native coverage complete through those subgroups, and preserves Phase 71A runtime/source-run MASM behavior and the Phase 71B source-run output-contract metadata.
- Phase 71B removes stale milestone-number wording from active user-facing IMUL, CALL, RET, and RETF diagnostics, advances source-run output-contract metadata for the visible wording change, and preserves Phase 71A runtime/source-run MASM behavior.
- Phase 71A1 adds official diagnostic subgroup runner commands and static consistency checks while preserving Phase 71A runtime/source-run MASM behavior and source-run output-contract metadata.
- Phase 71A implements optional `rootRetMode = "strict-call-frame"` rejection for selected-entry root `RET` while preserving MASM32-compatible root `RET` default success, called non-entry procedure fallthrough diagnostics, and helper `RET` checked return-token behavior.
- Phase 71 implements selected-entry root `RET` termination success and called non-entry procedure fallthrough diagnostics while preserving helper `RET` checked return-token behavior.
- Phase 70B is documentation/static-test cleanup only. It aligns the canonical spec, guide, active status docs, historical navigation, and static documentation checks after Phase 70A while preserving Phase 70 runtime/source-run MASM behavior metadata and the then-current source-run output-contract token.
- Phase 70A tightened runtime metadata mismatch checks by default as a protocol/artifact compatibility corrective phase and did not change MASM source behavior.
- Phase 70 implements plain near helper `RET` with checked pseudo-EIP return-token reads, return-token validation, ESP pop-on-success, and `invalid-return-address` for readable invalid tokens while preserving root RET, RET imm16, source-level stack instructions, frames, and Irvine32 routine dispatch as future work at that time. Phase 71 has since implemented selected-entry root RET success.
- Phase 69C is artifact/test-infrastructure cleanup only. It adds separate source-run output-contract metadata and protocol/rendering/static tests for stale browser/Wasm artifact detection while preserving Phase 69 runtime/source-run behavior metadata and existing broad runner groups.
- Phase 69B is output/message-ordering cleanup only. It adds final-register display grouping and startup-first Simulator Messages ordering without changing runtime/source-run MASM behavior.
- Phase 69A is documentation/static-check cleanup only. It made the then-active source-of-truth spec/guide revision clearer without changing runtime/source-run MASM behavior beyond Phase 69.
- Phase 69 implements direct user-procedure `CALL` to user `PROC` entries with checked internal pseudo-EIP return-token stack writes and target transfer in its historical scope while preserving RET and Irvine32 routine calls as future work at that time. Phase 70 has since implemented plain near helper RET. Current public source-run JSON does not expose the implicit CALL return-token stack write as a visible `memoryChanges` row.
- Phase 68B displays `EIP` as derived pseudo-code-address control state, rejects source-level `EIP` operands and definitions, and preserves direct supported `ESP` writes.
- Phase 68 adds parser metadata and a classifier that Phase 69 now uses for direct user-procedure `CALL`; future INVOKE phases may reuse the same target-classification foundation.
- Phase 67A corrects source-run selected-entry startup and selected-entry procedure-boundary fallthrough without implementing CALL, RET, stack mutation, or Irvine32 routine dispatch.
- Phase 67 adds validation harness coverage for existing arithmetic, branch, and instruction-count watchdog behavior without changing runtime/source-run MASM behavior.
- Phase 66 implements executable unsigned relational conditional jumps: `ja`, `jnbe`, `jae`, `jnb`, `jb`, `jnae`, `jbe`, and `jna` direct label branches. These consume the required unsigned-comparison flags through the undefined-flag-use policy, preserve registers/memory/modeled flags/Program Console output/memory-change rows, and respect `instructionLimit`.
- Phase 65 implements executable signed relational conditional jumps: `jl`, `jnge`, `jle`, `jng`, `jg`, `jnle`, `jge`, and `jnl` direct label branches. These consume the required signed-comparison flags through the undefined-flag-use policy, preserve registers/memory/modeled flags/Program Console output/memory-change rows, and respect `instructionLimit`.
- Phase 64D adds memory-change source attribution to successful memory-change rows, including the one-based source line and preserved source text when available, without adding MASM syntax or changing runtime semantics.
- Phase 64C expands final register display by keeping the EFLAGS parent row and adding subordinate CF, ZF, SF, and OF child rows without adding new flags, flag semantics, diagnostics, or MASM syntax.
- Phase 64B corrects rendered Simulator Messages grouping for startup notices, runtime diagnostics, and successful completion without adding new syntax or source-run status fields.
- Phase 64A corrects source-run planned-read coverage for existing memory-reading instructions without adding new syntax. Memory-destination read-modify-write instructions now participate in `uninitialized-read` warning and strict/error policy before consuming bytes or committing write-back.
- Phase 64 implements executable equality conditional jumps: `je`, `jz`, `jne`, and `jnz` direct label branches. These consume `ZF` through the undefined-flag-use policy, preserve registers/memory/modeled flags, and respect `instructionLimit`.
- Phase 63 implements `cmp` memory operand forms. CMP memory comparisons read through checked helpers and planned-read validation, update modeled subtraction flags only, preserve operands, and produce no memory-change rows.
- Phase 62 implements `cmp` register/register and register/immediate forms. CMP updates modeled subtraction flags only, preserves operands, and produces no memory-change rows.
- Phase 61E rejects simulator-recognized MASM reserved words as user-defined data symbols, numeric equates, code labels, and procedure names. Reserved-word matching is case-insensitive and independent of `OPTION CASEMAP`; `OPTION CASEMAP:NONE` does not enable reserved-word identifiers. `OPTION NOKEYWORD` remains unsupported until a later explicit keyword-control phase.
- Phase 61D documents and tests parser/source-run capacity behavior. Capacity diagnostics such as `token-capacity-exceeded`, `source-text-capacity-exceeded`, `code-label-capacity-exceeded`, and `data-capacity-exceeded` are pre-runtime source-run failures, not runtime `instructionLimit` failures. Runtime/source-run MASM behavior metadata remains Phase 61.
- Phase 61C clarifies that Phase 61 direct-JMP runtime execution and later debugger/editor behavior are separate systems. Preserving branch source metadata and lowered target metadata does not implement debugger stepping, breakpoint binding, editor source navigation, current-instruction highlighting, CodeMirror gutter behavior, or branch-target editor highlighting. Runtime/source-run MASM behavior metadata remains Phase 61.
- Phase 61B clarifies that Phase 61 direct-JMP runtime execution is governed by the Phase 59 instruction-count watchdog only. Active-time watchdog behavior is not part of Phase 61, Phase 61A, Phase 61B, or Phase 61C; Phase 200 - Active Time Watchdog and Worker Responsiveness remains its owner. Runtime/source-run MASM behavior metadata remains Phase 61.
- Phase 0 through Phase 7 established the static browser scaffold, C99 core boundary, CPU/flag/memory basics, minimal IR/executor, lexer/parser path, and browser source-run path.
- Phase 8 through Phase 14 implemented `.data`, symbols, constant symbol offsets, `PTR` overrides, register-indirect operands, `TYPE`, `LENGTHOF`, `SIZEOF`, and character literal behavior.
- Phase 15 through Phase 17 improved unsupported-feature classification, lexer/parser diagnostic surfacing, and multi-diagnostic recovery.
- Phase 18 through Phase 30 added signed integer data declarations, sign/zero extension, selected unary/binary instructions, MASM compatibility directives, `.DATA?`, `.CONST`, numeric equates, extended constant expressions, and nested `DUP`.
- Phase 31 through Phase 42 added native/Node diagnostic rendering tests, memory-layout policy infrastructure, automatic/randomized layout options for tests/configuration, CASEMAP correction, object maps, validation modes, uninitialized-origin tracking, the Irvine32 virtual registry, and the virtual `exit` terminator.
- Phase 43 through Phase 50 added `inc`, `dec`, `and`, `or`, `xor`, `not`, `shl`, `sal`, `shr`, `sar`, `rol`, and `ror`.
- Phase 50A through Phase 50B added modeled-flag validity metadata and configurable undefined-flag-use diagnostics for existing flag consumers.
- Phase 51 added post-30 smoke-harness validation coverage.
- Phase 52 added `lea` effective-address computation.
- Phase 52A improved signed register and memory value display formatting.
- Phase 53 through Phase 55 added unsigned `mul`, memory-validation policy clarification/correction, section-capacity and section-image validation modes, default teaching diagnostics, compatibility notices, browser diagnostic settings, one-operand signed `imul`, and two-/three-operand signed `imul` forms.
- Phase 56 added unsigned `div`.
- Phase 56A improved test-runner decomposition and assistant verification ergonomics.
- Phase 56B cleaned live user-facing diagnostic and current-status wording.
- Phase 57 added signed `idiv` and advanced runtime/source-run MASM behavior phase metadata to Phase 57.
- Phase 57-CORR1 clarified fatal `region-boundary-crossing` diagnostics for cross-region accesses intersecting protected `.CONST` storage without advancing runtime/source-run MASM behavior metadata.
- Phase 57-CORR2 corrected compact negative register-indirect displacement parsing, such as `[eax-4]`, without advancing runtime/source-run MASM behavior metadata.
- Phase 57A rewrote the README into a concise landing page and moved preserved milestone history into this document.
- Phase 57B extracted long-form build and development guidance into [`BUILDING_AND_DEVELOPMENT.md`](BUILDING_AND_DEVELOPMENT.md), updated this milestone-history navigation layer, and kept runtime/source-run MASM behavior at Phase 57 - Signed IDIV.
- Phase 57C added a behavior-preserving diagnostic-policy registry skeleton for optional teaching diagnostics and kept runtime/source-run MASM behavior at Phase 57 - Signed IDIV.
- Phase 57D routes existing configurable diagnostic-policy lookup through the registry or compatibility adapters while preserving existing user-visible behavior and keeping runtime/source-run MASM behavior at Phase 57 - Signed IDIV.
- Phase 57E activates and documents the non-fatal startup-state notice while preserving deterministic startup values and keeping runtime/source-run MASM behavior at Phase 57 - Signed IDIV.
- Phase 57F adds opt-in deterministic seeded startup for general-purpose registers and modeled flags while preserving memory bytes, uninitialized-origin metadata, default zero startup, and existing browser UI behavior without adding browser UI controls.
- Phase 57G adds opt-in deterministic seeded visible bytes for uninitialized-origin storage while preserving metadata, default zero startup, initialized data bytes, and existing browser UI behavior without adding browser UI controls.
- Phase 57H adds final-register `[unchanged]` display markers for canonical register families that the executed program did not write, while keeping runtime/source-run MASM behavior metadata at Phase 57G.
- Phase 57I accepts `.CONST ?` and `.CONST DUP(?)` as read-only uninitialized-origin storage, advances runtime/source-run MASM behavior metadata to Phase 57I, and is followed by Phase 57J configurable declaration diagnostics.
- Phase 57J activates configurable `const-uninitialized-storage` declaration diagnostics for `.CONST ?` and `.CONST DUP(?)`, advances runtime/source-run MASM behavior metadata to Phase 57J, and preserves `.CONST` storage semantics.
- Phase 57K locks the v1 policy that `.code` memory access is unsupported and that MASM segment/group names are not aliases for simulator regions, while keeping runtime/source-run MASM behavior at Phase 57J.
- Phase 57L implements mandatory `.code` memory-access diagnostics and advances runtime/source-run MASM behavior metadata to Phase 57L.
- Phase 57M implements targeted `unsupported-segment-symbol` parser/source-run diagnostics for MASM/object/linker segment and group names and advances runtime/source-run MASM behavior metadata to Phase 57M.
- Phase 57N audits and hardens existing zero-operand `nop` behavior, updates rejected operand-form diagnostics, and keeps runtime/source-run MASM behavior metadata at Phase 57M.
- Phase 57O accepts selected MASM-compatible NOP register and explicit-width memory-looking encoding-operand forms as IR-level no-ops and advances runtime/source-run MASM behavior metadata to Phase 57O.
- Phase 57P recognizes host/path-like `INCLUDE` directive tails, emits one unsupported include diagnostic per recognized host/path-like directive instead of repeated path-character lexer errors, preserves supported virtual includes, does not implement host file loading, and advances runtime/source-run MASM behavior metadata to Phase 57P.
- Phase 57Q recognizes `INCLUDELIB` library directive tails, emits linker/import-library non-goal diagnostics instead of generic parser or lexer diagnostics, does not implement library loading, object files, imports, PE loading, a linker, WinAPI execution, or external routine execution, and advances runtime/source-run MASM behavior metadata to Phase 57Q.
- Phase 57R adds unsupported `INVOKE`, `ADDR`, MASM32 runtime-style, CRT-style, and WinAPI/external routine diagnostics, refuses execution for those source lines, preserves virtual Irvine32 `exit`, does not implement invocation lowering, stack setup, calling conventions, WinAPI, CRT, MASM32 runtime, PE, linker, or external routine execution, and advances runtime/source-run MASM behavior metadata to Phase 57R.
- Phase 57S adds targeted unsupported high-level MASM flow diagnostics for `.IF`, `.ELSE`, `.ENDIF`, `.ELSEIF`, `.WHILE`, `.ENDW`, `.REPEAT`, `.UNTIL`, `.UNTILCXZ`, `.BREAK`, and `.CONTINUE`, suppresses safe body cascades, and advances runtime/source-run MASM behavior metadata to Phase 57S.
- Phase 57T adds realistic MASM32 playground-program diagnostic-recovery smoke fixtures and documentation while preserving runtime/source-run MASM behavior metadata at Phase 57S.
- Phase 58 adds ordinary and procedure-entry code-label metadata, duplicate/conflict label diagnostics, and runtime/source-run MASM behavior metadata advancement to Phase 58 while preserving future branch/procedure behavior as unsupported.
- Phase 59 adds a source-run/test-facing executed-instruction watchdog through `instructionLimit`, emits `instruction-limit-exceeded`, and advances runtime/source-run MASM behavior metadata to Phase 59.
- Phase 60 parses, classifies, and lowers direct `jmp label` forms to executable code labels or procedure-entry labels, emits `branch-runtime-deferred` when a lowered direct JMP is reached, and keeps runtime branch transfer deferred to Phase 61.
- Phase 61 executes already-lowered direct `jmp label` forms by transferring to the resolved VM instruction index, counting JMP as one executed instruction, preserving modeled flags, and producing no memory-change row.
- Phase 61A hardens direct-JMP accounting/status tests and documentation while keeping runtime/source-run MASM behavior metadata at Phase 61.



## Phase 68A - Stack Runtime Initialization and ESP Startup Contract

Repository/archive milestone:
Phase 68A - Stack Runtime Initialization and ESP Startup Contract

Runtime/source-run MASM behavior phase:
Phase 68A - Stack Runtime Initialization and ESP Startup Contract

Phase 68A initializes `ESP` from the active stack region when a program is loaded. The empty-stack value is the first address past the high end of the selected stack region. Later stack-writing phases must compute `ESP - 4` before writing a 32-bit value through checked stack memory.

Implemented behavior:

- default fixed-layout runs initialize `ESP` to `00900000h`, the fixed stack region's exclusive high limit;
- automatic and layout-policy runs initialize `ESP` from the selected stack region metadata rather than from hardcoded executor constants;
- seeded register/flag startup remains deterministic for the other modeled registers and flags while `ESP` uses the stack startup contract;
- `.stack size` remains parser/layout metadata and can influence automatic/layout-policy stack capacity through existing layout behavior;
- Phase 67A selected-entry boundaries and Phase 68 call-target classification remain unchanged.

Non-goals preserved:

- no executable CALL, RET, LEAVE, RET imm16, source-level PUSH/POP, or stack mutation instructions;
- no procedure frames, PROC USES, LOCAL, PROTO, INVOKE, or ADDR behavior;
- no Irvine32 callable routine dispatch beyond the existing virtual `exit` terminator;
- no new stack sizing UI, URL state, layout sizing behavior, heap sizing behavior, or randomized layout behavior.


## Phase 71A - Optional Root RET Strictness Mode

Repository/archive milestone:
Phase 71A - Optional Root RET Strictness Mode

Runtime/source-run MASM behavior phase:
Phase 71A - Optional Root RET Strictness Mode

Phase 71A adds the optional `rootRetMode` setting. The default value is `masm32-compatible`, which preserves Phase 71 selected-entry root `RET` success without reading `[ESP]`, validating a pseudo-EIP return token, mutating `ESP`, creating public `memoryChanges`, or emitting `invalid-address` / `invalid-return-address`. The opt-in `strict-call-frame` value rejects selected-entry root `RET` before any stack read or mutation with `root-ret-disallowed-by-mode`; its message explains that selected-entry `RET` has no caller-supplied return address, avoids repeating the literal strict-mode setting value, and points users to MASM32-compatible root RET mode or the supported Irvine32 `exit` routine. Ordinary helper `RET` preserves Phase 70 checked-memory and return-token validation behavior in both modes. A called non-entry procedure that reaches its `ENDP` boundary without `RET` still emits `non-root-procedure-fell-through`.

Phase 71A updates runtime/source-run phase metadata and the source-run output-contract identifier to `phase-71a-source-run-output-contract-v1`, updates active status documentation, exposes the setting through browser/source-run settings, and adds native, source-run, rendered-message, web/settings, protocol, and static documentation regression tests.

## Phase 71 - Root Procedure Termination Semantics

Repository/archive milestone:
Phase 71 - Root Procedure Termination Semantics

Runtime/source-run MASM behavior phase:
Phase 71 - Root Procedure Termination Semantics

Phase 71 implements selected-entry root `RET` termination and called non-entry procedure fallthrough diagnostics. A no-operand `ret`/`RET` in the selected entry procedure terminates successfully when no helper return is pending, without reading `[ESP]`, validating a pseudo-EIP return token, mutating `ESP`, creating public `memoryChanges`, or emitting `invalid-address` / `invalid-return-address`. Ordinary helper `RET` preserves Phase 70 checked-memory and return-token validation behavior. A called non-entry procedure that reaches its `ENDP` boundary without `RET` emits `non-root-procedure-fell-through`.

Phase 71 also updates runtime/source-run phase metadata and the source-run output-contract identifier to `phase-71-source-run-output-contract-v1`, updates active status documentation, and adds native, source-run, rendered-message, protocol, and static documentation regression tests.

## Phase 70B - Canonical Documentation Alignment and Compatibility Test Matrix Cleanup

Phase 70B is a documentation/static-test cleanup phase inserted after Phase 70A and before Phase 71. It makes the Phase 70A compatibility matrix explicit, removes stale exact future phase-number wording from the full spec, clarifies active current-status blocks, and preserves the rule that historical reports are evidence rather than canonical source-of-truth. At Phase 70B completion, runtime/source-run MASM behavior metadata remained Phase 70 - RET Execution and Return Address Validation, and Phase 71 - Root Procedure Termination Semantics remained both the next canonical guide phase and the next runtime/source-run MASM behavior phase. Phase 71A has since advanced the current runtime/source-run behavior phase.

Phase 70B does not change accepted MASM syntax, parser behavior, VM behavior, instruction behavior, Irvine32 behavior, memory behavior, source-run output shape, source-run output-contract token, Program Console output, or Simulator Messages behavior.

## Phase 70 - RET Execution and Return Address Validation

Repository/archive milestone:
Phase 70 - RET Execution and Return Address Validation

Runtime/source-run MASM behavior phase:
Phase 70 - RET Execution and Return Address Validation

Phase 70 implements plain near `ret`/`RET` with no operands. The VM reads a DWORD pseudo-EIP return token from `[ESP]` through the same checked memory helpers used by explicit memory reads, validates that the token maps to a loaded executable VM instruction, increments `ESP` by 4 only after validation succeeds, and transfers control to the validated instruction. The successful implicit return-token read is internal control-flow state and remains absent from public source-run `memoryChanges` rows.

Implemented behavior:

- parser/lowering accepts plain `ret` and `RET` as a zero-operand executable instruction;
- parser/lowering rejects `ret imm16`, register operands, memory operands, and `retf` as future or non-goal forms;
- executor RET reads `[ESP]` as a checked DWORD and participates in planned-read policy validation;
- executor RET validates that the read token is an aligned pseudo-EIP for a loaded executable instruction;
- successful RET increments `ESP` by 4, transfers control, and preserves modeled flags and flag-validity metadata;
- failed checked reads leave `ESP`, instruction pointer, flags, memory, Program Console output, terminal status, and public memory-change rows unchanged;
- readable invalid tokens emit `invalid-return-address` and leave `ESP`, instruction pointer, flags, memory, Program Console output, terminal status, and public memory-change rows unchanged.

Non-goals preserved:

- no root RET termination success;
- no non-entry procedure fallthrough diagnostics;
- no call-depth or active-call-frame validation;
- no `RET imm16`, `RETF`, `LEAVE`, source-level `PUSH`/`POP`, procedure frames, `PROC USES`, `LOCAL`, `PROTO`, `INVOKE`, or `ADDR`;
- no Irvine32 callable routine dispatch beyond the existing virtual `exit` terminator.


## Phase 69C - Wasm Output-Contract Compatibility and Test Runner Decomposition

Phase 69C - Wasm Output-Contract Compatibility and Test Runner Decomposition

Phase 69C is artifact/test-infrastructure cleanup only. It adds the C source-run JSON field `sourceRunOutputContract` with value `phase-71a-source-run-output-contract-v1`. The browser protocol expects this value separately from runtime/source-run MASM behavior metadata and renders `stale-wasm-output-contract` when a loaded artifact omits the field or reports a different output contract. Stale runtime behavior metadata and stale output-contract metadata are reported distinctly.

Phase 69C preserves the existing broad runner flags (`--structure`, `--native`, `--source-run`, `--web`, `--diagnostics`, `--protocol`, `--static`, and `--all`) and does not add subgroup flags. Documentation now states that preferred subgroup families remain future guidance unless the runner help exposes those flags. Phase 69C does not change parser behavior, VM instruction semantics, supported MASM syntax, source-level diagnostics, Program Console output, register values, memory values, memory-change rows, or the runtime/source-run MASM behavior phase.

## Phase 69B - Register Display Grouping and Startup Diagnostic Ordering

Repository/archive milestone:
Phase 69B - Register Display Grouping and Startup Diagnostic Ordering

Runtime/source-run MASM behavior phase:
Phase 69 - Direct CALL to User Procedures

Latest output/message-ordering cleanup phase:
Phase 69B - Register Display Grouping and Startup Diagnostic Ordering

Phase 69B updates display/message ordering only. Final register display now separates general registers, index registers, stack/frame registers, and control/modeled flag state with exact `-------------------------------------------------------------------` major divider rows, and separates adjacent parent register families inside those groups with exact `       |` spacer rows. Simulator Messages now serialize and render `startup-state-notice` first whenever execution begins and startup notices are enabled, followed by nonfatal pre-execution diagnostics, runtime diagnostics, and final `execution-complete` on success.

Phase 69B does not change parser behavior, VM instruction semantics, supported MASM syntax, diagnostic codes, Program Console output, memory-change rows, source-run protocol field names, or the runtime/source-run MASM behavior phase. `RET`, source-level `PUSH`/`POP`, procedure frames, Irvine32 routine dispatch, and unsupported CALL target forms remain future-owned or out-of-scope as documented by the canonical specification and guide.


## Phase 69A - Documentation and Static-Check Cleanup After Direct CALL

Repository/archive milestone:
Phase 69A - Documentation and Static-Check Cleanup After Direct CALL

Runtime/source-run MASM behavior phase:
Phase 69 - Direct CALL to User Procedures

Phase 69A updates active documentation and static documentation checks only. It removes README next-phase recommendation wording, separates future simulator work from permanent external/API non-goals, documents rejected CALL target forms as rejected rather than promised future work, clarifies the Phase 69 transitional CALL boundary, defines the executable-successor rule for Phase 70 RET planning, clarifies that uninitialized-read warnings are orthogonal to region-only address/range validation, and clarifies that `SUPPORTED_SYNTAX.md` is a tested current-reference document rather than an independent canonical override.

Phase 69A does not change parser behavior, VM behavior, source-run behavior, browser protocol, diagnostic codes, Wasm API behavior, or supported MASM syntax.

## Phase 69 - Direct CALL to User Procedures

Repository/archive milestone:
Phase 69 - Direct CALL to User Procedures

Runtime/source-run MASM behavior phase:
Phase 69 - Direct CALL to User Procedures

Phase 69 implements the first executable procedure-call behavior: direct near `call ProcedureName` to user procedure entries declared with `ProcedureName PROC`.

Implemented behavior:

- parser/lowering accepts direct user-procedure CALL targets only when the target resolves to a user `PROC` entry under the active user-symbol `CASEMAP` policy at the call site;
- ordinary labels, data symbols, numeric equates, unknown identifiers, external/API names, recognized Irvine32 routine names, register targets, memory targets, `OFFSET` targets, and malformed target expressions produce structured diagnostics rather than executable CALL behavior;
- runtime CALL computes the return token from the Phase 68B pseudo-EIP of the lowered instruction immediately after the call;
- runtime CALL computes the stack destination as `ESP - 4`, validates and writes the 32-bit return token through checked VM memory helpers, updates `ESP`, records the memory/register changes, and transfers to the target procedure entry;
- failed CALL stack writes use the central memory diagnostic path and stop before committing the transfer, `ESP` update, memory write, Program Console output, or completion message;
- CALL preserves modeled flags and flag-validity metadata;
- valid stack writes are not rejected solely because optional declared-object teaching validation is enabled and stack memory is not a declared data object.

Non-goals preserved:

- no RET, root RET, LEAVE, RET imm16, source-level PUSH/POP, or procedure-frame behavior;
- no Irvine32 routine dispatch for `call WriteString`, `call ReadInt`, `call ExitProcess`, or similar names;
- no PROC USES, LOCAL, PROTO, INVOKE, ADDR, argument handling, call-depth diagnostics, call traces, stack-pointer warning diagnostics, code memory, native x86 instruction encoding, PE loading, or linker/RVA modeling.


## Phase 68B - EIP Pseudo-Code Address Display and Source-Operand Restrictions

Status:
Completed after Phase 68A.

Phase 68B is a corrective, non-renumbering milestone. It does not change Phase 69 or later phase numbers.

Implemented behavior:

- displayed `EIP` becomes a derived VM pseudo-code address rather than a source-writable general-purpose register;
- pseudo-EIP uses `VM_CODE_BASE + lowered_instruction_index * VM_CODE_STRIDE`, initially `00401000h + index * 4`;
- pseudo-EIP values are control-flow tokens, not VM data-memory addresses, not PE RVAs, not linker addresses, and not native x86 instruction byte offsets;
- `EIP` is rejected as an ordinary source operand, NOP encoding operand, and memory-address base/index; `IP` remains unsupported unless the parser already recognizes it;
- direct `ESP` writes such as `mov esp, 1` remain legal when the instruction form is otherwise supported;
- invalid `ESP` values are diagnosed only when a later implicit stack operation performs a checked stack memory access;
- Phase 69 direct CALL can use pseudo-EIP return tokens from the accepted Phase 68B contract;
- Phase 70 RET can later validate pseudo-EIP return tokens after Phase 69 is complete.

Non-goals preserved:

- no CALL or RET execution in Phase 68B;
- no source-level PUSH or POP execution;
- no stack-overflow, stack-underflow, or suspicious-stack-pointer diagnostics;
- no real x86 instruction-length decoding;
- no executable code memory, PE loading, object linking, or linker/RVA modeling.


## Phase 68 - Call Target Classification and Procedure Entry Metadata

Repository/archive milestone:
Phase 68 - Call Target Classification and Procedure Entry Metadata

Runtime/source-run MASM behavior phase:
Phase 68 - Call Target Classification and Procedure Entry Metadata

Phase 68 adds parser-owned metadata for user procedure target classification. It records procedure entries as procedure-entry targets distinct from ordinary code labels and exposes a documented classifier that Phase 69 now uses for direct user-procedure `CALL`; later `INVOKE` work may reuse that foundation.

Implemented behavior:

- procedure declarations preserve procedure-entry metadata, source location, and source span information;
- future-call target classification distinguishes user procedure entries, ordinary executable code labels, data symbols, numeric equates, supported/planned/unsupported Irvine32 registry names, external/API/linker non-goal names, reserved words, unknown symbols, and malformed target expressions;
- Irvine32 classification reuses the central virtual Irvine32 registry instead of creating a separate call-target table;
- user procedure and label names continue to follow the active `OPTION CASEMAP` policy;
- mnemonics, directives, registers, data types, `PTR` names, virtual include names, and recognized Irvine32 names remain case-insensitive reserved names;
- duplicate procedure names and procedure collisions with labels, data symbols, equates, and reserved words continue to produce structured diagnostics;
- Phase 67A selected-entry startup and selected-entry procedure-boundary fallthrough remain unchanged.

Non-goals preserved:

- no executable CALL or INVOKE behavior;
- no RET, root RET, LEAVE, RET imm16, stack mutation, source-level PUSH/POP, or procedure-frame behavior;
- no Irvine32 callable routine dispatch beyond the existing virtual `exit` terminator;
- no Windows API execution, PE loading, object-file linking, import-library behavior, or host filesystem include loading.


## Phase 67A - Entry Procedure Runtime Boundary and END Entry Selection

Repository/archive milestone:
Phase 67A - Entry Procedure Runtime Boundary and END Entry Selection

Runtime/source-run MASM behavior phase:
Phase 67A - Entry Procedure Runtime Boundary and END Entry Selection

Phase 67A makes `END entryName` authoritative for source-run startup and selected-entry procedure fallthrough.

Implemented behavior:

- source-run starts at the first executable instruction inside the selected `END entryName` procedure;
- helper procedures before the selected entry procedure do not execute by source order;
- helper procedures after the selected entry procedure do not execute by ordinary fallthrough;
- an empty selected entry procedure completes successfully without executing another procedure;
- ordinary fallthrough at the selected entry `ENDP` boundary emits normal completion;
- existing direct branch behavior to labels and procedure-entry labels remains executable;
- existing Irvine32 virtual `exit` behavior remains successful from inside the selected entry procedure;
- runtime/source-run phase metadata advances to Phase 67A.

Non-goals preserved:

- no CALL or RET behavior;
- no stack mutation or procedure-frame behavior;
- no Irvine32 callable routine dispatch beyond the existing virtual `exit` terminator;
- no Phase 68 call-target classification.

## Phase 67 - Arithmetic, Branch, and Watchdog Integration Harness

Repository/archive milestone:
Phase 67 - Arithmetic, Branch, and Watchdog Integration Harness

Runtime/source-run MASM behavior phase:
Phase 66 - Unsigned Relational Conditional Jumps

Phase 67 is a validation-only test harness and status-documentation milestone.

Implemented validation coverage:

- combined `cmp` followed by equality, signed relational, and unsigned relational Jcc in one source-run program;
- direct `jmp` and Jcc regression coverage through the existing branch target classifier;
- an instruction-count watchdog fixture using already implemented `cmp`, Jcc, and `jmp` behavior;
- arithmetic fault no-partial-mutation coverage for `mul`, `imul`, `div`, and `idiv` paths;
- no-memory-change-row coverage for existing `lea`, `cmp`, `mul`, `imul`, `div`, `idiv`, `jmp`, and Jcc behavior;
- rendered Simulator Messages coverage for a branch-target classifier error, a division runtime error, and an instruction-limit runtime error.

No runtime/source-run MASM behavior changed in this phase. Runtime/source-run metadata remains Phase 66.

Not implemented in this phase:

- no MASM syntax changes;
- no parser behavior changes;
- no VM behavior changes;
- no executor behavior changes;
- no Wasm API behavior changes;
- no browser UI behavior changes;
- no worker protocol behavior changes;
- no new diagnostic codes or rendered-message categories;
- no Program Console behavior changes;
- no source-run JSON behavior changes;
- no future branch, loop, stack, procedure, Irvine32, debugger, active-time watchdog, macro, WinAPI, PE/linker, or host-filesystem behavior.

## Phase 66A - Current-Status Documentation De-Cluttering

Repository/archive milestone:
Phase 66A - Current-Status Documentation De-Cluttering

Runtime/source-run MASM behavior phase:
Phase 66 - Unsigned Relational Conditional Jumps

Phase 66A is a documentation-only corrective milestone.

It cleans current-status documentation so README, supported syntax, and build/development docs remain concise orientation surfaces instead of rolling milestone reports.

Implemented documentation changes:

- README current-status text was replaced rather than appended.
- README current-scope text was shortened to feature categories and documentation links.
- `docs/SUPPORTED_SYNTAX.md` opening status text was replaced rather than appended.
- `docs/SUPPORTED_SYNTAX.md` detailed syntax reference sections remained the owner of accepted/rejected syntax detail.
- `docs/BUILDING_AND_DEVELOPMENT.md` was kept focused on build, serve, environment, and test workflows.
- Historical detail remains in this milestone history document and milestone reports.
- The spec and guide now include explicit anti-clutter rules for current-status surfaces.

No runtime/source-run MASM behavior changed in this phase.

Not implemented in this phase:

- no MASM syntax changes;
- no parser behavior changes;
- no VM behavior changes;
- no executor behavior changes;
- no Wasm API behavior changes;
- no browser UI behavior changes;
- no worker protocol behavior changes;
- no diagnostic code or rendered-message behavior changes;
- no Program Console behavior changes;
- no source-run JSON behavior changes;
- no runtime/source-run MASM behavior metadata advancement.

Future assistants should preserve this distinction: current-status summaries are replaced in place, while milestone history accumulates here.

## Corrective audit note after Phase 66

A post-Phase-66 audit identified roadmap wording risks in future stack/procedure phases. This note is documentation-only and does not change the Phase 66 implementation state.

Current accepted implementation state remains:

```text
Repository/archive milestone:
Phase 66 - Unsigned Relational Conditional Jumps

Runtime/source-run MASM behavior phase:
Phase 66 - Unsigned Relational Conditional Jumps
```

Future roadmap clarifications to preserve:

- Procedure names are user-defined symbols. They are case-insensitive by default under `CASEMAP:ALL` behavior and exact-case only after `OPTION CASEMAP:NONE`.
- `OPTION CASEMAP:NONE` does not make instructions, directives, registers, virtual include names, or recognized Irvine32 routine names case-sensitive.
- Recognized Irvine32 routine and terminator names remain reserved and must be classified through the central virtual Irvine32 registry or a documented wrapper.
- Phase 68 - Call Target Classification and Procedure Entry Metadata is metadata-only and must not execute CALL, RET, INVOKE, stack mutation, Irvine routine dispatch, or root termination.
- Phase 69 - Direct CALL to User Procedures owns only direct user-procedure CALL mechanics and implicit VM return-token stack writes. It does not own RET, root termination, source-level PUSH/POP, or Irvine32 routine calls.
- If later source-run fixtures use source-level `push` or `pop`, the guide must provide a prior non-renumbering PUSH/POP phase, such as Phase 72A.
- LEAVE and RET imm16 phases must not implement PUSH/POP incidentally.

This note does not authorize implementation of future stack/procedure features before their selected target milestones.

## Detailed milestone report references

Detailed milestone reports remain historical evidence and do not replace the canonical specification or implementation guide.


## Phase 61E - Reserved Word Symbol Diagnostics

Phase 61E is a parser/source-run MASM behavior phase. It rejects simulator-recognized MASM reserved words when they are declared as user-defined data symbols, numeric equates, code labels, or procedure names. The primary diagnostic is `reserved-word-symbol`, points at the declaration token where possible, and prevents the rejected declaration from entering user-symbol tables. Reserved-word matching is case-insensitive and separate from `OPTION CASEMAP`; `OPTION CASEMAP:NONE` does not make reserved words usable as symbols. `OPTION NOKEYWORD` remains unsupported until a later explicit keyword-control phase. Runtime/source-run MASM behavior metadata advances to Phase 61E - Reserved Word Symbol Diagnostics. Phase 62 - CMP Register and Immediate Forms, Phase 63 - CMP Memory Operand Forms, and Phase 64 - Equality Conditional Jumps are implemented; later relational and loop control-flow phases remain future work.

## Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening

Phase 61D is a documentation, static-check, and regression-test cleanup phase for source-run capacity behavior. It documents that lexer token capacity, parser diagnostic capacity, instruction/source-text buffers, data symbols, code labels, data image bytes, and source-run JSON/result buffers are bounded. Simulator-controlled capacity failures should produce structured diagnostics and rendered Simulator Messages where possible, should not write Program Console output, should not emit `execution-complete`, and should not be confused with Phase 59 runtime `instructionLimit` failures. Runtime/source-run MASM behavior metadata remains Phase 61 - Direct JMP Runtime Execution. Phase 62 - CMP Register and Immediate Forms remains the next ordinary runtime instruction milestone after the post-61 cleanup chain.

## Phase 61C - Branch Debugger Dependency Cleanup

Phase 61C is a documentation/static-check cleanup phase for the debugger/editor dependency boundary around direct-JMP runtime execution. It clarifies that Phase 60 branch target labels and Phase 61 direct-JMP runtime transfer preserve metadata that later debugger/editor phases must use for branch/source-map regression coverage, but Phase 61C does not implement debugger stepping, breakpoint creation, breakpoint binding, breakpoint persistence, current-instruction highlighting, editor source navigation, source-map UI behavior, CodeMirror gutter behavior, or branch-target editor highlighting. Runtime/source-run MASM behavior metadata remains Phase 61 - Direct JMP Runtime Execution. No C runtime, worker, UI, settings, protocol, source-run JSON, or Simulator Messages behavior changes were made by Phase 61C. Phase 62 - CMP Register and Immediate Forms remains the next ordinary runtime instruction milestone after the post-61 cleanup chain.

## Phase 61B - Branch Runtime Watchdog Scope Cleanup

Phase 61B is a documentation/static-check cleanup phase for the watchdog boundary around direct-JMP runtime execution. It clarifies that Phase 61 backward direct-JMP loops stop through the Phase 59 - Control-Flow Instruction Limit instruction-count watchdog. Active-time watchdog behavior is not implemented in Phase 61, Phase 61A, or Phase 61B; Phase 200 - Active Time Watchdog and Worker Responsiveness remains the future owner. Runtime/source-run MASM behavior metadata remains Phase 61 - Direct JMP Runtime Execution. No C runtime, worker, UI, settings, protocol, source-run JSON, or Simulator Messages behavior changes were made by Phase 61B.

## Phase 61A - Direct JMP Runtime Accounting and Status Hardening

Phase 61A is a repository/test/documentation hardening phase for behavior already owned by Phase 61. It verifies that committed direct `jmp` instructions are counted in `executedInstructionCount`, normal completion leaves `attemptedNextInstructionIndex` as `null`, instruction-limit failure identifies the blocked next instruction, valid direct JMP never emits `branch-runtime-deferred`, skipped instructions do not mutate state, direct JMP preserves modeled flags and flag-validity metadata, and direct JMP itself creates no memory-change or Program Console output. Runtime/source-run MASM behavior metadata remains Phase 61 - Direct JMP Runtime Execution. Conditional jumps, `loop`, indirect branches, register-target jumps, memory-target jumps, immediate-target jumps, branch-distance/type overrides, stack/procedure execution, active-time watchdog behavior, and debugger/editor branch behavior remain future work.

## Phase 61 - Direct JMP Runtime Execution

Phase 61 executes the direct branch metadata lowered by Phase 60. A valid direct `jmp label` transfers to the resolved VM instruction index, counts as one executed VM instruction, preserves all modeled flags and flag-validity metadata, produces no memory-change row, and respects the Phase 59 instruction-count watchdog. Procedure-entry labels remain direct branch targets only and do not imply `CALL`, `RET`, stack frames, arguments, calling conventions, or procedure invocation semantics. Invalid or malformed branch target metadata reports `invalid-branch-target` without partial mutation. Conditional jumps, `loop`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, branch-distance/type overrides, stack/procedure execution, debugger stepping, breakpoints, and UI label navigation remain future work.

## Phase 60 - Direct JMP Parsing and Target Lowering

Phase 60 parses, classifies, and lowers direct `jmp label` forms when the target is an executable code label or procedure-entry label. The lowered IR records the direct branch target for Phase 61. Runtime branch transfer is not implemented in Phase 60; reaching a lowered direct JMP emits `branch-runtime-deferred`, preserves already-committed state, reports the attempted instruction index, and omits `execution-complete`. Invalid direct JMP targets, including data symbols, equates, Irvine32 symbols, unknown symbols, registers, memory operands, immediates, no-executable labels, and `SHORT`/`NEAR PTR`/`FAR PTR` forms, produce structured diagnostics.

## Phase 59 - Control-Flow Instruction Limit

Phase 59 adds deterministic executed-instruction limit enforcement through the source-run/test-facing `instructionLimit` setting. Runs count completed VM instructions rather than source lines or labels. If the configured limit has been reached and another instruction would be fetched, execution stops before the next instruction, emits `instruction-limit-exceeded`, preserves state from completed instructions, and omits `execution-complete`. Source-run JSON reports `instructionLimit`, `executedInstructionCount`, `attemptedNextInstructionIndex`, and `currentInstructionIndex`. Phase 59 does not implement runtime branch transfer, conditional jumps, `loop`, `call`, `ret`, stack behavior, procedure invocation, debugger stepping, breakpoints, or browser UI controls for the limit.

## Phase 57T - Playground Program Diagnostic-Recovery Smoke Fixtures

Phase 57T adds source-run and rendered Simulator Messages smoke fixtures for realistic unsupported MASM32 playground programs. The fixtures verify concise diagnostics for host include paths, `INCLUDELIB`, `INVOKE`, `ADDR`, external MASM32/CRT/WinAPI routine boundaries, high-level MASM flow, and then-still-unsupported `CALL`, `RET`, and unsigned conditional-jump syntax as it existed in the Phase 57T repository state. Phase 57T is repository/test/documentation work and does not implement host include loading, library linking, invocation lowering, WinAPI execution, high-level-flow lowering, stack/procedure behavior, or branch execution. Runtime/source-run MASM behavior metadata remains Phase 57S.

## Phase 57S - Unsupported High-Level Flow Diagnostics

Phase 57S implements targeted parser/source-run diagnostics for unsupported high-level MASM flow. `.IF`, `.ELSE`, `.ENDIF`, `.ELSEIF`, `.WHILE`, `.ENDW`, `.REPEAT`, `.UNTIL`, `.UNTILCXZ`, `.BREAK`, and `.CONTINUE` are recognized as unsupported high-level flow where encountered. Source containing these diagnostics refuses execution, does not produce Program Console output, and does not lower conditions into labels, branches, or runtime block semantics.

## Phase 57R - Unsupported INVOKE, ADDR, and External Routine Diagnostics

Phase 57R implements targeted parser/source-run diagnostics for common unsupported invocation-style source lines. `INVOKE` reports `unsupported-invoke`; `ADDR` arguments report `unsupported-addr`; `StdOut` reports `unsupported-masm32-runtime-routine`; `crt_printf` reports `unsupported-crt-routine`; and `ExitProcess` reports `unsupported-winapi-execution` instead of being confused with the virtual Irvine32 `exit` terminator. Source containing these diagnostics refuses execution, does not produce Program Console output, and does not create calls, stack frames, linker/import metadata, or external routine behavior.

## Phase 57Q - INCLUDELIB and External Library Diagnostics

Phase 57Q implements targeted parser/source-run diagnostics for `INCLUDELIB` operands. General library references report `unsupported-includelib`; MASM32 library references report `unsupported-masm32-library`; Windows import-library references report `unsupported-windows-api-library`. Source containing `INCLUDELIB` does not execute, does not produce Program Console output, and does not create linker/import metadata. Phase 57Q does not implement library search paths, object files, import tables, PE loading, a linker, WinAPI execution, or external routine execution.

## Phase 57P - Host Include Path Diagnostics

Phase 57P implements targeted parser/source-run diagnostics for host/path-like `INCLUDE` operands. Recognized MASM32 SDK paths, Windows/API include paths, relative host paths, and Unix-style absolute host paths now emit one stable unsupported include diagnostic per directive instead of repeated lexer `unexpected-character` diagnostics for path separators. Supported virtual includes remain unchanged. Phase 57P does not implement host filesystem access, include search paths, `INCLUDELIB`, PE/object/linker behavior, WinAPI execution, or macro expansion.

## Phase 57O - Explicit-Width NOP Encoding-Operand Forms

Phase 57O accepts selected MASM-compatible 16-bit and 32-bit `nop` encoding operands as source-level no-ops: register forms such as `nop ax` and `nop eax`, plus explicit `WORD PTR`, `SWORD PTR`, `DWORD PTR`, and `SDWORD PTR` memory-looking forms such as `nop DWORD PTR [eax]`. Accepted register forms are not read or written, and accepted memory-looking forms reuse existing memory-addressing grammar only for parsing. They do not evaluate addresses, read or write memory, perform planned memory validation, emit memory diagnostics, create memory-change rows, produce Program Console output, or emit NOP-specific Simulator Messages. Unsupported forms such as 8-bit register operands, immediate operands, untyped memory-looking operands, byte/signed-byte PTR operands and QWORD/SQWORD PTR forms remain rejected with stable diagnostics. Phase 57O does not implement real x86 opcode bytes, `.code` byte images, disassembly, PE/object/linker behavior, or browser UI controls.

## Phase 57N - Zero-Operand NOP Audit, Repair, and Regression Hardening

Phase 57N audits and hardens existing zero-operand `nop` behavior, strengthens parser/executor/source-run/rendered-message coverage, and gives rejected operand-bearing `nop` forms stable diagnostics that deferred explicit-width NOP encoding operands to Phase 57O at the time. It did not add accepted source syntax, real x86 opcode bytes, `.code` byte images, disassembly, browser controls, or Program Console output. Runtime/source-run MASM behavior metadata remained Phase 57M after Phase 57N because zero-operand `nop` was already accepted. Phase 57O later superseded the deferral for selected MASM-compatible NOP register and explicit-width memory-looking encoding operands.

## Phase 57M - MASM Segment and Group Symbol Diagnostics

Phase 57M implements targeted parser/source-run diagnostics for MASM/object/linker segment and group names such as `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT`. When these names are used as addressable symbols, data/equate names, or segment/group definition forms such as `_TEXT SEGMENT` and `DGROUP GROUP _DATA, _BSS`, the parser reports `unsupported-segment-symbol`, refuses execution, and does not create ordinary data symbols, numeric equates, internal-region aliases, segment metadata, linker groups, object files, relocation records, PE layout, or loader metadata. Under default `CASEMAP:ALL`, case variants such as `_text` and `dgroup` are diagnosed as the recognized unsupported names. Under `OPTION CASEMAP:NONE`, exact recognized spellings are diagnosed, while different-case ordinary user labels such as `_text` may be accepted as ordinary symbols.

## Phase 57L - .CODE Memory Access Diagnostics

Phase 57L implements mandatory source-level/runtime `.code` memory-access diagnostics. Wholly contained `.code` reads and writes report `unsupported-code-memory-access`. Cross-region memory accesses whose final byte range intersects `.code` report `region-boundary-crossing` with `.code` protected-region context from the active runtime layout. Fatal diagnostics stop before read consumption, memory mutation, flag mutation, Program Console output, memory-change rows, or `execution-complete`. Phase 57L does not implement segment/group-symbol diagnostics, readable `.code` byte images, x86 opcode emission, PE layout, linker behavior, or segment modeling.

## Phase 57K - .CODE and MASM Segment Symbol Access Policy

Phase 57K audits and characterizes current `.code` memory behavior while locking the v1 source-level policy to `unsupported-code-memory-access`. The simulator executes internal IR and does not expose real x86 opcode bytes, deterministic code-image bytes, a PE `.text` image, or writable/readable `.code` section bytes to simulated source programs. Phase 57L owns later runtime/source-run `.code` access diagnostics.

Phase 57K also defines `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` as unsupported MASM/object/linker segment or group symbols, not aliases for `.code`, `.data`, `.DATA?`, `.CONST`, stack, heap, or any internal VM region. Phase 57M now implements targeted `unsupported-segment-symbol` parser/source-run diagnostics for those names.

Phase 57K updated repository/archive status only. At that time, runtime/source-run MASM behavior remained Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy. Phase 57L later advanced runtime/source-run MASM behavior metadata and implemented `.code` runtime/source-run diagnostics, and Phase 57M now advances runtime/source-run MASM behavior metadata for segment/group-symbol diagnostics.

## Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy

Phase 57J activates the `const-uninitialized-storage` diagnostic family for `.CONST ?` and `.CONST DUP(?)` declarations accepted by Phase 57I. The default `warn` policy emits a Simulator Messages declaration warning and continues; `off` suppresses only the declaration diagnostic; `error` reports an assembly error and refuses execution before runtime. The phase preserves `.CONST` read-only protection, uninitialized-origin metadata, seeded uninitialized-storage visible-byte behavior, and read-time `uninitialized-read` diagnostics as separate behavior.

## Phase 57I - .CONST Uninitialized Storage Acceptance

Phase 57I accepts `.CONST ?` and `.CONST DUP(?)` declarations as compatibility forms. The allocated bytes live in the `.CONST` region, remain read-only for simulated program writes, retain uninitialized-origin metadata, and participate in existing uninitialized-read diagnostics and seeded uninitialized-storage visible-byte mode. Phase 57J adds the configurable declaration diagnostic policy for this suspicious teaching pattern.

## Phase 57H - Register Unchanged Display Markers

Phase 57H appends `[unchanged]` to final-register canonical parent rows when no part of that register family was written by the executed program. Alias rows remain clean and inherit parent-family status visually. Same-value writes such as `mov eax, 0` count as changed because the implementation uses explicit register-family write metadata instead of value-equality inference.

Phase 57H is display-formatting work only. It does not add MASM syntax, parser behavior, VM instruction semantics, diagnostic codes, rendered Simulator Messages wording, Program Console output, browser settings UI, or runtime/source-run MASM behavior phase advancement.

## Phase 57G - Seeded Random Uninitialized Storage Mode

Phase 57G adds a source-run/test-facing `uninitialized_storage_visible_byte_mode` setting with `zero` and `seeded-random` modes. The default zero mode preserves prior visible zero bytes for `.DATA?`, `?`, and `DUP(?)`. The seeded mode initializes only visible bytes that still carry uninitialized-origin metadata from `startup_state_seed`, while preserving that metadata so uninitialized-read diagnostics still report code-quality issues.

Phase 57G does not randomize initialized `.data` or `.CONST` bytes, does not accept `.CONST ?`, does not add browser UI controls, and does not change MASM parser or instruction semantics.

## Phase 57F - Seeded Random Register and Flag Startup Mode

Phase 57F adds a source-run/test-facing `startup_register_flag_mode` setting with `zero` and `seeded-random` modes plus a `startup_state_seed` value. The default mode preserves deterministic zero startup for registers and modeled flags. The seeded mode initializes EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, and the modeled CF/ZF/SF/OF flags from a deterministic seed while leaving EIP and unmodeled EFLAGS bits at zero.

Phase 57F does not randomize memory, does not change `.DATA?`, `?`, or `DUP(?)` visible bytes, does not change uninitialized-origin metadata, does not add browser UI controls, and does not change MASM parser or instruction semantics.

Corrective current-status note after Phase 68A: the Phase 57F historical entry describes the behavior as implemented at that time. Phase 68A later changed `ESP` startup so that the initial `ESP` value comes from the active stack region's documented empty-stack address. Current documentation and future phases must treat seeded register/flag startup as applying to the ordinary startup register set, currently `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`, and modeled flags. `ESP` remains source-writable through supported explicit register instructions, but its initial value remains stack-derived and is not supplied by the ordinary zero-start or seeded-startup register path. This note does not rewrite Phase 57F history; it records the later Phase 68A contract that supersedes the old `ESP` startup wording.

## Phase 57E - Startup State Notice and Zero-Default Documentation

Phase 57E documents that the simulator starts registers and modeled flags at zero, and that uninitialized storage bytes are zero-filled while preserving uninitialized-origin metadata for code-quality diagnostics. It activates the `startup-state-notice` diagnostic-policy family and emits a default non-fatal Simulator Messages notice explaining that real MASM programs should not rely on arbitrary register or flag startup values.

Phase 57E preserves startup values, parser behavior, VM instruction behavior, executor behavior, browser UI controls, diagnostic setting names, Program Console output, and runtime/source-run MASM behavior metadata. Runtime/source-run MASM behavior remains Phase 57 - Signed IDIV. Test/configuration-facing paths can opt out of the notice through the diagnostic policy registry path.

## Phase 57D - Existing Diagnostic Policy Migration

Phase 57D migrates existing configurable diagnostic-policy lookup for uninitialized-read diagnostics, undefined-flag-use diagnostics, and compatibility notices through the shared registry or narrow compatibility adapters. It preserves the Phase 53E public setting names and backend enum compatibility, including `strict` as the legacy public name for registry `error` and `on` as the legacy public name for emitted compatibility notices.

Phase 57D does not add new diagnostic families, browser settings, MASM syntax, parser behavior, VM behavior, executor behavior, diagnostic codes, rendered Simulator Messages wording, source-run JSON behavior, Program Console behavior, or runtime/source-run MASM behavior metadata changes. Runtime/source-run MASM behavior remains Phase 57 - Signed IDIV.

## Phase 57C - Diagnostic Policy Registry Design

Phase 57C adds the shared diagnostic-policy registry skeleton for optional teaching diagnostics. The registry defines the common `off`, `warn`, and `error` policy vocabulary and central names for current and reserved policy families: `uninitialized-read`, `undefined-flag-use`, `compatibility-notice`, `const-uninitialized-storage`, `startup-state-notice`, and `unsupported-code-memory-access`.

Phase 57C does not migrate existing diagnostic settings onto the registry and does not add user-visible diagnostic behavior. It does not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser UI behavior, worker protocol behavior, diagnostic JSON fields, diagnostic codes, rendered Simulator Messages wording, source-run JSON behavior, Program Console behavior, or runtime/source-run MASM behavior metadata changes.

## Phase 57B - Milestone History and Build Documentation Extraction

Phase 57B completes the documentation decomposition started by Phase 57A. It updates this milestone-history document so it is a useful navigation layer, adds a dedicated build/development guide, keeps README concise, and preserves the distinction between repository/archive milestone and runtime/source-run MASM behavior phase.

Phase 57B did not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser UI behavior, worker protocol behavior, diagnostic JSON fields, diagnostic codes, rendered Simulator Messages wording, source-run JSON behavior, Program Console behavior, or runtime/source-run MASM behavior metadata changes.

## Phase 57A - README Landing Page Cleanup

Phase 57A moved the long milestone-accomplishment wall out of the README and into this history file. It did not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser UI behavior, diagnostic behavior, rendered Simulator Messages behavior, source-run JSON behavior, or runtime/source-run MASM behavior metadata changes.

## Preserved README milestone summary before Phase 57A

The following summary was preserved from `README.md` in the Phase 57-CORR2 repository archive before the Phase 57A README cleanup.

Static, browser-based MASM32 educational simulator. The repository state includes runtime MASM behavior implemented through Milestone 30, Milestone 31 native/Node diagnostic rendering tests, Milestone 32 fixed-layout policy infrastructure, Milestone 33 automatic deterministic layout sizing available to tests/configuration, Milestone 34 stack/heap size metadata applied to automatic layout, Milestone 35 seeded/fresh randomized layout placement available to tests/configuration, Milestone 35A MASM-compatible `OPTION CASEMAP` user-symbol case policy correction, Milestone 36 declared-object allocation map metadata for tests/internal tooling, Milestone 37 allocated-object warning mode for tests/configuration, Milestone 38 allocated-object strict mode for tests/configuration, Milestone 39 uninitialized-origin byte metadata/write tracking for test/internal inspection, Milestone 40 opt-in uninitialized-read warning/strict validation for tests/configuration, Milestone 41 virtual Irvine32 symbol-registry metadata/diagnostics, Milestone 42 Irvine32 `exit` virtual terminator behavior, Milestone 43 `inc`/`dec` instruction behavior, Milestone 44 `and`/`or`/`xor` instruction behavior, Milestone 45 `not` instruction behavior, Milestone 46 `shl`/`sal` instruction behavior, Milestone 47 `shr` instruction behavior, Milestone 48 `sar` instruction behavior, Milestone 49 `rol` instruction behavior, Milestone 50 `ror` instruction behavior, Milestone 50A undefined modeled-flag validity metadata for currently modeled flags, Milestone 50B opt-in undefined flag-use diagnostics for existing flag consumers, Milestone 51 post-30 smoke-harness validation coverage, Milestone 52 `lea` effective-address computation, Milestone 52A signed register and memory value display formatting, Milestone 53 unsigned `mul` instruction behavior, Milestone 53A memory-validation policy clarification with symbol-offset runtime correction, Milestone 53B opt-in section-capacity and section-image validation modes, Milestone 53C default teaching warnings for existing uninitialized-read and undefined-flag-use diagnostics, Milestone 53D default compatibility notices for accepted no-op, metadata-only, and limited-behavior MASM constructs, Milestone 53E browser UI settings for existing memory validation and teaching diagnostic policies, Milestone 54 one-operand signed `imul` instruction behavior, Milestone 55 two- and three-operand signed `imul` forms, Milestone 56 unsigned `div` instruction behavior, Milestone 57 signed `idiv` instruction behavior, plus Phase 57-CORR1 `.CONST` cross-region diagnostic clarification and Phase 57-CORR2 compact negative register-indirect displacement correction without advancing runtime/source-run MASM behavior metadata.

## Preserved current-scope history before Phase 57A

The following section was preserved from `README.md` in the Phase 57-CORR2 repository archive. It describes the repository/archive and runtime/source-run status immediately before Phase 57A.

## Current scope before Phase 57A

Repository/archive milestone:
Phase 57-CORR2 - Compact Negative Register-Indirect Displacement Correction

Runtime/source-run MASM behavior phase:
Phase 57 - Signed IDIV

Phase 57-CORR2 is a parser-acceptance corrective milestone. It keeps runtime/source-run MASM behavior metadata at Phase 57 - Signed IDIV while accepting compact negative register-indirect displacements such as `[eax-4]` as the same simple base-minus-constant byte displacement form as `[eax - 4]`. Phase 57-CORR1 remains the accepted diagnostic-context clarification for fatal `region-boundary-crossing` Simulator Messages on cross-region reads and writes whose requested final byte range intersects protected `.CONST` storage. Implemented runtime behavior includes:

- C99 core, parser, executor, memory model, and WebAssembly export boundary.
- Static browser UI shell with worker-based source execution.
- MASM32-mode register aliases, arithmetic flags, checked memory regions, and minimal IR execution.
- Lexer/parser support through `.data`, `.DATA?`, `.CONST`, `.code`, symbols, numeric equates, extended constant expressions, constant and register-indirect memory operands, `PTR`, `OFFSET`, `TYPE`, `LENGTHOF`, `SIZEOF`, and character literals.
- Executable `mov`, `add`, `sub`, `movsx`, `movzx`, `cbw`, `cwde`, `cwd`, `cdq`, `xchg`, `neg`, `nop`, `adc`, `sbb`, `clc`, `stc`, `cmc`, `test`, `inc`, `dec`, `and`, `or`, `xor`, `not`, `shl`, `sal`, `shr`, `sar`, `rol`, `ror`, `mul`, `imul`, `div`, and `idiv` for the currently supported operand forms.
- Explicit `unsupported-feature` diagnostics for common textbook MASM constructs tracked by the Milestone 15 backlog checkpoint.
- Milestone 22 `test` instruction support, including MASM-compatible ambiguous memory-width diagnostics for `test [reg], imm` forms.
- Milestone 23 signed `PTR` width aliases: `SBYTE PTR`, `SWORD PTR`, `SDWORD PTR`, and recognized-but-deferred `SQWORD PTR` executable memory operations.
- Milestone 24 all-GPR register-indirect bases: `[eax]`, `[ebx]`, `[ecx]`, `[edx]`, `[esi]`, `[edi]`, `[ebp]`, and `[esp]`, including simple spaced and compact base-plus-or-minus-constant byte displacements such as `[eax + 4]`, `[eax+4]`, `[eax - 4]`, and `[eax-4]`.
- Milestone 25 global memory-width resolution rules: memory operands now share one parser validation path for the implemented memory-capable instructions, register operands can supply unambiguous memory width, and ambiguous memory/immediate forms produce `ambiguous-memory-width` diagnostics.
- Milestone 26 MASM32 header compatibility directives: `.386`, `.model flat, stdcall`, `.stack`, `OPTION CASEMAP:ALL`, `OPTION CASEMAP:NONE`, virtual `INCLUDE Irvine32.inc` / `INCLUDE Macros.inc`, and listing no-ops parse before the program body. Milestone 53D emits non-fatal `simulator-notice` messages for accepted compatibility no-ops, metadata-only directives, and limited virtual behavior such as `INCLUDE Macros.inc`, while active semantics such as `INCLUDE Irvine32.inc`, `OPTION CASEMAP`, `.DATA?`, and `.CONST` do not receive generic no-op notices. Milestone 53E exposes these notices, uninitialized-read warnings, undefined-flag-use warnings, and existing memory validation modes through browser settings without changing backend semantics.
- Milestone 27 additional data sections: `.DATA?` accepts `?`/`DUP(?)` uninitialized declarations and emits deterministic zero-filled writable storage with uninitialized metadata; `.CONST` emits initialized read-only storage with direct and indirect write protection.
- Milestone 29 extended constant expressions: numeric equates now support unary `+`/`-`/`NOT`, parentheses, binary `+`, `-`, `*`, `/`, `MOD`, `SHL`, `SHR`, `AND`, `OR`, `XOR`, and extraction operators `HIGH`, `LOW`, `HIGHWORD`, and `LOWWORD` in supported compile-time constant contexts.
- Milestone 30 nested `DUP`: data declarations now support nested `DUP` expansion such as `ROWS DUP(COLS DUP(0))`, expression-backed counts and initializer values, deterministic `?` storage, and expansion-capacity diagnostics.
- Native diagnostic rendering harness: the aggregate test runner builds a C source-run JSON producer and a Node harness verifies exact Simulator Messages text through the same formatter module used by the browser UI. This verifies native JSON and formatter output, not that `web/dist` Wasm artifacts were rebuilt.
- Milestone 32 memory layout policy infrastructure: the VM memory loader now consumes an explicit fixed-layout policy object while preserving the existing educational region bases, sizes, permissions, and diagnostics. This adds no new MASM syntax or user-facing layout controls.
- Milestone 33 automatic deterministic layout sizing: tests/configuration can select automatic sizing that computes aligned region sizes from parsed code/data/const metadata and documented defaults. The served website still uses fixed educational layout by default.
- Milestone 34 stack/heap size metadata for layout: automatic layout now applies parsed `.stack` size metadata and configured heap-size requests to region capacity metadata. This remains test/configuration-only and adds no stack instructions, heap allocation API, or user-facing layout controls.
- Milestone 35 randomized layout placement: seeded and fresh randomized layout modes can be selected by tests/configuration. Symbolic addresses, `OFFSET`, `.CONST` permissions, and `.DATA?` storage relocate to selected bases; hardcoded fixed addresses are intentionally unreliable under randomized layout. No UI controls, URL seed persistence, object-bounds diagnostics, new MASM syntax, or new runtime instructions were added.
- Milestone 35A `OPTION CASEMAP` correction: user-defined symbols are case-insensitive by default, `CASEMAP:ALL` explicitly selects that policy, `CASEMAP:NONE` enables exact-case user-symbol lookup from that point forward, `CASEMAP:NOTPUBLIC` is recognized but unsupported, invalid CASEMAP values are diagnosed, and policy-change warnings do not block execution.
- Milestone 36 declared-object allocation map: tests/internal tooling can query declared `.data`, `.DATA?`, and `.CONST` objects with final selected-layout address ranges, size/type metadata, permissions, source locations, and a `not-tracked` initialization-origin placeholder. This metadata adds no object-bounds warnings/errors and does not change served-site execution behavior.
- Milestone 37 allocated-object warning mode: tests/configuration can enable non-fatal simulator warnings for valid-region memory accesses that are outside declared data objects, partially overlap object boundaries, or span adjacent objects.
- Milestone 38 allocated-object strict mode: tests/configuration can stop execution with `object-bounds-violation` for those same valid-region/object-boundary escapes. Default region-only execution remains unchanged; provenance diagnostics remain deferred, while Milestone 53E exposes declared-object warning/strict validation as an optional browser memory range setting.
- Milestone 39 uninitialized-origin metadata: tests/internal tooling can inspect which `.data` and `.DATA?` bytes originated from `?`/`DUP(?)` and which bytes have been initialized by successful program writes. Default runtime values and browser UI behavior remain unchanged.
- Milestone 40 uninitialized-read validation: tests/configuration can enable warning mode, which emits non-fatal `uninitialized-read` simulator warnings, or strict mode, which stops execution before reading bytes that still have uninitialized-origin state. Milestone 53C makes omitted/default browser source-run behavior use the warning policy, while explicit region-only/off validation preserves silent deterministic zero-filled reads.
- Milestone 41 virtual Irvine32 symbol registry: `INCLUDE Irvine32.inc` records known Irvine32 names as parser/source-run metadata and lets currently recognized executable uses of known not-yet-executable routines report `unsupported-irvine32-routine`.
- Milestone 42 Irvine32 `exit` terminator: with `INCLUDE Irvine32.inc` active, zero-operand `exit` terminates execution successfully and skips following instructions. This adds no `call`, `ret`, ExitProcess behavior, stack behavior, Program Console routines, other Irvine32 bodies, Windows API execution, or linking.
- Milestone 43 `inc`/`dec`: register and unambiguous memory destinations are supported for 8-bit, 16-bit, and 32-bit widths. These instructions update `ZF`, `SF`, and `OF`, preserve `CF`, and keep QWORD/SQWORD executable memory operations deferred.
- Milestone 44 `and`/`or`/`xor`: register and unambiguous memory destinations are supported with compatible register, immediate, or memory sources. These instructions store the logical result, update `ZF` and `SF`, clear `CF` and `OF`, reject memory-to-memory and ambiguous memory/immediate forms, and keep QWORD/SQWORD executable memory operations deferred.
- Milestone 45 `not`: register and unambiguous memory destinations are supported for 8-bit, 16-bit, and 32-bit widths. `not` stores the bitwise complement, preserves `CF`, `ZF`, `SF`, and `OF` exactly, rejects ambiguous memory-width forms, and keeps QWORD/SQWORD executable memory operations deferred.
- Milestone 46 `shl`/`sal`: register and unambiguous memory destinations are supported with immediate byte counts or `CL`; counts use `raw_count & 31`, count zero is a no-op, default undefined modeled-flag cases warn and continue with flag-specific wording, and test/configuration strict mode stops before mutation.
- Milestone 47 `shr`: logical right shift uses the same destination and count forms as the Phase 46 shifts, fills high bits with zero, defines `OF` from the original sign bit for one-bit shifts, warns by default for undefined modeled-flag cases, and preserves deterministic strict-mode behavior through the test/configuration API.
- Milestone 48 `sar`: arithmetic right shift reuses the shift destination/count forms, fills high bits with the original sign bit, clears `OF` for one-bit shifts, and uses the same deterministic `undefined-shift-flag` warning/strict policy as the other shift instructions.
- Milestone 49 `rol`: rotate left reuses the same destination and count forms, rotates bits within the selected 8-bit, 16-bit, or 32-bit width, updates `CF` from the least significant bit of the rotated result, preserves `ZF` and `SF`, defines `OF` only for one-bit rotates, and emits warning code `undefined-modeled-flag` for non-one nonzero counts while continuing execution. Strict shift validation remains shift-only and does not convert `ROL` warnings into runtime errors.
- Milestone 50 `ror`: rotate right reuses the same destination and count forms as `rol`, rotates bits within the selected 8-bit, 16-bit, or 32-bit width, updates `CF` from the most significant bit of the rotated result, preserves `ZF` and `SF`, defines `OF` only for one-bit rotates, and emits warning code `undefined-modeled-flag` for non-one nonzero counts while continuing execution. Strict shift validation remains shift-only and does not convert `ROR` warnings into runtime errors.
- Milestone 50A modeled-flag validity metadata: the C99 CPU/executor now tracks whether each currently modeled flag value (`CF`, `ZF`, `SF`, and `OF`) is architecturally valid or is a deterministic fallback from an undefined-flag producer. This is internal metadata for later flag-consumer diagnostics; it adds no new instructions, no default `undefined-flag-use` diagnostics, and no changed Program Console or Simulator Messages output.
- Milestone 50B undefined flag-use diagnostics: source-run tests/configuration can select consumer-side `undefined-flag-use` diagnostics with `off`, `warn`, or `error` policy. Existing flag consumers `adc`, `sbb`, and `cmc` check `CF` before use. Warning mode emits a Simulator Messages warning and continues with the deterministic fallback bit; error mode stops before the consumer mutates state. Milestone 53C makes omitted/default browser source-run behavior use the warning policy, while explicit off preserves silent consumer behavior.
- Milestone 51 post-30 smoke harness: the aggregate native/Node test command now reports validation-only smoke coverage for fixed/automatic layout equivalence, `.CONST` permission precedence, uninitialized read-modify-write warnings, Irvine32 `exit` casing with CASEMAP preservation, and representative source-run plus rendered diagnostic smoke fixtures for the post-30 instruction families. This adds no new MASM syntax or runtime behavior.
- Milestone 52 `lea`: computes effective addresses into 32-bit register destinations for the currently supported address-expression forms, writes no memory, reads no memory, preserves modeled flags, emits no memory/object/uninitialized diagnostics for address computation itself, and keeps scaled-index addressing deferred.
- Milestone 52A signed value display: existing final register and memory-change displays show hexadecimal, unsigned decimal, and signed decimal interpretations for known-width 8-bit, 16-bit, and 32-bit integer values. Final registers use aligned grouped rows for register families, and memory changes use aligned old/new blocks. This is display-only formatting; it does not change parser behavior, VM execution, memory bytes, flags, Program Console output, or Simulator Messages text.
- Milestone 53 unsigned `mul`: one-operand unsigned multiplication supports 8-bit, 16-bit, and 32-bit register or unambiguous memory sources. `mul` multiplies `AL`, `AX`, or `EAX` by the source and stores the product in `AX`, `DX:AX`, or `EDX:EAX`, sets or clears `CF`/`OF` from the upper product half, preserves `ZF`/`SF`, uses checked memory reads, and does not create memory-change rows for memory sources.
- Milestone 53A memory-validation clarification: default execution is Level 1 region-only validation; parser symbol-offset validation no longer rejects valid memory operands merely because the inferred access crosses a declared object, declared section image, section capacity, or fixed-layout slack. The current VM represents `.data` and `.DATA?` in one writable `.data` region and `.CONST` in a separate read-only `.const` region, so `.CONST` writes remain permission failures and cross-region accesses remain ordinary runtime memory errors.
- Milestone 53B section-boundary diagnostics: Level 2 section-capacity validation and Level 3 section-image validation are now opt-in local test/source-run policies with `off`, `warn`, and `strict` behavior. Warning mode emits `section-capacity-violation` or `section-image-violation` simulator warnings and continues; strict mode emits the same codes as runtime errors before mutation. Default browser/source execution still leaves section-capacity, section-image, and allocated-object validation off; existing allocated-object warning/strict modes remain Level 4 declared-object validation.
- Milestone 53C default teaching diagnostics: omitted/default browser source-run behavior now emits non-fatal `uninitialized-read` and `undefined-flag-use` Simulator Messages warnings. Programs continue using deterministic zero-filled uninitialized-origin bytes and deterministic preserved flag values. Explicit region-only/off policies preserve the earlier silent behavior, and strict/error modes remain opt-in.
- Milestone 53D compatibility notices: accepted MASM compatibility constructs with no-op, metadata-only, virtual-only, or limited simulator behavior now emit non-fatal `simulator-notice` messages by default. This covers `.386`/`.486`/`.586`/`.686`, `.model flat, stdcall`, `.stack`, `.stack size`, `INCLUDE Macros.inc`, `TITLE`, `SUBTITLE`, and `PAGE`. These notices do not affect Program Console output or VM execution.
- Milestone 53E diagnostic settings: the browser UI exposes existing memory range validation, uninitialized-read, undefined-flag-use, and compatibility-notice policies. Defaults remain region-only memory validation, uninitialized-read warn, undefined-flag-use warn, and compatibility notices on. Diagnostic settings are local page-session preferences; share URLs are not implemented yet and do not currently encode these settings.
- Milestone 54 one-operand signed `imul`: signed multiplication supports 8-bit, 16-bit, and 32-bit register or unambiguous memory sources. `imul` multiplies signed `AL`, `AX`, or `EAX` by the signed source and stores the double-width product in `AX`, `DX:AX`, or `EDX:EAX`. `CF` and `OF` are clear only when the full signed product is exactly the sign extension of the lower result half; `ZF` and `SF` are preserved deterministically.
- Milestone 55 two- and three-operand signed `imul`: 16-bit and 32-bit register destinations now support register and unambiguous memory sources, plus signed immediate third operands. These forms store the low destination-width product in the destination register, set `CF`/`OF` on signed truncation, and preserve `ZF`/`SF` deterministically.
- Milestone 56 unsigned `div`: one-operand unsigned division supports 8-bit, 16-bit, and 32-bit register or unambiguous memory divisors. `div` divides `AX`, `DX:AX`, or `EDX:EAX` by the unsigned source, stores the quotient and remainder in the width-selected result registers, preserves modeled flags, and reports `divide-by-zero` or `quotient-overflow` runtime errors before mutation.
- Milestone 57 signed `idiv`: one-operand signed division supports 8-bit, 16-bit, and 32-bit register or unambiguous memory divisors. `idiv` divides signed `AX`, `DX:AX`, or `EDX:EAX` by the signed source, stores the quotient and remainder in the width-selected result registers, preserves modeled flags and flag-validity metadata, and reports `divide-by-zero` or `quotient-overflow` runtime errors before mutation.
- Command-line native and JavaScript tests.
- Windows development scripts for Visual Studio and Emscripten.

## Historical legacy limitation note

A stale limitation list previously appeared here. It was written for an earlier repository state before later control-flow, stack-startup, direct CALL, helper RET, selected-entry root RET, and called non-entry procedure fallthrough phases were accepted.

The stale list has been removed from the active milestone-history narrative because it contradicted the latest recorded Phase 71-era repository behavior. This section is preserved only to explain why old references to that list should not be used as current behavior authority.

For current accepted syntax and diagnostics, read:

- `docs/SUPPORTED_SYNTAX.md`

For canonical phase order, phase dependencies, required tests, and acceptance criteria, read:

- `docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`

For latest implementation-status evidence, read:

- `README.md`
- the latest accepted milestone report under `docs/history/reports/`

Current documentation must not treat removed or historical limitation notes as active “not implemented yet” checklists.
