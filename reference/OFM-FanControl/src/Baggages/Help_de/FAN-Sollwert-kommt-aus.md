### Sollwert kommt aus

Bestimmt, woraus der Master die Leistungsvorgabe für die Gruppe bildet.

* **Fester Wert** — die Anlage läuft dauerhaft mit einer eingestellten Leistung. Es wird kein
  Kommunikationsobjekt für die Leistung benötigt.
* **Externes Kommunikationsobjekt** — die Vorgabe kommt von außen, etwa aus einer
  Visualisierung, einer Zeitschaltuhr oder einem Logikbaustein.
* **Interne Regelung** — der Master regelt selbst auf einen Messwert, zum Beispiel CO₂ oder
  relative Luftfeuchte. Die Leistung steigt proportional an.
* **Zweipunkt mit Hysterese** — der Master schaltet an zwei Schwellen zwischen zwei festen
  Leistungen um, ohne Zwischenwerte.

Unabhängig von der Quelle multipliziert jeder Knoten die Gruppenvorgabe mit seinem eigenen
Anteilsfaktor.
