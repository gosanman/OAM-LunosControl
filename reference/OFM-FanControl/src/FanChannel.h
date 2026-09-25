#pragma once

#include "OpenKNX.h"
#include "FanTypes.h"
#include "IFanHardware.h"

/**
 * @brief Ein Luefterknoten. Kleinste adressierbare Einheit, in der ETS ein Kanal.
 *
 * Master und Slave benutzen dieselbe Klasse; die Rolle entscheidet der Parameter "Ist Master".
 * Der Knoten leitet seine Foerderrichtung selbst aus Zuordnung (Phase) und Taktzustand ab.
 */
class FanChannel : public OpenKNX::Channel
{
  public:
    /** Persistenter Teil des Kanalzustands. Wird von FanModule in den Flash geschrieben. */
    struct PersistentState
    {
        // Bit0 = Freigabe-Latch gesperrt, Bit1 = suspendiert,
        // Bit2 = Freigabe wurde schon einmal empfangen (E-1f)
        uint8_t flags;
        uint32_t runSeconds;   // Betriebssekunden
    };

    FanChannel(uint8_t index, IFanHardware &hardware);

    const std::string name() override { return "Fan"; }

    void setup();
    void loop();
    void processInputKo(GroupObject &ko);

    /** Messung scharf schalten bzw. fortschreiben. Laeuft auf Core 1. */
    void setup1();
    void loop1();

    /** true, wenn dieser Luefter laut geraeteweitem Zaehler "Anzahl Luefter" existiert. */
    bool isActive() const { return _active; }

    // --- Konsole ---
    /** Eine Zeile fuer die Uebersichtstabelle von "fan st". */
    void printStatusLine();
    /** Ausfuehrliche Einzelausgabe fuer "fan cNN". */
    void printDetail();
    /**
     * Testuebersteuerung setzen. power < 0 laesst die Leistung unangetastet, direction < 0 die
     * Richtung. Die Uebersteuerung ersetzt die Sollwertbildung, nicht die Vetos: Freigabe,
     * Master-Ueberwachung und Taupunktwaechter greifen weiter.
     */
    void setOverride(int16_t power, int8_t direction);
    /** Uebersteuerung beenden. Ohne aktive Uebersteuerung ein No-op. */
    void releaseOverride();
    bool overrideActive() const { return _testPower >= 0 || _testDirection >= 0; }

    PersistentState persistentState() const;
    void restore(const PersistentState &state);

    /** true genau einmal, nachdem sich ein persistenter Wert geaendert hat. */
    bool consumePersistDirty()
    {
        const bool dirty = _persistDirty;
        _persistDirty = false;
        return dirty;
    }

  private:
    // --- Ablauf ---
    void updateRole();
    void updateEnable();
    void applyOutput();
    void runMaster();
    void publish();
    void updateDiagnostics();

    // --- Rechnen ---
    uint8_t targetPower();
    uint8_t groupPower();
    uint8_t controlOutput() const;
    uint8_t hysteresisOutput();

    // --- Taupunktwaechter (nur Master) ---
    static float dewPoint(float relHumidity, float temperature);
    void updateDewGuard();
    uint8_t powerToDrive(uint8_t power) const;
    Fan::Direction desiredDirection() const;
    int32_t rpmToFlow(uint16_t rpm) const;
    Fan::Fault activeFault() const;

    /**
     * true, wenn der Fehler als Alarm gilt. Ein sperrender Taupunktwaechter und eine ausgesetzte
     * Ueberwachung sind bestimmungsgemaesser Betrieb, kein Defekt. Eine Definition fuer das
     * Alarm-KO und die Status-LED, damit beide nicht auseinanderlaufen.
     */
    static bool isAlarm(Fan::Fault fault);

    /** Status-LED nachfuehren, sofern der Benutzer diesem Kanal in der ETS eine zugewiesen hat. */
    void updateStatusLed();

    // --- Senden mit Totband und Mindestabstand ---
    bool passesDeadband(int32_t value, int32_t lastSent, uint8_t percentBand, uint16_t absBand) const;

    IFanHardware &_hw;

    // Konfiguration, in setup() eingelesen
    bool _active = false;
    Fan::ChannelType _type = Fan::ChannelType::NonReversible;
    bool _isMaster = false;
    bool _hasTacho = false;
    bool _configFault = false;

    // Zustand
    Fan::State _state = Fan::State::Off;
    Fan::DirMode _dirMode = Fan::DirMode::Reversing;
    Fan::Direction _direction = Fan::Direction::A;
    Fan::Direction _pendingDirection = Fan::Direction::A;
    uint8_t _powerSet = 0;      // empfangene Gruppenvorgabe
    float _ctrlValue = 0.0f;    // Istwert fuer die interne Regelung
    bool _ctrlValueSeen = false;
    bool _hystActive = false;   // Zweipunkt: aktueller Schaltzustand

    // Taupunktwaechter: 0 = Temperatur innen, 1 = Feuchte innen, 2 = aussen, 3 = aussen
    float _dewValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t _dewSeenAt[4] = {0, 0, 0, 0};
    bool _dewSeen[4] = {false, false, false, false};
    bool _dewBlocking = false;  // Veto aktiv, Aussenluft ist die feuchtere
    bool _dewNoData = false;    // Messwerte fehlen oder sind veraltet
    uint8_t _powerAct = 0;      // tatsaechlich kommandierte Leistung
    uint8_t _driveAct = 0;      // ausgegebene Stellgroesse
    bool _tact = false;
    bool _suspended = false;
    bool _enableLatched = true;      // false = gesperrt (selbsthaltend)
    bool _enableSeenEver = false;    // persistent: Freigabe-Objekt ist verknuepft (E-1f)
    bool _enableWatchRunning = false; // Ueberwachungszeit laeuft
    bool _invalidValue = false;
    bool _masterTimeout = false;
    bool _persistDirty = false;

    // --- Testuebersteuerung ueber die Konsole, verfaellt nach Fan::ConsoleOverrideMs ---
    int16_t _testPower = -1;    // <0 = Leistung nicht uebersteuert
    int8_t _testDirection = -1; // <0 = Richtung nicht uebersteuert
    uint32_t _testUntil = 0;

    // Zeitmarken
    uint32_t _stateSince = 0;
    uint32_t _lastEnableSeen = 0;
    uint32_t _lastMasterSeen = 0;
    uint32_t _boostUntil = 0;
    uint32_t _cycleStarted = 0;
    uint32_t _lastMasterSend = 0;
    uint32_t _lastSecondTick = 0;
    uint32_t _runSeconds = 0;

    // Drehzahl und Blockiererkennung
    uint32_t _blockWindowStart = 0;
    uint32_t _blockWindowPulses = 0;
    uint8_t _emptyWindows = 0;
    bool _blocked = false;

    // Letzte gesendete Werte fuer die Sendebedingungen
    int32_t _lastSentRpm = INT32_MIN;
    int32_t _lastSentFlow = INT32_MIN;
    uint32_t _lastSentRpmAt = 0;
    uint32_t _lastSentFlowAt = 0;
    uint8_t _lastSentPower = 0xFF;
    uint8_t _lastSentFault = 0xFF;
    bool _publishedOnce = false;

    // Nachgefuehrte Diagnosewerte (geschrieben, nicht gesendet)
    uint32_t _lastRunHoursWrite = 0;
    uint32_t _lastCycleRestWrite = 0;
    uint8_t _lastWrittenMasterOk = 0xFF;
    uint8_t _lastWrittenSuspended = 0xFF;
};
