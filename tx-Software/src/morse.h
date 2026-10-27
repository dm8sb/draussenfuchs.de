// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// morse.h – Morse-Kodierung und -Timing nach ITU
// ============================================================================

#ifndef ARDF_MORSE_H
#define ARDF_MORSE_H

#include <cstdint>

namespace ardf {
namespace morse {

// Morse-Element-Typ
enum MorseElement : uint8_t {
    DIT = 0,
    DAH = 1
};

// Morse-Zeichen: Array von Elementen + Länge
struct MorseChar {
    uint8_t elements[8];   // Maximal 8 Elemente (z.B. Ziffern haben 5)
    uint8_t length;        // Anzahl gültiger Elemente
    bool    valid;          // Zeichen bekannt?
};

// Wortabstand (zwischen MO-Wiederholungen und vor dem Rufzeichen) in dits
constexpr uint8_t WORD_GAP_DITS = 7;

// Dit-Dauer in ms berechnen (1200 / WPM)
uint16_t ditDurationMs(uint8_t wpm);

// Morse-Kodierung für ein Zeichen ermitteln
MorseChar encodeMorseChar(char c);

// Berechne die gesamte TX-ON-Zeit (Summe aller Ton-AN-Zeiten) für einen String
// Gibt die reine Ton-AN-Zeit in ms zurück (ohne 20 ms Vorlauf)
uint32_t calculateToneOnTimeMs(const char* text, uint8_t wpm);

// Berechne die gesamte Sendesequenz-Dauer (AN + AUS Zeiten) für einen String
// Gibt die Gesamtdauer in ms zurück (ohne 20 ms Vorlauf)
uint32_t calculateSequenceDurationMs(const char* text, uint8_t wpm);

// Berechne die TX-ON-Zeit für die MO-Kennung(en) inkl. Wiederholungen
// repeats: Anzahl der Wiederholungen (1 = einmal senden, 2 = zweimal senden)
// Gibt die reine Ton-AN-Zeit in ms zurück
uint32_t calculateMoToneOnTimeMs(uint8_t fox_id, uint8_t wpm, uint8_t repeats);

// Berechne Gesamtdauer der MO-Sequenz inkl. Wiederholungen
uint32_t calculateMoSequenceDurationMs(uint8_t fox_id, uint8_t wpm, uint8_t repeats);

// MO-Kennungsstring für Fox-ID erstellen (z.B. "MOE" für Fox 1)
const char* getMoCallsign(uint8_t fox_id);

// Sende eine Morse-Sequenz über den TX-Treiber
// freq_hz: Tonfrequenz, wpm: Geschwindigkeit
void sendMorseString(const char* text, uint16_t freq_hz, uint8_t wpm);

// Sende die komplette ARDF-TX-Sequenz für einen Slot
// Inkl. MO-Kennung(en) und optionalem Callsign
void sendArdfSlot(uint8_t fox_id,
                  const char* callsign, bool send_callsign,
                  uint16_t freq_hz, uint8_t wpm, uint8_t repeats);

} // namespace morse
} // namespace ardf

#endif // ARDF_MORSE_H
