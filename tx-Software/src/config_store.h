// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// config_store.h – NVS-Konfigurationsspeicher
// ============================================================================

#ifndef ARDF_CONFIG_STORE_H
#define ARDF_CONFIG_STORE_H

#include "config.h"

namespace ardf {
namespace config {

// CRC32-Berechnung (Software-Implementierung)
uint32_t crc32(const uint8_t* data, size_t length);

// Konfiguration aus NVS laden (gibt true zurück bei Erfolg)
bool loadConfig(ConfigV2& cfg);

// Konfiguration in NVS speichern
bool saveConfig(const ConfigV2& cfg);

// Konfiguration auf Werkseinstellungen zurücksetzen
void resetToDefaults(ConfigV2& cfg);

// Config-Hash berechnen (CRC32 über ConfigHashInput)
uint32_t calculateConfigHash(uint8_t profile_id, uint8_t fox_id,
                              uint16_t slot_ms, uint8_t wpm,
                              bool callsign_empty, uint8_t morse_repeats);

// Konfiguration validieren
bool isValidConfig(const ConfigV2& cfg);

// NVS komplett löschen (Werkseinstellungen)
void eraseAllNvs();

} // namespace config
} // namespace ardf

#endif // ARDF_CONFIG_STORE_H
