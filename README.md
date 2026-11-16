# KODAS v0.17 · Where Logic Knows No Bounds

KODAS is a programming language that compiles to C and produces standalone
native binaries. Designed by Mudasir Ali, built solo, in public.

- **Compiles to C** — every program becomes a portable .c file that your system
  C compiler (gcc/clang/cl) optimizes into a native binary.
- **Optional types.** Annotations help you; you don't serve them. Duck-typed at
  runtime when you leave them out.
- **`struct`, `enum`, `trait`, `impl`, `dyn`, closures, pattern matching,
  `Result/Maybe`, channels, HTTP, JSON** — the battery pack you actually use.
- **Two mascots**: Morty the dog (compiler/native side), Kun the cloud
  (website/online side).

## Install

```bash
# Linux / macOS (WSL works on Windows)
curl -fsSL https://kodas.xo.je/install | sh

# Windows: download kodas-windows-x86_64.zip, extract, double-click install.bat
```

## Try it

```bash
echo 'fn fib(n){if n<2{give n} give fib(n-1)+fib(n-2)}
show fib(35)' > fib.kds
kodas run fib.kds      # compiles to C, builds, runs
kodas build fib.kds    # produces standalone native binary ./fib
```

## The language in 60 seconds

```kds
trait Greet { fn hello(self) -> String }

struct User { name: String, age: int }

impl Greet for User {
    fn hello(self) -> String { give "Hi, I'm " + self.name }
}

fn greet(name = "World") {
    let alice = User { name: name, age: 30 }
    show alice.hello()
    show dyn("Greet", alice).hello()
}

greet()
greet("Alice")
[1,2,3].map(fn(x) { x * 2 }).filter(fn(x) { x > 2 })
```

Keywords: `let`, `let mut`, `fn`, `struct`, `enum`, `trait`, `impl`, `match`,
`give`, `if`, `while`, `for`, `loop`, `show`, `assert`, `spawn`, `dyn`.

`give` means return. `show` prints. `mut` makes a binding mutable.

## Speed (v0.17, recursive `fib(35)`, single run, real time)

|           | C -O2 | Node.js | **KODAS** | Python 3 |
|-----------|-------|---------|-----------|----------|
| fib(35)   | 0.01s | 0.07s   | **0.76s** | 0.81s    |

KODAS v0.17 is ~17× faster than v0.16 (12.8s → 0.76s) thanks to arity-
specialized fast calls, an open-addressing global hash table, and a small-integer
cache that eliminates malloc/free on the hot path. Binary size: ~220 KB, zero
external dependencies, deterministic reference counting (no GC pauses).

Tight integer loop 1..10M: **0.30s** (KODAS v0.17).

## Layout of this repository (for Visual Studio / GitHub)

```
.
├── src/
│   ├── main.c              # compiler entry point (kodas run/build/ast/lex)
│   ├── lexer.c/.h          # tokenizer
│   ├── parser.c/.h         # recursive-descent parser → AST
│   ├── ast.c/.h            # AST node types
│   ├── eval.c/.h           # tree-walking interpreter fallback
│   ├── value.c/.h          # interpreter-level value types
│   ├── environment.c/.h    # lexical scopes
│   ├── fs.c/.h             # file I/O helpers
│   ├── common.h            # shared macros / allocators
│   ├── platform/           # platform abstraction (kodas_platform.h)
│   └── codegen/
│       ├── codegen_krt.c   # KRT backend: Kodas → C transpiler (full language)
│       ├── codegen_c.c     # C backend: subset → fast unboxed C (numeric kernels)
│       ├── krt.c/.h        # Runtime library linked into every built binary
│       ├── stdlib_extra.c  # Extra natives (JSON, fs, string/list helpers)
│       ├── stdlib_http.c   # HTTP client + server natives
│       └── codegen_llvm.c  # (placeholder for future LLVM backend)
├── stdlib/                 # Standard library modules (core, web, io, ...)
├── tools/                  # Tooling: lsp, formatter, linter, repl, docgen
├── ide/                    # VS Code extension metadata (syntaxes, snippets)
├── kargo/                  # Kargo package manager (WIP)
├── website/                # Landing page (Kun side)
├── release/                # Release binaries + installers (for kodas.xo.je)
├── Makefile                # `make` → builds ./kodas; `make dev` for debug build
└── kargo.toml              # Package manifest (like Cargo.toml)
```

## Building from source

```bash
make            # builds optimized ./kodas (~220 KB)
make dev        # debug build (O0 + debug symbols)
make test       # smoke test
```

Requires `gcc` or `clang` on Linux/macOS, or MSVC/MinGW on Windows. The compiler
invokes your system C compiler automatically for `kodas run` / `kodas build`.

## v0.17 changelog (Sprint 17: Speed)

- **17× faster hot path** via arity-specialized `k_call0/1/2/3` and
  `k_method0/1/2` — no more building argv arrays on every call.
- **Open-addressing global hash table** replaces linear-scan global lookup —
  global function calls are O(1) now, not O(n).
- **Small-integer cache** (-128..4096) immortalizes common ints — they never hit
  malloc/free. null/true/false are immortal singletons.
- **Compiler flags**: `kodas run/build` now invokes gcc with `-O3 -flto -fno-plt`.
- Compiler itself builds with `-O3 -flto` and strips to 220 KB.
- Recursive `fib(35)`: 12.8s → **0.76s** (faster than Python 3).
- Tight loops 1..10M: 1.2s → **0.30s**.

## Mascots

- **Morty** 🐕 — compiler, backend, native binaries. Loyal, a little slow on
  the first lap but faster every sprint.
- **Kun** ☁️ — website, docs, online playground. Always online.

## License

Released by Mudasir Ali. Use it, fork it, ship it.
