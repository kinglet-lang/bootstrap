# Deprecated

`tests/cli/` was the legacy monolithic golden runner. Cases now live under the
[ADR 0012](../README.md) layout (`exec/`, `sema/`, `codegen/`, `ir/`, `parser/`).

`run_golden.sh` forwards to those suites for backward compatibility.
