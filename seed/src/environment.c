/// environment.c - Kodas environment & native builtins
#define _POSIX_C_SOURCE 200809L
#include "environment.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

// Forward declarations
static Value builtin_show(int argc, Value* args, void* ctx);
static Value builtin_print(int argc, Value* args, void* ctx);
static Value builtin_eprint(int argc, Value* args, void* ctx);
static Value builtin_str(int argc, Value* args, void* ctx);
static Value builtin_int(int argc, Value* args, void* ctx);
static Value builtin_float(int argc, Value* args, void* ctx);
static Value builtin_bool(int argc, Value* args, void* ctx);
static Value builtin_char(int argc, Value* args, void* ctx);
static Value builtin_len(int argc, Value* args, void* ctx);
static Value builtin_push(int argc, Value* args, void* ctx);
static Value builtin_pop(int argc, Value* args, void* ctx);
static Value builtin_insert(int argc, Value* args, void* ctx);
static Value builtin_remove(int argc, Value* args, void* ctx);
static Value builtin_range(int argc, Value* args, void* ctx);
static Value builtin_typename(int argc, Value* args, void* ctx);
static Value builtin_assert(int argc, Value* args, void* ctx);
static Value builtin_sqrt(int argc, Value* args, void* ctx);
static Value builtin_sin(int argc, Value* args, void* ctx);
static Value builtin_cos(int argc, Value* args, void* ctx);
static Value builtin_tan(int argc, Value* args, void* ctx);
static Value builtin_log(int argc, Value* args, void* ctx);
static Value builtin_exp(int argc, Value* args, void* ctx);
static Value builtin_floor(int argc, Value* args, void* ctx);
static Value builtin_ceil(int argc, Value* args, void* ctx);
static Value builtin_round(int argc, Value* args, void* ctx);
static Value builtin_min(int argc, Value* args, void* ctx);
static Value builtin_max(int argc, Value* args, void* ctx);
static Value builtin_pow(int argc, Value* args, void* ctx);
static Value builtin_abs(int argc, Value* args, void* ctx);
static Value builtin_input(int argc, Value* args, void* ctx);
static Value builtin_exit(int argc, Value* args, void* ctx);
static Value builtin_read_file(int argc, Value* args, void* ctx);
static Value builtin_write_file(int argc, Value* args, void* ctx);
static Value builtin_append_file(int argc, Value* args, void* ctx);
static Value builtin_clock(int argc, Value* args, void* ctx);
static Value builtin_sleep(int argc, Value* args, void* ctx);
static Value builtin_keys(int argc, Value* args, void* ctx);
static Value builtin_values(int argc, Value* args, void* ctx);
static Value builtin_list_get(int argc, Value* args, void* ctx);
static Value builtin_list_set(int argc, Value* args, void* ctx);
static Value builtin_split(int argc, Value* args, void* ctx);
static Value builtin_contains(int argc, Value* args, void* ctx);
static Value builtin_chars(int argc, Value* args, void* ctx);
static Value builtin_lines(int argc, Value* args, void* ctx);
static Value builtin_trim(int argc, Value* args, void* ctx);
static Value builtin_starts_with(int argc, Value* args, void* ctx);
static Value builtin_ends_with(int argc, Value* args, void* ctx);
static Value builtin_replace(int argc, Value* args, void* ctx);
static Value builtin_typeof(int argc, Value* args, void* ctx);
static Value builtin_ok(int argc, Value* args, void* ctx);
static Value builtin_err(int argc, Value* args, void* ctx);
static Value builtin_is_ok(int argc, Value* args, void* ctx);
static Value builtin_is_err(int argc, Value* args, void* ctx);
static Value builtin_unwrap(int argc, Value* args, void* ctx);
static Value builtin_unwrap_or(int argc, Value* args, void* ctx);
static Value builtin_reverse(int argc, Value* args, void* ctx);
static Value builtin_sort(int argc, Value* args, void* ctx);
static Value builtin_enumerate(int argc, Value* args, void* ctx);
static Value builtin_zip(int argc, Value* args, void* ctx);
static Value builtin_utf8(int argc, Value* args, void* ctx);
static Value builtin_chr(int argc, Value* args, void* ctx);
static Value builtin_ord(int argc, Value* args, void* ctx);
static Value builtin_format(int argc, Value* args, void* ctx);
static Value builtin_deep_copy(int argc, Value* args, void* ctx);
static Value builtin_hash(int argc, Value* args, void* ctx);

double current_time_seconds_impl(void);

Environment* env_new(Environment* enclosing) {
    Environment* env = (Environment*)kodas_calloc(1, sizeof(Environment));
    env->enclosing = enclosing;
    return env;
}

void env_free(Environment* env) {
    if (!env) return;
    for (int i = 0; i < env->count; i++) {
        free(env->names[i]);
        value_free(&env->values[i]);
    }
    free(env->names);
    free(env->values);
    free(env->mut_flags);
    free(env);
}

void env_define(Environment* env, const char* name, Value value, bool is_mut) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            if (env->enclosing == NULL) {
                // Top-level (module/global scope): allow redefinition via let/seal
                // so stdlib files can override builtin constants or prior definitions
                value_free(&env->values[i]);
                env->values[i] = value;
                env->mut_flags[i] = is_mut;
                return;
            }
            if (!env->mut_flags[i] && env->values[i].kind != VAL_NATIVE) {
                fprintf(stderr, "error: cannot reassign to seal '%s'\n", name);
                value_free(&value);
                return;
            }
            value_free(&env->values[i]);
            env->values[i] = value;
            env->mut_flags[i] = is_mut;
            return;
        }
    }
    if (env->count >= env->capacity) {
        env->capacity = env->capacity == 0 ? 8 : env->capacity * 2;
        env->names = kodas_realloc(env->names, env->capacity * sizeof(char*));
        env->values = kodas_realloc(env->values, env->capacity * sizeof(Value));
        env->mut_flags = kodas_realloc(env->mut_flags, env->capacity * sizeof(bool));
    }
    env->names[env->count] = kodas_strdup(name);
    env->values[env->count] = value;
    env->mut_flags[env->count] = is_mut;
    env->count++;
}

void env_define_global(Environment* env, const char* name, NativeFn fn, const char* fn_name, void* ctx) {
    env_define(env, name, val_native(fn, fn_name, ctx), false);
}

Value* env_lookup(Environment* env, const char* name) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) return &env->values[i];
    }
    if (env->enclosing) return env_lookup(env->enclosing, name);
    return NULL;
}

bool env_assign(Environment* env, const char* name, Value value) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) {
            if (!env->mut_flags[i]) {
                fprintf(stderr, "error: cannot assign to seal '%s'\n", name);
                value_free(&value);
                return false;
            }
            value_free(&env->values[i]);
            env->values[i] = value;
            return true;
        }
    }
    if (env->enclosing) return env_assign(env->enclosing, name, value);
    value_free(&value);
    return false;
}

void env_register_builtins(Environment* env) {
    // I/O
    env_define_global(env, "show", builtin_show, "show", NULL);
    env_define_global(env, "print", builtin_print, "print", NULL);
    env_define_global(env, "eprint", builtin_eprint, "eprint", NULL);
    env_define_global(env, "input", builtin_input, "input", NULL);
    env_define_global(env, "exit", builtin_exit, "exit", NULL);

    // Type conversion
    env_define_global(env, "str", builtin_str, "str", NULL);
    env_define_global(env, "int", builtin_int, "int", NULL);
    env_define_global(env, "float", builtin_float, "float", NULL);
    env_define_global(env, "bool", builtin_bool, "bool", NULL);
    env_define_global(env, "char", builtin_char, "char", NULL);
    env_define_global(env, "chr", builtin_chr, "chr", NULL);
    env_define_global(env, "ord", builtin_ord, "ord", NULL);

    // Collections
    env_define_global(env, "len", builtin_len, "len", NULL);
    env_define_global(env, "push", builtin_push, "push", NULL);
    env_define_global(env, "pop", builtin_pop, "pop", NULL);
    env_define_global(env, "insert", builtin_insert, "insert", NULL);
    env_define_global(env, "remove", builtin_remove, "remove", NULL);
    env_define_global(env, "range", builtin_range, "range", NULL);
    env_define_global(env, "reverse", builtin_reverse, "reverse", NULL);
    env_define_global(env, "sort", builtin_sort, "sort", NULL);
    env_define_global(env, "enumerate", builtin_enumerate, "enumerate", NULL);
    env_define_global(env, "zip", builtin_zip, "zip", NULL);
    env_define_global(env, "keys", builtin_keys, "keys", NULL);
    env_define_global(env, "values", builtin_values, "values", NULL);
    env_define_global(env, "list_get", builtin_list_get, "list_get", NULL);
    env_define_global(env, "list_set", builtin_list_set, "list_set", NULL);
    env_define_global(env, "get", builtin_list_get, "get", NULL);
    env_define_global(env, "set", builtin_list_set, "set", NULL);
    env_define_global(env, "deep_copy", builtin_deep_copy, "deep_copy", NULL);
    env_define_global(env, "clone", builtin_deep_copy, "clone", NULL);

    // Strings
    env_define_global(env, "split", builtin_split, "split", NULL);
    env_define_global(env, "contains", builtin_contains, "contains", NULL);
    env_define_global(env, "chars", builtin_chars, "chars", NULL);
    env_define_global(env, "lines", builtin_lines, "lines", NULL);
    env_define_global(env, "trim", builtin_trim, "trim", NULL);
    env_define_global(env, "starts_with", builtin_starts_with, "starts_with", NULL);
    env_define_global(env, "ends_with", builtin_ends_with, "ends_with", NULL);
    env_define_global(env, "replace", builtin_replace, "replace", NULL);
    env_define_global(env, "utf8", builtin_utf8, "utf8", NULL);
    env_define_global(env, "format", builtin_format, "format", NULL);

    // Math
    env_define_global(env, "sqrt", builtin_sqrt, "sqrt", NULL);
    env_define_global(env, "sin", builtin_sin, "sin", NULL);
    env_define_global(env, "cos", builtin_cos, "cos", NULL);
    env_define_global(env, "tan", builtin_tan, "tan", NULL);
    env_define_global(env, "log", builtin_log, "log", NULL);
    env_define_global(env, "exp", builtin_exp, "exp", NULL);
    env_define_global(env, "floor", builtin_floor, "floor", NULL);
    env_define_global(env, "ceil", builtin_ceil, "ceil", NULL);
    env_define_global(env, "round", builtin_round, "round", NULL);
    env_define_global(env, "pow", builtin_pow, "pow", NULL);
    env_define_global(env, "abs", builtin_abs, "abs", NULL);
    env_define_global(env, "min", builtin_min, "min", NULL);
    env_define_global(env, "max", builtin_max, "max", NULL);

    // Reflection / types
    env_define_global(env, "typename", builtin_typename, "typename", NULL);
    env_define_global(env, "typeof", builtin_typeof, "typeof", NULL);
    env_define_global(env, "hash", builtin_hash, "hash", NULL);

    // Error handling
    env_define_global(env, "ok", builtin_ok, "ok", NULL);
    env_define_global(env, "err", builtin_err, "err", NULL);
    env_define_global(env, "is_ok", builtin_is_ok, "is_ok", NULL);
    env_define_global(env, "is_err", builtin_is_err, "is_err", NULL);
    env_define_global(env, "unwrap", builtin_unwrap, "unwrap", NULL);
    env_define_global(env, "unwrap_or", builtin_unwrap_or, "unwrap_or", NULL);

    // Testing / assertions
    env_define_global(env, "assert", builtin_assert, "assert", NULL);

    // Files / system
    env_define_global(env, "read_file", builtin_read_file, "read_file", NULL);
    env_define_global(env, "write_file", builtin_write_file, "write_file", NULL);
    env_define_global(env, "append_file", builtin_append_file, "append_file", NULL);
    env_define_global(env, "clock", builtin_clock, "clock", NULL);
    env_define_global(env, "sleep", builtin_sleep, "sleep", NULL);

    // Constants
    env_define(env, "PI", val_float(3.141592653589793), false);
    env_define(env, "E", val_float(2.718281828459045), false);
    env_define(env, "true", val_bool(true), false);
    env_define(env, "false", val_bool(false), false);
    env_define(env, "null", val_null(), false);
    env_define(env, "empty", val_empty(), false);
}

// ---- Helper macros ----
#define ARG(i) ((i) < argc ? args[(i)] : val_null())
#define REQUIRE(n, msg) do { if (argc < (n)) return val_err(val_string(msg)); } while(0)
#define TYPE(i, k, msg) do { if ((i) >= argc || args[(i)].kind != (k)) return val_err(val_string(msg)); } while(0)
#define STR(i) ((args[(i)].kind == VAL_STRING && args[(i)].as.str) ? args[(i)].as.str->data : "")
#define STR_LEN(i) ((args[(i)].kind == VAL_STRING && args[(i)].as.str) ? args[(i)].as.str->length : 0)
#define IS_NUM(v) ((v).kind == VAL_INT || (v).kind == VAL_FLOAT)
#define AS_DBL(v) ((v).kind == VAL_FLOAT ? (v).as.float_val : (double)(v).as.int_val)
#define LIST_ITEMS(v) ((v).as.list ? (v).as.list->items : NULL)
#define LIST_COUNT(v) ((v).as.list ? (v).as.list->count : 0)

// ── I/O ─────────────────────────────────────────────────────────────────
static Value builtin_show(int argc, Value* args, void* ctx) {
    (void)ctx;
    for (int i = 0; i < argc; i++) { if (i>0) putchar(' '); value_print(args[i], false); }
    putchar('\n'); fflush(stdout); return val_null();
}
static Value builtin_print(int argc, Value* args, void* ctx) {
    (void)ctx;
    for (int i = 0; i < argc; i++) value_print(args[i], false);
    fflush(stdout); return val_null();
}
static Value builtin_eprint(int argc, Value* args, void* ctx) {
    (void)ctx;
    for (int i = 0; i < argc; i++) { if (i>0) fputc(' ', stderr); value_print(args[i], false); }
    fputc('\n', stderr); fflush(stderr); return val_null();
}
static Value builtin_input(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc >= 1) { value_print(args[0], false); fflush(stdout); }
    char buf[8192];
    if (!fgets(buf, sizeof(buf), stdin)) return val_string("");
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
    if (len > 0 && buf[len-1] == '\r') buf[--len] = '\0';
    return val_string_len(buf, len);
}
static Value builtin_exit(int argc, Value* args, void* ctx) {
    (void)ctx;
    int code = 0;
    if (argc >= 1 && args[0].kind == VAL_INT) code = (int)args[0].as.int_val;
    exit(code); return val_null();
}

// ── Type conversion ────────────────────────────────────────────────────
static Value builtin_str(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_string("");
    char* s = value_to_string(args[0]);
    Value v = val_string(s); free(s); return v;
}
static Value builtin_int(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_int(0);
    Value v = args[0];
    if (v.kind == VAL_INT) return value_copy(v);
    if (v.kind == VAL_FLOAT) return val_int((int64_t)v.as.float_val);
    if (v.kind == VAL_BOOL) return val_int(v.as.bool_val ? 1 : 0);
    if (v.kind == VAL_STRING && v.as.str) {
        char* end; long long n = strtoll(v.as.str->data, &end, 10); return val_int((int64_t)n);
    }
    if (v.kind == VAL_CHAR) return val_int((int64_t)(unsigned char)v.as.char_val);
    return val_int(0);
}
static Value builtin_float(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_float(0);
    Value v = args[0];
    if (v.kind == VAL_FLOAT) return value_copy(v);
    if (v.kind == VAL_INT) return val_float((double)v.as.int_val);
    if (v.kind == VAL_STRING && v.as.str) {
        char* end; double d = strtod(v.as.str->data, &end); return val_float(d);
    }
    if (v.kind == VAL_BOOL) return val_float(v.as.bool_val ? 1.0 : 0.0);
    return val_float(0);
}
static Value builtin_bool(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_bool(false);
    return val_bool(value_truthy(args[0]));
}
static Value builtin_char(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_char(0);
    if (args[0].kind == VAL_INT) return val_char((char)args[0].as.int_val);
    if (args[0].kind == VAL_STRING && args[0].as.str && args[0].as.str->length > 0)
        return val_char(args[0].as.str->data[0]);
    return val_char(0);
}
static Value builtin_chr(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "chr: expected codepoint");
    if (args[0].kind != VAL_INT) return val_err(val_string("chr: expected Int"));
    return val_char((char)args[0].as.int_val);
}
static Value builtin_ord(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "ord: expected Char");
    if (args[0].kind != VAL_CHAR) return val_err(val_string("ord: expected Char"));
    return val_int((int64_t)(unsigned char)args[0].as.char_val);
}

// ── Collections ────────────────────────────────────────────────────────
static Value builtin_len(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "len: expected argument");
    Value v = args[0];
    switch (v.kind) {
        case VAL_STRING: return val_int((int64_t)(v.as.str ? v.as.str->length : 0));
        case VAL_LIST: case VAL_TUPLE: return val_int((int64_t)LIST_COUNT(v));
        case VAL_MAP: return val_int((int64_t)(v.as.map ? v.as.map->count : 0));
        default: return val_int(0);
    }
}
static Value builtin_push(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "push(list, item)");
    if (args[0].kind != VAL_LIST) return val_err(val_string("push: expected list"));
    list_push(&args[0], value_copy(args[1]));
    return value_copy(args[0]);
}
static Value builtin_pop(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "pop(list)");
    if (args[0].kind != VAL_LIST) return val_err(val_string("pop: expected list"));
    if (LIST_COUNT(args[0]) == 0) return val_err(val_string("pop from empty list"));
    return list_pop(&args[0]);
}
static Value builtin_insert(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(3, "insert(list, idx, item)");
    if (args[0].kind != VAL_LIST) return val_err(val_string("insert: expected list"));
    if (args[1].kind != VAL_INT) return val_err(val_string("insert: expected int index"));
    ObjList* l = args[0].as.list;
    int idx = (int)args[1].as.int_val;
    if (idx < 0) idx += l->count;
    if (idx < 0) idx = 0;
    if (idx > l->count) idx = l->count;
    if (l->count >= l->capacity) {
        l->capacity = l->capacity == 0 ? 8 : l->capacity * 2;
        l->items = kodas_realloc(l->items, l->capacity * sizeof(Value));
    }
    for (int i = l->count; i > idx; i--) l->items[i] = l->items[i-1];
    l->items[idx] = value_copy(args[2]);
    l->count++;
    return value_copy(args[0]);
}
static Value builtin_remove(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "remove(list, idx)");
    if (args[0].kind != VAL_LIST) return val_err(val_string("remove: expected list"));
    if (args[1].kind != VAL_INT) return val_err(val_string("remove: expected int index"));
    ObjList* l = args[0].as.list;
    int idx = (int)args[1].as.int_val;
    if (idx < 0) idx += l->count;
    if (idx < 0 || idx >= l->count) return val_err(val_string("remove: index out of range"));
    Value removed = l->items[idx];
    for (int i = idx; i < l->count - 1; i++) l->items[i] = l->items[i+1];
    l->count--;
    return removed;
}
static Value builtin_range(int argc, Value* args, void* ctx) {
    (void)ctx;
    int64_t start = 0, end = 0, step = 1;
    if (argc == 1) { end = args[0].kind == VAL_INT ? args[0].as.int_val : (int64_t)AS_DBL(args[0]); }
    else if (argc >= 2) {
        start = args[0].kind == VAL_INT ? args[0].as.int_val : (int64_t)AS_DBL(args[0]);
        end = args[1].kind == VAL_INT ? args[1].as.int_val : (int64_t)AS_DBL(args[1]);
    }
    if (argc >= 3) step = args[2].kind == VAL_INT ? args[2].as.int_val : (int64_t)AS_DBL(args[2]);
    if (step == 0) step = 1;
    Value list = val_list();
    if (step > 0) { for (int64_t i = start; i < end; i += step) list_push(&list, val_int(i)); }
    else { for (int64_t i = start; i > end; i += step) list_push(&list, val_int(i)); }
    return list;
}
static Value builtin_reverse(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "reverse: expected list");
    if (args[0].kind != VAL_LIST) return val_err(val_string("reverse: expected list"));
    ObjList* l = args[0].as.list;
    for (int i = 0; i < l->count / 2; i++) {
        Value tmp = l->items[i];
        l->items[i] = l->items[l->count - 1 - i];
        l->items[l->count - 1 - i] = tmp;
    }
    return value_copy(args[0]);
}
static int val_cmp_qsort(const void* a, const void* b) {
    return value_compare(*(const Value*)a, *(const Value*)b);
}
static Value builtin_sort(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "sort: expected list");
    if (args[0].kind != VAL_LIST) return val_err(val_string("sort: expected list"));
    ObjList* l = args[0].as.list;
    if (l->count > 1) qsort(l->items, (size_t)l->count, sizeof(Value), val_cmp_qsort);
    return value_copy(args[0]);
}
static Value builtin_enumerate(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "enumerate: expected list");
    if (args[0].kind != VAL_LIST) return val_err(val_string("enumerate: expected list"));
    Value r = val_list();
    ObjList* l = args[0].as.list;
    for (int i = 0; i < l->count; i++) {
        Value pair[2] = {val_int(i), value_copy(l->items[i])};
        list_push(&r, val_tuple(pair, 2));
        value_free(&pair[0]); value_free(&pair[1]);
    }
    return r;
}
static Value builtin_zip(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "zip(a, b)");
    if (args[0].kind != VAL_LIST || args[1].kind != VAL_LIST) return val_err(val_string("zip: expected two lists"));
    ObjList *a = args[0].as.list, *b = args[1].as.list;
    int n = a->count < b->count ? a->count : b->count;
    Value r = val_list();
    for (int i = 0; i < n; i++) {
        Value pair[2] = {value_copy(a->items[i]), value_copy(b->items[i])};
        list_push(&r, val_tuple(pair, 2));
        value_free(&pair[0]); value_free(&pair[1]);
    }
    return r;
}
static Value builtin_keys(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "keys: expected map");
    if (args[0].kind != VAL_MAP) return val_err(val_string("keys: expected map"));
    ObjMap* m = args[0].as.map;
    Value r = val_list();
    for (int i = 0; i < m->count; i++) list_push(&r, val_string(m->keys[i]));
    return r;
}
static Value builtin_values(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "values: expected map");
    if (args[0].kind != VAL_MAP) return val_err(val_string("values: expected map"));
    ObjMap* m = args[0].as.map;
    Value r = val_list();
    for (int i = 0; i < m->count; i++) list_push(&r, value_copy(m->values[i]));
    return r;
}
static Value builtin_list_get(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "list_get(list, idx)");
    if (args[0].kind != VAL_LIST) return val_err(val_string("list_get: expected list"));
    if (args[1].kind != VAL_INT) return val_err(val_string("list_get: expected int"));
    return list_get(args[0], (int)args[1].as.int_val);
}
static Value builtin_list_set(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(3, "list_set(list, idx, val)");
    if (args[0].kind != VAL_LIST) return val_err(val_string("list_set: expected list"));
    if (args[1].kind != VAL_INT) return val_err(val_string("list_set: expected int"));
    list_set(args[0], (int)args[1].as.int_val, value_copy(args[2]));
    return value_copy(args[0]);
}
static Value builtin_deep_copy(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "deep_copy: expected value");
    return value_clone(args[0]);
}

// ── Strings ────────────────────────────────────────────────────────────
static Value builtin_split(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "split(str, delim)");
    if (args[0].kind != VAL_STRING) return val_err(val_string("split: expected string"));
    const char* s = STR(0);
    char* d = value_to_string(args[1]);
    size_t dl = strlen(d);
    Value r = val_list();
    if (dl == 0) { free(d); list_push(&r, val_string(s)); return r; }
    const char* start = s;
    while (*start) {
        const char* found = strstr(start, d);
        if (!found) { list_push(&r, val_string(start)); break; }
        list_push(&r, val_string_len(start, found - start));
        start = found + dl;
    }
    free(d);
    return r;
}
static Value builtin_contains(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "contains(haystack, needle)");
    if (args[0].kind == VAL_STRING && args[1].kind == VAL_STRING) {
        return val_bool(strstr(STR(0), STR(1)) != NULL);
    }
    if (args[0].kind == VAL_LIST) {
        for (int i = 0; i < LIST_COUNT(args[0]); i++)
            if (value_equals(args[0].as.list->items[i], args[1])) return val_bool(true);
    }
    if (args[0].kind == VAL_MAP && args[1].kind == VAL_STRING) {
        return val_bool(map_has(args[0], STR(1)));
    }
    return val_bool(false);
}
static Value builtin_chars(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "chars: expected string");
    if (args[0].kind != VAL_STRING) return val_err(val_string("chars: expected string"));
    Value r = val_list();
    const char* s = STR(0); size_t len = STR_LEN(0);
    for (size_t i = 0; i < len; i++) list_push(&r, val_char(s[i]));
    return r;
}
static Value builtin_lines(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "lines: expected string");
    if (args[0].kind != VAL_STRING) return val_err(val_string("lines: expected string"));
    Value r = val_list();
    const char* s = STR(0); const char* start = s;
    const char* end = s + STR_LEN(0);
    for (const char* p = s; p <= end; p++) {
        if (p == end || *p == '\n') {
            const char* le = p;
            if (le > start && *(le-1) == '\r') le--;
            list_push(&r, val_string_len(start, le - start));
            start = p + 1;
        }
    }
    return r;
}
static Value builtin_trim(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "trim: expected string");
    if (args[0].kind != VAL_STRING) return val_string("");
    const char* s = STR(0); size_t len = STR_LEN(0);
    size_t a = 0, b = len;
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b-1])) b--;
    return val_string_len(s + a, b - a);
}
static Value builtin_starts_with(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "starts_with(s, prefix)");
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) return val_bool(false);
    size_t la = STR_LEN(0), lb = STR_LEN(1);
    if (lb > la) return val_bool(false);
    return val_bool(memcmp(STR(0), STR(1), lb) == 0);
}
static Value builtin_ends_with(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "ends_with(s, suffix)");
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) return val_bool(false);
    size_t la = STR_LEN(0), lb = STR_LEN(1);
    if (lb > la) return val_bool(false);
    return val_bool(memcmp(STR(0) + la - lb, STR(1), lb) == 0);
}
static Value builtin_replace(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(3, "replace(s, from, to)");
    if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING || args[2].kind != VAL_STRING)
        return val_err(val_string("replace: expected 3 strings"));
    const char* s = STR(0); const char* from = STR(1); const char* to = STR(2);
    size_t lf = STR_LEN(1), lt = STR_LEN(2), ls = STR_LEN(0);
    if (lf == 0) return val_string(s);
    int count = 0;
    for (const char* p = s; ls - (size_t)(p - s) >= lf && (p = strstr(p, from)) != NULL; p += lf) count++;
    char* out = kodas_malloc(ls + count * (lt - lf) + 1);
    char* o = out;
    const char* cur = s;
    const char* end = s + ls;
    while (cur < end) {
        if ((size_t)(end - cur) >= lf && strncmp(cur, from, lf) == 0) {
            memcpy(o, to, lt); o += lt; cur += lf;
        } else { *o++ = *cur++; }
    }
    *o = '\0';
    return val_string_take(out);
}
static Value builtin_utf8(int argc, Value* args, void* ctx) {
    (void)ctx;
    // Treat bytes as codepoints for now (ASCII mode)
    return builtin_chars(argc, args, ctx);
}
static Value builtin_format(int argc, Value* args, void* ctx) {
    (void)ctx;
    // Simple format: joins all arguments with space (printf-style formatting planned)
    char* result = NULL; size_t cap = 0, len = 0;
    #define FAPP(str, slen) do { \
        if (len + (slen) + 1 > cap) { cap = cap?cap*2:64; if (cap < len+(slen)+1) cap = len+(slen)+1; result = kodas_realloc(result, cap); } \
        memcpy(result+len, (str), (slen)); len += (slen); result[len]='\0'; \
    } while(0)
    result = kodas_malloc(1); result[0] = '\0'; cap = 1; len = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) FAPP(" ", 1);
        char* s = value_to_string(args[i]);
        size_t sl = strlen(s);
        FAPP(s, sl);
        free(s);
    }
    #undef FAPP
    Value v = val_string_take(result);
    return v;
}

// ── Math ───────────────────────────────────────────────────────────────
static Value math_unary(int argc, Value* args, double (*fn)(double)) {
    if (argc < 1) return val_float(0);
    if (!IS_NUM(args[0])) return val_err(val_string("math: expected number"));
    return val_float(fn(AS_DBL(args[0])));
}
static Value builtin_sqrt(int argc, Value* args, void* ctx) { (void)ctx; return math_unary(argc, args, sqrt); }
static Value builtin_sin(int argc, Value* args, void* ctx)  { (void)ctx; return math_unary(argc, args, sin); }
static Value builtin_cos(int argc, Value* args, void* ctx)  { (void)ctx; return math_unary(argc, args, cos); }
static Value builtin_tan(int argc, Value* args, void* ctx)  { (void)ctx; return math_unary(argc, args, tan); }
static Value builtin_log(int argc, Value* args, void* ctx)  { (void)ctx; return math_unary(argc, args, log); }
static Value builtin_exp(int argc, Value* args, void* ctx)  { (void)ctx; return math_unary(argc, args, exp); }
static Value builtin_floor(int argc, Value* args, void* ctx) {
    (void)ctx; if (argc<1) return val_float(0);
    if (!IS_NUM(args[0])) return val_err(val_string("floor: expected number"));
    double d = floor(AS_DBL(args[0]));
    if (args[0].kind == VAL_INT) return value_copy(args[0]);
    return val_float(d);
}
static Value builtin_ceil(int argc, Value* args, void* ctx) {
    (void)ctx; if (argc<1) return val_float(0);
    if (!IS_NUM(args[0])) return val_err(val_string("ceil: expected number"));
    return val_float(ceil(AS_DBL(args[0])));
}
static Value builtin_round(int argc, Value* args, void* ctx) {
    (void)ctx; if (argc<1) return val_float(0);
    if (!IS_NUM(args[0])) return val_err(val_string("round: expected number"));
    double d = round(AS_DBL(args[0]));
    if (args[0].kind == VAL_INT) return value_copy(args[0]);
    return val_float(d);
}
static Value builtin_pow(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "pow(base, exp)");
    if (!IS_NUM(args[0]) || !IS_NUM(args[1])) return val_err(val_string("pow: expected numbers"));
    return val_float(pow(AS_DBL(args[0]), AS_DBL(args[1])));
}
static Value builtin_abs(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "abs: expected number");
    if (args[0].kind == VAL_INT) {
        int64_t x = args[0].as.int_val;
        return val_int(x < 0 ? -x : x);
    }
    if (args[0].kind == VAL_FLOAT) return val_float(fabs(args[0].as.float_val));
    return val_err(val_string("abs: expected number"));
}
static Value builtin_min(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc == 1 && args[0].kind == VAL_LIST) {
        ObjList* l = args[0].as.list;
        if (l->count == 0) return val_null();
        Value best = value_copy(l->items[0]);
        for (int i = 1; i < l->count; i++) {
            if (value_compare(l->items[i], best) < 0) {
                value_free(&best); best = value_copy(l->items[i]);
            }
        }
        return best;
    }
    if (argc < 2) return argc >= 1 ? value_copy(args[0]) : val_null();
    Value best = value_copy(args[0]);
    for (int i = 1; i < argc; i++) {
        if (value_compare(args[i], best) < 0) { value_free(&best); best = value_copy(args[i]); }
    }
    return best;
}
static Value builtin_max(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc == 1 && args[0].kind == VAL_LIST) {
        ObjList* l = args[0].as.list;
        if (l->count == 0) return val_null();
        Value best = value_copy(l->items[0]);
        for (int i = 1; i < l->count; i++) {
            if (value_compare(l->items[i], best) > 0) {
                value_free(&best); best = value_copy(l->items[i]);
            }
        }
        return best;
    }
    if (argc < 2) return argc >= 1 ? value_copy(args[0]) : val_null();
    Value best = value_copy(args[0]);
    for (int i = 1; i < argc; i++) {
        if (value_compare(args[i], best) > 0) { value_free(&best); best = value_copy(args[i]); }
    }
    return best;
}

// ── Reflection ─────────────────────────────────────────────────────────
static Value builtin_typename(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_string("null");
    switch (args[0].kind) {
        case VAL_NULL: return val_string("null");
        case VAL_INT: return val_string("Int");
        case VAL_FLOAT: return val_string("Float");
        case VAL_BOOL: return val_string("Bool");
        case VAL_STRING: return val_string("String");
        case VAL_CHAR: return val_string("Char");
        case VAL_LIST: return val_string("List");
        case VAL_MAP: return val_string("Map");
        case VAL_TUPLE: return val_string("Tuple");
        case VAL_SET: return val_string("Set");
        case VAL_CLOSURE: return val_string("Function");
        case VAL_NATIVE: return val_string("Function");
        case VAL_STRUCT_INST: return val_string(args[0].as.struct_inst->type_name);
        case VAL_ENUM_VARIANT: return val_string(args[0].as.variant->enum_name);
        case VAL_MAYBE: return val_string("maybe");
        case VAL_RESULT: return val_string("Result");
        case VAL_CHANNEL: return val_string("Channel");
        default: return val_string("?");
    }
}
static Value builtin_typeof(int argc, Value* args, void* ctx) { return builtin_typename(argc, args, ctx); }

static Value builtin_hash(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "hash: expected value");
    // Simple FNV-1a hash on string representation
    char* s = value_to_string(args[0]);
    uint64_t h = 1469598103934665603ULL;
    for (const char* p = s; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ULL; }
    free(s);
    return val_int((int64_t)h);
}

// ── Error handling ─────────────────────────────────────────────────────
static Value builtin_ok(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_ok(val_null());
    return val_ok(args[0]);
}
static Value builtin_err(int argc, Value* args, void* ctx) {
    (void)ctx;
    if (argc < 1) return val_err(val_string(""));
    return val_err(args[0]);
}
static Value builtin_is_ok(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "is_ok: expected Result");
    if (args[0].kind != VAL_RESULT) return val_bool(false);
    return val_bool(!args[0].is_error);
}
static Value builtin_is_err(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "is_err: expected Result");
    if (args[0].kind != VAL_RESULT) return val_bool(false);
    return val_bool(args[0].is_error);
}
static Value builtin_unwrap(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "unwrap: expected Result/maybe");
    if ((args[0].kind == VAL_RESULT || args[0].kind == VAL_MAYBE) && args[0].as.wrapped)
        return value_copy(*args[0].as.wrapped);
    return val_err(val_string("unwrap on empty/err"));
}
static Value builtin_unwrap_or(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "unwrap_or(val, default)");
    if ((args[0].kind == VAL_RESULT && !args[0].is_error && args[0].as.wrapped) ||
        (args[0].kind == VAL_MAYBE && args[0].as.wrapped))
        return value_copy(*args[0].as.wrapped);
    return value_copy(args[1]);
}

// ── Assertions ─────────────────────────────────────────────────────────
static Value builtin_assert(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "assert: expected condition");
    if (!value_truthy(args[0])) {
        fprintf(stderr, "  ✗ assertion failed");
        if (argc >= 2) { fprintf(stderr, ": "); value_print(args[1], false); }
        fprintf(stderr, "\n");
        return val_err(val_string("assertion failed"));
    }
    return val_bool(true);
}

// ── Files / system ─────────────────────────────────────────────────────
static Value builtin_read_file(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "read_file: expected path");
    if (args[0].kind != VAL_STRING) return val_err(val_string("read_file: expected string"));
    FILE* f = fopen(STR(0), "rb");
    if (!f) return val_err(val_string(strerror(errno)));
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = kodas_malloc(size + 1);
    size_t n = fread(buf, 1, size, f); buf[n] = '\0';
    fclose(f);
    // buf is consumed by val_string_take which copies, so we need val_string_len then free
    Value v = val_string_len(buf, n);
    free(buf);
    return val_ok(v);
}
static Value builtin_write_file(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "write_file(path, content)");
    if (args[0].kind != VAL_STRING) return val_err(val_string("write_file: expected string path"));
    char* content = value_to_string(args[1]);
    FILE* f = fopen(STR(0), "wb");
    if (!f) { free(content); return val_err(val_string(strerror(errno))); }
    fwrite(content, 1, strlen(content), f);
    fclose(f); free(content);
    return val_ok(val_bool(true));
}
static Value builtin_append_file(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(2, "append_file(path, content)");
    if (args[0].kind != VAL_STRING) return val_err(val_string("append_file: expected string"));
    char* content = value_to_string(args[1]);
    FILE* f = fopen(STR(0), "ab");
    if (!f) { free(content); return val_err(val_string(strerror(errno))); }
    fwrite(content, 1, strlen(content), f);
    fclose(f); free(content);
    return val_ok(val_bool(true));
}
static Value builtin_clock(int argc, Value* args, void* ctx) {
    (void)ctx; (void)argc; (void)args;
    return val_float(current_time_seconds_impl());
}
static Value builtin_sleep(int argc, Value* args, void* ctx) {
    (void)ctx; REQUIRE(1, "sleep(seconds)");
    double secs = AS_DBL(args[0]);
    struct timespec ts;
    ts.tv_sec = (time_t)secs;
    ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
    return val_null();
}

double current_time_seconds_impl(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
