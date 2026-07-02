#!/usr/bin/env python3
"""Format commit message body: rewrap paragraphs to 72 columns.

Preserves:
- Blank lines (paragraph separators)
- Lines starting with '-', '*', '>' (list items, blockquotes)
- Indented lines (code blocks, 2+ leading spaces)
- Lines starting with '#' (comments stripped by git anyway)

Only rewraps "prose" paragraphs — contiguous non-special lines.
"""

from __future__ import annotations

import re
import sys
import textwrap
from pathlib import Path

MAX_WIDTH = 72
SPECIAL_LINE = re.compile(r"^([-*>] | {2,}|\t)")


def is_prose(line: str) -> bool:
    """True if the line should be part of a rewrap group."""
    stripped = line.rstrip("\n")
    if not stripped:
        return False
    if SPECIAL_LINE.match(stripped):
        return False
    return True


def rewrap_paragraph(lines: list[str]) -> list[str]:
    """Join and rewrap a paragraph of prose lines."""
    joined = " ".join(lines)
    return textwrap.wrap(joined, width=MAX_WIDTH)


def format_body(text: str) -> str:
    """Rewrap the body portion of a commit message."""
    parts = text.split("\n\n", 1)
    if len(parts) < 2:
        return text  # No body.

    subject = parts[0]
    body = parts[1]

    out_lines: list[str] = [subject, ""]
    para: list[str] = []

    def flush_para() -> None:
        if para:
            out_lines.extend(rewrap_paragraph(para))
            out_lines.append("")  # blank after paragraph
            para.clear()

    for raw in body.split("\n"):
        line = raw.rstrip()
        if not line:
            flush_para()
        elif is_prose(line):
            para.append(line)
        else:
            flush_para()
            out_lines.append(line)

    flush_para()

    # Strip trailing blank lines.
    while out_lines and out_lines[-1] == "":
        out_lines.pop()

    return "\n".join(out_lines) + "\n"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: format_commit_body.py <msg-file>", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    original = path.read_text(encoding="utf-8")
    formatted = format_body(original)
    if formatted != original:
        path.write_text(formatted, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
