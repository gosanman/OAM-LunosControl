// ============================================================================
// Simulator: Hauptprogramm des Kerns (kwlsim.exe).
//
// Liest Befehle zeilenweise von stdin, schreibt Ereignisse als JSON-Zeilen nach
// stdout. Jeder Befehl endet mit {"ev":"ok"}. Damit laesst sich der Kern von
// der Python-Oberflaeche steuern - oder mit einer Szenario-Datei direkt:
//
//     kwlsim.exe < szenario.txt
//
// Befehle:
//   load <hex>          Parameterspeicher setzen, als waere das Geraet schon
//                       programmiert (ohne Download, ohne Neustart)
//   pimg <hex>          Parameterspeicher fuer den naechsten ETS-Download
//   ets [ms]            ETS-Download: knx.configured() faellt fuer [ms] (Vorgabe
//                       3000) auf false, dann processBeforeRestart, Flash
//                       sichern, neue Parameter, Neustart
//   unload              Applikation entladen (Geraet wie neu), Neustart
//   restart             Neustart (processBeforeRestart, Flash, setup)
//   bus 0|1             KNX-Bus aus/ein. Aus: SAVE (savePower, Flash), Prozessor
//                       aus. Ein: Neustart.
//   v12 0|1             12 V aus/ein. Ein: DACs im Werkszustand (0 V).
//   ko <nr> <dpt> <wert> Telegramm vom Bus an Objekt <nr>, z. B. ko 26 5.10 3
//   btn short|long|double  Funktionstaste
//   run <ms>            Zeit fortschreiben, loop() in Schritten von stepms
//   stepms <ms>         Schrittweite (Vorgabe 10 ms)
//   cmd <text>          Konsolenbefehl, z. B. cmd kwl r1
//   diag <text>         Befehl ueber das Diagnose-KO (Antwort in 14 Zeichen)
//   gain <kanal> <f>    Steigungsfehler eines DAC-Kanals (1..4), z. B. 1.0026
//   state               Zustand als JSON
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <new>
#include <sstream>
#include <string>

#include "sim.h"

#include "KwlFanModule.h"
#include "KwlRoomModule.h"

extern Kwl::KwlFanModule openknxKwlFanModule;

namespace
{
    std::vector<uint8_t> pending;
    uint32_t stepMs = 10;
    bool busOn = false;

    OpenKNX::Module* modules[] = {&openknxKwlRoomModule, &openknxKwlFanModule};
    constexpr int kModules = 2;

    void event(const std::string& name, const std::string& extra = "")
    {
        sim::emit("{\"ev\":\"" + name + "\",\"t\":" + std::to_string(sim::now) +
                  (extra.empty() ? "" : "," + extra) + "}");
    }

    bool parseHex(const std::string& hex, std::vector<uint8_t>& out)
    {
        if (hex.size() % 2 != 0)
            return false;
        out.assign(hex.size() / 2, 0);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
        return true;
    }

    // ------------------------------------------------------------ Flash

    void flashSave()
    {
        sim::flashBlocks.assign(kModules, {});
        for (int i = 0; i < kModules; i++)
        {
            openknx.flash.writeTarget = &sim::flashBlocks[i];
            modules[i]->writeFlash();
            sim::flashBlocks[i].resize(modules[i]->flashSize(), 0);
        }
        openknx.flash.writeTarget = nullptr;
        sim::flashValid = true;
        event("flash", "\"action\":\"save\"");
    }

    void flashLoad()
    {
        for (int i = 0; i < kModules; i++)
        {
            const uint16_t size = modules[i]->flashSize();
            if (sim::flashValid && i < static_cast<int>(sim::flashBlocks.size()) && size > 0 &&
                sim::flashBlocks[i].size() == size)
            {
                openknx.flash.readPtr = sim::flashBlocks[i].data();
                openknx.flash.readEnd = sim::flashBlocks[i].data() + size;
                modules[i]->readFlash(sim::flashBlocks[i].data(), size);
            }
            else
            {
                openknx.flash.readPtr = nullptr;
                modules[i]->readFlash(nullptr, 0);
            }
        }
        openknx.flash.readPtr = nullptr;
    }

    // ------------------------------------------------------------ Prozessor

    /// Kaltstart des Prozessors: RAM weg, DACs und Flash bleiben.
    void boot()
    {
        // RAM zuruecksetzen: die beiden Modulobjekte neu aufbauen. Die Kanaele
        // (new KwlRoom/KwlFan) bleiben als Leck stehen - im Simulator egal.
        openknxKwlRoomModule.~KwlRoomModule();
        new (&openknxKwlRoomModule) Kwl::KwlRoomModule();
        openknxKwlFanModule.~KwlFanModule();
        new (&openknxKwlFanModule) Kwl::KwlFanModule();

        for (size_t i = 0; i < sim::kos.size(); i++)
            sim::kos[i].simInit(static_cast<uint16_t>(i));
        openknx.ledFunctions.simReset();
        openknx.func1Button = OpenKNX::SimButton{};

        sim::bootAt = sim::now;
        sim::mcuRunning = true;
        sim::downloading = false;
        sim::resetInvariants();

        const bool cfg = sim::configured();
        event("boot", std::string("\"configured\":") + (cfg ? "true" : "false"));

        // Reihenfolge wie OpenKNX Common::setup(): erst setup() aller Module,
        // dann den Flash lesen.
        for (auto* m : modules)
            m->setup(cfg);
        if (cfg)
            flashLoad();
    }

    void loopOnce()
    {
        const bool cfg = sim::configured();
        for (auto* m : modules)
            m->loop(cfg);
    }

    /// Jede Aenderung einer Ausgangsspannung mit Zeitstempel - fuer den
    /// Verlauf in der Oberflaeche. Die Firmware schreibt in jedem Durchlauf;
    /// gemeldet wird nur, was sich am Ausgang tatsaechlich aendert.
    void emitDacChanges()
    {
        static double last[4] = {-1, -1, -1, -1};
        bool changed = false;
        double v[4];
        for (int ch = 0; ch < FANDRV_BOARD_CHANNELS && ch < 4; ch++)
        {
            v[ch] = sim::outputVolt(ch);
            if (std::fabs(v[ch] - last[ch]) > 0.0005)
                changed = true;
        }
        if (!changed)
            return;
        std::string o = "{\"ev\":\"dac\",\"t\":" + std::to_string(sim::now) + ",\"v\":[";
        for (int ch = 0; ch < FANDRV_BOARD_CHANNELS && ch < 4; ch++)
        {
            last[ch] = v[ch];
            o += (ch ? "," : "") + sim::num(v[ch], 3);
        }
        sim::emit(o + "]}");
    }

    void run(uint64_t ms)
    {
        const uint64_t end = sim::now + ms;
        while (sim::now < end)
        {
            const uint64_t step = std::min<uint64_t>(stepMs, end - sim::now);
            sim::now += step;
            if (sim::mcuRunning)
                loopOnce();
            sim::checkInvariants();
            emitDacChanges();
        }
    }

    void beforeRestart()
    {
        if (!sim::mcuRunning)
            return;
        for (auto* m : modules)
            m->processBeforeRestart();
        flashSave();
    }

    void setBus(bool on)
    {
        if (on == busOn)
            return;
        busOn = on;
        if (!on)
        {
            // SAVE-Interrupt: nur bei konfiguriertem Geraet (Common::loop).
            if (sim::mcuRunning && sim::configured())
            {
                for (auto* m : modules)
                    m->savePower();
                flashSave();
            }
            sim::mcuRunning = false;
            event("mcu", "\"running\":false");
        }
        else
        {
            boot();
        }
    }

    // ------------------------------------------------------------ Befehle

    std::string rest(std::istringstream& in)
    {
        std::string r;
        std::getline(in, r);
        const size_t p = r.find_first_not_of(' ');
        return p == std::string::npos ? "" : r.substr(p);
    }

    void receiveKo(int nr, const std::string& dptText, const std::string& valueText)
    {
        if (nr < 0 || nr >= static_cast<int>(sim::kos.size()))
        {
            event("error", "\"text\":" + sim::esc("unbekanntes Objekt " + std::to_string(nr)));
            return;
        }
        const Dpt dpt = sim::parseDpt(dptText);
        GroupObject& ko = sim::kos[nr];
        bool ok;
        if (dpt.mainGroup == 1)
        {
            const bool v = valueText == "1" || valueText == "true" || valueText == "ein";
            ok = ko.simReceive(KNXValue(v), dpt);
        }
        else if (dpt.mainGroup == 16)
            ok = ko.simReceive(KNXValue(valueText.c_str()), dpt);
        else
            ok = ko.simReceive(KNXValue(std::stod(valueText)), dpt);
        if (!ok)
        {
            event("error", "\"text\":" + sim::esc("Wert " + valueText + " passt nicht zu DPT " +
                                                   dptText + " - Objekt " + std::to_string(nr)));
            return;
        }

        // Ein unkonfiguriertes Geraet hat keine Zuordnungstabelle - es kommt
        // nichts an. Ohne Bus laeuft der Prozessor nicht.
        const bool delivered = sim::mcuRunning && sim::configured();
        event("rx", "\"nr\":" + std::to_string(nr) + ",\"dpt\":\"" + dptText +
                        "\",\"val\":" + sim::koValueJson(ko, dpt) +
                        ",\"delivered\":" + (delivered ? "true" : "false"));
        if (delivered)
            for (auto* m : modules)
                m->processInputKo(ko);
    }

    void console(const std::string& text, bool debugKo)
    {
        if (!sim::mcuRunning)
        {
            event("error", "\"text\":\"Prozessor laeuft nicht (kein KNX-Bus)\"");
            return;
        }
        if (text == "help" || text == "?")
        {
            for (auto* m : modules)
                m->showHelp();
            return;
        }
        for (auto* m : modules)
            if (m->processCommand(text, debugKo))
                return;
        event("error", "\"text\":" + sim::esc("unbekannter Befehl: " + text));
    }

    void state()
    {
        std::string flags = std::string("\"uptime\":") + std::to_string(millis()) +
                            ",\"mcu\":" + (sim::mcuRunning ? "true" : "false") +
                            ",\"bus\":" + (busOn ? "true" : "false") +
                            ",\"v12\":" + (sim::chips[0].powered ? "true" : "false") +
                            ",\"paramsValid\":" + (sim::paramsValid ? "true" : "false") +
                            ",\"downloading\":" + (sim::downloading ? "true" : "false") +
                            ",\"configured\":" + (sim::configured() ? "true" : "false");
        sim::emit("{\"ev\":\"state\",\"t\":" + std::to_string(sim::now) + "," + flags +
                  ",\"sim\":" + sim::snapshotJson() + "}");
    }

    void handle(const std::string& line)
    {
        std::istringstream in(line);
        std::string op;
        in >> op;

        if (op.empty() || op[0] == '#')
            return;
        if (op == "load" || op == "pimg")
        {
            std::string hex;
            in >> hex;
            std::vector<uint8_t> img;
            if (!parseHex(hex, img))
            {
                event("error", "\"text\":\"Parameterabbild ist kein Hex\"");
                return;
            }
            img.resize(MAIN_ParameterSize, 0);
            if (op == "load")
            {
                sim::params = img;
                sim::paramsValid = true;
            }
            pending = img;
        }
        else if (op == "ets")
        {
            uint64_t ms = 3000;
            in >> ms;
            if (!sim::mcuRunning)
            {
                event("error", "\"text\":\"ETS-Download ohne KNX-Bus nicht moeglich\"");
                return;
            }
            if (pending.empty())
                pending = sim::params;
            sim::downloading = true;
            event("download", "\"phase\":\"start\"");
            run(ms);
            beforeRestart();
            sim::params = pending;
            sim::paramsValid = true;
            event("download", "\"phase\":\"done\"");
            boot();
        }
        else if (op == "unload")
        {
            beforeRestart();
            sim::paramsValid = false;
            if (sim::mcuRunning)
                boot();
        }
        else if (op == "restart")
        {
            if (sim::mcuRunning)
            {
                beforeRestart();
                boot();
            }
        }
        else if (op == "bus")
        {
            int v = 0;
            in >> v;
            setBus(v != 0);
        }
        else if (op == "v12")
        {
            int v = 0;
            in >> v;
            sim::dacPower(v != 0);
            event("v12", std::string("\"on\":") + (v ? "true" : "false"));
            emitDacChanges();
        }
        else if (op == "ko")
        {
            int nr = -1;
            std::string dpt;
            in >> nr >> dpt;
            receiveKo(nr, dpt, rest(in));
        }
        else if (op == "btn")
        {
            std::string kind;
            in >> kind;
            auto& b = openknx.func1Button;
            std::function<void()> cb =
                kind == "short" ? b.shortCb : (kind == "long" ? b.longCb : b.doubleCb);
            if (sim::mcuRunning && cb)
                cb();
        }
        else if (op == "run")
        {
            uint64_t ms = 0;
            in >> ms;
            run(ms);
        }
        else if (op == "stepms")
        {
            in >> stepMs;
            if (stepMs == 0)
                stepMs = 1;
        }
        else if (op == "cmd")
            console(rest(in), false);
        else if (op == "diag")
            console(rest(in), true);
        else if (op == "gain")
        {
            int ch = 0;
            double f = 1.0;
            in >> ch >> f;
            if (ch >= 1 && ch <= 4)
                sim::gain[ch - 1] = f;
        }
        else if (op == "state")
            state();
        else if (op == "quit")
            std::exit(0);
        else
            event("error", "\"text\":" + sim::esc("unbekannter Befehl: " + op));
    }
} // namespace

int main()
{
    std::ios::sync_with_stdio(false);
    static char outbuf[1 << 16];
    std::setvbuf(stdout, outbuf, _IOFBF, sizeof(outbuf));

    // Ausgangslage: alles stromlos, nie programmiert. Die DACs starten beim
    // ersten "v12 1" im Werkszustand.
    sim::dacPower(false);
    for (size_t i = 0; i < sim::kos.size(); i++)
        sim::kos[i].simInit(static_cast<uint16_t>(i));
    event("hello", "\"paramSize\":" + std::to_string(MAIN_ParameterSize) +
                       ",\"maxKo\":" + std::to_string(MAIN_MaxKoNumber) +
                       ",\"channels\":" + std::to_string(FANDRV_BOARD_CHANNELS) +
                       ",\"boardId\":" + std::to_string(FANDRV_BOARD_ID));
    std::fflush(stdout);

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        try
        {
            handle(line);
        }
        catch (const std::exception& e)
        {
            event("error", "\"text\":" + sim::esc(std::string("Ausnahme: ") + e.what()));
        }
        sim::emit("{\"ev\":\"ok\"}");
        std::fflush(stdout);
    }
    return 0;
}
