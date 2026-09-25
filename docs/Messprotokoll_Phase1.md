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

Datum: __________

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

Datum: 25.09.2026

---

## P11 — Wird das Bereichsbit mitgesichert?

Wie P10, aber Register 0x01 nach dem Kaltstart **nicht** setzen.

| Beobachtung | Bedeutung |
|---|---|
| 5,00 V | Bereichsbit ist mitgesichert — gut |
| 2,50 V | Chip startet im 0–5-V-Modus mit dem Halbausschlag für 10 V |

Gemessen: ________ V   Datum: __________

> **2,50 V sind nicht Stillstand.** Beim e²60 liegt das knapp unter Stufe 2 in
> einer Richtung. Fällt P11 durch, muss der Startup-Pfad Register 0x01 in den
> ersten Millisekunden schreiben, und das verbleibende Risiko gehört in die
> Applikationsbeschreibung.

---

## Store-Zähler

Jeder `store` ist ein Schreibzyklus des EEPROM. Die zulässige Zahl ist unbekannt.

| Datum | Adresse | Grund |
|---|---|---|
| | | |
| | | |
| | | |

---

## Offene Punkte, die sich hier nebenbei klären lassen

| # | Frage | Beobachtung |
|---|---|---|
| O6 | Lässt sich der gesetzte Wert zurücklesen? | |
| P7-Zusatz | Antwortet der Chip auf einen **Lesezugriff** mit ACK? | |

Die zweite Zeile ist nicht kosmetisch: `Gp8413Drive::probe()` prüft die Adresse
heute mit einem Lesezugriff. Antwortet der Chip darauf nicht, meldet die Firmware
einen Ausfall, obwohl alles in Ordnung ist — dann muss die Abfrage auf einen
Schreibzugriff ohne Nutzdaten umgestellt werden.
