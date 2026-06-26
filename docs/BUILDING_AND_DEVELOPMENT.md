# Building and Development

This file is the detailed build, local-serving, and development-environment reference for the Online MASM32 Educational Simulator. It was extracted from README-level guidance during Phase 57B so the README can remain a concise landing page.

Source-of-truth rule:

- [`FULL_IMPLEMENTATION_SPEC.md`](FULL_IMPLEMENTATION_SPEC.md) remains the canonical source for final product behavior, product boundaries, stable cross-cutting rules, permanent non-goals, and user-visible behavior contracts.
- [`INCREMENTAL_IMPLEMENTATION_GUIDE.md`](INCREMENTAL_IMPLEMENTATION_GUIDE.md) remains the canonical source for phase numbering, phase tasks, required tests, and acceptance criteria.
- [`TESTING_GUIDE.md`](TESTING_GUIDE.md) remains the detailed test-runner, fixture-inventory, timeout, and failure-reporting reference.
- This file is a development-operations guide. It does not define supported MASM syntax or runtime behavior.

## Current status

Current milestone:

- Phase 88 - Irvine32 WriteChar

Phase 88 advances runtime/source-run behavior metadata and the source-run output-contract identifier because source-run JSON can now contain public Irvine32 `WriteChar` Program Console character output copied from the low byte of `AL`. Phase 87 virtual Irvine32 `Crlf` Program Console newline output, Phase 86 deterministic Program Console byte/line limit fields and `console-output-limit-exceeded` diagnostics, Phase 85 separate Program Console stream infrastructure, Phase 84 limited same-file user-procedure `INVOKE` DWORD argument lowering with exact `ret imm16` cleanup validation, Phase 83 helper-level ADDR preparation, Phase 82 zero-argument INVOKE behavior, Phase 81 limited parser-owned `PROTO` metadata, Phase 80 LOCAL operand resolution/addressing, and Phase 79 automatic LOCAL frame behavior remain part of the public source-run behavior.

This file is a build and development reference. It does not define supported MASM syntax or runtime behavior. For current syntax and diagnostics, see [`SUPPORTED_SYNTAX.md`](SUPPORTED_SYNTAX.md). For product boundaries and stable behavior rules, see [`FULL_IMPLEMENTATION_SPEC.md`](FULL_IMPLEMENTATION_SPEC.md). For phase order and acceptance criteria, see [`INCREMENTAL_IMPLEMENTATION_GUIDE.md`](INCREMENTAL_IMPLEMENTATION_GUIDE.md). For milestone history, see [`MILESTONE_HISTORY.md`](MILESTONE_HISTORY.md).

## Artifact verification versus rebuild verification

Browser/Wasm artifact verification has separate levels:

1. **Source-level verification**

   Native C tests, source-run tests, Node protocol tests, rendered-message tests, and static documentation checks verify source files and checked-in JavaScript behavior. They do not prove that `web/dist` was rebuilt.

2. **Checked-in artifact-content verification**

   A binary-content scan of checked-in `web/dist/masm32_sim_core.wasm` may confirm that the artifact contains an expected output-contract string such as `phase-88-irvine32-writechar-contract-v1`. This is useful stale-artifact evidence, but it is not a rebuild.

3. **Emscripten rebuild verification**

   Running the Emscripten build script with `emcc` available proves that the current source can produce fresh `web/dist` artifacts in that environment.

4. **Browser/Wasm smoke verification**

   Serving the page and running a smoke program against the browser-loaded Wasm artifact verifies the served browser path. For Phase 70A and later compatibility checks, a successful browser/Wasm smoke should not show `stale-wasm-artifact` when the loaded artifact reports the exact expected runtime metadata, and should not show `stale-wasm-output-contract` when the loaded artifact reports the expected contract.

If `emcc` is unavailable, report rebuild verification as skipped. Do not infer that checked-in `web/dist` is stale solely from missing `emcc`. Inspect the checked-in artifact or run browser/Wasm smoke if artifact status matters.

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
docs/history/
docs/history/reports/
```

Core simulator code is C99 and lives under `src/core`, `src/parser`, and `src/wasm`. Browser code lives under `web/src`. Generated or rebuilt WebAssembly artifacts live under `web/dist`.

## Historical reports and archive paths

Milestone reports are stored under:

```text
docs/history/reports/
```

Curated audit and handoff reports are stored under:

```text
docs/history/
```

These files are historical evidence. They are useful when auditing assumptions, test coverage, skipped dependencies, and regression risks. They are not the active source of truth for current MASM syntax or runtime behavior.

For current behavior, use:

```text
docs/FULL_IMPLEMENTATION_SPEC.md
docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md
docs/SUPPORTED_SYNTAX.md
```

Moving or reorganizing historical reports is documentation organization only. It must not change runtime/source-run MASM behavior metadata.

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

Phase 71B1 provides official native and source-run subgroups when a broad native or source-run group is too large for the active environment:

```sh
python3 scripts/run_tests.py --native-parser
python3 scripts/run_tests.py --native-exec
python3 scripts/run_tests.py --native-memory-layout
python3 scripts/run_tests.py --native-diagnostics-policy
python3 scripts/run_tests.py --native-control-flow
python3 scripts/run_tests.py --source-run-core
python3 scripts/run_tests.py --source-run-diagnostics
python3 scripts/run_tests.py --source-run-settings
python3 scripts/run_tests.py --source-run-memory-layout
python3 scripts/run_tests.py --source-run-control-flow
```

Phase 71A1 also provides official diagnostic subgroups when the broad diagnostic group is too large for the active environment:

```sh
python3 scripts/run_tests.py --diagnostics-json
python3 scripts/run_tests.py --diagnostics-rendered-call-ret
python3 scripts/run_tests.py --diagnostics-rendered-memory
python3 scripts/run_tests.py --diagnostics-rendered-directives
python3 scripts/run_tests.py --diagnostics-rendered-compatibility
python3 scripts/run_tests.py --diagnostics-rendered-arithmetic
python3 scripts/run_tests.py --diagnostics-rendered-shift-rotate
python3 scripts/run_tests.py --diagnostics-rendered-mul-div
python3 scripts/run_tests.py --diagnostics-rendered-runtime
```

The source-run, native, and diagnostic subgroups preserve exact assertions from their broad suites. They are not smoke tests.

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

### Source-run verification in constrained environments

The aggregate test runner remains the normal full-suite verification command.

In hosted assistant/container environments, the aggregate command or full source-run group may time out even when focused groups pass. A timeout in that environment must be reported honestly and must not be described as a successful aggregate pass.

When the aggregate or full source-run group times out, run the smallest available focused groups or fixture families that cover the changed area. Report the exact groups run.

For procedure-entry, CALL, and future RET work, focused verification should include:

- structure/static checks;
- native executor tests when CALL/RET or stack tokens are involved;
- source-run tests for `END entryName` startup;
- rendered Simulator Messages tests for success and fatal runtime paths;
- branch/watchdog regressions when procedure entry changes instruction-pointer startup behavior.

Do not weaken exact structured diagnostic or rendered-message assertions merely to reduce runtime.

Use this report wording when applicable:

```text
aggregate timed out in assistant/container environment
focused groups passed:
- <group>
- <group>

aggregate success was not claimed
```

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

For current accepted syntax, rejected forms, and runtime behavior details, use [`SUPPORTED_SYNTAX.md`](SUPPORTED_SYNTAX.md). Browser/Wasm smoke checks should confirm that rebuilt artifacts run a small accepted program and that Program Console and Simulator Messages remain separated; they should not duplicate milestone-history prose in this build guide.


### Output-contract changes and browser/Wasm artifact status

Some corrective phases may change C/Wasm-facing source-run output behavior without changing accepted MASM syntax or VM execution semantics. Examples include diagnostic/status serialization order, source-run JSON ordering, final-state serialization details, or browser protocol metadata that depends on Wasm output.

When a phase changes C/Wasm-facing output behavior:

- native source-run tests verify the C source-run path;
- Node/web/protocol tests verify browser-side formatting and protocol expectations;
- neither of those proves that committed or served `web/dist` artifacts were rebuilt from the current C sources.

For those phases, the milestone report must explicitly state one of the following:

```text
Browser/Wasm artifacts rebuilt and smoke-tested with emcc.
```

or:

```text
Browser/Wasm artifact rebuild skipped because emcc was unavailable.
Served web/dist artifacts may require a manual rebuild before release/distribution.
```

or:

```text
Browser/Wasm artifact compatibility verified through the documented output-contract identifier.
```

The C source-run JSON field is `sourceRunOutputContract`. Its value is a source-run output-contract version token for the public source-run JSON shape, ordering, serialization, and protocol interpretation. A token may include the milestone in which that output contract was introduced. For example:

```text
phase-88-irvine32-writechar-contract-v1
```

The example above is both the token expected by this source tree and an example of the naming convention. A phase-looking prefix in such a token is contract-version naming from the phase that introduced that specific output contract. It is not a separate repository/runtime status field or an absolute value that future output-contract-changing phases must keep.

The browser protocol expects the loaded artifact to report the same token value as the current UI/source files. It renders a distinct `stale-wasm-output-contract` Simulator Messages warning when a loaded artifact omits the field or reports a different value. This warning is browser/protocol artifact-status metadata; it is not a MASM source diagnostic and does not change Program Console output. A later accepted phase that changes the public source-run JSON shape, ordering, serialization, or protocol interpretation must define and test a new output-contract identifier token.

Do not advance runtime/source-run MASM behavior phase metadata solely to detect output-only artifact staleness. Runtime/source-run behavior metadata describes implemented MASM syntax and VM semantics, not whether `web/dist` was rebuilt after a formatting, ordering, serialization, documentation, or test-infrastructure cleanup.

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
