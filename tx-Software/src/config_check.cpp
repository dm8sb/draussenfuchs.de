// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// config_check.cpp – CRC, Validierung und Config-Hash
// (hardwareunabhängig, nativ testbar; NVS-Zugriff in config_store.cpp)
// ============================================================================

#include "config_store.h"
#include <cstring>

namespace ardf {
namespace config {

// ---- CRC-32 (IEEE 802.3, Polynom 0xEDB88320, wie zlib/esp_rom_crc32_le) ----
// Bitweise statt Tabelle: Die Datenmengen sind klein (≤ 20 Byte), und eine
// fehlerhafte Tabelle wie in v2.0.x ist so ausgeschlossen.
uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320U : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

void updateCrc(FoxConfig& cfg) {
    cfg.crc32 = crc32(reinterpret_cast<const uint8_t*>(&cfg),
                      sizeof(FoxConfig) - sizeof(uint32_t));
}

void resetToDefaults(FoxConfig& cfg) {
    memset(&cfg, 0, sizeof(FoxConfig));
    cfg.version    = CONFIG_VERSION;
    cfg.fox_id     = DEFAULT_FOX_ID;
    cfg.profile_id = DEFAULT_FOX_CYCLE_PROFILE;
    updateCrc(cfg);
}

bool isValidFields(const FoxConfig& cfg) {
    // Fox-ID: 1..5
    if (cfg.fox_id < 1 || cfg.fox_id > MAX_FOXES) return false;
    // Profil-ID: 0..3
    if (cfg.profile_id > PROFILE_D_ID) return false;
    // Rufzeichen: nullterminiert, nur erlaubte Zeichen
    if (memchr(cfg.callsign, '\0', sizeof(cfg.callsign)) == nullptr) return false;
    for (size_t i = 0; cfg.callsign[i] != '\0'; i++) {
        char c = cfg.callsign[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '-' || c == '.')) return false;
    }
    return true;
}

bool isValidConfig(const FoxConfig& cfg) {
    if (cfg.version != CONFIG_VERSION) return false;
    uint32_t expected = crc32(reinterpret_cast<const uint8_t*>(&cfg),
                              sizeof(FoxConfig) - sizeof(uint32_t));
    if (cfg.crc32 != expected) return false;
    return isValidFields(cfg);
}

uint32_t calculateConfigHash(uint8_t profile_id,
                              uint16_t slot_ms, uint8_t wpm,
                              bool callsign_empty, uint8_t morse_repeats) {
    ConfigHashInput input;
    input.protocol_cfg_ver = SYNC_PROTOCOL_VERSION;
    input.profile_id       = profile_id;
    input.slot_duration_ms = slot_ms;
    input.wpm              = wpm;
    input.callsign_empty   = callsign_empty ? 1 : 0;
    input.morse_repeats    = morse_repeats;
    return crc32(reinterpret_cast<const uint8_t*>(&input), sizeof(ConfigHashInput));
}

} // namespace config
} // namespace ardf
