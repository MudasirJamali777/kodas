/// ast.c - Kodas AST helpers
#include "ast.h"

Expr* expr_new(ExprKind kind, Location loc) {
    Expr* e = (Expr*)kodas_calloc(1, sizeof(Expr));
    e->kind = kind;
    e->loc = loc;
    return e;
}

Stmt* stmt_new(StmtKind kind, Location loc) {
    Stmt* s = (Stmt*)kodas_calloc(1, sizeof(Stmt));
    s->kind = kind;
    s->loc = loc;
    s->next = NULL;
    return s;
}

Pattern* pattern_new(PatKind kind, Location loc) {
    Pattern* p = (Pattern*)kodas_calloc(1, sizeof(Pattern));
    p->kind = kind;
    p->loc = loc;
    return p;
}

TypeNode* type_new(TypeNodeKind kind) {
    TypeNode* t = (TypeNode*)kodas_calloc(1, sizeof(TypeNode));
    t->kind = kind;
    return t;
}

void program_init(Program* prog) {
    prog->first = NULL;
    prog->last = NULL;
    prog->count = 0;
}

void program_add(Program* prog, Stmt* s) {
    if (!s) return;
    if (!prog->first) {
        prog->first = s;
        prog->last = s;
    } else {
        prog->last->next = s;
        prog->last = s;
    }
    prog->count++;
}

Expr* expr_int(int64_t val, Location loc) {
    Expr* e = expr_new(EXPR_INT, loc);
    e->as.int_val = val;
    return e;
}

Expr* expr_float(double val, Location loc) {
    Expr* e = expr_new(EXPR_FLOAT, loc);
    e->as.float_val = val;
    return e;
}

Expr* expr_bool(bool val, Location loc) {
    Expr* e = expr_new(EXPR_BOOL, loc);
    e->as.bool_val = val;
    return e;
}

Expr* expr_string(const char* val, Location loc) {
    Expr* e = expr_new(EXPR_STRING, loc);
    e->as.str_val = kodas_strdup(val);
    return e;
}

Expr* expr_ident(const char* name, Location loc) {
    Expr* e = expr_new(EXPR_IDENT, loc);
    e->as.str_val = kodas_strdup(name);
    return e;
}

Expr* expr_binary(Expr* left, const char* op, Expr* right, Location loc) {
    Expr* e = expr_new(EXPR_BINARY, loc);
    e->as.binary.left = left;
    e->as.binary.op = kodas_strdup(op);
    e->as.binary.right = right;
    return e;
}

Expr* expr_unary(const char* op, Expr* operand, Location loc) {
    Expr* e = expr_new(EXPR_UNARY, loc);
    e->as.unary.op = kodas_strdup(op);
    e->as.unary.operand = operand;
    return e;
}

Expr* expr_call(Expr* callee, ExprList args, Location loc) {
    Expr* e = expr_new(EXPR_CALL, loc);
    e->as.call.callee = callee;
    e->as.call.args = args;
    return e;
}

Expr* expr_block(StmtList stmts, Location loc) {
    Expr* e = expr_new(EXPR_BLOCK, loc);
    e->as.block.stmts = stmts;
    return e;
}

Expr* expr_if(Expr* cond, Expr* then_b, Expr* else_b, Location loc) {
    Expr* e = expr_new(EXPR_IF, loc);
    e->as.if_e.cond = cond;
    e->as.if_e.then_branch = then_b;
    e->as.if_e.else_branch = else_b;
    return e;
}

void expr_list_append(ExprList* list, Expr* e) {
    da_append(list, e);
}

void stmt_list_append(StmtList* list, Stmt* s) {
    da_append(list, s);
}

void param_list_append(ParamList* list, Param p) {
    da_append(list, p);
}

void name_list_append(NameList* list, const char* name) {
    char* copy = kodas_strdup(name);
    da_append(list, copy);
}

char* type_to_string(TypeNode* t) {
    if (!t) return kodas_strdup("?");
    char buf[512];
    switch (t->kind) {
        case TYPE_INFER: return kodas_strdup("_");
        case TYPE_INT: return kodas_strdup("Int");
        case TYPE_FLOAT: return kodas_strdup("Float");
        case TYPE_BOOL: return kodas_strdup("Bool");
        case TYPE_STRING: return kodas_strdup("String");
        case TYPE_CHAR: return kodas_strdup("Char");
        case TYPE_BYTE: return kodas_strdup("Byte");
        case TYPE_ANY: return kodas_strdup("any");
        case TYPE_NEVER: return kodas_strdup("never");
        case TYPE_NAMED: case TYPE_USER:
            return t->name ? kodas_strdup(t->name) : kodas_strdup("?");
        case TYPE_LIST:
            if (t->param_count > 0) {
                char* inner = type_to_string(t->params[0]);
                snprintf(buf, sizeof(buf), "List[%s]", inner);
                free(inner);
            } else {
                snprintf(buf, sizeof(buf), "List");
            }
            return kodas_strdup(buf);
        case TYPE_MAP:
            if (t->param_count > 1) {
                char* k = type_to_string(t->params[0]);
                char* v = type_to_string(t->params[1]);
                snprintf(buf, sizeof(buf), "Map[%s, %s]", k, v);
                free(k); free(v);
            } else snprintf(buf, sizeof(buf), "Map");
            return kodas_strdup(buf);
        case TYPE_SET:
            if (t->param_count > 0) {
                char* inner = type_to_string(t->params[0]);
                snprintf(buf, sizeof(buf), "Set[%s]", inner);
                free(inner);
            } else snprintf(buf, sizeof(buf), "Set");
            return kodas_strdup(buf);
        case TYPE_TUPLE: {
            char* s = kodas_strdup("Tuple(");
            size_t len = strlen(s);
            for (int i = 0; i < t->param_count && (int)len < 500; i++) {
                char* inner = type_to_string(t->params[i]);
                size_t il = strlen(inner);
                if (i > 0) { s[len++] = ','; s[len++] = ' '; s[len] = '\0'; }
                memcpy(s + len, inner, il);
                len += il;
                s[len] = '\0';
                free(inner);
                s = kodas_realloc(s, len + 2);
            }
            s[len++] = ')'; s[len] = '\0';
            return s;
        }
        case TYPE_MAYBE:
            if (t->param_count > 0) {
                char* inner = type_to_string(t->params[0]);
                snprintf(buf, sizeof(buf), "maybe %s", inner);
                free(inner);
            } else snprintf(buf, sizeof(buf), "maybe _");
            return kodas_strdup(buf);
        case TYPE_RESULT:
            if (t->param_count > 1) {
                char* ok_t = type_to_string(t->params[0]);
                char* err_t = type_to_string(t->params[1]);
                snprintf(buf, sizeof(buf), "Result[%s, %s]", ok_t, err_t);
                free(ok_t); free(err_t);
            } else snprintf(buf, sizeof(buf), "Result");
            return kodas_strdup(buf);
        case TYPE_FUNC:
            snprintf(buf, sizeof(buf), "fn");
            return kodas_strdup(buf);
        case TYPE_CHANNEL:
            if (t->param_count > 0) {
                char* inner = type_to_string(t->params[0]);
                snprintf(buf, sizeof(buf), "Channel[%s]", inner);
                free(inner);
            } else snprintf(buf, sizeof(buf), "Channel");
            return kodas_strdup(buf);
        case TYPE_GENERIC:
            return t->name ? kodas_strdup(t->name) : kodas_strdup("T");
    }
    return kodas_strdup("?");
}

void pattern_free(Pattern* p) {
    if (!p) return;
    for (int i = 0; i < p->sub_count; i++) pattern_free(p->sub[i]);
    free(p->sub);
    for (int i = 0; i < p->field_count; i++) {
        free(p->field_names[i]);
        pattern_free(p->field_pats[i]);
    }
    free(p->field_names);
    free(p->field_pats);
    free(p->name);
    expr_free(p->literal);
    expr_free(p->guard);
    free(p);
}

void type_free(TypeNode* t) {
    if (!t) return;
    free(t->name);
    for (int i = 0; i < t->param_count; i++) type_free(t->params[i]);
    free(t->params);
    type_free(t->ret);
    free(t);
}

void expr_list_free(ExprList* l) {
    for (int i = 0; i < l->count; i++) expr_free(l->items[i]);
    da_free(l);
}

void stmt_list_free(StmtList* l) {
    for (int i = 0; i < l->count; i++) stmt_free(l->items[i]);
    da_free(l);
}

void expr_free(Expr* e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_INT: case EXPR_FLOAT: case EXPR_BOOL: case EXPR_CHAR: case EXPR_EMPTY: case EXPR_SELF:
            break;
        case EXPR_STRING: case EXPR_IDENT:
            free(e->as.str_val); break;
        case EXPR_INTERP_STRING:
            for (int i = 0; i < e->as.interp.count; i++) expr_free(e->as.interp.parts[i]);
            free(e->as.interp.parts); break;
        case EXPR_BINARY:
            free(e->as.binary.op);
            expr_free(e->as.binary.left);
            expr_free(e->as.binary.right); break;
        case EXPR_UNARY:
            free(e->as.unary.op); expr_free(e->as.unary.operand); break;
        case EXPR_INDEX:
            expr_free(e->as.index.collection); expr_free(e->as.index.index); break;
        case EXPR_FIELD:
            expr_free(e->as.field.object); free(e->as.field.field); break;
        case EXPR_CALL:
            expr_free(e->as.call.callee);
            for (int i = 0; i < e->as.call.args.count; i++) expr_free(e->as.call.args.items[i]);
            free(e->as.call.args.items); break;
        case EXPR_METHOD_CALL:
            expr_free(e->as.method_call.object);
            free(e->as.method_call.method);
            for (int i = 0; i < e->as.method_call.args.count; i++) expr_free(e->as.method_call.args.items[i]);
            free(e->as.method_call.args.items); break;
        case EXPR_LET:
            free(e->as.let.name); type_free(e->as.let.type); expr_free(e->as.let.value); break;
        case EXPR_SEAL:
            free(e->as.assign.name); expr_free(e->as.assign.value); break;
        case EXPR_ASSIGN:
            free(e->as.assign.name); expr_free(e->as.assign.value); break;
        case EXPR_COMPOUND_ASSIGN:
            expr_free(e->as.compound.target); free(e->as.compound.op); expr_free(e->as.compound.value); break;
        case EXPR_IF:
            expr_free(e->as.if_e.cond); expr_free(e->as.if_e.then_branch); expr_free(e->as.if_e.else_branch); break;
        case EXPR_MATCH: {
            expr_free(e->as.match_e.obj);
            for (int i = 0; i < e->as.match_e.arm_count; i++) {
                pattern_free(e->as.match_e.arms[i].pat);
                expr_free(e->as.match_e.arms[i].body);
            }
            free(e->as.match_e.arms);
            break;
        }
        case EXPR_FN: case EXPR_LAMBDA:
            for (int i = 0; i < e->as.fn_e.params.count; i++) {
                free(e->as.fn_e.params.items[i].name);
                free(e->as.fn_e.params.items[i].type_ann);
                type_free(e->as.fn_e.params.items[i].type_ann ? NULL : NULL);
            }
            free(e->as.fn_e.params.items);
            type_free(e->as.fn_e.ret_type);
            expr_free(e->as.fn_e.body);
            free(e->as.fn_e.name); break;
        case EXPR_BLOCK:
            stmt_list_free(&e->as.block.stmts); break;
        case EXPR_PIPE: case EXPR_ERR_PIPE:
            expr_free(e->as.pipe.callee);
            for (int i = 0; i < e->as.pipe.pipe_count; i++) expr_free(e->as.pipe.pipelines[i]);
            free(e->as.pipe.pipelines); break;
        case EXPR_LOOP:
            expr_free(e->as.loop_e.count); expr_free(e->as.loop_e.body); break;
        case EXPR_LOOP_IN: case EXPR_WHILE:
            free(e->as.loop_in.var); free(e->as.loop_in.idx_var);
            expr_free(e->as.loop_in.iter); expr_free(e->as.loop_in.body); expr_free(e->as.loop_in.cond); break;
        case EXPR_STOP: case EXPR_SKIP: case EXPR_GIVE: case EXPR_SHOW: case EXPR_ASSERT:
            expr_free(e->as.expr); break;
        case EXPR_STRUCT_INIT: case EXPR_STRUCT_LIT: {
            free(e->as.struct_init.name);
            for (int i = 0; i < e->as.struct_init.field_count; i++) {
                free(e->as.struct_init.field_names[i]);
                expr_free(e->as.struct_init.field_vals[i]);
            }
            free(e->as.struct_init.field_names);
            free(e->as.struct_init.field_vals);
            break;
        }
        case EXPR_LIST: case EXPR_TUPLE: case EXPR_SET:
            expr_list_free((ExprList*)&e->as.call.args); break;
        case EXPR_MAP: {
            ExprList keys = {0}, vals = {0};
            expr_list_free(&keys); expr_list_free(&vals); break;
        }
        case EXPR_RANGE: case EXPR_RANGE_INCL:
            expr_free(e->as.binary.left); expr_free(e->as.binary.right); break;
        case EXPR_OK: case EXPR_ERR:
            expr_free(e->as.result_e.value); break;
        case EXPR_THEN_ELSE:
            expr_free(e->as.then_else.cond); expr_free(e->as.then_else.then_val); expr_free(e->as.then_else.else_val); break;
        case EXPR_FIELD_ASSIGN:
            expr_free(e->as.field_assign.object); free(e->as.field_assign.field); expr_free(e->as.field_assign.value); break;
        case EXPR_LIST_PUSH:
        case EXPR_SPREAD:
        case EXPR_ARRAY_COMP:
        case EXPR_LAMBDA_SHORT:
            break;
    }
    free(e);
}

void stmt_free(Stmt* s) {
    if (!s) return;
    switch (s->kind) {
        case STMT_EXPR:
        case STMT_RETURN:
            expr_free(s->as.expr); break;
        case STMT_LET:
            free(s->as.let_s.name);
            type_free(s->as.let_s.type);
            expr_free(s->as.let_s.value);
            break;
        case STMT_SEAL:
            free(s->as.seal_s.name);
            break;
        case STMT_FN: {
            free(s->as.fn_s.name);
            for (int i = 0; i < s->as.fn_s.params.count; i++) {
                free(s->as.fn_s.params.items[i].name);
                free(s->as.fn_s.params.items[i].type_ann);
            }
            free(s->as.fn_s.params.items);
            type_free(s->as.fn_s.ret_type);
            expr_free(s->as.fn_s.body);
            for (int i = 0; i < s->as.fn_s.generics.count; i++) {
                free(s->as.fn_s.generics.items[i].name);
                for (int j = 0; j < s->as.fn_s.generics.items[i].bound_count; j++)
                    free(s->as.fn_s.generics.items[i].bounds[j]);
                free(s->as.fn_s.generics.items[i].bounds);
            }
            free(s->as.fn_s.generics.items);
            free(s->as.fn_s.op_name);
            break;
        }
        case STMT_STRUCT: {
            free(s->as.struct_s.name);
            for (int i = 0; i < s->as.struct_s.fields.count; i++) {
                free(s->as.struct_s.fields.items[i].name);
                type_free(s->as.struct_s.fields.items[i].type);
                expr_free(s->as.struct_s.fields.items[i].default_val);
            }
            free(s->as.struct_s.fields.items);
            for (int i = 0; i < s->as.struct_s.generics.count; i++) {
                free(s->as.struct_s.generics.items[i].name);
            }
            free(s->as.struct_s.generics.items);
            break;
        }
        case STMT_ENUM: {
            free(s->as.enum_s.name);
            for (int i = 0; i < s->as.enum_s.variants.count; i++) {
                free(s->as.enum_s.variants.items[i].name);
            }
            free(s->as.enum_s.variants.items);
            break;
        }
        case STMT_BEHAVE: {
            free(s->as.behave_s.type_name);
            for (int i = 0; i < s->as.behave_s.trait_count; i++) free(s->as.behave_s.traits[i]);
            free(s->as.behave_s.traits);
            for (int i = 0; i < s->as.behave_s.methods.count; i++) {
                FnDecl* m = &s->as.behave_s.methods.items[i];
                free(m->name);
                for (int j = 0; j < m->params.count; j++) {
                    free(m->params.items[j].name);
                    free(m->params.items[j].type_ann);
                }
                free(m->params.items);
                type_free(m->ret_type);
                expr_free(m->body);
            }
            free(s->as.behave_s.methods.items);
            break;
        }
        case STMT_TRAIT: {
            free(s->as.trait_s.name);
            for (int i = 0; i < s->as.trait_s.method_count; i++) {
                TraitMethod* m = &s->as.trait_s.methods[i];
                free(m->name);
                for (int j = 0; j < m->params.count; j++) {
                    free(m->params.items[j].name);
                    free(m->params.items[j].type_ann);
                }
                free(m->params.items);
                type_free(m->ret_type);
                expr_free(m->default_body);
            }
            free(s->as.trait_s.methods);
            break;
        }
        case STMT_IMPORT: {
            free(s->as.import_s.name);
            for (int i = 0; i < s->as.import_s.imports.count; i++)
                free(s->as.import_s.imports.items[i]);
            free(s->as.import_s.imports.items);
            break;
        }
        case STMT_MODULE:
            free(s->as.module_s.name); break;
        case STMT_BRIDGE: {
            free(s->as.bridge_s.lang);
            for (int i = 0; i < s->as.bridge_s.stmts.count; i++) stmt_free(s->as.bridge_s.stmts.items[i]);
            free(s->as.bridge_s.stmts.items);
            break;
        }
        case STMT_TEST: case STMT_BENCH: {
            free(s->as.test_s.name);
            expr_free(s->as.test_s.body);
            for (int i = 0; i < s->as.test_s.bench_stmts.count; i++) expr_free(s->as.test_s.bench_stmts.items[i]);
            free(s->as.test_s.bench_stmts.items);
            break;
        }
        case STMT_CONTRACT: {
            for (int i = 0; i < s->as.contract_s.decls.count; i++) expr_free(s->as.contract_s.decls.items[i]);
            free(s->as.contract_s.decls.items);
            break;
        }
        case STMT_DECORATOR: {
            free(s->as.decorator_s.name);
            for (int i = 0; i < s->as.decorator_s.args.count; i++) expr_free(s->as.decorator_s.args.items[i]);
            free(s->as.decorator_s.args.items);
            stmt_free(s->as.decorator_s.stmt);
            break;
        }
        case STMT_PUB:
        case STMT_IMPL:
            break;
    }
    stmt_free(s->next);
    free(s);
}

void program_free(Program* prog) {
    stmt_free(prog->first);
    prog->first = NULL;
    prog->last = NULL;
    prog->count = 0;
}
