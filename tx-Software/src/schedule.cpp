// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// schedule.cpp – Slotplan Implementierung
// ============================================================================

#include "schedule.h"
#include "duty_guard.h"
#include "fox_profile.h"

namespace ardf {
namespace schedule {

static uint32_t maxU32(uint32_t a, uint32_t b) { return (a > b) ? a : b; }

// Reserve für Taktabweichungen zwischen Referenz- und lokaler Uhr
// (DRIFT_PPM_MAX_VALID + 1 ms), damit der Laufzeit-Guard nie knapp blockiert
static uint32_t withDriftMargin(uint32_t ms) {
    return ms + (uint32_t)((uint64_t)ms * DRIFT_PPM_MAX_VALID / 1000000ULL) + 1;
}

CyclePlan computePlan(uint8_t profile_id, const FoxTiming* foxes, uint8_t n) {
    const FoxProfile& prof = getProfile(profile_id);
    CyclePlan plan = {};

    // Fox-IDs eindeutig und aufsteigend, längste Sendezeiten bestimmen
    for (uint8_t i = 0; i < n; i++) {
        plan.tx_norm_ms = maxU32(plan.tx_norm_ms, foxes[i].tx_norm_ms);
        plan.tx_max_ms  = maxU32(plan.tx_max_ms, maxU32(foxes[i].tx_max_ms, foxes[i].tx_norm_ms));

        bool known = false;
        for (uint8_t j = 0; j < plan.count; j++) {
            if (plan.fox_ids[j] == foxes[i].fox_id) known = true;
        }
        if (known || plan.count >= MAX_FOXES) continue;

        uint8_t pos = plan.count++;
        while (pos > 0 && plan.fox_ids[pos - 1] > foxes[i].fox_id) {
            plan.fox_ids[pos] = plan.fox_ids[pos - 1];
            pos--;
        }
        plan.fox_ids[pos] = foxes[i].fox_id;
    }
    if (plan.count == 0) plan.count = 1;

    // Profil D: Solo, fester Zyklus (mindestens ISM-sicher)
    if (prof.is_continuous) {
        plan.cycle_ms = maxU32(prof.cycle_ms, duty::calculateMinCycleMs(plan.tx_norm_ms));
        plan.slot_ms  = plan.cycle_ms;
        plan.extended = plan.cycle_ms > prof.cycle_ms;
        return plan;
    }

    uint32_t nominal_ms = prof.slot_ms * plan.count;
    uint32_t slot_need  = maxU32(prof.slot_ms, plan.tx_max_ms + SLOT_GUARD_MS);

    uint32_t cycle = slot_need * plan.count;
    cycle = maxU32(cycle, withDriftMargin(duty::calculateMinCycleMs(plan.tx_norm_ms, false)));
    cycle = maxU32(cycle, withDriftMargin(duty::calculateMinCycleMs(plan.tx_max_ms, true)));

    plan.cycle_ms = cycle;
    plan.slot_ms  = cycle / plan.count;
    plan.extended = cycle > nominal_ms;
    return plan;
}

int8_t slotIndex(const CyclePlan& plan, uint8_t fox_id) {
    for (uint8_t i = 0; i < plan.count; i++) {
        if (plan.fox_ids[i] == fox_id) return (int8_t)i;
    }
    return -1;
}

uint64_t slotStartUs(uint64_t plan_start_us, const CyclePlan& plan,
                     uint8_t slot_idx, uint32_t k) {
    return plan_start_us
         + (uint64_t)k * plan.cycle_ms * 1000ULL
         + (uint64_t)slot_idx * plan.slot_ms * 1000ULL;
}

} // namespace schedule
} // namespace ardf
