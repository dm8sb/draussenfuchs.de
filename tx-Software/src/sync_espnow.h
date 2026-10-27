// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// sync_espnow.h – ESP-NOW-Funk für die Synchronisation (Profile A/B/C)
// Die Verbundlogik selbst steckt in sync_logic.h.
// ============================================================================

#ifndef ARDF_SYNC_ESPNOW_H
#define ARDF_SYNC_ESPNOW_H

#include "config.h"
#include "sync_logic.h"

namespace ardf {
namespace sync {

// ESP-NOW initialisieren und Sync starten (Profil D: nichts zu tun, true)
bool initSync(const ConfigV2& cfg);

// Empfangene Beacons auswerten, Zustand fortschreiben, Beacons senden
// (non-blocking, in der Startphase regelmäßig aufrufen)
void processSync();

// Läuft die Sync-Phase (ESP-NOW aktiv)?
bool isActive();

// Sync-Phase beendet (Sendebeginn steht unmittelbar bevor)?
bool isSyncComplete();

// Menüpunkt 10: Sendebeginn vorziehen (nur Master)
bool requestStart();

// Zeitmessung verwerfen und neu beginnen
void resetTimeModel();

// Sync beenden: letzte Messung im NVS ablegen, WiFi abschalten
void finishSync();

// Verbundzustand (bleibt nach finishSync() erhalten)
const Network& network();

// Eigene MAC-Adresse ermitteln
void getOwnMac(uint8_t* mac);

} // namespace sync
} // namespace ardf

#endif // ARDF_SYNC_ESPNOW_H
