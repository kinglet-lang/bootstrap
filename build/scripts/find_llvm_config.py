#!/usr/bin/env python3
"""Print the first usable llvm-config path for GN exec_script."""

from __future__ import annotations

import os
import shutil
import sys


def main() -> int:
    candidates = [
        os.environ.get("LLVM_CONFIG", ""),
        "/opt/homebrew/opt/llvm/bin/llvm-config",
        "/usr/local/opt/llvm/bin/llvm-config",
    ]
    for path in candidates:
        if path and os.path.isfile(path) and os.access(path, os.X_OK):
            print(path)
            return 0

    found = shutil.which("llvm-config")
    if found:
        print(found)
        return 0

    sys.stderr.write("llvm-config not found; set llvm_config or LLVM_CONFIG\n")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
