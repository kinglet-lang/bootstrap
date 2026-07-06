#!/usr/bin/env python3
"""Print the first usable llvm-config path for GN exec_script."""

from __future__ import annotations

import os
import shutil
import sys


def _norm(path: str) -> str:
    """Normalise to forward slashes for clean embedding in GN args."""
    return path.replace("\\", "/") if path else path


def main() -> int:
    candidates = []

    explicit = os.environ.get("LLVM_CONFIG", "")
    if explicit:
        candidates.append(explicit)

    if sys.platform == "win32" or os.name == "nt":
        # On Windows the only complete, widely available llvm-config (one that
        # also ships the matching static/import libs and headers) is the MSYS2
        # MinGW LLVM. The official llvm.org installer omits llvm-config and the
        # dev libraries, so prefer MinGW prefixes here.
        msystem_prefix = os.environ.get("MSYSTEM_PREFIX", "")
        if msystem_prefix:
            candidates.append(f"{msystem_prefix}/bin/llvm-config.exe")

        win_roots = [
            os.environ.get("MSYS2", ""),
            r"C:\msys64",
            r"C:\clang64",
            os.environ.get("LLVM_INSTALL_DIR", ""),
            r"C:\Program Files\LLVM",
            r"C:\ProgramData\chocolatey\lib\llvm\tools",
        ]
        for root in win_roots:
            if not root:
                continue
            for sub in (r"mingw64\bin", r"clang64\bin", r"ucrt64\bin", "bin"):
                candidates.append(os.path.join(root, sub, "llvm-config.exe"))
    else:
        candidates += [
            "/opt/homebrew/opt/llvm/bin/llvm-config",
            "/usr/local/opt/llvm/bin/llvm-config",
        ]

    for path in candidates:
        if path and os.path.isfile(path) and os.access(path, os.X_OK):
            print(_norm(path))
            return 0

    # Fall back to whatever is on PATH (handles llvm-config-N too).
    for name in ("llvm-config", "llvm-config.exe"):
        found = shutil.which(name)
        if found:
            print(_norm(found))
            return 0
    for n in range(20, 9, -1):
        found = shutil.which(f"llvm-config-{n}")
        if found:
            print(_norm(found))
            return 0

    sys.stderr.write("llvm-config not found; set llvm_config or LLVM_CONFIG\n")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
