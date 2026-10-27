// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// sync_logic.h – Verbundlogik: Peers, Master-Wahl, Slotplan, Zeitmodell
// (hardwareunabhängig, nativ testbar; Funk in sync_espnow.cpp)
// ============================================================================
//
// Ablauf:
//  1. Nach dem Start SYNC_LISTEN_MS nur zuhören.
//  2. Master = höchste MAC unter allen aktiven, kompatiblen Füchsen. Ein Fuchs,
//     dessen Plan bereits eingefroren ist, bleibt Master (Nachzügler übernehmen).
//  3. Jeder Fuchs übernimmt nach dem Zuhören einen im Verbund bereits bekannten
//     Sendebeginn (FLAG_HAS_START) oder schlägt Sync-Start + SYNC_WINDOW_MS vor.
//     Ein später hinzukommender Fuchs verschiebt den Start deshalb nicht.
//     Der Master legt den Slotplan fest (Start, Zyklus, Slot, Reihenfolge) und
//     sendet ihn in jedem Beacon.
//  4. Slaves messen ihre Uhr nur gegen den Master (TimeModel) und rechnen alle
//     Sendezeitpunkte aus der Master-Uhr in die lokale Uhr um.
//  5. PLAN_FREEZE_MS vor dem Start wird der Plan eingefroren,
//     SYNC_FINISH_LEAD_MS vor dem Start endet die Sync-Phase.

#ifndef ARDF_SYNC_LOGIC_H
#define ARDF_SYNC_LOGIC_H

#include <cstdint>
#include "config.h"
#include "schedule.h"
#include "time_sync.h"

namespace ardf {
namespace sync {

// Erkannter Nachbar
struct PeerFox {
    uint8_t  mac[6];
    uint8_t  fox_id;
    uint8_t  profile_id;
    uint8_t  flags;
    uint32_t boot_id;
    uint64_t last_seen_us;     // lokale Empfangszeit
    uint32_t tx_norm_ms;
    uint32_t tx_max_ms;
    bool     compatible;
    bool     in_use;
    bool     was_active;
};

// Eigene Daten
struct OwnInfo {
    uint8_t  mac[6];
    uint8_t  fox_id;
    uint8_t  profile_id;
    uint32_t config_hash;
    bool     afu;
    uint32_t tx_norm_ms;
    uint32_t tx_max_ms;
    uint32_t boot_id;
};

// Ereignisse für die Log-Ausgabe (takeEvents() setzt sie zurück)
constexpr uint8_t EV_MASTER_CHANGED = (1 << 0);
constexpr uint8_t EV_PLAN_CHANGED   = (1 << 1);
constexpr uint8_t EV_FROZEN         = (1 << 2);
constexpr uint8_t EV_COLLISION      = (1 << 3);
constexpr uint8_t EV_MISMATCH       = (1 << 4);
constexpr uint8_t EV_PEER_LOST      = (1 << 5);
constexpr uint8_t EV_START_MOVED    = (1 << 6);

constexpr int8_t MASTER_NONE = -2;   // noch in der Zuhörphase
constexpr int8_t MASTER_SELF = -1;

class Network {
public:
    void begin(const OwnInfo& own, uint64_t now_us);

    // Beacon verarbeiten; Rückgabe: Index eines neu entdeckten Peers, sonst −1
    int8_t onBeacon(const uint8_t* src_mac, const SyncBeacon& b, uint64_t rx_us);

    // Peers altern lassen, Master wählen, Plan pflegen, ggf. einfrieren
    void tick(uint64_t now_us);

    // Menüpunkt 10: Master zieht den Sendebeginn auf now + START_LEAD_MS vor.
    // false, wenn dieser Fuchs nicht Master ist oder der Plan schon fest ist.
    bool requestStart(uint64_t now_us);

    // Beacon für den Versand füllen
    void fillBeacon(SyncBeacon& b, uint64_t now_us) const;

    // Sync-Phase beendet (Plan bekannt, Start steht unmittelbar bevor)?
    bool finished(uint64_t now_us) const;

    uint8_t takeEvents();
    void resetTimeModel();

    // ---- Zustand ----
    const OwnInfo& own() const                     { return own_; }
    int8_t master() const                          { return master_; }
    bool isMaster() const                          { return master_ == MASTER_SELF; }
    bool hasPlan() const                           { return has_plan_; }
    bool frozen() const                            { return frozen_; }
    const schedule::CyclePlan& plan() const        { return plan_; }
    uint64_t planStartRefUs() const                { return plan_start_ref_; }
    uint64_t planStartLocalUs() const;
    int8_t ownSlotIndex() const;
    const timesync::TimeModel& timeModel() const   { return model_; }   // Master → lokal

    bool collision() const                         { return collision_; }
    bool mismatch() const                          { return mismatch_; }
    uint8_t mismatchFoxId() const                  { return mismatch_fox_id_; }
    uint8_t mismatchProfileId() const              { return mismatch_profile_id_; }
    const uint8_t* mismatchMac() const             { return mismatch_mac_; }

    const PeerFox& peer(uint8_t i) const           { return peers_[i]; }
    bool peerActive(uint8_t i, uint64_t now_us) const;

private:
    void updatePlanAsMaster();
    void copyPlanFromBeacon(const SyncBeacon& b);

    OwnInfo  own_;
    uint64_t start_us_ = 0;
    uint64_t now_us_ = 0;
    PeerFox  peers_[MAX_PEERS];
    int8_t   master_ = MASTER_NONE;
    bool     frozen_ = false;

    schedule::CyclePlan plan_;
    bool     has_plan_ = false;
    uint64_t plan_start_ref_ = 0;      // in der Uhr des Masters
    uint64_t plan_start_local_ = 0;    // lokale Schätzung (für Vererbung)
    bool     has_start_local_ = false;

    timesync::TimeModel model_;
    bool     model_dirty_ = false;
    bool     model_valid_ = false;     // seit dem letzten clear() berechnet
    uint64_t last_model_us_ = 0;

    bool     collision_ = false;
    bool     mismatch_ = false;
    uint8_t  mismatch_fox_id_ = 0;
    uint8_t  mismatch_profile_id_ = 0;
    uint8_t  mismatch_mac_[6] = {0};

    uint8_t  events_ = 0;
};

} // namespace sync
} // namespace ardf

#endif // ARDF_SYNC_LOGIC_H
