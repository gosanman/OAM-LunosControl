#include <Arduino.h>
#include <knx.h>

#include "KwlFanModule.h"
#include "KwlRoomModule.h"
#include "OpenKNX.h"

void setup()
{
    // Bei Firmwareaenderungen, die keine neue knxprod brauchen, wird diese Zahl
    // erhoeht.
    const uint8_t firmwareRevision = 0;
    openknx.init(firmwareRevision);

    // Die Reihenfolge bestimmt die Flash-Aufteilung der Module. Sie wird nicht
    // mehr geaendert, sobald das erste Geraet Werte gespeichert hat.
    //
    // Der Raum steht VOR dem Luefter: sein setup() muss gelaufen sein, bevor das
    // Luefter-Modul die Stufenwuensche abholt.
    openknx.addModule(1, openknxKwlRoomModule);
    openknx.addModule(2, openknxKwlFanModule);

    openknx.setup();
}

void loop()
{
    openknx.loop();
}

#ifdef OPENKNX_DUALCORE
void setup1()
{
    openknx.setup1();
}

void loop1()
{
    openknx.loop1();
}
#endif
