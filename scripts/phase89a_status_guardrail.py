#!/usr/bin/env python3
"""
@file phase89a_status_guardrail.py
@brief Validates the active Irvine32 documentation and metadata status.

The validator originated in Phase 89A and remains the active documentation/status
guardrail. It classifies files by role, evaluates bounded Markdown inventory
units, runs the focused positive/negative fixture matrix, and verifies that the
active runtime metadata matches the currently implemented Phase 90 WriteDec contract.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import pathlib
import re
from dataclasses import dataclass
from typing import Iterable, Sequence


EXPECTED_PHASE = 90
EXPECTED_PHASE_NAME = "Phase 90 - Irvine32 WriteDec"
EXPECTED_TOKEN = "phase-90-irvine32-writedec-contract-v1"
EXPECTED_DEFAULT_EDITOR_SOURCE_SHA256 = "e27f7cf8ef9ddab5d81bcadd6cf8dc06a190364edcbfa2b4bf683a0cfbb77fe5"
FIXTURE_MANIFEST = pathlib.Path("tests/static/phase89a/manifest.json")

DEFERRED_PATTERNS = (
    r"\bdefer(?:red|s|ring)?\b",
    r"\bfuture[- ]owned\b",
    r"\bfuture (?:simulator )?(?:work|features?)\b",
    r"\bnot yet executable\b",
    r"\bnot implemented\b",
    r"\bunimplemented\b",
    r"\bunsupported\b",
    r"\bdoes not support\b",
    r"\bnot supported\b",
    r"\bremain(?:s)? rejected\b",
    r"\bdoes not enable\b",
    r"\bnot executable\b",
    r"\bnot available\b",
    r"\bunavailable\b",
    r"\bdisabled\b",
    r"\bexclud(?:e|es|ed|ing)\b",
    r"\bdoes not include\b",
    r"\bnot included\b",
    r"\boutside (?:the )?supported subset\b",
    r"\brejected\b",
)
IMPLEMENTED_PATTERNS = (
    r"\bimplement(?:ed|s|ation)?\b",
    r"\bexecutable\b",
    r"\baccept(?:ed|s)?\b",
    r"\bsupport(?:ed|s)?\b",
    r"\benabl(?:e|ed|es)\b",
    r"\bavailable\b",
    r"\boffers?\b",
    r"\bprovides?\b",
    r"\bworks\b",
    r"\bcan be used\b",
    r"\busable\b",
)
INVENTORY_PATTERNS = (
    r"\boutput (?:routine|routines|set)\b",
    r"\broutine set\b",
    r"\bimplemented (?:routine|routines|form|forms|set)\b",
    r"\bsupported (?:routine|routines|form|forms|set)\b",
    r"\bcurrent (?:routine|routines|form|forms|set|output)\b",
    r"\bcurrent irvine32\b",
    r"\bdirect[- ]call exceptions?\b",
    r"\binclude irvine32\.inc (?:enables|provides|offers|supports)\b",
    r"\b(?:enabled|provided|offered|supported) by include irvine32\.inc\b",
    r"\b(?:after|with) include irvine32\.inc\b",
)
FUTURE_SIMULATOR_PATTERNS = (
    r"\bwrite(?:int|hex|bin)\b",
    r"\bnumeric output\b",
    r"\binput routines?\b",
    r"\bdebug routines?\b",
    r"\brandom routines?\b",
    r"\bfile routines?\b",
    r"\binvoke\s+(?:writestring|writechar)\b",
    r"\birvine32[^.;:]*\binvoke\b",
    r"\birvine32 routine dispatch beyond\b",
)
PERMANENT_NONGOAL_PATTERNS = (
    r"\bwinapi\b",
    r"\bwindows api\b",
    r"\bpe (?:loading|linking|loader|linker)\b",
    r"\bportable executable (?:loading|linking|loader|linker)\b",
    r"\bobject[- ]file linking\b",
    r"\bimport[- ]library\b",
    r"\bexternal(?:/api| api) execution\b",
    r"\bexternal native(?: library)?\b",
    r"\bhost callbacks?\b",
    r"\bhost file ?system\b",
    r"\bnative[- ]process\b",
    r"\bwindows[- ]process\b",
    r"\bfull x86\b",
)
COMBINED_CATEGORY_PHRASES = (
    "unsupported or deferred",
    "deferred or unsupported",
    "future-owned or permanent non-goals",
    "future owned or permanent non-goals",
    "deferred or non-goal",
    "deferred/non-goal",
)
COMBINED_CATEGORY_PATTERNS = (
    r"\bboth (?:planned|pending|future|deferred|unsupported)\b",
    r"\ball (?:planned|pending|future|deferred|unsupported)\b",
    r"\bplanned for later\b",
    r"\bpending features?\b",
    r"\bfuture features?\b",
)
SEPARATION_PATTERNS = (
    r"\bseparate(?:ly|d)?\b",
    r"\bdistinguish(?:ed|es|ing)?\b",
    r"\bwhereas\b",
    r"\bwhile\b",
    r"\brather than\b",
    r"\bnot ordinary\b",
    r"\bdifferent categor(?:y|ies)\b",
)


@dataclass(frozen=True)
class TextUnit:
    """One bounded Markdown text unit with source location."""

    text: str
    line_start: int
    line_end: int
    kind: str


@dataclass(frozen=True)
class ValidationIssue:
    """One deterministic guardrail violation."""

    code: str
    path: str
    line_start: int
    line_end: int
    message: str

    def format(self) -> str:
        """Return a compact repository-style diagnostic string."""

        location = f"{self.path}:{self.line_start}"
        if self.line_end != self.line_start:
            location += f"-{self.line_end}"
        return f"{location}: [{self.code}] {self.message}"


@dataclass(frozen=True)
class FixtureResult:
    """Outcome from one Phase 89A static fixture."""

    path: str
    expected: str
    actual: str
    issues: tuple[ValidationIssue, ...]


@dataclass(frozen=True)
class RepositorySlice:
    """One explicitly classified repository surface or bounded text slice."""

    path: str
    role: str
    validator: str
    start_marker: str | None = None
    end_marker: str | None = None
    expected_sha256: str | None = None
    require_match: bool = True


class GuardrailInputError(RuntimeError):
    """Raised when the fixture manifest or a repository surface is malformed."""


def normalize_markdown(text: str) -> str:
    """Normalize non-semantic Markdown presentation for status comparisons."""

    normalized = re.sub(r"<!--.*?-->", " ", text, flags=re.DOTALL)
    normalized = re.sub(r"\[([^\]]+)\]\([^\)]+\)", r"\1", normalized)
    normalized = re.sub(r"[`*_~]", "", normalized)
    normalized = normalized.replace("|", " ")
    normalized = re.sub(r"\s+", " ", normalized)
    return normalized.strip().lower()


def _is_table_separator(line: str) -> bool:
    """Return whether one Markdown line is a table separator row."""

    stripped = line.strip()
    return bool(stripped) and bool(re.fullmatch(r"\|?[\s:|-]+\|?", stripped)) and "-" in stripped


def _is_status_context_lead(text: str) -> bool:
    """Return whether a heading or lead paragraph supplies status to a following block."""

    normalized = normalize_markdown(text)
    if not normalized:
        return False
    return _matches_any(
        normalized,
        (
            r"\bcurrent\b",
            r"\bimplemented\b",
            r"\bsupported\b",
            r"\bavailable\b",
            r"\bfuture\b",
            r"\bdeferred\b",
            r"\bnon-goals?\b",
            r"\bproduct boundary\b",
            r"\broutines?\b",
            r"\bcapabilit(?:y|ies)\b",
            r"\bexact forms?\b",
        ),
    )


def markdown_units(text: str, *, line_offset: int = 0) -> list[TextUnit]:
    """Split Markdown into bounded paragraphs, lists, tables, and status blocks.

    Fenced code blocks are ignored because examples are not capability
    inventories unless a fixture expresses the assertion as ordinary prose.
    A heading or colon-terminated lead such as ``Future features include:`` is
    inherited by the immediately following list or table so category meaning
    cannot be detached from its items.
    """

    lines = text.splitlines()
    units: list[TextUnit] = []
    paragraph: list[str] = []
    paragraph_start = 0
    list_block: list[str] = []
    list_start = 0
    table_block: list[str] = []
    table_start = 0
    pending_context: str | None = None
    in_fence = False

    def contextual_text(value: str) -> str:
        """Prefix a block item with its immediately preceding status context."""

        if pending_context is None:
            return value
        return f"{pending_context} {value}"

    def flush_paragraph(end_index: int) -> None:
        """Append the current paragraph, if any, and update block context."""

        nonlocal paragraph, paragraph_start, pending_context
        if not paragraph:
            return
        paragraph_text = " ".join(part.strip() for part in paragraph)
        units.append(
            TextUnit(
                text=paragraph_text,
                line_start=paragraph_start + 1 + line_offset,
                line_end=end_index + line_offset,
                kind="paragraph",
            )
        )
        pending_context = paragraph_text if paragraph_text.rstrip().endswith(":") and _is_status_context_lead(paragraph_text) else None
        paragraph = []
        paragraph_start = 0

    def flush_list(end_index: int) -> None:
        """Append one contiguous contextual list block and clear its buffer."""

        nonlocal list_block, list_start, pending_context
        if not list_block:
            return
        block_text = ". ".join(contextual_text(part.strip().rstrip(".")) for part in list_block)
        units.append(
            TextUnit(
                text=block_text,
                line_start=list_start + 1 + line_offset,
                line_end=end_index + line_offset,
                kind="list-block",
            )
        )
        list_block = []
        list_start = 0
        pending_context = None

    def flush_table(end_index: int) -> None:
        """Append one contiguous contextual table block and clear its buffer."""

        nonlocal table_block, table_start, pending_context
        if not table_block:
            return
        block_text = ". ".join(contextual_text(part.strip().rstrip(".")) for part in table_block)
        units.append(
            TextUnit(
                text=block_text,
                line_start=table_start + 1 + line_offset,
                line_end=end_index + line_offset,
                kind="table-block",
            )
        )
        table_block = []
        table_start = 0
        pending_context = None

    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("```"):
            flush_paragraph(index)
            flush_list(index)
            flush_table(index)
            pending_context = None
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if not stripped:
            flush_paragraph(index)
            flush_list(index)
            flush_table(index)
            continue
        if stripped.startswith("#"):
            flush_paragraph(index)
            flush_list(index)
            flush_table(index)
            pending_context = None
            continue
        if stripped.startswith("|") and not _is_table_separator(stripped):
            flush_paragraph(index)
            flush_list(index)
            if not table_block:
                table_start = index
            table_block.append(stripped)
            units.append(
                TextUnit(
                    contextual_text(stripped),
                    index + 1 + line_offset,
                    index + 1 + line_offset,
                    "table-row",
                )
            )
            continue
        if _is_table_separator(stripped):
            flush_paragraph(index)
            flush_list(index)
            if table_block:
                table_block.append(stripped)
            continue
        if re.match(r"^(?:[-*+]\s+|\d+[.)]\s+)", stripped):
            flush_paragraph(index)
            flush_table(index)
            if not list_block:
                list_start = index
            item_text = re.sub(r"^(?:[-*+]\s+|\d+[.)]\s+)", "", stripped)
            list_block.append(item_text)
            units.append(
                TextUnit(
                    contextual_text(item_text),
                    index + 1 + line_offset,
                    index + 1 + line_offset,
                    "list-item",
                )
            )
            continue
        flush_list(index)
        flush_table(index)
        if not paragraph:
            pending_context = None
            paragraph_start = index
        paragraph.append(stripped)

    flush_paragraph(len(lines))
    flush_list(len(lines))
    flush_table(len(lines))
    return units


def _has_status_language(text: str) -> bool:
    """Return whether text contains implemented or deferred status language."""

    return _matches_any(text, IMPLEMENTED_PATTERNS) or _matches_any(text, DEFERRED_PATTERNS)


def _status_segments(text: str) -> list[str]:
    """Split one unit at sentence, contrast, and local exact-form boundaries."""

    initial = [
        segment.strip()
        for segment in re.split(
            r"(?<=[.!?;])\s+|\s*,?\s*\b(?:while|whereas|but|however|although)\b\s+",
            text,
        )
        if segment.strip()
    ]
    segments: list[str] = []
    exact_form = re.compile(r"\b(?:direct\s+)?(?:call\s+|invoke\s+)?(?:crlf|writechar|writestring|writedec)\b")
    exact_form_at_start = re.compile(r"^(?:direct\s+)?(?:call\s+|invoke\s+)?(?:crlf|writechar|writestring|writedec)\b")
    for candidate in initial:
        if re.search(r"\bbeyond (?:the )?implemented\b", candidate):
            segments.append(candidate)
            continue
        split_match = None
        for match in re.finditer(r"\s*,?\s+and\s+", candidate):
            left = candidate[:match.start()].strip()
            right = candidate[match.end():].strip()
            if (
                exact_form.search(left)
                and exact_form_at_start.search(right)
                and _has_status_language(left)
                and _has_status_language(right)
            ):
                split_match = match
                break
        if split_match is None:
            segments.append(candidate)
            continue
        left = candidate[:split_match.start()].strip()
        right = candidate[split_match.end():].strip()
        if left:
            segments.append(left)
        if right:
            segments.extend(_status_segments(right))
    return segments

def _contains_any(text: str, values: Iterable[str]) -> bool:
    """Return whether normalized text contains any supplied fragment."""

    return any(value in text for value in values)


def _matches_any(text: str, patterns: Iterable[str]) -> bool:
    """Return whether normalized text matches any supplied regular expression."""

    return any(re.search(pattern, text) is not None for pattern in patterns)


def _is_deferred_assertion(text: str) -> bool:
    """Return whether text affirmatively classifies a form as deferred or rejected."""

    candidate = re.sub(
        r"\bnot\s+(?:an?\s+)?(?:unsupported|deferred|rejected|unimplemented)(?:-feature)?\b",
        " ",
        text,
    )
    candidate = re.sub(r"\bdoes not remain rejected\b", " ", candidate)
    return _matches_any(candidate, DEFERRED_PATTERNS)


def _has_inventory_context(text: str) -> bool:
    """Return whether text presents a current implemented capability inventory."""

    return (
        _matches_any(text, INVENTORY_PATTERNS)
        or _contains_any(text, ("current", "currently", "at present", "available now", "today"))
        or " status " in f" {text} "
        or re.search(r"\b(?:consist(?:s)? of|compris(?:e|es)|include(?:s)?|contain(?:s)?)\b", text) is not None
    )


def _implemented_output_forms(text: str) -> set[str]:
    """Return Phase 89 output forms affirmatively implemented by one unit."""

    forms: set[str] = set()
    unit_inventory_context = _has_inventory_context(text)
    for segment in _status_segments(text):
        is_deferred = _is_deferred_assertion(segment)
        explicit_implemented = _matches_any(segment, IMPLEMENTED_PATTERNS) and not is_deferred
        implicit_inventory = (
            unit_inventory_context
            and not is_deferred
            and (
                _has_inventory_context(segment)
                or re.search(r"\b(?:yes|current|included)\b", segment) is not None
            )
        )
        if not explicit_implemented and not implicit_inventory:
            continue
        if re.search(r"\bcrlf\b", segment):
            forms.add("crlf")
        if re.search(r"\bwritechar\b", segment):
            if re.search(r"\b(?:call\s+writechar|direct\s+(?:call\s+)?writechar)\b", segment):
                forms.add("direct-writechar")
            elif not re.search(r"\binvoke\s+writechar\b", segment):
                forms.add("direct-writechar")
        if re.search(r"\bwritestring\b", segment):
            if re.search(r"\b(?:call\s+writestring|direct\s+(?:call\s+)?writestring)\b", segment):
                forms.add("direct-writestring")
            elif not re.search(r"\binvoke\s+writestring\b", segment):
                forms.add("direct-writestring")
        if re.search(r"\bwritedec\b", segment):
            if re.search(r"\b(?:call\s+writedec|direct\s+(?:call\s+)?writedec)\b", segment):
                forms.add("direct-writedec")
            elif not re.search(r"\binvoke\s+writedec\b", segment):
                forms.add("direct-writedec")
    return forms

def _looks_like_meta_test_instruction(text: str) -> bool:
    """Return whether text describes the validator rather than product status."""

    markers = (
        "positive fixture",
        "negative fixture",
        "fixture that",
        "fixture must",
        "must pass",
        "must fail",
        "validator must",
        "static checks must",
        "an active inventory",
        "historical phase 87",
        "historical phase 88",
        "active deferred direct-call table",
        "active file implies",
        "historical navigation",
    )
    return _contains_any(text, markers)


def validate_active_markdown(text: str, path: str, *, line_offset: int = 0) -> list[ValidationIssue]:
    """Validate all relevant bounded capability inventories in active text."""

    issues: list[ValidationIssue] = []
    for unit in markdown_units(text, line_offset=line_offset):
        normalized = normalize_markdown(unit.text)
        if not normalized or _looks_like_meta_test_instruction(normalized):
            continue

        clauses = _status_segments(normalized)
        implemented_forms = _implemented_output_forms(normalized)
        has_inventory_context = _has_inventory_context(normalized)
        phase89_inventory = {"crlf", "direct-writechar"}.issubset(implemented_forms) and has_inventory_context
        if phase89_inventory and "direct-writestring" not in implemented_forms:
            issues.append(
                ValidationIssue(
                    "missing-direct-writestring",
                    path,
                    unit.line_start,
                    unit.line_end,
                    "Current Irvine32 output inventory names Crlf and WriteChar but omits implemented direct WriteString.",
                )
            )
        phase90_inventory = {"crlf", "direct-writechar", "direct-writestring"}.issubset(implemented_forms) and has_inventory_context
        if phase90_inventory and "direct-writedec" not in implemented_forms:
            issues.append(
                ValidationIssue(
                    "missing-direct-writedec",
                    path,
                    unit.line_start,
                    unit.line_end,
                    "Current Irvine32 output inventory names Crlf, WriteChar, and WriteString but omits implemented direct WriteDec.",
                )
            )

        for clause in clauses:
            is_deferred = _is_deferred_assertion(clause)
            is_implemented = _matches_any(clause, IMPLEMENTED_PATTERNS) and not is_deferred
            qualifying_exception = _contains_any(
                clause,
                (
                    "beyond",
                    "except for",
                    "other than",
                    "outside the implemented",
                    "implemented direct call writestring",
                    "implemented direct call writedec",
                    "implemented direct call writechar",
                    "implemented call crlf",
                    "implemented invoke crlf",
                    "but invoke",
                ),
            )

            if is_deferred and not qualifying_exception and unit.kind not in {"list-block", "table-block"}:
                deferred_implemented_forms = (
                    (r"\bcall\s+crlf\b", "direct-crlf-deferred", "Implemented direct CALL Crlf"),
                    (r"\binvoke\s+crlf\b", "invoke-crlf-deferred", "Implemented zero-argument INVOKE Crlf"),
                    (r"\bcall\s+writechar\b|\bdirect\s+writechar\b", "direct-writechar-deferred", "Implemented direct CALL WriteChar"),
                    (r"\bcall\s+writestring\b|\bdirect\s+writestring\b", "direct-writestring-deferred", "Implemented direct CALL WriteString"),
                    (r"\bcall\s+writedec\b|\bdirect\s+writedec\b", "direct-writedec-deferred", "Implemented direct CALL WriteDec"),
                )
                for pattern, code, description in deferred_implemented_forms:
                    if re.search(pattern, clause):
                        issues.append(
                            ValidationIssue(
                                code,
                                path,
                                unit.line_start,
                                unit.line_end,
                                f"{description} is classified as deferred or unsupported.",
                            )
                        )
                if re.search(r"\b(?:virtual\s+|irvine32\s+)(?:irvine32\s+)?exit\b|(?<!\.)\bexit\b", clause):
                    issues.append(
                        ValidationIssue(
                            "virtual-exit-deferred",
                            path,
                            unit.line_start,
                            unit.line_end,
                            "Implemented zero-operand virtual Irvine32 exit is classified as deferred or unsupported.",
                        )
                    )

            if is_implemented and re.search(r"\binvoke\s+writestring\b", clause):
                issues.append(
                    ValidationIssue(
                        "invoke-writestring-implemented",
                        path,
                        unit.line_start,
                        unit.line_end,
                        "INVOKE WriteString is presented as implemented even though only direct CALL WriteString is implemented.",
                    )
                )
            if is_implemented and re.search(r"\binvoke\s+writechar\b", clause):
                issues.append(
                    ValidationIssue(
                        "invoke-writechar-implemented",
                        path,
                        unit.line_start,
                        unit.line_end,
                        "INVOKE WriteChar is presented as implemented even though only direct CALL WriteChar is implemented.",
                    )
                )
            if is_implemented and re.search(r"\binvoke\s+writedec\b", clause):
                issues.append(
                    ValidationIssue(
                        "invoke-writedec-implemented",
                        path,
                        unit.line_start,
                        unit.line_end,
                        "INVOKE WriteDec is presented as implemented even though only direct CALL WriteDec is implemented.",
                    )
                )

        for clause in clauses:
            if not (
                _matches_any(clause, FUTURE_SIMULATOR_PATTERNS)
                and _matches_any(clause, PERMANENT_NONGOAL_PATTERNS)
            ):
                continue
            combined_category = (
                _contains_any(clause, COMBINED_CATEGORY_PHRASES)
                or _matches_any(clause, COMBINED_CATEGORY_PATTERNS)
                or _is_deferred_assertion(clause)
            )
            if combined_category and not _matches_any(clause, SEPARATION_PATTERNS):
                issues.append(
                    ValidationIssue(
                        "combined-deferred-and-nongoal",
                        path,
                        unit.line_start,
                        unit.line_end,
                        "Future simulator work and permanent product non-goals are combined into one undifferentiated category.",
                    )
                )

    return _deduplicate_issues(issues)


def _deduplicate_issues(issues: Sequence[ValidationIssue]) -> list[ValidationIssue]:
    """Return issues with duplicate code/location/message tuples removed."""

    unique: list[ValidationIssue] = []
    seen: set[tuple[str, str, int, int, str]] = set()
    for issue in issues:
        key = (issue.code, issue.path, issue.line_start, issue.line_end, issue.message)
        if key not in seen:
            seen.add(key)
            unique.append(issue)
    return unique


def validate_metadata_payload(payload: object, path: str) -> list[ValidationIssue]:
    """Validate one synthetic active metadata/token payload."""

    if not isinstance(payload, dict):
        return [ValidationIssue("invalid-metadata-fixture", path, 1, 1, "Metadata fixture must be a JSON object.")]

    issues: list[ValidationIssue] = []
    phase = payload.get("phase")
    token = payload.get("token")
    phase_name = payload.get("phaseName", EXPECTED_PHASE_NAME)
    phase_suffix = payload.get("phaseSuffix", "")
    if phase != EXPECTED_PHASE:
        issues.append(
            ValidationIssue(
                "phase-token-mismatch",
                path,
                1,
                1,
                f"Active metadata phase must be {EXPECTED_PHASE}; found {phase!r}.",
            )
        )
    if token != EXPECTED_TOKEN:
        issues.append(
            ValidationIssue(
                "phase-token-mismatch",
                path,
                1,
                1,
                f"Phase {EXPECTED_PHASE} metadata must use token {EXPECTED_TOKEN}; found {token!r}.",
            )
        )
    if phase_name != EXPECTED_PHASE_NAME:
        issues.append(
            ValidationIssue(
                "phase-name-mismatch",
                path,
                1,
                1,
                f"Active runtime phase name must remain {EXPECTED_PHASE_NAME!r}; found {phase_name!r}.",
            )
        )
    if phase_suffix != "":
        issues.append(
            ValidationIssue(
                "phase-suffix-mismatch",
                path,
                1,
                1,
                f"Active runtime phase suffix must remain empty; found {phase_suffix!r}.",
            )
        )
    return issues


def validate_fixture(root: pathlib.Path, entry: dict[str, object]) -> FixtureResult:
    """Validate one manifest fixture according to its explicit file role."""

    relative_path = str(entry.get("path", ""))
    role = str(entry.get("role", ""))
    expected = str(entry.get("expect", ""))
    fixture_path = root / "tests" / "static" / "phase89a" / relative_path
    if not fixture_path.is_file():
        raise GuardrailInputError(f"missing Phase 89A fixture: {fixture_path}")
    if role not in {"active-current-status", "historical", "active-metadata", "future-token"}:
        raise GuardrailInputError(f"unsupported Phase 89A fixture role {role!r} for {relative_path}")
    if expected not in {"pass", "fail"}:
        raise GuardrailInputError(f"unsupported Phase 89A expected result {expected!r} for {relative_path}")

    if role == "historical":
        issues: list[ValidationIssue] = []
    elif role in {"active-metadata", "future-token"}:
        try:
            payload = json.loads(fixture_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            issues = [
                ValidationIssue(
                    "invalid-metadata-fixture",
                    str(fixture_path.relative_to(root)),
                    error.lineno,
                    error.lineno,
                    f"Invalid JSON fixture: {error.msg}.",
                )
            ]
        else:
            issues = validate_metadata_payload(payload, str(fixture_path.relative_to(root)))
            if role == "future-token" and isinstance(payload, dict):
                forbidden_keys = {f"phase{number}Token" for number in range(91, 96)}
                present = sorted(forbidden_keys.intersection(payload))
                if present:
                    issues.append(
                        ValidationIssue(
                            "future-token-premature",
                            str(fixture_path.relative_to(root)),
                            1,
                            1,
                            "Future Phase 91-95 runtime tokens must be absent before their owning phases: " + ", ".join(present),
                        )
                    )
    else:
        issues = validate_active_markdown(
            fixture_path.read_text(encoding="utf-8"),
            str(fixture_path.relative_to(root)),
        )

    return FixtureResult(
        path=relative_path,
        expected=expected,
        actual="fail" if issues else "pass",
        issues=tuple(issues),
    )


def load_phase89a_manifest(root: pathlib.Path) -> dict[str, object]:
    """Load and validate the top-level Phase 89A manifest object."""

    manifest_path = root / FIXTURE_MANIFEST
    if not manifest_path.is_file():
        raise GuardrailInputError(f"missing Phase 89A fixture manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schemaVersion") != 2:
        raise GuardrailInputError("Phase 89A fixture manifest must use schemaVersion 2")
    return manifest


def run_fixture_matrix(root: pathlib.Path) -> tuple[list[FixtureResult], list[str]]:
    """Run every manifest fixture and return outcomes plus contract failures."""

    manifest_path = root / FIXTURE_MANIFEST
    manifest = load_phase89a_manifest(root)
    entries = manifest.get("fixtures")
    if not isinstance(entries, list) or not entries:
        raise GuardrailInputError("Phase 89A fixture manifest must contain a non-empty fixtures array")

    results: list[FixtureResult] = []
    failures: list[str] = []
    listed_paths: set[str] = set()
    for raw_entry in entries:
        if not isinstance(raw_entry, dict):
            raise GuardrailInputError("Phase 89A fixture manifest entries must be objects")
        result = validate_fixture(root, raw_entry)
        results.append(result)
        listed_paths.add(result.path)
        expected_code = raw_entry.get("expectedCode")
        expected_line = raw_entry.get("expectedLine")
        if result.actual != result.expected:
            failures.append(
                f"fixture {result.path} expected {result.expected} but produced {result.actual}"
            )
            continue
        if result.expected == "fail":
            if not result.issues:
                failures.append(f"negative fixture {result.path} failed without a useful issue")
                continue
            matching_issues = [
                issue
                for issue in result.issues
                if (not isinstance(expected_code, str) or issue.code == expected_code)
                and (not isinstance(expected_line, int) or issue.line_start == expected_line)
            ]
            if not matching_issues:
                produced = sorted({(issue.code, issue.line_start) for issue in result.issues})
                failures.append(
                    f"negative fixture {result.path} expected one issue matching "
                    f"code={expected_code!r}, line={expected_line!r}, but produced {produced}"
                )

    fixture_dir = manifest_path.parent
    actual_paths = {
        str(path.relative_to(fixture_dir))
        for path in fixture_dir.iterdir()
        if path.is_file() and path.name != manifest_path.name
    }
    unlisted = sorted(actual_paths - listed_paths)
    if unlisted:
        failures.append("unlisted Phase 89A fixture files: " + ", ".join(unlisted))
    missing = sorted(listed_paths - actual_paths)
    if missing:
        failures.append("manifest entries without fixture files: " + ", ".join(missing))
    return results, failures


def _slice_text(text: str, surface: RepositorySlice) -> tuple[str, int]:
    """Extract one configured active repository slice and its line offset."""

    start = 0
    end = len(text)
    if surface.start_marker is not None:
        start = text.find(surface.start_marker)
        if start < 0:
            raise GuardrailInputError(f"{surface.path} is missing start marker: {surface.start_marker}")
    if surface.end_marker is not None:
        end = text.find(surface.end_marker, start)
        if end < 0:
            raise GuardrailInputError(f"{surface.path} is missing end marker: {surface.end_marker}")
    line_offset = text.count("\n", 0, start)
    return text[start:end], line_offset


def repository_surfaces_from_manifest(root: pathlib.Path) -> list[RepositorySlice]:
    """Return explicit active and historical repository-surface roles."""

    manifest = load_phase89a_manifest(root)
    raw_surfaces = manifest.get("repositorySurfaces")
    if not isinstance(raw_surfaces, list) or not raw_surfaces:
        raise GuardrailInputError("Phase 89A manifest must contain a non-empty repositorySurfaces array")

    surfaces: list[RepositorySlice] = []
    allowed_roles = {"active-current-status", "active-metadata", "historical"}
    allowed_validators = {"markdown", "status-shape", "metadata", "none"}
    for raw_surface in raw_surfaces:
        if not isinstance(raw_surface, dict):
            raise GuardrailInputError("Phase 89A repository surface entries must be objects")
        path = raw_surface.get("path")
        role = raw_surface.get("role")
        validator = raw_surface.get("validator", "none")
        if not isinstance(path, str) or not path:
            raise GuardrailInputError("Phase 89A repository surface path must be a non-empty string")
        if role not in allowed_roles:
            raise GuardrailInputError(f"unsupported Phase 89A repository surface role {role!r} for {path}")
        if validator not in allowed_validators:
            raise GuardrailInputError(f"unsupported Phase 89A repository validator {validator!r} for {path}")
        if role == "active-current-status" and validator not in {"markdown", "status-shape"}:
            raise GuardrailInputError(f"active current-status surface {path} needs markdown or status-shape validation")
        if role == "active-metadata" and validator != "metadata":
            raise GuardrailInputError(f"active metadata surface {path} must use metadata validation")
        if role == "historical" and validator != "none":
            raise GuardrailInputError(f"historical surface {path} must use validator 'none'")

        start_marker = raw_surface.get("startMarker")
        end_marker = raw_surface.get("endMarker")
        expected_sha256 = raw_surface.get("expectedSha256")
        require_match = raw_surface.get("requireMatch", True)
        if start_marker is not None and not isinstance(start_marker, str):
            raise GuardrailInputError(f"repository surface startMarker must be a string for {path}")
        if end_marker is not None and not isinstance(end_marker, str):
            raise GuardrailInputError(f"repository surface endMarker must be a string for {path}")
        if expected_sha256 is not None and not isinstance(expected_sha256, str):
            raise GuardrailInputError(f"repository surface expectedSha256 must be a string for {path}")
        if not isinstance(require_match, bool):
            raise GuardrailInputError(f"repository surface requireMatch must be boolean for {path}")
        surfaces.append(
            RepositorySlice(
                path=path,
                role=role,
                validator=validator,
                start_marker=start_marker,
                end_marker=end_marker,
                expected_sha256=expected_sha256,
                require_match=require_match,
            )
        )
    return surfaces


def validate_repository_surface_roles(root: pathlib.Path) -> list[ValidationIssue]:
    """Validate required active/historical roles and preserved historical hashes."""

    surfaces = repository_surfaces_from_manifest(root)
    required_active_paths = {
        "README.md",
        "docs/BUILDING_AND_DEVELOPMENT.md",
        "docs/FULL_IMPLEMENTATION_SPEC.md",
        "docs/SUPPORTED_SYNTAX.md",
        "docs/TESTING_GUIDE.md",
        "docs/MILESTONE_HISTORY.md",
        "web/index.html",
        "src/wasm/wasm_api.c",
        "web/src/protocol.js",
    }
    classified_active_paths = {
        surface.path
        for surface in surfaces
        if surface.role in {"active-current-status", "active-metadata"}
    }
    issues: list[ValidationIssue] = []
    for missing_path in sorted(required_active_paths - classified_active_paths):
        issues.append(
            ValidationIssue(
                "missing-active-role",
                str(FIXTURE_MANIFEST),
                1,
                1,
                f"Required active current-status surface is not explicitly classified: {missing_path}.",
            )
        )

    historical_patterns = {surface.path for surface in surfaces if surface.role == "historical"}
    for required_pattern in ("docs/history/reports/*.md", "docs/history/PROJECT_AUDIT_AND_HANDOFF_REPORT_*.md"):
        if required_pattern not in historical_patterns:
            issues.append(
                ValidationIssue(
                    "missing-historical-role",
                    str(FIXTURE_MANIFEST),
                    1,
                    1,
                    f"Required historical surface pattern is not explicitly classified: {required_pattern}.",
                )
            )

    for surface in surfaces:
        matches = sorted(path for path in root.glob(surface.path) if path.is_file())
        if surface.require_match and not matches:
            issues.append(
                ValidationIssue(
                    "missing-classified-surface",
                    surface.path,
                    1,
                    1,
                    "Explicitly classified repository surface did not match a file.",
                )
            )
            continue
        if surface.expected_sha256 is not None:
            if len(matches) != 1:
                issues.append(
                    ValidationIssue(
                        "historical-hash-target-count",
                        surface.path,
                        1,
                        1,
                        "Historical SHA-256 preservation checks require exactly one matched file.",
                    )
                )
                continue
            actual_sha256 = hashlib.sha256(matches[0].read_bytes()).hexdigest()
            if actual_sha256 != surface.expected_sha256:
                issues.append(
                    ValidationIssue(
                        "historical-report-rewritten",
                        str(matches[0].relative_to(root)),
                        1,
                        1,
                        f"Historical file SHA-256 changed: expected {surface.expected_sha256}, found {actual_sha256}.",
                    )
                )
    return issues


def validate_active_repository_markdown(root: pathlib.Path) -> list[ValidationIssue]:
    """Validate manifest-classified active current-status Markdown slices."""

    issues: list[ValidationIssue] = []
    for surface in repository_surfaces_from_manifest(root):
        if surface.role != "active-current-status" or surface.validator != "markdown":
            continue
        file_path = root / surface.path
        if not file_path.is_file():
            issues.append(
                ValidationIssue("missing-active-surface", surface.path, 1, 1, "Required active current-status surface is missing.")
            )
            continue
        text = file_path.read_text(encoding="utf-8")
        selected, line_offset = _slice_text(text, surface)
        issues.extend(validate_active_markdown(selected, surface.path, line_offset=line_offset))
    return _deduplicate_issues(issues)


def _extract_c_define(text: str, name: str, path: str) -> str:
    """Extract one quoted or integer C preprocessor definition."""

    match = re.search(rf"^#define\s+{re.escape(name)}\s+(.+?)\s*$", text, flags=re.MULTILINE)
    if match is None:
        raise GuardrailInputError(f"{path} is missing #define {name}")
    return match.group(1).strip()


def _extract_js_constant(text: str, name: str, path: str) -> str:
    """Extract one exported JavaScript constant value."""

    match = re.search(rf"^export const\s+{re.escape(name)}\s*=\s*(.+?);\s*$", text, flags=re.MULTILINE)
    if match is None:
        raise GuardrailInputError(f"{path} is missing export const {name}")
    return match.group(1).strip()


def _decode_literal(value: str) -> object:
    """Decode an integer or double-quoted source literal."""

    stripped = value.strip()
    if stripped.endswith("U") and stripped[:-1].isdigit():
        return int(stripped[:-1])
    if stripped.isdigit():
        return int(stripped)
    if stripped.startswith('"') and stripped.endswith('"'):
        return json.loads(stripped)
    return stripped


def validate_repository_metadata(root: pathlib.Path) -> list[ValidationIssue]:
    """Verify C/Wasm metadata and browser protocol metadata remain paired."""

    c_path = "src/wasm/wasm_api.c"
    js_path = "web/src/protocol.js"
    c_text = (root / c_path).read_text(encoding="utf-8")
    js_text = (root / js_path).read_text(encoding="utf-8")

    c_payload = {
        "phase": _decode_literal(_extract_c_define(c_text, "MASM32_SIM_WASM_RUNTIME_PHASE_NUMBER", c_path)),
        "phaseSuffix": _decode_literal(_extract_c_define(c_text, "MASM32_SIM_WASM_RUNTIME_PHASE_SUFFIX", c_path)),
        "phaseName": _decode_literal(_extract_c_define(c_text, "MASM32_SIM_WASM_RUNTIME_PHASE_NAME", c_path)),
        "token": _decode_literal(_extract_c_define(c_text, "MASM32_SIM_WASM_SOURCE_RUN_OUTPUT_CONTRACT", c_path)),
    }
    js_payload = {
        "phase": _decode_literal(_extract_js_constant(js_text, "IMPLEMENTED_PHASE", js_path)),
        "phaseSuffix": _decode_literal(_extract_js_constant(js_text, "IMPLEMENTED_PHASE_SUFFIX", js_path)),
        "phaseName": _decode_literal(_extract_js_constant(js_text, "IMPLEMENTED_PHASE_NAME", js_path)),
        "token": _decode_literal(_extract_js_constant(js_text, "SOURCE_RUN_OUTPUT_CONTRACT", js_path)),
    }

    issues = validate_metadata_payload(c_payload, c_path)
    issues.extend(validate_metadata_payload(js_payload, js_path))
    if c_payload != js_payload:
        issues.append(
            ValidationIssue(
                "active-metadata-disagreement",
                js_path,
                1,
                1,
                f"C/Wasm metadata {c_payload!r} disagrees with browser protocol metadata {js_payload!r}.",
            )
        )
    return _deduplicate_issues(issues)



def validate_default_editor_source(root: pathlib.Path) -> list[ValidationIssue]:
    """Verify that the browser default program matches the accepted current source."""

    manifest = load_phase89a_manifest(root)
    expected = manifest.get("defaultEditorSourceSha256")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise GuardrailInputError("Phase 89A manifest must define defaultEditorSourceSha256 as lowercase SHA-256")
    if expected != EXPECTED_DEFAULT_EDITOR_SOURCE_SHA256:
        raise GuardrailInputError(
            "status manifest defaultEditorSourceSha256 disagrees with the accepted Phase 90 default-source hash"
        )

    relative_path = "web/index.html"
    index_text = (root / relative_path).read_text(encoding="utf-8")
    match = re.search(r'<textarea\s+id="editor"[^>]*>(.*?)</textarea>', index_text, flags=re.DOTALL)
    if match is None:
        return [
            ValidationIssue(
                "missing-default-editor-source",
                relative_path,
                1,
                1,
                "Browser page is missing the default <textarea id=\"editor\"> source.",
            )
        ]
    source = html.unescape(match.group(1))
    actual = hashlib.sha256(source.encode("utf-8")).hexdigest()
    if actual == expected:
        return []
    line = index_text.count("\n", 0, match.start(1)) + 1
    return [
        ValidationIssue(
            "default-editor-source-changed",
            relative_path,
            line,
            line + source.count("\n"),
            f"Active status must use the Phase 90 default editor source SHA-256 {expected}; found {actual}.",
        )
    ]

def validate_required_status_shapes(root: pathlib.Path) -> list[ValidationIssue]:
    """Verify compact current project-status surfaces and runtime phase."""

    issues: list[ValidationIssue] = []
    required_fragments = {
        "README.md": (
            "| Current milestone | Phase 90 - Irvine32 WriteDec |",
            "| Runtime/source-run MASM behavior phase | Phase 90 - Irvine32 WriteDec |",
            "Phase 90 adds direct virtual Irvine32 `WriteDec` output while preserving the previously implemented Irvine32 output forms and keeping future routine forms deferred.",
        ),
        "docs/BUILDING_AND_DEVELOPMENT.md": (
            "Current milestone:\n\n- Phase 90 - Irvine32 WriteDec",
            "Runtime/source-run MASM behavior phase:\n\n- Phase 90 - Irvine32 WriteDec",
        ),
        "docs/SUPPORTED_SYNTAX.md": (
            "Current milestone:\n\n- Phase 90 - Irvine32 WriteDec",
            "Runtime/source-run MASM behavior phase:\n\n- Phase 90 - Irvine32 WriteDec",
        ),
        "docs/TESTING_GUIDE.md": (
            "Current milestone:\n\n- Phase 90 - Irvine32 WriteDec",
            "Runtime/source-run MASM behavior phase:\n\n- Phase 90 - Irvine32 WriteDec",
        ),
        "docs/MILESTONE_HISTORY.md": (
            "Latest recorded completed milestone in this history file:\nPhase 90 - Irvine32 WriteDec",
            "Latest recorded runtime/source-run MASM behavior phase in this history file:\nPhase 90 - Irvine32 WriteDec",
            "## Phase 90 - Irvine32 WriteDec",
        ),
        "web/index.html": (
            "Milestone 90: Irvine32 WriteDec",
        ),
    }
    for relative_path, fragments in required_fragments.items():
        text = (root / relative_path).read_text(encoding="utf-8")
        for fragment in fragments:
            if fragment not in text:
                issues.append(
                    ValidationIssue(
                        "missing-current-status",
                        relative_path,
                        1,
                        1,
                        f"Required current status fragment is missing: {fragment!r}.",
                    )
                )

    readme_status = (root / "README.md").read_text(encoding="utf-8").split("## Current simulator scope", 1)[0]
    if EXPECTED_TOKEN in readme_status:
        issues.append(
            ValidationIssue(
                "readme-status-too-detailed",
                "README.md",
                1,
                1,
                "README current-status block must not include output-contract tokens.",
            )
        )

    spec_text = (root / "docs/FULL_IMPLEMENTATION_SPEC.md").read_text(encoding="utf-8")
    diagnostic_start = spec_text.find("Diagnostic classification:")
    diagnostic_end = spec_text.find("#### 8.1.3A", diagnostic_start)
    if diagnostic_start < 0 or diagnostic_end < 0:
        raise GuardrailInputError("FULL_IMPLEMENTATION_SPEC diagnostic-classification subsection markers are missing")
    diagnostic_section = spec_text[diagnostic_start:diagnostic_end]
    if "Phase 90" in diagnostic_section:
        line = spec_text.count("\n", 0, diagnostic_start) + 1
        issues.append(
            ValidationIssue(
                "stable-spec-phase-label",
                "docs/FULL_IMPLEMENTATION_SPEC.md",
                line,
                line,
                "Stable diagnostic classification text must not embed a Phase 90 current-status label.",
            )
        )
    return issues


def validate_phase89a_repository(root: pathlib.Path, *, verbose: bool = False) -> list[str]:
    """Run the full Phase 89A fixture and active-repository guardrail."""

    results, fixture_failures = run_fixture_matrix(root)
    messages = list(fixture_failures)
    if verbose:
        for result in results:
            detail = ""
            if result.issues:
                detail = " -> " + "; ".join(issue.format() for issue in result.issues)
            print(f"Phase 89A fixture {result.path}: expected {result.expected}, actual {result.actual}{detail}")

    repository_issues: list[ValidationIssue] = []
    repository_issues.extend(validate_repository_surface_roles(root))
    repository_issues.extend(validate_active_repository_markdown(root))
    repository_issues.extend(validate_repository_metadata(root))
    repository_issues.extend(validate_default_editor_source(root))
    repository_issues.extend(validate_required_status_shapes(root))
    messages.extend(issue.format() for issue in _deduplicate_issues(repository_issues))
    return messages


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    """Parse command-line arguments for standalone guardrail execution."""

    parser = argparse.ArgumentParser(description="Validate Phase 89A documentation/status guardrails.")
    parser.add_argument("--root", default=".", help="Repository root to validate.")
    parser.add_argument("--verbose", action="store_true", help="Print every fixture outcome.")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    """Run the standalone Phase 89A guardrail command."""

    args = parse_arguments(argv)
    root = pathlib.Path(args.root).resolve()
    try:
        failures = validate_phase89a_repository(root, verbose=args.verbose)
    except (GuardrailInputError, json.JSONDecodeError, OSError) as error:
        print(f"phase89a_status_guardrail: {error}")
        return 1
    if failures:
        print("Phase 89A status guardrail failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("Phase 89A status guardrail passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
