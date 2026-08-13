# ARDF-Fuchsjagd-Sender ESP32-C3

**Version 2.0.0** – Juli 2026

Ein vollständiger ARDF-Fuchsjagd-Sender für das 70-cm-Band (433,920 MHz) auf Basis des
ESP32-C3 mit PlatformIO, ESP-NOW-Synchronisation, dynamischen Profilen, Duty-Guard und
deutschsprachiger serieller Konfiguration.

## Herkunft / Attribution

Dieses Projekt ist eine **vollständige Weiterentwicklung** des
„Very basic 70cm-ISM fox-transmitter" von
**Harm  , DK4HAA – [draussenfuchs.de](https://draussenfuchs.de/)**
(MIT, 2025).

Ab 2026 komplett neu strukturiert, erweitert und gepflegt durch
**Sebastian , DM8SB**.

Unterschiede zur Original-Basis:

- Original: ~94 LOC Arduino INO, 880 Hz Dauerton, 200 ms TX / 1800 ms Sleep,
  keine Kennung, kein Sync, kein ARDF
- Diese Version: PlatformIO, ESP32-C3, 5-Fuchs ARDF mit MOE/MOI/MOS/MOH/MO5,
  ESP-NOW-Zeitsynchronisation, Duty-Guard <9,5 % (ISM),
  Profile A/B/C/D, deutschsprachige CLI, NVS-Persistenz,
  Zeitdrift-Kompensation, konsequenter Deep-Sleep

Lizenz: MIT – siehe `LICENSE`, `NOTICE`, `AUTHORS.md`.

Erstellung mit KI-Assistenz, fachliche Verantwortung und Freigabe: DM8SB.

---

## Inhaltsverzeichnis

- [Hardware-Verdrahtung](#hardware-verdrahtung)
- [Rechtliche Hinweise](#rechtliche-hinweise)
- [Profile](#profile)
- [Dynamische Zykluszeiten](#dynamische-zykluszeiten)
- [Synchronisation und Verteilung](#synchronisation-und-verteilung)
- [Serielle CLI-Befehle](#serielle-cli-befehle)
- [LED- und Fehlercodes](#led--und-fehlercodes)
- [Driftkorrektur](#driftkorrektur)
- [Build und Flash](#build-und-flash)
- [Lizenz](#lizenz)

---

## Hardware-Verdrahtung

### Basis

Die Hardware basiert auf der [draussenfuchs.de](https://draussenfuchs.de/)-Plattform:
- **MCU:** ESP32-C3 Super Mini oder Lolin C3 Mini
- **TX-Modul:** Qiachip WL102-341 (433,920 MHz, ASK/OOK, ca. 10 mW)

### Anschlussbelegung

| ESP32-C3 Pin | Funktion              | Verbindung            |
|:-------------|:----------------------|:----------------------|
| GPIO3        | TX-Daten (DATA_PIN)   | WL102-341 DATA        |
| GPIO4        | TX-VCC (TX_VCC_EN)    | WL102-341 VCC-Steuerung |
| GPIO7        | LED 1 (active LOW)    | Eingebaute LED (Lolin C3 Mini) |
| GPIO8        | LED 2 (active LOW)    | Eingebaute LED (Super Mini)    |
| 3V3          | Versorgung            | WL102-341 VCC (über GPIO4)     |
| GND          | Masse                 | WL102-341 GND                  |

### Hinweise

- Das TX-Modul wird über GPIO4 eingeschaltet (HIGH = an, LOW = aus/stromlos)
- Die LEDs auf GPIO7 und GPIO8 werden parallel angesteuert und sind active LOW
- Keine DIP-Schalter – Konfiguration ausschließlich per serieller CLI

---

## Rechtliche Hinweise

### ISM-Betrieb (Standard)

- **Frequenz:** 433,920 MHz (ISM/SRD-Band 433,050–434,790 MHz)
- **Sendeleistung:** ca. 10 mW ERP
- **Duty Cycle:** < 10 % gemäß Allgemeinzuteilung SRD/ISM
- **Duty-Guard:** Strikt auf < 9,5 % limitiert (5 % Sicherheitsaufschlag)
- **Rufzeichen:** Kein Rufzeichen im ISM-Betrieb (CALLSIGN = "")
- Kein Peil-Dauerton in Profilen A/B/C
- Ausschließlich standardisierte kurze Morse-Kennungen

### AFU-Betrieb (optional)

- **Rufzeichen (CALLSIGN)** nur für lizenzierte Funkamateure
- Das Setzen eines Rufzeichens aktiviert den AFU-Modus
- Rufzeichensendung: Jeden 10. Slot (MO-Kennung + 7 dits Pause + Rufzeichen)
- **Ausnahmeregelung Duty im 10. Slot:** Duty-Guard-Grenze wird auf max. 50 % angehoben
  (gesetzliche 10-%-ISM-Einschränkung gilt im Amateurfunkbetrieb nicht in derselben Form)
- In den Slots 1–9 bleibt die strenge 9,5-%-Grenze aktiv
- **Warnung:** Im ISM-Betrieb das Rufzeichen leer lassen!

---

## Profile

| Eigenschaft       | A – Standard    | B – Sprint      | C – Ultra (Default) | D – Solo         |
|:------------------|:----------------|:----------------|:--------------------|:-----------------|
| Füchse            | 5               | 5               | 5                   | 1                |
| Zyklus            | 60 s            | 30 s            | 20 s                | 2,4 s            |
| Slot              | 12 s            | 6 s             | 4 s                 | 2,4 s            |
| WPM               | 18              | 20              | 24                  | –                |
| MO-Wiederholungen | 2×              | 1×              | 1×                  | –                |
| Sendeinhalt       | MO-Kennung      | MO-Kennung      | MO-Kennung          | Dauerton         |
| TX-Dauer (ca.)    | 3,8–4,9 s       | 1,5–2,0 s       | 1,3–1,7 s           | 200 ms           |
| Duty              | 6,3–8,1 %       | 5,1–6,6 %       | 6,3–8,3 %           | 8,33 %           |
| ESP-NOW           | Ja              | Ja              | Ja                  | Nein             |
| CLI-Timeout       | 120 s           | 120 s           | 120 s               | 30 s             |

### Profil D: draussenfuchs Solo

- Profil D: draussenfuchs-ähnlich, ISM-sicher, Solo, fox-spezifische Tonhöhe, keine ARDF-Synchronisation
- Dauerton: 200 ms ON / 2.200 ms OFF
- Ton: Fox-spezifisch (700/800/900/1000/1200 Hz je nach Fox-ID)
- Keine MO-Kennung, kein ESP-NOW, kein Sync
- Duty-Cycle: exakt 8,33 %

---

## Dynamische Zykluszeiten

Bei weniger als 5 aktiven Füchsen passt sich der Zyklus automatisch an die maximale
ISM-sichere Wiederholrate an.

### Profil A (18 WPM, 2× MO)

| Aktive Füchse | Gewünschter Zyklus | Effektiver Zyklus | Duty (Worst Case) |
|--------------:|-------------------:|------------------:|------------------:|
|             1 |          12.000 ms |       ~54.200 ms  |          ~9,5 %   |
|             2 |          24.000 ms |       ~54.200 ms  |          ~9,5 %   |
|             3 |          36.000 ms |       ~54.200 ms  |          ~9,5 %   |
|             4 |          48.000 ms |       ~54.200 ms  |          ~9,5 %   |
|             5 |          60.000 ms |        60.000 ms  |          ~8,1 %   |

### Profil B (20 WPM, 1× MO)

| Aktive Füchse | Gewünschter Zyklus | Effektiver Zyklus | Duty (Worst Case) |
|--------------:|-------------------:|------------------:|------------------:|
|             1 |           6.000 ms |       ~22.100 ms  |          ~9,5 %   |
|             2 |          12.000 ms |       ~22.100 ms  |          ~9,5 %   |
|             3 |          18.000 ms |       ~22.100 ms  |          ~9,5 %   |
|             4 |          24.000 ms |        24.000 ms  |          ~8,7 %   |
|             5 |          30.000 ms |        30.000 ms  |          ~7,0 %   |

### Profil C (24 WPM, 1× MO) – Default

| Aktive Füchse | Gewünschter Zyklus | Effektiver Zyklus | Duty (Worst Case) |
|--------------:|-------------------:|------------------:|------------------:|
|             1 |           4.000 ms |       ~18.500 ms  |          ~9,5 %   |
|             2 |           8.000 ms |       ~18.500 ms  |          ~9,5 %   |
|             3 |          12.000 ms |       ~18.500 ms  |          ~9,5 %   |
|             4 |          16.000 ms |       ~18.500 ms  |          ~9,5 %   |
|             5 |          20.000 ms |        20.000 ms  |          ~8,8 %   |

*Werte gerundet; tatsächliche Zeiten hängen von der Fox-ID ab (MO5 ist am längsten).*

---

## Synchronisation und Verteilung

### Schritt-für-Schritt-Anleitung

1. **Vorbereitung:**
   - Alle Sender mit identischem Profil (A, B oder C) konfigurieren
   - Jedem Sender eine eindeutige Fox-Nummer (1..5) zuweisen
   - Rufzeichen nur setzen, wenn Amateurfunklizenz vorhanden

2. **Einschalten:**
   - Alle Sender innerhalb weniger Sekunden einschalten
   - Die Discovery-/Sync-Phase startet automatisch (120 s)
   - Die LEDs blinken im Herzschlag-Takt (2 Hz)

3. **Synchronisation:**
   - Die Sender tauschen automatisch ESP-NOW-Beacons aus
   - Master-Wahl erfolgt automatisch (höchste MAC-Adresse)
   - Fehler werden über LED-Codes und Serial angezeigt

4. **Überprüfung (optional):**
   - Enter drücken, um die CLI zu öffnen
   - Menüpunkt 6 (Sync-Status) zeigt erkannte Nachbarn
   - Menüpunkt 5 (Duty-Cycle) zeigt die berechneten Zeiten

5. **Start:**
   - Menüpunkt 10 oder Warten auf Auto-Timeout (120 s)
   - Die Sender beginnen synchron im zugewiesenen Zeitraster

6. **Verteilung im Gelände:**
   - Nach dem Start die Sender an den gewünschten Positionen aufstellen
   - Keine weitere Konfiguration notwendig
   - Die Synchronisation bleibt über die Driftkorrektur erhalten

### Allein-Boot (einzelner Sender)

- Wird kein Nachbar erkannt, sendet der Fuchs autonom
- `active_count = 1`, eigener Master-Status
- Maximale ISM-sichere Wiederholrate wird automatisch berechnet

---

## Serielle CLI-Befehle

Verbindung: 115200 Baud, 8N1, USB-CDC

```
ARDF-Fuchsjagd-Sender – Konfiguration
-------------------------------------
 1) Aktuelle Konfiguration anzeigen
 2) Fox-Nummer ändern (1..5)
 3) Profil ändern (A=Standard, B=Sprint, C=Ultra, D=draussenfuchs Solo)
 4) Rufzeichen ändern/löschen
 5) Duty-Cycle berechnen/anzeigen
 6) Sync-Status anzeigen
 7) Zeitkorrekturwerte anzeigen
 8) Zeitkorrekturwerte löschen
 9) Werkseinstellungen laden
10) Normalbetrieb starten und CLI verlassen
11) Neustart
 h) Hilfe
```

### Hinweis zur seriellen Schnittstelle

- **ESP32-C3 Super Mini** (nativer USB-Serial/JTAG): Build-Flags
  `ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_USB_MODE=1` (bereits konfiguriert)
- **Lolin C3 Mini** (CH340/CH9102 USB-to-Serial): Ggf. `ARDUINO_USB_MODE=0`
  in der `platformio.ini` setzen

Vollständige CLI-Dokumentation: [`docs/cli.md`](docs/cli.md)

---

## LED- und Fehlercodes

| Zustand                 | LED-Muster                              | TX aktiv? |
|:------------------------|:----------------------------------------|:----------|
| Normalbetrieb TX        | Synchron im Ton-Takt                    | Ja        |
| CLI aktiv / Idle        | 0,5 Hz langsam (500 ms an / 1500 ms aus)| Nein      |
| Boot / Sync läuft       | 2 Hz Herzschlag                         | Nein      |
| FOX_ID-Kollision        | SOS (…---…) wiederholt, Pause 1,2 s     | Nein      |
| Profil-/Config-Mismatch | 3× kurz (150 ms), Pause 1 s             | Nein      |
| Ungültige Konfiguration | 10 Hz Dauerblinken                       | Nein      |
| Duty-Guard blockiert    | 2× kurz, Pause 2 s                      | Nein      |

### Fehler-Erholung

- Alle Fehlerzustände werden über Serial mit `[ERR]`-Prefix ausgegeben
- Nach 60 Sekunden im Fehlerzustand erfolgt ein automatischer Neustart
- Manueller Neustart jederzeit über Menüpunkt 11

---

## Driftkorrektur

### Funktionsweise

- Während der Sync-Phase werden Clock-Offsets zwischen den Sendern gemessen
- Lineare Regression berechnet die Drift in ppm
- Ausreißer-Filterung über Median / MAD (Median Absolute Deviation)
- Ergebnisse werden im NVS gespeichert und beim nächsten Start wiederverwendet

### Vorlaufberechnung

```
vorlauf = 80 ms + |drift_ppm| × elapsed_s + 20 ms
```

Begrenzt auf 100 ms ≤ Vorlauf ≤ 350 ms.

### Empfehlung für längere Einsätze

Bei Einsätzen über **60 Minuten** wird ein Neustart (Aus-/Einschalten) aller Sender
empfohlen. Dadurch wird eine frische Sync-Phase mit neuer Driftmessung durchgeführt.
Die im NVS gespeicherten Driftwerte dienen als Initialwerte.

Ausführliche Dokumentation: [`docs/time_sync.md`](docs/time_sync.md)

---

## Build und Flash

### Voraussetzungen

- [PlatformIO](https://platformio.org/) (CLI oder IDE-Extension)
- ESP32-C3-Board via USB angeschlossen

### Kompilieren

```bash
pio run -e lolin_c3_mini
```

### Flash

```bash
pio run -e lolin_c3_mini --target upload
```

### Serial Monitor

```bash
pio device monitor -b 115200
```

### Tests

```bash
pio test
```

### Hinweis: Deep-Sleep und Flashing

Dieses Programm verwendet Deep-Sleep. Im Deep-Sleep ist der ESP32-C3 nicht als
serielles Gerät erreichbar. Zum erneuten Flashen:

1. **Boot**-Taste gedrückt halten
2. **RST**-Taste kurz drücken
3. **Boot**-Taste loslassen
4. Flash-Vorgang starten

### Plattformversion

`espressif32 @ ^6.8.0` stellt sicher, dass Arduino ESP32 Core ≥ 3.0 verwendet wird.
Dieser Core unterstützt die neue pin-basierte LEDC-API (`ledcAttach`, `ledcWriteTone`).

---

## Lizenz

MIT License – siehe [`LICENSE`](LICENSE), [`NOTICE`](NOTICE), [`AUTHORS.md`](AUTHORS.md).

Copyright (c) 2025 Harm  , DK4HAA – [draussenfuchs.de](https://draussenfuchs.de/)
Copyright (c) 2026 Sebastian , DM8SB
