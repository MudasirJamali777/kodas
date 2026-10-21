/// lexer.c - Kodas lexer implementation
#include "lexer.h"
#include <string.h>
#include <ctype.h>

static const char* keywords[] = {
    "let", "seal", "show", "fn", "if", "else", "match",
    "loop", "while", "in", "stop", "skip", "give", "return",
    "struct", "enum", "behave", "trait", "impl", "is", "pub",
    "as", "module", "import", "use", "mut", "true", "false",
    "maybe", "empty", "ok", "err", "Result", "never", "any",
    "flow", "together", "timeout", "Channel", "bridge", "link",
    "test", "bench", "assert", "measure", "contract",
    "then", "otherwise", "demand", "for",
    "op", "self", "new", "type", "where",
    "Bool", "Int", "Float", "String", "Char", "Byte",
    NULL
};

static const TokenKind kw_tokens[] = {
    TOK_LET, TOK_SEAL, TOK_SHOW, TOK_FN, TOK_IF, TOK_ELSE, TOK_MATCH,
    TOK_LOOP, TOK_WHILE, TOK_IN, TOK_STOP, TOK_SKIP, TOK_GIVE, TOK_RETURN,
    TOK_STRUCT, TOK_ENUM, TOK_BEHAVE, TOK_TRAIT, TOK_IMPL, TOK_IS, TOK_PUB,
    TOK_AS, TOK_MODULE, TOK_IMPORT, TOK_USE, TOK_MUT, TOK_TRUE, TOK_FALSE,
    TOK_MAYBE, TOK_EMPTY, TOK_OK, TOK_ERR, TOK_RESULT, TOK_NEVER, TOK_ANY,
    TOK_FLOW, TOK_TOGETHER, TOK_TIMEOUT, TOK_CHANNEL, TOK_BRIDGE, TOK_LINK,
    TOK_TEST, TOK_BENCH, TOK_ASSERT, TOK_MEASURE, TOK_CONTRACT,
    TOK_THEN, TOK_OTHERWISE, TOK_DEMAND, TOK_FOR,
    TOK_OP, TOK_SELF, TOK_NEW_KW, TOK_TYPE_KW, TOK_WHERE,
    TOK_IDENT, TOK_IDENT, TOK_IDENT, TOK_IDENT, TOK_IDENT, TOK_IDENT,
};

void lexer_init(Lexer* lex, const char* file, const char* source) {
    memset(lex, 0, sizeof(Lexer));
    lex->file = file;
    lex->source = source;
    lex->start = source;
    lex->current = source;
    lex->line = 1;
    lex->col = 1;
}

static inline bool is_at_end(Lexer* lex) {
    return *lex->current == '\0';
}

static inline char peek(Lexer* lex) {
    return *lex->current;
}

static inline char peek_next(Lexer* lex) {
    if (is_at_end(lex)) return '\0';
    return lex->current[1];
}

static inline char advance(Lexer* lex) {
    char c = *lex->current++;
    if (c == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return c;
}

static inline bool match(Lexer* lex, char expected) {
    if (is_at_end(lex)) return false;
    if (*lex->current != expected) return false;
    lex->current++;
    if (*lex->current == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return true;
}

static Token make_token(Lexer* lex, TokenKind kind) {
    Token t;
    t.kind = kind;
    t.start = lex->start;
    t.length = (int)(lex->current - lex->start);
    t.line = lex->start_line;
    t.col = lex->start_col;
    t.num.int_val = 0;
    t.num.float_val = 0;
    t.num.bool_val = false;
    return t;
}

static Token error_token(Lexer* lex, const char* msg) {
    Token t;
    t.kind = TOK_ERROR;
    t.start = msg;
    t.length = (int)strlen(msg);
    t.line = lex->line;
    t.col = lex->col;
    return t;
}

static void skip_whitespace(Lexer* lex) {
    while (!is_at_end(lex)) {
        char c = peek(lex);
        switch (c) {
            case ' ': case '\r': case '\t': advance(lex); break;
            case '\n': advance(lex); break;
            default: return;
        }
    }
}

static void skip_comment(Lexer* lex) {
    if (peek(lex) == '-' && peek_next(lex) == '-') {
        if (lex->current[2] == '-' && lex->current[3] == '-') {
            // Multi-line comment --- ... ---
            advance(lex); advance(lex); advance(lex); advance(lex);
            int depth = 1;
            while (!is_at_end(lex) && depth > 0) {
                if (peek(lex) == '-' && peek_next(lex) == '-' &&
                    lex->current[2] == '-' && lex->current[3] == '-') {
                    advance(lex); advance(lex); advance(lex); advance(lex);
                    depth--;
                    if (depth <= 0) break;
                } else if (peek(lex) == '-' && peek_next(lex) == '-' &&
                           lex->current[2] == '-' && lex->current[3] == '-') {
                    advance(lex); advance(lex); advance(lex); advance(lex);
                    depth++;
                } else {
                    advance(lex);
                }
            }
        } else {
            // Single line -- comment
            while (!is_at_end(lex) && peek(lex) != '\n') advance(lex);
        }
    }
}

static TokenKind check_keyword(Lexer* lex, int offset, const char* rest, TokenKind kind) {
    int len = (int)strlen(rest);
    if (lex->current - lex->start == offset + len &&
        memcmp(lex->start + offset, rest, len) == 0) {
        return kind;
    }
    return TOK_IDENT;
}

static TokenKind match_kw(Lexer* lex, const char* kw, TokenKind kind) {
    int len = (int)strlen(kw);
    if (lex->current - lex->start == len && memcmp(lex->start, kw, len) == 0) return kind;
    return TOK_IDENT;
}

static TokenKind ident_type(Lexer* lex) {
    int len = (int)(lex->current - lex->start);
    char buf[64];
    if (len >= 64) len = 63;
    memcpy(buf, lex->start, len);
    buf[len] = '\0';

    // Use simple chain of comparisons
    switch (buf[0]) {
        case 'a':
            if (strcmp(buf, "as") == 0) return TOK_AS;
            if (strcmp(buf, "any") == 0) return TOK_ANY;
            if (strcmp(buf, "assert") == 0) return TOK_ASSERT;
            break;
        case 'b':
            if (strcmp(buf, "behave") == 0) return TOK_BEHAVE;
            if (strcmp(buf, "bridge") == 0) return TOK_BRIDGE;
            break;
        case 'c':
            if (strcmp(buf, "Channel") == 0) return TOK_CHANNEL;
            if (strcmp(buf, "contract") == 0) return TOK_CONTRACT;
            break;
        case 'd':
            if (strcmp(buf, "demand") == 0) return TOK_DEMAND;
            break;
        case 'e':
            if (strcmp(buf, "else") == 0) return TOK_ELSE;
            if (strcmp(buf, "empty") == 0) return TOK_EMPTY;
            if (strcmp(buf, "err") == 0) return TOK_ERR;
            if (strcmp(buf, "enum") == 0) return TOK_ENUM;
            break;
        case 'f':
            if (strcmp(buf, "fn") == 0) return TOK_FN;
            if (strcmp(buf, "false") == 0) return TOK_FALSE;
            if (strcmp(buf, "flow") == 0) return TOK_FLOW;
            if (strcmp(buf, "for") == 0) return TOK_FOR;
            break;
        case 'g':
            if (strcmp(buf, "give") == 0) return TOK_GIVE;
            break;
        case 'i':
            if (strcmp(buf, "if") == 0) return TOK_IF;
            if (strcmp(buf, "in") == 0) return TOK_IN;
            if (strcmp(buf, "import") == 0) return TOK_IMPORT;
            if (strcmp(buf, "impl") == 0) return TOK_IMPL;
            if (strcmp(buf, "is") == 0) return TOK_IS;
            break;
        case 'l':
            if (strcmp(buf, "let") == 0) return TOK_LET;
            if (strcmp(buf, "link") == 0) return TOK_LINK;
            if (strcmp(buf, "loop") == 0) return TOK_LOOP;
            break;
        case 'm':
            if (strcmp(buf, "match") == 0) return TOK_MATCH;
            if (strcmp(buf, "maybe") == 0) return TOK_MAYBE;
            if (strcmp(buf, "measure") == 0) return TOK_MEASURE;
            if (strcmp(buf, "module") == 0) return TOK_MODULE;
            if (strcmp(buf, "mut") == 0) return TOK_MUT;
            break;
        case 'n':
            if (strcmp(buf, "never") == 0) return TOK_NEVER;
            if (strcmp(buf, "new") == 0) return TOK_NEW_KW;
            break;
        case 'o':
            if (strcmp(buf, "ok") == 0) return TOK_OK;
            if (strcmp(buf, "otherwise") == 0) return TOK_OTHERWISE;
            if (strcmp(buf, "op") == 0) return TOK_OP;
            break;
        case 'p':
            if (strcmp(buf, "pub") == 0) return TOK_PUB;
            break;
        case 'r':
            if (strcmp(buf, "return") == 0) return TOK_RETURN;
            if (strcmp(buf, "Result") == 0) return TOK_RESULT;
            break;
        case 's':
            if (strcmp(buf, "seal") == 0) return TOK_SEAL;
            if (strcmp(buf, "self") == 0) return TOK_SELF;
            if (strcmp(buf, "show") == 0) return TOK_SHOW;
            if (strcmp(buf, "skip") == 0) return TOK_SKIP;
            if (strcmp(buf, "stop") == 0) return TOK_STOP;
            if (strcmp(buf, "struct") == 0) return TOK_STRUCT;
            break;
        case 't':
            if (strcmp(buf, "test") == 0) return TOK_TEST;
            if (strcmp(buf, "then") == 0) return TOK_THEN;
            if (strcmp(buf, "timeout") == 0) return TOK_TIMEOUT;
            if (strcmp(buf, "together") == 0) return TOK_TOGETHER;
            if (strcmp(buf, "trait") == 0) return TOK_TRAIT;
            if (strcmp(buf, "type") == 0) return TOK_TYPE_KW;
            if (strcmp(buf, "true") == 0) return TOK_TRUE;
            break;
        case 'u':
            if (strcmp(buf, "use") == 0) return TOK_USE;
            break;
        case 'w':
            if (strcmp(buf, "while") == 0) return TOK_WHILE;
            if (strcmp(buf, "where") == 0) return TOK_WHERE;
            break;
        case '_': return TOK_UNDERSCORE;
    }
    return TOK_IDENT;
}

static Token identifier(Lexer* lex) {
    while (isalnum((unsigned char)peek(lex)) || peek(lex) == '_') advance(lex);
    return make_token(lex, ident_type(lex));
}

static Token number(Lexer* lex) {
    bool is_float = false;
    while (isdigit((unsigned char)peek(lex))) advance(lex);

    if (peek(lex) == '.' && isdigit((unsigned char)peek_next(lex))) {
        is_float = true;
        advance(lex);
        while (isdigit((unsigned char)peek(lex))) advance(lex);
    }

    if (peek(lex) == 'e' || peek(lex) == 'E') {
        is_float = true;
        advance(lex);
        if (peek(lex) == '+' || peek(lex) == '-') advance(lex);
        while (isdigit((unsigned char)peek(lex))) advance(lex);
    }

    Token t = make_token(lex, is_float ? TOK_FLOAT : TOK_INT);
    char buf[256];
    int len = t.length < 255 ? t.length : 255;
    memcpy(buf, t.start, len);
    buf[len] = '\0';
    if (is_float) {
        t.num.float_val = strtod(buf, NULL);
    } else {
        t.num.int_val = strtoll(buf, NULL, 10);
    }
    return t;
}

static char escape_char(Lexer* lex) {
    advance(lex); // skip backslash
    char c = advance(lex);
    switch (c) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        case '0': return '\0';
        default: return c;
    }
}

static Token char_literal(Lexer* lex) {
    advance(lex); // opening '
    char c;
    if (peek(lex) == '\\') {
        c = escape_char(lex);
    } else {
        c = advance(lex);
    }
    if (peek(lex) != '\'') {
        return error_token(lex, "unterminated char literal");
    }
    advance(lex); // closing '
    Token t = make_token(lex, TOK_CHAR);
    t.num.int_val = c;
    return t;
}

static Token raw_string(Lexer* lex) {
    advance(lex); // 'r'
    advance(lex); // opening "
    while (!is_at_end(lex) && peek(lex) != '"') {
        advance(lex);
    }
    if (is_at_end(lex)) return error_token(lex, "unterminated raw string");
    advance(lex); // closing "
    return make_token(lex, TOK_RAW_STRING);
}

static Token string_literal(Lexer* lex) {
    // Note: caller (lexer_next) already consumed the opening " via advance(lex)
    // So lex->start points AT opening ", and lex->current points to first content char
    lex->start = lex->current; // start points at first content char
    while (!is_at_end(lex) && peek(lex) != '"') {
        if (peek(lex) == '\\') {
            advance(lex);
            if (peek(lex) != '\0') advance(lex);
        } else if (peek(lex) == '{' && peek_next(lex) != '{') {
            Token t = make_token(lex, TOK_INTERP_STRING_START);
            advance(lex); // {
            lex->interp_stack[lex->interp_depth++] = 1;
            lex->in_interp = true;
            return t;
        } else {
            advance(lex);
        }
    }
    if (is_at_end(lex)) return error_token(lex, "unterminated string");
    Token t = make_token(lex, TOK_STRING);
    advance(lex); // closing "
    return t;
}

static Token multiline_string(Lexer* lex) {
    advance(lex); advance(lex); advance(lex); // """
    lex->start += 2; // adjust past first two quotes
    while (!is_at_end(lex)) {
        if (peek(lex) == '"' && peek_next(lex) == '"' && lex->current[2] == '"') {
            advance(lex); advance(lex); advance(lex);
            return make_token(lex, TOK_STRING);
        }
        if (peek(lex) == '{' && peek_next(lex) != '{') {
            Token t = make_token(lex, TOK_INTERP_STRING_START);
            advance(lex);
            lex->interp_stack[lex->interp_depth++] = 2; // multiline
            return t;
        }
        advance(lex);
    }
    return error_token(lex, "unterminated multiline string");
}

Token lexer_next(Lexer* lex) {
    if (lex->in_interp && lex->interp_depth > 0) {
        // Inside interpolation; return to string on }
    }

    skip_whitespace(lex);
    skip_comment(lex);
    while (peek(lex) == '\n') {
        advance(lex);
        skip_whitespace(lex);
        skip_comment(lex);
    }

    lex->start = lex->current;
    lex->start_line = lex->line;
    lex->start_col = lex->col;

    if (is_at_end(lex)) return make_token(lex, TOK_EOF);

    char c = advance(lex);

    // Identifiers
    if (isalpha((unsigned char)c) || c == '_') {
        return identifier(lex);
    }

    // Numbers
    if (isdigit((unsigned char)c)) {
        // Move back for number()
        lex->current--;
        lex->col--;
        return number(lex);
    }

    switch (c) {
        case '(': return make_token(lex, TOK_LPAREN);
        case ')': return make_token(lex, TOK_RPAREN);
        case '{': {
            if (lex->in_interp) {
                lex->interp_stack[lex->interp_depth-1]++;
            }
            return make_token(lex, TOK_LBRACE);
        }
        case '}': {
            if (lex->in_interp && lex->interp_depth > 0) {
                lex->interp_stack[lex->interp_depth-1]--;
                if (lex->interp_stack[lex->interp_depth-1] == 0) {
                    lex->interp_depth--;
                    if (lex->interp_depth == 0) lex->in_interp = false;
                    // Continue parsing string
                    lex->start = lex->current;
                    lex->start_line = lex->line;
                    lex->start_col = lex->col;
                    int multiline = 0;
                    while (!is_at_end(lex)) {
                        if (peek(lex) == '"') {
                            if (multiline == 0 || (peek_next(lex) == '"' && lex->current[2] == '"')) {
                                if (multiline) { advance(lex); advance(lex); }
                                Token t = make_token(lex, TOK_INTERP_STRING_END);
                                advance(lex);
                                return t;
                            }
                        }
                        if (peek(lex) == '{' && peek_next(lex) != '{') {
                            Token t = make_token(lex, TOK_INTERP_STRING_PART);
                            advance(lex);
                            lex->interp_stack[lex->interp_depth++] = 1;
                            lex->in_interp = true;
                            return t;
                        }
                        advance(lex);
                    }
                    return make_token(lex, TOK_INTERP_STRING_END);
                }
            }
            return make_token(lex, TOK_RBRACE);
        }
        case '[': return make_token(lex, TOK_LBRACKET);
        case ']': return make_token(lex, TOK_RBRACKET);
        case ',': return make_token(lex, TOK_COMMA);
        case ';': return make_token(lex, TOK_SEMI);
        case '~': return make_token(lex, TOK_TILDE);
        case '^': return make_token(lex, match(lex, '=') ? TOK_CARETEQ : TOK_CARET);
        case '%': return make_token(lex, match(lex, '=') ? TOK_PERCENTEQ : TOK_PERCENT);
        case '@': return make_token(lex, TOK_AT);
        case '#': return make_token(lex, TOK_HASH);
        case '?': return make_token(lex, TOK_QUESTION);

        case ':':
            if (match(lex, ':')) return make_token(lex, TOK_COLONCOLON);
            return make_token(lex, TOK_COLON);

        case '.':
            if (match(lex, '.')) {
                if (match(lex, '=')) return make_token(lex, TOK_DOTDOTEQ);
                return make_token(lex, TOK_DOTDOT);
            }
            return make_token(lex, TOK_DOT);

        case '+':
            return make_token(lex, match(lex, '=') ? TOK_PLUSEQ : TOK_PLUS);
        case '-':
            if (peek(lex) == '-') {
                // Comment - but we already skipped whitespace...
                // Backup and let skip_comment handle it
                lex->current--; lex->col--;
                skip_comment(lex);
                return lexer_next(lex);
            }
            if (match(lex, '>')) return make_token(lex, TOK_ARROW);
            if (match(lex, '=')) return make_token(lex, TOK_MINUSEQ);
            return make_token(lex, TOK_MINUS);
        case '*':
            return make_token(lex, match(lex, '=') ? TOK_STAREQ : TOK_STAR);
        case '/':
            return make_token(lex, match(lex, '=') ? TOK_SLASHEQ : TOK_SLASH);

        case '=':
            if (match(lex, '=')) return make_token(lex, TOK_EQEQ);
            if (match(lex, '>')) return make_token(lex, TOK_FAT_ARROW);
            return make_token(lex, TOK_EQ);

        case '!':
            if (match(lex, '=')) return make_token(lex, TOK_NEQ);
            return make_token(lex, TOK_NOT);

        case '<':
            if (match(lex, '=')) return make_token(lex, TOK_LTE);
            if (match(lex, '>')) return make_token(lex, TOK_NEQ);  // <>
            // Custom operator starting with <
            while (isgraph((unsigned char)peek(lex)) && peek(lex) != '(' && peek(lex) != ')' &&
                   peek(lex) != '{' && peek(lex) != '}' && peek(lex) != '[' && peek(lex) != ']' &&
                   peek(lex) != ',' && peek(lex) != ';' && peek(lex) != ':' && peek(lex) != ' ' &&
                   peek(lex) != '\n' && peek(lex) != '\r' && peek(lex) != '\t' && peek(lex) != '"' &&
                   peek(lex) != '\'' && !isalnum((unsigned char)peek(lex))) {
                advance(lex);
            }
            return make_token(lex, TOK_IDENT); // custom operator as identifier
        case '>':
            if (match(lex, '=')) return make_token(lex, TOK_GTE);
            return make_token(lex, TOK_GT);

        case '&':
            if (match(lex, '&')) return make_token(lex, TOK_AND);
            return make_token(lex, match(lex, '=') ? TOK_AMPEQ : TOK_AMP);
        case '|':
            if (match(lex, '|')) return make_token(lex, TOK_OR);
            if (match(lex, '>')) {
                if (match(lex, '?')) {
                    // Check for |?>
                    return make_token(lex, TOK_PIPE_ARROW);
                }
                return make_token(lex, TOK_PIPE_ARROW);
            }
            if (match(lex, '?')) {
                if (match(lex, '>')) return make_token(lex, TOK_PIPE_ARROW);
                lex->current--; lex->col--;
            }
            return make_token(lex, match(lex, '=') ? TOK_PIPEEQ : TOK_PIPE);

        case '\'': return char_literal(lex);
        case '"':
            if (peek(lex) == '"' && peek_next(lex) == '"') {
                return multiline_string(lex);
            }
            return string_literal(lex);
        case 'r':
            if (peek(lex) == '"') return raw_string(lex);
            return identifier(lex);
    }

    if (isgraph((unsigned char)c)) {
        // Custom operators
        while (isgraph((unsigned char)peek(lex)) && peek(lex) != '(' && peek(lex) != ')' &&
               peek(lex) != '{' && peek(lex) != '}' && peek(lex) != '[' && peek(lex) != ']' &&
               peek(lex) != ',' && peek(lex) != ';' && peek(lex) != ':' && peek(lex) != ' ' &&
               peek(lex) != '\n' && peek(lex) != '\r' && peek(lex) != '\t') {
            advance(lex);
        }
        return make_token(lex, TOK_IDENT);
    }

    return error_token(lex, "unexpected character");
}

void lexer_tokenize(Lexer* lex) {
    for (;;) {
        Token t = lexer_next(lex);
        if (t.kind == TOK_INTERP_STRING_START || t.kind == TOK_INTERP_STRING_PART) {
            lex->in_interp = true;
        }
        if (lex->count >= lex->capacity) {
            lex->capacity = lex->capacity == 0 ? 64 : lex->capacity * 2;
            lex->tokens = (Token*)kodas_realloc(lex->tokens, lex->capacity * sizeof(Token));
        }
        lex->tokens[lex->count++] = t;
        if (t.kind == TOK_EOF || t.kind == TOK_ERROR) break;
    }
}

void lexer_free(Lexer* lex) {
    free(lex->tokens);
    lex->tokens = NULL;
    lex->count = lex->capacity = 0;
}

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOK_INT: return "INT";
        case TOK_FLOAT: return "FLOAT";
        case TOK_STRING: return "STRING";
        case TOK_CHAR: return "CHAR";
        case TOK_RAW_STRING: return "RAW_STRING";
        case TOK_INTERP_STRING_START: return "INTERP_STR_START";
        case TOK_INTERP_STRING_PART: return "INTERP_STR_PART";
        case TOK_INTERP_STRING_END: return "INTERP_STR_END";
        case TOK_IDENT: return "IDENT";
        case TOK_LET: return "let";
        case TOK_SEAL: return "seal";
        case TOK_SHOW: return "show";
        case TOK_FN: return "fn";
        case TOK_IF: return "if";
        case TOK_ELSE: return "else";
        case TOK_MATCH: return "match";
        case TOK_LOOP: return "loop";
        case TOK_WHILE: return "while";
        case TOK_IN: return "in";
        case TOK_STOP: return "stop";
        case TOK_SKIP: return "skip";
        case TOK_GIVE: return "give";
        case TOK_RETURN: return "return";
        case TOK_STRUCT: return "struct";
        case TOK_ENUM: return "enum";
        case TOK_BEHAVE: return "behave";
        case TOK_TRAIT: return "trait";
        case TOK_IMPL: return "impl";
        case TOK_IS: return "is";
        case TOK_PUB: return "pub";
        case TOK_AS: return "as";
        case TOK_MODULE: return "module";
        case TOK_IMPORT: return "import";
        case TOK_USE: return "use";
        case TOK_MUT: return "mut";
        case TOK_TRUE: return "true";
        case TOK_FALSE: return "false";
        case TOK_MAYBE: return "maybe";
        case TOK_EMPTY: return "empty";
        case TOK_OK: return "ok";
        case TOK_ERR: return "err";
        case TOK_RESULT: return "Result";
        case TOK_NEVER: return "never";
        case TOK_ANY: return "any";
        case TOK_FLOW: return "flow";
        case TOK_TOGETHER: return "together";
        case TOK_TIMEOUT: return "timeout";
        case TOK_CHANNEL: return "Channel";
        case TOK_BRIDGE: return "bridge";
        case TOK_LINK: return "link";
        case TOK_TEST: return "test";
        case TOK_BENCH: return "bench";
        case TOK_ASSERT: return "assert";
        case TOK_MEASURE: return "measure";
        case TOK_CONTRACT: return "contract";
        case TOK_THEN: return "then";
        case TOK_OTHERWISE: return "otherwise";
        case TOK_DEMAND: return "demand";
        case TOK_FOR: return "for";
        case TOK_OP: return "op";
        case TOK_SELF: return "self";
        case TOK_NEW_KW: return "new";
        case TOK_TYPE_KW: return "type";
        case TOK_WHERE: return "where";
        case TOK_PLUS: return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR: return "*";
        case TOK_SLASH: return "/";
        case TOK_PERCENT: return "%";
        case TOK_EQ: return "=";
        case TOK_EQEQ: return "==";
        case TOK_NEQ: return "!=";
        case TOK_LT: return "<";
        case TOK_GT: return ">";
        case TOK_LTE: return "<=";
        case TOK_GTE: return ">=";
        case TOK_AND: return "&&";
        case TOK_OR: return "||";
        case TOK_NOT: return "!";
        case TOK_AMP: return "&";
        case TOK_PIPE: return "|";
        case TOK_CARET: return "^";
        case TOK_TILDE: return "~";
        case TOK_PLUSEQ: return "+=";
        case TOK_MINUSEQ: return "-=";
        case TOK_STAREQ: return "*=";
        case TOK_SLASHEQ: return "/=";
        case TOK_PERCENTEQ: return "%=";
        case TOK_AMPEQ: return "&=";
        case TOK_PIPEEQ: return "|=";
        case TOK_CARETEQ: return "^=";
        case TOK_LPAREN: return "(";
        case TOK_RPAREN: return ")";
        case TOK_LBRACE: return "{";
        case TOK_RBRACE: return "}";
        case TOK_LBRACKET: return "[";
        case TOK_RBRACKET: return "]";
        case TOK_COMMA: return ",";
        case TOK_DOT: return ".";
        case TOK_DOTDOT: return "..";
        case TOK_DOTDOTEQ: return "..=";
        case TOK_SEMI: return ";";
        case TOK_COLON: return ":";
        case TOK_COLONCOLON: return "::";
        case TOK_ARROW: return "->";
        case TOK_FAT_ARROW: return "=>";
        case TOK_PIPE_ARROW: return "|>";
        case TOK_QUESTION: return "?";
        case TOK_HASH: return "#";
        case TOK_AT: return "@";
        case TOK_UNDERSCORE: return "_";
        case TOK_NEWLINE: return "NEWLINE";
        case TOK_ERROR: return "ERROR";
        case TOK_EOF: return "EOF";
        default: return "?";
    }
}

void token_print(Token* t) {
    printf("%-20s '%.*s' [line %d:%d]\n",
           token_kind_name(t->kind), t->length, t->start, t->line, t->col);
}
