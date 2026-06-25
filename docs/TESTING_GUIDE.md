# Testing Guide

This document describes practical ways to build, run, and manually test the MASM32 educational simulator from a local development checkout. It expands the short README commands with Windows CMD, Visual Studio Developer PowerShell, Linux shells, Node harness, native diagnostic producer, and browser/Wasm workflows.

The examples assume commands are run from the repository root.

Current milestone:

- Phase 87 - Irvine32 Crlf

Phase 79 adds tests for automatic LOCAL frame setup and release on selected-entry procedures and direct-CALL helper procedures, including root `RET`, helper `RET`, helper `RET imm16`, virtual Irvine32 `exit`, source-level `LEAVE` plus `RET`, deterministic visible byte initialization, local-object descriptor lifetime, branch/fallthrough rejection with `local-frame-entry-unsupported`, frame-state failures with `invalid-frame-state`, LOCAL stack-capacity failures with `stack-overflow`, rendered Simulator Messages, output-contract metadata, and no public `memoryChanges` rows for automatic LOCAL stack housekeeping.

Phase 87 regression coverage verifies virtual Irvine32 `Crlf` source-run Program Console newline output, exact `\n` byte serialization, zero-argument `INVOKE Crlf`, register/flag/memory preservation, no-partial Program Console limit failures through `Crlf`, precise Crlf invalid-source diagnostics, rendered Simulator Messages for those diagnostics, and continued deferral of non-target Irvine32 routines. Phase 86 regression coverage verifies deterministic Program Console byte/line limits, no-partial-append output-limit failure behavior, source-run limit-status serialization, rendered `console-output-limit-exceeded` Simulator Messages, and separate Program Console and Simulator Messages streams while preserving Phase 84 limited same-file user-procedure `INVOKE` DWORD argument lowering, Phase 83 helper-level `ADDR symbol` preparation, Phase 82 accepted zero-argument same-file user-procedure `INVOKE`, Phase 81 limited `PROTO` metadata, Phase 80 LOCAL operand behavior, accepted limited `OPTION NOKEYWORD` behavior, Phase 79 automatic LOCAL frame setup/release, Phase 78 parser LOCAL metadata, Phase 77 `PROC USES` runtime behavior, Phase 74 `RET imm16`, Phase 73 `LEAVE`, Phase 72A `PUSH`/`POP`, helper `CALL`/plain `RET`, root `RET`, call-depth limits, procedure fallthrough, entry-end compatibility, Irvine32 `exit`, empty Program Console output for no-output and diagnostic paths, deterministic Program Console limit fields, and future-owned deferral of executable source-level `ADDR` outside accepted INVOKE argument forms, unsupported user-procedure INVOKE argument forms, `OFFSET local`, executable `PROTO` behavior beyond Phase 84 validation, pointer or unnamed prototype parameters, `VARARG`, scaled-index LOCAL addressing, named runtime parameters, calling conventions, QWORD/SQWORD executable LOCAL memory operands, and non-Crlf Irvine32 routine dispatch.


Static documentation checks for active source-of-truth text should reject stale milestone-context phrases unless they appear in historical reports, changelogs, quoted forbidden-pattern examples, or allowlist entries used by the check itself. The minimum phrases to scan are:

- `As of the source-of-truth revision after Phase`
- `next canonical guide phase is Phase`
- `Current behavior through Phase`
- `Current Phase <N> behavior`
- `current behavior through Phase`
- `as of Phase <N>`

The check must not reject the active status field label `Current milestone:` when it appears in the compact active-status payload format defined by the implementation guide. The check must not reject historical milestone reports under `docs/history/`.

When a guide phase describes behavior it will replace, the preferred wording is `pre-<phase> accepted behavior` or `behavior replaced by this phase`.

### Browser default editor source-run smoke test

Any milestone that changes `web/index.html`, the default editor program, source-run syntax support, source-run output metadata, unsupported-feature diagnostics, `INVOKE`, `ADDR`, `PROTO`, `LOCAL`, Irvine32 virtual includes, or the `exit` terminator must include a default-editor source-run smoke test.

The test must:

1. extract the exact text content of the default editor textarea from `web/index.html`;
2. HTML-decode the text exactly as the browser would expose it to the editor;
3. run that source through the same native source-run path used by the diagnostic JSON producer or source-run fixture tests;
4. assert successful source-run completion;
5. assert that the runtime/source-run behavior metadata and source-run output contract match the current milestone;
6. assert that no `unsupported-feature`, `unsupported-invoke`, parser error, runtime error, or terminal diagnostic appears in Simulator Messages;
7. assert that Program Console and Simulator Messages remain separate streams.

Static substring checks are not enough for the default editor program. The exact default source shown to the user must parse and run successfully unless a milestone deliberately changes the product to show an error-focused teaching example and documents that decision in the spec, guide, supported syntax reference, and milestone report.

### INVOKE/ADDR sequencing regression tests

When any phase touches `INVOKE`, `ADDR`, `PROTO`, procedure metadata, procedure calls, procedure arguments, source-run output metadata, or the browser default editor program, tests must prove the roadmap boundary explicitly.

For the zero-argument INVOKE phase:

- `INVOKE Helper` succeeds for a same-file zero-argument user procedure.
- The same program using `call Helper` still succeeds and has equivalent observable control-flow behavior.
- `INVOKE Helper, 1` fails with the phase-owned unsupported-arguments diagnostic.
- `INVOKE Helper, ADDR msg` fails and does not treat ADDR as executable.
- `INVOKE Helper, OFFSET msg` fails and does not treat INVOKE arguments as executable.
- `INVOKE ExitProcess, 0` fails as an external/API non-goal.
- Irvine32 routine INVOKE fails as recognized but not executable unless the phase explicitly owns the routine.
- Wrong-kind targets such as data symbols, equates, registers, immediates, and labels reject without execution.

For the ADDR-preparation phase:

- helper/parser tests may prove `ADDR msg`, `ADDR constSymbol`, `ADDR dataQuestionSymbol`, and `ADDR localVar` can become address-valued future argument records;
- source-run tests must prove `INVOKE Helper, ADDR msg` still refuses execution;
- source-run tests must prove `INVOKE Helper, ADDR localVar` still refuses execution;
- `ADDR` outside the owned helper/future-argument context remains rejected;
- invalid ADDR targets preserve source line, column, byte offset, and span length;
- `.CONST`, `.DATA?`, object-bounds, and uninitialized-origin policy are not bypassed by address preparation.

For the INVOKE-argument phase:

- tests must prove right-to-left argument lowering;
- tests must prove checked stack writes and no partial mutation on failed validation;
- tests must prove argument-count mismatch against `PROTO` metadata and exact cleanup validation against procedure `RET imm16` metadata;
- tests must prove cleanup mismatch is diagnosed instead of silently leaking stack bytes;
- tests must prove ADDR passes an address and does not bypass `.CONST`, `.DATA?`, object-bound, or uninitialized-origin policy;
- tests must prove source-level named PROC parameter access remains unsupported unless the phase explicitly implements it;
- tests must include rendered Simulator Messages for every new or changed user-visible diagnostic family.

### PROTO, INVOKE, and ADDR diagnostic coverage matrix

For every phase that adds or changes a PROTO, INVOKE, or ADDR diagnostic, the milestone tests must include both structured diagnostic coverage and rendered Simulator Messages coverage unless the phase report explicitly justifies why a diagnostic is internal-only.

Structured diagnostic coverage must assert:

- diagnostic code;
- severity/kind;
- human-readable message text or stable message substring;
- source line;
- source column;
- byte offset;
- span length;
- related location, when the diagnostic depends on a previous declaration or paired metadata item.

Rendered Simulator Messages coverage must assert:

- final UI-visible text for each diagnostic family added or changed by the phase;
- no Program Console output for refused-execution diagnostics;
- separation between Program Console and Simulator Messages;
- no cascading token-level noise when one unsupported feature line has already been classified clearly.


### Implemented virtual Irvine32 routine diagnostic coverage matrix

For every phase that implements a virtual Irvine32 routine, tests must distinguish invalid use of the implemented routine from use of still-deferred Irvine32 routines.

Invalid source forms for an implemented routine must be covered by both structured diagnostics and rendered Simulator Messages. These diagnostics must normally be `assembly-error` diagnostics because the routine is implemented and the user's source form is invalid or incomplete. Do not classify implemented-routine misuse as `unsupported-feature` unless the failing behavior is a genuinely unimplemented future source form and the guide explicitly says so.

For Phase 87 `Crlf`, structured and rendered tests must cover:

| Fixture purpose | Expected category | Expected code | Required behavior |
|---|---|---|---|
| `CALL Crlf` without `INCLUDE Irvine32.inc` | `assembly-error` | `missing-irvine32-include` | Refuses execution, writes no Program Console output, points at the `Crlf` token, and does not report `unsupported-irvine32-routine`. |
| `INVOKE Crlf` without `INCLUDE Irvine32.inc` | `assembly-error` | `missing-irvine32-include` | Refuses execution, writes no Program Console output, points at the `Crlf` target token, and does not report `unsupported-irvine-invoke`. |
| `INVOKE Crlf, eax` after `INCLUDE Irvine32.inc` | `assembly-error` | `invalid-irvine32-argument-count` | Refuses execution, writes no Program Console output, points at the first offending argument, and does not report `unsupported-invoke-argument`. |
| Bare `Crlf` after `INCLUDE Irvine32.inc` | `assembly-error` | `invalid-irvine32-call-form` | Refuses execution, writes no Program Console output, points at the bare `Crlf` token, and does not report `unsupported-irvine32-routine`. |
| `CALL WriteString` or another still-deferred Irvine32 routine | `unsupported-feature` | `unsupported-irvine32-routine` | Proves that implementing `Crlf` did not execute future Irvine32 routines. |
| `INVOKE WriteString` or another still-deferred Irvine32 routine | `unsupported-feature` | `unsupported-irvine-invoke` | Proves that implementing zero-argument `INVOKE Crlf` did not implement general Irvine32 `INVOKE` dispatch. |

Each fixture must assert all of the following:

- diagnostic category;
- diagnostic code;
- exact primary message text or an explicitly approved stable message substring;
- source line;
- source column;
- byte offset;
- span length;
- terminal status;
- absence of `execution-complete`;
- empty Program Console output;
- absence of unrelated cascading token-level diagnostics;
- rendered Simulator Messages text for every user-visible diagnostic added or changed by the phase.

A test is insufficient if it asserts only that parsing failed. The test must prove the final user-visible diagnostic taxonomy.

At minimum, the following families must be covered when introduced or changed:

- `invalid-proto-declaration`;
- `unsupported-proto-type`;
- `unsupported-external-proto`;
- `duplicate-proto`;
- `proto-proc-mismatch`;
- `invalid-invoke-target`;
- `invoke-arguments-not-supported-yet`;
- `invoke-argument-count-mismatch`;
- `unsupported-external-invoke`;
- `unsupported-irvine-invoke`;
- `missing-irvine32-include`;
- `invalid-irvine32-argument-count`;
- `invalid-irvine32-call-form`;
- `invalid-addr-target`;
- `addr-outside-invoke`;
- `unknown-addr-symbol`;
- `unsupported-addr-expression`;
- `unsupported-invoke-argument`;
- `invoke-cleanup-mismatch`;
- `invoke-argument-width-unsupported`.

## 1. Prerequisites

### Required for native and Node tests

Install or make available:

- Python 3
- Node.js
- A C99-compatible compiler usable from the command line

On Windows with Visual Studio, the recommended compiler for the existing test runner is Clang from the Visual Studio C++ toolchain, because the runner uses GCC/Clang-style flags such as `-std=c99`, `-Wall`, `-Wextra`, `-Werror`, and `-pedantic`.

On Linux, GCC or Clang can be used. The examples below use GCC by default and also show the optional `CC=clang` form.

In the Visual Studio Installer, install:

- Desktop development with C++
- C++ Clang tools for Windows

Then use either:

- **Developer PowerShell for Visual Studio**
- **x64 Native Tools Command Prompt for VS**
- The Visual Studio integrated terminal opened through **View -> Terminal**, when it is configured as a Developer PowerShell environment

Check tools:

```cmd
py --version
node --version
clang --version
```

PowerShell equivalent:

```powershell
py --version
node --version
clang --version
```

Linux shell equivalent:

```sh
python3 --version
node --version
cc --version
```

Optional Linux package examples:

Debian/Ubuntu:

```sh
sudo apt update
sudo apt install build-essential python3 nodejs npm
```

Fedora:

```sh
sudo dnf install gcc python3 nodejs npm
```

Arch Linux:

```sh
sudo pacman -S base-devel python nodejs npm
```

If you prefer Clang on Linux, install `clang` and run the aggregate tests with `CC=clang`.

### Required for browser/Wasm rebuilds

To rebuild `web/dist/masm32_sim_core.js` and `web/dist/masm32_sim_core.wasm`, install and activate Emscripten/emsdk so `emcc` is available.

Check:

```cmd
emcc --version
```

PowerShell equivalent:

```powershell
emcc --version
```

Linux shell equivalent:

```sh
emcc --version
```

If `emcc` is unavailable, native and Node tests can still be run, but Wasm artifacts cannot be rebuilt locally.

## 2. Running the full test suite

The aggregate test runner builds native C test binaries, runs them, builds the native diagnostic JSON producer, and runs Node-based protocol/formatter/diagnostic-rendering tests.

### Windows CMD

```cmd
set CC=clang
py scripts\run_tests.py
```

### Developer PowerShell / Visual Studio Terminal

```powershell
$env:CC = "clang"
py scripts\run_tests.py
```

### Linux shell

Using the default compiler (`cc`):

```sh
python3 scripts/run_tests.py
```

Using GCC explicitly:

```sh
CC=gcc python3 scripts/run_tests.py
```

Using Clang explicitly:

```sh
CC=clang python3 scripts/run_tests.py
```

Expected final output:

```text
All implemented milestone tests passed.
```

### Focused runner groups

The default command remains valid and is equivalent to full aggregate verification:

```sh
python3 scripts/run_tests.py
python3 scripts/run_tests.py --all
```

The same commands work on Windows with `py`:

```cmd
py scripts\run_tests.py
py scripts\run_tests.py --all
py scripts\run_tests.py --diagnostics
```

Focused groups are available for timeout-safe verification:

```sh
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
```

Official native and source-run subgroups are available when a broad native or source-run group is too large:

```sh
python3 scripts/run_tests.py --native-parser
python3 scripts/run_tests.py --native-exec
python3 scripts/run_tests.py --native-memory-layout
python3 scripts/run_tests.py --native-diagnostics-policy
python3 scripts/run_tests.py --native-control-flow
python3 scripts/run_tests.py --source-run-core
python3 scripts/run_tests.py --source-run-diagnostics
python3 scripts/run_tests.py --source-run-settings
python3 scripts/run_tests.py --source-run-memory-layout
python3 scripts/run_tests.py --source-run-control-flow
```

Group ownership:

- `structure`: repository structure, expected files, public header comments, Doxygen/static source-shape checks, and milestone metadata checks.
- `native`: native C unit/parser/executor/helper tests that do not require Node and do not run the source-run integration binary.
- `source-run`: native source-run JSON/integration coverage in `tests/core/test_wasm_source_run.c`. This group is intentionally runnable independently from native, web, protocol, static, and rendered diagnostic tests.
- `web`: browser-side Node module tests that do not require the native diagnostic producer.
- `diagnostics`: native diagnostic JSON producer build plus exact rendered Simulator Messages checks.
- `protocol`: worker/protocol schema tests separated from general web tests.
- `static`: runner help, group-name documentation, timeout-policy, failure-reporting, fixture-inventory consistency checks, and selected current-status or stale-wording guards.

Output-only corrective phases still require tests across the surfaces they affect.

For a phase that changes rendered Simulator Messages ordering, run and update:

- structured source-run tests that verify diagnostic/status object order;
- structured source-run tests that verify diagnostic/status object counts;
- rendered web tests that verify exact Simulator Messages text and blank-line placement;
- protocol tests if the phase changes or explicitly preserves protocol ordering;
- static documentation checks when active docs describe the ordering.

For a phase that changes final register display formatting, run and update:

- web formatter tests for exact rendered register output;
- tests that prove display-only register separator rows are not source-run JSON objects;
- protocol tests if the protocol field order or schema is deliberately changed;
- source-run tests only if structured register payloads are deliberately changed.

Display-only grouping must not be tested by changing VM semantics, parser behavior, diagnostic codes, source-run success/failure status, Program Console output, or memory/register values.

Output controls:

```sh
python3 scripts/run_tests.py --quiet --source-run
python3 scripts/run_tests.py --verbose --diagnostics
```

`--quiet` prints group start/status lines, the final compact summary, and failure details only. `--verbose` prints subprocess commands, captured subprocess output, fixture-level details, milestone fixture names, and expected rendered diagnostic lines where the runner owns that inventory.

`--quick` is a smoke subset, not full verification:

```sh
python3 scripts/run_tests.py --quick
```

A milestone report that uses only `--quick` must say that full verification was not performed. Do not report `All implemented milestone tests passed` from a quick-only run.

Milestone reports must distinguish these fields when applicable:

```text
Current milestone:
Runtime/source-run MASM behavior phase:
Output/message-ordering cleanup phase:
Artifact/test-infrastructure cleanup phase:

Aggregate tests:
Focused test groups:
Subgroup tests:
Timed-out commands:
Skipped dependency checks:
Browser/Wasm rebuild status:
Browser/Wasm smoke status:
Served web/dist compatibility status:
Whether aggregate success was claimed:
```

Rules:

- Do not report aggregate success if `--all` timed out or was not run.
- Do not report browser/Wasm rebuild verification if `emcc` was unavailable.
- Do not infer that checked-in `web/dist` is stale solely because `emcc` was unavailable. Treat checked-in artifact-content evidence, Emscripten rebuild verification, and browser/Wasm smoke verification as separate facts.
- Do not report browser/Wasm artifact verification if `emcc` was unavailable and no artifact-compatibility identifier was checked.
- Do not treat a maintenance-only repository/archive phase as a runtime/source-run MASM behavior phase.
- Do not change runtime/source-run behavior metadata merely to align it with a documentation, output-ordering, artifact-compatibility, or test-infrastructure corrective phase.


### Timeout-safe verification policy

A timeout is not a pass. A timeout is also not by itself proof of a simulator regression. When a command times out, report it explicitly as `TIMED OUT` and continue with the smallest official runner groups that still preserve coverage for the changed surface.

The broad focused groups remain supported, but they are not guaranteed to complete in every hosted assistant/container environment. If `scripts/run_tests.py --all --quiet` times out, run the documented broad focused groups. If a broad focused group times out, use the official subgroup commands for that group.

Do not replace exact JSON assertions, exact rendered Simulator Messages assertions, parser assertions, or VM assertions with smoke-only checks to avoid timeout. Test decomposition must preserve coverage.

Manual compile/run fallback is allowed only as a documented fallback when official commands cannot complete in the active environment. A milestone report that uses manual fallback must state:

- which official command timed out or could not run;
- which manual command was used instead;
- which fixture or fixture family the manual command covered;
- which coverage remains unverified, if any.

### Official subgroup command ownership

The following subgroup names are implemented public runner commands. Each subgroup must appear in `scripts/run_tests.py --help`, must be documented here, and must preserve the same exact assertions as the broad group that owns it. Run source-run/native subgroups when a broad source-run or native command is too large for the active environment.

Required native subgroups from Phase 71B1:

| Command | Ownership |
|---|---|
| `--native-parser` | Native lexer, parser, symbol, and parser-level semantic-validation tests. |
| `--native-exec` | Native CPU, flag, and API smoke tests that are not better owned by memory/layout or control-flow subgroups. |
| `--native-memory-layout` | Native memory, layout, data-section, object-map, checked-memory, and data-image tests. |
| `--native-diagnostics-policy` | Native configurable diagnostic-policy registry and lookup behavior. |
| `--native-control-flow` | Native executor regression tests that own VM control-flow risk for future control-transfer phases. |

Required source-run subgroups from Phase 71B1:

| Command | Ownership |
|---|---|
| `--source-run-core` | Small successful programs, source-run setup, final-state basics, API result-buffer behavior, and core source-run parser/runtime integration not better owned by another source-run subgroup. |
| `--source-run-diagnostics` | Source-run structured diagnostics, parser/runtime error paths, unsupported forms, capacity diagnostics, terminal failure serialization, and exact diagnostic-preservation fixtures. |
| `--source-run-settings` | Source-run settings serialization, invalid settings, CASEMAP behavior, compatibility-policy behavior, startup-mode behavior, seeded-startup behavior, and policy-driven behavior. |
| `--source-run-memory-layout` | Source-run data declarations, symbol offsets, pointer widths, memory operands, layout modes, object/section validation, `.CONST`, `.DATA?`, uninitialized-origin metadata, planned-read checks, and memory-change attribution. |
| `--source-run-control-flow` | Source-run labels, direct branches, conditional branches, instruction-limit/watchdog paths, selected-entry startup, direct user-procedure `CALL`, helper `RET`, selected-entry root `RET`, procedure-boundary regressions, and pseudo-EIP display/rejection fixtures. |

Required diagnostic subgroups from Phase 71A1:

| Command | Ownership |
|---|---|
| `--diagnostics-json` | Native diagnostic JSON producer build and structured diagnostic payload checks. |
| `--diagnostics-rendered-call-ret` | Rendered Simulator Messages for CALL, RET, root RET, helper RET, procedure fallthrough, and code-end diagnostics. |
| `--diagnostics-rendered-memory` | Rendered Simulator Messages for memory bounds, object bounds, uninitialized reads, `.CONST`, `.DATA?`, and checked memory helper diagnostics. |
| `--diagnostics-rendered-directives` | Rendered Simulator Messages for directives, sections, `PROC`, `ENDP`, `END`, include/compatibility directives, and parser-level directive diagnostics. |
| `--diagnostics-rendered-compatibility` | Rendered Simulator Messages for compatibility settings, unsupported-but-recognized MASM forms, and non-goal boundary diagnostics. |
| `--diagnostics-rendered-arithmetic` | Rendered Simulator Messages for ADD/SUB/CMP/ADC/SBB/NEG and related arithmetic diagnostics. |
| `--diagnostics-rendered-shift-rotate` | Rendered Simulator Messages for SHL/SHR/SAL/SAR/ROL/ROR/RCL/RCR diagnostics. |
| `--diagnostics-rendered-mul-div` | Rendered Simulator Messages for MUL/IMUL/DIV/IDIV diagnostics. |
| `--diagnostics-rendered-runtime` | Rendered Simulator Messages for runtime terminal diagnostics not better owned by a more specific family. |

Implemented source-run/native subgroups from Phase 71B1 preserve the broad groups:

- `--native` runs every native subgroup internally.
- `--source-run` runs every source-run subgroup internally.
- `--all` runs the broad groups, and the broad groups preserve complete subgroup coverage.

The broad `--diagnostics` group calls the diagnostic JSON subgroup and each rendered diagnostic subgroup internally. A broad group may call subgroups internally, but it must not silently skip a subgroup. If a subgroup cannot run because of an unavailable dependency, the report must mark that subgroup as `SKIPPED - dependency unavailable` and name the dependency.

Failure output for subgroup and broad-group execution must identify the failing group, the failing subgroup where applicable, the failing command, the subprocess exit code, and a bounded stdout/stderr tail. Source-run fixture failures include the fixture name whenever the failing path can identify it.

### Required timeout-aware command reporting

Milestone reports must separate command results into these categories:

- aggregate command, if run;
- broad focused group commands, if run;
- official subgroup commands, if run;
- manually compiled fallback commands, if any;
- skipped dependency checks, such as unavailable `emcc`;
- commands that timed out;
- commands not run.

Use explicit status words:

```text
PASS
FAIL
TIMED OUT
SKIPPED - dependency unavailable
NOT RUN
```

Do not replace `TIMED OUT` with `FAIL` unless the command returned an actual failure result. Do not replace `TIMED OUT` with `SKIPPED` unless the command was intentionally not run because of a documented unavailable dependency. Do not omit timed-out commands from the command list.

### Rendered diagnostic coverage must not be weakened for timeout reasons

Rendered Simulator Messages tests are part of the diagnostic contract. If a rendered diagnostic suite becomes too large for one hosted command, split it by fixture family. Do not convert exact rendered-output checks into presence-only checks, smoke tests, or manual visual inspection unless the milestone report explicitly marks the affected coverage as unverified.

For phases that change diagnostic text, severity, code, source location, span length, terminal status, or message ordering, the report must name the rendered diagnostic subgroup or exact rendered test command that verified the change.

Official diagnostic subgroup examples:

```sh
python3 scripts/run_tests.py --diagnostics-json
python3 scripts/run_tests.py --diagnostics-rendered-call-ret
python3 scripts/run_tests.py --diagnostics-rendered-memory
python3 scripts/run_tests.py --diagnostics-rendered-directives
python3 scripts/run_tests.py --diagnostics-rendered-compatibility
python3 scripts/run_tests.py --diagnostics-rendered-arithmetic
python3 scripts/run_tests.py --diagnostics-rendered-shift-rotate
python3 scripts/run_tests.py --diagnostics-rendered-mul-div
python3 scripts/run_tests.py --diagnostics-rendered-runtime
```

These commands run exact assertions from the same diagnostic harness used by `--diagnostics`; they are not smoke tests and are not manual visual checks.

### Source-run fixture inventory

The source-run fixture inventory is intentionally a navigation aid. It does not duplicate every assertion in `tests/core/test_wasm_source_run.c`. It records the major fixture families owned by the broad `--source-run` group and by the official Phase 71B1 source-run subgroups so future assistants do not split, weaken, or remove them without an explicit test-runner maintenance phase.

| Fixture or family | Focused group | Category | Current disposition |
| --- | --- | --- | --- |
| minimal source execution sample | `source-run`, `source-run-core` | focused success fixture | preserved in the broad source-run group and owned by the core subgroup |
| lexer/parser diagnostic source-run failures | `source-run`, `source-run-diagnostics` | focused error fixture | preserved in the broad source-run group and owned by the diagnostics subgroup |
| memory/layout and automatic layout programs | `source-run`, `source-run-memory-layout` | regression fixture | preserved in the broad source-run group and owned by the memory-layout subgroup |
| uninitialized-read and memory-validation policy programs | `source-run`, `source-run-memory-layout` | warning/notice fixture | preserved in the broad source-run group and owned by the memory-layout subgroup unless the fixture is primarily settings-policy coverage |
| phase51-layout-fixed-automatic-equivalence | `source-run`, `source-run-memory-layout` | integration smoke fixture | preserved in the broad source-run group and owned by the memory-layout subgroup |
| phase51-const-permission-precedence | `source-run`, `source-run-memory-layout` | integration smoke fixture | preserved in the broad source-run group and owned by the memory-layout subgroup |
| phase51-uninitialized-rmw-warning | `source-run`, `source-run-memory-layout` | integration smoke fixture | preserved in the broad source-run group and owned by the memory-layout subgroup |
| phase51-inc-dec-source-smoke through phase51-ror-source-smoke | `source-run`, `source-run-core` | integration smoke fixture | preserved in the broad source-run group and owned by the core subgroup unless a narrower diagnostic, memory-layout, settings, or control-flow concern owns the fixture |
| phase53e-ui-settings-policy-routing | `source-run`, `source-run-settings` | focused settings fixture | preserved in the broad source-run group and owned by the settings subgroup |
| phase56-div-source-run-coverage | `source-run`, `source-run-core`, `source-run-diagnostics` | focused success/error/regression fixture family | preserved in the broad source-run group and split by exact fixture owner between core and diagnostics subgroups |
| phase57-idiv-source-run-coverage | `source-run`, `source-run-core`, `source-run-diagnostics` | focused success/error/regression fixture family | preserved in the broad source-run group and split by exact fixture owner between core and diagnostics subgroups |
| direct branch, conditional jump, label, CALL, RET, selected-entry, and watchdog fixtures | `source-run`, `source-run-control-flow` | control-flow regression fixture family | preserved in the broad source-run group and owned by the control-flow subgroup |
| startup mode, seeded startup, compatibility-notice, CASEMAP, and policy-setting fixtures | `source-run`, `source-run-settings` | settings and policy fixture family | preserved in the broad source-run group and owned by the settings subgroup |

Large individual MASM source fixtures remain intentionally grouped under `--source-run` and are now also reachable through one official source-run subgroup. The Phase 51 instruction-family programs remain integration smoke fixtures. The remaining embedded source strings in `tests/core/test_wasm_source_run.c` are preserved as success, error, warning/notice, edge-case, settings, memory-layout, control-flow, or regression fixtures. No fixture should be moved, renamed, split, or weakened unless a future test-runner maintenance phase explicitly owns that change and updates this inventory.

The test runner writes native test binaries under:

```text
build\tests\
```

On Windows, these binaries are emitted with `.exe` suffixes, for example:

```text
build\tests\test_object_map.exe
build\tests\diagnostic_json_producer.exe
```

On Linux, the same binaries are extensionless, for example:

```text
build/tests/test_object_map
build/tests/diagnostic_json_producer
```

## 3. Running one native test binary directly

First run the full test suite once so binaries are built, or let the failing aggregate run build the binary before stopping.

### Windows CMD

```cmd
build\tests\test_object_map.exe
```

### Developer PowerShell

```powershell
.\build\tests\test_object_map.exe
```

### Linux shell

```sh
./build/tests/test_object_map
```

Successful tests usually print a short pass message, for example:

```text
test_object_map passed
```

## 4. Running Node tests directly

The Node tests cover browser-side protocol and formatting code without launching a browser.

### Protocol formatter tests that do not need native producer

CMD:

```cmd
node tests\web\test_protocol.mjs
node tests\web\test_formatters.mjs
```

PowerShell:

```powershell
node tests\web\test_protocol.mjs
node tests\web\test_formatters.mjs
```

Linux shell:

```sh
node tests/web/test_protocol.mjs
node tests/web/test_formatters.mjs
```

### Diagnostic rendering test

`tests/web/test_diagnostic_rendering.mjs` needs the native diagnostic producer. The aggregate runner passes its path automatically. If running it manually, set `MASM32_DIAGNOSTIC_JSON_PRODUCER` first.

CMD:

```cmd
set MASM32_DIAGNOSTIC_JSON_PRODUCER=%CD%\build\tests\diagnostic_json_producer.exe
node tests\web\test_diagnostic_rendering.mjs
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_JSON_PRODUCER = "$PWD\build\tests\diagnostic_json_producer.exe"
node tests\web\test_diagnostic_rendering.mjs
```

Linux shell:

```sh
MASM32_DIAGNOSTIC_JSON_PRODUCER="$PWD/build/tests/diagnostic_json_producer" node tests/web/test_diagnostic_rendering.mjs
```

Expected result: the script exits successfully after printing its `PASS ...` lines.

## 5. Manual source-run tests with the diagnostic JSON producer

The native diagnostic producer runs MASM-like source through the same C source-run path used by the Wasm-facing API and prints JSON. This is useful for testing parser, execution, diagnostics, and non-default test modes without the browser.

Build it with:

CMD:

```cmd
set CC=clang
py scripts\run_tests.py
```

PowerShell:

```powershell
$env:CC = "clang"
py scripts\run_tests.py
```

Linux shell:

```sh
python3 scripts/run_tests.py
```

or explicitly:

```sh
CC=gcc python3 scripts/run_tests.py
```

Then run a source file through the producer.

### CMD, from a `.asm` file

```cmd
type program.asm | build\tests\diagnostic_json_producer.exe
```

### PowerShell, from a `.asm` file

```powershell
Get-Content .\program.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

### Linux shell, from a `.asm` file

```sh
cat program.asm | ./build/tests/diagnostic_json_producer
```

or:

```sh
./build/tests/diagnostic_json_producer < program.asm
```

### CMD, with an input file argument

```cmd
build\tests\diagnostic_json_producer.exe program.asm
```

### PowerShell, with an input file argument

```powershell
.\build\tests\diagnostic_json_producer.exe .\program.asm
```

### Linux shell, with an input file argument

```sh
./build/tests/diagnostic_json_producer program.asm
```

The output is JSON. Search it for message codes such as:

```text
execution-complete
assembly-error
runtime-error
simulator-warning
object-bounds-warning
```

## 6. Selecting diagnostic producer modes

The diagnostic producer supports environment variables for internal/test-only modes.

### Memory validation mode

After Phase 53C, omitted/default source-run behavior emits teaching warnings
for uninitialized-origin reads. To preserve the older silent region-only
behavior in local diagnostic-producer tests, explicitly select `off` or
`region-only`.

Explicit memory-validation values include:

```text
off
region-only
allocated-object-warnings
allocated-object-strict
uninitialized-read-warnings
uninitialized-read-strict
```

Milestone 37 added allocated-object warning mode:

```text
allocated-object-warnings
```

CMD:

```cmd
set MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_MEMORY_VALIDATION = "allocated-object-warnings"
```

Linux shell:

```sh
export MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

For one command only on Linux:

```sh
MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings ./build/tests/diagnostic_json_producer program.asm
```

Reset to Phase 53C/53D/53E default teaching diagnostics and compatibility notices:

CMD:

```cmd
set MASM32_DIAGNOSTIC_MEMORY_VALIDATION=
```

PowerShell:

```powershell
Remove-Item Env:MASM32_DIAGNOSTIC_MEMORY_VALIDATION -ErrorAction SilentlyContinue
```

Linux shell:

```sh
unset MASM32_DIAGNOSTIC_MEMORY_VALIDATION
```

Explicit opt-out example on Linux:

```sh
MASM32_DIAGNOSTIC_MEMORY_VALIDATION=off ./build/tests/diagnostic_json_producer program.asm
```

Undefined modeled-flag consumer diagnostics also default to warnings after
Phase 53C. Phase 53D also emits default `simulator-notice` compatibility messages for accepted no-op, metadata-only, and limited-behavior MASM constructs. Phase 53E exposes these policies in the browser UI as local page-session settings while preserving the same backend defaults. To preserve the older silent consumer behavior in a local diagnostic
producer run, set:

```text
MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE=off
```


### Compatibility notices

After Phase 53D, accepted MASM compatibility constructs such as `.686`, `.model flat, stdcall`, `.stack 4096`, `INCLUDE Macros.inc`, `TITLE`, `SUBTITLE`, and `PAGE` emit non-fatal `simulator-notice` messages by default. These notices are informational, do not write Program Console output, and do not change VM execution. Active semantic constructs such as `INCLUDE Irvine32.inc`, `OPTION CASEMAP`, `.DATA?`, and `.CONST` are not reported as generic no-ops.

After Phase 53E, browser users can turn compatibility notices off and can select the existing memory range validation, uninitialized-read, and undefined-flag-use policies from the page. These browser settings are local preferences for the page session; share URLs are not implemented yet and do not currently encode diagnostic settings. The default browser profile remains region-only memory validation, uninitialized-read warn, undefined-flag-use warn, and compatibility notices on.

### Layout mode

Some layout tests use diagnostic producer layout modes. Current supported environment usage includes values such as fixed/default, automatic, seeded randomized, or fresh randomized depending on the implemented milestone and test harness support.

CMD example:

```cmd
set MASM32_DIAGNOSTIC_LAYOUT_MODE=automatic
```

PowerShell example:

```powershell
$env:MASM32_DIAGNOSTIC_LAYOUT_MODE = "automatic"
```

Linux shell example:

```sh
export MASM32_DIAGNOSTIC_LAYOUT_MODE=automatic
```

For one command only on Linux:

```sh
MASM32_DIAGNOSTIC_LAYOUT_MODE=automatic ./build/tests/diagnostic_json_producer program.asm
```

Reset:

CMD:

```cmd
set MASM32_DIAGNOSTIC_LAYOUT_MODE=
```

PowerShell:

```powershell
Remove-Item Env:MASM32_DIAGNOSTIC_LAYOUT_MODE -ErrorAction SilentlyContinue
```

Linux shell:

```sh
unset MASM32_DIAGNOSTIC_LAYOUT_MODE
```

## 7. Milestone 37 manual test programs

These programs demonstrate allocated-object warning mode. They are not visible in the served website by default because the browser UI does not currently expose a validation-mode selector.

Before running them, enable warning mode.

CMD:

```cmd
set MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_MEMORY_VALIDATION = "allocated-object-warnings"
```

Linux shell:

```sh
export MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

### 7.1 Valid `.data` region gap read warns and continues

Save as `phase37_gap_read.asm`:

```asm
.data
x DWORD 1234h

.code
main PROC
    mov esi, OFFSET x
    mov eax, DWORD PTR [esi + 4]
main ENDP
END main
```

CMD:

```cmd
type phase37_gap_read.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_gap_read.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_gap_read.asm
```

Expected:

- JSON contains `object-bounds-warning`
- JSON contains `execution-complete`
- final `EAX` is zero because the read is from valid zero-filled data-region storage outside a declared object

### 7.2 Valid `.data` region gap write warns and continues

Save as `phase37_gap_write.asm`:

```asm
.data
x DWORD 0

.code
main PROC
    mov esi, OFFSET x
    mov DWORD PTR [esi + 4], 99
    mov eax, DWORD PTR [esi + 4]
main ENDP
END main
```

CMD:

```cmd
type phase37_gap_write.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_gap_write.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_gap_write.asm
```

Expected:

- JSON contains `object-bounds-warning`
- JSON contains `execution-complete`
- final `EAX` is `00000063h / 99`

### 7.3 Access into another declared object does not warn

Save as `phase37_other_object.asm`:

```asm
.data
var1 DWORD 1111h
arr1 DWORD 2222h, 3333h

.code
main PROC
    mov esi, OFFSET var1
    mov eax, DWORD PTR [esi + 4]
main ENDP
END main
```

CMD:

```cmd
type phase37_other_object.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_other_object.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_other_object.asm
```

Expected:

- JSON does not contain `object-bounds-warning`
- JSON contains `execution-complete`
- final `EAX` is `00002222h / 8738`

This confirms that Milestone 37 is not provenance tracking. The pointer starts from `var1`, but the final access lands wholly inside `arr1`.

### 7.4 Access spanning adjacent objects warns and continues

Save as `phase37_span_objects.asm`:

```asm
.data
a BYTE 11h
b BYTE 22h

.code
main PROC
    mov esi, OFFSET a
    mov ax, WORD PTR [esi]
main ENDP
END main
```

CMD:

```cmd
type phase37_span_objects.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_span_objects.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_span_objects.asm
```

Expected:

- JSON contains `object-bounds-warning`
- JSON contains `execution-complete`
- final `AX` is `2211h`

### 7.5 `.CONST` permission failure takes precedence

Save as `phase37_const_write.asm`:

```asm
.CONST
limit DWORD 10

.code
main PROC
    mov esi, OFFSET limit
    mov DWORD PTR [esi], 20
main ENDP
END main
```

CMD:

```cmd
type phase37_const_write.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_const_write.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_const_write.asm
```

Expected:

- JSON contains a runtime permission/write diagnostic
- JSON does not contain `execution-complete`
- object warnings do not replace the permission error

### 7.6 Invalid address remains a runtime error, not an object warning

Save as `phase37_invalid_address.asm`:

```asm
.code
main PROC
    mov esi, 0
    mov eax, DWORD PTR [esi]
main ENDP
END main
```

CMD:

```cmd
type phase37_invalid_address.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_invalid_address.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_invalid_address.asm
```

Expected:

- JSON contains a runtime invalid-address diagnostic
- JSON does not contain `object-bounds-warning`
- JSON does not contain `execution-complete`

## 8. Rebuilding and serving the browser version

Use this when browser-visible behavior changed and Emscripten is available.

### Windows CMD

```cmd
scripts\windows\build_wasm.cmd
scripts\windows\serve_web.cmd
```

Stop the server:

```cmd
scripts\windows\stop_web.cmd
```

### Developer PowerShell

You may run CMD scripts from PowerShell:

```powershell
.\scripts\windows\build_wasm.cmd
.\scripts\windows\serve_web.cmd
```

Or use the PowerShell server helpers when available:

```powershell
.\scripts\windows\serve_web.ps1
```

Stop the server:

```powershell
.\scripts\windows\stop_web.ps1
```

### Linux shell

If executable bits were preserved:

```sh
./scripts/build_wasm.sh
python3 -m http.server 8000 --directory web
```

If executable bits were not preserved:

```sh
bash scripts/build_wasm.sh
python3 -m http.server 8000 --directory web
```

Then open:

```text
http://localhost:8000/
```

Stop the Python server with `Ctrl+C` in the terminal where it is running.

After serving, open the local URL printed by the script or server and run manual browser programs in the editor.

For Milestone 56 unsigned `div` verification, run:

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

Expected final state includes `EAX = 0000000Eh`, `EDX = 00000002h`, no memory-change rows, and an `execution-complete` Simulator Messages entry. Divide-by-zero and quotient-overflow programs should report runtime errors in Simulator Messages without updating the quotient or remainder registers.

Milestone 37 note: allocated-object warning/strict validation began as a test/configuration-facing mode. After Phase 53E, the browser diagnostic settings panel exposes the same existing declared-object bounds warning and strict-stop policies as optional Memory range validation choices. Default browser execution remains region-only.

## 9. Troubleshooting

### `test_object_map.exe` exits with `3221225725`

On Windows this code is commonly `0xC00000FD`, a stack overflow. Ensure the current source has object-map test buffers in static storage rather than large local stack allocations.

### `fopen` or `getenv` is deprecated and fails under `-Werror`

When compiling against the Visual Studio/UCRT headers, standard C functions may be annotated as deprecated unless `_CRT_SECURE_NO_WARNINGS` is defined. The native diagnostic producer should define it before including CRT headers.

### Node cannot spawn the diagnostic producer

Set the producer path explicitly.

CMD:

```cmd
set MASM32_DIAGNOSTIC_JSON_PRODUCER=%CD%\build\tests\diagnostic_json_producer.exe
node tests\web\test_diagnostic_rendering.mjs
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_JSON_PRODUCER = "$PWD\build\tests\diagnostic_json_producer.exe"
node tests\web\test_diagnostic_rendering.mjs
```

Linux shell:

```sh
MASM32_DIAGNOSTIC_JSON_PRODUCER="$PWD/build/tests/diagnostic_json_producer" node tests/web/test_diagnostic_rendering.mjs
```

### `emcc` is not found

Native and Node tests can still run, but Wasm/browser artifacts cannot be rebuilt. Activate emsdk first, then rerun the build script.

On Linux, this usually means the emsdk environment was not activated in the current shell. A typical setup uses commands similar to:

```sh
source /path/to/emsdk/emsdk_env.sh
emcc --version
```

### Linux cannot find `node`, `python3`, or `cc`

Install the missing package through the system package manager, then reopen the terminal or refresh the shell path. The aggregate runner expects `python3`, `node`, and a C compiler available on `PATH`.

### Linux build fails with permissions on scripts

Some archives may not preserve executable bits. Invoke scripts through their interpreter:

```sh
python3 scripts/run_tests.py
bash scripts/build_wasm.sh
```

### Phase 57 signed IDIV verification

Phase 57 adds runtime source behavior for signed one-operand `idiv`, so verification must include native executor tests, parser tests, source-run JSON tests, rendered Simulator Messages tests, protocol phase metadata tests, and static current-status checks. Memory-divisor `idiv` tests must prove planned-read validation runs before quotient/remainder mutation for strict uninitialized-read and object-boundary policies.

Recommended focused commands after Phase 57 changes:

```bash
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
```

Run `python3 scripts/run_tests.py --all` when practical. If the aggregate command times out in a hosted assistant/container environment, report focused group results separately and do not claim an aggregate pass.

## 10. Recommended checklist before reporting a milestone complete

1. Run the full aggregate test suite.
2. Run any new targeted native tests directly if they touched low-level C modules.
3. Run the Node formatter/diagnostic harness if diagnostic wording or JSON changed.
4. Run manual diagnostic-producer programs for new source-visible behavior.
5. Rebuild Wasm and manually test the browser if the feature is user-visible in the served site.
6. Report:
   - changed files;
   - tests added;
   - commands run;
   - pass/fail result;
   - limitations such as missing `emcc` or browser-invisible backend modes.

### Phase 53B section-boundary validation controls

The native diagnostic JSON producer also accepts these local test-only environment variables:

```text
MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION=off|warn|strict
MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION=off|warn|strict
```

`warn` emits `section-capacity-violation` or `section-image-violation` as a simulator warning and continues when Level 1 memory validation succeeds. `strict` emits the same code as a runtime error before mutation. Milestone 53B added these controls to the test/source-run path. Phase 53E exposes the same existing section-capacity and section-image policies in the browser Memory range validation setting. Defaults still leave section-capacity and section-image validation off unless the user selects one of those modes.

## Stack, CALL, RET, and procedure milestone verification note

Future stack/procedure milestones must separate focused executor tests from complete source-run programs.

Use executor/native tests when a phase implements an instruction whose full source-run program would require a later phase. Examples:

- Phase 69 - Direct CALL to User Procedures can be tested by stepping one CALL and checking the return token, stack write, instruction pointer, modeled flags, and no-partial-mutation behavior.
- Phase 70 - RET Execution and Return Address Validation can be tested with executor-level stack setup before complete source-level CALL/root termination programs exist.
- Phase 73 - LEAVE Instruction can be tested with direct frame setup even when source-level setup instructions are also available.
- Phase 74 - RET imm16 Instruction can be tested with source-level argument pushes because Phase 72A already implements the required 32-bit PUSH subset.

Use source-run tests only when all required source-level dependencies already exist. Examples:

- A complete CALL/RET source program requires both CALL and RET behavior and a defined termination path.
- A source program using `push ebp` requires source-level PUSH support.
- A source program using argument pushes before `ret imm16` is valid after Phase 74 because source-level PUSH support is already available from Phase 72A.

Do not implement future instructions merely to make a source-run fixture pass. If a fixture depends on a future phase, either move that fixture to the owning future phase or mark it as an integration regression that becomes active only after the dependency phase is complete.

For stack/procedure diagnostics, tests must prove:

- structured diagnostic fields include code, severity, line, column, byte offset, and span length where source-backed;
- rendered Simulator Messages match expected wording;
- fatal stack/procedure failures do not emit `execution-complete`;
- no-partial-mutation behavior covers registers, modeled flags, flag-validity metadata, memory bytes, Program Console output, memory-change rows, stack pointer, instruction pointer, and call-depth metadata where applicable;
- runtime/source-run MASM behavior metadata is advanced only when the target phase introduces or explicitly redefines accepted runtime/source-run behavior; project current-milestone status may advance for accepted fixture, documentation, or conformance-cleanup phases while preserving the prior runtime/source-run behavior phase.


For Phase 71A root RET mode, Phase 71C code-stream fallthrough, Phase 71D procedure-fallthrough policy, Phase 71E entry-procedure end mode, Phase 71F fallthrough fixture migration, and Phase 72 call-depth resource protection specifically, tests must prove:

- a source-run fixture where the selected entry procedure contains only `ret` completes successfully in default `rootRetMode = "masm32-compatible"`;
- a source-run fixture where explicit `rootRetMode = "masm32-compatible"` preserves default root `RET` success;
- a source-run fixture where `rootRetMode = "strict-call-frame"` rejects selected-entry root `RET` with `root-ret-disallowed-by-mode`;
- strict root `RET` rejection explains that root-code-stream `RET` has no caller-supplied return address, suggests MASM32-compatible root RET mode or the supported Irvine32 `exit` routine, and does not repeat the literal strict-mode setting value;
- strict root `RET` rejection does not read `[ESP]`, mutate `ESP`, validate a pseudo-EIP token, create public `memoryChanges`, or emit success;
- a source-run fixture where the selected entry procedure calls a helper, the helper returns, and the selected entry procedure then uses root `ret` completes successfully in compatible mode;
- selected-entry procedure `ENDP` fallthrough is no longer successful by default after Phase 71C; Phase 71A root RET tests should use explicit `RET` or Irvine32 `exit` when they are not testing code-stream falloff;
- Phase 71F opposite fixtures cover default `code-stream` fallthrough from `main` into a later procedure and the matching `stop-at-entry-end` non-fallthrough case;
- Phase 71F opposite fixtures cover empty selected-entry default `code-stream` `code-fell-off-end` and matching `stop-at-entry-end` success;
- Phase 71F opposite fixtures cover `procedureFallthroughPolicy` values `warn`, `off`, and `error`;
- Phase 71F opposite fixtures cover explicit Irvine32 `exit` termination in both entry-end modes;
- Phase 71F opposite fixtures cover root `RET` behavior as governed by `rootRetMode`, not by `entryProcedureEndMode`;
- Phase 71F static checks reject active fixture descriptions that describe implicit selected-entry `ENDP` default success as current behavior;
- Phase 72 call-depth tests cover default `callDepthLimit = 64`, accepted values `1`, `64`, and `4096`, rejected invalid values, `call-depth-exceeded`, `invalid-call-depth-limit`, mutation rollback for rejected calls, structured source-run JSON, rendered Simulator Messages, and absence of call-trace metadata;
- a called non-entry procedure that falls through without RET reports `procedure-fell-through`;
- root-code-stream RET default success does not read `[ESP]`;
- root-code-stream RET reached after selected-entry procedure fallthrough also succeeds in compatible mode without reading `[ESP]`;
- selected-entry/root-code-stream RET default success does not mutate `ESP`;
- selected-entry/root-code-stream RET does not emit `invalid-address`;
- selected-entry/root-code-stream RET does not emit `invalid-return-address`;
- selected-entry/root-code-stream RET emits exactly one successful terminal status;
- ordinary helper RET still emits checked-memory diagnostics when `[ESP]` must be read and is unreadable;
- ordinary helper RET still emits `invalid-return-address` for readable invalid tokens;
- rendered Simulator Messages show root RET completion exactly once;
- rendered Simulator Messages for non-entry fallthrough include `procedure-fell-through`;
- static documentation checks assert selected-entry root RET default success, optional strict root RET rejection, and called non-entry procedure fallthrough are implemented after Phase 71A is accepted;
- static documentation checks continue to list RETF, `OFFSET local`, scaled-index LOCAL addressing, QWORD/SQWORD executable LOCAL memory operands, general procedure-frame features beyond Phase 84 limited INVOKE DWORD arguments, Phase 83 helper-level ADDR preparation, Phase 82 zero-argument INVOKE, and Phase 80 LOCAL operand access, executable `PROTO` behavior beyond Phase 84 validation, unsupported `INVOKE` argument forms, pointer or unnamed prototype parameters, VARARG, executable source-level ADDR outside accepted INVOKE arguments, calling-convention behavior, and Irvine32 callable routine dispatch as deferred unless later phases implement them, while verifying that Phase 73 `LEAVE`, Phase 74 `RET imm16`, Phase 76 `PROC USES` parsing metadata, Phase 77 `PROC USES` runtime save/restore, Phase 78 `LOCAL` declaration metadata, Phase 79 automatic LOCAL frame allocation/lifetime, Phase 80 supported LOCAL operand resolution/addressing, Phase 81 limited `PROTO` metadata, Phase 82 zero-argument same-file user-procedure `INVOKE`, Phase 83 helper-level `ADDR symbol` record preparation, and Phase 84 limited same-file user-procedure INVOKE DWORD argument lowering are documented as implemented. The same checks should reject stale active fixed-layout examples that cite stack top `0x00800000` instead of the current fixed-layout stack top `0x00900000`, except inside historical files, explicit stale-value allowlists, or audit-only draft text.

## Current-status documentation clutter checks

Static or manual documentation checks should protect current-status surfaces from becoming milestone-history dumps. They should also protect active source-of-truth text from stale status regressions. When a target phase touches the relevant docs, its milestone report must state whether each applicable check was automated, manually reviewed, or intentionally deferred.

The checks should apply to active current-status/current-scope sections in:

- `README.md`;
- `docs/SUPPORTED_SYNTAX.md` opening status block;
- `docs/BUILDING_AND_DEVELOPMENT.md`;
- browser/protocol status strings if they are stored in source files;
- tests that assert current-status wording.

Recommended checks:

1. README active current-status section must contain exactly one active `Current milestone:` label. It must contain one active `Runtime/source-run MASM behavior phase:` label only when the current milestone does not change accepted source syntax, parsed operands, instruction semantics, VM execution behavior, procedure semantics, memory semantics, register semantics, source-run success/failure behavior, or implemented runtime features.
2. If README contains quoted examples or templates with additional status labels, those must be clearly outside the active current-status section and should be avoided unless necessary.
3. README active current-status/current-scope text must not contain milestone-report headings such as:
   - `Exact requirements implemented`
   - `Assumptions used`
   - `TODO-style note disposition`
   - `Files changed`
   - `Tests added`
   - `Commands used to test`
4. README active current-status text must not contain a rolling sequence of several milestone paragraphs beginning with `Phase <N> adds`, `Phase <N> added`, `Phase <N> remains`, or `After Phase <N>`.
5. `docs/BUILDING_AND_DEVELOPMENT.md` must not define implemented MASM feature behavior beyond a short status note and links to the proper behavior documents.
6. `docs/SUPPORTED_SYNTAX.md` may contain detailed syntax sections, but its opening status block must remain concise and must not become a milestone ledger.
7. Detailed historical text removed from README must be preserved in `docs/MILESTONE_HISTORY.md` or milestone reports when it has historical value.
8. If automated static checks are deferred, the milestone report must list the manual checks performed and preserve static-check implementation as future documentation-maintenance work.
9. Active unsupported/deferred directive lists must not contain bare `EQU` or `=` without distinguishing implemented numeric equates from unsupported text-substitution equates.
10. Active unsupported-feature or recovery-policy text must not describe implemented `.DATA?`, `.CONST`, numeric equates, `cmp`, direct `jmp`, equality conditional jumps, signed relational conditional jumps, or unsigned relational conditional jumps as unsupported or future behavior.
11. Active current-status text must preserve the distinction between current CALL target classification/procedure-entry metadata and future executable CALL transfer.
12. Active current-status text must preserve the distinction between the implemented instruction-count watchdog and future active-time or wall-clock watchdog behavior.
13. Active diagnostic examples should include severity and source-span fields when they are examples of source-tied diagnostics. If an example intentionally omits those fields, it must be labeled as abbreviated.
14. Active diagnostic category names and wording must not imply PE loading, object linking, import-library linking, Windows loader behavior, host include-file loading, or host filesystem access as simulator behavior.
15. Active roadmap sections must state that roadmap themes are not a current-support table and that current support comes from the canonical guide, supported-syntax document, latest repository state, latest accepted milestone report, and current tests.
16. Optional or future virtual-filesystem wording must state that it would be simulator-owned educational behavior only and must not imply direct browser filesystem access, host filesystem access, Windows file API behavior, PE loading, or import-library behavior.
17. Active Irvine32 routine-contract text that mentions `EIP`, including future `DumpRegs` behavior, must state that `EIP` is the displayed pseudo-EIP/control-state value, not a native address, PE/RVA/linker address, raw VM instruction index, source byte offset, or source-writable register value.
18. Active user-facing diagnostic message strings in source files and active rendered-message expected strings in non-historical tests must not explain current rejected behavior with milestone-relative phrases such as:
    - `not supported in Phase`
    - `outside Phase`
    - `Phase <number> accepts only`
    - `deferred to Phase <number>`

    This check must ignore `docs/history/`, milestone reports, audit notes, documentation sections explicitly labeled as historical legacy material, and explicit forbidden-string lists used by the check itself.
19. Active source-of-truth text must not require a root-return sentinel such as `VM_RETURN_TOKEN_ROOT` or `0xFFFFFFFFu` unless an accepted owning guide phase defines the sentinel, validation rules, collision-proofing, user-memory exposure rules, JSON behavior, structured tests, and rendered Simulator Messages tests.
20. Active supported-syntax text must not contain an isolated statement that the simulator “does not implement RET” after the project has accepted plain near helper `RET` and selected-entry root `RET`. Any Irvine32 include limitation must distinguish between “Irvine32.inc does not add Irvine32 routine-call behavior or additional RET forms” and “plain near RET is implemented separately.”
21. Active milestone-history navigation must not preserve stale limitation lists that contradict later accepted phases. If historical context is necessary, replace stale lists with a short note that points readers back to `docs/SUPPORTED_SYNTAX.md`, `docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md`, the README current-status section, and the latest accepted milestone report.
22. If a corrective diagnostic-copy phase changes exact source-run-visible diagnostic wording, source-run tests must verify the corresponding output-contract token. For Phase 71B, the expected token is `phase-71b-source-run-output-contract-v1` unless the phase report explicitly documents a different accepted token. For Phase 71E, the expected token is `phase-71e-entry-procedure-end-mode-output-contract-v1` because the phase changes the public source-run settings contract and selected-entry terminal behavior. For Phase 72A, the expected token is `phase-72a-push-pop-stack-output-contract-v1` because the phase changes accepted syntax, runtime stack semantics, structured JSON memory-change rows, rendered Simulator Messages, and protocol interpretation for source-level PUSH/POP stack transfers. For Phase 73, the expected token is `phase-73-leave-output-contract-v1` because the phase changes accepted syntax, runtime stack-read semantics, structured diagnostics, rendered Simulator Messages, and protocol interpretation for source-level LEAVE frame teardown. For Phase 74, the expected token is `phase-74-ret-imm16-output-contract-v1` because the phase changes accepted syntax, runtime return-cleanup semantics, structured diagnostics, rendered Simulator Messages, and protocol interpretation for near `RET imm16` caller cleanup. For Phase 75, the expected token is `phase-75-proc-metadata-output-contract-v1` because the phase changes structured parser/source-run diagnostics, rendered Simulator Messages, and protocol interpretation for `PROC` declaration diagnostics. For Phase 76, the expected token is `phase-76-proc-uses-metadata-output-contract-v1` because the phase changes accepted `PROC USES` parsing metadata, structured diagnostics, rendered Simulator Messages, and protocol interpretation for runtime deferral when execution enters a `USES` procedure. For Phase 77, the expected token is `phase-77-proc-uses-runtime-output-contract-v1` because the phase changes runtime `PROC USES` save/restore semantics, structured diagnostics, rendered Simulator Messages, and protocol interpretation for automatic USES stack failures. For Phase 78, the expected token is `phase-78-local-metadata-output-contract-v1` because the phase changes accepted `LOCAL` parser metadata, structured diagnostics, rendered Simulator Messages, and protocol interpretation for LOCAL declaration errors while leaving runtime local allocation and local operand resolution deferred. For Phase 78A, the expected token is `phase-78a-nokeyword-output-contract-v1` because the phase changes accepted limited `OPTION NOKEYWORD:<LOOP>`, `OPTION NOKEYWORD:<OFFSET>`, and `OPTION NOKEYWORD:<LOOP OFFSET>` syntax, parser keyword classification, structured diagnostics, rendered Simulator Messages, and user-symbol table behavior while leaving runtime VM behavior unchanged. For Phase 79, the expected token is `phase-79-local-frame-output-contract-v1` because the phase changes automatic LOCAL frame allocation/release, structured frame diagnostics, rendered Simulator Messages, runtime metadata, and source-run behavior for procedures with accepted LOCAL metadata while leaving source-level LOCAL operands deferred. For Phase 80, the expected token is `phase-80-local-operand-output-contract-v1` because the phase changes accepted source-level LOCAL operand execution, frame-relative LOCAL addressing, `LEA` local-address behavior, runtime metadata, source-run behavior, and protocol interpretation. For Phase 81, the expected token is `phase-81-proto-metadata-output-contract-v1` because the phase changes accepted `PROTO` metadata, structured `PROTO` diagnostics, rendered Simulator Messages, runtime metadata, source-run behavior, and protocol interpretation. For Phase 82, the expected token is `phase-82-invoke-zero-argument-output-contract-v1` because the phase changes accepted zero-argument `INVOKE` syntax, INVOKE target diagnostics, rendered Simulator Messages, runtime metadata, source-run behavior, and protocol interpretation. For Phase 83, the expected token is `phase-83-addr-preparation-output-contract-v1` because the phase changes helper-level ADDR argument-record diagnostics, source-level ADDR rejection diagnostics, rendered Simulator Messages, runtime metadata, source-run behavior, and protocol interpretation while preserving source-level INVOKE-with-arguments rejection. For Phase 84, the expected token is `phase-84-invoke-dword-argument-output-contract-v1` because the phase changes accepted source-level INVOKE DWORD arguments, checked stack-lowering semantics, cleanup mismatch diagnostics, rendered Simulator Messages, runtime metadata, source-run behavior, and protocol interpretation. For Phase 85, the expected token is `phase-85-program-console-stream-output-contract-v1` because the phase changes the public source-run JSON stream layout by adding a separate Program Console object beside Simulator Messages while preserving empty Program Console output for current no-output and diagnostic paths. For Phase 86, the expected token is `phase-86-program-console-output-limits-contract-v1` because the phase changes Program Console limit enforcement, adds limit-status fields to source-run JSON, and adds the `console-output-limit-exceeded` resource-limit diagnostic. For Phase 87 - Irvine32 Crlf, the expected token is `phase-87-irvine32-crlf-contract-v1` because the phase adds public virtual Irvine32 `Crlf` Program Console newline output for `call Crlf` and zero-argument `INVOKE Crlf`.

These checks should not forbid normal references to phase numbers in canonical guide sections, milestone history, milestone reports, or explicitly historical audit notes.

