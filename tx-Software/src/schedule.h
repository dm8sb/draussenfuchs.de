// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// schedule.h – Slotplan des Verbunds und Sendezeitpunkte
// (hardwareunabhängig, nativ testbar)
// ============================================================================

#ifndef ARDF_SCHEDULE_H
#define ARDF_SCHEDULE_H

#include <cstdint>
#include "config.h"

namespace ardf {
namespace schedule {

// Sendezeiten eines Fuchses (aus dessen Beacon bzw. eigener Konfiguration)
struct FoxTiming {
    uint8_t  fox_id;
    uint32_t tx_norm_ms;   // normaler Slot
    uint32_t tx_max_ms;    // längste Aussendung (Rufzeichen-Slot, sonst = tx_norm_ms)
};

// Verbundweiter Slotplan – für alle Füchse identisch
struct CyclePlan {
    uint32_t cycle_ms;
    uint32_t slot_ms;
    uint8_t  count;                 // aktive Füchse
    uint8_t  fox_ids[MAX_FOXES];    // Slotreihenfolge (aufsteigende Fox-ID)
    uint32_t tx_norm_ms;            // längste normale Aussendung (nur beim Master gefüllt)
    uint32_t tx_max_ms;             // längste Aussendung (nur beim Master gefüllt)
    bool     extended;              // Zyklus über den Profilwert hinaus verlängert
};

// Slotplan berechnen. Zyklus = Maximum aus
//  - Profil-Slot (bzw. längste Aussendung + Schutzabstand) × Anzahl Füchse
//  - ISM-Mindestzyklus der längsten normalen Aussendung (9,5 %)
//  - Mindestzyklus der längsten Aussendung im AFU-Rufzeichen-Slot (50 %)
// Doppelte Fox-IDs werden zusammengefasst. Profil D: fester Solo-Zyklus.
CyclePlan computePlan(uint8_t profile_id, const FoxTiming* foxes, uint8_t n);

// Slot-Index einer Fox-ID im Plan, −1 wenn nicht enthalten
int8_t slotIndex(const CyclePlan& plan, uint8_t fox_id);

// Beginn des Slots slot_idx im Zyklus k (Referenzuhr, µs)
uint64_t slotStartUs(uint64_t plan_start_us, const CyclePlan& plan,
                     uint8_t slot_idx, uint32_t k);

} // namespace schedule
} // namespace ardf

#endif // ARDF_SCHEDULE_H
