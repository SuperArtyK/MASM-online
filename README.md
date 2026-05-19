# MASM32 Educational Simulator

Static, browser-based MASM32 educational simulator with runtime MASM behavior implemented through Milestone 30, Milestone 31 native/Node diagnostic rendering tests, Milestone 32 fixed-layout policy infrastructure, Milestone 33 automatic deterministic layout sizing available to tests/configuration, Milestone 34 stack/heap size metadata applied to automatic layout, and Milestone 35 seeded/fresh randomized layout placement available to tests/configuration.

## Current scope

Implemented through the current milestone:

- C99 core, parser, executor, memory model, and WebAssembly export boundary.
- Static browser UI shell with worker-based source execution.
- MASM32-mode register aliases, arithmetic flags, checked memory regions, and minimal IR execution.
- Lexer/parser support through `.data`, `.DATA?`, `.CONST`, `.code`, symbols, numeric equates, extended constant expressions, constant and register-indirect memory operands, `PTR`, `OFFSET`, `TYPE`, `LENGTHOF`, `SIZEOF`, and character literals.
- Executable `mov`, `add`, `sub`, `movsx`, `movzx`, `cbw`, `cwde`, `cwd`, `cdq`, `xchg`, `neg`, `nop`, `adc`, `sbb`, `clc`, `stc`, `cmc`, and `test` for the currently supported operand forms.
- Explicit `unsupported-feature` diagnostics for common textbook MASM constructs tracked by the Milestone 15 backlog checkpoint.
- Milestone 22 `test` instruction support, including MASM-compatible ambiguous memory-width diagnostics for `test [reg], imm` forms.
- Milestone 23 signed `PTR` width aliases: `SBYTE PTR`, `SWORD PTR`, `SDWORD PTR`, and recognized-but-deferred `SQWORD PTR` executable memory operations.
- Milestone 24 all-GPR register-indirect bases: `[eax]`, `[ebx]`, `[ecx]`, `[edx]`, `[esi]`, `[edi]`, `[ebp]`, and `[esp]`, including simple displacements.
- Milestone 25 global memory-width resolution rules: memory operands now share one parser validation path for the implemented memory-capable instructions, register operands can supply unambiguous memory width, and ambiguous memory/immediate forms produce `ambiguous-memory-width` diagnostics.
- Milestone 26 MASM32 header compatibility directives: `.386`, `.model flat, stdcall`, `.stack`, `OPTION CASEMAP:NONE`, virtual `INCLUDE Irvine32.inc` / `INCLUDE Macros.inc`, and listing no-ops parse before the program body.
- Milestone 27 additional data sections: `.DATA?` accepts `?`/`DUP(?)` uninitialized declarations and emits deterministic zero-filled writable storage with uninitialized metadata; `.CONST` emits initialized read-only storage with direct and indirect write protection.
- Milestone 29 extended constant expressions: numeric equates now support unary `+`/`-`/`NOT`, parentheses, binary `+`, `-`, `*`, `/`, `MOD`, `SHL`, `SHR`, `AND`, `OR`, `XOR`, and extraction operators `HIGH`, `LOW`, `HIGHWORD`, and `LOWWORD` in supported compile-time constant contexts.
- Milestone 30 nested `DUP`: data declarations now support nested `DUP` expansion such as `ROWS DUP(COLS DUP(0))`, expression-backed counts and initializer values, deterministic `?` storage, and expansion-capacity diagnostics.
- Native diagnostic rendering harness: the aggregate test runner builds a C source-run JSON producer and a Node harness verifies exact Simulator Messages text through the same formatter module used by the browser UI. This verifies native JSON and formatter output, not that `web/dist` Wasm artifacts were rebuilt.
- Milestone 32 memory layout policy infrastructure: the VM memory loader now consumes an explicit fixed-layout policy object while preserving the existing educational region bases, sizes, permissions, and diagnostics. This adds no new MASM syntax or user-facing layout controls.
- Milestone 33 automatic deterministic layout sizing: tests/configuration can select automatic sizing that computes aligned region sizes from parsed code/data/const metadata and documented defaults. The served website still uses fixed educational layout by default.
- Milestone 34 stack/heap size metadata for layout: automatic layout now applies parsed `.stack` size metadata and configured heap-size requests to region capacity metadata. This remains test/configuration-only and adds no stack instructions, heap allocation API, or user-facing layout controls.
- Milestone 35 randomized layout placement: seeded and fresh randomized layout modes can be selected by tests/configuration. Symbolic addresses, `OFFSET`, `.CONST` permissions, and `.DATA?` storage relocate to selected bases; hardcoded fixed addresses are intentionally unreliable under randomized layout. No UI controls, URL seed persistence, object-bounds diagnostics, new MASM syntax, or new runtime instructions were added.
- Command-line native and JavaScript tests.
- Windows development scripts for Visual Studio and Emscripten.

Not implemented yet:

- Control flow, stack, call/ret, Irvine32 routines, debugger stepping, macros, runtime high-level condition expressions, and full expression parsing beyond the Milestone 29 compile-time subset, and Windows API behavior.
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
