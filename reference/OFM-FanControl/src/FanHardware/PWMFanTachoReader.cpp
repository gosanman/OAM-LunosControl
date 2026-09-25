#include "PWMFanTachoReader.h"

void PWMFanTachoReader::begin(uint8_t pin, uint8_t pulsesPerRev)
{
    _pin = pin;
    _pulsesPerRev = pulsesPerRev > 0 ? pulsesPerRev : 1;

    pinMode(_pin, INPUT_PULLUP);

    // Mit `this` als Parameter registriert. Damit braucht es weder eine statische
    // Instanztabelle noch je Instanz eine eigene ISR-Funktion - und keine Obergrenze.
    attachInterruptParam(digitalPinToInterrupt(_pin), onPulse, FALLING, this);

    _lastTime = millis();
    _lastCount = _pulseCount;
}

void PWMFanTachoReader::update()
{
    const uint32_t now = millis();
    const uint32_t elapsed = now - _lastTime;

    // Unter einer halben Sekunde ist das Ergebnis bei kleinen Drehzahlen zu grob.
    if (elapsed < 500) return;

    // Ein 32-Bit-Wort wird auf dem RP2040 in einem Zugriff geladen, der ISR kann hier also
    // nicht dazwischenfunken. Der Zaehler laeuft weiter, gemessen wird die Differenz.
    const uint32_t count = _pulseCount;
    const uint32_t delta = count - _lastCount; // Ueberlauf ist unkritisch: Zweierkomplement

    _rpm = (uint16_t)(delta * 60000UL / (elapsed * _pulsesPerRev));

    _lastCount = count;
    _lastTime = now;
}

void PWMFanTachoReader::onPulse(void *self)
{
    static_cast<PWMFanTachoReader *>(self)->_pulseCount++;
}
