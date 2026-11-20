/*******************************************************************************
 * test_lut.c - Response-curve lookup table accuracy tests
 *
 * Tests the real curve_lookup() from src/input/input.c against powf().
 ******************************************************************************/

#include "test_framework.h"
#include "fake_platform.h"

TEST(curve_endpoints_exact) {
    ASSERT_FLOAT_EQ(curve_lookup(0.0f, 1.8f), 0.0f, 0.000001f);
    ASSERT_FLOAT_EQ(curve_lookup(1.0f, 1.8f), 1.0f, 0.000001f);
    TEST_PASS();
}

/* With linear interpolation between entries (K12) the table tracks powf to
   better than 1e-4 everywhere. Truncating to the lower entry, as before, is
   off by up to 0.007 and fails this test. */
TEST(curve_matches_powf_default_curve) {
    float worst = 0.0f;
    for (int i = 0; i <= 10000; i++) {
        float x = (float)i / 10000.0f;
        float error = fabsf(curve_lookup(x, 1.8f) - powf(x, 1.8f));
        if (error > worst) worst = error;
    }
    ASSERT(worst < 0.0001f);
    TEST_PASS();
}

TEST(curve_rebuilds_for_other_curves) {
    float curves[] = {1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 1.8f};
    for (int c = 0; c < 6; c++) {
        for (int i = 1; i <= 100; i++) {
            float x = (float)i / 100.0f;
            ASSERT(fabsf(curve_lookup(x, curves[c]) - powf(x, curves[c])) < 0.0002f);
        }
    }
    // Curves below 1 are steep near zero; still close beyond the first entries
    ASSERT(fabsf(curve_lookup(0.5f, 0.5f) - powf(0.5f, 0.5f)) < 0.0002f);
    TEST_PASS();
}

TEST(curve_symmetric_for_negative_input) {
    for (int i = 0; i <= 100; i++) {
        float x = (float)i / 100.0f;
        ASSERT_EQ(curve_lookup(-x, 1.8f), curve_lookup(x, 1.8f));
    }
    TEST_PASS();
}

TEST(curve_clamps_beyond_full_tilt) {
    ASSERT_FLOAT_EQ(curve_lookup(1.5f, 1.8f), 1.0f, 0.000001f);
    ASSERT_FLOAT_EQ(curve_lookup(-7.0f, 1.8f), 1.0f, 0.000001f);
    ASSERT_FLOAT_EQ(curve_lookup(INFINITY, 1.8f), 1.0f, 0.000001f);
    ASSERT_FLOAT_EQ(curve_lookup(NAN, 1.8f), 1.0f, 0.000001f);   // never reads out of bounds
    TEST_PASS();
}

/* Strictly increasing at a resolution finer than the table: a truncating
   lookup is flat between entries and fails this. */
TEST(curve_is_strictly_monotonic) {
    float prev = curve_lookup(0.0f, 1.8f);
    for (int i = 1; i <= 2048; i++) {
        float value = curve_lookup((float)i / 2048.0f, 1.8f);
        ASSERT(value > prev);
        prev = value;
    }
    TEST_PASS();
}

int main(void) {
    TEST_SUITE("Response Curve Lookup");

    RUN_TEST(curve_endpoints_exact);
    RUN_TEST(curve_matches_powf_default_curve);
    RUN_TEST(curve_rebuilds_for_other_curves);
    RUN_TEST(curve_symmetric_for_negative_input);
    RUN_TEST(curve_clamps_beyond_full_tilt);
    RUN_TEST(curve_is_strictly_monotonic);

    TEST_SUMMARY();
    return TEST_EXIT_CODE();
}
