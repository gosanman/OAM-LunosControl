# Entwicklungsplan Firmware — KNXFANDRV / Lüftersteuerung 0–10 V

**Stand:** 20.09.2026
**Zielhardware:** KNXFANDRV Rev 0.1 (RP2040 + OpenKNX-BCU GN100, 2 × GP8413 über ADuM1250)
**Vorlage:** `OpenKNX/OAM-FanControl` und `OpenKNX/OFM-FanControl`, Branch `cad435/main`
**Funktionsmesslatte:** Arcus-EDS KNX-LUNOS-CONTROL4, Applikationsbeschreibung 682x_d4a vom 07.10.2024 (Abschnitt 0.5; Bedienlogik in Phase 4 daran ausgerichtet)
**Begleitdokumente:** `Referenzdesign_Rev4_GP8413.md`, `Pruefliste_KNXFANDRV_Rev01.md`

Dieser Plan ist so geschrieben, dass er in Claude Code Phase für Phase abgearbeitet
werden kann. Jede Phase hat ein Ziel, konkrete Schritte, eine Fertig-Definition und
den Kontext, den Claude Code dafür braucht. Die Phasen bauen aufeinander auf —
**Phase 1 ist ohne KNX und muss vor allem anderen laufen**, weil sie die einzige
offene Sicherheitsfrage der Hardware klärt.

---

## 0. Entscheidungen vor der ersten Zeile Code

### 0.1 Neue Basis, aber OpenKNX-Konventionen behalten

Die Vorlage wird nicht geforkt, sondern als Muster gelesen. Neu geschrieben werden
das Funktionsmodul (OFM) und die Applikation (OAM). Unverändert eingebunden werden
die OpenKNX-Bibliotheken, weil sie den ETS-Produktbau, den Konfigurationstransfer,
die Konsole und das Flash-Management liefern — das selbst zu bauen wäre Monate.
**OGM-HardwareConfig entfällt** — die Platine ist einmalig, ihr Header definiert alles
selbst (siehe Phase 2, Schritt 3):

| Bibliothek | Zweck | Version wie in der Vorlage |
|---|---|---|
| `OpenKNX/knx` | KNX-Stack | v1 |
| `OpenKNX/OGM-Common` | Modulrahmen, Konsole, Flash, LEDs | v1, BASE_VerifyVersion 1.9 |
| `OpenKNX/OFM-ConfigTransfer` | ETS-Konfiguration exportieren/importieren | v1, 0.5 |
| `OpenKNX/OFM-FileTransferModule` | Dateisystem über den Bus | v1 |
| `OpenKNX/OFM-LogicModule` | Logikkanäle, optional | v1, 4.4 |

**Lizenz:** Die Vorlage steht unter GPL-3.0. Wer Code daraus übernimmt — und die
Kanal-Zustandsmaschine wird man teilweise übernehmen —, veröffentlicht unter
GPL-3.0. Das ist bei OpenKNX üblich und kein Nachteil.

### 0.2 Repository-Struktur

Zwei Repositories, wie bei OpenKNX üblich. Der OFM ist eine PlatformIO-Bibliothek
und wird vom OAM als `lib/` eingebunden.

```
OFM-KwlControl/                    Funktionsmodul (Bibliothek)
  library.json                       Name, Version — der Producer prüft dagegen
  platformio.ini                     für native Unit-Tests
  src/
    Room.share.xml / Room.templ.xml  ETS: Raum-Modul (Präfix ROOM)
    Fan.share.xml / Fan.templ.xml    ETS: Lüfter-Modul (Präfix FAN) + Verbund-Parameter
    KwlRoomModule.h/.cpp           OpenKNX::Module ROOM — bis 8 Räume, Flash, Konsole
    KwlRoom.h/.cpp                 OpenKNX::Channel — Sensoren, Betriebsarten, Führungen
    KwlFanModule.h/.cpp            OpenKNX::Module FAN — bis 12 Lüfter, Verbünde, DAC
    KwlFan.h/.cpp                  OpenKNX::Channel — ein Lüfter, Antrieb, Status
    KwlGroup.h/.cpp                Verbund: Stufenregel, Takt, Phase, Nachströmung
    StageLadder.h/.cpp               Grenzwert-Treppe (rF, CO₂, VOC)
    KwlTypes.h                     Enums, Konstanten, Fehlercodes
    KwlCurve.h/.cpp                Kennlinien: Gerätetyp × Stufe × Richtung → Volt
    CableComp.h/.cpp                 Kabelkompensation, stufenabhängig
    IDacDrive.h                      Schnittstelle: Spannung auf Kanal ausgeben
    Drive/Gp8413Drive.h/.cpp         IDacDrive über I²C, zwei Kanäle je Chip
    Drive/Gp8413Store.h/.cpp         Bitbang-EEPROM-Sequenz (kein normales I²C)
    Baggages/Help_de/*.md            Kontexthilfe je Parameter, wird in die ETS gebaut
  test/
    test_curve.cpp                   Volt → Code, Rundung, Klemmung
    test_cablecomp.cpp               Kompensation je Stufe und Länge
    test_tact.cpp                    Pendel-Zustandsmaschine
    test_stagemap.cpp                Prozent → Stufe mit Hysterese
  doc/
    Applikationsbeschreibung-Kwl.md

OAM-KwlControl/                    Applikation (Firmware-Projekt)
  platformio.ini                     zieht OGM-Common-Basis + custom
  platformio.custom.ini              Umgebungen: develop, release, test_dac
  dependencies.txt                   Commit-Stände der lib/-Repos
  restore/Restore-Dependencies.ps1   klont lib/ aus dependencies.txt
  include/
    hardware.h                       wählt den Board-Header
    KnxFanDrv_Rev01.h                Pins, Adressen, FAN_INIT() für DEINE Platine
    KnxFanDrv_DevPico.h              Entwicklungsaufbau Pico + DFR1073, 2 Kanäle
  src/
    main.cpp                         openknx.init / addModule / setup — ~40 Zeilen
    main_dactest.cpp                 Phase 1: Testfirmware OHNE KNX
    Kwl.xml                        ETS-Produkt: op:define aller Module
    Kwl.conf.xml                   Versionen, Vorbelegungen
  docs/
    PLAN.md                          dieses Dokument
    Referenzdesign_Rev4_GP8413.md
    Pruefliste_KNXFANDRV_Rev01.md
  CLAUDE.md                          Regeln für Claude Code — Sicherheitsinvarianten
```

Namen sind Vorschläge. Was zählt: OpenKNX erwartet `OFM-`/`OAM-`-Präfix,
`library.json` mit Version, und dass der Modulname im XML (`prefix="LUN"`) zur
`MODULE_<Name>_Version`-Konstante passt.

### 0.3 Werkzeugkette (Windows)

| Werkzeug | Wofür | Hinweis |
|---|---|---|
| VS Code + PlatformIO | Bauen, Flashen, Monitor | RP2040-Plattform aus OGM-Common-Basis |
| OpenKNXproducer | XML → knxprod für die ETS | .NET, braucht **ETS ≥ 5.7 auf demselben PC** |
| ETS | Parametrieren, Programmieren | Lizenz für Demo reicht für 5 Geräte |
| PowerShell | `Restore-Dependencies.ps1` | klont die lib/-Abhängigkeiten |
| Claude Code (VS Code) | Umsetzung | Arbeitsverzeichnis = OAM-Repo, OFM als Submodul oder Nachbar |
| Logikanalysator | Phase 1 | für P15, beidseitig des Isolators |
| Voltmeter | Phase 1 | für P6 — die wichtigste Messung des Projekts |

**Build-Befehle**, wie in der Vorlage:
```
OpenKNXproducer create --Debug -h include/knxprod.h src/Kwl
pio run -e develop_KnxFanDrv
pio run -e develop_KnxFanDrv -t upload
pio device monitor
pio test -e native            (Unit-Tests ohne Hardware, im OFM-Repo)
```

### 0.4 Was die Vorlage liefert und was neu ist

| Aus der Vorlage übernehmen (Konzept, teils Code) | Neu schreiben | Entfällt |
|---|---|---|
| Modul/Kanal-Aufteilung, Flash-Persistenz | `IDacDrive` statt `IFanHardware` — Volt statt Prozent | Tacho, Drehzahl-KO |
| Takt/Pendel: Master, Phase, Zykluszeit, Totzeit | Stufenmodell 0…4 statt Prozent | Blockiererkennung |
| Freigabe-Latch, Master-Überwachung, Lebenszeichen | Gerätetypen e²60 / ego / RA / generisch | Lastschalter |
| Sollwertquellen: fest, KO, P-Regler, Zweipunkt | ego als **Doppelkanal-Knoten** mit Abluftstoß | PWM-Frequenz |
| Taupunktwächter | Kabelkompensation, Kalibrierfaktor | Anlaufpuls |
| Stoßlüftung | EEPROM-Einschaltwert per Konsole | Drehzahl→Volumenstrom-Kennlinie |
| Fehlercode-Priorität, Alarm-Bit | Volumenstrom aus Stufe (nominal) | |
| Konsole: `fan st`, `fan cNN`, Übersteuerung | Bitbang-Store durch den Isolator | |
| Status-LED-Zuordnung über PT-SLEDFunc | Busformat-Schalter als Compile-Zeit-Zwang | |
| Baggages/Help_de-Struktur | | |

### 0.5 Funktionsumfang — Zielbild

Die Messlatte ist die Funktionsliste des kommerziellen Arcus-EDS KNX-LUNOS-CONTROL4.
Jede Funktion ist einem Mechanismus zugeordnet; wo mehrere Funktionen dasselbe
meinen, steht es dabei. Umsetzung in den Phasen 3 bis 5.

| Funktion (Anforderung) | Mechanismus | Phase |
|---|---|---|
| Lüfterstufen steuern | Stufenmodell 0…4, KO Stufe (5.100) und Prozent (5.001), Stufe +/− (1.007) | 3 |
| Richtung steuern | Richtungsbildung: Pendel / Zuluft / Abluft, KO Betriebsweise (5.010) | 4c |
| **Wärmerückgewinnung steuern** | = Richtungsbildung. WRG an = Pendel, WRG aus = Einrichtungsbetrieb. Beim ego nur über Stufe 4 abschaltbar. | 4c |
| Manuelle Stufe über KNX | KO Stufe mit Parameter „Manuell gilt: dauerhaft / N Minuten / bis Betriebsartwechsel" | 4a |
| Filterwechselanzeige | Laufzeit- oder volumenstromgewichteter Zähler, persistent; KO Fällig, Restlaufzeit, Reset | 5 |
| rF innen → Feuchteabführung | Grenzwert-Treppe: 5 Grenzwerte für 4 Stufen, Hysterese eingebaut | 4b |
| **abs. Feuchte innen/außen → Kellertrocknung** | Feuchtevergleich über Wasserdampf-Partialdruck; fördert nur, wenn außen trockener. Ersetzt den Taupunktwächter der Vorlage. Anzeige in g/kg mit Höhenparameter. | 4b |
| T innen → Gebäudeschutz | Frost-/Hitzeschutz → automatischer Wechsel in Betriebsart Schutz | 4a/4b |
| T innen/außen → WRG-Optimierung | Zyklusregel: kurzer Zyklus (WRG) im Heizfall, langer Zyklus (Sommer) sonst; Temperaturabstand als Konfliktschutz zur Heizung | 4b/4c |
| T innen/außen/Soll → Heiz-/Kühlunterstützung | Anforderungsgröße „Temperatur": freie Kühlung / Wärmeerhalt aus drei Temperaturen | 4b |
| CO₂ | Grenzwert-Treppe wie rF | 4b |
| Zuluftbetrieb | Richtungsbildung, feste Richtung — einzeln je Knoten oder gruppenweit | 4c |
| Abluftbetrieb, einzeln und kombiniert | Betriebsweise-KO je Knoten; Abluftanforderung mit Vor-/Nachlauf; **Zuluftanforderung** als Ausgang für die Partner (Nachströmung) | 4c |
| Leitungskompensation | `CableComp`, stufenabhängig | 3 |
| HVAC Komfort / Standby / Nacht / Schutz | KO Betriebsart (20.102, dauerhaft) und Zwangsbetriebsart (20.102, mit Laufzeit); je Betriebsart Parametersatz: Grundstufe, Maximalstufe, Führungen, Zyklusregel, Intervall | 4a |
| Stoßlüften (erweitert) | Betriebsart 5; über frei belegbares Zwangsobjekt (Vorgabe ZO1), Laufzeit | 4a |
| Ruhe/Aus (erweitert) | Betriebsart 7; über Zwangsobjekt (Vorgabe ZO2); Systemsperre zusätzlich als eigenes Sperr-KO | 4a |
| Sommerbetrieb (Schaltobjekt) | KO Sommer (1.001) → **langer Zyklus** (Vorgabe 1 h) statt 70 s — kein Einrichtungsbetrieb | 4c |
| Nachtbetrieb (Schaltobjekt) | KO Nacht (1.001) → Betriebsart Nacht, gleichwertig zu DPT 20.102 = 3 | 4a |
| Alle Betriebsarten frei konfigurierbar | 7 Betriebsarten (inkl. Temperatur-Absenkung) mit je eigenem Parametersatz, je Knoten | 4a |
| *(neu)* Intervallbetrieb | je Betriebsart: Periode/Aktivzeit | 4a |
| *(neu)* Zykluszeit je Stufe | 40 s…2 h, Vorgabe 70 s | 4c |
| *(neu)* Anteilsfaktor je Knoten | 25…100 % der Gruppen-Stufe | 4c |
| *(neu)* Stufe +/− per Taster | KO 1.007 | 4a |

Was das Arcus-Modul **nicht** kann und dieser Entwurf zusätzlich bietet: Mischbetrieb
e²/ego auf einem Gerät, raumweise getrennte Betriebsarten, ETS-überschreibbare
Kennlinien, Leitungskompensation je Knoten, Einschaltwert im EEPROM, Nachlauf,
Fensterkontakt-Sperre, Betriebsstunden und Taktzähler auf dem Bus.

### 0.6 Datenmodell: Raum, Verbund, Lüfter

Frühere Fassungen kannten nur „Knoten = Lüfter". Das trägt nicht: Schlafzimmer und
Büro sind zwei Räume mit eigenen Sensoren und eigenen Betriebsarten, aber ihre beiden
e²60 sind **ein** Pendelpaar und müssen mit gleicher Stufe gegenläufig laufen.
Deshalb drei Ebenen:

```
  Raum 1 Schlafzimmer ─┐                        ┌─ Lüfter 1  e²60   Phase 0  Anteil 100 %
    Sensoren, Betriebs-│   Verbund 1 (Pendel)   │
    arten, Führungen   ├──► Regel: max(Anford.) ├─ Lüfter 2  e²60   Phase 1  Anteil 100 %
  Raum 2 Büro ─────────┘   ≤ min(Deckel)        │
                            Takt 70 s / 1 h     └──────────────────────────────────────
  Raum 3 Bad ──────────────► Verbund 2 (Einzel) ─── Lüfter 3  ego    pendelt intern, 2 Kanäle
```

| Ebene | Trägt | ETS-Modellierung | Anzahl |
|---|---|---|---|
| **Raum** | Sensor-KOs (rF, T, CO₂, VOC, Tsoll, Außenwerte), Betriebsart-Ebenen, Zwangsobjekte, Handstufe, Sommer, Sperre, Abluftanforderung, Führungen, Schutz → **Raumanforderung** (Stufe), **Raumdeckel** (Maximalstufe), **Zyklusregel-Wunsch**, **Richtungswunsch** | eigenes OpenKNX-Modul `KwlRoomModule`, Präfix `ROOM`, Kanaltemplate = Raum | **max. 8**, in der ETS 1…8 einblendbar |
| **Verbund** | Stufenregel, Zyklusregel-Konflikt, Zykluszeiten je Stufe, Sommerzykluszeit, Totzeit, Rolle (intern / Master sendet / Slave folgt extern), „folgt Zuluftanforderung von Raum …" | Parameterblock im share.xml des Lüftermoduls; kein eigener KO-Block — Gruppen-KOs liegen beim Lüfter mit Rolle Master/Slave | **max. 8**, sichtbar bis ⌈Lüfter/2⌉ + 2 |
| **Lüfter** | Gerätetyp, DAC-Kanal(e), Kennlinie, Kalibrierung, Kabelkompensation, **Raum** (1…8), **Verbund** (1…8 oder eigenständig), Phase (0/1), Anteil, Freigabe, Gruppen-KOs, Status-KOs, Betriebsstunden, Filter, Störung | eigenes OpenKNX-Modul `KwlFanModule`, Präfix `FAN`, Kanaltemplate = Lüfter | **max. 12**, Anzahl folgt der **Hardwareauswahl** |

**Regeln:**
- Ein Lüfter gehört zu genau einem Raum und genau einem Verbund. Ein Raum kann null
  bis zwölf Lüfter haben (Raum ohne Lüfter = reiner Sensor-/Bedienraum, z. B. als
  Außenwertquelle). Die Räume eines Verbunds sind die Räume seiner Lüfter.
- **Verbundstufe** = Regel über die Raumanforderungen seiner Räume. Parameter je Verbund:
  `Maximum, begrenzt durch kleinsten Raumdeckel` (Vorgabe) / `Minimum` / `Raum X führt`.
  Vorgabe-Begründung: CO₂ im Büro hebt tags beide an; nachts deckelt das Schlafzimmer
  beide auf Stufe 1.
- **Zyklusregel im Verbund**: wollen die Räume Verschiedenes (einer WRG, einer Sommer),
  gewinnt `WRG (kurz)` — Vorgabe, weil Wärmeverlust teurer ist als entgangene Kühlung.
  Alternativ `Raum X führt`.
- **Richtungswunsch** (Zuluft/Abluft aus Betriebsweise-KO oder Abluftanforderung) eines
  Raums wirkt auf *seine* Lüfter; beim Pendelpaar bedeutet Abluft in Raum 1
  automatisch Zuluft in Raum 2 — das ist die Phase.
- **Nachströmung**: Verbund-Parameter „folgt Zuluftanforderung von Raum 3" → solange
  Raum 3 Abluft anfordert, fährt der Verbund alle Lüfter auf Zuluft. Intern, ohne Bus.
  Für Fremdgeräte zusätzlich das Ausgangs-KO am Raum.
- Rang 1–4 der Vorfahrtstabelle (Sperre, Schutz, Zu-/Abluftanforderung, Gruppe) werden
  **je Lüfter** nach der Verbundbildung angewandt; Rang 5–11 (Hand, Automatik,
  Betriebsart-Ebenen) **je Raum** davor. Die Pipeline in Phase 4 ist entsprechend
  zu lesen: „Raum" bis zur Raumanforderung, dann Verbund, dann Lüfter.
- Zwei Module statt einem: OpenKNX kann mehrere Module aus einer Bibliothek laden;
  `Kwl.xml` definiert `ROOM` (KoOffset 20, **8** Kanäle × 40 KOs = 20…339) und `FAN`
  (KoOffset 340, **12** × 24 = 340…627). Das Logikmodul rückt auf KoOffset **640**.
  `KwlFanModule` liest die Raumanforderungen **in-process** über eine Schnittstelle
  von `KwlRoomModule`, nicht über den Bus.

**Kanalanzahl in der ETS — wie bei allen OpenKNX-Modulen ein-/ausblendbar:**

| | Kompiliert (Maximum) | ETS-Parameter | Sichtbarkeit |
|---|---|---|---|
| Räume | 8 | `ROOM_Active` je Raum | Raumseite sichtbar, wenn der Raum aktiviert ist |
| Lüfter | 12 | **`FAN_Hardware`** — Auswahlliste der Platinen | Lüfterseite n sichtbar, wenn n ≤ Kanalzahl der gewählten Platine |
| Verbünde | 8 | `FAN_GrpN_Active` je Verbund | Verbundseite sichtbar, wenn der Verbund aktiviert ist |

**Hardwareauswahl statt Lüfterzahl.** Der Anwender wählt in der ETS nicht „wie viele
Lüfter", sondern **welche Platine**; die Kanalzahl folgt daraus:

| Auswahl `FAN_Hardware` | Lüfterkanäle | DACs | I²C-Adressen |
|---|---|---|---|
| Entwicklungsaufbau Pico + DFR1073 | **2** | 1 × GP8413 (Breakout) | 0x58 |
| KNXFANDRV Rev 0.1 | **4** | 2 × GP8413 | 0x58, 0x59 |
| KNXFANDRV 6 (geplant) | 6 | 3 | 0x58…0x5A |
| KNXFANDRV 8 (geplant) | 8 | 4 | 0x58…0x5B |
| KNXFANDRV 10 (geplant) | 10 | 5 | 0x58…0x5C |
| KNXFANDRV 12 (geplant, Maximum) | 12 | 6 | 0x58…0x5D |

Die GP8413-Adressierung (A2A1A0 → 8 Adressen) trägt bis 16 Kanäle; 12 ist die Grenze
der Platinenfamilie, nicht des Busses. Jede Variante bekommt ihren eigenen
Board-Header mit `FANDRV_BOARD_CHANNELS` und der Zuordnung Kanal → (Adresse, VOUT).

**Plausibilitätsprüfung beim Start:** Die Firmware vergleicht die ETS-Auswahl mit
`FANDRV_BOARD_CHANNELS` aus dem Board-Header. Stimmen sie nicht überein — falsche
knxprod auf der Platine, oder Rev-0.1-Firmware mit 8-Kanal-Parametrierung —, gehen
**alle** Lüfter auf Fehlercode 3 (Konfiguration), Alarm, LED rot, Ausgänge 5,00 V.
Ein Lüfter, der laut ETS existiert, aber keinen DAC-Kanal hat, darf nicht stumm
ignoriert werden.

**Ausgeblendete Kanäle** sind in der Firmware inaktiv: kein KO-Verkehr, keine
Iteration in `loop()`, kein Flash-Anteil außer dem reservierten Block. Das ist das
Muster des OpenKNX-Kanalauswahl-Patterns: nur aktive Kanäle werden überhaupt
angelegt (`isActive()`, sonst `nullptr`-Slot) — die
Vorlage macht es in `FanModule::setup()` vor, dort mit `FAN_ChannelCount`.

**Beispielkonfiguration für das Haus:**

| Lüfter | Typ | Raum | Verbund | Phase | Anteil |
|---|---|---|---|---|---|
| 1 | e²60 | 1 Schlafzimmer | 1 | 0 | 100 % |
| 2 | e²60 | 2 Büro | 1 | 1 | 100 % |
| 3 | ego | 3 Bad | 2 | — (pendelt intern) | 100 % |
| 4 | — | — | — | — | vorhanden, in der ETS „nicht belegt" |

ETS: `FAN_Hardware` = KNXFANDRV Rev 0.1 (→ 4 Lüfterzeilen), Räume 1…3 aktiviert. Verbund 1: Maximum ≤ kleinster Deckel, WRG gewinnt, folgt Zuluftanforderung von
Raum 3. Verbund 2: Einzel.

**Führungsgrößen je Raum**, alle optional, alle mit Überwachungszeit und „… aktiv"-KO:

| Größe | DPT | Mechanismus | Vorgabe-Grenzwerte GW0…GW4 |
|---|---|---|---|
| rF innen | 9.007 | Grenzwert-Treppe | 45 / 50 / 55 / 60 / 65 % |
| CO₂ | 9.008 | Grenzwert-Treppe | 700 / 850 / 1000 / 1300 / 1700 ppm |
| **VOC / Luftgüte** | 9.008 (ppb) oder Index (5.010 / 7.001) — Parameter | Grenzwert-Treppe, einheitenfrei | 300 / 500 / 750 / 1000 / 1500 (ppb) bzw. 100 / 150 / 200 / 300 / 400 (Index) |
| T innen, T außen, T soll | 9.001 | Temperaturführung, Schutz | s. 4b |
| rF außen + T außen | 9.007 / 9.001 | Feuchtevergleich | Δe 1,5 / 0,5 hPa |

Außenwerte hat das Haus einmal: Parameter „Außenwerte von Raum 1 übernehmen" in
Raum 2…4.


---

## Phase 1 — Hardware-Nachweis ohne KNX

**Ziel:** Die drei Messungen, an denen die Architektur hängt, mit einer minimalen
Firmware erledigen. Kein OpenKNX, kein ETS, nur RP2040, I²C und ein Voltmeter.

**Warum zuerst:** O17 (Busformat des 15-Bit-Werts) entscheidet, ob 5,00 V als
`0x4000` oder `0x8000` geschrieben wird. Die falsche Wahl macht aus dem
Sicherheitswert 10,00 V — Volllast. Bevor das nicht gemessen ist, darf keine
Zeile Kanallogik einen Ausgang treiben.

### Schritte

1. **PlatformIO-Umgebung `test_dac`** in `platformio.custom.ini`: RP2040, Arduino-Core,
   `src_filter` auf `main_dactest.cpp`, kein knx, kein OGM-Common.
2. **`Gp8413Drive`** schreiben, minimal:
   - `begin()`: Register 0x01 ← 0x11 (Bereich 0–10 V) für beide Adressen 0x58/0x59
   - `writeRaw(addr, channel, uint16_t wire)`: 3 Bytes, Low zuerst
   - `writeBoth(addr, wire0, wire1)`: 5 Bytes an Register 0x02
   - I²C auf **GPIO2/GPIO3**, 100 kHz zum Start, später 400 kHz
3. **Serielle Konsole** mit Befehlen:
   - `raw <addr> <ch> <hex>` — Rohwert schreiben (für P6)
   - `range <addr>` — Register 0x01 erneut setzen
   - `probe` — Adressabfrage 0x58…0x5F, Ergebnis ausgeben (wie `begin()` der DFRobot-Lib)
   - `store <addr>` — Bitbang-Sequenz (P11/P15), **nur nach Rückfrage `y`**
4. **`Gp8413Store`** — Bitbang nach DFRobot_GP8XXX, exakt:
   ```
   Pins auf SIO umschalten, SDA open-drain (Eingang = High)
   START, 3 Bit 0b010 ohne ACK, STOP
   START, 0x10, 0x03, STOP
   START, (addr<<1), 8× 0x00, STOP
   delay 10 ms
   START, 3 Bit 0b010 ohne ACK, STOP
   START, 0x10, 0x00, STOP
   Pins zurück auf I2C-Funktion
   ```
   Zeiten: 1 µs vor / 2 µs nach Flanke, 5 µs Zyklus (≈ 125 kHz).

   > **Stand 2026-09-25 — `Gp8413Store` entfällt aus der Firmware.** Befund B1
   > (`Messprotokoll_Phase1.md`): Frame 1/2/4/5 gehen an alle Chips, Frame 3 ist
   > ein roher Bitstrom ohne ACK, den jeder entsperrte Chip mitliest. Ein `store 58`
   > hat U3 (0x59) dauerhaft um +10,5 % verstellt. Die Sequenz bleibt nur in
   > `test_dac`, gesperrt hinter `FANDRV_TESTDAC_ALLOW_STORE`.
5. **Messprotokoll** als `docs/Messprotokoll_Phase1.md` anlegen, Tabelle für P6, P7,
   P10, P11, P15 mit Spalten Soll / Gemessen / Datum.

### Messungen (aus der Prüfliste, Reihenfolge zwingend)

| # | Was | Erwartung | Ergebnis legt fest |
|---|---|---|---|
| P6 | `raw 0x58 0 0x4000`, dann `raw 0x58 0 0x8000`, jeweils S1 messen | genau einer ergibt 5,00 V | `FANDRV_DAC_LEFT_ALIGNED` = 0 oder 1 |
| P7 | logisch 0 und 32767 auf jedem Kanal | 0,00 / 10,00 V ±30 mV | Skalenfehler je Kanal → Kalibrierfaktor |
| P15 | `store` mit Logikanalysator **beidseitig** des ADuM1250 | Frames kommen sekundär unverfälscht an | Isolator trägt die Sequenz — sonst Architekturänderung |
| P10 | EEPROM mit 5,00 V laden, 12 V aus/ein, ohne I²C messen | 5,00 V an allen Kanälen | EEPROM funktioniert |
| P11 | wie P10, aber Register 0x01 nach Kaltstart **nicht** setzen | 5,00 V, nicht 2,50 V | Bereichsbit wird mitgesichert |

### Fertig, wenn

- [ ] `FANDRV_DAC_LEFT_ALIGNED` ist gemessen und steht im Board-Header mit Datum
- [ ] Skalenfehler aller vier Kanäle notiert
- [x] P15 bestanden — oder die Architekturentscheidung (Optokoppler / ohne Trennung
      / ohne EEPROM) ist getroffen → **ohne EEPROM**, 2026-09-25, Befund B1. Der
      Isolator trägt die Sequenz (U2 hat gespeichert); das Problem ist der Bus mit
      zwei Chips, nicht der Isolator.
- [x] P11 bestanden — oder der Startup-Pfad schreibt Register 0x01 in den ersten
      Millisekunden und das Risiko ist dokumentiert → gegenstandslos ohne EEPROM;
      der Startup-Pfad schreibt Register 0x01 ohnehin zuerst (Invariante 3).

**Claude Code braucht:** Referenzdesign §2.3 (Registersatz, beide Formatvarianten),
§2.4 (Store-Sequenz), §6.4 (Bitbang-Pseudocode), die DFRobot_GP8XXX-Konstanten aus
Anhang A. Und die Anweisung: **kein `store` ohne explizites `y` des Bedieners**.

---

## Phase 2 — OFM-Skelett und ETS-Produkt

**Ziel:** Ein Gerät, das in der ETS erscheint, programmiert werden kann und über
ein KO eine Stufe auf einen Kanal schreibt. Noch keine Regelung, kein Takt.

### Schritte

1. **Repos anlegen**, Skelett aus der Vorlage kopieren und umbenennen:
   `library.json`, `platformio.ini`, `restore/`, `.gitignore`, LICENSE (GPL-3.0).
2. **`Restore-Dependencies.ps1`** ausführen, `dependencies.txt` mit den Commit-Ständen
   der Vorlage übernehmen (siehe 0.1), `OFM-FanControl` durch den eigenen OFM ersetzen.
3. **Board-Header `KnxFanDrv_Rev01.h`** — vollständig, damit nichts erraten wird:
   Ohne OGM-HardwareConfig muss der Header **alles** liefern, was OGM-Common
   erwartet — nach dem Muster von `MrSpiebFanControlHardware.h` der Vorlage. Die
   exakten Makronamen gegen `lib/OGM-Common` prüfen (grep nach `PROG_LED_PIN`,
   `SAVE_INTERRUPT_PIN`, `INFO1_LED_PIN`, `KNX_UART_`), sie ändern sich zwischen
   Versionen. Pins aus dem Controller-Schaltplan Rev 0.1 gelesen — **vor dem ersten
   Flash am Board nachprüfen:**
   ```c
   // KNXFANDRV Rev 0.1 — eigene Platine, BCU-Modul GN100, RP2040 on-board
   // --- KNX / OpenKNX-Systempins (Controller-Blatt) ---
   #define KNX_UART_NUM         0
   #define KNX_UART_TX_PIN      12     // Netz KNX_Tx  → GN100 Rx
   #define KNX_UART_RX_PIN      13     // Netz KNX_Rx  ← GN100 Tx
   #define SAVE_INTERRUPT_PIN   14     // KNX_SAVE
   #define PROG_LED_PIN         22     // PROGLED
   #define PROG_LED_PIN_ACTIVE_ON 1
   #define PROG_BUTTON_PIN      23     // PROGBTN
   #define FUNC1_BUTTON_PIN     24     // FUNC1BTN
   #define INFO1_LED_PIN        25     // INFOLED
   #define INFO1_LED_PIN_ACTIVE_ON 1
   // --- Lüfteransteuerung ---
   #define FANDRV_I2C_SDA        2      // GPIO2, I2C1
   #define FANDRV_I2C_SCL        3      // GPIO3, I2C1
   #define FANDRV_I2C_PORT       i2c1
   #define FANDRV_DAC_ADDR_A     0x58   // U2: S1 = VOUT0, S2 = VOUT1
   #define FANDRV_DAC_ADDR_B     0x59   // U3: S3 = VOUT0, S4 = VOUT1
   #define FANDRV_BOARD_CHANNELS 4      // muss zur ETS-Auswahl FAN_Hardware passen
   #define FANDRV_BOARD_ID       1      // 0 = DevPico (2), 1 = Rev 0.1 (4), 2 = 6, 3 = 8, 4 = 10, 5 = 12
   // Aus Phase 1, gemessen am __.__.2026:
   // #define FANDRV_DAC_LEFT_ALIGNED 0   // 5,00 V = 0x4000 (Datenblatt)
   // #define FANDRV_DAC_LEFT_ALIGNED 1   // 5,00 V = 0x8000 (DFRobot-Lib)
   #ifndef FANDRV_DAC_LEFT_ALIGNED
   #error "FANDRV_DAC_LEFT_ALIGNED nicht gesetzt: erst Phase 1 / P6 messen"
   #endif
   ```
   Der `#error` ist Absicht: die Firmware darf sich nicht bauen lassen, solange das
   Busformat nicht gemessen ist.
4. **`IDacDrive`** — Schnittstelle in Volt, nicht Prozent:
   ```c
   class IDacDrive {
     virtual void begin() = 0;                       // Bereich setzen, sicherer Zustand
     virtual void setVolt(uint8_t ch, float v) = 0;  // 0..10, klemmt, rundet
     virtual void setVoltPair(uint8_t chA, float vA, uint8_t chB, float vB) = 0; // ein Frame
     virtual bool probe(uint8_t ch) = 0;             // Chip antwortet?
     virtual uint8_t channels() const = 0;
   };
   ```
   `Gp8413Drive` implementiert sie für 4 Kanäle, kennt die Zuordnung Kanal →
   (Adresse, VOUT) und den Formatschalter. `setVoltPair` ist für den ego: beide
   Motoren in einem I²C-Frame (Register 0x02, 4 Datenbytes, **zwei verschiedene
   Werte** — die DFRobot-Funktion kann das nicht, das Register schon).
5. **`KwlCurve`** — reine Logik, testbar ohne Hardware:
   - Gerätetyp × Stufe × Richtung → Sollspannung (Tabellen aus Referenzdesign §3.4)
   - Volt → 15-Bit-Code: `round(v × 3276.7)`, klemmen 0…0x7FFF
   - Code → Wire nach Formatschalter
6. **`KwlRoomModule` / `KwlFanModule`** minimal: Raummodul mit 8, Lüftermodul mit
   12 Kanälen kompiliert, per ETS 3 Räume und Rev-0.1-Hardware (4 Lüfter) sichtbar.
   Lüfter hat KO
   „Stufe" (DPT 5.100, 0…4) als Eingang und „Stufe Status" als Ausgang, schreibt die
   Spannung aus der Kennlinie auf seinen Kanal. Richtung fest A.
7. **ETS-XML:**
   - `Fan.share.xml` / `Room.share.xml`: geräteweite Parameter (später Takt-Zykluszeit, Kompensations-
     querschnitt), PT-SLEDFunc-Enum für die Status-LEDs (Basis 110, wie Vorlage)
   - `Fan.templ.xml` / `Room.templ.xml`: je Kanal Parameter „Gerätetyp", „Kanal aktiv"; KOs Stufe /
     Stufe Status. KO-Nummern als `%K0%`…, Offset im OAM 20, Blockgröße wie Vorlage 32.
   - `Kwl.xml`: `op:define` für BASE (10), UCT (99), **ROOM (20, NumChannels 8,
     KoOffset 20)**, **FAN (30, NumChannels 12, KoOffset 340)**, optional LOG (10,
     KoOffset 640). Sichtbarkeit der Kanalseiten über die Kanalaktivität; bei den
     Lüftern zusätzlich begrenzt durch die Kanalzahl aus `FAN_Hardware`, wie in 0.6.
   - **Eigene ApplicationNumber** wählen, nicht 0x86 der Vorlage — sonst kollidiert es
     in der ETS mit einem installierten FanControl.
8. **Startup-Sequenz** in `KwlModule::setup()`:
   ```
   drive.begin()                     → Register 0x01 = 0x11 an beide Chips
   für jeden Knoten: sicherer Zustand → 5,00 V bipolar / 0,00 V unipolar
   Adressabfrage beider Chips; fehlt einer → Fehlercode Config, Knoten inaktiv
   ```
9. **Native Tests** (`pio test -e native`) für `KwlCurve`: alle 20 Tabellenwerte
   aus §3.4 gegen die dort berechneten Codes, beide Formatvarianten, Klemmung.

### Fertig, wenn

- [ ] knxprod baut, Gerät erscheint in der ETS, lässt sich programmieren
- [ ] KO „Stufe" auf Kanal 1 → gemessene Spannung passt zur Tabelle ±30 mV
- [ ] Native Tests grün
- [ ] Gerät startet mit 5,00 V auf allen bipolaren Kanälen — mit Oszilloskop am
      Einschaltmoment geprüft, nicht nur nach dem Hochlauf

**Claude Code braucht:** die Vorlage als Lesestoff (`FanModule.h/.cpp`, `Fan.xml`,
`Fan.templ.xml` Kopfteil), Referenzdesign §2.3, §3.4, §6.1–6.3, die OpenKNX-Wiki-
Seiten zu OpenKNXproducer. Die Vorlage in ein Verzeichnis `reference/` legen, das
nicht gebaut wird.

---

## Phase 3 — Gerätemodell: Gerätetypen, Doppelkanal, Kompensation

**Ziel:** Alle drei Lüfter des Hauses korrekt ansteuern, inklusive ego-Abluftstoß
und Kabelkompensation. Noch kein Takt — Richtung wird per KO vorgegeben.

### Gerätetypen

| Typ | Kanäle | Stufen | Besonderheit |
|---|---|---|---|
| `E2_60` | 1 | 0…4, bipolar, Codierschalter 5 | Pendelpartner möglich |
| `EGO` | **2** | 0…4, bipolar, Codierschalter 9 | Stufe 4 = Abluftstoß, beide 1,11 V |
| `RA_15_60` | 1 | 0…4, **unipolar**, Codierschalter 0 | 0 V = aus, kein Pendel |
| `GENERIC_BIPOLAR` | 1 | 0…4 | Spannungen frei parametrierbar |
| `GENERIC_UNIPOLAR` | 1 | 0…4 | Spannungen frei parametrierbar |

### Schritte

1. **Lüfter mit Kanalbedarf 1 oder 2.** Der DAC-Kanal wird **je Lüfter in der ETS
   ausdrücklich gewählt** (`FAN_Channel`, Vorgabe Lüfter n → Kanal n); der ego belegt
   diesen und den folgenden Kanal. `KwlFanModule` prüft beim Start: doppelt belegter
   Kanal, Kanal jenseits der Platine oder ein ego auf geradem Kanal (beide Motoren
   müssten auf demselben DAC liegen) → Fehlercode 3 am Lüfter. Zusätzlich die
   Plausibilitätsprüfung aus 0.6: ETS-Hardwareauswahl ≠ `FANDRV_BOARD_CHANNELS` →
   Fehlercode 3 an **allen** Lüftern, Ausgänge 5,00 V. Das ist die Verallgemeinerung
   von `FAN_BOARD_CHANNELS` aus der Vorlage.
2. **ego-Doppelkanal:** `KwlNode` mit Typ EGO schreibt immer `setVoltPair`. Stufen
   1…3: Motor 1 / Motor 2 gespiegelt um 5 V, bei Richtungswechsel getauscht. Stufe 4:
   beide auf 1,11 V, Richtung irrelevant.
3. **Kennlinien als ETS-Parameter mit Vorgabewerten** aus §3.4. Sichtbar nur bei
   Typ GENERIC oder wenn „Kennlinie anpassen" gesetzt ist. Vorgabewerte sind die
   gemessenen LUNOS-Werte, nicht Datenblattwerte.
4. **`CableComp`** — reine Logik, testbar:
   ```
   R  = Länge × ρ / A           ρ = 0,0175 Ω·mm²/m, A aus Parameter
   ΔV = I(Typ, Stufe) × R       I-Tabelle je Typ, Vorgaben aus Messung M2
   V  = V_soll + ΔV             immer positiv, auch unter 5 V
   ```
   Parameter je Knoten: Leitungslänge (0…30 m), Querschnitt (0,5/0,7/1,0/1,5 mm²),
   Schalter „vierte Ader vorhanden" → Kompensation aus.
   Grenzfall dokumentieren: bei 10,00 V ist keine Kompensation mehr darstellbar
   (§3.5). Firmware klemmt, meldet nichts — oder Stufe 4 nominal auf 9,90 V.
   **Entscheidung nach M2**, im Code als Konstante mit Kommentar.
5. **Kalibrierfaktor k** je Kanal (0,98…1,02), Parameter, Vorgabe 1,000. Wird in
   Phase 1 gemessen und hier eingetragen.
6. **Prozent-Eingang** zusätzlich zur Stufe (Stufe selbst als **DPT 5.100**): KO DPT 5.001 mit Schwellen
   (0 → 0, 1–25 → 1, 26–50 → 2, 51–75 → 3, 76–100 → 4) und Hysterese ±3 %, damit
   Visualisierungen mit Schieberegler funktionieren. Stufe und Prozent schreiben
   dasselbe Ziel; der letzte Empfang gewinnt.
7. **Richtung per KO** (DPT 1.x, A/B) — nur Übergangslösung bis Phase 4, dann als
   Richtungsart „über KO" beibehalten (wie in der Vorlage).
8. **Status-KOs** je Knoten: Stufe Status, Richtung Status, Ausgangsspannung
   (DPT 9.020 mV — gut zum Prüfen), Volumenstrom nominal (DPT 9.009 m³/h, aus
   Stufentabelle, vorzeichenbehaftet wie die Vorlage: positiv = Zuluft).
9. **Konsole:** `kwl st` (Übersicht), `kwl n1` (Detail), `kwl n1 s 3 a` (Test:
   Knoten 1, Stufe 3, Richtung A, verfällt nach 10 min wie in der Vorlage),
   `kwl n1 v 4.01` (Rohspannung, nur mit `OPENKNX_DEBUG`).

### Fertig, wenn

- [ ] Alle drei Lüfter laufen in allen Stufen, ego-Stoß nachweislich mit beiden
      Motoren gleichsinnig (Blatt Papier vor der Blende)
- [ ] Kompensation: Spannung am Lüfterende ±30 mV vom Sollwert bei 15 m Leitung
- [ ] Native Tests für `CableComp` und die Prozent→Stufe-Abbildung grün
- [ ] Ein Knoten mehr aktiviert als Kanäle vorhanden → Fehlercode 3, kein Absturz

**Offene Messungen, die hier einfließen:** M1 (Polarität: ist < 5 V Zuluft oder
Abluft? — entscheidet die Richtungsnamen in ETS und Status-KO), M2 (Strom je
Stufe → I-Tabelle), M3 (Totband → Warnschwelle im Kommentar), M5 (Eingangsstrom S).

---

## Phase 4 — Betriebsarten, Führungen, Richtungsbildung

**Ziel:** Das Gerät führt die Lüftung selbst. Sieben Betriebsarten, drei Führungen
(Temperatur, Feuchte, CO₂), Richtungsbildung mit Wärmerückgewinnung — und eine
Vorfahrtsregel, die entscheidet, wer gewinnt.

**Referenz für die Mechanik:** die Applikationsbeschreibung des Arcus-EDS
KNX-LUNOS-CONTROL4 (682x_d4a, 07.10.2024), Abschnitte 3.2–3.14. Das Modul ist seit
Jahren im Feld, seine Bedienlogik ist erprobt. Wo dieser Plan davon abweicht, steht
es dabei und hat einen Grund.

**Lesart nach dem Datenmodell in 0.6:** 4a und 4b laufen **je Raum** und liefern
Raumanforderung, Raumdeckel und Zyklusregel-Wunsch. 4c läuft **je Verbund** (Stufen-
regel, Takt) und **je Lüfter** (Richtung aus Phase, Anteil, Sperre, Schutz). Wo unten
„Knoten" steht, ist je nach Kontext Raum oder Lüfter gemeint; die KO-Tabellen im
Anhang sind bereits getrennt.

Phase 4 ist in drei Teile geschnitten. **4a zuerst**, weil die Arbitrierung das
Gerüst ist, in das 4b und 4c ihre Ergebnisse einhängen.

### Die Pipeline — gilt für jeden Knoten

```
  Betriebsart-Ebenen (4a), unterste gilt nur ohne höhere:
    Zwangsobjekt 1..3 (1-Bit, Laufzeit) ─┐
    Nacht (1-Bit, ohne Ablauf)           ├─► aktive Betriebsart ─► Parametersatz:
    Zwangsbetriebsart (20.102, Laufzeit) │      Grundstufe, Maximalstufe,
    Betriebsart (20.102, dauerhaft)      │      Führungen an/aus, Zyklusregel,
    Standard-Betriebsart (Parameter)    ─┘      Intervallbetrieb
                                                        │
  Führungen (4b):  Grenzwert-Treppe rF ──┐             ▼
                   Grenzwert-Treppe CO₂ ─┼─ max ─► Automatikstufe, begrenzt
                   Temperaturführung   ──┘          auf [Grund, Max] der Betriebsart
                   Feuchtevergleich: sperrt rF-Treppe, wenn außen feuchter
                                                        │
  Handstufe (4a): KO Stufe / Stufe % / Stufe +/-  ──── ersetzt Automatikstufe ──┐
                                                                                 ▼
  Gruppe (4c): Slave folgt Gruppen-Stufe ──────────────────────────── ersetzt ──┤
  Zu-/Abluftanforderung (4c): erzwingt Richtung, Stufe aus Parameter ─ ersetzt ─┤
  Schutz (4b): Frost / Hitze ──────────────────────────────── Schutzstufe ──────┤
  Sperren (4a): Sperre · fehlende Freigabe · Master-Timeout ──────── 5,00 V ────┤
                                                                                 ▼
                                                              wirksame Stufe 0…4
                                                                                 │
  Richtungsbildung (4c): Betriebsweise · Zyklusregel (Sommer = lang) · Takt · Anteil
                                                                                 ▼
                                                    Stufe + Richtung → KwlCurve → Volt
```

**Vorfahrt, von oben nach unten — Übernahme aus Arcus §3.14, angepasst:**

| Rang | Ebene | Bemerkung |
|---|---|---|
| 1 | **Sperre** (Fenster/Kamin, fehlende Freigabe, Master-Timeout) | immer 5,00 V, gewinnt gegen alles |
| 2 | **Schutz** (Frost/Hitze) | Schutzstufe, gewinnt gegen Hand und Gruppe — **Abweichung von Arcus**, dort ist Frostschutz nur eine Führung. Ein durchfrierender Raum ist kein Bedienfall. |
| 3 | **Zu-/Abluftanforderung** | erzwingt Richtung und Anforderungsstufe |
| 4 | **Gruppe** (nur Slave) | folgt Gruppen-Stufe und -Richtung |
| 5 | **Handstufe** | ersetzt die Automatikstufe, Laufzeit parametriert |
| 6 | **Automatikstufe** aus Führungen und aktiver Betriebsart | |
| 7…11 | Betriebsart-Ebenen: Zwangsobjekt 1 / 2 / 3 → Nacht → Zwangsbetriebsart → Betriebsart → Standard | bestimmen, *welcher* Parametersatz in Rang 6 gilt |

**Rücksetz-Semantik (Arcus §3.14, übernommen):** Ändert sich ein Objekt der Ränge 5
bis 11, werden alle Objekte mit höherem Rang bis einschließlich 5 zurückgesetzt —
jede Nutzeraktion bekommt eine sichtbare Reaktion. Ränge 1 bis 4 sind ausgenommen.
Nach Ablauf oder Rücknahme fällt das Gerät auf die nächste noch aktive Ebene zurück.
Die beiden Arcus-Bedienbeispiele sind Testfälle:
- Auto → Nacht → Zwangsobjekt 1: Zwangsobjekt 1 aktiv; nach Ablauf wieder Nacht.
- Auto → Zwangsobjekt 1 → Nacht: Nacht löscht Zwangsobjekt 1; nach Rücknahme Auto.

Die Vorfahrt ist im Code **eine** Funktion `KwlNode::effectiveStage()` und wird als
Tabelle in der Applikationsbeschreibung abgedruckt. Jede Zeile ist ein Testfall.

---

### 4a — Betriebsarten und Arbitrierung

**Sieben Betriebsarten**, wie Arcus (vier nach KNX-Standard, drei erweiterte):

| # | Betriebsart | DPT 20.102 | Vorgabe Grundstufe | Vorgabe Maximalstufe |
|---|---|---|---|---|
| 1 | Komfort | 1 | 1 | 4 |
| 2 | Standby | 2 | 1 | 2 |
| 3 | Eco / Nacht | 3 | 1 | **1** |
| 4 | Frost-/Gebäudeschutz | 4 | 0 | 1 |
| 5 | Stoßlüften | erweitert | 4 | 4 |
| 6 | Temperatur-Absenkung | erweitert | 1 | 2 |
| 7 | Ruhe (Aus) | erweitert | 0 | 0 |

**Abweichung von Arcus:** dort hat jede Betriebsart *eine* Lüfterstufe, die die
Führungen anheben dürfen. Hier Grundstufe **und** Maximalstufe — die Obergrenze ist
für Nacht im Schlafzimmer der eigentliche Zweck des Projekts.

**Parametersatz je Betriebsart, je Knoten** (Arcus §3.3, erweitert):

| Parameter | Bereich | Bemerkung |
|---|---|---|
| Grundstufe | 0…4 | läuft immer, auch ohne Anforderung |
| Maximalstufe | 0…4 | Deckel für alle Führungen |
| Temperaturführung | aus / an | 4b |
| Frostschutz | aus / an | 4b, in Schutz immer an |
| Feuchteführung (rF-Treppe) | aus / an | 4b |
| Entfeuchtung (Feuchtevergleich) | aus / an | 4b |
| CO₂-Führung | aus / an | 4b |
| Zyklusregel | WRG (kurz) / Sommer (lang) / über Sommer-KO / Zuluft / Abluft | 4c |
| Intervallbetrieb | aus / an | s. u. |
| Nachlauf nach Anforderungsende | 0…60 min | |

**Intervallbetrieb** (Arcus §3.7): innerhalb einer Periode wird für eine Aktivzeit
auf Grundstufe gelüftet, sonst 5,00 V. Vorgaben 1 h / 15 min, 4 h / 30 min,
12 h / 60 min, frei parametrierbar. Für Standby und Abwesenheit.

**Betriebsart-Eingänge** je Knoten, per Parameter auch „folgt Knoten 1":

| KO | DPT | Wirkung | Laufzeit |
|---|---|---|---|
| Betriebsart | 20.102 | dauerhaft, von Schaltuhr oder Zentrale. 0 = Auto → Standard-Betriebsart (Parameter, Vorgabe Standby) | — |
| Zwangsbetriebsart | 20.102 | überlagert für die Laufzeit | 0…240 min, 0 = unendlich |
| Nacht | 1.003 | überlagert, ohne Ablauf | — |
| Zwangsobjekt 1 | 1.003 | frei belegbar, Vorgabe **Stoßlüften** | 0…240 min, Vorgabe 30 |
| Zwangsobjekt 2 | 1.003 | frei belegbar, Vorgabe **Ruhe** | Vorgabe 0 |
| Zwangsobjekt 3 | 1.003 | frei belegbar, Vorgabe **Komfort** | Vorgabe 30 |
| Priorität der Zwangsobjekte | Parameter | gleich (letztes gewinnt) / hierarchisch 1 < 2 < 3 | |

Die drei Zwangsobjekte ersetzen feste Stoßlüften- und Ruhe-KOs. Wer Stoßlüften auf
einen Taster legt und Ruhe auf den Fensterkontakt, parametriert das — statt dass die
Firmware es vorgibt.

**Handstufe** (Arcus §3.6):

| KO | DPT | Wirkung |
|---|---|---|
| Stufe manuell | **5.100** | 0…4, Laufzeit 0…240 min (Vorgabe 60), 0 = dauerhaft |
| Stufe manuell % | 5.001 | über die Prozent→Stufe-Abbildung aus Phase 3 |
| Stufe +/− | 1.007 | Schritt hoch/runter, für Taster |
| Handbetrieb aktiv | 1.012 | **Ein- und Ausgang**: 1 = Hand aktiv; eine 0 vom Bus beendet den Handbetrieb sofort |

DPT 5.100 ist der KNX-Datenpunkttyp „Lüfterstufe" — nicht 5.010 (Zähler) wie in
früheren Fassungen dieses Plans.

**Sperren:** KO Sperre (1.003, Fenster/Kamin, nicht selbsthaltend), Freigabe-Latch
und Master-Überwachung aus der Vorlage.

**Fertig, wenn:**
- [ ] Native Tests: jede Zeile der Vorfahrtstabelle, beide Arcus-Bedienbeispiele,
      Priorität gleich/hierarchisch, Laufzeitablauf jeder Ebene
- [ ] Betriebsartwechsel per 20.102, Zwangsbetriebsart mit Ablauf, Nacht-Schaltobjekt,
      Zwangsobjekt-Kette — Status-KO folgt jeweils
- [ ] Handbetrieb: setzen, +/−, per 0 auf „Handbetrieb aktiv" beenden
- [ ] Verbund: Raum 1 Nacht (Deckel 1) + Raum 2 CO₂ über GW3 → beide Lüfter Stufe 1;
      Raum 1 Standby (Deckel 2) → beide Stufe 2; Regel „Raum 2 führt" → beide Stufe 3

---

### 4b — Führungen und Schutz

Alle Sensorwerte kommen über KNX. Jeder Sensor-KO hat eine Überwachungszeit
(Parameter, Vorgabe 60 min); fehlt der Wert, gilt der Parameter „bei fehlenden
Messwerten" (weiterlüften auf Grundstufe / sperren). Außenwerte per Parameter von
Knoten 1 übernehmbar. Jede Führung hat ein KO „… aktiv" (1.003), mit dem sie vom Bus
mit 0 abgeschaltet wird (Arcus Obj 13/16/18).

**Sensor-KOs je Knoten:** rF innen (9.007), T innen (9.001), rF außen (9.007), T außen
(9.001), CO₂ (9.008), T Soll (9.001).

#### Grenzwert-Treppe — der Mechanismus für rF und CO₂

Übernommen aus Arcus §3.13, von drei auf vier Stufen erweitert: **fünf Grenzwerte
für vier Stufen.** Grenzwert n schaltet Stufe n ein (n = 1…4), Grenzwert n−1 schaltet
sie wieder aus. Ist eine höhere Stufe aktiv, ändert das Überschreiten eines
niedrigeren Grenzwerts nichts. Die Hysterese ist der Abstand der Grenzwerte.

```
Stufe 4 ─────────────────────────────────────────┐ ein bei GW4
Stufe 3 ──────────────────────────────┐          │ aus bei GW3
Stufe 2 ───────────────────┐          │ ein GW3  │
Stufe 1 ────────┐          │ ein GW2  │ aus GW2  │
Stufe 0         │ ein GW1  │ aus GW1  │          │
                │ aus GW0  │          │          │
           GW0  GW1       GW2        GW3        GW4
```

Ein Mechanismus, eine Klasse `StageLadder`, eine Testdatei. Ersetzt P-Regler und
Zweipunkt aus früheren Fassungen — für einen Lüfter mit vier Stufen ist ein
Prozent-Regler mit anschließender Stufenabbildung ein Umweg.

| Führung | GW0 | GW1 | GW2 | GW3 | GW4 | Bereich |
|---|---|---|---|---|---|---|
| rF innen [%] | 45 | 50 | 55 | 60 | 65 | 0…90 |
| CO₂ [ppm] | 700 | 850 | 1000 | 1300 | 1700 | 500…4000 |
| VOC [ppb] | 300 | 500 | 750 | 1000 | 1500 | 0…5000 |
| VOC-Index | 100 | 150 | 200 | 300 | 400 | 0…500 |

VOC ist einheitenfrei: Parameter „VOC-Eingang als" ppb (9.008) / Index (5.010 oder
7.001). Die Treppe ist dieselbe.

Arcus-Vorgaben zum Vergleich (drei Stufen): rF 48/53/58/62, CO₂ 800/1000/1500/2000.

#### Feuchtevergleich — Entfeuchtung, Kellertrocknung, Taupunktschutz

Lüften trocknet nur, wenn die Außenluft weniger Wasser enthält. Verglichen wird der
**Wasserdampf-Partialdruck**, nicht rF und nicht g/m³:

```
e_s(T) = 6.112 · exp(17.62·T / (243.12 + T))     Sättigungsdampfdruck [hPa]
e      = rF/100 · e_s(T)                          Partialdruck [hPa]
```

Der Partialdruck ist luftdruckunabhängig und vergleicht innen/außen korrekt auch bei
verschiedenen Temperaturen — g/m³ täte das nicht, g/kg (Arcus) bräuchte den Luftdruck.
Für die **Anzeige** in g/kg (KO, DPT 9.029) wird der Luftdruck aus dem Parameter
„Höhe über Meer" (0…2000 m, Vorgabe 500) geschätzt, Formeln im Anhang.

Regel: `e_innen − e_außen ≥ Δe_ein` (Vorgabe 1,5 hPa ≈ 1 g/kg) → Entfeuchtung wirkt,
rF-Treppe erlaubt. `≤ Δe_aus` (Vorgabe 0,5 hPa) → **Feuchtevergleich sperrt** die
rF-Treppe und die Entfeuchtung; Grundstufe bleibt. Fehlercode 7 ohne Alarm, Status-KO.
Arcus deaktiviert die rF-Führung automatisch, sobald außen feuchter ist — dasselbe.

#### Temperaturführung

Parameter (Arcus §3.10): **Frostschutz [°C]** 5…16 (Vorgabe 8) — Ti darunter →
Lüftung aus (Schutz, Rang 2), Rückkehr bei +2 K. **Temperaturabstand [K]** 0…10
(Vorgabe 3) — erst wenn |Ta − Ti| größer ist, versucht die Führung, Tsoll durch
Wechsel zwischen WRG- und Sommerzyklus zu erreichen. Ein größerer Abstand vermeidet
Konflikte mit der Heizungsregelung.

| Fall | Bedingung | Stufenanforderung | Zyklusregel (an 4c) |
|---|---|---|---|
| Freie Kühlung | Ti > Tsoll + 1 K und Ta < Ti − Abstand | Stufe „Kühlung" (Vorgabe 3) | Sommer (lang) |
| Wärmeerhalt | Ti < Tsoll und Ta < Ti − Abstand | keine | WRG (kurz) |
| Warmluft nutzen | Ti < Tsoll − 1 K und Ta > Ti + Abstand | Stufe „Heizung" (Vorgabe 2) | Sommer (lang) |
| sonst | | keine | Betriebsart |

**Hitzeschutz:** Ti > T_hitze (Vorgabe 30 °C) und Ta > Ti → Schutz.

**Sperrausgang Heizung** (Arcus „Master-Verhalten"): optional sendet der Knoten
„Temperaturführung aktiv" als Ausgang = 1, solange er frei kühlt — damit der
Heizungsaktor in der Übergangszeit nicht gegenheizt. Parameter Ein-/Ausgang.

**Fertig, wenn:**
- [ ] Native Tests: `StageLadder` alle Übergänge auf und ab, Partialdruck gegen
      Prüfwerte, g/kg-Umrechnung, Temperaturregelwerk alle vier Fälle mit Hysterese
- [ ] Sensor-Timeout provoziert → Verhalten wie parametriert
- [ ] Bad: Duschen → rF über GW3 → Stufe 3 → unter GW2 → Stufe 2 → Nachlauf →
      Grundstufe

---

### 4c — Richtungsbildung, Wärmerückgewinnung, Gruppe

Beim e² ist die Wärmerückgewinnung die Pendelbewegung — kein Schalter. Deshalb ein
Block für Richtung, WRG und Gruppe.

**Zyklusregel** — die zentrale Korrektur gegenüber früheren Fassungen: **Sommerbetrieb
ist kein Einrichtungsbetrieb, sondern ein langer Zyklus.** Arcus stellt die
Reversierzeit im Sommer auf 1 h statt 70 s. Der Regenerator sättigt nach etwa einer
Minute; bei einer Stunde Zykluszeit läuft der Lüfter praktisch ohne WRG, wechselt
aber weiter die Richtung — kein dauerhafter Über- oder Unterdruck, beide Räume
bekommen Frischluft.

| Zyklusregel | Zykluszeit je Richtung | WRG | Verwendung |
|---|---|---|---|
| WRG (kurz) | je Stufe parametrierbar, Vorgabe **70 s** (Arcus, LUNOS) | ja | Heizperiode |
| Sommer (lang) | Parameter, Vorgabe **1 h**, Bereich 40 s…2 h | praktisch nein | Sommer, freie Kühlung |
| über Sommer-KO | KO Sommer (1.001) schaltet zwischen den beiden | | Standard |
| Zuluft | fest, kein Wechsel | nein | externen Ablüfter unterstützen |
| Abluft | fest, kein Wechsel | nein | Abluftgerät |

Arcus erlaubt die Zykluszeit **je Stufe** (40 s…2 h) und warnt, dass bei Änderung der
Vorgaben die erreichbare Wärmerückgewinnung nicht mehr gewährleistet ist. Beides
übernehmen — Parameter je Stufe, Warnhinweis in der Kontexthilfe.

**Betriebsweise-KO** (5.010): 0 = auto (Zyklusregel der Betriebsart), 1 = WRG,
2 = Zuluft, 3 = Abluft. Direkte Vorgabe, Rang 3. Für Zuluftbetrieb einzeln und
Abluftbetrieb einzeln.

**Abluftanforderung** (1.003, Arcus §3.8) — die Badlüftung: Knoten fährt nach
**Vorlaufzeit** (0 s…5 min) in Abluft mit „Stufe bei Abluftanforderung" (Vorgabe 4;
beim ego Stufe 4 = Abluftstoß beider Motoren), nach Wegnahme **Nachlaufzeit**
(0…60 min, Vorgabe 15) weiter, dann zurück. Optional intermittierend (Aktivzeit /
Periode, Arcus: 1 min/5 min bis 10 min/30 min).

**Zuluftanforderung** (Ausgang 1.003, Arcus Obj 22) — **neu, ergibt sich aus dem
Haus:** solange ein Knoten Abluft fährt, sendet er Zuluftanforderung = 1. Partner
in derselben Gruppe mit Parameter „folgt Zuluftanforderung" schalten für die Dauer
auf Zuluft, damit nachströmt. Der ego im Bad zieht 45 m³/h ab, die beiden e²60
liefern nach — sonst pfeift es an den Fenstern.

**Gruppe — Pendelbetrieb** (Vorlage und Arcus Obj 26/27):
- Master sendet Gruppen-Stufe (5.100), Gruppen-Richtung/Takt (1.012) und
  Lebenszeichen; Slaves folgen mit ihrer **Phase** (0/1) und ihrem **Anteil**.
- **Anteilsfaktor** je Knoten (Parameter 25…100 %, Vorgabe 100): skaliert die
  Gruppen-Stufe. Arcus braucht ihn für drei unpaarige e²60 (einer voll, zwei
  halb); hier für Feinabstimmung, wenn zwei Räume verschieden groß sind. Die
  Vorlage hatte ihn, frühere Fassungen dieses Plans hatten ihn verloren.
- **Balance-Hinweis** (Arcus §5.3) in die Applikationsbeschreibung: Zuluft- und
  Abluftseite müssen sich die Waage halten, sonst gibt es weder Luftaustausch noch
  WRG.
- **Totzeit** vor jedem Wechsel 5,00 V, Vorgabe 2 s. Beim ego beide Motoren
  gleichzeitig; er pendelt intern und kann trotzdem Slave sein.
- **Richtungsnamen** in der ETS: Zuluft / Abluft. Welche Spannungshälfte welche ist,
  entscheidet Messung M1 → `FANDRV_BELOW_5V_IS_SUPPLY` im Board-Header.

**Richtungskonflikt** (ergänzt 2026-09-23, vorher offen): Ein Verbund hat genau
**eine** Richtung — die der Phase 0; Phase 1 ist immer die Gegenrichtung. Was in den
einen Raum hineingedrückt wird, muss aus dem anderen heraus. Jede Forderung (Rang 3
oder feste Betriebsweise) wird deshalb in die Richtung übersetzt, die sie für Phase 0
bedeutet: ein Lüfter in Phase 1, der Abluft fordert, verlangt Zuluft für Phase 0.

- Stimmen alle übersetzten Forderungen überein, gibt es **keinen** Konflikt — auch
  wenn die Räume verschiedene Richtungen nennen. Das ist der Normalfall der
  Zuluftanforderung.
- Gehen sie auseinander, **führt die höhere Stufe**; bei Gleichstand die niedrigere
  Raumnummer. Der unterlegene Raum meldet **Fehlercode 11** ohne Alarm und bleibt
  im Status sichtbar. Der Konflikt wird gemeldet, nicht versteckt — auflösen muss ihn
  der Mensch, etwa durch eine andere Verbundzuordnung.
- Auch der Weg in eine erzwungene Richtung läuft über die **Totzeit**
  (Sicherheitsinvariante 5).

**Busausfall:** DACs halten den letzten Wert. Bei Rückkehr Zustand erneut schreiben.
**12-V-Ausfall bei stehendem Bus:** Adressabfrage fehlgeschlagen → Fehlercode 4,
Alarm, LED rot; bei Rückkehr `begin()` und Zustand neu schreiben.

**Fertig, wenn:**
- [ ] Pendelpaar 24 h synchron, 70 s, Totzeit sichtbar 5,00 V
- [ ] Sommer ein → Zykluszeit 1 h, Wechsel findet weiter statt
- [ ] Abluftanforderung am ego → nach Vorlauf Stufe 4 beide Motoren Abluft →
      Zuluftanforderung = 1 → e²60-Paar auf Zuluft → Nachlauf → alles zurück
- [ ] Betriebsweise-KO Abluft auf Knoten 1 → nur Knoten 1, Partner pendelt weiter
- [ ] Native Tests: Takt-Zustandsmaschine mit Zykluszeit je Stufe, Anteil,
      Zuluftanforderungs-Kette

---

## Phase 5 — Sicherheit, Persistenz, EEPROM, Diagnose

**Ziel:** Das Gerät ist verteilerfest. Alles, was im Fehlerfall passiert, ist
definiert und dokumentiert.

1. **Kein EEPROM-Einschaltwert.** Entschieden 2026-09-25 (Messprotokoll Phase 1,
   Befund B1): Die Store-Sequenz verstellt auf einem Bus mit zwei GP8413 den nicht
   adressierten Chip dauerhaft, und der Schaden ist nicht rückgängig zu machen.
   `kwl store`, `Gp8413Store`, der ETS-Parameter „Einschaltwert ohne Bus (EEPROM)"
   und sein Hilfetext sind gestrichen. **Das Restrisiko gehört in die
   Applikationsbeschreibung:** Nach Wiederkehr der 12 V ohne Bus laufen alle Lüfter
   mit Volllast in einer Richtung, bis die Firmware startet; ohne Bus dauerhaft.
   Abhilfe nur über Hardware — DAC-Versorgung erst mit dem RP2040 freigeben oder
   getrennte I²C-Stränge — für Rev 0.2 vorgemerkt, Referenzdesign O19.

   Aus dem Versuch für `Gp8413Drive` (alles im Messprotokoll belegt):
   - Anwesenheit per **Schreibzugriff** (Register 0x01 ← 0x11) prüfen, nicht per
     Lesezugriff: U3 quittierte nach Kaltstart Lesezugriffe zeitweise nicht, einen
     Schreibzugriff sofort. Der RP2040 kann keinen Schreibzugriff ohne Nutzdaten
     senden, deshalb der Bereichsschreibzugriff als Abfrage; er ist idempotent.
   - Adressabfrage mit **Wiederholung** (3 × 10 ms), nicht Fehler nach dem ersten NACK.
   - Registerfile 16 Bytes, Zeiger modulo 16, obere Nibble der Registeradresse
     ignoriert: Mehrbyte-Frames dürfen nie über 0x05 hinauslaufen, sonst landen sie
     in 0x00…0x04 (Bereich und Kanal 0).
   - Nach Mehrbyte-Frames eine kurze Pause, bis geklärt ist, warum dicht gesendete
     Frames verloren gingen (E3b, Durchgang 1/2 gegen 3).
   - Ein verstellter Abgleich ist im Betrieb nicht erkennbar (O6). Der einzige
     Schutz davor ist, die Ursache nie zu erzeugen.
2. **Störungs-KO und Fehlercode** je Knoten wie in der Vorlage (DPT 1.005 Alarm,
   DPT 5.010 Code), Prioritätsliste erweitert um „DAC nicht erreichbar".
3. **Betriebsstunden** je Knoten, persistent, wie Vorlage. Zusätzlich
   **Taktzähler** (Anzahl Richtungswechsel) — Verschleißindikator.
3a. **Filterwechselanzeige** je Knoten, persistent:
   - Parameter „Zählweise": Laufzeit (Stunden mit Stufe > 0) oder
     **volumenstromgewichtet** (Σ Stufe-Volumenstrom × Zeit → m³; ein Filter setzt
     sich nach Luftmenge zu, nicht nach Uhr). Vorgabe: volumenstromgewichtet.
   - Parameter „Wechselintervall": 500…8760 h bzw. 10 000…500 000 m³. Vorgabe
     entspricht LUNOS-Empfehlung ~3 Monate Dauerbetrieb Stufe 2.
   - KOs (Arcus Obj 28/29): **Filterwechsel fällig** (1.005, Ein-/Ausgang — eine 0
     vom Bus quittiert kurzfristig, nach 24 h meldet es sich wieder), **Restlaufzeit**
     (5.001 %, sendet bei Änderung um 5 %), **Filterwechsel-Quittung** (1.016,
     setzt den Zähler zurück; auch per Konsole `kwl n1 filter reset`).
3b. **Bedienzustände persistent** (Arcus §5.4: alle über den Bus geänderten Werte
   bleiben erhalten): Betriebsart (KO 0), Sommer, Nacht und die Zwangsobjekt-
   Zustände mit Restlaufzeit werden im Modulflash gehalten und nach Neustart
   wiederhergestellt. Sonst fiele das Gerät nach jedem Busausfall in die
   Standard-Betriebsart, ohne dass die Schaltuhr es merkt. Schreiben nur bei
   Änderung, gedrosselt auf einmal je Minute.
   - Flash-Schreibung wie Betriebsstunden: alle 30 min in den Modulbereich, nicht
     bei jeder Änderung.
4. **Suspendieren** je Knoten (Wartung): 5,00 V, Überwachung aus, Fehlercode 6.
   Aus der Vorlage.
5. **Watchdog** in der Release-Umgebung (`OPENKNX_WATCHDOG`), wie Vorlage.
6. **Status-LEDs:** Info1 Gerätestatus, Info2/3 per ETS wählbar auf Knoten 1…3
   (Basis 110). Farben wie Vorlage: grün A, blau B, rot Fehler, dunkel Stillstand.
   Die eigene Platine hat andere LEDs als REG1-Front — im Board-Header prüfen, was
   OGM-Common dafür braucht.
7. **`processBeforeRestart()`:** alle Knoten auf 5,00 V, dann Neustart. Kein Lüfter
   darf über einen Firmware-Reset hinweg auf Vollgas stehen.

### Fertig, wenn

- [ ] Kaltstart ohne Bus: Dauer des Vollgas-Moments bis `setup()` gemessen und in
      der Applikationsbeschreibung beziffert
- [ ] Jeder Fehlercode einmal provoziert und auf dem Bus gesehen
- [ ] ETS-Neuprogrammierung mitten im Betrieb → kein Vollgas-Moment (Oszilloskop)

---

## Phase 6 — Dokumentation und Release

1. **Applikationsbeschreibung** `doc/Applikationsbeschreibung-Kwl.md` nach dem Muster
   der Vorlage: Grundbegriffe, Betriebsfälle (Pendelpaar / Einzelbad / externer
   Master), dann Parameter für Parameter. Sie ist zugleich die Quelle der ETS-
   Kontexthilfe (`Baggages/Help_de/*.md`, eine Datei je Parameter, Dateiname =
   Präfix + Parametername).
2. **Inbetriebnahmeanleitung** aus Prüfliste D und Referenzdesign §7.
3. **`ReplacesVersions`** nur die eigene Version, wie die Vorlage begründet.
4. **Release-Umgebung** in `platformio.custom.ini`, Watchdog an, Debug aus.
5. **GitHub-Release** mit knxprod + uf2, `dependencies.txt` eingefroren.

---

## Zeitplan bis zur Platinenankunft (zwei Wochen)

Die Platinen sind in Fertigung. Bis sie da sind, entstehen die ETS-Applikation und
die gesamte hardwareunabhängige Logik — das ist mehr als die Hälfte des Plans.
Voraussetzung: der Producer braucht `knxprod.h`, die Firmware braucht `knxprod.h`,
also **XML zuerst**.

### Kein Entwicklungsaufbau

Alle Hardwarepunkte (Phase 1, P4–P15) werden an der fertigen Platine gemessen. Bis
dahin: ETS-Applikation und Logik. Der Eintrag „Entwicklungsaufbau Pico + DFR1073" in
der Hardwareauswahl bleibt als Option erhalten, wird aber nicht beschafft.

### Woche 1 — ETS-Applikation

| Tag | Schritt | Ergebnis |
|---|---|---|
| 1 | Repos anlegen, Skelett aus der Vorlage, `Restore-Dependencies.ps1`, `reference/` mit Vorlage und Arcus-PDF, `CLAUDE.md`, `docs/PLAN.md` | Producer läuft, leere Applikation baut |
| 1 | **Einfrieren:** ApplicationNumber, Präfixe ROOM/FAN, KoOffsets 20/340/640, Blockgrößen 40/24, Hardware-Enum | steht in `Kwl.xml` und im Plan |
| 2 | `Fan.share.xml` (Hardwareauswahl, Verbund-Parameter ×8) und `Fan.templ.xml` (Lüfter: Typ, Raum, Verbund, Phase, Anteil, Kompensation, Kalibrierung, Status-KOs) | Lüfterseiten in der ETS sichtbar, Sichtbarkeit folgt der Hardwareauswahl |
| 3 | `Room.share.xml` (Höhe über Meer, Hysterese) und `Room.templ.xml` Teil 1: Sensoren, Führungen mit Grenzwert-Treppen, Feuchtevergleich, Temperaturführung | Raumseiten Sensorik |
| 4 | `Room.templ.xml` Teil 2: sieben Betriebsarten je mit Parametersatz, Zwangsobjekte, Handstufe, Intervall, Abluftanforderung, Sperren | Raumseiten Betriebsarten |
| 5 | Sichtbarkeitslogik durchklicken (Typ e²/ego/RA/generisch, Betriebsart-Parametersätze, Verbund-Regeln), Vorgabewerte gegen den Plan prüfen, `OpenKNXproducer check` | knxprod ohne Warnungen, Testprojekt in der ETS mit 3 Räumen und Rev-0.1-Hardware |

Claude Code schreibt das XML nach dem Muster der Vorlage (`Fan.templ.xml` ist 644
Zeilen Referenz für Syntax, `%C%`-Makros, Sichtbarkeitsbedingungen, Enum-Typen). Die
Beurteilung der Seiten machst **du** in der ETS — dafür gibt es keinen Test.

### Woche 2 — Logik und Skelett

| Tag | Schritt | Ergebnis |
|---|---|---|
| 6 | Native Tests + Implementierung: `KwlCurve` (20 Tabellenwerte, beide Formate, Klemmung), `CableComp` | grün |
| 7 | `StageLadder` (alle Übergänge), Magnus/Partialdruck/g/kg (Prüfwerte), Temperaturregelwerk | grün |
| 8 | `effectiveStage()`: Rangtabelle Zeile für Zeile, beide Arcus-Bedienbeispiele, Laufzeitabläufe, Priorität gleich/hierarchisch | grün — der wichtigste Testtag |
| 9 | `KwlGroup`: Stufenregel (drei Varianten), Zyklusregel-Konflikt, Takt mit Zykluszeit je Stufe, Totzeit, Phase, Anteil, Nachströmung | grün |
| 10 | Firmware-Skelett: beide Module, Board-Header Rev 0.1 **und** DevPico, `IDacDrive` mit Mock, Startup-Sequenz, Konsole `kwl st`; `pio run -e develop_DevPico` | kompiliert; ohne `FANDRV_DAC_LEFT_ALIGNED` bricht der Build wie vorgesehen |
| 11–12 | `test_dac`-Firmware für Phase 1 vorbereiten (Konsole, `raw`, `probe`, `store` mit Rückfrage) — läuft erst an der Platine | Phase 1 startet am Tag der Ankunft ohne Vorlauf |
| 12–14 | Baggages/Hilfetexte aus der Parameterspezifikation, Applikationsbeschreibung Grundgerüst, Konsolenbefehle `kwl st` / `kwl r1` / `kwl f1` | Doku wächst mit der Applikation, nicht hinterher |

### Wenn die Platine kommt

Reihenfolge am ersten Tag: Bestückung sichtprüfen → P4 (Isolation `BCU_GND`/`PGND`)
→ 12 V anlegen ohne Bus, P1–P3 → **Phase 1 mit `test_dac`**: P6 (O17), P7, P10, P11,
P15 → `FANDRV_DAC_LEFT_ALIGNED` in den Board-Header → erst dann die eigentliche
Firmware bauen und per ETS programmieren. Die ETS-Applikation ist zu dem Zeitpunkt
fertig, die Logik getestet; es fehlt nur die eine gemessene Zahl.

### Startprompt für die erste Claude-Code-Sitzung

> Lies CLAUDE.md, docs/PLAN.md und docs/ETS_Parameterspezifikation.md vollständig.
> Wir sind in Woche 1, Tag 1. Lege die
> Repository-Struktur aus Abschnitt 0.2 an, kopiere aus reference/OAM-FanControl das
> Build-Skelett (platformio.ini, platformio.custom.ini, restore/, .gitignore, LICENSE)
> und benenne um. Erzeuge Kwl.xml mit den op:define-Einträgen aus Phase 2 Schritt 7
> und leere Room.share/templ und Fan.share/templ, die der Producer ohne Warnung baut.
> Ändere nichts an den KoOffsets und Blockgrößen aus 0.6 — die sind eingefroren.
> Frag nach, bevor du die ApplicationNumber festlegst.


## Arbeiten mit Claude Code

**Vorbereitung im Repo:**
- `CLAUDE.md` in die Wurzel (Datei liegt bei) — enthält die Sicherheitsinvarianten
  und die Regeln, die in jeder Sitzung gelten
- `docs/PLAN.md` (dieses Dokument), `docs/Referenzdesign_Rev4_GP8413.md`,
  `docs/Pruefliste_KNXFANDRV_Rev01.md`
- `reference/OAM-FanControl/` und `reference/OFM-FanControl/` als Lesekopie der
  Vorlage, nicht im Build

**Sitzungsstart:** „Lies CLAUDE.md und docs/PLAN.md. Wir sind in Phase N, Schritt M.
Vorher: Was ist der Stand, was fehlt für die Fertig-Definition?"

**Arbeitsregeln, die sich bewährt haben:**
- **Tests zuerst** für alle reine Logik (`KwlCurve`, `CableComp`, Stufenabbildung,
  Takt). Das ist die Schicht, in der ein Fehler einen Lüfter auf Vollgas stellt, und
  die einzige, die ohne Hardware prüfbar ist.
- **Ein Commit je Schritt**, Commit-Nachricht nennt Phase und Schritt.
- **Vor jedem Flashen** die Frage stellen lassen: „Welcher Ausgangszustand liegt an,
  wenn diese Firmware startet?" Wenn Claude Code das nicht aus dem Code beantworten
  kann, ist der Code nicht fertig.
- **Kein `store`.** Die Sequenz ist aus der Firmware gestrichen (Befund B1,
  2026-09-25). In `test_dac` existiert sie nur hinter `FANDRV_TESTDAC_ALLOW_STORE`,
  nur für Platine 1, nur nach Rückfrage.
- **Die Vorlage ist Lesestoff, keine Kopiervorlage.** Konzepte übernehmen, Code
  neu schreiben, außer bei klar abgegrenzten Stücken (Taupunktformel, Totband-
  Sendebedingung), die dann mit Herkunft kommentiert werden.
- **Bei Widersprüchen zwischen Datenblatt, Bibliothek und Messung** gilt die
  Reihenfolge aus Referenzdesign Anhang B: gemessen vor Bibliothek vor Datenblatt.
  Claude Code soll das wissen, damit es nicht das Datenblatt „korrigiert".

**Was Claude Code nicht kann und du selbst machst:** OpenKNXproducer und ETS
bedienen, messen, flashen und hinsehen. Claude Code liefert die Firmware und die
XML; die Schleife über die Hardware schließt der Mensch.

---

## Anhang — Zahlen, die Claude Code braucht

### Registersatz GP8413

| Register | Inhalt |
|---|---|
| 0x01 | Bereich: 0x00 = 0–5 V, **0x11 = 0–10 V** |
| 0x02 | VOUT0 (2 Bytes) — oder VOUT0+VOUT1 (4 Bytes) |
| 0x04 | VOUT1 (2 Bytes) |

Adressbyte `1011 A2 A1 A0 W` → 0x58 (U2), 0x59 (U3). Bytes Low zuerst.
Code = round(V × 3276.7), 0…0x7FFF. Wire = Code oder Code << 1 (Phase 1 entscheidet).

### Sollspannungen (Nominal, ohne Kompensation)

**e²60**, Codierschalter 5 — A / B:
0: 5,00/5,00 · 1: 4,01/6,01 · 2: 3,21/6,91 · 3: 1,96/8,15 · 4: 0,01/10,00

**ego**, Codierschalter 9 — M1 / M2:
0: 5,00/5,00 · 1: 3,26/6,85 · 2: 2,09/8,00 · 3: 1,25/8,85 · **4: 1,11/1,11**

**RA 15-60**, Codierschalter 0, unipolar:
0: 0,01 · 1: 1,58 · 2: 3,57 · 3: 5,76 · 4: 8,15

Volumenstrom nominal m³/h — e²60: 0/5/20/40/60 · ego: 0/5/10/20/45 · RA: 0/15/30/45/60.

### Ströme für die Kompensation (Vorgabe, durch M2 zu ersetzen)

e²60: 0,033 A (St. 1) … 0,275 A (St. 4). ego: bis 0,41 A. R = 0,025 Ω/m bei 0,7 mm².

### KO-Blöcke (Planung): Raum 40 KOs, Lüfter 24 KOs

ROOM: KoOffset 20, 8 Räume → 20…339. FAN: KoOffset 340, 12 Lüfter → 340…627.
Logikmodul auf KoOffset 640. Stufen in **DPT 5.100**. Die Blockgrößen 40/24 sind mit
der ersten knxprod eingefroren — eine spätere Änderung verschiebt alle
Gruppenadressen der Anwender.

**Raum-Block (40)**

| # | R | KO | DPT | Arcus |
|---|---|---|---|---|
| 0 | E | Betriebsart (dauerhaft) | 20.102 | Obj 0 |
| 1 | E | Zwangsbetriebsart (Laufzeit) | 20.102 | Obj 1 |
| 2 | E | Nacht | 1.003 | Obj 2 |
| 3 | E | Zwangsobjekt 1 (Vorgabe Stoßlüften) | 1.003 | Obj 3 |
| 4 | E | Zwangsobjekt 2 (Vorgabe Ruhe) | 1.003 | Obj 4 |
| 5 | E | Zwangsobjekt 3 (Vorgabe Komfort) | 1.003 | Obj 5 |
| 6 | E | Stufe manuell | 5.100 | Obj 6 |
| 7 | E | Stufe manuell % | 5.001 | Obj 6 |
| 8 | E | Stufe +/− | 1.007 | Obj 8 |
| 9 | E/A | Handbetrieb aktiv (0 beendet) | 1.012 | Obj 7 |
| 10 | E | Sommer | 1.001 | Obj 9 |
| 11 | E | Betriebsweise (auto/WRG/Zuluft/Abluft) | 5.010 | Obj 21 |
| 12 | E | Abluftanforderung (Vor-/Nachlauf) | 1.003 | Obj 19 |
| 13 | E | Sperre Fenster/Kamin | 1.003 | Obj 25 |
| 14 | E | rF innen | 9.007 | Obj 14 |
| 15 | E | T innen | 9.001 | Obj 10 |
| 16 | E | rF außen | 9.007 | Obj 15 |
| 17 | E | T außen | 9.001 | Obj 11 |
| 18 | E | CO₂ | 9.008 | Obj 17 |
| 19 | E | VOC / Luftgüte (2 Byte: ppb 9.008 oder Index 7.001) | 9.008 / 7.001 | — |
| 20 | E | T Soll | 9.001 | Obj 12 |
| 21 | E/A | Temperaturführung aktiv (E: 0 sperrt · A: Heizungssperre) | 1.003 | Obj 13 |
| 22 | E | Feuchteführung aktiv | 1.003 | Obj 16 |
| 23 | E | CO₂-Führung aktiv | 1.003 | Obj 18 |
| 24 | E | VOC-Führung aktiv | 1.003 | — |
| 25 | A | Raumanforderung Stufe | 5.100 | — |
| 26 | A | Raumanforderung % | 5.001 | — |
| 27 | A | Betriebsart Status (20.102) | 20.102 | — |
| 28 | A | Betriebsart Status erweitert (1…7) | 5.010 | — |
| 29 | A | Betriebsweise Status | 5.010 | — |
| 30 | A | Schutz aktiv | 1.001 | — |
| 31 | A | Feuchtevergleich sperrt | 1.001 | — |
| 32 | A | abs. Feuchte innen [g/kg] | 9.029 | — |
| 33 | A | abs. Feuchte außen [g/kg] | 9.029 | — |
| 34 | A | Zuluftanforderung (an Fremdgeräte) | 1.003 | Obj 22 |
| 35 | A | Intervall aktiv | 1.001 | — |
| 36 | E | VOC / Luftgüte als Index 0…255 — Alternative zu 19 | 5.010 | — |
| 37–39 | — | Reserve | | |

**Lüfter-Block (24)**

| # | R | KO | DPT | Arcus |
|---|---|---|---|---|
| 0 | E | Freigabe | 1.003 | — |
| 1 | E | Suspendieren (Wartung) | 1.001 | — |
| 2 | E/A | Gruppen-Stufe (Rolle Master sendet, Slave empfängt) | 5.100 | Obj 26 |
| 3 | E/A | Gruppen-Richtung / Takt | 1.012 | Obj 27 |
| 4 | E/A | Gruppen-Lebenszeichen | 1.001 | — |
| 5 | E/A | Gruppen-Betriebsweise | 5.010 | — |
| 6 | A | Stufe Status (wirksam am Lüfter) | 5.100 | — |
| 7 | A | Stufe Status % | 5.001 | — |
| 8 | A | Richtung Status (Zuluft/Abluft) | 1.001 | — |
| 9 | A | WRG aktiv (kurzer Zyklus) | 1.001 | — |
| 10 | A | Ausgangsspannung | 9.020 | — |
| 11 | A | Volumenstrom nominal (± Zuluft/Abluft) | 9.009 | — |
| 12 | A | Betriebsstunden | 7.007 | — |
| 13 | A/E | Filterwechsel fällig (E: 0 quittiert 24 h) | 1.005 | Obj 28 |
| 14 | A | Filter Restlaufzeit | 5.001 | — |
| 15 | E | Filterwechsel-Quittung | 1.016 | Obj 29 |
| 16 | A | Störung | 1.005 | — |
| 17 | A | Fehlercode | 5.010 | — |
| 18–23 | — | Reserve | | |

Die Gruppen-KOs sind nur aktiv, wenn der Verbund des Lüfters die Rolle „Master sendet"
oder „Slave folgt extern" hat; beim internen Verbund bleiben sie unbenutzt. Bei mehreren
Lüftern im selben externen Verbund sendet nur der erste.

### Fehlercodes (Reihenfolge = Priorität, kleinster anliegender Wert > 0 gewinnt)

1 Freigabe fehlt · 2 Master-Timeout · 3 Konfiguration (Kanäle/Kennlinie) ·
4 **DAC nicht erreichbar** (neu, Alarm) · 5 ungültiger Empfangswert ·
6 Überwachung ausgesetzt · 7 Feuchtevergleich sperrt (kein Alarm) ·
8 Sensorwerte fehlen (kein Alarm) · 9 Schutzbetrieb aktiv (kein Alarm) ·
10 Filterwechsel fällig (kein Alarm) ·
11 Richtungskonflikt im Verbund (kein Alarm, ergänzt 2026-09-23)

### Formeln

Sättigungsdampfdruck (Magnus über Wasser, −45…+60 °C):
`e_s = 6.112·exp(17.62·T/(243.12+T))` hPa. Partialdruck `e = rF/100 · e_s`.
Luftdruck aus Höhe: `p = 1013.25·(1 − 2.25577e-5·h)^5.2559` hPa.
Mischungsverhältnis (Anzeige): `r = 622·e/(p − e)` g/kg.
Prüfwerte bei 500 m (p = 954,6 hPa): 20 °C/50 % → e 11,66 hPa, r 7,69 g/kg ·
0 °C/80 % → 4,89 hPa, 3,20 g/kg · 25 °C/60 % → 18,96 hPa, 12,60 g/kg.
Zum Vergleich auf Meereshöhe (p = 1013,25 hPa): 7,24 · 3,02 · 11,86 g/kg — die
Partialdrücke ändern sich dabei nicht, nur die Anzeige in g/kg.
(Korrigiert 2026-09-22: die früheren g/kg-Werte 7,3 / 3,1 / 12,0 gehörten zu keiner
einheitlichen Höhe. Gegengerechnet mit den Formeln dieses Anhangs, geprüft in
`test/test_moistair`.)

Grenzwert-Treppe: Stufe n ein bei Wert ≥ GW_n, aus bei Wert < GW_(n−1); eine höhere
aktive Stufe blockiert niedrigere Übergänge.

Prozent → Stufe: 0 → 0 · 1–25 → 1 · 26–50 → 2 · 51–75 → 3 · 76–100 → 4, Hysterese ±3 %.

### Pins KNXFANDRV Rev 0.1

I²C1: GPIO2 SDA, GPIO3 SCL. Pull-ups R4/R5 4k7 auf der Platine. I²C auch auf
J110 Pin 7/8. DAC-Kanäle: S1 = U2.VOUT0, S2 = U2.VOUT1, S3 = U3.VOUT0, S4 = U3.VOUT1.
Klemmen J3…J6: 1 = 12 V, 2 = S, 3 = GND.
