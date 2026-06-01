# Building and Development

This file is the detailed build, local-serving, and development-environment reference for the Online MASM32 Educational Simulator. It was extracted from README-level guidance during Phase 57B so the README can remain a concise landing page.

Source-of-truth rule:

- [`FULL_IMPLEMENTATION_SPEC.md`](FULL_IMPLEMENTATION_SPEC.md) remains the canonical source for product boundaries, stable behavior, cross-cutting rules, and current/future/non-goal distinctions.
- [`INCREMENTAL_IMPLEMENTATION_GUIDE.md`](INCREMENTAL_IMPLEMENTATION_GUIDE.md) remains the canonical source for phase numbering, phase tasks, required tests, and acceptance criteria.
- [`TESTING_GUIDE.md`](TESTING_GUIDE.md) remains the detailed test-runner, fixture-inventory, timeout, and failure-reporting reference.
- This file is a development-operations guide. It does not define supported MASM syntax or runtime behavior.

Current status:

Repository/archive milestone:
Phase 61B - Branch Runtime Watchdog Scope Cleanup

Runtime/source-run MASM behavior phase:
Phase 61 - Direct JMP Runtime Execution: Direct `jmp label` forms are parsed, classified, lowered, and executed when they target executable code labels or procedure-entry labels. Direct JMP runtime transfer counts as one executed VM instruction, preserves modeled flags, and produces no memory-change row.

Status interpretation:
Phase 61B is documentation/static-check hardening for the Phase 61 direct-JMP watchdog boundary. It does not add a new runtime/source-run MASM behavior phase, and active-time watchdog behavior remains future work owned by Phase 200 - Active Time Watchdog and Worker Responsiveness.

Common paths:

```text
README.md
scripts/run_tests.py
scripts/build_wasm.sh
scripts/windows/build_wasm.cmd
scripts/windows/clean_wasm.cmd
scripts/windows/serve_web.cmd
scripts/windows/serve_web.ps1
scripts/windows/stop_web.cmd
scripts/windows/stop_web.ps1
src/core/
src/parser/
src/wasm/
tests/core/
tests/web/
web/index.html
web/src/
web/dist/
docs/
```

Core simulator code is C99 and lives under `src/core`, `src/parser`, and `src/wasm`. Browser code lives under `web/src`. Generated or rebuilt WebAssembly artifacts live under `web/dist`.

## Local website serving

The browser app uses JavaScript modules and a Web Worker. Serve the `web` directory over local HTTP instead of opening `web/index.html` with `file://`.

POSIX shell:

```sh
python3 -m http.server 8000 --directory web
```

Windows command prompt:

```cmd
scripts\windows\serve_web.cmd
```

Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\serve_web.ps1
```

Then open:

```text
http://localhost:8000
```

To stop the Windows helper server:

```cmd
scripts\windows\stop_web.cmd
```

or:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\stop_web.ps1
```

If you start a POSIX `python3 -m http.server` process manually, stop it from the terminal where it is running.

## Native test prerequisites

Native tests require:

- Python 3.
- A C compiler capable of compiling the core as C99.
- Node.js for browser-side formatter, settings, and protocol tests.

The aggregate runner builds and runs native C tests, source-run integration tests, Node tests, diagnostic rendering tests, protocol tests, static structure checks, and documentation checks. It also reports whether browser/Wasm smoke after rebuild was skipped because Emscripten is unavailable.

## Emscripten prerequisites

Rebuilding browser WebAssembly artifacts requires Emscripten and a working `emcc` on `PATH`.

Check availability with:

```sh
emcc --version
```

or on Windows:

```cmd
where emcc
```

If `emcc` is missing, the native C and Node test groups can still run. Browser/Wasm rebuild smoke checks are reported as skipped by the test runner when `emcc` is unavailable in the current environment.

## Build commands

Build the WebAssembly module on POSIX systems:

```sh
./scripts/build_wasm.sh
```

Build the WebAssembly module from Windows command prompt:

```cmd
scripts\windows\build_wasm.cmd
```

Clean generated WebAssembly outputs on Windows:

```cmd
scripts\windows\clean_wasm.cmd
```

The build scripts write browser-loadable artifacts under `web/dist`.

## Test commands

Aggregate verification:

```sh
python3 scripts/run_tests.py --all
```

The default runner invocation is also intended to be full verification:

```sh
python3 scripts/run_tests.py
```

Focused verification groups are available when a hosted assistant/container environment times out or when a developer wants a smaller test target:

```sh
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
```

Quick smoke verification:

```sh
python3 scripts/run_tests.py --quick
```

Output control:

```sh
python3 scripts/run_tests.py --quiet --all
python3 scripts/run_tests.py --verbose --diagnostics
```

Windows examples using the Python launcher:

```cmd
py scripts\run_tests.py --all
py scripts\run_tests.py --diagnostics
```

Use [`TESTING_GUIDE.md`](TESTING_GUIDE.md) for the full runner contract, focused-group ownership, source-run fixture inventory, timeout-safe assistant reporting rules, and failure-output policy.

## Browser/Wasm smoke guidance

When `emcc` is available and WebAssembly artifacts are rebuilt:

1. Run the build command for the platform.
2. Serve the `web` directory over local HTTP.
3. Open the page in a browser.
4. Run a small accepted source program.
5. Confirm Program Console and Simulator Messages remain separated.
6. Confirm final registers, including Phase 57H `[unchanged]` parent-row markers, and memory changes render as expected.

Suggested small program:

```asm
.code
main PROC
    mov eax, -10
    cdq
    mov ebx, 3
    idiv ebx
main ENDP
END main
```

The expected arithmetic instruction subset remains anchored by Phase 57 - Signed IDIV, while repository/archive status is Phase 61B - Branch Runtime Watchdog Scope Cleanup and runtime/source-run MASM behavior status remains Phase 61 - Direct JMP Runtime Execution. Phase 61 executes already-lowered direct `jmp label` forms targeting executable code labels or procedure-entry labels. Phase 59 source-run/test callers may use `instructionLimit` to enforce a deterministic executed-instruction watchdog; this is the instruction-count watchdog for direct-JMP loops. No browser UI limit control is currently required for the Phase 59 instruction-count watchdog path. Active-time watchdog behavior is not part of Phase 61, Phase 61A, or Phase 61B; it remains future work owned by Phase 200 - Active Time Watchdog and Worker Responsiveness. Phase 58 label metadata and duplicate/conflict diagnostics remain available, while conditional jumps, `loop`, indirect jumps, register-target jumps, memory-target jumps, immediate-target jumps, `call`, `ret`, stack behavior, procedure invocation, branch-distance/type overrides, debugger stepping, breakpoints, and UI label navigation remain future work. Phase 57S high-level-flow diagnostics remain active without condition evaluation, label lowering, branch execution, expression parsing for conditions, or block execution; Phase 57R INVOKE/ADDR/external-routine diagnostics remain available without adding invocation lowering, calling conventions, WinAPI execution, CRT/MASM32 runtime execution, PE loading, a linker, or external routine execution; Phase 57Q INCLUDELIB linker/library diagnostics remain available without adding host filesystem access, library search paths, object files, imports, PE loading, a linker, WinAPI execution, or external routine execution; Phase 57P host/path-like `INCLUDE` diagnostics remain available without adding host filesystem access, include search paths, or WinAPI execution; Phase 57O - Explicit-Width NOP Encoding-Operand Forms accepted NOP register and explicit-width memory-looking encoding operands remain available without adding browser UI controls or real x86 byte emission; Phase 57M parser/source-run diagnostics for MASM segment/group symbols, including `unsupported-segment-symbol`, and Phase 57L `.code` memory-access diagnostics, including `unsupported-code-memory-access`, remain active. Phase 57J - .CONST Uninitialized Storage Diagnostics and Policy remains the owner of configurable `.CONST ?` / `.CONST DUP(?)` declaration diagnostics. Phase 57J adds configurable declaration diagnostics for `.CONST ?` / `.CONST DUP(?)`; Phase 57I adds read-only `.CONST ?` / `.CONST DUP(?)` acceptance, Phase 57H adds final-register display markers, Phase 57G adds source-run/test-facing seeded uninitialized-storage visible-byte settings, and Phase 57F preserves seeded register/flag startup settings.

## Missing `emcc` troubleshooting

If the Wasm build script fails because `emcc` cannot be found:

1. Install or activate Emscripten for the current shell.
2. Confirm `emcc --version` works from the same shell that will run the build script.
3. Re-run `./scripts/build_wasm.sh` or `scripts\windows\build_wasm.cmd`.
4. If working in a hosted assistant/container environment where Emscripten is not installed, report browser/Wasm rebuild smoke as skipped because `emcc` is unavailable.

Do not treat missing `emcc` as a native/source-run/Node test failure. It is a dependency limitation for rebuilding browser WebAssembly artifacts.

## Visual Studio notes

The inspected repository state does not include a committed Visual Studio solution or project file such as `.sln` or `.vcxproj`.

Implication:

- Visual Studio solution usage is local-only unless a solution file is later added to the repository.
- Repository-supported Windows command-file paths are the scripts under `scripts\windows`.
- If a developer creates a local Visual Studio Makefile Project, configure it to call the repository scripts rather than inventing separate build semantics.

Optional local Visual Studio External Tools entries may point to these repository commands:

```text
Serve site: scripts\windows\serve_web.cmd
Stop site:  scripts\windows\stop_web.cmd
Build Wasm: scripts\windows\build_wasm.cmd
Clean Wasm: scripts\windows\clean_wasm.cmd
Run tests:  py scripts\run_tests.py --all
```

These Visual Studio entries are optional local convenience wrappers. They are not required repository state and do not change the canonical build or test commands.

## Development guardrails

- Keep core VM/parser/executor/memory/Irvine32/Wasm API code in C99 `.c` and `.h` files.
- Do not introduce C++ core code, C++ standard library use, classes, templates, exceptions, RTTI, or `extern "C"` wrappers.
- Keep browser UI code in JavaScript or TypeScript.
- Route simulator execution through the Web Worker.
- Route simulated memory reads and writes through checked VM memory helpers.
- Keep Program Console output separate from Simulator Messages diagnostics.
- Add structured and rendered Simulator Messages tests for user-visible diagnostic changes.
- Do not advance runtime/source-run MASM behavior phase metadata for documentation-only or repository-hygiene work.
