// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// cli.cpp – Serielle Kommandozeile Implementierung
// ============================================================================

#include "cli.h"
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"
#include "morse.h"
#include "duty_guard.h"
#include "sync_espnow.h"
#include "time_sync.h"
#include "time_correction_store.h"
#include "tx_driver.h"
#include <Arduino.h>
#include <cstring>

// Externer Zugriff auf die globale Konfiguration (in main.cpp definiert)
extern ConfigV2 g_config;
extern bool g_start_normal_mode;

namespace ardf {
namespace cli {

// ---- Interner Zustand ----
static bool cli_active = false;
static uint64_t last_activity_us = 0;
static char input_buffer[64];
static uint8_t input_pos = 0;
static bool menu_shown = false;
static bool restart_required = false;

void init() {
    cli_active = true;
    last_activity_us = esp_timer_get_time();
    input_pos = 0;
    menu_shown = false;

    const FoxProfile& prof = getProfile(g_config.profile_id);
    uint32_t timeout_ms = (prof.id == PROFILE_D_ID) ? CLI_TIMEOUT_PROFILD_MS : CLI_TIMEOUT_MS;

    Serial.println();
    Serial.println("=========================================");
    Serial.printf( "  ARDF-Fuchsjagd-Sender v%s\n", ARDF_VERSION);
    Serial.println("=========================================");
    Serial.printf( LOG_FOX "Fox-ID: %u | Profil: %c (%s)\n",
                  g_config.fox_id, getProfileChar(g_config.profile_id),
                  getProfile(g_config.profile_id).description);
    if (g_config.callsign[0] != '\0') {
        Serial.printf(LOG_FOX "Rufzeichen: %s (AFU-Modus)\n", g_config.callsign);
    }
    Serial.println();
    Serial.printf(LOG_CLI "Enter drücken für Konfigurationsmenü (Auto-Start in %lu s)\n",
                  (unsigned long)(timeout_ms / 1000));
    Serial.println(LOG_CLI "Sync-Fenster läuft im Hintergrund...");
    Serial.println();
}

void showMenu() {
    Serial.println();
    Serial.println("ARDF-Fuchsjagd-Sender – Konfiguration");
    Serial.println("-------------------------------------");
    Serial.println(" 1) Aktuelle Konfiguration anzeigen");
    Serial.println(" 2) Fox-Nummer ändern (1..5)");
    Serial.println(" 3) Profil ändern (A=Standard, B=Sprint, C=Ultra, D=draussenfuchs Solo)");
    Serial.println(" 4) Rufzeichen ändern/löschen");
    Serial.println(" 5) Duty-Cycle berechnen/anzeigen");
    Serial.println(" 6) Sync-Status anzeigen");
    Serial.println(" 7) Zeitkorrekturwerte anzeigen");
    Serial.println(" 8) Zeitkorrekturwerte löschen");
    Serial.println(" 9) Werkseinstellungen laden");
    Serial.println("10) Normalbetrieb starten und CLI verlassen");
    Serial.println("11) Neustart");
    Serial.println(" h) Hilfe");
    Serial.println();
    Serial.print("> ");
    menu_shown = true;
}

// ---- Interne Befehle ----

// Nach einer gespeicherten Änderung: Neustart anbieten. Ohne Neustart bleibt der
// Normalbetrieb gesperrt, weil Sync und Startphase noch mit der alten Konfiguration laufen.
static void requireRestart() {
    restart_required = true;
    Serial.println(LOG_WARN "Änderung wird erst nach einem Neustart wirksam.");
    Serial.print("Jetzt neu starten? (J/N): ");

    uint64_t start = esp_timer_get_time();
    while (!Serial.available()) {
        if (esp_timer_get_time() - start > 10000000ULL) {
            Serial.println("\nKein Neustart – bitte später Menüpunkt 11 wählen");
            return;
        }
        yield();
    }
    char c = Serial.read();
    while (Serial.available()) Serial.read();
    Serial.println(c);

    if (c == 'J' || c == 'j') {
        Serial.println(LOG_WARN "Neustart wird ausgeführt...");
        delay(500);
        esp_restart();
    }
    Serial.println("Kein Neustart – Normalbetrieb bis zum Neustart gesperrt (Menüpunkt 11)");
}

static void cmdShowConfig() {
    Serial.println();
    Serial.println("=== Aktuelle Konfiguration ===");
    Serial.printf("Fox-ID:      %u\n", g_config.fox_id);
    Serial.printf("Profil:      %c – %s\n",
                  getProfileChar(g_config.profile_id),
                  getProfile(g_config.profile_id).description);
    Serial.printf("Rufzeichen:  %s\n",
                  (g_config.callsign[0] != '\0') ? g_config.callsign : "(leer – ISM-Modus)");
    Serial.printf("Ton:         %u Hz\n", FOX_TONE_HZ[g_config.fox_id - 1]);

    const FoxProfile& prof = getProfile(g_config.profile_id);
    Serial.printf("Zyklus:      %lu ms\n", (unsigned long)prof.cycle_ms);
    Serial.printf("Slot:        %lu ms\n", (unsigned long)prof.slot_ms);
    Serial.printf("WPM:         %u\n", prof.wpm);
    Serial.printf("Wiederh.:    %u\n", prof.morse_repeats);
    Serial.printf("ESP-NOW:     %s\n", prof.use_espnow ? "Ja" : "Nein");
    Serial.printf("Dauerton:    %s\n", prof.is_continuous ? "Ja" : "Nein");
    Serial.printf("Version:     %s\n", ARDF_VERSION);
    Serial.println();
}

static void cmdChangeFoxId() {
    Serial.print("Neue Fox-Nummer (1..5): ");
    // Warte auf Eingabe (mit Timeout)
    uint64_t start = esp_timer_get_time();
    while (!Serial.available()) {
        if (esp_timer_get_time() - start > 10000000ULL) { // 10 s Timeout
            Serial.println("\nZeitüberschreitung");
            return;
        }
        yield();
    }
    int val = Serial.parseInt();
    Serial.println(val);

    if (val >= 1 && val <= MAX_FOXES) {
        g_config.fox_id = (uint8_t)val;
        config::saveConfig(g_config);
        Serial.printf(LOG_CFG "Fox-Nummer auf %u geändert (Ton: %u Hz)\n",
                      g_config.fox_id, FOX_TONE_HZ[g_config.fox_id - 1]);
        requireRestart();
    } else {
        Serial.println(LOG_ERR "Ungültige Fox-Nummer (erlaubt: 1..5)");
    }
}

static void cmdChangeProfile() {
    Serial.print("Neues Profil (A/B/C/D): ");
    uint64_t start = esp_timer_get_time();
    while (!Serial.available()) {
        if (esp_timer_get_time() - start > 10000000ULL) {
            Serial.println("\nZeitüberschreitung");
            return;
        }
        yield();
    }
    char c = Serial.read();
    // Restliche Zeichen konsumieren
    while (Serial.available()) Serial.read();
    Serial.println(c);

    uint8_t id = getProfileIdByChar(c);
    if (id == 0xFF) {
        Serial.println(LOG_ERR "Ungültiges Profil (erlaubt: A, B, C, D)");
        return;
    }

    g_config.profile_id = id;
    config::saveConfig(g_config);

    const FoxProfile& prof = getProfile(id);
    Serial.printf(LOG_CFG "Profil auf %c geändert: %s\n", prof.name, prof.description);

    if (id == PROFILE_D_ID) {
        Serial.println(LOG_WARN "Profil D: Solo-Modus ohne ARDF-Synchronisation");
        Serial.println(LOG_WARN "Fox-spezifischer Dauerton, kein ESP-NOW");
    }
    requireRestart();
}

static void cmdChangeCallsign() {
    Serial.println("Aktuelles Rufzeichen: ");
    if (g_config.callsign[0] != '\0') {
        Serial.printf("  %s\n", g_config.callsign);
    } else {
        Serial.println("  (leer – ISM-Modus)");
    }
    Serial.println("Neues Rufzeichen eingeben (leer = löschen): ");
    Serial.print("> ");

    uint64_t start = esp_timer_get_time();
    char buf[CALLSIGN_MAX_LEN + 2];
    uint8_t pos = 0;
    memset(buf, 0, sizeof(buf));

    while (true) {
        if (esp_timer_get_time() - start > 30000000ULL) { // 30 s Timeout
            Serial.println("\nZeitüberschreitung");
            return;
        }
        if (!Serial.available()) {
            yield();
            continue;
        }
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            // CR oder LF → Eingabe abschließen
            // Restliche CR/LF konsumieren
            delay(10);
            while (Serial.available()) {
                char x = Serial.peek();
                if (x == '\r' || x == '\n') Serial.read();
                else break;
            }
            break;
        }
        if (pos < CALLSIGN_MAX_LEN) {
            buf[pos++] = c;
            Serial.print(c);
        }
    }
    buf[pos] = '\0';
    Serial.println();

    // Löschen?
    if (pos == 0) {
        memset(g_config.callsign, 0, sizeof(g_config.callsign));
        config::saveConfig(g_config);
        Serial.println(LOG_CFG "Rufzeichen gelöscht – ISM-Modus aktiv");
        requireRestart();
        return;
    }

    // Großbuchstaben-Konvertierung
    for (uint8_t i = 0; i < pos; i++) {
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = buf[i] - 'a' + 'A';
    }

    // Validierung
    for (uint8_t i = 0; i < pos; i++) {
        char ch = buf[i];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '/' || ch == '-' || ch == '.')) {
            Serial.printf(LOG_ERR "Ungültiges Zeichen '%c' – erlaubt: A-Z 0-9 / - .\n", ch);
            return;
        }
    }

    // Übernehmen
    memset(g_config.callsign, 0, sizeof(g_config.callsign));
    strncpy(g_config.callsign, buf, CALLSIGN_MAX_LEN);
    config::saveConfig(g_config);

    Serial.printf(LOG_CFG "Rufzeichen gesetzt: %s\n", g_config.callsign);
    Serial.println(LOG_WARN "Rufzeichen nur für AFU-Betrieb verwenden; im ISM-Betrieb leer lassen.");
    requireRestart();
}

static void cmdDutyCycle() {
    Serial.println();
    Serial.println("=== Duty-Cycle-Berechnung ===");

    const FoxProfile& prof = getProfile(g_config.profile_id);
    uint16_t freq_hz = FOX_TONE_HZ[g_config.fox_id - 1];
    const char* mo = morse::getMoCallsign(g_config.fox_id);

    Serial.printf("Fox-ID:    %u (%s)\n", g_config.fox_id, mo);
    Serial.printf("Profil:    %c\n", prof.name);
    Serial.printf("Ton:       %u Hz\n", freq_hz);
    Serial.printf("WPM:       %u\n", prof.wpm);
    Serial.printf("Wiederh.:  %u\n", prof.morse_repeats);

    uint32_t tx_total = txBudgetMs(g_config.profile_id, g_config.fox_id, nullptr);

    if (prof.is_continuous) {
        duty::GuardCheck gc = duty::checkDuty(
            g_config.profile_id, g_config.fox_id, 1, tx_total, false);
        Serial.printf("TX-AN:     %u ms (+ %u ms Vorlauf)\n", prof.tone_on_ms, TX_WARMUP_MS);
        Serial.printf("TX-AUS:    %lu ms\n",
                      (unsigned long)(gc.effective_cycle_ms - tx_total));
        Serial.printf("Zyklus:    %lu ms\n", (unsigned long)gc.effective_cycle_ms);
        Serial.printf("Duty:      %.2f %% (inkl. 5 %% Sicherheitsaufschlag)\n", gc.duty_percent);
    } else {
        // Ton-AN-Zeit (Information) und Sendezeit-Budget (Grundlage des Duty-Guards)
        uint32_t tone_on = morse::calculateMoToneOnTimeMs(
            g_config.fox_id, prof.wpm, prof.morse_repeats);

        Serial.printf("Ton-AN:    %lu ms\n", (unsigned long)tone_on);
        Serial.printf("TX-Budget: %lu ms (Sequenz inkl. Pausen + 20 ms Vorlauf)\n",
                      (unsigned long)tx_total);

        // Für 1..5 Füchse
        Serial.println();
        Serial.println("Aktive  Zyklus   Slot    Duty    Entscheidung");
        Serial.println("------  ------   ----    ----    ------------");

        for (uint8_t n = 1; n <= MAX_FOXES; n++) {
            duty::GuardCheck gc = duty::checkDuty(
                g_config.profile_id, g_config.fox_id, n, tx_total, false);
            Serial.printf("  %u     %5lu ms  %5lu ms  %5.1f%%   %s\n",
                          n,
                          (unsigned long)gc.effective_cycle_ms,
                          (unsigned long)gc.effective_slot_ms,
                          gc.duty_percent,
                          gc.decision_text);
        }
    }
    Serial.println();
}

static void cmdSyncStatus() {
    Serial.println();
    Serial.println("=== Sync-Status ===");

    const sync::SyncState& ss = sync::getSyncState();
    const FoxProfile& prof = getProfile(g_config.profile_id);

    if (!prof.use_espnow) {
        Serial.println("ESP-NOW deaktiviert (Profil D: Solo-Modus)");
        Serial.println();
        return;
    }

    uint8_t mac[6];
    sync::getOwnMac(mac);
    Serial.printf("Eigene MAC:      %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("Master:          %s\n", ss.is_master ? "Ja" : "Nein");
    Serial.printf("Aktive Füchse:   %u\n", ss.active_count);
    Serial.printf("Eigener Slot:    %u\n", ss.own_slot_index);
    Serial.printf("Eff. Zyklus:     %lu ms\n", (unsigned long)ss.effective_cycle_ms);
    Serial.printf("Eff. Slot:       %lu ms\n", (unsigned long)ss.effective_slot_ms);
    Serial.printf("Config-Hash:     0x%08lX\n", (unsigned long)ss.config_hash);

    if (ss.fox_id_collision) {
        Serial.println(LOG_ERR "FOX_ID-KOLLISION erkannt!");
    }
    if (ss.config_mismatch) {
        Serial.printf(LOG_ERR "Konfigurations-Abweichung: Fox %u, Profil %c, "
                      "MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                      ss.mismatch_fox_id,
                      getProfileChar(ss.mismatch_profile_id),
                      ss.mismatch_mac[0], ss.mismatch_mac[1], ss.mismatch_mac[2],
                      ss.mismatch_mac[3], ss.mismatch_mac[4], ss.mismatch_mac[5]);
    }

    if (ss.peer_count > 0) {
        Serial.println("\nErkannte Nachbarn:");
        for (uint8_t i = 0; i < ss.peer_count; i++) {
            const sync::PeerFox& p = ss.peers[i];
            Serial.printf("  Fox %u | MAC %02X:%02X:%02X:%02X:%02X:%02X | %s | %s\n",
                          p.fox_id,
                          p.mac[0], p.mac[1], p.mac[2],
                          p.mac[3], p.mac[4], p.mac[5],
                          p.compatible ? "kompatibel" : "MISMATCH",
                          p.active ? "aktiv" : "inaktiv");
        }
    } else {
        Serial.println("Keine Nachbarn erkannt (Allein-Boot)");
    }
    Serial.println();
}

static void cmdTimeCorrection() {
    Serial.println();
    Serial.println("=== Zeitkorrekturwerte ===");

    const timesync::TimeSyncState& ts = timesync::getState();
    Serial.printf("Drift:        %d ppm\n", ts.drift_ppm);
    Serial.printf("Offset:       %lld µs\n", (long long)ts.offset_us);
    Serial.printf("Qualität:     %u %%\n", ts.quality);
    Serial.printf("Messungen:    %u\n", ts.meas_count);
    Serial.printf("Gültig:       %s\n", ts.valid ? "Ja" : "Nein");

    if (ts.valid && ts.last_update_us > 0) {
        uint64_t elapsed = esp_timer_get_time() - ts.last_update_us;
        Serial.printf("Letzte Akt.:  vor %.1f s\n", (float)elapsed / 1000000.0f);
    }
    Serial.println();
}

static void cmdClearTimeCorrection() {
    Serial.println(LOG_WARN "Zeitkorrekturwerte werden gelöscht!");
    timesync::clearNvs();
    timesync::reset();
    Serial.println(LOG_CFG "Zeitkorrekturwerte gelöscht und zurückgesetzt");
}

static void cmdFactoryReset() {
    Serial.println(LOG_WARN "Alle Einstellungen werden auf Werkseinstellungen zurückgesetzt!");
    Serial.print("Bestätigung (J/N): ");

    uint64_t start = esp_timer_get_time();
    while (!Serial.available()) {
        if (esp_timer_get_time() - start > 10000000ULL) {
            Serial.println("\nAbgebrochen");
            return;
        }
        yield();
    }
    char c = Serial.read();
    while (Serial.available()) Serial.read();
    Serial.println(c);

    if (c == 'J' || c == 'j') {
        config::eraseAllNvs();
        config::resetToDefaults(g_config);
        config::saveConfig(g_config);
        Serial.println(LOG_CFG "Werkseinstellungen geladen und gespeichert");
        requireRestart();
    } else {
        Serial.println("Abgebrochen");
    }
}

static void cmdStartNormal() {
    if (restart_required) {
        Serial.println(LOG_ERR "Konfiguration wurde geändert – erst neu starten (Menüpunkt 11)");
        return;
    }

    Serial.println();
    Serial.println("=== Normalbetrieb wird gestartet ===");

    const sync::SyncState& ss = sync::getSyncState();
    const FoxProfile& prof = getProfile(g_config.profile_id);

    Serial.printf(LOG_FOX "Fox-ID: %u | Profil: %c\n",
                  g_config.fox_id, prof.name);
    Serial.printf(LOG_FOX "Aktive Füchse: %u\n", ss.active_count);
    Serial.printf(LOG_FOX "Eigener Slot:  %u von %u\n",
                  ss.own_slot_index + 1, ss.active_count);
    Serial.printf(LOG_FOX "Eff. Zyklus:   %lu ms\n", (unsigned long)ss.effective_cycle_ms);
    Serial.printf(LOG_FOX "Eff. Slot:     %lu ms\n", (unsigned long)ss.effective_slot_ms);
    Serial.printf(LOG_FOX "Ton:           %u Hz\n", FOX_TONE_HZ[g_config.fox_id - 1]);

    if (g_config.callsign[0] != '\0') {
        Serial.printf(LOG_FOX "Rufzeichen: %s (AFU-Modus, alle %u Slots)\n",
                      g_config.callsign, CALLSIGN_SLOT_INTERVAL);
    }

    Serial.println();
    Serial.println(LOG_FOX "Slotplan:");
    for (uint8_t i = 0; i < ss.active_count; i++) {
        const char* marker = (i == ss.own_slot_index) ? " <<<" : "";
        Serial.printf("  Slot %u: Fox %u%s\n", i + 1, ss.active_fox_ids[i], marker);
    }
    Serial.println();

    cli_active = false;
    g_start_normal_mode = true;
}

static void cmdReboot() {
    Serial.println(LOG_WARN "Neustart wird ausgeführt...");
    delay(500);
    esp_restart();
}

static void cmdHelp() {
    Serial.println();
    Serial.println("=== Hilfe ===");
    Serial.println("Dieses Menü ist nur während der Startphase aktiv.");
    Serial.println("Nach Inaktivität startet der Normalbetrieb automatisch.");
    Serial.println();
    Serial.println("Profile:");
    Serial.println("  A – ISM-Standard: 60s Zyklus, 18 WPM, MO-Kennung 2×");
    Serial.println("  B – ISM-Sprint:   30s Zyklus, 20 WPM, MO-Kennung 1×");
    Serial.println("  C – ISM-Ultra:    20s Zyklus, 24 WPM, MO-Kennung 1× (Default)");
    Serial.println("  D – draussenfuchs Solo: Dauerton, kein Sync, kein ARDF");
    Serial.println();
    Serial.println("Fuchs-Kennungen: MOE(1), MOI(2), MOS(3), MOH(4), MO5(5)");
    Serial.println("Töne:            700, 800, 900, 1000, 1200 Hz");
    Serial.println();
    Serial.println("Rufzeichen nur für AFU-Betrieb – im ISM-Betrieb leer lassen!");
    Serial.println();
}

// ---- Befehlsverarbeitung ----
static void processCommand(const char* cmd) {
    // Leerzeichen entfernen
    while (*cmd == ' ') cmd++;

    if (strcmp(cmd, "1") == 0) {
        cmdShowConfig();
    } else if (strcmp(cmd, "2") == 0) {
        cmdChangeFoxId();
    } else if (strcmp(cmd, "3") == 0) {
        cmdChangeProfile();
    } else if (strcmp(cmd, "4") == 0) {
        cmdChangeCallsign();
    } else if (strcmp(cmd, "5") == 0) {
        cmdDutyCycle();
    } else if (strcmp(cmd, "6") == 0) {
        cmdSyncStatus();
    } else if (strcmp(cmd, "7") == 0) {
        cmdTimeCorrection();
    } else if (strcmp(cmd, "8") == 0) {
        cmdClearTimeCorrection();
    } else if (strcmp(cmd, "9") == 0) {
        cmdFactoryReset();
    } else if (strcmp(cmd, "10") == 0) {
        cmdStartNormal(); // beendet die CLI, außer ein Neustart steht aus
    } else if (strcmp(cmd, "11") == 0) {
        cmdReboot();
    } else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "H") == 0) {
        cmdHelp();
    } else if (strlen(cmd) == 0) {
        // Leere Eingabe (Enter) → Menü anzeigen
    } else {
        Serial.printf(LOG_ERR "Unbekannter Befehl: '%s'\n", cmd);
    }

    if (cli_active) {
        showMenu();
    }
}

bool process() {
    if (!cli_active) return false;

    // Zeichen einlesen (non-blocking)
    while (Serial.available()) {
        char c = Serial.read();
        last_activity_us = esp_timer_get_time();

        // CR/LF → Befehl verarbeiten
        if (c == '\r' || c == '\n') {
            // Weitere CR/LF überspringen
            delay(5);
            while (Serial.available()) {
                char x = Serial.peek();
                if (x == '\r' || x == '\n') Serial.read();
                else break;
            }

            input_buffer[input_pos] = '\0';
            Serial.println();

            if (!menu_shown && input_pos == 0) {
                // Erster Enter-Druck → Menü anzeigen
                showMenu();
            } else {
                processCommand(input_buffer);
            }

            input_pos = 0;
            return !cli_active;
        }

        // Backspace
        if (c == 0x08 || c == 0x7F) {
            if (input_pos > 0) {
                input_pos--;
                Serial.print("\b \b");
            }
            continue;
        }

        // Zeichen puffern
        if (input_pos < sizeof(input_buffer) - 1) {
            input_buffer[input_pos++] = c;
            Serial.print(c);
        }
    }

    return false;
}

bool isActive() {
    return cli_active;
}

bool isTimedOut() {
    if (!cli_active) return false;

    const FoxProfile& prof = getProfile(g_config.profile_id);
    uint32_t timeout_ms = (prof.id == PROFILE_D_ID) ? CLI_TIMEOUT_PROFILD_MS : CLI_TIMEOUT_MS;
    uint64_t elapsed = esp_timer_get_time() - last_activity_us;

    return (elapsed > (uint64_t)timeout_ms * 1000ULL);
}

void resetIdleTimer() {
    last_activity_us = esp_timer_get_time();
}

bool restartRequired() {
    return restart_required;
}

} // namespace cli
} // namespace ardf
