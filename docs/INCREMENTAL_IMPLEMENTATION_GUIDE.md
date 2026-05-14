# Online MASM32 Educational Simulator - Incremental Implementation Guide

## 1. Purpose

This guide breaks the simulator into small implementation phases suitable for incremental development with an AI coding assistant.

The goal is to avoid attempting full MASM compatibility at once. Each phase should produce a working, testable improvement.

The guide assumes the final target described in `FULL_IMPLEMENTATION_SPEC.md`.

## 2. General Development Rules

### 2.1 Build Vertically

Prefer small vertical slices over broad incomplete systems.

A good slice:

```text
Parse `mov eax, 20`
Execute it
Show EAX changed in debugger
Test it
```

A bad slice:

```text
Create 50 parser classes before anything can run
```

### 2.2 Keep the VM Deterministic

The same source, settings, and input should produce the same result.

Random behavior should be seeded.

### 2.3 Centralize Safety Checks

All memory reads and writes must go through the VM memory module.

All console output must go through the console module.

All execution must go through the VM run/step functions.

### 2.4 Preserve Source Locations

Every parsed instruction and generated IR instruction must retain:

- Source file.
- Source line.
- Source column if practical.
- Original source text.

Diagnostics and debugger output depend on this.

### 2.5 Documentation Requirement

Every source file must have a file header block comment.

Every public function, struct, enum, and module-level API must use Doxygen-style triple-slash documentation.

Example file header:

```c
/*
 * @file vm_cpu.c
 * @brief CPU register and flag model for the MASM simulator VM.
 *
 * This module owns canonical register storage and exposes helpers for reading
 * and writing full registers and aliases such as AX, AH, and AL.
 */
```

Example symbol documentation:

```c
/// Writes a value to a register or register alias.
///
/// @param cpu CPU state to mutate.
/// @param reg Register identifier, including aliases such as AL or AX.
/// @param value Value to write. The value is masked to the register width.
/// @return true if the register was valid and the write succeeded.
bool vm_cpu_write_register(VmCpu *cpu, VmRegister reg, uint64_t value);
```

## 3. Recommended Repository Structure

Suggested structure:

```text
/project-root
  /src
    /core
      vm_cpu.c
      vm_cpu.h
      vm_flags.c
      vm_flags.h
      vm_memory.c
      vm_memory.h
      vm_ir.c
      vm_ir.h
      vm_exec.c
      vm_exec.h
      vm_debug.c
      vm_debug.h
      vm_console.c
      vm_console.h
      irvine32.c
      irvine32.h
    /parser
      lexer.c
      lexer.h
      parser.c
      parser.h
      symbols.c
      symbols.h
      data_layout.c
      data_layout.h
    /wasm
      wasm_api.c
      wasm_api.h
  /web
    index.html
    src/main.ts
    src/worker.ts
    src/state.ts
    src/url_state.ts
    src/ui/
  /tests
    core/
    parser/
    programs/
  /docs
    FULL_IMPLEMENTATION_SPEC.md
    INCREMENTAL_IMPLEMENTATION_GUIDE.md
```

Actual file names may vary, but module boundaries should remain clear.

## 4. Phase 0 - Project Skeleton

### Goal

Create a minimal static web project with a WebAssembly-ready C core and a basic browser UI.

### Tasks

1. Create repository structure.
2. Add build system for C to WebAssembly using Emscripten.
3. Add basic HTML/CSS/JS or TypeScript frontend.
4. Add Web Worker that loads the Wasm module.
5. Add a simple message protocol between UI and worker.
6. Add placeholder editor, Run button, Program Console, and Simulator Messages panel.
7. Add test runner setup.
8. Add Doxygen comment conventions to initial files.

### Acceptance Criteria

- Page loads locally.
- Worker initializes.
- UI can send a `PING` message to worker.
- Worker replies with `PONG`.
- Wasm module exports at least one test function.
- Tests can be run from command line.

## 5. Phase 1 - Core CPU Register Model

### Goal

Implement canonical register storage and alias read/write behavior.

### Tasks

1. Define CPU state for MASM32 mode.
2. Add canonical registers:
   - `EAX`, `EBX`, `ECX`, `EDX`
   - `ESI`, `EDI`, `EBP`, `ESP`
   - `EIP`
   - `EFLAGS`
3. Add alias support:
   - `AX`, `AH`, `AL`
   - `BX`, `BH`, `BL`
   - `CX`, `CH`, `CL`
   - `DX`, `DH`, `DL`
   - `SI`, `DI`, `BP`, `SP`
4. Implement register read/write helpers.
5. Add unit tests for aliases.

### Acceptance Criteria

- Writing `EAX = 0x12345678` gives:
  - `AX = 0x5678`
  - `AH = 0x56`
  - `AL = 0x78`
- Writing `AL = 0xFF` updates only the low byte of `EAX`.
- Writing `AH = 0xEE` updates bits 8-15 only.
- Register helpers are documented with Doxygen comments.

## 6. Phase 2 - Flags Model

### Goal

Implement initial flags needed for arithmetic and jumps.

### Tasks

1. Define flag bits:
   - `CF`
   - `ZF`
   - `SF`
   - `OF`
2. Add helpers to set, clear, and read flags.
3. Add arithmetic helper functions for common flag updates:
   - addition
   - subtraction
   - comparison
4. Add tests for edge cases.

### Acceptance Criteria

Test cases pass for:

```asm
0xFFFFFFFF + 1
0x7FFFFFFF + 1
0 - 1
5 - 5
```

Expected flags must be verified for `CF`, `ZF`, `SF`, and `OF`.

## 7. Phase 3 - Simulated Memory Regions

### Goal

Implement checked memory access for `.code`, `.data`, heap, and stack regions.

### Tasks

1. Define memory region structure:
   - base address
   - size
   - permissions
   - backing storage
2. Add default layout.
3. Implement checked read/write helpers for:
   - 8-bit
   - 16-bit
   - 32-bit
   - 64-bit storage, even if QWORD execution is later
4. Enforce bounds and permissions.
5. Detect unaligned reads/writes and return warnings while still allowing them.
6. Add memory-change recording hooks.
7. Add tests for valid and invalid access.

### Acceptance Criteria

- Reading/writing valid `.data` memory succeeds.
- Writing `.code` fails.
- Reading invalid addresses fails with structured error.
- Unaligned DWORD read succeeds and emits a warning.
- All memory APIs have Doxygen documentation.

## 8. Phase 4 - Minimal IR and Executor

### Goal

Execute a tiny hardcoded program through internal IR.

### Tasks

1. Define IR instruction structure.
2. Define operand types:
   - immediate
   - register
   - memory address
3. Implement VM step function.
4. Implement initial instructions:
   - `mov`
   - `add`
   - `sub`
5. Add last-step delta capture:
   - changed registers
   - changed flags
   - changed memory
6. Expose one test API from Wasm that runs a hardcoded IR program.

### Acceptance Criteria

Hardcoded program:

```asm
mov eax, 20
add eax, 22
```

produces:

```text
EAX = 42
```

Debugger delta reports:

```text
EAX: 0 -> 20
EAX: 20 -> 42
```

## 9. Phase 5 - Lexer

### Goal

Tokenize a small MASM-like source file.

### Tasks

1. Implement lexer for:
   - identifiers
   - registers
   - numbers, including decimal and hex forms
   - strings
   - commas
   - brackets
   - colons
   - comments beginning with `;`
   - line endings
2. Preserve source positions.
3. Add lexer diagnostics.
4. Add tests.

### Acceptance Criteria

Lexer handles:

```asm
.data
msg BYTE "Hello", 0
.code
mov eax, 20 ; comment
```

with correct token positions.

## 10. Phase 6 - Parser for Minimal Code

### Goal

Parse a tiny `.code` program into IR.

### Tasks

1. Parse `.code` section.
2. Parse labels.
3. Parse simple instructions:
   - register/immediate
   - register/register
4. Parse `END main`.
5. Preserve line information.
6. Report unsupported syntax cleanly.

### Acceptance Criteria

Program:

```asm
.code
main PROC
    mov eax, 20
    add eax, 22
main ENDP
END main
```

runs and produces `EAX = 42`.

## 11. Phase 7 - Basic UI Execution

### Goal

Run a small parsed program from the browser UI.

### Tasks

1. Editor text area.
2. Run button.
3. Worker receives source code.
4. Worker parses and executes.
5. UI displays final register state.
6. UI displays Simulator Messages.

### Acceptance Criteria

A user can paste:

```asm
.code
main PROC
    mov eax, 20
    add eax, 22
main ENDP
END main
```

click Run, and see `EAX = 42`.

## 12. Phase 8 - Data Section and Symbols

### Goal

Support `.data` declarations and symbolic memory references.

### Tasks

1. Parse `.data` section.
2. Support:
   - `BYTE`
   - `WORD`
   - `DWORD`
   - `QWORD`
   - strings
   - `DUP`
   - `?`
3. Build symbol table.
4. Lay out `.data` memory.
5. Support `OFFSET`.
6. Support direct symbol memory writes:
   - `mov var, 100`
7. Add memory-change display by symbol.

### Acceptance Criteria

Program:

```asm
.data
var BYTE 0
.code
main PROC
    mov var, 100
main ENDP
END main
```

shows:

```text
var: 00h / 0 -> 64h / 100
```

## 13. Phase 9 - Control Flow

### Goal

Support labels, jumps, comparisons, and loops.

### Tasks

1. Resolve labels.
2. Implement:
   - `cmp`
   - `jmp`
   - `je`, `jne`
   - signed jumps: `jl`, `jle`, `jg`, `jge`
   - unsigned jumps: `ja`, `jae`, `jb`, `jbe`
   - `loop`
3. Add instruction counter.
4. Add instruction-limit enforcement.
5. Add tests for signed and unsigned branches.

### Acceptance Criteria

A loop program executes correctly.

An infinite loop stops with:

```text
Execution stopped: instruction limit exceeded.
```

The message includes source line and instruction text.

## 14. Phase 10 - Stack, CALL, and RET

### Goal

Support procedure calls and stack operations.

### Tasks

1. Initialize stack pointer.
2. Implement:
   - `push`
   - `pop`
   - `call`
   - `ret`
3. Track stack usage and peak stack usage.
4. Add stack overflow detection.
5. Add optional call-depth tracking.
6. Add tests for nested calls and recursion.

### Acceptance Criteria

A program with `call SomeProc` and `ret` works.

Infinite recursion stops with either:

- call-depth limit exceeded, or
- stack overflow.

Diagnostic must include line and procedure if known.

## 15. Phase 11 - Irvine32 Output Routines

### Goal

Support basic console output.

### Tasks

1. Implement Program Console stream.
2. Intercept calls to:
   - `WriteString`
   - `WriteChar`
   - `WriteInt`
   - `WriteDec`
   - `WriteHex`
   - `Crlf`
3. Add output byte and line limits.
4. Separate Program Console and Simulator Messages.
5. Add tests for output routines.

### Acceptance Criteria

Program:

```asm
.data
msg BYTE "Hello", 0
.code
main PROC
    mov edx, OFFSET msg
    call WriteString
    call Crlf
main ENDP
END main
```

prints `Hello` in Program Console.

Simulator warnings/errors do not appear in Program Console.

## 16. Phase 12 - Irvine32 Input Routines

### Goal

Support interactive input without blocking the UI.

### Tasks

1. Add VM state `WAITING_FOR_INPUT`.
2. Implement:
   - `ReadString`
   - `ReadInt`
   - `ReadChar`
3. Pause active execution timer while waiting for input.
4. Keep Stop available while waiting for input.
5. Resume VM after input submission.
6. Add input cancellation behavior.
7. Add tests using simulated input.

### Acceptance Criteria

A program can call `ReadString`, wait for browser input, resume, and use the resulting buffer.

Execution time limit does not count human input wait time.

## 17. Phase 13 - Debugger v1

### Goal

Add source-level stepping and current-state inspection.

### Tasks

1. Add Step Into.
2. Add Continue.
3. Add Reset.
4. Highlight current source line.
5. Display current registers.
6. Display aliases in grouped rows.
7. Display current flags.
8. Display last-step register and flag changes.
9. Display last-step memory changes.
10. Display instruction count and active execution time.

### Acceptance Criteria

For:

```asm
mov eax, 20
```

Last Step panel shows:

```text
EAX: 00000000h / 0 -> 00000014h / 20
AX:  0000h / 0     -> 0014h / 20
AL:  00h / 0       -> 14h / 20
```

Unchanged aliases are hidden by default.

## 18. Phase 14 - Breakpoints and Step Over

### Goal

Add practical debugging controls.

### Tasks

1. Add source-line breakpoints.
2. Stop execution at breakpoints.
3. Implement Step Over.
4. For Step Over, aggregate register/flag/memory changes across the call.
5. Show number of instructions executed during Step Over.

### Acceptance Criteria

A breakpoint pauses before executing the associated instruction.

Step Over on `call SomeProc` runs until return and shows all changed registers and memory.

## 19. Phase 15 - Memory Visualization Improvements

### Goal

Make memory deltas educational and symbol-aware.

### Tasks

1. Resolve memory changes to nearest symbol.
2. Show byte offsets using MASM-style byte offsets.
3. Show element index when the symbol has an element size.
4. Show unaligned access warnings.
5. Add logical write grouping and expandable byte-level view.
6. Add string interpretation for byte arrays.

### Acceptance Criteria

For:

```asm
.data
nums DWORD 10 DUP(0)
.code
mov nums[8], 100
```

Memory Changes shows:

```text
nums + 8 DWORD
  byte offset: +8
  element index: 2
  00000000h / 0 -> 00000064h / 100
```

## 20. Phase 16 - URL Save and Share

### Goal

Allow users to share projects through encoded URLs.

### Tasks

1. Define project JSON schema.
2. Implement encode:
   - JSON stringify
   - compress
   - base64url encode
   - write to URL fragment
3. Implement decode on page load.
4. Include visible `name` parameter in URL.
5. Validate schema version.
6. Handle invalid or corrupted links gracefully.
7. Add tests for encode/decode stability.

### Acceptance Criteria

A user can create a project, copy a share URL, reload/open it, and recover source plus settings.

Super-extended memory permission is not saved.

## 21. Phase 17 - Memory Settings UI

### Goal

Expose configurable stack, `.data`, and heap sizes safely.

### Tasks

1. Add dropdown presets.
2. Add exact byte textbox.
3. Make textbox the source of truth.
4. Add Custom dropdown state.
5. Add extended presets checkbox.
6. Add validation and user-friendly errors.
7. Save requested memory settings in URL project state.
8. Enforce local safety caps.

### Acceptance Criteria

A project requesting too much memory does not run automatically.

The UI reports:

```text
This project requests <N> of simulated memory. Your local limit is <M>.
```

## 22. Phase 18 - Super-Extended Memory Mode

### Goal

Allow advanced users to opt into very large virtual memory requests without making shared links dangerous.

### Tasks

1. Add local-only setting for super-extended memory.
2. Keep it off by default.
3. Do not save it in project state.
4. Add enable confirmation dialog with required checkbox.
5. Add run confirmation for oversized projects.
6. Add committed-memory cap.
7. Add lazy page allocation if not already complete.
8. Add tests for local-only behavior.

### Acceptance Criteria

A shared project can request 3 GiB, but it cannot force the recipient's browser to enable super-extended mode.

The recipient must manually enable it and confirm before running.

## 23. Phase 19 - Extended 32-bit Mode

### Goal

Add partial 64-bit support without claiming full x64 MASM compatibility.

### Tasks

1. Add execution mode selector.
2. Extend CPU state with 64-bit canonical registers.
3. Add aliases:
   - `RAX`/`EAX`/`AX`/`AH`/`AL`
   - `R8`/`R8D`/`R8W`/`R8B`
   - etc.
4. Implement QWORD data support if not already complete.
5. Implement selected 64-bit instructions and arithmetic.
6. Ensure 32-bit writes zero-extend into 64-bit canonical registers by default.
7. Update debugger register table.
8. Add tests.

### Acceptance Criteria

Extended mode supports:

```asm
mov rax, 10
add rax, 20
```

and displays:

```text
RAX = 000000000000001Eh / 30
EAX = 0000001Eh / 30
AX  = 001Eh / 30
AL  = 1Eh / 30
```

## 24. Phase 20 - Resource Watchdogs and Diagnostics

### Goal

Harden the simulator against runaway programs and expensive assembly inputs.

### Tasks

1. Add active execution time limit.
2. Add optional input wait timeout.
3. Add output actions:
   - stop
   - keep latest output
   - pause and ask
4. Add committed-memory cap.
5. Add call-depth diagnostics.
6. Add generated-instruction count limit.
7. Prepare future include/macro depth limits.

### Acceptance Criteria

Runaway execution, runaway output, and excessive memory allocation stop with structured diagnostics instead of freezing the page.

## 25. Phase 21 - Test Suite Expansion

### Goal

Convert example programs and bug cases into regression tests.

### Tasks

1. Add tests for every implemented instruction.
2. Add tests for register aliases.
3. Add tests for flag behavior.
4. Add tests for signed and unsigned jumps.
5. Add tests for memory permissions.
6. Add tests for unaligned access.
7. Add tests for Irvine32 routines.
8. Add tests for debugger deltas.
9. Add tests for URL schema migration.
10. Add tests for worker stop behavior where practical.

### Acceptance Criteria

All known supported behavior is covered by automated tests.

Bug fixes must include regression tests.

## 26. Phase 22 - Documentation and Examples

### Goal

Make the project understandable for users and future contributors.

### Tasks

1. Add user-facing quickstart.
2. Add examples:
   - hello world
   - arithmetic
   - loop
   - array write
   - ReadString
   - procedure call
3. Add supported syntax reference.
4. Add unsupported-feature list.
5. Add developer architecture notes.
6. Generate Doxygen documentation for C core.
7. Ensure all public APIs have Doxygen comments.

### Acceptance Criteria

A new developer can understand the architecture and add a simple instruction by following documentation.

A new user can run an example without reading the implementation.

## 27. Phase 23 - Polish and UX Hardening

### Goal

Improve quality, clarity, and resilience.

### Tasks

1. Improve error wording.
2. Add copyable diagnostics.
3. Add collapsible debugger panels.
4. Add compact and expanded register views.
5. Add signed decimal display option.
6. Add memory watch panel.
7. Add raw memory viewer.
8. Add project reset confirmation.
9. Add corrupted URL recovery flow.
10. Add UI state persistence for harmless local preferences.

### Acceptance Criteria

The simulator is comfortable for real educational use.

Errors are actionable and do not look like internal crashes unless the simulator itself failed.

## 28. Suggested AI Assistant Workflow

When using an AI assistant to implement the project, use small prompts tied to one phase at a time.

Good prompt style:

```text
Implement Phase 3 only. Add checked memory regions with read/write helpers, permissions, unaligned-access warning reporting, and unit tests. Follow the Doxygen documentation rules in the spec. Do not implement the parser yet.
```

Avoid prompts like:

```text
Build the whole MASM simulator.
```

For each phase, ask the assistant to:

1. Summarize the intended changes.
2. Modify only the necessary files.
3. Add or update tests.
4. Keep public APIs documented.
5. Run the test suite.
6. Explain any limitations.

## 29. Definition of v1 Complete

Version 1 is complete when a user can:

1. Paste a small MASM32/Irvine32 console program.
2. Run it in the browser.
3. Provide input when requested.
4. See program output in Program Console.
5. See simulator warnings/errors in Simulator Messages.
6. Step through instructions.
7. Inspect registers, aliases, flags, stack summary, and changed memory.
8. Use breakpoints and Step Over.
9. Configure memory, execution, and output limits.
10. Share the project with a compressed URL.

The v1 target is educational quality and predictable behavior, not full MASM compatibility.

