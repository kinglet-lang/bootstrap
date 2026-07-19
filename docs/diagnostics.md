# Kinglet Diagnostic Codes

Stable identifiers for compiler diagnostics. See [ADR 0031](../../ADRs/0031-diagnostic-system.md)
for the scheme, categories, and rendering rules.

## Format

- Codes are `K` + a category digit (`0-9`, or `10+` for later categories) + a
  serial number: e.g. `K2001`, `K10002`.
- Codes are **stable** once assigned. When a diagnostic is retired the code is
  retired with it — never reused for a different meaning.
- Message text may change across versions. The code identifies the *category*,
  not the message wording.

## Categories

| Range | Category |
|-------|----------|
| `K0xxx` | Lexical, syntax, and parse |
| `K1xxx` | Names, declarations, and scope |
| `K2xxx` | Type system and type inference |
| `K3xxx` | Value category, mutability, and assignment |
| `K4xxx` | Ownership, transfer, and move |
| `K5xxx` | Borrow, reference, and lifetime |
| `K6xxx` | Initialization, destruction, and resource lifecycle |
| `K7xxx` | Functions, parameters, and calls |
| `K8xxx` | Control flow and reachability |
| `K9xxx` | Expressions and operators |
| `K10xxx` | Optional, fallible cast, and unwrap |
| `K11xxx` | Arrays, indexing, slicing, and dimensions |
| `K12xxx` | Generics and compile-time evaluation |
| `K13xxx` | Pattern matching and exhaustiveness |
| `K14xxx` | FFI, ABI, and external symbols |
| `K15xxx` | Modules, imports, and visibility |
| `K16xxx` | Concurrency, atomics, and thread safety |
| `K17xxx` | Attributes, built-in capabilities, and language constraints |
| `K18xxx` | Unused code, style, and suspicious patterns |
| `K19xxx` | Compiler limits and internal diagnostics |

## Assigned codes

| Code | Severity | Description | Example message | Secondary label |
|------|----------|-------------|-----------------|-----------------|
| `K0001` | error | Lexical error — the source contains an invalid or unterminated token. | `lexer error: Unexpected character.` | — |
| `K0002` | error | Syntax error — the token stream does not match the Kinglet grammar. | `Expected expression.` | — |
| `K1001` | error | Name resolution failure — unknown type or undeclared variable. | `Unknown type 'Foo'.` / `Undeclared variable 'x'.` | — |
| `K1002` | error | Redeclaration of a name in the same scope. | `Variable 'x' already declared.` | first declaration site |
| `K2001` | error | Type mismatch on assignment or initialization. | `Cannot assign string to variable of type int.` | declaration site of the target |
| `K3002` | error | Assignment to a `const` binding after initialization. | `Cannot assign to const variable 'x'.` | declaration site of the const |
| `K4001` | error | Use of a value that was moved / transferred elsewhere. | `Variable 'x' was transferred and is no longer valid.` | transfer site |
| `K5001` | error | Conflicting borrow — a new borrow overlaps a live one, or a use overlaps a live mutable borrow. | `Conflicting borrow of 'x'.` / `Cannot use 'x' while it is mutably borrowed.` | earlier borrow site |
| `K5004` | error | A reference escapes the scope that owns its referent. | `References cannot escape their owning scope.` | — |
| `K6001` | error | A variable or field may be read before it is definitely initialized. | `Variable 'x' may be uninitialized.` | — |
| `K7001` | error | Overload resolution failed — no viable candidate for the call. | `No matching overload.` | every candidate's declaration |
| `K7007` | error | A non-void function has an incomplete return — a bare `return` or a path that reaches the end without a value. | `Non-void function must return a value.` | — |
| `K7010` | warning | Expression statement's result is discarded silently. | `Expression result is unused.` | — |
| `K8001` | warning | A statement or match arm cannot be reached. | `Unreachable code.` | — |
| `K10001` | error | A nullable value is used where its non-null inner value is required. | `Left operand of '+' has nullable type int?.` | — |
| `K10002` | error | Fallible cast (`string → int`, etc.) not handled with `?:` or postfix `?`. | `Fallible cast from string to int must be handled with '?:' or postfix '?'.` | — |
| `K13001` | error | A match expression does not cover every possible input. | `Non-exhaustive match. Missing variant(s): Blue.` | — |
| `K15001` | error | Module resolution failure — `using`/`import` references a module that cannot be resolved. | `Unknown module 'foo'.` | — |
| `K15002` | error | A known module is referenced without first being imported or opened. | `Module 'io' is not imported.` | — |
| `K18001` | warning | Local variable declared but never read. | `Unused variable 'x'.` | — |
| `K19001` | error | Source nesting exceeds the compiler's supported parsing limit. | `Maximum nesting depth exceeded.` | — |

Codes reserved for checks that are not currently emitted by the compiler:

- `K2008` (recursive layout)
- `K4005` (partial move)
- `K11001` (compile-time out-of-bounds indexing)

New codes are added by appending to this table; no separate architecture decision is required.
