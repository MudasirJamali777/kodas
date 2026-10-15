/// lexer.h - Kodas lexer/tokenizer
#ifndef KODAS_LEXER_H
#define KODAS_LEXER_H

#include "common.h"

typedef enum {
    // Literals
    TOK_INT, TOK_FLOAT, TOK_STRING, TOK_CHAR, TOK_RAW_STRING,
    TOK_INTERP_STRING_START, TOK_INTERP_STRING_PART, TOK_INTERP_STRING_END,
    TOK_IDENT,

    // Keywords
    TOK_LET, TOK_SEAL, TOK_SHOW, TOK_FN, TOK_IF, TOK_ELSE, TOK_MATCH,
    TOK_LOOP, TOK_WHILE, TOK_IN, TOK_STOP, TOK_SKIP, TOK_GIVE, TOK_RETURN,
    TOK_STRUCT, TOK_ENUM, TOK_BEHAVE, TOK_TRAIT, TOK_IS, TOK_PUB,
    TOK_AS, TOK_MODULE, TOK_IMPORT, TOK_USE, TOK_MUT, TOK_TRUE, TOK_FALSE,
    TOK_MAYBE, TOK_EMPTY, TOK_OK, TOK_ERR, TOK_RESULT, TOK_NEVER, TOK_ANY,
    TOK_FLOW, TOK_TOGETHER, TOK_TIMEOUT, TOK_CHANNEL, TOK_BRIDGE, TOK_LINK,
    TOK_TEST, TOK_BENCH, TOK_ASSERT, TOK_MEASURE, TOK_CONTRACT,
    TOK_THEN, TOK_OTHERWISE, TOK_DEMAND, TOK_FOR,
    TOK_OP, TOK_SELF, TOK_NEW_KW, TOK_TYPE_KW, TOK_WHERE,

    // Operators and punctuation
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_EQEQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,
    TOK_AND, TOK_OR, TOK_NOT, TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ, TOK_SLASHEQ, TOK_PERCENTEQ,
    TOK_AMPEQ, TOK_PIPEEQ, TOK_CARETEQ,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_DOT, TOK_DOTDOT, TOK_DOTDOTEQ, TOK_SEMI, TOK_COLON, TOK_COLONCOLON,
    TOK_ARROW, TOK_FAT_ARROW, TOK_PIPE_ARROW,
    TOK_QUESTION, TOK_HASH, TOK_AT, TOK_UNDERSCORE,

    // Special
    TOK_NEWLINE, TOK_INDENT, TOK_DEDENT,
    TOK_ERROR, TOK_EOF
} TokenKind;

typedef struct {
    TokenKind kind;
    const char* start;
    int length;
    int line;
    int col;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
    } num;
} Token;

typedef struct {
    const char* file;
    const char* source;
    const char* start;
    const char* current;
    int line;
    int col;
    int start_line;
    int start_col;
    Token* tokens;
    int count;
    int capacity;
    // Interpolation nesting (for strings with {expr})
    int interp_stack[64];
    int interp_depth;
    bool in_interp;
} Lexer;

void lexer_init(Lexer* lex, const char* file, const char* source);
void lexer_tokenize(Lexer* lex);
Token lexer_next(Lexer* lex);
void lexer_free(Lexer* lex);
const char* token_kind_name(TokenKind kind);
void token_print(Token* t);

#endif // KODAS_LEXER_H
