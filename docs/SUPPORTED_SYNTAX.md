# Supported MASM32 Educational Simulator Syntax

Repository/archive milestone:
Phase 58 - Code Label Table and Label Diagnostics

Runtime/source-run MASM behavior phase:
Phase 58 - Code Label Table and Label Diagnostics: The simulator currently accepts code labels such as `start:` and procedure-entry labels such as `main PROC` as parser/source metadata. Labels do not execute, do not create IR instructions, and do not add branch behavior. Duplicate or conflicting labels produce structured Simulator Messages diagnostics.

### Code labels

Ordinary code labels (`name:`) and procedure-entry labels (`name PROC`) are accepted and recorded as parser/source metadata. Consecutive labels before one executable instruction target the same following instruction. Labels before `ENDP`, `END`, or another non-executable boundary are accepted as no-executable-target metadata.

Branch consumers remain future work: `jmp label`, conditional jumps, `loop`, `call`, `ret`, and stack/procedure execution remain unsupported.

### Unsupported high-level flow diagnostics

High-level MASM flow constructs are recognized but not executable. The parser/source-run path reports source-located unsupported-feature diagnostics for `.IF`, `.ELSE`, `.ENDIF`, `.ELSEIF`, `.WHILE`, `.ENDW`, `.REPEAT`, `.UNTIL`, `.UNTILCXZ`, `.BREAK`, and `.CONTINUE` where they are encountered. Recognized unsupported high-level-flow blocks are skipped for recovery where safe so body instructions do not execute and do not produce unrelated cascaded diagnostics. The simulator does not lower these constructs to labels, jumps, or branches.

### Segment/group and `.code` memory diagnostics

The simulator executes internal IR and source metadata. It does not expose `.code` as user-readable or user-writable simulated program memory, does not emit real x86 opcode bytes, and does not model a PE `.text` image. Attempts to access `.code` through memory operands produce `unsupported-code-memory-access` or, for cross-region protected overlaps, `region-boundary-crossing`.

MASM segment/group names such as `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` are linker/object concepts, not aliases for simulator memory regions. They produce targeted `unsupported-segment-symbol` diagnostics where recognized.

### Startup and data diagnostics

Default successful browser/source-run execution keeps deterministic zero-startup behavior: registers, modeled flags, and visible bytes of uninitialized storage start at zero while uninitialized-origin metadata is preserved. The `startup-state-notice` remains non-fatal and is emitted through Simulator Messages by default.

Source-run/test-facing settings can opt into deterministic seeded startup for general-purpose registers, modeled flags, and visible bytes of uninitialized-origin storage. Initialized `.data` and initialized `.CONST` bytes are not randomized. Accepted `.CONST ?` and `.CONST DUP(?)` storage remains read-only, and its declaration diagnostic is controlled separately from read-time `uninitialized-read` diagnostics.

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
- Labels are accepted as parser/source metadata, but control-flow target resolution is not implemented yet.

### MASM32 header compatibility directives

Accepted before `.data` or `.code` as compatibility no-ops, metadata-only directives, virtual includes, or semantic directives:

- `.386`, `.486`, `.586`, `.686` processor compatibility declarations.
- `.model flat, stdcall`. Other `.model` forms report `unsupported-model`.
- `.stack` and `.stack size`, where the optional size is stored as parser metadata. In automatic layout selected by tests/configuration, the parsed size controls stack region capacity metadata. Fixed-layout browser execution and runtime stack behavior are still deferred.
- `INCLUDE Irvine32.inc` and `INCLUDE Macros.inc` as virtual built-ins. The simulator does not load host files. `INCLUDE Irvine32.inc` registers known Irvine32 names as virtual metadata for classification and diagnostics. It also enables the zero-operand virtual `exit` terminator. It does not implement `call`, `ret`, ExitProcess behavior, stack behavior, Program Console routines, other Irvine32 bodies, Windows API behavior, linking, or host include loading. `INCLUDE Macros.inc` remains a virtual no-op and does not populate the Irvine32 registry. `INCLUDELIB` directives are not virtual includes and are not accepted as no-ops; they report linker/library diagnostics such as `unsupported-includelib`, `unsupported-masm32-library`, or `unsupported-windows-api-library`. Basename-only unsupported include files report `unsupported-include`. Host/path-like include operands such as `include \masm32\include\masm32.inc`, `include C:\masm32\include\kernel32.inc`, `include ..\include\file.inc`, `include .\local.inc`, and `include /usr/local/include/file.inc` report host/path diagnostics such as `unsupported-host-include-path`, `unsupported-masm32-library-include`, or `unsupported-windows-api-include` instead of repeated lexer path-separator errors.

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

- The default memory validation mode is region-only: final byte ranges, permissions, `.CONST` protection, uninitialized-origin reads, and unaligned accesses are runtime concerns routed through checked VM memory helpers.
- Optional source-run/test-facing validation policies can report declared-object, section-capacity, and section-image warnings or strict errors without changing the default browser behavior.
- Uninitialized-origin metadata is tracked for `.DATA?`, `?`, `DUP(?)`, and accepted `.CONST ?` / `.CONST DUP(?)` storage. Default source-run behavior warns on uninitialized reads while preserving deterministic visible bytes.
- The virtual Irvine32 registry recognizes known routine names for classification and diagnostics. Only the zero-operand virtual `exit` terminator is executable in the current subset; other Irvine32 routines, `CALL`, stack behavior, and Windows/API execution remain deferred or outside scope.
- The current instruction subset includes the arithmetic, logic, shift, rotate, multiply, divide, conversion, exchange, negation, NOP, and effective-address forms listed below.


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

`.DATA?` and `.CONST` were promoted from recovered unsupported sections to implemented data sections in Milestone 27. Numeric equates and Stage A constant expressions were promoted to implemented syntax in Milestone 28. Extended constant-expression operators were promoted to implemented syntax in Milestone 29. Nested `DUP` expansion was promoted to implemented syntax in Milestone 30. Milestone 31 added a native diagnostic JSON producer plus Node rendering harness for exact Simulator Messages tests; Milestone 32 added a fixed-layout policy object consumed by VM memory initialization; Milestone 33 added automatic deterministic region sizing for tests/configuration; Milestone 34 applies parsed `.stack` metadata and configured heap-size requests to automatic layout capacity metadata; Milestone 35 adds seeded/fresh randomized layout placement for tests/configuration, with symbolic addresses relocated to selected bases while fixed numeric addresses remain literal; Milestone 36 adds declared-object allocation map metadata; Milestone 37 adds allocated-object warning validation for tests/configuration while leaving default region-only execution unchanged; Milestone 38 adds allocated-object strict validation for tests/configuration while still leaving default region-only execution unchanged; Milestone 39 adds uninitialized-origin byte metadata plus successful-write tracking for test/internal inspection, Milestone 40 adds opt-in uninitialized-read warning and strict modes while preserving the then-current default runtime output, Milestone 41 adds virtual Irvine32 registry metadata, Milestone 42 adds the zero-operand virtual `exit` terminator, Milestone 43 adds `inc` and `dec` runtime instruction behavior, Milestone 44 adds `and`, `or`, and `xor` runtime instruction behavior, Milestone 45 adds `not` runtime instruction behavior, Milestone 46 adds `shl`/`sal` runtime instruction behavior, Milestone 47 adds `shr` runtime instruction behavior, Milestone 48 adds `sar` runtime instruction behavior, Milestone 49 adds `rol` runtime instruction behavior, Milestone 50 adds `ror` runtime instruction behavior, Milestone 50A adds internal modeled-flag validity metadata, Milestone 50B adds opt-in undefined flag-use diagnostics for existing `CF` consumers, Milestone 51 adds validation-only smoke-harness coverage, and Milestone 52 adds `lea` runtime effective-address computation. The infrastructure-only milestones do not prove stale `web/dist` artifacts were rebuilt; Milestone 50 intentionally adds only rotate-right instruction semantics beyond the prior rotate-left behavior, Milestone 50A intentionally adds no new source syntax or visible default diagnostics, Milestone 50B adds only opt-in consumer diagnostics without new source syntax or default browser behavior changes at that time, Milestone 51 adds validation-only smoke-harness coverage without new source syntax or runtime behavior, and Milestone 52 adds only `lea` effective-address computation without scaled-index addressing or memory reads; Milestone 52A adds only signed register and memory value display formatting without source syntax or runtime semantics changes; Milestone 53 adds only unsigned one-operand `mul` without signed multiplication, division, or multi-operand multiplication forms; Milestone 53A adds only memory-validation policy clarification and symbol-offset runtime correction without new MASM syntax or new instruction behavior; Milestone 53B adds opt-in section-capacity and section-image validation modes without changing default region-only behavior; Milestone 53C changes omitted/default browser source-run behavior so the existing uninitialized-read and undefined-flag-use diagnostics warn by default; Milestone 53D adds default compatibility notices for accepted no-op, metadata-only, and limited-behavior MASM constructs without adding new syntax or runtime execution behavior; Milestone 53E exposes existing diagnostic policies through browser UI settings without adding backend semantics; Milestone 54 adds only one-operand signed `imul`; Milestone 55 adds only two- and three-operand signed `imul` forms without division or other multiplication forms; Milestone 56 adds only unsigned one-operand `div` without signed `idiv` or new multiplication forms; Milestone 57 adds only signed one-operand `idiv` without control flow, stack behavior, QWORD/SQWORD execution, or new multiplication forms.

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

Deferred systems include control flow, stack initialization, `push`, `pop`, `call`, `ret`, Irvine32 routines beyond the virtual `exit` terminator, debugger stepping, breakpoints, scaled-index addressing, carry rotates, macro expansion, Windows API modeling, and full MASM expression compatibility.
