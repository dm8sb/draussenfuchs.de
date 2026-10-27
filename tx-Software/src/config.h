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
constexpr uint32_t SYNC_WINDOW_MS           = 120000;  // Sendebeginn 120 s nach Sync-Start des Masters
constexpr uint32_t SYNC_LISTEN_MS           = 1500;    // Erst zuhören, dann ggf. Master werden
constexpr uint32_t PEER_TIMEOUT_MS          = 3000;    // Peer ohne Beacon gilt als inaktiv
constexpr uint32_t PLAN_FREEZE_MS           = 10000;   // Slotplan 10 s vor Sendebeginn fest
constexpr uint32_t SYNC_FINISH_LEAD_MS      = 1000;    // Sync endet 1 s vor Sendebeginn
constexpr uint32_t START_LEAD_MS            = 5000;    // Menüpunkt 10 (Master): Start in 5 s
constexpr uint32_t SLOT_GUARD_MS            = 250;     // Schutzabstand bis zum nächsten Slot
constexpr uint8_t  ESPNOW_CHANNEL           = 1;
constexpr uint8_t  MAX_FOXES                = 5;
constexpr uint8_t  MAX_PEERS                = 8;       // auch fremde/inkompatible Geräte

// ---- Duty-Guard ----
constexpr float    DUTY_LIMIT_ISM           = 0.095f;  // 9,5 % Sicherheitsmarge
constexpr float    DUTY_LIMIT_AFU_CALLSIGN  = 0.50f;   // 50 % im AFU-Callsign-Slot
constexpr float    DUTY_SAFETY_FACTOR       = 1.05f;   // 5 % Sicherheitsaufschlag auf TX-Zeit

// ---- CLI ----
constexpr uint32_t CLI_TIMEOUT_PROFILD_MS   = 30000;   // 30 s Auto-Start (Profil D)
constexpr uint32_t CLI_PROMPT_TIMEOUT_MS    = 30000;   // Rückfragen ohne Antwort abbrechen
constexpr uint32_t USB_CDC_SETTLE_MS        = 500;     // USB-CDC nach Kaltstart stabilisieren

// ---- Zeitkorrektur ----
// Quarz-Toleranz zweier Boards: zusammen deutlich unter 100 ppm; größere
// Steigungen sind Messfehler und werden verworfen.
constexpr int16_t  DRIFT_PPM_MAX_VALID      = 100;
constexpr uint8_t  DRIFT_MIN_SAMPLES        = 30;      // Min. Messpunkte für Regression
constexpr uint16_t TIME_MAX_SAMPLES         = 256;     // Ringpuffer: ganzes Sync-Fenster (2 Beacons/s)
constexpr uint32_t TIME_OUTLIER_MIN_US      = 500;     // Mindest-Ausreißergrenze

// ---- TX-Modul ----
constexpr uint16_t TX_WARMUP_MS             = 20;      // 20 ms Oszillator-Einschwingzeit

// ---- Fehler-Erholung ----
constexpr uint32_t ERROR_RECOVERY_MS        = 60000;   // 60 s ohne Eingabe bis Neuversuch
constexpr uint8_t  MAX_ERROR_RETRIES        = 5;       // danach Tiefschlaf bis Reset

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
constexpr const char* NVS_KEY_CFG           = "cfg_v3";
constexpr const char* NVS_KEY_CFG_LEGACY    = "cfg_v2";  // v2.0.x: wird beim Laden übernommen
constexpr const char* NVS_NAMESPACE_TIME    = "timecorr";
constexpr const char* NVS_KEY_TIME          = "tc_v2";   // letzte Messung (nur Anzeige)

// ---- Config-Struct (NVS-Persistenz) ----
// Version 3: gleiches Layout wie Version 2, aber Standard-CRC-32 (v2 hatte eine
// fehlerhafte CRC-Tabelle).
constexpr uint8_t CONFIG_VERSION        = 3;
constexpr uint8_t CONFIG_VERSION_LEGACY = 2;

struct __attribute__((packed)) FoxConfig {
    uint8_t  version;        // = CONFIG_VERSION
    uint8_t  fox_id;         // 1..5
    uint8_t  profile_id;     // 0..3
    char     callsign[13];   // Max. 12 Zeichen + Null-Terminator
    uint32_t crc32;          // CRC32-Prüfsumme
};

// ---- Zeitkorrektur-Struct (NVS-Persistenz) ----
// Letzte Messung gegen den Master. Nur zur Anzeige: Die Drift gilt relativ zum
// jeweiligen Master und wird beim nächsten Einsatz neu gemessen.
struct __attribute__((packed)) TimeCorrectionData {
    int16_t  drift_ppm;
    int64_t  offset_us;
    uint8_t  quality;
    uint16_t meas_count;
    uint32_t crc32;
};

// ---- SyncBeacon-Struct (Protokoll v3) ----
// Absender-MAC kommt aus dem ESP-NOW-Empfang, nicht aus dem Payload.
struct __attribute__((packed)) SyncBeacon {
    uint8_t  protocol_ver;        // = SYNC_PROTOCOL_VERSION
    uint8_t  profile_id;
    uint8_t  fox_id;
    uint8_t  flags;               // Bit-Maske FLAG_*
    uint32_t boot_id;             // Zufällige ID pro Boot
    uint64_t uptime_us;           // Sendezeitpunkt in der Uhr des Absenders
    uint32_t config_hash;         // verbundweite Parameter (ohne Fox-ID)
    uint32_t tx_norm_ms;          // Sendezeit-Budget normaler Slot
    uint32_t tx_max_ms;           // längste Aussendung (Rufzeichen-Slot)
    uint64_t plan_start_us;       // Sendebeginn in der Uhr des Absenders (FLAG_HAS_START)
    // Slotplan – nur gültig mit FLAG_MASTER
    uint32_t plan_cycle_ms;
    uint32_t plan_slot_ms;
    uint8_t  plan_count;
    uint8_t  plan_fox_ids[MAX_FOXES];
};

// ---- SyncBeacon Flags ----
constexpr uint8_t FLAG_AFU_MODE       = (1 << 0);  // Bit 0: AFU-Modus aktiv
constexpr uint8_t FLAG_MASTER         = (1 << 1);  // Bit 1: Master, Slotplan gültig
constexpr uint8_t FLAG_PLAN_FROZEN    = (1 << 2);  // Bit 2: Slotplan eingefroren
constexpr uint8_t FLAG_HAS_START      = (1 << 3);  // Bit 3: plan_start_us gültig

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
