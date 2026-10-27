// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// cli.cpp – Serielle Kommandozeile Implementierung
// ============================================================================
//
// Nicht blockierend: Eingaben werden zeichenweise in einen Zeilenpuffer gelesen
// und bei Enter ausgewertet. Rückfragen (Fox-Nummer, Profil, …) sind Zustände
// mit Zeitüberschreitung, damit Sync und LED währenddessen weiterlaufen.

#include "cli.h"
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"
#include "morse.h"
#include "duty_guard.h"
#include "schedule.h"
#include "sync_espnow.h"
#include "time_correction_store.h"
#include <Arduino.h>
#include <cstring>

namespace ardf {
namespace cli {

// ---- Interner Zustand ----
enum class State : uint8_t {
    Idle,             // Menü noch nicht angezeigt
    Menu,             // Menüauswahl
    AwaitFox,         // Rückfrage Fox-Nummer
    AwaitProfile,     // Rückfrage Profil
    AwaitCallsign,    // Rückfrage Rufzeichen
    AwaitFactory,     // Bestätigung Werkseinstellungen
    AwaitRestart      // Rückfrage Neustart nach Änderung
};

static FoxConfig* cfg_ = nullptr;
static Mode mode = Mode::Startup;
static State state = State::Idle;
static bool cli_active = false;
static bool start_requested = false;
static bool restart_required = false;
static uint64_t last_activity_us = 0;
static uint64_t prompt_start_us = 0;

static char line[32];
static uint8_t line_len = 0;
static bool last_was_cr = false;

static void showMenu();

static void printMac(const uint8_t* m) {
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void prompt(State s, const char* text) {
    state = s;
    prompt_start_us = esp_timer_get_time();
    Serial.print(text);
}

void init(FoxConfig& cfg, Mode m) {
    cfg_ = &cfg;
    mode = m;
    state = State::Idle;
    cli_active = true;
    start_requested = false;
    last_activity_us = esp_timer_get_time();
    line_len = 0;

    const FoxProfile& prof = getProfile(cfg.profile_id);

    Serial.println();
    if (mode == Mode::Error) {
        Serial.println(LOG_CLI "Enter drücken für das Menü: Konfiguration prüfen bzw. ändern,");
        Serial.println(LOG_CLI "danach neu starten (Menüpunkt 11).");
        Serial.println();
        return;
    }

    Serial.println("=========================================");
    Serial.printf( "  ARDF-Fuchsjagd-Sender v%s\n", ARDF_VERSION);
    Serial.println("=========================================");
    Serial.printf( LOG_FOX "Fox-ID: %u | Profil: %c (%s)\n",
                  cfg.fox_id, getProfileChar(cfg.profile_id), prof.description);
    if (cfg.callsign[0] != '\0') {
        Serial.printf(LOG_FOX "Rufzeichen: %s (AFU-Modus)\n", cfg.callsign);
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
        Serial.println(LOG_CLI "Hinweis: Im Betrieb ist die CLI nur nach Reset (RST) erreichbar");
    }
    Serial.println();
}

static void showMenu() {
    state = State::Menu;
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
    if (mode == Mode::Startup) {
        Serial.println("10) Normalbetrieb starten und CLI verlassen");
    }
    Serial.println("11) Neustart");
    Serial.println(" h) Hilfe");
    Serial.println();
    Serial.print("> ");
}

// ---- Interne Befehle ----

// Nach einer gespeicherten Änderung: Neustart anbieten. Ohne Neustart bleibt der
// Normalbetrieb gesperrt, weil Sync und Startphase noch mit der alten Konfiguration laufen.
static void requireRestart() {
    restart_required = true;
    Serial.println(LOG_WARN "Änderung wird erst nach einem Neustart wirksam.");
    prompt(State::AwaitRestart, "Jetzt neu starten? (J/N): ");
}

static void cmdShowConfig() {
    const FoxConfig& cfg = *cfg_;
    const FoxProfile& prof = getProfile(cfg.profile_id);
    Serial.println();
    Serial.println("=== Aktuelle Konfiguration ===");
    Serial.printf("Fox-ID:      %u (%s)\n", cfg.fox_id, morse::getMoCallsign(cfg.fox_id));
    Serial.printf("Profil:      %c – %s\n", prof.name, prof.description);
    Serial.printf("Rufzeichen:  %s\n",
                  (cfg.callsign[0] != '\0') ? cfg.callsign : "(leer – ISM-Modus)");
    Serial.printf("Ton:         %u Hz\n", FOX_TONE_HZ[cfg.fox_id - 1]);
    Serial.printf("Zyklus:      %lu ms (Profilwert, siehe Menüpunkt 5/6)\n",
                  (unsigned long)prof.cycle_ms);
    Serial.printf("Slot:        %lu ms (Profilwert)\n", (unsigned long)prof.slot_ms);
    Serial.printf("WPM:         %u\n", prof.wpm);
    Serial.printf("Wiederh.:    %u\n", prof.morse_repeats);
    Serial.printf("ESP-NOW:     %s\n", prof.use_espnow ? "Ja" : "Nein");
    Serial.printf("Dauerton:    %s\n", prof.is_continuous ? "Ja" : "Nein");
    Serial.printf("Version:     %s\n", ARDF_VERSION);
    if (restart_required) {
        Serial.println(LOG_WARN "Geänderte Konfiguration – Neustart ausstehend");
    }
    Serial.println();
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
    const FoxConfig& cfg = *cfg_;
    const FoxProfile& prof = getProfile(cfg.profile_id);
    const bool afu = (cfg.callsign[0] != '\0');
    uint32_t tx_total = txBudgetMs(cfg.profile_id, cfg.fox_id, nullptr);

    Serial.println();
    Serial.println("=== Duty-Cycle-Berechnung ===");
    Serial.printf("Fox-ID:    %u (%s)\n", cfg.fox_id, morse::getMoCallsign(cfg.fox_id));
    Serial.printf("Profil:    %c\n", prof.name);
    Serial.printf("Ton:       %u Hz\n", FOX_TONE_HZ[cfg.fox_id - 1]);

    if (prof.is_continuous) {
        schedule::FoxTiming own = {cfg.fox_id, tx_total, tx_total};
        schedule::CyclePlan plan = schedule::computePlan(cfg.profile_id, &own, 1);
        Serial.printf("TX-AN:     %u ms (+ %u ms Vorlauf)\n", prof.tone_on_ms, TX_WARMUP_MS);
        Serial.printf("TX-AUS:    %lu ms\n", (unsigned long)(plan.cycle_ms - tx_total));
        Serial.printf("Zyklus:    %lu ms\n", (unsigned long)plan.cycle_ms);
        Serial.printf("Duty:      %.2f %% (inkl. 5 %% Sicherheitsaufschlag)\n",
                      duty::calculateDutyPercent(tx_total, plan.cycle_ms));
        Serial.println();
        return;
    }

    uint32_t tone_on = morse::calculateMoToneOnTimeMs(cfg.fox_id, prof.wpm, prof.morse_repeats);
    Serial.printf("WPM:       %u\n", prof.wpm);
    Serial.printf("Wiederh.:  %u\n", prof.morse_repeats);
    Serial.printf("Ton-AN:    %lu ms\n", (unsigned long)tone_on);
    Serial.printf("TX-Budget: %lu ms (Sequenz inkl. Pausen + 20 ms Vorlauf)\n",
                  (unsigned long)tx_total);
    if (afu) {
        Serial.printf("TX-Budget im Rufzeichen-Slot: %lu ms\n",
                      (unsigned long)txBudgetMs(cfg.profile_id, cfg.fox_id, cfg.callsign));
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
        foxes[count++] = {cfg.fox_id, tx_total,
                          txBudgetMs(cfg.profile_id, cfg.fox_id, afu ? cfg.callsign : nullptr)};
        for (uint8_t id = MAX_FOXES; id >= 1 && count < n; id--) {
            if (id == cfg.fox_id) continue;
            foxes[count++] = {id, txBudgetMs(cfg.profile_id, id, nullptr),
                              txBudgetMs(cfg.profile_id, id, afu ? cfg.callsign : nullptr)};
        }
        schedule::CyclePlan plan = schedule::computePlan(cfg.profile_id, foxes, count);
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
        if (getProfile(cfg_->profile_id).use_espnow) {
            Serial.println("ESP-NOW nicht aktiv");
        } else {
            Serial.println("ESP-NOW deaktiviert (Profil D: Solo-Modus)");
        }
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

static void cmdStartNormal() {
    if (mode == Mode::Error) {
        Serial.println(LOG_ERR "Im Fehlerzustand nicht möglich – Ursache beheben, dann neu starten (11)");
        return;
    }
    if (restart_required) {
        Serial.println(LOG_ERR "Konfiguration wurde geändert – erst neu starten (Menüpunkt 11)");
        return;
    }

    const FoxConfig& cfg = *cfg_;
    Serial.println();
    if (!sync::isActive()) {
        // Profil D: sofort starten
        Serial.println("=== Normalbetrieb wird gestartet ===");
        Serial.printf(LOG_FOX "Fox-ID: %u | Profil: %c | Ton: %u Hz\n",
                      cfg.fox_id, getProfileChar(cfg.profile_id), FOX_TONE_HZ[cfg.fox_id - 1]);
        cli_active = false;
        start_requested = true;
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
    if (cfg.callsign[0] != '\0') {
        Serial.printf(LOG_FOX "Rufzeichen: %s (AFU-Modus, alle %u Aussendungen)\n",
                      cfg.callsign, CALLSIGN_SLOT_INTERVAL);
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

// ---- Auswertung einer Eingabezeile ----

static void handleMenu(const char* cmd) {
    if (strcmp(cmd, "1") == 0) {
        cmdShowConfig();
    } else if (strcmp(cmd, "2") == 0) {
        prompt(State::AwaitFox, "Neue Fox-Nummer (1..5): ");
        return;
    } else if (strcmp(cmd, "3") == 0) {
        prompt(State::AwaitProfile, "Neues Profil (A/B/C/D): ");
        return;
    } else if (strcmp(cmd, "4") == 0) {
        Serial.printf("Aktuelles Rufzeichen: %s\n",
                      cfg_->callsign[0] != '\0' ? cfg_->callsign : "(leer – ISM-Modus)");
        prompt(State::AwaitCallsign, "Neues Rufzeichen (leer = löschen): ");
        return;
    } else if (strcmp(cmd, "5") == 0) {
        cmdDutyCycle();
    } else if (strcmp(cmd, "6") == 0) {
        cmdSyncStatus();
    } else if (strcmp(cmd, "7") == 0) {
        cmdTimeCorrection();
    } else if (strcmp(cmd, "8") == 0) {
        cmdClearTimeCorrection();
    } else if (strcmp(cmd, "9") == 0) {
        Serial.println(LOG_WARN "Alle Einstellungen werden auf Werkseinstellungen zurückgesetzt!");
        prompt(State::AwaitFactory, "Bestätigung (J/N): ");
        return;
    } else if (strcmp(cmd, "10") == 0) {
        cmdStartNormal();   // beendet die CLI, außer etwas spricht dagegen
    } else if (strcmp(cmd, "11") == 0) {
        cmdReboot();
    } else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "H") == 0) {
        cmdHelp();
    } else if (cmd[0] != '\0') {
        Serial.printf(LOG_ERR "Unbekannter Befehl: '%s'\n", cmd);
    }
    if (cli_active) showMenu();
}

static void handleFox(const char* in) {
    FoxConfig& cfg = *cfg_;
    // Genau eine Ziffer 1..5 (kein parseInt: „2abc" wird abgelehnt)
    if (strlen(in) == 1 && in[0] >= '1' && in[0] <= '0' + MAX_FOXES) {
        cfg.fox_id = (uint8_t)(in[0] - '0');
        config::saveConfig(cfg);
        Serial.printf(LOG_CFG "Fox-Nummer auf %u geändert (Ton: %u Hz)\n",
                      cfg.fox_id, FOX_TONE_HZ[cfg.fox_id - 1]);
        requireRestart();
        return;
    }
    Serial.println(LOG_ERR "Ungültige Fox-Nummer (erlaubt: 1..5) – Wert unverändert");
    showMenu();
}

static void handleProfile(const char* in) {
    FoxConfig& cfg = *cfg_;
    uint8_t id = (strlen(in) == 1) ? getProfileIdByChar(in[0]) : 0xFF;
    if (id == 0xFF) {
        Serial.println(LOG_ERR "Ungültiges Profil (erlaubt: A, B, C, D) – Wert unverändert");
        showMenu();
        return;
    }
    cfg.profile_id = id;
    config::saveConfig(cfg);

    const FoxProfile& prof = getProfile(id);
    Serial.printf(LOG_CFG "Profil auf %c geändert: %s\n", prof.name, prof.description);
    if (id == PROFILE_D_ID) {
        Serial.println(LOG_WARN "Profil D: Solo-Modus ohne ARDF-Synchronisation");
        Serial.println(LOG_WARN "Fox-spezifischer Dauerton, kein ESP-NOW");
    }
    requireRestart();
}

static void handleCallsign(const char* in) {
    FoxConfig& cfg = *cfg_;
    size_t len = strlen(in);

    if (len == 0) {
        memset(cfg.callsign, 0, sizeof(cfg.callsign));
        config::saveConfig(cfg);
        Serial.println(LOG_CFG "Rufzeichen gelöscht – ISM-Modus aktiv");
        requireRestart();
        return;
    }
    if (len > CALLSIGN_MAX_LEN) {
        Serial.printf(LOG_ERR "Rufzeichen zu lang (max. %u Zeichen) – Wert unverändert\n",
                      CALLSIGN_MAX_LEN);
        showMenu();
        return;
    }

    char buf[CALLSIGN_MAX_LEN + 1];
    for (size_t i = 0; i <= len; i++) {
        char ch = in[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (ch != '\0' && !((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                            ch == '/' || ch == '-' || ch == '.')) {
            Serial.printf(LOG_ERR "Ungültiges Zeichen '%c' – erlaubt: A-Z 0-9 / - . "
                          "– Wert unverändert\n", ch);
            showMenu();
            return;
        }
        buf[i] = ch;
    }

    memset(cfg.callsign, 0, sizeof(cfg.callsign));
    memcpy(cfg.callsign, buf, len);
    config::saveConfig(cfg);
    Serial.printf(LOG_CFG "Rufzeichen gesetzt: %s\n", cfg.callsign);
    Serial.println(LOG_WARN "Rufzeichen nur für AFU-Betrieb verwenden; im ISM-Betrieb leer lassen.");
    requireRestart();
}

static void handleFactory(const char* in) {
    if (strcmp(in, "J") == 0 || strcmp(in, "j") == 0) {
        config::eraseAllNvs();
        config::resetToDefaults(*cfg_);
        config::saveConfig(*cfg_);
        sync::resetTimeModel();
        Serial.println(LOG_CFG "Werkseinstellungen geladen und gespeichert");
        requireRestart();
        return;
    }
    Serial.println("Abgebrochen – Werte unverändert");
    showMenu();
}

static void handleRestart(const char* in) {
    if (strcmp(in, "J") == 0 || strcmp(in, "j") == 0) {
        cmdReboot();
    }
    Serial.println("Kein Neustart – Normalbetrieb bis zum Neustart gesperrt (Menüpunkt 11)");
    showMenu();
}

static void handleLine(const char* in) {
    // Führende Leerzeichen ignorieren
    while (*in == ' ') in++;

    switch (state) {
        case State::Idle:
            if (*in == '\0') showMenu();
            else handleMenu(in);
            break;
        case State::Menu:          handleMenu(in);     break;
        case State::AwaitFox:      handleFox(in);      break;
        case State::AwaitProfile:  handleProfile(in);  break;
        case State::AwaitCallsign: handleCallsign(in); break;
        case State::AwaitFactory:  handleFactory(in);  break;
        case State::AwaitRestart:  handleRestart(in);  break;
    }
}

void process() {
    if (!cli_active) return;
    uint64_t now = esp_timer_get_time();

    // Rückfrage ohne Antwort: abbrechen, Werte bleiben unverändert
    bool in_prompt = (state != State::Idle && state != State::Menu);
    if (in_prompt && now - prompt_start_us > (uint64_t)CLI_PROMPT_TIMEOUT_MS * 1000ULL) {
        Serial.println();
        if (state == State::AwaitRestart) {
            Serial.println("Zeitüberschreitung – kein Neustart, Normalbetrieb bis zum Neustart gesperrt");
        } else {
            Serial.println("Zeitüberschreitung – abgebrochen, Wert unverändert");
        }
        line_len = 0;
        showMenu();
    }

    while (Serial.available()) {
        char c = (char)Serial.read();
        last_activity_us = esp_timer_get_time();

        // CR, LF oder CRLF beenden die Zeile (LF direkt nach CR ignorieren)
        if (c == '\n' && last_was_cr) {
            last_was_cr = false;
            continue;
        }
        last_was_cr = (c == '\r');
        if (c == '\r' || c == '\n') {
            Serial.println();
            line[line_len] = '\0';
            line_len = 0;
            handleLine(line);
            if (!cli_active) return;
            continue;
        }

        // Backspace / Entf
        if (c == 0x08 || c == 0x7F) {
            if (line_len > 0) {
                line_len--;
                Serial.print("\b \b");
            }
            continue;
        }

        // Nur druckbare ASCII-Zeichen übernehmen
        if (c < 0x20 || c > 0x7E) continue;
        if (line_len < sizeof(line) - 1) {
            line[line_len++] = c;
            Serial.print(c);
        }
    }
}

bool isActive() {
    return cli_active;
}

bool isTimedOut() {
    // Nur Profil D – in den Profilen A–C bestimmt der Slotplan den Start
    if (!cli_active || mode != Mode::Startup || sync::isActive()) return false;
    if (state != State::Idle && state != State::Menu) return false;   // Rückfrage offen

    uint64_t elapsed = esp_timer_get_time() - last_activity_us;
    return (elapsed > (uint64_t)CLI_TIMEOUT_PROFILD_MS * 1000ULL);
}

bool startRequested() {
    return start_requested;
}

bool restartRequired() {
    return restart_required;
}

uint64_t lastActivityUs() {
    return last_activity_us;
}

} // namespace cli
} // namespace ardf
