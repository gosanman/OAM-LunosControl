# Messprotokoll Phase 1 — KNXFANDRV Rev 0.1

Auszufüllen am Tag der Lieferung, in dieser Reihenfolge. Die Firmware dafür ist
`test_dac`; sie enthält kein KNX und keine Kanallogik.

```
pio run -e test_dac -t upload      (erst wenn die Platine am USB hängt)
pio device monitor -b 115200
```

Adressen und Werte werden hexadezimal ohne `0x` eingegeben: `raw 58 0 4000`.

**Vor allem anderen:** Frontpanel J110 abziehen, Lüfter abklemmen. Die Ausgänge
führen nach einem `raw` sofort die eingestellte Spannung, und ein Lüfter an
0,00 V oder 10,00 V läuft mit voller Drehzahl.

---

## Reihenfolge

Sichtprüfung der Bestückung → **P4** → 12 V ohne Bus, **P1–P3** → dann die
Messungen unten. Erst danach die eigentliche Firmware bauen und per ETS
programmieren.

---

## P6 — Busformat des 15-Bit-Werts

**Die wichtigste Messung des Projekts.** Sie entscheidet, ob der sichere Zustand
5,00 V heißt oder 10,00 V, und damit zwischen Stillstand und Vollgas.

```
probe
range 58
raw 58 0 4000        → S1 messen
raw 58 0 8000        → S1 messen
```

Genau einer der beiden Werte ergibt 5,00 V ± 25 mV. Der andere ergibt 10,00 V
oder klemmt am Anschlag.

| Eingabe | Erwartung | Gemessen an S1 | |
|---|---|---|---|
| `raw 58 0 4000` | 5,00 V **oder** 10,00 V | 2,504 V | |
| `raw 58 0 8000` | der jeweils andere Wert | 5,012 V | |

**Ergebnis:** `FANDRV_DAC_LEFT_ALIGNED` = **1**  (`0x8000` → 5,00 V)

Datum: 25.09.2026  Messgerät: RIGOL DM858

Beide Werte liegen auf derselben Geraden: `0x8000` ist die halbe Skala eines
16-Bit-Registers, `0x4000` die viertel. Der logische 15-Bit-Code geht also um ein
Bit nach links, wie es die DFRobot-Bibliothek macht. Die Messung bestätigt sich
damit selbst — ein Ablesefehler hätte nur einen der beiden Punkte getroffen.

Abweichung +12 mV bei 5 V und +4 mV bei 2,5 V, also rund **+0,2 % Steigungsfehler**
und kein nennenswerter Nullpunktfehler. Bei Vollausschlag sind demnach etwa
10,02 V zu erwarten; das ist in P7 zu bestätigen.

> **Eingetragen am 2026-09-25** in `include/KnxFanDrv_Rev01.h`; der Warnkasten ist
> durch die Herkunftszeile ersetzt, CLAUDE.md Invariante 2 ist nachgezogen.
> **Für P7 gilt damit `<voll>` = `fffe`.**

---

## P7 — Skalenfehler je Kanal

Für jeden Kanal den kleinsten und den größten Wert, jeweils an der Klemme messen.
`<voll>` ist `7fff` bei rechtsbündig, `fffe` bei linksbündig — also das Ergebnis
von P6 einsetzen.

| Kanal | Klemme | Adresse | Ch | bei `0` (Soll 0,00 V) | bei `<voll>` (Soll 10,00 V) | Abweichung |
|---|---|---|---|---|---|---|
| S1 | J3 | 0x58 | 0 | 0,004 V | 10,025 V | ________ |
| S2 | J4 | 0x58 | 1 | 0,006 V | 10,027 V | ________ |
| S3 | J5 | 0x59 | 0 | 0,003 V | 10,006 V | ________ |
| S4 | J6 | 0x59 | 1 | 0,002 V | 10,052 V | ________ |

Toleranz ± 30 mV. Größere Abweichungen ergeben den Kalibrierfaktor je Kanal.

> **Gleichzeitig prüfen:** stimmt die Klemmenbeschriftung? Die Zuordnung
> S3 = J5 und S4 = J6 stammt aus dem Referenzdesign und ist am Bestückungsdruck
> noch nicht bestätigt. Kanal 1 liegt links außen.

---

## P15 — Trägt der ADuM1250 die Store-Sequenz?

**Vor P10 und P11**, weil ein Durchfallen die Architektur ändert.

Logikanalysator auf **beide** Seiten des Isolators, dann:

```
range 58
raw 58 0 <5,00 V>
raw 58 1 <5,00 V>
store 58              → Rückfrage mit y bestätigen
```

Die Sequenz sendet Drei-Bit-Frames ohne ACK und Frames an die reservierte
Adresse 0x10. Zu prüfen ist, ob sie sekundärseitig unverfälscht ankommt.

| Prüfpunkt | Erwartung | Beobachtet |
|---|---|---|
| Drei-Bit-Frame `010`, primär | vorhanden | ☐ |
| Drei-Bit-Frame `010`, sekundär | unverfälscht | ☐ |
| Frame an 0x10, sekundär | unverfälscht | ☐ |
| Acht Datenbytes 0x00, sekundär | unverfälscht | ☐ |

Datum: 25.09.2026 — `store 58` ausgeführt, Mitschnitt liegt vor, **Auswertung offen**

**Fällt P15 durch**, ist zu entscheiden: Optokoppler statt ADuM1250, Verzicht auf
die galvanische Trennung, oder Verzicht auf den EEPROM-Einschaltwert. Erst danach
weiter.

> **Die Sequenz ist beim Zielchip angekommen** — P10 an U2 bestanden. Sie hat aber
> zugleich den **Nachbarchip U3 dauerhaft verstellt**, siehe Befund B1 unten. Damit
> ist P15 nicht mehr nur eine Frage des Isolators.

---

## P10 — Hält der EEPROM den Einschaltwert?

```
range 58
raw 58 0 <5,00 V>
raw 58 1 <5,00 V>
store 58   → y
```

Dann **12 V aus, warten, 12 V ein** — und ohne jeden I²C-Zugriff messen. Der
RP2040 bleibt dabei aus oder die Firmware wird nicht gestartet.

| Kanal | Erwartung | Gemessen |
|---|---|---|
| S1 | 5,00 V | 5,013 V |
| S2 | 5,00 V | 5,004 V |

Datum: 25.09.2026

**Bestanden für U2.** Nebenwirkung auf U3: Befund B1.

---

## P11 — Wird das Bereichsbit mitgesichert?

Wie P10, aber Register 0x01 nach dem Kaltstart **nicht** setzen.

| Beobachtung | Bedeutung |
|---|---|
| 5,00 V | Bereichsbit ist mitgesichert — gut |
| 2,50 V | Chip startet im 0–5-V-Modus mit dem Halbausschlag für 10 V |

Gemessen: ________ V   Datum: __________

> **Hinweis aus P10:** Dort wurde nach dem Kaltstart ohne jeden I²C-Zugriff
> gemessen, also auch ohne Register 0x01 — und es lagen 5,013 V an. Das spricht
> für „mitgesichert". Es unterscheidet aber nicht zwischen „mitgesichert" und
> „Werkseinstellung ist ohnehin 0–10 V". Eintragen erst, wenn das geklärt ist.

> **2,50 V sind nicht Stillstand.** Beim e²60 liegt das knapp unter Stufe 2 in
> einer Richtung. Fällt P11 durch, muss der Startup-Pfad Register 0x01 in den
> ersten Millisekunden schreiben, und das verbleibende Risiko gehört in die
> Applikationsbeschreibung.

---

## Store-Zähler

Jeder `store` ist ein Schreibzyklus des EEPROM. Die zulässige Zahl ist unbekannt.

| Datum | Adresse | Grund |
|---|---|---|
| 25.09.2026 | 0x58 | P15 / P10 — Platine 1. Verstellt U3 (0x59), Befund B1 |
| 25.09.2026 | alle (`modetest`) | F3 — Platine 1. Kein Store, nur Speichermodus an/aus; keine Veränderung an U2 oder U3 |
| 25.09.2026 | 0x58 | E1 — Platine 1. U3 vorher auf 0x2000/0xC000; keine Veränderung an U2 oder U3 |
| 25.09.2026 | 0x58 | E2 — Platine 1. Nutzdaten 8 × `ff`; keine Veränderung an U2 oder U3 |
| 25.09.2026 | 0x58 | E5 — Platine 1. U3 `0x06..0x0F` ← `40` vorher; keine Veränderung an U2 oder U3 |

Stand Platine 1: **vier** Stores an 0x58 (P10/P15, E1, E2, E5), ein `modetest`.
| | | |
| | | |

---

## Befund B1 — `store 58` verstellt den Nachbarchip U3 (Platine 1)

Datum: 25.09.2026   Messgerät: RIGOL DM858

### Messwerte

Verstärkung = gemessen / Soll (Soll bei `8000` 5,00 V, bei `fffe` 10,00 V).

| Kanal | Chip | vor dem Store (P7) | nach dem Store | Verstärkung vorher → nachher |
|---|---|---|---|---|
| S1 | U2 0x58 | 0,004 V / 10,025 V | `0`: 0,004 V · `4000`: 2,504 V · `8000`: 5,013 V · `fffe`: 10,025 V | 1,002 → **1,002** |
| S3 | U3 0x59 | 0,003 V / 10,006 V | `8000`: 5,532 V · `fffe`: 11,057 V | 1,001 → **1,106** |
| S4 | U3 0x59 | 0,002 V / 10,052 V | `0`: 0,005 V · `4000`: 2,786 V · `8000`: 5,555 V · `fffe`: 11,106 V | 1,005 → **1,111** |

Beide Kanäle von U3 sind gegenüber ihrem eigenen P7-Wert um denselben Faktor
**1,105** gewachsen. Kein Nullpunktfehler, bis auf wenige mV linear. U2 ist
unverändert.

### Einschaltwerte (Kaltstart, 12 V und BCU aus/ein, ohne I²C gemessen)

| Kanal | ab Werk (vor dem Store) | nach dem Store |
|---|---|---|
| S1, S2 (U2) | — | 5,013 V / 5,004 V (P10) |
| S3, S4 (U3) | 0 V | 0,005 V / 0,004 V |

Der Faktor 1,105 bleibt nach dem Kaltstart bestehen (`range 59`, `raw 59 0 8000`
→ S3 = 5,532 V). **Die Verstellung ist dauerhaft**, sie sitzt im nichtflüchtigen
Speicher von U3.

### Deutung

Die Store-Sequenz ([main_dactest.cpp](../src/main_dactest.cpp), `storeSequence()`)
adressiert den Chip nur in einem ihrer fünf Frames:

| Frame | Inhalt | Empfänger |
|---|---|---|
| 1 | Drei-Bit-Kopf `010` | alle Chips am Bus |
| 2 | `0x10`, `0x03` — Speichermodus an | alle Chips am Bus |
| 3 | Adresse `0xB0` (0x58 « 1), 8 × `0x00` | adressiert: U2 |
| 4, 5 | Kopf, `0x10`, `0x00` — Speichermodus aus | alle Chips am Bus |

U2 hat getan, was ein Store tun soll: den aktuellen Ausgangswert als
Einschaltwert übernommen, Verstärkung unberührt. U3 war über Frame 1 und 2
ebenfalls im Speichermodus und hat Frame 3 — fremde Adresse, acht Nullbytes —
offenbar in einen Bereich geschrieben, den der reguläre Store nicht berührt
(Abgleich oder Referenz).

**Das ist eine Deutung, kein Nachweis.** Belegt ist nur: U3 hat sich verändert,
und zwar genau nach dem Store an 0x58. Die DFRobot-Bibliothek stammt von Modulen
mit einem Chip am Bus; das Verhalten bei zwei Chips ist dort nicht beschrieben.

### Folgen

- **Auf einem Bus mit zwei GP8413 ist ein Store im eingebauten Zustand nicht
  sicher.** Ein `store 59` träfe nach dieser Deutung U2 auf dieselbe Weise.
- U3 auf Platine 1 liefert bei `0x8000` 5,53–5,56 V statt 5,00 V. Das ist **kein
  sicherer Zustand**; an S3/S4 dürfen auf dieser Platine keine Lüfter.
- Der Einschaltwert von U3 ab Werk ist 0 V, also **Volllast** in einer Richtung,
  bis `setup()` den sicheren Zustand schreibt.
- Auf den weiteren Platinen: **kein `store`**, bis B1 geklärt ist. P6 und P7 sind
  ohne Store durchführbar.

### Entscheidung (25.09.2026)

Die Firmware wird **ohne Store** entwickelt. Das verbleibende Risiko wird
hingenommen und dokumentiert: Nach jedem Einschalten liegt an allen Kanälen der
Werkseinschaltwert **0 V = Volllast** in einer Richtung, bis `setup()` den sicheren
Zustand schreibt. Fehlt der KNX-Bus bei anliegenden 12 V, **bleibt es dabei**.
Nachzuziehen: PLAN Phase 5, CLAUDE.md Invariante 7, ETS-Parameter „Einschaltwert
ohne Bus (EEPROM)" samt Hilfetext, Applikationsbeschreibung.

### Fehlersuche an Platine 1

Platine 1 ist das Versuchsobjekt. Reihenfolge: erst ohne Risiko, dann mit.
`test_dac` hat dafür die Befehle `rd`, `rdreg` und `modetest`.

| # | Versuch | Risiko | Ergebnis |
|---|---|---|---|
| F1 | Mitschnitt aus P15 auswerten: Frames primär gegen sekundär, ACK-Takte in Frame 2 und 4, Störimpulse | keins | |
| F2 | Zurücklesen (O6): liefert der Chip etwas, das den Abgleich zeigt? | keins / gering | **nein** — beide Chips liefern nur `11`, 25.09.2026 |
| F3 | Speichermodus an/aus **ohne** Frame 3 | **hoch, beide Chips** | **keine Veränderung** — Verstärkung und Einschaltwert beider Chips gleich, 25.09.2026 |

**F2 — Zurücklesen.** Erst `rd` (schreibt nichts), dann `rdreg` (schreibt nur den
Registerzeiger, ohne Nutzdaten). Jeweils U2 und U3 vergleichen — ein Unterschied
zwischen dem guten und dem verstellten Chip ist der interessante Befund.

```
rd 58 10             rd 59 10
rdreg 58 1 2         rdreg 59 1 2
rdreg 58 2 4         rdreg 59 2 4
rdreg 58 4 2         rdreg 59 4 2
```

| Befehl | 0x58 (U2, gut) | 0x59 (U3, verstellt) |
|---|---|---|
| `rd <a> 10` | 16 × `11` | 16 × `11` |
| `rdreg <a> 1 2` | `11 11` | `11 11` |
| `rdreg <a> 2 4` | `11 11 11 11` | `11 11 11 11` |
| `rdreg <a> 4 2` | `11 11` | `11 11` |

Datum: 25.09.2026

**Ergebnis F2:** Jeder Lesezugriff liefert dasselbe Byte `0x11`, unabhängig vom
Registerzeiger und von der Anzahl. `0x11` ist der Wert des Bereichsregisters
(0–10 V). Kein Echo des zuletzt geschriebenen Bytes — sonst hätte `rdreg … 2`
`02` geliefert. Der gute und der verstellte Chip sind **nicht zu unterscheiden**:
Abgleich und Ausgangswert sind über normales I²C nicht lesbar.

**F3 — nur der Modus.** Frame 1+2, 10 ms, Frame 4+5; Frame 3 fehlt. Klärt, ob
schon das Betreten des Speichermodus einen Chip verstellt, oder ob es der
fremdadressierte Frame 3 war. Vorher und nachher an **allen vier** Kanälen
`8000` und `fffe` messen, dann Kaltstart und noch einmal.

```
range 58   range 59
raw 58 0 8000 … (alle vier Kanäle, dann fffe)   → messen
modetest   → Rückfrage mit 'modus'
range 58   range 59   dieselben Werte           → messen
Kaltstart, Einschaltwerte messen, range, dieselben Werte → messen
```

| Kanal | vorher `8000` / `fffe` | nachher `8000` / `fffe` | nach Kaltstart `8000` / `fffe` | Einschaltwert |
|---|---|---|---|---|
| S1 | 5,013 / 10,025 V | 5,013 / 10,025 V | nicht gemessen | 5,013 V (vorher 5,013 V) |
| S2 | 5,003 / 10,027 V | 5,003 / 10,026 V | nicht gemessen | 5,004 V (vorher 5,004 V) |
| S3 | 5,531 / 11,057 V | 5,531 / 11,057 V | nicht gemessen | 0,005 V (vorher 0,005 V) |
| S4 | 5,555 / 11,107 V | 5,555 / 11,106 V | nicht gemessen | 0,004 V (vorher 0,004 V) |

> **Korrektur 25.09.2026:** Zunächst waren für S3/S4 nach dem Kaltstart 5,272 V
> und 4,689 V notiert. Das war ein Messfehler; richtig sind 0,005 V und 0,004 V,
> bestätigt über fünf Kaltstarts (F4).

Deutung: ändert sich nichts, war es Frame 3 → der Fehler liegt in der
Adressierung. Ändert sich U2 oder U3, reicht schon der Modus.

Datum: 25.09.2026

**Ergebnis F3: keine Veränderung** an beiden Chips — weder Verstärkung noch
Einschaltwert.

- **Der Modus allein verstellt nichts**, weder am gesunden U2 noch weiter am
  verstellten U3. Die Verstärkungsänderung aus B1 hängt damit am **Frame 3**
  (fremde Adresse, 8 × `0x00`) oder an der Kombination mit ihm.
- **Der Modus allein speichert nichts.** Unmittelbar vor `modetest` standen alle
  Ausgänge auf `fffe`. Hätte ein Chip den Moduswechsel als Store gedeutet, läge
  sein Einschaltwert jetzt bei 10 V bzw. 11 V. U2 liegt unverändert bei 5,00 V,
  U3 unverändert bei 0 V.
- **Einschränkung:** eine Beobachtung, eine Platine.

### F4 — Wiederholbarkeit der Einschaltwerte

Kein I²C-Zugriff, nur 12 V und BCU aus/ein, mindestens 10 s stromlos.

| Kaltstart | S1 | S2 | S3 | S4 |
|---|---|---|---|---|
| 1…5 | 5,013 V | 5,004 V | 0,005 V | 0,004 V |

Datum: 25.09.2026

**Ergebnis F4:** stabil über fünf Kaltstarts. Die Einschaltwerte beider Chips
sind seit dem `store 58` unverändert.

### Zusammenfassung B1 (Stand 25.09.2026)

| Sequenz | U2 (gesund) | U3 |
|---|---|---|
| `store 58` (Ziel U2) | Einschaltwert 5,00 V übernommen ✔, Verstärkung unverändert | **Verstärkung +10,5 % dauerhaft**, Einschaltwert 0 V unverändert |
| `modetest` (kein Ziel) | nichts verändert | nichts verändert |

- Die Frames an die reservierte Adresse erreichen **jeden** Chip am Bus — aber
  **Modus an und aus allein richten nichts an**.
- Schaden entsteht erst mit **Frame 3**: Ein Chip im Speichermodus, der den
  Datenframe einer **fremden** Adresse sieht, schreibt ihn offenbar in seinen
  Abgleich statt in seinen Einschaltwert.
- Das ist noch eine Deutung: F3 schließt nur den Modus allein als Ursache aus.
  Den direkten Nachweis — ein Store an 0x59, der dann U2 verstellt — hätte man
  nur mit dem Verlust von U2.
- Jede Zeile beruht auf **einer** Beobachtung an **einer** Platine.
- **Für die Firmware ist die Frage beantwortet:** kein Store auf einem Bus mit
  mehr als einem GP8413.

### Rettungsversuch U3 — Herleitung aus den Quellen (25.09.2026)

Quellen: `DFRobot_GP8XXX.cpp` v1.1.0 (`store()`, `sendByte()`, `recvAck()`),
GP8413-Datenblatt FN1601-79.2a §3.3.6, GP8403-Datenblatt §3.3.7.

**Was Frame 3 wirklich ist.** DFRobot sendet Frame 2 (`0x10`, `0x03`) mit
`sendByte(x, 0)` — erwartetes ACK = 0, der Chip quittiert. Frame 3 sendet sie mit
`sendByte(x, 1)` — **erwartetes ACK = 1**, also NACK, für das Adressbyte und alle
acht Datenbytes. Das Datenblatt zeichnet dieselben Slots als `1`. Nach dem
Entsperren quittiert also kein Chip mehr; Frame 3 ist kein adressierter
I²C-Transfer, sondern ein **roher Bitstrom von 81 Takten, den jeder entsperrte
Chip mitliest**. Die „Adresse" `0xB0` ist für den Chip nur ein Datenbyte, das er
mit seiner eigenen Adresse vergleicht.

**Was wir nicht wissen:** was der Chip bei Nichtübereinstimmung mit dem Rest des
Bitstroms tut. Zwei Hypothesen, die sich mit je einem Versuch trennen lassen:

| | Hypothese | Was in U3 gelandet ist | Was uns einen Hebel gäbe |
|---|---|---|---|
| H-a | die 8 Nutzdatenbytes sind die Daten | 8 × `0x00` → Abgleich = 0 → +10,5 % | Nutzdaten ändern (`storep`) |
| H-c | der Chip kopiert seine **flüchtigen Register** in eine Zeile, die der Adressvergleich auswählt | U3-Registerinhalt zur Store-Zeit (unbekannt: 0 oder `fffe` aus P7?) | U3-Register vor dem Store setzen (`raw`, dann `store 58`) |

Beiden gemeinsam: die Verstärkung beider U3-Kanäle ist um denselben Faktor
gewachsen (×1,1049 / ×1,1055), ohne Nullpunktfehler → **ein globaler Abgleich
(Referenz), nicht zwei Kanalabgleiche.** Es ist also **ein** Wert zu finden.

**Was nicht geht:** den Werkswert zurücklesen. F2 hat gezeigt, dass jeder
Lesezugriff nur `0x11` liefert — auch bei U2. Der Weg ist deshalb *suchen*:
einen Schreibpfad finden, der den Abgleich von U3 beeinflusst, dann den Wert
einstellen, bei dem `fffe` wieder 10,00 V ± 30 mV ergibt.

**Schutz für U2 während der Suche:** U2 sieht in Frame 3 nur dann seine eigene
Adresse, wenn das Adressbyte `0xB0` ist. **Solange U2 am Bus hängt, wird Frame 3
nie mit einem anderen Adressbyte gesendet** — kein `store 59`, kein `storep 59`.
Und vor jedem Store steht U2 auf `range` + `0x8000`/`0x8000`, damit sein
Selbst-Store das speichert, was schon drin ist.

### Versuchsplan (Reihenfolge = steigendes Risiko)

| # | Versuch | EEPROM-Zyklen | Risiko U2 | Ergebnis |
|---|---|---|---|---|
| E0 | F1 nachholen: im P15-Mitschnitt die ACK-Slots von Frame 3 prüfen — bleibt SDA in allen 9 Slots high? | 0 | keins | |
| E3 | flüchtiger Registerscan an 0x59 (`scan 59`): ändert ein verstecktes Register die Verstärkung? | 0 | keins (U2 ignoriert 0x59) | **kein Abgleichregister gefunden**, 25.09.2026 — Auswertung unten |
| E1 | `store 58` exakt wie in B1, aber U3 vorher auf `raw 59 0 2000` / `raw 59 1 c000` | 1 | wie B1, kein neues | **U3 unverändert** — H-c falsch, 25.09.2026 |
| E2 | `e2 ff` — Store an 0x58 mit 8 × `ff` (U3 auf `0x8000`) | 1 | klein: U2-Einschaltwert könnte sich ändern | **U3 und U2 unverändert** — H-a falsch, 25.09.2026 |
| E4 | gezielte Suche nach dem Wert, mit dem Hebel aus E1/E2/E3 | ~5–10 | wie E1/E2 | |

**E3 im Einzelnen.** `range 59`, `raw 59 0 8000`, S3 messen (Referenz 5,53 V).
Dann Registerbereiche mit 16 Bytes auf einmal beschreiben und S3 beobachten:

```
wrn 59 05 ff 10     → S3?      wrn 59 05 00 10     → S3?
wrn 59 15 ff 10     → S3?      wrn 59 15 00 10     → S3?
wrn 59 25 ff 10 …   bis 0xF5
wr 59 00 ff         → S3?      wr 59 03 ff         → S3?
```

Ändert sich S3 anders als durch einen DAC-Wert (Faktor statt Sprung), den
Bereich halbieren, bis das Register gefunden ist. Kaltstart setzt alles zurück.
Register 0x01/0x02/0x04 auslassen, die sind bekannt.

**Ergebnis E3 (25.09.2026, `scan 59`, 37 Schritte, Referenz S3 = 5,532 V):**

| Schritte | Schreibzugriff | S3 |
|---|---|---|
| 2–31 | 16 Bytes ab `0x05`, `0x15` … `0xE5`, je `ff` und `00` | **≈ 0 V** (mV-Bereich) |
| 32, 33 | 11 Bytes ab `0xF5` (`0xF5..0xFF`), `ff` und `00` | 5,532 V — unverändert |
| 34, 35 | Reg `0x00` ← `ff`, `00` | 5,532 V — unverändert |
| 36 | Reg `0x03` ← `ff` | **11,018 V** |
| 37 | Reg `0x03` ← `00` | ≈ 0 V |

Deutung:

- **Reg `0x03` ist das High-Byte von Kanal 0** (`ff` → `0xFF00` → 11,02 V bei
  Faktor 1,105). Damit ist die Registerbelegung `0x02`/`0x03` = Kanal 0 Low/High,
  `0x04`/`0x05` = Kanal 1 Low/High bestätigt.
- **Der Chip wertet nur die unteren vier Bit der Registeradresse aus, und der
  Zeiger läuft modulo 16 um.** Nur so passen die Blöcke zusammen: 16 Bytes ab
  `0x05` (oder `0x15`, … `0xE5`) landen in `0x05..0x0F` **und** `0x00..0x04` —
  also auch in `0x01` (Bereich) und `0x02`/`0x03` (Kanal 0) → Ausgang 0 V, bei
  `ff` wie bei `00` (`0x01` ← `0xFF` ist kein gültiger Bereich). 11 Bytes ab
  `0xF5` bleiben in `0x05..0x0F`, treffen `0x01..0x03` nicht → unverändert.
- **`0x00` und `0x05..0x0F` mit `ff` oder `00` ändern die Verstärkung von
  Kanal 0 nicht.** Entweder gibt es dort keine Register, oder sie sind ohne
  Entsperren nicht beschreibbar, oder sie wirken nicht auf die Verstärkung.
- Kein Schritt ohne ACK gemeldet.

**Folge:** Über normales I²C ist der Abgleich nicht erreichbar. Der Hebel — wenn es
einen gibt — liegt im entsperrten Zustand, also in Frame 3. Weiter mit E1.

Drei freiwillige Kontrollschreibzugriffe, die das Registermodell absichern
(je 0 Zyklen, danach `raw 59 0 8000` als Referenz):

```
wrn 59 06 ff 0a     → erwartet 5,532 V  (0x06..0x0F ohne Wirkung)
wr  59 f2 00 40     → erwartet 2,77 V   (obere Nibble ignoriert: 0xF2 = 0x02)
wrn 59 0e ff 04     → erwartet ≈ 0 V    (Zeiger läuft von 0x0F auf 0x00/0x01 um)
```

**Beobachtung E3a (25.09.2026) — der Ausgang wird instabil.** Die drei Zugriffe
wurden ohne Kaltstart direkt nach Scan-Schritt 37 gesendet. Registerzustand zu
dem Zeitpunkt, aus dem Scan-Ablauf rekonstruiert: `0x00..0x0F` alle `00`,
danach `0x01 = 0x11` (Bereich) und Kanal 0 = `0x0000`. Ergebnis:

| Zugriff | S3 |
|---|---|
| `wrn 59 06 ff 0a` | **instabil: pendelt zwischen 0 V und 12 V**, danach driftende Nachkommastellen |
| `wr 59 f2 00 40` | weiter instabil |
| `wrn 59 0e ff 04` | weiter instabil |
| Kaltstart | 0,005 V, stabil |
| `range 59`, `raw 59 0 8000` | 5,531 V, stabil |
| zehn Einzelzugriffe `wr 59 06..0f ff` bei Kanal 0 = `0x8000` | 5,531 V, stabil |
| Kaltstart, `range`, `raw 59 0 8000`, dann `wrn 59 06 ff 0a` | **5,531 V, stabil** |

12 V ist die Versorgung — mehr, als der 10-V-Bereich je liefert. Der Analogteil
hat also nicht einen anderen Wert ausgegeben, sondern seinen Zustand verlassen.
**Das ist die erste Wirkung eines Schreibzugriffs jenseits von `0x00..0x05`.**
Sie hängt am Vorzustand: derselbe Burst ist nach Kaltstart mit Kanal 0 =
`0x8000` harmlos. Der Vorzustand unterscheidet sich in drei Punkten: Kanal 0 =
`0x0000`, Register `0x00` = `00` (statt Reset-Wert), `0x06..0x0F` = `00` vor dem
Burst (statt Reset-Wert).

**E3b — Vorzustand-Test (`scan2 59`, 13 Schritte, 0 Zyklen).** Jeder Schritt
stellt den Grundzustand her (16 × `00`, dann `range`) und ändert ein Element:

| # | Schritt | S3 stabil? / Wert |
|---|---|---|
| 1 | Referenz: `range` + Kanal 0 `0x8000` | |
| 2 | **Nachstellen:** Grundzustand, Kanal 0 = 0, Burst `0x06..0x0F` ← `ff` | |
| 3 | nur Kanal 0 ← `0x8000` (kein `range`) — wieder stabil? | |
| 4 | nur `range` | |
| 5 | Grundzustand + Kanal 0 `0x8000`, ohne Burst | |
| 6 | dazu Burst `ff` bei Kanal 0 = `0x8000` | |
| 7 | jetzt Kanal 0 ← 0, Burst war schon drin | |
| 8 | Grundzustand, Reg `0x00` ← `ff`, Kanal 0 = 0, Burst `ff` | |
| 9 | Grundzustand, Kanal 1 ← `0x8000`, Kanal 0 = 0, Burst `ff` | |
| 10 | Grundzustand, Kanal 0 = 0, Burst nur `0x06..0x0A` | |
| 11 | Grundzustand, Kanal 0 = 0, Burst nur `0x0B..0x0F` | |
| 12 | Grundzustand, Kanal 0 = 0, Burst `55` statt `ff` | |
| 13 | Aufräumen: Grundzustand + Kanal 0 `0x8000` | 5,53 V stabil |

Datum: 25.09.2026 — drei Durchgänge.

| Durchgang | Firmware | Ergebnis |
|---|---|---|
| 1 | ohne ACK-Ausgabe, ohne Pause zwischen Frames | **alle 13 Schritte 0,005 V** — auch Schritt 1 |
| 2 | dito | nur Schritt 13 auf 5,53 V, Rest 0,005 V |
| 3 | mit ACK-Ausgabe und 5 ms Pause | 1, 3, 4, 5, 6, 13 = 5,53 V stabil; 2, 7–12 = 0,005 V stabil |

Durchgang 3 entspricht **exakt** der Erwartung für einen Chip, der alle Frames
umsetzt: 5,53 V überall dort, wo Kanal 0 auf `0x8000` steht, 0 V überall dort, wo
er auf 0 steht — und **nirgends Instabilität**. Schritt 2 hat den Zustand vor dem
instabilen Ereignis nachgestellt und es **nicht** reproduziert.

Durchgang 1 und 2 sind damit als Messfehler oder als verlorene Frames zu werten
(Frames dicht hintereinander, keine Pause). Dass Schritt 1 in Durchgang 1 auf
0,005 V blieb, ist nur so erklärbar; ob der Chip nach einem 17-Byte-Frame kurz
nicht ansprechbar ist, wäre mit der ACK-Ausgabe zu prüfen (Zeilen `KEIN ACK`).

**Folge:** Das Registerbild (`0x00..0x0F` = 00, Bereich `0x11`, Kanal 0 = 0)
allein löst die Instabilität nicht aus. Was fehlt, ist die **Vorgeschichte** des
ersten Scans: dort waren vorher dreißigmal `0x01` ← `ff` (ungültiger Bereich),
Kanal 1 ← `0xFFFF`, `0x03` ← `ff` (Kanal 0 = `0xFF00`, 11 V) geschrieben worden.
Möglich ist ein Zustand außerhalb der 16 Register, den einer dieser Zugriffe
gesetzt hat und der einen Kaltstart überlebt — oder nicht. Nächster Schritt:
die Vorgeschichte reproduzieren (E3c).

**E3c (25.09.2026): Vorgeschichte reproduziert — Instabilität tritt nicht auf.**
Kaltstart, `scan 59` vollständig (37 Schritte, alle Frames mit ACK, Schritt 36 =
11,02 V, Schritt 37 = 0,005 V), direkt danach ohne Kaltstart:

| Zugriff | S3 |
|---|---|
| `wrn 59 06 ff 0a` | 0,005 V, **stabil** |
| `raw 59 0 8000` | 5,531 V, stabil |
| `range 59` | 5,531 V, stabil |

Das 0-V/12-V-Ereignis aus E3a bleibt ein **Einzelereignis** ohne bekannte
Ursache. Kandidaten außerhalb des Chips: Messleitung, Kontakt an der Klemme,
Störung. Es wird als Beobachtung stehen gelassen, nicht als Befund geführt.

**Abschluss E3:** Über normales I²C erreicht kein Schreibzugriff den Abgleich.
Das Registerfile ist 16 Bytes, der Zeiger läuft modulo 16, `0x00` und
`0x06..0x0F` sind ohne sichtbare Wirkung. Der einzige verbleibende Hebel liegt
im entsperrten Zustand — **weiter mit E1.**

Lesart: Schritt 2 instabil → Ereignis reproduzierbar. Schritt 3 stabil → Kanal 0
allein holt den Chip zurück. Schritt 6 stabil und 7 instabil → Kanal 0 = 0 ist
der Auslöser, Reihenfolge egal. Schritt 8 stabil → Reg `0x00` gehört dazu.
Schritt 10/11 → welche Hälfte der Register. Nach einem Kaltstart mit
`scan2 59 <schritt>` wieder einsteigen.

**E1 im Einzelnen.** Zwei *verschiedene* Kanalwerte auf U3, damit sich zeigt, ob
und welches Byte in den Abgleich wandert:

```
range 58   raw 58 0 8000   raw 58 1 8000     (U2: 5,00 V, gemessen)
range 59   raw 59 0 2000   raw 59 1 c000     (U3: gemessen)
store 58   → y
range 59   raw 59 0 8000 / fffe / 0000       → S3, S4 messen, mit B1 vergleichen
Kaltstart → Einschaltwerte
```

| Kanal | vor dem Store | nach E1 `8000` / `fffe` / `0000` | nach Kaltstart |
|---|---|---|---|
| S1 | 5,013 V (`8000`) | — | 5,013 V |
| S2 | 5,003 V (`8000`) | — | 5,004 V |
| S3 | 1,385 V (`2000`, Soll bei 1,105: 1,381 V) | 5,531 / 11,056 / 0,005 V | 0,005 V |
| S4 | 8,330 V (`c000`, Soll bei 1,110: 8,33 V) | 5,554 / 11,104 / 0,005 V | 0,005 V |

Datum: 25.09.2026 — Store-Zähler: zweiter `store 58` auf Platine 1.

- S3/S4 unverändert → H-c ist falsch, weiter mit E2.
- S3/S4 verändert, beide gleich → ein Registerbyte wirkt global.
- S3/S4 verändert, unterschiedlich → Kanalregister wandern getrennt in den Abgleich.

**Ergebnis E1: U3 unverändert.** Verstärkung 1,106 / 1,110 wie in B1, Einschalt-
wert 0 V wie in B1, U2 wie in B1. Der Registerinhalt von U3 wandert **nicht** in
den Abgleich. Zugleich: Der zweite, byteweise identische Store hat U3 nicht
weiter verändert — was der fremdadressierte Frame in U3 schreibt, ist beim
zweiten Mal dasselbe wie beim ersten. Das passt zu H-a (die Nutzdaten sind die
Daten) oder zu einem festen, datenunabhängigen Schreibvorgang. E2 trennt beides.

> **Nebenbeobachtung Kaltstart:** Nach dem Kaltstart vor E1 antwortete U3 zweimal
> nicht (`range 59` → kein ACK, `probe` fand nur 0x58), erst beim dritten Anlauf
> beide. U2 antwortete sofort. Ob U3 nach dem Einschalten länger braucht als U2 —
> und ob das mit dem verstellten Abgleich zusammenhängt (etwa ein Oszillator-
> Trim in derselben Zeile) — ist offen. Für die Firmware: die Adressabfrage
> beim Start muss wiederholen können, nicht nach dem ersten NACK auf Fehler gehen.
>
> **Zweites Mal (25.09.2026, vor E2):** Nach Kaltstart dreimal `probe` → nur 0x58.
> Dann `range 59` (Schreibzugriff) → **ACK**. U3 quittiert also den Schreibzugriff,
> während der Lesezugriff aus `probe` unbeantwortet bleibt. Ob ein Lesezugriff
> nach einem Schreibzugriff dann wieder geht, ist zu prüfen (`probe` direkt
> danach). Für `Gp8413Drive::probe()` heißt das: **die Anwesenheitsprüfung muss
> als Schreibzugriff ohne Nutzdaten laufen, nicht als Lesezugriff** — genau die
> Alternative, die unter „P7-Zusatz" schon vorgesehen war. Ob das nur den
> verstellten U3 betrifft oder auch gesunde Chips nach dem Einschalten, ist an
> den Platinen 2–5 zu prüfen, bevor es in die Firmware geht.

**E2 im Einzelnen (25.09.2026, `e2 ff`, alle Frames ACK):**

| Kanal | vor dem Store | nach E2 `8000` / `fffe` / `0000` | nach Kaltstart |
|---|---|---|---|
| S1, S2 | 5,00 V | 5,00 / 10,03 / — | 5,00 V |
| S3 | 5,53 V | 5,53 / 11,06 / 0 | 0,005 V |
| S4 | 5,55 V | 5,55 / 11,10 / 0 | 0,004 V |

**Ergebnis E2: nichts verändert.** Der adressierte U2 ignoriert die Nutzdaten
(Einschaltwert bleibt 5,00 V), der nicht adressierte U3 ebenfalls.

**Zwischenstand nach E1 und E2.** Drei byteweise gleiche oder nur in den
Nutzdaten verschiedene Stores an 0x58 haben U3 nach dem ersten Mal nie wieder
verändert. Was der fremdadressierte Frame in U3 schreibt, hängt weder vom
Registerinhalt noch von den Nutzdaten ab. Zwei Erklärungen bleiben:

1. **Der Frame schreibt einen festen Inhalt** (etwa den Schattenspeicher einer
   Zeile, der leer war) — der erste Store hat ihn gesetzt, jeder weitere schreibt
   dasselbe. Dann läge der einzige Hebel in einem Schattenspeicher, der außerhalb
   von `0x02..0x05` liegt und dessen Wirkung erst nach Store **und Kaltstart**
   sichtbar wird — E3 hätte ihn nicht sehen können, weil dort nur die
   flüchtige Wirkung gemessen wurde. Prüfbar mit E5.
2. **Der erste Store war kein sauberer Frame.** Beim ersten Store hing der
   Logikanalysator an beiden Seiten des Isolators (P15) — mit Massebrücke und
   kapazitiver Last auf SDA/SCL. Ein gestörter Bitstrom in einem Chip, der gerade
   entsperrt ist und ohne ACK mitliest, kann etwas geschrieben haben, was der
   saubere Frame nie schreibt. Dann ist der Schaden ein Einzelereignis und mit
   Frames **nicht** reproduzierbar — und auch nicht rückgängig zu machen.

**E5 — Schattenspeicher-Hypothese (1 Zyklus, Risiko U2 wie B1).** U3 vor dem
Store zusätzlich `0x06..0x0F` ← `40` (ein mittlerer Wert, der eine Verstärkungs-
änderung sichtbar machen würde, ohne den Chip stummzuschalten), Store an 0x58
mit `00`, messen, **Kaltstart, messen.** Befehl: `e5 40`.

| Kanal | vor dem Store | nach E5 `8000` / `fffe` / `0000` | nach Kaltstart, dann `range` + `8000` / `fffe` |
|---|---|---|---|
| S1, S2 | 5,00 V | 5,00 / 10,03 / — | 5,00 V |
| S3 | 5,53 V | 5,53 / 11,06 / 0 | 0,005 V, dann wie vorher |
| S4 | 5,55 V | 5,55 / 11,10 / 0 | 0,004 V, dann wie vorher |

Datum: 25.09.2026 — **Ergebnis E5: identisch mit E2, nichts verändert.**

### Abschluss Rettungsversuch (25.09.2026)

Vier Stores an 0x58 nach dem ersten — E1 (andere Registerwerte), E2 (andere
Nutzdaten), E5 (gefüllter Schattenspeicher `0x06..0x0F`) und der ursprüngliche
P10-Store selbst — haben U3 nicht mehr verändert. Alle Größen, die sich mit U2
am Bus gefahrlos variieren lassen, sind variiert. Übrig ist das Adressbyte, und
jedes andere als `0xB0` trifft U2.

**Wahrscheinlichste Erklärung:** Der erste Store (P15) lief mit angeschlossenem
Logikanalysator auf beiden Seiten des Isolators. U3 war entsperrt, hat ohne ACK
mitgelesen, und ein gestörter Bitstrom hat etwas geschrieben, was ein sauberer
Frame nicht schreibt. Das ist nicht bewiesen; die sauberen Wiederholungen
sprechen aber dafür, dass der Schaden nicht aus dem Protokoll selbst kommt.

**Entscheidung:** Rettungsversuch beendet. U3 auf Platine 1 bleibt bei Faktor
1,105 / 1,110. Platine 1 ist Entwicklungsplatine; **an S3/S4 hängt dort nie ein
Lüfter.** Kein weiterer Store auf Platine 1, ausgenommen ausdrücklich für einen
neuen, begründeten Versuch.

**Was aus dem Versuch für die Firmware bleibt:**

1. Kein Store auf einem Bus mit mehr als einem GP8413 (B1).
2. Anwesenheitsprüfung als Schreibzugriff ohne Nutzdaten, nicht als Lesezugriff
   (U3 quittiert nach Kaltstart Lesezugriffe zeitweise nicht).
3. Adressabfrage beim Start mit Wiederholung, nicht Fehler nach dem ersten NACK.
4. Registerfile 16 Bytes, Zeiger modulo 16, obere Nibble der Registeradresse
   ignoriert — Mehrbyte-Schreibzugriffe dürfen nie über `0x05` hinauslaufen,
   sonst landen sie in `0x00..0x04`.
5. Zwischen dicht aufeinanderfolgenden Frames eine kurze Pause (`scan2`
   Durchgang 1/2 gegen 3): bis zur Klärung 5 ms nach jedem Mehrbyte-Frame.
6. Der Chip ist nicht rücklesbar (O6); ein verstellter Abgleich ist im Betrieb
   nicht erkennbar.

Bleibt auch E5 ohne Wirkung, ist mit U2 am Bus **kein Hebel mehr übrig**: Die
einzige noch nicht variierte Größe ist das Adressbyte in Frame 3, und jedes
andere Adressbyte als `0xB0` trifft U2. Dann bleibt nur: U3 auslöten und allein
auf einem Adapter betreiben — oder aufhören.

**Ehrlich zum Ausgang:** Es kann sein, dass keiner der Pfade den Abgleich
erreicht. Dann bleibt U3 auf Platine 1 ein Chip mit Faktor 1,105, der nur mit
Kalibrierfaktor im Modulcode nutzbar wäre — und das nur auf der Entwicklungs-
platine, nie mit Lüfter an S3/S4.

## Offene Punkte, die sich hier nebenbei klären lassen

| # | Frage | Beobachtung |
|---|---|---|
| O6 | Lässt sich der gesetzte Wert zurücklesen? | **Nein.** Jeder Lesezugriff liefert nur `0x11` (Bereichsregister), Registerzeiger wird ignoriert — F2, 25.09.2026 |
| P7-Zusatz | Antwortet der Chip auf einen **Lesezugriff** mit ACK? | **Ja**, U2 und U3 — `probe` und F2, 25.09.2026 |

Die zweite Zeile ist nicht kosmetisch: `Gp8413Drive::probe()` prüft die Adresse
heute mit einem Lesezugriff. Antwortet der Chip darauf nicht, meldet die Firmware
einen Ausfall, obwohl alles in Ordnung ist — dann muss die Abfrage auf einen
Schreibzugriff ohne Nutzdaten umgestellt werden.
