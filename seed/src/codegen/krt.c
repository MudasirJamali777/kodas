/// krt.c - Kodas Runtime implementation
#include "codegen/krt.h"
#include <assert.h>
#include <alloca.h>
#include <time.h>

// Forward declarations
static KVal* k_str_concat3(const char* a, const char* b, const char* c);
KVal* k_errf(const char* fmt, ...);

__thread KFrame* k_current_frame = NULL;

// ── Panic / error handling ─────────────────────────────────────────────
void k_throw(KVal* err) {
    if (!k_current_frame) {
        fprintf(stderr, "fatal: uncaught error: ");
        k_eprint(err);
        exit(1);
    }
    k_current_frame->error = k_retain(err);
    longjmp(k_current_frame->buf, 1);
}

KVal* k_assert(KVal* cond, KVal* msg) {
    if (!k_truthy(cond)) {
        if (msg && msg->kind == K_STRING) k_throw(k_err(msg));
        else k_throw(k_err(k_str("assertion failed")));
    }
    return k_retain(cond);
}

// ── Memory helpers ─────────────────────────────────────────────────────
void* k_alloc(size_t n) {
    void* p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static char* k_strdup(const char* s) {
    size_t n = strlen(s);
    char* p = (char*)k_alloc(n+1);
    memcpy(p, s, n+1);
    return p;
}

// ── Allocation of KVal ─────────────────────────────────────────────────
KVal* k_new(KKind kind) {
    KVal* v = (KVal*)k_alloc(sizeof(KVal));
    v->kind = kind;
    v->is_error = false;
    v->refcount = 1;
    v->as.bool_v = false;
    return v;
}

KVal* k_null(void)   { return k_new(K_NULL); }
KVal* k_bool(bool b) { KVal* v = k_new(K_BOOL); v->as.bool_v = b; return v; }
KVal* k_int(int64_t i) { KVal* v = k_new(K_INT); v->as.int_v = i; return v; }
KVal* k_float(double d){ KVal* v = k_new(K_FLOAT); v->as.float_v = d; return v; }
KVal* k_str(const char* s) { return k_str_len(s, (int)strlen(s)); }

KVal* k_str_len(const char* s, int len) {
    KVal* v = k_new(K_STRING);
    char* p = (char*)k_alloc(len+1);
    memcpy(p, s, len); p[len] = '\0';
    v->as.string_v = p;
    return v;
}

KVal* k_char(char c) { KVal* v = k_new(K_CHAR); v->as.char_v = c; return v; }

// Type-as-value for static method dispatch (e.g. Point.new(...)).
// Name is a static string literal supplied by codegen, so no allocation/free.
KVal* k_type(const char* name) { KVal* v = k_new(K_TYPE); v->as.type_v = name; return v; }
const char* k_type_name(KVal* v) {
    switch (v->kind) {
        case K_NULL: return "null";
        case K_BOOL: return "bool";
        case K_INT: return "int";
        case K_FLOAT: return "float";
        case K_CHAR: return "char";
        case K_STRING: return "str";
        case K_LIST: return "list";
        case K_TUPLE: return "tuple";
        case K_MAP: return "map";
        case K_SET: return "set";
        case K_NATIVE: return "native";
        case K_CLOSURE: return "fn";
        case K_STRUCT: return v->as.struct_v->name;
        case K_ENUM: return v->as.enum_v->variant;
        case K_MAYBE: return v->is_error ? "empty" : "some";
        case K_RESULT: return v->is_error ? "err" : "ok";
        case K_MUT_REF: return "mut";
        case K_TYPE: return v->as.type_v;
        case K_RANGE: return "range";
        default: return "?";
    }
}

// Range (start..end / start..=end) — integer range for for-in loops.
KVal* k_range(int64_t start, int64_t end, bool inclusive) {
    KVal* v = k_new(K_RANGE);
    KRange* r = (KRange*)k_alloc(sizeof(KRange));
    r->start = start; r->end = end; r->inclusive = inclusive;
    v->as.range = r;
    return v;
}

// Sets are backed by a KMap from value -> true (using k_eq_deep).
KVal* k_set_empty(void) { return k_map_empty(); }
void k_set_add(KVal* set, KVal* v) { k_index_set(set, v, k_bool(true)); }

// Structural equality (for match literals / set membership).
bool k_eq_deep(KVal* a, KVal* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case K_NULL: return true;
        case K_BOOL: return a->as.bool_v == b->as.bool_v;
        case K_INT: return a->as.int_v == b->as.int_v;
        case K_FLOAT: return a->as.float_v == b->as.float_v;
        case K_CHAR: return a->as.char_v == b->as.char_v;
        case K_STRING: return strcmp(a->as.string_v, b->as.string_v) == 0;
        case K_LIST: case K_TUPLE: {
            KList* la = (a->kind==K_LIST)?a->as.list:a->as.tuple;
            KList* lb = (b->kind==K_LIST)?b->as.list:b->as.tuple;
            if (la->count != lb->count) return false;
            for (int i=0;i<la->count;i++) if (!k_eq_deep(la->items[i], lb->items[i])) return false;
            return true;
        }
        case K_STRUCT: {
            KStruct* sa = a->as.struct_v; KStruct* sb = b->as.struct_v;
            if (strcmp(sa->name, sb->name) != 0) return false;
            if (sa->field_count != sb->field_count) return false;
            for (int i=0;i<sa->field_count;i++) if (!k_eq_deep(sa->fields[i], sb->fields[i])) return false;
            return true;
        }
        case K_ENUM: {
            KEnum* ea = a->as.enum_v; KEnum* eb = b->as.enum_v;
            if (strcmp(ea->variant, eb->variant) != 0) return false;
            if (ea->payload_count != eb->payload_count) return false;
            for (int i=0;i<ea->payload_count;i++) if (!k_eq_deep(ea->payload[i], eb->payload[i])) return false;
            return true;
        }
        default: return false;
    }
}

KVal* k_list_empty(void) {
    KVal* v = k_new(K_LIST);
    KList* l = (KList*)k_alloc(sizeof(KList));
    l->count = 0; l->cap = 4;
    l->items = (KVal**)k_alloc(sizeof(KVal*)*l->cap);
    v->as.list = l;
    return v;
}

KVal* k_tuple_empty(void) {
    KVal* v = k_new(K_TUPLE);
    KList* l = (KList*)k_alloc(sizeof(KList));
    l->count = 0; l->cap = 0; l->items = NULL;
    v->as.tuple = l;
    return v;
}

KVal* k_tuple(int count, ...) {
    KVal* v = k_tuple_empty();
    v->as.tuple->cap = count;
    if (count) v->as.tuple->items = (KVal**)k_alloc(sizeof(KVal*)*count);
    va_list ap; va_start(ap, count);
    for (int i = 0; i < count; i++) {
        KVal* x = va_arg(ap, KVal*);
        v->as.tuple->items[i] = k_retain(x);
    }
    va_end(ap);
    v->as.tuple->count = count;
    return v;
}

KVal* k_list(int count, ...) {
    KVal* v = k_list_empty();
    va_list ap; va_start(ap, count);
    for (int i = 0; i < count; i++) {
        k_list_push(v, va_arg(ap, KVal*));
    }
    va_end(ap);
    return v;
}

void k_list_push(KVal* list, KVal* val) {
    if (list->kind != K_LIST) k_throw(k_err(k_str("push on non-list")));
    KList* l = list->as.list;
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap*2 : 4;
        l->items = (KVal**)realloc(l->items, sizeof(KVal*)*l->cap);
    }
    l->items[l->count++] = k_retain(val);
}

KVal* k_list_pop(KVal* list) {
    if (list->kind != K_LIST || list->as.list->count == 0)
        k_throw(k_err(k_str("pop from empty/non-list")));
    return list->as.list->items[--list->as.list->count];  // caller owns reference
}

static uint32_t k_hash(KVal* v) {
    // Simple hash: use pointer or int value
    if (v->kind == K_INT) return (uint32_t)v->as.int_v;
    if (v->kind == K_STRING) {
        uint32_t h = 2166136261u;
        for (const char* s = v->as.string_v; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
        return h;
    }
    if (v->kind == K_BOOL) return v->as.bool_v ? 1231 : 1237;
    return (uint32_t)(uintptr_t)v;
}

static bool k_equals(KVal* a, KVal* b);

static bool k_list_equals(KList* a, KList* b) {
    if (a->count != b->count) return false;
    for (int i = 0; i < a->count; i++) if (!k_equals(a->items[i], b->items[i])) return false;
    return true;
}

static bool k_equals(KVal* a, KVal* b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case K_NULL: return true;
        case K_BOOL: return a->as.bool_v == b->as.bool_v;
        case K_INT:  return a->as.int_v == b->as.int_v;
        case K_FLOAT: return a->as.float_v == b->as.float_v;
        case K_STRING: return strcmp(a->as.string_v, b->as.string_v) == 0;
        case K_CHAR: return a->as.char_v == b->as.char_v;
        case K_LIST: return k_list_equals(a->as.list, b->as.list);
        case K_TUPLE: return k_list_equals(a->as.tuple, b->as.tuple);
        case K_MAYBE:
        case K_RESULT:
            if (a->is_error != b->is_error) return false;
            if (a->is_error && b->is_error) return k_equals(a->as.wrapped, b->as.wrapped);
            if (!a->as.wrapped && !b->as.wrapped) return true;
            if (!a->as.wrapped || !b->as.wrapped) return false;
            return k_equals(a->as.wrapped, b->as.wrapped);
        default: return false;
    }
}

static KMap* k_map_alloc(void) {
    KMap* m = (KMap*)k_alloc(sizeof(KMap));
    m->count = 0; m->cap = 16;
    m->buckets = (KMapEntry**)calloc(m->cap, sizeof(KMapEntry*));
    return m;
}

KVal* k_map_empty(void) {
    KVal* v = k_new(K_MAP);
    v->as.map = k_map_alloc();
    return v;
}

static KMapEntry* k_map_lookup(KMap* m, KVal* key, KMapEntry*** out_bucket) {
    uint32_t h = k_hash(key) % m->cap;
    KMapEntry** b = &m->buckets[h];
    while (*b) {
        if (k_equals((*b)->key, key)) {
            if (out_bucket) *out_bucket = b;
            return *b;
        }
        b = &(*b)->next;
    }
    if (out_bucket) *out_bucket = b;
    return NULL;
}

static void k_map_set(KVal* map, KVal* key, KVal* val) {
    KMap* m = map->as.map;
    KMapEntry** b;
    KMapEntry* e = k_map_lookup(m, key, &b);
    if (e) {
        k_release(e->val);
        e->val = k_retain(val);
        k_release(key);
        return;
    }
    e = (KMapEntry*)k_alloc(sizeof(KMapEntry));
    e->key = k_retain(key);
    e->val = k_retain(val);
    e->next = NULL;
    *b = e;
    m->count++;
}

static KVal* k_map_get(KVal* map, KVal* key) {
    KMap* m = map->as.map;
    KMapEntry* e = k_map_lookup(m, key, NULL);
    if (!e) return k_null();
    return k_retain(e->val);
}

KVal* k_struct_new(const char* name, int field_count, const char** names, ...) {
    KVal* v = k_new(K_STRUCT);
    KStruct* s = (KStruct*)k_alloc(sizeof(KStruct));
    s->name = k_strdup(name);
    s->field_count = field_count;
    s->field_names = (const char**)k_alloc(sizeof(char*)*field_count);
    s->fields = (KVal**)k_alloc(sizeof(KVal*)*field_count);
    va_list ap; va_start(ap, names);
    for (int i = 0; i < field_count; i++) {
        s->field_names[i] = k_strdup(names[i]);
        s->fields[i] = k_retain(va_arg(ap, KVal*));
    }
    va_end(ap);
    v->as.struct_v = s;
    return v;
}

KVal* k_enum_new(const char* variant, int payload_count, ...) {
    KVal* v = k_new(K_ENUM);
    KEnum* e = (KEnum*)k_alloc(sizeof(KEnum));
    e->variant = k_strdup(variant);
    e->payload_count = payload_count;
    e->payload = payload_count ? (KVal**)k_alloc(sizeof(KVal*)*payload_count) : NULL;
    va_list ap; va_start(ap, payload_count);
    for (int i = 0; i < payload_count; i++) {
        e->payload[i] = k_retain(va_arg(ap, KVal*));
    }
    va_end(ap);
    v->as.enum_v = e;
    return v;
}

KVal* k_ok(KVal* w)    { KVal* v = k_new(K_RESULT); v->is_error = false; v->as.wrapped = k_retain(w); return v; }
KVal* k_err(KVal* w)   { KVal* v = k_new(K_RESULT); v->is_error = true;  v->as.wrapped = k_retain(w); return v; }
KVal* k_some(KVal* w)  { KVal* v = k_new(K_MAYBE);  v->is_error = false; v->as.wrapped = k_retain(w); return v; }
KVal* k_empty(void)    { KVal* v = k_new(K_MAYBE);  v->is_error = true;  v->as.wrapped = NULL; return v; }

KVal* k_closure(KClosureFn fn, int env_count, KVal** env, int arity, const char* name) {
    KVal* v = k_new(K_CLOSURE);
    KClosure_* c = (KClosure_*)k_alloc(sizeof(KClosure_));
    c->fn = fn; c->arity = arity; c->name = name ? k_strdup(name) : NULL;
    c->env_count = env_count;
    c->env = env_count ? (KVal**)k_alloc(sizeof(KVal*)*env_count) : NULL;
    for (int i = 0; i < env_count; i++) c->env[i] = k_retain(env[i]);
    v->as.closure = c;
    return v;
}

KVal* k_native(KNativeFn fn, const char* name) {
    KVal* v = k_new(K_NATIVE);
    KNative_* n = (KNative_*)k_alloc(sizeof(KNative_));
    n->fn = fn; n->name = name ? k_strdup(name) : NULL;
    v->as.native = n;
    return v;
}

KVal* k_mutref(KVal* inner) {
    KVal* v = k_new(K_MUT_REF);
    KMutRef* m = (KMutRef*)k_alloc(sizeof(KMutRef));
    m->inner = k_retain(inner);
    v->as.mutref = m;
    return v;
}

// ── Reference counting ─────────────────────────────────────────────────
KVal* k_retain(KVal* v) { if (v) v->refcount++; return v; }

static void k_free_deep(KVal* v) {
    if (!v) return;
    switch (v->kind) {
        case K_STRING: free(v->as.string_v); break;
        case K_LIST: {
            KList* l = v->as.list;
            if (l) { for (int i = 0; i < l->count; i++) k_release(l->items[i]); free(l->items); free(l); }
            break;
        }
        case K_TUPLE: {
            KList* l = v->as.tuple;
            if (l) { for (int i = 0; i < l->count; i++) k_release(l->items[i]); free(l->items); free(l); }
            break;
        }
        case K_MAP: {
            KMap* m = v->as.map;
            if (m) {
                for (int i = 0; i < m->cap; i++) {
                    KMapEntry* e = m->buckets[i];
                    while (e) {
                        KMapEntry* nx = e->next;
                        k_release(e->key); k_release(e->val);
                        free(e);
                        e = nx;
                    }
                }
                free(m->buckets); free(m);
            }
            break;
        }
        case K_STRUCT: {
            KStruct* s = v->as.struct_v;
            if (s) {
                for (int i = 0; i < s->field_count; i++) k_release(s->fields[i]);
                free(s->field_names); free(s->fields); free((void*)s->name); free(s);
            }
            break;
        }
        case K_ENUM: {
            KEnum* e = v->as.enum_v;
            if (e) {
                for (int i = 0; i < e->payload_count; i++) k_release(e->payload[i]);
                free(e->payload); free((void*)e->variant); free(e);
            }
            break;
        }
        case K_CLOSURE: {
            KClosure_* c = v->as.closure;
            if (c) {
                for (int i = 0; i < c->env_count; i++) k_release(c->env[i]);
                free(c->env); free((void*)c->name); free(c);
            }
            break;
        }
        case K_NATIVE: {
            KNative_* n = v->as.native;
            if (n) { free((void*)n->name); free(n); }
            break;
        }
        case K_TYPE:
            // type_v is a static string literal from codegen — nothing to free
            break;
        case K_RANGE:
            if (v->as.range) free(v->as.range);
            break;
        case K_MAYBE:
        case K_RESULT:
            if (v->as.wrapped) k_release(v->as.wrapped);
            break;
        case K_MUT_REF:
            if (v->as.mutref) { k_release(v->as.mutref->inner); free(v->as.mutref); }
            break;
        default: break;
    }
    free(v);
}

void k_release(KVal* v) {
    if (!v) return;
    if (--v->refcount == 0) k_free_deep(v);
}

void k_release_many(int count, ...) {
    va_list ap; va_start(ap, count);
    for (int i = 0; i < count; i++) k_release(va_arg(ap, KVal*));
    va_end(ap);
}

// ── Arithmetic & comparison ────────────────────────────────────────────
bool k_truthy(KVal* v) {
    if (!v) return false;
    if (v->kind == K_NULL) return false;
    if (v->kind == K_BOOL) return v->as.bool_v;
    if (v->kind == K_MAYBE && v->is_error) return false;  // empty
    if (v->kind == K_RESULT && v->is_error) return false;
    if (v->kind == K_INT) return v->as.int_v != 0;
    if (v->kind == K_FLOAT) return v->as.float_v != 0.0;
    return true;
}

KVal* k_neg(KVal* a) {
    if (a->kind == K_INT) return k_int(-a->as.int_v);
    if (a->kind == K_FLOAT) return k_float(-a->as.float_v);
    k_throw(k_err(k_str("unary - on non-number")));
    return k_null();
}

KVal* k_not(KVal* a) { return k_bool(!k_truthy(a)); }

#define NUMERIC_BINOP(NAME, OP, STR_OP) \
KVal* k_##NAME(KVal* a, KVal* b) { \
    if (a->kind == K_INT && b->kind == K_INT) return k_int(a->as.int_v OP b->as.int_v); \
    if ((a->kind == K_INT || a->kind == K_FLOAT) && (b->kind == K_INT || b->kind == K_FLOAT)) { \
        double av = (a->kind == K_FLOAT) ? a->as.float_v : (double)a->as.int_v; \
        double bv = (b->kind == K_FLOAT) ? b->as.float_v : (double)b->as.int_v; \
        return k_float(av OP bv); \
    } \
    k_throw(k_err(k_str("operator " STR_OP " on non-numeric types"))); \
    return k_null(); \
}

NUMERIC_BINOP(sub, -, "-")
NUMERIC_BINOP(mul, *, "*")
NUMERIC_BINOP(div, /, "/")

KVal* k_mod(KVal* a, KVal* b) {
    if (a->kind == K_INT && b->kind == K_INT) {
        if (b->as.int_v == 0) k_throw(k_err(k_str("division by zero")));
        return k_int(a->as.int_v % b->as.int_v);
    }
    if ((a->kind == K_INT || a->kind == K_FLOAT) && (b->kind == K_INT || b->kind == K_FLOAT)) {
        double av = (a->kind == K_FLOAT) ? a->as.float_v : (double)a->as.int_v;
        double bv = (b->kind == K_FLOAT) ? b->as.float_v : (double)b->as.int_v;
        if (bv == 0) k_throw(k_err(k_str("division by zero")));
        return k_float(fmod(av, bv));
    }
    k_throw(k_err(k_str("operator % on non-numeric types")));
    return k_null();
}

// String concatenation special-cased in k_add via wrapper
KVal* k_add(KVal* a, KVal* b) { return k_add_str(a,b); }

KVal* k_add_str(KVal* a, KVal* b) {
    if (a->kind == K_STRING && b->kind == K_STRING) {
        size_t la = strlen(a->as.string_v), lb = strlen(b->as.string_v);
        char* buf = (char*)k_alloc(la+lb+1);
        memcpy(buf, a->as.string_v, la);
        memcpy(buf+la, b->as.string_v, lb+1);
        KVal* v = k_new(K_STRING);
        v->as.string_v = buf;
        return v;
    }
    // Numeric add
    if (a->kind == K_INT && b->kind == K_INT) return k_int(a->as.int_v + b->as.int_v);
    if ((a->kind == K_INT || a->kind == K_FLOAT) && (b->kind == K_INT || b->kind == K_FLOAT)) {
        double av = (a->kind == K_FLOAT) ? a->as.float_v : (double)a->as.int_v;
        double bv = (b->kind == K_FLOAT) ? b->as.float_v : (double)b->as.int_v;
        return k_float(av + bv);
    }
    k_throw(k_err(k_str("operator + on incompatible types")));
    return k_null();
}

#define CMP_BINOP(NAME, OP) \
KVal* k_##NAME(KVal* a, KVal* b) { \
    if (a->kind == K_INT && b->kind == K_INT) return k_bool(a->as.int_v OP b->as.int_v); \
    if ((a->kind == K_INT || a->kind == K_FLOAT) && (b->kind == K_INT || b->kind == K_FLOAT)) { \
        double av = (a->kind == K_FLOAT) ? a->as.float_v : (double)a->as.int_v; \
        double bv = (b->kind == K_FLOAT) ? b->as.float_v : (double)b->as.int_v; \
        return k_bool(av OP bv); \
    } \
    if (a->kind == K_STRING && b->kind == K_STRING) return k_bool(strcmp(a->as.string_v, b->as.string_v) OP 0); \
    k_throw(k_err(k_str("comparison on incompatible types"))); \
    return k_null(); \
}

CMP_BINOP(lt, <)  CMP_BINOP(gt, >)  CMP_BINOP(lte, <=)  CMP_BINOP(gte, >=)

KVal* k_eq(KVal* a, KVal* b) { return k_bool(k_equals(a,b)); }
KVal* k_neq(KVal* a, KVal* b){ return k_bool(!k_equals(a,b)); }
KVal* k_and(KVal* a, KVal* b){ return k_bool(k_truthy(a) && k_truthy(b)); }
KVal* k_or(KVal* a, KVal* b) { return k_bool(k_truthy(a) || k_truthy(b)); }

// ── Calls ──────────────────────────────────────────────────────────────
KVal* k_call(KVal* fn, int argc, KVal** argv) {
    if (fn->kind == K_NATIVE && fn->as.native->fn) {
        return fn->as.native->fn(argc, argv);
    }
    if (fn->kind == K_CLOSURE && fn->as.closure->fn) {
        return fn->as.closure->fn(fn->as.closure->env, argc, argv);
    }
    k_throw(k_err(k_str("calling non-function")));
    return k_null();
}

// ── Accessors ──────────────────────────────────────────────────────────
static int k_find_field(KStruct* s, const char* name) {
    for (int i = 0; i < s->field_count; i++) if (strcmp(s->field_names[i], name) == 0) return i;
    return -1;
}

KVal* k_get_field(KVal* obj, const char* name) {
    if (obj->kind == K_STRUCT) {
        int i = k_find_field(obj->as.struct_v, name);
        if (i < 0) k_throw(k_errf("struct has no field '%s'", name));
        return k_retain(obj->as.struct_v->fields[i]);
    }
    if (obj->kind == K_MUT_REF) return k_get_field(obj->as.mutref->inner, name);
    k_throw(k_errf("cannot access field '%s' on non-struct", name));
    return k_null();
}

void k_set_field(KVal* obj, const char* name, KVal* val) {
    if (obj->kind == K_MUT_REF) { k_set_field(obj->as.mutref->inner, name, val); return; }
    if (obj->kind != K_STRUCT) k_throw(k_err(k_str("field assign on non-struct")));
    int i = k_find_field(obj->as.struct_v, name);
    if (i < 0) k_throw(k_errf("struct has no field '%s'", name));
    k_release(obj->as.struct_v->fields[i]);
    obj->as.struct_v->fields[i] = k_retain(val);
}

KVal* k_index(KVal* col, KVal* key) {
    if (col->kind == K_LIST || col->kind == K_TUPLE) {
        if (key->kind != K_INT) k_throw(k_err(k_str("index must be int")));
        int i = (int)key->as.int_v;
        KList* l = col->kind == K_LIST ? col->as.list : col->as.tuple;
        if (i < 0 || i >= l->count) k_throw(k_err(k_str("index out of bounds")));
        return k_retain(l->items[i]);
    }
    if (col->kind == K_MAP) return k_map_get(col, key);
    if (col->kind == K_STRING && key->kind == K_INT) {
        int i = (int)key->as.int_v;
        const char* s = col->as.string_v;
        int n = (int)strlen(s);
        if (i < 0 || i >= n) k_throw(k_err(k_str("string index out of bounds")));
        return k_char(s[i]);
    }
    k_throw(k_err(k_str("index on non-indexable type")));
    return k_null();
}

void k_index_set(KVal* col, KVal* key, KVal* val) {
    if (col->kind == K_LIST) {
        if (key->kind != K_INT) k_throw(k_err(k_str("index must be int")));
        int i = (int)key->as.int_v;
        KList* l = col->as.list;
        if (i < 0 || i >= l->count) k_throw(k_err(k_str("index out of bounds")));
        k_release(l->items[i]);
        l->items[i] = k_retain(val);
        return;
    }
    if (col->kind == K_MAP) { k_map_set(col, key, val); return; }
    k_throw(k_err(k_str("index set on non-mutable collection")));
}

int k_len(KVal* v) {
    switch (v->kind) {
        case K_STRING: return (int)strlen(v->as.string_v);
        case K_LIST: return v->as.list->count;
        case K_TUPLE: return v->as.tuple->count;
        case K_MAP: return v->as.map->count;
        default: k_throw(k_err(k_str("len() on unsupported type"))); return 0;
    }
}

// ── Show / print ───────────────────────────────────────────────────────
static void k_show_into(KVal* v, FILE* f) {
    if (!v) { fputs("null", f); return; }
    switch (v->kind) {
        case K_NULL: fputs("null", f); break;
        case K_BOOL: fputs(v->as.bool_v ? "true" : "false", f); break;
        case K_INT: fprintf(f, "%lld", (long long)v->as.int_v); break;
        case K_FLOAT: fprintf(f, "%g", v->as.float_v); break;
        case K_STRING: fputs(v->as.string_v, f); break;
        case K_CHAR: fputc(v->as.char_v, f); break;
        case K_LIST: {
            fputc('[', f);
            for (int i = 0; i < v->as.list->count; i++) {
                if (i) fputs(", ", f);
                k_show_into(v->as.list->items[i], f);
            }
            fputc(']', f);
            break;
        }
        case K_TUPLE: {
            fputc('(', f);
            for (int i = 0; i < v->as.tuple->count; i++) {
                if (i) fputs(", ", f);
                k_show_into(v->as.tuple->items[i], f);
            }
            fputc(')', f);
            break;
        }
        case K_MAP: {
            fputc('{', f);
            int n = 0;
            KMap* m = v->as.map;
            for (int i = 0; i < m->cap; i++) {
                for (KMapEntry* e = m->buckets[i]; e; e = e->next) {
                    if (n++) fputs(", ", f);
                    k_show_into(e->key, f); fputs(": ", f); k_show_into(e->val, f);
                }
            }
            fputc('}', f);
            break;
        }
        case K_CLOSURE: fprintf(f, "<fn %s>", v->as.closure->name ? v->as.closure->name : "anon"); break;
        case K_NATIVE: fprintf(f, "<native %s>", v->as.native->name ? v->as.native->name : "?"); break;
        case K_STRUCT: {
            KStruct* s = v->as.struct_v;
            fprintf(f, "%s { ", s->name);
            for (int i = 0; i < s->field_count; i++) {
                if (i) fputs(", ", f);
                fprintf(f, "%s: ", s->field_names[i]);
                k_show_into(s->fields[i], f);
            }
            fputs(" }", f);
            break;
        }
        case K_ENUM: {
            KEnum* e = v->as.enum_v;
            fprintf(f, "%s", e->variant);
            if (e->payload_count) {
                fputc('(', f);
                for (int i = 0; i < e->payload_count; i++) {
                    if (i) fputs(", ", f);
                    k_show_into(e->payload[i], f);
                }
                fputc(')', f);
            }
            break;
        }
        case K_MAYBE:
            if (v->is_error) fputs("empty", f);
            else { fputs("some(", f); k_show_into(v->as.wrapped, f); fputc(')', f); }
            break;
        case K_RESULT:
            if (v->is_error) { fputs("err(", f); k_show_into(v->as.wrapped, f); fputc(')', f); }
            else { fputs("ok(", f); k_show_into(v->as.wrapped, f); fputc(')', f); }
            break;
        case K_MUT_REF:
            fputs("mut(", f); k_show_into(v->as.mutref->inner, f); fputc(')', f);
            break;
        case K_SET: fputs("<set>", f); break;
    }
}

KVal* k_show(KVal* v) {
    // Simple: use open_memstream if available, else grow a buffer manually.
    char* buf = NULL; size_t sz = 0;
    FILE* f = open_memstream(&buf, &sz);
    if (!f) {
        // fallback: static buffer, truncated
        static char sb[4096];
        f = fmemopen(sb, sizeof(sb)-1, "w");
        k_show_into(v, f);
        size_t n = ftell(f); sb[n] = '\0'; fclose(f);
        return k_str(sb);
    }
    k_show_into(v, f);
    fclose(f);
    KVal* r = k_str(buf ? buf : "");
    free(buf);
    return r;
}

static KVal* k_str_buf(const char* fmt, ...) {
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    return k_str_len(buf, n);
}

KVal* k_str_concat3(const char* a, const char* b, const char* c) {
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
    char* buf = (char*)k_alloc(la+lb+lc+1);
    memcpy(buf, a, la); memcpy(buf+la, b, lb); memcpy(buf+la+lb, c, lc);
    buf[la+lb+lc] = '\0';
    KVal* v = k_new(K_STRING); v->as.string_v = buf; return v;
}

KVal* k_errf(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return k_err(k_str(buf));
}

void k_print(KVal* v) { k_show_into(v, stdout); fputc('\n', stdout); }
void k_eprint(KVal* v){ k_show_into(v, stderr); fputc('\n', stderr); }

KVal* k_concat(KVal* a, KVal* b) { return k_add_str(a, b); }

KVal* k_to_str(KVal* v) {
    char buf[64];
    switch (v->kind) {
        case K_STRING: return k_retain(v);
        case K_INT: snprintf(buf, sizeof(buf), "%lld", (long long)v->as.int_v); return k_str(buf);
        case K_FLOAT: snprintf(buf, sizeof(buf), "%g", v->as.float_v); return k_str(buf);
        case K_BOOL: return k_str(v->as.bool_v ? "true" : "false");
        case K_NULL: return k_str("null");
        default: return k_show(v);
    }
}

KVal* k_to_int(KVal* v) {
    if (v->kind == K_INT) return k_retain(v);
    if (v->kind == K_FLOAT) return k_int((int64_t)v->as.float_v);
    if (v->kind == K_STRING) return k_int(strtoll(v->as.string_v, NULL, 10));
    if (v->kind == K_BOOL) return k_int(v->as.bool_v ? 1 : 0);
    k_throw(k_err(k_str("cannot convert to int")));
    return k_null();
}

KVal* k_to_float(KVal* v) {
    if (v->kind == K_FLOAT) return k_retain(v);
    if (v->kind == K_INT) return k_float((double)v->as.int_v);
    if (v->kind == K_STRING) return k_float(strtod(v->as.string_v, NULL));
    k_throw(k_err(k_str("cannot convert to float")));
    return k_null();
}

// ── Error pipes / unwrap ───────────────────────────────────────────────
KVal* k_pipe(KVal* v, KVal* fn) {
    // Error pipe: unwrap ok/just, call fn with inner value; short-circuit err/none; pass-through plain values.
    // Does NOT consume v (caller must release); returns a new retained ref.
    KVal* retained_v = k_retain(v);
    if ((retained_v->kind == K_RESULT || retained_v->kind == K_MAYBE) && retained_v->is_error) return retained_v;
    KVal* inner;
    if (retained_v->kind == K_RESULT || retained_v->kind == K_MAYBE) inner = k_retain(retained_v->as.wrapped ? retained_v->as.wrapped : k_null());
    else inner = k_retain(retained_v);
    KVal* args[1] = { inner };
    KVal* r = k_call(fn, 1, args);
    k_release(inner);
    k_release(retained_v);
    return r;
}

KVal* k_unwrap(KVal* v) {
    if (v->kind == K_RESULT) {
        if (v->is_error) k_throw(k_err(k_str("called unwrap on err")));
        return k_retain(v->as.wrapped ? v->as.wrapped : k_null());
    }
    if (v->kind == K_MAYBE) {
        if (v->is_error) k_throw(k_err(k_str("called unwrap on empty")));
        return k_retain(v->as.wrapped ? v->as.wrapped : k_null());
    }
    return k_retain(v);
}

KVal* k_unwrap_or(KVal* v, KVal* alt) {
    if ((v->kind == K_RESULT || v->kind == K_MAYBE) && v->is_error) return k_retain(alt);
    if (v->kind == K_RESULT || v->kind == K_MAYBE) return k_retain(v->as.wrapped ? v->as.wrapped : k_null());
    return k_retain(v);
}

// ── Iterators ──────────────────────────────────────────────────────────
KIter k_iter_new(KVal* col) {
    KIter it; it.col = col; it.idx = 0; return it;
}

KVal* k_iter_next(KIter* it) {
    if (!it->col) return NULL;
    if (it->col->kind == K_RANGE) {
        KRange* r = it->col->as.range;
        int64_t i = r->start + it->idx;
        bool done = r->inclusive ? (i > r->end) : (i >= r->end);
        if (done) return NULL;
        it->idx++;
        return k_int(i);
    }
    if (it->col->kind == K_LIST || it->col->kind == K_TUPLE) {
        KList* l = it->col->kind == K_LIST ? it->col->as.list : it->col->as.tuple;
        if (it->idx >= l->count) return NULL;
        return k_retain(l->items[it->idx++]);
    }
    if (it->col->kind == K_STRING) {
        const char* s = it->col->as.string_v;
        int n = (int)strlen(s);
        if (it->idx >= n) return NULL;
        return k_char(s[it->idx++]);
    }
    if (it->col->kind == K_MAP || it->col->kind == K_SET) {
        // For for-in over maps/sets in this sprint: snapshot keys on first call.
        // We stash the snapshot list in ->as.list (reinterpret as list) via idx=-1 trick.
        // Simpler approach: collect keys into an internal KList stored in a static pointer.
        // For simplicity, build the list once on first call using malloc'd storage
        // stored in col->as.mutref (abuse). To avoid mutating the map, we keep a temp
        // list in the iterator. Since KIter is passed by value on the codegen side,
        // we instead use a helper via a cached list pointer on the KMap/KSet via
        // (col->as.map) — but maps don't have a slot. Simplest: copy keys into a list
        // up front in codegen (for-in on map/set unsupported natively yet); return NULL.
        (void)it;
        return NULL;
    }
    return NULL;
}

// ── Method dispatch (used for `obj.method(...)`) ───────────────────────
// Forward declarations
static KVal* b_show(int argc, KVal** argv);
static KVal* b_str(int argc, KVal** argv);
static KVal* b_len(int argc, KVal** argv);
static KVal* b_push(int argc, KVal** argv);
static KVal* b_pop(int argc, KVal** argv);
static KVal* b_is_ok(int argc, KVal** argv);
static KVal* b_is_err(int argc, KVal** argv);
static KVal* b_is_empty(int argc, KVal** argv);
static KVal* b_unwrap(int argc, KVal** argv);
static KVal* b_unwrap_or(int argc, KVal** argv);
static KVal* b_to_int(int argc, KVal** argv);
static KVal* b_to_float(int argc, KVal** argv);
static KVal* b_keys(int argc, KVal** argv);
static KVal* b_values(int argc, KVal** argv);
static KVal* b_contains(int argc, KVal** argv);
static KVal* b_split(int argc, KVal** argv);
static KVal* b_trim(int argc, KVal** argv);
static KVal* b_chars(int argc, KVal** argv);
static KVal* b_lines(int argc, KVal** argv);
static KVal* b_get(int argc, KVal** argv);
static KVal* b_set(int argc, KVal** argv);
static KVal* b_abs(int argc, KVal** argv);
static KVal* b_min(int argc, KVal** argv);
static KVal* b_max(int argc, KVal** argv);
static KVal* b_sqrt(int argc, KVal** argv);
static KVal* b_sin(int argc, KVal** argv);
static KVal* b_cos(int argc, KVal** argv);
static KVal* b_tan(int argc, KVal** argv);
static KVal* b_log(int argc, KVal** argv);
static KVal* b_exp(int argc, KVal** argv);
static KVal* b_floor(int argc, KVal** argv);
static KVal* b_ceil(int argc, KVal** argv);
static KVal* b_round(int argc, KVal** argv);
static KVal* b_pow(int argc, KVal** argv);
static KVal* b_clock(int argc, KVal** argv);
static KVal* b_exit(int argc, KVal** argv);

typedef struct { const char* name; KNativeFn fn; } Method;

static Method g_methods[] = {
    {"show", b_show}, {"to_str", b_str}, {"str", b_str}, {"len", b_len},
    {"push", b_push}, {"pop", b_pop},
    {"is_ok", b_is_ok}, {"is_err", b_is_err}, {"is_empty", b_is_empty},
    {"unwrap", b_unwrap}, {"unwrap_or", b_unwrap_or},
    {"to_int", b_to_int}, {"to_float", b_to_float},
    {"keys", b_keys}, {"values", b_values}, {"contains", b_contains},
    {"split", b_split}, {"trim", b_trim}, {"chars", b_chars}, {"lines", b_lines},
    {"get", b_get}, {"set", b_set},
    {"abs", b_abs}, {"min", b_min}, {"max", b_max}, {"sqrt", b_sqrt},
    {"sin", b_sin}, {"cos", b_cos}, {"tan", b_tan}, {"log", b_log}, {"exp", b_exp},
    {"floor", b_floor}, {"ceil", b_ceil}, {"round", b_round}, {"pow", b_pow},
    {"clock", b_clock}, {"exit", b_exit},
};

KVal* k_method(KVal* obj, const char* name, int argc, KVal** argv) {
    // Static dispatch: TypeName.method(...) => call TypeName_method(argc, argv) without self
    if (obj->kind == K_TYPE) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s_%s", obj->as.type_v, name);
        KVal* fn = k_global_get(buf);
        if (fn && fn->kind == K_CLOSURE) {
            return k_call(fn, argc, argv);
        }
        k_throw(k_errf("unknown static method '%s.%s'", obj->as.type_v, name));
        return k_null();
    }
    // Dispatch: if obj is a struct, first check for a field that is callable (module-style: io.println(...))
    if (obj->kind == K_STRUCT) {
        int fi = k_find_field(obj->as.struct_v, name);
        if (fi >= 0) {
            KVal* fn = obj->as.struct_v->fields[fi];
            if (fn->kind == K_NATIVE || fn->kind == K_CLOSURE) {
                return k_call(fn, argc, argv);
            }
        }
        // Otherwise: look up global "TypeName_method" and pass self as first arg.
        char buf[512];
        snprintf(buf, sizeof(buf), "%s_%s", obj->as.struct_v->name, name);
        KVal* mfn = k_global_get(buf);
        if (mfn && mfn->kind == K_CLOSURE) {
            int targc = argc + 1;
            KVal** targv = (KVal**)alloca(sizeof(KVal*)*targc);
            targv[0] = obj;
            for (int i = 0; i < argc; i++) targv[i+1] = argv[i];
            KVal* r = k_call(mfn, targc, targv);
            return r;
        }
    }
    int targc = argc + 1;
    KVal** targv = (KVal**)alloca(sizeof(KVal*)*targc);
    targv[0] = obj;
    for (int i = 0; i < argc; i++) targv[i+1] = argv[i];
    for (int i = 0; i < (int)(sizeof(g_methods)/sizeof(*g_methods)); i++) {
        if (strcmp(g_methods[i].name, name) == 0) return g_methods[i].fn(targc, targv);
    }
    k_throw(k_errf("unknown method '%s'", name));
    return k_null();
}

// ── Native builtins ────────────────────────────────────────────────────
#define REQUIRE(n, msg) do { if (argc < (n)) k_throw(k_err(k_str(msg))); } while(0)
#define TYPE(i, k, msg) do { if ((i) >= argc || argv[(i)]->kind != (k)) k_throw(k_err(k_str(msg))); } while(0)
#define STR(i) argv[(i)]->as.string_v
#define INT(i) argv[(i)]->as.int_v
#define FLT(i) argv[(i)]->as.float_v

static KVal* b_show(int argc, KVal** argv) { REQUIRE(1, "show needs 1 arg"); k_print(argv[0]); return k_null(); }
static KVal* b_str(int argc, KVal** argv)  { REQUIRE(1, "str needs 1 arg"); return k_to_str(argv[0]); }
static KVal* b_len(int argc, KVal** argv)  { REQUIRE(1, "len needs 1 arg"); return k_int(k_len(argv[0])); }
static KVal* b_push(int argc, KVal** argv) { REQUIRE(2, "push needs 2 args"); k_list_push(argv[0], argv[1]); return k_retain(argv[0]); }
static KVal* b_pop(int argc, KVal** argv)  { REQUIRE(1, "pop needs 1 arg"); return k_list_pop(argv[0]); }
static KVal* b_is_ok(int argc, KVal** argv){ REQUIRE(1, "is_ok"); return k_bool(argv[0]->kind == K_RESULT && !argv[0]->is_error); }
static KVal* b_is_err(int argc, KVal** argv){REQUIRE(1,"is_err"); return k_bool(argv[0]->kind == K_RESULT && argv[0]->is_error); }
static KVal* b_is_empty(int argc, KVal** argv){REQUIRE(1,"is_empty"); return k_bool(argv[0]->kind == K_MAYBE && argv[0]->is_error); }
static KVal* b_unwrap(int argc, KVal** argv){ REQUIRE(1,"unwrap"); return k_unwrap(argv[0]); }
static KVal* b_unwrap_or(int argc, KVal** argv){ REQUIRE(2,"unwrap_or"); return k_unwrap_or(argv[0], argv[1]); }
static KVal* b_to_int(int argc, KVal** argv){ REQUIRE(1,"to_int"); return k_to_int(argv[0]); }
static KVal* b_to_float(int argc, KVal** argv){REQUIRE(1,"to_float"); return k_to_float(argv[0]); }

static KVal* b_get(int argc, KVal** argv) { REQUIRE(2,"get needs 2"); return k_index(argv[0], argv[1]); }
static KVal* b_set(int argc, KVal** argv) { REQUIRE(3,"set needs 3"); k_index_set(argv[0], argv[1], argv[2]); return k_retain(argv[0]); }

static KVal* b_keys(int argc, KVal** argv) {
    REQUIRE(1,"keys");
    if (argv[0]->kind != K_MAP) k_throw(k_err(k_str("keys on non-map")));
    KVal* out = k_list_empty();
    KMap* m = argv[0]->as.map;
    for (int i = 0; i < m->cap; i++) {
        for (KMapEntry* e = m->buckets[i]; e; e = e->next) k_list_push(out, e->key);
    }
    return out;
}
static KVal* b_values(int argc, KVal** argv) {
    REQUIRE(1,"values");
    if (argv[0]->kind != K_MAP) k_throw(k_err(k_str("values on non-map")));
    KVal* out = k_list_empty();
    KMap* m = argv[0]->as.map;
    for (int i = 0; i < m->cap; i++) {
        for (KMapEntry* e = m->buckets[i]; e; e = e->next) k_list_push(out, e->val);
    }
    return out;
}
static KVal* b_contains(int argc, KVal** argv) {
    REQUIRE(2,"contains");
    KVal* hay = argv[0], *needle = argv[1];
    if (hay->kind == K_STRING && needle->kind == K_STRING) {
        return k_bool(strstr(hay->as.string_v, needle->as.string_v) != NULL);
    }
    if (hay->kind == K_LIST) {
        KList* l = hay->as.list;
        for (int i = 0; i < l->count; i++) if (k_equals(l->items[i], needle)) return k_bool(true);
        return k_bool(false);
    }
    k_throw(k_err(k_str("contains on unsupported type"))); return k_null();
}
static KVal* b_chars(int argc, KVal** argv) {
    REQUIRE(1,"chars"); TYPE(0,K_STRING,"chars expects string");
    const char* s = STR(0);
    KVal* out = k_list_empty();
    for (const char* p = s; *p; p++) k_list_push(out, k_char(*p));
    return out;
}
static KVal* b_split(int argc, KVal** argv) {
    REQUIRE(2,"split"); TYPE(0,K_STRING,"split string"); TYPE(1,K_STRING,"split delim");
    const char* s = STR(0), *d = STR(1);
    size_t dl = strlen(d);
    KVal* out = k_list_empty();
    const char* p = s;
    while (*p) {
        const char* found = dl ? strstr(p, d) : NULL;
        if (!found) { k_list_push(out, k_str(p)); break; }
        k_list_push(out, k_str_len(p, (int)(found-p)));
        p = found + (dl ? dl : 1);
    }
    return out;
}
static KVal* b_lines(int argc, KVal** argv) { return b_split(2, (KVal*[]){argv[0], k_str("\n")}); }
static KVal* b_trim(int argc, KVal** argv) {
    REQUIRE(1,"trim"); TYPE(0,K_STRING,"trim expects string");
    const char* s = STR(0);
    while (*s && isspace((unsigned char)*s)) s++;
    const char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    return k_str_len(s, (int)(e-s));
}

static KVal* b_abs(int argc, KVal** argv) { REQUIRE(1,"abs"); KVal* x = argv[0];
    if (x->kind == K_INT) return k_int(x->as.int_v < 0 ? -x->as.int_v : x->as.int_v);
    if (x->kind == K_FLOAT) return k_float(fabs(x->as.float_v));
    k_throw(k_err(k_str("abs needs number"))); return k_null();
}
static KVal* b_min(int argc, KVal** argv) { REQUIRE(2,"min");
    if (argv[0]->kind == K_INT && argv[1]->kind == K_INT) return k_int(argv[0]->as.int_v < argv[1]->as.int_v ? INT(0) : INT(1));
    double a = (argv[0]->kind==K_FLOAT)?argv[0]->as.float_v:(double)argv[0]->as.int_v;
    double b = (argv[1]->kind==K_FLOAT)?argv[1]->as.float_v:(double)argv[1]->as.int_v;
    return k_float(a < b ? a : b);
}
static KVal* b_max(int argc, KVal** argv) { REQUIRE(2,"max");
    if (argv[0]->kind == K_INT && argv[1]->kind == K_INT) return k_int(argv[0]->as.int_v > argv[1]->as.int_v ? INT(0) : INT(1));
    double a = (argv[0]->kind==K_FLOAT)?argv[0]->as.float_v:(double)argv[0]->as.int_v;
    double b = (argv[1]->kind==K_FLOAT)?argv[1]->as.float_v:(double)argv[1]->as.int_v;
    return k_float(a > b ? a : b);
}
#define MATH1(NAME, FN) static KVal* b_##NAME(int argc, KVal** argv) { REQUIRE(1,#NAME); \
    double x = argv[0]->kind==K_FLOAT?argv[0]->as.float_v:(double)argv[0]->as.int_v; \
    return k_float(FN(x)); }
MATH1(sqrt, sqrt) MATH1(sin, sin) MATH1(cos, cos) MATH1(tan, tan)
MATH1(log, log) MATH1(exp, exp) MATH1(floor, floor) MATH1(ceil, ceil)
MATH1(round, round)
static KVal* b_pow(int argc, KVal** argv) { REQUIRE(2,"pow");
    double a = argv[0]->kind==K_FLOAT?argv[0]->as.float_v:(double)argv[0]->as.int_v;
    double b = argv[1]->kind==K_FLOAT?argv[1]->as.float_v:(double)argv[1]->as.int_v;
    return k_float(pow(a,b));
}
static KVal* b_clock(int argc, KVal** argv) { (void)argc;(void)argv;
    extern double k_current_time(void);
    return k_float(k_current_time());
}
static KVal* b_exit(int argc, KVal** argv) {
    int code = 0;
    if (argc >= 1 && argv[0]->kind == K_INT) code = (int)argv[0]->as.int_v;
    exit(code); return k_null();
}
// println: prints each argument with k_show followed by space then newline
static KVal* b_println(int argc, KVal** argv) {
    for (int i = 0; i < argc; i++) { if(i)fputc(' ',stdout);k_show_into(argv[i],stdout); }
    fputc('\n', stdout); fflush(stdout);
    return k_null();
}
static KVal* b_input(int argc, KVal** argv) {
    if (argc >= 1) { k_show_into(argv[0], stdout); fflush(stdout); }
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return k_str("");
    size_t n = strlen(buf); if(n && buf[n-1]=='\n') buf[n-1]=0;
    return k_str(buf);
}
// Higher-order list functions
static KVal* b_map(int argc, KVal** argv) {
    REQUIRE(2,"map(list, fn)");
    KVal* lst = argv[0]; KVal* fn = argv[1];
    if (lst->kind != K_LIST && lst->kind != K_TUPLE) k_throw(k_err(k_str("map: not list")));
    KList* l = (lst->kind==K_LIST)?lst->as.list:lst->as.tuple;
    KVal* out = k_list_empty();
    for (int i = 0; i < l->count; i++) {
        KVal* arg[1] = { l->items[i] };
        KVal* r = k_call(fn, 1, arg);
        k_list_push(out, r);
        k_release(r);
    }
    return out;
}
static KVal* b_filter(int argc, KVal** argv) {
    REQUIRE(2,"filter(list, fn)");
    KVal* lst = argv[0]; KVal* fn = argv[1];
    if (lst->kind != K_LIST && lst->kind != K_TUPLE) k_throw(k_err(k_str("filter: not list")));
    KList* l = (lst->kind==K_LIST)?lst->as.list:lst->as.tuple;
    KVal* out = k_list_empty();
    for (int i = 0; i < l->count; i++) {
        KVal* arg[1] = { l->items[i] };
        KVal* r = k_call(fn, 1, arg);
        bool keep = k_truthy(r);
        k_release(r);
        if (keep) k_list_push(out, l->items[i]);
    }
    return out;
}
static KVal* b_reduce(int argc, KVal** argv) {
    REQUIRE(3,"reduce(list, init, fn)");
    KVal* lst = argv[0]; KVal* acc = argv[1]; KVal* fn = argv[2];
    if (lst->kind != K_LIST && lst->kind != K_TUPLE) k_throw(k_err(k_str("reduce: not list")));
    KList* l = (lst->kind==K_LIST)?lst->as.list:lst->as.tuple;
    KVal* cur = k_retain(acc);
    for (int i = 0; i < l->count; i++) {
        KVal* arg[2] = { cur, l->items[i] };
        KVal* nx = k_call(fn, 2, arg);
        k_release(cur); cur = nx;
    }
    return cur;
}
static KVal* b_foreach(int argc, KVal** argv) {
    REQUIRE(2,"foreach(list, fn)");
    KVal* lst = argv[0]; KVal* fn = argv[1];
    if (lst->kind != K_LIST && lst->kind != K_TUPLE) k_throw(k_err(k_str("foreach: not list")));
    KList* l = (lst->kind==K_LIST)?lst->as.list:lst->as.tuple;
    for (int i = 0; i < l->count; i++) {
        KVal* arg[1] = { l->items[i] };
        KVal* r = k_call(fn, 1, arg);
        k_release(r);
    }
    return k_null();
}
// Introspection / type name
static KVal* b_type_name(int argc, KVal** argv) {
    REQUIRE(1,"type_name");
    return k_str(k_type_name(argv[0]));
}
static KVal* b_panic(int argc, KVal** argv) {
    if (argc >= 1) k_throw(k_err(k_retain(argv[0])));
    else k_throw(k_err(k_str("panic")));
    return k_null();
}
// Helper to build a module object (a struct-like KVal holding named functions)
#define MOD_MAX_FIELDS 64
KVal* k_module(const char* name) {
    KVal* v = k_new(K_STRUCT);
    KStruct* s = (KStruct*)k_alloc(sizeof(KStruct));
    s->name = name; s->field_count = 0;
    s->field_names = (const char**)k_alloc(sizeof(char*)*MOD_MAX_FIELDS);
    s->fields = (KVal**)k_alloc(sizeof(KVal*)*MOD_MAX_FIELDS);
    v->as.struct_v = s;
    return v;
}
void k_mod_set(KVal* mod, const char* name, KVal* val) {
    KStruct* s = mod->as.struct_v;
    if (s->field_count >= MOD_MAX_FIELDS) { fprintf(stderr,"module too full: %s\n",name); exit(1); }
    s->field_names[s->field_count] = k_strdup(name);
    s->fields[s->field_count] = k_retain(val);
    s->field_count++;
}

// ── Stdlib registration ────────────────────────────────────────────────
#define DEF_GLOBAL(name, fn) k_global_set((name), k_native((fn), (name)))

static KVal* b_assert_native(int argc, KVal** argv) {
    REQUIRE(1,"assert needs condition");
    if (!k_truthy(argv[0])) {
        KVal* msg = (argc >= 2) ? argv[1] : (KVal*)k_str("assertion failed");
        k_throw(k_err(k_retain(msg)));
    }
    return k_bool(true);
}
static KVal* b_ok(int argc, KVal** argv) {
    if (argc == 0) return k_ok(k_bool(true));
    return k_ok(argv[0]);
}
static KVal* b_err(int argc, KVal** argv) {
    if (argc == 0) return k_err(k_str("error"));
    return k_err(argv[0]);
}
static KVal* b_some(int argc, KVal** argv) {
    REQUIRE(1,"some needs value"); return k_some(argv[0]);
}
static KVal* b_empty_native(int argc, KVal** argv) { (void)argc;(void)argv; return k_empty(); }
static KVal* b_list_native(int argc, KVal** argv) {
    KVal* out = k_list_empty();
    for (int i = 0; i < argc; i++) k_list_push(out, argv[i]);
    return out;
}
static KVal* b_range(int argc, KVal** argv) {
    REQUIRE(2,"range(start,end)"); TYPE(0,K_INT,"range start int"); TYPE(1,K_INT,"range end int");
    int64_t s = INT(0), e = INT(1);
    KVal* out = k_list_empty();
    for (int64_t i = s; i < e; i++) k_list_push(out, k_int(i));
    return out;
}

// Global table
#define K_GLOBALS_MAX 4096
static struct { const char* name; KVal* val; } g_globals[K_GLOBALS_MAX];
static int g_globals_count = 0;

KVal* k_global_get(const char* name) {
    for (int i = 0; i < g_globals_count; i++) if (strcmp(g_globals[i].name, name) == 0) return g_globals[i].val;
    return NULL;
}
void k_global_set(const char* name, KVal* val) {
    // Check existing first
    for (int i = 0; i < g_globals_count; i++) {
        if (strcmp(g_globals[i].name, name) == 0) {
            k_release(g_globals[i].val);
            g_globals[i].val = k_retain(val);
            return;
        }
    }
    if (g_globals_count >= K_GLOBALS_MAX) { fprintf(stderr,"too many globals\n"); exit(1); }
    g_globals[g_globals_count].name = k_strdup(name);
    g_globals[g_globals_count].val = k_retain(val);
    g_globals_count++;
}

double k_current_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void k_register_stdlib(void) {
    DEF_GLOBAL("show", b_show);
    DEF_GLOBAL("print", b_show);
    DEF_GLOBAL("str", b_str);
    DEF_GLOBAL("to_str", b_str);
    DEF_GLOBAL("len", b_len);
    DEF_GLOBAL("push", b_push);
    DEF_GLOBAL("pop", b_pop);
    DEF_GLOBAL("range", b_range);
    DEF_GLOBAL("list", b_list_native);
    DEF_GLOBAL("assert", b_assert_native);
    DEF_GLOBAL("ok", b_ok);
    DEF_GLOBAL("err", b_err);
    DEF_GLOBAL("some", b_some);
    DEF_GLOBAL("empty", b_empty_native);
    DEF_GLOBAL("keys", b_keys);
    DEF_GLOBAL("values", b_values);
    DEF_GLOBAL("split", b_split);
    DEF_GLOBAL("contains", b_contains);
    DEF_GLOBAL("chars", b_chars);
    DEF_GLOBAL("lines", b_lines);
    DEF_GLOBAL("trim", b_trim);
    DEF_GLOBAL("to_int", b_to_int);
    DEF_GLOBAL("to_float", b_to_float);
    DEF_GLOBAL("abs", b_abs);
    DEF_GLOBAL("min", b_min);
    DEF_GLOBAL("max", b_max);
    DEF_GLOBAL("sqrt", b_sqrt);
    DEF_GLOBAL("sin", b_sin);
    DEF_GLOBAL("cos", b_cos);
    DEF_GLOBAL("tan", b_tan);
    DEF_GLOBAL("log", b_log);
    DEF_GLOBAL("exp", b_exp);
    DEF_GLOBAL("floor", b_floor);
    DEF_GLOBAL("ceil", b_ceil);
    DEF_GLOBAL("round", b_round);
    DEF_GLOBAL("pow", b_pow);
    DEF_GLOBAL("clock", b_clock);
    DEF_GLOBAL("exit", b_exit);
    DEF_GLOBAL("println", b_println);
    DEF_GLOBAL("input", b_input);
    DEF_GLOBAL("map", b_map);
    DEF_GLOBAL("filter", b_filter);
    DEF_GLOBAL("reduce", b_reduce);
    DEF_GLOBAL("foreach", b_foreach);
    DEF_GLOBAL("type_name", b_type_name);
    DEF_GLOBAL("panic", b_panic);
    // Module objects (io, math, str, list, ...)
    {
        KVal* io = k_module("io");
        k_mod_set(io, "println", k_native(b_println,"io.println"));
        k_mod_set(io, "print", k_native(b_show,"io.print"));
        k_mod_set(io, "input", k_native(b_input,"io.input"));
        k_global_set("io", io); k_release(io);
        KVal* m = k_module("math");
        k_mod_set(m, "sqrt", k_native(b_sqrt,"math.sqrt"));
        k_mod_set(m, "sin", k_native(b_sin,"math.sin"));
        k_mod_set(m, "cos", k_native(b_cos,"math.cos"));
        k_mod_set(m, "tan", k_native(b_tan,"math.tan"));
        k_mod_set(m, "log", k_native(b_log,"math.log"));
        k_mod_set(m, "exp", k_native(b_exp,"math.exp"));
        k_mod_set(m, "floor", k_native(b_floor,"math.floor"));
        k_mod_set(m, "ceil", k_native(b_ceil,"math.ceil"));
        k_mod_set(m, "round", k_native(b_round,"math.round"));
        k_mod_set(m, "pow", k_native(b_pow,"math.pow"));
        k_mod_set(m, "abs", k_native(b_abs,"math.abs"));
        k_mod_set(m, "min", k_native(b_min,"math.min"));
        k_mod_set(m, "max", k_native(b_max,"math.max"));
        k_global_set("math", m); k_release(m);
        // "strings" / "lists" modules — avoid clobbering top-level str()/list() functions.
        KVal* sm = k_module("strings");
        k_mod_set(sm, "from_int", k_native(b_str,"strings.from_int"));
        k_mod_set(sm, "split", k_native(b_split,"strings.split"));
        k_mod_set(sm, "contains", k_native(b_contains,"strings.contains"));
        k_mod_set(sm, "trim", k_native(b_trim,"strings.trim"));
        k_mod_set(sm, "chars", k_native(b_chars,"strings.chars"));
        k_mod_set(sm, "lines", k_native(b_lines,"strings.lines"));
        k_global_set("strings", sm); k_release(sm);
        KVal* lm = k_module("lists");
        k_mod_set(lm, "map", k_native(b_map,"lists.map"));
        k_mod_set(lm, "filter", k_native(b_filter,"lists.filter"));
        k_mod_set(lm, "reduce", k_native(b_reduce,"lists.reduce"));
        k_mod_set(lm, "foreach", k_native(b_foreach,"lists.foreach"));
        k_mod_set(lm, "push", k_native(b_push,"lists.push"));
        k_mod_set(lm, "pop", k_native(b_pop,"lists.pop"));
        k_mod_set(lm, "range", k_native(b_range,"lists.range"));
        k_global_set("lists", lm); k_release(lm);
        // Also aliased as list_mod via the `list` name: only if no list() global exists. For now, don't overwrite.
    }
    // Constants
    k_global_set("PI", k_float(3.14159265358979323846));
    k_global_set("E",  k_float(2.71828182845904523536));
    k_global_set("TAU",k_float(6.28318530717958647692));
}

// ── main glue ──────────────────────────────────────────────────────────
// When compiling krt.c into the GENERATED binary, define KRT_MAIN=1.
// Leave it off when linking krt.c into the compiler binary itself.
#ifdef KRT_MAIN
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    k_register_stdlib();
    extern void k_register_stdlib_extra(void);
    k_register_stdlib_extra();

    KFrame frame;
    frame.prev = k_current_frame;
    frame.error = NULL;
    k_current_frame = &frame;

    KVal* result = NULL;
    if (setjmp(frame.buf)) {
        // Caught panic from k_throw
        if (frame.error) {
            fprintf(stderr, "  ✗ runtime error: ");
            k_eprint(frame.error);
            k_release(frame.error);
        }
        k_current_frame = frame.prev;
        return 1;
    }
    KVal* args = k_list(0);
    result = k_entry(1, &args);
    k_release(args);
    if (result) k_release(result);
    k_current_frame = frame.prev;
    return 0;
}
#endif // KRT_MAIN
