/// krt.h - Kodas Runtime for compiled-to-C programs
///
/// Tagged-union value type + runtime library used by all KRT-compiled .c files.
#ifndef KODAS_KRT_H
#define KODAS_KRT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <math.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Value kinds ────────────────────────────────────────────────────────
typedef enum {
    K_NULL, K_BOOL, K_INT, K_FLOAT, K_STRING, K_CHAR,
    K_LIST, K_MAP, K_TUPLE, K_SET,
    K_CLOSURE, K_NATIVE,
    K_STRUCT, K_ENUM, K_MAYBE, K_RESULT,
    K_TYPE,        // type-as-value (for static methods like Point.new)
    K_RANGE,       // inclusive/exclusive numeric range
    K_MUT_REF
} KKind;

struct KVal;
typedef struct KVal KVal;
typedef KVal* (*KNativeFn)(int argc, KVal** argv);
typedef KVal* (*KClosureFn)(KVal** env, int argc, KVal** argv);

typedef struct KList { int count; int cap; KVal** items; } KList;
typedef struct KMapEntry { KVal* key; KVal* val; struct KMapEntry* next; } KMapEntry;
typedef struct KMap { int count; int cap; KMapEntry** buckets; } KMap;
typedef struct KStruct { const char* name; int field_count; const char** field_names; KVal** fields; } KStruct;
typedef struct KEnum { const char* variant; int payload_count; KVal** payload; } KEnum;
typedef struct KClosure { KClosureFn fn; int env_count; KVal** env; int arity; const char* name; } KClosure_;
typedef struct KNative { KNativeFn fn; const char* name; } KNative_;
typedef struct KMutRef { KVal* inner; } KMutRef;
typedef struct KRange { int64_t start, end; bool inclusive; } KRange;

struct KVal {
    KKind kind;
    bool is_error;      // for K_RESULT (true => err) or K_MAYBE empty
    uint32_t refcount;
    union {
        bool bool_v;
        int64_t int_v;
        double float_v;
        char* string_v;
        char char_v;
        KList* list;
        KMap* map;
        KList* tuple;   // same shape as list
        KClosure_* closure;
        KNative_* native;
        KStruct* struct_v;
        KEnum* enum_v;
        const char* type_v;     // K_TYPE: type name string (static,  )
        KVal* wrapped;      // K_MAYBE / K_RESULT inner value
        KRange* range;
        KMutRef* mutref;
    } as;
};

// ── Panic / error handling ─────────────────────────────────────────────
typedef struct KFrame KFrame;
struct KFrame {
    jmp_buf buf;
    KVal* error;
    KFrame* prev;
};

extern __thread KFrame* k_current_frame;

// Throw an error; longjmps to nearest k_catch.
void k_throw(KVal* err);
// Like assert(condition, msg): throw if condition is falsy.
KVal* k_assert(KVal* cond, KVal* msg);
// Convenience: build err(str) from printf format.
KVal* k_errf(const char* fmt, ...);

// ── Value constructors ─────────────────────────────────────────────────
KVal* k_null(void);
KVal* k_bool(bool b);
KVal* k_int(int64_t i);
KVal* k_float(double d);
KVal* k_str(const char* s);                // strdup'd copy
KVal* k_str_len(const char* s, int len);
KVal* k_char(char c);
KVal* k_list(int count, ...);              // k_list(n, v1, v2, ...)
KVal* k_list_empty(void);
KVal* k_tuple(int count, ...);
KVal* k_map_empty(void);
KVal* k_struct_new(const char* name, int field_count, const char** names, ...);
KVal* k_enum_new(const char* variant, int payload_count, ...);
KVal* k_type(const char* name);             // type-as-value for static dispatch
KVal* k_range(int64_t start, int64_t end, bool inclusive);
KVal* k_set_empty(void);
void  k_set_add(KVal* set, KVal* v);
bool  k_eq_deep(KVal* a, KVal* b);         // structural equality
KVal* k_ok(KVal* v);
KVal* k_err(KVal* v);
KVal* k_empty(void);                       // K_MAYBE empty
KVal* k_some(KVal* v);                     // K_MAYBE some
KVal* k_closure(KClosureFn fn, int env_count, KVal** env, int arity, const char* name);
KVal* k_native(KNativeFn fn, const char* name);
KVal* k_mutref(KVal* inner);

// ── Reference counting ─────────────────────────────────────────────────
KVal* k_retain(KVal* v);
void  k_release(KVal* v);
void  k_release_many(int count, ...);

// ── Operations ─────────────────────────────────────────────────────────
KVal* k_call(KVal* fn, int argc, KVal** argv);
KVal* k_method(KVal* obj, const char* name, int argc, KVal** argv);
KVal* k_get_field(KVal* obj, const char* name);
void  k_set_field(KVal* obj, const char* name, KVal* val);
KVal* k_index(KVal* col, KVal* key);
void  k_index_set(KVal* col, KVal* key, KVal* val);
KVal* k_add(KVal* a, KVal* b);
KVal* k_add_str(KVal* a, KVal* b);   // string concat / numeric add
KVal* k_sub(KVal* a, KVal* b);
KVal* k_mul(KVal* a, KVal* b);
KVal* k_div(KVal* a, KVal* b);
KVal* k_mod(KVal* a, KVal* b);
KVal* k_neg(KVal* a);
KVal* k_not(KVal* a);
KVal* k_eq(KVal* a, KVal* b);
KVal* k_neq(KVal* a, KVal* b);
KVal* k_lt(KVal* a, KVal* b);
KVal* k_gt(KVal* a, KVal* b);
KVal* k_lte(KVal* a, KVal* b);
KVal* k_gte(KVal* a, KVal* b);
KVal* k_and(KVal* a, KVal* b);
KVal* k_or(KVal* a, KVal* b);
bool  k_truthy(KVal* v);

// ── List/map helpers ───────────────────────────────────────────────────
void  k_list_push(KVal* list, KVal* v);
KVal* k_list_pop(KVal* list);
int   k_len(KVal* v);
KVal* k_show(KVal* v);         // return string representation
void  k_print(KVal* v);        // print to stdout with newline
void  k_eprint(KVal* v);       // print to stderr with newline
KVal* k_concat(KVal* a, KVal* b);
KVal* k_to_str(KVal* v);
KVal* k_to_int(KVal* v);
KVal* k_to_float(KVal* v);

// ── Error-pipe helpers (|?>) ───────────────────────────────────────────
// If `v` is an error/empty, propagate it out; otherwise apply `fn(v)`.
KVal* k_pipe(KVal* v, KVal* fn);
// Unwrap (unwrap!): panics if v is error/empty, else returns inner.
KVal* k_unwrap(KVal* v);
KVal* k_unwrap_or(KVal* v, KVal* alt);

// ── Iterator for for-in loops ──────────────────────────────────────────
typedef struct KIter { KVal* col; int idx; } KIter;
KIter k_iter_new(KVal* col);
KVal* k_iter_next(KIter* it);   // returns NULL when done

// ── Stdlib native registrations (called before main body) ──────────────
void k_register_stdlib(void);
// Look up a global by name (used when codegen can't statically resolve).
KVal* k_global_get(const char* name);
void  k_global_set(const char* name, KVal* v);

// ── Entry point (implemented by generated code) ────────────────────────
KVal* k_entry(int argc, KVal** argv);

#ifdef __cplusplus
}
#endif

#endif // KODAS_KRT_H
