# Referenzdesign Rev 4 — Lüftersteuerung 0–10 V mit GP8413

**Projekt:** Dezentrale Lüftung mit KNX-Eigenbausteuerung (OpenKNX-Applikationsmodul)
**Stand:** 09.09.2026
**Ersetzt:** Rev 3 (`Referenzdesign_Rev3_Minimal_3Adrig_Isoliert.md`)
**Status:** Entwurf zur Prüfung, Schaltplan noch nicht gezeichnet

---

## 0. Was sich gegenüber Rev 3 ändert und warum

Die Datenblätter GP8403-CN-V1.13 und GP8413-CN-V1.13 haben die drei offenen Fragen
aus Rev 3 beantwortet:

| Frage aus Rev 3 | Antwort | Konsequenz |
|---|---|---|
| EEPROM beim GP8413? | **ja**, identische Sequenz wie GP8403 (§3.3.6/3.3.7) | letztes Argument für den GP8403 entfällt |
| Logikpegel des I²C-Eingangs? | **2,7–5,5 V**, beide Typen | RP2040 mit 3,3 V speist direkt, kein Pegelwandler |
| Gehäuse? | **ESOP10** mit Heat Slug, bei beiden Typen maßidentisch | ein Footprint für beide, Auswahl erst bei Bestückung |

**Entscheidung: 2 × GP8413-TC25-EW.**
Begründung im Fehlerbudget (Abschnitt 3.3). Der GP8403 bleibt pinkompatible
Rückfallposition ohne Layoutänderung.

Die Analogkette der Rev 0 bis Rev 3 entfällt vollständig. Damit fallen zwei
Konstruktionsprobleme weg, die den Entwurf bisher bestimmt haben:

- **Riso innerhalb der Gegenkopplung** — gegenstandslos, es gibt keine
  Gegenkopplung mehr. Die Genauigkeit hängt nicht länger an der unbekannten
  Eingangsimpedanz des Lüfters.
- **Trennung AGND / PGND mit genau einer Brücke** — gegenstandslos. Es gibt
  keinen Präzisions-Referenzknoten mehr, dessen Versatz mit Faktor 2 bis 3
  am Lüfter erscheint. Übrig bleibt eine reine Layoutregel (Abschnitt 8.3).

**Entfallene Baugruppen:** OPA4197, MCP4728, REF5050, Ladungspumpe TPS60403,
Induktivität L1, LC-Nachfilter, negative Hilfsspannung, Kelvin-Zweig,
BSS138-Pegelwandler, alle Präzisionswiderstände der Summierstufe.

---


## 0.5 Abgleich mit der realisierten Platine KNXFANDRV Rev 0.1

Die Platine ist gezeichnet und weicht an mehreren Stellen von diesem Entwurf
ab. **Wo Entwurf und Platine auseinandergehen, gilt die Platine** — dieses
Dokument ist hier nachgezogen, nicht umgekehrt. Die Abweichungen im Einzelnen:

| Punkt | Entwurf | Platine Rev 0.1 | Bewertung |
|---|---|---|---|
| Aufbau | Applikationsplatine auf REG1-Base, gekoppelt über J1 | **eine Platine**: OpenKNX-BCU-Modul GN100 plus eigener RP2040 (U100) | Platine. J1/REG1_App_Connector entfällt ersatzlos; die Pinliste in 5.1 ist damit hinfällig. |
| I²C-Leitungen | GP26 / GP27 (I2C1) | **GPIO2 = SDA, GPIO3 = SCL** (I2C1) | Platine. Betrifft die Bitbang-Routine in 6.4. |
| I²C nach außen | — | zusätzlich auf J110 (FrontPanel) Pin 7/8 | siehe Prüfliste: die Store-Sequenz belegt Adresse 0x08 |
| Klemmen | J5 sechspolig für das Bad | **vier Stück XY303V-3,81-3P**, J3…J6 | Platine. Für das Bad zwei Klemmen statt einer — funktional gleich, eine Leitung mehr zu ziehen. |
| Klemmenbelegung | 1 = ⊕, 2 = ⊖, 3 = S | **1 = 12 V, 2 = S, 3 = GND** | Platine. Reihenfolge merken, sie steht auch im Bestückungsdruck. |
| Masserückführung | gemeinsames PGND an allen Klemmen | **CH1_PGND … CH4_PGND**, je Kanal eigenes Netz | **besser als der Entwurf.** Entspricht der LUNOS-Vorgabe „sternförmig von der Steuerung aus". |
| Reserveleiste J6 | sechspolige Stiftleiste | entfällt, J6 ist jetzt eine Klemme | Platine. |
| DAC-Typ | GP8413-TC25-EW | **GP8413-TC50-EW** | siehe Prüfliste, Fehlerbudget trägt es |
| R4 / R5 | DNP | **bestückt** | **Platine hat recht.** Ohne REG1-Base bringt niemand sonst Pull-ups mit. |
| C9 / C12 (10 µF) | gestrichen | nicht vorhanden | siehe Prüfliste: Pads wären sinnvoll |
| Bezeichnerzuordnung | C14…C17 = S1…S4 der Reihe nach | **C14→S1, C16→S2, C15→S3, C17→S4**, D2→S1, D4→S2, D3→S3, D5→S4 | Platine. Funktional gleich, Stückliste folgt dem Schaltplan. |
| Q1 | P-FET mit V(GS) ≥ ±20 V | **AO3401A** mit DZ1 8,2 V | korrekt — die Zenerklemmung macht die ±12-V-Grenze des AO3401A unkritisch |
| TVS | SMAJ12A | **SMBJ12A** (größeres Gehäuse, unidirektional) | gleichwertig, höhere Pulsbelastbarkeit |
| U1 Pinbelegung | ungeprüft (O5) | 1 VDD1, 2 SDA1, 3 SCL1, 4 GND1, 5 GND2, 6 SCL2, 7 SDA2, 8 VDD2 | **O5 geschlossen** |

## 1. Systemüberblick

```
   KNX-Bus
      │
┌─────┴───────────────────┐
│  OpenKNX REG1-Base      │   Busankopplung + RP2040
│  (Trägerplatine)        │
└─────┬───────────────────┘
      │ J1 = REG1_App_Connector
      │ Pin 4 GP26 = SDA · Pin 5 GP27 = SCL   (Entwurf; Platine: GPIO2/GPIO3)
      │ Pin 8 GND  = BCU_GND · Pin 9 = +3V3_BCU
══════╪═══════════════════════════════════════════  Applikationsplatine Rev 4
      │
┌─────┴──────┐
│  U1        │  ADuM1250ARZ — galvanische Trennung des I²C
│  ADuM1250  │  VDD1 aus +3V3_BCU (~1 mA) │ VDD2 aus +5V_ISO
└─────┬──────┘
      │ SDA_I / SCL_I, Pull-ups 4k7 nach +5V_ISO
      ├───────────────────────┬──────────────────────┐
┌─────┴──────┐         ┌──────┴─────┐         ┌──────┴─────┐
│  U2        │         │  U3        │         │  U4        │
│  GP8413    │         │  GP8413    │         │  LDO 5 V   │
│  0x58      │         │  0x59      │         │  TLV70450  │
│  VOUT0→S1  │         │  VOUT0→S3  │         └──────┬─────┘
│  VOUT1→S2  │         │  VOUT1→S4  │                │ +5V_ISO
└─────┬──────┘         └──────┬─────┘                │
      │                       │                      │
      └───────────┬───────────┴──────────────────────┘
                  │  je Kanal: 100 nF · TVS 12 V · PTC 0,75 A
      ┌───────────┼───────────┬───────────┐
    J3 (S1)     J4 (S2)     J5 (S3)     J6 (S4)
   Schlaf-       Büro        Bad          Bad
   zimmer                    Motor 1      Motor 2
   e²60          e²60        ego          ego
                  ▲
                  │ +12 V SELV, PGND
            ┌─────┴─────────────────────┐
            │ J2 ← LUNOS 5/NT18 o. ä.   │
            │ F1 PTC 1,5 A · Q1 P-FET   │
            │ D1 TVS · C1 100 µF        │
            └───────────────────────────┘
```

**Kanalzuordnung**

| Kanal | Netz | Klemme | Raum | Gerät | DAC | Ausgang | I²C-Adr. | Register |
|---|---|---|---|---|---|---|---|---|
| 1 | S1 | J3 | Schlafzimmer | e²60 | U2 | VOUT0 (Pin 8) | 0x58 | 0x02 |
| 2 | S2 | J4 | Büro | e²60 | U2 | VOUT1 (Pin 7) | 0x58 | 0x04 |
| 3 | S3 | J5 | Bad | ego Motor 1 | U3 | VOUT0 (Pin 8) | 0x59 | 0x02 |
| 4 | S4 | J6 | Bad | ego Motor 2 | U3 | VOUT1 (Pin 7) | 0x59 | 0x04 |

> **Klemmenbeschriftung nachgezogen (20.09.2026):** Blockbild und Tabelle nannten noch
> die sechspolige J5 des Entwurfs. Die Abgleichtabelle in Abschnitt 1 entscheidet das
> zugunsten der Platine: vier dreipolige Klemmen J3…J6, also S3 auf J5 und S4 auf J6.
> Gegen den Bestückungsdruck prüfen, bevor verdrahtet wird.

Der ego liegt bewusst **vollständig auf U3**. Beide Motoren müssen synchron
gestellt werden, und U3 erlaubt mit Register 0x02 einen Doppelschreibvorgang
für VOUT0 und VOUT1 in einem einzigen I²C-Frame (Datenblatt §3.3.5). Damit
laufen die beiden Motoren des ego auch beim Umschalten der Förderrichtung
nicht auseinander.

---

## 2. Bauteilbezogene Fakten aus den Datenblättern

### 2.1 Pinbelegung GP8413 / GP8403 (identisch)

| Pin | Name | Funktion |
|---|---|---|
| 1 | SCLK | I²C-Takt |
| 2 | SDA | I²C-Daten |
| 3 | A0 | Hardware-Adresse Bit 0 |
| 4 | A1 | Hardware-Adresse Bit 1 |
| 5 | VCC | Versorgung 9…36 V (typ. 12 V) |
| 6 | GND | Masse |
| 7 | **VOUT1** | Analogausgang 2 |
| 8 | **VOUT0** | Analogausgang 1 |
| 9 | A2 | Hardware-Adresse Bit 2 |
| 10 | V5V | interner LDO, 5 V, ≥ 1 µF extern zwingend |

> **Achtung:** VOUT0 liegt auf Pin **8**, VOUT1 auf Pin **7** — nicht
> aufsteigend. Zweite Falle nach der vertauschten Unit C des OPA4197.

**Gehäuse ESOP10**, JEDEC MO-137E: Body D 4,80/4,90/5,00 mm, E1 3,80/3,90/4,00 mm,
Spannweite E 5,80/6,00/6,20 mm, Raster e 1,0 mm BSC, Pinbreite b 0,31/0,35/0,39 mm,
Höhe A 1,35/1,50/1,65 mm, Fußlänge L 0,45/0,60/0,80 mm.
**Heat Slug** (Exposed Pad) D1 3,20/3,30/3,40 mm × E2 2,00/2,10/2,20 mm.

### 2.2 Elektrische Kenndaten

| Parameter | GP8413 | Bemerkung zum Entwurf |
|---|---|---|
| VCC | 9 / 12 / 36 V | läuft direkt an den 12 V, keine Vorregelung |
| ICC | 2 mA typ., < 5 mA | 2 Chips ⇒ ≤ 10 mA |
| VOUT | 0…10 V | Bereich per Register 0x01 |
| ΔVOUT | 0,2 % typ. | = 20 mV auf 10 V |
| Linearität | 0,02 % (Merkmale) / 0,1 % (DC-Tabelle) | konservativ 0,1 % = 10 mV rechnen |
| TC | 25 PPM/°C (TC25) | 0,25 mV/K auf Vollausschlag |
| Startzeit | < 2 ms | |
| f(SCLK) | ≤ 400 kHz | |
| Logikpegel high | 2,7…5,5 V | 3,3 V des RP2040 ausreichend |
| Kurzschlussschutz | ja, Ausgang gegen GND ⇒ Schutzmodus | |
| ESD | > 2 kV | trotzdem TVS an der Klemme |

### 2.3 Registersatz und Datenformat

| Register | Wirkung |
|---|---|
| 0x01 | Bereichswahl: Datenbyte 0x00 → 0–5 V, **0x11 → 0–10 V** |
| 0x02 | VOUT0 schreiben; bei Doppelschreiben VOUT0 **und** VOUT1 |
| 0x04 | VOUT1 schreiben |

Adressbyte: `1 0 1 1 A2 A1 A0 W` ⇒ 7-Bit-Adressen **0x58 … 0x5F**, acht Chips
an einem Bus.

Datenformat GP8413, 15 Bit, logischer Wertebereich `0x0000…0x7FFF`, zwei Bytes
**Low zuerst**. Wie der logische Wert auf die zwei Bytes abgebildet wird, ist
**strittig** — siehe O17. Zwei Kandidaten:

```
Code = round(VOUT × 3276.7)             logischer 15-Bit-Wert, 0…0x7FFF

Variante D (Datenblatt §3.3.3, rechtsbündig):
  Wire      = Code                      Bit 15 = 0
  Low Byte  = Wire & 0x00FF
  High Byte = (Wire >> 8) & 0x007F

Variante B (DFRobot_GP8XXX, linksbündig):
  Wire      = Code << 1                 0…0xFFFE
  Low Byte  = Wire & 0x00FF
  High Byte = (Wire >> 8) & 0x00FF
```

**Die beiden liegen um Faktor zwei auseinander.** 5,00 V ist entweder 0x4000
oder 0x8000, Vollausschlag entweder 0x7FFF oder 0xFFFE. Wer die falsche
Variante wählt, erzeugt aus dem Sicherheitswert 5,00 V entweder 2,50 V
(Dauerlüftung) oder 10,00 V (**Volllast**). Vor jeder anderen Messung zu
klären, und der Wert im EEPROM hängt genauso daran.

Datenformat GP8403 (Rückfall), 12 Bit **linksbündig** in 16 Bit: die unteren
vier Bit des Low-Byte werden ignoriert, `VOUT = 10 V × DATA / 0xFFF`, auf den
Bus geht `code12 << 4`.

**Die Wireformate sind nicht kompatibel.** Halbe Skala, also 5,00 V, ist beim
GP8413 `0x4000`, beim GP8403 `0x8000` (aus `0x800 << 4`). Vollausschlag ist
`0x7FFF` gegen `0xFFF0`. Ein für den einen Chip berechneter Wert erzeugt am
anderen also einen völlig anderen Ausgang — beim Verwechseln von 5,00 V nach
10,00 V, und das ist Volllast. Der Chiptyp muss ein harter
Konfigurationsparameter sein, kein Autodetect-Versuch.

### 2.4 EEPROM-Sequenz — kein normales I²C

Das Timingdiagramm in §3.3.7 (GP8403) bzw. §3.3.6 zweiter Teil (GP8413) lässt
sich mit der Referenzimplementierung von DFRobot in konkrete Bytewerte
auflösen. Die Bitmuster im Diagramm und die Konstanten der Bibliothek stimmen
überein:

| Schritt | Inhalt | Bemerkung |
|---|---|---|
| 1 | START, **drei Bit** `010` (= 0x02), **kein ACK**, STOP | „Kopf" der Entsperrung |
| 2 | START, Byte `0x10`, Byte `0x03`, STOP | Entsperren |
| 3 | START, Byte `Adr << 1`, dann **8 × `0x00`**, STOP | löst das Brennen aus |
| 4 | **≥ 7 ms warten** (DFRobot wartet 10 ms) | |
| 5 | START, drei Bit `010`, kein ACK, STOP | Kopf der Sperrung |
| 6 | START, Byte `0x10`, Byte `0x00`, STOP | Sperren |

**Das ist der wichtigste neue Befund, und er ist unangenehm.** Drei Konsequenzen:

**(a) Der I²C-Controller des RP2040 kann das nicht erzeugen.** Ein Frame aus
drei Bit ohne ACK ist kein gültiges I²C. Für die Store-Sequenz müssen GPIO2 und
GPIO3 vorübergehend aus der I2C1-Funktion gelöst und als GPIO bitgebangt werden
(`gpio_set_function` → `GPIO_FUNC_SIO`, danach zurück auf `GPIO_FUNC_I2C`).
Genau das macht die DFRobot-Bibliothek auch: sie beendet `Wire`, bangt, und
startet `Wire` neu.

**(b) Die Sequenz muss durch den ADuM1250 hindurch.** Der Isolator parst kein
I²C, sondern überträgt Open-Drain-Pegel bidirektional — nichtstandardkonforme
Wellenformen sollten also durchgehen. Bewiesen ist das nicht. Die
Bitbang-Taktzeit der Referenz liegt bei etwa 5 µs pro Halbwelle, also ~100 kHz;
das ist für den ADuM1250 unkritisch. **Neuer offener Punkt O13, neuer
Prüfschritt P15.**

**(c) Entsperren und Sperren gelten busweit, nur Schritt 3 ist adressiert.**
Die Frames in Schritt 2 und 6 gehen an `0x10` als Adressbyte, also an 7-Bit-
Adresse 0x08 — eine reservierte Adresse, keinen der beiden DACs. Daraus folgt:

- Beide Chips **sequenziell** speichern, nie verschachtelt: entsperren →
  U2 brennen → warten → sperren, dann dasselbe für U3.
- Der I²C-Strang darf während der Store-Sequenz **nicht** mit anderen
  Teilnehmern geteilt werden. In Rev 4 hängen ohnehin nur U2 und U3 daran,
  aber die Reserveleiste J6 darf dafür nicht als I²C-Erweiterung missbraucht
  werden.

Die Schreibzyklenzahl ist in keinem der beiden Datenblätter angegeben.
**Der EEPROM ist ausschließlich Ablage des Einschaltwerts, gesetzt bei der
Inbetriebnahme — niemals Ablage von Betriebssollwerten.**

---

## 3. Sicherheits- und Genauigkeitskonzept

### 3.1 Der Einschaltzustand — wichtigste Anforderung des Entwurfs

0 V am Stellsignal bedeutet **Volllast**, nicht Stillstand. Rev 4 löst das
zweistufig:

**Stufe 1 — EEPROM des DAC.** Der Chip gibt nach dem Einschalten der 12 V den
gespeicherten Wert aus, ohne dass ein Controller beteiligt ist. Startzeit < 2 ms.

**Nebenwirkung des LDO:** Der TLV704 leitet über die Body-Diode seines
PMOS-Durchgangstransistors Rückstrom, sobald IN unter OUT fällt, und begrenzt
ihn nicht. Beim Wegfall der 12 V entlädt sich C7 also rückwärts in die
12-V-Schiene. Bei 4,7 µF ist das harmlos; eine externe Begrenzung wäre nur bei
dauerhaftem Rückspannungsbetrieb nötig.

**Stufe 2 — Registerhaltung bei Busausfall.** Fällt der KNX-Bus aus, stirbt der
RP2040 (er wird laut REG1-Konzept aus dem Bus versorgt). Die DACs hängen an den
**12 V** und halten ihren letzten geschriebenen Registerwert unverändert. Die
Lüfter laufen also weiter, und zwar auf dem letzten gültigen Sollwert. Damit ist
die Feuchteschutz-Anforderung aus Rev 3 §5.1 durch die Architektur erfüllt —
ohne Latch, ohne Stützkondensator, ohne Watchdog-Hardware.

### 3.2 Entscheidung D1: welcher Wert in den EEPROM?

| Option | Wert je Kanal | Wirkung nach 12-V-Ausfall | Risiko |
|---|---|---|---|
| **A — STOPP** | 5,00 V | alle Lüfter stehen | Lüftung bleibt nach jedem Netzwischer still, bis der Bus wieder da ist; im Bad Feuchteproblem |
| **B — Stufe 1** | e²60 4,01/6,01 V, ego 3,26/6,85 V | Grundlüftung läuft | ohne Controller keine Rückmeldung, dauerhafte Grundlüftung |

**Empfehlung: B.** Stufe 1 ist beim e²60 5 m³/h bei 3 dB(A) in 3 m und 0,4 W —
das ist der Feuchteschutz, den man nach einem Netzausfall genau will. Option A
wirkt sicherer, ist es aber nicht: ein stillstehender Lüfter im renovierten Bad
ist der Schadensfall, den das ganze Projekt vermeiden soll. Zu prüfen bleibt,
ob der EEPROM auch das Bereichsbit sichert — siehe offener Punkt O1.

### 3.3 Fehlerbudget je Kanal

| Beitrag | GP8413-TC25 | GP8403-TC50 |
|---|---|---|
| Ausgangsfehler | ±20 mV (0,2 %) | ±50 mV (0,5 % max.) |
| Linearität | ±10 mV (0,1 %) | ±10 mV |
| Temperatur, ΔT = 30 K | ±7,5 mV | ±15 mV |
| Auflösung (½ LSB) | ±0,15 mV | ±1,2 mV |
| Restfehler Kabelkompensation | ±20…30 mV | ±20…30 mV |
| **quadratisch summiert** | **≈ ±33 mV** | **≈ ±58 mV** |
| **worst case linear** | **≈ ±58 mV** | **≈ ±106 mV** |

**Auf der Platine Rev 0.1 sind TC50-Typen bestückt**, nicht TC25. Der
Temperaturbeitrag verdoppelt sich damit auf ±15 mV über 30 K, quadratisch
summiert etwa ±37 mV statt ±33 mV. Das Budget trägt es; falls TC25 lieferbar
ist, wäre der Tausch ohne jeden Mehraufwand die bessere Wahl.

Damit wird die Breite des Totbands um 5,00 V zur Abnahmebedingung
(Messung M3, Abschnitt 9). Liegt sie unter ±50 mV, ist der GP8403 unbrauchbar
und der GP8413 grenzwertig ohne vierte Ader. Liegt sie über ±100 mV, taugen
beide.

Der Ausgangsfehler von 0,2 % ist ein **Skalenfehler**, kein Zufallsfehler. Er
lässt sich bei der Inbetriebnahme je Kanal einmalig ausmessen und in der
Firmware als Verstärkungskorrektur ablegen. Dann bleiben ±20 mV übrig,
dominiert von der Kabelkompensation.

### 3.4 Sollwerttabelle mit DAC-Codes (0-10-V-Modus, 15 Bit)

Alle Codes in den folgenden Tabellen sind **logische 15-Bit-Werte**
(`round(V × 3276.7)`). Ob sie so oder um ein Bit nach links geschoben auf den
Bus gehen, entscheidet O17. Eine Zeile Firmware, aber die falsche Zeile
verdoppelt jede Ausgangsspannung.

**e²60, Codierschalter 5**

| Stufe | m³/h | Kanal A | Code A | hex | Kanal B | Code B | hex |
|---|---|---|---|---|---|---|---|
| AUS | 0 | 5,00 V | 16384 | 0x4000 | 5,00 V | 16384 | 0x4000 |
| 1 | 5 | 4,01 V | 13140 | 0x3354 | 6,01 V | 19693 | 0x4CED |
| 2 | 20 | 3,21 V | 10518 | 0x2916 | 6,91 V | 22642 | 0x5872 |
| 3 | 40 | 1,96 V | 6422 | 0x1916 | 8,15 V | 26705 | 0x6851 |
| 4 | 60 | 0,01 V | 33 | 0x0021 | 10,00 V | 32767 | 0x7FFF |

**ego, Codierschalter 9**

| Stufe | m³/h | Motor 1 | Code | hex | Motor 2 | Code | hex |
|---|---|---|---|---|---|---|---|
| AUS | 0 | 5,00 V | 16384 | 0x4000 | 5,00 V | 16384 | 0x4000 |
| 1 | 5 | 3,26 V | 10682 | 0x29BA | 6,85 V | 22445 | 0x57AD |
| 2 | 10 | 2,09 V | 6848 | 0x1AC0 | 8,00 V | 26214 | 0x6666 |
| 3 | 20 | 1,25 V | 4096 | 0x1000 | 8,85 V | 28999 | 0x7147 |
| 4 | 45 Abluft | 1,11 V | 3637 | 0x0E35 | **1,11 V** | 3637 | 0x0E35 |

**RA 15-60, Codierschalter 0** (unipolar, für einen möglichen Reservekanal)

| Stufe | m³/h | Spannung | Code | hex |
|---|---|---|---|---|
| AUS | 0 | 0,01 V | 33 | 0x0021 |
| 1 | 15 | 1,58 V | 5177 | 0x1439 |
| 2 | 30 | 3,57 V | 11698 | 0x2DB2 |
| 3 | 45 | 5,76 V | 18874 | 0x49BA |
| 4 | 60 | 8,15 V | 26705 | 0x6851 |

Die Werte sind Nominalwerte aus der LUNOS-Steuerung 5/SC-FT v5.14. Die
tatsächlich auszugebenden Codes ergeben sich erst nach Addition der
Kabelkompensation.

### 3.5 Kabelkompensation in Codes

```
V(Ausgang) = V(Soll) + I(Stufe) × R(Rückleiter)
ΔCode      = round(I(Stufe) × R(Rückleiter) × 3276.7)
```

Bei 0,7 mm² und 0,025 Ω/m:

| Länge | R | e²60 Stufe 4 (0,275 A) | ΔCode | ego (0,41 A) | ΔCode |
|---|---|---|---|---|---|
| 5 m | 0,125 Ω | 34 mV | 113 | 51 mV | 168 |
| 10 m | 0,250 Ω | 69 mV | 225 | 103 mV | 336 |
| 15 m | 0,375 Ω | 103 mV | 338 | 154 mV | 504 |
| 20 m | 0,500 Ω | 138 mV | 451 | 205 mV | 672 |

Die Korrektur ist **immer positiv** — auch für Codes unterhalb 0x4000, weil sich
der Massebezug verschiebt, nicht das Vorzeichen. Sie muss **stufenabhängig**
sein: auf Stufe 1 zieht der e²60 nur 0,033 A, das sind 12 mV bei 15 m.

Bei Kanal 4 des e²60 (0x7FFF = 10,00 V) ist die Korrektur **nicht mehr
darstellbar** — der DAC ist am Anschlag. Konsequenz: entweder Stufe 4 in
Richtung B minimal reduzieren (z. B. auf 9,90 V nominal) oder akzeptieren, dass
Stufe 4 in einer Richtung geringfügig unter Nennluftleistung bleibt.
**Zu entscheiden nach Messung M2.**

---

## 4. Netzliste

### 4.1 Netzübersicht

| Netz | Domäne | Beschreibung |
|---|---|---|
| `BCU_GND` | KNX | Masse der Trägerplatine, **nur** an J1.8 und U1 Primärseite |
| `+3V3_BCU` | KNX | 3,3 V aus der BCU, Budget beachten |
| `SDA_BCU` | KNX | GPIO2 (I2C1 SDA) |
| `SCL_BCU` | KNX | GPIO3 (I2C1 SCL) |
| `+12V_RAW` | Last | direkt an J2, vor Schutz |
| `+12V` | Last | nach F1 und Q1, Versorgung U2/U3/U4 |
| `PGND` | Last | Lastmasse, Bezug aller Stellsignale |
| `+5V_ISO` | Last | LDO-Ausgang, VDD2 des Isolators und Pull-ups |
| `SDA_I` / `SCL_I` | Last | isolierter I²C |
| `V5V_U2` / `V5V_U3` | Last | interne LDO-Ausgänge, **nicht** verbinden |
| `S1` … `S4` | Last | Stellsignale zu den Klemmen |
| `CH1_12V` … `CH4_12V` | Last | +12 V je Kanal nach PTC |

> **`BCU_GND` und `PGND` sind galvanisch getrennt.** Es gibt zwischen ihnen
> keine Brücke, keinen Widerstand und keinen Kondensator. Das war der Anlass
> für Rev 3 und gilt unverändert.

### 4.2 Verbindungen

**Steckverbinder zur Trägerplatine (J1, REG1_App_Connector)** — **hinfällig ab Platine Rev 0.1**, siehe 0.5. Der RP2040 sitzt dort auf derselben Platine, die KNX-Ankopplung übernimmt das Modul GN100. Die folgende Tabelle bleibt nur als Beleg stehen, warum die Netznamen so heißen wie sie heißen.


| Pin | Netz |
|---|---|
| 1 (GP16) | `RSV1` → J6.1 |
| 2 (GP17) | `RSV2` → J6.2 |
| 3 (GP18) | `RSV3` → J6.3 |
| 4 (GP26) | `SDA_BCU` |
| 5 (GP27) | `SCL_BCU` |
| 6 (GP28) | `RSV4` → J6.4 |
| 7 (GP29) | `RSV5` → J6.5 |
| 8 (GND) | `BCU_GND` |
| 9 (3V3) | `+3V3_BCU` |
| 10 (VCC2) | **unbeschaltet** |

**Eingangsschutz**

| Bauteil | Anschluss | Netz |
|---|---|---|
| J2.1 | +12 V Zuführung | `+12V_RAW` |
| J2.2 | Masse | `PGND` |
| F1 | 1 | `+12V_RAW` |
| F1 | 2 | `Q1_S` |
| Q1 (P-MOS) | S | `Q1_S` |
| Q1 | D | `+12V` |
| Q1 | G | `Q1_G` |
| R1 (100 k) | 1 / 2 | `Q1_G` / `PGND` |
| DZ1 (8,2 V) | K / A | `Q1_S` / `Q1_G` |
| D1 (TVS 15 V) | 1 / 2 | `+12V` / `PGND` |
| C1 (100 µF) | + / − | `+12V` / `PGND` |
| C2 (100 nF) | 1 / 2 | `+12V` / `PGND` |

Q1 in Source-nach-Quelle-Anordnung als Verpolschutz: Body-Diode leitet bei
richtiger Polarität, der Kanal schaltet über das nach PGND gezogene Gate durch.
DZ1 begrenzt V(GS) auf 8,2 V; damit sind auch P-FETs mit nur ±12 V
V(GS,max) einsetzbar (offener Punkt #1 aus Rev 3 damit **gelöst**, unabhängig
von der Bauteilwahl).

**Isolator U1 (ADuM1250ARZ) — Pinbelegung ungeprüft, siehe O5**

| Signal | Netz |
|---|---|
| VDD1 | `+3V3_BCU` |
| GND1 | `BCU_GND` |
| SDA1 | `SDA_BCU` |
| SCL1 | `SCL_BCU` |
| VDD2 | `+5V_ISO` |
| GND2 | `PGND` |
| SDA2 | `SDA_I` |
| SCL2 | `SCL_I` |

| Bauteil | Anschluss | Netz |
|---|---|---|
| C3 (100 nF) | 1 / 2 | `+3V3_BCU` / `BCU_GND` |
| C4 (100 nF) | 1 / 2 | `+5V_ISO` / `PGND` |
| C5 (1 µF) | 1 / 2 | `+5V_ISO` / `PGND` |
| R2 (4k7) | 1 / 2 | `SDA_I` / `+5V_ISO` |
| R3 (4k7) | 1 / 2 | `SCL_I` / `+5V_ISO` |
| R4 (4k7, DNP) | 1 / 2 | `SDA_BCU` / `+3V3_BCU` |
| R5 (4k7, DNP) | 1 / 2 | `SCL_BCU` / `+3V3_BCU` |

**Auf der Platine Rev 0.1 sind R4 und R5 bestückt, und das ist richtig.** Die
Einordnung als DNP galt nur für den Aufbau auf einer REG1-Base, die eigene
Pull-ups mitbringt. Mit dem RP2040 auf derselben Platine bringt sie niemand
sonst mit.

**LDO U4 (TLV70450DBVR, SOT-23-5, DBV)**

| Pin | Signal | Netz |
|---|---|---|
| 1 | **GND** | `PGND` |
| 2 | **IN** | `+12V` |
| 3 | **OUT** | `+5V_ISO` |
| 4 | NC | `PGND` (nur Wärmeabfuhr) |
| 5 | NC | `PGND` (nur Wärmeabfuhr) |

> **Achtung, die Reihenfolge ist unüblich und der TLV704 hat keinen
> EN-Pin.** Nicht mit dem TLV702 verwechseln (IN 1, GND 2, EN 3, NC 4, OUT 5) —
> dessen Belegung stand in Rev 0 und war hier falsch übernommen. Pin 4 und 5
> sind laut Datenblatt nicht intern verbunden und dürfen offen bleiben; an PGND
> gelegt verbessern sie die Wärmeabfuhr.

| Bauteil | Anschluss | Netz |
|---|---|---|
| C6 (1 µF) | 1 / 2 | `+12V` / `PGND` |
| C7 (**4,7 µF**) | 1 / 2 | `+5V_ISO` / `PGND` |

`C7 ≥ 1 µF` ist beim TLV704 **Stabilitätsbedingung**, kein Komfort. 1 µF wäre
genau das Minimum, und Keramik verliert unter DC-Vorspannung und Toleranz
leicht 30 %. Deshalb 4,7 µF. Die Masse von C7 gehört laut TI **direkt an Pin 1**
des LDO, nicht über die Massefläche.

**DAC U2 (GP8413, Adresse 0x58 — A2/A1/A0 = 000)**

| Pin | Signal | Netz |
|---|---|---|
| 1 | SCLK | `SCL_I` |
| 2 | SDA | `SDA_I` |
| 3 | A0 | `PGND` — fest |
| 4 | A1 | `PGND` — fest |
| 5 | VCC | `+12V` |
| 6 | GND | `PGND` |
| 7 | VOUT1 | `S2` |
| 8 | VOUT0 | `S1` |
| 9 | A2 | `PGND` — fest |
| 10 | V5V | `V5V_U2` |
| EP | Heat Slug | `PGND` |

| Bauteil | Anschluss | Netz |
|---|---|---|
| C8 (100 nF) | 1 / 2 | `+12V` / `PGND` |
| C10 (1 µF) | 1 / 2 | `V5V_U2` / `PGND` |

**DAC U3 (GP8413, Adresse 0x59 — A0 = 1)**

Wie U2, abweichend:

| Pin | Signal | Netz |
|---|---|---|
| 3 | A0 | `+5V_ISO` — fest (A1, A2 auf `PGND`) |
| 7 | VOUT1 | `S4` |
| 8 | VOUT0 | `S3` |
| 10 | V5V | `V5V_U3` |

| Bauteil | Anschluss | Netz |
|---|---|---|
| C11 (100 nF) | 1 / 2 | `+12V` / `PGND` |
| C13 (1 µF) | 1 / 2 | `V5V_U3` / `PGND` |

Die Adress-Pins sind **fest verdrahtet**, ohne Lötbrücken: U2 alle drei nach
`PGND` (A2A1A0 = 000 → 0x58), U3 nur A0 nach **`+5V_ISO`** (A2A1A0 = 001 →
0x59) — nicht nach `V5V`. So bleibt der interne LDO unbelastet und O2 spielt
hier keine Rolle mehr.
Damit fallen JP1…JP6 aus der Stückliste. Mehr als zwei Chips sind auf dieser
Platine ohnehin nicht vorgesehen, und die Adressen sind in der Firmware
Konstanten.

**Ausgangsstufe, je Kanal n = 1…4 (Klemmen J3, J4, J5)**

| Bauteil | Anschluss | Netz |
|---|---|---|
| Fn (PTC 0,75 A) | 1 / 2 | `+12V` / `CHn_12V` |
| Cn (**100 nF** X7R 50 V) | 1 / 2 | `Sn` / `PGND` |
| Dn (TVS 12 V unidir.) | K / A | `Sn` / `PGND` |
| Dn+ (BAT54S, **DNP**) | COM / 1 / 2 | `Sn` / `PGND` / `+12V` |
| Klemme ⊕ | | `CHn_12V` |
| Klemme ⊖ | | `PGND` |
| Klemme S | | `Sn` |

**Klemmenbelegung auf der Platine Rev 0.1** (XY303V-3,81-3P, vier Stück):
Pin **1 = 12 V** (CHn_12V), Pin **2 = S** (CHn_S), Pin **3 = GND**
(CHn_PGND). Kanal 1 → J3, Kanal 2 → J4, Kanal 3 → J5, Kanal 4 → J6.

Die Masse ist je Kanal ein **eigenes Netz** CH1_PGND…CH4_PGND, das erst auf
der Platine zusammenläuft. Das ist besser als im Entwurf und entspricht der
LUNOS-Vorgabe, sternförmig von der Steuerung aus zu verkabeln.

**Der BAT54S ist unbestückt.** Ein *unidirektionaler* 12-V-TVS klemmt über
seine eigene Durchlassrichtung auch negativ auf etwa −0,7 V — das war die
Aufgabe der unteren BAT54S-Diode. Die obere klemmt gegen +12 V, der TVS gegen
seine Klemmspannung von rund 13 V; praktisch derselbe Wert. Die Klemmdiode war
also redundant, nicht ergänzend. Das Referenzmodul (Anhang A) schützt jeden
Ausgang ebenfalls nur mit Kondensator und TVS.

Pads bleiben trotzdem im Layout: falls sich in P12 zeigt, dass die
Bestandsleitung mehr einkoppelt als erwartet, ist der BAT54S nachrüstbar.
Den Fall S-gegen-⊖ fängt der Kurzschlussschutz des GP8413 selbst ab.

**Reserveleiste J6** — GP16, GP17, GP18, GP28, GP29, `BCU_GND`.
Sechspolig, 2,54 mm, unbestückt bestellbar. GP18 bleibt als LDAC-Option
reserviert, auch wenn Rev 4 sie nicht braucht.

---

## 5. Stückliste

### 5.1 Halbleiter und aktive Bauteile

| Pos | Ref | Bauteil | Gehäuse | Menge | Bemerkung |
|---|---|---|---|---|---|
| 1 | U1 | ADuM1250ARZ | SOIC-8 | 1 | Pinbelegung prüfen (O5) |
| 2 | U2, U3 | **GP8413-TC25-EW** | ESOP10 | 2 | Footprint selbst zeichnen |
| 3 | U4 | **TLV70450DBVR** (LCSC C91672) | SOT-23-5 (DBV) | 1 | 1 GND, 2 IN, 3 OUT, 4+5 NC · kein EN · V(IN) 2,5…24 V · 150 mA |
| 4 | Q1 | P-MOSFET, V(DS) ≥ −30 V, R(DS,on) ≤ 50 mΩ | SOT-23 | 1 | mit DZ1 unkritisch |
| 5 | DZ1 | Zenerdiode 8,2 V, 250 mW | SOD-323 | 1 | Gate-Klemmung |
| 6 | D1 | TVS unidirektional 15 V, SMBJ15A | SMB | 1 | Eingang |
| 7 | D2…D5 | TVS unidirektional 12 V, SMAJ12A | SMA | 4 | je Kanal, Datenblatt §4 |
| 8 | D6…D9 | BAT54S | SOT-23 | 4 | **DNP** — Pads für den Nachrüstfall |

### 5.2 Passive Bauteile

| Pos | Ref | Wert | Gehäuse | Menge | Netz / Funktion |
|---|---|---|---|---|---|
| 9 | C1 | **100 µF / 25 V** | SMD 6,3 × 7,7 mm | 1 | Eingangspuffer, Bauhöhe siehe 8.1; Wert nach M4 |
| 10 | C2, C8, C11 | 100 nF X7R 50 V | 0603 | 3 | Abblockung +12 V |
| 11 | C3, C4 | 100 nF X7R 50 V | 0603 | 2 | Abblockung U1 |
| 12 | C5, C6 | 1 µF X7R 25 V | 0805 | 2 | VDD2 / LDO-Eingang |
| 13 | C7 | **4,7 µF** X7R 16 V | 0805 | 1 | LDO-Ausgang, Stabilitätsbedingung |
| 14 | C10, C13 | 1 µF X7R 16 V | 0805 | 2 | **V5V, zwingend ≥ 1 µF** |
| 15 | C14…C17 | **100 nF** X7R 50 V | 0603 | 4 | je Ausgang, zwingend |
| 16 | R1 | 100 kΩ 1 % | 0603 | 1 | Gate-Pulldown Q1 |
| 17 | R2, R3 | 4,7 kΩ 1 % | 0603 | 2 | Pull-up sekundär |
| 18 | R4, R5 | 4,7 kΩ 1 % | 0603 | 2 | Pull-up primär, **DNP** |
| 19 | R6 | 2,2 kΩ | 0603 | 1 | LED-Vorwiderstand |
| 20 | F1 | PTC rückstellbar 1,5 A / 30 V | 1206 | 1 | Eingang |
| 21 | F2…F5 | PTC rückstellbar 0,75 A / 30 V | 1206 | 4 | je Kanal |

### 5.3 Steckverbinder, Anzeige, Mechanik

| Pos | Ref | Bauteil | Menge | Bemerkung |
|---|---|---|---|---|
| 22 | J1 | REG1_App_Connector, 10-pol. | 1 | Bauform, Lage **und Bauhöhe** aus OpenKNX-Repo (O7) |
| 23 | J2 | Klemme 2-pol., **RM 3,81** | 1 | 12 V SELV Einspeisung |
| 24 | J3, J4 | Klemme 3-pol., **RM 3,81** | 2 | ⊕ / ⊖ / S — Schlafzimmer, Büro |
| 25 | J5 | Klemme **6-pol.**, RM 3,81 | 1 | Bad: ego Motor 1 + Motor 2 in einem Block |
| 26 | J6 | Stiftleiste 6-pol., RM 2,54 | 1 | Reserve, unbestückt |
| 27 | LED1 | LED grün | 1 | +12 V vorhanden |
| 28 | TP1…TP6 | Prüfpunkt | 6 | S1…S4, +12V, +5V_ISO |

> **Lücken in der Nummerierung sind gewollt.** C9 und C12 (je 10 µF am
> VCC-Pin der DACs) sind bei der Vereinfachung entfallen — das Referenzmodul
> kommt mit 100 nF aus, und C1 am Eingang puffert die Platine. Die Bezeichner
> C10 und C13 behalten trotzdem ihre Nummern, damit Netzliste, Stückliste und
> die drei Schaltplanblätter übereinstimmen. Dasselbe gilt für D6…D9, die als
> DNP im Layout stehen, aber nicht bestückt werden.

### 5.4 Zusammenfassung

Ausgezählt, nicht fortgeschrieben — die Bilanz früherer Fassungen war durch
schrittweises Abziehen falsch geworden.

| Gruppe | Anzahl | Bezeichner |
|---|---|---|
| Kondensatoren | 15 | C1…C8, C10, C11, C13…C17 |
| Widerstände | 4 | R1, R2, R3, R6 |
| PTC-Sicherungen | 5 | F1…F5 |
| Aktive Bauteile | 4 | U1 (Isolator), U2/U3 (DAC), U4 (LDO) |
| Transistor, Zener | 2 | Q1, DZ1 |
| Dioden / TVS | 5 | D1, D2…D5 |
| Steckverbinder | 5 | J1, J2, J3, J4, J5 |
| Anzeige | 1 | LED1 |
| **bestückt** | **41** | |
| DNP / unbestückt | 7 | R4, R5, D6…D9, J6 |
| Prüfpunkte | 6 | TP1…TP6 |
| **Bezeichner gesamt** | **54** | in 28 Stücklistenpositionen |

| | Rev 0 | Rev 3 | **Rev 4** |
|---|---|---|---|
| bestückte Bauteile | 90 | 67 | **41** |
| davon je Kanal | ~14 | 7 | **3** (PTC, 100 nF, TVS) |
| Präzisionsbauteile | 16 | 8 | **0** |
| Stücklistenpositionen | — | — | **28** |

Von den 41 bestückten Teilen sind 19 Kondensatoren und Widerstände aus dem
JLCPCB-Basissortiment, 10 Schutzbauteile (F1…F5, D1…D5), 2 Halbleiter der
Eingangsstufe, 4 aktive Bauteile, 5 Steckverbinder und eine LED. Kein einziges
Präzisionsbauteil, keine Induktivität, kein Abgleichelement.

## 6. Firmware-Schnittstelle

### 6.1 Initialisierung (Reihenfolge zwingend)

```
1. warte 5 ms nach Anlegen der 12 V          (Startzeit < 2 ms + Reserve)
2. für jeden DAC:
     schreibe Register 0x01 = 0x11           (Bereich 0-10 V)
3. für jeden Kanal:
     schreibe Sollwert aus KNX-Objekt oder Sicherheitswert
4. verifiziere durch Rücklesen, falls unterstützt (O6)
```

Punkt 2 ist auch dann nötig, wenn der EEPROM den Bereich sichert — der Schreib-
vorgang ist idempotent und kostet nichts.

### 6.2 Sollwertausgabe

```c
// 15-Bit-Code aus Zielspannung und Stufenstrom
uint16_t kwl_code(float v_soll, float i_stufe, float r_leiter, float k_gain)
{
    float v = (v_soll + i_stufe * r_leiter) * k_gain;   // k_gain: Kalibrierung
    if (v < 0.0f)  v = 0.0f;
    if (v > 10.0f) v = 10.0f;
    int32_t c = (int32_t)(v * 3276.7f + 0.5f);
    if (c > 0x7FFF) c = 0x7FFF;
    return (uint16_t)c;
}

// Abbildung logischer Code -> Busformat. LINKSBUENDIG ist ein
// Konfigurationsschalter, kein Compilerschalter: erst nach P6 festnageln.
static inline uint16_t kwl_wire(uint16_t code)
{
    return LINKSBUENDIG ? (uint16_t)(code << 1) : code;
}

// Einzelkanal
uint16_t w = kwl_wire(code);
buf[0] = (kanal == 0) ? 0x02 : 0x04;
buf[1] = w & 0xFF;               // Low  Byte zuerst
buf[2] = (w >> 8) & 0xFF;
i2c_write_blocking(i2c1, addr, buf, 3, false);

// Doppelschreiben für den ego (beide Motoren in einem Frame)
uint16_t w1 = kwl_wire(code_m1), w2 = kwl_wire(code_m2);
buf[0] = 0x02;
buf[1] = w1 & 0xFF;  buf[2] = (w1 >> 8) & 0xFF;
buf[3] = w2 & 0xFF;  buf[4] = (w2 >> 8) & 0xFF;
i2c_write_blocking(i2c1, addr, buf, 5, false);
```

**Achtung beim Doppelschreiben:** `DFRobot_GP8XXX::sendData` füllt für
Kanal 2 den Puffer mit `{lo, hi, lo, hi}`, schreibt also **denselben** Wert in
beide Ausgänge. Für den ego brauchen die zwei Motoren in den Stufen 1 bis 3
verschiedene Werte; nur in Stufe 4 sind sie gleich. Die Bibliotheksfunktion ist
dafür also unbrauchbar, das Register aber sehr wohl — vier Datenbytes mit zwei
unterschiedlichen Werten, wie oben.

### 6.3 Zu parametrierende Größen je Kanal

| Parameter | Bereich | Zweck |
|---|---|---|
| Gerätetyp | e²60 / ego / RA / unipolar | wählt die Kennlinientabelle |
| Chiptyp | GP8413 / GP8403 | 15 oder 12 Bit |
| Leitungslänge | 0…30 m | R(Rückleiter) = Länge × 0,025 Ω/m |
| Kabelquerschnitt | 0,5 / 0,7 / 1,0 / 1,5 mm² | ersetzt den festen Faktor |
| Verstärkungskorrektur k | 0,98…1,02 | Kalibrierung des 0,2-%-Skalenfehlers |
| Totband | 0…200 mV | um 5,00 V, aus Messung M3 |
| Einschaltwert | AUS / Stufe 1…4 | in den EEPROM zu schreiben |

### 6.4 Store-Implementierung

Ablauf, umzusetzen als eigene Funktion mit Bitbang auf GPIO2/GPIO3:

```
store_einschaltwert():
    für jeden DAC d in [U2, U3]:
        schreibe Sollwerte(d)                  // normales I2C, gewünschter Zustand
    i2c_deinit(i2c1)
    gpio_set_function(2, SIO); gpio_set_function(3, SIO)   // GPIO2 = SDA, GPIO3 = SCL
    für jeden DAC d in [U2, U3]:               // sequenziell, nie verschachtelt
        bb_start(); bb_bits(0b010, 3, kein_ack); bb_stop()
        bb_start(); bb_byte(0x10); bb_byte(0x03); bb_stop()
        bb_start(); bb_byte(adr(d) << 1); 8 × bb_byte(0x00); bb_stop()
        sleep_ms(10)
        bb_start(); bb_bits(0b010, 3, kein_ack); bb_stop()
        bb_start(); bb_byte(0x10); bb_byte(0x00); bb_stop()
    gpio_set_function(2, I2C); gpio_set_function(3, I2C)
    i2c_init(i2c1, 400000)
```

**Die Pins sind GPIO2 und GPIO3**, nicht GP26/GP27 wie in früheren Fassungen
dieses Dokuments — auf der Platine Rev 0.1 liegt I2C1 dort.
Halbwellendauer 2 bis 5 µs. Die Bitbang-Routinen müssen SDA als
Open-Drain behandeln (Pin auf Eingang statt aktiv High), sonst treibt der
RP2040 gegen die Pull-ups des Isolators — die DFRobot-Referenz macht das
`digitalWrite(HIGH)` bewusst nur, weil Arduino dort einen schwachen Treiber
hat, und schaltet für das ACK-Lesen auf `INPUT_PULLUP` um.

### 6.5 Sperre gegen EEPROM-Verschleiß

Die EEPROM-Sequenz darf **nur** durch einen expliziten KNX-Befehl oder ein
Inbetriebnahme-Kommando ausgelöst werden, nie im normalen Sollwertpfad. In der
Applikation als eigener Parameter „Einschaltwert speichern" mit Einmalwirkung
umsetzen. Zusätzlich ein Zähler im Flash des RP2040, der die Zahl der
ausgeführten Store-Vorgänge mitschreibt — solange der Hersteller keine
Zyklenzahl angibt, ist das die einzige Möglichkeit, das Risiko überhaupt zu
beobachten.

### 6.6 Was die vorhandenen Bibliotheken taugen

| Quelle | Nutzen | Vorsicht |
|---|---|---|
| `DFRobot/DFRobot_GP8XXX` | **die maßgebliche Quelle.** Deckt den GP8413 ausdrücklich ab (DFR1073), hat `store()`, liefert alle Store-Konstanten (HEAD 0x02, ADDR 0x10, CMD1 0x03, CMD2 0x00, Delay 10 ms) und die Bitbang-Zeiten (1 µs / 2 µs / 5 µs, ~125 kHz). `begin()` ist eine reine Adressabfrage ohne Nutzdaten. | Schiebt 15-Bit-Werte um ein Bit nach links → O17; `_scl`/`_sda` auf den Arduino-Standardpins festgenagelt; Kanal-2-Doppelschreiben nur mit identischen Werten |
| `DFRobot/DFRobot_GP8403` | ältere Einzelbibliothek, dieselbe Store-Sequenz | nur GP8403, 12-Bit-Format `code << 4`; `setDACOutVoltage` teilt durch eine Variable, die erst `setDACOutRange` setzt — ohne diesen Aufruf Division durch Null |
| `knifter/lib-GP8413` | bestätigt 15 Bit, 0…32767, Adresse 0x58, Halbskala ≈ 16383 für 5 V im 10-V-Bereich | **kein `store()`** — die EEPROM-Funktion fehlt vollständig |
| `h-ebel/GP8413_Raspberry` | belegt, dass für den Normalbetrieb einfache Schreibbefehle ohne Bibliothek genügen | Beispiel mit fest übersetzter Spannung, keine Store-Funktion |

Für Rev 4 heißt das: die Sollwertausgabe aus 6.2 selbst schreiben (drei bzw.
fünf Bytes, das ist keine Bibliothek wert), die Store-Sequenz aus der
DFRobot-Referenz übernehmen und auf Bitbang für GPIO2/GPIO3 umschreiben.

---

## 7. Prüfplan Inbetriebnahme

| # | Schritt | Erwartung |
|---|---|---|
| P1 | 12 V anlegen, kein I²C | LED1 leuchtet, I(gesamt) ≤ 15 mA ohne Last |
| P2 | Spannung an V5V_U2, V5V_U3 | 5,0 V ±0,2 V |
| P3 | Spannung `+5V_ISO` | 5,0 V ±0,1 V |
| P4 | Isolationsprüfung `BCU_GND` ↔ `PGND` | > 10 MΩ |
| P5 | Bus anstecken, I²C-Scan | 0x58 und 0x59 antworten |
| P6 | **Formatentscheidung (O17):** Register 0x01 = 0x11, dann Kanal 1 nacheinander mit `0x4000` und mit `0x8000` beschreiben, jeweils TP1 messen | Genau einer der beiden Werte ergibt 5,00 V ±25 mV. Ergebnis legt `LINKSBUENDIG` fest. **Vor allen weiteren Schritten.** |
| P6a | Gewählte Variante auf alle vier Kanäle, logischer Code 16384 | TP1…TP4 = 5,00 V ±25 mV |
| P7 | Logischer Code 0 und 32767 je Kanal | 0,00 V / 10,00 V ±30 mV, Skalenfehler notieren → k |
| P8 | Nachbarkanalübersprechen: Kanal 1 auf 0x7FFF, Kanal 2 auf 0x4000 | Abweichung Kanal 2 < 5 mV |
| P9 | Kanal gegen PGND kurzschließen | Chip geht in Schutzmodus, kein Schaden, andere Kanäle unbeeinflusst |
| P10 | EEPROM mit 0x4000 beschreiben, 12 V aus/ein, ohne I²C messen | 5,00 V an allen vier Kanälen |
| P11 | **Bereichsbit prüfen:** EEPROM im 0-10-V-Modus mit 0x4000 beschreiben, Kaltstart, ohne Register-0x01-Schreibvorgang messen | 5,00 V. Werden 2,50 V gemessen, ist das Bereichsbit **nicht** gesichert → O1 |
| P12 | Lüfter anschließen, Stufen 1…4 durchfahren, Strom messen | Sollwerttabelle aus 3.4, Kompensation prüfen |
| P13 | KNX-Bus abziehen, 12 V bleibt | Lüfter laufen mit letztem Sollwert weiter |
| P14 | Dauerlauf 24 h, Temperatur an U2/U3 | ΔT < 25 K über Umgebung |
| P15 | **Store-Sequenz durch den ADuM1250:** Bitbang-Sequenz aus 6.4 fahren, mit Logikanalysator auf **beiden** Seiten des Isolators mitschneiden | Die drei-Bit-Frames und die Frames an `0x10` kommen sekundärseitig unverfälscht an; anschließend P10 erfolgreich |

**P11 und P15 sind die kritischen Schritte.** P11 entscheidet, ob der EEPROM
inhaltlich taugt, P15, ob er über den Isolator überhaupt erreichbar ist.
Scheitert P15, gibt es drei Auswege: einen anderen Isolator (Optokoppler mit
getrennten Richtungen statt bidirektionalem I²C-Buffer), Verzicht auf die
galvanische Trennung, oder Verzicht auf den EEPROM samt Rückfall auf eine
Hardwaremaßnahme für den Einschaltzustand. **Deshalb P15 vor dem Layout auf
einem Steckbrettaufbau prüfen, nicht erst auf der fertigen Platine.**

---

## 8. Layout

### 8.1 Mechanische Hülle und Floorplan

**Vorgabe:** 4TE-REG-Gehäuse, Platine **64 mm breit × 82 mm hoch**.

| Größe | Wert | Bemerkung |
|---|---|---|
| Außenmaß Platine | 64,0 × 82,0 mm | |
| Führungsnut-Freihaltung | je 1,5 mm links und rechts | bauteilfreier Rand, nur Kupfer nach Bedarf |
| nutzbare Breite | **61 mm** | maßgeblich für alles Folgende |
| Gesamtfläche | 5248 mm² | nutzbar ~5000 mm² |
| Fläche für Elektronik | ~4270 mm² | nach Abzug eines 12-mm-Klemmenstreifens |
| Flächenbedarf der Bestückung | ~1200 mm² | reichlich Reserve, zweilagig genügt |

Die Platine ist für 28 Positionen großzügig. Der Platz geht nicht an die
Bauteile, sondern an **Klemmen und Isolationsabstand** — und dort wird es
tatsächlich knapp.

#### Die Klemmenrechnung entscheidet über das Rastermaß

Vier Klemmenblöcke, 14 Pole:
J2 (12 V, 2-pol) + J3 (Schlafzimmer, 3-pol) + J4 (Büro, 3-pol) +
**J5 (Bad, 6-pol)**.

> **Änderung gegenüber der Stückliste in 5.3:** Der ego braucht sechs Adern in
> denselben Raum. Ein einziger 6-poliger Block für das Bad ist sauberer als
> zwei 3-polige — eine Leitung, eine Klemme. Die bisherige J6 entfällt, die
> Reserveleiste wird zu **J6**.

| Rastermaß | Klemmenlänge, 14 Pole | Rest von 61 mm |
|---|---|---|
| 5,08 mm | **71,1 mm** | **−10,1 mm — passt nicht** |
| 3,81 mm | 53,3 mm | 7,7 mm |
| 3,50 mm | 49,0 mm | 12,0 mm |

**Empfehlung: RM 3,81 mm** (z. B. Phoenix MC 1,5/x-G-3,81 oder Wago 2060).
Bemessen für 1,5 mm² und ~8 A, bei 0,41 A je Kanal weit auf der sicheren
Seite. 5,08 mm ist für 0,7-mm²-Adern ohnehin überdimensioniert und scheitert
hier an der Kantenlänge.

**Warum das nicht bloß Geschmack ist:** Bei 5,08 mm müssten die Klemmen auf
beide Querkanten verteilt werden (8 Pole unten = 40,6 mm, 6 Pole oben =
30,5 mm). Dann liegt Lastpotenzial an **beiden** Kanten, und die KNX-Zone
müsste als senkrechte Spalte an einen Längsrand ausweichen. Für den
Isolationsspalt bleiben dann bei 61 mm nutzbarer Breite nur noch etwa 42 mm
für die Lastzone — und dort sollen 40,6 mm Klemme unterkommen. Das geht nicht
sauber auf. Mit 3,81 mm liegen alle 14 Pole an **einer** Kante, und die
waagerechte Zonentrennung aus 8.2 bleibt erhalten.

#### Floorplan, Variante A (bevorzugt, J1 an der oberen Kante)

Ursprung unten links, Maße in mm:

```
 y=82 ┌────────────────────────────────────────────┐ 64 mm
      │  J1  REG1_App_Connector · C3 · R4/R5(DNP)  │  KNX-Zone
 y=70 │  U1 ADuM1250 (primär)      BCU_GND         │  12 mm
      ├────────────────────────────────────────────┤
 y=66 │      Isolationsspalt, alle Lagen, ≥ 4 mm   │
      ├────────────────────────────────────────────┤
      │  U1 (sekundär) · R2/R3 · C4/C5             │
 y=56 │  U4 LDO · C6 · C7          +5V_ISO         │
      │                                            │
      │  U2 GP8413 0x58        U3 GP8413 0x59      │  Lastzone
 y=40 │  C8 · C10              C11 · C13           │  ~52 mm
      │                                            │
      │  J2-Einspeisung: F1 · Q1 · DZ1 · R1        │
 y=26 │  D1 · C1 · C2 · LED1/R6    ★ Sternpunkt    │
      │                                            │
      │  Ausgangsstufen 1-4: F2-F5 · C14-C17       │
 y=14 │  D2-D5 · D6-D9 · TP1-TP6                   │
      ├────────────────────────────────────────────┤
  y=0 │ [J2 2p][J3 3p][J4 3p][  J5 6p Bad  ] RM3,81│  Klemmenkante
      └────────────────────────────────────────────┘
       x=0                                      x=64
```

Der Isolationsspalt läuft waagerecht bei y ≈ 66…70 mm quer über die ganze
Breite, unter U1 durch, in **allen** Lagen. Bei 82 mm Höhe kosten 4 mm Spalt
nur 5 % der Fläche — es lohnt, auf 6 mm zu gehen, falls J1 es zulässt.

#### Floorplan, Variante B (Rückfall, J1 in der Blattmitte)

Sitzt J1 nicht an einer Kante, muss der Spalt um J1 herumgeführt werden: KNX-
Zone als **U-Form** um J1, Spalt als Kontur mit ≥ 4 mm Abstand, Lastzone im
Rest. Das ist zeichnerisch unangenehm, aber bei 5000 mm² machbar. Die Klemmen
bleiben in beiden Varianten an der unteren Kante.

#### Bauhöhe — neuer Engpass

Zwei Platinen übereinander im 4TE-Gehäuse. Der Abstand zwischen Basis- und
Applikationsplatine ergibt sich aus der Bauform von J1 und ist unbekannt
(O7). Kritisch ist ein Bauteil:

| Bauteil | Höhe | Bewertung |
|---|---|---|
| C1 als 470 µF/25 V, SMD-V-Chip 10 × 10,5 mm | **~10,5 mm** | Grund für die Verkleinerung auf 100 µF |
| Klemmen RM 3,81 | ~10…12 mm | muss zum Frontausschnitt passen |
| U2/U3 ESOP10 | 1,65 mm | unkritisch |
| J1 | unbekannt | O7 |

**C1 verkleinern.** Es gibt in Rev 4 keinen Schaltregler mehr, C1 puffert nur
den Eingang gegen die Leitungsinduktivität und den Einschaltstromstoß.
**100 µF/25 V genügen**, in 6,3 × 7,7 mm oder als 2 × 47 µF/25 V in 6,3 × 5,8 mm
flach nebeneinander. Endgültig nach Messung M4 (Einschaltstromspitze).

#### Was jetzt noch fehlt

Mit 64 × 82 mm ist O7 zur Hälfte erledigt. Offen bleiben:

- **Position und Bauform von J1** auf diesem Umriss — entscheidet zwischen
  Variante A und B
- **Plattenabstand** Basis ↔ Applikation — entscheidet über C1 und die
  Klemmenhöhe
- **Lage der Frontausschnitte** des Gehäuses — muss zur Klemmenkante passen
- **Halterung:** Nutführung oder Schrauben? Bei Nut die 1,5-mm-Ränder
  freihalten, bei Schrauben Bohrungen und Keep-outs ergänzen

### 8.2 Zonen

Drei Bereiche, im 64 × 82-Umriss **waagerecht** übereinander (Variante A aus
8.1):

```
 y=82 ┌────────────────────────────────────────────┐
      │  KNX-Zone        J1, U1 primär, C3,        │
      │                  R4/R5, Netz BCU_GND       │
 y=70 ├────────────────────────────────────────────┤
      │  Isolationsspalt ≥ 4 mm, keine Kupferlage, │
 y=66 │  kein Massepour, in ALLEN Lagen            │
      ├────────────────────────────────────────────┤
      │  Lastzone        U1 sekundär, U2, U3, U4,  │
      │                  Eingangsschutz, J2,       │
  y=0 │                  Ausgangsstufen, J3…J5     │
      └────────────────────────────────────────────┘
```

Der Spalt läuft unter U1 durch, in **allen** Lagen, ohne Ausnahme für
Massepolygone oder Silkscreen-Füllungen. Vier Millimeter sind für SELV
funktional ausreichend; bei 82 mm Bauhöhe kosten sechs Millimeter fast nichts
und sind die bessere Wahl, sofern J1 sie zulässt.

### 8.3 Masseführung in der Lastzone

Es gibt nur noch **eine** Masse (`PGND`), aber der Lüfterstrom von bis zu 1 A
darf nicht über den Referenzpfad der DACs laufen. Regel:

1. `PGND` als durchgehendes Polygon auf der Innen- oder Rücklage.
2. Der Minuspol von C1 ist der **Sternpunkt**. Von dort ausgehend:
   Klemmen-⊖ (J2.2, J3.2…J6.2) auf der einen Seite, GND von U2/U3/U4 auf der
   anderen. Die Lüfterströme fließen dann nicht über den Chip-GND-Anschluss.
3. Adressleitungen A0/A1/A2 als kurze Stichleitungen direkt am Pin nach `PGND`
   bzw. `V5V` — keine Brücken, keine Durchkontaktierung quer über die Platine.
4. Heat Slug von U2 und U3 mit mindestens vier Vias auf `PGND`, direkt am Pad,
   nicht über eine Zuleitung.
4. C8/C11 (100 nF) innerhalb von 2 mm an Pin 5, C10/C13 (1 µF) innerhalb von
   2 mm an Pin 10.
5. C14…C17 (100 nF am Ausgang) direkt an den Ausgangspins, **vor** PTC und
   Klemme, damit sie die Leitungsinduktivität sehen.
6. Masse von C7 mit einer eigenen kurzen Bahn **direkt an Pin 1 von U4**, nicht
   über das Polygon. TI empfiehlt für IN und OUT getrennte Masseflächen, die
   sich nur am GND-Pin treffen — auf dieser Platine reicht die kurze
   Direktverbindung.

### 8.4 Leiterbahnen

| Netz | Breite | Bemerkung |
|---|---|---|
| `+12V_RAW`, `+12V`, `PGND` (Hauptpfad) | ≥ 1,0 mm | 1 A Gesamtlast |
| `CHn_12V` | ≥ 0,5 mm | 0,41 A je Kanal |
| `S1`…`S4` | 0,25 mm | Kleinsignal, kurz halten, nicht parallel zu `CHn_12V` führen |
| `SDA_I`, `SCL_I` | 0,25 mm | von den Ausgangsleitungen fernhalten |

### 8.5 Verlustleistung

| Bauteil | Verlust | Bemerkung |
|---|---|---|
| U2, U3 | je ≈ 60 mW bei 12 V / 5 mA | unkritisch, Heat Slug reicht deutlich |
| U4 | (12 − 5) V × ~10 mA = 70 mW | bei R(θJA) = 213 K/W etwa 15 K Übertemperatur — unkritisch; Pin 4 und 5 an PGND verbessern es weiter |
| Q1 | 1 A² × 50 mΩ = 50 mW | unkritisch |
| F1 | ≈ 100 mW bei 1 A | Platz um das Bauteil freihalten |

Kein Kühlkörper, kein Kupferfeld über das Nötige hinaus.

---

## 9. Offene Punkte

### 9.1 Blocker

| # | Punkt | Wirkung | Weg |
|---|---|---|---|
| **O17** | **Busformat des 15-Bit-Werts: rechtsbündig oder um 1 geschoben?** | Faktor zwei auf jede Ausgangsspannung. Aus 5,00 V wird 2,50 V oder 10,00 V — letzteres ist Volllast. Betrifft Sollwerte, Kabelkompensation und den EEPROM-Wert gleichermaßen. | Prüfschritt P6 an einem DFR1073-Breakout, zwei Schreibvorgänge und ein Voltmeter. **Vor dem Layout, vor P11.** |
| **O7** | **Position, Bauform und Bauhöhe von J1** auf dem 64 × 82-Umriss | Umriss ist gesetzt (4TE, 64 × 82 mm). Offen bleibt J1: Kante oder Blattmitte entscheidet zwischen Floorplan A und B; die Bauhöhe entscheidet über C1 und die Klemmenwahl. | OpenKNX-Repository `HW-REG1-Base`, Mechanikzeichnung oder KiCAD-Board |
| **O15** | Lage der Frontausschnitte des 4TE-Gehäuses | muss zur Klemmenkante bei y = 0 passen; sitzen die Öffnungen an beiden Querkanten, ist auch die 5,08-Variante wieder denkbar | Gehäusedatenblatt |
| **O16** | Halterung: Nutführung oder Verschraubung? | bei Nut die 1,5-mm-Ränder bauteilfrei halten, bei Schrauben Bohrungen und Keep-outs ergänzen | Gehäusedatenblatt |

### 9.2 Vor der Bestellung

| # | Punkt | Warum |
|---|---|---|
| **O1** | Sichert der EEPROM auch Register 0x01 (Bereichswahl)? | Startet der Chip im 0-5-V-Modus mit dem für 10 V gespeicherten Halbausschlag, liegen 2,50 V an S — beim e²60 knapp unter Stufe 2 in Richtung A, also **nicht** Stillstand. **Indiz aus `DFRobot_GP8XXX`:** dort ist `store()` für den GP8413 ausdrücklich vorgesehen, und die Sequenz überträgt achtmal `0x00` als Nutzdaten — der Chip speichert danach trotzdem den zuvor gesetzten Ausgangswert. Der Befehl kopiert also den lebenden Registersatz ins EEPROM; wenn Register 0x01 dazugehört, wird der Bereich mitgesichert. Bewiesen ist das nicht. Prüfschritt P11. |
| **O2** | Belastbarkeit von V5V (Pin 10) | nicht spezifiziert, und das Referenzmodul belastet den Pin ebenfalls nicht — es hängt nur ein Kondensator daran. Für die Adresspins ist die Frage durch `+5V_ISO` umgangen. Offen bleibt sie nur für die Sekundärseite des ADuM1250: wäre V5V mit ≥ 10 mA belastbar, entfielen U4, C6 und C7. Bis dahin bleibt U4. |
| ~~O3~~ | ~~Widerspruch Ausgangskondensator~~ | **geklärt: 100 nF.** Die Pintabelle des GP8413 nennt 0,1 µF, und das Referenzmodul (Anhang A) bestückt an beiden Ausgängen 100 nF. Nebeneffekt: geringere kapazitive Last am unbuffered Ausgang. An V5V bleibt es bei 1 µF — dort fordern beide Datenblätter ausdrücklich ≥ 1 µF, auch wenn das Modul nur 100 nF verbaut. |
| ~~O4~~ | ~~Pin 9: A2 oder SEL?~~ | **geklärt: A2.** Das Referenzmodul (Anhang A) führt Pin 9 über einen DIP-Schalter mit 4k7-Pull-down als Adressbit heraus. Ein ausgeliefertes Produkt wiegt schwerer als das erkennbar fremde Applikationsbild in §3.1/3.2. Die Bereichswahl läuft ausschließlich über Register 0x01. |
| ~~O5~~ | ~~Pinbelegung ADuM1250ARZ~~ | **geklärt an der Platine Rev 0.1:** 1 VDD1, 2 SDA1, 3 SCL1, 4 GND1, 5 GND2, 6 SCL2, 7 SDA2, 8 VDD2. Offen bleibt nur die Bestätigung, dass VDD1 bei 3,3 V spezifiziert ist — ein Blick ins Datenblatt, nachdem uns die TLV702/TLV704-Verwechslung schon erwischt hat. |
| **O6** | Ist der GP8413 rücklesbar? | Datenblatt zeigt nur Schreibvorgänge, und keine der vier geprüften Bibliotheken liest zurück. Als praktisch geklärt zu behandeln: nicht rücklesbar. Damit keine Verifikation in P6/P7 außer per Voltmeter und keine Selbstdiagnose im Betrieb. Als Ersatz die Adressabfrage aus `DFRobot_GP8XXX::begin()` — Startbedingung, Adressbyte, ACK auswerten, Stopp, ohne jedes Nutzbyte. Prüft die Anwesenheit des Chips, nicht den Registerinhalt. |
| **O8** | LCSC-Nummer GP8413-TC25-EW, JLCPCB-bestückbar? | Wenn nicht: von Hand löten oder GP8403 nehmen. Kann die Bestellung aufhalten. |
| **O9** | ESOP10-Footprint | 10 Pins bei 1,0 mm Raster mit Exposed Pad ist keine Standard-SOIC-Variante. Wahrscheinlich selbst zu zeichnen, Maße vollständig in §8 beider Datenblätter. |
| **O10** | Elko 100 µF / 25 V in 6,3 × 7,7 mm bei JLCPCB bestückbar? | Bauhöhe ist die Randbedingung, nicht die Kapazität — siehe 8.1 |
| **O11** | Busbudget J1 Pin 9 | ADuM1250 VDD1 zieht ~1 mA. Mit R4/R5 unbestückt bleibt es dabei. |
| ~~O12~~ | ~~TLV70450: V(in,max)~~ | **geklärt: 2,5…24 V, Absolutmaximum 24 V.** Bei 12 V nominal ausreichend Luft. Nur falls das Netzteil im Leerlauf über 24 V gehen könnte, wäre ein 36-V-Typ nötig — beim 5/NT18 nicht zu erwarten. |
| **O18** | KiCAD-Symbol für den TLV70450DBV | `Regulator_Linear:TLV70225_SOT23-5` passt **nicht** (hat EN). Ein Symbol mit GND 1 / IN 2 / OUT 3 / NC 4 / NC 5 suchen oder selbst anlegen. Ein dreipoliges Generikum passt nicht auf den SOT-23-5-Footprint. |
| **O13** | **Überträgt der ADuM1250 die nichtstandardkonforme Store-Sequenz?** | Drei-Bit-Frames ohne ACK, Frames an eine reservierte Adresse. Der Isolator sollte als transparenter Open-Drain-Puffer alles durchlassen, bewiesen ist es nicht. Prüfschritt P15, **vor dem Layout**. Fällt er durch, ändert sich die Architektur. |
| **O14** | Ist während der Store-Sequenz ein Ausgangssprung zu erwarten? | Der Chip verarbeitet gerade eine Brennsequenz; ob VOUT dabei stabil bleibt, sagt kein Datenblatt. Bei P10/P15 mitmessen. Falls es Sprünge gibt: Store nur bei stehenden Lüftern ausführen, in der Applikation entsprechend verriegeln. |

### 9.3 Am Gerät zu messen (unverändert aus Rev 3)

| # | Messung | Warum |
|---|---|---|
| **M1** | **Polarität: bedeutet < 5 V Zuluft oder Abluft?** | Quellen widersprechen sich. Geht direkt in die Kennlinientabellen aus 3.4. |
| **M2** | Stromaufnahme je Stufe, 4 Stufen × 2 Typen | Kabelkompensation, und Entscheidung zum Anschlagproblem in 3.5 |
| **M3** | Breite des Totbands um 5,00 V | Abnahmebedingung des Fehlerbudgets 3.3 |
| **M4** | Einschaltstromspitze | PTC-Wahl F1…F5 |
| **M5** | *neu:* Eingangsstrom des S-Anschlusses | in Rev 4 nicht mehr genauigkeitsrelevant, aber der DAC muss die Last treiben können |

**Messaufbau zu M1, M2, M5:** Lüfter an 12 V, Signal von einem Labornetzteil
0 → 10 V fahren, Strom in beiden Zweigen protokollieren, Blatt Papier vor die
Innenblende halten. Eine halbe Stunde, klärt drei Punkte.

### 9.4 Bauliche Blocker (unverändert)

Das Bad braucht **sechs Adern** (ego = zwei Kanäle × ⊕/⊖/S). Die vorhandene
dreiadrige Leitung reicht dort nicht; da renoviert wird, neu ziehen. Wenn die
Wand offen ist, **vierte Ader auch in Büro und Schlafzimmer** — dann entfällt
die Firmware-Kompensation samt Restfehler von ±20…30 mV. Rev 4 unterstützt das
ohne Schaltungsänderung: die vierte Ader wird einfach als zweiter ⊖-Anschluss
an derselben Klemme geführt und die Kompensation in der Firmware auf 0 gesetzt.

---

## 10. Nächste Schritte

1. **O7-Rest klären** — Position, Bauform und Bauhöhe von J1 auf dem
   64 × 82-Umriss. Entscheidet Floorplan A gegen B und die Bauhöhe von C1.
2. **Messungen M1…M4** am e²60 durchführen. Eine halbe Stunde, klärt die
   Kennlinienrichtung, das Fehlerbudget und über M4 auch den Wert von C1.
3. **P6 zuerst** — die Formatfrage O17 an einem DFR1073-Breakout klären.
   Register 0x01 auf 0x11, dann `0x4000` und `0x8000` schreiben und messen.
   Fünf Minuten, und sie entscheidet über jede Zahl in Abschnitt 3.4.
4. **P11 und P15 vorziehen** — beides auf einem Steckbrettaufbau, bevor
   irgendetwas gezeichnet wird: RP2040 (Pico genügt) → ADuM1250 → GP8413-Breakout
   (DFR1073) an 12 V. Store-Sequenz bitbangen, mit Logikanalysator beidseitig
   mitschneiden, kaltstarten, VOUT messen. Zwei Abende Arbeit, und danach steht
   fest, ob die Architektur trägt.
5. **Schaltplan neu zeichnen** — nicht Rev 3 ändern. Es entfallen so viele
   Baugruppen, dass Ändern aufwendiger wäre als neu zeichnen.
6. ESOP10-Footprint anlegen, aus der Maßtabelle in §8 der Datenblätter.
7. Stückliste als `Stueckliste_LUNOS-KNX_Rev4.xlsx` mit LCSC-Nummern
   vervollständigen.

### 10.1 KiCAD-Arbeitsregeln (Konnect MCP v0.11.0)

Aus Rev 0 bis Rev 3 mühsam erarbeitet, gilt weiter:

1. **Nur ein Toolset pro Zug laden.** Nach einem `load_toolset` ist nur der
   erste Aufruf verlässlich.
2. **Batches auf acht bis zehn Bauteile begrenzen.** Rev 4 hat 28 Positionen,
   also drei Züge.
3. Einstellungsänderungen greifen erst nach vollständigem Clientneustart;
   `server_stats` zeigt an `started_at_ms`, ob der Prozess wirklich neu ist.
4. Nach `create_project` überschreibt `create_schematic` keine bestehende Datei.

---

## Anhang A — Quellen

| Dokument | Version | Verwendete Abschnitte |
|---|---|---|
| GP8403 Datenblatt | GP8403-CN-V1.13 | §1 Pins, §2 Grenzwerte, §3.3.3–3.3.7 Protokoll, §4 Funktion, §5/6 Kenndaten, §7 Bestellcode, §8 Gehäuse |
| GP8413 Datenblatt | GP8413-CN-V1.13 | ebenso |
| LUNOS 5/SC-FT | v5.14, SN 055xxx | Sollwerte der Stufentabellen |
| Projektübergabe | 09.09.2026 | Bauliche Gegebenheiten, Gerätewahl, Rev 0–3 |
| TI TLV704 Datenblatt | SBVS148D, Jan 2015 | Pinbelegung DBV (1 GND, 2 IN, 3 OUT, 4+5 NC), V(IN) 2,5…24 V, 150 mA, C(OUT) ≥ 1 µF als Stabilitätsbedingung, Rückstrom über die Body-Diode, R(θJA) 213 K/W, Layouthinweis zur Masse des Ausgangskondensators |
| **`DFRobot/DFRobot_GP8XXX`** | v1.1.0, 2025-07-04, MIT | GP8413 ausdrücklich unterstützt; `RESOLUTION_15_BIT` = 0x7FFF mit `code << 1` beim Senden (→ O17); Store-Konstanten HEAD 0x02 / ADDR 0x10 / CMD1 0x03 / CMD2 0x00 / Delay 10 ms; Bitbang-Zeiten `I2C_CYCLE_BEFORE` 1 µs, `AFTER` 2 µs, `TOTAL` 5 µs; `begin()` als reine Adressabfrage |
| `DFRobot/DFRobot_GP8403` | master, MIT | Registerkonstanten (0x01 Bereich, 0x02 Kanal 0, 0x02<<1 Kanal 1), Bereichswerte 0x00/0x11, Store-Sequenz identisch, 12-Bit-Format `code << 4` |
| `knifter/lib-GP8413` | main | Bestätigung 15 Bit / 0…32767 / Adresse 0x58 / Halbskala 16383 ≈ 5 V im 10-V-Bereich; kein Store |
| `h-ebel/GP8413_Raspberry` | main | Belegt, dass der Normalbetrieb mit einfachen Schreibbefehlen ohne Bibliothek auskommt |
| **Referenzmodul GP8413** (Schaltplan eines Fremdmoduls, 10.09.2026) | — | Pin 9 = A2 (Adressbit am DIP-Schalter mit 4k7-Pull-down), Adressbits an der externen Logik-VCC statt an V5V, Ausgangsschutz nur aus 100 nF plus TVS, 100 nF an VCC ohne Elko |

Die Bibliotheken sind hier als **Protokollquelle** benutzt, nicht als Code-
Vorlage. Für den Normalbetrieb wird selbst geschrieben (6.2), aus der
DFRobot-Referenz wird ausschließlich die Store-Sequenz übernommen (6.4).

## Anhang B — Widersprüche in den Datenblättern

Beim Lesen aufgefallen, für die Bewertung künftiger Angaben relevant:

1. **Ausgangskondensator** — Pintabelle und §4 widersprechen sich in beiden
   Dokumenten, und zwar gegenläufig (O3).
2. **Applikationsbilder §3.1/3.2** — beschriften Pins mit NC, NC, PWM, PWM, VCC
   und zeigen an Pin 9 „SEL"; das passt zu keinem der beiden Chips und stammt
   erkennbar aus einem anderen Datenblatt (O4).
3. **Linearität GP8413** — Merkmalsliste 0,02 %, DC-Tabelle 0,1 %.
4. **Ausgangsfehler GP8403** — Merkmalsliste 0,2 % typ., DC-Tabelle 0,5 % max.,
   §4 nennt 0,5 % als Vorgabewert.
5. **Abschnittsnummerierung GP8413** — §3.3.6 ist zweimal vergeben
   (Bereichswahl und EEPROM).
6. **§3.3.5 beider Dokumente** — schreibt beim zweiten Kanal
   `VOUT0=DATA1/…` statt `VOUT1=DATA1/…`. Offensichtlicher Tippfehler; das
   GP8403-Dokument korrigiert sich einen Satz später selbst.

7. **Datenformat 15 Bit** — §3.3.3 zeichnet den Wert rechtsbündig mit Bit 15
   = 0, die Herstellerbibliothek `DFRobot_GP8XXX` schiebt ihn um ein Bit nach
   links. Faktor zwei Unterschied, sicherheitsrelevant, siehe O17.
8. **Pin 9** — die Pintabelle nennt A2, die Applikationsbilder SEL, die
   DC-Tabelle des GP8403 beschreibt eine SEL-Funktion. Durch das Referenzmodul
   zugunsten von A2 entschieden (O4).

Die Datenblätter sind brauchbar, aber nicht sorgfältig. Bei jeder Angabe, die
in den Entwurf eingeht, gilt: **gemessen vor Bibliothek, Bibliothek vor
Pintabelle und DC-Tabelle, diese vor Merkmalsliste, Merkmalsliste vor
Applikationsbild.** Die Reihenfolge ist mit jedem Fund teurer erkauft
worden.
