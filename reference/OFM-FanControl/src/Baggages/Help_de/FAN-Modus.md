### Modus

Legt fest, wie dieser Lüfter gefahren wird.

* **Nicht reversibel** — eine Förderrichtung. Es gibt keine Richtungs- und Taktobjekte, und die
  Stellgröße wird gewöhnlich gefahren: 0 % ist Stillstand, 100 % volle Leistung.
* **Reversibel** — zwei Förderrichtungen auf einem Ausgang. Richtung und Takt sind verfügbar,
  Stellgröße Minimum und Maximum werden je Richtung eingestellt, und die Mittelstellung ist der
  Stillstand.

Der Modus gehört zum einzelnen Lüfter und steht deshalb hier, nicht in der Kanalauswahl. Ob der
Kanal überhaupt benutzt wird, legt „Kanalaktivität" in der Kanalauswahl fest.

Ob die Hardware eine zweite Förderrichtung unterstützt, hängt vom Board und von der eingestellten
Mittelstellung ab; passt beides nicht zusammen, meldet das Gerät einen Konfigurationsfehler.
