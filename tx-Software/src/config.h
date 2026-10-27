// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// config.h – Zentrale Konfigurationsdatei für den ARDF-Fuchsjagd-Sender
// ============================================================================

#ifndef ARDF_CONFIG_H
#define ARDF_CONFIG_H

#include <cstdint>
#include <cstring>

// ---- Versions-String (Build-Flag) ----
#ifndef ARDF_VERSION
  #define ARDF_VERSION "2.0.0"
#endif

// ---- Hardware-Pins ----
constexpr uint8_t DATA_PIN        = 3;   // Datenpin zum TX-Modul (Qiachip WL102-341)
constexpr uint8_t TX_VCC_EN_PIN   = 4;   // Versorgung TX-Modul (HIGH = an)
#ifndef LED_PIN_1
  #define LED_PIN_1 7
#endif
#ifndef LED_PIN_2
  #define LED_PIN_2 8
#endif

// ---- Seriell ----
constexpr uint32_t SERIAL_BAUD    = 115200;

// ---- Factory-Defaults ----
constexpr uint8_t  DEFAULT_FOX_ID           = 1;
constexpr uint8_t  DEFAULT_FOX_CYCLE_PROFILE = 2;  // Profil C (ISM-Ultra)
constexpr uint8_t  CALLSIGN_MAX_LEN         = 12;

// ---- Sync-Protokoll ----
constexpr uint8_t  SYNC_PROTOCOL_VERSION    = 3;
constexpr uint16_t BEACON_INTERVAL_MS       = 500;  // ± 50 ms Jitter
constexpr uint8_t  BEACON_JITTER_MS         = 50;
constexpr uint32_t SYNC_WINDOW_NOMINAL_MS   = 120000;  // 120 s (Profile A–C)
constexpr uint32_t SYNC_WINDOW_PROFILD_MS   = 30000;   // 30 s (Profil D)
constexpr uint32_t SYNC_WINDOW_MAX_MS       = 180000;  // 180 s maximal
constexpr uint8_t  ESPNOW_CHANNEL           = 1;
constexpr uint8_t  MAX_FOXES                = 5;

// ---- Duty-Guard ----
constexpr float    DUTY_LIMIT_ISM           = 0.095f;  // 9,5 % Sicherheitsmarge
constexpr float    DUTY_LIMIT_AFU_CALLSIGN  = 0.50f;   // 50 % im AFU-Callsign-Slot
constexpr float    DUTY_SAFETY_FACTOR       = 1.05f;   // 5 % Sicherheitsaufschlag auf TX-Zeit

// ---- CLI ----
constexpr uint32_t CLI_TIMEOUT_MS           = 120000;  // 120 s Auto-Start (Profile A–C)
constexpr uint32_t CLI_TIMEOUT_PROFILD_MS   = 30000;   // 30 s Auto-Start (Profil D)

// ---- Zeitkorrektur ----
constexpr int16_t  DRIFT_PPM_MAX_VALID      = 500;     // |ppm| < 500 für NVS-Nutzung
constexpr uint8_t  DRIFT_QUALITY_MIN        = 60;      // Mindestqualität für NVS-Nutzung
constexpr uint8_t  DRIFT_MIN_SAMPLES        = 30;      // Min. Messpunkte für Regression

// ---- Vorlaufberechnung (µs) ----
constexpr uint32_t VORLAUF_BASE_US          = 80000;   // 80 ms Basis
constexpr uint32_t VORLAUF_EXTRA_US         = 20000;   // 20 ms Zusatz
constexpr uint32_t VORLAUF_MIN_US           = 100000;  // 100 ms Minimum
constexpr uint32_t VORLAUF_MAX_US           = 350000;  // 350 ms Maximum

// ---- TX-Modul ----
constexpr uint16_t TX_WARMUP_MS             = 20;      // 20 ms Oszillator-Einschwingzeit

// ---- Fehler-Erholung ----
constexpr uint32_t ERROR_RECOVERY_MS        = 60000;   // 60 s bis Auto-Neustart

// ---- Profil-IDs ----
constexpr uint8_t  PROFILE_A_ID             = 0;  // ISM-Standard
constexpr uint8_t  PROFILE_B_ID             = 1;  // ISM-Sprint
constexpr uint8_t  PROFILE_C_ID             = 2;  // ISM-Ultra (Default)
constexpr uint8_t  PROFILE_D_ID             = 3;  // draussenfuchs Solo

// ---- Callsign TX-Counter ----
constexpr uint8_t  CALLSIGN_SLOT_INTERVAL   = 10;  // Jeden 10. Slot
constexpr uint8_t  CALLSIGN_SLOT_TRIGGER    = 9;   // Bei counter == 9

// ---- Morse-Tonfrequenzen (Hz) je Fox-ID ----
constexpr uint16_t FOX_TONE_HZ[MAX_FOXES]  = {700, 800, 900, 1000, 1200};

// ---- NVS-Namespaces ----
constexpr const char* NVS_NAMESPACE_CFG     = "ardf_fox";
constexpr const char* NVS_KEY_CFG           = "cfg_v2";
constexpr const char* NVS_NAMESPACE_TIME    = "timecorr";
constexpr const char* NVS_KEY_TIME          = "tc_v1";

// ---- Config-Struct (NVS-Persistenz) ----
struct __attribute__((packed)) ConfigV2 {
    uint8_t  version;        // = 2
    uint8_t  fox_id;         // 1..5
    uint8_t  profile_id;     // 0..3
    char     callsign[13];   // Max. 12 Zeichen + Null-Terminator
    uint32_t crc32;          // CRC32-Prüfsumme
};

// ---- Zeitkorrektur-Struct (NVS-Persistenz) ----
struct __attribute__((packed)) TimeCorrectionData {
    int16_t  drift_ppm;
    int64_t  offset_us;
    uint8_t  quality;
    uint16_t meas_count;
    uint32_t crc32;
};

// ---- SyncBeacon-Struct ----
struct __attribute__((packed)) SyncBeacon {
    uint8_t  protocol_ver;        // = 2
    uint8_t  profile_id;
    uint8_t  fox_id;
    uint8_t  flags;               // Bit-Maske
    uint8_t  mac[6];
    uint32_t boot_id;             // Zufällige ID pro Boot
    uint64_t uptime_us;
    uint64_t cycle_start_us;
    uint32_t config_hash;
    int16_t  drift_ppm_est;
    uint8_t  sync_quality;
    uint8_t  active_count;
    uint8_t  active_fox_ids[5];
    uint16_t slot_ms;
    uint16_t cycle_ms;
};

// ---- SyncBeacon Flags ----
constexpr uint8_t FLAG_AFU_MODE       = (1 << 0);  // Bit 0: AFU-Modus aktiv
constexpr uint8_t FLAG_DUTY_GUARD     = (1 << 1);  // Bit 1: Duty-Guard aktiv
constexpr uint8_t FLAG_MASTER         = (1 << 2);  // Bit 2: Master-Rolle

// ---- Config-Hash Eingabe-Struct ----
// Nur verbundweite Parameter – keine Fox-ID, sonst wären Füchse mit
// unterschiedlicher Nummer nie kompatibel.
struct __attribute__((packed)) ConfigHashInput {
    uint8_t  protocol_cfg_ver;    // = SYNC_PROTOCOL_VERSION
    uint8_t  profile_id;
    uint16_t slot_duration_ms;
    uint8_t  wpm;
    uint8_t  callsign_empty;      // 1 wenn leer, sonst 0
    uint8_t  morse_repeats;
};

// ---- Log-Präfixe ----
#define LOG_FOX   "[FOX] "
#define LOG_SYNC  "[SYNC] "
#define LOG_DUTY  "[DUTY] "
#define LOG_CLI   "[CLI] "
#define LOG_ERR   "[ERR] "
#define LOG_CFG   "[CFG] "
#define LOG_WARN  "[WARN] "

#endif // ARDF_CONFIG_H
