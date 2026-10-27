// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// cli.h – Serielle Kommandozeile (deutschsprachig, nicht blockierend)
// ============================================================================

#ifndef ARDF_CLI_H
#define ARDF_CLI_H

#include <cstdint>
#include "config.h"

namespace ardf {
namespace cli {

enum class Mode : uint8_t {
    Startup,   // Startphase: Konfiguration, Sync-Status, Start
    Error      // Fehlerzustand: Konfiguration korrigieren, dann Neustart
};

// CLI initialisieren; cfg wird bei Änderungen direkt angepasst und gespeichert
void init(FoxConfig& cfg, Mode mode);

// Eingaben verarbeiten (non-blocking, regelmäßig aufrufen)
void process();

// Ist die CLI aktiv?
bool isActive();

// Profil D: Auto-Start nach Inaktivität fällig?
bool isTimedOut();

// Profil D: Normalbetrieb per Menüpunkt 10 angefordert?
bool startRequested();

// Wurde die Konfiguration geändert? Dann muss vor dem Senden neu gestartet werden,
// weil Sync-Zustand und Startphase noch auf der alten Konfiguration beruhen.
bool restartRequired();

// Zeitpunkt der letzten Eingabe (esp_timer, µs)
uint64_t lastActivityUs();

} // namespace cli
} // namespace ardf

#endif // ARDF_CLI_H
