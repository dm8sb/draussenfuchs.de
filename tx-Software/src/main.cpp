// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// main.cpp – Hauptprogramm ARDF-Fuchsjagd-Sender ESP32-C3
// ============================================================================

#include <Arduino.h>
#include <esp_arduino_version.h>
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"
#include "morse.h"
#include "tx_driver.h"
#include "duty_guard.h"
#include "sync_espnow.h"
#include "time_sync.h"
#include "cli.h"
#include <esp_sleep.h>

// ---- Globale Konfiguration ----
ConfigV2 g_config;
bool g_start_normal_mode = false;

// ---- RTC-persistente Daten (überleben Deep-Sleep) ----
RTC_DATA_ATTR uint8_t  tx_counter = 0;          // Callsign-Slot-Zähler (nur AFU)
RTC_DATA_ATTR bool     rtc_initialized = false;  // Erster Boot?
RTC_DATA_ATTR uint64_t rtc_cycle_start_us = 0;   // Zyklusstart-Zeitpunkt
RTC_DATA_ATTR uint8_t  rtc_active_count = 1;     // Aktive Füchse
RTC_DATA_ATTR uint8_t  rtc_own_slot_idx = 0;     // Eigener Slot-Index
RTC_DATA_ATTR uint32_t rtc_effective_cycle_ms = 0;
RTC_DATA_ATTR uint32_t rtc_effective_slot_ms = 0;
RTC_DATA_ATTR int16_t  rtc_drift_ppm = 0;
RTC_DATA_ATTR uint64_t rtc_last_sync_us = 0;     // Letzter Sync-Zeitpunkt
RTC_DATA_ATTR uint8_t  rtc_error_state = 0;      // 0 = kein Fehler
RTC_DATA_ATTR uint64_t rtc_error_start_us = 0;   // Fehler-Startzeit

// ---- Fehlerzustände ----
enum ErrorState : uint8_t {
    ERR_NONE           = 0,
    ERR_FOX_COLLISION  = 1,
    ERR_CONFIG_MISMATCH = 2,
    ERR_INVALID_CONFIG = 3,
    ERR_DUTY_BLOCKED   = 4
};

// ---- Vorwärtsdeklarationen ----
static void runStartupPhase();
static void runNormalMode();
static void enterDeepSleep(uint64_t sleep_us);
static void handleError(ErrorState error);
static uint64_t calculateNextSlotUs();

void setup() {
    // Hardware initialisieren
    ardf::tx::init();

    // Seriell starten
    Serial.begin(SERIAL_BAUD);

    // Bei erstem Boot (nicht aus Deep-Sleep) kurz warten
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    bool from_deep_sleep = (wakeup == ESP_SLEEP_WAKEUP_TIMER);

    if (!from_deep_sleep) {
        // Erster Boot oder Reset
        delay(500); // USB-CDC Stabilisierung
        Serial.println();
        Serial.printf(LOG_FOX "ARDF-Fuchsjagd-Sender v%s – Boot\n", ARDF_VERSION);

        // Konfiguration laden
        ardf::config::loadConfig(g_config);

        // Zeitkorrektur initialisieren
        ardf::timesync::init();

        // RTC-Variablen zurücksetzen
        tx_counter = 0;
        rtc_initialized = false;
        rtc_error_state = ERR_NONE;

        // Startphase ausführen (CLI + Sync)
        runStartupPhase();
    } else {
        // Wakeup aus Deep-Sleep → Normalbetrieb
        // Konfiguration laden (schnell, ohne viel Log)
        ardf::config::loadConfig(g_config);
        runNormalMode();
    }
}

void loop() {
    // Wird nie erreicht (Deep-Sleep nach jedem TX)
}

// ============================================================================
// Startphase: CLI + ESP-NOW-Sync
// ============================================================================
static void runStartupPhase() {
    const ardf::FoxProfile& prof = ardf::getProfile(g_config.profile_id);

    // ESP-NOW-Sync starten (außer Profil D)
    if (prof.use_espnow) {
        ardf::sync::initSync(g_config.profile_id, g_config.fox_id, g_config.callsign);
    } else {
        // Profil D: Sync sofort als abgeschlossen markieren
        ardf::sync::initSync(g_config.profile_id, g_config.fox_id, g_config.callsign);
    }

    // CLI initialisieren
    ardf::cli::init();

    // Hauptschleife der Startphase
    while (true) {
        // CLI verarbeiten
        ardf::cli::process();

        // Normalbetrieb angefordert?
        if (g_start_normal_mode) {
            break;
        }

        // CLI-Timeout prüfen
        if (ardf::cli::isTimedOut()) {
            Serial.println();
            Serial.println(LOG_CLI "Inaktivitäts-Timeout – Normalbetrieb wird gestartet");
            g_start_normal_mode = true;
            break;
        }

        // Sync verarbeiten (non-blocking, Profile A–C)
        if (prof.use_espnow && !ardf::sync::isSyncComplete()) {
            ardf::sync::processSync();
        }

        // LED-Herzschlag (Boot/Sync)
        if (ardf::cli::isActive()) {
            // CLI aktiv: langsames Blinken
            static uint64_t last_blink = 0;
            uint64_t now = esp_timer_get_time();
            if (now - last_blink > 2000000ULL) { // 2 s
                ardf::tx::ledOn();
                delay(500);
                ardf::tx::ledOff();
                last_blink = now;
            }
        }

        yield();
        delay(10);
    }

    // Sync beenden
    const ardf::sync::SyncState& ss = ardf::sync::getSyncState();

    // Fehler prüfen
    if (ss.fox_id_collision) {
        handleError(ERR_FOX_COLLISION);
        return;
    }
    if (ss.config_mismatch) {
        handleError(ERR_CONFIG_MISMATCH);
        return;
    }

    // Konfiguration validieren
    if (!ardf::config::isValidConfig(g_config)) {
        handleError(ERR_INVALID_CONFIG);
        return;
    }

    // Sync-Ergebnisse in RTC übernehmen
    rtc_initialized = true;
    rtc_active_count = ss.active_count;
    rtc_own_slot_idx = ss.own_slot_index;
    rtc_effective_cycle_ms = ss.effective_cycle_ms;
    rtc_effective_slot_ms = ss.effective_slot_ms;
    rtc_cycle_start_us = ss.cycle_start_us;

    const ardf::timesync::TimeSyncState& ts = ardf::timesync::getState();
    rtc_drift_ppm = ts.drift_ppm;
    rtc_last_sync_us = ts.last_update_us;

    // Sync-Ressourcen freigeben
    ardf::sync::deinitSync();

    // Duty-Guard prüfen und loggen
    uint32_t tx_on_ms = ardf::calculateTxOnTimeMs(
        g_config.profile_id, g_config.fox_id,
        false, 0);
    ardf::duty::GuardCheck gc = ardf::duty::checkDuty(
        g_config.profile_id, g_config.fox_id,
        rtc_active_count, tx_on_ms, false);
    ardf::duty::logDutyStatus(
        g_config.profile_id, rtc_active_count,
        gc.effective_slot_ms, gc.effective_cycle_ms,
        tx_on_ms, gc.duty_percent, gc.decision_text);

    // Ersten TX starten
    runNormalMode();
}

// ============================================================================
// Normalbetrieb: Senden + Deep-Sleep
// ============================================================================
static void runNormalMode() {
    const ardf::FoxProfile& prof = ardf::getProfile(g_config.profile_id);
    uint16_t freq_hz = FOX_TONE_HZ[g_config.fox_id - 1];

    // ---- Profil D: Einfacher Solo-Modus ----
    if (prof.is_continuous) {
        // TX-Sequenz: Dauerton
        ardf::tx::power(true);
        delay(TX_WARMUP_MS);
        ardf::tx::toneOn(freq_hz);
        delay(prof.tone_on_ms);
        ardf::tx::toneOff();
        ardf::tx::power(false);

        // Deep-Sleep für die Pausenzeit
        enterDeepSleep((uint64_t)prof.tone_off_ms * 1000ULL);
        return;
    }

    // ---- Profile A/B/C: ARDF-Modus ----

    // Duty-Guard prüfen
    bool is_afu = (g_config.callsign[0] != '\0');
    bool send_callsign = false;

    if (is_afu) {
        // AFU-Modus: Callsign alle 10 Slots
        if (tx_counter == CALLSIGN_SLOT_TRIGGER) {
            send_callsign = true;
        }
    }

    // TX-Zeit berechnen
    uint32_t tx_on_ms;
    if (send_callsign) {
        // MO-Kennung + Pause + Callsign
        uint32_t mo_dur = ardf::morse::calculateMoSequenceDurationMs(
            g_config.fox_id, prof.wpm, prof.morse_repeats);
        uint16_t dit_ms = ardf::morse::ditDurationMs(prof.wpm);
        uint32_t cs_dur = ardf::morse::calculateSequenceDurationMs(
            g_config.callsign, prof.wpm);
        tx_on_ms = mo_dur + 7 * dit_ms + cs_dur + TX_WARMUP_MS;
    } else {
        uint32_t mo_dur = ardf::morse::calculateMoSequenceDurationMs(
            g_config.fox_id, prof.wpm, prof.morse_repeats);
        tx_on_ms = mo_dur + TX_WARMUP_MS;
    }

    // Duty-Guard-Prüfung
    ardf::duty::GuardCheck gc = ardf::duty::checkDuty(
        g_config.profile_id, g_config.fox_id,
        rtc_active_count, tx_on_ms, send_callsign);

    if (gc.result == ardf::duty::GuardResult::BLOCKIERT) {
        Serial.println(LOG_ERR "Duty-Guard: Sendung blockiert!");
        ardf::duty::logDutyStatus(
            g_config.profile_id, rtc_active_count,
            gc.effective_slot_ms, gc.effective_cycle_ms,
            tx_on_ms, gc.duty_percent, gc.decision_text);

        // Fehlerbehandlung
        rtc_error_state = ERR_DUTY_BLOCKED;
        rtc_error_start_us = esp_timer_get_time();

        // Nächsten Slot berechnen und schlafen
        uint64_t sleep_us = calculateNextSlotUs();
        enterDeepSleep(sleep_us);
        return;
    }

    if (send_callsign) {
        Serial.printf(LOG_FOX "AFU-Slot: Rufzeichen %s wird gesendet (Slot %u/%u)\n",
                      g_config.callsign, tx_counter + 1, CALLSIGN_SLOT_INTERVAL);
    }

    // ---- TX-Sequenz ----
    ardf::tx::power(true);
    delay(TX_WARMUP_MS);

    // Morse senden
    ardf::morse::sendArdfSlot(
        g_config.fox_id, g_config.profile_id,
        g_config.callsign, send_callsign,
        freq_hz, prof.wpm, prof.morse_repeats);

    ardf::tx::toneOff();
    ardf::tx::power(false);

    // tx_counter aktualisieren (nur im AFU-Modus)
    if (is_afu) {
        tx_counter++;
        if (tx_counter >= CALLSIGN_SLOT_INTERVAL) {
            tx_counter = 0;
        }
    }
    // Im ISM-Modus: tx_counter bleibt 0

    // Fehler-Zustand zurücksetzen
    rtc_error_state = ERR_NONE;

    // Nächsten Slot berechnen und Deep-Sleep
    uint64_t sleep_us = calculateNextSlotUs();
    enterDeepSleep(sleep_us);
}

// ============================================================================
// Nächsten eigenen Slot berechnen
// ============================================================================
static uint64_t calculateNextSlotUs() {
    uint64_t now_us = esp_timer_get_time();

    const ardf::FoxProfile& prof = ardf::getProfile(g_config.profile_id);

    if (prof.is_continuous) {
        // Profil D: feste Pausenzeit
        return (uint64_t)prof.tone_off_ms * 1000ULL;
    }

    // Nächster eigener Slot im Zyklus
    uint64_t cycle_us = (uint64_t)rtc_effective_cycle_ms * 1000ULL;
    uint64_t slot_us  = (uint64_t)rtc_effective_slot_ms * 1000ULL;

    // Vorlaufzeit berechnen
    uint32_t elapsed_s = 0;
    if (rtc_last_sync_us > 0 && now_us > rtc_last_sync_us) {
        elapsed_s = (uint32_t)((now_us - rtc_last_sync_us) / 1000000ULL);
    }
    uint32_t vorlauf_us = ardf::timesync::calculateVorlaufUs(rtc_drift_ppm, elapsed_s);

    // Sleep-Zeit = Zykluszeit - Vorlauf
    // (Wir wachen einen Zyklus später auf, da wir gerade gesendet haben)
    uint64_t sleep_us = cycle_us;
    if (sleep_us > vorlauf_us) {
        sleep_us -= vorlauf_us;
    } else {
        sleep_us = VORLAUF_MIN_US; // Minimum: 100 ms
    }

    // Drift-Kompensation
    if (rtc_drift_ppm != 0) {
        // Korrektur: sleep_us * (1 - drift_ppm / 1e6)
        int64_t correction = (int64_t)sleep_us * rtc_drift_ppm / 1000000LL;
        sleep_us = (uint64_t)((int64_t)sleep_us - correction);
    }

    return sleep_us;
}

// ============================================================================
// Deep-Sleep
// ============================================================================
static void enterDeepSleep(uint64_t sleep_us) {
    // Sicherstellen, dass TX aus ist
    ardf::tx::power(false);
    ardf::tx::toneOff();

    // LEDC aufräumen
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcDetach(DATA_PIN);
#else
    ledcDetachPin(DATA_PIN);
#endif

    // Minimale Sleep-Zeit: 10 ms
    if (sleep_us < 10000ULL) sleep_us = 10000ULL;

    esp_sleep_enable_timer_wakeup(sleep_us);
    esp_deep_sleep_start();
}

// ============================================================================
// Fehlerbehandlung
// ============================================================================
static void handleError(ErrorState error) {
    rtc_error_state = error;
    rtc_error_start_us = esp_timer_get_time();
    uint64_t error_end_us = rtc_error_start_us + (uint64_t)ERROR_RECOVERY_MS * 1000ULL;

    switch (error) {
        case ERR_FOX_COLLISION:
            Serial.println(LOG_ERR "FOX_ID-Kollision – kein Senden möglich!");
            Serial.println(LOG_ERR "Bitte Fox-Nummer über CLI ändern (Menüpunkt 2)");
            while (esp_timer_get_time() < error_end_us) {
                ardf::tx::blinkSos();
                // CLI-Eingabe prüfen (für Neustart)
                if (Serial.available()) {
                    Serial.println(LOG_ERR "Automatische Wiederherstellung – Neustart in 3…2…1");
                    delay(3000);
                    esp_restart();
                }
                yield();
            }
            break;

        case ERR_CONFIG_MISMATCH: {
            const ardf::sync::SyncState& ss = ardf::sync::getSyncState();
            Serial.println(LOG_ERR "Konfigurations-Abweichung im Verbund – kein Senden!");
            Serial.printf(LOG_ERR "Abweichender Fuchs: Fox %u, Profil %c, "
                          "MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                          ss.mismatch_fox_id,
                          ardf::getProfileChar(ss.mismatch_profile_id),
                          ss.mismatch_mac[0], ss.mismatch_mac[1], ss.mismatch_mac[2],
                          ss.mismatch_mac[3], ss.mismatch_mac[4], ss.mismatch_mac[5]);
            while (esp_timer_get_time() < error_end_us) {
                ardf::tx::blinkMismatch();
                if (Serial.available()) {
                    Serial.println(LOG_ERR "Automatische Wiederherstellung – Neustart in 3…2…1");
                    delay(3000);
                    esp_restart();
                }
                yield();
            }
            break;
        }

        case ERR_INVALID_CONFIG:
            Serial.println(LOG_ERR "Ungültige Konfiguration – kein Senden!");
            while (esp_timer_get_time() < error_end_us) {
                ardf::tx::blinkInvalid();
                if (Serial.available()) {
                    Serial.println(LOG_ERR "Automatische Wiederherstellung – Neustart in 3…2…1");
                    delay(3000);
                    esp_restart();
                }
                yield();
            }
            break;

        case ERR_DUTY_BLOCKED:
            Serial.println(LOG_ERR "Duty-Guard hat die Sendung blockiert");
            // Bei Duty-Block kein 60-s-Loop, sondern direkt Sleep zum nächsten Slot
            return;

        default:
            break;
    }

    // Auto-Recovery nach 60 s
    Serial.println(LOG_ERR "Automatische Wiederherstellung – Neustart in 3…2…1");
    delay(3000);
    esp_restart();
}
