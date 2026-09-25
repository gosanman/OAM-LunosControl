#include "PwmFan.h"
#include <Arduino.h>

// Nur fuer die Startmeldung: der Logger haengt an der Facade. Bewusst hier und nicht im Header,
// damit die Schnittstelle des Ansteuerverfahrens leicht bleibt.
#include "OpenKNX.h"
#include "hardware.h"

void PwmFan::configureShared(uint32_t freqHz)
{
    // Global fuer den Core, deshalb nur einmal aus FAN_INIT() des Geraete-Headers aufrufen.
    if (freqHz < 500) freqHz = 500;
    if (freqHz > 20000) freqHz = 20000;

    analogWriteFreq(freqHz);
    analogWriteRange(PwmRange);

    // Die Polaritaet ist eine Board-Eigenschaft und stellt bei diesen Luftern die Foerderrichtung
    // auf den Kopf, wenn sie falsch ist. Deshalb beim Start protokollieren - hier und nicht je
    // Kanal, damit die Zeile einmal erscheint und nicht je Ausgang.
#ifdef FAN_PWM_ACTIVE_LOW
    logInfo("PwmFan", "Ausgang invertiert (Open-Drain mit Pullup), %u Hz", (unsigned)freqHz);
#else
    logInfo("PwmFan", "Ausgang nicht invertiert, %u Hz", (unsigned)freqHz);
#endif
}

void PwmFan::begin()
{
    if (_pinDrive >= 0) pinMode(_pinDrive, OUTPUT);
    if (_pinDriveMirror >= 0) pinMode(_pinDriveMirror, OUTPUT);
    if (_pinSwitch >= 0) pinMode(_pinSwitch, OUTPUT);

    stop();
}

void PwmFan::beginSpeedFeedback()
{
    // Der Interrupt landet auf dem Core, der ihn registriert - deshalb nicht in begin().
    if (_pinTacho >= 0) _tacho.begin((uint8_t)_pinTacho);
}

void PwmFan::writeDuty(uint8_t dutyPercent)
{
    if (dutyPercent > 100) dutyPercent = 100;

    // Bewusst kein Sonderfall fuer 0: bei dieser Ansteuerung ist 0 % volle Leistung
    // Richtung A und damit ein gueltiger Betriebspunkt, kein "aus".
    uint32_t duty = (uint32_t)dutyPercent * PwmRange / 100;

#ifdef FAN_PWM_ACTIVE_LOW
    // Invertierende Endstufe (Level-Shifter auf NMOS mit Pullup): das Tastverhaeltnis am Pin
    // ist das Gegenstueck zu dem, was der Luefter sehen soll. Die Umkehr passiert bewusst
    // erst hier - alles darueber rechnet in Werten, die am Luefter ankommen.
    duty = PwmRange - duty;
#endif

    // Der Spiegel-Ausgang bekommt exakt dasselbe Signal (zweiter Luefter desselben Geraetes).
    if (_pinDrive >= 0) analogWrite(_pinDrive, duty);
    if (_pinDriveMirror >= 0) analogWrite(_pinDriveMirror, duty);
}

void PwmFan::drive(Fan::Direction dir, uint8_t speedPercent)
{
    if (speedPercent == 0)
    {
        stop();
        return;
    }

    if (speedPercent > 100) speedPercent = 100;

    uint8_t duty;
    if (dir == Fan::Direction::A && _midpoint > 0)
    {
        // Untere Haelfte: von der Mittelstellung Richtung 0.
        duty = (uint8_t)(_midpoint - (uint32_t)speedPercent * _midpoint / 100);
    }
    else
    {
        // Obere Haelfte: von der Mittelstellung Richtung 100.
        // Auch der Fall Mittelstellung 0 (gewoehnlicher Luefter) laeuft hier durch.
        duty = (uint8_t)(_midpoint + (uint32_t)speedPercent * (100 - _midpoint) / 100);
    }

    writeDuty(duty);
    if (_pinSwitch >= 0) digitalWrite(_pinSwitch, HIGH);
}

void PwmFan::stop()
{
    // Sicherer Zustand ist die Mittelstellung, nicht 0 %.
    writeDuty(_midpoint);
    if (_pinSwitch >= 0) digitalWrite(_pinSwitch, LOW);
}
