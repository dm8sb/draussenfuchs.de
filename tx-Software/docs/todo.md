# Review & TODO – ARDF-Fuchsjagd-Sender ESP32-C3 (tx-Software v2.0.0)

Stand: 2026-10-27 · Umfang: kompletter Quellcode (`src/`, `test/`), `platformio.ini`, README, `docs/`

**Methode:** Ich habe den Code vollständig gelesen, die Firmware gebaut (Quellen kompilieren gegen
Arduino-Core 2.0.17 / `framework-arduinoespressif32 @ 3.20017`), die CRC-Tabelle gegen eine
Referenzimplementierung geprüft und die Morse-/Duty-Zeiten für alle Profile und Füchse nachgerechnet.
**Nicht** gemacht: Tests auf echter Hardware, HF-Messungen, Strommessungen. Befunde, die nur auf
Hardware sicher zu klären sind, sind als *„prüfen"* markiert.

**Prioritäten**

| Prio | Bedeutung |
|:-----|:----------|
| **P0** | Kernfunktion kaputt oder Regelverstoß (ISM-Duty) möglich – vor dem nächsten Einsatz beheben |
| **P1** | Fehler mit spürbarer Auswirkung im Betrieb |
| **P2** | Qualität, Wartbarkeit, Bedienkomfort |
| **P3** | Kosmetik, Doku, nice-to-have |

---

## 0. Gesamtbewertung (Kurzfassung)

**Stärken**
- Saubere Modulaufteilung (`morse`, `duty_guard`, `sync_espnow`, `time_sync`, `config_store`, `cli`, `tx_driver`), einheitliche Namespaces und Log-Präfixe.
- NVS-Persistenz mit Versionsfeld und CRC, Validierung der Eingaben in der CLI.
- Gute, ausführliche deutschsprachige Doku; die ISM-Duty-Grenzen sind bewusst im Code verankert.
- Profil D (Solo) ist einfach und funktioniert im Wesentlichen wie das Original.

**Kernproblem**
Der **Mehr-Fuchs-Betrieb (Profile A/B/C mit ≥ 2 Sendern) funktioniert in der aktuellen Form nicht**:
1. Jeder Fuchs hält jeden anderen Fuchs für „inkompatibel", weil die Fox-ID in den Config-Hash eingeht. Dadurch landet **jeder Verbund im Fehlerzustand „Konfigurations-Abweichung"**, und kein Fuchs sendet (→ 1.1).
2. Auch wenn man 1. behebt: Das Zeitraster wird nie angewendet. Slot-Index und Zyklusstart werden berechnet, aber nicht benutzt. Jeder Fuchs sendet sofort nach seiner eigenen Startphase und danach in einem Rhythmus, der pro Zyklus um die eigene Sendedauer wächst (→ 1.2–1.5).
3. Der Deep-Sleep-Timer läuft auf dem ungenauen internen RC-Oszillator. Die gemessene Quarz-Drift hilft dagegen nicht (→ 1.6).

Außerdem kann ein **Profilwechsel D → A/B/C in der CLI** zu einem Sendebetrieb mit deutlich über 10 % Duty führen (→ 2.1).

Für einen echten ARDF-Verbund empfehle ich, das Timing neu zu bauen (Vorschlag in Abschnitt 8.1). Einzelne Stellen zu flicken reicht nicht.

---

## 1. Kritische Funktionsfehler (Sync / Timing)

- [x] **P0 – 1.1 Config-Hash enthält die Fox-ID → alle Füchse sind gegenseitig „inkompatibel"**
  [config_store.cpp:145-157](../src/config_store.cpp#L145-L157), [sync_espnow.cpp:83-87](../src/sync_espnow.cpp#L83-L87), [sync_espnow.cpp:200-202](../src/sync_espnow.cpp#L200-L202)
  `calculateConfigHash(profile, fox_id, …)` wird mit der *eigenen* Fox-ID berechnet und mit dem Hash des Peers (der *dessen* Fox-ID enthält) verglichen. Fox 1 und Fox 2 haben damit immer verschiedene Hashes → `config_mismatch = true` → `ERR_CONFIG_MISMATCH` → kein Fuchs sendet. Kompatibel sind nur Füchse mit gleicher ID, und die lösen dann eine Kollision aus.
  **Fix:** Die Fox-ID aus `ConfigHashInput` entfernen. Der Hash darf nur verbundweite Parameter enthalten (Profil, WPM, Repeats, Slot, AFU-Flag, Protokollversion). Danach `SYNC_PROTOCOL_VERSION` erhöhen und `docs/sync_protocol.md` anpassen.
  **Erledigt:** Fox-ID aus `ConfigHashInput` und `calculateConfigHash()` entfernt; der Hash umfasst nur noch Protokollversion, Profil, Slot, WPM, Wiederholungen und AFU-Flag. `SYNC_PROTOCOL_VERSION` = 3 (alte Firmware wird ignoriert, alle Füchse müssen aktualisiert werden). Abgedeckt durch [test_config.cpp](../test/test_config/test_config.cpp).

- [x] **P0 – 1.2 Eigener Slot-Index und Zyklusstart werden im Sendebetrieb nie benutzt**
  [main.cpp:173-201](../src/main.cpp#L173-L201), [main.cpp:315-353](../src/main.cpp#L315-L353)
  `rtc_own_slot_idx` und `rtc_cycle_start_us` werden gesetzt, aber nirgends gelesen. Der erste TX erfolgt direkt am Ende der Startphase. Startet ein Fuchs ein paar Sekunden später, sendet er auch ein paar Sekunden später, unabhängig vom Slotplan.
  **Fix:** Die nächste Sendezeit absolut berechnen: `t_tx = cycle_start + k·cycle + slot_idx·slot` (siehe 8.1).
  **Erledigt:** Der Sendebetrieb rechnet jeden Sendezeitpunkt absolut aus dem Slotplan: `slotStartUs(plan_start, plan, slot_idx, k)` in [schedule.cpp](../src/schedule.cpp), umgerechnet über das Zeitmodell in die lokale Uhr ([main.cpp](../src/main.cpp) `runArdfMode()`). Slot-Index und Planstart werden also genutzt; der erste TX liegt im eigenen Slot des ersten Zyklus.

- [x] **P0 – 1.3 Schlafzeit wird relativ zum TX-*Ende* gerechnet → Zyklus wächst um TX-Dauer + Bootzeit**
  [main.cpp:338-343](../src/main.cpp#L338-L343)
  `sleep = cycle − vorlauf` beginnt erst nach der Morsesequenz. Die tatsächliche Periode ist damit `cycle − vorlauf + Bootzeit + NVS-Laden + Sequenzdauer`. Der Fehler pro Zyklus liegt zwischen ca. +1,1 s (Profil C, MOE) und +4,7 s (Profil A, MO5). Er ist **je Fox-ID verschieden**, deshalb laufen die Füchse schon nach wenigen Zyklen in fremde Slots.
  Dazu kommt: Der Vorlauf soll laut Doku „früh aufwachen, dann auf den Slot warten" bedeuten. Das Warten bis zum Slotbeginn fehlt aber, also wird einfach um den Vorlauf zu früh gesendet.
  **Erledigt:** Keine relative Schlafzeit mehr: Die Sendezeitpunkte sind absolut (Zyklus k), deshalb summieren sich TX-Dauer und Bootzeit nicht auf. Gewartet wird mit `delay()` und in den letzten 2–3 ms aktiv bis auf den Slotbeginn (`waitUntil()`). Die Vorlaufkonstanten sind entfernt.

- [x] **P0 – 1.4 `esp_timer_get_time()` beginnt nach jedem Deep-Sleep wieder bei 0**
  [main.cpp:316](../src/main.cpp#L316), [main.cpp:331-333](../src/main.cpp#L331-L333), [main.cpp:269](../src/main.cpp#L269)
  `rtc_last_sync_us`, `rtc_cycle_start_us` und `rtc_error_start_us` stammen aus der Zeitbasis des Kaltstarts. Nach dem Aufwachen ist `now_us` klein, deshalb ist `elapsed_s` praktisch immer 0 und die driftabhängige Vorlaufkomponente wirkt nie.
  **Fix:** Eine Zeitbasis nutzen, die den Deep-Sleep überdauert: `esp_rtc_get_time_us()` bzw. `gettimeofday()`. Alternativ die Zykluszahl im RTC-RAM mitzählen.
  **Erledigt:** Im ARDF-Betrieb gibt es keinen Deep-Sleep mehr (siehe 1.6), `esp_timer` läuft durchgehend. Die RTC-Variablen mit Zeitstempeln aus dem Kaltstart sind entfernt; Profil D braucht keine Zeitbezüge über den Schlaf hinweg.

- [x] **P0 – 1.5 Jeder Fuchs rechnet mit einem anderen effektiven Zyklus**
  [sync_espnow.cpp:194-197](../src/sync_espnow.cpp#L194-L197), [duty_guard.cpp:25-44](../src/duty_guard.cpp#L25-L44)
  `effective_cycle_ms` wird aus der TX-Zeit der *eigenen* Fox-ID berechnet. Bei < 5 aktiven Füchsen hat z. B. MOE in Profil C 9.064 ms und MO5 11.274 ms Zyklus (nach Ton-AN-Zeit). Ein gemeinsames Raster kann so nicht entstehen.
  **Fix:** Den Zyklus verbundweit aus der *längsten* Sequenz aller aktiven Füchse berechnen (oder fest aus MO5). Der Master gibt ihn im Beacon vor, alle übernehmen ihn.
  **Erledigt:** Der Master berechnet den Slotplan aus der längsten Aussendung *aller* aktiven Füchse (`tx_norm_ms`/`tx_max_ms` aus den Beacons) und verteilt ihn; alle Füchse nutzen denselben Zyklus. Test: `test_plan_uses_longest_fox`, `test_all_subsets_ism`.

- [x] **P0 – 1.6 Deep-Sleep-Timer läuft auf dem RC-Oszillator, die Driftmessung misst aber den Quarz** *(Architektur)*
  [time_sync.cpp](../src/time_sync.cpp), [main.cpp:345-350](../src/main.cpp#L345-L350)
  Die Regression misst die Drift des 40-MHz-Quarzes (`esp_timer`). Der Deep-Sleep-Wakeup läuft beim ESP32-C3 standardmäßig auf `RTC_SLOW_CLK` (internes RC, ~136 kHz). Dessen Fehler ist um Größenordnungen größer und temperaturabhängig, typischerweise 0,1–1 %, also 20–200 ms pro 20-s-Zyklus. Die Driftkorrektur greift also an der falschen Uhr an. Ohne Re-Sync laufen die Füchse innerhalb von Minuten auseinander. Die README-Empfehlung „Neustart nach 60 min" reicht dafür bei Weitem nicht.
  **Optionen:** (a) externer 32,768-kHz-Quarz an GPIO0/1 (`CONFIG_RTC_CLK_SRC_EXT_CRYS`, braucht ESP-IDF-Menuconfig bzw. ein eigenes sdkconfig); (b) Light-Sleep bzw. `esp_timer` mit Quarz statt Deep-Sleep (höherer Ruhestrom); (c) periodischer Re-Sync per ESP-NOW (siehe 8.1). Empfehlung: (c), optional zusätzlich (a).
  **Erledigt:** Entscheidung des Nutzers: Im ARDF-Betrieb bleibt der Fuchs nach der Sync-Phase wach (WiFi aus, CPU 80 MHz), alle Zeiten laufen über den Quarz (`esp_timer`/SYSTIMER). Die gemessene Drift wirkt damit auf die Uhr, die auch das Timing bestimmt. Simulation (`test_sync`): 5 Füchse mit ±35 ppm liegen nach 3 h innerhalb von ±2 ms im Raster (Schutzabstand 250 ms). **Kosten:** Ruhestrom ca. 10–20 mA statt µA (auf Hardware messen). Profil D bleibt im Deep-Sleep.

- [x] **P1 – 1.7 Slaves übernehmen den Zyklusstart des Masters nie**
  [sync_espnow.cpp:65-151](../src/sync_espnow.cpp#L65-L151), [sync_espnow.cpp:352-356](../src/sync_espnow.cpp#L352-L356)
  `beacon.cycle_start_us` und `beacon.cycle_ms/slot_ms` werden im Empfänger nicht ausgewertet. Der Master setzt `cycle_start_us` außerdem erst beim Sync-Ende, und dann werden keine Beacons mehr verschickt. Die Rollen Master/Slave haben damit keine Wirkung.
  **Erledigt:** Slaves übernehmen Zyklus, Slot, Reihenfolge und Sendebeginn aus dem Beacon des Masters (`copyPlanFromBeacon()` in [sync_logic.cpp](../src/sync_logic.cpp)). Der Sendebeginn wird zudem im Verbund vererbt (`FLAG_HAS_START`), sodass ein später eingeschalteter Fuchs mit höherer MAC den Start nicht verschiebt (`test_five_foxes_stay_aligned_3h`, `test_master_leaves`).

- [x] **P1 – 1.8 CLI-Timeout beendet die Startphase vor dem Sync-Ende**
  [main.cpp:124-130](../src/main.cpp#L124-L130), [config.h:42-44](../src/config.h#L42-L44), [config.h:54](../src/config.h#L54)
  CLI-Timeout und Sync-Fenster sind beide 120 s lang. Sobald Peers gesehen werden, wird das Sync-Fenster auf 180 s verlängert, die CLI bricht aber nach 120 s ab. Gerade im Mehr-Fuchs-Fall wird `processSync()` deshalb nie abgeschlossen: keine finale Regression, kein `saveToNvs()`, kein `cycle_start`. Dasselbe passiert bei Menüpunkt 10 vor Ende des Sync-Fensters.
  **Fix:** Normalbetrieb erst starten, wenn `isSyncComplete()` true ist, oder die Sync-Finalisierung beim Start erzwingen (`finalizeSync()`).
  **Erledigt:** Die Startphase der Profile A–C endet nicht mehr per CLI-Timeout, sondern genau dann, wenn die Sync-Phase abgeschlossen ist (`SYNC_FINISH_LEAD_MS` = 1 s vor dem vereinbarten Sendebeginn). Menüpunkt 10 zieht am Master den Start für alle vor; an einem Slave schließt er nur die CLI. Die Fensterverlängerung auf 180 s entfällt.

- [x] **P1 – 1.9 Driftmessung mischt Messwerte aller Peers**
  [sync_espnow.cpp:144-147](../src/sync_espnow.cpp#L144-L147), [time_sync.cpp:51-62](../src/time_sync.cpp#L51-L62)
  Alle Peers schreiben in denselben Ringpuffer. Ihre Offsets unterscheiden sich um die Boot-Zeitdifferenz (Sekunden). Median/MAD wirft dann alle Peers außer dem häufigsten weg, und die Regression ergibt keinen sinnvollen Wert.
  **Fix:** Nur gegen den Master messen (bzw. pro Peer getrennt). Der Master selbst korrigiert nicht.
  **Erledigt:** Neues Zeitmodell `TimeModel` ([time_sync.cpp](../src/time_sync.cpp)) misst nur gegen den aktuellen Master; bei Masterwechsel oder Neustart des Masters wird neu gemessen. Median/MAD-Filter und Regression wie zuvor, aber über statische Puffer (256 Werte ≈ ganzes Sync-Fenster).

- [x] **P1 – 1.10 Vorzeichen der Driftkorrektur prüfen**
  [main.cpp:345-350](../src/main.cpp#L345-L350)
  Mit `offset = local − remote` bedeutet eine positive Steigung, dass die lokale Uhr schneller läuft. Dann braucht man *mehr* lokale µs für dieselbe reale Zeit. Der Code rechnet aber `sleep·(1 − drift)`. Wahrscheinlich ist das Vorzeichen falsch, mit Test verifizieren. (Durch 1.6 ist das derzeit ohnehin wirkungslos.)
  **Erledigt:** Es wird nicht mehr die Schlafzeit skaliert, sondern jeder Zeitpunkt direkt umgerechnet: lokal = ref + a + b·(ref − x0). Das Vorzeichen ist durch `test_positive_drift_3h`/`test_negative_drift_3h` abgesichert.

- [x] **P1 – 1.11 Race Condition zwischen ESP-NOW-Callback und Hauptschleife**
  [sync_espnow.cpp:65-151](../src/sync_espnow.cpp#L65-L151), [time_sync.cpp:83-187](../src/time_sync.cpp#L83-L187)
  `onReceive` läuft im WiFi-Task. Er verändert `sync_state` und den Messpuffer, während die Hauptschleife `computeRegression()` ausführt (liest Puffer, `new/delete`) oder die CLI `sync_state` ausgibt. Dazu kommt `Serial.printf` im Callback.
  **Fix:** Im Callback nur Beacon und Zeitstempel in eine FreeRTOS-Queue legen und alles Weitere in `processSync()` erledigen.
  **Erledigt:** Der ESP-NOW-Callback legt nur noch Absender-MAC, Beacon und Empfangszeitstempel in eine FreeRTOS-Queue; Auswertung, Regression und alle Serial-Ausgaben laufen in `processSync()` im Hauptkontext.

- [x] **P2 – 1.12 Peers werden nie inaktiv, und die Peer-Tabelle kann „verstopfen"**
  [sync_espnow.cpp:98-119](../src/sync_espnow.cpp#L98-L119)
  `peer.active` wird nie zurückgesetzt (kein Timeout über `last_seen_us`). Ein ausgeschalteter Fuchs zählt weiter mit. Bei 4 belegten Plätzen (inkl. fremder/inkompatibler Geräte) wird ein legitimer Fuchs ignoriert. Ein Neustart eines Peers (`boot_id` wechselt) wird nicht erkannt.
  **Erledigt:** Peers gelten nach `PEER_TIMEOUT_MS` (3 s) ohne Beacon als inaktiv, abgelaufene Einträge werden wiederverwendet (8 Plätze, `MAX_PEERS`). Ein Wechsel der `boot_id` des Masters setzt die Zeitmessung zurück. Dabei gefundener Folgefehler: `now − last_seen` lief über, wenn ein Beacon während der Auswertung eintraf; jetzt robust per Addition.

- [x] **P2 – 1.13 Beacon-Jitter wird bei jedem Schleifendurchlauf neu ausgewürfelt**
  [sync_espnow.cpp:365-372](../src/sync_espnow.cpp#L365-L372)
  Weil der Zufallswert alle 10 ms neu gezogen wird, feuert der Beacon fast immer bei ~450 ms. Den nächsten Sendezeitpunkt einmal nach dem Senden festlegen.
  **Erledigt:** Der nächste Beacon-Zeitpunkt wird einmal nach dem Senden ausgewürfelt (500 ms ± 50 ms).

- [x] **P3 – 1.14 `cycle_ms`/`slot_ms` im Beacon sind `uint16_t`**
  [config.h:126-127](../src/config.h#L126-L127), [sync_espnow.cpp:411-412](../src/sync_espnow.cpp#L411-L412)
  Das reicht heute knapp (Profil A max. ~54 s). Ein AFU-Slot oder ein späteres Profil mit > 65,5 s wird aber stillschweigend abgeschnitten. Besser `uint32_t` oder Einheit 10 ms.
  **Erledigt:** Beacon-Protokoll v3 mit `uint32_t` für Zyklus, Slot und Sendezeiten.

---

## 2. Regulatorik / HF-Sicherheit

- [x] **P0 – 2.1 Profilwechsel D → A/B/C in der CLI führt zu > 10 % Duty**
  [main.cpp:101-108](../src/main.cpp#L101-L108), [sync_espnow.cpp:257-266](../src/sync_espnow.cpp#L257-L266), [main.cpp:326](../src/main.cpp#L326)
  Bootet der Fuchs mit Profil D, wird `effective_cycle_ms = 2400` gesetzt. Wechselt man dann in der CLI auf C und startet, sendet der Fuchs MO-Kennungen (~1,25–1,65 s Sequenz, 0,8–1,0 s Ton-AN) mit `sleep = 2400 − 100 ms`. Das ergibt deutlich über 10 % Duty. Der Duty-Guard in `runNormalMode` erkennt das als VERLÄNGERT, **seine Zykluszeit wird aber nicht für den Sleep verwendet** (→ 2.2).
  **Fix:** Nach jeder Konfigurationsänderung neu starten (oder Sync neu initialisieren), siehe 6.2.
  **Erledigt:** Doppelt abgesichert: (1) Nach jeder Konfigurationsänderung ist ein Neustart Pflicht (6.2), damit läuft der Sync immer mit dem aktiven Profil. (2) Die Pause im Sendebetrieb kommt aus dem Duty-Guard der jeweiligen Aussendung (2.2). Nachgerechnet: Szenario D → C (MOE) ergibt einen Zyklus von 14.037 ms und 9,50 % statt > 10 %.

- [x] **P0 – 2.2 Das Ergebnis des Duty-Guards steuert das Timing nicht**
  [main.cpp:255-275](../src/main.cpp#L255-L275) vs. [main.cpp:326-327](../src/main.cpp#L326-L327)
  `checkDuty()` rechnet einen eigenen `effective_cycle_ms` aus, geschlafen wird aber mit `rtc_effective_cycle_ms` aus der Sync-Phase. Der Guard ist so nur Anzeige. Außerdem ist `BLOCKIERT` praktisch unerreichbar, weil `checkDuty` den Zyklus immer so weit verlängert, dass die Grenze eingehalten wird.
  **Fix:** Der Guard muss die *tatsächlich* nächste Sendezeit freigeben oder verschieben: „letzter TX-Start + min_cycle ≤ geplanter TX-Start". Zusätzlich eine echte Sendezeitbilanz im RTC-RAM führen (gleitendes Fenster, z. B. 1 h nach ERC/REC 70-03).
  **Erledigt:** Schritt 2: Echter Laufzeit-Guard `duty::Guard`: Vor jeder Aussendung wird geprüft, ob seit dem Beginn der vorherigen deren Mindestabstand vergangen ist (9,5 % bzw. 50 % im Rufzeichen-Slot); sonst wird die Aussendung übersprungen. Der Slotplan enthält eine Reserve für 100 ppm Taktabweichung, damit der Guard im Normalfall nie greift. Tests: `test_guard_*`, `test_all_subsets_ism`.

- [x] **P1 – 2.3 Zwei verschiedene Definitionen der TX-Zeit**
  [fox_profile.cpp:109-135](../src/fox_profile.cpp#L109-L135) (Ton-AN), [main.cpp:240-253](../src/main.cpp#L240-L253) (Sequenzdauer inkl. Pausen), [cli.cpp:262-266](../src/cli.cpp#L262-L266) (Ton-AN)
  Sync und CLI rechnen mit Ton-AN-Zeit (z. B. Profil C/MOE: Mindestzyklus 9.064 ms), der Laufzeit-Guard und die README-Tabellen mit Sequenzdauer (14.037 ms). Die Werte in README, `docs/duty_cycle.md`, CLI-Menüpunkt 5 und im tatsächlichen Verhalten passen dadurch nicht zusammen.
  **Fix:** *Eine* zentrale Funktion `txBudgetMs(profile, fox, with_callsign)` mit dokumentierter, konservativer Definition. Empfehlung: Sendezeit = Zeit, in der das Modul versorgt ist (Warmup + Sequenz). Das ist konservativ und leicht nachweisbar.
  **Erledigt:** Zentrale Funktion `txBudgetMs()` in [fox_profile.cpp](../src/fox_profile.cpp) (Vorlauf + komplette Sequenz inkl. Pausen, optional + 7 dit + Rufzeichen). Sync, Duty-Guard, Startphase und CLI-Menüpunkt 5 verwenden sie einheitlich; `calculateTxOnTimeMs()` mit der geschätzten Rufzeichenlänge ist entfernt.

- [x] **P1 – 2.4 Rufzeichen-Slot passt in keinem Profil in den Slot**
  Nachgerechnet für MO5 + 7 dit + „DL1ABC/P": Profil A 12,1 s (Slot 12 s), **B 8,6 s (Slot 6 s), C 7,2 s (Slot 4 s)**. Der Fuchs sendet dann in den Slot des nächsten Fuchses.
  **Fix:** Länge vorab prüfen (CLI-Warnung, ggf. blockieren), im AFU-Slot das Rufzeichen statt der MO-Kennung senden, oder das Rufzeichen auf mehrere Slots verteilen.
  **Erledigt:** Mit dem Slotplan gelöst (vorgezogen aus Schritt 3): Jeder Fuchs meldet seine längste Aussendung (`tx_max_ms`), der Slot ist mindestens so lang wie die längste Aussendung im Verbund + 250 ms Schutzabstand. Im AFU-Modus wird der Zyklus dadurch länger (Profil C mit „DL1ABC/P": Slot ≈ 7,4 s). Test: `test_callsign_slot_fits`.

- [x] **P1 – 2.5 GPIO-Zustand im Deep-Sleep nicht definiert** *(prüfen)*
  [main.cpp:358-375](../src/main.cpp#L358-L375)
  Im Deep-Sleep verlieren GPIO3 (DATA) und GPIO4 (TX-VCC) ihren Ausgangszustand. Ohne Pull-down oder `gpio_hold_en()`/`gpio_deep_sleep_hold_en()` kann das Modul teilweise versorgt sein oder der Daten-Pin floaten, was ungewollte Abstrahlung und Stromverbrauch bedeuten kann.
  **Fix:** Vor dem Schlafen `gpio_hold_en(TX_VCC_EN_PIN)` setzen (LOW), DATA ebenso; nach dem Aufwachen `gpio_hold_dis()`. Alternativ Hardware-Pull-downs vorsehen. Am Board mit Oszilloskop und Strommessung verifizieren.
  **Erledigt:** Vor dem Deep-Sleep werden DATA (GPIO3) und TX-VCC (GPIO4) auf LOW gesetzt und per `gpio_hold_en()` + `gpio_deep_sleep_hold_en()` gehalten. `tx::init()` hebt das Halten nach der LOW-Konfiguration wieder auf (glitchfrei laut IDF-Doku). Auf Hardware noch per Strommessung/Oszilloskop zu bestätigen.

- [ ] **P2 – 2.6 Rechtliche Aussagen zum AFU-Modus fachlich prüfen lassen**
  README „Rechtliche Hinweise", [config.h:50](../src/config.h#L50)
  Dass im 10. Slot 50 % Duty zulässig sind, die Slots 1–9 aber unter SRD-Regeln laufen, ist eine Mischform. Entweder ist der Betrieb Amateurfunk, dann gelten AFuV-Regeln (u. a. Kennungsintervall, unbemannter Betrieb), oder SRD. Empfehlung: einen eigenen **AFU-Modus** mit IARU-ARDF-Standardtiming (z. B. 60 s pro Fuchs, 5-min-Zyklus) und Kennung nach AFuV, klar getrennt vom ISM-Modus. Das Kennungsintervall gegen die AFuV prüfen: Profil A mit 1 Fuchs × 10 Slots ≈ 10 min ist grenzwertig.

- [ ] **P2 – 2.7 Duty-Cycle-Bezugszeitraum dokumentieren**
  ERC/REC 70-03 definiert den Duty-Cycle über ein Beobachtungsintervall von 1 h. Der Guard prüft pro Zyklus, was strenger und damit sicher ist. Trotzdem in `docs/duty_cycle.md` erläutern und die Quellen (Vfg./Allgemeinzuteilung der BNetzA, EN 300 220) mit Nummer zitieren.

---

## 3. Weitere Fehler

- [ ] **P1 – 3.1 CRC32-Tabelle ist fehlerhaft (116 von 256 Einträgen)**
  [config_store.cpp:17-61](../src/config_store.cpp#L17-L61)
  Gegen die Standard-CRC-32 (Polynom 0xEDB88320) geprüft. Erste Abweichungen bei Index 10, 13, 14, 15, 42, … Teilweise sind Werte doppelt, z. B. `0x3B6E20C8`. Funktional ist die Prüfsumme deterministisch, aber keine CRC-32 mehr: schwächere Fehlererkennung, keine Interoperabilität mit Fremdwerkzeugen.
  **Fix:** ROM-Funktion `esp_rom_crc32_le()` (bzw. `crc32_le()` aus `esp_crc.h`) verwenden oder die Tabelle zur Compile-Zeit per `constexpr` generieren. Achtung: Das ändert die gespeicherte CRC. NVS-Key/Version hochzählen (`cfg_v3`) oder beim Laden beide CRCs akzeptieren.

- [ ] **P1 – 3.2 Fehlerbehandlung: Endlosschleife aus Neustarts, irreführende Meldung**
  [main.cpp:380-448](../src/main.cpp#L380-L448)
  - Bei Kollision oder Mismatch startet der Fuchs alle 60 s neu, synct erneut, scheitert erneut, und das endlos. Im Gelände bemerkt das niemand, und der Akku wird leergezogen (WiFi aktiv).
  - Ein Tastendruck löst den Neustart aus, die Ausgabe sagt aber „Automatische Wiederherstellung". Die Meldung „Bitte Fox-Nummer über CLI ändern" ist im Fehlerzustand nicht umsetzbar, weil es dort keine CLI gibt.
  - Ein Mismatch eines *einzelnen* Fremdgeräts legt den ganzen Verbund lahm.
  **Vorschlag:** Bei Mismatch den inkompatiblen Peer ignorieren und mit den kompatiblen weiterarbeiten. Bei Kollision deterministisch auflösen (z. B. höhere MAC gibt nach und meldet sich). Im Fehlerzustand direkt in die CLI springen. Nach N erfolglosen Versuchen in den Tiefschlaf gehen statt endlos neu zu starten.

- [x] **P1 – 3.3 Konfigurationsänderungen in der CLI wirken nicht auf den laufenden Sync**
  [cli.cpp:107-162](../src/cli.cpp#L107-L162), [main.cpp:101](../src/main.cpp#L101)
  `initSync()` läuft vor der CLI mit der alten Fox-ID und dem alten Profil. Wird beides in der CLI geändert, verschicken die Beacons weiter die alten Werte, und `prof` in `runStartupPhase` zeigt auf das alte Profil. Siehe auch 2.1. → 6.2.
  **Erledigt:** Durch 6.2 behoben: Geänderte Konfiguration wird erst nach einem Neustart aktiv, Sync und Startphase laufen also immer mit der aktuellen Konfiguration.

- [ ] **P2 – 3.4 Startphase blockiert den Sync durch `delay()`**
  [main.cpp:138-148](../src/main.cpp#L138-L148), [cli.cpp:111-117](../src/cli.cpp#L111-L117)
  Das LED-Blinken mit `delay(500)` und die blockierenden Eingabeschleifen der Untermenüs (bis 30 s) halten `processSync()` an: keine Beacons, keine Regression. Andere Füchse halten diesen dann ggf. für weg (sobald 1.12 umgesetzt ist).
  **Fix:** LED per nicht-blockierender Zustandsmaschine (`millis()`-basiert) steuern, Untermenüs als Zustände in `cli::process()` umsetzen.

- [ ] **P2 – 3.5 LED-Muster laut README sind nicht oder anders implementiert**
  [tx_driver.cpp:138-157](../src/tx_driver.cpp#L138-L157), [main.cpp:137-148](../src/main.cpp#L137-L148)
  `blinkHeartbeat()` (2 Hz Boot/Sync), `blinkIdle()` und `blinkDutyBlocked()` werden nie aufgerufen. Da `cli_active` ab Boot `true` ist, zeigt die LED in der Startphase immer „500 ms an / ~1,5 s aus". Sync läuft und CLI aktiv sind nicht unterscheidbar.

- [ ] **P2 – 3.6 Kleinere CLI-Fehler**
  - [cli.cpp:118](../src/cli.cpp#L118) `Serial.parseInt()` lässt CR/LF im Puffer, deshalb wird das Menü doppelt ausgegeben. `parseInt` akzeptiert außerdem „2abc".
  - [cli.cpp:179-204](../src/cli.cpp#L179-L204) Bei der Rufzeichen-Eingabe funktioniert Backspace nicht (das Zeichen landet im Puffer und wird als ungültig abgelehnt).
  - [cli.cpp:141-143](../src/cli.cpp#L141-L143) Bei der Profilwahl kommen die restlichen Zeichen eventuell erst nach dem Leeren an, der Effekt ist derselbe wie oben.
  - [cli.cpp:385-389](../src/cli.cpp#L385-L389) Die Werkseinstellungen setzen den RAM-Zustand von `timesync` nicht zurück (anders als Menüpunkt 8), und der laufende Sync behält die alten Werte.
  - [cli.cpp:355-358](../src/cli.cpp#L355-L358) / [time_sync.cpp:45](../src/time_sync.cpp#L45) „Letzte Aktualisierung vor x s" zählt nach dem Laden aus dem NVS ab Boot, obwohl die Werte von einem früheren Einsatz stammen.

- [ ] **P2 – 3.7 Konfiguration wird bei jedem Aufwachen aus dem NVS gelesen**
  [main.cpp:88](../src/main.cpp#L88)
  Das kostet Zeit und Strom bei jedem Zyklus. Ist der NVS beschädigt, lädt der Fuchs *mitten im Einsatz* still die Werkseinstellungen (Fox 1, Profil C) und ändert damit seine Identität.
  **Fix:** Die Konfiguration nach dem Kaltstart in den RTC-RAM kopieren (mit CRC) und im Sendebetrieb nur von dort lesen.

- [ ] **P3 – 3.8 `ERR_INVALID_CONFIG` ist praktisch unerreichbar**
  `loadConfig()` ersetzt eine ungültige Konfiguration bereits durch die Defaults, deshalb schlägt die Prüfung in [main.cpp:168](../src/main.cpp#L168) nie fehl. Entweder entfernen oder `loadConfig`-Fehler als Warnung (LED) anzeigen.

---

## 4. Code-Qualität & Architektur

- [ ] **P2 – 4.1 Toter bzw. unbenutzter Code entfernen oder anschließen**
  `rtc_initialized`, `rtc_cycle_start_us`, `rtc_own_slot_idx` (nur geschrieben), `sync::extendSyncWindow()`, `timesync::getCorrectedOffsetUs()`, `cli::resetIdleTimer()`, Rückgabewert von `cli::process()`, `isValidProfileId()`, `blinkHeartbeat/Idle/DutyBlocked`, `FoxProfile::fox_count`, die Parameter `with_callsign/callsign_len` in `calculateTxOnTimeMs` (inkl. unbenutztem `cs_ptr` in [fox_profile.cpp:124](../src/fox_profile.cpp#L124)), der Fall `ERR_DUTY_BLOCKED` in `handleError`.
- [x] **P2 – 4.2 Identische if/else-Zweige** in [main.cpp:104-109](../src/main.cpp#L104-L109): beide rufen `initSync(...)` auf.
  **Erledigt:** Mit der neuen Startphase entfallen.
- [x] **P2 – 4.3 Doppelte Duty-Logik**
  `calculateEffectiveCycleMs()` und `checkDuty()` rechnen dasselbe zweimal; `calculateMinCycleMs()` existiert parallel dazu. Zu einer Funktion zusammenführen.
  **Erledigt:** `checkDuty()`/`calculateEffectiveCycleMs()` entfernt; der Zyklus kommt ausschließlich aus `schedule::computePlan()`, der Duty-Guard enthält nur noch Grenzwert, Mindestzyklus, Duty-Berechnung und den Laufzeit-Guard.
- [ ] **P2 – 4.4 Globale `extern`-Kopplung zwischen `main.cpp` und `cli.cpp`**
  `g_config` und `g_start_normal_mode` sind globale Variablen. Besser: Die CLI bekommt eine Referenz auf die Konfiguration und meldet über einen Rückgabewert (`enum class CliResult { None, StartNormal, Reboot }`).
- [x] **P2 – 4.5 Hardware-Abstraktion für Tests**
  `morse.cpp` gibt im reinen Rechenpfad über `Serial` aus ([morse.cpp:105](../src/morse.cpp#L105)), und `duty_guard.cpp` bindet `Arduino.h` ein. Rechenlogik (Morse-Timing, Duty, Slotplan, Regression) in Arduino-freie Dateien trennen, damit sie nativ testbar wird (→ 5.1).
  **Erledigt:** Rechenlogik vollständig Arduino-frei: `morse.cpp`, `fox_profile.cpp`, `duty_guard.cpp`, `config_check.cpp`, `schedule.cpp`, `time_sync.cpp`, `sync_logic.cpp`. Hardware/Funk/NVS in `morse_tx.cpp`, `sync_espnow.cpp`, `config_store.cpp`, `time_correction_store.cpp`, `tx_driver.cpp`.
- [x] **P2 – 4.6 Heap-Allokation in `computeRegression()`** ([time_sync.cpp:92-119](../src/time_sync.cpp#L92-L119))
  `new[]`, alle 10 s im Sync. Durch statische Puffer ersetzen (`MAX_MEASUREMENTS` ist bekannt). Der Bubble-/Selection-Sort mit O(n²) ist bei n = 120 vertretbar, `std::nth_element` wäre aber eleganter.
  **Erledigt:** `TimeModel` arbeitet mit statischen Puffern, ohne `new[]`.
- [ ] **P3 – 4.7 Magische Zahlen**
  Timeouts der Untermenüs (10 s / 30 s), Blink-Zeiten, `delay(500)` USB-Stabilisierung, „7 dits" (mehrfach). Als benannte Konstanten in `config.h` ablegen.
- [ ] **P3 – 4.8 `ARDF_VERSION` doppelt definiert** ([platformio.ini:14](../platformio.ini#L14), [config.h:16-18](../src/config.h#L16-L18)), dazu README „Version 2.0.0 – Juli 2026". Version aus `git describe` per `extra_scripts` erzeugen.
- [x] **P3 – 4.9 Feste Array-Größe `[5]`** in `SyncBeacon::active_fox_ids` statt `MAX_FOXES`.
  **Erledigt:** Beacon und Slotplan verwenden `MAX_FOXES`.
- [ ] **P3 – 4.10 Kommentar und Realität beim Arduino-Core**
  [tx_driver.cpp:17-19](../src/tx_driver.cpp#L17-L19) ist korrekt (Core 2.x), die README behauptet „≥ 3.0" (→ 7.1). Die `#if`-Kompatibilitätsschicht ist gut, mit Core 3.x aber ungetestet. Entweder eine Plattformversion festpinnen oder beide Varianten im CI bauen.
- [ ] **P3 – 4.11 Statische Analyse und Formatierung**
  `build_flags = -Wall -Wextra` aktivieren, `pio check` (cppcheck/clang-tidy) einführen, eine `.clang-format` hinzufügen.
  **Teilweise erledigt:** `build_src_flags = -Wall -Wextra` für den eigenen Code (Firmware) und `-Wall -Wextra` in den nativen Tests; Stand: keine Warnungen. `pio check`/clang-format nicht eingeführt (Werkzeug-Erweiterung).

---

## 5. Tests, Build, CI

- [x] **P1 – 5.1 Die Tests testen nicht den Produktionscode**
  [test/test_duty/test_duty.cpp:13-47](../test/test_duty/test_duty.cpp#L13-L47), [test/test_morse/test_morse.cpp:12-60](../test/test_morse/test_morse.cpp#L12-L60)
  Beide Testdateien enthalten eine *Kopie* der Morse-Tabelle bzw. der Duty-Formeln. Fehler im echten Code (z. B. 1.1, 2.2, 2.3, 3.1) bleiben so unentdeckt.
  **Fix:** Eine `[env:native]` in `platformio.ini` (`platform = native`, `test_build_src = yes`, `build_src_filter` auf die Arduino-freien Module), die Tests gegen die echten Header/Quellen.
  **Erledigt:** Neue Envs `native` (GCC im PATH) und `native_win` (MinGW über PlatformIO) mit `test_build_src = yes` und einem `build_src_filter` auf die hardwareunabhängigen Module. Beide Testdateien binden jetzt die echten Header/Quellen ein, die Kopien der Tabellen und Formeln sind entfernt. Aufruf: `pio test -e native_win` (Windows) bzw. `pio test -e native`. Die Firmware-Env ignoriert Tests (`test_ignore`).
- [ ] **P1 – 5.2 Fehlende Tests für die kritischen Pfade**
  - Config-Hash: zwei Füchse mit unterschiedlicher ID, sonst gleich → kompatibel (deckt 1.1 auf)
  - Slotplan/Sendezeitpunkt über viele Zyklen ohne Akkumulation (1.2/1.3)
  - Gleicher effektiver Zyklus auf allen Füchsen (1.5)
  - Duty-Bilanz bei Profilwechsel (2.1), Callsign-Slot-Länge ≤ Slot (2.4)
  - CRC32 gegen bekannte Testvektoren (`"123456789"` → `0xCBF43926`) (3.1)
  - Regression mit synthetischer Drift (+/− ppm, Ausreißer), Vorzeichen (1.10)
  **Teilweise erledigt:** Neu: `test_config` (Hash ohne Fox-ID), `test_schedule` (Slotplan, alle Fox-Teilmengen ISM-konform, Rufzeichen-Slot), `test_duty` (Budget, Guard, Profilwechsel-Szenario), `test_timesync` (Drift ±, Ausreißer, Ringpuffer), `test_sync` (Verbund-Simulation: Ausrichtung nach 3 h, Kollision, Mismatch, Nachzügler, Masterausfall, vorgezogener Start, Allein-Boot). CRC-Testvektor folgt mit 3.1.
- [ ] **P2 – 5.3 Build-Umgebungen**
  - Ein eigenes `[env:esp32c3_supermini]` (README nennt den Super Mini als Hauptziel, gebaut wird nur `lolin_c3_mini`).
  - `platform = espressif32 @ 6.8.x` exakt pinnen (reproduzierbare Builds).
  - `lib_deps =` ist leer, also entfernen.
- [ ] **P2 – 5.4 CI (GitHub Actions)**: `pio run` für alle Envs plus `pio test -e native` bei jedem Push/PR. Das Firmware-Binary als Release-Artefakt anhängen, damit Nutzer ohne PlatformIO flashen können (z. B. mit ESP Web Tools).
- [ ] **P3 – 5.5 Lokale Umgebung**: Hier schlug der Flash-Image-Schritt fehl (`ModuleNotFoundError: intelhex` im PlatformIO-esptool), die Quellen kompilieren aber. In der README unter „Build" einen Hinweis zur Fehlerbehebung ergänzen (`pio pkg update` bzw. PlatformIO neu installieren).

---

## 6. Bedienung / Usability

- [ ] **P1 – 6.1 Startablauf ist für den Feldeinsatz unklar und lang**
  Jeder Fuchs braucht ≥ 120 s bis zur ersten Aussendung, ohne sichtbaren Countdown oder Fortschritt. Ob der Sync erfolgreich war (Anzahl gefundener Füchse), sieht man nur mit Laptop.
  **Vorschläge:**
  - Nach Sync-Ende per LED die Anzahl der gefundenen Füchse blinken (z. B. n× kurz) und die eigene Fox-ID (n× lang).
  - Countdown in der seriellen Ausgabe („Start in 45 s, 3 Füchse gefunden").
  - Startphase vorzeitig beenden, sobald alle erwarteten Füchse gefunden und stabil sind.
- [x] **P1 – 6.2 Nach Konfigurationsänderung Neustart anbieten bzw. erzwingen**
  Nach Änderung von Fox-ID, Profil oder Rufzeichen: „Änderung gespeichert – Neustart erforderlich, jetzt neu starten? (J/N)". Mindestens Menüpunkt 10 sperren, bis neu gestartet wurde. Behebt 2.1 und 3.3.
  **Erledigt:** Nach dem Speichern von Fox-ID, Profil, Rufzeichen oder Werkseinstellungen fragt die CLI „Jetzt neu starten? (J/N)". Ohne Neustart ist Menüpunkt 10 gesperrt, und beim Auto-Timeout wird neu gestartet statt mit veraltetem Zustand zu senden.
- [ ] **P2 – 6.3 Konfiguration ohne Laptop**
  Derzeit geht das nur per USB-Seriell. Optionen:
  - BOOT-Taste (GPIO9) nach dem Start: kurz drücken = Fox-ID weiterschalten, lang = Profil; Rückmeldung per LED.
  - **Web-Serial-Konfigurationsseite** (statisches HTML, Chrome/Edge): ein Klick auf „Verbinden" und die Konfiguration per Formular. Dafür braucht es ein maschinenlesbares Kommando-Interface (6.5).
  - Konfiguration aller Füchse auf einmal: Ein Fuchs schickt seine Profil/WPM-Einstellung per ESP-NOW an alle, nur die Fox-ID bleibt individuell.
- [ ] **P2 – 6.4 LED im Sendebetrieb abschaltbar machen**
  Eine im Ton-Takt blinkende LED verrät einen versteckten Fuchs und kostet Strom. Option „LED: aus / nur Startphase / immer".
- [ ] **P2 – 6.5 Skriptbare Befehle zusätzlich zum Nummernmenü**
  Z. B. `fox 3`, `profil c`, `call DL1ABC`, `status`, `start`, `reboot`, optional `status json`. Erleichtert die Bedienung, automatisierte Tests und eine Web-UI. Die Menünummern 10/11 sind fehleranfällig (Tippfehler „1" + Enter statt „10").
- [ ] **P2 – 6.6 Im Fehlerzustand die CLI öffnen**
  Statt SOS-Blinken plus Neustart sollte die CLI in den Fehlerzuständen erreichbar bleiben. Die Meldung „Fox-Nummer über CLI ändern" ist sonst nicht umsetzbar (siehe 3.2).
- [ ] **P2 – 6.7 Testaussendung**
  `docs/cli.md` schließt einen Testmodus ausdrücklich aus. Für den Aufbau im Gelände ist eine einzelne, duty-konforme Testaussendung („T) Kennung einmal senden") aber sehr hilfreich, ebenso eine Option „Peilempfänger-Check".
- [ ] **P3 – 6.8 Untermenü-Timeouts** (10 s) sind knapp. Eine Zeitüberschreitung sollte mit „Abgebrochen, Wert unverändert" klar gemeldet werden.
- [ ] **P3 – 6.9 Profil D: Hinweis auf den Reset**
  Wegen des 2,2-s-Deep-Sleep ist die CLI nur nach dem Drücken der RST-Taste erreichbar. In der CLI-Startmeldung und in der README sichtbar erwähnen.
- [ ] **P3 – 6.10 Sonderzeichen in der Ausgabe** (`–`, `…`, `µ`, Umlaute)
  Manche Terminals (PuTTY mit Standard-Codepage, Arduino Serial Monitor älterer Versionen) zeigen sie falsch an. Entweder auf ASCII beschränken oder UTF-8 als Voraussetzung dokumentieren.

---

## 7. Dokumentation

- [ ] **P2 – 7.1 README: Plattformaussage falsch** ([README.md:325-328](../README.md#L325-L328))
  `espressif32 @ ^6.8.0` liefert Arduino-Core **2.0.17**, nicht ≥ 3.0 (siehe Build-Ausgabe `framework-arduinoespressif32 @ 3.20017…`).
- [ ] **P2 – 7.2 README: Aussage zu Lolin C3 Mini / USB prüfen** ([README.md:228-231](../README.md#L228-L231), `docs/cli.md`)
  Nach meinem Kenntnisstand nutzt auch der Lolin C3 Mini natives USB (kein CH340). Zudem hat der Lolin C3 Mini (je nach Revision) auf GPIO7 ggf. eine RGB-LED (WS2812) statt einer einfachen LED, *prüfen*.
- [ ] **P2 – 7.3 README: LED-Tabelle** entspricht nicht der Implementierung (siehe 3.5).
- [ ] **P2 – 7.4 Zahlen konsistent machen**
  Die Zyklus- und Duty-Tabellen in README, `docs/duty_cycle.md` (Profil C, 1 Fuchs: 18.500 ms vs. 14.200 ms) und die CLI-Ausgabe weichen voneinander ab (Ursache 2.3). Idealerweise erzeugt ein Skript oder Test die Tabellen aus dem Code.
- [ ] **P2 – 7.5 `docs/sync_protocol.md` / `docs/time_sync.md`** an das tatsächliche (bzw. neue) Verhalten anpassen: Config-Hash ohne Fox-ID, kein Re-Sync, RTC-Uhr vs. Quarz, tatsächliche Rolle des Masters.
- [ ] **P3 – 7.6 Fehlende Datei `NOTICE`**: Sie wird in README und Lizenzhinweis referenziert, existiert aber nicht.
- [ ] **P3 – 7.7 Tippfehler/Artefakte**
  „Einsätzrn" (`docs/time_sync.md`); doppelte Leerzeichen und Kommas in Namen („Harm  , DK4HAA", „Sebastian , DM8SB") in allen Datei-Headern, README und `AUTHORS.md`; Aufzählung in `AUTHORS.md` mit leeren Einträgen („, CLI", „Tests, ").
- [ ] **P3 – 7.8 Hardware-Doku ergänzen**: Stromaufnahme (Sync, TX, Deep-Sleep), Akkulaufzeit, Antenne/Yagi-Hinweis (Gerber liegt im Repo-Root unter `ant/`), Schaltplan bzw. Verweis auf `tx/`.
- [ ] **P3 – 7.9 Root-README** (`../README.md`) ist nur ein Zweizeiler. Sie sollte auf `tx-Software/`, `tx/` (Original-Sketch, Gerber, Gehäuse) und `ant/` verlinken.

---

## 8. Verbesserungsvorschläge (Design)

### 8.1 Neues Timing-Konzept für den Verbund (behebt 1.2–1.9, 2.2)

1. **Gemeinsame Zeitbasis:** Der Master verteilt im Beacon `master_time_us` und `cycle_start_us` (in Master-Zeit). Slaves rechnen nur gegen den Master: `offset = local − master`.
2. **Absolute Sendezeitpunkte:** `t_tx(k) = cycle_start + k·cycle + slot_idx·slot`. Nach dem Senden wird *nicht* „Zyklus minus Vorlauf" geschlafen, sondern bis `t_tx(k+1) − vorlauf`. Danach wird aktiv (Busy-Wait oder Light-Sleep) bis `t_tx(k+1)` gewartet. So akkumulieren sich weder TX-Dauer noch Bootzeit.
3. **Zeit über den Deep-Sleep hinweg:** `esp_rtc_get_time_us()` bzw. ein im RTC-RAM gespeicherter Bezug. `esp_timer` beginnt nach jedem Aufwachen bei 0.
4. **Verbundweite Parameter:** Zyklus und Slot werden vom Master aus der längsten aktiven Sequenz (bzw. fest MO5) berechnet und im Beacon vorgegeben, alle übernehmen sie.
5. **Periodischer Re-Sync:** Der Master sendet z. B. alle N Zyklen (oder in jedem eigenen Slot) unmittelbar nach seiner Aussendung einen kurzen ESP-NOW-Beacon. Die Slaves wachen dafür ein Fenster von ±(Drift-Budget) auf und horchen. Kosten: wenige ms Funkbetrieb pro Re-Sync, dafür beliebig lange Einsätze ohne Neustart.
6. **Fallback:** Hört ein Slave den Master K-mal nicht, sendet er im zuletzt bekannten Raster weiter (mit wachsendem Schutzabstand) und meldet das per LED.
7. **Kalibrierung des RTC-Oszillators:** Während der Sync-Phase den RC-Takt gegen den Quarz messen (`rtc_clk_cal()` bzw. Messung über zwei kurze Schlafphasen) und den Sleep damit korrigieren. Das wäre der eigentliche Sinn der „Driftkorrektur".

### 8.2 Duty-Guard als echte Sendezeitbilanz
Im RTC-RAM einen Ringpuffer der letzten TX-Starts und -Dauern führen (z. B. 1 h). Vor jedem TX die Bilanz prüfen. Bei Überschreitung verschieben statt nur loggen. Der Guard bleibt so unabhängig von Fehlern in der Zeitplanung, als letzte Sicherheitsinstanz.

### 8.3 Robustere Verbundbildung
- Event-/Gruppen-ID (z. B. 16 Bit, in der CLI setzbar) im Beacon, damit Fremdgeräte oder Nachbarveranstaltungen ignoriert werden.
- Optional ESP-NOW-Verschlüsselung (PMK/LMK aus Gruppen-ID abgeleitet) oder zumindest ein HMAC über den Beacon.
- Absender-MAC aus `recv_info->src_addr` statt aus dem Payload nehmen.

### 8.4 Energie
- Konfiguration im RTC-RAM statt NVS-Lesen pro Zyklus (3.7).
- `Serial.begin()` und `delay` im Wake-Pfad nur bei angeschlossenem USB (`HWCDC`-Verbindungsstatus prüfen).
- Strombudget messen und dokumentieren (Sync-Phase mit WiFi ~80–100 mA über 2–3 min ist relevant).

---

## 9. Erweiterungsvorschläge (Features)

- [ ] **Akkuspannungsmessung** (ADC + Spannungsteiler): Anzeige in der CLI, Warnung per LED, optional per Morse („LB") im AFU-Modus. Abschalten unter einer Mindestspannung, um Tiefentladung zu vermeiden.
- [ ] **Startzeit / Startverzögerung**: „Sendebeginn in 30 min" bzw. zu einer Uhrzeit (über den Master synchronisiert). Die Füchse können so vorab ausgelegt werden, ohne Akku zu verbrauchen, ARDF-typisch.
- [ ] **Betriebsende / maximale Laufzeit**: automatisches Abschalten nach X Stunden (Akku- und Rechtssicherheit, vergessene Füchse).
- [ ] **AFU-Profil nach IARU-ARDF-Regeln** (z. B. 60 s pro Fuchs, MO-Kennungen, Zielsender „MO" Dauerbetrieb), klar getrennt vom ISM-Modus (siehe 2.6).
- [ ] **Foxoring/Sprint-Varianten**: Sprint mit Slow/Fast-Füchsen („MOE…MO5" / „S"), Zielbake. Im ISM nur mit Duty-Grenze möglich, im AFU-Modus frei.
- [ ] **Web-Serial-Konfigurator** als statische Seite (siehe 6.3), inklusive Firmware-Flashen per ESP Web Tools.
- [ ] **Statistik**: Anzahl Aussendungen, Laufzeit, Neustarts, Sync-Qualität im RTC/NVS. Nach dem Einsatz in der CLI abrufbar, gut zur Fehlersuche.
- [ ] **Konfigurierbare Tonhöhen und WPM** pro Fuchs (innerhalb der Duty-Grenzen), statt nur fester Tabellen.
- [ ] **Unterstützung > 5 Füchse** bzw. variable Fuchsanzahl (MAX_FOXES ist überall hart verdrahtet).
- [ ] **Externer 32-kHz-Quarz als Hardware-Option** für präzisen Deep-Sleep (siehe 1.6), mit Compile-Flag.

---

## 10. Empfohlene Reihenfolge

1. **Sofort (Sicherheit):** 2.1 und 2.2 (Duty bei Profilwechsel, Guard steuert das Timing), 6.2 (Neustart nach Änderung), 2.5 (GPIO-Hold im Deep-Sleep).
2. **Verbund lauffähig machen:** 1.1 (Hash), dann das Timing-Konzept 8.1 mit 1.2–1.9 und 1.11. Parallel 5.1/5.2 (native Tests gegen den echten Code) als Absicherung.
3. **Robustheit:** 3.1 (CRC), 3.2 (Fehlerbehandlung), 3.4/3.5 (nicht-blockierende LED/CLI), 3.7 (Config im RTC-RAM), 2.4 (Rufzeichen-Slot).
4. **Bedienung & Doku:** 6.1, 6.3–6.7, Abschnitt 7.
5. **Erweiterungen:** Abschnitt 9 nach Bedarf. Am meisten Nutzen bringen voraussichtlich Akkuüberwachung, Startverzögerung und der Web-Serial-Konfigurator.
