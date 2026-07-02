# Project Manifest (`kinglet.nest`) and Modules

A Kinglet project is described by a `kinglet.nest` manifest at its root. The manifest
declares **build targets** (what artifacts to produce and from which sources). Module
identity lives in the `.kl` source files themselves, C++20-modules style — the manifest
never maps module names to files.

## Manifest format

```nest
project "myapp" version "0.1.0"

target calc {
  kind    = "binary"
  sources = [
    "src/calc.kl",
    "math/",
    "geo/",
    "utils/config.kl",
  ]
}

target app {
  kind    = "binary"
  sources = ["src/app.kl", "math/"]
}

build {
  default = "calc"
  out     = ".kinglet/out"
  cache   = ".kinglet/cache"
}

fmt {
  indent    = 2
  max_width = 80
}
```

### `project`
`project "<name>" version "<version>"` — required, one line.

### `target <name> { ... }`
A build target. Fields:
- `kind` — one of:
  - `binary` (default) — an executable. Exactly one source must define `int main()`;
    that file is the entry, auto-detected. Zero or multiple mains is an error.
  - `library` — no entry; exports its modules to targets that list it in `deps`.
  - `test` — an executable run by the test runner (built like `binary` for now).
  - `object` — compile to object files only, no link (parse-only support at present).
- `sources` — a list of `.kl` files and/or directory globs. A trailing-slash entry
  (`"math/"`) or an actual directory globs every `*.kl` inside it (sorted). Everything
  else is a literal file path, relative to the project root.
- `deps` — a list of other target names whose exported modules become importable here.

### `build`
- `default` — the target built by `kinglet build` with no argument.
- `out` — output directory for artifacts (default `.kinglet/out`).
- `cache` — object cache directory (default `.kinglet/cache`).

### `fmt`
Formatter settings: `indent`, `max_width`, `newline` (`"lf"`/`"crlf"`),
`trailing_comma`, `extensions`.

## Building

```
kinglet build            # builds build.default
kinglet build app        # builds the target named "app"
```

The output binary is named after the target (`.kinglet/out/<target>`).

## Modules (C++20 style)

A `.kl` file may declare its module name:

```kl
export module math.basic;

pub int square(int x) { return x * x; }
```

Another file imports it by name — not by path:

```kl
export module calc;
using io;
import math;            // group import: every module named math.*
import geo.point;       // exact import: just geo.point
import utils.config;

int main() {
  io::out.line("{}", math::basic::square(4));   // math.basic
  Point p = geo::point::new_point(3, 7);        // geo.point
  io::out.line("{}", utils::config::app_name());
  return 0;
}
```

### Resolution rules
- `import a.b;` resolves to the single source declaring `export module a.b;`.
- `import x;` where `x` is not itself a module name is a **group import**: it loads every
  module whose name starts with `x.` (e.g. `import math;` pulls in `math.basic` and
  `math.advanced`). Access each via its full path: `math::basic::square`.
- Resolution searches the sources of the current target plus its `deps`. A name with no
  matching `export module` anywhere is a hard error — there is no directory-scan fallback
  and no manifest name→file table.
- `io`, `fs`, `sys`, `rt` are built-in runtime namespaces. Bring them in with `using`
  (`using io;`), never `import`.

### Directory globs vs. namespaces
Listing `"math/"` in `sources` only adds the directory's `.kl` files to the target. It
does **not** impose any namespace — each file still declares its own `export module`. The
namespace is whatever the file says, not where the file lives.
