# Online MASM32 Educational Simulator - Full Implementation Specification

## 1. Project Goal

Build a static, browser-based educational simulator for MASM32-style assembly programs. The project runs entirely on the client side and uses a C99 virtual machine compiled to WebAssembly with Emscripten.

The simulator is intended for learning, experimentation, debugging, and sharing small MASM32/Irvine32-style console programs.

The core product is not a native MASM compiler and not a full Windows/x86 emulator. It is a MASM-like source parser plus an internal virtual machine that models registers, flags, memory, stack behavior, control flow, selected Irvine32 routines, and resource limits.

A user should be able to:

- Type or paste MASM32-style source code in a rich browser editor with line numbers, indentation support, and later MASM syntax highlighting.
- Run the program in the browser.
- Interact with console input routines such as `ReadString`, `ReadInt`, and `ReadChar`.
- View program output separately from simulator diagnostics.
- Debug step-by-step.
- Inspect registers, flags, memory changes, stack usage, and last-step deltas.
- Share the project through an encoded URL.
- Configure memory and execution limits safely.

## 2. High-Level Product Definition

The simulator should be described as:

> A MASM32 educational simulator with Irvine32-style console support.

It should not be advertised as:

- A full MASM replacement.
- A full x86 emulator.
- A Windows emulator.
- A PE loader/linker.
- A real `ml.exe` or `ml64.exe` implementation.

## 3. Non-Goals

The following are explicit non-goals for the first complete version:

- No native x86 binary execution.
- No PE loading or real object-file linking.
- No Windows API execution.
- No GUI API simulation.
- No DLL imports.
- No threads.
- No cycle-accurate CPU simulation.
- No hardware buses, caches, paging, privilege rings, interrupts, or OS-level behavior.
- No full MASM macro system in the initial version.
- No full x64 MASM compatibility.
- No direct browser or host filesystem access from simulated programs.
- No network access from simulated programs.
- No arbitrary JavaScript execution from simulated programs.

## 4. Execution Modes

### 4.1 MASM32 Educational Mode

Default mode.

Features:

- MASM32-style syntax subset.
- 32-bit general-purpose registers.
- Flat memory model.
- Simulated `.code`, `.data`, heap, and stack regions.
- Irvine32-style console routines.
- Source-level debugging.

This mode targets beginner and intermediate MASM32/Irvine32 console programs.

### 4.2 Extended 32-bit Mode

Optional advanced mode.

This mode extends the MASM32 educational model with selected 64-bit features:

- 64-bit general-purpose registers.
- QWORD data and arithmetic.
- Selected 64-bit computations and instructions.
- 64-bit register aliases.

This is not full x64 MASM or `ml64` behavior. It should be labeled clearly as "Extended 32-bit Mode" to avoid implying real x64 Windows ABI support.

## 5. Architecture Overview

Recommended architecture:

```text
Browser main thread
  - CodeMirror 6 source editor UI
  - console UI
  - debugger UI
  - settings UI
  - URL import/export

Web Worker
  - WebAssembly module
  - parser/assembler front-end
  - IR generation
  - VM execution loop
  - Irvine32 runtime simulation
  - resource-limit enforcement
```

The main thread should never run long VM work directly. All parsing, loading, and execution should happen inside a Web Worker.

The Web Worker may be terminated by the UI if the simulator becomes unresponsive.

### 5.1 Source Editor Component

The browser source editor should eventually use **CodeMirror 6** rather than a raw `<textarea>`. CodeMirror 6 is the selected editor component for the polished editor experience because it is permissively licensed, modular, and designed for custom language extensions.

Editor integration goals:

- Line-number gutter for mapping diagnostics to source lines.
- Future breakpoint gutter using the same editor gutter system.
- Current-instruction line highlighting for debugging.
- Parser/VM diagnostic markers and clickable diagnostics.
- MASM syntax highlighting for directives, instructions, registers, data types, labels, comments, strings, numeric literals, and supported operators.
- Basic indentation support, including preserving the previous line's indentation and handling Tab / Shift+Tab predictably.
- Local-only dark/light editor themes coordinated with the site theme.
- Optional later autocomplete for instructions, registers, directives, data symbols, labels, and Irvine32 routines.

The CodeMirror integration must remain a UI layer. It must not become the source of truth for MASM semantics. Semantic validation, symbol resolution, execution, runtime errors, and authoritative diagnostics must continue to come from the C99 parser, assembler front-end, and VM.

The worker protocol should continue to accept and return plain source strings and structured diagnostics. Replacing the editor implementation must not require changes to the core C99 VM/parser APIs.

## 6. Implementation Language and Toolchain

The simulator core must be implemented in **C99**.

Required language policy:

- Use C source and header files (`.c` and `.h`) for the VM, parser, executor, memory model, Irvine32 runtime, and Wasm-facing API.
- Compile the native test build and Emscripten build as C99.
- Do not require C++ for the core implementation.
- Do not add C++ compatibility scaffolding such as `extern "C"` unless the project explicitly changes language policy later.
- Avoid C++ source files, C++ standard library dependencies, templates, classes, exceptions, and RTTI.
- Browser-side code may remain JavaScript or TypeScript.
- Public and module-level C APIs must continue to use Doxygen-style `///` comments, and each source/header file must keep a file-level block comment.

Recommended compiler posture:

```text
C standard: C99
Warnings: enabled and treated strictly where practical
Core ownership: explicit structs and functions
Allocation: deterministic and bounded where practical
Error handling: explicit status codes and structured diagnostics
```

## 7. Source-to-Execution Pipeline

Pipeline:

```text
Source files
  -> preprocess limited includes/directives
  -> parse supported MASM subset
  -> build symbols and data layout
  -> lower source to internal IR
  -> create simulated memory regions
  -> execute IR in VM
  -> stream console output and simulator diagnostics
```

The first version should execute internal IR rather than native x86 machine code.

Each IR instruction must preserve:

- Operation code.
- Operands.
- Source file.
- Source line.
- Original source text.
- Instruction address or VM instruction index.

This enables high-quality debugging and diagnostics.

## 8. Supported MASM Subset, Version 1

The numbered subsections in this section, such as `8.1` and `8.5`, are specification sections only. They are not implementation phase numbers. The incremental implementation guide owns phase numbering.

### 8.1 Directives

Initial supported directives:

- `.386`
- `.model`
- `.stack`
- `.data`
- `.code`
- `PROC`
- `ENDP`
- `END`
- `INCLUDE`, initially for built-in virtual includes only

Unsupported directives should produce explicit unsupported-feature diagnostics rather than generic syntax errors.

### 8.2 Data Declarations

Supported data declarations:

- `BYTE`
- `WORD`
- `DWORD`
- `QWORD`
- `DB`
- `DW`
- `DD`
- `DQ`
- comma-separated initializers
- `DUP`, initially flat/non-nested
- string literals for byte-oriented data
- single-character literals such as `'A'`
- packed multi-character literals such as `'AB'` and `'ABCD'`, with width validation
- `?` uninitialized values, represented deterministically by the simulator

Examples:

```asm
.data
msg BYTE "Hello", 0
ch  BYTE 'A'
pair WORD 'AB'
tag DWORD 'ABCD'
var DWORD 10
arr BYTE 1, 2, 3, 4
buf BYTE 64 DUP(0)
qval QWORD 12345678h
neg DWORD -1
```

Notes:

- `?` reserves storage. The initial deterministic simulator behavior may zero-fill the bytes while retaining metadata that the declaration was originally uninitialized.
- Nested `DUP`, `.data?`, and complex initializer expressions are later compatibility features.

### 8.3 Numeric Literals

Supported numeric literal forms:

- decimal: `42`
- MASM-style hexadecimal with `h` suffix: `2Ah`, `0FFh`
- C-style hexadecimal with `0x` prefix: `0x2A`
- optional later MASM/debugger-style radix prefixes: `0n42`, `0y1010`, `0t52`
- optional later MASM-style binary/octal suffixes: `1010b`, `52o`, `52q`
- negative decimal: `-42`
- negative hexadecimal: `-2Ah`, `-0x2A`

Negative literals are accepted only where a numeric literal is already valid. They must be validated against the destination width and encoded as two's-complement values when they fit.

Examples:

```asm
mov al, -1          ; AL receives FFh
mov ax, -2          ; AX receives FFFEh
mov eax, -3         ; EAX receives FFFFFFFDh
.data
b BYTE -1
w WORD -2
d DWORD -3
q QWORD -4
```

Required range behavior:

```text
8-bit destination:   unsigned 0..255, signed negative -128..-1
16-bit destination:  unsigned 0..65535, signed negative -32768..-1
32-bit destination:  unsigned 0..4294967295, signed negative -2147483648..-1
64-bit data layout:  unsigned and negative QWORD initializers encoded as 64-bit data bytes
```

Out-of-range literals should produce structured assembly diagnostics instead of silent truncation.

Unary plus, parenthesized expressions, arithmetic expressions, binary/octal literals, radix-changing directives such as `.RADIX`, and symbolic expressions such as `OFFSET label - 4` are later expression-parser features.


### 8.3.1 Character and Packed Character Literals

Supported quoted character literal behavior:

- Single-character literals such as `'A'` are valid anywhere a byte-compatible numeric literal is valid.
- Multi-character literals such as `'AB'`, `'ABC'`, and `'ABCD'` are valid where the destination width can hold the decoded byte count.
- In instruction/immediate contexts, quoted character literals are converted to unsigned packed integer constants after decoding.
- Packing uses little-endian integer layout: the first decoded character becomes the least significant byte.
- Therefore `'A'` is `41h`, `'AB'` is `4241h`, and `'ABCD'` is `44434241h`.
- Width validation is required. For example, `mov al, 'AB'` must be rejected because two decoded bytes do not fit an 8-bit destination.
- `BYTE` / `DB` declarations may continue to treat quoted strings as byte sequences, so `msg BYTE 'AB', 0` emits `41h, 42h, 00h`.
- `WORD` / `DW`, `DWORD` / `DD`, and `QWORD` / `DQ` may treat quoted literals as packed scalar initializers when the decoded byte count fits the element width.

Examples:

```asm
mov al, 'A'       ; AL receives 41h
mov ax, 'AB'      ; AX receives 4241h
mov eax, 'ABCD'   ; EAX receives 44434241h
.data
ch   BYTE 'A'
pair WORD 'AB'
tag  DWORD 'ABCD'
```

Initial limitations:

- Empty character literals are rejected.
- Character literals larger than 8 decoded bytes are rejected until wider data/expression support exists.
- Escape handling should be explicit and tested. Unsupported escape forms must produce structured diagnostics rather than being silently misdecoded.


### 8.4 Operators and Type Overrides

Initial supported operators:

- `OFFSET`, initially `OFFSET symbol`
- `SIZEOF`
- `LENGTHOF`
- `TYPE`

Memory type overrides should be supported in staged form:

- `BYTE PTR`
- `WORD PTR`
- `DWORD PTR`
- `QWORD PTR`, for data layout and Extended 32-bit Mode where applicable

Examples:

```asm
mov edx, OFFSET msg
mov ecx, LENGTHOF arr
mov ebx, TYPE arr
mov eax, SIZEOF arr
mov BYTE PTR nums[3], 100
mov DWORD PTR [esi], 12345678h
```

`SIZEOF`, `LENGTHOF`, and `TYPE` are core textbook MASM operators and should be implemented shortly after data symbols and indexed memory operands.

### 8.5 Memory Operands and Addressing Forms

Memory operands should be implemented in stages so textbook MASM array code works before the simulator attempts full x86 addressing complexity.

Stage A - direct symbols:

```asm
mov var, 100
mov eax, var
mov edx, OFFSET msg
```

Stage B - symbol-relative and constant-indexed memory operands:

```asm
mov nums[8], 100
mov eax, nums[8]
mov BYTE PTR nums[3], 100
mov DWORD PTR [nums + 8], 100
```

For MASM-style source syntax in this simulator, bracketed array offsets are byte offsets. For example, `nums DWORD 10 DUP(0)` followed by `nums[8]` addresses byte offset `8`, which is DWORD element index `2`.

Stage C - register-indirect and simple displacement forms:

```asm
mov eax, [esi]
mov eax, [esi + 4]
mov eax, [esi - 4]
mov [edi], al
mov array[esi], al
```

Stage D - later scaled-index forms:

```asm
mov eax, [base + index * scale + displacement]
mov eax, array[esi * 4]
```

Stage D is a later compatibility feature and should not block textbook examples that use constant byte offsets.

### 8.6 Instructions

Initial instruction subset:

- `mov`
- `add`
- `sub`
- `inc`
- `dec`
- `cmp`
- `jmp`
- `je`, `jz`
- `jne`, `jnz`
- `jl`, `jnge`
- `jle`, `jng`
- `jg`, `jnle`
- `jge`, `jnl`
- `ja`, `jnbe`
- `jae`, `jnb`
- `jb`, `jnae`
- `jbe`, `jna`
- `loop`
- `push`
- `pop`
- `call`
- `ret`
- `and`
- `or`
- `xor`
- `not`
- `shl`
- `shr`
- `sar`
- `mul`
- `imul`
- `div`
- `idiv`
- `lea`

Additional instructions can be added incrementally once the VM, debugger, and tests are stable.

### 8.7 Unsupported Initially

Initially unsupported:

- Full MASM macro language.
- Full conditional assembly.
- `.IF`, `.ELSE`, `.ENDIF`, `.WHILE`, `.REPEAT` high-level MASM constructs.
- `INVOKE`, `PROTO`, and full calling-convention modeling.
- Full expression parsing, including arbitrary arithmetic expressions and parenthesized expressions.
- Nested `DUP` initializers.
- `.data?`, unless added as a later data-layout compatibility feature.
- Full scaled-index addressing, until the staged memory-operand milestones reach it.
- FPU instructions.
- SSE/AVX instructions.
- String instructions such as `movsb`, `stosb`, `lodsb`, unless added later.
- Segment registers and segment override behavior.
- Interrupts.
- Windows API calls.

### 8.8 MASM Compatibility Coverage Notes

The current target is **educational MASM32/Irvine32 compatibility**, not full MASM. Microsoft documents MASM as including many directive families and operators beyond the initial subset, including conditional assembly, high-level conditional control-flow directives, equates, macros, procedure/prototype directives, segment directives, structure/record directives, repeat blocks, and simplified segment directives. These should be treated as staged roadmap items rather than implicit v1 behavior.

Important textbook/compatibility areas to track explicitly:

- Equates and constants: `=`, `EQU`, `TEXTEQU`, `LABEL`.
- Additional data types: `SBYTE`, `SWORD`, `SDWORD`, `REAL4`, `REAL8`, `REAL10`, `TBYTE`, and possibly `FWORD`.
- `.CONST` and `.DATA?`, with deterministic simulator behavior for uninitialized storage.
- Structure support: `STRUCT`, `UNION`, `RECORD`, field access, `TYPEDEF`, and structure initializers.
- Procedure metadata: `PROTO`, `INVOKE`, `LOCAL`, parameters, and calling-convention modeling.
- High-level MASM flow: `.IF`, `.ELSE`, `.ELSEIF`, `.ENDIF`, `.WHILE`, `.REPEAT`, `.UNTIL`, `.BREAK`, `.CONTINUE`.
- Conditional assembly: `IFDEF`, `IFNDEF`, `IFE`, `IFB`, `IFNB`, `ELSE`, `ENDIF`, and related compile-time directives.
- Macro system: `MACRO`, `ENDM`, macro parameters, `LOCAL`, `EXITM`, `PURGE`, repeat/for blocks, expansion limits, and recursion protection.
- Include/library declarations: broader `INCLUDE`, `INCLUDELIB`, `EXTERN`, `EXTERNDEF`, `PUBLIC`, and `COMM` handling.
- Expression parser: `+`, `-`, `*`, `/`, `MOD`, `SHL`, `SHR`, `AND`, `OR`, `XOR`, `NOT`, relational operators, parentheses, `HIGH`, `LOW`, `HIGHWORD`, `LOWWORD`, `SHORT`, `THIS`, and segment-related operators where applicable.
- Instruction prefixes and string instructions: `REP`, `REPE`, `REPNE`, `LOCK`, `movsb`, `movsd`, `stosb`, `stosd`, `lodsb`, `cmpsb`, and direction-flag behavior.

These features should not be silently accepted before they are implemented. Unsupported forms should produce explicit `unsupported-feature` diagnostics with source location.

## 9. Register Model

### 9.1 32-bit Mode Canonical Registers

Canonical stored registers:

- `EAX`
- `EBX`
- `ECX`
- `EDX`
- `ESI`
- `EDI`
- `EBP`
- `ESP`
- `EIP`
- `EFLAGS`

Aliases should be derived dynamically, not stored independently.

Displayed aliases:

- `EAX`, `AX`, `AH`, `AL`
- `EBX`, `BX`, `BH`, `BL`
- `ECX`, `CX`, `CH`, `CL`
- `EDX`, `DX`, `DH`, `DL`
- `ESI`, `SI`
- `EDI`, `DI`
- `EBP`, `BP`
- `ESP`, `SP`
- `EIP`

`IP` may be optionally displayed in an advanced alias mode.

### 9.2 Extended 32-bit Canonical Registers

Canonical stored registers:

- `RAX`
- `RBX`
- `RCX`
- `RDX`
- `RSI`
- `RDI`
- `RBP`
- `RSP`
- `R8` through `R15`
- `RIP`
- `RFLAGS`

Displayed aliases:

- `RAX`, `EAX`, `AX`, `AH`, `AL`
- `RBX`, `EBX`, `BX`, `BH`, `BL`
- `RCX`, `ECX`, `CX`, `CH`, `CL`
- `RDX`, `EDX`, `DX`, `DH`, `DL`
- `RSI`, `ESI`, `SI`
- `RDI`, `EDI`, `DI`
- `RBP`, `EBP`, `BP`
- `RSP`, `ESP`, `SP`
- `R8`, `R8D`, `R8W`, `R8B`
- `R9`, `R9D`, `R9W`, `R9B`
- `R10`, `R10D`, `R10W`, `R10B`
- `R11`, `R11D`, `R11W`, `R11B`
- `R12`, `R12D`, `R12W`, `R12B`
- `R13`, `R13D`, `R13W`, `R13B`
- `R14`, `R14D`, `R14W`, `R14B`
- `R15`, `R15D`, `R15W`, `R15B`
- `RIP`

In Extended 32-bit Mode, writing to a 32-bit subregister such as `EAX` should zero-extend into the corresponding 64-bit register unless the project explicitly chooses a simplified non-x64 behavior. The default should be real x86-64-style zero-extension.

### 9.3 Register Display

The register table should group aliases:

```text
RAX  0000000000000014h / 20
  EAX 00000014h / 20
    AX 0014h / 20
      AH 00h / 0
      AL 14h / 20
```

The UI should support collapsing groups to avoid clutter.

Default number formats:

- Hexadecimal.
- Unsigned decimal.

Optional formats:

- Signed decimal.
- Binary.
- Character view for bytes.

## 10. Flag Model

Initial flags:

- `CF` carry flag
- `ZF` zero flag
- `SF` sign flag
- `OF` overflow flag

Later flags:

- `PF` parity flag
- `AF` auxiliary carry flag
- `DF` direction flag, especially if string instructions are added

Flag behavior must be tested carefully for arithmetic, comparisons, signed jumps, and unsigned jumps.

## 11. Memory Model

### 11.1 Regions

The VM should use deterministic simulated memory regions.

Example default layout:

```text
.code   base 0x00400000
.data   base 0x00500000
.heap   base 0x00600000
.stack  top  0x00800000, grows downward
```

Actual sizes are configurable through project settings and local safety limits.

### 11.2 Permissions

Recommended permissions:

```text
.code
  read: yes
  write: no
  execute: yes

.data
  read: yes
  write: yes
  execute: no

.heap
  read: yes
  write: yes
  execute: no

.stack
  read: yes
  write: yes
  execute: no
```

Writes to `.code` should fail unless a future advanced option enables self-modifying code.

Execution from `.data`, heap, or stack should fail by default.

### 11.3 Bounds Checking

Every memory access must go through checked helpers.

Examples:

```c
bool vm_read_u8(Vm *vm, uint32_t address, uint8_t *out_value);
bool vm_write_u32(Vm *vm, uint32_t address, uint32_t value);
```

Errors should include:

- Access type.
- Address.
- Size.
- Source line.
- Instruction text.
- Closest known symbol, if any.

### 11.4 Lazy Allocation

The simulator should support lazy page allocation, especially for large virtual memory settings.

Separate concepts:

- Virtual memory limit: the simulated address range available to the MASM program.
- Committed memory limit: real browser/Wasm memory actually allocated for touched pages.

This enables large virtual regions without immediately allocating huge browser memory.

### 11.5 Unaligned Access

Unaligned reads and writes should be simulated correctly for normal integer operations.

Example:

```asm
mov esi, OFFSET arr
inc esi
mov eax, DWORD PTR [esi]
```

The VM should perform the read but emit a simulator warning:

```text
Warning: unaligned DWORD read at 00500001h.
```

The warning should appear in Simulator Messages, not Program Console.

## 12. Stack Model

The stack is a memory region that grows downward.

Default stack size:

```text
64 KiB
```

`ESP` or `RSP` should be initialized to the top of the stack region.

Stack overflow occurs when stack movement or memory access crosses below the configured stack region.

The debugger should display:

- Current stack pointer.
- Current stack usage.
- Peak stack usage.
- Stack remaining.
- Optional warning when peak usage exceeds a threshold, such as 80% or 90%.

A separate call-depth watchdog may be provided for clearer recursion diagnostics, but the stack size remains the hard correctness boundary.

## 13. Irvine32 Runtime Support

Irvine32 routines should be simulated as VM intrinsics, not as real library code.

When the VM sees:

```asm
call WriteString
```

it should intercept the call and execute the corresponding simulated routine.

### 13.1 Initial Supported Routines

Recommended v1 routines:

- `WriteString`
- `WriteChar`
- `WriteInt`
- `WriteDec`
- `WriteHex`
- `Crlf`
- `ReadString`
- `ReadInt`
- `ReadChar`
- `DumpRegs`
- `DumpMem`
- `RandomRange`

### 13.2 Routine Contracts

Each routine must have a documented contract.

Example: `WriteString`

```text
Input:
  EDX = address of null-terminated string

Effect:
  Reads bytes from simulated memory until a null terminator.
  Appends the decoded string to Program Console.

Errors:
  Invalid EDX pointer.
  Missing null terminator before readable memory ends.
  Output limit exceeded.
```

Example: `ReadString`

```text
Input:
  EDX = destination buffer
  ECX = maximum character count

Effect:
  VM enters WAITING_FOR_INPUT state.
  UI enables input box.
  Submitted input is written into simulated memory.
  EAX receives character count.

Errors:
  Invalid destination pointer.
  ECX too large for writable memory.
  Input cancelled by user.
```

## 14. Console and Input Model

### 14.1 Separate Output Streams

There must be two separate panels:

```text
Program Console
  User program output and interactive input.

Simulator Messages
  Assembly errors, warnings, runtime errors, resource-limit stops, unsupported-feature messages, and VM diagnostics.
```

The simulated program must not be able to print into Simulator Messages.

### 14.2 Input Handling

When an input routine is reached:

```text
RUNNING -> WAITING_FOR_INPUT
```

During input wait:

- Active execution timer pauses.
- Instruction counter stops increasing.
- Stop button remains available.
- Program Console input field becomes active.

When input is submitted:

```text
WAITING_FOR_INPUT -> RUNNING
```

Input cancellation should stop the program with a structured reason:

```text
Execution stopped: input cancelled by user.
```

### 14.3 Output Limits

Output limits should support both bytes and lines.

Default limits:

```text
Max output bytes: 1 MiB
Max output lines: 10,000
Default action: stop program
```

Supported actions when exceeded:

- Stop program.
- Keep latest output and discard oldest output.
- Pause and ask the user what to do.

A simple clear-and-continue action is not preferred because it hides the issue and can confuse users.

## 15. VM Execution State

VM states:

- `IDLE`
- `ASSEMBLING`
- `READY`
- `RUNNING`
- `WAITING_FOR_INPUT`
- `PAUSED`
- `BREAKPOINT_HIT`
- `STOPPED`
- `HALTED`
- `CRASHED`

Transitions should be explicit and testable.

## 16. Browser Execution and Worker Model

The VM should run inside a Web Worker.

Main thread responsibilities:

- Editor rendering.
- UI controls.
- Program Console display.
- Simulator Messages display.
- Debugger display.
- URL import/export.

Worker responsibilities:

- Loading WebAssembly.
- Parsing and assembling source.
- Running VM chunks.
- Enforcing instruction/time/output/memory limits.
- Sending structured events to the UI.

### 16.1 Stop Button

The Stop button should support:

1. Soft stop: send a stop request to the worker/VM.
2. Hard stop: terminate and recreate the worker if the worker does not respond.

The UI should always allow stopping, including while waiting for input.

## 17. Execution Limits

### 17.1 Instruction Limit

Default:

```text
Enabled: yes
Limit: 1,000,000 executed VM instructions
```

This counts executed VM instructions, not source lines.

When exceeded:

```text
Execution stopped: instruction limit exceeded.
Limit: 1,000,000 instructions
Stopped at line: <line>
Instruction: <instruction>
```

### 17.2 Active Time Limit

Default:

```text
Enabled: yes
Limit: 10 seconds active VM execution time
```

The timer pauses while the VM is waiting for user input.

### 17.3 Optional Input Wait Timeout

Default:

```text
Disabled
```

Useful for automated testing or classroom scenarios.

When exceeded:

```text
Execution stopped: input wait timeout.
```

### 17.4 Chunked Execution

The worker should run VM instructions in chunks rather than one unbroken loop.

Example:

```text
Run 10,000 instructions
Flush output/messages
Check stop request
Check time limit
Yield to worker event loop
Repeat
```

This keeps Stop responsive and allows periodic UI updates.

## 18. Memory Configuration and Safety Tiers

### 18.1 User-Configurable Memory Controls

Expose controls for:

- Stack size.
- `.data` limit.
- Heap size.

Use a dropdown plus exact byte textbox.

The textbox is the source of truth. The dropdown only populates common values and changes to `Custom` if the textbox value differs.

Suggested normal presets:

- 4 KiB
- 16 KiB
- 64 KiB
- 256 KiB
- 1 MiB
- 4 MiB
- 16 MiB
- 64 MiB
- 128 MiB
- Custom

Suggested extended presets:

- 256 MiB
- 512 MiB
- 1 GiB

### 18.2 Safety Tiers

Recommended tiers:

```text
Normal mode:
  max total virtual memory: 256 MiB

Extended memory mode:
  max total virtual memory: 1 GiB

Super-extended memory mode:
  max total virtual memory: up to 4 GiB
  local-only
  off by default
  never saved into shared project state
  requires confirmation when enabled
  requires confirmation before running oversized projects
```

### 18.3 Super-Extended Memory Confirmation

When enabling super-extended memory:

```text
Super-extended memory mode allows projects to request very large simulated memory regions.

This may slow down, freeze, or crash this browser tab if the program touches too much memory. This setting is local to this browser session and is not saved into shared project links.

Enable super-extended memory mode?
```

Require a checkbox:

```text
[ ] I understand that this may freeze or crash the tab.
```

When running a project that exceeds normal/extended limits:

```text
This project requests <N> of simulated memory.
Current local mode permits this, but running the program may allocate large amounts of real browser memory depending on what the program does.

Run anyway?
```

## 19. Debugger Model

### 19.1 Controls

Required controls:

- Run.
- Stop.
- Reset.
- Step Into.
- Step Over.
- Continue.
- Breakpoints.

Definitions:

```text
Run:
  Execute until halt, crash, breakpoint, input wait, stop request, or resource limit.

Step Into:
  Execute exactly one VM instruction.

Step Over:
  If current instruction is CALL, execute until the return address is reached or until a stop condition occurs.
  If not CALL, behaves like Step Into.

Continue:
  Resume from pause or breakpoint.

Stop:
  Request execution stop; hard-terminate worker if unresponsive.

Reset:
  Rebuild VM from source and clear runtime state.
```

### 19.2 Debugger Panels

Required panels:

- Source editor with current line highlight.
- Register table.
- Flags table.
- Program Console.
- Simulator Messages.
- Last Step Changes.
- Memory Changes.
- Stack summary.

Optional advanced panels:

- Raw memory viewer.
- Watch variables.
- Call stack.
- Symbol table.
- Instruction trace.

### 19.3 Last-Step Delta

Each step should produce a structured delta showing:

- Instruction executed.
- Registers changed.
- Register aliases changed.
- Flags changed.
- Memory changed.
- Program output produced.
- Simulator warnings produced.
- Instruction count.
- Active execution time.

Example:

```text
Last instruction:
  mov eax, 20

Register changes:
  EAX: 00000000h / 0 -> 00000014h / 20
  AX:  0000h / 0     -> 0014h / 20
  AL:  00h / 0       -> 14h / 20
```

Unchanged aliases should be hidden by default.

### 19.4 Step-Over Delta

For Step Over, the delta is the aggregate difference from before the call to after it returns.

Example:

```text
Step-over result:
  call SomeProc

Instructions executed inside call: 17

Register changes:
  EAX: 00000000h / 0 -> 0000002Ah / 42
  ECX: 00000005h / 5 -> 00000000h / 0

Memory changes:
  result DWORD: 00000000h / 0 -> 0000002Ah / 42
```

## 20. Memory Change Display

### 20.1 Default Display

By default, show only changed memory for the last step.

Example:

```asm
.data
var BYTE 0
.code
mov var, 100
```

Display:

```text
Memory changes:
  var BYTE
    address: 00500000h
    byte offset: +0
    00h / 0 -> 64h / 100
```

### 20.2 Arrays and Byte Offsets

MASM-style array offsets are byte offsets. Parsing and executing indexed operands such as `nums[8]` belongs to the staged memory-operand implementation; this section defines how those writes should be displayed once they are available.

Example:

```asm
.data
nums DWORD 10 DUP(0)
.code
mov nums[8], 100
```

Display:

```text
Memory changes:
  nums + 8 DWORD
    address: 00500008h
    byte offset: +8
    element index: 2
    00000000h / 0 -> 00000064h / 100
```

If the address is unaligned inside an element:

```text
nums + 9
  element: nums[2] + 1 byte
```

### 20.3 Logical and Byte-Level Views

Internally record raw byte changes. The UI may group them by logical write.

For a DWORD write:

```text
DWORD write at 00500001h
old: 05040302h
new: 12345678h
```

Expanded byte view:

```text
arr + 1 BYTE: 02h / 2 -> 78h / 120
arr + 2 BYTE: 03h / 3 -> 56h / 86
arr + 3 BYTE: 04h / 4 -> 34h / 52
arr + 4 BYTE: 05h / 5 -> 12h / 18
```

### 20.4 Strings

For byte arrays that look like strings, show a text interpretation.

Example:

```text
buffer BYTE[6]
  hex: 68 65 6C 6C 6F 00
  text: "hello"
```

Large string changes should be collapsed by default.

## 21. Error, Warning, and Diagnostic Model

All errors should be structured internally.

Example:

```json
{
  "kind": "runtime-error",
  "code": "invalid-memory-read",
  "message": "Invalid memory read at 0x0050FFFF.",
  "file": "main.asm",
  "line": 18,
  "instruction": "mov eax, [ebx]",
  "address": "0x0050FFFF",
  "registers": {
    "eax": "0x00000000",
    "ebx": "0x0050FFFF",
    "esp": "0x007FFFE8"
  }
}
```

Categories:

- `assembly-error`
- `link-load-error`
- `runtime-error`
- `resource-limit-error`
- `user-stopped`
- `unsupported-feature`
- `simulator-warning`
- `internal-simulator-error`

Unsupported features should be explicit:

```text
Unsupported feature: Windows API calls are not available in this simulator.
Line 14: invoke MessageBox, NULL, ADDR msg, ADDR title, MB_OK
```

## 22. Save and Share URL Format

Use compressed encoded project state in the URL fragment.

Recommended shape:

```text
https://example.com/#v=1&name=hello-world-v1&state=<compressed-base64url-json>
```

The readable `name` parameter is for humans. The trusted project name comes from the decoded state.

Recommended state shape:

```json
{
  "schema": 1,
  "name": "hello-world-v1",
  "files": {
    "main.asm": "..."
  },
  "settings": {
    "mode": "masm32-educational",
    "entryPoint": "main",
    "memory": {
      "stackSize": 65536,
      "dataSize": 1048576,
      "heapSize": 1048576
    },
    "execution": {
      "instructionLimitEnabled": true,
      "instructionLimit": 1000000,
      "timeLimitEnabled": true,
      "timeLimitMs": 10000
    },
    "console": {
      "outputLimitEnabled": true,
      "maxOutputBytes": 1048576,
      "maxOutputLines": 10000,
      "onLimitExceeded": "stop"
    },
    "debugger": {
      "breakpoints": []
    }
  }
}
```

Saved in shared project state:

- Project name.
- Source files.
- Execution mode.
- Entry point.
- Memory sizes requested by the project.
- Execution limits requested by the project.
- Console limits requested by the project.
- Breakpoints.

Not saved:

- Super-extended memory permission.
- Developer overrides.
- Local committed-memory cap.
- Current runtime state.
- Program output.
- Simulator messages.
- Temporary input text.
- UI theme and CodeMirror theme selection.
- Editor local preferences such as folded panels, font size, and local-only editor options.
- Scroll positions.

## 23. Project File Model

Version 1 may be single-file, but the internal state should support multiple files from the start.

Project:

```text
name
files
active file
settings
breakpoints
```

Built-in virtual includes:

- `Irvine32.inc`, simplified to declarations recognized by the simulator.

Future virtual project files:

- `main.asm`
- `.inc` files
- virtual input/output text files

## 24. Security and Safety Model

The simulator runs untrusted user code inside an emulated VM, not directly in the browser environment.

Safety rules:

- Simulated code cannot access real files.
- Simulated code cannot access network APIs.
- Simulated code cannot call browser APIs.
- Simulated code cannot execute JavaScript.
- All memory access is VM-checked.
- All execution occurs in a Web Worker.
- Worker can be terminated by the main UI.
- Resource limits are enforced by the VM and worker.
- Large memory modes require local consent.

## 25. Documentation Requirements

All C and TypeScript/JavaScript code should be documented consistently. The core implementation language is C99; examples in this section use C and should not be converted to C++.

### 25.1 File Header Documentation

Each source file should start with a block comment:

```c
/*
 * @file vm_memory.c
 * @brief Checked memory access and lazy page allocation for the MASM simulator VM.
 *
 * This file owns the simulated address-space model. All reads and writes from
 * VM instructions must pass through this module so bounds checks, permissions,
 * unaligned-access warnings, and memory-change recording remain centralized.
 */
```

### 25.2 Doxygen-Style Symbol Documentation

Use triple-slash documentation for functions, structs, enums, and public module APIs.

Example:

```c
/// Describes the result of a checked VM memory access.
typedef enum VmMemoryResult {
    VM_MEMORY_OK,
    VM_MEMORY_INVALID_ADDRESS,
    VM_MEMORY_PERMISSION_DENIED,
    VM_MEMORY_COMMITTED_LIMIT_EXCEEDED
} VmMemoryResult;

/// Reads a 32-bit little-endian value from simulated memory.
///
/// @param vm The VM instance that owns the simulated memory regions.
/// @param address The simulated address to read from.
/// @param out_value Receives the decoded 32-bit value on success.
/// @return VM_MEMORY_OK on success, or an error code describing the failure.
VmMemoryResult vm_memory_read_u32(Vm *vm, uint32_t address, uint32_t *out_value);
```

### 25.3 Documentation Policy

Every new public function, struct, enum, and module-level API must include Doxygen documentation.

Internal helpers should be documented when their behavior is non-obvious.

Comments should explain intent and invariants, not restate simple code.

## 26. Testing Requirements

Tests are required from the beginning.

Minimum groups:

- Parser tests.
- Data declaration tests.
- Numeric literal tests, including negative decimal and hexadecimal literals.
- Character literal tests once character literals are enabled, including single-character literals, packed multi-character literals, width-overflow cases, data declaration cases, and instruction immediate cases.
- Register alias tests.
- Instruction tests.
- Flag behavior tests.
- Direct symbol memory operand tests.
- Indexed and symbol-relative memory operand tests.
- `PTR`, `OFFSET`, `TYPE`, `LENGTHOF`, and `SIZEOF` tests.
- Signed and unsigned jump tests.
- Stack tests.
- Memory bounds tests.
- Lazy allocation tests.
- Irvine32 routine tests.
- Input waiting/resume tests.
- Output limit tests.
- Execution limit tests.
- URL encode/decode tests.
- Debugger step/delta tests.

Flag tests are especially important.

Example:

```asm
mov eax, 0FFFFFFFFh
add eax, 1
```

Expected:

```text
EAX = 00000000h
ZF = 1
CF = 1
OF = 0
```

## 27. Implementation Phasing Guidance

The incremental implementation guide intentionally splits large simulator/editor features into small numbered phases. This specification defines the final behavior; the guide defines the order and granularity of implementation.

Important split areas:

- Memory operand support should be implemented incrementally: constant symbol offsets, `PTR` width overrides, then register-indirect operands.
- Data operators and literals should be implemented incrementally: `TYPE`, then `LENGTHOF`, then `SIZEOF` together with single-character and packed multi-character literals.
- Control flow should be implemented incrementally: labels/`JMP`, then `CMP` and equality jumps, then signed/unsigned jumps, then `LOOP` and instruction limits.
- Stack support should be implemented incrementally: stack initialization with `PUSH`/`POP`, then `CALL`/`RET`, then call-depth diagnostics.
- Irvine32 support should be implemented incrementally: console infrastructure, basic text output, numeric output, diagnostic output/randomness, input protocol, simple input, then string input.
- Debugger support should be implemented incrementally: Step Into backend, current-state UI, last-step delta UI, execution stats, breakpoints, Continue, Step Over backend, and Step Over aggregate delta display.
- CodeMirror editor support should be implemented incrementally: source editor replacement, MASM highlighting, indentation, dark/light local preferences, diagnostics integration, and debugger/breakpoint integration.

Large multi-feature implementation passes should be avoided. Each implementation phase should remain independently testable and should preserve previous milestone behavior.

## 28. Future Roadmap

Possible future features:

- CodeMirror 6 source editor integration with line numbers, MASM highlighting, dark/light themes, and local-only editor preferences.
- CodeMirror diagnostic markers, clickable Simulator Messages, current execution-line highlighting, and breakpoint gutter integration.
- Optional CodeMirror autocomplete for instructions, registers, directives, data symbols, labels, and Irvine32 routines.
- More MASM directives, especially `.CONST`, `.DATA?`, `ALIGN`, `LABEL`, and `.RADIX`.
- MASM equates and constants: `=`, `EQU`, `TEXTEQU`.
- Additional numeric literal forms: binary/octal suffixes and radix prefixes where useful.
- Full expression parser with arithmetic, logical, relational, and parenthesized expressions.
- `.data?` support.
- Nested `DUP` initializers.
- Broader expression parsing, including `OFFSET symbol + constant`.
- Full scaled-index addressing.
- `ADDR` and later `INVOKE` / `PROTO` support.
- Calling convention support.
- High-level MASM control-flow directives.
- Macro expansion.
- Include depth and macro expansion safeguards.
- More Irvine32 routines.
- FPU support.
- SSE subset.
- Watch variables.
- Data breakpoints/watchpoints.
- Raw memory hex editor.
- Multi-file project editor.
- Virtual filesystem.
- Download/upload project archives.
- Backend snippet sharing with short URLs.
- Optional UASM/JWasm investigation.

