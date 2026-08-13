// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// time_sync.h – Zeitdrift-Kompensation und Clock-Abgleich
// ============================================================================

#ifndef ARDF_TIME_SYNC_H
#define ARDF_TIME_SYNC_H

#include <cstdint>

namespace ardf {
namespace timesync {

// Zeitmessung-Struktur
struct TimeMeasurement {
    uint64_t local_us;       // Lokaler Empfangszeitpunkt
    uint64_t remote_us;      // Remote-Uptime aus Beacon
    int64_t  offset_us;      // Berechneter Offset
};

// Zeitkorrektur-Zustand
struct TimeSyncState {
    int16_t  drift_ppm;       // Geschätzte Drift in ppm
    int64_t  offset_us;       // Aktueller Offset in µs
    uint8_t  quality;         // Qualität 0..100
    uint16_t meas_count;      // Anzahl Messungen
    bool     valid;           // Daten gültig?
    uint64_t last_update_us;  // Letzter Update-Zeitpunkt
};

// Zeitkorrektur initialisieren (NVS-Werte laden)
void init();

// Neue Messung hinzufügen
void addMeasurement(uint64_t local_rx_us, uint64_t remote_uptime_us);

// Lineare Regression berechnen (nach ≥ 30 Messpunkten)
void computeRegression();

// Aktuellen Zustand abrufen
const TimeSyncState& getState();

// Korrigierte Zeitdifferenz berechnen (für Deep-Sleep-Wakeup)
int64_t getCorrectedOffsetUs();

// Drift in ppm abrufen
int16_t getDriftPpm();

// Qualität abrufen
uint8_t getQuality();

// Vorlaufzeit berechnen (µs)
// elapsed_s: Sekunden seit letztem Sync-Update (oder seit Boot)
uint32_t calculateVorlaufUs(int16_t drift_ppm, uint32_t elapsed_s);

// Werte in NVS speichern
bool saveToNvs();

// Werte aus NVS laden
bool loadFromNvs();

// NVS-Zeitkorrekturwerte löschen
void clearNvs();

// Alle Messwerte zurücksetzen
void reset();

} // namespace timesync
} // namespace ardf

#endif // ARDF_TIME_SYNC_H
