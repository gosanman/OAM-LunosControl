# Testplan Inbetriebnahme — KNXFANDRV Rev 0.1

Manuelle Prüfung am Bus: Werte auf Objekte senden, an S1/S2 messen, auf der
Konsole nachsehen. Jeder Plan ist für sich abarbeitbar; die Reihenfolge ist aber
so gewählt, dass jeder auf dem vorigen aufbaut.

**Nur S1 und S2 benutzen.** S3 und S4 der Platine 1 sind seit Befund B1 nicht
verwendbar. Lüfter bleiben abgeklemmt — gemessen wird die Spannung an der Klemme.

---

## Vorbereitung

### ETS-Testprojekt

| Wo | Einstellung |
|---|---|
| Allgemein | Platine **Rev 0.1 (4 Kanäle)**, Info-LED = **Lüftung** |
| Kanalauswahl | Raum 1, Raum 2, Lüfter 1, Lüfter 2, Verbund 1 aktiv |
| Lüfter 1 | Typ e²60, Stellkanal **1**, Raum 1, Verbund 1, **Phase 0**, Kalibrierfaktor **9975** |
| Lüfter 2 | Typ e²60, Stellkanal **2**, Raum 2, Verbund 1, **Phase 1**, Kalibrierfaktor **9973** |
| Verbund 1 | intern, Stufenregel Maximum mit Deckel, Totzeit 2 s, Zykluszeiten **70 s** |
| Raum 1, 2 | Standard-Betriebsart Standby, alles andere Vorgabe |

Zykluszeiten für die Taktprüfung ruhig auf **40 s** senken — dann sieht man den
Wechsel, ohne Kaffee holen zu müssen.

### Objekte, die du brauchst

Absolute Nummern. Formel: Raum *n* = 20 + (n−1)·40 + Index, Lüfter *n* = 340 + (n−1)·24 + Index.

| Objekt | Raum 1 | Raum 2 | DPT | Richtung |
|---|---|---|---|---|
| Betriebsart | 20 | 60 | 20.102 | Ein |
| Zwangsbetriebsart | 21 | 61 | 20.102 | Ein |
| Nacht | 22 | 62 | 1.003 | Ein |
| Zwangsobjekt 1 / 2 / 3 | 23 / 24 / 25 | 63 / 64 / 65 | 1.003 | Ein |
| Stufe manuell | 26 | 66 | 5.100 | Ein |
| Stufe manuell % | 27 | 67 | 5.001 | Ein |
| Stufe höher/niedriger | 28 | 68 | 1.007 | Ein |
| Handbetrieb aktiv | 29 | 69 | 1.012 | Ein/Aus |
| Sommer | 30 | 70 | 1.001 | Ein |
| Betriebsweise | 31 | 71 | 5.010 | Ein |
| Abluftanforderung | 32 | 72 | 1.003 | Ein |
| Sperre | 33 | 73 | 1.003 | Ein |
| rF innen | 34 | 74 | 9.007 | Ein |
| T innen | 35 | 75 | 9.001 | Ein |
| rF außen | 36 | 76 | 9.007 | Ein |
| T außen | 37 | 77 | 9.001 | Ein |
| CO₂ | 38 | 78 | 9.008 | Ein |
| Solltemperatur | 40 | 80 | 9.001 | Ein |
| Feuchteführung aktiv | 42 | 82 | 1.003 | Ein |
| CO₂-Führung aktiv | 43 | 83 | 1.003 | Ein |
| **Raumanforderung Stufe** | **45** | **85** | 5.100 | Aus |
| Betriebsart Status | 47 | 87 | 20.102 | Aus |
| Schutzbetrieb aktiv | 50 | 90 | 1.001 | Aus |
| Feuchtevergleich sperrt | 51 | 91 | 1.001 | Aus |
| Zuluftanforderung | 54 | 94 | 1.003 | Aus |
| Intervall aktiv | 55 | 95 | 1.001 | Aus |

| Objekt | Lüfter 1 | Lüfter 2 | DPT | Richtung |
|---|---|---|---|---|
| Freigabe | 340 | 364 | 1.003 | Ein |
| Suspendieren | 341 | 365 | 1.001 | Ein |
| Verbund Stufe / Takt / Lebenszeichen | 342 / 343 / 344 | — | 5.100 / 1.012 / 1.001 | Ein/Aus |
| **Stufe Status** | **346** | **370** | 5.100 | Aus |
| Richtung Status (1 = Zuluft) | 348 | 372 | 1.001 | Aus |
| WRG aktiv | 349 | 373 | 1.001 | Aus |
| **Ausgangsspannung (mV)** | **350** | **374** | 9.020 | Aus |
| Volumenstrom | 351 | 375 | 9.009 | Aus |
| Betriebsstunden | 352 | 376 | 7.007 | Aus |
| Filterwechsel fällig / Restlaufzeit / quittieren | 353 / 354 / 355 | 377 / 378 / 379 | 1.005 / 5.001 / 1.016 | |
| **Störung** / **Fehlercode** | **356 / 357** | **380 / 381** | 1.005 / 5.010 | Aus |

**Diagnose** (Basis): KO **7**, DPT 16.

### Sollspannungen e²60

Die Kennlinie, gegen die du misst. Bei eingetragenem Kalibrierfaktor ±30 mV.

| Stufe | Zuluft | Abluft |
|---|---|---|
| 0 | 5,00 | 5,00 |
| 1 | 4,01 | 6,01 |
| 2 | 3,21 | 6,91 |
| 3 | 1,96 | 8,15 |
| 4 | 0,01 | 10,00 |

Zuluft ist die Hälfte *unter* 5 V — das ist die Annahme `FANDRV_BELOW_5V_IS_SUPPLY`
aus dem Board-Header, bis Messung M1 sie bestätigt. Sind Zu- und Abluft in allen
Tests vertauscht, ist die Annahme falsch, nicht der Test.

### Konsole

`kwl st`, `kwl r1`, `kwl f1`, `kwl grp`, `kwl r`. Bei jedem „warum?" zuerst
`kwl r1` — es zeigt den Rang, aus dem die Stufe kommt.

---

## T1 — Start und sicherer Zustand

**Ziel:** Nach dem Einschalten steht alles auf 5,00 V, ohne dass jemand etwas gesendet hat.

1. 12 V und Bus einschalten, nichts senden.
2. S1 und S2 messen.
3. `kwl st`.

| Erwartung | ☐ |
|---|---|
| S1 = S2 = 5,00 V (± Steigungsfehler: S1 ≈ 5,013 V unkalibriert, ≈ 5,00 V kalibriert) | ☐ |
| `kwl st`: 4 Stellkanäle, Busformat **linksbündig**, Fehlercode 0 | ☐ |
| KO 346 / 370 = 0, KO 357 / 381 = 0 | ☐ |

Danach die ETS auf eine **falsche Platine** stellen (z. B. 8 Kanäle), programmieren:

| Erwartung | ☐ |
|---|---|
| KO 357 = **3**, KO 356 = 1, LED Blinkcode 3 (drei Blitze, Pause) | ☐ |
| S1 = S2 = 5,00 V, nichts läuft | ☐ |

Platine zurückstellen, programmieren, weiter.

---

## T2 — Handstufe und Vorfahrt

**Ziel:** Rang 5 ersetzt die Automatik; die Maximalstufe deckelt sie *nicht*.

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | KO 26 = **2** | S1 → 3,21 V (Zuluft, Stufe 2) oder 6,91 V; KO 346 = 2; `kwl r1`: Rang 5 | ☐ |
| 2 | KO 26 = **0** | S1 → 5,00 V | ☐ |
| 3 | KO 27 = **60 %** | Stufe 3 (Prozent → Stufe mit Hysterese: 60 % liegt über 54) | ☐ |
| 4 | KO 28 = **1** | Stufe 4 (eine höher) | ☐ |
| 5 | KO 28 = **0** | Stufe 3 | ☐ |
| 6 | KO 29 = **0** | Handbetrieb aus → zurück auf Grundstufe 1 (Standby) | ☐ |
| 7 | KO 22 = 1 (Nacht), dann KO 26 = **3** | Stufe **3** trotz Nachtdeckel 1 — Hand wird nicht gedeckelt | ☐ |
| 8 | KO 26 = 0, KO 22 = 0 | zurück | ☐ |

Zeit: Handstufe läuft nach der parametrierten Laufzeit (Vorgabe 60 min) von
selbst ab — für den Test auf 1 min stellen und einmal abwarten.

---

## T3 — Betriebsarten und Rücksetzen

**Ziel:** Die beiden Arcus-Beispiele, wörtlich aus dem Plan.

**Beispiel 1:** Auto → Nacht → Zwangsobjekt 1 → nach Ablauf wieder Nacht.

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | KO 22 = 1 | KO 47 = 3 (Nacht), Stufe 1 | ☐ |
| 2 | KO 23 = 1 | KO 47 = 5 (Stoßlüften), Stufe **4**; `kwl r1`: Ebene 7 | ☐ |
| 3 | Laufzeit abwarten (Zwangsobjekt 1: 30 min, für den Test auf 1 min stellen) | zurück auf Nacht, Stufe 1 | ☐ |

**Beispiel 2:** Auto → Zwangsobjekt 1 → Nacht → Nacht löscht das Zwangsobjekt → nach Rücknahme Auto.

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | KO 22 = 0, KO 23 = 1 | Stoßlüften, Stufe 4 | ☐ |
| 2 | KO 22 = 1 | **Nacht**, Stufe 1 — das Zwangsobjekt ist gelöscht | ☐ |
| 3 | KO 22 = 0 | Standby (Auto), Stufe 1 — nicht zurück auf Stoßlüften | ☐ |

**Betriebsart-KO:** KO 20 = 1 (Komfort) → KO 47 = 1; KO 20 = 7 (Ruhe) → Stufe 0, S1 = 5,00 V; KO 20 = 0 → Standby.

---

## T4 — Verbund, Pendeltakt, Totzeit

**Ziel:** Zwei Lüfter laufen immer gegenläufig und wechseln über 5,00 V.

Beide Räume auf Komfort (KO 20 = 1, KO 60 = 1), Handstufe 2 in Raum 1 (KO 26 = 2).

| Erwartung | ☐ |
|---|---|
| S1 und S2 liegen in **verschiedenen Hälften**: einer bei 3,21 V, der andere bei 6,91 V | ☐ |
| KO 348 und KO 372 sind **verschieden** | ☐ |
| Nach der Zykluszeit: beide für ~2 s auf **5,00 V**, dann getauscht | ☐ |
| `kwl grp`: Verbund 1, Stufe 2, Zykluszeit, „Totzeit" während des Wechsels | ☐ |
| KO 349 = 1 (WRG aktiv) | ☐ |

**Sommer:** KO 30 = 1 → `kwl grp` zeigt 3600 s, KO 349 = 0. Der Wechsel findet weiter statt, nur seltener.

**Stufenregel Nachtdeckel:** Raum 1 Nacht (KO 22 = 1), Raum 2 Hand 3 (KO 66 = 3) → **beide** Lüfter Stufe 1. Raum 1 Standby → beide Stufe 2. Regel „Raum 2 führt" in der ETS → beide Stufe 3.

---

## T5 — Sperre

**Ziel:** Rang 1 gewinnt gegen alles; beide Sperrverhalten.

Handstufe 4 setzen (KO 26 = 4).

| Sperrverhalten (ETS Raum 1) | KO 33 = 1 | Erwartung | ☐ |
|---|---|---|---|
| Stillstand | | S1 = 5,00 V, `kwl r1`: Rang 1, Stufe 0 | ☐ |
| Grundstufe der Betriebsart | | Stufe **1** (Standby-Grundstufe), Rang 1 — nicht 4 | ☐ |

KO 33 = 0 → Handstufe 4 ist **noch da** (Rang 1–4 setzen nichts zurück). KO 26 = 0.

---

## T6 — Grenzwert-Treppen

**Ziel:** Fünf Grenzwerte, vier Stufen, Hysterese.

Raum 1 Komfort. rF-Treppe braucht den Feuchtevergleich (siehe T7) — deshalb zuerst CO₂, das ist unabhängig.

| Schritt | KO 38 (CO₂) | Erwartung KO 45 | ☐ |
|---|---|---|---|
| 1 | 600 | 1 (Grundstufe) | ☐ |
| 2 | 850 | 1 (GW1 = ein für Stufe 1, Grundstufe ist schon 1) | ☐ |
| 3 | 1000 | 2 | ☐ |
| 4 | 1300 | 3 | ☐ |
| 5 | 1700 | 4 | ☐ |
| 6 | 1650 | **4 bleibt** — Stufe 4 geht erst unter GW3 = 1300 aus (Hysterese) | ☐ |
| 7 | 1299 | 3 | ☐ |
| 8 | 999 | 2 | ☐ |
| 9 | 400 | 1 | ☐ |

**Nachtdeckel:** KO 22 = 1, KO 38 = 1700 → KO 45 = **1**. Der Zweck des Projekts.

**Führung abschalten:** KO 43 = 0 → KO 45 fällt auf Grundstufe, egal was CO₂ sagt. KO 43 = 1 → wieder da.

---

## T7 — Feuchtevergleich, der Kellerfall

**Ziel:** Lüften nur, wenn es trocknet. Verglichen wird der Partialdruck.

Raum 1 Komfort, Entfeuchtung an (Vorgabe).

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | KO 35 = 18 °C, KO 34 = 75 %, KO 37 = 28 °C, KO 36 = 60 % | KO 51 = **1** (sperrt) — draußen steht mehr Wasser in der Luft, obwohl es „trockener" wirkt | ☐ |
| 2 | KO 34 = 90 % | KO 45 bleibt Grundstufe — die rF-Treppe läuft gesperrt | ☐ |
| 3 | KO 37 = 12 °C, KO 36 = 80 % | KO 51 = 0 — Nachtluft, jetzt lohnt es; KO 45 = **4** (90 % > GW4) | ☐ |
| 4 | KO 34 = 52 % | KO 45 = 1 (52 liegt zwischen GW1 50 und GW2 55 → Stufe 1) | ☐ |
| 5 | KO 36 löschen geht nicht — stattdessen Überwachungszeit auf 1 min stellen, 1 min warten | KO 51 = **0**, `kwl r1`: „Wert fehlt" — fehlende Werte sind kein „sperrt" (Code 8, nicht 7) | ☐ |

Absolute Feuchte: KO 52/53 (Raum 1: 32+20 = 52, 53) zeigen g/kg; bei 20 °C/50 % und 500 m ≈ 7,7 g/kg.

---

## T8 — Temperaturführung und Schutz

**Ziel:** Vier Fälle, und Frostschutz ohne Sollwert.

Raum 1 Komfort, Temperaturführung an, Abstand 3 K, Sollwert KO 40 = 22.

| Fall | KO 35 (Ti) | KO 37 (Ta) | Erwartung | ☐ |
|---|---|---|---|---|
| Freie Kühlung | 24 | 18 | Stufe 3 (Kühlstufe), `kwl grp` Sommer-Zyklus | ☐ |
| Wärmeerhalt | 20 | 10 | Grundstufe, WRG-Zyklus | ☐ |
| Warmluft nutzen | 18 | 25 | Stufe 2 (Heizstufe) | ☐ |
| Sonst | 22 | 21 | Grundstufe — 1 K Abstand ist unter dem Mindestabstand | ☐ |

**Frostschutz:** KO 35 = 7 → KO 50 = 1, Stufe 0, S1 = 5,00 V. KO 35 = 9,9 → noch Schutz. KO 35 = 10 → Schutz aus.

**Frostschutz ohne Sollwert:** Überwachungszeit 1 min, Sollwert nicht mehr senden, 1 min warten, KO 35 = 5 → KO 50 = **1** trotzdem. (Das war ein Fehler aus dem Review — ein Raum bei 5 °C lüftete weiter, weil der Thermostat schwieg.)

**Hitzeschutz:** KO 35 = 31, KO 37 = 33 → KO 50 = 1. KO 37 = 25 → kein Schutz, sondern freie Kühlung.

---

## T9 — Fehlende Messwerte

**Ziel:** Der Parameter tut in *beiden* Stellungen, was er sagt.

Raum 1 Komfort, CO₂-Führung an, Überwachungszeit 1 min. CO₂ = 1300 senden (Stufe 3), dann **nichts mehr senden**.

| ETS „bei fehlenden Messwerten" | nach 1 min | ☐ |
|---|---|---|
| Grundstufe weiterfahren | KO 45 = 1, KO 357 = **8**, KO 356 = 0 (kein Alarm) | ☐ |
| Stillstand | KO 45 = **0**, S1 = 5,00 V, KO 357 = 8 | ☐ |

Die zweite Zeile war vor dem Review Stufe 1 — „Stillstand" erreichte keinen Stillstand.

**Nur nicht verknüpfte Führungen:** CO₂-Führung in der ETS **aus** → kein Code 8, obwohl kein CO₂ kommt. Einem Raum ohne Führungen fehlt nichts.

---

## T10 — Abluftanforderung und Nachströmung

**Ziel:** Vorlauf, Nachlauf, Zuluftanforderung, und dass der Partner nachströmt.

Raum 1: Stufe bei Abluftanforderung 4, Vorlauf 10 s, Nachlauf 1 min. Verbund 1: „folgt Zuluftanforderung von Raum **1**".

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | KO 32 = 1 | 10 s lang nichts (Vorlauf), `kwl r1`: „Vorlauf" | ☐ |
| 2 | nach 10 s | S1 → **Abluft** Stufe 4 (10,00 V oder 0,01 V), KO 54 = 1; S2 → **Zuluft** — der Verbund folgt der Zuluftanforderung | ☐ |
| 3 | KO 32 = 0 | läuft weiter (Nachlauf), `kwl r1`: „Nachlauf" | ☐ |
| 4 | nach 1 min | zurück auf Grundstufe, KO 54 = 0, Pendeln läuft wieder | ☐ |
| 5 | KO 32 = 1, nach 3 s KO 32 = 0 | **nichts** — im Vorlauf zurückgenommen, kein Nachlauf für eine Lüftung, die nie lief | ☐ |

**Intermittierend:** ETS „1 min je 5 min" → 1 min Abluft, 4 min Grundlüftung (nicht Stufe 0!), wieder 1 min…

---

## T11 — Betriebsweise und Richtungskonflikt

**Ziel:** Feste Richtung ohne Stufenänderung; Konflikt wird gemeldet, nicht versteckt.

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | KO 31 = **2** (Zuluft) | S1 bleibt in der Zuluft-Hälfte, **kein Wechsel** mehr; Stufe unverändert | ☐ |
| 2 | KO 31 = 3 (Abluft) | S1 wechselt über 5,00 V in die Abluft-Hälfte, bleibt dort | ☐ |
| 3 | KO 31 = 0 | Pendeln läuft wieder | ☐ |
| 4 | Raum 1: KO 32 = 1 (Abluft, Stufe 4). Raum 2: KO 72 = 1 (Abluft, aber Stufe 2 parametrieren) | Beide fordern Abluft — geht nicht. Raum 1 gewinnt (höhere Stufe): S1 Abluft, S2 **Zuluft**. KO 381 (Lüfter 2) = **11**, KO 357 = 0 | ☐ |
| 5 | KO 72 = 0 | Konflikt weg, KO 381 = 0 | ☐ |

`kwl grp` zeigt während Schritt 4 „RICHTUNGSKONFLIKT".

---

## T12 — Intervallbetrieb und Nachlauf

**Ziel:** In der Pause steht alles, auch die Grundstufe.

Standby: Intervallbetrieb an, Periode 5 min, Aktivzeit 2 min (Minimum in der ETS beachten — sonst 30/5).

| Erwartung | ☐ |
|---|---|
| Aktivzeit: Grundstufe, KO 55 = 1 | ☐ |
| Pause: S1 = **5,00 V**, KO 45 = 0, KO 55 = 0 — nicht Stufe 1 | ☐ |
| In der Pause KO 26 = 2 senden: Handstufe **schlägt** die Pause | ☐ |

**Nachlauf:** Komfort, Nachlauf 2 min, CO₂ 1300 (Stufe 3), dann CO₂ 400 → Stufe 3 bleibt **2 min** stehen, dann Grundstufe.

---

## T13 — Freigabe, Suspendierung, Fehlercodes

Lüfter 1: „Freigabe über Objekt" an, Suspendieren erlaubt.

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | nach dem Programmieren nichts | S1 = 5,00 V, KO 357 = **1**, KO 356 = 1, LED Blinkcode 1 | ☐ |
| 2 | KO 340 = 1 | Fehler weg, Lüfter läuft | ☐ |
| 3 | 12 V aus/ein | Freigabe **noch da** — sie überlebt den Spannungsausfall (Flash) | ☐ |
| 4 | KO 341 = 1 | S1 = 5,00 V, KO 357 = **6**, KO 356 = 0 (Meldung, kein Alarm) | ☐ |
| 5 | KO 341 = 0 | läuft wieder | ☐ |

**Priorität:** Freigabe weg (1) *und* Suspendiert (6) → KO 357 = **1**. Der kleinste gewinnt.

---

## T14 — Filter und Betriebsstunden

Lüfter 1: Filter „nach Luftmenge", Wechselintervall 1 (× 1000 m³). Bei 60 m³/h (Stufe 4) sind das ~17 Stunden — der Zähler ist also nur im Lauf zu prüfen, das Fälligwerden ist ein Langzeittest.

| Erwartung | ☐ |
|---|---|
| Handstufe 4, eine Stunde laufen lassen: KO 352 = 1, KO 354 fällt von 100 auf ~94 % | ☐ |
| (Vor dem Review kam der Zähler nach Luftmenge **nie** voran.) | |
| KO 355 = 1 (quittieren) → KO 354 = 100 %, KO 353 = 0 | ☐ |
| Nach ~17 h: KO 353 = 1, KO 357 = 10. KO 353 = 0 senden → 24 h Ruhe, dann wieder | ☐ |

---

## T15 — Taste und LED

Info-LED = „Lüftung". Beide Räume Standby.

| Geste | Erwartung | ☐ |
|---|---|---|
| kurz | LED blitzt; KO 47 = 5, KO 87 = 5 (Stoßlüften in beiden Räumen), Stufe 4 | ☐ |
| kurz | wieder aus — Standby | ☐ |
| lang (> 1 s) | KO 47 = 7 (Ruhe), S1 = S2 = 5,00 V | ☐ |
| lang | zurück | ☐ |
| doppelt (< 0,5 s) | nichts, solange kein Filter fällig ist — genau so gewollt | ☐ |

**LED:** Lüfter läuft → an. Stufe 0 → aus. Code 8 (Meldung) → blinkt. Code 1 (Alarm) → Blinkcode 1. Info-LED = „Lüftung, nur Störungen" → im Normalbetrieb **dunkel**.

---

## T16 — Diagnose-KO

Auf KO 7 (DPT 16) senden, Antwort im Gruppenmonitor auf KO 7 lesen.

| Senden | Erwartung (je Zeile ≤ 14 Zeichen) | ☐ |
|---|---|---|
| `kwl st` | `F1 S2 Z 3210` und `F2 S2 A 6910` (Beispiel Stufe 2) | ☐ |
| `kwl f1` | dazu `Kal9975 Fi100%`, `Bh0 Frei` | ☐ |
| `kwl r1` | `R1 S2 Rg6 M1`, `rF52 C850`, `V-- Ti21`, dann Befunde | ☐ |
| `kwl grp` | `G1 S2 Z 70` | ☐ |
| `kwl r` | eine Zeile je Raum | ☐ |

**Zu beobachten:** Kommen bei `kwl st` **alle** Zeilen an, oder wird jede zweite verschluckt? Die Leerzeile nach jeder Listenzeile ist eine Abhilfe aus dem Logikmodul; wenn alles ankommt, kann sie raus.

---

## T17 — Neustart und Neuprogrammierung

**Ziel:** Invariante 8 — kein Vollgas-Moment beim Programmieren.

Handstufe 4 (S1 auf 0,01 oder 10,00 V). Dann in der ETS **programmieren** (Applikation).

| Erwartung | ☐ |
|---|---|
| Während des Programmierens: S1 → **5,00 V**, bevor das Gerät neu startet | ☐ |
| Nach dem Neustart: 5,00 V, dann Sollwerte — Handstufe ist weg (nicht persistent) | ☐ |
| Oszilloskop an S1 beim 12-V-Einschalten: Dauer 0 V → 5,00 V notieren (**P16** im Messprotokoll) | ☐ |

---

## T18 — Master und Slave (ohne zweites Gerät)

Verbund 1 auf **Slave**, Überwachungszeit 30 s. Den Master spielst du mit der ETS auf den Verbund-Objekten von Lüfter 1.

| Schritt | Senden | Erwartung | ☐ |
|---|---|---|---|
| 1 | KO 342 = 3 (Stufe), KO 343 = 0 (Halbwelle), KO 344 = 1 | beide Lüfter Stufe 3, S1 Zuluft, S2 Abluft | ☐ |
| 2 | KO 343 = 1 | Totzeit 2 s bei 5,00 V, dann **getauscht** — der Slave wechselt nicht ohne Totzeit | ☐ |
| 3 | 30 s nichts senden | S1 = S2 = 5,00 V, KO 357 = **2** (Master-Timeout), KO 356 = 1 | ☐ |
| 4 | KO 344 = 1 | läuft wieder, Fehler weg | ☐ |

Rolle auf **Master**: `kwl grp` läuft intern, und auf KO 342/343/344 erscheinen Stufe, Halbwelle und alle 60 s ein Lebenszeichen.

---

## Wenn etwas nicht stimmt

1. `kwl r1` — welcher Rang liefert die Stufe? Steht dort Rang 1 oder 2, ist es keine Führung.
2. `kwl f1` — passt der Stellkanal? Steht „GESPERRT"?
3. Fehlercode am Lüfter (KO 357): die Nummer sagt, wo. 8 = Messwert fehlt, 11 = Richtungskonflikt.
4. Zu- und Abluft **überall** vertauscht → Messung M1, `FANDRV_BELOW_5V_IS_SUPPLY` im Board-Header.
5. Spannung stimmt bis auf ~0,25 % → Kalibrierfaktor nicht eingetragen.
