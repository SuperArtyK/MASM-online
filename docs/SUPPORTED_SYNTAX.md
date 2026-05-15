# Supported MASM32 Educational Simulator Syntax

This reference describes the implemented source subset through Milestone 28. It is intentionally not a full MASM reference. Unsupported constructs listed here should produce stable `unsupported-feature` diagnostics instead of vague parser errors.

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
- `.DATA?` declarations must use `?` or flat `DUP(?)` uninitialized initializers. They are emitted as deterministic zero-filled writable storage while retaining metadata that they were originally uninitialized.
- `.CONST` declarations are emitted into read-only storage. Direct writes to known `.CONST` symbols are assembly diagnostics, and calculated-address writes fail at runtime through checked memory permissions.
- Procedure markers using `PROC` and `ENDP` as structural markers.
- `END name` entry-point validation.
- Labels are accepted syntactically, but control-flow target resolution is not implemented yet.

### MASM32 header compatibility directives

Accepted before `.data` or `.code` as compatibility no-ops or metadata:

- `.386`, `.486`, `.586`, `.686` processor compatibility declarations.
- `.model flat, stdcall`. Other `.model` forms report `unsupported-model`.
- `.stack` and `.stack size`, where the optional size is stored as parser metadata only. Runtime stack behavior is still deferred.
- `INCLUDE Irvine32.inc` and `INCLUDE Macros.inc` as virtual built-ins. The simulator does not load host files. Other include files report `unsupported-include`.
- `OPTION CASEMAP:NONE` as a compatibility no-op documenting the simulator case policy. Other `OPTION` forms report `unsupported-option`.
- `TITLE`, `SUBTITLE`, and `PAGE` as listing/documentation no-ops.

Case policy:

- Instructions, registers, directives, operators, and data type names are case-insensitive.
- User-defined symbols are case-sensitive in MASM32 Educational Mode.


### Numeric equates and Stage A constant expressions

Implemented numeric equate forms:

```asm
COUNT = 4
EXTRA EQU 2
```

Numeric equates are compile-time constants. They are stored separately from data labels and are not addressable storage symbols. Supported Stage A constant-expression syntax includes:

- numeric literals already supported by the lexer;
- numeric equate identifiers;
- unary `+` and unary `-`;
- parentheses;
- binary `+` and binary `-`.

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
- Stage B operators such as `*`, `/`, `MOD`, `SHL`, `SHR`, `AND`, `OR`, `XOR`, `NOT`, `HIGH`, and `LOW`.

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
- Flat `DUP`, such as `10 DUP(0)` or `COUNT DUP(0)`.
- `?`, represented deterministically as zero-filled storage while retaining uninitialized metadata.
- Stage A constant expressions where a numeric initializer is valid.

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

`.DATA?` and `.CONST` were promoted from recovered unsupported sections to implemented data sections in Milestone 27. Numeric equates and Stage A constant expressions were promoted to implemented syntax in Milestone 28.

## Backlog notes

Additional data types tracked for later compatibility work:

- `REAL4`
- `REAL8`
- `REAL10`
- `TBYTE`
- `FWORD`

Expression parser expansion tracked for later compatibility work:

- Extended arithmetic expressions beyond binary `+` and `-`, including `*`, `/`, and `MOD`.
- Logical expressions.
- Relational expressions.
- Binary and octal literals.
- `.RADIX`.
- `HIGH`, `LOW`, `HIGHWORD`, `LOWWORD`, `SHORT`, and `THIS`.

## Still deferred

Deferred systems include control flow, stack initialization, `push`, `pop`, `call`, `ret`, Irvine32 routines, debugger stepping, breakpoints, scaled-index addressing, nested `DUP`, macro expansion, Windows API modeling, and full MASM expression compatibility.
