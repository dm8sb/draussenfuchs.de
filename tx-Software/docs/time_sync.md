# Zeitsynchronisation

## Übersicht

Alle Füchse eines Verbunds senden in einem gemeinsamen Slotraster. Dafür muss jeder Slave
wissen, wie seine Uhr zur Uhr des Masters steht. Das Zeitmodell (`TimeModel` in
`src/time_sync.cpp`) schätzt daraus Offset und Taktabweichung. Danach rechnet es jeden
Sendezeitpunkt aus der Master-Uhr in die lokale Uhr um.

## Warum die Füchse im Betrieb wach bleiben

Der ESP32-C3 hat zwei Zeitbasen:

| Zeitbasis                  | Takt                        | Genauigkeit              |
|----------------------------|-----------------------------|--------------------------|
| `esp_timer` (SYSTIMER)     | 40-MHz-Quarz                | typ. ±10…40 ppm          |
| Deep-Sleep-/RTC-Timer      | interner RC-Oszillator      | deutlich schlechter, temperaturabhängig |

Im Deep-Sleep läuft nur der RC-Oszillator. Seine Abweichung kann in einem 20-s-Zyklus
mehrere zehn Millisekunden ausmachen und ändert sich mit der Temperatur; ein
gemeinsames Raster wäre nach kurzer Zeit verloren.

Deshalb bleiben die Füchse in den Profilen A–C nach der Sync-Phase **wach** (WiFi aus,
CPU 80 MHz). Alle Zeiten laufen über den Quarz, und die gemessene Taktabweichung wirkt
auf dieselbe Uhr, die auch das Timing bestimmt. Profil D braucht kein gemeinsames Raster
und schläft weiterhin.

Kosten: ein Ruhestrom von einigen mA statt µA (auf der eigenen Hardware messen).

## Messverfahren

1. Der Master sendet Beacons mit seinem Sendezeitpunkt `uptime_us` (Master-Uhr)
2. Der Slave nimmt im Empfangs-Callback den lokalen Zeitstempel `rx_local_us`
3. Ein Messpunkt ist das Paar (Master-Zeit, lokale Zeit); gemessen wird **nur gegen den
   aktuellen Master**
4. Ringpuffer mit 256 Messpunkten (bei 2 Beacons/s etwa das ganze Sync-Fenster)
5. Bei einem Masterwechsel oder Neustart des Masters (`boot_id`) beginnt die Messung neu

## Modell

```
lokal = ref + a + b · (ref − x0)
```

- `ref` = Zeitpunkt in der Master-Uhr
- `a` = Offset in µs (enthält auch die mittlere Funklatenz)
- `b` = Taktabweichung (dimensionslos; `b × 10⁶` = ppm)
- `x0` = Bezugspunkt (ältester Messpunkt)

### Ausreißer-Filterung (Median / MAD)

1. Offset je Messpunkt: `lokal − ref`
2. Median aller Offsets
3. MAD = Median der Abweichungen vom Median
4. Verworfen werden Punkte mit Abweichung > max(3 × MAD, 500 µs)

### Lineare Regression

- Kleinste Quadrate über die verbleibenden Punkte
- Mindestens 30 Punkte, sonst nur Offset (Median), keine Taktabweichung
- Plausibilität: |b| < 100 ppm (zwei Quarze weichen zusammen deutlich weniger ab);
  sonst nur Offset

### Qualität

- 0–59: zu wenige Punkte für eine Drift-Schätzung
- 60–100: Anteil der verwendeten Punkte
- 10/20: Steigung unplausibel bzw. Regression numerisch nicht lösbar

## Genauigkeit

Die Verbund-Simulation (`test/test_sync`) mit 5 Füchsen, Quarzen zwischen −30 und
+35 ppm und Funklatenz-Jitter ergibt:

| Zeitpunkt        | Abweichung vom gemeinsamen Raster |
|------------------|-----------------------------------|
| Sendebeginn      | < 1 ms                            |
| nach 3 Stunden   | ≤ 2 ms (≤ 9 ms nach einem Masterwechsel mit nur 60 s Messdauer) |

Der Schutzabstand zwischen dem Ende der längsten Aussendung und dem nächsten Slot beträgt
mindestens 250 ms.

## Laufzeit

- Es gibt keinen Wakeup-Vorlauf mehr: Der Fuchs wartet mit `delay()` und in den letzten
  2–3 ms aktiv bis zum exakten Slotbeginn
- Ein verpasster Slot (sollte nicht vorkommen) wird übersprungen, nie verspätet gesendet
- Kein erneuter Abgleich während des Betriebs: Die Füchse brauchen nach dem Start keinen
  Funkkontakt

## NVS-Persistenz

Die letzte Messung (Offset, Drift, Qualität, Anzahl) wird nach der Sync-Phase im NVS
gespeichert (Namespace `timecorr`, Key `tc_v2`) und in Menüpunkt 7 angezeigt. Sie wird
**nicht** als Startwert verwendet: Die Taktabweichung gilt relativ zum jeweiligen Master
und wird bei jedem Einsatz neu gemessen. Löschen über Menüpunkt 8 oder 9.

## Zusammenspiel mit dem Sync-Protokoll

1. **Sync-Phase:** Beacons empfangen, gegen den Master messen (siehe
   [`sync_protocol.md`](sync_protocol.md))
2. **Modell:** sofort nach den ersten Messpunkten, danach höchstens 1× pro Sekunde neu
   geschätzt
3. **Ende:** 1 s vor Sendebeginn – letzte Messung im NVS ablegen, WiFi aus
4. **Normalbetrieb:** Sendezeitpunkt `k` = Planstart + k × Zyklus + Slot-Index × Slot,
   über das Modell in lokale Zeit umgerechnet
