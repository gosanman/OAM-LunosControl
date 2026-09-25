#pragma once

#include "../IFanHardware.h"

#ifdef ARDUINO_ARCH_RP2040
    #include <hardware/pio.h>
#endif

/**
 * @brief Luefteransteuerung ueber bidirektionales DShot, umgesetzt mit dem PIO-Block des RP2040.
 *
 * DShot kommt aus dem Drohnenbereich und ersetzt dort die analoge Ansteuerung digitaler Regler
 * (ESC). Fuer diese Applikation ist daran zweierlei interessant: die Stellgroesse ist ein
 * digitaler Wert statt eines Tastverhaeltnisses, und in der bidirektionalen Variante liefert der
 * Regler die Drehzahl auf **derselben Leitung** zurueck - ein getrennter Tacho-Eingang entfaellt.
 *
 * ## Wozu diese Klasse da ist
 *
 * Sie ist der Gegenbeweis zu PwmFan: zwei voellig verschiedene Ansteuerverfahren hinter einer
 * Schnittstelle, ohne dass Kanallogik oder Modul davon etwas mitbekommen. Wo PwmFan Pins und ein
 * Tastverhaeltnis braucht, braucht diese Klasse einen PIO-Block, eine Bitrate und die Polzahl des
 * Motors - deshalb kennt IFanHardware weder das eine noch das andere.
 *
 * ## Richtung: hier explizit statt ueber die Mittelstellung
 *
 * PwmFan kodiert die Foerderrichtung in den Pegel, weshalb die Mittelstellung der Stillstand ist.
 * DShot braucht diesen Umweg nicht: der 3D-Modus der ueblichen Regler-Firmware teilt den
 * Stellbereich in zwei Haelften, 48..1047 fuer die eine und 1048..2047 fuer die andere Richtung,
 * und 0 ist Stillstand. setMidpoint() wird deshalb angenommen und **bewusst ignoriert** - genau
 * die Art Unterschied, die eine Abstraktion tragen muss.
 *
 * ## Zyklisches Senden
 *
 * Anders als ein Tastverhaeltnis ist DShot nicht einstellen-und-fertig: der Regler erwartet
 * laufend Rahmen und laeuft sonst in seinen eigenen Timeout. drive() merkt sich deshalb nur den
 * Zielwert; gesendet wird in updateSpeedFeedback(), das zyklisch auf Core 1 laeuft und im selben
 * Zug die Antwort einliest.
 *
 * ## Verdrahtung
 *
 * Die Leitung muss in beide Richtungen benutzbar sein: der Controller sendet, gibt die Leitung
 * frei und liest die Antwort. Ein Pfad ueber Levelshifter oder Optokoppler geht deshalb **nicht**;
 * der Pin muss direkt am Mikrocontroller haengen.
 *
 * ## Stand: Referenzimplementierung, kein Produktivpfad
 *
 * **Kein Board dieser Applikation benutzt diese Klasse.** Sie existiert, um die Abstraktion
 * abzugrenzen: erst ein zweites, wirklich anderes Verfahren zeigt, was in IFanHardware gehoert
 * und was nicht. Auf der Hardwareseite ist DShot bisher kein Thema.
 *
 * Die Protokollschicht (Rahmen, CRC, GCR-Dekodierung, Periode nach Drehzahl) ist vollstaendig und
 * folgt der Spezifikation. Das PIO-Timing ist **nicht auf Hardware verifiziert** - dafuer fehlen
 * ein Regler und eine bidirektionale Leitung. Das Taktbudget ist in DShotFan.cpp Zeile fuer Zeile
 * dokumentiert, damit es an einem Testaufbau nachgeprueft werden kann.
 */
class DShotFan : public IFanHardware
{
  public:
    /** Bitrate. Bidirektionales DShot ist erst ab DShot300 spezifiziert. */
    enum class Speed : uint32_t
    {
        DShot300 = 300000,
        DShot600 = 600000,
        DShot1200 = 1200000
    };

    /**
     * @param pin        GPIO, direkt am RP2040 - kein Levelshifter, kein Optokoppler
     * @param speed      Bitrate; DShot600 ist der uebliche Wert
     * @param motorPoles Polzahl des Motors, fuer die Umrechnung der elektrischen in die
     *                   mechanische Drehzahl. Bei Unkenntnis ist 14 der haeufigste Wert.
     */
    DShotFan(uint8_t pin, Speed speed = Speed::DShot600, uint8_t motorPoles = 14);

    void begin() override;

    void setMidpoint(uint8_t percent) override;
    void drive(Fan::Direction dir, uint8_t speedPercent) override;
    void stop() override;

    bool hasSpeedFeedback() const override { return true; }
    void beginSpeedFeedback() override;
    void updateSpeedFeedback() override;
    uint16_t rpm() const override { return _rpm; }
    uint32_t speedPulses() const override { return _telemetryFrames; }

    // --- Protokoll, als reine Funktionen: ohne Hardware pruefbar ---

    /**
     * @brief 16-Bit-Rahmen bauen: 11 Bit Stellwert, 1 Bit Telemetrieanforderung, 4 Bit CRC.
     *
     * Die CRC ist im bidirektionalen Betrieb invertiert - das ist der einzige Unterschied zum
     * gewoehnlichen DShot und zugleich das, woran ein Regler die Betriebsart erkennt.
     */
    static uint16_t buildFrame(uint16_t throttle, bool telemetryRequest, bool bidirectional);

    /**
     * @brief Die 21 empfangenen Bits in eine Periode in Mikrosekunden uebersetzen.
     *
     * Schritte: NRZ aufloesen (`gcr = raw ^ (raw >> 1)`), vier Fuenfergruppen ueber die
     * GCR-Tabelle zu Nibbles, dann 12 Bit Wert und 4 Bit CRC pruefen. Der Wert selbst ist
     * `eee mmmmmmmmm`: Periode = Mantisse << Exponent.
     *
     * @return false bei ungueltiger GCR-Gruppe oder falscher CRC - dann Wert unveraendert.
     */
    static bool decodeTelemetry(uint32_t raw21, uint16_t &periodUs);

    /** @brief Periode in Mikrosekunden nach mechanischer Drehzahl. 0 bei Stillstand. */
    static uint16_t periodToRpm(uint16_t periodUs, uint8_t motorPoles);

  private:
    uint16_t throttleFor(Fan::Direction dir, uint8_t speedPercent) const;
    void sendFrame(uint16_t frame);
    bool receiveTelemetry(uint32_t &raw21);

    const uint8_t _pin;
    const uint32_t _bitrate;
    const uint8_t _motorPoles;

    uint16_t _throttle = 0; // 0 = Stillstand
    uint16_t _rpm = 0;
    uint32_t _telemetryFrames = 0;
    uint32_t _lastFrameUs = 0;
    bool _ready = false;

#ifdef ARDUINO_ARCH_RP2040
    PIO _pio = nullptr;
    int _smTx = -1;
    int _smRx = -1;
    uint _offsetTx = 0;
    uint _offsetRx = 0;
#endif

    /** Stellwertbereiche des 3D-Modus. 0 ist Stillstand, 1..47 sind Sonderbefehle. */
    static constexpr uint16_t ThrottleAMin = 48;
    static constexpr uint16_t ThrottleAMax = 1047;
    static constexpr uint16_t ThrottleBMin = 1048;
    static constexpr uint16_t ThrottleBMax = 2047;

    /** Mindestabstand zweier Rahmen. Der Regler braucht die Luecke zum Antworten. */
    static constexpr uint32_t FramePeriodUs = 1000;
};
