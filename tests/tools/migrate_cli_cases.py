#!/usr/bin/env python3
"""One-shot migration: tests/cli/cases -> ADR 0012 layout with harness directives."""

from __future__ import annotations

import os
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "tests" / "cli" / "cases"

# Parsed from legacy tests/cli/run_golden.sh
CASES: list[dict] = [
    # --- exec (run) ---
    {"name": "arrays_success", "suite": "exec", "run": "run", "exit": 0, "stdout": "1 20 []\n"},
    {"name": "arrays_type_error", "suite": "exec", "run": "run", "exit": 65,
     "stderr": "2:18: error: Array elements must have compatible types.\n"},
    {"name": "arrays_oob", "suite": "exec", "run": "run", "exit": 70,
     "stderr": "runtime error: Array index out of bounds.\n"},
    {"name": "operators_arithmetic", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "13\n7\n30\n3\n1\n-5\n-6\n"},
    {"name": "operators_comparison", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "true\ntrue\ntrue\ntrue\ntrue\nfalse\n"},
    {"name": "operators_logic", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "true\nfalse\nfalse\ntrue\ntrue\nfalse\nfalse\ntrue\nshort-circuit:\ndone\n"},
    {"name": "bitwise_ops", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "16\n64\n4\n7\n4\n3\n8\n46532\n"},
    {"name": "generic_field_index", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "10 2\none 1\n99\n"},
    {"name": "array_resize", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "8 -1 -1\n42\n2\n? ? ?\n"},
    {"name": "structs_basic", "suite": "exec", "run": "run", "exit": 0, "stdout": "3 4\n10\n"},
    {"name": "enums_basic", "suite": "exec", "run": "run", "exit": 0, "stdout": "green\nnot red\n"},
    {"name": "match_basic", "suite": "exec", "run": "run", "exit": 0, "stdout": "zero\none\nother\n"},
    {"name": "match_binding", "suite": "exec", "run": "run", "exit": 0, "stdout": "A\npass\nfail\n"},
    {"name": "match_array", "suite": "exec", "run": "run", "exit": 0, "stdout": "20\n10\n1\n"},
    {"name": "generics_basic", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "42\nhello\n99\nworld\n1 one\n42\n"},
    {"name": "generic_infer", "suite": "exec", "run": "run", "exit": 0, "stdout": "42\nhi\n7\n3\n"},
    {"name": "generic_struct_infer", "suite": "exec", "run": "run", "exit": 0, "stdout": "7\nhi\n1 x\n"},
    {"name": "control_flow", "suite": "exec", "run": "run", "exit": 0, "stdout": "10\n3\n2\n1\nyes\n",
     "stderr": "16:6: warning: Condition is always true.\n"},
    {"name": "functions_recursion", "suite": "exec", "run": "run", "exit": 0, "stdout": "720\n55\n"},
    {"name": "io_methods", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "hello world\n1 + 2 = 3\nno newline here\ndone\n"},
    {"name": "using_system_selective", "suite": "exec", "run": "run", "exit": 0, "stdout": "ok\n"},
    {"name": "warnings", "suite": "exec", "run": "run", "exit": 0, "stdout": "",
     "stderr": "4:6: warning: Condition is always true.\n8:9: warning: Condition is always false; loop body never executes.\n9:5: warning: Unused variable 'x'.\n13:3: warning: Unreachable code.\n2:3: warning: Unused variable 'unused'.\n"},
    {"name": "array_methods", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "len: 5\nafter push: len = 6\npopped: 6\nremoved at 0: 1\ncontains 3: true\ncontains 99: false\nafter clear: len = 0\n"},
    {"name": "array_insert", "suite": "exec", "run": "run", "exit": 0, "stdout": "1 10 2 3\nlen = 7\n7 8 9\n"},
    {"name": "array_slice_reverse", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "index_of 3: 2\nindex_of 99: -1\nslice(1,4): 2 3 4\nreversed: 5 4 3 2 1\n"},
    {"name": "chained_comparisons", "suite": "exec", "run": "run", "exit": 0, "stdout": "in rangenot smalledge"},
    {"name": "pipeline", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "5 |> twice |> add_one = 11\n3 |> add_one |> twice |> negate = -8\n"},
    {"name": "native_fn_firstclass", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "hello from variable\n1 + 2 = 3\nab\npipeline test\n", "stderr": "stderr message\n"},
    {"name": "implicit_return", "suite": "exec", "run": "run", "exit": 0, "stdout": "25\ntrue\nfalse\n42\n"},
    {"name": "unpack_decl", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "10\n20\n[30, 40, 50]\nhello\nworld\n"},
    {"name": "guard_stmt", "suite": "exec", "run": "run", "exit": 0, "stdout": "5\n-1\n5\n3\n0\n"},
    {"name": "string_ops", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "hello world\ntrue\ntrue\ntrue\ntrue\ntrue\ne\n5\ntrue\nfalse\ntrue\ntrue\nfalse\n2\n-1\nhello\nworld\nhello kinglet\n3\na\nb\nc\nhello\nHELLO\nhello\n"},
    {"name": "error_missing_using_io", "suite": "exec", "run": "run", "exit": 65,
     "stderr": "2:3: error: Module 'io' is not imported. Add 'using io;' at the top of the file.\n"},
    {"name": "nullable_type", "suite": "exec", "run": "run", "exit": 0, "stdout": ""},
    {"name": "try_catch", "suite": "exec", "run": "run", "exit": 0, "stdout": "7\n-99\n",
     "stderr": "9:3: warning: Unused variable 'e'.\n18:3: warning: Unused variable 'e'.\n"},
    {"name": "enum_payload_test", "suite": "exec", "run": "run", "exit": 0, "stdout": "start\ndone\n",
     "stderr": "10:3: warning: Unused variable 's'.\n"},
    {"name": "enum_destructure_test", "suite": "exec", "run": "run", "exit": 0, "stdout": "3.14\n12\n0\n"},
    {"name": "enum_guard_test", "suite": "exec", "run": "run", "exit": 0, "stdout": "big\nnone\n",
     "stderr": "14:22: warning: Unused variable 'x'.\n20:22: warning: Unused variable 'x'.\n"},
    {"name": "match_enum_destruct", "suite": "exec", "run": "run", "exit": 0, "stdout": "42\n-1\n"},
    {"name": "match_exhaustive_ok", "suite": "exec", "run": "run", "exit": 0, "stdout": "up\nup\n"},
    {"name": "fixed_width_types", "suite": "exec", "run": "run", "exit": 0, "stdout": "42 42 255\n"},
    {"name": "fixed_width_i32_wrap", "suite": "exec", "run": "run", "exit": 0, "stdout": "-2147483648\n"},
    {"name": "fs_read_missing", "suite": "exec", "run": "run", "exit": 0, "stdout": "missing is null\n"},
    {"name": "map_basic", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "len=3\na=1\nmissing=null\nhas_b=true\nhas_b_after=false\nlen_after=2\nkeys_len=2\nx=10 y=20\nidx0=zero idx1=one\nhas0=true\n"},
    {"name": "map_array_type", "suite": "exec", "run": "run", "exit": 0, "stdout": "k=42\nlen=1\n"},
    {"name": "map_symbol_table", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "found: x kind=1\nz not found\ntotal=2\n  x -> kind=1\n  y -> kind=2\n"},
    {"name": "fs_roundtrip", "suite": "exec", "run": "run", "exit": 0, "stdout": "hello fs\n",
     "run_args": "$HARNESS_TMP"},
    {"name": "sys_args_three", "suite": "exec", "run": "run", "exit": 0,
     "stdout": "argc: 3\narg: alpha\narg: beta\narg: --flag\n", "source": "sys_args.kl",
     "run_args": "alpha beta --flag"},
    {"name": "sys_args_empty", "suite": "exec", "run": "run", "exit": 0, "stdout": "argc: 0\n",
     "source": "sys_args.kl"},
    {"name": "cat", "suite": "exec", "run": "run", "exit": 0, "stdout": "hello fs\n",
     "run_args": "$CASE_DIR/cat_fixture.txt"},
    # --- sema fail (check) ---
    {"name": "match_exhaustive_warn", "suite": "sema/fail", "run": "check", "compile_fail": True, "exit": 65,
     "stderr": "11:16: error: Non-exhaustive match. Missing variant(s): Blue.\n"},
    {"name": "match_bool_non_exhaustive", "suite": "sema/fail", "run": "check", "compile_fail": True, "exit": 65,
     "stderr": "2:12: error: Non-exhaustive match on bool. Missing case(s): false.\n"},
    {"name": "match_nullable_missing_null", "suite": "sema/fail", "run": "check", "compile_fail": True, "exit": 65,
     "stderr": "7:12: error: Non-exhaustive match on nullable type. Missing case: null.\n"},
    {"name": "match_enum_payload_partial", "suite": "sema/fail", "run": "check", "compile_fail": True, "exit": 65,
     "stderr": "5:12: error: Non-exhaustive payload pattern for variant(s): Some.\n"},
    {"name": "match_int_non_exhaustive", "suite": "sema/fail", "run": "check", "compile_fail": True, "exit": 65,
     "stderr": "2:12: error: Non-exhaustive match on int. Add a catch-all pattern (`_` or `let x`).\n"},
    {"name": "match_struct_partial", "suite": "sema/fail", "run": "check", "compile_fail": True, "exit": 65,
     "stderr": "5:12: error: Non-exhaustive struct pattern for 'Point'.\n"},
    {"name": "fixed_width_narrowing", "suite": "sema/fail", "run": "check", "compile_fail": True, "exit": 65,
     "stderr": "2:3: error: Cannot assign int to variable of type int32.\n"},
    # --- sema pass (check with warnings) ---
    {"name": "match_duplicate_enum_warn", "suite": "sema/pass", "run": "check", "exit": 0,
     "stderr": "6:5: warning: Duplicate match arm for variant 'Red'.\n"},
    # --- codegen ---
    {"name": "arrays_bytecode", "suite": "codegen", "run": "bytecode", "exit": 0,
     "checks": ["ArrayNew", "IndexGet", "IndexSet"]},
    # --- ir ---
    {"name": "fixed_width_kir", "suite": "ir", "run": "ir", "exit": 0,
     "checks": ["const_i32", "const_i64", "iadd32", ": int32"]},
    {"name": "container_array_index", "suite": "ir", "run": "ir", "exit": 0,
     "checks": ["index_get", ": int32"]},
    # --- parser (ast) ---
    {"name": "nullable_type_ast", "suite": "parser", "run": "ast", "exit": 0,
     "source": "nullable_type.kl",
     "checks": ["(function Nullable<int> pick", "type=Nullable<int> name=v"]},
]


def strip_directives(body: str) -> str:
    lines = body.splitlines(keepends=True)
    out: list[str] = []
    in_block = True
    for line in lines:
        stripped = line.strip()
        if in_block and (stripped == "" or stripped.startswith("// RUN:") or stripped.startswith("// EXPECT-")
                         or stripped.startswith("// CHECK") or stripped == "// COMPILE-FAIL"
                         or stripped.startswith("// RUN-ARGS:")):
            continue
        in_block = False
        out.append(line)
    return "".join(out).lstrip("\n")


def build_header(case: dict) -> str:
    lines = [f"// RUN: {case['run']}"]
    if case.get("compile_fail"):
        lines.append("// COMPILE-FAIL")
    if case.get("exit") is not None and case["exit"] != 0:
        lines.append(f"// EXPECT-EXIT: {case['exit']}")
    if case.get("run_args"):
        lines.append(f"// RUN-ARGS: {case['run_args']}")
    for key, directive in (("stdout", "EXPECT-STDOUT"), ("stderr", "EXPECT-STDERR")):
        val = case.get(key)
        if val is None or val == "":
            continue
        if "\n" in val or len(val) > 60:
            continue  # use sidecar
        lines.append(f"// {directive}: {val.rstrip(chr(10))}")
    for c in case.get("checks", []):
        lines.append(f"// CHECK: {c}")
    return "\n".join(lines) + "\n"


def write_sidecars(dest: Path, case: dict) -> None:
    if case.get("stdout") is not None:
        (dest.with_suffix(".expected")).write_text(case["stdout"])
    if case.get("stderr"):
        (dest.with_suffix(".stderr")).write_text(case["stderr"])


def migrate_case(case: dict) -> None:
    name = case["name"]
    src_name = case.get("source", f"{name}.kl")
    src = CLI / src_name
    if not src.exists():
        print(f"skip missing {src}")
        return

    suite = case["suite"]
    dest_dir = ROOT / "tests" / suite / "cases"
    dest_dir.mkdir(parents=True, exist_ok=True)
    dest = dest_dir / f"{name}.kl"

    body = strip_directives(src.read_text(encoding="utf-8"))
    header = build_header(case)
    dest.write_text(header + "\n" + body, encoding="utf-8")
    write_sidecars(dest, case)

    # Fixtures co-located with exec cases
    if name == "cat":
        fixture = CLI / "cat_fixture.txt"
        if fixture.exists():
            shutil.copy2(fixture, dest_dir / "cat_fixture.txt")


def move_import_basic() -> None:
    src = CLI / "import_basic"
    if not src.exists():
        return
    dest = ROOT / "tests" / "module" / "import_basic"
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(src, dest)


def add_sema_directives() -> None:
    migrated = {c["name"] for c in CASES if c["suite"].startswith("sema/")}
    for sub, run, compile_fail in [("pass", "check", False), ("fail", "check", True)]:
        d = ROOT / "tests" / "sema" / sub
        for path in sorted(d.glob("*.kl")):
            if path.stem in migrated:
                continue
            text = path.read_text(encoding="utf-8")
            if text.lstrip().startswith("// RUN:"):
                continue
            body = strip_directives(text)
            header = f"// RUN: {run}\n"
            if compile_fail:
                header += "// COMPILE-FAIL\n// EXPECT-EXIT: 65\n"
            path.write_text(header + "\n" + body, encoding="utf-8")


def main() -> None:
    for case in CASES:
        migrate_case(case)
    move_import_basic()

    # error_handling stays in exec if present
    eh = CLI / "error_handling.kl"
    if eh.exists():
        dest = ROOT / "tests" / "exec" / "cases" / "error_handling.kl"
        body = strip_directives(eh.read_text(encoding="utf-8"))
        dest.write_text("// RUN: run\n\n" + body, encoding="utf-8")
        exp = CLI / "error_handling.expected"
        if exp.exists():
            shutil.copy2(exp, dest.with_suffix(".expected"))

    add_sema_directives()
    print("Migration complete.")


if __name__ == "__main__":
    main()
