# Supported MASM32 Educational Simulator Syntax

Repository/archive milestone:
Phase 57O - Explicit-Width NOP Encoding-Operand Forms

Runtime/source-run MASM behavior phase:
Phase 57O - Explicit-Width NOP Encoding-Operand Forms

Phase 57O accepts selected MASM-compatible 16-bit and 32-bit `nop` encoding operands as source-level no-ops. Accepted forms include zero-operand `nop`, register-form encoding operands such as `nop ax` and `nop eax`, and explicit `WORD PTR` / `SWORD PTR` / `DWORD PTR` / `SDWORD PTR` memory-looking encoding operands that reuse the existing memory-addressing grammar. Register-form NOP operands are not read or written, and memory-looking NOP operands are not simulated memory operands: execution does not evaluate the effective address, read memory, write memory, perform planned memory validation, emit memory diagnostics, or create memory-change rows. Unsupported `nop` operand forms remain diagnosed, and real x86 byte encoding, `.code` byte images, disassembly, and PE/object/linker behavior remain outside current behavior. Phase 57M - MASM Segment and Group Symbol Diagnostics targeted `unsupported-segment-symbol` parser/source-run diagnostics remain active for MASM/object/linker segment and group names such as `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT`. Phase 57L `.code` memory diagnostics, Phase 57J configurable `const-uninitialized-storage` declaration diagnostics, Phase 57I accepted `.CONST ?` / `.CONST DUP(?)` storage, Phase 57H final-register `[unchanged]` display markers, Phase 57G seeded uninitialized-storage visible-byte settings, Phase 57F seeded register/flag startup, Phase 57E startup-state notices, Phase 57D diagnostic-policy migration, Phase 57C diagnostic-policy registry design, Phase 57B documentation extraction, Phase 57A README cleanup, Phase 57-CORR2 compact negative register-indirect displacement parsing, and Phase 57-CORR1 protected-region diagnostic clarification remain accepted behavior.

This reference describes the implemented MASM source syntax and instruction subset through Phase 57 - Signed IDIV plus later diagnostics, display, startup, `.CONST`, segment-symbol, `.code` access, and NOP encoding-operand phases through Phase 57O - Explicit-Width NOP Encoding-Operand Forms. This document is intentionally not a full MASM reference. Unsupported constructs listed here should produce stable diagnostics instead of vague parser errors.

Historical infrastructure note: Milestone 32 adds fixed memory-layout policy infrastructure only; later layout, diagnostic, and startup settings build on that infrastructure without changing the current instruction subset unless their phase explicitly says so.

### Phase 57M MASM segment/group symbol diagnostics and Phase 57L `.code` memory diagnostics

Phase 57L enforces the v1 `.code` memory access policy at runtime/source-run level. The simulator executes internal IR and source metadata. It does not expose `.code` as user-readable or user-writable simulated program memory, does not emit real x86 opcode bytes, and does not model a PE `.text` image. If an existing source form reaches a `.code` address through register-indirect or displacement memory operands after loading a `.code` address into a register, the access now fails before reading a value or mutating memory. Wholly contained `.code` reads and writes report `unsupported-code-memory-access`; cross-region `.code` overlaps report `region-boundary-crossing`. The rendered diagnostic explains that `.CODE/_TEXT` is not exposed as an accessible memory region and that the program stopped before access. This is diagnostic wording only: `_TEXT` remains an unsupported MASM/object segment symbol and is not an addressable alias for the simulator's internal `.code` region.

Phase 57M implements the MASM segment/group symbol policy with targeted `unsupported-segment-symbol` diagnostics. `_TEXT`, `_DATA`, `_BSS`, `CONST`, `STACK`, `DGROUP`, and `FLAT` are MASM/object/linker concepts, not aliases for simulator regions. They must not be used to access `.code`, `.data`, `.DATA?`, `.CONST`, stack, heap, or any internal VM region. Users should declare ordinary data labels and use `OFFSET label` for simulator data addresses. Segment/group definition forms such as `_TEXT SEGMENT`, `_DATA ENDS`, and `DGROUP GROUP _DATA, _BSS` are diagnosed instead of creating simulator symbols or linker metadata. Under the default `CASEMAP:ALL`, case variants such as `_text` and `dgroup` are diagnosed as the recognized unsupported names. Under `OPTION CASEMAP:NONE`, exact recognized spellings are diagnosed, while different-case ordinary user labels such as `_text` may be used as normal data symbols.

### Phase 57F, Phase 57G, Phase 57I, and Phase 57J startup/data diagnostics

Runtime/source-run MASM behavior phase Phase 57O preserves the independent source-run/test-facing startup settings added by Phase 57F and Phase 57G:

```text
startup_register_flag_mode = zero | seeded-random
uninitialized_storage_visible_byte_mode = zero | seeded-random
startup_state_seed = <u32>
```

Default successful browser/source-run execution keeps the deterministic zero-startup behavior: registers, modeled flags, and visible bytes of uninitialized storage start at zero, while uninitialized-origin metadata is preserved for code-quality diagnostics. The `startup-state-notice` remains non-fatal, is emitted only through Simulator Messages, and can be disabled through the diagnostic policy path.

When `startup_register_flag_mode` is `seeded-random`, the simulator initializes EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, and the currently modeled flags CF, ZF, SF, and OF from a deterministic pseudo-random stream derived from `startup_state_seed`. EIP remains zero, unmodeled EFLAGS bits remain zero, and modeled flag-validity metadata remains valid.

When `uninitialized_storage_visible_byte_mode` is `seeded-random`, the simulator initializes only visible bytes of storage that still carry uninitialized-origin metadata, including `.DATA?`, scalar `?`, `DUP(?)`, and Phase 57I accepted `.CONST ?` / `.CONST DUP(?)` storage. The same source, settings, seed, and input produce the same visible bytes. Different seeds are expected to produce at least one different byte in targeted fixtures.

Initialized `.data` and initialized `.CONST` bytes are not randomized. Uninitialized-origin metadata remains preserved, so reads from randomized uninitialized-origin bytes still emit uninitialized-read diagnostics according to the selected policy. Accepted `.CONST ?` and `.CONST DUP(?)` storage remains read-only even when seeded visible bytes are enabled. The `const-uninitialized-storage` declaration diagnostic is independent from read-time `uninitialized-read`: the default is `warn`, `off` suppresses only the declaration diagnostic, and `error` rejects execution before runtime.

### Phase 57-CORR1 memory diagnostic clarification

Repository/archive milestone Phase 57-CORR1 clarifies one runtime memory diagnostic without adding MASM syntax or changing the runtime/source-run MASM behavior phase.

Runtime/source-run MASM behavior remains:

```text
Phase 57 - Signed IDIV
```

Diagnostic behavior clarified by Phase 57-CORR1:

- a cross-region memory read or write whose requested final byte range intersects protected `.CONST` storage reports `region-boundary-crossing`;
- the rendered message identifies the access kind, attempted address, byte count, final byte range, `.CONST`, and the runtime `.CONST` base address;
- the `.CONST` base address in the diagnostic must come from active layout metadata and must not be hardcoded to the fixed-layout default;
- a direct or wholly-contained write into `.CONST` remains `permission-denied`;
- a wholly-contained read from `.CONST` remains allowed when otherwise valid;
- unrelated cross-region failures remain ordinary Level 1 region/range diagnostics unless they intersect a known protected region;
- the simulator still never stitches one memory access across independent VM regions.

## Implemented now

- Milestone 53 adds unsigned one-operand `mul` for 8-bit, 16-bit, and 32-bit register or unambiguous memory sources. It uses the implicit accumulator forms `AL * r/m8 -> AX`, `AX * r/m16 -> DX:AX`, and `EAX * r/m32 -> EDX:EAX`; updates `CF` and `OF` according to whether the upper product half is nonzero; preserves `ZF` and `SF`; and uses checked memory reads without producing memory-change rows for source-only memory operands.
- Milestone 53A clarifies memory validation as Level 1 region-only validation by default and existing Level 4 declared-object validation for allocated-object warning/strict modes. Symbol-offset operands such as `DWORD PTR [x+1]` or `mul [x+1]` are no longer assembly errors merely because the inferred access crosses a declared object, section image, section capacity, or fixed-layout slack; final byte ranges are controlled by checked runtime memory helpers and enabled validation modes.
- Milestone 53B implements opt-in Level 2 section-capacity and Level 3 section-image validation for local tests/source-run configuration. `warn` mode emits `section-capacity-violation` or `section-image-violation` simulator warnings and continues. `strict` mode emits the same codes as runtime errors before mutation. Default browser/source execution still leaves section-capacity and section-image validation off; no browser UI controls were added by Phase 53B.
- Milestone 53C changes omitted/default browser source-run behavior so reads from uninitialized-origin bytes emit non-fatal `uninitialized-read` warnings and consumers of invalid modeled flags emit non-fatal `undefined-flag-use` warnings. Execution remains deterministic and continues in warning mode. Explicit region-only/off policies preserve prior silent behavior, and strict/error policies remain opt-in.
- Milestone 53D adds default non-fatal `simulator-notice` messages for accepted MASM compatibility constructs whose real MASM behavior is no-op, metadata-only, virtual-only, or limited in the simulator. Notices do not write Program Console output and do not change execution semantics.
- Milestone 53E adds browser UI settings for existing memory range validation, uninitialized-read diagnostics, undefined-flag-use diagnostics, and compatibility notices. Defaults remain region-only memory validation, uninitialized-read warn, undefined-flag-use warn, and compatibility notices on. The settings do not add new runtime validation semantics, new MASM syntax, or new diagnostic categories; they route to already-implemented backend policies.
- Milestone 52A adds signed decimal interpretations to existing known-width final register and memory-change displays while preserving existing hexadecimal and unsigned decimal display. Final registers use aligned grouped rows for register families, and memory changes use aligned old/new blocks. This display-only milestone adds no accepted syntax, parser behavior, VM semantics, diagnostics, Program Console output, or Simulator Messages text.
- Milestone 52 adds `lea` for effective-address computation into a 32-bit register. Milestone 51 added no accepted syntax and expanded aggregate local-test reporting and smoke coverage for post-30 memory, diagnostic, Irvine32 `exit`, CASEMAP, and instruction-family regressions.


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

Accepted before `.data` or `.code` as compatibility no-ops, metadata-only directives, virtual includes, or semantic directives:

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
- Milestone 38 adds a tests/configuration-facing allocated-object strict mode. When explicitly selected, the same valid-region object-boundary escapes stop execution with runtime diagnostic code `object-bounds-violation`. Accesses wholly inside another declared object remain valid. Default region-only mode emits no object warnings or strict object errors. Provenance diagnostics remain deferred. Milestone 53E exposes declared-object warning/strict validation as an optional browser memory range setting without changing default region-only behavior.
- Milestone 39 adds uninitialized-origin byte metadata and write tracking for `.data` and `.DATA?` storage. Explicit initializer bytes start initialized; `?` and `DUP(?)` bytes start uninitialized-origin but remain deterministic zero-filled at runtime; successful writes mark only the written bytes initialized. This metadata is exposed only through test/internal inspection paths. It does not change default runtime values.
- Milestone 40 adds tests/configuration-facing uninitialized-read warning and strict modes. Warning mode emits `uninitialized-read` simulator warnings and continues; strict mode stops before the read with runtime diagnostic code `uninitialized-read`. Milestone 53C makes omitted/default browser source-run behavior use warning mode, while explicit off preserves prior silent deterministic-zero reads. Milestone 53E exposes uninitialized-read warn/off/strict choices in the browser UI. Provenance diagnostics remain deferred.
- Milestone 53A documents the active memory-validation levels and keeps default execution as Level 1 region-only validation. The current runtime stores `.data` and `.DATA?` together in one writable data VM region and `.CONST` in a separate read-only const VM region. The parser does not use object, section-image, or section-capacity bounds to reject otherwise valid symbol-offset memory operands; final byte-range, permission, `.CONST`, Level 4, uninitialized-read, and unaligned diagnostics are runtime concerns.
- Milestone 53B implements Level 2 section-capacity and Level 3 section-image validation as opt-in local test/source-run policies with `off`, `warn`, and `strict` behavior. Warning mode emits `section-capacity-violation` or `section-image-violation` simulator warnings and continues. Strict mode emits the same codes as runtime errors before mutation. Default browser/source execution still leaves section-capacity, section-image, and allocated-object validation off; existing allocated-object warning/strict modes remain Level 4 declared-object validation.
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
- Milestone 50A adds internal validity metadata for the currently modeled flags: `CF`, `ZF`, `SF`, and `OF`. Defined flag writes mark the flag valid, architecturally preserved flags preserve validity metadata, and undefined shift/rotate flag cases mark the affected flag invalid while preserving the deterministic fallback bit value. This metadata is for later flag-consumer diagnostics; default source-run output and Simulator Messages remain unchanged.
- Milestone 50B adds consumer-side diagnostics for using architecturally undefined modeled flags. The current implementation checks already-supported `CF` consumers (`adc`, `sbb`, and `cmc`) when the source-run/API policy is set to `warn` or `error`. Warning mode emits `undefined-flag-use` and continues with the deterministic fallback flag bit. Error mode emits `undefined-flag-use` and stops before the consumer uses the invalid flag. Milestone 53C makes omitted/default browser source-run behavior use warning mode, while explicit off preserves prior silent consumer behavior.


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
