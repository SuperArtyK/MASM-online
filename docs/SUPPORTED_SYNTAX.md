# Supported MASM32 Educational Simulator Syntax

This reference describes the implemented source subset through Milestone 15. It is intentionally not a full MASM reference. Unsupported constructs listed here should produce stable `unsupported-feature` diagnostics instead of vague parser errors.

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
- `QWORD PTR` is recognized but executable QWORD memory operations are rejected in MASM32 Educational Mode.

Array bracket offsets are byte offsets, not element indexes.

### Instructions

Implemented executable instructions:

- `mov`
- `add`
- `sub`

## Scheduled soon

The following signed integer data declarations are scheduled for the next milestone and should not be treated as permanently unsupported:

- `SBYTE`
- `SWORD`
- `SDWORD`
- `SQWORD`

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
