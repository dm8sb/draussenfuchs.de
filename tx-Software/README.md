# ARDF-Fuchsjagd-Sender ESP32-C3

**Version 2.1.0** – Oktober 2026

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
  ESP-NOW-Synchronisation mit gemeinsamem Slotplan, Duty-Guard < 9,5 % (ISM),
  Profile A/B/C/D, deutschsprachige CLI, NVS-Persistenz,
  Zeitdrift-Kompensation, Deep-Sleep im Solo-Profil D

Lizenz: MIT – siehe `LICENSE` und `AUTHORS.md`.

Erstellung mit KI-Assistenz, fachliche Verantwortung und Freigabe: DM8SB.

> **Update von 2.0.x:** Das Sync-Protokoll ist inkompatibel geändert (Version 3).
> Alle Füchse eines Verbunds müssen dieselbe Firmware haben. Die gespeicherte
> Konfiguration (Fox-ID, Profil, Rufzeichen) wird beim ersten Start übernommen.

---

## Inhaltsverzeichnis

- [Hardware-Verdrahtung](#hardware-verdrahtung)
- [Rechtliche Hinweise](#rechtliche-hinweise)
- [Profile](#profile)
- [Dynamische Zykluszeiten](#dynamische-zykluszeiten)
- [Synchronisation und Verteilung](#synchronisation-und-verteilung)
- [Serielle CLI-Befehle](#serielle-cli-befehle)
- [LED- und Fehlercodes](#led--und-fehlercodes)
- [Zeitsynchronisation und Stromverbrauch](#zeitsynchronisation-und-stromverbrauch)
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
- Im Deep-Sleep werden GPIO3 und GPIO4 per Pin-Hold auf LOW gehalten
- Die LEDs auf GPIO7 und GPIO8 werden parallel angesteuert und sind active LOW.
  Dieselbe Firmware läuft deshalb auf beiden Boards.
- Lolin C3 Mini: Je nach Board-Revision sitzt an GPIO7 eine RGB-LED (WS2812), die auf
  einfache Pegel nicht reagiert. Dann bleibt die LED-Anzeige dunkel (bitte am eigenen
  Board prüfen).
- Keine DIP-Schalter – Konfiguration ausschließlich per serieller CLI

---

## Rechtliche Hinweise

Die Firmware setzt die folgenden Grenzen technisch um. Ob sie für den eigenen Betrieb
genügen, muss der Betreiber selbst prüfen; die Angaben sind keine Rechtsberatung.

### ISM-Betrieb (Standard)

- **Frequenz:** 433,920 MHz (ISM/SRD-Band 433,050–434,790 MHz)
- **Sendeleistung:** ca. 10 mW ERP
- **Duty Cycle:** < 10 % gemäß Allgemeinzuteilung SRD/ISM
- **Duty-Guard:** Strikt auf ≤ 9,5 % limitiert, gerechnet mit 5 % Aufschlag auf das
  Sendezeit-Budget (Zeit, in der das TX-Modul versorgt ist: 20 ms Vorlauf + komplette
  Morsesequenz inkl. Pausen). Vor jeder Aussendung prüft ein Laufzeit-Guard zusätzlich
  den Abstand zur vorherigen Aussendung.
- **Rufzeichen:** Kein Rufzeichen im ISM-Betrieb (CALLSIGN = "")
- Kein Peil-Dauerton in Profilen A/B/C
- Ausschließlich standardisierte kurze Morse-Kennungen

### AFU-Betrieb (optional)

- **Rufzeichen (CALLSIGN)** nur für lizenzierte Funkamateure
- Das Setzen eines Rufzeichens aktiviert den AFU-Modus
- Rufzeichensendung: Bei jeder 10. Aussendung (MO-Kennung + 7 dits Pause + Rufzeichen)
- **Duty im Rufzeichen-Slot:** Die Firmware erlaubt dort bis 50 % (die
  10-%-ISM-Einschränkung gilt im Amateurfunkbetrieb nicht in derselben Form). Bei allen
  anderen Aussendungen bleibt die 9,5-%-Grenze aktiv.
- Damit die Rufzeichen-Aussendung nicht in den Slot des nächsten Fuchses ragt, wird der
  Slot im AFU-Modus entsprechend verlängert (siehe [AFU-Modus](#afu-modus))
- Vom Betreiber zu prüfen: Kennungsintervall und Regeln für den unbemannten Betrieb
  nach AFuV (bei Profil A mit einem Fuchs liegen zwischen zwei Rufzeichen ca. 9 min)
- **Warnung:** Im ISM-Betrieb das Rufzeichen leer lassen!

---

## Profile

| Eigenschaft        | A – Standard    | B – Sprint      | C – Ultra (Default) | D – Solo         |
|:-------------------|:----------------|:----------------|:--------------------|:-----------------|
| Füchse             | 5               | 5               | 5                   | 1                |
| Zyklus (5 Füchse)  | 60 s            | 30 s            | 20 s                | 2,43 s           |
| Slot               | 12 s            | 6 s             | 4 s                 | –                |
| WPM                | 18              | 20              | 24                  | –                |
| MO-Wiederholungen  | 2×              | 1×              | 1×                  | –                |
| Sendeinhalt        | MO-Kennung      | MO-Kennung      | MO-Kennung          | Dauerton         |
| TX-Budget          | 3,8–4,8 s       | 1,5–2,0 s       | 1,3–1,7 s           | 220 ms           |
| Duty (5 Füchse)    | 6,6–8,5 %       | 5,3–7,0 %       | 6,7–8,8 %           | 9,5 % (Ton 8,2 %)|
| ESP-NOW            | Ja              | Ja              | Ja                  | Nein             |
| Sendebeginn        | nach Sync (Master) | nach Sync (Master) | nach Sync (Master) | 30 s Inaktivität |
| Betrieb            | wach, WiFi aus  | wach, WiFi aus  | wach, WiFi aus      | Deep-Sleep       |

*TX-Budget und Duty je nach Fox-ID (MOE kürzeste, MO5 längste Kennung); Duty inkl.
5 % Sicherheitsaufschlag.*

### Profil D: draussenfuchs Solo

- Profil D: draussenfuchs-ähnlich, ISM-sicher, Solo, fox-spezifische Tonhöhe, keine ARDF-Synchronisation
- Dauerton: 200 ms ON (+ 20 ms Vorlauf), 2.212 ms Pause
- Ton: Fox-spezifisch (700/800/900/1000/1200 Hz je nach Fox-ID)
- Keine MO-Kennung, kein ESP-NOW, kein Sync
- Duty-Cycle: 9,5 % nach Budget-Definition inkl. Aufschlag, reine Tonzeit ca. 8,2 %
- Die CLI ist im Betrieb nur nach einem Reset (RST-Taste) erreichbar

---

## Dynamische Zykluszeiten

Bei weniger als 5 aktiven Füchsen verkürzt sich der Zyklus bis zur maximalen
ISM-sicheren Wiederholrate. Maßgeblich ist die **längste Aussendung im Verbund**, alle
Füchse verwenden denselben Slotplan. Die Tabellen zeigen den ungünstigsten Fall (MO5 ist
aktiv); ohne MO5 wird der Zyklus etwas kürzer.

### Profil A (18 WPM, 2× MO)

| Aktive Füchse | Gewünschter Zyklus | Effektiver Zyklus | Slot      | Duty   |
|--------------:|-------------------:|------------------:|----------:|-------:|
|             1 |          12.000 ms |         53.488 ms | 53.488 ms | 9,5 %  |
|             2 |          24.000 ms |         53.488 ms | 26.744 ms | 9,5 %  |
|             3 |          36.000 ms |         53.488 ms | 17.829 ms | 9,5 %  |
|             4 |          48.000 ms |         53.488 ms | 13.372 ms | 9,5 %  |
|             5 |          60.000 ms |         60.000 ms | 12.000 ms | 8,5 %  |

### Profil B (20 WPM, 1× MO)

| Aktive Füchse | Gewünschter Zyklus | Effektiver Zyklus | Slot      | Duty   |
|--------------:|-------------------:|------------------:|----------:|-------:|
|             1 |           6.000 ms |         22.118 ms | 22.118 ms | 9,5 %  |
|             2 |          12.000 ms |         22.118 ms | 11.059 ms | 9,5 %  |
|             3 |          18.000 ms |         22.118 ms |  7.372 ms | 9,5 %  |
|             4 |          24.000 ms |         24.000 ms |  6.000 ms | 8,8 %  |
|             5 |          30.000 ms |         30.000 ms |  6.000 ms | 7,0 %  |

### Profil C (24 WPM, 1× MO) – Default

| Aktive Füchse | Gewünschter Zyklus | Effektiver Zyklus | Slot      | Duty   |
|--------------:|-------------------:|------------------:|----------:|-------:|
|             1 |           4.000 ms |         18.469 ms | 18.469 ms | 9,5 %  |
|             2 |           8.000 ms |         18.469 ms |  9.234 ms | 9,5 %  |
|             3 |          12.000 ms |         18.469 ms |  6.156 ms | 9,5 %  |
|             4 |          16.000 ms |         18.469 ms |  4.617 ms | 9,5 %  |
|             5 |          20.000 ms |         20.000 ms |  4.000 ms | 8,8 %  |

*Werte aus dem Code berechnet (inkl. einer Reserve von 100 ppm + 10 ms für Taktabweichungen).
Menüpunkt 5 der CLI zeigt die Werte für die eigene Konfiguration.*

### AFU-Modus

Der Slot ist mindestens so lang wie die längste Aussendung im Verbund plus 500 ms
Schutzabstand. Mit Rufzeichen wird der Zyklus deshalb länger, z. B. bei 5 Füchsen und
dem Rufzeichen „DL1ABC/P":

| Profil | Längste Aussendung | Slot     | Zyklus    |
|:-------|-------------------:|---------:|----------:|
| A      |          12.098 ms | 12.598 ms | 62.990 ms |
| B      |           8.600 ms |  9.100 ms | 45.500 ms |
| C      |           7.170 ms |  7.670 ms | 38.350 ms |

---

## Synchronisation und Verteilung

### Schritt-für-Schritt-Anleitung

1. **Vorbereitung:**
   - Alle Sender mit identischem Profil (A, B oder C) konfigurieren
   - Jedem Sender eine eindeutige Fox-Nummer (1..5) zuweisen
   - Rufzeichen nur setzen, wenn Amateurfunklizenz vorhanden (dann bei allen Füchsen)

2. **Einschalten:**
   - Alle Sender möglichst gemeinsam einschalten – **spätestens 15 s vor dem
     Sendebeginn** (10 s vorher wird der Slotplan festgelegt). Für eine genaue
     Messung der Taktabweichung sollte jeder Fuchs **mindestens 30 s** vor dem
     Sendebeginn laufen; sonst wird nur der Offset gemessen.
   - Die Sync-Phase startet automatisch, die LEDs blinken im Herzschlag-Takt (2 Hz)
   - Eine Kollision oder Konfigurations-Abweichung zeigt die LED schon während der
     Sync-Phase (SOS bzw. 3× kurz) – dann vor dem Sendebeginn korrigieren

3. **Synchronisation:**
   - Die Sender tauschen ESP-NOW-Beacons aus (2 pro Sekunde)
   - Master ist der zuerst eingeschaltete Sender (bei gleichzeitigem Einschalten der
     mit der höchsten MAC-Adresse); er bleibt Master, solange er zu hören ist
   - Der Master legt den Slotplan fest (Zyklus, Slot, Reihenfolge nach Fox-ID) und den
     **Sendebeginn: 120 s nach dem Einschalten des ersten Fuchses**. Später
     eingeschaltete Füchse übernehmen diesen Zeitpunkt.
   - Jeder Slave misst dabei Offset und Taktabweichung seiner Uhr gegenüber dem Master
   - Fehler werden über LED-Codes und Serial angezeigt

4. **Überprüfung (optional):**
   - Enter drücken, um die CLI zu öffnen
   - Menüpunkt 6 (Sync-Status) zeigt Master, Slotplan, Sendebeginn und Nachbarn
   - Menüpunkt 5 (Duty-Cycle) zeigt die berechneten Zeiten

5. **Start:**
   - Automatisch zum Sendebeginn; Menüpunkt 10 am Master zieht ihn für alle vor
     (Start nach 5 s, frühestens aber 30 s nach Beginn der Messung)
   - Alle Sender senden im gemeinsamen Zeitraster, jeder in seinem Slot

6. **Verteilung im Gelände:**
   - Nach dem Start die Sender an den gewünschten Positionen aufstellen
   - Keine weitere Konfiguration notwendig, kein Funkkontakt zwischen den Füchsen nötig
   - Die Sender bleiben über die Quarzuhr und die gemessene Taktabweichung synchron
     (Simulation mit voller Messdauer: nach 3 h wenige Millisekunden; ohne
     Driftmessung je nach Quarz bis einige 100 ms pro Stunde). Temperaturänderungen
     zwischen Messung und Einsatz sind nicht berücksichtigt.

### Allein-Boot (einzelner Sender)

- Wird kein Nachbar erkannt, ist der Fuchs sein eigener Master und sendet autonom
- Die maximale ISM-sichere Wiederholrate wird automatisch berechnet

### Nachzügler und Neustarts

- Ein Fuchs, der erst nach dem Festlegen des Slotplans eingeschaltet wird, ist nicht im
  Plan. Er sendet dann nicht (LED 3× kurz), statt das Raster der anderen zu stören.
- Startet ein Fuchs kurz vor dem Sendebeginn neu, sodass er den Plan nicht mehr
  bekommt, sendet er ebenfalls nicht.
- **Wichtig:** Nach dem Sendebeginn sind die Füchse funkstill. Ein Fuchs, der danach
  eingeschaltet oder neu gestartet wird (auch RST-Taste, Unterspannung), hört keinen
  Verbund und sendet als Allein-Boot **unsynchronisiert**. Deshalb alle Füchse vor dem
  Sendebeginn einschalten und bei Problemen **alle Füchse gemeinsam neu starten**.

---

## Serielle CLI-Befehle

Verbindung: 115200 Baud, 8N1, USB-CDC. Das Terminal sollte UTF-8 darstellen (Umlaute,
Gedankenstriche).

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

- Änderungen an Fox-Nummer, Profil, Rufzeichen oder Werkseinstellungen werden erst nach
  einem **Neustart** wirksam; die CLI fragt danach, und bis dahin ist der Normalbetrieb
  gesperrt
- Rückfragen ohne Antwort brechen nach 30 s ab (Wert unverändert)

### Hinweis zur seriellen Schnittstelle

- **ESP32-C3 Super Mini** und **Lolin C3 Mini** nutzen den nativen USB-Serial/JTAG des
  ESP32-C3. Die Build-Flags `ARDUINO_USB_CDC_ON_BOOT=1` und `ARDUINO_USB_MODE=1` sind
  bereits gesetzt.

Vollständige CLI-Dokumentation: [`docs/cli.md`](docs/cli.md)

---

## LED- und Fehlercodes

| Zustand                         | LED-Muster                               | TX aktiv? |
|:--------------------------------|:-----------------------------------------|:----------|
| Normalbetrieb TX                | Synchron im Ton-Takt, sonst aus          | Ja        |
| Startphase mit Sync (A–C)       | 2 Hz Herzschlag (250 ms an / 250 ms aus) | Nein      |
| Startphase ohne Sync (D)        | 0,5 Hz (500 ms an / 1500 ms aus)         | Nein      |
| FOX_ID-Kollision                | SOS (···−−−···) wiederholt, Pause 1,2 s  | Nein      |
| Konfigurations-Abweichung       | 3× kurz (150 ms), Pause 1 s              | Nein      |
| Nicht im Slotplan / Sendebeginn verpasst / Neustart ausstehend | 3× kurz (150 ms), Pause 1 s | Nein |
| Ungültige Konfiguration / ESP-NOW-Fehler | 10 Hz Dauerblinken              | Nein      |
| Duty-Guard blockiert            | 2× kurz, Pause 2 s (bis zur nächsten erlaubten Aussendung) | Nein |

### Fehler-Erholung

- Alle Fehlerzustände werden über Serial mit `[ERR]`-Prefix ausgegeben
- Im Fehlerzustand bleibt die CLI bedienbar (Enter): Konfiguration prüfen bzw. ändern
- Kollision, Abweichung, „nicht im Slotplan" und ein verpasster Sendebeginn werden erst
  zum Sendebeginn festgestellt; die anderen Füchse senden dann bereits. Ein Neustart
  dieses einen Fuchses würde unsynchronisiert senden – deshalb kein automatischer
  Neuversuch: **Ursache beheben und alle Füchse gemeinsam neu starten.**
- ESP-NOW-Startfehler (beim Booten, die anderen sind noch im Sync): nach 60 s ohne
  Eingabe automatischer Neuversuch, höchstens 5-mal
- Danach Tiefschlaf ohne Senden bis zum Reset (RST-Taste), damit der Akku nicht leerläuft
- Wurde die Konfiguration geändert, aber nicht neu gestartet, und läuft ein Verbund an,
  geht der Fuchs in den Fehlerzustand statt allein neu zu starten

---

## Zeitsynchronisation und Stromverbrauch

### Funktionsweise

- Während der Sync-Phase misst jeder Slave Paare aus Master-Zeit und Empfangszeit
- Ausreißer werden per Median/MAD verworfen, eine lineare Regression liefert Offset und
  Taktabweichung (ppm) gegenüber dem Master
- Jeder Sendezeitpunkt wird absolut aus dem Slotplan berechnet und in die lokale Uhr
  umgerechnet – Fehler summieren sich nicht auf
- Im ARDF-Betrieb bleibt der Fuchs nach der Sync-Phase **wach** (WiFi aus, CPU 80 MHz),
  damit alle Zeiten über den Quarz laufen. Der Deep-Sleep-Timer läuft auf einem internen
  RC-Oszillator, der für ein gemeinsames Slotraster zu ungenau ist.
- Die letzte Messung wird zur Anzeige im NVS gespeichert (Menüpunkt 7); beim nächsten
  Einsatz wird neu gemessen, weil die Abweichung relativ zum jeweiligen Master gilt

### Stromverbrauch

- **Profile A–C:** dauerhaft einige mA (wach bei 80 MHz, WiFi aus; erwartet ca.
  10–20 mA, auf der eigenen Hardware messen) plus TX-Modul während der Aussendungen.
  Während der Sync-Phase ist WiFi aktiv (deutlich mehr).
- **Profil D:** Deep-Sleep zwischen den Aussendungen (µA-Bereich)

Ausführliche Dokumentation: [`docs/time_sync.md`](docs/time_sync.md),
[`docs/sync_protocol.md`](docs/sync_protocol.md), [`docs/duty_cycle.md`](docs/duty_cycle.md)

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

Die hardwareunabhängige Logik (Morse-Timing, Duty-Guard, Slotplan, Zeitmodell,
Verbund-Simulation, CRC) wird nativ auf dem PC getestet:

```bash
pio test -e native       # GCC im PATH (Linux, macOS, Windows mit MinGW)
pio test -e native_win   # Windows ohne GCC: PlatformIO lädt MinGW selbst
```

### Hinweis: Deep-Sleep und Flashing

Profil D (und der Tiefschlaf nach dauerhaften Fehlern) verwendet Deep-Sleep. Im
Deep-Sleep ist der ESP32-C3 nicht als serielles Gerät erreichbar. Zum erneuten Flashen:

1. **Boot**-Taste gedrückt halten
2. **RST**-Taste kurz drücken
3. **Boot**-Taste loslassen
4. Flash-Vorgang starten

### Plattformversion

`espressif32 @ 6.13.0` ist exakt gepinnt und liefert den Arduino-ESP32-Core **2.0.17**
(kanalbasierte LEDC-API `ledcSetup`/`ledcAttachPin`). Der Code enthält zusätzlich eine
Kompatibilitätsschicht für Core 3.x (`ledcAttach`), die aber nicht getestet ist.

### Fehlerbehebung

- `ModuleNotFoundError: No module named 'intelhex'` beim Erzeugen von `bootloader.bin`:
  Der PlatformIO-Umgebung fehlt ein Python-Paket. Abhilfe:
  `~/.platformio/penv/Scripts/python -m pip install intelhex` (Windows) bzw.
  `~/.platformio/penv/bin/python -m pip install intelhex`, alternativ PlatformIO
  aktualisieren.

---

## Lizenz

MIT License – siehe [`LICENSE`](LICENSE) und [`AUTHORS.md`](AUTHORS.md).

Copyright (c) 2025 Harm  , DK4HAA – [draussenfuchs.de](https://draussenfuchs.de/)
Copyright (c) 2026 Sebastian , DM8SB
