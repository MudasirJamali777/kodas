/// parser.c - Kodas recursive descent parser
#include "parser.h"
#include <stdio.h>
#include <stdarg.h>

static void error_at(Parser* p, Token* t, const char* fmt, ...) {
    if (p->panic_mode) return;
    p->panic_mode = true;
    p->had_error = true;

    char buf[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    p->error_len += snprintf(p->errors + p->error_len, sizeof(p->errors) - p->error_len,
        "  ✗ error at %s:%d:%d: %s\n", p->file, t->line, t->col, buf);
}

static Token* peek_tok(Parser* p) {
    return &p->tokens[p->pos];
}

static Token* previous(Parser* p) {
    if (p->pos == 0) return &p->tokens[0];
    return &p->tokens[p->pos - 1];
}

static bool is_at(Parser* p, TokenKind k) {
    return peek_tok(p)->kind == k;
}

static bool check(Parser* p, TokenKind k) {
    return peek_tok(p)->kind == k;
}

static Token* advance_tok(Parser* p) {
    if (peek_tok(p)->kind != TOK_EOF) p->pos++;
    return previous(p);
}

static bool match_tok(Parser* p, TokenKind k) {
    if (!check(p, k)) return false;
    advance_tok(p);
    return true;
}

static Token* consume(Parser* p, TokenKind k, const char* msg) {
    if (peek_tok(p)->kind == k) return advance_tok(p);
    error_at(p, peek_tok(p), "%s (got %s)", msg, token_kind_name(peek_tok(p)->kind));
    return peek_tok(p);
}

static void synchronize(Parser* p) {
    p->panic_mode = false;
    while (peek_tok(p)->kind != TOK_EOF) {
        if (previous(p)->kind == TOK_SEMI || previous(p)->kind == TOK_NEWLINE) return;
        switch (peek_tok(p)->kind) {
            case TOK_FN: case TOK_LET: case TOK_SEAL: case TOK_STRUCT: case TOK_ENUM:
            case TOK_IF: case TOK_LOOP: case TOK_MATCH: case TOK_SHOW: case TOK_TEST:
            case TOK_BEHAVE: case TOK_TRAIT: case TOK_IMPORT: case TOK_MODULE:
            case TOK_BRIDGE: case TOK_RETURN:
                return;
            default: advance_tok(p);
        }
    }
}

static Location current_loc(Parser* p) {
    Token* t = peek_tok(p);
    return loc_make(p->file, t->line, t->col, p->pos);
}

static Location prev_loc(Parser* p) {
    Token* t = previous(p);
    return loc_make(p->file, t->line, t->col, p->pos - 1);
}

// Precedence levels
typedef enum {
    PREC_NONE,
    PREC_ASSIGN,     // =
    PREC_PIPE,       // |> , |?>
    PREC_OR,         // or, ||
    PREC_AND,        // and, &&
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_RANGE,      // .. ..=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * / %
    PREC_UNARY,      // ! -
    PREC_CALL,       // . () []
    PREC_PRIMARY
} Precedence;

static int get_precedence(Token* t) {
    switch (t->kind) {
        case TOK_EQ: return PREC_ASSIGN;
        case TOK_PLUSEQ: case TOK_MINUSEQ: case TOK_STAREQ: case TOK_SLASHEQ:
        case TOK_PERCENTEQ: case TOK_AMPEQ: case TOK_PIPEEQ: case TOK_CARETEQ:
            return PREC_ASSIGN;
        case TOK_PIPE_ARROW: return PREC_PIPE;
        case TOK_OTHERWISE: return PREC_PIPE;
        case TOK_OR: return PREC_OR;
        case TOK_AND: return PREC_AND;
        case TOK_EQEQ: case TOK_NEQ: return PREC_EQUALITY;
        case TOK_LT: case TOK_GT: case TOK_LTE: case TOK_GTE: return PREC_COMPARISON;
        case TOK_DOTDOT: case TOK_DOTDOTEQ: return PREC_RANGE;
        case TOK_PLUS: case TOK_MINUS: return PREC_TERM;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return PREC_FACTOR;
        default:
            if (t->kind == TOK_IDENT && t->length == 1) {
                // Custom ops
                char c = t->start[0];
                if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
                    c == '=' || c == '!' || c == '<' || c == '>' || c == '&' ||
                    c == '|' || c == '^' || c == '~') {
                    if (c == '=') return PREC_ASSIGN;
                    if (c == '<' || c == '>') return PREC_COMPARISON;
                    if (c == '+' || c == '-') return PREC_TERM;
                    if (c == '*' || c == '/' || c == '%') return PREC_FACTOR;
                    return PREC_COMPARISON;
                }
            }
            return PREC_NONE;
    }
}

// Forward declarations
static Expr* parse_expr(Parser* p);
static Stmt* parse_stmt(Parser* p);
static Stmt* parse_declaration(Parser* p);

static TypeNode* parse_type(Parser* p) {
    TypeNode* t = NULL;
    Token* tk = peek_tok(p);

    if (match_tok(p, TOK_MAYBE)) {
        t = type_new(TYPE_MAYBE);
        t->params = kodas_calloc(1, sizeof(TypeNode*));
        t->params[0] = parse_type(p);
        t->param_count = 1;
        return t;
    }

    if (tk->kind == TOK_IDENT) {
        const char* name = kodas_strndup(tk->start, tk->length);
        advance_tok(p);
        if (strcmp(name, "Int") == 0) { free((void*)name); return type_new(TYPE_INT); }
        if (strcmp(name, "Float") == 0) { free((void*)name); return type_new(TYPE_FLOAT); }
        if (strcmp(name, "Bool") == 0) { free((void*)name); return type_new(TYPE_BOOL); }
        if (strcmp(name, "String") == 0) { free((void*)name); return type_new(TYPE_STRING); }
        if (strcmp(name, "Char") == 0) { free((void*)name); return type_new(TYPE_CHAR); }
        if (strcmp(name, "Byte") == 0) { free((void*)name); return type_new(TYPE_BYTE); }
        if (strcmp(name, "any") == 0) { free((void*)name); return type_new(TYPE_ANY); }
        if (strcmp(name, "never") == 0) { free((void*)name); return type_new(TYPE_NEVER); }
        if (strcmp(name, "List") == 0) {
            t = type_new(TYPE_LIST);
            if (match_tok(p, TOK_LBRACKET)) {
                t->params = kodas_calloc(4, sizeof(TypeNode*));
                t->params[t->param_count++] = parse_type(p);
                consume(p, TOK_RBRACKET, "expected ']'");
            }
            free((void*)name);
            return t;
        }
        if (strcmp(name, "Map") == 0) {
            t = type_new(TYPE_MAP);
            if (match_tok(p, TOK_LBRACKET)) {
                t->params = kodas_calloc(4, sizeof(TypeNode*));
                t->params[t->param_count++] = parse_type(p);
                consume(p, TOK_COMMA, "expected ','");
                t->params[t->param_count++] = parse_type(p);
                consume(p, TOK_RBRACKET, "expected ']'");
            }
            free((void*)name);
            return t;
        }
        if (strcmp(name, "Set") == 0) {
            t = type_new(TYPE_SET);
            if (match_tok(p, TOK_LBRACKET)) {
                t->params = kodas_calloc(4, sizeof(TypeNode*));
                t->params[t->param_count++] = parse_type(p);
                consume(p, TOK_RBRACKET, "expected ']'");
            }
            free((void*)name);
            return t;
        }
        t = type_new(TYPE_NAMED);
        t->name = (char*)name;
        if (match_tok(p, TOK_LBRACKET)) {
            t->params = kodas_calloc(4, sizeof(TypeNode*));
            do {
                if (t->param_count > 0) consume(p, TOK_COMMA, "expected ','");
                t->params[t->param_count++] = parse_type(p);
            } while (match_tok(p, TOK_COMMA));
            consume(p, TOK_RBRACKET, "expected ']'");
        }
        return t;
    }
    if (tk->kind == TOK_RESULT) {
        advance_tok(p);
        t = type_new(TYPE_RESULT);
        if (match_tok(p, TOK_LBRACKET)) {
            t->params = kodas_calloc(4, sizeof(TypeNode*));
            t->params[t->param_count++] = parse_type(p);
            consume(p, TOK_COMMA, "expected ','");
            t->params[t->param_count++] = parse_type(p);
            consume(p, TOK_RBRACKET, "expected ']'");
        }
        return t;
    }
    if (match_tok(p, TOK_LPAREN)) {
        t = type_new(TYPE_TUPLE);
        t->params = kodas_calloc(8, sizeof(TypeNode*));
        if (!check(p, TOK_RPAREN)) {
            do {
                t->params[t->param_count++] = parse_type(p);
            } while (match_tok(p, TOK_COMMA));
        }
        consume(p, TOK_RPAREN, "expected ')'");
        return t;
    }
    return type_new(TYPE_INFER);
}

static Expr* parse_primary(Parser* p) {
    Token* tk = peek_tok(p);
    Location loc = current_loc(p);

    switch (tk->kind) {
        case TOK_INT: {
            advance_tok(p);
            return expr_int(tk->num.int_val, loc);
        }
        case TOK_FLOAT: {
            advance_tok(p);
            return expr_float(tk->num.float_val, loc);
        }
        case TOK_TRUE: advance_tok(p); return expr_bool(true, loc);
        case TOK_FALSE: advance_tok(p); return expr_bool(false, loc);
        case TOK_EMPTY: advance_tok(p); return expr_new(EXPR_EMPTY, loc);
        case TOK_SELF: advance_tok(p); return expr_new(EXPR_SELF, loc);

        case TOK_STRING: case TOK_RAW_STRING: {
            advance_tok(p);
            const char* start = tk->start;
            int len = tk->length;
            const char* content_start = start;
            int content_len = len;
            if (tk->kind == TOK_RAW_STRING) {
                content_start = start + 1;
                content_len = len;
            }
            // len is content length (lexer sets start after opening quote)
            char* s = kodas_strndup(content_start, content_len);
            char* out = kodas_malloc(content_len + 1);
            int oi = 0;
            for (int i = 0; i < content_len; i++) {
                if (s[i] == '\\' && i+1 < content_len && tk->kind != TOK_RAW_STRING) {
                    i++;
                    switch (s[i]) {
                        case 'n': out[oi++] = '\n'; break;
                        case 'r': out[oi++] = '\r'; break;
                        case 't': out[oi++] = '\t'; break;
                        case '\\': out[oi++] = '\\'; break;
                        case '"': out[oi++] = '"'; break;
                        case '0': out[oi++] = '\0'; break;
                        default: out[oi++] = s[i];
                    }
                } else {
                    out[oi++] = s[i];
                }
            }
            out[oi] = '\0';
            Expr* e = expr_string(out, loc);
            free(out); free(s);
            return e;
        }

        case TOK_INTERP_STRING_START: {
            Token* start_tok = peek_tok(p);
            advance_tok(p);
            Expr* e = expr_new(EXPR_INTERP_STRING, loc);
            e->as.interp.parts = (Expr**)kodas_calloc(16, sizeof(Expr*));
            // First part is the literal string before {
            char* first_str = kodas_strndup(start_tok->start, start_tok->length);
            e->as.interp.parts[e->as.interp.count++] = expr_string(first_str, loc);
            free(first_str);
            // Parse expression inside { }
            Expr* first = parse_expr(p);
            e->as.interp.parts[e->as.interp.count++] = first;
            // Consume closing } (already handled by brace counting in lexer -> next token is PART or END)
            while (!is_at(p, TOK_EOF)) {
                if (is_at(p, TOK_INTERP_STRING_END)) {
                    Token* end_tok = advance_tok(p);
                    if (end_tok->length > 0) {
                        char* last = kodas_strndup(end_tok->start, end_tok->length);
                        e->as.interp.parts[e->as.interp.count++] = expr_string(last, loc);
                        free(last);
                    }
                    break;
                }
                if (is_at(p, TOK_INTERP_STRING_PART)) {
                    Token* part_tok = advance_tok(p);
                    char* mid = kodas_strndup(part_tok->start, part_tok->length);
                    e->as.interp.parts[e->as.interp.count++] = expr_string(mid, loc);
                    free(mid);
                }
                Expr* next_e = parse_expr(p);
                e->as.interp.parts[e->as.interp.count++] = next_e;
            }
            return e;
        }

        case TOK_CHAR: {
            advance_tok(p);
            Expr* e = expr_new(EXPR_CHAR, loc);
            e->as.char_val = (char)tk->num.int_val;
            return e;
        }

        case TOK_IDENT: case TOK_UNDERSCORE: {
            advance_tok(p);
            char* name = kodas_strndup(tk->start, tk->length);
            // Check for ok(...) / err(...)
            if (strcmp(name, "ok") == 0 && check(p, TOK_LPAREN)) {
                free(name);
                advance_tok(p);
                Expr* val = parse_expr(p);
                consume(p, TOK_RPAREN, "expected ')'");
                Expr* e = expr_new(EXPR_OK, loc);
                e->as.result_e.value = val;
                e->as.result_e.is_err = false;
                return e;
            }
            if (strcmp(name, "err") == 0 && check(p, TOK_LPAREN)) {
                free(name);
                advance_tok(p);
                Expr* val = parse_expr(p);
                consume(p, TOK_RPAREN, "expected ')'");
                Expr* e = expr_new(EXPR_ERR, loc);
                e->as.result_e.value = val;
                e->as.result_e.is_err = true;
                return e;
            }
            // Check for struct init: Name { ... }
            // We use lookahead without advancing by checking the next two tokens
            if (is_at(p, TOK_LBRACE)) {
                bool is_struct_init = false;
                int np = p->pos + 1; // past {
                if (np < p->count && p->tokens[np].kind == TOK_RBRACE) {
                    is_struct_init = true; // empty struct {}
                } else if (np + 1 < p->count && p->tokens[np].kind == TOK_IDENT &&
                           (p->tokens[np+1].kind == TOK_COLON)) {
                    is_struct_init = true;
                }
                if (is_struct_init) {
                    advance_tok(p); // consume {
                    Expr* e = expr_new(EXPR_STRUCT_INIT, loc);
                    e->as.struct_init.name = name;
                    e->as.struct_init.field_names = kodas_calloc(8, sizeof(char*));
                    e->as.struct_init.field_vals = kodas_calloc(8, sizeof(Expr*));
                    while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
                        if (e->as.struct_init.field_count > 0) {
                            if (!match_tok(p, TOK_COMMA)) break;
                        }
                        Token* fname = consume(p, TOK_IDENT, "expected field name");
                        char* fn = kodas_strndup(fname->start, fname->length);
                        consume(p, TOK_COLON, "expected ':'");
                        Expr* fv = parse_expr(p);
                        e->as.struct_init.field_names[e->as.struct_init.field_count] = fn;
                        e->as.struct_init.field_vals[e->as.struct_init.field_count] = fv;
                        e->as.struct_init.field_count++;
                    }
                    consume(p, TOK_RBRACE, "expected '}'");
                    return e;
                }
            }
            Expr* e = expr_ident(name, loc);
            free(name);
            return e;
        }

        case TOK_LPAREN: {
            advance_tok(p);
            // Tuple / grouping
            ExprList items = {0};
            if (!check(p, TOK_RPAREN)) {
                do {
                    expr_list_append(&items, parse_expr(p));
                } while (match_tok(p, TOK_COMMA));
            }
            consume(p, TOK_RPAREN, "expected ')'");
            if (items.count == 1) {
                Expr* e = items.items[0];
                free(items.items);
                return e;
            }
            Expr* t = expr_new(EXPR_TUPLE, loc);
            // Reuse call.args for list storage
            t->as.call.args = items;
            return t;
        }

        case TOK_LBRACKET: {
            advance_tok(p);
            ExprList items = {0};
            // Check for list comprehension: [expr for x in col if cond]
            int save = p->pos;
            if (!is_at(p, TOK_RBRACKET)) {
                Expr* first = parse_expr(p);
                expr_list_append(&items, first);
                if (is_at(p, TOK_FOR)) {
                    // It's a list comprehension - we'll treat as expression
                    // for simplicity, evaluate eagerly
                    advance_tok(p);
                    // Parse the rest but for now just create array-comp node
                    // We'll simplify: consume until ]
                    while (!is_at(p, TOK_RBRACKET) && !is_at(p, TOK_EOF)) advance_tok(p);
                    advance_tok(p); // ]
                    Expr* e = expr_new(EXPR_ARRAY_COMP, loc);
                    // Store first expr
                    e->as.call.callee = first;
                    da_free(&items);
                    return e;
                }
                while (match_tok(p, TOK_COMMA)) {
                    expr_list_append(&items, parse_expr(p));
                }
            }
            consume(p, TOK_RBRACKET, "expected ']'");
            Expr* e = expr_new(EXPR_LIST, loc);
            e->as.call.args = items;
            return e;
        }

        case TOK_LBRACE: {
            // Block expression
            advance_tok(p);
            StmtList stmts = {0};
            while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
                Stmt* s = parse_declaration(p);
                if (s) stmt_list_append(&stmts, s);
                if (p->panic_mode) synchronize(p);
            }
            consume(p, TOK_RBRACE, "expected '}'");
            return expr_block(stmts, loc);
        }

        case TOK_FN: {
            // Lambda: fn(params) => body  or  fn(params) { body }
            advance_tok(p);
            Expr* e = expr_new(EXPR_FN, loc);
            e->as.fn_e.name = NULL;
            consume(p, TOK_LPAREN, "expected '('");
            while (!is_at(p, TOK_RPAREN) && !is_at(p, TOK_EOF)) {
                if (e->as.fn_e.params.count > 0) consume(p, TOK_COMMA, "expected ','");
                Param param = {0};
                if (is_at(p, TOK_MUT)) { advance_tok(p); param.is_mut = true; }
                if (is_at(p, TOK_SELF)) {
                    advance_tok(p);
                    param.is_self = true;
                    param.name = kodas_strdup("self");
                    param_list_append(&e->as.fn_e.params, param);
                    continue;
                }
                Token* pn = consume(p, TOK_IDENT, "expected parameter name");
                param.name = kodas_strndup(pn->start, pn->length);
                if (match_tok(p, TOK_COLON)) {
                    TypeNode* ty = parse_type(p);
                    param.type_ann = type_to_string(ty);
                    type_free(ty);
                }
                if (match_tok(p, TOK_EQ)) {
                    param.default_value = parse_expr(p);
                }
                param_list_append(&e->as.fn_e.params, param);
            }
            consume(p, TOK_RPAREN, "expected ')'");
            if (match_tok(p, TOK_ARROW)) {
                e->as.fn_e.ret_type = parse_type(p);
            }
            if (match_tok(p, TOK_FAT_ARROW)) {
                e->as.fn_e.is_arrow = true;
                e->as.fn_e.body = parse_expr(p);
            } else if (is_at(p, TOK_LBRACE)) {
                e->as.fn_e.body = parse_primary(p); // block
            } else {
                error_at(p, peek_tok(p), "expected '=>' or '{'");
            }
            return e;
        }

        case TOK_IF: {
            advance_tok(p);
            Expr* cond = parse_expr(p);
            Expr* then_b;
            if (is_at(p, TOK_LBRACE)) {
                then_b = parse_primary(p);
            } else if (match_tok(p, TOK_THEN)) {
                then_b = parse_expr(p);
            } else {
                then_b = parse_expr(p);
            }
            Expr* else_b = NULL;
            if (match_tok(p, TOK_ELSE)) {
                if (is_at(p, TOK_IF)) {
                    else_b = parse_primary(p);
                } else if (is_at(p, TOK_LBRACE)) {
                    else_b = parse_primary(p);
                } else {
                    else_b = parse_expr(p);
                }
            }
            return expr_if(cond, then_b, else_b, loc);
        }

        case TOK_MATCH: {
            advance_tok(p);
            Expr* obj = parse_expr(p);
            consume(p, TOK_LBRACE, "expected '{'");
            Expr* e = expr_new(EXPR_MATCH, loc);
            e->as.match_e.obj = obj;
            e->as.match_e.arms = NULL;
            e->as.match_e.arm_count = 0;
            int arm_cap = 8;
            e->as.match_e.arms = kodas_calloc(arm_cap, sizeof(*e->as.match_e.arms));
            while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
                if (e->as.match_e.arm_count >= arm_cap) {
                    arm_cap *= 2;
                    e->as.match_e.arms = kodas_realloc(e->as.match_e.arms, arm_cap * sizeof(*e->as.match_e.arms));
                }
                // Parse pattern (simplified: literal or ident or wildcard or range)
                Pattern* pat = pattern_new(PAT_BINDING, loc);
                if (is_at(p, TOK_UNDERSCORE)) {
                    advance_tok(p);
                    pat->kind = PAT_WILDCARD;
                    pat->name = NULL;
                } else if (is_at(p, TOK_INT) || is_at(p, TOK_FLOAT) || is_at(p, TOK_STRING) ||
                           is_at(p, TOK_TRUE) || is_at(p, TOK_FALSE) || is_at(p, TOK_CHAR)) {
                    Token* lit = peek_tok(p);
                    advance_tok(p);
                    pat->kind = PAT_LITERAL;
                    if (lit->kind == TOK_INT) pat->literal = expr_int(lit->num.int_val, loc);
                    else if (lit->kind == TOK_FLOAT) pat->literal = expr_float(lit->num.float_val, loc);
                    else if (lit->kind == TOK_TRUE) pat->literal = expr_bool(true, loc);
                    else if (lit->kind == TOK_FALSE) pat->literal = expr_bool(false, loc);
                    else if (lit->kind == TOK_CHAR) {
                        pat->literal = expr_new(EXPR_CHAR, loc);
                        pat->literal->as.char_val = (char)lit->num.int_val;
                    } else {
                        const char* s = kodas_strndup(lit->start, lit->length);
                        pat->literal = expr_string(s, loc);
                        free((void*)s);
                    }
                    // Check for range pattern: N..M
                    if (is_at(p, TOK_DOTDOT) || is_at(p, TOK_DOTDOTEQ)) {
                        bool incl = is_at(p, TOK_DOTDOTEQ);
                        advance_tok(p);
                        Pattern* range_pat = pattern_new(PAT_RANGE, loc);
                        range_pat->sub = kodas_calloc(2, sizeof(Pattern*));
                        range_pat->sub_count = 2;
                        range_pat->sub[0] = pat;
                        Token* upper = peek_tok(p);
                        advance_tok(p);
                        Pattern* upper_pat = pattern_new(PAT_LITERAL, loc);
                        if (upper->kind == TOK_INT) upper_pat->literal = expr_int(upper->num.int_val, loc);
                        else upper_pat->literal = expr_float(upper->num.float_val, loc);
                        range_pat->sub[1] = upper_pat;
                        if (incl) { /* mark inclusive via guard or flag */ }
                        pat = range_pat;
                    }
                } else if (is_at(p, TOK_IDENT)) {
                    Token* name_t = advance_tok(p);
                    // Could be enum variant: Variant(...) or binding
                    pat->name = kodas_strndup(name_t->start, name_t->length);
                    if (is_at(p, TOK_LPAREN)) {
                        pat->kind = PAT_ENUM_VARIANT;
                        advance_tok(p);
                        pat->sub = kodas_calloc(8, sizeof(Pattern*));
                        while (!is_at(p, TOK_RPAREN) && !is_at(p, TOK_EOF)) {
                            if (pat->sub_count > 0) consume(p, TOK_COMMA, "expected ','");
                            // Simple binding inside tuple variant
                            Token* n = advance_tok(p);
                            Pattern* subp = pattern_new(PAT_BINDING, loc);
                            subp->name = kodas_strndup(n->start, n->length);
                            pat->sub[pat->sub_count++] = subp;
                        }
                        consume(p, TOK_RPAREN, "expected ')'");
                    }
                    if (is_at(p, TOK_LBRACE)) {
                        pat->kind = PAT_STRUCT;
                        advance_tok(p);
                        pat->field_names = kodas_calloc(8, sizeof(char*));
                        pat->field_pats = kodas_calloc(8, sizeof(Pattern*));
                        while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
                            if (pat->field_count > 0) consume(p, TOK_COMMA, "expected ','");
                            Token* fn = consume(p, TOK_IDENT, "expected field");
                            char* fname = kodas_strndup(fn->start, fn->length);
                            Pattern* fp = NULL;
                            if (match_tok(p, TOK_COLON)) {
                                // Sub-pattern
                                if (is_at(p, TOK_INT)) {
                                    Token* v = advance_tok(p);
                                    fp = pattern_new(PAT_LITERAL, loc);
                                    fp->literal = expr_int(v->num.int_val, loc);
                                } else if (is_at(p, TOK_IDENT)) {
                                    Token* v = advance_tok(p);
                                    fp = pattern_new(PAT_BINDING, loc);
                                    fp->name = kodas_strndup(v->start, v->length);
                                } else {
                                    fp = pattern_new(PAT_WILDCARD, loc);
                                    advance_tok(p);
                                }
                                // Check for range in field pattern
                                if (is_at(p, TOK_DOTDOT) || is_at(p, TOK_DOTDOTEQ)) {
                                    advance_tok(p);
                                    Pattern* rp = pattern_new(PAT_RANGE, loc);
                                    rp->sub = kodas_calloc(2, sizeof(Pattern*));
                                    rp->sub[0] = fp;
                                    Token* hi = advance_tok(p);
                                    Pattern* hip = pattern_new(PAT_LITERAL, loc);
                                    hip->literal = expr_int(hi->num.int_val, loc);
                                    rp->sub[1] = hip;
                                    rp->sub_count = 2;
                                    fp = rp;
                                }
                            } else {
                                fp = pattern_new(PAT_BINDING, loc);
                                fp->name = kodas_strdup(fname);
                            }
                            pat->field_names[pat->field_count] = fname;
                            pat->field_pats[pat->field_count] = fp;
                            pat->field_count++;
                        }
                        consume(p, TOK_RBRACE, "expected '}'");
                    }
                } else {
                    advance_tok(p);
                    pat->kind = PAT_WILDCARD;
                }

                // Check for guard: if cond
                if (is_at(p, TOK_IF)) {
                    advance_tok(p);
                    Pattern* gp = pattern_new(PAT_GUARD, loc);
                    gp->sub = kodas_calloc(1, sizeof(Pattern*));
                    gp->sub[0] = pat;
                    gp->sub_count = 1;
                    gp->guard = parse_expr(p);
                    pat = gp;
                }

                consume(p, TOK_FAT_ARROW, "expected '=>'");
                Expr* body = parse_expr(p);
                e->as.match_e.arms[e->as.match_e.arm_count].pat = pat;
                e->as.match_e.arms[e->as.match_e.arm_count].body = body;
                e->as.match_e.arm_count++;
                if (is_at(p, TOK_COMMA)) advance_tok(p);
            }
            consume(p, TOK_RBRACE, "expected '}'");
            return e;
        }

        case TOK_LOOP: {
            advance_tok(p);
            // Various loop forms
            if (is_at(p, TOK_LBRACE)) {
                Expr* body = parse_primary(p);
                Expr* e = expr_new(EXPR_LOOP, loc);
                e->as.loop_e.count = NULL;
                e->as.loop_e.body = body;
                return e;
            }
            if (is_at(p, TOK_WHILE)) {
                advance_tok(p);
                Expr* cond = parse_expr(p);
                Expr* body = is_at(p, TOK_LBRACE) ? parse_primary(p) : parse_expr(p);
                Expr* e = expr_new(EXPR_WHILE, loc);
                e->as.loop_in.cond = cond;
                e->as.loop_in.body = body;
                e->as.loop_in.var = NULL;
                e->as.loop_in.idx_var = NULL;
                e->as.loop_in.iter = NULL;
                e->as.loop_in.is_while = true;
                return e;
            }
            // loop N { ... }  or  loop x in col { ... }
            Expr* first = parse_expr(p);
            if (is_at(p, TOK_IN)) {
                advance_tok(p);
                // Determine if first is `i, item`
                char* var = NULL;
                char* idx_var = NULL;
                if (first->kind == EXPR_TUPLE && first->as.call.args.count == 2) {
                    idx_var = kodas_strdup(first->as.call.args.items[0]->as.str_val);
                    var = kodas_strdup(first->as.call.args.items[1]->as.str_val);
                } else if (first->kind == EXPR_IDENT) {
                    var = kodas_strdup(first->as.str_val);
                }
                Expr* iter = parse_expr(p);
                Expr* body = is_at(p, TOK_LBRACE) ? parse_primary(p) : parse_expr(p);
                Expr* e = expr_new(EXPR_LOOP_IN, loc);
                e->as.loop_in.var = var;
                e->as.loop_in.idx_var = idx_var;
                e->as.loop_in.iter = iter;
                e->as.loop_in.body = body;
                e->as.loop_in.is_while = false;
                expr_free(first);
                return e;
            }
            // loop N
            Expr* body = is_at(p, TOK_LBRACE) ? parse_primary(p) : parse_expr(p);
            Expr* e = expr_new(EXPR_LOOP, loc);
            e->as.loop_e.count = first;
            e->as.loop_e.body = body;
            return e;
        }

        case TOK_STOP: advance_tok(p); { Expr* e = expr_new(EXPR_STOP, loc); e->as.expr = NULL; return e; }
        case TOK_SKIP: advance_tok(p); { Expr* e = expr_new(EXPR_SKIP, loc); e->as.expr = NULL; return e; }
        case TOK_GIVE: {
            advance_tok(p);
            Expr* val = is_at(p, TOK_SEMI) || is_at(p, TOK_RBRACE) || is_at(p, TOK_COMMA) ? NULL : parse_expr(p);
            Expr* e = expr_new(EXPR_GIVE, loc);
            e->as.expr = val;
            return e;
        }

        case TOK_OK: {
            advance_tok(p);
            Expr* e = expr_new(EXPR_OK, loc);
            if (is_at(p, TOK_LPAREN)) {
                advance_tok(p);
                e->as.result_e.value = parse_expr(p);
                consume(p, TOK_RPAREN, "expected ')'");
            } else {
                e->as.result_e.value = NULL;
            }
            return e;
        }
        case TOK_ERR: {
            advance_tok(p);
            Expr* e = expr_new(EXPR_ERR, loc);
            e->as.result_e.is_err = true;
            if (is_at(p, TOK_LPAREN)) {
                advance_tok(p);
                e->as.result_e.value = parse_expr(p);
                consume(p, TOK_RPAREN, "expected ')'");
            } else {
                e->as.result_e.value = expr_bool(true, loc);
            }
            return e;
        }

        case TOK_NOT: case TOK_MINUS: {
            Token* op = advance_tok(p);
            char* op_str = kodas_strndup(op->start, op->length);
            Expr* operand = parse_precedence(p, PREC_UNARY);
            return expr_unary(op_str, operand, loc);
        }

        default:
            error_at(p, tk, "unexpected token '%s' in expression", token_kind_name(tk->kind));
            advance_tok(p);
            return expr_new(EXPR_EMPTY, loc);
    }
}

static Expr* parse_call_args(Parser* p, Expr* callee, Location loc) {
    advance_tok(p); // (
    ExprList args = {0};
    if (!check(p, TOK_RPAREN)) {
        do {
            expr_list_append(&args, parse_expr(p));
        } while (match_tok(p, TOK_COMMA));
    }
    consume(p, TOK_RPAREN, "expected ')'");
    return expr_call(callee, args, loc);
}

static Expr* parse_postfix(Parser* p, Expr* left) {
    for (;;) {
        Location loc = prev_loc(p);
        if (is_at(p, TOK_LPAREN)) {
            left = parse_call_args(p, left, loc);
        } else if (match_tok(p, TOK_DOT)) {
            // Method call or field access
            Token* name_t = consume(p, TOK_IDENT, "expected property name");
            char* name = kodas_strndup(name_t->start, name_t->length);
            if (is_at(p, TOK_LPAREN)) {
                advance_tok(p);
                ExprList args = {0};
                if (!check(p, TOK_RPAREN)) {
                    do {
                        expr_list_append(&args, parse_expr(p));
                    } while (match_tok(p, TOK_COMMA));
                }
                consume(p, TOK_RPAREN, "expected ')'");
                Expr* e = expr_new(EXPR_METHOD_CALL, loc);
                e->as.method_call.object = left;
                e->as.method_call.method = name;
                e->as.method_call.args = args;
                left = e;
            } else if (match_tok(p, TOK_EQ)) {
                Expr* val = parse_precedence(p, PREC_ASSIGN);
                Expr* e = expr_new(EXPR_FIELD_ASSIGN, loc);
                e->as.field_assign.object = left;
                e->as.field_assign.field = name;
                e->as.field_assign.value = val;
                left = e;
            } else {
                Expr* e = expr_new(EXPR_FIELD, loc);
                e->as.field.object = left;
                e->as.field.field = name;
                left = e;
            }
        } else if (is_at(p, TOK_LBRACKET)) {
            advance_tok(p);
            Expr* idx = parse_expr(p);
            consume(p, TOK_RBRACKET, "expected ']'");
            Expr* e = expr_new(EXPR_INDEX, loc);
            e->as.index.collection = left;
            e->as.index.index = idx;
            if (match_tok(p, TOK_EQ)) {
                Expr* val = parse_precedence(p, PREC_ASSIGN);
                e = expr_new(EXPR_FIELD_ASSIGN, loc);
                e->as.field_assign.object = left;
                e->as.field_assign.field = NULL;
                e->as.field_assign.value = val;
                // Use index for assignment
                e->kind = EXPR_COMPOUND_ASSIGN;
                e->as.compound.target = expr_new(EXPR_INDEX, loc);
                e->as.compound.target->as.index.collection = left;
                e->as.compound.target->as.index.index = idx;
                e->as.compound.op = kodas_strdup("=");
                e->as.compound.value = val;
            }
            left = e;
        } else {
            break;
        }
    }
    return left;
}

Expr* parse_precedence(Parser* p, int min_prec) {
    Location loc = current_loc(p);
    Expr* left = parse_primary(p);
    left = parse_postfix(p, left);

    while (get_precedence(peek_tok(p)) >= min_prec) {
        Token* op_tok = peek_tok(p);
        int prec = get_precedence(op_tok);

        if (op_tok->kind == TOK_PIPE_ARROW || op_tok->kind == TOK_OTHERWISE) {
            bool is_err = (op_tok->kind == TOK_PIPE_ARROW);
            // Need to check if it's |?> (error pipe) or |> (normal pipe)
            // Our lexer maps both |?> and |> to TOK_PIPE_ARROW, disambiguate by content
            is_err = (op_tok->length == 3 && op_tok->start[1] == '?');
            advance_tok(p);
            Expr* right = parse_precedence(p, prec + 1);
            // Wrap into pipe expression
            Expr* pipe_e;
            if (left->kind == EXPR_PIPE && left->as.pipe.is_error == is_err) {
                // Append to existing chain
                left->as.pipe.pipelines = kodas_realloc(left->as.pipe.pipelines,
                    (left->as.pipe.pipe_count + 1) * sizeof(Expr*));
                left->as.pipe.pipelines[left->as.pipe.pipe_count++] = right;
                continue;
            }
            pipe_e = expr_new(is_err ? EXPR_ERR_PIPE : EXPR_PIPE, loc);
            pipe_e->as.pipe.callee = left;
            pipe_e->as.pipe.pipelines = kodas_calloc(4, sizeof(Expr*));
            pipe_e->as.pipe.pipelines[0] = right;
            pipe_e->as.pipe.pipe_count = 1;
            pipe_e->as.pipe.is_error = is_err;
            left = pipe_e;
            continue;
        }

        if (op_tok->kind == TOK_DOTDOT || op_tok->kind == TOK_DOTDOTEQ) {
            advance_tok(p);
            bool inclusive = op_tok->kind == TOK_DOTDOTEQ;
            Expr* right = parse_precedence(p, prec + 1);
            Expr* e = expr_new(inclusive ? EXPR_RANGE_INCL : EXPR_RANGE, loc);
            e->as.binary.left = left;
            e->as.binary.right = right;
            e->as.binary.op = kodas_strdup(inclusive ? "..=" : "..");
            left = e;
            continue;
        }

        if (op_tok->kind == TOK_PLUSEQ || op_tok->kind == TOK_MINUSEQ || op_tok->kind == TOK_STAREQ ||
            op_tok->kind == TOK_SLASHEQ || op_tok->kind == TOK_PERCENTEQ) {
            advance_tok(p);
            const char* op_str = "?";
            switch (op_tok->kind) {
                case TOK_PLUSEQ: op_str = "+"; break;
                case TOK_MINUSEQ: op_str = "-"; break;
                case TOK_STAREQ: op_str = "*"; break;
                case TOK_SLASHEQ: op_str = "/"; break;
                case TOK_PERCENTEQ: op_str = "%"; break;
                default: break;
            }
            Expr* right = parse_precedence(p, prec + 1);
            Expr* e = expr_new(EXPR_COMPOUND_ASSIGN, loc);
            e->as.compound.target = left;
            e->as.compound.op = kodas_strdup(op_str);
            e->as.compound.value = right;
            left = e;
            continue;
        }

        if (op_tok->kind == TOK_EQ) {
            // Assignment - right associative
            advance_tok(p);
            Expr* right = parse_precedence(p, prec);
            if (left->kind == EXPR_IDENT) {
                Expr* e = expr_new(EXPR_ASSIGN, loc);
                e->as.assign.name = kodas_strdup(left->as.str_val);
                e->as.assign.value = right;
                expr_free(left);
                left = e;
            } else if (left->kind == EXPR_FIELD) {
                Expr* e = expr_new(EXPR_FIELD_ASSIGN, loc);
                e->as.field_assign.object = left->as.field.object;
                e->as.field_assign.field = left->as.field.field;
                e->as.field_assign.value = right;
                free(left);
                left = e;
            } else if (left->kind == EXPR_INDEX) {
                Expr* e = expr_new(EXPR_COMPOUND_ASSIGN, loc);
                e->as.compound.target = left;
                e->as.compound.op = kodas_strdup("=");
                e->as.compound.value = right;
                left = e;
            } else {
                error_at(p, op_tok, "invalid assignment target");
            }
            continue;
        }

        if (op_tok->kind == TOK_THEN) {
            advance_tok(p);
            Expr* then_val = parse_precedence(p, prec + 1);
            consume(p, TOK_ELSE, "expected 'else' in ternary");
            Expr* else_val = parse_precedence(p, prec);
            Expr* e = expr_new(EXPR_THEN_ELSE, loc);
            e->as.then_else.cond = left;
            e->as.then_else.then_val = then_val;
            e->as.then_else.else_val = else_val;
            left = e;
            continue;
        }

        // Binary operator
        advance_tok(p);
        char* op_str = kodas_strndup(op_tok->start, op_tok->length);
        Expr* right = parse_precedence(p, prec + 1);
        left = expr_binary(left, op_str, right, loc);
        free(op_str);
    }
    return left;
}

Expr* parse_expression(Parser* p) {
    return parse_precedence(p, PREC_ASSIGN);
}

Expr* parse_expr(Parser* p) {
    return parse_expression(p);
}

static ParamList parse_params(Parser* p) {
    ParamList params = {0};
    consume(p, TOK_LPAREN, "expected '(' after function name");
    while (!is_at(p, TOK_RPAREN) && !is_at(p, TOK_EOF)) {
        if (params.count > 0) consume(p, TOK_COMMA, "expected ','");
        Param param = {0};
        if (match_tok(p, TOK_MUT)) param.is_mut = true;
        if (match_tok(p, TOK_SELF)) {
            param.is_self = true;
            param.name = kodas_strdup("self");
            param_list_append(&params, param);
            continue;
        }
        Token* pn = consume(p, TOK_IDENT, "expected parameter name");
        param.name = kodas_strndup(pn->start, pn->length);
        if (match_tok(p, TOK_COLON)) {
            TypeNode* ty = parse_type(p);
            param.type_ann = type_to_string(ty);
            type_free(ty);
        }
        if (match_tok(p, TOK_EQ)) {
            param.default_value = parse_expr(p);
        }
        param_list_append(&params, param);
    }
    consume(p, TOK_RPAREN, "expected ')'");
    return params;
}

static Stmt* parse_fn(Parser* p, bool is_pub) {
    Token* fn_tok = previous(p);
    Location loc = prev_loc(p);

    // Check for operator overloading: op +(...)
    bool is_op = false;
    char* op_name = NULL;
    if (match_tok(p, TOK_OP)) is_op = true;

    Token* name_tok;
    if (is_op) {
        name_tok = advance_tok(p);
        op_name = kodas_strndup(name_tok->start, name_tok->length);
        name_tok = NULL;
    } else {
        name_tok = consume(p, TOK_IDENT, "expected function name");
    }

    Stmt* s = stmt_new(STMT_FN, loc);
    s->as.fn_s.name = is_op ? op_name : kodas_strndup(name_tok->start, name_tok->length);
    s->as.fn_s.is_pub = is_pub;
    s->as.fn_s.is_op = is_op;
    s->as.fn_s.op_name = op_name;
    s->as.fn_s.params = parse_params(p);

    // Return type
    if (match_tok(p, TOK_ARROW)) {
        s->as.fn_s.ret_type = parse_type(p);
    }

    // Body
    if (match_tok(p, TOK_FAT_ARROW)) {
        s->as.fn_s.is_arrow = true;
        s->as.fn_s.body = parse_expr(p);
    } else if (is_at(p, TOK_LBRACE)) {
        s->as.fn_s.body = parse_primary(p); // block
    } else {
        // Maybe just a signature (trait method)
        s->as.fn_s.body = NULL;
    }
    return s;
}

static Stmt* parse_struct(Parser* p, bool is_pub) {
    Location loc = prev_loc(p);
    Token* name_t = consume(p, TOK_IDENT, "expected struct name");
    Stmt* s = stmt_new(STMT_STRUCT, loc);
    s->as.struct_s.name = kodas_strndup(name_t->start, name_t->length);
    s->as.struct_s.is_pub = is_pub;

    // Generics [T]
    if (is_at(p, TOK_LBRACKET)) {
        advance_tok(p);
        while (!is_at(p, TOK_RBRACKET) && !is_at(p, TOK_EOF)) {
            if (s->as.struct_s.generics.count > 0) consume(p, TOK_COMMA, "expected ','");
            GenericParam gp = {0};
            Token* gn = consume(p, TOK_IDENT, "expected type parameter");
            gp.name = kodas_strndup(gn->start, gn->length);
            da_append(&s->as.struct_s.generics, gp);
        }
        consume(p, TOK_RBRACKET, "expected ']'");
    }

    consume(p, TOK_LBRACE, "expected '{'");
    while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
        StructField f = {0};
        if (match_tok(p, TOK_PUB)) f.is_pub = true;
        if (match_tok(p, TOK_MUT)) { /* mut field */ }
        Token* fn_t = consume(p, TOK_IDENT, "expected field name");
        f.name = kodas_strndup(fn_t->start, fn_t->length);
        consume(p, TOK_COLON, "expected ':'");
        f.type = parse_type(p);
        if (match_tok(p, TOK_EQ)) {
            f.default_val = parse_expr(p);
        }
        da_append(&s->as.struct_s.fields, f);
        if (!match_tok(p, TOK_COMMA)) break;
    }
    consume(p, TOK_RBRACE, "expected '}'");
    return s;
}

static Stmt* parse_enum(Parser* p, bool is_pub) {
    Location loc = prev_loc(p);
    Token* name_t = consume(p, TOK_IDENT, "expected enum name");
    Stmt* s = stmt_new(STMT_ENUM, loc);
    s->as.enum_s.name = kodas_strndup(name_t->start, name_t->length);
    s->as.enum_s.is_pub = is_pub;
    consume(p, TOK_LBRACE, "expected '{'");
    while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
        EnumVariant v = {0};
        Token* vn = consume(p, TOK_IDENT, "expected variant name");
        v.name = kodas_strndup(vn->start, vn->length);
        if (is_at(p, TOK_LPAREN)) {
            advance_tok(p);
            while (!is_at(p, TOK_RPAREN) && !is_at(p, TOK_EOF)) {
                if (v.fields.count > 0) consume(p, TOK_COMMA, "expected ','");
                Param f = {0};
                if (is_at(p, TOK_IDENT) && p->tokens[p->pos+1].kind == TOK_COLON) {
                    Token* fn = advance_tok(p);
                    f.name = kodas_strndup(fn->start, fn->length);
                    advance_tok(p);
                    TypeNode* ty = parse_type(p);
                    f.type_ann = type_to_string(ty);
                    type_free(ty);
                } else {
                    TypeNode* ty = parse_type(p);
                    f.type_ann = type_to_string(ty);
                    type_free(ty);
                }
                da_append(&v.fields, f);
            }
            consume(p, TOK_RPAREN, "expected ')'");
        }
        da_append(&s->as.enum_s.variants, v);
        if (!match_tok(p, TOK_COMMA)) break;
    }
    consume(p, TOK_RBRACE, "expected '}'");
    return s;
}

static Stmt* parse_behave(Parser* p, bool is_pub) {
    Location loc = prev_loc(p);
    Token* type_t = consume(p, TOK_IDENT, "expected type name");
    Stmt* s = stmt_new(STMT_BEHAVE, loc);
    s->as.behave_s.type_name = kodas_strndup(type_t->start, type_t->length);
    s->as.behave_s.is_pub = is_pub;
    s->as.behave_s.traits = NULL;
    s->as.behave_s.trait_count = 0;

    if (match_tok(p, TOK_IS)) {
        s->as.behave_s.traits = kodas_calloc(8, sizeof(char*));
        do {
            Token* tn = consume(p, TOK_IDENT, "expected trait name");
            s->as.behave_s.traits[s->as.behave_s.trait_count++] =
                kodas_strndup(tn->start, tn->length);
        } while (match_tok(p, TOK_COMMA));
    }

    consume(p, TOK_LBRACE, "expected '{'");
    while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
        if (is_at(p, TOK_FN) || (is_at(p, TOK_PUB) && p->tokens[p->pos+1].kind == TOK_FN)) {
            bool pub_m = match_tok(p, TOK_PUB);
            if (match_tok(p, TOK_FN)) {
                // Simple approach: parse function inline without moving
                Location floc = prev_loc(p);
                FnDecl d = {0};
                Token* fn_name = consume(p, TOK_IDENT, "expected method name");
                d.name = kodas_strndup(fn_name->start, fn_name->length);
                d.params = parse_params(p);
                if (match_tok(p, TOK_ARROW)) d.ret_type = parse_type(p);
                if (match_tok(p, TOK_FAT_ARROW)) { d.is_arrow = true; d.body = parse_expr(p); }
                else if (is_at(p, TOK_LBRACE)) { d.body = parse_primary(p); }
                else { d.body = NULL; }
                d.is_pub = pub_m;
                da_append(&s->as.behave_s.methods, d);
            }
        } else {
            advance_tok(p);
        }
    }
    consume(p, TOK_RBRACE, "expected '}'");
    return s;
}

static Stmt* parse_trait(Parser* p, bool is_pub) {
    Location loc = prev_loc(p);
    Token* name_t = consume(p, TOK_IDENT, "expected trait name");
    Stmt* s = stmt_new(STMT_TRAIT, loc);
    s->as.trait_s.name = kodas_strndup(name_t->start, name_t->length);
    s->as.trait_s.is_pub = is_pub;
    s->as.trait_s.methods = NULL;
    s->as.trait_s.method_count = 0;
    int cap = 8;
    s->as.trait_s.methods = kodas_calloc(cap, sizeof(TraitMethod));

    if (is_at(p, TOK_LBRACKET)) {
        // generics
        advance_tok(p);
        while (!is_at(p, TOK_RBRACKET)) {
            if (s->as.trait_s.generics.count > 0) advance_tok(p);
            Token* gn = consume(p, TOK_IDENT, "expected type param");
            GenericParam gp = {0};
            gp.name = kodas_strndup(gn->start, gn->length);
            da_append(&s->as.trait_s.generics, gp);
        }
        consume(p, TOK_RBRACKET, "expected ']'");
    }

    consume(p, TOK_LBRACE, "expected '{'");
    while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
        if (s->as.trait_s.method_count >= cap) {
            cap *= 2;
            s->as.trait_s.methods = kodas_realloc(s->as.trait_s.methods, cap * sizeof(TraitMethod));
        }
        TraitMethod* m = &s->as.trait_s.methods[s->as.trait_s.method_count];
        memset(m, 0, sizeof(TraitMethod));
        if (match_tok(p, TOK_FN)) {
            Token* mn = consume(p, TOK_IDENT, "expected method name");
            m->name = kodas_strndup(mn->start, mn->length);
            m->params = parse_params(p);
            if (match_tok(p, TOK_ARROW)) m->ret_type = parse_type(p);
            if (match_tok(p, TOK_FAT_ARROW)) {
                m->default_body = parse_expr(p);
            } else if (is_at(p, TOK_LBRACE)) {
                m->default_body = parse_primary(p);
            }
            s->as.trait_s.method_count++;
        } else {
            advance_tok(p);
        }
    }
    consume(p, TOK_RBRACE, "expected '}'");
    return s;
}

static Stmt* parse_import(Parser* p, bool is_pub) {
    Location loc = prev_loc(p);
    Token* mod = consume(p, TOK_IDENT, "expected module name");
    Stmt* s = stmt_new(STMT_IMPORT, loc);
    s->as.import_s.name = kodas_strndup(mod->start, mod->length);
    s->as.import_s.is_pub = is_pub;
    while (match_tok(p, TOK_DOT)) {
        Token* part = consume(p, TOK_IDENT, "expected path part");
        char* new_name = kodas_malloc(strlen(s->as.import_s.name) + 1 + part->length + 1);
        sprintf(new_name, "%s.%s", s->as.import_s.name, kodas_strndup(part->start, part->length));
        free(s->as.import_s.name);
        s->as.import_s.name = new_name;
    }
    if (is_at(p, TOK_LBRACE)) {
        advance_tok(p);
        while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
            if (s->as.import_s.imports.count > 0) consume(p, TOK_COMMA, "expected ','");
            Token* n = consume(p, TOK_IDENT, "expected import name");
            name_list_append(&s->as.import_s.imports, kodas_strndup(n->start, n->length));
        }
        consume(p, TOK_RBRACE, "expected '}'");
    }
    if (match_tok(p, TOK_AS)) {
        Token* alias = consume(p, TOK_IDENT, "expected alias");
        // store alias info via a special name prefix? For now just keep module name
    }
    return s;
}

static Stmt* parse_module(Parser* p, bool is_pub) {
    Location loc = prev_loc(p);
    Token* name = consume(p, TOK_IDENT, "expected module name");
    Stmt* s = stmt_new(STMT_MODULE, loc);
    s->as.module_s.name = kodas_strndup(name->start, name->length);
    s->as.module_s.is_pub = is_pub;
    return s;
}

static Stmt* parse_test(Parser* p, bool is_bench) {
    Location loc = prev_loc(p);
    Token* name_t;
    if (is_at(p, TOK_STRING)) {
        name_t = advance_tok(p);
    } else {
        name_t = consume(p, TOK_IDENT, "expected test name");
    }
    Stmt* s = stmt_new(is_bench ? STMT_BENCH : STMT_TEST, loc);
    s->as.test_s.name = kodas_strndup(name_t->start, name_t->length);
    s->as.test_s.is_bench = is_bench;
    if (is_at(p, TOK_LBRACE)) {
        s->as.test_s.body = parse_primary(p);
    } else {
        s->as.test_s.body = parse_expr(p);
    }
    return s;
}

static Stmt* parse_show(Parser* p) {
    Location loc = prev_loc(p);
    Expr* val = parse_expr(p);
    Stmt* s = stmt_new(STMT_EXPR, loc);
    Expr* e = expr_new(EXPR_SHOW, loc);
    e->as.expr = val;
    s->as.expr = e;
    return s;
}

static Stmt* parse_assert(Parser* p) {
    Location loc = prev_loc(p);
    Expr* val = parse_expr(p);
    Stmt* s = stmt_new(STMT_EXPR, loc);
    Expr* e = expr_new(EXPR_ASSERT, loc);
    e->as.expr = val;
    s->as.expr = e;
    return s;
}

static Stmt* parse_stmt(Parser* p) {
    Token* tk = peek_tok(p);
    Location loc = current_loc(p);

    switch (tk->kind) {
        case TOK_SHOW: advance_tok(p); return parse_show(p);
        case TOK_ASSERT: advance_tok(p); return parse_assert(p);
        case TOK_STOP: {
            advance_tok(p);
            Stmt* s = stmt_new(STMT_EXPR, loc);
            s->as.expr = expr_new(EXPR_STOP, loc);
            return s;
        }
        case TOK_SKIP: {
            advance_tok(p);
            Stmt* s = stmt_new(STMT_EXPR, loc);
            s->as.expr = expr_new(EXPR_SKIP, loc);
            return s;
        }
        case TOK_GIVE: {
            advance_tok(p);
            Stmt* s = stmt_new(STMT_RETURN, loc);
            s->as.expr = (is_at(p, TOK_SEMI) || is_at(p, TOK_RBRACE) || is_at(p, TOK_COMMA))
                ? NULL : parse_expr(p);
            return s;
        }
        case TOK_RETURN: {
            advance_tok(p);
            Stmt* s = stmt_new(STMT_RETURN, loc);
            s->as.expr = (is_at(p, TOK_SEMI) || is_at(p, TOK_RBRACE)) ? NULL : parse_expr(p);
            return s;
        }
        default: {
            Expr* e = parse_expression(p);
            Stmt* s = stmt_new(STMT_EXPR, loc);
            s->as.expr = e;
            return s;
        }
    }
}

static Stmt* parse_declaration(Parser* p) {
    bool is_pub = match_tok(p, TOK_PUB);

    // Decorators
    Stmt* decorator = NULL;
    while (is_at(p, TOK_AT)) {
        Location loc = current_loc(p);
        advance_tok(p);
        Token* dn = consume(p, TOK_IDENT, "expected decorator name");
        Stmt* dec = stmt_new(STMT_DECORATOR, loc);
        dec->as.decorator_s.name = kodas_strndup(dn->start, dn->length);
        if (match_tok(p, TOK_LPAREN)) {
            while (!is_at(p, TOK_RPAREN) && !is_at(p, TOK_EOF)) {
                if (dec->as.decorator_s.args.count > 0) consume(p, TOK_COMMA, "expected ','");
                expr_list_append(&dec->as.decorator_s.args, parse_expr(p));
            }
            consume(p, TOK_RPAREN, "expected ')'");
        }
        dec->as.decorator_s.stmt = decorator;
        decorator = dec;
        is_pub = is_pub || match_tok(p, TOK_PUB);
    }

    Stmt* s = NULL;
    Token* tk = peek_tok(p);
    Location loc = current_loc(p);

    switch (tk->kind) {
        case TOK_FN:
            advance_tok(p);
            s = parse_fn(p, is_pub);
            break;
        case TOK_STRUCT:
            advance_tok(p);
            s = parse_struct(p, is_pub);
            break;
        case TOK_ENUM:
            advance_tok(p);
            s = parse_enum(p, is_pub);
            break;
        case TOK_BEHAVE:
            advance_tok(p);
            s = parse_behave(p, is_pub);
            break;
        case TOK_TRAIT:
            advance_tok(p);
            s = parse_trait(p, is_pub);
            break;
        case TOK_LET: {
            advance_tok(p);
            bool is_mut = match_tok(p, TOK_MUT);
            Token* name_t = consume(p, TOK_IDENT, "expected variable name");
            s = stmt_new(STMT_LET, loc);
            s->as.let_s.name = kodas_strndup(name_t->start, name_t->length);
            s->as.let_s.is_mut = is_mut;
            s->as.let_s.is_pub = is_pub;
            s->as.let_s.type = NULL;
            if (match_tok(p, TOK_COLON)) {
                s->as.let_s.type = parse_type(p);
            }
            if (match_tok(p, TOK_EQ)) {
                s->as.let_s.value = parse_expr(p);
            } else {
                s->as.let_s.value = NULL;
            }
            // Destructuring: let (a, b) = ...
            break;
        }
        case TOK_SEAL: {
            advance_tok(p);
            Token* name_t = consume(p, TOK_IDENT, "expected constant name");
            s = stmt_new(STMT_SEAL, loc);
            s->as.seal_s.name = kodas_strndup(name_t->start, name_t->length);
            s->as.seal_s.is_pub = is_pub;
            if (match_tok(p, TOK_EQ)) {
                // parse expr, but store as STMT_LET-ish
                s->kind = STMT_LET;
                s->as.let_s.name = kodas_strdup(s->as.seal_s.name);
                s->as.let_s.is_mut = false;
                s->as.let_s.value = parse_expr(p);
                s->as.let_s.type = NULL;
                s->as.let_s.is_pub = is_pub;
            }
            break;
        }
        case TOK_IMPORT:
            advance_tok(p);
            s = parse_import(p, is_pub);
            break;
        case TOK_MODULE:
            advance_tok(p);
            s = parse_module(p, is_pub);
            break;
        case TOK_TEST:
            advance_tok(p);
            s = parse_test(p, false);
            break;
        case TOK_BENCH:
            advance_tok(p);
            s = parse_test(p, true);
            break;
        case TOK_CONTRACT: {
            advance_tok(p);
            s = stmt_new(STMT_CONTRACT, loc);
            consume(p, TOK_LBRACE, "expected '{'");
            while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
                expr_list_append(&s->as.contract_s.decls, parse_expr(p));
            }
            consume(p, TOK_RBRACE, "expected '}'");
            break;
        }
        case TOK_BRIDGE: {
            advance_tok(p);
            s = stmt_new(STMT_BRIDGE, loc);
            Token* lang_t = consume(p, TOK_IDENT, "expected language name");
            s->as.bridge_s.lang = kodas_strndup(lang_t->start, lang_t->length);
            consume(p, TOK_LBRACE, "expected '{'");
            while (!is_at(p, TOK_RBRACE) && !is_at(p, TOK_EOF)) {
                stmt_list_append(&s->as.bridge_s.stmts, parse_declaration(p));
            }
            consume(p, TOK_RBRACE, "expected '}'");
            break;
        }
        default:
            s = parse_stmt(p);
            if (is_pub && s) {
                // wrap in pub
                Stmt* ps = stmt_new(STMT_PUB, loc);
                ps->as.expr = NULL;
                ps->next = s;
                s = ps;
            }
            break;
    }

    if (decorator) {
        decorator->as.decorator_s.stmt = s;
        s = decorator;
    }
    return s;
}

void parser_init(Parser* p, const char* file, Token* tokens, int count) {
    memset(p, 0, sizeof(Parser));
    p->file = file;
    p->tokens = tokens;
    p->count = count;
    p->pos = 0;
    p->had_error = false;
    p->panic_mode = false;
    p->errors[0] = '\0';
    p->error_len = 0;
}

void parser_parse(Parser* p, Program* prog) {
    program_init(prog);
    while (!is_at(p, TOK_EOF)) {
        Stmt* s = parse_declaration(p);
        if (s) {
            // Skip auto-generated semicolons
            if (s->kind == STMT_EXPR && s->as.expr && s->as.expr->kind == EXPR_EMPTY) {
                stmt_free(s);
            } else {
                program_add(prog, s);
            }
        }
        if (p->panic_mode) synchronize(p);
        // Skip semicolons/newlines between statements
        while (is_at(p, TOK_SEMI)) advance_tok(p);
    }
}
