### Überwachungszeit Freigabe

Maximales Alter des Freigabesignals.

Bleibt die zyklische Aktualisierung der Freigabe länger als diese Zeit aus, gilt der Knoten als
gesperrt und fährt auf Leistung 0. Ein fehlendes Signal bedeutet niemals „freigegeben".

Die Zeit muss deutlich über dem Sendezyklus des freigebenden Gerätes liegen. **0 schaltet die
Überwachung ab**; das explizite Sperren wirkt weiterhin.

Die Sperre ist selbsthaltend und übersteht einen Spannungsausfall. Sie wird ausschließlich durch
ein empfangenes Freigabe-Telegramm aufgehoben — nicht durch Zeitablauf, nicht durch eine
Quittierung und nicht durch Aus- und Einschalten.

Die Überwachung beginnt mit dem ersten empfangenen Freigabe-Telegramm. Eine Anlage, die das
Objekt nicht verknüpft, läuft also normal. Sobald das Objekt aber **einmal** gesendet hat, bleibt
die Überwachung dauerhaft aktiv und läuft nach einem Neustart sofort wieder an — auch ohne neues
Telegramm.

> **Hinweis:** KNX ist kein Sicherheitsbus. Für den Verbund mit einer Feuerstätte ist
> üblicherweise ein fest verdrahteter, potentialfreier Kontakt gefordert. Der Busweg ist dann
> eine Ergänzung, kein Ersatz. Die Festlegung trifft die zuständige Stelle.

