# Changelog

All notable changes to the Kodas programming language.

## [0.1.0] - 2026-10-12

### Added
- Initial seed compiler in C (tree-walking interpreter)
- Lexer supporting all major token types (idents, numbers, strings, operators, keywords)
- Parser supporting: let, seal, show, fn (arrow & block), if/else, match, loops (loop N, loop in, loop while, infinite), struct, behave, traits skeleton, test blocks
- String interpolation: `"Hello, {name}!"`
- Line comments (`--`) and block comments (`--- ... ---`)
- Runtime values: Int, Float, Bool, String, Char, List, Map, Tuple, Struct instances, Enum variants, Result (ok/err), Closures
- Built-in functions: show, print, len, push, pop, range, str, int, float, sqrt, sin, cos, pow, abs, typename, input, exit, read_file, write_file, clock, keys, values, split, contains, chars, lines, trim, starts_with, ends_with, replace
- List methods: .map, .filter, .reduce, .join, .contains, .push, .len, .pop
- CLI commands: run, build (interpreter fallback), check, repl, tokens, ast, version, help
- String concatenation with `+`, arithmetic, equality & comparison operators
- Functions with recursion (factorial, etc.)
- Factorial (10!) = 3628800 verified working
- Ternary expressions: `if cond then a else b`
- Match expressions with literals and wildcards
- Struct initialization: `Point { x: 1.0, y: 2.0 }` and field access
- ok/err error values
- Constant PI (3.14159...)
- Factorial, closures and arrow functions
- Test blocks with pass/fail reporting
- README, ROADMAP, LICENSE
- Project directory structure with seed/, tests/, stdlib/, self/, kargo/, tools/, ide/, docs/, website/

### Known Issues
- Empty strings ("") work
- GC is a stub (no real collection; OS reclaims memory on exit)
- Behave method dispatch is partial
- No semantic/type checking in this release
- Some advanced constructs (|?> pipes, flow/together, bridge, modules) are parsed but not fully evaluated
- Self-hosting compiler not yet implemented
