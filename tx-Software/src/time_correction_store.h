// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// time_correction_store.h – NVS-Speicher für Zeitkorrekturwerte
// ============================================================================

#ifndef ARDF_TIME_CORRECTION_STORE_H
#define ARDF_TIME_CORRECTION_STORE_H

#include "config.h"

namespace ardf {
namespace timecorr {

// Zuletzt gemessene Zeitkorrekturwerte aus NVS laden (nur Anzeige)
bool load(TimeCorrectionData& data);

// Zeitkorrekturwerte in NVS speichern
bool save(const TimeCorrectionData& data);

// Zeitkorrekturwerte im NVS löschen
void clear();

} // namespace timecorr
} // namespace ardf

#endif // ARDF_TIME_CORRECTION_STORE_H
