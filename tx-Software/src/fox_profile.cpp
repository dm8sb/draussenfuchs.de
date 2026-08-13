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
        .fox_count    = 5,
        .cycle_ms     = 60000,
        .slot_ms      = 12000,
        .wpm          = 18,
        .morse_repeats = 2,
        .use_espnow   = true,
        .is_continuous = false,
        .tone_on_ms   = 0,
        .tone_off_ms  = 0
    },
    // Profil B – ISM-Sprint – ID 1
    {
        .id           = PROFILE_B_ID,
        .name         = 'B',
        .description  = "ISM-Sprint (30s Zyklus, 20 WPM, 1x MO)",
        .fox_count    = 5,
        .cycle_ms     = 30000,
        .slot_ms      = 6000,
        .wpm          = 20,
        .morse_repeats = 1,
        .use_espnow   = true,
        .is_continuous = false,
        .tone_on_ms   = 0,
        .tone_off_ms  = 0
    },
    // Profil C – ISM-Ultra (Default) – ID 2
    {
        .id           = PROFILE_C_ID,
        .name         = 'C',
        .description  = "ISM-Ultra (20s Zyklus, 24 WPM, 1x MO)",
        .fox_count    = 5,
        .cycle_ms     = 20000,
        .slot_ms      = 4000,
        .wpm          = 24,
        .morse_repeats = 1,
        .use_espnow   = true,
        .is_continuous = false,
        .tone_on_ms   = 0,
        .tone_off_ms  = 0
    },
    // Profil D – draussenfuchs Solo ISM – ID 3
    {
        .id           = PROFILE_D_ID,
        .name         = 'D',
        .description  = "draussenfuchs Solo (Dauerton, kein Sync)",
        .fox_count    = 1,
        .cycle_ms     = 2400,
        .slot_ms      = 2400,
        .wpm          = 0,
        .morse_repeats = 0,
        .use_espnow   = false,
        .is_continuous = true,
        .tone_on_ms   = 200,
        .tone_off_ms  = 2200
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

bool isValidProfileId(uint8_t profile_id) {
    return profile_id < PROFILE_COUNT;
}

uint32_t calculateTxOnTimeMs(uint8_t profile_id, uint8_t fox_id,
                              bool with_callsign, uint8_t callsign_len) {
    const FoxProfile& prof = getProfile(profile_id);

    // Profil D: Dauerton
    if (prof.is_continuous) {
        return prof.tone_on_ms + TX_WARMUP_MS;
    }

    // MO-Kennung Ton-AN-Zeit
    uint32_t tone_on_ms = morse::calculateMoToneOnTimeMs(
        fox_id, prof.wpm, prof.morse_repeats);

    // Callsign hinzufügen (7 dits Pause + Callsign)
    if (with_callsign && callsign_len > 0) {
        const char* cs_ptr = nullptr;
        // Callsign-Ton-AN-Zeit berechnen
        // Wir brauchen den eigentlichen String – übergeben als Parameter
        // Hier nur die Ton-AN-Zeit der MO-Kennung + geschätzter Callsign-Anteil
        uint16_t dit_ms = morse::ditDurationMs(prof.wpm);
        // 7 dits Pause (kein Ton, zählt nicht zur AN-Zeit)
        // Callsign: grobe Schätzung = callsign_len * 4 dits (Durchschnitt)
        tone_on_ms += callsign_len * 4 * dit_ms;
    }

    return tone_on_ms + TX_WARMUP_MS;
}

} // namespace ardf
