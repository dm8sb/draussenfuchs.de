# Review & TODO – ARDF-Fuchsjagd-Sender ESP32-C3 (tx-Software)

**Stand:** 2026-10-27 · Branch `bugfix/review-fixes` · Firmware 2.1.0 (Sync-Protokoll v3)

Diese Datei hat zwei Teile:

- **Teil A – aktueller Stand:** Ergebnis des zweiten, vollständigen Reviews nach der Bugfix-Runde, offene Befunde und Hardware-Testplan.
- **Teil B – Archiv:** die Befunde der ersten Review-Runde mit ihrem Erledigungsstand. Die Zeilenangaben dort beziehen sich auf den Stand **vor** der Bugfix-Runde (Commit `6da8696`).

**Prioritäten**

| Prio | Bedeutung |
|:-----|:----------|
| **P0** | Kernfunktion kaputt oder Regelverstoß (ISM-Duty) möglich – vor dem nächsten Einsatz klären |
| **P1** | Fehler mit spürbarer Auswirkung im Betrieb |
| **P2** | Qualität, Wartbarkeit, Bedienkomfort |
| **P3** | Kosmetik, Doku, nice-to-have |

---

# Teil A – Aktueller Stand

## A.0 Zweiter Review: Methode und Gesamtbewertung

**Methode:**
- Den gesamten Code (`src/`, `test/`, Doku) habe ich selbst durchgesehen. Zusätzlich hat ein unabhängiger Reviewer ohne Kenntnis meiner Bewertung geprüft und kritische Szenarien mit einer eigenen Simulation gegen den Produktionscode nachgestellt.
- Alle bestätigten Befunde mit klarer Ursache sind behoben (Commit `5013a50`) und mit Tests abgesichert.
- Firmware-Build ohne Warnungen (`-Wall -Wextra -Wsign-compare -Wformat`): RAM 14,1 %, Flash 56,1 %.
- 75 native Tests grün (`pio test -e native_win`), darunter eine Verbund-Simulation mit bis zu 5 Füchsen, Quarzabweichung, Latenz-Jitter, Neustarts und Ausfällen.
- **Nicht** geprüft: echte Hardware (siehe A.1).

**Gesamtbewertung:**
- Die Kernfehler der ersten Runde sind behoben:
  - Ein Verbund kommt zustande, und alle Füchse senden im gemeinsamen Slotraster. In der Simulation liegt die Abweichung nach 3 h im Bereich weniger Millisekunden.
  - Der Duty-Guard steuert das Timing und prüft vor jeder Aussendung den tatsächlichen Abstand.
  - Profilwechsel können die Duty-Grenze nicht mehr aushebeln.
  - Die Konfiguration ist korrekt CRC-gesichert.
- Logik und Hardware sind getrennt; die Logik ist nativ testbar.
- Die Bedienung ist nicht-blockierend, und im Fehlerzustand bleibt die CLI erreichbar.
- Verbleibende Risiken:
  - Verhalten auf echter Hardware (Ruhestrom, Funk, Pin-Hold).
  - Zwei Grenzfälle der Verbundbildung, die eine Protokoll- oder Designentscheidung brauchen (A.2.1, A.2.2).

**Im zweiten Review gefunden und behoben** (Fehler im neuen Sync-Code der Bugfix-Runde):

| Befund | Behebung |
|:-------|:---------|
| Neustart des Masters ließ den Plan in der alten Master-Uhr stehen. Slaves rechneten 1:1 um und beendeten den Sync vorzeitig in falschem Raster. | Vor dem Einfrieren werden Plan und Messung verworfen und neu aufgebaut. Danach bleiben beide erhalten, und Beacons der neuen Instanz werden ignoriert. Der neu gestartete Fuchs reiht sich ins Raster ein (Tests `test_master_restart_*`). |
| Menüpunkt 8/9 verwarf die laufende Messung (gleiche Wirkung). | Menüpunkt 8 löscht nur die gespeicherte Anzeige-Messung. |
| Automatischer Neustart nach Kollision oder Abweichung erzeugte nach dem Sendebeginn einen unsynchronisierten Allein-Master. Dasselbe galt für den Auto-Neustart bei ausstehender Konfigurationsänderung. | Kein Auto-Neustart mehr bei Verbundfehlern. Verpasster Sendebeginn und ausstehender Neustart in einem laufenden Verbund werden Fehlerzustände. Die LED warnt schon während des Syncs. |
| Ein später eingeschalteter Fuchs mit höchster MAC übernahm den Master und löschte bei allen die Driftmessung. Dasselbe passierte bei Menüpunkt 10 kurz nach dem Start. | „Klebriger" Master: Gewählt wird nur unter Füchsen, die fertig zugehört haben. Menüpunkt 10 wartet mindestens 30 s Messzeit ab. |
| Die Planreserve (1 ms) war kleiner als die erlaubte Startverspätung (2 ms), der Guard konnte fälschlich blockieren. | Reserve 100 ppm + 10 ms. |
| Pläne aus Beacons wurden ungeprüft übernommen. WLAN blieb bei ESP-NOW-Fehler an. `boot_id` wurde ohne aktiven Funk erzeugt. | Plausibilitätsprüfung, WLAN aus, `boot_id` erst nach dem WLAN-Start. |
| CLI: Auch derselbe Wert erzwang einen Neustart, NVS-Fehler blieben unbemerkt, Menüpunkt 6 war im Fehlerzustand leer. | Behoben. Zusätzlich ein Hinweis, dass das Rufzeichen in Profil D nicht gesendet wird. |
| AFU-Rufzeichen-Slot mit nur 250 ms Schutzabstand. | 500 ms. |
| Testlücken: Master-Neustart, später Master, AFU-Teilmengen, inhaltsleere Morse-Tests, zu lockere 3-h-Toleranz. | Tests ergänzt bzw. korrigiert; Toleranz 10 ms bei voller Messdauer. |

---

## A.1 Hardware-Verifikation (P0 – offen)

Die Bugfix-Runde ändert das Laufzeitverhalten grundlegend. Folgendes ist nur per Simulation und Build geprüft und muss vor einem Einsatz auf echter Hardware bestätigt werden:

- [ ] **Synchronität im Verbund:** 3–5 Füchse, versetzt eingeschaltet. Slotbeginn per Empfänger oder Logic-Analyzer an GPIO3/GPIO4 über mindestens 2–3 h messen. Erwartung: Abweichung im ms-Bereich, keine Überlappung.
- [ ] **Duty-Cycle messen:** Versorgungszeit an GPIO4 über 1 h für jedes Profil, mit 1 und 5 Füchsen sowie im AFU-Modus. Erwartung ≤ 9,5 % (AFU-Rufzeichen-Slot ≤ 50 %).
- [ ] **Ruhestrom:** Profile A–C nach dem Sync (wach, 80 MHz, WLAN aus; erwartet etwa 10–20 mA), Sync-Phase (WLAN aktiv), Profil D im Deep-Sleep (µA). README-Abschnitt „Stromverbrauch" mit Messwerten ergänzen.
- [ ] **GPIO-Hold im Deep-Sleep (2.5):** DATA und TX-VCC bleiben LOW; kein Stromfluss ins TX-Modul.
- [ ] **Profil D Zyklus (L2):** Abstand der Aussendungen messen; der RC-Sleep-Timer ist ungenau. Wenn der Abstand unter 2432 ms liegt, die Schlafzeit um einige Prozent verlängern.
- [ ] **Übernahme der v2-Konfiguration:** Ein Board mit 2.0.x-Konfiguration auf 2.1.0 flashen; Fox-ID, Profil und Rufzeichen müssen erhalten bleiben.
- [ ] **Fehlerpfade:** Kollision, Abweichung, Nachzügler nach dem Einfrieren, Neustart des Masters vor und nach dem Einfrieren, Neustart kurz vor dem Start. LED-Muster und Meldungen müssen der README entsprechen.
- [ ] **CLI auf beiden Boards** (Super Mini, Lolin C3 Mini): Eingabe, Backspace, Zeitüberschreitung, Fehlerzustand. Lolin C3 Mini: LED an GPIO7 (ggf. RGB-LED) prüfen.
- [ ] **`setCpuFrequencyMhz(80)`** nach dem Sync: USB-CDC-Ausgabe und Tonfrequenz unverändert.

---

## A.2 Offene Befunde

### A.2.1 P1 – Neustart nach dem Sendebeginn erzeugt einen unsynchronisierten Allein-Boot
- **Ursache:** Nach dem Sendebeginn sind alle Füchse funkstill. Ein Fuchs, der danach (neu) startet, hört keinen Verbund und arbeitet als Allein-Boot mit eigenem Raster. Das betrifft RST-Taste, Brownout bei schwachem Akku während TX, Panic/Watchdog und einen manuellen Neustart aus der CLI im Fehlerzustand. Er überlappt dann periodisch mit dem Verbund.
- **Bereits entschärft:** Kein automatischer Neustart mehr bei Verbundfehlern. Ein Neustart, der den Sendebeginn noch kennt, aber keinen Plan mehr bekommt, führt zum Fehlerzustand (`missedStart()`). README und CLI fordern „alle Füchse gemeinsam neu starten".
- **Vorschlag (Designentscheidung):** Nach einem Software-, Brownout- oder Panic-Reset (`esp_reset_reason()`) nicht automatisch allein senden, sondern nur nach Power-on oder nach ausdrücklicher Bestätigung (Menüpunkt 10). Nachteil: Ein einzelner Fuchs muss nach einer Konfigurationsänderung einmal bestätigt bzw. aus- und eingeschaltet werden.

### A.2.2 P1 – Einseitige oder teilweise Funkverbindung kann zwei Raster ergeben
- **Simulation des Reviewers:**
  - Hört A den Fuchs B nicht, B aber A, so wird A Solo-Master, während B ihn einplant. Zwei Zyklen laufen nebeneinander und überlappen regelmäßig.
  - In einer Kette A–B–C ohne Verbindung A↔C bekommt C keinen Plan.
- **Praxis:** In der Startphase liegen die Füchse meist nebeneinander, das Risiko ist daher eher gering. Beim Einschalten an verteilten Standorten ist es real.
- **Vorschlag (Protokollerweiterung v4):**
  - MAC des gewählten Masters im Beacon mitsenden.
  - Der Master plant nur Füchse ein, die ihn bestätigen.
  - Ein fremder Master gilt als Konflikt (nicht einfrieren).
  - Ohne Plan bis zum Sendebeginn folgt der Fehlerzustand.
- **Kurzfristig dokumentiert:** Alle Füchse gemeinsam und nah beieinander einschalten.

### A.2.3 P2 – Kurzer Ausfall des Masters kostet die Driftmessung
Hören die Slaves den Master 3 s lang nicht (6 verlorene Beacons), wählen sie neu. Kehrt er zurück, setzt sich nach der Regel „zwei Master → höhere MAC" eventuell wieder der alte durch. Jeder Wechsel verwirft die Messung; bei großem Abstand zum Start ist das unkritisch. **Vorschlag:** Messung je Master (MAC + `boot_id`) aufbewahren und `PEER_TIMEOUT_MS` für den Master verlängern.

### A.2.4 P2 – Kein Hinweis, wenn ein Slave ohne Driftschätzung startet
Die Log-Zeile beim Start zeigt „Drift … (nicht geschätzt)", aber keine LED-Warnung. Ohne Drift wächst die Abweichung mit der Quarzdifferenz (bis einige 100 ms/h); im ISM-Betrieb sind die Lücken ≥ 2 s groß, im AFU-Modus 500 ms. **Vorschlag:** einmaliges LED-Muster bzw. eine CLI-Warnung; Einschalten mindestens 30 s vor Start (ist dokumentiert).

### A.2.5 P2 – Sendebetrieb und CLI-Zustandsautomat sind nicht unit-getestet
`runArdfMode()` (verpasster Slot, Guard bei Startverspätung, Rufzeichen-Zähler) und `cli.cpp` hängen an `esp_timer`/`Serial`. **Vorschlag:** Zeitquelle bzw. `Stream` injizieren und die Logik nativ testen.

### A.2.6 P3 – Firmware 2.0.x im selben Funkbereich wird ignoriert
Beacons mit anderer Länge oder Protokollversion werden ohne Meldung verworfen. Ein noch nicht aktualisierter Fuchs bildet sein eigenes Raster. **Vorschlag:** Unbekannte Beacons auf Kanal 1 mit plausibler Länge (v2: 47 Byte, erstes Byte 2) als „fremde Firmware" melden.

### A.2.7 P3 – Kleinigkeiten
- [ ] Menüpunkt 10 am Slave schließt die CLI bis zum Reset (nur Menüpunkt-6-Status geht verloren).
- [ ] `printMac()` existiert in `cli.cpp` und `sync_espnow.cpp`; `main.cpp` formatiert die MAC selbst. In eine gemeinsame Hilfsfunktion auslagern.
- [ ] `FLAG_AFU_MODE` wird gesendet, aber nicht ausgewertet (rein informativ; die AFU-Kompatibilität prüft der Config-Hash).
- [ ] Die Übernahme der v2-Konfiguration prüft bewusst keine CRC, weil deren Tabelle fehlerhaft war; Version und Felder werden geprüft. Im Code kommentiert.
- [ ] Profil D: Die Pause hat keine Reserve für den RC-Sleep-Timer (siehe A.1, L2).

### A.2.8 Offen aus der ersten Runde (Entscheidung oder Messung nötig)
- [ ] **2.6** Rechtliche Prüfung des AFU-Betriebs durch einen Lizenzinhaber (Kennungsintervall, unbemannter Betrieb).
- [ ] **7.7** Namen „Harm  , DK4HAA" / „Sebastian , DM8SB" in Copyright-Zeilen: Die Kürzung wirkt beabsichtigt; selbst entscheiden.
- [ ] **7.8** Messwerte Stromaufnahme/Akkulaufzeit, Antennen- und Schaltplanverweise.
- [ ] **4.8** Version per `git describe` (Build-Skript), **4.11** `pio check`/clang-format – Werkzeug-Erweiterungen.

---

## A.3 Empfohlene Reihenfolge (nächste Schritte)

1. **Hardware-Verifikation A.1**, insbesondere Synchronität, Duty-Cycle und Ruhestrom. Erst danach im Einsatz verwenden.
2. **Designentscheidung A.2.1** (kein automatischer Allein-Boot nach Software-Reset) und, falls Füchse verteilt eingeschaltet werden, **A.2.2** (Protokoll v4 mit Master-Bestätigung).
3. A.2.3–A.2.5 (Robustheit und Testbarkeit des Sendebetriebs).
4. Erweiterungen aus Teil B, Abschnitt B.9 nach Bedarf. Am meisten Nutzen bringen voraussichtlich Akkuüberwachung, Startverzögerung, Web-Serial-Konfigurator und Betriebsende.

---

# Teil B – Archiv: erste Review-Runde

Die Bugfix-Runde hat diese Befunde in der empfohlenen Reihenfolge abgearbeitet (Commits `29ac264` bis `4164a00`, Nacharbeit `5013a50`). Legende: `[x]` erledigt, `[ ]` teilweise erledigt (siehe Vermerk), `[-]` nicht umgesetzt (Erweiterung bzw. bewusste Entscheidung). Zeilenangaben beziehen sich auf den Stand vor der Bugfix-Runde.

## B.0 Gesamtbewertung der ersten Runde (Kurzfassung, Stand vor den Fixes)

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

## B.1 Kritische Funktionsfehler (Sync / Timing)

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
  **Erledigt:** Slaves übernehmen Zyklus, Slot, Reihenfolge und Sendebeginn aus dem Beacon des Masters (`copyPlanFromBeacon()` in [sync_logic.cpp](../src/sync_logic.cpp), mit Plausibilitätsprüfung). Der Sendebeginn wird im Verbund vererbt (`FLAG_HAS_START`). Seit dem zweiten Review bleibt der Master Master; ein später eingeschalteter Fuchs übernimmt nicht und löscht keine laufende Messung. Tests: `test_five_foxes_stay_aligned_3h`, `test_late_high_mac_keeps_master`, `test_master_leaves`, `test_master_restart_*`.

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

## B.2 Regulatorik / HF-Sicherheit

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
  **Erledigt:** Mit dem Slotplan gelöst (vorgezogen aus Schritt 3): Jeder Fuchs meldet seine längste Aussendung (`tx_max_ms`). Der Slot ist mindestens so lang wie die längste Aussendung im Verbund + 500 ms Schutzabstand. Im AFU-Modus wird der Zyklus dadurch länger (Profil C mit „DL1ABC/P": Slot ≈ 7,7 s). Tests: `test_callsign_slot_fits`, `test_all_subsets_afu`.

- [x] **P1 – 2.5 GPIO-Zustand im Deep-Sleep nicht definiert** *(prüfen)*
  [main.cpp:358-375](../src/main.cpp#L358-L375)
  Im Deep-Sleep verlieren GPIO3 (DATA) und GPIO4 (TX-VCC) ihren Ausgangszustand. Ohne Pull-down oder `gpio_hold_en()`/`gpio_deep_sleep_hold_en()` kann das Modul teilweise versorgt sein oder der Daten-Pin floaten, was ungewollte Abstrahlung und Stromverbrauch bedeuten kann.
  **Fix:** Vor dem Schlafen `gpio_hold_en(TX_VCC_EN_PIN)` setzen (LOW), DATA ebenso; nach dem Aufwachen `gpio_hold_dis()`. Alternativ Hardware-Pull-downs vorsehen. Am Board mit Oszilloskop und Strommessung verifizieren.
  **Erledigt:** Vor dem Deep-Sleep werden DATA (GPIO3) und TX-VCC (GPIO4) auf LOW gesetzt und per `gpio_hold_en()` + `gpio_deep_sleep_hold_en()` gehalten. `tx::init()` hebt das Halten nach der LOW-Konfiguration wieder auf (glitchfrei laut IDF-Doku). Auf Hardware noch per Strommessung/Oszilloskop zu bestätigen.

- [ ] **P2 – 2.6 Rechtliche Aussagen zum AFU-Modus fachlich prüfen lassen**
  README „Rechtliche Hinweise", [config.h:50](../src/config.h#L50)
  Dass im 10. Slot 50 % Duty zulässig sind, die Slots 1–9 aber unter SRD-Regeln laufen, ist eine Mischform. Entweder ist der Betrieb Amateurfunk, dann gelten AFuV-Regeln (u. a. Kennungsintervall, unbemannter Betrieb), oder SRD. Empfehlung: einen eigenen **AFU-Modus** mit IARU-ARDF-Standardtiming (z. B. 60 s pro Fuchs, 5-min-Zyklus) und Kennung nach AFuV, klar getrennt vom ISM-Modus. Das Kennungsintervall gegen die AFuV prüfen: Profil A mit 1 Fuchs × 10 Slots ≈ 10 min ist grenzwertig.
  **Teilweise erledigt:** README und `docs/duty_cycle.md` formulieren die AFU-Regeln jetzt als *technische Umsetzung*, die der Betreiber rechtlich prüfen muss (Kennungsintervall, unbemannter Betrieb; Profil A mit einem Fuchs: ca. 9 min zwischen zwei Rufzeichen). **Offen:** fachliche/rechtliche Prüfung durch einen Lizenzinhaber; ein separater AFU-Modus nach IARU-Regeln wäre eine Erweiterung.

- [x] **P2 – 2.7 Duty-Cycle-Bezugszeitraum dokumentieren**
  ERC/REC 70-03 definiert den Duty-Cycle über ein Beobachtungsintervall von 1 h. Der Guard prüft pro Zyklus, was strenger und damit sicher ist. Trotzdem in `docs/duty_cycle.md` erläutern und die Quellen (Vfg./Allgemeinzuteilung der BNetzA, EN 300 220) mit Nummer zitieren.
  **Erledigt:** `docs/duty_cycle.md`: Bezugszeitraum 1 h nach ERC/REC 70-03 erläutert; die Firmware prüft strenger je Aussendungsabstand. Quellen (BNetzA-Allgemeinzuteilung, ERC/REC 70-03, EN 300 220) genannt, mit Hinweis auf eigene Prüfung der aktuellen Fassung.

---

## B.3 Weitere Fehler

- [x] **P1 – 3.1 CRC32-Tabelle ist fehlerhaft (116 von 256 Einträgen)**
  [config_store.cpp:17-61](../src/config_store.cpp#L17-L61)
  Gegen die Standard-CRC-32 (Polynom 0xEDB88320) geprüft. Erste Abweichungen bei Index 10, 13, 14, 15, 42, … Teilweise sind Werte doppelt, z. B. `0x3B6E20C8`. Funktional ist die Prüfsumme deterministisch, aber keine CRC-32 mehr: schwächere Fehlererkennung, keine Interoperabilität mit Fremdwerkzeugen.
  **Fix:** ROM-Funktion `esp_rom_crc32_le()` (bzw. `crc32_le()` aus `esp_crc.h`) verwenden oder die Tabelle zur Compile-Zeit per `constexpr` generieren. Achtung: Das ändert die gespeicherte CRC. NVS-Key/Version hochzählen (`cfg_v3`) oder beim Laden beide CRCs akzeptieren.
  **Erledigt:** Bitweise Standard-CRC-32 (Polynom 0xEDB88320) statt der fehlerhaften Tabelle; Testvektor „123456789" → 0xCBF43926 in `test_config`. Konfiguration jetzt Version 3 unter `cfg_v3` (Struktur `FoxConfig`). Gespeicherte v2-Konfigurationen werden beim ersten Start übernommen (Version + Feldprüfung, dann mit korrekter CRC neu gespeichert, alter Schlüssel gelöscht), Füchse müssen nach dem Update also nicht neu eingestellt werden. Nebenbei behoben: Ein Rufzeichen ohne Nullterminator galt bisher als gültig.

- [x] **P1 – 3.2 Fehlerbehandlung: Endlosschleife aus Neustarts, irreführende Meldung**
  [main.cpp:380-448](../src/main.cpp#L380-L448)
  - Bei Kollision oder Mismatch startet der Fuchs alle 60 s neu, synct erneut, scheitert erneut, und das endlos. Im Gelände bemerkt das niemand, und der Akku wird leergezogen (WiFi aktiv).
  - Ein Tastendruck löst den Neustart aus, die Ausgabe sagt aber „Automatische Wiederherstellung". Die Meldung „Bitte Fox-Nummer über CLI ändern" ist im Fehlerzustand nicht umsetzbar, weil es dort keine CLI gibt.
  - Ein Mismatch eines *einzelnen* Fremdgeräts legt den ganzen Verbund lahm.
  **Vorschlag:** Bei Mismatch den inkompatiblen Peer ignorieren und mit den kompatiblen weiterarbeiten. Bei Kollision deterministisch auflösen (z. B. höhere MAC gibt nach und meldet sich). Im Fehlerzustand direkt in die CLI springen. Nach N erfolglosen Versuchen in den Tiefschlaf gehen statt endlos neu zu starten.
  **Erledigt:** Neue Fehlerbehandlung in [main.cpp](../src/main.cpp) `handleError()`: kein Senden, LED-Muster, die **CLI bleibt bedienbar**, ein Tastendruck löst keinen Neustart mehr aus. Kollision und Abweichung zeigt die LED schon während der Sync-Phase, damit sie vor dem Sendebeginn behoben werden können. **Nach dem zweiten Review geändert:** Verbundfehler werden erst zum Sendebeginn festgestellt; ein automatischer Neustart danach würde unsynchronisiert allein senden. Deshalb gibt es einen Neuversuch nur noch bei einem ESP-NOW-Startfehler (höchstens 5×, Zähler in `RTC_NOINIT`). Sonst gilt nach 60 s ohne Eingabe: Tiefschlaf bis zum Reset, mit der Anweisung „alle Füchse gemeinsam neu starten". Bei einer Abweichung sendet bewusst niemand (zwei Raster würden kollidieren); gegen Fremdgeräte hilft die Gruppen-ID aus 8.3.

- [x] **P1 – 3.3 Konfigurationsänderungen in der CLI wirken nicht auf den laufenden Sync**
  [cli.cpp:107-162](../src/cli.cpp#L107-L162), [main.cpp:101](../src/main.cpp#L101)
  `initSync()` läuft vor der CLI mit der alten Fox-ID und dem alten Profil. Wird beides in der CLI geändert, verschicken die Beacons weiter die alten Werte, und `prof` in `runStartupPhase` zeigt auf das alte Profil. Siehe auch 2.1. → 6.2.
  **Erledigt:** Durch 6.2 behoben: Geänderte Konfiguration wird erst nach einem Neustart aktiv, Sync und Startphase laufen also immer mit der aktuellen Konfiguration.

- [x] **P2 – 3.4 Startphase blockiert den Sync durch `delay()`**
  [main.cpp:138-148](../src/main.cpp#L138-L148), [cli.cpp:111-117](../src/cli.cpp#L111-L117)
  Das LED-Blinken mit `delay(500)` und die blockierenden Eingabeschleifen der Untermenüs (bis 30 s) halten `processSync()` an: keine Beacons, keine Regression. Andere Füchse halten diesen dann ggf. für weg (sobald 1.12 umgesetzt ist).
  **Fix:** LED per nicht-blockierender Zustandsmaschine (`millis()`-basiert) steuern, Untermenüs als Zustände in `cli::process()` umsetzen.
  **Erledigt:** Neues Modul [led.cpp](../src/led.cpp) mit nicht-blockierenden Mustern (`led::update()`), die CLI ist ein Zustandsautomat ohne Warteschleifen. Die Startphase ruft CLI, Sync und LED pro Durchlauf ohne Blockade auf (`delay(5)`).

- [x] **P2 – 3.5 LED-Muster laut README sind nicht oder anders implementiert**
  [tx_driver.cpp:138-157](../src/tx_driver.cpp#L138-L157), [main.cpp:137-148](../src/main.cpp#L137-L148)
  `blinkHeartbeat()` (2 Hz Boot/Sync), `blinkIdle()` und `blinkDutyBlocked()` werden nie aufgerufen. Da `cli_active` ab Boot `true` ist, zeigt die LED in der Startphase immer „500 ms an / ~1,5 s aus". Sync läuft und CLI aktiv sind nicht unterscheidbar.
  **Erledigt:** Alle README-Muster implementiert und verwendet: 2 Hz während des Syncs, 0,5 Hz in der Startphase von Profil D, SOS bei Kollision, 3× kurz bei Abweichung/nicht im Slotplan, 10 Hz bei ungültiger Konfiguration/ESP-NOW-Fehler, 2× kurz bei Duty-Guard-Blockade. Die alten blockierenden `blink*()`-Funktionen sind entfernt.

- [x] **P2 – 3.6 Kleinere CLI-Fehler**
  - [cli.cpp:118](../src/cli.cpp#L118) `Serial.parseInt()` lässt CR/LF im Puffer, deshalb wird das Menü doppelt ausgegeben. `parseInt` akzeptiert außerdem „2abc".
  - [cli.cpp:179-204](../src/cli.cpp#L179-L204) Bei der Rufzeichen-Eingabe funktioniert Backspace nicht (das Zeichen landet im Puffer und wird als ungültig abgelehnt).
  - [cli.cpp:141-143](../src/cli.cpp#L141-L143) Bei der Profilwahl kommen die restlichen Zeichen eventuell erst nach dem Leeren an, der Effekt ist derselbe wie oben.
  - [cli.cpp:385-389](../src/cli.cpp#L385-L389) Die Werkseinstellungen setzen den RAM-Zustand von `timesync` nicht zurück (anders als Menüpunkt 8), und der laufende Sync behält die alten Werte.
  - [cli.cpp:355-358](../src/cli.cpp#L355-L358) / [time_sync.cpp:45](../src/time_sync.cpp#L45) „Letzte Aktualisierung vor x s" zählt nach dem Laden aus dem NVS ab Boot, obwohl die Werte von einem früheren Einsatz stammen.
  **Erledigt:** Mit der Zustandsautomaten-CLI behoben: Fox-Nummer muss genau eine Ziffer 1..5 sein (kein `parseInt`), CR/LF/CRLF werden sauber als ein Zeilenende behandelt (kein doppeltes Menü), Backspace funktioniert in allen Eingaben, nicht druckbare Zeichen werden ignoriert. Werkseinstellungen setzen auch die laufende Zeitmessung zurück. Menüpunkt 7 zeigt die aktuelle Messung gegen den Master und getrennt davon die letzte gespeicherte Messung (ohne irreführendes „vor x s").

- [x] **P2 – 3.7 Konfiguration wird bei jedem Aufwachen aus dem NVS gelesen**
  [main.cpp:88](../src/main.cpp#L88)
  Das kostet Zeit und Strom bei jedem Zyklus. Ist der NVS beschädigt, lädt der Fuchs *mitten im Einsatz* still die Werkseinstellungen (Fox 1, Profil C) und ändert damit seine Identität.
  **Fix:** Die Konfiguration nach dem Kaltstart in den RTC-RAM kopieren (mit CRC) und im Sendebetrieb nur von dort lesen.
  **Erledigt:** Profil D legt die Konfiguration vor dem Deep-Sleep als CRC-geschützte Kopie im RTC-RAM ab (`rtc_config`) und liest sie nach dem Aufwachen von dort; nur wenn die Kopie ungültig ist, wird aus dem NVS geladen. Die ARDF-Profile schlafen nicht mehr (siehe 1.6).

- [-] **P3 – 3.8 `ERR_INVALID_CONFIG` ist praktisch unerreichbar**
  `loadConfig()` ersetzt eine ungültige Konfiguration bereits durch die Defaults, deshalb schlägt die Prüfung in [main.cpp:168](../src/main.cpp#L168) nie fehl. Entweder entfernen oder `loadConfig`-Fehler als Warnung (LED) anzeigen.
  **Nicht umgesetzt:** Bewusst beibehalten als defensive Prüfung vor dem Senden: `loadConfig()` fällt zwar auf Werkseinstellungen zurück, die Prüfung kostet aber nichts und schützt vor künftigen Fehlern im Ladepfad. Ein Hinweis auf geladene Werkseinstellungen erscheint bereits als `[CFG]`-Meldung.

---

## B.4 Code-Qualität & Architektur

- [x] **P2 – 4.1 Toter bzw. unbenutzter Code entfernen oder anschließen**
  `rtc_initialized`, `rtc_cycle_start_us`, `rtc_own_slot_idx` (nur geschrieben), `sync::extendSyncWindow()`, `timesync::getCorrectedOffsetUs()`, `cli::resetIdleTimer()`, Rückgabewert von `cli::process()`, `isValidProfileId()`, `blinkHeartbeat/Idle/DutyBlocked`, `FoxProfile::fox_count`, die Parameter `with_callsign/callsign_len` in `calculateTxOnTimeMs` (inkl. unbenutztem `cs_ptr` in [fox_profile.cpp:124](../src/fox_profile.cpp#L124)), der Fall `ERR_DUTY_BLOCKED` in `handleError`.
  **Erledigt:** Entfernt bzw. ersetzt: RTC-Variablen des alten Timings, `extendSyncWindow()`, `getCorrectedOffsetUs()`, `calculateVorlaufUs()`, `resetIdleTimer()`, Rückgabewert von `cli::process()`, `isValidProfileId()`, `FoxProfile::fox_count`, `calculateTxOnTimeMs()` (samt `cs_ptr`), `ERR_DUTY_BLOCKED`, die `blink*()`-Funktionen, der unbenutzte Parameter `profile_id` von `sendArdfSlot()`. LED-Muster werden jetzt alle verwendet.
- [x] **P2 – 4.2 Identische if/else-Zweige** in [main.cpp:104-109](../src/main.cpp#L104-L109): beide rufen `initSync(...)` auf.
  **Erledigt:** Mit der neuen Startphase entfallen.
- [x] **P2 – 4.3 Doppelte Duty-Logik**
  `calculateEffectiveCycleMs()` und `checkDuty()` rechnen dasselbe zweimal; `calculateMinCycleMs()` existiert parallel dazu. Zu einer Funktion zusammenführen.
  **Erledigt:** `checkDuty()`/`calculateEffectiveCycleMs()` entfernt; der Zyklus kommt ausschließlich aus `schedule::computePlan()`, der Duty-Guard enthält nur noch Grenzwert, Mindestzyklus, Duty-Berechnung und den Laufzeit-Guard.
- [x] **P2 – 4.4 Globale `extern`-Kopplung zwischen `main.cpp` und `cli.cpp`**
  `g_config` und `g_start_normal_mode` sind globale Variablen. Besser: Die CLI bekommt eine Referenz auf die Konfiguration und meldet über einen Rückgabewert (`enum class CliResult { None, StartNormal, Reboot }`).
  **Erledigt:** `g_config` ist `static` in `main.cpp`; die CLI erhält die Konfiguration per Referenz (`cli::init(cfg, mode)`) und meldet Start-/Neustartwunsch über `startRequested()`/`restartRequired()`. Kein `extern` mehr.
- [x] **P2 – 4.5 Hardware-Abstraktion für Tests**
  `morse.cpp` gibt im reinen Rechenpfad über `Serial` aus ([morse.cpp:105](../src/morse.cpp#L105)), und `duty_guard.cpp` bindet `Arduino.h` ein. Rechenlogik (Morse-Timing, Duty, Slotplan, Regression) in Arduino-freie Dateien trennen, damit sie nativ testbar wird (→ 5.1).
  **Erledigt:** Rechenlogik vollständig Arduino-frei: `morse.cpp`, `fox_profile.cpp`, `duty_guard.cpp`, `config_check.cpp`, `schedule.cpp`, `time_sync.cpp`, `sync_logic.cpp`. Hardware/Funk/NVS in `morse_tx.cpp`, `sync_espnow.cpp`, `config_store.cpp`, `time_correction_store.cpp`, `tx_driver.cpp`.
- [x] **P2 – 4.6 Heap-Allokation in `computeRegression()`** ([time_sync.cpp:92-119](../src/time_sync.cpp#L92-L119))
  `new[]`, alle 10 s im Sync. Durch statische Puffer ersetzen (`MAX_MEASUREMENTS` ist bekannt). Der Bubble-/Selection-Sort mit O(n²) ist bei n = 120 vertretbar, `std::nth_element` wäre aber eleganter.
  **Erledigt:** `TimeModel` arbeitet mit statischen Puffern, ohne `new[]`.
- [x] **P3 – 4.7 Magische Zahlen**
  Timeouts der Untermenüs (10 s / 30 s), Blink-Zeiten, `delay(500)` USB-Stabilisierung, „7 dits" (mehrfach). Als benannte Konstanten in `config.h` ablegen.
  **Erledigt:** Benannte Konstanten: `CLI_PROMPT_TIMEOUT_MS`, `USB_CDC_SETTLE_MS`, `MAX_ERROR_RETRIES`, `morse::WORD_GAP_DITS`, Sync-Zeiten (`SYNC_LISTEN_MS`, `PEER_TIMEOUT_MS`, `PLAN_FREEZE_MS`, …), LED-Muster als benannte Tabellen in `led.cpp`.
- [ ] **P3 – 4.8 `ARDF_VERSION` doppelt definiert** ([platformio.ini:14](../platformio.ini#L14), [config.h:16-18](../src/config.h#L16-L18)), dazu README „Version 2.0.0 – Juli 2026". Version aus `git describe` per `extra_scripts` erzeugen.
  **Teilweise erledigt:** Version einheitlich 2.1.0 (Protokoll v3 ist inkompatibel zu 2.0.x). Der Fallback in `config.h` bleibt bewusst für Builds ohne Build-Flag. Version aus `git describe` per `extra_scripts` nicht umgesetzt (Build-Erweiterung).
- [x] **P3 – 4.9 Feste Array-Größe `[5]`** in `SyncBeacon::active_fox_ids` statt `MAX_FOXES`.
  **Erledigt:** Beacon und Slotplan verwenden `MAX_FOXES`.
- [x] **P3 – 4.10 Kommentar und Realität beim Arduino-Core**
  [tx_driver.cpp:17-19](../src/tx_driver.cpp#L17-L19) ist korrekt (Core 2.x), die README behauptet „≥ 3.0" (→ 7.1). Die `#if`-Kompatibilitätsschicht ist gut, mit Core 3.x aber ungetestet. Entweder eine Plattformversion festpinnen oder beide Varianten im CI bauen.
  **Erledigt:** README korrigiert: Plattform 6.13.0 liefert Core 2.0.17; die Core-3.x-Kompatibilitätsschicht ist als ungetestet gekennzeichnet.
- [ ] **P3 – 4.11 Statische Analyse und Formatierung**
  `build_flags = -Wall -Wextra` aktivieren, `pio check` (cppcheck/clang-tidy) einführen, eine `.clang-format` hinzufügen.
  **Teilweise erledigt:** `build_src_flags = -Wall -Wextra -Wsign-compare -Wformat` (das Framework schaltet sign-compare/format ab; dabei einen echten Vorzeichenvergleich und einen unbenutzten Parameter gefunden und behoben). `-Wunused-parameter` würde in Framework-Headern warnen und wird stattdessen in den nativen Tests (`-Wall -Wextra`) geprüft. Stand: keine Warnungen. `pio check`/clang-format nicht eingeführt (Werkzeug-Erweiterung).

---

## B.5 Tests, Build, CI

- [x] **P1 – 5.1 Die Tests testen nicht den Produktionscode**
  [test/test_duty/test_duty.cpp:13-47](../test/test_duty/test_duty.cpp#L13-L47), [test/test_morse/test_morse.cpp:12-60](../test/test_morse/test_morse.cpp#L12-L60)
  Beide Testdateien enthalten eine *Kopie* der Morse-Tabelle bzw. der Duty-Formeln. Fehler im echten Code (z. B. 1.1, 2.2, 2.3, 3.1) bleiben so unentdeckt.
  **Fix:** Eine `[env:native]` in `platformio.ini` (`platform = native`, `test_build_src = yes`, `build_src_filter` auf die Arduino-freien Module), die Tests gegen die echten Header/Quellen.
  **Erledigt:** Neue Envs `native` (GCC im PATH) und `native_win` (MinGW über PlatformIO) mit `test_build_src = yes` und einem `build_src_filter` auf die hardwareunabhängigen Module. Beide Testdateien binden jetzt die echten Header/Quellen ein, die Kopien der Tabellen und Formeln sind entfernt. Aufruf: `pio test -e native_win` (Windows) bzw. `pio test -e native`. Die Firmware-Env ignoriert Tests (`test_ignore`).
- [x] **P1 – 5.2 Fehlende Tests für die kritischen Pfade**
  - Config-Hash: zwei Füchse mit unterschiedlicher ID, sonst gleich → kompatibel (deckt 1.1 auf)
  - Slotplan/Sendezeitpunkt über viele Zyklen ohne Akkumulation (1.2/1.3)
  - Gleicher effektiver Zyklus auf allen Füchsen (1.5)
  - Duty-Bilanz bei Profilwechsel (2.1), Callsign-Slot-Länge ≤ Slot (2.4)
  - CRC32 gegen bekannte Testvektoren (`"123456789"` → `0xCBF43926`) (3.1)
  - Regression mit synthetischer Drift (+/− ppm, Ausreißer), Vorzeichen (1.10)
  **Erledigt:** Alle genannten Testfälle sind vorhanden, inklusive CRC-Testvektor (`test_crc32_check_value`). Insgesamt 67 native Tests.
- [x] **P2 – 5.3 Build-Umgebungen**
  - Ein eigenes `[env:esp32c3_supermini]` (README nennt den Super Mini als Hauptziel, gebaut wird nur `lolin_c3_mini`).
  - `platform = espressif32 @ 6.8.x` exakt pinnen (reproduzierbare Builds).
  - `lib_deps =` ist leer, also entfernen.
  **Erledigt:** Plattform exakt auf `espressif32 @ 6.13.0` gepinnt (Arduino-Core 2.0.17), leeres `lib_deps` entfernt, `default_envs` gesetzt. Eine eigene Super-Mini-Env ist nicht nötig: Die Firmware steuert beide LED-Pins (GPIO7 und GPIO8) parallel und läuft unverändert auf beiden Boards (in der README dokumentiert).
- [-] **P2 – 5.4 CI (GitHub Actions)**: `pio run` für alle Envs plus `pio test -e native` bei jedem Push/PR. Das Firmware-Binary als Release-Artefakt anhängen, damit Nutzer ohne PlatformIO flashen können (z. B. mit ESP Web Tools).
  **Nicht umgesetzt:** Erweiterung (neue CI-Infrastruktur) – laut Auftrag nur Fehlerbehebung. Die nativen Tests (`pio test -e native`) sind CI-tauglich.
- [x] **P3 – 5.5 Lokale Umgebung**: Hier schlug der Flash-Image-Schritt fehl (`ModuleNotFoundError: intelhex` im PlatformIO-esptool), die Quellen kompilieren aber. In der README unter „Build" einen Hinweis zur Fehlerbehebung ergänzen (`pio pkg update` bzw. PlatformIO neu installieren).
  **Erledigt:** README, Abschnitt „Fehlerbehebung": `intelhex` in die PlatformIO-Umgebung installieren. Auf diesem Rechner so behoben.

---

## B.6 Bedienung / Usability

- [ ] **P1 – 6.1 Startablauf ist für den Feldeinsatz unklar und lang**
  Jeder Fuchs braucht ≥ 120 s bis zur ersten Aussendung, ohne sichtbaren Countdown oder Fortschritt. Ob der Sync erfolgreich war (Anzahl gefundener Füchse), sieht man nur mit Laptop.
  **Vorschläge:**
  - Nach Sync-Ende per LED die Anzahl der gefundenen Füchse blinken (z. B. n× kurz) und die eigene Fox-ID (n× lang).
  - Countdown in der seriellen Ausgabe („Start in 45 s, 3 Füchse gefunden").
  - Startphase vorzeitig beenden, sobald alle erwarteten Füchse gefunden und stabil sind.
  **Teilweise erledigt:** Der Sendebeginn ist jetzt fest und vorhersagbar (120 s nach dem Einschalten des ersten Fuchses). Er erscheint im Log beim Einfrieren („Slotplan fest – Sendebeginn in x s") und in Menüpunkt 6 mit Restzeit. Menüpunkt 10 am Master zieht ihn vor (frühestens nach 30 s Messzeit). Kollision und Abweichung zeigt die LED schon während des Syncs. **Nicht umgesetzt (Erweiterungen):** LED-Rückmeldung der gefundenen Füchse/eigenen Fox-ID und vorzeitiges Ende der Startphase, sobald alle erwarteten Füchse da sind.
- [x] **P1 – 6.2 Nach Konfigurationsänderung Neustart anbieten bzw. erzwingen**
  Nach Änderung von Fox-ID, Profil oder Rufzeichen: „Änderung gespeichert – Neustart erforderlich, jetzt neu starten? (J/N)". Mindestens Menüpunkt 10 sperren, bis neu gestartet wurde. Behebt 2.1 und 3.3.
  **Erledigt:** Nach dem Speichern von Fox-ID, Profil, Rufzeichen oder Werkseinstellungen fragt die CLI „Jetzt neu starten? (J/N)". Ohne Neustart ist Menüpunkt 10 gesperrt, und beim Auto-Timeout wird neu gestartet statt mit veraltetem Zustand zu senden.
- [-] **P2 – 6.3 Konfiguration ohne Laptop**
  Derzeit geht das nur per USB-Seriell. Optionen:
  - BOOT-Taste (GPIO9) nach dem Start: kurz drücken = Fox-ID weiterschalten, lang = Profil; Rückmeldung per LED.
  - **Web-Serial-Konfigurationsseite** (statisches HTML, Chrome/Edge): ein Klick auf „Verbinden" und die Konfiguration per Formular. Dafür braucht es ein maschinenlesbares Kommando-Interface (6.5).
  - Konfiguration aller Füchse auf einmal: Ein Fuchs schickt seine Profil/WPM-Einstellung per ESP-NOW an alle, nur die Fox-ID bleibt individuell.
  **Nicht umgesetzt:** Erweiterung (BOOT-Taste, Web-Serial-Seite, Konfiguration per ESP-NOW) – laut Auftrag nur Fehlerbehebung.
- [-] **P2 – 6.4 LED im Sendebetrieb abschaltbar machen**
  Eine im Ton-Takt blinkende LED verrät einen versteckten Fuchs und kostet Strom. Option „LED: aus / nur Startphase / immer".
  **Nicht umgesetzt:** Erweiterung (neue Option) – laut Auftrag nur Fehlerbehebung.
- [-] **P2 – 6.5 Skriptbare Befehle zusätzlich zum Nummernmenü**
  Z. B. `fox 3`, `profil c`, `call DL1ABC`, `status`, `start`, `reboot`, optional `status json`. Erleichtert die Bedienung, automatisierte Tests und eine Web-UI. Die Menünummern 10/11 sind fehleranfällig (Tippfehler „1" + Enter statt „10").
  **Nicht umgesetzt:** Erweiterung (zusätzliche Befehlssyntax) – laut Auftrag nur Fehlerbehebung.
- [x] **P2 – 6.6 Im Fehlerzustand die CLI öffnen**
  Statt SOS-Blinken plus Neustart sollte die CLI in den Fehlerzuständen erreichbar bleiben. Die Meldung „Fox-Nummer über CLI ändern" ist sonst nicht umsetzbar (siehe 3.2).
  **Erledigt:** Im Fehlerzustand startet die CLI im Modus `Error`: Menü, Konfiguration ändern, Neustart (11); Menüpunkt 10 erklärt, warum er gesperrt ist.
- [-] **P2 – 6.7 Testaussendung**
  `docs/cli.md` schließt einen Testmodus ausdrücklich aus. Für den Aufbau im Gelände ist eine einzelne, duty-konforme Testaussendung („T) Kennung einmal senden") aber sehr hilfreich, ebenso eine Option „Peilempfänger-Check".
  **Nicht umgesetzt:** Erweiterung (neuer Testmodus) – laut Auftrag nur Fehlerbehebung.
- [x] **P3 – 6.8 Untermenü-Timeouts** (10 s) sind knapp. Eine Zeitüberschreitung sollte mit „Abgebrochen, Wert unverändert" klar gemeldet werden.
  **Erledigt:** Rückfragen haben 30 s Zeit (`CLI_PROMPT_TIMEOUT_MS`); bei Zeitüberschreitung oder ungültiger Eingabe meldet die CLI „abgebrochen, Wert unverändert".
- [x] **P3 – 6.9 Profil D: Hinweis auf den Reset**
  Wegen des 2,2-s-Deep-Sleep ist die CLI nur nach dem Drücken der RST-Taste erreichbar. In der CLI-Startmeldung und in der README sichtbar erwähnen.
  **Erledigt:** Die CLI-Startmeldung in Profil D weist darauf hin, dass die CLI im Betrieb nur nach Reset (RST) erreichbar ist.
- [x] **P3 – 6.10 Sonderzeichen in der Ausgabe** (`–`, `…`, `µ`, Umlaute)
  Manche Terminals (PuTTY mit Standard-Codepage, Arduino Serial Monitor älterer Versionen) zeigen sie falsch an. Entweder auf ASCII beschränken oder UTF-8 als Voraussetzung dokumentieren.
  **Erledigt:** UTF-8 als Voraussetzung in README und `docs/cli.md` dokumentiert; die CLI ignoriert nicht druckbare Eingabezeichen.

---

## B.7 Dokumentation

- [x] **P2 – 7.1 README: Plattformaussage falsch** ([README.md:325-328](../README.md#L325-L328))
  `espressif32 @ ^6.8.0` liefert Arduino-Core **2.0.17**, nicht ≥ 3.0 (siehe Build-Ausgabe `framework-arduinoespressif32 @ 3.20017…`).
  **Erledigt:** Siehe 4.10.
- [x] **P2 – 7.2 README: Aussage zu Lolin C3 Mini / USB prüfen** ([README.md:228-231](../README.md#L228-L231), `docs/cli.md`)
  Nach meinem Kenntnisstand nutzt auch der Lolin C3 Mini natives USB (kein CH340). Zudem hat der Lolin C3 Mini (je nach Revision) auf GPIO7 ggf. eine RGB-LED (WS2812) statt einer einfachen LED, *prüfen*.
  **Erledigt:** README und `docs/cli.md`: Beide Boards nutzen den nativen USB-Serial/JTAG (kein CH340). Hinweis ergänzt, dass beim Lolin C3 Mini je nach Revision an GPIO7 eine RGB-LED sitzt (am eigenen Board prüfen).
- [x] **P2 – 7.3 README: LED-Tabelle** entspricht nicht der Implementierung (siehe 3.5).
  **Erledigt:** LED-Tabelle entspricht der Implementierung (`led.cpp`), inkl. „nicht im Slotplan" und Duty-Guard-Blockade.
- [x] **P2 – 7.4 Zahlen konsistent machen**
  Die Zyklus- und Duty-Tabellen in README, `docs/duty_cycle.md` (Profil C, 1 Fuchs: 18.500 ms vs. 14.200 ms) und die CLI-Ausgabe weichen voneinander ab (Ursache 2.3). Idealerweise erzeugt ein Skript oder Test die Tabellen aus dem Code.
  **Erledigt:** Alle Tabellen in README und `docs/duty_cycle.md` aus dem Produktionscode erzeugt (Hilfsprogramm gegen `schedule.cpp`/`fox_profile.cpp`): Budgets je Fuchs, Zyklen für 1–5 Füchse, AFU-Beispiel, Profil D.
- [x] **P2 – 7.5 `docs/sync_protocol.md` / `docs/time_sync.md`** an das tatsächliche (bzw. neue) Verhalten anpassen: Config-Hash ohne Fox-ID, kein Re-Sync, RTC-Uhr vs. Quarz, tatsächliche Rolle des Masters.
  **Erledigt:** `docs/sync_protocol.md` und `docs/time_sync.md` neu: Protokoll v3, Hash ohne Fox-ID, Zuhörphase, Master-Wahl, Vererbung des Sendebeginns, Einfrieren, Nachzügler, Queue-Verarbeitung, Zeitmodell gegen den Master, Begründung für den Wach-Betrieb (Quarz vs. RC), Genauigkeit laut Simulation. `docs/cli.md` an die neue CLI angepasst.
- [x] **P3 – 7.6 Fehlende Datei `NOTICE`**: Sie wird in README und Lizenzhinweis referenziert, existiert aber nicht.
  **Erledigt:** Verweise auf `NOTICE` entfernt (README); die Lizenzangaben stehen in `LICENSE` und `AUTHORS.md`.
- [ ] **P3 – 7.7 Tippfehler/Artefakte**
  „Einsätzrn" (`docs/time_sync.md`); doppelte Leerzeichen und Kommas in Namen („Harm  , DK4HAA", „Sebastian , DM8SB") in allen Datei-Headern, README und `AUTHORS.md`; Aufzählung in `AUTHORS.md` mit leeren Einträgen („, CLI", „Tests, ").
  **Teilweise erledigt:** Tippfehler („Einsätzrn") mit der Neufassung von `time_sync.md` entfallen, leere Aufzählungen in `AUTHORS.md` bereinigt. **Offen:** Die Namen „Harm  , DK4HAA" / „Sebastian , DM8SB" in Copyright-Zeilen, README und AUTHORS.md sind unverändert – die Kürzung wirkt beabsichtigt; bitte selbst entscheiden.
- [ ] **P3 – 7.8 Hardware-Doku ergänzen**: Stromaufnahme (Sync, TX, Deep-Sleep), Akkulaufzeit, Antenne/Yagi-Hinweis (Gerber liegt im Repo-Root unter `ant/`), Schaltplan bzw. Verweis auf `tx/`.
  **Teilweise erledigt:** README: Abschnitt „Stromverbrauch" (Wach-Betrieb der Profile A–C vs. Deep-Sleep in Profil D). **Offen:** gemessene Werte, Akkulaufzeit, Antennen-/Schaltplanverweise (Messung/Erweiterung).
- [-] **P3 – 7.9 Root-README** (`../README.md`) ist nur ein Zweizeiler. Sie sollte auf `tx-Software/`, `tx/` (Original-Sketch, Gerber, Gehäuse) und `ant/` verlinken.
  **Nicht umgesetzt:** Erweiterung außerhalb von `tx-Software` – laut Auftrag nur Fehlerbehebung.

---

## B.8 Verbesserungsvorschläge (Design)

### B.8.1 Neues Timing-Konzept für den Verbund (behebt 1.2–1.9, 2.2)
**Status:** Umgesetzt in abgewandelter Form (Entscheidung des Nutzers): Statt Deep-Sleep mit Funk-Resync bleiben die Füchse im ARDF-Betrieb wach und nutzen den Quarz. Umgesetzt sind die Punkte 1 (Zeitmodell nur gegen den Master), 2 (absolute Sendezeitpunkte) und 4 (verbundweiter Plan). Die Punkte 3, 5, 6 und 7 entfallen damit.


1. **Gemeinsame Zeitbasis:** Der Master verteilt im Beacon `master_time_us` und `cycle_start_us` (in Master-Zeit). Slaves rechnen nur gegen den Master: `offset = local − master`.
2. **Absolute Sendezeitpunkte:** `t_tx(k) = cycle_start + k·cycle + slot_idx·slot`. Nach dem Senden wird *nicht* „Zyklus minus Vorlauf" geschlafen, sondern bis `t_tx(k+1) − vorlauf`. Danach wird aktiv (Busy-Wait oder Light-Sleep) bis `t_tx(k+1)` gewartet. So akkumulieren sich weder TX-Dauer noch Bootzeit.
3. **Zeit über den Deep-Sleep hinweg:** `esp_rtc_get_time_us()` bzw. ein im RTC-RAM gespeicherter Bezug. `esp_timer` beginnt nach jedem Aufwachen bei 0.
4. **Verbundweite Parameter:** Zyklus und Slot werden vom Master aus der längsten aktiven Sequenz (bzw. fest MO5) berechnet und im Beacon vorgegeben, alle übernehmen sie.
5. **Periodischer Re-Sync:** Der Master sendet z. B. alle N Zyklen (oder in jedem eigenen Slot) unmittelbar nach seiner Aussendung einen kurzen ESP-NOW-Beacon. Die Slaves wachen dafür ein Fenster von ±(Drift-Budget) auf und horchen. Kosten: wenige ms Funkbetrieb pro Re-Sync, dafür beliebig lange Einsätze ohne Neustart.
6. **Fallback:** Hört ein Slave den Master K-mal nicht, sendet er im zuletzt bekannten Raster weiter (mit wachsendem Schutzabstand) und meldet das per LED.
7. **Kalibrierung des RTC-Oszillators:** Während der Sync-Phase den RC-Takt gegen den Quarz messen (`rtc_clk_cal()` bzw. Messung über zwei kurze Schlafphasen) und den Sleep damit korrigieren. Das wäre der eigentliche Sinn der „Driftkorrektur".

### B.8.2 Duty-Guard als echte Sendezeitbilanz
**Status:** Teilweise – Laufzeit-Guard mit Mindestabstand je Aussendung (strenger als eine 1-h-Bilanz) umgesetzt. Eine Bilanz im RTC-RAM ist dadurch nicht nötig.

Im RTC-RAM einen Ringpuffer der letzten TX-Starts und -Dauern führen (z. B. 1 h). Vor jedem TX die Bilanz prüfen. Bei Überschreitung verschieben statt nur loggen. Der Guard bleibt so unabhängig von Fehlern in der Zeitplanung, als letzte Sicherheitsinstanz.

### B.8.3 Robustere Verbundbildung
**Status:** Absender-MAC aus dem Empfang umgesetzt (Protokoll v3). Gruppen-ID und Verschlüsselung offen (Erweiterung), siehe auch A.2.2.

- Event-/Gruppen-ID (z. B. 16 Bit, in der CLI setzbar) im Beacon, damit Fremdgeräte oder Nachbarveranstaltungen ignoriert werden.
- Optional ESP-NOW-Verschlüsselung (PMK/LMK aus Gruppen-ID abgeleitet) oder zumindest ein HMAC über den Beacon.
- Absender-MAC aus `recv_info->src_addr` statt aus dem Payload nehmen.

### B.8.4 Energie
**Status:** Konfiguration im RTC-RAM umgesetzt (3.7). Strombudget messen siehe A.1; Rest offen.

- Konfiguration im RTC-RAM statt NVS-Lesen pro Zyklus (3.7).
- `Serial.begin()` und `delay` im Wake-Pfad nur bei angeschlossenem USB (`HWCDC`-Verbindungsstatus prüfen).
- Strombudget messen und dokumentieren (Sync-Phase mit WiFi ~80–100 mA über 2–3 min ist relevant).

---

## B.9 Erweiterungsvorschläge (Features)

**Status:** Laut Auftrag nicht umgesetzt (nur Fehlerbehebung), weiterhin offen.

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

## B.10 Reihenfolge der Bugfix-Runde (abgeschlossen)

Schritte 1–4 sind abgearbeitet, Schritt 5 (Erweiterungen) ist laut Auftrag entfallen.

1. **Sofort (Sicherheit):** 2.1 und 2.2 (Duty bei Profilwechsel, Guard steuert das Timing), 6.2 (Neustart nach Änderung), 2.5 (GPIO-Hold im Deep-Sleep).
2. **Verbund lauffähig machen:** 1.1 (Hash), dann das Timing-Konzept 8.1 mit 1.2–1.9 und 1.11. Parallel 5.1/5.2 (native Tests gegen den echten Code) als Absicherung.
3. **Robustheit:** 3.1 (CRC), 3.2 (Fehlerbehandlung), 3.4/3.5 (nicht-blockierende LED/CLI), 3.7 (Config im RTC-RAM), 2.4 (Rufzeichen-Slot).
4. **Bedienung & Doku:** 6.1, 6.3–6.7, Abschnitt 7.
5. **Erweiterungen:** Abschnitt 9 nach Bedarf. Am meisten Nutzen bringen voraussichtlich Akkuüberwachung, Startverzögerung und der Web-Serial-Konfigurator.
