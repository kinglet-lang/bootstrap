#!/usr/bin/env python3
"""Validate comment and copyright conventions on staged core source files."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

CORE_SUFFIXES = {".cc", ".h", ".c", ".kl"}

BOOTSTRAP_CORE_PREFIXES = (
    "src/ast/",
    "src/checker/",
    "src/compiler/",
    "src/lexer/",
    "src/module/",
    "src/parser/",
    "src/preen/",
    "src/types/",
    "src/ir/",
    "src/codegen/llvm/",
    "src/kinglet/main.",
    "src/kinglet/cli_driver.",
)

KINGLET_CORE_PREFIXES = (
    "compiler/",
    "parser/",
    "checker/",
    "lexer/",
    "core/",
    "ir/",
    "stdlib/manifest.kl",
)

BANNED_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"ADR\s+\d{4}"), "remove ADR references from source comments"),
    (re.compile(r"Amendment\s+20\d{2}"), "remove amendment dates from source comments"),
    (re.compile(r"\bPhase\s+[A-Z]\d*\b"), "remove plan phase labels (Phase A, Phase B, …)"),
    (re.compile(r"L\d+\s+—"), "remove layer labels (L1 —, L2 —, …)"),
    (re.compile(r"See ADR"), "remove ADR cross-references from source comments"),
    (re.compile(r"stdlib tree"), "remove planning notes from source comments"),
    (
        re.compile(r"LICENSE file in the root directory of this source tree"),
        "replace Meta-style license boilerplate with the SPDX header",
    ),
    (
        re.compile(r"\bPass\s+0[a-z]?\b"),
        "describe the compile step instead of Pass 0 / Pass 0b labels",
    ),
]

COPYRIGHT_RE = re.compile(
    r"Copyright\s*\(c\)\s*20\d{2}\s+Kinglet Language Developers",
    re.IGNORECASE,
)
SPDX_RE = re.compile(r"SPDX-License-Identifier:\s*MIT", re.IGNORECASE)
COMMENT_LINE_RE = re.compile(r"^\s*(?://|/\*|\*)")


def repo_root() -> Path:
    out = subprocess.check_output(
        ["git", "rev-parse", "--show-toplevel"],
        text=True,
    )
    return Path(out.strip())


def core_prefixes(root: Path) -> tuple[str, ...]:
    if (root / "src" / "compiler").is_dir():
        return BOOTSTRAP_CORE_PREFIXES
    if (root / "compiler").is_dir() and (root / "parser").is_dir():
        return KINGLET_CORE_PREFIXES
    return ()


def is_core_file(path: str, prefixes: tuple[str, ...]) -> bool:
    if not any(path.endswith(suffix) for suffix in CORE_SUFFIXES):
        return False
    return any(path.startswith(prefix) for prefix in prefixes)


def staged_files(root: Path) -> list[str]:
    out = subprocess.check_output(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
        cwd=root,
        text=True,
    )
    return [line.strip() for line in out.splitlines() if line.strip()]


def comment_lines(text: str) -> list[tuple[int, str]]:
    lines: list[tuple[int, str]] = []
    in_block = False
    for index, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if in_block:
            lines.append((index, line))
            if "*/" in stripped:
                in_block = False
            continue
        if stripped.startswith("/*"):
            lines.append((index, line))
            if "*/" not in stripped:
                in_block = True
            continue
        if stripped.startswith("//"):
            lines.append((index, line))
    return lines


def header_slice(text: str, line_count: int = 8) -> str:
    return "\n".join(text.splitlines()[:line_count])


def check_file(path: Path, prefixes: tuple[str, ...]) -> list[str]:
    rel = path.as_posix()
    if not is_core_file(rel, prefixes):
        return []

    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [f"{rel}: cannot read file ({exc})"]

    errors: list[str] = []

    for line_no, line in comment_lines(text):
        for pattern, hint in BANNED_PATTERNS:
            if pattern.search(line):
                errors.append(f"{rel}:{line_no}: banned comment pattern ({hint})")

    header = header_slice(text)
    if not COPYRIGHT_RE.search(header):
        errors.append(
            f"{rel}: missing copyright header "
            "(expected: Copyright (c) 20XX Kinglet Language Developers)"
        )
    if not SPDX_RE.search(header):
        errors.append(f"{rel}: missing SPDX-License-Identifier: MIT in file header")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "paths",
        nargs="*",
        help="optional file paths; defaults to staged files in the current repo",
    )
    args = parser.parse_args()

    root = repo_root()
    prefixes = core_prefixes(root)
    if not prefixes:
        print("check_comments: unknown repository layout", file=sys.stderr)
        return 1

    rel_paths = args.paths or staged_files(root)
    errors: list[str] = []
    for rel in rel_paths:
        errors.extend(check_file(root / rel, prefixes))

    if errors:
        print("Comment / header check failed:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        print(
            "\nSee scripts/hooks/README.md for the expected header and comment rules.",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
