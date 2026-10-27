// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// main.cpp – Hauptprogramm ARDF-Fuchsjagd-Sender ESP32-C3
// ============================================================================
//
// Profile A/B/C (ARDF-Verbund): Startphase mit CLI und ESP-NOW-Sync bis zum
// vereinbarten Sendebeginn. Danach bleibt der Fuchs wach (WiFi aus, 80 MHz),
// damit alle Sendezeitpunkte über den Quarz laufen – der Deep-Sleep-Timer
// (interner RC-Oszillator) ist für ein gemeinsames Slotraster zu ungenau.
//
// Profil D (Solo): Aussendung, dann Deep-Sleep bis zur nächsten Aussendung.

#include <Arduino.h>
#include <esp_arduino_version.h>
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"
#include "morse.h"
#include "tx_driver.h"
#include "duty_guard.h"
#include "schedule.h"
#include "sync_espnow.h"
#include "cli.h"
#include "led.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/gpio.h>

// ---- Konfiguration ----
static FoxConfig g_config;

// ---- Fehler-Neuversuche: überleben esp_restart(), nicht aber Power-on/RST ----
static constexpr uint32_t RETRY_MAGIC = 0xA5D1F0C5;
RTC_NOINIT_ATTR static uint32_t retry_magic;
RTC_NOINIT_ATTR static uint8_t  retry_count;

static void resetRetries() {
    retry_magic = RETRY_MAGIC;
    retry_count = 0;
}

// ---- Fehlerzustände ----
enum ErrorState : uint8_t {
    ERR_NONE            = 0,
    ERR_FOX_COLLISION   = 1,
    ERR_CONFIG_MISMATCH = 2,
    ERR_INVALID_CONFIG  = 3,
    ERR_NOT_IN_PLAN     = 4,
    ERR_SYNC_INIT       = 5
};

// ---- Vorwärtsdeklarationen ----
static void runStartupPhase();
static void runArdfMode();
static void runSoloMode();
static void enterDeepSleep(uint64_t sleep_us);
static void handleError(ErrorState error);

// ---- Duty-Log im vorgegebenen Format ----
static void logDutyStatus(uint8_t profile_id, uint8_t active_count,
                          uint32_t slot_ms, uint32_t cycle_ms,
                          uint32_t tx_on_ms, float duty_pct,
                          const char* decision) {
    Serial.printf(LOG_DUTY "Profil %c | aktiv:%u | Slot:%lu ms | Zyklus:%lu ms | "
                  "TX:%lu ms | Duty:%.1f %% | Entscheidung:%s\n",
                  ardf::getProfileChar(profile_id), active_count,
                  (unsigned long)slot_ms, (unsigned long)cycle_ms,
                  (unsigned long)tx_on_ms, duty_pct, decision);
}

void setup() {
    // Hardware initialisieren
    ardf::tx::init();

    // Seriell starten
    Serial.begin(SERIAL_BAUD);

    // Aufwachen aus dem Deep-Sleep gibt es nur in Profil D
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        ardf::config::loadConfig(g_config);
        if (ardf::getProfile(g_config.profile_id).is_continuous) {
            runSoloMode();
            return;
        }
        // Profil inzwischen geändert: regulär starten
    }

    // Erster Boot oder Reset
    delay(USB_CDC_SETTLE_MS);
    Serial.println();
    Serial.printf(LOG_FOX "ARDF-Fuchsjagd-Sender v%s – Boot\n", ARDF_VERSION);

    // Neuversuche zählen nur über Software-Neustarts hinweg
    if (retry_magic != RETRY_MAGIC || esp_reset_reason() != ESP_RST_SW) {
        resetRetries();
    }

    ardf::config::loadConfig(g_config);

    // Startphase ausführen (CLI + Sync)
    runStartupPhase();
}

void loop() {
    // Wird nie erreicht (Sendebetrieb läuft in runArdfMode/runSoloMode)
}

// ============================================================================
// Startphase: CLI + ESP-NOW-Sync
// ============================================================================
static void runStartupPhase() {
    const ardf::FoxProfile& prof = ardf::getProfile(g_config.profile_id);

    bool sync_ok = ardf::sync::initSync(g_config);
    ardf::cli::init(g_config, ardf::cli::Mode::Startup);
    if (!sync_ok) {
        handleError(ERR_SYNC_INIT);
        return;
    }

    // LED: 2 Hz während des Syncs, 0,5 Hz in der Startphase von Profil D
    ardf::led::set(prof.use_espnow ? ardf::led::Pattern::Sync : ardf::led::Pattern::Idle);

    // Hauptschleife der Startphase (nichts davon blockiert)
    while (true) {
        ardf::cli::process();
        ardf::led::update();

        if (prof.use_espnow) {
            // ARDF: Der Sendebeginn ergibt sich aus dem Slotplan des Masters
            ardf::sync::processSync();
            if (ardf::sync::isSyncComplete()) {
                if (ardf::cli::isActive()) {
                    Serial.println();
                    Serial.println(LOG_CLI "Sendebeginn – CLI wird beendet");
                }
                break;
            }
        } else {
            // Profil D: Start per Menüpunkt 10 oder Inaktivitäts-Timeout
            if (ardf::cli::startRequested()) break;
            if (ardf::cli::isTimedOut()) {
                Serial.println();
                Serial.println(LOG_CLI "Inaktivitäts-Timeout – Normalbetrieb wird gestartet");
                break;
            }
        }

        delay(5);
    }
    ardf::led::set(ardf::led::Pattern::Off);

    // Geänderte Konfiguration nie mit veraltetem Sync-Zustand senden
    if (ardf::cli::restartRequired()) {
        Serial.println(LOG_CLI "Konfiguration geändert – Neustart");
        delay(500);
        esp_restart();
    }

    if (!ardf::config::isValidConfig(g_config)) {
        handleError(ERR_INVALID_CONFIG);
        return;
    }

    if (!prof.use_espnow) {
        resetRetries();
        runSoloMode();
        return;
    }

    // Verbund prüfen
    const ardf::sync::Network& net = ardf::sync::network();
    if (net.collision()) {
        handleError(ERR_FOX_COLLISION);
        return;
    }
    if (net.mismatch()) {
        handleError(ERR_CONFIG_MISMATCH);
        return;
    }
    if (net.ownSlotIndex() < 0) {
        handleError(ERR_NOT_IN_PLAN);
        return;
    }

    ardf::sync::finishSync();
    resetRetries();
    runArdfMode();
}

// ============================================================================
// Bis zum lokalen Zeitpunkt target_us warten (Quarz-genau)
// ============================================================================
static void waitUntil(uint64_t target_us) {
    while (true) {
        uint64_t now = esp_timer_get_time();
        if (now >= target_us) return;
        uint64_t remaining_us = target_us - now;
        if (remaining_us > 3000) {
            ardf::led::update();
            uint64_t ms = (remaining_us - 2000) / 1000;
            delay(ms > 50 ? 50 : (uint32_t)ms);
        }
        // letzte 2–3 ms aktiv warten
    }
}

// ============================================================================
// ARDF-Betrieb (Profile A/B/C): Senden im gemeinsamen Slotraster
// ============================================================================
static void runArdfMode() {
    const ardf::sync::Network& net = ardf::sync::network();
    const ardf::schedule::CyclePlan& plan = net.plan();
    const ardf::timesync::TimeModel& model = net.timeModel();
    const uint64_t start_ref = net.planStartRefUs();
    const uint8_t slot_idx = (uint8_t)net.ownSlotIndex();

    const ardf::FoxProfile& prof = ardf::getProfile(g_config.profile_id);
    const uint16_t freq_hz = FOX_TONE_HZ[g_config.fox_id - 1];
    const bool is_afu = (g_config.callsign[0] != '\0');
    const uint32_t tx_norm_ms = ardf::txBudgetMs(g_config.profile_id, g_config.fox_id, nullptr);
    const uint32_t tx_call_ms = is_afu
        ? ardf::txBudgetMs(g_config.profile_id, g_config.fox_id, g_config.callsign)
        : tx_norm_ms;

    Serial.printf(LOG_FOX "Normalbetrieb: %s, Slot %u von %u, Zyklus %lu ms, Slot %lu ms\n",
                  net.isMaster() ? "Master" : "Slave", slot_idx + 1, plan.count,
                  (unsigned long)plan.cycle_ms, (unsigned long)plan.slot_ms);
    if (!net.isMaster()) {
        Serial.printf(LOG_SYNC "Zeitmodell: Offset %lld µs, Drift %.2f ppm (%s), %u Messwerte\n",
                      (long long)model.offsetUs(), model.driftPpm(),
                      model.hasDrift() ? "geschätzt" : "nicht geschätzt", model.samples());
    }
    logDutyStatus(g_config.profile_id, plan.count, plan.slot_ms, plan.cycle_ms, tx_norm_ms,
                  ardf::duty::calculateDutyPercent(tx_norm_ms, plan.cycle_ms),
                  plan.extended ? "VERLÄNGERT" : "OK");
    if (is_afu) {
        logDutyStatus(g_config.profile_id, plan.count, plan.slot_ms, plan.cycle_ms, tx_call_ms,
                      ardf::duty::calculateDutyPercent(tx_call_ms, plan.cycle_ms),
                      "AFU-Rufzeichen-Slot");
    }

    // WiFi ist aus; 80 MHz genügen und sparen Strom. Die Zeitbasis (SYSTIMER,
    // Quarz) und LEDC (APB-Takt) sind davon unabhängig.
    Serial.flush();
    setCpuFrequencyMhz(80);

    ardf::duty::Guard guard;
    uint8_t tx_counter = 0;   // Rufzeichen-Slot-Zähler (nur AFU)
    uint32_t k = 0;           // Zyklusindex

    while (true) {
        uint64_t target = model.toLocal(
            ardf::schedule::slotStartUs(start_ref, plan, slot_idx, k));
        if (esp_timer_get_time() > target + 2000) {
            // Slot verpasst (sollte nicht vorkommen) – nie verspätet senden
            Serial.println(LOG_WARN "Slot verpasst – weiter mit dem nächsten Zyklus");
            k++;
            continue;
        }
        waitUntil(target);

        bool send_callsign = is_afu && tx_counter == CALLSIGN_SLOT_TRIGGER;
        uint32_t tx_ms = send_callsign ? tx_call_ms : tx_norm_ms;
        uint64_t tx_start = esp_timer_get_time();

        if (!guard.allows(tx_start)) {
            // Letzte Instanz: sollte bei korrektem Slotplan nie greifen
            Serial.printf(LOG_ERR "Duty-Guard: Sendung blockiert (Freigabe in %lu ms)\n",
                          (unsigned long)((guard.nextAllowedUs() - tx_start) / 1000));
            ardf::led::set(ardf::led::Pattern::DutyBlocked);
        } else {
            ardf::led::set(ardf::led::Pattern::Off);   // LED folgt jetzt dem Ton
            guard.record(tx_start, tx_ms, send_callsign);
            if (send_callsign) {
                Serial.printf(LOG_FOX "AFU-Slot: Rufzeichen %s wird gesendet\n", g_config.callsign);
            }

            ardf::tx::power(true);
            delay(TX_WARMUP_MS);
            ardf::morse::sendArdfSlot(
                g_config.fox_id, g_config.profile_id,
                g_config.callsign, send_callsign,
                freq_hz, prof.wpm, prof.morse_repeats);
            ardf::tx::toneOff();
            ardf::tx::power(false);

            if (is_afu) {
                tx_counter = (tx_counter + 1) % CALLSIGN_SLOT_INTERVAL;
            }
        }
        k++;
    }
}

// ============================================================================
// Solo-Betrieb (Profil D): Aussendung, dann Deep-Sleep
// ============================================================================
static void runSoloMode() {
    const ardf::FoxProfile& prof = ardf::getProfile(g_config.profile_id);
    uint16_t freq_hz = FOX_TONE_HZ[g_config.fox_id - 1];

    // Pause aus dem ISM-sicheren Zyklus (mindestens tone_off_ms)
    uint32_t tx_ms = ardf::txBudgetMs(g_config.profile_id, g_config.fox_id, nullptr);
    ardf::schedule::FoxTiming own = {g_config.fox_id, tx_ms, tx_ms};
    ardf::schedule::CyclePlan plan = ardf::schedule::computePlan(g_config.profile_id, &own, 1);

    // TX-Sequenz: Dauerton
    ardf::tx::power(true);
    delay(TX_WARMUP_MS);
    ardf::tx::toneOn(freq_hz);
    delay(prof.tone_on_ms);
    ardf::tx::toneOff();
    ardf::tx::power(false);

    // Deep-Sleep für die Pausenzeit
    enterDeepSleep((uint64_t)(plan.cycle_ms - tx_ms) * 1000ULL);
}

// ============================================================================
// Deep-Sleep (Profil D); sleep_us = 0: ohne Weckzeit, nur Reset weckt auf
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

    // TX-Versorgung und Datenpin im Deep-Sleep auf LOW halten – sonst floaten
    // die Pins und das Modul kann teilversorgt sein oder ungewollt abstrahlen
    pinMode(DATA_PIN, OUTPUT);
    digitalWrite(DATA_PIN, LOW);
    digitalWrite(TX_VCC_EN_PIN, LOW);
    gpio_hold_en((gpio_num_t)DATA_PIN);
    gpio_hold_en((gpio_num_t)TX_VCC_EN_PIN);
    gpio_deep_sleep_hold_en();

    if (sleep_us > 0) {
        // Minimale Sleep-Zeit: 10 ms
        if (sleep_us < 10000ULL) sleep_us = 10000ULL;
        esp_sleep_enable_timer_wakeup(sleep_us);
    }
    esp_deep_sleep_start();
}

// ============================================================================
// Fehlerbehandlung
// ============================================================================
// Kein Senden. LED-Muster zeigt den Fehler, die CLI bleibt bedienbar. Nach
// ERROR_RECOVERY_MS ohne Eingabe: Neuversuch per Neustart (nur bei Fehlern,
// die ein anderer Fuchs verursacht haben kann), höchstens MAX_ERROR_RETRIES-mal.
// Danach Tiefschlaf bis zum Reset, damit der Akku nicht leergezogen wird.
static void handleError(ErrorState error) {
    const ardf::sync::Network& net = ardf::sync::network();

    switch (error) {
        case ERR_FOX_COLLISION:
            Serial.println(LOG_ERR "FOX_ID-Kollision – kein Senden möglich!");
            Serial.println(LOG_ERR "Bitte Fox-Nummer über CLI ändern (Menüpunkt 2)");
            break;
        case ERR_CONFIG_MISMATCH: {
            const uint8_t* m = net.mismatchMac();
            Serial.println(LOG_ERR "Konfigurations-Abweichung im Verbund – kein Senden!");
            Serial.printf(LOG_ERR "Abweichender Fuchs: Fox %u, Profil %c, "
                          "MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                          net.mismatchFoxId(), ardf::getProfileChar(net.mismatchProfileId()),
                          m[0], m[1], m[2], m[3], m[4], m[5]);
            break;
        }
        case ERR_NOT_IN_PLAN:
            Serial.println(LOG_ERR "Fuchs ist nicht im Slotplan – Slotplan war schon fest");
            Serial.println(LOG_ERR "Zu spät eingeschaltet? Alle Füchse vor dem Sendebeginn einschalten");
            break;
        case ERR_SYNC_INIT:
            Serial.println(LOG_ERR "ESP-NOW-Start fehlgeschlagen – kein Senden ohne Synchronisation");
            break;
        case ERR_INVALID_CONFIG:
            Serial.println(LOG_ERR "Ungültige Konfiguration – kein Senden!");
            break;
        default:
            break;
    }

    ardf::sync::finishSync();

    switch (error) {
        case ERR_FOX_COLLISION:   ardf::led::set(ardf::led::Pattern::Sos);      break;
        case ERR_CONFIG_MISMATCH:
        case ERR_NOT_IN_PLAN:     ardf::led::set(ardf::led::Pattern::Mismatch); break;
        default:                  ardf::led::set(ardf::led::Pattern::Invalid);  break;
    }

    // Neuversuch nur, wenn ein anderer Fuchs die Ursache sein kann. Ein
    // Nachzügler würde nach einem Neustart allein (unsynchronisiert) senden.
    bool retryable = (error == ERR_FOX_COLLISION || error == ERR_CONFIG_MISMATCH ||
                      error == ERR_SYNC_INIT);

    ardf::cli::init(g_config, ardf::cli::Mode::Error);
    uint64_t error_start_us = esp_timer_get_time();
    while (true) {
        ardf::cli::process();
        ardf::led::update();
        uint64_t last_us = ardf::cli::lastActivityUs();
        if (last_us < error_start_us) last_us = error_start_us;
        if (esp_timer_get_time() - last_us > (uint64_t)ERROR_RECOVERY_MS * 1000ULL) break;
        delay(10);
    }

    if (ardf::cli::restartRequired()) {
        Serial.println(LOG_CLI "Konfiguration geändert – Neustart");
        resetRetries();
        delay(500);
        esp_restart();
    }
    if (retryable && retry_count < MAX_ERROR_RETRIES) {
        retry_count++;
        Serial.printf(LOG_ERR "Neuer Versuch %u von %u – Neustart\n", retry_count, MAX_ERROR_RETRIES);
        delay(500);
        esp_restart();
    }

    Serial.println(LOG_ERR "Fehler besteht weiter – Tiefschlaf. Neustart mit der RST-Taste");
    Serial.flush();
    ardf::led::set(ardf::led::Pattern::Off);
    enterDeepSleep(0);
}
