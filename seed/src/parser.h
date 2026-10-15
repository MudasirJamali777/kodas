/// parser.h - Kodas recursive descent parser
#ifndef KODAS_PARSER_H
#define KODAS_PARSER_H

#include "common.h"
#include "lexer.h"
#include "ast.h"

typedef struct {
    const char* file;
    Token* tokens;
    int count;
    int pos;
    bool had_error;
    bool panic_mode;
    // Error reporting
    char errors[4096];
    int error_len;
} Parser;

void parser_init(Parser* p, const char* file, Token* tokens, int count);
void parser_parse(Parser* p, Program* prog);
Expr* parse_expression(Parser* p);
Expr* parse_precedence(Parser* p, int min_prec);

#endif // KODAS_PARSER_H
