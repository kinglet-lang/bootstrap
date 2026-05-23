# Kinglet Language Support

Language support for [Kinglet](https://github.com/sentomk/kinglet), a statically-typed, compiled programming language.

## Features

- **Syntax highlighting** — full TextMate grammar for keywords, types, strings, comments, operators
- **Diagnostics** — real-time error and warning reporting (type errors, unused variables)
- **Completions** — struct field completion (`.`), enum variant completion (`::`)
- **Hover** — type information and struct/enum definitions on hover
- **Document symbols** — outline view for functions, structs, enums

## Requirements

The extension requires the `kinglet-lsp` binary. Set the path in settings:

```json
{
  "kinglet.server.path": "/path/to/kinglet-lsp"
}
```

If not configured, the extension searches `~/bin/kinglet-lsp` and `/usr/local/bin/kinglet-lsp`.

## Language Overview

```kinglet
using io;

struct Point {
  int x;
  int y;
}

enum Color {
  Red,
  Green,
  Blue,
}

int main() {
  Point p { 10, 20 };
  Color c = Color::Red;

  inspect (c) {
    Color::Red => io::out("red"),
    Color::Green => io::out("green"),
    _ => io::out("other")
  };

  io::out(p.x);
  return 0;
}
```

## Extension Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `kinglet.server.path` | `kinglet-lsp` | Path to the LSP server binary |
