### Richtungsart

Legt fest, wie die Gruppe fördert. Nur der Master stellt das ein; die Slaves übernehmen es.

* **Automatisch reversieren** — die Gruppe wechselt im Takt die Förderrichtung. Nur in diesem
  Fall gibt es eine Zykluszeit.
* **Nur Richtung A** / **Nur Richtung B** — dauerhafte Förderung in eine Richtung, kein
  Taktwechsel. Die Zuordnung Phase/Gegenphase ist dann wirkungslos.
* **Über Kommunikationsobjekt** — die Richtungsart kommt zur Laufzeit von außen.

Bei fester Vorgabe sendet der Master die Richtungsart zyklisch an die Gruppe, sodass auch ein
später hinzukommender Knoten sie erhält.

