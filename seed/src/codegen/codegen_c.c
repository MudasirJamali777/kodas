/// codegen_c.c - Kodas-to-C transpiler (subset, produces compilable C)
///
/// Transpiles a subset of Kodas to C99, then invokes the system C compiler (gcc/clang/cl)
/// to produce a native binary. Supports:
///   - int/float/bool/string literals
///   - arithmetic + comparison
///   - let / mut variables
///   - fn definitions and calls (top-level only)
///   - if/else, while
///   - show() -> printf()
///   - recursion (fib, factorial work)
///   - `import core.math` / `import core.strings` etc. — inlined into the output
///
/// Limitations (subset): no closures, no struct methods, no pattern matching, no lists, no enums.
/// These will run via the interpreter for now; the C backend targets the "algorithmic core"
/// so compute-heavy Kodas code can compile to fast native.
#include "codegen/codegen_c.h"
#include "../common.h"
#include "../ast.h"
#include "../lexer.h"
#include "../parser.h"
#include "../fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

typedef struct {
    FILE* out;
    int indent;
    int temp_var_counter;
    int had_error;
    char error_msg[256];
} CGen;

static void cg_i(CGen* g) { for (int i = 0; i < g->indent; i++) fputc(' ', g->out); }
static void cg_p(CGen* g, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    cg_i(g); vfprintf(g->out, fmt, ap);
    fputc('\n', g->out);
    va_end(ap);
}

// Rewrite Kodas identifiers to C identifiers. Avoids C keywords and libm
// collisions (where our stdlib/core/math.kds defines helpers that shadow libm).
static const char* c_id(const char* name) {
    static const char* c_keywords[] = {
        "int","float","double","char","if","else","while","for","return","void",
        "switch","case","break","continue","struct","enum","union","const","static",
        "extern","register","typedef","sizeof","long","short","unsigned","signed",
        "volatile","goto","auto","bool","true","false","NULL","inline","restrict",
        "_Bool","_Complex","_Imaginary"
    };
    for (int i = 0; i < (int)(sizeof(c_keywords)/sizeof(*c_keywords)); i++) {
        if (strcmp(name, c_keywords[i]) == 0) {
            static char buf[256];
            snprintf(buf, sizeof(buf), "k_%s", name);
            return buf;
        }
    }
    // Map our math constants (defined by core/math.kds or preamble) to k_-prefixed
    // names so they don't clash with macros like M_PI / identifiers like `E`.
    if (strcmp(name, "PI") == 0) return "k_PI";
    if (strcmp(name, "E") == 0)  return "k_E";
    if (strcmp(name, "TAU") == 0) return "k_TAU";
    if (strcmp(name, "DEG_TO_RAD") == 0) return "k_DEG_TO_RAD";
    if (strcmp(name, "RAD_TO_DEG") == 0) return "k_RAD_TO_DEG";
    return name;
}

// Return true if fn name is provided by the C preamble (either libm or our
// inline helpers) so we skip re-emitting it from stdlib source.
static bool is_preamble_fn(const char* name) {
    static const char* names[] = {
        // libm
        "sin","cos","tan","log","exp","floor","ceil","round","pow","sqrt","abs",
        // math.kds replacements (provided as d_* wrappers; we #define them below)
        "radians","degrees","sign","clamp","lerp","is_nan","is_finite",
        NULL
    };
    for (int i = 0; names[i]; i++)
        if (strcmp(name, names[i]) == 0) return true;
    return false;
}

// Functions that return floating-point (used when inferring decl types so
// default numeric stays long long but math helpers use double).

// Return true if name is a math constant we pre-define in the C preamble so
// we don't re-emit the definition from stdlib/core/math.kds.
static bool is_preamble_const(const char* name) {
    return strcmp(name, "PI") == 0 || strcmp(name, "E") == 0 ||
           strcmp(name, "TAU") == 0 || strcmp(name, "DEG_TO_RAD") == 0 ||
           strcmp(name, "RAD_TO_DEG") == 0;
}

// Forward decls
static void cg_expr(CGen* g, Expr* e);
static void cg_stmt(CGen* g, Stmt* s);


static void cg_block(CGen* g, Expr* body) {
    if (body && body->kind == EXPR_BLOCK) {
        cg_p(g, "{");
        g->indent += 4;
        for (int i = 0; i < body->as.block.stmts.count; i++) {
            cg_stmt(g, body->as.block.stmts.items[i]);
        }
        g->indent -= 4;
        cg_p(g, "}");
    } else if (body) {
        cg_i(g);
        cg_expr(g, body);
        fprintf(g->out, ";\n");
    }
}

static void cg_literal_str(CGen* g, const char* s) {
    fputc('"', g->out);
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case '\n': fputs("\\n", g->out); break;
            case '\t': fputs("\\t", g->out); break;
            case '\r': fputs("\\r", g->out); break;
            case '\\': fputs("\\\\", g->out); break;
            case '"':  fputs("\\\"", g->out); break;
            default:   fputc(*p, g->out);
        }
    }
    fputc('"', g->out);
}

static void cg_expr(CGen* g, Expr* e) {
    if (!e) { fprintf(g->out, "0"); return; }
    switch (e->kind) {
        case EXPR_INT:    fprintf(g->out, "%lldLL", (long long)e->as.int_val); break;
        case EXPR_FLOAT:  fprintf(g->out, "%g", e->as.float_val); break;
        case EXPR_BOOL:   fprintf(g->out, "%s", e->as.bool_val ? "1" : "0"); break;
        case EXPR_STRING: cg_literal_str(g, e->as.str_val ? e->as.str_val : ""); break;
        case EXPR_CHAR:   fprintf(g->out, "'%c'", e->as.char_val); break;
        case EXPR_EMPTY:  fprintf(g->out, "0"); break;

        case EXPR_IDENT:  fprintf(g->out, "%s", c_id(e->as.str_val)); break;

        case EXPR_BINARY: {
            const char* op = e->as.binary.op;
            fprintf(g->out, "(");
            // Special: string concat -> helper
            bool is_strcat = (strcmp(op, "+") == 0); // rough; we assume both numeric
            if (strcmp(op, "and") == 0 || strcmp(op, "&&") == 0) op = "&&";
            else if (strcmp(op, "or") == 0 || strcmp(op, "||") == 0) op = "||";
            else if (strcmp(op, "<>") == 0) op = "!=";
            cg_expr(g, e->as.binary.left);
            fprintf(g->out, " %s ", op);
            cg_expr(g, e->as.binary.right);
            fprintf(g->out, ")");
            (void)is_strcat;
            break;
        }
        case EXPR_UNARY: {
            const char* op = e->as.unary.op;
            if (strcmp(op, "not") == 0) op = "!";
            fprintf(g->out, "(%s", op);
            cg_expr(g, e->as.unary.operand);
            fprintf(g->out, ")");
            break;
        }

        case EXPR_CALL: {
            // Show / print become printf
            const char* name = NULL;
            if (e->as.call.callee->kind == EXPR_IDENT) name = e->as.call.callee->as.str_val;
            if (name && strcmp(name, "show") == 0) {
                fprintf(g->out, "printf(");
                for (int i = 0; i < e->as.call.args.count; i++) {
                    if (i > 0) fprintf(g->out, "\" \", ");
                    Expr* a = e->as.call.args.items[i];
                    bool is_str = a && a->kind == EXPR_STRING;
                    bool is_int = a && a->kind == EXPR_INT;
                    if (is_str) {
                        cg_expr(g, a);
                    } else if (is_int) {
                        fprintf(g->out, "\"%%lld\", (long long)(");
                        cg_expr(g, a); fprintf(g->out, ")");
                    } else {
                        fprintf(g->out, "\"%%g\", (double)(");
                        cg_expr(g, a); fprintf(g->out, ")");
                    }
                    fprintf(g->out, ", ");
                }
                fprintf(g->out, "\"\\n\")");
                break;
            }
            if (name && strcmp(name, "assert") == 0 && e->as.call.args.count > 0) {
                fprintf(g->out, "((void)(!(");
                cg_expr(g, e->as.call.args.items[0]);
                fprintf(g->out, ") && (fprintf(stderr, \"Assert failed at line %%d\\n\", __LINE__), abort(), 0)))");
                break;
            }
            // Stubs for unsupported builtins that return 0 / no-op
            if (name && (strcmp(name, "len") == 0 || strcmp(name, "push") == 0 ||
                         strcmp(name, "str") == 0 || strcmp(name, "input") == 0 ||
                         strcmp(name, "read_file") == 0 || strcmp(name, "write_file") == 0 ||
                         strcmp(name, "append_file") == 0 || strcmp(name, "clock") == 0 ||
                         strcmp(name, "sleep") == 0 || strcmp(name, "typename") == 0 ||
                         strcmp(name, "ok") == 0 || strcmp(name, "err") == 0 ||
                         strcmp(name, "keys") == 0 || strcmp(name, "values") == 0 ||
                         strcmp(name, "split") == 0 || strcmp(name, "contains") == 0 ||
                         strcmp(name, "chars") == 0 || strcmp(name, "lines") == 0 ||
                         strcmp(name, "trim") == 0 || strcmp(name, "starts_with") == 0 ||
                         strcmp(name, "ends_with") == 0 || strcmp(name, "replace") == 0 ||
                         strcmp(name, "list_get") == 0 || strcmp(name, "list_set") == 0 ||
                         strcmp(name, "pop") == 0 || strcmp(name, "insert") == 0 ||
                         strcmp(name, "remove") == 0 || strcmp(name, "typeof") == 0 ||
                         strcmp(name, "exit") == 0 || strcmp(name, "bool") == 0 ||
                         strcmp(name, "char") == 0)) {
                fprintf(g->out, "0 /* builtin %s */", name);
                fprintf(g->out, "/*(");
                for (int i = 0; i < e->as.call.args.count; i++) { if(i)fprintf(g->out,","); cg_expr(g, e->as.call.args.items[i]); }
                fprintf(g->out, ")*/");
                break;
            }
            // Numeric conversions
            if (name && strcmp(name, "int") == 0) {
                fprintf(g->out, "((long long)(");
                if (e->as.call.args.count > 0) cg_expr(g, e->as.call.args.items[0]); else fprintf(g->out,"0");
                fprintf(g->out, "))");
                break;
            }
            if (name && strcmp(name, "float") == 0) {
                fprintf(g->out, "((double)(");
                if (e->as.call.args.count > 0) cg_expr(g, e->as.call.args.items[0]); else fprintf(g->out,"0");
                fprintf(g->out, "))");
                break;
            }
            // Math.h mappings
            if (name && strcmp(name, "abs") == 0) { fprintf(g->out, "k_abs"); }
            else if (name && (strcmp(name, "sqrt") == 0 || strcmp(name, "sin") == 0 ||
                              strcmp(name, "cos") == 0 || strcmp(name, "tan") == 0 ||
                              strcmp(name, "log") == 0 || strcmp(name, "exp") == 0 ||
                              strcmp(name, "floor") == 0 || strcmp(name, "ceil") == 0 ||
                              strcmp(name, "round") == 0 || strcmp(name, "pow") == 0)) {
                fprintf(g->out, "%s", name);
            }
            else { cg_expr(g, e->as.call.callee); }
            fprintf(g->out, "(");
            for (int i = 0; i < e->as.call.args.count; i++) {
                if (i > 0) fprintf(g->out, ", ");
                cg_expr(g, e->as.call.args.items[i]);
            }
            fprintf(g->out, ")");
            break;
        }
        case EXPR_BLOCK: {
            // Use GCC statement expressions; last expression is value.
            int n = e->as.block.stmts.count;
            fprintf(g->out, "({\n");
            g->indent += 4;
            cg_i(g); fprintf(g->out, "long long __blkv = 0;\n");
            for (int i = 0; i < n - 1; i++) {
                cg_stmt(g, e->as.block.stmts.items[i]);
            }
            // Last statement may be EXPR_GIVE (handled by cg_stmt) or an expr to be the value
            if (n > 0) {
                Stmt* last = e->as.block.stmts.items[n-1];
                if (last->kind == STMT_EXPR && last->as.expr->kind == EXPR_GIVE) {
                    cg_stmt(g, last);
                    cg_i(g); fprintf(g->out, "0LL;\n");
                } else if (last->kind == STMT_EXPR) {
                    cg_i(g); fprintf(g->out, "__blkv = ");
                    cg_expr(g, last->as.expr);
                    fprintf(g->out, ";\n");
                } else {
                    cg_stmt(g, last);
                    cg_i(g); fprintf(g->out, "0LL;\n");
                }
            }
            g->indent -= 4;
            cg_i(g); fprintf(g->out, "__blkv;\n");
            cg_i(g); fprintf(g->out, "})");
            break;
        }

        case EXPR_GIVE: {
            fprintf(g->out, "return ");
            if (e->as.expr) cg_expr(g, e->as.expr);
            break;
        }
        case EXPR_STOP: fprintf(g->out, "break"); break;
        case EXPR_SKIP: fprintf(g->out, "continue"); break;

        case EXPR_SHOW: {
            Expr* v = e->as.expr;
            if (v && v->kind == EXPR_STRING) {
                fprintf(g->out, "printf(");
                cg_literal_str(g, v->as.str_val ? v->as.str_val : "");
                fprintf(g->out, "); printf(\"\\n\")");
            } else if (v && v->kind == EXPR_INT) {
                fprintf(g->out, "printf(\"%%lld\\n\", (long long)(");
                cg_expr(g, v); fprintf(g->out, "))");
            } else {
                fprintf(g->out, "printf(\"%%g\\n\", (double)(");
                if (v) cg_expr(g, v); else fprintf(g->out, "0");
                fprintf(g->out, "))");
            }
            break;
        }
        case EXPR_ASSERT: {
            fprintf(g->out, "((void)(!(");
            if (e->as.expr) cg_expr(g, e->as.expr);
            fprintf(g->out, ") && (fprintf(stderr, \"Assert failed at line %%d\\n\", __LINE__), abort(), 0)))");
            break;
        }
        case EXPR_ASSIGN: {
            // Simple identifier assignment
            fprintf(g->out, "(%s = ", c_id(e->as.assign.name));
            cg_expr(g, e->as.assign.value);
            fprintf(g->out, ")");
            break;
        }
        case EXPR_COMPOUND_ASSIGN: {
            // Compound assign like += uses as.assign.{name,value} with op stored separately
            fprintf(g->out, "(%s", c_id(e->as.assign.name));
            // Find the operator from token; easiest: re-derive from expr kind label not available,
            // but for transpile we fall back to a = a op (value) — approximation.
            fprintf(g->out, " = %s ", c_id(e->as.assign.name));
            cg_expr(g, e->as.assign.value);
            fprintf(g->out, ")");
            break;
        }
        case EXPR_FIELD_ASSIGN: {
            cg_expr(g, NULL); // stub
            fprintf(g->out, "/* field assign */0");
            break;
        }
        case EXPR_WHILE: {
            fprintf(g->out, "while (");
            if (e->as.loop_in.cond) cg_expr(g, e->as.loop_in.cond); else fprintf(g->out, "1");
            fprintf(g->out, ") ");
            if (e->as.loop_in.body) cg_expr(g, e->as.loop_in.body); else fprintf(g->out, "{}");
            break;
        }
        case EXPR_LOOP: {
            fprintf(g->out, "while (1) ");
            if (e->as.loop_e.body) cg_expr(g, e->as.loop_e.body); else fprintf(g->out, "{}");
            break;
        }
        case EXPR_IF: {
            fprintf(g->out, "(");
            cg_expr(g, e->as.if_e.cond);
            fprintf(g->out, " ? ");
            cg_expr(g, e->as.if_e.then_branch);
            fprintf(g->out, " : ");
            if (e->as.if_e.else_branch) cg_expr(g, e->as.if_e.else_branch);
            else fprintf(g->out, "0");
            fprintf(g->out, ")");
            break;
        }

        default:
            fprintf(g->out, "0 /* unsupported cg expr kind %d */", (int)e->kind);
            g->had_error = true;
            break;
    }
}

static void cg_params(CGen* g, ParamList params, bool first_is_self) {
    bool first = true;
    for (int i = 0; i < params.count; i++) {
        if (first_is_self && i == 0 && params.items[i].is_self) continue;
        if (!first) fprintf(g->out, ", ");
        first = false;
        const char* ty = "long long";
        if (params.items[i].type_ann) {
            if (strcmp(params.items[i].type_ann, "Float") == 0) ty = "double";
            else if (strcmp(params.items[i].type_ann, "Bool") == 0) ty = "int";
            else if (strcmp(params.items[i].type_ann, "String") == 0) ty = "const char*";
            else if (strcmp(params.items[i].type_ann, "Int") == 0) ty = "long long";
        }
        fprintf(g->out, "%s %s", ty, c_id(params.items[i].name ? params.items[i].name : "_"));
    }
    if (first) fprintf(g->out, "void");
}

static void cg_ret_type(CGen* g, TypeNode* ret, const char* fname) {
    (void)g;
    if (fname && strcmp(fname, "main") == 0) { fprintf(g->out, "int"); return; }
    if (!ret) { fprintf(g->out, "long long"); return; }
    if (ret->kind == TYPE_FLOAT) fprintf(g->out, "double");
    else if (ret->kind == TYPE_BOOL) fprintf(g->out, "int");
    else if (ret->kind == TYPE_STRING) fprintf(g->out, "const char*");
    else fprintf(g->out, "long long");
}

// Detect whether a fn body "gives" a value (ends with give or has give).
// Simple heuristic: count EXPR_GIVE nodes; if any non-empty, non-void.

static void cg_stmt(CGen* g, Stmt* s) {
    if (!s) return;
    switch (s->kind) {
        case STMT_EXPR: {
            // Control-flow expressions at statement position become C statements
            if (s->as.expr->kind == EXPR_IF) {
                Expr* cond = s->as.expr->as.if_e.cond;
                Expr* th   = s->as.expr->as.if_e.then_branch;
                Expr* el   = s->as.expr->as.if_e.else_branch;
                cg_i(g); fprintf(g->out, "if ("); cg_expr(g, cond); fprintf(g->out, ") ");
                if (th && th->kind == EXPR_BLOCK) cg_block(g, th);
                else if (th) { cg_i(g); fprintf(g->out, "{ "); cg_expr(g, th); fprintf(g->out, "; }\n"); }
                else { fprintf(g->out, "{}\n"); }
                if (el) {
                    if (el->kind == EXPR_IF) {
                        // else if: recurse as statement
                        cg_i(g); fprintf(g->out, "else ");
                        Stmt el_stmt = {0}; el_stmt.kind = STMT_EXPR; el_stmt.as.expr = el;
                        cg_stmt(g, &el_stmt);
                    } else {
                        cg_i(g); fprintf(g->out, "else ");
                        if (el->kind == EXPR_BLOCK) cg_block(g, el);
                        else { fprintf(g->out, "{ "); cg_expr(g, el); fprintf(g->out, "; }\n"); }
                    }
                }
                break;
            }
            if (s->as.expr->kind == EXPR_WHILE) {
                cg_i(g); fprintf(g->out, "while (");
                cg_expr(g, s->as.expr->as.loop_in.cond);
                fprintf(g->out, ") ");
                Expr* body = s->as.expr->as.loop_in.body;
                if (body && body->kind == EXPR_BLOCK) cg_block(g, body);
                else { fprintf(g->out, "{}\n"); }
                break;
            }
            if (s->as.expr->kind == EXPR_LOOP) {
                cg_i(g); fprintf(g->out, "while (1) ");
                Expr* body = s->as.expr->as.loop_e.body;
                if (body && body->kind == EXPR_BLOCK) cg_block(g, body);
                else { fprintf(g->out, "{}\n"); }
                break;
            }
            if (s->as.expr->kind == EXPR_GIVE) {
                cg_i(g); fprintf(g->out, "return");
                if (s->as.expr->as.expr) { fprintf(g->out, " "); cg_expr(g, s->as.expr->as.expr); }
                fprintf(g->out, ";\n");
                break;
            }
            cg_i(g);
            cg_expr(g, s->as.expr);
            fprintf(g->out, ";\n");
            break;
        }
        case STMT_LET:
        case STMT_SEAL: {
            const char* ty = "long long";
            if (s->as.let_s.type) {
                TypeNode* t = s->as.let_s.type;
                if (t->kind == TYPE_FLOAT) ty = "double";
                else if (t->kind == TYPE_BOOL) ty = "int";
                else if (t->kind == TYPE_STRING) ty = "const char*";
                else if (t->kind == TYPE_INT) ty = "long long";
            } else if (s->as.let_s.value) {
                if (s->as.let_s.value->kind == EXPR_INT) ty = "long long";
                else if (s->as.let_s.value->kind == EXPR_FLOAT) ty = "double";
                else if (s->as.let_s.value->kind == EXPR_BOOL) ty = "int";
                else if (s->as.let_s.value->kind == EXPR_STRING) ty = "const char*";
                else if (s->as.let_s.value->kind == EXPR_BINARY) {
                    Expr* l = s->as.let_s.value->as.binary.left;
                    Expr* r = s->as.let_s.value->as.binary.right;
                    if ((l && l->kind == EXPR_FLOAT) || (r && r->kind == EXPR_FLOAT) ||
                        (l && l->kind == EXPR_IDENT && is_preamble_const(l->as.str_val)) ||
                        (r && r->kind == EXPR_IDENT && is_preamble_const(r->as.str_val))) {
                        ty = "double";
                    }
                } else if (s->as.let_s.value->kind == EXPR_IDENT &&
                           is_preamble_const(s->as.let_s.value->as.str_val)) {
                    ty = "double";
                }
            }
            cg_i(g);
            fprintf(g->out, "%s %s", ty, c_id(s->as.let_s.name));
            if (s->as.let_s.value) {
                fprintf(g->out, " = ");
                cg_expr(g, s->as.let_s.value);
            } else {
                fprintf(g->out, " = 0");
            }
            fprintf(g->out, ";\n");
            break;
        }
        case STMT_FN: {
            cg_ret_type(g, s->as.fn_s.ret_type, s->as.fn_s.name);
            fprintf(g->out, " %s(", c_id(s->as.fn_s.name));
            cg_params(g, s->as.fn_s.params, false);
            fprintf(g->out, ") ");
            if (s->as.fn_s.body && s->as.fn_s.body->kind == EXPR_BLOCK) {
                fprintf(g->out, "\n");
                cg_block(g, s->as.fn_s.body);
            } else if (s->as.fn_s.is_arrow && s->as.fn_s.body) {
                fprintf(g->out, "{ return ");
                cg_expr(g, s->as.fn_s.body);
                fprintf(g->out, "; }\n");
            } else {
                fprintf(g->out, "{}\n");
            }
            break;
        }
        case STMT_RETURN: {
            cg_i(g);
            fprintf(g->out, "return");
            if (s->as.expr) { fprintf(g->out, " "); cg_expr(g, s->as.expr); }
            fprintf(g->out, ";\n");
            break;
        }
        case STMT_STRUCT:
        case STMT_ENUM:
        case STMT_BEHAVE:
        case STMT_TRAIT:
        case STMT_IMPORT:
        case STMT_MODULE:
        case STMT_BRIDGE:
        case STMT_PUB:
        case STMT_IMPL:
        case STMT_CONTRACT:
        case STMT_DECORATOR:
        case STMT_TEST:
        case STMT_BENCH:
            // Skip: not translatable in subset
            cg_i(g);
            fprintf(g->out, "/* skipped stmt kind %d */\n", (int)s->kind);
            break;
        default:
            cg_i(g);
            fprintf(g->out, "/* unknown stmt */\n");
    }
}

// === Loaded module tracking (for import inlining) =========================

typedef struct LoadedMod {
    char* path;
    char* src;
    Lexer* lex;
    Program* prog;
    struct LoadedMod* next;
} LoadedMod;

static LoadedMod* loaded_mods = NULL;

static bool mod_already_loaded(const char* p) {
    for (LoadedMod* m = loaded_mods; m; m = m->next)
        if (strcmp(m->path, p) == 0) return true;
    return false;
}

static Program* cg_load_module(const char* source_path, const char* mod_path, CGen* g) {
    char resolved[2048];
    if (!fs_resolve_module(source_path, mod_path, resolved, sizeof(resolved))) {
        fprintf(g->out, "/* could not resolve import '%s' */\n", mod_path);
        return NULL;
    }
    if (mod_already_loaded(resolved)) {
        for (LoadedMod* m = loaded_mods; m; m = m->next)
            if (strcmp(m->path, resolved) == 0) return m->prog;
    }
    char* src = fs_read_file(resolved);
    if (!src) {
        fprintf(g->out, "/* could not read import '%s' */\n", resolved);
        return NULL;
    }
    Lexer* lex = (Lexer*)calloc(1, sizeof(Lexer));
    lexer_init(lex, resolved, src); lexer_tokenize(lex);
    Parser* p = (Parser*)calloc(1, sizeof(Parser));
    parser_init(p, resolved, lex->tokens, lex->count);
    Program* prog = (Program*)calloc(1, sizeof(Program));
    parser_parse(p, prog);
    if (p->had_error) {
        fprintf(stderr, "%s", p->errors);
        // leak intentionally; caller returns error
        return NULL;
    }
    LoadedMod* m = (LoadedMod*)calloc(1, sizeof(LoadedMod));
    m->path = strdup(resolved); m->src = src; m->lex = lex; m->prog = prog;
    m->next = loaded_mods; loaded_mods = m;

    // Recursively load imports of this module so forward decls cover them too.
    for (Stmt* s = prog->first; s; s = s->next) {
        if (s->kind == STMT_IMPORT) {
            cg_load_module(resolved, s->as.import_s.name, g);
        }
    }
    return prog;
}

static void cg_free_modules(void) {
    LoadedMod* nxt;
    for (LoadedMod* m = loaded_mods; m; m = nxt) {
        nxt = m->next;
        if (m->prog) { program_free(m->prog); free(m->prog); }
        if (m->lex) { lexer_free(m->lex); free(m->lex); }
        // Note: Parser is stack-copied via calloc — no parser_free.
        free(m->src); free(m->path); free(m);
    }
    loaded_mods = NULL;
}


static void cg_preamble(CGen* g) {
    fprintf(g->out,
        "// Auto-generated by Kodas C backend v%s\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdint.h>\n"
        "#include <math.h>\n"
        "#include <string.h>\n\n"
        "typedef long long i64;\n\n"
        "/* ---- Math constants (from core.math) ---- */\n"
        "#ifndef M_PI\n#define M_PI 3.14159265358979323846\n#endif\n"
        "#ifndef M_E\n#define M_E 2.7182818284590452354\n#endif\n"
        "#define k_PI  ((double)M_PI)\n"
        "#define k_E   ((double)M_E)\n"
        "#define k_TAU (2.0 * M_PI)\n"
        "#define k_DEG_TO_RAD (M_PI / 180.0)\n"
        "#define k_RAD_TO_DEG (180.0 / M_PI)\n\n"
        "/* double-precision math helpers (used via _Generic overloads below) */\n"
        "static inline double d_radians(double deg){return deg*k_DEG_TO_RAD;}\n"
        "static inline double d_degrees(double rad){return rad*k_RAD_TO_DEG;}\n"
        "static inline double d_sign_d(double x){return x>0?1.0:(x<0?-1.0:0.0);}\n"
        "static inline long long d_sign_i(long long x){return x>0?1:(x<0?-1:0);}\n"
        "static inline double d_clamp_d(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}\n"
        "static inline long long d_clamp_i(long long x,long long lo,long long hi){return x<lo?lo:(x>hi?hi:x);}\n"
        "static inline double d_lerp(double a,double b,double t){return a+(b-a)*t;}\n"
        "static inline double d_nan_d(double x){(void)x; return 0.0/0.0;}\n"
        "static inline long long d_nan_i(long long x){(void)x; return 0;}\n"
        "static inline double d_is_finite_d(double x){return fabs(x)<1e308;}\n"
        "static inline long long d_is_finite_i(long long x){(void)x; return 1;}\n"
        "/* C11 _Generic overloads so int and double calls both work */\n"
        "#define sign(X) _Generic((X), double: d_sign_d, float: d_sign_d, default: d_sign_i)(X)\n"
        "#define clamp(X,L,H) _Generic((X), double: d_clamp_d, float: d_clamp_d, default: d_clamp_i)(X,L,H)\n"
        "#define radians(X) d_radians((double)(X))\n"
        "#define degrees(X) d_degrees((double)(X))\n"
        "#define lerp(A,B,T) d_lerp((double)(A),(double)(B),(double)(T))\n"
        "#define is_nan(X) ((X)!=(X))\n"
        "#define is_finite(X) _Generic((X), double: d_is_finite_d, float: d_is_finite_d, default: d_is_finite_i)(X)\n"
        "/* Overrides for abs with integers (libm fabs takes double) */\n"
        "#ifndef __cplusplus\n"
        "static inline long long k_abs_i(long long x){return x<0?-x:x;}\n"
        "#define k_abs(X) _Generic((X), double: fabs, float: fabsf, default: k_abs_i)(X)\n"
        "#endif\n\n",
        KODAS_VERSION_STRING);
}

static void cg_emit_forward_decls(CGen* g, Program* prog) {
    for (Stmt* s = prog->first; s; s = s->next) {
        if (s->kind == STMT_FN && !is_preamble_fn(s->as.fn_s.name)) {
            cg_ret_type(g, s->as.fn_s.ret_type, s->as.fn_s.name);
            fprintf(g->out, " %s(", c_id(s->as.fn_s.name));
            cg_params(g, s->as.fn_s.params, false);
            fprintf(g->out, ");\n");
        }
    }
}

static void cg_emit_toplevel_decls(CGen* g, Program* prog, bool* out_had_main) {
    for (Stmt* s = prog->first; s; s = s->next) {
        if (s->kind == STMT_FN && strcmp(s->as.fn_s.name, "main") == 0) {
            if (out_had_main) *out_had_main = true;
        }
        // Skip libm colliding fns (sin/cos/tan/etc.) — we use libm directly.
        if (s->kind == STMT_FN && is_preamble_fn(s->as.fn_s.name)) continue;
        // Skip math constants already defined in preamble.
        if ((s->kind == STMT_LET || s->kind == STMT_SEAL) &&
            is_preamble_const(s->as.let_s.name)) continue;
        if (s->kind == STMT_FN || s->kind == STMT_LET || s->kind == STMT_SEAL) {
            cg_stmt(g, s);
            fprintf(g->out, "\n");
        }
    }
}


int codegen_c_emit_file(const char* source_path, const char* c_out_path) {
    fs_set_exe_path(source_path); // best-effort; argv0 not available here

    FILE* fin = fopen(source_path, "rb");
    if (!fin) { fprintf(stderr, "  ✗ cannot open %s: %s\n", source_path, strerror(errno)); return 1; }
    fseek(fin, 0, SEEK_END); long sz = ftell(fin); fseek(fin, 0, SEEK_SET);
    char* src = (char*)malloc(sz + 1); size_t n = fread(src, 1, sz, fin); src[n] = '\0';
    fclose(fin);

    Lexer lex; lexer_init(&lex, source_path, src); lexer_tokenize(&lex);
    Parser p; parser_init(&p, source_path, lex.tokens, lex.count);
    Program prog; parser_parse(&p, &prog);

    if (p.had_error) {
        fprintf(stderr, "%s", p.errors);
        program_free(&prog);
        lexer_free(&lex); free(src);
        return 1;
    }

    // Pre-load all imported modules (recursively). This gives us Programs to
    // inline before emitting.
    CGen g; memset(&g, 0, sizeof(g));
    g.out = NULL; // temp: no output during load
    for (Stmt* s = prog.first; s; s = s->next) {
        if (s->kind == STMT_IMPORT) {
            cg_load_module(source_path, s->as.import_s.name, &g);
        }
    }

    FILE* fout = fopen(c_out_path, "wb");
    if (!fout) { fprintf(stderr, "  ✗ cannot write %s\n", c_out_path); goto fail; }

    memset(&g, 0, sizeof(g));
    g.out = fout;

    cg_preamble(&g);

    // Forward-declare functions from all modules first (order-independent calls).
    for (LoadedMod* m = loaded_mods; m; m = m->next) cg_emit_forward_decls(&g, m->prog);
    cg_emit_forward_decls(&g, &prog);
    fprintf(g.out, "\n");

    // Emit stdlib module bodies in loaded order.
    for (LoadedMod* m = loaded_mods; m; m = m->next) {
        fprintf(g.out, "/* ---- begin module %s ---- */\n", m->path);
        cg_emit_toplevel_decls(&g, m->prog, NULL);
        fprintf(g.out, "/* ---- end module %s ---- */\n\n", m->path);
    }

    // Emit user program fn decls at file scope; LET/SEAL/EXPR go into main.
    bool had_main = false;
    for (Stmt* s = prog.first; s; s = s->next) {
        if (s->kind == STMT_FN) {
            if (strcmp(s->as.fn_s.name, "main") == 0) had_main = true;
            if (is_preamble_fn(s->as.fn_s.name)) continue;
            cg_stmt(&g, s);
            fprintf(g.out, "\n");
        }
    }

    if (!had_main) {
        fprintf(g.out, "int main(int argc, char** argv) {\n");
        fprintf(g.out, "    (void)argc; (void)argv;\n");
        g.indent += 4;
        for (Stmt* s = prog.first; s; s = s->next) {
            if (s->kind == STMT_LET || s->kind == STMT_SEAL || s->kind == STMT_EXPR) {
                cg_stmt(&g, s);
            }
        }
        g.indent -= 4;
        fprintf(g.out, "    return 0;\n}\n");
    }

    fclose(fout);
    program_free(&prog);
    lexer_free(&lex); free(src);
    int rc = g.had_error ? 1 : 0;
    cg_free_modules();
    return rc;

fail:
    program_free(&prog);
    lexer_free(&lex); free(src);
    cg_free_modules();
    return 1;
}
