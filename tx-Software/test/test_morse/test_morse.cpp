// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_morse.cpp – Unit-Tests für Morse-Kodierung und Timing
// ============================================================================

#include <unity.h>
#include <cstdint>
#include <cstring>

// Getestet wird der echte Produktionscode (src/morse.cpp)
#include "morse.h"

using namespace ardf::morse;

// ---- Tests ----

// Test: Dit-Dauer bei verschiedenen WPM
void test_dit_duration_18wpm(void) {
    TEST_ASSERT_EQUAL_UINT16(66, ditDurationMs(18)); // 1200/18 = 66,67 → abgerundet 66
}

void test_dit_duration_20wpm(void) {
    TEST_ASSERT_EQUAL_UINT16(60, ditDurationMs(20)); // 1200/20 = 60
}

void test_dit_duration_24wpm(void) {
    TEST_ASSERT_EQUAL_UINT16(50, ditDurationMs(24)); // 1200/24 = 50
}

void test_dit_duration_0wpm(void) {
    TEST_ASSERT_EQUAL_UINT16(0, ditDurationMs(0));
}

// Test: Dah = 3 × Dit (Ton-AN von „T" = ein Dah, „E" = ein Dit)
void test_dah_is_3x_dit(void) {
    uint8_t wpm_values[] = {18, 20, 24};
    for (uint8_t i = 0; i < 3; i++) {
        uint16_t dit = ditDurationMs(wpm_values[i]);
        TEST_ASSERT_EQUAL_UINT32(dit, calculateToneOnTimeMs("E", wpm_values[i]));
        TEST_ASSERT_EQUAL_UINT32(3 * dit, calculateToneOnTimeMs("T", wpm_values[i]));
    }
}

// Test: Alle Buchstaben A-Z korrekt kodiert
void test_all_letters_valid(void) {
    for (char c = 'A'; c <= 'Z'; c++) {
        MorseChar mc = encodeMorseChar(c);
        TEST_ASSERT_TRUE_MESSAGE(mc.valid, "Buchstabe sollte gültig sein");
        TEST_ASSERT_TRUE(mc.length >= 1 && mc.length <= 6);
    }
}

// Test: Alle Ziffern 0-9 korrekt kodiert
void test_all_digits_valid(void) {
    for (char c = '0'; c <= '9'; c++) {
        MorseChar mc = encodeMorseChar(c);
        TEST_ASSERT_TRUE_MESSAGE(mc.valid, "Ziffer sollte gültig sein");
        TEST_ASSERT_EQUAL_UINT8(5, mc.length); // Alle Ziffern haben 5 Elemente
    }
}

// Test: Sonderzeichen gültig
void test_special_chars_valid(void) {
    const char specials[] = "/?.,=-";
    for (size_t i = 0; i < strlen(specials); i++) {
        MorseChar mc = encodeMorseChar(specials[i]);
        TEST_ASSERT_TRUE_MESSAGE(mc.valid, "Sonderzeichen sollte gültig sein");
    }
}

// Test: Unbekannte Zeichen werden übersprungen
void test_unknown_chars_invalid(void) {
    MorseChar mc1 = encodeMorseChar('!');
    TEST_ASSERT_FALSE(mc1.valid);
    MorseChar mc2 = encodeMorseChar('@');
    TEST_ASSERT_FALSE(mc2.valid);
    MorseChar mc3 = encodeMorseChar('#');
    TEST_ASSERT_FALSE(mc3.valid);
}

// Test: Kleinbuchstaben werden konvertiert
void test_lowercase_conversion(void) {
    MorseChar mc_upper = encodeMorseChar('A');
    MorseChar mc_lower = encodeMorseChar('a');
    TEST_ASSERT_TRUE(mc_upper.valid);
    TEST_ASSERT_TRUE(mc_lower.valid);
    TEST_ASSERT_EQUAL_UINT8(mc_upper.length, mc_lower.length);
    for (uint8_t i = 0; i < mc_upper.length; i++) {
        TEST_ASSERT_EQUAL_UINT8(mc_upper.elements[i], mc_lower.elements[i]);
    }
}

// Test: Spezifische Morse-Kodierungen (ITU-Referenz)
void test_morse_E(void) {
    MorseChar mc = encodeMorseChar('E');
    TEST_ASSERT_TRUE(mc.valid);
    TEST_ASSERT_EQUAL_UINT8(1, mc.length);
    TEST_ASSERT_EQUAL_UINT8(DIT, mc.elements[0]);
}

void test_morse_T(void) {
    MorseChar mc = encodeMorseChar('T');
    TEST_ASSERT_TRUE(mc.valid);
    TEST_ASSERT_EQUAL_UINT8(1, mc.length);
    TEST_ASSERT_EQUAL_UINT8(DAH, mc.elements[0]);
}

void test_morse_S(void) {
    // S = ...
    MorseChar mc = encodeMorseChar('S');
    TEST_ASSERT_TRUE(mc.valid);
    TEST_ASSERT_EQUAL_UINT8(3, mc.length);
    TEST_ASSERT_EQUAL_UINT8(DIT, mc.elements[0]);
    TEST_ASSERT_EQUAL_UINT8(DIT, mc.elements[1]);
    TEST_ASSERT_EQUAL_UINT8(DIT, mc.elements[2]);
}

void test_morse_O(void) {
    // O = ---
    MorseChar mc = encodeMorseChar('O');
    TEST_ASSERT_TRUE(mc.valid);
    TEST_ASSERT_EQUAL_UINT8(3, mc.length);
    TEST_ASSERT_EQUAL_UINT8(DAH, mc.elements[0]);
    TEST_ASSERT_EQUAL_UINT8(DAH, mc.elements[1]);
    TEST_ASSERT_EQUAL_UINT8(DAH, mc.elements[2]);
}

void test_morse_5(void) {
    // 5 = .....
    MorseChar mc = encodeMorseChar('5');
    TEST_ASSERT_TRUE(mc.valid);
    TEST_ASSERT_EQUAL_UINT8(5, mc.length);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8(DIT, mc.elements[i]);
    }
}

// Test: TX-Zeit-Berechnung für MOE bei 24 WPM
void test_tx_time_moe_24wpm(void) {
    // M = -- (2 dah = 6 dit)
    // O = --- (3 dah = 9 dit)
    // E = . (1 dit)
    // Ton-AN: M(6) + O(9) + E(1) = 16 dits × 50 ms = 800 ms
    uint32_t tone_on = calculateToneOnTimeMs("MOE", 24);
    TEST_ASSERT_EQUAL_UINT32(800, tone_on);
}

// Test: Sequenzdauer für MOE bei 24 WPM
void test_sequence_moe_24wpm(void) {
    // M: dah(3) + pause(1) + dah(3) = 7 dits Dauer
    // Zeichenabstand: 3 dits
    // O: dah(3) + pause(1) + dah(3) + pause(1) + dah(3) = 11 dits
    // Zeichenabstand: 3 dits
    // E: dit(1) = 1 dit
    // Gesamt: 7 + 3 + 11 + 3 + 1 = 25 dits × 50 ms = 1250 ms
    uint32_t seq = calculateSequenceDurationMs("MOE", 24);
    TEST_ASSERT_EQUAL_UINT32(1250, seq);
}

// Test: Wiederholte MO-Kennung = n × Sequenz + (n−1) × 7 dits Wortabstand
// (Sendezeit-Budget inkl. Vorlauf: siehe test_duty)
void test_tx_total_with_warmup(void) {
    uint32_t single = calculateSequenceDurationMs("MOE", 18);
    TEST_ASSERT_EQUAL_UINT32(single, calculateMoSequenceDurationMs(1, 18, 1));
    TEST_ASSERT_EQUAL_UINT32(2 * single + WORD_GAP_DITS * ditDurationMs(18),
                             calculateMoSequenceDurationMs(1, 18, 2));
    TEST_ASSERT_EQUAL_UINT32(2 * calculateToneOnTimeMs("MOE", 18),
                             calculateMoToneOnTimeMs(1, 18, 2));
}

// Test: Wortabstand (7 dits)
void test_word_spacing(void) {
    // "A B" bei 24 WPM (dit = 50 ms)
    // A: dit(1) + dah(3) = 4 dits Ton → Sequenz: dit(1) + pause(1) + dah(3) = 5 dits
    // Wortabstand: 7 dits total (3 Zeichenabstand + 4 extra)
    // B: dah(3) + pause(1) + dit(1) + pause(1) + dit(1) + pause(1) + dit(1) = 9 dits
    // Gesamt: 5 + 4 + 9 = 18 dits (Achtung: nach A kommt kein Zeichenabstand, sondern Wortabstand)
    // Korrekte Berechnung:
    // A = dit(1) + pause(1) + dah(3) = 5 dits
    // Wort-Pause = 4 dits (7 - 3 Zeichenabstand)
    // Aber first_char=true wird nach Space gesetzt, also kein Zeichenabstand vor B
    // B = dah(3) + pause(1) + dit(1) + pause(1) + dit(1) + pause(1) + dit(1) = 9 dits
    // Total = 5 + 4 + 9 = 18 dits × 50 ms = 900 ms
    uint32_t seq = calculateSequenceDurationMs("A B", 24);
    TEST_ASSERT_EQUAL_UINT32(900, seq);
}

// Test: Zeichenabstand (3 dits)
void test_char_spacing(void) {
    // "AB" bei 24 WPM
    // A: dit(1) + pause(1) + dah(3) = 5 dits
    // Zeichenabstand: 3 dits
    // B: dah(3) + pause(1) + dit(1) + pause(1) + dit(1) + pause(1) + dit(1) = 9 dits
    // Total: 5 + 3 + 9 = 17 dits × 50 ms = 850 ms
    uint32_t seq = calculateSequenceDurationMs("AB", 24);
    TEST_ASSERT_EQUAL_UINT32(850, seq);

    // Nur A
    uint32_t seq_a = calculateSequenceDurationMs("A", 24);
    // A: 5 dits × 50 = 250 ms
    TEST_ASSERT_EQUAL_UINT32(250, seq_a);
}

// Test: MO-Kennungen existieren für alle Fox-IDs
void test_mo_callsigns(void) {
    TEST_ASSERT_EQUAL_STRING("MOE", getMoCallsign(1));
    TEST_ASSERT_EQUAL_STRING("MOI", getMoCallsign(2));
    TEST_ASSERT_EQUAL_STRING("MOS", getMoCallsign(3));
    TEST_ASSERT_EQUAL_STRING("MOH", getMoCallsign(4));
    TEST_ASSERT_EQUAL_STRING("MO5", getMoCallsign(5));
}

// Test: Sequenzdauer MO5 bei 24 WPM (längste Kennung)
void test_sequence_mo5_24wpm(void) {
    // M: dah(3)+p(1)+dah(3) = 7
    // Zeichenabstand: 3
    // O: dah(3)+p(1)+dah(3)+p(1)+dah(3) = 11
    // Zeichenabstand: 3
    // 5: dit+p+dit+p+dit+p+dit+p+dit = 9
    // Total: 7 + 3 + 11 + 3 + 9 = 33 dits × 50 = 1650 ms
    uint32_t seq = calculateSequenceDurationMs("MO5", 24);
    TEST_ASSERT_EQUAL_UINT32(1650, seq);
}

// Test: Sequenzdauer MOE kürzer als MO5
void test_moe_shorter_than_mo5(void) {
    uint32_t moe = calculateSequenceDurationMs("MOE", 24);
    uint32_t mo5 = calculateSequenceDurationMs("MO5", 24);
    TEST_ASSERT_TRUE(moe < mo5);
}

// Test: Leerer String
void test_empty_string(void) {
    TEST_ASSERT_EQUAL_UINT32(0, calculateToneOnTimeMs("", 24));
    TEST_ASSERT_EQUAL_UINT32(0, calculateSequenceDurationMs("", 24));
}

// Test: NULL-String
void test_null_string(void) {
    TEST_ASSERT_EQUAL_UINT32(0, calculateToneOnTimeMs(nullptr, 24));
    TEST_ASSERT_EQUAL_UINT32(0, calculateSequenceDurationMs(nullptr, 24));
}

// ---- Hauptfunktion ----

void setUp(void) {}
void tearDown(void) {}


int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_dit_duration_18wpm);
    RUN_TEST(test_dit_duration_20wpm);
    RUN_TEST(test_dit_duration_24wpm);
    RUN_TEST(test_dit_duration_0wpm);
    RUN_TEST(test_dah_is_3x_dit);
    RUN_TEST(test_all_letters_valid);
    RUN_TEST(test_all_digits_valid);
    RUN_TEST(test_special_chars_valid);
    RUN_TEST(test_unknown_chars_invalid);
    RUN_TEST(test_lowercase_conversion);
    RUN_TEST(test_morse_E);
    RUN_TEST(test_morse_T);
    RUN_TEST(test_morse_S);
    RUN_TEST(test_morse_O);
    RUN_TEST(test_morse_5);
    RUN_TEST(test_tx_time_moe_24wpm);
    RUN_TEST(test_sequence_moe_24wpm);
    RUN_TEST(test_tx_total_with_warmup);
    RUN_TEST(test_word_spacing);
    RUN_TEST(test_char_spacing);
    RUN_TEST(test_mo_callsigns);
    RUN_TEST(test_sequence_mo5_24wpm);
    RUN_TEST(test_moe_shorter_than_mo5);
    RUN_TEST(test_empty_string);
    RUN_TEST(test_null_string);

    return UNITY_END();
}
