// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_duty.cpp – Unit-Tests für Sendezeit-Budget und Duty-Guard
// ============================================================================

#include <unity.h>
#include <cstdint>

// Getestet wird der echte Produktionscode (src/duty_guard.cpp, src/fox_profile.cpp)
#include "config.h"
#include "duty_guard.h"
#include "fox_profile.h"

using namespace ardf;

// ---- Sendezeit-Budget ----

// MOE bei 24 WPM: Sequenz 25 dits × 50 ms = 1250 ms + 20 ms Vorlauf
void test_budget_moe_profil_c(void) {
    TEST_ASSERT_EQUAL_UINT32(1270, txBudgetMs(PROFILE_C_ID, 1, nullptr));
}

// MO5 bei 24 WPM: Sequenz 33 dits × 50 ms = 1650 ms + 20 ms Vorlauf
void test_budget_mo5_profil_c(void) {
    TEST_ASSERT_EQUAL_UINT32(1670, txBudgetMs(PROFILE_C_ID, 5, nullptr));
}

// Profil A: 2× MO5 bei 18 WPM (dit 66 ms) + 7 dits Pause zwischen den Wiederholungen
void test_budget_mo5_profil_a(void) {
    // 2 × 33 dits + 7 dits = 73 dits × 66 ms = 4818 ms + 20 ms
    TEST_ASSERT_EQUAL_UINT32(4838, txBudgetMs(PROFILE_A_ID, 5, nullptr));
}

// Profil D: Dauerton 200 ms + 20 ms Vorlauf
void test_budget_profil_d(void) {
    TEST_ASSERT_EQUAL_UINT32(220, txBudgetMs(PROFILE_D_ID, 1, nullptr));
}

// Rufzeichen-Slot: MO-Kennung + 7 dits + Rufzeichen
void test_budget_with_callsign(void) {
    // "E" = 1 dit → MOE 25 dits + 7 dits + 1 dit = 33 dits × 50 ms + 20 ms
    TEST_ASSERT_EQUAL_UINT32(1670, txBudgetMs(PROFILE_C_ID, 1, "E"));
    // Leeres Rufzeichen = normaler Slot
    TEST_ASSERT_EQUAL_UINT32(1270, txBudgetMs(PROFILE_C_ID, 1, ""));
}

// ---- Mindestzyklus ----

void test_safety_factor_applied(void) {
    // 1000 × 1,05 / 0,095 = 11052,6 → 11053
    TEST_ASSERT_EQUAL_UINT32(11053, duty::calculateMinCycleMs(1000));
}

void test_min_cycle_edge_1ms(void) {
    uint32_t min_cycle = duty::calculateMinCycleMs(1);
    TEST_ASSERT_EQUAL_UINT32(12, min_cycle);
    TEST_ASSERT_TRUE(duty::calculateDutyPercent(1, min_cycle) <= 9.5f);
}

// ---- Duty-Guard ----

// Alle ARDF-Profile, alle Fox-IDs, 1..5 aktive Füchse: Duty ≤ 9,5 %, nie blockiert
void test_all_profiles_all_foxes_ism(void) {
    for (uint8_t p = PROFILE_A_ID; p <= PROFILE_C_ID; p++) {
        for (uint8_t fox = 1; fox <= MAX_FOXES; fox++) {
            uint32_t tx = txBudgetMs(p, fox, nullptr);
            for (uint8_t n = 1; n <= MAX_FOXES; n++) {
                duty::GuardCheck gc = duty::checkDuty(p, fox, n, tx, false);
                TEST_ASSERT_TRUE_MESSAGE(gc.duty_percent <= 9.5f, "Duty > 9,5 %");
                TEST_ASSERT_TRUE(gc.result != duty::GuardResult::BLOCKIERT);
                TEST_ASSERT_TRUE(gc.effective_cycle_ms >= getProfile(p).slot_ms * n);
            }
        }
    }
}

// 5 Füchse: nominaler Profilzyklus reicht in allen Profilen (kein Verlängern)
void test_five_foxes_nominal_cycle(void) {
    for (uint8_t p = PROFILE_A_ID; p <= PROFILE_C_ID; p++) {
        uint32_t tx = txBudgetMs(p, 5, nullptr);  // MO5 = längste Kennung
        duty::GuardCheck gc = duty::checkDuty(p, 5, 5, tx, false);
        TEST_ASSERT_EQUAL_UINT32(getProfile(p).cycle_ms, gc.effective_cycle_ms);
        TEST_ASSERT_TRUE(gc.result == duty::GuardResult::OK);
    }
}

// 1 Fuchs in Profil C: Zyklus wird auf ISM-sicheren Wert verlängert
void test_one_fox_extended(void) {
    uint32_t tx = txBudgetMs(PROFILE_C_ID, 1, nullptr);
    duty::GuardCheck gc = duty::checkDuty(PROFILE_C_ID, 1, 1, tx, false);
    TEST_ASSERT_EQUAL_UINT32(14037, gc.effective_cycle_ms);
    TEST_ASSERT_TRUE(gc.result == duty::GuardResult::VERLAENGERT);
}

// Profil D: Mindestzyklus nach Budget-Definition (220 ms × 1,05 / 0,095 = 2432 ms)
void test_profil_d_cycle(void) {
    uint32_t tx = txBudgetMs(PROFILE_D_ID, 1, nullptr);
    duty::GuardCheck gc = duty::checkDuty(PROFILE_D_ID, 1, 1, tx, false);
    TEST_ASSERT_EQUAL_UINT32(2432, gc.effective_cycle_ms);
    TEST_ASSERT_TRUE(gc.duty_percent <= 9.5f);
}

// AFU-Rufzeichen-Slot: 50-%-Grenze
void test_afu_callsign_slot(void) {
    duty::GuardCheck gc = duty::checkDuty(PROFILE_C_ID, 1, 1, 5000, true);
    TEST_ASSERT_TRUE(gc.duty_percent <= 50.0f);
    TEST_ASSERT_TRUE(gc.effective_cycle_ms >= 10500);
}

// ---- Hauptfunktion ----

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_budget_moe_profil_c);
    RUN_TEST(test_budget_mo5_profil_c);
    RUN_TEST(test_budget_mo5_profil_a);
    RUN_TEST(test_budget_profil_d);
    RUN_TEST(test_budget_with_callsign);
    RUN_TEST(test_safety_factor_applied);
    RUN_TEST(test_min_cycle_edge_1ms);
    RUN_TEST(test_all_profiles_all_foxes_ism);
    RUN_TEST(test_five_foxes_nominal_cycle);
    RUN_TEST(test_one_fox_extended);
    RUN_TEST(test_profil_d_cycle);
    RUN_TEST(test_afu_callsign_slot);

    return UNITY_END();
}
