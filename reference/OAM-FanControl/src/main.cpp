#include <knx.h>
#include <Arduino.h>
#include "OpenKNX.h"
#include "FanModule.h"
#include "Logic.h"
#include "FileTransferModule.h"

void setup()
{
	//Bei Firmwareänderungen, die keine neue knxprod benötigen, kann die Revision erhöht werden.
	const uint8_t firmwareRevision = 0;
    openknx.init(firmwareRevision);
    // Reihenfolge bestimmt die Flash-Aufteilung der Module. FanControl bleibt deshalb auf 1,
    // damit die bereits gespeicherten Werte (Freigabe-Latch, Betriebsstunden) liegen bleiben.
    openknx.addModule(1, openknxFanModule);
    openknx.addModule(2, openknxLogic);
    // Id 9 ist die OpenKNX-Konvention fuer den Dateitransfer. Das Modul braucht keine
    // ETS-Einbindung, aber ein Dateisystem - siehe RP2040_16MB in platformio.custom.ini.
    openknx.addModule(9, openknxFileTransferModule);
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