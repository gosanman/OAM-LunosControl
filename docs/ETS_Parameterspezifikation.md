# ETS-Parameterspezifikation — OAM-KwlControl

**Stand:** 20.09.2026 · **Zweck:** Vorlage für `Room.share.xml`, `Room.templ.xml`,
`Fan.share.xml`, `Fan.templ.xml`, `Kwl.xml` und die Baggages-Hilfetexte. Claude Code
setzt diese Tabellen in OpenKNX-XML um; die Syntax kommt aus der Vorlage
(`reference/OFM-FanControl/src/Fan.templ.xml`), der Inhalt von hier.

Konventionen in diesem Dokument:
- **Typ**: `enum` (Auswahlliste), `uint8`/`uint16` (Zahl), `bool` (Ja/Nein),
  `float9` (DPT-9-Fließkomma als Parameter), `text` (Freitext), `time` (Zeit mit Einheit)
- **Sichtbar**: Bedingung; leer = immer. `Typ` meint den Lüftertyp des Kanals,
  `HW` die Hardwareauswahl, `Betriebsart.X` den Parametersatz X.
- **Hilfe**: Kurzfassung für die Kontexthilfe; wird in `Baggages/Help_de/` je Parameter
  eine Datei. Ton wie die Arcus-Applikationsbeschreibung: sachlich, ein Absatz, sagt
  *wozu*, nicht nur *was*.

---

## 0. Festlegungen (einfrieren vor der ersten knxprod)

| Größe | Wert | Bemerkung |
|---|---|---|
| OpenKnxId | **0xA8** | eingefroren 20.09.2026 |
| ApplicationNumber | **0x05** — in der knxprod 0xA805 = 43013 | eingefroren 20.09.2026, ≠ 0x86 der Vorlage |
| ApplicationVersion | 0x01 | `ReplacesVersions` nur eigene |
| Produktname | KNX Lüftersteuerung 0–10 V | ETS-Katalogtext |
| Modul ROOM | Präfix `ROOM`, NumChannels 8, KoOffset 20, Block 40 | KO 20…339 |
| Modul FAN | Präfix `FAN`, NumChannels 12, KoOffset 340, Block 24 | KO 340…627 |
| Modul LOG | KoOffset 640 | optional |
| Kanalnamen | ROOM: „Raum %C%", FAN: „Lüfter %C%" | plus Freitextname |
| Stufen | 0…4, DPT 5.100 | |
| Sprache | de-DE primär; Struktur für en-US vorsehen | |

---

## 1. Modul FAN — geräteweite Parameter (`Fan.share.xml`)

### Seite „Kanalauswahl" — Anzahlen aller drei Ebenen

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `FAN_Hardware` | enum | 2 = Entwicklungsaufbau (2 Kanäle) · 4 = KNXFANDRV Rev 0.1 · 6 · 8 · 10 · 12 = KNXFANDRV 6/8/10/12 | **4** | | Wählt die Platine. Die Anzahl der Lüfterkanäle folgt daraus. Die Firmware prüft beim Start, ob die gewählte Platine zur bestückten passt — bei Abweichung Störung an allen Lüftern. |
| — | | | | | Es gibt **keine** Anzahl-Parameter für Räume und Verbünde. Nach dem OpenKNX-Kanalauswahl-Pattern (Beschluss 09.07.2026) entfällt der „Verfügbare Kanäle"-Schieber; ein Kanal erscheint im Baum, weil er aktiviert ist. Alle Zeilen stehen immer in der Tabelle. |

Auf derselben Seite je eine Tabelle Raum / Lüfter / Verbund mit Kanalaktivität bzw. Rolle und Beschreibung.

### Seite „Allgemein"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `FAN_StartupDelay` | uint8 | 0…30 s | 3 | | Wartezeit nach Busspannungswiederkehr, bevor Sollwerte geschrieben werden. |
| `ROOM_Altitude`, `ROOM_PercentHysteresis` | | | | | aus `Room.share.xml`, hier angezeigt — siehe Abschnitt 3. |

Der **Enum-Wert ist die Kanalzahl** — Sichtbarkeit der Lüfterseiten dann einfach
`%C% <= FAN_Hardware`.

### Seite „Verbünde" — 8 Unterseiten „Verbund 1…8"

Sichtbar: Verbund n, wenn `FAN_GrpN_Active` = Aktiviert. Die Aktivität steht in der Kanalauswahl-Tabelle, die Rolle nur auf der Verbundseite. (Frühere Fassungen: `ceil(FAN_Hardware/2) + 2`, dann ein Anzahl-Parameter — beides abgelöst vom Kanalauswahl-Pattern.)

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `FAN_GrpN_Name` | text 20 | | „Verbund N" | | Anzeigename in Konsole und Diagnose. |
| `FAN_GrpN_Role` | enum | 0 intern · 1 Master (sendet auf den Bus) · 2 Slave (folgt externem Master) | 0 | | Intern: alle Lüfter auf dieser Platine. Master/Slave koppelt Verbünde über mehrere Geräte über die Gruppen-KOs. |
| `FAN_GrpN_StageRule` | enum | 0 Maximum, begrenzt durch kleinsten Raumdeckel · 1 Minimum · 2 Raum führt | 0 | Role ≠ 2 | Wie aus den Anforderungen mehrerer Räume eine Verbundstufe wird. Vorgabe: der lauteste Raum bestimmt, der leiseste deckelt. |
| `FAN_GrpN_LeadRoom` | uint8 | 1…8 | 1 | StageRule = 2 | Raum, dessen Anforderung allein gilt. |
| `FAN_GrpN_CycleRule` | enum | 0 WRG gewinnt (kürzester Zyklus) · 1 Raum führt | 0 | Role ≠ 2 | Wollen Räume unterschiedliche Zyklusregeln, entscheidet diese Einstellung. |
| `FAN_GrpN_CycleS1` … `_CycleS4` | time | 40 s … 2 h | **70 s** | | Zykluszeit je Richtung in Stufe 1…4 bei Wärmerückgewinnung. Vorgabe entspricht LUNOS. **Änderungen verringern die erreichbare Wärmerückgewinnung.** |
| `FAN_GrpN_CycleSummer` | time | 40 s … 2 h | **1 h** | | Zykluszeit im Sommerbetrieb. Der Regenerator sättigt nach etwa einer Minute; ein langer Zyklus lüftet praktisch ohne Wärmerückgewinnung, wechselt aber weiter die Richtung. |
| `FAN_GrpN_DeadTime` | uint8 | 1…10 s | 2 | | Aufenthalt bei Stillstand (5,00 V) vor jedem Richtungswechsel. |
| `FAN_GrpN_FollowSupplyFrom` | uint8 | 0 aus · 1…8 Raum | 0 | Role ≠ 2 | Solange dieser Raum Abluft anfordert, fahren alle Lüfter des Verbunds Zuluft (Nachströmung). |
| `FAN_GrpN_HeartbeatCycle` | time | 10 s … 10 min | 1 min | Role = 1 | Sendezyklus des Lebenszeichens an Slaves. |
| `FAN_GrpN_MasterTimeout` | time | 30 s … 30 min | 3 min | Role = 2 | Bleibt das Lebenszeichen des Masters aus, gehen die Lüfter des Verbunds auf Stillstand und melden Fehlercode 2. |

### Seite „Status-LEDs" (optional, wie Vorlage)

| Parameter | Typ | Werte | Vorgabe |
|---|---|---|---|
| Info-LED 2/3 Funktion | PT-SLEDFunc | Basis 110: Lüfter 1…12 | Lüfter 1 / Lüfter 2 |

---

## 2. Modul FAN — je Lüfter (`Fan.templ.xml`)

Kanal n sichtbar, wenn `n <= FAN_Hardware`.

### Seite „Lüfter %C%" — Allgemein

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `FAN_Active` | bool | | Kanal 1–3: ja, sonst nein | | Nicht belegte Kanäle geben dauerhaft Stillstand aus und erzeugen keinen Busverkehr. |
| `FAN_Name` | text 20 | | „Lüfter %C%" | Active | Anzeigename. |
| `FAN_Type` | enum | 0 LUNOS e²60 · 1 LUNOS ego (2 Kanäle) · 2 LUNOS RA 15-60 (unipolar) · 3 generisch bipolar · 4 generisch unipolar | 0 | Active | Der ego belegt zwei aufeinanderfolgende Kanäle; der folgende Lüfterkanal ist dann nicht verfügbar. |
| `FAN_Channel` | uint8 | 1…12 | **%C%** | Active | DAC-Kanal und damit Klemme, an der dieser Lüfter hängt. Der ego belegt diesen und den folgenden Kanal und muss auf einem ungeraden Kanal beginnen. Kein Kanal doppelt; die Firmware prüft es beim Start (Fehlercode 3). Welche Klemme das ist, zeigt die Seite „Anschluss". |
| `FAN_Room` | uint8 | 1…8 | 1 | Active | Raum, dessen Sensoren, Betriebsart und Bedienung diesen Lüfter führen. |
| `FAN_Group` | enum | 0 eigenständig · 1…8 Verbund | 0 | Active | Verbund, mit dem dieser Lüfter gemeinsam taktet. Eigenständig: eigener Takt. |
| `FAN_Phase` | enum | 0 Phase A · 1 Phase B | 0 | Group ≠ 0 ∧ Typ ≠ ego | Lüfter eines Pendelpaars laufen gegenläufig: einer Phase A, einer Phase B. |
| `FAN_Share` | uint8 | 25…100 % | 100 | Group ≠ 0 | Anteil an der Verbundstufe. 50 % lässt diesen Lüfter eine Stufe niedriger laufen als den Verbund. |
| `FAN_Direction5V` | enum | 0 unter 5 V = Zuluft · 1 unter 5 V = Abluft | aus Board-Header | nur mit `OPENKNX_DEBUG` | Zuordnung der Spannungshälften. Wird durch Messung M1 festgelegt; im Normalfall nicht ändern. |

### Seite „Kennlinie"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `FAN_CurveCustom` | bool | | nein | Typ ≤ 2 | Ja: die Vorgabewerte des Lüftertyps werden durch die folgenden ersetzt. Nur ändern, wenn nachgemessen. |
| `FAN_CurveS1A` … `_S4A` | uint16 | 0…10000 mV | je Typ (s. u.) | CurveCustom ∨ Typ ≥ 3 | Stellspannung Stufe 1…4, Richtung A. |
| `FAN_CurveS1B` … `_S4B` | uint16 | 0…10000 mV | je Typ | (CurveCustom ∨ Typ ≥ 3) ∧ Typ bipolar | Stellspannung Stufe 1…4, Richtung B. |
| `FAN_CurveBoostA`, `_BoostB` | uint16 | 0…10000 mV | ego: 1110 / 1110 | Typ = ego | Abluftstoß (Stufe 4): beide Motoren gleichsinnig. |
| `FAN_FlowS1` … `_S4` | uint8 | 0…100 m³/h | je Typ | | Nennvolumenstrom je Stufe für Statusmeldung und Filterzähler. |
| `FAN_CurrentS1` … `_S4` | uint16 | 0…1000 mA | je Typ | CableComp aktiv | Stromaufnahme je Stufe für die Leitungskompensation. |

**Vorgaben je Typ** (Referenzdesign §3.4; Ströme aus Messung M2 zu ersetzen):

| Typ | S1 A/B | S2 A/B | S3 A/B | S4 A/B | Flow | Strom mA |
|---|---|---|---|---|---|---|
| e²60 | 4010/6010 | 3210/6910 | 1960/8150 | 10/10000 | 5/20/40/60 | 33/90/170/275 |
| ego (M1/M2) | 3260/6850 | 2090/8000 | 1250/8850 | 1110/1110 | 5/10/20/45 | 80/150/250/410 |
| RA 15-60 (unipolar) | 1580 | 3570 | 5760 | 8150 | 15/30/45/60 | 60/120/200/300 |
| generisch | 4000/6000 | 3000/7000 | 2000/8000 | 1000/9000 | 10/20/30/40 | 100/150/200/300 |

Aus 0 → 5000 mV (bipolar) bzw. 0 mV (unipolar), nicht parametrierbar.

### Seite „Leitung und Kalibrierung"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `FAN_CableComp` | bool | | nein | | Ein: die Firmware hebt die Stellspannung um den Spannungsabfall auf dem Rückleiter an. Nötig ab etwa 5 m bei 0,7 mm² ohne vierte Ader. |
| `FAN_CableLength` | uint8 | 1…30 m | 10 | CableComp | Einfache Leitungslänge zwischen Steuerung und Lüfter. |
| `FAN_CableSection` | enum | 0,5 · 0,7 · 1,0 · 1,5 mm² | 0,7 | CableComp | Aderquerschnitt. |
| `FAN_CableResistance` | uint16 | 0…2000 mΩ | berechnet | Debug | Alternativ direkt: Widerstand eines Leiters (Arcus-Kompatibilität). |
| `FAN_CalibGain` | uint16 | 9800…10200 (× 0,0001) | 10000 | | Kalibrierfaktor aus Prüfschritt P7. 10000 = 1,0000. |

### Seite „Überwachung und Wartung"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `FAN_UseEnable` | bool | | nein | | Ein: der Lüfter läuft nur bei Freigabe = 1 über das KO. Die Freigabe wird gespeichert und überlebt Neustart. |
| `FAN_SuspendAllowed` | bool | | ja | | Erlaubt Wartungs-Suspend über KO. |
| `FAN_FilterMode` | enum | 0 aus · 1 Laufzeit · 2 volumenstromgewichtet | 2 | | Filterzähler nach Stunden mit Stufe > 0 oder nach durchgesetzter Luftmenge. Ein Filter setzt sich nach Luftmenge zu. |
| `FAN_FilterHours` | uint16 | 500…8760 h | 2200 | FilterMode = 1 | Wechselintervall. |
| `FAN_FilterVolume` | uint16 | 10…500 (× 1000 m³) | 60 | FilterMode = 2 | Wechselintervall als Luftmenge. 60 000 m³ ≈ 3 Monate Stufe 2 Dauerbetrieb beim e²60. |
| `FAN_FilterRemindCycle` | time | 1…72 h | 24 h | FilterMode ≠ 0 | Wiederholung der Meldung nach kurzfristiger Quittung. |
| `FAN_PowerOnStage` | enum | 0 Stillstand · 1 Stufe 1 · 2 Stufe 2 | 1 | | Wert, den die Ausgangsstufe nach Ausfall der 12-V-Versorgung ohne Bus einnimmt (EEPROM). Stufe 1 = Feuchteschutz. Wird nur per Konsole `kwl store` geschrieben. |
| `FAN_SendCycleStatus` | time | 0 aus · 1 min … 24 h | 0 | | Zyklisches Senden von Stufe, Richtung, Spannung. |
| `FAN_SendCycleHours` | time | 0 aus · 1 h … 24 h | 1 h | | Zyklisches Senden der Betriebsstunden. |

### KOs je Lüfter (Block 24, Nummer = KoOffset + 24·(n−1) + #)

| # | Name | DPT | Flags | Sendet |
|---|---|---|---|---|
| 0 | Freigabe | 1.003 | K L S | — |
| 1 | Suspendieren | 1.001 | K L S | — |
| 2 | Gruppe: Stufe | 5.100 | Master: K L Ü · Slave: K L S | bei Änderung |
| 3 | Gruppe: Richtung/Takt | 1.012 | Master: K L Ü · Slave: K L S | bei Wechsel |
| 4 | Gruppe: Lebenszeichen | 1.001 | Master: K L Ü · Slave: K L S | zyklisch |
| 5 | Gruppe: Betriebsweise | 5.010 | Master: K L Ü · Slave: K L S | bei Änderung |
| 6 | Stufe Status | 5.100 | K L Ü | bei Änderung, zyklisch |
| 7 | Stufe Status % | 5.001 | K L Ü | bei Änderung |
| 8 | Richtung Status (1 = Zuluft) | 1.001 | K L Ü | bei Wechsel |
| 9 | WRG aktiv | 1.001 | K L Ü | bei Änderung |
| 10 | Ausgangsspannung | 9.020 | K L Ü | bei Änderung > 50 mV |
| 11 | Volumenstrom nominal (± Zuluft/Abluft) | 9.009 | K L Ü | bei Änderung |
| 12 | Betriebsstunden | 7.007 | K L Ü | zyklisch |
| 13 | Filterwechsel fällig | 1.005 | K L S Ü | bei Erreichen, dann alle RemindCycle |
| 14 | Filter Restlaufzeit | 5.001 | K L Ü | bei Änderung ≥ 5 % |
| 15 | Filterwechsel-Quittung | 1.016 | K L S | — |
| 16 | Störung | 1.005 | K L Ü | bei Änderung |
| 17 | Fehlercode | 5.010 | K L Ü | bei Änderung |
| 18–23 | Reserve | | | |

---

## 3. Modul ROOM — geräteweite Parameter (`Room.share.xml`)

| Parameter | Typ | Werte | Vorgabe | Hilfe |
|---|---|---|---|---|
| `ROOM_Altitude` | uint16 | 0…2000 m | 500 | Höhe über Meer, nur für die Umrechnung der absoluten Feuchte in g/kg. Der Feuchtevergleich selbst ist höhenunabhängig. Angezeigt auf der Seite „Allgemein". |
| `ROOM_PercentHysteresis` | uint8 | 0…10 % | 3 | Hysterese der Umrechnung Prozent → Stufe an den Schwellen 25/50/75 %. |

---

## 4. Modul ROOM — je Raum (`Room.templ.xml`)

Kanal n sichtbar, wenn `ROOM_Active` = Aktiviert. Die Aktivität steht in der Kanalauswahl-Tabelle, nicht auf der Raumseite.

### Seite „Raum %C%" — Allgemein

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_Active` | bool | | Kanal 1–3: ja | | |
| `ROOM_Name` | text 20 | | „Raum %C%" | Active | |
| `ROOM_OutdoorSource` | enum | 0 eigene KOs · 1 von Raum 1 übernehmen | Kanal 1: 0, sonst 1 | Active ∧ %C% > 1 | Außentemperatur und -feuchte gibt es im Haus meist einmal. |
| `ROOM_DefaultMode` | enum | 1 Komfort · 2 Standby · 3 Nacht · 4 Schutz | 2 | Active | Betriebsart, wenn das KO „Betriebsart" 0 (Auto) enthält oder nie empfangen wurde. |
| `ROOM_ModeSource` | enum | 0 eigene KOs · 1 Betriebsart-KOs von Raum 1 übernehmen | 0 | Active ∧ %C% > 1 | Für Häuser mit einer zentralen Betriebsartvorgabe. |
| `ROOM_ManualTimeout` | time | 0 dauerhaft · 1…240 min | 60 min | Active | Geltungsdauer einer manuellen Stufe. Danach zurück in die Automatik. |
| `ROOM_ManualClearsOnMode` | bool | | ja | Active | Ein Betriebsartwechsel beendet den Handbetrieb. |

### Seite „Betriebsarten" — 7 Unterseiten

Für jede Betriebsart X ∈ {Komfort, Standby, Nacht, Schutz, Stoßlüften, Absenkung, Ruhe}:

| Parameter | Typ | Werte | Vorgaben K/S/N/Sch/St/A/R | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_ModeX_BaseStage` | enum | 0…4 | 1/1/1/0/4/1/0 | | Grundstufe: läuft immer, auch ohne Anforderung. |
| `ROOM_ModeX_MaxStage` | enum | 0…4 | 4/2/1/1/4/2/0 | | Obergrenze für alle Führungen in dieser Betriebsart. |
| `ROOM_ModeX_LeadTemp` | bool | | ja/nein/nein/nein/nein/ja/nein | X ≠ Ruhe | Temperaturführung aktiv. |
| `ROOM_ModeX_Frost` | bool | | ja/ja/ja/**ja fest**/ja/ja/ja | | Frostschutz aktiv (in Schutz nicht abschaltbar). |
| `ROOM_ModeX_LeadHum` | bool | | ja/ja/nein/nein/nein/nein/nein | X ≠ Ruhe | Feuchteführung (rF-Treppe). |
| `ROOM_ModeX_Dehum` | bool | | ja/ja/nein/nein/nein/nein/nein | X ≠ Ruhe | Entfeuchtung nach absolutem Feuchtevergleich. |
| `ROOM_ModeX_LeadCO2` | bool | | ja/ja/nein/nein/nein/nein/nein | X ≠ Ruhe | CO₂-Führung. |
| `ROOM_ModeX_LeadVOC` | bool | | ja/nein/nein/nein/nein/nein/nein | X ≠ Ruhe | VOC-Führung. |
| `ROOM_ModeX_Cycle` | enum | 0 über Sommer-KO · 1 WRG (kurz) · 2 Sommer (lang) · 3 Zuluft · 4 Abluft | 0/0/1/1/0/0/1 | X ≠ Ruhe | Zyklusregel-Wunsch des Raums; der Verbund entscheidet bei Konflikt. |
| `ROOM_ModeX_Interval` | bool | | nein/nein/nein/nein/nein/nein/nein | X ∉ {Stoß, Ruhe} | Intervallbetrieb in dieser Betriebsart. |
| `ROOM_ModeX_RunOn` | time | 0…60 min | 10/5/0/0/0/5/0 | X ≠ Ruhe | Nachlauf nach Ende einer Anforderung. |

### Seite „Zwangsobjekte und Nacht"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_ForcePriority` | enum | 0 gleich (letztes gewinnt) · 1 hierarchisch 1 < 2 < 3 | 0 | | Verhalten bei gleichzeitig aktiven Zwangsobjekten. |
| `ROOM_Force1_Mode` | enum | 1…7 Betriebsart | 5 Stoßlüften | | Betriebsart, die Zwangsobjekt 1 auslöst. |
| `ROOM_Force1_Timeout` | time | 0 unendlich · 1…240 min | 30 min | | Nach Ablauf zurück zur darunterliegenden Ebene. |
| `ROOM_Force2_Mode` / `_Timeout` | | | 7 Ruhe / 0 | | |
| `ROOM_Force3_Mode` / `_Timeout` | | | 1 Komfort / 30 min | | |
| `ROOM_ForcedModeTimeout` | time | 0 unendlich · 1…240 min | 30 min | | Laufzeit der Zwangsbetriebsart (KO 1). |
| `ROOM_NightMode` | enum | 3 Nacht · 6 Absenkung · 7 Ruhe | 3 | | Betriebsart, die das Nacht-KO setzt. |

### Seite „Intervallbetrieb"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_IntervalPeriod` | time | 30 min … 24 h | 4 h | irgendeine Betriebsart mit Interval | Periode. |
| `ROOM_IntervalActive` | time | 5 min … 2 h | 30 min | ebenso | Aktivzeit innerhalb der Periode, auf Grundstufe. Außerhalb Stillstand. |

### Seite „Führungen" — Unterseite „Feuchte"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_HumGW0` … `_GW4` | uint8 | 0…90 % | 45/50/55/60/65 | | Grenzwert-Treppe: Grenzwert n schaltet Stufe n ein, Grenzwert n−1 schaltet sie aus. Grenzwerte müssen aufsteigend sein. |
| `ROOM_DehumOn` | uint16 | 0…50 (× 0,1 hPa) | 15 | | Entfeuchtung wirkt, wenn der Dampfdruck innen um diesen Betrag über außen liegt. |
| `ROOM_DehumOff` | uint16 | 0…50 (× 0,1 hPa) | 5 | | Darunter sperrt der Feuchtevergleich die Feuchteführung — Lüften würde Feuchte hereinholen. |
| `ROOM_DehumStage` | enum | 1…4 | 2 | | Stufe bei aktiver Entfeuchtung. |
| `ROOM_SensorTimeout` | time | 0 aus · 5 min … 24 h | 60 min | | Bleibt ein Sensorwert länger aus, gilt er als fehlend. |
| `ROOM_OnMissing` | enum | 0 Grundstufe weiter · 1 Stillstand | 0 | | Verhalten bei fehlenden Messwerten. |

### Unterseite „CO₂ und VOC"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_CO2GW0` … `_GW4` | uint16 | 400…4000 ppm | 700/850/1000/1300/1700 | | Grenzwert-Treppe CO₂. |
| `ROOM_VOCUnit` | enum | 0 ppb (DPT 9.008) · 1 Index (DPT 5.010) · 2 Index (DPT 7.001) | 0 | | Format des VOC-Sensors. |
| `ROOM_VOCGW0` … `_GW4` | uint16 | 0…5000 | ppb: 300/500/750/1000/1500 · Index: 100/150/200/300/400 | | Grenzwert-Treppe VOC, Einheit wie gewählt. |

### Unterseite „Temperatur"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_FrostTemp` | uint8 | 5…16 °C | 8 | | Unter dieser Innentemperatur wird die Lüftung ausgeschaltet (Schutzbetrieb). Rückkehr 2 K darüber. |
| `ROOM_HeatTemp` | uint8 | 26…40 °C | 30 | | Über dieser Innentemperatur bei wärmerer Außenluft: Schutzbetrieb. |
| `ROOM_TempGap` | uint8 | 0…10 K | 3 | | Erst ab diesem Abstand zwischen Innen- und Außentemperatur greift die Temperaturführung. Größer = weniger Konflikte mit der Heizungsregelung. |
| `ROOM_CoolStage` | enum | 1…4 | 3 | | Stufe bei freier Kühlung. |
| `ROOM_HeatStage` | enum | 1…4 | 2 | | Stufe bei Warmluftnutzung. |
| `ROOM_TempLeadMode` | enum | 0 Eingang (0 sperrt) · 1 Ausgang (Heizungssperre) | 0 | | KO „Temperaturführung aktiv": als Eingang schaltet eine 0 die Führung ab; als Ausgang meldet 1, dass frei gekühlt wird — zum Sperren der Heizung. |

### Seite „Bedienung und Abluft"

| Parameter | Typ | Werte | Vorgabe | Sichtbar | Hilfe |
|---|---|---|---|---|---|
| `ROOM_ExhaustStage` | enum | 1…4 | 4 | | Stufe bei Abluftanforderung. Beim ego Stufe 4 = Abluftstoß beider Motoren. |
| `ROOM_ExhaustLead` | time | 0 s … 5 min | 0 | | Vorlaufzeit bis zum Abluftbetrieb. |
| `ROOM_ExhaustLag` | time | 0 … 60 min | 15 min | | Nachlaufzeit nach Wegnahme. |
| `ROOM_ExhaustInterval` | enum | 0 dauernd · 1 min/5 min · 2/5 · 1/15 · 2/15 · 5/15 · 1/30 · 2/30 · 5/30 · 10/30 | 0 | | Aktivzeit / Periode bei intermittierender Abluft. |
| `ROOM_ExhaustSendSupply` | bool | | ja | | Während der Abluftanforderung Zuluftanforderung (KO 34) senden. |
| `ROOM_LockBehaviour` | enum | 0 Stillstand · 1 Grundstufe | 0 | | Verhalten bei Sperre (Fenster offen, Kamin). |

### KOs je Raum (Block 40, Nummer = 20 + 40·(n−1) + #)

| # | Name | DPT | Flags | Sendet / Bemerkung |
|---|---|---|---|---|
| 0 | Betriebsart | 20.102 | K L S | 0 = Auto → DefaultMode |
| 1 | Zwangsbetriebsart | 20.102 | K L S | mit ForcedModeTimeout |
| 2 | Nacht | 1.003 | K L S | |
| 3 | Zwangsobjekt 1 | 1.003 | K L S | |
| 4 | Zwangsobjekt 2 | 1.003 | K L S | |
| 5 | Zwangsobjekt 3 | 1.003 | K L S | |
| 6 | Stufe manuell | 5.100 | K L S | |
| 7 | Stufe manuell % | 5.001 | K L S | |
| 8 | Stufe +/− | 1.007 | K L S | |
| 9 | Handbetrieb aktiv | 1.012 | K L S Ü | 0 beendet Handbetrieb; sendet bei Änderung |
| 10 | Sommer | 1.001 | K L S | |
| 11 | Betriebsweise | 5.010 | K L S | 0 auto · 1 WRG · 2 Zuluft · 3 Abluft |
| 12 | Abluftanforderung | 1.003 | K L S | |
| 13 | Sperre | 1.003 | K L S | |
| 14 | rF innen | 9.007 | K L S | |
| 15 | T innen | 9.001 | K L S | |
| 16 | rF außen | 9.007 | K L S | nur OutdoorSource = 0 |
| 17 | T außen | 9.001 | K L S | nur OutdoorSource = 0 |
| 18 | CO₂ | 9.008 | K L S | |
| 19 | VOC (2 Byte) | 9.008 / 7.001 | K L S | nur VOCUnit = ppb oder Index 0…65535 |
| 20 | T Soll | 9.001 | K L S | |
| 21 | Temperaturführung aktiv | 1.003 | Eingang: K L S · Ausgang: K L Ü | nach TempLeadMode |
| 22 | Feuchteführung aktiv | 1.003 | K L S | |
| 23 | CO₂-Führung aktiv | 1.003 | K L S | |
| 24 | VOC-Führung aktiv | 1.003 | K L S | |
| 25 | Raumanforderung Stufe | 5.100 | K L Ü | bei Änderung |
| 26 | Raumanforderung % | 5.001 | K L Ü | bei Änderung |
| 27 | Betriebsart Status | 20.102 | K L Ü | bei Änderung; erweiterte Betriebsarten → nächste Standardart |
| 28 | Betriebsart Status erweitert | 5.010 | K L Ü | 1…7 |
| 29 | Betriebsweise Status | 5.010 | K L Ü | |
| 30 | Schutz aktiv | 1.001 | K L Ü | |
| 31 | Feuchtevergleich sperrt | 1.001 | K L Ü | |
| 32 | abs. Feuchte innen | 9.029 | K L Ü | g/kg, bei Änderung ≥ 0,2 |
| 33 | abs. Feuchte außen | 9.029 | K L Ü | |
| 34 | Zuluftanforderung | 1.003 | K L Ü | nach ExhaustSendSupply |
| 35 | Intervall aktiv | 1.001 | K L Ü | |
| 36 | VOC (1 Byte) | 5.010 | K L S | nur VOCUnit = Index 0…255; ein KO hat genau eine Größe |
| 37–39 | Reserve | | | |

---

## 5. Prüfliste für die fertige knxprod

- [ ] `OpenKNXproducer check` ohne Warnung
- [ ] Testprojekt: `FAN_Hardware` = Rev 0.1 → genau 4 Lüfterseiten; = 12 → 12 Seiten
- [ ] Räume 1…3 aktiviert → drei Raumseiten, KO-Nummern 20…139; nicht aktivierte Räume fehlen im Baum
- [ ] Lüfter 3 Typ ego → Kennlinienseite zeigt M1/M2 und Boost, Phase ausgeblendet
- [ ] Lüfter 4 Typ RA → nur Richtung A, keine B-Werte
- [ ] Betriebsart Ruhe: keine Führungsparameter sichtbar
- [ ] Verbund 1 Regel „Raum führt" → LeadRoom erscheint
- [ ] Alle Grenzwert-Treppen: aufsteigende Vorgaben; Hilfetext erklärt die Treppe
- [ ] Jeder Parameter hat eine Hilfedatei in `Baggages/Help_de/`
- [ ] Gruppenadressen in der ETS auf drei Räume und drei Lüfter für das Haus anlegbar,
      ohne dass ein KO fehlt (Probe: Duschen-Szenario aus Phase 4b von Hand durchgehen)
