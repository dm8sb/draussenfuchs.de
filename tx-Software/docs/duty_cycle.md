# Duty-Cycle-Berechnung und Duty-Guard

## Rechtlicher Rahmen

Die Firmware setzt die folgenden Grenzen technisch um. Ob sie für den eigenen Betrieb
genügen, muss der Betreiber prüfen (aktuelle Allgemeinzuteilung der BNetzA für SRD,
ERC/REC 70-03, EN 300 220); die Angaben sind keine Rechtsberatung.

### ISM/SRD-Betrieb (433,920 MHz)

- Frequenzband: 433,050 – 434,790 MHz
- Frequenz fix: 433,920 MHz (hardwareseitig nicht änderbar)
- Sendeleistung: ca. 10 mW ERP
- **Duty Cycle: < 10 %**
- **Duty-Guard Sicherheitsmarge: ≤ 9,5 %** (inkl. 5 % Aufschlag auf die Sendezeit)

### Bezugszeitraum

ERC/REC 70-03 bezieht den Duty Cycle auf einen Beobachtungszeitraum von **einer Stunde**.
Die Firmware prüft strenger: Der Abstand zwischen zwei Aussendungen muss bereits für sich
die Grenze einhalten. Damit ist die Grenze auch über jede Stunde eingehalten.

### AFU-Betrieb

- Rufzeichen (`CALLSIGN`) ausschließlich für den Amateurfunkbetrieb
- Im AFU-Modus gilt die 10-%-ISM-Einschränkung nicht in derselben Form
- Bei jeder 10. Aussendung (Rufzeichen-Slot) erlaubt die Firmware bis **50 %**
- Alle anderen Aussendungen bleiben bei 9,5 %

## Sendezeit-Budget

Grundlage aller Berechnungen ist das **Sendezeit-Budget** einer Aussendung, also die Zeit,
in der das TX-Modul versorgt ist (`txBudgetMs()` in `src/fox_profile.cpp`):

```
budget = 20 ms Vorlauf + Dauer der kompletten Morsesequenz (inkl. aller Pausen)
         [+ 7 dits + Rufzeichen im Rufzeichen-Slot]
```

Das ist konservativ: Bei ASK/OOK strahlt das Modul nur während der Töne ab, gezählt wird
aber die gesamte Versorgungszeit. Profil D: 20 ms Vorlauf + 200 ms Dauerton = 220 ms.

### Sicherheitsaufschlag

Der Duty-Guard rechnet mit **5 % Aufschlag** auf das Budget:

```
duty_percent = budget × 1,05 / zyklus × 100
min_zyklus   = ⌈budget × 1,05 / 0,095⌉            (Rufzeichen-Slot: / 0,50)
```

## Slotplan (verbundweit)

Der Master berechnet den Slotplan aus den Budgets **aller** aktiven Füchse (jeder Fuchs
meldet im Beacon sein normales und sein längstes Budget). Alle Füchse verwenden denselben
Plan (`computePlan()` in `src/schedule.cpp`):

1. `tx_norm` = längstes normales Budget im Verbund, `tx_max` = längste Aussendung
   (Rufzeichen-Slot, sonst = `tx_norm`)
2. `slot_need = max(profil.slot_ms, tx_max + 250 ms Schutzabstand)`
3. `zyklus = max(slot_need × n,  min_zyklus(tx_norm, 9,5 %),  min_zyklus(tx_max, 50 %))`
   – die Mindestzyklen jeweils mit 100 ppm Reserve für Taktabweichungen
4. `slot = zyklus / n`, Reihenfolge nach aufsteigender Fox-ID

| Ergebnis    | Bedeutung                                         |
|-------------|---------------------------------------------------|
| OK          | Profilzyklus reicht (`slot_ms × n`)               |
| VERLÄNGERT  | Zyklus über den Profilwert hinaus verlängert       |

### Laufzeit-Guard

Vor jeder Aussendung prüft `duty::Guard`, ob seit dem Beginn der vorherigen Aussendung
deren Mindestzyklus vergangen ist. Wenn nicht, wird die Aussendung übersprungen
(`[ERR] Duty-Guard: Sendung blockiert`, LED 2× kurz). Bei korrektem Slotplan greift er
nie; er ist die letzte Sicherung unabhängig von der Zeitplanung.

### Log-Format

```
[DUTY] Profil X | aktiv:N | Slot:Y ms | Zyklus:Z ms | TX:T ms | Duty:D % | Entscheidung:OK/VERLÄNGERT
```

Im AFU-Modus folgt eine zweite Zeile für den Rufzeichen-Slot.

## Beispielwerte

Alle Werte aus dem Code berechnet. Ungünstigster Fall: MO5 ist aktiv.

### Profil C (ISM-Ultra, 24 WPM, 1× MO)

| Füchse | Gewünschter Zyklus | Eff. Zyklus | Slot      | Duty   |
|-------:|-------------------:|------------:|----------:|-------:|
|      1 |           4.000 ms |   18.460 ms | 18.460 ms | 9,5 %  |
|      2 |           8.000 ms |   18.460 ms |  9.230 ms | 9,5 %  |
|      3 |          12.000 ms |   18.460 ms |  6.153 ms | 9,5 %  |
|      4 |          16.000 ms |   18.460 ms |  4.615 ms | 9,5 %  |
|      5 |          20.000 ms |   20.000 ms |  4.000 ms | 8,8 %  |

Profile A und B: siehe README, Abschnitt „Dynamische Zykluszeiten".

### Profil D (draussenfuchs Solo)

| Parameter        | Wert      |
|:-----------------|:----------|
| TX-Budget        | 220 ms (200 ms Ton + 20 ms Vorlauf) |
| Pause            | 2.212 ms  |
| Zyklus           | 2.432 ms  |
| Duty (Budget)    | 9,5 %     |
| Duty (nur Ton)   | 8,2 %     |

## AFU-Rufzeichen-Slot

Im Amateurfunkbetrieb (Rufzeichen gesetzt) wird bei jeder 10. Aussendung das Rufzeichen
gesendet:

- Sendeablauf: MO-Kennung(en) → 7 dits Pause → Rufzeichen 1×
- Die Duty-Grenze für diese Aussendung ist **50 %**
- Der Slot wird so verlängert, dass die Rufzeichen-Aussendung plus 250 ms
  Schutzabstand hineinpasst (z. B. Profil C, „DL1ABC/P": Slot 7.420 ms, Zyklus 37.100 ms
  bei 5 Füchsen)
- Der Zähler läuft nur im AFU-Modus

## Sendezeit-Budget je Fuchs

| Profil | MOE      | MOI      | MOS      | MOH      | MO5      | Duty bei 5 Füchsen |
|:-------|---------:|---------:|---------:|---------:|---------:|-------------------:|
| A      | 3.782 ms | 4.046 ms | 4.310 ms | 4.574 ms | 4.838 ms | 6,6–8,5 %          |
| B      | 1.520 ms | 1.640 ms | 1.760 ms | 1.880 ms | 2.000 ms | 5,3–7,0 %          |
| C      | 1.270 ms | 1.370 ms | 1.470 ms | 1.570 ms | 1.670 ms | 6,7–8,8 %          |
