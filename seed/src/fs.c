/// fs.c - File system helpers
#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <libgen.h>
#define PATH_SEP '/'
#endif

char* fs_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int fs_file_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
}

char* fs_path_join(char* out, size_t out_size, const char* dir, const char* name) {
    if (!dir || !dir[0]) { snprintf(out, out_size, "%s", name); return out; }
    size_t dl = strlen(dir);
    char sep = '/';
    if (dl > 0 && (dir[dl-1] == '/' || dir[dl-1] == '\\')) sep = '\0';
    snprintf(out, out_size, "%s%s%s", dir, sep ? "/" : "", name);
    // Normalize backslashes to forward slashes for portability
    for (char* p = out; *p; p++) if (*p == '\\') *p = '/';
    return out;
}

void fs_dirname(const char* path, char* dir_out, size_t dir_out_size) {
    snprintf(dir_out, dir_out_size, "%s", path);
    char* last = NULL;
    for (char* p = dir_out; *p; p++) if (*p == '/' || *p == '\\') last = p;
    if (last) { *last = '\0'; }
    else { dir_out[0] = '.'; dir_out[1] = '\0'; }
    if (dir_out[0] == '\0') { dir_out[0] = '/'; dir_out[1] = '\0'; }
}

static char exe_dir[1024] = {0};

void fs_set_exe_path(const char* argv0) {
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (n > 0) {
        fs_dirname(buf, exe_dir, sizeof(exe_dir));
    } else {
        exe_dir[0] = '.'; exe_dir[1] = '\0';
    }
#else
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        fs_dirname(buf, exe_dir, sizeof(exe_dir));
    } else {
        // fallback: use argv0 if absolute
        if (argv0 && argv0[0] == '/') {
            fs_dirname(argv0, exe_dir, sizeof(exe_dir));
        } else {
            exe_dir[0] = '.'; exe_dir[1] = '\0';
        }
    }
#endif
}

const char* fs_stdlib_root(void) {
    static char root[1024] = {0};
    if (root[0]) return root;
    const char* env = getenv("KODAS_PATH");
    if (env && env[0]) {
        snprintf(root, sizeof(root), "%s", env);
        for (char* p = root; *p; p++) if (*p == '\\') *p = '/';
        return root;
    }
    // <exe_dir>/../stdlib
    if (exe_dir[0]) {
        snprintf(root, sizeof(root), "%s/../stdlib", exe_dir);
        if (fs_file_exists(fs_path_join((char*)&(char[1024]){0}, 1024, root, "core/math.kds"))) return root;
    }
    // ./stdlib (cwd)
    if (fs_file_exists("stdlib/core/math.kds")) { snprintf(root, sizeof(root), "stdlib"); return root; }
    // ../stdlib (from seed/)
    if (fs_file_exists("../stdlib/core/math.kds")) { snprintf(root, sizeof(root), "../stdlib"); return root; }
    root[0] = '\0';
    return NULL;
}

int fs_resolve_module(const char* current_file, const char* module_path, char* out, size_t out_size) {
    char candidate[2048];
    // Convert dotted module path "core.math" or "core/math" to path "core/math.kds"
    char mp[1024];
    snprintf(mp, sizeof(mp), "%s", module_path);
    for (char* p = mp; *p; p++) if (*p == '.') *p = '/';

    // 1. Relative to current file's directory
    if (current_file) {
        char dir[1024];
        fs_dirname(current_file, dir, sizeof(dir));
        fs_path_join(candidate, sizeof(candidate), dir, mp);
        snprintf(out, out_size, "%s.kds", candidate);
        if (fs_file_exists(out)) return 1;
    }
    // 2. Relative to cwd
    snprintf(out, out_size, "%s.kds", mp);
    if (fs_file_exists(out)) return 1;
    // 3. Stdlib
    const char* root = fs_stdlib_root();
    if (root) {
        fs_path_join(candidate, sizeof(candidate), root, mp);
        snprintf(out, out_size, "%s.kds", candidate);
        if (fs_file_exists(out)) return 1;
    }
    return 0;
}
