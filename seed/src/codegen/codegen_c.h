/// codegen_c.h - C transpilation backend
#ifndef KODAS_CODEGEN_C_H
#define KODAS_CODEGEN_C_H

#include <stdio.h>
#include "../parser.h"

/// Emit C source for a Kodas program (subset backend). Returns nonzero on failure.
int codegen_c_emit_file(const char* source_path, const char* c_out_path);

/// Emit C source for a parsed Program using the full KVal (KRT) runtime.
/// Emits #include "krt.h"; the compile step must compile+link krt.c alongside.
/// Returns 1 on success, 0 on error.
int codegen_krt(Program* prog, const char* out_path, const char* source_path);

#endif
