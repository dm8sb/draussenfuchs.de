// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// config_store.cpp – NVS-Konfigurationsspeicher Implementierung
// ============================================================================

#include "config_store.h"
#include <Preferences.h>
#include <cstring>

namespace ardf {
namespace config {

bool loadConfig(ConfigV2& cfg) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_CFG, true)) {
        Serial.println(LOG_CFG "NVS-Namespace konnte nicht geöffnet werden");
        resetToDefaults(cfg);
        return false;
    }
    size_t len = prefs.getBytes(NVS_KEY_CFG, &cfg, sizeof(ConfigV2));
    prefs.end();

    if (len != sizeof(ConfigV2) || !isValidConfig(cfg)) {
        Serial.println(LOG_CFG "Konfiguration ungültig oder nicht vorhanden – Werkseinstellungen geladen");
        resetToDefaults(cfg);
        return false;
    }

    Serial.println(LOG_CFG "Konfiguration aus NVS geladen");
    return true;
}

bool saveConfig(const ConfigV2& cfg) {
    // CRC32 vor dem Speichern berechnen
    ConfigV2 saveCfg = cfg;
    saveCfg.crc32 = crc32(reinterpret_cast<const uint8_t*>(&saveCfg),
                           sizeof(ConfigV2) - sizeof(uint32_t));

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_CFG, false)) {
        Serial.println(LOG_ERR "NVS-Namespace konnte nicht zum Schreiben geöffnet werden");
        return false;
    }
    size_t written = prefs.putBytes(NVS_KEY_CFG, &saveCfg, sizeof(ConfigV2));
    prefs.end();

    if (written != sizeof(ConfigV2)) {
        Serial.println(LOG_ERR "Fehler beim Speichern der Konfiguration");
        return false;
    }

    Serial.println(LOG_CFG "Konfiguration im NVS gespeichert");
    return true;
}

void eraseAllNvs() {
    Preferences prefs;
    // Konfiguration löschen
    if (prefs.begin(NVS_NAMESPACE_CFG, false)) {
        prefs.clear();
        prefs.end();
    }
    // Zeitkorrektur löschen
    if (prefs.begin(NVS_NAMESPACE_TIME, false)) {
        prefs.clear();
        prefs.end();
    }
    Serial.println(LOG_CFG "Alle NVS-Daten gelöscht – Werkseinstellungen aktiv");
}

} // namespace config
} // namespace ardf
