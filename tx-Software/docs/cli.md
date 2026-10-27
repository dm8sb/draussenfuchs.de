# Serielle Kommandozeile (CLI)

## Übersicht

Die CLI ermöglicht die Konfiguration des ARDF-Fuchsjagd-Senders über die serielle Schnittstelle
(115200 Baud, 8N1, USB-CDC). Sie ist während der Startphase und im Fehlerzustand aktiv.
Die CLI blockiert nie: Sync, LED und Zeitmessung laufen während jeder Eingabe weiter.

## Verbindung

- Baudrate: 115200
- Datenformat: 8N1
- Zeilenende: CR, LF oder CRLF (alle akzeptiert)
- Backspace/Entf löscht das letzte Zeichen
- USB-CDC (nativer USB-Serial/JTAG des ESP32-C3, Super Mini und Lolin C3 Mini)
- Terminal mit UTF-8 (Umlaute, Gedankenstriche, µ)

## Bedienung

### Startphase

1. Nach dem Einschalten/Reset zeigt der Sender eine Statusmeldung an
2. Enter drücken, um das Konfigurationsmenü zu öffnen (oder direkt eine Menünummer + Enter)
3. **Profile A–C:** Im Hintergrund läuft der ESP-NOW-Sync. Der Sendebeginn wird vom Master
   für alle Füchse festgelegt (120 s nach dem Einschalten des ersten Fuchses). Zum
   Sendebeginn wird die CLI automatisch beendet.
4. **Profil D:** Nach 30 s ohne Eingabe startet der Normalbetrieb automatisch. Im Betrieb
   ist die CLI nur nach einem Reset (RST-Taste) erreichbar.

### Rückfragen

- Menüpunkte 2, 3, 4 und 9 fragen nach einem Wert bzw. einer Bestätigung
- Ohne Antwort innerhalb von 30 s wird abgebrochen („Zeitüberschreitung – abgebrochen,
  Wert unverändert")
- Ungültige Eingaben werden mit einer Fehlermeldung abgelehnt, der Wert bleibt unverändert

### Neustart nach Änderungen

- Fox-Nummer, Profil, Rufzeichen und Werkseinstellungen werden sofort im NVS gespeichert,
  aber erst nach einem **Neustart** wirksam (Sync und Startphase laufen noch mit der alten
  Konfiguration)
- Nach jeder Änderung fragt die CLI „Jetzt neu starten? (J/N)"
- Ohne Neustart ist der Normalbetrieb gesperrt; zum Sendebeginn bzw. nach Ablauf des
  Timeouts startet der Sender dann neu, statt mit veraltetem Zustand zu senden

### Wichtig

- Während die CLI aktiv ist, wird **nicht** gesendet
- Die Sync-Messung läuft im Hintergrund weiter

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

Im Fehlerzustand fehlt Menüpunkt 10.

## Menüpunkt-Details

### 1) Aktuelle Konfiguration anzeigen

Zeigt alle gespeicherten und abgeleiteten Parameter:
- Fox-ID und zugehörige MO-Kennung
- Aktives Profil mit Beschreibung
- Rufzeichen (leer = ISM-Modus)
- Tonfrequenz
- Nominale Zykluszeit und Slot-Dauer des Profils, WPM, Wiederholungen
- ESP-NOW-Status
- Firmware-Version
- Hinweis, falls ein Neustart aussteht

### 2) Fox-Nummer ändern (1..5)

- Eingabe: genau eine Ziffer 1 bis 5
- Jede Fox-Nummer hat eine eindeutige MO-Kennung und Tonfrequenz:
  - 1 = MOE (700 Hz)
  - 2 = MOI (800 Hz)
  - 3 = MOS (900 Hz)
  - 4 = MOH (1000 Hz)
  - 5 = MO5 (1200 Hz)

### 3) Profil ändern (A/B/C/D)

- Eingabe: A, B, C oder D (Groß- oder Kleinbuchstabe)
- Profile:
  - A – ISM-Standard: 60 s Zyklus, 18 WPM, MO-Kennung 2×
  - B – ISM-Sprint: 30 s Zyklus, 20 WPM, MO-Kennung 1×
  - C – ISM-Ultra: 20 s Zyklus, 24 WPM, MO-Kennung 1× (**Default**)
  - D – draussenfuchs Solo: Dauerton, kein Sync, kein ARDF
- Bei Auswahl von Profil D erscheint ein Hinweis auf den Solo-Modus

### 4) Rufzeichen ändern/löschen

- Zeigt das aktuelle Rufzeichen an
- Eingabe des neuen Rufzeichens (max. 12 Zeichen)
- Leere Eingabe = Rufzeichen löschen
- Erlaubte Zeichen: A-Z, 0-9, /, -, .
- Kleinbuchstaben werden automatisch in Großbuchstaben konvertiert
- **Warnung:** „Rufzeichen nur für AFU-Betrieb verwenden; im ISM-Betrieb leer lassen."

### 5) Duty-Cycle berechnen/anzeigen

- Ton-AN-Zeit und Sendezeit-Budget (Grundlage des Duty-Guards) des eigenen Fuchses,
  im AFU-Modus zusätzlich das Budget im Rufzeichen-Slot
- Tabelle des Slotplans für 1 bis 5 aktive Füchse im ungünstigsten Fall (eigener Fuchs
  plus die längsten Kennungen) mit Zyklus, Slot, Duty und Entscheidung (OK/VERLÄNGERT)
- Profil D: Ton-, Pausen- und Zykluszeit
- Alle Duty-Werte inkl. 5 % Sicherheitsaufschlag

### 6) Sync-Status anzeigen

- Eigene MAC-Adresse und Config-Hash
- Master (Zuhörphase / dieser Fuchs / Fox-Nummer und MAC des Masters)
- Slotplan: Anzahl Füchse, Zyklus, Slot, Reihenfolge (eigener Slot mit `<<<` markiert)
- Verbleibende Zeit bis zum Sendebeginn, ob der Plan schon fest ist
- Fehler: Kollision, Konfigurations-Abweichung, „nicht im Slotplan"
- Erkannte Nachbarn mit Fox-Nummer, Profil, MAC, Kompatibilität und Aktivität

### 7) Zeitkorrekturwerte anzeigen

- Aktuelle Messung gegen den Master: Offset, Taktabweichung (ppm), Anzahl Messwerte
  (davon verwendet), Qualität. Am Master: Hinweis, dass er die Referenzuhr ist.
- Letzte gespeicherte Messung aus dem NVS (nur zur Anzeige)

### 8) Zeitkorrekturwerte löschen

- Löscht die gespeicherte Messung aus dem NVS
- Verwirft die laufende Messung; sie beginnt mit dem nächsten Beacon neu

### 9) Werkseinstellungen laden

- Bestätigung erforderlich (J/N)
- Setzt alle NVS-Daten zurück:
  - Fox-ID → 1
  - Profil → C (ISM-Ultra)
  - Rufzeichen → leer
  - Zeitkorrektur → gelöscht
- Neue Defaults werden sofort im NVS gespeichert, wirksam nach Neustart

### 10) Normalbetrieb starten und CLI verlassen

- **Profil D:** startet den Sendebetrieb sofort
- **Profile A–C:** Der Sendebeginn gilt für den ganzen Verbund
  - am **Master**: zieht den Sendebeginn für alle Füchse auf „in 5 s" vor (sofern der
    Slotplan noch nicht fest ist)
  - an einem **Slave**: beendet nur die CLI; der Sendebeginn bleibt beim Master
  - zeigt den Slotplan und die Zeit bis zum Sendebeginn
- Gesperrt, solange nach einer Änderung kein Neustart erfolgt ist, und im Fehlerzustand

### 11) Neustart

- Führt einen sofortigen Neustart des ESP32-C3 durch
- Erneute Sync-Phase nach dem Neustart

### h) Hilfe

- Übersicht aller Profile und Kennungen
- Kurzanleitung zur Bedienung

## Fehlerzustand

Bei FOX_ID-Kollision, Konfigurations-Abweichung, „nicht im Slotplan" oder ESP-NOW-Fehler
sendet der Fuchs nicht. Die CLI bleibt erreichbar (Enter):

- Konfiguration prüfen und ändern, dann Neustart (11)
- Nach 60 s ohne Eingabe: Neuversuch per Neustart (nicht bei „nicht im Slotplan"),
  höchstens 5-mal; danach Tiefschlaf bis zum Reset

## NVS-Persistenz

### Konfiguration

- Namespace: `ardf_fox`
- Key: `cfg_v3`
- Struktur: `FoxConfig` (Version 3, CRC-32 nach IEEE 802.3)
- Eine Konfiguration aus Firmware 2.0.x (Key `cfg_v2`) wird beim ersten Start übernommen
- Defaults bei ungültigen Daten: Fox 1, Profil C, kein Rufzeichen

### Zeitkorrektur

- Namespace: `timecorr`
- Key: `tc_v2`
- Struktur: `TimeCorrectionData` (CRC-gesichert)
- Nur zur Anzeige: letzte Messung gegen den Master; beim nächsten Einsatz wird neu gemessen

## Kein Testmodus

Aussendungen erfolgen ausschließlich im regulären Normalbetrieb nach Verlassen der CLI.
Es gibt keinen Morse- oder TX-Testmodus in der CLI.
