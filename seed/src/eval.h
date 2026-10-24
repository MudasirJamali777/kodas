/// eval.h - Kodas tree-walking interpreter
#ifndef KODAS_EVAL_H
#define KODAS_EVAL_H

#include "common.h"
#include "ast.h"
#include "value.h"
#include "environment.h"

typedef struct {
    bool had_error;
    bool had_return;
    bool had_stop;    // break
    bool had_skip;    // continue
    Value return_value;
    Environment* globals;
    int call_depth;
    char error_msg[1024];
    Stmt* prog_first;
    const char* current_file;  // path of file currently being evaluated
    int tests_passed;
    int tests_failed;
    bool collecting_tests;     // when true, don't print per-test lines (used by `kodas test`)
} Evaluator;

void eval_init(Evaluator* ev);
void eval_free(Evaluator* ev);
Value eval_program(Evaluator* ev, Program* prog);
Value eval_stmt(Evaluator* ev, Environment* env, Stmt* stmt);

/// Load and execute a .kds module file. If already loaded (tracked via path), returns 0.
/// Populates the evaluator's globals with the module's public definitions.
/// If select_names/select_count are non-null/nonzero, only those names are imported
/// (analogous to `import mod { a, b }`).
int eval_load_module(Evaluator* ev, const char* source_path, const char* module_path,
                     char** select_names, int select_count);
Value eval_expr(Evaluator* ev, Environment* env, Expr* expr);
Value eval_block(Evaluator* ev, Environment* env, StmtList stmts);

#endif // KODAS_EVAL_H
