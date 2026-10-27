// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// morse.cpp – Morse-Kodierung und -Timing Implementierung
// (hardwareunabhängig, nativ testbar; Aussendung in morse_tx.cpp)
// ============================================================================

#include "morse.h"
#include "config.h"
#include <cstring>

namespace ardf {
namespace morse {

// ---- Morse-Tabelle (ITU) ----
// Kodierung: 0 = dit, 1 = dah
struct MorseEntry {
    char    ch;
    uint8_t elements[8];
    uint8_t length;
};

static const MorseEntry morse_table[] = {
    {'A', {0,1},             2},
    {'B', {1,0,0,0},         4},
    {'C', {1,0,1,0},         4},
    {'D', {1,0,0},           3},
    {'E', {0},               1},
    {'F', {0,0,1,0},         4},
    {'G', {1,1,0},           3},
    {'H', {0,0,0,0},         4},
    {'I', {0,0},             2},
    {'J', {0,1,1,1},         4},
    {'K', {1,0,1},           3},
    {'L', {0,1,0,0},         4},
    {'M', {1,1},             2},
    {'N', {1,0},             2},
    {'O', {1,1,1},           3},
    {'P', {0,1,1,0},         4},
    {'Q', {1,1,0,1},         4},
    {'R', {0,1,0},           3},
    {'S', {0,0,0},           3},
    {'T', {1},               1},
    {'U', {0,0,1},           3},
    {'V', {0,0,0,1},         4},
    {'W', {0,1,1},           3},
    {'X', {1,0,0,1},         4},
    {'Y', {1,0,1,1},         4},
    {'Z', {1,1,0,0},         4},
    {'0', {1,1,1,1,1},       5},
    {'1', {0,1,1,1,1},       5},
    {'2', {0,0,1,1,1},       5},
    {'3', {0,0,0,1,1},       5},
    {'4', {0,0,0,0,1},       5},
    {'5', {0,0,0,0,0},       5},
    {'6', {1,0,0,0,0},       5},
    {'7', {1,1,0,0,0},       5},
    {'8', {1,1,1,0,0},       5},
    {'9', {1,1,1,1,0},       5},
    {'/', {1,0,0,1,0},       5},
    {'?', {0,0,1,1,0,0},     6},
    {'.', {0,1,0,1,0,1},     6},
    {',', {1,1,0,0,1,1},     6},
    {'=', {1,0,0,0,1},       5},
    {'-', {1,0,0,0,0,1},     6},
};

static const uint8_t MORSE_TABLE_SIZE = sizeof(morse_table) / sizeof(morse_table[0]);

// ---- MO-Kennungen ----
static const char* mo_callsigns[] = {
    "MOE",   // Fox 1
    "MOI",   // Fox 2
    "MOS",   // Fox 3
    "MOH",   // Fox 4
    "MO5"    // Fox 5
};

uint16_t ditDurationMs(uint8_t wpm) {
    if (wpm == 0) return 0;
    return 1200 / wpm;
}

MorseChar encodeMorseChar(char c) {
    MorseChar result;
    memset(&result, 0, sizeof(result));
    result.valid = false;

    // Kleinbuchstaben → Großbuchstaben
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    for (uint8_t i = 0; i < MORSE_TABLE_SIZE; i++) {
        if (morse_table[i].ch == c) {
            memcpy(result.elements, morse_table[i].elements, morse_table[i].length);
            result.length = morse_table[i].length;
            result.valid = true;
            return result;
        }
    }

    // Unbekanntes Zeichen (valid = false, wird beim Senden übersprungen;
    // Rufzeichen werden bereits bei der Eingabe auf gültige Zeichen geprüft)
    return result;
}

const char* getMoCallsign(uint8_t fox_id) {
    if (fox_id >= 1 && fox_id <= MAX_FOXES) {
        return mo_callsigns[fox_id - 1];
    }
    return "MOE"; // Fallback
}

// Berechne Ton-AN-Zeit für einen einzelnen String (nur Ton-AN-Anteile)
uint32_t calculateToneOnTimeMs(const char* text, uint8_t wpm) {
    if (wpm == 0 || text == nullptr) return 0;

    uint16_t dit_ms = ditDurationMs(wpm);
    uint32_t tone_on_ms = 0;

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == ' ') continue; // Leerzeichen zählen nicht als Ton

        MorseChar mc = encodeMorseChar(text[i]);
        if (!mc.valid) continue;

        for (uint8_t e = 0; e < mc.length; e++) {
            if (mc.elements[e] == DIT) {
                tone_on_ms += dit_ms;
            } else {
                tone_on_ms += 3 * dit_ms; // dah = 3 × dit
            }
        }
    }

    return tone_on_ms;
}

// Berechne Gesamtdauer einer Morse-Sequenz (AN + AUS Zeiten)
uint32_t calculateSequenceDurationMs(const char* text, uint8_t wpm) {
    if (wpm == 0 || text == nullptr) return 0;

    uint16_t dit_ms = ditDurationMs(wpm);
    uint32_t total_ms = 0;
    bool first_char = true;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];

        if (c == ' ') {
            // Wortabstand = 7 dits (aber 3 dits Zeichenabstand nach letztem Zeichen
            // waren bereits gezählt, also nur 4 dits zusätzlich)
            total_ms += 4 * dit_ms;
            first_char = true;
            continue;
        }

        MorseChar mc = encodeMorseChar(c);
        if (!mc.valid) continue;

        // Zeichenabstand vor dem Zeichen (außer beim ersten)
        if (!first_char) {
            total_ms += 3 * dit_ms;
        }
        first_char = false;

        for (uint8_t e = 0; e < mc.length; e++) {
            // Inter-Element-Pause vor jedem Element (außer dem ersten)
            if (e > 0) {
                total_ms += dit_ms;
            }
            // Element-Dauer
            if (mc.elements[e] == DIT) {
                total_ms += dit_ms;
            } else {
                total_ms += 3 * dit_ms;
            }
        }
    }

    return total_ms;
}

uint32_t calculateMoToneOnTimeMs(uint8_t fox_id, uint8_t wpm, uint8_t repeats) {
    if (repeats == 0 || wpm == 0) return 0;

    const char* mo = getMoCallsign(fox_id);
    uint32_t single_on = calculateToneOnTimeMs(mo, wpm);

    return single_on * repeats;
}

uint32_t calculateMoSequenceDurationMs(uint8_t fox_id, uint8_t wpm, uint8_t repeats) {
    if (repeats == 0 || wpm == 0) return 0;

    const char* mo = getMoCallsign(fox_id);
    uint16_t dit_ms = ditDurationMs(wpm);
    uint32_t single_duration = calculateSequenceDurationMs(mo, wpm);

    if (repeats == 1) {
        return single_duration;
    }

    // Mehrere Wiederholungen: Pause zwischen Wiederholungen = 7 dits
    uint32_t total = single_duration;
    for (uint8_t r = 1; r < repeats; r++) {
        total += WORD_GAP_DITS * dit_ms; // Wortabstand
        total += single_duration;
    }

    return total;
}


} // namespace morse
} // namespace ardf
