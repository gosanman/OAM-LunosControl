#pragma once

#include <stdint.h>

namespace Fan
{
    /**
     * Betriebsart des Luefters, entspricht PT-FanMode in Fan.templ.xml.
     *
     * Es gibt hier bewusst kein "Deaktiviert": wie viele Luefter es gibt, sagt der
     * geraeteweite Zaehler "Anzahl Luefter" - und das ist eine andere Frage als die, welcher
     * Art ein vorhandener Luefter ist.
     */
    enum class ChannelType : uint8_t
    {
        NonReversible = 0,
        Reversible = 1
    };

    /** Richtungsart, empfangen ueber KO "Richtungsart" (DPT 5.010). */
    enum class DirMode : uint8_t
    {
        Reversing = 0,
        OnlyA = 1,
        OnlyB = 2,
        Last = OnlyB
    };

    /** Woher der Master seine Gruppenvorgabe nimmt (Parameter "Sollwert kommt aus"). */
    enum class SetpointSource : uint8_t
    {
        Fixed = 0,
        ExternalKo = 1,
        InternalControl = 2,
        Hysteresis = 3
    };

    /** Richtungsart-Vorgabe in der ETS (Parameter "Richtungsart"). */
    enum class DirModeSel : uint8_t
    {
        Reversing = 0,
        OnlyA = 1,
        OnlyB = 2,
        ViaKo = 3
    };

    /** Physikalische Foerderrichtung. */
    enum class Direction : uint8_t
    {
        A = 0,
        B = 1
    };

    /**
     * Fehlercode, gesendet ueber KO "Fehlercode" (DPT 5.010).
     * Die Reihenfolge ist die Prioritaet: der kleinste anliegende Wert > 0 gewinnt.
     */
    enum class Fault : uint8_t
    {
        None = 0,
        EnableMissing = 1,       // Freigabe fehlt oder Ueberwachungszeit abgelaufen
        MasterTimeout = 2,       // Lebenszeichen des Masters ausgeblieben
        Config = 3,              // Kanaltyp passt nicht zur Hardware, Kennlinie unplausibel
        InvalidValue = 4,        // ungueltiger Empfangswert
        NoRotation = 5,          // angesteuert, aber keine Drehzahl (auch blockierter Rotor)
        MonitoringSuspended = 6, // Ueberwachung ausgesetzt, weil suspendiert
        DewPointBlocked = 7,     // Taupunktwaechter sperrt: Aussenluft ist die feuchtere
        DewPointNoData = 8       // Taupunktwaechter ohne brauchbare Messwerte
    };

    /**
     * Basis der LED-Funktions-IDs. Kanal n meldet sich als LedFunctionBase + n an; in der ETS
     * waehlt der Benutzer pro Info-LED aus, welche Funktion sie anzeigt.
     *
     * Die IDs sind geraeteweit eindeutig zu halten: OGM-Common belegt 1..4, das Netzwerkmodul 10,
     * das Logikmodul 90..99. 110..117 ist frei und laesst Platz fuer alle acht Kanaele.
     * Muss zu den Enumerationswerten von PT-SLEDFunc in Fan.share.xml passen.
     */
    constexpr uint32_t LedFunctionBase = 110;

    /** Helligkeit der Status-LEDs: 25 % von 255. Voll aufgedreht blenden die RGBs im Verteiler. */
    constexpr uint8_t LedLevel = 64;

    /**
     * Verfallszeit einer Konsolen-Uebersteuerung. Ein Testbefehl ist zum Messen und Hinsehen da,
     * nicht zum Betreiben - ein vergessener Befehl darf die Anlage nicht dauerhaft von ihrer
     * Regelung abschneiden. Nach Ablauf uebernimmt wieder die normale Sollwertbildung.
     */
    constexpr uint32_t ConsoleOverrideMs = 10UL * 60UL * 1000UL;

    /** Verhalten des Taupunktwaechters, wenn Messwerte fehlen oder veraltet sind. */
    enum class DewFallback : uint8_t
    {
        KeepVentilating = 0,
        Block = 1
    };

    /** Zustaende der Kanal-Zustandsmaschine. */
    enum class State : uint8_t
    {
        Off,        // Stellgroesse 0
        StartPulse, // Anlaufpuls laeuft
        Running,    // Zielwert wird gefahren
        DeadTime    // Totzeit vor dem Richtungswechsel, Lüfter steht
    };

    /** Blockiererkennung nach S-7: Fensterlaenge und Anzahl leerer Fenster bis zum Fehler. */
    constexpr uint32_t BlockWindowMs = 5000;
    constexpr uint8_t BlockWindowsToFault = 2;

    /**
     * Nachfuehrintervalle der Diagnose-Objekte. Diese Werte werden nur in das KO geschrieben,
     * nicht gesendet: eine Leseanforderung liefert damit den aktuellen Stand, ohne dass
     * zyklischer Busverkehr entsteht.
     */
    constexpr uint32_t RunHoursWriteMs = 30UL * 60UL * 1000UL; // Betriebsstunden
    constexpr uint32_t CycleRestWriteMs = 5000;                // Restzeit des Taktes
}
