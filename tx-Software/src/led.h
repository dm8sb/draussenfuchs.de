// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// led.h – Nicht-blockierende LED-Muster (Status und Fehler)
// ============================================================================

#ifndef ARDF_LED_H
#define ARDF_LED_H

#include <cstdint>

namespace ardf {
namespace led {

enum class Pattern : uint8_t {
    Off,          // aus (im Sendebetrieb steuert die Aussendung die LED)
    Sync,         // 2 Hz Herzschlag: Startphase mit ESP-NOW-Sync
    Idle,         // 0,5 Hz: Startphase ohne Sync (Profil D)
    Sos,          // FOX_ID-Kollision
    Mismatch,     // 3× kurz: Konfigurations-Abweichung / nicht im Slotplan
    Invalid,      // 10 Hz: ungültige Konfiguration / ESP-NOW-Fehler
    DutyBlocked   // 2× kurz: Duty-Guard hat blockiert
};

// Muster setzen (beginnt bei einem Wechsel von vorn)
void set(Pattern p);

// Muster fortschreiben – regelmäßig aufrufen (blockiert nie)
void update();

} // namespace led
} // namespace ardf

#endif // ARDF_LED_H
