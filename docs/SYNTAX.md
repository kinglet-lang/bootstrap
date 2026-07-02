# Kinglet Language Syntax Reference

This document describes the surface syntax of the Kinglet language as implemented
by the self-hosting bootstrap compiler (`kinglet 0.1.0-rc.3`). It is derived
directly from the compiler frontend:

- Lexer: `compiler/frontend/lexer/scanner.cc`, `token.h`
- Parser: `compiler/frontend/parser/parse_{decl,stmt,expr,type}.cc`, `parser.cc`
- AST: `compiler/frontend/ast/ast.h`
- Type checker: `compiler/frontend/checker/type_checker.cc`

Where the grammar and the checker disagree, the checker wins at compile time;
this document notes such cases. Anything marked *reserved* is recognized by the
lexer but not yet wired into the parser.

For the project manifest (`kinglet.nest`) and the module system, see
[`MODULES.md`](MODULES.md). This document covers only the `.kl` source language.

## Lexical structure

### Comments

```kinglet
// line comment — runs to end of line
/* block comment — does not nest; ends at the first */
```

Block comments do **not** nest (`scanner.cc` scans to the first `*/`).

### Identifiers

An identifier starts with a letter or `_` and continues with letters, digits, or
`_`: `[A-Za-z_][A-Za-z0-9_]*`. Identifiers are case-sensitive.

`_` alone is a valid identifier and is treated specially in patterns (wildcard)
and as a throwaway binding.

### Keywords

Reserved by the lexer (`identifier_type()` in `scanner.cc`):

```
auto  int  int8  int16  int32  int64  uint8  uint16  uint32  uint64
float  float32  float64  double  bool  string  void  byte  char
const  return  if  else  for  while  break  continue  guard
match  let  when  try  catch
pub  import  export  namespace  using
struct  enum  concept
spawn  select
true  false  null
```

`when`, `spawn`, and `select` are reserved but **not parsed** by any rule —
they are placeholders for future features and cannot be used today.

`module` is **not** a keyword; it is a contextual identifier matched only in the
`export module <name>;` form.

`mut` is **not** a keyword; it is a contextual identifier recognized only
directly after `&` in type and reference position (`&mut T`, `&mut x`).

### Integer literals

```kinglet
42
1_000_000        // underscores are digit separators, stripped by the lexer
0xFF   0xdead_beef   // hexadecimal
0b1010   0b1000_0001 // binary
```

Optional width suffix (no separating dot): `i8 i16 i32 i64 u8 u16 u32 u64`.

```kinglet
255u8
-1i32
9999999999u64
```

### Float literals

```kinglet
3.14
1_000.5
1e10   2.5e-3   6.02E23   // exponent form
```

Optional width suffix: `f32`, `f64`.

```kinglet
3.14f32
2.0f64
```

A `.` is only the start of a fraction when followed by a digit; `x.y` is field
access, not a float.

### String literals

Double-quoted, with backslash escapes:

```kinglet
"hello"
"tab\tnewline\n"
"quote: \" backslash: \\"
```

Recognized escapes (`escaped_char_value`): `\0 \n \r \t \\ \' \"`. Any other
escaped character yields that character verbatim. Strings do not span multiple
lines (an unterminated string is a lex error).

### Character literals

Single-quoted, exactly one character (after escape processing):

```kinglet
'a'
'\n'
'\''
```

## Types

Type syntax (`parse_type_expr` in `parse_type.cc`):

### Primitive and named types

```kinglet
int  int8 int16 int32 int64  uint8 uint16 uint32 uint64
float  float32 float64  double
bool  string  void  byte  char
auto                       // deduced from initializer
MyStruct  MyEnum           // any identifier names a type
```

### Generic types

Angle-bracket type arguments:

```kinglet
Box<int>
Pair<int, string>
Box<Box<int>>              // nested; the lexer's `>>` is split by the parser
```

### Array types

A trailing `[]` suffix, repeatable for nested arrays:

```kinglet
int[]
string[]
int[][]                    // array of arrays
```

### Map types

Brace form `{K: V}` (internally `Map<K, V>`):

```kinglet
{string: int}
{int: string}
{string: int}[]            // array of maps
```

### Nullable types

A trailing `?` makes a type nullable (internally `Nullable<T>`):

```kinglet
int?
string?
{string: int}?
```

### Reference types

A leading `&` (optionally `&mut`) forms a reference type:

```kinglet
&int
&mut Point
```

## Top-level declarations

A program is a sequence of declarations (`declaration()` in `parse_decl.cc`).
The forms are: `using`, `export module`, `import`, `struct`, `enum`, `concept`,
function declarations, and bare statements (which become top-level statements,
e.g. `int main()` is a function, but a global `const` is a statement decl).

`pub` may prefix `struct`, `enum`, `concept`, or a function to mark it public
(exported from its module). `pub` before anything else is an error.

### Functions

Block body:

```kinglet
int add(int a, int b) {
  return a + b;
}
```

Expression body with `=>` (desugars to a single `return`):

```kinglet
int twice(int x) => x * 2;
```

Generic functions declare type parameters after the name:

```kinglet
T identity<T>(T x) => x;
```

`void` return and zero parameters are allowed:

```kinglet
void greet() {
  // ...
}
```

The entry point is `int main()`. A binary target must contain exactly one file
with an `int main()` — the compiler discovers the entry automatically.

### Structs

```kinglet
struct Point {
  int x;
  int y;
}

struct Box<T> {
  T value;
}

struct Pair<A, B> {
  A first;
  B second;
}
```

Fields are `Type name;` — each terminated by `;`. Struct instances are created
with a struct literal (see below).

### Enums

Variants may be bare or carry positional payload types:

```kinglet
enum Color {
  Red,
  Green,
  Blue,
}

enum Shape {
  Circle(float),
  Rect(float, float),
  None,
}
```

A trailing comma after the last variant is allowed.

### Concepts

A concept declares one or more type parameters and method signatures (like an
interface / trait). At least one type parameter is required.

```kinglet
concept Printable<T> {
  string to_string(T value);
}
```

Method signatures end with `;` and require explicit return types (no `auto`).

### Modules, imports, using

Full details in [`MODULES.md`](MODULES.md). Surface forms:

```kinglet
export module math.basic;     // declare this file's module identity
import math.basic;            // import an exact module
import math;                  // group import: all math.* modules

using io;                     // open a runtime namespace (io/fs/sys/rt)
using namespace math.basic;   // open a module's public names unqualified
using alias = math.basic;     // module alias
```

The following forms are explicitly **rejected** with a diagnostic:

- `import { ... }` — old import-block syntax (removed)
- `using module::*` — use `using namespace module` instead
- `using module { sym }` — selective using is unsupported

`io`, `fs`, `sys`, `rt` are hardcoded runtime namespaces: they can only be
brought in with `using`, never `import`.

## Statements

Statement forms (`statement()` in `parse_stmt.cc`):

### Blocks

```kinglet
{
  // statements
}
```

### Variable declarations

```kinglet
int x = 5;
auto y = 10;              // type deduced
const int MAX = 100;      // const-qualified
string name;              // no initializer
{string: int} m = {};
int[] xs = [1, 2, 3];
```

`const` is the only storage qualifier. Type may be `auto` for deduction.

Struct-literal initialization without `=` (brace directly after `Type name`):

```kinglet
Point p { 3, 4 };
Box<int> b { 42 };
```

### Destructuring declarations

Array/tuple unpacking with `auto [ ... ]`, optional `...rest`:

```kinglet
auto [a, b, ...rest] = nums;
auto [x, y] = words;
```

### Assignment

Assignment is an expression (see below) used in statement position:

```kinglet
x = 5;
x += 1;    x -= 1;    x *= 2;    x /= 2;
p.x = 10;             // field assignment
arr[0] = 9;           // index assignment (only `=`, no compound)
m["key"] = 1;
```

Compound assignment (`+= -= *= /=`) works on plain variables. Indexed targets
accept only `=` (compound assignment to an index is an error).

### return

```kinglet
return;
return expr;
```

### if / else

Parentheses around the condition are **optional**:

```kinglet
if (x > 0) { ... }
if x > 0 { ... } else { ... }

if x > 0 {
  ...
} else if x < 0 {
  ...
} else {
  ...
}
```

### while

```kinglet
while (i < n) { ... }
while i < n { ... }        // parens optional
```

### for

C-style, three clauses in parentheses (any may be empty):

```kinglet
for (i = 0; i < n; i = i + 1) { ... }
for (int i = 0; i < n; i = i + 1) { ... }   // init may declare
```

Note: there is no `++`/`--` operator; use `i = i + 1`.

### guard

Early-exit guard; the `else` block must not fall through:

```kinglet
guard b != 0 else {
  return -1;
}
```

### break / continue

```kinglet
break;
continue;
```

### try / catch

One or more `catch` clauses, each binding `let name: Type`:

```kinglet
try {
  b = int("bad")?;
} catch (let e: CastError) {
  b = -99;
} catch (let e: string) {
  b = -1;
}
```

At least one `catch` is required.

## Expressions

### Operator precedence

From lowest to highest binding (as implemented by the recursive-descent chain
in `parse_expr.cc`):

```
assignment        =  +=  -=  *=  /=            (right-assoc, statement-like)
ternary           ?:                            (cond ? a : b, right-assoc)
coalesce          ?:                            (elvis / error coalescing)
pipeline          |>
logical or        ||
logical and       &&
bitwise or        |
bitwise xor       ^
bitwise and       &
equality          ==  !=
comparison        <  >  <=  >=                  (supports chaining, see below)
shift             <<  >>
term              +  -
factor            *  /  %
unary             !  -  ~  &  &mut              (prefix)
call / postfix    f(x)  a.b  a[i]  x match {…}  e?
primary           literals, identifiers, (…), […], {…}
```

### Chained comparisons

Kinglet supports mathematical comparison chaining — `a < b < c` desugars to
`a < b && b < c` (the middle operand is evaluated once):

```kinglet
if (0 <= i < n) { ... }    // 0 <= i && i < n
```

### Arithmetic and bitwise

```kinglet
a + b   a - b   a * b   a / b   a % b
a & b   a | b   a ^ b   ~a   a << b   a >> b
```

### Logical

```kinglet
a && b   a || b   !a
```

### Unary

```kinglet
-x       // negation
!flag    // logical not
~bits    // bitwise not
&x       // reference
&mut x   // mutable reference
```

### Ternary

```kinglet
int x = cond ? 7 : 0;
return ok ? 1 : null;
```

### Elvis / error coalescing (`?:`)

Bare form returns the right side when the left is null/error:

```kinglet
string s = maybe ?: "default";
return v ?: 0;
```

Binding form captures the error. The checker requires the left-hand side to be
a direct cast expression (a fallible cast such as `int(...)`); the bound `err`
is a `CastError`, and the result stays nullable so the target must be `T?`:

```kinglet
int? x = int(text) ?: let err => 0;
```

### Error propagation (`?` postfix)

In a `T?`-returning function, `expr?` returns null on failure; inside a `try`
block it transfers to the matching `catch`:

```kinglet
int b = int("bad")?;
```

The parser disambiguates postfix `?` from ternary `?` by looking at the token
after `?`: if it can start an expression, `?` is treated as ternary.

### Pipeline (`|>`)

`x |> f` calls `f(x)`; chains left to right:

```kinglet
int result = 5 |> twice |> add_one;
int r2 = 3 |> add_one |> twice |> negate;
```

### Casts and type-qualified calls

Function-style cast using a primitive type name:

```kinglet
int n = int("42");
float f = float(3);
```

Type-qualified method call with `::`:

```kinglet
int::bits(x)
float::from_bits(n)
```

### Function calls and generics

```kinglet
add(1, 2)
identity<int>(99)
identity<string>("world")
```

### Field access, indexing

```kinglet
p.x
nested.value.value
arr[0]
m["key"]
"hello"[1]
```

### Literals

```kinglet
42  3.14  "text"  'c'  true  false  null
[1, 2, 3]                       // array literal
[]                              // empty array
{"x": 10, "y": 20}              // map literal
{}                              // empty map
```

In expression position `{…}` is always a map literal (never a block). A map key
list may have a trailing comma.

### Struct literals

Positional (fields in declaration order):

```kinglet
Point p { 3, 4 };
Box<int> b { 42 };
```

Inline struct literal in expression position requires the type name to start
with an uppercase letter (parser heuristic to disambiguate from a block):

```kinglet
foo(Point { 1, 2 });
```

### Match

`match` is a postfix expression: `value match { arms }`. Arms use `pattern =>
expr`, comma-separated, trailing comma allowed. An optional guard uses
`if (cond)`.

```kinglet
string grade(int score) {
  return score match {
    let x if (x >= 90) => "A",
    let x if (x >= 60) => "pass",
    _ => "fail",
  };
}
```

Patterns:

```kinglet
_                          // wildcard
let x                      // binding
42                         // literal
Color::Green               // enum variant (no payload)
Shape::Circle(let r)       // enum variant with payload binding
Shape::Rect(let w, let h)
[let a, let b]             // array pattern
Point { let x, let y }     // struct pattern (field bindings)
Point { x: let px }        // struct pattern (named field)
```

Enum destructuring in match:

```kinglet
float area(Shape s) {
  return s match {
    Shape::Circle(let r) => 3.14 * r * r,
    Shape::Rect(let w, let h) => w * h,
    Shape::None => 0.0,
  };
}
```

The checker enforces exhaustiveness for enum, bool, and nullable match subjects.

## Built-in methods

The type checker recognizes these methods on built-in types (no import needed).

### String

```kinglet
s.len()                    -> int
s.contains(sub)            -> bool
s.starts_with(pre)         -> bool
s.ends_with(suf)           -> bool
s.index_of(sub)            -> int
s.slice(start, end)        -> string
s.replace(from, to)        -> string
s.split(sep)               -> string[]
s.trim()                   -> string
s.to_upper()               -> string
s.to_lower()               -> string
```

Strings support `+` (concatenation), comparison operators, and indexing
(`s[i]` yields a char).

### Array

```kinglet
a.len()                    -> int
a.push(x)                  -> void
a.pop()                    -> element
a.remove(index)            -> element
a.insert(index, value)     -> void
a.contains(x)              -> bool
a.index_of(x)              -> int
a.slice(start, end)        -> element[]
a.resize(count, default)   -> void
a.reverse()                -> void
a.clear()                  -> void
```

### Map

```kinglet
m.len()                    -> int
m.has(key)                 -> bool
m.remove(key)              -> void
m.keys()                   -> keytype[]
```

Reading a missing map key yields `null` (compare with `== null`).

## Runtime namespaces

Four hardcoded runtime namespaces are opened with `using` (never `import`):

### io

```kinglet
using io;

io::out("{} {}\n", a, b);     // formatted write to stdout ({} placeholders)
io::out.line(x);              // write x plus a newline
io::out.line("x={}", v);      // formatted line
io::out.flush();
io::err.line("oops");         // stderr
string s = io::in();          // read a line
string p = io::in.secret();   // read without echo
```

`{}` in a format string is a value placeholder; the checker validates argument
count against placeholders.

### fs

```kinglet
using fs;

string data = fs::__read(path);
fs::__write(path, data);
string[] entries = fs::__listdir(path);
```

### sys

```kinglet
using sys;

string[] argv = sys::args();
```

### rt

Low-level runtime intrinsics (e.g. `rt::enum_payload_at`), used by the
self-hosting compiler; not intended for general application code.

## A complete example

```kinglet
using io;

struct Point {
  int x;
  int y;
}

enum Shape {
  Circle(float),
  Rect(float, float),
}

float area(Shape s) {
  return s match {
    Shape::Circle(let r) => 3.14 * r * r,
    Shape::Rect(let w, let h) => w * h,
  };
}

int main() {
  Point p { 3, 4 };
  io::out.line("point=({}, {})", p.x, p.y);

  Shape c = Shape::Circle(2.0);
  io::out.line("area={}", area(c));

  int[] xs = [1, 2, 3, 4, 5];
  int sum = 0;
  for (int i = 0; i < xs.len(); i = i + 1) {
    sum = sum + xs[i];
  }
  io::out.line("sum={}", sum);

  return 0;
}
```

## Notable non-features

Things a C/C++/Rust programmer might expect that Kinglet does **not** have:

- No `++` / `--` (use `i = i + 1`).
- No compound assignment on indexed targets (`arr[i] += 1` is rejected; write
  `arr[i] = arr[i] + 1`).
- No `do`/`while` or `switch` (use `match`).
- No labelled blocks or `goto`.
- `when`, `spawn`, `select` are reserved words with no implementation yet.
- Block comments do not nest.
