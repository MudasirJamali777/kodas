/// fs.h - File system helpers (cross-platform path resolution, file reading, existence)
#ifndef KODAS_FS_H
#define KODAS_FS_H

#include "common.h"

/// Read entire file into malloc'd buffer (caller frees). Returns NULL on failure.
char* fs_read_file(const char* path);

/// Return 1 if path exists and is a regular file; 0 otherwise.
int fs_file_exists(const char* path);

/// Join directory + separator + name into out (size out_size). Returns out.
char* fs_path_join(char* out, size_t out_size, const char* dir, const char* name);

/// Get directory component of path into dir_out (including trailing separator).
void fs_dirname(const char* path, char* dir_out, size_t dir_out_size);

/// Find the Kodas stdlib root. Searches, in order:
///   1. $KODAS_PATH if set
///   2. <exe_dir>/../stdlib/ (relative to kodas binary)
///   3. <cwd>/stdlib/
/// Returns path into a static buffer (valid until next call).
/// Set exe path from argv[0] so fs_stdlib_root can find stdlib relative to binary.
void fs_set_exe_path(const char* argv0);

const char* fs_stdlib_root(void);

/// Resolve a module path like "core/math" or "core/strings" to an absolute
/// `.kds` file path. Writes into out (size out_size). Returns 1 if found, 0 otherwise.
int fs_resolve_module(const char* current_file, const char* module_path, char* out, size_t out_size);

#endif
