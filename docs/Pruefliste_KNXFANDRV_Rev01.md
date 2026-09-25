# Prüfliste vor der Fertigung — KNXFANDRV Rev 0.1

**Stand:** 20.09.2026
**Gegenstand:** Schaltplansatz und Layout der KNX-Lüftersteuerung KNXFANDRV
**Bezug:** `Referenzdesign_Rev4_GP8413.md`, Abschnitt 0.5

Abhaken von oben nach unten. Die Blocker stoppen die Bestellung, alles darunter
kostet nur Zeit, wenn es liegenbleibt.

---

## A — Blocker: vor dem Fertigungsauftrag zu klären

### A1 · Isolationsbarriere im Kupfer nachweisen

Die einzige Daseinsberechtigung des ADuM1250 ist, dass `PGND` und `BCU_GND`
sich nirgends berühren. Ein durchgehender Massepour macht das leicht kaputt,
ohne dass der DRC meckert — beide Netze sind gültig, sie dürfen sich nur nicht
treffen.

- [ ] In KiCAD nacheinander **nur** `PGND` und **nur** `BCU_GND` hervorheben,
      in **jeder** Lage, auch in den Füllpolygonen
- [ ] Besonders unter U1 hindurch prüfen: kein Via, keine Insel, kein
      Zonenausläufer
- [ ] Kleinsten Abstand zwischen den beiden Zonen messen — Ziel ≥ 4 mm,
      besser 6 mm
- [ ] **Abstand J2 (12 V) zu J100 (KNX-Klemme)**: beide liegen an der unteren
      Kante und gehören zu verschiedenen Zonen. Im Layoutbild sind es grob
      5 mm. Nachmessen, und wenn möglich vergrößern.
- [ ] Gegenprobe im ERC/DRC: gibt es eine Regel, die die beiden Netzklassen
      auf Abstand hält? Wenn nicht, anlegen.

> Fällt A1 durch, ist die Trennung nur gezeichnet, nicht gebaut.

### A2 · Prüfpunkte nachrüsten

Es ist keiner im Layout. Der **erste** Inbetriebnahmeschritt ist die
Formatentscheidung O17: Register 0x01 auf 0x11 setzen, dann `0x4000` und
`0x8000` schreiben und die Ausgangsspannung messen. Ohne Messpad bedeutet das,
an einem 0,35-mm-Pin des ESOP10 oder an einer bestückten Klemme zu messen.

- [ ] Je ein Testpad an **S1, S2, S3, S4**
- [ ] Je ein Testpad an **+12 V**, **+5V_ISO**, **PGND**
- [ ] Optional: **+3V3_BCU** und **BCU_GND** auf der KNX-Seite
- [ ] Pads so setzen, dass eine Krokodilklemme oder ein Tastkopf drankommt —
      1,5 mm rund genügt

Sieben Pads, kein Bauteil, kein Cent. Sie entscheiden, ob die Inbetriebnahme
eine halbe Stunde oder einen Abend dauert.

### A3 · Silkscreen „12W"

Auf dem Bestückungsdruck steht an J3…J6 über der 12-V-Klemme **12W**.

- [ ] Prüfen, ob das wirklich der Text ist oder nur der Font (V wie W
      gerendert)
- [ ] Falls Text: auf **12V** korrigieren — viermal
- [ ] Bei der Gelegenheit die Klemmenbeschriftung gegen die Belegung prüfen:
      **1 = 12 V, 2 = S, 3 = GND**

---

## B — Vor der Bestellung entscheiden

### B1 · TC25 statt TC50?

Bestückt ist **GP8413-TC50-EW**. Der Temperaturkoeffizient verdoppelt sich
damit von 25 auf 50 PPM/°C, über 30 K also ±15 mV statt ±7,5 mV auf den
Vollausschlag. Quadratisch summiert etwa **±37 mV statt ±33 mV** — das
Fehlerbudget trägt es.

- [ ] Prüfen, ob **GP8413-TC25-EW** bei LCSC lieferbar ist
- [ ] Falls ja: tauschen. Gleiches Gehäuse, gleiche Pinbelegung, kein
      Layoutaufwand.
- [ ] Falls nein: TC50 ist in Ordnung, aber das Totband aus Messung M3 wird zur
      harten Abnahmebedingung

### B2 · Pads für C9 und C12 (10 µF an VCC der DACs)

Ich hatte sie gestrichen, mit Verweis auf das DFRobot-Referenzmodul. Das Modul
ist aber eine Briefmarke. Auf dieser Platine läuft die 12-V-Schiene vom
Eingang unten links über rund 60 mm nach oben und führt dabei bis zu **1 A
Lüfterstrom**, bevor sie U2 und U3 speist.

Die GP8413-Datenblätter geben **keine PSRR-Angabe**. Es ist also nicht
bekannt, wie gut der Chip Welligkeit auf VCC vom Ausgang fernhält.

- [ ] Zwei freie **0805-Pads** an den VCC-Pins von U2 und U3 vorsehen,
      unbestückt
- [ ] Falls P12 zeigt, dass die Ausgangsspannung beim Stufenwechsel springt:
      10 µF bestücken

Zwei Pads kosten nichts. Eine zweite Platinenrunde kostet zwei Wochen.

---

## C — Nachverfolgen im Schaltplan

### C1 · `+5v_ISO` mit kleinem v

Am Driver-Blatt und an U3 Pin 3 steht `+5v_ISO`, überall sonst `+5V_ISO`.
Über die Hierarchiepins funktioniert es, weil Blattpin und Label innen
zusammenpassen. **KiCAD unterscheidet Groß- und Kleinschreibung.** Sobald
jemand im Driver-Blatt ein globales Label `+5V_ISO` setzt, hängt es in der
Luft.

- [ ] Alle Vorkommen auf eine Schreibweise bringen
- [ ] Dasselbe für `PGND`, `SDA_I`, `SCL_I` quer über alle vier Blätter prüfen

### C2 · SDA und SCL vom Controller zum Isolator

Im Wurzelblatt kreuzen sich die beiden Leitungen optisch zwischen dem
Controller- und dem Isolator-Block.

- [ ] Einmal verfolgen, dass `SDA_1` → `SDA_BCU` und `SCL_1` → `SCL_BCU`
      landet, nicht vertauscht

Ein vertauschtes Paar ist mit einer Drahtbrücke zu retten, aber nur wenn man
weiß, dass man danach suchen muss.

### C3 · Masse von C7 direkt an U4 Pin 1

TI schreibt für den TLV704 ausdrücklich, die Masse des Ausgangskondensators
**direkt an den GND-Pin** zu führen, nicht über die Massefläche.

- [ ] Im Layout prüfen, ob C7 eine eigene kurze Bahn zu U4 Pin 1 hat

### C4 · U4 Pin 4 und 5

Sind als NC markiert und offen gelassen. Das ist laut Datenblatt zulässig.
An PGND gelegt verbesserten sie die Wärmeabfuhr — bei 70 mW Verlustleistung
und 213 K/W ist das etwa 15 K Übertemperatur, also unkritisch.

- [ ] Keine Änderung nötig. Nur zur Kenntnis.

### C5 · ADuM1250 VDD1 bei 3,3 V

Die Pinbelegung ist durch die Platine geklärt (O5). Offen bleibt, ob VDD1 für
3,3 V spezifiziert ist.

- [ ] Datenblatt aufrufen, Versorgungsbereich VDD1 prüfen
- [ ] Bei der Gelegenheit: Stromaufnahme VDD1 gegen das Budget der BCU halten

Nach der TLV702/TLV704-Verwechslung ist das keine Förmlichkeit.

---

## D — Für die Inbetriebnahme vormerken

### D1 · I²C am Frontpanel während der Store-Sequenz

Der I²C-Bus liegt zusätzlich auf **J110 Pin 7 und 8**. Die EEPROM-Sequenz
sendet Frames an das Adressbyte `0x10`, also an die **reservierte 7-Bit-Adresse
0x08**, und arbeitet mit einem Drei-Bit-Frame ohne ACK.

- [ ] In die Inbetriebnahmeanleitung: **Frontpanel abziehen**, bevor der
      Einschaltwert gespeichert wird — oder sicherstellen, dass das
      angeschlossene Gerät die Sequenz aushält

### D2 · Firmware-Pins

Die Bitbang-Routine für die Store-Sequenz muss **GPIO2 (SDA) und GPIO3 (SCL)**
umschalten, nicht GP26/GP27 wie in früheren Entwurfsfassungen.

- [ ] `gpio_set_function(2, …)` und `gpio_set_function(3, …)`
- [ ] Nach der Sequenz zurück auf `GPIO_FUNC_I2C`

### D3 · Bezeichnerzuordnung in die Stückliste übernehmen

Auf der Platine gilt: **C14→S1, C16→S2, C15→S3, C17→S4** und
**D2→S1, D4→S2, D3→S3, D5→S4**. Mein Entwurfsdokument hatte sie der Reihe
nach zugeordnet.

- [ ] Stückliste dem Schaltplan anpassen, nicht umgekehrt

### D4 · Reihenfolge der ersten Messungen

1. **O17 / Prüfschritt P6** — Busformat klären. `0x4000` und `0x8000` an S1.
   Entscheidet jede Zahl in Abschnitt 3.4 des Referenzdokuments.
2. **P15** — Store-Sequenz durch den ADuM1250, mit Logikanalysator beidseitig
3. **P11** — EEPROM: sichert er auch das Bereichsbit (Register 0x01)?
4. **P4** — Isolationsprüfung `BCU_GND` gegen `PGND`, > 10 MΩ
5. Erst danach die Lüfter anschließen

---

## Was an der Platine gut gelöst ist

Nicht alles auf einer Prüfliste muss ein Mangel sein. Drei Dinge sind besser
als im Entwurf:

**Die Masse ist je Kanal ein eigenes Netz** (CH1_PGND…CH4_PGND) und läuft erst
auf der Platine zusammen. Das erfüllt die LUNOS-Vorgabe „sternförmig von der
Steuerung aus, Durchschleifen nicht zulässig" auf der Leiterplatte statt erst
im Kabelkanal.

**R4 und R5 sind bestückt.** Mein Entwurf hatte sie als DNP geführt, weil er
von einer getrennten REG1-Base ausging, die eigene Pull-ups mitbringt. Auf
einer Platine mit eigenem RP2040 bringt sie niemand mit — die Platine hat
recht, der Entwurf hatte eine falsche Annahme.

**Die Zenerklemmung an Q1 funktioniert wie gedacht.** Der AO3401A ist mit
V(GS,max) = ±12 V für 12 V Dauerbetrieb eigentlich zu knapp; DZ1 mit 8,2 V
begrenzt die Gate-Source-Spannung und macht das unkritisch. Das war der
offene Punkt 1 aus Rev 3, und er ist damit sauber erledigt.
