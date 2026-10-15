/// value.h - Kodas runtime values (refcounted heap objects)
#ifndef KODAS_VALUE_H
#define KODAS_VALUE_H

#include "common.h"

struct Expr;
struct Stmt;
struct Environment;

/// Ref-count header for heap-allocated objects
typedef struct ObjHeader {
    int refcount;
    void (*free_fn)(void*);
} ObjHeader;

/// Increment refcount
static inline void obj_retain(void* ptr) {
    if (ptr) ((ObjHeader*)ptr)->refcount++;
}

/// Decrement refcount and free if zero
static inline void obj_release(void* ptr) {
    if (!ptr) return;
    ObjHeader* h = (ObjHeader*)ptr;
    if (--h->refcount <= 0) {
        if (h->free_fn) h->free_fn(ptr);
        free(ptr);
    }
}

// Heap object types (with refcount header first)
typedef struct ObjList {
    ObjHeader hdr;
    struct Value* items;
    int count;
    int capacity;
} ObjList;

typedef struct ObjMap {
    ObjHeader hdr;
    char** keys;
    struct Value* values;
    int count;
    int capacity;
} ObjMap;

typedef struct ObjString {
    ObjHeader hdr;
    char* data;
    size_t length;
} ObjString;

typedef struct ObjStruct {
    ObjHeader hdr;
    char* type_name;
    ObjMap* fields;
} ObjStruct;

typedef struct VParam {
    char* name;
    char* type_ann;
    struct Expr* default_value;
    bool is_mut;
    bool is_self;
} VParam;

typedef struct {
    VParam* items;
    int count;
    int capacity;
} VParamList;

typedef enum {
    VAL_NULL,
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_CHAR,
    VAL_LIST,
    VAL_MAP,
    VAL_TUPLE,
    VAL_SET,
    VAL_CLOSURE,
    VAL_NATIVE,
    VAL_STRUCT_INST,
    VAL_ENUM_VARIANT,
    VAL_MAYBE,
    VAL_RESULT,
    VAL_MUT_REF,
    VAL_CHANNEL,
} ValueKind;

typedef struct Value Value;
typedef Value (*NativeFn)(int arg_count, Value* args, void* ctx);

typedef struct Closure {
    struct Stmt* decl;
    struct Environment* closure;
    char* name;
    VParamList params;
    struct Expr* body;
    bool is_arrow;
    int refcount;
} Closure;

typedef struct {
    char* enum_name;
    char* variant_name;
    Value* payload;
    int refcount;
} ValueEnumVariant;

struct Value {
    ValueKind kind;
    bool is_error;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        char char_val;
        ObjString* str;
        ObjList* list;
        ObjMap* map;
        ObjStruct* struct_inst;
        Closure* closure;
        struct { NativeFn fn; char* name; void* ctx; } native;
        ValueEnumVariant* variant;
        Value* wrapped;
        struct {
            int send_fd;
            int recv_fd;
            bool closed;
        } channel;
    } as;
};

Value val_null(void);
Value val_int(int64_t v);
Value val_float(double v);
Value val_bool(bool v);
Value val_string(const char* v);
Value val_string_take(char* v);
Value val_string_len(const char* s, size_t len);
Value val_char(char c);
Value val_list(void);
Value val_list_from(Value* items, int count);
Value val_map(void);
Value val_closure(Closure* c);
Value val_native(NativeFn fn, const char* name, void* ctx);
Value val_struct(const char* name);
Value val_enum_variant(const char* enum_name, const char* var_name, Value* payload);
Value val_ok(Value v);
Value val_err(Value v);
Value val_empty(void);
Value val_maybe_just(Value v);
Value val_tuple(Value* items, int count);
Value val_channel(int send_fd, int recv_fd);

const char* val_str_cstr(Value v);
int64_t val_str_len(Value v);

void list_push(Value* list, Value item);
Value list_pop(Value* list);
Value list_get(Value list, int idx);
void list_set(Value list, int idx, Value val);
int list_len(Value list);
Value list_slice(Value list, int start, int end);

void map_set(Value* map, const char* key, Value val);
Value* map_get(Value map, const char* key);
bool map_has(Value map, const char* key);
int map_len(Value map);
void map_delete(Value* map, const char* key);

void struct_set(Value* inst, const char* field, Value val);
Value* struct_get(Value inst, const char* field);

void closure_retain(Closure* c);
void closure_release(Closure* c);

void value_print(Value v, bool newline);
char* value_to_string(Value v);
bool value_truthy(Value v);
bool value_equals(Value a, Value b);
int value_compare(Value a, Value b);
Value value_add(Value a, Value b);
Value value_sub(Value a, Value b);
Value value_mul(Value a, Value b);
Value value_div(Value a, Value b);
Value value_mod(Value a, Value b);
Value value_neg(Value v);
Value value_not(Value v);

/// Copy: increments refcount for heap types (shallow copy)
Value value_copy(Value v);
/// Deep clone (for explicit deep_copy() builtin)
Value value_clone(Value v);
void value_free(Value* v);

void gc_init(void);
void gc_shutdown(void);

#endif // KODAS_VALUE_H
