# Milestone History

This file preserves long-form milestone-history material outside the README. Phase 57B updated it into a navigation layer for humans and AI assistants while keeping historical detail available.

The README is intentionally concise. This file carries the longer milestone history that had previously made the README difficult to use as a landing page.

Source-of-truth rule:

- [`FULL_IMPLEMENTATION_SPEC.md`](FULL_IMPLEMENTATION_SPEC.md) remains the canonical source for product boundaries, stable behavior, cross-cutting rules, and current/future/non-goal distinctions.
- [`INCREMENTAL_IMPLEMENTATION_GUIDE.md`](INCREMENTAL_IMPLEMENTATION_GUIDE.md) remains the canonical source for phase numbering, phase tasks, required tests, and acceptance criteria.
- [`SUPPORTED_SYNTAX.md`](SUPPORTED_SYNTAX.md) remains the current reference for accepted MASM32 Educational Mode syntax and diagnostics.
- [`BUILDING_AND_DEVELOPMENT.md`](BUILDING_AND_DEVELOPMENT.md) owns detailed local serving, build, prerequisite, Visual Studio, and development workflow guidance.
- Milestone reports, archived repository states, and this history file are historical evidence. They do not replace or override the canonical specification and implementation guide.

Current status at Phase 57O:

Repository/archive milestone:
Phase 57O - Explicit-Width NOP Encoding-Operand Forms

Runtime/source-run MASM behavior phase:
Phase 57O - Explicit-Width NOP Encoding-Operand Forms

Status interpretation:

Phase 57O accepts selected MASM-compatible 16-bit and 32-bit `nop` encoding operands as source-level no-ops: register forms such as `nop ax` and `nop eax`, plus explicit `WORD PTR`, `SWORD PTR`, `DWORD PTR`, and `SDWORD PTR` memory-looking forms such as `nop DWORD PTR [eax]`. Accepted register-form NOP operands are not read or written, and accepted memory-looking forms reuse existing memory-addressing grammar but do not evaluate addresses, read or write memory, perform planned memory validation, emit memory diagnostics, or create memory-change rows. Zero-operand `nop` remains supported. Unsupported `nop` operand forms still report stable diagnostics that explain invalid immediates, missing memory sizes, invalid operand sizes, or that real x86 byte encoding is not emitted. Phase 57O does not add real opcode bytes, `.code` byte images, disassembly, PE/object/linker behavior, browser controls, or Program Console output. Phase 57M segment/group-symbol diagnostics, Phase 57L `.code` memory-access diagnostics, existing `.CONST` declaration diagnostics, `.CONST` write protection, read-time `uninitialized-read` diagnostics, Phase 57H final-register `[unchanged]` display markers, Phase 57G seeded uninitialized-storage visible-byte settings, and Phase 57F seeded register/flag startup remain available.

## How to use this file

Use this file as a compact orientation layer before reading detailed reports or planning new work:

1. Confirm the repository/archive milestone and runtime/source-run MASM behavior phase above.
2. Use the concise ledger below to understand the implemented project shape.
3. Use detailed milestone reports when available for implementation evidence, changed files, tests, assumptions, and non-goals.
4. Use the canonical specification and implementation guide for implementation decisions. Historical entries here are not authority when they conflict with the current canonical docs.

## Detailed milestone report references

Detailed milestone reports are usually stored as separate handoff artifacts named with this pattern:

```text
Milestone <phase> report.md
```

Examples from recent handoff artifacts include:

```text
Milestone 57 report.md
Milestone 57-CORR1 report.md
Milestone 57-CORR2 report.md
Milestone 57A report.md
```

Those reports are implementation history and evidence. They are useful for changed-file lists, exact tests run, assumptions, and regression risks, but they do not replace the canonical spec/guide pair.

## Concise milestone ledger

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

Not implemented yet:

- Control flow, stack, call/ret, Irvine32 routines other than the virtual `exit` terminator, debugger stepping, scaled-index addressing, carry rotates, macros, runtime high-level condition expressions, full expression parsing beyond the Milestone 29 compile-time subset, and Windows API behavior.
- Extended 32-bit / 64-bit register behavior.

See `SUPPORTED_SYNTAX.md` for the current supported subset, scheduled features, and recognized unsupported constructs.
