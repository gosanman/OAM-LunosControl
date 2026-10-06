# Befunde aus der Simulation – Auswertung, Änderungen, Commit-Plan

Stand 2026-10-06. Quelle: drei Wochenläufe des Simulators (`sim/weekrun.py`,
Berichte in `sim/build/reports/`) mit dem unveränderten Firmware-Code, dazu die
Befunde aus Selbsttest und Simulatorbau. Dieses Dokument plant nur. Geändert
wird erst nach Freigabe, Schritt für Schritt nach dem Commit-Plan unten.

## 1. Auswertung

Die Befunde sind nach Wirkung am Gerät geordnet, nicht nach Fundzeitpunkt.

| Nr | Befund | Wirkung am Gerät | Schwere |
|---|---|---|---|
| A | Telegrammsturm beim ego ohne WRG-Takt | Bus geflutet, sobald der ego im Sommerzyklus oder mit fester Richtung läuft | **kritisch** |
| B | Betriebsart (DPT 20.102) wird nicht gelesen und nicht gesendet | Betriebsart über KNX wirkungslos; Räume bleiben auf der Standard-Betriebsart; Status-KO stumm | **hoch** |
| C | Richtungswechsel ohne Totzeit (Invariante 5) an drei Stellen | Motor reversiert ohne Aufenthalt bei 5,00 V | **hoch** (Sicherheitsinvariante) |
| D | ego-Motoren im sicheren Zustand in zwei Frames (Invariante 6) | beide Motoren kurz versetzt | mittel |
| E | Fehlercode 7 auch ohne Feuchteanforderung | verdeckt die Codes 8, 9, 10 und 11 | mittel |
| F | Status in jeder Totzeit zweimal gesendet | ~1.300 Telegramme/h bei zwei Lüftern | mittel |
| G | ETS-Parameter „Zuordnung der Spannungshälften“ je Lüfter wirkungslos | Parameter ohne Funktion | niedrig |
| H | Rang 4 im Arbiter ungenutzt | toter Code, Doku stimmt | niedrig |
| I | „Richtung Status“ invertiert (Nachtrag, gefunden bei F) | Firmware sendet 1 = Abluft, ETS und Doku sagen 1 = Zuluft | mittel |

### A – Telegrammsturm beim ego

`KwlFan::drive()` prüft auf Änderung, bevor es `mHrv` für den ego anpasst:

```cpp
const bool changed = ... || cmd.hrv != mHrv;
mHrv = (mType == FanType::EGO && stage > 0 && stage < kStageMax) ? true : cmd.hrv;
```

Beim ego in Stufe 1–3 ist `mHrv` danach immer `true`. Meldet der Takt
`cmd.hrv = false`, also im Sommerzyklus, bei fester Richtung oder als
eigenständiger Lüfter mit Sommer, gilt der Status in **jedem** Durchlauf als
geändert. Alle sechs Statusobjekte senden dann in jeder Schleife: im Simulator
50.000 Telegramme je Stunde und Objekt, am Gerät ein Vielfaches davon. Im
Winter fällt das nicht auf, weil der Takt dort `hrv = true` meldet.

### B – DPT 20.102

`lib/knx/src/knx/dptconvert.cpp` kennt die Hauptgruppe 20 nicht.

- **Lesen** (`ko.value(DPT_HVACMode)` in `KwlRoom::processInputKo`, Objekte 20/21):
  Die Dekodierung scheitert, `KNXValue("")` wird zu 0 = Auto. Jede Betriebsart
  über KNX setzt den Raum auf die Standard-Betriebsart, eine Zwangsbetriebsart
  wird sofort beendet.
- **Senden** (`KoROOM_ModeAct.value(mode, DPT_HVACMode)` in `sendMode()`,
  Objekt 27): Die Kodierung scheitert, und der Stack sendet **nicht**.
- Folge im Wochenlauf: Komfort kam in 30 von 36 Fällen nicht an. Standby mit
  Deckel 2 bleibt stehen. Die Temperaturführung, die nur in Komfort freigegeben
  ist, kühlt im Sommer nie. Im Schlafzimmer liegt das CO₂ bis 14 h/Woche über
  1500 ppm.
- Am Bus ist 20.102 ein Byte mit dem Wert 0…4. Die Firmware kann dasselbe Byte
  mit `DPT_Value_1_Ucount` (5.010) lesen und schreiben; die ETS behält 20.102.

### C – Invariante 5 an drei Stellen

Der Wächter fand drei Wege an der Totzeit vorbei. Alle drei haben dieselbe
Wurzel: Die Totzeit lebt nur in `KwlGroup` und greift nur, wenn sich die
**Verbundrichtung** ändert. Was die Spannung am Kanal sonst umkehrt, sieht
niemand.

1. **12-V-Wiederkehr:** DAC startet mit 0 V, `tryRecoverOutput()` schreibt
   5,00 V und im selben Durchlauf die alte Richtung. Auch nach 300 ms Ausfall.
2. **Abluftstoß des ego:** Stufe 1–3 pendelt mit Motor 2 in der Gegenhälfte,
   Stufe 4 legt beide Motoren auf 1,11 V. Motor 1 springt von 6,85 V auf
   1,11 V, die Verbundrichtung bleibt „Abluft“.
3. **Kaltstart:** Lag vor dem Start der Werkswert 0 V an, geht der Lüfter nach
   `begin()` ohne Aufenthalt in seine Richtung. Im Wochenlauf nicht gemessen,
   folgt aber aus demselben Mechanismus.

### D – Invariante 6 im sicheren Zustand

`KwlOutput::begin()` und `KwlOutput::safeAll()` schreiben jeden Kanal einzeln.
Beim ego liegen dadurch kurz Motor 1 auf 5,00 V und Motor 2 noch auf seinem alten
Wert. Unkritischer als ein Richtungswechsel, verletzt aber Invariante 6 dem
Wortlaut nach („Niemals zwei getrennte Schreibvorgänge für Motor 1 und
Motor 2“). Nebeneffekt der Korrektur: halb so viele Frames im SAVE-Fenster.

### E – Fehlercode 7 ohne Anforderung

`mDehumBlocked = f.dehum && dehumInputs && !dehumAllowed` meldet „Feuchtevergleich
sperrt“, sobald draußen nicht deutlich trockener ist als drinnen. Das gilt auch
dann, wenn die rF-Treppe gar nichts will. Im Winter steht die 7 so 60–90 % der
Zeit an, auch bei 30 % rF. Weil `lowestError()` den kleinsten Code nimmt, sind in
dieser Zeit 8, 9, 10 und 11 unsichtbar, etwa ein fälliger Filter. Die Sperre
selbst rechnet richtig; falsch ist nur, dass sie ohne Anforderung als Fehlercode
erscheint.

### F – Buslast in der Totzeit

`KwlFan::driveSafe()` setzt in jeder Totzeit `mStage = 0` und sendet den Status;
zwei Sekunden später sendet `drive()` die neue Stufe. `sendStatus()` sendet dabei
immer alle sechs Objekte, auch unveränderte. Pro Pendelwechsel und Lüfter sind
das 12 Telegramme, bei 70 s Zyklus ~600 je Stunde und Lüfter. Nötig wären 3
(Richtung, Spannung, Volumenstrom).

### G – Parameter ohne Wirkung

`FAN_f%C%Direction5V` („Zuordnung der Spannungshälften“) steht in der ETS, die
Firmware liest ihn nirgends; sie nimmt immer `FANDRV_BELOW_5V_IS_SUPPLY` aus dem
Board-Header. Entweder umsetzen (je Lüfter umkehrbar, etwa bei vertauschter
Montage) oder aus der ETS nehmen.

### H – Rang 4 ungenutzt

`StageArbiter::setGroupStage()` wird nirgends aufgerufen; der Slave läuft über
`collectMembers()`. Die Ablaufdiagramme beschreiben das bereits. Kein
Handlungsbedarf außer einem Kommentar oder dem Entfernen.

## 2. Was im Code geändert werden muss

### A – `KwlFan::drive()` (OFM, `src/KwlFan.cpp`)

Erst den wirksamen WRG-Wert bilden, dann vergleichen:

```cpp
const bool hrv = (mType == FanType::EGO && stage > 0 && stage < kStageMax) ? true : cmd.hrv;
const bool changed = !mStatusValid || stage != mStage || cmd.direction != mDirection || hrv != mHrv;
mHrv = hrv;
```

Prüfung: Simulator-Selbsttest bekommt einen Fall „ego, Sommer: höchstens N
Telegramme je Stunde“; Wochenlauf Sommer muss unter 2.000/h bleiben.

### B – Betriebsart als Byte (OFM, `src/KwlRoom.cpp`)

- Lesen: `ko.value(DPT_Value_1_Ucount)` statt `DPT_HVACMode` für Objekte 20 und 21.
  Werte über 7 verwerfen (heute: undefiniert).
- Senden Objekt 27 „Betriebsart Status“ (20.102) im Stil des „erweiterten
  HVAC-Modus“ der LUNOS/Arcus-Steuerung: 1…4 normgerecht, dazu 5 Stoßlüften,
  6 Absenkung, 7 Ruhe – derselbe Wertebereich wie die Eingänge 20/21.
  *(entschieden 2026-10-06)*
- Objekt 28 wird umgewidmet zum **HVAC-Status-Bitfeld** nach MDT-Schema
  (MDT „Visualisierung des HVAC Status“): Bit 0 Komfort, Bit 1 Standby, Bit 2
  Nacht, Bit 3 Frost-/Hitzeschutz, Bit 7 Frostschutz aktiv. Die erweiterten
  Betriebsarten setzen das Bit der nächstliegenden Grundart (Stoßlüften →
  Komfort, Absenkung → Nacht, Ruhe → Schutz). Die Firmware sendet das Byte roh
  (`DPT_Value_1_Ucount`); die KNX-Bibliothek kennt kein HVAC-Status-DPT. In der
  ETS: 1 Byte, DPT bei der Umsetzung klären. Keine Verschiebung von
  Objektnummern; KoOffset-Plan in PLAN.md wird trotzdem fortgeschrieben.
  *(entschieden 2026-10-06)*
- Kommentar am Code: warum nicht `DPT_HVACMode` (Bibliothek kennt 20.x nicht).
- Prüfung: Selbsttest Fall T3 wird grün; Wochenlauf: „Betriebsart Komfort“ ohne
  Wirkung = 0.

### C – Totzeit je Kanal (OFM, neue Klasse + `KwlFan`, `KwlFanModule`)

Die Totzeit zusätzlich **je Kanal** durchsetzen, unabhängig davon, warum die
Spannung die Seite wechselt. Neue reine Klasse `DirectionGuard`
(`src/DirectionGuard.h/.cpp`, ohne Arduino, nativ testbar):

- merkt sich je Motor die letzte Seite von 5 V (unter, bei, über) und seit wann
  der Kanal bei 5,00 V steht;
- verlangt eine Seite, die der letzten widerspricht, liefert sie 5,00 V, bis
  der Kanal die Totzeit lang dort stand;
- `restart(now)` macht die Seite „unbekannt“ und verlangt die volle Totzeit ab
  jetzt (nach 12-V-Wiederkehr und beim Start, weil vorher 0 V anlagen);
- nur für bipolare Kanäle; unipolare laufen durch.

Einbau:

- `DriveCommand` bekommt `deadTimeMs`; `KwlFanModule::driveGroup()` setzt ihn aus
  dem Takt-Platz (`KwlGroup` braucht dafür einen Getter für die Totzeit).
- `KwlFan::drive()` schickt die konditionierten Spannungen durch den Guard, beim
  ego beide Motoren gemeinsam (ein Frame bleibt ein Frame).
- `KwlFanModule::tryRecoverOutput()` und `setup()` rufen `restart()` für alle Lüfter.
- Solange der Guard hält, gilt das wie Totzeit: Status nicht auf 0 setzen (siehe F).

Totzeit auch nach jedem Start: 2 s Stillstand nach dem Einschalten, weil vor
dem Start der Werkswert 0 V anliegen kann. *(entschieden 2026-10-06)*

Prüfung: neue native Tests `test_directionguard` (Seitenwechsel, Halten,
Restart, ego-Paar, unipolar); Wochenläufe: Invariante 5 = 0 Meldungen.

### D – Sicherer Zustand als Paar (OFM, `src/KwlOutput.cpp`)

`begin()` Schritt 3 und `safeAll()` schreiben je Chip mit `writePair()`, wenn
beide Kanäle des Chips existieren; nur ein einzelner Restkanal mit `write()`.
Prüfung: `test_output` um „safeAll nutzt nur WritePair“ erweitern (MockDacDrive
protokolliert die Art). Wochenlauf: Invariante 6 = 0.

### E – Code 7 nur mit Anforderung (OFM, `src/KwlRoom.cpp`)

Fehlercode 7 nur, wenn die Sperre tatsächlich etwas unterdrückt: Feuchteführung
aktiv und `mHumIn.value >= mHumidity.threshold(1)` (Einschaltgrenze Stufe 1).

Das Objekt „Feuchtevergleich sperrt“ (Objekt 31) bleibt der reine Vergleich
(Information für die Visualisierung); nur der Fehlercode bekommt die Bedingung.
Applikationsbeschreibung ergänzen. *(entschieden 2026-10-06)*

Prüfung: Wochenlauf „Fehlercode 7 ohne Feuchteanforderung“ = 0.

### F – Status nur bei echter Änderung (OFM, `src/KwlFan.cpp`, `KwlFanModule.cpp`)

- Totzeit und Guard-Halt sind Teil der Pendelbewegung, kein Stufenwechsel:
  `driveSafe()` bekommt einen Modus „vorübergehend“, der 5,00 V schreibt, aber
  Stufe und Status stehen lässt. Echte Stillstände (Freigabe fehlt, Master-
  Timeout, Fehler) bleiben wie heute: Stufe 0, Status senden.
- `sendStatus()` sendet je Objekt nur bei Änderung (`Sent<T>` wie im Raum), dazu
  das parametrierte Sendezyklus-Intervall.

Status „Stufe“ während der Totzeit = laufende Stufe, nicht 0. Testplan T4
anpassen. *(entschieden 2026-10-06)*

Prüfung: Wochenlauf Buslast < 400/h bei zwei Lüftern.

### G – Spannungshälften je Lüfter (OFM, `src/KwlFan.cpp` oder ETS)

Elektrisch lässt sich nichts vertauschen: das Signal kommt auf einer Ader gegen
Masse. Unterschiedlich sein kann, **welche Luftrichtung** ein Lüfter bei „unter
5 V“ fördert:

- **Einbaulage:** Beim e²60 sitzt der Lüftereinsatz in der Wandhülse; wird er
  andersherum eingeschoben, fördert er bei derselben Spannung in die andere
  Richtung.
- **Fremdgeräte:** Ein „generisch bipolarer“ Lüfter eines anderen Herstellers
  kann die Hälften genau andersherum belegen als LUNOS.

Im Pendelbetrieb fällt das nicht auf, denn der Lüfter pendelt trotzdem. Falsch
werden aber feste Richtungen (Zuluftanforderung, Betriebsweise Zu-/Abluft), die
Statusobjekte „Richtung“ und „Volumenstrom“ und die Gegenläufigkeit im Verbund.
Der Board-Header (`FANDRV_BELOW_5V_IS_SUPPLY`) kann das nur für alle Lüfter
gemeinsam drehen.

**Entscheidung offen:** umsetzen oder entfernen. Umsetzen heißt: `KwlFan::drive()`
übergibt `below5vIsSupply != mDir5vInverted` an `KwlCurve`. Entfernen heißt:
Parameter aus `Fan.templ.xml`, knxprod neu, keine Firmware-Änderung. Vorschlag:
umsetzen, weil ein vertauscht montiertes Gerät sonst nur über den Board-Header zu
retten ist, der für alle Lüfter gilt.

### H – Rang 4 (OFM, `src/StageArbiter.*`)

Vorschlag: nur Kommentar an `setGroupStage()` („derzeit ungenutzt, Slave läuft
über `KwlFanModule::collectMembers()`“). Kein eigener Commit nötig, kann in C
oder F mit.

## 3. Commit-Plan

Regel aus CLAUDE.md: ein Commit je Planschritt, erst nach Freigabe, Nachricht
nennt den Schritt. Gebaut und geprüft wird vor jedem Commit:
`pio test -e native` (OFM), `pio run -e develop_KnxFanDrv` (OAM), bei
ETS-Änderung knxprod, dazu `sim/selftest.py` und – nach C, E, F – die drei
Wochenläufe.

### Stufe 0 – Liegengebliebenes zuerst sichern

In beiden Repositories liegen noch unkommittete Änderungen aus den Runden vom
27.09. und danach. Sie gehören vor die neuen Änderungen, damit jede Änderung
für sich nachvollziehbar bleibt.

| # | Repo | Inhalt | Dateien |
|---|---|---|---|
| 0.1 | OFM | Review 27.09.: Rang 1–3 je Lüfter, Anteil nach Übersteuerung, eigenständige Lüfter, Slave-Schutzmeldung, sicherer Zustand bei ETS-Download/Busausfall/12-V-Wiederkehr, Kommentare | `src/StageArbiter.h`, `src/KwlFanModule.*`, `src/KwlRoom.*`, `test/test_stagearbiter/…` |
| 0.2 | OFM | ETS: ConstOne einmal, Objekte 0/34 bedingt, Hinweis am Klemmenbild | `src/Room.templ.xml`, `src/Room.share.xml`, `src/Fan.share.xml` |
| 0.3 | OFM | Applikationsbeschreibung: Erstinbetriebnahme, Rang 1–3 je Lüfter, eigenständige Lüfter | `doc/Applikationsbeschreibung-Kwl.md` |
| 0.4 | OAM | Review-Doku: Testplan (T4, T10, T13, T17, T18, T19), PLAN, Gruppenadressen | `docs/Testplan_Inbetriebnahme.md`, `docs/PLAN.md`, `docs/ETS_Gruppenadressen_9-6_Inbetriebnahme.xml` |
| 0.5 | OAM | Ablaufdiagramme | `docs/Ablaufdiagramme.md` |
| 0.6 | OAM | Simulator (Kern, App, Umgebung, Bewohner, Wochenlauf) und VS-Code-Tasks | `sim/…`, `.vscode/tasks.json`, `.gitignore` |
| 0.7 | OAM | dieses Dokument | `docs/Befunde_Simulation_2026-10-06.md` |

0.1 fasst Code-Änderungen zusammen, die in `KwlFanModule.cpp` ineinander greifen;
eine Aufteilung ginge nur hunk-weise und brächte keinen prüfbaren
Zwischenstand.

OAM bindet OFM als Symlink ein (`lib/OFM-KwlControl` → `../../OFM-LunosControl`)
und hält den gebauten OFM-Stand in `dependencies.txt` fest (heute `6a829d2`).
Die Datei schreibt der Build; sie geht jeweils mit dem nächsten OAM-Commit nach
einem OFM-Commit mit (0.4 nach Stufe 0, 2.1 nach Stufe 1).

### Stufe 1 – Firmware-Korrekturen, je Befund ein Commit

| # | Repo | Befund | Inhalt | Test |
|---|---|---|---|---|
| 1.1 | OFM | A | `KwlFan::drive()`: WRG erst bilden, dann vergleichen | Selbsttest neu: ego Sommer |
| 1.2 | OFM | B | Betriebsart als Byte lesen/senden (1…7), Objekt 28 → HVAC-Status-Bitfeld, ETS | Selbsttest T3 grün, Bitfeld je Betriebsart |
| 1.3 | OFM | D | `begin()`/`safeAll()` mit `writePair()` | `test_output` erweitert |
| 1.4 | OFM | C | `DirectionGuard` + Einbau in `KwlFan`/`KwlFanModule`, Getter Totzeit | `test_directionguard` neu |
| 1.5 | OFM | F | Totzeit ohne Statuswechsel, Status je Objekt nur bei Änderung | Wochenlauf Buslast |
| 1.6 | OFM | E | Fehlercode 7 nur mit Feuchteanforderung | Wochenlauf |
| 1.7 | OFM | G | Spannungshälften je Lüfter (oder Parameter entfernen) | Selbsttest neu |

Reihenfolge nach Schwere, mit einer Ausnahme: D vor C, weil der Guard beim ego
auf dem Paar-Schreiben aufsetzt. Jeder Commit ergänzt die
Applikationsbeschreibung, wo sich sichtbares Verhalten ändert (B, E, F, G).

### Stufe 2 – Nachziehen

| # | Repo | Inhalt |
|---|---|---|
| 2.1 | OAM | Selbsttest: neue Fälle (ego Sommer, Betriebsart, Totzeit nach Abluftstoß), Erwartungen nach den Korrekturen |
| 2.2 | OAM | Testplan: T3 (Betriebsart am Gerät), T4 (Status in der Totzeit), neuer Fall ego-Abluftstoß, Busmonitor im Sommerzyklus |
| 2.3 | OAM | Wochenläufe wiederholen, Ergebnis an dieses Dokument anhängen |

Kein `git push`, kein Branch, kein Tag ohne ausdrückliche Aufforderung.

## 4. Entscheidungen

| Punkt | Entscheidung (2026-10-06) |
|---|---|
| B, Objekt 27 | LUNOS-Stil: 1…4 normgerecht, 5 Stoßlüften, 6 Absenkung, 7 Ruhe |
| B, HVAC-Status | Objekt 28 wird zum HVAC-Status-Bitfeld nach MDT-Schema |
| C | Totzeit je Kanal, auch nach jedem Start |
| E | nur der Fehlercode bekommt die Bedingung, Objekt 31 bleibt |
| F | Status „Stufe“ in der Totzeit = laufende Stufe |
| G | Parameter aus der ETS entfernen |
| Stufe 0 | freigegeben: liegengebliebene Änderungen in sieben Commits sichern |

Durch die Entscheidung zu Objekt 28 ändert Commit 1.2 auch die ETS
(`Room.templ.xml`, knxprod neu) und in OAM den KoOffset-Plan in PLAN.md.

## 5. Umsetzung und Ergebnis (2026-10-06)

### Commits

| Plan | Repo | Commit | Inhalt |
|---|---|---|---|
| 0.1–0.3 | OFM | `14ac530`, `083b507`, `2055081` | Review 27.09.: Code, ETS, Applikationsbeschreibung |
| 0.4–0.7 | OAM | `8f0e484`, `40a35a2`, `265f60c`, `5f3d1c4` | Review-Doku, Ablaufdiagramme, Simulator, dieses Dokument |
| 1.1 | OFM | `8cc637b` | A – Telegrammsturm beim ego |
| 1.2 | OFM | `010a1bc` | B – Betriebsart als Byte, erweiterter HVAC-Modus, Objekt 28 HVAC-Status |
| 1.3 | OFM | `b7adcb6` | D – sicherer Zustand je Chip als Paar |
| 1.4 | OFM | `c5cb4b4` | C – Totzeit je Kanal (`DirectionGuard`) |
| 1.5 | OFM | `ddacf6b` | F – Status nur bei Änderung, Totzeit ohne Stufe 0 |
| – | OFM | `91277da` | I – Richtung Status 1 = Zuluft |
| 1.6 | OFM | `69f6c47` | E – Fehlercode 7 nur mit Feuchteanforderung |
| 1.7 | OFM | `284c504` | G – Parameter „Zuordnung der Spannungshälften“ entfernt |
| – | OFM | `89c91fb` | H – Rang 4 als ungenutzt kommentiert |

Jeder Befund hat einen Test, der mit dem alten Kern scheitert und mit dem neuen
besteht: native Tests 219 (neu `test_directionguard`, HVAC-Status, Paar-Schreiben),
Selbsttest des Simulators mit den Fällen 6b, 9, 10 und den Prüfungen zu Objekt 47,
48 und 348.

### Wochenläufe vorher und nachher

Gleiche Projekte, gleiche Startwerte, gleiche Störungen.

| | Aufbau B, Winter | ego-Bad, Winter | ego-Bad, Sommer |
|---|---|---|---|
| Befunde vorher | 9 | 15 | 14 |
| Befunde nachher | **0** | **0** | **0** |
| Gesendete Telegramme je Stunde, vorher | 1.276 | 1.908 | 93.558 |
| Gesendete Telegramme je Stunde, nachher | 490 | 724 | 596 |
| Betriebsart Komfort ohne Wirkung, vorher | 12 von 12 | 12 von 18 | 12 von 12 |
| Betriebsart Komfort ohne Wirkung, nachher | 0 | 0 | 0 |
| Meldungen Invariante 5 / 6, vorher | 1 / 0 | 4 / 1 | 3 / 1 |
| Meldungen Invariante 5 / 6, nachher | 0 / 0 | 0 / 0 | 0 / 0 |

Nach allen vier Störungen war das Gerät nach spätestens drei Minuten wieder in
Betrieb. Die Firmware-Fehlerzeile „Ausgabe fehlgeschlagen – Fehlercode 4“ beim
12-V-Ausfall ist die erwartete Meldung und zählt im Wochenlauf nicht mehr als
Befund.

### Beobachtungen ohne Firmware-Fehler

- **Schlafzimmer nachts:** Im Sommer liegt das CO₂ dort rund 11 h je Woche über
  1500 ppm, weil nachts Nacht/Eco mit Maximalstufe 1 gilt. Für zwei Personen in
  40 m³ reicht Stufe 1 nicht. Das ist eine Frage der Parametrierung (Maximalstufe
  Nacht oder CO₂-Führung nachts), keine der Firmware.
- **Code 7 im Sommer:** Er steht jetzt nur noch an, wenn feuchte Sommerluft eine
  tatsächliche Feuchteanforderung blockiert – so gewollt.
- **Raummodell:** ohne Sonneneinstrahlung; die Sommertemperaturen sind Tendenz,
  nicht Messung.
