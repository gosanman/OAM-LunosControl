### Kanalaktivität

Schaltet diesen Lüfterkanal ein oder aus. Steht **ausschließlich in der Kanalauswahl** — auf dem
Lüfter-Reiter erscheint das Feld bewusst nicht noch einmal.

* **Deaktiviert** — der Kanal wird nicht benutzt. Es gibt keinen Lüfter-Reiter und keine
  Kommunikationsobjekte; die Beschreibung bleibt trotzdem editierbar.
* **Aktiviert** — der Kanal wird benutzt und erhält seinen eigenen Reiter.

Die Kanäle sind unabhängig voneinander: es ist zulässig, nur Lüfter 2 zu aktivieren und Lüfter 1
ungenutzt zu lassen.

Das ist die dauerhafte Festlegung „ist der Kanal bestückt". Um einen **vorhandenen** Kanal
vorübergehend ruhen zu lassen — für Service oder Fehlersuche — dient „Suspendiert" auf dem
Lüfter-Reiter; dabei bleiben Objekte und Gruppenadressen erhalten.

> **Mehr Kanäle als Ausgänge:** Die Applikation bietet 8 Kanäle an, die Zahl der tatsächlichen
> Ausgänge bestimmt die Hardware. Werden mehr Kanäle aktiviert, als das Gerät treiben kann,
> laufen die überzähligen nicht — das Gerät meldet beim Start einen Konfigurationsfehler.
