// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// duty_guard.h – Duty-Cycle-Überwachung und -Schutz
// ============================================================================

#ifndef ARDF_DUTY_GUARD_H
#define ARDF_DUTY_GUARD_H

#include <cstdint>

namespace ardf {
namespace duty {

// Ergebnis der Duty-Guard-Prüfung
enum class GuardResult : uint8_t {
    OK = 0,           // Senden erlaubt
    VERLAENGERT,      // Zyklus wurde verlängert, Senden erlaubt
    BLOCKIERT         // Senden blockiert
};

// Duty-Guard-Ergebnis mit Details
struct GuardCheck {
    GuardResult result;
    uint32_t    effective_cycle_ms;
    uint32_t    effective_slot_ms;
    uint32_t    tx_on_ms;          // Geschätzte TX-ON-Zeit
    float       duty_percent;
    const char* decision_text;     // "OK", "VERLÄNGERT" oder "BLOCKIERT"
};

// Prüfe ob Senden im aktuellen Slot erlaubt ist
// afu_callsign_slot: true wenn dies der 10. Slot im AFU-Modus ist
GuardCheck checkDuty(uint8_t profile_id, uint8_t fox_id,
                     uint8_t active_count, uint32_t tx_on_ms,
                     bool afu_callsign_slot);

// Berechne den minimalen Zyklus in ms für gegebene TX-Zeit
// Formel: ceil(tx_on_ms * 1.05 / 0.095)
uint32_t calculateMinCycleMs(uint32_t tx_on_ms);

// Berechne den effektiven Zyklus basierend auf aktiven Füchsen
uint32_t calculateEffectiveCycleMs(uint8_t profile_id, uint8_t active_count,
                                    uint32_t tx_on_ms);

// Berechne den Duty-Cycle in Prozent
float calculateDutyPercent(uint32_t tx_on_ms, uint32_t cycle_ms);

// Log-Ausgabe im vorgegebenen Format
void logDutyStatus(uint8_t profile_id, uint8_t active_count,
                   uint32_t slot_ms, uint32_t cycle_ms,
                   uint32_t tx_on_ms, float duty_pct,
                   const char* decision);

} // namespace duty
} // namespace ardf

#endif // ARDF_DUTY_GUARD_H
