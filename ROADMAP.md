# Kodas Language Roadmap

## Phase 1: Seed Compiler (C) - IN PROGRESS
- [x] Lexer (tokens, strings, numbers, operators)
- [x] Parser (expressions, statements, blocks)
- [x] Tree-walking interpreter
- [x] Variables (let, mut, seal)
- [x] Functions (fn, => arrows, recursion, closures basics)
- [x] Strings, Ints, Floats, Bools
- [x] Arithmetic (+ - * / %)
- [x] String concatenation (+)
- [x] String interpolation ("Hello, {name}!")
- [x] Comments (-- single line, --- multi-line ---)
- [x] If/else expressions
- [x] Ternary (if cond then a else b)
- [x] Lists & list methods (map, filter, reduce, push, len)
- [x] Ranges (.., ..=)
- [x] Loops (loop N, loop in, loop while, infinite loop)
- [x] Match expressions (literals, wildcards, ranges)
- [x] Structs (declaration & initialization)
- [x] Error handling (ok/err, give err)
- [x] Built-in functions (show, print, len, push, sqrt, input, read_file, write_file, ...)
- [x] Test blocks (test "..." { ... })
- [x] CLI (run, check, tokens, ast, build, repl, version, help)
- [x] Float literals (scientific notation) - partially done
- [x] Char literals - lexer done
- [x] Raw strings r"..." - lexer token defined
- [x] Multiline strings """...""" - lexer token defined
- [x] Compound assignments (+=, -=, *=, /=, %=, string +=, field targets)
- [x] Default parameters
- [x] Multiple return values (tuples + let-destructuring with wildcards/mut/swapping)
- [x] Behave methods (full method dispatch, self/mut self/static ctors)
- [x] Traits (trait definitions, impl Trait for Type blocks, default methods, duck-typed dispatch)
- [x] Enums with payload (single + multi-payload, pattern matching)
- [x] Generics [T], [T: Trait] (duck-typed; monomorphization deferred to self-host)
- [x] maybe T + empty (empty keyword + Option-style enum pattern matching)
- [x] |>, |?> pipe operators (forward + error pipe with short-circuit)
- [x] otherwise / demand (`x otherwise default`, `x demand` for Result/maybe)
- [x] flow / together / timeout concurrency (keywords parsed; thread-pool deferred)
- [x] Channel[T] (keyword parsed; type stub)
- [x] bridge blocks (parsed; single-file seed skips)
- [x] import / module (parsed; single-file seed skips)
- [x] bench blocks (real timing via clock_gettime)
- [x] contract blocks (parsed; asserts inline)
- [x] @decorators (parsed; attached but unused in seed)
- [ ] Operator overloading
- [ ] List/map/set comprehensions
- [ ] Lambda short form with _
- [x] Field access and method calls
- [ ] Semantic analyzer (type checking)
- [ ] Pattern exhaustiveness checking
- [ ] GC (mark-and-sweep)
- [ ] Concurrency (thread pool, channels)
- [ ] FFI bridge system
- [ ] Beautiful error messages

## Phase 2: Native Code Generation
- [ ] codegen_llvm.c (LLVM IR backend)
- [ ] codegen_wasm.c (WebAssembly backend)
- [ ] codegen_js.c (JavaScript transpilation)
- [x] codegen_c.c (C transpilation fallback — working subset: fn/let/if/while/give/recursion/arith/show/assert; auto-invokes gcc/clang)

## Phase 3: Self-Hosting
- [ ] self/compiler/lexer.kds
- [ ] self/compiler/parser.kds
- [ ] self/compiler/ast.kds
- [ ] self/compiler/semantic.kds
- [ ] self/compiler/codegen.kds
- [ ] self/compiler/main.kds
- [ ] Verify stage3 == stage2

## Phase 4: Standard Library
- [x] core/types.kds
- [x] core/collections.kds
- [x] core/strings.kds
- [x] core/math.kds
- [ ] core/time.kds
- [ ] core/maybe.kds
- [ ] core/result.kds
- [ ] core/iter.kds
- [ ] io/file.kds
- [ ] io/stdio.kds
- [ ] io/net.kds
- [ ] io/path.kds
- [ ] web/http.kds
- [ ] web/router.kds
- [ ] web/websocket.kds
- [ ] web/json.kds
- [ ] web/html.kds
- [ ] db/postgres.kds, sqlite.kds, mysql.kds, mongo.kds
- [ ] crypto/*
- [ ] test/*
- [ ] concurrency/*
- [ ] gui/*
- [ ] system/*

## Phase 5: Kargo Package Manager
- [ ] kargo/src/cli.kds
- [ ] kargo/src/registry.kds
- [ ] kargo/src/resolver.kds
- [ ] kargo/src/installer.kds
- [ ] kargo/src/publisher.kds
- [ ] kargo.toml config

## Phase 6: Tooling
- [ ] kodas fmt (formatter)
- [ ] kodas repl (enhanced)
- [ ] kodas lsp (LSP server)
- [ ] kodas doc (doc generation)
- [ ] kodas lint (linter)

## Phase 7: KodasForge IDE
- [ ] Tauri backend
- [ ] Svelte/React frontend
- [ ] Code editor (Monaco/CodeMirror)
- [ ] File explorer
- [ ] Integrated terminal
- [ ] Debugger
- [ ] Git integration
- [ ] Settings UI

## Phase 8: Documentation & Website
- [ ] docs/spec.md
- [ ] docs/tutorial.md
- [ ] docs/stdlib/
- [ ] docs/cookbook.md
- [ ] docs/migration/
- [ ] docs/architecture.md
- [ ] website/

## Phase 9: Testing
- [x] Unit tests (basic features)
- [ ] Integration tests
- [ ] Stdlib tests
- [ ] Compiler edge cases
- [ ] Regression tests
- [ ] Benchmarks
- [ ] Example programs
- [ ] 95%+ code coverage

## Phase 10: Release
- [x] GPL-3.0 license
- [ ] CI/CD (GitHub Actions)
- [ ] install.sh / install.ps1
- [ ] Docker images
- [ ] Homebrew formula
- [ ] Scoop manifest
- [ ] v1.0.0 release
