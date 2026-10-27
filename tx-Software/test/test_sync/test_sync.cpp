// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_sync.cpp – Simulation des Verbunds (Sync-Logik ohne Funk)
// ============================================================================
//
// Mehrere Füchse mit versetzten Einschaltzeiten und driftenden Quarzen tauschen
// Beacons mit Latenz-Jitter aus. Geprüft wird, ob die tatsächlichen
// Sendezeitpunkte aller Füchse im gemeinsamen Slotraster liegen – direkt nach
// dem Start und nach drei Stunden.

#include <unity.h>
#include <cstdint>
#include <cstring>

// Getestet wird der echte Produktionscode (src/sync_logic.cpp u. a.)
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"
#include "schedule.h"
#include "sync_logic.h"

using namespace ardf;

static const uint64_t SEC = 1000000ULL;
static const uint64_t STEP_US = 5000ULL;              // Simulationsschritt
static const uint64_t NEVER = 0xFFFFFFFFFFFFFFFFULL;

struct SimNode {
    sync::Network net;
    uint8_t  mac[6];
    uint8_t  fox_id;
    uint8_t  profile_id;
    uint64_t boot_us;      // Einschaltzeitpunkt (wahre Zeit)
    uint64_t off_us;       // Ausschaltzeitpunkt (wahre Zeit)
    double   ppm;          // Quarzabweichung
    uint64_t next_beacon_us;
    bool     finished;
};

static SimNode nodes[8];
static uint8_t node_count = 0;
static uint32_t rng = 1;

static uint32_t rnd(uint32_t range) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % range;
}

static uint64_t localOf(const SimNode& n, uint64_t t) {
    return (uint64_t)((double)(t - n.boot_us) * (1.0 + n.ppm * 1e-6));
}

static double trueOf(const SimNode& n, uint64_t local) {
    return (double)n.boot_us + (double)local / (1.0 + n.ppm * 1e-6);
}

static bool alive(const SimNode& n, uint64_t t) {
    return t >= n.boot_us && t < n.off_us;
}

static void reset() {
    node_count = 0;
    rng = 1;
}

static SimNode& addNode(uint8_t mac_last, uint8_t fox_id, double boot_s, double ppm,
                        uint8_t profile_id = PROFILE_C_ID) {
    SimNode& n = nodes[node_count++];
    const uint8_t mac[6] = {0x24, 0x0A, 0xC4, 0x12, 0x34, mac_last};
    memcpy(n.mac, mac, 6);
    n.fox_id = fox_id;
    n.profile_id = profile_id;
    n.boot_us = (uint64_t)(boot_s * 1e6);
    n.off_us = NEVER;
    n.ppm = ppm;
    n.next_beacon_us = n.boot_us + 100000ULL;
    n.finished = false;

    const FoxProfile& p = getProfile(profile_id);
    sync::OwnInfo own;
    memcpy(own.mac, mac, 6);
    own.fox_id = fox_id;
    own.profile_id = profile_id;
    own.config_hash = config::calculateConfigHash(profile_id, p.slot_ms, p.wpm, true,
                                                  p.morse_repeats);
    own.afu = false;
    own.tx_norm_ms = txBudgetMs(profile_id, fox_id, nullptr);
    own.tx_max_ms = own.tx_norm_ms;
    own.boot_id = 1000u + mac_last;
    n.net.begin(own, 0);
    return n;
}

// Simulation bis alle eingeschalteten Füchse die Sync-Phase beendet haben.
// action_at_us/action: optionaler Eingriff zu einem Zeitpunkt.
static void run(uint64_t limit_us, uint64_t action_at_us = NEVER,
                void (*action)(uint64_t) = nullptr) {
    for (uint64_t t = 0; t < limit_us; t += STEP_US) {
        if (action && t == action_at_us) action(t);

        bool all_done = true;
        for (uint8_t i = 0; i < node_count; i++) {
            SimNode& n = nodes[i];
            if (n.finished || t >= n.off_us) continue;
            all_done = false;
            if (!alive(n, t)) continue;

            if (t >= n.next_beacon_us) {
                SyncBeacon b;
                n.net.fillBeacon(b, localOf(n, t));
                for (uint8_t j = 0; j < node_count; j++) {
                    SimNode& r = nodes[j];
                    if (j == i || r.finished || !alive(r, t)) continue;
                    uint64_t latency = 300 + rnd(400);
                    r.net.onBeacon(n.mac, b, localOf(r, t + latency));
                }
                n.next_beacon_us = t + (450000ULL + rnd(100000));
            }

            uint64_t now = localOf(n, t);
            n.net.tick(now);
            n.net.takeEvents();
            if (n.net.finished(now)) n.finished = true;
        }
        if (all_done) return;
    }
}

// Wahrer Beginn des eigenen Slots im Zyklus k
static double trueSlotStart(const SimNode& n, uint32_t k) {
    int8_t idx = n.net.ownSlotIndex();
    uint64_t ref = schedule::slotStartUs(n.net.planStartRefUs(), n.net.plan(), (uint8_t)idx, k);
    return trueOf(n, n.net.timeModel().toLocal(ref));
}

// Alle Füchse (ohne ausgeschaltete) haben denselben Plan, und ihre Slots liegen
// im Zyklus k höchstens tol_us neben dem Raster des ersten Slots
static void assertAligned(uint32_t k, double tol_us) {
    const SimNode* ref_node = nullptr;
    for (uint8_t i = 0; i < node_count; i++) {
        const SimNode& n = nodes[i];
        if (n.off_us != NEVER) continue;
        TEST_ASSERT_TRUE_MESSAGE(n.finished, "Fuchs hat die Sync-Phase nicht beendet");
        TEST_ASSERT_TRUE_MESSAGE(n.net.ownSlotIndex() >= 0, "Fuchs nicht im Slotplan");
        if (!ref_node) { ref_node = &n; continue; }
        TEST_ASSERT_EQUAL_UINT32(ref_node->net.plan().cycle_ms, n.net.plan().cycle_ms);
        TEST_ASSERT_EQUAL_UINT32(ref_node->net.plan().slot_ms, n.net.plan().slot_ms);
        TEST_ASSERT_EQUAL_UINT8(ref_node->net.plan().count, n.net.plan().count);
    }
    TEST_ASSERT_NOT_NULL(ref_node);

    // Raster aus dem ersten Fuchs ableiten
    double slot_us = (double)ref_node->net.plan().slot_ms * 1000.0;
    double base = trueSlotStart(*ref_node, k) - ref_node->net.ownSlotIndex() * slot_us;
    for (uint8_t i = 0; i < node_count; i++) {
        const SimNode& n = nodes[i];
        if (n.off_us != NEVER) continue;
        double expected = base + n.net.ownSlotIndex() * slot_us;
        double err = trueSlotStart(n, k) - expected;
        TEST_ASSERT_TRUE_MESSAGE(err < tol_us && err > -tol_us, "Slot liegt neben dem Raster");
    }
}

static double firstSlotTrueS() {
    for (uint8_t i = 0; i < node_count; i++) {
        if (nodes[i].off_us == NEVER) {
            const SimNode& n = nodes[i];
            return (trueSlotStart(n, 0) - n.net.ownSlotIndex() * n.net.plan().slot_ms * 1000.0) / 1e6;
        }
    }
    return -1;
}

// ---- Tests ----

// 5 Füchse, versetzt eingeschaltet, Quarze ±35 ppm. Der Fuchs mit der höchsten
// MAC kommt als vierter dazu und übernimmt den bestehenden Startzeitpunkt.
void test_five_foxes_stay_aligned_3h(void) {
    reset();
    addNode(0x10, 3, 0.0, +20.0);
    addNode(0x20, 1, 1.2, -15.0);
    addNode(0x30, 5, 2.5, +35.0);
    addNode(0xF0, 2, 4.1, -30.0);   // höchste MAC → Master
    addNode(0x40, 4, 7.3, +5.0);
    run(200 * SEC);

    TEST_ASSERT_EQUAL_UINT8(5, nodes[0].net.plan().count);
    TEST_ASSERT_EQUAL_UINT32(20000, nodes[0].net.plan().cycle_ms);
    TEST_ASSERT_TRUE(nodes[3].net.isMaster());

    // Sendebeginn = Sync-Start des ersten Fuchses + 120 s (vererbt)
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 120.0f, (float)firstSlotTrueS());

    assertAligned(0, 2000.0);       // ±2 ms beim Start
    assertAligned(540, 50000.0);    // ±50 ms nach 3 Stunden (Schutzabstand 250 ms)
}

void test_collision_detected(void) {
    reset();
    addNode(0x10, 2, 0.0, 0.0);
    addNode(0x20, 2, 1.0, 0.0);
    addNode(0x30, 4, 2.0, 0.0);
    run(200 * SEC);
    TEST_ASSERT_TRUE(nodes[0].net.collision());
    TEST_ASSERT_TRUE(nodes[1].net.collision());
    TEST_ASSERT_FALSE(nodes[2].net.collision());
}

void test_mismatch_detected(void) {
    reset();
    addNode(0x10, 1, 0.0, 0.0);
    addNode(0x20, 2, 1.0, 0.0, PROFILE_B_ID);
    run(200 * SEC);
    TEST_ASSERT_TRUE(nodes[0].net.mismatch());
    TEST_ASSERT_EQUAL_UINT8(2, nodes[0].net.mismatchFoxId());
    TEST_ASSERT_EQUAL_UINT8(PROFILE_B_ID, nodes[0].net.mismatchProfileId());
}

// Nachzügler nach dem Einfrieren: übernimmt den Plan, ist aber nicht enthalten
void test_late_joiner_not_in_plan(void) {
    reset();
    addNode(0x10, 1, 0.0, 0.0);
    addNode(0x20, 2, 1.0, 0.0);
    SimNode& late = addNode(0xF0, 3, 115.0, 0.0);
    run(200 * SEC);
    TEST_ASSERT_TRUE(late.finished);
    TEST_ASSERT_FALSE(late.net.isMaster());
    TEST_ASSERT_EQUAL_INT8(-1, late.net.ownSlotIndex());
    TEST_ASSERT_EQUAL_UINT8(2, nodes[0].net.plan().count);
}

// Master fällt vor dem Einfrieren aus: Neuwahl, Startzeit bleibt erhalten
void test_master_leaves(void) {
    reset();
    addNode(0x10, 1, 0.0, +10.0);
    addNode(0x20, 2, 1.0, -10.0);
    addNode(0x30, 3, 2.0, +25.0);
    SimNode& master = addNode(0xF0, 4, 3.0, -20.0);
    master.off_us = 60 * SEC;
    run(200 * SEC);
    TEST_ASSERT_EQUAL_UINT8(3, nodes[0].net.plan().count);
    TEST_ASSERT_TRUE(nodes[2].net.isMaster());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 120.0f, (float)firstSlotTrueS());
    assertAligned(0, 2000.0);
    assertAligned(540, 50000.0);
}

// Menüpunkt 10 am Master zieht den Sendebeginn für alle vor
static void pressStartOnMaster(uint64_t t) {
    SimNode& m = nodes[2];
    TEST_ASSERT_TRUE(m.net.requestStart(localOf(m, t)));
}

void test_request_start(void) {
    reset();
    addNode(0x10, 1, 0.0, 0.0);
    addNode(0x20, 2, 1.0, 0.0);
    addNode(0xF0, 3, 2.0, 0.0);   // Master
    run(200 * SEC, 30 * SEC, pressStartOnMaster);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 35.0f, (float)firstSlotTrueS());
    assertAligned(0, 2000.0);
    // Ein Slave kann den Start nicht vorziehen
    TEST_ASSERT_FALSE(nodes[0].net.requestStart(0));
}

// Allein-Boot: eigener Master, Start nach dem Sync-Fenster
void test_solo(void) {
    reset();
    addNode(0x10, 5, 0.0, 0.0);
    run(200 * SEC);
    TEST_ASSERT_TRUE(nodes[0].net.isMaster());
    TEST_ASSERT_EQUAL_UINT8(1, nodes[0].net.plan().count);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 120.0f, (float)firstSlotTrueS());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_five_foxes_stay_aligned_3h);
    RUN_TEST(test_collision_detected);
    RUN_TEST(test_mismatch_detected);
    RUN_TEST(test_late_joiner_not_in_plan);
    RUN_TEST(test_master_leaves);
    RUN_TEST(test_request_start);
    RUN_TEST(test_solo);

    return UNITY_END();
}
