/// codegen_llvm.c - LLVM IR backend (placeholder for Phase 2)
///
/// When LLVM is available, this module will:
///   - Create an LLVM module and context
///   - Emit LLVM IR for each AST node
///   - Run optimizations (O2/O3)
///   - JIT for `kodas run` or compile to object file for `kodas build`
///
/// Dependency: LLVM C API (llvm-c/*.h)
/// Install: winget install LLVM (Windows) or apt install llvm-dev (Linux)
#include <stdio.h>

void codegen_llvm_emit(const char* source_path, const char* output_path, int opt_level) {
    (void)source_path; (void)output_path; (void)opt_level;
    fprintf(stderr, "LLVM backend not yet available (requires LLVM C API)\n");
}
