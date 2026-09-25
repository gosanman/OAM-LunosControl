# Anforderungen gegen umgesetzte Software

**Bezug:** `Anforderungen_Dezentrale_Lueftersteuerung.md` (erstes Konzeptpapier, 14.08.2026)
**Stand dieses Dokuments:** 16.08.2026
**Zweck:** Zeigen, **wo die Software von den Anforderungen abweicht** — und warum.

Das Anforderungsdokument wurde aus Sicht der Gewerke geschrieben, ohne Kenntnis des KNX-Busses.
Dieses Dokument ist die Gegenüberstellung: was davon unverändert übernommen wurde, was der Bus
anders erzwungen hat, was gestrichen wurde und was fehlte.

Die Kennungen `K-…`, `E-…`, `A-…`, `M-…`, `MA-…` verweisen auf Regeln im Anforderungsdokument.

| Marke | Bedeutung |
|---|---|
| ✔ | unverändert übernommen |
| ◇ | anders umgesetzt, bewusst entschieden |
| ➕ | ergänzt, fehlte in den Anforderungen |
| ✖ | gestrichen |

---

## 1. Bilanz

| | Anzahl | Kern |
|---|---|---|
| ✔ unverändert | 34 Regeln | Die gesamte Regelungslogik: Master/Slave, Phase und Takt, Leistung/Stellgröße/Drehzahl-Trennung, Freigabe-Ruhestromprinzip, Volumenstrom als reine Anzeige |
| ◇ anders umgesetzt | 9 Punkte | Stellgrößen-Kennlinie, `DEAKTIVIERT`, Zeiteinheiten, Sollwertquelle, Diagnose-Objekte |
| ➕ ergänzt | 8 Bereiche | Sendebedingungen, Masterüberwachung, Mittelstellung, Hardwarevarianten, PWM-Frequenz, Logikmodul |
| ✖ gestrichen | 4 Punkte | `KNOTEN_ID`, `GRUPPEN_ID`, `KENNLINIE_STELLGROESSE`, TypeSelect-Kopplung |

**Kurzfassung:** fachlich trägt das Konzeptpapier. Die Abweichungen betreffen fast
ausschließlich die Übersetzung in Bus- und ETS-Mechanik. Offene Abweichungen gibt es keine
mehr.

---

## 2. Gestrichen ✖

| Anforderung | Grund |
|---|---|
| `KNOTEN_ID`, `GRUPPEN_ID` | In KNX ist Adressierung gelöst: die physikalische Adresse identifiziert das Gerät, die gemeinsame Gruppenadresse bildet die Gruppe. Eigene IDs wären ein zweites, paralleles Adressschema, das gegenüber der tatsächlichen Verknüpfung auseinanderdriften kann — und die Firmware könnte den Widerspruch nicht einmal erkennen. |
| `KENNLINIE_STELLGROESSE` | Ersetzt, siehe 3.1. |
| `DEAKTIVIERT` als ein Parameter | Aufgeteilt, siehe 3.2. |
| TypeSelect auf dem Kanalreiter | Der OpenKNX-Kanalauswahl-Beschluss sieht den Kanaltyp zusätzlich auf dem Kanalreiter vor, gekoppelt über `BASE_SyncChannelType`. Umgesetzt ist er **nur** in der Kanalauswahl-Tabelle: zweimal derselbe Umschalter an zwei Stellen war in der Bedienung schlicht verwirrend. |

---

## 3. Anders umgesetzt ◇

### 3.1 Stellgrößen-Kennlinie → Mindest-/Maximalwert + Anlaufpuls

Die Anforderung sah eine Polylinie Leistung → Stellgröße vor (`KENNLINIE_STELLGROESSE`,
K-4). Umgesetzt ist:

```
Stellgröße = 0                                  für Leistung = 0
Stellgröße = MIN + (MAX − MIN) × Leistung/100   für Leistung > 0
```

mit eigenen MIN/MAX je Richtung, plus optionalem Anlaufpuls (Stellgröße und Dauer).

Begründung: die Kennlinie hätte **zwei physikalisch verschiedene Schwellen** in einen Verlauf
gepresst. Das Losbrechen aus dem Stillstand braucht kurzzeitig hohes Drehmoment — das leistet
der Puls. Der stabile Dauerlauf braucht einen Mindestwert — das leistet MIN. Eine Kennlinie
hätte das Anlaufproblem nur maskiert, indem sie den unteren Bereich generell anhebt, und damit
im Dauerlauf unnötig schnell gedreht.

Die Anlaufpuls-Parameter gelten knotenweit, nicht je Richtung: beim Losbrechen liegt noch keine
Strömung an, die aerodynamische Last ist praktisch null. Übrig bleibt die Haftreibung, und die
ist eine Eigenschaft des Motors, nicht der Förderrichtung.

Kosten: 5 Byte statt rund 20–24 für zwei Polylinien. Rückholbar — Parameter dürfen angehängt
werden, nur Umnummerieren und Entfernen ist verboten.

### 3.2 `DEAKTIVIERT` → Kanalaktivität + Suspendiert

Der Parameter deckte zwei Fälle ab, die OpenKNX trennt:

| Fall in den Anforderungen | Umsetzung | Wirkung |
|---|---|---|
| „nicht bestückter Kanal" | **Kanalaktivität** (Deaktiviert / Aktiviert) in der Kanalauswahl | kein Kanalreiter, keine KOs; die Beschreibung bleibt editierbar |
| „Service, Fehlersuche" (M-3…M-5) | **Suspendiert**: ETS-Parameter als Anfangswert, KO als Laufzeit-Übersteuerung, im Flash gehalten | KOs und Verknüpfungen bleiben, die Funktion ruht |

Das ist die Variante „Nur Aktiv/Inaktiv" des Kanalauswahl-Beschlusses (09.07.2026): ein
1-Bit-Parameter je Kanal, Werte „Deaktiviert"/„Aktiviert", **ohne** `UIHint` — die ETS stellt
einen 1-Bit-TypeRestriction von selbst als Radio-Button dar, und `UIHint="RadioButton"` ist im
KNX-Schema ungültig. Die Sichtbarkeit des Kanalreiters hängt an `choose test="=1"` auf genau
diesem Parameter. Spaltenbreiten 15/35/50 sind für diese Variante feste Regel, weil der
Radio-Select unter 35 % umbricht.

Zwischenzeitlich stand hier ein geräteweiter Zähler „Anzahl Lüfter" mit
`choose test="&gt;=%C%"`. Das ist **das alte Muster, das der Beschluss ausdrücklich abgeschafft
hat** (`VisibleChannels` und `PT-XxxNumChannels` entfallen vollständig) — zurückgebaut.

Der Modus (reversibel / nicht reversibel) steht auf dem Lüfter-Reiter, nicht in der Tabelle.
Der Beschluss erlaubt das: den Kanaltyp auf dem Kanalreiter führt er ausdrücklich als optional,
und das Aktiv/Inaktiv-Feld darf dort gerade **nicht** noch einmal erscheinen.

Dass der zweite Fall gemeint war, steht im Anforderungsdokument selbst: M-4 verlangt, der
stillgelegte Knoten bleibe „über die Schnittstelle lesbar und parametrierbar". Das trifft nur
auf *Suspendiert* zu.

Folge: der Ausgang `IST_DEAKTIVIERT` ist in dieser Form nicht umsetzbar — ein deaktivierter
Kanal hat keine KOs und kann nichts melden. Umbenannt in **`Ist suspendiert`**.

### 3.3 Volumenstrom-Kennlinie: Richtung B optional

K-7 fordert eigene Punkte je Richtung, ausdrücklich **nicht gespiegelt**. Umgesetzt ist:
Kennlinie A ist Pflicht, Kennlinie B optional — bleibt ihr Endpunkt 0, gilt A für beide
Richtungen, mit dem Vorzeichen der gefahrenen Richtung.

Grund: Reversierlüfter fördern in beide Richtungen praktisch gleich viel; beim ebm-papst
AxiRev 126 liegen die Kennlinien übereinander. Ohne Rückfall lieferte Richtung B bei
symmetrischer Konfiguration 0 statt des negativen Wertes — die Pflicht zur Doppeleingabe hätte
also vor allem Fehlerquellen geschaffen. Wer zwei Kennlinien pflegt, bekommt zwei.

### 3.4 Sollwertquelle des Masters: aus „darf" wird eine Auswahl

MA-3 erlaubt dem Master, `LEISTUNG_SOLL` aus einer Bedarfsgröße zu bilden. Umgesetzt ist das
als explizite Auswahl aus drei sich ausschließenden Quellen:

| Quelle | Wirkung |
|---|---|
| Fester Wert | eingestellte Leistung, ohne Kommunikationsobjekt |
| Externes Kommunikationsobjekt | Vorgabe von außen (Vorgabe) |
| Interne Regelung | P-Regler auf CO₂ oder relative Feuchte |

```
Leistung = Grundlast + (Istwert − Sollwert) / Proportionalband × (Maximalleistung − Grundlast)
```

Bewusst ohne I-Anteil: Lüftung ist träge, ein Integrator bringt Überschwingen. Unterhalb des
Sollwerts und solange kein Istwert empfangen wurde, gilt die Grundlast.

Für den Slave bleibt M-1 unberührt — er sieht nur einen Leistungswert und kann die Quelle nicht
unterscheiden.

### 3.5 Richtungsart: Eingang wird zum Parameter mit vierter Option

In den Anforderungen ist `RICHTUNGSART` ein **Eingang** mit drei Werten. Umgesetzt ist ein
**ETS-Parameter des Masters** mit vier Werten: Automatisch reversieren (Vorgabe), Nur A, Nur B,
Über Kommunikationsobjekt. Nur bei der letzten Option existiert das KO.

Grund: in der Praxis ist die Richtungsart eine Anlageneigenschaft, keine Laufzeitgröße. Bei
fester Vorgabe sendet der Master sie zyklisch mit, damit auch ein später hinzukommender Knoten
sie erhält.

### 3.6 Zeiten: Einheiten an die Größenordnung angepasst

| Parameter | Anforderung | Umgesetzt | Grund |
|---|---|---|---|
| `TOTZEIT` | „Zeit", Beispiel 10 s | **0–10000 ms**, Vorgabe 0 | Lüfter bis 10 W mit integriertem ESC schalten in unter 2 s um |
| `ANLAUFPULS_DAUER` | typisch 100–500 ms | **0–10000 ms**, Vorgabe 0 | Puls erst auf der Hardware verifizieren |
| `FREIGABE_UEBERWACHUNGSZEIT` | „Zeit" | **0–60 min**, Vorgabe 2, 0 = aus | Minuten statt Stunden; 0 erlaubt Anlagen ohne verknüpfte Freigabe |
| `ZYKLUSZEIT` | „Zeit" | **0–3600 s**, Vorgabe 40 | |
| Überwachungszeit Master | — | **0–3600 s**, Vorgabe 35, 0 = aus | **In Sekunden**, weil die Zeit unter einer Zykluszeit bleiben muss — in Minuten wäre schon der kleinste Wert das Anderthalbfache der Vorgabe-Zykluszeit |
| Sendeabstand Lebenszeichen | — | **1–60 min**, Vorgabe 1 | |

Die Totzeit liegt **innerhalb** der Zykluszeit, sie kommt nicht obendrauf (offene Frage O-1 der
alten Fassung, damit erledigt).

### 3.7 Takt und Richtungsart: ein Objekt für beide Rollen

Die Anforderungen listen `TAKT_ZUSTAND` als Slave-Eingang; MA-2 verlangt ihn zugleich als
Master-Ausgang. Umgesetzt ist **je ein Objekt für beide Rollen** auf einer gemeinsamen
Gruppenadresse — der Master sendet darauf, die Slaves empfangen dort. Nur die Beschriftung
wechselt mit der Rolle („… Sollwert Gruppe" beim Master).

### 3.8 Diagnose-Objekte: geschrieben, nicht gesendet

`BETRIEBSSTUNDEN`, `MASTER_ERREICHBAR`, `IST_SUSPENDIERT` und `ZYKLUS_REST` werden in ihr KO
geschrieben, ohne zu senden. Eine Leseanforderung liefert damit den aktuellen Stand, ohne
zyklische Buslast:

| Objekt | Nachgeführt |
|---|---|
| Betriebsstunden | alle 30 min |
| Zyklus Restzeit | alle 5 s |
| Master erreichbar | bei jeder Änderung |
| Ist suspendiert | beim Start und bei jedem Wechsel |

`ZYKLUS_REST` wird zusätzlich beim Richtungswechsel aktiv gesendet, damit der Wechsel auf dem
Bus sichtbar ist.

### 3.9 Stoßlüftung: Anstoß mit Abbruchmöglichkeit

M-10 fordert einen „einmaligen Anstoß, kein Zustand". Umgesetzt mit DPT 1.010 Start/Stop statt
1.017 Trigger, damit eine `0` die laufende Stoßlüftung abbricht. Ein erneuter Anstoß startet
die Dauer neu (M-13).

---

## 4. Ergänzt ➕

Das fehlte in den Anforderungen, weil es ohne Bus- und Hardwarekenntnis nicht sichtbar war.

| Bereich | Ergänzung |
|---|---|
| **Sendebedingungen** | Totband in % **und** absoluter Sockelwert je Analogausgang, plus Mindest-Sendeabstand. Eine rein relative Schwelle versagt beim vorzeichenbehafteten Volumenstrom: am Nulldurchgang wird x % beliebig klein und der Knoten sendet dauerhaft. |
| **Masterüberwachung** | `MASTER_LEBT` (Master-Ausgang / Slave-Eingang), Parameter Überwachungszeit und Sendeabstand. KNX kennt keinen Verbindungszustand — Erreichbarkeit ist nur über zyklisches Senden plus Timeout darstellbar. |
| **Stellgröße Mittelstellung** | K-3 fordert eine bipolare Stellgröße, benennt den Mittelwert aber als Teil der gestrichenen Kennlinie. Jetzt ein eigener Parameter, nur beim reversiblen Kanal. |
| **Fehlercode-Wertevorrat** | Enum 0–8 mit dokumentierter Priorität (siehe 4.1). |
| **Kanalbeschreibung** | Freitext je Kanal, benennt den ETS-Reiter und die Objekte. |
| **Zwei Hardwarevarianten** | Reg1 Fan-Addon-X2 und MrSpieb HW-FanControl, zur Compile-Zeit gewählt: PWM-Polarität und Anzahl der Ausgänge unterscheiden sich und gehören nicht in die ETS. |
| **PWM-Frequenz** | 500–20000 Hz, Vorgabe 1000, geräteweit — auf dem RP2040 ist sie keine Eigenschaft des einzelnen Ausgangs. |
| **Logikmodul** | 30 Kanäle, unverändert eingebunden. |
| **Zweipunkt mit Hysterese** | Vierte Sollwertquelle, aus der Original-Firmware nachgeholt: schalten an zwei Schwellen statt proportional. Teilt Regelgröße und Istwert-Eingang mit dem P-Regler. |
| **Taupunktwächter** | Master-Veto über allen Sollwertquellen: vergleicht den Taupunkt innen und außen und sperrt, wenn die Außenluft die feuchtere ist. Vier eigene Messwert-Objekte (KO 20–23), Hysterese über zwei Abstände, Überwachungszeit für die Messwerte und wählbares Verhalten, wenn sie fehlen. |
| **Status-LEDs** | Eine RGB-LED je Lüfter auf Geräten, die sie haben: grün Richtung A, blau Richtung B, rot bei Störung, dunkel im Stillstand, 25 % Helligkeit. Die Anforderungen kennen keine örtliche Anzeige — sie entstanden ohne Kenntnis der Gerätehardware. Welche LED welchen Lüfter zeigt, wählt der Benutzer im BASE-Modul. |
| **Konsolenbefehle** | Kanalzustände lesen und einen Lüfter zum Test ansteuern, ohne ETS und ohne Gruppenadresse — für die Erstinbetriebnahme, wenn die Verdrahtung vor der Projektierung geprüft werden soll. Die Übersteuerung ersetzt den Sollwert, nicht die Vetos, und verfällt nach 10 Minuten. |

### 4.1 Fehlercode

Ein Enum als Einzelwert, kein Bitfeld. Tragend dafür: die meisten Ursachen schließen sich
konstruktiv aus, weil jeder Fehler, der den Lüfter stoppt, die laufabhängigen Fehler
unterdrückt — genau das Prinzip aus M-4. Nach Freigabe- oder Master-Timeout ist die Leistung 0,
„keine Drehzahl trotz Ansteuerung" kann dann nicht mehr auslösen.

| Wert | Bedeutung | Priorität |
|---|---|---|
| 0 | kein Fehler | — |
| 1 | Freigabe fehlt oder Überwachungszeit abgelaufen | 1 (höchste) |
| 2 | Master-Timeout | 2 |
| 3 | Konfigurations- bzw. Kennlinienfehler | 3 |
| 4 | ungültiger Empfangswert | 4 |
| 5 | keine Drehzahl trotz Ansteuerung | 5 |
| 6 | Überwachung ausgesetzt (suspendiert) | 6 |
| 7 | Taupunktwächter sperrt | 7 |
| 8 | Taupunktwächter ohne Messwerte | 8 (niedrigste) |

Wert 6 ist die konkrete Darstellung des von A-2 geforderten „nicht verfügbar". `Störung`
bleibt dabei 0 — eine Suspendierung ist kein Fehler.

Blockierter Rotor und defekter Drehzahlgeber fallen bewusst zu **einem** Wert zusammen (5): das
Symptom ist in beiden Fällen „angesteuert, aber keine Drehzahl", eine Unterscheidung ist
grundsätzlich nicht möglich. Genau das sagt A-1 bereits.

---

## 5. Layout: beschlossen gegen umgesetzt

Die alte Fassung dieses Dokuments nannte hier Zahlen, die sich während der Umsetzung geändert
haben. Verbindlich ist die rechte Spalte, verifiziert gegen `include/knxprod.h`.

| | Beschlossen | Umgesetzt |
|---|---|---|
| KOs je Kanal | 29er-Block, 23 belegt, Kanal 1 = KO 20–48 | **32er-Block, 26 belegt**, Kanal 1 = **KO 20–51** … Kanal 8 = **244–275** |
| Freie KO-Nummern | 8, 9, 20–23 | **9, 24, 25, 26, 28, 29** — 8 ist der Istwert, 20–23 gehören dem Taupunktwächter |
| Parameter je Kanal | 80 Byte, 52 belegt | 80 Byte, **72 belegt**, 8 Byte Reserve |
| Knoten je Gerät | 2 | **8 Kanäle in der ETS**, real so viele wie das Board Ausgänge hat |
| Logikmodul | `KoOffset 280` | **`KoOffset 512`** — 8 Lüfterkanäle reichen bis KO 275 |

Die Kanalzahl der Applikation und die des Boards sind getrennt: die ETS bietet 8 an, das
Geräte-Header sagt über `FAN_BOARD_CHANNELS` und `FAN_INIT()`, wie viele davon Pins
haben (beide Boards derzeit 2). Die Firmware dimensioniert ihre Felder nach der **Board**-Zahl,
ein höherer ETS-Wert kostet also KO-Nummern und Parameterspeicher, aber kein RAM. Mehr Lüfter zu
konfigurieren als das Board treiben kann, wird beim Start als Fehler protokolliert.

Die freien KO-Nummern liegen **innerhalb** des Blocks, nicht am Ende. Der OpenKNXproducer
leitet die Blockgröße ausschließlich aus der höchsten benutzten `%Kn%`-Nummer ab; ein Attribut
zum Setzen der Blockgröße existiert nicht, ein angegebenes `KoBlockSize` wird **ohne
Fehlermeldung ignoriert**. Reserve am oberen Ende ließe sich nur über ein Dummy-Objekt
erzwingen, was kein OpenKNX-Modul tut.

Das ist eine Einbahnstraße: KO-Nummern und ETS-IDs dürfen sich innerhalb eines Update-Pfades
nie ändern. Ein neues KO gehört in eine der Lücken, niemals oberhalb von 31.

**Versionierung:** `OpenKnxId 0xAF`, `ApplicationNumber 0x86`, `ApplicationVersion 0.1`,
`ReplacesVersions 0.1`. Die Selbstreferenz ist Absicht — die Neuentwicklung bricht das
KO-Layout, ein Migrationspfad aus der Alt-Applikation würde Bestandsanlagen beschädigen. Leeren
lässt sich das Attribut nicht, der Producer behandelt es als Pflichtfeld. Die
ApplicationNumber ist noch die alte und vor dem ersten externen Test umzustellen.

---

## 6. Erledigte Detailfragen

Die alte Fassung führte sieben offene Detailfragen. Stand jetzt:

| Frage | Antwort |
|---|---|
| Ist die Totzeit Teil der Zykluszeit? | **Teil davon.** Der Taktgeber läuft unabhängig vom Zustandsautomaten, die Totzeit zehrt von der Zykluszeit. |
| Fällt `Störung` selbsttätig? | **Gemischt.** Master-Timeout löst sich selbst, sobald ein Lebenszeichen eintrifft. Die Blockiermeldung ist selbsthaltend und braucht eine Quittierung. Ein ungültiger Empfangswert verfällt mit dem nächsten gültigen. |
| Sockelwert des Totbands: fest oder Parameter? | **Parameter**, je Analogausgang. |
| Ungültige Richtungsart-Codes? | **Abweisen und melden**, Fehlercode 4. Der letzte gültige Wert bleibt. |
| Vorgabe der Master-Überwachungszeit? | **35 s**, bei 40 s Vorgabe-Zykluszeit. Deshalb in Sekunden statt Minuten — in Minuten war die Empfehlung „≤ eine Zykluszeit" nicht darstellbar. |
| Master und Slave im selben Gerät koppeln? | **Nicht umgesetzt.** Die Telegramme laufen auch geräteintern über den Bus und sind in der ETS zu verknüpfen. |
| ConfigTransfer-Format für ein Begleit-Tool? | **Offen**, kein Tool gebaut. Das Format ist laut ConfigTransfer-README noch Entwurf. |

---

## 7. Unverändert übernommen ✔

Zur Abgrenzung — alles Folgende gilt wörtlich wie im Anforderungsdokument und ist oben
deshalb **nicht** aufgeführt:

**Begriffe und Konfiguration:** Lüfterknoten als kleinste Einheit, Gruppe, Master/Slave,
Phase/Gegenphase, Richtung A/B, Taktzustand, Zyklus, Leistung, Anteilsfaktor.
`IST_MASTER`, `HAT_RUECKMELDUNG`, `PHASE`, `ANTEILSFAKTOR`, `VOLUMENSTROM_INVERTIEREN`.

**Kennlinienregeln:** K-1, K-2, K-4, K-5, K-6, K-8, K-9, K-10. K-3 gilt weiter, nur anders
ausgedrückt (3.1). K-7 gilt mit einer Einschränkung (3.3).

**Freigabe:** E-1 bis E-1h vollständig. Zwei unabhängige Sperrauslöser, selbsthaltend,
persistent, Ruhestromprinzip, unverzüglicher Stopp ohne Rücksicht auf Totzeit, Stoßlüftung oder
anstehende Taktumschaltung.

E-1f verlangt, dass die Überwachung nach einem Neustart **sofort** wieder greift. Dafür genügt
der Zeitstempel des letzten Telegramms nicht, denn der ist nach dem Neustart leer. Umgesetzt ist
deshalb ein zusätzliches **persistentes Bit „Freigabe wurde schon einmal empfangen"**: ist es
gesetzt, läuft die Überwachungszeit ab dem Neustart, ohne auf ein erstes Telegramm zu warten.
Ist es nie gesetzt worden, ist das Objekt offensichtlich nicht verknüpft und die Überwachung
bleibt aus — sonst würde jede Anlage ohne Freigabe-Konzept nach zwei Minuten sperren. Damit sind
beide Fälle erfüllt, ohne sie gegeneinander auszuspielen.

Die Tabelle in **M-6** ist damit an einer Stelle zu korrigieren: `FREIGABE = 0` bleibt nach
Neustart erhalten (**ja**, nicht nein). E-1e sagt das bereits, die Tabelle widersprach ihm.

**Übrige Eingangsregeln:** E-2, E-3, E-4, E-5, E-6.

**Ausgänge:** A-1, A-2, A-3, A-4 — inklusive Vorzeichenkonvention positiv = Zuluft und der
Summierbarkeit über mehrere Knoten.

**Betrieb:** M-1, M-2, M-3, M-4, M-5, M-7, M-8, M-9, M-11, M-12, M-13, M-15.

**Master:** MA-1, MA-2, MA-3, MA-4, MA-5, MA-6.

---

## 8. Sicherheitshinweis

KNX ist **kein Sicherheitsbus**. E-1h benennt das richtig: für den Verbund mit einer Feuerstätte
ist üblicherweise ein fest verdrahteter, potentialfreier Kontakt gefordert; der Busweg ist
Ergänzung, nicht Ersatz. Das gehört in die Produktdokumentation.
