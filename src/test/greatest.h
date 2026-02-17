//
// greatest.h
// Minimal C test framework for Wormhole project.
// Inspired by the greatest.h single-header test framework.
//
// Usage:
//   GREATEST_MAIN_DEFS();
//   TEST my_test(void) { ASSERT(1 + 1 == 2); PASS(); }
//   SUITE(my_suite) { RUN_TEST(my_test); }
//   int main(void) {
//       GREATEST_MAIN_BEGIN();
//       RUN_SUITE(my_suite);
//       GREATEST_MAIN_END();
//   }
//

#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// ANSI color codes
// ---------------------------------------------------------------------------
#define GREATEST_CLR_GREEN  "\x1b[32m"
#define GREATEST_CLR_RED    "\x1b[31m"
#define GREATEST_CLR_YELLOW "\x1b[33m"
#define GREATEST_CLR_RESET  "\x1b[0m"

// ---------------------------------------------------------------------------
// Test result enum
// ---------------------------------------------------------------------------
typedef enum {
    GREATEST_TEST_RES_PASS = 0,
    GREATEST_TEST_RES_FAIL = 1,
    GREATEST_TEST_RES_SKIP = 2
} greatest_test_res;

// ---------------------------------------------------------------------------
// Enable ANSI escape sequences on Windows 10+
// ---------------------------------------------------------------------------
static void greatest_enable_colors_(void)
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            SetConsoleMode(hOut, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Global state — one set per test executable (file-scope statics)
// ---------------------------------------------------------------------------
#define GREATEST_MAIN_DEFS() \
    static unsigned greatest_passed_  = 0; \
    static unsigned greatest_failed_  = 0; \
    static unsigned greatest_skipped_ = 0; \
    static unsigned greatest_suites_  = 0

// ---------------------------------------------------------------------------
// Main begin / end
// ---------------------------------------------------------------------------
#define GREATEST_MAIN_BEGIN() \
    greatest_enable_colors_(); \
    printf("\n")

#define GREATEST_MAIN_END() \
    printf("\n=== RESULTS ===\n"); \
    printf("  Suites run: %u\n", greatest_suites_); \
    printf("  Tests:  %u total, " \
           GREATEST_CLR_GREEN "%u passed" GREATEST_CLR_RESET ", " \
           GREATEST_CLR_RED   "%u failed" GREATEST_CLR_RESET ", " \
           GREATEST_CLR_YELLOW "%u skipped" GREATEST_CLR_RESET "\n", \
           greatest_passed_ + greatest_failed_ + greatest_skipped_, \
           greatest_passed_, greatest_failed_, greatest_skipped_); \
    if (greatest_failed_ > 0) { \
        printf("  " GREATEST_CLR_RED "FAILED" GREATEST_CLR_RESET "\n\n"); \
    } else { \
        printf("  " GREATEST_CLR_GREEN "ALL PASSED" GREATEST_CLR_RESET "\n\n"); \
    } \
    return (greatest_failed_ > 0) ? 1 : 0

// ---------------------------------------------------------------------------
// Test function signature and result macros
// ---------------------------------------------------------------------------
#define TEST   static greatest_test_res

#define PASS() return GREATEST_TEST_RES_PASS
#define SKIP() return GREATEST_TEST_RES_SKIP
#define FAILm() do { \
    printf("    " GREATEST_CLR_RED "FAIL" GREATEST_CLR_RESET \
           " at %s:%d\n", __FILE__, __LINE__); \
    return GREATEST_TEST_RES_FAIL; \
} while (0)

// ---------------------------------------------------------------------------
// Run a single test and record result
// ---------------------------------------------------------------------------
#define RUN_TEST(test_fn) do { \
    greatest_test_res res_ = test_fn(); \
    switch (res_) { \
    case GREATEST_TEST_RES_PASS: \
        greatest_passed_++; \
        printf("  " GREATEST_CLR_GREEN "[PASS]" GREATEST_CLR_RESET " %s\n", #test_fn); \
        break; \
    case GREATEST_TEST_RES_FAIL: \
        greatest_failed_++; \
        printf("  " GREATEST_CLR_RED "[FAIL]" GREATEST_CLR_RESET " %s\n", #test_fn); \
        break; \
    case GREATEST_TEST_RES_SKIP: \
        greatest_skipped_++; \
        printf("  " GREATEST_CLR_YELLOW "[SKIP]" GREATEST_CLR_RESET " %s\n", #test_fn); \
        break; \
    } \
} while (0)

// ---------------------------------------------------------------------------
// Suite definition and execution
// ---------------------------------------------------------------------------
#define SUITE(name)      static void name(void)
#define RUN_SUITE(name)  do { \
    greatest_suites_++; \
    printf("== SUITE: %s ==\n", #name); \
    name(); \
    printf("\n"); \
} while (0)

// ---------------------------------------------------------------------------
// Assertion macros — print file:line on failure, then return FAIL
// ---------------------------------------------------------------------------
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("    " GREATEST_CLR_RED "ASSERT" GREATEST_CLR_RESET \
               ": %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return GREATEST_TEST_RES_FAIL; \
    } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        printf("    " GREATEST_CLR_RED "ASSERT_FALSE" GREATEST_CLR_RESET \
               ": %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return GREATEST_TEST_RES_FAIL; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("    " GREATEST_CLR_RED "ASSERT_EQ" GREATEST_CLR_RESET \
               ": %s:%d: (%s) != (%s)\n", __FILE__, __LINE__, #a, #b); \
        return GREATEST_TEST_RES_FAIL; \
    } \
} while (0)

#define ASSERT_MEM_EQ(a, b, len) do { \
    if (memcmp((a), (b), (len)) != 0) { \
        printf("    " GREATEST_CLR_RED "ASSERT_MEM_EQ" GREATEST_CLR_RESET \
               ": %s:%d: memcmp(%s, %s, %s) != 0\n", \
               __FILE__, __LINE__, #a, #b, #len); \
        return GREATEST_TEST_RES_FAIL; \
    } \
} while (0)
