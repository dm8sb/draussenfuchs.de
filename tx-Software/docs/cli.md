# Serielle Kommandozeile (CLI)

## Übersicht

Die CLI ermöglicht die Konfiguration des ARDF-Fuchsjagd-Senders über die serielle Schnittstelle
(115200 Baud, 8N1, USB-CDC). Sie ist ausschließlich während der Startphase aktiv.

## Verbindung

- Baudrate: 115200
- Datenformat: 8N1
- Zeilenende: CR, LF oder CRLF (alle akzeptiert)
- USB-CDC (ESP32-C3 Super Mini) oder CH340/CH9102 (Lolin C3 Mini)

## Bedienung

### Startphase

1. Nach dem Einschalten/Reset zeigt der Sender eine Statusmeldung an
2. Enter drücken, um das Konfigurationsmenü zu öffnen
3. Im Hintergrund läuft das ESP-NOW-Sync-Fenster (120 s bei Profilen A–C, 30 s bei Profil D)
4. Jede CLI-Eingabe setzt den Inaktivitäts-Timer zurück
5. Nach Ablauf des Timeouts (120 s bzw. 30 s) startet automatisch der Normalbetrieb

### Wichtig

- Während die CLI aktiv ist, wird **nicht** gesendet
- Die Sync-Messung läuft im Hintergrund non-blocking weiter
- Der Normalbetrieb beginnt über Menüpunkt 10 oder durch Auto-Timeout

## Menüstruktur

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

## Menüpunkt-Details

### 1) Aktuelle Konfiguration anzeigen

Zeigt alle gespeicherten und abgeleiteten Parameter:
- Fox-ID und zugehörige MO-Kennung
- Aktives Profil mit Beschreibung
- Rufzeichen (leer = ISM-Modus)
- Tonfrequenz
- Zykluszeit, Slot-Dauer, WPM, Wiederholungen
- ESP-NOW-Status
- Firmware-Version

### 2) Fox-Nummer ändern (1..5)

- Eingabe: Ganzzahl 1 bis 5
- Jede Fox-Nummer hat eine eindeutige MO-Kennung und Tonfrequenz:
  - 1 = MOE (700 Hz)
  - 2 = MOI (800 Hz)
  - 3 = MOS (900 Hz)
  - 4 = MOH (1000 Hz)
  - 5 = MO5 (1200 Hz)
- Änderung wird sofort im NVS gespeichert

### 3) Profil ändern (A/B/C/D)

- Eingabe: A, B, C oder D (Groß- oder Kleinbuchstabe)
- Profile:
  - A – ISM-Standard: 60 s Zyklus, 18 WPM, MO-Kennung 2×
  - B – ISM-Sprint: 30 s Zyklus, 20 WPM, MO-Kennung 1×
  - C – ISM-Ultra: 20 s Zyklus, 24 WPM, MO-Kennung 1× (**Default**)
  - D – draussenfuchs Solo: Dauerton, kein Sync, kein ARDF
- Bei Auswahl von Profil D erscheint ein Hinweis auf Solo-Modus
- Änderung wird sofort im NVS gespeichert

### 4) Rufzeichen ändern/löschen

- Zeigt das aktuelle Rufzeichen an
- Eingabe des neuen Rufzeichens (max. 12 Zeichen)
- Leere Eingabe = Rufzeichen löschen
- Erlaubte Zeichen: A-Z, 0-9, /, -, .
- Kleinbuchstaben werden automatisch in Großbuchstaben konvertiert
- **Warnung:** „Rufzeichen nur für AFU-Betrieb verwenden; im ISM-Betrieb leer lassen."
- Änderung wird sofort im NVS gespeichert

### 5) Duty-Cycle berechnen/anzeigen

- Zeigt die exakte Duty-Cycle-Berechnung für den aktuellen Fuchs und das aktive Profil
- Tabelle mit Duty-Werten für 1 bis 5 aktive Füchse
- Berücksichtigt den 5% Sicherheitsaufschlag des Duty-Guards

### 6) Sync-Status anzeigen

- Eigene MAC-Adresse
- Master-Status (Ja/Nein)
- Anzahl aktiver Füchse
- Eigener Slot-Index
- Effektiver Zyklus und Slot
- Config-Hash
- Erkannte Nachbarn mit MAC, Fox-ID, Kompatibilität
- Fehlermeldungen (Kollision, Mismatch)

### 7) Zeitkorrekturwerte anzeigen

- Drift in ppm
- Offset in µs
- Qualität in %
- Anzahl Messungen
- Gültigkeitsstatus
- Zeit seit letzter Aktualisierung

### 8) Zeitkorrekturwerte löschen

- Löscht die gespeicherten Zeitkorrekturwerte aus dem NVS
- Setzt die interne Zeitkorrektur zurück
- Warnung wird angezeigt

### 9) Werkseinstellungen laden

- Bestätigung erforderlich (J/N)
- Setzt alle NVS-Daten zurück:
  - Fox-ID → 1
  - Profil → C (ISM-Ultra)
  - Rufzeichen → leer
  - Zeitkorrektur → gelöscht
- Neue Defaults werden sofort im NVS gespeichert

### 10) Normalbetrieb starten und CLI verlassen

- Gibt den aktuellen Slotplan aus
- Zeigt die Anzahl erkannter Füchse
- Zeigt den effektiven Zyklus und Slot
- Beendet die CLI und startet den regulären Sendebetrieb

### 11) Neustart

- Führt einen sofortigen Neustart des ESP32-C3 durch
- Alle Fehlerzustände werden zurückgesetzt
- Erneute Discovery-Phase nach dem Neustart

### h) Hilfe

- Übersicht aller Profile und Kennungen
- Kurzanleitung zur Bedienung

## NVS-Persistenz

### Konfiguration

- Namespace: `ardf_fox`
- Key: `cfg_v2`
- Struktur: `ConfigV2` (versioniert, CRC32-gesichert)
- Defaults bei ungültigen Daten: Fox 1, Profil C, kein Rufzeichen

### Zeitkorrektur

- Namespace: `timecorr`
- Key: `tc_v1`
- Struktur: `TimeCorrectionData` (CRC32-gesichert)
- Wird nur geladen wenn |drift_ppm| < 500 und Qualität ≥ 60

## Kein Testmodus

Aussendungen erfolgen ausschließlich im regulären Normalbetrieb nach Verlassen der CLI.
Es gibt keinen Morse- oder TX-Testmodus in der CLI.
