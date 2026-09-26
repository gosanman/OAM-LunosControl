#include "FileTransferModule.h"
#include "KwlFanModule.h"
#include "KwlRoomModule.h"
#include "Logic.h"
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
    openknx.addModule(3, openknxLogic);
    // Id 9 ist die OpenKNX-Konvention fuer den Dateitransfer. Das Modul braucht keine
    // ETS-Einbindung, aber ein Dateisystem - siehe RP2040_16MB in platformio.custom.ini.
    openknx.addModule(9, openknxFileTransferModule);

    // OFM-ConfigTransfer wird hier NICHT angemeldet: es hat keinen Firmware-Teil.
    // Das Modul besteht aus ETS-XML und einem Skript und ist ueber den
    // op:define prefix="UCT" in src/Kwl.xml eingebunden.

    openknx.setup();
}

void loop()
{
    openknx.loop();
}

// Kein setup1()/loop1(): OPENKNX_DUALCORE ist in platformio.custom.ini bewusst
// nicht gesetzt - es gibt keinen Tachoeingang und keine zweite Aufgabe, die einen
// eigenen Kern braucht.
