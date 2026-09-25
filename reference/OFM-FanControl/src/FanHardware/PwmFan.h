#pragma once

#include "../IFanHardware.h"
#include "PWMFanTachoReader.h"

/**
 * @brief Luefteransteuerung ueber ein Tastverhaeltnis, mit getrenntem Tacho-Eingang.
 *
 * Das ist das Verfahren der beiden bisherigen Boards: ein Ausgang je Knoten traegt Drehzahl und
 * Richtung gemeinsam (siehe IFanHardware), ein Lastschalter trennt den Luefter im Stillstand,
 * und die Drehzahl kommt ueber einen eigenen Eingang zurueck.
 *
 * Die Klasse heisst nach dem Verfahren, nicht nach dem Mikrocontroller: was sich zwischen
 * Boards unterscheidet, ist die Art der Ansteuerung, nicht der Prozessor. analogWrite() ist
 * Arduino-API und nicht RP2040-spezifisch; nur configureShared() greift auf eine Eigenheit
 * dieser Plattform zu.
 */
class PwmFan : public IFanHardware
{
  public:
    /**
     * @param pinDrive       Ansteuerpfad, traegt Drehzahl und Richtung
     * @param pinDriveMirror zweiter Ausgang mit **identischem** Signal, < 0 wenn nicht vorhanden
     * @param pinSwitch      Lastschalter, < 0 wenn nicht vorhanden
     * @param pinTacho       Drehzahleingang, < 0 wenn nicht vorhanden
     *
     * Der Spiegel-Ausgang ist keine zweite Richtung, sondern dieselbe Ansteuerung ein zweites
     * Mal: das MrSpieb-Board fuehrt je Knoten zwei Ausgaenge heraus, einen je Luefter eines
     * Maico-Paares. Beide Luefter eines Geraetes drehen ohnehin immer gleich herum. Boards mit
     * nur einem Ausgang (Reg1 Fan-Addon-X2) klemmen beide Luefter auf dieselbe Klemme, was der
     * hochohmige PWM-Eingang der Luefter erlaubt.
     */
    PwmFan(int8_t pinDrive, int8_t pinDriveMirror, int8_t pinSwitch, int8_t pinTacho)
        : _pinDrive(pinDrive), _pinDriveMirror(pinDriveMirror), _pinSwitch(pinSwitch),
          _pinTacho(pinTacho) {}

    void begin() override;

    void setMidpoint(uint8_t percent) override { _midpoint = percent > 100 ? 100 : percent; }
    void drive(Fan::Direction dir, uint8_t speedPercent) override;
    void stop() override;

    bool hasSpeedFeedback() const override { return _pinTacho >= 0; }
    void beginSpeedFeedback() override;
    void updateSpeedFeedback() override { if (_tacho.isEnabled()) _tacho.update(); }
    uint16_t rpm() const override { return _tacho.getRPM(); }
    uint32_t speedPulses() const override { return _tacho.getPulseCount(); }

    /**
     * @brief PWM-Aufloesung und -Frequenz einmalig setzen.
     *
     * Wirkt auf dem RP2040 global und nicht je Pin, deshalb ist die Frequenz ein geraeteweiter
     * ETS-Parameter und nicht je Kanal einstellbar. Aufzurufen aus FAN_INIT() des
     * Geraete-Headers, weil nur das Board weiss, ob es ueberhaupt PWM benutzt.
     */
    static void configureShared(uint32_t freqHz);

  private:
    void writeDuty(uint8_t dutyPercent);

    int8_t _pinDrive = -1;
    int8_t _pinDriveMirror = -1;
    int8_t _pinSwitch = -1;
    int8_t _pinTacho = -1;
    uint8_t _midpoint = 50;
    PWMFanTachoReader _tacho;

    static constexpr uint16_t PwmRange = 1000;
};
