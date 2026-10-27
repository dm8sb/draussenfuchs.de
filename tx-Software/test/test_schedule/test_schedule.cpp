// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_schedule.cpp – Unit-Tests für den Slotplan
// ============================================================================

#include <unity.h>
#include <cstdint>

// Getestet wird der echte Produktionscode (src/schedule.cpp)
#include "config.h"
#include "duty_guard.h"
#include "fox_profile.h"
#include "schedule.h"

using namespace ardf;
using schedule::CyclePlan;
using schedule::FoxTiming;

static FoxTiming timing(uint8_t profile_id, uint8_t fox_id, const char* callsign = nullptr) {
    FoxTiming t;
    t.fox_id = fox_id;
    t.tx_norm_ms = txBudgetMs(profile_id, fox_id, nullptr);
    t.tx_max_ms = txBudgetMs(profile_id, fox_id, callsign);
    return t;
}

static CyclePlan planFor(uint8_t profile_id, const uint8_t* ids, uint8_t n,
                         const char* callsign = nullptr) {
    FoxTiming t[MAX_FOXES];
    for (uint8_t i = 0; i < n; i++) t[i] = timing(profile_id, ids[i], callsign);
    return schedule::computePlan(profile_id, t, n);
}

// 5 Füchse: nominale Profilwerte, Slotreihenfolge nach Fox-ID
void test_five_foxes_nominal(void) {
    const uint8_t ids[] = {3, 5, 1, 4, 2};
    for (uint8_t p = PROFILE_A_ID; p <= PROFILE_C_ID; p++) {
        CyclePlan plan = planFor(p, ids, 5);
        TEST_ASSERT_EQUAL_UINT32(getProfile(p).cycle_ms, plan.cycle_ms);
        TEST_ASSERT_EQUAL_UINT32(getProfile(p).slot_ms, plan.slot_ms);
        TEST_ASSERT_EQUAL_UINT8(5, plan.count);
        for (uint8_t i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT8(i + 1, plan.fox_ids[i]);
        TEST_ASSERT_FALSE(plan.extended);
    }
}

// Befund 1.5: Der Plan hängt nicht davon ab, welcher Fuchs ihn berechnet –
// maßgeblich ist die längste Aussendung im Verbund (hier MO5)
void test_plan_uses_longest_fox(void) {
    const uint8_t ids_a[] = {1, 5};
    const uint8_t ids_b[] = {5, 1};
    CyclePlan a = planFor(PROFILE_C_ID, ids_a, 2);
    CyclePlan b = planFor(PROFILE_C_ID, ids_b, 2);
    TEST_ASSERT_EQUAL_UINT32(a.cycle_ms, b.cycle_ms);
    TEST_ASSERT_EQUAL_UINT32(a.slot_ms, b.slot_ms);
    // MO5: 1670 ms → ISM-Mindestzyklus 18458 ms (+ Driftreserve)
    TEST_ASSERT_TRUE(a.cycle_ms >= duty::calculateMinCycleMs(1670));
    TEST_ASSERT_TRUE(a.cycle_ms <= duty::calculateMinCycleMs(1670) + 15);
    TEST_ASSERT_TRUE(a.extended);
}

// Alle Profile, alle Fox-Teilmengen: Duty ≤ 9,5 % auch mit Driftreserve
void test_all_subsets_ism(void) {
    for (uint8_t p = PROFILE_A_ID; p <= PROFILE_C_ID; p++) {
        for (uint8_t mask = 1; mask < 32; mask++) {
            uint8_t ids[MAX_FOXES];
            uint8_t n = 0;
            for (uint8_t f = 0; f < MAX_FOXES; f++) {
                if (mask & (1 << f)) ids[n++] = f + 1;
            }
            CyclePlan plan = planFor(p, ids, n);
            // Lokaler Abstand bei −100 ppm Abweichung zur Master-Uhr
            uint32_t local_cycle = plan.cycle_ms - plan.cycle_ms / 10000;
            TEST_ASSERT_TRUE(local_cycle >= duty::calculateMinCycleMs(plan.tx_norm_ms));
            TEST_ASSERT_TRUE(duty::calculateDutyPercent(plan.tx_norm_ms, plan.cycle_ms) <= 9.5f);
            TEST_ASSERT_TRUE(plan.slot_ms >= plan.tx_max_ms + SLOT_GUARD_MS);
            TEST_ASSERT_TRUE(plan.slot_ms * plan.count <= plan.cycle_ms);
        }
    }
}

// Befund 2.4: Rufzeichen-Slot passt in den Slot (inkl. Schutzabstand)
void test_callsign_slot_fits(void) {
    const uint8_t ids[] = {1, 2, 3, 4, 5};
    for (uint8_t p = PROFILE_A_ID; p <= PROFILE_C_ID; p++) {
        CyclePlan plan = planFor(p, ids, 5, "DL1ABC/P");
        uint32_t longest = txBudgetMs(p, 5, "DL1ABC/P");
        TEST_ASSERT_EQUAL_UINT32(longest, plan.tx_max_ms);
        TEST_ASSERT_TRUE(plan.slot_ms >= longest + SLOT_GUARD_MS);
        TEST_ASSERT_TRUE(duty::calculateDutyPercent(plan.tx_max_ms, plan.cycle_ms) <= 50.0f);
        TEST_ASSERT_TRUE(duty::calculateDutyPercent(plan.tx_norm_ms, plan.cycle_ms) <= 9.5f);
    }
}

// AFU: alle Fox-Teilmengen mit Rufzeichen – Rufzeichen-Slot passt, Duty-Grenzen
// (9,5 % normal, 50 % Rufzeichen-Slot) auch mit Taktreserve eingehalten
void test_all_subsets_afu(void) {
    for (uint8_t p = PROFILE_A_ID; p <= PROFILE_C_ID; p++) {
        for (uint8_t mask = 1; mask < 32; mask++) {
            uint8_t ids[MAX_FOXES];
            uint8_t n = 0;
            for (uint8_t f = 0; f < MAX_FOXES; f++) {
                if (mask & (1 << f)) ids[n++] = f + 1;
            }
            CyclePlan plan = planFor(p, ids, n, "DL1ABC/P");
            uint32_t local_cycle = plan.cycle_ms - plan.cycle_ms / 10000;
            TEST_ASSERT_TRUE(local_cycle >= duty::calculateMinCycleMs(plan.tx_norm_ms));
            TEST_ASSERT_TRUE(local_cycle >= duty::calculateMinCycleMs(plan.tx_max_ms, true));
            TEST_ASSERT_TRUE(plan.slot_ms >= plan.tx_max_ms + SLOT_GUARD_MS);
        }
    }
}

// Doppelte Fox-IDs werden zusammengefasst
void test_duplicates_merged(void) {
    const uint8_t ids[] = {2, 2, 4};
    CyclePlan plan = planFor(PROFILE_C_ID, ids, 3);
    TEST_ASSERT_EQUAL_UINT8(2, plan.count);
    TEST_ASSERT_EQUAL_UINT8(2, plan.fox_ids[0]);
    TEST_ASSERT_EQUAL_UINT8(4, plan.fox_ids[1]);
}

// Profil D: Solo-Zyklus nach Budget-Definition (220 ms → 2432 ms)
void test_profile_d(void) {
    const uint8_t ids[] = {3};
    CyclePlan plan = planFor(PROFILE_D_ID, ids, 1);
    TEST_ASSERT_EQUAL_UINT32(2432, plan.cycle_ms);
    TEST_ASSERT_EQUAL_UINT32(2432, plan.slot_ms);
}

void test_slot_index_and_start(void) {
    const uint8_t ids[] = {1, 3, 5};
    CyclePlan plan = planFor(PROFILE_C_ID, ids, 3);
    TEST_ASSERT_EQUAL_INT8(0, schedule::slotIndex(plan, 1));
    TEST_ASSERT_EQUAL_INT8(1, schedule::slotIndex(plan, 3));
    TEST_ASSERT_EQUAL_INT8(2, schedule::slotIndex(plan, 5));
    TEST_ASSERT_EQUAL_INT8(-1, schedule::slotIndex(plan, 2));

    uint64_t start = 5000000ULL;
    uint64_t t = schedule::slotStartUs(start, plan, 2, 10);
    TEST_ASSERT_TRUE(t == start + 10ULL * plan.cycle_ms * 1000ULL + 2ULL * plan.slot_ms * 1000ULL);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_five_foxes_nominal);
    RUN_TEST(test_plan_uses_longest_fox);
    RUN_TEST(test_all_subsets_ism);
    RUN_TEST(test_callsign_slot_fits);
    RUN_TEST(test_all_subsets_afu);
    RUN_TEST(test_duplicates_merged);
    RUN_TEST(test_profile_d);
    RUN_TEST(test_slot_index_and_start);

    return UNITY_END();
}
