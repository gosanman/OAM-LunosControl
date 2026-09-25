#pragma once

#include <stdint.h>
#include "FanTypes.h"

/**
 * @brief Ansteuerung eines Luefterknotens, entkoppelt vom Ansteuerverfahren.
 *
 * Die unterstuetzten Luefter (Maico PPB30, Fawas HST) tragen Drehzahl UND Foerderrichtung
 * auf **einem** Ansteuerpfad: die Mittelstellung ist Stillstand, das eine Ende volle Leistung
 * Richtung A, das andere volle Leistung Richtung B. Ein Maico enthaelt zwar zwei Luefter,
 * die aber innerhalb des Geraetes immer gleich herum drehen und deshalb gemeinsam an einem
 * Ausgang haengen.
 *
 * Daraus folgt die wichtigste Eigenschaft dieser Schnittstelle: **0 % Stellgroesse ist nicht
 * "aus", sondern volle Leistung Richtung A.** Der sichere Zustand ist die Mittelstellung.
 *
 * Diese Schnittstelle beschreibt **was** angesteuert wird, nicht **wie**. Sie kennt deshalb
 * bewusst keine Pins, keine Frequenzen und kein Protokoll: eine Implementierung bringt ihre
 * eigene Konfiguration im Konstruktor mit, weil ein PWM-Ausgang Pins braucht, ein digitaler
 * Regler dagegen eine Schnittstelle und eine Adresse. Angelegt werden die Objekte vom
 * Geraete-Header ueber FAN_INIT(), so wie OGM-Common es mit LED_INIT() haelt.
 *
 * Vorhandene Implementierungen: PwmFan (Stellgroesse als Tastverhaeltnis, Drehzahl ueber einen
 * getrennten Tacho-Eingang), DShotFan (bidirektionales DShot, Stellgroesse und Drehzahl auf
 * derselben Leitung).
 */
class IFanHardware
{
  public:
    virtual ~IFanHardware() = default;

    /**
     * @brief Ausgaenge in den sicheren Zustand bringen. Wird einmalig im Setup aufgerufen.
     *
     * Pins und Protokollparameter kennt die Implementierung bereits aus ihrem Konstruktor;
     * hier wird nur noch scharf geschaltet.
     */
    virtual void begin() = 0;

    // --- Ansteuerung ---

    /**
     * @brief Mittelstellung setzen (Stellgroesse bei Stillstand).
     *
     * 50 % entspricht einem reversierenden Luefter, 0 % einem gewoehnlichen Luefter,
     * bei dem 0 aus und 100 volle Leistung bedeutet.
     */
    virtual void setMidpoint(uint8_t percent) = 0;

    /**
     * @brief Drehzahl in der angegebenen Richtung ausgeben.
     * @param speedPercent 0..100, bezogen auf die jeweilige Haelfte. 0 ergibt Stillstand.
     */
    virtual void drive(Fan::Direction dir, uint8_t speedPercent) = 0;

    /** @brief Sicherer Zustand: Mittelstellung ausgeben und den Lastschalter oeffnen. */
    virtual void stop() = 0;

    // --- Drehzahlrueckmeldung ---
    //
    // Bewusst nicht "Tacho" genannt: woher die Drehzahl kommt, ist Sache der Implementierung.
    // PwmFan zaehlt Impulse an einem eigenen Eingang, DShotFan liest sie als Telemetrie auf
    // derselben Leitung zurueck, ueber die es ansteuert.

    /** @brief true, wenn dieser Knoten eine Drehzahl melden kann. */
    virtual bool hasSpeedFeedback() const = 0;

    /**
     * @brief Messung scharf schalten. Aus dem Kontext aufrufen, der auch messen soll.
     *
     * Getrennt von begin(), weil eine interruptbasierte Messung auf dem Core landet, der sie
     * registriert: die Messung laeuft auf Core 1, die Ansteuerung auf Core 0.
     */
    virtual void beginSpeedFeedback() = 0;

    /** @brief Messung fortschreiben. Aus demselben Kontext wie beginSpeedFeedback(). */
    virtual void updateSpeedFeedback() = 0;

    /** @brief Letzte gemessene Drehzahl. Aus einem anderen Core lesbar. */
    virtual uint16_t rpm() const = 0;

    /**
     * @brief Monoton steigender Zaehler fuer die Blockiererkennung.
     *
     * Er muss zunehmen, solange sich der Rotor dreht, und stehen bleiben, wenn nicht. Ob dahinter
     * Tacho-Impulse oder empfangene Telemetriewerte stecken, ist unerheblich - die
     * Blockiererkennung wertet nur die Zunahme aus. Niemals zuruecksetzen: die Erkennung
     * vergleicht Zaehlerstaende zweier Zeitfenster, und ein Ruecksprung sieht wie Stillstand aus.
     */
    virtual uint32_t speedPulses() const = 0;
};
