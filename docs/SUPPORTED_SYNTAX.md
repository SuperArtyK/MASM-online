# Supported MASM32 Educational Simulator Syntax

This reference describes the implemented source subset through Milestone 21. It is intentionally not a full MASM reference. Unsupported constructs listed here should produce stable `unsupported-feature` diagnostics instead of vague parser errors.

## Implemented now

### Sections and procedure shape

```asm
.data
; optional data declarations
.code
main PROC
    ; supported instructions
main ENDP
END main
```

Supported structural forms:

- Optional `.data` section before `.code`.
- Required `.code` section.
- Procedure markers using `PROC` and `ENDP` as structural markers.
- `END name` entry-point validation.
- Labels are accepted syntactically, but control-flow target resolution is not implemented yet.

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
- Flat `DUP`, such as `10 DUP(0)`.
- `?`, represented deterministically as zero-filled storage while retaining uninitialized metadata.

### Operators and memory operands

Implemented operators:

- `OFFSET symbol`
- `TYPE symbol`
- `LENGTHOF symbol`
- `SIZEOF symbol`

Implemented memory forms:

- Direct symbols: `mov var, 100`, `mov eax, var`.
- Constant byte offsets: `nums[8]`, `[nums + 8]`, `[nums - 4]`, `[nums]`, `[nums + 0]`, `nums[0]`.
- Register-indirect forms: `[esi]`, `[edi]`, `[ebx]`, `[ebp]`.
- Simple displacements: `[esi + 4]`, `[esi - 4]`.
- Simple symbol/register forms: `array[esi]`, `[array + esi]`.
- Width overrides: `BYTE PTR`, `WORD PTR`, `DWORD PTR`.
- `QWORD PTR` and `SQWORD PTR` are recognized but executable 64-bit memory operations are rejected in MASM32 Educational Mode.

Array bracket offsets are byte offsets, not element indexes.

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

`movsx` and `movzx` require a register destination and an 8-bit or 16-bit register or memory source whose width is narrower than the destination. Register-indirect memory sources such as `[esi]` require `BYTE PTR` or `WORD PTR` because the source width is otherwise ambiguous.

Accumulator conversions are no-operand instructions:

- `cbw`: sign-extend `AL` into `AX`
- `cwde`: sign-extend `AX` into `EAX`
- `cwd`: sign-extend `AX` into `DX:AX`
- `cdq`: sign-extend `EAX` into `EDX:EAX`

Ordinary `mov` from signed memory does not sign-extend automatically; use `movsx` when sign extension is required.

`xchg` supports register/register, register/memory, and memory/register exchanges when both operands have matching widths. Memory operands must use existing direct-symbol, symbol-offset, or `PTR`/register-indirect forms that make the access width unambiguous. `xchg` does not modify tracked flags.

`neg` supports register and memory destinations with 8-bit, 16-bit, or 32-bit widths. It updates the tracked arithmetic flags (`CF`, `ZF`, `SF`, and `OF`) using the destination width.

`nop` takes no operands. It advances execution without changing registers, flags, memory, or console state.

`adc` and `sbb` support register and memory destinations with compatible register, immediate, or memory sources where existing width rules are unambiguous. They use the current carry flag as carry-in or borrow-in and update the tracked arithmetic flags (`CF`, `ZF`, `SF`, and `OF`).

`clc`, `stc`, and `cmc` take no operands and mutate only the tracked carry flag.

## Recognized unsupported features

The parser should report `unsupported-feature` for these recognizable textbook constructs until their milestones are implemented:

- `.DATA?`
- `.CONST`
- `EQU`
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
- `PUBLIC`
- `COMM`


## Diagnostic recovery behavior

Milestone 17 and later can report multiple safely recoverable `unsupported-feature` diagnostics in one parse. The parser skips known unsupported line-level constructs, block-like constructs, and unsupported sections only far enough to resynchronize; programs with any diagnostics are not executed.

Recovered line-level constructs include `INVOKE`, `PROTO`, `LOCAL`, `EQU`, `TEXTEQU`, `INCLUDELIB`, `EXTERN`, `PUBLIC`, and `COMM`.

Recovered block-like constructs include `STRUCT` / `ENDS`, `UNION` / `ENDS`, `MACRO` / `ENDM`, `.IF` / `.ENDIF`, `.WHILE` / `.ENDW`, and `.REPEAT` / `.UNTIL` or `.UNTILCXZ`.

Recovered unsupported sections include `.DATA?` and `.CONST`.

## Backlog notes

Additional data types tracked for later compatibility work:

- `REAL4`
- `REAL8`
- `REAL10`
- `TBYTE`
- `FWORD`

Expression parser expansion tracked for later compatibility work:

- Arithmetic expressions.
- Logical expressions.
- Relational expressions.
- Parenthesized expressions.
- Binary and octal literals.
- `.RADIX`.
- `HIGH`, `LOW`, `HIGHWORD`, `LOWWORD`, `SHORT`, and `THIS`.

## Still deferred

Deferred systems include control flow, stack initialization, `push`, `pop`, `call`, `ret`, Irvine32 routines, debugger stepping, breakpoints, scaled-index addressing, nested `DUP`, macro expansion, Windows API modeling, and full MASM expression compatibility.
