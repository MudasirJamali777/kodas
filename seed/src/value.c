/// value.c - Kodas runtime values (refcounted heap objects)
#include "value.h"
#include "environment.h"
#include "ast.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

// ── Free functions for refcounted types ────────────────────────────────
static void obj_string_free(void* p) {
    ObjString* s = (ObjString*)p;
    free(s->data);
}
static void obj_list_free(void* p) {
    ObjList* l = (ObjList*)p;
    for (int i = 0; i < l->count; i++) value_free(&l->items[i]);
    free(l->items);
}
static void obj_map_free(void* p) {
    ObjMap* m = (ObjMap*)p;
    for (int i = 0; i < m->count; i++) {
        free(m->keys[i]);
        value_free(&m->values[i]);
    }
    free(m->keys);
    free(m->values);
}
static void obj_struct_free(void* p) {
    ObjStruct* s = (ObjStruct*)p;
    free(s->type_name);
    if (s->fields) obj_release(s->fields);
}
static void obj_enum_variant_free(ValueEnumVariant* v) {
    if (!v) return;
    free(v->enum_name);
    free(v->variant_name);
    if (v->payload) { value_free(v->payload); free(v->payload); }
    free(v);
}

// ── GC (no-op; refcounting manages memory in seed) ─────────────────────
void gc_init(void) {}
void gc_shutdown(void) {}

// ── Constructors ────────────────────────────────────────────────────────
Value val_null(void) { Value v; memset(&v, 0, sizeof(v)); v.kind = VAL_NULL; return v; }
Value val_int(int64_t x) { Value v; memset(&v, 0, sizeof(v)); v.kind = VAL_INT; v.as.int_val = x; return v; }
Value val_float(double x) { Value v; memset(&v, 0, sizeof(v)); v.kind = VAL_FLOAT; v.as.float_val = x; return v; }
Value val_bool(bool x) { Value v; memset(&v, 0, sizeof(v)); v.kind = VAL_BOOL; v.as.bool_val = x; return v; }
Value val_char(char x) { Value v; memset(&v, 0, sizeof(v)); v.kind = VAL_CHAR; v.as.char_val = x; return v; }
Value val_empty(void) { Value v; memset(&v, 0, sizeof(v)); v.kind = VAL_MAYBE; v.as.wrapped = NULL; return v; }

static ObjString* obj_string_new(const char* s, size_t len) {
    ObjString* o = (ObjString*)kodas_calloc(1, sizeof(ObjString));
    o->hdr.refcount = 1;
    o->hdr.free_fn = obj_string_free;
    o->length = len;
    o->data = (char*)kodas_malloc(len + 1);
    memcpy(o->data, s, len);
    o->data[len] = '\0';
    return o;
}

Value val_string(const char* s) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_STRING;
    v.as.str = obj_string_new(s, strlen(s));
    return v;
}
Value val_string_take(char* s) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_STRING;
    size_t len = strlen(s);
    v.as.str = obj_string_new(s, len);
    free(s);
    return v;
}
Value val_string_len(const char* s, size_t len) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_STRING;
    v.as.str = obj_string_new(s, len);
    return v;
}

static ObjList* obj_list_new(void) {
    ObjList* o = (ObjList*)kodas_calloc(1, sizeof(ObjList));
    o->hdr.refcount = 1;
    o->hdr.free_fn = obj_list_free;
    o->items = NULL;
    o->count = 0;
    o->capacity = 0;
    return o;
}

Value val_list(void) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_LIST;
    v.as.list = obj_list_new();
    return v;
}

Value val_list_from(Value* items, int count) {
    Value v = val_list();
    for (int i = 0; i < count; i++) list_push(&v, value_copy(items[i]));
    return v;
}

static ObjMap* obj_map_new(void) {
    ObjMap* o = (ObjMap*)kodas_calloc(1, sizeof(ObjMap));
    o->hdr.refcount = 1;
    o->hdr.free_fn = obj_map_free;
    return o;
}

Value val_map(void) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_MAP;
    v.as.map = obj_map_new();
    return v;
}

Value val_tuple(Value* items, int count) {
    // Tuples are immutable lists for now (same ObjList)
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_TUPLE;
    v.as.list = obj_list_new();
    for (int i = 0; i < count; i++) list_push(&v, value_copy(items[i]));
    return v;
}

Value val_closure(Closure* c) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_CLOSURE;
    v.as.closure = c;
    if (c) c->refcount++;
    return v;
}

Value val_native(NativeFn fn, const char* name, void* ctx) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_NATIVE;
    v.as.native.fn = fn;
    v.as.native.name = kodas_strdup(name);
    v.as.native.ctx = ctx;
    return v;
}

Value val_struct(const char* name) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_STRUCT_INST;
    v.as.struct_inst = (ObjStruct*)kodas_calloc(1, sizeof(ObjStruct));
    v.as.struct_inst->hdr.refcount = 1;
    v.as.struct_inst->hdr.free_fn = obj_struct_free;
    v.as.struct_inst->type_name = kodas_strdup(name);
    v.as.struct_inst->fields = obj_map_new();
    return v;
}

Value val_enum_variant(const char* enum_name, const char* var_name, Value* payload) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_ENUM_VARIANT;
    v.as.variant = (ValueEnumVariant*)kodas_calloc(1, sizeof(ValueEnumVariant));
    v.as.variant->refcount = 1;
    v.as.variant->enum_name = kodas_strdup(enum_name);
    v.as.variant->variant_name = kodas_strdup(var_name);
    v.as.variant->payload = payload ? (Value*)kodas_malloc(sizeof(Value)) : NULL;
    if (payload) *v.as.variant->payload = value_copy(*payload);
    return v;
}

Value val_ok(Value x) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_RESULT;
    v.is_error = false;
    v.as.wrapped = (Value*)kodas_malloc(sizeof(Value));
    *v.as.wrapped = value_copy(x);
    return v;
}

Value val_err(Value x) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_RESULT;
    v.is_error = true;
    v.as.wrapped = (Value*)kodas_malloc(sizeof(Value));
    *v.as.wrapped = value_copy(x);
    return v;
}

Value val_maybe_just(Value x) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_MAYBE;
    v.as.wrapped = (Value*)kodas_malloc(sizeof(Value));
    *v.as.wrapped = value_copy(x);
    return v;
}

Value val_channel(int send_fd, int recv_fd) {
    Value v; memset(&v, 0, sizeof(v));
    v.kind = VAL_CHANNEL;
    v.as.channel.send_fd = send_fd;
    v.as.channel.recv_fd = recv_fd;
    v.as.channel.closed = false;
    return v;
}

// ── String helpers ──────────────────────────────────────────────────────
const char* val_str_cstr(Value v) {
    return (v.kind == VAL_STRING && v.as.str) ? v.as.str->data : "";
}
int64_t val_str_len(Value v) {
    return (v.kind == VAL_STRING && v.as.str) ? (int64_t)v.as.str->length : 0;
}

// ── List operations ────────────────────────────────────────────────────
void list_push(Value* list, Value item) {
    if (list->kind != VAL_LIST && list->kind != VAL_TUPLE) return;
    ObjList* l = list->as.list;
    if (l->count >= l->capacity) {
        l->capacity = l->capacity == 0 ? 8 : l->capacity * 2;
        l->items = (Value*)kodas_realloc(l->items, l->capacity * sizeof(Value));
    }
    l->items[l->count++] = item;
}

Value list_pop(Value* list) {
    if (list->kind != VAL_LIST || !list->as.list || list->as.list->count == 0) return val_null();
    ObjList* l = list->as.list;
    return l->items[--l->count];
}

Value list_get(Value list, int idx) {
    if (list.kind != VAL_LIST && list.kind != VAL_TUPLE) return val_null();
    ObjList* l = list.as.list;
    if (idx < 0) idx += l->count;
    if (idx < 0 || idx >= l->count) return val_null();
    return value_copy(l->items[idx]);
}

void list_set(Value list, int idx, Value val) {
    if (list.kind != VAL_LIST || !list.as.list) return;
    ObjList* l = list.as.list;
    if (idx < 0) idx += l->count;
    if (idx < 0 || idx >= l->count) { value_free(&val); return; }
    value_free(&l->items[idx]);
    l->items[idx] = val;
}

int list_len(Value list) {
    if (!list.as.list) return 0;
    return list.as.list->count;
}

Value list_slice(Value list, int start, int end) {
    if (list.kind != VAL_LIST && list.kind != VAL_TUPLE) return val_list();
    ObjList* l = list.as.list;
    if (start < 0) start += l->count;
    if (end < 0) end += l->count;
    if (start < 0) start = 0;
    if (end > l->count) end = l->count;
    if (start >= end) return val_list();
    Value r = val_list();
    for (int i = start; i < end; i++) list_push(&r, value_copy(l->items[i]));
    return r;
}

// ── Map operations ─────────────────────────────────────────────────────
void map_set(Value* map, const char* key, Value val) {
    if (map->kind != VAL_MAP || !map->as.map) return;
    ObjMap* m = map->as.map;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->keys[i], key) == 0) {
            value_free(&m->values[i]);
            m->values[i] = val;
            return;
        }
    }
    if (m->count >= m->capacity) {
        m->capacity = m->capacity == 0 ? 8 : m->capacity * 2;
        m->keys = (char**)kodas_realloc(m->keys, m->capacity * sizeof(char*));
        m->values = (Value*)kodas_realloc(m->values, m->capacity * sizeof(Value));
    }
    m->keys[m->count] = kodas_strdup(key);
    m->values[m->count] = val;
    m->count++;
}

Value* map_get(Value map, const char* key) {
    if (map.kind != VAL_MAP || !map.as.map) return NULL;
    ObjMap* m = map.as.map;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->keys[i], key) == 0) return &m->values[i];
    }
    return NULL;
}

bool map_has(Value map, const char* key) { return map_get(map, key) != NULL; }

int map_len(Value map) { return map.as.map ? map.as.map->count : 0; }

void map_delete(Value* map, const char* key) {
    if (map->kind != VAL_MAP || !map->as.map) return;
    ObjMap* m = map->as.map;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->keys[i], key) == 0) {
            free(m->keys[i]);
            value_free(&m->values[i]);
            m->keys[i] = m->keys[m->count - 1];
            m->values[i] = m->values[m->count - 1];
            m->count--;
            return;
        }
    }
}

// ── Struct operations ──────────────────────────────────────────────────
void struct_set(Value* inst, const char* field, Value val) {
    if (inst->kind != VAL_STRUCT_INST || !inst->as.struct_inst) return;
    // Wrap in a Value for map_set
    Value mwrap; memset(&mwrap, 0, sizeof(mwrap));
    mwrap.kind = VAL_MAP;
    mwrap.as.map = inst->as.struct_inst->fields;
    map_set(&mwrap, field, val);
}

Value* struct_get(Value inst, const char* field) {
    if (inst.kind != VAL_STRUCT_INST || !inst.as.struct_inst) return NULL;
    Value mwrap; memset(&mwrap, 0, sizeof(mwrap));
    mwrap.kind = VAL_MAP;
    mwrap.as.map = inst.as.struct_inst->fields;
    return map_get(mwrap, field);
}

// ── Closure refcount ───────────────────────────────────────────────────
void closure_retain(Closure* c) { if (c) c->refcount++; }
void closure_release(Closure* c) {
    if (!c) return;
    if (--c->refcount > 0) return;
    free(c->name);
    for (int i = 0; i < c->params.count; i++) {
        free(c->params.items[i].name);
        free(c->params.items[i].type_ann);
    }
    free(c->params.items);
    // body and decl point into AST (freed at exit)
    free(c);
}

// ── value_to_string / print ─────────────────────────────────────────────
static void str_append(char** buf, size_t* cap, size_t* len, const char* s) {
    size_t sl = strlen(s);
    if (*len + sl + 1 > *cap) {
        *cap = (*cap == 0 ? 64 : *cap * 2);
        if (*cap < *len + sl + 1) *cap = *len + sl + 1;
        *buf = kodas_realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, sl);
    *len += sl;
    (*buf)[*len] = '\0';
}
static void str_append_n(char** buf, size_t* cap, size_t* len, const char* s, size_t n) {
    if (*len + n + 1 > *cap) {
        *cap = (*cap == 0 ? 64 : *cap * 2);
        if (*cap < *len + n + 1) *cap = *len + n + 1;
        *buf = kodas_realloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}
static void str_append_char(char** buf, size_t* cap, size_t* len, char c) {
    char tmp[2] = {c, 0};
    str_append(buf, cap, len, tmp);
}

char* value_to_string(Value v) {
    char* buf = NULL; size_t cap = 0, len = 0;
    str_append(&buf, &cap, &len, "");
    char tmp[128];

    switch (v.kind) {
        case VAL_NULL: str_append(&buf, &cap, &len, "null"); break;
        case VAL_INT:
            snprintf(tmp, sizeof(tmp), "%lld", (long long)v.as.int_val);
            str_append(&buf, &cap, &len, tmp); break;
        case VAL_FLOAT:
            snprintf(tmp, sizeof(tmp), "%g", v.as.float_val);
            str_append(&buf, &cap, &len, tmp); break;
        case VAL_BOOL: str_append(&buf, &cap, &len, v.as.bool_val ? "true" : "false"); break;
        case VAL_STRING:
            if (v.as.str) str_append_n(&buf, &cap, &len, v.as.str->data, v.as.str->length);
            break;
        case VAL_CHAR: str_append_char(&buf, &cap, &len, v.as.char_val); break;
        case VAL_LIST: {
            str_append(&buf, &cap, &len, "[");
            ObjList* l = v.as.list;
            for (int i = 0; i < l->count; i++) {
                if (i > 0) str_append(&buf, &cap, &len, ", ");
                char* item = value_to_string(l->items[i]);
                str_append(&buf, &cap, &len, item);
                free(item);
            }
            str_append(&buf, &cap, &len, "]");
            break;
        }
        case VAL_TUPLE: {
            str_append(&buf, &cap, &len, "(");
            ObjList* l = v.as.list;
            for (int i = 0; i < l->count; i++) {
                if (i > 0) str_append(&buf, &cap, &len, ", ");
                char* item = value_to_string(l->items[i]);
                str_append(&buf, &cap, &len, item);
                free(item);
            }
            str_append(&buf, &cap, &len, ")");
            break;
        }
        case VAL_MAP: {
            str_append(&buf, &cap, &len, "{");
            ObjMap* m = v.as.map;
            for (int i = 0; i < m->count; i++) {
                if (i > 0) str_append(&buf, &cap, &len, ", ");
                str_append(&buf, &cap, &len, m->keys[i]);
                str_append(&buf, &cap, &len, ": ");
                char* item = value_to_string(m->values[i]);
                str_append(&buf, &cap, &len, item);
                free(item);
            }
            str_append(&buf, &cap, &len, "}");
            break;
        }
        case VAL_CLOSURE:
            snprintf(tmp, sizeof(tmp), "<fn %s>", v.as.closure && v.as.closure->name ? v.as.closure->name : "anon");
            str_append(&buf, &cap, &len, tmp); break;
        case VAL_NATIVE:
            snprintf(tmp, sizeof(tmp), "<native %s>", v.as.native.name ? v.as.native.name : "?");
            str_append(&buf, &cap, &len, tmp); break;
        case VAL_STRUCT_INST: {
            ObjStruct* s = v.as.struct_inst;
            str_append(&buf, &cap, &len, s->type_name);
            str_append(&buf, &cap, &len, " {");
            ObjMap* f = s->fields;
            for (int i = 0; i < f->count; i++) {
                if (i > 0) str_append(&buf, &cap, &len, ", ");
                str_append(&buf, &cap, &len, f->keys[i]);
                str_append(&buf, &cap, &len, ": ");
                char* item = value_to_string(f->values[i]);
                str_append(&buf, &cap, &len, item);
                free(item);
            }
            str_append(&buf, &cap, &len, "}");
            break;
        }
        case VAL_ENUM_VARIANT: {
            str_append(&buf, &cap, &len, v.as.variant->variant_name);
            if (v.as.variant->payload) {
                str_append(&buf, &cap, &len, "(");
                char* item = value_to_string(*v.as.variant->payload);
                str_append(&buf, &cap, &len, item);
                free(item);
                str_append(&buf, &cap, &len, ")");
            }
            break;
        }
        case VAL_MAYBE:
            if (!v.as.wrapped) str_append(&buf, &cap, &len, "empty");
            else { char* it = value_to_string(*v.as.wrapped); str_append(&buf, &cap, &len, it); free(it); }
            break;
        case VAL_RESULT:
            if (v.is_error) str_append(&buf, &cap, &len, "err(");
            else str_append(&buf, &cap, &len, "ok(");
            if (v.as.wrapped) { char* it = value_to_string(*v.as.wrapped); str_append(&buf, &cap, &len, it); free(it); }
            str_append(&buf, &cap, &len, ")");
            break;
        case VAL_MUT_REF:
            if (v.as.wrapped) { char* it = value_to_string(*v.as.wrapped); str_append(&buf, &cap, &len, it); free(it); }
            break;
        case VAL_CHANNEL: str_append(&buf, &cap, &len, "<channel>"); break;
        case VAL_SET: str_append(&buf, &cap, &len, "<set>"); break;
    }
    return buf;
}

void value_print(Value v, bool newline) {
    char* s = value_to_string(v);
    fputs(s, stdout);
    free(s);
    if (newline) putchar('\n');
}

bool value_truthy(Value v) {
    switch (v.kind) {
        case VAL_NULL: return false;
        case VAL_BOOL: return v.as.bool_val;
        case VAL_INT: return v.as.int_val != 0;
        case VAL_FLOAT: return v.as.float_val != 0.0;
        case VAL_STRING: return v.as.str && v.as.str->length > 0;
        case VAL_MAYBE: return v.as.wrapped != NULL;
        case VAL_RESULT: return !v.is_error;
        case VAL_LIST: case VAL_TUPLE: return v.as.list && v.as.list->count > 0;
        case VAL_MAP: return v.as.map && v.as.map->count > 0;
        default: return true;
    }
}

bool value_equals(Value a, Value b) {
    if (a.kind != b.kind) {
        if (a.kind == VAL_INT && b.kind == VAL_FLOAT) return (double)a.as.int_val == b.as.float_val;
        if (a.kind == VAL_FLOAT && b.kind == VAL_INT) return a.as.float_val == (double)b.as.int_val;
        // String compared to non-string via to_string for convenience
        return false;
    }
    switch (a.kind) {
        case VAL_NULL: return true;
        case VAL_INT: return a.as.int_val == b.as.int_val;
        case VAL_FLOAT: return fabs(a.as.float_val - b.as.float_val) < 1e-12;
        case VAL_BOOL: return a.as.bool_val == b.as.bool_val;
        case VAL_CHAR: return a.as.char_val == b.as.char_val;
        case VAL_STRING:
            if (!a.as.str || !b.as.str) return a.as.str == b.as.str;
            if (a.as.str->length != b.as.str->length) return false;
            return memcmp(a.as.str->data, b.as.str->data, a.as.str->length) == 0;
        case VAL_LIST: case VAL_TUPLE: {
            if (a.as.list == b.as.list) return true; // same ref
            if (a.as.list->count != b.as.list->count) return false;
            for (int i = 0; i < a.as.list->count; i++)
                if (!value_equals(a.as.list->items[i], b.as.list->items[i])) return false;
            return true;
        }
        case VAL_MAP: {
            if (a.as.map == b.as.map) return true;
            if (a.as.map->count != b.as.map->count) return false;
            for (int i = 0; i < a.as.map->count; i++) {
                Value* v = map_get(b, a.as.map->keys[i]);
                if (!v) return false;
                if (!value_equals(a.as.map->values[i], *v)) return false;
            }
            return true;
        }
        case VAL_STRUCT_INST: {
            if (a.as.struct_inst == b.as.struct_inst) return true;
            if (strcmp(a.as.struct_inst->type_name, b.as.struct_inst->type_name) != 0) return false;
            Value ma, mb; memset(&ma, 0, sizeof(ma)); memset(&mb, 0, sizeof(mb));
            ma.kind = VAL_MAP; ma.as.map = a.as.struct_inst->fields;
            mb.kind = VAL_MAP; mb.as.map = b.as.struct_inst->fields;
            return value_equals(ma, mb);
        }
        case VAL_CLOSURE: case VAL_NATIVE: return a.as.closure == b.as.closure;
        case VAL_ENUM_VARIANT: {
            if (a.as.variant == b.as.variant) return true;
            if (strcmp(a.as.variant->variant_name, b.as.variant->variant_name) != 0) return false;
            if (!a.as.variant->payload && !b.as.variant->payload) return true;
            if (!a.as.variant->payload || !b.as.variant->payload) return false;
            return value_equals(*a.as.variant->payload, *b.as.variant->payload);
        }
        case VAL_MAYBE: case VAL_RESULT: case VAL_MUT_REF: {
            if (!a.as.wrapped && !b.as.wrapped) return true;
            if (!a.as.wrapped || !b.as.wrapped) return false;
            if (a.kind == VAL_RESULT && a.is_error != b.is_error) return false;
            return value_equals(*a.as.wrapped, *b.as.wrapped);
        }
        default: return false;
    }
}

int value_compare(Value a, Value b) {
    if (a.kind == VAL_INT && b.kind == VAL_INT) {
        if (a.as.int_val < b.as.int_val) return -1;
        if (a.as.int_val > b.as.int_val) return 1;
        return 0;
    }
    if (a.kind == VAL_STRING && b.kind == VAL_STRING && a.as.str && b.as.str) {
        int c = memcmp(a.as.str->data, b.as.str->data,
            a.as.str->length < b.as.str->length ? a.as.str->length : b.as.str->length);
        if (c != 0) return c;
        if (a.as.str->length < b.as.str->length) return -1;
        if (a.as.str->length > b.as.str->length) return 1;
        return 0;
    }
    double da, db;
    da = (a.kind == VAL_FLOAT) ? a.as.float_val : (double)a.as.int_val;
    db = (b.kind == VAL_FLOAT) ? b.as.float_val : (double)b.as.int_val;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static double num_val(Value v) {
    return v.kind == VAL_FLOAT ? v.as.float_val : (double)v.as.int_val;
}
static bool is_num(Value v) { return v.kind == VAL_INT || v.kind == VAL_FLOAT; }

Value value_add(Value a, Value b) {
    if (a.kind == VAL_STRING || b.kind == VAL_STRING) {
        char* sa = value_to_string(a);
        char* sb = value_to_string(b);
        size_t la = strlen(sa), lb = strlen(sb);
        char* r = (char*)kodas_malloc(la + lb + 1);
        memcpy(r, sa, la); memcpy(r + la, sb, lb); r[la+lb] = '\0';
        free(sa); free(sb);
        return val_string_take(r);
    }
    if (!is_num(a) || !is_num(b)) return val_err(val_string("TypeError: + expects numbers or strings"));
    bool isf = a.kind == VAL_FLOAT || b.kind == VAL_FLOAT;
    if (isf) return val_float(num_val(a) + num_val(b));
    return val_int(a.as.int_val + b.as.int_val);
}
Value value_sub(Value a, Value b) {
    if (!is_num(a) || !is_num(b)) return val_err(val_string("TypeError: - expects numbers"));
    bool isf = a.kind == VAL_FLOAT || b.kind == VAL_FLOAT;
    if (isf) return val_float(num_val(a) - num_val(b));
    return val_int(a.as.int_val - b.as.int_val);
}
Value value_mul(Value a, Value b) {
    if (a.kind == VAL_STRING && b.kind == VAL_INT) {
        int n = (int)b.as.int_val;
        if (n <= 0) return val_string("");
        size_t len = a.as.str->length;
        char* r = (char*)kodas_malloc(len * n + 1);
        for (int i = 0; i < n; i++) memcpy(r + i*len, a.as.str->data, len);
        r[len*n] = '\0';
        return val_string_take(r);
    }
    if (!is_num(a) || !is_num(b)) return val_err(val_string("TypeError: * expects numbers"));
    bool isf = a.kind == VAL_FLOAT || b.kind == VAL_FLOAT;
    if (isf) return val_float(num_val(a) * num_val(b));
    return val_int(a.as.int_val * b.as.int_val);
}
Value value_div(Value a, Value b) {
    if (!is_num(a) || !is_num(b)) return val_err(val_string("TypeError: / expects numbers"));
    double db = num_val(b);
    if (db == 0.0) return val_err(val_string("division by zero"));
    bool isf = a.kind == VAL_FLOAT || b.kind == VAL_FLOAT;
    double da = num_val(a);
    if (!isf && a.as.int_val % b.as.int_val == 0) return val_int(a.as.int_val / b.as.int_val);
    return val_float(da / db);
}
Value value_mod(Value a, Value b) {
    if (a.kind != VAL_INT || b.kind != VAL_INT) return val_err(val_string("modulo requires integers"));
    if (b.as.int_val == 0) return val_err(val_string("division by zero"));
    return val_int(a.as.int_val % b.as.int_val);
}
Value value_neg(Value v) {
    if (v.kind == VAL_INT) return val_int(-v.as.int_val);
    if (v.kind == VAL_FLOAT) return val_float(-v.as.float_val);
    return val_err(val_string("cannot negate non-number"));
}
Value value_not(Value v) { return val_bool(!value_truthy(v)); }

// ── Copy (shallow - increments refcount for heap types) ─────────────────
Value value_copy(Value v) {
    Value r = v;
    switch (v.kind) {
        case VAL_STRING: obj_retain(v.as.str); break;
        case VAL_LIST: case VAL_TUPLE: obj_retain(v.as.list); break;
        case VAL_MAP: obj_retain(v.as.map); break;
        case VAL_STRUCT_INST: obj_retain(v.as.struct_inst); break;
        case VAL_CLOSURE: closure_retain(v.as.closure); break;
        case VAL_NATIVE: r.as.native.name = kodas_strdup(v.as.native.name); break;
        case VAL_ENUM_VARIANT:
            v.as.variant->refcount++;
            break;
        case VAL_MAYBE: case VAL_RESULT: case VAL_MUT_REF:
            if (v.as.wrapped) {
                r.as.wrapped = (Value*)kodas_malloc(sizeof(Value));
                *r.as.wrapped = value_copy(*v.as.wrapped);
            }
            break;
        default: break; // int/float/bool/char/null/channel are value types
    }
    return r;
}

// ── Clone (deep copy) ──────────────────────────────────────────────────
Value value_clone(Value v) {
    switch (v.kind) {
        case VAL_STRING: return val_string(v.as.str ? v.as.str->data : "");
        case VAL_LIST: case VAL_TUPLE: {
            Value r = v.kind == VAL_TUPLE ? val_list() : val_list();
            // make it a tuple if needed
            r.kind = v.kind;
            ObjList* l = v.as.list;
            for (int i = 0; i < l->count; i++) list_push(&r, value_clone(l->items[i]));
            return r;
        }
        case VAL_MAP: {
            Value r = val_map();
            ObjMap* m = v.as.map;
            for (int i = 0; i < m->count; i++) map_set(&r, m->keys[i], value_clone(m->values[i]));
            return r;
        }
        case VAL_STRUCT_INST: {
            Value r = val_struct(v.as.struct_inst->type_name);
            ObjMap* f = v.as.struct_inst->fields;
            for (int i = 0; i < f->count; i++) struct_set(&r, f->keys[i], value_clone(f->values[i]));
            return r;
        }
        case VAL_MAYBE: case VAL_RESULT: case VAL_MUT_REF: {
            Value r = v;
            if (v.as.wrapped) {
                r.as.wrapped = (Value*)kodas_malloc(sizeof(Value));
                *r.as.wrapped = value_clone(*v.as.wrapped);
            }
            return r;
        }
        case VAL_ENUM_VARIANT: {
            Value inner;
            if (v.as.variant->payload) inner = value_clone(*v.as.variant->payload);
            return val_enum_variant(v.as.variant->enum_name, v.as.variant->variant_name,
                v.as.variant->payload ? &inner : NULL);
        }
        default: return value_copy(v);
    }
}

// ── Free (decrements refcount for heap types) ──────────────────────────
void value_free(Value* v) {
    if (!v) return;
    switch (v->kind) {
        case VAL_STRING: obj_release(v->as.str); v->as.str = NULL; break;
        case VAL_LIST: case VAL_TUPLE: obj_release(v->as.list); v->as.list = NULL; break;
        case VAL_MAP: obj_release(v->as.map); v->as.map = NULL; break;
        case VAL_STRUCT_INST: obj_release(v->as.struct_inst); v->as.struct_inst = NULL; break;
        case VAL_CLOSURE: closure_release(v->as.closure); v->as.closure = NULL; break;
        case VAL_NATIVE: free(v->as.native.name); v->as.native.name = NULL; break;
        case VAL_ENUM_VARIANT: {
            if (v->as.variant && --v->as.variant->refcount <= 0)
                obj_enum_variant_free(v->as.variant);
            v->as.variant = NULL;
            break;
        }
        case VAL_MAYBE: case VAL_RESULT: case VAL_MUT_REF:
            if (v->as.wrapped) { value_free(v->as.wrapped); free(v->as.wrapped); v->as.wrapped = NULL; }
            break;
        default: break;
    }
    v->kind = VAL_NULL;
}
