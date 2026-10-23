/// codegen_c.h - C transpilation backend
#ifndef KODAS_CODEGEN_C_H
#define KODAS_CODEGEN_C_H

#include <stdio.h>

/// Emit C source for a Kodas program. The source_path is the entry .kds file;
/// imported stdlib modules are inlined into the emitted C so the resulting
/// binary is a single self-contained executable.
/// Returns 0 on success, non-zero on error.
int codegen_c_emit_file(const char* source_path, const char* c_out_path);

#endif
