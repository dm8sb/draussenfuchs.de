// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// time_sync.cpp – Zeitdrift-Kompensation Implementierung
// ============================================================================

#include "time_sync.h"
#include "config.h"
#include "time_correction_store.h"
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ardf {
namespace timesync {

// ---- Interner Zustand ----
static TimeSyncState state;

// Messwert-Ringpuffer
static const uint16_t MAX_MEASUREMENTS = 120;
static TimeMeasurement measurements[MAX_MEASUREMENTS];
static uint16_t meas_write_idx = 0;
static uint16_t meas_count = 0;

void init() {
    memset(&state, 0, sizeof(state));
    state.valid = false;
    state.quality = 0;
    state.meas_count = 0;
    meas_write_idx = 0;
    meas_count = 0;

    // NVS-Werte laden als Initialwerte
    TimeCorrectionData nvs_data;
    if (timecorr::load(nvs_data)) {
        state.drift_ppm   = nvs_data.drift_ppm;
        state.offset_us   = nvs_data.offset_us;
        state.quality     = nvs_data.quality;
        state.meas_count  = nvs_data.meas_count;
        state.valid       = true;
        state.last_update_us = esp_timer_get_time();
        Serial.printf(LOG_SYNC "Zeitkorrektur aus NVS: Drift=%d ppm, Qualität=%u\n",
                      state.drift_ppm, state.quality);
    }
}

void addMeasurement(uint64_t local_rx_us, uint64_t remote_uptime_us) {
    TimeMeasurement m;
    m.local_us  = local_rx_us;
    m.remote_us = remote_uptime_us;
    m.offset_us = (int64_t)local_rx_us - (int64_t)remote_uptime_us;

    measurements[meas_write_idx] = m;
    meas_write_idx = (meas_write_idx + 1) % MAX_MEASUREMENTS;
    if (meas_count < MAX_MEASUREMENTS) meas_count++;

    state.meas_count = meas_count;
}

// Hilfsfunktion: Median berechnen
static int64_t calculateMedian(int64_t* values, uint16_t count) {
    // Einfacher Sortier-basierter Median (für kleine Arrays)
    // Kopie sortieren
    for (uint16_t i = 0; i < count - 1; i++) {
        for (uint16_t j = i + 1; j < count; j++) {
            if (values[j] < values[i]) {
                int64_t tmp = values[i];
                values[i] = values[j];
                values[j] = tmp;
            }
        }
    }
    if (count % 2 == 0) {
        return (values[count / 2 - 1] + values[count / 2]) / 2;
    }
    return values[count / 2];
}

void computeRegression() {
    if (meas_count < DRIFT_MIN_SAMPLES) {
        state.quality = (uint8_t)((meas_count * 100) / DRIFT_MIN_SAMPLES);
        if (state.quality > 59) state.quality = 59; // Noch nicht genug Daten
        return;
    }

    // Offset-Werte sammeln
    uint16_t n = meas_count;
    int64_t* offsets = new int64_t[n];
    if (offsets == nullptr) return;

    uint16_t read_idx = 0;
    if (meas_count >= MAX_MEASUREMENTS) {
        read_idx = meas_write_idx; // Ältester Eintrag
    }

    for (uint16_t i = 0; i < n; i++) {
        uint16_t idx = (read_idx + i) % MAX_MEASUREMENTS;
        offsets[i] = measurements[idx].offset_us;
    }

    // Median und MAD berechnen
    int64_t* offsets_copy = new int64_t[n];
    if (offsets_copy == nullptr) { delete[] offsets; return; }
    memcpy(offsets_copy, offsets, n * sizeof(int64_t));

    int64_t median = calculateMedian(offsets_copy, n);

    // MAD (Median Absolute Deviation)
    for (uint16_t i = 0; i < n; i++) {
        offsets_copy[i] = abs((int64_t)(offsets[i] - median));
    }
    int64_t mad = calculateMedian(offsets_copy, n);
    if (mad == 0) mad = 1; // Division durch Null vermeiden

    delete[] offsets_copy;

    // Ausreißer filtern (> 3 * MAD)
    int64_t threshold = 3 * mad;
    uint16_t valid_count = 0;

    // Lineare Regression: y = a + b*x
    // x = lokale Zeit (relativ zum ersten Messwert), y = Offset
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    uint64_t t0 = measurements[read_idx].local_us;

    for (uint16_t i = 0; i < n; i++) {
        uint16_t idx = (read_idx + i) % MAX_MEASUREMENTS;
        int64_t diff = offsets[i] - median;
        if (abs(diff) > threshold) continue; // Ausreißer

        double x = (double)(measurements[idx].local_us - t0);
        double y = (double)offsets[i];
        sum_x  += x;
        sum_y  += y;
        sum_xy += x * y;
        sum_xx += x * x;
        valid_count++;
    }

    delete[] offsets;

    if (valid_count < DRIFT_MIN_SAMPLES / 2) {
        state.quality = 30;
        return;
    }

    // Regressionskoeffizienten
    double n_d = (double)valid_count;
    double denom = n_d * sum_xx - sum_x * sum_x;
    if (fabs(denom) < 1e-10) {
        state.quality = 20;
        return;
    }

    double b = (n_d * sum_xy - sum_x * sum_y) / denom; // Steigung (Drift)
    double a = (sum_y - b * sum_x) / n_d;               // Achsenabschnitt (Offset)

    // Drift in ppm umrechnen (b ist µs/µs = dimensionslos)
    int16_t drift_ppm = (int16_t)(b * 1e6);

    // Plausibilitätsprüfung
    if (abs(drift_ppm) >= DRIFT_PPM_MAX_VALID) {
        Serial.printf(LOG_WARN "Drift-Schätzung unplausibel: %d ppm\n", drift_ppm);
        state.quality = 10;
        return;
    }

    // Qualität schätzen (basierend auf Ausreißer-Rate und Datenmenge)
    uint8_t quality = (uint8_t)((valid_count * 100) / n);
    if (quality > 100) quality = 100;

    // Ergebnis übernehmen
    state.drift_ppm    = drift_ppm;
    state.offset_us    = (int64_t)a;
    state.quality      = quality;
    state.valid        = true;
    state.last_update_us = esp_timer_get_time();

    Serial.printf(LOG_SYNC "Zeitkorrektur berechnet: Drift=%d ppm, Offset=%lld µs, "
                  "Qualität=%u%%, Messungen=%u/%u\n",
                  drift_ppm, (long long)state.offset_us,
                  quality, valid_count, n);
}

const TimeSyncState& getState() {
    return state;
}

int64_t getCorrectedOffsetUs() {
    return state.offset_us;
}

int16_t getDriftPpm() {
    return state.drift_ppm;
}

uint8_t getQuality() {
    return state.quality;
}

uint32_t calculateVorlaufUs(int16_t drift_ppm, uint32_t elapsed_s) {
    // Formel: vorlauf_us = 80000 + |drift_ppm| × elapsed_s + 20000
    // Grenzen: 100000 µs ≤ vorlauf ≤ 350000 µs
    uint32_t drift_component = (uint32_t)abs(drift_ppm) * elapsed_s;
    uint32_t vorlauf = VORLAUF_BASE_US + drift_component + VORLAUF_EXTRA_US;

    if (vorlauf < VORLAUF_MIN_US) vorlauf = VORLAUF_MIN_US;
    if (vorlauf > VORLAUF_MAX_US) vorlauf = VORLAUF_MAX_US;

    return vorlauf;
}

bool saveToNvs() {
    if (!state.valid) return false;

    TimeCorrectionData data;
    data.drift_ppm  = state.drift_ppm;
    data.offset_us  = state.offset_us;
    data.quality    = state.quality;
    data.meas_count = state.meas_count;

    return timecorr::save(data);
}

bool loadFromNvs() {
    TimeCorrectionData data;
    if (!timecorr::load(data)) return false;

    state.drift_ppm    = data.drift_ppm;
    state.offset_us    = data.offset_us;
    state.quality      = data.quality;
    state.meas_count   = data.meas_count;
    state.valid        = true;
    state.last_update_us = esp_timer_get_time();
    return true;
}

void clearNvs() {
    timecorr::clear();
}

void reset() {
    memset(&state, 0, sizeof(state));
    state.valid = false;
    meas_write_idx = 0;
    meas_count = 0;
    Serial.println(LOG_SYNC "Zeitkorrektur zurückgesetzt");
}

} // namespace timesync
} // namespace ardf
