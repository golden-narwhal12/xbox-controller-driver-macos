/*******************************************************************************
 * test_framework.h - Simple unit test framework
 ******************************************************************************/

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

/*******************************************************************************
 * Test State
 ******************************************************************************/
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static const char *g_current_suite = NULL;

/*******************************************************************************
 * Test Macros
 ******************************************************************************/
#define TEST_SUITE(name) \
    do { \
        g_current_suite = name; \
        printf("\n=== Test Suite: %s ===\n", name); \
    } while(0)

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        g_tests_run++; \
        printf("  [TEST] %s... ", #name); \
        test_##name(); \
    } \
    static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            printf("FAILED\n"); \
            printf("         Assertion failed: %s\n", #condition); \
            printf("         File: %s, Line: %d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAILED\n"); \
            printf("         Expected: %s == %s\n", #a, #b); \
            printf("         File: %s, Line: %d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NEQ(a, b) \
    do { \
        if ((a) == (b)) { \
            printf("FAILED\n"); \
            printf("         Expected: %s != %s\n", #a, #b); \
            printf("         File: %s, Line: %d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            printf("FAILED\n"); \
            printf("         Expected: \"%s\" == \"%s\"\n", (a), (b)); \
            printf("         File: %s, Line: %d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FLOAT_EQ(a, b, epsilon) \
    do { \
        if (fabsf((a) - (b)) > (epsilon)) { \
            printf("FAILED\n"); \
            printf("         Expected: %f ~= %f (epsilon=%f)\n", (float)(a), (float)(b), (float)(epsilon)); \
            printf("         File: %s, Line: %d\n", __FILE__, __LINE__); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(condition) ASSERT(condition)
#define ASSERT_FALSE(condition) ASSERT(!(condition))
#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)

#define TEST_PASS() \
    do { \
        printf("PASSED\n"); \
        g_tests_passed++; \
    } while(0)

#define TEST_SUMMARY() \
    do { \
        printf("\n=== Test Summary ===\n"); \
        printf("  Total:  %d\n", g_tests_run); \
        printf("  Passed: %d\n", g_tests_passed); \
        printf("  Failed: %d\n", g_tests_failed); \
        printf("\n"); \
        if (g_tests_failed == 0) { \
            printf("All tests passed!\n"); \
        } else { \
            printf("Some tests FAILED!\n"); \
        } \
    } while(0)

#define TEST_EXIT_CODE() (g_tests_failed > 0 ? 1 : 0)

#endif // TEST_FRAMEWORK_H
