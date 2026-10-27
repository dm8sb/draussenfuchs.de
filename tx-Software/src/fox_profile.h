// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// fox_profile.h – ARDF-Profil-Definitionen (A/B/C/D)
// ============================================================================

#ifndef ARDF_FOX_PROFILE_H
#define ARDF_FOX_PROFILE_H

#include <cstdint>

namespace ardf {

// Profildaten-Struktur
struct FoxProfile {
    uint8_t  id;              // Profil-ID (0..3)
    char     name;            // Profilbuchstabe (A/B/C/D)
    const char* description;  // Beschreibung
    uint8_t  fox_count;       // Anzahl Füchse im Zyklus
    uint32_t cycle_ms;        // Gesamtzyklus in ms
    uint32_t slot_ms;         // Slot-Dauer in ms
    uint8_t  wpm;             // Morsegeschwindigkeit
    uint8_t  morse_repeats;   // Anzahl MO-Kennungs-Wiederholungen (0 = kein Morse)
    bool     use_espnow;      // ESP-NOW-Sync aktiv?
    bool     is_continuous;   // Dauerton statt Morse?
    uint16_t tone_on_ms;      // Nur Profil D: Dauerton-Dauer
    uint16_t tone_off_ms;     // Nur Profil D: Pausendauer
};

// Profil anhand ID ermitteln
const FoxProfile& getProfile(uint8_t profile_id);

// Profilname (Buchstabe) anhand ID
char getProfileChar(uint8_t profile_id);

// Profil-ID anhand Buchstabe
uint8_t getProfileIdByChar(char c);

// Profil gültig?
bool isValidProfileId(uint8_t profile_id);

// Sendezeit-Budget einer Aussendung in ms (zentrale Definition für den Duty-Guard):
// Zeit, in der das TX-Modul versorgt ist = 20 ms Vorlauf + komplette Sequenz inkl.
// Pausen. callsign != nullptr (und nicht leer) → Rufzeichen-Slot (MO + 7 dits + Rufzeichen).
uint32_t txBudgetMs(uint8_t profile_id, uint8_t fox_id, const char* callsign);

} // namespace ardf

#endif // ARDF_FOX_PROFILE_H
