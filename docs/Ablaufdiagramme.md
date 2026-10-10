# Ablaufdiagramme — KNXFANDRV / KwlControl

Stand: 2026-09-27, nach dem Review. Die Diagramme zeigen, was der Code **tut**,
nicht was der Plan vorsieht. Jede Box nennt die Funktion, in der es passiert.
Weicht ein Diagramm vom Code ab, gilt der Code, und das Diagramm wird nachgezogen.

Darstellung: Mermaid. GitHub zeigt es direkt an. In VS Code braucht die
Markdown-Vorschau die Erweiterung *Markdown Preview Mermaid Support*.

| Nr. | Diagramm | Frage, die es beantwortet |
|---|---|---|
| 1 | [Schichten](#1-schichten) | Wo entsteht was, und wer liest wen? |
| 2 | [Lebenszyklus des Geräts](#2-lebenszyklus-des-geräts) | Welche Spannung liegt in welchem Zustand an? |
| 3 | [Start](#3-start--kwlfanmodulesetup) | In welcher Reihenfolge wird initialisiert? |
| 4 | [Hauptschleife des Lüftermoduls](#4-hauptschleife--kwlfanmoduleloop) | Wann wird gefahren, wann gehalten? |
| 5 | [Raumschleife](#5-raumschleife--kwlroomloop) | Wie entsteht der Stufenwunsch eines Raums? |
| 6 | [Vorfahrt](#6-vorfahrt--stagearbiterupdate) | Welcher Rang gewinnt? |
| 7 | [Verbund je Takt-Platz](#7-verbund-je-takt-platz--kwlfanmoduledrivegroup) | Wie wird aus Räumen eine Stufe je Lüfter? |
| 8 | [Takt und Totzeit](#8-takt-und-totzeit--kwlgroupupdate) | Wann wechselt die Richtung? |
| 9 | [Abluftanforderung](#9-abluftanforderung--kwlroomrunexhaust) | Vorlauf, Nachlauf, Intermittierend |
| 10 | [Vom Stufenwunsch zum Bus](#10-vom-stufenwunsch-zum-bus--kwlfandrive) | Wie wird aus Stufe und Richtung ein DAC-Code? |
| 11 | [Master und Slave](#11-master-und-slave) | Was läuft über den Bus zwischen zwei Geräten? |
| 12 | [Fehlercode und LED](#12-fehlercode-und-led) | Was wird wo gemeldet? |

---

## 1. Schichten

Drei Ebenen, getrennt nach Zuständigkeit (CLAUDE.md, „Drei Ebenen, nicht eine“).
Das Lüftermodul liest die Räume **in-process**, nie über den Bus.

```mermaid
flowchart LR
    BUS(["KNX-Bus<br/>Gruppenobjekte"])

    subgraph ROOM["KwlRoomModule — 8 Räume"]
        R["KwlRoom<br/>Sensoren, Betriebsarten, Bedienung"]
        G["Führungen<br/>StageLadder rF, CO₂, VOC<br/>TempControl, HumidityCompare"]
        A["StageArbiter<br/>Vorfahrt Rang 1…11"]
        R --> G --> A
    end

    subgraph FAN["KwlFanModule"]
        S["Takt-Plätze<br/>8 Verbünde, jeder Lüfter in genau einem"]
        K["KwlGroup<br/>Stufenregel, Takt, Rampe, Totzeit"]
        F["KwlFan × 12<br/>Anteil, Kennlinie, Kalibrierung,<br/>Kabelkompensation, Status"]
        S --> K --> F
    end

    subgraph OUT["Ausgabe"]
        O["KwlOutput<br/>Invarianten 3, 4, 6, 8"]
        D["Gp8413Drive<br/>Board-Header"]
        O --> D
    end

    DAC[["U2 0x58 · U3 0x59<br/>S1…S4, 0…10 V"]]

    BUS -->|"processInputKo"| R
    A -->|"stage(), groupCap(),<br/>cycleWish(), supplyRequested()"| S
    F --> O
    D -->|"I²C, isoliert"| DAC
    F -->|"Status, Fehlercode"| BUS
    A -->|"Raumanforderung, Status"| BUS
```

---

## 2. Lebenszyklus des Geräts

Der RP2040 hängt am **KNX-Bus**, die DACs hängen an den **12 V**. Beides fällt
getrennt aus. Die DACs halten jeden Wert, solange sie Strom haben, und starten
ohne gespeicherten Wert mit **0 V = Volllast** (Invariante 7, Befund B1).

```mermaid
stateDiagram-v2
    direction TB

    state "Stromlos" as OFF
    state "Nur 12 V, kein Bus<br/>Firmware läuft nicht<br/>S1…S4 = 0 V = VOLLLAST" as V12
    state "Unparametriert<br/>holdSafeUnconfigured alle 250 ms<br/>Bereich + 5,00 V überall" as UNCONF
    state "Betrieb<br/>Sollwerte je Durchlauf" as RUN
    state "ETS-Download<br/>configured() = false<br/>sicherer Zustand sofort" as DL
    state "Neustart<br/>processBeforeRestart: sicherer Zustand<br/>DACs halten ihn" as RST
    state "12 V weg, Bus da<br/>Fehlercode 4, Alarm<br/>tryRecoverOutput alle 250 ms" as E4
    state "Bus weg, 12 V da<br/>savePower: sicherer Zustand<br/>loop hält ihn" as SAVE
    state "Falsche Platine<br/>Fehlercode 3<br/>sicherer Zustand, dauerhaft" as E3

    [*] --> OFF
    OFF --> V12 : 12 V ein
    OFF --> UNCONF : Bus ein, nie programmiert
    OFF --> RUN : Bus ein, programmiert
    OFF --> E4 : Bus ein, programmiert, 12 V fehlen noch
    V12 --> UNCONF : Bus kommt, nie programmiert
    V12 --> RUN : Bus kommt, programmiert
    UNCONF --> DL : ETS lädt
    RUN --> DL : ETS lädt
    DL --> RST : Download fertig
    RST --> RUN : setup, Platine passt
    RST --> E3 : setup, Platine passt nicht
    RUN --> E4 : Schreibzugriff ohne ACK
    E4 --> RUN : begin läuft ganz durch
    RUN --> SAVE : SAVE-Interrupt
    SAVE --> RUN : restorePower, kurzer Einbruch
    SAVE --> OFF : Prozessor aus
    E3 --> DL : ETS korrigiert
```

**Zeitfenster mit 0 V**, die bleiben:

| Wann | Dauer | Warum nicht kürzer |
|---|---|---|
| 12 V ohne Bus | unbegrenzt | Ohne Bus läuft keine Firmware. Abhilfe nur in der Installation: 12 V mit dem Bus schalten. |
| 12 V kommen zu laufender Firmware | ≤ Abfrageintervall 250 ms + DAC-Anlauf | Kein EEPROM-Startwert (Invariante 7). |
| Kaltstart mit Bus und 12 V gleichzeitig | Hochlauf bis `setup()` | Messung P16 im Messprotokoll. |

Alle übrigen Übergänge enden in 5,00 V bipolar bzw. 0 V unipolar, bevor der
Zustand wechselt.

---

## 3. Start — `KwlFanModule::setup()`

Reihenfolge nach Invariante 3: **Bereich → sicherer Zustand → Sollwerte**. Das
Raummodul ist als Modul 1 vor dem Lüftermodul (Modul 2) angemeldet, sein `setup()`
ist also schon gelaufen.

```mermaid
flowchart TD
    START(["setup(configured)"]) --> ATT["mOutput.attach<br/>Treiber + Busformat aus dem Board-Header<br/>alle Kanäle bipolar, nicht bereit"]
    ATT --> Q1{"configured?"}

    Q1 -->|"nein, neues Board"| U1["mOutput.begin<br/>Adressabfrage, Bereich 0x11, 5,00 V"]
    U1 --> U2{"DAC antwortet?"}
    U2 -->|"nein, 12 V fehlen"| U3["Log: DAC nicht erreichbar"]
    U2 -->|"ja"| U4["safeAll"]
    U3 --> U4
    U4 --> UEND(["Ende — loop hält den Zustand,<br/>siehe Diagramm 4"])

    Q1 -->|"ja"| C1["setupChannels<br/>Kanaltyp je Stellkanal setzen<br/>VOR begin: er bestimmt den sicheren Zustand"]
    C1 --> C2["setupGroups<br/>8 Verbünde, Sprecher-Lüfter je Verbund,<br/>Lüfter ohne aktiven Verbund → Code 3"]
    C2 --> C3["setupButton, LED-Funktionen 110 / 111"]
    C3 --> C4{"checkHardware<br/>FAN_Hardware = FANDRV_BOARD_ID?"}
    C4 -->|"nein"| E3["mError = Configuration, Code 3<br/>begin + safeAll"]
    E3 --> EEND(["Ende — Fehlerzweig in loop"])
    C4 -->|"ja"| B1["mOutput.begin"]
    B1 --> B2{"ok?"}
    B2 -->|"nein"| E4["mError = DacUnreachable, Code 4"]
    E4 --> EEND
    B2 -->|"ja"| OK(["Stellkanäle bereit —<br/>Sollwerte erst in loop"])

    subgraph BEGIN["KwlOutput::begin"]
        direction LR
        P1["probe<br/>antworten U2 und U3?"] --> P2["configure<br/>Register 0x01 = 0x11"] --> P3["writeVolt safeVolt je Kanal<br/>bipolar 5,00 V = 0x8000 auf dem Bus<br/>unipolar 0 V"] --> P4["mReady = true"]
    end
```

---

## 4. Hauptschleife — `KwlFanModule::loop()`

```mermaid
flowchart TD
    L(["loop(configured)"]) --> Q1{"configured?"}

    Q1 -->|"nein"| W1{"mWasConfigured?<br/>ETS-Download hat begonnen"}
    W1 -->|"ja"| W2["safeAll sofort<br/>mWasConfigured = false"]
    W1 -->|"nein"| H1
    W2 --> H1["holdSafeUnconfigured<br/>alle 250 ms begin:<br/>Bereich + sicherer Zustand"]
    H1 --> RET1(["return"])

    Q1 -->|"ja"| M1["mWasConfigured = true"]
    M1 --> Q2{"mPowerSaved?<br/>SAVE-Interrupt aktiv"}
    Q2 -->|"ja"| RET2(["return — 5,00 V aus savePower bleibt"])
    Q2 -->|"nein"| Q3{"Fehler oder<br/>Ausgabe nicht bereit?"}

    Q3 -->|"ja"| R1{"tryRecoverOutput<br/>nur Code 4, alle 250 ms begin"}
    R1 -->|"geheilt"| RUN
    R1 -->|"nicht geheilt"| F1["sendFault an jeden Lüfter<br/>updateLed"]
    F1 --> RET3(["return — nichts wird gefahren"])

    Q3 -->|"nein"| RUN["Taste auswerten<br/>Flag aus dem Interrupt"]
    RUN --> FL["KwlFan::loop je Lüfter<br/>Betriebsstunden, Filter"]
    FL --> DG["driveGroup je aktivem Verbund 0…7<br/>Diagramm 7<br/>danach driveWithoutGroup: 5,00 V, Code 3"]
    DG --> LED["updateLed<br/>Diagramm 12"]
    LED --> RET4(["return"])
```

`knx.configured()` ist false, sobald die ETS irgendeine Tabelle lädt, auch nur die
Gruppenadressen, und bleibt es bis zum Neustart (`BauSystemBDevice::configured`).

---

## 5. Raumschleife — `KwlRoom::loop()`

Der Raum treibt nichts. Er liefert Stufe, Herkunft, Richtungsforderung, Deckel und
Zykluswunsch; das Lüftermodul liest sie im selben Durchlauf.

```mermaid
flowchart TD
    L(["KwlRoom::loop"]) --> A0{"Raum aktiv?"}
    A0 -->|"nein"| RET(["return"])
    A0 -->|"ja"| S1["expireSensors<br/>Überwachungszeit je Messwert"]
    S1 --> S2{"Betriebsart von Raum 1?"}
    S2 -->|"ja"| S3["mArbiter.setMode Raum 1<br/>wie ein empfangenes Telegramm,<br/>mit Rücksetz-Semantik"]
    S2 -->|"nein"| S4
    S3 --> S4["mArbiter.update<br/>nur um die aktive Betriebsart zu kennen"]
    S4 --> S5["readModeFlags<br/>Freigaben der Führungen dieser Betriebsart"]
    S5 --> S6["runGuidance<br/>rF, CO₂, VOC, Temperatur → Stufenwunsch<br/>setzt mSensorsMissing, Schutz"]
    S6 --> S7["applyRunOn<br/>Nachlauf nach Anforderungsende"]
    S7 --> S8["setAutomaticSuppressed<br/>Intervallpause oder<br/>fehlende Messwerte = Stillstand"]
    S8 --> S9["setDirectionOverride<br/>Betriebsweise-KO vor Zyklusregel"]
    S9 --> S10["runExhaust<br/>Diagramm 9 → setAirDemand"]
    S10 --> S11["setGuidanceStage, setProtection"]
    S11 --> S12["mStage = mArbiter.update<br/>Diagramm 6"]
    S12 --> S13["senden, nur bei Änderung<br/>Stufe, Betriebsart, abs. Feuchte,<br/>Schutz, Intervall, Betriebsweise"]
    S13 --> RET2(["return"])
```

Die Sperre kommt nicht aus dieser Schleife, sondern direkt aus `processInputKo`:
`mArbiter.setLock(active, keepBase)`. Es gibt keinen zweiten Rang-1-Pfad.

---

## 6. Vorfahrt — `StageArbiter::update()`

Zwei Stufen: erst **welche Betriebsart** (Rang 7…11), dann **welche Stufe**
(Rang 1…6). Die Betriebsart liefert Grundstufe und Maximalstufe für Rang 6.

```mermaid
flowchart TD
    U(["update(now)"]) --> X["expireRuntimes<br/>Hand, Zwangsbetriebsart, Zwangsobjekte"]
    X --> M7{"Rang 7<br/>Zwangsobjekt aktiv?"}
    M7 -->|"ja"| MM7["dessen Betriebsart"]
    M7 -->|"nein"| M8{"Rang 8<br/>Nacht?"}
    M8 -->|"ja"| MM8["Nacht-Betriebsart"]
    M8 -->|"nein"| M9{"Rang 9<br/>Zwangsbetriebsart?"}
    M9 -->|"ja"| MM9["Zwangsbetriebsart"]
    M9 -->|"nein"| M10{"Rang 10<br/>Betriebsart ≠ Auto?"}
    M10 -->|"ja"| MM10["Betriebsart"]
    M10 -->|"nein"| MM11["Rang 11<br/>Standard-Betriebsart"]
    MM7 & MM8 & MM9 & MM10 & MM11 --> P["ModeParams p<br/>Grundstufe, Maximalstufe"]

    P --> R1{"Rang 1<br/>Sperre?"}
    R1 -->|"ja"| RR1["Stufe 0<br/>oder Grundstufe bei keepBase<br/>keine Richtung"]
    R1 -->|"nein"| R2{"Rang 2<br/>Schutz?"}
    R2 -->|"ja"| RR2["Schutzstufe"]
    R2 -->|"nein"| R3{"Rang 3<br/>Zu-/Abluftanforderung?"}
    R3 -->|"ja"| RR3["Stufe der Anforderung<br/>Richtung erzwungen"]
    R3 -->|"nein"| R4{"Rang 4<br/>Verbund gesetzt?"}
    R4 -->|"ja"| RR4["Verbundstufe und -richtung"]
    R4 -->|"nein"| R5{"Rang 5<br/>Hand aktiv?"}
    R5 -->|"ja"| RR5["Handstufe<br/>NICHT gedeckelt"]
    R5 -->|"nein"| R6["Rang 6 Automatik<br/>max Grundstufe, Führung<br/>gedeckelt auf Maximalstufe<br/>0 bei unterdrückter Automatik"]
```

Die Betriebsweise-Vorgabe (Richtung ohne Stufe) wird vor Rang 1 eingetragen und
gilt für alles ab Rang 4 abwärts. Sperre löscht sie, Rang 3 überschreibt sie.

**Rang 4 im Arbiter ist derzeit ungenutzt.** `setGroupStage()` ruft die Firmware
nirgends auf. Der Slave-Verbund läuft stattdessen im Lüftermodul: `collectMembers()`
legt die empfangene Stufe und Halbwelle als einziges Mitglied in den Verbund
(Diagramm 7). Wirkung auf die Rangfolge: Sperre, Schutz und Zu-/Abluftanforderung
des Raums gelten am Slave trotzdem, je Lüfter über `fanStageInGroup()`. Handstufe
und Automatik des Raums haben am Slave keine Wirkung, weil der Raum nicht Mitglied
des Verbunds ist.

**Rücksetz-Semantik** (`resetAbove`): eine Aktion auf Rang *n* ≥ 5 setzt alle
Ränge über *n* bis einschließlich 5 zurück. Rang 1…4 setzt nichts zurück.

| Aktion auf Rang | setzt zurück |
|---|---|
| 11 Standard, 10 Betriebsart | Zwangsbetriebsart, Nacht, Zwangsobjekte, Hand |
| 9 Zwangsbetriebsart | Nacht, Zwangsobjekte, Hand |
| 8 Nacht | Zwangsobjekte, Hand |
| 7, 6 | Hand |

---

## 7. Verbund je Takt-Platz — `KwlFanModule::driveGroup()`

Platz 0…7 sind die ETS-Verbünde 1…8. Jeder Lüfter gehört zu genau einem, auch
ein Lüfter allein im Raum (seit 2026-10-10; davor gab es Platz 8…19 für
„eigenständig“). `inSlot()` entscheidet, welcher Lüfter zu welchem Platz gehört.
Ein Lüfter ohne aktiven Verbund fährt nicht: `driveWithoutGroup()` hält ihn auf
5,00 V und meldet Fehlercode 3.

```mermaid
flowchart TD
    D(["driveGroup(slot)"]) --> T{"Slave mit<br/>Überwachungszeit?"}
    T -->|"ja"| T2["Lebenszeichen älter als Timeout?<br/>→ mGroupTimedOut"]
    T -->|"nein"| C
    T2 --> C

    C["collectMembers"] --> C1{"Rolle Slave?"}
    C1 -->|"ja"| CS["ein Mitglied:<br/>empfangene Stufe, Halbwelle als Richtungsforderung"]
    C1 -->|"nein"| CR["je Lüfter im Platz ein Mitglied aus seinem Raum:<br/>Stufe, Deckel groupCap, Zykluswunsch,<br/>Richtungsforderung, Phase"]
    CR --> CF{"folgt Zuluftanforderung<br/>von Raum n, und n fordert?"}
    CF -->|"ja"| CF2["Zusatzmitglied Stufe 0,<br/>Richtung Zuluft"]
    CF -->|"nein"| N
    CF2 --> N
    CS --> N{"Mitglieder > 0?"}
    N -->|"nein"| RET(["return — nichts wird geschrieben"])
    N -->|"ja"| UP["KwlGroup::update<br/>Diagramm 8"]
    UP --> MS{"Master?"}
    MS -->|"ja"| SND["sendGroup<br/>bei Änderung oder Herzschlag"]
    MS -->|"nein"| LOOP
    SND --> LOOP

    LOOP["je Lüfter im Platz"] --> SAFE{"Totzeit, Freigabe fehlt,<br/>suspendiert oder Master-Timeout?"}
    SAFE -->|"ja"| DS["driveSafe<br/>5,00 V, ego beide Motoren"]
    SAFE -->|"nein"| OV{"Raum auf Rang 1, 2 oder 3?<br/>roomOverridesGroup"}
    OV -->|"ja"| OV1["Stufe = Raumstufe<br/>kein Nachbardeckel"]
    OV -->|"nein"| OV2["Stufe = Verbundstufe"]
    OV1 & OV2 --> SH["stageForShare<br/>Anteil gilt für beide Wege"]
    SH --> DIR["Richtung aus dem Takt<br/>Phase 0 = Verbundrichtung, Phase 1 = Gegenrichtung"]
    DIR --> DRV["KwlFan::drive<br/>Diagramm 10"]
    DS & DRV --> OK{"Ausgabe noch bereit?"}
    OK -->|"nein"| ERR["mError = Code 4<br/>safeAll"]
    OK -->|"ja"| FLT
    ERR --> FLT["Fehlerschleife je Lüfter<br/>Diagramm 12"]
    FLT --> RET2(["return"])
```

**Stufenregeln** (`KwlGroup::computeStage`):

| Regel | Verbundstufe |
|---|---|
| Maximum mit Deckel (Vorgabe) | höchste Mitgliedsstufe, begrenzt durch den kleinsten Deckel |
| Minimum | kleinste Mitgliedsstufe |
| Raum *n* führt | Stufe des führenden Raums; fehlt er im Verbund: Minimum |

Deckel eines Raums = Maximalstufe seiner Betriebsart, nie unter seiner eigenen
Stufe (`groupCapFor`, Variante a). Ein gesperrter Raum bringt seinen Deckel
weiterhin ein.

**Zykluszeit:** kürzester Wunsch gewinnt (Vorgabe) oder der führende Raum
entscheidet. WRG ist aktiv, wenn gependelt wird und nicht der Sommerzyklus gewählt
ist.

**Richtungskonflikt:** Mehrere Mitglieder fordern eine Richtung. Es führt die höhere
Stufe, bei Gleichstand die kleinere Raumnummer. Der kleinste unterlegene Raum meldet
Code 11.

---

## 8. Takt, Rampe und Totzeit — `KwlGroup::update()`

`mCurrent` ist die Richtung von Phase 0. Die Totzeit ist der einzige Weg von einer
Richtung in die andere (Invariante 5). Seit 2026-10-10 liegt davor und danach je
eine Rampe (`GroupResult::ramp`, 0…1 des Ausschlags um 5,00 V); Rampenzeit 0
überspringt beide Rampen.

```mermaid
stateDiagram-v2
    state "Stillstand<br/>Stufe 0<br/>Uhr läuft nicht" as STOP
    state "Richtung halten<br/>mCurrent, ramp = 1" as HOLD
    state "Rampe ab<br/>mCurrent, ramp 1 → 0" as DOWN
    state "Totzeit<br/>alle Kanäle 5,00 V<br/>Ziel mPending" as DEAD
    state "Rampe auf<br/>neue Richtung, ramp 0 → 1" as UP

    [*] --> HOLD : erster update setzt Zeitbezug
    HOLD --> STOP : Stufe 0
    DOWN --> STOP : Stufe 0, Wechsel verworfen
    DEAD --> STOP : Stufe 0, Wechsel verworfen
    UP --> STOP : Stufe 0
    STOP --> HOLD : Stufe > 0, Uhr beginnt neu

    HOLD --> DOWN : Zykluszeit abgelaufen<br/>oder feste Richtung weicht ab
    HOLD --> DEAD : dasselbe, aber Rampe 0<br/>oder Verbund stand
    DOWN --> UP : Forderung weg vor 5,00 V,<br/>ohne Sprung zurück
    DOWN --> DEAD : Rampe abgelaufen
    DEAD --> DEAD : feste Richtung ändert das Ziel,<br/>Totzeit läuft weiter
    DEAD --> UP : Totzeit abgelaufen,<br/>mCurrent = mPending, Zyklus ab hier
    DEAD --> HOLD : dasselbe bei Rampe 0
    UP --> DOWN : neue Forderung in Gegenrichtung
    UP --> HOLD : Rampe abgelaufen
```

Aus dem Stillstand heraus beginnt der Takt mit der zuletzt gültigen Richtung.
Verlangt eine Forderung dann die andere Richtung, geht es ohne Rampe ab direkt in
die Totzeit – es dreht sich ja noch nichts – und danach die Rampe hinauf.

`KwlFan::drive()` skaliert den Ausschlag um 5,00 V erst nach Kompensation und
Kalibrierung. So bleibt jeder Rampenwert auf der Seite seines Ziels, und der
Totzeitwächter je Kanal sieht keinen zusätzlichen Seitenwechsel. Der Status
„Ausgangsspannung“ meldet das Ziel ohne Rampe.

---

## 9. Abluftanforderung — `KwlRoom::runExhaust()`

```mermaid
stateDiagram-v2
    state "Idle<br/>keine Anforderung" as IDLE
    state "Lead<br/>Vorlauf, fördert nichts" as LEAD
    state "Active<br/>Rang 3: Abluft, Stufe der Anforderung" as ACT
    state "Lag<br/>Nachlauf, Rang 3 weiter aktiv" as LAG

    [*] --> IDLE
    IDLE --> LEAD : KO = 1, Vorlauf > 0
    IDLE --> ACT : KO = 1, Vorlauf = 0
    LEAD --> IDLE : KO = 0 im Vorlauf, kein Nachlauf
    LEAD --> ACT : Vorlauf abgelaufen
    ACT --> LAG : KO = 0, Nachlauf > 0
    ACT --> IDLE : KO = 0, Nachlauf = 0
    LAG --> ACT : KO = 1
    LAG --> IDLE : Nachlauf abgelaufen
```

- **Intermittierend:** in Active und Lag nur die Aktivzeit je Periode. In der Pause
  gilt die Automatik des Raums, nicht Stufe 0.
- **Zuluftanforderung:** `mSupplyReq = fördert und „Zuluftanforderung senden“ = Ja`.
  Derselbe Parameter schaltet auch den in-process-Wunsch ab, den Verbünde mit
  „folgt Zuluftanforderung von Raum n“ lesen.
- **Im Verbund** fährt der Lüfter des anfordernden Raums die Stufe der Anforderung,
  ohne Deckel des Nachbarn (Diagramm 7). Die Richtung Abluft geht als
  Richtungsforderung in den Verbund, der Partner mit Phase 1 fährt damit Zuluft.

---

## 10. Vom Stufenwunsch zum Bus — `KwlFan::drive()`

```mermaid
flowchart TD
    CMD(["DriveCommand<br/>Stufe, Richtung, WRG"]) --> CL["Stufe auf 0…4 klemmen"]
    CL --> SV{"Curve::stageVolt<br/>Gerätetyp"}
    SV -->|"unipolar RA 15-60"| UV["Tabellenwert,<br/>keine Richtung"]
    SV -->|"ego, Stufe 4"| EG4["Abluftstoß<br/>beide Motoren gleichsinnig"]
    SV -->|"bipolar"| BH{"Polarität des Lüfters (ETS)<br/>Standard: über 5 V Zuluft"}
    BH --> BV["untere oder obere Hälfte<br/>ego: Motor 2 in der Gegenhälfte"]

    UV & EG4 & BV --> CO["condition je Motor<br/>1. + Kabelkompensation, Stufe > 0<br/>2. × Kalibrierfaktor<br/>3. klemmen 0…10 V"]
    CO --> CH{"zwei Kanäle?"}
    CH -->|"ego"| PAIR["KwlOutput::setEgo<br/>writePair: EIN I²C-Frame,<br/>Register 0x02, vier Datenbytes"]
    CH -->|"sonst"| ONE["KwlOutput::setVolt"]

    PAIR & ONE --> V2C["Curve::voltToCode<br/>round V × 3276,7<br/>klemmen 0…0x7FFF, NaN → 0"]
    V2C --> WIRE["Curve::codeToWire<br/>linksbündig: Code << 1<br/>5,00 V → 0x8000"]
    WIRE --> I2C[["I²C → U2 / U3"]]
    I2C --> ACK{"ACK?"}
    ACK -->|"nein"| FAIL["fail DacUnreachable<br/>mReady = false"]
    ACK -->|"ja"| ST["Status senden,<br/>nur bei Änderung"]
```

Der sichere Zustand (`driveSafe`, `safeAll`) geht ohne Kalibrierung und ohne
Kabelkompensation direkt auf `safeVolt`: bipolar 5,00 V, unipolar 0 V.

Die Ausgabe schreibt in **jedem** Durchlauf, sie speichert keinen „letzten Wert“
zwischen. Deshalb überschreibt `loop()` einen sicheren Zustand sofort wieder,
wenn es nicht ausdrücklich angehalten wird: `mPowerSaved`, Fehlerzweig,
unkonfiguriert.

---

## 11. Master und Slave

Zwei Geräte teilen einen Verbund. Die Verbund-Objekte hängen am Lüfter mit der
kleinsten Kanalnummer im Verbund.

```mermaid
sequenceDiagram
    participant M as Gerät A — Verbund n, Master
    participant B as KNX-Bus
    participant S as Gerät B — Verbund n, Slave

    Note over M: KwlGroup rechnet aus den eigenen Räumen
    M->>B: Stufe, Halbwelle bei Änderung
    B->>S: Stufe, Halbwelle
    Note over S: collectMembers: ein Mitglied mit<br/>Stufe und Richtungsforderung.<br/>Wechsel geht über die eigene Totzeit
    loop alle Herzschlag-Sekunden
        M->>B: Lebenszeichen
        B->>S: Lebenszeichen → mGroupLastAlive
    end
    Note over S: Raum im Schutz: dieser Lüfter 5,00 V,<br/>Code 9, die übrigen folgen weiter
    M--xB: Lebenszeichen bleibt aus
    Note over S: nach Überwachungszeit:<br/>alle Lüfter des Verbunds 5,00 V, Code 2
    M->>B: Lebenszeichen
    B->>S: Lebenszeichen
    Note over S: Timeout aufgehoben, folgt wieder
```

---

## 12. Fehlercode und LED

Je Lüfter gewinnt der **kleinste** anliegende Code (`lowestError`). Codes 1…5 sind
Alarme: Störungs-KO = 1 und Blinkcode an der LED.

```mermaid
flowchart LR
    subgraph MOD["Modul"]
        E3["3 Konfiguration"]
        E4["4 DAC nicht erreichbar"]
        E2["2 Master-Timeout"]
    end
    subgraph FANX["Lüfter selbst"]
        E1["1 Freigabe fehlt"]
        E6["6 suspendiert"]
        E10["10 Filter fällig"]
    end
    subgraph RM["Raum"]
        E7["7 Feuchtevergleich sperrt"]
        E8["8 Messwerte fehlen"]
        E9["9 Schutzbetrieb"]
    end
    subgraph GRP["Verbund"]
        E11["11 Richtungskonflikt"]
    end

    MOD --> SF["KwlFan::sendFault<br/>lowestError"]
    FANX --> SF
    RM -->|"Slave: nur 9"| SF
    GRP -->|"nur der unterlegene Raum"| SF
    SF --> KO["KO Fehlercode, KO Störung<br/>nur bei Änderung"]
    SF --> LED{"updateLed<br/>schlimmster Code aller Lüfter"}
    LED -->|"Code 1…5"| BL["Blinkcode:<br/>n Blitze, Pause"]
    LED -->|"Code 6…11"| BK["Blinken"]
    LED -->|"kein Code, Funktion 110,<br/>ein Lüfter läuft"| ON["an"]
    LED -->|"sonst"| OFF["aus"]
```

Code 4 heilt von selbst, sobald `begin()` wieder ganz durchläuft (Diagramm 4).
Code 3 heilt nur durch eine korrigierte Parametrierung.
