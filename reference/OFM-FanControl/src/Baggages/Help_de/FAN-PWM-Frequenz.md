### PWM-Frequenz

Trägerfrequenz des Ansteuersignals, gültig für **alle Kanäle des Gerätes**.

Sie lässt sich nicht je Kanal einstellen, weil die PWM-Frequenz auf dem RP2040 eine Eigenschaft
des gesamten Ausgabepfades ist.

Der passende Wert steht im Datenblatt des Lüfters. Die **Vorgabe ist 1 kHz**, einstellbar
zwischen 500 Hz und 20 kHz. Übliche Bereiche liegen zwischen 1 und 5 kHz; manche Geräte
vertragen bis 10 kHz und mehr. Eine zu hohe Frequenz kann dazu führen, dass der Lüfter das
Signal nicht mehr sauber auswertet, eine zu niedrige kann hörbar werden.

