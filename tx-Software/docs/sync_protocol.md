# ESP-NOW Synchronisationsprotokoll

## Übersicht

Das Synchronisationsprotokoll legt für bis zu 5 ARDF-Fuchsjagd-Sender einen gemeinsamen
Slotplan und Sendebeginn fest. Es basiert auf ESP-NOW (Kanal 1) und läuft nur in der
Startphase; nach dem Sendebeginn ist WiFi ausgeschaltet.

**Protokollversion:** 3 (inkompatibel zu Firmware 2.0.x / Protokoll 2)

Implementierung: Verbundlogik in `src/sync_logic.cpp` (hardwareunabhängig, per Simulation
getestet), Funk in `src/sync_espnow.cpp`.

## Anwendungsbereich

- Gilt für Profile A, B und C
- Profil D (draussenfuchs Solo) nimmt nicht am Sync teil

## Beacon-Struktur

Die Beacons werden als gepackte Binärstruktur über ESP-NOW (Broadcast) gesendet. Die
Absender-MAC stammt aus dem ESP-NOW-Empfang.

```cpp
struct __attribute__((packed)) SyncBeacon {
    uint8_t  protocol_ver;        // = 3
    uint8_t  profile_id;          // Profil-ID (0..3)
    uint8_t  fox_id;              // Fox-Nummer (1..5)
    uint8_t  flags;               // Bit-Maske (siehe unten)
    uint32_t boot_id;             // Zufällige ID pro Boot
    uint64_t uptime_us;           // Sendezeitpunkt in der Uhr des Absenders
    uint32_t config_hash;         // CRC-32 über verbundweite Parameter
    uint32_t tx_norm_ms;          // Sendezeit-Budget normaler Slot
    uint32_t tx_max_ms;           // längste Aussendung (Rufzeichen-Slot)
    uint64_t plan_start_us;       // Sendebeginn in der Uhr des Absenders (FLAG_HAS_START)
    // Slotplan – nur gültig mit FLAG_MASTER
    uint32_t plan_cycle_ms;
    uint32_t plan_slot_ms;
    uint8_t  plan_count;
    uint8_t  plan_fox_ids[5];     // Slotreihenfolge
};
```

### Flags-Feld (Bit-Maske)

| Bit | Name               | Bedeutung                                       |
|----:|--------------------|-------------------------------------------------|
|   0 | `FLAG_AFU_MODE`    | AFU-Modus aktiv (Rufzeichen gesetzt)            |
|   1 | `FLAG_MASTER`      | Absender ist Master, Slotplan gültig            |
|   2 | `FLAG_PLAN_FROZEN` | Slotplan ist eingefroren                        |
|   3 | `FLAG_HAS_START`   | `plan_start_us` gültig                          |
| 4–7 | –                  | Reserviert (immer 0)                            |

## Config-Hash

Der Config-Hash stellt sicher, dass alle Sender im Verbund identische Betriebsparameter
verwenden. Er enthält **nur verbundweite Parameter, keine Fox-ID**:

```cpp
struct __attribute__((packed)) ConfigHashInput {
    uint8_t  protocol_cfg_ver;  // = 3
    uint8_t  profile_id;        // Profil-ID
    uint16_t slot_duration_ms;  // Slot-Dauer des Profils
    uint8_t  wpm;               // Morsegeschwindigkeit
    uint8_t  callsign_empty;    // 1 wenn Rufzeichen leer, sonst 0
    uint8_t  morse_repeats;     // Anzahl MO-Wiederholungen
};
```

Kompatibel = gleiche `protocol_ver` und gleicher `config_hash`.

## Ablauf

### Beacon-Intervall

500 ms ± 50 ms; der Jitter wird nach jedem Beacon einmal neu ausgewürfelt.

### Peers

- Bis zu 8 Einträge (auch fremde/inkompatible Geräte)
- Ein Peer gilt als **inaktiv**, wenn 3 s lang kein Beacon kam; abgelaufene Einträge
  werden wiederverwendet
- Neustart des Masters (neue `boot_id`):
  - **vor dem Einfrieren:** Zeitmessung *und* Plan werden verworfen (beide gehören zur
    alten Master-Uhr) und mit den Beacons der neuen Instanz neu aufgebaut; der
    Sendebeginn bleibt
  - **nach dem Einfrieren:** Messung und Plan bleiben, Beacons der neuen Instanz werden
    ignoriert. Der neu gestartete Fuchs übernimmt den Sendebeginn von den Slaves,
    berechnet aus denselben Füchsen denselben Plan und reiht sich ins Raster ein
- Pläne aus Beacons werden nur übernommen, wenn sie plausibel sind (1–5 Füchse,
  aufsteigende Fox-IDs 1–5, Slot > 0, Slot × Anzahl ≤ Zyklus)

### Zuhörphase und Sendebeginn

1. Nach dem Start hört ein Fuchs 1,5 s nur zu
2. Hat er in dieser Zeit einen Sendebeginn gehört (`FLAG_HAS_START`), übernimmt er ihn.
   Sonst schlägt er „eigener Sync-Start + 120 s" vor.
3. Jeder Fuchs sendet seinen bekannten Sendebeginn mit. Ein später eingeschalteter Fuchs
   verschiebt den Start deshalb nicht, auch wenn er Master wird.

### Master-Wahl

1. Sendet ein kompatibler Fuchs einen eingefrorenen Plan (`FLAG_MASTER |
   FLAG_PLAN_FROZEN`), ist er Master – der eigene Fuchs ist Nachzügler
2. Ein bestehender Master bleibt Master („klebrig"): Gewählt wird nur unter den Füchsen,
   die bereits `FLAG_MASTER` senden (bei zwei Mastern bleibt der mit der höheren MAC).
   Ein später eingeschalteter Fuchs übernimmt also nicht, und die laufende
   Zeitmessung der Slaves bleibt erhalten.
3. Gibt es keinen Master: höchste MAC (`memcmp(mac1, mac2, 6) > 0`) unter dem eigenen
   Fuchs und den Füchsen, die schon mindestens 1,5 s zu hören sind (also selbst fertig
   zugehört haben). Damit wird in der Regel der zuerst eingeschaltete Fuchs Master; bei
   gleichzeitigem Einschalten setzt sich die höhere MAC nach Regel 2 durch.

Fällt der Master vor dem Einfrieren aus, wird nach Regel 3 neu gewählt; der
Sendebeginn bleibt.

### Slotplan

- Der Master berechnet den Plan aus den Budgets aller aktiven, kompatiblen Füchse
  (siehe [`duty_cycle.md`](duty_cycle.md)) und sendet ihn in jedem Beacon
- Slaves übernehmen Zyklus, Slot, Reihenfolge und Sendebeginn des Masters

### Einfrieren und Ende

| Zeitpunkt               | Ereignis                                                     |
|-------------------------|--------------------------------------------------------------|
| Sendebeginn − 10 s      | Plan eingefroren: kein Masterwechsel, keine Planänderung mehr |
| Sendebeginn − 1 s       | Sync-Phase endet, WiFi wird abgeschaltet                     |
| Sendebeginn + Slot × i  | erste Aussendung des Fuchses in Slot i                       |

Menüpunkt 10 am Master setzt den Sendebeginn auf „jetzt + 5 s", frühestens aber 30 s
nach Beginn der Master-Rolle (Messzeit für die Drift der Slaves); nur vor dem Einfrieren.

### Verpasster Sendebeginn

Kennt ein Fuchs den Sendebeginn, hat aber bis dahin keinen Plan (z. B. Neustart kurz vor
dem Start), wird er nie mehr selbst Master: `missedStart()`, Fehlerzustand, kein Senden.
Hört ein neu gestarteter Fuchs gar keinen Verbund mehr (Neustart *nach* dem
Sendebeginn), kann er das nicht erkennen und arbeitet als Allein-Boot.

## Clock-Abgleich

Siehe [`time_sync.md`](time_sync.md). Kurz: Jeder Slave misst Paare (Master-Zeit,
lokale Empfangszeit) **nur vom aktuellen Master** und schätzt daraus Offset und
Taktabweichung. Alle Sendezeitpunkte werden aus der Master-Uhr in die lokale Uhr
umgerechnet.

## Verarbeitung im Empfänger

Der ESP-NOW-Empfangs-Callback läuft im WiFi-Task. Er nimmt nur den Zeitstempel und legt
Absender-MAC, Beacon und Zeitstempel in eine FreeRTOS-Queue (16 Einträge). Auswertung,
Regression und Ausgaben laufen in `processSync()` in der Hauptschleife.

## Fehlerzustände

### FOX_ID-Kollision

- Ein aktiver, kompatibler Sender mit gleicher Fox-Nummer
- Wird schon während der Sync-Phase per LED (SOS) und Log angezeigt
- Ergebnis zum Sendebeginn: kein Senden, CLI bleibt erreichbar, kein automatischer
  Neuversuch
- Behebung: Fox-Nummer über CLI ändern (Menüpunkt 2), dann alle Füchse gemeinsam neu
  starten (oder schon während der Sync-Phase korrigieren und nur diesen Fuchs neu
  starten)

### Konfigurations-Abweichung

- Ein aktiver Sender mit gleicher Protokollversion, aber anderem Config-Hash
- Wird schon während der Sync-Phase per LED (3× kurz) und Log angezeigt
- Ergebnis: 3× Blinken, kein Senden (zwei unabhängige Raster auf derselben Frequenz würden
  kollidieren), kein automatischer Neuversuch
- Kein automatisches Überschreiben der lokalen Konfiguration
- Behebung: Profile und Einstellungen aller Sender abgleichen

### Nicht im Slotplan

- Der Fuchs wurde erst nach dem Einfrieren eingeschaltet
- Ergebnis: 3× Blinken, kein Senden, kein automatischer Neuversuch
- Behebung: alle Füchse gemeinsam neu starten

## Allein-Boot

Wird kein anderer Fuchs erkannt:
- Eigener Master-Status, Slotplan mit einem Fuchs
- Sendebeginn 120 s nach dem Einschalten (Menüpunkt 10: in 5 s)
- Senden im maximal ISM-sicheren Zyklus
