/// codegen_c.c - C transpilation backend (placeholder)
/// This is a stub for Phase 2 - native code generation.
///
/// The C backend transpiles Kodas AST to C code, which is then compiled
/// with the system C compiler. This provides a fast path to native binaries
/// without requiring LLVM as a dependency.
///
/// Planned features:
///   - Full AST -> C source translation
///   - Type declarations -> C structs
///   - Functions -> C functions
///   - Pattern matching -> C switch statements
///   - Closures via function pointers + environment structs
///   - GC via Boehm or reference counting
///
/// Usage (future):
///   kodas build --target c program.kds -o program
#include <stdio.h>

void codegen_c_emit(const char* source_path, const char* output_path) {
    (void)source_path; (void)output_path;
    fprintf(stderr, "C codegen not yet available in seed; running interpreter\n");
}
