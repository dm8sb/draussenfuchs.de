# ESP-NOW Synchronisationsprotokoll

## Übersicht

Das Synchronisationsprotokoll ermöglicht die zeitliche Abstimmung von bis zu 5 ARDF-Fuchsjagd-Sendern
im 70-cm-Band. Es basiert auf ESP-NOW (WiFi Direct, Kanal 1) und verwendet periodische Beacons zur
Zeitsynchronisation, Konfigurationsabgleich und Master-Wahl.

**Protokollversion:** 2

## Anwendungsbereich

- Gilt für Profile A, B und C
- Profil D (draussenfuchs Solo) nimmt nicht am Sync teil

## Beacon-Struktur

Die Beacons werden als gepackte Binärstruktur (packed struct) über ESP-NOW gesendet:

```cpp
struct __attribute__((packed)) SyncBeacon {
    uint8_t  protocol_ver;     // = 2
    uint8_t  profile_id;       // Profil-ID (0..3)
    uint8_t  fox_id;           // Fox-Nummer (1..5)
    uint8_t  flags;            // Bit-Maske (siehe unten)
    uint8_t  mac[6];           // Eigene MAC-Adresse
    uint32_t boot_id;          // Zufällige ID pro Boot
    uint64_t uptime_us;        // Eigene Uptime in µs
    uint64_t cycle_start_us;   // Zyklusstart-Zeitpunkt
    uint32_t config_hash;      // CRC32 über Konfigurationsparameter
    int16_t  drift_ppm_est;    // Geschätzte Taktdrift in ppm
    uint8_t  sync_quality;     // Synchronisationsqualität (0..100)
    uint8_t  active_count;     // Anzahl aktiver Füchse
    uint8_t  active_fox_ids[5];// IDs der aktiven Füchse
    uint16_t slot_ms;          // Effektive Slot-Dauer in ms
    uint16_t cycle_ms;         // Effektiver Zyklus in ms
};
```

### Flags-Feld (Bit-Maske)

| Bit | Bedeutung                           |
|----:|-------------------------------------|
|   0 | AFU-Modus aktiv (Rufzeichen gesetzt)|
|   1 | Duty-Guard aktiv (ISM-Regelwerk)    |
|   2 | Sender hat Master-Rolle             |
| 3–7 | Reserviert (immer 0)                |

## Boot-ID

Die `boot_id` ist eine zufällige 32-Bit-Ganzzahl, die bei jedem Systemstart neu generiert wird.
Sie dient zur Erkennung von Neustarts anderer Knoten: Ändert sich die `boot_id` eines bekannten
Peers, hat dieser in der Zwischenzeit einen Neustart durchgeführt.

## Beacon-Intervall

- Sendeintervall: 500 ms ± 50 ms zufälliger Jitter
- Der Jitter verhindert systematische Kollisionen zwischen gleichzeitig sendenden Beacons

## Sync-Fenster

- Nominale Dauer: 120 s (Profile A–C)
- Automatische Verlängerung auf bis zu 180 s, wenn aktive Messung läuft
- Nach Abschluss: WiFi und ESP-NOW werden vollständig deinitialisiert
- Kein periodischer Re-Sync nach dem Initialfenster

## Config-Hash

Der Config-Hash stellt sicher, dass alle Sender im Verbund identische Betriebsparameter verwenden.
Er wird als CRC32 über ein 8-Byte-Eingabe-Struct berechnet:

```cpp
struct __attribute__((packed)) ConfigHashInput {
    uint8_t  protocol_cfg_ver;  // = 2
    uint8_t  profile_id;        // Profil-ID
    uint8_t  fox_id;            // Fox-Nummer
    uint16_t slot_duration_ms;  // Slot-Dauer des Profils
    uint8_t  wpm;               // Morsegeschwindigkeit
    uint8_t  callsign_empty;    // 1 wenn Rufzeichen leer, sonst 0
    uint8_t  morse_repeats;     // Anzahl MO-Wiederholungen
};
```

## Master-Wahl

- Master = Fuchs mit der höchsten MAC-Adresse unter allen kompatiblen Sendern
- Kompatibilität erfordert: gleiche `protocol_ver` UND gleicher `config_hash`
- Vergleich: numerischer 6-Byte-Vergleich mittels `memcmp(mac1, mac2, 6) > 0`
- Der Master setzt den `cycle_start_us`-Zeitpunkt, an dem alle Sender das Zeitraster ausrichten

## Clock-Abgleich

### Messverfahren

1. Beacons enthalten den Sendezeitstempel `uptime_us`
2. Empfänger misst lokalen Empfangszeitpunkt `rx_local_us`
3. Offset-Berechnung: `offset_us = rx_local_us - uptime_us`
4. Sammlung von Offset-Werten in einem Ringpuffer (max. 120 Werte)

### Ausreißer-Filterung

- Berechnung des Medians aller Offsets
- Berechnung der MAD (Median Absolute Deviation)
- Ausreißer: |offset - median| > 3 × MAD werden verworfen

### Lineare Drift-Regression

- Mindestens 30 Messpunkte erforderlich
- Lineare Regression: `offset = a + b × local_time`
- Steigung `b` ergibt die Drift in ppm: `drift_ppm = b × 10⁶`
- Plausibilitätsprüfung: |drift_ppm| < 500 ppm

### Qualitäts-Bewertung

- 0–59: Nicht genügend Daten
- 60–100: Gültige Schätzung (basierend auf Ausreißer-Rate)
- Mindestqualität für NVS-Speicherung: 60

## Fehlerzustände

### FOX_ID-Kollision

- Zwei kompatible Sender mit gleicher Fox-Nummer erkannt
- Ergebnis: SOS-Blinkmuster, kein Senden
- Behebung: Fox-Nummer über CLI ändern (Menüpunkt 2)

### Konfigurations-Abweichung

- Sender mit unterschiedlichem Config-Hash erkannt
- Ergebnis: 3× Blinken, kein Senden
- Kein automatisches Überschreiben der lokalen Konfiguration
- Behebung: Profile und Einstellungen aller Sender abgleichen

## Allein-Boot

Wird kein anderer Fuchs im Sync-Fenster erkannt:
- Eigener Master-Status
- `active_count = 1`
- Senden im maximal ISM-sicheren Zyklus mit Duty-Guard
