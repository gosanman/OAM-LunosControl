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
// Fehlersuche Befund B1 (store verstellt den Nachbarchip), nur Platine 1:
//   F2  rd / rdreg                               -> laesst sich etwas zuruecklesen?
//   F3  modetest                                 -> verstellt schon der Modus allein?
//   E1..E5 store / storep / e2 / e5              -> Rettungsversuch U3, abgeschlossen
//
// SPERRE: Alle Befehle, die den Speichermodus betreten (store, storep, e2, e5,
// modetest), sind seit 2026-09-25 gesperrt. Ein Store verstellt den nicht
// adressierten Chip dauerhaft (Messprotokoll Phase 1, Befund B1). Freischalten
// nur fuer Versuche an Platine 1: bauen mit -D FANDRV_TESTDAC_ALLOW_STORE=1.
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

    /// Zaehlt die Versuche F3 (Speichermodus ohne Frame 3). Ob sie einen
    /// Schreibzyklus kosten, ist unbekannt - daher eigener Zaehler.
    uint16_t gModeOnlyCount = 0;

    constexpr uint8_t kReadMax = 16;

    char gLine[80];
    uint8_t gLineLen = 0;

    /// Was nach der Rueckfrage ausgefuehrt wird.
    enum class Pending : uint8_t
    {
        None,
        Store,
        ModeOnly,
        StorePayload,
        Scan,
        Scan2,
        E2,
    };
    uint8_t gE2Step = 0;

    /// E5: vor dem Store 0x06..0x0F auf U3 mit einem Muster fuellen. 0 = aus (E2).
    bool gE2Shadow = false;
    uint8_t gE2ShadowPattern = 0;
    Pending gPending = Pending::None;
    uint8_t gPendingStoreAddr = 0;
    uint8_t gPendingPayload = 0;

    constexpr uint8_t kWriteMax = 16;

    // --- Versuch E3, automatisiert: Registerscan mit Messpause ------------
    // Schritt 0 ist die Referenz ohne Schreibzugriff. Danach je Block 16 Bytes
    // ab 0x05, 0x15, ... 0xF5 (der letzte Block endet bei 0xFF), erst 0xFF, dann
    // 0x00. Zum Schluss die Einzelregister 0x00 und 0x03. 0x01/0x02/0x04 sind
    // bekannt und bleiben aus.
    constexpr uint8_t kScanBlocks = 16;
    constexpr uint8_t kScanSteps = 1 + kScanBlocks * 2 + 4;
    uint8_t gScanAddr = 0;
    uint8_t gScanStep = 0;

    // --- Versuch E3b: welcher Vorzustand macht den Ausgang instabil? --------
    // Beobachtung 25.09.2026: 0x06..0x0F <- ff liess S3 zwischen 0 und 12 V
    // pendeln, wenn vorher alle 16 Register 00 waren, Bereich 0x11 und Kanal 0
    // auf 0x0000 stand. Mit Kanal 0 auf 0x8000 nach Kaltstart blieb S3 stabil.
    // Die Schritte stellen den Zustand nach und variieren je EIN Element.
    uint8_t gScan2Step = 0;

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

    /// Versuch E3: beliebiges Register mit n Bytes beschreiben - normales I2C,
    /// fluechtig, ein Kaltstart setzt alles zurueck. Kein EEPROM-Zyklus.
    bool dacWriteRegs(uint8_t addr, uint8_t reg, const uint8_t* data, uint8_t n)
    {
        uint8_t buf[1 + kWriteMax];
        buf[0] = reg;
        for (uint8_t i = 0; i < n; i++)
            buf[1 + i] = data[i];
        return i2cWrite(addr, buf, (uint8_t)(1 + n));
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

    void printBytes(const uint8_t* data, int len)
    {
        for (int i = 0; i < len; i++)
        {
            Serial.print(' ');
            if (data[i] < 0x10)
                Serial.print('0');
            Serial.print(data[i], HEX);
        }
        Serial.println();
    }

    /// Versuch F2, ohne Risiko: reiner Lesezugriff, kein einziges Byte wird
    /// geschrieben.
    void cmdRead(uint8_t addr, uint8_t n)
    {
        uint8_t buf[kReadMax] = {0};
        const int got = i2c_read_blocking(FANDRV_I2C_PORT, addr, buf, n, false);
        if (got < 0)
        {
            Serial.println(F("kein ACK"));
            return;
        }
        Serial.print(F("0x"));
        Serial.print(addr, HEX);
        Serial.print(F(" liest:"));
        printBytes(buf, got);
    }

    /// Versuch F2, geringes Risiko: Registerzeiger schreiben (ein Byte, ohne
    /// Nutzdaten, ohne Stop), dann mit Repeated Start lesen. Was der GP8413 mit
    /// einem Schreibzugriff ohne Nutzdaten macht, ist nicht dokumentiert.
    void cmdReadReg(uint8_t addr, uint8_t reg, uint8_t n)
    {
        if (i2c_write_blocking(FANDRV_I2C_PORT, addr, &reg, 1, true) != 1)
        {
            Serial.println(F("kein ACK auf den Registerzeiger"));
            return;
        }
        uint8_t buf[kReadMax] = {0};
        const int got = i2c_read_blocking(FANDRV_I2C_PORT, addr, buf, n, false);
        if (got < 0)
        {
            Serial.println(F("Zeiger angenommen, aber kein ACK beim Lesen"));
            return;
        }
        Serial.print(F("0x"));
        Serial.print(addr, HEX);
        Serial.print(F(" Reg 0x"));
        Serial.print(reg, HEX);
        Serial.print(F(" liest:"));
        printBytes(buf, got);
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

    /// Frame 1 und 2: Speichermodus an. Richtet sich an ALLE Chips am Bus
    /// (Messprotokoll, Befund B1).
    void storeModeEnter()
    {
        bbStart();
        bbBits(kStoreHead, 3); // 0b010, ohne ACK
        bbStop();

        bbStart();
        bbByte(kStoreAddr);
        bbByte(kStoreCmd1);
        bbStop();
    }

    /// Frame 3: der einzige adressierte Frame. Datenblatt 3.3.6 und DFRobot
    /// zeichnen die ACK-Slots dieses Frames als 1 - kein Chip quittiert, der
    /// Frame geht als roher Bitstrom an jeden entsperrten Chip.
    /// `payload` ist im Normalfall 0x00 (DFRobot CMD2); Versuch E2 variiert es.
    void storeModeData(uint8_t addr, uint8_t payload = kStoreCmd2)
    {
        bbStart();
        bbByte((uint8_t)(addr << 1));
        for (uint8_t i = 0; i < 8; i++)
            bbByte(payload);
        bbStop();
    }

    /// Frame 4 und 5: Speichermodus aus. Ebenfalls an alle Chips.
    void storeModeExit()
    {
        bbStart();
        bbBits(kStoreHead, 3);
        bbStop();

        bbStart();
        bbByte(kStoreAddr);
        bbByte(kStoreCmd2);
        bbStop();
    }

    /// Die Store-Sequenz fuer EINEN Chip. Nie verschachtelt fuer zwei.
    /// Auf einem Bus mit zwei GP8413 verstellt sie den Nachbarchip (Befund B1).
    void storeSequence(uint8_t addr, uint8_t payload = kStoreCmd2)
    {
        bbPins();
        storeModeEnter();
        storeModeData(addr, payload);
        delay(kStoreDelayMs);
        storeModeExit();
        bbPinsBack();
        gStoreCount++;
    }

    /// Versuch F3: Speichermodus an und aus, OHNE Frame 3. Klaert, ob schon der
    /// Modus allein den Abgleich eines Chips verstellt.
    void modeOnlySequence()
    {
        bbPins();
        storeModeEnter();
        delay(kStoreDelayMs);
        storeModeExit();
        bbPinsBack();
        gModeOnlyCount++;
    }

    // ---------------------------------------------------------------- Scan E3

    /// Fuehrt Schritt `step` des Scans aus: Referenz herstellen, Block schreiben,
    /// Messaufforderung ausgeben.
    void scanApply(uint8_t step)
    {
        // Referenz vor jedem Schritt neu, falls ein Block den DAC-Wert mitgeschrieben hat.
        dacRange(gScanAddr);
        dacRaw(gScanAddr, 0, 0x8000);

        Serial.println();
        Serial.print(F("[Scan "));
        Serial.print(step + 1);
        Serial.print('/');
        Serial.print(kScanSteps);
        Serial.print(F("] "));

        if (step == 0)
        {
            Serial.println(F("Referenz: nur range + 0x8000 auf Kanal 0, nichts sonst."));
        }
        else if (step <= kScanBlocks * 2)
        {
            const uint8_t block = (uint8_t)((step - 1) / 2);
            const uint8_t pattern = ((step - 1) % 2 == 0) ? 0xFF : 0x00;
            const uint8_t reg = (uint8_t)(0x05 + block * 0x10);
            const uint8_t n = (uint8_t)((0x100 - reg) < kWriteMax ? (0x100 - reg) : kWriteMax);
            uint8_t data[kWriteMax];
            for (uint8_t i = 0; i < n; i++)
                data[i] = pattern;
            const bool ok = dacWriteRegs(gScanAddr, reg, data, n);
            Serial.print(F("Reg 0x"));
            Serial.print(reg, HEX);
            Serial.print(F("..0x"));
            Serial.print((uint8_t)(reg + n - 1), HEX);
            Serial.print(F(" <- "));
            Serial.print(n);
            Serial.print(F(" x 0x"));
            Serial.print(pattern, HEX);
            Serial.println(ok ? F("  (ACK)") : F("  (KEIN ACK)"));
        }
        else
        {
            const uint8_t k = (uint8_t)(step - 1 - kScanBlocks * 2); // 0..3
            const uint8_t reg = (k < 2) ? 0x00 : 0x03;
            const uint8_t pattern = (k % 2 == 0) ? 0xFF : 0x00;
            const bool ok = dacWriteRegs(gScanAddr, reg, &pattern, 1);
            Serial.print(F("Reg 0x"));
            Serial.print(reg, HEX);
            Serial.print(F(" <- 0x"));
            Serial.print(pattern, HEX);
            Serial.println(ok ? F("  (ACK)") : F("  (KEIN ACK)"));
        }

        Serial.println(F("S3 messen. Referenz ist der Wert aus Schritt 1."));
        Serial.println(F("Enter = weiter, r = Schritt wiederholen, x = abbrechen"));
    }

    // ---------------------------------------------------------------- Scan2 E3b

    /// Jeder Frame meldet sein ACK, danach 5 ms Pause. Ohne die Meldung war im
    /// ersten Durchlauf nicht zu sehen, ob der Chip die Frames ueberhaupt
    /// angenommen hat.
    constexpr uint32_t kS2GapMs = 5;

    void s2Report(bool ok, const __FlashStringHelper* what)
    {
        Serial.print(ok ? F("    ok      ") : F("    KEIN ACK "));
        Serial.println(what);
        delay(kS2GapMs);
    }

    void s2Range() { s2Report(dacRange(gScanAddr), F("range 0x11")); }

    void s2Raw(uint8_t ch, uint16_t v)
    {
        const bool ok = dacRaw(gScanAddr, ch, v);
        Serial.print(ok ? F("    ok      ") : F("    KEIN ACK "));
        Serial.print(F("Kanal "));
        Serial.print(ch);
        Serial.print(F(" <- 0x"));
        Serial.println(v, HEX);
        delay(kS2GapMs);
    }

    void s2Write(uint8_t reg, uint8_t value, uint8_t n)
    {
        uint8_t data[kWriteMax];
        for (uint8_t i = 0; i < n; i++)
            data[i] = value;
        const bool ok = dacWriteRegs(gScanAddr, reg, data, n);
        Serial.print(ok ? F("    ok      ") : F("    KEIN ACK "));
        Serial.print(F("Reg 0x"));
        Serial.print(reg, HEX);
        Serial.print(F(" <- "));
        Serial.print(n);
        Serial.print(F(" x 0x"));
        Serial.println(value, HEX);
        delay(kS2GapMs);
    }

    void s2Fill(uint8_t value) { s2Write(0x00, value, kWriteMax); }
    void s2Burst(uint8_t value) { s2Write(0x06, value, 10); }

    /// Grundzustand wie vor dem instabilen Ereignis: alle 16 Register 00,
    /// dann Bereich 0x11. Kanal 0 und 1 stehen danach auf 0x0000.
    void s2Ground()
    {
        s2Fill(0x00);
        s2Range();
    }

    struct Scan2Step
    {
        const char* text;
        void (*act)();
    };

    const Scan2Step kScan2[] = {
        {"Referenz: range + Kanal 0 = 0x8000. Erwartet 5,53 V stabil.",
         [] { s2Range(); s2Raw(0, 0x8000); }},
        {"NACHSTELLEN: alle 16 Reg = 00, range, Kanal 0 = 0 - dann 0x06..0x0F <- ff. Instabil?",
         [] { s2Ground(); s2Burst(0xFF); }},
        {"Nur Kanal 0 = 0x8000 (kein range). Wieder stabil?",
         [] { s2Raw(0, 0x8000); }},
        {"Nur range. Aendert das etwas?",
         [] { s2Range(); }},
        {"Grundzustand (16 x 00, range) + Kanal 0 = 0x8000, OHNE Burst. Erwartet 5,53 V.",
         [] { s2Ground(); s2Raw(0, 0x8000); }},
        {"Dazu Burst 0x06..0x0F <- ff bei Kanal 0 = 0x8000. Ist Kanal 0 = 0 der Ausloeser?",
         [] { s2Burst(0xFF); }},
        {"Jetzt Kanal 0 = 0x0000 - Burst war schon drin. Reihenfolge egal?",
         [] { s2Raw(0, 0x0000); }},
        {"Grundzustand, Reg 0x00 <- ff, Kanal 0 = 0, Burst ff. Reg 0x00 beteiligt?",
         [] { s2Ground(); s2Write(0x00, 0xFF, 1); s2Burst(0xFF); }},
        {"Grundzustand, Kanal 1 = 0x8000, Kanal 0 = 0, Burst ff. Kanal 1 beteiligt?",
         [] { s2Ground(); s2Raw(1, 0x8000); s2Burst(0xFF); }},
        {"Grundzustand, Kanal 0 = 0, Burst nur 0x06..0x0A <- ff. Untere Haelfte?",
         [] { s2Ground(); s2Write(0x06, 0xFF, 5); }},
        {"Grundzustand, Kanal 0 = 0, Burst nur 0x0B..0x0F <- ff. Obere Haelfte?",
         [] { s2Ground(); s2Write(0x0B, 0xFF, 5); }},
        {"Grundzustand, Kanal 0 = 0, Burst 0x06..0x0F <- 55. Muster statt ff?",
         [] { s2Ground(); s2Burst(0x55); }},
        {"Aufraeumen: Grundzustand + Kanal 0 = 0x8000. Erwartet 5,53 V stabil.",
         [] { s2Ground(); s2Raw(0, 0x8000); }},
    };
    constexpr uint8_t kScan2Steps = sizeof(kScan2) / sizeof(kScan2[0]);

    void scan2Apply(uint8_t step)
    {
        Serial.println();
        Serial.print(F("[Scan2 "));
        Serial.print(step + 1);
        Serial.print('/');
        Serial.print(kScan2Steps);
        Serial.print(F("] "));
        Serial.println(kScan2[step].text);
        kScan2[step].act();
        Serial.println(F("S3 messen - stabil / instabil, Wert notieren."));
        Serial.println(F("Enter = weiter, r = Schritt wiederholen, x = abbrechen"));
    }

    void scan2Next()
    {
        if (gScan2Step + 1 >= kScan2Steps)
        {
            gPending = Pending::None;
            Serial.println();
            Serial.println(F("Scan2 beendet."));
            return;
        }
        gScan2Step++;
        scan2Apply(gScan2Step);
    }

    // ---------------------------------------------------------------- E2

    /// Beide Kanaele eines Chips setzen, mit ACK-Ausgabe.
    void e2Set(uint8_t addr, uint16_t v0, uint16_t v1)
    {
        gScanAddr = addr;
        s2Range();
        s2Raw(0, v0);
        s2Raw(1, v1);
    }

    constexpr uint8_t kE2StoreStep = 2; ///< 0-basiert: Schritt 3 ist der Store
    constexpr uint8_t kE2Steps = 9;

    void e2Apply(uint8_t step)
    {
        Serial.println();
        Serial.print(F("[E2 "));
        Serial.print(step + 1);
        Serial.print('/');
        Serial.print(kE2Steps);
        Serial.print(F("] "));
        switch (step)
        {
            case 0:
                Serial.println(F("U2 (0x58): range, beide Kanaele 0x8000. S1, S2 messen: 5,00 V?"));
                e2Set(FANDRV_DAC_ADDR_A, 0x8000, 0x8000);
                break;
            case 1:
                Serial.println(F("U3 (0x59): range, beide Kanaele 0x8000. S3, S4 messen: 5,53 / 5,55 V?"));
                e2Set(FANDRV_DAC_ADDR_B, 0x8000, 0x8000);
                if (gE2Shadow)
                {
                    Serial.println(F("  E5: dazu 0x06..0x0F auf U3 fuellen (Schattenspeicher-Hypothese)."));
                    s2Write(0x06, gE2ShadowPattern, 10);
                }
                break;
            case 2:
                Serial.print(F("STORE an 0x58 mit Nutzdaten 8 x 0x"));
                Serial.print(gPendingPayload, HEX);
                Serial.println(F(". Ein EEPROM-Zyklus. Kann beide Chips veraendern."));
                Serial.println(F("Nur wenn S1/S2 = 5,00 V gemessen sind.  'payload' = ja, alles andere bricht ab"));
                return; // kein Enter-Prompt, eigene Rueckfrage
            case 3:
                Serial.println(F("U3: 0x8000 / 0x8000. S3, S4 messen - noch 5,53 / 5,55 V?"));
                e2Set(FANDRV_DAC_ADDR_B, 0x8000, 0x8000);
                break;
            case 4:
                Serial.println(F("U3: 0xFFFE / 0xFFFE. S3, S4 messen - noch 11,06 / 11,10 V?"));
                e2Set(FANDRV_DAC_ADDR_B, 0xFFFE, 0xFFFE);
                break;
            case 5:
                Serial.println(F("U3: 0x0000 / 0x0000. S3, S4 messen - 0 V?"));
                e2Set(FANDRV_DAC_ADDR_B, 0x0000, 0x0000);
                break;
            case 6:
                Serial.println(F("U2: 0x8000 / 0x8000. S1, S2 messen - noch 5,00 V?"));
                e2Set(FANDRV_DAC_ADDR_A, 0x8000, 0x8000);
                break;
            case 7:
                Serial.println(F("U2: 0xFFFE / 0xFFFE. S1, S2 messen - noch 10,03 V?"));
                e2Set(FANDRV_DAC_ADDR_A, 0xFFFE, 0xFFFE);
                break;
            case 8:
                Serial.println(F("U2 zurueck auf 0x8000 / 0x8000, U3 auf 0x8000 / 0x8000."));
                e2Set(FANDRV_DAC_ADDR_A, 0x8000, 0x8000);
                e2Set(FANDRV_DAC_ADDR_B, 0x8000, 0x8000);
                Serial.println(F("Danach Kaltstart und S1..S4 ohne Befehl messen."));
                break;
        }
        Serial.println(F("Enter = weiter, r = Schritt wiederholen, x = abbrechen"));
    }

    void e2Next()
    {
        if (gE2Step + 1 >= kE2Steps)
        {
            gPending = Pending::None;
            Serial.println();
            Serial.println(F("E2 beendet."));
            return;
        }
        gE2Step++;
        e2Apply(gE2Step);
    }

    void scanNext()
    {
        if (gScanStep + 1 >= kScanSteps)
        {
            gPending = Pending::None;
            Serial.println();
            Serial.println(F("Scan beendet. Kaltstart setzt alle fluechtigen Register zurueck."));
            return;
        }
        gScanStep++;
        scanApply(gScanStep);
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
        Serial.println(F("                   VERSTELLT DEN NACHBARCHIP (Befund B1)"));
#ifndef FANDRV_TESTDAC_ALLOW_STORE
        Serial.println(F("                   store/storep/e2/e5/modetest sind GESPERRT"));
#endif
        Serial.println();
        Serial.println(F("Fehlersuche B1:"));
        Serial.println(F("rd <a> <n>         F2: n Bytes lesen, schreibt nichts"));
        Serial.println(F("rdreg <a> <r> <n>  F2: Registerzeiger r, dann n Bytes lesen"));
        Serial.println(F("modetest           F3: Speichermodus an/aus OHNE Frame 3 -"));
        Serial.println(F("                   trifft alle Chips, Rueckfrage mit 'modus'"));
        Serial.println(F("wr <a> <r> <b>..   E3: Register r fluechtig beschreiben (max 16 Bytes)"));
        Serial.println(F("wrn <a> <r> <b> <n> E3: n Kopien von b ab Register r"));
        Serial.println(F("scan <a>           E3 automatisch: 37 Schritte, nach jedem Enter"));
        Serial.println(F("scan2 <a> [s]      E3b: Vorzustand-Test, 13 Schritte, s = Einstieg"));
        Serial.println(F("storep <a> <b>     E2: store, Frame 3 mit 8 x b statt 8 x 00 -"));
        Serial.println(F("                   Rueckfrage mit 'payload'"));
        Serial.println(F("e2 <b>             E2 gefuehrt: 9 Schritte, Store an 0x58, U3 messen"));
        Serial.println(F("e5 <b>             E5 gefuehrt: wie e2, vorher 0x06..0x0F auf U3 <- b"));
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

    void handleConfirm(const char* line)
    {
        if (gPending == Pending::E2)
        {
            if (strcmp(line, "x") == 0)
            {
                gPending = Pending::None;
                Serial.println(F("E2 abgebrochen. Ausgaenge stehen, wie zuletzt gesetzt."));
                return;
            }
            if (gE2Step == kE2StoreStep)
            {
                if (strcmp(line, "payload") != 0)
                {
                    gPending = Pending::None;
                    Serial.println(F("E2 abgebrochen, kein Store ausgefuehrt."));
                    return;
                }
                Serial.println(F("store laeuft..."));
                storeSequence(FANDRV_DAC_ADDR_A, gPendingPayload);
                Serial.print(F("fertig. Store-Vorgaenge in dieser Sitzung: "));
                Serial.println(gStoreCount);
                e2Next();
                return;
            }
            if (strcmp(line, "r") == 0)
            {
                e2Apply(gE2Step);
                return;
            }
            if (line[0] != 0)
            {
                Serial.println(F("Enter = weiter, r = wiederholen, x = abbrechen"));
                return;
            }
            e2Next();
            return;
        }

        if (gPending == Pending::Scan || gPending == Pending::Scan2)
        {
            const bool two = (gPending == Pending::Scan2);
            if (strcmp(line, "x") == 0)
            {
                gPending = Pending::None;
                Serial.println(F("Scan abgebrochen. Kaltstart setzt alle fluechtigen Register zurueck."));
                return;
            }
            if (strcmp(line, "r") == 0)
            {
                two ? scan2Apply(gScan2Step) : scanApply(gScanStep);
                return;
            }
            if (line[0] != 0)
            {
                Serial.println(F("Enter = weiter, r = wiederholen, x = abbrechen"));
                return;
            }
            two ? scan2Next() : scanNext();
            return;
        }

        const Pending what = gPending;
        gPending = Pending::None;

        if (what == Pending::ModeOnly)
        {
            if (strcmp(line, "modus") != 0)
            {
                Serial.println(F("abgebrochen."));
                return;
            }
            Serial.println(F("Speichermodus an/aus ohne Frame 3 laeuft..."));
            modeOnlySequence();
            Serial.print(F("fertig. F3-Versuche in dieser Sitzung: "));
            Serial.println(gModeOnlyCount);
            Serial.println(F("Jetzt Verstaerkung beider Chips messen, dann Kaltstart."));
            return;
        }

        if (what == Pending::StorePayload)
        {
            if (strcmp(line, "payload") != 0)
            {
                Serial.println(F("abgebrochen."));
                return;
            }
            Serial.print(F("store an 0x"));
            Serial.print(gPendingStoreAddr, HEX);
            Serial.print(F(" mit Nutzdaten 8 x 0x"));
            Serial.print(gPendingPayload, HEX);
            Serial.println(F(" laeuft..."));
            storeSequence(gPendingStoreAddr, gPendingPayload);
            Serial.print(F("fertig. Store-Vorgaenge in dieser Sitzung: "));
            Serial.println(gStoreCount);
            Serial.println(F("Jetzt Verstaerkung und Nullpunkt beider Chips messen, dann Kaltstart."));
            return;
        }

        if (strcmp(line, "y") != 0)
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
        if (gPending != Pending::None)
        {
            handleConfirm(line);
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

#ifndef FANDRV_TESTDAC_ALLOW_STORE
        if (strcmp(cmd, "store") == 0 || strcmp(cmd, "storep") == 0 || strcmp(cmd, "e2") == 0 ||
            strcmp(cmd, "e5") == 0 || strcmp(cmd, "modetest") == 0)
        {
            Serial.println(F("GESPERRT. Befund B1 (Messprotokoll Phase 1, 2026-09-25): ein Store"));
            Serial.println(F("verstellt den nicht adressierten Chip am selben Bus dauerhaft."));
            Serial.println(F("Nur fuer Versuche an Platine 1: bauen mit -D FANDRV_TESTDAC_ALLOW_STORE=1"));
            return;
        }
#endif

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
            gPending = Pending::Store;
            Serial.println();
            Serial.println(F("ACHTUNG Befund B1: store verstellt den Nachbarchip am selben Bus"));
            Serial.println(F("dauerhaft. Nur auf der Versuchsplatine 1."));
            Serial.println(F("store schreibt den JETZIGEN Ausgangszustand ins EEPROM."));
            Serial.println(F("Die Sequenz ist kein normales I2C: sie bangt GPIO2/GPIO3 und"));
            Serial.println(F("sendet an die reservierte Adresse 0x10. Frontpanel J110 vorher"));
            Serial.println(F("abziehen. Die Schreibzyklen des Chips sind unbekannt."));
            Serial.print(F("Wirklich speichern an 0x"));
            Serial.print(gPendingStoreAddr, HEX);
            Serial.println(F("?  y = ja, alles andere bricht ab"));
            return;
        }

        if (strcmp(cmd, "rd") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b) ||
                b < 1 || b > kReadMax)
            {
                Serial.println(F("rd <adresse> <anzahl 1..10 hex>"));
                return;
            }
            cmdRead((uint8_t)a, (uint8_t)b);
            return;
        }

        if (strcmp(cmd, "rdreg") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b) ||
                !parseHex(strtok(nullptr, " "), c) || b > 0xFF || c < 1 || c > kReadMax)
            {
                Serial.println(F("rdreg <adresse> <register> <anzahl 1..10 hex>"));
                return;
            }
            cmdReadReg((uint8_t)a, (uint8_t)b, (uint8_t)c);
            return;
        }

        if (strcmp(cmd, "wr") == 0)
        {
            // wr <a> <r> <b0> [b1 .. b15]
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b) || b > 0xFF)
            {
                Serial.println(F("wr <adresse> <register> <byte> [byte ...]  (max 16)"));
                return;
            }
            uint8_t data[kWriteMax];
            uint8_t n = 0;
            while (n < kWriteMax && parseHex(strtok(nullptr, " "), c) && c <= 0xFF)
                data[n++] = (uint8_t)c;
            if (n == 0)
            {
                Serial.println(F("wr <adresse> <register> <byte> [byte ...]  (max 16)"));
                return;
            }
            const bool ok = dacWriteRegs((uint8_t)a, (uint8_t)b, data, n);
            Serial.print(ok ? F("geschrieben: 0x") : F("kein ACK: 0x"));
            Serial.print((uint8_t)a, HEX);
            Serial.print(F(" Reg 0x"));
            Serial.print((uint8_t)b, HEX);
            Serial.print(F(" <-"));
            printBytes(data, n);
            return;
        }

        if (strcmp(cmd, "wrn") == 0)
        {
            // wrn <a> <r> <byte> <n>: n Kopien von byte ab Register r
            uint32_t n = 0;
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b) ||
                !parseHex(strtok(nullptr, " "), c) || !parseHex(strtok(nullptr, " "), n) ||
                b > 0xFF || c > 0xFF || n < 1 || n > kWriteMax)
            {
                Serial.println(F("wrn <adresse> <register> <byte> <anzahl 1..10 hex>"));
                return;
            }
            uint8_t data[kWriteMax];
            for (uint8_t i = 0; i < n; i++)
                data[i] = (uint8_t)c;
            const bool ok = dacWriteRegs((uint8_t)a, (uint8_t)b, data, (uint8_t)n);
            Serial.print(ok ? F("geschrieben: 0x") : F("kein ACK: 0x"));
            Serial.print((uint8_t)a, HEX);
            Serial.print(F(" Reg 0x"));
            Serial.print((uint8_t)b, HEX);
            Serial.print(F(" <-"));
            printBytes(data, (int)n);
            return;
        }

        if (strcmp(cmd, "storep") == 0)
        {
            // storep <a> <payload>: wie store, aber Frame 3 mit 8 x <payload>
            if (!parseHex(strtok(nullptr, " "), a) || !parseHex(strtok(nullptr, " "), b) || b > 0xFF)
            {
                Serial.println(F("storep <adresse> <nutzdatenbyte>"));
                return;
            }
            gPendingStoreAddr = (uint8_t)a;
            gPendingPayload = (uint8_t)b;
            gPending = Pending::StorePayload;
            Serial.println();
            Serial.println(F("Versuch E2: Store mit veraendertem Nutzdatenbyte in Frame 3."));
            Serial.println(F("Der adressierte Chip speichert; was der Nachbar mit den Nutzdaten"));
            Serial.println(F("macht, ist die Frage. Kann BEIDE Chips veraendern. Nur Platine 1."));
            Serial.println(F("Vorher: beide Chips range, 0x8000 auf allen Kanaelen, gemessen."));
            Serial.print(F("Adresse 0x"));
            Serial.print(gPendingStoreAddr, HEX);
            Serial.print(F(", Nutzdaten 8 x 0x"));
            Serial.print(gPendingPayload, HEX);
            Serial.println(F(".  Ausfuehren?  'payload' = ja, alles andere bricht ab"));
            return;
        }

        if (strcmp(cmd, "scan") == 0)
        {
            if (!parseHex(strtok(nullptr, " "), a) || a > 0x7F)
            {
                Serial.println(F("scan <adresse>   - E3, gedacht fuer 59"));
                return;
            }
            gScanAddr = (uint8_t)a;
            gScanStep = 0;
            gPending = Pending::Scan;
            Serial.println();
            Serial.println(F("Versuch E3: fluechtiger Registerscan. Normales I2C, kein EEPROM."));
            Serial.println(F("Vor jedem Schritt: range + 0x8000 auf Kanal 0, dann ein Block."));
            if (gScanAddr == FANDRV_DAC_ADDR_A)
                Serial.println(F("ACHTUNG: das ist U2, der gesunde Chip. Fluechtig, aber unbekannt."));
            scanApply(0);
            return;
        }

        if (strcmp(cmd, "scan2") == 0)
        {
            // scan2 <a> [schritt]: Schritt zum Wiedereinstieg nach Kaltstart, 1-basiert
            uint32_t start = 1;
            if (!parseHex(strtok(nullptr, " "), a) || a > 0x7F)
            {
                Serial.println(F("scan2 <adresse> [schritt]   - E3b, gedacht fuer 59"));
                return;
            }
            const char* s = strtok(nullptr, " ");
            if (s != nullptr && (!parseHex(s, start) || start < 1 || start > kScan2Steps))
            {
                Serial.println(F("schritt ausserhalb 1..d (hex)"));
                return;
            }
            gScanAddr = (uint8_t)a;
            gScan2Step = (uint8_t)(start - 1);
            gPending = Pending::Scan2;
            Serial.println();
            Serial.println(F("Versuch E3b: welcher Vorzustand macht den Ausgang instabil?"));
            Serial.println(F("Normales I2C, kein EEPROM. Kaltstart setzt alles zurueck."));
            if (gScanAddr == FANDRV_DAC_ADDR_A)
                Serial.println(F("ACHTUNG: das ist U2, der gesunde Chip."));
            scan2Apply(gScan2Step);
            return;
        }

        if (strcmp(cmd, "e5") == 0)
        {
            // e5 <muster>: wie e2 mit Nutzdaten 00, aber vorher 0x06..0x0F auf U3 <- muster
            if (!parseHex(strtok(nullptr, " "), a) || a > 0xFF)
            {
                Serial.println(F("e5 <musterbyte>   - z.B. e5 40"));
                return;
            }
            gE2Shadow = true;
            gE2ShadowPattern = (uint8_t)a;
            gPendingPayload = kStoreCmd2;
            gE2Step = 0;
            gPending = Pending::E2;
            Serial.println();
            Serial.println(F("Versuch E5: liegt in 0x06..0x0F ein Schattenspeicher, den der"));
            Serial.println(F("fremdadressierte Frame in U3 ins EEPROM kopiert? Wirkung erst nach"));
            Serial.println(F("Store UND Kaltstart messbar. Store an 0x58 mit 00, wie in B1."));
            Serial.println(F("Nur Platine 1. Frontpanel J110 ab. Ein EEPROM-Zyklus."));
            e2Apply(0);
            return;
        }

        if (strcmp(cmd, "e2") == 0)
        {
            // e2 <nutzdatenbyte>: gefuehrter Ablauf, Store an U2 mit Nutzdaten, U3 beobachten
            if (!parseHex(strtok(nullptr, " "), a) || a > 0xFF)
            {
                Serial.println(F("e2 <nutzdatenbyte>   - z.B. e2 ff"));
                return;
            }
            gE2Shadow = false;
            gPendingPayload = (uint8_t)a;
            gE2Step = 0;
            gPending = Pending::E2;
            Serial.println();
            Serial.println(F("Versuch E2: sind die Nutzdaten von Frame 3 die Daten, die im"));
            Serial.println(F("Nachbarchip landen? Store an U2 (0x58), U3 (0x59) wird beobachtet."));
            Serial.println(F("Nur Platine 1. Frontpanel J110 ab. Ein EEPROM-Zyklus."));
            e2Apply(0);
            return;
        }

        if (strcmp(cmd, "modetest") == 0)
        {
            gPending = Pending::ModeOnly;
            Serial.println();
            Serial.println(F("Versuch F3: Frame 1+2 (Speichermodus an), 10 ms, Frame 4+5"));
            Serial.println(F("(Speichermodus aus) - OHNE den adressierten Frame 3."));
            Serial.println(F("Die Frames richten sich an ALLE Chips am Bus. Kann beide Chips"));
            Serial.println(F("verstellen. Nur auf der Versuchsplatine 1. Frontpanel J110 ab."));
            Serial.println(F("Vorher Verstaerkung beider Chips messen und notieren."));
            Serial.println(F("Ausfuehren?  'modus' = ja, alles andere bricht ab"));
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
                Serial.println(); // Echo: Zeile abschliessen, bevor die Antwort kommt
                gLine[gLineLen] = 0;
                if (gLineLen > 0 || gPending != Pending::None)
                    execute(gLine);
                gLineLen = 0;
                continue;
            }
            if (ch == '\b' || ch == 0x7F) // Rueckschritt: Zeichen auch am Schirm loeschen
            {
                if (gLineLen > 0)
                {
                    gLineLen--;
                    Serial.print(F("\b \b"));
                }
                continue;
            }
            if (gLineLen < sizeof(gLine) - 1)
            {
                gLine[gLineLen++] = ch;
                Serial.write(ch); // Echo, der Monitor zeigt die Eingabe sonst nicht
            }
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
