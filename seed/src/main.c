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
    printf("  %s test                  Run tests (no project specified - runs file)\n", argv0);
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
        if (argc < 3) { fprintf(stderr, "Usage: kodas test <file>\n"); return 1; }
        printf("Running tests in %s:\n", argv[2]);
        return cmd_run(argv[2]);
    }

    // Try to treat as file to run
    if (argv[1][0] != '-') {
        return cmd_run(argv[1]);
    }

    print_usage(argv[0]);
    return 1;
}
