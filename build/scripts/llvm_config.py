#!/usr/bin/env python3
"""Emit llvm-config flags one per line for GN exec_script list_lines."""

from __future__ import annotations

import shlex
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: llvm_config.py <llvm-config> <cxxflags|ldflags|libs|systemlibs>\n")
        return 1

    llvm_config = sys.argv[1]
    what = sys.argv[2]
    if what == "cxxflags":
        args = ["--cxxflags"]
    elif what == "ldflags":
        args = ["--ldflags"]
    elif what == "libs":
        args = ["--libs", "core", "native"]
    elif what == "systemlibs":
        args = ["--system-libs"]
    else:
        sys.stderr.write(f"unknown llvm_config mode: {what}\n")
        return 1

    out = subprocess.check_output([llvm_config, *args], text=True).strip()
    flags = shlex.split(out)
    skip_next = False
    for flag in flags:
        if skip_next:
            skip_next = False
            continue
        if flag.startswith("-std="):
            continue
        if flag == "-fno-exceptions":
            continue
        if flag in ("-stdlib=libc++",):
            continue
        if flag == "-Wl,-headerpad_max_install_names":
            continue
        if what == "libs" and flag.startswith("-l"):
            print(flag[2:])
            continue
        print(flag)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
