# MASM32 Educational Simulator

Static, browser-based MASM32 educational simulator with runtime MASM behavior implemented through Milestone 30, Milestone 31 native/Node diagnostic rendering tests, Milestone 32 fixed-layout policy infrastructure, Milestone 33 automatic deterministic layout sizing available to tests/configuration, Milestone 34 stack/heap size metadata applied to automatic layout, Milestone 35 seeded/fresh randomized layout placement available to tests/configuration, Milestone 35A MASM-compatible `OPTION CASEMAP` user-symbol case policy correction, Milestone 36 declared-object allocation map metadata for tests/internal tooling, Milestone 37 allocated-object warning mode for tests/configuration, Milestone 38 allocated-object strict mode for tests/configuration, Milestone 39 uninitialized-origin byte metadata/write tracking for test/internal inspection, Milestone 40 opt-in uninitialized-read warning/strict validation for tests/configuration, Milestone 41 virtual Irvine32 symbol-registry metadata/diagnostics, Milestone 42 Irvine32 `exit` virtual terminator behavior, Milestone 43 `inc`/`dec` instruction behavior, Milestone 44 `and`/`or`/`xor` instruction behavior, Milestone 45 `not` instruction behavior, Milestone 46 `shl`/`sal` instruction behavior, Milestone 47 `shr` instruction behavior, Milestone 48 `sar` instruction behavior, Milestone 49 `rol` instruction behavior, Milestone 50 `ror` instruction behavior, Milestone 50A undefined modeled-flag validity metadata for currently modeled flags, Milestone 50B opt-in undefined flag-use diagnostics for existing flag consumers, Milestone 51 post-30 smoke-harness validation coverage, Milestone 52 `lea` effective-address computation, and Milestone 52A signed register and memory value display formatting.

## Current scope

Implemented through the current milestone:

- C99 core, parser, executor, memory model, and WebAssembly export boundary.
- Static browser UI shell with worker-based source execution.
- MASM32-mode register aliases, arithmetic flags, checked memory regions, and minimal IR execution.
- Lexer/parser support through `.data`, `.DATA?`, `.CONST`, `.code`, symbols, numeric equates, extended constant expressions, constant and register-indirect memory operands, `PTR`, `OFFSET`, `TYPE`, `LENGTHOF`, `SIZEOF`, and character literals.
- Executable `mov`, `add`, `sub`, `movsx`, `movzx`, `cbw`, `cwde`, `cwd`, `cdq`, `xchg`, `neg`, `nop`, `adc`, `sbb`, `clc`, `stc`, `cmc`, `test`, `inc`, `dec`, `and`, `or`, `xor`, `not`, `shl`, `sal`, `shr`, `sar`, `rol`, and `ror` for the currently supported operand forms.
- Explicit `unsupported-feature` diagnostics for common textbook MASM constructs tracked by the Milestone 15 backlog checkpoint.
- Milestone 22 `test` instruction support, including MASM-compatible ambiguous memory-width diagnostics for `test [reg], imm` forms.
- Milestone 23 signed `PTR` width aliases: `SBYTE PTR`, `SWORD PTR`, `SDWORD PTR`, and recognized-but-deferred `SQWORD PTR` executable memory operations.
- Milestone 24 all-GPR register-indirect bases: `[eax]`, `[ebx]`, `[ecx]`, `[edx]`, `[esi]`, `[edi]`, `[ebp]`, and `[esp]`, including simple displacements.
- Milestone 25 global memory-width resolution rules: memory operands now share one parser validation path for the implemented memory-capable instructions, register operands can supply unambiguous memory width, and ambiguous memory/immediate forms produce `ambiguous-memory-width` diagnostics.
- Milestone 26 MASM32 header compatibility directives: `.386`, `.model flat, stdcall`, `.stack`, `OPTION CASEMAP:ALL`, `OPTION CASEMAP:NONE`, virtual `INCLUDE Irvine32.inc` / `INCLUDE Macros.inc`, and listing no-ops parse before the program body.
- Milestone 27 additional data sections: `.DATA?` accepts `?`/`DUP(?)` uninitialized declarations and emits deterministic zero-filled writable storage with uninitialized metadata; `.CONST` emits initialized read-only storage with direct and indirect write protection.
- Milestone 29 extended constant expressions: numeric equates now support unary `+`/`-`/`NOT`, parentheses, binary `+`, `-`, `*`, `/`, `MOD`, `SHL`, `SHR`, `AND`, `OR`, `XOR`, and extraction operators `HIGH`, `LOW`, `HIGHWORD`, and `LOWWORD` in supported compile-time constant contexts.
- Milestone 30 nested `DUP`: data declarations now support nested `DUP` expansion such as `ROWS DUP(COLS DUP(0))`, expression-backed counts and initializer values, deterministic `?` storage, and expansion-capacity diagnostics.
- Native diagnostic rendering harness: the aggregate test runner builds a C source-run JSON producer and a Node harness verifies exact Simulator Messages text through the same formatter module used by the browser UI. This verifies native JSON and formatter output, not that `web/dist` Wasm artifacts were rebuilt.
- Milestone 32 memory layout policy infrastructure: the VM memory loader now consumes an explicit fixed-layout policy object while preserving the existing educational region bases, sizes, permissions, and diagnostics. This adds no new MASM syntax or user-facing layout controls.
- Milestone 33 automatic deterministic layout sizing: tests/configuration can select automatic sizing that computes aligned region sizes from parsed code/data/const metadata and documented defaults. The served website still uses fixed educational layout by default.
- Milestone 34 stack/heap size metadata for layout: automatic layout now applies parsed `.stack` size metadata and configured heap-size requests to region capacity metadata. This remains test/configuration-only and adds no stack instructions, heap allocation API, or user-facing layout controls.
- Milestone 35 randomized layout placement: seeded and fresh randomized layout modes can be selected by tests/configuration. Symbolic addresses, `OFFSET`, `.CONST` permissions, and `.DATA?` storage relocate to selected bases; hardcoded fixed addresses are intentionally unreliable under randomized layout. No UI controls, URL seed persistence, object-bounds diagnostics, new MASM syntax, or new runtime instructions were added.
- Milestone 35A `OPTION CASEMAP` correction: user-defined symbols are case-insensitive by default, `CASEMAP:ALL` explicitly selects that policy, `CASEMAP:NONE` enables exact-case user-symbol lookup from that point forward, `CASEMAP:NOTPUBLIC` is recognized but unsupported, invalid CASEMAP values are diagnosed, and policy-change warnings do not block execution.
- Milestone 36 declared-object allocation map: tests/internal tooling can query declared `.data`, `.DATA?`, and `.CONST` objects with final selected-layout address ranges, size/type metadata, permissions, source locations, and a `not-tracked` initialization-origin placeholder. This metadata adds no object-bounds warnings/errors and does not change served-site execution behavior.
- Milestone 37 allocated-object warning mode: tests/configuration can enable non-fatal simulator warnings for valid-region memory accesses that are outside declared data objects, partially overlap object boundaries, or span adjacent objects.
- Milestone 38 allocated-object strict mode: tests/configuration can stop execution with `object-bounds-violation` for those same valid-region/object-boundary escapes. Default region-only execution remains unchanged; provenance diagnostics, uninitialized-read diagnostics, and UI controls remain deferred.
- Milestone 39 uninitialized-origin metadata: tests/internal tooling can inspect which `.data` and `.DATA?` bytes originated from `?`/`DUP(?)` and which bytes have been initialized by successful program writes. Default runtime values and browser UI behavior remain unchanged.
- Milestone 40 uninitialized-read validation: tests/configuration can enable warning mode, which emits non-fatal `uninitialized-read` simulator warnings, or strict mode, which stops execution before reading bytes that still have uninitialized-origin state. Default browser/source-run behavior remains warning-free and deterministic zero-filled.
- Milestone 41 virtual Irvine32 symbol registry: `INCLUDE Irvine32.inc` records known Irvine32 names as parser/source-run metadata and lets currently recognized executable uses of known not-yet-executable routines report `unsupported-irvine32-routine`.
- Milestone 42 Irvine32 `exit` terminator: with `INCLUDE Irvine32.inc` active, zero-operand `exit` terminates execution successfully and skips following instructions. This adds no `call`, `ret`, ExitProcess behavior, stack behavior, Program Console routines, other Irvine32 bodies, Windows API execution, or linking.
- Milestone 43 `inc`/`dec`: register and unambiguous memory destinations are supported for 8-bit, 16-bit, and 32-bit widths. These instructions update `ZF`, `SF`, and `OF`, preserve `CF`, and keep QWORD/SQWORD executable memory operations deferred.
- Milestone 44 `and`/`or`/`xor`: register and unambiguous memory destinations are supported with compatible register, immediate, or memory sources. These instructions store the logical result, update `ZF` and `SF`, clear `CF` and `OF`, reject memory-to-memory and ambiguous memory/immediate forms, and keep QWORD/SQWORD executable memory operations deferred.
- Milestone 45 `not`: register and unambiguous memory destinations are supported for 8-bit, 16-bit, and 32-bit widths. `not` stores the bitwise complement, preserves `CF`, `ZF`, `SF`, and `OF` exactly, rejects ambiguous memory-width forms, and keeps QWORD/SQWORD executable memory operations deferred.
- Milestone 46 `shl`/`sal`: register and unambiguous memory destinations are supported with immediate byte counts or `CL`; counts use `raw_count & 31`, count zero is a no-op, default undefined modeled-flag cases warn and continue with flag-specific wording, and test/configuration strict mode stops before mutation.
- Milestone 47 `shr`: logical right shift uses the same destination and count forms as the Phase 46 shifts, fills high bits with zero, defines `OF` from the original sign bit for one-bit shifts, warns by default for undefined modeled-flag cases, and preserves deterministic strict-mode behavior through the test/configuration API.
- Milestone 48 `sar`: arithmetic right shift reuses the shift destination/count forms, fills high bits with the original sign bit, clears `OF` for one-bit shifts, and uses the same deterministic `undefined-shift-flag` warning/strict policy as the other shift instructions.
- Milestone 49 `rol`: rotate left reuses the same destination and count forms, rotates bits within the selected 8-bit, 16-bit, or 32-bit width, updates `CF` from the least significant bit of the rotated result, preserves `ZF` and `SF`, defines `OF` only for one-bit rotates, and emits warning code `undefined-modeled-flag` for non-one nonzero counts while continuing execution. Strict shift validation remains shift-only and does not convert `ROL` warnings into runtime errors.
- Milestone 50 `ror`: rotate right reuses the same destination and count forms as `rol`, rotates bits within the selected 8-bit, 16-bit, or 32-bit width, updates `CF` from the most significant bit of the rotated result, preserves `ZF` and `SF`, defines `OF` only for one-bit rotates, and emits warning code `undefined-modeled-flag` for non-one nonzero counts while continuing execution. Strict shift validation remains shift-only and does not convert `ROR` warnings into runtime errors.
- Milestone 50A modeled-flag validity metadata: the C99 CPU/executor now tracks whether each currently modeled flag value (`CF`, `ZF`, `SF`, and `OF`) is architecturally valid or is a deterministic fallback from an undefined-flag producer. This is internal metadata for later flag-consumer diagnostics; it adds no new instructions, no default `undefined-flag-use` diagnostics, and no changed Program Console or Simulator Messages output.
- Milestone 50B undefined flag-use diagnostics: source-run tests/configuration can enable consumer-side `undefined-flag-use` diagnostics with `off`, `warn`, or `error` policy. Existing flag consumers `adc`, `sbb`, and `cmc` check `CF` before use. Warning mode emits a Simulator Messages warning and continues with the deterministic fallback bit; error mode stops before the consumer mutates state. Default browser/source-run behavior remains compatible with Milestone 50A and keeps the consumer policy off.
- Milestone 51 post-30 smoke harness: the aggregate native/Node test command now reports validation-only smoke coverage for fixed/automatic layout equivalence, `.CONST` permission precedence, uninitialized read-modify-write warnings, Irvine32 `exit` casing with CASEMAP preservation, and representative source-run plus rendered diagnostic smoke fixtures for the post-30 instruction families. This adds no new MASM syntax or runtime behavior.
- Milestone 52 `lea`: computes effective addresses into 32-bit register destinations for the currently supported address-expression forms, writes no memory, reads no memory, preserves modeled flags, emits no memory/object/uninitialized diagnostics for address computation itself, and keeps scaled-index addressing deferred.
- Milestone 52A signed value display: existing final register and memory-change displays show hexadecimal, unsigned decimal, and signed decimal interpretations for known-width 8-bit, 16-bit, and 32-bit integer values. Final registers use aligned grouped rows for register families, and memory changes use aligned old/new blocks. This is display-only formatting; it does not change parser behavior, VM execution, memory bytes, flags, Program Console output, or Simulator Messages text.
- Command-line native and JavaScript tests.
- Windows development scripts for Visual Studio and Emscripten.

Not implemented yet:

- Control flow, stack, call/ret, Irvine32 routines other than the virtual `exit` terminator, debugger stepping, scaled-index addressing, carry rotates, multiplication, division, macros, runtime high-level condition expressions, full expression parsing beyond the Milestone 29 compile-time subset, and Windows API behavior.
- Extended 32-bit / 64-bit register behavior.

See `docs/SUPPORTED_SYNTAX.md` for the current supported subset, scheduled features, and recognized unsupported constructs.

## Run native tests

```sh
./scripts/run_tests.py
```

On Windows, run the same test command from a terminal with Python and a C compiler available:

```cmd
python scripts\run_tests.py
```

The native tests do not require Emscripten. They compile the C99 core/parser/executor/source-run tests with the host C compiler, build the native diagnostic JSON producer, then run JavaScript protocol, formatter, and diagnostic-rendering harness tests with Node.js.

## Install emsdk on Windows

Emscripten recommends installing through `emsdk`. On Windows, use `emsdk.bat` and `emsdk_env.bat` rather than the POSIX shell scripts.

A typical Command Prompt setup is:

```cmd
git clone https://github.com/emscripten-core/emsdk.git C:\tools\emsdk
cd /d C:\tools\emsdk
emsdk.bat install latest
emsdk.bat activate latest
emsdk_env.bat
```

Set `EMSDK_ROOT` to the root of the cloned `emsdk` directory. This lets the project scripts activate the Emscripten environment even when launched from Visual Studio:

```cmd
setx EMSDK_ROOT C:\tools\emsdk
```

Restart Visual Studio or any open terminals after using `setx` so they inherit the new environment variable.

The build script also works when `emcc` is already on `PATH`, but `EMSDK_ROOT` is the preferred Windows configuration for Visual Studio integration.

## Build Wasm

### Windows

```cmd
scripts\windows\build_wasm.cmd
```

The script writes the Emscripten output to:

```text
web\dist\masm32_sim_core.js
web\dist\masm32_sim_core.wasm
```

That path matches the worker import path in `web/src/worker.js`:

```text
../dist/masm32_sim_core.js
```

### POSIX shell

```sh
./scripts/build_wasm.sh
```

## Clean Wasm outputs on Windows

```cmd
scripts\windows\clean_wasm.cmd
```

This removes only generated Phase 0 Emscripten outputs from `web\dist`.

## Serve the web app

The app uses JavaScript modules and a Web Worker, so open it through a local HTTP server rather than directly from `file://`.

### Windows

```cmd
scripts\windows\serve_web.cmd
```

The default port is `8000`. To use a different port, pass it as the first argument:

```cmd
scripts\windows\serve_web.cmd 8080
```

On Windows, `serve_web.cmd` runs Python `http.server` as a tracked foreground process. When the tool is configured with **Use Output window** enabled, the live server log remains visible in Visual Studio's Output window instead of opening a separate console window. The script records the process identifier and selected port in:

```text
build\dev-server.pid
build\dev-server.port
```

Then open:

```text
http://localhost:8000
```

### Stop the Windows web server

Use the explicit stop script rather than Visual Studio's tool-cancel button:

```cmd
scripts\windows\stop_web.cmd
```

The stop script reads `build\dev-server.pid`, verifies that the process still looks like this project's Python `http.server`, stops only that process, and removes stale PID files. If the direct PID is stale, it can fall back to the recorded port and stop only a matching Python `http.server` listener. It intentionally avoids broad commands such as killing every Python process.

### POSIX shell

```sh
python3 -m http.server 8000 --directory web
```

## Manual browser check

1. Build the Wasm module if Emscripten is installed.
2. Serve the `web` folder.
3. Open the served page in a browser.
4. Confirm the Simulator Messages panel receives a `READY` message.
5. Click **Ping worker**.
6. Confirm the Program Console receives a `PONG` message.

If the Wasm artifact has not been built yet, the worker still initializes and reports `wasm.status` as `not-built`. That is expected for Phase 0.

## Visual Studio External Tools setup

Visual Studio can run the Windows scripts through **Tools > External Tools...**.

Suggested entries:

### Build Wasm

- Title: `Build MASM32 Simulator Wasm`
- Command: `$(SolutionDir)scripts\windows\build_wasm.cmd`
- Initial directory: `$(SolutionDir)`
- Use Output window: enabled

### Clean Wasm

- Title: `Clean MASM32 Simulator Wasm`
- Command: `$(SolutionDir)scripts\windows\clean_wasm.cmd`
- Initial directory: `$(SolutionDir)`
- Use Output window: enabled

### Serve Web

- Title: `Serve MASM32 Simulator Web`
- Command: `$(SolutionDir)scripts\windows\serve_web.cmd`
- Arguments: `8000`
- Initial directory: `$(SolutionDir)`
- Use Output window: enabled

This command stays running and streams Python `http.server` output to Visual Studio's Output window. It does not open a separate console window. Use the Stop Web tool to stop the recorded server process.

### Stop Web

- Title: `Stop MASM32 Simulator Web`
- Command: `$(SolutionDir)scripts\windows\stop_web.cmd`
- Initial directory: `$(SolutionDir)`
- Use Output window: enabled

Use this explicit stop tool instead of relying on Visual Studio to cancel the running tool. External Tools are best treated as command launchers; the project owns the server lifecycle through the recorded PID and port files.

If the project is opened without a solution file, replace `$(SolutionDir)` with the absolute path to the repository root.

## Visual Studio Makefile Project setup

A Visual Studio Makefile Project can call the same scripts without introducing MSBuild-specific project files yet.

Recommended NMake settings:

- Build Command Line: `scripts\windows\build_wasm.cmd`
- Rebuild Command Line: `scripts\windows\clean_wasm.cmd && scripts\windows\build_wasm.cmd`
- Clean Command Line: `scripts\windows\clean_wasm.cmd`
- Output: `web\dist\masm32_sim_core.js`

Optional Makefile Project helper commands can also point at:

```cmd
scripts\windows\serve_web.cmd 8000
scripts\windows\stop_web.cmd
```

Set the project working directory to the repository root. Keep the native simulator source files as C files; do not enable C++ compilation for the core.

## Notes for later milestones

- Keep all simulator execution inside the worker.
- Keep C APIs documented with triple-slash Doxygen comments.
- Do not add parser, memory, instruction execution, or debugger behavior until the corresponding milestone.
