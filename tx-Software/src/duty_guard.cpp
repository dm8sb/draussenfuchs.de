// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// duty_guard.cpp – Duty-Cycle-Überwachung Implementierung
// ============================================================================

#include "duty_guard.h"
#include "config.h"
#include <cmath>

namespace ardf {
namespace duty {

float limitFor(bool afu_callsign_slot) {
    return afu_callsign_slot ? DUTY_LIMIT_AFU_CALLSIGN : DUTY_LIMIT_ISM;
}

uint32_t calculateMinCycleMs(uint32_t tx_ms, bool afu_callsign_slot) {
    // Der Faktor 1.05 bildet den 5%-Sicherheitsaufschlag des Duty-Guards ab
    float min_cycle = (float)tx_ms * DUTY_SAFETY_FACTOR / limitFor(afu_callsign_slot);
    return (uint32_t)ceilf(min_cycle);
}

float calculateDutyPercent(uint32_t tx_ms, uint32_t cycle_ms) {
    if (cycle_ms == 0) return 100.0f;
    float effective_tx = (float)tx_ms * DUTY_SAFETY_FACTOR;
    return (effective_tx / (float)cycle_ms) * 100.0f;
}

bool Guard::allows(uint64_t start_us) const {
    return !has_last_ || start_us >= next_allowed_us_;
}

void Guard::record(uint64_t start_us, uint32_t tx_ms, bool afu_callsign_slot) {
    has_last_ = true;
    next_allowed_us_ = start_us +
        (uint64_t)calculateMinCycleMs(tx_ms, afu_callsign_slot) * 1000ULL;
}

} // namespace duty
} // namespace ardf
