// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// tx_driver.h – TX-Hardware-Treiber (Qiachip WL102-341)
// ============================================================================

#ifndef ARDF_TX_DRIVER_H
#define ARDF_TX_DRIVER_H

#include <cstdint>

namespace ardf {
namespace tx {

// TX-Hardware initialisieren (Pin-Modi setzen)
void init();

// TX-Versorgung ein-/ausschalten
void power(bool on);

// Ton einschalten (LEDC + LED an)
void toneOn(uint16_t freq_hz);

// Ton ausschalten (LEDC + LED aus)
void toneOff();

// LED-Steuerung (active LOW, GPIO7 + GPIO8 synchron)
void ledOn();
void ledOff();

// LED-Blinkmuster für Fehlerzustände
void blinkSos();         // SOS: ...---... wiederholt, Pause 1,2 s
void blinkMismatch();    // 3× kurz (150 ms an / 150 ms aus), Pause 1 s
void blinkInvalid();     // 10 Hz Dauerblinken
void blinkDutyBlocked(); // 2× kurz, Pause 2 s
void blinkHeartbeat();   // 2 Hz Herzschlag (Boot/Sync)
void blinkIdle();        // 0,5 Hz langsam (CLI/Idle)

} // namespace tx
} // namespace ardf

#endif // ARDF_TX_DRIVER_H
