# Zeitdrift-Kompensation und Zeitsynchronisation

## Übersicht

Die Zeitsynchronisation stellt sicher, dass alle ARDF-Sender im Verbund trotz unvermeidlicher
Taktdrift der internen Oszillatoren dauerhaft synchron bleiben. Die Kompensation basiert auf
einer linearen Regression der gemessenen Clock-Offsets.

## Messalgorithmus

### Datensammlung

1. Während des Sync-Fensters (120–180 s) werden ESP-NOW-Beacons empfangen
2. Jeder Beacon enthält die `uptime_us` des Senders (Mikrosekunden seit Boot)
3. Der Empfänger misst den lokalen Empfangszeitpunkt `rx_local_us`
4. Offset: `offset_us = rx_local_us - uptime_us`
5. Maximal 120 Messwerte werden in einem Ringpuffer gespeichert

### Ausreißer-Filterung (Median / MAD)

1. Median aller Offset-Werte berechnen
2. MAD (Median Absolute Deviation) berechnen:
   `MAD = median(|offset_i - median|)`
3. Ausreißer: Werte mit `|offset - median| > 3 × MAD` werden verworfen
4. Die MAD-basierte Filterung ist robuster als Standardabweichung gegen einzelne Ausreißer

### Lineare Regression

Mindestens 30 Messpunkte sind für eine valide Regression erforderlich.

Modell: `offset(t) = a + b × t`

- `t` = lokale Zeit relativ zum ersten Messwert
- `a` = initialer Offset (Achsenabschnitt)
- `b` = Drift-Rate (Steigung)
- `drift_ppm = b × 10⁶`

Berechnung über die Methode der kleinsten Quadrate (OLS):
```
b = (n·Σ(x·y) - Σx·Σy) / (n·Σ(x²) - (Σx)²)
a = (Σy - b·Σx) / n
```

### Qualitätsbewertung

- 0–59: Nicht genügend Daten für Regression
- 60–100: Gültige Schätzung, basierend auf dem Verhältnis gültiger zu gesamten Messwerten
- Werte ≥ 60 werden als vertrauenswürdig eingestuft

## NVS-Persistenz

### Gespeicherte Werte

```cpp
struct TimeCorrectionData {
    int16_t  drift_ppm;    // Geschätzte Drift in ppm
    int64_t  offset_us;    // Letzter Offset in µs
    uint8_t  quality;      // Qualitätsfaktor 0..100
    uint16_t meas_count;   // Anzahl Messungen
    uint32_t crc32;        // CRC32-Prüfsumme
};
```

### Ladekriterien

NVS-Werte werden als Initialwerte genutzt, wenn:
- |drift_ppm| < 500 ppm
- Qualität ≥ 60

### Speicherzeitpunkt

Nach erfolgreichem Abschluss der Sync-Phase (Regression berechnet, Qualität ausreichend).

### Löschen

Über CLI-Menüpunkt 8 oder als Teil der Werkseinstellungen (Menüpunkt 9).

## Vorlaufberechnung

Der Wakeup-Vorlauf stellt sicher, dass der Sender rechtzeitig vor seinem Slot aus dem
Deep-Sleep aufwacht. Er kompensiert die akkumulierte Drift seit dem letzten Sync.

### Formel

```
vorlauf_us = 80.000 + |drift_ppm| × elapsed_s + 20.000
```

Dabei ist:
- `80.000 µs` = Basis-Vorlauf (80 ms, für Bootzeit und Initialisierung)
- `|drift_ppm| × elapsed_s` = driftabhängiger Anteil (wächst mit der Zeit)
- `20.000 µs` = Sicherheitszuschlag (20 ms)
- `elapsed_s` = Sekunden seit dem letzten Sync-Offset-Update (oder seit Boot, falls kein Sync)

### Grenzen

- Minimum: 100.000 µs (100 ms)
- Maximum: 350.000 µs (350 ms)

### Drift-Kompensation im Deep-Sleep

Die Sleep-Zeit wird um den geschätzten Drift-Anteil korrigiert:

```
sleep_us_korrigiert = sleep_us × (1 - drift_ppm / 10⁶)
```

## Empfehlung für längere Einsätze

Bei längeren Einsätzrn kann die Zeit trotzdem auseinanderlaufen.

### Warum?

- Kristalloszillatoren driften temperaturabhängig
- Akkumulierte Fehler können bei langen Einsätzen die Slot-Grenzen überschreiten
- Die gespeicherten NVS-Driftdaten dienen beim Neustart als Initialwerte

## Zusammenspiel mit dem Sync-Protokoll

1. **Boot:** NVS-Driftdaten laden (falls vorhanden und plausibel)
2. **Sync-Phase:** ESP-NOW-Beacons empfangen, Offsets messen
3. **Regression:** Nach ≥ 30 Messpunkten lineare Drift berechnen
4. **Speichern:** Ergebnisse im NVS ablegen
5. **Normalbetrieb:** Deep-Sleep-Timer mit Drift korrigiert
6. **Kein Re-Sync:** Nach dem Initialfenster findet kein erneuter Sync statt
