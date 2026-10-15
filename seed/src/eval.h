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
} Evaluator;

void eval_init(Evaluator* ev);
void eval_free(Evaluator* ev);
Value eval_program(Evaluator* ev, Program* prog);
Value eval_stmt(Evaluator* ev, Environment* env, Stmt* stmt);
Value eval_expr(Evaluator* ev, Environment* env, Expr* expr);
Value eval_block(Evaluator* ev, Environment* env, StmtList stmts);

#endif // KODAS_EVAL_H
