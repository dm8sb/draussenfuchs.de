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

// CRC-32 (IEEE 802.3, wie zlib)
uint32_t crc32(const uint8_t* data, size_t length);

// CRC-Feld einer Konfiguration neu berechnen
void updateCrc(FoxConfig& cfg);

// Konfiguration aus NVS laden (gibt true zurück bei Erfolg)
bool loadConfig(FoxConfig& cfg);

// Konfiguration in NVS speichern
bool saveConfig(const FoxConfig& cfg);

// Konfiguration auf Werkseinstellungen zurücksetzen
void resetToDefaults(FoxConfig& cfg);

// Config-Hash berechnen (CRC32 über ConfigHashInput, verbundweite Parameter)
uint32_t calculateConfigHash(uint8_t profile_id,
                              uint16_t slot_ms, uint8_t wpm,
                              bool callsign_empty, uint8_t morse_repeats);

// Konfiguration validieren (Version, CRC und Felder)
bool isValidConfig(const FoxConfig& cfg);

// Nur die Felder prüfen (Fox-ID, Profil, Rufzeichen) – für die Übernahme von v2
bool isValidFields(const FoxConfig& cfg);

// NVS komplett löschen (Werkseinstellungen)
void eraseAllNvs();

} // namespace config
} // namespace ardf

#endif // ARDF_CONFIG_STORE_H
