// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// sync_logic.cpp – Verbundlogik Implementierung
// ============================================================================

#include "sync_logic.h"
#include "fox_profile.h"
#include <cstring>

namespace ardf {
namespace sync {

static int compareMac(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6);
}

static bool samePlan(const schedule::CyclePlan& a, const schedule::CyclePlan& b) {
    return a.cycle_ms == b.cycle_ms && a.slot_ms == b.slot_ms &&
           a.count == b.count && memcmp(a.fox_ids, b.fox_ids, a.count) == 0;
}

void Network::begin(const OwnInfo& own, uint64_t now_us) {
    own_ = own;
    start_us_ = now_us;
    memset(peers_, 0, sizeof(peers_));
    master_ = MASTER_NONE;
    master_boot_id_ = 0;
    master_since_us_ = now_us;
    frozen_ = false;
    missed_start_ = false;
    memset(&plan_, 0, sizeof(plan_));
    has_plan_ = false;
    plan_start_ref_ = 0;
    plan_start_local_ = 0;
    has_start_local_ = false;
    model_.clear();
    model_dirty_ = false;
    model_valid_ = false;
    last_model_us_ = 0;
    collision_ = false;
    mismatch_ = false;
    events_ = 0;
}

bool Network::peerActive(uint8_t i, uint64_t now_us) const {
    const PeerFox& p = peers_[i];
    // Addition statt Differenz: ein Empfangszeitstempel minimal nach now_us
    // (Beacon während der Auswertung eingetroffen) darf nicht überlaufen
    return p.in_use && now_us <= p.last_seen_us + (uint64_t)PEER_TIMEOUT_MS * 1000ULL;
}

void Network::dropMasterTiming() {
    model_.clear();
    model_dirty_ = false;
    model_valid_ = false;
    has_plan_ = false;   // Plan und Modell gehören zusammen (gleiche Master-Uhr)
}

int8_t Network::onBeacon(const uint8_t* src_mac, const SyncBeacon& b, uint64_t rx_us) {
    if (b.protocol_ver != SYNC_PROTOCOL_VERSION) return -1;

    // Peer suchen; neue Peers belegen einen freien oder abgelaufenen Platz
    int8_t idx = -1;
    int8_t free_idx = -1;
    for (uint8_t i = 0; i < MAX_PEERS; i++) {
        if (peers_[i].in_use && compareMac(peers_[i].mac, src_mac) == 0) {
            idx = (int8_t)i;
            break;
        }
        if (free_idx < 0 && !peerActive(i, rx_us) && (int8_t)i != master_) {
            free_idx = (int8_t)i;
        }
    }

    bool is_new = false;
    if (idx < 0) {
        if (free_idx < 0) return -1;   // Tabelle voll
        idx = free_idx;
        memset(&peers_[idx], 0, sizeof(PeerFox));
        memcpy(peers_[idx].mac, src_mac, 6);
        peers_[idx].in_use = true;
        is_new = true;
    }

    PeerFox& p = peers_[idx];
    if (is_new || p.boot_id != b.boot_id) {
        p.first_seen_us = rx_us;   // neue Instanz (auch nach Neustart): hört erst zu
    }
    p.fox_id       = b.fox_id;
    p.profile_id   = b.profile_id;
    p.flags        = b.flags;
    p.boot_id      = b.boot_id;
    p.last_seen_us = rx_us;
    p.tx_norm_ms   = b.tx_norm_ms;
    p.tx_max_ms    = b.tx_max_ms;
    p.compatible   = (b.config_hash == own_.config_hash);

    if (idx == master_ && b.boot_id != master_boot_id_ && !frozen_) {
        // Master neu gestartet: seine Uhr beginnt wieder bei 0, Messung und Plan
        // (beide in der alten Master-Uhr) verwerfen und neu aufbauen
        dropMasterTiming();
        master_boot_id_ = b.boot_id;
        master_since_us_ = rx_us;
        events_ |= EV_MASTER_CHANGED;
    }
    // Nach dem Einfrieren gelten altes Modell und alter Plan weiter; Beacons einer
    // neuen Master-Instanz (andere boot_id) passen nicht dazu und werden ignoriert.

    // Vererbung: einen im Verbund bereits bekannten Sendebeginn übernehmen
    if (p.compatible && (b.flags & FLAG_HAS_START) && !has_start_local_) {
        plan_start_local_ = b.plan_start_us + (rx_us - b.uptime_us);
        has_start_local_ = true;
    }

    if (p.compatible && (b.flags & FLAG_MASTER) && idx == master_ &&
        b.boot_id == master_boot_id_) {
        model_.add(b.uptime_us, rx_us);
        model_dirty_ = true;
        copyPlanFromBeacon(b);
        if ((b.flags & FLAG_PLAN_FROZEN) && !frozen_ && has_plan_) {
            frozen_ = true;
            events_ |= EV_FROZEN;
        }
    }

    return is_new ? idx : -1;
}

void Network::copyPlanFromBeacon(const SyncBeacon& b) {
    // Plausibilität: nur Pläne übernehmen, die auch selbst so entstehen könnten
    if (b.plan_count < 1 || b.plan_count > MAX_FOXES ||
        b.plan_slot_ms == 0 || (uint64_t)b.plan_slot_ms * b.plan_count > b.plan_cycle_ms) {
        return;
    }
    for (uint8_t i = 0; i < b.plan_count; i++) {
        uint8_t id = b.plan_fox_ids[i];
        if (id < 1 || id > MAX_FOXES || (i > 0 && id <= b.plan_fox_ids[i - 1])) return;
    }

    schedule::CyclePlan np = {};
    np.cycle_ms = b.plan_cycle_ms;
    np.slot_ms  = b.plan_slot_ms;
    np.count    = b.plan_count;
    memcpy(np.fox_ids, b.plan_fox_ids, np.count);
    np.extended = np.cycle_ms > getProfile(own_.profile_id).slot_ms * np.count;

    if (!has_plan_ || !samePlan(np, plan_) || plan_start_ref_ != b.plan_start_us) {
        events_ |= EV_PLAN_CHANGED;
    }
    plan_ = np;
    plan_start_ref_ = b.plan_start_us;
    has_plan_ = true;
}

void Network::updatePlanAsMaster(uint64_t now_us) {
    schedule::FoxTiming timings[MAX_PEERS + 1];
    uint8_t n = 0;
    timings[n++] = {own_.fox_id, own_.tx_norm_ms, own_.tx_max_ms};
    for (uint8_t i = 0; i < MAX_PEERS; i++) {
        if (peerActive(i, now_us) && peers_[i].compatible) {
            timings[n++] = {peers_[i].fox_id, peers_[i].tx_norm_ms, peers_[i].tx_max_ms};
        }
    }
    schedule::CyclePlan np = schedule::computePlan(own_.profile_id, timings, n);
    if (!has_plan_ || !samePlan(np, plan_)) {
        events_ |= EV_PLAN_CHANGED;
    }
    plan_ = np;
    has_plan_ = true;
}

int8_t Network::electMaster(uint64_t now_us) const {
    // 1. Ein Fuchs mit eingefrorenem Plan bleibt Master – wir sind Nachzügler
    // 2. Sonst bleibt ein bestehender Master Master („klebrig"): gewählt wird nur
    //    unter den Füchsen, die bereits FLAG_MASTER senden (einschließlich uns
    //    selbst, falls wir Master sind). So übernimmt ein später eingeschalteter
    //    Fuchs nicht und die laufende Zeitmessung bleibt erhalten.
    // 3. Gibt es keinen Master: höchste MAC unter uns und den Füchsen, die schon
    //    mindestens SYNC_LISTEN_MS zu hören sind (also selbst fertig zugehört
    //    haben). Schalten zwei Füchse gleichzeitig ein, werden kurz beide Master;
    //    nach Regel 2 setzt sich dann die höhere MAC durch.
    int8_t frozen_master = -1;
    int8_t flagged = -1;
    for (uint8_t i = 0; i < MAX_PEERS; i++) {
        if (!peerActive(i, now_us) || !peers_[i].compatible) continue;
        if (!(peers_[i].flags & FLAG_MASTER)) continue;
        if ((peers_[i].flags & FLAG_PLAN_FROZEN) &&
            (frozen_master < 0 || compareMac(peers_[i].mac, peers_[frozen_master].mac) > 0)) {
            frozen_master = (int8_t)i;
        }
        if (flagged < 0 || compareMac(peers_[i].mac, peers_[flagged].mac) > 0) {
            flagged = (int8_t)i;
        }
    }
    if (frozen_master >= 0) return frozen_master;
    if (now_us - start_us_ < (uint64_t)SYNC_LISTEN_MS * 1000ULL) return MASTER_NONE;

    if (flagged >= 0) {
        if (master_ == MASTER_SELF && compareMac(own_.mac, peers_[flagged].mac) > 0) {
            return MASTER_SELF;   // zwei Master: die höhere MAC bleibt
        }
        return flagged;
    }
    if (master_ == MASTER_SELF) return MASTER_SELF;

    int8_t m = MASTER_SELF;
    const uint8_t* best = own_.mac;
    for (uint8_t i = 0; i < MAX_PEERS; i++) {
        if (!peerActive(i, now_us) || !peers_[i].compatible) continue;
        if (now_us < peers_[i].first_seen_us + (uint64_t)SYNC_LISTEN_MS * 1000ULL) continue;
        if (compareMac(peers_[i].mac, best) > 0) {
            best = peers_[i].mac;
            m = (int8_t)i;
        }
    }
    return m;
}

void Network::tick(uint64_t now_us) {
    // Peers altern lassen, Kollision und Abweichung bestimmen
    bool collision = false;
    bool mismatch = false;
    for (uint8_t i = 0; i < MAX_PEERS; i++) {
        PeerFox& p = peers_[i];
        if (!p.in_use) continue;
        bool active = peerActive(i, now_us);
        if (p.was_active && !active) events_ |= EV_PEER_LOST;
        p.was_active = active;
        if (!active) continue;

        if (p.compatible && p.fox_id == own_.fox_id) collision = true;
        if (!p.compatible && !mismatch) {
            mismatch = true;
            mismatch_fox_id_ = p.fox_id;
            mismatch_profile_id_ = p.profile_id;
            memcpy(mismatch_mac_, p.mac, 6);
        }
    }
    if (collision && !collision_) events_ |= EV_COLLISION;
    if (mismatch && !mismatch_) events_ |= EV_MISMATCH;
    collision_ = collision;
    mismatch_ = mismatch;

    // Zeitmodell sofort nach den ersten Messwerten, danach höchstens 1×/s schätzen
    if (model_dirty_ && (!model_valid_ || now_us - last_model_us_ >= 1000000ULL)) {
        model_.compute();
        model_dirty_ = false;
        model_valid_ = true;
        last_model_us_ = now_us;
    }

    if (frozen_ || missed_start_) return;   // Plan und Master stehen fest

    bool listened = now_us - start_us_ >= (uint64_t)SYNC_LISTEN_MS * 1000ULL;
    if (listened && !has_start_local_) {
        // Kein Sendebeginn gehört: eigenen vorschlagen (gilt, falls wir Master werden)
        plan_start_local_ = start_us_ + (uint64_t)SYNC_WINDOW_MS * 1000ULL;
        has_start_local_ = true;
    }

    // Bekannter Sendebeginn verstrichen, ohne dass wir einen Plan haben (z. B.
    // Neustart kurz vor dem Start): nie mehr selbst Master werden – der Verbund
    // sendet bereits, ein eigener Plan wäre ein zweites, unsynchronisiertes Raster
    if (has_start_local_ && !has_plan_ && now_us >= plan_start_local_) {
        missed_start_ = true;
        return;
    }

    // ---- Master-Wahl ----
    int8_t m = electMaster(now_us);
    if (m != master_) {
        master_ = m;
        master_boot_id_ = (m >= 0) ? peers_[m].boot_id : own_.boot_id;
        master_since_us_ = now_us;
        dropMasterTiming();   // Plan kommt mit dem nächsten Beacon bzw. wird neu berechnet
        events_ |= EV_MASTER_CHANGED;
    }

    if (master_ == MASTER_SELF) {
        if (now_us >= plan_start_local_) {
            // Geerbter Sendebeginn liegt schon zurück – siehe oben
            missed_start_ = true;
            return;
        }
        plan_start_ref_ = plan_start_local_;
        updatePlanAsMaster(now_us);
    } else if (master_ >= 0 && has_plan_ && model_valid_) {
        // Lokale Schätzung mitführen, falls wir später Master werden
        plan_start_local_ = model_.toLocal(plan_start_ref_);
        has_start_local_ = true;
    }

    if (timingValid() && now_us + (uint64_t)PLAN_FREEZE_MS * 1000ULL >= planStartLocalUs()) {
        frozen_ = true;
        events_ |= EV_FROZEN;
    }
}

bool Network::requestStart(uint64_t now_us) {
    if (master_ != MASTER_SELF || frozen_ || missed_start_) return false;
    // Mindestens START_MIN_SYNC_MS Messzeit seit Beginn der Master-Rolle, damit
    // die Slaves ihre Taktabweichung schätzen können
    uint64_t t = now_us + (uint64_t)START_LEAD_MS * 1000ULL;
    uint64_t t_min = master_since_us_ + (uint64_t)START_MIN_SYNC_MS * 1000ULL;
    if (t < t_min) t = t_min;
    if (t < plan_start_local_) {
        plan_start_local_ = t;
        plan_start_ref_ = t;
        events_ |= EV_START_MOVED;
    }
    return true;
}

void Network::fillBeacon(SyncBeacon& b, uint64_t now_us) const {
    memset(&b, 0, sizeof(b));
    b.protocol_ver = SYNC_PROTOCOL_VERSION;
    b.profile_id   = own_.profile_id;
    b.fox_id       = own_.fox_id;
    b.boot_id      = own_.boot_id;
    b.uptime_us    = now_us;
    b.config_hash  = own_.config_hash;
    b.tx_norm_ms   = own_.tx_norm_ms;
    b.tx_max_ms    = own_.tx_max_ms;
    if (own_.afu) b.flags |= FLAG_AFU_MODE;

    if (has_start_local_) {
        b.flags |= FLAG_HAS_START;
        b.plan_start_us = plan_start_local_;
    }

    if (master_ == MASTER_SELF && has_plan_) {
        b.flags |= FLAG_MASTER;
        if (frozen_) b.flags |= FLAG_PLAN_FROZEN;
        b.plan_cycle_ms = plan_.cycle_ms;
        b.plan_slot_ms  = plan_.slot_ms;
        b.plan_count    = plan_.count;
        memcpy(b.plan_fox_ids, plan_.fox_ids, MAX_FOXES);
    }
}

bool Network::timingValid() const {
    // Slave: Plan und Startzeit liegen in der Master-Uhr und brauchen ein
    // berechnetes Zeitmodell; ohne Messwerte wäre die Umrechnung 1:1 (falsch)
    if (!has_plan_ || master_ == MASTER_NONE) return false;
    return master_ == MASTER_SELF || (model_valid_ && model_.samples() > 0);
}

uint64_t Network::planStartLocalUs() const {
    if (master_ == MASTER_SELF) return plan_start_ref_;
    return model_.toLocal(plan_start_ref_);
}

bool Network::finished(uint64_t now_us) const {
    if (missed_start_ || !timingValid()) return false;
    return now_us + (uint64_t)SYNC_FINISH_LEAD_MS * 1000ULL >= planStartLocalUs();
}

int8_t Network::ownSlotIndex() const {
    if (!has_plan_) return -1;
    return schedule::slotIndex(plan_, own_.fox_id);
}

uint8_t Network::takeEvents() {
    uint8_t e = events_;
    events_ = 0;
    return e;
}

} // namespace sync
} // namespace ardf
