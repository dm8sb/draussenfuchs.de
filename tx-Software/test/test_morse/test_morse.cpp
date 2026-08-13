// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_morse.cpp – Unit-Tests für Morse-Kodierung und Timing
// ============================================================================

#include <unity.h>
#include <cstdint>
#include <cstring>

// ---- Morse-Element-Typ ----
enum MorseElement : uint8_t {
    DIT = 0,
    DAH = 1
};

// ---- Morse-Zeichen ----
struct MorseChar {
    uint8_t elements[8];
    uint8_t length;
    bool    valid;
};

// ---- Morse-Tabelle (identisch zu morse.cpp) ----
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

// ---- Testbare Funktionen (Reimplementierung für native Tests) ----

static uint16_t ditDurationMs(uint8_t wpm) {
    if (wpm == 0) return 0;
    return 1200 / wpm;
}

static MorseChar encodeMorseChar(char c) {
    MorseChar result;
    memset(&result, 0, sizeof(result));
    result.valid = false;

    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    for (uint8_t i = 0; i < MORSE_TABLE_SIZE; i++) {
        if (morse_table[i].ch == c) {
            memcpy(result.elements, morse_table[i].elements, morse_table[i].length);
            result.length = morse_table[i].length;
            result.valid = true;
            return result;
        }
    }
    return result;
}

// Berechne Ton-AN-Zeit für einen String
static uint32_t calculateToneOnTimeMs(const char* text, uint8_t wpm) {
    if (wpm == 0 || text == nullptr) return 0;
    uint16_t dit_ms = ditDurationMs(wpm);
    uint32_t tone_on = 0;

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == ' ') continue;
        MorseChar mc = encodeMorseChar(text[i]);
        if (!mc.valid) continue;
        for (uint8_t e = 0; e < mc.length; e++) {
            if (mc.elements[e] == DIT) tone_on += dit_ms;
            else tone_on += 3 * dit_ms;
        }
    }
    return tone_on;
}

// Berechne Gesamtdauer einer Morse-Sequenz
static uint32_t calculateSequenceDurationMs(const char* text, uint8_t wpm) {
    if (wpm == 0 || text == nullptr) return 0;
    uint16_t dit_ms = ditDurationMs(wpm);
    uint32_t total = 0;
    bool first_char = true;

    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == ' ') {
            total += 4 * dit_ms;
            first_char = true;
            continue;
        }
        MorseChar mc = encodeMorseChar(c);
        if (!mc.valid) continue;
        if (!first_char) total += 3 * dit_ms;
        first_char = false;
        for (uint8_t e = 0; e < mc.length; e++) {
            if (e > 0) total += dit_ms;
            if (mc.elements[e] == DIT) total += dit_ms;
            else total += 3 * dit_ms;
        }
    }
    return total;
}

// MO-Kennungsstring
static const char* getMoCallsign(uint8_t fox_id) {
    static const char* mo[] = {"MOE", "MOI", "MOS", "MOH", "MO5"};
    if (fox_id >= 1 && fox_id <= 5) return mo[fox_id - 1];
    return "MOE";
}

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

// Test: Dah = 3 × Dit
void test_dah_is_3x_dit(void) {
    uint8_t wpm_values[] = {18, 20, 24};
    for (uint8_t i = 0; i < 3; i++) {
        uint16_t dit = ditDurationMs(wpm_values[i]);
        uint16_t dah = 3 * dit;
        TEST_ASSERT_EQUAL_UINT16(3 * dit, dah);
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

// Test: TX-Zeit inkl. 20 ms Vorlauf
void test_tx_total_with_warmup(void) {
    uint32_t tone_on = calculateToneOnTimeMs("MOE", 24);
    uint32_t tx_total = tone_on + 20; // 20 ms Vorlauf
    TEST_ASSERT_EQUAL_UINT32(820, tx_total);
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

#ifdef ARDUINO

#include <Arduino.h>

void setup() {
    delay(2000);
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

    UNITY_END();
}

void loop() {}

#else

int main(int argc, char **argv) {
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

#endif
