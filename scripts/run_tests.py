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
import os
import pathlib
import shutil
import subprocess
import sys
from dataclasses import dataclass
from typing import Callable, Iterable, Sequence


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "tests"
REQUIRED_GROUPS = ["structure", "native", "source-run", "web", "diagnostics", "protocol", "static"]
MAX_FAILURE_TAIL_CHARS = 12000
VERBOSE_OUTPUT = False
QUIET_OUTPUT = False
CURRENT_GROUP = "unknown"


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


def run_command(command: list[str], *, cwd: pathlib.Path = ROOT, env: dict[str, str] | None = None) -> None:
    """Run one command and raise TestFailure when it fails.

    Successful subprocess output is captured to keep default and quiet runs
    compact. Verbose mode prints commands and captured output. Failure output is
    always shown with group, command, exit code, and bounded stdout/stderr tails.

    Args:
        command: Command and arguments to execute.
        cwd: Working directory for the subprocess.
        env: Optional environment variables for the subprocess.
    """

    command_text = format_command(command)
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
            "- Subgroup: n/a",
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
    assert_text_contains("README.md", "Phase 61 - Direct JMP Runtime Execution")
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
    assert_text_contains("web/src/protocol.js", "IMPLEMENTED_PHASE = 64")
    assert_text_contains("web/src/protocol.js", "IMPLEMENTED_PHASE_SUFFIX = \"A\"")
    assert_text_contains("web/src/protocol.js", "Phase 64A - Planned-Read Coverage Correction for Existing Memory-Reading Instructions")
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
    assert_text_contains("tests/core/test_wasm_source_run.c", "Source execution tests through Phase 64B simulator-message ordering passed.")
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
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "The default memory validation mode is region-only")
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


def compile_c_binary(output_name: str, sources: list[str]) -> pathlib.Path:
    """Compile one C test-support binary.

    Args:
        output_name: File name for the compiled executable.
        sources: Repository-relative C source files to compile.

    Returns:
        Path to the compiled executable.
    """

    compiler = os.environ.get("CC", "cc")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    output = executable_output_path(output_name)
    run_command([
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
    ])
    return output


def compile_and_run_c_test(output_name: str, sources: list[str]) -> None:
    """Compile and run one C unit test binary.

    Args:
        output_name: File name for the compiled test executable.
        sources: Repository-relative C source files to compile.
    """

    output = compile_c_binary(output_name, sources)
    run_command([str(output)])


def run_native_tests() -> None:
    """Compile and run native C tests except source-run integration tests."""

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
    )
    compile_and_run_c_test(
        "test_vm_cpu",
        [
            "tests/core/test_vm_cpu.c",
            "src/core/vm_cpu.c",
        ],
    )
    compile_and_run_c_test(
        "test_vm_flags",
        [
            "tests/core/test_vm_flags.c",
            "src/core/vm_cpu.c",
        ],
    )
    compile_and_run_c_test(
        "test_diagnostic_policy",
        [
            "tests/core/test_diagnostic_policy.c",
            "src/core/vm_diagnostic_policy.c",
        ],
    )
    compile_and_run_c_test(
        "test_vm_memory",
        [
            "tests/core/test_vm_memory.c",
            "src/core/vm_memory.c",
            "src/core/vm_layout.c",
        ],
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
    )

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
    )

    compile_and_run_c_test(
        "test_lexer",
        [
            "tests/core/test_lexer.c",
            "src/core/vm_cpu.c",
            "src/parser/lexer.c",
        ],
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
    )



def run_source_run_tests() -> None:
    """Compile and run native source-run integration coverage."""

    compile_and_run_c_test(
        "test_wasm_source_run",
        [
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
        ],
    )

def build_diagnostic_json_producer() -> None:
    """Build the native source-run JSON producer used by Node rendering tests."""

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


def run_diagnostics_tests() -> None:
    """Run native diagnostic JSON production plus rendered Simulator Messages tests."""

    require_node()
    build_diagnostic_json_producer()
    run_command(["node", "tests/web/test_diagnostic_rendering.mjs"], env=diagnostic_rendering_environment())


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
  --source-run   native source-run JSON/integration tests (currently one preserved binary)
  --web          browser-side Node formatter/settings tests that do not need native diagnostics
  --diagnostics  build native diagnostic JSON producer and run exact rendered Simulator Messages tests
  --protocol     worker/protocol schema tests
  --static       documentation, runner, group-name, and fixture-inventory consistency checks

Output modes:
  default         compact group status and final summary
  --quiet         group start/status lines, final summary, and failure details only
  --verbose       subprocess commands, captured subprocess output, and fixture inventory details

Notes:
  --quick is a smoke subset, not full verification.
  Source-run and diagnostic subgroups are intentionally not split in Phase 56A;
  rerun --source-run or --diagnostics independently when --all times out.

Windows examples:
  py scripts\\run_tests.py --all
  py scripts\\run_tests.py --diagnostics
""".strip(),
    )
    parser.add_argument("--all", action="store_true", help="run every focused group; default when no group is selected")
    parser.add_argument("--quick", action="store_true", help="run a smoke subset only; not sufficient for full milestone acceptance")
    parser.add_argument("--structure", action="store_true", help="run repository structure and static shape checks")
    parser.add_argument("--native", action="store_true", help="run native C tests excluding source-run integration")
    parser.add_argument("--source-run", action="store_true", help="run native source-run JSON/integration tests independently")
    parser.add_argument("--web", action="store_true", help="run browser-side Node tests that do not need native diagnostics")
    parser.add_argument("--diagnostics", action="store_true", help="run native diagnostic JSON plus exact rendered Simulator Messages tests")
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
        "--source-run",
        "--web",
        "--diagnostics",
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

    selected = [group for group in REQUIRED_GROUPS if getattr(args, group.replace("-", "_"))]
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
        "source-run": "source-run integration binary passed independently",
        "web": "browser-side Node module tests passed",
        "diagnostics": "native diagnostic producer and rendered messages passed",
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
    if "quick" in selected_groups:
        table_groups = ["quick", *table_groups]

    print("\nGroup        Status    Details")
    for group in table_groups:
        result = result_by_name.get(group)
        if result is None:
            print(f"{group:<12} NOT-RUN   not selected")
        else:
            print(f"{group:<12} {result.status:<8} {result.details}")

    if shutil.which("emcc") is None:
        print("Browser/Wasm rebuild smoke: SKIP - emcc unavailable in this environment.")
    else:
        print("Browser/Wasm rebuild smoke: NOT-RUN - not requested by this native/Node runner command.")


def assert_help_lists_supported_flags() -> None:
    """Verify that help output lists every public runner flag."""

    help_text = create_arg_parser().format_help()
    missing = [flag for flag in supported_help_flags() if flag not in help_text]
    if missing:
        raise TestFailure("help output is missing runner flags: " + ", ".join(missing))


def assert_parser_accepts_required_flags() -> None:
    """Verify that argparse accepts every required public flag."""

    parser = create_arg_parser()
    parser.parse_args(["--all"])
    parser.parse_args(["--quick"])
    parser.parse_args(["--quiet", "--structure"])
    parser.parse_args(["--verbose", "--diagnostics"])
    for group in REQUIRED_GROUPS:
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
    """Verify that the Phase 56A source-run fixture inventory exists."""

    assert_all_text_contains(
        "docs/TESTING_GUIDE.md",
        [
            "Source-run fixture inventory",
            "tests/core/test_wasm_source_run.c",
            "phase51-layout-fixed-automatic-equivalence",
            "phase53e-ui-settings-policy-routing",
            "phase56-div-source-run-coverage",
            "phase57-idiv-source-run-coverage",
            "kept whole for Phase 56A",
        ],
    )


def assert_timeout_policy_documented() -> None:
    """Verify that timeout-safe assistant verification policy is documented."""

    assert_all_text_contains(
        "docs/TESTING_GUIDE.md",
        [
            "If `python3 scripts/run_tests.py --all` times out or output is truncated in a hosted assistant/container environment, this is not automatically a project test failure.",
            "aggregate timed out in assistant/container environment, focused groups passed",
            "An assistant must not claim that the full aggregate suite passed unless the aggregate command actually completed",
            "group skipped because dependency unavailable, such as emcc",
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
        "current milestone",
        "this milestone",
        "unsupported by the current milestone",
        "not implemented in this milestone",
        "not supported in this milestone",
        "unsupported in this milestone",
        "future milestone",
        "will be added in a future milestone",
        "implemented through the current milestone",
        "current milestone metadata",
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
        for line_number, line in enumerate(lines, start=1):
            lowered = line.lower()
            if "expect_json_not_contains" in line or "assert_text_not_contains" in line:
                continue
            for phrase in forbidden_phrases:
                if phrase in lowered:
                    violations.append(f"{relative_path}:{line_number}: {phrase}")
                    break

    if violations:
        raise TestFailure("live milestone-relative wording found:\n" + "\n".join(violations))


def assert_phase64b_status_and_message_grouping_present() -> None:
    """Verify Phase 64B repository status, Phase 64A runtime metadata, and grouping docs/tests."""

    required_status_fragments = [
        "Repository/archive milestone:",
        "Phase 64B - Simulator Message Runtime Notice Ordering and Grouping",
        "Runtime/source-run MASM behavior phase:",
        "Phase 64A - Planned-Read Coverage Correction for Existing Memory-Reading Instructions",
    ]
    status_block = """Repository/archive milestone:
Phase 64B - Simulator Message Runtime Notice Ordering and Grouping

Runtime/source-run MASM behavior phase:
Phase 64A - Planned-Read Coverage Correction for Existing Memory-Reading Instructions"""
    for path in [
        "README.md",
        "docs/SUPPORTED_SYNTAX.md",
        "docs/MILESTONE_HISTORY.md",
        "docs/BUILDING_AND_DEVELOPMENT.md",
    ]:
        assert_all_text_contains(path, required_status_fragments)
        assert_text_contains(path, status_block)

    assert_all_text_contains(
        "README.md",
        [
            "docs/FULL_IMPLEMENTATION_SPEC.md",
            "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md",
            "docs/SUPPORTED_SYNTAX.md",
            "docs/TESTING_GUIDE.md",
            "docs/MILESTONE_HISTORY.md",
            "docs/BUILDING_AND_DEVELOPMENT.md",
            "`instructionLimit` watchdog",
            "`instruction-limit-exceeded`",
            "direct `jmp label` forms",
            "Direct JMP Runtime Execution",
            "CMP Memory Operand Forms",
            "`cmp reg, mem`, `cmp mem, reg`, and `cmp mem, imm`",
            "planned-read validation",
            "Reserved Word Symbol Diagnostics",
            "reserved-word-symbol",
            "`cmp`",
            "Phase 64 - Equality Conditional Jumps",
            "Phase 64A - Planned-Read Coverage Correction for Existing Memory-Reading Instructions",
            "Phase 64B - Simulator Message Runtime Notice Ordering and Grouping",
            "Simulator Messages now render `startup-state-notice`, runtime diagnostics, and `execution-complete` as adjacent logical groups",
            "Blank separators are formatter-only",
            "`OPTION CASEMAP:NONE` does not make reserved words available as user-defined symbols",
            "`OPTION NOKEYWORD` remains unsupported",
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening clarifies that parser/source-run capacity diagnostics",
            "`token-capacity-exceeded`",
            "`source-text-capacity-exceeded`",
            "`code-label-capacity-exceeded`",
            "`data-capacity-exceeded`",
            "debugger stepping, breakpoints, breakpoint binding, current-instruction highlighting, editor source navigation, CodeMirror gutter behavior, branch-target editor highlighting",
            "python3 -m http.server 8000 --directory web",
            "python3 scripts/run_tests.py --all",
            "./scripts/build_wasm.sh",
        ],
    )
    assert_all_text_not_contains(
        "README.md",
        [
            "Preserved diagnostic/code-policy highlights",
            "Phase 57T - Playground Program Diagnostic-Recovery Smoke Fixtures",
            "unsupported-high-level-if",
            "The repository state includes runtime MASM behavior implemented through",
            "Milestone 32 memory layout policy infrastructure",
            "Native diagnostic rendering harness",
            "## Notes for later milestones",
            "Visual Studio External Tools setup",
            "Visual Studio Makefile Project setup",
            "scripts\\windows\\serve_web.cmd",
            "scripts\\windows\\build_wasm.cmd",
        ],
    )
    assert_all_text_contains(
        "docs/MILESTONE_HISTORY.md",
        [
            "Phase 64 - Equality Conditional Jumps",
            "Phase 62 - CMP Register and Immediate Forms",
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
            "Phase 61C - Branch Debugger Dependency Cleanup",
            "Phase 61B - Branch Runtime Watchdog Scope Cleanup",
            "Phase 61 - Direct JMP Runtime Execution",
            "Current status at Phase 64B:",
            "Phase 64B changes rendered Simulator Messages ordering and group separators",
            "CMP memory reads participate in planned-read validation before flags are updated.",
            "Phase 64 implements executable equality conditional jumps",
            "Phase 64A corrects source-run planned-read coverage",
            "Phase 64B corrects rendered Simulator Messages grouping",
            "Capacity diagnostics such as `token-capacity-exceeded`, `source-text-capacity-exceeded`, `code-label-capacity-exceeded`, and `data-capacity-exceeded` are pre-runtime source-run failures",
            "Preserving branch source metadata and lowered target metadata does not implement debugger stepping, breakpoint binding, editor source navigation, current-instruction highlighting, CodeMirror gutter behavior, or branch-target editor highlighting.",
            "Phase 200 - Active Time Watchdog and Worker Responsiveness",
            "instructionLimit",
            "instruction-limit-exceeded",
            "Concise milestone ledger",
            "Detailed milestone report references",
            "Milestone reports, archived repository states, and this history file are historical evidence.",
            "They do not replace or override the canonical specification and implementation guide.",
        ],
    )
    assert_all_text_contains(
        "docs/BUILDING_AND_DEVELOPMENT.md",
        [
            "Phase 64 - Equality Conditional Jumps",
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
            "Phase 61C - Branch Debugger Dependency Cleanup",
            "Phase 61B - Branch Runtime Watchdog Scope Cleanup",
            "Phase 61 - Direct JMP Runtime Execution",
            "Phase 64B - Simulator Message Runtime Notice Ordering and Grouping",
            "Phase 200 - Active Time Watchdog and Worker Responsiveness",
            "Runtime/source-run MASM behavior phase:",
            "reserved-word-symbol",
            "CMP memory reads use checked helpers and planned-read validation",
            "existing read-modify-write memory destinations continue to route through planned-read validation",
            "Phase 64B changes rendered Simulator Messages ordering and group separators",
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening remains the documentation/static-check and regression-test hardening phase",
            "`token-capacity-exceeded`",
            "`source-text-capacity-exceeded`",
            "`code-label-capacity-exceeded`",
            "`data-capacity-exceeded`",
            "debugger stepping, breakpoints, breakpoint binding, current-instruction highlighting, editor source navigation, CodeMirror gutter behavior, branch-target editor highlighting",
            "python3 -m http.server 8000 --directory web",
            "./scripts/build_wasm.sh",
            "scripts\\windows\\build_wasm.cmd",
            "python3 scripts/run_tests.py --all",
            "python3 scripts/run_tests.py --source-run",
            "python3 scripts/run_tests.py --diagnostics",
            "missing `emcc`",
            "Browser/Wasm smoke guidance",
            "does not include a committed Visual Studio solution or project file",
            "Visual Studio External Tools",
        ],
    )

    assert_all_text_contains(
        "web/index.html",
        [
            "Milestone 64B: Simulator Messages now group startup notices, runtime diagnostics, and completion status",
            "Accepted syntax remains the Phase 64 equality-jump subset",
            "je equal",
            "jmp done",
            "done:",
            "final-registers",
            "Program Console",
        ],
    )
    assert_all_text_not_contains(
        "web/index.html",
        [
            "CMP memory operands remain deferred to Phase 63",
            "Milestone 63: CMP Memory Operand Forms",
            "Milestone 64A: Planned-Read Coverage Correction",
        ],
    )
    assert_all_text_contains(
        "docs/SUPPORTED_SYNTAX.md",
        [
            "Accepted MASM syntax remains the Phase 64 equality-jump subset",
            "Phase 64B changes rendered Simulator Messages ordering and blank-line group separators only",
            "Phase 64A remains the runtime/source-run MASM behavior phase",
            "### Simulator Messages grouping",
            "The renderer inserts exactly one blank line between adjacent non-empty startup, runtime-diagnostic, and final-status groups",
            "Those blank lines are formatter-only",
            "### Reserved words and user-defined symbols",
            "reserved-word-symbol",
            "`cmp`",
            "`cmp reg, mem`",
            "`cmp mem, reg`",
            "`cmp mem, imm`",
            "planned-read validation",
            "Phase 64 - Equality Conditional Jumps",
            "Phase 64A - Planned-Read Coverage Correction for Existing Memory-Reading Instructions",
            "Phase 64B - Simulator Message Runtime Notice Ordering and Grouping",
            "`OPTION CASEMAP:NONE` does not make reserved words available as user-defined symbols",
            "`OPTION NOKEYWORD` remains unsupported",
            "Phase 61D documents and tests source-run/parser capacity behavior",
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
            "## Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
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
            "Phase 64B corrects rendered Simulator Messages grouping",
        ],
    )
    assert_all_text_contains(
        "README.md",
        [
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening clarifies that parser/source-run capacity diagnostics",
            "`token-capacity-exceeded`",
            "runtime `instructionLimit` failures",
        ],
    )
    assert_all_text_contains(
        "docs/BUILDING_AND_DEVELOPMENT.md",
        [
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
            "the documentation/static-check and regression-test hardening phase for parser/source-run capacity behavior",
            "capacity diagnostics such as `token-capacity-exceeded`, `source-text-capacity-exceeded`, `code-label-capacity-exceeded`, and `data-capacity-exceeded`",
            "runtime `instructionLimit` watchdog",
        ],
    )
    assert_all_text_contains(
        "tests/core/test_wasm_source_run.c",
        [
            "test_phase61d_token_capacity_diagnostic_source_run_program",
            "test_phase61d_source_text_capacity_diagnostic_source_run_program",
            "test_phase61d_code_label_capacity_diagnostic_source_run_program",
            "test_phase64b_source_run_message_ordering_contract",
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
    assert_all_text_contains(
        "tests/web/test_formatters.mjs",
        [
            "formats Phase 64B startup, runtime warning, and completion groups",
            "startup-state-notice",
            "execution-complete",
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

def assert_phase61b_watchdog_scope_documented() -> None:
    """Verify Phase 61B keeps active-time watchdog behavior deferred to Phase 200."""

    required_boundary_fragments = [
        "Phase 61B",
        "direct `jmp label`",
        "Phase 59",
        "instruction-count watchdog",
        "Active-time watchdog behavior is not part of Phase 61, Phase 61A, Phase 61B",
        "Phase 200 - Active Time Watchdog and Worker Responsiveness",
    ]
    for path in [
        "README.md",
        "docs/SUPPORTED_SYNTAX.md",
        "docs/MILESTONE_HISTORY.md",
        "docs/BUILDING_AND_DEVELOPMENT.md",
    ]:
        assert_all_text_contains(path, required_boundary_fragments)

    assert_all_text_contains(
        "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md",
        [
            "## 65B. Phase 61B - Branch Runtime Watchdog Scope Cleanup",
            "Phase 61 direct-JMP execution must respect the existing Phase 59 - Control-Flow Instruction Limit instruction-count watchdog.",
            "Active-time watchdog behavior is not part of Phase 61, Phase 61A, or Phase 61B.",
            "Phase 200 - Active Time Watchdog and Worker Responsiveness",
        ],
    )

    forbidden_active_time_current_status_fragments = [
        "Phase 61 implements active-time watchdog",
        "Phase 61A implements active-time watchdog",
        "Phase 61B implements active-time watchdog",
        "Phase 61C implements active-time watchdog",
        "Phase 61D implements active-time watchdog",
        "active-time-limit-exceeded",
    ]
    for path in [
        "README.md",
        "docs/SUPPORTED_SYNTAX.md",
        "docs/MILESTONE_HISTORY.md",
        "docs/BUILDING_AND_DEVELOPMENT.md",
    ]:
        assert_all_text_not_contains(path, forbidden_active_time_current_status_fragments)


def assert_phase61c_debugger_dependency_documented() -> None:
    """Verify Phase 61C keeps direct-JMP runtime separate from debugger/editor behavior."""

    required_current_status_fragments = [
        "Phase 61C - Branch Debugger Dependency Cleanup",
        "Phase 61 - Direct JMP Runtime Execution",
        "debugger/editor",
        "does not implement debugger",
        "breakpoint binding",
        "editor source navigation",
        "current-instruction highlighting",
        "CodeMirror gutter behavior",
        "branch-target editor highlighting",
    ]
    for path in [
        "README.md",
        "docs/SUPPORTED_SYNTAX.md",
        "docs/MILESTONE_HISTORY.md",
        "docs/BUILDING_AND_DEVELOPMENT.md",
    ]:
        assert_all_text_contains(path, required_current_status_fragments)

    assert_all_text_contains(
        "docs/INCREMENTAL_IMPLEMENTATION_GUIDE.md",
        [
            "## 65C. Phase 61C - Branch Debugger Dependency Cleanup",
            "direct-JMP runtime execution and debugger/editor behavior are separate systems",
            "Breakpoint binding to target line remains valid in later debugger tests.",
            "future debugger regression requirement, not as a Phase 61 implementation or acceptance requirement",
            "direct-JMP target labels from Phase 60 and direct-JMP runtime transfer from Phase 61 are part of the branch/source-map regression corpus for debugger phases",
            "breakpoint binding to a branch target line is tested when breakpoint binding exists",
            "branch target highlighting and source navigation are tested when editor navigation exists",
            "those later debugger/editor tests must not be backported into Phase 61",
            "Phase 61C branch-target regression note: direct-JMP target labels from Phase 60 - Direct JMP Parsing and Target Lowering and direct-JMP runtime transfer from Phase 61 - Direct JMP Runtime Execution are part of the branch/source-map regression corpus for debugger phases.",
            "Branch target highlighting and source navigation are tested only when editor navigation exists; those tests must not be backported into Phase 61.",
            "branch target gutter binding for direct-JMP labels from Phase 60 - Direct JMP Parsing and Target Lowering and direct-JMP runtime transfer from Phase 61 - Direct JMP Runtime Execution belongs to this future editor/debugger binding phase, not to Phase 61.",
        ],
    )

    phase61_current_docs = [
        "README.md",
        "docs/SUPPORTED_SYNTAX.md",
        "docs/MILESTONE_HISTORY.md",
        "docs/BUILDING_AND_DEVELOPMENT.md",
    ]
    forbidden_current_status_fragments = [
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
    ]
    for path in phase61_current_docs:
        assert_all_text_not_contains(path, forbidden_current_status_fragments)


def assert_phase61d_capacity_documented() -> None:
    """Verify Phase 61D documents source-run capacity behavior separately from runtime limits."""

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
            "## Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
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
        "README.md",
        [
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening clarifies that parser/source-run capacity diagnostics",
            "`token-capacity-exceeded`",
            "runtime `instructionLimit` failures",
        ],
    )
    assert_all_text_contains(
        "docs/BUILDING_AND_DEVELOPMENT.md",
        [
            "Phase 61D - Source-Run Capacity Documentation and Diagnostic Hardening",
            "the documentation/static-check and regression-test hardening phase for parser/source-run capacity behavior",
            "capacity diagnostics such as `token-capacity-exceeded`, `source-text-capacity-exceeded`, `code-label-capacity-exceeded`, and `data-capacity-exceeded`",
            "runtime `instructionLimit` watchdog",
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

def run_static_tests() -> None:
    """Run runner, documentation, and fixture-inventory consistency checks."""

    assert_help_lists_supported_flags()
    assert_parser_accepts_required_flags()
    assert_unknown_flag_fails()
    assert_docs_match_runner_groups()
    assert_fixture_inventory_documented()
    assert_timeout_policy_documented()
    assert_failure_reporting_contract_present()
    assert_live_text_avoids_milestone_relative_wording()
    assert_phase64b_status_and_message_grouping_present()
    assert_phase61b_watchdog_scope_documented()
    assert_phase61c_debugger_dependency_documented()
    assert_phase61d_capacity_documented()
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
        "source-run": run_source_run_tests,
        "web": run_web_tests,
        "diagnostics": run_diagnostics_tests,
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
