// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// duty_guard.h – Duty-Cycle-Überwachung und -Schutz
// (hardwareunabhängig, nativ testbar)
// ============================================================================

#ifndef ARDF_DUTY_GUARD_H
#define ARDF_DUTY_GUARD_H

#include <cstdint>

namespace ardf {
namespace duty {

// Duty-Grenze: 9,5 % (ISM), 50 % im AFU-Rufzeichen-Slot
float limitFor(bool afu_callsign_slot);

// Minimaler Abstand zwischen zwei Sendebeginnen für eine Aussendung von tx_ms
// Formel: ceil(tx_ms × 1,05 / Grenze)
uint32_t calculateMinCycleMs(uint32_t tx_ms, bool afu_callsign_slot = false);

// Duty-Cycle in Prozent (inkl. 5 % Sicherheitsaufschlag)
float calculateDutyPercent(uint32_t tx_ms, uint32_t cycle_ms);

// Laufzeit-Guard: gibt eine Aussendung nur frei, wenn seit dem Beginn der
// vorherigen Aussendung deren Mindestabstand vergangen ist. Letzte Instanz
// unabhängig von der Zeitplanung.
class Guard {
public:
    bool allows(uint64_t start_us) const;
    void record(uint64_t start_us, uint32_t tx_ms, bool afu_callsign_slot);
    uint64_t nextAllowedUs() const { return next_allowed_us_; }

private:
    bool     has_last_ = false;
    uint64_t next_allowed_us_ = 0;
};

} // namespace duty
} // namespace ardf

#endif // ARDF_DUTY_GUARD_H
