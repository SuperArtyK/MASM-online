# Supported MASM32 Educational Simulator Syntax

Current milestone:

- Phase 82 - INVOKE Zero-Argument User Procedure Calls

This document describes the currently accepted MASM32 Educational Mode syntax, rejected forms, diagnostics, and future/deferred syntax. Phase 82 implements executable zero-argument `INVOKE Helper` / `invoke Helper` for same-file user procedures that require zero arguments. Phase 81 limited parser-owned `PROTO` metadata for same-file educational procedure prototypes, Phase 80 source-level LOCAL operand resolution/addressing, Phase 79 automatic LOCAL frames, Phase 78A limited `OPTION NOKEYWORD` forms, Phase 78 LOCAL parser metadata, Phase 77 direct-CALL `PROC USES` runtime save/restore, Phase 76 `PROC USES` parsing metadata, and Phase 75 targeted `PROC` diagnostics remain implemented.

Current direct control-transfer support includes direct `jmp label`, equality conditional jumps, signed relational conditional jumps, unsigned relational conditional jumps, direct near user-procedure `call ProcedureName`, plain near `ret`/`RET` with no operands, and near `ret imm16`/`RET imm16`.

### Current INVOKE and ADDR status

In the current repository state, source-level executable `INVOKE` is accepted only in the Phase 82 zero-argument same-file user-procedure form: `INVOKE Helper` or `invoke Helper`, where `Helper` resolves to a user `PROC` that requires zero arguments under current metadata. Accepted zero-argument `INVOKE` lowers to the same checked runtime behavior as direct user-procedure `call Helper`.

All source-level `INVOKE` forms with arguments remain unsupported. Source-level executable `ADDR` remains unsupported. `PROTO` metadata remains parser-owned metadata and does not make arguments, runtime parameters, external calls, WinAPI calls, Irvine32 routine calls, C runtime calls, MASM32 runtime calls, import libraries, linking, or calling conventions executable. Later roadmap examples involving `ADDR` or INVOKE arguments are not current accepted syntax. A later ADDR-preparation phase may prepare helper-level `ADDR symbol` records for future INVOKE arguments while still refusing source-level `INVOKE Helper, ADDR symbol`. A still later INVOKE-argument phase may accept a limited DWORD argument subset only after the owning guide phase, tests, output contracts, and this supported-syntax file are updated.

Current accepted `PROC USES` metadata syntax:

```asm
MyProc PROC USES eax
MyProc PROC USES eax ebx ecx
MyProc PROC USES EAX EBX ESI EDI
```

Accepted `USES` register names are `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, and `EDI`, case-insensitively. The parser stores canonical register identities in declared order. Direct `CALL` entry into a `PROC USES` procedure saves listed registers in declared order on the checked simulated stack and restores them in reverse order on `RET`. Selected-entry startup, direct branch transfer, and ordinary fallthrough into a `USES` procedure remain unsupported and report `unsupported-proc-uses-runtime` so the simulator does not silently ignore USES metadata without a CALL-created preservation frame.

Current accepted `LOCAL` declaration metadata syntax:

```asm
LOCAL temp:DWORD
LOCAL ch:BYTE
LOCAL signedVal:SDWORD
LOCAL buf[16]:BYTE
LOCAL a:DWORD, b:DWORD
```

Accepted local types are `BYTE`, `SBYTE`, `WORD`, `SWORD`, `DWORD`, and `SDWORD`, case-insensitively. A valid `LOCAL` declaration is accepted only inside a `PROC` body and only before executable instructions in that procedure. Array counts must be positive numeric literal counts or already-defined numeric equates that resolve to positive 32-bit constant values without registers or runtime operands. The parser stores declaration order, element count, element size, alignment, source location, negative `EBP`-relative metadata offsets, and the procedure total local-frame size rounded to 4 bytes. Phase 79 allocates runtime stack storage for accepted LOCAL metadata on selected-entry and direct-CALL procedure paths, initializes visible LOCAL bytes to deterministic zero, and releases those automatic frames on supported procedure termination paths. Phase 80 resolves supported source-level local names as active-frame memory operands for scalar locals and byte-offset local array elements, including forms such as `mov temp, eax`, `mov eax, temp`, `mov BYTE PTR buf[0], 'A'`, `mov al, BYTE PTR buf[1]`, `lea eax, temp`, and `lea eax, buf`. The canonical Phase 78 example `LOCAL ch:BYTE` is accepted even though `CH` is otherwise a recognized register alias; other register names and register aliases remain reserved and are rejected as local symbol names.

Targeted `LOCAL` diagnostics include `local-outside-procedure`, `local-after-instruction`, `unsupported-local-type`, `invalid-local-declaration`, `duplicate-local-symbol`, and `invalid-local-count`. Unsupported types such as `QWORD`, `SQWORD`, `REAL4`, and user-defined structure types are rejected with `unsupported-local-type`; malformed declarations, initializers, invalid array counts, duplicate locals, procedure-name collisions, and same-procedure label collisions receive targeted parser/source-run diagnostics.

Current accepted `PROTO` metadata syntax:

```asm
MyProc PROTO
MyProc PROTO arg1:DWORD
MyProc PROTO arg1:DWORD, p:SDWORD
```

Phase 81 stores parser-owned prototype metadata separately from data symbols, labels, procedures, equates, and LOCAL declarations. Accepted prototypes preserve prototype name spelling, parameter names, parameter type identities, declaration order, parameter count, and source locations/spans for prototype and parameter tokens. Accepted `PROTO` declarations emit no executable IR, do not allocate stack frames, do not make `ADDR` executable, and do not model calling conventions, external linkage, imports, WinAPI behavior, Irvine32 routine dispatch, C runtime calls, MASM32 runtime calls, or runtime parameters. Phase 82 may use compatible zero-argument `PROTO` metadata only to permit zero-argument same-file user-procedure `INVOKE`; it does not implement PROTO parameter runtime access or argument lowering. The first PROTO implementation accepts only named `DWORD` and `SDWORD` parameters.

A zero-argument `PROTO` may link to a same-name bare `PROC`. A parameterized `PROTO`, such as `MyProc PROTO arg1:DWORD`, is not currently compatible with a same-name bare `PROC`, because accepted PROC parameter metadata and runtime parameter access do not exist yet. That mismatch reports `proto-proc-mismatch`.

Pointer forms such as `p:PTR BYTE`, unnamed parameters such as `:DWORD`, `VARARG`, language/distance metadata, structure types, floating-point types, 64-bit parameter types, and external/API prototypes remain rejected or non-goal behavior. `ExitProcess PROTO :DWORD` and similar external/API prototype declarations are rejected with `unsupported-external-proto` rather than treated as import metadata.

Targeted `PROTO` diagnostics include `invalid-proto-declaration`, `unsupported-proto-type`, `unsupported-external-proto`, `duplicate-proto`, and `proto-proc-mismatch`.

Implemented procedure and termination behavior for the active runtime/source-run MASM behavior phase:

- `END entryName` selects the source-run entry procedure.
- Execution starts at the selected entry procedure's first lowered executable slot. If the selected entry procedure is empty, execution starts at the source-order position immediately after that procedure range.
- `PROC`, `ENDP`, and `END` are source/module structure, not executable VM instructions, hidden stops, hidden returns, or implicit program terminators.
- Bare `name PROC` declarations record procedure metadata and remain accepted. `name PROC USES reglist` declarations are accepted when `reglist` is a whitespace-separated list using only `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, and `EDI`; the declared order is preserved as metadata. Unsupported non-USES attributes or parameters after `PROC` are rejected with targeted parser/source-run diagnostics rather than generic line-ending diagnostics.
- Direct near `CALL` to accepted user procedures is implemented for the supported direct form. Direct `call ProcedureName` is executable only when `ProcedureName` resolves to a user `PROC` entry under the active user-symbol `CASEMAP` policy. A successful direct user-procedure `CALL` writes a pseudo-EIP return token to `ESP - 4`, updates `ESP`, and transfers to the target procedure entry through the checked VM control-flow path. When the target procedure has `PROC USES` metadata, the CALL path also saves the listed registers on the checked simulated stack before entry; insufficient stack capacity reports `stack-overflow` before partial CALL/USES mutation. The current public source-run output contract does not expose implicit CALL return-token writes or automatic USES save/restore stack writes as user-visible `memoryChanges` rows.
- Plain near `RET` is implemented for helper returns through simulator pseudo-EIP return tokens when a helper call return is pending. When returning from a CALL-created `PROC USES` frame, the runtime validates and restores saved registers before popping and validating the return token; missing saved slots report `stack-underflow` before partial restore/return mutation.
- Near `RET imm16` is implemented for helper returns as explicit unsigned 16-bit caller cleanup applied only after the return token is validated.
- `LEAVE` is implemented as validation-first stack-frame teardown shorthand: it reads saved `EBP` from DWORD `[EBP]`, then commits `ESP = old EBP + 4` and `EBP = saved EBP` only after the read succeeds.
- Root-code-stream `RET` succeeds by default in MASM32-compatible root RET mode when no helper return is pending, including after ordinary fallthrough from the selected entry into later procedure text.
- Optional strict root RET mode rejects root-code-stream `RET` with `root-ret-disallowed-by-mode`.
- Virtual Irvine32 `exit` is an explicit successful terminator where recognized.
- Ordinary VM sequential execution follows the lowered executable code stream and may fall through across procedure boundaries when no explicit supported transfer or terminator changes control flow.
- Ordinary procedure-boundary fallthrough is governed by `procedureFallthroughPolicy`. The default `warn` mode emits non-fatal `procedure-fell-through` and continues, `off` suppresses only that diagnostic and continues, and `error` emits runtime-error `procedure-fell-through` and stops before executing the first destination-procedure instruction.
- `entryProcedureEndMode` controls selected-entry `ENDP` boundary compatibility. Default `code-stream` preserves realistic VM control flow across selected-entry `ENDP`; opt-in `stop-at-entry-end` terminates successfully only when ordinary sequential execution reaches the selected entry procedure boundary, including empty selected-entry procedures.
- Reaching the end of the lowered executable code stream without explicit `RET`, virtual Irvine32 `exit`, or another supported terminator reports runtime error `code-fell-off-end`.
- When the responsible procedure for `code-fell-off-end` is named exactly `front` under ASCII case-insensitive comparison, the simulator appends the notice `the-front-fell-off` after the runtime error.
- Called helper procedure fallthrough while a helper return token is pending is mapped to `procedure-fell-through`; active public output does not expose the older `non-root-procedure-fell-through` code for this procedure-boundary code smell.

The selected-entry `ENDP` success rule from pre-71C accepted behavior is no longer the default; selected-entry `ENDP` is not an implicit successful terminator. The default behavior is baseline VM code-stream fallthrough plus `procedureFallthroughPolicy` for procedure-boundary crossings and `code-fell-off-end` when executable code ends without an explicit terminator. Phase 71E provides opt-in `entryProcedureEndMode = "stop-at-entry-end"` for beginner-friendly selected-entry boundary completion without changing parser, semantic, root `RET`, or helper `CALL`/`RET` behavior. This behavior is simulator-owned; it must not be described as a native MASM diagnostic, a native x86 CPU trap, a PE loader behavior, a C runtime behavior, or Windows process termination.

Phase 72 adds `callDepthLimit` as a source-run/protocol setting for direct user-procedure `CALL` chains. The setting defaults to `64`, accepts integer values from `1` through `4096`, and rejects invalid values with `invalid-call-depth-limit` before execution. The selected entry procedure itself is not counted as a call frame; committed direct helper calls increment the current call depth until a successfully validated ordinary helper `RET` returns. An over-limit direct `CALL` reports `call-depth-exceeded` before return-token stack writes, `ESP` mutation, IP transfer, flag changes, memory-change rows, Program Console output, or successful terminal status. Phase 72 emits no call trace metadata and no `call-trace-truncated` message.

Source-level 32-bit `push`, source-level 32-bit `pop`, `LEAVE`, near `RET imm16` caller-cleanup returns, `PROC USES` parsing metadata, direct-CALL `PROC USES` runtime save/restore, parser `LOCAL` declaration metadata, automatic runtime LOCAL stack allocation/lifetime, Phase 80 supported source-level LOCAL operands, Phase 81 limited `PROTO` metadata, and Phase 82 zero-argument same-file user-procedure `INVOKE` are implemented. Procedures with parsed `LOCAL` declarations receive checked automatic frames on selected-entry startup, direct user-procedure `CALL`, and accepted zero-argument user-procedure `INVOKE`; those frames are released on root `RET`, helper `RET`, helper `RET imm16`, source-level `LEAVE` followed by `RET`, and virtual Irvine32 `exit`. `INVOKE` arguments, `OFFSET local`, `ADDR`, executable `PROTO` behavior, pointer or unnamed prototype parameters, `VARARG`, scaled-index LOCAL addressing, parameters, calling-convention behavior, QWORD/SQWORD executable LOCAL memory operands, and selected Irvine32 routine dispatch remain deferred unless a later accepted phase explicitly implements them. Simulator-owned rejected CALL target forms remain rejected unless a later accepted phase explicitly changes the specific simulator-owned form; they are not future work merely because they are currently rejected. External/API calls, WinAPI execution, PE loading, object-file linking, import-library behavior, host filesystem access, native x86 execution, and full x86 emulation remain non-goals rather than deferred simulator features. Detailed accepted and rejected forms are listed in the sections below.

Implemented and planned fallthrough diagnostics and settings:

| Item | Phase/status | Default/status | Meaning |
|---|---:|---|---|
| `code-fell-off-end` | 71D implemented | runtime error | Execution reached the end of the executable code stream without an explicit program terminator. |
| `the-front-fell-off` | 71D implemented | notice, required easter egg | Harmless notice emitted only after `code-fell-off-end` when the responsible procedure name is exactly `front` under ASCII case-insensitive comparison. |
| `procedure-fell-through` | 71D implemented | warning by default; configurable `off`/`warn`/`error` | Ordinary sequential execution crossed from one procedure range into another without explicit supported control transfer or termination. |
| `entryProcedureEndMode` | 71E implemented | default `code-stream`; opt-in `stop-at-entry-end` | Compatibility setting for selected-entry `ENDP` auto-stop behavior. |
| `callDepthLimit` | 72 implemented | default `64`; accepted `1..4096` | Resource limit for committed direct user-procedure `CALL` frames that have not yet returned through a successfully validated ordinary helper `RET`. |
| `invalid-call-depth-limit` | 72 implemented | settings error | Invalid `callDepthLimit` value rejected before source execution. |
| `call-depth-exceeded` | 72 implemented | resource-limit-error | Direct user-procedure `CALL` rejected before mutation because the attempted call depth would exceed `callDepthLimit`. |

The implemented `code-fell-off-end` text is:

```text
Execution reached the end of the executable code stream without an explicit program terminator. Did you forget to add RET or Irvine32 exit?
```

The implemented `code-fell-off-end` diagnostic is simulator-owned. It must not be described as a native MASM, x86 CPU, PE loader, Windows process, C runtime, or Irvine32 library diagnostic.

### Future stack/procedure syntax not implemented yet

Consult the current instruction-support table in this document for implemented branch, control-flow, stack-transfer, LOCAL operand, limited `PROTO` metadata, and zero-argument `INVOKE` forms. Direct near `call ProcedureName`, zero-argument `INVOKE ProcedureName` to same-file user procedures, plain near `ret`/`RET` with no operands, near `ret imm16`/`RET imm16`, the documented 32-bit source-level `push`/`pop` subset, `leave`/`LEAVE`, automatic runtime LOCAL frames for procedures with parsed `LOCAL` declarations, supported Phase 80 LOCAL operands, and Phase 81 limited same-file `PROTO` metadata are implemented. Do not infer unsupported CALL forms, `INVOKE` arguments, `OFFSET local`, executable `ADDR`, pointer or unnamed prototype parameters, scaled-index LOCAL addressing, far returns, Irvine32 routine dispatch through INVOKE, argument metadata, external calls, import/linker behavior, or calling-convention behavior from future roadmap examples.

The corrected future sequence after Phase 82 is: limited helper-level `ADDR` argument preparation, then limited DWORD `INVOKE` argument lowering and cleanup. Until those phases are implemented and this file is updated, all source-level `INVOKE` forms with arguments and all source-level `ADDR` operands remain unsupported.

The following stack/procedure features remain future work only if a later accepted milestone explicitly implements them:

- `INVOKE` arguments beyond the accepted zero-argument same-file user-procedure form;
- `ADDR` as a future INVOKE-argument helper/operator;
- source-level `INVOKE` with arguments;
- `OFFSET local` and `ADDR local` as executable source-level argument behavior;
- scaled-index LOCAL addressing and QWORD/SQWORD executable LOCAL memory access;
- automatic procedure-frame features beyond Phase 80 LOCAL operand access and already implemented `PROC USES` preservation frames;
- pointer or unnamed `PROTO` parameter metadata, `VARARG`, language/distance prototype metadata, and executable prototype/calling-convention behavior;
- selected Irvine32 routine calls beyond already implemented virtual terminator behavior;
- external calls, WinAPI calls, C runtime calls, MASM32 runtime calls, import libraries, object linking, PE loading, host callbacks, and native x86 execution.

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
- Implemented `CALL` internal return-token writes are not the same public feature as source-level `push`.
- Implemented `RET` internal return-token reads are not the same public feature as source-level `pop`.
- Source-level 32-bit `push` and `pop` are implemented by Phase 72A, and source-level `leave`/`LEAVE` is implemented by Phase 73; wider or narrower stack transfers remain rejected.
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

Rejected declaration categories include data symbols, numeric equates, code labels, procedure names, and procedure-local symbols, except for the narrow Phase 78 `LOCAL ch:BYTE` compatibility example documented above. The diagnostic code is `reserved-word-symbol`, and it points at the declaration name when source location is available. A rejected reserved-word declaration is not inserted into the user-symbol tables.

`OPTION CASEMAP` controls lookup for accepted user-defined symbols only. `OPTION CASEMAP:NONE` does not make reserved words available as user-defined symbols, and reserved-word matching remains case-insensitive.

Phase 78A implements a limited educational `OPTION NOKEYWORD:<...>` subset. Only the exact words `LOOP` and `OFFSET` are disable-eligible. Accepted forms are `OPTION NOKEYWORD:<LOOP>`, `OPTION NOKEYWORD:<OFFSET>`, `OPTION NOKEYWORD:<LOOP OFFSET>`, and `OPTION NOKEYWORD: <LOOP OFFSET>`; directive names and listed words are matched case-insensitively, and list entries must be whitespace-separated inside angle brackets. Valid directives affect only later source lines, multiple valid directives accumulate, and duplicate listed words are idempotent.

After a valid directive, disabled `LOOP` and `OFFSET` spellings and ASCII case variants may be used as ordinary user-defined symbols where that symbol kind is otherwise valid. Disabled `LOOP` is no longer available as its old instruction-family keyword on later source lines, and disabled `OFFSET` is no longer available as its old address-operator keyword on later source lines. Malformed lists use `invalid-nokeyword-syntax` or `invalid-nokeyword-list`; unknown listed words use `nokeyword-unknown-keyword`; recognized but protected words use `nokeyword-protected-keyword`; old keyword-role use after disablement uses `disabled-keyword-used-as-keyword` or, where the parser cannot choose a safe symbol interpretation, `disabled-keyword-ambiguous`. A failed directive is atomic: no word in that directive is newly disabled, while disabled keyword state created by earlier valid directives remains active.

Protected words such as all registers, register aliases, `EIP`, implemented instructions, branch aliases other than `LOOP`, data type names, `PTR` width names, structural directives, virtual include names, Irvine32 registry names, external/API names, unknown words, and parser-critical option keywords remain rejected. Broader MASM keyword compatibility, keyword re-enable forms, macro-time keyword behavior, `PUSHCONTEXT`/`POPCONTEXT`, public/external linkage behavior, object-file/linker behavior, WinAPI behavior, and host include behavior remain outside the current simulator boundary unless a later accepted phase deliberately expands them.

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

Phase 61C - Branch Debugger Dependency Cleanup documents that direct-branch execution and debugger/editor behavior are separate systems; it does not implement debugger behavior. Executable direct branches and direct user-procedure `CALL` do not enable debugger stepping, breakpoint binding, current-instruction highlighting, editor source navigation, CodeMirror gutter behavior, or branch-target editor highlighting or CALL-target editor highlighting; those systems remain future debugger/editor work. `loop`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, branch-distance/type overrides such as `SHORT`/`NEAR PTR`/`FAR PTR`, unsupported stack-transfer widths, and full procedure/calling-convention execution remain future work. Rejected CALL target forms remain rejected unless a later accepted phase explicitly changes them.

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
- Procedure markers using bare `PROC` and `ENDP` as structural markers.
- `END name` entry-point validation.
- Labels are accepted as parser/source metadata. Implemented direct branch and conditional branch families may target executable code labels or procedure-entry labels according to their instruction-specific rules. Direct user-procedure `CALL` targets user `PROC` entries only.
- `PROC USES` tails are accepted only as whitespace-separated metadata using `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, and `EDI`; duplicate registers, `ESP`, `EBP`, 8-bit aliases, 16-bit aliases, unknown names, comma-separated lists, missing lists, and malformed lists are rejected with targeted `PROC USES` diagnostics. Other `PROC` tails such as parameters, language attributes, visibility/export attributes, `FRAME`, `NEAR`, and `FAR` remain rejected with `unsupported-proc-attribute`; malformed punctuation after `PROC` reports `invalid-proc-declaration`.

Procedure declarations create source ranges and procedure-entry symbols distinct from ordinary `label:` code labels. Duplicate procedure declarations report `duplicate-procedure`, and an `ENDP` name that does not match the currently open procedure reports `proc-end-mismatch`. `END entryName` selects the source-run entry procedure. Execution starts at the first executable instruction inside that selected procedure. If the selected entry procedure has no executable instruction, execution starts at the source-order position immediately after that selected procedure range. Helper procedures before the selected entry procedure do not run automatically merely because they appear earlier in source order. Helper procedures after the selected entry procedure may run by ordinary VM code-stream fallthrough when control reaches their lowered executable instructions, or by implemented explicit control transfer such as direct `jmp` or direct user-procedure `CALL`. Falling off the end of the lowered executable stream without explicit `RET`, virtual Irvine32 `exit`, or another supported terminator reports `code-fell-off-end`; `ENDP` itself is not a successful terminator.

### MASM32 header compatibility directives

Accepted before `.data` or `.code` as compatibility no-ops, metadata-only directives, virtual includes, or semantic directives:

- `.386`, `.486`, `.586`, `.686` processor compatibility declarations.
- `.model flat, stdcall`. Other `.model` forms report `unsupported-model`.
- `.stack` and `.stack size`, where the optional size is stored as parser metadata. In automatic layout selected by tests/configuration, the parsed size controls stack region capacity metadata. Runtime startup initializes `ESP` from the active stack region's exclusive high limit. Direct user-procedure `CALL` performs an internal checked 32-bit return-token write at `ESP - 4`. Helper plain near `RET` performs an internal checked 32-bit return-token read from `[ESP]`, validates the token, and increments `ESP` only after validation succeeds. Selected-entry root `RET` succeeds without reading `[ESP]` when no helper return is pending. Source-level 32-bit `push` and `pop` use the active checked stack region. `LEAVE` reads saved `EBP` from DWORD `[EBP]` through checked VM memory and then commits `ESP = old EBP + 4` and `EBP = saved EBP`. Near `RET imm16` reads and validates the helper return token exactly like plain helper `RET`, then applies the unsigned 16-bit immediate byte cleanup only after validation succeeds. Procedure-frame creation remains deferred. Accepted near return forms include `ret`, `RET`, `ret 0`, `ret 4`, `ret 8`, `ret 16`, and hexadecimal constants such as `ret 0010h`. The immediate must be an unsigned 16-bit constant expression in bytes; `ret -1`, `ret 10000h`, `ret eax`, `ret DWORD PTR [esp]`, `retf`, and `retf 4` remain rejected.
- `INCLUDE Irvine32.inc` and `INCLUDE Macros.inc` as virtual built-ins. The simulator does not load host files. `INCLUDE Irvine32.inc` registers known Irvine32 names as virtual metadata for classification and diagnostics. It also enables the zero-operand virtual `exit` terminator. It does not implement Irvine32 routine calls, additional RET forms beyond the separately documented plain near helper `RET` and selected-entry root `RET`, ExitProcess behavior, procedure frames, Program Console routines, other Irvine32 bodies, Windows API behavior, linking, or host include loading. `INCLUDE Macros.inc` remains a virtual no-op and does not populate the Irvine32 registry. `INCLUDELIB` directives are not virtual includes and are not accepted as no-ops; they report linker/library diagnostics such as `unsupported-includelib`, `unsupported-masm32-library`, or `unsupported-windows-api-library`. Basename-only unsupported include files report `unsupported-include`. Host/path-like include operands such as `include \masm32\include\masm32.inc`, `include C:\masm32\include\kernel32.inc`, `include ..\include\file.inc`, `include .\local.inc`, and `include /usr/local/include/file.inc` report host/path diagnostics such as `unsupported-host-include-path`, `unsupported-masm32-library-include`, or `unsupported-windows-api-include` instead of repeated lexer path-separator errors.

Phase 82 `INVOKE` diagnostics: accepted `INVOKE Helper` / `invoke Helper` to same-file zero-argument user procedures lowers to direct-CALL behavior. `INVOKE` with arguments reports `invoke-arguments-not-supported-yet` unless a more specific target non-goal applies. Wrong-kind, unknown, malformed, data-symbol, label, equate, register, memory, immediate, or other non-procedure targets report `invalid-invoke-target`. External/API targets such as `ExitProcess`, C runtime targets such as `crt_printf`, and MASM32 runtime targets such as `StdOut` report `unsupported-external-invoke`. Irvine32 routine targets such as `WriteString` report `unsupported-irvine-invoke`. `ADDR` operands remain unsupported and are not made executable by Phase 82. These diagnostics are emitted through Simulator Messages, refuse execution, and do not write to Program Console. A later ADDR-preparation phase may prepare helper-level address argument records but still must not execute INVOKE-with-arguments. A later INVOKE-argument phase must update this section before any source-level `INVOKE target, ADDR symbol` form is documented as accepted.
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
- The virtual Irvine32 registry recognizes known routine names for classification and diagnostics, including CALL-target classification. Only the zero-operand virtual `exit` terminator is executable as an Irvine32 behavior in the current subset; direct user-procedure `CALL` is executable only for user `PROC` entries. Calls to Irvine32 routines and procedure-frame features beyond Phase 80 LOCAL operand access remain deferred according to the future-feature sections in this document. Near `RET imm16` is implemented only as explicit helper return cleanup; it does not imply Irvine32 routine calls or procedure metadata. Windows/API execution remains outside the simulator boundary as a permanent non-goal unless the canonical specification and guide are deliberately revised.
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
- `push`
- `pop`
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

`push` accepts one 32-bit register source, one 32-bit immediate source, or one DWORD memory source. Accepted examples include `push eax`, `push esp`, `push 1234h`, `push -1`, `push DWORD PTR value`, and `push DWORD PTR [esp]`. The source value is resolved first, then `ESP - 4` is checked and written through the central VM memory helpers. Successful source-level `push` stack writes appear as public `memoryChanges` rows. `push esp` pushes the original `ESP` value that existed before the stack write commits. Byte, word, QWORD, SQWORD, ambiguous-width memory, malformed, and multi-operand `push` forms are rejected.

`pop` accepts one 32-bit register destination or one DWORD memory destination. Accepted examples include `pop eax`, `pop esp`, `pop DWORD PTR value`, and `pop DWORD PTR [esp]`. It reads DWORD `[original ESP]` through the checked memory path, validates the destination, and commits only after successful validation. Register-destination `pop` stack reads do not create public `memoryChanges` rows. Memory-destination `pop` writes create public `memoryChanges` rows for the destination only. `pop esp` leaves externally visible `ESP` equal to the popped DWORD value. `pop DWORD PTR [esp]` computes the destination using `original ESP + 4`. Immediate, byte, word, QWORD, SQWORD, ambiguous-width memory, malformed, and multi-operand `pop` forms are rejected. Declared-object bounds validation applies to real declared data objects; it does not synthesize stack objects or reject valid stack-region PUSH/POP accesses merely because the stack has no declared data-object metadata.

`leave`/`LEAVE` accepts no operands. It is implemented as validation-first shorthand for stack-frame teardown: the simulator reads DWORD `[EBP]` through the checked memory path, then commits `ESP = old EBP + 4` and `EBP = saved EBP` only after that read succeeds. It preserves all modeled flags and creates no public `memoryChanges` rows. `leave eax`, multi-operand `leave` forms, and `enter` remain rejected. If `[EBP]` cannot be read, the checked-memory runtime diagnostic is reported on the `leave` instruction before `ESP`, `EBP`, flags, memory, Program Console output, terminal status, or public `memoryChanges` mutate.

`exit` is accepted only as a zero-operand virtual Irvine32 terminator after `INCLUDE Irvine32.inc`. It terminates execution successfully and skips following instructions without changing registers, flags, memory, or Program Console output. It is not `call ExitProcess` and does not model Windows API behavior.

`call ProcedureName` supports direct near calls to user procedure entries declared with `ProcedureName PROC`. The target is resolved under the active user-symbol `CASEMAP` policy at the call site; default `CASEMAP:ALL` folds user procedure names, while `OPTION CASEMAP:NONE` requires exact-case user-symbol spelling. On success, the VM writes the pseudo-EIP return token for the executable lowered instruction immediately after the `CALL` to `ESP - 4`, updates `ESP`, preserves modeled flags and flag-validity metadata, and transfers execution to the target procedure entry. The internal stack write uses the central checked-memory diagnostic path. If the write fails, execution stops before committing the call transfer and before mutating `ESP`, instruction pointer, modeled flags, flag-validity metadata, Program Console output, terminal status, or memory-change rows.

The successful CALL return-token stack write is an implicit simulator-owned stack write. It is checked and tracked internally by the VM, but the current public source-run output contract does not expose that implicit write as a user-visible `memoryChanges` row. A later milestone may add public stack-write visualization only if that milestone updates the supported-syntax documentation, source-run JSON tests, rendered display tests, and compatibility notes together.

The executable successor after `CALL` means the next lowered VM instruction that is actually executable in the selected procedure's execution path. It does not mean the next source line, source byte offset, ordinary label, data declaration, `ENDP`, first instruction in another procedure, source boundary, or synthetic boundary marker.

A `CALL` whose successor is not an executable lowered VM instruction has no ordinary executable return target before the owning return-boundary phase defines one. The implementation must not invent a synthetic terminal pseudo-EIP, `ENDP` return target, source-boundary token, root-return sentinel, native-address-like value, or instruction-after-the-procedure placeholder for that edge.

`ret`/`RET` with no operands is implemented as a plain near return. In a called helper procedure, it reads the DWORD return token at `[ESP]` through the central checked-memory path, validates that the token maps to a loaded executable pseudo-EIP, increments `ESP` by 4, and transfers to the target only after the read and token validation both succeed. If `[ESP]` is unreadable, the existing checked-memory diagnostic is emitted and no token validation is attempted. If the read succeeds but the token is not a valid executable pseudo-EIP, execution stops with `invalid-return-address` before changing `ESP` or transferring control. In the selected entry procedure, default `rootRetMode = "masm32-compatible"` lets a no-operand root `RET` with no helper return pending terminate successfully without reading `[ESP]`, validating a pseudo-EIP token, changing `ESP`, or adding public `memoryChanges`. Optional `rootRetMode = "strict-call-frame"` instead rejects that selected-entry root `RET` with `root-ret-disallowed-by-mode` before any stack read, stack mutation, pseudo-EIP validation, public `memoryChanges` row, or successful terminal status. A called non-entry procedure that reaches its `ENDP` boundary without `RET` is governed by `procedureFallthroughPolicy`: default `warn` emits `procedure-fell-through` and continues, `off` suppresses only that diagnostic, and `error` stops with `procedure-fell-through`. Source-level stack-frame support, argument handling, calling-convention behavior, and Irvine32 routine dispatch remain future-owned behavior.

Register, memory, indirect, immediate, `OFFSET`, ordinary-label, data-symbol, equate, unknown, malformed, external/API, and Irvine32 routine CALL targets are rejected or classified with structured diagnostics rather than dispatched. External/API calls and WinAPI behavior are non-goals, not deferred direct-CALL target forms.

## Recognized unsupported features

The parser should report `unsupported-feature` for these recognizable textbook constructs until their milestones are implemented:

- `TEXTEQU`
- `STRUCT`
- `UNION`
- `RECORD`
- `INVOKE`
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

Recovered line-level constructs include `INVOKE`, unsupported or malformed `PROTO` forms, `TEXTEQU`, `INCLUDELIB`, `EXTERN`, `EXTERNDEF`, `EXTRN`, `PUBLIC`, `COMM`, `ASSUME`, `ALIGN`, `EVEN`, `LABEL`, `ORG`, `COMMENT`, and `ECHO`.

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

Deferred simulator features include `loop`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, branch-distance/type overrides, unsupported stack-transfer widths, remaining simulator-owned CALL/RET behavior explicitly assigned to later phases, stack frames, Irvine32 routines beyond the virtual `exit` terminator, debugger stepping, breakpoints, scaled-index addressing, carry rotates, selected macro-compatibility conveniences explicitly assigned to later accepted milestones, and fuller MASM expression compatibility beyond the currently implemented subset.

Currently rejected forms remain rejected unless a later accepted phase explicitly changes them. Rejection today is not, by itself, a roadmap promise.

For CALL specifically, simulator-owned rejected target forms include register, memory, indirect, immediate, `OFFSET`, ordinary-label, data-symbol, equate, unknown, malformed, and recognized-but-not-yet-executable Irvine32 routine targets. A later accepted phase may change one of those simulator-owned forms only if it defines the syntax, runtime behavior, diagnostics, rendered messages, and tests.

External/API CALL targets are not ordinary deferred CALL forms. They are non-goals. Non-goals remain outside the simulator unless the canonical full specification and implementation guide are deliberately revised. Non-goals include external/API calls, Windows API execution or modeling, PE loading, object-file linking, import-library behavior, host filesystem include loading, native x86 execution, full x86 emulation, Windows process behavior, DLL loading, handles, and kernel behavior. Do not treat these non-goals as ordinary future/deferred simulator features.

Equality conditional jumps `je`, `jz`, `jne`, and `jnz`; signed relational conditional jumps `jl`, `jnge`, `jle`, `jng`, `jg`, `jnle`, `jge`, and `jnl`; and unsigned relational conditional jumps `ja`, `jnbe`, `jae`, `jnb`, `jb`, `jnae`, `jbe`, and `jna` are already implemented for direct executable code-label and procedure-entry targets. Direct user-procedure `CALL` is implemented for user `PROC` entry targets and writes pseudo-EIP return tokens to the simulated stack. Direct `CALL` entry into a procedure with `USES` metadata also performs automatic checked save/restore for listed registers. Plain near `RET` is implemented for checked pseudo-EIP return-token reads from the simulated stack, and near `RET imm16` is implemented for explicit unsigned 16-bit caller-cleanup returns after the return token is validated. `ESP` startup initialization from the active stack region is implemented, and `ESP` remains source-writable through supported explicit register instructions. Source-level 32-bit `push` and `pop` are implemented separately from CALL/RET internals, and `LEAVE` is implemented as validation-first frame teardown shorthand; this does not make automatic frame creation, argument metadata, calling-convention inference, or Irvine32 routine calls executable. Displayed `EIP` is already derived pseudo-code-address control-state display and is not a source-writable operand or user-definable symbol.
