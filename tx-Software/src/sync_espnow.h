// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// sync_espnow.h – ESP-NOW-Synchronisation für ARDF-Profile A/B/C
// ============================================================================

#ifndef ARDF_SYNC_ESPNOW_H
#define ARDF_SYNC_ESPNOW_H

#include "config.h"

namespace ardf {
namespace sync {

// Erkannter Nachbar-Fuchs
struct PeerFox {
    uint8_t  mac[6];
    uint8_t  fox_id;
    uint8_t  profile_id;
    uint32_t config_hash;
    uint32_t boot_id;
    uint64_t last_seen_us;
    int64_t  clock_offset_us;
    bool     compatible;
    bool     active;
};

// Sync-Zustandsinformationen
struct SyncState {
    bool     is_master;
    uint8_t  active_count;
    uint8_t  active_fox_ids[MAX_FOXES];
    uint8_t  own_slot_index;
    uint32_t effective_slot_ms;
    uint32_t effective_cycle_ms;
    uint64_t cycle_start_us;
    uint32_t config_hash;
    bool     fox_id_collision;
    bool     config_mismatch;
    uint8_t  mismatch_fox_id;
    uint8_t  mismatch_profile_id;
    uint8_t  mismatch_mac[6];
    uint8_t  peer_count;
    PeerFox  peers[MAX_FOXES - 1];
};

// ESP-NOW initialisieren und Sync starten
bool initSync(uint8_t profile_id, uint8_t fox_id, const char* callsign);

// Sync-Schleife aufrufen (non-blocking, im Startfenster periodisch aufrufen)
void processSync();

// Beacon senden
void sendBeacon();

// Sync beenden und WiFi deinitialisieren
void deinitSync();

// Aktuellen Sync-Zustand abrufen
const SyncState& getSyncState();

// Ist die Sync-Phase abgeschlossen?
bool isSyncComplete();

// Sync-Fenster manuell verlängern
void extendSyncWindow();

// Eigene MAC-Adresse ermitteln
void getOwnMac(uint8_t* mac);

} // namespace sync
} // namespace ardf

#endif // ARDF_SYNC_ESPNOW_H
