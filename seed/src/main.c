/// main.c - Kodas CLI entry point
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
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

static int cmd_build(const char* file) {
    // 1. Parse via C backend -> write .c file
    char c_path[1024], out_path[1024];
    snprintf(c_path, sizeof(c_path), "%s.c", file);
    // Output binary: strip .kds extension
    size_t fl = strlen(file);
    if (fl > 4 && strcmp(file + fl - 4, ".kds") == 0) {
        snprintf(out_path, sizeof(out_path), "%.*s", (int)(fl - 4), file);
    } else {
        snprintf(out_path, sizeof(out_path), "%s.out", file);
    }
    printf("  [1/3] Transpiling %s -> %s\n", file, c_path);
    int rc = codegen_c_emit_file(file, c_path);
    if (rc != 0) {
        fprintf(stderr, "  ✗ code generation failed (program uses features beyond C subset; falling back to interpreter)\n");
        return cmd_run(file);
    }
    // 2. Locate a C compiler (simple PATH probe via --version check)
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
    printf("  [2/3] Compiling %s -> %s (using %s)\n", c_path, out_path, cc_path);
    char cmd[8192];
    if (strcmp(cc_path, "cl") == 0) {
        snprintf(cmd, sizeof(cmd), "cl /nologo /O2 /Fe%s \"%s\"", out_path, c_path);
    } else {
        snprintf(cmd, sizeof(cmd), "%s -O2 -std=c11 \"%s\" -o \"%s\" -lm", cc_path, c_path, out_path);
    }
    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "  ✗ C compilation failed (rc=%d)\n", rc);
        return rc;
    }
    // 3. Run the produced binary
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
