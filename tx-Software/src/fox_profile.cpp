// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// fox_profile.cpp – ARDF-Profil-Definitionen Implementierung
// ============================================================================

#include "fox_profile.h"
#include "config.h"
#include "morse.h"

namespace ardf {

// ---- Profiltabelle ----
static const FoxProfile profiles[] = {
    // Profil A – ISM-Standard – ID 0
    {
        .id           = PROFILE_A_ID,
        .name         = 'A',
        .description  = "ISM-Standard (60s Zyklus, 18 WPM, 2x MO)",
        .cycle_ms     = 60000,
        .slot_ms      = 12000,
        .wpm          = 18,
        .morse_repeats = 2,
        .use_espnow   = true,
        .is_continuous = false,
        .tone_on_ms   = 0
    },
    // Profil B – ISM-Sprint – ID 1
    {
        .id           = PROFILE_B_ID,
        .name         = 'B',
        .description  = "ISM-Sprint (30s Zyklus, 20 WPM, 1x MO)",
        .cycle_ms     = 30000,
        .slot_ms      = 6000,
        .wpm          = 20,
        .morse_repeats = 1,
        .use_espnow   = true,
        .is_continuous = false,
        .tone_on_ms   = 0
    },
    // Profil C – ISM-Ultra (Default) – ID 2
    {
        .id           = PROFILE_C_ID,
        .name         = 'C',
        .description  = "ISM-Ultra (20s Zyklus, 24 WPM, 1x MO)",
        .cycle_ms     = 20000,
        .slot_ms      = 4000,
        .wpm          = 24,
        .morse_repeats = 1,
        .use_espnow   = true,
        .is_continuous = false,
        .tone_on_ms   = 0
    },
    // Profil D – draussenfuchs Solo ISM – ID 3
    {
        .id           = PROFILE_D_ID,
        .name         = 'D',
        .description  = "draussenfuchs Solo (Dauerton, kein Sync)",
        .cycle_ms     = 2400,
        .slot_ms      = 2400,
        .wpm          = 0,
        .morse_repeats = 0,
        .use_espnow   = false,
        .is_continuous = true,
        .tone_on_ms   = 200
    }
};

static const uint8_t PROFILE_COUNT = sizeof(profiles) / sizeof(profiles[0]);

const FoxProfile& getProfile(uint8_t profile_id) {
    if (profile_id < PROFILE_COUNT) {
        return profiles[profile_id];
    }
    // Fallback: Profil C
    return profiles[PROFILE_C_ID];
}

char getProfileChar(uint8_t profile_id) {
    if (profile_id < PROFILE_COUNT) {
        return profiles[profile_id].name;
    }
    return '?';
}

uint8_t getProfileIdByChar(char c) {
    // Großbuchstaben-Konvertierung
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        if (profiles[i].name == c) return i;
    }
    return 0xFF; // Ungültig
}

uint32_t txBudgetMs(uint8_t profile_id, uint8_t fox_id, const char* callsign) {
    const FoxProfile& prof = getProfile(profile_id);

    // Profil D: Dauerton
    if (prof.is_continuous) {
        return TX_WARMUP_MS + prof.tone_on_ms;
    }

    // MO-Kennung(en) inkl. Pausen zwischen den Wiederholungen
    uint32_t seq_ms = morse::calculateMoSequenceDurationMs(
        fox_id, prof.wpm, prof.morse_repeats);

    // Rufzeichen-Slot: 7 dits Pause + Rufzeichen
    if (callsign != nullptr && callsign[0] != '\0') {
        seq_ms += morse::WORD_GAP_DITS * morse::ditDurationMs(prof.wpm);
        seq_ms += morse::calculateSequenceDurationMs(callsign, prof.wpm);
    }

    return TX_WARMUP_MS + seq_ms;
}

} // namespace ardf
