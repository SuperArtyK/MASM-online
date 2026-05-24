# Testing Guide

This document describes practical ways to build, run, and manually test the MASM32 educational simulator from a local development checkout. It expands the short README commands with Windows CMD, Visual Studio Developer PowerShell, Linux shells, Node harness, native diagnostic producer, and browser/Wasm workflows.

The examples assume commands are run from the repository root.

## 1. Prerequisites

### Required for native and Node tests

Install or make available:

- Python 3
- Node.js
- A C99-compatible compiler usable from the command line

On Windows with Visual Studio, the recommended compiler for the existing test runner is Clang from the Visual Studio C++ toolchain, because the runner uses GCC/Clang-style flags such as `-std=c99`, `-Wall`, `-Wextra`, `-Werror`, and `-pedantic`.

On Linux, GCC or Clang can be used. The examples below use GCC by default and also show the optional `CC=clang` form.

In the Visual Studio Installer, install:

- Desktop development with C++
- C++ Clang tools for Windows

Then use either:

- **Developer PowerShell for Visual Studio**
- **x64 Native Tools Command Prompt for VS**
- The Visual Studio integrated terminal opened through **View -> Terminal**, when it is configured as a Developer PowerShell environment

Check tools:

```cmd
py --version
node --version
clang --version
```

PowerShell equivalent:

```powershell
py --version
node --version
clang --version
```

Linux shell equivalent:

```sh
python3 --version
node --version
cc --version
```

Optional Linux package examples:

Debian/Ubuntu:

```sh
sudo apt update
sudo apt install build-essential python3 nodejs npm
```

Fedora:

```sh
sudo dnf install gcc python3 nodejs npm
```

Arch Linux:

```sh
sudo pacman -S base-devel python nodejs npm
```

If you prefer Clang on Linux, install `clang` and run the aggregate tests with `CC=clang`.

### Required for browser/Wasm rebuilds

To rebuild `web/dist/masm32_sim_core.js` and `web/dist/masm32_sim_core.wasm`, install and activate Emscripten/emsdk so `emcc` is available.

Check:

```cmd
emcc --version
```

PowerShell equivalent:

```powershell
emcc --version
```

Linux shell equivalent:

```sh
emcc --version
```

If `emcc` is unavailable, native and Node tests can still be run, but Wasm artifacts cannot be rebuilt locally.

## 2. Running the full test suite

The aggregate test runner builds native C test binaries, runs them, builds the native diagnostic JSON producer, and runs Node-based protocol/formatter/diagnostic-rendering tests.

### Windows CMD

```cmd
set CC=clang
py scripts\run_tests.py
```

### Developer PowerShell / Visual Studio Terminal

```powershell
$env:CC = "clang"
py scripts\run_tests.py
```

### Linux shell

Using the default compiler (`cc`):

```sh
python3 scripts/run_tests.py
```

Using GCC explicitly:

```sh
CC=gcc python3 scripts/run_tests.py
```

Using Clang explicitly:

```sh
CC=clang python3 scripts/run_tests.py
```

Expected final output:

```text
All implemented milestone tests passed.
```

The test runner writes native test binaries under:

```text
build\tests\
```

On Windows, these binaries are emitted with `.exe` suffixes, for example:

```text
build\tests\test_object_map.exe
build\tests\diagnostic_json_producer.exe
```

On Linux, the same binaries are extensionless, for example:

```text
build/tests/test_object_map
build/tests/diagnostic_json_producer
```

## 3. Running one native test binary directly

First run the full test suite once so binaries are built, or let the failing aggregate run build the binary before stopping.

### Windows CMD

```cmd
build\tests\test_object_map.exe
```

### Developer PowerShell

```powershell
.\build\tests\test_object_map.exe
```

### Linux shell

```sh
./build/tests/test_object_map
```

Successful tests usually print a short pass message, for example:

```text
test_object_map passed
```

## 4. Running Node tests directly

The Node tests cover browser-side protocol and formatting code without launching a browser.

### Protocol formatter tests that do not need native producer

CMD:

```cmd
node tests\web\test_protocol.mjs
node tests\web\test_formatters.mjs
```

PowerShell:

```powershell
node tests\web\test_protocol.mjs
node tests\web\test_formatters.mjs
```

Linux shell:

```sh
node tests/web/test_protocol.mjs
node tests/web/test_formatters.mjs
```

### Diagnostic rendering test

`tests/web/test_diagnostic_rendering.mjs` needs the native diagnostic producer. The aggregate runner passes its path automatically. If running it manually, set `MASM32_DIAGNOSTIC_JSON_PRODUCER` first.

CMD:

```cmd
set MASM32_DIAGNOSTIC_JSON_PRODUCER=%CD%\build\tests\diagnostic_json_producer.exe
node tests\web\test_diagnostic_rendering.mjs
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_JSON_PRODUCER = "$PWD\build\tests\diagnostic_json_producer.exe"
node tests\web\test_diagnostic_rendering.mjs
```

Linux shell:

```sh
MASM32_DIAGNOSTIC_JSON_PRODUCER="$PWD/build/tests/diagnostic_json_producer" node tests/web/test_diagnostic_rendering.mjs
```

Expected result: the script exits successfully after printing its `PASS ...` lines.

## 5. Manual source-run tests with the diagnostic JSON producer

The native diagnostic producer runs MASM-like source through the same C source-run path used by the Wasm-facing API and prints JSON. This is useful for testing parser, execution, diagnostics, and non-default test modes without the browser.

Build it with:

CMD:

```cmd
set CC=clang
py scripts\run_tests.py
```

PowerShell:

```powershell
$env:CC = "clang"
py scripts\run_tests.py
```

Linux shell:

```sh
python3 scripts/run_tests.py
```

or explicitly:

```sh
CC=gcc python3 scripts/run_tests.py
```

Then run a source file through the producer.

### CMD, from a `.asm` file

```cmd
type program.asm | build\tests\diagnostic_json_producer.exe
```

### PowerShell, from a `.asm` file

```powershell
Get-Content .\program.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

### Linux shell, from a `.asm` file

```sh
cat program.asm | ./build/tests/diagnostic_json_producer
```

or:

```sh
./build/tests/diagnostic_json_producer < program.asm
```

### CMD, with an input file argument

```cmd
build\tests\diagnostic_json_producer.exe program.asm
```

### PowerShell, with an input file argument

```powershell
.\build\tests\diagnostic_json_producer.exe .\program.asm
```

### Linux shell, with an input file argument

```sh
./build/tests/diagnostic_json_producer program.asm
```

The output is JSON. Search it for message codes such as:

```text
execution-complete
assembly-error
runtime-error
simulator-warning
object-bounds-warning
```

## 6. Selecting diagnostic producer modes

The diagnostic producer supports environment variables for internal/test-only modes.

### Memory validation mode

After Phase 53C, omitted/default source-run behavior emits teaching warnings
for uninitialized-origin reads. To preserve the older silent region-only
behavior in local diagnostic-producer tests, explicitly select `off` or
`region-only`.

Explicit memory-validation values include:

```text
off
region-only
allocated-object-warnings
allocated-object-strict
uninitialized-read-warnings
uninitialized-read-strict
```

Milestone 37 added allocated-object warning mode:

```text
allocated-object-warnings
```

CMD:

```cmd
set MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_MEMORY_VALIDATION = "allocated-object-warnings"
```

Linux shell:

```sh
export MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

For one command only on Linux:

```sh
MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings ./build/tests/diagnostic_json_producer program.asm
```

Reset to Phase 53C/53D/53E default teaching diagnostics and compatibility notices:

CMD:

```cmd
set MASM32_DIAGNOSTIC_MEMORY_VALIDATION=
```

PowerShell:

```powershell
Remove-Item Env:MASM32_DIAGNOSTIC_MEMORY_VALIDATION -ErrorAction SilentlyContinue
```

Linux shell:

```sh
unset MASM32_DIAGNOSTIC_MEMORY_VALIDATION
```

Explicit opt-out example on Linux:

```sh
MASM32_DIAGNOSTIC_MEMORY_VALIDATION=off ./build/tests/diagnostic_json_producer program.asm
```

Undefined modeled-flag consumer diagnostics also default to warnings after
Phase 53C. Phase 53D also emits default `simulator-notice` compatibility messages for accepted no-op, metadata-only, and limited-behavior MASM constructs. Phase 53E exposes these policies in the browser UI as local page-session settings while preserving the same backend defaults. To preserve the older silent consumer behavior in a local diagnostic
producer run, set:

```text
MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE=off
```


### Compatibility notices

After Phase 53D, accepted MASM compatibility constructs such as `.686`, `.model flat, stdcall`, `.stack 4096`, `INCLUDE Macros.inc`, `TITLE`, `SUBTITLE`, and `PAGE` emit non-fatal `simulator-notice` messages by default. These notices are informational, do not write Program Console output, and do not change VM execution. Active semantic constructs such as `INCLUDE Irvine32.inc`, `OPTION CASEMAP`, `.DATA?`, and `.CONST` are not reported as generic no-ops.

After Phase 53E, browser users can turn compatibility notices off and can select the existing memory range validation, uninitialized-read, and undefined-flag-use policies from the page. These browser settings are local preferences for the page session; share URLs are not implemented yet and do not currently encode diagnostic settings. The default browser profile remains region-only memory validation, uninitialized-read warn, undefined-flag-use warn, and compatibility notices on.

### Layout mode

Some layout tests use diagnostic producer layout modes. Current supported environment usage includes values such as fixed/default, automatic, seeded randomized, or fresh randomized depending on the implemented milestone and test harness support.

CMD example:

```cmd
set MASM32_DIAGNOSTIC_LAYOUT_MODE=automatic
```

PowerShell example:

```powershell
$env:MASM32_DIAGNOSTIC_LAYOUT_MODE = "automatic"
```

Linux shell example:

```sh
export MASM32_DIAGNOSTIC_LAYOUT_MODE=automatic
```

For one command only on Linux:

```sh
MASM32_DIAGNOSTIC_LAYOUT_MODE=automatic ./build/tests/diagnostic_json_producer program.asm
```

Reset:

CMD:

```cmd
set MASM32_DIAGNOSTIC_LAYOUT_MODE=
```

PowerShell:

```powershell
Remove-Item Env:MASM32_DIAGNOSTIC_LAYOUT_MODE -ErrorAction SilentlyContinue
```

Linux shell:

```sh
unset MASM32_DIAGNOSTIC_LAYOUT_MODE
```

## 7. Milestone 37 manual test programs

These programs demonstrate allocated-object warning mode. They are not visible in the served website by default because the browser UI does not currently expose a validation-mode selector.

Before running them, enable warning mode.

CMD:

```cmd
set MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_MEMORY_VALIDATION = "allocated-object-warnings"
```

Linux shell:

```sh
export MASM32_DIAGNOSTIC_MEMORY_VALIDATION=allocated-object-warnings
```

### 7.1 Valid `.data` region gap read warns and continues

Save as `phase37_gap_read.asm`:

```asm
.data
x DWORD 1234h

.code
main PROC
    mov esi, OFFSET x
    mov eax, DWORD PTR [esi + 4]
main ENDP
END main
```

CMD:

```cmd
type phase37_gap_read.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_gap_read.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_gap_read.asm
```

Expected:

- JSON contains `object-bounds-warning`
- JSON contains `execution-complete`
- final `EAX` is zero because the read is from valid zero-filled data-region storage outside a declared object

### 7.2 Valid `.data` region gap write warns and continues

Save as `phase37_gap_write.asm`:

```asm
.data
x DWORD 0

.code
main PROC
    mov esi, OFFSET x
    mov DWORD PTR [esi + 4], 99
    mov eax, DWORD PTR [esi + 4]
main ENDP
END main
```

CMD:

```cmd
type phase37_gap_write.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_gap_write.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_gap_write.asm
```

Expected:

- JSON contains `object-bounds-warning`
- JSON contains `execution-complete`
- final `EAX` is `00000063h / 99`

### 7.3 Access into another declared object does not warn

Save as `phase37_other_object.asm`:

```asm
.data
var1 DWORD 1111h
arr1 DWORD 2222h, 3333h

.code
main PROC
    mov esi, OFFSET var1
    mov eax, DWORD PTR [esi + 4]
main ENDP
END main
```

CMD:

```cmd
type phase37_other_object.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_other_object.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_other_object.asm
```

Expected:

- JSON does not contain `object-bounds-warning`
- JSON contains `execution-complete`
- final `EAX` is `00002222h / 8738`

This confirms that Milestone 37 is not provenance tracking. The pointer starts from `var1`, but the final access lands wholly inside `arr1`.

### 7.4 Access spanning adjacent objects warns and continues

Save as `phase37_span_objects.asm`:

```asm
.data
a BYTE 11h
b BYTE 22h

.code
main PROC
    mov esi, OFFSET a
    mov ax, WORD PTR [esi]
main ENDP
END main
```

CMD:

```cmd
type phase37_span_objects.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_span_objects.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_span_objects.asm
```

Expected:

- JSON contains `object-bounds-warning`
- JSON contains `execution-complete`
- final `AX` is `2211h`

### 7.5 `.CONST` permission failure takes precedence

Save as `phase37_const_write.asm`:

```asm
.CONST
limit DWORD 10

.code
main PROC
    mov esi, OFFSET limit
    mov DWORD PTR [esi], 20
main ENDP
END main
```

CMD:

```cmd
type phase37_const_write.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_const_write.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_const_write.asm
```

Expected:

- JSON contains a runtime permission/write diagnostic
- JSON does not contain `execution-complete`
- object warnings do not replace the permission error

### 7.6 Invalid address remains a runtime error, not an object warning

Save as `phase37_invalid_address.asm`:

```asm
.code
main PROC
    mov esi, 0
    mov eax, DWORD PTR [esi]
main ENDP
END main
```

CMD:

```cmd
type phase37_invalid_address.asm | build\tests\diagnostic_json_producer.exe
```

PowerShell:

```powershell
Get-Content .\phase37_invalid_address.asm -Raw | .\build\tests\diagnostic_json_producer.exe
```

Linux shell:

```sh
./build/tests/diagnostic_json_producer < phase37_invalid_address.asm
```

Expected:

- JSON contains a runtime invalid-address diagnostic
- JSON does not contain `object-bounds-warning`
- JSON does not contain `execution-complete`

## 8. Rebuilding and serving the browser version

Use this when browser-visible behavior changed and Emscripten is available.

### Windows CMD

```cmd
scripts\windows\build_wasm.cmd
scripts\windows\serve_web.cmd
```

Stop the server:

```cmd
scripts\windows\stop_web.cmd
```

### Developer PowerShell

You may run CMD scripts from PowerShell:

```powershell
.\scripts\windows\build_wasm.cmd
.\scripts\windows\serve_web.cmd
```

Or use the PowerShell server helpers when available:

```powershell
.\scripts\windows\serve_web.ps1
```

Stop the server:

```powershell
.\scripts\windows\stop_web.ps1
```

### Linux shell

If executable bits were preserved:

```sh
./scripts/build_wasm.sh
python3 -m http.server 8000 --directory web
```

If executable bits were not preserved:

```sh
bash scripts/build_wasm.sh
python3 -m http.server 8000 --directory web
```

Then open:

```text
http://localhost:8000/
```

Stop the Python server with `Ctrl+C` in the terminal where it is running.

After serving, open the local URL printed by the script or server and run manual browser programs in the editor.

Milestone 37 note: allocated-object warning/strict validation began as a test/configuration-facing mode. After Phase 53E, the browser diagnostic settings panel exposes the same existing declared-object bounds warning and strict-stop policies as optional Memory range validation choices. Default browser execution remains region-only.

## 9. Troubleshooting

### `test_object_map.exe` exits with `3221225725`

On Windows this code is commonly `0xC00000FD`, a stack overflow. Ensure the current source has object-map test buffers in static storage rather than large local stack allocations.

### `fopen` or `getenv` is deprecated and fails under `-Werror`

When compiling against the Visual Studio/UCRT headers, standard C functions may be annotated as deprecated unless `_CRT_SECURE_NO_WARNINGS` is defined. The native diagnostic producer should define it before including CRT headers.

### Node cannot spawn the diagnostic producer

Set the producer path explicitly.

CMD:

```cmd
set MASM32_DIAGNOSTIC_JSON_PRODUCER=%CD%\build\tests\diagnostic_json_producer.exe
node tests\web\test_diagnostic_rendering.mjs
```

PowerShell:

```powershell
$env:MASM32_DIAGNOSTIC_JSON_PRODUCER = "$PWD\build\tests\diagnostic_json_producer.exe"
node tests\web\test_diagnostic_rendering.mjs
```

Linux shell:

```sh
MASM32_DIAGNOSTIC_JSON_PRODUCER="$PWD/build/tests/diagnostic_json_producer" node tests/web/test_diagnostic_rendering.mjs
```

### `emcc` is not found

Native and Node tests can still run, but Wasm/browser artifacts cannot be rebuilt. Activate emsdk first, then rerun the build script.

On Linux, this usually means the emsdk environment was not activated in the current shell. A typical setup uses commands similar to:

```sh
source /path/to/emsdk/emsdk_env.sh
emcc --version
```

### Linux cannot find `node`, `python3`, or `cc`

Install the missing package through the system package manager, then reopen the terminal or refresh the shell path. The aggregate runner expects `python3`, `node`, and a C compiler available on `PATH`.

### Linux build fails with permissions on scripts

Some archives may not preserve executable bits. Invoke scripts through their interpreter:

```sh
python3 scripts/run_tests.py
bash scripts/build_wasm.sh
```

## 10. Recommended checklist before reporting a milestone complete

1. Run the full aggregate test suite.
2. Run any new targeted native tests directly if they touched low-level C modules.
3. Run the Node formatter/diagnostic harness if diagnostic wording or JSON changed.
4. Run manual diagnostic-producer programs for new source-visible behavior.
5. Rebuild Wasm and manually test the browser if the feature is user-visible in the served site.
6. Report:
   - changed files;
   - tests added;
   - commands run;
   - pass/fail result;
   - limitations such as missing `emcc` or browser-invisible backend modes.

### Phase 53B section-boundary validation controls

The native diagnostic JSON producer also accepts these local test-only environment variables:

```text
MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION=off|warn|strict
MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION=off|warn|strict
```

`warn` emits `section-capacity-violation` or `section-image-violation` as a simulator warning and continues when Level 1 memory validation succeeds. `strict` emits the same code as a runtime error before mutation. Milestone 53B added these controls to the test/source-run path. Phase 53E exposes the same existing section-capacity and section-image policies in the browser Memory range validation setting. Defaults still leave section-capacity and section-image validation off unless the user selects one of those modes.
