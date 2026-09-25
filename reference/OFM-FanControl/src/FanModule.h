#pragma once

#include "OpenKNX.h"
#include "FanChannel.h"
#include "IFanHardware.h"
#include "hardware.h"
#include "knxprod.h"

/**
 * @brief OpenKNX-Modul der Luftersteuerung. Haelt die Knoten des Geraetes.
 *
 * Core 0 fuehrt die Kanallogik, Core 1 misst die Drehzahl. Die persistenten Werte
 * (Freigabe-Latch, Suspendierung, Betriebssekunden) liegen im Modul-Flashbereich.
 */
class FanModule : public OpenKNX::Module
{
  public:
    void setup(bool configured) override;
    void loop(bool configured) override;
    void processInputKo(GroupObject &ko) override;

#ifdef OPENKNX_DUALCORE
    void setup1(bool configured) override;
    void loop1(bool configured) override;
#endif

    void processAfterStartupDelay() override;
    void processBeforeRestart() override;

    uint16_t flashSize() override;
    void writeFlash() override;
    void readFlash(const uint8_t *data, const uint16_t size) override;

    /**
     * @brief Eine Luefteransteuerung anmelden. Wird ausschliesslich aus FAN_INIT() aufgerufen.
     *
     * Die Reihenfolge der Aufrufe ist die Reihenfolge der ETS-Kanaele. Das Modul uebernimmt das
     * Objekt und gibt es nie frei - es lebt so lange wie das Geraet.
     */
    void addHardware(IFanHardware *hardware);

    void showHelp() override;
    bool processCommand(const std::string cmd, bool debugKo) override;

    const std::string name() override { return "FanControl"; }
    const std::string version() override { return MODULE_FanControl_Version; }

  private:
    static constexpr uint8_t FlashVersion = 1;

    // Dimensioniert nach den Ausgaengen des Boards, nicht nach FAN_ChannelCount: die ETS darf
    // mehr Luefter anbieten, als das Board treiben kann, und diese Felder sind physisch.
    //
    // Zeiger auf die Schnittstelle, nicht konkrete Objekte: welches Ansteuerverfahren ein
    // Ausgang benutzt, weiss nur der Geraete-Header, und er legt die Objekte in FAN_INIT() an.
    // FAN_BOARD_CHANNELS bleibt eine Compile-Zeit-Zahl, weil flashSize() damit rechnet und vom
    // Framework vor setup() gerufen wird.
    IFanHardware *_hw[FAN_BOARD_CHANNELS] = {};
    uint8_t _hwCount = 0;
    FanChannel *_channel[FAN_BOARD_CHANNELS] = {};

    volatile bool _setupComplete = false;
    bool _startupDelayDone = false;
};

extern FanModule openknxFanModule;
