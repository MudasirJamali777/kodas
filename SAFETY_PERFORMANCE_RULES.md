# Kodas — Safety, Performance & Engineering Rules (v1.0 Lock-In)

These rules are **non-negotiable** for all code merged into the Kodas compiler,
runtime, or stdlib. Every sprint must maintain or improve on these invariants.

---

## 1. Memory Safety

1.1 **No undefined behaviour.** The compiler and KRT (Kodas Runtime) must compile
    cleanly under `-Wall -Wextra -Werror` on GCC and Clang, and pass an
    ASan/UBSan build (`-fsanitize=address,undefined`) on the test suite with
    zero leaks and zero errors.
1.2 **Bounds checking is always on.** All list/string/bytes/slice accesses in
    generated code must go through a bounds-checked KRT primitive. No raw
    pointer arithmetic is emitted by the code generator for user code.
1.3 **No null-pointer dereference.** `Option(T)` / `maybe T` and `Result(T,E)`
    are mandatory for fallible operations. `demand` (`!`) is an explicit opt-in
    that traps with a clear runtime error, never UB.
1.4 **Every allocation is paired with a free.** The KRT uses an arena-per-value
    model for temporary values; GC / reference counting covers all heap objects
    reachable from the root set. `valgrind --leak-check=full` must report zero
    leaks on test programs.
1.5 **No `strcpy`, `sprintf`, `gets`, `strcat`** anywhere in the codebase. Use
    `snprintf`, bounded `memcpy`, and KRT `buf_*` helpers exclusively.
1.6 **Integer overflow** is defined: signed arithmetic traps in debug builds,
    wraps with two's-complement in release (documented), and never causes UB.

## 2. Concurrency Safety

2.1 **No data races by construction.** Shared-mutable state requires `chan`,
    `Mutex`, or atomic primitives from `stdlib/concurrency`. The compiler
    rejects bare cross-thread capture of mutable references (enforced in
    Sprint 16+ borrow/mut analysis).
2.2 **Channels are MPMC, bounded or unbounded, and never deadlock silently.**
    Every blocking channel op has a documented select/otherwise alternative.
2.3 **Threads are joined or detached explicitly.** No orphaned threads;
    `thread.spawn` returns a handle whose destructor joins in debug mode.

## 3. Error Handling

3.1 **No silent failures.** Every stdlib function that can fail returns
    `Result(T,E)` (or `maybe T`). No `NULL` sentinels, no `-1` magic values, no
    swallowed `errno`.
3.2 **Errors carry source spans.** Runtime panics print file:line:column and a
    stack trace (debug builds) or file:line (release).
3.3 **`|>` (pipe) propagates errors; `|?>` (error pipe) short-circuits.** Never
    mix the two in user code without an explicit `otherwise` clause.
3.4 **Compiler errors are actionable** — they point at the token, show the
    source line with `^^^`, say what was expected, and never say "syntax
    error" without context (Sprint 19).

## 4. Performance Rules

4.1 **Compile-time cost target:** 50k lines/sec on a single core for the
    C-backend pipeline (lex → parse → codegen → gcc -O0).
4.2 **Runtime cost target:** naive recursive `fib(40)` must be within 2× of
    equivalent C.
4.3 **Zero-cost abstractions:** closures, iterators, `|>` pipelines, struct
    methods, and monomorphised generics must inline to direct calls under `-O2`.
    No heap allocation in hot paths unless the user asked for it.
4.4 **No hidden allocations.** String concatenation, list append, and boxing
    are explicit in the generated C; any stdlib function that allocates is
    documented with `/* alloc */` in krt.c.
4.5 **I/O is non-blocking in the server path.** `http.listen` uses an
    epoll/kqueue thread pool (Sprint 21 dogfood must hit 10k RPS on "hello
    world").
4.6 **Binary size budget:** stripped `kodas` compiler ≤ 5 MB; hello-world
    static binary ≤ 200 KB.
4.7 **Bootstrap:** the compiler self-hosts in `self/compiler/` once v1 is
    tagged; before then, C source is the source of truth and the KRT is
    auditable line-by-line.

## 5. Code-Generation Discipline

5.1 **The C backend emits deterministic, human-readable C.** Every `.kds.c`
    artifact compiles with `gcc -Wall -Wextra -pedantic` with zero warnings.
5.2 **KRT (`krt.c`) is the only unsafe code in the system.** User programs
    must never call libc directly — only KRT functions.
5.3 **Name mangling is stable** so that cross-file LTO works across sprint
    rebuilds.
5.4 **Source spans are preserved** through to runtime panics; `Location` structs
    are carried on every AST node and on every closure/call generated.
5.5 **Frame/TLS restore is mandatory** on every thread entry point. The
    `k_current_frame` __thread variable (Sprint 13 HTTP bug) must be saved and
    restored in every pthread wrapper — no exceptions, no early returns.

## 6. Testing Rules

6.1 **Every sprint ships ≥ 3 new tests** for the feature it adds. Tests live in
    `tests/` and run from the `seed/` directory via `kodas build <file>`.
6.2 **No regression.** The test count is monotonic — a failing test must be
    fixed, not deleted.
6.3 **Before each Drive upload** the deliverable script must:
    1. `make clean && make` from `seed/`
    2. Run the full `tests/` suite (all 32+ programs) and confirm zero failures
    3. `strip kodas` and remove `*.o` artifacts
    4. Verify zip contents: `unzip -l` must show all `seed/src/*.c`,
       `seed/src/*.h`, `seed/src/codegen/*.c`, `seed/Makefile`, `seed/kodas`
    5. Smoke-test the binary inside the zip before upload
6.4 **Dogfood programs (Sprints 21-22)** must include: HTTP server, CLI tool
    (wc/todo), TCP chat. They are the v1.0 acceptance tests.

## 7. Build & Release Hygiene

7.1 **Never upload an empty zip.** Before uploading, run
    `unzip -l <bundle>.zip | grep -c '\.c$'` and confirm the count equals the
    number of `.c` files in `seed/src/` + `seed/src/codegen/` (currently 16).
7.2 **Never ship debug symbols in release bundles.** Always `strip` the
    `kodas` binary and delete `*.o`.
7.3 **Every sprint zip is self-contained:** `unzip` then `cd seed && make`
    produces a working compiler from source.
7.4 **Version is monotonic.** `KODAS_VERSION` in `main.c` and `README.md`
    are bumped each sprint; v1.0 is tagged on Sprint 23.
7.5 **Google Drive is the source of truth for published sprints.** Every zip
    gets a fresh file id when re-uploaded; delete the broken one first so the
    folder never contains duplicates.

## 8. Language Invariants

8.1 **No breaking syntax changes after v1.0.** Anything that parses today
    parses on v1.x.
8.2 **Imports are deterministic.** `import "path"` resolves relative to the
    including file, deduplicates by realpath, and refuses to load more than
    `MAX_INCLUDES=64` files to prevent cycles.
8.3 **ASI is conservative.** A newline only inserts a semicolon when the
    previous token ends an expression, the next token starts a statement, and
    the previous token is not a continuation operator (`+ - * / % . , ( [ { =>
    |>` etc.).
8.4 **Whitespace is never significant beyond ASI.** Indentation does not
    change semantics.
8.5 **All values are printable.** `io.println(v)` works for every type — int,
    float, string, bool, list, struct, enum, closure, maybe, result, fn.

## 9. Security

9.1 **No shell-out without explicit user opt-in.** `os.exec` requires a flag.
9.2 **`http.get` / `http.post** validate URLs and enforce a redirect cap (5).
9.3 **`fs.read_file`/`fs.write_file` sandboxing** honors `--cwd` when set;
    path traversal is rejected outside the project root (Sprint 18 project
    model).
9.4 **The REPL never auto-executes imported code with side effects beyond
    `fn`/`struct`/`enum`/`behave`/`impl` declarations.**

## 10. How to verify a build before uploading

```
cd seed
make clean && make          # must build clean
strip kodas                 # no debug info in release
find src -name '*.o' -delete
cd ..
# run all tests
for t in tests/*.kds; do
  seed/kodas build "$t" > /dev/null 2>&1 || echo "FAIL: $t"
done
# build the bundle
zip -r kodas-sprintN.zip seed/ stdlib/ docs/ examples/ tests/ self/ tools/ \
                     vscode-extension/ website/ .github/ kargo/ ide/ \
                     README.md ROADMAP.md CHANGELOG.md LICENSE \
                     SAFETY_PERFORMANCE_RULES.md
unzip -l kodas-sprintN.zip | grep -c '\.c$'   # must equal real count
unzip -l kodas-sprintN.zip | grep -c 'seed/kodas$'  # must be 1
```

Follow these rules on every sprint. No exceptions.
