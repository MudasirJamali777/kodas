# 🚀 KODAS

> "Code should read like poetry, execute like lightning, and debug like a conversation."

Kodas is a new programming language designed for developer joy: no null pointers, no try/catch hell, no async complexity, no boilerplate. Built for correctness, expressiveness, and speed.

## Features (Seed v0.1.0)

- **Variables & Constants**: `let`, `let mut`, `seal`
- **First-class functions**: `fn name(...) { ... }` and arrow `fn(x) => x * 2`
- **String interpolation**: `"Hello, {name}!"`
- **If/else expressions** + ternary `if cond then a else b`
- **Loops**: `loop N`, `loop x in col`, `loop while cond`, infinite `loop`
- **Lists** with `.map`, `.filter`, `.reduce`, `.push`, `.len`, `.join`
- **Ranges**: `1..10`
- **Match expressions** with literals, wildcards, ranges
- **Structs**: `struct Point { x: Float, y: Float }` and field access
- **Error values**: `ok(x)`, `err(msg)`, `give err("...")`
- **Built-in tests**: `test "name" { assert ... }`
- **Built-in math**: `PI`, `sqrt`, `sin`, `cos`, `pow`, `abs`
- **I/O**: `show`, `print`, `input`, `read_file`, `write_file`
- **CLI**: `kodas run`, `kodas check`, `kodas repl`, `kodas tokens`, `kodas ast`

## Quick Start

```bash
cd seed
make
./kodas run ../tests/hello.kds
```

### Hello, World
```kodas
let name = "World"
show "Hello, {name}!"
```

### Factorial
```kodas
fn factorial(n) {
    if n <= 1 { give 1 }
    give n * factorial(n - 1)
}
show factorial(10)   -- 3628800
```

### Lists and Pipelines
```kodas
let numbers = [1, 2, 3, 4, 5, 6]
let evens = numbers.filter(fn(n) => n % 2 == 0)
let doubled = evens.map(fn(n) => n * 2)
show doubled   -- [2, 4, 6, 8, 10, 12]
```

### Structs
```kodas
struct Point { x: Float, y: Float }
let p = Point { x: 3.0, y: 4.0 }
show "({p.x}, {p.y})"
```

### Tests
```kodas
test "addition works" {
    assert 2 + 2 == 4
}
```

## Building

```bash
cd seed
make             # builds kodas binary
make test        # runs test suite
```

**Requirements**: GCC or Clang with C11 support, `make`.

## CLI Commands

```
kodas run <file.kds>        Run a Kodas program
kodas check <file.kds>      Parse and check a file
kodas repl                  Start interactive REPL
kodas tokens <file.kds>     Show tokens (debug)
kodas ast <file.kds>        Show AST info (debug)
kodas version               Show version
kodas help                  Show help
```

## Project Structure

```
kodas/
├── seed/           # Seed compiler (C, tree-walking interpreter)
│   └── src/        #   Source: lexer, parser, AST, values, env, eval, main
├── tests/          # Test programs (.kds)
├── self/           # Future self-hosting compiler (written in Kodas)
├── stdlib/         # Standard library (Kodas)
├── kargo/          # Package manager
├── tools/          # Tooling (formatter, lsp, repl, linter)
├── ide/            # KodasForge IDE
├── docs/           # Documentation
├── website/        # Website
├── vscode-extension/ # VS Code extension
├── ROADMAP.md      # Detailed roadmap
├── CHANGELOG.md    # Version history
├── LICENSE         # GPL-3.0
└── README.md       # This file
```

## Upcoming Features (see ROADMAP.md)

- `|>` and `|?>` (normal & error pipes)
- `behave` blocks for methods
- `trait` definitions
- `enum` with variants
- Generics `[T]`
- `maybe T` option types
- `flow` / `together` concurrency with channels
- `import` / `module` system
- Semantic analysis / type checking
- LLVM native code generation
- WebAssembly backend
- Self-hosting compiler
- Standard library (web, db, crypto, gui, ...)
- Kargo package manager
- KodasForge IDE (Tauri + web frontend)
- And much more...

## Philosophy

Kodas aims to eliminate the common pain points developers face every day:

- **Null pointer exceptions** → `maybe T` + `empty` (no null!)
- **Try/catch hell** → `|?>` error pipe for happy-path chaining
- **Async complexity** → `flow` + `together`
- **Boilerplate** → Auto-derived behaviors from structs
- **"Works on my machine"** → Built-in contracts
- **Dependency hell** → Built-in polyglot bridge

## License

GPL-3.0 - see [LICENSE](LICENSE).

## Author

Mudasir Jamali ([@MudasirJamali777](https://github.com/MudasirJamali777))

---

*Make code feel like magic.*
