#pragma once

#include <Arduino.h>

/**
 * @brief Drehzahlmessung an einem Tacho-Eingang.
 *
 * Der Puls-Zaehler ist **monoton**: er wird nie zurueckgesetzt. Die Blockiererkennung fragt
 * nicht nach der Drehzahl, sondern danach, ob sich der Zaehlerstand veraendert hat - dafuer
 * muss er weiterlaufen. Die Drehzahl entsteht aus der Differenz zweier Staende, nicht daraus,
 * den Zaehler zu leeren.
 *
 * Es gibt bewusst keine statische Instanztabelle: der Interrupt wird mit `this` als Parameter
 * registriert, deshalb ist die Anzahl der Instanzen nicht begrenzt.
 */
class PWMFanTachoReader
{
  public:
    void begin(uint8_t pin, uint8_t pulsesPerRev = 2);

    /** Aus dem Kontext aufrufen, der die Messung fuehrt (hier Core 1). */
    void update();

    /** Letzte berechnete Drehzahl. Aus einem anderen Core lesbar. */
    uint16_t getRPM() const { return _rpm; }

    /** Monotoner Pulszaehler, laeuft ueber - Differenzen bleiben trotzdem richtig. */
    uint32_t getPulseCount() const { return _pulseCount; }

    bool isEnabled() const { return _pin != 0xFF; }

  private:
    static void onPulse(void *self);

    uint8_t _pin = 0xFF;
    uint8_t _pulsesPerRev = 2;

    volatile uint32_t _pulseCount = 0; // nur im ISR erhoeht, nie zurueckgesetzt
    uint32_t _lastCount = 0;           // Stand bei der letzten Auswertung
    uint32_t _lastTime = 0;
    volatile uint16_t _rpm = 0;
};
