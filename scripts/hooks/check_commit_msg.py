#!/usr/bin/env python3
"""Validate commit message format."""

from __future__ import annotations

import re
import sys
from pathlib import Path

CONVENTIONAL_RE = re.compile(
    r"^(feat|fix|docs|style|refactor|perf|test|chore|build|ci|revert)"
    r"(\([a-z0-9_,./-]+\))?: .{4,}$"
)
PHASE_RE = re.compile(r"\bPhase\s+[A-Z]\d*\b")
ADR_RE = re.compile(r"\bADR\s+\d{4}\b")


def read_message(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    # Keep only the subject line for the primary check.
    return text.splitlines()[0].strip() if text else ""


def is_exempt(subject: str) -> bool:
    if not subject:
        return False
    if subject.startswith("Merge "):
        return True
    if subject.startswith("fixup!") or subject.startswith("squash!"):
        return True
    if subject.startswith("Revert "):
        return True
    return False


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_commit_msg.py <commit-msg-file>", file=sys.stderr)
        return 2

    subject = read_message(Path(sys.argv[1]))
    if is_exempt(subject):
        return 0

    errors: list[str] = []
    if not CONVENTIONAL_RE.match(subject):
        errors.append(
            "subject must use conventional commits: "
            "type(scope): description "
            "(types: feat, fix, docs, style, refactor, perf, test, chore, build, ci, revert)"
        )
    if PHASE_RE.search(subject):
        errors.append("subject must not reference plan phases (Phase A, Phase B, …)")
    if ADR_RE.search(subject):
        errors.append("subject must not reference ADR numbers")

    if errors:
        print("Commit message check failed:", file=sys.stderr)
        print(f"  subject: {subject!r}", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        print(
            "\nExample: feat(compiler): add logical import resolution",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
