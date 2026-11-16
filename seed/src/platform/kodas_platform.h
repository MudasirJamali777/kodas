// kodas_platform.h - cross-platform shims (POSIX / Win32)
#pragma once

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOGDI
  #define NOUSER
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <io.h>
  #include <direct.h>
  #include <process.h>
  #include <malloc.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <stddef.h>
  // Replace POSIX names (mingw already provides alloca via malloc.h)
  #define strcasecmp _stricmp
  #define strncasecmp _strnicmp
  #define fdopen _fdopen
  #define fileno _fileno
  #define isatty _isatty
  #define dup _dup
  #define dup2 _dup2
  #define popen _popen
  #define pclose _pclose
  #define access _access
  #define chdir _chdir
  #define mkdir(p, m) _mkdir(p)
  #define rmdir _rmdir
  #define getcwd _getcwd
  #define unlink _unlink
  #define PATH_SEP '\\'
  #define PATH_SEP_STR "\\"
  #define KODAS_PLATFORM_WINDOWS 1
  #define KODAS_HAS_HTTP 0
  #define KODAS_HAS_THREADS 0
  // POSIX mode_t flags not defined on MSVC
  #ifndef S_IRUSR
  #define S_IRUSR _S_IREAD
  #define S_IWUSR _S_IWRITE
  #define S_IXUSR 0
  #define S_IRGRP 0
  #define S_IWGRP 0
  #define S_IROTH 0
  #endif
  // realpath
  static inline char* win_realpath(const char* p, char* buf) {
    return _fullpath(buf, p, 1024);
  }
  #define realpath win_realpath
  // getenv: already present
  #define KODAS_THREAD_RET unsigned __stdcall
  typedef HANDLE kthread_t;
  static inline int kthread_create(kthread_t* t, unsigned (__stdcall *fn)(void*), void* arg) {
    *t = (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
    return *t ? 0 : -1;
  }
  static inline void kthread_join(kthread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
  typedef CRITICAL_SECTION kmutex_t;
  static inline void kmutex_init(kmutex_t* m) { InitializeCriticalSection(m); }
  static inline void kmutex_lock(kmutex_t* m) { EnterCriticalSection(m); }
  static inline void kmutex_unlock(kmutex_t* m) { LeaveCriticalSection(m); }
  static inline void kmutex_destroy(kmutex_t* m) { DeleteCriticalSection(m); }
  #define KODAS_POPEN_READ "rb"
  #define KODAS_EXE_EXT ".exe"
  #define KODAS_THREAD_LOCAL __declspec(thread)
#else
  #include <unistd.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <fcntl.h>
  #include <alloca.h>
  #include <pthread.h>
  #include <dirent.h>
  #include <libgen.h>
  #define PATH_SEP '/'
  #define PATH_SEP_STR "/"
  #define KODAS_PLATFORM_POSIX 1
  #define KODAS_HAS_HTTP 1
  #define KODAS_HAS_THREADS 1
  #define KODAS_EXE_EXT ""
  typedef pthread_t kthread_t;
  #define KODAS_THREAD_RET void*
  static inline int kthread_create(kthread_t* t, void*(*fn)(void*), void* arg) {
    return pthread_create(t, NULL, fn, arg);
  }
  static inline void kthread_join(kthread_t t) { pthread_join(t, NULL); }
  typedef pthread_mutex_t kmutex_t;
  static inline void kmutex_init(kmutex_t* m) { pthread_mutex_init(m, NULL); }
  static inline void kmutex_lock(kmutex_t* m) { pthread_mutex_lock(m); }
  static inline void kmutex_unlock(kmutex_t* m) { pthread_mutex_unlock(m); }
  static inline void kmutex_destroy(kmutex_t* m) { pthread_mutex_destroy(m); }
  #define KODAS_POPEN_READ "r"
  #define KODAS_THREAD_LOCAL __thread
#endif
