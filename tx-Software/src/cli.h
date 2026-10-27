// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// cli.h – Serielle Kommandozeile (deutschsprachig)
// ============================================================================

#ifndef ARDF_CLI_H
#define ARDF_CLI_H

#include <cstdint>

namespace ardf {
namespace cli {

// CLI initialisieren
void init();

// CLI-Verarbeitung (non-blocking, im Loop aufrufen)
// Gibt true zurück, wenn der Normalbetrieb gestartet werden soll
bool process();

// Menü anzeigen
void showMenu();

// Ist die CLI aktiv?
bool isActive();

// CLI-Timeout (Auto-Start) prüfen
bool isTimedOut();

// Inaktivitäts-Timer zurücksetzen
void resetIdleTimer();

// Wurde die Konfiguration geändert? Dann muss vor dem Senden neu gestartet werden,
// weil Sync-Zustand und Startphase noch auf der alten Konfiguration beruhen.
bool restartRequired();

} // namespace cli
} // namespace ardf

#endif // ARDF_CLI_H
