// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// time_sync.cpp – Zeitmodell Implementierung
// ============================================================================

#include "time_sync.h"
#include <cmath>

namespace ardf {
namespace timesync {

// Median eines Arrays (sortiert das Array in-place, n ≤ TIME_MAX_SAMPLES)
static int64_t median(int64_t* v, uint16_t n) {
    for (uint16_t i = 1; i < n; i++) {
        int64_t x = v[i];
        int16_t j = (int16_t)i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    if (n % 2 == 0) {
        return (v[n / 2 - 1] + v[n / 2]) / 2;
    }
    return v[n / 2];
}

void TimeModel::clear() {
    count_ = 0;
    write_idx_ = 0;
    a_ = 0;
    b_ = 0.0;
    x0_ = 0;
    inliers_ = 0;
    has_drift_ = false;
    quality_ = 0;
}

void TimeModel::add(uint64_t ref_us, uint64_t local_us) {
    ref_[write_idx_] = ref_us;
    local_[write_idx_] = local_us;
    write_idx_ = (write_idx_ + 1) % TIME_MAX_SAMPLES;
    if (count_ < TIME_MAX_SAMPLES) count_++;
}

void TimeModel::compute() {
    has_drift_ = false;
    b_ = 0.0;
    if (count_ == 0) {
        a_ = 0;
        inliers_ = 0;
        quality_ = 0;
        return;
    }

    // Offsets und robuste Mitte
    static int64_t offs[TIME_MAX_SAMPLES];
    static int64_t work[TIME_MAX_SAMPLES];
    for (uint16_t i = 0; i < count_; i++) {
        offs[i] = (int64_t)local_[i] - (int64_t)ref_[i];
        work[i] = offs[i];
    }
    int64_t med = median(work, count_);
    for (uint16_t i = 0; i < count_; i++) {
        work[i] = (offs[i] > med) ? offs[i] - med : med - offs[i];
    }
    int64_t threshold = 3 * median(work, count_);
    if (threshold < (int64_t)TIME_OUTLIER_MIN_US) threshold = TIME_OUTLIER_MIN_US;

    // Bezugspunkt: kleinste Referenzzeit
    uint64_t x0 = ref_[0];
    for (uint16_t i = 1; i < count_; i++) {
        if (ref_[i] < x0) x0 = ref_[i];
    }

    // Lineare Regression (OLS) über die Inlier, y relativ zum Median
    double sx = 0, sy = 0, sxy = 0, sxx = 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < count_; i++) {
        int64_t dev = offs[i] - med;
        if (dev > threshold || dev < -threshold) continue;
        double x = (double)(ref_[i] - x0);
        double y = (double)dev;
        sx += x; sy += y; sxy += x * y; sxx += x * x;
        n++;
    }
    inliers_ = n;
    x0_ = x0;
    a_ = med;

    if (n < DRIFT_MIN_SAMPLES) {
        // Zu wenig Daten: nur Offset (Median), keine Drift
        uint32_t q = (uint32_t)count_ * 100 / DRIFT_MIN_SAMPLES;
        quality_ = (uint8_t)(q > 59 ? 59 : q);
        return;
    }

    double denom = (double)n * sxx - sx * sx;
    if (std::fabs(denom) < 1e-6) {
        quality_ = 20;
        return;
    }
    double b = ((double)n * sxy - sx * sy) / denom;
    double a = (sy - b * sx) / (double)n;

    if (std::fabs(b * 1e6) >= DRIFT_PPM_MAX_VALID) {
        // Unplausible Steigung: nur Offset verwenden
        quality_ = 10;
        return;
    }

    a_ = med + (int64_t)std::llround(a);
    b_ = b;
    has_drift_ = true;
    quality_ = (uint8_t)((uint32_t)n * 100 / count_);
}

uint64_t TimeModel::toLocal(uint64_t ref_us) const {
    double drift = 0.0;
    if (has_drift_) {
        drift = b_ * ((double)ref_us - (double)x0_);
    }
    return (uint64_t)((int64_t)ref_us + a_ + (int64_t)std::llround(drift));
}

} // namespace timesync
} // namespace ardf
