// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// sync_espnow.cpp – ESP-NOW-Synchronisation Implementierung
// ============================================================================

#include "sync_espnow.h"
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"
#include "time_sync.h"
#include "duty_guard.h"
#include <Arduino.h>
#include <esp_arduino_version.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <cstring>
#include <algorithm>

namespace ardf {
namespace sync {

// ---- Interner Zustand ----
static SyncState sync_state;
static uint8_t own_mac[6];
static uint32_t own_boot_id;
static uint8_t own_profile_id;
static uint8_t own_fox_id;
static char own_callsign[CALLSIGN_MAX_LEN + 1];
static bool sync_initialized = false;
static bool sync_complete_flag = false;
static uint64_t sync_start_us = 0;
static uint64_t last_beacon_us = 0;
static uint32_t sync_window_ms = SYNC_WINDOW_NOMINAL_MS;
static bool measuring_active = false;

// Broadcast-MAC für ESP-NOW
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- Vorwärtsdeklarationen ----
static void updateSyncState();
static int compareMac(const uint8_t* mac1, const uint8_t* mac2);
static void sortActiveFoxes();

// ESP-NOW Empfangs-Callback – Signatur variiert je nach Core-Version
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static void onReceive(const esp_now_recv_info_t* recv_info,
                      const uint8_t* data, int data_len);
#else
static void onReceive(const uint8_t* mac_addr,
                      const uint8_t* data, int data_len);
#endif

// ---- MAC-Vergleich (numerisch, 6 Byte) ----
static int compareMac(const uint8_t* mac1, const uint8_t* mac2) {
    return memcmp(mac1, mac2, 6);
}

// ---- ESP-NOW Empfangs-Callback ----
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static void onReceive(const esp_now_recv_info_t* recv_info,
                      const uint8_t* data, int data_len) {
#else
static void onReceive(const uint8_t* mac_addr,
                      const uint8_t* data, int data_len) {
#endif
    if (data_len != sizeof(SyncBeacon)) return;

    SyncBeacon beacon;
    memcpy(&beacon, data, sizeof(SyncBeacon));

    // Protokollversion prüfen
    if (beacon.protocol_ver != SYNC_PROTOCOL_VERSION) return;

    uint64_t rx_local_us = esp_timer_get_time();

    // Config-Hash für Kompatibilitätsprüfung berechnen
    const FoxProfile& prof = getProfile(own_profile_id);
    uint32_t own_hash = config::calculateConfigHash(
        own_profile_id, prof.slot_ms, prof.wpm,
        (own_callsign[0] == '\0'), prof.morse_repeats);

    bool compatible = (beacon.config_hash == own_hash);

    // Prüfe ob wir diesen Peer schon kennen
    int peer_idx = -1;
    for (uint8_t i = 0; i < sync_state.peer_count; i++) {
        if (memcmp(sync_state.peers[i].mac, beacon.mac, 6) == 0) {
            peer_idx = i;
            break;
        }
    }

    if (peer_idx < 0) {
        // Neuer Peer
        if (sync_state.peer_count >= MAX_FOXES - 1) return; // Kein Platz mehr

        peer_idx = sync_state.peer_count;
        sync_state.peer_count++;
        memcpy(sync_state.peers[peer_idx].mac, beacon.mac, 6);
        Serial.printf(LOG_SYNC "Neuer Fuchs entdeckt: Fox %u (MAC %02X:%02X:%02X:%02X:%02X:%02X)\n",
                      beacon.fox_id,
                      beacon.mac[0], beacon.mac[1], beacon.mac[2],
                      beacon.mac[3], beacon.mac[4], beacon.mac[5]);
    }

    // Peer-Daten aktualisieren
    PeerFox& peer = sync_state.peers[peer_idx];
    peer.fox_id       = beacon.fox_id;
    peer.profile_id   = beacon.profile_id;
    peer.config_hash  = beacon.config_hash;
    peer.boot_id      = beacon.boot_id;
    peer.last_seen_us = rx_local_us;
    peer.compatible   = compatible;
    peer.active       = true;

    // FOX_ID-Kollision prüfen
    if (beacon.fox_id == own_fox_id && compatible) {
        sync_state.fox_id_collision = true;
        Serial.printf(LOG_ERR "FOX_ID-Kollision! Fox %u von MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                      beacon.fox_id,
                      beacon.mac[0], beacon.mac[1], beacon.mac[2],
                      beacon.mac[3], beacon.mac[4], beacon.mac[5]);
    }

    // Config-Mismatch erkennen
    if (!compatible) {
        sync_state.config_mismatch  = true;
        sync_state.mismatch_fox_id  = beacon.fox_id;
        sync_state.mismatch_profile_id = beacon.profile_id;
        memcpy(sync_state.mismatch_mac, beacon.mac, 6);
        Serial.printf(LOG_ERR "Konfigurations-Abweichung! Fox %u, Profil %c, "
                      "MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                      beacon.fox_id, getProfileChar(beacon.profile_id),
                      beacon.mac[0], beacon.mac[1], beacon.mac[2],
                      beacon.mac[3], beacon.mac[4], beacon.mac[5]);
    }

    // Zeitmessung für kompatible Peers
    if (compatible && !sync_state.fox_id_collision) {
        timesync::addMeasurement(rx_local_us, beacon.uptime_us);
        measuring_active = true;
    }

    // Sync-Zustand aktualisieren
    updateSyncState();
}

// ---- Sync-Zustand aktualisieren ----
static void updateSyncState() {
    // Aktive und kompatible Füchse zählen
    uint8_t active = 1; // Eigener Fuchs zählt immer
    sync_state.active_fox_ids[0] = own_fox_id;

    for (uint8_t i = 0; i < sync_state.peer_count; i++) {
        if (sync_state.peers[i].active && sync_state.peers[i].compatible) {
            if (active < MAX_FOXES) {
                sync_state.active_fox_ids[active] = sync_state.peers[i].fox_id;
                active++;
            }
        }
    }
    sync_state.active_count = active;

    // Slot-Reihenfolge sortieren
    sortActiveFoxes();

    // Master-Wahl: höchste MAC unter kompatiblen
    sync_state.is_master = true; // Annahme: wir sind Master
    for (uint8_t i = 0; i < sync_state.peer_count; i++) {
        if (sync_state.peers[i].active && sync_state.peers[i].compatible) {
            if (compareMac(sync_state.peers[i].mac, own_mac) > 0) {
                sync_state.is_master = false;
                break;
            }
        }
    }

    // Eigenen Slot-Index bestimmen
    sync_state.own_slot_index = 0;
    for (uint8_t i = 0; i < sync_state.active_count; i++) {
        if (sync_state.active_fox_ids[i] == own_fox_id) {
            sync_state.own_slot_index = i;
            break;
        }
    }

    // Effektive Zykluszeiten berechnen
    const FoxProfile& prof = getProfile(own_profile_id);
    uint32_t tx_on_ms = txBudgetMs(own_profile_id, own_fox_id, nullptr);
    sync_state.effective_cycle_ms = duty::calculateEffectiveCycleMs(
        own_profile_id, active, tx_on_ms);
    sync_state.effective_slot_ms = sync_state.effective_cycle_ms / active;

    // Config-Hash berechnen
    sync_state.config_hash = config::calculateConfigHash(
        own_profile_id, prof.slot_ms, prof.wpm,
        (own_callsign[0] == '\0'), prof.morse_repeats);
}

// ---- Fox-IDs aufsteigend sortieren, bei Gleichheit MAC als Tie-Break ----
static void sortActiveFoxes() {
    // Einfacher Bubble-Sort (max. 5 Elemente)
    for (uint8_t i = 0; i < sync_state.active_count - 1; i++) {
        for (uint8_t j = i + 1; j < sync_state.active_count; j++) {
            bool swap = false;
            if (sync_state.active_fox_ids[j] < sync_state.active_fox_ids[i]) {
                swap = true;
            } else if (sync_state.active_fox_ids[j] == sync_state.active_fox_ids[i]) {
                // Tie-Break: MAC-Adresse (eigene MAC vs. Peer-MAC)
                const uint8_t* mac_j = nullptr;
                const uint8_t* mac_i = nullptr;

                if (sync_state.active_fox_ids[j] == own_fox_id) {
                    mac_j = own_mac;
                } else {
                    for (uint8_t p = 0; p < sync_state.peer_count; p++) {
                        if (sync_state.peers[p].fox_id == sync_state.active_fox_ids[j]) {
                            mac_j = sync_state.peers[p].mac;
                            break;
                        }
                    }
                }
                if (sync_state.active_fox_ids[i] == own_fox_id) {
                    mac_i = own_mac;
                } else {
                    for (uint8_t p = 0; p < sync_state.peer_count; p++) {
                        if (sync_state.peers[p].fox_id == sync_state.active_fox_ids[i]) {
                            mac_i = sync_state.peers[p].mac;
                            break;
                        }
                    }
                }

                if (mac_j && mac_i && compareMac(mac_j, mac_i) < 0) {
                    swap = true;
                }
            }

            if (swap) {
                uint8_t tmp = sync_state.active_fox_ids[i];
                sync_state.active_fox_ids[i] = sync_state.active_fox_ids[j];
                sync_state.active_fox_ids[j] = tmp;
            }
        }
    }
}

bool initSync(uint8_t profile_id, uint8_t fox_id, const char* callsign) {
    const FoxProfile& prof = getProfile(profile_id);

    // Profil D: kein ESP-NOW
    if (!prof.use_espnow) {
        memset(&sync_state, 0, sizeof(SyncState));
        sync_state.active_count = 1;
        sync_state.active_fox_ids[0] = fox_id;
        sync_state.own_slot_index = 0;
        sync_state.is_master = true;
        sync_state.effective_cycle_ms = prof.cycle_ms;
        sync_state.effective_slot_ms = prof.slot_ms;
        sync_complete_flag = true;
        return true;
    }

    own_profile_id = profile_id;
    own_fox_id = fox_id;
    strncpy(own_callsign, callsign ? callsign : "", CALLSIGN_MAX_LEN);
    own_callsign[CALLSIGN_MAX_LEN] = '\0';

    // Boot-ID generieren
    own_boot_id = esp_random();

    // Sync-Zustand initialisieren
    memset(&sync_state, 0, sizeof(SyncState));
    sync_state.active_count = 1;
    sync_state.active_fox_ids[0] = fox_id;
    sync_state.is_master = true;
    sync_state.own_slot_index = 0;

    // WiFi im STA-Modus initialisieren
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Eigene MAC ermitteln
    esp_wifi_get_mac(WIFI_IF_STA, own_mac);
    memcpy(sync_state.mismatch_mac, own_mac, 6); // Initialisierung

    Serial.printf(LOG_SYNC "Eigene MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  own_mac[0], own_mac[1], own_mac[2],
                  own_mac[3], own_mac[4], own_mac[5]);
    Serial.printf(LOG_SYNC "Boot-ID: 0x%08lX\n", (unsigned long)own_boot_id);

    // WiFi-Kanal setzen
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // ESP-NOW initialisieren
    if (esp_now_init() != ESP_OK) {
        Serial.println(LOG_ERR "ESP-NOW-Initialisierung fehlgeschlagen");
        return false;
    }

    // Broadcast-Peer hinzufügen
    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, broadcast_mac, 6);
    peer_info.channel = ESPNOW_CHANNEL;
    peer_info.encrypt = false;

    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        Serial.println(LOG_ERR "Broadcast-Peer konnte nicht hinzugefügt werden");
        return false;
    }

    // Empfangs-Callback registrieren
    esp_now_register_recv_cb(onReceive);

    sync_start_us = esp_timer_get_time();
    sync_window_ms = SYNC_WINDOW_NOMINAL_MS;
    sync_initialized = true;
    sync_complete_flag = false;

    // Effektive Zykluszeiten initial berechnen
    updateSyncState();

    Serial.printf(LOG_SYNC "ESP-NOW-Sync gestartet (Kanal %u, Fenster %lu ms)\n",
                  ESPNOW_CHANNEL, (unsigned long)sync_window_ms);
    return true;
}

void processSync() {
    if (!sync_initialized || sync_complete_flag) return;

    uint64_t now_us = esp_timer_get_time();
    uint64_t elapsed_us = now_us - sync_start_us;

    // Sync-Fenster-Verwaltung
    if (elapsed_us > (uint64_t)sync_window_ms * 1000ULL) {
        // Verlängerung prüfen (wenn Messung aktiv und noch nicht max)
        if (measuring_active && sync_window_ms < SYNC_WINDOW_MAX_MS) {
            sync_window_ms = SYNC_WINDOW_MAX_MS;
            Serial.printf(LOG_SYNC "Sync-Fenster verlängert auf %lu ms\n",
                          (unsigned long)sync_window_ms);
        } else {
            // Sync abschließen
            timesync::computeRegression();
            timesync::saveToNvs();

            // Cycle-Start setzen (Master)
            if (sync_state.is_master) {
                sync_state.cycle_start_us = now_us;
                Serial.println(LOG_SYNC "Master: Zyklusstart gesetzt");
            }

            sync_complete_flag = true;
            Serial.println(LOG_SYNC "Sync-Phase abgeschlossen");
            return;
        }
    }

    // Periodisch Beacons senden
    uint64_t beacon_elapsed = now_us - last_beacon_us;
    uint32_t jitter = esp_random() % (BEACON_JITTER_MS * 2 * 1000);
    uint32_t next_beacon_us = (BEACON_INTERVAL_MS * 1000) - (BEACON_JITTER_MS * 1000) + jitter;

    if (beacon_elapsed >= next_beacon_us) {
        sendBeacon();
        last_beacon_us = now_us;
    }

    // Periodisch Regression berechnen (alle 10 Sekunden)
    static uint64_t last_regression_us = 0;
    if (now_us - last_regression_us > 10000000ULL) { // 10 s
        timesync::computeRegression();
        last_regression_us = now_us;
    }
}

void sendBeacon() {
    if (!sync_initialized) return;

    const FoxProfile& prof = getProfile(own_profile_id);
    const timesync::TimeSyncState& ts = timesync::getState();

    SyncBeacon beacon;
    memset(&beacon, 0, sizeof(beacon));

    beacon.protocol_ver = SYNC_PROTOCOL_VERSION;
    beacon.profile_id   = own_profile_id;
    beacon.fox_id       = own_fox_id;

    // Flags setzen
    beacon.flags = 0;
    if (own_callsign[0] != '\0') beacon.flags |= FLAG_AFU_MODE;
    beacon.flags |= FLAG_DUTY_GUARD; // Duty-Guard immer aktiv
    if (sync_state.is_master) beacon.flags |= FLAG_MASTER;

    memcpy(beacon.mac, own_mac, 6);
    beacon.boot_id       = own_boot_id;
    beacon.uptime_us     = esp_timer_get_time();
    beacon.cycle_start_us = sync_state.cycle_start_us;
    beacon.config_hash   = sync_state.config_hash;
    beacon.drift_ppm_est = ts.drift_ppm;
    beacon.sync_quality  = ts.quality;
    beacon.active_count  = sync_state.active_count;
    memcpy(beacon.active_fox_ids, sync_state.active_fox_ids,
           sizeof(beacon.active_fox_ids));
    beacon.slot_ms       = (uint16_t)sync_state.effective_slot_ms;
    beacon.cycle_ms      = (uint16_t)sync_state.effective_cycle_ms;

    esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&beacon),
                 sizeof(SyncBeacon));
}

void deinitSync() {
    if (!sync_initialized) return;

    esp_now_unregister_recv_cb();
    esp_now_deinit();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    sync_initialized = false;
    Serial.println(LOG_SYNC "WiFi und ESP-NOW deinitialisiert");
}

const SyncState& getSyncState() {
    return sync_state;
}

bool isSyncComplete() {
    return sync_complete_flag;
}

void extendSyncWindow() {
    if (sync_window_ms < SYNC_WINDOW_MAX_MS) {
        sync_window_ms = SYNC_WINDOW_MAX_MS;
        Serial.printf(LOG_SYNC "Sync-Fenster manuell verlängert auf %lu ms\n",
                      (unsigned long)sync_window_ms);
    }
}

void getOwnMac(uint8_t* mac) {
    memcpy(mac, own_mac, 6);
}

} // namespace sync
} // namespace ardf
