/// eval.c - Kodas tree-walking interpreter
#include "eval.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

extern double current_time_seconds_impl(void);

// ── Native constructors (struct / enum variant) ────────────────────────
// These are stored as VAL_NATIVE with ctx pointing to a null-terminated name
// string (type name for structs, "Enum.Variant" for variants).

static Value native_struct_ctor(int argc, Value* argv, void* ctx) {
    const char* type_name = (const char*)ctx;
    Value inst = val_struct(type_name);
    // Positional constructor: User(name, age) - we don't have param->field mapping
    // but the EXPR_STRUCT_INIT syntax User{name:"x", age:1} handles named init.
    // So this just returns a zeroed struct (used for User.new() static methods).
    (void)argc; (void)argv;
    return inst;
}

static Value native_enum_variant_ctor(int argc, Value* argv, void* ctx) {
    // ctx is a string "EnumName.VariantName"
    const char* full = (const char*)ctx;
    const char* dot = strchr(full, '.');
    char enum_name[128] = {0};
    char var_name[128] = {0};
    if (dot) {
        size_t el = (size_t)(dot - full);
        memcpy(enum_name, full, el);
        strncpy(var_name, dot + 1, sizeof(var_name) - 1);
    } else {
        strncpy(var_name, full, sizeof(var_name) - 1);
    }
    if (argc == 0) return val_enum_variant(enum_name, var_name, NULL);
    if (argc == 1) return val_enum_variant(enum_name, var_name, &argv[0]);
    // Multi-payload -> wrap in tuple
    Value tup = val_tuple(argv, argc);
    Value v = val_enum_variant(enum_name, var_name, &tup);
    value_free(&tup);
    return v;
}

static void vparams_from_params(VParamList* dst, ParamList* src) {
    memset(dst, 0, sizeof(VParamList));
    for (int i = 0; i < src->count; i++) {
        VParam p = {0};
        p.name = src->items[i].name ? kodas_strdup(src->items[i].name) : NULL;
        p.type_ann = src->items[i].type_ann ? kodas_strdup(src->items[i].type_ann) : NULL;
        p.default_value = src->items[i].default_value;
        p.is_mut = src->items[i].is_mut;
        p.is_self = src->items[i].is_self;
        if (dst->count >= dst->capacity) {
            dst->capacity = dst->capacity == 0 ? 8 : dst->capacity * 2;
            dst->items = (VParam*)kodas_realloc(dst->items, dst->capacity * sizeof(VParam));
        }
        dst->items[dst->count++] = p;
    }
}

void eval_init(Evaluator* ev) {
    memset(ev, 0, sizeof(Evaluator));
    ev->globals = env_new(NULL);
    env_register_builtins(ev->globals);
    gc_init();
}

void eval_free(Evaluator* ev) {
    env_free(ev->globals);
    gc_shutdown();
}

static void runtime_error(Evaluator* ev, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev->error_msg, sizeof(ev->error_msg), fmt, ap);
    va_end(ap);
    ev->had_error = true;
    fprintf(stderr, "  ✗ runtime error: %s\n", ev->error_msg);
}

static Value eval_call(Evaluator* ev, Environment* env, Value callee, int argc, Value* argv);
static bool pattern_match(Evaluator* ev, Environment* env, Pattern* pat, Value val, Environment* binds);

static Value numeric_binary(Evaluator* ev, Value a, const char* op, Value b) {
    (void)ev;
    if (strcmp(op, "+") == 0) return value_add(a, b);
    if (strcmp(op, "-") == 0) return value_sub(a, b);
    if (strcmp(op, "*") == 0) return value_mul(a, b);
    if (strcmp(op, "/") == 0) return value_div(a, b);
    if (strcmp(op, "%") == 0) return value_mod(a, b);

    bool eq;
    int cmp = -2;

    // Equality / comparison
    if (strcmp(op, "==") == 0) return val_bool(value_equals(a, b));
    if (strcmp(op, "!=") == 0) return val_bool(!value_equals(a, b));
    if (strcmp(op, "<>") == 0) return val_bool(!value_equals(a, b));

    bool num_ok = (a.kind == VAL_INT || a.kind == VAL_FLOAT) &&
                  (b.kind == VAL_INT || b.kind == VAL_FLOAT);
    if (num_ok) {
        cmp = value_compare(a, b);
        if (strcmp(op, "<") == 0) eq = (cmp < 0);
        else if (strcmp(op, ">") == 0) eq = (cmp > 0);
        else if (strcmp(op, "<=") == 0) eq = (cmp <= 0);
        else if (strcmp(op, ">=") == 0) eq = (cmp >= 0);
        else return val_err(val_string("unknown operator"));
        return val_bool(eq);
    }

    // String/other
    if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
        int c = strcmp(a.as.str->data, b.as.str->data);
        if (strcmp(op, "<") == 0) return val_bool(c < 0);
        if (strcmp(op, ">") == 0) return val_bool(c > 0);
        if (strcmp(op, "<=") == 0) return val_bool(c <= 0);
        if (strcmp(op, ">=") == 0) return val_bool(c >= 0);
    }

    if (strcmp(op, "&&") == 0 || strcmp(op, "and") == 0) {
        return val_bool(value_truthy(a) && value_truthy(b));
    }
    if (strcmp(op, "||") == 0 || strcmp(op, "or") == 0) {
        return val_bool(value_truthy(a) || value_truthy(b));
    }
    return val_err(val_string("type mismatch in binary operation"));
}

static Value eval_interp_string(Evaluator* ev, Environment* env, Expr* e) {
    char* result = kodas_strdup("");
    for (int i = 0; i < e->as.interp.count; i++) {
        Value v = eval_expr(ev, env, e->as.interp.parts[i]);
        if (ev->had_error) { free(result); return val_null(); }
        char* s = value_to_string(v);
        size_t lr = strlen(result), ls = strlen(s);
        result = kodas_realloc(result, lr + ls + 1);
        memcpy(result + lr, s, ls);
        result[lr + ls] = '\0';
        free(s);
    }
    return val_string_take(result);
}

static Value make_list_from_range(int64_t lo, int64_t hi, bool inclusive) {
    Value list = val_list();
    int64_t end = inclusive ? hi + 1 : hi;
    for (int64_t i = lo; i < end; i++) list_push(&list, val_int(i));
    return list;
}

Value eval_expr(Evaluator* ev, Environment* env, Expr* e) {
    if (!e) return val_null();
    if (ev->had_error || ev->had_return || ev->had_stop || ev->had_skip) return val_null();

    switch (e->kind) {
        case EXPR_INT: return val_int(e->as.int_val);
        case EXPR_FLOAT: return val_float(e->as.float_val);
        case EXPR_BOOL: return val_bool(e->as.bool_val);
        case EXPR_STRING: return e->as.str_val ? val_string(e->as.str_val) : val_string("");
        case EXPR_CHAR: return val_char(e->as.char_val);
        case EXPR_EMPTY: return val_empty();
        case EXPR_SELF: {
            Value* v = env_lookup(env, "self");
            return v ? value_copy(*v) : val_null();
        }

        case EXPR_INTERP_STRING: return eval_interp_string(ev, env, e);

        case EXPR_IDENT: {
            Value* v = env_lookup(env, e->as.str_val);
            if (!v) {
                runtime_error(ev, "undefined variable '%s'", e->as.str_val);
                return val_null();
            }
            return value_copy(*v);
        }

        case EXPR_BINARY: {
            // Handle otherwise / demand before normal binary evaluation
            if (e->as.binary.op && strcmp(e->as.binary.op, "demand") == 0) {
                Value v = eval_expr(ev, env, e->as.binary.left);
                if (ev->had_error) return val_null();
                bool is_bad = false;
                if (v.kind == VAL_RESULT && v.is_error) is_bad = true;
                if (v.kind == VAL_MAYBE && v.as.wrapped == NULL) is_bad = true;
                if (is_bad) {
                    runtime_error(ev, "demand: value is error or empty");
                    value_free(&v);
                    return val_null();
                }
                // Unwrap: return wrapped value if Result-ok or Maybe-some
                if ((v.kind == VAL_RESULT && !v.is_error && v.as.wrapped) ||
                    (v.kind == VAL_MAYBE && v.as.wrapped)) {
                    Value inner = value_copy(*v.as.wrapped);
                    value_free(&v);
                    return inner;
                }
                return v;
            }
            if (e->as.binary.op && strcmp(e->as.binary.op, "otherwise") == 0) {
                Value v = eval_expr(ev, env, e->as.binary.left);
                if (ev->had_error) { ev->had_error = false; ev->error_msg[0] = 0; value_free(&v); return eval_expr(ev, env, e->as.binary.right); }
                bool is_bad = false;
                if (v.kind == VAL_RESULT && v.is_error) is_bad = true;
                if (v.kind == VAL_MAYBE && v.as.wrapped == NULL) is_bad = true;
                if (is_bad) {
                    value_free(&v);
                    return eval_expr(ev, env, e->as.binary.right);
                }
                // If ok-result or some-maybe, unwrap
                if (((v.kind == VAL_RESULT && !v.is_error) || v.kind == VAL_MAYBE) && v.as.wrapped) {
                    Value inner = value_copy(*v.as.wrapped);
                    value_free(&v);
                    return inner;
                }
                return v;
            }

            Value left = eval_expr(ev, env, e->as.binary.left);
            if (ev->had_error) return val_null();
            // Short-circuit
            if (strcmp(e->as.binary.op, "&&") == 0 || strcmp(e->as.binary.op, "and") == 0) {
                if (!value_truthy(left)) return val_bool(false);
            }
            if (strcmp(e->as.binary.op, "||") == 0 || strcmp(e->as.binary.op, "or") == 0) {
                if (value_truthy(left)) return val_bool(true);
            }
            Value right = eval_expr(ev, env, e->as.binary.right);
            if (ev->had_error) return val_null();
            Value result = numeric_binary(ev, left, e->as.binary.op, right);
            value_free(&left); value_free(&right);
            return result;
        }

        case EXPR_UNARY: {
            Value operand = eval_expr(ev, env, e->as.unary.operand);
            if (ev->had_error) return val_null();
            Value result;
            if (strcmp(e->as.unary.op, "!") == 0 || strcmp(e->as.unary.op, "not") == 0) {
                result = value_not(operand);
            } else if (strcmp(e->as.unary.op, "-") == 0) {
                result = value_neg(operand);
            } else {
                result = operand;
            }
            value_free(&operand);
            return result;
        }

        case EXPR_LIST: {
            Value list = val_list();
            for (int i = 0; i < e->as.call.args.count; i++) {
                Value v = eval_expr(ev, env, e->as.call.args.items[i]);
                if (ev->had_error) { value_free(&list); return val_null(); }
                list_push(&list, v);
            }
            return list;
        }

        case EXPR_TUPLE: {
            Value* items = kodas_calloc(e->as.call.args.count > 0 ? e->as.call.args.count : 1, sizeof(Value));
            int cnt = e->as.call.args.count;
            for (int i = 0; i < cnt; i++) {
                items[i] = eval_expr(ev, env, e->as.call.args.items[i]);
                if (ev->had_error) {
                    for (int j = 0; j <= i; j++) value_free(&items[j]);
                    free(items); return val_null();
                }
            }
            Value v = val_tuple(items, cnt);
            free(items);
            return v;
        }

        case EXPR_MAP: {
            Value m = val_map();
            // Parse: {key: val, ...} - we stored key/val pairs differently but
            // since our parser doesn't separate them well, this handles basic map literals
            return m;
        }

        case EXPR_RANGE: case EXPR_RANGE_INCL: {
            Value lo_v = eval_expr(ev, env, e->as.binary.left);
            Value hi_v = eval_expr(ev, env, e->as.binary.right);
            if (ev->had_error) return val_null();
            bool incl = (e->kind == EXPR_RANGE_INCL);
            if (lo_v.kind == VAL_INT && hi_v.kind == VAL_INT) {
                Value list = make_list_from_range(lo_v.as.int_val, hi_v.as.int_val, incl);
                value_free(&lo_v); value_free(&hi_v);
                return list;
            }
            value_free(&lo_v); value_free(&hi_v);
            return val_list();
        }

        case EXPR_INDEX: {
            Value col = eval_expr(ev, env, e->as.index.collection);
            Value idx = eval_expr(ev, env, e->as.index.index);
            if (ev->had_error) return val_null();
            Value result;
            if (col.kind == VAL_LIST || col.kind == VAL_TUPLE) {
                int i = (int)(idx.kind == VAL_INT ? idx.as.int_val : (int64_t)idx.as.float_val);
                result = list_get(col, i);
                result = value_copy(result);
            } else if (col.kind == VAL_STRING && idx.kind == VAL_INT) {
                int i = (int)idx.as.int_val;
                int len = (int)strlen(col.as.str->data);
                if (i < 0) i += len;
                if (i < 0 || i >= len) result = val_string("");
                else {
                    char buf[2] = {col.as.str->data[i], 0};
                    result = val_string(buf);
                }
            } else if (col.kind == VAL_MAP && idx.kind == VAL_STRING) {
                Value* v = map_get(col, idx.as.str->data);
                result = v ? value_copy(*v) : val_empty();
            } else {
                result = val_null();
            }
            value_free(&col); value_free(&idx);
            return result;
        }

        case EXPR_FIELD: {
            Value obj = eval_expr(ev, env, e->as.field.object);
            if (ev->had_error) return val_null();
            Value result;
            if (obj.kind == VAL_STRUCT_INST) {
                Value* f = struct_get(obj, e->as.field.field);
                result = f ? value_copy(*f) : val_null();
            } else if (obj.kind == VAL_MAP) {
                Value* f = map_get(obj, e->as.field.field);
                result = f ? value_copy(*f) : val_empty();
            } else if (obj.kind == VAL_STRING) {
                // String methods via builtin
                if (strcmp(e->as.field.field, "len") == 0) {
                    result = val_int((int64_t)strlen(obj.as.str->data));
                } else {
                    result = val_null();
                }
            } else if (obj.kind == VAL_LIST) {
                if (strcmp(e->as.field.field, "len") == 0) {
                    result = val_int((int64_t)obj.as.list->count);
                } else {
                    result = val_null();
                }
            } else {
                result = val_null();
            }
            value_free(&obj);
            return result;
        }

        case EXPR_CALL: {
            Value callee = eval_expr(ev, env, e->as.call.callee);
            if (ev->had_error) return val_null();
            Value* argv = kodas_calloc(e->as.call.args.count > 0 ? e->as.call.args.count : 1, sizeof(Value));
            int argc = e->as.call.args.count;
            for (int i = 0; i < argc; i++) {
                argv[i] = eval_expr(ev, env, e->as.call.args.items[i]);
                if (ev->had_error) {
                    for (int j = 0; j <= i; j++) value_free(&argv[j]);
                    free(argv); value_free(&callee);
                    return val_null();
                }
            }
            Value result = eval_call(ev, env, callee, argc, argv);
            for (int i = 0; i < argc; i++) value_free(&argv[i]);
            free(argv);
            value_free(&callee);
            return result;
        }

        case EXPR_METHOD_CALL: {
            Value obj = eval_expr(ev, env, e->as.method_call.object);
            if (ev->had_error) return val_null();
            int argc = e->as.method_call.args.count;

            // Determine the type name and whether this is a static-style call
            const char* type_name = NULL;
            bool is_type_ref = false;
            if (obj.kind == VAL_STRUCT_INST) {
                type_name = obj.as.struct_inst->type_name;
            } else if (obj.kind == VAL_NATIVE && obj.as.native.ctx) {
                // Static call: e.g. Point.make(...) — obj is the native struct/enum ctor,
                // whose ctx is the type name string "Point" or "Shape".
                type_name = (const char*)obj.as.native.ctx;
                is_type_ref = true;
            }

            // Resolve callee
            Value callee = val_null();
            bool found = false;
            char method_key[256];
            if (type_name) {
                snprintf(method_key, sizeof(method_key), "%s.%s", type_name, e->as.method_call.method);
                Value* fn = env_lookup(ev->globals, method_key);
                if (fn) { callee = value_copy(*fn); found = true; }
            }
            if (!found) {
                Value* fn = env_lookup(ev->globals, e->as.method_call.method);
                if (fn) { callee = value_copy(*fn); found = true; }
            }

            // Build default argv: [obj, arg1, arg2, ...] for instance builtins
            Value* argv = kodas_calloc(argc + 2, sizeof(Value));
            argv[0] = value_copy(obj);
            bool args_ok = true;
            for (int i = 0; i < argc; i++) {
                argv[i+1] = eval_expr(ev, env, e->as.method_call.args.items[i]);
                if (ev->had_error) { args_ok = false; break; }
            }
            if (!args_ok) {
                for (int i = 0; i <= argc; i++) value_free(&argv[i]);
                free(argv); value_free(&obj); value_free(&callee);
                return val_null();
            }

            Value result;
            if (found) {
                bool method_has_self = false;
                if (callee.kind == VAL_CLOSURE && callee.as.closure->params.count > 0) {
                    method_has_self = callee.as.closure->params.items[0].is_self;
                }
                int call_argc;
                Value* call_argv;
                if (is_type_ref) {
                    // Static call: args are argv[1..argc] (skip argv[0]=obj)
                    call_argc = argc;
                    call_argv = argv + 1;
                } else if (method_has_self) {
                    // Instance method with self: argv[0..argc]
                    call_argc = argc + 1;
                    call_argv = argv;
                } else {
                    // Instance value but no self param — skip obj
                    call_argc = argc;
                    call_argv = argv + 1;
                }
                result = eval_call(ev, env, callee, call_argc, call_argv);
            } else {
                // Try builtin functions as methods (e.g. xs.push(4) -> push(xs, 4))
                Value* bf = env_lookup(ev->globals, e->as.method_call.method);
                if (bf && bf->kind == VAL_NATIVE) {
                    result = eval_call(ev, env, value_copy(*bf), argc + 1, argv);
                } else if (obj.kind == VAL_LIST) {
                    // High-order list methods that need closures
                    const char* m = e->as.method_call.method;
                    if (strcmp(m, "map") == 0 && argc >= 1) {
                        Value mapped = val_list();
                        for (int i = 0; i < obj.as.list->count; i++) {
                            Value arg = value_copy(obj.as.list->items[i]);
                            Value item = eval_call(ev, env, argv[1], 1, &arg);
                            value_free(&arg);
                            list_push(&mapped, item);
                        }
                        result = mapped;
                    } else if (strcmp(m, "filter") == 0 && argc >= 1) {
                        Value filtered = val_list();
                        for (int i = 0; i < obj.as.list->count; i++) {
                            Value arg = value_copy(obj.as.list->items[i]);
                            Value keep = eval_call(ev, env, argv[1], 1, &arg);
                            if (value_truthy(keep)) list_push(&filtered, value_copy(obj.as.list->items[i]));
                            value_free(&arg); value_free(&keep);
                        }
                        result = filtered;
                    } else if (strcmp(m, "reduce") == 0 && argc >= 1) {
                        Value acc = argc >= 2 ? value_copy(argv[2]) : value_copy(obj.as.list->items[0]);
                        int start = argc >= 2 ? 0 : 1;
                        for (int i = start; i < obj.as.list->count; i++) {
                            Value args2[2] = {acc, value_copy(obj.as.list->items[i])};
                            Value new_acc = eval_call(ev, env, argv[1], 2, args2);
                            acc = new_acc;
                            value_free(&args2[1]);
                        }
                        result = acc;
                    } else if (strcmp(m, "join") == 0 && argc >= 1) {
                        const char* sep = argv[1].kind == VAL_STRING ? argv[1].as.str->data : "";
                        size_t cap = 64, len = 0;
                        char* buf = kodas_malloc(cap); buf[0] = '\0';
                        for (int i = 0; i < obj.as.list->count; i++) {
                            if (i > 0) {
                                size_t sl = strlen(sep);
                                while (len + sl + 1 > cap) { cap *= 2; buf = kodas_realloc(buf, cap); }
                                memcpy(buf + len, sep, sl); len += sl; buf[len] = '\0';
                            }
                            char* s = value_to_string(obj.as.list->items[i]);
                            size_t sl = strlen(s);
                            while (len + sl + 1 > cap) { cap *= 2; buf = kodas_realloc(buf, cap); }
                            memcpy(buf + len, s, sl); len += sl; buf[len] = '\0';
                            free(s);
                        }
                        result = val_string_take(buf);
                    } else if (strcmp(m, "len") == 0) {
                        result = val_int((int64_t)obj.as.list->count);
                    } else {
                        runtime_error(ev, "unknown list method '%s'", m);
                        result = val_null();
                    }
                } else if (obj.kind == VAL_STRING) {
                    if (strcmp(e->as.method_call.method, "len") == 0) {
                        result = val_int((int64_t)strlen(obj.as.str->data));
                    } else if (strcmp(e->as.method_call.method, "trim") == 0) {
                        // use builtin trim function with 1 arg
                        Value* tfn = env_lookup(ev->globals, "trim");
                        if (tfn) { result = eval_call(ev, env, value_copy(*tfn), 1, &obj); }
                        else result = value_copy(obj);
                    } else if (strcmp(e->as.method_call.method, "split") == 0 && argc >= 1) {
                        Value* sfn = env_lookup(ev->globals, "split");
                        if (sfn) {
                            Value args[2] = {obj, argv[1]};
                            result = eval_call(ev, env, value_copy(*sfn), 2, args);
                        }
                        else result = val_list();
                    } else if (strcmp(e->as.method_call.method, "contains") == 0 && argc >= 1) {
                        bool found_it = (argv[1].kind == VAL_STRING) &&
                            (strstr(obj.as.str->data, argv[1].as.str->data) != NULL);
                        result = val_bool(found_it);
                    } else if (strcmp(e->as.method_call.method, "replace") == 0 && argc >= 2) {
                        Value* rfn = env_lookup(ev->globals, "replace");
                        if (rfn) {
                            Value args[3] = {obj, argv[1], argv[2]};
                            result = eval_call(ev, env, value_copy(*rfn), 3, args);
                        }
                        else result = value_copy(obj);
                    } else {
                        runtime_error(ev, "unknown string method '%s'", e->as.method_call.method);
                        result = val_null();
                    }
                } else if (obj.kind == VAL_MAP) {
                    const char* m = e->as.method_call.method;
                    if (strcmp(m, "len") == 0) result = val_int((int64_t)obj.as.map->count);
                    else if (strcmp(m, "has") == 0 && argc >= 1) {
                        result = val_bool(argv[1].kind == VAL_STRING && map_has(obj, argv[1].as.str->data));
                    } else if (strcmp(m, "get") == 0 && argc >= 1) {
                        if (argv[1].kind == VAL_STRING) {
                            Value* v = map_get(obj, argv[1].as.str->data);
                            result = v ? value_copy(*v) : val_empty();
                        } else result = val_empty();
                    } else {
                        runtime_error(ev, "unknown map method '%s'", m);
                        result = val_null();
                    }
                } else if (obj.kind == VAL_RESULT) {
                    if (strcmp(e->as.method_call.method, "is_ok") == 0) result = val_bool(!obj.is_error);
                    else if (strcmp(e->as.method_call.method, "is_err") == 0) result = val_bool(obj.is_error);
                    else if (strcmp(e->as.method_call.method, "unwrap") == 0) {
                        if (obj.is_error) { runtime_error(ev, "called unwrap on err"); result = val_null(); }
                        else result = obj.as.wrapped ? value_copy(*obj.as.wrapped) : val_null();
                    } else { result = val_null(); }
                } else if (obj.kind == VAL_MAYBE) {
                    if (strcmp(e->as.method_call.method, "is_empty") == 0) result = val_bool(obj.as.wrapped == NULL);
                    else if (strcmp(e->as.method_call.method, "unwrap") == 0) {
                        if (!obj.as.wrapped) { runtime_error(ev, "called unwrap on empty"); result = val_null(); }
                        else result = value_copy(*obj.as.wrapped);
                    } else if (strcmp(e->as.method_call.method, "unwrap_or") == 0 && argc >= 1) {
                        result = obj.as.wrapped ? value_copy(*obj.as.wrapped) : value_copy(argv[1]);
                    } else { result = val_null(); }
                } else {
                    runtime_error(ev, "cannot call method '%s' on this value", e->as.method_call.method);
                    result = val_null();
                }
            }
            for (int i = 0; i <= argc; i++) value_free(&argv[i]);
            free(argv);
            value_free(&obj);
            value_free(&callee);
            return result;
        }

        case EXPR_FN: case EXPR_LAMBDA: {
            Closure* c = (Closure*)kodas_calloc(1, sizeof(Closure));
            c->name = e->as.fn_e.name ? kodas_strdup(e->as.fn_e.name) : NULL;
            vparams_from_params(&c->params, &e->as.fn_e.params);
            c->body = e->as.fn_e.body;
            c->is_arrow = e->as.fn_e.is_arrow;
            c->closure = env;
            c->decl = NULL;
            return val_closure(c);
        }

        case EXPR_BLOCK: {
            Environment* block_env = env_new(env);
            Value result = val_null();
            for (int i = 0; i < e->as.block.stmts.count; i++) {
                value_free(&result);
                result = eval_stmt(ev, block_env, e->as.block.stmts.items[i]);
                if (ev->had_error || ev->had_return || ev->had_stop || ev->had_skip) break;
            }
            env_free(block_env);
            return result;
        }

        case EXPR_IF: {
            Value cond = eval_expr(ev, env, e->as.if_e.cond);
            if (ev->had_error) return val_null();
            Value result;
            if (value_truthy(cond)) {
                result = eval_expr(ev, env, e->as.if_e.then_branch);
            } else if (e->as.if_e.else_branch) {
                result = eval_expr(ev, env, e->as.if_e.else_branch);
            } else {
                result = val_null();
            }
            value_free(&cond);
            return result;
        }

        case EXPR_THEN_ELSE: {
            Value cond = eval_expr(ev, env, e->as.then_else.cond);
            if (ev->had_error) return val_null();
            Value result = value_truthy(cond)
                ? eval_expr(ev, env, e->as.then_else.then_val)
                : eval_expr(ev, env, e->as.then_else.else_val);
            value_free(&cond);
            return result;
        }

        case EXPR_MATCH: {
            Value obj = eval_expr(ev, env, e->as.match_e.obj);
            if (ev->had_error) return val_null();
            Value result = val_null();
            bool matched = false;
            for (int i = 0; i < e->as.match_e.arm_count; i++) {
                Environment* bind_env = env_new(env);
                if (pattern_match(ev, env, e->as.match_e.arms[i].pat, obj, bind_env)) {
                    // Check guard
                    if (e->as.match_e.arms[i].pat->kind == PAT_GUARD && e->as.match_e.arms[i].pat->guard) {
                        Value g = eval_expr(ev, bind_env, e->as.match_e.arms[i].pat->guard);
                        bool ok = value_truthy(g);
                        value_free(&g);
                        if (!ok) { env_free(bind_env); continue; }
                    }
                    result = eval_expr(ev, bind_env, e->as.match_e.arms[i].body);
                    matched = true;
                    env_free(bind_env);
                    break;
                }
                env_free(bind_env);
            }
            if (!matched && !ev->had_error) {
                runtime_error(ev, "match: non-exhaustive patterns");
            }
            value_free(&obj);
            return result;
        }

        case EXPR_PIPE: {
            Value acc = eval_expr(ev, env, e->as.pipe.callee);
            if (ev->had_error) return val_null();
            for (int i = 0; i < e->as.pipe.pipe_count; i++) {
                Value fn = eval_expr(ev, env, e->as.pipe.pipelines[i]);
                if (ev->had_error) { value_free(&acc); value_free(&fn); return val_null(); }
                Value arg = acc;
                Value result = eval_call(ev, env, fn, 1, &arg);
                acc = result;
                value_free(&fn);
            }
            return acc;
        }

        case EXPR_ERR_PIPE: {
            Value acc = eval_expr(ev, env, e->as.pipe.callee);
            if (ev->had_error) return val_null();
            for (int i = 0; i < e->as.pipe.pipe_count; i++) {
                // If acc is err, propagate
                if (acc.kind == VAL_RESULT && acc.is_error) break;
                Value fn = eval_expr(ev, env, e->as.pipe.pipelines[i]);
                if (ev->had_error) { value_free(&acc); value_free(&fn); return val_null(); }
                // Extract ok value
                Value arg;
                if (acc.kind == VAL_RESULT && !acc.is_error && acc.as.wrapped) {
                    arg = value_copy(*acc.as.wrapped);
                } else {
                    arg = value_copy(acc);
                }
                Value result = eval_call(ev, env, fn, 1, &arg);
                value_free(&acc);
                value_free(&arg);
                acc = result;
                value_free(&fn);
            }
            // Handle otherwise/demand: handled at binary level for simplicity
            return acc;
        }

        case EXPR_LOOP: {
            Value result = val_null();
            if (e->as.loop_e.count) {
                Value count_v = eval_expr(ev, env, e->as.loop_e.count);
                int64_t n = count_v.as.int_val;
                for (int64_t i = 0; i < n; i++) {
                    value_free(&result);
                    result = eval_expr(ev, env, e->as.loop_e.body);
                    if (ev->had_error) { value_free(&count_v); return val_null(); }
                    if (ev->had_stop) { ev->had_stop = false; break; }
                    if (ev->had_skip) { ev->had_skip = false; continue; }
                    if (ev->had_return) break;
                }
                value_free(&count_v);
            } else {
                while (!ev->had_error && !ev->had_return) {
                    value_free(&result);
                    result = eval_expr(ev, env, e->as.loop_e.body);
                    if (ev->had_stop) { ev->had_stop = false; break; }
                    if (ev->had_skip) { ev->had_skip = false; continue; }
                }
            }
            return result;
        }

        case EXPR_WHILE: {
            Value result = val_null();
            while (!ev->had_error && !ev->had_return) {
                Value cond = eval_expr(ev, env, e->as.loop_in.cond);
                if (!value_truthy(cond)) { value_free(&cond); break; }
                value_free(&cond);
                value_free(&result);
                result = eval_expr(ev, env, e->as.loop_in.body);
                if (ev->had_stop) { ev->had_stop = false; break; }
                if (ev->had_skip) { ev->had_skip = false; continue; }
            }
            return result;
        }

        case EXPR_LOOP_IN: {
            Value iter_v = eval_expr(ev, env, e->as.loop_in.iter);
            if (ev->had_error) return val_null();
            Value result = val_null();
            Environment* loop_env = env_new(env);
            if (iter_v.kind == VAL_LIST || iter_v.kind == VAL_TUPLE || iter_v.kind == VAL_STRING) {
                int len;
                if (iter_v.kind == VAL_STRING) len = (int)strlen(iter_v.as.str->data);
                else len = iter_v.as.list->count;
                for (int i = 0; i < len; i++) {
                    Value item;
                    if (iter_v.kind == VAL_STRING) {
                        char buf[2] = {iter_v.as.str->data[i], 0};
                        item = val_string(buf);
                    } else {
                        item = value_copy(iter_v.as.list->items[i]);
                    }
                    if (e->as.loop_in.var) env_define(loop_env, e->as.loop_in.var, value_copy(item), true);
                    if (e->as.loop_in.idx_var) env_define(loop_env, e->as.loop_in.idx_var, val_int(i), true);
                    value_free(&result);
                    result = eval_expr(ev, loop_env, e->as.loop_in.body);
                    value_free(&item);
                    if (ev->had_error) break;
                    if (ev->had_stop) { ev->had_stop = false; break; }
                    if (ev->had_skip) { ev->had_skip = false; continue; }
                    if (ev->had_return) break;
                }
            } else if (iter_v.kind == VAL_MAP) {
                for (int i = 0; i < iter_v.as.map->count; i++) {
                    Value key = val_string(iter_v.as.map->keys[i]);
                    Value val = value_copy(iter_v.as.map->values[i]);
                    if (e->as.loop_in.var) env_define(loop_env, e->as.loop_in.var, value_copy(val), true);
                    if (e->as.loop_in.idx_var) env_define(loop_env, e->as.loop_in.idx_var, value_copy(key), true);
                    value_free(&result);
                    result = eval_expr(ev, loop_env, e->as.loop_in.body);
                    value_free(&key); value_free(&val);
                    if (ev->had_error) break;
                    if (ev->had_stop) { ev->had_stop = false; break; }
                    if (ev->had_skip) { ev->had_skip = false; continue; }
                    if (ev->had_return) break;
                }
            }
            value_free(&iter_v);
            env_free(loop_env);
            return result;
        }

        case EXPR_STOP: ev->had_stop = true; return val_null();
        case EXPR_SKIP: ev->had_skip = true; return val_null();
        case EXPR_GIVE: {
            if (e->as.expr) {
                value_free(&ev->return_value);
                ev->return_value = eval_expr(ev, env, e->as.expr);
            } else {
                value_free(&ev->return_value);
                ev->return_value = val_null();
            }
            ev->had_return = true;
            return val_null();
        }

        case EXPR_SHOW: {
            if (e->as.expr) {
                Value v = eval_expr(ev, env, e->as.expr);
                if (ev->had_error) return val_null();
                value_print(v, true);
                value_free(&v);
            } else {
                putchar('\n');
            }
            fflush(stdout);
            return val_null();
        }

        case EXPR_ASSERT: {
            Value v = eval_expr(ev, env, e->as.expr);
            if (!value_truthy(v)) {
                runtime_error(ev, "assertion failed");
            }
            value_free(&v);
            return val_null();
        }

        case EXPR_ASSIGN: {
            Value v = eval_expr(ev, env, e->as.assign.value);
            if (!env_assign(env, e->as.assign.name, value_copy(v))) {
                runtime_error(ev, "undefined variable '%s'", e->as.assign.name);
            }
            return v;
        }

        case EXPR_COMPOUND_ASSIGN: {
            Value v = eval_expr(ev, env, e->as.compound.value);
            Value tgt = eval_expr(ev, env, e->as.compound.target);
            Value result;
            if (strcmp(e->as.compound.op, "=") == 0) result = value_copy(v);
            else result = numeric_binary(ev, tgt, e->as.compound.op, v);
            // Write back if target is ident
            if (e->as.compound.target->kind == EXPR_IDENT) {
                env_assign(env, e->as.compound.target->as.str_val, value_copy(result));
            }
            value_free(&tgt); value_free(&v);
            return result;
        }

        case EXPR_FIELD_ASSIGN: {
            Value v = eval_expr(ev, env, e->as.field_assign.value);
            Value obj = eval_expr(ev, env, e->as.field_assign.object);
            if (obj.kind == VAL_STRUCT_INST && e->as.field_assign.field) {
                struct_set(&obj, e->as.field_assign.field, value_copy(v));
            }
            value_free(&obj);
            return v;
        }

        case EXPR_LET: {
            Value v = e->as.let.value ? eval_expr(ev, env, e->as.let.value) : val_null();
            env_define(env, e->as.let.name, value_copy(v), e->as.let.is_mut);
            return v;
        }

        case EXPR_OK: {
            if (e->as.result_e.value) {
                Value inner = eval_expr(ev, env, e->as.result_e.value);
                return val_ok(inner);
            }
            return val_ok(val_bool(true));
        }
        case EXPR_ERR: {
            if (e->as.result_e.value) {
                Value inner = eval_expr(ev, env, e->as.result_e.value);
                return val_err(inner);
            }
            return val_err(val_string("error"));
        }

        case EXPR_STRUCT_INIT: {
            Value inst = val_struct(e->as.struct_init.name);
            for (int i = 0; i < e->as.struct_init.field_count; i++) {
                Value v = eval_expr(ev, env, e->as.struct_init.field_vals[i]);
                struct_set(&inst, e->as.struct_init.field_names[i], v);
            }
            return inst;
        }

        default:
            return val_null();
    }
}

static Value eval_call(Evaluator* ev, Environment* env, Value callee, int argc, Value* argv) {
    (void)env;
    if (callee.kind == VAL_NATIVE && callee.as.native.fn) {
        return callee.as.native.fn(argc, argv, callee.as.native.ctx);
    }
    if (callee.kind == VAL_CLOSURE && callee.as.closure) {
        Closure* c = callee.as.closure;
        Environment* fn_env = env_new(c->closure ? c->closure : ev->globals);
        // Bind parameters
        for (int i = 0; i < c->params.count; i++) {
            Value val;
            if (i < argc) {
                val = value_copy(argv[i]);
            } else if (c->params.items[i].default_value) {
                val = eval_expr(ev, ev->globals, c->params.items[i].default_value);
            } else {
                val = val_null();
            }
            env_define(fn_env, c->params.items[i].name, val, c->params.items[i].is_mut);
        }
        ev->call_depth++;
        Value result;
        bool prev_return = ev->had_return;
        ev->had_return = false;
        if (c->is_arrow) {
            result = eval_expr(ev, fn_env, c->body);
        } else if (c->body && c->body->kind == EXPR_BLOCK) {
            result = val_null();
            for (int i = 0; i < c->body->as.block.stmts.count; i++) {
                value_free(&result);
                result = eval_stmt(ev, fn_env, c->body->as.block.stmts.items[i]);
                if (ev->had_error || ev->had_return || ev->had_stop || ev->had_skip) break;
            }
        } else if (c->body) {
            result = eval_expr(ev, fn_env, c->body);
        } else {
            result = val_null();
        }
        if (ev->had_return) {
            value_free(&result);
            result = ev->return_value;
            ev->return_value = val_null();
        }
        ev->had_return = prev_return;
        ev->had_stop = false;
        ev->had_skip = false;
        ev->call_depth--;
        env_free(fn_env);
        return result;
    }
    runtime_error(ev, "attempt to call a non-function value");
    return val_null();
}

Value eval_block(Evaluator* ev, Environment* env, StmtList stmts) {
    Value result = val_null();
    for (int i = 0; i < stmts.count; i++) {
        value_free(&result);
        result = eval_stmt(ev, env, stmts.items[i]);
        if (ev->had_error || ev->had_return || ev->had_stop || ev->had_skip) break;
    }
    return result;
}

Value eval_stmt(Evaluator* ev, Environment* env, Stmt* stmt) {
    if (!stmt) return val_null();
    if (ev->had_error || ev->had_return || ev->had_stop || ev->had_skip) return val_null();

    switch (stmt->kind) {
        case STMT_EXPR: {
            Value v = eval_expr(ev, env, stmt->as.expr);
            return v;
        }
        case STMT_RETURN: {
            if (stmt->as.expr) {
                value_free(&ev->return_value);
                ev->return_value = eval_expr(ev, env, stmt->as.expr);
            } else {
                value_free(&ev->return_value);
                ev->return_value = val_null();
            }
            ev->had_return = true;
            return val_null();
        }
        case STMT_LET: {
            Value v = stmt->as.let_s.value ? eval_expr(ev, env, stmt->as.let_s.value) : val_null();
            env_define(env, stmt->as.let_s.name, value_copy(v), stmt->as.let_s.is_mut);
            return v;
        }
        case STMT_SEAL: {
            env_define(env, stmt->as.seal_s.name, val_null(), false);
            return val_null();
        }
        case STMT_FN: {
            Closure* c = (Closure*)kodas_calloc(1, sizeof(Closure));
            c->name = kodas_strdup(stmt->as.fn_s.name);
            vparams_from_params(&c->params, &stmt->as.fn_s.params);
            c->body = stmt->as.fn_s.body;
            c->is_arrow = stmt->as.fn_s.is_arrow;
            c->closure = env;
            c->decl = stmt;
            Value fn = val_closure(c);
            env_define(env, stmt->as.fn_s.name, fn, false);
            return val_null();
        }
        case STMT_STRUCT: {
            // Register a native constructor: User(...) builds a User struct
            const char* sname = stmt->as.struct_s.name;
            char* name_copy = kodas_strdup(sname);
            env_define(env, sname, val_native(native_struct_ctor, sname, name_copy), false);
            return val_null();
        }
        case STMT_ENUM: {
            const char* ename = stmt->as.enum_s.name;
            // Register each variant
            for (int i = 0; i < stmt->as.enum_s.variants.count; i++) {
                const char* var_name = stmt->as.enum_s.variants.items[i].name;
                int nfields = stmt->as.enum_s.variants.items[i].fields.count;
                if (nfields == 0) {
                    // No-payload variant: expose as a constant value (like a literal)
                    Value v = val_enum_variant(ename, var_name, NULL);
                    env_define(env, var_name, v, false);
                } else {
                    // Payload variant: expose as a native constructor function
                    char* full = (char*)kodas_malloc(strlen(ename) + strlen(var_name) + 2);
                    strcpy(full, ename); strcat(full, "."); strcat(full, var_name);
                    Value ctor = val_native(native_enum_variant_ctor, var_name, full);
                    env_define(env, var_name, ctor, false);
                }
            }
            // Register enum name as a type marker (matches struct behavior)
            char* ename_copy = kodas_strdup(ename);
            env_define(env, ename, val_native(native_struct_ctor, ename, ename_copy), false);
            return val_null();
        }
        case STMT_BEHAVE: {
            // Register methods as TypeName.method in global scope
            for (int i = 0; i < stmt->as.behave_s.methods.count; i++) {
                FnDecl* m = &stmt->as.behave_s.methods.items[i];
                char full_name[256];
                snprintf(full_name, sizeof(full_name), "%s.%s", stmt->as.behave_s.type_name, m->name);
                Closure* c = (Closure*)kodas_calloc(1, sizeof(Closure));
                c->name = kodas_strdup(full_name);
                vparams_from_params(&c->params, &m->params);
                c->body = m->body;
                c->is_arrow = m->is_arrow;
                c->closure = env;
                Value fn = val_closure(c);
                env_define(ev->globals, full_name, fn, false);
            }
            return val_null();
        }
        case STMT_TRAIT: {
            // Register trait default methods as closures keyed by "TraitName.methodName"
            for (int i = 0; i < stmt->as.trait_s.method_count; i++) {
                TraitMethod* m = &stmt->as.trait_s.methods[i];
                if (m->default_body) {
                    char key[256];
                    snprintf(key, sizeof(key), "%s.%s", stmt->as.trait_s.name, m->name);
                    Closure* c = (Closure*)kodas_calloc(1, sizeof(Closure));
                    c->name = kodas_strdup(key);
                    vparams_from_params(&c->params, &m->params);
                    c->body = m->default_body;
                    c->is_arrow = false;
                    c->closure = env;
                    env_define(ev->globals, key, val_closure(c), false);
                }
            }
            return val_null();
        }
        case STMT_IMPL: {
            // impl Trait for Type { fn method(...) { ... } }
            // First collect which method names this impl overrides.
            const char* trait_name = stmt->as.impl_s.trait_name;
            const char* type_name = stmt->as.impl_s.type_name;
            bool provided[64] = {0};
            int provided_cap = 64;
            for (int i = 0; i < stmt->as.impl_s.methods.count; i++) {
                FnDecl* m = &stmt->as.impl_s.methods.items[i];
                char full_name[256];
                snprintf(full_name, sizeof(full_name), "%s.%s", type_name, m->name);
                Closure* c = (Closure*)kodas_calloc(1, sizeof(Closure));
                c->name = kodas_strdup(full_name);
                vparams_from_params(&c->params, &m->params);
                c->body = m->body;
                c->is_arrow = m->is_arrow;
                c->closure = env;
                env_define(ev->globals, full_name, val_closure(c), false);
                if (i < provided_cap) provided[i] = true;
            }
            // Also copy default trait methods (from TraitName.method) for any not overridden.
            // Naive scan: look for globals starting with "TraitName." and register alias "Type.method"
            // In seed, globals are in an Environment*; we iterate by scanning names we know.
            // For simplicity, we iterate up to a reasonable number of trait methods if they exist in impl_s context — instead we maintain a global list via env iteration.
            // Since env iteration is unavailable, we rely on a flat scan via env_lookup on known defaults stored during trait decl.
            // Use the trait_methods list on ev? Simpler: we stored trait defaults under "Trait.method"; copy each one whose matching "Type.method" isn't already present.
            // We keep a small hard-coded table of known trait methods — but better: find the STMT_TRAIT matching trait_name in the program and iterate.
            // Do a lazy approach: walk the program to find trait decl.
            {
                Stmt* ts = NULL;
                for (Stmt* s2 = ev->prog_first; s2; s2 = s2->next) {
                    if (s2->kind == STMT_TRAIT && strcmp(s2->as.trait_s.name, trait_name) == 0) { ts = s2; break; }
                }
                if (ts) {
                    for (int i = 0; i < ts->as.trait_s.method_count; i++) {
                        TraitMethod* tm = &ts->as.trait_s.methods[i];
                        if (!tm->default_body) continue;
                        char type_key[256];
                        snprintf(type_key, sizeof(type_key), "%s.%s", type_name, tm->name);
                        if (env_lookup(ev->globals, type_key)) continue; // already overridden
                        char trait_key[256];
                        snprintf(trait_key, sizeof(trait_key), "%s.%s", trait_name, tm->name);
                        Value* def = env_lookup(ev->globals, trait_key);
                        if (def) {
                            env_define(ev->globals, type_key, value_copy(*def), false);
                        }
                    }
                }
            }
            return val_null();
        }
        case STMT_MODULE:
        case STMT_IMPORT:
        case STMT_BRIDGE:
            // Modules/imports are no-ops in seed (single-file execution)
            return val_null();
        case STMT_TEST: {
            // Run test immediately
            Value result = eval_expr(ev, env, stmt->as.test_s.body);
            value_free(&result);
            if (!ev->had_error) {
                printf("  ✓ test %s\n", stmt->as.test_s.name);
            } else {
                printf("  ✗ test %s FAILED: %s\n", stmt->as.test_s.name, ev->error_msg);
                ev->had_error = false;
                ev->error_msg[0] = '\0';
            }
            return val_null();
        }
        case STMT_BENCH: {
            printf("  ⏱ bench %s\n", stmt->as.test_s.name);
            double start = current_time_seconds_impl();
            Value result = eval_expr(ev, env, stmt->as.test_s.body);
            double elapsed = current_time_seconds_impl() - start;
            printf("    took %.4f seconds\n", elapsed);
            value_free(&result);
            return val_null();
        }
        case STMT_CONTRACT: {
            // Contracts - validate environment
            for (int i = 0; i < stmt->as.contract_s.decls.count; i++) {
                Value v = eval_expr(ev, env, stmt->as.contract_s.decls.items[i]);
                value_free(&v);
            }
            return val_null();
        }
        case STMT_DECORATOR: {
            // Apply decorator: decorator(inner_stmt)
            // Simplified: just evaluate the inner statement
            if (stmt->as.decorator_s.stmt) {
                return eval_stmt(ev, env, stmt->as.decorator_s.stmt);
            }
            return val_null();
        }
        case STMT_PUB: {
            if (stmt->next) return eval_stmt(ev, env, stmt->next);
            return val_null();
        }
    }
    return val_null();
}

Value eval_program(Evaluator* ev, Program* prog) {
    Value result = val_null();
    ev->prog_first = prog->first;
    for (Stmt* s = prog->first; s; s = s->next) {
        value_free(&result);
        result = eval_stmt(ev, ev->globals, s);
        if (ev->had_error) break;
        if (ev->had_return) { result = ev->return_value; break; }
    }
    return result;
}

// Pattern matching
static bool pattern_match(Evaluator* ev, Environment* env, Pattern* pat, Value val, Environment* binds) {
    (void)ev; (void)env;
    switch (pat->kind) {
        case PAT_WILDCARD: return true;
        case PAT_LITERAL:
            return pat->literal && value_equals(*(Value*)&pat->literal->as.int_val /* unused */, val) ? false : value_equals(
                // simpler: re-evaluate literal
                (pat->literal->kind == EXPR_INT ? val_int(pat->literal->as.int_val) :
                 pat->literal->kind == EXPR_FLOAT ? val_float(pat->literal->as.float_val) :
                 pat->literal->kind == EXPR_BOOL ? val_bool(pat->literal->as.bool_val) :
                 pat->literal->kind == EXPR_STRING ? (pat->literal->as.str_val ? val_string(pat->literal->as.str_val) : val_string("")) :
                 val_null()), val);
        case PAT_BINDING:
            if (pat->name) {
                env_define(binds, pat->name, value_copy(val), true);
            }
            return true;
        case PAT_TUPLE:
            if (val.kind != VAL_TUPLE && val.kind != VAL_LIST) return false;
            if (val.as.list->count != pat->sub_count) return false;
            for (int i = 0; i < pat->sub_count; i++) {
                Value elem = value_copy(val.as.list->items[i]);
                if (!pattern_match(ev, env, pat->sub[i], elem, binds)) { value_free(&elem); return false; }
                value_free(&elem);
            }
            return true;
        case PAT_RANGE: {
            if (pat->sub_count < 2) return false;
            Value lo = pat->sub[0]->kind == PAT_LITERAL && pat->sub[0]->literal->kind == EXPR_INT ? val_int(pat->sub[0]->literal->as.int_val) : val_null();
            Value hi = pat->sub[1]->kind == PAT_LITERAL && pat->sub[1]->literal->kind == EXPR_INT ? val_int(pat->sub[1]->literal->as.int_val) : val_null();
            bool ok = value_compare(val, lo) >= 0 && value_compare(val, hi) <= 0;
            // Note: this is inclusive; for .. ranges we should exclude hi but parser can't easily flag
            return ok;
        }
        case PAT_STRUCT:
            if (val.kind != VAL_STRUCT_INST) return false;
            for (int i = 0; i < pat->field_count; i++) {
                Value* f = struct_get(val, pat->field_names[i]);
                if (!f) return false;
                Value fv = value_copy(*f);
                if (!pattern_match(ev, env, pat->field_pats[i], fv, binds)) { value_free(&fv); return false; }
                value_free(&fv);
            }
            return true;
        case PAT_ENUM_VARIANT: {
            if (val.kind != VAL_ENUM_VARIANT) return false;
            if (strcmp(val.as.variant->variant_name, pat->name) != 0) return false;
            if (pat->sub_count == 0) return true;
            if (!val.as.variant->payload) return false;
            // If single sub-pattern, match payload directly; else assume tuple
            if (pat->sub_count == 1) {
                return pattern_match(ev, env, pat->sub[0], *val.as.variant->payload, binds);
            }
            if (val.as.variant->payload->kind == VAL_TUPLE && val.as.variant->payload->as.list->count == pat->sub_count) {
                for (int i = 0; i < pat->sub_count; i++) {
                    Value elem = value_copy(val.as.variant->payload->as.list->items[i]);
                    if (!pattern_match(ev, env, pat->sub[i], elem, binds)) { value_free(&elem); return false; }
                    value_free(&elem);
                }
                return true;
            }
            return false;
        }
        case PAT_GUARD:
            if (pat->sub_count < 1) return false;
            if (!pattern_match(ev, env, pat->sub[0], val, binds)) return false;
            return true; // guard checked in caller
        default: return false;
    }
}
