//
// common.h
// by Dylan Kress
//

#pragma once

// Prevent windows.h from including winsock.h (which conflicts with winsock2.h in msquic)
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#endif

// MSVC uses static_assert in C mode; GCC/Clang use _Static_assert
#ifdef _MSC_VER
#define _Static_assert static_assert
#endif

// MsQuic must come before any Windows includes
#include <msquic.h>

// Standard C includes
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <strings.h>
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#define _fseeki64(f, o, w) fseeko((f), (off_t)(o), (w))
#define _ftelli64(f)       ftello((f))
#define __int64            int64_t
#define _strdup(s)         strdup(s)
#endif

// BOOLEAN type - Windows already defines this, so just use it
#ifndef BOOLEAN
typedef unsigned char BOOLEAN;
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) (void)(P)
#endif

//=============================================================================
// Cross-Platform Abstraction Macros
//=============================================================================

#ifdef _WIN32

// --- Mutex ---
#define WH_MUTEX                    CRITICAL_SECTION
#define WH_MUTEX_INIT(m)            InitializeCriticalSection(&(m))
#define WH_MUTEX_DESTROY(m)         DeleteCriticalSection(&(m))
#define WH_MUTEX_LOCK(m)            EnterCriticalSection(&(m))
#define WH_MUTEX_UNLOCK(m)          LeaveCriticalSection(&(m))

// --- Atomics ---
#define WH_ATOMIC_SET(ptr, val)           InterlockedExchange((volatile LONG *)(ptr), (LONG)(val))
#define WH_ATOMIC_ADD(ptr, val)           InterlockedExchangeAdd((volatile LONG *)(ptr), (LONG)(val))
#define WH_ATOMIC_ADD64(ptr, val)         InterlockedExchangeAdd64((volatile LONGLONG *)(ptr), (LONGLONG)(val))
#define WH_ATOMIC_INC(ptr)               InterlockedIncrement((volatile LONG *)(ptr))
#define WH_ATOMIC_DEC(ptr)               InterlockedDecrement((volatile LONG *)(ptr))
#define WH_ATOMIC_LOAD(ptr)              InterlockedCompareExchange((volatile LONG *)(ptr), 0, 0)
#define WH_ATOMIC_LOAD64(ptr)            InterlockedCompareExchange64((volatile LONGLONG *)(ptr), 0, 0)
#define WH_ATOMIC_CAS(ptr, expected, desired) \
    InterlockedCompareExchange((volatile LONG *)(ptr), (LONG)(desired), (LONG)(expected))

// --- Events (manual-reset) ---
#define WH_EVENT                    HANDLE
#define WH_EVENT_CREATE()           CreateEvent(NULL, TRUE, FALSE, NULL)
#define WH_EVENT_SET(e)             SetEvent(e)
#define WH_EVENT_RESET(e)           ResetEvent(e)
#define WH_EVENT_WAIT(e, ms)        WaitForSingleObject((e), (ms))
#define WH_EVENT_DESTROY(e)         do { if (e) CloseHandle(e); } while(0)
#define WH_EVENT_WAIT_OK            WAIT_OBJECT_0
#define WH_EVENT_WAIT_TIMEOUT       WAIT_TIMEOUT

// --- Threads ---
#define WH_THREAD                   HANDLE
#define WH_THREAD_RETURN            DWORD WINAPI
#define WH_THREAD_PARAM             LPVOID
#define WH_THREAD_CREATE(t, func, arg)  (((t) = CreateThread(NULL, 0, (func), (arg), 0, NULL)) != NULL)
#define WH_THREAD_JOIN(t, ms)       do { if (t) { WaitForSingleObject((t), (ms)); CloseHandle(t); (t) = NULL; } } while(0)

// --- Semaphore ---
#define WH_SEMAPHORE                HANDLE
#define WH_SEM_CREATE(s, max)       ((s) = CreateSemaphore(NULL, 0, (max), NULL))
#define WH_SEM_WAIT(s, ms)          WaitForSingleObject((s), (ms))
#define WH_SEM_POST(s)              ReleaseSemaphore((s), 1, NULL)
#define WH_SEM_DESTROY(s)           do { if (s) CloseHandle(s); } while(0)

// --- Sleep ---
#define WH_SLEEP_MS(ms)             Sleep(ms)

// --- Timer (high-resolution, returns seconds as double) ---
static inline double WH_TIMER_NOW(void)
{
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

// --- Path separator ---
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"

// --- Thread-local storage ---
#define WH_THREAD_LOCAL     __declspec(thread)

// --- Init-once ---
#define WH_ONCE             INIT_ONCE
#define WH_ONCE_INIT        INIT_ONCE_STATIC_INIT

#else // Linux / POSIX

// --- Mutex ---
#define WH_MUTEX                    pthread_mutex_t
#define WH_MUTEX_INIT(m)            pthread_mutex_init(&(m), NULL)
#define WH_MUTEX_DESTROY(m)         pthread_mutex_destroy(&(m))
#define WH_MUTEX_LOCK(m)            pthread_mutex_lock(&(m))
#define WH_MUTEX_UNLOCK(m)          pthread_mutex_unlock(&(m))

// --- Atomics (GCC/Clang builtins) ---
#define WH_ATOMIC_SET(ptr, val)           __atomic_store_n((volatile int32_t *)(ptr), (int32_t)(val), __ATOMIC_SEQ_CST)
#define WH_ATOMIC_ADD(ptr, val)           __atomic_fetch_add((volatile int32_t *)(ptr), (int32_t)(val), __ATOMIC_SEQ_CST)
#define WH_ATOMIC_ADD64(ptr, val)         __atomic_fetch_add((volatile int64_t *)(ptr), (int64_t)(val), __ATOMIC_SEQ_CST)
#define WH_ATOMIC_INC(ptr)               __atomic_fetch_add((volatile int32_t *)(ptr), 1, __ATOMIC_SEQ_CST)
#define WH_ATOMIC_DEC(ptr)               __atomic_fetch_sub((volatile int32_t *)(ptr), 1, __ATOMIC_SEQ_CST)
#define WH_ATOMIC_LOAD(ptr)              __atomic_load_n((volatile int32_t *)(ptr), __ATOMIC_SEQ_CST)
#define WH_ATOMIC_LOAD64(ptr)            __atomic_load_n((volatile int64_t *)(ptr), __ATOMIC_SEQ_CST)
#define WH_ATOMIC_CAS(ptr, expected, desired) \
    wh_atomic_cas_helper((volatile int32_t *)(ptr), (int32_t)(expected), (int32_t)(desired))

static inline int32_t wh_atomic_cas_helper(volatile int32_t *ptr, int32_t expected, int32_t desired)
{
    int32_t old = expected;
    __atomic_compare_exchange_n(ptr, &old, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return old;  // Returns previous value (same semantics as InterlockedCompareExchange)
}

// --- Events (manual-reset, implemented with mutex+condvar+flag) ---
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             signaled;
} WH_EVENT_IMPL;

typedef WH_EVENT_IMPL* WH_EVENT;

static inline WH_EVENT wh_event_create(void)
{
    WH_EVENT_IMPL *ev = (WH_EVENT_IMPL *)calloc(1, sizeof(WH_EVENT_IMPL));
    if (!ev) return NULL;
    pthread_mutex_init(&ev->mutex, NULL);
    pthread_cond_init(&ev->cond, NULL);
    ev->signaled = 0;
    return ev;
}

static inline void wh_event_set(WH_EVENT ev)
{
    if (!ev) return;
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = 1;
    pthread_cond_broadcast(&ev->cond);
    pthread_mutex_unlock(&ev->mutex);
}

static inline int wh_event_wait(WH_EVENT ev, uint32_t timeout_ms)
{
    if (!ev) return -1;
    pthread_mutex_lock(&ev->mutex);
    if (ev->signaled) {
        pthread_mutex_unlock(&ev->mutex);
        return 0;  // WH_EVENT_WAIT_OK
    }
    if (timeout_ms == 0xFFFFFFFF) {
        // Infinite wait
        while (!ev->signaled)
            pthread_cond_wait(&ev->cond, &ev->mutex);
        pthread_mutex_unlock(&ev->mutex);
        return 0;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    int rc = 0;
    while (!ev->signaled && rc == 0)
        rc = pthread_cond_timedwait(&ev->cond, &ev->mutex, &ts);
    int result = ev->signaled ? 0 : 1;  // 0=OK, 1=TIMEOUT
    pthread_mutex_unlock(&ev->mutex);
    return result;
}

static inline void wh_event_destroy(WH_EVENT ev)
{
    if (!ev) return;
    pthread_mutex_destroy(&ev->mutex);
    pthread_cond_destroy(&ev->cond);
    free(ev);
}

static inline void wh_event_reset(WH_EVENT ev)
{
    if (!ev) return;
    pthread_mutex_lock(&ev->mutex);
    ev->signaled = 0;
    pthread_mutex_unlock(&ev->mutex);
}

#define WH_EVENT_CREATE()           wh_event_create()
#define WH_EVENT_SET(e)             wh_event_set(e)
#define WH_EVENT_RESET(e)           wh_event_reset(e)
#define WH_EVENT_WAIT(e, ms)        wh_event_wait((e), (ms))
#define WH_EVENT_DESTROY(e)         wh_event_destroy(e)
#define WH_EVENT_WAIT_OK            0
#define WH_EVENT_WAIT_TIMEOUT       1

// --- Threads ---
typedef pthread_t WH_THREAD;
#define WH_THREAD_RETURN            void*
#define WH_THREAD_PARAM             void*
#define WH_THREAD_CREATE(t, func, arg) (pthread_create(&(t), NULL, (func), (arg)) == 0)
#define WH_THREAD_JOIN(t, ms)       do { (void)(ms); pthread_join((t), NULL); } while(0)

// --- Semaphore ---
typedef sem_t WH_SEMAPHORE;
#define WH_SEM_CREATE(s, max)       sem_init(&(s), 0, 0)
#define WH_SEM_WAIT(s, ms)          wh_sem_timedwait(&(s), (ms))
#define WH_SEM_POST(s)              sem_post(&(s))
#define WH_SEM_DESTROY(s)           sem_destroy(&(s))

static inline int wh_sem_timedwait(sem_t *sem, uint32_t timeout_ms)
{
    if (timeout_ms == 0xFFFFFFFF) {
        return sem_wait(sem) == 0 ? 0 : -1;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return sem_timedwait(sem, &ts) == 0 ? 0 : -1;
}

// --- Sleep ---
#define WH_SLEEP_MS(ms)             usleep((ms) * 1000)

// --- Timer (high-resolution, returns seconds as double) ---
static inline double WH_TIMER_NOW(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// --- Path separator ---
#define PATH_SEP '/'
#define PATH_SEP_STR "/"

// --- Thread-local storage ---
#define WH_THREAD_LOCAL     __thread

// --- Init-once ---
#define WH_ONCE             pthread_once_t
#define WH_ONCE_INIT        PTHREAD_ONCE_INIT

// --- Windows type compatibility ---
typedef int32_t  LONG;
typedef int64_t  LONGLONG;
typedef uint32_t DWORD;

static inline DWORD GetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#endif // _WIN32

// --- Restrictive file permissions (owner read/write only) ---
#ifdef _WIN32
#define WH_SET_FILE_PRIVATE(path) ((void)(path))
#else
static inline void wh_set_file_private(const char *p) { chmod(p, 0600); }
#define WH_SET_FILE_PRIVATE(path) wh_set_file_private(path)
#endif

// --- Portable string wrappers ---
#ifndef _WIN32
#define strcpy_s(dst, dst_size, src) snprintf((dst), (dst_size), "%s", (src))
#define _TRUNCATE ((size_t)-1)
#define strncpy_s(dst, dst_size, src, count) snprintf((dst), (dst_size), "%s", (src))
#endif

// Timestamp logging helper
static inline void PrintTimestamp(void)
{
#ifdef _WIN32
	SYSTEMTIME st;
	GetLocalTime(&st);
	printf("[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm *tm_info = localtime(&ts.tv_sec);
	printf("[%02d:%02d:%02d.%03ld] ", 
		tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, ts.tv_nsec / 1000000);
#endif
}

// Timestamp logging helper for stderr
static inline void PrintTimestampErr(void)
{
#ifdef _WIN32
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(stderr, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm *tm_info = localtime(&ts.tv_sec);
	fprintf(stderr, "[%02d:%02d:%02d.%03ld] ",
		tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, ts.tv_nsec / 1000000);
#endif
}

// Monotonic clock in seconds — use for elapsed-time/timeout calculations.
// Unlike time(NULL), this is immune to NTP adjustments and clock_settime.
static inline time_t wh_monotonic_sec(void)
{
#ifdef _WIN32
    return (time_t)(GetTickCount64() / 1000);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
#endif
}

// Convert a 32-byte hash to a null-terminated 65-char hex string.
static inline void WH_HashToHex(const uint8_t hash[32], char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
        hex[i * 2]     = digits[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[hash[i] & 0x0F];
    }
    hex[64] = '\0';
}

// Logging macros with timestamps
#define LOG(...) do { PrintTimestamp(); printf(__VA_ARGS__); } while(0)
#define LOG_ERROR(...) do { PrintTimestampErr(); fprintf(stderr, __VA_ARGS__); } while(0)
