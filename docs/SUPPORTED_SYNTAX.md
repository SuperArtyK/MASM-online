# Supported MASM32 Educational Simulator Syntax

Current milestone:

- Phase 71B - User-Facing Diagnostic Milestone-Wording Cleanup

Runtime/source-run MASM behavior phase:

- Phase 71A - Optional Root RET Strictness Mode

This document describes the currently accepted MASM32 Educational Mode syntax, rejected forms, diagnostics, and future/deferred syntax. Phase 71B is diagnostic-copy cleanup only; it removes stale milestone-number wording from active user-facing diagnostics without adding or removing accepted MASM syntax, rejected forms, runtime diagnostics, source-run behavior, Wasm API behavior, or browser behavior.

Current direct control-transfer support includes direct `jmp label`, equality conditional jumps, signed relational conditional jumps, unsigned relational conditional jumps, direct near user-procedure `call ProcedureName`, and plain near `ret`/`RET` with no operands.

Implemented procedure and termination behavior for the active runtime/source-run MASM behavior phase:

- `END entryName` selects the source-run entry procedure.
- Execution starts inside the selected entry procedure.
- Direct near `CALL` to accepted user procedures is implemented for the supported direct form. Direct `call ProcedureName` is executable only when `ProcedureName` resolves to a user `PROC` entry under the active user-symbol `CASEMAP` policy. A successful direct user-procedure `CALL` writes a pseudo-EIP return token to `ESP - 4`, updates `ESP`, and transfers to the target procedure entry through the checked VM control-flow path. The current public source-run output contract does not expose that implicit write as a user-visible `memoryChanges` row.
- Plain near `RET` is implemented for helper returns through simulator pseudo-EIP return tokens.
- Selected-entry root `RET` succeeds by default in MASM32-compatible root RET mode.
- Optional strict root RET mode rejects selected-entry root `RET` with `root-ret-disallowed-by-mode`.
- Virtual Irvine32 `exit` is an explicit successful terminator where recognized.
- Ordinary selected-entry `ENDP` fallthrough currently terminates successfully as an educational boundary simplification.
- Called non-entry procedure fallthrough is diagnosed with `non-root-procedure-fell-through` by the current CALL/RET termination rules.

The selected-entry `ENDP` success rule is an implemented educational boundary simplification for the active runtime/source-run MASM behavior phase. It is not MASM/x86-like code-stream behavior, and it must not be described as a property of native MASM, a native x86 CPU, a PE loader, a C runtime, or Windows process termination. `PROC`, `ENDP`, and `END` are source/module structure, not executable VM instructions.

Planned corrective phases will replace the default with code-stream fallthrough, add `code-fell-off-end`, add configurable `procedure-fell-through`, and add an explicit entry-procedure auto-stop compatibility setting. Do not describe any of those planned diagnostics or settings as implemented until their owning phases are accepted.

Source-level stack instructions, procedure frames, argument handling, calling-convention behavior, and selected Irvine32 routine dispatch remain deferred unless a later accepted phase explicitly implements them. Simulator-owned rejected CALL target forms remain rejected unless a later accepted phase explicitly changes the specific simulator-owned form; they are not future work merely because they are currently rejected. External/API calls, WinAPI execution, PE loading, object-file linking, import-library behavior, host filesystem access, native x86 execution, and full x86 emulation remain non-goals rather than deferred simulator features. Detailed accepted and rejected forms are listed in the sections below.

Planned fallthrough diagnostics and settings, not implemented until their owning phases are accepted:

| Item | Planned phase | Planned default/status | Meaning |
|---|---:|---|---|
| `code-fell-off-end` | 71C | runtime error | Execution reached the end of the executable code stream without an explicit program terminator. |
| `procedure-fell-through` | 71D | warning by default; configurable `off`/`warn`/`error` | Ordinary sequential execution crossed from one procedure range into another without explicit supported control transfer or termination. |
| `entryProcedureEndMode` | 71E | default `code-stream`; opt-in `stop-at-entry-end` | Compatibility setting for selected-entry `ENDP` auto-stop behavior. |
| `the-front-fell-off` | 71C | notice, required easter egg | Harmless notice emitted only after `code-fell-off-end` when the responsible procedure name is `front` under ASCII case-insensitive comparison. |

The planned `code-fell-off-end` text is:

```text
Execution reached the end of the executable code stream without an explicit program terminator. Did you forget to add RET or Irvine32 exit?
```

The planned `code-fell-off-end` diagnostic is simulator-owned. It must not be described as a native MASM, x86 CPU, PE loader, Windows process, C runtime, or Irvine32 library diagnostic.

After the owning phases are accepted, move these entries from planned/future wording into the implemented behavior table and remove any statement that says they are not current behavior.

### Future stack/procedure syntax not implemented yet

Consult the current instruction-support table in this document for implemented branch and control-flow forms. Direct near `call ProcedureName` to a user `PROC` entry and plain near `ret`/`RET` with no operands are implemented. Do not infer `RET imm16`, unsupported CALL forms, source-level stack instructions, procedure-frame support, Irvine32 routine dispatch, argument handling, or calling-convention behavior from future roadmap examples.

The following stack/procedure features remain future work only if a later accepted milestone explicitly implements them:

- source-level `push`;
- source-level `pop`;
- `leave`;
- `ret imm16`;
- `PROC USES`;
- `LOCAL`;
- `PROTO`;
- `INVOKE`;
- `ADDR`;
- selected Irvine32 routine calls beyond already implemented virtual terminator behavior.

Rejected simulator-owned CALL target forms remain rejected unless a later accepted phase explicitly changes that simulator-owned form. These include register, memory, indirect, immediate, `OFFSET`, ordinary-label, data-symbol, equate, unknown, malformed, and recognized-but-not-yet-executable Irvine32 routine targets.

External/API calls are not simulator-owned deferred CALL forms. They are non-goal target categories and remain outside the simulator unless the canonical specification and guide deliberately revise the project boundary.

The following are not future stack/procedure implementation promises and remain outside the simulator unless the canonical specification and guide are deliberately revised:

- external/API calls;
- WinAPI execution or modeling;
- PE loading;
- object-file linking;
- import-library behavior;
- host filesystem include loading;
- native x86 execution;
- full x86 emulation;
- Windows process, DLL, handle, or kernel behavior.

Important distinction:

- Implemented direct user-procedure `CALL` uses simulator-defined 32-bit VM return tokens, not native x86 addresses.
- Implemented plain near `RET` consumes simulator-defined 32-bit VM return tokens, not native x86 addresses.
- Implemented `CALL` internal return-token writes are not the same feature as source-level `push`.
- Implemented `RET` internal return-token reads are not the same feature as source-level `pop`.
- Future source-level `push` and `pop` require their own implementation phase before documentation examples may rely on them.
- Procedure names are user-defined symbols and follow the active `OPTION CASEMAP` policy.
- Recognized Irvine32 routine and terminator names remain case-insensitive reserved names and are classified through the virtual Irvine32 registry.
- Windows API calls, PE loading, object-file linking, import-library behavior, and host include-file loading remain non-goals.

This section is a roadmap/status note only. It does not make any listed future feature available before its implementation phase is accepted and tested.

### Memory-change source attribution display

Rendered memory-change rows now show the source line that produced each successful memory write, using the same one-based line-number convention as diagnostics. When preserved instruction source text is available, the row shows the original instruction text, for example `a DWORD | line 10: inc a`. The attribution identifies the instruction that performed the write, including direct symbol writes, read-modify-write memory destinations, indirect writes, repeated writes on different lines, and same-value writes that are already recorded by explicit-write tracking. It does not point to data declarations, earlier `OFFSET` address-loading lines, later reads, or synthesized source text. Failed writes, strict planned-access failures, invalid-address or invalid-range writes, and read-only/no-write instructions still create no successful memory-change rows.

### Final register EFLAGS child display

Phase 69B final-register display uses stable high-level educational groups. General registers (`EAX`, `EBX`, `ECX`, `EDX` and their 16-bit/8-bit aliases), index registers (`ESI`, `EDI` and their 16-bit aliases), stack/frame registers (`EBP`, `ESP` and their 16-bit aliases), and control/modeled flag state (`EIP`, `EFLAGS`, `CF`, `ZF`, `SF`, `OF`) are separated by display-only major divider rows. Each major divider row is exactly `-------------------------------------------------------------------`: 67 hyphen characters with no leading or trailing spaces. Inside a high-level group, adjacent parent register families are separated by the display-only parent-family spacer row `       |`: seven spaces followed by one vertical bar and no trailing spaces. Parent-family spacer rows appear only after `AL`, `BL`, `CL`, `SI`, `BP`, and `EIP`; major divider rows appear only after `DL`, `DI`, and `SP`. No separator appears before the first row, after the final modeled flag row, or between aliases of the same parent register family. The Phase 64C `EFLAGS` parent row and subordinate `CF`, `ZF`, `SF`, and `OF` child rows remain implemented. Flag-validity annotations remain future display work, and unmodeled x86 flags such as `PF`, `AF`, `DF`, `IF`, and `TF` are not displayed. This is display formatting only and does not change flag semantics or supported MASM syntax.

### Simulator Messages grouping

Rendered Simulator Messages now use Phase 69B logical ordering. When execution begins and `startup-state-notice` is enabled, the startup notice is serialized and rendered first. Nonfatal pre-execution diagnostics, including compatibility notices and accepted-construct warnings, follow the startup notice. Runtime diagnostics follow nonfatal pre-execution diagnostics. Final `execution-complete` status appears last on success only. When execution does not begin, existing pre-execution diagnostic order is preserved and no startup notice, runtime group, final status group, or `execution-complete` message is emitted. The renderer inserts exactly one blank line between adjacent non-empty rendered groups. Those blank lines are formatter-only and are not source-run JSON diagnostics, Program Console text, memory-change rows, register-separator records, protocol fields, or new status records.

### Reserved words and user-defined symbols

MASM reserved words are not valid user-defined symbols by default. The current simulator rejects declarations whose names conflict with words it already recognizes as reserved, including implemented or recognized instruction mnemonics, registers and aliases, directives, operators, data type names, `PTR` width names, signed `PTR` aliases, virtual include names where recognized, and Irvine32 registry names.

Rejected declaration categories include data symbols, numeric equates, code labels, and procedure names. The diagnostic code is `reserved-word-symbol`, and it points at the declaration name when source location is available. A rejected reserved-word declaration is not inserted into the user-symbol tables.

`OPTION CASEMAP` controls lookup for accepted user-defined symbols only. `OPTION CASEMAP:NONE` does not make reserved words available as user-defined symbols, and reserved-word matching remains case-insensitive. `OPTION NOKEYWORD` remains unsupported until a later explicit keyword-control phase; it must not be treated as enabling reserved-word identifiers.

### Execution limits

Source-run and test-facing callers may set `instructionLimit` to a positive integer. When omitted, the default limit is 1,000,000 executed VM instructions. The simulator counts completed VM instructions, not source lines or labels. If the limit has been reached and another instruction would be fetched, execution stops before that next instruction, emits `instruction-limit-exceeded`, preserves state from completed instructions, and omits `execution-complete`. This is the watchdog used for direct-JMP loops after Phase 61. Active-time watchdog behavior is separate future work owned by Phase 200 - Active Time Watchdog and Worker Responsiveness.

### Parser and source-run capacity limits

Phase 61D documents and tests source-run/parser capacity behavior.

The source-run path is intentionally bounded. Parser/source-run capacity limits are separate from the runtime `instructionLimit` watchdog. A capacity diagnostic such as `token-capacity-exceeded`, `source-text-capacity-exceeded`, `instruction-capacity-exceeded`, `code-label-capacity-exceeded`, `symbol-capacity-exceeded`, `diagnostic-capacity-exceeded`, or `data-capacity-exceeded` occurs while lexing, parsing, lowering, or preparing the source-run result before VM execution begins. These diagnostics are not MASM syntax errors unless malformed source also produced a syntax diagnostic, and they are not evidence that a runtime loop exceeded `instructionLimit`.

When the simulator controls the failure path, capacity failures are reported through structured Simulator Messages with stable diagnostic codes, source line/column/byte-offset/span fields where the failing source token or declaration is known, no Program Console output, no `execution-complete` message, and no hidden partial VM execution. Source-run JSON/result output also has finite buffers; if a result cannot be fully produced, the source-run layer should report a structured capacity or infrastructure error rather than relying on a generic worker failure.

Memory-region capacity limits are distinct from parser/source-run capacity. Data-image expansion such as a very large `DUP` may report `data-capacity-exceeded` during layout before execution, while optional section-capacity and section-image validation policies can later warn or stop for runtime memory accesses that leave a configured section boundary.

Program Console output limits and Simulator Messages output limits are separate UI/result-surface concerns. Program Console is simulated program I/O; Simulator Messages are diagnostics, notices, runtime errors, and execution-status messages. A parser/source-run capacity failure must not write Program Console output.

Worker/browser hard failures are not a supported diagnostic surface. If a failure is caused by a known simulator-owned capacity, it should be represented as a structured source-run or Simulator Messages diagnostic. The simulator remains an educational small-program environment and does not claim arbitrary large MASM program support. Larger-program support requires an explicit later capacity-expansion phase rather than silently removing bounded C99-core capacities.

### Code labels

Ordinary code labels (`name:`) and procedure-entry labels (`name PROC`) are accepted and recorded as parser/source metadata. Consecutive labels before one executable instruction target the same following instruction. Labels before `ENDP`, `END`, or another non-executable boundary are accepted as no-executable-target metadata.

Direct `jmp label` is accepted only when the target is an executable code label or procedure-entry label. It is lowered to branch-target metadata and executes by transferring to the resolved VM instruction index. Procedure-entry targets are direct branch targets only; they do not imply `CALL`, `RET`, stack mutation, argument handling, frames, or calling-convention behavior.

Equality conditional jumps are accepted for direct executable code-label or procedure-entry targets: `je label`, `jz label`, `jne label`, and `jnz label`. `je` and `jz` branch when `ZF = 1`; `jne` and `jnz` branch when `ZF = 0`. They consume `ZF` through the undefined-flag-use diagnostic policy, preserve registers, memory, and modeled flags, and do not create memory-change rows.

Signed relational conditional jumps are accepted for direct executable code-label or procedure-entry targets:

- `jl label` and `jnge label` branch when `SF != OF`.
- `jle label` and `jng label` branch when `ZF = 1` or `SF != OF`.
- `jg label` and `jnle label` branch when `ZF = 0` and `SF = OF`.
- `jge label` and `jnl label` branch when `SF = OF`.

`jl`, `jnge`, `jge`, and `jnl` consume `SF` and `OF`. `jle`, `jng`, `jg`, and `jnle` consume `ZF`, `SF`, and `OF`. No signed relational conditional jump consumes `CF`.

Unsigned relational conditional jumps are accepted for direct executable code-label or procedure-entry targets:

- `ja label` / `jnbe label` branch when `CF = 0` and `ZF = 0`.
- `jae label` / `jnb label` branch when `CF = 0`.
- `jb label` / `jnae label` branch when `CF = 1`.
- `jbe label` / `jna label` branch when `CF = 1` or `ZF = 1`.

`ja`, `jnbe`, `jbe`, and `jna` consume `CF` and `ZF`. `jae`, `jnb`, `jb`, and `jnae` consume `CF` only. No unsigned relational conditional jump consumes `SF` or `OF`.

`cmp` is accepted for implemented register, immediate, and unambiguous memory comparison forms, including `cmp reg, mem`, `cmp mem, reg`, and `cmp mem, imm`. It updates comparison flags without storing a result and uses the same planned-read validation path as other current memory-reading instructions.

Phase 61C - Branch Debugger Dependency Cleanup documents that direct-branch execution and debugger/editor behavior are separate systems; it does not implement debugger behavior. Executable direct branches and direct user-procedure `CALL` do not enable debugger stepping, breakpoint binding, current-instruction highlighting, editor source navigation, CodeMirror gutter behavior, or branch-target editor highlighting or CALL-target editor highlighting; those systems remain future debugger/editor work. `loop`, `ret imm16`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, branch-distance/type overrides such as `SHORT`/`NEAR PTR`/`FAR PTR`, source-level stack instructions, and full procedure/calling-convention execution remain future work. Rejected CALL target forms remain rejected unless a later accepted phase explicitly changes them.

### Unsupported high-level flow diagnostics

High-level MASM flow constructs are recognized but not executable. The parser/source-run path reports source-located unsupported-feature diagnostics for `.IF`, `.ELSE`, `.ENDIF`, `.ELSEIF`, `.WHILE`, `.ENDW`, `.REPEAT`, `.UNTIL`, `.UNTILCXZ`, `.BREAK`, and `.CONTINUE` where they are encountered. Recognized unsupported high-level-flow blocks are skipped for recovery where safe so body instructions do not execute and do not produce unrelated cascaded diagnostics. The simulator does not lower these constructs to labels, jumps, or branches.

### Segment/group and `.code` memory diagnostics

The simulator executes internal IR and source metadata. It does not expose `.code` as user-readable or user-writable simulated program memory, does not emit real x86 opcode bytes, and does not model a PE `.text` image. Attempts to access `.code` through memory operands produce `unsupported-code-memory-access` or, for cross-region protected overlaps, `region-boundary-crossing`.

MASM segment/group names such as `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` are linker/object concepts, not aliases for simulator memory regions. They produce targeted `unsupported-segment-symbol` diagnostics where recognized.

### Startup and data diagnostics

Default successful browser/source-run execution remains deterministic. `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`, modeled flags, and visible bytes of uninitialized storage start at zero while uninitialized-origin metadata is preserved. `ESP` is the exception to the ordinary zero-start register wording: its initial value is initialized from the active stack region's documented empty-stack address. `ESP` remains source-writable through supported explicit register instructions. Displayed `EIP` is also an exception to ordinary register-startup wording: it is derived VM pseudo-code-address control state, beginning at `00401000h` for lowered executable instruction index 0 and advancing by 4 per lowered executable VM instruction. The `startup-state-notice` remains non-fatal and is emitted through Simulator Messages by default.

Source-run/test-facing settings can opt into deterministic seeded startup for the ordinary startup register set: `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `EBP`, and modeled flags. Source-run/test-facing settings can also seed visible bytes of uninitialized-origin storage where that seeded-startup mode is enabled. `ESP` remains source-writable through supported explicit register instructions, but its initial value continues to come from the active stack region's documented empty-stack address rather than from the ordinary zero-start or seeded-startup register path. `EIP` remains derived from VM control flow rather than the zero-start or seeded-startup register path. Initialized `.data` and initialized `.CONST` bytes are not randomized. Accepted `.CONST ?` and `.CONST DUP(?)` storage remains read-only, and its declaration diagnostic is controlled separately from read-time `uninitialized-read` diagnostics.

Source code cannot read, write, address through, use as an instruction operand, or define `EIP`. Attempts such as `mov eip, 1`, `mov eax, eip`, `add eip, 4`, `cmp eip, eax`, `xchg eip, eax`, `lea eip, [eax]`, `nop eip`, `mov eax, [eip]`, `mov eax, [eax + eip]`, `mov [eip], eax`, `EIP DWORD 1`, `EIP = 4`, `EIP:`, or `EIP PROC` produce `invalid-eip-operand`. The diagnostic explains that `EIP` is displayed VM control state, not a source-writable general-purpose register.

### Sections and procedure shape

```asm
.DATA?
; optional deterministic zero-filled uninitialized declarations
.data
; optional writable initialized declarations
.CONST
; optional read-only initialized declarations
.code
main PROC
    ; supported instructions
main ENDP
END main
```

Supported structural forms:

- Optional `.DATA?`, `.data`, and `.CONST` sections before `.code`.
- Required `.code` section.
- `.DATA?` declarations must use `?` or `DUP(?)` uninitialized initializers, including nested `DUP` forms that expand only to `?`. They are emitted as deterministic zero-filled writable storage while retaining metadata that they were originally uninitialized.
- `.CONST` declarations are emitted into read-only storage. Direct writes to known `.CONST` symbols are assembly diagnostics, and calculated-address writes fail at runtime through checked memory permissions.
- Procedure markers using `PROC` and `ENDP` as structural markers.
- `END name` entry-point validation.
- Labels are accepted as parser/source metadata. Implemented direct branch and conditional branch families may target executable code labels or procedure-entry labels according to their instruction-specific rules. Direct user-procedure `CALL` targets user `PROC` entries only.

Procedure declarations create source ranges and procedure-entry symbols distinct from ordinary `label:` code labels. `END entryName` selects the source-run entry procedure. Execution starts at the first executable instruction inside that selected procedure. If the selected entry procedure has no executable instruction, source-run completes successfully without executing another procedure. Helper procedures before the selected entry procedure do not run automatically, helper procedures after it do not run by ordinary fallthrough, and falling off the selected entry procedure at `ENDP` completes successfully. Helper procedures execute only when reached by implemented explicit control transfer such as direct `jmp` or direct user-procedure `CALL`.

### MASM32 header compatibility directives

Accepted before `.data` or `.code` as compatibility no-ops, metadata-only directives, virtual includes, or semantic directives:

- `.386`, `.486`, `.586`, `.686` processor compatibility declarations.
- `.model flat, stdcall`. Other `.model` forms report `unsupported-model`.
- `.stack` and `.stack size`, where the optional size is stored as parser metadata. In automatic layout selected by tests/configuration, the parsed size controls stack region capacity metadata. Runtime startup initializes `ESP` from the active stack region's exclusive high limit. Direct user-procedure `CALL` performs an internal checked 32-bit return-token write at `ESP - 4`. Helper plain near `RET` performs an internal checked 32-bit return-token read from `[ESP]`, validates the token, and increments `ESP` only after validation succeeds. Selected-entry root `RET` succeeds without reading `[ESP]` when no helper return is pending. Source-level stack instructions, `RET imm16`, and procedure frames remain deferred.
- `INCLUDE Irvine32.inc` and `INCLUDE Macros.inc` as virtual built-ins. The simulator does not load host files. `INCLUDE Irvine32.inc` registers known Irvine32 names as virtual metadata for classification and diagnostics. It also enables the zero-operand virtual `exit` terminator. It does not implement Irvine32 routine calls, additional RET forms beyond the separately documented plain near helper `RET` and selected-entry root `RET`, ExitProcess behavior, source-level stack instructions, procedure frames, Program Console routines, other Irvine32 bodies, Windows API behavior, linking, or host include loading. `INCLUDE Macros.inc` remains a virtual no-op and does not populate the Irvine32 registry. `INCLUDELIB` directives are not virtual includes and are not accepted as no-ops; they report linker/library diagnostics such as `unsupported-includelib`, `unsupported-masm32-library`, or `unsupported-windows-api-library`. Basename-only unsupported include files report `unsupported-include`. Host/path-like include operands such as `include \masm32\include\masm32.inc`, `include C:\masm32\include\kernel32.inc`, `include ..\include\file.inc`, `include .\local.inc`, and `include /usr/local/include/file.inc` report host/path diagnostics such as `unsupported-host-include-path`, `unsupported-masm32-library-include`, or `unsupported-windows-api-include` instead of repeated lexer path-separator errors.

Unsupported `INVOKE` diagnostics: `INVOKE` remains unsupported and is not lowered into calls or stack arguments. `ADDR` operands remain unsupported. `StdOut` is diagnosed as an external MASM32 runtime-style routine, `crt_printf` as a C runtime routine, and `ExitProcess` as WinAPI/external process behavior outside the simulator boundary. These diagnostics are emitted through Simulator Messages, refuse execution, and do not write to Program Console.
- `OPTION CASEMAP:ALL` as an explicit selection of the default user-symbol case-insensitive policy.
- `OPTION CASEMAP:NONE` as an exact-case user-symbol policy from that directive forward.
- `OPTION CASEMAP:NOTPUBLIC` is recognized but reports `unsupported-option` because public/external linkage semantics are not implemented.
- Other `OPTION CASEMAP` values report `invalid-option-value`; unrelated unsupported `OPTION` families still report `unsupported-option`.
- `TITLE`, `SUBTITLE`, and `PAGE` as listing/documentation no-ops.

Case policy:

- Instructions, registers and aliases, directives, operators, data type names, `PTR` width names, and virtual include names are always case-insensitive.
- User-defined symbols are case-insensitive by default, equivalent to `OPTION CASEMAP:ALL`.
- Under `CASEMAP:ALL`, definitions whose names differ only by ASCII case are duplicates, and references may use any casing.
- Under `CASEMAP:NONE`, user-symbol definitions and references use exact source spelling from that point forward; different-case symbols may coexist.
- Switching between supported CASEMAP policies emits a non-fatal `casemap-policy-changed` warning when it changes a previously selected supported policy.
- If `CASEMAP:ALL` lookup matches multiple valid exact-case symbols created under `CASEMAP:NONE`, lookup fails with `ambiguous-symbol` rather than choosing one.

Memory diagnostics and optional validation policies:

- The default address/range memory validation mode is region-only. A read succeeds only when the final byte range is wholly inside one readable VM memory region. A write succeeds only when the final byte range is wholly inside one writable VM memory region and does not overlap read-only `.CONST` storage. Invalid address ranges, address overflow, region containment failures, permission failures, and mandatory `.CONST` write protection are routed through central checked VM memory helpers.
- Section-capacity validation, section-image validation, and declared-object bounds validation are optional educational policies. They may warn or stop depending on the selected policy, but they are not enabled by the default region-only address/range validation mode.
- Uninitialized-origin metadata is tracked for `.DATA?`, `?`, `DUP(?)`, and accepted `.CONST ?` / `.CONST DUP(?)` storage. Default source-run behavior emits uninitialized-read teaching warnings while preserving deterministic visible zero bytes.
- Uninitialized-read warning policy is orthogonal to region-only address/range validation. Do not treat uninitialized-read warnings as object-bounds failures, section-capacity failures, section-image failures, `.CONST` permission failures, or invalid-address failures.
- The virtual Irvine32 registry recognizes known routine names for classification and diagnostics, including CALL-target classification. Only the zero-operand virtual `exit` terminator is executable as an Irvine32 behavior in the current subset; direct user-procedure `CALL` is executable only for user `PROC` entries. Calls to Irvine32 routines, source-level stack instructions, RET imm16, and procedure frames remain deferred according to the future-feature sections in this document. Windows/API execution remains outside the simulator boundary as a permanent non-goal unless the canonical specification and guide are deliberately revised.
- The current instruction subset includes the arithmetic, logic, shift, rotate, multiply, divide, conversion, exchange, negation, NOP, effective-address, branch, and direct user-procedure CALL forms listed below.


### Numeric equates and extended constant expressions

Implemented numeric equate forms:

```asm
COUNT = 4
EXTRA EQU 2
```

Numeric equates are compile-time constants. They are stored separately from data labels and are not addressable storage symbols. Supported constant-expression syntax includes:

- numeric literals already supported by the lexer;
- numeric equate identifiers;
- unary `+`, unary `-`, and unary `NOT`;
- byte/word extraction operators `HIGH`, `LOW`, `HIGHWORD`, and `LOWWORD`;
- parentheses;
- binary `+`, `-`, `*`, `/`, `MOD`, `SHL`, `SHR`, `AND`, `OR`, and `XOR`.

Supported constant-expression contexts include:

- instruction immediates, such as `mov eax, COUNT + EXTRA`;
- data initializers, such as `value DWORD COUNT + 1`;
- `DUP` counts, such as `arr DWORD COUNT DUP(0)`;
- constant symbol offsets, such as `nums[COUNT + 4]`;
- `.stack` size metadata, such as `.stack COUNT + 1024`;
- static `OFFSET symbol + constant` forms.

Unsupported constant-expression and text-equate forms:

- `TEXTEQU` and text substitution;
- text or macro-style `EQU` values such as `NAME EQU <text>`;
- recursive or unknown equate references;
- runtime high-level condition operators such as `EQ` in constant contexts;
- non-constant terms such as register names in equate expressions.

### Data declarations

Implemented data types:

- `BYTE`, `DB`
- `WORD`, `DW`
- `DWORD`, `DD`
- `QWORD`, `DQ` for data layout and metadata only; executable 64-bit memory operations remain unsupported in MASM32 Educational Mode.
- `SBYTE`
- `SWORD`
- `SDWORD`
- `SQWORD`

Implemented initializer forms:

- Decimal and hexadecimal numeric literals.
- Negative numeric literals with width validation and two's-complement storage.
- Double-quoted byte strings for `BYTE` / `DB`.
- Single-quoted character literals.
- Packed character literals such as `'AB'`, `'ABC'`, and `'ABCD'` where they fit the destination/data width.
- Comma-separated initializers.
- Flat and nested `DUP`, such as `10 DUP(0)`, `COUNT DUP(0)`, and `ROWS DUP(COLS DUP(0))`.
- `?`, represented deterministically as zero-filled storage while retaining uninitialized metadata.
- Phase 29 constant expressions where a numeric initializer is valid, including in nested `DUP` counts and initializer values.

### Operators and memory operands

Implemented operators:

- `OFFSET symbol` and static `OFFSET symbol + constant`
- `TYPE symbol`
- `LENGTHOF symbol`
- `SIZEOF symbol`

Implemented memory forms:

- Direct symbols: `mov var, 100`, `mov eax, var`.
- Constant byte offsets: `nums[8]`, `nums[COUNT + 4]`, `[nums + 8]`, `[nums - 4]`, `[nums]`, `[nums + 0]`, `nums[0]`.
- Register-indirect forms: `[eax]`, `[ebx]`, `[ecx]`, `[edx]`, `[esi]`, `[edi]`, `[ebp]`, `[esp]`.
- Simple base-plus-or-minus-constant byte displacements for all 32-bit general-purpose base registers. Whitespace is insignificant for this simple form, so spaced and compact spellings are accepted, such as `[eax + 4]`, `[eax+4]`, `[ecx - 4]`, `[ecx-4]`, `[esp + 8]`, and `[esp-8]`.
- Simple symbol/register forms: `array[reg32]`, `[array + reg32]`, where `reg32` is a supported 32-bit general-purpose base register.
- Width overrides: `BYTE PTR`, `WORD PTR`, `DWORD PTR`, `SBYTE PTR`, `SWORD PTR`, and `SDWORD PTR`.
- `QWORD PTR` and `SQWORD PTR` are recognized but executable 64-bit memory operations are rejected in MASM32 Educational Mode.

Array bracket offsets and simple register displacements are byte offsets, not element indexes. Compact subtraction in a supported simple register displacement, such as `[eax-4]`, is the same form as `[eax - 4]`. Scaled-index addressing, base-plus-index addressing, and general arithmetic inside brackets remain unsupported until future phases explicitly implement them.

Memory operands must have a known access width before execution. The width may come from an explicit `PTR` override, declared symbol metadata, symbol-relative metadata, or a register operand in the same instruction when that register unambiguously supplies the width.

Accepted examples because width is explicit or inferable:

```asm
mov [eax], bl
add [eax], ebx
sub [eax], ax
adc [eax], al
sbb [eax], ebx
xchg [eax], cx
test [eax], eax
mov BYTE PTR [eax + 1], 7
mov BYTE PTR [eax+1], 7
mov BYTE PTR [eax - 1], 7
mov BYTE PTR [eax-1], 7
mov WORD PTR [eax + 2], 7
mov WORD PTR [eax+2], 7
mov WORD PTR [eax - 2], 7
mov WORD PTR [eax-2], 7
mov DWORD PTR [eax + 4], 7
mov DWORD PTR [eax+4], 7
mov DWORD PTR [eax - 4], 7
mov DWORD PTR [eax-4], 7
inc BYTE PTR [eax]
dec WORD PTR [eax]
test BYTE PTR [eax], 1
test value, 1
test nums[8], 1
```

Rejected examples because memory width is ambiguous:

```asm
mov [eax], 1
add [eax], 1
sub [eax], 1
adc [eax], 1
sbb [eax], 1
test [eax], 1
test [eax + 4], 1
neg [eax]
inc [eax]
dec [eax]
```

These report `ambiguous-memory-width` with guidance to use `BYTE PTR`, `WORD PTR`, or `DWORD PTR`.

### Instructions

Implemented executable instructions:

- `mov`
- `add`
- `sub`
- `cmp`
- `movsx`
- `movzx`
- `cbw`
- `cwde`
- `cwd`
- `cdq`
- `xchg`
- `neg`
- `nop`
- `adc`
- `sbb`
- `clc`
- `stc`
- `cmc`
- `test`
- `inc`
- `dec`
- `and`
- `or`
- `xor`
- `not`
- `shl`
- `sal`
- `shr`
- `sar`
- `rol`
- `ror`
- `lea`
- `mul`
- `imul`
- `div`
- `idiv`
- `exit` when `INCLUDE Irvine32.inc` is active

`movsx` and `movzx` require a register destination and an 8-bit or 16-bit register or memory source whose width is narrower than the destination. Register-indirect memory sources such as `[esi]` require `BYTE PTR` or `WORD PTR` because the source width is otherwise ambiguous.

Accumulator conversions are no-operand instructions:

- `cbw`: sign-extend `AL` into `AX`
- `cwde`: sign-extend `AX` into `EAX`
- `cwd`: sign-extend `AX` into `DX:AX`
- `cdq`: sign-extend `EAX` into `EDX:EAX`

Ordinary `mov` from signed memory does not sign-extend automatically; use `movsx` when sign extension is required.

Signed `PTR` aliases select memory access width only. `SBYTE PTR`, `SWORD PTR`, and `SDWORD PTR` read and write 8-bit, 16-bit, and 32-bit raw values respectively; they do not make ordinary `mov` sign-extend.

`xchg` supports register/register, register/memory, and memory/register exchanges when both operands have matching widths. Memory operands must use existing direct-symbol, symbol-offset, or `PTR`/register-indirect forms that make the access width unambiguous. `xchg` does not modify tracked flags.

`neg` supports register and memory destinations with 8-bit, 16-bit, or 32-bit widths. It updates the tracked arithmetic flags (`CF`, `ZF`, `SF`, and `OF`) using the destination width.

`nop` supports the zero-operand form plus selected MASM-compatible 16-bit and 32-bit encoding-operand forms:

```asm
nop
nop r16
nop r32
nop WORD PTR [reg32]
nop SWORD PTR [reg32]
nop DWORD PTR [reg32]
nop SDWORD PTR [reg32]
```

The accepted explicit-width memory-looking forms reuse the current memory-addressing grammar, so displacement and symbol-plus-register forms such as `nop DWORD PTR [eax + 4]`, `nop DWORD PTR [eax - 4]`, `nop DWORD PTR [array + esi]`, and `nop DWORD PTR array[esi]` are accepted when the corresponding address syntax is already supported. These NOP operands are encoding operands only. Register-form NOP operands are not read or written, and memory-looking NOP operands do not evaluate an effective address at runtime, do not read or write memory, do not perform planned memory validation, and do not emit uninitialized-read, declared-object, section-capacity, section-image, `.CONST`, or `.code` memory diagnostics. They advance execution without changing registers, modeled flags, flag-validity metadata, memory, Program Console output, or memory-change rows, and each accepted `nop` counts as one executed instruction. Unsupported forms such as `nop al`, `nop 1`, `nop [eax]`, byte/signed-byte PTR operands such as `nop BYTE PTR [eax]` and `nop SBYTE PTR [eax]`, `nop QWORD PTR [eax]`, and `nop SQWORD PTR [eax]` remain rejected. Accepted `nop` forms do not imply real x86 opcode-byte emission.

`adc` and `sbb` support register and memory destinations with compatible register, immediate, or memory sources where existing width rules are unambiguous. They use the current carry flag as carry-in or borrow-in and update the tracked arithmetic flags (`CF`, `ZF`, `SF`, and `OF`).

`clc`, `stc`, and `cmc` take no operands and mutate only the tracked carry flag.

`test` supports register/register, register/immediate, register/memory, memory/register, and memory/immediate forms when the memory width is explicit or inferable. It computes a transient bitwise AND, updates `ZF` and `SF`, clears `CF` and `OF`, and does not store the result. MASM-compatible ambiguous memory/immediate forms such as `test [esi], 1` and `test [esi + 4], 1` are rejected with an `ambiguous-memory-width` diagnostic; use `BYTE PTR`, `WORD PTR`, or `DWORD PTR`.

`inc` and `dec` support register destinations and memory destinations whose width is known from `PTR`, signed `PTR` aliases, direct symbol metadata, or symbol-offset metadata. Register aliases mutate only their selected width. Memory destinations are read-modify-write operations through checked memory helpers. Both instructions update `ZF`, `SF`, and `OF` and preserve `CF`. Untyped register-indirect forms such as `inc [eax]` and `dec [eax]` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`and`, `or`, and `xor` support register destinations and memory destinations with compatible register, immediate, or memory sources where existing width rules are unambiguous. They store the logical result in the destination, update `ZF` and `SF`, and clear `CF` and `OF`. Memory operands use checked memory helpers. Memory-to-memory forms such as `and value, other` are rejected with `invalid-instruction-operands`; untyped memory/immediate forms such as `xor [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`not` supports register destinations and memory destinations whose width is known from `PTR`, signed `PTR` aliases, direct symbol metadata, or symbol-offset metadata. It stores the bitwise complement at the selected width and preserves `CF`, `ZF`, `SF`, and `OF` exactly. Memory operands use checked memory helpers. Untyped register-indirect forms such as `not [eax]` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`shl`, `sal`, `shr`, and `sar` support register destinations and memory destinations whose width is known from `PTR`, signed `PTR` aliases, direct symbol metadata, or symbol-offset metadata. The count operand must be an immediate byte count or `CL`; no other register count is accepted. Counts use `raw_count & 31`; effective count zero is a no-op, effective count one updates `CF`, `ZF`, `SF`, and `OF` according to the selected shift mnemonic, and larger effective counts use the deterministic undefined-flag policy. Default mode emits `undefined-shift-flag` warnings and continues for undefined modeled-flag cases; the warning names the modeled flags updated from the result and the undefined modeled flags preserved by simulator policy. Strict test/configuration mode reports `undefined-shift-flag` as a runtime error before mutation. Untyped register-indirect forms such as `shl [eax], 1`, `shr [eax], 1`, and `sar [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`rol` supports the same register and unambiguous memory destination/count forms. Counts use `raw_count & 31`; effective count zero is a full no-op, and nonzero counts rotate left by `effective_count % operand_width`. `CF` is set from bit 0 of the rotated result, `ZF` and `SF` are preserved, and `OF` is defined only for effective count 1. For non-one nonzero counts, `OF` is preserved deterministically and default execution emits `undefined-modeled-flag`; strict shift validation remains shift-only and does not convert `ROL` warnings into runtime errors. Untyped register-indirect forms such as `rol [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`ror` supports the same register and unambiguous memory destination/count forms. Counts use `raw_count & 31`; effective count zero is a full no-op, and nonzero counts rotate right by `effective_count % operand_width`. `CF` is set from the most significant bit of the rotated result, `ZF` and `SF` are preserved, and `OF` is defined only for effective count 1. For non-one nonzero counts, `OF` is preserved deterministically and default execution emits `undefined-modeled-flag`; strict shift validation remains shift-only and does not convert `ROR` warnings into runtime errors. Untyped register-indirect forms such as `ror [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`lea` supports a 32-bit register destination and currently supported effective-address source forms: `symbol`, `symbol[offset]`, `[symbol + constant]`, `[reg32]`, `[reg32 + constant]`, `[reg32 - constant]`, `symbol[reg32]`, and `[symbol + reg32]`. It computes the effective address modulo 2^32, writes that address to the destination register, preserves `CF`, `ZF`, `SF`, and `OF`, performs no memory read or write, and produces no memory-change rows. Because `lea` computes an address only, its source expression does not require the computed address to be mapped, aligned, readable, writable, or inside a declared object, and it does not emit invalid-address, unaligned-access, object-bounds, or uninitialized-read diagnostics for that source expression. It rejects non-32-bit destinations, memory destinations, immediate/register/`OFFSET` sources, numeric-only address expressions, `PTR` source forms, and malformed address expressions with stable diagnostics. Scaled-index forms such as `[eax * 4]` and `[base + index * 4]` remain unsupported and are not implemented by `lea`.

`mul` supports one register or unambiguous memory source at 8-bit, 16-bit, or 32-bit width. `mul r/m8` multiplies `AL` by the source and stores the 16-bit product in `AX`; `mul r/m16` multiplies `AX` by the source and stores the 32-bit product in `DX:AX`; `mul r/m32` multiplies `EAX` by the source and stores the 64-bit product in `EDX:EAX`. `CF` and `OF` are set when the upper half of the product is nonzero and cleared when it is zero. `ZF` and `SF` are preserved deterministically. Memory sources may use direct typed symbols, symbol offsets, explicit unsigned `PTR` forms, or signed `PTR` aliases for executable 8-bit, 16-bit, and 32-bit widths. Untyped register-indirect forms such as `mul [eax]`, immediate sources such as `mul 5`, and multi-operand forms such as `mul eax, ebx` are rejected. Executable QWORD/SQWORD memory operations remain deferred.

`imul` supports the one-operand signed accumulator form with one register or unambiguous memory source at 8-bit, 16-bit, or 32-bit width. `imul r/m8` multiplies signed `AL` by the signed source and stores the 16-bit product in `AX`; `imul r/m16` multiplies signed `AX` by the signed source and stores the 32-bit product in `DX:AX`; `imul r/m32` multiplies signed `EAX` by the signed source and stores the 64-bit product in `EDX:EAX`. `imul` also supports explicit 16-bit and 32-bit destination forms: `imul reg16, reg16`, `imul reg32, reg32`, `imul reg16, mem16`, `imul reg32, mem32`, `imul reg16, reg16, imm`, `imul reg32, reg32, imm`, `imul reg16, mem16, imm`, and `imul reg32, mem32, imm`. Explicit-destination forms store only the low destination-width product in the destination register, preserve the source register or memory bytes, and set `CF`/`OF` when significant signed bits are truncated. `ZF` and `SF` are preserved deterministically. Memory sources use the same direct-symbol, symbol-offset, unsigned `PTR`, and signed `PTR` width rules as `mul`; memory reads go through checked VM helpers and do not create memory-change rows. Untyped register-indirect forms such as `imul [eax]`, immediate-only sources such as `imul 5`, two-operand register/immediate forms such as `imul eax, 5`, 8-bit explicit-destination forms such as `imul al, bl`, memory destinations, and register/register/register three-operand forms are rejected. Executable QWORD/SQWORD memory operations remain deferred.

`div` supports one register or unambiguous memory divisor at 8-bit, 16-bit, or 32-bit width. `div r/m8` divides unsigned `AX` by the source and stores the quotient in `AL` and remainder in `AH`; `div r/m16` divides unsigned `DX:AX` by the source and stores the quotient in `AX` and remainder in `DX`; `div r/m32` divides unsigned `EDX:EAX` by the source and stores the quotient in `EAX` and remainder in `EDX`. `CF`, `ZF`, `SF`, and `OF` are preserved deterministically. Memory divisors may use direct typed symbols, symbol offsets, explicit unsigned `PTR` forms, or signed `PTR` aliases for executable 8-bit, 16-bit, and 32-bit widths. Memory reads go through checked VM helpers and do not create memory-change rows. `div` reports `divide-by-zero` when the divisor is zero and `quotient-overflow` when the quotient does not fit the quotient register `AL`, `AX`, or `EAX`; both stop before updating quotient or remainder registers. Untyped register-indirect forms such as `div [eax]`, immediate sources such as `div 5`, and multi-operand forms such as `div eax, ebx` are rejected. Executable QWORD/SQWORD memory operations remain deferred.

`idiv` supports one register or unambiguous memory divisor at 8-bit, 16-bit, or 32-bit width. `idiv r/m8` divides signed `AX` by the signed source and stores the quotient in `AL` and remainder in `AH`; `idiv r/m16` divides signed `DX:AX` by the signed source and stores the quotient in `AX` and remainder in `DX`; `idiv r/m32` divides signed `EDX:EAX` by the signed source and stores the quotient in `EAX` and remainder in `EDX`. Quotients truncate toward zero and remainders keep the sign of the dividend. `CF`, `ZF`, `SF`, and `OF` are preserved deterministically, and existing flag-validity metadata is preserved. Memory divisors may use direct typed symbols, symbol offsets, explicit unsigned `PTR` forms, or signed `PTR` aliases for executable 8-bit, 16-bit, and 32-bit widths. Memory reads go through checked VM helpers, participate in planned-read validation, and do not create memory-change rows. `idiv` reports `divide-by-zero` when the divisor is zero and `quotient-overflow` when the signed quotient does not fit the quotient register `AL`, `AX`, or `EAX`; both stop before updating quotient or remainder registers. Untyped register-indirect forms such as `idiv [eax]`, immediate sources such as `idiv 5`, and multi-operand forms such as `idiv eax, ebx` are rejected. Executable QWORD/SQWORD memory operations remain deferred.

`exit` is accepted only as a zero-operand virtual Irvine32 terminator after `INCLUDE Irvine32.inc`. It terminates execution successfully and skips following instructions without changing registers, flags, memory, or Program Console output. It is not `call ExitProcess` and does not model Windows API behavior.

`call ProcedureName` supports direct near calls to user procedure entries declared with `ProcedureName PROC`. The target is resolved under the active user-symbol `CASEMAP` policy at the call site; default `CASEMAP:ALL` folds user procedure names, while `OPTION CASEMAP:NONE` requires exact-case user-symbol spelling. On success, the VM writes the pseudo-EIP return token for the executable lowered instruction immediately after the `CALL` to `ESP - 4`, updates `ESP`, preserves modeled flags and flag-validity metadata, and transfers execution to the target procedure entry. The internal stack write uses the central checked-memory diagnostic path. If the write fails, execution stops before committing the call transfer and before mutating `ESP`, instruction pointer, modeled flags, flag-validity metadata, Program Console output, terminal status, or memory-change rows.

The successful CALL return-token stack write is an implicit simulator-owned stack write. It is checked and tracked internally by the VM, but the current public source-run output contract does not expose that implicit write as a user-visible `memoryChanges` row. A later milestone may add public stack-write visualization only if that milestone updates the supported-syntax documentation, source-run JSON tests, rendered display tests, and compatibility notes together.

The executable successor after `CALL` means the next lowered VM instruction that is actually executable in the selected procedure's execution path. It does not mean the next source line, source byte offset, ordinary label, data declaration, `ENDP`, first instruction in another procedure, source boundary, or synthetic boundary marker.

A `CALL` whose successor is not an executable lowered VM instruction has no ordinary executable return target before the owning return-boundary phase defines one. The implementation must not invent a synthetic terminal pseudo-EIP, `ENDP` return target, source-boundary token, root-return sentinel, native-address-like value, or instruction-after-the-procedure placeholder for that edge.

`ret`/`RET` with no operands is implemented as a plain near return. In a called helper procedure, it reads the DWORD return token at `[ESP]` through the central checked-memory path, validates that the token maps to a loaded executable pseudo-EIP, increments `ESP` by 4, and transfers to the target only after the read and token validation both succeed. If `[ESP]` is unreadable, the existing checked-memory diagnostic is emitted and no token validation is attempted. If the read succeeds but the token is not a valid executable pseudo-EIP, execution stops with `invalid-return-address` before changing `ESP` or transferring control. In the selected entry procedure, default `rootRetMode = "masm32-compatible"` lets a no-operand root `RET` with no helper return pending terminate successfully without reading `[ESP]`, validating a pseudo-EIP token, changing `ESP`, or adding public `memoryChanges`. Optional `rootRetMode = "strict-call-frame"` instead rejects that selected-entry root `RET` with `root-ret-disallowed-by-mode` before any stack read, stack mutation, pseudo-EIP validation, public `memoryChanges` row, or successful terminal status. A called non-entry procedure that reaches its `ENDP` boundary without `RET` stops with `non-root-procedure-fell-through`. Source-level stack-frame support, argument handling, calling-convention behavior, and Irvine32 routine dispatch remain future-owned behavior.

Register, memory, indirect, immediate, `OFFSET`, ordinary-label, data-symbol, equate, unknown, malformed, external/API, and Irvine32 routine CALL targets are rejected or classified with structured diagnostics rather than dispatched. External/API calls and WinAPI behavior are non-goals, not deferred direct-CALL target forms.

## Recognized unsupported features

The parser should report `unsupported-feature` for these recognizable textbook constructs until their milestones are implemented:

- `TEXTEQU`
- `STRUCT`
- `UNION`
- `RECORD`
- `INVOKE`
- `PROTO`
- `LOCAL`
- `.IF`
- `.WHILE`
- `.REPEAT`
- `.BREAK`
- `.CONTINUE`
- `MACRO` / `ENDM`
- `INCLUDELIB`
- `EXTERN`
- `EXTERNDEF`
- `EXTRN`
- `PUBLIC`
- `COMM`
- `ASSUME`
- `.STARTUP`
- `.EXIT`
- `.DOSSEG`
- `.FARDATA` / `.FARDATA?`
- `ALIGN`
- `EVEN`
- `LABEL`
- `ORG`
- `COMMENT`
- `ECHO`
- `.LIST`, `.NOLIST`, `.CREF`, `.NOCREF`, `.TFCOND`


## Diagnostic recovery behavior

Phase 17 and later behavior can report multiple safely recoverable `unsupported-feature` diagnostics in one parse. The parser skips known unsupported line-level constructs, block-like constructs, and unsupported sections only far enough to resynchronize; programs with any diagnostics are not executed.

Recovered line-level constructs include `INVOKE`, `PROTO`, `LOCAL`, `TEXTEQU`, `INCLUDELIB`, `EXTERN`, `EXTERNDEF`, `EXTRN`, `PUBLIC`, `COMM`, `ASSUME`, `ALIGN`, `EVEN`, `LABEL`, `ORG`, `COMMENT`, and `ECHO`.

Recovered block-like constructs include `STRUCT` / `ENDS`, `UNION` / `ENDS`, `MACRO` / `ENDM`, `.IF` / `.ENDIF`, `.WHILE` / `.ENDW`, and `.REPEAT` / `.UNTIL` or `.UNTILCXZ`.

Current syntax details are maintained in the sections above. Historical promotion details for older milestones belong in [`MILESTONE_HISTORY.md`](MILESTONE_HISTORY.md), not in this syntax reference section.


## Backlog notes

Additional data types tracked for later compatibility work:

- `REAL4`
- `REAL8`
- `REAL10`
- `TBYTE`
- `FWORD`

Expression parser expansion tracked for later compatibility work:

- Full MASM expression compatibility beyond the implemented Phase 29 constant-expression operators.
- Runtime logical and relational expressions.
- Binary and octal literals.
- `.RADIX`.
- `SHORT`, `THIS`, and segment-related expression operators.

## Still deferred

Deferred simulator features include `loop`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, branch-distance/type overrides, source-level `push`, source-level `pop`, remaining simulator-owned CALL/RET behavior explicitly assigned to later phases, `ret imm16`, stack frames, Irvine32 routines beyond the virtual `exit` terminator, debugger stepping, breakpoints, scaled-index addressing, carry rotates, selected macro-compatibility conveniences explicitly assigned to later accepted milestones, and fuller MASM expression compatibility beyond the currently implemented subset.

Currently rejected forms remain rejected unless a later accepted phase explicitly changes them. Rejection today is not, by itself, a roadmap promise.

For CALL specifically, simulator-owned rejected target forms include register, memory, indirect, immediate, `OFFSET`, ordinary-label, data-symbol, equate, unknown, malformed, and recognized-but-not-yet-executable Irvine32 routine targets. A later accepted phase may change one of those simulator-owned forms only if it defines the syntax, runtime behavior, diagnostics, rendered messages, and tests.

External/API CALL targets are not ordinary deferred CALL forms. They are non-goals. Non-goals remain outside the simulator unless the canonical full specification and implementation guide are deliberately revised. Non-goals include external/API calls, Windows API execution or modeling, PE loading, object-file linking, import-library behavior, host filesystem include loading, native x86 execution, full x86 emulation, Windows process behavior, DLL loading, handles, and kernel behavior. Do not treat these non-goals as ordinary future/deferred simulator features.

Equality conditional jumps `je`, `jz`, `jne`, and `jnz`; signed relational conditional jumps `jl`, `jnge`, `jle`, `jng`, `jg`, `jnle`, `jge`, and `jnl`; and unsigned relational conditional jumps `ja`, `jnbe`, `jae`, `jnb`, `jb`, `jnae`, `jbe`, and `jna` are already implemented for direct executable code-label and procedure-entry targets. Direct user-procedure `CALL` is implemented for user `PROC` entry targets and writes pseudo-EIP return tokens to the simulated stack. Plain near `RET` is implemented for checked pseudo-EIP return-token reads from the simulated stack. `ESP` startup initialization from the active stack region is implemented, and `ESP` remains source-writable through supported explicit register instructions. That does not make source-level stack instructions, RET imm16, stack frames, arguments, or Irvine32 routine calls executable. Displayed `EIP` is already derived pseudo-code-address control-state display and is not a source-writable operand or user-definable symbol.
