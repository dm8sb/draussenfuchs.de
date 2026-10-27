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

// ---- AFU-Grenze ----

void test_afu_callsign_limit(void) {
    // 5000 × 1,05 / 0,50 = 10500 ms
    TEST_ASSERT_EQUAL_UINT32(10500, duty::calculateMinCycleMs(5000, true));
    TEST_ASSERT_TRUE(duty::calculateDutyPercent(5000, 10500) <= 50.0f);
}

// ---- Laufzeit-Guard ----

// Erste Aussendung ist immer erlaubt
void test_guard_first_tx_allowed(void) {
    duty::Guard g;
    TEST_ASSERT_TRUE(g.allows(0));
    TEST_ASSERT_TRUE(g.allows(123456789ULL));
}

// Nach einer Aussendung: Freigabe erst nach dem ISM-Mindestabstand
void test_guard_blocks_too_early(void) {
    duty::Guard g;
    uint64_t t0 = 1000000ULL;
    g.record(t0, 1000, false);                          // Mindestabstand 11053 ms
    TEST_ASSERT_FALSE(g.allows(t0 + 11052000ULL));
    TEST_ASSERT_TRUE(g.allows(t0 + 11053000ULL));
    TEST_ASSERT_TRUE(g.nextAllowedUs() == t0 + 11053000ULL);
}

// Profilwechsel-Szenario (Befund 2.1): Profil C mit dem 2,4-s-Zyklus von Profil D
// muss blockiert werden
void test_guard_blocks_profile_d_cycle_with_morse(void) {
    duty::Guard g;
    uint32_t tx = txBudgetMs(PROFILE_C_ID, 1, nullptr);
    g.record(0, tx, false);
    TEST_ASSERT_FALSE(g.allows(2400000ULL));
}

// AFU-Rufzeichen-Slot: Abstand nach der 50-%-Grenze
void test_guard_afu_slot(void) {
    duty::Guard g;
    g.record(0, 5000, true);                            // 10500 ms
    TEST_ASSERT_FALSE(g.allows(10499000ULL));
    TEST_ASSERT_TRUE(g.allows(10500000ULL));
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
    RUN_TEST(test_afu_callsign_limit);
    RUN_TEST(test_guard_first_tx_allowed);
    RUN_TEST(test_guard_blocks_too_early);
    RUN_TEST(test_guard_blocks_profile_d_cycle_with_morse);
    RUN_TEST(test_guard_afu_slot);

    return UNITY_END();
}
