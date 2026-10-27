// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// test_config.cpp – Unit-Tests für Config-Hash, Validierung und CRC
// ============================================================================

#include <unity.h>
#include <cstdint>
#include <cstring>

// Getestet wird der echte Produktionscode (src/config_check.cpp)
#include "config.h"
#include "config_store.h"
#include "fox_profile.h"

using namespace ardf;

static uint32_t hashFor(uint8_t profile_id, bool callsign_empty) {
    const FoxProfile& p = getProfile(profile_id);
    return config::calculateConfigHash(profile_id, p.slot_ms, p.wpm,
                                       callsign_empty, p.morse_repeats);
}

// ---- Config-Hash ----

// Der Hash enthält nur verbundweite Parameter – keine Fox-ID. Füchse mit
// gleichem Profil müssen sich deshalb gegenseitig als kompatibel erkennen.
void test_hash_same_profile_is_equal(void) {
    TEST_ASSERT_EQUAL_HEX32(hashFor(PROFILE_C_ID, true), hashFor(PROFILE_C_ID, true));
}

void test_hash_differs_by_profile(void) {
    TEST_ASSERT_NOT_EQUAL(hashFor(PROFILE_A_ID, true), hashFor(PROFILE_B_ID, true));
    TEST_ASSERT_NOT_EQUAL(hashFor(PROFILE_B_ID, true), hashFor(PROFILE_C_ID, true));
}

void test_hash_differs_by_afu_mode(void) {
    TEST_ASSERT_NOT_EQUAL(hashFor(PROFILE_C_ID, true), hashFor(PROFILE_C_ID, false));
}

// ---- Validierung ----

void test_defaults_are_valid(void) {
    FoxConfig cfg;
    config::resetToDefaults(cfg);
    TEST_ASSERT_TRUE(config::isValidConfig(cfg));
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_FOX_ID, cfg.fox_id);
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_FOX_CYCLE_PROFILE, cfg.profile_id);
    TEST_ASSERT_EQUAL_CHAR('\0', cfg.callsign[0]);
}

void test_invalid_fox_id_rejected(void) {
    FoxConfig cfg;
    config::resetToDefaults(cfg);
    cfg.fox_id = 6;
    TEST_ASSERT_FALSE(config::isValidConfig(cfg));
}

// ---- CRC-32 (Befund 3.1) ----

// Standard-Testvektor der CRC-32 (IEEE 802.3)
void test_crc32_check_value(void) {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, config::crc32(reinterpret_cast<const uint8_t*>(s), 9));
    TEST_ASSERT_EQUAL_HEX32(0x00000000, config::crc32(nullptr, 0));
}

void test_crc_detects_change(void) {
    FoxConfig cfg;
    config::resetToDefaults(cfg);
    cfg.profile_id = PROFILE_A_ID;   // ohne neue CRC
    TEST_ASSERT_FALSE(config::isValidConfig(cfg));
    config::updateCrc(cfg);
    TEST_ASSERT_TRUE(config::isValidConfig(cfg));
}

void test_wrong_version_rejected(void) {
    FoxConfig cfg;
    config::resetToDefaults(cfg);
    cfg.version = CONFIG_VERSION_LEGACY;
    config::updateCrc(cfg);
    TEST_ASSERT_FALSE(config::isValidConfig(cfg));
    TEST_ASSERT_TRUE(config::isValidFields(cfg));   // Übernahme aus v2 möglich
}

// Rufzeichen ohne Nullterminator oder mit unerlaubten Zeichen
void test_callsign_fields(void) {
    FoxConfig cfg;
    config::resetToDefaults(cfg);
    strcpy(cfg.callsign, "DL1ABC/P");
    TEST_ASSERT_TRUE(config::isValidFields(cfg));
    strcpy(cfg.callsign, "dl1abc");
    TEST_ASSERT_FALSE(config::isValidFields(cfg));
    memset(cfg.callsign, 'A', sizeof(cfg.callsign));   // kein Terminator
    TEST_ASSERT_FALSE(config::isValidFields(cfg));
}

// ---- Hauptfunktion ----

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_hash_same_profile_is_equal);
    RUN_TEST(test_hash_differs_by_profile);
    RUN_TEST(test_hash_differs_by_afu_mode);
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_invalid_fox_id_rejected);
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_crc_detects_change);
    RUN_TEST(test_wrong_version_rejected);
    RUN_TEST(test_callsign_fields);

    return UNITY_END();
}
