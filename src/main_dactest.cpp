// ============================================================================
// test_dac — Messfirmware fuer Phase 1 (PLAN Phase 1, Referenzdesign 6.1/6.2/6.4)
//
// Diese Firmware ist absichtlich allein: kein KNX, kein OGM-Common, keine
// Kanallogik. Am Tag der Lieferung steht zwischen dir und dem Multimeter genau
// eine Datei, und wenn etwas nicht geht, liegt es an der Platine oder an dieser
// Datei - nicht an einem Modul, das drei Ebenen tiefer etwas anderes tut.
//
// SIE SCHREIBT NICHTS VON SELBST. Nach dem Start liegen an den Ausgaengen die
// Werte, die der DAC aus seinem EEPROM geladen hat. Jeder Schreibvorgang kommt
// aus einem Kommando, das du eingibst.
//
// Messungen, fuer die sie da ist:
//   P6  raw 58 0 4000 / raw 58 0 8000, S1 messen -> FANDRV_DAC_LEFT_ALIGNED
//   P7  raw <a> <c> 0 und 7fff auf jedem Kanal   -> Skalenfehler je Kanal
//   P10 store, 12 V aus/ein, ohne I2C messen     -> EEPROM haelt den Wert
//   P11 wie P10, Register 0x01 NICHT setzen      -> Bereichsbit mitgesichert
//   P15 store mit Logikanalysator beidseitig     -> ADuM1250 traegt die Sequenz
//
// Bauen:  pio run -e test_dac
// ============================================================================

#include <Arduino.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>

#include "hardware.h"

namespace
{
    // --- Register des GP8413 (Referenzdesign 3.3) ---------------------------
    constexpr uint8_t kRegRange = 0x01;
    constexpr uint8_t kRegVout0 = 0x02; ///< mit vier Datenbytes auch VOUT1
    constexpr uint8_t kRegVout1 = 0x04;
    constexpr uint8_t kRange0to10V = 0x11;

    // --- Store-Sequenz (DFRobot_GP8XXX, Referenzdesign 2.4 und 6.4) ---------
    constexpr uint8_t kStoreHead = 0x02;  ///< drei Bit 0b010, ohne ACK
    constexpr uint8_t kStoreAddr = 0x10;  ///< reservierte Adresse
    constexpr uint8_t kStoreCmd1 = 0x03;
    constexpr uint8_t kStoreCmd2 = 0x00;
    constexpr uint32_t kStoreDelayMs = 10;

    // Bitbang-Zeiten aus der DFRobot-Bibliothek: 1 us vor, 2 us nach der Flanke,
    // 5 us Zyklus - rund 125 kHz.
    constexpr uint32_t kBbBefore = 1;
    constexpr uint32_t kBbAfter = 2;
    constexpr uint32_t kBbCycle = 5;

    constexpr uint32_t kI2cHz = 100000; ///< langsam anfangen, spaeter 400 kHz

    /// Zaehlt die Store-Vorgaenge dieser Sitzung. Die Schreibzyklen des Chips
    /// sind unbekannt; jeder Store ist einer davon.
    uint16_t gStoreCount = 0;

    char gLine[80];
    uint8_t gLineLen = 0;

    /// Wartet auf die Bestaetigung eines store.
    bool gAwaitStoreConfirm = false;
    uint8_t gPendingStoreAddr = 0;

    // ---------------------------------------------------------------- I2C

    void i2cBegin()
    {
        i2c_init(FANDRV_I2C_PORT, kI2cHz);
        gpio_set_function(FANDRV_I2C_SDA, GPIO_FUNC_I2C);
        gpio_set_function(FANDRV_I2C_SCL, GPIO_FUNC_I2C);
        // Keine internen Pull-ups: R4/R5 sitzen auf der Platine.
    }

    bool i2cWrite(uint8_t addr, const uint8_t* data, uint8_t len)
    {
        return i2c_write_blocking(FANDRV_I2C_PORT, addr, data, len, false) == (int)len;
    }

    bool dacPresent(uint8_t addr)
    {
        uint8_t dummy = 0;
        return i2c_read_blocking(FANDRV_I2C_PORT, addr, &dummy, 1, false) >= 0;
    }

    bool dacRange(uint8_t addr)
    {
        const uint8_t buf[2] = {kRegRange, kRange0to10V};
        return i2cWrite(addr, buf, sizeof(buf));
    }

    bool dacRaw(uint8_t addr, uint8_t channel, uint16_t wire)
    {
        uint8_t buf[3];
        buf[0] = (channel == 0) ? kRegVout0 : kRegVout1;
        buf[1] = (uint8_t)(wire & 0xFF); // Low-Byte zuerst
        buf[2] = (uint8_t)(wire >> 8);
        return i2cWrite(addr, buf, sizeof(buf));
    }

    bool dacBoth(uint8_t addr, uint16_t wire0, uint16_t wire1)
    {
        uint8_t buf[5];
        buf[0] = kRegVout0;
        buf[1] = (uint8_t)(wire0 & 0xFF);
        buf[2] = (uint8_t)(wire0 >> 8);
        buf[3] = (uint8_t)(wire1 & 0xFF);
        buf[4] = (uint8_t)(wire1 >> 8);
        return i2cWrite(addr, buf, sizeof(buf));
    }

    // ---------------------------------------------------------------- Bitbang
    //
    // SDA und SCL werden als Open Drain gefuehrt: Low heisst Ausgang auf 0, High
    // heisst Eingang. Ein aktives High wuerde gegen die Pull-ups des Isolators
    // treiben. Die DFRobot-Referenz schreibt dort digitalWrite(HIGH), weil der
    // Arduino-Treiber schwach ist - das ist kein Vorbild, sondern ein Notbehelf.

    inline void bbRelease(uint8_t pin) { gpio_set_dir(pin, GPIO_IN); }
    inline void bbPullLow(uint8_t pin)
    {
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);
    }

    inline void bbSda(bool high) { high ? bbRelease(FANDRV_I2C_SDA) : bbPullLow(FANDRV_I2C_SDA); }
    inline void bbScl(bool high) { high ? bbRelease(FANDRV_I2C_SCL) : bbPullLow(FANDRV_I2C_SCL); }

    void bbPins()
    {
        i2c_deinit(FANDRV_I2C_PORT);
        gpio_set_function(FANDRV_I2C_SDA, GPIO_FUNC_SIO);
        gpio_set_function(FANDRV_I2C_SCL, GPIO_FUNC_SIO);
        gpio_put(FANDRV_I2C_SDA, 0);
        gpio_put(FANDRV_I2C_SCL, 0);
        bbRelease(FANDRV_I2C_SDA);
        bbRelease(FANDRV_I2C_SCL);
        delayMicroseconds(kBbCycle);
    }

    void bbPinsBack()
    {
        gpio_set_function(FANDRV_I2C_SDA, GPIO_FUNC_I2C);
        gpio_set_function(FANDRV_I2C_SCL, GPIO_FUNC_I2C);
        i2c_init(FANDRV_I2C_PORT, kI2cHz);
    }

    void bbStart()
    {
        bbSda(true);
        bbScl(true);
        delayMicroseconds(kBbCycle);
        bbSda(false);
        delayMicroseconds(kBbCycle);
        bbScl(false);
        delayMicroseconds(kBbCycle);
    }

    void bbStop()
    {
        bbSda(false);
        delayMicroseconds(kBbBefore);
        bbScl(true);
        delayMicroseconds(kBbCycle);
        bbSda(true);
        delayMicroseconds(kBbCycle);
    }

    /// Ein Bit hinausschieben. Kein ACK, kein Lesen.
    void bbBit(bool value)
    {
        bbSda(value);
        delayMicroseconds(kBbBefore);
        bbScl(true);
        delayMicroseconds(kBbCycle);
        bbScl(false);
        delayMicroseconds(kBbAfter);
    }

    /// `count` Bits aus `value`, hoechstwertiges zuerst, ohne ACK.
    void bbBits(uint8_t value, uint8_t count)
    {
        for (int8_t i = count - 1; i >= 0; i--)
            bbBit((value >> i) & 1);
    }

    /// Ein Byte mit ACK-Takt. Das ACK wird getaktet, aber nicht ausgewertet -
    /// die Sequenz laeuft ohnehin blind.
    void bbByte(uint8_t value)
    {
        bbBits(value, 8);
        bbSda(true); // SDA freigeben, damit der Chip quittieren kann
        delayMicroseconds(kBbBefore);
        bbScl(true);
        delayMicroseconds(kBbCycle);
        bbScl(false);
        delayMicroseconds(kBbAfter);
    }

    /// Die Store-Sequenz fuer EINEN Chip. Nie verschachtelt fuer zwei.
    void storeSequence(uint8_t addr)
    {
        bbPins();

        bbStart();
        bbBits(kStoreHead, 3); // 0b010, ohne ACK
        bbStop();

        bbStart();
        bbByte(kStoreAddr);
        bbByte(kStoreCmd1);
        bbStop();

        bbStart();
        bbByte((uint8_t)(addr << 1));
        for (uint8_t i = 0; i < 8; i++)
            bbByte(0x00);
        bbStop();

        delay(kStoreDelayMs);

        bbStart();
        bbBits(kStoreHead, 3);
        bbStop();

        bbStart();
        bbByte(kStoreAddr);
        bbByte(kStoreCmd2);
        bbStop();

        bbPinsBack();
        gStoreCount++;
    }

    // ---------------------------------------------------------------- Konsole

    void banner()
    {
        Serial.println();
        Serial.println(F("=== test_dac - Messfirmware Phase 1 ==============================="));
        Serial.print(F("Platine "));
        Serial.print(FANDRV_BOARD_ID);
        Serial.print(F(", "));
        Serial.print(FANDRV_BOARD_CHANNELS);
        Serial.print(F(" Kanaele, I2C auf GPIO"));
        Serial.print(FANDRV_I2C_SDA);
        Serial.print(F("/GPIO"));
        Serial.println(FANDRV_I2C_SCL);
        Serial.println(F("Diese Firmware schreibt nichts von selbst."));
        Serial.println(F("An den Ausgaengen liegt, was der DAC aus dem EEPROM geladen hat."));
        Serial.println(F("'help' zeigt die Befehle."));
        Serial.println();
    }

    void help()
    {
        Serial.println(F("probe              Adressen 0x58..0x5F abfragen"));
        Serial.println(F("range <a>          Register 0x01 = 0x11 (Bereich 0-10 V)"));
        Serial.println(F("raw <a> <c> <hex>  Rohwert auf Kanal c (0 oder 1) - fuer P6 und P7"));
        Serial.println(F("both <a> <h0> <h1> beide Kanaele in EINEM Frame - wie beim ego"));
        Serial.println(F("mid <a> <0|1>      5,00 V annehmen: 0 -> 0x4000, 1 -> 0x8000"));
        Serial.println(F("store <a>          Einschaltwert ins EEPROM - Rueckfrage mit y"));
        Serial.println();
        Serial.println(F("Adressen und Werte hexadezimal, ohne 0x: 'raw 58 0 4000'"));
        Serial.println(F("P6: raw 58 0 4000 messen, dann raw 58 0 8000 messen."));
        Serial.println(F("    Genau einer der beiden ergibt 5,00 V - das ist das Busformat."));
    }

    void cmdProbe()
    {
        Serial.println(F("Adressabfrage 0x58..0x5F:"));
        uint8_t found = 0;
        for (uint8_t a = 0x58; a <= 0x5F; a++)
        {
            if (dacPresent(a))
            {
                Serial.print(F("  0x"));
                Serial.print(a, HEX);
                Serial.println(F("  antwortet"));
                found++;
            }
        }
        if (found == 0)
            Serial.println(F("  niemand antwortet - 12 V da? Isolator versorgt?"));
    }

    /// Hexzahl ohne 0x. Gibt false zurueck, wenn nichts Sinnvolles dasteht.
    bool parseHex(const char* text, uint32_t& value)
    {
        if (text == nullptr || *text == 0)
            return false;
        char* end = nullptr;
        value = strtoul(text, &end, 16);
        return end != text;
    }

    void handleStoreConfirm(const char* line)
    {
        gAwaitStoreConfirm = false;
        if (line[0] != 'y' || line[1] != 0)
        {
            Serial.println(F("abgebrochen."));
            return;
        }

        Serial.print(F("store an 0x"));
        Serial.print(gPendingStoreAddr, HEX);
        Serial.println(F(" laeuft..."));
        storeSequence(gPendingStoreAddr);
        Serial.print(F("fertig. Store-Vorgaenge in dieser Sitzung: "));
        Serial.println(gStoreCount);
        Serial.println(F("Die Schreibzyklen des Chips sind unbekannt - sparsam damit."));
    }

    void execute(char* line)
    {
        if (gAwaitStoreConfirm)
        {
            handleStoreConfirm(line);
            return;
        }

        char* cmd = strtok(line, " ");
        if (cmd == nullptr)
            return;

        if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
        {
            help();
            return;
        }

        if (strcmp(cmd, "probe") == 0)
        {
            cmdProbe();
            return;
        }

        uint32_t a = 0, b = 0, c = 0;

        if (strcmp(cmd, "range") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a))
            {
                Serial.println(F("range <adresse>"));
                return;
            }
            Serial.println(dacRange((uint8_t)a) ? F("Bereich 0-10 V gesetzt")
                                               : F("kein ACK"));
            return;
        }

        if (strcmp(cmd, "raw") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b) ||
                !parseHex(strtok(nullptr, " "), c))
            {
                Serial.println(F("raw <adresse> <kanal 0|1> <hexwert>"));
                return;
            }
            const bool ok = dacRaw((uint8_t)a, (uint8_t)b, (uint16_t)c);
            Serial.print(ok ? F("geschrieben: 0x") : F("kein ACK: 0x"));
            Serial.print((uint16_t)c, HEX);
            Serial.print(F(" auf 0x"));
            Serial.print((uint8_t)a, HEX);
            Serial.print(F(" Kanal "));
            Serial.println((uint8_t)b);
            return;
        }

        if (strcmp(cmd, "both") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b) ||
                !parseHex(strtok(nullptr, " "), c))
            {
                Serial.println(F("both <adresse> <hex kanal0> <hex kanal1>"));
                return;
            }
            Serial.println(dacBoth((uint8_t)a, (uint16_t)b, (uint16_t)c)
                               ? F("Doppelschreiben abgesetzt")
                               : F("kein ACK"));
            return;
        }

        if (strcmp(cmd, "mid") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b))
            {
                Serial.println(F("mid <adresse> <0 rechtsbuendig | 1 linksbuendig>"));
                return;
            }
            const uint16_t wire = (b == 0) ? 0x4000 : 0x8000;
            dacRaw((uint8_t)a, 0, wire);
            dacRaw((uint8_t)a, 1, wire);
            Serial.print(F("beide Kanaele auf 0x"));
            Serial.println(wire, HEX);
            return;
        }

        if (strcmp(cmd, "store") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a))
            {
                Serial.println(F("store <adresse>"));
                return;
            }
            gPendingStoreAddr = (uint8_t)a;
            gAwaitStoreConfirm = true;
            Serial.println();
            Serial.println(F("store schreibt den JETZIGEN Ausgangszustand ins EEPROM."));
            Serial.println(F("Die Sequenz ist kein normales I2C: sie bangt GPIO2/GPIO3 und"));
            Serial.println(F("sendet an die reservierte Adresse 0x10. Frontpanel J110 vorher"));
            Serial.println(F("abziehen. Die Schreibzyklen des Chips sind unbekannt."));
            Serial.print(F("Wirklich speichern an 0x"));
            Serial.print(gPendingStoreAddr, HEX);
            Serial.println(F("?  y = ja, alles andere bricht ab"));
            return;
        }

        Serial.print(F("unbekannt: "));
        Serial.println(cmd);
    }

    void pollConsole()
    {
        while (Serial.available() > 0)
        {
            const char ch = (char)Serial.read();
            if (ch == '\r')
                continue;
            if (ch == '\n')
            {
                gLine[gLineLen] = 0;
                if (gLineLen > 0 || gAwaitStoreConfirm)
                    execute(gLine);
                gLineLen = 0;
                continue;
            }
            if (gLineLen < sizeof(gLine) - 1)
                gLine[gLineLen++] = ch;
        }
    }
} // namespace

void setup()
{
    Serial.begin(115200);
    // Auf die serielle Verbindung warten, aber nicht ewig: die Platine soll auch
    // ohne Terminal starten, etwa beim Kaltstarttest P10.
    const uint32_t until = millis() + 3000;
    while (!Serial && (int32_t)(millis() - until) < 0)
        delay(10);

    i2cBegin();
    banner();
}

void loop()
{
    pollConsole();
}
