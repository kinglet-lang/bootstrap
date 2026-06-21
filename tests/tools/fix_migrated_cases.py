#!/usr/bin/env python3
"""Post-migration fixes: flatten sema/cases and use CHECK-ERR (no line numbers)."""

from __future__ import annotations

import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

MSG_RE = re.compile(r"^\d+:\d+:\s*((?:error|warning):.*)$", re.MULTILINE)


def flatten_sema_cases() -> None:
    for sub in ("pass", "fail"):
        cases_dir = ROOT / "tests" / "sema" / sub / "cases"
        if not cases_dir.is_dir():
            continue
        for path in sorted(cases_dir.iterdir()):
            dest = ROOT / "tests" / "sema" / sub / path.name
            if dest.exists():
                dest.unlink()
            shutil.move(str(path), str(dest))
        cases_dir.rmdir()


def core_messages(stderr: str) -> list[str]:
    out: list[str] = []
    for line in stderr.splitlines():
        line = line.strip()
        if not line:
            continue
        m = MSG_RE.match(line)
        out.append(m.group(1) if m else line)
    return out


def fix_kl_sidecars(kl: Path) -> None:
    stderr_path = kl.with_suffix(".stderr")
    if not stderr_path.exists():
        return
    text = stderr_path.read_text(encoding="utf-8")
    msgs = core_messages(text)
    if not msgs:
        stderr_path.unlink()
        return

    contains = kl.with_suffix(".stderr_contains")
    contains.write_text("\n".join(msgs) + "\n", encoding="utf-8")
    stderr_path.unlink()

    lines = kl.read_text(encoding="utf-8").splitlines()
    out: list[str] = []
    for line in lines:
        if line.startswith("// EXPECT-STDERR:"):
            continue
        out.append(line)
    kl.write_text("\n".join(out) + "\n", encoding="utf-8")


def main() -> None:
    flatten_sema_cases()
    for pattern in (
        "tests/exec/cases/*.kl",
        "tests/sema/pass/*.kl",
        "tests/sema/fail/*.kl",
    ):
        for kl in ROOT.glob(pattern):
            fix_kl_sidecars(kl)
    print("Post-migration fixes applied.")


if __name__ == "__main__":
    main()
