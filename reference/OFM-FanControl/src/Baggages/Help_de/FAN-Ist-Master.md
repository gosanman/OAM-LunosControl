### Ist Master

Genau ein Knoten je Gruppe wird als Master konfiguriert, alle übrigen als Slave.

Der Master erzeugt die Vorgaben der Gruppe und verteilt Leistung, Richtungsart und Taktzustand
über Gruppenadressen. Er ist zugleich die Zeitreferenz und bestimmt den Zeitpunkt des
Richtungswechsels. Zusätzlich sendet er zyklisch ein Lebenszeichen, an dem die Slaves seinen
Ausfall erkennen.

Ein Rollenwechsel zur Laufzeit findet nicht statt.

