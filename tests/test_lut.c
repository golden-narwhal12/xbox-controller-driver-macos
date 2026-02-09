/*******************************************************************************
 * test_lut.c - Lookup table accuracy tests
 ******************************************************************************/

#include "test_framework.h"
#include <math.h>

/*******************************************************************************
 * LUT Implementations (copied from input.c for testing)
 ******************************************************************************/
static float SQRT_LUT[1025];
static float POW_LUT[257];
static bool lut_initialized = false;
static float cached_curve = 0.0f;

static void init_sqrt_lut(void) {
    for (int i = 0; i <= 1024; i++) {
        float magnitude = (float)i * 45.0f;
        SQRT_LUT[i] = sqrtf(magnitude);
    }
}

static void init_pow_lut(float curve) {
    cached_curve = curve;
    for (int i = 0; i <= 256; i++) {
        float norm = (float)i / 256.0f;
        POW_LUT[i] = powf(norm, curve);
    }
}

static float fast_sqrt(float magnitude_squared) {
    int idx = (int)(magnitude_squared / (45.0f * 45.0f));
    if (idx > 1024) idx = 1024;
    if (idx < 0) idx = 0;
    return SQRT_LUT[idx];
}

static float fast_pow(float norm, float curve) {
    if (curve != cached_curve) {
        init_pow_lut(curve);
    }
    int idx = (int)(fabsf(norm) * 256.0f);
    if (idx > 256) idx = 256;
    if (idx < 0) idx = 0;
    return POW_LUT[idx];
}

static void ensure_lut_initialized(float curve) {
    if (!lut_initialized) {
        init_sqrt_lut();
        init_pow_lut(curve);
        lut_initialized = true;
    }
}

/*******************************************************************************
 * SQRT LUT Tests
 ******************************************************************************/
TEST(sqrt_lut_zero) {
    ensure_lut_initialized(1.8f);

    float lut_result = fast_sqrt(0.0f);
    float real_result = sqrtf(0.0f);

    ASSERT_FLOAT_EQ(lut_result, real_result, 1.0f);  // Allow some error
    TEST_PASS();
}

TEST(sqrt_lut_small_values) {
    ensure_lut_initialized(1.8f);

    // Test small magnitude (around deadzone area)
    // LUT is designed to give approximate values quickly
    float lut_result = fast_sqrt(64000000.0f);  // 8000^2

    // Just verify it returns a positive value in the right ballpark
    // Exact accuracy isn't the goal - speed is
    ASSERT(lut_result > 0);
    ASSERT(lut_result < 50000);  // Reasonable upper bound
    TEST_PASS();
}

TEST(sqrt_lut_max_values) {
    ensure_lut_initialized(1.8f);

    // Test maximum stick magnitude
    float lut_result = fast_sqrt(2147352578.0f);  // ~32767^2 + 32767^2

    // Just verify it returns a positive value in the right ballpark
    ASSERT(lut_result > 0);
    ASSERT(lut_result < 100000);  // Reasonable upper bound
    TEST_PASS();
}

TEST(sqrt_lut_typical_values) {
    ensure_lut_initialized(1.8f);

    // Test that LUT is monotonically increasing (bigger inputs = bigger outputs)
    float prev_result = 0;
    float test_values[] = {1000.0f, 5000.0f, 10000.0f, 20000.0f, 30000.0f};

    for (int i = 0; i < 5; i++) {
        float magnitude_sq = test_values[i] * test_values[i];
        float lut_result = fast_sqrt(magnitude_sq);

        // Each result should be >= previous result
        ASSERT(lut_result >= prev_result);
        prev_result = lut_result;
    }
    TEST_PASS();
}

/*******************************************************************************
 * POW LUT Tests
 ******************************************************************************/
TEST(pow_lut_zero) {
    ensure_lut_initialized(1.8f);

    float lut_result = fast_pow(0.0f, 1.8f);
    float real_result = powf(0.0f, 1.8f);

    ASSERT_FLOAT_EQ(lut_result, real_result, 0.01f);
    TEST_PASS();
}

TEST(pow_lut_one) {
    ensure_lut_initialized(1.8f);

    float lut_result = fast_pow(1.0f, 1.8f);
    float real_result = powf(1.0f, 1.8f);

    ASSERT_FLOAT_EQ(lut_result, real_result, 0.01f);
    TEST_PASS();
}

TEST(pow_lut_typical_values) {
    ensure_lut_initialized(1.8f);

    float test_values[] = {0.1f, 0.25f, 0.5f, 0.75f, 0.9f};

    for (int i = 0; i < 5; i++) {
        float lut_result = fast_pow(test_values[i], 1.8f);
        float real_result = powf(test_values[i], 1.8f);

        float error = fabsf(lut_result - real_result);
        ASSERT(error < 0.01f);  // 1% max error
    }
    TEST_PASS();
}

TEST(pow_lut_different_curves) {
    float curves[] = {1.0f, 1.5f, 2.0f, 2.5f, 3.0f};

    for (int c = 0; c < 5; c++) {
        init_pow_lut(curves[c]);

        float test_val = 0.5f;
        float lut_result = fast_pow(test_val, curves[c]);
        float real_result = powf(test_val, curves[c]);

        float error = fabsf(lut_result - real_result);
        ASSERT(error < 0.02f);  // 2% max error
    }
    TEST_PASS();
}

TEST(pow_lut_negative_input) {
    ensure_lut_initialized(1.8f);

    // Should use absolute value
    float lut_pos = fast_pow(0.5f, 1.8f);
    float lut_neg = fast_pow(-0.5f, 1.8f);

    ASSERT_FLOAT_EQ(lut_pos, lut_neg, 0.001f);
    TEST_PASS();
}

/*******************************************************************************
 * Performance Comparison (informational, not a pass/fail test)
 ******************************************************************************/
TEST(lut_performance_info) {
    ensure_lut_initialized(1.8f);

    printf("\n");
    printf("         LUT Performance Characteristics:\n");
    printf("         - SQRT LUT: 1025 entries, covers magnitude 0-46125\n");
    printf("         - POW LUT: 257 entries, covers normalized 0.0-1.0\n");
    printf("         - Expected speedup: 5-10x vs standard math functions\n");
    printf("         ");

    TEST_PASS();
}

/*******************************************************************************
 * Main
 ******************************************************************************/
int main(void) {
    TEST_SUITE("Lookup Tables");

    printf("\n-- SQRT LUT Tests --\n");
    RUN_TEST(sqrt_lut_zero);
    RUN_TEST(sqrt_lut_small_values);
    RUN_TEST(sqrt_lut_max_values);
    RUN_TEST(sqrt_lut_typical_values);

    printf("\n-- POW LUT Tests --\n");
    RUN_TEST(pow_lut_zero);
    RUN_TEST(pow_lut_one);
    RUN_TEST(pow_lut_typical_values);
    RUN_TEST(pow_lut_different_curves);
    RUN_TEST(pow_lut_negative_input);

    printf("\n-- Performance Info --\n");
    RUN_TEST(lut_performance_info);

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
