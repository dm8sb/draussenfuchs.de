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
#include "schedule.h"
#include "sync_espnow.h"
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
    if (prof.use_espnow) {
        Serial.println(LOG_CLI "Enter drücken für Konfigurationsmenü");
        Serial.printf(LOG_CLI "Sync läuft im Hintergrund – Sendebeginn legt der Master fest "
                      "(spätestens %lu s nach seinem Start)\n",
                      (unsigned long)(SYNC_WINDOW_MS / 1000));
    } else {
        Serial.printf(LOG_CLI "Enter drücken für Konfigurationsmenü (Auto-Start in %lu s)\n",
                      (unsigned long)(CLI_TIMEOUT_PROFILD_MS / 1000));
    }
    Serial.println();
}

// Warten in Eingabeschleifen: Synchronisation weiter bedienen
static void waitTick() {
    sync::processSync();
    delay(1);
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
        waitTick();
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
        waitTick();
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
        waitTick();
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
            waitTick();
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

static void printMac(const uint8_t* m) {
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void printPlan(const schedule::CyclePlan& plan, int8_t own_idx) {
    Serial.printf("Slotplan:        %u Füchse | Zyklus %lu ms | Slot %lu ms%s\n",
                  plan.count, (unsigned long)plan.cycle_ms, (unsigned long)plan.slot_ms,
                  plan.extended ? " (verlängert)" : "");
    for (uint8_t i = 0; i < plan.count; i++) {
        Serial.printf("  Slot %u: Fox %u (%s)%s\n", i + 1, plan.fox_ids[i],
                      morse::getMoCallsign(plan.fox_ids[i]),
                      (int8_t)i == own_idx ? " <<<" : "");
    }
}

static void cmdDutyCycle() {
    Serial.println();
    Serial.println("=== Duty-Cycle-Berechnung ===");

    const FoxProfile& prof = getProfile(g_config.profile_id);
    const bool afu = (g_config.callsign[0] != '\0');
    uint32_t tx_total = txBudgetMs(g_config.profile_id, g_config.fox_id, nullptr);

    Serial.printf("Fox-ID:    %u (%s)\n", g_config.fox_id, morse::getMoCallsign(g_config.fox_id));
    Serial.printf("Profil:    %c\n", prof.name);
    Serial.printf("Ton:       %u Hz\n", FOX_TONE_HZ[g_config.fox_id - 1]);

    if (prof.is_continuous) {
        schedule::FoxTiming own = {g_config.fox_id, tx_total, tx_total};
        schedule::CyclePlan plan = schedule::computePlan(g_config.profile_id, &own, 1);
        Serial.printf("TX-AN:     %u ms (+ %u ms Vorlauf)\n", prof.tone_on_ms, TX_WARMUP_MS);
        Serial.printf("TX-AUS:    %lu ms\n", (unsigned long)(plan.cycle_ms - tx_total));
        Serial.printf("Zyklus:    %lu ms\n", (unsigned long)plan.cycle_ms);
        Serial.printf("Duty:      %.2f %% (inkl. 5 %% Sicherheitsaufschlag)\n",
                      duty::calculateDutyPercent(tx_total, plan.cycle_ms));
        Serial.println();
        return;
    }

    uint32_t tone_on = morse::calculateMoToneOnTimeMs(g_config.fox_id, prof.wpm, prof.morse_repeats);
    Serial.printf("WPM:       %u\n", prof.wpm);
    Serial.printf("Wiederh.:  %u\n", prof.morse_repeats);
    Serial.printf("Ton-AN:    %lu ms\n", (unsigned long)tone_on);
    Serial.printf("TX-Budget: %lu ms (Sequenz inkl. Pausen + 20 ms Vorlauf)\n",
                  (unsigned long)tx_total);
    if (afu) {
        Serial.printf("TX-Budget im Rufzeichen-Slot: %lu ms\n",
                      (unsigned long)txBudgetMs(g_config.profile_id, g_config.fox_id, g_config.callsign));
    }

    // Ungünstigster Fall für n aktive Füchse: eigener Fuchs + die längsten
    // Kennungen (MO5, MOH, …); im AFU-Modus mit dem eigenen Rufzeichen
    Serial.println();
    Serial.println("Slotplan je Anzahl aktiver Füchse (ungünstigster Fall):");
    Serial.println("Aktive  Zyklus     Slot       Duty    Entscheidung");
    Serial.println("------  ---------  ---------  ------  ------------");
    for (uint8_t n = 1; n <= MAX_FOXES; n++) {
        schedule::FoxTiming foxes[MAX_FOXES];
        uint8_t count = 0;
        foxes[count++] = {g_config.fox_id, tx_total,
                          txBudgetMs(g_config.profile_id, g_config.fox_id,
                                     afu ? g_config.callsign : nullptr)};
        for (uint8_t id = MAX_FOXES; id >= 1 && count < n; id--) {
            if (id == g_config.fox_id) continue;
            foxes[count++] = {id, txBudgetMs(g_config.profile_id, id, nullptr),
                              txBudgetMs(g_config.profile_id, id, afu ? g_config.callsign : nullptr)};
        }
        schedule::CyclePlan plan = schedule::computePlan(g_config.profile_id, foxes, count);
        Serial.printf("  %u     %6lu ms  %6lu ms  %5.1f%%  %s\n",
                      n, (unsigned long)plan.cycle_ms, (unsigned long)plan.slot_ms,
                      duty::calculateDutyPercent(plan.tx_norm_ms, plan.cycle_ms),
                      plan.extended ? "VERLÄNGERT" : "OK");
    }
    Serial.println();
}

static void cmdSyncStatus() {
    Serial.println();
    Serial.println("=== Sync-Status ===");

    if (!sync::isActive()) {
        Serial.println("ESP-NOW deaktiviert (Profil D: Solo-Modus)");
        Serial.println();
        return;
    }

    const sync::Network& net = sync::network();
    uint64_t now = esp_timer_get_time();
    uint8_t mac[6];
    sync::getOwnMac(mac);

    Serial.print("Eigene MAC:      ");
    printMac(mac);
    Serial.println();
    Serial.printf("Config-Hash:     0x%08lX\n", (unsigned long)net.own().config_hash);

    if (net.master() == sync::MASTER_NONE) {
        Serial.println("Master:          (Zuhörphase)");
    } else if (net.isMaster()) {
        Serial.println("Master:          dieser Fuchs");
    } else {
        const sync::PeerFox& m = net.peer((uint8_t)net.master());
        Serial.printf("Master:          Fox %u (", m.fox_id);
        printMac(m.mac);
        Serial.println(")");
    }

    if (net.hasPlan()) {
        printPlan(net.plan(), net.ownSlotIndex());
        uint64_t start = net.planStartLocalUs();
        Serial.printf("Sendebeginn:     in %.1f s%s\n",
                      start > now ? (double)(start - now) / 1e6 : 0.0,
                      net.frozen() ? " (Slotplan fest)" : "");
        if (net.ownSlotIndex() < 0) {
            Serial.println(LOG_ERR "Dieser Fuchs ist nicht im Slotplan!");
        }
    } else {
        Serial.println("Slotplan:        noch nicht bekannt");
    }

    if (net.collision()) {
        Serial.println(LOG_ERR "FOX_ID-KOLLISION erkannt!");
    }
    if (net.mismatch()) {
        Serial.printf(LOG_ERR "Konfigurations-Abweichung: Fox %u, Profil %c, MAC ",
                      net.mismatchFoxId(), getProfileChar(net.mismatchProfileId()));
        printMac(net.mismatchMac());
        Serial.println();
    }

    bool any = false;
    for (uint8_t i = 0; i < MAX_PEERS; i++) {
        const sync::PeerFox& p = net.peer(i);
        if (!p.in_use) continue;
        if (!any) Serial.println("\nErkannte Nachbarn:");
        any = true;
        Serial.printf("  Fox %u | Profil %c | MAC ", p.fox_id, getProfileChar(p.profile_id));
        printMac(p.mac);
        Serial.printf(" | %s | %s\n", p.compatible ? "kompatibel" : "MISMATCH",
                      net.peerActive(i, now) ? "aktiv" : "inaktiv");
    }
    if (!any) {
        Serial.println("Keine Nachbarn erkannt (Allein-Boot)");
    }
    Serial.println();
}

static void cmdTimeCorrection() {
    Serial.println();
    Serial.println("=== Zeitkorrekturwerte ===");

    if (sync::isActive()) {
        const sync::Network& net = sync::network();
        const timesync::TimeModel& m = net.timeModel();
        if (net.isMaster()) {
            Serial.println("Aktuell:      dieser Fuchs ist Master (Referenzuhr)");
        } else {
            Serial.println("Aktuell (gegen den Master):");
            Serial.printf("  Offset:     %lld µs\n", (long long)m.offsetUs());
            Serial.printf("  Drift:      %.2f ppm%s\n", m.driftPpm(),
                          m.hasDrift() ? "" : " (noch nicht geschätzt)");
            Serial.printf("  Messungen:  %u (davon %u verwendet)\n", m.samples(), m.inliers());
            Serial.printf("  Qualität:   %u %%\n", m.quality());
        }
    }

    TimeCorrectionData last;
    if (timecorr::load(last)) {
        Serial.println("Letzte gespeicherte Messung (nur Anzeige):");
        Serial.printf("  Drift:      %d ppm\n", last.drift_ppm);
        Serial.printf("  Offset:     %lld µs\n", (long long)last.offset_us);
        Serial.printf("  Qualität:   %u %%\n", last.quality);
        Serial.printf("  Messungen:  %u\n", last.meas_count);
    } else {
        Serial.println("Keine gespeicherte Messung");
    }
    Serial.println();
}

static void cmdClearTimeCorrection() {
    Serial.println(LOG_WARN "Zeitkorrekturwerte werden gelöscht!");
    timecorr::clear();
    sync::resetTimeModel();
    Serial.println(LOG_CFG "Zeitkorrekturwerte gelöscht, Messung beginnt neu");
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
        waitTick();
    }
    char c = Serial.read();
    while (Serial.available()) Serial.read();
    Serial.println(c);

    if (c == 'J' || c == 'j') {
        config::eraseAllNvs();
        config::resetToDefaults(g_config);
        config::saveConfig(g_config);
        sync::resetTimeModel();
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
    if (!sync::isActive()) {
        // Profil D: sofort starten
        Serial.println("=== Normalbetrieb wird gestartet ===");
        Serial.printf(LOG_FOX "Fox-ID: %u | Profil: %c | Ton: %u Hz\n",
                      g_config.fox_id, getProfileChar(g_config.profile_id),
                      FOX_TONE_HZ[g_config.fox_id - 1]);
        cli_active = false;
        g_start_normal_mode = true;
        return;
    }

    // ARDF: Der Sendebeginn gilt für den ganzen Verbund
    const sync::Network& net = sync::network();
    if (!net.hasPlan()) {
        Serial.println(LOG_WARN "Sync läuft noch – Slotplan noch nicht bekannt, bitte kurz warten");
        return;
    }
    if (sync::requestStart()) {
        Serial.println(LOG_FOX "Master: Sendebeginn für alle Füchse vorgezogen");
    } else if (!net.isMaster()) {
        Serial.println(LOG_FOX "Den Sendebeginn legt der Master fest");
    }
    uint64_t now = esp_timer_get_time();
    uint64_t start = net.planStartLocalUs();
    Serial.printf(LOG_FOX "Sendebeginn in %.1f s – CLI wird beendet\n",
                  start > now ? (double)(start - now) / 1e6 : 0.0);
    printPlan(net.plan(), net.ownSlotIndex());
    if (g_config.callsign[0] != '\0') {
        Serial.printf(LOG_FOX "Rufzeichen: %s (AFU-Modus, alle %u Aussendungen)\n",
                      g_config.callsign, CALLSIGN_SLOT_INTERVAL);
    }
    Serial.println();
    cli_active = false;
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
    Serial.println("Profile A–C: Den Sendebeginn legt der Master für alle Füchse fest;");
    Serial.println("             Menüpunkt 10 am Master zieht ihn vor.");
    Serial.println("Profil D:    Nach Inaktivität startet der Normalbetrieb automatisch.");
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
    // Nur Profil D – in den Profilen A–C bestimmt der Slotplan den Start
    if (!cli_active || sync::isActive()) return false;

    uint64_t elapsed = esp_timer_get_time() - last_activity_us;
    return (elapsed > (uint64_t)CLI_TIMEOUT_PROFILD_MS * 1000ULL);
}

void resetIdleTimer() {
    last_activity_us = esp_timer_get_time();
}

bool restartRequired() {
    return restart_required;
}

} // namespace cli
} // namespace ardf
