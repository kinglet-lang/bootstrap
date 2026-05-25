# Changelog

## [0.0.4] — 2026-05-25

### Language Features

- **String operations** — `+` concatenation, comparison operators, indexing `s[i]`, and 11 built-in methods: `len()`, `contains()`, `starts_with()`, `ends_with()`, `index_of()`, `slice()`, `replace()`, `split()`, `trim()`, `to_upper()`, `to_lower()`
- **Implicit return** — last expression in a non-void function body is the return value (inspired by P0927)
- **Structured unpacking** — `auto [a, b, ...rest] = arr;` with rest syntax (inspired by P1858)
- **Guard statement** — `guard condition else { return; }` for early-exit control flow
- **First-class native functions** — `io::out`, `io::in` etc. can be passed as values and used in pipelines

### Diagnostics

- General unused-result warning for non-void expressions in statement position
- Format string arity check for `io::out`

### LSP

- Pipeline-aware completion for `io::` members (no parens after `|>`)
- String method dot-completion (all 11 methods)
- Simplified `io::` completion (plain text, no forced parentheses)

## [0.0.3] — 2025-05-25

### Language Features

- **Pipeline operator** `|>` — `x |> f` desugars to `f(x)` (inspired by P2011)
- **Chained comparisons** — `1 <= x <= 10` desugars to `1 <= x && x <= 10` (inspired by P0893)
- **Array methods** — `len()`, `push()`, `pop()`, `remove()`, `contains()`, `clear()`, `insert()`, `index_of()`, `slice()`, `reverse()`
- **I/O methods** — `io::out.line()`, `io::err.line()`, `io::in.secret()`

### Diagnostics

- Suggest `using io;` when `io::` is used without import
- Constant-condition warnings (`if true`, `while false`)
- LSP: document symbols, signature help, wider diagnostic underlines

### Tooling

- CI workflow with golden tests on push/PR

### Fixes

- Suppress clang warnings (missing field initializers, sign conversion)
- Add missing `<algorithm>` include for `std::reverse` on Linux

## [0.0.2] — 2025-05-24

Initial public release with structs, enums, generics, pattern matching via `inspect`, and LSP support.

## [0.0.1] — 2025-05-23

Project bootstrap: lexer, parser, type checker, bytecode compiler, VM, basic I/O.
