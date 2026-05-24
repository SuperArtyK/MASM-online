#!/usr/bin/env python3
"""
@file run_tests.py
@brief Runs command-line tests for the MASM32 simulator skeleton milestones.

The runner compiles C tests with the host C compiler and runs JavaScript
protocol tests with Node.js when available. It also performs lightweight
repository-structure checks for the implemented milestone skeleton.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess
import sys
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "tests"


class TestFailure(RuntimeError):
    """Raised when a command-line test step fails."""


def run_command(command: list[str], *, cwd: pathlib.Path = ROOT, env: dict[str, str] | None = None) -> None:
    """Run one command and raise TestFailure when it fails.

    Args:
        command: Command and arguments to execute.
        cwd: Working directory for the subprocess.
        env: Optional environment variables for the subprocess.
    """

    print("$ " + " ".join(command))
    completed = subprocess.run(command, cwd=cwd, env=env, check=False)
    if completed.returncode != 0:
        raise TestFailure(f"command failed with exit code {completed.returncode}: {' '.join(command)}")


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
    assert_text_contains("scripts/build_wasm.sh", "lexer.c")
    assert_text_contains("scripts/build_wasm.sh", "parser.c")
    assert_text_contains("scripts/build_wasm.sh", "symbols.c")
    assert_text_contains("scripts/build_wasm.sh", "object_map.c")
    assert_text_contains("scripts/build_wasm.sh", "src/parser")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_milestone4_hardcoded_result")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_run_source_json")
    assert_text_contains("scripts/build_wasm.sh", "_masm32_sim_wasm_run_source_json_with_ui_settings")
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
    assert_text_contains("scripts/windows/build_wasm.cmd", "lexer.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "parser.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "symbols.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "object_map.c")
    assert_text_contains("scripts/windows/build_wasm.cmd", "src\\parser")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_milestone4_hardcoded_result")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_run_source_json")
    assert_text_contains("scripts/windows/build_wasm.cmd", "_masm32_sim_wasm_run_source_json_with_ui_settings")
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
    assert_text_contains("src/parser/parser.c", "Unsupported feature: INVOKE is not supported yet; use CALL when available.")
    assert_text_contains("src/parser/parser.c", "Unsupported feature: MASM macro definitions are not supported yet.")
    assert_text_contains("README.md", "Milestone 37")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "through Milestone 55")
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
    assert_text_contains("web/src/protocol.js", "IMPLEMENTED_PHASE = 55")
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
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase51_fixed_and_automatic_layout_smoke_harness")
    assert_text_contains("tests/core/test_wasm_source_run.c", "test_phase51_instruction_family_source_run_smoke_harness")
    assert_text_contains("tests/core/test_wasm_source_run.c", "Source execution tests through Phase 55 IMUL coverage passed.")
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
    assert_text_contains("README.md", "Native diagnostic rendering harness")
    assert_text_contains("README.md", "Milestone 32 memory layout policy infrastructure")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "Milestone 32 adds fixed memory-layout policy infrastructure only")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "casemap-policy-changed")
    assert_text_contains("docs/SUPPORTED_SYNTAX.md", "ambiguous-symbol")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_CASEMAP_POLICY_CHANGED")
    assert_text_contains("src/parser/parser.h", "VM_PARSER_DIAGNOSTIC_AMBIGUOUS_SYMBOL")
    assert_text_not_contains("src/core/vm_memory.c", "0x00400000U")
    assert_text_not_contains("src/core/vm_memory.c", "0x00500000U")
    assert_text_not_contains("src/core/vm_exec.c", "0x00500000U")
    print("Milestone structure tests passed.")


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


def run_c_tests() -> None:
    """Compile and run C unit tests for implemented milestones."""

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
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
        ],
    )

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
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
            "src/wasm/wasm_api.c",
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
            "src/parser/lexer.c",
            "src/parser/parser.c",
            "src/parser/symbols.c",
            "src/parser/object_map.c",
            "src/wasm/wasm_api.c",
        ],
    )

def run_js_tests() -> None:
    """Run JavaScript and Node-based diagnostic rendering tests."""

    if shutil.which("node") is None:
        raise TestFailure("node is required for JavaScript and diagnostic rendering tests")
    run_command(["node", "tests/web/test_protocol.mjs"])
    run_command(["node", "tests/web/test_settings.mjs"])
    run_command(["node", "tests/web/test_collapsible_settings.mjs"])
    run_command(["node", "tests/web/test_formatters.mjs"])

    diagnostic_env = os.environ.copy()
    for key in [
        "MASM32_DIAGNOSTIC_MEMORY_VALIDATION",
        "MASM32_DIAGNOSTIC_LAYOUT_MODE",
        "MASM32_DIAGNOSTIC_AUTO_DATA_LIMIT",
        "MASM32_DIAGNOSTIC_AUTO_STACK_LIMIT",
        "MASM32_DIAGNOSTIC_AUTO_HEAP_REQUEST",
        "MASM32_DIAGNOSTIC_AUTO_HEAP_LIMIT",
        "MASM32_DIAGNOSTIC_AUTO_TOTAL_LIMIT",
    ]:
        diagnostic_env.pop(key, None)
    diagnostic_env["MASM32_DIAGNOSTIC_JSON_PRODUCER"] = str(executable_output_path("diagnostic_json_producer").resolve())
    run_command(["node", "tests/web/test_diagnostic_rendering.mjs"], env=diagnostic_env)



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
    ambiguous_memory_width = "[assembly-error] ambiguous-memory-width line 3, column 9, byte offset 24, span length 1: Memory operand width is ambiguous. Use BYTE PTR, WORD PTR, or DWORD PTR."
    rendered_diagnostic_lines = [
        ("automatic-layout instruction smoke", "[info] execution-complete: Execution completed successfully."),
        ("CONST precedence", "[runtime-error] permission-denied line 6: Memory write at 00600000h for 4 bytes is not permitted in .const."),
        ("uninitialized RMW warning", "[simulator-warning] uninitialized-read line 5: Memory read range 00500000h..00500003h reads 4 bytes from x + 0; 4 of those bytes still originated from uninitialized storage."),
        ("uninitialized RMW completion", "[info] execution-complete: Execution completed successfully."),
        ("INC/DEC", ambiguous_memory_width),
        ("AND/OR/XOR", ambiguous_memory_width),
        ("NOT", ambiguous_memory_width),
        ("SHL/SAL", ambiguous_memory_width),
        ("SHR", ambiguous_memory_width),
        ("SAR", ambiguous_memory_width),
        ("ROL", ambiguous_memory_width),
        ("ROR", ambiguous_memory_width),
    ]

    print("Phase 51 source-run smoke programs exercised:")
    for program in source_run_programs:
        print(f"- {program}")

    print("Phase 51 expected rendered diagnostic lines:")
    for label, line in rendered_diagnostic_lines:
        print(f"- {label}: {line}")

    browser_smoke_status = os.environ.get("MASM32_BROWSER_MANUAL_SMOKE_AFTER_WASM_REBUILD")
    if browser_smoke_status:
        print(f"Phase 51 browser manual smoke after rebuilding Wasm: {browser_smoke_status}")
    elif shutil.which("emcc") is None:
        print("Phase 51 browser manual smoke after rebuilding Wasm: not run; emcc is unavailable in this environment.")
    else:
        print("Phase 51 browser manual smoke after rebuilding Wasm: not run by the aggregate native/Node test command.")

def main() -> int:
    """Run all implemented milestone tests.

    Returns:
        Zero on success, non-zero on failure.
    """

    try:
        run_structure_tests()
        run_c_tests()
        build_diagnostic_json_producer()
        run_js_tests()
        report_phase51_smoke_harness_status()
    except TestFailure as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print("All implemented milestone tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
