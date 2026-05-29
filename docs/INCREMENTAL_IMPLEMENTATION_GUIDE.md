# Online MASM32 Educational Simulator - Incremental Implementation Guide

> **Canonical source-of-truth note:** This file is paired with `FULL_IMPLEMENTATION_SPEC.md`. Together they are the current reviewed source-of-truth revision for Phase 57M MASM segment and group symbol diagnostics, Phase 57L .CODE memory access diagnostics, Phase 57K .CODE and MASM segment symbol access policy, Phase 57J .CONST uninitialized storage diagnostics and policy, Phase 57I .CONST uninitialized storage acceptance, Phase 57H register unchanged display markers, Phase 57G seeded random uninitialized-storage visible-byte mode, Phase 57F seeded random register/flag startup mode, Phase 57E startup-state notice and zero-default documentation, Phase 57D existing diagnostic-policy migration, Phase 57C diagnostic-policy registry design, the Phase 57-CORR2 compact negative register-indirect displacement correction, and the Phase 57-CORR1 `region-boundary-crossing` protected-region diagnostic clarification. This guide preserves completed Phases 0-30, then defines the canonical post-30 roadmap, phase numbering, implementation tasks, required tests, and acceptance criteria. The paired specification owns product boundaries, stable behavior, stable cross-cutting rules, and current/future/non-goal distinctions.


## 1. Purpose

This guide breaks the simulator into small implementation phases suitable for incremental development with an AI coding assistant.

The goal is to avoid attempting full MASM compatibility at once. Each phase should produce a working, testable improvement.

The guide assumes the final target described in `FULL_IMPLEMENTATION_SPEC.md`.

Documentation ownership rule for future edits: keep stable product boundaries, broad non-goals, and cross-cutting behavior contracts in the specification. Keep phase order, phase scope, implementation tasks, required tests, and acceptance criteria in this guide. A phase may contain a short local reminder of a specification rule when that reminder prevents likely scope creep, but the phase must not restate a large product-boundary block unless the phase is specifically changing or testing that boundary.

## 2. General Development Rules

### 2.0 Core Language Policy

`FULL_IMPLEMENTATION_SPEC.md` Section 6 owns the stable core language policy. This guide section is the phase-work reminder that must be applied during every implementation phase.

The VM core, parser, executor, memory model, Irvine32 runtime, and Wasm-facing API must be implemented in **C99**.

Rules:

- Use `.c` and `.h` files for core modules.
- Compile native tests and Emscripten builds as C99.
- Do not introduce C++ source files for the core.
- Do not use C++ standard library features, templates, classes, exceptions, RTTI, or `extern "C"` compatibility wrappers.
- Keep API boundaries plain C: structs, enums, explicit status codes, and documented functions.
- Browser UI code may remain JavaScript or TypeScript.
- The polished source editor should use CodeMirror 6 as the selected editor component; the core C99 parser/VM remains the source of truth for MASM semantics.

### 2.1 Build Vertically

Prefer small vertical slices over broad incomplete systems.

A good slice:

```text
Parse `mov eax, 20`
Execute it
Show EAX changed in debugger
Test it
```

A bad slice:

```text
Create 50 parser classes before anything can run
```

### 2.2 Keep the VM Deterministic

The same source, settings, and input should produce the same result.

Random behavior should be seeded.

### 2.3 Centralize Safety Checks

All memory reads and writes must go through the VM memory module.

All console output must go through the console module.

All execution must go through the VM run/step functions.


### 2.4a Diagnostic Rendering Tests

Diagnostic tests must verify both structured diagnostics and final rendered Simulator Messages text. Native C tests that only inspect internal status codes are not sufficient for user-facing diagnostic quality.

The project must keep the browser Simulator Messages formatter in a side-effect-free module that can be imported by Node tests. The aggregate test runner should run a native diagnostic JSON producer and the Node formatter harness whenever diagnostic wording, JSON shape, parser recovery, runtime errors, or warning routing changes.

Manual browser testing remains required for rebuilt Wasm artifacts and DOM/Worker integration, but manual browser testing must not be the only way to validate diagnostic message wording.

User-facing diagnostic wording must be stable across repository/archive maintenance milestones.

Diagnostics shown in Simulator Messages must not describe an error as “unsupported by the current milestone,” “not implemented in this milestone,” or similar milestone-relative wording. Such wording becomes stale when maintenance phases advance the repository/archive milestone without changing runtime/source-run MASM behavior.

Diagnostics should instead identify one of these stable reasons:

- malformed MASM syntax;
- MASM-invalid ambiguous form, such as ambiguous memory width;
- implemented simulator restriction, such as executable QWORD/SQWORD memory operations being unavailable in MASM32 Educational Mode;
- planned future feature, named with both phase number and phase title when appropriate;
- explicit non-goal, such as WinAPI execution, PE loading, object linking, host filesystem include loading, or native x86 execution;
- runtime safety failure, such as invalid address range or `.CONST` write overlap.

Historical milestone reports may contain milestone-relative wording because they describe what happened at the time. Current user-facing diagnostics, current supported-syntax documentation, current browser status text, and current tests should use stable wording.

### 2.4b Test Runner Decomposition, Fixture Size, and Timeout-Safe Verification

`FULL_IMPLEMENTATION_SPEC.md` Section 21.3 owns the stable timeout-safe verification policy. This guide section owns the implementation-session reporting rules and the phase-level obligation to preserve exact structured and rendered diagnostic coverage.

The aggregate test runner should remain the normal full-suite command, but it must not be the only way to verify the implemented milestone suite.

The test runner must support focused groups so that assistants, CI jobs, and developers can run expensive or verbose test families independently. This is required because hosted assistant/container environments may impose wall-time, output-size, or process limits that are stricter than a local developer terminal.

A timeout from a hosted assistant/container tool is not automatically a project test failure. When the aggregate command times out or its output is truncated in a hosted environment, the assistant must rerun focused test groups individually and report the result honestly.

Required reporting distinction:

```text
aggregate completed and passed
aggregate timed out in assistant/container environment, focused groups passed
aggregate failed with a real failing group
focused group failed
focused group timed out, subgroups or fixtures rerun
group skipped because dependency was unavailable, such as emcc
```

The assistant must not claim that the full aggregate suite passed unless the aggregate command completed and returned the final success status in that environment.

The assistant may claim focused verification only when it lists the groups that were actually run and passed.

If a focused group is itself too large for the assistant/container environment, the assistant must rerun documented subgroups or individual fixture families and report those results by name.

The test runner and test files must keep user-visible diagnostic coverage intact:

- do not remove exact rendered Simulator Messages tests to reduce runtime;
- do not replace exact rendered-message assertions with weaker substring checks unless an exact assertion is preserved elsewhere and the exception is documented;
- do not skip native diagnostic JSON generation when diagnostic wording, JSON shape, parser recovery, runtime errors, or warning routing changes;
- do not treat manual browser testing as a substitute for native/Node diagnostic-rendering tests.

Large test files and oversized individual MASM source fixtures must be handled deliberately.

Rules:

- Prefer focused fixtures over broad "kitchen sink" fixtures.
- Use integration smoke fixtures only when cross-feature composition is the explicit purpose.
- Label every source-run fixture with a stable, descriptive fixture name.
- On failure, report the fixture name before printing fixture output.
- Do not delete fixtures to reduce runtime.
- Do not weaken assertions while splitting files or fixtures.
- Do not move source-run fixtures into lower-level executor tests merely to reduce runtime; source-run tests intentionally exercise parser, layout, VM execution, diagnostics, and JSON/result behavior together.
- If a source fixture becomes too long to maintain source line/column expectations safely, split it into smaller programs or move it to a named external fixture file.

Default successful output should be compact. Verbose fixture-level output must be available on demand and must be shown or summarized when a failure occurs.

Future milestone reports must document whether the aggregate runner completed, whether focused groups were run, whether any focused group required subgroup/fixture reruns, and whether browser/Wasm smoke testing was skipped because Emscripten was unavailable.

### 2.4c Repository Milestone Status versus Runtime Phase Metadata

The repository/archive milestone and the runtime/source-run MASM behavior phase are related but not always identical.

The repository/archive milestone identifies the latest accepted project state. It may advance because of any accepted project work, including:

- runtime behavior;
- parser behavior;
- browser UI behavior;
- source-run or Wasm API behavior;
- diagnostic JSON shape or rendered message wording;
- documentation;
- supported-syntax reference updates;
- test-runner infrastructure;
- verification ergonomics;
- repository maintenance.

The runtime/source-run MASM behavior phase identifies the latest implemented MASM/source execution behavior that the runtime reports to tests, worker protocol results, browser status displays, source-run JSON payloads, Wasm/source-run status fields, supported-syntax current-status text, or other current-status surfaces. It must advance only when the target phase explicitly changes runtime-visible MASM/source behavior or explicitly requires metadata advancement.

Use the current-status surface list in `FULL_IMPLEMENTATION_SPEC.md` Section 1.1 - Current-status Surface Hygiene. That list is mandatory for guide work and may be extended by a target phase when the phase introduces a new status-bearing surface. Do not maintain a second divergent copy of the complete surface list in this guide; if a new stable current-status surface is added, update the specification list and reference it from phase-local text.

Future assistants must apply this rule:

```text
Do not update source-run JSON phase fields, protocol phase values, browser runtime-status strings, supported-syntax runtime-status wording, README current-status wording, worker status wording, or tests that assert runtime phase values merely because a documentation-only, display-only, validation-only, diagnostic-wording, test-runner-only, verification-ergonomics, or repository-maintenance phase was completed.
```

A maintenance phase may advance the repository/archive milestone without changing runtime/source-run MASM behavior metadata.

Examples:

- Phase 52A - Signed Register and Memory Value Display advanced display formatting behavior but did not add new MASM syntax.
- Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics advanced repository/test-runner status after Phase 56 but did not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, diagnostic codes, diagnostic JSON fields, or rendered Simulator Messages wording.
- Phase 56B - User-Facing Diagnostic Wording Cleanup advances repository/archive status after Phase 56A when implemented, but it is diagnostic wording, current-status wording, and test-maintenance work only. It does not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, new diagnostic codes, new diagnostic JSON fields, or new rendered Simulator Messages categories.
- After Phase 56A and Phase 56B, the runtime/source-run MASM behavior phase remains Phase 56 - Unsigned DIV until Phase 57 - Signed IDIV or another runtime behavior phase is implemented and accepted.

Every future milestone report must state both values when they differ. Use this exact label format:

```text
Repository/archive milestone:
Runtime/source-run MASM behavior phase:
```

For a runtime behavior milestone, the two values normally match. For a maintenance, documentation, display-only, test-runner-only, verification-ergonomics, diagnostic-wording, or repository-maintenance milestone, the two values may differ.

Required report wording when they differ:

```text
Repository/archive milestone:
Phase <N or suffix> - <phase title>

Runtime/source-run MASM behavior phase:
Phase <M> - <runtime behavior phase title>

Status interpretation:
The repository/archive milestone is newer than the runtime/source-run MASM behavior phase because this target phase did not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, diagnostic codes, diagnostic JSON fields, or rendered Simulator Messages wording. Do not update runtime/source-run phase metadata, supported-syntax runtime wording, or tests that assert runtime phase values unless the current target phase explicitly owns that change.
```

A future assistant must not "fix" a perceived mismatch between repository/archive milestone and runtime/source-run MASM behavior phase unless the current target phase explicitly owns that metadata change.

If a test appears stale because it expects the previous runtime phase number after a maintenance phase, the assistant must first decide whether the target phase changed runtime-visible behavior. If it did not, the test is probably correct and must not be rewritten to match the repository/archive milestone.

If a supported-syntax document appears stale because it says syntax support is through an earlier runtime phase after a later maintenance phase, the assistant must first decide whether the later phase changed supported MASM syntax. If it did not, the supported-syntax document is probably correct. At most, add a short note explaining that the later repository/archive phase was maintenance-only.

Static/reporting acceptance criterion:

```text
When a milestone report describes a target phase whose repository/archive milestone differs from runtime/source-run MASM behavior phase, the report must include this exact block:

Repository/archive milestone:
Phase <N or suffix> - <phase title>

Runtime/source-run MASM behavior phase:
Phase <M> - <runtime behavior phase title>

Status interpretation:
The repository/archive milestone is newer than the runtime/source-run MASM behavior phase because this target phase did not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, diagnostic codes, diagnostic JSON fields, or rendered Simulator Messages wording. Do not update runtime/source-run phase metadata, supported-syntax runtime wording, or tests that assert runtime phase values unless the current target phase explicitly owns that change.
```

This exact block is required for maintenance, documentation, display-only, test-runner-only, verification-ergonomics, diagnostic-wording, and repository-maintenance phases when they do not advance runtime/source-run MASM behavior.

Historical reports that predate this exact wording do not need to be rewritten unless the user explicitly asks for historical report cleanup. Future milestone reports must use the block.

When a milestone is a runtime behavior milestone and the two values match, the report should still include both labels if the immediately preceding repository/archive milestone was maintenance-only. This prevents the assistant from losing the distinction at the point where runtime metadata intentionally advances again.

### 2.4d Standing Rule: Modeled-Flag Consumers Must Use the Shared Undefined-Flag-Use Helper

Every instruction, Irvine32 routine, debugger action, or future semantic feature that consumes a modeled flag value for behavior must use the shared undefined-flag-use checking path introduced by Phase 50B - Undefined Flag Use Diagnostics for Flag Consumers.

This rule applies when consuming any currently modeled flag whose validity metadata exists, including:

```text
CF
ZF
SF
OF
```

A future feature must not read a modeled flag directly and silently ignore validity metadata.

Required behavior:

- In `off` mode, the consumer uses the deterministic preserved flag value and emits no `undefined-flag-use` diagnostic.
- In `warn` mode, the consumer emits one `undefined-flag-use` warning at the consumer source location and continues using the deterministic preserved flag value.
- In `error` mode, the consumer emits one `undefined-flag-use` runtime error at the consumer source location and stops before consumer mutation.
- The diagnostic must identify the consumed flag or flags and include producer metadata when available.
- Error mode must preserve registers, flags, memory, Program Console output, and memory-change rows as if the consumer instruction did not execute.

Default user-facing behavior after Phase 53C - Default Teaching Diagnostics for Existing Warning Modes is `warn`.

Tests for any new flag-consuming feature must include:

- valid flag consumption with no warning;
- undefined flag consumption in default warning mode;
- explicit off mode;
- strict/error mode with no partial mutation;
- exact rendered Simulator Messages coverage for warning and error paths.


### 2.4d-1 Standing Rule: Memory-Capable Features Must Use Planned-Access Validation Before Mutation

`FULL_IMPLEMENTATION_SPEC.md` Sections 11.9 through 11.9.4 own the stable memory-validation model, parser/runtime memory-operand boundary, simple register-displacement whitespace policy, and diagnostic precedence. This guide section owns the phase-level implementation and test obligations for future memory-capable work.

Every future instruction, Irvine32 routine, debugger action, procedure feature, stack feature, string/buffer feature, or runtime-library feature that performs a simulated memory read or simulated memory write must integrate with the project's two-layer memory-validation model.

This rule applies even when the feature has only a memory source operand and does not write memory.

This rule also applies when the feature performs memory access implicitly, such as stack push/pop, call/return token handling, procedure frame setup, Irvine32 buffer routines, or future string instructions.

A memory-capable feature must perform both of these steps:

1. **Use central checked VM memory helpers for the final read or write.**

   The checked VM helpers remain the mandatory Level 1 authority for:

   - final effective address validation;
   - byte-range validation;
   - address overflow detection;
   - region containment;
   - read/write permissions;
   - mandatory `.CONST` write protection;
   - low-level runtime memory errors.

   Mandatory `.CONST` write protection remains a Level 1 permission/read-only failure and must not be reclassified as section-capacity, section-image, declared-object, or uninitialized-read validation.

2. **Expose the planned read or planned write to the source-run/Wasm/browser policy path before the instruction consumes a memory value or mutates visible state.**

   The planned-access path is required so optional educational policies can warn or stop consistently, including:

   - Level 2 section-capacity validation;
   - Level 3 section-image validation;
   - Level 4 declared-object validation;
   - uninitialized-origin read diagnostics;
   - future memory provenance, source-intent, taint, or bounds policies, if implemented later.

Calling a checked memory helper inside the executor is not enough when an optional strict policy is supposed to stop before the instruction uses a memory value or mutates state.

For strict policy failures, the feature must preserve the no-partial-mutation guarantee:

- all registers retain the values they had immediately before the failing instruction began;
- modeled flag bits retain the values they had immediately before the failing instruction began;
- Phase 50A flag-validity metadata retains the state it had immediately before the failing instruction began;
- memory is unchanged;
- Program Console output is unchanged;
- no memory-change row is created;
- no `execution-complete` message is emitted after the fatal diagnostic.

For warning policy paths, the feature may continue execution using the deterministic checked-memory value after emitting the warning, unless a lower-level mandatory VM memory check fails.

Required tests for every future memory-capable feature:

- default region-only behavior through mandatory checked VM memory helpers;
- invalid address, invalid range, invalid region, and invalid permission behavior through mandatory Level 1 diagnostics;
- direct `.CONST` write-protection precedence for memory writes, when the feature can write memory;
- cross-region write behavior for memory writes when a fixture can start in one VM region and end in another;
- cross-region `.CONST`-overlap read/write behavior when a fixture can start before `.CONST` and end inside `.CONST` storage;
- section-capacity warning and strict behavior when a suitable fixture can pass Level 1 but leave section capacity;
- section-image warning and strict behavior when a suitable fixture can pass Level 1 but leave section image;
- declared-object warning and strict behavior when a suitable fixture can pass Level 1 but cross declared-object bounds;
- uninitialized-read warning and strict behavior for memory reads;
- no-partial-mutation behavior for every strict or fatal planned-access failure;
- exact rendered Simulator Messages coverage for every new user-visible diagnostic path.

The cross-region protected-region-overlap case is a Level 1 diagnostic-quality requirement, not a new validation mode.

A future memory-capable feature must preserve the following Level 1 behavior:

- a single memory access must be wholly contained in one suitable VM memory region;
- the simulator must not stitch one access across independent VM regions;
- the simulator must not partially complete a read or write to produce a more precise diagnostic;
- fatal Level 1 failures must stop before the failing instruction consumes a read value or mutates visible state.

When a cross-region access intersects a protected memory region and the active layout metadata can identify that region, the diagnostic code must be:

```text
region-boundary-crossing
```

For this rule, **intersects** means that the requested final inclusive byte range shares at least one byte with the protected region's byte range.

The rendered message must use this shape:

```text
Cross-region memory <read|write> at <address> for <N> bytes. The memory address range <start>..<end> crosses/overlaps a protected memory region, <region-name>, that starts at <region-start>. This is not allowed; program stopped before access.
```

For current `.CONST` behavior:

- a cross-region read or write whose requested range intersects `.CONST` reports `region-boundary-crossing`;
- a direct or wholly-contained write overlapping `.CONST` reports `permission-denied`;
- a wholly-contained read from `.CONST` is allowed unless another mandatory or enabled strict validation rejects it;
- a cross-region read intersecting `.CONST` must not be described as a general `.CONST` read prohibition.

The protected-region start address in the message must come from runtime layout metadata. It must not be hardcoded to the fixed-layout default. This is required so the diagnostic remains correct under fixed educational layout, automatic deterministic layout, seeded randomized layout, and fresh randomized layout.

Phase 57L `.code` memory-access-denial rule: `.code` is now protected for source-level memory reads and writes. Cross-region accesses that intersect `.code` reuse the `region-boundary-crossing` diagnostic shape. The diagnostic substitutes `.code` for `.CONST`, uses the runtime `.code` base address from active layout metadata, and must not hardcode fixed-layout addresses.

Required test coverage does not need to cover every Cartesian combination of operand form and memory-validation policy unless the target phase explicitly requires that. It must cover every applicable policy at least once and every newly accepted memory operand class at least once.

If a future phase introduces a memory-capable feature but a listed policy cannot apply to that feature, the milestone report must say why. For example, a memory-source-only instruction has no `.CONST` write-protection or cross-region `.CONST`-overlap write case because it does not write memory.

Implementation reports for future memory-capable phases must explicitly state whether planned-read or planned-write collection was updated. Future assistants must not leave this as an implicit assumption.

### 2.4e Standing Rule: Keyword Matching and User-Symbol CASEMAP Policy Are Separate

Instruction mnemonics, register names, register aliases, directives, operators, data type names, `PTR` width names, virtual include names, and recognized Irvine32 routine names are matched case-insensitively.

User-defined symbol lookup is controlled by the current `OPTION CASEMAP` policy:

- default behavior is equivalent to `OPTION CASEMAP:ALL`;
- `OPTION CASEMAP:ALL` selects case-insensitive user-symbol matching;
- `OPTION CASEMAP:NONE` selects exact-case user-symbol matching;
- `OPTION CASEMAP:NOTPUBLIC` remains unsupported until public/external linkage semantics exist.

Do not apply `OPTION CASEMAP:NONE` to instruction mnemonics, directives, registers, virtual include names, or recognized Irvine32 routine names.

Example requirement:

```asm
OPTION CASEMAP:NONE
INCLUDE Irvine32.inc

.data
Value DWORD 1
value DWORD 2

.code
main PROC
    mov eax, Value
    mov ebx, value
    EXIT
main ENDP
END main
```

The two data symbols must remain distinct because user-symbol lookup is exact-case under `OPTION CASEMAP:NONE`. The `EXIT` virtual Irvine32 terminator must still be recognized case-insensitively because Irvine32 routine/terminator matching is not governed by user-symbol CASEMAP policy.

Future tests that combine Irvine32 names and mixed-case user symbols must prove both halves of this rule.

### 2.4f Standing Rule: Irvine32 Routine Recognition Must Use the Phase 41 Virtual Registry

Recognized Irvine32 routine names must be classified through the Phase 41 - Virtual Irvine32 Symbol Registry or a direct successor to that registry. Future phases must not add ad hoc Irvine32 string checks in unrelated parser, executor, diagnostic, or UI code.

The registry classification categories remain:

```text
supported virtual intrinsic now
planned Irvine32 routine later
known but explicitly unsupported in v1
Windows/API/external symbol
unknown symbol
```

Do not create a second independent Irvine32 classification taxonomy. If an implementation uses internal enum values, those enum values must map clearly to the Phase 41 categories in comments or tests.

Required category behavior:

- `supported virtual intrinsic now`: the routine or terminator has simulator-defined behavior implemented, tested, and documented.
- `planned Irvine32 routine later`: the routine is recognized, but executable behavior is deferred. If the owning future phase is known, diagnostics should name both phase number and phase title.
- `known but explicitly unsupported in v1`: the routine is recognized, but intentionally outside the first complete educational version.
- `Windows/API/external symbol`: the name implies Windows API execution, external linking, import libraries, PE behavior, or other behavior outside the simulator boundary.
- `unknown symbol`: the name is not recognized as an implemented instruction, user symbol, supported directive, or known virtual Irvine32 symbol.

Diagnostics for recognized-but-unimplemented Irvine32 routines must be emitted through Simulator Messages and must preserve source line, column, byte offset, and span length for the routine name when available.

Diagnostics must not use milestone-relative wording such as:

```text
unsupported by the current milestone
not implemented in this milestone
not supported in this milestone
```

Diagnostics must instead explain the stable reason:

- deferred to a named future phase;
- deferred to later Irvine32 routine phases;
- unavailable because it is outside the simulator boundary.

Examples:

```text
WriteString is a recognized Irvine32 routine, but executable Irvine32 output routines are deferred to Phase <N> - <phase title>.
```

```text
ReadString is a recognized Irvine32 routine, but Irvine32 input routines are deferred to later Irvine32 routine phases.
```

```text
Irvine32 file I/O routines are not supported because simulated programs cannot access the host filesystem.
```

This rule does not make any Irvine32 routine executable by itself. A future phase must explicitly implement the routine contract, parser/lowering behavior, runtime behavior, diagnostics, source-run JSON behavior, rendered Simulator Messages tests, and Program Console behavior.

When a future phase implements an Irvine32 routine, it must update the centralized registry classification from deferred or unsupported to implemented. It must not leave stale deferred diagnostics in place for newly implemented behavior.

Required tests for any future phase that touches Irvine32 routine recognition:

- implemented routine success path, if the phase implements a routine;
- recognized deferred routine diagnostic, if deferred routines remain visible;
- exact rendered Simulator Messages text for deferred routine diagnostics;
- source-location fields for the routine name;
- `OPTION CASEMAP:NONE` regression proving recognized Irvine32 names remain case-insensitive;
- user-symbol case-policy regression proving user symbols still obey `OPTION CASEMAP`;
- non-goal diagnostic for at least one host-filesystem, WinAPI, linker, PE, or external-symbol path when applicable.

### 2.4g Preserve Source Locations

Every parsed instruction and generated IR instruction must retain:

- Source file.
- Source line.
- Source column for real source tokens. If a diagnostic or metadata item is synthesized or source-less, use the documented nullable source-location representation instead of inventing a column.
- Original source text.

Diagnostics and debugger output depend on this.

### 2.5 Documentation Requirement

Every source file must have a file header block comment.

Every public function, struct, enum, and module-level API must use Doxygen-style triple-slash documentation.

Example file header:

```c
/*
 * @file vm_cpu.c
 * @brief CPU register and flag model for the MASM simulator VM.
 *
 * This module owns canonical register storage and exposes helpers for reading
 * and writing full registers and aliases such as AX, AH, and AL.
 */
```

Example symbol documentation:

```c
/// Writes a value to a register or register alias.
///
/// @param cpu CPU state to mutate.
/// @param reg Register identifier, including aliases such as AL or AX.
/// @param value Value to write. The value is masked to the register width.
/// @return true if the register was valid and the write succeeded.
bool vm_cpu_write_register(VmCpu *cpu, VmRegister reg, uint64_t value);
```

## 3. Recommended Repository Structure

Suggested structure:

```text
/project-root
  /src
    /core
      vm_cpu.c
      vm_cpu.h
      vm_flags.c
      vm_flags.h
      vm_memory.c
      vm_memory.h
      vm_ir.c
      vm_ir.h
      vm_exec.c
      vm_exec.h
      vm_debug.c
      vm_debug.h
      vm_console.c
      vm_console.h
      irvine32.c
      irvine32.h
    /parser
      lexer.c
      lexer.h
      parser.c
      parser.h
      symbols.c
      symbols.h
      data_layout.c
      data_layout.h
    /wasm
      wasm_api.c
      wasm_api.h
  /web
    index.html
    src/main.ts
    src/worker.ts
    src/state.ts
    src/url_state.ts
    src/editor/
      codemirror_setup.ts
      masm_language.ts
      editor_theme.ts
    src/ui/
  /tests
    core/
    parser/
    programs/
  /docs
    FULL_IMPLEMENTATION_SPEC.md
    INCREMENTAL_IMPLEMENTATION_GUIDE.md
```

Actual file names may vary, but module boundaries should remain clear.

## 4. Phase 0 - Project Skeleton

### Goal

Create a minimal static web project with a WebAssembly-ready C99 core and a basic browser UI.

### Tasks

1. Create repository structure.
2. Add build system for C99 to WebAssembly using Emscripten.
3. Add basic HTML/CSS/JS or TypeScript frontend.
4. Add Web Worker that loads the Wasm module.
5. Add a simple message protocol between UI and worker.
6. Add placeholder editor, Run button, Program Console, and Simulator Messages panel.
7. Add test runner setup.
8. Add Doxygen comment conventions to initial files.

### Acceptance Criteria

- Page loads locally.
- Worker initializes.
- UI can send a `PING` message to worker.
- Worker replies with `PONG`.
- Wasm module exports at least one test function.
- Tests can be run from command line.

## 5. Phase 1 - Core CPU Register Model

### Goal

Implement canonical register storage and alias read/write behavior.

### Tasks

1. Define CPU state for MASM32 mode.
2. Add canonical registers:
   - `EAX`, `EBX`, `ECX`, `EDX`
   - `ESI`, `EDI`, `EBP`, `ESP`
   - `EIP`
   - `EFLAGS`
3. Add alias support:
   - `AX`, `AH`, `AL`
   - `BX`, `BH`, `BL`
   - `CX`, `CH`, `CL`
   - `DX`, `DH`, `DL`
   - `SI`, `DI`, `BP`, `SP`
4. Implement register read/write helpers.
5. Add unit tests for aliases.

### Acceptance Criteria

- Writing `EAX = 0x12345678` gives:
  - `AX = 0x5678`
  - `AH = 0x56`
  - `AL = 0x78`
- Writing `AL = 0xFF` updates only the low byte of `EAX`.
- Writing `AH = 0xEE` updates bits 8-15 only.
- Register helpers are documented with Doxygen comments.

## 6. Phase 2 - Flags Model

### Goal

Implement initial flags needed for arithmetic and jumps.

### Tasks

1. Define flag bits:
   - `CF`
   - `ZF`
   - `SF`
   - `OF`
2. Add helpers to set, clear, and read flags.
3. Add arithmetic helper functions for common flag updates:
   - addition
   - subtraction
   - comparison
4. Add tests for edge cases.

### Acceptance Criteria

Test cases pass for:

```asm
0xFFFFFFFF + 1
0x7FFFFFFF + 1
0 - 1
5 - 5
```

Expected flags must be verified for `CF`, `ZF`, `SF`, and `OF`.

## 7. Phase 3 - Simulated Memory Regions

### Goal

Implement checked memory access for `.code`, `.data`, heap, and stack regions.

### Tasks

1. Define memory region structure:
   - base address
   - size
   - permissions
   - backing storage
2. Add default layout.
3. Implement checked read/write helpers for:
   - 8-bit
   - 16-bit
   - 32-bit
   - 64-bit storage, even if QWORD execution is later
4. Enforce bounds and permissions.
5. Detect unaligned reads/writes and return warnings while still allowing them.
6. Add memory-change recording hooks.
7. Add tests for valid and invalid access.

### Acceptance Criteria

- Reading/writing valid `.data` memory succeeds.
- Writing `.code` fails.
- Reading invalid addresses fails with structured error.
- Unaligned DWORD read succeeds and emits a warning.
- All memory APIs have Doxygen documentation.

## 8. Phase 4 - Minimal IR and Executor

### Goal

Execute a tiny hardcoded program through internal IR.

### Tasks

1. Define IR instruction structure.
2. Define operand types:
   - immediate
   - register
   - memory address
3. Implement VM step function.
4. Implement initial instructions:
   - `mov`
   - `add`
   - `sub`
5. Add last-step delta capture:
   - changed registers
   - changed flags
   - changed memory
6. Expose one test API from Wasm that runs a hardcoded IR program.

### Acceptance Criteria

Hardcoded program:

```asm
mov eax, 20
add eax, 22
```

produces:

```text
EAX = 42
```

Debugger delta reports:

```text
EAX: 0 -> 20
EAX: 20 -> 42
```

## 9. Phase 5 - Lexer

### Goal

Tokenize a small MASM-like source file.

### Tasks

1. Implement lexer for:
   - identifiers
   - registers
   - numbers, including decimal, hexadecimal, and signed forms
   - signed numeric tokens such as `-42`, `-0x2A`, and `-2Ah`
   - strings
   - quoted character/string literal tokens such as `'A'` and `'AB'`, with final literal semantics enabled in Phase 14
   - commas
   - brackets
   - plus and minus tokens where not part of a numeric literal
   - colons
   - comments beginning with `;`
   - line endings
2. Preserve source positions.
3. Add lexer diagnostics.
4. Add tests.

### Acceptance Criteria

Lexer handles:

```asm
.data
msg BYTE "Hello", 0
.code
mov eax, -1 ; comment
```

with correct token positions and signed numeric metadata.

## 10. Phase 6 - Parser for Minimal Code

### Goal

Parse a tiny `.code` program into IR.

### Tasks

1. Parse `.code` section.
2. Parse labels.
3. Parse simple instructions:
   - register/immediate
   - register/register
4. Parse `END main`.
5. Preserve line information.
6. Report unsupported syntax cleanly.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 20
    add eax, 22
main ENDP
END main
```

runs and produces `EAX = 42`.

## 11. Phase 7 - Basic UI Execution

### Goal

Run a small parsed program from the browser UI.

### Tasks

1. Editor text area.
2. Run button.
3. Worker receives source code.
4. Worker parses and executes.
5. UI displays final register state.
6. UI displays Simulator Messages.

### Acceptance Criteria

A user can paste:

```asm
.code
main PROC
    mov eax, 20
    add eax, 22
main ENDP
END main
```

click Run, and see `EAX = 42`.

## 12. Phase 8 - Data Section and Symbols

### Goal

Support `.data` declarations and symbolic memory references.

### Tasks

1. Parse `.data` section.
2. Support:
   - `BYTE`
   - `WORD`
   - `DWORD`
   - `QWORD`
   - `DB`
   - `DW`
   - `DD`
   - `DQ`
   - comma-separated initializers
   - strings
   - flat `DUP`
   - `?`
   - negative numeric initializers using two's-complement encoding after width validation
3. Build symbol table.
4. Lay out `.data` memory.
5. Support `OFFSET symbol`.
6. Support direct symbol memory writes:
   - `mov var, 100`
7. Add memory-change display by symbol.
8. Reject out-of-range immediates instead of silently truncating them.

### Acceptance Criteria

Program:

```asm
.data
var BYTE 0
.code
main PROC
    mov var, 100
main ENDP
END main
```

shows:

```text
var: 00h / 0 -> 64h / 100
```


## 13. Phase 9 - Constant Symbol Offsets

### Goal

Support constant byte-offset forms for data symbols before control flow is implemented.

This phase makes common textbook MASM array forms executable while preserving MASM byte-offset semantics.

### Tasks

1. Parse symbol-relative constant operands:
   - `nums[8]`
   - `[nums + 8]`
   - `[nums - 4]` where the resulting address remains valid
2. Treat bracketed symbol offsets as byte offsets, not element indexes.
3. Resolve the final absolute address through the existing symbol table.
4. Infer access width from the symbol declaration when no `PTR` override is present.
5. Support both source and destination memory operands where the executor already supports the width.
6. Return symbol-aware memory changes with byte offset and element index when aligned.
7. Preserve source line/column metadata in diagnostics.
8. Add tests for aligned, unaligned, negative, and out-of-bounds symbol offsets.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov nums[8], 100
    mov eax, nums[8]
main ENDP
END main
```

executes successfully and reports:

```text
EAX = 00000064h / 100
Memory Changes:
  nums + 8 DWORD
    byte offset: +8
    element index: 2
    00000000h / 0 -> 00000064h / 100
```

`nums[9]` for a DWORD access should execute as an unaligned access and emit a simulator warning.

## 14. Phase 10 - PTR Width Overrides

### Goal

Support explicit MASM memory-width overrides for memory operands.

### Tasks

1. Parse:
   - `BYTE PTR`
   - `WORD PTR`
   - `DWORD PTR`
   - `QWORD PTR`, as scaffolded layout/Extended-mode support until 64-bit execution is available
2. Apply the explicit width to symbol-relative memory operands.
3. Validate immediate ranges against the explicit width.
4. Ensure explicit width overrides do not silently conflict with unsupported execution widths.
5. Emit structured diagnostics for malformed or unsupported `PTR` forms.
6. Add tests for valid and invalid width overrides.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov BYTE PTR nums[3], 100
    mov DWORD PTR nums[8], 12345678h
main ENDP
END main
```

executes supported writes using the explicit access widths.

Invalid forms such as unsupported `QWORD PTR` execution in MASM32 mode produce structured diagnostics instead of truncation or crashes.

## 15. Phase 11 - Register-Indirect Memory Operands

### Goal

Support simple register-indirect addressing forms used by textbook array and buffer code.

### Tasks

1. Parse register-indirect operands:
   - `[esi]`
   - `[edi]`
   - `[ebx]`
   - `[ebp]`
2. Parse simple displacement forms:
   - `[esi + 4]`
   - `[esi - 4]`
3. Parse the currently defined simple symbol/register forms:
   - `array[esi]`
   - `[array + esi]`
4. Use `PTR` or known destination/source width to determine access size.
5. Route all reads/writes through the checked memory module.
6. Emit unaligned-access warnings through the existing warning path.
7. Reject unsupported scaled-index forms with explicit diagnostics.
8. Add tests for valid, invalid, unaligned, and out-of-bounds register-indirect accesses.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov esi, OFFSET nums
    mov DWORD PTR [esi + 8], 100
    mov eax, DWORD PTR [esi + 8]
main ENDP
END main
```

executes successfully and sets:

```text
EAX = 00000064h / 100
```

Unsupported scaled forms such as `[esi * 4]` produce explicit unsupported-feature diagnostics.

## 16. Phase 12 - TYPE Operator

### Goal

Implement the `TYPE` operator for data symbols.

### Tasks

1. Parse `TYPE symbol` in immediate contexts.
2. Return the declared element size in bytes.
3. Support `BYTE`, `WORD`, `DWORD`, `QWORD`, and their aliases.
4. Preserve existing `OFFSET symbol` behavior.
5. Add tests for scalar declarations, arrays, strings, and `DUP` declarations.
6. Emit structured diagnostics for unsupported `TYPE` expressions.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov eax, TYPE nums
main ENDP
END main
```

produces:

```text
EAX = 00000004h / 4
```

## 17. Phase 13 - LENGTHOF Operator

### Goal

Implement the `LENGTHOF` operator for data symbols.

### Tasks

1. Parse `LENGTHOF symbol` in immediate contexts.
2. Return the symbol element count.
3. Define and test behavior for scalar declarations, arrays, strings, and flat `DUP` declarations.
4. Preserve source diagnostics for unknown or unsupported symbols.
5. Add tests for byte, word, dword, qword, string, and `DUP` declarations.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
buf  BYTE "Hello", 0
.code
main PROC
    mov eax, LENGTHOF nums
    mov ebx, LENGTHOF buf
main ENDP
END main
```

produces:

```text
EAX = 0000000Ah / 10
EBX = 00000006h / 6
```

## 18. Phase 14 - SIZEOF Operator and Character Literals

### Goal

Implement `SIZEOF` and add single-character plus packed multi-character literals for byte-compatible and wider immediate/data contexts.

### Tasks

1. Parse `SIZEOF symbol` in immediate contexts.
2. Return total symbol byte size.
3. Finalize single-character literals such as `'A'` and `'0'` where a byte-compatible numeric literal is valid.
4. Implement packed multi-character literals such as `'AB'`, `'ABC'`, and `'ABCD'` where the destination width can hold the decoded bytes.
5. Use little-endian integer packing for immediate contexts: the first decoded character is the least significant byte, so `'AB'` becomes `4241h` and `'ABCD'` becomes `44434241h`.
6. Preserve byte-string behavior for `BYTE` / `DB` declarations, so `msg BYTE 'AB', 0` emits `41h, 42h, 00h`.
7. Allow packed scalar initializers for `WORD` / `DW`, `DWORD` / `DD`, and `QWORD` / `DQ` when the decoded byte count fits the element width.
8. Reject empty character literals, literals too wide for the destination width, and unsupported escape forms with structured diagnostics.
9. Add tests for `SIZEOF` with scalar declarations, strings, arrays, and flat `DUP`.
10. Add tests for character literals in data declarations, register immediates, memory immediates, accepted width boundaries, and rejected width-overflow cases.
11. Add diagnostics for unsupported expression forms such as `OFFSET arr + 4` until general expression parsing exists.

### Acceptance Criteria

Program:

```asm
.data
nums DWORD 10 DUP(0)
ch   BYTE 'A'
pair WORD 'AB'
tag  DWORD 'ABCD'
.code
main PROC
    mov eax, SIZEOF nums
    mov bl, ch
    mov cx, pair
    mov edx, 'ABCD'
main ENDP
END main
```

produces:

```text
EAX = 00000028h / 40
BL = 41h / 65
CX = 4241h / 16961
EDX = 44434241h / 1145258561
```

`mov al, 'AB'` must fail with a structured assembly diagnostic because two decoded bytes do not fit an 8-bit destination.

## 19. Phase 15 - Textbook MASM Compatibility Backlog Checkpoint

### Goal

Before moving into control flow, explicitly classify common MASM features as implemented now, scheduled soon, or intentionally deferred. This prevents unsupported textbook constructs from appearing as vague syntax errors.

### Tasks

1. Add or update a supported-syntax reference for the current implemented subset.
2. Add explicit unsupported-feature diagnostics for common-but-not-yet-supported constructs:
   - `.DATA?`
   - `.CONST`
   - `EQU` and `TEXTEQU`
   - `STRUCT`, `UNION`, and `RECORD`
   - `INVOKE`, `PROTO`, and `LOCAL`
   - `.IF`, `.WHILE`, `.REPEAT`, `.BREAK`, `.CONTINUE`
   - `MACRO` / `ENDM`
   - `INCLUDELIB`, `EXTERN`, `PUBLIC`, `COMM`
3. Add targeted parser tests that these unsupported constructs produce stable `unsupported-feature` diagnostics instead of confusing parse errors.
4. Note that signed integer data types `SBYTE`, `SWORD`, `SDWORD`, and `SQWORD` are scheduled after the diagnostic-cleanup phases and should not be treated as permanently unsupported.
5. Add backlog notes for additional non-integer data types: `REAL4`, `REAL8`, `REAL10`, `TBYTE`, and `FWORD`.
6. Add backlog notes for expression parser expansion: arithmetic, logical, relational, parenthesized, binary/octal literals, `.RADIX`, `HIGH`, `LOW`, `HIGHWORD`, `LOWWORD`, `SHORT`, and `THIS`.
7. Keep this as a documentation/diagnostics milestone; do not implement the full feature set here.

### Acceptance Criteria

Unsupported but recognizable MASM textbook constructs produce explicit diagnostics such as:

```text
Unsupported feature: STRUCT declarations are not supported yet.
Unsupported feature: INVOKE is not supported yet; use CALL when available.
Unsupported feature: MASM macro definitions are not supported yet.
```

The user-facing supported-syntax reference accurately reflects the current implementation.


## 20. Phase 16 - Diagnostic Plumbing and Lexer Error Surfacing

### Goal

Expose the real lexer and parser diagnostics through source-run and the browser UI instead of collapsing them into broad umbrella errors such as `lexer-failed`.

This phase is a diagnostics-quality milestone. It must not add new MASM syntax or instruction behavior.

### Tasks

1. Preserve all lexer diagnostics when lexing fails.
2. Convert lexer diagnostics into source-run `assembly-error` messages with:
   - diagnostic code,
   - line,
   - column,
   - byte offset, using `null` only when the diagnostic has no memory operand,
   - source span, using `null` only when the diagnostic originates from synthesized or source-less metadata,
   - clear user-facing message.
3. Replace generic messages such as:
   - `lexer-failed line 1, column 1: Lexer failed or produced diagnostics before parsing.`
   with the original lexer diagnostic location and reason.
4. Keep fatal capacity and infrastructure errors distinct:
   - token capacity exhausted,
   - diagnostic capacity exhausted,
   - source-text buffer exhausted,
   - internal parser invariant failure.
5. Surface parser diagnostics through the same structured message path without losing their specific codes.
6. Ensure source-run refuses execution when lexer or parser diagnostics exist.
7. Ensure Simulator Messages displays each surfaced diagnostic as a separate message.
8. Add tests for:
   - invalid character,
   - unterminated string,
   - unterminated character literal,
   - invalid hexadecimal literal,
   - numeric overflow,
   - malformed numeric text such as `123abc`,
   - parser diagnostics that should not be hidden behind generic messages.
9. Preserve existing Phase 15 unsupported-feature diagnostics.
10. Do not implement multi-diagnostic parser recovery in this phase beyond preserving already-produced diagnostics.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0xZZ
main ENDP
END main
```

produces a specific diagnostic similar to:

```text
[assembly-error] invalid-hex-literal line 3, column 14: Invalid hexadecimal literal.
```

Program:

```asm
.data
msg BYTE "Hello
.code
main PROC
END main
```

produces a specific diagnostic similar to:

```text
[assembly-error] unterminated-string line 2, column 10: Unterminated string literal.
```

The UI must not show `lexer-failed` as the only diagnostic when the lexer has already produced a more specific diagnostic.

## 21. Phase 17 - Multi-Diagnostic Unsupported-Feature Recovery

### Goal

Collect multiple recoverable unsupported-feature diagnostics in one pass without executing invalid programs.

This phase improves editor/compiler feedback. It should continue scanning after known unsupported constructs only when the parser can safely resynchronize.

### Tasks

1. Add a parser diagnostic-recovery mode for known unsupported constructs.
2. Continue scanning after recoverable line-level unsupported constructs, such as:
   - `INVOKE`,
   - `PROTO`,
   - `LOCAL`,
   - `EQU`,
   - `TEXTEQU`,
   - `INCLUDELIB`,
   - `EXTERN`,
   - `PUBLIC`,
   - `COMM`.
3. Add construct-aware block recovery for unsupported block-like constructs:
   - `STRUCT` / `ENDS`,
   - `UNION` / `ENDS`,
   - `MACRO` / `ENDM`,
   - `.IF` / `.ENDIF`,
   - `.WHILE` / `.ENDW` where recognized,
   - `.REPEAT` / `.UNTIL` or `.UNTILCXZ` where recognized.
4. Add section-aware recovery for unsupported sections such as:
   - `.DATA?`,
   - `.CONST`.
5. Define a maximum diagnostic count to prevent unbounded error output.
6. Preserve line, column, byte offset, and source span for each recovered diagnostic.
7. Keep fatal errors fatal. Do not attempt recovery after:
   - token capacity exhaustion,
   - diagnostic capacity exhaustion,
   - data image capacity exhaustion,
   - instruction buffer exhaustion,
   - unrecoverable lexer state,
   - internal parser invariant failure.
8. Ensure source-run refuses execution if any assembly diagnostic exists.
9. Add tests where one source file contains several unsupported constructs and the result reports all safely recoverable diagnostics.
10. Avoid noisy cascaded diagnostics from inside skipped unsupported blocks.

### Acceptance Criteria

Program:

```asm
.data
x DWORD 0

MyStruct STRUCT
    a DWORD ?
MyStruct ENDS

.code
main PROC
    INVOKE SomeProc
    .IF eax == 0
        mov ebx, 1
    .ENDIF
main ENDP
END main
```

reports multiple diagnostics, for example:

```text
unsupported-struct at line 4
unsupported-invoke at line 10
unsupported-high-level-if at line 11
```

No execution occurs.

The parser should not emit misleading extra diagnostics for the skipped body of `STRUCT`, `MACRO`, or high-level flow blocks.

## 22. Phase 18 - Signed Integer Data Types

### Goal

Implement textbook signed integer data declarations without enabling executable 64-bit operations in MASM32 Educational Mode.

### Tasks

1. Add parser/data-layout support for:
   - `SBYTE`
   - `SWORD`
   - `SDWORD`
   - `SQWORD`
2. Define storage sizes:
   - `SBYTE` = 1 byte
   - `SWORD` = 2 bytes
   - `SDWORD` = 4 bytes
   - `SQWORD` = 8 bytes
3. Validate initializers against signed ranges:
   - `SBYTE`: -128..127
   - `SWORD`: -32768..32767
   - `SDWORD`: -2147483648..2147483647
   - `SQWORD`: -9223372036854775808..9223372036854775807
4. Encode accepted negative values using two's-complement little-endian storage.
5. Support comma-separated initializers, flat `DUP`, `?`, and packed character literals where the decoded literal fits the signed declaration width.
6. Integrate signed types with existing symbol metadata so `TYPE`, `LENGTHOF`, and `SIZEOF` work correctly.
7. Keep `QWORD` and `SQWORD` data declarations, layout, and metadata available in MASM32 Educational Mode.
8. Continue rejecting executable `QWORD PTR` and `SQWORD PTR` memory operations in MASM32 Educational Mode until Extended 32-bit Mode enables them.
9. Do not add automatic sign-extension on ordinary `mov` from signed memory. Sign-extension instructions such as `movsx` are a later feature.
10. Add tests for valid initializers, range failures, metadata operators, `DUP`, packed literals, and unsupported executable 64-bit operations.

### Acceptance Criteria

Program:

```asm
.data
sb SBYTE -1
sw SWORD -2
sd SDWORD -3
sq SQWORD -4
arr SWORD 3 DUP(-1)
.code
main PROC
    mov eax, TYPE sq
    mov ebx, LENGTHOF arr
    mov ecx, SIZEOF arr
main ENDP
END main
```

produces:

```text
EAX = 00000008h / 8
EBX = 00000003h / 3
ECX = 00000006h / 6
```

`SBYTE 127` succeeds, `SBYTE 128` fails, `SBYTE -128` succeeds, and `SBYTE -129` fails.

`mov eax, sb` must not automatically sign-extend `SBYTE` storage. Ordinary `mov` uses the resolved operand width; explicit sign-extension instructions are added later.

## 23. Phase 19 - Sign and Zero Extension Instructions

### Goal

Implement the sign-extension and zero-extension instructions needed for signed integer data declarations and common textbook MASM programs.

### Tasks

1. Add parser, IR, and executor support for:
   - `movsx`
   - `movzx`
   - `cbw`
   - `cwde`
   - `cwd`
   - `cdq`
2. Support `movsx` and `movzx` with register destinations and 8-bit or 16-bit register/memory sources where the destination is wider than the source.
3. Implement accumulator conversion instructions:
   - `cbw`: sign-extend `AL` into `AX`
   - `cwde`: sign-extend `AX` into `EAX`
   - `cwd`: sign-extend `AX` into `DX:AX`
   - `cdq`: sign-extend `EAX` into `EDX:EAX`
4. Ensure ordinary `mov` from signed memory still does not sign-extend automatically.
5. Preserve source metadata and structured diagnostics for invalid widths and malformed operands.
6. Add tests for signed data symbols, unsigned byte data, register aliases, memory sources, and invalid destination/source widths.

### Acceptance Criteria

Program:

```asm
.data
x SBYTE -1
y BYTE  0FFh
.code
main PROC
    movsx eax, x
    movzx ebx, y
main ENDP
END main
```

produces:

```text
EAX = FFFFFFFFh / 4294967295
EBX = 000000FFh / 255
```

Additional tests verify `cbw`, `cwde`, `cwd`, and `cdq` using negative accumulator values.

## 24. Phase 20 - Exchange, Negation, and NOP

### Goal

Implement small but common utility instructions used in introductory assembly examples and debugger-oriented programs.

### Tasks

1. Add parser, IR, and executor support for:
   - `xchg`
   - `neg`
   - `nop`
2. Support `xchg reg, reg`, `xchg reg, mem`, and `xchg mem, reg` where operand widths match and existing memory rules make the memory access unambiguous.
3. Implement `neg` for register and memory destinations using arithmetic-width-aware flag behavior.
4. Define `nop` as an instruction that advances execution but does not mutate registers, flags, memory, or console state.
5. Ensure `xchg` does not modify flags.
6. Add tests for register exchange, memory exchange, negation edge cases, flag behavior, and no-op last-step deltas.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 5
    mov ebx, 10
    xchg eax, ebx
    neg eax
    nop
main ENDP
END main
```

leaves:

```text
EAX = FFFFFFF6h / 4294967286
EBX = 00000005h / 5
```

Tests verify that `xchg` and `nop` do not modify tracked flags.

## 25. Phase 21 - Carry/Borrow Arithmetic and Carry Flag Control

### Goal

Implement carry-aware arithmetic and explicit carry flag manipulation for multi-word arithmetic examples.

### Tasks

1. Add parser, IR, and executor support for:
   - `adc`
   - `sbb`
   - `clc`
   - `stc`
   - `cmc`
2. Support `adc` and `sbb` for register destinations and memory destinations where existing width rules are unambiguous.
3. Support register, immediate, and memory sources where compatible with the destination width.
4. Use the current `CF` as an input to `adc` and `sbb`.
5. Update `CF`, `ZF`, `SF`, and `OF` according to the supported operand width.
6. Implement `clc`, `stc`, and `cmc` as carry-flag-only instructions.
7. Add tests for carry propagation, borrow propagation, flag-control instructions, immediate ranges, and memory operands.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0FFFFFFFFh
    add eax, 1
    mov ebx, 0
    adc ebx, 0
main ENDP
END main
```

leaves:

```text
EAX = 00000000h / 0
EBX = 00000001h / 1
```

Separate tests verify `stc`, `clc`, and `cmc` behavior.

## 26. Phase 22 - TEST Instruction

### Goal

Implement `test`, a common flag-setting instruction used before conditional jumps, while preserving the simulator's explicit/inferable memory-width rule.

### Tasks

1. Add parser, IR, and executor support for `test`.
2. Support register/register, register/immediate, register/memory, memory/register, and memory/immediate forms where existing width rules are unambiguous.
3. For `test mem, imm`, require an explicit or inferable memory width:
   - valid: `test value, 1` where `value` is a typed data symbol;
   - valid: `test nums[8], 1` where symbol metadata gives the width;
   - valid: `test BYTE PTR [esi], 1`;
   - valid: `test WORD PTR [esi], 1`;
   - valid: `test DWORD PTR [esi], 1`;
   - rejected: `test [esi], 1`;
   - rejected: `test [esi + 4], 1`.

   These rejections are MASM-compatible and must not be described as temporary milestone limitations. The diagnostic should explain that the memory access width is ambiguous and that the user should write `BYTE PTR`, `WORD PTR`, or `DWORD PTR`.
4. Compute bitwise AND for flag updates only; do not store the result.
5. Update `ZF` and `SF`.
6. Clear `CF` and `OF`.
7. Preserve source metadata and emit structured diagnostics for invalid widths or malformed operands.
8. Add tests proving that destination operands are not modified.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 0
    test eax, eax
main ENDP
END main
```

executes successfully and sets:

```text
ZF = 1
```

A separate test verifies that `test eax, 0FFh` does not change `EAX`.

Additional tests must verify memory/immediate width behavior:

```asm
.data
value DWORD 0F0F0F0Fh
nums  DWORD 10 DUP(0)

.code
main PROC
    test value, 0FFh
    test nums[0], 0FFh
    test DWORD PTR [esi], 1
main ENDP
END main
```

The above forms are accepted when the memory address is valid.

These forms must produce structured assembly diagnostics:

```asm
test [esi], 1
test [esi + 4], 1
```

because the memory access width is ambiguous.

The diagnostic should use a stable code such as:

```text
ambiguous-memory-width
```

or:

```text
invalid-instruction-operands
```

It should not use `unsupported-syntax` with wording such as "unsupported by the current milestone."

## 27. Phase 23 - Signed PTR Width Aliases

### Goal

Correct the signed integer `PTR` width gap before adding more memory-capable instructions.

Signed data declarations already exist. MASM-compatible `PTR` handling must therefore accept signed integer type names anywhere their unsigned-width counterparts are legal.

### Tasks

1. Parse and accept signed `PTR` width overrides:
   - `SBYTE PTR`
   - `SWORD PTR`
   - `SDWORD PTR`
   - `SQWORD PTR`

2. Map signed `PTR` aliases to access widths:

   ```text
   SBYTE PTR  -> 1 byte
   SWORD PTR  -> 2 bytes
   SDWORD PTR -> 4 bytes
   SQWORD PTR -> 8 bytes
   ```

3. Preserve existing unsigned `PTR` behavior:
   - `BYTE PTR`
   - `WORD PTR`
   - `DWORD PTR`
   - `QWORD PTR`

4. Treat `SBYTE PTR`, `SWORD PTR`, and `SDWORD PTR` as executable 8-bit, 16-bit, and 32-bit memory-width overrides in MASM32 Educational Mode.

5. Continue recognizing `QWORD PTR` and `SQWORD PTR`, but reject executable 64-bit memory operations in MASM32 Educational Mode until Extended 32-bit Mode enables them.

6. Preserve signedness metadata for all signed declarations for diagnostics and later display, but do not make ordinary `mov` sign-extend.

7. Update unsupported-PTR diagnostics so valid signed width names are not reported as unsupported.

8. Add parser, source-run, and regression tests.

### Acceptance Criteria

This program executes and reads signed data bytes/words/dwords by width without implicit sign extension:

```asm
.data
b SBYTE -1
w SWORD -2
d SDWORD -3
.code
main PROC
    mov al, SBYTE PTR b
    mov bx, SWORD PTR w
    mov ecx, SDWORD PTR d
main ENDP
END main
```

Expected final state:

```text
AL = FFh
BX = FFFEh
ECX = FFFFFFFDh
```

This program writes through a signed `PTR` alias:

```asm
.data
buf BYTE 4 DUP(0)
.code
main PROC
    mov esi, OFFSET buf
    mov SBYTE PTR [esi], -1
    mov al, BYTE PTR [esi]
main ENDP
END main
```

Expected final state:

```text
AL = FFh
```

This remains rejected in MASM32 Educational Mode:

```asm
.data
q SQWORD -1
.code
main PROC
    mov eax, SQWORD PTR q
main ENDP
END main
```

Expected diagnostic category:

```text
unsupported-runtime-feature or unsupported-ptr-width
```

The message should explain that executable QWORD/SQWORD memory operations are deferred until Extended 32-bit Mode.

## 28. Phase 24 - All-GPR Register-Indirect Base Registers

### Goal

Expand simple register-indirect addressing from the earlier minimal subset to MASM-compatible 32-bit base registers.

### Tasks

1. Support all 32-bit general-purpose registers as simple memory base registers:
   - `[eax]`
   - `[ebx]`
   - `[ecx]`
   - `[edx]`
   - `[esi]`
   - `[edi]`
   - `[ebp]`
   - `[esp]`

2. Support simple displacement forms for all base registers:
   - `[eax + 4]`
   - `[ecx - 4]`
   - `[esp + 8]`

3. Preserve existing supported forms:
   - `[esi]`
   - `[edi]`
   - `[ebx]`
   - `[ebp]`
   - `[esi + 4]`
   - `[esi - 4]`
   - `array[esi]`
   - `[array + esi]`

4. Treat `ESP` as a valid base register.

5. Keep scaled-index forms rejected:
   - `[eax * 4]`
   - `[base + index * scale]`
   - `array[esi * 4]`

6. Preserve runtime memory safety. Invalid addresses produced by valid addressing syntax must fail at runtime through checked memory diagnostics, not at parse time.

7. Add parser, executor/source-run, and diagnostic tests for every newly accepted base register.

### Acceptance Criteria

This program executes:

```asm
.data
nums DWORD 4 DUP(0)
.code
main PROC
    mov eax, OFFSET nums
    mov DWORD PTR [eax], 100
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Expected final state:

```text
EBX = 00000064h / 100
```

This program parses but fails at runtime because address zero is invalid:

```asm
.code
main PROC
    mov eax, 0
    test [eax], eax
main ENDP
END main
```

Expected behavior:

```text
No assembly unsupported-register-indirect-base diagnostic.
Runtime memory diagnostic for address 00000000h.
```

This remains rejected as unsupported scaled-index syntax:

```asm
.code
main PROC
    mov eax, DWORD PTR [eax * 4]
main ENDP
END main
```

## 29. Phase 25 - Global Memory Width Resolution Rules

### Goal

Centralize memory-width resolution so all current and future instruction parsers use the same MASM-compatible rules.

### Tasks

1. Add or refactor a shared memory-width resolution helper for parser/instruction validation.

2. Use the helper for all currently implemented memory-capable instructions:
   - `mov`
   - `add`
   - `sub`
   - `adc`
   - `sbb`
   - `xchg`
   - `neg`
   - `test`

3. Define width sources:
   - explicit `PTR` override;
   - declared symbol metadata;
   - symbol-relative operand metadata;
   - register operand in the same instruction when the instruction form unambiguously supplies width;
   - instruction-specific implicit width where defined.

4. Reject ambiguous memory/immediate forms with a stable diagnostic code:

   ```text
   ambiguous-memory-width
   ```

5. Use user-facing wording similar to:

   ```text
   Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.
   ```

6. Ensure MASM-invalid ambiguous forms are not classified as `unsupported-feature` or `unsupported-syntax` caused by an implementation milestone.

7. Add regression tests for all current instructions that accept memory operands.

### Acceptance Criteria

These are accepted because a register operand supplies width:

```asm
.code
main PROC
    mov eax, 0
    test [eax], eax
    test [eax], ax
    test [eax], al
main ENDP
END main
```

The program may fail at runtime if `EAX` points outside simulated memory, but it must not fail assembly because of ambiguous memory width.

These are accepted because `PTR` supplies width:

```asm
.code
main PROC
    mov eax, 0
    test DWORD PTR [eax], 1
    test WORD PTR [eax], 1
    test BYTE PTR [eax], 1
main ENDP
END main
```

These are rejected as ambiguous:

```asm
.code
main PROC
    mov eax, 0
    test [eax], 1
    mov [eax], 1
    add [eax], 1
    sbb [eax], 1
main ENDP
END main
```

Expected diagnostic code:

```text
ambiguous-memory-width
```

## 30. Phase 26 - MASM Header Directives

### Goal

Accept common MASM32 textbook header directives and improve directive diagnostics before continuing with more instruction milestones.

This phase lets users paste ordinary MASM32/Irvine32 classroom programs without deleting standard setup lines such as `.386`, `.model flat, stdcall`, `.stack`, `OPTION CASEMAP:NONE`, and `INCLUDE Irvine32.inc`.

### Tasks

1. Parse and accept processor compatibility directives as no-ops:
   - `.386`
   - `.486`
   - `.586`
   - `.686`

2. Parse and accept:

   ```asm
   .model flat, stdcall
   ```

   as MASM32 Educational Mode compatibility syntax.

3. Reject unsupported `.model` forms with structured diagnostics.

4. Parse `.stack` with optional size:

   ```asm
   .stack
   .stack 4096
   .stack 1000h
   ```

5. Store requested stack size as parser/project metadata in the existing metadata model. If the metadata model cannot represent it yet, add the smallest explicit field needed for `.stack` metadata. Do not apply runtime stack behavior in this phase.

6. Parse and accept built-in virtual includes:

   ```asm
   INCLUDE Irvine32.inc
   INCLUDE Macros.inc
   ```

7. Treat `INCLUDE Irvine32.inc` as accepted even before Irvine32 routines are executable.

8. Treat `INCLUDE Macros.inc` as accepted virtual no-op for paste compatibility. Macro invocations remain unsupported until the selected macro compatibility phase.

9. Reject unknown include files with structured diagnostics such as:

   ```text
   unsupported-include
   ```

10. Parse and classify `OPTION CASEMAP` forms as MASM32 header compatibility syntax:

   ```asm
   OPTION CASEMAP:ALL
   OPTION CASEMAP:NONE
   OPTION CASEMAP:NOTPUBLIC
   ```

11. Define the intended case policy, with full semantic implementation completed by Phase 35A:
    - instructions, registers, register aliases, directives, operators, data types, `PTR` width names, virtual include names, and recognized Irvine32 routine names are case-insensitive;
    - user-defined symbols are case-insensitive by default;
    - `OPTION CASEMAP:ALL` explicitly selects the default user-symbol policy and is accepted;
    - `OPTION CASEMAP:NONE` selects exact-case user-symbol policy and is accepted;
    - `OPTION CASEMAP:NOTPUBLIC` is recognized but unsupported until public/external linkage semantics exist;
    - any other `CASEMAP` value is invalid.

    Phase 26 accepted header compatibility syntax before the full user-symbol case-policy correction existed. Phase 35A corrects the earlier contradictory wording and implements the missing semantic behavior. Phase 26 must not be read as specifying case-sensitive user symbols by default.

12. Reject unsupported or invalid `OPTION` forms with structured diagnostics:
    - `OPTION CASEMAP:NOTPUBLIC` should produce an `unsupported-option` diagnostic explaining that public/external linkage semantics are not implemented;
    - invalid `CASEMAP` values should produce `invalid-option-value` where available, or `unsupported-option` with clear wording;
    - unrelated unsupported option families such as `OPTION NOKEYWORD`, `OPTION DOTNAME`, `OPTION NODOTNAME`, and unsupported `OPTION LANGUAGE` forms remain rejected.

13. Parse and accept listing/documentation directives as no-ops:
    - `TITLE`
    - `SUBTITLE`
    - `PAGE`

14. Recognize but reject or defer:
    - `COMMENT`
    - `ECHO`
    - `.LIST`
    - `.NOLIST`
    - `.CREF`
    - `.NOCREF`
    - `.TFCOND`

15. Add explicit unsupported-feature diagnostics for:
    - `ASSUME`
    - `.STARTUP`
    - `.EXIT`
    - `.DOSSEG`
    - `.FARDATA`
    - `.FARDATA?`
    - `ALIGN`
    - `EVEN`
    - `LABEL`
    - `ORG`
    - `EXTERNDEF`
    - `EXTRN`
    - broader directive families listed in the full specification.

16. Preserve line, column, byte offset, and span length for every directive diagnostic.

17. Do not implement real include file loading, Windows API behavior, stack runtime behavior, object-file linkage, listing generation, macro expansion, segment assumptions, or conditional assembly semantics.

### Acceptance Criteria

This program parses and executes the body normally:

```asm
.386
.model flat, stdcall
.stack 4096
OPTION CASEMAP:NONE
INCLUDE Irvine32.inc

.data
msg BYTE "Hello", 0
.code
main PROC
    mov edx, OFFSET msg
main ENDP
END main
```

This program also parses because `Macros.inc` is virtual no-op:

```asm
.386
.model flat, stdcall
INCLUDE Irvine32.inc
INCLUDE Macros.inc
.code
main PROC
END main
```

This program is rejected:

```asm
.model small, c
.code
main PROC
END main
```

Expected diagnostic:

```text
unsupported-model
```

This program is rejected:

```asm
INCLUDE Windows.inc
.code
main PROC
END main
```

Expected diagnostic:

```text
unsupported-include
```

## 31. Phase 27 - Additional Data Sections: .DATA? and .CONST

### Goal

Support common MASM simplified data sections that are important for textbook paste compatibility.

### Tasks

1. Parse `.DATA?` as an uninitialized data section.

2. Store `.DATA?` declarations deterministically as zero-filled bytes while retaining metadata that the bytes were originally uninitialized.

3. Parse `.CONST` as an initialized constant data section.

4. Store `.CONST` symbols with read-only metadata or in a read-only memory region.

5. Reject statically known writes to `.CONST` symbols when the destination directly resolves to `.CONST`. Computed or indirect `.CONST` write overlap must still fail through checked runtime memory permissions.

6. Ensure runtime writes to `.CONST` fail through checked memory permissions if `.CONST` has a read-only region.

7. Preserve `.data` behavior and symbol ordering.

8. Add parser, data-layout, source-run, and memory-change tests.

### Acceptance Criteria

```asm
.DATA?
buf BYTE 16 DUP(?)
.data
x DWORD 1
.CONST
limit DWORD 10
.code
main PROC
    mov eax, SIZEOF buf
    mov ebx, limit
main ENDP
END main
```

Expected:

```text
EAX = 00000010h / 16
EBX = 0000000Ah / 10
```

This should be rejected or fail at runtime through read-only memory protection:

```asm
.CONST
limit DWORD 10
.code
main PROC
    mov limit, 20
main ENDP
END main
```

## 32. Phase 28 - Numeric Equates and Simple Constant Expressions

### Goal

Add numeric equates and the first real MASM constant-expression subset.

### Tasks

1. Parse numeric equates:

   ```asm
   COUNT = 10
   MAX_SIZE EQU 128
   ```

2. Store equates in a symbol table distinct from data labels.

3. Support simple constant expressions:
   - numeric literals;
   - equate identifiers;
   - unary `+` and `-`;
   - parentheses;
   - binary `+` and `-`.

4. Allow these expressions in:
   - instruction immediates;
   - data initializers;
   - `DUP` counts;
   - symbol offsets;
   - `.stack size`;
   - `OFFSET symbol + constant` where the result is static.

5. Reject text equates and macro-style substitution unless explicitly handled in a later phase.

6. Add diagnostics for recursive, unknown, or non-constant equates.

### Acceptance Criteria

```asm
COUNT = 4
EXTRA EQU 2
.data
arr DWORD COUNT DUP(0)
.code
main PROC
    mov eax, COUNT + EXTRA
    mov ebx, SIZEOF arr
main ENDP
END main
```

Expected:

```text
EAX = 00000006h / 6
EBX = 00000010h / 16
```

## 33. Phase 29 - Extended Constant Expressions

### Goal

Expand compile-time expression support for common MASM textbook constants.

### Tasks

1. Add constant-expression operators:
   - `*`
   - `/`
   - `MOD`
   - `SHL`
   - `SHR`
   - `AND`
   - `OR`
   - `XOR`
   - `NOT`

2. Add byte/word extraction operators:
   - `HIGH`
   - `LOW`
   - `HIGHWORD`
   - `LOWWORD`

3. Define precedence and associativity explicitly in tests.

4. Keep runtime high-level condition operators out of this phase.

5. Reject non-constant expressions where constants are required.

### Acceptance Criteria

```asm
COUNT = 4 * 3
MASK  EQU 1 SHL 7
.data
arr BYTE COUNT DUP(0)
.code
main PROC
    mov eax, COUNT
    mov ebx, MASK
    mov ecx, LOWWORD 12345678h
main ENDP
END main
```

Expected:

```text
EAX = 0000000Ch / 12
EBX = 00000080h / 128
ECX = 00005678h / 22136
```

## 34. Phase 30 - Nested DUP and Initializer Expressions

### Goal

Support nested `DUP` and expression-backed initializers for textbook array declarations.

### Tasks

1. Parse nested `DUP` expressions, for example:

   ```asm
   matrix DWORD 3 DUP(4 DUP(0))
   ```

2. Support equates and constant expressions in `DUP` counts.

3. Support constant expressions in data initializers.

4. Preserve deterministic `?` storage behavior.

5. Add capacity checks and diagnostics for excessive expansion.

6. Add tests for nested byte, word, dword, signed, string, and `?` initializers.

### Acceptance Criteria

```asm
ROWS = 3
COLS = 4
.data
matrix DWORD ROWS DUP(COLS DUP(0))
.code
main PROC
    mov eax, LENGTHOF matrix
    mov ebx, SIZEOF matrix
main ENDP
END main
```

Expected:

```text
EAX = 0000000Ch / 12
EBX = 00000030h / 48
```

## Post-Milestone-30 Roadmap Overhaul Notes

The following phases replace the previous post-Milestone-30 roadmap. Phase numbers are canonical from this point forward and are renumbered sequentially starting at Phase 31. Historical planning-batch labels are intentionally omitted from the source-of-truth guide.

The completed phases 0-30 above remain unchanged. The post-30 phases below implement the accepted split-roadmap policy: one focused implementation task per phase, explicit accepted/rejected behavior, source-span-aware diagnostics, rendered Simulator Messages tests for user-visible diagnostics, central checked memory access, and no early implementation of future features.

### Post-30 global implementation requirements

- All memory reads and writes go through checked VM memory helpers.
- Every new user-visible diagnostic must have structured diagnostic tests and rendered Simulator Messages tests.
- Every phase that introduces parser/runtime behavior must specify accepted syntax, rejected syntax, source-span target, no-partial-mutation behavior on failure, required tests, and non-goals.
- Program Console and Simulator Messages remain separate streams.
- Worker protocol payloads must be structured-clone-safe and JSON-compatible unless a later phase explicitly introduces a binary payload type.
- Backend source byte offsets remain authoritative; browser editor mappings to CodeMirror UTF-16 offsets must go through the tested mapping utility.
- UI and settings diagnostics are rendered through the same Simulator Messages formatter using stable `ui-warning` or `ui-error` categories.
- Local preferences and simulator settings are distinct from share-safe project state; transient runtime state is never encoded in share URLs.
- Extended 32-bit Mode remains part of v1, but true x64 MASM, `ml64`, Windows ABI, PE loading, and WinAPI behavior remain non-goals for this v1 guide. Do not treat them as future v1 roadmap items unless a later reviewed specification/guide revision deliberately changes the project scope.

### Post-30 phase-reference hygiene

The post-30 roadmap has been renumbered and corrected over time. A stale phase number can mislead future implementation sessions.

Rules:

- Cross-references should include both the phase number and the phase title whenever practical.
- If a phase number and named feature disagree, the named feature and surrounding scope determine intent.
- Correcting a stale cross-reference is a documentation fix unless the replacement text explicitly changes accepted syntax, runtime behavior, diagnostics, or tests.
- Do not implement a future feature early because a stale dependency names the wrong phase.
- Do not renumber existing phases to fix a stale reference. Fix the reference in place.
- Before applying any global replacement, inspect the surrounding heading and confirm the reference is part of the active roadmap text, not a historical note, audit note, or stale-reference test fixture.
- Do not rewrite completed phase history as if it originally used the corrected number. Correct only active forward-looking requirements, dependencies, tests, and examples.
- When adding a new cross-reference, prefer this form:

  ```text
  Phase 72 - Call Depth Limit and Call Trace Diagnostics
  ```

  rather than:

  ```text
  Phase 72
  ```

### Static phase-reference audit requirement

Static documentation checks should verify active roadmap phase references against the current phase-heading map.

The check should build a map from every active guide heading of this form:

```text
Phase <N or suffix> - <current phase title>
```

Then it should scan active guide text for phase references and apply these rules:

1. A reference written as `Phase <N> - <Title>` must match the current heading title for `Phase <N>` exactly, ignoring only Markdown emphasis and harmless whitespace differences.

2. A reference written as `Phase <N> <description>` without a dash should be flagged when `<description>` contains a known feature keyword that conflicts with the current title for `Phase <N>`.

   The static check may use an explicit denylist for known stale phrases and a warning-only report for uncertain matches. Uncertain matches should be reviewed manually rather than automatically rewritten.

3. Bare phase ranges such as `Phases 95-98` should be flagged unless the surrounding text lists the specific phase titles and explains why the range is still valid.

4. Known stale active-roadmap phrases should fail explicitly:

   ```text
   PROC USES eax ebx       ; Phase 59
   signed-vs-unsigned difference from Phase 60
   Phase 67 ReadString completion
   Phase 95 virtual macro registry
   Phase 95 encoder
   Phase 91/92
   memory form rejected until Phase 95
   Phase 95 diagnostic model
   Phase 96 source-offset mapper
   Phase 97 diagnostic marker rendering
   Phase 98 click-to-source navigation
   Phase 100 highlight data model
   Phase 105 debugger editor accessibility
   Phase 106 accessibility audit harness
   Phase 107 responsive layout
   Phase 74 - RET imm16 Caller Cleanup
   Phase 109 - PF/AF Storage and CPU Helper Scaffolding
   ```

5. The static audit applies to active forward-looking guide text only, including:
   - phase goals;
   - dependencies;
   - tasks;
   - accepted syntax;
   - rejected syntax;
   - required tests;
   - acceptance criteria;
   - non-goals;
   - future-work notes.

6. The static audit must not require changes to historical milestone reports, audit reports, changelog excerpts, or intentionally quoted stale examples unless those excerpts are promoted into active roadmap authority.

7. If historical text is copied into the active guide, it must be updated to current phase numbering and current phase titles before it becomes source-of-truth text.

8. When this check fails in active roadmap text, the normal fix is to correct the reference in place. Do not renumber phases, do not rewrite completed milestone history, and do not implement a future feature early merely because a stale reference points to the wrong phase.

9. If the correct dependency is unclear, prefer the named feature and surrounding phase scope over the stale phase number. The corrected reference must include both the current phase number and current phase title.

Static documentation checks should also flag shared-section headings that name bare phase ranges without phase titles, such as `for Phases 57-61`, unless the surrounding text lists the specific phase titles and explains why the range is still valid. Preferred shared-section headings should describe the behavior or policy directly, for example `Shared Direct Branch Target Classification`, rather than a bare numeric range.

#### Markdown heading number versus canonical phase identifier

The leading number in a Markdown heading is a document-section locator. It is not always the same as the canonical implementation phase identifier.

The canonical implementation phase identifier is the text that begins with `Phase`.

For example, in this heading:

```text
## 60A. Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics
```

the implementation phase is:

```text
Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics
```

not:

```text
Phase 60A
```

Future assistants must follow these rules:

- Use the `Phase <number or suffix> - <title>` text as the phase identity.
- Treat the leading Markdown heading number as only a document navigation aid.
- Do not create, rename, or implement a phase based only on the leading Markdown heading number.
- Do not renumber later phases merely to make Markdown heading numbers and phase identifiers match.
- When a corrective phase such as `35A`, `50A`, `50B`, `52A`, `53A`, `53B`, `53C`, `53D`, `53E`, or `56A` is inserted, preserve later canonical phase identifiers unless the user explicitly requests a roadmap renumbering.
- If a heading number and phase identifier appear to conflict, use the phase identifier, phase title, local scope text, and current canonical guide sequence to determine the intended owner.

Static documentation checks may warn about bare phase-number references where the phase title is absent. Static checks must not require Markdown heading locator numbers to match canonical phase identifiers.

### Required test categories at release gate

The final v1 release gate must include native C tests, parser tests, source-run JSON tests, Node diagnostic-rendering tests, browser/worker smoke tests, static documentation checks, and Wasm/Emscripten builds. Exact command names are repository-dependent until the test runner matures.

## 35. Phase 31 - Native Diagnostic Rendering Harness

### Goal

Make final user-facing Simulator Messages text testable from native/Node tests without requiring an Emscripten build or a manual browser session.

This is a diagnostic-quality and test-infrastructure phase only. It must not add new MASM syntax, new instructions, new runtime semantics, new UI behavior, or future milestone features. Phase 30 nested `DUP` support is considered complete before this phase begins.

### Dependencies

- Existing C source-run JSON path used by the Wasm source-run API.
- Existing browser Simulator Messages formatter.
- Existing native test build.
- Existing Node/web test environment or a minimal Node test script.

### Tasks

1. Add or expose a native diagnostic JSON producer.
   - It must compile with the native C test toolchain.
   - It must link the same parser, source-run, executor, diagnostic, and Wasm-facing API code used by browser source execution.
   - It should call `masm32_sim_wasm_run_source_json` or the current equivalent source-run JSON entry point.
   - It must not require Emscripten.
   - Emscripten export annotations must remain no-ops in native builds.
   - It must accept fixture source through a test file, command-line argument, or standard input.
   - It must print only the raw JSON payload on stdout unless an explicit debug flag is supplied.

2. Extract or confirm a pure web formatter module.
   - The Simulator Messages formatter must be importable by Node tests.
   - Importing the formatter must not initialize the DOM, create a Worker, load Wasm, attach event listeners, or mutate browser state.
   - If formatter logic currently lives in UI bootstrap code, move it to a side-effect-free module and update the UI to call that module.
   - The Node test must use this real formatter module. It must not duplicate formatting rules in a separate test-only implementation.

3. Add a Node-based diagnostic rendering test harness.
   - It must run the native diagnostic JSON producer for each fixture.
   - It must parse returned JSON.
   - It must validate structured diagnostic fields.
   - It must format messages with the real browser formatter.
   - It must compare exact expected Simulator Messages text for stable cases.
   - Stable fixtures must use exact full rendered-line equality, not only substring checks.
   - Partial `must include` assertions are allowed only for explicitly unstable legacy diagnostics, and each such fixture must document why the exact wording is not frozen yet.
   - On failure, it must print fixture source, raw JSON, rendered text, expected text, and diff context.

4. Add required golden fixtures:
   - invalid hex literal;
   - unterminated string literal;
   - unknown symbol;
   - unsupported feature from a recognized deferred directive or construct;
   - ambiguous memory width;
   - invalid runtime memory address;
   - read-only `.CONST` write through the runtime memory path;
   - unaligned memory warning;
   - successful execution message.

5. Add at least one multi-diagnostic fixture.
   - Verify stable ordering.
   - Verify no redundant cascade diagnostic appears when the parser already emitted the intended primary diagnostic.
   - Verify `execution-complete` is absent when diagnostics prevent execution.

6. Add stale-artifact guard wording to tests or docs.
   - Native/Node diagnostic tests verify the native source-run JSON path and browser formatter module.
   - They do not prove that `web/dist` was rebuilt.
   - Manual browser testing remains required when Wasm artifacts, Worker loading, protocol shape, DOM rendering, or editor diagnostic integration changes.

7. Integrate the harness into the normal test command.
   - `scripts/run_tests.py` or the current aggregate test runner must run the native diagnostic JSON producer tests and the Node formatter tests.
   - Test output must clearly identify native/Node diagnostic rendering coverage.

8. Preserve scope.
   - Do not implement Phase 32 or later behavior.
   - Do not change diagnostic wording except where a golden test exposes an existing inconsistency that must be fixed.
   - Do not add new MASM syntax to create easier fixtures.

### Required Golden Examples

Invalid hex literal:

```asm
.code
main PROC
    mov eax, 0xZZ
main ENDP
END main
```

Expected rendered text must use exact full rendered-line equality once the current wording is frozen. Until then, this fixture may use a documented partial-match exception, but it must still validate the exact structured location fields and this diagnostic identity:

```text
[assembly-error] invalid-hex-literal
```

Ambiguous memory width:

```asm
.data
value DWORD 1

.code
main PROC
    mov eax, OFFSET value
    test [eax], 1
main ENDP
END main
```

Expected rendered text must use exact full rendered-line equality once the current wording is frozen. Until then, this fixture may use a documented partial-match exception, but it must still validate:

```text
[assembly-error] ambiguous-memory-width
```

Runtime invalid address:

```asm
.code
main PROC
    mov eax, 0
    test [eax], eax
main ENDP
END main
```

Expected rendered text must use exact full rendered-line equality once the current wording is frozen. Until then, this fixture may use a documented partial-match exception, but it must still validate:

```text
[runtime-error]
```

and the runtime diagnostic code for invalid memory access used by the current implementation.

Read-only `.CONST` write:

```asm
.CONST
limit DWORD 10

.code
main PROC
    mov eax, OFFSET limit
    mov DWORD PTR [eax], 20
main ENDP
END main
```

Expected rendered message must classify the failure as a runtime read-only memory or permission violation. The write must not produce a successful memory-change row.

Unaligned warning:

```asm
.data
nums DWORD 2 DUP(0)

.code
main PROC
    mov eax, DWORD PTR nums[1]
main ENDP
END main
```

Expected rendered messages must exactly match the stable warning and success lines for this fixture once the current wording is frozen. Until then, the fixture must validate the warning code, source location fields, and successful execution line.

Successful execution:

```asm
.code
main PROC
    mov eax, 42
main ENDP
END main
```

Expected rendered text must exactly match this stable line:

```text
[info] execution-complete: Execution completed successfully.
```

### Acceptance Criteria

- The native diagnostic JSON producer builds and runs without Emscripten.
- The Node harness uses the same Simulator Messages formatter module as the browser UI.
- The formatter module is side-effect-free when imported in Node.
- Golden tests compare both structured JSON fields and final rendered text.
- Multi-diagnostic ordering is tested.
- Existing C, parser, source-run, and web formatter tests still pass.
- No new MASM language feature is implemented.
- Phase metadata must advance only if the project treats diagnostic harness availability as implementation-phase metadata. If phase metadata is user-facing and reserved for MASM behavior, document that this phase does not change runtime MASM support.

## 36. Phase 32 - Memory Layout Policy Object and Fixed-Layout Compatibility

### Goal

Refactor the VM loader/memory initialization path to use an explicit layout policy while preserving the current fixed educational layout exactly.

This is a core infrastructure phase. It must not add automatic sizing, randomized layout, object-bounds diagnostics, Irvine32 behavior, new MASM syntax, new instructions, or UI controls.

### Dependencies

- Current checked VM memory regions.
- Current `.data`, `.DATA?`, and `.CONST` layout.
- Current `.stack` metadata parsing from header directives.

### Tasks

1. Add a documented C99 layout-policy struct or equivalent configuration object.
2. Represent at least these fields:
   - layout mode;
   - stack size request or named default stack size;
   - heap size request or named default heap size;
   - fixed-layout bases and limits for `.code`, `.data`, `.const`, heap, and stack;
   - minimum region sizes for code, data, const, heap, and stack;
   - maximum per-region sizes by safety tier;
   - maximum total memory reservation by safety tier;
   - region alignment in bytes;
   - guard-gap size in bytes;
   - optional random seed field for later phases.
3. Put all layout defaults in one documented header/configuration module or defaults table. Parser, loader, executor, Wasm API, and UI code must not introduce new magic addresses, sizes, alignment values, guard gaps, or safety-tier limits.
4. Wire the loader to consume the policy object while still selecting fixed educational layout by default.
5. Preserve existing region bases, sizes, permissions, and diagnostics in fixed layout.
6. Add layout metadata to source-run JSON only if it does not destabilize existing UI output; otherwise keep it internal until later layout phases.
7. Add Doxygen comments for the new policy type and loader APIs.

### Non-Goals

- No automatic sizing.
- No randomized base selection.
- No UI/settings controls.
- No object-bounds diagnostics.
- No stack instruction behavior.
- No heap allocation API.

### Required Tests

- Native memory/layout test: fixed layout produces the same documented region bases as before.
- Source-run regression: representative programs from Milestones 27-30 behave identically under default settings.
- Runtime diagnostic regression: invalid address and `.CONST` write diagnostics are unchanged.
- Static compliance: new public structs/functions are documented.
- Static anti-hardcoding check: new code uses named layout defaults instead of scattered numeric region addresses or sizes.

### Acceptance Criteria

- Default source-run behavior is unchanged.
- Existing tests still pass.
- All memory access still goes through central checked VM memory helpers.
- The code now has one explicit place to select future layout modes.

## 37. Phase 33 - Automatic Deterministic Region Sizing

### Goal

Compute deterministic region sizes from program metadata and settings while keeping fixed educational layout as the default.

This phase implements automatic sizing only. It must not add randomized layout, object-bounds diagnostics, Irvine32 behavior, new MASM syntax, new instructions, or UI controls.

### Dependencies

- Phase 32 layout-policy object.
- Current data images for `.data`, `.DATA?`, and `.CONST`.
- Current `.stack` metadata.

### Tasks

1. Add an automatic deterministic layout mode selectable by tests/configuration.
2. Compute region sizes from:
   - generated code/IR metadata;
   - `.data` initialized bytes;
   - `.DATA?` zero-filled bytes;
   - `.CONST` read-only bytes;
   - documented default stack size;
   - documented default heap size.
3. Use the named minimum sizes, maximum sizes, total reservation limit, alignment, and guard-gap policy from Phase 32.
4. Round every computed region size up with overflow-checked alignment.
5. Compute every region byte range as `[base, base + size - 1]` with overflow checks before allocation.
6. Ensure the same source and settings produce the same region sizes.
7. Reject data/code/const/heap/stack or total-memory requests beyond configured safety limits with structured `resource-limit-exceeded` diagnostics.
8. Ensure automatic sizing does not expand regions after program load.
9. Preserve `.CONST` read-only permissions and `.DATA?` writable permissions.
10. Keep default user-facing mode as fixed educational layout.

### Non-Goals

- No seeded or fresh randomized bases.
- No UI controls.
- No object-bounds validation.
- No stack instructions.
- No heap allocation routines.

### Required Tests

- Tiny program: automatic region sizes are at least documented minimums.
- Large `.data` nested `DUP`: region grows deterministically or fails with `resource-limit-exceeded`.
- `.DATA?`: zero-filled bytes fit inside automatic data/uninitialized storage.
- `.CONST`: automatic const region is read-only and rejects writes.
- Invalid access after load remains invalid; automatic mode must not grow memory on demand.
- Source-run JSON test if layout metadata is reported.
- Overflow test for a size calculation that would wrap `base + size - 1`.
- Rendered Simulator Messages test for `resource-limit-exceeded`, with the diagnostic source span pointing to the oversized declaration or `.stack` size expression where applicable.

### Acceptance Criteria

- Automatic deterministic layout can be selected in tests.
- Same source/settings produce the same sizes.
- Fixed educational layout remains default and unchanged.
- Resource-limit failures are structured and user-friendly.

## 38. Phase 34 - Stack and Heap Size Metadata for Layout

### Goal

Apply `.stack` metadata and configured heap size to region sizing without implementing runtime stack instructions or heap allocation routines.

This phase is a layout/sizing phase only.

### Dependencies

- Phase 32 layout-policy object.
- Phase 33 automatic deterministic sizing.
- Existing `.stack` parser metadata from MASM32 header directives.

### Tasks

1. Let `.stack` metadata determine the requested stack region size in automatic deterministic layout.
2. Define default stack size if no `.stack` directive is present.
3. Define default heap size and configurable heap-size field for tests/configuration.
4. Validate stack and heap sizes against safety-tier limits.
5. Add structured diagnostics for excessive `.stack` or heap requests.
6. Ensure stack and heap region bounds are present in layout metadata.
7. Preserve current `ESP` behavior if runtime stack initialization has not been implemented yet.

### Non-Goals

- No `push`, `pop`, `call`, `ret`, `leave`, or stack-frame behavior.
- No stack overflow/underflow instruction diagnostics beyond ordinary region access checks.
- No heap allocation API.
- No UI controls.

### Required Tests

- `.stack 4096` produces the requested stack capacity under automatic layout.
- `.stack` with no operand uses the documented default.
- `.stack` expression from prior constant-expression support is accepted if already implemented.
- Excessive `.stack` size fails with `resource-limit-exceeded` and points to the `.stack` source span.
- Heap default is deterministic and bounded.
- Stack and heap size limits are read from the named layout-policy defaults, not local magic constants.

### Acceptance Criteria

- Stack/heap sizes influence region capacity metadata only.
- No stack instruction behavior is accidentally implemented.
- Existing source-run behavior remains unchanged except where automatic layout is explicitly selected.

## 39. Phase 35 - Seeded and Fresh Randomized Memory Layout

### Goal

Add randomized region-base placement modes that discourage hardcoded simulator addresses while preserving reproducibility through explicit seeds.

This phase must not add object-bounds diagnostics, provenance checks, UI controls, new MASM syntax, new instructions, stack instructions, or Irvine32 behavior.

### Dependencies

- Phase 32 layout-policy object.
- Phase 33 automatic deterministic region sizing.
- Phase 34 stack/heap size metadata.

### Tasks

1. Add seeded randomized layout mode.
2. Add fresh randomized layout mode.
3. Implement seeded base selection with documented alignment, range, and non-overlap rules.
4. Ensure the same source, settings, and seed produce the same layout.
5. Ensure fresh randomized mode records the generated seed in run/layout metadata.
6. Ensure labels, `OFFSET`, symbol-relative operands, and checked memory helpers use selected region bases consistently.
7. Ensure `.CONST` permissions and `.DATA?` metadata still apply after randomized placement.
8. Add tests proving hardcoded fixed `.data` addresses are unreliable under randomized layout while `OFFSET symbol` programs continue to work.

### Non-Goals

- No UI settings controls.
- No URL/share-state seed persistence.
- No object-bounds/provenance diagnostics.
- No fresh-random manual UX beyond metadata exposure.

### Required Tests

- Same seed/source/settings gives identical region bases.
- Two named fixed test seeds, for example `0x00000001` and `0x00000002`, produce different bases for at least one region under the reference randomized-layout algorithm. If the configured address space cannot satisfy this, the layout mode must report randomization unavailable rather than pass a probability-based test.
- Regions do not overlap and satisfy guard-gap/alignment policy.
- Hardcoded `00500000h` data access fails or reads unrelated data under randomized layout.
- Equivalent `OFFSET value` program succeeds.
- Fresh randomized mode returns a generated seed.
- Default tests/documentation still use fixed or explicitly seeded layouts.

### Acceptance Criteria

This hardcoded-address program must not be accepted as reliable under seeded randomized layout:

```asm
.code
main PROC
    mov eax, 00500000h
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

The equivalent label-based program must remain valid:

```asm
.data
value DWORD 123
.code
main PROC
    mov eax, OFFSET value
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

## 39A. Phase 35A - OPTION CASEMAP and User Symbol Case Policy Correction

### Goal

Correct and implement the simulator's user-defined symbol case policy before object-map and provenance work depends on stable symbol identity.

This phase fixes the earlier Phase 26 wording that incorrectly described user-defined symbols as case-sensitive by default and described `OPTION CASEMAP:NONE` as a no-op. The simulator must instead use MASM-compatible default case-insensitive user-symbol matching, support explicit `CASEMAP:ALL`, support `CASEMAP:NONE` exact-case mode, recognize but reject `CASEMAP:NOTPUBLIC`, and reject invalid `CASEMAP` values clearly.

This is a corrective compatibility phase inserted between Phase 35 and Phase 36. It does not renumber later phases. After this phase is complete, the next numbered phase remains Phase 36.

### Dependencies

- Phase 26 MASM header directive parsing.
- Existing parser symbol tables for data symbols, numeric equates, labels, and procedure names where implemented.
- Existing duplicate-symbol and unknown-symbol diagnostics.
- Phase 31 native diagnostic rendering harness for rendered Simulator Messages tests.

### Default case policy

Without any `OPTION CASEMAP` directive:

- instructions are case-insensitive;
- registers and register aliases are case-insensitive;
- directives are case-insensitive;
- operators such as `OFFSET`, `TYPE`, `LENGTHOF`, and `SIZEOF` are case-insensitive;
- data type names and `PTR` width names are case-insensitive;
- virtual include names and recognized Irvine32 routine names are case-insensitive;
- user-defined symbols are case-insensitive;
- symbol definitions whose names differ only by ASCII case are duplicates;
- symbol references may use any casing.

In default mode, these references must resolve to the same symbol:

```asm
.DATA?
buf DWORD ?

.code
main PROC
    mov eax, OFFSET bUF
    mov DWORD PTR [eax], 77
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Expected result:

```text
execution-complete
EBX = 0000004Dh / 77
```

In default mode, this remains rejected as a duplicate symbol:

```asm
.DATA?
buf DWORD ?
bUF DWORD ?

.code
main PROC
END main
```

Expected diagnostic:

```text
duplicate-symbol
```

The diagnostic must point at the second declaration name.

### Required symbol-resolution model

The implementation must use one active user-symbol case policy while parsing.

The parser starts in:

```text
CASEMAP:ALL
```

This means the default simulator behavior is case-insensitive user-symbol matching.

The active policy may change only through supported directives:

```asm
OPTION CASEMAP:ALL
OPTION CASEMAP:NONE
```

The active policy applies only to declarations and references parsed after the directive. The implementation must not retroactively modify accepted symbols when the policy changes.

Each accepted user symbol must retain:

- original source spelling;
- namespace/category;
- source location of declaration;
- enough comparison metadata to support exact-case lookup and ASCII-folded lookup.

The implementation may store a folded key for efficiency, but the folded key must be derived by ASCII uppercase folding, not locale-sensitive case conversion.

### Declaration rules

When declaring a user symbol under `CASEMAP:ALL`:

- compare the new spelling to already-accepted symbols in the same namespace using ASCII-folded spelling;
- if a match exists, emit `duplicate-symbol`;
- do not insert the rejected declaration.

When declaring a user symbol under `CASEMAP:NONE`:

- compare the new spelling to already-accepted symbols in the same namespace using exact spelling;
- if an exact spelling match exists, emit `duplicate-symbol`;
- if only different-case spellings exist, accept the new symbol as distinct.

These rules apply to data symbols and numeric equates in this phase. They must also apply to labels/procedure names where those tables already exist. Future user-symbol namespaces must follow the same active-policy model when implemented.

### Reference lookup rules

When resolving a user-symbol reference under `CASEMAP:ALL`:

1. ASCII-fold the reference spelling.
2. Find accepted symbols in the relevant namespace whose ASCII-folded declaration spelling matches.
3. If zero symbols match, emit `unknown-symbol`.
4. If one symbol matches, resolve to that symbol.
5. If more than one symbol matches, emit `ambiguous-symbol` and do not choose one.

When resolving a user-symbol reference under `CASEMAP:NONE`:

1. Match only accepted symbols whose declaration spelling exactly equals the reference spelling.
2. If no exact match exists, emit `unknown-symbol`.
3. Do not fall back to case-insensitive lookup.

### CASEMAP directive rules

`OPTION CASEMAP:ALL`:

- is supported;
- sets active user-symbol policy to `CASEMAP:ALL`;
- is equivalent to the default policy if no earlier `CASEMAP:NONE` changed the mode.

`OPTION CASEMAP:NONE`:

- is supported;
- sets active user-symbol policy to `CASEMAP:NONE`.

`OPTION CASEMAP:NOTPUBLIC`:

- is recognized but unsupported;
- must emit `unsupported-option`;
- must not change the active policy;
- must explain that `NOTPUBLIC` depends on public/external linkage semantics not modeled by the simulator.

Any other `CASEMAP` value:

- is invalid;
- must emit `invalid-option-value` where available, or `unsupported-option` if no narrower code exists;
- must not change the active policy;
- must list `ALL` and `NONE` as supported values and `NOTPUBLIC` as recognized but unsupported.

If a supported `CASEMAP` directive changes the active policy from a previously selected supported policy, emit:

```text
casemap-policy-changed
```

Severity:

```text
warning
```

The warning must not block execution by itself.

No warning is required when:

- the first explicit `CASEMAP` directive appears;
- `OPTION CASEMAP:ALL` appears while the active policy is already `ALL`;
- `OPTION CASEMAP:NONE` appears while the active policy is already `NONE`.

A warning is required when:

- `ALL` changes to `NONE`;
- `NONE` changes to `ALL`.

### Required diagnostic behavior

Every new or changed diagnostic must preserve:

- severity;
- diagnostic code;
- line;
- column;
- byte offset;
- span length;
- final rendered Simulator Messages text.

Required severities:

```text
casemap-policy-changed: warning
duplicate-symbol: assembly-error
unknown-symbol: assembly-error
ambiguous-symbol: assembly-error
unsupported-option: assembly-error
invalid-option-value: assembly-error
```

Execution rule:

- warning-only programs may execute;
- any assembly-error prevents execution.

### Duplicate, unknown, and ambiguous symbol behavior

When `CASEMAP:ALL` is active, declarations that differ only by case are duplicates.

If a duplicate declaration is rejected, it must not be entered into the symbol table.

If a later `CASEMAP:NONE` directive changes lookup to exact-case mode, references to the rejected spelling remain unresolved and may produce `unknown-symbol` if parser recovery continues.

This behavior is intentional:

```asm
OPTION CASEMAP:ALL

.DATA?
buf DWORD ?
bUF DWORD ?

OPTION CASEMAP:NONE

.code
main PROC
    mov eax, OFFSET bUF
    mov DWORD PTR [eax], 77
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Expected diagnostics, if recovery continues:

```text
duplicate-symbol on the bUF declaration
casemap-policy-changed warning on OPTION CASEMAP:NONE
unknown-symbol on OFFSET bUF
```

No execution occurs.

If `CASEMAP:ALL` becomes active after earlier `CASEMAP:NONE` declarations created multiple valid case-distinct symbols that fold to the same case-insensitive key, a later folded lookup that matches more than one valid symbol must not guess.

Preferred diagnostic:

```text
ambiguous-symbol
```

Acceptable fallback if no ambiguity diagnostic exists yet:

```text
duplicate-symbol
```

Suggested example:

```asm
OPTION CASEMAP:NONE

.data
buf DWORD 1
bUF DWORD 2

OPTION CASEMAP:ALL

.code
main PROC
    mov eax, buf
main ENDP
END main
```

Expected:

```text
casemap-policy-changed warning
ambiguous-symbol or duplicate-symbol for folded lookup
no execution
```

### Implementation tasks

1. Add an explicit parser/source setting for active user-symbol case policy.
2. Default the policy to `CASEMAP:ALL` behavior: case-insensitive user symbols.
3. Parse and support `OPTION CASEMAP:ALL`.
4. Parse and support `OPTION CASEMAP:NONE`.
5. Parse and recognize `OPTION CASEMAP:NOTPUBLIC`, but reject it with `unsupported-option`.
6. Reject all other `CASEMAP` values with `invalid-option-value` where available, or `unsupported-option` with precise wording.
7. Ensure keyword, instruction, register, register-alias, directive, operator, data type, `PTR` width, virtual include, and recognized Irvine32 routine matching remains case-insensitive under all `CASEMAP` modes.
8. Ensure data-symbol lookup and duplicate detection use the active user-symbol policy.
9. Ensure numeric equate lookup and duplicate detection use the active user-symbol policy.
10. Ensure label lookup and duplicate detection use the active user-symbol policy where label support currently exists.
11. Ensure procedure-name lookup and duplicate detection use the active user-symbol policy where procedure-name metadata currently exists.
12. Ensure future user-symbol categories named in the spec follow the same active policy when implemented.
13. Support source-order policy application: declarations and references are interpreted using the policy active at their source location.
14. Emit a non-fatal `casemap-policy-changed` warning when a supported `CASEMAP` directive changes a previously set supported `CASEMAP` policy.
15. Preserve current default behavior where references such as `OFFSET bUF` resolve to `buf`.
16. Under `CASEMAP:NONE`, make references require exact user-symbol spelling.
17. Under `CASEMAP:NONE`, allow separately declared user symbols whose names differ only by case.
18. Under `CASEMAP:ALL`, reject duplicate declarations whose names differ only by case.
19. Do not enter rejected duplicate declarations into the symbol table.
20. If case-insensitive lookup under `CASEMAP:ALL` matches multiple valid case-distinct symbols from an earlier `CASEMAP:NONE` region, emit an ambiguity diagnostic rather than choosing one.
21. Add or update Doxygen comments for any new public structs, enums, helpers, or configuration fields.
22. Update `FULL_IMPLEMENTATION_SPEC.md`.
23. Update Phase 26 wording in `INCREMENTAL_IMPLEMENTATION_GUIDE.md`.
24. Update `docs/SUPPORTED_SYNTAX.md`.
25. Add regression tests proving that older default case-insensitive behavior remains unchanged.
26. Add structured diagnostic tests and rendered Simulator Messages tests for every new or changed user-visible diagnostic or warning.

### Required tests

#### Default case-insensitive symbol lookup

```asm
.DATA?
buf DWORD ?

.code
main PROC
    mov eax, OFFSET bUF
    mov DWORD PTR [eax], 77
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Expected:

```text
execution-complete
EBX = 0000004Dh / 77
```

#### Default duplicate-by-case-folded-name rejection

```asm
.DATA?
buf DWORD ?
bUF DWORD ?

.code
main PROC
END main
```

Expected:

```text
duplicate-symbol
```

The diagnostic must point at the second declaration. The implementation must not accept both symbols.

#### Explicit CASEMAP:ALL symbol lookup

```asm
OPTION CASEMAP:ALL

.DATA?
buf DWORD ?

.code
main PROC
    mov eax, OFFSET bUF
    mov DWORD PTR [eax], 77
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Expected:

```text
execution-complete
EBX = 0000004Dh / 77
```

#### Explicit CASEMAP:ALL duplicate-by-case-folded-name rejection

```asm
OPTION CASEMAP:ALL

.DATA?
buf DWORD ?
bUF DWORD ?

.code
main PROC
END main
```

Expected:

```text
duplicate-symbol
```

The diagnostic must point at the second declaration. The implementation must not accept both symbols.

#### Default equate lookup is case-insensitive

```asm
COUNT = 5

.code
main PROC
    mov eax, count
main ENDP
END main
```

Expected:

```text
EAX = 00000005h / 5
```

#### Default equate duplicate differs only by case

```asm
COUNT = 5
count = 6

.code
main PROC
END main
```

Expected:

```text
duplicate-symbol
```

or a narrower duplicate-equate diagnostic if one already exists.

#### CASEMAP:NONE makes data symbols case-sensitive

```asm
OPTION CASEMAP:NONE

.DATA?
buf DWORD ?

.code
main PROC
    mov eax, OFFSET bUF
main ENDP
END main
```

Expected:

```text
unknown-symbol
```

The diagnostic must point at `bUF`. The implementation must not resolve `bUF` to `buf`.

#### CASEMAP:NONE permits case-distinct data symbols

```asm
OPTION CASEMAP:NONE

.data
buf DWORD 1
bUF DWORD 2

.code
main PROC
    mov eax, buf
    mov ebx, bUF
main ENDP
END main
```

Expected:

```text
EAX = 00000001h / 1
EBX = 00000002h / 2
```

#### CASEMAP:NONE keeps instructions and registers case-insensitive

```asm
OPTION CASEMAP:NONE

.data
value DWORD 3

.code
main PROC
    MoV EaX, VaLuE
main ENDP
END main
```

Expected:

```text
unknown-symbol
```

The instruction and register must be recognized. The failure must be only the user-symbol spelling mismatch for `VaLuE`.

#### Exact-case reference succeeds under CASEMAP:NONE

```asm
OPTION CASEMAP:NONE

.data
VaLuE DWORD 3

.code
main PROC
    MoV EaX, VaLuE
main ENDP
END main
```

Expected:

```text
EAX = 00000003h / 3
```

#### CASEMAP:NONE applies to `.CONST`

```asm
OPTION CASEMAP:NONE

.CONST
Limit DWORD 10

.code
main PROC
    mov eax, limit
main ENDP
END main
```

Expected:

```text
unknown-symbol
```

#### CASEMAP:NONE applies to `.DATA?`

```asm
OPTION CASEMAP:NONE

.DATA?
Buffer DWORD ?

.code
main PROC
    mov eax, OFFSET buffer
main ENDP
END main
```

Expected:

```text
unknown-symbol
```

#### CASEMAP:NONE applies to `TYPE`, `LENGTHOF`, and `SIZEOF`

```asm
OPTION CASEMAP:NONE

.data
Nums DWORD 4 DUP(0)

.code
main PROC
    mov eax, TYPE nums
    mov ebx, LENGTHOF NUMS
    mov ecx, SIZEOF nUMS
main ENDP
END main
```

Expected:

```text
unknown-symbol
```

At least one test should verify each operator with exact casing succeeds.

#### CASEMAP:NONE applies to numeric equates

```asm
OPTION CASEMAP:NONE

COUNT = 5

.code
main PROC
    mov eax, count
main ENDP
END main
```

Expected:

```text
unknown-symbol
```

#### CASEMAP:NONE permits case-distinct equates

```asm
OPTION CASEMAP:NONE

COUNT = 5
count = 6

.code
main PROC
    mov eax, COUNT
    mov ebx, count
main ENDP
END main
```

Expected:

```text
EAX = 00000005h / 5
EBX = 00000006h / 6
```

#### CASEMAP policy change from NONE to ALL emits warning and applies ALL afterward

```asm
OPTION CASEMAP:NONE
OPTION CASEMAP:ALL

.data
buf DWORD 1

.code
main PROC
    mov eax, bUF
main ENDP
END main
```

Expected:

```text
casemap-policy-changed warning
execution-complete
EAX = 00000001h / 1
```

The implementation must not treat `casemap-policy-changed` as an assembly error.

#### CASEMAP policy change from ALL to NONE emits warning and applies NONE afterward

```asm
OPTION CASEMAP:ALL
OPTION CASEMAP:NONE

.data
buf DWORD 1

.code
main PROC
    mov eax, bUF
main ENDP
END main
```

Expected:

```text
casemap-policy-changed warning
unknown-symbol for bUF
no execution
```

#### Repeated same CASEMAP value does not require a warning

```asm
OPTION CASEMAP:ALL
OPTION CASEMAP:ALL

.data
buf DWORD 1

.code
main PROC
    mov eax, bUF
main ENDP
END main
```

Expected:

```text
execution-complete
EAX = 00000001h / 1
```

No `casemap-policy-changed` warning is required because the active policy did not change.

#### Duplicate under ALL then unknown under later NONE

```asm
OPTION CASEMAP:ALL

.DATA?
buf DWORD ?
bUF DWORD ?

OPTION CASEMAP:NONE

.code
main PROC
    mov eax, OFFSET bUF
    mov DWORD PTR [eax], 77
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Expected, if parser recovery continues:

```text
duplicate-symbol on bUF declaration
casemap-policy-changed warning on OPTION CASEMAP:NONE
unknown-symbol on OFFSET bUF
no execution
```

The implementation must not insert the rejected `bUF` declaration and then resolve it later.

#### Ambiguous folded lookup after switching from NONE to ALL

```asm
OPTION CASEMAP:NONE

.data
buf DWORD 1
bUF DWORD 2

OPTION CASEMAP:ALL

.code
main PROC
    mov eax, buf
main ENDP
END main
```

Expected:

```text
casemap-policy-changed warning
ambiguous-symbol or duplicate-symbol for folded lookup
no execution
```

The implementation must not silently choose either `buf` or `bUF`.

#### CASEMAP:NOTPUBLIC is recognized but unsupported

```asm
OPTION CASEMAP:NOTPUBLIC

.code
main PROC
END main
```

Expected:

```text
unsupported-option
```

The message must explain that `CASEMAP:NOTPUBLIC` depends on public/external linkage semantics that are not implemented. The implementation must not silently treat `NOTPUBLIC` as `ALL`, `NONE`, or default.

#### CASEMAP:NOTPUBLIC does not change active policy

```asm
OPTION CASEMAP:NOTPUBLIC

.data
buf DWORD 1

.code
main PROC
    mov eax, bUF
main ENDP
END main
```

Expected:

```text
unsupported-option
no execution
```

If recovery proceeds far enough to attempt lookup, `bUF` should still be interpreted under the prior active policy because `NOTPUBLIC` must not change the active policy.

#### Invalid CASEMAP value is rejected

```asm
OPTION CASEMAP:LOWER

.code
main PROC
END main
```

Expected:

```text
invalid-option-value
```

or, if no specific invalid-value diagnostic exists:

```text
unsupported-option
```

The message must identify:

```text
Supported CASEMAP values: ALL, NONE. Recognized but unsupported: NOTPUBLIC.
```

#### Source-span and rendered-message tests

For every new diagnostic or warning introduced or changed in this phase:

- assert structured diagnostic code;
- assert diagnostic severity;
- assert one-based source line;
- assert one-based source column;
- assert byte offset;
- assert span length;
- assert final rendered Simulator Messages text through the native diagnostic JSON producer and Node formatter harness.

Warnings must be tested separately from fatal assembly errors to prove that warning-only programs can still execute.

### Non-Goals

This phase must not implement:

- object allocation map behavior;
- object-bounds/provenance diagnostics;
- STRUCT, UNION, RECORD, TYPEDEF, or field access;
- macro definitions or macro expansion;
- `OPTION NOKEYWORD`;
- `OPTION CASEMAP:NOTPUBLIC` semantics;
- public/external linkage semantics;
- `PUBLIC`, `EXTERN`, `COMM`, or linker/object-file behavior;
- Windows API, PE/linker, or host include behavior;
- control flow, stack instructions, Irvine32 routines, or new runtime instructions.
## 40. Phase 36 - Declared Object Allocation Map

### Goal

Create a declared-object allocation map for `.data`, `.DATA?`, and `.CONST` storage. This is metadata infrastructure for later object-bounds, memory visualization, and uninitialized-read diagnostics.

This phase must not add object-bounds warnings/errors yet.

### Dependencies

- Current data layout and symbol metadata.
- Current `.DATA?` and `.CONST` sections.
- Current selected memory layout after fixed/automatic/randomized placement.
- Phase 35A user-symbol case policy correction.

### Tasks

1. Add a declared-object map emitted by parser/data-layout or loader.
2. Track for each object:
   - symbol name;
   - section kind;
   - base address after selected layout;
   - byte size;
   - element size;
   - element count;
   - signedness/type metadata;
   - permissions;
   - source declaration line, column, byte offset, and span;
   - initialization-origin metadata placeholder with explicit state `not-tracked` before uninitialized-origin tracking is implemented.
3. Expose lookup helpers by address range.
4. Ensure object map works in fixed, automatic deterministic, and randomized layouts.
5. Preserve central region permission checks.
6. Build object ranges as overflow-checked inclusive byte ranges `[object_base, object_base + object_size - 1]`.
7. Treat each declared symbol as one owning object after nested `DUP` expansion. Adjacent declarations remain separate objects even when contiguous.
8. Do not emit zero-size objects.
9. Add tests for scalar declarations, arrays, nested `DUP`, `.DATA?`, `.CONST`, and randomized layout.

### Non-Goals

- No warnings or errors for object-bounds access yet.
- No provenance tracking.
- No uninitialized-read diagnostics.
- No UI memory visualization changes.

### Required Tests

- Scalar `.data`, array `.data`, nested `DUP`, `.DATA?`, and `.CONST` each produce correct object ranges.
- Adjacent symbols create separate object-map entries, not one merged object.
- Object-map construction uses full byte ranges and rejects or prevents address-range overflow.
- Unaligned accesses still behave exactly as before because enforcement is not active yet.
- Static compliance verifies object-map structs/APIs are documented.

### Acceptance Criteria

- Tests can query the object map for declared objects and verify address, size, section, permissions, and source locations.
- Existing runtime behavior and diagnostics remain unchanged.
- `.CONST` writes are still blocked by memory permissions, not by object-map-only checks.

### Full-Range Classification Rules

Every object-mode diagnostic must classify the full access range `[address, address + width - 1]` with overflow checks. Tests and implementation must distinguish:

- range wholly inside one declared object;
- range wholly inside another declared object;
- range wholly inside a valid region but outside every object;
- range starting inside one object and ending outside it;
- range starting outside an object and ending inside it;
- range spanning two adjacent declared objects;
- range in padding/gap space inside a valid region;
- range outside all regions;
- range overlapping `.CONST` read-only storage, which must keep `.CONST` permission precedence.

Runtime diagnostics should point at the memory operand source span when available; otherwise they must include the IR instruction source location and computed address/range.

### Corrective note: object-map classification is not parser rejection

Phase 36 object-map classification data must not be interpreted as permission to reject otherwise valid MASM-style memory operands during parsing.

The object map provides metadata for later runtime educational diagnostics. It must not turn object-boundary, section-image-boundary, or section-capacity-boundary cases into assembly errors.

Valid memory syntax with inferable width must be lowered to IR and checked at runtime unless it fails for an ordinary parser reason such as malformed syntax, unknown symbol, ambiguous width, unsupported addressing mode, unsupported executable width, or constant-expression failure.

Examples that must not be rejected merely by object-map construction:

```asm
.data
x DWORD ?
.code
main PROC
    mov eax, DWORD PTR [x + 1]
main ENDP
END main
```

```asm
.DATA?
x DWORD ?
.code
main PROC
    mov eax, 10
    mul [x+1]
main ENDP
END main
```

These examples may later produce runtime diagnostics depending on memory region validity, `.CONST` permissions, section-validation modes, object-validation modes, uninitialized-read modes, and unaligned-access policy.

Do not add or preserve an assembly-time `symbol-offset-out-of-range` diagnostic for these valid memory operand shapes merely because the access crosses an object or section boundary.

## 41. Phase 37 - Allocated-Object Warning Mode

### Goal

Add an educational warning mode for accesses inside a valid memory region but outside any declared data object.

This phase must not change default region-only execution and must not add strict object errors yet.

### Dependencies

- Phase 36 declared object allocation map.
- Existing runtime memory access path.
- Phase 31 rendered Simulator Messages tests.

### Tasks

1. Add memory validation mode: allocated-object warnings.
2. For each memory read/write, compute the full access range `[address, address + width - 1]` with overflow checks.
3. Classify each full access range as:
   - wholly inside the intended declared object;
   - wholly inside another declared object;
   - inside a valid region but outside all declared objects;
   - partially overlapping one object boundary;
   - spanning two adjacent objects;
   - outside all regions;
   - permission violation.
4. Emit a simulator warning for valid-region/outside-object, partial-overlap, and cross-object accesses after ordinary region and permission checks pass.
5. Continue execution after the warning.
6. Do not warn for an unaligned access that is wholly contained in a single declared object.
7. Preserve lower-level runtime errors for invalid address and permission failures. `.CONST` write failures take precedence over object warnings.
8. Add structured warning JSON and rendered Simulator Messages tests.

### Non-Goals

- No strict stop-on-object-bounds mode.
- No provenance diagnostics.
- No uninitialized-read diagnostics.
- No UI setting controls.


### Corrective note: Phase 37 is Level 4 declared-object warning only

Phase 37 owns the declared-object warning layer. It does not define the full memory-validation-level policy.

Interpret Phase 37 as:

```text
Level 4 declared-object validation, warn policy.
```

Phase 37 does not implement or require:

- section-capacity validation;
- section-image validation;
- browser UI controls;
- provenance validation;
- uninitialized-read validation;
- parser-time object-bound rejection;
- parser-time section-bound rejection.

Default region-only behavior must remain unchanged when allocated-object warning mode is off.

When warning mode is on, warnings are emitted only after ordinary region and permission checks pass. `.CONST` write failures remain permission/runtime failures and take precedence over object warnings.

Phase 53B - Section-Capacity and Section-Image Validation Modes adds separate Level 2 and Level 3 policy layers and tests without rewriting Phase 37 history.

### Required Tests

- Default region-only mode: no warning for valid-region/outside-object access.
- Allocated-object warning mode: warning for `var1` followed by `[eax+40h]` when address is in `.data` but outside all objects.
- Partial-overlap warning where an access starts inside an object and ends outside it.
- Access starting before an object and ending inside it.
- Access spanning two adjacent objects.
- Unaligned access wholly inside one object: unaligned warning may appear, object warning must not.
- Access into `arr1` from a pointer derived from `var1` remains valid without allocated-object warning when the full access range is wholly inside `arr1`.
- Permission violations still produce runtime errors, not object warnings.
- `.CONST` write overlap reports a read-only/permission runtime diagnostic before any object warning.
- Rendered Simulator Messages warning text is tested.

### Acceptance Criteria

In allocated-object warning mode, this emits a warning and continues if the final address is inside the `.data` region but outside any declared object:

```asm
.data
var1 DWORD 12345
.code
main PROC
    mov eax, OFFSET var1
    test [eax+40h], eax
main ENDP
END main
```

In the same mode, this remains valid without allocated-object warning if the final address lands inside `arr1`:

```asm
.data
var1 DWORD 12345
arr1 DWORD 20 DUP(0ABCDEF12h)
.code
main PROC
    mov eax, OFFSET var1
    test [eax+40h], eax
main ENDP
END main
```

## 42. Phase 38 - Allocated-Object Strict Mode

### Goal

Add an educational strict mode that stops execution for accesses inside a valid memory region but outside any declared data object.

Default region-only behavior must remain unchanged.

### Dependencies

- Phase 36 declared object allocation map.
- Phase 37 allocated-object warning classifier.
- Phase 31 rendered Simulator Messages tests.

### Tasks

1. Add memory validation mode: allocated-object strict.
2. Reuse the classifier from Phase 37.
3. Stop execution with `runtime-error` and diagnostic code `object-bounds-violation` for valid-region/outside-object accesses.
4. Allow accesses into another declared object.
5. Preserve diagnostic precedence:
   - address overflow;
   - outside all regions;
   - permission or `.CONST` read-only violation;
   - object-bounds violation.
6. Add source-run JSON and rendered Simulator Messages tests.
7. Add regression tests proving default region-only mode is unchanged.

### Non-Goals

- No provenance mode.
- No uninitialized-read diagnostics.
- No UI settings controls.


### Corrective note: Phase 38 is Level 4 declared-object strict only

Phase 38 owns the declared-object strict layer. It does not define section-capacity strictness or section-image strictness.

Interpret Phase 38 as:

```text
Level 4 declared-object validation, strict policy.
```

Phase 38 strict mode must not change default region-only behavior. When the strict object mode is off, valid-region accesses outside declared objects remain allowed unless a lower-level runtime memory error occurs.

Phase 38 strict mode must not be implemented as parser rejection. It is runtime validation of the final byte range after effective-address calculation.

Do not reject valid MASM-style memory operands at assembly time merely because they would fail Level 4 declared-object strict validation at runtime.

Phase 53B - Section-Capacity and Section-Image Validation Modes adds separate section-capacity and section-image strict modes with distinct diagnostic codes.

### Required Tests

- Strict failure for access that starts inside one object and ends outside it.
- Strict failure for access spanning two adjacent declared objects.
- Strict failure for access into padding/gap inside a valid region.
- No strict failure for an unaligned access wholly contained inside one object.
- `.CONST` write still reports read-only/permission diagnostic before object-bounds strict diagnostic.
- Rendered Simulator Messages test for `object-bounds-violation` with the source span on the memory operand.

### Acceptance Criteria

In allocated-object strict mode, this fails because the final address is not inside a declared object:

```asm
.data
var1 DWORD 12345
.code
main PROC
    mov eax, OFFSET var1
    test [eax+40h], eax
main ENDP
END main
```

In allocated-object strict mode, this remains valid if the final address lands inside `arr1`:

```asm
.data
var1 DWORD 12345
arr1 DWORD 20 DUP(0ABCDEF12h)
.code
main PROC
    mov eax, OFFSET var1
    test [eax+40h], eax
main ENDP
END main
```

## 43. Phase 39 - Uninitialized-Origin Metadata and Write Tracking

### Goal

Track which bytes originated from `?` or `.DATA?` storage and mark bytes initialized when successful program writes cover them.

This phase must not emit uninitialized-read warnings or errors yet. Default runtime behavior remains deterministic zero-filled storage.

### Dependencies

- `.DATA?` and `.CONST` implementation.
- Nested `DUP` support.
- Declared object map; before the object-map phase, this field is present as an empty map with `objectMapEnabled = false`.

### Tasks

1. Add an initialized-byte mask or equivalent byte-range metadata for `.data` and `.DATA?` storage.
2. Mark explicitly initialized bytes as initialized.
3. Mark bytes from `?` and `DUP(?)` as uninitialized-origin but runtime-zero-filled.
4. Mark every byte in a successful program write as initialized.
5. Ensure failed writes do not mark bytes initialized.
6. Ensure partial writes initialize only the written bytes.
7. Do not track register-value taint/provenance in this phase: storing a register value into memory marks the destination bytes initialized even if that register previously came from an uninitialized-origin read.
8. Preserve source-run output unless test-only metadata is explicitly requested.
9. Add tests for scalar `?`, nested `DUP(?)`, `.DATA?`, byte writes, partial writes, full writes, failed writes, and the no-taint copy rule.

### Non-Goals

- No uninitialized-read warnings/errors.
- No UI display of uninitialized metadata.
- No change to default runtime values.

### Acceptance Criteria

Default mode still reads zero without warning:

```asm
.data
x DWORD ?
.code
main PROC
    mov eax, x
main ENDP
END main
```

A test-only metadata inspection confirms:

- `x` starts uninitialized-origin;
- `mov BYTE PTR x, 12h` initializes only byte 0;
- `mov x, 123` initializes all 4 bytes;
- a failed `.CONST` write or invalid address write initializes nothing;
- reading `x DWORD ?` into `EAX` and then storing `EAX` into `y DWORD ?` marks `y` initialized because register-value taint is not modeled.

## 44. Phase 40 - Uninitialized-Read Warning and Strict Modes

### Goal

Warn or error when a program reads bytes that originated as uninitialized storage and have not yet been written by the simulated program.

Default educational mode remains warning-free and deterministic zero-filled.

### Dependencies

- Phase 39 uninitialized-origin metadata and write tracking.
- Phase 31 rendered Simulator Messages tests.

### Tasks

1. Add uninitialized-read warning mode.
2. Add uninitialized-read strict mode.
3. Check every byte in multi-byte reads.
4. In warning mode, emit a simulator warning and continue.
5. In strict mode, stop execution with `runtime-error` and diagnostic code `uninitialized-read`.
6. For read-modify-write instructions, perform the uninitialized-read check before marking the destination bytes initialized by write-back.
7. Report symbol name when the address maps to a known symbol, otherwise `symbolName = null`; always report byte offset/range, access width, and source location or explicit `sourceLocation = null`.
8. Preserve diagnostic precedence after invalid region, read-only `.CONST`, object-bounds, and permission checks.
9. Add source-run JSON and rendered Simulator Messages tests.

### Non-Goals

- No provenance diagnostics.
- No UI settings controls.
- No change to default runtime value of `?`.

### Required Tests

- Default mode: no warning for reading `DWORD ?`.
- Warning mode: warning for reading `DWORD ?`.
- Strict mode: runtime error for reading `DWORD ?`.
- Full write before read suppresses warning/error.
- Partial write followed by multi-byte read still warns/errors for remaining uninitialized bytes.
- Multi-byte read spanning initialized and uninitialized bytes diagnoses the whole read range.
- `.DATA?` object behaves like `?` storage.
- Read-modify-write warnings for `inc mem`, `dec mem`, `not mem`, `and mem, imm`, `or mem, imm`, `xor mem, imm`, `shl mem, 1`, and `ror mem, 1` when the memory bytes are still uninitialized-origin.
- Strict read-modify-write case stops before write-back and leaves the destination bytes uninitialized-origin.
- Rendered Simulator Messages text is tested.

### Acceptance Criteria

Default mode:

```asm
.data
x DWORD ?
.code
main PROC
    mov eax, x
main ENDP
END main
```

reads zero without warning.

Uninitialized-read warning mode emits a simulator warning for the same program.

This program does not warn after the write initializes the bytes:

```asm
.data
x DWORD ?
.code
main PROC
    mov x, 123
    mov eax, x
main ENDP
END main
```


### Corrective note after later default-policy phase

Phase 40 implemented the uninitialized-read warning and strict mechanisms as opt-in modes. Its original default behavior was warning-free deterministic zero-filled reads.

A later default teaching-diagnostics phase intentionally changes the default policy from `off` to `warn`.

After that later phase:

```text
uninitialized_read_policy default = warn
```

The explicit `off` policy remains available and preserves the original Phase 40 silent behavior.

The `strict` policy remains available and still stops before consuming uninitialized-origin bytes.

Do not reinterpret Phase 40 history as if default warnings existed from the beginning. Treat the default change as a later policy change.

## 45. Phase 41 - Virtual Irvine32 Symbol Registry

### Goal

Make `INCLUDE Irvine32.inc` register known Irvine32 names as virtual symbols before full Irvine32 routine execution exists.

This is metadata and diagnostic infrastructure only. It must not add CALL execution, real Irvine32 routine bodies, Program Console behavior, Windows API behavior, or stack behavior.

### Dependencies

- MASM32 header directive support for `INCLUDE Irvine32.inc`.
- Current symbol/diagnostic infrastructure.

### Tasks

1. When `INCLUDE Irvine32.inc` is present, register a table of known Irvine32 routine names.
2. Classify known names as:
   - supported virtual intrinsic now;
   - planned Irvine32 routine later;
   - known but explicitly unsupported in v1;
   - Windows/API/external symbol;
   - unknown symbol.
3. Add diagnostics for executable use of known-but-unsupported Irvine32 names if they can currently appear in source.
4. Add an explicit note that CALL target classification is deferred until CALL exists.
5. Ensure virtual include does not read host files or imply linking.
6. Add tests that the include is accepted and the registry is populated.

### Non-Goals

- No `call` execution.
- No `ret` behavior.
- No Program Console output routines.
- No `exit` terminator yet; that is next phase.
- No Windows/API execution.

### Acceptance Criteria

```asm
INCLUDE Irvine32.inc
.code
main PROC
END main
```

parses successfully and records the Irvine32 virtual include/registry metadata.

A known unsupported Irvine32 routine must be diagnosable as `unsupported-irvine32-routine` when used in any executable form currently recognized by the parser.

## 46. Phase 42 - Irvine32 `exit` Terminator

### Goal

Accept the common Irvine `exit` terminator as a virtual built-in when `INCLUDE Irvine32.inc` is active.

This phase must not implement CALL, RET, ExitProcess, stack behavior, Program Console output, or any other Irvine32 routine body.

### Dependencies

- Phase 41 virtual Irvine32 symbol registry.
- Source-run termination/status path.

### Tasks

1. Parse `exit` as a zero-operand virtual Irvine32 terminator when `INCLUDE Irvine32.inc` is active.
2. Lower `exit` to a VM termination IR operation or handle it as a virtual pseudo-instruction.
3. Terminate execution successfully.
4. Do not mutate registers, flags, memory, or Program Console except for normal termination state.
5. Ensure instructions after `exit` do not execute in the same run.
6. Reject `exit` operands with `invalid-instruction-operands`.
7. When `exit` appears without `INCLUDE Irvine32.inc`, emit `unknown-instruction` at the `exit` mnemonic with the message `Unknown instruction or virtual Irvine32 terminator. Add INCLUDE Irvine32.inc to use exit.`
8. Add source-run JSON and rendered Simulator Messages tests.

### Non-Goals

- No CALL target classification.
- No `call ExitProcess` support.
- No Windows API behavior.
- No stack behavior.
- No Program Console routines.

### Acceptance Criteria

```asm
INCLUDE Irvine32.inc
.code
main PROC
    mov eax, 123
    exit
    mov eax, 999
main ENDP
END main
```

Expected:

```text
EAX = 0000007Bh / 123
Execution completed successfully.
```

This must produce a clear unsupported Windows/API diagnostic or unsupported CALL diagnostic depending on current CALL support; it must not execute as Windows API behavior:

```asm
INCLUDE Irvine32.inc
.code
main PROC
    call ExitProcess
main ENDP
END main
```

If CALL is not implemented yet, the primary diagnostic should remain CALL unsupported. The `unsupported-windows-api` target classification moves to the CALL/RET phase family.

## 47. Phase 43 - INC and DEC

### Goal

Implement `inc` and `dec` as a focused read-modify-write instruction milestone.

This phase must not implement bitwise instructions, shifts, rotates, `lea`, multiplication, division, labels, jumps, stack behavior, or procedure behavior.

### Behavior category

Runtime instruction behavior.

### Dependencies

- Global memory-width resolution is already implemented.
- Checked VM memory helpers are already implemented.
- Modeled flags currently include `CF`, `ZF`, `SF`, and `OF`.

### Accepted syntax

```asm
inc reg8
inc reg16
inc reg32
inc BYTE PTR [reg32]
inc WORD PTR [reg32]
inc DWORD PTR [reg32]
inc symbol
inc symbol[offset]

dec reg8
dec reg16
dec reg32
dec BYTE PTR [reg32]
dec WORD PTR [reg32]
dec DWORD PTR [reg32]
dec symbol
dec symbol[offset]
```

Signed `PTR` aliases from earlier phases must work equivalently by width:

```asm
inc SBYTE PTR [esi]
dec SWORD PTR [esi]
```

### Rejected syntax

```asm
inc 1
dec 1
inc eax, ebx
dec eax, ebx
inc [eax]        ; ambiguous width unless a later rule supplies it
dec [eax]        ; ambiguous width unless a later rule supplies it
inc QWORD PTR q  ; executable QWORD/SQWORD memory operation remains deferred
```

### Runtime semantics

- `inc` computes `dest = dest + 1` at the selected operand width.
- `dec` computes `dest = dest - 1` at the selected operand width.
- Register destinations mutate only the selected register or alias width.
- Memory destinations are read-modify-write operations.
- Memory read and memory write must both go through checked VM memory helpers.
- `.CONST` writes must fail through static diagnostics when obvious and through central checked memory permissions for computed addresses.

### Flag behavior

- Update `ZF` from the result.
- Update `SF` from the result sign bit at operand width.
- Update `OF` for signed overflow.
- Preserve `CF` exactly.
- Other real x86 flags remain unmodeled.

### Diagnostics

- `invalid-instruction-operands` for immediates, wrong operand counts, or unsupported operand shapes.
- `ambiguous-memory-width` for untyped memory destinations such as `inc [eax]`.
- Runtime memory diagnostics for invalid address, permission failure, `.CONST` overlap, and checked-memory failures.
- Diagnostics must preserve line, column, byte offset, and span length. Ambiguous memory-width diagnostics must point at the memory operand.

### Required tests

Parser tests:

- Accept register forms for AL, AX, EAX.
- Accept explicit BYTE/WORD/DWORD PTR memory forms.
- Accept direct typed symbols and symbol offsets.
- Reject immediate destinations.
- Reject extra operands.
- Reject ambiguous memory width.

Executor tests:

- `inc al`: `7Fh -> 80h`, `OF=1`, `CF` preserved.
- `dec al`: `80h -> 7Fh`, `OF=1`, `CF` preserved.
- `inc ax`: `7FFFh -> 8000h`, `OF=1`, `CF` preserved.
- `dec ax`: `8000h -> 7FFFh`, `OF=1`, `CF` preserved.
- `inc eax`: `FFFFFFFFh -> 00000000h`, `ZF=1`, `CF` preserved.
- `dec eax`: `00000000h -> FFFFFFFFh`, `SF=1`, `CF` preserved.
- Memory read-modify-write succeeds for writable memory.
- `.CONST` memory destination fails before mutation.

Source-run JSON tests:

- Successful register program.
- Successful memory program.
- Ambiguous memory-width diagnostic.
- Runtime `.CONST` write failure through computed address.

Rendered Simulator Messages tests:

- Exact rendered success message.
- Exact or stable rendered `ambiguous-memory-width` line.
- Rendered runtime memory-permission failure line.

### Acceptance program

```asm
.code
main PROC
    stc
    mov eax, 0FFFFFFFFh
    inc eax
    dec eax
main ENDP
END main
```

Expected:

```text
EAX = FFFFFFFFh / 4294967295
CF = 1
SF = 1
ZF = 0
OF = 0
```

---

## 48. Phase 44 - AND, OR, and XOR

### Goal

Implement `and`, `or`, and `xor` together because they share operand shape, destination mutation, width rules, and modeled flag behavior.

This phase must not implement `not`, shifts, rotates, `test`, `cmp`, jumps, multiplication, division, or future bitwise instructions.

### Behavior category

Runtime instruction behavior.

### Accepted syntax

```asm
and reg, reg
and reg, imm
and reg, mem
and mem, reg
and mem, imm

or reg, reg
or reg, imm
or reg, mem
or mem, reg
or mem, imm

xor reg, reg
xor reg, imm
xor reg, mem
xor mem, reg
xor mem, imm
```

Memory operands must have known width through explicit `PTR`, symbol metadata, symbol-relative metadata, or a register operand in the same instruction where applicable.

### Rejected syntax

```asm
and imm, reg
or  imm, reg
xor imm, reg
and [eax], 1      ; ambiguous width
or  [eax], 1      ; ambiguous width
xor [eax], 1      ; ambiguous width
and value, other  ; memory-to-memory rejected
```

### Runtime semantics

- `and` writes `dest & src` to the destination.
- `or` writes `dest | src` to the destination.
- `xor` writes `dest ^ src` to the destination.
- Immediate validation must reuse the existing destination-width immediate validator, including established negative-literal encoding rules.
- Memory reads and writes must go through checked VM memory helpers.

### Flag behavior

- Update `ZF` and `SF` from the result.
- Clear `CF` and `OF`.
- Other real x86 flags remain unmodeled until the extended flag phase.

### Diagnostics

- `invalid-instruction-operands` for invalid destinations, wrong counts, width mismatches, or memory-to-memory forms.
- `ambiguous-memory-width` for untyped memory/immediate forms.
- Runtime memory diagnostics for invalid checked-memory reads/writes.

### Required tests

Parser tests:

- Register/register, register/immediate, register/memory, memory/register, memory/immediate with explicit `PTR`.
- Direct symbol and symbol-offset memory forms.
- Signed `PTR` aliases by width.
- Rejected memory-to-memory forms.
- Rejected ambiguous memory/immediate forms.

Executor tests:

- 8-bit, 16-bit, and 32-bit operations.
- `xor eax, eax` sets `ZF=1`, clears `CF`/`OF`, clears `SF`.
- `and eax, 80000000h` sets `SF=1` when result has sign bit.
- `or` combines bits and clears `CF`/`OF`.
- Memory destination mutates only selected width.
- `.CONST` destination fails.

Source-run and rendered-message tests:

- Successful mixed register/memory program.
- `ambiguous-memory-width` rendered diagnostic.
- Runtime invalid memory source/destination diagnostic.

### Acceptance program

```asm
.code
main PROC
    mov eax, 0F0F0h
    and eax, 00FFh
    or  eax, 0100h
    xor eax, 000Fh
main ENDP
END main
```

Expected:

```text
EAX = 000001FFh / 511
CF = 0
OF = 0
ZF = 0
SF = 0
```

---

## 49. Phase 45 - NOT

### Goal

Implement `not` separately from the other bitwise instructions because it has one operand and must preserve all modeled flags.

This phase must not implement shifts, rotates, `and`, `or`, `xor`, multiplication, division, labels, or jumps.

### Behavior category

Runtime instruction behavior.

### Accepted syntax

```asm
not reg8
not reg16
not reg32
not BYTE PTR [reg32]
not WORD PTR [reg32]
not DWORD PTR [reg32]
not symbol
not symbol[offset]
```

### Rejected syntax

```asm
not 1
not eax, ebx
not [eax]        ; ambiguous width
not QWORD PTR q  ; executable QWORD/SQWORD memory operation remains deferred
```

### Runtime semantics

- Compute bitwise complement at the selected width.
- Write the result to the destination.
- Memory destinations are read-modify-write operations through checked memory helpers.

### Flag behavior

- Preserve `CF`, `ZF`, `SF`, and `OF` exactly.

### Required tests

- `not al` from `00h -> FFh` without changing flags.
- `not ax` from `0F0Fh -> F0F0h` without changing flags.
- `not eax` from `00000000h -> FFFFFFFFh` without changing flags.
- Memory destination mutation through `BYTE PTR`, `WORD PTR`, and `DWORD PTR`.
- `.CONST` destination failure.
- Ambiguous memory-width diagnostic.
- Wrong operand count diagnostic.

### Acceptance program

```asm
.code
main PROC
    stc
    mov eax, 0
    cmp eax, eax
    not eax
main ENDP
END main
```

Expected:

```text
EAX = FFFFFFFFh / 4294967295
CF = 0
ZF = 1
SF = 0
OF = 0
```

The flags remain as set by `cmp eax, eax`; `not` does not update them.

---

## 50. Phase 46 - SHL and SAL

### Goal

Implement `shl` and `sal` as aliases with MASM-compatible shift-count execution and educational warning/strict diagnostics for architecturally undefined modeled flags.

This phase must not implement `shr`, `sar`, rotates, multiplication, division, labels, or jumps.

### Behavior category

Runtime instruction behavior.

### Accepted syntax

```asm
shl reg, 1
shl reg, imm
shl reg, cl
shl mem, 1
shl mem, imm
shl mem, cl

sal reg, 1
sal reg, imm
sal reg, cl
sal mem, 1
sal mem, imm
sal mem, cl
```

Memory destinations must have known width through an explicit `PTR`, declaration metadata, symbol-offset metadata, or another existing global memory-width source.

`imm` is an encoded immediate shift count and must fit the implemented immediate-count range. `cl` means the low 8 bits of `ECX`; no other register count operand is accepted.

### Rejected syntax

Reject genuinely invalid or out-of-scope forms, including:

```asm
shl 1, al          ; immediate destination
shl eax, ebx       ; only CL is accepted as a register count
shl eax, cx        ; only CL is accepted as a register count
shl [eax], 1       ; ambiguous memory width
shl QWORD PTR q, 1 ; executable QWORD remains unsupported in MASM32 Educational Mode
```

These rejections are not shift-count compatibility rejections. Do not reject a MASM-valid instruction solely because the effective shift count is greater than or equal to the destination width.

### Shared count policy

- Raw count is an unsigned immediate or the low 8 bits of `CL`.
- Effective count is `raw_count & 31` for 8-bit, 16-bit, and 32-bit destinations.
- Effective count `0` is a no-op: destination and modeled flags unchanged.
- Nonzero effective counts execute using the destination width.
- Effective count greater than or equal to operand width is accepted in default MASM32 Educational Mode and executes with the same deterministic result computation as the repeated left-shift operation.
- This acceptance is the MASM/x86 compatibility policy. Undefined modeled flags are handled by warning or strict diagnostics, not by assembly rejection.

Examples:

```asm
shl eax, 32  ; raw 32 -> effective 0, accepted no-op
shl al, 8    ; raw 8 -> effective 8, accepted; warns by default because modeled flags are undefined
shl eax, cl  ; raw count is CL, effective count is CL & 31
```

### Runtime semantics

- Shift left by effective count.
- Fill low bits with zero.
- Apply the result at the selected destination width.
- `sal` must behave exactly like `shl`.
- Memory destinations are read-modify-write operations and must use checked memory read/write helpers.
- Failed validation, read, or write must not partially mutate registers, flags, memory, console state, or memory-change rows.

### Flag behavior

For effective count `0`:

- Destination is unchanged.
- `CF`, `ZF`, `SF`, and `OF` are unchanged.
- No undefined-shift warning is emitted.

For effective count `1`:

- `CF` is the bit shifted out of the most significant bit position.
- `ZF` and `SF` update from the result.
- `OF` is `new_sign_bit XOR CF`.
- No undefined-shift warning is emitted.

For effective count greater than `1` and less than operand width:

- `CF` is the last bit shifted out of the most significant bit position.
- `ZF` and `SF` update from the result.
- `OF` is architecturally undefined. Preserve the previous simulator `OF` value as the deterministic fallback.
- Default mode emits `undefined-shift-flag` as a warning.
- Shift undefined-flag strict mode reports `undefined-shift-flag` as a runtime error before mutation.

For effective count greater than or equal to operand width:

- The destination result is still computed and written in default mode.
- `ZF` and `SF` update from the result.
- `CF` and `OF` are architecturally undefined. Preserve their previous simulator values as the deterministic fallback.
- Default mode emits `undefined-shift-flag` as a warning.
- Shift undefined-flag strict mode reports `undefined-shift-flag` as a runtime error before mutation.

Required diagnostic code:

```text
undefined-shift-flag
```

Suggested warning wording:

```text
SHL/SAL with this effective count has architecturally undefined modeled flag result(s). The simulator executed the MASM-compatible shift and preserved deterministic values for undefined modeled flags.
```

### Required tests

Success and compatibility tests:

- `shl al, 1` with `80h` sets `CF=1`, result `00h`, `ZF=1`, and `OF=1`.
- `sal eax, 1` equals `shl eax, 1`.
- `shl eax, 32` is a no-op including flags and emits no warning because the effective count is `0`.
- `shl al, 8` is accepted and executes; the result is `00h`, `ZF=1`, `SF=0`, and default mode emits `undefined-shift-flag` rather than `unsupported-shift-count`.
- `shl eax, 2` executes and emits `undefined-shift-flag` because `OF` is undefined for multi-bit shifts.
- `shl BYTE PTR [esi], 1` uses checked memory read/write.
- `shl eax, cl` uses `CL`, not full `ECX`.

Diagnostic and strict-mode tests:

- `shl [esi], 1` reports `ambiguous-memory-width`.
- `shl eax, ebx` reports `invalid-instruction-operands` or the existing stable invalid-count-register diagnostic.
- `shl QWORD PTR q, 1` keeps the existing executable QWORD/SQWORD rejection in MASM32 Educational Mode.
- In default mode, `shl al, 8` has a structured warning and still completes execution.
- In shift undefined-flag strict mode, `shl al, 8` reports `undefined-shift-flag` as a runtime error and stops before write-back.
- Rendered Simulator Messages tests must cover both the default warning and strict runtime-error forms.
- Static or regression checks must ensure `unsupported-shift-count` is not used for MASM-valid `shl`/`sal` counts.

---

## 51. Phase 47 - SHR

### Goal

Implement logical right shift `shr` as a separate milestone.

This phase must not implement `sar`, rotates, multiplication, division, labels, or jumps.

### Behavior category

Runtime instruction behavior.

### Accepted and rejected syntax

Use the same destination/count forms and MASM-compatible count/undefined-flag diagnostic policy as Phase 46, replacing the mnemonic with `shr`.

### Runtime semantics

- Shift right by effective count.
- Fill high bits with zero.

### Flag behavior

For supported nonzero counts:

- `CF` is the last bit shifted out of the least significant bit position.
- `ZF` and `SF` update from the result.
- `OF` is the original most significant bit for effective count 1.
- For larger supported counts, preserve prior `OF` as the deterministic simulator policy.

### Required tests

- `shr al, 1` with `01h` sets `CF=1`, result `00h`, `ZF=1`.
- `shr al, 1` with `80h` produces `40h`, `SF=0`, `OF=1` for count 1.
- `shr eax, 32` is no-op.
- `shr al, 8` is accepted in default mode, executes with `undefined-shift-flag` warning, and fails before mutation only in shift undefined-flag strict mode.
- `shr eax, cl` uses `CL`.
- Memory destination and ambiguous memory-width diagnostics.

---

## 52. Phase 48 - SAR

### Goal

Implement arithmetic right shift `sar` as a separate milestone.

This phase must not implement rotates, multiplication, division, labels, or jumps.

### Behavior category

Runtime instruction behavior.

### Accepted and rejected syntax

Use the same destination/count forms and MASM-compatible count/undefined-flag diagnostic policy as Phase 46, replacing the mnemonic with `sar`.

### Runtime semantics

- Shift right by effective count.
- Fill high bits with the original sign bit.

### Flag behavior

For supported nonzero counts:

- `CF` is the last bit shifted out of the least significant bit position.
- `ZF` and `SF` update from the result.
- `OF` is cleared for effective count 1.
- For larger supported counts, preserve prior `OF` as the deterministic simulator policy.

### Required tests

- `sar al, 1` with `80h` produces `C0h`, `SF=1`, `OF=0`.
- `sar al, 1` with `01h` produces `00h`, `CF=1`, `ZF=1`.
- `sar eax, 32` is no-op.
- `sar al, 8` is accepted in default mode, executes with `undefined-shift-flag` warning, and fails before mutation only in shift undefined-flag strict mode.
- `sar eax, cl` uses `CL`.
- Memory destination and ambiguous memory-width diagnostics.

---

## 53. Phase 49 - ROL

### Goal

Implement rotate-left `rol` as a focused rotate milestone.

This phase implements only `ROL`. It must not implement `ROR`, carry rotates, shifts, multiplication, division, labels, jumps, smart undefined-flag validity metadata, or flag-consumer diagnostics.

### Behavior category

Runtime instruction behavior.

### Dependencies

- Phase 46 - SHL and SAL, for established count-source parsing patterns.
- Phase 47 - SHR, for shift-count compatibility precedent.
- Phase 48 - SAR, for completed shift-family undefined modeled flag diagnostics.
- Existing global memory-width resolution rules.
- Existing checked memory helpers.
- Existing rendered Simulator Messages test harness.

### Accepted syntax

```asm
rol reg, 1
rol reg, imm
rol reg, cl
rol mem, 1
rol mem, imm
rol mem, cl
```

Accepted register destinations:

```text
reg8
reg16
reg32
```

Accepted memory destinations:

```asm
rol BYTE PTR [eax], 1
rol WORD PTR [eax], 1
rol DWORD PTR [eax], 1
rol SBYTE PTR [eax], 1
rol SWORD PTR [eax], 1
rol SDWORD PTR [eax], 1
rol value, 1
rol values[4], 1
```

Memory destinations must have known width through one of the already-supported width sources:

- explicit `BYTE PTR`, `WORD PTR`, or `DWORD PTR`;
- explicit signed `SBYTE PTR`, `SWORD PTR`, or `SDWORD PTR`;
- typed symbol metadata;
- typed symbol-offset metadata;
- another already-implemented unambiguous width-resolution rule.

### Rejected syntax

```asm
rol 1, al
rol eax, ebx
rol eax, cx
rol eax, ecx
rol eax
rol eax, 1, 2
rol [eax], 1
rol QWORD PTR q, 1
rol SQWORD PTR q, 1
```

Expected diagnostics:

- immediate destination: `invalid-instruction-operands`;
- count register other than `CL`: `invalid-instruction-operands` or existing stable invalid-count-register diagnostic;
- wrong operand count: `invalid-instruction-operands`;
- untyped memory destination: `ambiguous-memory-width`;
- executable `QWORD PTR` / `SQWORD PTR`: existing MASM32 Educational Mode unsupported-runtime diagnostic.

Do not classify `rol al, 8`, `rol eax, 32`, or `rol eax, cl` as unsupported syntax.

### Count policy

Raw count source:

- immediate byte count; or
- low 8 bits of `ECX` when the count operand is `CL`.

Effective count:

```text
effective_count = raw_count & 31
```

Count behavior:

- Effective count `0` is a full no-op:
  - destination unchanged;
  - `CF`, `ZF`, `SF`, and `OF` unchanged;
  - no undefined-modeled-flag warning.
- Nonzero effective count rotates by:

  ```text
  rotate_count = effective_count % operand_width
  ```

- Nonzero effective count greater than or equal to operand width is not rejected.
- If `rotate_count` is `0` but effective count is nonzero, the destination bits are unchanged, but nonzero-count flag behavior still applies.
- For nonzero effective count where `rotate_count == 0`, do not treat the instruction as a full no-op. The destination bits are unchanged, but nonzero-count rotate flag behavior still applies: `CF` is updated according to the rotate result, `ZF` and `SF` are preserved, and `OF` is undefined unless the effective count is exactly `1`.
- This rotate policy is distinct from shift behavior. Do not copy shift `CF`/`OF` rules blindly.

### Runtime semantics

For nonzero effective counts:

- Rotate bits left within the selected operand width.
- Bits shifted out of the most significant bit position re-enter at the least significant bit position.
- Apply the rotated result at the selected destination width.
- Register destinations mutate only the selected register or alias width.
- Memory destinations are read-modify-write operations through checked memory helpers.
- Failed validation, failed memory read, or failed memory write must not partially mutate registers, flags, memory, console state, or memory-change rows.

### Flag behavior

For effective count `0`:

- destination unchanged;
- `CF` unchanged;
- `OF` unchanged;
- `ZF` unchanged;
- `SF` unchanged;
- no undefined-modeled-flag warning.

For nonzero effective count:

- `CF` becomes the least significant bit of the rotated result.
- `ZF` remains unchanged.
- `SF` remains unchanged.

For one-bit rotates where `effective_count == 1`:

- `OF = new_most_significant_bit XOR CF`.
- No undefined-modeled-flag warning is emitted.

For other nonzero effective counts where `effective_count != 1`:

- `OF` is architecturally undefined.
- Preserve the previous simulator `OF` value as the deterministic fallback.
- Default mode emits an eager producer warning.
- This phase must not stop before mutation merely because `OF` is undefined.
- This phase must not implement smart flag-validity metadata. Phase 50A owns metadata.
- This phase must not implement use-warning or use-error behavior. Phase 50B owns flag-consumer diagnostics.

Required producer warning code:

```text
undefined-modeled-flag
```

Suggested warning wording:

```text
ROL count <raw_count> has effective count <effective_count> for a <width>-bit destination. CF was updated from the rotated result. ZF and SF were preserved because ROL does not modify them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.
```

If the project chooses to reuse the existing shift-specific code temporarily, document that as a compatibility exception. Preferred new rotate code is `undefined-modeled-flag`, not `undefined-shift-flag`.

### Required tests

Parser tests:

- `rol al, 1`
- `rol ax, 1`
- `rol eax, 1`
- `rol eax, 0`
- `rol eax, 32`
- `rol eax, cl`
- mixed-case mnemonic such as `RoL eax, cl`
- explicit unsigned PTR memory destinations;
- explicit signed PTR alias memory destinations;
- typed symbol destination;
- typed symbol-offset destination;
- every rejected form listed above.

Executor tests:

- `rol al, 1` with `80h` produces `01h`, `CF=1`, and defined `OF`.
- `rol al, 1` with `40h` produces `80h`, `CF=0`, and defined `OF`.
- `rol al, 8` has nonzero effective count and rotate count zero:
  - destination unchanged;
  - `CF` updated according to nonzero rotate policy;
  - `OF` preserved and warned because effective count is not 1.
- `rol eax, 32` is a full no-op because effective count is zero:
  - destination unchanged;
  - all modeled flags unchanged;
  - no warning.
- `rol eax, cl` uses `CL`, not full `ECX`.
- `ZF` and `SF` remain unchanged across a rotate that changes the visible sign bit.
- Memory destination success uses checked read/write helpers.
- Invalid memory read fails without mutation.
- Failed memory write restores CPU/flag state and creates no successful memory-change rows.
- `.CONST` memory destination fails through existing static or runtime protection as appropriate.

Source-run JSON tests:

```asm
.code
main PROC
    mov al, 80h
    rol al, 1
main ENDP
END main
```

Expected:

```text
AL = 01h
CF = 1
OF defined according to one-bit ROL rule
No undefined-modeled-flag warning
```

```asm
.code
main PROC
    mov al, 80h
    rol al, 2
main ENDP
END main
```

Expected default behavior:

```text
Program executes successfully.
Destination is rotated.
CF is updated.
OF is preserved.
A simulator warning with code undefined-modeled-flag is emitted.
execution-complete is present.
```

Rendered Simulator Messages tests:

- successful `rol al, 1` has only execution-complete;
- `rol al, 2` has `undefined-modeled-flag` warning and execution-complete;
- ambiguous memory width renders correctly;
- invalid count register renders correctly;
- invalid memory/runtime diagnostic renders correctly.

Regression tests:

- Phase 46 `shl` / `sal` behavior unchanged.
- Phase 47 `shr` behavior unchanged.
- Phase 48 `sar` behavior unchanged.
- Existing `undefined-shift-flag` tests remain stable.
- No `ROR` opcode, parser case, or executor behavior is added in this phase.
- No flag-validity metadata is added in this phase.

### Acceptance program

```asm
.code
main PROC
    mov al, 80h
    rol al, 1
main ENDP
END main
```

Expected:

```text
AL = 01h
CF = 1
ZF and SF remain unchanged by ROL.
No undefined-modeled-flag warning.
```

---

## 54. Phase 50 - ROR

### Goal

Implement rotate-right `ror` as a focused rotate milestone.

This phase implements only `ROR`. It must not implement carry rotates, multiplication, division, labels, jumps, smart undefined-flag validity metadata, or flag-consumer diagnostics.

### Behavior category

Runtime instruction behavior.

### Dependencies

- Phase 49 - ROL, for shared rotate destination/count policy.
- Existing global memory-width resolution rules.
- Existing checked memory helpers.
- Existing rendered Simulator Messages test harness.

### Accepted syntax

Use the same destination/count forms and rotate count policy as **Phase 49 - ROL**, replacing the mnemonic with `ror`.

Accepted forms are:

```asm
ror reg, 1
ror reg, imm
ror reg, cl
ror mem, 1
ror mem, imm
ror mem, cl
```

Accepted register destinations:

```text
reg8
reg16
reg32
```

Accepted memory destinations:

```asm
ror BYTE PTR [eax], 1
ror WORD PTR [eax], 1
ror DWORD PTR [eax], 1
ror SBYTE PTR [eax], 1
ror SWORD PTR [eax], 1
ror SDWORD PTR [eax], 1
ror value, 1
ror values[4], 1
```

Memory destinations must have known width through an explicit `PTR`, signed `PTR` alias, symbol metadata, symbol-offset metadata, or another already-supported width-resolution rule.

### Rejected syntax

```asm
ror 1, al
ror eax, ebx
ror eax, cx
ror eax, ecx
ror eax
ror eax, 1, 2
ror [eax], 1
ror QWORD PTR q, 1
ror SQWORD PTR q, 1
```

Expected diagnostics match Phase 49 categories:

- immediate destination: `invalid-instruction-operands`;
- count register other than `CL`: `invalid-instruction-operands` or existing stable invalid-count-register diagnostic;
- wrong operand count: `invalid-instruction-operands`;
- untyped memory destination: `ambiguous-memory-width`;
- executable `QWORD PTR` / `SQWORD PTR`: existing MASM32 Educational Mode unsupported-runtime diagnostic.

Do not reference Phase 47 for `ROR` count behavior. Phase 47 is `SHR`, not a rotate phase.

### Count policy

Raw count source:

- immediate byte count; or
- low 8 bits of `ECX` when the count operand is `CL`.

Effective count:

```text
effective_count = raw_count & 31
```

Count behavior:

- Effective count `0` is a full no-op:
  - destination unchanged;
  - `CF`, `ZF`, `SF`, and `OF` unchanged;
  - no undefined-modeled-flag warning.
- Nonzero effective count rotates by:

  ```text
  rotate_count = effective_count % operand_width
  ```

- Nonzero effective count greater than or equal to operand width is not rejected.
- If `rotate_count` is `0` but effective count is nonzero, the destination bits are unchanged, but nonzero-count flag behavior still applies.
- For nonzero effective count where `rotate_count == 0`, do not treat the instruction as a full no-op. The destination bits are unchanged, but nonzero-count rotate flag behavior still applies: `CF` is updated according to the rotate result, `ZF` and `SF` are preserved, and `OF` is undefined unless the effective count is exactly `1`.

### Runtime semantics

For nonzero effective counts:

- Rotate bits right within the selected operand width.
- Bits shifted out of the least significant bit position re-enter at the most significant bit position.
- Apply the rotated result at the selected destination width.
- Register destinations mutate only the selected register or alias width.
- Memory destinations are read-modify-write operations through checked memory helpers.
- Failed validation, failed memory read, or failed memory write must not partially mutate registers, flags, memory, console state, or memory-change rows.

### Flag behavior

For effective count `0`:

- destination unchanged;
- `CF` unchanged;
- `OF` unchanged;
- `ZF` unchanged;
- `SF` unchanged;
- no undefined-modeled-flag warning.

For nonzero effective count:

- `CF` becomes the most significant bit of the rotated result.
- `ZF` remains unchanged.
- `SF` remains unchanged.

For one-bit rotates where `effective_count == 1`:

- `OF` is the XOR of the two most significant bits of the result.
- No undefined-modeled-flag warning is emitted.

For other nonzero effective counts where `effective_count != 1`:

- `OF` is architecturally undefined.
- Preserve the previous simulator `OF` value as the deterministic fallback.
- Default mode emits an eager producer warning.
- This phase must not stop before mutation merely because `OF` is undefined.
- This phase must not implement smart flag-validity metadata. Phase 50A owns metadata.
- This phase must not implement use-warning or use-error behavior. Phase 50B owns flag-consumer diagnostics.

Required producer warning code:

```text
undefined-modeled-flag
```

Suggested warning wording:

```text
ROR count <raw_count> has effective count <effective_count> for a <width>-bit destination. CF was updated from the rotated result. ZF and SF were preserved because ROR does not modify them. OF is architecturally undefined because the effective count is not 1. The simulator preserved OF deterministically.
```

### Required tests

Parser tests:

- `ror al, 1`
- `ror ax, 1`
- `ror eax, 1`
- `ror eax, 0`
- `ror eax, 32`
- `ror eax, cl`
- mixed-case mnemonic such as `RoR eax, cl`
- explicit unsigned PTR memory destinations;
- explicit signed PTR alias memory destinations;
- typed symbol destination;
- typed symbol-offset destination;
- every rejected form listed above.

Executor tests:

- `ror al, 1` with `01h` produces `80h`, `CF=1`, and defined `OF`.
- `ror al, 1` with `02h` produces `01h`, `CF=0`, and defined `OF`.
- `ror al, 8` has nonzero effective count and rotate count zero:
  - destination unchanged;
  - `CF` updated according to nonzero rotate policy;
  - `OF` preserved and warned because effective count is not 1.
- `ror eax, 32` is a full no-op because effective count is zero:
  - destination unchanged;
  - all modeled flags unchanged;
  - no warning.
- `ror eax, cl` uses `CL`, not full `ECX`.
- `ZF` and `SF` remain unchanged across a rotate that changes the visible sign bit.
- Memory destination success uses checked read/write helpers.
- Invalid memory read fails without mutation.
- Failed memory write restores CPU/flag state and creates no successful memory-change rows.
- `.CONST` memory destination fails through existing static or runtime protection as appropriate.

Source-run JSON tests:

```asm
.code
main PROC
    mov al, 01h
    ror al, 1
main ENDP
END main
```

Expected:

```text
AL = 80h
CF = 1
OF defined according to one-bit ROR rule
No undefined-modeled-flag warning
```

```asm
.code
main PROC
    mov al, 01h
    ror al, 2
main ENDP
END main
```

Expected default behavior:

```text
Program executes successfully.
Destination is rotated.
CF is updated.
OF is preserved.
A simulator warning with code undefined-modeled-flag is emitted.
execution-complete is present.
```

Rendered Simulator Messages tests:

- successful `ror al, 1` has only execution-complete;
- `ror al, 2` has `undefined-modeled-flag` warning and execution-complete;
- ambiguous memory width renders correctly;
- invalid count register renders correctly;
- invalid memory/runtime diagnostic renders correctly.

Regression tests:

- Phase 49 `rol` behavior unchanged.
- Phase 46-48 shift behavior unchanged.
- Existing `undefined-shift-flag` tests remain stable.
- No flag-validity metadata is added in this phase.
- No flag-consumer diagnostics are added in this phase.

### Acceptance program

```asm
.code
main PROC
    mov al, 01h
    ror al, 1
main ENDP
END main
```

Expected:

```text
AL = 80h
CF = 1
ZF and SF remain unchanged by ROR.
No undefined-modeled-flag warning.
```

---

## 54A. Phase 50A - Undefined Modeled Flag Validity Metadata

### Goal

Add metadata that records whether each currently modeled flag value is architecturally valid or was preserved as a deterministic fallback after an undefined-flag producer instruction.

This phase does not change ordinary instruction results. It adds validity metadata so later phases can warn or error when code actually depends on an undefined flag.

### Behavior category

Core CPU/flag metadata and diagnostics infrastructure.

### Dependencies

- Phase 46 - SHL and SAL.
- Phase 47 - SHR.
- Phase 48 - SAR.
- Phase 49 - ROL.
- Phase 50 - ROR.
- Existing CPU/EFLAGS helper model.
- Existing source-span metadata on IR instructions.
- Existing source-run JSON and rendered Simulator Messages test harness.

### Scope

This phase covers currently modeled flags only:

```text
CF
ZF
SF
OF
```

Do not add `PF`, `AF`, or `DF` in this phase.

Do not change the visible deterministic flag values computed by existing instructions.

Do not implement new instructions.

Do not implement conditional jumps or labels.

Do not implement consumer diagnostics beyond native/helper-level validation unless a real flag-consuming instruction already exists in the implementation at the time this phase is reached.

Phase 50A is metadata-only for normal source execution. It must not change default warning behavior, final register values, final flag values, Program Console output, or Simulator Messages output except where internal test-only JSON exposure is explicitly added and documented.

### Required data model

Add validity metadata for every currently modeled flag.

Conceptual model:

```text
flag_value[flag]:
  Existing deterministic simulator bit value.

flag_valid[flag]:
  true if the flag value is architecturally valid.
  false if the flag value was preserved because the producing instruction left that flag architecturally undefined.

flag_undefined_code[flag]:
  Stable reason code, such as undefined-modeled-flag or undefined-shift-flag.

flag_undefined_producer_mnemonic[flag]:
  Source mnemonic that made the flag invalid, such as shl, shr, sar, rol, or ror.

flag_undefined_producer_span[flag]:
  Source line, column, byte offset, and span length for the producer instruction when available.
```

The exact C representation may differ, but it must support these behaviors and must remain C99.

### Required validity rules

When an instruction defines a modeled flag architecturally:

```text
set flag value
mark flag valid
clear undefined-origin metadata for that flag
```

When an instruction explicitly clears or sets a modeled flag by contract:

```text
set flag value
mark flag valid
clear undefined-origin metadata for that flag
```

Examples:

```text
clc -> CF value 0, CF valid
stc -> CF value 1, CF valid
cmc -> CF toggled, CF valid
test -> CF valid, OF valid, ZF valid, SF valid
```

When an instruction preserves a modeled flag architecturally:

```text
preserve flag value
preserve flag validity
preserve undefined-origin metadata
```

Examples:

```text
mov preserves all modeled flags and their validity metadata
not preserves all modeled flags and their validity metadata
rol preserves ZF/SF and their validity metadata
ror preserves ZF/SF and their validity metadata
```

When an instruction makes a modeled flag architecturally undefined:

```text
preserve deterministic simulator flag value
mark flag invalid
store undefined-origin metadata
```

Examples:

```text
shl eax, 2:
  OF invalid
  CF/ZF/SF valid according to existing Phase 46 behavior

shl al, 8:
  CF invalid
  OF invalid
  ZF/SF valid according to existing Phase 46 behavior

shr al, 8:
  CF invalid
  OF invalid
  ZF/SF valid according to existing Phase 47 behavior

sar al, 8:
  CF invalid
  OF invalid
  ZF/SF valid according to existing Phase 48 behavior

rol eax, 2:
  OF invalid
  CF valid
  ZF/SF validity preserved

ror eax, 2:
  OF invalid
  CF valid
  ZF/SF validity preserved
```

### Default initialization

On VM reset/load:

```text
CF/ZF/SF/OF values use the existing reset policy.
CF/ZF/SF/OF validity is true.
Undefined-origin metadata is empty.
```

If tests or APIs can explicitly set raw EFLAGS, they must also have a defined validity behavior:

```text
Raw EFLAGS write marks written modeled flags valid unless the API explicitly supplies validity metadata.
```

### Source-run JSON

Expose flag validity metadata in source-run JSON only if the project can do so without destabilizing existing UI output. If exposed, use a structured shape such as:

```json
{
  "flags": {
    "CF": {"value": 0, "valid": true},
    "ZF": {"value": 1, "valid": true},
    "SF": {"value": 0, "valid": true},
    "OF": {
      "value": 0,
      "valid": false,
      "undefinedCode": "undefined-modeled-flag",
      "producerMnemonic": "rol",
      "producerLine": 4,
      "producerColumn": 5
    }
  }
}
```

If flag-validity metadata is not exposed in source-run JSON in this phase, that is acceptable. Native executor/helper tests must still prove the metadata exists and is updated correctly. UI, debugger, and final-output display of validity metadata are future work unless explicitly assigned here.

### Diagnostics

This phase should not add new default source-level diagnostics beyond preserving the existing Phase 46-50 producer warnings.

It must not remove:

```text
undefined-shift-flag
undefined-modeled-flag
```

from existing producer-warning paths.

This phase must not introduce `undefined-flag-use` source-level diagnostics unless a real flag-consuming instruction already exists.

### Required tests

Native CPU/flag metadata tests:

- VM reset marks `CF`, `ZF`, `SF`, and `OF` valid.
- Defining `ZF` through an existing flag-setting instruction marks `ZF` valid.
- Clearing `CF` through `clc` marks `CF` valid.
- Preserving flags through `mov` preserves validity metadata.
- Preserving flags through `not` preserves validity metadata.

Executor tests:

- `shl eax, 2` marks `OF` invalid and leaves `CF`, `ZF`, and `SF` valid.
- `shl al, 8` marks `CF` and `OF` invalid and leaves `ZF` and `SF` valid.
- `shr al, 8` marks `CF` and `OF` invalid and leaves `ZF` and `SF` valid.
- `sar al, 8` marks `CF` and `OF` invalid and leaves `ZF` and `SF` valid.
- `rol eax, 2` marks `OF` invalid, marks `CF` valid, and preserves `ZF`/`SF` validity.
- `ror eax, 2` marks `OF` invalid, marks `CF` valid, and preserves `ZF`/`SF` validity.
- `rol eax, 1` marks `OF` and `CF` valid.
- `ror eax, 1` marks `OF` and `CF` valid.
- `rol eax, 32` preserves all flag values and validity metadata because effective count is zero.
- `ror eax, 32` preserves all flag values and validity metadata because effective count is zero.

Rollback tests:

- A memory-destination shift or rotate that fails validation must not change flag values or flag validity metadata.
- A memory write failure after tentative computation must restore flag values and flag validity metadata.
- Runtime `.CONST` permission failure must not change flag values or flag validity metadata.

Source-run tests:

If JSON exposes validity metadata:

```asm
.code
main PROC
    mov eax, 1
    rol eax, 2
main ENDP
END main
```

Expected:

```text
OF value preserved
OF valid=false
OF producerMnemonic=rol
CF valid=true
execution-complete present
```

If JSON does not expose validity metadata, equivalent native executor tests are required and the source-run output must remain backward-compatible.

Rendered Simulator Messages tests:

- Existing producer warning rendering for `rol eax, 2` remains unchanged from Phase 49.
- Existing producer warning rendering for `ror eax, 2` remains unchanged from Phase 50.
- Existing shift warning rendering remains unchanged.
- No new `undefined-flag-use` rendered message appears in this phase unless a real flag consumer already exists.

### Acceptance criteria

- Every modeled flag has validity metadata.
- Existing flag values and existing producer warnings remain stable.
- Shift and rotate undefined flags are marked invalid.
- Architecturally defined flag writes mark flags valid.
- Architecturally preserved flags preserve validity metadata.
- Failed instructions do not partially mutate flag values or validity metadata.
- No new instructions are implemented.
- No conditional jumps or flag consumers are implemented.
- No browser settings UI is implemented.

---

## 54B. Phase 50B - Undefined Flag Use Diagnostics for Flag Consumers

### Goal

Add a shared diagnostic mechanism for instructions that consume flags whose current values are marked architecturally undefined.

This phase implements the smart educational model:

```text
The producer instruction executes.
Undefined modeled flags are preserved deterministically and marked invalid.
A warning or error occurs only when later code consumes an invalid flag.
```

This phase must not reject MASM-valid producer instructions merely because they create undefined flag results.

### Behavior category

Core diagnostic infrastructure and future flag-consumer contract.

### Dependencies

- Phase 50A - Undefined Modeled Flag Validity Metadata.
- Existing diagnostic rendering harness.
- Existing source-span metadata.
- Existing source-run JSON path.
- Existing branch/flag-consumer phases if they have already been implemented in the local repository state.

### Scope

This phase adds:

- a shared helper for checking validity before a flag is consumed;
- reporting-mode support for use-warning and use-error behavior;
- structured diagnostics for invalid flag consumption;
- tests for the helper;
- integration into already-implemented flag consumers, if any exist when this phase is implemented;
- explicit requirements for later conditional jump phases to use this helper.

This phase must not implement labels, jumps, `cmp`, `loop`, `SETcc`, `CMOVcc`, or any other new flag-consuming instruction if those instructions are not already implemented.

If no source-level flag-consuming instruction exists when Phase 50B is implemented, this phase is still valid as an infrastructure phase. In that case, it must include native/helper tests and must update later flag-consumer phase text so those consumers call the helper when implemented.

### Reporting modes

Define a setting or internal option equivalent to:

```text
undefined_flag_use_policy = off
undefined_flag_use_policy = warn
undefined_flag_use_policy = error
```

`undefined_flag_use_policy` controls only diagnostics emitted when a later instruction consumes an invalid flag. It does not control producer warnings such as `undefined-shift-flag` or `undefined-modeled-flag`. Producer-warning policy remains owned by the instruction phase that creates the undefined flag result, or by a later explicit diagnostics/settings phase.

Required behavior:

```text
off:
  Do not diagnose invalid flag consumption.
  Consumers use the deterministic preserved flag value.

warn:
  Emit a runtime warning at the consumer instruction.
  Continue execution using the deterministic preserved flag value.

error:
  Emit a runtime error at the consumer instruction.
  Stop before the consumer makes a flag-dependent decision.
```

Default user-facing mode after this phase should remain compatible with earlier behavior unless the guide explicitly changes it:

```text
Default MASM32 Educational Mode:
  Producer eager warnings may still exist for shift/rotate undefined flags.
  Undefined flag use diagnostics are warning-capable but do not have to become default-on until a settings or diagnostics phase chooses that UI behavior.
```

Recommended future user settings:

```text
Undefined flag diagnostics:
  - warn when produced
  - warn when used
  - error when used
```

Do not expose a browser UI setting in this phase unless a settings/UI phase explicitly owns it.

### Diagnostic code

Use this diagnostic code for consumer diagnostics:

```text
undefined-flag-use
```

Severity:

```text
warn mode:
  runtime-warning or simulator-warning, matching existing warning categories

error mode:
  runtime-error
```

The diagnostic must preserve:

- consumer line;
- consumer column;
- consumer byte offset;
- consumer span length;
- consumed flag name;
- producer mnemonic when available;
- producer line/column when available;
- producer diagnostic code when available.

Suggested warning wording:

```text
<CONSUMER> reads <FLAG>, but <FLAG> is architecturally undefined from <PRODUCER> at line <producer-line>. The simulator preserved <FLAG> deterministically; this flag-dependent behavior is not portable.
```

Suggested runtime-error wording:

```text
<CONSUMER> reads <FLAG>, but <FLAG> is architecturally undefined from <PRODUCER> at line <producer-line>. Execution stopped before using the undefined flag.
```

If producer metadata is unavailable, use:

```text
<CONSUMER> reads <FLAG>, but <FLAG> is currently marked architecturally undefined. Execution used or stopped before using the deterministic simulator fallback according to the active undefined-flag-use policy.
```

### Consumer helper contract

Add a shared helper conceptually equivalent to:

```text
check_flag_consumption(required_flags, consumer_instruction, policy)
```

The helper must:

1. inspect validity metadata for every required flag;
2. collect every invalid required flag;
3. return success if all required flags are valid;
4. in `off` mode, return success even if some flags are invalid;
5. in `warn` mode, emit one diagnostic per consumer instruction, listing all invalid consumed flags;
6. in `error` mode, emit one diagnostic per consumer instruction, listing all invalid consumed flags, then stop before the consumer makes a branch/condition decision;
7. preserve registers, memory, flags, console state, and memory-change rows on `error`;
8. not clear validity metadata merely because a warning was emitted.

A consumer that reads multiple flags must report all invalid flags in one diagnostic. The structured diagnostic must preserve the complete invalid-flag list even if the rendered message uses compact wording.

Example:

```text
JG reads ZF, SF, and OF.
If SF and OF are invalid, emit one undefined-flag-use diagnostic naming SF and OF.
```

### Flag consumers

Known current/future flag consumers include:

```text
jo / jno        read OF
js / jns        read SF
jz / je         read ZF
jnz / jne       read ZF
jc / jb / jnae  read CF
jnc / jae / jnb read CF
jl / jnge       read SF and OF
jle / jng       read ZF, SF, and OF
jg / jnle       read ZF, SF, and OF
jge / jnl       read SF and OF
ja / jnbe       read CF and ZF
jbe / jna       read CF and ZF
```

Future PF consumers after PF exists:

```text
jp / jpe        read PF
jnp / jpo       read PF
```

Do not implement any unimplemented jump mnemonic in this phase.

### Later phase integration rule

Every later phase that implements a flag-consuming instruction must call the Phase 50B helper before making the flag-dependent decision.

If the helper reports `error`, the consumer instruction must not:

- branch;
- advance based on the branch result;
- mutate registers;
- mutate memory;
- mutate flags;
- write Program Console output;
- create memory-change rows.

If the helper reports `warn`, the consumer may continue using the deterministic preserved flag value.

### Updates required in later conditional jump phases

When editing or implementing later conditional jump phases, add this requirement to each phase:

```text
Before evaluating the branch condition, call the Phase 50B undefined-flag-use helper with the exact flags read by this condition. In warn mode, emit undefined-flag-use and continue using deterministic preserved flag values. In error mode, stop before making the branch decision.
```

Apply this requirement to:

- Phase 64 - Equality Conditional Jumps;
- Phase 65 - Signed Relational Conditional Jumps;
- Phase 66 - Unsigned Relational Conditional Jumps;
- any later loop or condition-code phase that reads flags.

### Required tests

Native/helper tests:

- consuming a valid flag returns success;
- consuming one invalid flag in `off` mode returns success and emits no diagnostic;
- consuming one invalid flag in `warn` mode emits `undefined-flag-use` and returns success;
- consuming one invalid flag in `error` mode emits `undefined-flag-use` and returns runtime-error status;
- consuming multiple invalid flags emits one diagnostic listing all invalid consumed flags;
- warning does not clear validity metadata;
- error does not mutate flag values or validity metadata.

Metadata tests:

```text
producer: rol eax, 2
invalid flag: OF
consumer request: OF
warn mode: diagnostic points to consumer and mentions producer rol
error mode: diagnostic points to consumer and stops before decision
```

If a real flag-consuming instruction exists in the repository when this phase is implemented, add source-run tests. Example with future `JO`:

```asm
.code
main PROC
    mov eax, 1
    rol eax, 2
    jo used_of
    mov ebx, 0
    jmp done
used_of:
    mov ebx, 1
done:
main ENDP
END main
```

Expected in warn mode:

```text
ROL executes.
OF is marked invalid.
JO emits undefined-flag-use warning.
JO then uses the deterministic preserved OF value.
Execution completes.
```

Expected in error mode:

```text
ROL executes.
OF is marked invalid.
JO emits undefined-flag-use runtime error.
Execution stops before deciding whether JO is taken.
EBX is unchanged from before JO's successor path.
No execution-complete message.
```

If no real flag-consuming instruction exists yet, source-run consumer tests are deferred. The phase must add helper tests and later-phase guide requirements instead.

Rendered Simulator Messages tests:

- `undefined-flag-use` warning rendering, using a native diagnostic fixture if no source-level consumer exists.
- `undefined-flag-use` runtime-error rendering, using a native diagnostic fixture if no source-level consumer exists.
- Diagnostic includes consumer source location.
- Diagnostic includes producer information when available.

Regression tests:

- Producer instructions such as `rol eax, 2`, `ror eax, 2`, `shl eax, 2`, and `sar al, 8` still execute in default mode.
- Existing producer warnings remain unchanged unless this phase explicitly changes the active reporting mode.
- No producer instruction becomes a syntax error because of undefined flags.
- Existing shift legacy strict API behavior remains stable unless a later corrective phase explicitly migrates it.

### Acceptance criteria

- A shared flag-consumption validity helper exists.
- `undefined-flag-use` diagnostic exists and is rendered.
- `warn` and `error` consumer policies are testable.
- `undefined_flag_use_policy` affects only consumer diagnostics.
- Existing producer warnings remain under their existing instruction-phase policy.
- The browser default is not changed by this phase unless a settings/UI phase explicitly requests it.
- Error mode stops at the consumer, not at the producer.
- Warning mode continues using deterministic preserved flag values.
- No new flag-consuming instruction is implemented merely to test this phase.
- Later conditional jump phases have explicit instructions to use the helper.
- Producer instructions remain MASM-compatible and executable.

---


### Corrective note after later default-policy phase

Phase 50B implemented the `undefined-flag-use` diagnostic mechanism with explicit policies:

```text
off
warn
error
```

Phase 50B preserved the then-current browser/source-run default by keeping the default policy `off`.

A later default teaching-diagnostics phase intentionally changes the default policy from `off` to `warn`.

After that later phase:

```text
undefined_flag_use_policy default = warn
```

The explicit `off` policy remains available and preserves the original Phase 50B silent-consumer behavior.

The `error` policy remains available and still stops before the consumer instruction uses the undefined modeled flag.

Do not reinterpret Phase 50B history as if default consumer warnings existed from the beginning. Treat the default change as a later policy change.

## 55. Phase 51 - Post-30 Memory, Diagnostic, Irvine, and Instruction Integration Smoke Harness

### Goal

Perform a validation-only smoke harness over the post-30 memory, diagnostic, virtual Irvine, and instruction changes.

This phase must not add MASM syntax or runtime behavior.

### Required coverage

- Native diagnostic JSON producer plus Node renderer still agree after memory-layout and instruction changes.
- Fixed layout and automatic deterministic layout produce equivalent results for a small `.data`, `.const`, `.data?`, and instruction program where addresses are not asserted.
- `.CONST` runtime write rejection remains higher precedence than object-bound diagnostics.
- Uninitialized-read warning mode catches a read-modify-write instruction before the destination becomes initialized.
- `INCLUDE Irvine32.inc` accepts `exit`, `Exit`, and `EXIT` equivalently, while user symbols still follow the documented case policy.
- `inc/dec`, `and/or/xor`, `not`, `shl/sal`, `shr`, `sar`, `rol`, and `ror` each have one source-run smoke test and one rendered diagnostic smoke test.

### Non-goals

No CALL, RET, stack behavior, full Irvine32 routines, macros, or future instruction groups.

### Acceptance criteria

The aggregate test runner reports every source-run program exercised, every expected rendered diagnostic line, and whether a browser manual smoke test was also run after rebuilding Wasm.

## 56. Phase 52 - LEA

### Goal

Implement `lea` as effective-address computation without memory reads.

This phase must not implement scaled-index addressing, multiplication, division, labels, jumps, stack behavior, or procedure behavior.

### Behavior category

Runtime address-computation instruction.

### Accepted syntax

```asm
lea reg32, symbol
lea reg32, symbol[offset]
lea reg32, [symbol + constant]
lea reg32, [reg32]
lea reg32, [reg32 + constant]
lea reg32, [reg32 - constant]
lea reg32, symbol[reg32]
lea reg32, [symbol + reg32]
```

Only memory-expression shapes already supported by the current addressing parser are accepted.

### Rejected syntax

```asm
lea mem, symbol
lea eax, ebx
lea eax, 123
lea eax, OFFSET symbol
lea ax, symbol
lea al, symbol
lea eax, [eax * 4]       ; until scaled-index addressing exists
lea eax, [base + index * 4]
```

### Runtime semantics

- Compute effective address only.
- Write the computed 32-bit address to the destination register.
- Do not read memory.
- Do not write memory.
- Do not require the computed address to be mapped, aligned, readable, writable, or inside a declared object.
- Do not emit invalid-address, unaligned, object-bounds, or uninitialized-read diagnostics for the source expression itself.
- Use the same address-expression parser and constant-expression evaluator as memory operands, but classify the final operand as address-computation-only.
- Reject malformed or unsupported address expressions before execution with `invalid-effective-address-expression`, pointing at the malformed expression.
- Evaluate valid effective-address arithmetic modulo 2^32 for MASM32 Educational Mode.
- Do not treat modulo wrap as an invalid memory address condition, because `lea` does not access memory.
- This modulo-32-bit policy is mandatory for both static symbol/displacement forms and register-derived runtime forms in this phase.

### Flag behavior

- Preserve all modeled flags.

### Required tests

Parser tests:

- Accept every supported source memory-expression form.
- Reject non-register destination.
- Reject non-32-bit destination.
- Reject `OFFSET` source form.
- Reject scaled-index forms until that future phase.

Executor/source-run tests:

- `lea eax, nums[8]` equals `OFFSET nums + 8`.
- `lea eax, [ebx + 4]` computes `EBX + 4` without reading memory.
- `lea eax, constSymbol` and `lea eax, [constSymbol + 4]` compute addresses without `.CONST` read/write diagnostics.
- `lea eax, [0]` or equivalent unsupported numeric memory expression remains rejected unless the addressing parser already supports it.
- `lea eax, [ebx + 4]` with `EBX=0` does not produce invalid-address because no memory read occurs.
- `lea eax, [ebx + 4]` with `EBX=0FFFFFFFFh` produces `00000003h` by modulo-32-bit arithmetic and emits no memory diagnostic.
- Malformed address expressions still fail before execution.
- Flags remain unchanged: set `CF`, `ZF`, `SF`, and `OF` to known values before `lea`, execute `lea`, and assert all modeled flags are preserved.
- No memory-change rows are produced.

Rendered diagnostic tests:

- Invalid operand form.
- Scaled-index unsupported feature.
- Address-expression overflow, when statically detected.

### Acceptance program

```asm
.data
nums DWORD 10 DUP(0)
.code
main PROC
    mov ebx, OFFSET nums
    lea eax, nums[8]
main ENDP
END main
```

Expected:

```text
EAX = EBX + 8
```

Concretely, if `EBX = 00500000h`, then `EAX = 00500008h`.

### Display follow-up

Phase 52 - LEA may produce addresses that appear in registers or memory-related display paths. It must not add signed register or memory display as part of `LEA`.

Signed register and memory display is owned by Phase 52A - Signed Register and Memory Value Display. If Phase 52 updates examples or tests that assert final register display, those examples may continue using the pre-52A hex/unsigned display format until Phase 52A updates the shared formatter expectations.

Do not mix `LEA` semantics with signed display formatting. `LEA` computes an effective address. Phase 52A changes only how already-existing integer values are presented to users.

---

## 56A. Phase 52A - Signed Register and Memory Value Display

### Goal

Add signed decimal interpretations to existing register and memory value displays.

This is a UI/formatter and display-contract milestone. It must not add MASM syntax, new instructions, new parser behavior, new IR opcodes, new executor behavior, new memory semantics, new flag semantics, new diagnostics, new Program Console behavior, or new Simulator Messages behavior.

The purpose is to make existing bit patterns easier to understand by showing both unsigned and signed interpretations where the display width is known.

### Behavior category

Display and formatter improvement.

### Dependencies

This phase assumes the repository already has existing display or formatter paths for at least some of these value categories:

- final register state after Run;
- register rows used by source-run output or protocol tests;
- memory-change rows;
- debugger current-state rows, if the debugger UI/backend display path already exists;
- last-step delta rows, if the debugger delta path already exists.

Only display paths that already exist in the repository are in scope. Do not implement a missing debugger, missing Step Into path, missing Step Over path, missing raw memory viewer, or missing watch window merely to satisfy this phase.

Do not renumber Phase 53 or any later phase.

### Required display format

For each displayed integer value whose width is known, preserve the existing hexadecimal representation and unsigned decimal interpretation, and add a signed decimal interpretation.

Use this display shape unless the current UI has a table layout where separate columns are clearer:

```text
<hex> / u:<unsigned decimal> / s:<signed decimal>
```

Examples:

```text
00h / u:0 / s:0
7Fh / u:127 / s:127
80h / u:128 / s:-128
FFh / u:255 / s:-1
```

```text
00000000h / u:0 / s:0
7FFFFFFFh / u:2147483647 / s:2147483647
80000000h / u:2147483648 / s:-2147483648
FFFFFFFFh / u:4294967295 / s:-1
```

The exact label text may be `u:` and `s:` or table headings `Unsigned` and `Signed`, but the rendered UI and formatter tests must make it unambiguous which decimal value is unsigned and which decimal value is signed.

Do not remove the existing hexadecimal display.

Do not remove the existing unsigned decimal display unless a later explicit protocol/display migration replaces it and updates every affected test.

### Width rules

The signed value is computed from the displayed width, not from declaration signedness and not from the parent register width.

Register display:

- 8-bit aliases such as `AL`, `AH`, `BL`, `BH`, `CL`, `CH`, `DL`, and `DH` use 8-bit signed interpretation.
- 16-bit aliases such as `AX`, `BX`, `CX`, `DX`, `SI`, `DI`, `BP`, and `SP` use 16-bit signed interpretation.
- 32-bit general-purpose registers such as `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`, and `ESP` use 32-bit signed interpretation.
- `EIP` may use 32-bit signed interpretation only if it is already displayed as a generic integer row.
- `EFLAGS` is not required to show signed decimal. It may continue to show hexadecimal, unsigned decimal, and named modeled flag bits.
- If a future mode displays 64-bit registers, this phase does not authorize lossy signed 64-bit display.

Memory display:

- `BYTE` and `SBYTE` rows use 8-bit signed interpretation.
- `WORD` and `SWORD` rows use 16-bit signed interpretation.
- `DWORD` and `SDWORD` rows use 32-bit signed interpretation.
- For explicit `PTR` memory rows, use the `PTR` width.
- For symbol-derived memory rows, use the resolved access width already used by the memory-change row.
- For byte-expanded views, each byte row uses 8-bit signed interpretation.
- For grouped logical writes, use the grouped access width.
- Signed display uses the displayed or access width only. It must not use the declaration's signedness to decide whether to show a signed value. Both `BYTE` and `SBYTE` rows show signed 8-bit interpretation. Both `DWORD` and `SDWORD` rows show signed 32-bit interpretation.

The following example must be treated as correct:

```text
EAX = 000000FFh / u:255 / s:255
AL  = FFh / u:255 / s:-1
```

Reason: `EAX` is displayed as a 32-bit register, while `AL` is displayed as an 8-bit alias.

### Signedness is display-only

Signed display must not imply or introduce automatic sign extension.

This phase must preserve the existing rule that ordinary `mov` from signed memory does not sign-extend. For example, reading an `SBYTE` with ordinary `mov` still reads the resolved operand width. Explicit sign-extension instructions own sign-extension behavior.

This phase must not change the stored bytes for signed data declarations. `SBYTE -1`, `BYTE 0FFh`, and an instruction that writes `FFh` to a byte all display the same 8-bit signed interpretation when shown as a byte:

```text
FFh / u:255 / s:-1
```

### Simulator Messages and Program Console policy

Normal Simulator Messages diagnostic lines must remain unchanged.

Do not add signed register or memory display text to assembly errors, runtime errors, simulator warnings, or execution-complete messages.

Program Console output must remain unchanged.

This phase changes only existing register and memory value display surfaces.

### Source-run JSON and protocol rules

Prefer implementing signed display in the browser/Node formatter layer from existing numeric or hex fields.

If adding new JSON fields is necessary, the fields must be additive and explicit. Acceptable field names include:

```text
signedDecimal
unsignedDecimal
displaySigned
displayUnsigned
```

Do not reinterpret an existing unsigned JSON field as signed.

Do not remove or rename existing fields in this phase unless the repository already has a dedicated protocol-migration mechanism and all protocol tests are updated.

For 8-bit, 16-bit, and 32-bit values, signed decimal fields may be JSON numbers or deterministic decimal strings.

Phase 52A does not implement signed QWORD/SQWORD decimal display. Signed 64-bit display is deferred to a later lossless 64-bit display/protocol phase. Do not use JavaScript `Number` for signed 64-bit decimal conversion. Do not put JavaScript `BigInt` values directly into worker protocol payloads.

### Required implementation work

1. Add or identify a shared display-format helper for integer values.

   The helper must accept:
   - raw value or already-formatted hex value;
   - width in bits or bytes;
   - enough information to preserve the current hex formatting width.

   The helper must return or render:
   - zero-padded hexadecimal;
   - unsigned decimal;
   - signed decimal.

2. Use the helper for all existing register display paths in scope.

3. Use the helper for all existing memory-change display paths in scope.

4. Keep display formatting deterministic across native/Node formatter tests and browser UI.

5. Keep Program Console and Simulator Messages separate. This phase must not add signed value text to Program Console or Simulator Messages.

6. Update documentation and status text to say signed display is implemented as a display feature, not as an execution feature.

7. Preserve C99 core boundaries. Browser-side formatting may remain JavaScript or TypeScript. If any C helper is added for source-run formatting, it must be C99 with file-level headers and Doxygen-style comments for public APIs.

### Required tests

Add formatter/unit tests for the signed conversion helper.

8-bit required cases:

```text
00h -> u:0,   s:0
01h -> u:1,   s:1
7Fh -> u:127, s:127
80h -> u:128, s:-128
FFh -> u:255, s:-1
```

16-bit required cases:

```text
0000h -> u:0,     s:0
0001h -> u:1,     s:1
7FFFh -> u:32767, s:32767
8000h -> u:32768, s:-32768
FFFFh -> u:65535, s:-1
```

32-bit required cases:

```text
00000000h -> u:0,          s:0
00000001h -> u:1,          s:1
7FFFFFFFh -> u:2147483647, s:2147483647
80000000h -> u:2147483648, s:-2147483648
FFFFFFFFh -> u:4294967295, s:-1
```

Register display tests:

```asm
.code
main PROC
    mov eax, 0FFFFFFFFh
main ENDP
END main
```

Expected display includes:

```text
EAX = FFFFFFFFh / u:4294967295 / s:-1
AX  = FFFFh / u:65535 / s:-1
AL  = FFh / u:255 / s:-1
```

Register alias width test:

```asm
.code
main PROC
    mov eax, 000000FFh
main ENDP
END main
```

Expected display includes:

```text
EAX = 000000FFh / u:255 / s:255
AL  = FFh / u:255 / s:-1
```

Memory-change display tests:

```asm
.data
value DWORD 0
.code
main PROC
    mov value, 0FFFFFFFFh
main ENDP
END main
```

Expected memory-change display includes:

```text
value DWORD
  00000000h / u:0 / s:0 -> FFFFFFFFh / u:4294967295 / s:-1
```

```asm
.data
b BYTE 0
.code
main PROC
    mov b, 0FFh
main ENDP
END main
```

Expected memory-change display includes:

```text
b BYTE
  00h / u:0 / s:0 -> FFh / u:255 / s:-1
```

Signed declaration display test:

```asm
.data
sb SBYTE -1
sd SDWORD -1
.code
main PROC
    mov al, sb
    mov eax, sd
main ENDP
END main
```

Expected final display includes:

```text
AL  = FFh / u:255 / s:-1
EAX = FFFFFFFFh / u:4294967295 / s:-1
```

Regression tests:

- Existing hex output remains present.
- Existing unsigned decimal output remains present.
- Existing source-run success/failure status is unchanged.
- Existing Program Console output is unchanged.
- Existing Simulator Messages output is unchanged.
- Existing VM register values are unchanged.
- Existing VM memory bytes are unchanged.
- Existing flag values are unchanged.
- Existing diagnostics are unchanged.

Protocol tests, only if new fields are added:

- New fields are additive.
- New fields are JSON-compatible.
- Existing protocol consumers still receive the old fields.
- No `BigInt`, `Map`, `Set`, functions, `undefined`, DOM nodes, cyclic objects, or binary transfer-only values are introduced.

Manual browser tests:

- Rebuild Wasm if the implementation touches Wasm-facing output, bundled JS, worker protocol, or files consumed by the browser.
- Run the browser locally.
- Verify final register display for `EAX = FFFFFFFFh`.
- Verify memory-change display for a DWORD write to `FFFFFFFFh`.
- Verify Program Console remains unchanged.
- Verify Simulator Messages still show only the normal execution-complete line for successful programs.

### Acceptance criteria

The aggregate test runner passes.

The signed conversion helper produces exact expected values for 8-bit, 16-bit, and 32-bit boundaries.

A program that leaves `EAX = FFFFFFFFh` displays:

```text
EAX = FFFFFFFFh / u:4294967295 / s:-1
```

The same program displays alias rows using alias widths:

```text
AX = FFFFh / u:65535 / s:-1
AL = FFh / u:255 / s:-1
```

A program that leaves `EAX = 000000FFh` displays:

```text
EAX = 000000FFh / u:255 / s:255
AL = FFh / u:255 / s:-1
```

A DWORD memory write to `FFFFFFFFh` displays:

```text
FFFFFFFFh / u:4294967295 / s:-1
```

A BYTE memory write to `FFh` displays:

```text
FFh / u:255 / s:-1
```

No parser behavior changes.

No IR behavior changes.

No VM execution behavior changes.

No instruction semantics change.

No new MASM syntax is accepted.

No existing supported MASM syntax is rejected.

No Program Console output changes.

No Simulator Messages text changes.

No QWORD/SQWORD signed decimal display is added.

### Rejected / out of scope

- No new instructions.
- No `LEA` changes.
- No `CMP`, jumps, labels, stack, procedures, calls, returns, or Irvine32 routine bodies.
- No sign-extension semantic changes.
- No implicit sign extension for ordinary `mov`.
- No signed arithmetic mode.
- No new CPU flags.
- No PF/AF display work.
- No QWORD/SQWORD executable memory operations.
- No QWORD/SQWORD signed decimal display.
- No BigInt values in worker protocol payloads.
- No source-run JSON field removal or renaming.
- No browser settings UI.
- No debugger feature implementation beyond updating already-existing display rows.

---

## 57. Phase 53 - Unsigned MUL

### Goal

Implement one-operand unsigned `mul` exactly enough for MASM32 educational integer examples.

This phase must not implement `imul`, `div`, `idiv`, two-/three-operand multiplication, labels, jumps, stack behavior, or procedure behavior.

### Behavior category

Runtime arithmetic instruction with implicit accumulator operands.

### Accepted syntax

```asm
mul reg8
mul reg16
mul reg32
mul BYTE PTR [reg32]
mul WORD PTR [reg32]
mul DWORD PTR [reg32]
mul symbol
mul symbol[offset]
```

### Rejected syntax

```asm
mul 5
mul eax, ebx
mul [eax]          ; ambiguous width
mul QWORD PTR q    ; executable QWORD/SQWORD memory operation remains deferred
```

### Runtime semantics

```text
MUL r/m8:   AX      = AL  * r/m8
MUL r/m16:  DX:AX   = AX  * r/m16
MUL r/m32:  EDX:EAX = EAX * r/m32
```

The multiplication is unsigned.

### Flag behavior

- Set `CF` and `OF` if the upper half of the product is nonzero.
- Clear `CF` and `OF` if the upper half of the product is zero.
- Preserve `ZF` and `SF` as a deterministic educational policy for real x86 undefined flags.

### Required tests

- `AL=10`, `mul bl=20` produces `AX=200`, `CF=0`, `OF=0`.
- `AL=0FFh`, `mul bl=2` produces `AX=01FEh`, `CF=1`, `OF=1`.
- `AX=1234h`, `mul bx=10h` produces `DX:AX` expected value.
- `EAX=FFFFFFFFh`, `mul ebx=2` produces `EDX:EAX = 00000001FFFFFFFEh`, `CF=1`, `OF=1`.
- Memory source read through checked memory helpers.
- Direct symbol memory source.
- Symbol-offset memory source.
- Explicit `PTR [reg32]` memory source.
- Readable `.CONST` memory source.
- Invalid-address runtime diagnostic for register-indirect source.
- Ambiguous memory-width diagnostic.
- QWORD source rejected in MASM32 Educational Mode.
- No memory-change rows are produced by memory-source reads.

### Acceptance program

```asm
.code
main PROC
    mov eax, 10
    mov ebx, 20
    mul ebx
main ENDP
END main
```

Expected:

```text
EDX = 00000000h / 0
EAX = 000000C8h / 200
CF = 0
OF = 0
```

---

## 57A. Phase 53A - Memory Validation Policy Clarification and Symbol-Offset Runtime Correction

### Goal

Formalize the simulator's memory-validation levels and correct symbol-offset memory operands so valid MASM-style memory syntax is not rejected merely because it crosses declared-object or section boundaries.

This is a corrective memory-diagnostics and policy phase. It must preserve default region-only behavior and must not add new MASM syntax, new instructions, new arithmetic semantics, browser UI settings, or Phase 54 - One-Operand Signed IMUL.

### Background

Earlier milestones implemented:

- checked VM memory regions and permissions;
- `.DATA?` deterministic zero-fill plus uninitialized-origin metadata;
- `.CONST` read-only write protection;
- declared object maps;
- allocated-object warning mode;
- allocated-object strict mode;
- uninitialized-read warning and strict modes.

Those features must remain valid.

This phase clarifies that:

```text
- region-only memory validation is the default;
- allocated-object warning/strict modes are the Level 4 declared-object validation layer;
- section-capacity and section-image validation are separate policy layers and are not automatically implemented by object validation;
- parser/static symbol-offset validation must not enforce runtime object or section policy;
- true VM-region and permission failures remain fatal runtime errors;
- `.CONST` write overlap remains a mandatory permission failure.
```

### Definitions

Use these terms consistently in code comments, diagnostics, tests, and documentation.

```text
VM memory region:
  A concrete allocated memory region in the VM, with base address, size, and permissions.

Section capacity:
  The allocated range reserved for a MASM data section or section-like area, such as `.data`, `.DATA?`, or `.CONST`.
  It may include fixed-layout slack, minimum-size capacity, alignment padding, or deterministic automatic-layout padding.

Section image:
  The declared byte image produced by declarations in a section after initializer expansion and `DUP` expansion.
  It excludes extra region capacity not produced by declarations.

Declared object:
  One object produced by one data declaration after expansion.
  Adjacent declarations remain separate objects even when contiguous.
```

### Validation levels

The implementation must document or encode these levels, even though this phase fixes Level 1 behavior and maps existing Level 4 behavior rather than implementing Level 2 or Level 3.

```text
Level 1 - Region-only validation:
  Always enforced.
  Default user-visible behavior.
  Reads require the full final byte range to be inside one readable VM region.
  Writes require the full final byte range to be inside one writable VM region and not overlap `.CONST`.
  Address overflow, outside-region access, and permission failure are runtime errors.

Level 2 - Section-capacity validation:
  Warning/strict policy implemented by Phase 53B - Section-Capacity and Section-Image Validation Modes.
  The full final byte range must stay inside the allocated capacity of the owning section.

Level 3 - Section-image validation:
  Warning/strict policy implemented by Phase 53B - Section-Capacity and Section-Image Validation Modes.
  The full final byte range must stay inside the declared section image.

Level 4 - Declared-object validation:
  Existing allocated-object warning/strict behavior.
  The full final byte range must be wholly inside one declared object unless the mode is off.
```

For Level 2 and Level 3, the owning section is determined by the starting address of the access. If the starting address is not inside any known section capacity or section image, the access violates the relevant enabled section validation level after Level 1 region validation passes.

Declared-object validation checks only whether the final byte range is wholly contained in one declared object. It does not check whether the expression's base symbol, if any, intended that object. A future provenance/intent validation mode may warn when an expression based on one symbol lands wholly inside a different symbol, but that is not Level 4.

Recommended diagnostic codes are:

```text
section-capacity-violation
section-image-violation
object-bounds-violation
```

Do not use `symbol-offset-out-of-range` for runtime section or object validation. A `symbol-offset-out-of-range` diagnostic may be retained only for expression/address representability failures, not for crossing symbol/object bounds.

### Parser/runtime boundary

The parser must not use declared-object, section-image, or section-capacity bounds to reject otherwise valid memory operands.

The parser may still reject:

- malformed memory syntax;
- unknown symbols;
- ambiguous memory width;
- unsupported addressing modes;
- unsupported executable memory widths;
- constant-expression errors;
- address-expression values that cannot be represented;
- obvious static `.CONST` direct writes where current static diagnostics already apply.

The runtime memory path owns final byte-range checks.

### Required behavior correction

Correct symbol-offset handling so this program is not rejected at assembly time with `symbol-offset-out-of-range` merely because the inferred DWORD access extends beyond `x`:

```asm
.DATA?
x DWORD ?

.code
main PROC
    mov eax, 10
    mul [x+1]
main ENDP
END main
```

The expected default behavior is:

- The program parses.
- `mul [x+1]` infers a DWORD memory source from `x DWORD`.
- The final read range is computed at runtime.
- If the final range is inside a readable VM memory region, execution may proceed.
- If the final range is outside a readable VM memory region, execution stops with the ordinary runtime memory diagnostic.
- If uninitialized-read warning mode is enabled and the accessed bytes are uninitialized-origin, emit `uninitialized-read` according to that mode.
- If allocated-object warning mode is enabled, emit the existing object-bound warning because the access starts inside `x` and extends beyond `x`.
- If allocated-object strict mode is enabled, stop with `object-bounds-violation` before result-register or flag mutation.
- Default mode must not emit object-bound diagnostics.

### True cross-region behavior

A single memory access must be wholly contained in one suitable VM memory region.

If an access starts in one VM region and ends in another VM region, or starts in a region and ends outside it, it must stop with the ordinary runtime memory diagnostic. This is not warning-only behavior.

Do not implement stitching across separate VM regions.

### Cross-section behavior

If `.data`, `.DATA?`, and `.CONST` are separate VM regions in the current implementation, a single access that crosses from one to another fails Level 1 region-only validation and does not reach section-capacity or section-image validation.

If `.data`, `.DATA?`, and `.CONST` are subranges of one shared VM region, a single access that crosses from one section subrange to another may pass Level 1 and then be diagnosed by section-image or section-capacity validation when those modes exist.

This phase must document the current representation and add tests that match it.

### `.CONST` precedence

`.CONST` write protection remains mandatory.

Any write whose final byte range overlaps `.CONST` storage must fail as a permission/read-only runtime diagnostic before section-capacity, section-image, declared-object, uninitialized-read, or unaligned-access diagnostics.

Reads from `.CONST` remain allowed if the range is otherwise valid. Reads crossing `.CONST` are not permission failures merely because `.CONST` is read-only. They fail only if they cross independent VM regions or violate an enabled section/object strict policy.

### Diagnostic precedence

For one memory access, use this precedence:

```text
1. Address arithmetic overflow.
2. Final range not wholly contained in one suitable VM memory region.
3. Permission failure, including `.CONST` write overlap.
4. Section-capacity violation, if implemented and enabled.
5. Section-image violation, if implemented and enabled.
6. Declared-object violation, if enabled.
7. Uninitialized-read warning or strict error, if enabled.
8. Unaligned-access warning.
```

Strict diagnostics stop before mutation. If an earlier fatal or strict diagnostic stops execution, later warning diagnostics for the same access do not need to be emitted.

Emit at most one diagnostic per enabled validation level for one memory access, in precedence order. Do not combine separate validation-level diagnostics unless the relevant phase explicitly defines a combined diagnostic shape and tests it.

### Implementation tasks

1. Audit parser/static symbol-offset validation.
2. Remove or narrow any parser diagnostic that rejects valid memory operands solely because the inferred access crosses object, section-image, section-capacity, or fixed-layout slack bounds.
3. Keep parser diagnostics for malformed syntax, unsupported forms, unknown symbols, ambiguous width, unsupported executable width, and representability failures.
4. Route final byte-range validation through checked VM memory helpers.
5. Preserve existing allocated-object warning and strict modes as Level 4 declared-object validation.
6. Preserve existing uninitialized-read warning and strict modes as an orthogonal read-origin validation policy.
7. Preserve `.CONST` direct static diagnostics where already implemented, but do not rely on them for computed writes.
8. Add or update documentation so future phases and assistants use the Level 1-4 terminology.
9. Add structured diagnostic and rendered Simulator Messages tests for any changed user-visible diagnostic wording.
10. Add regression tests proving default mode does not emit object/section diagnostics.

### Required tests

Default parser/runtime correction:

```asm
.DATA?
x DWORD ?

.code
main PROC
    mov eax, 10
    mul [x+1]
main ENDP
END main
```

Required assertions:

- no assembly-time `symbol-offset-out-of-range`;
- no assembly error solely because the access crosses `x`;
- final behavior is runtime-controlled by region validity and enabled validation modes.

Default object-bound preservation:

```asm
.data
x DWORD 0
y DWORD 0

.code
main PROC
    mov eax, DWORD PTR [x+1]
main ENDP
END main
```

Required assertions:

- default mode does not emit object-bound diagnostics;
- unaligned warning behavior remains whatever the existing unaligned policy requires;
- if the final range is inside a valid readable region, the program is not stopped by object validation in default mode.

Allocated-object warning mode:

```asm
.data
x DWORD 0
y DWORD 0

.code
main PROC
    mov eax, DWORD PTR [x+1]
main ENDP
END main
```

Required assertions:

- warning mode emits the existing object-bound warning for partial/cross-object access;
- execution continues unless another fatal diagnostic occurs;
- rendered Simulator Messages text is tested.

Allocated-object strict mode:

```asm
.data
x DWORD 0
y DWORD 0

.code
main PROC
    mov eax, DWORD PTR [x+1]
main ENDP
END main
```

Required assertions:

- strict mode emits `object-bounds-violation`;
- execution stops before register or flag mutation caused by the memory-consuming instruction;
- rendered Simulator Messages text is tested.

Runtime invalid region still stops:

```asm
.code
main PROC
    mov eax, 0
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Required assertions:

- runtime invalid-address/range diagnostic remains fatal;
- no object-bound warning masks the lower-level runtime memory error.

`.CONST` write precedence:

```asm
.CONST
c DWORD 123

.code
main PROC
    mov eax, OFFSET c
    mov DWORD PTR [eax], 456
main ENDP
END main
```

Required assertions:

- permission/read-only diagnostic is emitted;
- object-bound or section-bound diagnostics do not replace the permission diagnostic;
- no memory-change row is committed.

Uninitialized-read mode interaction:

```asm
.DATA?
x DWORD ?
y DWORD ?

.code
main PROC
    mov eax, DWORD PTR [x+1]
main ENDP
END main
```

Required assertions:

- default mode emits no uninitialized-read diagnostic;
- uninitialized-read warning mode emits `uninitialized-read` when the final read range includes uninitialized-origin bytes and no earlier fatal diagnostic stops execution;
- uninitialized-read strict mode stops before register/flag mutation.

### Non-goals

- No Phase 54 - One-Operand Signed IMUL.
- No new instructions.
- No new MASM syntax.
- No scaled-index addressing.
- No section-capacity validation implementation.
- No section-image validation implementation.
- No browser UI setting controls.
- No provenance validation.
- No new `.CONST` behavior beyond preserving existing permission semantics.
- No executable QWORD/SQWORD memory operations.
- No Windows API, PE, linker, macro, host filesystem, or full MASM behavior.

### Acceptance criteria

- Existing aggregate tests pass.
- Valid MASM-style symbol-offset memory operands are not rejected merely for crossing declared-object or section-image bounds.
- Default behavior remains region-only.
- Allocated-object warning and strict modes still work as Level 4 declared-object validation.
- `.CONST` writes still fail through permission/read-only diagnostics before educational boundary diagnostics.
- True cross-region accesses remain fatal runtime memory errors.
- All changed user-visible diagnostics have structured tests and rendered Simulator Messages tests.
- Documentation names the four validation levels and states which levels are implemented after this phase.

---


### Corrective note after Phase 57 - `.CONST` cross-region diagnostic wording

A later audit found one diagnostic-quality ambiguity in the Phase 53A memory-validation wording.

The Phase 53A behavior remains correct in these ways:

```text
- A single memory access must be wholly contained in one suitable VM memory region.
- The simulator must not stitch one source-level memory access across independent VM regions.
- `.CONST` write protection remains mandatory.
- A failed memory write must not partially mutate registers, flags, memory, Program Console output, or memory-change rows.
```

The ambiguity is a write whose final byte range both:

```text
1. crosses an independent VM-region boundary; and
2. overlaps read-only `.CONST` storage.
```

Example:

```asm
.const
x BYTE 1

.code
main PROC
    mov eax, OFFSET x
    sub eax, 2
    mov DWORD PTR [eax], 0FFFFFFFFh
main ENDP
END main
```

If `x` is at the start of the `.CONST` VM region, the final write range is:

```text
OFFSET x - 2 through OFFSET x + 1
```

That range starts before `.CONST` and ends inside `.CONST`. If `.data` and `.CONST` are separate VM regions in the current repository, the write is a fatal Level 1 region-containment failure because it is not wholly contained in one VM region. It is also useful to the user to know that the attempted write overlaps read-only `.CONST` storage.

Corrected diagnostic expectation:

```text
- The write remains fatal.
- The write remains non-mutating.
- The simulator still does not stitch the access across VM regions.
- The diagnostic should keep the existing Level 1 region/range diagnostic code unless implementation review shows that the current runtime already uses a more specific Level 1 permission diagnostic for this path.
- The rendered message must mention `.CONST` overlap when layout metadata can prove that the failed write range overlaps `.CONST`.
- The diagnostic must not be reclassified as section-capacity, section-image, declared-object, uninitialized-read, or unaligned-access behavior.
```

This is a diagnostic wording and diagnostic-context correction. It is not a memory-layout change, not a permission relaxation, not a new validation mode, and not a reason to implement section-capacity or section-image behavior outside its owning phase.

## 57B. Phase 53B - Section-Capacity and Section-Image Validation Modes

### Goal

Add optional warning and strict validation for section-capacity and section-image memory access boundaries.

This phase implements Level 2 and Level 3 of the memory-validation model. It must preserve Level 1 region-only default behavior and existing Level 4 declared-object behavior.

### Behavior category

Runtime memory diagnostics and educational validation modes.

### Dependencies

- Phase 53A - Memory Validation Policy Clarification and Symbol-Offset Runtime Correction.
- Existing checked VM memory helpers.
- Existing layout metadata for `.data`, `.DATA?`, and `.CONST`.
- Existing diagnostic rendering harness.

### Modes to implement

Add these selectable policies through the same local test/source-run configuration style used by existing diagnostic modes:

```text
section_capacity_validation = off | warn | strict
section_image_validation    = off | warn | strict
```

Do not add browser UI controls in this phase.

### Level 2: section-capacity validation

Section-capacity validation checks whether the final byte range remains inside the allocated capacity of the owning section or section-like storage area.

For section-capacity validation, the owning section is determined by the starting address of the access. If the starting address is not inside any known section capacity, the access violates this validation level after Level 1 region validation passes.

Warning mode:

```text
If the access is otherwise valid but violates section capacity, emit a simulator warning and continue.
```

Strict mode:

```text
If the access violates section capacity, emit a runtime error and stop before mutation.
```

If the access already fails Level 1 region-only validation, emit the Level 1 runtime memory diagnostic instead of a section-capacity diagnostic.

### Level 3: section-image validation

Section-image validation checks whether the final byte range remains inside the declared section image.

For section-image validation, the owning section is determined by the starting address of the access. If the starting address is not inside any known section image, the access violates this validation level after Level 1 region validation passes.

Warning mode:

```text
If the access is otherwise valid but leaves the declared section image, emit a simulator warning and continue.
```

Strict mode:

```text
If the access leaves the declared section image, emit a runtime error and stop before mutation.
```

If the access already fails Level 1 region-only validation or Level 2 section-capacity strict validation, emit the earlier diagnostic instead.

### Section boundary behavior

If `.data`, `.DATA?`, and `.CONST` are separate VM regions in the current implementation, a single access that crosses from one to another fails Level 1 region-only validation and does not reach section-capacity or section-image validation.

If `.data`, `.DATA?`, and `.CONST` are subranges of one shared VM region, a single access that crosses from one section subrange to another may pass Level 1 and then be diagnosed by section-image or section-capacity validation.

This phase must document which representation the current repository uses and test the behavior accordingly.

### `.CONST` precedence

Writes overlapping `.CONST` storage remain mandatory permission failures.

A `.CONST` write overlap must not be downgraded to a warning and must not be hidden behind section-capacity, section-image, declared-object, uninitialized-read, or unaligned warnings.

Reads crossing `.CONST` are not permission failures merely because `.CONST` is read-only. They fail only if they cross independent VM regions or violate an enabled section/object strict policy.

### Diagnostic codes

Use stable diagnostic codes.

Required codes:

```text
section-capacity-violation
section-image-violation
```

For warning mode, render as:

```text
[simulator-warning] section-capacity-violation ...
[simulator-warning] section-image-violation ...
```

For strict mode, render as:

```text
[runtime-error] section-capacity-violation ...
[runtime-error] section-image-violation ...
```

Messages must include:

- access kind: read or write;
- computed address;
- access width;
- computed final byte range;
- owning section, when known;
- section capacity range or section image range, when known;
- source line, column, byte offset, and span length for the memory operand when available.

Emit at most one diagnostic per enabled validation level for one memory access, in precedence order. Do not combine separate validation-level diagnostics in this phase.

### Required tests

Default mode:

- Accesses outside section image but inside valid region remain allowed in default mode.
- Accesses outside section capacity but inside valid region remain allowed in default mode only if Level 1 permits them.
- No section-capacity or section-image diagnostics appear by default.

Section-capacity warning mode:

- Warn and continue when an access leaves the owning section capacity but remains inside a valid VM region.
- Do not warn when access remains wholly inside section capacity.
- Do not replace Level 1 invalid-region diagnostics.

Section-capacity strict mode:

- Stop before mutation when an access leaves section capacity.
- Preserve registers, flags, memory, Program Console output, and memory-change rows on strict failure.

Section-image warning mode:

- Warn and continue when an access leaves declared section image but remains otherwise valid.
- Do not warn when access remains wholly inside declared section image.
- If section capacity is larger than image because of fixed-layout slack, accessing that slack warns in section-image warning mode.

Section-image strict mode:

- Stop before mutation when an access leaves declared section image.
- Preserve no-partial-mutation guarantees.

Interaction with Level 4 declared-object mode:

- If both section-image warning and declared-object warning are enabled, diagnostics appear in documented precedence order.
- If section-image strict stops execution, declared-object warning for the same access need not also be emitted.
- Existing object warning/strict tests still pass.

`.CONST` precedence:

- A write overlapping `.CONST` emits the permission/read-only diagnostic before section diagnostics.

Rendered messages:

- Add exact rendered Simulator Messages tests for warning and strict forms of both new diagnostic codes.

### Non-goals

- No browser UI controls.
- No Phase 54 - One-Operand Signed IMUL.
- No new instructions.
- No new MASM syntax.
- No provenance validation.
- No uninitialized-read behavior changes.
- No default-behavior change.
- No executable QWORD/SQWORD memory operations.
- No full MASM, PE, linker, Windows API, macro, or host filesystem behavior.

### Acceptance criteria

- Section-capacity validation can be selected in tests as off, warn, or strict.
- Section-image validation can be selected in tests as off, warn, or strict.
- Default behavior remains region-only.
- Existing declared-object validation remains Level 4 and still passes.
- Lower-level region and permission diagnostics keep precedence.
- `.CONST` write overlap remains fatal permission/read-only behavior.
- All new diagnostics have structured source-run tests and rendered Simulator Messages tests.

---


## 57C. Phase 53C - Default Teaching Diagnostics for Existing Warning Modes

### Goal

Change selected existing diagnostics from opt-in to default-on teaching warnings.

This phase changes defaults only. It must not add new MASM syntax, new instructions, new executor semantics, new memory-validation levels, new strict modes, new UI presets, or broad static-analysis diagnostics.

### Behavior category

Diagnostic default policy correction.

### Dependencies

- Phase 40 - Uninitialized-Read Warning and Strict Modes.
- Phase 50B - Undefined Flag Use Diagnostics for Flag Consumers.
- Existing structured diagnostic and rendered Simulator Messages harnesses.

### Default policy changes

Change these defaults for user-facing source-run and browser execution:

```text
uninitialized_read_policy:
  before this phase: off
  after this phase:  warn

undefined_flag_use_policy:
  before this phase: off
  after this phase:  warn
```

Low-level unit tests may still construct explicit policies directly. Any user-facing run path that omits a policy must use the teaching defaults after this phase.

The opt-out behavior must require an explicit policy value of `off`; missing or omitted policy fields must not silently mean `off` after this phase.

Preserve these explicit policies:

```text
uninitialized_read_policy = off
uninitialized_read_policy = warn
uninitialized_read_policy = strict

undefined_flag_use_policy = off
undefined_flag_use_policy = warn
undefined_flag_use_policy = error
```

The `off` settings are the opt-out / "I know what I'm doing" behavior.

The strict/error settings remain opt-in.

Tests that need old silent behavior must be updated to pass explicit `uninitialized_read_policy = off` or `undefined_flag_use_policy = off`.

### Required behavior

#### Uninitialized reads

By default, a read from bytes that originated as `?`, `DUP(?)`, or `.DATA?` and have not yet been successfully written by the simulated program must emit:

```text
[simulator-warning] uninitialized-read
```

The program continues.

The read value remains deterministic zero-filled unless previous successful writes changed the bytes.

The warning must not imply that the simulator read random host memory or native uninitialized garbage.

The warning must explain that the bytes came from uninitialized-origin storage and have not yet been written by the simulated program.

Explicit `uninitialized_read_policy = off` must suppress this warning and preserve the original Phase 40 silent deterministic-zero behavior.

Explicit `uninitialized_read_policy = strict` must continue to stop before consuming the uninitialized-origin bytes.

#### Undefined flag use

By default, if a flag-consuming instruction reads a modeled flag whose validity metadata says the current deterministic value is architecturally undefined, the consumer must emit:

```text
[simulator-warning] undefined-flag-use
```

The consumer continues using the deterministic preserved flag value.

The warning must point at the consumer instruction and mention producer metadata when available.

Explicit `undefined_flag_use_policy = off` must suppress this warning and preserve the original Phase 50B silent-consumer behavior.

Explicit `undefined_flag_use_policy = error` must continue to stop before the consumer uses the undefined modeled flag.

### What this phase does not change

This phase must not change:

- deterministic zero-fill behavior for `?`, `DUP(?)`, or `.DATA?`;
- initialized-byte tracking;
- register-value taint policy;
- uninitialized-read strict behavior;
- flag-validity metadata;
- producer warnings such as `undefined-shift-flag` and `undefined-modeled-flag`;
- default producer-warning behavior for `undefined-shift-flag` and `undefined-modeled-flag`;
- undefined-flag-use error behavior;
- checked memory region and permission errors;
- `.CONST` write protection;
- object-bound warning or strict modes;
- section-capacity warning or strict modes;
- section-image warning or strict modes;
- browser settings UI;
- warning presets;
- Program Console output.

### Required tests

Update tests that previously asserted silent default behavior. Tests that intentionally need old silent behavior must pass explicit `off` policies.

Uninitialized-read default warning:

```asm
.DATA?
x DWORD ?

.code
main PROC
    mov eax, x
main ENDP
END main
```

Expected default behavior after this phase:

- execution succeeds;
- `EAX` receives deterministic zero;
- one `uninitialized-read` simulator warning is emitted;
- `execution-complete` is still emitted;
- Program Console remains unchanged.

Uninitialized-read opt-out:

Run the same program with explicit `uninitialized_read_policy = off`.

Expected:

- execution succeeds;
- `EAX` receives deterministic zero;
- no `uninitialized-read` warning is emitted;
- `execution-complete` is emitted.

Uninitialized-read strict:

Run the same program with explicit `uninitialized_read_policy = strict`.

Expected:

- execution stops before consuming the uninitialized-origin bytes;
- `uninitialized-read` is emitted as a runtime error;
- no `execution-complete` message is emitted.

Uninitialized read with Phase 53-style memory source:

```asm
.DATA?
x DWORD ?

.code
main PROC
    mov eax, 10
    mul [x+1]
main ENDP
END main
```

Expected default behavior when the final range is otherwise a valid readable VM range:

- no assembly-time object/section bounds diagnostic merely because of `[x+1]`;
- `uninitialized-read` warning is emitted for the memory source;
- execution continues;
- result registers reflect the deterministic bytes read;
- `execution-complete` is emitted.

If the final byte range is outside a valid readable VM region, the lower-level runtime memory error takes precedence and the uninitialized-read warning is not required.

Undefined flag use default warning:

```asm
.code
main PROC
    stc
    mov al, 1
    shl al, 8
    mov ebx, 0
    adc ebx, 0
main ENDP
END main
```

Expected default behavior after this phase:

- the existing producer warning remains present where applicable;
- `adc ebx, 0` emits `undefined-flag-use` because it consumes `CF`;
- execution continues using the deterministic preserved `CF`;
- `execution-complete` is emitted.

Undefined flag use opt-out:

Run the same program with explicit `undefined_flag_use_policy = off`.

Expected:

- no `undefined-flag-use` diagnostic;
- existing producer warning behavior remains unchanged;
- execution completes.

Undefined flag use error:

Run the same program with explicit `undefined_flag_use_policy = error`.

Expected:

- `undefined-flag-use` runtime error at the consumer;
- execution stops before consumer mutation;
- no `execution-complete` message.

Rendered message tests:

- Add exact rendered Simulator Messages tests for default `uninitialized-read` warning.
- Add exact rendered Simulator Messages tests for default `undefined-flag-use` warning.
- Preserve existing rendered tests for strict/error variants.

### Non-goals

- No new MASM syntax.
- No new instructions.
- No Phase 54 - One-Operand Signed IMUL.
- No new diagnostic code beyond using existing `uninitialized-read` and `undefined-flag-use`.
- No object-bound default-warning change.
- No section-capacity or section-image default-warning change.
- No provenance or taint tracking.
- No broad warning preset system.
- No browser diagnostic-settings UI.
- No Program Console changes.
- No changes to hard runtime memory errors.

### Acceptance criteria

- Default user-facing source-run and browser behavior emits `uninitialized-read` warnings for uninitialized-origin reads.
- Default user-facing source-run and browser behavior emits `undefined-flag-use` warnings when an existing flag consumer reads invalid modeled flag metadata.
- Omitted policy fields use teaching defaults, not the old silent behavior.
- Explicit opt-out policies preserve old silent behavior.
- Strict/error policies preserve existing stop-before-mutation behavior.
- Existing producer warnings remain unchanged.
- Existing object/section validation defaults remain unchanged.
- All changed user-visible diagnostics have structured tests and rendered Simulator Messages tests.

## 57D. Phase 53D - Compatibility No-Op and Limited-Behavior Notices

### Goal

Emit default informational notices for accepted MASM compatibility constructs that have meaningful real MASM behavior but are no-op, metadata-only, virtual-only, or limited in the simulator.

This phase improves user understanding. It must not reject programs, implement deferred MASM behavior, or change VM execution semantics.

### Behavior category

Parser/directive diagnostic notice improvement.

### Dependencies

- Existing directive classification from the spec.
- Existing structured diagnostic and rendered Simulator Messages infrastructure.
- Existing accepted no-op, metadata-only, and virtual compatibility directives.

### Diagnostic category

Use a non-fatal notice category, not an assembly error.

Recommended rendered category:

```text
[simulator-notice]
```

Recommended diagnostic codes:

```text
compatibility-no-op
compatibility-metadata-only
compatibility-limited
```

Use the most specific code available.

This phase should add a distinct notice severity if the diagnostic model does not already have one. Do not use `assembly-error` or `runtime-error` for compatibility notices. Use `simulator-warning` only as a last resort, and only if adding notice severity is disproportionate for the current architecture. If `simulator-warning` is used temporarily, the rendered wording must clearly say that the diagnostic is an informational notice, and the milestone report must record the limitation.

### Default behavior

Compatibility notices are default-on after this phase.

They must not block execution.

They must not write to Program Console.

They must be rendered in Simulator Messages.

They must preserve line, column, byte offset, and span length for the relevant directive or construct.

If a backend/test policy for compatibility notices already exists or is simple to add, support:

```text
compatibility_notices = off
compatibility_notices = on
```

If adding that policy would require broad diagnostic architecture work, Phase 53E - Memory Validation and Teaching Diagnostic UI Settings must add the opt-out setting. In either case, default behavior after Phase 53D is notices on.

### Constructs that should produce notices

Emit notices for accepted compatibility constructs whose real MASM meaning is not fully performed by the simulator.

Required default notices:

```text
.386
.486
.586
.686
.model flat, stdcall
.stack
.stack size
INCLUDE Macros.inc
TITLE
SUBTITLE
PAGE
```

Recommended wording examples:

```text
.686 is accepted for MASM compatibility but does not change the simulator CPU mode.
.model flat, stdcall is accepted for MASM32 textbook compatibility; the simulator does not perform real object-file, linker, or Windows calling-convention behavior.
.stack 4096 records stack-size metadata where implemented; it does not by itself execute stack instructions or create procedure frames.
INCLUDE Macros.inc is accepted as a virtual compatibility include; general MASM macro expansion is not enabled.
TITLE is accepted as a listing/documentation directive and does not affect VM execution.
```

### Constructs that should not receive generic no-op notices

Do not emit generic compatibility no-op notices for constructs with active simulator semantics.

Examples:

```text
INCLUDE Irvine32.inc
OPTION CASEMAP:ALL
OPTION CASEMAP:NONE
.data
.DATA?
.CONST
.code
PROC
ENDP
END
```

Specific diagnostics for invalid or unsupported uses of these constructs still apply.

`INCLUDE Irvine32.inc` may receive a targeted limitation notice only if a later phase decides it is useful to explain that it is a virtual include, but it must not be described as a no-op because it enables virtual Irvine32 names and supported Irvine32 behavior.

### Grouping and noise control

This phase may emit one notice per relevant source construct.

If the implementation already has or can cheaply add grouping, repeated no-op directive notices may be grouped into a single notice with source references. Grouping is optional and should not be implemented if it requires broad diagnostic architecture changes.

Do not suppress source locations merely to group notices.

Do not emit duplicate notices for the same source directive.

### Required tests

Parser/source-run tests:

- `.386` emits `compatibility-no-op` or equivalent notice and execution still succeeds.
- `.686` emits a CPU-mode no-op notice and execution still succeeds.
- `.model flat, stdcall` emits a limited-behavior notice and execution still succeeds.
- `.stack 4096` emits metadata-only or limited-behavior notice and execution still succeeds.
- `INCLUDE Macros.inc` emits macro-expansion-limited notice and execution still succeeds.
- `TITLE`, `SUBTITLE`, and `PAGE` emit listing/documentation no-op notices and execution still succeeds.

No-notice regression tests:

- `INCLUDE Irvine32.inc` does not receive a generic no-op notice.
- `OPTION CASEMAP:NONE` does not receive a generic no-op notice.
- `.DATA?` does not receive a generic no-op notice.
- `.CONST` does not receive a generic no-op notice.

Rendered Simulator Messages tests:

- exact rendered notice for `.686`;
- exact rendered notice for `.model flat, stdcall`;
- exact rendered notice for `INCLUDE Macros.inc`;
- successful execution appears after notices when the program has no blocking errors.

Error-precedence tests:

- unsupported directives still produce assembly errors or unsupported-feature diagnostics as before;
- compatibility notices must not downgrade real assembly errors;
- assembly errors still prevent execution.

### Non-goals

- No macro expansion.
- No real host include loading.
- No listing file generation.
- No real object-file or linker behavior.
- No Windows calling-convention behavior.
- No new instruction behavior.
- No new memory behavior.
- No browser settings UI.
- No warning preset system.
- No broad static analyzer.
- No changes to Program Console.

### Acceptance criteria

- Meaningful accepted no-op or limited compatibility constructs emit default notices.
- Notices are non-fatal.
- Notices appear in Simulator Messages, not Program Console.
- Notices include source location metadata.
- Existing accepted syntax remains accepted.
- Existing unsupported constructs remain unsupported.
- Constructs with real simulator semantics are not mislabeled as no-ops.

## 57E. Phase 53E - Memory Validation and Teaching Diagnostic UI Settings

### Goal

Expose implemented memory-validation and teaching-diagnostic policies in the browser UI without changing backend semantics.

This phase adds UI/settings access to policies that already exist. It must not invent new warning modes, new strict modes, new memory-validation semantics, or broad warning presets.

### Dependencies

- Phase 53A - Memory Validation Policy Clarification and Symbol-Offset Runtime Correction.
- Phase 53B - Section-Capacity and Section-Image Validation Modes, if those modes are implemented before this UI phase.
- Phase 53C - Default Teaching Diagnostics for Existing Warning Modes.
- Phase 53D - Compatibility No-Op and Limited-Behavior Notices.
- Existing allocated-object warning/strict modes.
- Existing uninitialized-read warning/strict modes.
- Existing undefined-flag-use warning/error modes.
- Existing settings/protocol infrastructure.

### Required UI settings

Expose memory validation and selected teaching diagnostics in clear educational terms.

Recommended UI labels:

```text
Memory range validation:
- Region-only (default)
- Section capacity: warn
- Section capacity: strict stop
- Section image: warn
- Section image: strict stop
- Declared object bounds: warn
- Declared object bounds: strict stop

Uninitialized reads:
- Warn (default)
- Off / I know what I'm doing
- Strict stop

Undefined flag use:
- Warn (default)
- Off / I know what I'm doing
- Strict stop

Compatibility notices:
- On (default)
- Off
```

The UI must explain:

```text
Region-only is closest to native MASM-style memory behavior.
Uninitialized storage is deterministic zero-filled in this simulator, but warning by default helps find mistakes.
Undefined flag-use warnings report non-portable flag-dependent behavior while continuing with deterministic simulator values.
Compatibility notices explain accepted MASM directives that the simulator ignores or models only partially.
Strict stop modes are educational safety checks.
`.CONST` writes are always blocked.
Invalid VM-region accesses are always blocked.
```

### Protocol rules

- Settings must be represented in structured, JSON-compatible worker messages.
- Unsupported combinations must produce `ui-warning` or `ui-error` messages rendered through the normal Simulator Messages formatter.
- Defaults must match Phase 53C and Phase 53D:
  - uninitialized-read: warn;
  - undefined-flag-use: warn;
  - compatibility notices: on;
  - region-only memory validation remains default;
  - object/section validation remains off unless selected.
- Share URL behavior must be explicit:
  - either include diagnostic settings intentionally as share-safe project state;
  - or classify them as local preferences only.
- The implementation must document which choice was made.

### Required tests

- Unit tests for settings serialization.
- Worker protocol tests for each setting.
- Rendered `ui-warning` or `ui-error` tests for invalid setting combinations.
- Browser/manual tests showing default page-load behavior matches Phase 53C and Phase 53D defaults.
- Manual browser program for uninitialized-read default warning and opt-out.
- Manual browser program for undefined-flag-use default warning and opt-out.
- Manual browser program for compatibility notices default-on and opt-out.
- Regression tests proving Program Console remains unchanged.

### Non-goals

- No new runtime validation semantics beyond already-implemented backend policies.
- No new MASM syntax.
- No new instructions.
- No new diagnostic categories beyond exposing existing policies.
- No broad warning preset system.
- No provenance validation unless already implemented.
- No debugger memory visualization changes unless separately scoped.
- No global diagnostic audit or final warning taxonomy.

### Acceptance criteria

- Browser users can select implemented validation and teaching-diagnostic policies.
- Default UI behavior matches:
  - uninitialized-read warn;
  - undefined-flag-use warn;
  - compatibility notices on;
  - region-only memory validation;
  - object/section validation off.
- Opt-out settings restore silent uninitialized-read and undefined-flag-use behavior.
- Strict/error settings route to existing backend stop-before-mutation policies.
- UI settings route to the same backend policies used by native/source-run tests.
- Program Console and Simulator Messages remain separate streams.

---

## 58. Phase 54 - One-Operand Signed IMUL

### Goal

Implement one-operand signed `imul` with implicit accumulator operands.

This phase must not implement two-operand or three-operand `imul`, `mul`, `div`, `idiv`, labels, jumps, stack behavior, or procedure behavior.

### Behavior category

Runtime arithmetic instruction with implicit accumulator operands.

### Accepted syntax

```asm
imul reg8
imul reg16
imul reg32
imul BYTE PTR [reg32]
imul WORD PTR [reg32]
imul DWORD PTR [reg32]
imul symbol
imul symbol[offset]
```

### Rejected syntax

```asm
imul 5
imul eax, ebx       ; implemented by Phase 55 - Two- and Three-Operand IMUL Forms
imul eax, ebx, 5    ; implemented by Phase 55 - Two- and Three-Operand IMUL Forms
imul [eax]          ; ambiguous width
imul QWORD PTR q    ; executable QWORD/SQWORD memory operation remains deferred
```

### Runtime semantics

```text
IMUL r/m8:   AX      = signed(AL)  * signed(r/m8)
IMUL r/m16:  DX:AX   = signed(AX)  * signed(r/m16)
IMUL r/m32:  EDX:EAX = signed(EAX) * signed(r/m32)
```

### Flag behavior

- Clear `CF` and `OF` if the full signed product is exactly the sign extension of the lower half.
- Set `CF` and `OF` otherwise.
- Preserve `ZF` and `SF` as a deterministic educational policy for real x86 undefined flags.

### Required tests

- `AL=-2`, source `3` produces `AX=FFFAh`, `CF=0`, `OF=0`.
- `AL=7Fh`, source `2` produces overflow-significant result, `CF=1`, `OF=1`.
- `AX=-2`, source `3` produces `DX:AX=FFFFFFFAh` as a 32-bit combined value, meaning `DX=FFFFh` and `AX=FFFAh`, with no significant truncation.
- `EAX=-2`, source `3` produces `EDX:EAX=FFFFFFFFFFFFFFFAh` equivalent in `EDX:EAX`.
- Memory source read through checked memory helpers.
- Direct symbol memory source.
- Symbol-offset memory source.
- Explicit `PTR [reg32]` memory source.
- Readable `.CONST` memory source.
- Invalid-address runtime diagnostic for register-indirect source.
- Two- and three-operand forms are implemented and tested by **Phase 55 - Two- and Three-Operand IMUL Forms**.
- No memory-change rows are produced by memory-source reads.

### Acceptance program

```asm
.code
main PROC
    mov eax, -2
    mov ebx, 3
    imul ebx
main ENDP
END main
```

Expected:

```text
EDX = FFFFFFFFh / 4294967295
EAX = FFFFFFFAh / 4294967290
CF = 0
OF = 0
```

---

## 59. Phase 55 - Two- and Three-Operand IMUL Forms

### Goal

Implement the common non-accumulator `imul` forms only after one-operand `imul` is correct.

This phase must not implement `mul`, `div`, `idiv`, labels, jumps, stack behavior, or procedure behavior.

### Behavior category

Runtime arithmetic instruction.

### Accepted syntax

```asm
imul reg16, reg16
imul reg32, reg32
imul reg16, mem16
imul reg32, mem32
imul reg16, reg16, imm
imul reg32, reg32, imm
imul reg16, mem16, imm
imul reg32, mem32, imm
```

Immediate policy for this educational simulator:

- Accept constant-expression immediates that fit the signed destination/source operand width.
- Treat the immediate semantically as a signed operand-width value; do not expose MASM machine-code encoding distinctions such as `imm8` versus `imm16/imm32` in user diagnostics.
- Sign-extend the immediate to the operation width before multiplication.
- Reject out-of-range immediates with `immediate-out-of-range` or `invalid-instruction-operands`, pointing at the immediate expression.

### Rejected syntax

```asm
imul al, bl          ; no 8-bit two-operand form
imul mem, reg        ; memory destination rejected
imul reg, imm        ; ambiguous with unsupported form unless explicitly parsed
imul reg, reg, reg   ; third operand must be immediate
imul eax, ebx, ecx
```

### Runtime semantics

- Multiply signed source operands.
- Store the low destination-width result in the destination register.
- Do not modify the source register or source memory.
- Overflow is based on whether the full signed product is exactly the sign extension of the stored destination-width result.

### Flag behavior

- Set `CF` and `OF` when truncation loses significant signed bits.
- Clear `CF` and `OF` otherwise.
- Preserve `ZF` and `SF` as a deterministic educational policy for real x86 undefined flags.

### Required tests

- `imul eax, ebx` with `3*4` gives `12`, `CF=0`, `OF=0`.
- `imul eax, ebx, -5` handles signed immediate.
- Immediate boundary tests cover accepted `-2147483648`, accepted `2147483647`, and rejected out-of-range 32-bit signed constants.
- For 16-bit and 32-bit forms, include at least one no-overflow case and one truncation-overflow case proving `CF`/`OF` behavior.
- Overflow case sets `CF=1`, `OF=1`; no-overflow case clears both.
- Memory source read through checked memory helpers.
- Memory destination rejected.
- 8-bit two-operand form rejected.
- Missing/extra operands rejected with source spans.

---

## 60. Phase 56 - Unsigned DIV

### Goal

Implement one-operand unsigned `div` with precise divide-by-zero and quotient-overflow diagnostics.

This phase must not implement `idiv`, multiplication, labels, jumps, stack behavior, or procedure behavior.

### Behavior category

Runtime arithmetic instruction with implicit accumulator operands.

### Accepted syntax

```asm
div reg8
div reg16
div reg32
div BYTE PTR [reg32]
div WORD PTR [reg32]
div DWORD PTR [reg32]
div symbol
div symbol[offset]
```

### Rejected syntax

```asm
div 5
div eax, ebx
div [eax]          ; ambiguous width
div QWORD PTR q    ; executable QWORD/SQWORD memory operation remains deferred
```

### Runtime semantics

```text
DIV r/m8:   AX      / r/m8  -> AL quotient, AH remainder
DIV r/m16:  DX:AX   / r/m16 -> AX quotient, DX remainder
DIV r/m32:  EDX:EAX / r/m32 -> EAX quotient, EDX remainder
```

### Runtime errors

- Divisor zero: `divide-by-zero`.
- Quotient too large for the selected quotient register: `quotient-overflow`.
- Invalid memory divisor address: existing runtime memory diagnostic.
- On any divide error, leave quotient and remainder registers unchanged. Do not partially write `AL/AH`, `AX/DX`, or `EAX/EDX`.
- Divide errors must not create memory-change rows or partial implicit-register deltas.

Post-implementation correction:

Milestone 56 selected `quotient-overflow` as the canonical divide-family diagnostic code for an overflowing quotient. The earlier guide wording that allowed either `division-overflow` or `quotient-overflow` is obsolete. Future division-family phases, including Phase 57 - Signed IDIV, must use `quotient-overflow` unless a later reviewed canonical edit deliberately changes the diagnostic naming policy.

`division-overflow` must not be introduced as a second equivalent diagnostic code. If older notes, comments, or tests mention `division-overflow`, treat them as stale pre-implementation wording and update them to `quotient-overflow`.

This note corrects future-facing guide wording only. It does not imply that the completed Phase 56 implementation originally had different requirements or that milestone history should be rewritten.

### Flag behavior

- Preserve all currently modeled flag bits as a deterministic educational policy for real x86 undefined flags.
- Preserve Phase 50A flag-validity metadata exactly.
- A successful `div` must not mark any modeled flag valid or invalid.
- A failed `div`, including divide-by-zero, quotient overflow, invalid memory reads, strict planned-read validation failures, and strict uninitialized-read validation failures, must preserve both the deterministic flag bits and their validity metadata.

### Required tests

- 8-bit division: `AX=0014h`, divisor `5`, result `AL=4`, `AH=0`.
- 8-bit remainder case.
- 16-bit division using `DX:AX`.
- 32-bit division using `EDX:EAX`.
- Stale high-half test: set `EDX` nonzero so `DIV r/m32` overflows even though `EAX` alone would look divisible.
- Divide by zero from register.
- Divide by zero from memory.
- Quotient overflow for each supported width.
- Successful `div` preserves pre-set modeled flags `CF`, `ZF`, `SF`, and `OF`.
- Failed `div` preserves pre-instruction quotient/remainder registers and modeled flags.
- Memory source read through checked memory helpers.
- Ambiguous memory-width diagnostic.
- Rendered runtime diagnostics for divide-by-zero and quotient overflow.

### Acceptance program

```asm
.code
main PROC
    mov edx, 0
    mov eax, 100
    mov ebx, 7
    div ebx
main ENDP
END main
```

Expected:

```text
EAX = 0000000Eh / 14
EDX = 00000002h / 2
```

---


## 60A. Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics

### Goal

Improve the test harness so the full implemented-milestone suite remains easy to run, easy to split, and easy to report in constrained assistant/container environments.

This phase addresses verification ergonomics only. It exists because the aggregate test runner and some accumulated test families have grown large enough that hosted assistant tool environments may time out or truncate output before the final success line.

This phase must preserve all existing test coverage. It must not remove diagnostic-rendering tests, source-run regression tests, protocol tests, static checks, milestone-specific coverage, or exact rendered Simulator Messages checks merely to make the aggregate command shorter.

This phase must address three distinct size/timeout risks:

1. large aggregate runner or runner group output;
2. large test files, especially `tests/core/test_wasm_source_run.c`;
3. oversized individual MASM source fixtures or test programs.

### Placement and roadmap rule

This is a non-renumbering maintenance phase inserted after Phase 56 - Unsigned DIV and before Phase 57 - Signed IDIV.

Do not renumber Phase 57 or any later phase.

After Phase 56A is complete, the next runtime instruction milestone remains Phase 57 - Signed IDIV.

### Behavior category

Test-harness and reporting maintenance.

This phase must not change simulator language behavior, parser behavior, VM/executor behavior, memory behavior, diagnostic semantics, Program Console output, Simulator Messages wording, browser UI behavior, Wasm API behavior, or supported syntax.

The only allowed changes are test-runner, test-file organization, test-fixture organization, test-reporting, helper-script, and documentation changes needed to make test execution more modular and less noisy.

### Non-goals

This phase must not implement:

- signed `idiv`;
- new instructions;
- new MASM syntax;
- parser behavior changes;
- VM/executor runtime changes;
- memory model changes;
- diagnostic code changes;
- diagnostic JSON schema changes;
- rendered Simulator Messages wording changes;
- Program Console wording changes;
- browser diagnostic settings;
- browser UI features;
- Wasm runtime behavior;
- Emscripten build behavior except documentation or reporting of existing availability checks;
- roadmap renumbering.

This phase must not remove tests to make the suite faster.

This phase must not weaken exact assertions to make the suite easier to split.

If an implementation assistant finds a real project bug while working on this phase, it must report the bug separately. Do not mix simulator behavior fixes into Phase 56A unless the user explicitly authorizes a scope expansion.

### Required runner modes

Update `scripts/run_tests.py` so the implemented test suite can be run as a full aggregate or as focused groups.

Required command forms:

```text
python3 scripts/run_tests.py
python3 scripts/run_tests.py --all
python3 scripts/run_tests.py --quick
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
```

The existing command must remain valid:

```text
python3 scripts/run_tests.py
```

Default behavior may remain equivalent to `--all`. If default behavior is equivalent to `--all`, the runner should print a short hint explaining that focused groups are available when a hosted environment times out.

Windows usage must also work:

```bat
py scripts\run_tests.py
py scripts\run_tests.py --all
py scripts\run_tests.py --diagnostics
```

Runner argument parsing and subprocess invocation must remain portable across Windows and POSIX environments. Do not rely on shell-only syntax, Unix-only path separators, or Bash-specific behavior inside `scripts/run_tests.py`.

### Required output modes

Add output controls:

```text
--quiet
--verbose
```

Default output should be compact enough for assistant/container logs.

`--quiet` should print only:

- group start status;
- group pass/fail/skip status;
- final compact summary;
- failure details when a failure occurs.

`--verbose` should print fixture-level details, milestone fixture names, expected rendered diagnostic lines, subprocess commands, and other detailed output that is currently printed unconditionally.

On failure, the runner must always print enough information to identify:

- failing group;
- failing subgroup, when applicable;
- failing command;
- failing test file, when available;
- failing fixture or source-run program name, when available;
- subprocess exit code;
- captured stdout/stderr tail or full output according to size limits;
- path to any temporary output file or diff file, if generated.

A successful quiet run must not hide a failed subprocess. Quiet mode reduces noise only for success paths.

### Required test groups

The exact implementation may use repository-specific internal function names, but the runner must provide stable user-facing group names for at least:

```text
structure
native
source-run
web
diagnostics
protocol
static
```

Definitions:

- `structure`: repository structure checks, documentation file/header checks, Doxygen/static policy checks, milestone metadata checks, and source tree shape checks.
- `native`: native C unit, executor, parser, CPU, memory, and helper tests that do not require Node.
- `source-run`: native source-run JSON tests and integration-style source execution tests.
- `web`: Node tests for browser-side formatter, protocol, settings, or other modules that do not require a served browser.
- `diagnostics`: native diagnostic JSON producer build plus Node rendered Simulator Messages tests.
- `protocol`: worker/protocol/schema tests where separable from generic web tests.
- `static`: documentation, status, supported-syntax, manifest, and other static consistency checks.

If a test logically belongs to more than one group, choose one primary group and document that choice in the runner help text. Do not run the same expensive test twice in `--all` unless duplication is intentional and documented.

### `--quick` mode

Add a `--quick` mode for fast sanity checking.

`--quick` must be clearly documented as a smoke subset, not full verification.

`--quick` may run:

- structure checks;
- a representative native test subset;
- a representative source-run success fixture;
- a representative rendered diagnostic fixture;
- a representative web/protocol check.

`--quick` is never sufficient for milestone acceptance unless the user explicitly asks for smoke-only verification. A milestone report that uses only `--quick` must say that full verification was not performed.

Milestone reports must not say `All implemented milestone tests passed` if only `--quick` was run.

### Source-run test decomposition requirement

`tests/core/test_wasm_source_run.c` is a known high-growth integration test file. It has accumulated source-run coverage from many milestones, including memory/layout behavior, diagnostic policy behavior, instruction-family smoke tests, UI-settings policy routing, and regression fixtures.

Phase 56A must specifically review `tests/core/test_wasm_source_run.c`.

If `tests/core/test_wasm_source_run.c` is still small enough to compile and run comfortably as one binary in local and hosted assistant/container environments, it may remain one file, but the milestone report must say that it was reviewed and explain why splitting was not necessary.

If `tests/core/test_wasm_source_run.c` times out, produces excessive output, or is difficult to use for focused verification, split it into behavior-family source-run test files.

Preferred split:

```text
tests/core/test_source_run_memory_layout.c
tests/core/test_source_run_instruction_smoke.c
tests/core/test_source_run_diagnostic_policies.c
tests/core/test_source_run_settings.c
tests/core/test_source_run_regressions.c
```

Alternative names are allowed, but the grouping must be documented in:

- `scripts/run_tests.py --help`;
- `docs/TESTING_GUIDE.md`;
- the Phase 56A milestone report.

The split must preserve every existing fixture and assertion. A fixture may be renamed only if the new name remains descriptive and the milestone report maps the old name to the new name.

Do not move source-run fixtures into lower-level executor tests merely to reduce runtime. Source-run tests intentionally exercise parser, data layout, memory layout, VM execution, diagnostics, source-run JSON, and result rendering metadata together.

Required and preferred source-run runner forms:

Required:

```text
python3 scripts/run_tests.py --source-run
```

Preferred, and required if `tests/core/test_wasm_source_run.c` is split or if `--source-run` still risks timing out in hosted assistant/container verification:

```text
python3 scripts/run_tests.py --source-run=memory-layout
python3 scripts/run_tests.py --source-run=instructions
python3 scripts/run_tests.py --source-run=diagnostic-policies
python3 scripts/run_tests.py --source-run=settings
python3 scripts/run_tests.py --source-run=regressions
python3 scripts/run_tests.py --source-run=all
```

At minimum, `--source-run` must run independently from native unit tests, web tests, protocol tests, static checks, and rendered diagnostic tests.

If source-run subgrouping is not implemented in Phase 56A, the milestone report must explicitly say why, must prove that `tests/core/test_wasm_source_run.c` runs comfortably as an independent focused group, and must identify the future owner for subgrouping.

### Individual test-program size and fixture scope policy

Phase 56A must review not only large test files, but also large individual MASM source fixtures embedded in tests or loaded from fixture files.

A source-run fixture should normally test one behavior family or one regression. It should not become a broad milestone demo program that exercises many unrelated features unless the fixture is explicitly marked as an integration smoke test.

Large fixture risks:

- one failing assertion becomes hard to diagnose;
- one source program generates too much JSON or rendered output;
- source-location expectations become brittle;
- future assistants may misread a fixture as specifying more behavior than intended;
- assistant/container runs may time out or truncate output before the failure location is visible.

Rules for individual MASM test programs:

- Prefer several small focused fixtures over one very large fixture.
- Keep each fixture's purpose named and documented in the test helper call or fixture name.
- Do not combine unrelated behavior families merely to reduce the number of test cases.
- Do not use a giant "kitchen sink" source program as the only coverage for a feature.
- Integration smoke fixtures are allowed, but they must be labeled as smoke or integration fixtures and must not replace focused unit/source-run tests.
- If a fixture checks diagnostics, keep the expected diagnostic count and rendered output small enough that failures are easy to locate.
- If a fixture needs many similar cases, prefer table-driven fixtures or separate named fixture cases.
- If a fixture source becomes long enough that source line/column maintenance is error-prone, move it to a named fixture file or split it into smaller programs.
- Every pre-existing test fixture must either remain in place or be moved to a new file/group with equivalent assertions.
- The milestone report must account for moved, renamed, split, or intentionally preserved large fixtures.
- When splitting or externalizing fixtures, do not rewrite the MASM program to rely on different simulator semantics unless the milestone report identifies the change as a test refactor preserving equivalent coverage.

Recommended fixture categories:

```text
focused success fixture
focused error fixture
focused warning/notice fixture
edge-case fixture
regression fixture
integration smoke fixture
```

Large fixture review requirement:

Phase 56A must identify any unusually large embedded MASM source strings or external fixture programs. For each one, the milestone report must state one of:

```text
kept as-is because it is a labeled integration smoke fixture
kept as-is because it is still small and focused enough
split into smaller focused fixtures
converted into a table-driven set of smaller fixtures
moved into an external named fixture file for readability
left for a later phase, with reason and risk
```

Splitting a fixture must preserve all assertions. Do not delete edge cases while shrinking a test program.

### Diagnostic-rendering decomposition

The diagnostic-rendering test group must be further splittable by family once it contains multiple fixture families or begins exceeding assistant/container execution limits.

Required and preferred diagnostic runner forms:

Required:

```text
python3 scripts/run_tests.py --diagnostics
```

Preferred, and required if `--diagnostics` still risks timing out in hosted assistant/container verification:

```text
python3 scripts/run_tests.py --diagnostics=memory
python3 scripts/run_tests.py --diagnostics=directives
python3 scripts/run_tests.py --diagnostics=compatibility
python3 scripts/run_tests.py --diagnostics=arithmetic
python3 scripts/run_tests.py --diagnostics=shift-rotate
python3 scripts/run_tests.py --diagnostics=mul-div
python3 scripts/run_tests.py --diagnostics=all
```

If the implementation chooses a different subgroup scheme, it must document the names in:

- `python3 scripts/run_tests.py --help`;
- `docs/TESTING_GUIDE.md`;
- the Phase 56A milestone report.

`--diagnostics` must work as an independent focused group. Diagnostic subgroups such as `--diagnostics=memory` are preferred. If diagnostic subgroups are not implemented in Phase 56A, the milestone report must document why and identify the future owner.

Diagnostic-rendering coverage must remain exact for stable user-visible diagnostics. Do not weaken exact rendered Simulator Messages assertions merely to make subgrouping easier.

### Large test file decomposition

Review unusually large test files and split them only where doing so improves maintainability or focused execution.

Primary candidates:

- large native source-run regression files;
- large rendered diagnostic-message test files;
- large protocol/formatter tests if they mix unrelated concerns.

Splitting rules:

- Keep fixture names stable where practical.
- Do not weaken assertions.
- Do not remove exact rendered Simulator Messages tests.
- Do not replace exact diagnostic checks with broad substring checks unless the original exact check is preserved elsewhere and the exception is documented.
- Keep shared test helpers in common helper files rather than duplicating expected formatting logic.
- Avoid splitting files solely by milestone number if behavior-family grouping is clearer.
- Preserve current test order only when order is semantically important. Otherwise, group by behavior family.
- Do not split a file merely to satisfy this phase if the runner can already invoke that test family independently and the file remains maintainable.

Preferred file grouping:

```text
source-run memory/layout tests
source-run arithmetic/instruction tests
source-run diagnostics-policy tests
source-run settings tests
source-run regression tests
rendered memory diagnostics
rendered directive/compatibility diagnostics
rendered arithmetic diagnostics
rendered shift/rotate diagnostics
rendered mul/div diagnostics
web formatter tests
web protocol/settings tests
```

If a file is split, the milestone report must list the old file, new files, and the reason for the split.

### Source-run fixture inventory

Phase 56A must produce or update a source-run fixture inventory.

The inventory may live in `docs/TESTING_GUIDE.md`, runner help text, a test manifest, or another documented location.

The inventory must make it possible to identify:

- each source-run fixture or fixture family;
- which focused group or subgroup owns it;
- whether it is focused success, focused error, warning/notice, edge-case, regression, or integration smoke;
- whether it was moved, renamed, split, or intentionally preserved during Phase 56A.

The inventory does not need to duplicate every assertion. It is a navigation and audit aid for future assistants.

### Aggregate summary output

The aggregate runner must print a compact final summary table.

The table should include at least:

```text
Group        Status    Details
structure    PASS      ...
native       PASS      ...
source-run   PASS      ...
web          PASS      ...
diagnostics  PASS      ...
protocol     PASS      ...
static       PASS      ...
```

Allowed statuses:

```text
PASS
FAIL
SKIP
NOT-RUN
```

`SKIP` must include a reason, such as:

```text
SKIP - emcc unavailable
```

`NOT-RUN` should be used only when a selected command did not request that group.

The final aggregate success line must remain clear. The existing final wording may be preserved if already stable:

```text
All implemented milestone tests passed.
```

If any selected group fails, the runner must exit nonzero and must not print the all-tests-passed line.

The goal is not to guarantee a universal runtime limit. The goal is that each focused group produces bounded, reviewable output and can be rerun independently when the aggregate command is too large for the environment.

### Timeout-safe assistant verification policy

Add documented verification guidance for assistant/container environments.

Required wording for `docs/TESTING_GUIDE.md` or equivalent testing documentation:

```text
If `python3 scripts/run_tests.py --all` times out or output is truncated in a hosted assistant/container environment, this is not automatically a project test failure.

The assistant must rerun focused groups individually and report:

- which focused groups were run;
- which focused groups passed;
- which focused groups failed;
- which focused groups were skipped and why;
- whether any focused group required subgroup or fixture-level reruns;
- whether `--all` completed in that environment;
- whether the final local/user run produced `All implemented milestone tests passed.`
```

Milestone reports must distinguish:

```text
aggregate completed and passed
aggregate timed out in assistant/container environment, focused groups passed
aggregate failed with a real failing group
focused group failed
focused group timed out, subgroups or fixtures rerun
group skipped because dependency unavailable, such as emcc
```

An assistant must not claim that the full aggregate suite passed unless the aggregate command actually completed and returned the final success line in that environment.

An assistant may report focused verification only by naming the focused groups, subgroups, or fixtures that actually passed.

### Browser/Wasm availability reporting

Preserve honest reporting around Emscripten availability.

If `emcc` is unavailable, the runner or milestone report may say browser/Wasm rebuild smoke was not run in that environment. This must not be reported as a test failure unless the current phase explicitly requires an Emscripten-capable environment.

The runner should keep or improve current browser manual-smoke status reporting.

Manual browser testing remains required when these change:

- Emscripten build scripts;
- `web/dist` artifact generation;
- Worker loading;
- worker protocol shape;
- UI DOM rendering;
- CodeMirror diagnostic integration;
- source-run JSON schema;
- formatter module import path or public API.

Manual browser testing is still supplemental. It is not a replacement for native, source-run, Node, protocol, static, or rendered diagnostic tests.

### Required documentation updates

Update:

```text
docs/TESTING_GUIDE.md
README.md, if it lists test commands
any milestone-reporting guidance or report template, if present
```

Documentation must include:

- full aggregate command;
- focused group commands;
- source-run subgroup commands;
- diagnostic subgroup commands, if implemented;
- quiet and verbose modes;
- quick mode and its limitations;
- recommended assistant/container verification workflow;
- how to interpret assistant tool timeouts;
- how to report unavailable Emscripten/Wasm browser smoke;
- how to classify large fixtures;
- Windows command examples.

### Required tests for the runner itself

Add tests or static checks proving:

- `python3 scripts/run_tests.py --help` lists every supported group flag.
- `--all` is accepted.
- `--quick` is accepted.
- `--quiet` is accepted.
- `--verbose` is accepted.
- every required focused group flag is accepted.
- `--source-run` is accepted.
- source-run subgroup flags are accepted if implemented.
- `--diagnostics` is accepted.
- diagnostic subgroup flags are accepted if implemented.
- unknown flags fail with a nonzero exit code and a useful message.
- group names used in documentation match group names accepted by the runner.
- the default command remains valid.
- at least one focused group can be run independently without invoking the full aggregate suite.
- failure reporting includes the group name and failing command.
- source-run fixture failures include the fixture name.
- moved or split fixture mappings are documented if files are split.

These runner tests should avoid recursively running the entire suite where possible. Prefer lightweight static checks, dry-run mode, help-output checks, command-construction checks, or a small self-test path.

If a dry-run mode is added for runner self-tests, it must be clearly documented as a runner-planning feature, not as a substitute for running tests.

### Acceptance criteria

This phase passes only when:

- `python3 scripts/run_tests.py` still works.
- `python3 scripts/run_tests.py --all` works.
- focused group commands work for `structure`, `native`, `source-run`, `web`, `diagnostics`, `protocol`, and `static`.
- `--quick` works and is documented as a smoke subset.
- `--quiet` reduces routine successful output.
- `--verbose` preserves detailed fixture-level reporting.
- `tests/core/test_wasm_source_run.c` is explicitly reviewed.
- the milestone report states whether `tests/core/test_wasm_source_run.c` was kept whole or split.
- if `tests/core/test_wasm_source_run.c` is split, all moved fixtures are mapped from old location/name to new location/name.
- if `tests/core/test_wasm_source_run.c` is not split, `python3 scripts/run_tests.py --source-run` must compile and run the source-run suite independently within the assistant/container limit.
- large individual MASM source fixtures are reviewed.
- any oversized fixture is either split, labeled as integration smoke, moved to an external fixture file, converted to table-driven smaller fixtures, kept with rationale, or explicitly deferred with rationale.
- no fixture coverage is removed.
- no exact rendered diagnostic assertion is weakened.
- diagnostic-rendering tests can be run without running every native/source-run test first.
- source-run tests can be run without running Node rendered diagnostic tests.
- Node rendered diagnostic tests can be run without running the full source-run suite.
- the aggregate runner prints a compact final summary table.
- the runner exits nonzero on real selected-group failure.
- a timeout-safe assistant verification policy is documented.
- unavailable Emscripten/browser smoke is reported separately from test failure.
- existing test coverage is preserved.
- exact rendered Simulator Messages tests are preserved.
- no simulator runtime behavior changes.
- no supported syntax changes.
- no diagnostic semantics changes.
- no diagnostic wording changes except runner/reporting text.
- no roadmap renumbering occurs.

### Commands to use for Phase 56A verification

Minimum expected commands:

```text
python3 scripts/run_tests.py --help
python3 scripts/run_tests.py --quick
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
python3 scripts/run_tests.py --all
```

If source-run subgroups are implemented, also run:

```text
python3 scripts/run_tests.py --source-run=memory-layout
python3 scripts/run_tests.py --source-run=instructions
python3 scripts/run_tests.py --source-run=diagnostic-policies
python3 scripts/run_tests.py --source-run=settings
python3 scripts/run_tests.py --source-run=regressions
python3 scripts/run_tests.py --source-run=all
```

If diagnostic subgroups are implemented, also run the documented diagnostic subgroup commands.

If the hosted assistant/container environment times out on `--all`, rerun every focused group and report the timeout as an environment limitation, not a project failure, unless one focused group fails.

If the hosted assistant/container environment times out on `--source-run`, rerun source-run subgroups or documented source-run fixture families and report exactly which ones passed.

Windows examples for user/local verification:

```bat
py scripts\run_tests.py --help
py scripts\run_tests.py --quick
py scripts\run_tests.py --source-run
py scripts\run_tests.py --diagnostics
py scripts\run_tests.py --all
```

### Milestone report requirements

The Phase 56A milestone report must include:

- files changed;
- test files split, with old and new file names;
- explicit disposition of `tests/core/test_wasm_source_run.c`;
- fixture-level changes, not only file-level changes;
- moved/renamed/split fixture mapping;
- large fixtures reviewed and their disposition;
- runner flags added;
- output modes added;
- focused groups added;
- source-run subgroup behavior;
- diagnostic-rendering subgroup behavior;
- documentation updated;
- runner self-tests added;
- exact commands run;
- whether aggregate `--all` completed in the assistant/container environment;
- focused group pass/fail/skip results;
- subgroup or fixture pass/fail/skip results if a focused group timed out;
- any skipped browser/Wasm checks and the reason;
- explicit confirmation that no simulator behavior, supported syntax, diagnostic semantics, or rendered Simulator Messages wording changed.

### Runtime metadata note

Phase 56A does not advance runtime/source-run MASM behavior metadata.

After Phase 56A:

```text
Repository/archive milestone: Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics
Runtime/source-run MASM behavior phase: Phase 56 - Unsigned DIV
```

Future phases must preserve this distinction. Do not change source-run JSON phase fields, protocol phase assertions, browser runtime-status values, supported-syntax runtime wording, README current-status wording, worker status wording, or tests that check runtime phase metadata merely because Phase 56A exists.

After Phase 56A, Phase 56B - User-Facing Diagnostic Wording Cleanup is the next corrective documentation/diagnostic cleanup phase. Phase 56B is inserted after Phase 56A and before Phase 57 - Signed IDIV without renumbering Phase 57 or any later phase.

Phase 56B does not add MASM syntax, parser acceptance behavior, VM behavior, executor behavior, memory behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, diagnostic codes, diagnostic JSON fields, diagnostic policy modes, or rendered Simulator Messages categories. It cleans live milestone-relative wording and current-status wording so the repository is clearer before the next runtime instruction milestone.

After Phase 56B is complete, Phase 57 - Signed IDIV is the next runtime arithmetic phase that may advance runtime/source-run MASM behavior metadata.

## 60B. Phase 56B - User-Facing Diagnostic Wording Cleanup

### Goal

Replace milestone-relative user-facing diagnostic wording with stable, feature-specific wording.

This is a diagnostic wording, current-status wording, and test-maintenance phase. It must not add MASM syntax, parser behavior, executor behavior, VM behavior, memory behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, new diagnostic codes, new diagnostic policies, or new rendered Simulator Messages categories.

### Placement, priority, and roadmap rule

Insert this phase immediately after Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics and before Phase 57 - Signed IDIV.

This is a non-renumbering corrective phase. Do not renumber Phase 57 - Signed IDIV or any later phase. If the guide's local Markdown section numbering changes in a future revision, preserve the phase title exactly and use the guide's local non-renumbering suffix convention.

The user controls implementation order. This guide defines Phase 56B as the canonical cleanup phase for live milestone-relative wording and current-status wording after Phase 56A. Future assistants must treat Phase 56B as a documented corrective phase, not as a runtime behavior phase or an opportunity to combine unrelated work.

Phase 56B is diagnostic wording, current-status wording, and test-maintenance work only. It must not add MASM syntax, parser acceptance, executor behavior, VM behavior, memory behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, new diagnostic codes, new diagnostic policies, or new rendered Simulator Messages categories.

For this phase, milestone-relative wording includes phrases such as:

```text
current milestone
this milestone
unsupported by the current milestone
not implemented in this milestone
not supported in this milestone
unsupported in this milestone
```

Milestone-relative wording is allowed in historical milestone reports, historical audit notes, historical handoff reports, and guide text that explicitly explains why such wording is prohibited. It must not appear as live current behavior wording in current user-facing diagnostics, current supported-syntax documentation, README current-status text, browser status text, worker-generated `ui-error` messages, or exact rendered-message tests for current behavior.

### Motivation

User-facing diagnostics should explain the actual simulator rule, unsupported form, non-goal, or future phase. They should not depend on phrases such as “current milestone,” because repository/archive milestones and runtime/source-run MASM behavior phases can differ after maintenance phases such as Phase 56A.

Milestone-relative wording is allowed in historical milestone reports and internal audit notes. It should not appear in diagnostics shown to users in Simulator Messages.

### Scope

Audit live user-facing text, current-status text, and assistant-facing runtime-metadata wording emitted by or shipped with the current repository.

The audit must include, at minimum:

- lexer diagnostic messages;
- parser diagnostic messages;
- source-run/Wasm diagnostic messages;
- executor/runtime diagnostic messages;
- worker-generated `ui-error` messages;
- browser Simulator Messages formatter output;
- exact rendered Simulator Messages expected-output tests;
- supported-syntax examples that quote expected user-facing diagnostics;
- `docs/SUPPORTED_SYNTAX.md` current behavior summaries;
- `README.md` current-scope and current-status text;
- browser runtime-status strings;
- worker/protocol status strings;
- source-run JSON phase/status fields and any human-readable status strings carried in source-run JSON payloads;
- test assertion descriptions that refer to runtime/source-run phase metadata;
- test-runner reporting strings that name current runtime instruction coverage or current behavior.

Replace milestone-relative phrases such as:

```text
unsupported by the current milestone
current milestone
this milestone does not support
not supported in this milestone
unsupported in this milestone
will be added in a future milestone
implemented through the current milestone
current milestone metadata
```

with stable, feature-specific wording.

Preferred wording patterns:

```text
This instruction form is not supported in MASM32 Educational Mode.
This valid MASM form is deferred to Phase <N> - <Phase Title>.
Executable QWORD/SQWORD memory operations are deferred until Extended 32-bit Mode.
The memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR.
This construct is a simulator non-goal: real Windows API execution is not supported.
This include is unsupported because host filesystem include loading is not part of the browser simulator.
Runtime/source-run MASM behavior is implemented through Phase <N> - <Phase Title>.
Repository/archive milestone is Phase <N or suffix> - <Phase Title>.
Runtime/source-run MASM behavior phase is Phase <N> - <Phase Title>.
```

When a future phase is named, include both the phase number and phase title. Do not use a bare phase number.

### Planned-future wording is allowed when stable

Phase 56B must not remove useful, accurate roadmap references merely because they mention a future phase.

The problem is milestone-relative wording such as:

```text
unsupported by the current milestone
not supported in this milestone
will be added in a future milestone
```

The problem is not a precise statement that a known feature is deferred to a named phase.

Allowed examples:

```text
Two- and three-operand IMUL forms are deferred to Phase 55 - Two- and Three-Operand IMUL Forms.
Signed IDIV is deferred to Phase 57 - Signed IDIV.
Executable QWORD/SQWORD memory operations are not supported in MASM32 Educational Mode and are deferred until Extended 32-bit Mode.
```

Disallowed examples:

```text
Two- and three-operand IMUL forms are not supported in this milestone.
IDIV is unsupported by the current milestone.
QWORD memory execution will be added in a future milestone.
```

Use these rules:

1. If a feature is assigned to a specific later phase, name both the phase number and phase title.
2. If a feature is deferred to a broader mode or roadmap area, name that stable destination, such as `Extended 32-bit Mode`.
3. If a feature is a simulator non-goal, say it is a non-goal and do not describe it as deferred.
4. If a form is MASM-invalid, explain the MASM-style correction instead of calling it unsupported.
5. Do not use `current milestone`, `this milestone`, or `future milestone` as the reason for rejection.

The audit must distinguish live current-status/user-facing text from historical evidence. Historical milestone reports, handoff reports, audit reports, and change logs may retain milestone-relative wording if they are clearly historical. Do not rewrite historical reports during Phase 56B unless the user explicitly asks for historical report cleanup.

The audit must also distinguish user-facing wording from internal comments. Internal comments may mention milestone-relative phrases only when they are explaining this rule or preserving historical context. New comments should still prefer stable phase names and behavior descriptions.

### Known seed findings from the Phase 56A follow-up audit

The Phase 56B implementation must perform a repository-wide audit. Do not limit the work to the examples in this subsection. These seed findings exist only to prevent future assistants from missing already-observed problem areas.

A Phase 56A follow-up audit found milestone-relative or assistant-confusing wording in these areas:

- parser diagnostics for unsupported `TYPE`, `LENGTHOF`, and `SIZEOF` expression forms;
- parser diagnostics for unsupported destination widths;
- generic unsupported-instruction diagnostics;
- repeated-section or unsupported section-order diagnostics;
- text `EQU` / `TEXTEQU` diagnostics;
- README current-scope wording that says behavior is implemented through the “current milestone” rather than naming the runtime/source-run MASM behavior phase;
- supported-syntax current behavior wording that says “unsupported in this milestone” rather than describing the current supported subset or the stable reason for rejection;
- non-user-facing test assertion descriptions that say “current milestone metadata” where “runtime/source-run MASM behavior phase metadata” is more accurate.

For each live user-facing diagnostic changed in Phase 56B:

1. preserve the existing diagnostic code unless the code itself is inaccurate;
2. preserve severity unless the severity itself is inaccurate;
3. preserve source line, source column, byte offset, and span length;
4. preserve JSON field names unless the user explicitly authorizes a diagnostic schema correction;
5. update exact rendered Simulator Messages tests.

For current-status documentation changed in Phase 56B:

1. distinguish repository/archive milestone from runtime/source-run MASM behavior phase when they differ;
2. name both phase number and phase title;
3. do not imply that Phase 56B adds MASM syntax or runtime behavior;
4. do not update runtime/source-run phase metadata merely because repository/archive milestone advances to Phase 56B.

### Required constraints

- Preserve existing diagnostic codes unless the code itself is inaccurate.
- Preserve parser/executor acceptance and rejection behavior.
- Preserve source line, column, byte offset, and span length.
- When only message text changes, preserve the existing diagnostic code, severity, source line, source column, byte offset, span length, and JSON field names unless Phase 56B explicitly documents a structured diagnostic schema defect and the user authorizes a schema correction.
- Preserve Program Console and Simulator Messages separation.
- Preserve exact rendered Simulator Messages testing discipline.
- Do not weaken exact rendered-message assertions to broad substring checks.
- Do not remove diagnostics to reduce test maintenance.
- Do not combine this cleanup with Phase 57 - Signed IDIV arithmetic unless the user explicitly authorizes combined scope.

### Non-goals

Phase 56B must not implement:

- `idiv`;
- any new instruction;
- labels, jumps, conditional jumps, or `cmp`;
- stack behavior;
- procedure behavior;
- Irvine32 routine expansion;
- macro expansion;
- WinAPI behavior;
- PE/object/linker behavior;
- new UI settings;
- new diagnostic policies;
- new diagnostic severity categories.

### Required tests

Add or update tests for every changed live user-facing diagnostic string, current-status string, or runtime-metadata assertion description.

Required coverage:

1. **Structured diagnostic coverage**
   - Add or update native parser, executor, or source-run tests for every changed diagnostic path.
   - Preserve the existing diagnostic code unless the code itself is inaccurate.
   - Preserve severity, source line, source column, byte offset, span length, and JSON field names unless the user explicitly authorizes a schema correction.

2. **Rendered Simulator Messages coverage**
   - Add or update exact rendered Simulator Messages tests for every changed rendered diagnostic.
   - Do not replace exact assertions with broad substring checks.
   - Do not remove diagnostics to reduce test maintenance.

3. **Worker/protocol coverage**
   - Add or update protocol tests for changed worker-generated `ui-error` messages or status strings.
   - Preserve malformed protocol request behavior unless the changed message is specifically in scope.

4. **Current-status documentation coverage**
   - Add or update static checks for README and supported-syntax current-status wording when those files are changed.
   - If repository/archive milestone and runtime/source-run MASM behavior phase differ, documentation must name both values explicitly.
   - Documentation must not say or imply that a maintenance-only phase added MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, diagnostic codes, diagnostic JSON fields, or rendered Simulator Messages categories.

5. **Runtime/source-run metadata coverage**
   - If test assertion descriptions mention “current milestone metadata,” update them to “runtime/source-run MASM behavior phase metadata” or equivalent stable wording.
   - Do not update source-run JSON phase fields, protocol phase values, browser runtime-status strings, or supported-syntax runtime-status wording merely because Phase 56B exists.

6. **Static milestone-relative wording audit**
   - Add a static audit that fails when live user-facing or current-status files contain forbidden milestone-relative wording.
   - The audit must scan at least:
     - `src/`;
     - `web/src/`;
     - `tests/` expected rendered-message fixtures and assertion descriptions;
     - `docs/SUPPORTED_SYNTAX.md`;
     - `README.md`;
     - any source-run diagnostic JSON producer fixtures;
     - any worker/protocol status-message fixtures.
   - The audit must ignore:
     - `Milestone *.md` historical reports;
     - historical changelog/audit/handoff documents;
     - repository archives or generated ZIP contents;
     - comments or guide text that explicitly explain why milestone-relative wording is prohibited;
     - negative assertions or audit fixtures where the forbidden phrase appears only as the intentionally rejected text being searched for, such as `expect_json_not_contains(..., "unsupported by the current milestone", ...)`; such strings are allowed only when the test proves the phrase is absent from user-facing output and does not render the phrase as expected output;
     - this Phase 56B section of the implementation guide when the forbidden phrase appears only as an example of wording that must not appear in live diagnostics or current-status text.

### Acceptance criteria

Phase 56B is complete only when all of the following are true:

1. A repository-wide audit of live user-facing text, current-status text, and assistant-facing runtime-metadata wording has been performed.
2. Live user-facing diagnostics no longer contain milestone-relative phrases such as:
   - `current milestone`;
   - `this milestone`;
   - `unsupported by the current milestone`;
   - `not implemented in this milestone`;
   - `not supported in this milestone`;
   - `unsupported in this milestone`.
3. Current-status documentation no longer uses `current milestone` as a substitute for a named repository/archive milestone or runtime/source-run MASM behavior phase.
4. README and supported-syntax current-status text distinguish repository/archive milestone from runtime/source-run MASM behavior phase when those values differ.
5. Stable planned-future wording remains allowed when it names a specific phase number and phase title, or a stable roadmap destination such as Extended 32-bit Mode.
6. Changed diagnostics preserve diagnostic code, severity, source line, source column, byte offset, span length, and JSON field names unless the milestone report explicitly documents a user-authorized schema correction.
7. Changed rendered Simulator Messages have exact expected-output tests.
8. Changed worker-generated `ui-error` messages, if any, have protocol or worker tests.
9. A static milestone-relative wording audit exists and passes.
10. Focused groups pass:
    - `python3 scripts/run_tests.py --diagnostics`
    - `python3 scripts/run_tests.py --source-run`
    - `python3 scripts/run_tests.py --static`
11. If the implementation touched browser formatter, worker, or protocol code, the relevant focused groups also pass:
    - `python3 scripts/run_tests.py --web`
    - `python3 scripts/run_tests.py --protocol`
12. If the aggregate command completes in the environment, it must pass before the report may say `All implemented milestone tests passed.`
13. If the aggregate command times out or its output is truncated in an assistant/container environment, focused verification must be reported honestly according to §2.4b.
14. The milestone report states explicitly that Phase 56B did not change:
    - MASM syntax support;
    - parser acceptance or rejection behavior;
    - executor behavior;
    - VM behavior;
    - memory behavior;
    - Wasm API behavior;
    - browser controls, browser layout, browser interaction behavior, or settings behavior;
    - diagnostic codes, unless specifically user-authorized;
    - diagnostic JSON fields, unless specifically user-authorized;
    - rendered Simulator Messages categories.
15. The milestone report includes both labels:
    ```text
    Repository/archive milestone:
    Phase 56B - User-Facing Diagnostic Wording Cleanup

    Runtime/source-run MASM behavior phase:
    Phase 56 - Unsigned DIV
    ```

### Runtime metadata note

Phase 56B is a diagnostic wording, current-status wording, and test-maintenance phase. It does not advance runtime/source-run MASM behavior metadata.

After Phase 56B:

```text
Repository/archive milestone:
Phase 56B - User-Facing Diagnostic Wording Cleanup

Runtime/source-run MASM behavior phase:
Phase 56 - Unsigned DIV
```

Do not update source-run JSON phase fields, protocol phase values, browser runtime-status strings, supported-syntax runtime-status wording, or tests that assert runtime phase values merely because Phase 56B exists.

Phase 56B changes wording, current-status text, tests, and static audit coverage. Those changes do not make Phase 56B a runtime behavior phase.

The runtime/source-run MASM behavior phase remains Phase 56 - Unsigned DIV after Phase 56B. Do not update runtime phase constants, source-run phase JSON fields, browser runtime behavior labels, supported-syntax runtime behavior labels, or tests that assert runtime phase values to Phase 56B.

---

## 61. Phase 57 - Signed IDIV


Phase 57 follows Phase 56B - User-Facing Diagnostic Wording Cleanup in the canonical roadmap.

Phase 57 is the next runtime instruction phase after the Phase 56A/56B maintenance sequence. It adds signed `idiv` behavior and may intentionally advance runtime/source-run MASM behavior metadata after implementation and tests.

Before implementing Phase 57, verify that the test runner can execute focused groups independently, especially `--source-run`, `--diagnostics`, and `--native`.

Signed IDIV adds runtime-error, quotient-overflow, divide-by-zero, memory-source, no-partial-mutation, and rendered Simulator Messages coverage. Therefore Phase 57 implementation must start from the decomposed runner introduced in Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics and the cleaned diagnostic/current-status wording baseline established by Phase 56B - User-Facing Diagnostic Wording Cleanup.

This dependency does not change Phase 57's instruction scope. Do not implement additional test-runner work inside Phase 57 unless Phase 56A was left incomplete and the user authorizes that scope. Do not implement Phase 56B diagnostic wording cleanup inside Phase 57 unless the user explicitly authorizes combined scope.

### Goal

Implement one-operand signed `idiv` after Phase 56 - Unsigned DIV is correct.

This phase adds signed accumulator division only. It must preserve all existing `div`, `mul`, and `imul` behavior.

This phase must not implement multiplication, labels, jumps, conditional jumps, `cmp`, stack behavior, procedure behavior, Irvine32 routine expansion, scaled-index addressing, executable QWORD/SQWORD memory operations, or any future instruction group.

### Behavior category

Runtime arithmetic instruction with implicit accumulator operands.

`idiv` is a memory-reading or register-reading instruction. It does not write memory. Memory divisor reads must use the same checked VM memory helper path, planned-read validation path, uninitialized-read policy path, and memory-validation policy path used by the existing memory-source arithmetic instructions.

### Runtime metadata, documentation, and status updates

Phase 57 adds runtime-visible MASM/source behavior. Therefore, after `idiv` is implemented, tested, and accepted, Phase 57 must advance the runtime/source-run MASM behavior phase from:

```text
Phase 56 - Unsigned DIV
```

to:

```text
Phase 57 - Signed IDIV
```

This is different from Phase 56A. Phase 56A advanced repository/archive milestone status only. It did not add MASM syntax, parser behavior, VM behavior, executor behavior, Wasm API behavior, browser controls, browser layout, browser interaction behavior, diagnostic codes, diagnostic JSON fields, or rendered Simulator Messages wording. Phase 56A therefore did not advance runtime/source-run MASM behavior metadata.

Phase 56B - User-Facing Diagnostic Wording Cleanup follows the same maintenance-phase metadata rule. Phase 56B advances repository/archive status when implemented, but it does not advance runtime/source-run MASM behavior metadata.

When Phase 57 starts from a Phase 56B repository/archive baseline, the starting status is:

```text
Repository/archive milestone:
Phase 56B - User-Facing Diagnostic Wording Cleanup

Runtime/source-run MASM behavior phase:
Phase 56 - Unsigned DIV
```

After `idiv` is implemented, tested, and accepted, Phase 57 intentionally advances both repository/archive milestone and runtime/source-run MASM behavior phase to:

```text
Phase 57 - Signed IDIV
```

Phase 57 must update every runtime-visible status surface that intentionally reports latest implemented MASM/source execution behavior, including:

- source-run JSON phase fields;
- source-run JSON status strings, if any;
- native source-run tests that assert phase or runtime status;
- worker/protocol phase assertions;
- browser runtime-status text;
- README current-status text;
- `docs/SUPPORTED_SYNTAX.md`;
- `docs/TESTING_GUIDE.md`;
- aggregate or focused test-runner reporting text where it names runtime instruction coverage;
- default browser sample/status text if it names the latest runtime instruction phase.

Do not erase Phase 56A or Phase 56B history. Phase 56A remains the repository/archive maintenance milestone that introduced test-runner decomposition and assistant verification ergonomics. Phase 56B remains the repository/archive maintenance milestone that cleaned live diagnostic wording and current-status wording before Phase 57.

After a successful Phase 57 runtime milestone, the normal status is:

```text
Repository/archive milestone:
Phase 57 - Signed IDIV

Runtime/source-run MASM behavior phase:
Phase 57 - Signed IDIV
```

This match is allowed because Phase 57 is a runtime behavior milestone. Future maintenance-only phases after Phase 57 may again advance repository/archive milestone status without changing runtime/source-run MASM behavior metadata.

Required Phase 57 metadata tests:

- A source-run metadata test must prove runtime/source-run phase reporting identifies Phase 57 only after `idiv` support is implemented.
- A protocol/browser-status test must prove browser-facing runtime status identifies Phase 57.
- A supported-syntax documentation/static test must prove `docs/SUPPORTED_SYNTAX.md` lists `idiv` as supported and identifies runtime instruction support through Phase 57.
- A regression test must prove Phase 56A and Phase 56B maintenance-only status did not cause premature Phase 57 metadata before `idiv` implementation.

### Accepted syntax

```asm
idiv reg8
idiv reg16
idiv reg32

idiv BYTE PTR [reg32]
idiv WORD PTR [reg32]
idiv DWORD PTR [reg32]

idiv SBYTE PTR [reg32]
idiv SWORD PTR [reg32]
idiv SDWORD PTR [reg32]

idiv symbol
idiv symbol[offset]
```

Signed PTR aliases are accepted by executable width:

```text
SBYTE PTR  -> 8-bit divisor
SWORD PTR  -> 16-bit divisor
SDWORD PTR -> 32-bit divisor
```

Signedness of the divisor is determined by the `idiv` instruction semantics, not by declaration signedness alone. For `idiv`, the resolved operand width controls how the divisor bits are sign-interpreted:

```text
8-bit divisor:   sign-interpret source8
16-bit divisor:  sign-interpret source16
32-bit divisor:  sign-interpret source32
```

Direct symbols and symbol-offset operands infer memory width from existing symbol metadata and the existing global memory-width resolution rules. Explicit `PTR` width overrides take precedence according to the established memory-width policy.

`.CONST` divisor sources are readable. `idiv` must not reject `.CONST` memory sources merely because the section is read-only, because this instruction reads the divisor and does not write memory.

### Rejected syntax

```asm
idiv 5
idiv eax, ebx
idiv [eax]
idiv QWORD PTR q
idiv SQWORD PTR q
idiv
idiv eax, ebx, ecx
```

Diagnostic expectations:

- `idiv 5` reports `invalid-instruction-operands`.
- `idiv eax, ebx` reports `invalid-instruction-operands`.
- `idiv` with no operand reports `invalid-instruction-operands`.
- `idiv eax, ebx, ecx` reports `invalid-instruction-operands`.
- `idiv [eax]` reports `ambiguous-memory-width` because the memory access width is not inferable.
- `idiv QWORD PTR q` and `idiv SQWORD PTR q` must use the same stable diagnostic code currently used by executable QWORD/SQWORD memory-operation rejection in MASM32 Educational Mode. Do not introduce a new IDIV-specific diagnostic code for this case.

These rejections must not be described as temporary parser gaps if they are MASM32 Educational Mode policy. In particular, ambiguous memory-width forms must be diagnosed as ambiguous width, not as unsupported syntax.

### Runtime semantics

For 8-bit divisor operands:

```text
signed(AX) / signed(source8) -> AL quotient, AH remainder
```

For 16-bit divisor operands:

```text
signed(DX:AX) / signed(source16) -> AX quotient, DX remainder
```

For 32-bit divisor operands:

```text
signed(EDX:EAX) / signed(source32) -> EAX quotient, EDX remainder
```

Rules:

- Division is signed.
- The divisor is read at the resolved operand width.
- The divisor bits are sign-interpreted at the resolved operand width.
- The dividend is the required implicit accumulator pair for the divisor width.
- The dividend is sign-interpreted at the full dividend width:
  - `AX` as signed 16-bit for an 8-bit divisor;
  - `DX:AX` as signed 32-bit for a 16-bit divisor;
  - `EDX:EAX` as signed 64-bit for a 32-bit divisor.
- The quotient truncates toward zero.
- The remainder has the same sign as the dividend.
- The absolute value of the remainder is less than the absolute value of the divisor.
- The quotient must fit in the signed range of the selected quotient register:
  - `AL`: -128 through 127;
  - `AX`: -32768 through 32767;
  - `EAX`: -2147483648 through 2147483647.
- Only the selected quotient and remainder registers are updated on success.
- Source registers and source memory are not modified.
- Memory divisor reads do not create memory-change rows.

### Validation and execution order

The implementation must validate all required conditions before committing quotient or remainder registers.

For a memory divisor:

1. Resolve the final effective address and memory width.
2. Run mandatory checked VM memory read validation.
3. Run selected memory-validation policy checks that apply to planned reads.
4. Run selected uninitialized-read policy checks.
5. If strict planned-read validation or strict uninitialized-read validation stops execution, do not consume the divisor value and do not perform division.
6. If the divisor read succeeds, evaluate divide-by-zero.
7. If the divisor is nonzero, compute the signed mathematical quotient and remainder using a safe intermediate representation.
8. Check whether the quotient fits in the selected signed quotient register.
9. Commit quotient and remainder registers only after every validation step succeeds.

For a register divisor:

1. Read the selected register or register alias value.
2. Sign-interpret it at the selected divisor width.
3. Evaluate divide-by-zero.
4. Compute the signed mathematical quotient and remainder using a safe intermediate representation.
5. Check whether the quotient fits in the selected signed quotient register.
6. Commit quotient and remainder registers only after every validation step succeeds.

### Source-run planned-read and diagnostic-policy integration

Phase 57 owns integration of `idiv` memory divisor operands with every existing source-run planned-read and planned-access validation path.

Executor-level checked memory reads are mandatory, but they are not sufficient by themselves. The simulator also has source-run/UI diagnostic policies that can stop execution before an instruction mutates state. A memory-source `idiv` must participate in those pre-mutation policy checks the same way existing memory-source arithmetic instructions do.

When Phase 57 adds the `idiv` IR opcode, the implementation must audit and update every switch, helper, visitor, table, or collector that enumerates memory-reading opcodes for planned validation.

At minimum, memory-divisor `idiv` forms must be included in:

- planned memory-read collection for uninitialized-read diagnostics;
- section-capacity planned-read validation;
- section-image planned-read validation;
- declared-object planned-read validation;
- source-run/Wasm policy routing used by browser diagnostic settings;
- native diagnostic JSON producer paths used by rendered Simulator Messages tests.

If the codebase contains a TODO or comment that says future memory-capable opcodes must update planned-read or planned-access collection, Phase 57 owns resolving that TODO for `idiv`.

The required ordering is:

1. Parse and lower the `idiv` instruction.
2. Resolve the divisor operand width.
3. For memory divisors, compute the planned effective address and byte width without mutating quotient registers, remainder registers, modeled flags, flag-validity metadata, Program Console output, or memory-change rows.
4. Apply strict planned-read policies that are configured for the current source-run/UI settings.
5. If a strict planned-read policy fails, emit the appropriate runtime diagnostic and stop before divisor consumption and before division arithmetic.
6. If planned-read policies allow execution, perform the actual checked VM memory read through the central memory helper.
7. Apply divide-by-zero and quotient-overflow validation.
8. Commit quotient and remainder registers only after every validation step succeeds.

This planned-read integration must apply to all supported memory divisor forms:

```asm
idiv BYTE PTR [reg32]
idiv WORD PTR [reg32]
idiv DWORD PTR [reg32]
idiv SBYTE PTR [reg32]
idiv SWORD PTR [reg32]
idiv SDWORD PTR [reg32]
idiv symbol
idiv symbol[offset]
```

The implementation must not treat `idiv` as register-only in source-run policy code after parser/executor memory support is added.

The implementation must not weaken existing planned-read tests or exact rendered Simulator Messages tests to make `idiv` easier to add.

### Runtime errors

Runtime error codes:

```text
divide-by-zero
quotient-overflow
```

`divide-by-zero` behavior:

- Triggered when the divisor value is zero after reading and sign-interpreting the operand.
- Stops execution before updating quotient or remainder registers.
- Preserves source registers, source memory, modeled flag bits, flag-validity metadata, Program Console output, Simulator Messages already emitted, and memory-change rows.
- Must include source line, column, byte offset, and span length for the `idiv` instruction or the divisor operand according to the current source-run diagnostic convention.
- Rendered wording should name the divisor operand and the quotient/remainder registers that were not updated.

Suggested wording shape:

```text
IDIV divisor operand EBX evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.
```

For memory divisors, preserve source operand text where available:

```text
IDIV divisor operand divisor evaluated to zero. Division by zero is not allowed. Execution stopped before updating the quotient register EAX and remainder register EDX.
```

`quotient-overflow` behavior:

- Triggered when the signed mathematical quotient is outside the signed range of the selected quotient register.
- Stops execution before updating quotient or remainder registers.
- Preserves source registers, source memory, modeled flag bits, flag-validity metadata, Program Console output, Simulator Messages already emitted, and memory-change rows.
- Must include source line, column, byte offset, and span length for the `idiv` instruction or divisor operand according to the current source-run diagnostic convention.
- Rendered wording should name the quotient register and the quotient/remainder registers that were not updated.

Suggested wording shape:

```text
IDIV quotient is outside the signed range of quotient register EAX. Execution stopped before updating the quotient register EAX and remainder register EDX.
```

Do not use `division-overflow` for Phase 57. Milestone 56 established `quotient-overflow` as the canonical divide-family quotient overflow diagnostic.

### No-partial-mutation behavior

On any failed `idiv` execution path, the simulator must not partially mutate visible state.

Failure paths include:

- divide-by-zero;
- quotient overflow;
- invalid memory divisor address;
- memory permission failure;
- section-capacity strict validation failure;
- section-image strict validation failure;
- declared-object strict validation failure;
- strict uninitialized-read validation failure;
- address arithmetic overflow;
- unsupported executable QWORD/SQWORD memory operation.

On all failure paths:

- leave quotient and remainder registers unchanged;
- leave source registers unchanged;
- leave source memory unchanged;
- preserve modeled flag bits;
- preserve Phase 50A flag-validity metadata;
- do not write Program Console output;
- do not create memory-change rows;
- do not emit `execution-complete` after a fatal runtime diagnostic.

### Flag behavior

Real x86 leaves flags undefined after `IDIV`. MASM32 Educational Mode uses deterministic preservation for currently modeled flags.

Phase 57 behavior:

- Preserve all currently modeled flag bits as a deterministic educational policy for real x86 undefined flags.
- Preserve Phase 50A flag-validity metadata exactly on successful `idiv`.
- Preserve Phase 50A flag-validity metadata exactly on failed `idiv`.
- Do not mark `CF`, `ZF`, `SF`, or `OF` valid or invalid as a side effect of `idiv`.
- Do not emit an undefined-modeled-flag producer warning for `idiv` in this phase unless a later reviewed phase explicitly changes multiply/divide undefined-flag policy.

### Required tests

Parser and operand-shape tests:

- Accept `idiv reg8`, `idiv reg16`, and `idiv reg32`.
- Accept `idiv BYTE PTR [reg32]`, `idiv WORD PTR [reg32]`, and `idiv DWORD PTR [reg32]`.
- Accept `idiv SBYTE PTR [reg32]`, `idiv SWORD PTR [reg32]`, and `idiv SDWORD PTR [reg32]`.
- Accept direct typed symbol divisors.
- Accept typed symbol-offset divisors.
- Accept readable `.CONST` divisor sources.
- Reject `idiv 5` with `invalid-instruction-operands`.
- Reject `idiv eax, ebx` with `invalid-instruction-operands`.
- Reject `idiv` with no operand.
- Reject `idiv eax, ebx, ecx` with `invalid-instruction-operands`.
- Reject `idiv [eax]` with `ambiguous-memory-width`.
- Reject `idiv QWORD PTR q` and `idiv SQWORD PTR q` with the same stable diagnostic code currently used by executable QWORD/SQWORD memory-operation rejection in MASM32 Educational Mode.

Runtime success tests:

- 32-bit positive dividend / positive divisor: `100 / 7` gives quotient `14`, remainder `2`.
- 32-bit negative dividend / positive divisor: `-100 / 7` gives quotient `-14`, remainder `-2`.
- 32-bit positive dividend / negative divisor: `100 / -7` gives quotient `-14`, remainder `2`.
- 32-bit negative dividend / negative divisor: `-100 / -7` gives quotient `14`, remainder `-2`.
- Tests prove quotient truncates toward zero.
- Tests prove the remainder sign follows the dividend.
- 16-bit signed division success.
- 8-bit signed division success.
- Stale/significant high-half test proving `IDIV r/m32` uses signed `EDX:EAX`, not only `EAX`.
- Memory divisor read through checked memory helpers.
- Memory divisor read from `.CONST`.
- Declaration-signedness independence:
  - A divisor declared as `BYTE 0FFh` must be sign-interpreted as `-1` when used by `idiv` at 8-bit divisor width.
  - A divisor declared as `SBYTE -1` must produce the same divisor value and the same quotient/remainder result as the raw-byte-equivalent `BYTE 0FFh` case.
  - A divisor declared as `WORD 0FFFFh` must be sign-interpreted as `-1` when used by `idiv` at 16-bit divisor width.
  - A divisor declared as `SWORD -1` must produce the same divisor value and the same quotient/remainder result as the raw-word-equivalent `WORD 0FFFFh` case.
  - A divisor declared as `DWORD 0FFFFFFFFh` must be sign-interpreted as `-1` when used by `idiv` at 32-bit divisor width.
  - A divisor declared as `SDWORD -1` must produce the same divisor value and the same quotient/remainder result as the raw-dword-equivalent `DWORD 0FFFFFFFFh` case.
  - These tests must prove that IDIV sign interpretation comes from the instruction and resolved operand width, not from declaration signedness alone.
  - These tests must not change ordinary `mov` behavior. Ordinary `mov` from signed memory still reads raw bytes at the resolved width and does not implicitly sign-extend.

  Representative safe 8-bit equivalence fixture:

  ```asm
  .data
  u BYTE 0FFh
  s SBYTE -1

  .code
  main PROC
      mov ax, 5
      idiv u

      mov bx, ax

      mov ax, 5
      idiv s
  main ENDP
  END main
  ```

  Expected behavior:

  ```text
  After `idiv u`: AL = FBh / signed -5, AH = 00h / signed 0.
  After `idiv s`: AL = FBh / signed -5, AH = 00h / signed 0.
  ```

  The test may preserve the first result in another register or split the two cases into separate fixtures if that produces cleaner assertions.
- Memory divisor reads do not create memory-change rows.
- Successful `idiv` preserves pre-set modeled flag bits `CF`, `ZF`, `SF`, and `OF`.
- Successful `idiv` preserves Phase 50A flag-validity metadata.
- Successful `idiv` preserves already-invalid Phase 50A flag-validity metadata:
  - Before executing a successful `idiv`, create an invalid modeled-flag metadata state using an already implemented undefined-flag producer, such as a shift or rotate count that marks `OF` or `CF` architecturally invalid.
  - Then execute an `idiv` that succeeds.
  - Verify that the deterministic flag bits are unchanged by `idiv`.
  - Verify that every modeled flag that was valid before `idiv` remains valid after `idiv`.
  - Verify that every modeled flag that was invalid before `idiv` remains invalid after `idiv`.
  - Verify that undefined-origin metadata is preserved for invalid flags, including producer mnemonic, producer diagnostic code, producer source line, producer source column, byte offset, and span length where available.
  - Verify that `idiv` does not become the producer mnemonic for any undefined flag metadata in this phase.
  - If flag-validity metadata is not exposed through normal UI/source-run output, this requirement must be tested through native executor tests or an existing test-only inspection path. Do not add new user-visible flag-validity UI as part of Phase 57 merely to test this behavior.

Runtime error tests:

- Divide by zero from register.
- Divide by zero from memory.
- 8-bit quotient overflow.
- 16-bit quotient overflow.
- 32-bit quotient overflow, including `-2147483648 / -1`.
- Failed memory divisor read stops before quotient/remainder mutation.
- Strict uninitialized-read memory divisor diagnostic stops before divisor consumption and before quotient/remainder mutation.
- Strict planned-read validation stops before division.
- Divide-error tests prove quotient/remainder registers remain unchanged.
- Divide-error tests prove modeled flag bits remain unchanged.
- Divide-error tests prove Phase 50A flag-validity metadata remains unchanged.
- Failed `idiv` preserves already-invalid Phase 50A flag-validity metadata:
  - Before executing a failing `idiv` path, create an invalid modeled-flag metadata state using an already implemented undefined-flag producer, such as a shift or rotate count that marks `OF` or `CF` architecturally invalid.
  - Then execute an `idiv` that fails with `divide-by-zero`.
  - Repeat for an `idiv` that fails with `quotient-overflow`.
  - Verify that the deterministic flag bits are unchanged by the failing `idiv`.
  - Verify that every modeled flag that was valid before the failing `idiv` remains valid afterward.
  - Verify that every modeled flag that was invalid before the failing `idiv` remains invalid afterward.
  - Verify that undefined-origin metadata is preserved for invalid flags, including producer mnemonic, producer diagnostic code, producer source line, producer source column, byte offset, and span length where available.
  - Verify that the fatal IDIV diagnostic does not clear, rewrite, validate, or replace unrelated pre-existing flag metadata.
  - If flag-validity metadata is not exposed through normal UI/source-run output, this requirement must be tested through native executor tests or an existing test-only inspection path. Do not add new user-visible flag-validity UI as part of Phase 57 merely to test this behavior.
- Divide-error tests prove no memory-change rows are produced.

Additional planned-read integration tests:

1. **Strict declared-object validation before mutation**

   Program shape:

   ```asm
   .data
   first  DWORD 1
   second DWORD 2

   .code
   main PROC
       mov esi, OFFSET first
       mov edx, 0
       mov eax, 10
       idiv DWORD PTR [esi + 1]
   main ENDP
   END main
   ```

   The exact fixture may use a different offset, declaration, or policy setup, but it must prove that the final memory divisor read passes mandatory region validation and then fails the selected strict declared-object planned-read policy before quotient or remainder registers are mutated. The sample shape intentionally uses an address range that starts inside `first` and crosses into adjacent data, so future assistants must not replace it with an invalid-address fixture.

2. **Strict section-image or section-capacity validation before mutation**

   Add at least one fixture where the final memory divisor range passes mandatory VM region validation but fails the selected section policy in strict mode. The diagnostic must be the selected section-policy diagnostic, not a generic memory-read failure and not a divide diagnostic.

3. **Strict uninitialized-read stop**

   Program shape:

   ```asm
   .DATA?
   divisor SDWORD ?

   .code
   main PROC
       mov edx, 0
       mov eax, 10
       idiv divisor
   main ENDP
   END main
   ```

   In strict uninitialized-read mode, execution must stop before consuming the divisor and before updating quotient or remainder registers.

4. **Default uninitialized-read warning composition**

   Use the same uninitialized divisor in default warning mode. The simulator must emit the `uninitialized-read` warning and then proceed according to the deterministic zero-filled divisor value. If that value is zero, the next fatal diagnostic should be `divide-by-zero`, and no quotient or remainder mutation should occur.

5. **No memory-change rows**

   Successful and failed memory-divisor `idiv` programs must not create successful memory-change rows, because `idiv` reads source memory and writes only implicit quotient/remainder registers on success.

6. **Rendered Simulator Messages**

   Every new or affected diagnostic path must have an exact rendered Simulator Messages test through the native diagnostic JSON producer and Node formatter harness.

Rendered Simulator Messages tests:

- Exact rendered `divide-by-zero` message for a register divisor.
- Exact rendered `divide-by-zero` message for a memory divisor, preserving operand text where available.
- Exact rendered `quotient-overflow` message.
- Exact rendered ambiguous-memory-width diagnostic.
- Exact rendered unsupported executable QWORD/SQWORD diagnostic if the current formatter has a stable message for that path.
- Verify fatal `idiv` diagnostics do not render `execution-complete`.

### Acceptance program

```asm
.code
main PROC
    mov eax, -100
    cdq
    mov ebx, 7
    idiv ebx
main ENDP
END main
```

Expected semantic result:

```text
EAX raw 32-bit value = FFFFFFF2h
EAX unsigned value   = 4294967282
EAX signed value     = -14

EDX raw 32-bit value = FFFFFFFEh
EDX unsigned value   = 4294967294
EDX signed value     = -2
```

If this acceptance program is checked through the current rendered final-register formatter, the rendered output must use the current known-width register display contract rather than an older unsigned-only format. The formatter-owned spacing may vary, but the displayed 32-bit `EAX` and `EDX` values must include:

- hexadecimal raw storage;
- unsigned decimal interpretation;
- signed decimal interpretation.

Do not add or change IDIV runtime semantics merely to satisfy a stale display string. If a test failure is only a formatter string mismatch, first verify whether the expected string predates Phase 52A - Signed Register and Memory Value Display.

This acceptance program relies on previously implemented `cdq` behavior. If `cdq` is unavailable in a narrow executor-only test harness, initialize `EDX:EAX` directly through supported setup helpers or use a 16-bit or 8-bit IDIV fixture instead. Do not implement, modify, or broaden `cdq` as part of Phase 57.

### Phase 57 milestone report requirements

The Phase 57 milestone report must include both milestone/status labels, even if they match:

```text
Repository/archive milestone:
Phase 57 - Signed IDIV

Runtime/source-run MASM behavior phase:
Phase 57 - Signed IDIV
```

The report must explicitly state:

- that Phase 57 is a runtime behavior milestone;
- that Phase 56A - Test Runner Decomposition and Assistant Verification Ergonomics was a maintenance/test-runner milestone and did not advance runtime/source-run MASM behavior metadata;
- that Phase 56B - User-Facing Diagnostic Wording Cleanup was a diagnostic/current-status wording maintenance milestone and did not advance runtime/source-run MASM behavior metadata;
- that Phase 57 intentionally advances runtime/source-run MASM behavior metadata after `idiv` implementation;
- which status surfaces were updated;
- which metadata/status tests were added or updated;
- whether the aggregate runner completed;
- which focused groups passed if the aggregate timed out or output was truncated in an assistant/container environment;
- whether browser/Wasm smoke testing was run or skipped, and why;
- that no future features were implemented.

The report must not say `All implemented milestone tests passed.` unless the aggregate command completed and returned the final success status in that environment. If focused groups passed after aggregate timeout, the report must use the guide's focused-verification wording.

### Non-goals and future work not implemented in this phase

Phase 57 must not implement:

- unsigned `div` changes;
- multiplication changes;
- `cmp`;
- labels, `jmp`, conditional jumps, or `loop`;
- `lea`;
- `push`, `pop`, `call`, `ret`, `leave`, or stack behavior;
- procedure metadata, stack frames, `LOCAL`, `PROTO`, `INVOKE`, or `ADDR`;
- Irvine32 routine expansion beyond already implemented behavior;
- Program Console input/output routines;
- scaled-index addressing;
- executable QWORD/SQWORD memory operations;
- Windows API behavior;
- PE loading;
- object linking;
- host include loading;
- macro expansion.

---


## 61-CORR1. Phase 57-CORR1 - `.CONST` Cross-Region Diagnostic Clarification

### Goal

Clarify and test the Level 1 runtime diagnostic used when one simulated memory access crosses independent VM memory-region boundaries and the requested byte range intersects protected `.CONST` storage.

This is a corrective diagnostic-quality phase after Phase 57 - Signed IDIV and before Phase 57-CORR2 - Compact Negative Register-Indirect Displacement Correction and Phase 57A - README Landing Page Cleanup.

This phase does not add MASM syntax, parser acceptance behavior, VM memory semantics, instruction behavior, layout behavior, browser controls, or runtime/source-run MASM behavior metadata. It only makes the existing fatal cross-region/protected-region failure more precise, better structured, and better tested.

Repository/archive milestone:

```text
Phase 57-CORR1 - .CONST Cross-Region Diagnostic Clarification
```

Runtime/source-run MASM behavior phase:

```text
Phase 57 - Signed IDIV
```

Status interpretation:

The repository/archive milestone is newer than the runtime/source-run MASM behavior phase because this corrective phase does not add MASM syntax, parser behavior, VM instruction behavior, new executable language features, browser controls, or runtime/source-run phase metadata. Do not update source-run JSON phase fields, protocol phase values, supported-syntax runtime behavior phase, or tests that assert runtime phase values merely because Phase 57-CORR1 is completed.

### Required behavior

A single source-level memory access must remain wholly contained in one suitable VM memory region.

If a memory access final byte range is not wholly contained in one VM memory region, the access is a mandatory Level 1 runtime failure. The simulator must not split, stitch, or partially execute the access across independent VM memory regions.

When the final byte range crosses independent VM memory-region boundaries and the active layout metadata shows that the range intersects protected `.CONST` storage, the diagnostic code must be:

```text
region-boundary-crossing
```

For this phase, **intersects**, **overlaps**, and **crosses/overlaps** all mean that the requested final inclusive byte range shares at least one byte with the `.CONST` byte range. The implementation must use final byte ranges, not only static symbols or the access starting address, to classify the diagnostic.

The rendered Simulator Message must use this shape:

```text
Cross-region memory <read|write> at <address> for <N> bytes. The memory address range <start>..<end> crosses/overlaps a protected memory region, .CONST, that starts at <runtime .CONST start address>. This is not allowed; program stopped before access.
```

The `<runtime .CONST start address>` value must be dynamic. It must be taken from the active VM layout metadata, not hardcoded to the fixed-layout default address. The diagnostic must remain correct under:

- fixed educational layout;
- automatic deterministic layout;
- seeded randomized layout;
- fresh randomized layout, once available.

The phrase `protected memory region` is intentional for this diagnostic, but it must be interpreted precisely:

- `.CONST` is currently protected against writes because it is read-only.
- `.CONST` is not generally protected against reads.
- A wholly-contained read from `.CONST` remains allowed unless another mandatory or enabled strict validation rejects it.
- A cross-region read whose requested range intersects `.CONST` reports `region-boundary-crossing` because one memory access cannot span independent VM regions, not because `.CONST` is unreadable.

Direct or wholly-contained writes whose final byte range overlaps `.CONST` remain permission failures. Their diagnostic code remains:

```text
permission-denied
```

Wholly-contained reads from `.CONST` remain allowed unless another mandatory validation or enabled strict validation rejects the access.

Cross-region accesses that do not intersect `.CONST` or another known protected region remain ordinary Level 1 region/range failures. This phase does not require reclassifying every invalid address, invalid region, or range-containment diagnostic.

### Example target diagnostic

For a fixed-layout fixture where `.CONST` starts at `00600000h`:

```asm
.CONST
x BYTE 1

.code
main PROC
    mov eax, OFFSET x
    mov ebx, [eax]
    sub eax, 2
    mov DWORD PTR [eax], 0FFFFFFFFh
main ENDP
END main
```

The failing write attempts this range:

```text
005FFFFEh..00600001h
```

Expected rendered Simulator Message:

```text
[runtime-error] region-boundary-crossing line 9: Cross-region memory write at 005FFFFEh for 4 bytes. The memory address range 005FFFFEh..00600001h crosses/overlaps a protected memory region, .CONST, that starts at 00600000h. This is not allowed; program stopped before access.
```

The fixed address values in this example are example output for the fixed educational layout. The implementation must not hardcode those addresses. Tests that run with other layout modes must derive the expected address values from that layout.

### Required tests

Add or update structured source-run tests and exact rendered Simulator Messages tests for all of the following cases.

1. Cross-region `.CONST` write overlap:

```asm
.CONST
x BYTE 1

.code
main PROC
    mov eax, OFFSET x
    mov ebx, [eax]
    sub eax, 2
    mov DWORD PTR [eax], 0FFFFFFFFh
main ENDP
END main
```

Required result:

- diagnostic kind: `runtime-error`;
- diagnostic code: `region-boundary-crossing`;
- message identifies `write`;
- message includes the attempted start address;
- message includes access width `4 bytes`;
- message includes the computed inclusive byte range;
- message identifies `.CONST`;
- message includes the runtime `.CONST` start address;
- no successful memory-change row is emitted for the failing write;
- no `execution-complete` message appears after the fatal diagnostic;
- registers, flags, flag-validity metadata, memory, Program Console output, and memory-change rows remain unchanged by the failing instruction.

2. Cross-region `.CONST` read overlap:

```asm
.CONST
x BYTE 1

.code
main PROC
    mov eax, OFFSET x
    sub eax, 2
    mov ebx, DWORD PTR [eax]
main ENDP
END main
```

Required result:

- diagnostic kind: `runtime-error`;
- diagnostic code: `region-boundary-crossing`;
- message identifies `read`;
- message includes the attempted start address;
- message includes access width `4 bytes`;
- message includes the computed inclusive byte range;
- message identifies `.CONST`;
- message includes the runtime `.CONST` start address;
- the read value is not consumed;
- no `execution-complete` message appears after the fatal diagnostic;
- the diagnostic must not say or imply that `.CONST` is generally unreadable.

3. Direct or wholly-contained `.CONST` write:

```asm
.CONST
x BYTE 1

.code
main PROC
    mov eax, OFFSET x
    mov DWORD PTR [eax], 0FFFFFFFFh
main ENDP
END main
```

Required result:

- diagnostic kind: `runtime-error`;
- diagnostic code: `permission-denied`;
- diagnostic remains a `.CONST` read-only/permission failure;
- diagnostic is not `region-boundary-crossing` because the final byte range is handled as a wholly-contained permission failure rather than a cross-region boundary failure;
- no successful memory-change row is emitted;
- no `execution-complete` message appears after the fatal diagnostic.

4. Cross-region write that does not intersect `.CONST` or another protected region:

Required result:

- remains the ordinary Level 1 region/range diagnostic for the existing invalid-address or invalid-region path;
- does not use `region-boundary-crossing` unless the crossed range intersects a known protected region;
- does not mention `.CONST`;
- no successful memory-change row is emitted;
- no `execution-complete` message appears after the fatal diagnostic.

5. Phase/status regression:

Required result:

- repository/archive milestone may advance to Phase 57-CORR1 in README or status documentation;
- runtime/source-run MASM behavior phase remains Phase 57 - Signed IDIV;
- source-run JSON phase fields remain `57`;
- worker/protocol phase values remain `57`;
- tests that assert runtime/source-run phase metadata must not be rewritten to `57-CORR1`.

6. Layout metadata regression:

Required result:

- the protected-region start address in diagnostics comes from runtime region metadata;
- the implementation does not hardcode fixed-layout addresses;
- if existing test infrastructure can exercise automatic or randomized layout, include at least one test proving the message uses the active layout's `.CONST` base address;
- if current test infrastructure cannot exercise those modes for this diagnostic, state that the implementation uses dynamic metadata and leave broader layout-mode coverage to the phase that owns the unavailable layout mode.

### Required implementation notes

- Add `region-boundary-crossing` as a structured diagnostic code only for the cross-region/protected-region-overlap case approved by this phase.
- Do not reclassify unrelated invalid addresses merely to use the new code.
- Do not reclassify direct or wholly-contained `.CONST` writes away from `permission-denied`.
- Do not reject ordinary wholly-contained reads from `.CONST`.
- Preserve source line, column, byte offset, and span length for the failing memory operand.
- Preserve Program Console and Simulator Messages as separate streams.
- Preserve all no-partial-mutation guarantees.
- Keep the core implementation C99.
- Add or update exact rendered Simulator Messages tests for the new wording.
- Update documentation/status only where needed to describe the corrective diagnostic behavior and repository/runtime status split.

### Historical note for `.code` protected-region diagnostics

During Phase 57-CORR1 this item was future-owned. Phase 57L implements `.code` memory-access denial, so cross-region accesses that intersect `.code` reuse the `region-boundary-crossing` diagnostic shape.

The Phase 57L `.code` diagnostic must:

- identify the no-access `.CODE/_TEXT` region in rendered diagnostic copy without making `_TEXT` an addressable symbol alias;
- use the runtime `.code` base address from active layout metadata;
- avoid hardcoded fixed-layout addresses;
- work under fixed educational layout, automatic deterministic layout, seeded randomized layout, and fresh randomized layout;
- preserve no-partial-mutation behavior;
- include exact structured diagnostics and rendered Simulator Messages tests.

This item remained future-owned during Phase 57-CORR1 and was intentionally not implemented there. Phase 57L is the implementation owner for `.code` memory-access denial.

### Non-goals

Phase 57-CORR1 must not implement:

- new MASM syntax;
- new instructions;
- parser acceptance changes;
- new memory-layout modes;
- memory-region merging;
- section-capacity behavior changes;
- section-image behavior changes;
- declared-object validation changes;
- uninitialized-read behavior changes;
- browser diagnostic settings;
- `.CONST` writability changes;
- `.code` access-denial behavior;
- stack behavior;
- control flow;
- Irvine32 routine expansion;
- Program Console input/output routines;
- executable QWORD/SQWORD memory operations;
- Windows API behavior;
- PE loading;
- object linking;
- host include loading;
- macro expansion;
- runtime/source-run MASM behavior phase advancement beyond Phase 57.

### Acceptance criteria

Phase 57-CORR1 is complete only when all of the following are true:

- cross-region `.CONST` write overlap reports `region-boundary-crossing`;
- cross-region `.CONST` read overlap reports `region-boundary-crossing`;
- both cross-region `.CONST` diagnostics use the canonical protected-region message shape;
- the protected-region start address is dynamic and comes from active layout metadata;
- direct or wholly-contained `.CONST` writes still report `permission-denied`;
- wholly-contained `.CONST` reads remain allowed when otherwise valid;
- non-`.CONST` cross-region failures remain ordinary Level 1 region/range diagnostics unless another protected region is involved;
- failed accesses remain fatal and non-mutating;
- no memory access is stitched across independent VM regions;
- no successful memory-change row is emitted for the failing instruction;
- no `execution-complete` message appears after the fatal diagnostic;
- structured source-run tests cover the new diagnostic code;
- exact rendered Simulator Messages tests cover the new wording;
- current status documentation distinguishes repository/archive milestone from runtime/source-run MASM behavior phase;
- runtime/source-run phase metadata remains Phase 57;
- Phase 57-CORR2, Phase 57A through Phase 57T, Phase 58, and later phases are not renumbered.

---

## 61-CORR2. Phase 57-CORR2 - Compact Negative Register-Indirect Displacement Correction

### Goal

Correct the register-indirect displacement parser so compact negative displacement spelling works for already-supported simple register-derived memory expressions.

This phase fixes a whitespace-sensitive parser inconsistency. It must not implement advanced addressing.

The motivating failing source form is:

```asm
mov DWORD PTR [eax-4], 10
```

The currently working equivalent form is:

```asm
mov DWORD PTR [eax - 4], 10
```

Both forms are the same simple base-minus-constant displacement. They must parse and execute the same way.

### Behavior category

Corrective parser/source-run acceptance fix for existing simple displacement syntax.

Repository/archive milestone advances to:

```text
Phase 57-CORR2 - Compact Negative Register-Indirect Displacement Correction
```

Runtime/source-run MASM behavior phase remains:

```text
Phase 57 - Signed IDIV
```

### Status interpretation

Phase 57-CORR2 is a corrective parser-quality phase after Phase 57-CORR1 and before Phase 57A.

It advances repository/archive status because it changes parser acceptance for a currently inconsistent spelling of an already-supported memory operand form.

It does not advance runtime/source-run MASM behavior phase metadata because it does not add a new instruction family, new runtime semantic family, new diagnostic policy, new browser setting, new source-run JSON field, or new VM execution feature.

Do not update source-run JSON phase fields, worker/protocol phase values, browser runtime-status text, README runtime-status text, or supported-syntax runtime phase text merely because this phase is completed. Keep runtime/source-run MASM behavior phase metadata at Phase 57 - Signed IDIV unless a later accepted phase explicitly owns metadata advancement.

### Source-of-truth context

Phase 5 - Lexer supports signed numeric tokens such as:

```asm
-42
-0x2A
-2Ah
```

Phase 11 - Register-Indirect Memory Operands supports simple register displacement forms such as:

```asm
[esi + 4]
[esi - 4]
```

Phase 52 - LEA reuses the currently supported address-expression subset and supports effective-address source forms equivalent to:

```asm
lea reg32, [reg32 + constant]
lea reg32, [reg32 - constant]
```

Therefore compact spelling such as:

```asm
[eax-4]
```

must not be misclassified as advanced addressing. It is the same simple register-minus-constant form as:

```asm
[eax - 4]
```

### Problem being corrected

The lexer may tokenize compact negative displacement text as one signed numeric token:

```text
NUMBER(-4)
```

instead of two tokens:

```text
MINUS
NUMBER(4)
```

The existing register-indirect parser path accepts the explicit-minus-token form but rejects the signed-numeric-token form after a register base.

That creates incorrect whitespace-sensitive behavior:

```asm
[eax - 4]   ; accepted
[eax-4]     ; rejected
```

This phase removes that inconsistency.

### Required behavior

The parser must accept compact negative numeric displacement tokens after a supported 32-bit register base inside brackets.

Accepted examples must include:

```asm
mov DWORD PTR [eax-4], 10
mov DWORD PTR [esi-8], 10
mov DWORD PTR [ebp-10h], 10
mov DWORD PTR [ebp-0x10], 10
mov eax, DWORD PTR [ecx-4]
mov ax, WORD PTR [edx-2]
mov al, BYTE PTR [edi-1]
lea eax, [ebx-4]
```

Each accepted compact form must be semantically equivalent to the corresponding spaced form:

```asm
mov DWORD PTR [eax - 4], 10
mov DWORD PTR [esi - 8], 10
mov DWORD PTR [ebp - 10h], 10
mov DWORD PTR [ebp - 0x10], 10
mov eax, DWORD PTR [ecx - 4]
mov ax, WORD PTR [edx - 2]
mov al, BYTE PTR [edi - 1]
lea eax, [ebx - 4]
```

The displacement remains a byte displacement. It is not an element index.

The final effective address for memory-reading or memory-writing instructions remains subject to all existing runtime memory validation:

- address arithmetic overflow checks;
- region containment checks;
- permission checks;
- `.CONST` write protection;
- Phase 57-CORR1 `region-boundary-crossing` behavior;
- optional section-capacity, section-image, declared-object, and uninitialized-read policies where applicable.

`lea` remains address-only. It must compute the effective address but must not perform a simulated memory read or write, must not emit memory-read diagnostics, and must not create memory-change rows.

### Required parser behavior

Update the shared register-displacement parsing path so that, after a valid 32-bit register base inside brackets, it accepts these suffix shapes:

```text
]
+ numeric-displacement ]
- numeric-displacement ]
signed-negative-numeric-displacement ]
```

The signed-negative-numeric-displacement path is required for compact forms such as:

```asm
[eax-4]
[eax-10h]
[eax-0x10]
```

The signed-negative-numeric-displacement path must use the same signed displacement range validation as the existing spaced-minus path.

After consuming a displacement token, the parser must require the closing bracket. If any additional token appears before the closing bracket, the parser must reject the operand rather than accepting a prefix.

For example:

```asm
mov eax, DWORD PTR [eax-4*2]
```

must not be accepted as:

```asm
mov eax, DWORD PTR [eax-4]
```

The `*2` makes the source an unsupported advanced effective-address expression.

### Lexer constraint

Do not change the lexer merely to force `-4` into separate `-` and `4` tokens.

The lexer's signed numeric token behavior is valid project behavior. This phase fixes the parser so it accepts a signed numeric token in the specific register-displacement suffix position where that token is semantically equivalent to an explicit minus token followed by a positive displacement.

### Unsupported forms that must remain rejected

This phase must not implement scaled-index addressing, base-plus-index addressing, SIB addressing, or general effective-address arithmetic.

The following forms must remain rejected:

```asm
mov eax, DWORD PTR [eax*4]
mov eax, DWORD PTR [eax * 4]
mov eax, DWORD PTR [eax+ebx]
mov eax, DWORD PTR [eax + ebx]
mov eax, DWORD PTR [eax+ebx*4]
mov eax, DWORD PTR [eax + ebx * 4]
mov eax, DWORD PTR [eax-4*2]
mov eax, DWORD PTR [eax - 4 * 2]
mov eax, DWORD PTR [eax-(4)]
mov eax, DWORD PTR [eax - (4)]
```

The following malformed forms must remain rejected unless a later expression phase deliberately defines them:

```asm
mov eax, DWORD PTR [eax--4]
mov eax, DWORD PTR [eax+-4]
mov eax, DWORD PTR [eax-]
mov eax, DWORD PTR [eax+]
```

Do not reinterpret any of these as accepted simple displacements.

### Implementation guidance

The safest implementation is to update the shared register-displacement helper rather than patching each instruction parser independently.

After parsing a valid register base and before reporting the current "Expected ], + displacement, or - displacement after register memory base" diagnostic, add handling for a signed negative numeric token.

Conceptual behavior:

```text
if next token is right bracket:
    displacement = 0
    accept

else if next token is plus token:
    require non-negative numeric displacement
    displacement = +value
    require right bracket
    accept

else if next token is minus token:
    require non-negative numeric displacement
    displacement = -value
    require right bracket
    accept

else if next token is signed negative numeric token:
    displacement = signed token value
    require right bracket
    accept

else:
    emit existing expected-suffix diagnostic or existing unsupported-addressing diagnostic
```

The implementation must preserve source-location metadata for diagnostics. If a compact negative displacement is malformed because extra unsupported address tokens follow it, the diagnostic span should identify the unsupported trailing token or the smallest useful offending address-expression span according to the existing parser diagnostic conventions.

### Required native parser tests

Add accepted parser tests proving compact negative forms parse with the same displacement as spaced equivalents:

```asm
mov DWORD PTR [eax-4], 10
mov DWORD PTR [esi-8], 10
mov DWORD PTR [ebp-10h], 10
mov DWORD PTR [ebp-0x10], 10
mov eax, DWORD PTR [ecx-4]
mov ax, WORD PTR [edx-2]
mov al, BYTE PTR [edi-1]
lea eax, [ebx-4]
```

Add regression parser tests proving existing accepted forms still parse:

```asm
mov DWORD PTR [eax], 10
mov DWORD PTR [eax+4], 10
mov DWORD PTR [eax + 4], 10
mov DWORD PTR [eax - 4], 10
lea eax, [ebx + 4]
lea eax, [ebx - 4]
```

Add parser rejection tests proving advanced and malformed forms remain rejected:

```asm
mov eax, DWORD PTR [eax*4]
mov eax, DWORD PTR [eax * 4]
mov eax, DWORD PTR [eax+ebx]
mov eax, DWORD PTR [eax + ebx]
mov eax, DWORD PTR [eax+ebx*4]
mov eax, DWORD PTR [eax + ebx * 4]
mov eax, DWORD PTR [eax-4*2]
mov eax, DWORD PTR [eax - 4 * 2]
mov eax, DWORD PTR [eax-(4)]
mov eax, DWORD PTR [eax--4]
mov eax, DWORD PTR [eax+-4]
```

### Required source-run tests

Add source-run success coverage for compact negative displacement write:

```asm
.data
x DWORD 0, 0

.code
main PROC
    mov eax, OFFSET x
    add eax, 4
    mov DWORD PTR [eax-4], 10
    mov ebx, x
main ENDP
END main
```

Expected behavior:

```text
EBX = 0000000Ah
x + 0 DWORD changes from 00000000h to 0000000Ah
execution-complete is emitted
```

Add source-run success coverage for compact negative displacement read:

```asm
.data
x DWORD 10, 20

.code
main PROC
    mov eax, OFFSET x
    add eax, 4
    mov ebx, DWORD PTR [eax-4]
main ENDP
END main
```

Expected behavior:

```text
EBX = 0000000Ah
execution-complete is emitted
```

Add source-run success coverage for compact negative displacement with `lea`:

```asm
.data
x DWORD 0, 0

.code
main PROC
    mov ebx, OFFSET x
    add ebx, 4
    lea eax, [ebx-4]
main ENDP
END main
```

Expected behavior:

```text
EAX equals OFFSET x
LEA emits no memory-read diagnostics
LEA emits no memory-change rows
execution-complete is emitted
```

Add source-run rejection coverage proving unsupported advanced addressing remains rejected:

```asm
.data
x DWORD 0, 0

.code
main PROC
    mov eax, OFFSET x
    mov ebx, DWORD PTR [eax-4*2]
main ENDP
END main
```

Expected behavior:

```text
An assembly diagnostic is emitted for unsupported advanced/scaled effective-address syntax.
No execution occurs.
No Program Console output is produced.
No memory-change row is produced.
```

### Required rendered Simulator Messages tests

Add exact rendered Simulator Messages tests for:

- compact negative displacement write success, expecting only the normal execution-complete message;
- compact negative displacement read success, expecting only the normal execution-complete message;
- compact negative `lea` success, expecting only the normal execution-complete message;
- malformed advanced expression such as `[eax-4*2]`, expecting the existing unsupported advanced/scaled-addressing diagnostic.

The malformed-address diagnostic must not say that simple subtraction in memory addresses is unsupported. The issue is advanced effective-address arithmetic, not subtraction itself.

### Required documentation updates

Update `docs/SUPPORTED_SYNTAX.md` to list both spaced and compact simple register-displacement forms as accepted:

```asm
BYTE PTR [eax + 1]
BYTE PTR [eax+1]
BYTE PTR [eax - 1]
BYTE PTR [eax-1]

WORD PTR [eax + 2]
WORD PTR [eax+2]
WORD PTR [eax - 2]
WORD PTR [eax-2]

DWORD PTR [eax + 4]
DWORD PTR [eax+4]
DWORD PTR [eax - 4]
DWORD PTR [eax-4]
```

The documentation must state that these are simple base-plus-or-minus-constant byte displacements.

The documentation must also state that scaled-index addressing, base-plus-index addressing, and general bracket arithmetic remain unsupported until future phases explicitly implement them.

### Required milestone report content

The milestone report for Phase 57-CORR2 must include:

- the user-observed failing form:

```asm
mov DWORD PTR [eax-4], 10
```

- the already-working equivalent form:

```asm
mov DWORD PTR [eax - 4], 10
```

- confirmation that the fix is parser acceptance for an existing simple displacement form;
- confirmation that no advanced addressing was implemented;
- confirmation that runtime/source-run MASM behavior phase metadata remains Phase 57 - Signed IDIV;
- exact tests run, including aggregate and focused groups if applicable;
- whether browser/Wasm smoke testing was skipped because `emcc` was unavailable.

### Acceptance criteria

- Compact negative register-displacement memory operands parse and execute anywhere the equivalent spaced form already parses and executes.
- Compact negative `lea` effective-address operands parse and execute anywhere the equivalent spaced form already parses and executes.
- Existing positive, zero-displacement, and spaced negative forms still work.
- Unsupported advanced addressing remains rejected.
- Source-run tests prove the fix through user-visible source, not only through parser internals.
- Rendered Simulator Messages tests prove the user-facing success and rejection paths.
- Runtime/source-run MASM behavior phase metadata remains Phase 57 - Signed IDIV.
- No scaled-index, base-plus-index, SIB, parenthesized effective-address arithmetic, or general expression parsing is implemented.

### Non-goals

Do not implement:

- scaled-index addressing;
- base-plus-index addressing;
- SIB addressing;
- parenthesized effective-address expressions;
- register-plus-register effective addresses;
- general arithmetic inside memory brackets;
- new instruction behavior;
- new memory validation policy;
- QWORD/SQWORD executable memory operations;
- stack behavior;
- control-flow behavior;
- procedure behavior;
- Irvine32 routine expansion;
- `.code` memory-access diagnostics;
- segment/group symbol diagnostics;
- host include diagnostics;
- `INCLUDELIB` diagnostics;
- README landing-page cleanup.


---

## 61A. Phase 57A - README Landing Page Cleanup

### Goal

Turn `README.md` into a concise project landing page.

This phase addresses README clutter only. It must not change source code, MASM syntax, parser behavior, VM behavior, executor behavior, Wasm APIs, browser UI behavior, diagnostic codes, diagnostic policies, source-run JSON fields, rendered Simulator Messages wording, or runtime/source-run MASM behavior metadata.

### Behavior category

Documentation and repository hygiene only.

Repository/archive milestone advances to:

```text
Phase 57A - README Landing Page Cleanup
```

Runtime/source-run MASM behavior phase remains:

```text
Phase 57 - Signed IDIV
```

### Tasks

1. Rewrite `README.md` as a short project entry point.

2. Keep the README focused on:
   - project name;
   - one-paragraph project description;
   - current repository/archive milestone;
   - current runtime/source-run MASM behavior phase;
   - concise current simulator scope;
   - how to serve the website locally;
   - how to run the aggregate test command;
   - how to run focused test groups or where to find focused test group documentation;
   - how to build with checked-in scripts, command files, or the Visual Studio solution if present;
   - links to detailed documentation.

3. Remove the large milestone-accomplishment wall from immediately after the title.

4. Remove large milestone-accomplishment lists from the current-scope area.

5. Replace those large sections with short status text and links to the later milestone-history document.

6. Keep a short project-boundary section explaining that the simulator is:
   - a static browser application;
   - a C99 MASM-like parser plus internal VM compiled to WebAssembly;
   - an educational MASM32/Irvine32-style console simulator;
   - not a full MASM compiler;
   - not a full x86 emulator;
   - not a Windows emulator;
   - not a PE loader/linker;
   - not a WinAPI simulator.

7. Keep only basic build/test guidance in the README.

8. Do not move detailed build instructions into the README during this phase.

9. Do not delete historical information merely because it is removed from the README. Leave extraction to Phase 57B - Milestone History and Build Documentation Extraction.

### README target outline

Use a compact outline similar to:

```markdown
# Online MASM32 Educational Simulator

Short project description.

## Current status

- Repository/archive milestone: Phase 57A - README Landing Page Cleanup
- Runtime/source-run MASM behavior phase: Phase 57 - Signed IDIV

## Current simulator scope

Short description of current accepted source/runtime behavior.

## Quick start

### Serve the website

Command or link.

### Run tests

Aggregate command and focused-test pointer.

### Build

Command-file or Visual Studio solution note.

## Documentation

Links to detailed docs.

## Project boundaries

Short non-goal list.
```

### Required tests/checks

Add static documentation checks where practical:

- README contains current repository/archive milestone.
- README contains current runtime/source-run MASM behavior phase.
- README links to the full implementation spec.
- README links to the incremental implementation guide.
- README links to supported syntax documentation.
- README links to testing documentation.
- README does not contain the old large milestone-accomplishment block if a stable marker or heading can identify it.
- README still contains at least one website-serving command or a link to one.
- README still contains at least one aggregate test command or a link to one.
- README still points users to focused-test guidance.

### Acceptance criteria

- README is short enough to function as a project landing page.
- README no longer contains enormous milestone accomplishment blocks.
- README still tells a new user how to serve, test, and build the project at a basic level.
- README links to deeper documentation.
- No source code behavior changes.
- No runtime/source-run MASM behavior metadata change.

### Non-goals

- No source code changes.
- No MASM syntax changes.
- No parser, VM, executor, Wasm, browser UI, or diagnostic changes.
- No new documentation files required in this phase except small placeholders or links if needed.
- No milestone renumbering.

---

## 61B. Phase 57B - Milestone History and Build Documentation Extraction

### Goal

Move long-form milestone history and detailed build/development instructions out of the README and into dedicated documentation files.

This phase completes the documentation decomposition started by Phase 57A.

### Behavior category

Documentation and repository hygiene only.

Repository/archive milestone advances to:

```text
Phase 57B - Milestone History and Build Documentation Extraction
```

Runtime/source-run MASM behavior phase remains:

```text
Phase 57 - Signed IDIV
```

### Tasks

1. Create or update:

   ```text
   docs/MILESTONE_HISTORY.md
   ```

2. Move long milestone-accomplishment history into `docs/MILESTONE_HISTORY.md`.

3. Structure `docs/MILESTONE_HISTORY.md` so it is useful to an AI assistant without cluttering the README.

4. Include:
   - latest repository/archive milestone;
   - latest runtime/source-run MASM behavior phase;
   - concise milestone ledger;
   - links or references to detailed milestone reports when available;
   - warning that milestone reports are historical evidence and do not override the canonical spec/guide.

5. Create or update:

   ```text
   docs/BUILDING_AND_DEVELOPMENT.md
   ```

6. Move detailed build/development content into `docs/BUILDING_AND_DEVELOPMENT.md`, including:
   - local website serving;
   - native test prerequisites;
   - Emscripten prerequisites;
   - command-file build paths;
   - Visual Studio solution usage if the solution is included in the repository;
   - Visual Studio External Tools setup if useful;
   - troubleshooting for missing `emcc`;
   - aggregate and focused test commands;
   - browser/Wasm smoke guidance.

7. Keep README links to both documents.

8. Preserve information. Do not delete useful milestone or build instructions merely because they are no longer in the README.

9. Ensure any Visual Studio-specific material clearly says whether the Visual Studio solution is expected to be present in the repository or local-only.

10. Do not add future behavior claims to milestone history.

### Required tests/checks

Add static documentation checks where practical:

- `docs/MILESTONE_HISTORY.md` exists.
- `docs/BUILDING_AND_DEVELOPMENT.md` exists.
- README links to both files.
- milestone-history document states that milestone reports are historical evidence, not source of truth.
- build/development document includes aggregate test command.
- build/development document includes focused test command guidance.
- build/development document includes missing-`emcc` guidance or explicitly links to it.
- README does not contain detailed Visual Studio External Tools setup after extraction.

### Acceptance criteria

- README is short.
- Long history is preserved in `docs/MILESTONE_HISTORY.md`.
- Long build/development instructions are preserved in `docs/BUILDING_AND_DEVELOPMENT.md`.
- Documentation remains internally consistent about current status.
- No runtime/source-run MASM behavior metadata change.

### Non-goals

- No source code changes.
- No build-system behavior changes unless a link or documentation correction requires it.
- No diagnostic behavior changes.
- No browser UI changes.
- No milestone renumbering.

---

## 61C. Phase 57C - Diagnostic Policy Registry Design

### Goal

Design and add the shared diagnostic-policy registry skeleton without changing existing diagnostic behavior.

This phase creates the architecture that later phases use for additional configurable diagnostics.

### Behavior category

Backend diagnostic-policy architecture only.

Repository/archive milestone advances to:

```text
Phase 57C - Diagnostic Policy Registry Design
```

Runtime/source-run MASM behavior phase remains:

```text
Phase 57 - Signed IDIV
```

unless this phase intentionally adds public source-run setting schema fields. If any status surface advances, the milestone report must list it explicitly.

### Policy vocabulary

Define a common conceptual policy vocabulary:

```text
off
warn
error
```

Meanings:

- `off`: do not emit this optional teaching diagnostic or notice.
- `warn`: emit a non-fatal Simulator Message and continue when no lower-level fatal error occurs.
- `error`: emit a fatal assembly or runtime diagnostic and stop before affected runtime mutation when applicable.

Internal enum names may differ, but public documentation must map them clearly to this vocabulary.

### Tasks

1. Add a central diagnostic-policy registry type or equivalent internal structure.

2. Add a central way to identify diagnostic policy families.

3. Define initial family names for existing and planned families:

   ```text
   uninitialized-read
   undefined-flag-use
   compatibility-notice
   const-uninitialized-storage
   startup-state-notice
   unsupported-code-memory-access
   ```

4. Mark future families as reserved or inactive if they do not have behavior yet.

5. Add comments explaining how a future phase registers a new family.

6. Add policy parse/format helpers if useful.

7. Add tests for policy parse/format helpers.

8. Do not migrate existing diagnostics to the registry in this phase unless the migration is purely internal and provably behavior-preserving.

9. Do not change existing default behavior.

10. Do not add browser UI controls.

11. Do not change rendered Simulator Messages wording.

12. Do not add new MASM syntax.

### Required tests

Add tests for:

- policy value parsing:
  - `off`;
  - `warn`;
  - `error`;
- invalid policy value rejection;
- known policy family lookup;
- unknown policy family handling according to documented rule;
- inactive/future family handling according to documented rule;
- no behavior change in existing source-run defaults.

### Acceptance criteria

- There is a documented central policy model.
- A future phase can add a new policy-controlled diagnostic without inventing a separate settings path.
- Existing diagnostics behave exactly as before.
- Existing rendered Simulator Messages tests still pass.
- No UI controls are added.

### Non-goals

- No migration of all existing diagnostics unless strictly internal and behavior-preserving.
- No new diagnostic family behavior.
- No startup randomization.
- No `.CONST ?` compatibility.
- No `.code` read policy implementation.
- No README rewrite.

---

## 61D. Phase 57D - Existing Diagnostic Policy Migration

### Goal

Migrate existing configurable diagnostic families onto the shared diagnostic-policy registry while preserving existing behavior.

This phase should make the implementation more modular without changing the user-visible defaults.

### Behavior category

Backend diagnostic-policy migration.

Repository/archive milestone advances to:

```text
Phase 57D - Existing Diagnostic Policy Migration
```

Runtime/source-run MASM behavior phase remains:

```text
Phase 57 - Signed IDIV
```

unless this phase intentionally changes source-run setting schemas or status fields. Any such change must be listed in the milestone report.

### Tasks

1. Audit existing policy/configuration paths for:
   - uninitialized-read diagnostics;
   - undefined-flag-use diagnostics;
   - compatibility notices;
   - section-capacity validation;
   - section-image validation;
   - declared-object validation;
   - any Phase 53E diagnostic settings path.

2. Migrate policy lookup for existing families to the shared registry where practical.

3. Preserve all current default values.

4. Preserve all current source-run/test-facing setting names, or provide backward-compatible aliases.

5. Preserve all current browser/worker setting behavior.

6. Preserve all existing diagnostic codes.

7. Preserve all existing rendered Simulator Messages wording unless a wording change is explicitly required and tested.

8. Add validation for invalid values through the existing renderable setting-diagnostic path.

9. Ensure one policy family does not affect another.

10. Do not add new diagnostic families in this phase unless they are inactive placeholders.

### Required tests

Add regression tests for:

- uninitialized-read default behavior unchanged;
- undefined-flag-use default behavior unchanged;
- compatibility notices default behavior unchanged;
- memory validation setting behavior unchanged;
- explicit off/warn/error or off/warn/strict settings still map correctly for existing families;
- invalid setting values still produce renderable diagnostics;
- one policy family does not affect another;
- exact rendered Simulator Messages tests still pass.

### Acceptance criteria

- Existing policy-controlled diagnostics route through the shared registry or an explicitly documented compatibility layer.
- Existing defaults are unchanged.
- Existing UI/source-run settings remain compatible.
- Existing tests pass.
- No new MASM syntax or runtime instruction behavior is added.

### Non-goals

- No new browser settings.
- No startup randomization.
- No `.CONST ?` compatibility.
- No `.code` read policy behavior.
- No diagnostic wording cleanup beyond necessary setting-error wording.

---

## 61E. Phase 57E - Startup State Notice and Zero-Default Documentation

### Goal

Make deterministic zero startup behavior visible and documented without changing startup values.

This phase explains the simulator's default startup model to users.

### Behavior category

Documentation and diagnostic notice.

Repository/archive milestone advances to:

```text
Phase 57E - Startup State Notice and Zero-Default Documentation
```

Runtime/source-run MASM behavior phase remains:

```text
Phase 57 - Signed IDIV
```

unless the source-run payload intentionally reports the new notice family as runtime metadata. The milestone report must state the decision.

### Tasks

1. Document that default startup state is deterministic and simulator-defined.

2. Add or use diagnostic policy family:

   ```text
   startup-state-notice
   ```

3. Default behavior may emit a non-fatal notice explaining deterministic startup.

4. The notice must be routed through Simulator Messages, not Program Console.

5. The notice must not block parsing, loading, or execution.

6. The notice must not imply that real MASM programs start with zeroed arbitrary registers or flags.

7. The notice must not change register, flag, or memory initialization.

8. Add opt-out through the diagnostic policy registry if Phase 57D made that practical.

9. Do not implement randomization in this phase.

### Example notice text

```text
The simulator starts registers and modeled flags at 0. Uninitialized storage bytes are also zero-filled, with uninitialized-origin metadata preserved for code-quality diagnostics. Real MASM programs running on real systems should not rely on arbitrary register or flag startup values.
```

The exact wording may differ, but it must be stable and covered by exact rendered Simulator Messages tests.

### Required tests

Add tests for:

- default startup values unchanged;
- representative existing program output unchanged except for the optional notice;
- notice rendered through Simulator Messages;
- no Program Console output from notice;
- notice opt-out if implemented;
- exact rendered Simulator Messages wording;
- no changes to uninitialized-origin metadata.

### Acceptance criteria

- Users can see or find an explanation that zero startup is a simulator choice.
- Default startup values do not change.
- Existing execution semantics do not change.
- No randomization is implemented.
- Notice behavior is testable and configurable if policy plumbing supports it.

### Non-goals

- No seeded random mode.
- No memory randomization.
- No UI controls.
- No MASM syntax changes.
- No instruction behavior changes.

---

## 61F. Phase 57F - Seeded Random Register and Flag Startup Mode

### Goal

Add an opt-in deterministic seeded-random startup mode for registers and modeled flags.

This phase does not randomize memory.

### Behavior category

Runtime/source-run configuration.

Repository/archive milestone advances to:

```text
Phase 57F - Seeded Random Register and Flag Startup Mode
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57F - Seeded Random Register and Flag Startup Mode
```

because this phase adds runtime-visible initialization behavior when non-default settings are selected.

Default behavior remains deterministic zero.

### Settings

Introduce register/flag startup randomization as its own setting axis. Do not use a single catch-all startup mode that later phases must overload for unrelated startup state.

Public/test-facing settings added or confirmed by this phase:

```text
startup_register_flag_mode = zero | seeded-random
startup_state_seed = <u32>
```

Internal enum names may differ, but the public/test-facing setting names, source-run option names, environment-test names, worker setting names, or documentation names used by this phase must make the register/flag scope explicit.

The startup settings use three independent axes across Phase 57F and Phase 57G:

- `startup_register_flag_mode`: owned by Phase 57F; controls only general-purpose register and modeled-flag startup values.
- `uninitialized_storage_visible_byte_mode`: owned by Phase 57G - Seeded Random Uninitialized Storage Mode; controls only visible bytes of uninitialized-origin storage.
- `startup_state_seed`: shared deterministic seed input; it does not by itself choose which startup categories are randomized.

Phase 57G intentionally adds the uninitialized-storage setting as a separate axis. Do not merge that memory setting into `startup_register_flag_mode`.

### Required behavior

`startup_register_flag_mode = zero`:

- preserves the current default CPU startup behavior;
- leaves general-purpose registers and modeled flag bits at the current default values;
- ignores or does not require `startup_state_seed`.

`startup_register_flag_mode = seeded-random`:

- initializes general-purpose registers from a deterministic pseudo-random generator seeded by `startup_state_seed`;
- initializes modeled flag bits from the same deterministic seed stream or a documented deterministic derivation;
- produces the same initial register and modeled-flag state for the same source, same settings, same seed, and same input;
- should produce at least one different register or modeled-flag value for different seeds in fixtures designed to observe startup randomization;
- does not randomize any memory region;
- does not randomize initialized `.data`;
- does not randomize initialized `.CONST`;
- does not randomize `.DATA?`, `?`, or `DUP(?)` visible bytes in this phase;
- does not change uninitialized-origin metadata;
- does not change deterministic behavior when `startup_register_flag_mode = zero`.

This phase must not use host CPU/register/process state and must not use true nondeterministic randomness.

### Required tests

Add tests for:

- default `startup_register_flag_mode = zero` unchanged;
- fixed seed reproduces identical register and flag initial state;
- different seeds produce at least one different register or modeled flag value;
- memory bytes unchanged from existing default behavior;
- initialized `.data` unchanged;
- initialized `.CONST` unchanged;
- uninitialized-origin visible bytes unchanged;
- uninitialized-origin metadata unchanged;
- invalid register/flag startup mode rejected with structured/renderable setting diagnostic;
- invalid seed rejected with structured/renderable setting diagnostic;
- startup randomization notice rendered if notice policy is enabled;
- no Program Console output from startup notices.

### Acceptance criteria

- Seeded register/flag startup is deterministic and testable.
- Default behavior remains zero.
- Memory behavior is unchanged.
- Uninitialized-origin metadata is unchanged.
- No host CPU/register/process state is used.
- No true nondeterministic randomness is used.
- Existing tests pass under default settings.
- Phase 57G remains able to add uninitialized-storage visible-byte randomization as an independent setting axis.

### Non-goals

- No memory randomization.
- No uninitialized storage visible-byte randomization.
- No `.CONST ?` acceptance.
- No UI controls.
- No stack execution behavior.
- No instruction semantic changes.

---

## 61G. Phase 57G - Seeded Random Uninitialized Storage Mode

### Goal

Add an opt-in deterministic seeded mode for visible bytes in uninitialized-origin storage while preserving uninitialized-origin metadata.

This phase builds on Phase 57F - Seeded Random Register and Flag Startup Mode but is separate because memory initialization interacts with layout, source-run results, uninitialized-read diagnostics, and tests.

### Behavior category

Runtime/source-run configuration and memory initialization.

Repository/archive milestone advances to:

```text
Phase 57G - Seeded Random Uninitialized Storage Mode
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57G - Seeded Random Uninitialized Storage Mode
```

because this phase changes runtime-visible memory initialization when non-default settings are selected.

Default behavior remains deterministic zero.

### Settings

Add uninitialized-storage visible-byte randomization as a second startup setting axis. This phase must not overload, replace, or reinterpret the Phase 57F register/flag startup setting.

Public/test-facing settings after this phase:

```text
startup_register_flag_mode = zero | seeded-random
uninitialized_storage_visible_byte_mode = zero | seeded-random
startup_state_seed = <u32>
```

Internal enum names may differ, but public/test-facing names must make the two axes unambiguous.

These setting axes must remain orthogonal. Changing `startup_register_flag_mode` must not silently change `uninitialized_storage_visible_byte_mode`, and changing `uninitialized_storage_visible_byte_mode` must not silently change `startup_register_flag_mode`. `startup_state_seed` is only the deterministic seed consumed by whichever seeded-random axes are enabled.

Behavior:

- `startup_register_flag_mode = zero` preserves current default register and modeled-flag initialization.
- `startup_register_flag_mode = seeded-random` initializes general-purpose registers and modeled flag bits from the deterministic seed.
- `uninitialized_storage_visible_byte_mode = zero` preserves current visible-byte initialization for `.DATA?`, `?`, and `DUP(?)`.
- `uninitialized_storage_visible_byte_mode = seeded-random` initializes visible bytes of uninitialized-origin storage from the deterministic seed while preserving uninitialized-origin metadata.
- The two settings are independent and may be combined.
- The same source, same settings, same seed, and same input must produce the same startup state.
- Different seeds should produce at least one different randomized register, modeled flag, or uninitialized-origin byte in fixtures designed to observe that category.
- Initialized `.data` bytes are never randomized by either setting.
- Initialized `.CONST` bytes are never randomized by either setting.
- `.CONST ?` and `.CONST DUP(?)` bytes are governed by `uninitialized_storage_visible_byte_mode` only after Phase 57I - .CONST Uninitialized Storage Acceptance has made those forms accepted.
- Default behavior remains `zero` for both axes.

A mutually exclusive combined startup enum is not acceptable for Phase 57G. The implementation must expose register/flag startup and uninitialized-storage visible-byte startup as independent setting axes, even if an internal helper shares seeded-random generator code between the two axes.

### Required behavior

When `uninitialized_storage_visible_byte_mode = zero`:

- visible bytes of uninitialized-origin storage remain deterministic zero by default;
- uninitialized-origin metadata remains present until overwritten by simulated program writes;
- existing uninitialized-read warning/strict behavior remains unchanged.

When `uninitialized_storage_visible_byte_mode = seeded-random`:

- visible bytes of uninitialized-origin storage are initialized from a deterministic pseudo-random generator seeded by `startup_state_seed`;
- uninitialized-origin metadata remains present;
- reads from those bytes still trigger uninitialized-read diagnostics according to the active policy;
- initialized `.data` bytes are not randomized;
- initialized `.CONST` bytes are not randomized;
- `.CONST ?` bytes are randomized only if Phase 57I - .CONST Uninitialized Storage Acceptance has already implemented `.CONST ?` acceptance and this phase explicitly includes them through the shared uninitialized-origin storage initialization path;
- region permissions are unchanged;
- `.CONST` write protection is unchanged;
- checked memory helpers remain authoritative.

### Required tests

Add tests for all four setting combinations:

- zero registers/flags + zero uninitialized storage;
- seeded registers/flags + zero uninitialized storage;
- zero registers/flags + seeded uninitialized storage;
- seeded registers/flags + seeded uninitialized storage.

Also add tests for:

- default zero/zero mode unchanged;
- fixed seed reproduces identical uninitialized visible bytes;
- different seeds produce at least one different uninitialized visible byte;
- initialized `.data` unchanged;
- initialized `.CONST` unchanged;
- `.DATA?` uninitialized-origin metadata preserved;
- `?` metadata preserved;
- `DUP(?)` metadata preserved;
- uninitialized-read warning still fires even when visible byte is nonzero;
- strict uninitialized-read behavior still stops before consuming bytes;
- no Program Console output from startup notices.

After Phase 57I - .CONST Uninitialized Storage Acceptance, add or update tests proving `.CONST ?` and `.CONST DUP(?)` follow `uninitialized_storage_visible_byte_mode` while retaining read-only protection and uninitialized-origin metadata.

### Acceptance criteria

- Memory randomization is opt-in and deterministic.
- Register/flag randomization and uninitialized-storage visible-byte randomization are independent setting axes.
- Uninitialized-origin metadata is preserved.
- Existing default tests pass.
- Existing uninitialized-read diagnostics still work.
- No initialized storage is randomized.

### Non-goals

- No randomization of initialized `.data`.
- No randomization of initialized `.CONST`.
- No `.CONST ?` acceptance; Phase 57I owns that acceptance.
- No host memory use.
- No nondeterministic randomness.
- No UI controls.
- No instruction semantic changes.

---

## 61H. Phase 57H - Register Unchanged Display Markers

### Goal

Add compact unchanged markers to final register display so users can quickly identify canonical register families left unmodified by the program.

This is a display-formatting phase.

### Behavior category

Display formatting only.

Repository/archive milestone advances to:

```text
Phase 57H - Register Unchanged Display Markers
```

Runtime/source-run MASM behavior phase remains the latest prior runtime/source-run phase.

### Display policy

For each canonical register family, append a compact marker to the canonical parent row only when no part of that register family was written by the program.

Required marker text:

```text
[unchanged]
```

Required display shape:

```text
EAX    | 00000000h / u: 0 / s: 0        [unchanged]
  AX   |     0000h / u: 0 / s: 0
    AH |       00h / u: 0 / s: 0
    AL |       00h / u: 0 / s: 0
```

Rules:

- The marker appears only on the canonical parent register row.
- The marker does not appear on alias rows.
- Alias rows inherit the parent-family status visually.
- If `AL`, `AH`, `AX`, or `EAX` is written, the `EAX` parent row must not show `[unchanged]`.
- If `BL`, `BH`, `BX`, or `EBX` is written, the `EBX` parent row must not show `[unchanged]`.
- If `CL`, `CH`, `CX`, or `ECX` is written, the `ECX` parent row must not show `[unchanged]`.
- If `DL`, `DH`, `DX`, or `EDX` is written, the `EDX` parent row must not show `[unchanged]`.
- If `SI` or `ESI` is written, the `ESI` parent row must not show `[unchanged]`.
- If `DI` or `EDI` is written, the `EDI` parent row must not show `[unchanged]`.
- If `BP` or `EBP` is written, the `EBP` parent row must not show `[unchanged]`.
- If `SP` or `ESP` is written, the `ESP` parent row must not show `[unchanged]`.

Preferred unchanged policy:

```text
write-tracking unchanged
```

Meaning:

- A register family is `[unchanged]` only if no part of that family was written by the program.
- A write from `0` to `0` still counts as changed because the program touched the register.

If write-tracking is not available, the phase may temporarily use value-equality only if the milestone report documents the limitation and adds a future TODO. The preferred result remains write-tracking.

Small-window rule:

- The marker must be appended after the existing aligned numeric display.
- The formatter must not wrap register rows merely to show `[unchanged]`.
- The formatter must not break numeric alignment merely to show `[unchanged]`.
- If the available display width is too narrow, the formatter may omit the marker.

### Required tests

Add formatter tests for:

- all canonical register families unchanged;
- `EAX` written through `EAX`;
- `EAX` written through `AX`;
- `EAX` written through `AH`;
- `EAX` written through `AL`;
- `ESI` written through `SI`;
- `ESP` written through `SP`;
- register written with same final value does not show `[unchanged]` if write-tracking is implemented;
- marker appears only on parent row;
- alias rows do not include marker;
- existing signed/unsigned/hex display remains intact;
- Program Console output remains unchanged;
- Simulator Messages output remains unchanged.

### Acceptance criteria

- Final register display shows `[unchanged]` on parent rows for untouched register families.
- Changed register families omit the marker.
- Alias rows remain clean and aligned.
- Existing signed/unsigned/hex display remains intact.
- No VM execution changes.
- No parser or executor behavior changes.
- No diagnostic behavior changes.

### Non-goals

- No debugger stepping changes.
- No memory-change display redesign.
- No Program Console changes.
- No browser settings UI.
- No source-run semantic changes unless strictly required to expose write-tracking metadata to the formatter.

---

## 61I. Phase 57I - .CONST Uninitialized Storage Acceptance

### Goal

Accept `.CONST ?` and `.CONST DUP(?)` declarations as read-only uninitialized-origin storage.

Through Phase 57G - Seeded Random Uninitialized Storage Mode, `.CONST ?` and `.CONST DUP(?)` remain rejected. Phase 57I is the first phase that accepts these forms; Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy is the first phase that makes declaration diagnostics for these forms configurable.

This phase implements compatibility acceptance only. It does not add the configurable warning/error policy; that belongs to Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy.

### Behavior category

Parser/data-layout compatibility.

Repository/archive milestone advances to:

```text
Phase 57I - .CONST Uninitialized Storage Acceptance
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57I - .CONST Uninitialized Storage Acceptance
```

because this phase changes accepted MASM/source behavior.

### Accepted syntax

After this phase, `.CONST` accepts uninitialized storage forms that are already accepted in compatible data declarations elsewhere.

Accepted examples:

```asm
.CONST
x DWORD ?
buf BYTE 16 DUP(?)
words WORD 4 DUP(?)
```

The exact accepted initializer grammar must match the current repository's existing support for `?` and `DUP(?)` in data declarations.

Do not broaden `DUP`, expression, type, or initializer syntax beyond what this phase explicitly requires.

### Runtime/data semantics

Required behavior:

- `.CONST ?` storage is allocated in `.CONST`.
- `.CONST DUP(?)` storage is allocated in `.CONST`.
- Bytes are deterministic by default.
- Bytes retain uninitialized-origin metadata.
- Reads are allowed if the final address/range passes mandatory memory validation.
- Writes are rejected through existing `.CONST` read-only protection.
- Static direct writes to `.CONST` remain rejected when the implementation can detect them statically.
- Computed writes whose final byte range overlaps `.CONST` remain rejected by mandatory runtime memory permission checks.
- `.CONST` write failures must not be reclassified as uninitialized-read, section-image, section-capacity, declared-object, or teaching diagnostics.

### Required tests

Add parser/data-layout tests for:

- `.CONST x DWORD ?` accepted;
- `.CONST buf BYTE 16 DUP(?)` accepted;
- `.CONST words WORD 4 DUP(?)` accepted;
- `.CONST` initialized declarations still accepted;
- malformed `.CONST` declarations still rejected;
- unsupported initializer forms still rejected.

Add source-run tests for:

- reading `.CONST ?` returns deterministic bytes;
- reading `.CONST ?` preserves uninitialized-origin metadata;
- direct write to `.CONST ?` remains rejected;
- computed write to `.CONST ?` remains rejected through mandatory runtime permission checks;
- failed `.CONST` write does not mutate registers, flags, memory, Program Console output, or memory-change rows.

### Acceptance criteria

- `.CONST ?` and `.CONST DUP(?)` are accepted for compatibility.
- `.CONST` remains read-only.
- Uninitialized-origin metadata is preserved.
- Existing initialized `.CONST` behavior remains unchanged.
- Existing `.DATA?` behavior remains unchanged.
- No new configurable warning/error policy is added in this phase.

### Non-goals

- No new browser UI controls.
- No `const-uninitialized-storage` policy yet.
- No diagnostic wording changes except existing diagnostics that must adapt to acceptance.
- No weakening of `.CONST` write protection.
- No executable QWORD/SQWORD memory operations.
- No linker or PE behavior.

---

## 61J. Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy

### Goal

Add configurable teaching diagnostics for `.CONST` uninitialized storage accepted by Phase 57I.

### Behavior category

Diagnostic policy and rendered messages.

Repository/archive milestone advances to:

```text
Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy
```

because this phase changes user-visible diagnostics and policy behavior for accepted source.

### Diagnostic family

Add or activate diagnostic family:

```text
const-uninitialized-storage
```

Default policy:

```text
warn
```

Policy behavior:

- `off`: accept `.CONST ?` and `.CONST DUP(?)` silently, while preserving uninitialized-origin metadata.
- `warn`: emit a non-fatal `simulator-warning` and continue when no other fatal diagnostic exists.
- `error`: emit an assembly error and refuse execution.

### Warning message requirements

The warning must:

- identify the `.CONST` declaration;
- explain that the simulator accepts the declaration for compatibility;
- explain that bytes are initialized deterministically by the simulator;
- explain that uninitialized-origin metadata is preserved;
- avoid claiming that real programs should rely on the value;
- appear in Simulator Messages, not Program Console.

Example warning:

```text
.CONST declaration `x` reserves uninitialized read-only storage. The simulator accepts this for compatibility, initializes the bytes deterministically, and preserves uninitialized-origin metadata.
```

Exact wording may differ, but it must be stable, actionable, and covered by exact rendered Simulator Messages tests.

### Interaction with uninitialized-read diagnostics

Reading `.CONST ?` storage may also trigger `uninitialized-read` according to the active uninitialized-read policy.

Example:

```asm
.CONST
x DWORD ?

.code
main PROC
    mov eax, x
main ENDP
END main
```

Expected default behavior when both policies warn:

- `const-uninitialized-storage` warning is emitted for the declaration;
- `uninitialized-read` warning is emitted for the read;
- execution continues if no fatal diagnostic occurs;
- `EAX` receives the deterministic bytes read;
- `.CONST` remains read-only.

If `const-uninitialized-storage` is off, only the declaration warning is suppressed.

If `uninitialized-read` is off, only the read warning is suppressed.

### Required tests

Add source-run tests for:

- default declaration warning;
- policy `off` suppresses declaration warning only;
- policy `error` rejects declaration and prevents execution;
- uninitialized-read warning still applies to reads when enabled;
- uninitialized-read policy off does not suppress declaration warning;
- direct write to `.CONST ?` still uses existing `.CONST` protection diagnostics;
- computed write to `.CONST ?` still uses runtime permission diagnostics.

Add rendered Simulator Messages tests for:

- default declaration warning;
- policy `error` diagnostic;
- read warning interaction;
- `.CONST` write protection still uses existing permission/read-only wording.

### Acceptance criteria

- `.CONST ?` diagnostics are configurable.
- Default behavior warns and continues.
- Policy `error` blocks execution.
- `.CONST` write protection remains mandatory.
- Uninitialized-read policy remains separate.
- Program Console remains unchanged.

### Non-goals

- No new `.CONST` acceptance syntax beyond Phase 57I.
- No weakening of `.CONST` protection.
- No browser UI controls.
- No linker or PE behavior.

---

---

## 61K. Phase 57K - .CODE and MASM Segment Symbol Access Policy

### Goal

Audit current `.code` memory access behavior and lock the v1 policy that `.code` is not user-readable or user-writable simulated program memory.

This phase also defines how MASM/object/linker segment and group symbols such as `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` are classified.

This is an audit, policy, documentation, and test-characterization phase. It should avoid behavior changes unless required to expose current behavior safely in tests.

### Behavior category

Memory-policy audit and MASM compatibility classification.

Repository/archive milestone advances to:

```text
Phase 57K - .CODE and MASM Segment Symbol Access Policy
```

Runtime/source-run MASM behavior phase remains the latest prior runtime/source-run behavior phase unless this phase intentionally changes source-run memory-access behavior or diagnostics. The milestone report must state which case applies.

### Required `.code` policy decision

The v1 policy is:

```text
unsupported-code-memory-access
```

This is not optional.

Do not keep or introduce these alternative v1 policies:

```text
unsupported-readable-code-image
deterministic-simulator-code-image
```

Reason:

- `.code` is not populated with real x86 instruction bytes;
- returning deterministic zero bytes from `.code` is misleading;
- returning simulator pseudo-bytes would imply a code-image contract the project does not currently have;
- implementing real code bytes would move toward assembler/PE/linker behavior, which is outside current scope;
- the simulator executes internal IR, not native x86 bytecode.

### Required `.code` access policy

All simulated source-level memory accesses whose final byte range overlaps `.code` are unsupported.

The policy applies to:

- reads wholly inside `.code`;
- writes wholly inside `.code`;
- reads partially overlapping `.code`;
- writes partially overlapping `.code`;
- direct absolute-address forms, if supported;
- register-indirect forms;
- displacement forms;
- symbol-derived forms, if any future feature can produce them;
- computed addresses that happen to land in `.code`.

This policy applies regardless of whether the access would otherwise pass ordinary region validation.

The target diagnostic family is:

```text
unsupported-code-memory-access
```

The implementation may split this into read/write-specific codes if the distinction is documented and tested:

```text
unsupported-code-memory-read
unsupported-code-memory-write
```

The milestone report must state the chosen code names.

### Diagnostic precedence policy

Mandatory lower-level diagnostics still take precedence when the simulator cannot compute or classify a final `.code` overlap safely.

Precedence examples:

1. Malformed memory syntax remains a parser diagnostic.
2. Ambiguous memory width remains an ambiguous-width diagnostic.
3. Unsupported executable memory width remains an unsupported-width diagnostic.
4. Address arithmetic overflow remains an address/range diagnostic.
5. Address outside all VM regions remains an invalid-region diagnostic.
6. `.CONST` write protection remains a `.CONST` permission/read-only diagnostic.
7. If a final valid computed range overlaps `.code`, emit the selected unsupported `.code` memory-access diagnostic.

Do not reclassify `.CONST` permission failures as `.code` access.

Do not reclassify invalid-region accesses as `.code` access.

### Required current-behavior audit

Audit and document current `.code` behavior:

- `.code` region base address, if one exists;
- `.code` region capacity, if one exists;
- `.code` region permissions;
- whether reads are currently allowed;
- whether writes are currently rejected;
- what byte values are currently returned by reads;
- why zero bytes are returned, if current reads return zero;
- whether returned bytes are backing storage, section image, capacity slack, deterministic default memory, or another artifact;
- source forms that can currently access `.code` addresses.

The milestone report must state whether current behavior already denies `.code` writes, and whether this denial is from ordinary permissions or a specific `.code` access policy.

### MASM segment and group symbol policy

Classify these names as recognized unsupported MASM/object/linker concepts when used as addressable symbols or segment definitions:

```text
_TEXT
_DATA
_BSS
CONST
STACK
DGROUP
FLAT
```

The exact list may include additional conventional segment/group names if the implementation already recognizes them, but do not add broad segment modeling.

Required classification:

```text
unsupported-segment-symbol
```

or a documented equivalent.

These names must not become aliases for simulator internal regions.

They must not be accepted as ways to access:

- `.code`;
- `.data`;
- `.DATA?`;
- `.CONST`;
- stack;
- heap;
- any internal VM region.

They must not imply:

- COFF section symbols;
- OMF segment behavior;
- linker groups;
- PE section layout;
- relocation records;
- loader behavior.

Examples that must be classified as unsupported when recognized:

```asm
mov eax, OFFSET _TEXT
mov eax, OFFSET _DATA
mov eax, OFFSET _BSS
mov eax, OFFSET CONST
mov eax, OFFSET STACK
mov eax, OFFSET DGROUP
mov eax, OFFSET FLAT

mov eax, DWORD PTR [_TEXT]
mov eax, DWORD PTR [_DATA]

_TEXT SEGMENT
_TEXT ENDS
_DATA SEGMENT
_DATA ENDS
```

Ordinary user data labels remain supported and must not be confused with segment symbols:

```asm
.data
value DWORD 1

.code
main PROC
    mov eax, OFFSET value
main ENDP
END main
```

### CASEMAP policy for segment/group names

Segment/group-symbol diagnostics must respect the active user-symbol lookup policy when the source form treats the token as a symbol.

Default and `OPTION CASEMAP:ALL` behavior:

- `_TEXT`, `_text`, and `_Text` should be recognized as the same unsupported segment symbol.

`OPTION CASEMAP:NONE` behavior:

- exact unsupported segment-symbol spellings such as `_TEXT` should still be diagnosed;
- a different-case user-defined symbol may be allowed only if the current symbol policy permits it and it does not collide with a reserved exact spelling.

The milestone report must state the implemented matching behavior.

### Documentation requirements

Update documentation to state:

- `.code` memory reads are unsupported;
- `.code` memory writes are unsupported;
- `.code` is not a readable PE `.text` image;
- the simulator executes internal IR;
- the simulator does not expose `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, or `FLAT` as addressable linker symbols;
- users should use declared data labels instead of MASM segment symbols for data addresses.

### Required tests/checks

Add audit or characterization tests where practical:

- current `.code` read behavior is identified;
- current `.code` write behavior is identified;
- documentation names `unsupported-code-memory-access`;
- documentation does not mention `deterministic-simulator-code-image` as a v1 option;
- documentation does not claim x86 opcode bytes are emitted;
- `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` are listed as unsupported segment/group symbols.

### Acceptance criteria

- The v1 policy is locked to `unsupported-code-memory-access`.
- The optional deterministic `.code` image policy is removed.
- MASM segment/group symbols are classified as unsupported addressable symbols.
- Phase 57L has a precise `.code` runtime diagnostic target.
- Phase 57M has a precise segment/group symbol diagnostic target.
- No real x86 encoding is implemented.
- No PE, COFF, OMF, linker, or loader behavior is implemented.

### Non-goals

- No `.code` access diagnostic implementation unless needed for safe characterization.
- No segment/group symbol diagnostic implementation unless needed for safe characterization.
- No x86 opcode emitter.
- No disassembler.
- No PE `.text` layout generation.
- No object files.
- No linker.
- No relocation records.
- No Windows loader behavior.
- No host filesystem behavior.
- No branch/control-flow implementation.
- No segment-register behavior.
- No segment override behavior.
- No `ASSUME` implementation.

---

## 61L. Phase 57L - .CODE Memory Access Diagnostics

### Goal

Implement the unsupported `.code` memory-access policy defined by Phase 57K - .CODE and MASM Segment Symbol Access Policy.

This phase denies both reads and writes whose final byte range overlaps `.code`.

This phase does not implement MASM segment/group symbol diagnostics. Those belong to Phase 57M - MASM Segment and Group Symbol Diagnostics.

### Behavior category

Memory diagnostics and source-run behavior.

Repository/archive milestone advances to:

```text
Phase 57L - .CODE Memory Access Diagnostics
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57L - .CODE Memory Access Diagnostics
```

because this phase changes source-run behavior for `.code` memory accesses.

### Diagnostic codes

Preferred code for any source-level memory access whose final range overlaps `.code`:

```text
unsupported-code-memory-access
```

Acceptable split codes, if implemented consistently:

```text
unsupported-code-memory-read
unsupported-code-memory-write
```

The milestone report must list the actual diagnostic codes used.

### Required `.code` read behavior

A source-level memory read whose final byte range overlaps `.code` must fail before consuming a value.

This includes:

- read wholly inside `.code`;
- read whose first byte is outside `.code` but later bytes overlap `.code`;
- read whose first byte is inside `.code` and later bytes leave `.code`;
- direct absolute address read, if absolute address reads are supported;
- register-indirect read;
- displacement read;
- symbol-derived read, if any source form can produce one.

Example shapes:

```asm
mov eax, DWORD PTR [ebx]      ; if EBX points into .code
mov ax, WORD PTR [ebx + 1]    ; if final range overlaps .code
mov al, BYTE PTR [ebx]        ; if EBX points into .code
```

The diagnostic must explain:

- `.CODE/_TEXT` is not exposed as an accessible memory region;
- the program stopped before access.

The `.CODE/_TEXT` wording is diagnostic copy only. `_TEXT` remains an unsupported MASM/object segment symbol and is not an addressable alias for the simulator's internal `.code` region.

### Required `.code` write behavior

A source-level memory write whose final byte range overlaps `.code` must fail before committing memory mutation.

This includes:

- write wholly inside `.code`;
- write whose first byte is outside `.code` but later bytes overlap `.code`;
- write whose first byte is inside `.code` and later bytes leave `.code`;
- direct absolute address write, if absolute address writes are supported;
- register-indirect write;
- displacement write;
- symbol-derived write, if any source form can produce one.

Example shapes:

```asm
mov DWORD PTR [ebx], 1        ; if EBX points into .code
inc DWORD PTR [ebx]           ; if EBX points into .code
add BYTE PTR [ebx], 1         ; if EBX points into .code
```

The diagnostic must explain:

- `.CODE/_TEXT` is not exposed as an accessible memory region;
- the program stopped before access.

The `.CODE/_TEXT` wording is diagnostic copy only. `_TEXT` remains an unsupported MASM/object segment symbol and is not an addressable alias for the simulator's internal `.code` region.

### No-partial-mutation behavior

For fatal `.code` memory-access diagnostics:

- preserve all registers;
- preserve modeled flags;
- preserve flag-validity metadata;
- preserve memory;
- preserve Program Console output;
- create no successful memory-change row;
- do not emit `execution-complete`;
- append only the relevant Simulator Messages diagnostic.

For read-modify-write instructions such as `inc DWORD PTR [ebx]`:

- do not perform the read;
- do not perform the write;
- do not update flags;
- do not create a memory-change row.

### Relationship to planned-access validation

`.code` memory-access rejection is mandatory simulator behavior, not an optional teaching diagnostic.

It must not be controlled by:

- section-capacity policy;
- section-image policy;
- declared-object policy;
- uninitialized-read policy;
- compatibility-notice policy;
- startup-state policy.

If implementation architecture uses planned-access validation to detect `.code` overlap before mutation, that is acceptable. The resulting diagnostic remains mandatory and must not be downgraded to a warning by optional policies.

### Diagnostic precedence

Parser/source-shape diagnostics still happen before runtime memory classification:

- malformed memory operands remain parser diagnostics;
- ambiguous memory width still reports `ambiguous-memory-width`;
- unsupported executable QWORD/SQWORD memory width remains the existing unsupported-width diagnostic;
- unsupported advanced addressing remains the existing unsupported-addressing diagnostic.

Mandatory Level 1 runtime memory failures must preserve this order:

1. Address arithmetic overflow remains an address/range diagnostic.
2. A final byte range that is plainly outside all VM regions and does not intersect a known protected region remains the existing invalid-address, invalid-region, or range diagnostic.
3. A cross-region access that intersects a known protected region reports `region-boundary-crossing`.
4. A wholly-contained `.CONST` write reports the existing `.CONST` permission/read-only diagnostic.
5. A wholly-contained `.CONST` read remains allowed unless another mandatory or enabled strict validation rejects it.
6. A wholly-contained `.code` read or write reports the Phase 57L `.code` memory-access diagnostic.
7. A cross-region access that intersects `.code`, after `.code` is treated as a protected memory-access region by Phase 57L, reports `region-boundary-crossing` using runtime `.code` layout metadata.

Do not report `.code` access for an address that cannot be proven to overlap `.code`.

Do not suppress `.CONST` write-protection diagnostics by reporting `.code` access instead.

The protected-region start address in any `region-boundary-crossing` rendered message must come from active runtime layout metadata. It must not be hardcoded to the fixed educational layout.

This phase must not weaken or reword the Phase 57-CORR1 `.CONST` distinction:

```text
Wholly-contained write overlapping `.CONST`:
  permission-denied

Cross-region read or write whose final range intersects `.CONST`:
  region-boundary-crossing

Wholly-contained read from `.CONST`:
  allowed, unless another mandatory or enabled strict validation rejects it

Cross-region access that does not intersect `.CONST` or another known protected region:
  ordinary Level 1 region/range diagnostic
```

### Required source-run `.code` access tests

Add source-run tests for `.code` access. Use whatever setup the implementation provides to place a register at a known `.code` address.

Required cases:

- BYTE read wholly inside `.code`;
- WORD read wholly inside `.code`;
- DWORD read wholly inside `.code`;
- read partially overlapping `.code` from before the region;
- read partially overlapping `.code` from inside the region;
- BYTE write wholly inside `.code`;
- WORD write wholly inside `.code`;
- DWORD write wholly inside `.code`;
- write partially overlapping `.code` from before the region;
- write partially overlapping `.code` from inside the region;
- read-modify-write instruction targeting `.code`, if any implemented instruction can do so.

Each fatal `.code` access test must verify:

- no register mutation;
- no flag mutation;
- no flag-validity metadata mutation;
- no memory mutation;
- no Program Console output;
- no successful memory-change row;
- no `execution-complete`.

### Required precedence tests

Add tests proving:

- invalid address still reports invalid address/range when no known protected region is intersected;
- invalid region still reports invalid region when no known protected region is intersected;
- ambiguous width still reports ambiguous memory width;
- unsupported QWORD/SQWORD executable memory width remains unsupported width;
- unsupported advanced addressing remains unsupported addressing;
- wholly-contained `.CONST` write remains `.CONST` permission/read-only diagnostic;
- wholly-contained `.CONST` read remains allowed unless another mandatory or enabled strict validation rejects it;
- cross-region `.CONST` read overlap reports `region-boundary-crossing`;
- cross-region `.CONST` write overlap reports `region-boundary-crossing`;
- wholly-contained `.code` read reports the Phase 57L `.code` access diagnostic;
- wholly-contained `.code` write reports the Phase 57L `.code` access diagnostic;
- cross-region `.code` read overlap reports `region-boundary-crossing`;
- cross-region `.code` write overlap reports `region-boundary-crossing`.

The Phase 57L milestone report must explicitly state that `.CONST` `region-boundary-crossing` regression coverage was preserved while adding `.code` memory-access diagnostics.

### Required rendered-message tests

Add exact rendered Simulator Messages tests for:

- `.code` read diagnostic;
- `.code` write diagnostic;
- partial-overlap `.code` access diagnostic.

Rendered messages must include source line, column, byte offset, and span length where available.

### Documentation updates

Update supported syntax and troubleshooting documentation to say:

- `.code` memory reads are unsupported;
- `.code` memory writes are unsupported;
- `.code` is not exposed as a readable or writable byte image;
- the simulator executes internal IR;
- real x86 opcode bytes are not emitted.

### Acceptance criteria

- All `.code` reads are denied.
- All `.code` writes are denied.
- Partial overlaps are denied.
- No partial mutation occurs after fatal `.code` access diagnostics.
- `.CONST` write protection precedence is preserved.
- Documentation no longer describes a deterministic `.code` image option.
- No x86 opcode emission, PE layout, linker behavior, or segment modeling is implemented.

### Non-goals

- No MASM segment/group symbol diagnostics; Phase 57M owns those.
- No `.code` readable byte image.
- No deterministic simulator code-image bytes.
- No x86 opcode emitter.
- No disassembler.
- No PE `.text` layout.
- No object files.
- No linker.
- No relocation records.
- No Windows loader behavior.
- No host filesystem behavior.
- No segment registers.
- No segment override behavior.
- No `ASSUME`.
- No branch/control-flow implementation.

---

## 61M. Phase 57M - MASM Segment and Group Symbol Diagnostics

### Goal

Add targeted diagnostics for MASM/object/linker segment and group names when they are used as addressable symbols or segment definitions.

This phase implements the segment/group symbol classification defined by Phase 57K - .CODE and MASM Segment Symbol Access Policy.

This phase does not implement `.code` memory access diagnostics. Those belong to Phase 57L - .CODE Memory Access Diagnostics.

### Behavior category

Parser diagnostics, symbol classification, and MASM compatibility classification.

Repository/archive milestone advances to:

```text
Phase 57M - MASM Segment and Group Symbol Diagnostics
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57M - MASM Segment and Group Symbol Diagnostics
```

because this phase changes source diagnostics for recognized MASM/object/linker symbol forms.

### Diagnostic code

Preferred code:

```text
unsupported-segment-symbol
```

If the implementation uses a more specific taxonomy, document the mapping in the milestone report. Examples:

```text
unsupported-code-segment-symbol
unsupported-data-segment-symbol
unsupported-group-symbol
```

Do not create multiple overlapping codes unless the distinction is documented and tested.

### Recognized unsupported names

Diagnose these names as unsupported segment/group symbols when they are used as addressable symbols or segment definitions:

```text
_TEXT
_DATA
_BSS
CONST
STACK
DGROUP
FLAT
```

The implementation may recognize additional conventional segment/group names if doing so is small and documented. Do not implement broad segment modeling.

### Required rejected source forms

Diagnose these forms:

```asm
mov eax, OFFSET _TEXT
mov eax, OFFSET _DATA
mov eax, OFFSET _BSS
mov eax, OFFSET CONST
mov eax, OFFSET STACK
mov eax, OFFSET DGROUP
mov eax, OFFSET FLAT

mov eax, DWORD PTR [_TEXT]
mov eax, DWORD PTR [_DATA]
mov eax, DWORD PTR [_BSS]

_TEXT SEGMENT
_TEXT ENDS
_DATA SEGMENT
_DATA ENDS
CONST SEGMENT
CONST ENDS
```

Expected behavior:

- emit `unsupported-segment-symbol` or a documented equivalent;
- do not create a user symbol for the segment/group name;
- do not resolve the name to an internal VM region;
- do not expose `.code`, `.data`, `.DATA?`, `.CONST`, stack, heap, or any internal region address through these names;
- source with this diagnostic does not execute.

### Ordinary data labels remain supported

This phase must not break ordinary data labels:

```asm
.data
value DWORD 1

.code
main PROC
    mov eax, OFFSET value
    mov ebx, value
main ENDP
END main
```

The expected behavior for ordinary declared symbols remains unchanged.

### CASEMAP policy and user-symbol collision rules for segment/group names

Segment/group-symbol diagnostics must respect the active user-symbol lookup policy when the source form treats the token as a symbol. They must also avoid accidentally exposing internal VM regions or MASM object/linker concepts as ordinary user symbols.

Recognized unsupported segment/group spellings include, at minimum:

```text
_TEXT
_DATA
_BSS
CONST
STACK
DGROUP
FLAT
```

The implementation may recognize additional conventional segment/group names, but it must not add broad segment modeling.

Default and `OPTION CASEMAP:ALL` behavior:

- `_TEXT`, `_text`, and `_Text` are the same spelling for lookup and diagnostic purposes.
- `_DATA`, `_data`, and `_Data` are the same spelling for lookup and diagnostic purposes.
- `_BSS`, `_bss`, and `_Bss` are the same spelling for lookup and diagnostic purposes.
- `CONST`, `const`, and `Const` are the same spelling for lookup and diagnostic purposes.
- `STACK`, `stack`, and `Stack` are the same spelling for lookup and diagnostic purposes.
- `DGROUP`, `dgroup`, and `DGroup` are the same spelling for lookup and diagnostic purposes.
- `FLAT`, `flat`, and `Flat` are the same spelling for lookup and diagnostic purposes.
- Exact or case-variant use of a recognized segment/group name as an addressable symbol must report `unsupported-segment-symbol` or a documented equivalent.
- A user-defined data label, code label, procedure name, or alias whose spelling collides with a recognized segment/group name under `CASEMAP:ALL` must not silently replace the diagnostic-only segment/group classification.

`OPTION CASEMAP:NONE` behavior:

- Exact unsupported segment/group spellings such as `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` must still be diagnosed when used as segment/group names or addressable linker-style symbols.
- A different-case spelling may be accepted as an ordinary user-defined symbol only if the implementation deliberately permits it, the spelling does not exactly match a recognized segment/group name, and the milestone report documents the rule.
- If the implementation chooses to reject different-case spellings as reserved-like names even under `CASEMAP:NONE`, that is allowed only if the behavior is documented and covered by tests.
- Do not leave different-case behavior implicit.

Definition-time behavior:

- The parser must not create ordinary user symbols for exact recognized segment/group declarations such as:

```asm
_TEXT SEGMENT
_TEXT ENDS
_DATA SEGMENT
_DATA ENDS
_BSS SEGMENT
_BSS ENDS
CONST SEGMENT
CONST ENDS
STACK SEGMENT
STACK ENDS
DGROUP GROUP _DATA, _BSS
```

- These forms must report `unsupported-segment-symbol` or a documented equivalent.
- These forms must not create internal `.code`, `.data`, `.DATA?`, `.CONST`, stack, heap, linker group, PE section, COFF section, OMF segment, relocation, or loader metadata.

Reference-time behavior:

- References such as the following must report `unsupported-segment-symbol` or a documented equivalent when the name is recognized according to the active policy:

```asm
mov eax, OFFSET _TEXT
mov eax, OFFSET _DATA
mov eax, OFFSET _BSS
mov eax, OFFSET CONST
mov eax, OFFSET STACK
mov eax, OFFSET DGROUP
mov eax, OFFSET FLAT

mov eax, DWORD PTR [_TEXT]
mov eax, DWORD PTR [_DATA]
mov eax, DWORD PTR [_BSS]
mov eax, DWORD PTR [CONST]
```

- These names must not resolve to internal simulator layout regions.
- These names must not expose `.code`, `.data`, `.DATA?`, `.CONST`, stack, heap, or any other internal VM region address.
- These names must not become shorthand for `OFFSET` of a simulator section.

Ordinary data labels remain supported when they do not collide with recognized segment/group names under the active policy:

```asm
.data
value DWORD 1

.code
main PROC
    mov eax, OFFSET value
    mov ebx, value
main ENDP
END main
```

The milestone report must explicitly state:

- whether exact recognized names are rejected at definition time;
- whether exact recognized names are rejected at reference time;
- how `CASEMAP:ALL` handles case variants;
- how `CASEMAP:NONE` handles different-case user-defined spellings;
- which diagnostic code or diagnostic-code family was used;
- whether any ordinary user-symbol spelling was intentionally permitted despite resembling a segment/group name.

Required tests:

- exact recognized names at definition time;
- exact recognized names at reference time;
- case variants under default `CASEMAP:ALL`;
- case variants under explicit `OPTION CASEMAP:ALL`;
- exact recognized names under `OPTION CASEMAP:NONE`;
- different-case spellings under `OPTION CASEMAP:NONE`, with the expected behavior matching the documented implementation choice;
- ordinary non-colliding data labels proving normal symbol lookup still works.

### Diagnostic wording

Example for `_TEXT`:

```text
`_TEXT` is a MASM/object segment symbol. MASM32 Educational Mode does not expose linker segment symbols or readable `.code` / section images.
```

Example for `_DATA`:

```text
`_DATA` is a MASM/object data-segment symbol. Use declared data labels instead; MASM32 Educational Mode does not expose linker segment symbols.
```

Example for `DGROUP`:

```text
`DGROUP` is a MASM memory-model group concept. MASM32 Educational Mode uses simulator-defined flat memory regions and does not expose linker groups as addressable symbols.
```

Exact wording may differ, but rendered Simulator Messages tests must cover the final wording.

### Required parser tests

Add parser tests for:

- `mov eax, OFFSET _TEXT`;
- `mov eax, OFFSET _DATA`;
- `mov eax, OFFSET _BSS`;
- `mov eax, OFFSET CONST`;
- `mov eax, OFFSET STACK`;
- `mov eax, OFFSET DGROUP`;
- `mov eax, OFFSET FLAT`;
- `mov eax, DWORD PTR [_TEXT]`;
- `mov eax, DWORD PTR [_DATA]`;
- `_TEXT SEGMENT`;
- `_TEXT ENDS`;
- `_DATA SEGMENT`;
- `_DATA ENDS`.

### Required CASEMAP tests

Add tests for:

- default/CASEMAP:ALL recognizes `_text` as `_TEXT`;
- default/CASEMAP:ALL recognizes `_data` as `_DATA`;
- default/CASEMAP:ALL recognizes `dgroup` as `DGROUP`;
- `OPTION CASEMAP:NONE` exact spelling `_TEXT` is diagnosed;
- `OPTION CASEMAP:NONE` different-case user symbol behavior is documented and tested;
- ordinary declared data label still works.

### Required source-run tests

Add source-run tests proving:

- source with unsupported segment symbol refuses execution;
- source with segment definition form refuses execution;
- no Program Console output is produced;
- no `execution-complete` is emitted after assembly diagnostics;
- ordinary declared data labels remain executable.

### Required rendered-message tests

Add exact rendered Simulator Messages tests for:

- `_TEXT` unsupported segment symbol;
- `_DATA` unsupported segment symbol;
- `_BSS` unsupported segment symbol;
- `CONST` unsupported segment symbol;
- `DGROUP` unsupported group symbol;
- `_TEXT SEGMENT` unsupported segment definition.

Rendered messages must include source line, column, byte offset, and span length where available.

### Documentation updates

Update supported syntax and troubleshooting documentation to say:

- `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` are not exposed as addressable symbols;
- MASM segment definitions such as `_TEXT SEGMENT` are unsupported;
- users should use declared data labels for data addresses;
- `.code` is not exposed through `_TEXT`;
- `.data` is not exposed through `_DATA`;
- `.DATA?` is not exposed through `_BSS`;
- linker groups such as `DGROUP` are not modeled.

### Acceptance criteria

- MASM segment/group symbols are rejected with targeted diagnostics.
- Segment definition forms are rejected with targeted diagnostics.
- Ordinary data labels remain usable.
- CASEMAP behavior is tested.
- Source with these diagnostics refuses execution.
- No internal VM region is exposed through a segment/group symbol.
- No segment modeling is implemented.

### Non-goals

- No `.code` memory access diagnostics; Phase 57L owns those.
- No segment registers.
- No segment overrides.
- No `ASSUME`.
- No OMF/COFF segment modeling.
- No linker groups.
- No PE section layout.
- No relocation records.
- No object files.
- No linker behavior.
- No host filesystem behavior.

## 61N. Phase 57N - Zero-Operand NOP Audit, Repair, and Regression Hardening

### Goal

Audit, verify, repair if necessary, and harden the existing zero-operand `nop` instruction behavior.

Phase 20 - Exchange, Negation, and NOP already specified zero-operand `nop` as an implemented instruction. However, this phase exists because `nop` behavior has been observed to fail in some paths, or may not be covered strongly enough by parser, IR, executor, source-run, browser, protocol, documentation, and rendered-message tests.

This phase must not assume either of these extremes:

- that `nop` is completely absent and should be reimplemented from scratch; or
- that `nop` is fully correct merely because Phase 20 listed it.

Instead, the assistant must inspect the current repository implementation and tests, determine which paths already work, preserve working behavior, repair broken or incomplete paths, and add regression coverage that prevents future `nop` regressions.

This is an audit/repair/hardening phase, not a blind new implementation phase, and it must not be used as a reason to skip zero-operand `nop` verification.

Explicit-width memory-looking NOP encoding-operand forms remain deferred to Phase 57O - Explicit-Width NOP Encoding-Operand Forms.

### Behavior category

Corrective runtime/source instruction audit and hardening.

Repository/archive milestone advances to:

```text
Phase 57N - Zero-Operand NOP Audit, Repair, and Regression Hardening
```

Runtime/source-run MASM behavior phase handling depends on the audit result:

- If zero-operand `nop` already parses, lowers, executes, appears in source-run/browser-visible behavior, and has correct no-op semantics, then this phase is audit/test/documentation hardening. In that case, do not advance runtime/source-run MASM behavior metadata merely because Phase 57N was completed.
- If zero-operand `nop` is absent, unreachable, rejected, incorrectly lowered, incorrectly executed, missing from source-run/browser-visible behavior, or incorrectly documented as unsupported, then this phase is a corrective runtime-visible behavior phase. In that case, runtime/source-run MASM behavior metadata may advance to Phase 57N - Zero-Operand NOP Audit, Repair, and Regression Hardening, because the phase actually made a source instruction usable or correct.
- If only invalid-form diagnostic wording changes, the milestone report must state that the phase changed user-facing diagnostics but did not add new accepted MASM syntax.

The milestone report must state which case occurred and must list the evidence used, such as parser tests, executor tests, source-run tests, rendered-message tests, or manual browser smoke testing.

### Required audit checklist

Before changing behavior, inspect and report whether the current repository has:

- lexer handling that permits `nop` to reach parser instruction recognition;
- parser recognition for zero-operand `nop`;
- rejection of operand-bearing `nop` forms before Phase 57O;
- an IR opcode or equivalent executor representation for `nop`;
- opcode-name/debug-name handling for `nop`, if the current IR layer has opcode-name helpers;
- source-location metadata for `nop`;
- executor handling for `nop`;
- source-run JSON behavior for programs containing `nop`;
- browser/worker behavior for programs containing `nop`;
- supported-syntax documentation for zero-operand `nop`;
- parser tests;
- executor or source-run tests;
- rendered Simulator Messages tests for rejected operand forms;
- instruction-limit accounting coverage.

Do not replace existing working code merely to satisfy a preferred internal shape. Small repair is preferred over rewrite.

### Accepted syntax to verify or repair

Zero-operand `nop` must be accepted case-insensitively:

```asm
nop
NOP
NoP
```

### Rejected syntax before Phase 57O

These operand forms must remain rejected in Phase 57N:

```asm
nop eax
nop ax
nop al
nop 1
nop eax, ebx
nop [eax]
nop BYTE PTR [eax]
nop WORD PTR [eax]
nop DWORD PTR [eax]
nop QWORD PTR [eax]
nop SQWORD PTR [eax]
```

Expected behavior:

- register operands are invalid;
- immediate operands are invalid;
- two-operand forms are invalid;
- memory-looking forms are intentionally deferred to Phase 57O - Explicit-Width NOP Encoding-Operand Forms;
- QWORD/SQWORD forms remain unavailable in MASM32 Educational Mode and must not imply executable 64-bit memory behavior.

Rejected-form diagnostics must use stable wording. They must not say `not supported in this phase`, `unsupported by the current milestone`, `not implemented in this milestone`, or similar milestone-relative wording.

Recommended diagnostic wording:

```text
NOP operand form is not accepted. Zero-operand `nop` is supported; explicit BYTE/WORD/DWORD PTR NOP encoding-operand forms are deferred to Phase 57O - Explicit-Width NOP Encoding-Operand Forms.
```

### Required runtime semantics

Accepted zero-operand `nop` must have these semantics:

- no register mutation;
- no modeled flag mutation;
- no flag-validity metadata mutation;
- no memory read;
- no memory write;
- no planned memory access;
- no Program Console output;
- no Simulator Messages warning or notice merely because `nop` executed;
- no memory-change row;
- no change to uninitialized-origin metadata;
- execution continues to the next IR instruction;
- the instruction counts as one executed IR instruction for instruction-limit and future debugger/step accounting.

`nop` must preserve deterministic behavior.

### Source metadata

The IR instruction or equivalent execution representation for `nop` must preserve:

- mnemonic spelling or canonical mnemonic, according to current IR conventions;
- source line;
- source column;
- byte offset;
- span length;
- original source text where current IR metadata supports it.

If the current IR layer does not preserve one of these fields for any instruction, do not invent a `nop`-only metadata path. Document the existing limitation and preserve consistency with the current IR metadata model.

### Required parser tests

Add or verify parser tests for:

- lowercase `nop`;
- uppercase `NOP`;
- mixed-case `NoP`;
- `nop` inside a normal `.code` / `PROC` / `ENDP` / `END` program;
- rejected register operand;
- rejected immediate operand;
- rejected two-operand form;
- rejected memory-looking operand, with diagnostic wording that names Phase 57O - Explicit-Width NOP Encoding-Operand Forms as the future owner;
- rejected QWORD/SQWORD operand forms.

### Required executor/source-run tests

Add or verify source-run or executor tests proving:

```asm
.code
main PROC
    mov eax, 1
    nop
    inc eax
main ENDP
END main
```

Expected final `EAX = 2`.

Also prove that `nop`:

- preserves all general-purpose registers except normal execution accounting not displayed as architectural state;
- preserves modeled flags;
- preserves flag-validity metadata;
- preserves memory;
- emits no Program Console output;
- creates no memory-change rows;
- emits no warning or notice merely because it executed;
- counts toward the instruction execution limit.

Include at least one test where `nop` appears:

- before another instruction;
- after another instruction;
- between two mutating instructions;
- as the only executable instruction in a procedure body.

### Required diagnostic and rendered-message tests

Add structured and rendered Simulator Messages tests for invalid forms that produce user-visible diagnostics:

- `nop eax`;
- `nop 1`;
- `nop eax, ebx`;
- `nop DWORD PTR [eax]`.

Rendered-message tests must assert exact wording for at least one representative invalid operand form and must preserve structured source location fields for the `nop` mnemonic or offending operand, according to the project's current diagnostic-span policy.

### Documentation updates

Update supported syntax documentation only if it is stale or incomplete.

Document zero-operand `nop` as supported when the audit confirms or repairs support.

Do not document memory-looking `nop` forms until Phase 57O.

Do not document `nop` as producing opcode `90h`.

Do not document any real x86 opcode sequence.

### Acceptance criteria

- Existing zero-operand `nop` behavior has been audited.
- Any broken or missing zero-operand `nop` parser, IR, executor, source-run, browser-visible, or documentation path has been repaired.
- Zero-operand `nop` parses and executes as a true no-op.
- `nop` counts as one executed instruction.
- Operand forms are rejected or deferred with stable diagnostics.
- Exact rendered Simulator Messages tests cover invalid operand forms.
- Existing diagnostics and memory validation are not weakened.
- Documentation does not imply `.code` byte-image support or x86 opcode emission.
- The milestone report states whether this phase merely hardened existing behavior or repaired runtime-visible behavior.

### Non-goals

- No explicit-width NOP encoding-operand forms.
- No real x86 opcode bytes.
- No `.code` memory image.
- No `ALIGN`.
- No `EVEN`.
- No raw byte emission into `.code`.
- No PE `.text` layout.
- No object files.
- No linker behavior.
- No relocation behavior.
- No disassembly.
- No timing, cycle, pipeline, or CPU-family behavior.

---

## 61O. Phase 57O - Explicit-Width NOP Encoding-Operand Forms

### Goal

Accept selected explicit-width memory-looking `nop` forms as source-level encoding-operand no-ops.

This phase builds on the zero-operand `nop` behavior specified by Phase 20 - Exchange, Negation, and NOP and audited, repaired if necessary, and hardened by Phase 57N - Zero-Operand NOP Audit, Repair, and Regression Hardening.

The key rule is that accepted `nop` operands in this phase are **encoding operands**, not simulated memory operands.

### Behavior category

Runtime/source instruction behavior.

Repository/archive milestone advances to:

```text
Phase 57O - Explicit-Width NOP Encoding-Operand Forms
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57O - Explicit-Width NOP Encoding-Operand Forms
```

because this phase adds accepted executable source forms.

### Accepted syntax

Accept selected explicit-width forms where the operand uses the memory-addressing grammar already available in the current repository.

Minimum accepted forms:

```asm
nop BYTE PTR [eax]
nop WORD PTR [eax]
nop DWORD PTR [eax]

nop BYTE PTR [esi]
nop WORD PTR [esi]
nop DWORD PTR [esi]

nop BYTE PTR [eax + 4]
nop WORD PTR [eax + 4]
nop DWORD PTR [eax + 4]
```

If the current parser already supports signed displacement spellings, equivalent negative displacement forms may be accepted:

```asm
nop DWORD PTR [eax - 4]
```

If the current parser already supports symbol-plus-register memory forms, this phase may accept them only if doing so reuses existing parsing and does not implement new addressing syntax:

```asm
nop DWORD PTR [array + esi]
nop DWORD PTR array[esi]
```

Do not add new addressing syntax merely for `nop`.

### Encoding-operand rule

For accepted `nop` operands, the operand is an encoding operand, not a simulated memory operand.

For accepted `nop` encoding operands:

- parse the operand enough to classify the source form;
- preserve source location metadata;
- do not evaluate the final effective address at runtime;
- do not read memory;
- do not write memory;
- do not call checked VM memory read helpers;
- do not call checked VM memory write helpers;
- do not perform planned-read validation;
- do not perform planned-write validation;
- do not emit uninitialized-read diagnostics;
- do not emit declared-object diagnostics;
- do not emit section-capacity diagnostics;
- do not emit section-image diagnostics;
- do not emit `.CONST` permission diagnostics;
- do not create memory-change rows.

This exception applies only to accepted `nop` encoding operands. It must not weaken the standing rule that real memory-capable instructions use checked memory helpers and planned-access validation.

### Rejected syntax

Reject or diagnose:

```asm
nop eax
nop ax
nop al
nop 1
nop eax, ebx
nop BYTE PTR [eax], 1
nop QWORD PTR [eax]
nop SQWORD PTR [eax]
nop [eax]
```

Expected behavior:

- register operands remain invalid;
- immediate operands remain invalid;
- two-operand forms remain invalid;
- untyped memory-looking operands such as `nop [eax]` are rejected as ambiguous or invalid because this phase only accepts explicit supported width forms;
- QWORD/SQWORD operands remain unsupported in MASM32 Educational Mode;
- malformed memory syntax remains a normal parser diagnostic.

### Runtime semantics

Every accepted `nop` form has identical runtime semantics:

- no register mutation;
- no modeled flag mutation;
- no flag-validity metadata mutation;
- no memory mutation;
- no memory read;
- no planned memory access;
- no Program Console output;
- no Simulator Messages warning;
- no memory-change row;
- no change to existing uninitialized-origin metadata;
- execution continues to the next IR instruction;
- the instruction counts as one executed IR instruction for execution-limit and future step/debug accounting.

### Required parser tests

Add parser tests for:

- accepted `BYTE PTR [reg32]`;
- accepted `WORD PTR [reg32]`;
- accepted `DWORD PTR [reg32]`;
- accepted displacement form already supported by current memory grammar;
- rejected register operand;
- rejected immediate operand;
- rejected two-operand form;
- rejected untyped memory-looking form `nop [eax]`;
- rejected `QWORD PTR`;
- rejected `SQWORD PTR`.

### Required executor/source-run tests

Add source-run or executor tests proving:

- `nop DWORD PTR [eax]` does not read from `[eax]`;
- `nop DWORD PTR [eax]` does not fail when `EAX` contains an address that would be invalid for a real memory read;
- `nop DWORD PTR [eax]` does not emit uninitialized-read diagnostics;
- `nop DWORD PTR [eax]` does not emit declared-object, section-capacity, section-image, or `.CONST` diagnostics;
- `nop DWORD PTR [eax]` preserves registers;
- `nop DWORD PTR [eax]` preserves modeled flags;
- `nop DWORD PTR [eax]` preserves flag-validity metadata;
- `nop DWORD PTR [eax]` preserves memory;
- `nop DWORD PTR [eax]` emits no Program Console output;
- `nop DWORD PTR [eax]` creates no memory-change rows;
- `nop DWORD PTR [eax]` counts toward the instruction execution limit.

### Required diagnostic and rendered-message tests

Add structured and rendered Simulator Messages tests for invalid forms:

- `nop eax`;
- `nop 1`;
- `nop eax, ebx`;
- `nop [eax]`;
- `nop QWORD PTR [eax]` or `nop SQWORD PTR [eax]`.

Diagnostic wording must explain that zero-operand `nop` and supported explicit BYTE/WORD/DWORD PTR encoding-operand forms are accepted, but real x86 byte encoding is not emitted.

Example diagnostic wording:

```text
NOP with this operand form is not supported. Zero-operand `nop` and explicit BYTE/WORD/DWORD PTR encoding-operand forms are supported as IR-level no-ops; real x86 byte encoding is not emitted.
```

Exact wording may differ, but rendered tests must cover it.

### Documentation updates

Update supported syntax documentation to list:

```asm
nop
nop BYTE PTR [reg32]
nop WORD PTR [reg32]
nop DWORD PTR [reg32]
```

Document that accepted `nop` memory-looking operands are encoding operands only and do not perform memory access.

Do not document `nop` as producing opcode `90h`.

Do not document any real multi-byte x86 opcode sequence.

### Acceptance criteria

- Supported explicit-width source-level `nop` forms parse and execute as no-ops.
- Accepted `nop` memory-looking operands do not perform memory access.
- `nop` counts as one executed instruction.
- Existing diagnostics and memory validation are not weakened for other instructions.
- Documentation does not imply `.code` byte-image support or x86 opcode emission.

### Non-goals

- No real x86 opcode bytes.
- No `.code` memory image.
- No `ALIGN`.
- No `EVEN`.
- No `ORG`.
- No raw byte emission into `.code`.
- No PE `.text` layout.
- No object files.
- No linker behavior.
- No relocation behavior.
- No disassembly.
- No timing, cycle, pipeline, or CPU-family behavior.

---

## 61P. Phase 57P - Host Include Path Diagnostics

### Goal

Replace repeated low-level lexer `unexpected-character` diagnostics for MASM32 host include paths with clear unsupported-feature diagnostics.

This phase handles `INCLUDE` with host/path-like operands only. `INCLUDELIB` is handled separately by Phase 57Q - INCLUDELIB and External Library Diagnostics.

### Behavior category

Lexer/parser diagnostic quality and unsupported-feature classification.

Repository/archive milestone advances to:

```text
Phase 57P - Host Include Path Diagnostics
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57P - Host Include Path Diagnostics
```

because this phase changes source diagnostics for recognized MASM/source constructs.

### Problem this phase fixes

A common MASM32 source file may contain:

```asm
include \masm32\include\masm32.inc
include \masm32\include\kernel32.inc
```

The simulator should not emit one `unexpected-character` diagnostic per backslash.

Instead, it should emit one meaningful diagnostic per unsupported host include directive.

### Required recognition

Recognize enough of these line shapes to produce directive-level diagnostics:

```asm
include \masm32\include\masm32.inc
include \masm32\include\kernel32.inc
include C:\masm32\include\kernel32.inc
include ..\include\something.inc
include .\local.inc
include /usr/local/include/file.inc
```

Recognition may be line-oriented for this phase. It does not need to tokenize host paths as general expressions.

### Diagnostic categories

Use stable diagnostic codes.

Recommended codes:

```text
unsupported-host-include-path
unsupported-windows-api-include
unsupported-masm32-library-include
```

If the implementation prefers fewer codes, this acceptable minimum taxonomy may be used:

```text
unsupported-host-include-path
```

However, rendered messages must still distinguish Windows/API and MASM32 SDK cases when recognized.

### Diagnostic behavior

For host include paths:

```asm
include \masm32\include\masm32.inc
```

Emit a diagnostic similar to:

```text
Host filesystem include paths such as `\masm32\include\masm32.inc` are not supported. This browser simulator does not read the local MASM32 SDK; use supported virtual includes only.
```

For Windows API includes:

```asm
include \masm32\include\kernel32.inc
```

Emit a diagnostic similar to:

```text
`kernel32.inc` describes Windows API symbols. Windows API execution is outside this simulator; PE loading, imports, and WinAPI calls are not performed.
```

Exact wording may differ, but the message must be stable, actionable, and covered by exact rendered Simulator Messages tests.

### Virtual include behavior

Preserve existing virtual include behavior.

For example, if the current repository supports:

```asm
INCLUDE Irvine32.inc
```

that behavior must continue unchanged.

Do not reclassify supported virtual includes as host include paths merely because they use `INCLUDE`.

Do not add new virtual include behavior in this phase unless it is only documentation or classification of a known unsupported virtual name.

### Lexer behavior

The lexer should not emit repeated `unexpected-character` diagnostics for backslashes inside recognized unsupported include directive lines.

Allowed implementation strategies:

- recognize `include` lines before ordinary tokenization when a path-like tail follows;
- teach the lexer a path-tail token only in directive-tail context;
- add parser recovery that consumes the rest of the line after `include` and emits one directive-level diagnostic.

Do not make backslash a general expression token merely for this phase.

Do not allow path tokens to leak into ordinary instruction operands.

### Required tests

Add lexer/parser/source-run tests for:

- `include \masm32\include\masm32.inc`;
- `include \masm32\include\kernel32.inc`;
- `include C:\masm32\include\kernel32.inc`;
- `include ..\include\file.inc`;
- several host include directives in one file;
- supported virtual include still works;
- unsupported path diagnostics suppress repeated path-separator `unexpected-character` diagnostics;
- source line, column, byte offset, and span length point at the directive or unsupported path according to the implementation's chosen diagnostic convention.

### Required rendered-message tests

Add exact rendered Simulator Messages tests for:

- unsupported host include path;
- unsupported Windows API include;
- multi-diagnostic host include source.

### Documentation updates

Update supported syntax and diagnostics documentation to say:

- host filesystem include paths are unsupported;
- supported virtual includes are simulator-defined and do not read the host filesystem;
- Windows API includes are outside MASM32 Educational Mode.

### Acceptance criteria

- MASM32 host include paths produce meaningful unsupported-feature diagnostics.
- Repeated backslash `unexpected-character` noise is eliminated for recognized include directive lines.
- Supported virtual include behavior is preserved.
- No host file loading is implemented.
- No WinAPI execution is implemented.

### Non-goals

- No host filesystem access.
- No MASM32 SDK file loading.
- No include search paths.
- No `INCLUDELIB` handling.
- No object files.
- No PE loading.
- No imports.
- No WinAPI execution.
- No macro expansion.

---

## 61Q. Phase 57Q - INCLUDELIB and External Library Diagnostics

### Goal

Replace generic parser or lexer diagnostics for `INCLUDELIB` with clear unsupported linker/library diagnostics.

This phase handles `INCLUDELIB` only. Host `INCLUDE` paths are handled by Phase 57P - Host Include Path Diagnostics.

### Behavior category

Parser diagnostic quality and non-goal classification.

Repository/archive milestone advances to:

```text
Phase 57Q - INCLUDELIB and External Library Diagnostics
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57Q - INCLUDELIB and External Library Diagnostics
```

because this phase changes source diagnostics for recognized MASM/source constructs.

### Required recognition

Recognize enough of these line shapes to produce directive-level diagnostics:

```asm
includelib \masm32\lib\masm32.lib
includelib \masm32\lib\kernel32.lib
includelib kernel32.lib
includelib masm32.lib
includelib C:\masm32\lib\kernel32.lib
```

Recognition may be line-oriented for this phase. It does not need to tokenize library paths as general expressions.

### Diagnostic categories

Use stable diagnostic codes.

Recommended codes:

```text
unsupported-includelib
unsupported-windows-api-library
unsupported-masm32-library
```

If the implementation prefers fewer codes, this acceptable minimum taxonomy may be used:

```text
unsupported-includelib
```

However, rendered messages must still distinguish linker/library behavior from host include loading.

### Diagnostic behavior

Diagnostics for `INCLUDELIB` must classify linker and import-library behavior as outside MASM32 Educational Mode.

Do not say or imply that real object-file linking, import-library loading, PE import processing, host `.lib` loading, or WinAPI execution is merely "not supported yet" unless the source-of-truth documents are deliberately changed to make such behavior a future roadmap item.

For a general `INCLUDELIB` directive:

```asm
includelib somefile.lib
```

Emit a diagnostic similar to:

```text
INCLUDELIB is a linker/import-library directive. MASM32 Educational Mode does not link object files, load .lib files, process PE imports, or execute external library routines.
```

For Windows API library paths:

```asm
includelib \masm32\lib\kernel32.lib
includelib kernel32.lib
includelib C:\masm32\lib\kernel32.lib
```

Emit a diagnostic similar to:

```text
kernel32.lib is a Windows import library. This simulator does not load PE imports or execute WinAPI routines.
```

For MASM32 SDK library paths:

```asm
includelib \masm32\lib\masm32.lib
includelib masm32.lib
```

Emit a diagnostic similar to:

```text
masm32.lib is an external MASM32 library. This browser simulator does not link MASM32 .lib files or load external library routines; only documented virtual Irvine32 behavior is provided.
```

Exact wording may differ, but the message must be stable, actionable, and covered by exact rendered Simulator Messages tests.

The message must distinguish `INCLUDELIB` from `INCLUDE`:

- host `INCLUDE` paths are rejected because the browser simulator does not read local MASM32 SDK include files;
- `INCLUDELIB` is rejected because the simulator does not link libraries, process object files, load PE imports, or execute external routines.

The diagnostic must not create linker/import metadata that could be misread as future executable support.

### Required behavior

- `INCLUDELIB` diagnostics must be assembly diagnostics.
- Source containing `INCLUDELIB` must not execute.
- `INCLUDELIB` must not be accepted as a no-op.
- `INCLUDELIB` must not be treated as a virtual include.
- `INCLUDELIB` must not attempt to read host files.
- `INCLUDELIB` must not create linker/import metadata that implies future execution.
- `INCLUDELIB` diagnostics must not use vague future-promising wording such as "not supported yet" unless they immediately clarify that real linker/import-library behavior is outside the simulator boundary.
- `INCLUDELIB` must not be documented as a planned runtime feature unless a later reviewed spec/guide revision deliberately changes the non-goal boundary.

### Required tests

Add lexer/parser/source-run tests for:

- `includelib \masm32\lib\masm32.lib`;
- `includelib \masm32\lib\kernel32.lib`;
- `includelib kernel32.lib`;
- `includelib masm32.lib`;
- several `INCLUDELIB` directives in one file;
- source containing `INCLUDELIB` refuses execution;
- no Program Console output is produced;
- no repeated path-separator `unexpected-character` diagnostics are emitted for recognized library path tails.

### Required rendered-message tests

Add exact rendered Simulator Messages tests for:

- unsupported `INCLUDELIB`;
- MASM32 library linking diagnostic;
- Windows API library diagnostic where recognized;
- multi-diagnostic library source.

### Documentation updates

Update supported syntax and diagnostics documentation to say:

- `INCLUDELIB` is unsupported because linking is outside the simulator;
- MASM32 `.lib` and Windows `.lib` files are not loaded;
- PE imports and object linking are not simulated.

### Acceptance criteria

- `INCLUDELIB` produces meaningful linker non-goal diagnostics.
- Repeated path-character diagnostics are eliminated for recognized library directive lines.
- No library linking is implemented.
- No object/import/PE behavior is implemented.
- Source containing `INCLUDELIB` does not execute.

### Non-goals

- No host filesystem access.
- No library search paths.
- No object files.
- No import tables.
- No PE loading.
- No linker.
- No WinAPI execution.
- No MASM32 runtime loading.
- No macro expansion.

---

## 61R. Phase 57R - Unsupported INVOKE, ADDR, and External Routine Diagnostics

### Goal

Add clear unsupported-feature diagnostics for common MASM32 invocation syntax and external routine names without implementing invocation behavior.

This phase handles `INVOKE`, `ADDR`, and recognized external routine categories such as `StdOut`, `crt_printf`, and `ExitProcess`.

### Behavior category

Parser diagnostic quality and unsupported-feature classification.

Repository/archive milestone advances to:

```text
Phase 57R - Unsupported INVOKE, ADDR, and External Routine Diagnostics
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57R - Unsupported INVOKE, ADDR, and External Routine Diagnostics
```

because this phase changes source diagnostics for recognized MASM/source constructs.

### Required recognition

Recognize common invocation lines enough to produce stable unsupported-feature diagnostics:

```asm
invoke StdOut, addr titleMsg
invoke crt_printf, addr numberFmt, counter
invoke ExitProcess, 0
```

Recognition may be line-oriented or parser-level. This phase does not need to fully parse all `INVOKE` argument expressions.

### Diagnostic categories

Recommended diagnostic codes:

```text
unsupported-invoke
unsupported-addr
unsupported-external-routine
unsupported-winapi-execution
unsupported-masm32-runtime-routine
unsupported-crt-routine
```

The implementation may use fewer codes if existing taxonomy already has stable equivalents, but rendered messages must distinguish these concepts when recognized:

- `INVOKE` syntax is not implemented;
- `ADDR` operands are not implemented;
- `StdOut` is an external MASM32 runtime-style routine, not implemented here;
- `crt_printf` is C runtime-style output, not implemented here;
- `ExitProcess` is WinAPI/external process termination behavior, not implemented here.

### Required behavior

For:

```asm
invoke StdOut, addr titleMsg
```

Expected diagnostics should explain:

- `INVOKE` is unsupported or deferred;
- `ADDR` is unsupported or deferred if reported separately;
- `StdOut` is not an implemented virtual routine.

For:

```asm
invoke crt_printf, addr numberFmt, counter
```

Expected diagnostics should explain:

- `INVOKE` is unsupported or deferred;
- C runtime formatted output is not implemented.

For:

```asm
invoke ExitProcess, 0
```

Expected diagnostics should explain:

- `ExitProcess` is WinAPI/external process behavior;
- WinAPI execution is outside the simulator boundary;
- users should not confuse this with the already supported virtual Irvine32 `exit` terminator, if present.

### Relationship to Irvine32 registry

If the current repository has a virtual Irvine32 routine registry, do not bypass it for Irvine32 names.

External MASM32, CRT, and WinAPI routine classification may use the same registry if appropriate, but this phase must not create a second contradictory taxonomy.

### Recovery behavior

A source containing several `INVOKE` lines should produce useful diagnostics for each line when safe.

Avoid cascaded diagnostics from every token inside an unsupported `INVOKE` line if the whole line has already been classified as unsupported.

### Required tests

Add parser/source-run tests for:

- `invoke StdOut, addr titleMsg`;
- `invoke crt_printf, addr numberFmt, counter`;
- `invoke ExitProcess, 0`;
- several `INVOKE` lines in one source;
- source containing `INVOKE` refuses execution;
- no Program Console output is produced;
- `ExitProcess` is classified as WinAPI/external behavior, not as Irvine32 `exit`;
- if `INCLUDE Irvine32.inc` and `exit` exist, virtual `exit` behavior remains unchanged.

### Required rendered-message tests

Add exact rendered Simulator Messages tests for:

- unsupported `INVOKE`;
- unsupported `ADDR`;
- unsupported external MASM32 runtime routine;
- unsupported CRT routine;
- unsupported WinAPI `ExitProcess`.

If the implementation reports one combined diagnostic per `INVOKE` line rather than separate `INVOKE`/`ADDR`/routine diagnostics, the rendered test must prove the combined message contains all relevant explanation.

### Documentation updates

Update supported syntax and diagnostics documentation to say:

- `INVOKE` is not implemented yet;
- `ADDR` is not implemented yet unless already implemented by a prior phase;
- MASM32 runtime routines such as `StdOut` are not executable unless a specific virtual routine phase implements them;
- C runtime routines such as `crt_printf` are not executable;
- WinAPI routines such as `ExitProcess` are outside the simulator boundary.

### Acceptance criteria

- Common `INVOKE` lines produce useful unsupported-feature diagnostics.
- `ExitProcess` is classified as WinAPI/external behavior.
- No invocation lowering is implemented.
- No stack/calling-convention behavior is implemented.
- Source containing unsupported invocation diagnostics does not execute.

### Non-goals

- No `INVOKE` implementation.
- No `ADDR` implementation.
- No calling convention behavior.
- No stack setup.
- No `StdOut` implementation.
- No `crt_printf` implementation.
- No `ExitProcess` implementation.
- No WinAPI execution.
- No PE imports.
- No linker behavior.

---

## 61S. Phase 57S - Unsupported High-Level Flow Diagnostics

### Goal

Add clear unsupported-feature diagnostics and recovery for high-level MASM flow constructs without implementing their lowering or execution.

This phase handles constructs such as `.IF`, `.ELSE`, and `.ENDIF`.

### Behavior category

Parser diagnostic quality and unsupported-feature recovery.

Repository/archive milestone advances to:

```text
Phase 57S - Unsupported High-Level Flow Diagnostics
```

Runtime/source-run MASM behavior phase advances to:

```text
Phase 57S - Unsupported High-Level Flow Diagnostics
```

because this phase changes source diagnostics for recognized MASM/source constructs.

### Required recognition

Recognize these high-level flow constructs as unsupported or deferred when not already implemented:

```asm
.IF eax == 0
.ELSE
.ENDIF
```

Where practical, also recognize:

```asm
.ELSEIF condition
.WHILE condition
.ENDW
.REPEAT
.UNTIL condition
.UNTILCXZ
.BREAK
.CONTINUE
```

Do not expand expression parsing merely to fully understand the condition. Recognize the directive family and consume enough of the line or block to avoid noisy cascades.

### Diagnostic categories

Recommended diagnostic codes:

```text
unsupported-high-level-if
unsupported-high-level-else
unsupported-high-level-endif
unsupported-high-level-while
unsupported-high-level-repeat
unsupported-high-level-flow
```

The implementation may use fewer codes if it already has a stable equivalent taxonomy, but rendered messages must identify the construct family.

### Required behavior

For:

```asm
.IF eax == 0
    mov ebx, 1
.ELSE
    mov ebx, 2
.ENDIF
```

Expected behavior:

- report `.IF` as unsupported high-level MASM flow;
- recover through `.ELSE` and `.ENDIF` if safe;
- avoid treating the contents as valid conditional execution;
- refuse execution because assembly diagnostics exist.

### Non-cascade rule

Avoid noisy cascaded diagnostics from inside a recognized unsupported high-level flow block where safe.

If the parser cannot safely recover, it may emit a fatal parser diagnostic, but the milestone report must identify the limitation.

### Required tests

Add parser/source-run tests for:

- `.IF` / `.ELSE` / `.ENDIF`;
- `.IF` without `.ELSE`;
- malformed or unbalanced high-level flow block;
- high-level flow block containing otherwise valid instructions;
- high-level flow block containing unsupported instructions;
- source containing high-level flow refuses execution;
- no Program Console output is produced.

### Required rendered-message tests

Add exact rendered Simulator Messages tests for:

- unsupported `.IF`;
- unsupported `.ELSE` / `.ENDIF` handling, if emitted separately;
- malformed high-level flow recovery, if applicable.

### Documentation updates

Update supported syntax and diagnostics documentation to say:

- high-level MASM flow constructs are recognized but not executable;
- they are not lowered to labels or branches;
- branch/control-flow phases will own lower-level control-flow behavior later.

### Acceptance criteria

- High-level flow constructs produce clear diagnostics.
- Diagnostics are source-located.
- Parser recovery avoids excessive cascades where safe.
- No high-level flow execution is implemented.
- No branch lowering is implemented.
- No expression parser expansion is implemented beyond what is needed for recognition.

### Non-goals

- No `.IF` lowering.
- No `.WHILE` lowering.
- No `.REPEAT` lowering.
- No conditional branch execution.
- No label generation.
- No expression parser expansion beyond recognition.
- No block execution semantics.

---

## 61T. Phase 57T - Playground Program Diagnostic-Recovery Smoke Fixtures

### Goal

Add realistic MASM32 playground-program smoke fixtures that prove the simulator reports useful unsupported-feature diagnostics instead of collapsing into low-level lexer/parser noise.

This is primarily a regression fixture and diagnostic-recovery verification phase. It must not implement the unsupported features in the playground program.

### Behavior category

Diagnostic recovery, fixture coverage, and documentation.

Repository/archive milestone advances to:

```text
Phase 57T - Playground Program Diagnostic-Recovery Smoke Fixtures
```

Runtime/source-run MASM behavior phase remains:

```text
Phase 57S - Unsupported High-Level Flow Diagnostics
```

unless this phase changes diagnostic behavior beyond adding tests/documentation. If it changes diagnostic behavior, the milestone report must identify the specific behavior and may advance the runtime/source-run MASM behavior phase.

### Playground fixture

Add a realistic unsupported MASM32 playground fixture based on this shape:

```asm
; ============================================
; MASM32 Playground Program
; ============================================

.386
.model flat, stdcall
option casemap:none

include \masm32\include\masm32.inc
include \masm32\include\kernel32.inc

includelib \masm32\lib\masm32.lib
includelib \masm32\lib\kernel32.lib

.data
    titleMsg   db "=== MASM32 Playground ===",13,10,0
    startMsg   db "Counting from 1 to 5",13,10,0
    evenMsg    db " -> even number",13,10,0
    oddMsg     db " -> odd number",13,10,0
    doneMsg    db 13,10,"Finished!",13,10,0

    numberFmt  db "Number: %d",13,10,0
    sumFmt     db 13,10,"Final sum = %d",13,10,0

    counter    dd 1
    total      dd 0

.code

AddToTotal PROC
    add total, eax
    ret
AddToTotal ENDP

start:
    invoke StdOut, addr titleMsg
    invoke StdOut, addr startMsg

main_loop:
    invoke crt_printf, addr numberFmt, counter

    mov eax, counter
    and eax, 1

    .IF eax == 0
        invoke StdOut, addr evenMsg
    .ELSE
        invoke StdOut, addr oddMsg
    .ENDIF

    mov eax, counter
    call AddToTotal

    inc counter

    cmp counter, 6
    jl main_loop

    invoke crt_printf, addr sumFmt, total
    invoke StdOut, addr doneMsg
    invoke ExitProcess, 0

end start
```

The exact fixture may be trimmed if needed to keep line/column diagnostics maintainable, but it must remain representative of a normal MASM32/Windows sample produced by AI tools or tutorials.

### Expected behavior

The fixture must not execute.

The simulator must report a concise set of useful diagnostics for unsupported feature families already covered by Phases 57P through 57S.

Expected diagnostic families include, when encountered and not already implemented:

```text
unsupported-host-include-path
unsupported-includelib
unsupported-invoke
unsupported-addr
unsupported-high-level-flow
unsupported-call
unsupported-ret
unsupported-conditional-jump
unsupported-winapi-execution
unsupported-external-routine
```

The exact diagnostic codes may differ if the existing implementation already uses stable equivalents. The milestone report must map the observed codes to the intended unsupported feature families.

### Diagnostic recovery policy

The fixture should prove that the simulator can recover from recognized unsupported constructs and continue scanning later lines when safe.

Requirements:

- report host include path problems at the include lines;
- report `INCLUDELIB` problems at the library lines;
- report `INVOKE` as unsupported or deferred;
- report `ADDR` as unsupported or deferred when it appears in unsupported invoke operands, if Phase 57R - Unsupported INVOKE, ADDR, and External Routine Diagnostics emits it separately;
- report `.IF`, `.ELSE`, and `.ENDIF` as unsupported high-level flow where current implementation does not support them;
- report `ret` as unsupported or deferred if procedure return is not implemented;
- report `call` as unsupported or deferred if procedure call is not implemented;
- report `cmp` or `jl` as unsupported/deferred if not implemented;
- report `ExitProcess`/WinAPI behavior as outside the simulator boundary when recognized;
- avoid cascades of meaningless secondary diagnostics inside already-reported unsupported constructs where safe.

### Noise limits

The fixture must not produce a wall of repeated character-level diagnostics.

Specifically:

- no repeated `unexpected-character` diagnostics for the backslashes in MASM32 include/library paths once Phase 57P - Host Include Path Diagnostics and Phase 57Q - INCLUDELIB and External Library Diagnostics are complete;
- no generic `lexer-failed` umbrella diagnostic when more specific diagnostics are available;
- no misleading diagnostic that suggests host filesystem includes, linker behavior, or WinAPI execution will be attempted;
- no Program Console output.

If the fixture still produces some lower-level diagnostics after the high-level diagnostics, the milestone report must classify them as either acceptable residual diagnostics or follow-up issues. The target should be concise feature-family diagnostics.


### Phase 57Q `INCLUDELIB` wording inheritance

Phase 57T must inherit the Phase 57Q `INCLUDELIB` non-goal wording.

If a realistic playground fixture includes `INCLUDELIB`, the rendered message must classify it as linker/import-library behavior outside MASM32 Educational Mode.

The message must not classify `INCLUDELIB` as:

- a missing local file;
- a host include-loading problem;
- a temporary runtime limitation;
- a feature that will later link `.lib` files;
- a feature that will later load PE imports;
- a feature that will later execute WinAPI routines.

If a playground fixture includes both `INCLUDE` and `INCLUDELIB`, the diagnostics must distinguish the two:

- host `INCLUDE` paths are rejected because the browser simulator does not read local MASM32 SDK include files;
- `INCLUDELIB` is rejected because the simulator does not link libraries, process object files, load PE imports, or execute external routines.

Recommended fixture line:

```asm
includelib \masm32\lib\kernel32.lib
```

or:

```asm
includelib kernel32.lib
```

Required rendered-message assertion:

- the exact rendered message must state that `kernel32.lib` or `INCLUDELIB` is external linker/import-library behavior outside MASM32 Educational Mode;
- the exact rendered message must not imply host filesystem probing, real linking, PE loading, or WinAPI execution.


### Required tests

Add source-run fixture tests for:

- full playground fixture or representative trimmed fixture;
- diagnostics include host include path;
- diagnostics include `INCLUDELIB`;
- diagnostics include `INVOKE` or equivalent unsupported routine invocation classification;
- diagnostics include high-level `.IF` flow unsupported classification;
- diagnostics include `call`/`ret` unsupported classification if still unimplemented;
- diagnostics include conditional jump unsupported classification if still unimplemented;
- diagnostics include WinAPI/external routine boundary if recognized;
- source-run refuses execution;
- no Program Console output;
- no execution-complete message after assembly errors;
- repeated backslash `unexpected-character` diagnostics are absent.

Add rendered Simulator Messages tests for the fixture or for stable smaller fixtures covering the same feature families.

### Fixture-size rule

If the full playground fixture is too large for stable line/column maintenance, split it into named smaller fixtures:

```text
playground_host_includes.asm
playground_includelib.asm
playground_invoke_addr.asm
playground_high_level_flow.asm
playground_call_ret_jcc.asm
playground_winapi_exitprocess.asm
```

The aggregate Phase 57T reporting must list which fixture family was run.

Do not weaken assertions merely because the full fixture is large. Prefer smaller named fixtures with exact diagnostics.

### Documentation updates

Update supported syntax or troubleshooting documentation with a short section similar to:

```text
Many AI-generated MASM32 examples use host include paths, INCLUDELIB, INVOKE, WinAPI calls, high-level .IF/.ELSE/.ENDIF flow, CALL/RET, and conditional jumps. These are real MASM32 patterns, but they are outside or ahead of the current simulator subset. The simulator reports them as unsupported feature families instead of trying to load local MASM32 files or link Windows libraries.
```

### Acceptance criteria

- A realistic MASM32 playground sample produces meaningful unsupported-feature diagnostics.
- The diagnostics classify unsupported feature families rather than reporting many path-character errors.
- No unsupported feature is accidentally implemented.
- No source with assembly errors executes.
- Program Console remains empty.
- Exact rendered Simulator Messages coverage exists for representative diagnostics.
- The milestone report identifies any remaining residual low-level diagnostics and whether they are acceptable or future work.

### Non-goals

- No `INVOKE` implementation.
- No `ADDR` implementation.
- No `.IF` / `.ELSE` / `.ENDIF` implementation.
- No `call` implementation.
- No `ret` implementation.
- No `cmp` or conditional jump implementation unless already implemented by an earlier accepted phase.
- No `StdOut` implementation.
- No `crt_printf` implementation.
- No `ExitProcess` implementation.
- No host include loading.
- No library linking.
- No WinAPI execution.
- No PE loading.
- No macro expansion.

---

After Phases 57A through 57T, the roadmap resumes with Phase 58 without renumbering.

The inserted 57A-57T phases are corrective documentation, diagnostic-policy, startup-state, display, `.CONST`, `.code` memory-access policy, MASM segment-symbol diagnostics, source-level `nop`, host include/library diagnostics, unsupported MASM invocation/flow diagnostics, and playground-fixture recovery phases. They do not renumber Phase 58 or later phases.

Assistants must not treat these inserted phases as permission to skip Phase 58 or to implement Phase 58 control-flow consumers early.

Assistants must not treat Phase 57K as permission to implement readable `.code` byte images, writable `.code` section images, deterministic simulator code-image bytes, real x86 opcode emission, PE layout, object-file behavior, linker behavior, relocation records, segment registers, segment overrides, or `ASSUME`.

Assistants must not treat Phase 57L as permission to expose `.code` as simulated program memory. Phase 57L denies `.code` memory reads and writes.

Assistants must not treat Phase 57M as permission to implement segment registers, segment overrides, `ASSUME`, OMF/COFF segment modeling, linker groups, PE section layout, or internal region aliases such as `_TEXT` or `_DATA`.

Assistants must not treat Phase 57N or Phase 57O `nop` support as permission to implement real x86 opcode emission, `.code` byte images, PE layout, alignment padding, raw byte emission, or disassembly.

Assistants must not treat Phases 57P through 57T as permission to implement host include loading, `INCLUDELIB` linking, `INVOKE`, `ADDR`, WinAPI execution, high-level MASM flow, `call`, `ret`, `cmp`, or conditional jumps.

## 62. Phase 58 - Code Label Table and Label Diagnostics

### Goal

Build a reliable code-label table before enabling branch execution.

Phase 58 is a parser/IR metadata and diagnostics phase. It introduces accepted code-label declarations and procedure-entry label metadata. It must not implement branch parsing, branch lowering, branch execution, instruction-pointer mutation, conditional control flow, `loop`, `call`, `ret`, stack behavior, procedure invocation, procedure frame setup, debugger stepping, breakpoints, or UI click-to-label behavior.

Phase 58 exists so later branch, loop, call, debugger, and procedure phases can rely on one tested source of label metadata instead of each later phase inventing label parsing and diagnostics independently.

### Behavior category

Parser/source metadata and diagnostics.

Phase 58 changes accepted MASM/source parser behavior because code-label declarations become recognized and recorded. It does not add a new runtime arithmetic/logical instruction and does not add any instruction that consumes a label.

After Phase 58 is implemented, tested, and accepted, current-status surfaces that describe the latest accepted MASM/source parser behavior may advance to:

```text
Phase 58 - Code Label Table and Label Diagnostics
```

If a status surface is intentionally instruction-runtime-only rather than parser/source-behavior status, the milestone report must identify that exception explicitly. Do not silently advance or silently preserve runtime/source-run metadata without stating the reason.

The Phase 58 milestone report must include both status labels:

```text
Repository/archive milestone:
Phase 58 - Code Label Table and Label Diagnostics

Runtime/source-run MASM behavior phase:
Phase 58 - Code Label Table and Label Diagnostics
```

If the implementation keeps any specific status field at Phase 57 because that field is explicitly instruction-runtime-only, the report must list that field and explain why it did not advance.

### Relationship to Phase 57 and later phases

Phase 57 - Signed IDIV was the last runtime instruction milestone before this phase. Phase 57 explicitly did not implement labels, `jmp`, conditional jumps, `loop`, stack behavior, procedure behavior, or future instruction groups.

Phase 58 must preserve that boundary. It may accept and record label declarations, but it must not make labels executable control-flow targets yet.

Later phases must own label-consuming behavior. A later branch-consuming phase must add its own tests for:

- reference lookup;
- missing-target diagnostics;
- branch target validation;
- instruction-pointer mutation;
- execution-step behavior;
- execution limits and infinite-loop protection, where applicable.

Phase 58 must not implement any of those later behaviors early.

### Accepted declaration syntax

Phase 58 accepts ordinary code-label declarations and records procedure-entry labels from existing `PROC` structure.

Accepted forms:

```asm
start:
loop_top:
main PROC
main ENDP
```

Ordinary labels use this form:

```asm
name:
```

A `PROC` name is also recorded as a code-label declaration:

```asm
name PROC
```

A `name PROC` declaration records a **procedure-entry code label**. Its target is the first executable instruction inside that procedure body when one exists. This is metadata for later control-flow and procedure phases only.

Recording `name PROC` as a procedure-entry label does not implement:

- `call`;
- `ret`;
- stack frames;
- root-procedure invocation semantics beyond the already existing execution model;
- parameter passing;
- `USES`;
- `LOCAL`;
- `PROTO`;
- `INVOKE`;
- `ADDR`;
- calling conventions;
- linker behavior;
- Windows procedure behavior.

A label by itself is not an executable instruction. It marks the next executable instruction when such an instruction exists.

### Label target metadata policy

For each accepted code-label declaration, Phase 58 must record a target classification or equivalent internal state.

Required conceptual target classifications:

```text
executable instruction target
procedure-entry target
no executable target
```

These names are metadata concepts. They are not required public JSON enum names unless Phase 58 explicitly exposes label metadata through a tested public, source-run, debug, or test-only API.

Definitions:

- **Executable instruction target**: a `name:` ordinary code label whose resolved target is a real executable IR instruction.
- **Procedure-entry target**: a `name PROC` entry whose resolved target is the first executable instruction inside that procedure body.
- **No executable target**: a `name:` ordinary label or `name PROC` entry that appears before `ENDP`, `END`, another non-executable boundary, or the end of the executable code stream without any following executable instruction to target.

No-executable-target labels are accepted declarations in Phase 58. They are not assembly errors merely because they exist.

Example:

```asm
.code
main PROC
start:
    mov eax, 1
done:
main ENDP
END main
```

Expected Phase 58 behavior:

- `start` is accepted and recorded as an executable instruction target for `mov eax, 1`.
- `done` is accepted and recorded as a no-executable-target label.
- No `empty-label-target`, `invalid-jump-target`, or branch-target diagnostic is emitted in Phase 58 because no branch instruction consumes `done`.

Later branch-consuming phases must diagnose attempts to branch to a no-executable-target label. Those later diagnostics should point at the target use and include prior-definition metadata for the referenced label when available.

Do not silently map no-executable-target labels to `ENDP`, `END`, root-procedure termination, successful program exit, or the next procedure unless a later phase explicitly defines that behavior.

### Multiple labels before one instruction

Phase 58 must support consecutive labels before one executable instruction.

Example:

```asm
.code
main PROC
first:
second:
    mov eax, 1
main ENDP
END main
```

Expected behavior:

- `first` is accepted.
- `second` is accepted.
- Both labels are recorded as executable instruction targets for the same following executable instruction, `mov eax, 1`.
- Neither label mutates registers, flags, memory, Program Console output, or memory-change rows.

### CASEMAP and label names

Label declarations follow the active user-defined symbol case policy.

The policy comes from the project-wide standing rule for keyword matching and user-defined symbols:

- instruction mnemonics are case-insensitive;
- register names and register aliases are case-insensitive;
- directives are case-insensitive;
- operators and data type names are case-insensitive;
- `PTR` width names are case-insensitive;
- virtual include names are case-insensitive;
- recognized Irvine32 routine names are case-insensitive;
- user-defined symbols are governed by `OPTION CASEMAP`.

Code labels and `PROC` names are user-defined symbols for this purpose.

Supported policy behavior:

- default behavior is equivalent to `OPTION CASEMAP:ALL`;
- `OPTION CASEMAP:ALL` selects case-insensitive user-symbol matching from that directive forward;
- `OPTION CASEMAP:NONE` selects exact-case user-symbol matching from that directive forward;
- `OPTION CASEMAP:NOTPUBLIC` remains unsupported until public/external linkage semantics exist.

Do not apply `OPTION CASEMAP:NONE` to instruction mnemonics, directives, registers, virtual include names, or recognized Irvine32 routine names.

Default and `OPTION CASEMAP:ALL` duplicate example:

```asm
.code
main PROC
Loop:
    mov eax, 1
loop:
    mov ebx, 2
main ENDP
END main
```

Expected behavior:

- default behavior is `CASEMAP:ALL`;
- `Loop:` and `loop:` conflict by ASCII-folded spelling;
- the second declaration is rejected;
- the diagnostic points at the second declaration and includes prior-definition metadata for the first declaration.

Explicit `OPTION CASEMAP:ALL` must behave the same way:

```asm
OPTION CASEMAP:ALL

.code
main PROC
Loop:
    mov eax, 1
loop:
    mov ebx, 2
main ENDP
END main
```

Expected behavior:

- `Loop:` and `loop:` conflict by ASCII-folded spelling;
- the explicit directive does not change default behavior except by making it explicit.

`OPTION CASEMAP:NONE` exact-case example:

```asm
OPTION CASEMAP:NONE

.code
main PROC
Loop:
    mov eax, 1
loop:
    mov ebx, 2
main ENDP
END main
```

Expected behavior:

- `Loop:` and `loop:` are distinct label declarations because exact-case user-symbol matching is active.
- This is declaration-metadata behavior only.
- Phase 58 must not parse or execute a branch reference merely to test case-sensitive label lookup.

### Label references are not implemented in Phase 58

Phase 58 must not validate branch-target references because there are no branch-target reference consumers yet.

The following source must continue to follow the current pre-branch unsupported-instruction behavior unless an earlier accepted phase already implemented `jmp`:

```asm
.code
main PROC
start:
    mov eax, 1
    jmp start
main ENDP
END main
```

Expected Phase 58 behavior:

- `start:` is accepted and recorded.
- `jmp start` is not implemented by Phase 58.
- The diagnostic for `jmp` must use the current stable unsupported-instruction or future-feature wording.
- Phase 58 must not implement `jmp`, instruction-pointer mutation, branch execution, or target-reference resolution in order to test label metadata.

Later label-consuming phases must resolve label references using the user-symbol case policy active at the reference location and must add their own reference-resolution tests.

### Symbol namespace and conflict policy

Phase 58 must use the same effective user-defined symbol namespace policy as the rest of the simulator unless a later phase explicitly introduces separate scopes.

For this phase, the effective namespace includes at least these already-implemented or already-planned user-defined symbol categories when they exist in the current repository state:

- data symbols;
- numeric equates;
- code labels;
- procedure names recorded from `name PROC`.

Do not create an independent namespace where `main PROC` and `main:` can both exist with the same effective name.

Do not create an independent namespace where a code label can silently shadow an existing data symbol or numeric equate.

If a symbol category is not implemented in the current repository state, do not implement that category merely to test Phase 58. Instead, document in the milestone report that the corresponding conflict test is not applicable until that symbol category exists.

#### Duplicate ordinary labels

```asm
.code
main PROC
start:
    mov eax, 1
start:
    mov ebx, 2
main ENDP
END main
```

Expected behavior:

- the second `start:` declaration is rejected;
- the first accepted `start:` declaration remains the prior definition;
- the rejected second declaration is not inserted into the code-label table;
- the rejected second declaration does not replace the first declaration.

#### Code label conflicts with data symbol

```asm
.data
value DWORD 1

.code
main PROC
value:
    mov eax, 1
main ENDP
END main
```

Expected behavior:

- `value:` conflicts with the already accepted data symbol `value`;
- under default or `OPTION CASEMAP:ALL`, a spelling that differs only by ASCII case also conflicts;
- under `OPTION CASEMAP:NONE`, only an exact spelling conflict is rejected unless another documented namespace rule applies.

#### Procedure-entry label conflicts with ordinary label

```asm
.code
main PROC
main:
    mov eax, 1
main ENDP
END main
```

Expected behavior:

- `main PROC` records `main` as a procedure-entry code label.
- `main:` conflicts with the previously accepted procedure-entry code label.
- The diagnostic points at `main:` and includes prior-definition metadata for `main PROC`.

#### Ordinary label conflicts with later procedure-entry label

```asm
.code
start:
start PROC
    mov eax, 1
start ENDP
END start
```

Expected behavior:

- `start:` records an ordinary code label.
- `start PROC` conflicts with the previously accepted ordinary code label.
- The diagnostic points at the `start` name in `start PROC` and includes prior-definition metadata for `start:`.

#### Code label conflicts with numeric equate, when numeric equates are implemented

Use the numeric equate syntax already implemented in the current repository state.

If the implemented syntax is `name = constant`, use this shape:

```asm
COUNT = 4

.code
main PROC
COUNT:
    mov eax, 1
main ENDP
END main
```

If the implemented syntax is another accepted numeric-equate form, use that implemented form instead.

Expected behavior:

- `COUNT:` conflicts with the already accepted numeric equate `COUNT`;
- under default or `OPTION CASEMAP:ALL`, folded-case spellings also conflict;
- under `OPTION CASEMAP:NONE`, exact-case policy applies.

Do not add or broaden numeric-equate syntax merely to create this Phase 58 test. If numeric equates are not implemented in the Phase 58 base, mark this conflict case as not applicable in the Phase 58 milestone report.

### Required label metadata

For each accepted code-label declaration, record metadata sufficient for later control-flow diagnostics, debugger display, and source navigation.

Required metadata:

- original label spelling exactly as written in source;
- normalized lookup key used under the active `OPTION CASEMAP` policy, if the implementation stores one;
- active CASEMAP policy at the declaration location;
- whether the declaration came from ordinary label syntax `name:` or procedure syntax `name PROC`;
- source line for the label name;
- source column for the label name;
- byte offset for the label name;
- span length for the label name;
- target instruction index, if a following executable instruction exists;
- target classification:
  - executable instruction target;
  - procedure-entry target;
  - no executable target;
- prior-definition metadata when a duplicate/conflict diagnostic is emitted.

The implementation does not need to expose all of these fields in public JSON unless Phase 58 explicitly chooses to add a tested metadata API.

If Phase 58 exposes code-label metadata through source-run JSON, debug JSON, or a test-only API, that payload must be documented and tested.

If Phase 58 does not expose public metadata, native parser/IR tests must still verify the metadata internally.

### Diagnostics

Phase 58 diagnostics must be structured and source-located.

Required diagnostic families:

- `duplicate-label`, or the implementation's selected equivalent, for duplicate code-label declarations;
- `symbol-redefinition` or `label-symbol-conflict`, or the implementation's selected equivalent, for conflicts between a code label and another accepted user-defined symbol in the same effective namespace;
- existing source-location and parser-capacity diagnostics must remain unchanged.

Use the most specific diagnostic code already present in the implementation if one exists. Do not introduce multiple overlapping diagnostic codes for the same case unless the distinction is documented and tested.

Diagnostic requirements:

- The diagnostic must point at the current rejected declaration, not only at the previous declaration.
- The diagnostic must include current declaration line, column, byte offset, and span length.
- The diagnostic must include prior-definition line, column, byte offset, and span length when those fields are available.
- The diagnostic message must identify the conflicting symbol spelling.
- For folded-case conflicts under default or `OPTION CASEMAP:ALL`, the message should make clear that the conflict is caused by case-insensitive user-symbol matching.
- For exact-case `OPTION CASEMAP:NONE`, diagnostics must not claim two labels conflict merely because their folded spellings match.
- Rejected declarations must not be inserted into the label table after a diagnostic.
- The diagnostic must be emitted through Simulator Messages, not Program Console.
- A source with duplicate/conflicting labels must not execute.

Diagnostics must not use milestone-relative wording such as:

```text
unsupported by the current milestone
not implemented in this milestone
not supported in this milestone
```

Diagnostics must use stable behavior-specific wording.

Acceptable diagnostic message examples:

```text
Duplicate code label `start`; first defined at line 3, column 1.
```

```text
Code label `value` conflicts with an existing data symbol defined at line 2, column 1.
```

```text
Code label `loop` conflicts with `Loop` because user-defined symbols are case-insensitive under the active CASEMAP policy.
```

The exact wording may differ, but it must be stable, actionable, and covered by exact rendered Simulator Messages tests.

### Required parser and metadata tests

Phase 58 must add parser and metadata tests for accepted label declarations.

Required tests:

- Ordinary label accepted:
  - `start:` before an executable instruction is recorded as an executable instruction target.
- Multiple ordinary labels accepted:
  - consecutive labels before one executable instruction are each recorded as executable instruction targets for the same following executable instruction.
- Procedure-entry label accepted:
  - `main PROC` records `main` as a procedure-entry code label.
- Procedure-entry target:
  - `main PROC` targets the first executable instruction in the procedure body when one exists.
- No-executable-target ordinary label accepted:
  - a label immediately before `ENDP` or `END` is recorded as no-executable-target and does not emit an error in Phase 58.
- No-executable-target procedure entry accepted:
  - an empty procedure body records the `PROC` name as a procedure-entry label with no executable target, or an equivalent documented internal representation.
  - the empty procedure entry must not be mapped to `ENDP`, `END`, synthetic root-procedure termination, or the first executable instruction of a later unrelated procedure.
- Procedure containing only non-executable compatibility constructs:
  - if a procedure body contains accepted metadata-only, compatibility-notice, or listing/documentation constructs but no executable instruction, the procedure entry remains a procedure-entry/no-executable-target metadata record.
  - accepted notices must not create fake executable instruction targets.
- Adjacent empty and non-empty procedures:
  - `Empty PROC` / `Empty ENDP` followed by `Real PROC` containing `mov eax, 1` must record `Empty` as no-executable-target and `Real` as targeting its own first executable instruction.
  - `Empty` must not point to `Real`'s first instruction.
- Metadata source locations:
  - ordinary label metadata records line, column, byte offset, and span length for the label name.
  - `PROC` label metadata records line, column, byte offset, and span length for the procedure name.
- Labels are non-executable:
  - label declarations do not generate executable IR instructions.
  - label declarations do not mutate registers, flags, memory, Program Console output, Simulator Messages, or memory-change rows.

### Required CASEMAP tests

Phase 58 must add CASEMAP tests for labels.

Required tests:

- Default folded duplicate:
  - `Loop:` followed by `loop:` is rejected under omitted/default CASEMAP behavior.
- Explicit `OPTION CASEMAP:ALL` folded duplicate:
  - `Loop:` followed by `loop:` is rejected after `OPTION CASEMAP:ALL`.
- `OPTION CASEMAP:NONE` exact-case distinct labels:
  - `Loop:` and `loop:` are accepted as distinct declarations after `OPTION CASEMAP:NONE`.
- CASEMAP does not affect keywords:
  - after `OPTION CASEMAP:NONE`, directive and instruction recognition remains case-insensitive where existing behavior already supports case-insensitive matching.
- Irvine32 routine recognition remains separate from user-symbol CASEMAP:
  - if `INCLUDE Irvine32.inc` and `exit` are available in the Phase 58 base, `EXIT`, `Exit`, and `exit` remain recognized case-insensitively even after `OPTION CASEMAP:NONE`.
- Rejected folded duplicate is not inserted:
  - after a folded duplicate label is rejected, parser metadata still points to the first accepted declaration.

### Required symbol-conflict tests

Phase 58 must add conflict tests for the symbol categories present in the current repository state.

Required tests:

- Duplicate ordinary label:
  - `start:` followed by another `start:` reports the selected duplicate-label diagnostic.
- Code label conflicts with data symbol:
  - `.data value DWORD 1` followed by `.code value:` reports the selected symbol-conflict diagnostic.
- Folded code label conflicts with data symbol under default or `OPTION CASEMAP:ALL`:
  - `.data Value DWORD 1` followed by `.code value:` conflicts under default or explicit `OPTION CASEMAP:ALL`.
- Exact-case data symbol and code label behavior under `OPTION CASEMAP:NONE`:
  - exact spelling conflicts are rejected.
  - different spelling by case is accepted only if the active namespace policy permits it and no other rule forbids it.
- `PROC` name conflicts with ordinary label:
  - `main PROC` followed by `main:` reports a conflict.
- Ordinary label conflicts with later `PROC` name:
  - `start:` followed by `start PROC` reports a conflict.
- Numeric equate conflicts with code label, when numeric equates are implemented:
  - use the numeric-equate syntax already implemented in the repository.
  - the equate name followed by a same-effective-name code label reports a conflict.
  - if numeric equates are not implemented in the Phase 58 base, document this case as not applicable instead of implementing equates early.

### Required diagnostic and rendered-message tests

Phase 58 must add structured diagnostic tests and exact rendered Simulator Messages tests for every new or changed user-visible diagnostic path.

Structured diagnostics must verify:

- diagnostic code;
- severity/kind;
- current declaration line;
- current declaration column;
- current declaration byte offset;
- current declaration span length;
- prior-definition line;
- prior-definition column;
- prior-definition byte offset;
- prior-definition span length, where available;
- conflicting symbol spelling or normalized conflict key when available.

Exact rendered Simulator Messages tests must cover at least:

- duplicate ordinary label;
- folded duplicate label under default or `OPTION CASEMAP:ALL`;
- data-symbol/code-label conflict;
- `PROC`/ordinary-label conflict in both declaration orders, unless one order is structurally impossible in the current parser and the report explains why.

Rendered tests must use the real Simulator Messages formatter path. Native C tests that only inspect internal parser status are not sufficient for user-visible diagnostic wording.

### Required source-run tests

Phase 58 must add source-run tests proving labels integrate without changing existing execution semantics.

Required tests:

- A valid source with labels and existing executable instructions still executes successfully.
- Consecutive labels before an existing instruction still execute that instruction exactly once in normal fall-through order.
- Labels do not create Program Console output.
- Labels do not create memory-change rows.
- Labels do not mutate registers or flags.
- Labels do not alter existing instruction semantics.
- A source with duplicate/conflicting labels refuses execution.
- A source containing `jmp label` still follows current pre-branch unsupported-instruction behavior unless an earlier accepted phase already implemented `jmp`.

### Required non-goal regression tests

Phase 58 must add or preserve tests proving that future control-flow and procedure behavior was not implemented early.

Required non-goal coverage:

- `jmp` remains unsupported unless a prior accepted phase already implemented it.
- conditional jumps remain unsupported unless a prior accepted phase already implemented them.
- `loop` remains unsupported unless a prior accepted phase already implemented it.
- `call` remains unsupported except for any already-implemented special virtual behavior.
- `ret` remains unsupported unless a prior accepted phase already implemented it.
- stack behavior is not added.
- procedure invocation is not added.
- debugger stepping, breakpoints, and UI label navigation are not added.

These tests may be parser/source-run diagnostics rather than executor tests, depending on the current implementation architecture.

### Required documentation updates

Phase 58 must update current documentation and supported-syntax references to describe accepted label declarations accurately.

Documentation must say:

- ordinary labels using `name:` are accepted as code-label metadata;
- `name PROC` records procedure-entry label metadata;
- labels are not executable instructions;
- labels do not yet enable `jmp`, conditional jumps, `loop`, `call`, `ret`, or procedure invocation;
- no-executable-target labels are accepted declarations but are not valid branch targets until a later branch-consuming phase defines and diagnoses target use;
- label declarations follow the user-defined symbol CASEMAP policy;
- keyword, directive, register, virtual include, and Irvine32 routine matching remain case-insensitive.

Supported-syntax documentation must not imply that labels can already be used as branch targets unless the same phase or a later accepted phase implements branch-consuming instructions.

### Acceptance criteria

A program with labels but no branch behavior still executes straight-line code exactly as before:

```asm
.code
main PROC
start:
    mov eax, 1
next:
    mov ebx, 2
main ENDP
END main
```

Expected final register state includes:

```text
EAX = 00000001h / 1
EBX = 00000002h / 2
```

A program with consecutive labels before one executable instruction still executes that instruction once:

```asm
.code
main PROC
first:
second:
    mov eax, 1
main ENDP
END main
```

Expected final register state includes:

```text
EAX = 00000001h / 1
```

A program with a no-executable-target label is accepted when the label is not consumed as a branch target:

```asm
.code
main PROC
    mov eax, 1
done:
main ENDP
END main
```

Expected behavior:

- execution completes successfully;
- `done` is recorded as no-executable-target metadata;
- no branch-target diagnostic is emitted in Phase 58.

A program with duplicate labels refuses execution and emits a structured, rendered diagnostic:

```asm
.code
main PROC
start:
    mov eax, 1
start:
    mov ebx, 2
main ENDP
END main
```

Expected behavior:

- no program execution occurs;
- the diagnostic points at the second `start:` declaration;
- prior-definition metadata points at the first `start:` declaration;
- rendered Simulator Messages include the exact duplicate/conflict wording selected by the implementation.

Phase 58 is accepted only when the tests prove that labels are recorded as metadata without adding branch execution, stack behavior, procedure invocation, or debugger behavior.

### Phase 58 milestone report requirements

The Phase 58 milestone report must explicitly state that Phase 58 is a parser/source metadata and diagnostics phase.

The report must include:

- source-of-truth files used;
- repository/archive milestone before and after the phase;
- runtime/source-run MASM behavior phase before and after the phase;
- whether Phase 58 advanced each status surface, with specific reasons;
- exact accepted label declaration forms implemented;
- exact target metadata policy implemented;
- exact duplicate/conflict cases implemented;
- explicit statement that branch parsing and branch execution were not implemented;
- explicit statement that `jmp`, conditional jumps, `loop`, `call`, `ret`, stack behavior, procedure invocation, and debugger stepping were not implemented;
- CASEMAP behavior tested:
  - default folded duplicate rejection;
  - explicit `OPTION CASEMAP:ALL` folded duplicate rejection;
  - `OPTION CASEMAP:NONE` exact-case distinct labels;
- symbol-conflict behavior tested:
  - ordinary duplicate labels;
  - data-symbol/code-label conflict;
  - `PROC`/ordinary-label conflicts in both declaration orders;
  - numeric equate/code-label conflict if numeric equates are present;
- structured diagnostics added or changed;
- exact rendered Simulator Messages tests added or changed;
- whether public JSON/debug metadata was added or whether metadata was tested through internal native parser/IR tests only;
- aggregate test result if the aggregate command completed;
- focused test groups run if the aggregate timed out or output was truncated in an assistant/container environment;
- whether browser/Wasm smoke testing was run or skipped, and why.

The report must not say:

```text
All implemented milestone tests passed.
```

unless the aggregate command completed and returned the final success status in that environment.

If focused groups passed after aggregate timeout, the report must use the guide's focused-verification wording and list the focused groups that passed.

---

## 63. Phase 59 - Control-Flow Instruction Limit

### Goal

Add instruction-count watchdog behavior before enabling branch instructions that can form loops.

This is a VM execution-safety phase. It must not implement `jmp`, conditional jumps, `loop`, calls, stack behavior, or procedure behavior.

The shared direct branch target classification section that follows is forward-looking guidance for later branch phases. It does not expand Phase 59 scope.

### Behavior category

Runtime resource-limit enforcement.

### Tasks

1. Add a configurable instruction-count limit to source-run execution.
2. Count every executed VM instruction.
3. Execute at most `limit` instructions. If `executed_count == limit` and another instruction would be fetched, stop before executing instruction `limit + 1` with `instruction-limit-exceeded`.
4. Preserve all register, flag, memory, and console state produced by the first `limit` executed instructions; do not partially execute the next instruction.
5. Return a structured runtime diagnostic when the limit is reached.
6. Ensure no `execution-complete` message is emitted after limit failure.
7. Include `instructionLimit`, `executedInstructionCount`, `attemptedNextInstructionIndex`, and `currentInstructionIndex` in source-run JSON.
8. Preserve deterministic behavior for the same source and settings.
9. Keep default limit high enough for normal straight-line examples.

### Source-run option and structured diagnostic field names

Phase 59 must expose the instruction-count limit through a single source-run/test-facing option named:

```text
instructionLimit
```

The option value must be a positive integer.

If `instructionLimit` is omitted, the simulator uses its documented default limit.

Invalid option values must produce a structured settings or source-run configuration diagnostic rather than falling back silently. Invalid values include:

- zero;
- negative values;
- non-integer values;
- non-numeric JSON values;
- values above the implementation's documented maximum, if a maximum is enforced.

When execution stops because the limit is reached, the diagnostic code must be:

```text
instruction-limit-exceeded
```

The source-run JSON result or diagnostic detail object must include these fields with these exact names:

```text
instructionLimit
executedInstructionCount
attemptedNextInstructionIndex
currentInstructionIndex
```

Field meanings:

- `instructionLimit`: the configured limit that stopped execution.
- `executedInstructionCount`: the number of VM instructions that fully executed and committed state before the stop.
- `attemptedNextInstructionIndex`: the instruction index that would have been fetched next and was blocked by the limit.
- `currentInstructionIndex`: the index of the last VM instruction that fully executed and committed state. If no instruction has executed yet, this field must be `null`.

The limit check occurs before fetching or executing the next instruction after the limit has been reached.

With `instructionLimit = 2`, a three-instruction straight-line program must behave as follows:

```text
instruction 1 executes and commits state
instruction 2 executes and commits state
instruction 3 does not execute
instruction-limit-exceeded is emitted
execution-complete is not emitted
```

The failed instruction-limit check must not mutate registers, flags, memory, Program Console output, or memory-change rows. It may append only the instruction-limit runtime diagnostic to Simulator Messages.

### Diagnostics

- `instruction-limit-exceeded` as a runtime diagnostic.
- Rendered Simulator Messages must clearly explain that execution stopped because the simulator limit was reached.

### Required tests

- Straight-line program below limit succeeds.
- Straight-line hardcoded test with artificially low limit fails.
- Instruction count includes no-op and normal instructions.
- Diagnostic JSON includes exact fields `instructionLimit`, `executedInstructionCount`, `attemptedNextInstructionIndex`, and `currentInstructionIndex`.
- Tests assert the final register state after failure. With limit `2` on a three-instruction straight-line program, the first two instructions have executed and the third has not.
- Rendered Simulator Messages test for instruction-limit failure.

### Acceptance criteria

With a test configuration limit of `2`, this program fails before normal completion:

```asm
.code
main PROC
    mov eax, 1
    mov ebx, 2
    mov ecx, 3
main ENDP
END main
```

Expected diagnostic:

```text
[runtime-error] instruction-limit-exceeded
```

---

## Shared Direct Branch Target Classification

This policy applies to Phase 58 - Code Label Table and Label Diagnostics, Phase 60 - Direct JMP Parsing and Target Lowering, Phase 61 - Direct JMP Runtime Execution, and later direct branch phases that consume source-level label operands.

This shared section defines target classification only. It does not implement any feature by itself.

All direct branch instructions that consume source-level label operands must use the same target classifier unless the owning phase explicitly documents a changed rule.

| Target class | Result |
|---|---|
| Code label from `name:` with executable target instruction | Accepted |
| Procedure-entry label from `name PROC` with a first executable instruction in the procedure body | Accepted as a direct branch to the first executable instruction only |
| Code label that resolves to no executable instruction | Rejected when consumed by a branch target operand |
| Procedure-entry label for a procedure body with no executable instruction | Rejected when consumed by a branch target operand |
| Data symbol | Rejected |
| Numeric equate or constant symbol | Rejected |
| Text equate or macro-like text symbol | Rejected |
| Irvine32 virtual routine or virtual terminator name | Rejected for direct `jmp`; future `call` classification owns callable Irvine32 behavior |
| Windows/API/external symbol | Rejected as a non-goal or invalid direct branch target, depending on the exact symbol category |
| Unknown identifier | Rejected |
| Empty target operand | Rejected as malformed branch syntax |
| Immediate numeric target | Rejected |
| Register target | Rejected for direct branch forms; future indirect branch behavior, if any, must be specified separately |
| Memory operand target | Rejected for direct branch forms; future indirect branch behavior, if any, must be specified separately |
| Distance/type override target such as `SHORT label`, `NEAR PTR label`, or `FAR PTR label` | Rejected unless a later phase explicitly implements that override |

A label with no executable target is not an error merely because it exists. It becomes an error only when a branch-consuming phase uses it as the operand of a branch instruction.

Every rejected direct-branch target diagnostic must point its primary source span at the branch operand. If useful, the diagnostic may include secondary metadata for the definition location of the rejected symbol.

A direct branch to a `PROC` entry is still a direct branch. It is not a procedure call.

This shared classifier does not decide or implement:

- `CALL` target validity;
- Irvine32 routine call validity;
- stack mutation;
- stack-frame setup or teardown;
- procedure arguments;
- `USES`;
- `LOCAL`;
- `PROTO`;
- `INVOKE`;
- calling-convention behavior;
- indirect branch behavior;
- Windows API behavior;
- PE loading;
- object linking.

### Non-goals for this shared section

This shared section does not authorize:

- direct `jmp` before Phase 60 - Direct JMP Parsing and Target Lowering;
- runtime branch execution before Phase 61 - Direct JMP Runtime Execution;
- conditional jumps before their own phases;
- `loop` before its own phase;
- indirect jumps;
- `CALL`;
- `RET`;
- procedure stack behavior;
- Irvine32 routine calls;
- Windows API calls;
- PE/linker behavior.

### Required shared tests for phases that use this classifier

Any phase that first uses the classifier for a new branch instruction family must add tests for:

- accepted code label target;
- accepted `PROC` entry target, with no CALL/RET semantics;
- rejected empty/no-executable label target;
- rejected data symbol target;
- rejected numeric equate target;
- rejected text equate or macro-like text target if such symbols are visible to the classifier;
- rejected Irvine32 virtual symbol such as `exit`;
- rejected Windows/API/external symbol when applicable;
- rejected unknown target;
- rejected empty target operand;
- rejected register target;
- rejected memory target;
- rejected distance/type override target unless that phase explicitly implements it;
- structured diagnostic source span pointing at the target operand;
- rendered Simulator Messages for every new rejected-target diagnostic path.

If a later phase intentionally changes one target class, that phase must state the changed class explicitly and add regression tests proving unchanged classes still behave as before.

### Reporting requirement for control-flow preparation phases

Milestone reports for Phase 58 - Code Label Table and Label Diagnostics through Phase 61 - Direct JMP Runtime Execution must state clearly whether the phase changed runtime branch execution behavior.

For metadata-only or parser/lowering-only phases, use this wording pattern:

```text
Runtime branch execution behavior:
- Not implemented in this phase; this phase records metadata, validates labels, classifies targets, or lowers branch metadata only.
```

For the first runtime branch phase, use this wording pattern:

```text
Runtime branch execution behavior:
- Implemented for direct `jmp` only. Conditional jumps, loop instructions, call/ret behavior, stack behavior, procedure-frame behavior, indirect jumps, and Irvine32 routine calls remain future work.
```

Reports for parser/lowering-only phases must not say that `jmp`, branches, or control flow are "supported" without qualifying whether support is parser-only, lowering-only, or executable runtime behavior.

Reports for runtime branch phases must state:

- which branch forms execute;
- which branch forms are parsed but not executable, if any;
- which branch forms are rejected;
- which branch forms remain future work;
- whether runtime/source-run MASM behavior phase metadata was advanced.

---

## 64. Phase 60 - Direct JMP Parsing and Target Lowering

### Goal

Parse `jmp label` and lower it to a branch target reference without executing branch behavior yet.

### Phase 60 runtime boundary before Phase 61

Phase 60 accepts, classifies, and lowers direct `jmp label` syntax. It does not execute branches.

Phase 60 must not implement any of the following behavior:

- runtime instruction-pointer mutation for `jmp`;
- execution transfer to a target label;
- branch-loop behavior;
- conditional jumps;
- `loop`;
- `call`;
- `ret`;
- stack behavior;
- procedure-entry semantics beyond target metadata;
- debugger stepping over branch execution.

A lowered Phase 60 `jmp` instruction must not silently fall through. It must not behave as a no-op. It must not execute the next straight-line instruction as if the branch were absent.

Before Phase 61 - Direct JMP Runtime Execution is implemented, any source-run execution path that reaches a lowered `jmp` instruction must stop before mutating the instruction pointer, registers, flags, memory, Program Console output, or memory-change rows.

Use this structured runtime diagnostic code:

```text
branch-runtime-deferred
```

Rendered message wording should explain the boundary without milestone-relative wording. Example:

```text
Direct JMP was parsed and resolved, but runtime branch execution is deferred to Phase 61 - Direct JMP Runtime Execution. Execution stopped before applying the jump.
```

This diagnostic is a Phase 60 transitional runtime boundary for accepted-but-not-yet-executable lowered direct branches.

Phase 61 - Direct JMP Runtime Execution must remove this transitional runtime-deferred behavior for valid direct `jmp label` instructions and replace it with actual instruction-pointer transfer. The `branch-runtime-deferred` diagnostic must remain only for branch forms that are still accepted for metadata but not executable, if any such forms remain explicitly documented.

Phase 60 tests may inspect lowered branch metadata without running the instruction. Source-run tests that execute a program containing `jmp` before Phase 61 must assert `branch-runtime-deferred` and must prove that no instruction after the `jmp` committed state.


### Deferred JMP execution-count and diagnostic metadata

Phase 60 lowers direct `jmp label` into IR so later phases can implement branch execution. Phase 60 does not execute the branch.

When execution reaches a lowered `jmp` before Phase 61 - Direct JMP Runtime Execution is implemented, the executor/source-run path must stop with `branch-runtime-deferred`.

The stopped `jmp` is an attempted instruction, not a committed instruction.

Required metadata behavior:

```text
executedInstructionCount = number of previously committed VM instructions
attemptedNextInstructionIndex = index of the lowered jmp instruction that was reached but not executed
currentInstructionIndex = last committed instruction index, or null if no instruction committed before the jmp
```

If the existing source-run JSON schema uses different field names, preserve the schema's established names but keep the same meaning. Do not invent new public JSON fields merely for Phase 60 unless Phase 60 explicitly owns a schema update.

Required runtime behavior for `branch-runtime-deferred`:

- stop before making any branch decision;
- do not mutate the instruction pointer;
- do not mutate registers;
- do not mutate modeled flags;
- do not mutate Phase 50A flag-validity metadata;
- do not mutate memory;
- do not write Program Console output;
- do not create memory-change rows;
- do not execute instructions after the `jmp`;
- do not emit `execution-complete`.

Diagnostic source-span rule:

- For `branch-runtime-deferred`, the primary source span should point at the `jmp` mnemonic or the full `jmp` instruction, because the problem is that runtime branch execution is deferred.
- For rejected or invalid targets, the primary source span should point at the target operand, because the problem is target classification or target validity.

Required tests:

- Source-run fixture where one ordinary instruction commits before `jmp`.
  - Assert that only the pre-`jmp` instruction is counted as committed.
  - Assert that the `jmp` is reported as the attempted next instruction, if the current schema exposes that concept.
  - Assert that instructions after `jmp` do not execute.
  - Assert no `execution-complete` message appears.

- Source-run fixture where `jmp` is the first executable instruction.
  - Assert zero committed instructions.
  - Assert no register, flag, memory, Program Console, or memory-change mutation occurs because of the `jmp`.

- Exact rendered Simulator Messages test for `branch-runtime-deferred`.
  - The message must use stable wording.
  - It must not say "not implemented in this milestone," "unsupported by the current milestone," or equivalent milestone-relative wording.
  - It should say that direct `JMP` parsing/lowering is present, but runtime branch execution is deferred to Phase 61 - Direct JMP Runtime Execution.

### Accepted syntax

```asm
jmp codeLabel
jmp procedureEntryLabel
```

### Rejected syntax

```asm
jmp dataSymbol
jmp equateName
jmp exit
jmp unknownName
jmp DWORD PTR [eax]
jmp eax
jmp SHORT label
jmp NEAR PTR label
jmp FAR PTR label
```

### Required behavior

- Use `Shared Direct Branch Target Classification` for every `jmp` target operand. Do not create a `jmp`-specific target classifier with different symbol-kind rules.
- Procedure entry labels are accepted as direct branch targets but do not imply CALL semantics.
- Diagnostic source span points at the target token.
- Runtime instruction-pointer mutation is not implemented in this phase. If source-run execution reaches a lowered `jmp`, it must stop with `branch-runtime-deferred` instead of falling through or acting as a no-op.

### Required tests

- Code label target classification.
- Procedure entry target classification.
- Data/equate/Irvine/unknown target diagnostics.
- Zero-length, directive-name, register-name, instruction-name, memory-target, and distance/type-override target rejections.
- Source-run `branch-runtime-deferred` diagnostic when execution reaches a lowered `jmp` before Phase 61.
- No-partial-mutation source-run test proving instructions after the deferred `jmp` do not execute.
- Rendered diagnostics for every rejected target class and for `branch-runtime-deferred`.

## 65. Phase 61 - Direct JMP Runtime Execution

### Goal

Execute already-lowered direct branch instructions.

### Runtime semantics

- Branch to the target VM instruction index.
- Consume the branch target metadata produced by Phase 60 - Direct JMP Parsing and Target Lowering. Do not reclassify source-level symbols during normal runtime execution.
- Count the `jmp` instruction as one executed instruction.
- Preserve all modeled flags.
- Produce no memory-change row.
- Respect instruction-count and active-time watchdogs.
- If target metadata is invalid after a source reload or malformed IR test, report `invalid-branch-target` without partial mutation.

### Required tests

- Forward jump skips instructions.
- Backward jump reaches instruction-count watchdog deterministically.
- Jump to procedure entry executes as a direct branch only.
- Runtime diagnostic for malformed target metadata.
- Breakpoint binding to target line remains valid in later debugger tests.

## 66. Phase 62 - CMP Register and Immediate Forms

### Goal

Implement `cmp` for register/register and register/immediate forms only.

### Accepted syntax

```asm
cmp r8/r16/r32, r8/r16/r32
cmp r8/r16/r32, imm
```

### Runtime semantics

- Compute subtraction for flags only.
- Do not mutate operands.
- Update `CF`, `ZF`, `SF`, `OF`, and any implemented `PF`/`AF` through the shared compare helper.
- Produce no memory-change rows.

### Required tests

- Equal and unequal register/register comparisons.
- Signed-negative bit patterns and unsigned carry cases.
- Immediate boundary values and existing negative-literal validation.
- Flags exactly match the shared subtraction/compare helper.
- Rendered diagnostics for width mismatch and malformed operands.

## 67. Phase 63 - CMP Memory Operand Forms

### Goal

Add memory operands to `cmp` after register/immediate forms are stable.

### Accepted syntax

```asm
cmp reg, mem
cmp mem, reg
cmp mem, imm
```

Memory width must be known through symbol metadata, explicit `PTR`, or an existing global width-resolution rule.

### Runtime semantics

- Read memory through central checked memory helpers.
- Compute subtraction for flags only.
- Do not mutate registers, memory, or flags until all memory reads needed for the comparison have succeeded.
- Produce no successful memory-change rows.


### Planned-read validation requirements for CMP memory forms

`cmp` memory forms read simulated memory and then update modeled flags from the comparison result. Therefore, every Phase 63 memory-source path must participate in the standing planned-access validation rule before the comparison consumes the memory value or updates flags.

This applies to all accepted CMP forms with memory operands, including:

```asm
cmp reg, mem
cmp mem, reg
cmp mem, imm
cmp symbol, reg
cmp symbol, imm
cmp symbol[offset], reg
cmp symbol[offset], imm
cmp BYTE PTR [reg32], imm
cmp WORD PTR [reg32], imm
cmp DWORD PTR [reg32], imm
```

Required policy behavior:

- Default region-only mode:
  - memory reads use mandatory checked VM memory helpers;
  - valid region reads proceed;
  - invalid address, invalid range, invalid region, or invalid permission diagnostics stop execution before flags are updated.

- Section-capacity warning mode:
  - a read that passes Level 1 but leaves section capacity emits `section-capacity-violation`;
  - execution continues;
  - flags are updated from the deterministic checked-memory value.

- Section-capacity strict mode:
  - a read that passes Level 1 but leaves section capacity emits `section-capacity-violation` as a runtime error;
  - execution stops before flags are updated.

- Section-image warning mode:
  - a read that passes Level 1 and applicable section-capacity checks but leaves the declared section image emits `section-image-violation`;
  - execution continues;
  - flags are updated from the deterministic checked-memory value.

- Section-image strict mode:
  - a read that passes Level 1 and applicable section-capacity checks but leaves the declared section image emits `section-image-violation` as a runtime error;
  - execution stops before flags are updated.

- Declared-object warning mode:
  - a read that passes lower-level validation but crosses declared-object bounds emits the existing declared-object diagnostic;
  - execution continues;
  - flags are updated from the deterministic checked-memory value.

- Declared-object strict mode:
  - a read that passes lower-level validation but crosses declared-object bounds emits the existing declared-object diagnostic as a runtime error;
  - execution stops before flags are updated.

- Uninitialized-read warning mode:
  - a read from bytes that still carry uninitialized-origin metadata emits `uninitialized-read`;
  - execution continues;
  - flags are updated from the deterministic zero-filled or current memory value.

- Uninitialized-read strict mode:
  - a read from bytes that still carry uninitialized-origin metadata emits `uninitialized-read` as a runtime error;
  - execution stops before flags are updated.

No-partial-mutation requirement:

For every strict or fatal planned-read failure, `cmp` must preserve:

- all general-purpose registers;
- `CF`, `ZF`, `SF`, and `OF`;
- Phase 50A flag-validity metadata;
- memory;
- Program Console output;
- memory-change rows.

A failed CMP planned-read path must not emit `execution-complete`.

Required test scope:

The required tests do not need to cover every Cartesian combination of CMP operand form and memory-validation policy. They must cover every applicable policy at least once and every accepted CMP memory operand class at least once.

At minimum, the Phase 63 test set must include:

- one `cmp reg, mem` planned-read policy case;
- one `cmp mem, reg` planned-read policy case;
- one `cmp mem, imm` planned-read policy case;
- one warning policy case that continues and updates flags;
- one strict policy case that stops before flags and flag-validity metadata change;
- one uninitialized-read warning case that continues using deterministic memory values;
- one invalid Level 1 memory-read case that stops before flags change;
- exact rendered Simulator Messages for one warning path and one strict path.

A milestone report for Phase 63 must state that CMP memory forms were added to planned-read collection, source-run/Wasm policy routing, diagnostic JSON production, and rendered Simulator Messages coverage.

### Required tests

- Direct symbol memory source and destination comparisons.
- Symbol-offset memory comparison.
- `.CONST` reads.
- `.DATA?` reads with uninitialized warning mode enabled.
- Invalid-address diagnostics.
- QWORD/SQWORD executable-memory rejection in MASM32 Educational Mode.
- Ambiguous `cmp [eax], 1` rejection.
- Rendered diagnostics and source spans for each failure class.

## 68. Phase 64 - Equality Conditional Jumps

### Goal

Implement equality conditional jumps after labels, instruction limits, `jmp`, and `cmp` exist.

This phase must not implement signed/unsigned relational jumps, anonymous labels, `loop`, calls, stack behavior, or procedure behavior.

### Behavior category

Runtime control-flow instructions.

### Accepted syntax

```asm
je label
jz label
jne label
jnz label
```

### Conditions

```text
JE/JZ:   jump when ZF = 1
JNE/JNZ: jump when ZF = 0
```

### Rejected syntax

```asm
je eax
jz [eax]
jne 1234h
jnz dataSymbol
je exit
```

### Runtime semantics

- Count each conditional jump as one executed instruction.
- Before evaluating `ZF`, call the Phase 50B undefined-flag-use helper for `ZF`. In warn mode, emit `undefined-flag-use` and continue using the deterministic preserved `ZF` value. In error mode, stop before deciding whether the jump is taken.
- If condition is true, branch to target label instruction index.
- If condition is false, continue to the next instruction.
- Do not modify registers, memory, or flags.
- Instruction-limit enforcement applies.

### Required tests

- `je` taken after equal `cmp`.
- `je` not taken after unequal `cmp`.
- `jne` taken after unequal `cmp`.
- `jz` and `jnz` aliases behave identically to `je` and `jne`.
- Flags are preserved by the jump instruction itself.
- Unknown target diagnostic.
- Data target diagnostic.
- `exit` target diagnostic.
- Infinite conditional loop reaches instruction limit.
- Source-run JSON and rendered diagnostic tests.

### Acceptance program

```asm
.code
main PROC
    mov eax, 5
    cmp eax, 5
    je equal
    mov ebx, 1
    jmp done
equal:
    mov ebx, 2
done:
main ENDP
END main
```

Expected:

```text
EBX = 00000002h / 2
```

---

## 69. Phase 65 - Signed Relational Conditional Jumps

### Goal

Implement signed relational conditional jumps after equality jumps are stable.

This phase must not implement unsigned relational jumps, anonymous labels, `loop`, calls, stack behavior, or procedure behavior.

### Behavior category

Runtime control-flow instructions.

### Accepted syntax and conditions

```text
jl  label   ; SF != OF
jnge label  ; alias of JL
jle label   ; ZF = 1 or SF != OF
jng label   ; alias of JLE
jg  label   ; ZF = 0 and SF = OF
jnle label  ; alias of JG
jge label   ; SF = OF
jnl label   ; alias of JGE
```

### Rejected syntax

- Same rejected target forms as equality jumps.
- Unsigned jump mnemonics remain unimplemented in this phase if not already implemented.

### Runtime semantics

- Read only `ZF`, `SF`, and `OF`.
- Before evaluating the signed branch condition, call the Phase 50B undefined-flag-use helper for every flag read by that mnemonic: `ZF`, `SF`, and/or `OF`. In warn mode, emit `undefined-flag-use` and continue using deterministic preserved flag values. In error mode, stop before deciding whether the jump is taken.
- Do not modify registers, memory, or flags.
- Count each jump as one executed instruction.
- Instruction-limit enforcement applies.

### Required tests

- Signed less-than with `-1 < 1` takes `jl`.
- Signed greater-than with `1 > -1` takes `jg`.
- Equal signed values take `jle` and `jge`, not `jl` or `jg`.
- Alias mnemonics match primary mnemonics. Require at least one taken and one not-taken test for each alias group: `JL/JNGE`, `JLE/JNG`, `JG/JNLE`, and `JGE/JNL`.
- Same raw values demonstrate signed-vs-unsigned difference, for example `FFFFFFFFh` compared with `1`.
- Unknown/data/`exit` target diagnostics.
- Instruction-limit diagnostics for a signed conditional loop.

### Acceptance program

```asm
.code
main PROC
    mov eax, -1
    cmp eax, 1
    jl less
    mov ebx, 0
    jmp done
less:
    mov ebx, 1
done:
main ENDP
END main
```

Expected:

```text
EBX = 00000001h / 1
```

---

## 70. Phase 66 - Unsigned Relational Conditional Jumps

### Goal

Implement unsigned relational conditional jumps after signed jumps are stable.

This phase must not implement anonymous labels, `loop`, calls, stack behavior, or procedure behavior.

### Behavior category

Runtime control-flow instructions.

### Accepted syntax and conditions

```text
ja   label  ; CF = 0 and ZF = 0
jnbe label  ; alias of JA
jae  label  ; CF = 0
jnb  label  ; alias of JAE
jb   label  ; CF = 1
jnae label  ; alias of JB
jbe  label  ; CF = 1 or ZF = 1
jna  label  ; alias of JBE
```

### Rejected syntax

- Same rejected target forms as equality jumps.

### Runtime semantics

- Read only `CF` and `ZF`.
- Before evaluating the unsigned branch condition, call the Phase 50B undefined-flag-use helper for every flag read by that mnemonic: `CF` and/or `ZF`. In warn mode, emit `undefined-flag-use` and continue using deterministic preserved flag values. In error mode, stop before deciding whether the jump is taken.
- Do not modify registers, memory, or flags.
- Count each jump as one executed instruction.
- Instruction-limit enforcement applies.

### Required tests

- Unsigned below with `1 < FFFFFFFFh` takes `jb`.
- Unsigned above with `FFFFFFFFh > 1` takes `ja`.
- Equal values take `jae` and `jbe`, not `ja` or `jb`.
- Alias mnemonics match primary mnemonics. Require at least one taken and one not-taken test for each alias group: `JA/JNBE`, `JAE/JNB`, `JB/JNAE`, and `JBE/JNA`.
- Same raw operand values must demonstrate the difference between unsigned relational jumps in this phase and signed relational jumps from Phase 65 - Signed Relational Conditional Jumps.
- Unknown/data/`exit` target diagnostics.
- Instruction-limit diagnostics for an unsigned conditional loop.

### Acceptance program

```asm
.code
main PROC
    mov eax, 0FFFFFFFFh
    cmp eax, 1
    ja above
    mov ebx, 0
    jmp done
above:
    mov ebx, 1
done:
main ENDP
END main
```

Expected:

```text
EBX = 00000001h / 1
```

## B. Batch-wide test requirements

Every revised phase in this batch must include:

1. Native C unit tests for new helpers or executor behavior.
2. Parser tests for accepted and rejected syntax.
3. Source-run JSON tests for successful programs and diagnostics.
4. Node Simulator Messages formatter tests for each diagnostic category whose wording or JSON shape is affected.
5. Manual browser smoke programs only as a supplement after rebuilding Wasm.
6. Regression tests proving previous milestone behavior still passes.
7. Doxygen/file-header review for any new public functions, structs, enums, or module APIs.
8. Tests proving read-only/no-write instructions do not create memory-change rows. This applies to `lea`, `mul`, `imul`, `div`, `idiv`, `cmp`, `jmp`, and conditional jumps unless a phase explicitly writes memory.

## C. Batch-wide diagnostic source-span requirements

Use these spans unless a later spec revision gives a more precise rule:

- Wrong mnemonic form: mnemonic span.
- Invalid destination: destination operand span.
- Invalid source: source operand span.
- Ambiguous memory width: memory operand span.
- Invalid shift/rotate count: count operand span.
- Divide-by-zero immediate or known constant: divisor operand span.
- Runtime divide-by-zero from register/memory: instruction span plus operand metadata when the referenced metadata exists.
- Unknown label: target token span.
- Duplicate label: duplicate label span plus prior-definition metadata when the referenced metadata exists.
- Instruction limit: current instruction span; for synthesized IR, use the source span of the high-level construct that generated the instruction.



## C. Additional v2 refinements applied

This `_v2` revision also locks down the following cross-cutting rules:

- `lea` effective-address arithmetic is modulo 2^32 and never validates mapped memory merely because the computed address is unmapped.
- Successful `lea`, `div`, and `idiv` must preserve all currently modeled flags unless a later phase explicitly changes the policy.
- Failed `mul`, `div`, and `idiv` must not create memory-change rows, partial implicit-register writes, or misleading successful deltas.
- Label lookup follows the active user-symbol case policy: case-insensitive by default, and case-sensitive only under `OPTION CASEMAP:NONE`.
- All branch instructions in this batch use one shared branch target classifier.
- Conditional jumps count as executed instructions whether taken or not taken.

## 71. Phase 67 - Arithmetic, Branch, and Watchdog Integration Harness

### Goal

Run a validation-only harness over arithmetic, branch, and watchdog behavior.

### Required coverage

- `cmp` followed by equality, signed, and unsigned Jcc in the same source program.
- `jmp` and Jcc both use the same target classifier.
- Instruction-count watchdog behavior for a small loop made from `cmp`, Jcc, and `jmp`.
- Arithmetic fault paths from `mul`, `imul`, `div`, and `idiv` preserve no-partial-mutation guarantees.
- `lea`, `cmp`, `mul`, `imul`, `div`, `idiv`, `jmp`, and Jcc produce no successful memory-change rows unless the instruction actually writes memory in a later phase.
- Rendered diagnostics for one parser target error, one runtime target error, one division runtime error, and one instruction-limit error.

### Acceptance criteria

The harness is part of the aggregate test runner and adds no runtime features.

## 72. Phase 68 - Call Target Classification and Procedure Entry Metadata

### Goal

Add parser/link metadata for classifying CALL and INVOKE targets without executing CALL yet.

This is a parser and metadata phase. It must not add CALL execution, RET execution, stack mutation, Irvine routine execution, or root procedure termination behavior.

### Dependencies

- Procedure structural parsing from earlier `.code` / `PROC` / `ENDP` phases.
- Code label table and direct branch target validation.
- Stack initialization and PUSH/POP support should already exist before later CALL execution phases.

### Accepted syntax

This phase recognizes target names in metadata only:

```asm
main PROC
main ENDP

Helper PROC
Helper ENDP

SomeLabel:
```

It classifies target identifiers that appear in future CALL/INVOKE contexts but does not execute them yet.

### Required target classes

The target classifier must distinguish:

1. user procedure entry;
2. non-procedure code label;
3. supported virtual Irvine32 intrinsic;
4. known Irvine32 intrinsic that is recognized but not implemented yet;
5. explicit external/API/linker non-goal target;
6. data symbol;
7. numeric equate;
8. local symbol if local metadata exists;
9. unknown symbol;
10. malformed target expression.

### Tasks

1. Add procedure-entry metadata separate from ordinary code labels.
2. Preserve source declaration location for each procedure.
3. Add a target-classification helper used by future `call` and `invoke` phases.
4. Add a virtual Irvine32 symbol registry with at least these names classified:
   - supported-now or planned: `exit`, `Crlf`, `WriteChar`, `WriteString`, `WriteDec`, `WriteInt`, `WriteHex`, `WriteBin`, `DumpRegs`, `DumpMem`, `Randomize`, `Random32`, `RandomRange`, `WaitMsg`, `ReadChar`, `ReadInt`, `ReadDec`, `ReadHex`, `ReadString`;
   - explicit non-goal or deferred Windows/UI names as appropriate, such as `MsgBox`, `MsgBoxAsk`, file I/O routines, and color/cursor routines if not scheduled in v1.
5. Preserve case policy: instructions and Irvine routine names are case-insensitive; user-defined procedure and label names follow the active user-symbol case policy.
6. Reject duplicate procedure names, duplicate labels, and collisions with existing data/equate symbols according to the existing global symbol policy.
7. Add structured diagnostics for duplicate or invalid procedure metadata.
8. Do not implement CALL, RET, INVOKE, or Irvine routine execution.

### Diagnostics

- `duplicate-symbol`: duplicate procedure/label/data/equate name according to current symbol policy.
- `invalid-procedure-name`: malformed procedure name.
- `reserved-irvine-symbol`: user procedure collides with a reserved virtual Irvine name, if the project chooses to reserve those names.
- `unsupported-call-target`: only for tests of the classifier API, not user CALL syntax yet.

Diagnostics must include line, column, byte offset, and span length for the target or declaration name.

### Required tests

Native/parser tests:

- Classify `Helper` declared as `Helper PROC` as user procedure.
- Classify `SomeLabel:` as non-procedure code label.
- Classify `value DWORD 1` as data symbol.
- Classify `COUNT = 4` as numeric equate.
- Classify `WriteString` and `writestring` as virtual Irvine32 intrinsic with case-insensitive lookup.
- Prove `Helper` and `helper` are distinct user procedure names in MASM32 Educational Mode.
- Duplicate procedure/data/equate names produce structured diagnostics.

Source-run tests:

- Existing programs without CALL still run unchanged.
- Unknown future CALL syntax, if still unsupported, remains diagnosed as unsupported rather than silently accepted.

Rendered Simulator Messages tests:

- Duplicate symbol diagnostics render with stable code and source location.

### Acceptance criteria

A parser/classifier test can classify procedure, label, data, equate, virtual Irvine, unsupported external, and unknown targets without executing any CALL or INVOKE.

---

## 73. Phase 69 - Direct CALL to User Procedures

### Goal

Implement direct near CALL to user procedure entries.

This phase implements only `call procedureName` where the target is a user procedure entry. It must not implement RET, root RET behavior, CALL to code labels, CALL to registers/memory, far calls, INVOKE, PROC USES, LOCAL, or Irvine routine calls.

### Accepted syntax

```asm
call Helper
call helperName
```

Target must classify as a user procedure entry using the target classifier from **Phase 68 - Call Target Classification and Procedure Entry Metadata**.

### Rejected syntax

```asm
call eax
call [eax]
call DWORD PTR [eax]
call OFFSET Helper
call SomeLabel       ; if SomeLabel is not a PROC entry
call value           ; data symbol
call COUNT           ; numeric equate
call WriteString     ; virtual Irvine call deferred to Irvine call-dispatch phase
call ExitProcess     ; external/API non-goal
```

### Runtime semantics

1. Compute a 32-bit VM return token encoding the VM instruction index immediately after CALL. This token is not a native address.
2. Validate the call-depth limit before stack mutation if **Phase 72 - Call Depth Limit and Call Trace Diagnostics** has already been implemented. If Phase 72 is not implemented yet, Phase 69 must not add a temporary or partial call-depth limit; it must preserve the later Phase 72 ownership of depth accounting and diagnostics.
3. Decrement ESP by 4.
4. Write the return token to `[ESP]` through the central checked memory write helper.
5. Set VM instruction pointer to the target procedure entry instruction.
6. Record stack memory change for the pushed return token.
7. Preserve all modeled flags.

Phase-boundary note:

```text
Phase 69 owns direct CALL mechanics only:
  - classify a user procedure target;
  - push one VM return token;
  - branch to the procedure entry;
  - preserve modeled flags;
  - roll back on failed stack write.

Phase 69 does not own:
  - RET execution;
  - root procedure termination;
  - call-depth diagnostics;
  - PROC USES;
  - LOCAL frames;
  - INVOKE;
  - Irvine32 call dispatch.
```

### Runtime errors

- `stack-overflow`: ESP decrement or return-token write fails.
- `invalid-call-target`: target metadata became invalid after parsing; should normally be internal unless malformed IR is tested.

No partial mutation rule: if the return-token push fails, the instruction pointer must not change and call-depth metadata must not change.

### Diagnostics

Assembly diagnostics:

- `invalid-call-target` for data/equate/non-procedure label targets.
- `unsupported-call-form` for register, memory, far, or expression targets.
- `unsupported-external-call` for Windows/API/external targets.
- `unsupported-irvine-call` for virtual Irvine targets before their dispatch phase.

Source span should point to the target operand, not the `call` mnemonic, unless the whole instruction is malformed.

### Required tests

Parser tests:

- `call Helper` parses when `Helper PROC` exists.
- Rejected target classes produce correct diagnostic codes.
- Case-sensitive user procedure lookup is enforced.

Executor tests:

- CALL pushes one return token to stack.
- CALL branches to procedure entry.
- CALL preserves flags.
- Failed stack write does not change instruction pointer.

Source-run tests:

```asm
.code
main PROC
    mov eax, 1
    call Helper
    mov ebx, 3
main ENDP

Helper PROC
    mov eax, 2
    ; no ret yet: this program should stop according to current missing-RET behavior or be used only in executor-level tests until RET is implemented
Helper ENDP
END main
```

Because RET is not implemented yet, full source-run success should be deferred or use a harness that steps only through CALL. The source-run test for this phase should primarily validate parser diagnostics and executor stepping, not full program completion.

Rendered Simulator Messages tests:

- Invalid CALL target diagnostics render exact target class wording.

### Acceptance criteria

A VM stepping test executes one `call Helper`, observes ESP decreased by 4, observes a return token written through stack memory, and observes the VM instruction pointer set to `Helper`.

---

## 74. Phase 70 - RET Execution and Return Address Validation

### Goal

Implement near `ret` using VM return-address tokens pushed by CALL.

This phase implements plain `ret` only. It must not implement `ret imm16`, `leave`, root RET success, PROC USES, LOCAL cleanup, INVOKE cleanup, or far returns.

### Accepted syntax

```asm
ret
RET
```

### Rejected syntax

```asm
ret 4          ; deferred to Phase 74 - RET imm16 Instruction
ret eax
ret WORD PTR [esp]
retf
```

### Runtime semantics

1. Read a 32-bit return token from `[ESP]` through the central checked memory read helper.
2. Validate that the token maps to a valid VM instruction boundary or the reserved root-return sentinel.
3. Increment ESP by 4.
4. Set the VM instruction pointer to the mapped return instruction.
5. Decrease call depth if call-depth metadata exists.
6. Preserve all modeled flags.

### Runtime errors

- `stack-underflow`: read from `[ESP]` fails because ESP is outside the stack or below the valid range.
- `invalid-return-address`: popped token does not map to a valid VM return target.
- `return-with-empty-call-stack`: RET is executed when there is no non-root call frame and root RET is not yet enabled.

No partial mutation rule:

- If token read or token validation fails, instruction pointer must not change.
- ESP must remain externally unchanged on failed RET. Implementations may pre-validate before incrementing or roll back internally, but tests must observe no ESP or instruction-pointer mutation.

### Tests

Executor tests:

- CALL followed by RET returns to the instruction after CALL.
- RET preserves flags.
- Invalid token in stack memory produces `invalid-return-address` and does not branch.
- RET with ESP outside stack produces `stack-underflow`.
- Failed RET produces no memory-change rows.

Parser tests:

- `ret` accepted.
- `ret 4` is deferred to **Phase 74 - RET imm16 Instruction**, which models the caller-cleanup stack-adjustment form. Phase 70 implements only near `ret` with no immediate operand.
- `retf` rejected as explicit non-goal.

Source-run acceptance program:

```asm
.code
main PROC
    mov eax, 1
    call Helper
    mov ebx, eax
    exit
main ENDP

Helper PROC
    mov eax, 42
    ret
Helper ENDP
END main
```

Expected:

```text
EAX = 0000002Ah / 42
EBX = 0000002Ah / 42
```

This source-run program is an integrated regression after the root-termination phase. this phase must use executor-level CALL/RET tests and parser diagnostics only; it must not implement `exit` or root termination early.

Rendered Simulator Messages tests:

- `invalid-return-address` and `stack-underflow` render as runtime errors with source span on the RET instruction.

---

## 75. Phase 71 - Root Procedure Termination Semantics

### Goal

Define and implement successful termination at the entry procedure boundary.

This phase is about terminal-state semantics. It must not add new call forms, new stack-frame behavior, INVOKE, or Irvine output routines.

### Required behavior

1. Falling off the entry procedure terminates successfully.
2. `ret` from the entry procedure terminates successfully in MASM32 Educational Mode.
3. `exit`, when implemented by the earlier virtual Irvine terminator phase, continues to terminate successfully. If that phase is not part of the actual sequence, this phase relies on entry-procedure fallthrough and root RET only; it must not implement `exit` here.
4. Falling off a non-entry procedure without RET is a runtime error.
5. Returning to an instruction after CALL continues execution normally.
6. Only one terminal status may be emitted.

### Runtime diagnostics

- `non-root-procedure-fell-through`: a procedure other than the entry procedure reaches its ENDP/fallthrough boundary without RET.
- `invalid-root-termination-state`: internal mismatch between call depth/root state and instruction pointer.

### Tests

Source-run tests:

- Entry procedure fallthrough completes successfully.
- Entry procedure `ret` completes successfully after this phase.
- Non-entry procedure fallthrough after CALL reports runtime error.
- `exit` still completes successfully.
- No duplicate `execution-complete` messages are emitted.

Acceptance program:

```asm
.code
main PROC
    call Helper
    ret
main ENDP

Helper PROC
    mov eax, 7
    ret
Helper ENDP
END main
```

Expected:

```text
execution-complete
EAX = 00000007h / 7
```

Rendered Simulator Messages tests:

- Completion messages render exactly once.
- Non-root fallthrough renders with code `non-root-procedure-fell-through`.

---

## 76. Phase 72 - Call Depth Limit and Call Trace Diagnostics

### Goal

Add resource protection and diagnostics for recursive or runaway CALL chains.

This phase must not add new CALL forms or procedure syntax. It only adds depth accounting and diagnostics.

### Tasks

1. Add named configuration constant `default_call_depth_limit`.
2. Increment call depth only after a CALL is guaranteed to commit.
3. Decrement call depth only after RET target validation succeeds.
4. Stop before pushing a new return token if the next CALL would exceed the limit.
5. Include current depth, configured limit, target name, and source location in JSON diagnostics.
6. Add optional call trace metadata with procedure names and source locations.
7. Preserve deterministic behavior across Run and Step.

### Diagnostics

- `call-depth-exceeded`: resource-limit error.
- `call-trace-truncated`: optional simulator warning if a displayed trace is truncated.

### Tests

Native executor tests:

- CALL at depth `limit - 1` succeeds.
- CALL at depth `limit` fails before stack mutation.
- Failed depth check does not change ESP, memory, instruction pointer, or call trace.

Source-run tests:

- A recursive procedure hits `call-depth-exceeded` with deterministic instruction count.
- A non-recursive call chain under the limit succeeds.

Rendered Simulator Messages tests:

- Resource-limit diagnostic includes depth and limit.

Acceptance program:

```asm
.code
main PROC
    call Recur
    exit
main ENDP

Recur PROC
    call Recur
    ret
Recur ENDP
END main
```

Expected with a low test limit:

```text
[resource-limit-error] call-depth-exceeded
```

---

## 77. Phase 73 - LEAVE Instruction

### Goal

Implement `leave` as stack-frame teardown shorthand.

This phase must not implement `ret imm16`, ENTER, PROC USES, LOCAL, or automatic frame creation. It assumes the earlier stack/PUSH/POP milestone is complete, but LEAVE tests should prefer executor-level frame setup unless they intentionally exercise PUSH as a regression dependency.

### Accepted syntax

```asm
leave
LEAVE
```

### Rejected syntax

```asm
leave eax
enter 8, 0
```

### Runtime semantics

`leave` is equivalent to:

```asm
mov esp, ebp
pop ebp
```

Steps:

1. Validate that DWORD `[EBP]` is readable through checked memory helpers.
2. If validation fails, report the runtime diagnostic before mutating ESP or EBP.
3. On success, read saved EBP.
4. Commit `ESP = EBP + 4`.
5. Commit `EBP = savedEBP`.
6. Preserve all modeled flags.

### Runtime errors

- `stack-underflow` or `invalid-memory-read` if `[EBP]` cannot be read. The failure is validation-first and must not mutate ESP, EBP, flags, call depth, Program Console, or memory-change rows.
- No partial mutation rule: if the pop read fails, EBP must not change. The phase must define whether ESP is restored or whether assignment to ESP happens only after read validation. Prefer validate first, then commit.

### Tests

Executor tests:

- Valid frame: EBP points to saved EBP, LEAVE restores EBP and updates ESP.
- Flags preserve.
- Invalid EBP address reports runtime error without changing EBP.
- LEAVE creates no memory-change rows.

Source-run acceptance program:

```asm
.code
main PROC
    push ebp
    mov ebp, esp
    mov eax, 123
    leave
    exit
main ENDP
END main
```

Expected:

```text
EAX = 0000007Bh / 123
```

Rendered Simulator Messages tests:

- Malformed `leave eax` renders an assembly error.
- Invalid `[EBP]` renders a runtime memory error with source span on `leave`.

---

## 78. Phase 74 - RET imm16 Instruction

### Goal

Implement near `ret imm16` stack cleanup.

This phase must not implement far returns, calling-convention inference, INVOKE, or PROC parameter cleanup beyond the immediate operand behavior.

### Accepted syntax

```asm
ret 0
ret 4
ret 8
ret 16
ret 0010h
```

The operand is an unsigned 16-bit constant expression in bytes.

### Rejected syntax

```asm
ret -1
ret 10000h
ret eax
ret DWORD PTR [esp]
retf 4
```

### Runtime semantics

1. Pop and validate the return token exactly like plain RET.
2. After return-token validation succeeds, set `ESP = ESP + imm16` using 32-bit arithmetic.
3. Validate that final ESP remains inside or at the documented valid stack boundary.
4. Branch to the popped return target.
5. Preserve all modeled flags.

`ret imm16` does not read or zero the skipped argument bytes.

### Tests

Parser tests:

- Valid immediate range 0..65535.
- Negative, too-large, register, and memory operands rejected.

Executor tests:

- `ret 8` pops return token and then advances ESP by 8 more bytes.
- `ret 0` matches plain RET behavior.
- Flags preserve.
- Failed return-token validation does not apply the immediate stack adjustment.

Source-run acceptance program:

```asm
.code
main PROC
    push 2222h
    push 1111h
    call Callee
    mov eax, esp
    exit
main ENDP

Callee PROC
    mov ebx, 5
    ret 8
Callee ENDP
END main
```

Expected:

- `EBX = 00000005h / 5`.
- ESP after return equals the value before the two argument pushes.

Rendered Simulator Messages tests:

- Invalid immediate points at the immediate operand.
- Runtime failure points at the `ret imm16` instruction.

---

## 79. Phase 75 - PROC Metadata Baseline and Attribute Diagnostics

### Goal

Expand PROC parsing enough to preserve metadata and reject unsupported attributes clearly, without implementing USES or parameters yet.

### Accepted syntax

```asm
main PROC
Helper PROC
Helper ENDP
```

Existing bare PROC behavior must continue to work.

### Rejected syntax in this phase

```asm
MyProc PROC USES eax ebx       ; deferred to Phase 76 - PROC USES Parsing and Metadata; runtime save/restore starts in Phase 77 - PROC USES Runtime Save/Restore
MyProc PROC arg1:DWORD         ; later PROC parameter phase
MyProc PROC C                  ; language metadata deferred unless already accepted
MyProc PROC STDCALL            ; deferred unless explicitly accepted
MyProc PROC PUBLIC
MyProc PROC PRIVATE
MyProc PROC EXPORT
MyProc PROC FRAME
MyProc PROC NEAR
MyProc PROC FAR
```

### Tasks

1. Add a `ProcedureMetadata` structure if not already present.
2. Store procedure name, declaration location, body start instruction index, body end boundary, and current unsupported attributes list.
3. Reject unsupported PROC attributes with specific diagnostics rather than generic expected-line-end errors.
4. Preserve existing user-symbol case policy.
5. Do not change runtime behavior.

### Diagnostics

- `unsupported-proc-attribute`
- `invalid-proc-declaration`
- `proc-end-mismatch`
- `duplicate-procedure`

Source span should point at the unsupported attribute or mismatched name.

### Tests

Parser tests:

- Bare PROC still parses.
- Unsupported attributes produce targeted diagnostics.
- Mismatched `MyProc PROC` / `Other ENDP` reports `proc-end-mismatch`.
- Duplicate PROC name reports duplicate symbol.

Rendered Simulator Messages tests:

- Unsupported PROC attribute diagnostic includes source location and attribute name.

### Acceptance criteria

Bare PROC programs still run exactly as before, and unsupported PROC attributes produce clear, stable diagnostics.

---

## 80. Phase 76 - PROC USES Parsing and Metadata

### Goal

Parse and store `PROC USES reglist` metadata without generating save/restore code yet.

### Accepted syntax

```asm
MyProc PROC USES eax
MyProc PROC USES eax ebx ecx
MyProc PROC USES EAX EBX ESI EDI
```

### Supported registers

Initial supported USES registers:

```text
EAX EBX ECX EDX ESI EDI
```

`ESP` and `EBP` are rejected. `ESP` is the stack pointer, and `EBP` is reserved for the educational frame pointer once LOCAL runtime support exists.

8-bit and 16-bit aliases are rejected in USES lists.

### Rejected syntax

```asm
MyProc PROC USES esp
MyProc PROC USES ax
MyProc PROC USES al
MyProc PROC USES eax eax
MyProc PROC USES unknownReg
MyProc PROC USES eax, ebx    ; reject comma form unless explicitly supported
```

### Tasks

1. Extend PROC parser to accept `USES` followed by a whitespace-separated register list.
2. Store ordered USES register metadata.
3. Reject duplicate registers.
4. Reject ESP, aliases, invalid registers, and malformed lists.
5. Do not save or restore registers at runtime yet.
6. Add documentation that runtime USES behavior starts in **Phase 77 - PROC USES Runtime Save/Restore**.

### Tests

Parser tests:

- One, two, and many USES registers parse.
- Mixed-case registers parse.
- Duplicate and invalid registers fail.
- USES metadata preserves declared order.

Source-run tests:

- A procedure with USES metadata may be parsed in this phase, but runtime save/restore is not implemented until **Phase 77 - PROC USES Runtime Save/Restore**. If a procedure with USES metadata is called before Phase 77 behavior exists, source-run must report `unsupported-proc-uses-runtime` or an equivalent stable diagnostic. Silent execution that ignores USES metadata is not allowed.

Rendered Simulator Messages tests:

- Invalid USES register diagnostics point to the register token.

### Acceptance criteria

The parser can report `MyProc` has USES list `[EAX, EBX, ECX]`, and invalid USES lists are diagnosed without changing runtime behavior.

---

## 81. Phase 77 - PROC USES Runtime Save/Restore

### Goal

Implement runtime save/restore behavior for procedures with USES metadata.

This phase depends on CALL/RET and PUSH/POP. It must not implement LOCAL variables, PROC parameters, PROTO, or INVOKE.

### Runtime model

When entering a procedure with USES metadata through CALL:

1. Save listed registers by pushing them onto the stack in declared order.
2. On any RET from that procedure, restore the registers in reverse order before popping the return token.
3. All stack writes/reads must go through checked memory helpers.
4. USES save/restore must preserve modeled flags.
5. The return value convention is user-controlled: if EAX is in USES, EAX is restored; if not, EAX may be used as a return register.

### Required ordering example

```asm
MyProc PROC USES eax ebx
```

Entry pushes:

```text
push eax
push ebx
```

Exit restores:

```text
pop ebx
pop eax
```

Then RET pops the return token.

### Runtime errors

- `stack-overflow` during USES save.
- `stack-underflow` during USES restore.
- No partial mutation: failure during save must not branch into procedure with a partially committed frame. If full rollback is complex, the implementation must preflight stack capacity before any save writes.

### Tests

Executor/source-run tests:

- `USES ebx esi` preserves EBX and ESI across a call.
- EAX is not preserved if not listed.
- EAX is preserved if listed.
- Restore happens before return to caller.
- Stack pointer after call/return matches pre-call value.
- Stack overflow during save produces runtime error before procedure body executes.

Acceptance program:

```asm
.code
main PROC
    mov ebx, 1111h
    call Helper
    mov eax, ebx
    exit
main ENDP

Helper PROC USES ebx
    mov ebx, 2222h
    ret
Helper ENDP
END main
```

Expected:

```text
EAX = 00001111h / 4369
EBX = 00001111h / 4369
```

Rendered Simulator Messages tests:

- Stack failure during USES save/restore renders as runtime error with procedure name.

---

## 82. Phase 78 - LOCAL Declaration Parser and Frame Layout Metadata

### Goal

Parse supported LOCAL declarations and compute procedure-local frame metadata without allocating stack storage yet.

This is parser and metadata work only.

### Accepted syntax

Inside a procedure body before executable instructions:

```asm
LOCAL temp:DWORD
LOCAL ch:BYTE
LOCAL signedVal:SDWORD
LOCAL buf[16]:BYTE
LOCAL a:DWORD, b:DWORD
```

### Supported local types

```text
BYTE SBYTE WORD SWORD DWORD SDWORD
```

### Rejected syntax

```asm
LOCAL q:QWORD
LOCAL sq:SQWORD
LOCAL r:REAL4
LOCAL s:STRUCTTYPE
LOCAL x DWORD        ; missing colon
LOCAL x:DWORD = 1    ; initializer rejected
LOCAL buf[0]:BYTE
LOCAL buf[-1]:BYTE
LOCAL buf[COUNT + eax]:BYTE
```

### Metadata rules

- LOCAL declarations are valid only inside PROC bodies.
- LOCAL declarations must appear before executable instructions in that procedure for the first implementation.
- Local names are scoped to the procedure. A LOCAL symbol may shadow a global data symbol or numeric equate only inside that procedure scope. A LOCAL must not collide with another local, parameter, PROC name, or label in the same procedure.
- Local offsets are negative EBP-relative offsets.
- Compute byte size and alignment using deterministic named rules: declaration order, alignment `min(size, 4)`, and total frame allocation rounded up to a 4-byte boundary. This exact layout is tested.
- Store declaration source location for diagnostics and debugger display.

### Tasks

1. Parse supported LOCAL declarations.
2. Add local symbol metadata scoped by procedure.
3. Compute frame offsets but do not generate stack allocation yet.
4. Reject unsupported forms with targeted diagnostics.
5. Do not resolve local operands in instructions yet.

### Diagnostics

- `local-outside-procedure`
- `local-after-instruction`
- `unsupported-local-type`
- `invalid-local-declaration`
- `duplicate-local-symbol`
- `invalid-local-count`

### Tests

Parser tests:

- Scalars and arrays parse.
- Comma-separated LOCAL declarations parse.
- Offsets are deterministic.
- Duplicate locals fail.
- Local/global shadowing behavior follows the spec decision and is tested.

Rendered Simulator Messages tests:

- Unsupported type and invalid count diagnostics render with source spans.

### Acceptance criteria

A parser test can inspect a procedure's local metadata and see names, types, sizes, counts, and EBP-relative offsets.

---

## 83. Phase 79 - LOCAL Stack Allocation and Lifetime

### Goal

Allocate and release stack storage for LOCAL metadata during procedure execution.

This phase must not yet allow source operands such as `mov localVar, eax`; operand resolution comes next.

### Runtime model

On entry to a procedure with LOCAL metadata:

1. Save old EBP through the checked stack write path.
2. Set EBP to the frame base.
3. Reserve the total local byte size, rounded up to a 4-byte boundary, by subtracting from ESP.
4. Validate the entire stack range before committing allocation.
5. Mark local byte ranges as uninitialized-origin if that validation mode exists.
6. Release local storage and restore EBP during the fixed epilogue before returning to caller.

### Required frame policy

This phase uses the automatic educational frame policy. On CALL to a procedure with LOCAL metadata, the VM creates a frame equivalent to saving old EBP, setting EBP to the frame base, and subtracting the rounded local-frame size from ESP. On normal procedure return, the fixed epilogue releases the local allocation and restores EBP before popping the return token. Manual frame setup is not required for LOCAL variables. User-written `leave` must match this frame model and is tested separately; malformed manual frame manipulation is diagnosed at runtime through checked stack reads.

### Runtime errors

- `stack-overflow` if local allocation would move ESP outside the stack region.
- `invalid-frame-state` if local release or frame restoration metadata is inconsistent.

No partial mutation rule:

- Failed local allocation must not execute the procedure body.
- Failed allocation must not partially decrement ESP.

### Tests

Executor/source-run tests:

- Procedure with one DWORD local allocates 4 bytes and releases it before return.
- Procedure with byte array local allocates exact declared size plus the fixed 4-byte total-frame padding/alignment policy.
- Stack overflow during local allocation fails before body executes.
- Nested calls each receive separate local storage.
- Recursive calls receive separate local storage until call-depth or stack limit is reached.

Acceptance program:

```asm
.code
main PROC
    call Helper
    mov eax, esp
    exit
main ENDP

Helper PROC
    LOCAL temp:DWORD
    mov ebx, 5
    ret
Helper ENDP
END main
```

Expected:

- `EBX = 00000005h / 5`.
- ESP after return equals ESP before the call.

Rendered Simulator Messages tests:

- Stack allocation failure renders with procedure name and local byte count.

---

## 84. Phase 80 - LOCAL Operand Resolution and Addressing

### Goal

Allow supported instructions to read and write LOCAL variables through frame-relative addresses.

### Accepted syntax

```asm
mov temp, eax
mov eax, temp
mov BYTE PTR buf[0], 'A'
mov al, BYTE PTR buf[1]
lea eax, temp
lea eax, buf
```

### Width rules

- Local scalar declarations provide width metadata.
- Local array element offsets are byte offsets, matching global symbol offset policy.
- Explicit PTR overrides may narrow or widen within the allocated local object if the final access range remains inside the local object.
- QWORD/SQWORD executable accesses remain rejected in MASM32 Educational Mode.

### Address rules

- Local addresses are computed as EBP-relative effective addresses.
- All reads and writes go through central checked memory helpers.
- Object-bound and uninitialized-origin validation modes apply to locals.
- `.CONST` does not apply to local storage.

### Rejected syntax

```asm
mov eax, OFFSET temp      ; use LEA or ADDR in scoped contexts unless this phase explicitly supports OFFSET local
mov eax, temp[eax*4]     ; scaled index deferred
mov qword ptr temp, eax  ; executable 64-bit rejected
```

### Tests

Parser/source-run tests:

- Read/write DWORD local.
- Read/write BYTE local array by byte offset.
- `lea eax, temp` returns address without reading memory.
- Out-of-range local offset reports object-bound diagnostic in strict mode.
- Reading uninitialized local reports uninitialized-read warning/error in relevant modes.
- Same local name in two procedures resolves to the active procedure's local.

Acceptance program:

```asm
.code
main PROC
    call Helper
    exit
main ENDP

Helper PROC
    LOCAL temp:DWORD
    mov temp, 42
    mov eax, temp
    ret
Helper ENDP
END main
```

Expected:

```text
EAX = 0000002Ah / 42
```

Rendered Simulator Messages tests:

- Unknown local outside scope points at the local token.
- Object-bound/uninitialized local diagnostics render with local name and byte offset.

---

## 85. Phase 81 - PROTO Metadata Parser

### Goal

Parse a limited MASM-style PROTO subset as metadata only.

This phase must not implement INVOKE lowering, external linking, Windows API calls, or calling convention execution.

### Accepted syntax

```asm
MyProc PROTO
MyProc PROTO :DWORD
MyProc PROTO arg1:DWORD, arg2:DWORD
MyProc PROTO pStr:PTR BYTE
```

Initial supported parameter types:

```text
BYTE WORD DWORD SBYTE SWORD SDWORD PTR BYTE PTR WORD PTR DWORD
```

All parameters are treated as 32-bit stack argument slots for initial INVOKE lowering unless a later phase documents smaller coercion behavior.

Parameter names are required for parameter entries in the first implementation. Supported first-form parameter syntax is `paramName:DWORD` or `paramName:SDWORD`; unnamed parameters, VARARG, pointer types, structures, and non-DWORD types are rejected until later phases.

### Rejected syntax

```asm
ExitProcess PROTO :DWORD       ; external/API target rejected or marked non-goal
MyProc PROTO VARARG            ; deferred
MyProc PROTO :REAL4            ; deferred
MyProc PROTO :QWORD            ; executable 64-bit deferred
MyProc PROTO NEAR C, x:WORD    ; language/distance metadata deferred unless explicitly supported
```

### Tasks

1. Add PROTO symbol metadata separate from data/procedure/equate symbols.
2. Reject duplicate or conflicting prototypes.
3. Link a PROTO to a later PROC with the same name if present, with diagnostics for incompatible metadata.
4. Preserve parameter source locations.
5. Do not generate code.

### Diagnostics

- `invalid-proto-declaration`
- `unsupported-proto-type`
- `unsupported-external-proto`
- `duplicate-proto`
- `proto-proc-mismatch`

### Tests

Parser tests:

- Zero-arg and multi-arg prototypes parse.
- Parameter names are required for parameter entries in the first implementation. Missing parameter names are rejected with `invalid-proto-declaration`.
- Unsupported types and VARARG reject clearly.
- Windows/API prototype target produces non-goal diagnostic.

Rendered Simulator Messages tests:

- Unsupported PROTO type points at the type token.

### Acceptance criteria

A parser test can inspect `MyProc PROTO arg1:DWORD, p:SDWORD` and see parameter count, names, types, and source locations. Unsupported pointer forms such as `p:PTR BYTE` are rejected until a later phase.

---

## 86. Phase 82 - ADDR Operator for INVOKE Arguments

### Goal

Support `ADDR` as a limited INVOKE-argument address operator.

This phase must not make ADDR a general instruction operand unless explicitly decided here. It must not implement full INVOKE lowering.

### Accepted syntax

Inside INVOKE argument parsing:

```asm
INVOKE MyProc, ADDR globalVar
INVOKE MyProc, ADDR buffer
INVOKE MyProc, ADDR localVar
```

### Address behavior

- `ADDR globalDataSymbol` produces the symbol address, equivalent to `OFFSET` in flat educational mode.
- `ADDR constSymbol` produces the `.CONST` symbol address for reads or by-reference arguments; write protection remains enforced if later code writes through it.
- `ADDR localSymbol` produces the current frame-relative address of the local.
- ADDR returns a 32-bit address value.

### Rejected syntax

```asm
ADDR eax
ADDR 123
ADDR COUNT
ADDR [eax]
ADDR globalVar + 4        ; defer until general ADDR expression support
mov eax, ADDR globalVar   ; reject unless general ADDR operands are explicitly supported
```

### Tasks

1. Extend argument parser to recognize ADDR operands.
2. Resolve data, const, data?, and local symbols.
3. Preserve source span for ADDR and the operand.
4. Reject unsupported ADDR targets with specific diagnostics.
5. `ADDR localVar` requires local symbol operand resolution and frame-relative addressing from **Phase 80 - LOCAL Operand Resolution and Addressing**. Before Phase 80 exists, it must either be helper-tested without runtime execution or rejected with `addr-local-not-available`. Do not implement LOCAL operand resolution or LOCAL frame addressing in Phase 82.

Phase-boundary note:

`ADDR globalVar` and `ADDR constSymbol` may use global symbol addresses already available through existing data/const metadata. `ADDR localVar` is different: it depends on an active stack frame and local-symbol addressing. A future assistant must not implement Phase 80 local addressing merely to make Phase 82 tests pass.
6. Add tests independent of full INVOKE lowering if necessary by testing argument-lowering helpers.

### Diagnostics

- `invalid-addr-target`
- `addr-outside-invoke`
- `unknown-addr-symbol`
- `unsupported-addr-expression`

### Tests

Parser/helper tests:

- ADDR global data resolves to address.
- ADDR local resolves to EBP-relative runtime address in helper context.
- ADDR const resolves but does not bypass write protection.
- ADDR register/immediate/equate rejected.
- ADDR outside INVOKE rejected or unsupported according to chosen scope.

Rendered Simulator Messages tests:

- ADDR diagnostics point to the invalid target operand.

### Acceptance criteria

An INVOKE argument parser can lower `ADDR msg` to a 32-bit address argument with correct source metadata.

---

## 87. Phase 83 - INVOKE Zero-Argument User Procedure Calls

### Goal

Support the simplest INVOKE form: zero-argument calls to user procedures.

This phase must not implement argument pushes, ADDR arguments, PROC parameter validation, external calls, or Windows APIs.

### Accepted syntax

```asm
INVOKE Helper
invoke Helper
```

Target must classify as a user procedure entry with zero parameters or no prototype requiring parameters.

### Rejected syntax

```asm
INVOKE Helper, 1
INVOKE WriteString
INVOKE ExitProcess, 0
INVOKE value
INVOKE Unknown
```

### Runtime semantics

`INVOKE Helper` lowers to the same runtime behavior as `call Helper`.

### Tasks

1. Parse zero-argument INVOKE.
2. Reuse call target classification.
3. Lower to direct CALL IR or equivalent internal operation.
4. Preserve INVOKE source text for diagnostics and debugger display.
5. Do not implement argument cleanup.

### Diagnostics

- `invalid-invoke-target`
- `invoke-arguments-not-supported-yet`
- `unsupported-external-invoke`
- `unsupported-irvine-invoke` if Irvine INVOKE is not supported.

### Tests

Source-run acceptance program:

```asm
.code
main PROC
    INVOKE Helper
    exit
main ENDP

Helper PROC
    mov eax, 55
    ret
Helper ENDP
END main
```

Expected:

```text
EAX = 00000037h / 55
```

Regression tests:

- `call Helper` still works.
- INVOKE does not require PROTO for same-file zero-argument user procedures in this phase. Unknown, external/API, data, equate, or Irvine targets are rejected unless explicitly supported.
- INVOKE with args produces targeted diagnostic.

Rendered Simulator Messages tests:

- External/API INVOKE renders as explicit non-goal.

---

## 88. Phase 84 - INVOKE DWORD Argument Lowering and Cleanup

### Goal

Support a limited educational subset of INVOKE arguments for user procedures.

This phase must not implement Windows API calls, varargs, register calling conventions, 64-bit args, structures, floating point, or full MASM coercion.

### Accepted syntax

```asm
INVOKE Helper, 1
INVOKE Helper, eax
INVOKE Helper, value
INVOKE Helper, OFFSET msg
INVOKE Helper, ADDR msg
INVOKE Helper, ADDR localVar
```

### Argument rules

- Initial arguments are 32-bit stack slots.
- Arguments are pushed right-to-left.
- Immediate and constant-expression arguments must fit 32-bit signed or unsigned context according to existing immediate rules.
- Register arguments read the full 32-bit register only for the first implementation. 8-bit/16-bit aliases require explicit extension before INVOKE and are rejected as direct arguments unless implemented explicitly.
- Data symbol arguments load the 32-bit value at the symbol if the symbol width is DWORD/SDWORD. Passing addresses requires OFFSET or ADDR.
- ADDR local/global produces a 32-bit address.

### Cleanup policy

Initial cleanup policy is deterministic: INVOKE with DWORD arguments uses stdcall-style callee cleanup and is accepted only when PROTO/PROC metadata proves the callee returns with `ret imm16` matching the pushed argument byte count. If the callee returns with plain `ret`, or if matching cleanup metadata is unavailable, source-run reports `invoke-cleanup-mismatch`. Silent stack leaks are not allowed.

### Diagnostics

- `invoke-argument-count-mismatch`
- `unsupported-invoke-argument`
- `invoke-cleanup-mismatch`
- `invoke-argument-width-unsupported`
- `invalid-addr-target`
- `unsupported-external-invoke`

### Tests

Parser/source-run tests:

- Push order is right-to-left.
- `INVOKE Helper, 1, 2` results in first parameter at the documented stack/frame location.
- ADDR data passes address, not pointed-to value.
- Register argument reads source before any pushes mutate ESP.
- Argument count mismatch against PROTO reports diagnostic.
- External/API target reports non-goal diagnostic.

Acceptance program using explicit `ret 8` cleanup:

```asm
.code
main PROC
    INVOKE AddTwo, 2, 3
    exit
main ENDP

AddTwo PROC
    ; exact parameter access depends on the selected frame/argument policy
    ; test should assert EAX becomes 5
    ret 8
AddTwo ENDP
END main
```

If parameter-name access is not yet implemented, use a lower-level stack access test and document it.

Rendered Simulator Messages tests:

- Cleanup mismatch and unsupported argument forms render stable diagnostics with argument source spans.

---

## 89. Phase 85 - Program Console Buffer and Stream Separation

### Goal

Create the VM Program Console stream as a separate runtime output channel.

This phase must not implement Irvine output routines yet. It is infrastructure only.

### Tasks

1. Add a VM console module, for example `vm_console.c/.h`.
2. Store console output bytes/text in VM state.
3. Return Program Console content separately from Simulator Messages in source-run JSON.
4. Clear Program Console on a new run.
5. Preserve Program Console across stepping until reset or rerun according to debugger state policy.
6. Ensure diagnostics never append to Program Console.
7. Ensure Program Console output never appears as a Simulator Message.
8. Add Doxygen comments and file headers.

### JSON contract

Source-run JSON must include separate fields such as:

```json
{
  "programConsole": {
    "text": "...",
    "truncated": false,
    "byteCount": 0,
    "lineCount": 0
  },
  "simulatorMessages": [ ... ]
}
```

Exact names may differ, but stream separation must be testable.

### Tests

Native C tests:

- Console append API appends text deterministically.
- Reset clears console.
- Console API handles zero-length appends.

Source-run tests:

- Successful no-output program returns empty Program Console and normal Simulator Messages.
- Assembly error returns diagnostics and empty Program Console.

Node formatter tests:

- UI formatter renders Program Console and Simulator Messages separately.

### Acceptance criteria

A source-run result has an empty Program Console stream and a separate execution-complete Simulator Message for a no-output program.

---

## 90. Phase 86 - Program Console Output Limits and Serialization

### Goal

Add deterministic output limits and robust serialization for Program Console.

This phase must not add Irvine routines. It only hardens console infrastructure.

### Tasks

1. Add named config constants:
   - `default_console_max_bytes`;
   - `default_console_max_lines`.
2. Track byte count and line count.
3. Define newline counting policy.
4. Stop execution with `resource-limit-error console-output-limit-exceeded` by default when a limit would be exceeded.
5. Ensure failed append does not partially append output unless a documented truncation mode is selected.
6. Add JSON fields for limit status.
7. Add rendered Simulator Messages tests for output-limit diagnostics.

### Tests

Native tests:

- Appending under limit succeeds.
- Appending exactly to limit succeeds.
- Appending over byte limit fails without partial append.
- Appending over line limit fails without partial append.

Source-run tests:

- Synthetic console-producing test harness triggers output limit.
- Diagnostics go to Simulator Messages, not Program Console.

### Acceptance criteria

When output exceeds the configured limit, execution stops with:

```text
[resource-limit-error] console-output-limit-exceeded
```

and Program Console contains only the fully committed output before the failing append.

---

## 91. Phase 87 - Irvine32 Crlf

### Goal

Implement the virtual Irvine32 `Crlf` routine as a focused Program Console output milestone.

### Runtime contract

- Accepted call shape: `call Crlf` and equivalent supported virtual-call form.
- Append exactly one internal Program Console newline sequence `\n`.
- Do not read or modify registers or flags.
- Respect Program Console output limits with no partial output.

### Required tests

- `Crlf` appends exactly `\n`.
- Consecutive `Crlf` calls append two newlines in order.
- Output-limit failure appends no partial newline.
- Program Console and Simulator Messages remain separate.

## 92. Phase 88 - Irvine32 WriteChar

### Goal

Implement virtual Irvine32 `WriteChar` independently from newline and string routines.

### Runtime contract

- Use the low byte of `AL` as the output byte.
- Render according to the ASCII Program Console byte policy.
- Preserve all registers and modeled flags.
- Respect output limits with no partial output.

### Required tests

- `AL = 'A'` outputs `A`.
- `EAX` high bytes do not affect output.
- Flags and registers are preserved.
- Output-limit diagnostics render through Simulator Messages.

### Shared Irvine32 Memory-Read and Program Console Output Policy

This shared policy applies to every future Irvine32 routine that reads simulated memory or appends to Program Console. It is a standing implementation rule for the Irvine32 routine phases. A later phase may add routine-specific behavior, but it must not contradict this policy unless the phase explicitly says it is changing the shared policy and adds tests for the change.

Memory-reading Irvine32 routines must use the same checked VM memory helper path as instruction execution.

Required memory-read ordering:

1. Resolve the routine input registers or operands according to the routine's phase contract.
2. Compute every planned memory read address and width needed before committing routine-visible output when the routine has a no-partial-output policy.
3. Apply mandatory address, range, region, permission, and overflow checks through checked VM memory helpers.
4. Apply `.CONST` rules normally: `.CONST` reads are allowed, `.CONST` writes remain protected elsewhere.
5. Apply optional section-capacity, section-image, and declared-object validation policies only after mandatory Level 1 region checks succeed.
6. Apply uninitialized-origin diagnostics according to the current global uninitialized-read policy.
7. If a strict policy emits a runtime error, stop before the routine consumes the bytes for output and before appending partial Program Console text.
8. If a warning policy emits a non-fatal warning, continue using the deterministic simulator bytes unless a later fatal diagnostic occurs.

Current default user-facing uninitialized-read behavior after Phase 53C - Default Teaching Diagnostics for Existing Warning Modes is warning mode. Therefore, a future Irvine32 routine that reads `.DATA?`, `?`, or `DUP(?)` bytes must emit `uninitialized-read` as a non-fatal Simulator Messages warning by default when those bytes still carry uninitialized-origin metadata.

Explicit uninitialized-read opt-out suppresses only the `uninitialized-read` teaching warning. It does not disable mandatory VM memory safety, `.CONST` protection, invalid-region errors, unaligned-access warnings, output-limit diagnostics, or unrelated diagnostics.

Strict uninitialized-read mode stops before the routine consumes uninitialized-origin bytes for output and before it appends partial Program Console text.

Program Console output must remain separate from Simulator Messages.

Required Program Console output rules:

- Program output goes only to Program Console.
- Diagnostics, warnings, notices, runtime errors, and execution-status messages go only to Simulator Messages.
- A routine with a no-partial-output policy must preflight memory reads and output length before appending text.
- If output-limit validation fails, the routine must emit the appropriate Simulator Messages diagnostic and append no partial Program Console text.
- Routine failures must preserve registers and modeled flags unless the routine-specific phase explicitly documents a different mutation contract.

Future Irvine32 routine phases must not copy older pre-Phase-53C wording that says default mode emits no uninitialized-read diagnostic. That historical behavior was superseded by Phase 53C - Default Teaching Diagnostics for Existing Warning Modes.

Required tests for each future Irvine32 routine that reads memory:

- default uninitialized-read warning test when reading `.DATA?`, `?`, or `DUP(?)` bytes;
- explicit uninitialized-read off test;
- strict uninitialized-read stop-before-output test;
- invalid address no-partial-output test;
- Program Console / Simulator Messages separation test.

## 93. Phase 89 - Irvine32 WriteString

### Goal

Implement virtual Irvine32 `WriteString` with checked memory reads.

### Accepted syntax

```asm
call WriteString
```

### Runtime behavior

- `EDX` contains the address of a null-terminated byte string.
- The routine pre-scans one byte at a time through checked memory helpers until the first `00h`, `string_scan_limit_bytes`, or memory/output validation failure.
- Reading stops at byte `00h`.
- Nonzero bytes before the terminator are appended to Program Console using the console byte-to-text policy only after validation succeeds.
- The null terminator is not printed.
- Modeled flags are preserved.
- `EDX` is preserved.
- The routine follows the shared Irvine32 memory-read and Program Console output policy immediately above this phase.

### Runtime diagnostics

- `invalid-memory-read` if any byte read is outside readable memory.
- `string-scan-limit-exceeded` / compatibility alias `unterminated-string-output` if no terminator is found before `string_scan_limit_bytes`.
- `console-output-limit-exceeded` if the validated output would exceed the configured console output limit.
- `uninitialized-read` warning/error according to the current global uninitialized-read policy when scanned bytes still carry uninitialized-origin metadata. After Phase 53C - Default Teaching Diagnostics for Existing Warning Modes, omitted/default user-facing behavior emits `uninitialized-read` as a non-fatal warning.

### No-partial-output policy

`WriteString` must pre-scan until terminator, `string_scan_limit_bytes`, or configured output limit using checked reads, then append only if the full string is valid and within output limits. Runtime failure must append no partial string.

Strict uninitialized-read mode is a runtime failure for this policy. It must stop before appending Program Console output.

Default warning mode is not a runtime failure. It emits `uninitialized-read` through Simulator Messages and continues using deterministic simulator bytes unless a later fatal diagnostic occurs.

Explicit uninitialized-read off suppresses only the `uninitialized-read` teaching warning. It does not suppress invalid-memory, output-limit, unterminated-string, unaligned-access, or unrelated diagnostics.

### Tests

Source-run acceptance program:

```asm
.data
msg BYTE "Hello", 0
.code
main PROC
    mov edx, OFFSET msg
    call WriteString
    exit
main ENDP
END main
```

Expected Program Console:

```text
Hello
```

Additional tests:

- Empty string prints nothing.
- Missing null terminator fails with `string-scan-limit-exceeded` / `unterminated-string-output` before unbounded scan.
- Invalid `EDX` fails with runtime diagnostic and no partial Program Console output.
- String in `.CONST` reads successfully.
- String in initialized `.data` reads successfully without `uninitialized-read`.
- String in `.DATA?` with zero first byte prints nothing, emits default `uninitialized-read` warning unless uninitialized-read diagnostics are explicitly off, and emits no warning when the byte has been initialized by a prior write.
- String in `.DATA?` with explicit uninitialized-read off prints according to deterministic zero-filled bytes and emits no `uninitialized-read` warning.
- String in `.DATA?` with strict uninitialized-read mode stops before appending Program Console output.
- Program Console and Simulator Messages remain separated.

Rendered Simulator Messages tests:

- Invalid address diagnostics point to `call WriteString` and include the `EDX` address.
- Unterminated string diagnostics point to `call WriteString` and include the `EDX` address and scan limit.
- Default uninitialized-origin string warning renders as `simulator-warning uninitialized-read` and execution can still complete.
- Strict uninitialized-origin string failure renders as `runtime-error uninitialized-read` and no `execution-complete` message follows.

---

## 94. Phase 90 - Irvine32 WriteDec

### Goal

Implement virtual Irvine32 `WriteDec` as unsigned decimal formatting of `EAX`.

### Runtime contract

- Format `EAX` as an unsigned 32-bit decimal value.
- Preserve registers and flags.
- Preflight output length and append no partial digits on output-limit failure.

### Required tests

- `0`, `1`, and `4294967295`.
- Output-limit failure before and at boundary.
- No Program Console mutation on failure.

## 95. Phase 91 - Irvine32 WriteInt

### Goal

Implement virtual Irvine32 `WriteInt` as signed decimal formatting of `EAX`.

### Runtime contract

- Interpret `EAX` as signed 32-bit two's-complement.
- Emit a leading minus sign for negative values.
- Preserve registers and flags.
- Preflight output length and append no partial digits on output-limit failure.

### Required tests

- `0`, `1`, `-1`, `2147483647`, and `-2147483648`.
- Output-limit failure before sign, after sign, and at boundary.
- No Program Console mutation on failure.

## 96. Phase 92 - Irvine32 WriteHex

### Goal

Implement virtual Irvine32 `WriteHex` as fixed-width hexadecimal formatting of `EAX`.

### Runtime contract

- Use `EAX` as an unsigned 32-bit value.
- Emit exactly eight uppercase hexadecimal digits with no prefix or suffix in v1.
- Preserve registers and flags.
- Preflight output length and append no partial digits on output-limit failure.

### Required tests

- `00000000`, `00000001`, `0000000A`, `7FFFFFFF`, and `FFFFFFFF`.
- Output-limit failure appends no partial formatted value.

## 97. Phase 93 - Irvine32 WriteBin

### Goal

Implement virtual Irvine32 `WriteBin` as fixed-width binary formatting of `EAX`.

### Runtime contract

- Use `EAX` as an unsigned 32-bit value.
- Emit exactly 32 binary digits with no separators in v1.
- Preserve registers and flags.
- Preflight output length and append no partial digits on output-limit failure.

### Required tests

- all-zero, all-one, low-bit, high-bit, and alternating-bit values.
- Output-limit failure appends no partial formatted value.

## 98. Phase 94 - Irvine32 DumpRegs

### Goal

Implement deterministic virtual Irvine32 `DumpRegs` output for modeled VM state.

### Runtime behavior

- `DumpRegs` writes canonical registers and modeled flags to Program Console.
- It reports EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, EIP/VM instruction token, and EFLAGS or modeled flag bits.
- It reports only modeled flags with exact labels: CF, ZF, SF, OF. Other real Irvine flags are omitted in the first implementation.
- It preserves all registers and flags.
- It writes no Simulator Messages unless a console output limit is exceeded.

### Formatting contract

The first implementation must use an exact deterministic multi-line format. Example shape:

```text
EAX=00000001 EBX=00000002 ECX=00000000 EDX=00000000
ESI=00000000 EDI=00000000 EBP=00000000 ESP=007FFFE0
EIP=00000003 EFLAGS=00000041
CF=1 ZF=1 SF=0 OF=0
```

Exact spacing, register order, uppercase hex width, newline placement, and flag labels are part of the golden Program Console contract and must not differ across implementations.

### Tests

Source-run tests:

- Registers and flags appear with expected values.
- Program Console contains DumpRegs output.
- Simulator Messages contain only execution status.
- DumpRegs preserves VM state.
- Output limit failure is handled.

Rendered output tests:

- Program Console formatter preserves newlines and spacing.

Acceptance criteria

A program setting EAX and flags then calling DumpRegs produces exact golden Program Console text.

---

## 99. Phase 95 - Irvine32 DumpMem

### Goal

Implement deterministic virtual Irvine32 `DumpMem` with checked memory reads.

### Calling convention for first implementation

Use the common Irvine-style inputs:

```text
ESI = starting address
ECX = element count
EBX = element size in bytes: 1, 2, or 4
```

The ESI/ECX/EBX contract above is mandatory for this phase; no alternate calling convention is allowed in this batch.

### Runtime behavior

- Validate EBX is 1, 2, or 4.
- Validate ECX count against a named max dump count to avoid huge output.
- Read every element through checked memory helpers.
- Use preflight validation before appending output, so invalid memory does not produce partial dump output.
- Preserve registers and modeled flags.
- Write dump output to Program Console only.

### Diagnostics

- `invalid-dumpmem-element-size`
- `dumpmem-count-limit-exceeded`
- `invalid-memory-read`
- `console-output-limit-exceeded`

### Tests

Source-run acceptance program:

```asm
.data
arr BYTE 01h, 02h, 03h, 04h
.code
main PROC
    mov esi, OFFSET arr
    mov ecx, 4
    mov ebx, 1
    call DumpMem
    exit
main ENDP
END main
```

Expected Program Console must match the exact selected dump format.

Additional tests:

- EBX = 1 formatting.
- EBX = 2 formatting.
- EBX = 4 formatting.
- EBX invalid value rejected with `invalid-dumpmem-element-size`.
- ECX = 0 produces empty or header-only output according to the exact documented DumpMem format.
- ECX above the named max dump count fails with `dumpmem-count-limit-exceeded`.
- Invalid starting address fails before partial Program Console output.
- Address range that starts valid but crosses into unreadable memory fails before partial Program Console output.
- `.CONST` memory can be dumped.
- Dumping initialized `.data` memory emits no `uninitialized-read` diagnostic.
- Dumping `.DATA?`, `?`, or `DUP(?)` bytes that still carry uninitialized-origin metadata emits default `uninitialized-read` warning after Phase 53C - Default Teaching Diagnostics for Existing Warning Modes and then continues if no fatal diagnostic occurs.
- Dumping the same `.DATA?` bytes with explicit uninitialized-read off emits no `uninitialized-read` warning and uses deterministic zero-filled bytes.
- Dumping the same `.DATA?` bytes with strict uninitialized-read mode stops before appending partial Program Console output.
- Dumping bytes that were originally `.DATA?` but have all been overwritten by the simulated program emits no `uninitialized-read` warning.
- Program Console contains only DumpMem output.
- Simulator Messages contain diagnostics, warnings, and execution status only.
- Registers and modeled flags are preserved on success.
- Registers, modeled flags, Program Console output, and memory-change rows are preserved on fatal failure.

Rendered Simulator Messages tests:

- Invalid memory read points to `call DumpMem` and includes ESI/ECX/EBX metadata.
- Invalid element size points to `call DumpMem` and includes the rejected EBX value.
- Dump count limit failure points to `call DumpMem` and includes ECX plus the configured limit.
- Default uninitialized-origin dump warning renders as `uninitialized-read` and identifies the final read range and byte count.
- Strict uninitialized-origin dump failure renders as `runtime-error uninitialized-read` and no `execution-complete` message follows.

---

## 100. Phase 96 - Deterministic Irvine32 Random Runtime State

### Goal

Add a simulator-owned deterministic pseudorandom state for future Irvine32 random routines.

This phase must not implement Randomize, Random32, or RandomRange calls yet.

### Requirements

1. Add RNG state to VM state.
2. Implement xorshift32 exactly: `x ^= x << 13; x ^= x >> 17; x ^= x << 5`, with unsigned 32-bit wrapping after each operation.
3. Add named default seed constant `0x12345678`.
4. Ensure reset/re-run produces deterministic RNG state unless project settings specify a different seed.
5. Ensure shared project state has either a documented deterministic-seed field or an explicit future-owner note so later settings/share phases can carry a seed without changing this phase's RNG semantics.
6. Add tests for repeatability and the documented first five values: `87985AA5h`, `155B24A3h`, `4820F4C4h`, `81B3AC98h`, `703A0788h`.

### Tests

Native tests:

- Same seed produces same first N values.
- Different seed produces different sequence for fixed test seeds.
- VM reset restores default seed.
- RNG state is independent per VM instance.

### Acceptance criteria

A native test can initialize RNG with the default seed `0x12345678` and receive the documented first sequence: `87985AA5h`, `155B24A3h`, `4820F4C4h`, `81B3AC98h`, `703A0788h`.

---

## 101. Phase 97 - Irvine32 Randomize, Random32, and RandomRange

### Goal

Implement deterministic virtual Irvine32 random routines.

### Accepted syntax

```asm
call Randomize
call Random32
call RandomRange
```

### Runtime behavior

- `Randomize` resets the VM RNG to the configured seed in deterministic mode. Tests must use the named default seed and must not use wall-clock time.
- `Random32` writes the next unsigned 32-bit random value to EAX.
- `RandomRange` reads upper bound `n` from EAX and returns a value in EAX where `0 <= EAX < n`.
- `RandomRange` with EAX = 0 produces `runtime-error invalid-random-range`, preserves EAX and modeled flags, and does not advance RNG state.
- Modeled flags are preserved.

### Tests

Source-run tests:

- `Random32` returns deterministic sequence.
- `Randomize` resets sequence according to documented seed policy.
- `RandomRange` returns `Random32() MOD n` below bound for bounds 1, 2, 10, and 100.
- `RandomRange` with zero bound reports diagnostic, preserves EAX and modeled flags, and does not advance RNG state.
- Flags preserve.

Acceptance program:

```asm
.code
main PROC
    call Randomize
    mov eax, 10
    call RandomRange
    call WriteDec
    exit
main ENDP
END main
```

Expected Program Console is the fixed deterministic value for the documented seed and PRNG.

Rendered Simulator Messages tests:

- Invalid random range renders as runtime error with source span on `RandomRange` target token in `call RandomRange`.

---

## 102. Phase 98 - WAITING_FOR_INPUT VM State

### Goal

Add backend VM state for routines that need input or wait-for-user behavior.

This phase is backend-only. It must not implement UI controls, actual text parsing for Read* routines, or WaitMsg dispatch. It only creates the VM state machine and source-run JSON representation needed by later routines.

### VM state requirements

Add terminal/paused state classification:

```text
RUNNING
COMPLETED
RUNTIME_ERROR
RESOURCE_LIMIT_ERROR
WAITING_FOR_INPUT
USER_STOPPED
```

When entering WAITING_FOR_INPUT, VM must store:

- request id;
- request kind;
- routine name;
- source instruction metadata;
- prompt/output already committed, if any;
- registers relevant to the request;
- whether cancellation is allowed.

### Timer and instruction count rules

- Entering wait counts as execution of the CALL/intrinsic instruction exactly once.
- Waiting time is not active execution time.
- No additional instruction count accumulates while waiting.
- Repeated run/continue while already waiting must not execute additional instructions.
- Stop remains valid while waiting.

### Tests

Native/worker tests:

- VM can enter WAITING_FOR_INPUT with request metadata.
- Repeated run/continue while waiting does not execute more instructions.
- Stop from wait produces USER_STOPPED.
- Reset clears pending request.
- Source-run JSON includes wait state distinctly from completion/error.

Rendered Simulator Messages tests:

- WAITING_FOR_INPUT state does not render as an error.
- Stop while waiting renders as user-stopped if that message exists.

### Acceptance criteria

A synthetic routine can place the VM into WAITING_FOR_INPUT, and the VM remains deterministic and inspectable until input is supplied, cancelled, stopped, or reset.

---

## 103. Phase 99 - Irvine32 WaitMsg

### Goal

Implement virtual Irvine32 `WaitMsg` using the backend wait state from **Phase 98 - WAITING_FOR_INPUT VM State**.

Phase-boundary note:

Phase 99 must not create a second wait-state mechanism. It must reuse the `WAITING_FOR_INPUT` state, request-id model, stop/reset behavior, and source-run JSON representation defined by Phase 98.

`WaitMsg` must not block the browser main thread or the worker event loop. It must not read host keyboard state directly.

### Runtime behavior

- `WaitMsg` appends the exact prompt text `Press any key to continue...` to Program Console.
- It then enters WAITING_FOR_INPUT with request kind `wait-key`.
- A structured key event or non-empty submitted text accepted by the worker protocol resumes execution after the call.
- Empty text without a key event is rejected as `input-empty` and keeps the VM waiting.
- On cancel, execution stops with `USER_STOPPED` unless a later input-routine phase defines a resumable cancel result.
- Entering wait counts as one executed instruction.
- Registers and modeled flags are preserved.

### Tests

- Program Console receives exact prompt text.
- VM enters WAITING_FOR_INPUT with request kind `wait-key`.
- Instruction count pauses while waiting.
- Stop cancels the wait.
- Resume after structured key event executes the next instruction.
- Resume after non-empty text executes the next instruction.
- Empty text without key event is rejected as `input-empty` and does not resume.
- Flags and registers preserve.

Rendered Simulator Messages tests:

- No diagnostic is produced for ordinary wait.
- Cancel/stop behavior renders separately from Program Console.

### Acceptance criteria

`call WaitMsg` produces the prompt, enters WAITING_FOR_INPUT, and resumes deterministically after matching input is submitted through the worker protocol.

---

## 104. Phase 100 - Input Submit/Cancel Worker Protocol

### Goal

Connect WAITING_FOR_INPUT VM state to the worker/UI protocol.

This phase must not implement ReadChar, ReadInt, ReadString, or other input routines unless explicitly included in later phases. It only transports input to a waiting VM.

### Protocol requirements

Add messages similar to:

```json
{ "type": "SUBMIT_INPUT", "requestId": 1, "text": "abc" }
{ "type": "CANCEL_INPUT", "requestId": 1 }
```

Worker responses must include updated VM state and either continued execution result or still-waiting/error state.

### Required behavior

1. UI can display the pending input request kind and prompt metadata.
2. Submit with matching request id resumes the VM exactly once.
3. Submit with stale or unknown request id is rejected.
4. Cancel with matching request id applies routine-specific cancellation behavior.
5. Stop remains available and supersedes pending input.
6. Reset clears pending input and rejects later stale submit/cancel messages.
7. Program Console input echo policy must be documented; do not echo input unless the routine requires it.
8. Simulator Messages remain separate from Program Console.

### Diagnostics and errors

- `stale-input-request`
- `invalid-input-request`
- `input-cancelled` if cancellation is diagnostic-producing
- `user-stopped` on stop

### Tests

Worker protocol tests:

- Submit matching request resumes VM.
- Duplicate submit rejected.
- Submit after cancel rejected.
- Submit after reset rejected.
- Reset while waiting invalidates the pending request and any later submit/cancel for that request id is stale.
- Empty text without a key event is rejected for `wait-key` as `input-empty`.
- Submit with wrong request kind rejected.
- Stale submit after reset rejected.
- Cancel matching request works.
- Cancel after completion rejected.
- Stop while waiting works.
- Program Console and Simulator Messages remain separate.

UI tests:

- Input control appears only while waiting.
- Input control hides after submit/cancel/stop/reset.
- Request id is not exposed as user-editable text.

Acceptance criteria

A synthetic wait request can be displayed in the browser, submitted through the worker protocol, and execution resumes deterministically.

## 105. Phase 101 - CALL/Frame/Console/Irvine Integration Smoke Harness

### Goal

Perform a validation-only harness proving CALL/frame/console/Irvine pieces work together.

### Required coverage

- Root program calls a user procedure using `USES`, `LOCAL`, `WriteChar`, `WriteString`, and `ret`.
- A second program uses `INVOKE` with DWORD arguments and matching `ret imm16` cleanup.
- `Crlf`, `WriteDec`, `WriteInt`, `WriteHex`, and `WriteBin` each appear in one source-run smoke program.
- `WaitMsg` enters waiting state and resumes from a structured key event without corrupting call-frame state.
- Call-depth diagnostics, console-output limit diagnostics, and input-cancel diagnostics render through Simulator Messages.

## 106. Phase 102 - Input Request Payload Normalization

### Goal

Define shared input payload normalization and request lifecycle behavior before implementing additional Irvine32 input routines.

### Work type

VM/worker protocol and source-run integration. No new Irvine routine semantics beyond shared request handling.

### Dependencies

- Existing Program Console stream separation.
- Existing `WAITING_FOR_INPUT` backend and worker protocol.
- Existing input submit/cancel/reset behavior.

### Implement

1. Define normalized input payload variants:
   - `line-text`: string payload for line-based routines.
   - `char-text`: non-empty string payload where the first byte/character is consumed.
   - `key-event`: structured key event with at least a byte/character field.
2. Define common request fields:
   - `requestId`;
   - `requestKind`;
   - source line/column/byte/span metadata;
   - routine name;
   - whether cancellation is allowed.
3. Define reset behavior:
   - reset invalidates pending request IDs;
   - later submit/cancel for an invalidated request produces `input-request-stale`.
4. Define double-submit behavior:
   - the first valid submit completes the request;
   - later submits for the same request ID produce `input-request-stale`.
5. Define empty-payload behavior:
   - line routines decide whether empty text is valid;
   - char routines reject empty text with `input-empty` unless a structured key event is supplied.
6. Ensure entering wait counts as one executed instruction and that waiting time does not increase instruction count.

### Explicit non-goals

- No buffered typeahead queue.
- No ReadKey or keyboard scan-code model.
- No browser focus/cursor UI polish.
- No host stdin.

### Diagnostics

- `input-request-stale`: stale or already-completed request ID.
- `invalid-input-payload`: missing payload, wrong request kind, malformed structured key event.
- `input-empty`: empty text submitted for a routine that requires at least one character.

Diagnostics point at the waiting instruction when the error is attached to a source program. Worker-only protocol diagnostics should include request ID and request kind in JSON.

### Required tests

Native C / core tests:

- Create request, submit once, complete successfully.
- Submit twice; second submit rejected as stale.
- Cancel pending request.
- Submit after cancel rejected as stale.
- Reset while waiting invalidates request.
- Submit after reset rejected as stale.
- Wrong request kind rejected.
- Missing request ID rejected.

Source-run JSON tests:

- Program enters wait with request kind and source metadata.
- Instruction count increases by one when wait begins.
- Instruction count does not increase while waiting.

Node rendered Simulator Messages tests:

- stale request diagnostic.
- invalid payload diagnostic.
- input empty diagnostic.

Manual browser smoke:

- Run a program that waits for input, cancel it, then verify Stop/Run still works.

## 107. Phase 103 - Irvine32 ReadChar

### Goal

Implement `ReadChar` as the smallest Irvine32 input routine.

### Work type

Virtual Irvine32 routine runtime plus input protocol integration.

### Dependencies

- Phase 102 - Input Request Payload Normalization, for submitted input payload shape, newline handling, cancellation behavior, and deterministic source-run/worker continuation semantics.
- Virtual Irvine32 routine registry.

Input-normalization ownership note:

Read* routine phases must not each invent their own submitted-input payload format. They must consume the normalized input request/response shape from **Phase 102 - Input Request Payload Normalization**. Individual Read* phases own parsing and register/flag results for their routine only.

- Program Console input UI.

### Accepted syntax

```asm
call ReadChar
```

### Runtime contract

- If no character is already supplied, enter `WAITING_FOR_INPUT` with request kind `read-char`.
- Accept a structured key event containing exactly one ASCII byte or text payload containing exactly one ASCII byte.
- Reject multi-byte text payloads with `input-extra-data`; do not consume a first byte and do not buffer the rest.
- On success, write the byte to `AL`.
- Preserve upper 24 bits of `EAX`.
- Preserve all modeled flags.
- No implicit echo.
- Cancellation preserves registers and flags.

### Rejected behavior

- `ReadChar` as an instruction mnemonic without `call` unless a prior phase explicitly supports Irvine pseudo-instruction syntax.
- Empty text payload.
- Multi-byte text payloads for `read-char`.
- Multi-byte Unicode semantics beyond the Program Console byte policy.
- Scan-code return behavior; this belongs to a later `ReadKey` phase.

### Diagnostics

- `input-empty` for empty text payload.
- `invalid-readchar-input` for malformed key event.
- `input-cancelled` for cancellation.

### Required tests

Core/runtime tests:

- Initial `EAX = 12345678h`, submitted `A` -> `EAX = 12345641h`.
- Upper EAX bits preserved.
- Flags preserve through successful read.
- Structured key event works.
- Empty text rejected.
- Multi-byte text rejected with `input-extra-data` and no hidden buffering.
- Cancel preserves EAX and flags.

Source-run JSON tests:

- wait response includes `read-char` request kind.
- completion result contains final register state.

Rendered Simulator Messages tests:

- empty input diagnostic.
- cancellation diagnostic.

Manual browser smoke:

```asm
INCLUDE Irvine32.inc
.code
main PROC
    call ReadChar
    call WriteChar
    exit
main ENDP
END main
```

Expected Program Console after submitting `Z`: `Z`.

## 108. Phase 104 - Irvine32 ReadInt

### Goal

Implement signed decimal integer input with deterministic overflow and flag behavior.

### Work type

Virtual Irvine32 routine runtime plus parser-independent numeric conversion.

### Dependencies

- Phase 102 - Input Request Payload Normalization, for submitted input payload shape, newline handling, cancellation behavior, and deterministic source-run/worker continuation semantics.
- Extended or existing flag helpers for `OF`.

### Accepted syntax

```asm
call ReadInt
```

### Input grammar

- Leading and trailing ASCII whitespace allowed.
- Optional leading `+` or `-` allowed.
- Decimal digits required after optional sign.
- Internal whitespace rejected.
- Prefixes/suffixes such as `0x`, `h`, `b`, `o` rejected.
- Range: `-2147483648..2147483647`.

### Runtime contract

- Enter `WAITING_FOR_INPUT` with request kind `read-line-int` if input is needed.
- On valid input, `EAX` receives the 32-bit two's-complement value and `OF` is cleared.
- On invalid or out-of-range input, `OF` is set and `EAX` is preserved.
- Other modeled flags are preserved.
- No implicit echo.
- Cancellation preserves EAX and flags and emits `input-cancelled`.

### Diagnostics

- `invalid-readint-input` for malformed text.
- `readint-out-of-range` for range overflow.
- `input-cancelled` for cancellation.

### Required tests

Core conversion tests:

- `0`, `+0`, `-0` -> EAX zero, OF clear.
- `2147483647` valid.
- `-2147483648` valid.
- `2147483648` out of range, EAX preserved, OF set.
- `-2147483649` out of range.
- `abc`, `12x`, `0x10`, `10h`, empty string invalid.
- Leading/trailing spaces accepted.

Source-run JSON tests:

- wait request kind `read-line-int`.
- invalid input resumes execution with OF set if program continues.
- cancellation stops/resumes according to input protocol contract.

Rendered Simulator Messages tests:

- malformed input diagnostic.
- out-of-range diagnostic.

Manual browser smoke:

```asm
INCLUDE Irvine32.inc
.code
main PROC
    call ReadInt
    call WriteInt
    exit
main ENDP
END main
```

Submit `-42`; Program Console shows `-42` after output.

## 109. Phase 105 - Irvine32 ReadDec

### Goal

Implement unsigned decimal integer input with deterministic carry-flag validation behavior.

### Work type

Virtual Irvine32 routine runtime.

### Dependencies

- Phase 102 - Input Request Payload Normalization, for submitted input payload shape, newline handling, cancellation behavior, and deterministic source-run/worker continuation semantics.
- Existing `CF` flag model.

### Accepted syntax

```asm
call ReadDec
```

### Input grammar

- Leading and trailing ASCII whitespace allowed.
- Decimal digits required.
- No sign accepted.
- Range: `0..4294967295`.

### Runtime contract

- Enter `WAITING_FOR_INPUT` with request kind `read-line-dec` if input is needed.
- On valid input, `EAX` receives the value and `CF` is cleared.
- On invalid or out-of-range input, `CF` is set and `EAX` is preserved.
- Other modeled flags are preserved.
- No implicit echo.

### Diagnostics

- `invalid-readdec-input`.
- `readdec-out-of-range`.
- `input-cancelled`.

### Required tests

Core conversion tests:

- `0` valid.
- `4294967295` valid.
- `4294967296` out of range.
- `+1`, `-1`, `0x10`, `10h`, empty string invalid.
- Leading/trailing spaces accepted.

Source-run JSON tests:

- request kind `read-line-dec`.
- invalid input sets CF and preserves EAX.

Rendered Simulator Messages tests:

- invalid text.
- out-of-range text.

Manual browser smoke:

```asm
INCLUDE Irvine32.inc
.code
main PROC
    call ReadDec
    call WriteDec
    exit
main ENDP
END main
```

Submit `123`; Program Console shows `123` after output.

## 110. Phase 106 - Irvine32 ReadHex

### Goal

Implement unsigned hexadecimal input with explicit accepted forms and failure behavior.

### Work type

Virtual Irvine32 routine runtime.

### Dependencies

- Phase 102 - Input Request Payload Normalization, for submitted input payload shape, newline handling, cancellation behavior, and deterministic source-run/worker continuation semantics.
- Existing `CF` flag model.

### Accepted syntax

```asm
call ReadHex
```

### Input grammar

Accepted forms:

```text
FF
0FFh
0ffH
0xFF
0Xff
```

Rules:

- Leading/trailing ASCII whitespace allowed.
- No sign accepted.
- At least one hexadecimal digit required.
- Range: `0..FFFFFFFFh`.
- Underscores, spaces inside digits, and binary/octal forms rejected.

### Runtime contract

- Enter `WAITING_FOR_INPUT` with request kind `read-line-hex` if input is needed.
- On valid input, `EAX` receives the value and `CF` is cleared.
- On invalid or out-of-range input, `CF` is set and `EAX` is preserved.
- Other modeled flags are preserved.
- No implicit echo.

### Diagnostics

- `invalid-readhex-input`.
- `readhex-out-of-range`.
- `input-cancelled`.

### Required tests

Core conversion tests:

- `0`, `F`, `FF`, `0FFh`, `0xFF`, `FFFFFFFFh` valid.
- `100000000h` out of range.
- `-1`, `+1`, `0x`, `h`, `GG`, empty string invalid.
- mixed-case hex accepted.

Source-run JSON tests:

- request kind `read-line-hex`.
- invalid input sets CF and preserves EAX.

Rendered Simulator Messages tests:

- malformed input.
- out of range.

Manual browser smoke:

```asm
INCLUDE Irvine32.inc
.code
main PROC
    call ReadHex
    call WriteHex
    exit
main ENDP
END main
```

Submit `2A`; Program Console shows `0000002A` after output.

## 111. Phase 107 - ReadString Buffer Preflight and Input Request

### Goal

Add validation and wait-request setup for `ReadString`, without yet writing submitted input to memory.

### Work type

Virtual Irvine32 routine runtime, memory validation, input protocol integration.

### Dependencies

- Phase 102 - Input Request Payload Normalization, for submitted input payload shape, newline handling, cancellation behavior, and deterministic source-run/worker continuation semantics.
- Checked VM memory writes.
- Program Console input UI.

### Accepted syntax

```asm
call ReadString
```

### Runtime preflight contract

Input registers:

```text
EDX = destination buffer address
ECX = maximum number of non-null characters
```

Preflight validation:

- `ECX = 0` is invalid.
- The full destination range `[EDX, EDX + ECX]` must be writable before waiting.
- The extra byte is for the null terminator.
- Address overflow in `EDX + ECX` is invalid.
- `.CONST` and other read-only regions fail through central write validation.

On successful preflight:

- enter `WAITING_FOR_INPUT` with request kind `read-string`;
- preserve all registers until input completion;
- include buffer address and maximum length in request JSON.

### Explicit non-goals

- No writing submitted input yet.
- No truncation behavior yet.
- No implicit echo.

### Diagnostics

- `invalid-readstring-length` for `ECX = 0`.
- `invalid-readstring-buffer` for address overflow, unmapped range, read-only range, or object-bound failure.

Diagnostic span points at the `ReadString` target token unless a lower-level memory diagnostic supplies a more precise source span.

### Required tests

Core/runtime tests:

- valid writable buffer enters wait.
- `ECX = 0` rejected before wait.
- `.CONST` destination rejected before wait.
- unmapped destination rejected before wait.
- `EDX + ECX` overflow rejected.
- partial-overlap writable failure rejected.

Source-run JSON tests:

- request kind `read-string`.
- request JSON includes address and max non-null count.

Rendered Simulator Messages tests:

- invalid length.
- invalid buffer.

Manual browser smoke:

Run a program that calls `ReadString` with a valid writable buffer and verify the UI enters input-wait state.

## 112. Phase 108 - ReadString Checked Write and Completion

### Goal

Complete `ReadString` by writing submitted input into simulated memory safely and returning the character count.

### Work type

Virtual Irvine32 routine runtime, checked memory writes, source-run/worker integration.

### Dependencies

- Phase 107 - ReadString Buffer Preflight and Input Request, for validating the destination pointer, maximum character count, null-terminator space, request metadata, and pre-input no-mutation behavior.
- Program Console byte policy.
- Input submit/cancel protocol.

Phase-boundary note:

Phase 108 owns committing accepted ReadString input bytes to memory after input is supplied. It must not move preflight validation out of Phase 107, and it must not change the Phase 107 request shape except through an explicit documented compatibility update.

### Runtime completion contract

- Submitted text is encoded by the Program Console byte policy.
- If encoded input length `n <= ECX`, write `n` bytes followed by `00h`.
- If `n > ECX`, write the first `ECX` bytes followed by `00h`, set `EAX = ECX`, and emit warning `input-truncated`.
- On success without truncation, `EAX = n`.
- All writes go through checked memory helpers.
- Validation-first rule: if the completion write would fail, no memory bytes, registers, or Program Console output are mutated.
- Cancellation preserves registers and memory and emits `input-cancelled`.
- No implicit echo.

### Diagnostics

- `input-truncated` warning, non-fatal.
- `readstring-write-failed` runtime error.
- `input-cancelled`.

### Required tests

Core/runtime tests:

- write `ABC` to `BYTE 4 DUP(?)` with `ECX=3`: bytes `41 42 43 00`, `EAX=3`.
- empty submitted line writes only `00h`, `EAX=0`.
- overlong input truncates to `ECX`, null terminates, emits warning.
- `.CONST` or invalidated buffer fails with no partial writes.
- cancellation preserves buffer and registers.

Source-run JSON tests:

- completion returns final register state and memory changes.
- truncation warning included.
- failed completion has no memory-change rows.

Rendered Simulator Messages tests:

- truncation warning.
- write failure.
- cancellation.

Manual browser smoke:

```asm
INCLUDE Irvine32.inc
.data
buf BYTE 6 DUP(?)
.code
main PROC
    mov edx, OFFSET buf
    mov ecx, 5
    call ReadString
    mov edx, OFFSET buf
    call WriteString
    exit
main ENDP
END main
```

Submit `Hi`; Program Console shows `Hi` after `WriteString`.

## 113. Phase 109 - PF and AF Flag Storage, Display, and Serialization

### Goal

Add `PF` and `AF` as modeled EFLAGS bits without changing instruction semantics yet.

### Work type

Core CPU/flags model, source-run JSON, UI formatter, DumpRegs/debug metadata.

### Dependencies

- Existing EFLAGS storage.
- Existing flag display path.

### Implement

1. Add flag enum values and helpers for:
   - `PF` bit 2;
   - `AF` bit 4.
2. Ensure raw EFLAGS read/write preserves these bits.
3. Add display names and JSON serialization.
4. Add default initialization policy: both clear on VM reset unless raw EFLAGS is explicitly set.
5. Update Program Console DumpRegs metadata if DumpRegs exists; otherwise add tests to the pending DumpRegs contract.

### Explicit non-goals

- Do not change arithmetic/logical helper semantics yet.
- Do not add DF yet.
- Do not implement string instructions.

### Required tests

Core tests:

- set/clear/read PF.
- set/clear/read AF.
- raw EFLAGS includes correct bit positions.
- existing CF/ZF/SF/OF behavior unchanged.

Source-run JSON tests:

- final flags include PF and AF.

Rendered UI/Node tests:

- final register/flag text includes PF/AF where flag display is active.

## 114. Phase 110 - PF/AF Integration Split

### Goal

Split `PF` and `AF` integration into focused, testable subphases.

This is intentionally not one implementation milestone. Do not implement all `PF`/`AF` behavior in one assistant session.

Phase 110 is a parent roadmap section only. Implementation must proceed through:

```text
Phase 110A - PF/AF for arithmetic and compare helpers
Phase 110B - PF/AF for logical and TEST helpers
Phase 110C - PF/AF policy for shifts and rotates
Phase 110D - PF/AF preservation and undefined-policy checks for multiply and divide
Phase 110E - PF/AF preservation regression for flag-preserving instructions
Phase 110F - PF/AF display integration for debugger, source-run, UI, and Irvine32 DumpRegs
```

If a future implementation session refers only to “Phase 110” without a subphase letter, the intended next target is Phase 110A unless the user explicitly selects a different 110 subphase. Do not implement Phase 110A through Phase 110F in one session.

Before Phase 110F, `PF` and `AF` may be stored internally and validated by native/helper/source-run tests without being exposed in every final UI, debugger, or Irvine32 display. Phase 110F owns display integration unless an earlier phase explicitly chooses to expose the flags.

### Dependencies

- Phase 109 `PF`/`AF` storage and CPU helper scaffolding.
- Existing instruction behavior through the phase immediately before Phase 110.
- Existing source-run JSON path.
- Existing rendered Simulator Messages test harness.
- Existing debugger and Irvine32 display phases, where Phase 110F applies.

### Global PF/AF definitions

`PF` is the parity flag:

```text
PF = 1 when the low byte of the result has an even number of 1 bits.
PF = 0 when the low byte of the result has an odd number of 1 bits.
```

`AF` is the auxiliary carry flag:

```text
For addition-family operations:
  AF = 1 when there is a carry from bit 3 to bit 4.

For subtraction-family operations:
  AF = 1 when there is a borrow across bit 4.
```

Implementation helpers may use:

```text
AF = ((lhs ^ rhs ^ result) & 10h) != 0
```

only if tests prove the helper is correct for both addition-family and subtraction-family operations.

### Global no-partial-mutation rule

Every Phase 110 subphase must preserve the existing validation-first behavior:

- failed assembly prevents execution;
- failed runtime validation stops before committing the instruction;
- failed memory reads do not mutate registers, flags, memory, console state, or memory-change rows;
- failed memory writes roll back any tentative register/flag changes;
- divide-error paths do not mutate newly modeled `PF`/`AF`;
- strict validation errors stop before mutation;
- warning-only paths may execute only if the relevant validation mode permits execution.

### Global testing rule

Every Phase 110 subphase must include:

- native helper tests for `PF` and/or `AF` formulas introduced by that subphase;
- executor tests for the affected instruction family;
- source-run JSON tests for representative accepted programs;
- regression tests proving existing `CF`, `ZF`, `SF`, and `OF` behavior remains unchanged;
- no-partial-mutation tests for failing memory or validation paths where the affected instructions can access memory;
- rendered Simulator Messages tests for any new warning or runtime-error diagnostic;
- static/documentation checks if supported-syntax or user-facing flag documentation changes.

A subphase that adds no new diagnostic path does not need new rendered Simulator Messages diagnostics, but it must not remove existing rendered diagnostic coverage.

### Phase 110A - PF/AF for Arithmetic and Compare Helpers

#### Goal

Add `PF` and `AF` updates to existing arithmetic and compare instructions.

#### Scope

This phase covers exactly:

```text
add
adc
sub
sbb
cmp
neg
inc
dec
```

Do not implement logical/test `PF`/`AF`, shifts, rotates, multiply/divide behavior, UI display changes, Irvine32 `DumpRegs` changes, `DF`, string instructions, or any new instruction syntax in this phase.

#### Required behavior

##### ADD and ADC

For `add` and `adc`:

- compute `PF` from the low byte of the arithmetic result;
- compute `AF` from carry out of bit 3;
- preserve already implemented `CF`, `ZF`, `SF`, and `OF` behavior;
- support every already-implemented register and memory operand form for these instructions;
- preserve existing immediate range behavior;
- preserve existing memory diagnostics and rollback behavior.

`ADC` must include the incoming `CF` in both the result and `AF` calculation.

##### SUB, SBB, and CMP

For `sub`, `sbb`, and `cmp`:

- compute `PF` from the low byte of the subtraction result;
- compute `AF` from borrow across bit 4;
- preserve already implemented `CF`, `ZF`, `SF`, and `OF` behavior;
- preserve existing immediate range behavior;
- preserve existing memory diagnostics and rollback behavior.

`SBB` must include the incoming `CF` borrow in both the result and `AF` calculation.

`CMP` updates flags only. It must not mutate the destination and must not create memory-change rows.

##### NEG

For `neg`:

- treat the operation as `0 - destination`;
- compute `PF` from the low byte of the result;
- compute `AF` from the borrow across bit 4 in `0 - destination`;
- preserve existing `CF`, `ZF`, `SF`, and `OF` behavior;
- preserve existing memory diagnostics and rollback behavior.

##### INC and DEC

For `inc`:

- compute `PF` from the low byte of `destination + 1`;
- compute `AF` from carry out of bit 3 in `destination + 1`;
- preserve `CF` exactly;
- preserve existing `ZF`, `SF`, and `OF` behavior.

For `dec`:

- compute `PF` from the low byte of `destination - 1`;
- compute `AF` from borrow across bit 4 in `destination - 1`;
- preserve `CF` exactly;
- preserve existing `ZF`, `SF`, and `OF` behavior.

#### Required tests

##### Native helper tests

Add helper-level tests for:

```text
PF low-byte even parity:
  00h -> PF=1
  01h -> PF=0
  03h -> PF=1
  FFh -> PF=1

AF addition:
  0Fh + 01h -> AF=1
  10h + 01h -> AF=0
  FFh + 01h -> AF=1

AF subtraction:
  10h - 01h -> AF=1
  11h - 01h -> AF=0
  00h - 01h -> AF=1
```

##### Executor/source-run tests

At minimum, include programs or executor tests for:

```asm
.code
main PROC
    mov eax, 0Fh
    add eax, 1
main ENDP
END main
```

Expected:

```text
EAX = 00000010h
AF = 1
PF from low byte 10h
```

```asm
.code
main PROC
    mov eax, 10h
    sub eax, 1
main ENDP
END main
```

Expected:

```text
EAX = 0000000Fh
AF = 1
PF from low byte 0Fh
```

```asm
.code
main PROC
    stc
    mov eax, 0Eh
    adc eax, 1
main ENDP
END main
```

Expected:

```text
EAX = 00000010h
AF = 1
CF/ZF/SF/OF follow existing ADC behavior
```

```asm
.code
main PROC
    stc
    mov eax, 11h
    sbb eax, 1
main ENDP
END main
```

Expected:

```text
EAX = 0000000Fh
AF = 1
CF/ZF/SF/OF follow existing SBB behavior
```

```asm
.code
main PROC
    mov eax, 10h
    cmp eax, 1
main ENDP
END main
```

Expected:

```text
EAX remains 00000010h
AF = 1
PF from comparison result 0Fh
No memory changes.
```

```asm
.code
main PROC
    mov eax, 0
    neg eax
main ENDP
END main
```

Expected:

```text
EAX = 00000000h
PF = 1
AF = 0
Existing NEG flags preserved.
```

```asm
.code
main PROC
    stc
    mov eax, 0Fh
    inc eax
    dec eax
main ENDP
END main
```

Expected after final `dec`:

```text
EAX = 0000000Fh
CF = 1
AF reflects the DEC operation
PF reflects low byte 0Fh
```

##### Memory rollback tests

For each affected memory-capable instruction family, include at least one failing memory write/read path proving `PF` and `AF` do not partially mutate.

Examples:

```asm
.CONST
limit DWORD 0Fh

.code
main PROC
    add limit, 1
main ENDP
END main
```

Expected:

```text
assembly or runtime const-write diagnostic according to existing .CONST path
No PF/AF mutation visible from the failed ADD.
No memory changes.
```

```asm
.code
main PROC
    mov eax, 0
    add DWORD PTR [eax], 1
main ENDP
END main
```

Expected:

```text
runtime invalid memory diagnostic
No PF/AF mutation visible from the failed ADD.
No memory changes.
```

#### Acceptance criteria

- `PF` and `AF` are stored and updated for all Phase 110A instructions.
- Existing `CF`, `ZF`, `SF`, and `OF` tests still pass.
- Existing instruction operand support is unchanged.
- Existing diagnostics and rendered Simulator Messages remain stable except for intentional flag-output changes.
- No logical/test, shift/rotate, multiply/divide, UI, debugger, Irvine32, or `DF` work is implemented in this phase.

### Phase 110B - PF/AF for Logical and TEST Helpers

#### Goal

Add `PF` and deterministic `AF` behavior to logical and `TEST` instructions.

#### Scope

This phase covers exactly:

```text
and
or
xor
test
```

Do not implement arithmetic `PF`/`AF`, shifts, rotates, multiply/divide behavior, UI display changes, Irvine32 `DumpRegs` changes, `DF`, string instructions, or any new instruction syntax in this phase.

#### Required behavior

For `and`, `or`, `xor`, and `test`:

- compute the logical result using existing operand and width rules;
- compute `PF` from the low byte of the logical result;
- preserve existing `ZF` and `SF` result behavior;
- preserve existing `CF` and `OF` clearing behavior;
- use this deterministic v1 simulator policy for `AF`:

```text
AF is cleared to 0 for AND, OR, XOR, and TEST.
```

This `AF` rule is a simulator contract for deterministic educational output. It must be documented as deterministic simulator behavior, not as a portable guarantee for real x86 programs.

Do not emit undefined-flag warnings for `AF` on `AND`, `OR`, `XOR`, or `TEST`. The v1 simulator deliberately clears `AF` to `0` for these logical/test instructions as deterministic educational behavior.

`TEST` must remain non-mutating:

- it updates flags;
- it does not write the logical result back to the destination;
- it does not create memory-change rows.

`AND`, `OR`, and `XOR` remain destination-mutating according to their existing behavior.

#### Required tests

##### Source-run tests

```asm
.code
main PROC
    mov eax, 03h
    and eax, 01h
main ENDP
END main
```

Expected:

```text
EAX = 00000001h
PF = 0
AF = 0
CF = 0
OF = 0
ZF = 0
SF = 0
```

```asm
.code
main PROC
    mov eax, 03h
    xor eax, 03h
main ENDP
END main
```

Expected:

```text
EAX = 00000000h
PF = 1
AF = 0
CF = 0
OF = 0
ZF = 1
SF = 0
```

```asm
.code
main PROC
    mov eax, 03h
    test eax, 03h
main ENDP
END main
```

Expected:

```text
EAX remains 00000003h
PF = 1
AF = 0
CF = 0
OF = 0
ZF = 0
SF = 0
No memory changes.
```

##### Memory tests

Include register/memory, memory/register, and memory/immediate tests for `AND`, `OR`, and `XOR` using already-supported memory forms.

Include a `TEST` memory-source test proving no memory mutation.

Include one invalid memory path proving `PF` and `AF` do not partially mutate.

#### Acceptance criteria

- `PF` is result-based for all Phase 110B instructions.
- `AF` is cleared for all Phase 110B instructions under the documented simulator policy.
- `TEST` remains read-only.
- Existing `CF`, `ZF`, `SF`, and `OF` behavior remains unchanged.
- Existing ambiguous-width diagnostics remain unchanged.
- No shift/rotate, arithmetic, multiply/divide, UI, debugger, Irvine32, or `DF` work is implemented in this phase.

### Phase 110C - PF/AF Policy for Shifts and Rotates

#### Goal

Integrate `PF` and `AF` with the existing shift and rotate instruction families without changing their already-defined count, `CF`, `OF`, `ZF`, or `SF` contracts.

#### Scope

This phase covers exactly:

```text
shl
sal
shr
sar
rol
ror
```

Do not implement new shift or rotate syntax in this phase. Do not reintroduce `unsupported-shift-count` for MASM-valid shift counts. Do not implement arithmetic/logical/test `PF`/`AF`, multiply/divide behavior, UI display changes, Irvine32 `DumpRegs` changes, `DF`, string instructions, or any new instruction family.

#### Dependencies

- Completed shift phases for `SHL`/`SAL`, `SHR`, and `SAR`.
- Completed rotate phases for `ROL` and `ROR`.
- Completed shift-count compatibility policy that accepts MASM-valid counts and handles undefined modeled flags through the documented warning/strict mechanism.
- Phase 109 `PF`/`AF` storage.
- Phase 110A and Phase 110B may be completed first, but this phase must not depend on their implementation details except shared helper functions.

#### Required behavior for SHL/SAL/SHR/SAR

For:

```text
shl
sal
shr
sar
```

the phase must apply the following rules.

##### Effective count zero

If the effective count is zero:

- destination is unchanged;
- `PF` is preserved;
- `AF` is preserved;
- all other modeled flags follow the existing shift-count-zero contract.

##### Effective count nonzero

If the effective count is nonzero and the instruction executes:

- destination is updated according to the existing shift semantics;
- `PF` is computed from the low byte of the shifted result;
- `AF` is architecturally undefined and must follow the existing shift undefined-flag simulator policy.

The required v1 simulator policy for `AF` on nonzero shifts is:

```text
Default mode:
  Preserve previous AF.
  Emit undefined-shift-flag warning only if the existing shift policy emits a warning for undefined modeled flags for this instruction/count case.

Strict undefined-shift mode:
  If the existing shift policy treats the instruction/count as a strict undefined-flag error, stop before mutation and preserve AF.
```

Phase 110C must not expand the `undefined-shift-flag` warning trigger set merely because `AF` has become modeled. The warning/strict trigger set remains the one defined by the shift-count compatibility phase. `AF` follows that policy only when the existing shift policy already warns or errors for the instruction/count case.

`PF` is not undefined for nonzero shifts; it is result-based.

#### Required behavior for ROL/ROR

For:

```text
rol
ror
```

the phase must apply the following rules.

##### Effective count zero

If the effective count is zero:

- destination is unchanged;
- `PF` is preserved;
- `AF` is preserved;
- all other modeled flags are preserved.

##### Effective count nonzero

If the effective count is nonzero and the instruction executes:

- destination is rotated according to existing rotate semantics;
- `PF` is preserved;
- `AF` is preserved;
- `SF` and `ZF` remain preserved according to the rotate contract;
- `CF` follows the rotate contract;
- `OF` follows the rotate contract, including undefined multi-bit rotate policy if the rotate phase has one.

This phase must not compute `PF` from rotate results. `ROL` and `ROR` do not use result-based `PF` in the simulator.

#### Required tests

##### Shift tests

```asm
.code
main PROC
    mov eax, 1
    shl eax, 0
main ENDP
END main
```

Expected:

```text
EAX unchanged
PF preserved
AF preserved
No new warning from Phase 110C
```

```asm
.code
main PROC
    mov eax, 1
    shl eax, 1
main ENDP
END main
```

Expected:

```text
EAX = 00000002h
PF = 0
AF follows the shift undefined-flag policy
Existing CF/OF/ZF/SF behavior unchanged
```

```asm
.code
main PROC
    mov eax, 3
    shl eax, 1
main ENDP
END main
```

Expected:

```text
EAX = 00000006h
PF = 1
AF follows the shift undefined-flag policy
```

```asm
.code
main PROC
    mov al, 1
    shl al, 8
main ENDP
END main
```

Expected in default compatibility mode:

```text
Program executes according to the existing MASM-compatible shift-count policy.
PF is computed from the result if the instruction executes with nonzero effective count.
AF follows the existing undefined-shift-flag policy.
No unsupported-shift-count assembly error.
```

Expected in strict undefined-shift mode, if that mode is active:

```text
runtime-error undefined-shift-flag according to existing strict policy
No destination mutation
No PF/AF mutation
```

Add equivalent representative tests for `shr` and `sar`.

`SAL` must be tested as an alias of `SHL`.

##### Rotate tests

```asm
.code
main PROC
    mov eax, 80000001h
    rol eax, 1
main ENDP
END main
```

Expected:

```text
EAX rotated according to existing ROL behavior
PF preserved
AF preserved
CF/OF follow existing ROL behavior
ZF/SF preserved
```

```asm
.code
main PROC
    mov eax, 80000001h
    ror eax, 4
main ENDP
END main
```

Expected:

```text
EAX rotated according to existing ROR behavior
PF preserved
AF preserved
CF follows existing ROR behavior
OF follows existing multi-bit rotate policy
ZF/SF preserved
```

#### Acceptance criteria

- Shifts update `PF` from result when effective count is nonzero.
- Shifts preserve `PF` and `AF` when effective count is zero.
- Shifts preserve or diagnose `AF` exactly according to the existing undefined-shift policy.
- Rotates preserve `PF` and `AF`.
- Existing shift/rotate `CF`, `OF`, `ZF`, and `SF` behavior remains unchanged.
- MASM-valid shift counts remain accepted in default mode.
- No new shift/rotate syntax is added.
- No arithmetic/logical/test, multiply/divide, UI, debugger, Irvine32, or `DF` work is implemented in this phase.

### Phase 110D - PF/AF Preservation and Undefined-Policy Checks for Multiply and Divide

#### Goal

Define and test `PF` and `AF` behavior for multiply and divide instruction families without changing their implicit-register semantics or error behavior.

#### Scope

This phase covers the already-implemented forms, and only the already-implemented forms, of:

```text
mul
imul
div
idiv
```

If the roadmap has split one-operand, two-operand, or three-operand `IMUL` into separate earlier phases, this phase must cover only the forms that are implemented by the time Phase 110D begins.

Do not add a missing multiply/divide form to make this phase’s tests broader.

Do not implement new multiply/divide forms in this phase. Do not implement arithmetic/logical/test `PF`/`AF`, shifts, rotates, UI display changes, Irvine32 `DumpRegs` changes, `DF`, string instructions, or any new instruction family.

#### Required behavior

For all covered multiply/divide forms:

```text
PF is preserved.
AF is preserved.
```

This is the v1 deterministic simulator policy unless the earlier multiply/divide phase explicitly selected a different undefined-flag policy. If an earlier phase explicitly chose a different policy, Phase 110D must follow that phase and document the reason.

For divide-error paths:

- `PF` is preserved;
- `AF` is preserved;
- existing register/memory/flag rollback behavior is preserved;
- no partial quotient/remainder mutation occurs;
- existing runtime diagnostic and rendered Simulator Messages behavior remains unchanged.

For multiply overflow or high-half-result cases:

- existing `CF` and `OF` behavior remains unchanged;
- `PF` and `AF` remain preserved unless the multiply phase explicitly documented otherwise.

#### Required tests

```asm
.code
main PROC
    mov eax, 2
    mov ebx, 3
    mul ebx
main ENDP
END main
```

Expected:

```text
MUL result follows existing MUL contract.
PF preserved.
AF preserved.
Existing CF/OF behavior unchanged.
```

```asm
.code
main PROC
    mov eax, 6
    mov ebx, 3
    div ebx
main ENDP
END main
```

Expected:

```text
DIV result follows existing DIV contract.
PF preserved.
AF preserved.
Existing divide behavior unchanged.
```

Divide-error test:

```asm
.code
main PROC
    mov eax, 1
    mov ebx, 0
    div ebx
main ENDP
END main
```

Expected:

```text
runtime divide diagnostic according to existing DIV phase
No quotient/remainder mutation
PF preserved
AF preserved
```

Add equivalent tests for every implemented `IMUL` and `IDIV` form.

#### Acceptance criteria

- `PF` and `AF` preservation is tested for all implemented multiply/divide forms.
- Divide-error paths preserve `PF` and `AF`.
- Existing multiply/divide implicit register behavior remains unchanged.
- Existing multiply/divide `CF`/`OF` behavior remains unchanged.
- No new multiply/divide syntax is added.
- No arithmetic/logical/test, shift/rotate, UI, debugger, Irvine32, or `DF` work is implemented in this phase.

### Phase 110E - PF/AF Preservation Regression for Flag-Preserving Instructions

#### Goal

Add regression coverage proving instructions that should not affect `PF` or `AF` preserve them.

This phase is mostly tests. It should require little or no executor logic if earlier phases implemented flag preservation correctly.

#### Scope

This phase covers already-implemented flag-preserving instructions and instruction families, including:

```text
mov
movsx
movzx
cbw
cwde
cwd
cdq
lea
xchg
nop
clc
stc
cmc
jmp and conditional jumps
loop-family instructions
push
pop
call
ret
leave
ret imm16
exit
Irvine32 routines unless a specific routine contract says otherwise
```

Only include instructions that are implemented by the time Phase 110E begins. Do not implement missing instructions just to test them.

Do not implement new instruction syntax, UI display changes, Irvine32 `DumpRegs` changes, `DF`, string instructions, or any new runtime feature.

#### Required behavior

The covered instructions must preserve `PF` and `AF` unless their own phase explicitly documented flag mutation.

Special rules:

- `CLC`, `STC`, and `CMC` mutate `CF` only. They must preserve `PF` and `AF`.
- `XCHG` preserves all modeled flags, including `PF` and `AF`.
- `NOP` preserves all modeled flags.
- `MOV`, `MOVSX`, `MOVZX`, `CBW`, `CWDE`, `CWD`, and `CDQ` preserve all modeled flags.
- `LEA` preserves all modeled flags.
- Branches and loop-family instructions preserve `PF` and `AF`; they may read flags or counters according to their own branch condition, but they must not mutate `PF` or `AF`.
- Stack/procedure instructions preserve `PF` and `AF` unless their own phase explicitly says otherwise.
- `exit` preserves `PF` and `AF`.
- Irvine32 routines preserve `PF` and `AF` unless the routine contract explicitly documents flag mutation.

#### Required tests

Use helper setup to force known initial `PF` and `AF` values, then execute one instruction and verify they are unchanged.

At minimum:

```asm
.code
main PROC
    mov eax, 0
    mov ebx, 123
main ENDP
END main
```

Expected:

```text
MOV preserves PF/AF.
```

```asm
.code
main PROC
    mov eax, 0
    xchg eax, ebx
main ENDP
END main
```

Expected:

```text
XCHG preserves PF/AF.
```

```asm
.code
main PROC
    stc
    clc
    stc
    cmc
main ENDP
END main
```

Expected:

```text
CLC/STC/CMC mutate CF according to existing behavior.
PF preserved.
AF preserved.
```

```asm
INCLUDE Irvine32.inc

.code
main PROC
    mov eax, 123
    exit
    mov eax, 999
main ENDP
END main
```

Expected:

```text
exit terminates according to existing behavior.
PF preserved.
AF preserved.
Instruction after exit does not execute.
```

Add tests for each branch/control-flow family that exists by this point. Branch tests must cover both taken and not-taken paths where practical.

Add tests for each stack/procedure family that exists by this point.

Add tests for each Irvine32 routine implemented by this point. If a routine mutates flags by contract, test that exact contract instead of preservation.

#### Acceptance criteria

- Covered flag-preserving instructions preserve `PF` and `AF`.
- Existing control-flow behavior remains unchanged.
- Existing stack/procedure behavior remains unchanged.
- Existing Irvine32 behavior remains unchanged.
- No missing instruction is implemented merely to satisfy this phase.
- No UI, debugger, DumpRegs, or `DF` work is implemented in this phase.

### Phase 110F - PF/AF Display Integration for Debugger, Source-Run, UI, and Irvine32 DumpRegs

#### Goal

Expose already-modeled `PF` and `AF` in user-visible displays and protocol payloads.

This phase is display and integration work only. It must not change instruction semantics.

#### Scope

This phase covers:

```text
final register/flag output
source-run JSON flag payloads
worker protocol flag payloads
debugger flags table
last-step flag delta display
Irvine32 DumpRegs output, if DumpRegs exists by this point
supported-syntax/user documentation updates
```

Do not implement new instruction behavior, new flag semantics, `DF`, string instructions, new Irvine32 routines, or new diagnostics unrelated to display/protocol validation.

If `DumpRegs` is not implemented when Phase 110F begins, do not implement `DumpRegs` in this phase. Instead, update the future `DumpRegs` phase or supported-syntax documentation to state that `DumpRegs` must include `PF` and `AF` when implemented.

#### Required behavior

After this phase:

- final flag display includes `PF` and `AF`;
- source-run JSON includes `PF` and `AF` in the same flag model used for `CF`, `ZF`, `SF`, and `OF`;
- worker protocol payloads remain structured-clone-safe and JSON-compatible;
- debugger flags table includes `PF` and `AF`;
- last-step deltas show `PF` and `AF` changes when they change;
- unchanged `PF` and `AF` may be omitted from compact last-step deltas if that matches existing delta UI policy;
- `DumpRegs`, if implemented, includes `PF` and `AF` in Program Console output;
- Program Console and Simulator Messages remain separate streams;
- tests and documentation stop describing the modeled flag set as only `CF`, `ZF`, `SF`, and `OF` after this phase.

#### Required tests

##### Source-run JSON tests

Run a program known to set `PF` and `AF`:

```asm
.code
main PROC
    mov eax, 0Fh
    add eax, 1
main ENDP
END main
```

Expected JSON includes:

```text
PF present
AF present
PF value matches low-byte parity
AF = 1
```

##### Rendered final-output tests

Expected final display includes `PF` and `AF` in the same section or format as other modeled flags.

##### Debugger tests

Step through:

```asm
.code
main PROC
    mov eax, 0Fh
    add eax, 1
    xor eax, eax
main ENDP
END main
```

Expected:

```text
Step after ADD shows PF/AF values or deltas according to debugger policy.
Step after XOR shows PF and AF according to Phase 110B behavior.
```

##### DumpRegs tests

If `DumpRegs` exists:

```asm
INCLUDE Irvine32.inc

.code
main PROC
    mov eax, 0Fh
    add eax, 1
    call DumpRegs
    exit
main ENDP
END main
```

Expected Program Console includes:

```text
PF=<value>
AF=<value>
```

The exact ordering and spacing must be frozen by the DumpRegs phase or this phase.

The output must appear in Program Console, not Simulator Messages.

##### Documentation/static tests

Add static checks so docs and supported-syntax references do not continue to say only `CF`, `ZF`, `SF`, and `OF` are modeled after Phase 110F.

#### Acceptance criteria

- `PF` and `AF` appear in final flags output.
- `PF` and `AF` appear in debugger flags output.
- `PF` and `AF` appear in last-step deltas when changed.
- `DumpRegs` includes `PF` and `AF` if DumpRegs is already implemented.
- Program Console and Simulator Messages remain separated.
- No instruction semantics change in this phase.
- No `DF` or string-instruction work is implemented in this phase.

## 115. Phase 111 - DF Flag and CLD/STD

### Goal

Add the direction flag and the `cld` / `std` instructions.

### Work type

Core flags, parser, IR, executor.

### Dependencies

- Existing CPU/EFLAGS helper patterns from the initial flag model.
- Phase 109 - PF and AF Flag Storage, Display, and Serialization, as the most recent extended-flag storage precedent.
- Existing debugger/source-run flag serialization conventions, if they already expose modeled flags.

DF ownership note:

Phase 111 adds `DF` storage plus `CLD` and `STD` behavior. It must not implement string instructions, REP prefixes, MOVS/STOS/LODS/CMPS/SCAS behavior, or debugger/UI display changes unless this phase explicitly says so. String instruction phases consume `DF`; they do not own initial `DF` storage.

### Accepted syntax

```asm
cld
std
```

### Runtime semantics

- `cld` clears `DF` bit 10.
- `std` sets `DF` bit 10.
- No other modeled flags are modified.
- No registers or memory are modified.

### Rejected syntax

```asm
cld eax
std 1
```

### Diagnostics

- `invalid-instruction-operands` for operands to `cld` or `std`.

### Required tests

Parser tests:

- accepts mixed-case `cld`/`std`.
- rejects operands.

Executor tests:

- `std` sets DF.
- `cld` clears DF.
- CF/ZF/SF/OF/PF/AF preserve.

Source-run JSON tests:

- final EFLAGS shows DF set/clear.

Manual browser smoke:

```asm
.code
main PROC
    std
    cld
main ENDP
END main
```

Expected final DF clear.

### String-instruction phase dependency map

The string-instruction phases depend on the current post-30 phase numbers below. Use this map when reading or editing the MOVS/STOS/LODS/CMPS/SCAS and REP/REPE/REPNE phases.

```text
Phase 111 - DF Flag and CLD/STD
  Owns DF storage and CLD/STD flag mutation.

Phase 112 - MOVS String Instructions Without REP
  Owns non-repeated MOVS behavior, checked memory reads/writes, ESI/EDI updates, and DF-based pointer direction for MOVS.

Phase 113 - STOS String Instructions Without REP
  Owns non-repeated STOS behavior, checked memory writes, EDI updates, accumulator source values, and DF-based pointer direction for STOS.

Phase 114 - LODS String Instructions Without REP
  Owns non-repeated LODS behavior, checked memory reads, accumulator loading, ESI updates, and DF-based pointer direction for LODS.

Phase 115 - CMPS String Instructions Without REP
  Owns non-repeated CMPS behavior, checked memory reads, comparison flag updates, ESI/EDI updates, and DF-based pointer direction for CMPS.

Phase 116 - SCAS String Instructions Without REP
  Owns non-repeated SCAS behavior, checked memory reads, accumulator comparison, EDI updates, and DF-based pointer direction for SCAS.

Phase 117 - REP for MOVS/STOS/LODS Operations
  Owns count-based repeated movement/store/load behavior for MOVS, STOS, and LODS.

Phase 118 - REPE/REPZ and REPNE/REPNZ for CMPS and SCAS
  Owns flag-dependent repeated comparison/search behavior for CMPS and SCAS.
```

If later roadmap text refers to old names such as `Phase 70 DF`, `Phase 73 CMPS/SCAS`, or `Phase 74 prefix loop infrastructure`, replace them with the matching current phase from this map. Do not renumber phases.

## 116. Phase 112 - MOVS String Instructions Without REP

### Goal

Implement one-element `movsb`, `movsw`, and `movsd` using ESI/EDI and DF.

### Work type

Parser, IR, executor, checked memory access.

### Dependencies

- Phase 111 - DF Flag and CLD/STD.
- Existing memory validation modes.

### Accepted syntax

```asm
movsb
movsw
movsd
```

No operands are accepted in this phase.

### Runtime semantics

- Width is selected by mnemonic: 1, 2, or 4 bytes.
- Read width bytes from `[ESI]`.
- Write width bytes to `[EDI]`.
- Validate read and write before mutation.
- If validation fails, do not modify memory, ESI, EDI, or flags.
- If success and `DF=0`, `ESI += width`, `EDI += width` modulo 2^32.
- If success and `DF=1`, `ESI -= width`, `EDI -= width` modulo 2^32.
- Preserve all modeled flags.

### Diagnostics

- `invalid-string-instruction-operands` for explicit operands.
- runtime memory diagnostics for invalid read/write.
- `.CONST` write failures use existing read-only memory diagnostic.

### Required tests

Core/executor tests:

- `movsb` copies one byte and increments ESI/EDI when DF clear.
- `movsw` copies two bytes little-endian and increments by 2.
- `movsd` copies four bytes and increments by 4.
- DF set decrements pointers.
- invalid source preserves state.
- invalid destination preserves state.
- `.CONST` destination fails without mutation.
- flags preserve.

Source-run JSON tests:

- memory-change rows show destination bytes.
- warnings/errors render through Simulator Messages.

Manual browser smoke:

```asm
.data
src BYTE "A", 0
dst BYTE 2 DUP(0)
.code
main PROC
    cld
    mov esi, OFFSET src
    mov edi, OFFSET dst
    movsb
main ENDP
END main
```

Expected `dst` first byte becomes `41h`.

## 117. Phase 113 - STOS String Instructions Without REP

### Goal

Implement `stosb`, `stosw`, and `stosd` without repeat prefixes.

### Runtime contract

- Store `AL`, `AX`, or `EAX` to `[EDI]` using the mnemonic width.
- Write through central checked memory helpers.
- Update `EDI` by `+width` when `DF = 0` and `-width` when `DF = 1` after a successful store.
- Preserve all modeled flags.
- No partial mutation on failed memory validation.

### Required tests

- Byte, word, and dword stores.
- Direction flag increment and decrement behavior.
- `.CONST` write rejection.
- Invalid-address diagnostics.
- No-operand-only accepted syntax and explicit-operand rejection.

## 118. Phase 114 - LODS String Instructions Without REP

### Goal

Implement `lodsb`, `lodsw`, and `lodsd` without repeat prefixes.

### Runtime contract

- Read from `[ESI]` using the mnemonic width into `AL`, `AX`, or `EAX`.
- Read through central checked memory helpers.
- Update `ESI` by `+width` when `DF = 0` and `-width` when `DF = 1` after a successful read.
- Preserve all modeled flags.
- Accepted syntax is no-operand mnemonics only; explicit operand forms are rejected.

### Required tests

- Byte, word, and dword loads.
- Direction flag increment and decrement behavior.
- Invalid-address diagnostics.
- Explicit operand rejection such as `lods BYTE PTR [esi]`.

## 119. Phase 115 - CMPS String Instructions Without REP

### Goal

Implement `cmpsb`, `cmpsw`, and `cmpsd` without repeat prefixes.

### Runtime contract

- Compare `[ESI] - [EDI]` using mnemonic width.
- Read both operands through central checked memory helpers before flag mutation.
- Update `CF/ZF/SF/OF/PF/AF` through the same compare helper used by `cmp`.
- Update `ESI` and `EDI` after the comparison executes, using `DF` and width.
- Produce no memory-change rows.

### Required tests

- Equal and unequal byte/word/dword comparisons.
- Direction flag increment and decrement behavior.
- Invalid source/destination address diagnostics.
- No operand syntax only.

## 120. Phase 116 - SCAS String Instructions Without REP

### Goal

Implement `scasb`, `scasw`, and `scasd` without repeat prefixes.

### Runtime contract

- Compare accumulator `AL`, `AX`, or `EAX` against memory at `[EDI]` using mnemonic width.
- Read memory through central checked memory helpers before flag mutation.
- Update `CF/ZF/SF/OF/PF/AF` through the same compare helper used by `cmp`.
- Update `EDI` after the comparison executes, using `DF` and width.
- Produce no memory-change rows.

### Required tests

- Equal and unequal byte/word/dword comparisons.
- Direction flag increment and decrement behavior.
- Invalid address diagnostics.
- No operand syntax only.

## 121. Phase 117 - REP for MOVS/STOS/LODS Operations

### Goal

Implement `rep` for non-comparison string instructions.

### Work type

Parser prefix support, executor loop, instruction-limit integration.

### Dependencies

- Phase 112 - MOVS String Instructions Without REP.
- Phase 113 - STOS String Instructions Without REP.
- Phase 114 - LODS String Instructions Without REP.
- Instruction-limit watchdog.

### Accepted syntax

```asm
rep movsb
rep movsw
rep movsd
rep stosb
rep stosw
rep stosd
rep lodsb
rep lodsw
rep lodsd
```

### Runtime semantics

- If `ECX = 0`, perform zero memory accesses and preserve ESI/EDI/EAX/memory/flags.
- Otherwise execute the underlying string operation up to ECX times.
- After each successful element, decrement ECX by 1.
- Pointer/register effects occur per underlying instruction.
- Each element counts against the instruction-limit watchdog as one repeated element.
- If an element fails memory validation, stop with runtime diagnostic and preserve the state for that failed element; prior successfully executed elements remain committed.

### Rejected syntax

- `rep` before non-string instruction.
- `rep cmps*` and `rep scas*` in this phase; comparison repeats are handled by **Phase 118 - REPE/REPZ and REPNE/REPNZ for CMPS and SCAS**.
- multiple prefixes.
- prefix without following instruction.

### Diagnostics

- `invalid-repeat-prefix`.
- instruction-limit diagnostic if repeated execution exceeds configured limit.

### Required tests

Executor tests:

- `rep movsb` copies ECX bytes and ends ECX at zero.
- `rep stosd` fills DWORD elements.
- `rep lodsb` ends EAX low byte as last loaded byte.
- `rep lodsb` with ECX zero preserves AL.
- ECX zero no-op for every REP family preserves registers, memory, and flags.
- DF decrement behavior.
- memory error after partial progress preserves previous committed elements and reports failure.
- instruction-limit failure includes completed element count.

Source-run JSON and rendered diagnostics tests for invalid prefix, memory failure, and instruction-limit failure.

## 122. Phase 118 - REPE/REPZ and REPNE/REPNZ for CMPS and SCAS

### Goal

Implement conditional repeat prefixes for comparison string instructions.

### Work type

Parser prefix support, executor loop, flag-driven stop conditions.

### Dependencies

- Phase 115 - CMPS String Instructions Without REP.
- Phase 116 - SCAS String Instructions Without REP.
- Phase 117 - REP for MOVS/STOS/LODS Operations, for prefix parsing, IR representation, and repetition scaffolding.

### Accepted syntax

```asm
repe cmpsb
repz cmpsb
repne cmpsb
repnz cmpsb
repe scasb
repz scasb
repne scasb
repnz scasb
```

And the `w`/`d` size variants.

### Runtime semantics

- If ECX is zero at entry, perform zero memory accesses and preserve flags.
- Execute one comparison element.
- Decrement ECX after each executed element.
- For `repe`/`repz`, continue while ECX is nonzero and ZF is 1.
- For `repne`/`repnz`, continue while ECX is nonzero and ZF is 0.
- Final flags are the flags from the last executed comparison, or preserved if no element executed.
- Pointer updates occur per element according to DF.
- Each element counts against instruction limit.

### Rejected syntax

- `repe`/`repne` with MOVS/STOS/LODS in this educational subset.
- prefixes on non-string instructions.

### Required tests

Executor tests:

- `repe cmpsb` stops on first mismatch.
- `repne scasb` stops on first match.
- aliases `repz`/`repe` and `repnz`/`repne` equivalent.
- ECX zero no-op preserves flags.
- DF decrement behavior.
- invalid memory after partial progress.
- instruction-limit interaction.

Source-run JSON and rendered diagnostics tests for invalid prefix and runtime memory errors.

## 123. Phase 119 - MASM COMMENT Block Skipping

### Goal

Implement MASM `COMMENT delimiter ... delimiter` block skipping with correct source positions.

### Work type

Lexer/parser source skipping and diagnostics.

### Dependencies

- Existing source-location model.
- Existing lexer diagnostic surfacing.

### Accepted syntax

```asm
COMMENT !
any text here, including .IF, STRUCT, invalid tokens
!
```

The delimiter is the first non-whitespace character after `COMMENT`.

### Semantics

- Skip all bytes until the next occurrence of the delimiter.
- Preserve line/column/byte offset tracking through skipped content.
- Do not tokenize or diagnose content inside the COMMENT block.
- `COMMENT` itself is not a runtime directive.
- Empty COMMENT blocks are valid.
- A terminating delimiter may appear on the same line as the opening directive.

### Rejected forms

- Missing delimiter.
- Newline/EOF as delimiter.
- Unterminated block.

### Diagnostics

- `unterminated-comment-block`.
- `invalid-comment-delimiter`.

Diagnostics point at the `COMMENT` keyword or delimiter as appropriate.

### Required tests

Lexer/parser tests:

- COMMENT block before `.code`.
- COMMENT block inside `.code` between instructions.
- COMMENT block containing otherwise invalid tokens.
- COMMENT block with nested-looking delimiter text.
- Empty same-line COMMENT block.
- Same-line delimiter after ignored text.
- Unterminated block reports correct start location.
- Source line after block has correct line/column.

Source-run JSON and rendered diagnostics tests for unterminated block.

Manual browser smoke:

Program with COMMENT block containing `.IF bad` still runs normally after block.

## 124. Phase 120 - Listing and Documentation No-Ops

### Goal

Accept selected MASM listing/documentation directives as compatibility no-ops or clear diagnostics.

### Work type

Parser compatibility handling.

### Dependencies

- Header directive handling.
- Diagnostic rendering tests.

### Accepted no-ops

```asm
.LIST
.NOLIST
.CREF
.NOCREF
.TFCOND
ECHO arbitrary text
```

### Semantics

- No Program Console output.
- No Simulator Message by default.
- No listing file generation.
- Source location tracking preserved.
- Accepted anywhere a listing directive is allowed in paste-compatible source: preamble, between sections, and between statements.

### Rejected forms

- `.LISTMACRO`, `.NOLISTMACRO`, `.LISTIF`, `.NOLISTIF`, `.LISTALL` until explicitly accepted.
- Malformed dotted listing directives.

### Diagnostics

- `unsupported-listing-directive`.

### Required tests

Parser/source-run tests:

- no-op directives before `.data`.
- no-op directives inside `.code` between instructions.
- ECHO with arbitrary tail text.
- unsupported listing directive diagnostic.
- no Program Console output from ECHO.

Rendered Simulator Messages tests for unsupported listing directive.

## 125. Phase 121 - LENGTH Compatibility Operator

### Goal

Implement `LENGTH symbol` as a narrow compatibility alias for `LENGTHOF symbol`.

### Work type

Parser/operator handling.

### Dependencies

- Existing `LENGTHOF`.
- Existing symbol metadata.

### Accepted syntax

```asm
mov eax, LENGTH nums
COUNT = LENGTH nums
```

### Semantics

- Accepted only where `LENGTHOF symbol` is accepted.
- Returns the same element count as `LENGTHOF` for the symbol.
- Works for `.data`, `.DATA?`, `.CONST`, STRUCT variables only after their metadata phase supports it.

### Rejected syntax

```asm
LENGTH
LENGTH eax
LENGTH [nums]
LENGTH nums + 1
```

### Diagnostics

- `unsupported-length-expression`.
- `unknown-symbol` for unknown symbol.

### Required tests

Parser/source-run tests:

- scalar returns 1.
- array returns element count.
- string returns byte count for BYTE string metadata.
- nested DUP metadata returns expanded count.
- `.DATA?` and `.CONST` symbols work if those sections are implemented.
- malformed forms rejected.

Rendered diagnostics tests for malformed expression and unknown symbol.

## 126. Phase 122 - SIZE Compatibility Operator

### Goal

Implement `SIZE symbol` as a narrow compatibility alias for `SIZEOF symbol`.

### Work type

Parser/operator handling.

### Dependencies

- Existing `SIZEOF`.
- Existing symbol metadata.

### Accepted syntax

```asm
mov eax, SIZE nums
COUNT = SIZE nums
```

### Semantics

- Accepted only where `SIZEOF symbol` is accepted.
- Returns the same total byte size as `SIZEOF` for the symbol.
- Does not implement every historical MASM `SIZE` edge case.

### Rejected syntax

```asm
SIZE
SIZE eax
SIZE [nums]
SIZE nums + 1
```

### Diagnostics

- `unsupported-size-expression`.
- `unknown-symbol`.

### Required tests

Parser/source-run tests:

- scalar DWORD returns 4.
- array DWORD 10 DUP returns 40.
- string returns emitted byte count.
- STRUCT variable works only after Phase 90.
- malformed forms rejected.

Rendered diagnostics tests for malformed expression and unknown symbol.

## 127. Phase 123 - High-Level Flow Block Parser and Recovery Infrastructure

### Goal

Add parser infrastructure for high-level MASM runtime flow blocks without lowering them yet.

### Work type

Parser block stack, recovery, diagnostics.

### Dependencies

- Existing multi-diagnostic recovery.
- Existing label/branch infrastructure.

### Recognized directives

```asm
.IF
.ELSEIF
.ELSE
.ENDIF
.WHILE
.ENDW
.REPEAT
.UNTIL
.UNTILCXZ
.BREAK
.CONTINUE
```

### Implement

- Token recognition and block stack for runtime high-level directives.
- Nested block tracking.
- Structural diagnostics for missing/extra/mismatched endings.
- Recovery to safe line/block boundaries.
- No execution/lowering yet.

### Explicit non-goals

- No condition expression evaluation.
- No generated labels or jumps yet.
- No conditional assembly support.
- No IFDEF/IFNDEF/ELSE/ENDIF without leading dot.

### Diagnostics

- `malformed-high-level-flow`.
- `unsupported-high-level-condition` for condition content before Phase 81.
- `break-outside-loop` and `continue-outside-loop` may be recognized but not fully lowered yet.

### Required tests

Parser tests:

- balanced `.IF/.ENDIF` recognized.
- nested `.IF` inside `.WHILE` recognized.
- extra `.ENDIF` diagnostic.
- missing `.ENDIF` diagnostic.
- `.ENDW` closing `.IF` diagnostic.
- conditional assembly `IFDEF/ENDIF` remains separate unsupported feature.

Rendered diagnostics tests for malformed block spans.

## 128. Phase 124 - High-Level Flow Runtime Condition Subset

### Goal

Implement the first runtime condition parser/lowering helper used by `.IF`, `.WHILE`, and `.UNTIL` phases.

### Work type

Parser expression subset and IR lowering helper.

### Dependencies

- Existing CMP/Jcc phases.
- Phase 80 block infrastructure.

### Accepted condition forms

```asm
.IF eax == 0
.IF eax != ebx
.IF eax < 10       ; signed
.IF eax <= value   ; signed if value is typed source
.IF eax > ebx      ; signed
.IF eax >= ebx     ; signed
.IF ZERO?
.IF !ZERO?
.IF CARRY?
.IF !CARRY?
```

Memory conditions require known width:

```asm
.IF DWORD PTR [esi] == 0
.IF value != 10
```

### Rejected condition forms

- Boolean `&&` / `||`.
- Parenthesized compound expressions.
- Function calls.
- String comparisons.
- Unsigned relation keywords unless a later phase adds them.
- Memory/immediate comparisons with ambiguous memory width.

### Lowering output

The helper must produce an internal condition object that can emit CMP/Jcc or direct flag-test jumps. It must not emit user-visible labels yet unless called by a lowering phase.

### Diagnostics

- `unsupported-high-level-condition`.
- existing ambiguous-memory-width diagnostic.
- existing unknown-symbol diagnostics.

### Required tests

Parser/helper tests:

- equality/inequality register-register, register-immediate, register-symbol.
- signed relational forms.
- ZERO/CARRY flag predicates.
- negated flag predicates.
- ambiguous memory rejected at memory operand span.
- unsupported compound conditions rejected.

Rendered diagnostics tests for unsupported conditions.

## 129. Phase 125 - .IF/.ELSEIF/.ELSE/.ENDIF Lowering

### Goal

Lower high-level conditional blocks to internal labels and conditional jumps.

### Work type

Parser lowering and execution integration.

### Dependencies

- Phase 80 block infrastructure.
- Phase 81 condition subset.
- Existing labels/CMP/Jcc.

### Accepted syntax

```asm
.IF condition
    statements
.ELSEIF condition
    statements
.ELSE
    statements
.ENDIF
```

### Semantics

- Conditions are runtime conditions.
- Lower to synthetic labels and existing branch IR.
- At most one branch body executes.
- Nested `.IF` blocks supported.
- Synthetic labels must not collide with user labels.
- Source metadata for generated branches points to the high-level directive that caused them.

### Rejected forms

- `.ELSEIF` after `.ELSE`.
- multiple `.ELSE` blocks.
- `.ELSE` without active `.IF`.
- `.ENDIF` without active `.IF`.
- unsupported conditions.

### Required tests

Source-run tests:

- true `.IF` executes body.
- false `.IF` skips body.
- `.ELSE` executes only when IF false.
- `.ELSEIF` first true branch wins.
- nested `.IF` works.
- source instruction count deterministic.

Diagnostics tests:

- malformed nesting.
- unsupported condition.
- duplicate ELSE.

Rendered Simulator Messages tests for malformed forms.

Manual browser smoke:

```asm
.code
main PROC
    mov eax, 5
    .IF eax == 5
        mov ebx, 1
    .ELSE
        mov ebx, 2
    .ENDIF
main ENDP
END main
```

Expected `EBX = 1`.

## 130. Phase 126 - .WHILE/.ENDW Lowering

### Goal

Lower `.WHILE` loops to internal labels and conditional branches.

### Work type

Parser lowering and execution integration.

### Dependencies

- Phase 81 condition subset.
- Phase 82 synthetic label infrastructure.
- Instruction-limit watchdog.

### Accepted syntax

```asm
.WHILE condition
    statements
.ENDW
```

### Semantics

- Test condition before each iteration.
- Execute zero or more times.
- Lower using synthetic loop start/end labels.
- Nested `.WHILE` and `.IF` supported.
- Instruction limit prevents infinite loops.

### Rejected forms

- `.ENDW` without `.WHILE`.
- missing `.ENDW`.
- unsupported condition.

### Required tests

Source-run tests:

- zero-iteration loop.
- multi-iteration loop.
- nested loop.
- condition with memory operand.
- instruction-limit failure for infinite loop.

Rendered diagnostics tests for malformed blocks and instruction limit.

## 131. Phase 127 - .REPEAT/.UNTIL Lowering

### Goal

Lower post-test `.REPEAT/.UNTIL` loops.

### Work type

Parser lowering and execution integration.

### Dependencies

- Phase 81 condition subset.
- Phase 82 synthetic label infrastructure.

### Accepted syntax

```asm
.REPEAT
    statements
.UNTIL condition
```

### Semantics

- Body executes at least once.
- Condition is tested after body.
- Loop exits when condition is true.
- Nested blocks supported.

### Rejected forms

- `.UNTIL` without `.REPEAT`.
- missing `.UNTIL`.
- unsupported condition.

### Required tests

Source-run tests:

- body executes once when condition initially true.
- body repeats until condition true.
- nested `.REPEAT` inside `.IF`.
- instruction-limit failure.

Rendered diagnostics tests for malformed blocks.

## 132. Phase 128 - .UNTILCXZ Lowering

### Goal

Implement `.UNTILCXZ` as a post-test loop terminator using `ECX == 0`.

### Work type

Parser lowering and execution integration.

### Dependencies

- Phase 84 `.REPEAT` infrastructure.

### Accepted syntax

```asm
.REPEAT
    statements
.UNTILCXZ
```

### Semantics

- Body executes at least once.
- At `.UNTILCXZ`, exit when `ECX == 0`; otherwise branch to repeat start.
- Does not implicitly decrement ECX.
- Does not modify flags.

### Rejected forms

- `.UNTILCXZ` outside `.REPEAT`.
- `.UNTILCXZ condition` with extra condition text.

### Required tests

Source-run tests:

- ECX zero after first body exits.
- ECX decremented by user code loops until zero.
- extra operands rejected.

Rendered diagnostics tests for outside loop and extra condition.

## 133. Phase 129 - .BREAK and .CONTINUE Lowering

### Goal

Implement `.BREAK` and `.CONTINUE` inside `.WHILE` and `.REPEAT` loops.

### Work type

Parser lowering and synthetic target resolution.

### Dependencies

- Phase 83 `.WHILE`.
- Phase 84 `.REPEAT`.
- Phase 85 `.UNTILCXZ` where relevant.

### Accepted syntax

```asm
.BREAK
.CONTINUE
.BREAK .IF condition
.CONTINUE .IF condition
```

The conditional forms are optional in this phase only if fully specified and tested; otherwise they must be rejected with `unsupported-high-level-condition`.

### Semantics

- `.BREAK` branches to the nearest enclosing loop exit label.
- `.CONTINUE` branches to nearest enclosing loop continue point:
  - `.WHILE`: condition re-test label.
  - `.REPEAT`: bottom condition check label.
- Nested loops target the nearest loop.

### Rejected forms

- outside loop.
- malformed conditional suffix.
- use inside `.IF` without enclosing loop.

### Required tests

Source-run tests:

- `.BREAK` exits nearest WHILE loop.
- `.CONTINUE` skips rest of current iteration.
- nested loop chooses nearest loop.
- outside loop diagnostic.

Rendered diagnostics tests for outside-loop cases.

## 134. Phase 130 - STRUCT Type Declaration Metadata

### Goal

Parse simple STRUCT type declarations and compute field offsets.

### Work type

Parser metadata only.

### Dependencies

- Existing data declaration parser.
- Existing symbol/type metadata tables.

### Accepted syntax

```asm
Point STRUCT
    x DWORD ?
    y DWORD ?
Point ENDS
```

### Semantics

- STRUCT names follow the active user-symbol case policy.
- Field names follow the active user-symbol case policy within the struct.
- Fields use existing scalar integer declaration types only.
- Offsets are assigned in declaration order.
- No implicit padding in first implementation.
- Total size is sum of field byte sizes.

### Rejected forms

- nested STRUCT definitions.
- UNION.
- methods/procedures inside STRUCT.
- duplicate field names.
- mismatched `ENDS` name.
- initializers requiring runtime data layout in this metadata-only phase.

### Diagnostics

- `duplicate-struct-field`.
- `unknown-struct-type`.
- `malformed-struct-declaration`.
- `unsupported-struct-field-type`.

### Required tests

Parser tests:

- two DWORD fields offsets 0 and 4, size 8.
- BYTE/WORD/DWORD mixed fields, no padding.
- duplicate field rejected.
- mismatched ENDS rejected.
- nested STRUCT rejected.

Rendered diagnostics tests for duplicate and mismatch.

## 135. Phase 131 - STRUCT Variable Layout and Initializers

### Goal

Allow variables of known STRUCT types in data sections.

### Work type

Data layout and symbol metadata.

### Dependencies

- Phase 87 STRUCT type metadata.
- Existing data image builder.

### Accepted syntax

```asm
.data
p Point <>
p2 Point <1, 2>
```

This phase supports exactly these angle-bracket initializer forms:

- `<>` zero/default initializer.
- `<scalar, scalar, ...>` with exactly one initializer per scalar field.

Partial field initializers, nested aggregate initializers, named-field initializers, and default-value mixing are rejected in this phase.

### Runtime/data semantics

- `<>` emits deterministic zero bytes for all fields.
- Explicit initializer count must match field count.
- Initializers use existing scalar initializer validation per field width/type.
- Symbol metadata marks the variable as a STRUCT instance with field table pointer.

### Rejected forms

- partial initializer lists.
- too many initializers.
- nested STRUCT initializers.
- arrays of STRUCT unless explicitly added later.
- DUP of STRUCT unless explicitly added later.

### Required tests

Parser/data tests:

- default zero initialization.
- explicit scalar initialization.
- signed field range validation.
- packed byte layout.
- malformed initializer counts rejected.

Source-run tests:

- `SIZEOF p` returns struct size only after Phase 90, or is explicitly rejected until then.

Rendered diagnostics tests for malformed initializers.

## 136. Phase 132 - STRUCT Field Access for Direct Data Symbols

### Goal

Support direct field access such as `p.x` for known STRUCT variables.

### Work type

Parser operand resolution, memory operand metadata, executor reuse.

### Dependencies

- Phase 88 STRUCT variables.
- Existing direct symbol memory operands and width resolver.

### Accepted syntax

```asm
mov eax, p.x
mov p.y, 10
mov DWORD PTR p.x, 20
```

### Semantics

- `p.field` resolves to base address of `p` plus field offset.
- Field access width defaults to the field type width.
- Existing `PTR` overrides apply only if the full requested byte range remains inside the selected field and the width is executable in MASM32 Educational Mode.
- All memory access goes through checked memory helpers.
- Memory-change rows should display `p.x` or `p + fieldOffset` with field metadata.

### Rejected forms

- unknown variable.
- unknown field.
- field access on non-STRUCT symbol.
- register-indirect field access such as `[esi].x` until a later phase.
- nested field access.

### Diagnostics

- `unknown-struct-field`.
- `not-a-struct-symbol`.
- existing memory diagnostics.

### Required tests

Source-run tests:

- read field.
- write field.
- field width inferred.
- PTR override works for smaller/larger supported field slices where existing memory-width policy allows it.
- `.CONST` struct field write fails.
- unknown field diagnostic.
- malformed whitespace around field selector diagnostics.
- field-width-conflict diagnostic for explicit PTR extending outside the field.

Rendered diagnostics tests for unknown field and const write.

## 137. Phase 133 - STRUCT Operators and Symbol Metadata Integration

### Goal

Integrate STRUCT variables and fields with metadata operators and memory display.

### Work type

Parser/operator metadata.

### Dependencies

- Phase 89 field access.
- Existing `TYPE`, `LENGTHOF`, `SIZEOF`, `LENGTH`, and `SIZE`.

### Semantics

For a STRUCT type `Point` with size 8 and variable `p Point <>`:

- `TYPE p` returns 8.
- `SIZEOF p` returns 8.
- `LENGTHOF p` returns 1.
- `SIZE p` returns 8 once SIZE compatibility exists.
- `LENGTH p` returns 1 once LENGTH compatibility exists.
- `TYPE p.x` returns field element size.
- `SIZEOF p.x` returns field byte size.

### Rejected forms

- operators on STRUCT type names directly unless explicitly accepted.
- operators on unknown field.
- nested field operators.

### Required tests

Parser/source-run tests:

- TYPE/SIZEOF/LENGTHOF struct variable.
- TYPE/SIZEOF field.
- LENGTH/SIZE aliases if their phases are complete.
- unknown field diagnostics.

Rendered diagnostics tests for unsupported operator forms.

## 138. Phase 134 - TYPEDEF Alias Metadata

### Goal

Implement a narrow metadata-only `TYPEDEF` subset for scalar aliases.

### Work type

Parser type-symbol metadata.

### Dependencies

- Existing data declaration types.

### Accepted syntax

```asm
COUNT_T TYPEDEF DWORD
BYTE_ALIAS TYPEDEF BYTE
```

### Semantics

- Type aliases follow the active user-symbol case policy.
- Aliases may be used in data declarations where the underlying scalar type is supported.
- Alias chains are allowed only if they resolve acyclically to a supported scalar type.
- Alias metadata preserves original spelling and target type.

### Rejected forms

- alias cycles.
- unknown target type.
- pointer/procedure/STRUCT/RECORD typedefs.
- duplicate alias names.

### Diagnostics

- `unsupported-typedef-target`.
- `typedef-cycle`.
- `duplicate-type-symbol`.

### Required tests

Parser/data tests:

- scalar alias declaration.
- alias used in `.data`.
- alias works with TYPE/SIZEOF/LENGTHOF.
- chained alias.
- cycle rejected.
- unknown target rejected.

Rendered diagnostics tests for cycle and unknown target.

## 139. Phase 135 - RECORD Declaration Layout Metadata

### Goal

Parse RECORD type declarations and compute bitfield metadata.

### Work type

Parser metadata only.

### Dependencies

- Constant expression evaluator.
- Type-symbol table.

### Accepted syntax

```asm
Flags RECORD carry:1, mode:3, value:4
```

### Semantics

- Total width must be 1..32 bits.
- Field widths must be positive constant expressions.
- Field names follow the active user-symbol case policy within the record.
- Field bit positions must be assigned deterministically. First field occupies the highest-order bits of the record. Metadata must store exact inclusive bit ranges using zero-based bit indices, where bit 0 is the least-significant bit of the selected storage unit.
- No runtime variables yet.

### Rejected forms

- zero or negative width.
- total width greater than 32.
- duplicate field name.
- initializer/default field values unless a later phase adds them.

### Diagnostics

- `invalid-record-width`.
- `duplicate-record-field`.
- `malformed-record-declaration`.

### Required tests

Parser tests:

- valid record widths and bit positions.
- constant-expression width.
- total >32 rejected.
- zero width rejected.
- duplicate field rejected.

Rendered diagnostics tests for invalid widths.

## 140. Phase 136 - RECORD Variables and Initializers

### Goal

Allow variables of known RECORD types using numeric initializers.

### Work type

Data layout and symbol metadata.

### Dependencies

- Phase 92 RECORD metadata.

### Accepted syntax

```asm
.data
f Flags 0
f2 Flags 0FFh
```

### Semantics

- Storage width is the smallest supported integer storage size that holds total record width: 1, 2, or 4 bytes.
- Numeric initializer must fit total record width.
- Stored little-endian in the selected storage width.
- Symbol metadata marks record type and bitfield table.

### Rejected forms

- record constructor syntax.
- field-name initializers.
- initializer out of range.
- arrays/DUP of records unless a later phase adds them.

### Required tests

Data/source-run tests:

- 8-bit, 16-bit, 32-bit record storage selection.
- initializer range boundary.
- TYPE/SIZEOF metadata once integrated.
- out-of-range diagnostic.

Rendered diagnostics tests for range failure.

## 141. Phase 137 - WIDTH and MASK Operators

### Goal

Implement `WIDTH` and `MASK` for RECORD metadata.

### Work type

Parser/operator constant folding.

### Dependencies

- Phase 92 RECORD metadata.

### Accepted syntax

```asm
mov eax, WIDTH Flags
mov ebx, WIDTH Flags.mode
mov ecx, MASK Flags.mode
```

### Semantics

- `WIDTH recordType` returns total record width in bits.
- `WIDTH recordType.field` returns field width in bits.
- `MASK recordType.field` returns a 32-bit mask with the field's bits set at their assigned positions.
- Operators fold to immediates in constant contexts.

### Rejected forms

- unknown record type.
- unknown field.
- MASK without field.
- WIDTH on non-record/non-field unless a later phase expands it.

### Diagnostics

- `unknown-record-type`.
- `unknown-record-field`.
- `unsupported-width-expression`.
- `unsupported-mask-expression`.

### Required tests

Parser/source-run tests:

- WIDTH record.
- WIDTH field.
- MASK field.
- use in equate.
- use in data initializer.
- unknown field diagnostic.
- malformed whitespace around field selector diagnostics.
- field-width-conflict diagnostic for explicit PTR extending outside the field.

Rendered diagnostics tests for unsupported forms.

## 142. Phase 138 - Macros.inc Virtual Macro Registry and Diagnostics

### Goal

Recognize selected Macros.inc classroom macro names and classify unsupported macro invocations cleanly without implementing full MASM macro expansion.

### Work type

Parser classification and diagnostics.

### Dependencies

- `INCLUDE Macros.inc` virtual include handling.
- Program Console output infrastructure.

### Registry

Known selected built-ins for this batch:

```text
mWrite
mWriteLn
mReadString
```

Known but unsupported Macros.inc names recognized only for diagnostics in this batch are: `mDumpMem`, `mDump`, `mShow`, `mShowRegister`, `mWriteSpace`, and `mWriteString`. Unknown macro-looking invocations remain normal unknown-symbol or unsupported syntax depending on context.

### Semantics

- Selected macros are parser-recognized virtual built-ins.
- User-defined `MACRO` / `ENDM` remains unsupported.
- Macro expansion recursion, parameters, text substitution, and local labels are not implemented.

### Diagnostics

- `unsupported-macro-invocation` for known but unsupported macro names.
- `macro-language-unsupported` for user-defined macro declarations.
- `invalid-macro-argument` for selected built-ins with malformed arguments.

### Required tests

Parser tests:

- `INCLUDE Macros.inc` registers selected names.
- `MACRO` / `ENDM` still unsupported.
- known unsupported macro reports macro-specific diagnostic.
- malformed selected macro reports invalid argument.

Rendered diagnostics tests for each diagnostic category.

## 143. Phase 139 - mWrite Built-In

### Goal

Implement selected virtual `Macros.inc` built-in `mWrite` without adding the MASM macro expander.

### Runtime contract

- Accepted syntax: `mWrite "ASCII literal"`.
- Emit the literal body as exact ASCII bytes according to the Program Console byte policy.
- Reject escape sequences, multiple arguments, non-string arguments, and text macro forms.
- Respect output limits with validation-first no-partial-output behavior.

### Required tests

- Simple ASCII literal output.
- Empty literal output.
- Escape-sequence rejection.
- Multi-argument rejection.
- Output-limit diagnostics.

## 144. Phase 140 - mWriteLn Built-In

### Goal

Implement selected virtual `Macros.inc` built-in `mWriteLn` without adding the MASM macro expander.

### Runtime contract

- Accepted syntax: `mWriteLn "ASCII literal"`.
- Emit the literal body followed by the Program Console newline sequence `\n`.
- Reject escape sequences, multiple arguments, non-string arguments, and text macro forms.
- Respect output limits with validation-first no-partial-output behavior.

### Required tests

- Literal plus newline output.
- Empty literal plus newline output.
- Escape-sequence rejection.
- Output-limit diagnostics with no partial literal or newline.

## 145. Phase 141 - mReadString Built-In

### Goal

Implement selected `mReadString` macro convenience by lowering to the existing `ReadString` path for statically known buffers.

### Work type

Parser lowering and input integration.

### Dependencies

Phase 141 depends on the completed behavior from these phases:

- Phase 108 - ReadString Checked Write and Completion.
- Phase 138 - Macros.inc Virtual Macro Registry and Diagnostics.
- The shared input wait-state and worker protocol behavior introduced by the Irvine32 input routine phases, especially Phase 103 - Irvine32 ReadChar, Phase 104 - Irvine32 ReadInt, and Phase 108 - ReadString Checked Write and Completion, if Phase 141 reuses their input-submission infrastructure.

Interpretation rules:

- `mReadString` must not be implemented before the underlying `ReadString` runtime behavior exists.
- `mReadString` must be classified through the virtual macro registry from Phase 138 - Macros.inc Virtual Macro Registry and Diagnostics.
- `mReadString` must not create a second independent macro-recognition path.
- `mReadString` may reuse shared input-state and worker protocol infrastructure introduced by earlier Irvine32 input phases, but it must not depend on obsolete phase numbers or pre-renumbering references.
- If a future assistant decides Phase 141 does not need any infrastructure from Phase 103 - Irvine32 ReadChar or Phase 104 - Irvine32 ReadInt, the milestone report must say so explicitly. Do not silently remove those references merely because `mReadString` is string-focused; the point is to verify whether shared input wait-state machinery is reused.

### Accepted syntax

```asm
mReadString buffer
```

### Semantics

- `buffer` must be a known writable byte-addressable data symbol.
- Buffer capacity must be statically known from symbol metadata.
- Lowering sets:
  - `EDX = OFFSET buffer`
  - `ECX = SIZEOF buffer - 1`
  - invokes `ReadString` behavior
- Register effects match the lowered operations and `ReadString` contract.
- If `SIZEOF buffer < 2`, reject because no non-null character plus terminator can fit.

### Rejected forms

- unknown symbol.
- non-byte-compatible symbol if no safe capacity can be inferred.
- `.CONST` buffer.
- register-indirect buffer.
- explicit length argument unless a later phase adds it.

### Diagnostics

- `invalid-macro-argument`.
- `invalid-readstring-buffer`.
- existing input diagnostics.

### Required tests

Source-run tests:

- valid byte buffer enters read-string wait.
- submitted input writes buffer and returns count.
- too-small buffer rejected.
- `.CONST` buffer rejected.
- unknown symbol rejected.
- truncation follows ReadString contract.

Rendered diagnostics tests for invalid buffer and unknown symbol.

Manual browser smoke:

```asm
INCLUDE Irvine32.inc
INCLUDE Macros.inc
.data
buf BYTE 8 DUP(?)
.code
main PROC
    mReadString buf
    mov edx, OFFSET buf
    call WriteString
    exit
main ENDP
END main
```

Submit `Test`; Program Console shows `Test` after output.

## 146. Phase 142 - String, High-Level Flow, STRUCT/RECORD, and Macro Integration Harness

### Goal

Perform a validation-only smoke harness over string instructions, high-level flow lowering, STRUCT/RECORD metadata, and selected Macros.inc built-ins.

### Required coverage

- `cld/std`, `movs`, `stos`, `lods`, `cmps`, and `scas` in separate source-run programs.
- REP zero-count and mid-repeat watchdog behavior.
- `.IF/.ELSEIF/.ELSE/.ENDIF`, `.WHILE`, `.REPEAT/.UNTIL`, `.UNTILCXZ`, `.BREAK`, and `.CONTINUE` lowering to ordinary IR and source-span metadata.
- STRUCT variable initialization and field access.
- RECORD WIDTH/MASK and initializer diagnostics.
- `mWrite`, `mWriteLn`, and `mReadString` smoke tests.
- Rendered diagnostics for one string instruction memory fault, one high-level-flow nesting error, one STRUCT initializer error, one RECORD field error, and one unsupported macro invocation.

## 147. Phase 143 - Debugger Session State and Worker Protocol Baseline

### Goal

Create the debugger session state and worker message protocol needed by later stepping, breakpoints, continue/pause, and step-over phases.

This is a backend/protocol milestone. It must not implement stepping behavior yet.

### Tasks

1. Add debugger session state with:
   - `sessionId`;
   - `sourceHash`;
   - `settingsHash`;
   - `runGeneration`;
   - `stateKind`;
   - `currentInstructionIndex`;
   - `currentProcedureName`;
   - `lastStopReason`;
   - instruction count;
   - active execution time placeholder;
   - stack summary placeholder;
   - breakpoint list placeholder.
2. Add worker protocol message types for:
   - `DEBUG_LOAD_SOURCE`;
   - `DEBUG_STATE`;
   - `DEBUG_ERROR`.
3. Add stale-session validation for debugger commands.
4. Add `stale-debug-session` diagnostics.
5. Add the initial debugger command/state validation table used by later phases. Commands not yet implemented must return a stable `debug-command-not-implemented` response rather than being silently ignored.
6. Reuse the same parser/source-run build path as Run mode; do not create a separate parser.
7. Return an initial paused/ready state after successful load.
8. Preserve source-run assembly diagnostics for invalid source.
9. Treat hard worker termination as invalidating all VM/debugger state. After termination, every stale `DEBUG_*` command must fail until the source is rebuilt in a fresh worker.
10. Add tests for valid load, invalid source, stale generation, repeated load replacing the previous session, hard termination invalidation, and protocol payload rejection for non-JSON-compatible values where the test harness can simulate them.

### Acceptance Criteria

Loading this source through the debugger load path:

```asm
.code
main PROC
    mov eax, 1
main ENDP
END main
```

returns a debugger state with:

```text
stateKind = ready
currentInstructionIndex = 0
lastStopReason = not-started
instructionCount = 0
```

No instruction is executed during this phase.

### Rejected / out of scope

- Step Into execution.
- Continue.
- Breakpoint binding.
- Step Over.
- UI rendering beyond existing state display scaffolding.

---

## 148. Phase 144 - Step Into Backend Execution

### Goal

Execute exactly one VM instruction through the debugger backend.

### Tasks

1. Add `DEBUG_STEP_INTO` worker command.
2. Execute exactly one VM instruction when state is `ready`, `paused`, or `stopped-at-breakpoint`.
3. Return a `DEBUG_STEP_RESULT` message.
4. Increment instruction count by exactly one for a normal instruction.
5. Execute Irvine intrinsics and selected virtual macros as one logical VM instruction when they are already implemented.
6. Preserve existing Run behavior.
7. Add backend tests for simple arithmetic, memory write, Irvine intrinsic as one step, and program completion after final instruction.

### Acceptance Criteria

```asm
.code
main PROC
    mov eax, 1
    add eax, 2
main ENDP
END main
```

After one step:

```text
EAX = 00000001h / 1
currentInstructionIndex = 1
instructionCount = 1
lastStopReason = step-complete
```

After the second step:

```text
EAX = 00000003h / 3
currentInstructionIndex = 2
instructionCount = 2
lastStopReason = step-complete
```

A later step at procedure completion returns `program-complete` or the then-current root-completion stop reason.

### Rejected / out of scope

- Breakpoint logic.
- Continue loop.
- Step Over.
- UI rendering.

---

## 149. Phase 145 - Step Into Stop Conditions and Source Metadata

### Goal

Finish Step Into by returning precise stop reasons, source metadata, and state snapshots for all supported one-step outcomes.

### Tasks

1. Attach source metadata to every step result:
   - source file;
   - line;
   - column;
   - byte offset;
   - span length;
   - original source text;
   - IR instruction index;
   - procedure name.
2. Return explicit stop reasons for:
   - `step-complete`;
   - `program-complete`;
   - `root-return`;
   - `exit`;
   - `waiting-for-input`;
   - `runtime-error`;
   - `instruction-limit-exceeded`;
   - `output-limit-exceeded`.
3. Return updated VM state after a step.
4. Preserve non-mutating behavior for failed instructions where the instruction contract requires validation-first behavior.
5. Add rendered Simulator Messages tests for runtime-error step results.

### Acceptance Criteria

A step that executes an invalid memory read returns:

```text
lastStopReason = runtime-error
sourceLine = line containing the memory instruction
spanLength points at the memory operand when available
```

The rendered Simulator Messages match the normal Run-mode runtime diagnostic wording.

### Rejected / out of scope

- Editor highlighting.
- Breakpoint gutter.
- Continue/pause.

---

## 150. Phase 146 - Register and Flag State Backend Snapshot

### Goal

Expose a stable backend register/flag state snapshot for debugger UI phases.

### Tasks

1. Return canonical registers in stable order:
   - `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`, `ESP`, `EIP`, `EFLAGS`.
2. Return aliases in grouped form:
   - `AX/AH/AL`, `BX/BH/BL`, `CX/CH/CL`, `DX/DH/DL`, `SI`, `DI`, `BP`, `SP`.
3. Return modeled flags only.
4. Include hex and unsigned decimal strings generated by tested shared formatting helpers.
5. Include changed-since-previous-step metadata.
6. Add tests for alias values after canonical register writes and alias writes.
7. Add tests for modeled flags after arithmetic, logical, comparison, and string compare/scan instructions when those phases exist.

### Acceptance Criteria

After:

```asm
.code
main PROC
    mov eax, 12345678h
main ENDP
END main
```

one Step Into state includes:

```text
EAX = 12345678h / 305419896
AX  = 5678h / 22136
AH  = 56h / 86
AL  = 78h / 120
```

### Rejected / out of scope

- DOM rendering.
- CodeMirror integration.
- Registers not modeled by MASM32 Educational Mode.

---

## 151. Phase 147 - Register and Flag Current-State UI

### Goal

Render the backend register and flag snapshot in the browser.

### Tasks

1. Add a current-state panel for canonical registers.
2. Add expandable or grouped alias display.
3. Add modeled flag display in stable order.
4. Highlight values changed since the previous step/continue command.
5. Show invalid/unavailable state clearly when no debug session is loaded.
6. Add UI formatter tests for register and flag rows.
7. Add manual browser verification after Wasm rebuild.

### Acceptance Criteria

After stepping `mov eax, 12345678h`, the UI shows `EAX`, `AX`, `AH`, and `AL` using the register-alias behavior established by **Phase 1 - Core CPU Register Model**, and highlights the changed values.

### Rejected / out of scope

- Editor current-line highlighting.
- Breakpoint UI.
- Step Over aggregate UI.

---

## 152. Phase 148 - Last-Step Delta Backend Schema

### Goal

Produce a backend-owned last-step delta for every Step Into result.

### Tasks

1. Add a structured last-step delta with:
   - instruction metadata;
   - stop reason;
   - register changes;
   - flag changes;
   - memory changes;
   - Program Console output delta;
   - Simulator Messages delta;
   - instruction count delta;
   - active time delta placeholder;
   - stack usage before/after.
2. Reuse existing memory-change JSON from Run mode.
3. Include deltas for successful steps, warning steps, runtime-error steps, input-wait steps, and program-completion steps.
4. Add native tests for delta contents.
5. Add Node formatter tests for rendered delta messages.

### Acceptance Criteria

Stepping:

```asm
.data
value DWORD 0
.code
main PROC
    mov value, 7
main ENDP
END main
```

returns a last-step delta containing one memory change for `value` and no Program Console output.

### Rejected / out of scope

- UI layout.
- Step Over aggregate deltas.

---

## 153. Phase 149 - Last-Step Delta UI Rendering

### Goal

Render last-step deltas in the browser.

### Tasks

1. Add a Last Step panel.
2. Show the executed source text and source location.
3. Show changed registers and flags.
4. Show memory changes using existing memory-change rendering.
5. Show Program Console output delta separately from Simulator Messages delta.
6. Hide unchanged aliases by default.
7. Add renderer tests for:
   - register-only change;
   - memory-only change;
   - flag-only change;
   - console-output change;
   - runtime-error diagnostic.

### Acceptance Criteria

After stepping `add eax, 2`, the Last Step panel shows the source line and the `EAX` before/after value, and does not show unrelated unchanged registers.

### Rejected / out of scope

- Memory visualization grouping improvements.
- Step Over aggregate UI.

---

## 154. Phase 150 - Execution Statistics Backend

### Goal

Expose execution statistics from the backend.

### Tasks

1. Track total instruction count.
2. Track instruction count delta per debugger command.
3. Track active execution time in milliseconds.
4. Pause active execution timer while VM is waiting for input.
5. Exclude time after a pause request has been acknowledged.
6. Return stop reason with every statistics snapshot.
7. Add deterministic tests for instruction count and command deltas.
8. Add smoke tests for active-time field presence without requiring exact wall-clock values.

### Acceptance Criteria

After two Step Into commands in a two-instruction program:

```text
instructionCount = 2
lastCommandInstructionCountDelta = 1
```

### Rejected / out of scope

- Stack summary calculations.
- UI rendering.
- Time-limit implementation if it does not already exist.

---

## 155. Phase 151 - Stack Summary Backend

### Goal

Expose stack-region usage and validity information from VM state.

### Tasks

1. Return stack region base, limit, and size.
2. Return current `ESP`.
3. Compute used, remaining, and peak stack bytes.
4. Mark stack summary invalid if `ESP` is outside the stack region.
5. Do not scan stack memory.
6. Update peak stack usage after every successful stack mutation.
7. Add tests for:
   - empty stack;
   - push/pop changes;
   - call/ret changes if implemented;
   - invalid `ESP`;
   - peak usage persists after pop.

### Acceptance Criteria

After one `push eax`, stack summary reports increased used bytes and decreased remaining bytes by 4.

### Rejected / out of scope

- Stack memory viewer.
- Call-stack panel.
- UI rendering.

---

## 156. Phase 152 - Execution Statistics and Stack Summary UI

### Goal

Render execution statistics and stack summary in the browser.

### Tasks

1. Add execution statistics panel.
2. Show total instruction count and last-command instruction delta.
3. Show active execution time with stable unit formatting.
4. Add stack summary panel.
5. Show stack used, remaining, peak, and invalid-ESP warning.
6. Add UI formatter tests for normal and invalid stack states.
7. Add manual browser verification after Wasm rebuild.

### Acceptance Criteria

A stepped program shows instruction count increasing one step at a time, and stack summary reflects `push`/`pop` behavior.

### Rejected / out of scope

- Source-line breakpoints.
- Memory visualization panel.

---

## 157. Phase 153 - Breakpoint Data Model and Source-Line Mapping

### Goal

Add source-line breakpoint storage and line-to-IR mapping without stopping execution yet.

### Tasks

1. Define breakpoint identity:
   - source file;
   - 1-based source line number;
   - enabled flag.
2. Define the editor/storage policy: breakpoints are line-number based in v1 and do not track moved code across arbitrary source edits. Content-based breakpoint remapping is out of scope.
3. Build line-to-IR mapping after parsing/lowering.
4. Bind breakpoints to all IR instructions whose primary source span starts on that line.
5. Preserve unbound breakpoints for non-executable lines.
6. Support generated IR from high-level lowering by mapping generated instructions back to the high-level source line.
7. Add tests for:
   - executable line;
   - comment/blank line;
   - data declaration line;
   - multi-instruction source line if supported;
   - high-level lowered line when available.

### Acceptance Criteria

A breakpoint on the line containing `mov eax, 1` binds to the IR instruction for that `mov`. A breakpoint on a blank line remains stored but unbound.

### Rejected / out of scope

- Worker breakpoint commands.
- Stopping on breakpoints.
- Breakpoint gutter UI.

---

## 158. Phase 154 - Breakpoint Worker Protocol and Persistence

### Goal

Allow the UI to set, clear, enable, and disable source-line breakpoints through the worker protocol.

### Tasks

1. Add `DEBUG_SET_BREAKPOINTS` command.
2. Return breakpoint binding status in `DEBUG_STATE`.
3. Preserve breakpoints across Reset when source file and line still exist.
4. Mark breakpoints unbound after source changes when their lines no longer map to instructions.
5. Reject stale session/generation commands.
6. Add tests for setting, clearing, disabling, reset preservation, source edit remapping, and stale command rejection.

### Acceptance Criteria

Setting a breakpoint on an executable line returns bound breakpoint metadata with at least one IR instruction index.

### Rejected / out of scope

- Runtime stop behavior.
- UI gutter.

---

## 159. Phase 155 - Breakpoint Stop Semantics

### Goal

Make Run/Continue stop before executing breakpointed instructions.

### Tasks

1. Check enabled breakpoints before executing each instruction in Run/Continue.
2. Stop with `breakpoint` before executing a matched instruction.
3. Include breakpoint source file, line, instruction index, and original source text in the stop result.
4. Implement continue-from-same-breakpoint skip-once behavior to prevent immediate re-stop. The skip-once key must include `{sourceFile, lineNumber, instructionIndex, runGeneration}` so source edits and multi-instruction lines cannot skip the wrong breakpoint.
5. Ensure Step Into ignores breakpoint checks for the instruction being stepped.
6. Ensure Step Over honors breakpoints inside called procedures.
7. Add tests for:
   - stop before mutation;
   - continue from breakpoint executes the instruction once;
   - loop hits breakpoint again on later visits;
   - disabled breakpoint ignored;
   - unbound breakpoint ignored;
   - Step Into behavior;
   - Step Over inner breakpoint behavior.

### Acceptance Criteria

For:

```asm
.code
main PROC
    mov eax, 1
    mov ebx, 2
main ENDP
END main
```

with a breakpoint on the `mov ebx, 2` line, Continue stops before `EBX` changes.

### Rejected / out of scope

- Breakpoint gutter UI.
- Conditional breakpoints.
- Data watchpoints.

---

## 160. Phase 156 - Continue Backend Execution Loop

### Goal

Add backend Continue behavior using a chunked worker loop.

### Tasks

1. Add `DEBUG_CONTINUE` command.
2. Execute from current state until a stop condition occurs.
3. Use named chunk size constant `VM_DEBUG_CONTINUE_CHUNK_INSTRUCTIONS = 1024`.
4. Count each ordinary VM instruction as one unit. For repeated-string instructions that execute element-by-element, count each committed element as one unit.
5. Observe pause requests only between chunks, not in the middle of a single VM instruction. If a pause request arrives after normal completion, runtime fault, or input wait, preserve the completed stop reason instead of rewriting it to `pause-requested`.
6. Preserve deterministic instruction order and state.
5. Stop for:
   - program completion;
   - root return;
   - Irvine `exit`;
   - breakpoint;
   - waiting for input;
   - runtime error;
   - instruction limit;
   - time limit;
   - output limit.
6. Return command delta and updated debugger state.
8. Add tests for each stop reason already implementable in the codebase, including pause arriving after completion, pause arriving while waiting for input, pause arriving after a runtime fault, and pause observed before the next continue chunk begins.

### Acceptance Criteria

Continuing a three-instruction program with no breakpoints runs to completion and reports instruction-count delta `3`.

### Rejected / out of scope

- Pause command.
- Stop/hard termination.
- Step Over.

---

## 161. Phase 157 - Pause, Stop, and Reset Worker Control

### Goal

Add debugger control over running worker execution.

### Tasks

1. Add `DEBUG_PAUSE`, `DEBUG_STOP`, and `DEBUG_RESET` commands.
2. Implement cooperative pause between chunks.
3. Finish the currently executing instruction before pausing.
4. Return `pause-requested` with updated state.
5. Make pause a no-op when already paused, halted, faulted, or waiting for input.
6. Implement stop as run invalidation; hard-terminate worker if cooperative pause does not complete within named timeout.
7. Implement reset as rebuild from current source/settings.
8. Reset clears runtime output, runtime messages, pending input, last-step delta, and aggregate delta.
9. Reset preserves source breakpoints unless user clears them.
10. Add tests for pause during loop, pause no-op, stop invalidation, reset clearing runtime state, and stale submit after reset.

### Acceptance Criteria

Pausing a long-running loop stops between instructions, returns a current instruction index, and allows Continue to resume from that point.

### Rejected / out of scope

- UI buttons.
- Step Over.

---

## 162. Phase 158 - Continue and Pause UI

### Goal

Add browser controls for Continue, Pause, Stop, and Reset.

### Tasks

1. Add Continue button.
2. Add Pause button.
3. Add Stop button.
4. Add Reset button if not already present.
5. Disable controls based on debugger state.
6. Show stop reason after Continue/Pause/Stop.
7. Preserve Program Console and Simulator Messages separation.
8. Add UI tests for enabled/disabled states and stop-reason rendering.
9. Add manual browser verification after Wasm rebuild.

### Acceptance Criteria

During a long-running debug session, Pause becomes available while running; after pause, Continue becomes available and execution resumes from the paused instruction.

### Rejected / out of scope

- Breakpoint gutter.
- Step Over UI.

---

## 163. Phase 159 - Step Over Target Planning

### Goal

Identify whether Step Over should behave like Step Into or run through a user procedure call.

### Tasks

1. Add backend helper to classify the current instruction:
   - non-call instruction;
   - user direct CALL;
   - Irvine intrinsic;
   - selected virtual macro;
   - unsupported or indirect call.
2. For non-call instructions, plan Step Over as Step Into.
3. For user direct CALL, record expected return token and starting call depth.
4. For Irvine intrinsics and selected virtual macros, plan one logical instruction.
5. Reject indirect/unsupported calls if not implemented.
6. Add tests for each classification.

### Acceptance Criteria

At `call Helper`, Step Over planning records the return target after the call and the current call depth.

### Rejected / out of scope

- Executing Step Over.
- Aggregate delta display.

---

## 164. Phase 160 - Step Over Backend Execution

### Goal

Execute Step Over for user direct calls and non-call instructions.

### Tasks

1. Add `DEBUG_STEP_OVER` command.
2. If current instruction is not a user direct CALL, behave exactly like Step Into.
3. If current instruction is a user direct CALL, execute internally until the recorded return target is reached at or below the starting call depth.
4. Count every internal instruction.
5. Aggregate state changes across internal instructions.
6. Treat Irvine intrinsics and selected virtual macros as one logical instruction.
7. Add tests for non-call, simple call/ret, nested call, and Irvine intrinsic cases.

### Acceptance Criteria

For:

```asm
.code
main PROC
    call Helper
    mov ebx, eax
main ENDP
Helper PROC
    mov eax, 42
    ret
Helper ENDP
END main
```

Step Over at `call Helper` stops before `mov ebx, eax`, with `EAX = 42` and `EBX` unchanged.

### Rejected / out of scope

- UI aggregate display.
- Conditional step-over policies.

---

## 165. Phase 161 - Step Over Stop Conditions and Early Stops

### Goal

Complete Step Over stop behavior for breakpoints, waits, errors, resource limits, pause, and stop.

### Tasks

1. Honor breakpoints inside the stepped-over call.
2. Stop on input wait inside the callee.
3. Stop on runtime error inside the callee.
4. Stop on instruction/time/output/resource limits.
5. Stop on pause/stop request.
6. Report the actual stop reason, not `step-over-complete`, for early stops.
7. Preserve partial aggregate delta up to the stop.
8. Add tests for each stop reason.

### Acceptance Criteria

If a breakpoint exists inside the called procedure, Step Over stops at that breakpoint with `stopReason = breakpoint` and does not run to the return target.

### Rejected / out of scope

- UI rendering.

---

## 166. Phase 162 - Step Over Aggregate Delta Backend

### Goal

Produce backend aggregate deltas for Step Over.

### Tasks

1. Add aggregate delta schema with:
   - starting instruction;
   - ending instruction;
   - source metadata;
   - instructions executed;
   - nested calls entered;
   - stop reason;
   - register/flag before and after;
   - register/flag changes;
   - ordered memory changes;
   - Program Console output delta;
   - Simulator Messages delta;
   - stack before/after/peak.
2. Ensure aggregate deltas compare pre-command and post-command state.
3. Include partial effects on early stop.
4. Add tests for simple call, nested call, console output, memory change, breakpoint early stop, and runtime error early stop.

### Acceptance Criteria

Step Over of a procedure that writes memory and console output returns one aggregate delta containing both the memory change and the console output delta.

### Rejected / out of scope

- Browser aggregate rendering.

---

## 167. Phase 163 - Step Over Aggregate Delta UI

### Goal

Render Step Over aggregate deltas in the browser.

### Tasks

1. Add aggregate Step Over display or extend the Last Step panel.
2. Clearly label aggregate deltas as Step Over results.
3. Show instruction count executed inside the call.
4. Show aggregate register/flag/memory changes.
5. Show console and simulator-message deltas.
6. Show early-stop reason when Step Over stops before return target.
7. Add renderer tests for normal completion and early stop.
8. Add manual browser verification after Wasm rebuild.

### Acceptance Criteria

Step Over of a simple helper displays `Instructions executed inside call: N` and aggregate register changes from before the call to after return.

### Rejected / out of scope

- Call-stack panel.
- Timeline trace.

---

## 168. Phase 164 - Memory Change Row Model

### Goal

Create a richer UI-neutral memory-change row model without changing VM execution semantics.

### Tasks

1. Extend memory-change JSON/view model with:
   - address;
   - width;
   - old/new bytes;
   - formatted old/new values;
   - source instruction index;
   - source span;
   - optional symbol metadata;
   - optional field metadata;
   - unaligned marker;
   - section marker;
   - register-indirect marker.
2. Keep raw VM memory-change recording unchanged unless fields are already available.
3. Add tests for row generation from existing memory changes.
4. Add snapshot/formatter tests for each row kind.

### Acceptance Criteria

A direct symbol write produces a row with symbol name, address, width, old bytes, new bytes, and source instruction metadata.

### Rejected / out of scope

- DOM memory panel.
- Raw memory viewer.

---

## 169. Phase 165 - Symbol-Aware Memory Change Visualization

### Goal

Render symbol-aware and field-aware memory changes in the browser.

### Tasks

1. Group memory changes by symbol when possible.
2. Show byte offset from symbol.
3. Show element index when aligned to the element width.
4. Show field name and field offset when struct field metadata exists.
5. Show access width and old/new formatted value.
6. Add tests for direct symbol, symbol offset, array element, struct field, and multi-byte little-endian display.

### Acceptance Criteria

`mov nums[8], 100` displays symbol `nums`, byte offset `+8`, element index `2`, width `DWORD`, and old/new values.

### Rejected / out of scope

- Raw memory viewer.
- Data watchpoints.

---

## 170. Phase 166 - Raw Address, .DATA?, and .CONST Memory Visualization

### Goal

Render memory changes that do not map cleanly to ordinary writable `.data` symbols.

### Tasks

1. Show raw address for register-indirect accesses without symbol metadata.
2. Mark `.DATA?` origin when metadata is available.
3. Mark `.CONST` region metadata when available.
4. Mark unaligned accesses in memory UI while keeping warnings in Simulator Messages.
5. Show writes with no symbol metadata without crashing or hiding them.
6. Add tests for raw address, `.DATA?`, `.CONST`, unaligned write, and no-symbol cases.

### Acceptance Criteria

`mov DWORD PTR [eax], 5` where `EAX` points into writable data but has no symbol metadata displays the raw address and width.

### Rejected / out of scope

- Editable memory viewer.
- Watchpoints.

---

## 171. Phase 167 - URL Share State Schema and Encoder

### Goal

Encode source and deterministic settings into a shareable URL fragment.

### Tasks

1. Define schema version `1`.
2. Encode:
   - project name;
   - files;
   - active file;
   - execution mode;
   - entry point;
   - memory settings that affect deterministic execution;
   - execution limits;
   - console limits;
   - debugger breakpoints;
   - deterministic seeds required for reproducibility.
3. Exclude runtime state, registers, memory, Program Console output, Simulator Messages, pending input, local theme, editor preferences, scroll position, developer overrides, and super-extended grants.
4. Encode as canonical JSON with stable key order, then UTF-8 bytes, then raw DEFLATE using the project URL codec wrapper, then base64url without padding in a URL fragment. The first implementation uses a pako-compatible DEFLATE wrapper in browser/Node tests; the wrapper boundary is versioned so the implementation can change later without silently breaking old URLs.
5. Use shape:

   ```text
   #v=1&state=<base64url-compressed-json>
   ```
6. Add pure encode/decode unit tests.
7. Add tests proving excluded runtime state is not encoded.
8. Add canonicalization tests proving equivalent state encodes to identical URLs, with stable key ordering and no base64url padding.

### Acceptance Criteria

Encoding a simple project creates a URL fragment that decodes back to the same source, active file, execution settings, and breakpoints, without runtime output.

### Rejected / out of scope

- Import UI.
- Server-side short URLs.
- Automatic execution after import.

---

## 172. Phase 168 - URL Import Decoder and Safety Validation

### Goal

Decode shared URL fragments safely and apply source/settings only after validation.

### Tasks

1. Parse URL fragment with `URLSearchParams` or equivalent tested parser. The parser must strip exactly one leading `#`, reject duplicate `v` or `state` parameters, reject missing `state`, ignore unknown noncritical parameters only after validating `v` and `state`, and handle percent-encoding deterministically.
2. Decode base64url compressed state.
3. Validate schema before applying.
4. Validate file count, source size, project name length, and active file.
5. Validate execution mode, memory settings, execution limits, console limits, and deterministic seeds.
6. Reject unavailable memory modes or unsafe imported limits.
7. Ignore unknown top-level fields.
8. Reject unknown safety-mode enum values.
9. Do not execute imported programs automatically.
10. On import failure, show a user-facing import diagnostic and preserve the current project.
11. Apply decoded state atomically only after every validation step passes. No source, settings, active file, or breakpoint state may be partially applied after a late validation failure.
12. Add tests for valid import, leading `#`, missing `state`, duplicate `state`, duplicate `v`, malformed base64url, padded base64url acceptance/rejection per decoder policy, invalid compression, invalid JSON, unsupported schema, unknown mode, excessive source size, excessive file count, unsafe memory request, late validation failure, and current-project preservation after failure.

### Acceptance Criteria

Opening a valid share URL loads the source and settings but does not run the program. Opening a malformed share URL shows an import diagnostic and leaves the previous editor contents unchanged.

### Rejected / out of scope

- Backend share service.
- Runtime state restore.

---

## 173. Phase 169 - URL Size Guardrails and Share UI

### Goal

Add user-facing share UI with deterministic size limits and clear diagnostics.

### Tasks

1. Add Share button or equivalent UI action.
2. Generate the URL fragment using the encoder from Phase 167 - URL Share State Schema and Encoder.
3. Enforce named constant `MAX_SHARE_URL_LENGTH = 8000` characters for the full generated URL string, not only the fragment.
4. If the generated URL exceeds the limit, refuse to copy/share and show a diagnostic explaining that the source/settings are too large for URL sharing.
5. Show copied/generated URL only after successful validation.
6. Add UI tests for successful share, too-large share, malformed import message, and deterministic seed preservation.
7. Add manual browser verification after Wasm rebuild.

### Acceptance Criteria

A small project can be shared and reopened with the same source, settings, breakpoints, and deterministic seed. A too-large project produces a clear share-size diagnostic and no broken URL.

### Rejected / out of scope

- Server-shortened links.
- Cloud storage.
- Account-based persistence.
- Sharing runtime output or VM state.


---

## 174. Phase 170 - Debugger Command State Matrix and Error Rendering

### Goal

Freeze the debugger command/state transition table and rendered error-message contract before adding more UI polish.

### Tasks

1. Define allowed commands for each state:
   - `not-loaded`: only debug load/import-safe commands are accepted.
   - `ready`: Step Into, Continue, Step Over, Reset, Stop, breakpoint edits.
   - `running`: Pause and Stop only, plus idempotent state queries.
   - `paused`: Step Into, Continue, Step Over, Reset, Stop, breakpoint edits.
   - `stopped-at-breakpoint`: Step Into, Continue, Step Over, Reset, Stop, breakpoint edits.
   - `waiting-for-input`: input submit/cancel, Reset, Stop, breakpoint edits, and state queries; Step/Continue/Step Over return `debugger-waiting-for-input`.
   - `halted`: Reset, Stop, breakpoint edits, state queries.
   - `faulted`: Reset, Stop, breakpoint edits, state queries.
   - `terminated`: no VM commands; rebuild/load is required.
2. Define all debugger protocol errors as structured diagnostics with stable codes and rendered Simulator Messages text.
3. Add Node formatter tests for every new debugger error code.
4. Add worker tests for every illegal state/command pair in the table.

### Acceptance Criteria

A `DEBUG_STEP_INTO` command sent while `stateKind = waiting-for-input` returns `debugger-waiting-for-input`, preserves VM state, and renders a stable Simulator Messages line.

### Rejected / out of scope

- New execution behavior.
- UI controls beyond error rendering.

---

## 175. Phase 171 - Source Offset Mapping and Current-Instruction Highlight Contract

### Goal

Add the tested mapping layer between backend byte spans and editor positions, and expose backend current-instruction highlight metadata.

### Tasks

1. Implement a source-offset mapper that converts UTF-8 byte offsets and byte span lengths into CodeMirror-compatible UTF-16 document offsets.
2. Preserve backend byte offsets as authoritative; do not change parser diagnostics to UTF-16 offsets.
3. Return nullable UI highlight spans when a backend span cannot be mapped, instead of guessing.
4. Add current-instruction highlight metadata:
   - source file;
   - source line;
   - byte offset/span;
   - mapped editor start/end offsets;
   - instruction index;
   - stop reason.
5. Add tests for ASCII source, non-ASCII in comments, non-ASCII in strings, CRLF input source, invalid byte span, multi-byte UTF-8 before the highlighted instruction, and one-past-end/no-current-instruction states.

### Acceptance Criteria

A diagnostic or current-instruction span after a non-ASCII comment maps to the correct CodeMirror UTF-16 offset range, while backend byte offsets remain unchanged.

### Rejected / out of scope

- Full source editor replacement.
- Syntax highlighting.
- Content-based breakpoint remapping.

---

## 176. Phase 172 - Current Instruction Highlighting UI

### Goal

Render the current instruction line/span in the editor using backend-provided mapping data.

### Tasks

1. Add UI rendering for current instruction highlight in debug states where an instruction is selected.
2. Clear the highlight for `not-loaded`, `terminated`, and states with no current instruction.
3. Preserve highlight during `stopped-at-breakpoint`, `paused`, and `waiting-for-input` when the backend provides a span.
4. Never infer a current instruction from UI cursor position.
5. Add UI tests for Step Into, Continue breakpoint stop, runtime fault, waiting-for-input, normal completion, reset, and source reload.

### Acceptance Criteria

After Step Into stops before the second instruction, the editor highlights the second instruction's source span using backend metadata.

### Rejected / out of scope

- Breakpoint gutter controls.
- Syntax highlighting.
- Editable memory/debug overlays.

---

## 177. Phase 173 - Breakpoint Entry, List, and Gutter UI

### Goal

Provide a first user-facing way to manage source-line breakpoints.

### Tasks

1. Add a breakpoint list or line-number input UI first, with an optional CodeMirror gutter if already practical.
2. If a gutter is implemented, it must use CodeMirror state/effects and must not mutate backend state directly.
3. Send all breakpoint changes through `DEBUG_SET_BREAKPOINTS`.
4. Display bound/unbound/disabled breakpoint state returned by the backend.
5. Preserve line-number-based behavior across Reset.
6. On source edits, keep breakpoints line-number based and mark backend binding status after rebuild; do not attempt content-based remapping.
7. Add UI tests for adding, removing, disabling, enabling, unbound display, reset preservation, source edit line deletion, and stale backend rejection.

### Acceptance Criteria

A user can set a breakpoint on line 4, Continue stops before executing the bound instruction, and the UI shows that breakpoint as bound and hit.

### Rejected / out of scope

- Conditional breakpoints.
- Watchpoints.
- Content-aware breakpoint relocation.

---

## 178. Phase 174 - URL Canonicalization, Compression Codec, and Base64url Rules

### Goal

Freeze the share-URL codec so generated URLs are deterministic and imports are interoperable.

### Tasks

1. Implement canonical state serialization with stable key order.
2. Encode canonical JSON as UTF-8 bytes.
3. Compress using the project URL codec wrapper, initially backed by raw DEFLATE compatible with pako test vectors.
4. Encode compressed bytes as canonical base64url without `=` padding.
5. Decoder must accept canonical no-padding base64url; padded input may be accepted only if tests prove it canonicalizes back to no-padding on re-encode.
6. Define semantic equality and byte-identical canonical URL equality tests.
7. Add privacy exclusion tests proving runtime output, diagnostics, memory, registers, pending input, theme, scroll position, and editor preferences are absent.

### Acceptance Criteria

Two semantically identical share states with differently ordered object keys produce the same URL string, and decoding then re-encoding a valid URL returns the canonical URL.

### Rejected / out of scope

- Server-side short links.
- Cloud persistence.
- Runtime-state restore.

---

## 179. Phase 175 - Browser Debugger Integration Smoke Harness

### Goal

Validate that the debugger backend, worker protocol, UI rendering, source mapping, breakpoint handling, command controls, input wait state, and share URL codec work together through the browser boundary.

This phase adds no new simulator behavior. It is an integration validation phase.

### Tasks

1. Add a browser or headless-browser integration smoke harness that uses the real worker boundary.
2. Load a small MASM source program through the UI/worker path.
3. Step Into once and verify register snapshot, current instruction index, and current-source highlight.
4. Set a breakpoint through the user-facing breakpoint UI.
5. Continue to the breakpoint and verify the stop reason, bound breakpoint id, and source highlight.
6. Continue from the same breakpoint and verify skip-once behavior.
7. Run a long-loop program, request Pause, and verify deterministic paused state at a chunk boundary.
8. Enter `waiting-for-input`, verify disabled Step Into / Step Over / Continue controls, submit input, and continue execution.
9. Reset and verify previous VM state, current highlight, pending input request, and stale commands are invalidated.
10. Generate a share URL and import it into a fresh session.
11. Reject a stale command from the old session after import or reset.
12. Verify all rendered debugger/import errors use the real Simulator Messages formatter.

### Acceptance Criteria

The smoke harness exercises Step Into, Continue, Pause, breakpoint hit, breakpoint skip-once, waiting-for-input, input submit, Reset, share export, share import, and stale-session rejection without direct access to private backend state.

### Required tests

- Browser/worker smoke test for the full flow listed above.
- Stale-session rejection after Reset.
- Stale-session rejection after hard worker replacement.
- Source highlight survives Step Into and breakpoint stop.
- Waiting-for-input disables Step Into, Step Over, and Continue.
- Share import is atomic in the browser UI.

### Rejected / out of scope

- Full end-to-end test coverage for every debugger feature.
- Visual screenshot regression testing.
- Performance benchmarking.
- Cloud share links.

## 180. Phase 176 - Simulator Settings Schema and Persistence Boundaries

### Goal

Create a typed browser-side simulator settings schema and persistence boundary before adding memory/settings UI controls.

This is a UI/state phase. It must not change VM execution semantics.

### Accepted state categories

Implement explicit state categories:

```text
share-safe project settings
local-only settings
transient runtime state
```

Phase 53E diagnostic settings compatibility note:

Phase 53E - Memory Validation and Teaching Diagnostic UI Settings initially treats diagnostic UI settings as local page-session preferences. Before this phase promotes any diagnostic setting into share-safe project state, it must explicitly reclassify that setting and add tests for the new persistence behavior.

This phase must not assume that every existing browser diagnostic setting is automatically share-safe merely because it can affect execution diagnostics. It must classify each setting independently.

At minimum, classify these settings:

- memory range validation;
- uninitialized-read diagnostics;
- undefined-flag-use diagnostics;
- compatibility-notice visibility;
- Diagnostic settings panel collapsed/expanded state.

The panel presentation state should remain local-only unless a later UI preferences phase deliberately persists it. Diagnostic behavior settings may become share-safe only if this phase documents the compatibility reason, defaulting behavior, import validation, and tests.

Diagnostic settings may affect whether teaching diagnostics are emitted as notices, warnings, or strict stops. They must not be described as changing MASM language semantics or adding/removing supported syntax.

Share-safe project settings include:

- source text;
- selected execution mode;
- memory size preset when share-safe;
- memory layout mode when deterministic or seeded;
- memory validation mode;
- explicit seed for seeded randomized layout;
- selected example ID when the field is defined by this phase.

Local-only settings include:

- super-extended memory enablement;
- local confirmation acknowledgements;
- UI-only preferences not yet included in share state;
- browser capability flags.

Transient runtime state includes:

- registers;
- flags;
- memory images;
- Program Console output;
- Simulator Messages;
- debugger position;
- last-step deltas;
- pending input IDs;
- worker session IDs;
- generated fresh-random seeds unless explicitly converted to share-safe seeded state.

### Rejected behavior

Do not serialize transient runtime state into local settings or share URLs.

Do not save super-extended memory enablement into share state.

Do not make localStorage availability required for running the simulator.

Do not modify C99 core settings APIs in this phase except for consuming already existing settings types when the corresponding feature is implemented.

### Diagnostics

Add UI/settings diagnostics:

```text
settings-load-failed
settings-save-failed
settings-schema-invalid
settings-share-unsafe
```

These diagnostics must render in Simulator Messages through the same formatter as other UI/worker diagnostics.

### Tests

Add tests for:

- default settings object creation;
- migration from missing settings;
- unknown setting key rejection or safe ignore according to the schema;
- local-only fields excluded from share-safe state;
- transient runtime fields excluded from settings persistence;
- localStorage unavailable or quota failure produces a rendered diagnostic but does not block Run;
- settings JSON round-trip for the share-safe subset;
- schema validation failure leaves the previous settings active.

---

## 181. Phase 177 - Settings Versioning and Migration

### Goal

Add explicit simulator settings schema versioning and migration before settings are persisted or shared.

### Tasks

- Add `settingsSchemaVersion` to share-safe simulator settings.
- Migrate missing-version settings to the current version when fields are unambiguous.
- Reject unsupported future versions atomically with `settings-version-unsupported`.
- Preserve local preference state separately from share-safe simulator settings.

### Acceptance criteria

Migration, unsupported-version rejection, and default fallback are covered by unit tests and rendered diagnostics where user-visible.

## 182. Phase 178 - Memory Size Preset UI

### Goal

Expose implemented memory-size presets through the UI without adding new core memory behavior.

### Preconditions

Core memory sizing and safety-tier enforcement must already exist.

### Accepted presets

Expose normal presets:

```text
16 MiB
64 MiB
128 MiB
```

Expose extended presets only if the corresponding tier is implemented:

```text
256 MiB
512 MiB
1 GiB
```

Each preset must display:

- total requested virtual memory;
- safety tier;
- whether reload/reinitialization is required;
- whether the setting is share-safe.

### Rejected behavior

Do not expose arbitrary custom size text fields in this phase.

Do not expose super-extended values in this phase.

Do not silently accept a preset unsupported by the current browser/core capability.

### Diagnostics

```text
memory-preset-unavailable
memory-preset-exceeds-tier
settings-reinitialization-required
```

### Tests

Add UI/unit tests for:

- selecting each normal preset;
- selecting each extended preset when enabled;
- disabled extended presets when the tier is unavailable;
- share-state encoding and decoding for share-safe presets;
- rendered diagnostics when a preset exceeds the active safety tier;
- no VM mutation until the setting is applied.

---

## 183. Phase 179 - Memory Settings Apply and VM Reinitialization

### Goal

Apply validated memory settings atomically and reinitialize VM state predictably.

### Behavior

When memory settings change:

1. Validate the new settings object.
2. If valid, discard the current VM runtime state.
3. Rebuild parser/loader/VM state on the next Run or Debug command.
4. Clear stale debugger session state.
5. Preserve editor source text and share-safe settings.
6. Show a rendered informational Simulator Message when a running/paused VM is invalidated.

### Rejected behavior

Do not partially apply invalid settings.

Do not keep stale memory images, register state, breakpoints bound to old instruction indices, pending input IDs, or last-step deltas after memory settings are applied.

Do not rerun the program automatically unless the user explicitly invokes Run.

### Diagnostics

```text
settings-apply-failed
vm-state-invalidated
settings-requires-rerun
```

### Tests

Add tests for:

- atomic apply success;
- atomic apply failure leaves old settings active;
- running VM invalidated by memory settings apply;
- paused debugger state invalidated;
- pending input request invalidated;
- breakpoints become unbound until source is reloaded;
- rendered Simulator Messages for invalidation and apply failure.

---

## 184. Phase 180 - Memory Layout Mode UI

### Goal

Expose implemented memory layout modes through UI controls.

This phase must not implement the core layout algorithms.

### Accepted layout modes

```text
fixed-educational
automatic-deterministic
seeded-randomized
fresh-randomized
```

### Behavior

- Fixed educational layout is the default.
- Automatic deterministic layout is share-safe.
- Seeded randomized layout requires a visible seed input or generated seed field and is share-safe only when the seed is explicit.
- Fresh randomized layout generates a new seed per run and displays the generated seed after each run.
- Copy/share from fresh randomized mode must convert the most recent generated seed to seeded-randomized mode. If no generated seed exists yet, share export must reject with `fresh-randomized-not-reproducible`.

### Rejected behavior

Do not silently share non-reproducible fresh-randomized state.

Do not allow layout modes whose core implementation is absent.

Do not change source code or MASM semantics when changing layout mode.

### Diagnostics

```text
memory-layout-mode-unavailable
invalid-layout-seed
fresh-randomized-not-reproducible
layout-seed-generated
```

### Tests

Add tests for:

- fixed layout default;
- automatic deterministic selection;
- seeded randomized selection with explicit seed;
- invalid seed rejection;
- fresh randomized warning text;
- fresh randomized seed generated after Run;
- sharing fresh randomized before first run fails;
- sharing fresh randomized after run converts to seeded randomized;
- imported seeded randomized URL reproduces region bases.

---

## 185. Phase 181 - Memory Validation Mode UI

### Goal

Expose implemented memory validation modes through UI controls.

### Accepted validation modes

```text
region-only
allocated-object-warnings
allocated-object-strict
uninitialized-read-warnings
uninitialized-read-strict
```

Only show a mode if the core supports it.

### Rejected behavior

Do not expose provenance warnings or provenance strict modes unless a provenance core model exists.

Do not label warning modes as strict or strict modes as warnings.

Do not convert a strict-mode runtime error into a warning in the UI.

### Diagnostics

```text
memory-validation-mode-unavailable
memory-validation-mode-conflict
```

### Tests

Add tests for:

- selecting each implemented validation mode;
- hiding unsupported validation modes;
- URL/share round-trip for validation mode;
- strict mode remains strict after import;
- warning mode emits warnings through Simulator Messages;
- validation-mode changes invalidate VM state through Phase 83 apply flow.

---

## 186. Phase 182 - Memory Safety Preset Buttons

### Goal

Add preset buttons that set multiple memory settings deterministically.

### Presets

```text
Beginner/default
Debug
Robustness
```

Required mappings:

Beginner/default:

```text
layout = fixed-educational
validation = region-only
memory tier = normal
size preset = 64 MiB
```

Debug:

```text
layout = automatic-deterministic
validation = allocated-object-warnings + uninitialized-read-warnings; disable this preset with memory-preset-unavailable until both modes exist
memory tier = normal
size preset = 64 MiB
```

Robustness:

```text
layout = seeded-randomized
seed = generated explicit seed if absent
validation = allocated-object-strict + uninitialized-read-strict; disable this preset with memory-preset-unavailable until both modes exist
memory tier = normal or extended only
size preset = 128 MiB
```

If a referenced validation mode is not implemented, the preset must use the strongest implemented lower mode and show a non-blocking `preset-partially-applied` diagnostic.

### Rejected behavior

Do not set super-extended memory from any preset.

Do not silently omit seeded randomization from Robustness.

Do not make presets browser-dependent.

### Diagnostics

```text
preset-partially-applied
preset-unavailable
```

### Tests

Add tests for:

- each preset produces exact settings;
- Robustness generates and displays a seed;
- unavailable strict modes downgrade with diagnostic;
- presets are share-safe;
- preset URL round-trip preserves deterministic behavior;
- applying a preset invalidates VM state through Phase 83.

---

## 187. Phase 183 - Extended Memory Tier UI

### Goal

Expose the implemented extended memory tier without enabling super-extended mode.

### Behavior

Extended memory tier allows these presets:

```text
256 MiB
512 MiB
1 GiB
```

The UI must show:

- larger memory warning;
- estimated requested virtual memory;
- browser-local capability notice;
- whether the selected preset is share-safe.

### Rejected behavior

Do not expose 4 GiB/super-extended settings in this phase.

Do not allocate memory on preset selection; allocation/preflight occurs at Run/load time.

Do not block the main thread while estimating memory use.

### Diagnostics

```text
extended-memory-tier-disabled
extended-memory-preset-too-large
```

### Tests

Add tests for:

- extended tier enablement;
- selecting 256 MiB, 512 MiB, and 1 GiB;
- selected preset persists as a share-safe setting;
- disabling extended tier downgrades or rejects incompatible presets deterministically;
- memory setting apply invalidates stale VM state.

---

## 188. Phase 184 - Browser Memory Capability Probe

### Goal

Probe browser/worker memory capability before enabling extended or super-extended memory settings.

### Tasks

- Distinguish configured VM memory, reserved address range, committed backing-store bytes, WebAssembly linear-memory pages, and browser allocation failure.
- Run probe work in the worker so the main thread remains responsive.
- Treat worker hard termination during probe as invalidating that worker's VM state.

### Acceptance criteria

Capability probe success/failure is deterministic in tests, does not allocate full super-extended memory by default, and renders clear diagnostics.

## 189. Phase 185 - Super-Extended Memory Local Confirmation

### Goal

Add local-only confirmation for super-extended memory mode.

### Behavior

Super-extended mode permits requests up to:

```text
4 GiB virtual memory request limit
```

It is local-only and never share-safe.

Required confirmation text:

```text
Super-extended memory mode allows projects to request very large simulated memory regions.

This may slow down, freeze, or crash this browser tab if the program touches too much memory. This setting is local to this browser session and is not saved into shared project links.

Enable super-extended memory mode?
```

Required checkbox:

```text
[ ] I understand that this may freeze or crash the tab.
```

### Rejected behavior

Do not enable super-extended mode without checkbox confirmation.

Do not serialize super-extended mode into local share URLs.

Do not import super-extended mode from a share URL.

Do not show super-extended mode in examples, tutorials, screenshots, or default presets.

### Diagnostics

```text
super-extended-confirmation-required
super-extended-local-only
super-extended-import-rejected
```

### Tests

Add tests for:

- confirmation modal text;
- checkbox required;
- enable after confirmation;
- local-only storage classification;
- share export excludes super-extended mode;
- share import rejects or downgrades super-extended state according to exact implementation policy;
- rendered warning diagnostics.

---

## 190. Phase 186 - Large Allocation Preflight and Responsiveness Safeguards

### Goal

Prevent large memory settings from freezing the browser and provide safe diagnostics.

### Behavior

Before loading/running a project, preflight must compute:

- requested `.code`, `.data`, `.DATA?`, `.CONST`, heap, and stack sizes;
- total requested virtual memory;
- active tier limit;
- whether the request requires extended or super-extended confirmation.

Large initialization must happen in the worker or be chunked. The main thread must remain responsive.

### Rejected behavior

Do not allocate backing arrays for rejected configurations.

Do not block the main thread during large memory initialization.

Do not silently expand memory beyond tier limits.

### Diagnostics

```text
large-allocation-preflight-failed
large-allocation-confirmation-required
memory-request-exceeds-tier
memory-initialization-cancelled
```

### Tests

Add tests for:

- preflight rejects over-tier request before allocation;
- preflight requires second run confirmation for super-extended projects;
- cancellation during initialization;
- worker remains responsive to cancel/terminate while initializing;
- no stale partial VM after failed preflight;
- rendered diagnostics include requested size and active tier.

---

## 191. Phase 187 - Execution Mode Selector and Mode-Gated Diagnostics

### Goal

Add an explicit execution mode selector before implementing Extended 32-bit behavior.

### Modes

```text
MASM32 Educational Mode
Extended 32-bit Mode
```

### Behavior

- MASM32 Educational Mode remains default.
- Extended 32-bit Mode is opt-in.
- Mode is share-safe.
- Mode changes invalidate VM state.
- The UI must state that Extended 32-bit Mode is not x64 MASM, not ml64, and not Windows x64 ABI behavior.

### Rejected behavior

Do not call Extended 32-bit Mode `x64`, `ml64`, or `64-bit Windows`.

Do not enable 64-bit registers or executable QWORD operations in MASM32 mode.

Do not implement x64 calling convention or OS behavior.

### Diagnostics

```text
unsupported-in-masm32-mode
unsupported-in-extended-32-mode
not-x64-masm
mode-change-invalidated-vm
```

### Tests

Add tests for:

- default mode is MASM32 Educational Mode;
- selecting Extended 32-bit Mode;
- mode URL/share round-trip;
- mode change invalidates VM state;
- QWORD executable operation still rejected until its dedicated phase;
- RAX rejected until the extended register phase;
- UI label text uses `Extended 32-bit Mode` exactly;
- no x64/ml64 labels appear.

---

## 192. Phase 188 - Extended 32-bit Address-Size Policy and Mode Boundary Matrix

### Goal

Freeze the address-size and non-x64 boundaries for Extended 32-bit Mode.

### Contract

- Extended 32-bit Mode adds selected 64-bit data/register/instruction behavior but is not x64 MASM or `ml64`.
- VM effective addresses remain 32-bit addresses.
- Any computed effective address above `0xFFFFFFFF` reports `address-size-exceeded` unless a later phase explicitly introduces a 64-bit VM address space.
- `RIP` is display/debug metadata only and not a source-level executable register.
- `RFLAGS` stores only modeled flags; unmodeled bits remain zero.

### Required tests

- 64-bit base register with low 32-bit valid address and high bits zero.
- Address over `0xFFFFFFFF` rejected.
- x64/ML64-only constructs rejected with stable diagnostics.

## 193. Phase 189 - Extended 32-bit Register File: Existing GPR 64-bit Parents

### Goal

Add 64-bit parent registers for existing MASM32 general-purpose registers in Extended 32-bit Mode.

### Accepted registers

```text
RAX RBX RCX RDX RSI RDI RBP RSP RIP RFLAGS
```

Aliases:

```text
RAX -> EAX -> AX -> AH/AL
RBX -> EBX -> BX -> BH/BL
RCX -> ECX -> CX -> CH/CL
RDX -> EDX -> DX -> DH/DL
RSI -> ESI -> SI
RDI -> EDI -> DI
RBP -> EBP -> BP
RSP -> ESP -> SP
```

### Behavior

- Register storage is 64-bit in Extended 32-bit Mode.
- Writing a 32-bit alias zero-extends into the parent 64-bit register.
- Writing a 16-bit or 8-bit alias updates only that portion and preserves other bits.
- MASM32 Educational Mode register behavior is unchanged.

### Rejected behavior

Do not implement R8-R15 in this phase.

Do not enable 64-bit memory operations in this phase.

Do not change MASM32 mode storage width.

### Diagnostics

```text
unsupported-register-in-mode
register-width-unavailable
```

### Tests

Add tests for:

- writing RAX and reading EAX/AX/AH/AL;
- writing EAX zero-extends RAX;
- writing AX preserves high RAX bits;
- writing AH/AL preserves surrounding bits;
- same behavior for RBX, RCX, and RDX;
- RSI/RDI/RBP/RSP 16/32/64 alias behavior;
- RAX rejected in MASM32 mode;
- R8 rejected with `unsupported-register-in-mode` until Phase 92.

---

## 194. Phase 190 - Extended 32-bit R8-R15 Register Group

### Goal

Add the optional advanced 64-bit register group separately from low-register parents.

### Accepted registers

```text
R8 R9 R10 R11 R12 R13 R14 R15
R8D R9D R10D R11D R12D R13D R14D R15D
R8W R9W R10W R11W R12W R13W R14W R15W
R8B R9B R10B R11B R12B R13B R14B R15B
```

### Behavior

- Available only in Extended 32-bit Mode.
- 32-bit writes zero-extend into the 64-bit parent.
- 16-bit and 8-bit writes preserve high bits.

### Rejected behavior

Do not implement x64 calling convention or shadow space.

Do not imply ml64 compatibility.

Do not add high-byte aliases such as `R8H`.

### Diagnostics

```text
unsupported-register-in-mode
invalid-register-alias
```

### Tests

Add tests for:

- R8/R8D/R8W/R8B alias behavior;
- R15/R15D/R15W/R15B alias behavior;
- 32-bit zero-extension;
- 8/16-bit partial write preservation;
- R8 rejected in MASM32 mode;
- invalid aliases rejected.

---

## 195. Phase 191 - Extended Register Display, Serialization, and Debugger Snapshots

### Goal

Expose Extended 32-bit registers consistently in backend JSON and UI/debugger state.

### Behavior

- Register snapshots include 64-bit canonical values in Extended 32-bit Mode.
- Aliases are derived for display only.
- MASM32 mode register JSON remains backward compatible.
- Debugger panels clearly label Extended 32-bit Mode.
- Values display as 16-hex-digit uppercase values for 64-bit registers.

### Rejected behavior

Do not display unsupported registers in MASM32 mode.

Do not serialize aliases as independent mutable state.

### Tests

Add tests for:

- source-run JSON in MASM32 mode unchanged;
- source-run JSON in Extended mode includes 64-bit registers;
- debugger snapshot includes mode label;
- UI displays 64-bit hex width;
- aliases reflect canonical values;
- no duplicate mutable alias state.

---

## 196. Phase 192 - Extended 64-bit Immediate/Register MOV

### Goal

Allow 64-bit immediate/register `mov` forms in Extended 32-bit Mode.

### Accepted forms

```asm
mov r64, imm64
mov r64, r64
mov r64, r32/r16/r8 only if existing width rules permit explicit alias write/read
mov r32/r16/r8, r64 only when destination width is explicit alias and truncation rules are already supported for register writes
```

For the first implementation, accepted core forms are:

```asm
mov r64, imm64
mov r64, r64
```

### Behavior

- 64-bit immediates are parsed and range-checked as unsigned 0..0FFFFFFFFFFFFFFFFh or negative signed literals that fit 64-bit two's complement.
- 32-bit writes to extended-mode aliases zero-extend according to Phase 189 - Extended 32-bit Register File: Existing GPR 64-bit Parents and Phase 190 - Extended 32-bit R8-R15 Register Group.
- `mov` does not change flags.

### Rejected behavior

Do not accept 64-bit `mov` in MASM32 mode.

Do not accept memory operands in this phase.

Do not silently truncate out-of-range immediates.

### Diagnostics

```text
unsupported-in-masm32-mode
immediate-out-of-range
operand-width-mismatch
unsupported-extended-memory-operand
```

### Tests

Add tests for:

- `mov rax, 123456789ABCDEF0h`;
- `mov rbx, -1` stores `FFFFFFFFFFFFFFFFh`;
- out-of-range immediate rejected;
- `mov rax, rbx`;
- flags preserved;
- same source rejected in MASM32 mode;
- Memory forms remain rejected until Phase 193 - Executable QWORD/SQWORD MOV Memory Access.

---

## 197. Phase 193 - Executable QWORD/SQWORD MOV Memory Access

### Goal

Enable QWORD/SQWORD executable memory access for `mov` only in Extended 32-bit Mode.

### Accepted forms

```asm
mov r64, qwordSymbol
mov qwordSymbol, r64
mov r64, QWORD PTR [reg]
mov QWORD PTR [reg], r64
mov r64, SQWORD PTR [reg]
mov SQWORD PTR [reg], r64
mov QWORD PTR symbol[offset], imm64
```

### Behavior

- All memory access goes through checked VM memory helpers.
- `.CONST` writes still fail by address range.
- QWORD/SQWORD declarations keep existing metadata.
- Signedness affects initializer validation and display metadata, not raw `mov` bit transfer.

### Rejected behavior

Do not enable QWORD arithmetic in this phase.

Do not enable memory-to-memory `mov`.

Do not enable QWORD/SQWORD executable memory access in MASM32 mode.

### Diagnostics

```text
unsupported-in-masm32-mode
unsupported-memory-width-in-mode
const-write
invalid-address
ambiguous-memory-width
operand-width-mismatch
```

### Tests

Add tests for:

- load QWORD symbol into RAX;
- store RAX into QWORD symbol;
- QWORD PTR register-indirect load/store;
- SQWORD PTR raw-bit load/store;
- immediate QWORD store boundary values;
- `.CONST` QWORD write failure;
- invalid address failure;
- MASM32 mode rejection;
- no memory mutation on failed writes.

---

## 198. Phase 194 - Extended 64-bit ADD, SUB, and CMP

### Goal

Add 64-bit arithmetic compare/add/sub behavior in Extended 32-bit Mode.

### Accepted forms

```asm
add r64, r64|imm64|qword memory
sub r64, r64|imm64|qword memory
cmp r64, r64|imm64|qword memory
add qword memory, r64|imm64
sub qword memory, r64|imm64
cmp qword memory, r64|imm64
```

### Behavior

- Results are masked to 64 bits.
- `add`/`sub` update `CF`, `ZF`, `SF`, `OF`, `PF`, and `AF` using 64-bit width.
- `cmp` updates flags like `sub` and writes no destination.
- `PF` still uses parity of the low byte.
- `AF` uses bit 3 carry/borrow behavior.

### Rejected behavior

Do not enable ADC/SBB in this phase.

Do not enable memory-to-memory forms.

Do not enable 64-bit arithmetic in MASM32 mode.

### Diagnostics

```text
unsupported-in-masm32-mode
operand-width-mismatch
ambiguous-memory-width
unsupported-memory-width-in-mode
```

### Tests

Add tests for:

- 64-bit add carry from `FFFFFFFFFFFFFFFFh + 1`;
- signed overflow from `7FFFFFFFFFFFFFFFh + 1`;
- subtraction borrow;
- zero result and sign result;
- PF low-byte parity;
- AF nibble carry/borrow;
- CMP no writeback;
- memory destination/source cases;
- `.CONST` destination rejection;
- MASM32 rejection.

---

## 199. Phase 195 - Extended 64-bit INC, DEC, and NEG

### Goal

Add 64-bit unary arithmetic operations in Extended 32-bit Mode.

### Accepted forms

```asm
inc r64|qword memory
dec r64|qword memory
neg r64|qword memory
```

### Behavior

- `inc` and `dec` preserve `CF` and update other modeled result flags at 64-bit width.
- `neg` computes `0 - operand` at 64-bit width and updates modeled subtraction flags.
- Memory read-modify-write operations validate before mutation.

### Rejected behavior

Do not enable MASM32 executable QWORD unary operations.

Do not accept ambiguous memory width.

### Tests

Add tests for:

- `inc rax` from `7FFFFFFFFFFFFFFFh`;
- `dec rax` from `8000000000000000h`;
- `CF` preservation for inc/dec;
- `neg rax` for zero, one, and `8000000000000000h`;
- memory operand cases;
- `.CONST` rejection;
- no mutation on failed memory validation.

---

## 200. Phase 196 - Extended 64-bit ADC and SBB

### Goal

Add carry-aware 64-bit arithmetic in Extended 32-bit Mode.

### Accepted forms

```asm
adc r64, r64|imm64|qword memory
adc qword memory, r64|imm64
sbb r64, r64|imm64|qword memory
sbb qword memory, r64|imm64
```

### Behavior

- Uses current `CF` as input.
- Updates `CF`, `ZF`, `SF`, `OF`, `PF`, and `AF` at 64-bit width.
- Memory read-modify-write operations validate before mutation.

### Rejected behavior

Do not enable in MASM32 mode.

Do not accept memory-to-memory forms.

### Tests

Add tests for:

- `adc` with `CF=0` and `CF=1`;
- `sbb` with `CF=0` and `CF=1`;
- carry out of bit 63;
- signed overflow;
- borrow;
- memory destination/source;
- `.CONST` destination rejection;
- no partial mutation on failed memory validation.

---

## 201. Phase 197 - Extended 64-bit Logical and TEST Instructions

### Goal

Add 64-bit logical operations in Extended 32-bit Mode.

### Accepted forms

```asm
and r64, r64|imm64|qword memory
or  r64, r64|imm64|qword memory
xor r64, r64|imm64|qword memory
test r64, r64|imm64|qword memory
not r64|qword memory
```

### Behavior

- `and`, `or`, `xor`, and `test` update `ZF`, `SF`, and `PF`; clear `CF` and `OF`; and use the simulator's deterministic AF policy.
- `test` writes no destination.
- `not` writes the bitwise complement and preserves all modeled flags.
- Width is 64 bits for r64/qword operands.

### Rejected behavior

Do not accept memory-to-memory forms.

Do not accept 64-bit logical instructions in MASM32 mode.

Do not accept ambiguous memory/immediate forms.

### Tests

Add tests for:

- each logical instruction with 64-bit registers;
- memory source and destination where applicable;
- low-byte PF behavior;
- `test` no writeback;
- `not` flag preservation;
- ambiguous memory width rejection;
- `.CONST` write rejection;
- MASM32 rejection.

---

## 202. Phase 198 - Extended Mode Boundary Regression Suite

### Goal

Prevent Extended 32-bit Mode from drifting into full x64/ml64 behavior.

This is a test/documentation phase, not a feature phase.

### Required rejected examples

```asm
.code
main PROC
    ; Windows x64 ABI assumptions remain unsupported
    sub rsp, 20h
main ENDP
END main
```

```asm
.code
main PROC
    movaps xmm0, xmm1
main ENDP
END main
```

```asm
.code
main PROC
    call QWORD PTR [rax]
main ENDP
END main
```

Other rejected families:

- SIMD/XMM/YMM/ZMM registers;
- x87/FPU expansion if not separately implemented;
- Windows x64 shadow space/calling convention semantics;
- ml64-specific directives not explicitly accepted;
- PE/COFF linking behavior;
- external imports.

### Tests

Add tests for:

- MASM32 examples still behave identically in default mode;
- each accepted Extended feature requires Extended mode;
- unsupported x64/ml64 constructs produce stable diagnostics;
- documentation labels do not say `x64 mode` or `ml64 mode`;
- share-state round-trip preserves mode;
- mode switch invalidates stale VM/debugger state.

---

## 203. Phase 199 - Instruction Count Watchdog Settings and Diagnostics

### Goal

Expose and harden instruction count limits.

### Behavior

Default:

```text
VM_DEFAULT_INSTRUCTION_LIMIT = 1000000
```

The VM executes at most `limit` logical instructions. Repeated string operations count one element as one logical instruction unless an earlier phase specifies a different per-element model.

On limit exhaustion:

- execution stops before the next logical instruction;
- committed prior instructions remain committed;
- state snapshot reflects completed instructions only;
- diagnostic includes configured limit and executed count.

### Diagnostics

```text
instruction-limit-exceeded
invalid-instruction-limit
```

### Tests

Add tests for:

- default limit present;
- low test limit stops deterministic loop;
- state reflects exactly completed instructions;
- repeated instruction per-element counting;
- rendered diagnostic includes limit and count;
- reset allows rerun.

---

## 204. Phase 200 - Active Time Watchdog and Worker Responsiveness

### Goal

Add an active execution time watchdog without blocking the browser main thread.

### Behavior

Default:

```text
VM_DEFAULT_ACTIVE_TIME_LIMIT_MS = 2000
```

The worker checks elapsed active execution time between chunks or logical instruction batches. Native/unit tests must use an injected fake clock.

### Rejected behavior

Do not check wall-clock time on every hot instruction if it creates excessive overhead.

Do not block the main thread while waiting for timeout.

Do not use non-deterministic real time in unit tests.

### Diagnostics

```text
active-time-limit-exceeded
invalid-time-limit
```

### Tests

Add tests for:

- fake-clock timeout;
- no timeout below limit;
- timeout between chunks;
- UI can cancel/terminate after timeout;
- rendered diagnostic includes elapsed time and configured limit.

---

## 205. Phase 201 - Output Limit Settings and Diagnostics

### Goal

Harden Program Console and Simulator Messages output limits.

### Defaults

```text
VM_DEFAULT_PROGRAM_CONSOLE_LIMIT_BYTES = 1048576
VM_DEFAULT_SIMULATOR_MESSAGES_LIMIT_BYTES = 262144
```

### Behavior

- Program Console and Simulator Messages have separate limits.
- Output limit failure stops the operation that would exceed the limit.
- Partial line/output behavior must be deterministic: validate output size before append where possible; otherwise append only complete emitted chunks.
- The diagnostic must go to Simulator Messages, not Program Console.

### Diagnostics

```text
program-console-limit-exceeded
simulator-messages-limit-exceeded
invalid-output-limit
```

### Tests

Add tests for:

- Program Console limit exceeded;
- Simulator Messages limit exceeded;
- exactly-at-limit succeeds;
- over-limit fails without partial chunk append;
- reset clears output counters;
- rendered diagnostics remain readable even when message limit is near exhaustion.

---

## 206. Phase 202 - Memory Capacity Limit Diagnostics

### Goal

Harden memory capacity diagnostics across loader, data layout, stack, heap, and configured tiers.

### Behavior

Every memory-capacity failure must include:

- requested bytes;
- allowed bytes;
- memory category;
- active tier;
- source span when caused by a declaration or directive.

### Diagnostics

```text
memory-capacity-exceeded
stack-capacity-exceeded
heap-capacity-exceeded
data-capacity-exceeded
code-capacity-exceeded
```

### Tests

Add tests for:

- `.data` exceeds capacity;
- `.DATA?` exceeds capacity;
- `.CONST` exceeds capacity;
- `.stack` exceeds active tier;
- heap setting exceeds active tier;
- generated diagnostic includes concrete numeric limits;
- no partial VM state after loader capacity failure.

---

## 207. Phase 203 - Resource Failure Recovery and Reset

### Goal

Ensure all resource failures leave the simulator recoverable.

### Behavior

After any resource failure:

- Run button remains usable after reset;
- worker can be recreated;
- stale debugger sessions are invalidated;
- stale input requests are invalidated;
- settings remain editable;
- source text is preserved;
- Program Console and Simulator Messages remain visible until reset/clear.

### Tests

Add tests for recovery after:

- instruction limit;
- active time limit;
- Program Console limit;
- Simulator Messages limit;
- memory capacity failure;
- worker hard termination;
- super-extended cancellation;
- failed settings apply.

---

## 208. Phase 204 - Supported Textbook Program Corpus

### Goal

Add a curated set of supported MASM32/Irvine32-style programs as regression tests.

### Requirements

Each program fixture must include:

- source text;
- required mode/settings;
- expected Program Console output;
- expected final registers where relevant;
- expected memory changes where relevant;
- expected Simulator Messages;
- notes explaining which features it covers.

### Initial corpus categories

- register arithmetic;
- arrays and memory operands;
- stack and procedures;
- Irvine32 output;
- Irvine32 input with scripted input;
- loops and conditionals;
- STRUCT/RECORD examples if implemented;
- debugger/share smoke examples where relevant.

### Tests

Add an aggregate test command for the corpus. The command must fail if any fixture lacks expected output metadata.

---

## 209. Phase 205 - Known-Unsupported and Non-Goal Corpus

### Goal

Add a regression corpus for recognized unsupported constructs and explicit non-goals.

### Categories

- MASM-valid but deferred constructs;
- simulator non-goals;
- x64/ml64 behavior not part of Extended 32-bit Mode;
- Windows API/linking/import examples;
- macro system examples not supported by virtual macro phases;
- malformed syntax with stable diagnostics.

### Tests

Every fixture must assert:

- structured diagnostic code;
- source line/column/byte offset/span length where applicable;
- rendered Simulator Messages text;
- no Program Console output unless intentionally produced before failure;
- no VM crash.

---

## 210. Phase 206 - Deterministic Parser Fuzz Harness Baseline

### Goal

Create a deterministic parser fuzz harness baseline without broadening supported syntax.

### Tasks

- Generate deterministic token/source inputs from a seed.
- Enforce per-case timeout, maximum diagnostics, maximum source size, and seed reproduction command.
- Assert no crash, hang, undefined behavior, or unbounded diagnostic growth.

### Acceptance criteria

A failing fuzz case prints seed, minimized source when available, diagnostic JSON, and reproduction instructions.

## 211. Phase 207 - Fuzz Corpus Expansion, Minimization, and Artifacts

### Goal

Expand the fuzz corpus and artifact handling after the baseline harness exists.

### Tasks

- Add mutation strategies for directives, declarations, operands, brackets, strings, comments, high-level flow, and macros.
- Preserve interesting failure cases as corpus artifacts.
- Add minimization for parser-only failures.

### Acceptance criteria

Corpus artifacts are deterministic, versioned, and included in test reports without hiding failing cases.

## 212. Phase 208 - Supported Syntax Reference Page

### Goal

Create a user-facing supported syntax reference that reflects implemented behavior.

### Requirements

The reference must classify features as:

```text
implemented
accepted no-op
metadata-only
virtual built-in
planned later
explicitly unsupported in v1
non-goal
```

### Content

Include sections for:

- directives;
- data declarations;
- registers;
- instructions;
- operators;
- memory operands;
- Irvine32 routines;
- virtual macros;
- debugger/settings/share behavior;
- Extended 32-bit Mode boundary.

### Tests

Add link/check tests ensuring every runnable example on the page exists as a test fixture.

---

## 213. Phase 209 - Runnable Example Gallery

### Goal

Add a browser example gallery tied to tested fixtures.

### Behavior

Each example must include:

- title;
- short description;
- required mode/settings;
- source text;
- expected output summary;
- list of covered concepts.

Loading an example must replace editor source only after confirmation if the editor has unsaved changes.

### Rejected behavior

Do not include examples requiring unsupported features.

Do not silently change memory mode or execution mode without showing the required settings.

### Tests

Add tests for:

- loading each example;
- unsaved-source confirmation;
- example source runs successfully under declared settings;
- examples do not depend on fresh randomized layout;
- share URL generated after loading an example is deterministic.

---

## 214. Phase 210 - Tutorial Documentation Pages

### Goal

Add beginner-facing tutorial pages aligned with tested simulator behavior.

### Required pages

- registers and aliases;
- flags;
- memory and data declarations;
- stack and procedures;
- Irvine32 console output/input;
- debugger stepping and breakpoints;
- memory settings and safety modes;
- sharing projects;
- Extended 32-bit Mode limitations.

### Requirements

- Every code block marked runnable must have a corresponding test fixture.
- Every unsupported example must show the exact expected diagnostic category.
- Tutorials must not imply Windows API, PE loading, or full MASM/ml64 support.

### Tests

Add documentation lint tests for:

- runnable code block fixture coverage;
- no future-roadmap features marked as available;
- links to supported syntax and examples;
- terminology: `Extended 32-bit Mode`, not `x64 mode`.

---

## 215. Phase 211 - CodeMirror Editor Replacement Baseline

### Goal

Replace the raw source editor with CodeMirror 6 without changing parser/VM semantics.

### Behavior

- CodeMirror stores and displays source text in the browser UI.
- Worker protocol still receives plain source strings.
- C99 parser remains the source of truth.
- Run/Debug commands sample the current editor text exactly.
- Line endings are preserved unless the UI has an explicit normalization rule.

### Rejected behavior

Do not implement syntax highlighting in this phase.

Do not implement indentation rules in this phase.

Do not add semantic validation in CodeMirror.

### Tests

Add tests for:

- editor initializes with default source;
- source text round-trip from editor to worker;
- empty source handling;
- CRLF and LF preservation according to selected policy;
- large source editing does not block Run button;
- replacing editor does not change C99 parser diagnostics for a fixture set.

---

## 216. Phase 212 - Editor Source Synchronization and Run-State Preservation

### Goal

Define how editor changes interact with existing run/debug state.

### Behavior

- Editing source after a successful run marks VM/debugger state stale.
- Running again uses current editor source.
- Reset preserves editor source.
- Loading an example or importing a share URL uses confirmation if the editor is dirty.
- Breakpoint line entries persist as user entries but become unbound until the next parse/load.

### Diagnostics/UI messages

```text
source-changed-vm-stale
unsaved-source-replace-confirmation
breakpoints-unbound-after-edit
```

### Tests

Add tests for:

- source edit after paused debugger invalidates session;
- source edit after halted run allows rerun;
- Reset preserves source;
- imported source confirmation;
- breakpoint entries unbound and rebound by line number on next run.

---

## 217. Phase 213 - Source Byte Offset to Editor Offset Mapping

### Goal

Implement a tested bridge from backend UTF-8 byte offsets to CodeMirror UTF-16 document offsets.

### Tasks

- Convert byte offset and span length from the exact source snapshot parsed by the backend.
- Handle ASCII, non-ASCII, combining marks, emoji/surrogate pairs, CRLF, LF, and mixed line endings.
- Return nullable mapping results for diagnostics that lack source locations.

### Acceptance criteria

Mapping tests prove editor diagnostic markers, current-instruction highlight, and breakpoint navigation use the same conversion helper.

## 218. Phase 214 - MASM Tokenization for Highlighting

### Goal

Add a browser-side MASM tokenizer/highlighter input layer for visual highlighting only.

### Token categories

```text
directives
instructions
registers
data types
operators
numeric literals
strings
character literals
comments
labels
known Irvine32 routines
known virtual macros
unsupported/deferred directive names
plain identifiers
```

### Requirements

- Tokenizer must be case-insensitive for instructions/directives/registers.
- User-defined symbol case must not be semantically changed.
- Tokenization errors must not block Run.
- Backend diagnostics remain authoritative.

### Rejected behavior

Do not use the CodeMirror tokenizer as the parser.

Do not reject source based on highlighting.

Do not resolve symbols in the highlighter.

### Tests

Add tests for tokenization of:

- common directives;
- labels;
- registers and aliases;
- data declarations;
- comments;
- strings and character literals;
- MASM numeric formats;
- unsupported directive names highlighted distinctly but not treated as parser diagnostics.

---

## 219. Phase 215 - MASM Syntax Highlight Rendering and Accessibility

### Goal

Render MASM token categories with accessible styles.

### Behavior

- Apply highlight classes/themes to CodeMirror.
- Preserve readable contrast in default site theme.
- Highlight unsupported/deferred constructs visually but rely on backend diagnostics for authority.
- Do not implement full dark/light preference persistence in this phase unless already available from another phase.

### Accessibility requirements

- Highlighting must not be the only indicator of errors.
- Diagnostics must have text output in Simulator Messages.
- Colors must meet the project's selected contrast threshold for normal text.

### Tests

Add tests for:

- token classes applied;
- unsupported/deferred tokens styled;
- labels and comments styled correctly;
- no semantic behavior change from highlighting;
- accessible names/text alternatives for diagnostic markers where present.

---

## 220. Phase 216 - Basic Editor Indentation, Tab, and Shift+Tab

### Goal

Implement baseline editor indentation controls.

### Behavior

- Enter preserves previous line indentation.
- Tab indents current line or all selected lines by configured indent unit.
- Shift+Tab removes up to one indent unit from each selected line.
- Indentation never deletes non-whitespace characters.
- Default indent unit is four spaces.

### Rejected behavior

Do not implement MASM block-aware indentation in this phase.

Do not convert tabs/spaces globally.

Do not autoformat source.

### Tests

Add tests for:

- Enter after indented line;
- Tab on single line;
- Tab on selection;
- Shift+Tab on single line;
- Shift+Tab on selection;
- mixed spaces/tabs according to selected policy;
- no semantic source changes except inserted/removed indentation.

---

## 221. Phase 217 - MASM-Aware Indentation Rules

### Goal

Add conservative MASM-aware indentation after baseline indentation is stable.

### Behavior

Indent one level after lines that open known block-like constructs:

```asm
main PROC
.IF condition
.WHILE condition
.REPEAT
STRUCT
```

Dedent lines beginning with closing constructs:

```asm
ENDP
.ENDIF
.ELSE
.ELSEIF
.ENDW
.UNTIL
.UNTILCXZ
ENDS
```

Rules are visual/editor behavior only. They must not change parser semantics.

### Rejected behavior

Do not auto-insert missing closing directives.

Do not reformat existing full files.

Do not indent based on semantic symbol resolution.

Do not implement autocomplete in this phase.

### Tests

Add tests for:

- PROC/ENDP indentation;
- .IF/.ELSE/.ENDIF indentation;
- .WHILE/.ENDW indentation;
- .REPEAT/.UNTIL indentation;
- STRUCT/ENDS indentation;
- comments and blank lines inside blocks;
- malformed source still editable and not auto-corrected;
- Run receives exactly the edited source text.


---

## 222. Phase 218 - Appearance and Editor Preference Schema

### Goal

Define the local-only appearance/editor preference schema without adding persistence, theme rendering, or editor UI controls yet.

### Work type

UI state/schema only.

### Dependencies

- CodeMirror editor integration must exist or be in progress.
- Share URL/project state schema must already distinguish shared state from local-only state.

### Accepted behavior

Add a typed browser-side settings schema for local-only preferences:

```text
appearance.mode: "system" | "light" | "dark"
editor.theme: "default-light" | "default-dark" | "high-contrast-light" | "high-contrast-dark"
editor.fontSizePx: integer 12..24, default 14
editor.lineWrap: boolean, default false
editor.tabInsertsSpaces: true
editor.indentUnitSpaces: 4
editor.showLineNumbers: true
editor.showDiagnosticGutter: true
editor.showBreakpointGutter: true
```

Define defaults and validation. Invalid local preference values are ignored and replaced with defaults.

### Explicit non-goals

- No theme rendering.
- No localStorage persistence.
- No preferences UI.
- No remote sync.
- No share URL integration except an exclusion test.
- No custom CSS or user-provided theme URLs.

### Diagnostics

Invalid loaded preference values should produce `ui-local-preference-invalid` only when preference loading exists. In this schema phase, invalid-value behavior is tested through pure validation helpers.

### Required tests

- Unit tests for default settings.
- Unit tests for every allowed enum value.
- Unit tests rejecting invalid enum values, out-of-range font sizes, and unexpected keys.
- Test proving local preferences are not included in share-safe project state serialization.
- Type-level or schema tests proving `tabInsertsSpaces` is fixed to `true` in v1.

## 223. Phase 219 - CodeMirror Theme Extensions and App Theme Synchronization

### Goal

Apply supported light/dark/high-contrast editor themes through CodeMirror 6 theme extensions and synchronize them with the app appearance mode.

### Work type

UI rendering only.

### Dependencies

- Phase 91 local preference schema.
- CodeMirror editor baseline from the prior editor phases.

### Accepted behavior

1. Add CodeMirror theme extensions for:
   - `default-light`;
   - `default-dark`;
   - `high-contrast-light`;
   - `high-contrast-dark`.
2. Support effective appearance mode:
   - `system` follows system preference when available;
   - `light` forces light UI/editor;
   - `dark` forces dark UI/editor.
3. Theme switching preserves:
   - source text;
   - diagnostics;
   - breakpoints;
   - current VM/debugger state;
   - Program Console;
   - Simulator Messages;
   - undo history where CodeMirror supports preserving it through reconfiguration;
   - keyboard focus unless the focused element is removed.
4. Theme switching must not trigger parse, assemble, run, reset, or worker reload.

### Explicit non-goals

- No third-party theme picker.
- No user-authored theme CSS.
- No semantic token validation.
- No parser or VM changes.

### Required tests

- Component tests for all four themes applying expected root theme markers/classes.
- Test switching light/dark does not change source text or run generation.
- Test switching theme does not call the worker Run/Parse/Reset paths.
- Test focus remains in editor when switching theme from a toolbar control and focus returns appropriately.
- Visual regression screenshots where the project test stack supports them.
- Accessibility checks for caret visibility, selection visibility, breakpoint marker visibility, diagnostic marker visibility, and current-instruction marker visibility.

## 224. Phase 220 - Editor Preference Controls and Local Persistence

### Goal

Expose editor preference controls and persist them locally in the browser.

### Work type

UI controls + local browser persistence.

### Dependencies

- Phase 91 schema.
- Phase 92 theme rendering.

### Accepted behavior

1. Add a settings UI for:
   - appearance mode;
   - editor theme;
   - font size;
   - line wrap.
2. Persist preferences using `localStorage` only.
3. Load persisted preferences at app startup before editor construction. If a browser startup path cannot do this, document that path and prove preferences apply before the editor becomes interactive.
4. Apply preferences without re-running the program.
5. Keep preferences local-only.
6. Never store local preferences in share URLs or project export.
7. Save only validated normalized preferences.

### Explicit non-goals

- No remote sync.
- No IndexedDB.
- No service-worker cache storage.
- No cookie storage.
- No cursor/selection/scroll persistence.
- No per-project local preference profiles.

### Diagnostics

Preference save failures use `ui-local-preference-save-failed`.
Preference load failures use `ui-local-preference-load-failed`.
These are UI diagnostics, not MASM assembly/runtime diagnostics.

### Required tests

- Load default preferences when no localStorage entry exists.
- Persist and reload valid preferences.
- Ignore invalid persisted values and fall back to defaults.
- Confirm local preference storage does not change share URL payload.
- Confirm source text and VM state do not change when preferences are changed.
- Mock `localStorage.setItem` throwing and assert non-blocking save diagnostic.
- Mock `localStorage.getItem` throwing and assert fallback defaults.

## 225. Phase 221 - Editor Preference Failure Recovery and Privacy Boundaries

### Goal

Harden local preference persistence failure cases and privacy boundaries.

### Work type

UI diagnostics + privacy tests.

### Dependencies

- Phase 93 persistence.
- Share URL/project state serialization.

### Accepted behavior

1. App remains usable when localStorage is unavailable or quota-exceeded.
2. Only one load-failure warning is emitted per page load.
3. Save failures are visible but non-fatal.
4. Runtime state, source code, Program Console, Simulator Messages, input text, and memory dumps are never stored in local preferences.
5. Local preferences never include shared project state.

### Explicit non-goals

- No encrypted local storage.
- No user account privacy controls.
- No storage-management UI beyond the warning.

### Required tests

- Simulated quota failure.
- Simulated localStorage disabled/unavailable.
- Simulated malformed JSON preference record.
- Share URL privacy exclusion test.
- Test that local preference diagnostics do not block Run or Debug commands.
- Test that clearing local preferences restores defaults without changing source or runtime state.

## 226. Phase 222 - Editor Diagnostic Data Model and Source-Version Binding

### Goal

Define the editor-side diagnostic model that projects backend diagnostics into CodeMirror without duplicating compiler semantics.

### Work type

UI data model only.

### Dependencies

- Structured diagnostics from parser/source-run.
- Source version/run generation tracking.

### Accepted behavior

Each editor diagnostic record contains:

```text
diagnosticId
category
code
severity
message
sourceFile
lineNumber1Based
columnNumber1Based
byteOffset
spanByteLength
renderedSimulatorMessageText
runGeneration
sourceVersion
isStale
```

Severity mapping:

```text
assembly-error -> error
runtime-error -> error
resource-limit-error -> error
unsupported-feature -> error when execution-blocking, warning otherwise
simulator-warning -> warning
user-stopped -> info
internal-simulator-error -> error
ui-local-preference-* -> warning
```

### Explicit non-goals

- No independent editor linter.
- No new parser.
- No CodeMirror syntax validation beyond display of backend diagnostics.

### Required tests

- Map every diagnostic category to an editor severity.
- Preserve diagnostic code and rendered text.
- Mark diagnostics stale when sourceVersion changes.
- Do not create editor markers for diagnostics without usable source locations.
- Do not lose Simulator Messages entries when editor markers are stale or missing.

## 227. Phase 223 - Source Byte Offset to Editor Offset Mapping for Diagnostics

### Goal

Convert backend UTF-8 byte offsets/spans into CodeMirror UTF-16 document offsets.

### Work type

UI utility + tests.

### Dependencies

- Phase 222 - Editor Diagnostic Data Model and Source-Version Binding.
- CodeMirror document access.

### Accepted behavior

1. Backend byte offsets remain authoritative.
2. Editor mapping converts UTF-8 byte offsets to UTF-16 offsets.
3. ASCII-only source maps byte offset to the same numeric UTF-16 offset.
4. Non-ASCII comments and string literals map correctly.
5. Emoji before a diagnostic span map correctly.
6. CRLF and LF variants map correctly.
7. Stale source versions disable click-to-source navigation.
8. If exact span mapping fails, line-level fallback is used only when line metadata is valid.

### Explicit non-goals

- No raw non-UTF-8 source file support.
- No background reparse.
- No semantic correction of backend spans.

### Required tests

- ASCII source span.
- Non-ASCII comment before span.
- Non-ASCII string before span.
- Emoji before span.
- Multi-line diagnostic span.
- CRLF and LF source variants.
- Stale source version fallback.
- Invalid byte span gracefully disables editor marker while preserving Simulator Messages.

## 228. Phase 224 - Editor Diagnostic Markers and Gutter Rendering

### Goal

Render backend diagnostics in CodeMirror as markers, underlines, and optional gutter indicators.

### Work type

UI rendering.

### Dependencies

- Phase 222 - Editor Diagnostic Data Model and Source-Version Binding.
- Phase 223 - Source Byte Offset to Editor Offset Mapping for Diagnostics.

### Accepted behavior

1. Render error/warning/info markers for current diagnostics.
2. Show gutter indicators when `editor.showDiagnosticGutter = true`.
3. Support multiple diagnostics on the same span.
4. Support multi-line diagnostics.
5. Mark stale diagnostics visually distinct or hide them from the editor while retaining Simulator Messages.
6. Diagnostic markers must not alter source text.
7. Diagnostic marker colors must not be the only indicator; shape/title/accessibility text must distinguish severity.

### Explicit non-goals

- No quick fixes.
- No automatic code edits.
- No semantic linting independent of backend diagnostics.

### Required tests

- Render one error marker.
- Render one warning marker.
- Render multiple markers on one span.
- Render multi-line marker.
- Toggle diagnostic gutter on/off.
- Stale source hides or disables marker.
- Accessibility label contains code/severity/message.
- Marker rendering does not change editor text or undo history.

## 229. Phase 225 - Simulator Messages Click-to-Source Navigation

### Goal

Make rendered Simulator Messages diagnostics navigate to matching editor locations when safe.

### Work type

UI integration.

### Dependencies

- Phase 222 - Editor Diagnostic Data Model and Source-Version Binding.
- Phase 223 - Source Byte Offset to Editor Offset Mapping for Diagnostics.
- Phase 224 - Editor Diagnostic Markers and Gutter Rendering.
- Existing Simulator Messages formatter.

### Accepted behavior

1. Clickable diagnostics navigate to the source span when source file, sourceVersion, and runGeneration match.
2. Stale diagnostics remain readable but do not navigate.
3. Diagnostics without source spans do not show source-navigation affordance.
4. Navigation scrolls the editor enough to reveal the span.
5. Navigation does not steal focus permanently from keyboard users; focus target is deterministic.
6. Line-level fallback highlights the line when exact span is unavailable.

### Explicit non-goals

- No multi-file tab switching beyond existing active-file support unless multi-file UI already exists.
- No automatic re-run after navigation.

### Required tests

- Click assembly diagnostic -> source span selected/highlighted.
- Click runtime diagnostic -> instruction span highlighted.
- Click stale diagnostic -> no navigation and accessible stale indication.
- Click diagnostic with no source -> no navigation affordance.
- Non-ASCII source location navigates correctly.
- Keyboard activation works the same as pointer click.

## 230. Phase 226 - Editor Diagnostic Staleness and Multi-Diagnostic Behavior

### Goal

Handle source edits, reruns, duplicate diagnostics, and many diagnostics without confusing editor state.

### Work type

UI state management.

### Dependencies

- Phase 222 - Editor Diagnostic Data Model and Source-Version Binding.
- Phase 223 - Source Byte Offset to Editor Offset Mapping for Diagnostics.
- Phase 224 - Editor Diagnostic Markers and Gutter Rendering.
- Phase 225 - Simulator Messages Click-to-Source Navigation.

### Accepted behavior

1. Source edit increments `sourceVersion`.
2. Existing editor diagnostic markers are removed immediately from the editor on source edit. Simulator Messages remain visible, are marked stale, and their source-navigation actions are disabled until the next successful run/debug result.
3. Simulator Messages from previous runs remain visible unless the app already clears them on edit; whichever policy exists must be documented and tested.
4. New run replaces editor markers with diagnostics from the new runGeneration.
5. Duplicate diagnostics are keyed by `diagnosticId`, not only by source span.
6. Diagnostic rendering has a maximum marker count to preserve responsiveness; excess diagnostics are summarized with a stable UI warning.

### Explicit non-goals

- No background parse-on-type diagnostics.
- No deduplication that hides separate backend diagnostics.

### Required tests

- Edit source after diagnostics; markers stale/removed.
- Re-run source; markers update to new generation.
- Two diagnostics on one line both remain accessible.
- Diagnostic-count cap shows summary and preserves Simulator Messages.
- Click-to-source disabled for stale diagnostics.

## 231. Phase 227 - Current Instruction Highlight Data Model

### Goal

Define how debugger backend instruction state maps to an editor highlight record.

### Work type

UI data model.

### Dependencies

- Debugger backend snapshots.
- Source version/run generation tracking.

### Accepted behavior

A current-instruction highlight record contains:

```text
instructionIndex
sourceFile
lineNumber1Based
columnNumber1Based
byteOffset
spanByteLength
runGeneration
sourceVersion
stateKind
isExactSpan
```

The highlight is valid only when source version and run generation match the active loaded program.

### Explicit non-goals

- No highlight rendering.
- No breakpoint gutter rendering.

### Required tests

- Build highlight record from paused state.
- Build highlight record from breakpoint state.
- Build highlight record from waiting-for-input state.
- Clear highlight on reset.
- Mark highlight stale on source edit.

## 232. Phase 228 - Current Instruction Highlight UI Rendering

### Goal

Render the current instruction in the editor during debugging.

### Work type

UI rendering.

### Dependencies

- Phase 227 - Current Instruction Highlight Data Model.
- Phase 223 - Source Byte Offset to Editor Offset Mapping for Diagnostics.

### Accepted behavior

1. Show exact-span highlight when source span exists.
2. Show line-level highlight when only line metadata exists.
3. Clear highlight on reset, source edit before rebuild, hard worker termination, and source-load failure.
4. Preserve keyboard focus unless user chooses focus-follow behavior.
5. Distinguish current-instruction highlight from diagnostics and breakpoints by color plus non-color cue.

### Required tests

- Step Into updates highlight to next instruction.
- Breakpoint hit highlights breakpointed instruction before execution.
- Waiting-for-input highlights the input routine instruction.
- Reset clears highlight.
- Source edit clears or marks stale highlight.
- Highlight is visually and accessibly distinguishable from diagnostics.

## 233. Phase 229 - Breakpoint Gutter Binding Model

### Goal

Define editor-side line breakpoint requests and their binding to backend instruction indexes.

### Work type

UI/backend state bridge.

### Dependencies

- Breakpoint backend protocol.
- Source line to instruction mapping.

### Accepted behavior

1. Breakpoint UI uses 1-based source line numbers.
2. A breakpoint request may be unbound before load or after source edit.
3. After successful load, breakpoint requests bind to executable instruction indexes.
4. A line with multiple executable instructions binds to the first executable instruction for v1.
5. Source edits invalidate bindings until reload.
6. Rebinding is line-number based only; content-aware movement is out of scope.
7. Breakpoint state includes:

```text
breakpointId
sourceFile
lineNumber1Based
isEnabled
bindingState: "unbound" | "bound" | "invalid"
instructionIndex or null
runGeneration or null
```

### Required tests

- Bound breakpoint on executable line.
- Unbound breakpoint on comment/blank line.
- Breakpoint invalidated by source edit.
- Rebind by same 1-based line after successful load.
- Multi-instruction line binds to first executable instruction.
- Disabled breakpoint remains visible but does not stop execution.

## 234. Phase 230 - Breakpoint Gutter UI Interactions

### Goal

Expose breakpoint toggling and state through the editor gutter.

### Work type

UI interactions.

### Dependencies

- Phase 102 binding model.
- CodeMirror gutter support from editor integration phases.

### Accepted behavior

1. Clicking/tapping gutter toggles breakpoint on that 1-based line.
2. Keyboard-accessible breakpoint controls are available for the current line.
3. Bound, unbound, disabled, and invalid breakpoints have distinct visual and accessible labels.
4. Gutter toggling sends worker protocol messages only where backend state must change.
5. Gutter toggling does not run or reset the program.
6. Backend rejection reverts UI optimistic state or displays a stable error without leaving inconsistent UI.

### Required tests

- Click gutter creates breakpoint request.
- Click again removes breakpoint request.
- Keyboard command toggles breakpoint on active line.
- Unbound breakpoint displays unbound state.
- Source edit marks breakpoint unbound.
- Backend rejection displays error and resolves UI state.
- Breakpoint icon has accessible name/state.

## 235. Phase 231 - Debugger Control Visual Feedback in the Editor Shell

### Goal

Coordinate editor-shell visual state with debugger state without changing backend execution behavior.

### Work type

UI state rendering.

### Dependencies

- Debugger command/state matrix.
- Current-instruction highlight.
- Breakpoint gutter.

### Accepted behavior

1. Running disables Run, Step Into, Step Over, and Continue; enables Pause/Stop according to backend capabilities.
2. Paused enables Step Into, Step Over, Continue, Reset, and Stop.
3. Stopped at breakpoint enables Step Into, Step Over, Continue, Reset, and Stop.
4. Waiting for input disables Step Into, Step Over, and Continue; enables input submit/cancel plus Reset/Stop.
5. Halted/faulted disables Continue/Pause and enables Reset/Run rebuild.
6. State changes are rendered atomically with the latest backend snapshot.
7. Stale command responses are ignored or diagnosed according to the debugger protocol, not applied to current UI state.

### Required tests

- Button states for each debugger state.
- Waiting-for-input disables debugger stepping.
- Runtime fault disables Continue.
- Stale response does not overwrite newer state.
- Visual busy state appears during running/continuing.

## 236. Phase 232 - Debugger Editor Accessibility and Keyboard Behavior

### Goal

Ensure debugger editor controls are keyboard-operable and accessible.

### Work type

UI accessibility.

### Dependencies

- Phase 224 - Editor Diagnostic Markers and Gutter Rendering, for diagnostic marker accessibility where applicable.
- Phase 225 - Simulator Messages Click-to-Source Navigation, for keyboard-accessible navigation from messages to source.
- Phase 228 - Current Instruction Highlight UI Rendering, for accessible current-instruction indication.
- Phase 230 - Breakpoint Gutter UI Interactions, for keyboard-operable breakpoint controls.

### Accepted behavior

1. Step Into, Step Over, Continue, Pause, Stop, Reset, breakpoint toggle, and diagnostic navigation controls have accessible names.
2. Disabled state is exposed to assistive technologies.
3. Focus order is deterministic.
4. Current-instruction, breakpoint, and diagnostic states are not communicated by color alone.
5. Keyboard users can toggle breakpoints and activate diagnostic links.
6. Debugger state changes do not unexpectedly steal focus.

### Required tests

- Keyboard navigation through debugger controls.
- Accessible names present for controls.
- Disabled states exposed.
- Breakpoint toggle keyboard path works.
- Diagnostic click-to-source keyboard path works.
- Focus preserved after Step Into and after runtime error.

## 237. Phase 233 - Accessibility Audit Harness and Manual Checklist

### Goal

Create a repeatable accessibility audit harness and manual checklist for the v1 UI.

### Work type

Validation only.

### Dependencies

- Main UI, editor, diagnostics, debugger controls, settings controls.

### Accepted behavior

1. Add automated accessibility checks for app shell, editor shell, settings, diagnostics, and debugger controls.
2. Add manual keyboard checklist.
3. Add manual screen-reader spot-check checklist.
4. Add contrast checklist for themes and semantic markers.
5. Track findings as release blockers or accepted known limitations.

### Required tests/checks

- Automated accessibility smoke test passes.
- Keyboard-only Run/debug flow works.
- Focus visible on all interactive controls.
- Program Console and Simulator Messages have distinct accessible labels.
- Diagnostic markers have accessible descriptions.
- Breakpoint markers have accessible descriptions.

## 238. Phase 234 - Responsive Layout and Mobile Interaction Pass

### Goal

Validate and harden layout at mobile/tablet/desktop sizes.

### Work type

UI validation and CSS/layout fixes only.

### Dependencies

- Main UI, editor, diagnostics, debugger panels, settings.

### Accepted behavior

Required viewport widths:

```text
320px mobile portrait
375px mobile portrait
768px tablet portrait
1024px tablet/desktop
large desktop
```

At each width:

- editor usable;
- Run/debug controls reachable;
- Program Console and Simulator Messages distinct;
- settings dialogs fit or scroll within viewport;
- tables collapse or scroll without hiding labels;
- breakpoint/diagnostic controls remain tappable;
- no essential controls hidden behind horizontal scrolling.

### Required tests/checks

- Responsive screenshot or DOM layout smoke tests at required widths.
- Touch/pointer breakpoint toggle test where supported.
- Modal/dialog overflow test.
- Console/messages visibility test.
- Keyboard focus remains visible at narrow widths.

## 239. Phase 235 - Diagnostic Message Catalog Final Review

### Goal

Freeze and audit stable user-facing diagnostics for v1.

### Work type

Diagnostics/documentation/test review only.

### Dependencies

- Native Diagnostic Rendering Harness.
- Node Simulator Messages formatter tests.
- Implemented diagnostic categories.

### Accepted behavior

Maintain a message catalog or golden fixture manifest covering:

- MASM-invalid syntax;
- unsupported planned features;
- explicit non-goals;
- runtime memory errors;
- `.CONST` write protection;
- resource-limit failures;
- input cancellation/errors;
- debugger stale-session/state errors;
- editor/source-position stale diagnostics;
- share/import failures;
- local preference persistence failures;
- internal simulator errors.

Each catalog entry has:

```text
stable code
category
message template
source/span behavior
rendered Simulator Messages golden fixture
sample source or command that triggers it
```

### Required tests

- Catalog entries match rendered golden messages.
- No known diagnostic falls back to generic `unsupported-syntax` when a specific code exists.
- Non-goal diagnostics do not imply future support.
- MASM-invalid diagnostics do not call the syntax a temporary limitation.
- Every changed diagnostic runs through Node formatter tests.

## 240. Phase 236 - Example Program Final Review

### Goal

Review runnable examples for correctness, scope, and reproducibility.

### Work type

Examples/tests/documentation only.

### Dependencies

- Runnable example gallery.
- Supported syntax reference.
- Source-run test harness.

### Accepted behavior

1. Every runnable example has expected Program Console output or expected Simulator Messages.
2. Examples run under default settings unless explicitly documented otherwise.
3. Examples requiring input include exact input transcripts.
4. Examples requiring seeded randomized layout include a fixed seed.
5. Examples demonstrating errors use stable expected diagnostics.
6. Examples do not require unsupported Windows/API/linker behavior.
7. Examples do not claim full MASM/x86/Windows/ml64 compatibility.

### Required tests

- Execute every runnable example through source-run JSON.
- For browser-only examples, include a Playwright/component smoke test or documented manual test.
- Verify expected Program Console output.
- Verify expected Simulator Messages for error examples.
- Verify example metadata: title, features used, required settings, expected result.

## 241. Phase 237 - Documentation Consistency Audit

### Goal

Ensure documentation matches implemented behavior and tests.

### Work type

Documentation validation only.

### Dependencies

- Supported syntax reference.
- Tutorial pages.
- Known-unsupported reference.
- Feature registry or fixture manifest.

### Accepted behavior

1. Every documented supported syntax form maps to a feature-registry entry or passing test fixture.
2. Every documented unsupported/non-goal form maps to a diagnostic fixture.
3. Tutorials use only implemented behavior unless clearly labeled as roadmap.
4. Documentation examples default to deterministic settings.
5. Documentation includes the project definition and non-goals.
6. Documentation distinguishes Program Console from Simulator Messages.

### Required tests

- Documentation link/checker; if the execution environment cannot perform external link checks, run internal anchor checks and report external link checking as environment-skipped.
- Feature-doc coverage script comparing docs to feature registry/fixtures.
- Known-unsupported docs compare to diagnostic fixtures.
- No roadmap-only feature appears in supported syntax reference.
- Stale phase-reference check:
  - Scan `FULL_IMPLEMENTATION_SPEC.md`, `INCREMENTAL_IMPLEMENTATION_GUIDE.md`, `SUPPORTED_SYNTAX.md`, and tutorial/known-limitation docs for phase references.
  - Flag references that use a phase number without a nearby title.
  - Flag references where the named feature does not match the current phase title.
  - Maintain an allowlist only for historical notes that explicitly say they are historical.
  - After a phase-reference cleanup edit, search for each stale phrase from the correction list. Any remaining occurrence must be inside this stale-reference static-check list, inside an explicitly labeled historical/audit note, or intentionally preserved with an adjacent explanation.
  - No active implementation requirement may still contain a stale reference.
  - The check must fail on known stale patterns such as:
    - `Phase 47` used as a rotate-policy reference for `ROR`;
    - `Phase 52` used as the two-/three-operand `IMUL` phase;
    - `Phase 51` used as the CALL target-classification phase;
    - `Phase 55` used as the call-depth limit phase;
    - `Phase 57` used as the `RET imm16` phase;
    - `Phase 60` used as PROC USES runtime save/restore;
    - `Phase 61 input normalization`;
    - `Phase 63` used as LOCAL operand/addressing for `ADDR localVar`;
    - `Phase 66 ReadString preflight`;
    - `Phase 68 flag storage patterns`;
    - `Phase 70 DF`;
    - `Phases 71 and 72` used as REP MOVS/STOS/LODS dependencies;
    - `Phase 73 CMPS/SCAS`;
    - `Phase 74 prefix loop infrastructure`;
    - `Phase 75` used as the comparison-repeat phase;
    - `Phase 78` used as the `WAITING_FOR_INPUT` backend phase;
    - `Phase 111 final regression orchestration`;
    - `Phase 112 v1 release gate and known-limitations report`.

## 242. Phase 238 - Final Regression Orchestration

### Goal

Create one final regression command or orchestrated script that runs all v1 validation suites.

### Work type

Test orchestration only.

### Dependencies

- All implemented v1 features and test suites.

### Accepted behavior

Final regression orchestration includes:

- native C unit tests;
- parser/source-run integration tests;
- Wasm/source-run JSON tests where Emscripten is available;
- Node Simulator Messages formatter tests;
- worker protocol tests;
- debugger UI tests;
- editor diagnostics tests;
- share URL tests;
- example gallery tests;
- supported textbook corpus;
- known-unsupported/non-goal corpus;
- deterministic fuzz baseline;
- accessibility smoke checks;
- responsive layout smoke checks.

The runner must report skipped suites explicitly, including the reason.

### Required tests

- Runner exits nonzero on failing required suite.
- Runner reports skipped Emscripten/Wasm suite when `emcc` is unavailable.
- Runner emits machine-readable summary JSON.
- Runner emits human-readable summary.
- Known failing tests cannot be silently ignored.

## 243. Phase 239 - v1 Release Gate and Known-Limitations Report

### Goal

Define and generate the final v1 release gate report.

### Work type

Release validation/reporting only.

### Dependencies

- Phase 238 - Final Regression Orchestration.
- Documentation consistency audit.
- Accessibility/responsive audits.

### Accepted behavior

The v1 gate report includes:

```text
project version/build id
source-of-truth doc versions
post-30 overhaul revision identifier
native C test result
Wasm build/test result or unavailable reason
Node formatter test result
browser/worker test result
editor/debugger test result
example corpus result
known-unsupported corpus result
fuzz baseline result
accessibility audit result
responsive layout result
open release blockers
accepted known limitations
explicit non-goals
```

Release decision states one of:

```text
ready-for-v1
blocked
ready-with-known-limitations
```

### Explicit non-goals

- No new feature implementation.
- No silent waiver of failed tests.
- No claiming unrun test suites passed.

### Required tests/checks

- Generate release report from a sample all-pass summary.
- Generate blocked report from failing summary.
- Generate ready-with-known-limitations report from accepted limitations.
- Ensure unavailable Emscripten is reported as unavailable, not passing.
- Ensure explicit non-goals are included.

## 244. Phase 240 - Local Preference Versioning and Storage Failure Matrix

### Goal

Finalize local preference persistence behavior by adding versioning, migration, exact storage keys, atomic load/apply semantics, and storage failure tests.

### Work type

Browser UI/settings persistence and tests only.

### Dependencies

- Phase 91 preference schema.
- Phase 93 preference controls and local persistence.
- Phase 94 privacy/failure boundaries.

### Accepted behavior

Use this exact storage key:

```text
masm32-sim.localPreferences.v1
```

Use this exact envelope field:

```text
localPreferenceSchemaVersion: 1
```

Load behavior:

- absent key: use defaults, no warning;
- valid v1 payload: validate all fields and apply valid values;
- invalid JSON: apply no stored values, use defaults, emit `ui-local-preference-invalid`;
- missing version: treat as v0 partial payload, validate recognized fields, fill defaults, and attempt to re-save as v1;
- unsupported future version: apply no stored values, use defaults, emit `ui-local-preference-version-unsupported`;
- invalid field value: default that field and emit one `ui-local-preference-invalid` warning for the payload;
- unknown extra field: ignore and drop it on next successful save.

Save behavior:

- save on supported preference control changes;
- if `localStorage` read/write throws, emit `ui-local-preference-save-failed` or `ui-local-preference-load-failed` as appropriate;
- storage failures do not block editing, run, debug, reset, share, or import.

### Explicit non-goals

- No IndexedDB.
- No cloud sync.
- No cookies.
- No service-worker storage.
- No remote account preference sync.
- No arbitrary preference JSON import UI.

### Required tests

- Default load when key absent.
- Valid v1 payload round trip.
- Invalid JSON falls back atomically.
- Missing-version v0 payload migrates recognized fields and fills defaults.
- Future version is rejected atomically.
- Invalid field value defaults only that field and emits one warning.
- Unknown fields are removed after next save.
- Simulated `SecurityError` on localStorage read does not crash the app.
- Simulated quota/save failure does not block Run.
- Share URL export excludes all local preference fields.

## 245. Phase 241 - Theme Reconfiguration and System Appearance Smoke Harness

### Goal

Verify theme switching and system appearance changes are UI-only CodeMirror/app reconfiguration operations that preserve editor/debugger state.

### Work type

UI validation harness.

### Dependencies

- Phase 92 CodeMirror theme extensions.
- Phase 224 - Editor Diagnostic Markers and Gutter Rendering.
- Phase 228 - Current Instruction Highlight UI Rendering.
- Phase 230 - Breakpoint Gutter UI Interactions.

### Accepted behavior

Theme changes must:

- use CodeMirror reconfiguration rather than editor destruction;
- preserve source text, selection, undo history, diagnostics, breakpoint markers, current-instruction highlight, Program Console, Simulator Messages, worker/debugger state, and keyboard focus;
- not call parse/assemble/run/reset;
- update immediately when `appearance.mode = "system"` and the system color scheme changes.

### Required tests

- Switch light -> dark -> high-contrast -> light without losing source or undo history.
- Switch while diagnostics are visible; markers remain correct.
- Switch while stopped at a breakpoint; current-line and breakpoint visuals remain correct.
- Switch while waiting for input; wait state remains unchanged.
- Simulated system color-scheme change updates theme only in `system` mode.
- Explicit light/dark modes ignore system color-scheme changes.
- Theme switching preserves keyboard focus.
- No worker command is sent during theme-only changes.

## 246. Phase 242 - Keyboard-Only Editor and Debugger Flow Harness

### Goal

Prove the final editor, diagnostics, debugger controls, settings, breakpoint, and navigation workflows are usable without a mouse.

### Work type

Accessibility/integration validation.

### Dependencies

- Phase 232 - Debugger Editor Accessibility and Keyboard Behavior.
- Phase 233 - Accessibility Audit Harness and Manual Checklist.
- Phase 234 - Responsive Layout and Mobile Interaction Pass.
- Phase 225 - Simulator Messages Click-to-Source Navigation.
- Phase 230 - Breakpoint Gutter UI Interactions.

### Accepted behavior

The keyboard-only flow must support:

1. focus editor;
2. edit source;
3. run;
4. inspect Simulator Messages;
5. keyboard-activate a diagnostic source link;
6. move focus to the editor selection;
7. set and clear a breakpoint through a keyboard-accessible control;
8. start debugging;
9. Step Into;
10. Continue;
11. Reset;
12. change appearance mode;
13. return focus to the editor.

State announcements for paused, waiting-for-input, runtime-error, complete, and reset-complete states must use a polite live region or equivalent accessible status mechanism without stealing focus.

### Required tests

- Automated keyboard traversal reaches every editor/debugger/settings control in a deterministic order.
- Keyboard diagnostic activation focuses the editor span.
- Pointer diagnostic activation leaves focus in Simulator Messages while revealing/selecting the span.
- Breakpoint set/clear is possible without a mouse.
- Disabled controls expose disabled state to assistive technology.
- Current state changes update the accessible status region.
- 200% zoom and mobile-width layouts keep controls reachable by keyboard.

## 247. Phase 243 - Release Artifact Inventory, Hash Manifest, and Worker-Path Smoke

### Goal

Finalize release validation by checking required artifacts, hashes, required/optional test status, and worker/Wasm execution routing.

### Work type

Release validation harness and report generation.

### Dependencies

- Phase 238 - Final Regression Orchestration.
- Phase 239 - v1 Release Gate and Known-Limitations Report.

### Accepted behavior

The release report must include these artifacts:

- built static app;
- Wasm module;
- JavaScript/TypeScript bundle;
- generated supported syntax documentation;
- runnable example corpus;
- unsupported/non-goal corpus;
- diagnostic catalog report;
- regression report;
- known-limitations report;
- SHA-256 hash manifest for release artifacts.

Test orchestration must separate required and optional suites:

- missing or failing required suite: release failure;
- skipped required suite: release failure;
- skipped optional suite: allowed only with explicit environment-dependent reason;
- every suite row lists command, environment, status, reason if skipped, and artifact path.

Worker-path smoke:

- Run uses the worker/Wasm path;
- Debug uses the worker/Wasm path;
- VM work is not executed on the main UI thread;
- long-running or faulting programs do not freeze the editor controls;
- worker hard-stop invalidates stale sessions and allows a fresh run after reset/reload.

### Required tests

- Release report generation with all required artifacts present.
- Release report fails if Wasm module is missing.
- Release report fails if a required test suite is skipped.
- Optional suite skip records reason without failing release.
- Hash manifest changes when a release artifact changes.
- Browser smoke proves Run/Debug communicate with a worker.
- Main-thread responsiveness test while worker executes a long-running program.
- Hard-stop recovery test after worker termination.

---

## 248. Phase 244 - Final UI Policy Conformance and Accessibility Regression Sweep

### Goal

Perform a final validation-only pass over the final UI/editor/release phases to prove deterministic policy conformance, accessibility minimums, and release-report completeness after all earlier UI slices have been implemented.

### Work type

Validation harness and release checklist only. This phase must not add new simulator semantics.

### Dependencies

- All runtime, Irvine32, input, debugger, editor, settings, sharing, persistence, accessibility, release, and UI policy phases required before Phase 244 - Final UI Policy Conformance and Accessibility Regression Sweep are complete. Do not treat any old bare phase range as authoritative for this final validation phase.
- Browser CI harness available.
- Native/Node diagnostic rendering harness available.
- Release artifact generation available.

### Accepted behavior

The harness must verify:

1. no final guide/spec policy-choice wording remains active after v2 amendments;
2. every UI/settings diagnostic is rendered through Simulator Messages with a stable category and code;
3. localStorage unavailable, blocked, invalid, quota-exceeded, and unsupported-version scenarios are non-fatal and deterministic;
4. system-theme fallback and live system-theme changes are deterministic;
5. CodeMirror diagnostic projections use canonical lint diagnostics and tested byte-to-editor offset mapping;
6. stale diagnostics are visible but non-navigable after source edit;
7. breakpoint gutter controls expose accessible labels and keyboard operation;
8. current-instruction, breakpoint, diagnostic, and focus visuals compose deterministically on the same line;
9. responsive layout preserves access to editor, controls, Simulator Messages, Program Console, registers, memory tables, and release report fields;
10. release artifact inventory contains every required artifact and hash;
11. required release suites cannot be skipped;
12. generated docs and example gallery are consistent with the feature/test manifest.

### Explicit non-goals

- No new MASM syntax.
- No new VM instruction behavior.
- No new debugger command semantics.
- No new theme beyond the four accepted v1 themes.
- No automatic WCAG certification claim.

### Required tests/checks

- Static grep/checklist test proving v2-superseded soft phrases are not present in the final incorporated guide/spec sections, except in historical notes or quoted migration comments.
- Browser test for localStorage blocked by throwing `SecurityError` on read and write.
- Browser test for storage quota/save failure.
- Browser test for `matchMedia` unavailable and for simulated dark/light system changes.
- Browser test with non-ASCII source before diagnostics, including combining marks, emoji, CRLF, and mixed line endings.
- Browser test proving a stale diagnostic link emits `ui-diagnostic-source-stale` and does not move focus.
- Browser test proving keyboard breakpoint toggling announces bound/unbound state.
- Browser test at 320 CSS px width and 200% zoom.
- Browser test for current-instruction + breakpoint + diagnostic marker visual/accessibility composition on one line.
- Release-report test proving all required artifact hashes are present and change when emitted bytes change.
- Release-report test proving skipped required suites fail the release gate.
- Documentation manifest test proving every supported syntax reference entry has at least one passing test fixture.
- Documentation manifest test proving unsupported/non-goal examples are not listed as supported features.

### Completion criteria

This phase passes only when the final incorporated spec/guide text, browser UI, diagnostic renderer, release reports, and documentation/test manifests agree on the same implemented v1 behavior. Failures block v1 release until fixed or explicitly moved to the known-limitations report as a non-goal rather than a partially implemented feature.

## Appendix A. Canonical Post-30 Cross-Cutting Requirements

These requirements apply to every post-30 phase unless a phase explicitly defines a narrower rule. They consolidate repeated audit refinements so future implementation sessions do not need any historical planning file.

### Implementation-readiness rules

- Replace policy-choice wording with a mandatory requirement, a named nullable field, a documented no-op, or an explicit deferred/non-goal diagnostic.
- Every new user-visible diagnostic path requires structured JSON tests and rendered Simulator Messages tests.
- Every parser/runtime phase must state accepted syntax, rejected syntax, source-span target, no-partial-mutation behavior on failure, required tests, and non-goals.
- New helper modules, exported structs/enums, public APIs, and configuration records remain subject to file-header and Doxygen requirements.
- Later-phase references are not permission to implement future behavior early.
- If a later corrective phase revisits a feature that an earlier completed phase already specified, the later phase must be written as audit/repair/hardening work unless it intentionally changes the feature contract. The assistant must inspect the current implementation first, preserve working behavior, repair broken or incomplete behavior, add regression tests, and state in the milestone report whether runtime/source-run behavior actually changed. Do not blindly reimplement the feature, do not skip the phase merely because an earlier phase claimed the feature, and do not advance runtime/source-run MASM behavior metadata unless the corrective work actually changes accepted syntax, runtime semantics, source-run behavior, or user-visible runtime diagnostics.
- When a phase references another phase as the owner of a future feature, include both the phase number and the phase title whenever practical. If a phase number and phase title disagree with the feature being discussed, treat that as a documentation defect. Do not implement a feature merely because a stale phase number points to the wrong phase.

### String, flow, STRUCT/RECORD, and macro precision rules

- REP validation-first behavior applies to MOVS/STOS/LODS; REPE/REPNE CMPS/SCAS execute element-by-element because the stop condition depends on flags.
- High-level flow generated IR must include original high-level source location and a `syntheticLoweringId` so debugger displays can group generated instructions without exposing synthetic labels as user symbols.
- STRUCT explicit PTR field access may access a subrange inside the field only; widening beyond the field is `field-width-conflict`.
- RECORD variables use unsigned storage in v1. Signed record fields and signed record initializers are rejected with `unsupported-record-signed-field`.
- Known-deferred Macros.inc names include `mDumpMem`, `mDump`, `mShow`, `mShowRegister`, `mWriteSpace`, and `mWriteString`; they produce `unsupported-macro-invocation`.

### Debugger, breakpoints, and share/import rules

- Debugger and share/import worker payloads must be structured-clone-safe and JSON-compatible. Functions, cyclic objects, `Map`, `Set`, `BigInt`, `undefined` fields, DOM nodes, and binary buffers are rejected or prevented unless a later phase explicitly adds a binary message type.
- Optional schema fields are absent rather than present as `undefined`. Nullable fields document exactly which state permits `null`.
- After hard worker termination, all prior session IDs, command IDs, run generations, pending input request IDs, breakpoint bindings, and VM references are invalid.
- Breakpoints are line-number based in v1 and do not track moved code. Source edits make backend bindings stale/unbound until the next successful source load.
- The breakpoint skip-once key is exactly `{ sourceFile, oneBasedLineNumber, instructionIndex, runGeneration }`.
- Backend source byte offsets map to CodeMirror UTF-16 document offsets only through the tested source-offset mapper.
- `VM_DEBUG_CONTINUE_CHUNK_INSTRUCTIONS = 1024` logical VM instructions. Pause and stop-reason precedence are deterministic and must be tested.
- Aggregate register/flag deltas are final-diff only; memory changes, Program Console output, and Simulator Messages remain ordered event streams.
- Memory-change rows include stable identity fields and are never deduplicated by address.
- Share URLs use canonical stable-key JSON, UTF-8 bytes, the project URL-codec wrapper, canonical no-padding base64url, duplicate-parameter rejection, and full-URL length guardrails.

### Settings, memory, extended mode, and editor rules

- Local settings persistence is best-effort and non-blocking. Persistence failure falls back to defaults and emits non-fatal rendered diagnostics.
- Persisted simulator settings use `settingsSchemaVersion = 1`. Unknown future versions are rejected and replaced with defaults. Migration is all-or-nothing.
- Share import is atomic. Unsupported, unsafe, local-only, super-extended-only, or unavailable memory settings reject the import with no partial application.
- KiB, MiB, and GiB use exact binary units.
- Configured VM address space, committed VM backing-store bytes, WebAssembly linear-memory pages, and browser allocation failures are distinct in diagnostics and UI.
- Extended 32-bit Mode still uses 32-bit VM effective addresses. Addresses above `0xFFFFFFFFu` are rejected with `address-size-exceeded`. The mode is not x64 MASM, not `ml64`, not a Windows x64 ABI simulator, and not a PE/WinAPI runtime.
- Supported syntax, examples, tutorials, and known-unsupported docs must be generated from or mechanically checked against a feature/test manifest.
- CodeMirror is UI-only. The C99 parser and VM remain the semantic authority.
- Editor/source mapping, diagnostics, breakpoints, current-instruction highlights, settings, and release artifacts remain subject to the browser accessibility and release-gate phases.

## Appendix B. Cross-Cutting Test Matrix References

Large repeated matrices are centralized here and referenced by individual phases:

- Diagnostic rendering matrix: every new stable diagnostic code requires structured JSON and rendered Simulator Messages assertions.
- Memory failure matrix: invalid address, permission failure, `.CONST` overlap, object-bound warning/strict mode, uninitialized-origin warning/strict mode, unaligned access, and no-partial-mutation behavior.
- Debugger state matrix: valid/invalid commands for `not-loaded`, `ready`, `running`, `paused`, `stopped-at-breakpoint`, `waiting-for-input`, `halted`, `faulted`, and `terminated`.
- URL import matrix: version, duplicate parameters, canonical base64url, decompression failure, schema failure, unsafe/local-only settings, and atomic no-partial-apply behavior.
- CodeMirror offset matrix: ASCII, CRLF/LF, non-ASCII, combining marks, emoji/surrogate pairs, stale source snapshots, line-only diagnostics, and diagnostics with no source location.

## Appendix C. Suggested AI Assistant Workflow

For each implementation phase: read the current source-of-truth spec and guide, implement only the requested phase, add or update tests before reporting completion, run the required native/Node/browser/Wasm test categories for that phase, report exact commands and results, and explicitly state any diagnostics, edge cases, or non-goals that were verified. Environment-dependent skipped suites require explicit reasons. Do not implement future-phase syntax or behavior as convenience work.

## Appendix D. Definition of v1 Complete

Version 1 is complete only when every implemented feature is documented in the supported syntax reference, every supported syntax entry maps to at least one passing test fixture, every known unsupported/non-goal example is represented in a corpus or diagnostic test, release artifacts and hashes are generated, required test suites pass, and the known-limitations report distinguishes deferred features from explicit non-goals.
