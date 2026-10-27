// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// time_correction_store.cpp – NVS-Speicher für Zeitkorrekturwerte
// ============================================================================

#include "time_correction_store.h"
#include "config_store.h"
#include <Preferences.h>

namespace ardf {
namespace timecorr {

bool load(TimeCorrectionData& data) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_TIME, true)) {
        return false;
    }

    size_t len = prefs.getBytes(NVS_KEY_TIME, &data, sizeof(TimeCorrectionData));
    prefs.end();

    if (len != sizeof(TimeCorrectionData)) {
        return false;
    }

    // CRC32 prüfen
    uint32_t expected = config::crc32(
        reinterpret_cast<const uint8_t*>(&data),
        sizeof(TimeCorrectionData) - sizeof(uint32_t));

    if (data.crc32 != expected) {
        Serial.println(LOG_ERR "Zeitkorrektur-CRC ungültig");
        return false;
    }

    return true;
}

bool save(const TimeCorrectionData& data) {
    TimeCorrectionData saveData = data;
    saveData.crc32 = config::crc32(
        reinterpret_cast<const uint8_t*>(&saveData),
        sizeof(TimeCorrectionData) - sizeof(uint32_t));

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_TIME, false)) {
        Serial.println(LOG_ERR "Zeitkorrektur-NVS konnte nicht geöffnet werden");
        return false;
    }

    size_t written = prefs.putBytes(NVS_KEY_TIME, &saveData, sizeof(TimeCorrectionData));
    prefs.end();

    if (written != sizeof(TimeCorrectionData)) {
        Serial.println(LOG_ERR "Fehler beim Speichern der Zeitkorrektur");
        return false;
    }

    Serial.println(LOG_CFG "Zeitkorrekturwerte im NVS gespeichert");
    return true;
}

void clear() {
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE_TIME, false)) {
        prefs.clear();
        prefs.end();
    }
    Serial.println(LOG_CFG "Zeitkorrekturwerte gelöscht");
}

} // namespace timecorr
} // namespace ardf
