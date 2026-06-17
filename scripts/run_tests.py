#!/usr/bin/env python3
"""
@file run_tests.py
@brief Runs command-line tests for the MASM32 simulator skeleton milestones.

The runner compiles C tests with the host C compiler and runs JavaScript
protocol tests with Node.js when available. It also performs lightweight
repository-structure checks for the implemented milestone skeleton.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Callable, Iterable, Sequence


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "tests"
REQUIRED_GROUPS = ["structure", "native", "source-run", "web", "diagnostics", "protocol", "static"]
NATIVE_SUBGROUPS = [
    "native-parser",
    "native-exec",
    "native-memory-layout",
    "native-diagnostics-policy",
    "native-control-flow",
]
SOURCE_RUN_SUBGROUPS = [
    "source-run-core",
    "source-run-diagnostics",
    "source-run-settings",
    "source-run-memory-layout",
    "source-run-control-flow",
]
DIAGNOSTIC_SUBGROUPS = [
    "diagnostics-json",
    "diagnostics-rendered-call-ret",
    "diagnostics-rendered-memory",
    "diagnostics-rendered-directives",
    "diagnostics-rendered-compatibility",
    "diagnostics-rendered-arithmetic",
    "diagnostics-rendered-shift-rotate",
    "diagnostics-rendered-mul-div",
    "diagnostics-rendered-runtime",
]
DIAGNOSTIC_RENDERING_GROUP_BY_SUBGROUP = {
    "diagnostics-rendered-call-ret": "rendered-call-ret",
    "diagnostics-rendered-memory": "rendered-memory",
    "diagnostics-rendered-directives": "rendered-directives",
    "diagnostics-rendered-compatibility": "rendered-compatibility",
    "diagnostics-rendered-arithmetic": "rendered-arithmetic",
    "diagnostics-rendered-shift-rotate": "rendered-shift-rotate",
    "diagnostics-rendered-mul-div": "rendered-mul-div",
    "diagnostics-rendered-runtime": "rendered-runtime",
}
MAX_FAILURE_TAIL_CHARS = 12000
VERBOSE_OUTPUT = False
QUIET_OUTPUT = False
CURRENT_GROUP = "unknown"
CURRENT_SUBGROUP = "n/a"
DIAGNOSTIC_JSON_PRODUCER_BUILT = False


class TestFailure(RuntimeError):
    """Raised when a command-line test step fails."""


@dataclass
class GroupResult:
    """Stores the status and detail line for one runner group."""

    name: str
    status: str
    details: str


def tail_text(text: str, limit: int = MAX_FAILURE_TAIL_CHARS) -> str:
    """Return a bounded tail of captured subprocess text.

    Args:
        text: Full captured subprocess output.
        limit: Maximum number of characters to return.

    Returns:
        The original text when short enough, otherwise a marked tail.
    """

    if len(text) <= limit:
        return text
    return f"[output truncated to last {limit} characters]\n" + text[-limit:]


def format_command(command: Sequence[str]) -> str:
    """Format a subprocess command for runner status output.

    Args:
        command: Command and arguments.

    Returns:
        A readable command string.
    """

    return " ".join(str(part) for part in command)


def run_command(
    command: list[str],
    *,
    cwd: pathlib.Path = ROOT,
    env: dict[str, str] | None = None,
    subgroup: str | None = None,
) -> None:
    """Run one command and raise TestFailure when it fails.

    Successful subprocess output is captured to keep default and quiet runs
    compact. Verbose mode prints commands and captured output. Failure output is
    always shown with group, subgroup, command, exit code, and bounded
    stdout/stderr tails.

    Args:
        command: Command and arguments to execute.
        cwd: Working directory for the subprocess.
        env: Optional environment variables for the subprocess.
        subgroup: Optional diagnostic subgroup name to report on failure.
    """

    command_text = format_command(command)
    active_subgroup = subgroup if subgroup is not None else CURRENT_SUBGROUP
    if VERBOSE_OUTPUT:
        print("$ " + command_text)

    completed = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    if VERBOSE_OUTPUT and completed.stdout:
        print(completed.stdout, end="")
    if VERBOSE_OUTPUT and completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)

    if completed.returncode != 0:
        details = [
            "Failure details:",
            f"- Group: {CURRENT_GROUP}",
            f"- Subgroup: {active_subgroup}",
            f"- Command: {command_text}",
            f"- Exit code: {completed.returncode}",
        ]
        if completed.stdout:
            details.extend(["- Stdout tail:", tail_text(completed.stdout).rstrip()])
        if completed.stderr:
            details.extend(["- Stderr tail:", tail_text(completed.stderr).rstrip()])
        raise TestFailure("\n".join(details))


def log_detail(message: str) -> None:
    """Print a message only in verbose output mode.

    Args:
        message: Message to print when verbose output is enabled.
    """

    if VERBOSE_OUTPUT:
        print(message)


def print_group_start(name: str) -> None:
    """Print the standardized start line for one group.

    Args:
        name: User-facing group name.
    """

    print(f"[{name}] START")


def print_group_status(name: str, status: str, details: str) -> None:
    """Print the standardized completion line for one group.

    Args:
        name: User-facing group name.
        status: Group status such as PASS or FAIL.
        details: Short human-readable detail line.
    """

    if QUIET_OUTPUT and status == "PASS":
        print(f"[{name}] {status}")
    else:
        print(f"[{name}] {status} - {details}")


def require_files(paths: Iterable[str]) -> None:
    """Verify that required Phase 0 files exist.

    Args:
        paths: Repository-relative paths expected to exist.
    """

    missing = [path for path in paths if not (ROOT / path).exists()]
    if missing:
        raise TestFailure("missing required files: " + ", ".join(missing))


def assert_text_contains(path: str, expected: str) -> None:
    """Verify that a repository file contains expected text.

    Args:
        path: Repository-relative file path.
        expected: Text expected inside the file.
    """

    text = (ROOT / path).read_text(encoding="utf-8")
    if expected not in text:
        raise TestFailure(f"{path} does not contain expected text: {expected}")



def assert_text_not_contains(path: str, unexpected: str) -> None:
    """Verify that a repository file does not contain unexpected text.

    Args:
        path: Repository-relative file path.
        unexpected: Text that must not appear inside the file.
    """

    text = (ROOT / path).read_text(encoding="utf-8")
    if unexpected in text:
        raise TestFailure(f"{path} contains unexpected text: {unexpected}")



def assert_text_order(path: str, first: str, second: str) -> None:
    """Verify that one text fragment appears before another in a repository file.

    Args:
        path: Repository-relative file path.
        first: Text that must appear first.
        second: Text that must appear after the first text.
    """

    text = (ROOT / path).read_text(encoding="utf-8")
    first_index = text.find(first)
    second_index = text.find(second)
    if first_index < 0:
        raise TestFailure(f"{path} does not contain expected text: {first}")
    if second_index < 0:
        raise TestFailure(f"{path} does not contain expected text: {second}")
    if first_index >= second_index:
        raise TestFailure(f"{path} has incorrect text order: {first} must appear before {second}")

def run_structure_tests() -> None:
    """Run static structure checks for the Phase 0 skeleton."""

    require_files([
        "src/core/masm32_sim_api.c",
        "src/core/masm32_sim_api.h",
        "src/wasm/wasm_api.c",
        "src/wasm/wasm_api.h",
        "src/core/vm_cpu.c",
        "src/core/vm_cpu.h",
        "src/core/vm_memory.c",
        "src/core/vm_memory.h",
        "src/core/vm_layout.c",
        "src/core/vm_layout.h",
        "src/core/vm_ir.c",
        "src/core/vm_ir.h",
        "src/core/vm_exec.c",
        "src/core/vm_exec.h",
        "src/core/vm_diagnostic_policy.c",
        "src/core/vm_diagnostic_policy.h",
        "src/parser/lexer.c",
        "src/parser/lexer.h",
        "src/parser/parser.c",
        "src/parser/parser.h",
        "src/parser/symbols.c",
        "src/parser/symbols.h",
        "src/parser/object_map.c",
        "src/parser/object_map.h",
        "docs/SUPPORTED_SYNTAX.md",
        "web/index.html",
        "web/src/main.js",
        "web/src/worker.js",
        "web/src/protocol.js",
        "web/src/settings.js",
        "scripts/build_wasm.sh",
        "scripts/windows/build_wasm.cmd",
        "scripts/windows/clean_wasm.cmd",
        "scripts/windows/serve_web.cmd",
        "scripts/windows/stop_web.cmd",
        "scripts/windows/serve_web.ps1",
        "scripts/windows/stop_web.ps1",
        "tests/core/test_milestone_zero.c",
        "tests/core/test_vm_cpu.c",
        "tests/core/test_vm_flags.c",
        "tests/core/test_vm_memory.c",
        "tests/core/test_vm_layout.c",
        "tests/core/test_diagnostic_policy.c",
        "tests/core/test_vm_exec.c",
        "tests/core/test_lexer.c",
        "tests/core/test_parser.c",
        "tests/core/test_wasm_source_run.c",
        "tests/core/test_data_section.c",
        "tests/core/test_object_map.c",
        "tests/web/test_protocol.mjs",
        "tests/web/test_settings.mjs",
        "tests/web/test_collapsible_settings.mjs",
        "tests/web/test_formatters.mjs",
        "tests/web/test_diagnostic_rendering.mjs",
        "tests/core/diagnostic_json_producer.c",
    ])
    assert_text_contains("web/src/protocol.js", "type: \"PONG\"")
    assert_text_contains("web/src/protocol.js", "unsupported-message")
    assert_text_contains("scripts/build_wasm.sh", "emcc")
    assert_text_contains("scripts/build_wasm.sh", "-std=c99")
    assert_text_contains("scripts/build_wasm.sh", "masm32_sim_api.c")
    assert_text_contains("scripts/build_wasm.sh", "wasm_api.c")
    assert_text_contains("scripts/build_wasm.sh", "vm_cpu.c")
    assert_text_contains("scripts/build_wasm.sh", "vm_memory.c")
    assert_text_contains("scripts/build_wasm.sh", "vm_layout.c")
    assert_text_contains("scripts/build_wasm.sh", "vm_ir.c")
    assert_text_contains("scripts/build_wasm.sh", "vm_exec.c")
    assert_text_contains("scripts/build_wasm.sh", "vm_diagnostic_policy.c")
    assert_text_contains("scripts/build_wasm.sh", "lexer.c")
    assert_text_contains("scripts/build_wasm.sh", "parser.c")
    assert_text_contains("scripts/build_wasm.sh", "symbols.c")
    assert_text_contains("scripts/build_wasm.sh", "object_map.c")
    assert_text_contains("scripts/build_wasm.sh", "src/parser")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_milestone4_hardcoded_result")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_run_source_json")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_run_source_json_with_ui_settings")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_run_source_json_with_ui_and_startup_settings")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings")
    assert_text_contains("scripts/windows/build_wasm.cmd", "EMSDK_ROOT")
    assert_text_contains("scripts/windows/build_wasm.cmd", "emsdk_env.bat")
    assert_text_contains("scripts/windows/build_wasm.cmd", "-std=c99")
    assert_text_contains("scripts/windows/build_wasm.cmd", "web\\dist")
    assert_text_contains("scripts/windows/build_wasm.cmd", "masm32_sim_core.js")
    assert_text_contains("scripts/windows/build_wasm.cmd", "masm32_sim_api.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "wasm_api.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "vm_cpu.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "vm_memory.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "vm_layout.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "vm_ir.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "vm_exec.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "vm_diagnostic_policy.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "lexer.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "parser.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "symbols.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "object_map.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "src\\parser")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_milestone4_hardcoded_result")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_run_source_json")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_run_source_json_with_ui_settings")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_run_source_json_with_ui_and_startup_settings")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_run_source_json_with_ui_startup_storage_instruction_limit_and_root_ret_settings")
    assert_text_contains("scripts/windows/clean_wasm.cmd", "masm32_sim_core.wasm")
    assert_text_contains("scripts/windows/serve_web.cmd", "serve_web.ps1")
    assert_text_contains("scripts/windows/serve_web.ps1", "http.server")
    assert_text_contains("scripts/windows/serve_web.ps1", "dev-server.pid")
    assert_text_contains("scripts/windows/serve_web.ps1", "dev-server.port")
    assert_text_contains("scripts/windows/serve_web.ps1", "Start-Process")
    assert_text_contains("scripts/windows/serve_web.ps1", "-NoNewWindow")
    assert_text_contains("scripts/windows/serve_web.ps1", "WaitForExit")
    assert_text_contains("scripts/windows/serve_web.ps1", "--directory")
    assert_text_not_contains("scripts/windows/serve_web.ps1", "WindowStyle")
    assert_text_not_contains("scripts/windows/serve_web.ps1", "RedirectStandardOutput")
    assert_text_not_contains("scripts/windows/serve_web.ps1", "RedirectStandardError")
    assert_text_contains("scripts/windows/stop_web.cmd", "stop_web.ps1")
    assert_text_contains("scripts/windows/stop_web.ps1", "dev-server.pid")
    assert_text_contains("scripts/windows/stop_web.ps1", "dev-server.port")
    assert_text_contains("scripts/windows/stop_web.ps1", "Stop-Process")
    assert_text_contains("scripts/windows/stop_web.ps1", "Get-NetTCPConnection")
    assert_text_contains("scripts/windows/stop_web.ps1", "http.server")
    assert_text_not_contains("scripts/windows/stop_web.ps1", "taskkill /IM python")
    assert_text_contains("web/src/worker.js", "../dist/masm32_sim_core.js")
    assert_text_contains("web/src/worker.js", "_masm32_sim_wasm_test_value")
    assert_text_contains("web/src/worker.js", "_masm32_sim_wasm_run_source_json")
    assert_text_contains("web/src/protocol.js", "RUN_SOURCE")
    assert_text_contains("web/src/protocol.js", "RUN_RESULT")
    assert_text_contains("web/src/settings.js", "DEFAULT_DIAGNOSTIC_SETTINGS")
    assert_text_contains("web/src/settings.js", "normalizeDiagnosticSettings")
    assert_text_contains("web/src/formatters.js", "formatRegisters")
    assert_text_contains("web/src/formatters.js", "formatSimulatorMessages")
    assert_text_contains("web/src/formatters.js", "formatMemoryChanges")
    assert_text_contains("web/src/formatters.js", "byte offset")
    assert_text_contains("web/src/formatters.js", "element index")
    assert_text_contains("web/src/main.js", "./formatters.js")
    assert_text_contains("web/index.html", "run-button")
    assert_text_contains("web/index.html", "final-registers")
    assert_text_contains("web/index.html", "memory-changes")
    assert_text_contains('web/src/main.js', 'new URL("./worker.js", import.meta.url)')
    assert_text_contains("web/src/main.js", "Worker failed to load or crashed before reporting a structured error.")
    assert_text_order("web/index.html", "Simulator Messages", "Program Console")
    assert_text_not_contains("src/core/masm32_sim_api.h", "__cplusplus")
    assert_text_not_contains("src/wasm/wasm_api.h", "__cplusplus")
    assert_text_not_contains("src/core/vm_cpu.h", "__cplusplus")
    assert_text_not_contains("src/core/vm_memory.h", "__cplusplus")
    assert_text_not_contains("src/core/vm_layout.h", "__cplusplus")
    assert_text_not_contains("src/core/vm_ir.h", "__cplusplus")
    assert_text_not_contains("src/core/vm_exec.h", "__cplusplus")
    assert_text_not_contains("src/core/vm_diagnostic_policy.h", "__cplusplus")
    assert_text_not_contains("src/parser/lexer.h", "__cplusplus")
    assert_text_not_contains("src/parser/parser.h", "__cplusplus")
    assert_text_not_contains("src/parser/symbols.h", "__cplusplus")
    assert_text_contains("src/core/vm_cpu.h", "/*\n * @file vm_cpu.h")
    assert_text_contains("src/core/vm_cpu.c", "/*\n * @file vm_cpu.c")
    assert_text_contains("src/core/vm_memory.h", "/*\n * @file vm_memory.h")
    assert_text_contains("src/core/vm_memory.c", "/*\n * @file vm_memory.c")
    assert_text_contains("src/core/vm_layout.h", "/*\n * @file vm_layout.h")
    assert_text_contains("src/core/vm_layout.c", "/*\n * @file vm_layout.c")
    assert_text_contains("src/core/vm_ir.h", "/*\n * @file vm_ir.h")
    assert_text_contains("src/core/vm_ir.c", "/*\n * @file vm_ir.c")
    assert_text_contains("src/core/vm_exec.h", "/*\n * @file vm_exec.h")
    assert_text_contains("src/core/vm_exec.c", "/*\n * @file vm_exec.c")
    assert_text_contains("src/core/vm_diagnostic_policy.h", "/*\n * @file vm_diagnostic_policy.h")
    assert_text_contains("src/core/vm_diagnostic_policy.c", "/*\n * @file vm_diagnostic_policy.c")
    assert_text_contains("src/parser/lexer.h", "/*\n * @file lexer.h")
    assert_text_contains("src/parser/lexer.c", "/*\n * @file lexer.c")
    assert_text_contains("src/parser/parser.h", "/*\n * @file parser.h")
    assert_text_contains("src/parser/parser.c", "/*\n * @file parser.c")
    assert_text_contains("src/parser/symbols.h", "/*\n * @file symbols.h")
    assert_text_contains("src/parser/symbols.c", "/*\n * @file symbols.c")
    assert_text_contains("src/parser/object_map.h", "/*\n * @file object_map.h")
    assert_text_contains("src/parser/object_map.c", "/*\n * @file object_map.c")
    assert_text_contains("src/core/vm_cpu.h", "/// Identifies a canonical MASM32 register")
    assert_text_contains("src/core/vm_cpu.h", "/// Identifies one named EFLAGS bit")
    assert_text_contains("src/core/vm_cpu.h", "/// Stores canonical 32-bit MASM32 CPU registers")
    assert_text_contains("src/core/vm_cpu.h", "bool vm_cpu_read_register")
    assert_text_contains("src/core/vm_cpu.h", "bool vm_cpu_read_flag")
    assert_text_contains("src/core/vm_cpu.h", "bool vm_cpu_update_add_flags")
    assert_text_contains("src/core/vm_cpu.h", "bool vm_cpu_update_sub_flags")
    assert_text_contains("src/core/vm_cpu.h", "bool vm_cpu_update_cmp_flags")
    assert_text_contains("src/core/vm_layout.h", "/// Describes the selected VM memory layout policy")
    assert_text_contains("src/core/vm_layout.h", "VmLayoutPolicy vm_layout_default_policy")
    assert_text_contains("src/core/vm_layout.c", "/// Initializes maximum-size values")
    assert_text_contains("src/core/vm_memory.h", "/// Identifies one deterministic simulated memory region")
    assert_text_contains("src/core/vm_memory.h", "/// Describes one raw byte change recorded")
    assert_text_contains("src/core/vm_memory.h", "VmMemoryStatus vm_memory_read_u32")
    assert_text_contains("src/core/vm_memory.h", "VmMemoryStatus vm_memory_write_u64")
    assert_text_contains("src/core/vm_memory.h", "VmMemoryStatus vm_memory_init_with_layout_policy")
    assert_text_contains("src/core/vm_memory.c", "/// Validates a checked access before bytes")
    assert_text_contains("src/core/vm_cpu.c", "/// Describes how a public register identifier maps")
    assert_text_contains("src/core/vm_cpu.c", "/// Describes the EFLAGS bit represented")
    assert_text_contains("src/core/vm_diagnostic_policy.h", "/// Identifies one common policy value")
    assert_text_contains("src/core/vm_diagnostic_policy.h", "VmDiagnosticPolicyFamilyInfo")
    assert_text_contains("src/core/vm_diagnostic_policy.h", "bool vm_diagnostic_policy_parse_value")
    assert_text_contains("src/core/vm_diagnostic_policy.h", "bool vm_diagnostic_policy_parse_family")
    assert_text_contains("src/core/vm_diagnostic_policy.c", "VM_DIAGNOSTIC_POLICY_FAMILY_TABLE")
    assert_text_contains("tests/core/test_diagnostic_policy.c", "/*\n * @file test_diagnostic_policy.c")
    assert_text_contains("tests/core/test_diagnostic_policy.c", "Diagnostic policy registry, migration, startup notice, and const-uninitialized-storage tests passed.")
    assert_text_contains("tests/core/test_vm_flags.c", "/*\n * @file test_vm_flags.c")
    assert_text_contains("tests/core/test_vm_flags.c", "/// Verifies success-path named flag")
    assert_text_contains("tests/core/test_vm_memory.c", "/*\n * @file test_vm_memory.c")
    assert_text_contains("tests/core/test_vm_memory.c", "/// Verifies default region layout")
    assert_text_contains("tests/core/test_vm_layout.c", "/*\n * @file test_vm_layout.c")
    assert_text_contains("tests/core/test_vm_layout.c", "/// Verifies default policy metadata")
    assert_text_contains("src/core/vm_ir.h", "/// Identifies the currently implemented IR operation code")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPERAND_MEMORY_REGISTER")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_MOVSX")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_MOVZX")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_CBW")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_XCHG")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_NEG")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_NOP")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_TEST")
    assert_text_contains("src/core/vm_ir.h", "VmIrOperand vm_ir_operand_memory_register")
    assert_text_contains("src/core/vm_ir.h", "VmIrInstruction vm_ir_instruction")
    assert_text_contains("src/core/vm_exec.h", "/// Owns CPU, memory")
    assert_text_contains("src/core/vm_exec.h", "VM_EXEC_MAX_MEMORY_ACCESSES")
    assert_text_contains("src/core/vm_exec.h", "VmExecMemoryAccess")
    assert_text_contains("src/core/vm_exec.h", "VmExecStatus vm_step")
    assert_text_contains("src/core/vm_exec.h", "VmExecStatus vm_init_with_layout_policy")
    assert_text_contains("src/core/vm_exec.c", "/// Executes one already-fetched instruction")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_movx")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_cbw")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_cdq")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_xchg")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_neg")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_nop")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_test")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_inc_dec")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_not")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_milestone4_hardcoded_result")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_run_source_json")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_run_source_json_with_ui_settings")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_run_source_json_with_ui_and_startup_settings")
    assert_text_contains("src/wasm/wasm_api.h", "Masm32SimWasmMemoryValidationMode")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_run_source_json_with_memory_validation_mode")
    assert_text_contains("src/wasm/wasm_api.c", "/// Appends compact JSON")
    assert_text_contains("tests/core/test_vm_exec.c", "/*\n * @file test_vm_exec.c")
    assert_text_contains("tests/core/test_vm_exec.c", "/// Verifies that the hardcoded Milestone 4 sample")
    assert_text_contains("src/parser/lexer.h", "/// Identifies one token emitted by the MASM-like lexer")
    assert_text_contains("src/parser/lexer.h", "VmLexerStatus vm_lexer_tokenize")
    assert_text_contains("src/parser/lexer.h", "VM_LEXER_TOKEN_PLUS")
    assert_text_contains("src/parser/lexer.h", "VM_LEXER_TOKEN_MINUS")
    assert_text_contains("src/parser/lexer.h", "VM_LEXER_TOKEN_ASTERISK")
    assert_text_contains("src/parser/lexer.c", "/// Tracks the current scan position")
    assert_text_contains("tests/core/test_lexer.c", "/*\n * @file test_lexer.c")
    assert_text_contains("tests/core/test_lexer.c", "/// Verifies the guide.s representative lexer source snippet")
    assert_text_contains("src/parser/parser.h", "/// Identifies the final status of one parser attempt")
    assert_text_contains("src/parser/parser.h", "VmParserStatus vm_parser_parse_program")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_SYMBOL_OFFSET_OUT_OF_RANGE")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_UNSUPPORTED_PTR_WIDTH")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_UNSUPPORTED_SCALED_INDEX")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_UNSUPPORTED_FEATURE")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_UNSUPPORTED_TYPE_EXPRESSION")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_UNSUPPORTED_LENGTHOF_EXPRESSION")
    assert_text_contains("src/parser/parser.c", "Unsupported feature: STRUCT declarations are not supported yet.")
    assert_text_contains("src/parser/parser.c", "INVOKE syntax is not implemented in MASM32 Educational Mode")
    assert_text_contains("src/parser/parser.c", "Unsupported feature: MASM macro definitions are not supported yet.")
    assert_text_contains("README.md", "Phase 77 - PROC USES Runtime Save/Restore")
    assert_text_contains("README.md", "Phase 77 executes supported direct `CALL` entry into `PROC USES` procedures")
    assert_text_contains("README.md", "callDepthLimit")
    assert_text_contains("README.md", "selected-entry source-run startup from `END entryName`")
    assert_text_not_contains("README.md", "- `leave` and `ret imm16`;")
    assert_text_not_contains("README.md", "- `ret imm16`;")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "Diagnostic recovery behavior")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "Recognized unsupported features")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "SBYTE")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "movsx")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "movzx")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "cbw")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "nop")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "neg")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "xchg")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "adc")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "sbb")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "clc")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "stc")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "cmc")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "test")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "inc")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "dec")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "not")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "SBYTE PTR")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "SWORD PTR")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "SDWORD PTR")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "Expression parser expansion")
    assert_text_contains("src/parser/parser.c", "WIDTH PTR")
    assert_text_contains("src/parser/parser.c", "TYPE symbol")
    assert_text_contains("src/parser/parser.c", "LENGTHOF symbol")
    assert_text_contains("src/parser/parser.c", "movsx")
    assert_text_contains("src/parser/parser.c", "movzx")
    assert_text_contains("src/parser/parser.c", "cbw")
    assert_text_contains("src/parser/parser.c", "nop")
    assert_text_contains("src/parser/parser.c", "neg")
    assert_text_contains("src/parser/parser.c", "xchg")
    assert_text_contains("src/parser/parser.c", "adc")
    assert_text_contains("src/parser/parser.c", "sbb")
    assert_text_contains("src/parser/parser.c", "clc")
    assert_text_contains("src/parser/parser.c", "stc")
    assert_text_contains("src/parser/parser.c", "cmc")
    assert_text_contains("src/parser/parser.c", "test")
    assert_text_contains("src/parser/parser.c", "inc")
    assert_text_contains("src/parser/parser.c", "dec")
    assert_text_contains("src/parser/parser.c", "not")
    assert_text_contains("src/parser/parser.c", "ambiguous-memory-width")
    assert_text_contains("src/parser/symbols.h", "/// Describes one data symbol")
    assert_text_contains("src/parser/symbols.h", "bool vm_symbol_parse_data_type")
    assert_text_contains("src/parser/object_map.h", "/// Describes one declared data object after layout selection")
    assert_text_contains("src/parser/object_map.h", "VmObjectMapStatus vm_object_map_build_from_symbols")
    assert_text_contains("src/parser/object_map.h", "VmObjectMapStatus vm_object_map_find_by_range")
    assert_text_contains("src/parser/object_map.h", "VmObjectMapStatus vm_object_map_classify_range")
    assert_text_contains("src/parser/object_map.h", "VM_OBJECT_INITIALIZATION_ORIGIN_TRACKED")
    assert_text_contains("src/parser/object_map.h", "VmObjectMapStatus vm_object_map_build_from_symbols_with_initialization_mask")
    assert_text_contains("src/parser/object_map.h", "/// Classifies how one full access range relates to declared objects and valid regions")
    assert_text_contains("src/parser/object_map.c", "/// Writes one object-map entry from one symbol")
    assert_text_contains("src/parser/object_map.c", "/// Finds the selected layout region that wholly contains one inclusive range")
    assert_text_contains("src/wasm/wasm_api.c", "object-bounds-warning")
    assert_text_contains("src/wasm/wasm_api.c", "object-bounds-violation")
    assert_text_contains("src/wasm/wasm_api.c", "/// Validates one memory access against the allocated-object mode")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_run_source_json_with_uninitialized_metadata")
    assert_text_contains("src/wasm/wasm_api.c", "uninitializedOrigin")
    assert_text_contains("src/wasm/wasm_api.c", "uninitializedByteCount")
    assert_text_contains("src/wasm/wasm_api.c", "/// Marks successful writes from the last executed instruction as initialized")
    assert_text_contains("src/parser/parser.c", "/// Owns mutable parser state")
    assert_text_contains("tests/core/test_parser.c", "/*\n * @file test_parser.c")
    assert_text_contains("tests/core/test_parser.c", "/// Verifies that the guide's minimal program parses")
    assert_text_contains("tests/core/test_parser.c", "diagnostic byte offset should be preserved")
    assert_text_contains("tests/core/test_wasm_source_run.c", "/*\n * @file test_wasm_source_run.c")
    assert_text_contains("tests/core/test_wasm_source_run.c", "/// Verifies that the guide's minimal source execution sample")
    assert_text_contains("tests/core/test_data_section.c", "/*\n * @file test_data_section.c")
    assert_text_contains("tests/core/test_data_section.c", "/// Verifies Milestone 15 data layout")
    assert_text_contains("tests/core/test_data_section.c", "/// Verifies register-indirect memory operands")
    assert_text_contains("tests/core/test_data_section.c", "/// Verifies TYPE symbol emits declared element-size immediates")
    assert_text_contains("tests/core/test_data_section.c", "/// Verifies LENGTHOF symbol emits element-count immediates")
    assert_text_contains("tests/core/test_data_section.c", "/// Verifies Milestone 30 nested DUP expansion")
    assert_text_contains("tests/core/test_object_map.c", "/*\n * @file test_object_map.c")
    assert_text_contains("tests/core/test_object_map.c", "/// Verifies scalar, array, nested DUP, .DATA?, and .CONST object metadata")
    assert_text_contains("tests/core/test_object_map.c", "/// Verifies full-range classification covers Phase 36 object-map categories")
    assert_text_contains("tests/core/test_object_map.c", "/// Verifies Phase 39 object maps track per-object initialized and uninitialized byte counts")
    assert_text_contains("tests/core/test_wasm_source_run.c", "/// Verifies explicit region-only mode preserves Phase 39 zero-filled reads without warnings or metadata output")
    assert_text_contains("web/src/formatters.js", "/*\n * @file formatters.js")
    assert_text_contains("web/src/protocol.js", "IMPLEMENTED_PHASE = 77")
    assert_text_contains("web/src/protocol.js", "IMPLEMENTED_PHASE_SUFFIX = \"\"")
    assert_text_contains("web/src/protocol.js", "Phase 77 - PROC USES Runtime Save/Restore")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_INC")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_DEC")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_AND")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_OR")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_XOR")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_NOT")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_SHL")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_SAL")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_SHR")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_ROL")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_ROR")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_LEA")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_MUL")
    assert_text_contains("src/core/vm_ir.h", "VM_IR_OPCODE_EXIT")
    assert_text_contains("src/core/vm_cpu.h", "VmFlagValidityMetadata")
    assert_text_contains("src/core/vm_cpu.h", "producer_source_column")
    assert_text_contains("src/core/vm_cpu.h", "producer_byte_offset")
    assert_text_contains("src/core/vm_cpu.h", "producer_span_length")
    assert_text_contains("src/core/vm_cpu.h", "vm_cpu_mark_flag_undefined")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_mark_flag_undefined_from_instruction")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_shift_right")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_rotate_left")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_rotate_right")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_exit")
    assert_text_contains("src/core/vm_exec.c", "vm_exec_execute_mul")
    assert_text_contains("src/parser/parser.c", "Unknown instruction or virtual Irvine32 terminator. Add INCLUDE Irvine32.inc to use exit.")
    assert_text_contains("tests/core/test_parser.c", "test_phase42_irvine32_exit_terminator_parser_paths")
    assert_text_contains("tests/core/test_parser.c", "test_phase43_inc_dec_parse_to_ir")
    assert_text_contains("tests/core/test_vm_exec.c", "test_inc_dec_register_flags_and_carry_preservation")
    assert_text_contains("tests/core/test_vm_exec.c", "test_exit_terminator_halts_without_mutation")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase42_irvine32_exit_terminator_source_run")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase43_inc_dec_register_source_run_program")
    assert_text_contains("tests/core/test_parser.c", "test_phase44_logical_binary_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase45_not_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase46_shift_left_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase47_shr_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase48_sar_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase49_rol_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase50_ror_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase52_lea_parse_to_ir")
    assert_text_contains("tests/core/test_parser.c", "test_phase53_mul_parse_to_ir")
    assert_text_contains("tests/core/test_vm_exec.c", "test_logical_binary_register_flags")
    assert_text_contains("tests/core/test_vm_exec.c", "test_not_register_destinations_preserve_flags")
    assert_text_contains("tests/core/test_vm_exec.c", "test_shift_left_register_flags_and_counts")
    assert_text_contains("tests/core/test_vm_exec.c", "test_shift_right_register_flags_and_counts")
    assert_text_contains("tests/core/test_vm_exec.c", "test_rotate_left_register_flags_and_counts")
    assert_text_contains("tests/core/test_vm_exec.c", "test_rotate_right_register_flags_and_counts")
    assert_text_contains("tests/core/test_vm_exec.c", "test_phase50a_shift_flag_validity_metadata")
    assert_text_contains("tests/core/test_vm_exec.c", "test_phase50a_rotate_flag_validity_metadata")
    assert_text_contains("tests/core/test_vm_exec.c", "test_phase50a_defined_and_preserved_flag_validity_metadata")
    assert_text_contains("tests/core/test_vm_exec.c", "test_phase50a_flag_validity_rollback_and_preservation")
    assert_text_contains("src/core/vm_exec.h", "VmUndefinedFlagUsePolicy")
    assert_text_contains("src/core/vm_exec.c", "vm_check_flag_consumption")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_run_source_json_with_undefined_flag_use_policy")
    assert_text_contains("tests/core/test_vm_exec.c", "test_phase50b_flag_use_helper_policies")
    assert_text_contains("tests/core/test_vm_exec.c", "test_phase52_lea_effective_address_execution")
    assert_text_contains("tests/core/test_vm_exec.c", "test_phase53_mul_register_semantics")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase50b_undefined_flag_use_warn_policy_source_run")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase52_lea_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53_mul_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53_mul_uninitialized_memory_source_warning")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase57f_seeded_startup_is_deterministic")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase57g_seeded_uninitialized_storage_randomizes_only_uninitialized_bytes")
    assert_text_contains("tests/core/test_vm_cpu.c", "test_seeded_register_flag_startup_is_deterministic")
    assert_text_contains("src/core/vm_cpu.h", "vm_cpu_init_seeded_registers_and_flags")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase51_fixed_and_automatic_layout_smoke_harness")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase51_instruction_family_source_run_smoke_harness")
    assert_text_contains("tests/core/test_wasm_source_run.c", "Source execution tests through Phase 77 PROC USES runtime save/restore behavior passed.")
    assert_text_contains("src/wasm/wasm_api.h", "Masm32SimWasmSectionValidationPolicy")
    assert_text_contains("src/wasm/wasm_api.h", "masm32_sim_wasm_run_source_json_with_section_validation_modes")
    assert_text_contains("src/wasm/wasm_api.c", "section-capacity-violation")
    assert_text_contains("src/wasm/wasm_api.c", "section-image-violation")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53b_section_image_warning_mode_continues")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53b_data_section_capacity_warning_mode_continues")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53b_data_section_capacity_strict_mode_stops_before_mutation")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53b_section_capacity_strict_mode_stops_before_mutation")
    assert_text_contains("tests/core/diagnostic_json_producer.c", "MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION")
    assert_text_contains("tests/core/diagnostic_json_producer.c", "MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "Phase 53B renders section-image warning exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "Phase 53B renders section-capacity strict violation exactly")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53c_default_uninitialized_read_warns_and_continues")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53c_default_undefined_flag_use_warns_and_continues")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "Phase 53C renders default uninitialized-read warning exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "Phase 53C renders default undefined flag-use warning exactly")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53d_default_compatibility_notices_continue_execution")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53d_notice_plus_error_still_blocks_execution")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase53e_ui_settings_route_to_existing_policies")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "Phase 53D renders compatibility notices exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "Phase 51 renders instruction-family diagnostic smoke lines exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "runPhase51RenderedDiagnosticSmoke")
    assert_text_contains("scripts/run_tests.py", "report_phase51_smoke_harness_status")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders undefined flag-use warning exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders undefined flag-use runtime error exactly")
    assert_text_contains("tests/core/diagnostic_json_producer.c", "MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE")
    assert_text_contains("tests/core/test_vm_cpu.c", "test_flag_validity_metadata_helpers")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase44_logical_binary_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase45_not_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase46_shift_left_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase47_shr_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase48_sar_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase49_rol_source_run_program")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase50_ror_source_run_program")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders AND ambiguous memory width diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders NOT ambiguous memory-width diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders SHL ambiguous memory-width diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders SHR ambiguous memory-width diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders SAR ambiguous memory-width diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders ROL undefined modeled flag warning exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders successful ROR execution exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders ROR undefined modeled flag warning exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders ROR invalid destination-address diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders MUL ambiguous memory-width diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders INC ambiguous memory width diagnostic exactly")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "renders exit without Irvine32 include diagnostic exactly")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_UNSUPPORTED_IRVINE32_ROUTINE")
    assert_text_contains("src/parser/parser.h", "VmIrvine32SymbolClass")
    assert_text_contains("src/parser/parser.c", "VM_PARSER_IRVINE32_REGISTRY")
    assert_text_contains("src/parser/parser.c", "unsupported-irvine32-routine")
    assert_text_contains("tests/core/test_parser.c", "test_phase41_virtual_irvine32_include_records_registry")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "unsupported-irvine32-routine")
    assert_text_contains("web/src/formatters.js", "Formats final registers returned by the worker, including supported aliases")
    assert_text_contains("web/src/formatters.js", "formatIntegerDisplay")
    assert_text_contains("tests/web/test_formatters.mjs", "formats Phase 52A signed integer boundary values")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "Phase 52A formats source-run register signed display from existing JSON")
    assert_text_contains("tests/web/test_formatters.mjs", "/*\n * @file test_formatters.mjs")
    assert_text_contains("tests/web/test_diagnostic_rendering.mjs", "/*\n * @file test_diagnostic_rendering.mjs")
    assert_text_contains("tests/core/diagnostic_json_producer.c", "/*\n * @file diagnostic_json_producer.c")
    assert_text_contains("tests/core/diagnostic_json_producer.c", "masm32_sim_wasm_run_source_json")
    assert_text_contains("README.md", "docs/FULL_IMPLEMENTATION_SPEC.md")
    assert_text_contains("README.md", "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "The default address/range memory validation mode is region-only")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "casemap-policy-changed")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "ambiguous-symbol")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL")
    assert_text_not_contains("src/core/vm_memory.c", "0x00400000U")
    assert_text_not_contains("src/core/vm_memory.c", "0x00500000U")
    assert_text_not_contains("src/core/vm_exec.c", "0x00500000U")
    log_detail("Milestone structure tests passed.")


def executable_output_path(output_name: str) -> pathlib.Path:
    """Return the platform-specific path for a native test executable.

    Args:
        output_name: Extensionless test executable name used by the runner.

    Returns:
        Build-directory path with a Windows `.exe` suffix when needed.
    """

    executable_name = output_name
    if os.name == "nt" and pathlib.Path(output_name).suffix == "":
        executable_name = output_name + ".exe"
    return BUILD_DIR / executable_name


def compile_c_binary(output_name: str, sources: list[str], *, subgroup: str | None = None) -> pathlib.Path:
    """Compile one C test-support binary.

    Args:
        output_name: File name for the compiled executable.
        sources: Repository-relative C source files to compile.
        subgroup: Optional runner subgroup to report if compilation fails.

    Returns:
        Path to the compiled executable.
    """

    compiler = os.environ.get("CC", "cc")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    output = executable_output_path(output_name)
    run_command(
        [
            compiler,
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-Isrc/core",
            "-Isrc/parser",
            *sources,
            "-o",
            str(output),
        ],
        subgroup=subgroup,
    )
    return output


def compile_and_run_c_test(output_name: str, sources: list[str], *, subgroup: str | None = None) -> None:
    """Compile and run one C unit test binary.

    Args:
        output_name: File name for the compiled test executable.
        sources: Repository-relative C source files to compile.
        subgroup: Optional runner subgroup to report if compilation or execution fails.
    """

    output = compile_c_binary(output_name, sources, subgroup=subgroup)
    run_command([str(output)], subgroup=subgroup)


def run_native_parser_tests() -> None:
    """Compile and run native lexer/parser coverage."""

    subgroup = "native-parser"
    compile_and_run_c_test(
        "test_lexer",
        [
            "tests/core/test_lexer.c",
            "src/core/vm_cpu.c",
            "src/parser/lexer.c",
        ],
        subgroup=subgroup,
    )
    compile_and_run_c_test(
        "test_parser",
        [
            "tests/core/test_parser.c",
            "src/core/vm_cpu.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
            "src/core/vm_ir.c",
            "src/core/vm_exec.c",
            "src/core/vm_diagnostic_policy.c",
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
        ],
        subgroup=subgroup,
    )


def run_native_exec_tests() -> None:
    """Compile and run native CPU, flag, and API smoke coverage."""

    subgroup = "native-exec"
    compile_and_run_c_test(
        "test_milestone_zero",
        [
            "tests/core/test_milestone_zero.c",
            "src/core/masm32_sim_api.c",
            "src/core/vm_cpu.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
            "src/core/vm_ir.c",
            "src/core/vm_exec.c",
            "src/core/vm_diagnostic_policy.c",
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
            "src/wasm/wasm_api.c",
        ],
        subgroup=subgroup,
    )
    compile_and_run_c_test(
        "test_vm_cpu",
        [
            "tests/core/test_vm_cpu.c",
            "src/core/vm_cpu.c",
        ],
        subgroup=subgroup,
    )
    compile_and_run_c_test(
        "test_vm_flags",
        [
            "tests/core/test_vm_flags.c",
            "src/core/vm_cpu.c",
        ],
        subgroup=subgroup,
    )


def run_native_memory_layout_tests() -> None:
    """Compile and run native memory, layout, data-section, and object-map coverage."""

    subgroup = "native-memory-layout"
    compile_and_run_c_test(
        "test_vm_memory",
        [
            "tests/core/test_vm_memory.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
        ],
        subgroup=subgroup,
    )
    compile_and_run_c_test(
        "test_vm_layout",
        [
            "tests/core/test_vm_layout.c",
            "src/core/vm_cpu.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
            "src/core/vm_ir.c",
            "src/core/vm_exec.c",
        ],
        subgroup=subgroup,
    )
    compile_and_run_c_test(
        "test_data_section",
        [
            "tests/core/test_data_section.c",
            "src/core/masm32_sim_api.c",
            "src/core/vm_cpu.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
            "src/core/vm_ir.c",
            "src/core/vm_exec.c",
            "src/core/vm_diagnostic_policy.c",
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
            "src/wasm/wasm_api.c",
        ],
        subgroup=subgroup,
    )
    compile_and_run_c_test(
        "test_object_map",
        [
            "tests/core/test_object_map.c",
            "src/core/masm32_sim_api.c",
            "src/core/vm_cpu.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
            "src/core/vm_ir.c",
            "src/core/vm_exec.c",
            "src/core/vm_diagnostic_policy.c",
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
            "src/wasm/wasm_api.c",
        ],
        subgroup=subgroup,
    )


def run_native_diagnostics_policy_tests() -> None:
    """Compile and run native diagnostic-policy registry coverage."""

    subgroup = "native-diagnostics-policy"
    compile_and_run_c_test(
        "test_diagnostic_policy",
        [
            "tests/core/test_diagnostic_policy.c",
            "src/core/vm_diagnostic_policy.c",
        ],
        subgroup=subgroup,
    )


def run_native_control_flow_tests() -> None:
    """Compile and run native executor coverage that owns control-flow regression risk."""

    subgroup = "native-control-flow"
    compile_and_run_c_test(
        "test_vm_exec",
        [
            "tests/core/test_vm_exec.c",
            "src/core/masm32_sim_api.c",
            "src/core/vm_cpu.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
            "src/core/vm_ir.c",
            "src/core/vm_exec.c",
            "src/core/vm_diagnostic_policy.c",
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
            "src/wasm/wasm_api.c",
        ],
        subgroup=subgroup,
    )


def run_native_tests() -> None:
    """Compile and run complete native C tests except source-run integration tests."""

    run_native_parser_tests()
    run_native_exec_tests()
    run_native_memory_layout_tests()
    run_native_diagnostics_policy_tests()
    run_native_control_flow_tests()


def source_run_test_sources() -> list[str]:
    """Return source files used by the source-run integration test binary."""

    return [
        "tests/core/test_wasm_source_run.c",
        "src/core/masm32_sim_api.c",
        "src/core/vm_cpu.c",
        "src/core/vm_memory.c",
        "src/core/vm_layout.c",
        "src/core/vm_ir.c",
        "src/core/vm_exec.c",
        "src/core/vm_diagnostic_policy.c",
        "src/parser/lexer.c",
        "src/parser/parser.c",
        "src/parser/symbols.c",
        "src/parser/object_map.c",
        "src/wasm/wasm_api.c",
    ]


def compile_source_run_test_binary(subgroup: str | None = None) -> pathlib.Path:
    """Compile the source-run integration test binary.

    Args:
        subgroup: Optional runner subgroup to report if compilation fails.

    Returns:
        Path to the compiled source-run test executable.
    """

    return compile_c_binary("test_wasm_source_run", source_run_test_sources(), subgroup=subgroup)


def run_source_run_subgroup_tests(subgroup_name: str) -> None:
    """Compile and run one source-run fixture-family subgroup.

    Args:
        subgroup_name: Public runner subgroup name from SOURCE_RUN_SUBGROUPS.
    """

    output = compile_source_run_test_binary(subgroup=subgroup_name)
    family_name = subgroup_name[len("source-run-"):]
    run_command([str(output), "--group", family_name], subgroup=subgroup_name)


def run_source_run_tests() -> None:
    """Compile and run complete native source-run integration coverage."""

    output = compile_source_run_test_binary(subgroup="source-run")
    for subgroup_name in SOURCE_RUN_SUBGROUPS:
        family_name = subgroup_name[len("source-run-"):]
        run_command([str(output), "--group", family_name], subgroup=subgroup_name)


def build_diagnostic_json_producer() -> None:
    """Build the native source-run JSON producer used by diagnostic tests."""

    global DIAGNOSTIC_JSON_PRODUCER_BUILT
    if DIAGNOSTIC_JSON_PRODUCER_BUILT:
        return

    compile_c_binary(
        "diagnostic_json_producer",
        [
            "tests/core/diagnostic_json_producer.c",
            "src/core/masm32_sim_api.c",
            "src/core/vm_cpu.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
            "src/core/vm_ir.c",
            "src/core/vm_exec.c",
            "src/core/vm_diagnostic_policy.c",
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
            "src/wasm/wasm_api.c",
        ],
    )
    DIAGNOSTIC_JSON_PRODUCER_BUILT = True

def require_node() -> None:
    """Require Node.js for JavaScript-based tests."""

    if shutil.which("node") is None:
        raise TestFailure("node is required for this focused test group")


def run_protocol_tests() -> None:
    """Run worker/protocol schema tests independently from other web tests."""

    require_node()
    run_command(["node", "tests/web/test_protocol.mjs"])


def run_web_tests() -> None:
    """Run browser-side Node tests that do not require native diagnostics."""

    require_node()
    run_command(["node", "tests/web/test_settings.mjs"])
    run_command(["node", "tests/web/test_collapsible_settings.mjs"])
    run_command(["node", "tests/web/test_formatters.mjs"])


def diagnostic_rendering_environment() -> dict[str, str]:
    """Build the deterministic environment for rendered diagnostic tests.

    Returns:
        Environment with stale diagnostic-control variables removed and the
        native diagnostic JSON producer path set explicitly.
    """

    diagnostic_env = os.environ.copy()
    for key in [
        "MASM32_DIAGNOSTIC_MEMORY_VALIDATION",
        "MASM32_DIAGNOSTIC_LAYOUT_MODE",
        "MASM32_DIAGNOSTIC_AUTO_DATA_LIMIT",
        "MASM32_DIAGNOSTIC_AUTO_STACK_LIMIT",
        "MASM32_DIAGNOSTIC_AUTO_HEAP_REQUEST",
        "MASM32_DIAGNOSTIC_AUTO_HEAP_LIMIT",
        "MASM32_DIAGNOSTIC_AUTO_TOTAL_LIMIT",
        "MASM32_DIAGNOSTIC_SECTION_CAPACITY_VALIDATION",
        "MASM32_DIAGNOSTIC_SECTION_IMAGE_VALIDATION",
        "MASM32_DIAGNOSTIC_SHIFT_VALIDATION",
        "MASM32_DIAGNOSTIC_UNDEFINED_FLAG_USE",
    ]:
        diagnostic_env.pop(key, None)
    diagnostic_env["MASM32_DIAGNOSTIC_JSON_PRODUCER"] = str(executable_output_path("diagnostic_json_producer").resolve())
    return diagnostic_env


def run_diagnostics_json_tests() -> None:
    """Run the native diagnostic JSON producer and structured-payload checks."""

    require_node()
    build_diagnostic_json_producer()
    diagnostic_env = diagnostic_rendering_environment()
    diagnostic_env["MASM32_DIAGNOSTIC_RENDERING_GROUP"] = "json"
    run_command(
        ["node", "tests/web/test_diagnostic_rendering.mjs"],
        env=diagnostic_env,
        subgroup="diagnostics-json",
    )


def run_diagnostics_rendered_subgroup(subgroup_name: str) -> None:
    """Run one exact rendered Simulator Messages diagnostic subgroup.

    Args:
        subgroup_name: Public runner subgroup name from DIAGNOSTIC_SUBGROUPS.
    """

    require_node()
    build_diagnostic_json_producer()
    rendering_group = DIAGNOSTIC_RENDERING_GROUP_BY_SUBGROUP[subgroup_name]
    diagnostic_env = diagnostic_rendering_environment()
    diagnostic_env["MASM32_DIAGNOSTIC_RENDERING_GROUP"] = rendering_group
    run_command(
        ["node", "tests/web/test_diagnostic_rendering.mjs"],
        env=diagnostic_env,
        subgroup=subgroup_name,
    )


def run_diagnostics_tests() -> None:
    """Run complete diagnostic JSON and rendered Simulator Messages coverage."""

    run_diagnostics_json_tests()
    for subgroup_name in DIAGNOSTIC_RENDERING_GROUP_BY_SUBGROUP:
        run_diagnostics_rendered_subgroup(subgroup_name)


def run_quick_tests() -> None:
    """Run a smoke subset that is explicitly not full milestone verification."""

    run_structure_tests()
    compile_and_run_c_test(
        "quick_test_vm_cpu",
        [
            "tests/core/test_vm_cpu.c",
            "src/core/vm_cpu.c",
        ],
    )
    run_protocol_tests()


def read_file(path: str) -> str:
    """Read a repository-relative UTF-8 text file.

    Args:
        path: Repository-relative text file path.

    Returns:
        File contents.
    """

    return (ROOT / path).read_text(encoding="utf-8")


def assert_all_text_contains(path: str, expected_values: Iterable[str]) -> None:
    """Verify that a file contains every expected text fragment.

    Args:
        path: Repository-relative text file path.
        expected_values: Required text fragments.
    """

    text = read_file(path)
    missing = [expected for expected in expected_values if expected not in text]
    if missing:
        raise TestFailure(f"{path} is missing expected text: {', '.join(missing)}")


def assert_all_text_not_contains(path: str, unexpected_values: Iterable[str]) -> None:
    """Verify that a file omits every unexpected text fragment.

    Args:
        path: Repository-relative text file path.
        unexpected_values: Text fragments that must not appear.
    """

    text = read_file(path)
    present = [unexpected for unexpected in unexpected_values if unexpected in text]
    if present:
        raise TestFailure(f"{path} contains unexpected text: {', '.join(present)}")


def create_arg_parser() -> argparse.ArgumentParser:
    """Create the command-line parser for the test runner.

    Returns:
        Configured argparse parser.
    """

    parser = argparse.ArgumentParser(
        description=(
            "Run Online MASM32 Educational Simulator tests. Default invocation "
            "is equivalent to --all; use focused groups when hosted environments "
            "time out or truncate output."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Focused groups:
  --structure    repository structure, file/header, metadata, and shape checks
  --native       native C unit/parser/executor/helper tests excluding source-run integration
  --source-run   native source-run JSON/integration tests through all source-run subgroups
  --web          browser-side Node formatter/settings tests that do not need native diagnostics
  --diagnostics  build native diagnostic JSON producer and run exact rendered Simulator Messages tests
  --protocol     worker/protocol schema tests
  --static       documentation, runner, group-name, and fixture-inventory consistency checks

Official native subgroups:
  --native-parser                 lexer and parser tests
  --native-exec                   CPU, flag, and API smoke tests
  --native-memory-layout          memory, layout, data-section, and object-map tests
  --native-diagnostics-policy     diagnostic-policy registry tests
  --native-control-flow           executor control-flow regression tests

Official source-run subgroups:
  --source-run-core               core source-run parser/runtime integration fixtures
  --source-run-diagnostics        source-run diagnostic and error-path fixtures
  --source-run-settings           settings, policy, CASEMAP, and startup-mode fixtures
  --source-run-memory-layout      memory-layout, object, section, and uninitialized-storage fixtures
  --source-run-control-flow       source-run label, branch, CALL, RET, entry, and watchdog fixtures

Official diagnostic subgroups:
  --diagnostics-json                     native producer build and structured diagnostic payload checks
  --diagnostics-rendered-call-ret        CALL, RET, root RET, helper RET, and procedure fallthrough messages
  --diagnostics-rendered-memory          memory bounds, object bounds, uninitialized, CONST, DATA?, and checked-memory messages
  --diagnostics-rendered-directives      directives, sections, PROC/ENDP/END, include directives, and parser directive messages
  --diagnostics-rendered-compatibility   compatibility settings, unsupported MASM forms, and non-goal boundary messages
  --diagnostics-rendered-arithmetic      ADD/SUB/CMP/ADC/SBB/NEG and related arithmetic/logical messages
  --diagnostics-rendered-shift-rotate    SHL/SHR/SAL/SAR/ROL/ROR/RCL/RCR messages
  --diagnostics-rendered-mul-div         MUL/IMUL/DIV/IDIV messages
  --diagnostics-rendered-runtime         runtime terminal messages not owned by a more specific family

Output modes:
  default         compact group status and final summary
  --quiet         group start/status lines, final summary, and failure details only
  --verbose       subprocess commands, captured subprocess output, and fixture inventory details

Notes:
  --quick is a smoke subset, not full verification.
  The broad focused groups remain the first timeout-safe decomposition.
  If --native or --source-run is too large, rerun the official subgroups for
  that broad group independently and report those subgroup results separately.
  If --diagnostics is too large, rerun the official diagnostic subgroups
  independently and report aggregate, broad-group, subgroup, skipped, timed-out,
  and not-run commands separately.

Windows examples:
  py scripts\\run_tests.py --all
  py scripts\\run_tests.py --diagnostics
""".strip(),
    )
    parser.add_argument("--all", action="store_true", help="run every focused group; default when no group is selected")
    parser.add_argument("--quick", action="store_true", help="run a smoke subset only; not sufficient for full milestone acceptance")
    parser.add_argument("--structure", action="store_true", help="run repository structure and static shape checks")
    parser.add_argument("--native", action="store_true", help="run native C tests excluding source-run integration")
    parser.add_argument("--native-parser", action="store_true", help="run native lexer and parser tests")
    parser.add_argument("--native-exec", action="store_true", help="run native CPU, flag, and API smoke tests")
    parser.add_argument("--native-memory-layout", action="store_true", help="run native memory, layout, data-section, and object-map tests")
    parser.add_argument("--native-diagnostics-policy", action="store_true", help="run native diagnostic-policy registry tests")
    parser.add_argument("--native-control-flow", action="store_true", help="run native executor control-flow regression tests")
    parser.add_argument("--source-run", action="store_true", help="run native source-run JSON/integration tests through all official subgroups")
    parser.add_argument("--source-run-core", action="store_true", help="run core source-run parser/runtime integration fixtures")
    parser.add_argument("--source-run-diagnostics", action="store_true", help="run source-run diagnostic and error-path fixtures")
    parser.add_argument("--source-run-settings", action="store_true", help="run source-run settings, policy, CASEMAP, and startup-mode fixtures")
    parser.add_argument("--source-run-memory-layout", action="store_true", help="run source-run memory-layout, object, section, and uninitialized-storage fixtures")
    parser.add_argument("--source-run-control-flow", action="store_true", help="run source-run label, branch, CALL, RET, entry, and watchdog fixtures")
    parser.add_argument("--web", action="store_true", help="run browser-side Node tests that do not need native diagnostics")
    parser.add_argument("--diagnostics", action="store_true", help="run native diagnostic JSON plus exact rendered Simulator Messages tests")
    parser.add_argument("--diagnostics-json", action="store_true", help="run native diagnostic producer and structured diagnostic payload checks")
    parser.add_argument("--diagnostics-rendered-call-ret", action="store_true", help="run CALL/RET rendered Simulator Messages diagnostics")
    parser.add_argument("--diagnostics-rendered-memory", action="store_true", help="run memory rendered Simulator Messages diagnostics")
    parser.add_argument("--diagnostics-rendered-directives", action="store_true", help="run directive rendered Simulator Messages diagnostics")
    parser.add_argument("--diagnostics-rendered-compatibility", action="store_true", help="run compatibility rendered Simulator Messages diagnostics")
    parser.add_argument("--diagnostics-rendered-arithmetic", action="store_true", help="run arithmetic/logical rendered Simulator Messages diagnostics")
    parser.add_argument("--diagnostics-rendered-shift-rotate", action="store_true", help="run shift/rotate rendered Simulator Messages diagnostics")
    parser.add_argument("--diagnostics-rendered-mul-div", action="store_true", help="run MUL/IMUL/DIV/IDIV rendered Simulator Messages diagnostics")
    parser.add_argument("--diagnostics-rendered-runtime", action="store_true", help="run runtime rendered Simulator Messages diagnostics not owned by a narrower subgroup")
    parser.add_argument("--protocol", action="store_true", help="run worker/protocol schema tests independently")
    parser.add_argument("--static", action="store_true", help="run documentation and runner consistency checks")
    output_group = parser.add_mutually_exclusive_group()
    output_group.add_argument("--quiet", action="store_true", help="print group status lines, final summary, and full failure details only")
    output_group.add_argument("--verbose", action="store_true", help="print commands, child output, and fixture inventory details")
    return parser


def supported_help_flags() -> list[str]:
    """Return every public flag that runner self-tests expect in help output.

    Returns:
        Public command-line flags.
    """

    return [
        "--all",
        "--quick",
        "--structure",
        "--native",
        *["--" + subgroup for subgroup in NATIVE_SUBGROUPS],
        "--source-run",
        *["--" + subgroup for subgroup in SOURCE_RUN_SUBGROUPS],
        "--web",
        "--diagnostics",
        *["--" + subgroup for subgroup in DIAGNOSTIC_SUBGROUPS],
        "--protocol",
        "--static",
        "--quiet",
        "--verbose",
    ]


def select_groups(args: argparse.Namespace) -> tuple[list[str], bool]:
    """Select runner groups from parsed command-line arguments.

    Args:
        args: Parsed command-line arguments.

    Returns:
        Pair of selected group names and a boolean indicating full verification.
    """

    if args.quick:
        return ["quick"], False

    selectable_groups = [*REQUIRED_GROUPS, *NATIVE_SUBGROUPS, *SOURCE_RUN_SUBGROUPS, *DIAGNOSTIC_SUBGROUPS]
    selected = [group for group in selectable_groups if getattr(args, group.replace("-", "_"))]
    if args.all or not selected:
        return list(REQUIRED_GROUPS), True
    return selected, False


def run_group(name: str, function: Callable[[], None]) -> GroupResult:
    """Run one focused group and return its status.

    Args:
        name: User-facing group name.
        function: Group runner callable.

    Returns:
        GroupResult describing pass/fail outcome.
    """

    global CURRENT_GROUP
    previous_group = CURRENT_GROUP
    CURRENT_GROUP = name
    print_group_start(name)
    try:
        function()
    except TestFailure as error:
        details = str(error)
        print_group_status(name, "FAIL", "see failure details below")
        print(details, file=sys.stderr)
        CURRENT_GROUP = previous_group
        return GroupResult(name=name, status="FAIL", details="selected group failed")
    CURRENT_GROUP = previous_group
    print_group_status(name, "PASS", group_success_detail(name))
    return GroupResult(name=name, status="PASS", details=group_success_detail(name))


def group_success_detail(name: str) -> str:
    """Return a compact success detail for one group.

    Args:
        name: User-facing group name.

    Returns:
        Short detail text.
    """

    details = {
        "quick": "smoke subset passed; full verification not performed",
        "structure": "repository structure and metadata checks passed",
        "native": "native C non-source-run tests passed",
        "native-parser": "native lexer and parser tests passed",
        "native-exec": "native CPU, flag, and API smoke tests passed",
        "native-memory-layout": "native memory and layout tests passed",
        "native-diagnostics-policy": "native diagnostic-policy tests passed",
        "native-control-flow": "native executor control-flow tests passed",
        "source-run": "all source-run subgroups passed independently",
        "source-run-core": "core source-run fixtures passed",
        "source-run-diagnostics": "source-run diagnostic fixtures passed",
        "source-run-settings": "source-run settings fixtures passed",
        "source-run-memory-layout": "source-run memory-layout fixtures passed",
        "source-run-control-flow": "source-run control-flow fixtures passed",
        "web": "browser-side Node module tests passed",
        "diagnostics": "native diagnostic producer and all diagnostic subgroups passed",
        "diagnostics-json": "native diagnostic JSON and structured-payload checks passed",
        "diagnostics-rendered-call-ret": "CALL/RET rendered diagnostics passed",
        "diagnostics-rendered-memory": "memory rendered diagnostics passed",
        "diagnostics-rendered-directives": "directive rendered diagnostics passed",
        "diagnostics-rendered-compatibility": "compatibility rendered diagnostics passed",
        "diagnostics-rendered-arithmetic": "arithmetic/logical rendered diagnostics passed",
        "diagnostics-rendered-shift-rotate": "shift/rotate rendered diagnostics passed",
        "diagnostics-rendered-mul-div": "MUL/IMUL/DIV/IDIV rendered diagnostics passed",
        "diagnostics-rendered-runtime": "runtime rendered diagnostics passed",
        "protocol": "protocol tests passed independently",
        "static": "runner/documentation consistency checks passed",
    }
    return details.get(name, "group passed")


def print_summary(results: list[GroupResult], selected_groups: Sequence[str]) -> None:
    """Print a compact final summary table.

    Args:
        results: Results for groups that were run.
        selected_groups: Groups requested for this runner invocation.
    """

    result_by_name = {result.name: result for result in results}
    table_groups = list(REQUIRED_GROUPS)
    for group in selected_groups:
        if group not in table_groups:
            table_groups.append(group)
    if "quick" in selected_groups:
        table_groups = ["quick", *table_groups]

    name_width = max(12, max((len(group) for group in table_groups), default=12))
    print(f"\n{'Group':<{name_width}} Status    Details")
    for group in table_groups:
        result = result_by_name.get(group)
        if result is None:
            print(f"{group:<{name_width}} NOT-RUN   not selected")
        else:
            print(f"{group:<{name_width}} {result.status:<8} {result.details}")

    if shutil.which("emcc") is None:
        print("Browser/Wasm rebuild smoke: SKIP - emcc unavailable in this environment.")
    else:
        print("Browser/Wasm rebuild smoke: NOT-RUN - not requested by this native/Node runner command.")


def assert_help_lists_supported_flags() -> None:
    """Verify that help output lists every public runner flag and broad-group guidance."""

    help_text = create_arg_parser().format_help()
    missing = [flag for flag in supported_help_flags() if flag not in help_text]
    if missing:
        raise TestFailure("help output is missing runner flags: " + ", ".join(missing))
    if "Official diagnostic subgroups:" not in help_text:
        raise TestFailure("help output is missing official diagnostic subgroup guidance")
    if "Source-run and diagnostic subgroups are intentionally not split in Phase 56A" in help_text:
        raise TestFailure("help output still contains stale Phase 56A subgroup guidance")


def assert_parser_accepts_required_flags() -> None:
    """Verify that argparse accepts every required public flag."""

    parser = create_arg_parser()
    parser.parse_args(["--all"])
    parser.parse_args(["--quick"])
    parser.parse_args(["--quiet", "--structure"])
    parser.parse_args(["--verbose", "--diagnostics"])
    parser.parse_args(["--quiet", "--native-control-flow"])
    parser.parse_args(["--quiet", "--source-run-control-flow"])
    parser.parse_args(["--quiet", "--diagnostics-rendered-memory"])
    for group in [*REQUIRED_GROUPS, *NATIVE_SUBGROUPS, *SOURCE_RUN_SUBGROUPS, *DIAGNOSTIC_SUBGROUPS]:
        parser.parse_args(["--" + group])


def assert_unknown_flag_fails() -> None:
    """Verify that an unknown flag fails with nonzero argparse status."""

    parser = create_arg_parser()
    stderr_capture = io.StringIO()
    try:
        with contextlib.redirect_stderr(stderr_capture):
            parser.parse_args(["--definitely-not-a-supported-runner-flag"])
    except SystemExit as error:
        if error.code != 0 and "unrecognized arguments" in stderr_capture.getvalue():
            return
    raise TestFailure("unknown runner flags must fail with a nonzero argparse exit and useful message")


def assert_docs_match_runner_groups() -> None:
    """Verify that testing and development documentation names every runner group."""

    docs = read_file("docs/TESTING_GUIDE.md")
    build_docs = read_file("docs/BUILDING_AND_DEVELOPMENT.md")
    readme = read_file("README.md")
    for group in REQUIRED_GROUPS:
        command = f"python3 scripts/run_tests.py --{group}"
        if command not in docs:
            raise TestFailure(f"docs/TESTING_GUIDE.md does not document {command}")
        if command not in build_docs:
            raise TestFailure(f"docs/BUILDING_AND_DEVELOPMENT.md does not document {command}")
    for command in [
        "python3 scripts/run_tests.py --all",
        "python3 scripts/run_tests.py --quick",
        "python3 scripts/run_tests.py --quiet",
        "python3 scripts/run_tests.py --verbose",
        "py scripts\\run_tests.py --all",
        "py scripts\\run_tests.py --diagnostics",
    ]:
        if command not in docs:
            raise TestFailure(f"docs/TESTING_GUIDE.md does not document {command}")
        if command not in build_docs:
            raise TestFailure(f"docs/BUILDING_AND_DEVELOPMENT.md does not document {command}")
    if "python3 scripts/run_tests.py --all" not in readme:
        raise TestFailure("README.md does not document the aggregate test command")
    if "docs/BUILDING_AND_DEVELOPMENT.md" not in readme:
        raise TestFailure("README.md does not link to detailed focused-test and build/development guidance")


def assert_fixture_inventory_documented() -> None:
    """Verify that the source-run fixture inventory exists."""

    assert_all_text_contains(
        "docs/TESTING_GUIDE.md",
        [
            "Source-run fixture inventory",
            "tests/core/test_wasm_source_run.c",
            "phase51-layout-fixed-automatic-equivalence",
            "source-run-memory-layout",
            "phase53e-ui-settings-policy-routing",
            "source-run-settings",
            "phase56-div-source-run-coverage",
            "phase57-idiv-source-run-coverage",
            "source-run-control-flow",
            "preserved in the broad source-run group",
        ],
    )


def assert_timeout_policy_documented() -> None:
    """Verify that timeout-safe assistant verification policy is documented."""

    assert_all_text_contains(
        "docs/TESTING_GUIDE.md",
        [
            "A timeout is not a pass. A timeout is also not by itself proof of a simulator regression.",
            "### Official subgroup command ownership",
            "Required diagnostic subgroups from Phase 71A1",
            "Implemented source-run/native subgroups from Phase 71B1",
            "### Required timeout-aware command reporting",
            "Do not omit timed-out commands from the command list",
            "Run source-run/native subgroups when a broad source-run or native command is too large",
            "Runtime/source-run MASM behavior phase:",
            "Browser/Wasm rebuild status:",
            "Whether aggregate success was claimed:",
        ],
    )


def assert_failure_reporting_contract_present() -> None:
    """Verify that failure-output contract text exists in runner and docs."""

    assert_all_text_contains(
        "scripts/run_tests.py",
        [
            "Failure details:",
            "- Group:",
            "- Command:",
            "- Exit code:",
            "- Stdout tail:",
            "- Stderr tail:",
        ],
    )
    assert_all_text_contains(
        "docs/TESTING_GUIDE.md",
        [
            "failing group",
            "failing command",
            "subprocess exit code",
            "stdout/stderr tail",
            "Source-run fixture failures include the fixture name",
        ],
    )


def assert_live_text_avoids_milestone_relative_wording() -> None:
    """Verify live diagnostics and current-status text avoid milestone-relative wording.

    Phase 56B allows historical reports, guide examples, and negative tests to
    mention prohibited phrases. This audit scans live source, web source/index, test, README,
    and supported-syntax surfaces while allowing negative assertions that prove
    a forbidden phrase is absent from user-facing output.
    """

    forbidden_phrases = [
        "this milestone",
        "unsupported by the current milestone",
        "not implemented in this milestone",
        "not supported in this milestone",
        "unsupported in this milestone",
        "future milestone",
        "will be added in a future milestone",
        "implemented through the current milestone",
        "not supported in phase",
        "outside phase",
        "deferred to phase",
    ]
    forbidden_patterns = [
        re.compile(r"phase\s+\d+[a-z0-9]*\s+accepts only", re.IGNORECASE),
        re.compile(r"phase\s+\d+[a-z0-9]*\s+direct call accepts only", re.IGNORECASE),
        re.compile(r"phase\s+\d+[a-z0-9]*\s+implements only", re.IGNORECASE),
    ]
    scanned_paths: list[pathlib.Path] = []
    for directory in [ROOT / "src", ROOT / "web" / "src", ROOT / "tests"]:
        scanned_paths.extend(path for path in directory.rglob("*") if path.is_file())
    scanned_paths.extend([ROOT / "web" / "index.html", ROOT / "docs" / "SUPPORTED_SYNTAX.md", ROOT / "README.md"])

    violations: list[str] = []
    for path in sorted(scanned_paths):
        if path.suffix.lower() in {".zip", ".wasm", ".o", ".exe", ".pyc"}:
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        relative_path = path.relative_to(ROOT)
        in_allowed_forbidden_fragment_list = False
        for line_number, line in enumerate(lines, start=1):
            lowered = line.lower()
            if "PHASE_71B_FORBIDDEN_DIAGNOSTIC_FRAGMENTS" in line:
                in_allowed_forbidden_fragment_list = True
                continue
            if in_allowed_forbidden_fragment_list:
                if line.strip() == "];":
                    in_allowed_forbidden_fragment_list = False
                continue
            if (
                "expect_json_not_contains" in line
                or "expect_string_not_contains" in line
                or "assert_text_not_contains" in line
                or "assertNoPhase71bForbiddenDiagnosticWording" in line
            ):
                continue
            matched_phrase = False
            for phrase in forbidden_phrases:
                if phrase in lowered:
                    violations.append(f"{relative_path}:{line_number}: {phrase}")
                    matched_phrase = True
                    break
            if matched_phrase:
                continue
            for pattern in forbidden_patterns:
                if pattern.search(line):
                    violations.append(f"{relative_path}:{line_number}: {pattern.pattern}")
                    break

    if violations:
        raise TestFailure("live milestone-relative wording found:\n" + "\n".join(violations))


def assert_phase71b2_stale_milestone_context_checks() -> None:
    """Verify active documentation avoids stale milestone-context wording.

    Phase 71B2 intentionally rejects a narrow set of active-source phrases
    that imply a source-of-truth document owns current milestone status or
    next-phase selection. Historical reports, quoted anti-pattern examples,
    and the guard lists used by this check remain allowed.
    """

    forbidden_patterns = [
        re.compile(r"As of the source-of-truth revision after Phase", re.IGNORECASE),
        re.compile(r"next canonical guide phase is Phase", re.IGNORECASE),
        re.compile(r"Current behavior through Phase", re.IGNORECASE),
        re.compile(r"Current Phase\s+\d+[A-Za-z0-9]*\s+behavior", re.IGNORECASE),
        re.compile(r"current Phase\s+\d+[A-Za-z0-9]*\s+repository state", re.IGNORECASE),
        re.compile(r"current behavior through Phase", re.IGNORECASE),
        re.compile(r"\bas of Phase\s+\d+[A-Za-z0-9]*\b", re.IGNORECASE),
    ]
    scanned_paths = [
        ROOT / "README.md",
        ROOT / "docs" / "FULL_IMPLEMENTATION_SPEC.md",
        ROOT / "docs" / "INCREMENTAL_IMPLEMENTATION_GUIDE.md",
        ROOT / "docs" / "SUPPORTED_SYNTAX.md",
        ROOT / "docs" / "TESTING_GUIDE.md",
        ROOT / "docs" / "BUILDING_AND_DEVELOPMENT.md",
        ROOT / "docs" / "MILESTONE_HISTORY.md",
        ROOT / "web" / "index.html",
    ]
    allowed_context_fragments = [
        "Do not use this style:",
        "Do not use broad phrases such as",
        "Remove stale-prone active wording such as",
        "The minimum rejected active-source patterns are:",
        "minimum phrases to scan are:",
    ]
    allowed_literal_lines = {
        "As of the source-of-truth revision after Phase",
        "next canonical guide phase is Phase",
        "Current behavior through Phase",
        "Current Phase <N> behavior",
        "current behavior through Phase",
        "as of Phase <N>",
        "Current behavior through Phase 71A treats selected-entry ENDP fallthrough as successful program completion. Phase 71C changes that default.",
        "Do not use broad phrases such as `current behavior through Phase <N>`, `currently unsupported by Phase <N>`, `not supported by the current milestone`, or `as of the source-of-truth revision after Phase <N>` in active source-of-truth sections. Such phrases may appear in historical reports, quoted anti-pattern examples, or explicitly historical changelog text.",
    }
    violations = []
    for path in scanned_paths:
        lines = path.read_text(encoding="utf-8").splitlines()
        relative_path = path.relative_to(ROOT)
        for line_number, line in enumerate(lines, start=1):
            stripped = line.strip().strip("`- ")
            if stripped in allowed_literal_lines:
                continue
            if any(fragment in line for fragment in allowed_context_fragments):
                continue
            for pattern in forbidden_patterns:
                if pattern.search(line):
                    violations.append(f"{relative_path}:{line_number}: {pattern.pattern}")
                    break
    if violations:
        raise TestFailure("stale active milestone-context wording found:\n" + "\n".join(violations))


def assert_current_status_and_harness_documented() -> None:
    """Verify Phase 77 status, concise status surfaces, and harness documentation wording."""

    def read_repo_text(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")

    def section_text(path: str, start_marker: str, end_marker: str) -> str:
        full_text = read_repo_text(path)
        start = full_text.index(start_marker)
        end = full_text.index(end_marker, start)
        return full_text[start:end]

    readme_current_status = section_text("README.md", "## Current status", "## Current simulator scope")
    if read_repo_text("README.md").count("## Current status") != 1:
        raise TestFailure("README must contain exactly one active current status section")
    if "## Current repository status" in read_repo_text("README.md"):
        raise TestFailure("README active status heading must use Current status, not Current repository status")
    assert_all_text_contains(
        "README.md",
        [
            "Current milestone",
            "Phase 77 - PROC USES Runtime Save/Restore",
            "Phase 77 executes supported direct `CALL` entry into `PROC USES` procedures",
            "stack-overflow",
            "stack-underflow",
            "source-level 32-bit `push` for registers, immediates, and DWORD memory sources",
            "source-level 32-bit `pop` for registers and DWORD memory destinations",
            "For current accepted syntax, rejected forms, diagnostics, and future/deferred features",
            "For build and artifact verification details",
            "selected-entry source-run startup from `END entryName`",
            "`code-fell-off-end` runtime diagnostics when execution reaches the end of the executable stream without explicit `RET` or Irvine32 `exit`",
            "direct user-procedure `call ProcedureName` with checked internal pseudo-EIP return-token stack writes",
            "plain near helper `ret`/`RET` with checked internal pseudo-EIP return-token stack reads",
            "root-code-stream `ret`/`RET` success by default without stack reads when no helper return is pending",
            "configurable `procedureFallthroughPolicy` for ordinary procedure-boundary fallthrough",
            "`procedure-fell-through` diagnostics for ordinary procedure-boundary fallthrough, including called helper procedures",
            "selected arithmetic, bitwise, shift, rotate, multiply, divide, compare, and branch instructions",
            "direct `jmp`",
            "equality conditional jumps",
            "signed relational conditional jumps",
            "unsigned relational conditional jumps",
        ],
    )
    for forbidden in [
        "phase-71e-entry-procedure-end-mode-output-contract-v1",
        "output-contract tokens and browser/Wasm artifact compatibility details",
        "Wasm API behavior",
        "browser behavior",
        "Source-run output-contract token",
        "source-run output-contract version identifier",
        "Next planned runtime/source-run behavior phase",
        "Repository/archive milestone",
        "Next canonical guide phase",
        "Phase 71E through Phase 71F are planned",
        "Artifact compatibility is intentionally strict",
        "older, newer, missing, malformed, or suffix-mismatched runtime/source-run behavior metadata",
        "Exact requirements implemented",
        "Assumptions used",
        "TODO-style note disposition",
        "Files changed",
        "Tests added",
        "Commands used to test",
        "When this section changes",
        "replace this table",
        "Do not append a second current-status block",
    ]:
        if forbidden in readme_current_status:
            raise TestFailure(f"README current-status section contains excessive status detail: {forbidden}")
    assert_all_text_not_contains(
        "README.md",
        [
            "Phase 69 implements direct near `call ProcedureName`",
            "A successful direct user-procedure `CALL` writes the pseudo-EIP return token",
            "That return-token write is an implicit VM stack write",
            "Failed internal stack writes use the central checked-memory diagnostic path",
            "Phase 70 implements helper plain near `ret`/`RET` with no operands",
            "Phase 71 adds selected-entry root `RET` termination",
            "Phase 71 also reports called non-entry procedure fallthrough with `procedure-fell-through`",
            
            "current source-run execution can still follow linear lowered-instruction order",
            "corrected `END entryName` source-run entry behavior",
            "Milestone 70A: runtime metadata exact-match compatibility; MASM runtime behavior remains Phase 70 RET execution.",
        ],
    )

    building_status = section_text("docs/BUILDING_AND_DEVELOPMENT.md", "## Current status", "## Artifact verification versus rebuild verification")
    assert_all_text_contains(
        "docs/BUILDING_AND_DEVELOPMENT.md",
        [
            "Current milestone:",
            "Phase 77 - PROC USES Runtime Save/Restore",
            "Phase 77 advances runtime/source-run behavior metadata",
            "direct `CALL` entry into `PROC USES` procedures now performs checked automatic register save/restore",
            "exposes `stack-overflow` and `stack-underflow` diagnostics for automatic save/restore failures",
            "Artifact verification versus rebuild verification",
            "Checked-in artifact-content verification",
            "stale-wasm-output-contract",
            "Do not infer that checked-in `web/dist` is stale solely from missing `emcc`",
            "python3 -m http.server 8000 --directory web",
            "./scripts/build_wasm.sh",
            "scripts\\windows\\build_wasm.cmd",
            "python3 scripts/run_tests.py --all",
            "python3 scripts/run_tests.py --source-run",
            "python3 scripts/run_tests.py --diagnostics",
            "Browser/Wasm smoke guidance",
            "Output-contract changes and browser/Wasm artifact status",
            "Browser/Wasm artifact compatibility verified through the documented output-contract identifier.",
        ],
    )
    for forbidden in [
        "When this section changes",
        "replace the existing status lines in place",
        "Do not append milestone-report prose",
        "Next canonical guide phase:",
        "Next runtime/source-run MASM behavior phase:",
        "Repository/archive milestone:",
        "Current source-run output-contract token:",
        "Wasm API behavior",
        "browser behavior",
        "Phase 69C introduced the separate `sourceRunOutputContract` metadata field",
        "Phase 70 implements helper plain near RET return-token execution and validation",
        "Phase 70A is protocol/artifact compatibility cleanup only",
        "Phase 71B follows Phase 71A1 as diagnostic-copy",
        "selected-entry root `RET` terminates successfully by default without an `[ESP]` read",
        "called non-entry procedure fallthrough emits `procedure-fell-through`",
        "Exact requirements implemented",
        "Assumptions used",
        "TODO-style note disposition",
        "Files changed",
        "Tests added",
        "Commands used to test",
    ]:
        if forbidden in building_status:
            raise TestFailure(f"BUILDING current-status section contains excessive status detail: {forbidden}")

    assert_all_text_contains(
        "docs/SUPPORTED_SYNTAX.md",
        [
            "Current milestone:",
            "Phase 77 - PROC USES Runtime Save/Restore",
            "Phase 77 executes supported direct `CALL` entry into `PROC USES` procedures",
            "This document describes the currently accepted MASM32 Educational Mode syntax, rejected forms, diagnostics, and future/deferred syntax.",
            "selected-entry `ENDP` is not an implicit successful terminator",
            "direct near user-procedure `call ProcedureName`",
            "Direct `call ProcedureName` is executable only when `ProcedureName` resolves to a user `PROC` entry",
            "A successful direct user-procedure `CALL` writes a pseudo-EIP return token to `ESP - 4`",
            "current public source-run output contract does not expose implicit CALL return-token writes or automatic USES save/restore stack writes as user-visible `memoryChanges` rows",
            "`ret`/`RET` with no operands is implemented as a plain near return",
            "Bare `name PROC` declarations record procedure metadata",
            "Unsupported non-USES attributes or parameters after `PROC` are rejected with targeted parser/source-run diagnostics",
            "duplicate-procedure",
            "proc-end-mismatch",
            "Near `RET imm16` reads and validates the helper return token exactly like plain helper `RET`",
            "Root-code-stream `RET` succeeds by default in MASM32-compatible root RET mode",
            "Optional strict root RET mode rejects root-code-stream `RET` with `root-ret-disallowed-by-mode`",
            "Called helper procedure fallthrough while a helper return token is pending is mapped to `procedure-fell-through`",
            "The selected-entry `ENDP` success rule from pre-71C accepted behavior is no longer the default",
            "Simulator-owned rejected CALL target forms remain rejected unless a later accepted phase explicitly changes the specific simulator-owned form",
            "they are not future work merely because they are currently rejected",
            "External/API calls are not simulator-owned deferred CALL forms",
            "| `the-front-fell-off` | 71D implemented | notice, required easter egg | Harmless notice emitted only after `code-fell-off-end` when the responsible procedure name is exactly `front` under ASCII case-insensitive comparison. |",
            "| `procedure-fell-through` | 71D implemented | warning by default; configurable `off`/`warn`/`error` |",
            "Execution reached the end of the executable code stream without an explicit program terminator. Did you forget to add RET or Irvine32 exit?",
            "Windows/API execution remains outside the simulator boundary as a permanent non-goal unless the canonical specification and guide are deliberately revised.",
            "### Reserved words and user-defined symbols",
            "reserved-word-symbol",
            "`OPTION CASEMAP:NONE` does not make reserved words available as user-defined symbols",
            "`OPTION NOKEYWORD` remains unsupported",
            "Source code cannot read, write, address through, use as an instruction operand, or define `EIP`",
            "Phase 69B final-register display uses stable high-level educational groups",
            "### Simulator Messages grouping",
        ],
    )
    assert_all_text_not_contains(
        "docs/SUPPORTED_SYNTAX.md",
        [
            
            "source-run execution may begin at the first lowered instruction",
            "Latest output/message-ordering cleanup phase:",
            "Latest source-run output-contract phase:",
            "Latest protocol/artifact compatibility cleanup phase:",
            "Root `RET`, non-entry procedure fallthrough diagnostics",
            "the-front-fell-off` | 71D, optional",
            "optional easter egg",
            "source spelling is exactly `front`",
        ],
    )

    assert_all_text_contains(
        "docs/MILESTONE_HISTORY.md",
        [
            "Latest recorded completed milestone in this history file:",
            "Phase 77 - PROC USES Runtime Save/Restore",
            "Latest recorded runtime/source-run MASM behavior phase in this history file:",
            "Phase 77 - PROC USES Runtime Save/Restore",
            "phase-71e-entry-procedure-end-mode-output-contract-v1",
            "This history file records completed milestones and audit evidence.",
            "It is not the phase-order authority",
            "Forward-looking phase navigation is guide-owned.",
            "Corrective artifact-evidence note for Phase 71B",
            "archive's artifact-content scan as the stronger evidence",
            "Milestone reports, archived repository states, and this history file are historical evidence.",
            "They do not replace or override the canonical specification and implementation guide.",
            "Phase 70B - Canonical Documentation Alignment and Compatibility Test Matrix Cleanup",
            "## Phase 71E - Entry-Procedure Auto-Stop Compatibility Setting",
            "Phase 71D is complete as a runtime/source-run behavior phase.",
            "## Phase 71B1 - Source-Run and Native Control-Flow Subgroup Preflight",
            "## Phase 71A1 - Diagnostic Test Runner Subgroup Decomposition",
            "## Phase 71A - Optional Root RET Strictness Mode",
            "## Phase 70 - RET Execution and Return Address Validation",
            "Recent milestone detail in this file may be listed most-recent-first",
            "Concise milestone ledger",
            "Detailed milestone report references",
        ],
    )

    assert_all_text_contains(
        "docs/history/HISTORY_README.md",
        [
            "Source-of-truth and historical report status",
            "docs/FULL_IMPLEMENTATION_SPEC.md",
            "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md",
            "Milestone reports, project audit/handoff reports, and other files under `docs/history/` are historical evidence",
            "A project audit/handoff report may stop before the current repository/archive milestone",
            "If a historical report conflicts with the current canonical spec or guide, treat the conflict as an audit finding",
        ],
    )

    assert_all_text_contains(
        "docs/FULL_IMPLEMENTATION_SPEC.md",
        [
            "This specification owns final product behavior",
            "It does not own phase numbering, latest-completed milestone status, next-phase selection",
            "A behavior described here is not automatically implemented in the repository merely because it appears in the specification.",
            "Implementation support status must be determined from implementation evidence, not from roadmap prose.",
            "### 1.1 Specification, Guide, and Status Surface Authority",
            "This specification is the product-behavior authority.",
            "`INCREMENTAL_IMPLEMENTATION_GUIDE.md` is the implementation-sequence authority.",
            "Short active status surfaces are orientation aids only.",
            "The visible `web/index.html` top-page milestone banner is the compact browser landing-page status surface.",
            "Milestone <N or suffix>: <phase title>",
            "All detailed active-status update workflow belongs in the implementation guide.",
            "Historical milestone reports, historical audit notes, historical handoff reports, changelogs, and sections explicitly labeled as historical may retain wording that was true when they were written.",
            "#### 8.1.1A Procedure Boundary Execution and Code-Stream Fallthrough",
            "The final procedure-boundary execution model is code-stream based.",
            "Procedure-boundary fallthrough diagnostics and optional beginner compatibility settings are assigned by the implementation guide.",
            "Required `the-front-fell-off` Diagnostic Easter Egg",
            "The implementation phase that introduces `code-fell-off-end` also adds one deliberately harmless notice-level diagnostic easter egg",
            "Procedure names such as `front`, `Front`, `FRONT`, and `fRoNt` match",
            "Matching is case-insensitive for this easter egg only",
            "current `docs/SUPPORTED_SYNTAX.md` as a tested current-reference document",
            "`docs/SUPPORTED_SYNTAX.md` is not an independent override",
            "External/API calls are not a future CALL target category",
            "External/API CALL targets are different. They are permanent project-boundary non-goal forms",
            "## 27. Implementation Guide Relationship",
            "This specification does not define the current milestone, next milestone, latest completed phase, active phase order, or phase acceptance status.",
            "Existing future phases must not be renumbered unless the project owner explicitly requests roadmap renumbering.",
            "## 28. Future Roadmap",
            "This roadmap names product capability areas.",
            "Roadmap items must remain inside the simulator boundary.",
            "native process execution, or full macro-system compatibility",
            "Implicit VM-internal memory accesses performed by simulator control-flow machinery",
            "accepted MASM32-compatible root RET default behavior",
            "`RETF`, stack-frame creation, and Irvine32 callable routine dispatch",
        ],
    )
    assert_all_text_not_contains(
        "docs/FULL_IMPLEMENTATION_SPEC.md",
        [
            "As of the Phase 69 source-of-truth revision",
            "current Phase 69 public output contract",
            "current source-of-truth revision after Phase 69C",
            "current Phase 69C source-run output-contract identifier",
            "The current Phase 69C identifier is:",
            "Phase 75 `PROC USES`; Phase 76 `LOCAL`; Phase 77 `PROTO`; Phase 78 `INVOKE`; Phase 79 `ADDR`; and Phase 80 Irvine32 callable routine dispatch",
            "Phase 70A is protocol/artifact compatibility cleanup only and must not implement root `RET` or any other MASM runtime behavior. If the guide uses corrective non-renumbering phases",
            "root `RET` termination, `RETF`, `LEAVE",
            "root `RET`, Irvine32 routine dispatch, and other CALL/RET forms remain future-owned",
            "Optional `the-front-fell-off` Diagnostic Easter Egg",
            "It is optional unless the owning phase explicitly makes it required",
        ],
    )

    assert_all_text_contains(
        "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md",
        [
            "Phase identifier note",
            "Every future milestone report must state both values when they differ. Use this minimum label format",
            "Current milestone:",
            "Minimum report wording when they differ",
            "This minimum block is required for maintenance, documentation, display-only, test-runner-only",
            "Creating a second active current-status block, keeping the old block and adding a new one above it, or appending a new phase paragraph below the old block is a defect",
            "Current-status blocks should answer only which current milestone is implemented",
            "The visible `web/index.html` top-page milestone banner is a file-specific compact banner",
            "Update it to the current accepted milestone even for maintenance or test-infrastructure milestones",
            "Current-status blocks contain only status values and one short interpretation sentence",
            "## 73A. Phase 69A - Documentation and Static-Check Cleanup After Direct CALL",
            "## 73B. Phase 69B - Register Display Grouping and Startup Diagnostic Ordering",
            "## 73C. Phase 69C - Wasm Output-Contract Compatibility and Test Runner Decomposition",
            "## 74. Phase 70 - RET Execution and Return Address Validation",
            "## 74A. Phase 70A - Runtime Metadata Exact-Match Compatibility Check",
            "## 74B. Phase 70B - Canonical Documentation Alignment and Compatibility Test Matrix Cleanup",
            "## 75. Phase 71 - Root Procedure Termination Semantics",
            "## 75A. Phase 71A - Optional Root RET Strictness Mode",
            "## 75A1. Phase 71A1 - Diagnostic Test Runner Subgroup Decomposition",
            "## 75B. Phase 71B - User-Facing Diagnostic Milestone-Wording Cleanup",
            "## 75B1. Phase 71B1 - Source-Run and Native Control-Flow Subgroup Preflight",
            "## 75B2. Phase 71B2 - Source-of-Truth Role Separation and Stale Milestone Context Cleanup",
            "## 75C. Phase 71C - Baseline Code-Stream Procedure Fallthrough and Code-End Runtime Diagnostic",
            "## 75D. Phase 71D - Configurable Procedure-Fallthrough Diagnostic Policy",
            "## 75E. Phase 71E - Entry-Procedure Auto-Stop Compatibility Setting",
            "## 75F. Phase 71F - Fallthrough Test Migration and Opposite Fixtures",
            "## 76. Phase 72 - Call Depth Limit and Call Trace Diagnostics",
            "## 77. Phase 73 - LEAVE Instruction",
            "## 78. Phase 74 - RET imm16 Instruction",
            "## 79. Phase 75 - PROC Metadata Baseline and Attribute Diagnostics",
            "## 80. Phase 76 - PROC USES Parsing and Metadata",
            "## 81. Phase 77 - PROC USES Runtime Save/Restore",
            "### Required easter egg",
            "This phase must add one deliberately harmless notice-level diagnostic easter egg",
            "Treat `front`, `Front`, `FRONT`, and `fRoNt` as matches",
            "Do not treat longer names such as `frontier`, `myfront`, or `front_` as matches",
            "the required `the-front-fell-off` easter egg is diagnostic-only, case-insensitive for the procedure name `front`, and fully tested",
            "Phase 70A is a UI/protocol/artifact compatibility corrective phase",
            "Missing suffix metadata is invalid even when the expected suffix is the empty string",
            "Malformed source-run output-contract metadata includes any present value that is not a string",
            "source-run output-contract token naming rule",
            "historical contract-version naming",
            "not the current repository milestone, not the current runtime/source-run MASM behavior phase",
            "Runtime/source-run behavior metadata problems must be reported before source-run output-contract metadata problems",
            "The RET return-token pop has the same internal/public distinction as the Phase 69 CALL return-token push",
            "Phase 71 must check selected-entry root RET eligibility before attempting the Phase 70 DWORD read from `[ESP]`",
            "invalid-root-termination-state",
            "do not claim checked-in `web/dist` is stale solely because `emcc` is unavailable",
        ],
    )
    assert_all_text_not_contains(
        "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md",
        [
            "### Optional easter egg",
            "The easter egg is optional",
            "If the easter egg is implemented",
            "source spelling is exactly `front`",
        ],
    )

    assert_all_text_contains(
        "docs/TESTING_GUIDE.md",
        [
            "Current milestone:",
            "Phase 77 - PROC USES Runtime Save/Restore",
            "Phase 77 adds tests for runtime `PROC USES` register save/restore",
            "ordered canonical register storage",
            "invalid register/list diagnostics",
            "stack-overflow",
            "stack-underflow",
            "regressions for Phase 74 `RET imm16`, Phase 73 `LEAVE`, Phase 72A `PUSH`/`POP`, helper `CALL`/plain `RET`, root `RET`, call-depth limits, procedure fallthrough, entry-end compatibility, and Irvine32 `exit`",
        ],
    )
    testing_status = read_repo_text("docs/TESTING_GUIDE.md").split("## 1. Prerequisites", 1)[0]
    for forbidden in [
        "When this section changes",
        "replace the existing status lines in place",
        "Do not append output-contract tokens",
    ]:
        if forbidden in testing_status:
            raise TestFailure(f"TESTING_GUIDE opening status text contains maintainer-facing update guidance: {forbidden}")

    assert_all_text_contains(
        "web/index.html",
        [
            "Milestone 77: PROC USES Runtime Save/Restore",
            "INCLUDE Irvine32.inc",
            ".stack 4096",
            "call Helper",
            "mov ebx, 1111h",
            "PROC USES ebx",
            "mov ebx, 2222h",
            "ret",
            "final-registers",
            "Program Console",
        ],
    )
    assert_all_text_not_contains(
        "web/index.html",
        [
            "Runtime behavior remains Phase 69:",
            "Milestone 69C: Wasm Output-Contract Compatibility and Test Runner Decomposition.</p>",
            "Repository status: Phase 71B",
            "Current milestone: Phase 71B",
            "Runtime/source-run MASM behavior remains Phase 71A",
            "Runtime/source-run MASM behavior phase:",
            "Protocol behavior: Phase 70A and later require runtime metadata and output-contract",
            "Source-run JSON now carries a separate output-contract identifier",
            "Final Registers still use parent-family spacer rows and major high-level divider rows",
            "direct user-procedure <code>CALL</code> is executable",
            "RET</code>, Irvine32 routine calls",
            "Milestone 70A: runtime metadata exact-match compatibility; MASM runtime behavior remains Phase 70 RET execution.",
            "Milestone 70B: documentation/static-check alignment",
        ],
    )

    assert_all_text_contains(
        "tests/web/test_protocol.mjs",
        [
            "SOURCE_RUN_OUTPUT_CONTRACT",
            "RUN_SOURCE accepts matching runtime and output-contract metadata",
            "RUN_SOURCE marks matching runtime phase with missing output-contract metadata",
            "RUN_SOURCE marks matching runtime phase with stale output-contract metadata",
            "RUN_SOURCE treats non-string output-contract metadata as missing",
            "RUN_SOURCE reports stale runtime phase and stale output contract distinctly",
            "RUN_SOURCE rejects newer runtime phase metadata by default",
            "RUN_SOURCE rejects missing runtime phase metadata",
            "RUN_SOURCE rejects missing runtime phase suffix metadata",
            "RUN_SOURCE rejects malformed runtime phase metadata",
            "RUN_SOURCE rejects malformed runtime phase suffix metadata",
        ],
    )
    assert_all_text_contains(
        "web/src/protocol.js",
        [
            "SOURCE_RUN_OUTPUT_CONTRACT",
            "stale-wasm-output-contract",
            "sourceRunOutputContract",
            "createMismatchedRuntimePhaseDiagnostic",
            "Number.isInteger(runResult.phase)",
            "IMPLEMENTED_PHASE = 77",
        ],
    )
    assert_all_text_not_contains(
        "src/core/vm_exec.c",
        [
            "source-level stack instructions, procedure frames, root RET",
        ],
    )
    assert_all_text_not_contains(
        "src/parser/parser.c",
        [
            "Source-level stack instructions, root RET",
        ],
    )
    assert_all_text_not_contains(
        "web/src/protocol.js",
        [
            "isNewerNumericPhase",
        ],
    )

    for path in ["docs/SUPPORTED_SYNTAX.md", "README.md", "docs/MILESTONE_HISTORY.md", "docs/BUILDING_AND_DEVELOPMENT.md"]:
        assert_all_text_not_contains(
            path,
            [
                "unsupported by the current milestone",
                "not implemented in this milestone",
                "not supported in this milestone",
                "current Phase 69C identifier",
                "current Phase 69C source-run output-contract identifier",
                "current source-run output-contract identifier value",
                "current contract identifier value",
            ],
        )

    for path in ["docs/FULL_IMPLEMENTATION_SPEC.md", "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md", "docs/TESTING_GUIDE.md"]:
        assert_all_text_not_contains(
            path,
            [
                "current Phase 69C identifier",
                "current Phase 69C source-run output-contract identifier",
                "current source-run output-contract identifier value",
                "current contract identifier value",
                "Phase 69C output-contract identifier unless the public source-run output contract actually changes",
                "Phase 69C output-contract behavior still passes",
            ],
        )

def assert_phase61b_watchdog_scope_documented() -> None:
    """Verify active-time watchdog history remains preserved outside README clutter."""

    assert_all_text_contains(
        "docs/MILESTONE_HISTORY.md",
        [
            "Phase 61B - Branch Runtime Watchdog Scope Cleanup",
            "Phase 59 - Control-Flow Instruction Limit",
            "Active-time watchdog behavior is not implemented in Phase 61, Phase 61A, or Phase 61B",
            "Phase 200 - Active Time Watchdog and Worker Responsiveness",
            "Runtime/source-run MASM behavior metadata remains Phase 61 - Direct JMP Runtime Execution.",
        ],
    )
    assert_all_text_contains(
        "docs/SUPPORTED_SYNTAX.md",
        [
            "instructionLimit",
            "instruction-limit-exceeded",
            "Active-time watchdog behavior is separate future work owned by Phase 200 - Active Time Watchdog and Worker Responsiveness.",
        ],
    )

    assert_all_text_not_contains(
        "README.md",
        [
            "Phase 61B - Branch Runtime Watchdog Scope Cleanup",
            "Active-time watchdog behavior is not part of Phase 61, Phase 61A, Phase 61B",
            "active-time-limit-exceeded",
        ],
    )
    assert_all_text_not_contains(
        "docs/BUILDING_AND_DEVELOPMENT.md",
        [
            "Phase 61B - Branch Runtime Watchdog Scope Cleanup",
            "Active-time watchdog behavior is not part of Phase 61, Phase 61A, Phase 61B",
            "active-time-limit-exceeded",
        ],
    )


def assert_phase61c_debugger_dependency_documented() -> None:
    """Verify direct-JMP debugger/editor dependency history is not in active README status."""

    assert_all_text_contains(
        "docs/SUPPORTED_SYNTAX.md",
        [
            "Phase 61C - Branch Debugger Dependency Cleanup",
            "direct-branch execution and debugger/editor behavior are separate systems",
            "does not implement debugger behavior",
            "breakpoint binding",
            "editor source navigation",
            "current-instruction highlighting",
            "CodeMirror gutter behavior",
            "branch-target editor highlighting",
        ],
    )
    assert_all_text_contains(
        "docs/MILESTONE_HISTORY.md",
        [
            "Phase 61C - Branch Debugger Dependency Cleanup",
            "Phase 61 direct-JMP runtime execution and later debugger/editor behavior are separate systems",
            "debugger stepping",
            "breakpoint binding",
            "editor source navigation",
            "current-instruction highlighting",
            "CodeMirror gutter behavior",
            "branch-target editor highlighting",
            "Runtime/source-run MASM behavior metadata remains Phase 61 - Direct JMP Runtime Execution.",
        ],
    )

    for path in ["README.md", "docs/BUILDING_AND_DEVELOPMENT.md"]:
        assert_all_text_not_contains(
            path,
            [
                "Phase 61C - Branch Debugger Dependency Cleanup",
                "Phase 61 implements debugger stepping",
                "Phase 61 implements breakpoint",
                "Phase 61A implements debugger stepping",
                "Phase 61A implements breakpoint",
                "Phase 61B implements debugger stepping",
                "Phase 61B implements breakpoint",
                "Phase 61C implements debugger stepping",
                "Phase 61C implements breakpoint",
                "executing `jmp` enables debugger source navigation",
                "direct `jmp` enables debugger source navigation",
            ],
        )


def assert_phase61d_capacity_documented() -> None:
    """Verify source-run capacity behavior remains documented outside README clutter."""

    assert_all_text_contains(
        "docs/SUPPORTED_SYNTAX.md",
        [
            "Phase 61D documents and tests source-run/parser capacity behavior",
            "### Parser and source-run capacity limits",
            "Parser/source-run capacity limits are separate from the runtime `instructionLimit` watchdog",
            "`token-capacity-exceeded`",
            "`source-text-capacity-exceeded`",
            "`instruction-capacity-exceeded`",
            "`code-label-capacity-exceeded`",
            "`symbol-capacity-exceeded`",
            "`diagnostic-capacity-exceeded`",
            "`data-capacity-exceeded`",
            "not MASM syntax errors unless malformed source also produced a syntax diagnostic",
            "not evidence that a runtime loop exceeded `instructionLimit`",
            "no Program Console output",
            "no `execution-complete` message",
            "Memory-region capacity limits are distinct from parser/source-run capacity",
            "Program Console output limits and Simulator Messages output limits are separate UI/result-surface concerns",
            "Worker/browser hard failures are not a supported diagnostic surface",
            "does not claim arbitrary large MASM program support",
        ],
    )
    assert_all_text_contains(
        "docs/MILESTONE_HISTORY.md",
        [
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
            "lexer token capacity",
            "parser diagnostic capacity",
            "instruction/source-text buffers",
            "data symbols",
            "code labels",
            "data image bytes",
            "source-run JSON/result buffers",
            "Runtime/source-run MASM behavior metadata remains Phase 61 - Direct JMP Runtime Execution.",
            "Phase 64 implements executable equality conditional jumps",
            "Phase 64A corrects source-run planned-read coverage",
        ],
    )
    assert_all_text_contains(
        "tests/core/test_wasm_source_run.c",
        [
            "test_phase61d_token_capacity_diagnostic_source_run_program",
            "test_phase61d_source_text_capacity_diagnostic_source_run_program",
            "test_phase61d_code_label_capacity_diagnostic_source_run_program",
            "instruction-limit-exceeded",
        ],
    )
    assert_all_text_contains(
        "tests/web/test_diagnostic_rendering.mjs",
        [
            "Phase 61D renders token capacity diagnostic exactly",
            "token-capacity-exceeded",
            "instruction-limit-exceeded",
        ],
    )

    for path in ["docs/SUPPORTED_SYNTAX.md", "README.md", "docs/MILESTONE_HISTORY.md", "docs/BUILDING_AND_DEVELOPMENT.md"]:
        assert_all_text_not_contains(
            path,
            [
                "capacity diagnostics prove arbitrary large MASM program support",
                "token-capacity-exceeded is an instruction-limit failure",
                "source-text-capacity-exceeded is an instruction-limit failure",
                "code-label-capacity-exceeded is an instruction-limit failure",
            ],
        )
    for path in ["README.md", "docs/BUILDING_AND_DEVELOPMENT.md"]:
        assert_all_text_not_contains(
            path,
            [
                "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening clarifies",
                "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening remains",
                "the documentation/static-check and regression-test hardening phase for parser/source-run capacity behavior",
            ],
        )

def assert_phase57m_segment_and_code_policy_documented() -> None:
    """Verify Phase 57M segment diagnostics and Phase 57L .code policy documentation."""

    phase57l_fragments = [
        "unsupported-code-memory-access",
        "unsupported-segment-symbol",
        "`.code` memory",
        "internal IR",
        "PE `.text`",
        "x86",
        "_TEXT",
        "_DATA",
        "_BSS",
        "CONST",
        "STACK",
        "DGROUP",
        "FLAT",
    ]
    assert_all_text_contains("docs/FULL_IMPLEMENTATION_SPEC.md", phase57l_fragments)
    assert_all_text_contains("docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md", phase57l_fragments)
    assert_all_text_contains("docs/SUPPORTED_SYNTAX.md", phase57l_fragments)
    assert_all_text_contains("docs/MILESTONE_HISTORY.md", ["Phase 57L - .CODE Memory Access Diagnostics", "unsupported-code-memory-access", "unsupported-segment-symbol", "_TEXT", "DGROUP", "Phase 57M implements targeted"])
    assert_all_text_not_contains(
        "README.md",
        [
            "unsupported-readable-code-image",
            "deterministic-simulator-code-image",
            "x86 opcode bytes are emitted",
        ],
    )
    assert_all_text_not_contains(
        "docs/SUPPORTED_SYNTAX.md",
        [
            "unsupported-readable-code-image",
            "deterministic-simulator-code-image",
            "x86 opcode bytes are emitted",
        ],
    )


def report_phase51_smoke_harness_status() -> None:
    """Report Phase 51 smoke-harness fixture coverage and browser-smoke status."""

    source_run_programs = [
        "phase51-layout-fixed-automatic-equivalence",
        "phase51-const-permission-precedence",
        "phase51-uninitialized-rmw-warning",
        "phase51-irvine-exit-lowercase",
        "phase51-irvine-exit-uppercase",
        "phase51-irvine-exit-mixed-case-with-casemap-none",
        "phase51-inc-dec-source-smoke",
        "phase51-and-or-xor-source-smoke",
        "phase51-not-source-smoke",
        "phase51-shl-sal-source-smoke",
        "phase51-shr-source-smoke",
        "phase51-sar-source-smoke",
        "phase51-rol-source-smoke",
        "phase51-ror-source-smoke",
    ]
    rendered_diagnostic_lines = [
        "automatic-layout instruction smoke",
        "CONST precedence",
        "uninitialized RMW warning",
        "uninitialized RMW completion",
        "INC/DEC ambiguous-memory-width",
        "AND/OR/XOR ambiguous-memory-width",
        "NOT ambiguous-memory-width",
        "SHL/SAL ambiguous-memory-width",
        "SHR ambiguous-memory-width",
        "SAR ambiguous-memory-width",
        "ROL ambiguous-memory-width",
        "ROR ambiguous-memory-width",
    ]

    print("Phase 51 source-run smoke programs exercised:")
    for program in source_run_programs:
        print(f"- {program}")

    print("Phase 51 expected rendered diagnostic line families:")
    for line in rendered_diagnostic_lines:
        print(f"- {line}")

    browser_smoke_status = os.environ.get("MASM32_BROWSER_MANUAL_SMOKE_AFTER_WASM_REBUILD")
    if browser_smoke_status:
        print(f"Phase 51 browser manual smoke after rebuilding Wasm: {browser_smoke_status}")
    elif shutil.which("emcc") is None:
        print("Phase 51 browser manual smoke after rebuilding Wasm: not run; emcc is unavailable in this environment.")
    else:
        print("Phase 51 browser manual smoke after rebuilding Wasm: not run by the aggregate native/Node test command.")

def documented_prefixed_subgroup_commands(prefix: str) -> list[str]:
    """Return documented subgroup commands that begin with a public prefix.

    Args:
        prefix: Public subgroup flag prefix without leading dashes.

    Returns:
        Ordered unique command names without leading dashes.
    """

    docs = read_file("docs/TESTING_GUIDE.md")
    commands: list[str] = []
    for raw_token in docs.replace("`", " ").replace("\n", " ").split():
        if not raw_token.startswith("--" + prefix):
            continue
        subgroup = raw_token[2:].rstrip(".,;:)")
        if subgroup in commands:
            continue
        commands.append(subgroup)
    return commands


def documented_diagnostic_subgroup_commands() -> list[str]:
    """Return diagnostic subgroup commands documented by TESTING_GUIDE.

    Returns:
        Ordered unique command names without leading dashes.
    """

    return documented_prefixed_subgroup_commands("diagnostics-")


def documented_native_subgroup_commands() -> list[str]:
    """Return native subgroup commands documented by TESTING_GUIDE.

    Returns:
        Ordered unique command names without leading dashes.
    """

    return documented_prefixed_subgroup_commands("native-")


def documented_source_run_subgroup_commands() -> list[str]:
    """Return source-run subgroup commands documented by TESTING_GUIDE.

    Returns:
        Ordered unique command names without leading dashes.
    """

    return documented_prefixed_subgroup_commands("source-run-")


def assert_subgroup_family_help_and_docs_match(
    family_name: str,
    subgroup_names: Sequence[str],
    documented_commands: Sequence[str],
) -> None:
    """Verify one subgroup family is present in both runner help and docs.

    Args:
        family_name: Human-readable subgroup family name for failure output.
        subgroup_names: Runner-owned subgroup command names without leading dashes.
        documented_commands: Documented subgroup command names without leading dashes.
    """

    help_text = create_arg_parser().format_help()
    docs = read_file("docs/TESTING_GUIDE.md")
    missing_from_help = [subgroup for subgroup in subgroup_names if "--" + subgroup not in help_text]
    missing_from_docs = [subgroup for subgroup in subgroup_names if "--" + subgroup not in docs]
    if missing_from_help:
        raise TestFailure(f"{family_name} subgroups missing from runner help: " + ", ".join(missing_from_help))
    if missing_from_docs:
        raise TestFailure(f"{family_name} subgroups missing from docs/TESTING_GUIDE.md: " + ", ".join(missing_from_docs))

    extra_in_docs = [subgroup for subgroup in documented_commands if subgroup not in subgroup_names]
    if extra_in_docs:
        raise TestFailure(f"docs/TESTING_GUIDE.md documents unknown {family_name} subgroups: " + ", ".join(extra_in_docs))
    missing_documented = [subgroup for subgroup in subgroup_names if subgroup not in documented_commands]
    if missing_documented:
        raise TestFailure(f"docs/TESTING_GUIDE.md omits {family_name} subgroups: " + ", ".join(missing_documented))


def assert_diagnostic_subgroup_help_and_docs_match() -> None:
    """Verify every diagnostic subgroup is present in both runner help and docs."""

    assert_subgroup_family_help_and_docs_match(
        "diagnostic",
        DIAGNOSTIC_SUBGROUPS,
        documented_diagnostic_subgroup_commands(),
    )


def assert_native_and_source_run_subgroup_help_and_docs_match() -> None:
    """Verify Phase 71B1 native and source-run subgroups match runner help and docs."""

    assert_subgroup_family_help_and_docs_match(
        "native",
        NATIVE_SUBGROUPS,
        documented_native_subgroup_commands(),
    )
    assert_subgroup_family_help_and_docs_match(
        "source-run",
        SOURCE_RUN_SUBGROUPS,
        documented_source_run_subgroup_commands(),
    )
    assert_all_text_not_contains(
        "docs/TESTING_GUIDE.md",
        [
            "Conditional source-run/native subgroups from Phase 71B1",
            "Preferred future subgroup families",
            "do not document any of these commands as implemented until `scripts/run_tests.py --help` exposes them",
        ],
    )


def assert_source_run_subgroup_inventory_table_present() -> None:
    """Verify the source-run C harness exposes all Phase 71B1 fixture families."""

    assert_all_text_contains(
        "tests/core/test_wasm_source_run.c",
        [
            "SourceRunTestFamily",
            "SourceRunTestCase",
            "SOURCE_RUN_TEST_CORE",
            "SOURCE_RUN_TEST_DIAGNOSTICS",
            "SOURCE_RUN_TEST_SETTINGS",
            "SOURCE_RUN_TEST_MEMORY_LAYOUT",
            "SOURCE_RUN_TEST_CONTROL_FLOW",
            "--group core|diagnostics|settings|memory-layout|control-flow",
            "--list-groups",
            "Source-run %s fixture-family tests passed",
        ],
    )


def diagnostic_rendering_inventory() -> dict[str, object]:
    """Read diagnostic-rendering subgroup inventory from the Node harness.

    Returns:
        Parsed JSON object with group names and test inventory.
    """

    require_node()
    completed = subprocess.run(
        ["node", "tests/web/test_diagnostic_rendering.mjs", "--list-diagnostic-tests"],
        cwd=ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        raise TestFailure(
            "could not list diagnostic rendering inventory:\n"
            + tail_text(completed.stdout)
            + tail_text(completed.stderr)
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise TestFailure("diagnostic rendering inventory is not JSON: " + str(error)) from error


def assert_diagnostic_subgroup_inventory_union() -> None:
    """Verify diagnostic subgroup inventory covers the same tests as broad diagnostics."""

    inventory = diagnostic_rendering_inventory()
    groups = inventory.get("groups")
    tests = inventory.get("tests")
    expected_groups = ["json", *DIAGNOSTIC_RENDERING_GROUP_BY_SUBGROUP.values()]
    if groups != expected_groups:
        raise TestFailure("diagnostic rendering group inventory does not match runner subgroups")
    if not isinstance(tests, list) or not tests:
        raise TestFailure("diagnostic rendering inventory is empty")

    all_names: set[str] = set()
    names_by_group: dict[str, set[str]] = {group: set() for group in expected_groups}
    for item in tests:
        if not isinstance(item, dict):
            raise TestFailure("diagnostic rendering inventory contains a non-object item")
        group = item.get("group")
        name = item.get("name")
        if not isinstance(group, str) or not isinstance(name, str):
            raise TestFailure("diagnostic rendering inventory item lacks group/name strings")
        if group not in names_by_group:
            raise TestFailure(f"diagnostic test has unsupported subgroup {group}: {name}")
        if name in all_names:
            raise TestFailure("diagnostic rendering inventory contains duplicate test name: " + name)
        all_names.add(name)
        names_by_group[group].add(name)

    empty_groups = [group for group, names in names_by_group.items() if not names]
    if empty_groups:
        raise TestFailure("diagnostic subgroups have no test inventory: " + ", ".join(empty_groups))

    subgroup_union: set[str] = set()
    for names in names_by_group.values():
        subgroup_union.update(names)
    if subgroup_union != all_names:
        raise TestFailure("diagnostic subgroup union does not match broad diagnostic inventory")


def assert_subgroup_failure_reporting_contract() -> None:
    """Verify subgroup subprocess failures identify the group, subgroup, and command."""

    global CURRENT_GROUP
    previous_group = CURRENT_GROUP
    CURRENT_GROUP = "diagnostics"
    try:
        try:
            run_command(
                [
                    sys.executable,
                    "-c",
                    "import sys; print('synthetic stdout'); print('synthetic stderr', file=sys.stderr); sys.exit(3)",
                ],
                subgroup="diagnostics-rendered-memory",
            )
        except TestFailure as error:
            text = str(error)
            for fragment in [
                "- Group: diagnostics",
                "- Subgroup: diagnostics-rendered-memory",
                "- Command:",
                "- Exit code: 3",
                "- Stdout tail:",
                "synthetic stdout",
                "- Stderr tail:",
                "synthetic stderr",
            ]:
                if fragment not in text:
                    raise TestFailure("synthetic subgroup failure output missing: " + fragment)
            return
        raise TestFailure("synthetic subgroup failure command unexpectedly passed")
    finally:
        CURRENT_GROUP = previous_group



def assert_phase71f_fallthrough_fixture_migration_checks() -> None:
    """Verify Phase 71F fallthrough fixture migration and opposite-fixture inventory."""

    source_run_text = read_file("tests/core/test_wasm_source_run.c")
    required_source_fragments = [
        "test_phase71f_fallthrough_opposite_fixtures_source_run",
        "Phase 71F default opposite fixture should serialize code-stream mode",
        "Phase 71F stop-at-entry-end opposite fixture should not execute later procedure text",
        "Phase 71F empty-main code-stream fixture should report code-fell-off-end",
        "Phase 71F empty-main stop-at-entry-end fixture should complete through named compatibility mode",
        "Phase 71F warn-policy opposite fixture should emit a warning",
        "Phase 71F off-policy opposite fixture should suppress only the fallthrough warning",
        "Phase 71F error-policy opposite fixture should stop before destination procedure instruction",
        "Phase 71F Irvine32 exit should terminate explicitly in code-stream mode",
        "Phase 71F Irvine32 exit should terminate explicitly in stop-at-entry-end mode",
        "Phase 71F compatible root RET should complete in code-stream mode",
        "Phase 71F strict root RET should reject in stop-at-entry-end mode",
    ]
    for fragment in required_source_fragments:
        if fragment not in source_run_text:
            raise TestFailure("Phase 71F source-run fixture inventory missing: " + fragment)

    active_test_paths = [
        "tests/core/test_wasm_source_run.c",
        "tests/core/test_vm_exec.c",
        "tests/web/test_diagnostic_rendering.mjs",
    ]
    forbidden_fixture_phrases = [
        "empty selected entry should complete",
        "empty selected entry source should complete",
        "empty selected entry should complete normally",
        "selected-entry fallthrough should emit exactly one completion",
        "should stop at selected entry ENDP boundary",
        "selected main fallthrough should complete normally",
        "implicit selected-entry ENDP success",
        "selected-entry ENDP default success",
    ]
    for path in active_test_paths:
        text = read_file(path)
        for phrase in forbidden_fixture_phrases:
            if phrase in text:
                raise TestFailure(f"Phase 71F stale selected-entry ENDP fixture wording remains in {path}: {phrase}")

    assert_all_text_contains(
        "docs/TESTING_GUIDE.md",
        [
            "Phase 71F opposite fixtures cover default `code-stream` fallthrough from `main` into a later procedure",
            "Phase 71F opposite fixtures cover empty selected-entry default `code-stream` `code-fell-off-end`",
            "Phase 71F opposite fixtures cover `procedureFallthroughPolicy` values `warn`, `off`, and `error`",
            "Phase 71F opposite fixtures cover explicit Irvine32 `exit` termination in both entry-end modes",
            "Phase 71F opposite fixtures cover root `RET` behavior as governed by `rootRetMode`, not by `entryProcedureEndMode`",
            "Phase 71F static checks reject active fixture descriptions that describe implicit selected-entry `ENDP` default success as current behavior",
        ],
    )


def assert_wasm_source_run_vm_storage_off_stack() -> None:
    """Verify browser source-run keeps the large VM object out of the Wasm call stack."""

    text = (ROOT / "src/wasm/wasm_api.c").read_text(encoding="utf-8")
    start_marker = "static const char *masm32_sim_wasm_run_source_json_internal_with_procedure_fallthrough_policy"
    end_marker = "/// Runs source with the default Phase 71D procedure-fallthrough warning policy."
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    body = text[start:end]
    if "/// VM instance used during source-run execution; stored with run storage to avoid overflowing the smaller browser Wasm stack." not in text:
        raise TestFailure("src/wasm/wasm_api.c must document why source-run VM storage is not stack-local")
    if "Vm *vm = &g_masm32_sim_wasm_run_storage.vm;" not in body:
        raise TestFailure("source-run Wasm API must use run-storage VM instead of a stack-local VM")
    if "\n    Vm vm;" in body:
        raise TestFailure("source-run Wasm API must not allocate Vm on the Wasm call stack")

def run_static_tests() -> None:
    """Run runner, documentation, and fixture-inventory consistency checks."""

    assert_help_lists_supported_flags()
    assert_parser_accepts_required_flags()
    assert_unknown_flag_fails()
    assert_docs_match_runner_groups()
    assert_fixture_inventory_documented()
    assert_timeout_policy_documented()
    assert_failure_reporting_contract_present()
    assert_native_and_source_run_subgroup_help_and_docs_match()
    assert_source_run_subgroup_inventory_table_present()
    assert_diagnostic_subgroup_help_and_docs_match()
    assert_diagnostic_subgroup_inventory_union()
    assert_subgroup_failure_reporting_contract()
    assert_live_text_avoids_milestone_relative_wording()
    assert_phase71b2_stale_milestone_context_checks()
    assert_current_status_and_harness_documented()
    assert_phase71f_fallthrough_fixture_migration_checks()
    assert_phase61b_watchdog_scope_documented()
    assert_phase61c_debugger_dependency_documented()
    assert_phase61d_capacity_documented()
    assert_wasm_source_run_vm_storage_off_stack()
    assert_phase57m_segment_and_code_policy_documented()
    if VERBOSE_OUTPUT:
        report_phase51_smoke_harness_status()


def group_function(name: str) -> Callable[[], None]:
    """Return the implementation function for one selected group.

    Args:
        name: User-facing group name.

    Returns:
        Callable that executes the group.
    """

    functions: dict[str, Callable[[], None]] = {
        "quick": run_quick_tests,
        "structure": run_structure_tests,
        "native": run_native_tests,
        "native-parser": run_native_parser_tests,
        "native-exec": run_native_exec_tests,
        "native-memory-layout": run_native_memory_layout_tests,
        "native-diagnostics-policy": run_native_diagnostics_policy_tests,
        "native-control-flow": run_native_control_flow_tests,
        "source-run": run_source_run_tests,
        "source-run-core": lambda: run_source_run_subgroup_tests("source-run-core"),
        "source-run-diagnostics": lambda: run_source_run_subgroup_tests("source-run-diagnostics"),
        "source-run-settings": lambda: run_source_run_subgroup_tests("source-run-settings"),
        "source-run-memory-layout": lambda: run_source_run_subgroup_tests("source-run-memory-layout"),
        "source-run-control-flow": lambda: run_source_run_subgroup_tests("source-run-control-flow"),
        "web": run_web_tests,
        "diagnostics": run_diagnostics_tests,
        "diagnostics-json": run_diagnostics_json_tests,
        "diagnostics-rendered-call-ret": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-call-ret"),
        "diagnostics-rendered-memory": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-memory"),
        "diagnostics-rendered-directives": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-directives"),
        "diagnostics-rendered-compatibility": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-compatibility"),
        "diagnostics-rendered-arithmetic": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-arithmetic"),
        "diagnostics-rendered-shift-rotate": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-shift-rotate"),
        "diagnostics-rendered-mul-div": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-mul-div"),
        "diagnostics-rendered-runtime": lambda: run_diagnostics_rendered_subgroup("diagnostics-rendered-runtime"),
        "protocol": run_protocol_tests,
        "static": run_static_tests,
    }
    return functions[name]


def main(argv: Sequence[str] | None = None) -> int:
    """Run selected implemented milestone test groups.

    Args:
        argv: Optional argument vector for tests; uses sys.argv when omitted.

    Returns:
        Zero on success, non-zero on failure.
    """

    global QUIET_OUTPUT, VERBOSE_OUTPUT
    parser = create_arg_parser()
    args = parser.parse_args(argv)
    QUIET_OUTPUT = args.quiet
    VERBOSE_OUTPUT = args.verbose
    selected_groups, full_verification = select_groups(args)

    if full_verification and argv is None and not args.all:
        print("Default test run is equivalent to --all. Use focused groups such as --source-run or --diagnostics if a hosted environment times out.")

    results: list[GroupResult] = []
    for group in selected_groups:
        results.append(run_group(group, group_function(group)))

    print_summary(results, selected_groups)

    if any(result.status == "FAIL" for result in results):
        return 1

    if full_verification:
        print("All implemented milestone tests passed.")
    elif selected_groups == ["quick"]:
        print("Quick smoke test groups passed. Full verification was not performed.")
    else:
        print("Selected test groups passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
