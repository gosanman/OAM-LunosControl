#pragma once
// ============================================================================
// Simulator: gemeinsamer Zustand von Laufzeit, Board und Hauptprogramm.
//
// Der Simulator trennt, was im Geraet getrennt ist:
//   - der Prozessor (RP2040) mit der Firmware - laeuft nur mit KNX-Bus,
//   - die beiden GP8413 - laufen nur mit 12 V und halten ihren Wert,
//   - der Parameterspeicher (ETS) und der Flash - ueberleben einen Neustart.
// Ein Neustart setzt nur den RAM des Prozessors zurueck, nicht die DACs.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "OpenKNX.h"

namespace sim
{
    // --- Zeit ----------------------------------------------------------------
    extern uint64_t now;    ///< Simulationszeit in ms seit Programmstart
    extern uint64_t bootAt; ///< Zeitpunkt des letzten Prozessorstarts

    // --- Geraetezustand -------------------------------------------------------
    extern std::vector<uint8_t> params; ///< wirksamer Parameterspeicher
    extern bool paramsValid;            ///< die ETS hat das Geraet programmiert
    extern bool downloading;            ///< ETS-Download laeuft gerade
    extern bool mcuRunning;             ///< Prozessor laeuft (KNX-Bus da)
    extern bool configured();           ///< knx.configured()

    extern std::vector<GroupObject> kos;

    // --- Flash, je Modul ein Block --------------------------------------------
    extern std::vector<std::vector<uint8_t>> flashBlocks;
    extern bool flashValid;

    // --- DAC-Modell -----------------------------------------------------------
    struct Chip
    {
        bool powered = true;   ///< 12 V da
        uint8_t range = 0x00;  ///< Bereichsregister; 0x11 = 0...10 V
        uint16_t reg[2] = {0, 0};
    };
    extern Chip chips[2];
    extern double gain[4];      ///< Steigungsfehler je Kanal, 1.0 = ideal
    extern uint64_t i2cFrames;
    extern uint64_t i2cNacks;
    double outputVolt(int channel);
    void dacPower(bool on);

    /// Bereich nach dem Einschalten des Chips. ANNAHME, nicht gemessen: der
    /// Werkszustand ist 0...5 V. Die Firmware schreibt den Bereich immer vor
    /// dem ersten Sollwert (Invariante 3); ein Verstoss wird dadurch sichtbar.
    constexpr uint8_t kRangePowerOn = 0x00;

    /// Letzter Schreibzugriff je Kanal: einzeln oder als Paar.
    extern bool lastWriteWasPair[4];
    extern bool rangeViolation;

    // --- Ausgabe ------------------------------------------------------------
    void emit(const std::string& json);
    std::string esc(const std::string& s);
    std::string num(double v, int decimals = 3);
    std::string koValueJson(GroupObject& ko, const Dpt& type);
    Dpt parseDpt(const std::string& s);
    Dpt fallbackDpt(const Dpt& type);

    /// Inspektion (sim_inspect.cpp): Zustand der Module als JSON.
    std::string snapshotJson();
    /// Invariantenwaechter, nach jedem Durchlauf.
    void checkInvariants();
    void resetInvariants();
    /// Meldung an den Waechter, gleiche Meldung gedrosselt.
    void warn(const std::string& key, const std::string& inv, const std::string& text);
} // namespace sim
