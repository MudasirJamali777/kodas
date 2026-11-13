/// main.c - Kodas CLI entry point
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include "lexer.h"
#include "parser.h"
#include "eval.h"
#include "fs.h"
#include "codegen/codegen_c.h"

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "  ✗ error: cannot open file '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void repl(void) {
    printf("Kodas v%s REPL (type 'exit' to quit)\n", KODAS_VERSION_STRING);
    printf("  ");
    Evaluator ev;
    eval_init(&ev);
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) break;
        if (len == 0) { printf("  "); continue; }

        Lexer lex;
        lexer_init(&lex, "<repl>", line);
        lexer_tokenize(&lex);

        Parser p;
        parser_init(&p, "<repl>", lex.tokens, lex.count);
        Program prog;
        parser_parse(&p, &prog);

        if (p.had_error) {
            printf("%s", p.errors);
        } else {
            Value result = eval_program(&ev, &prog);
            if (ev.had_error) {
                printf("  ✗ %s\n", ev.error_msg);
                ev.had_error = false;
                ev.error_msg[0] = '\0';
            } else if (result.kind != VAL_NULL) {
                value_print(result, true);
            }
            value_free(&result);
        }
        program_free(&prog);
        lexer_free(&lex);
        printf("  ");
        fflush(stdout);
    }
    eval_free(&ev);
    printf("\nGoodbye!\n");
}

static void print_usage(const char* argv0) {
    printf("Kodas v%s - A programming language that reads like poetry\n\n", KODAS_VERSION_STRING);
    printf("Usage:\n");
    printf("  %s run <file.kds>        Run a Kodas program\n", argv0);
    printf("  %s build <file.kds>      Compile (not yet implemented - runs in interpreter)\n", argv0);
    printf("  %s check <file.kds>      Type-check / parse a file\n", argv0);
    printf("  %s repl                  Start interactive REPL\n", argv0);
    printf("  %s tokens <file.kds>     Show tokens (debug)\n", argv0);
    printf("  %s ast <file.kds>        Show AST info (debug)\n", argv0);
    printf("  %s test [path]           Run tests (*_test.kds, test*.kds) in a file or directory (default: tests/)\n", argv0);
    printf("  %s version               Show version\n", argv0);
    printf("  %s help                  Show this help\n", argv0);
}

static int cmd_run(const char* file) {
    char* source = read_file(file);
    if (!source) return 1;

    Lexer lex;
    lexer_init(&lex, file, source);
    lexer_tokenize(&lex);

    Parser p;
    parser_init(&p, file, lex.tokens, lex.count);
    Program prog;
    parser_parse(&p, &prog);

    int exit_code = 0;
    if (p.had_error) {
        fprintf(stderr, "%s", p.errors);
        exit_code = 1;
    } else {
        Evaluator ev;
        eval_init(&ev);
        ev.current_file = file;
        Value result = eval_program(&ev, &prog);
        if (ev.had_error) {
            fprintf(stderr, "  ✗ %s\n", ev.error_msg);
            exit_code = 1;
        }
        value_free(&result);
        eval_free(&ev);
    }

    program_free(&prog);
    lexer_free(&lex);
    free(source);
    return exit_code;
}

static int cmd_tokens(const char* file) {
    char* source = read_file(file);
    if (!source) return 1;
    Lexer lex;
    lexer_init(&lex, file, source);
    lexer_tokenize(&lex);
    for (int i = 0; i < lex.count; i++) {
        token_print(&lex.tokens[i]);
    }
    lexer_free(&lex);
    free(source);
    return 0;
}

static int cmd_check(const char* file) {
    char* source = read_file(file);
    if (!source) return 1;
    Lexer lex;
    lexer_init(&lex, file, source);
    lexer_tokenize(&lex);
    Parser p;
    parser_init(&p, file, lex.tokens, lex.count);
    Program prog;
    parser_parse(&p, &prog);
    if (p.had_error) {
        printf("%s", p.errors);
        printf("  ✗ check failed\n");
    } else {
        printf("  ✓ check passed (%d statements)\n", prog.count);
    }
    int rc = p.had_error ? 1 : 0;
    program_free(&prog);
    lexer_free(&lex);
    free(source);
    return rc;
}

typedef struct {
    char (*files)[2048];
    int count;
    int max;
} TestCollector;

static bool is_test_filename(const char* name) {
    size_t n = strlen(name);
    if (n < 9) return false;
    const char* ext = strrchr(name, '.');
    if (!ext || strcmp(ext, ".kds") != 0) return false;
    size_t stem_len = (size_t)(ext - name);
    // Match *_test.kds: last 5 chars of stem must be "_test"
    if (stem_len >= 5 && strncmp(ext - 5, "_test", 5) == 0) return true;
    // Match test*.kds: stem starts with "test"
    if (stem_len >= 4 && strncmp(name, "test", 4) == 0) return true;
    return false;
}

static int test_collect_cb(const char* dir, const char* name, void* ctx) {
    TestCollector* tc = (TestCollector*)ctx;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    char full[2048];
    snprintf(full, sizeof(full), "%s/%s", dir, name);
    if (fs_is_dir(full)) return 0;
    if (!is_test_filename(name)) return 0;
    if (tc->count >= tc->max) return 0;
    // Insert in sorted position (simple insertion sort)
    int insert_at = tc->count;
    for (int i = 0; i < tc->count; i++) {
        if (strcmp(full, tc->files[i]) < 0) { insert_at = i; break; }
    }
    for (int i = tc->count; i > insert_at; i--) {
        memcpy(tc->files[i], tc->files[i-1], 2048);
    }
    snprintf(tc->files[insert_at], 2048, "%s", full);
    tc->count++;
    return 0;
}

static int cmd_test(const char* path) {
    char file_buf[256][2048];
    int file_count = 0;

    int isdir = fs_is_dir(path);
    if (isdir) {
        TestCollector tc = { .files = file_buf, .count = 0, .max = 256 };
        fs_list_dir(path, test_collect_cb, &tc);
        file_count = tc.count;
    } else {
        snprintf(file_buf[0], 2048, "%s", path);
        file_count = 1;
    }

    if (file_count == 0) {
        printf("No test files found in '%s'.\n", path);
        return 0;
    }

    printf("Running tests from %d file(s):\n\n", file_count);

    int total_passed = 0;
    int total_failed = 0;
    int total_files_ok = 0;
    int total_files_fail = 0;

    for (int i = 0; i < file_count; i++) {
        const char* f = file_buf[i];
        const char* target = f;

        char* source = read_file(target);
        if (!source) {
            printf("%s:\n", f);
            printf("  ✗ could not read file\n\n");
            total_files_fail++;
            total_failed++;
            continue;
        }

        Lexer lex;
        lexer_init(&lex, target, source);
        lexer_tokenize(&lex);

        Parser p;
        parser_init(&p, target, lex.tokens, lex.count);
        Program prog;
        parser_parse(&p, &prog);

        printf("%s:\n", f);

        if (p.had_error) {
            fprintf(stderr, "%s", p.errors);
            printf("  ✗ parse errors\n\n");
            total_files_fail++;
            total_failed++;
            program_free(&prog);
            lexer_free(&lex);
            free(source);
            continue;
        }

        Evaluator ev;
        eval_init(&ev);
        ev.current_file = target;
        ev.collecting_tests = false;
        Value result = eval_program(&ev, &prog);
        int fp = ev.tests_passed;
        int ff = ev.tests_failed;
        if (ev.had_error) {
            printf("  ✗ runtime error: %s\n", ev.error_msg[0] ? ev.error_msg : "unknown");
            ff++;
        }
        value_free(&result);
        eval_free(&ev);

        printf("  %d passed, %d failed\n\n", fp, ff);
        total_passed += fp;
        total_failed += ff;
        if (ff > 0) total_files_fail++; else total_files_ok++;

        program_free(&prog);
        lexer_free(&lex);
        free(source);
    }

    printf("=== Summary: %d passed, %d failed across %d file(s) (%d OK, %d failed) ===\n",
           total_passed, total_failed, file_count, total_files_ok, total_files_fail);
    return total_failed > 0 ? 1 : 0;
}

// ── Import preprocessor ───────────────────────────────────────────
// Replace `import "path"` lines with contents of that file, resolved relative
// to the including file's directory. Deduplicates by absolute realpath.
#define MAX_INCLUDES 64
static char* g_loaded[MAX_INCLUDES]; static int g_loaded_n = 0;
static bool already_loaded(const char* path) {
    char rp[1024];
    // realpath may fail; fallback to strcmp of given path
    if (realpath(path, rp)) {
        for (int i=0;i<g_loaded_n;i++) if (strcmp(g_loaded[i], rp)==0) return true;
        if (g_loaded_n < MAX_INCLUDES) {
            g_loaded[g_loaded_n] = strdup(rp); g_loaded_n++;
        }
        return false;
    }
    for (int i=0;i<g_loaded_n;i++) if (strcmp(g_loaded[i], path)==0) return true;
    if (g_loaded_n < MAX_INCLUDES) { g_loaded[g_loaded_n]=strdup(path); g_loaded_n++; }
    return false;
}
static char* dir_of(const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash) { static char dot[]="."; return dot; }
    static char buf[1024];
    size_t n = (size_t)(slash-path);
    if (n >= sizeof(buf)) n = sizeof(buf)-1;
    memcpy(buf, path, n); buf[n]=0;
    return buf;
}
static char* join_path(const char* dir, const char* rel) {
    static char buf[1024];
    if (rel[0] == '/') snprintf(buf,sizeof(buf),"%s",rel);
    else snprintf(buf,sizeof(buf),"%s/%s",dir,rel);
    return buf;
}
static char* resolve_imports(const char* file) {
    if (already_loaded(file)) return strdup("");
    char* src = read_file(file);
    if (!src) return NULL;
    const char* dir = dir_of(file);
    // Build output
    size_t cap = strlen(src)*2+1, len=0;
    char* out = malloc(cap);
    #define OUT_APPEND(s,n) do{ while(len+(n)+1>cap){cap*=2;out=realloc(out,cap);} memcpy(out+len,(s),(n)); len+=(n); out[len]=0; }while(0)
    const char* p = src;
    while (*p) {
        const char* line_start = p;
        // find end of line
        const char* nl = strchr(p, '\n');
        const char* line_end = nl ? nl : p+strlen(p);
        size_t llen = (size_t)(line_end - p);
        // skip leading whitespace
        const char* q = line_start;
        while (q < line_end && (*q==' '||*q=='\t')) q++;
        if (strncmp(q, "import", 6) == 0 && (q+6 < line_end) && (q[6]==' '||q[6]=='\t')) {
            const char* s = q+6;
            while (*s==' '||*s=='\t') s++;
            if (*s == '"') {
                s++;
                const char* e = s;
                while (e < line_end && *e != '"') e++;
                if (e < line_end) {
                    size_t plen = (size_t)(e-s);
                    char* pbuf = malloc(plen+1);
                    memcpy(pbuf, s, plen); pbuf[plen]=0;
                    char* full = join_path(dir, pbuf);
                    char* child = resolve_imports(full);
                    free(pbuf);
                    if (child) { OUT_APPEND(child, strlen(child)); free(child); }
                    p = nl ? nl+1 : line_end;
                    continue;
                }
            }
        }
        // copy line as-is
        OUT_APPEND(line_start, llen);
        if (nl) { OUT_APPEND("\n",1); p = nl+1; } else p = line_end;
    }
    free(src);
    return out;
}

static int cmd_build(const char* file) {
    // KRT-based backend: full-language compilation.
    g_loaded_n = 0; // reset import tracker
    char* source = resolve_imports(file);
    if (!source) { fprintf(stderr, "Error: failed to read '%s'\n", file); return 1; }

    Lexer lex;
    lexer_init(&lex, file, source);
    lexer_tokenize(&lex);
    Parser p;
    parser_init(&p, file, lex.tokens, lex.count);
    Program prog;
    parser_parse(&p, &prog);

    if (p.had_error) {
        fprintf(stderr, "%s", p.errors);
        program_free(&prog); lexer_free(&lex); free(source);
        return 1;
    }

    char c_path[1024], out_path[1024];
    snprintf(c_path, sizeof(c_path), "%s.c", file);
    size_t fl = strlen(file);
    if (fl > 4 && strcmp(file + fl - 4, ".kds") == 0) {
        snprintf(out_path, sizeof(out_path), "%.*s", (int)(fl - 4), file);
    } else {
        snprintf(out_path, sizeof(out_path), "%s.out", file);
    }
    printf("  [1/3] Compiling %s -> %s (KRT backend)\n", file, c_path);
    int ok = codegen_krt(&prog, c_path, file);
    program_free(&prog); lexer_free(&lex); free(source);
    if (!ok) {
        fprintf(stderr, "  ✗ code generation failed; falling back to interpreter\n");
        return cmd_run(file);
    }

    char cc_path[256] = {0};
    const char* cands[] = {"gcc", "clang", "cc", NULL};
    for (int i = 0; cands[i]; i++) {
        char testcmd[128];
        snprintf(testcmd, sizeof(testcmd), "%s --version >/dev/null 2>&1", cands[i]);
        if (system(testcmd) == 0) { snprintf(cc_path, sizeof(cc_path), "%s", cands[i]); break; }
    }
#if defined(_WIN32)
    if (cc_path[0] == 0) {
        if (system("where cl >nul 2>nul") == 0) snprintf(cc_path, sizeof(cc_path), "cl");
    }
#endif
    if (cc_path[0] == 0) {
        fprintf(stderr, "  ✗ no C compiler found (gcc/clang/cc/cl required) — falling back to interpreter\n");
        return cmd_run(file);
    }

    // Locate krt.c / stdlib_extra.c / krt.h relative to kodas binary.
    char krt_dir[1024], krt_c[1024], extra_c[1024], http_c[1024], krt_hdir[1024], src_dir[1024];
    const char* ed = fs_exe_dir();
    snprintf(krt_dir, sizeof(krt_dir), "%s/../src/codegen", ed && ed[0] ? ed : ".");
    snprintf(krt_c, sizeof(krt_c), "%s/krt.c", krt_dir);
    snprintf(extra_c, sizeof(extra_c), "%s/stdlib_extra.c", krt_dir);
    snprintf(http_c, sizeof(http_c), "%s/stdlib_http.c", krt_dir);
    FILE* fk = fopen(krt_c, "r");
    if (!fk) {
        snprintf(krt_dir, sizeof(krt_dir), "src/codegen");
        snprintf(krt_c, sizeof(krt_c), "%s/krt.c", krt_dir);
        snprintf(extra_c, sizeof(extra_c), "%s/stdlib_extra.c", krt_dir);
        snprintf(http_c, sizeof(http_c), "%s/stdlib_http.c", krt_dir);
        fk = fopen(krt_c, "r");
    }
    if (fk) fclose(fk);
    snprintf(krt_hdir, sizeof(krt_hdir), "%s", krt_dir);
    snprintf(src_dir, sizeof(src_dir), "%s/..", krt_dir);
    FILE* fx = fopen(extra_c, "r"); bool has_extra = fx != NULL; if (fx) fclose(fx);
    FILE* fh = fopen(http_c,  "r"); bool has_http  = fh != NULL; if (fh) fclose(fh);

    printf("  [2/3] Compiling %s -> %s (using %s)\n", c_path, out_path, cc_path);
    char cmd[16384];
    if (strcmp(cc_path, "cl") == 0) {
        snprintf(cmd, sizeof(cmd), "cl /nologo /O2 /DKRT_MAIN /I \"%s\" /I \"%s\" \"%s\" %s%s%s %s%s%s \"%s\" /Fe%s",
                 src_dir, krt_hdir, krt_c,
                 has_extra?"\"":"", has_extra?extra_c:"", has_extra?"\"":"",
                 has_http?"\"":"",  has_http?http_c:"",   has_http?"\"":"",
                 c_path, out_path);
    } else {
        snprintf(cmd, sizeof(cmd), "%s -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -DKRT_MAIN -pthread -I \"%s\" -I \"%s\" \"%s\" %s%s%s %s%s%s \"%s\" -o \"%s\" -lm",
                 cc_path, src_dir, krt_hdir, krt_c,
                 has_extra?"\"":"", has_extra?extra_c:"", has_extra?"\"":"",
                 has_http?"\"":"",  has_http?http_c:"",   has_http?"\"":"",
                 c_path, out_path);
    }
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "  ✗ C compilation failed (rc=%d)\n", rc);
        return rc;
    }
    printf("  [3/3] Running %s\n", out_path);
    printf("  ----------\n");
    fflush(stdout);
    snprintf(cmd, sizeof(cmd), "\"%s\"", out_path);
    return system(cmd);
}

static int cmd_ast(const char* file) {
    char* source = read_file(file);
    if (!source) return 1;
    Lexer lex;
    lexer_init(&lex, file, source);
    lexer_tokenize(&lex);
    Parser p;
    parser_init(&p, file, lex.tokens, lex.count);
    Program prog;
    parser_parse(&p, &prog);
    printf("Program: %d statements\n", prog.count);
    for (Stmt* s = prog.first; s; s = s->next) {
        printf("  - stmt kind %d\n", s->kind);
    }
    program_free(&prog);
    lexer_free(&lex);
    free(source);
    return 0;
}

int main(int argc, char** argv) {
    fs_set_exe_path(argv[0]);
    if (argc < 2) {
        repl();
        return 0;
    }

    const char* cmd = argv[1];

    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: kodas run <file>\n"); return 1; }
        return cmd_run(argv[2]);
    }
    if (strcmp(cmd, "build") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: kodas build <file>\n"); return 1; }
        return cmd_build(argv[2]);
    }
    if (strcmp(cmd, "check") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: kodas check <file>\n"); return 1; }
        return cmd_check(argv[2]);
    }
    if (strcmp(cmd, "tokens") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: kodas tokens <file>\n"); return 1; }
        return cmd_tokens(argv[2]);
    }
    if (strcmp(cmd, "ast") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: kodas ast <file>\n"); return 1; }
        return cmd_ast(argv[2]);
    }
    if (strcmp(cmd, "repl") == 0) { repl(); return 0; }
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("Kodas v%s\n", KODAS_VERSION_STRING);
        return 0;
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    if (strcmp(cmd, "test") == 0) {
        const char* target = (argc >= 3) ? argv[2] : "tests";
        return cmd_test(target);
    }

    // Try to treat as file to run
    if (argv[1][0] != '-') {
        return cmd_run(argv[1]);
    }

    print_usage(argv[0]);
    return 1;
}
