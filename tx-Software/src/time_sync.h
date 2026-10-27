// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// time_sync.h – Zeitmodell: Umrechnung Master-Uhr → lokale Uhr
// (hardwareunabhängig, nativ testbar)
// ============================================================================

#ifndef ARDF_TIME_SYNC_H
#define ARDF_TIME_SYNC_H

#include <cstdint>
#include "config.h"

namespace ardf {
namespace timesync {

// Lineares Modell lokal = ref + a + b·(ref − x0), geschätzt aus Paaren
// (Sendezeit in der Referenzuhr, lokaler Empfangszeitpunkt).
// Ausreißer werden per Median/MAD verworfen. Ohne Messwerte: Identität.
class TimeModel {
public:
    void clear();

    // Messpunkt hinzufügen (Ringpuffer, älteste Werte fallen heraus)
    void add(uint64_t ref_us, uint64_t local_us);

    // Modell neu schätzen
    void compute();

    // Referenzzeit → lokale Zeit
    uint64_t toLocal(uint64_t ref_us) const;

    uint16_t samples() const   { return count_; }
    uint16_t inliers() const   { return inliers_; }
    bool     hasDrift() const  { return has_drift_; }
    float    driftPpm() const  { return (float)(b_ * 1e6); }
    int64_t  offsetUs() const  { return a_; }
    uint8_t  quality() const   { return quality_; }

private:
    uint64_t ref_[TIME_MAX_SAMPLES];
    uint64_t local_[TIME_MAX_SAMPLES];
    uint16_t count_ = 0;
    uint16_t write_idx_ = 0;

    int64_t  a_ = 0;        // Offset in µs
    double   b_ = 0.0;      // Drift (dimensionslos)
    uint64_t x0_ = 0;       // Bezugspunkt der Steigung
    uint16_t inliers_ = 0;
    bool     has_drift_ = false;
    uint8_t  quality_ = 0;
};

} // namespace timesync
} // namespace ardf

#endif // ARDF_TIME_SYNC_H
