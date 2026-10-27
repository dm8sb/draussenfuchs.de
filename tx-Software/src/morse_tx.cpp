// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// morse_tx.cpp – Morse-Aussendung über den TX-Treiber
// ============================================================================

#include "morse.h"
#include "config.h"
#include "tx_driver.h"
#include <Arduino.h>

namespace ardf {
namespace morse {

// ---- TX-Funktionen ----

void sendMorseString(const char* text, uint16_t freq_hz, uint8_t wpm) {
    if (wpm == 0 || text == nullptr) return;

    uint16_t dit_ms = ditDurationMs(wpm);
    bool first_char = true;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];

        if (c == ' ') {
            // Wortabstand: 7 dits total, 3 bereits durch Zeichenabstand
            delay(4 * dit_ms);
            first_char = true;
            continue;
        }

        MorseChar mc = encodeMorseChar(c);
        if (!mc.valid) continue;

        // Zeichenabstand (außer erstes Zeichen)
        if (!first_char) {
            delay(3 * dit_ms);
        }
        first_char = false;

        for (uint8_t e = 0; e < mc.length; e++) {
            // Inter-Element-Pause
            if (e > 0) {
                tx::toneOff();
                delay(dit_ms);
            }

            // Element senden
            tx::toneOn(freq_hz);
            if (mc.elements[e] == DIT) {
                delay(dit_ms);
            } else {
                delay(3 * dit_ms);
            }
        }
        tx::toneOff();
    }
}

void sendArdfSlot(uint8_t fox_id,
                  const char* callsign, bool send_callsign,
                  uint16_t freq_hz, uint8_t wpm, uint8_t repeats) {
    if (repeats == 0) return; // Profil D: kein Morse

    const char* mo = getMoCallsign(fox_id);
    uint16_t dit_ms = ditDurationMs(wpm);

    // MO-Kennung senden (repeats-mal)
    for (uint8_t r = 0; r < repeats; r++) {
        if (r > 0) {
            // Pause zwischen Wiederholungen: 7 dits
            delay(WORD_GAP_DITS * dit_ms);
        }
        sendMorseString(mo, freq_hz, wpm);
    }

    // Callsign senden (wenn gewünscht)
    if (send_callsign && callsign != nullptr && callsign[0] != '\0') {
        // 7 dits Pause vor dem Callsign
        delay(WORD_GAP_DITS * dit_ms);
        sendMorseString(callsign, freq_hz, wpm);
    }
}

} // namespace morse
} // namespace ardf
