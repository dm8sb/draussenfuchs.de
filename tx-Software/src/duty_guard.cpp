// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// duty_guard.cpp – Duty-Cycle-Überwachung Implementierung
// ============================================================================

#include "duty_guard.h"
#include "config.h"
#include "fox_profile.h"
#include <cmath>

namespace ardf {
namespace duty {

uint32_t calculateMinCycleMs(uint32_t tx_on_ms) {
    // Formel: ceil(tx_on_ms * 1.05 / 0.095)
    // Der Faktor 1.05 bildet den 5%-Sicherheitsaufschlag des Duty-Guards ab
    float min_cycle = (float)tx_on_ms * DUTY_SAFETY_FACTOR / DUTY_LIMIT_ISM;
    return (uint32_t)ceilf(min_cycle);
}

uint32_t calculateEffectiveCycleMs(uint8_t profile_id, uint8_t active_count,
                                    uint32_t tx_on_ms) {
    const FoxProfile& prof = getProfile(profile_id);

    // Profil D: fester Zyklus, mindestens aber der ISM-sichere Mindestzyklus
    if (prof.is_continuous) {
        uint32_t min_cycle_ms = calculateMinCycleMs(tx_on_ms);
        return (prof.cycle_ms > min_cycle_ms) ? prof.cycle_ms : min_cycle_ms;
    }

    if (active_count == 0) active_count = 1;

    // Gewünschter Zyklus basierend auf aktiven Füchsen
    uint32_t desired_cycle_ms = prof.slot_ms * active_count;

    // Minimaler Zyklus für ISM-Konformität
    uint32_t min_cycle_ms = calculateMinCycleMs(tx_on_ms);

    // Effektiver Zyklus: Maximum aus gewünscht und minimal
    return (desired_cycle_ms > min_cycle_ms) ? desired_cycle_ms : min_cycle_ms;
}

float calculateDutyPercent(uint32_t tx_on_ms, uint32_t cycle_ms) {
    if (cycle_ms == 0) return 100.0f;
    // TX-Zeit mit Sicherheitsaufschlag
    float effective_tx = (float)tx_on_ms * DUTY_SAFETY_FACTOR;
    return (effective_tx / (float)cycle_ms) * 100.0f;
}

GuardCheck checkDuty(uint8_t profile_id, uint8_t fox_id,
                     uint8_t active_count, uint32_t tx_on_ms,
                     bool afu_callsign_slot) {
    GuardCheck result;
    result.tx_on_ms = tx_on_ms;

    const FoxProfile& prof = getProfile(profile_id);

    // Profil D: fester Zyklus, bei Bedarf auf den ISM-sicheren Mindestzyklus verlängert
    if (prof.is_continuous) {
        result.effective_cycle_ms = calculateEffectiveCycleMs(profile_id, 1, tx_on_ms);
        result.effective_slot_ms  = result.effective_cycle_ms;
        result.duty_percent = calculateDutyPercent(tx_on_ms, result.effective_cycle_ms);
        if (result.effective_cycle_ms > prof.cycle_ms) {
            result.result = GuardResult::VERLAENGERT;
            result.decision_text = "VERLÄNGERT";
        } else {
            result.result = GuardResult::OK;
            result.decision_text = "OK";
        }
        return result;
    }

    if (active_count == 0) active_count = 1;

    // Duty-Grenze bestimmen
    float duty_limit = DUTY_LIMIT_ISM; // 9,5%
    if (afu_callsign_slot) {
        duty_limit = DUTY_LIMIT_AFU_CALLSIGN; // 50%
    }

    // Gewünschter Zyklus
    uint32_t desired_cycle_ms = prof.slot_ms * active_count;

    // Minimaler Zyklus
    float min_cycle_f = (float)tx_on_ms * DUTY_SAFETY_FACTOR / duty_limit;
    uint32_t min_cycle_ms = (uint32_t)ceilf(min_cycle_f);

    // Effektiver Zyklus
    result.effective_cycle_ms = (desired_cycle_ms > min_cycle_ms)
                                 ? desired_cycle_ms : min_cycle_ms;
    result.effective_slot_ms = result.effective_cycle_ms / active_count;

    // Duty berechnen
    float effective_tx = (float)tx_on_ms * DUTY_SAFETY_FACTOR;
    result.duty_percent = (effective_tx / (float)result.effective_cycle_ms) * 100.0f;

    // Entscheidung
    if (result.duty_percent <= duty_limit * 100.0f) {
        if (result.effective_cycle_ms > desired_cycle_ms) {
            result.result = GuardResult::VERLAENGERT;
            result.decision_text = "VERLÄNGERT";
        } else {
            result.result = GuardResult::OK;
            result.decision_text = "OK";
        }
    } else {
        result.result = GuardResult::BLOCKIERT;
        result.decision_text = "BLOCKIERT";
    }

    return result;
}

} // namespace duty
} // namespace ardf
