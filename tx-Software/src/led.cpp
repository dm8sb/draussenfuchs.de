// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// led.cpp – Nicht-blockierende LED-Muster
// ============================================================================

#include "led.h"
#include "tx_driver.h"
#include <Arduino.h>

namespace ardf {
namespace led {

// Muster als Folge von Dauern in ms: an, aus, an, aus, … (wiederholt)
static const uint16_t SYNC[]     = {250, 250};
static const uint16_t IDLE[]     = {500, 1500};
// SOS: ··· — — — ··· (Dit 150 ms, Dah 450 ms, Zeichenabstand 450 ms), Pause 1,2 s
static const uint16_t SOS[]      = {150, 150, 150, 150, 150, 450,
                                    450, 150, 450, 150, 450, 450,
                                    150, 150, 150, 150, 150, 1350};
static const uint16_t MISMATCH[] = {150, 150, 150, 150, 150, 1150};
static const uint16_t INVALID[]  = {50, 50};
static const uint16_t BLOCKED[]  = {150, 150, 150, 2150};

struct Sequence {
    const uint16_t* steps;
    uint8_t count;
};

static Sequence sequenceFor(Pattern p) {
    switch (p) {
        case Pattern::Sync:        return {SYNC, sizeof(SYNC) / sizeof(SYNC[0])};
        case Pattern::Idle:        return {IDLE, sizeof(IDLE) / sizeof(IDLE[0])};
        case Pattern::Sos:         return {SOS, sizeof(SOS) / sizeof(SOS[0])};
        case Pattern::Mismatch:    return {MISMATCH, sizeof(MISMATCH) / sizeof(MISMATCH[0])};
        case Pattern::Invalid:     return {INVALID, sizeof(INVALID) / sizeof(INVALID[0])};
        case Pattern::DutyBlocked: return {BLOCKED, sizeof(BLOCKED) / sizeof(BLOCKED[0])};
        default:                   return {nullptr, 0};
    }
}

static Pattern current = Pattern::Off;
static Sequence seq = {nullptr, 0};
static uint8_t step = 0;
static uint32_t step_start_ms = 0;

static void apply() {
    // Gerade Schritte: an, ungerade: aus
    if (seq.count > 0 && step % 2 == 0) tx::ledOn();
    else tx::ledOff();
}

void set(Pattern p) {
    if (p == current) return;
    current = p;
    seq = sequenceFor(p);
    step = 0;
    step_start_ms = millis();
    apply();
}

void update() {
    if (seq.count == 0) return;
    uint32_t now = millis();
    while (now - step_start_ms >= seq.steps[step]) {
        step_start_ms += seq.steps[step];
        step = (uint8_t)((step + 1) % seq.count);
        apply();
    }
}

} // namespace led
} // namespace ardf
