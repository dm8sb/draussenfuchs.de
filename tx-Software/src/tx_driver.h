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

} // namespace tx
} // namespace ardf

#endif // ARDF_TX_DRIVER_H
