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
} Evaluator;

void eval_init(Evaluator* ev);
void eval_free(Evaluator* ev);
Value eval_program(Evaluator* ev, Program* prog);
Value eval_stmt(Evaluator* ev, Environment* env, Stmt* stmt);

/// Load and execute a .kds module file. If already loaded (tracked via path), returns 0.
/// Populates the evaluator's globals with the module's public definitions.
int eval_load_module(Evaluator* ev, const char* source_path, const char* module_path);
Value eval_expr(Evaluator* ev, Environment* env, Expr* expr);
Value eval_block(Evaluator* ev, Environment* env, StmtList stmts);

#endif // KODAS_EVAL_H
