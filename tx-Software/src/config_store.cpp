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

bool loadConfig(FoxConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_CFG, true)) {
        Serial.println(LOG_CFG "NVS-Namespace konnte nicht geöffnet werden");
        resetToDefaults(cfg);
        return false;
    }
    size_t len = prefs.getBytes(NVS_KEY_CFG, &cfg, sizeof(FoxConfig));
    if (len == sizeof(FoxConfig) && isValidConfig(cfg)) {
        prefs.end();
        Serial.println(LOG_CFG "Konfiguration aus NVS geladen");
        return true;
    }

    // Übernahme aus v2.0.x: Deren CRC-Tabelle war fehlerhaft, deshalb nur
    // Version und Felder prüfen und mit korrekter CRC neu speichern
    FoxConfig legacy;
    len = prefs.getBytes(NVS_KEY_CFG_LEGACY, &legacy, sizeof(FoxConfig));
    prefs.end();
    if (len == sizeof(FoxConfig) && legacy.version == CONFIG_VERSION_LEGACY &&
        isValidFields(legacy)) {
        cfg = legacy;
        cfg.version = CONFIG_VERSION;
        updateCrc(cfg);
        if (saveConfig(cfg) && prefs.begin(NVS_NAMESPACE_CFG, false)) {
            prefs.remove(NVS_KEY_CFG_LEGACY);
            prefs.end();
        }
        Serial.println(LOG_CFG "Konfiguration aus Firmware v2.0 übernommen");
        return true;
    }

    Serial.println(LOG_CFG "Konfiguration ungültig oder nicht vorhanden – Werkseinstellungen geladen");
    resetToDefaults(cfg);
    return false;
}

bool saveConfig(const FoxConfig& cfg) {
    // CRC32 vor dem Speichern berechnen
    FoxConfig saveCfg = cfg;
    updateCrc(saveCfg);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_CFG, false)) {
        Serial.println(LOG_ERR "NVS-Namespace konnte nicht zum Schreiben geöffnet werden");
        return false;
    }
    size_t written = prefs.putBytes(NVS_KEY_CFG, &saveCfg, sizeof(FoxConfig));
    prefs.end();

    if (written != sizeof(FoxConfig)) {
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
