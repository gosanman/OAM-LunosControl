# KWL-Simulator

Simuliert das Lüftungsmodul des KNXFANDRV am Schreibtisch: Parameter wie in der
ETS setzen, Eingangsobjekte senden, Ausgänge S1…S4 und alle Ausgangsobjekte
beobachten. Ohne Platine, ohne Bus, ohne ETS.

**Es läuft der echte Firmware-Code.** Der Simulator rechnet die Logik nicht nach.
Er übersetzt die unveränderten Quellen aus `lib/OFM-KwlControl/src` für den PC und
ersetzt nur, was die Firmware von außen aufruft: Parameterspeicher, Gruppenobjekte,
Logger, Flash, LED, Funktionstaste und die beiden GP8413. Was hier falsch läuft,
läuft auch im Gerät falsch.

## Starten

Einmalig bzw. nach jeder Änderung an Firmware oder ETS-Applikation:

```
OpenKNXproducer create --Debug -h include/knxprod.h src/Kwl    (Task "knxprod erzeugen")
powershell -File sim/build.ps1                                  (Task "Simulator bauen")
```

Dann:

```
~/.platformio/penv/Scripts/python.exe sim/kwlsim.py            (Task "Simulator starten")
```

Die Oberfläche öffnet sich unter <http://127.0.0.1:8765>. Optionen: `--port`,
`--no-browser`, `--project "Name"`. Der aktive Reiter steht in der Adresse, etwa
`#env` oder `#params`, und bleibt beim Neuladen erhalten.

Kanalnamen (ETS-Parameter „Beschreibung“) werden angezeigt und mit dem Projekt
gespeichert. Ins Parameterabbild kommen sie nicht, weil sie auch im Gerät keinen
Speicherplatz haben.

Voraussetzungen: MinGW-GCC (bereits für `pio test -e native` installiert) und
Python 3. Python-Pakete braucht es keine. Die Oberfläche läuft im Browser, weil
auf diesem Rechner keine Python-Installation Tkinter mitbringt.

## Bedienung

| Bereich | Was er tut |
|---|---|
| Kopfleiste | Zeit (anhalten, 1× bis 600×, Einzelschritte), KNX-Bus und 12 V schalten, Programmieren, Neustart, Entladen, Funktionstaste, Info-LED, Projekte |
| Bedienung | Ein Unterreiter je aktivem Raum: links der Raum, rechts die Lüfter, die ihm in der ETS zugeordnet sind, mit Stellkanal, Takt, Phase und Verbundpartnern. Alle Eingangsobjekte mit passendem Eingabefeld, in klappbaren Abschnitten. Messwerte lassen sich zyklisch senden, wie ein Sensor. Darunter die Ausgangsobjekte mit ihrem letzten Wert |
| Umgebung | Außenklima und Raumluft mit Zufallsmuster, siehe unten |
| Parameter | Die ETS-Seiten des Lüftungsmoduls mit derselben Sichtbarkeitslogik wie in der ETS. Änderungen werden erst mit **Programmieren** wirksam, mit Download und Neustart wie am Gerät |
| Objekte | Alle Objekte, die die ETS mit der programmierten Parametrierung einblendet |
| Telegramme | Jedes gesendete und empfangene Telegramm mit Zeit, DPT und Wert |
| Konsole | `kwl st`, `kwl grp`, `kwl r1`, `kwl f1` usw., wahlweise über das Diagnose-KO |
| Prüfung | Invariantenwächter und Hinweise, siehe unten |
| rechte Spalte | S1…S4 mit Spannung, Registerwert und Deutung, Verlauf der Stellsignale, Zustand von Lüftern, Räumen und Takt |

Projekte liegen in `sim/projects/` als JSON mit den geänderten Parametern.
Mitgeliefert: *Aufbau A* und *Aufbau B* aus dem Testplan, *Bad mit ego +
e²60-Paar* und *Neues Gerät (Vorgaben)*.

## Umgebungssimulation

Im Reiter **Umgebung** erzeugt der Simulator Temperatur, relative Feuchte, CO₂
und VOC für jeden aktiven Raum, dazu die Außenwerte. Er sendet sie wie echte
Sensoren auf die Objekte, die die ETS einblendet. Ein Sensor sendet bei Änderung
um eine Schwelle und spätestens nach dem Sendeintervall.

Die Umgebung läuft auf der Simulationszeit. Bei 1× ist das Echtzeit, die
Knöpfe 10× bis 600× beschleunigen sie mit. Ein Tag dauert bei 600× gut zwei
Minuten.

| Einstellung | Wirkung |
|---|---|
| Jahreszeit | Winter, Übergang, Sommer, Hitzewelle: Tagesgang draußen, Heizperiode ja oder nein |
| Start um | Tageszeit beim Start der Simulationszeit |
| Zufall | Stärke von Wetterschwankung und Messrauschen, 0 = glatte Kurven |
| Kopplung an Lüftung | Der Volumenstrom, den die Firmware je Lüfter meldet, wirkt auf den Raum zurück. Damit läuft der Regelkreis geschlossen: CO₂ steigt, die Führung hebt die Stufe, das CO₂ fällt |
| Ereignisse | Duschen im Bad, Kochen in der Küche, zufällig in ihren Zeitfenstern |
| Startwert | Gleicher Startwert ergibt denselben Ablauf. „Neu würfeln“ zieht einen anderen |
| je Raum | Profil (Wohnraum, Schlafzimmer, Bad, Küche, Büro), Volumen, Personen, Solltemperatur, welche Größen gesendet werden |

Das Modell ist bewusst einfach: eine Bilanz je Raum für CO₂, absolute Feuchte,
Temperatur und VOC. Gegenüber stehen Infiltration von 0,15 1/h und die
gemeldeten Volumenströme, bei Wärmerückgewinnung mit 85 % Wirkungsgrad. Die
Anwesenheit wird alle 10 min nach dem Tagesprofil neu gewürfelt. Für die
VOC-Indexformate (5.010, 7.001) wird ein Index um 100 als üblich angenommen.
Das ist eine Annahme, keine Messung.

Die Einstellungen werden mit dem Projekt gespeichert. Manuelle Eingaben im Reiter
Bedienung bleiben möglich. Ein von der Umgebung geführter Sensor überschreibt
sie aber beim nächsten Senden. Wer einen Wert von Hand führen will, nimmt die
Größe im Raum unter „Sendet“ heraus.

## Bewohner

Im Reiter **Umgebung** unten steht der **Bewohner**: ein simulierter Mensch, der
die Anlage über dieselben Objekte bedient wie ein Taster oder eine
Visualisierung. Er braucht die Umgebungssimulation, um zu sehen, wer da ist,
ob geduscht wird und ob die Luft stickig ist.

| Verhalten | Was er tut |
|---|---|
| Tagesablauf | morgens Nacht aus und Komfort, werktags tagsüber Standby, abends Komfort, zur Schlafenszeit Nacht ein. Die Zeiten streuen von Tag zu Tag |
| Handstufe | bei CO₂ über 1300 ppm oder rF über 70 % im besetzten Raum die Handstufe 3 |
| Fenster | bei CO₂ über 1600 ppm das Fenster auf: Sperre 1, in der Umgebung 4 1/h Luftwechsel, nach 10–20 min wieder zu |
| Badlüftung | beim Duschen oder Kochen die Abluftanforderung, danach aus. In 15 % der Fälle vergisst er sie |
| Taste | selten die Funktionstaste (Stoßlüften), wenn sie in der ETS so belegt ist |
| Sommer | im Sommer das Objekt „Sommer“ |

Jede Aktion steht mit Begründung im Protokoll unter den Einstellungen und als
empfangenes Telegramm im Reiter Telegramme.

## Wochenlauf

`weekrun.py` rechnet ohne Oberfläche mehrere Tage durch, mit Umgebung, Bewohner
und eingebauten Störungen (12 V aus, KNX-Bus aus, Neuprogrammierung, 12 V kurz
weg). Eine Woche dauert etwa eine Minute. Der Bericht landet in `build/reports/`.

```
~/.platformio/penv/Scripts/python.exe sim/weekrun.py --project "Aufbau B (Testplan)" --season Winter
~/.platformio/penv/Scripts/python.exe sim/weekrun.py --project "Bad mit ego + e2 60-Paar" --season Sommer --days 7 --seed 3
```

Er prüft laufend die Meldungen des Wächters und die Fehlerzeilen der Firmware.
Dazu kommen drei Prüfungen:

- **Plausibilität:** Passt die gemeldete Stufe und Richtung je Lüfter zur Spannung am DAC?
- **Wirkung:** Kommt jede Bedienung des Bewohners im Raum an?
- **Erholung:** Ist das Gerät drei Minuten nach jeder Störung wieder in Ordnung?

Am Ende stehen Statistiken zu Raumluft, Stufen, Fehlercodes und Buslast.

Läuft die App gerade und sperrt `build/kwlsim.exe`, baut man den Kern unter
anderem Namen und gibt ihn per Umgebungsvariable vor:

```
powershell -File sim/build.ps1 -Exe kwlsim_test.exe
$env:KWLSIM_EXE = "simuild\kwlsim_test.exe"
```

## Was der Simulator prüft

Nach jedem Durchlauf der Firmware:

| Prüfung | Meldet |
|---|---|
| Invariante 3 | Ein Sollwert wurde geschrieben, bevor das Bereichsregister 0x11 stand |
| Invariante 5 | Ein bipolarer Kanal wechselt die Seite von 5 V ohne Aufenthalt bei 5,00 V von mindestens der Totzeit seines Takts |
| Invariante 6 | Die beiden Motoren eines ego wurden in getrennten Frames geschrieben |
| DPT | Die Firmware liest oder sendet ein Objekt mit einem DPT, den die KNX-Bibliothek nicht kodieren kann. Am Gerät passiert das stumm |
| Volllast-Signal | Ein bipolarer Kanal lag unter 0,3 V, mit Dauer |

`sim/selftest.py` spielt Abläufe aus dem Testplan ohne Oberfläche durch, Task
„Simulator-Selbsttest“. Ein rotes Ergebnis heißt: Die Firmware weicht von der
Erwartung ab.

## Wie es gebaut ist

```
Browser (web/)  ⇄  kwlsim.py (HTTP, Zeit, Projekte)  ⇄  build/kwlsim.exe (stdin/stdout, JSON-Zeilen)
                                                          ├─ Firmware: lib/OFM-KwlControl/src/*.cpp, unverändert
                                                          ├─ DPT-Kodierung: lib/knx/src/knx (dpt, dptconvert, knx_value)
                                                          └─ sim/core: OpenKNX-Ersatz, GP8413-Modell, Inspektion
```

| Datei | Inhalt |
|---|---|
| `core/OpenKNX.h` | Ersatz für OpenKNX und die KNX-Stack-Schnittstelle, soweit das Modul sie benutzt |
| `core/sim_runtime.cpp` | Parameterspeicher, Gruppenobjekte (Verhalten wie `group_object.cpp`), Logger, Flash, LED |
| `core/sim_board.cpp` | Zwei GP8413 statt `Gp8413Drive.cpp`; Konstanten aus dem echten Board-Header |
| `core/sim_inspect.cpp` | Zustand für die Anzeige, Invariantenwächter |
| `core/sim_main.cpp` | Befehlsschleife, Start, ETS-Download, Bus- und 12-V-Ausfall |
| `gen_meta.py` | Liest `src/Kwl.debug.xml`: Vorgabe-Parameterabbild, Parameter, Objekte, ETS-Seiten |
| `kwlcore.py` | Python-Anbindung an den Kern, auch für eigene Testskripte |
| `kwlsim.py` | Die App: HTTP-Server, Simulationszeit, Projekte |
| `environment.py` | Umgebungssimulation: Außenklima, Raumbilanz, Sensoren |
| `occupant.py` | Bewohner: Tagesablauf, Handstufe, Fenster, Badlüftung, Taste |
| `weekrun.py` | Wochenlauf mit Störungen und Bericht |
| `web/` | Oberfläche, ohne Fremdbibliotheken |

Der Kern lässt sich auch direkt mit einer Befehlsdatei füttern, etwa für
Regressionstests: `build/kwlsim.exe < szenario.txt`. Die Befehle stehen im Kopf
von `core/sim_main.cpp`.

## Grenzen

- **Nur das Lüftungsmodul.** Logikmodul, Konfigurationstransfer und die
  OpenKNX-Grundfunktionen (Heartbeat, Uhrzeit, Startverzögerung) laufen nicht mit.
- **Bereich des GP8413 nach dem Einschalten:** angenommen 0…5 V, nicht gemessen.
  Die Firmware schreibt den Bereich immer zuerst; ein Verstoß würde gemeldet.
- **Zeit in Schritten von 10 ms.** Die Firmware läuft am Gerät im
  Millisekundentakt. Für Zykluszeiten, Totzeit und Vorlauf ist das genau genug.
- **Die Lüfter selbst sind nicht modelliert.** Die Anzeige deutet die Spannung
  (Richtung, Prozent), dreht aber nichts hoch. Ein kurzes Volllast-Signal ist
  daher nur ein Hinweis.
- **Lesezugriffe der Private-Felder:** `sim_inspect.cpp` öffnet sie für die
  Anzeige per `#define private public`. Das ist ein Kniff für Testwerkzeuge; der
  Firmware-Code bleibt unverändert.
- **Sichtbarkeit der ETS:** nachgebildet für `choose`/`when` und `Assign`. ETS-Skripte
  und Schaltflächen sind nicht nachgebildet.
