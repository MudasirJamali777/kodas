#ifndef KODAS_UNUSED
#define KODAS_UNUSED __attribute__((unused))
#endif

/// common.h - Kodas compiler common definitions
#ifndef KODAS_COMMON_H
#define KODAS_COMMON_H

// Ensure POSIX APIs (strnlen, strdup, etc.) on Linux/macOS
#if !defined(_MSC_VER) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

// ---------------------------------------------------------------------------
// Standard C headers FIRST (so Windows headers don't override them)
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Compiler / platform detection
// ---------------------------------------------------------------------------
#ifdef _MSC_VER
  // ---- MSVC (Visual Studio cl.exe) ----
  #ifndef __cplusplus
    // MSVC's C11 doesn't always ship <stdbool.h>; define it ourselves
    typedef unsigned char bool;
    #ifndef true
    #define true  ((bool)1)
    #endif
    #ifndef false
    #define false ((bool)0)
    #endif
  #endif

  // Bring in Windows headers (macros guarded to avoid duplicate-def warnings)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <io.h>
  #include <fcntl.h>
  #include <sys/types.h>
  #include <sys/stat.h>

  // ssize_t
  #include <BaseTsd.h>
  typedef SSIZE_T ssize_t;

  // strdup is _strdup on MSVC
  #ifndef strdup
  #define strdup _strdup
  #endif

  // strndup is missing on MSVC
  static inline char* ms_strndup(const char* s, size_t n) {
      size_t len = strnlen(s, n);
      char* p = (char*)malloc(len + 1);
      if (p) { memcpy(p, s, len); p[len] = '\0'; }
      return p;
  }
  #define strndup ms_strndup

  // clock_gettime / CLOCK_MONOTONIC shim
  // (Recent Windows SDK 10.0.28000+ ships these in <time.h> already;
  //  if not available, provide our own.)
  #ifndef CLOCK_MONOTONIC
  #define CLOCK_MONOTONIC 1
  static inline int clock_gettime_msvc(int clk_id, struct timespec* tp) {
      (void)clk_id;
      static LARGE_INTEGER freq = {0};
      if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
      LARGE_INTEGER pc; QueryPerformanceCounter(&pc);
      tp->tv_sec = (time_t)(pc.QuadPart / freq.QuadPart);
      tp->tv_nsec = (long)((pc.QuadPart % freq.QuadPart) * 1000000000LL / freq.QuadPart);
      return 0;
  }
  #define clock_gettime clock_gettime_msvc
  #endif

  // nanosleep shim
  static inline int nanosleep_msvc(const struct timespec* ts, struct timespec* rem) {
      (void)rem;
      Sleep((DWORD)(ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000));
      return 0;
  }
  #define nanosleep nanosleep_msvc

#else
  // ---- GCC / Clang / MinGW (POSIX) ----
  #include <stdbool.h>
  #include <unistd.h>
  #include <sys/stat.h>
  #ifdef _WIN32
    #include <windows.h>
  #endif
#endif

// ---------------------------------------------------------------------------
// Version & utilities
// ---------------------------------------------------------------------------
#define KODAS_VERSION_MAJOR 0
#define KODAS_VERSION_MINOR 1
#define KODAS_VERSION_PATCH 0
#define KODAS_VERSION_STRING "0.1.0"

/// Safe allocation with panic on failure
static inline void* kodas_malloc(size_t size) {
    void* p = malloc(size);
    if (!p) {
        fprintf(stderr, "fatal: out of memory (allocating %zu bytes)\n", size);
        exit(1);
    }
    return p;
}

static inline void* kodas_calloc(size_t n, size_t size) {
    void* p = calloc(n, size);
    if (!p) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return p;
}

static inline void* kodas_realloc(void* ptr, size_t size) {
    void* p = realloc(ptr, size);
    if (!p && size > 0) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(1);
    }
    return p;
}

static inline char* kodas_strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* p = (char*)kodas_malloc(len);
    memcpy(p, s, len);
    return p;
}

static inline char* kodas_strndup(const char* s, size_t n) {
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char* p = (char*)kodas_malloc(len + 1);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

/// Dynamic array
#define DA_INIT_CAP 8

#define da_append(arr, item) \
    do { \
        if ((arr)->count >= (arr)->capacity) { \
            (arr)->capacity = (arr)->capacity == 0 ? DA_INIT_CAP : (arr)->capacity * 2; \
            (arr)->items = kodas_realloc((arr)->items, (arr)->capacity * sizeof(*(arr)->items)); \
        } \
        (arr)->items[(arr)->count++] = (item); \
    } while(0)

#define da_free(arr) \
    do { \
        if ((arr)->items) free((arr)->items); \
        (arr)->items = NULL; \
        (arr)->count = (arr)->capacity = 0; \
    } while(0)

/// Source location
typedef struct {
    const char* file;
    int line;
    int col;
    int offset;
} Location;

static inline Location loc_make(const char* file, int line, int col, int offset) {
    Location l = {file, line, col, offset};
    return l;
}

#define LOC_NONE ((Location){NULL, 0, 0, 0})

#endif // KODAS_COMMON_H
