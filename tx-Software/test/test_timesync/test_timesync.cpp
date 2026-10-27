// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_timesync.cpp – Unit-Tests für das Zeitmodell (Master-Uhr → lokal)
// ============================================================================

#include <unity.h>
#include <cstdint>

// Getestet wird der echte Produktionscode (src/time_sync.cpp)
#include "time_sync.h"

using ardf::timesync::TimeModel;

static TimeModel model;

// Deterministischer Jitter im Bereich ±amp µs
static int64_t jitter(uint32_t& seed, int64_t amp) {
    seed = seed * 1103515245u + 12345u;
    return (int64_t)((seed >> 8) % (uint32_t)(2 * amp + 1)) - amp;
}

// Lokale Uhr = ref·(1 + ppm·1e-6) + offset (+ Jitter), Messung alle 500 ms
static void feed(int64_t offset_us, double ppm, uint16_t n, int64_t jitter_amp) {
    uint32_t seed = 42;
    model.clear();
    for (uint16_t i = 0; i < n; i++) {
        uint64_t ref = 10000000ULL + (uint64_t)i * 500000ULL;
        double local = (double)ref * (1.0 + ppm * 1e-6) + (double)offset_us;
        model.add(ref, (uint64_t)((int64_t)local + jitter(seed, jitter_amp)));
    }
    model.compute();
}

static int64_t errorAt(uint64_t ref, int64_t offset_us, double ppm) {
    double expected = (double)ref * (1.0 + ppm * 1e-6) + (double)offset_us;
    return (int64_t)model.toLocal(ref) - (int64_t)expected;
}

void test_empty_is_identity(void) {
    model.clear();
    model.compute();
    TEST_ASSERT_TRUE(model.toLocal(123456789ULL) == 123456789ULL);
}

void test_constant_offset(void) {
    feed(-3000000, 0.0, 20, 300);   // zu wenige Punkte für Drift → nur Offset
    TEST_ASSERT_FALSE(model.hasDrift());
    int64_t err = errorAt(50000000ULL, -3000000, 0.0);
    TEST_ASSERT_INT32_WITHIN(300, 0, (int32_t)err);
}

// Befund 1.10: Vorzeichen – positive und negative Drift werden korrekt
// erkannt und auf drei Stunden hochgerechnet
void test_positive_drift_3h(void) {
    feed(2500000, 40.0, 120, 200);
    TEST_ASSERT_TRUE(model.hasDrift());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 40.0f, model.driftPpm());
    uint64_t ref_3h = 10000000ULL + 3ULL * 3600ULL * 1000000ULL;
    TEST_ASSERT_INT32_WITHIN(10000, 0, (int32_t)errorAt(ref_3h, 2500000, 40.0));
}

void test_negative_drift_3h(void) {
    feed(-700000, -55.0, 120, 200);
    TEST_ASSERT_TRUE(model.hasDrift());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -55.0f, model.driftPpm());
    uint64_t ref_3h = 10000000ULL + 3ULL * 3600ULL * 1000000ULL;
    TEST_ASSERT_INT32_WITHIN(10000, 0, (int32_t)errorAt(ref_3h, -700000, -55.0));
}

// Einzelne Ausreißer (verspätete Pakete) verfälschen das Modell nicht
void test_outliers_ignored(void) {
    model.clear();
    for (uint16_t i = 0; i < 100; i++) {
        uint64_t ref = 10000000ULL + (uint64_t)i * 500000ULL;
        uint64_t local = ref + 1000000ULL + ((i % 17 == 0) ? 80000ULL : 0ULL);
        model.add(ref, local);
    }
    model.compute();
    TEST_ASSERT_TRUE(model.inliers() < model.samples());
    TEST_ASSERT_INT32_WITHIN(500, 0, (int32_t)((int64_t)model.toLocal(60000000ULL) - 61000000LL));
}

// Unplausible Steigung (> DRIFT_PPM_MAX_VALID) wird nicht verwendet
void test_implausible_drift_rejected(void) {
    feed(0, 400.0, 120, 50);
    TEST_ASSERT_FALSE(model.hasDrift());
}

// Ringpuffer: nur die neuesten Messwerte zählen
void test_ring_buffer(void) {
    model.clear();
    for (uint16_t i = 0; i < 2 * TIME_MAX_SAMPLES; i++) {
        uint64_t ref = (uint64_t)i * 500000ULL;
        // Offset springt nach dem ersten Pufferdurchlauf
        uint64_t off = (i < TIME_MAX_SAMPLES) ? 9000000ULL : 1000000ULL;
        model.add(ref, ref + off);
    }
    model.compute();
    TEST_ASSERT_EQUAL_UINT16(TIME_MAX_SAMPLES, model.samples());
    TEST_ASSERT_TRUE(model.toLocal(300000000ULL) == 301000000ULL);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_empty_is_identity);
    RUN_TEST(test_constant_offset);
    RUN_TEST(test_positive_drift_3h);
    RUN_TEST(test_negative_drift_3h);
    RUN_TEST(test_outliers_ignored);
    RUN_TEST(test_implausible_drift_rejected);
    RUN_TEST(test_ring_buffer);

    return UNITY_END();
}
