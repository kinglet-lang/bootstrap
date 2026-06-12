# Kinglet — Agent Instructions

## Project Overview

A systems programming language implemented in C++20. Compiler pipeline: Scanner → Parser → TypeChecker → Compiler → Bytecode VM.

## Build

```bash
gn gen out/Debug   # regenerate if BUILD.gn changed
ninja -C out/Debug # incremental build
```

Run:
```bash
./out/Debug/kinglet tests/cli/cases/operators_arithmetic.kl    # compile + run
./out/Debug/kinglet --ast tests/cli/cases/operators_arithmetic.kl
./out/Debug/kinglet --bytecode tests/cli/cases/operators_arithmetic.kl
./out/Debug/kinglet --repl                                   # interactive REPL
```

## Directory Structure

```
src/
├── ast/         AST node definitions (ast.h)
├── checker/     TypeChecker (type inference + error reporting)
├── compiler/    AST → bytecode compiler
├── kinglet/     CLI entry point (main.cc)
├── lexer/       Scanner + Token definitions
├── parser/      Recursive descent parser
├── types/       Type system (TypeKind, Type)
└── vm/          Bytecode VM (Value, Chunk, Vm)

runtime/         libkinglet_rt — C ABI runtime linked into native executables
```

## Code Conventions

- C++20, GN + ninja build, system clang on macOS (`/usr/bin/clang++`)
- `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`
- Use `dynamic_cast` for AST node dispatch (no visitor pattern yet)
- Namespace: `kinglet` (core), `kinglet::ast` (AST nodes)
- Prefer enum class over string-based dispatch
- New files need a `BUILD.gn` library target, and deps added to root `BUILD.gn`

## Completed

- **P1**: AST operator enums, CallFrame, JMP/JMP_FALSE, if/while/for/break/continue, inspect (pattern matching), REPL
- **P1.5**: Typed LiteralExpr, TypeChecker, `using io;` / `using namespace io;`
- **I/O**: NativeOut/NativeErr/NativeIn/NativePrint opcodes, `print()`, `io::out/err/in()`, `{}` format strings

## Known Gaps

- Multi-function calls (VM CallFrame exists, compiler only calls `main()`)
- Closures / lambda
- Array/Map literals
- Generics
- import/export semantics
- `dynamic_cast` — consider visitor pattern
- `Scanner::identifier_type()` rebuilds unordered_map every call
