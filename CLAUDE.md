# CLAUDE.md — OAM-KwlControl / KNXFANDRV

Diese Datei gilt in jeder Sitzung. Lies sie, bevor du Code änderst.

## Was dieses Projekt ist

OpenKNX-Firmware für eine KNX-Lüftersteuerung. Ein RP2040 steuert über einen
galvanisch getrennten I²C-Bus zwei GP8413-DACs, die vier 0–10-V-Stellsignale für
dezentrale LUNOS-Lüfter erzeugen (2 × e²60, 1 × ego mit zwei Motoren). Die Vorlage
liegt in `reference/` (OpenKNX OAM-/OFM-FanControl, Branch cad435/main) und ist
Lesestoff, keine Kopiervorlage. Der Plan steht in `docs/PLAN.md`, die Hardware ist in
`docs/Referenzdesign_Rev4_GP8413.md` beschrieben.

## Sicherheitsinvarianten — gelten immer, ohne Ausnahme

1. **5,00 V ist Stillstand. 0 V ist Volllast.** Das Stellsignal ist bipolar um 5 V.
   Ein DAC-Code 0 ist kein „aus", sondern volle Drehzahl in Richtung A. Der sichere
   Zustand jedes bipolaren Kanals ist der Code für 5,00 V; der eines unipolaren
   Kanals (RA 15-60) ist 0.

2. **Kein Ausgang ohne gemessenes Busformat.** `FANDRV_DAC_LEFT_ALIGNED` steht seit
   dem 2026-09-23 als **Annahme 0** im Board-Header, damit die Firmware ohne Platine
   baut. Die Zahl ist **kein Messwert**. Vor dem ersten Flashen wird sie durch das
   Ergebnis von Messung P6 (Phase 1) ersetzt und mit Datum kommentiert; bis dahin gilt
   sie nicht als bestätigt, und es wird nichts darauf aufgebaut, was von ihrer
   Richtigkeit abhängt. Der Kasten im Board-Header sagt dasselbe und bleibt stehen,
   bis gemessen ist. Ist der Wert falsch, wird aus dem sicheren Zustand 5,00 V die
   Volllast 10,00 V.

3. **Startup schreibt zuerst den Bereich, dann den sicheren Zustand.** Reihenfolge in
   `setup()`: Register 0x01 = 0x11 an beide Chips → 5,00 V auf alle bipolaren Kanäle →
   erst dann Sollwerte. Kein Codepfad darf einen Sollwert vor dem Bereichsregister
   schreiben.

4. **Codes werden geklemmt, nie überlaufen.** Volt → Code ist `round(v × 3276.7)`,
   begrenzt auf 0…0x7FFF, dann erst der Formatschalter. Ein negativer oder zu großer
   Zwischenwert ist ein Bug, kein Grenzfall.

5. **Richtungswechsel nur über die Totzeit.** Zwischen zwei Richtungen liegt immer ein
   Aufenthalt bei 5,00 V von mindestens der parametrierten Totzeit. Beim ego beide
   Motoren gleichzeitig.

6. **Der ego wird immer als Paar geschrieben.** Beide Motoren in einem I²C-Frame
   (Register 0x02, vier Datenbytes, zwei verschiedene Werte). Niemals zwei getrennte
   Schreibvorgänge für Motor 1 und Motor 2.

7. **EEPROM-Speichern nur per Konsole, nur nach `y`.** Die Store-Sequenz (`kwl store`)
   ist kein normales I²C — sie bangt GPIO2/GPIO3 und sendet an die reservierte
   Adresse 0x08. Sie wird nie aus `loop()`, nie aus einem KO, nie aus einem Test und
   nie automatisiert ausgelöst. Sie zählt im Flash mit. Die Schreibzyklen des Chips
   sind unbekannt.

8. **Vor Neustart sicherer Zustand.** `processBeforeRestart()` schreibt 5,00 V auf
   alle bipolaren Kanäle. Eine ETS-Neuprogrammierung darf keinen Vollgas-Moment
   erzeugen.

9. **Sperren schlagen alles, Schutz schlägt Bedienung.** Die wirksame Stufe entsteht
   in genau einer Funktion `effectiveStage()` nach der Rangtabelle in `docs/PLAN.md`
   Phase 4: Sperre → Schutz → Zu-/Abluftanforderung → Gruppe → Handstufe →
   Automatikstufe; darunter die Betriebsart-Ebenen Zwangsobjekt 1…3 → Nacht →
   Zwangsbetriebsart → Betriebsart → Standard. Rücksetz-Semantik nach Arcus §3.14:
   eine Nutzeraktion auf Rang ≥ 5 setzt höhere Ränge bis 5 zurück; Rang 1–4 nie.
   Kein zweiter Codepfad darf eine Stufe an dieser Funktion vorbei ausgeben.

10. **Ausfall wird gemeldet, nicht geraten.** Antwortet ein DAC nicht auf die
   Adressabfrage, geht der Knoten auf Fehlercode, Alarm-Bit und rote LED. Kein
   stilles Weiterrechnen.

## Regeln für die Arbeit

- **Tests zuerst** für reine Logik: `KwlCurve`, `CableComp`, Stufenabbildung,
  Takt-Zustandsmaschine. `pio test -e native` muss grün sein, bevor Hardware-Code
  angefasst wird. Das ist die Schicht, in der ein Fehler einen Lüfter auf Vollgas
  stellt, und die einzige ohne Hardware prüfbare.
- **Ein Commit je Planschritt** — sobald er freigegeben ist. Nachricht nennt Phase
  und Schritt aus `docs/PLAN.md`. Die Granularität regelt diese Zeile, das Ob regelt
  die Freigabe unter „Was du nicht tust".
- **Vor jedem Flash-Vorschlag beantworte:** „Welcher Ausgangszustand liegt an, wenn
  diese Firmware startet, und woher weiß ich das aus dem Code?" Kannst du das nicht
  aus dem Code herleiten, ist der Code nicht fertig.
- **Quellenrangfolge bei Widersprüchen:** gemessen > Herstellerbibliothek
  (`DFRobot_GP8XXX`) > Pintabelle/DC-Tabelle des Datenblatts > Merkmalsliste >
  Applikationsbild. Die Datenblätter der GP8403/GP8413 enthalten acht bekannte
  Widersprüche (Referenzdesign Anhang B). Korrigiere nie eine Messung anhand eines
  Datenblatts.
- **Konzepte aus der Vorlage übernehmen, Code neu schreiben.** Ausnahmen sind klar
  abgegrenzte Stücke (Taupunktformel, Sendebedingung mit Totband), die mit
  Herkunftskommentar übernommen werden. Die Vorlage ist GPL-3.0; dieses Projekt auch.
- **Richtung heißt Zuluft/Abluft, nicht A/B.** Welche Spannungshälfte welche ist,
  legt der Board-Header über `FANDRV_BELOW_5V_IS_SUPPLY` fest (Messung M1). Modulcode
  rechnet in Zuluft/Abluft, nur `KwlCurve` übersetzt in Spannungshälften.
- **WRG ist die Pendelbewegung.** Kurzer Zyklus (70 s) = WRG, langer Zyklus (1 h,
  Sommer) = praktisch ohne WRG, aber weiter wechselnd. Feste Richtung nur auf
  ausdrückliche Betriebsweise-Vorgabe. Beim ego ist WRG in Stufe 1–3 immer aktiv.
- **Stufen-KOs sind DPT 5.100**, nicht 5.010. Führungen (rF, CO₂) laufen über die
  Grenzwert-Treppe `StageLadder`, nicht über einen Prozent-Regler.
- **Prozent ist Eingabeformat, Stufe ist Wahrheit.** Intern rechnet alles in Stufen
  0…4 und Volt. Prozent-KOs werden am Rand in Stufen übersetzt (mit Hysterese) und
  nie weiter nach innen gereicht.
- **Keine Pins, Adressen oder Kennlinien im Modulcode.** Pins und I²C-Adressen leben im
  Board-Header `include/KnxFanDrv_Rev01.h` — auch die OpenKNX-Systempins (KNX-UART,
  PROG, SAVE, LEDs), denn OGM-HardwareConfig wird nicht eingebunden. Kennlinien-
  Vorgaben leben in `KwlCurve` und sind ETS-überschreibbar.
- **Kompiliert wird das Maximum, sichtbar ist die Auswahl.** 8 Räume, 12 Lüfter,
  8 Verbünde sind die Compile-Zeit-Obergrenzen. Die ETS blendet Räume über
  die Kanalaktivität und Lüfter zusätzlich über die Hardwareauswahl `FAN_Hardware` ein.
  Ausgeblendete Kanäle laufen nicht. Beim Start wird `FAN_Hardware` gegen
  `FANDRV_BOARD_CHANNELS` geprüft; bei Abweichung Fehlercode 3 an allen Lüftern und
  5,00 V — niemals stilles Weiterlaufen mit falscher Kanalzahl.
- **Drei Ebenen, nicht eine.** Raum (Sensoren, Betriebsarten, Führungen) → Verbund
  (Stufenregel, Takt) → Lüfter (Antrieb). Ein Lüfter gehört zu genau einem Raum und
  einem Verbund. Raumlogik gehört in `KwlRoom`, Antrieb in `KwlFan`, die Regel
  dazwischen in `KwlGroup`. `KwlFanModule` liest Raumanforderungen in-process,
  nie über den Bus.
- **Netznamen und Bezeichner wie auf der Platine.** S1…S4, CH1_12V, GPIO2/GPIO3,
  U2 = 0x58, U3 = 0x59. Kanal → DAC-Ausgang: S1 = U2.VOUT0, S2 = U2.VOUT1,
  S3 = U3.VOUT0, S4 = U3.VOUT1. **VOUT0 ist Pin 8, VOUT1 ist Pin 7** — nicht
  aufsteigend.

## Was du nicht tust

- **Kein Commit ohne ausdrückliche Freigabe.** Änderungen werden geschrieben, gebaut,
  geprüft und berichtet — committet wird erst, wenn der Mensch es in derselben
  Nachricht verlangt. Das gilt für beide Repositories, für `git add` auf Vorrat und
  für jedes „damit es nicht verloren geht". Kein `git push`, kein Branch, kein Tag,
  kein `git commit --amend`, kein Verwerfen von Änderungen des Menschen.
- Keine Kommandos an die Hardware, die der Mensch nicht angefordert hat.
- Kein `store`, kein Flashen, kein `pio run -t upload` ohne ausdrückliche Aufforderung
  in derselben Nachricht.
- Keine Änderung an `FANDRV_DAC_LEFT_ALIGNED`, es sei denn der Mensch nennt den
  Messwert aus P6 und das Datum.
- Keine Erweiterung des KO-Layouts, ohne den KoOffset-Plan in `docs/PLAN.md`
  fortzuschreiben — die Vorlage zeigt, was passiert, wenn KO 276 auf das nächste
  Modul trifft.

## Build

```
OpenKNXproducer create --Debug -h include/knxprod.h src/Kwl    (Windows, ETS nötig)
pio run -e develop_KnxFanDrv
pio test -e native                                                 (im OFM-Repo)
```

Phase 1 (`test_dac`) baut ohne knxprod und ohne OGM-Common:
`pio run -e test_dac`.
