/// environment.h - Kodas variable environments
#ifndef KODAS_ENVIRONMENT_H
#define KODAS_ENVIRONMENT_H

#include "common.h"
#include "value.h"

typedef struct Environment {
    char** names;
    Value* values;
    bool* mut_flags;
    int count;
    int capacity;
    struct Environment* enclosing;
    bool owns_values;
} Environment;

Environment* env_new(Environment* enclosing);
void env_free(Environment* env);
void env_define(Environment* env, const char* name, Value value, bool is_mut);
void env_define_global(Environment* env, const char* name, NativeFn fn, const char* fn_name, void* ctx);
Value* env_lookup(Environment* env, const char* name);
bool env_assign(Environment* env, const char* name, Value value);
Environment* env_copy(Environment* env);
void env_detach(Environment* env);

/// Register builtins
void env_register_builtins(Environment* env);

#endif // KODAS_ENVIRONMENT_H
