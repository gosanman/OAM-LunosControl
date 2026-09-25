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

| Kanal | Klemme | Adresse | Ch | bei `0` (Soll 0,00 V) | bei `fffe` (Soll 10,00 V) | Abweichung | `Kalibrierfaktor` |
|---|---|---|---|---|---|---|---|
| S1 | J3 | 0x58 | 0 | 0,004 V | 10,025 V | +25 mV (+0,25 %) | **9975** |
| S2 | J4 | 0x58 | 1 | 0,006 V | 10,027 V | +27 mV (+0,27 %) | **9973** |
| S3 | J5 | 0x59 | 0 | 0,003 V | 10,006 V | +6 mV (+0,06 %) | **9994** |
| S4 | J6 | 0x59 | 1 | 0,002 V | 10,052 V | **+52 mV (+0,52 %)** | **9948** |

Datum: 25.09.2026  Messgerät: RIGOL DM858

**Auswertung.** Alle vier Nullpunkte liegen unter 6 mV — das ist Rauschen, kein
Offset. Die Abweichungen sind rein multiplikativ: S1 zeigt bei 5 V +0,24 % (aus P6)
und bei 10 V +0,25 %. Ein reiner Steigungsfehler lässt sich vollständig über den
ETS-Parameter „Kalibrierfaktor" herausrechnen, ein Offset nicht — insofern der
günstige Fall.

**S4 liegt mit +52 mV außerhalb der ±30 mV.** Die drei übrigen Kanäle sind innerhalb.
Der Kalibrierfaktor deckt ±2 % ab (9800…10200), die größte nötige Korrektur sind
0,52 % — es ist also reichlich Luft.

Ohne Korrektur läge der Stillstandspunkt bei 5,017 / 5,020 / 5,006 / **5,028 V**.
Das ist unkritisch: die nächste Stufe des e²60 liegt rund 1 V entfernt, 28 mV sind
davon knapp 3 %. Die Kalibrierung geht also um die Genauigkeit des Volumenstroms,
nicht um die Sicherheit des Stillstands.

> In die ETS eintragen, je Lüfterkanal unter „Kalibrierfaktor" — **nach** der
> Zuordnung von Lüfter zu DAC-Kanal, denn der Faktor hängt am Ausgang und nicht
> am Gerät.

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
| Drei-Bit-Frame `010`, primär | vorhanden | ☐ nicht mitgeschnitten |
| Drei-Bit-Frame `010`, sekundär | unverfälscht | ☐ nicht mitgeschnitten |
| Frame an 0x10, sekundär | unverfälscht | ☐ nicht mitgeschnitten |
| Acht Datenbytes 0x00, sekundär | unverfälscht | ☐ nicht mitgeschnitten |

Datum: 25.09.2026 — **funktional bestanden, ohne Mitschnitt**

Der Logikanalysator war nicht angeschlossen, aber P10 beantwortet dieselbe Frage
von hinten: der `store` ist durch den ADuM1250 gegangen, der EEPROM hat den Wert
übernommen, und nach dem Kaltstart standen 5,013 V an der Klemme. Wäre eines der
Drei-Bit-Frames oder das Frame an 0x10 sekundärseitig verfälscht worden, hätte der
Chip nichts gespeichert.

Damit ist die Architekturfrage entschieden: **der Isolator trägt die Sequenz**, ein
Optokoppler ist nicht nötig, der EEPROM-Einschaltwert bleibt.

> Der Mitschnitt bleibt trotzdem sinnvoll, falls der `store` später einmal
> sporadisch fehlschlägt — dann ist die Frage, ob es an den Flanken liegt, und
> ohne Referenzbild von einem funktionierenden Lauf ist sie schwer zu beantworten.

**Fällt P15 durch**, ist zu entscheiden: Optokoppler statt ADuM1250, Verzicht auf
die galvanische Trennung, oder Verzicht auf den EEPROM-Einschaltwert. Erst danach
weiter.

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

Datum: 25.09.2026 — **bestanden**

Beide Werte decken sich mit dem Steigungsfehler aus P7 (S1 +0,25 %, S2 +0,27 %
ergäben 5,013 / 5,014 V). Der EEPROM gibt also genau das zurück, was hineingelegt
wurde; es ist kein zusätzlicher Fehler durch das Speichern entstanden.

**Das ist die wichtigste Eigenschaft der Platine überhaupt:** Sie steht nach dem
Anlegen von 12 V auf Stillstand, bevor der RP2040 irgendetwas getan hat. Der
sichere Zustand hängt damit nicht mehr allein an der Firmware.

> Geprüft wurde nur U2 (0x58, Kanäle S1/S2). **U3 (0x59, S3/S4) steht noch aus** —
> derselbe `store`, dieselbe Messung. Solange der nicht gelaufen ist, kommen S3 und
> S4 mit unbekanntem EEPROM-Inhalt hoch.

---

## P11 — Wird das Bereichsbit mitgesichert?

Wie P10, aber Register 0x01 nach dem Kaltstart **nicht** setzen.

**Kostet keinen weiteren `store`** — der EEPROM von U2 hält seit P10 bereits 5,00 V.

1. 12 V abschalten. Der DAC hängt über den LDO am 12-V-Zweig; ihm muss wirklich die
   Versorgung genommen werden, nicht nur dem RP2040. USB darf stecken bleiben.
2. Kurz warten, 12 V wieder einschalten.
3. **Nichts eintippen** — kein `range`, kein `raw`, kein `probe`.
4. S1 und S2 messen.

**Gegenprobe bei 2,50 V:** direkt danach `range 58` senden.
Springt die Spannung auf 5,00 V, war der gespeicherte *Wert* richtig und nur der
*Bereich* kam nicht mit. Bleibt sie stehen, war schon der gespeicherte Wert ein
anderer als angenommen. Gemessen wird also der Sprung, nicht der Absolutwert.

| Beobachtung | Bedeutung |
|---|---|
| 5,00 V | Bereichsbit ist mitgesichert — gut |
| 2,50 V | Chip startet im 0–5-V-Modus mit dem Halbausschlag für 10 V |

Gemessen: **5,013 V** (S1, derselbe Durchgang wie P10)   Datum: 25.09.2026 — **bestanden**

P10 wurde ohne jeden I²C-Zugriff gemessen — nur 12 V an, dann messen. Damit ist es
zugleich P11: im EEPROM steht der Halbausschlag `0x8000`, und ein halber Skalenwert
ergibt nur im 0–10-V-Bereich 5 V. Im 0–5-V-Bereich wären es 2,50 V gewesen.

> **Was die Messung nicht unterscheidet:** ob das Bereichsbit tatsächlich aus dem
> EEPROM zurückkommt oder ob 0–10 V die Einschaltvorgabe des Chips ist. Für den
> Betrieb ist beides gleichwertig — nach dem Kaltstart gilt der 0–10-V-Bereich.
> Die Firmware schreibt Register 0x01 beim Start ohnehin; der Schreibvorgang ist
> idempotent und kostet nichts (Referenzdesign 6.1).

Damit ist der offene Punkt **O1** in die gute Richtung entschieden: der
Startup-Pfad braucht keine Sonderbehandlung, und es gibt kein Zeitfenster mit
2,50 V an den Ausgängen.

> **2,50 V sind nicht Stillstand.** Beim e²60 liegt das knapp unter Stufe 2 in
> einer Richtung. Fällt P11 durch, muss der Startup-Pfad Register 0x01 in den
> ersten Millisekunden schreiben, und das verbleibende Risiko gehört in die
> Applikationsbeschreibung. — **Trifft nicht zu, P11 ist bestanden.**

---

## Stand Phase 1

| Messung | Ergebnis | Datum |
|---|---|---|
| P6 Busformat | `FANDRV_DAC_LEFT_ALIGNED` = 1, im Board-Header eingetragen | 25.09.2026 |
| P7 Skalenfehler | reiner Steigungsfehler, Kalibrierfaktoren 9975 / 9973 / 9994 / 9948 | 25.09.2026 |
| P15 Isolator | funktional bestanden (der `store` ist durchgegangen), ohne Mitschnitt | 25.09.2026 |
| P10 EEPROM-Wert | bestanden, 5,013 / 5,004 V nach Kaltstart | 25.09.2026 |
| P11 Bereichsbit | bestanden, 0–10 V gilt nach dem Kaltstart | 25.09.2026 |

**Die Platine steht nach dem Anlegen von 12 V auf Stillstand, bevor die Firmware
etwas getan hat.** Das gilt bisher für U2 (S1, S2).

### Offen

- [ ] **`store` an U3 (0x59)**, damit auch S3 und S4 auf 5,00 V hochkommen. Solange
      das nicht geschehen ist, halten sie einen unbekannten EEPROM-Inhalt — und
      unbekannt heißt an einem bipolaren Kanal möglicherweise Volllast.
      Ablauf: `range 59` → `raw 59 0 8000` → `raw 59 1 8000` → an S3/S4 prüfen,
      dass 5,00 V anliegen → `store 59` → `y` → 12 V aus/ein → nachmessen.
- [ ] Kalibrierfaktoren in der ETS eintragen (erst wenn `fCalibGain` in der Firmware
      ausgewertet wird — Woche 3).
- [ ] Klemmenbeschriftung S3 = J5, S4 = J6 am Bestückungsdruck bestätigen.

---

## Store-Zähler

Jeder `store` ist ein Schreibzyklus des EEPROM. Die zulässige Zahl ist unbekannt.

| Datum | Adresse | Grund |
|---|---|---|
| 25.09.2026 | 0x58 | P10 — Einschaltwert 5,00 V auf S1 und S2 |
| | | |
| | | |

---

## Offene Punkte, die sich hier nebenbei klären lassen

| # | Frage | Beobachtung |
|---|---|---|
| O1 | Sichert der EEPROM auch Register 0x01? | **erledigt** — nach dem Kaltstart gilt 0–10 V (P11). Ob gespeichert oder Einschaltvorgabe, bleibt offen und ist betrieblich gleichwertig. |
| O6 | Lässt sich der gesetzte Wert zurücklesen? | |
| P7-Zusatz | Antwortet der Chip auf einen **Lesezugriff** mit ACK? | in einem Satz zu klären: `probe` eingeben, listet es 0x58 und 0x59? |

Die zweite Zeile ist nicht kosmetisch: `Gp8413Drive::probe()` prüft die Adresse
heute mit einem Lesezugriff. Antwortet der Chip darauf nicht, meldet die Firmware
einen Ausfall, obwohl alles in Ordnung ist — dann muss die Abfrage auf einen
Schreibzugriff ohne Nutzdaten umgestellt werden.
