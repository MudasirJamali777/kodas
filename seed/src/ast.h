/// ast.h - Kodas AST node definitions
#ifndef KODAS_AST_H
#define KODAS_AST_H

#include "common.h"
#include "lexer.h"

typedef enum {
    // Literals
    EXPR_INT, EXPR_FLOAT, EXPR_BOOL, EXPR_STRING, EXPR_CHAR, EXPR_EMPTY,
    EXPR_IDENT, EXPR_INTERP_STRING,
    // Collections
    EXPR_LIST, EXPR_MAP, EXPR_SET, EXPR_TUPLE, EXPR_RANGE, EXPR_RANGE_INCL,
    // Operations
    EXPR_BINARY, EXPR_UNARY,
    // Access
    EXPR_INDEX, EXPR_FIELD, EXPR_CALL, EXPR_METHOD_CALL,
    // Constructs
    EXPR_IF, EXPR_MATCH, EXPR_FN, EXPR_BLOCK, EXPR_LAMBDA,
    EXPR_LET, EXPR_SEAL, EXPR_ASSIGN, EXPR_COMPOUND_ASSIGN,
    EXPR_LOOP, EXPR_LOOP_IN, EXPR_WHILE,
    EXPR_STOP, EXPR_SKIP, EXPR_GIVE,
    EXPR_SHOW, EXPR_ASSERT,
    EXPR_PIPE, EXPR_ERR_PIPE,
    EXPR_STRUCT_INIT, EXPR_STRUCT_LIT,
    EXPR_ARRAY_COMP,
    EXPR_OK, EXPR_ERR,
    EXPR_THEN_ELSE,
    EXPR_FIELD_ASSIGN,
    EXPR_SELF,
    EXPR_LAMBDA_SHORT,
    EXPR_LIST_PUSH,
    EXPR_SPREAD,
} ExprKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef struct {
    char** items;
    int count;
    int capacity;
} NameList;

typedef struct {
    Expr** items;
    int count;
    int capacity;
} ExprList;

typedef struct {
    Stmt** items;
    int count;
    int capacity;
} StmtList;

// Include value.h after the forward decls we need
// ValueList uses Value* inline and is defined in value.h

typedef enum {
    PAT_WILDCARD, PAT_LITERAL, PAT_BINDING, PAT_TUPLE, PAT_LIST,
    PAT_STRUCT, PAT_ENUM_VARIANT, PAT_RANGE, PAT_GUARD
} PatKind;

typedef struct Pattern {
    PatKind kind;
    Location loc;
    Expr* literal;         // for PAT_LITERAL
    char* name;            // for PAT_BINDING / PAT_ENUM_VARIANT
    struct Pattern** sub;  // sub-patterns
    int sub_count;
    int sub_cap;
    char** field_names;    // for PAT_STRUCT
    struct Pattern** field_pats;
    int field_count;
    Expr* guard;           // for PAT_GUARD
    bool is_mut;
} Pattern;

typedef struct {
    char* name;
    char* type_ann;
    Expr* default_value;
    bool is_mut;
    bool is_self;
} Param;

typedef struct {
    Param* items;
    int count;
    int capacity;
} ParamList;

typedef enum {
    TYPE_INFER, TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_STRING, TYPE_CHAR,
    TYPE_BYTE, TYPE_NAMED, TYPE_LIST, TYPE_MAP, TYPE_SET, TYPE_TUPLE,
    TYPE_MAYBE, TYPE_RESULT, TYPE_FUNC, TYPE_ANY, TYPE_NEVER, TYPE_USER,
    TYPE_GENERIC, TYPE_CHANNEL
} TypeNodeKind;

typedef struct TypeNode {
    TypeNodeKind kind;
    char* name;
    struct TypeNode** params;
    int param_count;
    struct TypeNode* ret;
    bool optional;
} TypeNode;

struct Expr {
    ExprKind kind;
    Location loc;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        char* str_val;
        char char_val;
        struct { Expr** parts; int count; } interp;
        struct { Expr* left; Expr* right; char* op; } binary;
        struct { char* op; Expr* operand; } unary;
        struct { Expr* collection; Expr* index; } index;
        struct { Expr* object; char* field; } field;
        struct { Expr* callee; ExprList args; } call;
        struct { Expr* object; char* method; ExprList args; } method_call;
        struct { char* name; bool is_mut; TypeNode* type; Expr* value; } let;
        struct { char* name; Expr* value; } assign;
        struct { Expr* target; char* op; Expr* value; } compound;
        struct { Expr* cond; Expr* then_branch; Expr* else_branch; } if_e;
        struct { Expr* obj; struct { Pattern* pat; Expr* body; }* arms; int arm_count; } match_e;
        struct { ParamList params; TypeNode* ret_type; Expr* body; bool is_arrow; char* name; } fn_e;
        struct { StmtList stmts; } block;
        struct { Expr* callee; Expr** pipelines; int pipe_count; bool is_error; } pipe;
        struct { Expr* count; Expr* body; } loop_e;
        struct { char* var; char* idx_var; Expr* iter; Expr* body; bool is_while; Expr* cond; } loop_in;
        struct { char* name; bool is_pub; } struct_field;
        struct { char* name; Expr** field_vals; char** field_names; int field_count; } struct_init;
        struct { Expr* value; bool is_err; } result_e;
        struct { Expr* cond; Expr* then_val; Expr* else_val; } then_else;
        struct { Expr* object; char* field; Expr* value; } field_assign;
        struct { Expr* elem; } list_push;
        Expr* expr;  // generic sub-expression for show/assert/give/stop/skip
    } as;
};

typedef enum {
    STMT_EXPR, STMT_LET, STMT_SEAL, STMT_FN, STMT_STRUCT, STMT_ENUM, STMT_BEHAVE,
    STMT_TRAIT, STMT_IMPL, STMT_MODULE, STMT_IMPORT, STMT_BRIDGE,
    STMT_TEST, STMT_BENCH, STMT_CONTRACT, STMT_DECORATOR, STMT_RETURN,
    STMT_PUB
} StmtKind;

typedef struct {
    char* name;
    TypeNode* type;
    Expr* default_val;
    bool is_pub;
} StructField;

typedef struct {
    StructField* items;
    int count;
    int capacity;
} StructFieldList;

typedef struct {
    char* name;
    ParamList fields;   // for variants with associated values
} EnumVariant;

typedef struct {
    EnumVariant* items;
    int count;
    int capacity;
} EnumVariantList;

typedef struct {
    char* name;
    ParamList params;
    TypeNode* ret_type;
    Expr* body;
    bool is_pub;
    bool has_body;
    bool is_arrow;
} FnDecl;

typedef struct {
    FnDecl* items;
    int count;
    int capacity;
} FnDeclList;

typedef struct {
    char* name;
    char** bounds;    // trait bounds
    int bound_count;
} GenericParam;

typedef struct {
    GenericParam* items;
    int count;
    int capacity;
} GenericParamList;

typedef struct TraitMethod {
    char* name;
    ParamList params;
    TypeNode* ret_type;
    Expr* default_body;
} TraitMethod;

struct Stmt {
    StmtKind kind;
    Location loc;
    union {
        Expr* expr;
        struct {
            char* name;
            bool is_mut;
            TypeNode* type;
            Expr* value;
            bool is_pub;
            // Destructuring: let (a, b, c) = value
            bool is_destruct;
            char** names;       // array of names (NULL for wildcards)
            bool* muts;
            int destruct_count;
        } let_s;
        struct { char* name; bool is_pub; } seal_s;
        struct {
            char* name;
            ParamList params;
            TypeNode* ret_type;
            Expr* body;
            bool is_pub;
            bool is_arrow;
            GenericParamList generics;
            bool is_op;
            char* op_name;
        } fn_s;
        struct {
            char* name;
            StructFieldList fields;
            bool is_pub;
            GenericParamList generics;
        } struct_s;
        struct {
            char* name;
            EnumVariantList variants;
            bool is_pub;
            GenericParamList generics;
        } enum_s;
        struct {
            char* type_name;
            char** traits;
            int trait_count;
            FnDeclList methods;
            bool is_pub;
            GenericParamList generics;
        } behave_s;
        struct {
            char* name;
            TraitMethod* methods;
            int method_count;
            bool is_pub;
            GenericParamList generics;
        } trait_s;
        struct {
            char* trait_name;
            char* type_name;
            FnDeclList methods;
            bool is_pub;
        } impl_s;
        struct { char* name; NameList imports; bool is_pub; } import_s;
        struct { char* name; bool is_pub; } module_s;
        struct { char* lang; StmtList stmts; } bridge_s;
        struct { char* name; Expr* body; bool is_bench; ExprList bench_stmts; } test_s;
        struct { ExprList decls; } contract_s;
        struct { char* name; ExprList args; Stmt* stmt; } decorator_s;
    } as;
    Stmt* next;  // linked list of statements
};

typedef struct {
    Stmt* first;
    Stmt* last;
    int count;
} Program;

/// Constructors
Expr* expr_new(ExprKind kind, Location loc);
Stmt* stmt_new(StmtKind kind, Location loc);
void program_init(Program* prog);
void program_add(Program* prog, Stmt* s);
void expr_free(Expr* e);
void stmt_free(Stmt* s);
void program_free(Program* prog);
Pattern* pattern_new(PatKind kind, Location loc);
void pattern_free(Pattern* p);
TypeNode* type_new(TypeNodeKind kind);
void type_free(TypeNode* t);
char* type_to_string(TypeNode* t);

/// Helper constructors
Expr* expr_int(int64_t val, Location loc);
Expr* expr_float(double val, Location loc);
Expr* expr_bool(bool val, Location loc);
Expr* expr_string(const char* val, Location loc);
Expr* expr_ident(const char* name, Location loc);
Expr* expr_binary(Expr* left, const char* op, Expr* right, Location loc);
Expr* expr_unary(const char* op, Expr* operand, Location loc);
Expr* expr_call(Expr* callee, ExprList args, Location loc);
Expr* expr_block(StmtList stmts, Location loc);
Expr* expr_if(Expr* cond, Expr* then_b, Expr* else_b, Location loc);

void expr_list_append(ExprList* list, Expr* e);
void stmt_list_append(StmtList* list, Stmt* s);
void param_list_append(ParamList* list, Param p);
void name_list_append(NameList* list, const char* name);

#endif // KODAS_AST_H
