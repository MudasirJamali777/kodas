Kodas Sprint 9 - Native backend expansion
========================================

Applies on top of Sprint 8 (f07776b) or Sprint 7.

Adds native compilation support for:
  - match (literal / wildcard / binding / enum-variant patterns)
  - for x in col { ... } and for i, x in col { ... }
  - integer ranges a..b (exclusive) and a..=b (inclusive) in for-in
  - set literals {a, b, c}
  - map literals {key: val, ...} with indexing
  - string interpolation "Hello, ${name}!"
  - standalone 'while' and 'for' (previously only 'loop while')
  - return statement (early return from function)
  - enum declarations (registers as type-value for future variant construction)
  - test "name" { ... } blocks (run automatically, print "ok" on success)
  - chained static/instance methods: Point.new(1,2).add(b).display()
  - soft keywords 'new' / 'type' / 'self' as valid method/function names
  - lambdas parsed and emitted as __lambda_N stubs (env captures still WIP)

Also fixes:
  - behave/impl method bodies now reuse parse_fn, fixing hang when a method
    body contains a struct literal (was caused by inline parser re-implementation)
  - double-release of for-in loop variables caused segfault on iteration 2+
  - retain-on-assign for match/interp results (use-after-free)
  - global table size increased to 4096 entries
  - set/map/block disambiguation in brace parsing (no longer misidentifies
    { s = s + i } as a set literal)
  - two-pass codegen counts lambdas before emitting, avoiding 256 placeholder decls

Applies via:  git am kodas-sprint9.patch
Then:         cd seed && make
