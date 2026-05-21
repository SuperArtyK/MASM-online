# Supported MASM32 Educational Simulator Syntax

This reference describes the implemented source subset through Milestone 50. Milestone 31 adds a native/Node diagnostic rendering harness only, Milestone 32 adds fixed memory-layout policy infrastructure only, Milestone 33 adds automatic deterministic layout sizing for tests/configuration only, Milestone 34 applies stack/heap size metadata to automatic layout only, Milestone 35 adds seeded/fresh randomized layout placement for tests/configuration only, Milestone 35A corrects MASM-compatible user-symbol case policy, Milestone 36 adds declared-object allocation map metadata for tests/internal tooling only, Milestone 37 adds allocated-object warning validation for tests/configuration only, Milestone 38 adds allocated-object strict validation for tests/configuration only, Milestone 39 adds uninitialized-origin byte metadata plus successful-write tracking for test-only inspection, Milestone 40 adds opt-in uninitialized-read warning and strict modes for tests/configuration, Milestone 41 adds virtual Irvine32 symbol-registry metadata plus specific diagnostics for known Irvine32 names before routine execution exists, Milestone 42 adds the zero-operand Irvine32 `exit` virtual terminator, Milestone 43 adds `inc` and `dec`, Milestone 44 adds `and`, `or`, and `xor`, Milestone 45 adds `not`, Milestone 46 adds `shl` and `sal`, Milestone 47 adds `shr`, Milestone 48 adds `sar`, Milestone 49 adds `rol`, and Milestone 50 adds `ror`. These infrastructure, compatibility-correction, exit-terminator, INC/DEC, logical-binary, unary-NOT, shift, ROL, and ROR milestones do not add CALL/RET, stack behavior, Program Console routines, other Irvine32 routine bodies, carry rotates, multiplication, division, labels, or jumps. This document is intentionally not a full MASM reference. Unsupported constructs listed here should produce stable diagnostics instead of vague parser errors.

## Implemented now

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
- Labels are accepted syntactically, but control-flow target resolution is not implemented yet.

### MASM32 header compatibility directives

Accepted before `.data` or `.code` as compatibility no-ops or metadata:

- `.386`, `.486`, `.586`, `.686` processor compatibility declarations.
- `.model flat, stdcall`. Other `.model` forms report `unsupported-model`.
- `.stack` and `.stack size`, where the optional size is stored as parser metadata. In automatic layout selected by tests/configuration, the parsed size controls stack region capacity metadata. Fixed-layout browser execution and runtime stack behavior are still deferred.
- `INCLUDE Irvine32.inc` and `INCLUDE Macros.inc` as virtual built-ins. The simulator does not load host files. `INCLUDE Irvine32.inc` registers known Irvine32 names as virtual metadata for classification and diagnostics. It also enables the zero-operand virtual `exit` terminator. It does not implement `call`, `ret`, ExitProcess behavior, stack behavior, Program Console routines, other Irvine32 bodies, Windows API behavior, linking, or host include loading. `INCLUDE Macros.inc` remains a virtual no-op and does not populate the Irvine32 registry. Other include files report `unsupported-include`.
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

Backend metadata note:

- Milestone 36 builds a declared-object allocation map for `.data`, `.DATA?`, and `.CONST` symbols after selected layout placement. It records object address ranges, declaration metadata, permissions, source locations, and a `not-tracked` initialization-origin placeholder for later diagnostics. It does not add object-bounds warnings, strict object errors, provenance checks, uninitialized-read diagnostics, or UI memory visualization.
- Milestone 37 adds a tests/configuration-facing allocated-object warning mode. When explicitly selected, valid-region memory accesses outside declared objects, partially overlapping object boundaries, or spanning adjacent objects emit `object-bounds-warning` simulator warnings and continue execution.
- Milestone 38 adds a tests/configuration-facing allocated-object strict mode. When explicitly selected, the same valid-region object-boundary escapes stop execution with runtime diagnostic code `object-bounds-violation`. Accesses wholly inside another declared object remain valid. Default region-only mode emits no object warnings or strict object errors. Provenance checks, uninitialized-read diagnostics, UI controls, and UI settings remain deferred.
- Milestone 39 adds uninitialized-origin byte metadata and write tracking for `.data` and `.DATA?` storage. Explicit initializer bytes start initialized; `?` and `DUP(?)` bytes start uninitialized-origin but remain deterministic zero-filled at runtime; successful writes mark only the written bytes initialized. This metadata is exposed only through test/internal inspection paths. It does not change default runtime values.
- Milestone 40 adds tests/configuration-facing uninitialized-read warning and strict modes. Warning mode emits `uninitialized-read` simulator warnings and continues; strict mode stops before the read with runtime diagnostic code `uninitialized-read`. Default browser/source-run behavior remains region-only and warning-free. UI settings controls and provenance diagnostics remain deferred.
- Milestone 41 adds a parser/source-run virtual Irvine32 symbol registry. Known names such as `WriteString`, `ReadInt`, `DumpRegs`, and `RandomRange` are classified for later routine milestones. Bare executable use of a known not-yet-executable Irvine32 name after `INCLUDE Irvine32.inc` reports `unsupported-irvine32-routine`.
- Milestone 42 adds the zero-operand Irvine32 `exit` virtual terminator when `INCLUDE Irvine32.inc` is active. `exit` terminates execution successfully, mutates no registers/flags/memory/Program Console state, and prevents later instructions from executing. Without the include, `exit` reports `unknown-instruction` with guidance to add `INCLUDE Irvine32.inc`; operands report `invalid-instruction-operands`. `CALL` target classification, Program Console routines, stack behavior, and Windows/API execution remain deferred.
- Milestone 43 adds `inc` and `dec` for 8-bit, 16-bit, and 32-bit register and unambiguous memory destinations. They update `ZF`, `SF`, and `OF`, preserve `CF`, and use checked memory read/write helpers for memory destinations.
- Milestone 44 adds `and`, `or`, and `xor` for compatible register, immediate, and unambiguous memory forms. They store the logical result, update `ZF` and `SF`, clear `CF` and `OF`, reject memory-to-memory and ambiguous memory/immediate forms, and use checked memory read/write helpers for memory operands.
- Milestone 45 adds `not` for register and unambiguous memory destinations. It stores the bitwise complement at the selected width, preserves `CF`, `ZF`, `SF`, and `OF`, rejects ambiguous memory-width forms, and uses checked memory read/write helpers for memory operands.
- Milestone 46 adds `shl` and `sal` for register and unambiguous memory destinations with immediate byte counts or `CL`. `sal` is an alias of `shl`; counts use `raw_count & 31`; count zero is a no-op; default undefined modeled-flag cases emit `undefined-shift-flag` warnings and continue; strict test/configuration mode reports `undefined-shift-flag` as a runtime error before mutation.
- Milestone 47 adds `shr` for the same register and unambiguous memory destination/count forms. `shr` shifts logically right, fills high bits with zero, sets `OF` from the original sign bit for a one-bit shift, and uses the same deterministic `undefined-shift-flag` warning/strict policy as the Phase 46 shifts.
- Milestone 48 adds `sar` for the same register and unambiguous memory destination/count forms. `sar` shifts arithmetically right, fills high bits with the original sign bit, clears `OF` for a one-bit shift, and uses the same deterministic `undefined-shift-flag` warning/strict policy as the other shift instructions.
- Milestone 49 adds `rol` for the same register and unambiguous memory destination/count forms. `rol` rotates bits left within the selected width, updates `CF` from the least significant bit of the rotated result, preserves `ZF` and `SF`, defines `OF` only for one-bit rotates, and emits `undefined-modeled-flag` warnings for non-one nonzero counts without using strict shift validation.
- Milestone 50 adds `ror` for the same register and unambiguous memory destination/count forms. `ror` rotates bits right within the selected width, updates `CF` from the most significant bit of the rotated result, preserves `ZF` and `SF`, defines `OF` only for one-bit rotates, and emits `undefined-modeled-flag` warnings for non-one nonzero counts without using strict shift validation.


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

Unsupported in this milestone:

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
- Milestone 29 constant expressions where a numeric initializer is valid, including in nested `DUP` counts and initializer values.

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
- Simple displacements for all 32-bit general-purpose base registers, such as `[eax + 4]`, `[ecx - 4]`, and `[esp + 8]`.
- Simple symbol/register forms: `array[reg32]`, `[array + reg32]`, where `reg32` is a supported 32-bit general-purpose base register.
- Width overrides: `BYTE PTR`, `WORD PTR`, `DWORD PTR`, `SBYTE PTR`, `SWORD PTR`, and `SDWORD PTR`.
- `QWORD PTR` and `SQWORD PTR` are recognized but executable 64-bit memory operations are rejected in MASM32 Educational Mode.

Array bracket offsets are byte offsets, not element indexes.

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

`nop` takes no operands. It advances execution without changing registers, flags, memory, or console state.

`adc` and `sbb` support register and memory destinations with compatible register, immediate, or memory sources where existing width rules are unambiguous. They use the current carry flag as carry-in or borrow-in and update the tracked arithmetic flags (`CF`, `ZF`, `SF`, and `OF`).

`clc`, `stc`, and `cmc` take no operands and mutate only the tracked carry flag.

`test` supports register/register, register/immediate, register/memory, memory/register, and memory/immediate forms when the memory width is explicit or inferable. It computes a transient bitwise AND, updates `ZF` and `SF`, clears `CF` and `OF`, and does not store the result. MASM-compatible ambiguous memory/immediate forms such as `test [esi], 1` and `test [esi + 4], 1` are rejected with an `ambiguous-memory-width` diagnostic; use `BYTE PTR`, `WORD PTR`, or `DWORD PTR`.

`inc` and `dec` support register destinations and memory destinations whose width is known from `PTR`, signed `PTR` aliases, direct symbol metadata, or symbol-offset metadata. Register aliases mutate only their selected width. Memory destinations are read-modify-write operations through checked memory helpers. Both instructions update `ZF`, `SF`, and `OF` and preserve `CF`. Untyped register-indirect forms such as `inc [eax]` and `dec [eax]` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`and`, `or`, and `xor` support register destinations and memory destinations with compatible register, immediate, or memory sources where existing width rules are unambiguous. They store the logical result in the destination, update `ZF` and `SF`, and clear `CF` and `OF`. Memory operands use checked memory helpers. Memory-to-memory forms such as `and value, other` are rejected with `invalid-instruction-operands`; untyped memory/immediate forms such as `xor [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`not` supports register destinations and memory destinations whose width is known from `PTR`, signed `PTR` aliases, direct symbol metadata, or symbol-offset metadata. It stores the bitwise complement at the selected width and preserves `CF`, `ZF`, `SF`, and `OF` exactly. Memory operands use checked memory helpers. Untyped register-indirect forms such as `not [eax]` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`shl`, `sal`, `shr`, and `sar` support register destinations and memory destinations whose width is known from `PTR`, signed `PTR` aliases, direct symbol metadata, or symbol-offset metadata. The count operand must be an immediate byte count or `CL`; no other register count is accepted. Counts use `raw_count & 31`; effective count zero is a no-op, effective count one updates `CF`, `ZF`, `SF`, and `OF` according to the selected shift mnemonic, and larger effective counts use the deterministic undefined-flag policy. Default mode emits `undefined-shift-flag` warnings and continues for undefined modeled-flag cases; the warning names the modeled flags updated from the result and the undefined modeled flags preserved by simulator policy. Strict test/configuration mode reports `undefined-shift-flag` as a runtime error before mutation. Untyped register-indirect forms such as `shl [eax], 1`, `shr [eax], 1`, and `sar [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`rol` supports the same register and unambiguous memory destination/count forms. Counts use `raw_count & 31`; effective count zero is a full no-op, and nonzero counts rotate left by `effective_count % operand_width`. `CF` is set from bit 0 of the rotated result, `ZF` and `SF` are preserved, and `OF` is defined only for effective count 1. For non-one nonzero counts, `OF` is preserved deterministically and default execution emits `undefined-modeled-flag`; strict shift validation remains shift-only and does not convert `ROL` warnings into runtime errors. Untyped register-indirect forms such as `rol [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`ror` supports the same register and unambiguous memory destination/count forms. Counts use `raw_count & 31`; effective count zero is a full no-op, and nonzero counts rotate right by `effective_count % operand_width`. `CF` is set from the most significant bit of the rotated result, `ZF` and `SF` are preserved, and `OF` is defined only for effective count 1. For non-one nonzero counts, `OF` is preserved deterministically and default execution emits `undefined-modeled-flag`; strict shift validation remains shift-only and does not convert `ROR` warnings into runtime errors. Untyped register-indirect forms such as `ror [eax], 1` are rejected with `ambiguous-memory-width`; executable QWORD/SQWORD memory operations remain deferred.

`exit` is accepted only as a zero-operand virtual Irvine32 terminator after `INCLUDE Irvine32.inc`. It terminates execution successfully and skips following instructions without changing registers, flags, memory, or Program Console output. It is not `call ExitProcess` and does not model Windows API behavior.

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

Milestone 17 and later can report multiple safely recoverable `unsupported-feature` diagnostics in one parse. The parser skips known unsupported line-level constructs, block-like constructs, and unsupported sections only far enough to resynchronize; programs with any diagnostics are not executed.

Recovered line-level constructs include `INVOKE`, `PROTO`, `LOCAL`, `TEXTEQU`, `INCLUDELIB`, `EXTERN`, `EXTERNDEF`, `EXTRN`, `PUBLIC`, `COMM`, `ASSUME`, `ALIGN`, `EVEN`, `LABEL`, `ORG`, `COMMENT`, and `ECHO`.

Recovered block-like constructs include `STRUCT` / `ENDS`, `UNION` / `ENDS`, `MACRO` / `ENDM`, `.IF` / `.ENDIF`, `.WHILE` / `.ENDW`, and `.REPEAT` / `.UNTIL` or `.UNTILCXZ`.

`.DATA?` and `.CONST` were promoted from recovered unsupported sections to implemented data sections in Milestone 27. Numeric equates and Stage A constant expressions were promoted to implemented syntax in Milestone 28. Extended constant-expression operators were promoted to implemented syntax in Milestone 29. Nested `DUP` expansion was promoted to implemented syntax in Milestone 30. Milestone 31 added a native diagnostic JSON producer plus Node rendering harness for exact Simulator Messages tests; Milestone 32 added a fixed-layout policy object consumed by VM memory initialization; Milestone 33 added automatic deterministic region sizing for tests/configuration; Milestone 34 applies parsed `.stack` metadata and configured heap-size requests to automatic layout capacity metadata; Milestone 35 adds seeded/fresh randomized layout placement for tests/configuration, with symbolic addresses relocated to selected bases while fixed numeric addresses remain literal; Milestone 36 adds declared-object allocation map metadata; Milestone 37 adds allocated-object warning validation for tests/configuration while leaving default region-only execution unchanged; Milestone 38 adds allocated-object strict validation for tests/configuration while still leaving default region-only execution unchanged; Milestone 39 adds uninitialized-origin byte metadata plus successful-write tracking for test/internal inspection, Milestone 40 adds opt-in uninitialized-read warning and strict modes while preserving default runtime output, Milestone 41 adds virtual Irvine32 registry metadata, Milestone 42 adds the zero-operand virtual `exit` terminator, Milestone 43 adds `inc` and `dec` runtime instruction behavior, Milestone 44 adds `and`, `or`, and `xor` runtime instruction behavior, Milestone 45 adds `not` runtime instruction behavior, Milestone 46 adds `shl`/`sal` runtime instruction behavior, Milestone 47 adds `shr` runtime instruction behavior, Milestone 48 adds `sar` runtime instruction behavior, Milestone 49 adds `rol` runtime instruction behavior, and Milestone 50 adds `ror` runtime instruction behavior. The infrastructure-only milestones do not prove stale `web/dist` artifacts were rebuilt; Milestone 50 intentionally adds only rotate-right instruction semantics beyond the prior rotate-left behavior and does not add carry rotates.

## Backlog notes

Additional data types tracked for later compatibility work:

- `REAL4`
- `REAL8`
- `REAL10`
- `TBYTE`
- `FWORD`

Expression parser expansion tracked for later compatibility work:

- Full MASM expression compatibility beyond the implemented Milestone 29 constant-expression operators.
- Runtime logical and relational expressions.
- Binary and octal literals.
- `.RADIX`.
- `SHORT`, `THIS`, and segment-related expression operators.

## Still deferred

Deferred systems include control flow, stack initialization, `push`, `pop`, `call`, `ret`, Irvine32 routines beyond the virtual `exit` terminator, debugger stepping, breakpoints, scaled-index addressing, macro expansion, Windows API modeling, and full MASM expression compatibility.
