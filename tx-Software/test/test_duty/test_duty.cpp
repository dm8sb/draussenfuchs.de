// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_duty.cpp – Unit-Tests für den Duty-Guard
// ============================================================================

#include <unity.h>
#include <cmath>
#include <cstdint>

// ---- Testbare Konstanten (aus config.h repliziert für native Tests) ----
static const float DUTY_LIMIT_ISM          = 0.095f;
static const float DUTY_LIMIT_AFU_CALLSIGN = 0.50f;
static const float DUTY_SAFETY_FACTOR      = 1.05f;

// ---- Minimale Reimplementierung der Duty-Guard-Logik für Tests ----

static uint32_t test_calculateMinCycleMs(uint32_t tx_on_ms) {
    float min_cycle = (float)tx_on_ms * DUTY_SAFETY_FACTOR / DUTY_LIMIT_ISM;
    return (uint32_t)ceilf(min_cycle);
}

static float test_calculateDutyPercent(uint32_t tx_on_ms, uint32_t cycle_ms) {
    if (cycle_ms == 0) return 100.0f;
    float effective_tx = (float)tx_on_ms * DUTY_SAFETY_FACTOR;
    return (effective_tx / (float)cycle_ms) * 100.0f;
}

// Profilparameter
struct TestProfile {
    uint8_t  id;
    uint32_t cycle_ms;
    uint32_t slot_ms;
    uint8_t  wpm;
    uint8_t  morse_repeats;
    bool     is_continuous;
    uint16_t tone_on_ms;
};

static const TestProfile profiles[] = {
    {0, 60000, 12000, 18, 2, false, 0},    // A
    {1, 30000,  6000, 20, 1, false, 0},    // B
    {2, 20000,  4000, 24, 1, false, 0},    // C
    {3,  2400,  2400,  0, 0, true, 200},   // D
};

// ---- Tests ----

// Test: Minimaler Zyklus für Profil A (längster Fox MO5, 18 WPM, 2×)
void test_min_cycle_profil_a_mo5(void) {
    // MO5 bei 18 WPM, 2× Wiederholung
    // dit = 1200/18 ≈ 66,7 ms
    // Geschätzte TX-Zeit ca. 4880 ms + 20 ms Vorlauf ≈ 4900 ms
    uint32_t tx_on_ms = 4900;
    uint32_t min_cycle = test_calculateMinCycleMs(tx_on_ms);
    // min_cycle = ceil(4900 * 1.05 / 0.095) = ceil(54158) = 54158
    float duty = test_calculateDutyPercent(tx_on_ms, min_cycle);
    TEST_ASSERT_TRUE(duty <= 9.5f);
    TEST_ASSERT_TRUE(min_cycle > 0);
}

// Test: Minimaler Zyklus enthält Sicherheitsaufschlag
void test_safety_factor_applied(void) {
    uint32_t tx_on_ms = 1000;
    uint32_t min_cycle = test_calculateMinCycleMs(tx_on_ms);
    // Ohne Sicherheitsaufschlag: 1000 / 0.095 = 10526
    // Mit Sicherheitsaufschlag: 1000 * 1.05 / 0.095 = 11053
    TEST_ASSERT_TRUE(min_cycle >= 11053);
}

// Test: Profil D hat exakt 8,33% Duty
void test_profil_d_duty(void) {
    float duty = (float)200 / (float)2400 * 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.33f, duty);
}

// Test: Dynamische Zyklusverkürzung hält 9,5% ein (1 Fuchs)
void test_dynamic_cycle_1_fox(void) {
    // Profil C, MOE, 24 WPM, 1× → TX ca. 1270 ms + 20 ms = 1290 ms
    uint32_t tx_on_ms = 1290;
    uint32_t min_cycle = test_calculateMinCycleMs(tx_on_ms);
    uint32_t desired_cycle = 4000 * 1; // 1 Fuchs × 4000 ms Slot
    uint32_t effective = (desired_cycle > min_cycle) ? desired_cycle : min_cycle;
    float duty = test_calculateDutyPercent(tx_on_ms, effective);
    TEST_ASSERT_TRUE(duty <= 9.5f);
}

// Test: Dynamische Zyklusverkürzung hält 9,5% ein (5 Füchse)
void test_dynamic_cycle_5_foxes(void) {
    // Profil C, MO5, 24 WPM, 1× → TX ca. 1670 ms + 20 ms = 1690 ms
    uint32_t tx_on_ms = 1690;
    uint32_t min_cycle = test_calculateMinCycleMs(tx_on_ms);
    uint32_t desired_cycle = 4000 * 5; // 5 Füchse × 4000 ms
    uint32_t effective = (desired_cycle > min_cycle) ? desired_cycle : min_cycle;
    float duty = test_calculateDutyPercent(tx_on_ms, effective);
    TEST_ASSERT_TRUE(duty <= 9.5f);
}

// Test: AFU-Ausnahme (50%-Grenze im 10. Slot)
void test_afu_exception_50_percent(void) {
    // Lange TX-Zeit im Callsign-Slot (z.B. 5000 ms)
    uint32_t tx_on_ms = 5000;
    float duty_limit = DUTY_LIMIT_AFU_CALLSIGN; // 0.50 = 50%

    // Minimaler Zyklus bei 50% Limit
    float min_cycle_f = (float)tx_on_ms * DUTY_SAFETY_FACTOR / duty_limit;
    uint32_t min_cycle = (uint32_t)ceilf(min_cycle_f);
    // = ceil(5000 * 1.05 / 0.50) = ceil(10500) = 10500
    TEST_ASSERT_TRUE(min_cycle <= 10500);

    float duty = (float)tx_on_ms * DUTY_SAFETY_FACTOR / (float)min_cycle * 100.0f;
    TEST_ASSERT_TRUE(duty <= 50.0f);
}

// Test: ISM-Grenze blockiert bei zu hoher TX-Zeit
void test_ism_blocking(void) {
    // TX-Zeit so hoch, dass 9,5% in keinem sinnvollen Zyklus eingehalten werden
    uint32_t tx_on_ms = 1000;
    uint32_t cycle_ms = 5000; // Zu kurzer Zyklus
    float duty = test_calculateDutyPercent(tx_on_ms, cycle_ms);
    // duty = 1000 * 1.05 / 5000 * 100 = 21.0%
    TEST_ASSERT_TRUE(duty > 9.5f);
}

// Test: Edge Case – minimale TX-Zeit
void test_min_tx_time(void) {
    uint32_t tx_on_ms = 1;
    uint32_t min_cycle = test_calculateMinCycleMs(tx_on_ms);
    TEST_ASSERT_TRUE(min_cycle >= 12); // ceil(1 * 1.05 / 0.095) = 12
    float duty = test_calculateDutyPercent(tx_on_ms, min_cycle);
    TEST_ASSERT_TRUE(duty <= 9.5f);
}

// Test: Edge Case – maximale TX-Zeit (Profil A, MO5, 2×)
void test_max_tx_time_profil_a(void) {
    uint32_t tx_on_ms = 5000; // Überschätzung für Sicherheit
    uint32_t min_cycle = test_calculateMinCycleMs(tx_on_ms);
    float duty = test_calculateDutyPercent(tx_on_ms, min_cycle);
    TEST_ASSERT_TRUE(duty <= 9.5f);
}

// Test: Alle Profile ISM-konform bei 5 Füchsen
void test_all_profiles_ism_5_foxes(void) {
    // Worst-Case TX-Zeiten (geschätzt)
    uint32_t worst_tx[] = {4900, 2100, 1700, 220}; // A, B, C, D

    for (int p = 0; p < 4; p++) {
        if (profiles[p].is_continuous) {
            // Profil D: fester Zyklus
            float duty = (float)profiles[p].tone_on_ms / (float)profiles[p].cycle_ms * 100.0f;
            TEST_ASSERT_TRUE(duty < 10.0f);
        } else {
            uint32_t desired = profiles[p].slot_ms * 5;
            uint32_t min_c = test_calculateMinCycleMs(worst_tx[p]);
            uint32_t effective = (desired > min_c) ? desired : min_c;
            float duty = test_calculateDutyPercent(worst_tx[p], effective);
            TEST_ASSERT_TRUE_MESSAGE(duty <= 9.5f,
                "Duty-Überschreitung bei 5 Füchsen");
        }
    }
}

// Test: Alle Profile ISM-konform bei 1 Fuchs
void test_all_profiles_ism_1_fox(void) {
    uint32_t worst_tx[] = {4900, 2100, 1700, 220};

    for (int p = 0; p < 4; p++) {
        if (profiles[p].is_continuous) {
            float duty = (float)profiles[p].tone_on_ms / (float)profiles[p].cycle_ms * 100.0f;
            TEST_ASSERT_TRUE(duty < 10.0f);
        } else {
            uint32_t desired = profiles[p].slot_ms * 1;
            uint32_t min_c = test_calculateMinCycleMs(worst_tx[p]);
            uint32_t effective = (desired > min_c) ? desired : min_c;
            float duty = test_calculateDutyPercent(worst_tx[p], effective);
            TEST_ASSERT_TRUE_MESSAGE(duty <= 9.5f,
                "Duty-Überschreitung bei 1 Fuchs");
        }
    }
}

// ---- Hauptfunktion ----

void setUp(void) {}
void tearDown(void) {}

#ifdef ARDUINO

#include <Arduino.h>

void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_min_cycle_profil_a_mo5);
    RUN_TEST(test_safety_factor_applied);
    RUN_TEST(test_profil_d_duty);
    RUN_TEST(test_dynamic_cycle_1_fox);
    RUN_TEST(test_dynamic_cycle_5_foxes);
    RUN_TEST(test_afu_exception_50_percent);
    RUN_TEST(test_ism_blocking);
    RUN_TEST(test_min_tx_time);
    RUN_TEST(test_max_tx_time_profil_a);
    RUN_TEST(test_all_profiles_ism_5_foxes);
    RUN_TEST(test_all_profiles_ism_1_fox);

    UNITY_END();
}

void loop() {}

#else

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_min_cycle_profil_a_mo5);
    RUN_TEST(test_safety_factor_applied);
    RUN_TEST(test_profil_d_duty);
    RUN_TEST(test_dynamic_cycle_1_fox);
    RUN_TEST(test_dynamic_cycle_5_foxes);
    RUN_TEST(test_afu_exception_50_percent);
    RUN_TEST(test_ism_blocking);
    RUN_TEST(test_min_tx_time);
    RUN_TEST(test_max_tx_time_profil_a);
    RUN_TEST(test_all_profiles_ism_5_foxes);
    RUN_TEST(test_all_profiles_ism_1_fox);

    return UNITY_END();
}

#endif
