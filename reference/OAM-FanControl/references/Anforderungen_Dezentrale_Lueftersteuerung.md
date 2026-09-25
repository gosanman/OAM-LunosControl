> # ⚠ Erstes Konzeptpapier — nicht mehr der gültige Stand
>
> Dies war das **erste Konzeptpapier**, geschrieben aus der Sicht von Heizungsbauer,
> Kältetechniker und Zimmerer — also von den Gewerken, die die Anlage später bauen und
> betreiben, **ohne Kenntnis des KNX-Busses**. Das war Absicht: die Funktion sollte zuerst
> aus der Anlage heraus beschrieben werden, nicht aus dem Bus heraus. Was der Bus daraus
> macht, kam bewusst erst im zweiten Durchgang dazu.
>
> Entsprechend fehlen hier alle Bus-Themen (DPTs, Gruppenadress-Topologie, Sendebedingungen)
> und einige Festlegungen widersprechen der Umsetzung — unter anderem bei M-6
> (Freigabe-Persistenz), bei der Stellgrößen-Kennlinie, bei `KNOTEN_ID`/`GRUPPEN_ID` und bei
> `DEAKTIVIERT`. Seit dem 14.08.2026 nicht mehr nachgeführt.
>
> **Wo die Software hiervon abweicht, steht in `Review_Anforderungen_KNX-Sicht.md`** — mitsamt
> den Abweichungen, die noch aufzulösen sind.
>
> Dieses Dokument bleibt als Beleg dafür, was fachlich ursprünglich gefordert war. Es ist damit
> die eine Hälfte des Paares: **Anforderung hier, Abweichung dort.**

---

# Anforderungen – Steuerung für dezentrale Lüfter

**Dokumenttyp:** Funktionale Anforderungen für die Firmware-Entwicklung
**Stand:** 14.08.2026
**Umfang:** Begriffe, Konfiguration je Knoten, Ein- und Ausgänge, Betriebsarten, Master-Rolle.

---

## 1. Begriffe

| Begriff | Bedeutung |
|---|---|
| **Lüfterknoten** (kurz: Knoten) | Ein einzelner Lüfter mit seiner Ansteuerung. Kleinste adressierbare Einheit. |
| **Gruppe** | Mehrere Knoten, die gemeinsam gesteuert werden. |
| **Master** | Der eine Knoten einer Gruppe, der die Vorgaben erzeugt und an die Slaves verteilt. |
| **Slave** | Jeder andere Knoten der Gruppe. Setzt die Vorgabe des Masters mit seinen lokalen Parametern um. |
| **Phase / Gegenphase** | Zuordnung eines Knotens innerhalb seiner Gruppe. Knoten in *Phase* fördern gegenläufig zu Knoten in *Gegenphase*. Die Zuordnung ist fest, die tatsächliche Richtung wechselt mit dem Takt. |
| **Paar** | Kein eigener Begriff, sondern der Sonderfall, dass in Phase und Gegenphase je genau ein Knoten liegt. |
| **Richtung A / Richtung B** | Die beiden physikalischen Förderrichtungen eines reversiblen Knotens. Ergibt sich aus Phase und Taktzustand. |
| **Taktzustand** | Welche der beiden Halbwellen gerade aktiv ist. Wird vom Master vorgegeben und gilt für die ganze Gruppe. |
| **Zyklus** | Zeitspanne zwischen zwei Richtungswechseln. |
| **Leistung** | Einheitliche Vorgabegröße 0–100 %. 0 % = Stillstand, 100 % = maximale Leistung des Geräts. |
| **Anteilsfaktor** | Fester Faktor 0–100 % je Knoten. Bestimmt, welchen Anteil der Gruppenvorgabe dieser Knoten umsetzt. |

---

## 2. Konfiguration je Knoten

Fest eingestellte Eigenschaften. Sie beschreiben **Hardware und Rolle** des Knotens, werden zur Laufzeit
nicht von außen verändert und bestimmen, welche Ein- und Ausgänge überhaupt existieren.

Master und Slave verwenden **dieselbe Firmware**; es unterscheidet sich nur die Rolle. Die Spalte „Gilt für"
gibt an, wo ein Parameter bzw. Kanal tatsächlich existiert: `beide`, `nur Master` oder `nur Slave`.

| Parameter | Gilt für | Werte | Wirkung |
|---|---|---|---|
| `KNOTEN_ID` | beide | Zahl | Adressierung |
| `GRUPPEN_ID` | beide | Zahl | Zugehörigkeit zur Gruppe |
| `DEAKTIVIERT` | beide | ja / nein | Knoten administrativ stillgelegt (Service, Diagnose, nicht bestückter Kanal). Bleibt über Neustart erhalten |
| `IST_MASTER` | beide | ja / nein | Genau ein Knoten je Gruppe ist Master |
| `REVERSIBEL` | beide | ja / nein | Bestimmt, ob Richtungsflags existieren und wie die Stellgröße abgebildet wird |
| `HAT_RUECKMELDUNG` | beide | ja / nein | Bestimmt, ob der Drehzahl-Ausgang existiert |
| `PHASE` | beide | Phase / Gegenphase | Zuordnung innerhalb der Gruppe. Nur bei `REVERSIBEL = ja` |
| `ANTEILSFAKTOR` | beide | 0–100 % | Anteil an der Gruppenvorgabe |
| `KENNLINIE_STELLGROESSE` | beide | Stützstellen | Umrechnung **Leistung (0–100 %) -> physikalische Stellgröße** (z. B. Tastverhältnis). Steuernd |
| `KENNLINIE_VOLUMENSTROM` | beide | Endpunkt + 2 freie Punkte je Richtung | Zuordnung **Drehzahl -> Volumenstrom (m³/h)**. Nullpunkt (0, 0) implizit; Endpunkt (max. Drehzahl, zugehöriger Volumenstrom) und 2 frei platzierbare Zwischenpunkte konfigurierbar. Rein anzeigend. Nur bei `HAT_RUECKMELDUNG = ja`. Bei `REVERSIBEL = ja` **je Richtung eigene Punkte**, **nicht** gespiegelt |
| `VOLUMENSTROM_INVERTIEREN` | beide | ja / nein | Dreht das Vorzeichen von `VOLUMENSTROM_IST` um. Gleicht Einbaulage bzw. Verdrahtung an die Vorzeichenkonvention an |
| `FREIGABE_UEBERWACHUNGSZEIT` | beide | Zeit | Max. Alter des Freigabesignals, bevor auf gesperrt geschaltet wird (siehe E-1b) |
| `STOSSLUEFTUNG_DAUER` | beide | Zeit | Laufzeit eines Stoßlüftungs-Anstoßes |
| `STOSSLUEFTUNG_LEISTUNG` | beide | 0–100 % | Leistung während der Stoßlüftung (typisch 100 %) |
| `TOTZEIT` | beide | Zeit | Pause zwischen Abschalten und Wiederanlauf bei Richtungswechsel |
| `ZYKLUSZEIT` | **nur Master** | Zeit | Dauer je Förderrichtung |

**Regeln**

- **K-1** Bei `REVERSIBEL = nein` existieren die Richtungs-Ein- und -Ausgänge nicht und dürfen nicht angeboten werden.
- **K-2** Bei `HAT_RUECKMELDUNG = nein` existiert der Drehzahl-Ausgang nicht. Es wird **kein** Ersatz- oder Nullwert geliefert.
- **K-3** Bei `REVERSIBEL = ja` gilt eine bipolare Stellgröße: Mittelstellung = Stillstand, ein Ende = volle Leistung Richtung A, das andere Ende = volle Leistung Richtung B. Mittelwert und Endenzuordnung sind Teil von `KENNLINIE_STELLGROESSE`.
- **K-4** Es gibt genau drei Größen, die klar zu trennen sind: die **Leistung** (0–100 %, der logische
  Befehl), die **Stellgröße** (das physikalische Signal am Gerät) und die **Drehzahl** (gemessener
  Rückmeldewert, siehe K-2). `KENNLINIE_STELLGROESSE` rechnet ausschließlich zwischen Leistung und Stellgröße um.
  Eine Umrechnung zwischen Leistung und Drehzahl findet **nicht** statt – die Steuerung arbeitet
  stellgrößengesteuert, nicht drehzahlgeregelt.
- **K-5** Die **Steuerung** rechnet ausschließlich in Prozent. Volumenströme sind **keine Stell- oder Regelgröße**; insbesondere basiert der `ANTEILSFAKTOR` nicht auf ihnen, sondern wird vom Anwender festgelegt.
- **K-6** Über `KENNLINIE_VOLUMENSTROM` darf der aktuelle Volumenstrom **rein anzeigend** ermittelt und ausgegeben werden (siehe `VOLUMENSTROM_IST`). Er darf keinerlei Rückwirkung auf die Ansteuerung haben.
- **K-7** Die Volumenstromkennlinie ordnet **Drehzahl einem Volumenstrom** zu und besteht aus vier Punkten:
  dem **impliziten Nullpunkt (0, 0)**, zwei **frei platzierbaren Zwischenpunkten** und dem
  **Endpunkt (maximale Drehzahl, zugehöriger Volumenstrom)**. Zu konfigurieren sind damit drei
  Wertepaare je Richtung; die Zwischenpunkte müssen zwischen 0 und der maximalen Drehzahl liegen und
  aufsteigend sortiert sein.
- **K-8** Zwischen den Punkten wird **linear interpoliert**. Über die frei platzierbaren Zwischenpunkte
  lassen sich **nichtlineare** Verläufe hinreichend genau abbilden – man legt sie dorthin, wo die
  Kennlinie am stärksten gekrümmt ist.
- **K-9** **Keine Extrapolation:** Wird eine Drehzahl **oberhalb des Endpunkts** gemessen, gilt dessen
  Volumenstromwert. Der Wert wird also **begrenzt, nicht fortgeschrieben**. Dieser Fall ist normal und
  **kein Fehler**: Reale Geräte drehen fertigungs- und betriebsbedingt geringfügig schneller, als das
  Datenblatt angibt. Die Begrenzung sorgt lediglich dafür, dass die Anzeige an der Bereichsgrenze
  **stabil** bleibt und nicht über den plausiblen Wert hinausläuft. Eine Meldung erfolgt nicht.
- **K-10** Ist die Kennlinie nicht hinterlegt, entfällt `VOLUMENSTROM_IST` (siehe A-2). Es wird **kein**
  Ersatzwert und keine Annahme eines linearen Verlaufs gebildet.

---

## 3. Eingänge

Werte und Flags, die ein Knoten **entgegennimmt** – vom Master, von der übergeordneten Schnittstelle
oder von der lokalen Bedienung.

| Eingang | Gilt für | Typ | Bedeutung |
|---|---|---|---|
| `FREIGABE` | beide | bool | Sicherheitsfreigabe. 0 = gesperrt (Leistung 0, unabhängig von allem anderen), 1 = freigegeben. Wird typischerweise von einem Gerät **außerhalb** der Lüftersteuerung gesetzt (z. B. Druckwächter einer Feuerstätte) |
| `LEISTUNG_SOLL` | beide | 0–100 % | Leistungsvorgabe. Beim Slave die Gruppenvorgabe, die intern mit `ANTEILSFAKTOR` multipliziert wird |
| `RICHTUNGSART` | beide | Enum | `REVERSIEREND` / `NUR_A` / `NUR_B`, siehe Abschnitt 5. Nur bei `REVERSIBEL = ja` |
| `TAKT_ZUSTAND` | **nur Slave** | 0 / 1 | Aktive Halbwelle, vom Master für die ganze Gruppe vorgegeben. Nur bei `REVERSIBEL = ja` |
| `STOSSLUEFTUNG` | beide | Trigger | Einmaliger Anstoß (kein Zustand). Überschreibt `LEISTUNG_SOLL` für `STOSSLUEFTUNG_DAUER` mit `STOSSLUEFTUNG_LEISTUNG`, danach selbsttätiger Rückfall |
| `QUITTIERUNG` | beide | Trigger | Setzt einen gespeicherten Fehler zurück |

**Regeln**

- **E-1** `FREIGABE = 0` hat Vorrang vor allen anderen Eingängen und vor jeder lokalen Bedienung.
- **E-1a** `FREIGABE` muss von einem **externen Gerät außerhalb der Lüftersteuerung** gesetzt werden können
  – etwa vom Druckwächter einer Feuerstätte. Weder Master, Slave noch übergeordnete Schnittstelle
  dürfen eine anliegende Sperre überstimmen.
- **E-1b** Es gibt **zwei voneinander unabhängige Sperrauslöser**, die beide zum Stopp führen:
  1. **Explizites `FREIGABE = 0`** – wirkt **sofort und unverzüglich**. Es wird **nicht** auf den Ablauf
     von `FREIGABE_UEBERWACHUNGSZEIT` gewartet, nicht entprellt und nicht verzögert.
  2. **Ausbleiben der zyklischen Aktualisierung** von `FREIGABE = 1` länger als
     `FREIGABE_UEBERWACHUNGSZEIT` – dann gilt ebenfalls gesperrt (Ruhestromprinzip).
  Ein fehlendes Signal bedeutet **niemals** „freigegeben".
- **E-1c** Der Stopp erfolgt in beiden Fällen unverzüglich. Weder eine laufende `TOTZEIT` noch eine
  aktive Stoßlüftung noch eine anstehende Taktumschaltung dürfen ihn verzögern.
- **E-1d** Die Sperre ist **selbsthaltend**. Sie kann **ausschließlich durch ein explizites
  `FREIGABE = 1`** aufgehoben werden. Insbesondere heben sie **nicht** auf: Zeitablauf, Wiederkehr der
  Kommunikation, ein Neustart, ein Spannungsausfall, `QUITTIERUNG` oder eine Bedienung vor Ort.
- **E-1e** Der Freigabezustand ist **persistent zu speichern** und nach Neustart oder Spannungsausfall
  **mit dem zuletzt gültigen Wert wieder anzufahren**. Eine anliegende Sperre übersteht damit den
  Spannungsausfall und lässt sich **nicht durch Aus- und Einschalten umgehen**.
- **E-1f** War zuletzt `FREIGABE = 1` gespeichert, läuft der Knoten wieder an. Die Überwachung nach
  E-1b greift dabei **sofort**: Bleibt die zyklische Aktualisierung aus, sperrt der Knoten nach Ablauf
  von `FREIGABE_UEBERWACHUNGSZEIT` erneut.
- **E-1g** Das Ansprechen der Sperre – sowohl durch explizites `FREIGABE = 0` als auch durch Ablauf der
  Überwachungszeit – ist als Ereignis **zu melden und zu speichern**.
- **E-1h** *Hinweis:* Für einen sicherheitsrelevanten Verbund mit einer Feuerstätte ist üblicherweise
  ein **fest verdrahteter, potentialfreier Kontakt** gefordert. Der Busweg nach E-1b ist dann als
  Ergänzung zu verstehen, nicht als Ersatz. Die Festlegung trifft die zuständige Stelle
  (z. B. Bezirksschornsteinfeger).
- **E-2** Ungültige Werte werden **abgewiesen und gemeldet**, nicht stillschweigend begrenzt.
- **E-3** Der resultierende Stellwert je Knoten wird auf 0–100 % begrenzt.
- **E-4** Ein Richtungswechsel erfolgt nur nach Ablauf der `TOTZEIT`.
- **E-5** Der Knoten leitet seine Richtung **selbst** aus `PHASE` und `TAKT_ZUSTAND` ab. Eine direkte
  Richtungsvorgabe von außen gibt es nicht (Ausnahme: Modi `NUR_A` / `NUR_B`).
- **E-6** `TAKT_ZUSTAND` wird als **Zustand** übertragen, nicht als Impuls. Ein verspätet zugeschalteter
  oder nach einer Störung zurückkehrender Knoten übernimmt damit sofort die richtige Halbwelle.

---

## 4. Ausgänge

Werte und Flags, die ein Knoten **bereitstellt**.

| Ausgang | Gilt für | Typ | Bedeutung |
|---|---|---|---|
| `LEISTUNG_IST` | beide | 0–100 % | Tatsächlich kommandierter Wert nach Anteilsfaktor und Begrenzung |
| `DREHZAHL_IST` | beide | Zahl | Gemessene Drehzahl. **Nur wenn** `HAT_RUECKMELDUNG = ja`. Bleibt das Signal trotz Ansteuerung aus, wird 0 ausgegeben – kein geschätzter Ersatzwert |
| `VOLUMENSTROM_IST` | beide | m³/h, **vorzeichenbehaftet** | Aktuell geförderter Volumenstrom, aus `DREHZAHL_IST` über `KENNLINIE_VOLUMENSTROM` berechnet. **Positiv = Zuluft** (in den Raum), **negativ = Abluft**. **Nur wenn** `HAT_RUECKMELDUNG = ja` **und** Kennlinie hinterlegt |
| `RICHTUNG_IST` | beide | A / B | Nur bei `REVERSIBEL = ja` |
| `ZYKLUS_REST` | **nur Master** | Zeit | Verbleibende Zeit bis zum nächsten Richtungswechsel |
| `LAEUFT` | beide | bool | 1 = Knoten ist in Betrieb |
| `IST_DEAKTIVIERT` | beide | bool | 1 = Knoten ist stillgelegt |
| `STOERUNG` | beide | bool | 1 = Fehler liegt an |
| `FEHLERCODE` | beide | Enum | Art des Fehlers |
| `BETRIEBSSTUNDEN` | beide | Zahl | Zähler |
| `MASTER_ERREICHBAR` | **nur Slave** | bool | Nur beim Slave: Zustand der Verbindung zum Master |

**Regeln**

- **A-1** `LEISTUNG_IST` und `DREHZAHL_IST` sind getrennt verfügbar. Die Aussage „sollte drehen, dreht aber nicht" ergibt sich aus der Kombination beider Werte und wird nicht durch Ersatzwerte verschleiert.
- **A-2** Ist eine Funktion wegen der Konfiguration nicht vorhanden, wird sie als **„nicht verfügbar"** gekennzeichnet – nicht als „in Ordnung".
- **A-3** `VOLUMENSTROM_IST` folgt unmittelbar der gemessenen Drehzahl. Es findet **keine Sonderbehandlung** für Totzeit, Anlauf oder Richtungswechsel statt: Steht der Ventilator, ist die Drehzahl 0 und damit auch der Volumenstrom 0. Der Wert ist eine Messung, keine Schätzung.
- **A-4** Das **Vorzeichen** von `VOLUMENSTROM_IST` ergibt sich aus der aktuellen Förderrichtung und wird durch
  `VOLUMENSTROM_INVERTIEREN` an die Konvention angepasst. Bei nicht reversiblen Knoten legt das Flag das
  Vorzeichen dauerhaft fest. Damit lassen sich die Werte mehrerer Knoten **aufsummieren**: eine Summe nahe 0
  bedeutet eine ausgeglichene Bilanz. Die Steuerung selbst zieht daraus **keine Schlüsse** und regelt nicht danach.

---

## 5. Richtungsart und Leistung

Der Knoten kennt **keine Betriebsarten-Enumeration**. Was er ausführt, ergibt sich aus drei Größen:
der `RICHTUNGSART`, dem Wert `LEISTUNG_SOLL` und einem etwaigen Stoßlüftungs-Anstoß.

### 5.1 `RICHTUNGSART`

| Wert | Verhalten |
|---|---|
| `REVERSIEREND` | Die Richtung folgt `PHASE` und `TAKT_ZUSTAND` |
| `NUR_A` | Dauerhafte Förderung in Richtung A, kein Taktwechsel |
| `NUR_B` | Dauerhafte Förderung in Richtung B, kein Taktwechsel |

### 5.2 Leistung

- **M-1** `LEISTUNG_SOLL` ist die **einzige** Leistungsvorgabe. Für den Knoten ist **nicht unterscheidbar**,
  ob der Wert von Hand, aus einem Zeitplan oder aus einer Bedarfsregelung stammt.
- **M-2** `LEISTUNG_SOLL = 0` bedeutet Stillstand. Ein eigener Modus „Aus" ist damit entbehrlich;
  die sicherheitsgerichtete Abschaltung erfolgt über `FREIGABE` (E-1).
- **M-3** `DEAKTIVIERT = ja` legt den Knoten **still**: Leistung 0, keine Teilnahme am Gruppenbetrieb.
  Die Stilllegung ist **administrativ**, nicht betrieblich – gedacht für Service, Fehlersuche oder einen
  nicht bestückten Kanal.
- **M-4** Ein stillgelegter Knoten meldet **keine Störung** wegen Nichtlaufens; laufabhängige Überwachungen
  (z. B. Blockiererkennung) sind ausgesetzt und als **„nicht verfügbar"** gekennzeichnet. Er bleibt über die
  Schnittstelle **lesbar und parametrierbar**.
- **M-5** Die Stilllegung bleibt über Neustart und Spannungsausfall **erhalten** und wird vom Master
  **nicht überschrieben**.
- **M-6** Die drei Wege zum Stillstand sind zu unterscheiden:

  | Zustand | Ausgelöst durch | Fehlerauswertung | Bleibt nach Neustart |
  |---|---|---|---|
  | `FREIGABE = 0` | externes Sicherheitssignal (E-1a) | aktiv, Ereignis wird gemeldet | nein |
  | `LEISTUNG_SOLL = 0` | normale Vorgabe im Betrieb | aktiv | nein |
  | `DEAKTIVIERT = ja` | administrative Stilllegung | **ausgesetzt** | **ja** |

- **M-7** `RICHTUNGSART` und `LEISTUNG_SOLL` sind **unabhängig** voneinander und frei kombinierbar.
  Beispiel: bedarfsgeführte Leistung bei dauerhafter Absaugung = beliebiger Leistungswert + `NUR_B`.
- **M-8** `RICHTUNGSART` existiert nur bei `REVERSIBEL = ja`. Bei nicht reversiblen Knoten entfällt der Eingang.
- **M-9** Bei `NUR_A` / `NUR_B` ist `TAKT_ZUSTAND` wirkungslos; die Richtungsart überschreibt die Phasenzuordnung.

### 5.3 Stoßlüftung

- **M-10** `STOSSLUEFTUNG` ist ein **einmaliger Anstoß, kein Zustand**. Sie wird bewusst vom Anwender ausgelöst.
- **M-11** Nach dem Anstoß fährt der Knoten für `STOSSLUEFTUNG_DAUER` die `STOSSLUEFTUNG_LEISTUNG` und
  **überschreibt dabei `LEISTUNG_SOLL`**. Danach gilt wieder der anliegende `LEISTUNG_SOLL` – ohne dass
  ein Rückschaltbefehl nötig wäre.
- **M-12** `RICHTUNGSART` bleibt während der Stoßlüftung unverändert.
- **M-13** Ein erneuter Anstoß während einer laufenden Stoßlüftung **startet die Dauer neu**.
- **M-14** `FREIGABE = 0` beendet die Stoßlüftung sofort.
- **M-15** Der sichere Zustand bei Reset, Neustart und Störung ist Leistung 0
  (bei reversiblen Knoten: Mittelstellung der Stellgröße).

---

## 6. Master

- **MA-1** Genau **ein Knoten je Gruppe** wird per Konfiguration (`IST_MASTER`) als Master festgelegt, alle übrigen sind Slaves. Keine Aushandlung zur Laufzeit.
- **MA-2** Der Master erzeugt die Vorgaben der Gruppe und verteilt sie an die Slaves über den Kommunikationskanal: `RICHTUNGSART`, `LEISTUNG_SOLL`, `TAKT_ZUSTAND`.
- **MA-3** Der Master **darf** `LEISTUNG_SOLL` aus einer Bedarfsgröße bilden (z. B. CO₂, relative Feuchte).
  Für die Slaves ist das nicht erkennbar – sie erhalten in jedem Fall nur einen Leistungswert.
- **MA-4** Der Master ist zugleich die **Zeitreferenz** der Gruppe und bestimmt den Zeitpunkt des Richtungswechsels.
  Nur er kennt `ZYKLUSZEIT` und gibt `ZYKLUS_REST` aus; Slaves folgen ausschließlich `TAKT_ZUSTAND`.
- **MA-5** **Lokale Parameter bleiben lokal.** Der Master überschreibt `REVERSIBEL`, `HAT_RUECKMELDUNG`, `PHASE`, `ANTEILSFAKTOR` und die Kennlinien nicht – sie beschreiben die Hardware des Slaves.
- **MA-6** Ein Slave berechnet seinen Stellwert selbst: Gruppenvorgabe × eigener `ANTEILSFAKTOR`, umgerechnet über die eigene `KENNLINIE_STELLGROESSE`.
- **MA-7** Bei Ausfall des Masters gehen die Slaves in einen konfigurierbaren Ersatzzustand. Ein selbsttätiger Rollenwechsel findet **nicht** statt.
- **MA-8** Slaves bleiben ohne Master lokal bedienbar und im Grundbetrieb funktionsfähig.
