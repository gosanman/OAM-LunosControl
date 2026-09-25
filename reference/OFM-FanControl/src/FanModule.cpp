#include "FanModule.h"

// Die konkreten Ansteuerverfahren. Eingebunden werden sie hier, damit FAN_INIT() aus dem
// Geraete-Header sie benutzen kann, ohne dass ein Hardware-Header Modulheader einbinden muss -
// ein Makro wird erst an der Aufrufstelle aufgeloest.
#include "FanHardware/PwmFan.h"
#include "FanHardware/DShotFan.h"

FanModule openknxFanModule;

#ifndef FAN_INIT
    #error "Das Geraete-Header muss FAN_BOARD_CHANNELS und FAN_INIT() definieren."
#endif

namespace
{
    // Wie viele Ausgaenge dieses Board hat. Eine Board-Eigenschaft, keine der Applikation:
    // die ETS bietet FAN_ChannelCount Kanaele an, Pins hat nur ein Teil davon.
    constexpr uint8_t BoardChannels = FAN_BOARD_CHANNELS;
}

// ===========================================================================
// Setup
// ===========================================================================

void FanModule::addHardware(IFanHardware *hardware)
{
    if (hardware == nullptr) return;

    // Mehr Ansteuerungen als deklarierte Ausgaenge waere ein Fehler im Geraete-Header. Das
    // ueberzaehlige Objekt wird verworfen statt ueber das Array hinaus geschrieben - der
    // Vergleich in setup() meldet die Abweichung.
    if (_hwCount >= BoardChannels)
    {
        logErrorP("FAN_INIT() meldet mehr als die deklarierten %u Ausgaenge an",
                  (unsigned)BoardChannels);
        delete hardware;
        return;
    }

    _hw[_hwCount++] = hardware;
}

void FanModule::setup(bool configured)
{
    // Das Board legt seine Ansteuerungen selbst an - nur es weiss, ob ein Ausgang ein
    // Tastverhaeltnis ausgibt oder ein Protokoll spricht, und mit welchen Pins. Dasselbe Muster
    // benutzt OGM-Common mit LED_INIT(); das Modul kennt danach nur noch IFanHardware.
    FAN_INIT();

    if (_hwCount != BoardChannels)
        logErrorP("FAN_INIT() hat %u Ansteuerungen angemeldet, das Board deklariert %u",
                  (unsigned)_hwCount, (unsigned)BoardChannels);

    // Auch ungenutzte Ausgaenge scharf schalten, damit sie definiert in der Mittelstellung
    // stehen und nicht undefiniert floaten.
    for (uint8_t i = 0; i < _hwCount; i++)
        _hw[i]->begin();

    if (!configured)
    {
        _setupComplete = true;
        return;
    }

    // Die ETS bietet FAN_ChannelCount Kanaele an - das ist eine Eigenschaft der Applikation.
    // Wie viele davon Pins haben, weiss nur das Board. Aktivierte Kanaele jenseits davon
    // werden gemeldet, statt stumm nichts zu tun. Der Kanalindex steckt hier nicht in einem
    // FanChannel, deshalb wird die Adresse von Hand gerechnet statt ParamFAN_fActive benutzt.
    for (uint8_t i = BoardChannels; i < FAN_ChannelCount; i++)
    {
        const uint16_t addr = FAN_ParamBlockOffset + i * FAN_ParamBlockSize + FAN_fActive;
        if (knx.paramByte(addr) & FAN_fActiveMask)
        {
            logErrorP("Luefter %u ist aktiviert, das Board hat aber nur %u Ausgaenge",
                      (unsigned)(i + 1), (unsigned)BoardChannels);
        }
    }

    for (uint8_t i = 0; i < BoardChannels; i++)
    {
        FanChannel *channel = new FanChannel(i, *_hw[i]);
        channel->setup();

        if (!channel->isActive())
        {
            delete channel;
            continue;
        }

        _channel[i] = channel;
    }

    _setupComplete = true;
}

#ifdef OPENKNX_DUALCORE
void FanModule::setup1(bool configured)
{
    // Auf das Setup von Core 0 warten, damit die Kanalkonfiguration steht.
    while (!_setupComplete)
        delay(1);

    if (!configured) return;

    // Der Tacho gehoert dem Kanal, nicht dem Modul: das Modul sagt nur, wann Core 1 dran ist.
    for (uint8_t i = 0; i < BoardChannels; i++)
        if (_channel[i] != nullptr) _channel[i]->setup1();
}

void FanModule::loop1(bool configured)
{
    if (!configured) return;

    for (uint8_t i = 0; i < BoardChannels; i++)
        if (_channel[i] != nullptr) _channel[i]->loop1();
}
#endif

// ===========================================================================
// Ablauf
// ===========================================================================

void FanModule::processAfterStartupDelay()
{
    // Erst ab hier darf ein Luefter anlaufen. Empfangene Werte werden vorher bereits
    // entgegengenommen, sodass der Start sofort mit aktuellen Vorgaben erfolgt.
    _startupDelayDone = true;
}

void FanModule::loop(bool configured)
{
    if (!configured) return;
    if (!_startupDelayDone) return;

    for (uint8_t i = 0; i < BoardChannels; i++)
    {
        FanChannel *channel = _channel[i];
        if (channel == nullptr) continue;

        channel->loop();

        // Freigabe-Latch und Suspendierung muessen einen Spannungsausfall ueberleben,
        // deshalb bei Aenderung sofort sichern und nicht auf das periodische Speichern warten.
        if (channel->consumePersistDirty())
            openknx.flash.save(true);
    }
}

void FanModule::processInputKo(GroupObject &ko)
{
    const int8_t index = FAN_KoCalcChannel(ko.asap());
    if (index < 0 || index >= BoardChannels) return;

    FanChannel *channel = _channel[index];
    if (channel != nullptr) channel->processInputKo(ko);
}

void FanModule::processBeforeRestart()
{
    writeFlash();
}

// ===========================================================================
// Persistenz
// ===========================================================================

uint16_t FanModule::flashSize()
{
    // Versionsbyte plus je Kanal ein Flagbyte und die Betriebssekunden.
    return 1 + BoardChannels * 5;
}

void FanModule::writeFlash()
{
    openknx.flash.writeByte(FlashVersion);

    for (uint8_t i = 0; i < BoardChannels; i++)
    {
        FanChannel::PersistentState state = {0x00, 0};
        if (_channel[i] != nullptr) state = _channel[i]->persistentState();

        openknx.flash.writeByte(state.flags);
        openknx.flash.writeInt(state.runSeconds);
    }
}

void FanModule::readFlash(const uint8_t *data, const uint16_t size)
{
    if (size < flashSize()) return; // noch nichts oder ein aelteres Layout gespeichert

    if (openknx.flash.readByte() != FlashVersion)
    {
        logInfoP("Gespeicherte Daten haben eine andere Version, werden verworfen");
        return;
    }

    for (uint8_t i = 0; i < BoardChannels; i++)
    {
        FanChannel::PersistentState state;
        state.flags = openknx.flash.readByte();
        state.runSeconds = openknx.flash.readInt();

        if (_channel[i] != nullptr) _channel[i]->restore(state);
    }
}

// ===========================================================================
// Konsole
// ===========================================================================

void FanModule::showHelp()
{
    openknx.console.printHelpLine("fan", "Luefterkanaele anzeigen und zum Test ansteuern");
}

bool FanModule::processCommand(const std::string cmd, bool debugKo)
{
    (void)debugKo; // Diagnose-KO wird von diesem Modul nicht bedient

    if (cmd.rfind("fan", 0) != 0) return false;

    if (cmd == "fan")
    {
        openknx.console.printHelpLine("fan st", "Alle Kanaele je eine Zeile");
        openknx.console.printHelpLine("fan cNN", "Kanal NN ausfuehrlich, z.B. fan c01");
        openknx.console.printHelpLine("fan cNN pXX", "Test: Leistung XX Prozent, z.B. fan c01 p60");
        openknx.console.printHelpLine("fan cNN a", "Test: Foerderrichtung A");
        openknx.console.printHelpLine("fan cNN b", "Test: Foerderrichtung B");
        openknx.console.printHelpLine("fan cNN auto", "Test beenden, nur Kanal NN");
        openknx.console.printHelpLine("fan auto", "Test beenden, alle Kanaele");
        openknx.console.printHelpLine("fan led", "Zuordnung der Status-LEDs pruefen");
        logInfoP("Ein Test verfaellt von selbst nach %u min.", (unsigned)(Fan::ConsoleOverrideMs / 60000));
        return true;
    }

    if (cmd == "fan st")
    {
        logInfoP("%u Kanal/Kanaele auf diesem Board:", (unsigned)BoardChannels);
        logIndentUp();
        for (uint8_t i = 0; i < BoardChannels; i++)
        {
            // Ein nicht aktivierter Kanal wird im Setup gar nicht angelegt. Das hier ausdruecklich
            // zu melden ist wichtiger als es zu ueberspringen: sonst sieht eine fehlende
            // Kanalaktivierung in der ETS wie ein defektes Modul aus.
            if (_channel[i] == nullptr)
                logInfoP("Ch%02u in der ETS nicht aktiviert", (unsigned)(i + 1));
            else
                _channel[i]->printStatusLine();
        }
        logIndentDown();
        return true;
    }

    if (cmd == "fan led")
    {
        // Ob eine Status-LED leuchtet, entscheidet nicht dieses Modul, sondern die Zuordnung in
        // OGM-Common. Ohne diese Ausgabe sieht eine fehlende Zuordnung wie ein toter LED-Code aus.
        logInfoP("Standardbelegung (BASE): %s",
                 openknx.ledFunctions.useDefaultFunction() ? "aktiv" : "abgewaehlt, es gelten die ETS-Dropdowns");
        logIndentUp();
        for (uint8_t i = 0; i < BoardChannels; i++)
        {
            const uint32_t id = Fan::LedFunctionBase + i;
            OpenKNX::Led::FunctionGroup *group = openknx.ledFunctions.get(id);
            logInfoP("Luefter %u -> Funktions-ID %u: %s", (unsigned)(i + 1), (unsigned)id,
                     (group != nullptr && group->active()) ? "LED zugewiesen" : "KEINE LED zugewiesen");
        }
        logIndentDown();
        return true;
    }

    if (cmd == "fan auto")
    {
        for (uint8_t i = 0; i < BoardChannels; i++)
            if (_channel[i] != nullptr) _channel[i]->releaseOverride();
        logInfoP("Alle Testuebersteuerungen beendet, zurueck auf Regelbetrieb");
        return true;
    }

    // Ab hier nur noch kanalbezogene Befehle: "fan cNN [rest]"
    if (cmd.length() < 6 || cmd[4] != 'c') return false;

    const int channelNumber = atoi(cmd.substr(5, 2).c_str());
    if (channelNumber < 1 || channelNumber > BoardChannels)
    {
        logInfoP("Kanal %d gibt es auf diesem Board nicht, es hat %u Ausgang/Ausgaenge",
                 channelNumber, (unsigned)BoardChannels);
        return true;
    }

    // Nicht aktivierte Kanaele legt das Setup nicht an. Ein Testbefehl darauf liefe ins Leere,
    // deshalb hier der Hinweis statt stiller Wirkungslosigkeit.
    FanChannel *channel = _channel[channelNumber - 1];
    if (channel == nullptr)
    {
        logInfoP("Kanal %d ist in der ETS nicht aktiviert - dort aktivieren, dann neu programmieren",
                 channelNumber);
        return true;
    }

    const size_t space = cmd.find(' ', 4);
    const std::string rest = (space == std::string::npos) ? "" : cmd.substr(space + 1);

    if (rest.empty())
    {
        channel->printDetail();
        return true;
    }

    if (rest == "auto")
    {
        channel->releaseOverride();
        logInfoP("Kanal %d zurueck auf Regelbetrieb", channelNumber);
    }
    else if (rest == "a")
        channel->setOverride(-1, 0);
    else if (rest == "b")
        channel->setOverride(-1, 1);
    else if (rest[0] == 'p')
    {
        const int power = atoi(rest.substr(1).c_str());
        if (power < 0 || power > 100)
            logInfoP("Leistung muss zwischen 0 und 100 Prozent liegen");
        else
            channel->setOverride((int16_t)power, -1);
    }
    else
    {
        logInfoP("Unbekannter Zusatz \"%s\", siehe \"fan\"", rest.c_str());
    }

    return true;
}
