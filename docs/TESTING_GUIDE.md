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

### Focused runner groups

The default command remains valid and is equivalent to full aggregate verification:

```sh
python3 scripts/run_tests.py
python3 scripts/run_tests.py --all
```

The same commands work on Windows with `py`:

```cmd
py scripts\run_tests.py
py scripts\run_tests.py --all
py scripts\run_tests.py --diagnostics
```

Focused groups are available for timeout-safe verification:

```sh
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
```

Group ownership:

- `structure`: repository structure, expected files, public header comments, Doxygen/static source-shape checks, and milestone metadata checks.
- `native`: native C unit/parser/executor/helper tests that do not require Node and do not run the source-run integration binary.
- `source-run`: native source-run JSON/integration coverage in `tests/core/test_wasm_source_run.c`. This group is intentionally runnable independently from native, web, protocol, static, and rendered diagnostic tests.
- `web`: browser-side Node module tests that do not require the native diagnostic producer.
- `diagnostics`: native diagnostic JSON producer build plus exact rendered Simulator Messages checks.
- `protocol`: worker/protocol schema tests separated from general web tests.
- `static`: runner help, group-name documentation, timeout-policy, failure-reporting, fixture-inventory consistency checks, and selected current-status or stale-wording guards.

Output controls:

```sh
python3 scripts/run_tests.py --quiet --source-run
python3 scripts/run_tests.py --verbose --diagnostics
```

`--quiet` prints group start/status lines, the final compact summary, and failure details only. `--verbose` prints subprocess commands, captured subprocess output, fixture-level details, milestone fixture names, and expected rendered diagnostic lines where the runner owns that inventory.

`--quick` is a smoke subset, not full verification:

```sh
python3 scripts/run_tests.py --quick
```

A milestone report that uses only `--quick` must say that full verification was not performed. Do not report `All implemented milestone tests passed` from a quick-only run.

Source-run subgroups such as `--source-run=memory-layout` are not currently implemented because the preserved source-run binary runs independently as a focused group. If `--source-run` later becomes too large for hosted assistant/container verification, the future test-runner decomposition maintenance owner should split it by behavior family, preferably memory/layout, instruction smoke, diagnostic policies, settings, and regressions.

Diagnostic subgroups such as `--diagnostics=memory` are not currently implemented because the diagnostic group runs independently after building only the native diagnostic JSON producer. If `--diagnostics` later risks timeout, the future test-runner decomposition maintenance owner should split rendered diagnostics by family, preferably memory, directives, compatibility, arithmetic, shift/rotate, and mul/div.

Failure output must identify the failing group, failing command, subprocess exit code, stdout/stderr tail, and any available fixture context. Source-run fixture failures include the fixture name through the source-run test binary's assertion context.

### Test-output current-coverage wording

Test binaries and runner static checks may include short human-readable coverage summaries, but those summaries must not become stale milestone ledgers.

When a test file begins covering a later implemented milestone, any success banner in that same test file must either:

- use neutral wording that does not name a specific old phase; or
- be updated to include the newest implemented behavior covered by that file.

For example, a source-run test binary that contains stack-startup fixtures must not print a success line implying that source-run coverage stops at an older arithmetic or branch phase. The matching `scripts/run_tests.py --static` assertion must be updated at the same time as the success banner.

Milestone history belongs in `docs/MILESTONE_HISTORY.md` and `docs/history/reports/`. Test success banners should communicate the current coverage surface of the binary, not preserve every historical milestone name.

### Assistant/container timeout policy

If `python3 scripts/run_tests.py --all` times out or output is truncated in a hosted assistant/container environment, this is not automatically a project test failure.

The assistant must rerun focused groups individually and report:

- which focused groups were run;
- which focused groups passed;
- which focused groups failed;
- which focused groups were skipped and why;
- whether any focused group required subgroup or fixture-level reruns;
- whether `--all` completed in that environment;
- whether the final local/user run produced `All implemented milestone tests passed.`

Milestone reports must distinguish:

```text
aggregate completed and passed
aggregate timed out in assistant/container environment, focused groups passed
aggregate failed with a real failing group
focused group failed
focused group timed out, subgroups or fixtures rerun
group skipped because dependency unavailable, such as emcc
```

An assistant must not claim that the full aggregate suite passed unless the aggregate command actually completed and returned the final success line in that environment.

An assistant may report focused verification only by naming the focused groups, subgroups, or fixtures that actually passed.

If `emcc` is unavailable, report browser/Wasm rebuild smoke as skipped because `emcc` is unavailable. This is separate from native, source-run, Node, protocol, static, and rendered diagnostic test failure.

### Source-run fixture inventory

The source-run fixture inventory is intentionally a navigation aid. It does not duplicate every assertion in `tests/core/test_wasm_source_run.c`. It records the major fixture families kept in the focused `source-run` group so future assistants do not split, weaken, or remove them without an explicit test-runner maintenance phase.

| Fixture or family | Focused group | Category | Current disposition |
| --- | --- | --- | --- |
| minimal source execution sample | `source-run` | focused success fixture | kept in the focused source-run group |
| lexer/parser diagnostic source-run failures | `source-run` | focused error fixture | kept in the focused source-run group |
| memory/layout and automatic layout programs | `source-run` | regression fixture | kept in the focused source-run group |
| uninitialized-read and memory-validation policy programs | `source-run` | warning/notice fixture | kept in the focused source-run group |
| phase51-layout-fixed-automatic-equivalence | `source-run` | integration smoke fixture | kept in the focused source-run group |
| phase51-const-permission-precedence | `source-run` | integration smoke fixture | kept in the focused source-run group |
| phase51-uninitialized-rmw-warning | `source-run` | integration smoke fixture | kept in the focused source-run group |
| phase51-inc-dec-source-smoke through phase51-ror-source-smoke | `source-run` | integration smoke fixture | kept in the focused source-run group |
| phase53e-ui-settings-policy-routing | `source-run` | focused settings fixture | kept in the focused source-run group |
| phase56-div-source-run-coverage | `source-run` | focused success/error/regression fixture family | kept in the focused source-run group |
| phase57-idiv-source-run-coverage | `source-run` | focused success/error/regression fixture family | kept in the focused source-run group |

Large individual MASM source fixtures remain intentionally grouped under `--source-run`. The Phase 51 instruction-family programs are intentionally labeled integration smoke fixtures. The remaining embedded source strings in `tests/core/test_wasm_source_run.c` are preserved as focused success, error, warning/notice, edge-case, or regression fixtures. No fixture should be moved, renamed, split, or weakened unless a future test-runner maintenance phase explicitly owns that change and updates this inventory.

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

For Milestone 56 unsigned `div` verification, run:

```asm
.code
main PROC
    mov edx, 0
    mov eax, 100
    mov ebx, 7
    div ebx
main ENDP
END main
```

Expected final state includes `EAX = 0000000Eh`, `EDX = 00000002h`, no memory-change rows, and an `execution-complete` Simulator Messages entry. Divide-by-zero and quotient-overflow programs should report runtime errors in Simulator Messages without updating the quotient or remainder registers.

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

### Phase 57 signed IDIV verification

Phase 57 adds runtime source behavior for signed one-operand `idiv`, so verification must include native executor tests, parser tests, source-run JSON tests, rendered Simulator Messages tests, protocol phase metadata tests, and static current-status checks. Memory-divisor `idiv` tests must prove planned-read validation runs before quotient/remainder mutation for strict uninitialized-read and object-boundary policies.

Recommended focused commands after Phase 57 changes:

```bash
python3 scripts/run_tests.py --structure
python3 scripts/run_tests.py --native
python3 scripts/run_tests.py --source-run
python3 scripts/run_tests.py --web
python3 scripts/run_tests.py --diagnostics
python3 scripts/run_tests.py --protocol
python3 scripts/run_tests.py --static
```

Run `python3 scripts/run_tests.py --all` when practical. If the aggregate command times out in a hosted assistant/container environment, report focused group results separately and do not claim an aggregate pass.

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


## Stack, CALL, RET, and procedure milestone verification note

Future stack/procedure milestones must separate focused executor tests from complete source-run programs.

Use executor/native tests when a phase implements an instruction whose full source-run program would require a later phase. Examples:

- Phase 69 - Direct CALL to User Procedures can be tested by stepping one CALL and checking the return token, stack write, instruction pointer, modeled flags, and no-partial-mutation behavior.
- Phase 70 - RET Execution and Return Address Validation can be tested with executor-level stack setup before Phase 71 - Root Procedure Termination Semantics exists.
- Phase 73 - LEAVE Instruction can be tested with direct frame setup even when source-level setup instructions are also available.

Use source-run tests only when all required source-level dependencies already exist. Examples:

- A complete CALL/RET source program requires both CALL and RET behavior and a defined termination path.
- A source program using `push ebp` requires source-level PUSH support.
- A source program using argument pushes before `ret imm16` requires source-level PUSH support.

Do not implement future instructions merely to make a source-run fixture pass. If a fixture depends on a future phase, either move that fixture to the owning future phase or mark it as an integration regression that becomes active only after the dependency phase is complete.

For stack/procedure diagnostics, tests must prove:

- structured diagnostic fields include code, severity, line, column, byte offset, and span length where source-backed;
- rendered Simulator Messages match expected wording;
- fatal stack/procedure failures do not emit `execution-complete`;
- no-partial-mutation behavior covers registers, modeled flags, flag-validity metadata, memory bytes, Program Console output, memory-change rows, stack pointer, instruction pointer, and call-depth metadata where applicable;
- current-status surfaces are advanced only by phases that actually change runtime/source-run MASM behavior.


## Current-status documentation clutter checks

Static or manual documentation checks should protect current-status surfaces from becoming milestone-history dumps.

The checks should apply to active current-status/current-scope sections in:

- `README.md`;
- `docs/SUPPORTED_SYNTAX.md` opening status block;
- `docs/BUILDING_AND_DEVELOPMENT.md`;
- browser/protocol status strings if they are stored in source files;
- tests that assert current-status wording.

Recommended checks:

1. README active current-status section must contain exactly one active `Repository/archive milestone:` label and one active `Runtime/source-run MASM behavior phase:` label.
2. If README contains quoted examples or templates with additional status labels, those must be clearly outside the active current-status section and should be avoided unless necessary.
3. README active current-status/current-scope text must not contain milestone-report headings such as:
   - `Exact requirements implemented`
   - `Assumptions used`
   - `TODO-style note disposition`
   - `Files changed`
   - `Tests added`
   - `Commands used to test`
4. README active current-status text must not contain a rolling sequence of several milestone paragraphs beginning with `Phase <N> adds`, `Phase <N> added`, `Phase <N> remains`, or `After Phase <N>`.
5. `docs/BUILDING_AND_DEVELOPMENT.md` must not define implemented MASM feature behavior beyond a short status note and links to the proper behavior documents.
6. `docs/SUPPORTED_SYNTAX.md` may contain detailed syntax sections, but its opening status block must remain concise and must not become a milestone ledger.
7. Detailed historical text removed from README must be preserved in `docs/MILESTONE_HISTORY.md` or milestone reports when it has historical value.
8. If automated static checks are deferred, the milestone report must list the manual checks performed and preserve static-check implementation as future documentation-maintenance work.

These checks should not forbid normal references to phase numbers in canonical guide sections, milestone history, milestone reports, or explicitly historical audit notes.

