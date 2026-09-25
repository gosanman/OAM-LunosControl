#include "DShotFan.h"
#include <Arduino.h>

#ifdef ARDUINO_ARCH_RP2040
    #include <hardware/clocks.h>
    #include <hardware/gpio.h>
    #include <hardware/pio_instructions.h>
#endif

// ===========================================================================
// Protokoll - ohne Hardware pruefbar
// ===========================================================================

namespace
{
    /**
     * GCR-Tabelle des DShot-Rueckkanals: ein Nibble wird als 5 Bit uebertragen, sodass nie zu
     * viele gleiche Bits aufeinander folgen. Das macht den Datenstrom selbsttaktend und robust
     * gegen Jitter - derselbe Trick wie auf alten Disketten- und Bandlaufwerken.
     *
     * Hier steht die Umkehrung der Sendetabelle: Index ist der empfangene 5-Bit-Code, Wert das
     * Nibble. 0xFF markiert eine ungueltige Gruppe, also einen Uebertragungsfehler.
     */
    constexpr uint8_t GcrToNibble[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 0x00..0x07 unbenutzt
        0xFF, 0x09, 0x0A, 0x0B, 0xFF, 0x0D, 0x0E, 0x0F, // 0x09..0x0F
        0xFF, 0xFF, 0x02, 0x03, 0xFF, 0x05, 0x06, 0x07, // 0x12->2 0x13->3 0x15->5 0x16->6 0x17->7
        0x00, 0x08, 0x01, 0xFF, 0x04, 0x0C, 0xFF, 0xFF  // 0x18->0 0x19->8 0x1A->1 0x1C->4 0x1D->C
    };
} // namespace

uint16_t DShotFan::buildFrame(uint16_t throttle, bool telemetryRequest, bool bidirectional)
{
    if (throttle > 2047) throttle = 2047;

    // 11 Bit Stellwert plus 1 Bit Telemetrieanforderung ergeben den 12-Bit-Wert, ueber den die
    // CRC gebildet wird.
    const uint16_t value = (uint16_t)((throttle << 1) | (telemetryRequest ? 1 : 0));

    // Die CRC ist die XOR-Summe der drei Nibbles. Im bidirektionalen Betrieb invertiert - das
    // ist der einzige Unterschied zum gewoehnlichen DShot.
    uint16_t crc = (uint16_t)(value ^ (value >> 4) ^ (value >> 8));
    if (bidirectional) crc = (uint16_t)~crc;

    return (uint16_t)((value << 4) | (crc & 0x0F));
}

bool DShotFan::decodeTelemetry(uint32_t raw21, uint16_t &periodUs)
{
    // Der Regler sendet NRZ-kodiert: ein GCR-Einsbit ist ein Pegelwechsel, ein Nullbit keiner.
    // Das Aufloesen ist eine XOR-Verschiebung, danach liegen die 20 GCR-Bits frei.
    const uint32_t gcr = (raw21 ^ (raw21 >> 1)) & 0x000FFFFF;

    // Vier Fuenfergruppen zu vier Nibbles. Eine ungueltige Gruppe ist ein Uebertragungsfehler
    // und wird verworfen, statt einen Zufallswert als Drehzahl auszugeben.
    uint16_t decoded = 0;
    for (int8_t group = 3; group >= 0; group--)
    {
        const uint8_t code = (uint8_t)((gcr >> (group * 5)) & 0x1F);
        const uint8_t nibble = GcrToNibble[code];
        if (nibble == 0xFF) return false;
        decoded = (uint16_t)((decoded << 4) | nibble);
    }

    // 12 Bit Wert, 4 Bit CRC. Der Rueckkanal benutzt die NICHT invertierte CRC.
    const uint16_t value = (uint16_t)(decoded >> 4);
    const uint16_t crc = (uint16_t)(decoded & 0x0F);
    if ((uint16_t)((value ^ (value >> 4) ^ (value >> 8)) & 0x0F) != crc) return false;

    // eee mmmmmmmmm: drei Bit Exponent, neun Bit Mantisse. Das haelt kleine und grosse Perioden
    // mit zwoelf Bit im Bereich.
    periodUs = (uint16_t)((value & 0x01FF) << ((value >> 9) & 0x07));
    return true;
}

uint16_t DShotFan::periodToRpm(uint16_t periodUs, uint8_t motorPoles)
{
    // Periode 0 gibt es nicht; steht der Motor, sendet der Regler die groesstmoegliche Periode.
    if (periodUs == 0) return 0;

    const uint8_t polePairs = (motorPoles < 2) ? 1 : (uint8_t)(motorPoles / 2);

    // Die Periode ist die Dauer einer elektrischen Umdrehung. Eine Minute hat 60e6 us, also
    // eRPM = 60e6 / Periode; mechanisch dreht der Motor einmal je Polpaar.
    const uint32_t erpm = 60000000UL / (uint32_t)periodUs;
    const uint32_t rpm = erpm / polePairs;

    return (rpm > 0xFFFF) ? 0xFFFF : (uint16_t)rpm;
}

uint16_t DShotFan::throttleFor(Fan::Direction dir, uint8_t speedPercent) const
{
    if (speedPercent == 0) return 0; // Stillstand. 1..47 waeren Sonderbefehle.
    if (speedPercent > 100) speedPercent = 100;

    // Der 3D-Modus legt die beiden Richtungen in die beiden Haelften des Stellbereichs. Damit
    // braucht DShot die Mittelstellung nicht, mit der PwmFan die Richtung kodiert.
    const uint16_t lo = (dir == Fan::Direction::B) ? ThrottleBMin : ThrottleAMin;
    const uint16_t hi = (dir == Fan::Direction::B) ? ThrottleBMax : ThrottleAMax;

    return (uint16_t)(lo + (uint32_t)(hi - lo) * speedPercent / 100);
}

// ===========================================================================
// Ansteuerung
// ===========================================================================

DShotFan::DShotFan(uint8_t pin, Speed speed, uint8_t motorPoles)
    : _pin(pin), _bitrate((uint32_t)speed), _motorPoles(motorPoles)
{
}

void DShotFan::setMidpoint(uint8_t percent)
{
    // Bewusst ohne Wirkung: bei DShot steckt die Richtung im Stellwert, nicht im Pegel. Der
    // Parameter bleibt in der Schnittstelle, weil PwmFan ihn braucht - genau die Art
    // Unterschied, die eine Abstraktion tragen muss.
    (void)percent;
}

void DShotFan::drive(Fan::Direction dir, uint8_t speedPercent)
{
    // Nur merken. Gesendet wird zyklisch, siehe updateSpeedFeedback().
    _throttle = throttleFor(dir, speedPercent);
}

void DShotFan::stop()
{
    _throttle = 0;
}

// ===========================================================================
// PIO-Transport
//
// ACHTUNG: das Taktbudget ist nach Spezifikation gerechnet, aber NICHT auf Hardware verifiziert.
// Dafuer fehlen ein digitaler Regler und eine bidirektional nutzbare Leitung. Die Budgets stehen
// deshalb Befehl fuer Befehl im Kommentar, damit sie am Oszilloskop nachzupruefen sind.
// ===========================================================================

#ifdef ARDUINO_ARCH_RP2040

void DShotFan::begin()
{
    // Sendeprogramm, gebaut mit pio_encode_* statt als .pio-Datei: so steht das Taktbudget
    // unmittelbar neben dem Befehl, der es verbraucht.
    //
    // Der Ausgang haengt am Side-Set, ein Pegelwechsel kostet damit keinen Takt. Bidirektionales
    // DShot ist invertiert: die Leitung ruht HIGH, ein Bit beginnt LOW.
    //
    // 16 Takte je Bit. Ein Einsbit ist 12 Takte LOW (0,750 T), ein Nullbit 6 (0,375 T) - genau
    // die Werte der Spezifikation:
    //
    //   0: out x, 1     side 0        1 Takt  LOW   Bitbeginn, Bit aus dem Schieberegister
    //   1: jmp !x, 4    side 0        1 Takt  LOW   -> 2
    //   2: nop          side 0 [9]   10 Takte LOW   -> 12   Einsbit fertig
    //   3: jmp 0        side 1 [3]    4 Takte HIGH  -> 16
    //   4: nop          side 0 [3]    4 Takte LOW   -> 6    Nullbit fertig
    //   5: jmp 0        side 1 [9]   10 Takte HIGH  -> 16
    //
    // Laeuft die Warteschlange leer, blockiert der out-Befehl und das Side-Set haelt den letzten
    // Pegel: die Leitung ruht HIGH, wie der invertierte Betrieb es verlangt.
    static const uint16_t txProgram[] = {
        (uint16_t)(pio_encode_out(pio_x, 1) | pio_encode_sideset(1, 0)),
        (uint16_t)(pio_encode_jmp_not_x(4) | pio_encode_sideset(1, 0)),
        (uint16_t)(pio_encode_nop() | pio_encode_sideset(1, 0) | pio_encode_delay(9)),
        (uint16_t)(pio_encode_jmp(0) | pio_encode_sideset(1, 1) | pio_encode_delay(3)),
        (uint16_t)(pio_encode_nop() | pio_encode_sideset(1, 0) | pio_encode_delay(3)),
        (uint16_t)(pio_encode_jmp(0) | pio_encode_sideset(1, 1) | pio_encode_delay(9)),
    };
    static const pio_program_t txDescriptor = {txProgram, (uint8_t)(sizeof(txProgram) / 2), -1};

    // Empfangsprogramm. Der Regler antwortet rund 30 us nach dem Rahmen mit 21 Bit bei 5/4 der
    // Sendebitrate. Zwei Takte je Bit, damit in der Bitmitte abgetastet wird:
    //
    //   0: wait 0 pin 0             auf die erste fallende Flanke warten
    //   1: nop                      halbes Bit versetzen
    //   2: in pins, 1               abtasten
    //   3: jmp x--, 2               21 mal
    //   4: push                     Rohwert abliefern
    static const uint16_t rxProgram[] = {
        (uint16_t)pio_encode_wait_pin(0, 0),
        (uint16_t)pio_encode_nop(),
        (uint16_t)pio_encode_in(pio_pins, 1),
        (uint16_t)pio_encode_jmp_x_dec(2),
        (uint16_t)pio_encode_push(false, true),
    };
    static const pio_program_t rxDescriptor = {rxProgram, (uint8_t)(sizeof(rxProgram) / 2), -1};

    _pio = pio0;

    const int smTx = pio_claim_unused_sm(_pio, false);
    const int smRx = pio_claim_unused_sm(_pio, false);
    if (smTx < 0 || smRx < 0) return; // kein PIO-Platz frei, Klasse bleibt inaktiv
    _smTx = smTx;
    _smRx = smRx;

    _offsetTx = pio_add_program(_pio, &txDescriptor);
    _offsetRx = pio_add_program(_pio, &rxDescriptor);

    pio_gpio_init(_pio, _pin);
    gpio_pull_up(_pin); // die ruhende Leitung muss HIGH sein, auch wenn niemand treibt

    // Senden: 16 Takte je Bit.
    pio_sm_config cTx = pio_get_default_sm_config();
    sm_config_set_sideset(&cTx, 1, false, true);
    sm_config_set_sideset_pins(&cTx, _pin);
    sm_config_set_out_shift(&cTx, false, true, 16); // MSB zuerst, Autopull nach 16 Bit
    sm_config_set_clkdiv(&cTx, (float)clock_get_hz(clk_sys) / (float)(_bitrate * 16));
    sm_config_set_wrap(&cTx, _offsetTx, _offsetTx + 5);
    pio_sm_init(_pio, _smTx, _offsetTx, &cTx);

    // Empfangen: 2 Takte je Bit bei 5/4 der Sendebitrate.
    pio_sm_config cRx = pio_get_default_sm_config();
    sm_config_set_in_pins(&cRx, _pin);
    sm_config_set_in_shift(&cRx, false, false, 21); // MSB zuerst, kein Autopush
    sm_config_set_clkdiv(&cRx, (float)clock_get_hz(clk_sys) / (float)(_bitrate * 5 / 4 * 2));
    pio_sm_init(_pio, _smRx, _offsetRx, &cRx);

    pio_sm_set_enabled(_pio, _smTx, true);
    _ready = true;

    stop();
}

void DShotFan::sendFrame(uint16_t frame)
{
    // Der Sende-Automat besitzt den Pin, solange er laeuft. Der Rahmen liegt links im Wort,
    // weil links geschoben wird.
    pio_sm_set_consecutive_pindirs(_pio, _smTx, _pin, 1, true);
    pio_sm_put_blocking(_pio, _smTx, (uint32_t)frame << 16);
}

bool DShotFan::receiveTelemetry(uint32_t &raw21)
{
    // Leitung freigeben und dem Regler ueberlassen. Der Pullup haelt sie HIGH, bis er zieht.
    pio_sm_set_enabled(_pio, _smTx, false);
    pio_sm_set_consecutive_pindirs(_pio, _smRx, _pin, 1, false);

    pio_sm_clear_fifos(_pio, _smRx);
    pio_sm_restart(_pio, _smRx);
    pio_sm_exec(_pio, _smRx, pio_encode_set(pio_x, 20)); // 21 Bit zaehlen
    pio_sm_exec(_pio, _smRx, pio_encode_jmp(_offsetRx));
    pio_sm_set_enabled(_pio, _smRx, true);

    // 21 Bit bei 5/4 der Bitrate plus Wendezeit, mit Reserve. Danach aufgeben: ein fehlender
    // oder stummer Regler darf die Kanallogik nicht anhalten.
    const uint32_t timeoutUs = 30 + (21 * 4 * 1000000UL) / (_bitrate * 5) + 50;
    const uint32_t start = micros();
    bool got = false;
    while ((uint32_t)(micros() - start) < timeoutUs)
    {
        if (!pio_sm_is_rx_fifo_empty(_pio, _smRx))
        {
            raw21 = pio_sm_get(_pio, _smRx) & 0x001FFFFF;
            got = true;
            break;
        }
    }

    pio_sm_set_enabled(_pio, _smRx, false);
    pio_sm_set_enabled(_pio, _smTx, true);
    return got;
}

void DShotFan::beginSpeedFeedback()
{
    // Nichts zu tun: Senden und Empfangen laufen im selben Kontext wie updateSpeedFeedback(),
    // es gibt keinen Interrupt zu registrieren.
}

void DShotFan::updateSpeedFeedback()
{
    if (!_ready) return;

    // Zyklisch senden, sonst laeuft der Regler in seinen eigenen Timeout und stellt ab. Das ist
    // der Grund, warum diese Klasse einen periodischen Tick braucht und PwmFan nicht.
    const uint32_t now = micros();
    if ((uint32_t)(now - _lastFrameUs) < FramePeriodUs) return;
    _lastFrameUs = now;

    sendFrame(buildFrame(_throttle, false, true));

    uint32_t raw = 0;
    if (!receiveTelemetry(raw)) return;

    uint16_t periodUs = 0;
    if (!decodeTelemetry(raw, periodUs)) return; // CRC- oder GCR-Fehler, Wert verwerfen

    _rpm = periodToRpm(periodUs, _motorPoles);

    // Zaehlt jede gueltige Antwort mit Drehung. Die Blockiererkennung wertet nur die Zunahme
    // aus, deshalb darf der Zaehler bei stehendem Rotor nicht weiterlaufen.
    if (_rpm > 0) _telemetryFrames++;
}

#else // Plattform ohne PIO-Block

void DShotFan::begin() {}
void DShotFan::beginSpeedFeedback() {}
void DShotFan::updateSpeedFeedback() {}
void DShotFan::sendFrame(uint16_t) {}
bool DShotFan::receiveTelemetry(uint32_t &) { return false; }

#endif
