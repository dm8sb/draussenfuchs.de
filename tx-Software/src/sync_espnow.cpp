// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// sync_espnow.cpp – ESP-NOW-Funk für die Synchronisation
// ============================================================================

#include "sync_espnow.h"
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"
#include "time_correction_store.h"
#include <Arduino.h>
#include <esp_arduino_version.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include <cmath>

namespace ardf {
namespace sync {

// Empfangener Beacon mit Zeitstempel, vom WiFi-Task an die Hauptschleife
struct RxItem {
    uint8_t    mac[6];
    SyncBeacon beacon;
    uint64_t   rx_us;
};

static constexpr UBaseType_t RX_QUEUE_LEN = 16;

static Network net;
static QueueHandle_t rx_queue = nullptr;
static uint8_t own_mac[6];
static bool sync_initialized = false;
static uint64_t next_beacon_us = 0;

// Broadcast-MAC für ESP-NOW
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void printMac(const uint8_t* m) {
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

// ---- ESP-NOW Empfangs-Callback (WiFi-Task) ----
// Nur Zeitstempel nehmen und in die Queue legen; ausgewertet wird in processSync().
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static void onReceive(const esp_now_recv_info_t* recv_info,
                      const uint8_t* data, int data_len) {
    const uint8_t* mac_addr = recv_info->src_addr;
#else
static void onReceive(const uint8_t* mac_addr,
                      const uint8_t* data, int data_len) {
#endif
    uint64_t rx_us = esp_timer_get_time();
    if (data_len != sizeof(SyncBeacon) || rx_queue == nullptr) return;

    RxItem item;
    memcpy(item.mac, mac_addr, 6);
    memcpy(&item.beacon, data, sizeof(SyncBeacon));
    item.rx_us = rx_us;
    xQueueSend(rx_queue, &item, 0);   // Queue voll: Beacon verwerfen
}

bool initSync(const ConfigV2& cfg) {
    const FoxProfile& prof = getProfile(cfg.profile_id);
    if (!prof.use_espnow) return true;   // Profil D: kein ESP-NOW

    bool afu = (cfg.callsign[0] != '\0');
    OwnInfo own;
    own.fox_id      = cfg.fox_id;
    own.profile_id  = cfg.profile_id;
    own.config_hash = config::calculateConfigHash(
        cfg.profile_id, prof.slot_ms, prof.wpm, !afu, prof.morse_repeats);
    own.afu         = afu;
    own.tx_norm_ms  = txBudgetMs(cfg.profile_id, cfg.fox_id, nullptr);
    own.tx_max_ms   = afu ? txBudgetMs(cfg.profile_id, cfg.fox_id, cfg.callsign)
                          : own.tx_norm_ms;
    own.boot_id     = esp_random();

    // WiFi im STA-Modus initialisieren
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_get_mac(WIFI_IF_STA, own_mac);
    memcpy(own.mac, own_mac, 6);

    Serial.print(LOG_SYNC "Eigene MAC: ");
    printMac(own_mac);
    Serial.printf(" | Boot-ID: 0x%08lX\n", (unsigned long)own.boot_id);

    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println(LOG_ERR "ESP-NOW-Initialisierung fehlgeschlagen");
        return false;
    }

    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, broadcast_mac, 6);
    peer_info.channel = ESPNOW_CHANNEL;
    peer_info.encrypt = false;
    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        Serial.println(LOG_ERR "Broadcast-Peer konnte nicht hinzugefügt werden");
        esp_now_deinit();
        return false;
    }

    rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
    if (rx_queue == nullptr) {
        Serial.println(LOG_ERR "Empfangs-Queue konnte nicht angelegt werden");
        esp_now_deinit();
        return false;
    }
    esp_now_register_recv_cb(onReceive);

    uint64_t now = esp_timer_get_time();
    net.begin(own, now);
    next_beacon_us = now;
    sync_initialized = true;

    Serial.printf(LOG_SYNC "ESP-NOW-Sync gestartet (Kanal %u, Sendebeginn spätestens %lu s nach dem Master-Start)\n",
                  ESPNOW_CHANNEL, (unsigned long)(SYNC_WINDOW_MS / 1000));
    return true;
}

static void logEvents(uint8_t ev, uint64_t now) {
    if (ev & EV_MASTER_CHANGED) {
        if (net.isMaster()) {
            Serial.println(LOG_SYNC "Master: dieser Fuchs");
        } else if (net.master() >= 0) {
            const PeerFox& m = net.peer((uint8_t)net.master());
            Serial.printf(LOG_SYNC "Master: Fox %u (", m.fox_id);
            printMac(m.mac);
            Serial.println(")");
        }
    }
    if (ev & EV_PLAN_CHANGED) {
        const schedule::CyclePlan& p = net.plan();
        Serial.printf(LOG_SYNC "Slotplan: %u Füchse | Zyklus %lu ms | Slot %lu ms | Reihenfolge:",
                      p.count, (unsigned long)p.cycle_ms, (unsigned long)p.slot_ms);
        for (uint8_t i = 0; i < p.count; i++) Serial.printf(" %u", p.fox_ids[i]);
        Serial.println();
    }
    if (ev & EV_START_MOVED) {
        Serial.println(LOG_SYNC "Sendebeginn vorgezogen");
    }
    if (ev & EV_FROZEN) {
        uint64_t start = net.planStartLocalUs();
        Serial.printf(LOG_SYNC "Slotplan fest – Sendebeginn in %.1f s\n",
                      start > now ? (double)(start - now) / 1e6 : 0.0);
    }
    if (ev & EV_PEER_LOST) {
        Serial.println(LOG_WARN "Ein Fuchs wird nicht mehr empfangen");
    }
    if (ev & EV_COLLISION) {
        Serial.printf(LOG_ERR "FOX_ID-Kollision! Ein anderer Fuchs sendet ebenfalls als Fox %u\n",
                      net.own().fox_id);
    }
    if (ev & EV_MISMATCH) {
        Serial.printf(LOG_ERR "Konfigurations-Abweichung! Fox %u, Profil %c, MAC ",
                      net.mismatchFoxId(), getProfileChar(net.mismatchProfileId()));
        printMac(net.mismatchMac());
        Serial.println();
    }
}

void processSync() {
    if (!sync_initialized) return;

    // Empfangene Beacons auswerten
    RxItem item;
    while (xQueueReceive(rx_queue, &item, 0) == pdTRUE) {
        int8_t idx = net.onBeacon(item.mac, item.beacon, item.rx_us);
        if (idx >= 0) {
            const PeerFox& p = net.peer((uint8_t)idx);
            Serial.printf(LOG_SYNC "Fuchs entdeckt: Fox %u, Profil %c (", p.fox_id,
                          getProfileChar(p.profile_id));
            printMac(p.mac);
            Serial.println(p.compatible ? ")" : ") – inkompatibel");
        }
    }

    // Erst nach dem Leeren der Queue: kein Empfangszeitstempel liegt nach now
    uint64_t now = esp_timer_get_time();
    net.tick(now);
    logEvents(net.takeEvents(), now);

    // Beacon senden, nächsten Zeitpunkt einmal auswürfeln (500 ms ± 50 ms)
    if (now >= next_beacon_us) {
        SyncBeacon b;
        net.fillBeacon(b, esp_timer_get_time());
        esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&b), sizeof(SyncBeacon));
        uint32_t jitter_us = esp_random() % (2U * BEACON_JITTER_MS * 1000U + 1U);
        next_beacon_us = now + (uint64_t)(BEACON_INTERVAL_MS - BEACON_JITTER_MS) * 1000ULL + jitter_us;
    }
}

bool isActive() {
    return sync_initialized;
}

bool isSyncComplete() {
    return sync_initialized && net.finished(esp_timer_get_time());
}

bool requestStart() {
    if (!sync_initialized) return false;
    return net.requestStart(esp_timer_get_time());
}

void resetTimeModel() {
    net.resetTimeModel();
}

void finishSync() {
    if (!sync_initialized) return;

    // Letzte Messung für die Anzeige im NVS ablegen (nur Slaves mit Drift-Schätzung)
    const timesync::TimeModel& m = net.timeModel();
    if (!net.isMaster() && m.hasDrift()) {
        TimeCorrectionData data;
        data.drift_ppm  = (int16_t)lroundf(m.driftPpm());
        data.offset_us  = m.offsetUs();
        data.quality    = m.quality();
        data.meas_count = m.samples();
        timecorr::save(data);
    }

    esp_now_unregister_recv_cb();
    esp_now_deinit();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    if (rx_queue != nullptr) {
        vQueueDelete(rx_queue);
        rx_queue = nullptr;
    }

    sync_initialized = false;
    Serial.println(LOG_SYNC "Sync-Phase beendet – WiFi und ESP-NOW abgeschaltet");
}

const Network& network() {
    return net;
}

void getOwnMac(uint8_t* mac) {
    memcpy(mac, own_mac, 6);
}

} // namespace sync
} // namespace ardf
