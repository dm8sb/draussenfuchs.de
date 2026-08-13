# Duty-Cycle-Berechnung und Duty-Guard

## Rechtlicher Rahmen

### ISM/SRD-Betrieb (433,920 MHz)

- Frequenzband: 433,050 – 434,790 MHz
- Frequenz fix: 433,920 MHz (hardwareseitig nicht änderbar)
- Sendeleistung: ca. 10 mW ERP
- **Duty Cycle: < 10 %** gemäß Allgemeinzuteilung SRD/ISM
- **Duty-Guard Sicherheitsmarge: strikt < 9,5 %**

### AFU-/Sonderbetrieb

- Rufzeichen (`CALLSIGN`) ausschließlich für den Amateurfunkbetrieb
- Im AFU-Modus gilt die 10%-ISM-Einschränkung nicht in derselben Form
- Im 10. Slot (Callsign-Aussendung) wird die Duty-Guard-Grenze auf **max. 50 %** angehoben
- In den Slots 1–9 bleibt die 9,5%-Grenze aktiv

## Duty-Guard-Mechanismus

### Sicherheitsaufschlag

Der Duty-Guard rechnet mit einem **5 % Sicherheitsaufschlag** auf die ermittelte Sendezeit:

```
effective_tx_ms = tx_on_ms × 1,05
```

Dieser Aufschlag kompensiert Ungenauigkeiten in der Zeitberechnung (Delays, Interrupt-Latenz etc.).

### Duty-Berechnung

```
duty_percent = (tx_on_ms × 1,05) / cycle_ms × 100
```

### Entscheidungslogik

| Bedingung                    | Ergebnis    |
|------------------------------|-------------|
| Duty ≤ 9,5 %, Zyklus normal | OK          |
| Duty ≤ 9,5 %, Zyklus verlängert | VERLÄNGERT |
| Duty > 9,5 % (ISM)          | BLOCKIERT   |
| Duty ≤ 50 % (AFU, 10. Slot) | OK          |

### Log-Format

```
[DUTY] Profil X | aktiv:N | Slot:Y ms | Zyklus:Z ms | TX:T ms | Duty:D % | Entscheidung:OK/VERLÄNGERT/BLOCKIERT
```

## Dynamische Zyklusverkürzung

### Ziel

Bei weniger als 5 aktiven Füchsen soll die Wiederholrate maximiert werden,
ohne die ISM-Duty-Grenze zu überschreiten.

### Algorithmus

1. `base_slot_ms` und `tx_on_ms` aus dem aktiven Profil ermitteln
2. `active_count` via ESP-NOW Discovery ermitteln
3. `desired_cycle_ms = base_slot_ms × active_count`
4. `min_cycle_ms = ⌈tx_on_ms × 1,05 / 0,095⌉`
5. `effective_cycle_ms = max(desired_cycle_ms, min_cycle_ms)`
6. `effective_slot_ms = effective_cycle_ms / active_count`

### Warum der Faktor 1,05 in der Formel?

Der Faktor 1,05 bildet den 5%-Sicherheitsaufschlag des Duty-Guards ab. Ohne diesen Faktor
würde der berechnete minimale Zyklus knapp unter der Grenze liegen, und der Duty-Guard
würde die Sendung fälschlicherweise blockieren.

### Beispielwerte

#### Profil C (ISM-Ultra, 24 WPM, 1× MO)

| Füchse | Gewünschter Zyklus | Min. Zyklus | Eff. Zyklus | Duty   |
|-------:|-------------------:|------------:|------------:|-------:|
|      1 |           4.000 ms |    ~14.200  |   14.200 ms | ~9,5 % |
|      2 |           8.000 ms |    ~18.700  |   18.700 ms | ~9,5 % |
|      3 |          12.000 ms |    ~18.700  |   18.700 ms | ~9,5 % |
|      4 |          16.000 ms |    ~18.700  |   18.700 ms | ~9,5 % |
|      5 |          20.000 ms |    ~18.700  |   20.000 ms | ~8,9 % |

*Hinweis: Die tatsächlichen Werte hängen von der Fox-ID (Länge der MO-Kennung) ab.*

#### Profil D (draussenfuchs Solo)

| Parameter    | Wert     |
|:-------------|:---------|
| TX-AN        | 200 ms   |
| Pause        | 2.200 ms |
| Zyklus       | 2.400 ms |
| Duty         | 8,33 %   |

## AFU-Ausnahme (10. Slot)

Im Amateurfunkbetrieb (Rufzeichen gesetzt) wird jeden 10. Slot das Rufzeichen gesendet:

- Sendeablauf: MO-Kennung(en) → 7 dits Pause → Rufzeichen 1×
- Die Duty-Guard-Grenze wird für diesen Slot auf **50 %** angehoben
- In den Slots 1–9 bleibt die 9,5%-Grenze aktiv
- Der `tx_counter` wird nur im AFU-Modus inkrementiert (ISM: dauerhaft 0)

## Profil-Übersicht

### Profil A – ISM-Standard

| Fox | Kennung | Geschätzte TX-Zeit | Duty bei 5 Füchsen |
|----:|---------|-------------------:|-------------------:|
|   1 | MOE     |         ~3.800 ms  |            ~6,3 %  |
|   2 | MOI     |         ~3.800 ms  |            ~6,3 %  |
|   3 | MOS     |         ~4.100 ms  |            ~6,8 %  |
|   4 | MOH     |         ~4.300 ms  |            ~7,2 %  |
|   5 | MO5     |         ~4.880 ms  |            ~8,1 %  |

### Profil B – ISM-Sprint

| Fox | Kennung | Geschätzte TX-Zeit | Duty bei 5 Füchsen |
|----:|---------|-------------------:|-------------------:|
|   1 | MOE     |         ~1.520 ms  |            ~5,3 %  |
|   2 | MOI     |         ~1.520 ms  |            ~5,3 %  |
|   3 | MOS     |         ~1.640 ms  |            ~5,7 %  |
|   4 | MOH     |         ~1.760 ms  |            ~6,2 %  |
|   5 | MO5     |         ~2.000 ms  |            ~7,0 %  |

### Profil C – ISM-Ultra

| Fox | Kennung | Geschätzte TX-Zeit | Duty bei 5 Füchsen |
|----:|---------|-------------------:|-------------------:|
|   1 | MOE     |         ~1.270 ms  |            ~6,7 %  |
|   2 | MOI     |         ~1.270 ms  |            ~6,7 %  |
|   3 | MOS     |         ~1.370 ms  |            ~7,2 %  |
|   4 | MOH     |         ~1.470 ms  |            ~7,7 %  |
|   5 | MO5     |         ~1.670 ms  |            ~8,8 %  |
