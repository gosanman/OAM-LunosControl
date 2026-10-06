// ============================================================================
// Simulator: Ersatz fuer Drive/Gp8413Drive.cpp.
//
// Modell zweier GP8413 an einem Bus. Was es nachbildet, und warum:
//   - Ohne 12 V antwortet kein Chip (kein ACK), und die Ausgaenge sind tot.
//   - Nach dem Einschalten stehen Wert 0 und das Bereichsregister im
//     Werkszustand (Invariante 7: kein EEPROM, also 0 V = Volllast).
//   - Der Wert liegt linksbuendig im 16-Bit-Register; der Chip nimmt die oberen
//     15 Bit (Messung P6). Wer rechtsbuendig schreibt, sieht die halbe Spannung.
//   - Ein Sollwert vor dem Bereichsregister ist ein Verstoss gegen Invariante 3
//     und wird gemeldet.
// Board-Konstanten (Kanalzahl, Kennung, Busformat, Polaritaet) kommen aus dem
// ECHTEN Board-Header, nicht aus einer Kopie.
// ============================================================================

#include "sim.h"

#include "Drive/Gp8413Drive.h"

namespace sim
{
    Chip chips[2];
    double gain[4] = {1.0, 1.0, 1.0, 1.0};
    uint64_t i2cFrames = 0;
    uint64_t i2cNacks = 0;
    bool lastWriteWasPair[4] = {true, true, true, true};
    bool rangeViolation = false;

    void dacPower(bool on)
    {
        for (auto& c : chips)
        {
            if (on && !c.powered)
            {
                // Einschalten: Werkszustand.
                c.range = kRangePowerOn;
                c.reg[0] = 0;
                c.reg[1] = 0;
            }
            c.powered = on;
        }
    }

    double outputVolt(int channel)
    {
        if (channel < 0 || channel >= FANDRV_BOARD_CHANNELS)
            return 0.0;
        const Chip& c = chips[channel / 2];
        if (!c.powered)
            return 0.0;
        const double fullScale = c.range == 0x11 ? 10.0 : 5.0;
        const uint16_t code15 = c.reg[channel % 2] >> 1; // obere 15 Bit
        return code15 / 32767.0 * fullScale * gain[channel];
    }
} // namespace sim

namespace Kwl
{
    class SimDacDrive : public IDacDrive
    {
      public:
        bool probe() override
        {
            sim::i2cFrames += 2;
            const bool ok = sim::chips[0].powered &&
                            (FANDRV_BOARD_CHANNELS <= 2 || sim::chips[1].powered);
            if (!ok)
                sim::i2cNacks++;
            return ok;
        }

        bool configure() override
        {
            bool ok = true;
            for (int i = 0; i < (FANDRV_BOARD_CHANNELS + 1) / 2; i++)
            {
                sim::i2cFrames++;
                if (!sim::chips[i].powered)
                {
                    sim::i2cNacks++;
                    ok = false;
                    continue;
                }
                sim::chips[i].range = 0x11;
            }
            return ok;
        }

        bool write(uint8_t channel, uint16_t wire) override
        {
            if (channel >= FANDRV_BOARD_CHANNELS)
                return false;
            sim::Chip& c = sim::chips[channel / 2];
            sim::i2cFrames++;
            if (!c.powered)
            {
                sim::i2cNacks++;
                return false;
            }
            if (c.range != 0x11)
                sim::rangeViolation = true;
            c.reg[channel % 2] = wire;
            sim::lastWriteWasPair[channel] = false;
            return true;
        }

        bool writePair(uint8_t firstChannel, uint16_t wireA, uint16_t wireB) override
        {
            if (firstChannel + 1 >= FANDRV_BOARD_CHANNELS || firstChannel % 2 != 0)
                return false;
            sim::Chip& c = sim::chips[firstChannel / 2];
            sim::i2cFrames++;
            if (!c.powered)
            {
                sim::i2cNacks++;
                return false;
            }
            if (c.range != 0x11)
                sim::rangeViolation = true;
            c.reg[0] = wireA;
            c.reg[1] = wireB;
            sim::lastWriteWasPair[firstChannel] = true;
            sim::lastWriteWasPair[firstChannel + 1] = true;
            return true;
        }

        uint8_t channelCount() const override { return FANDRV_BOARD_CHANNELS; }
        uint8_t channelsPerChip() const override { return 2; }
    };

    IDacDrive& boardDacDrive()
    {
        static SimDacDrive drive;
        return drive;
    }

    bool boardLeftAligned() { return FANDRV_DAC_LEFT_ALIGNED != 0; }
    uint8_t boardChannels() { return FANDRV_BOARD_CHANNELS; }
    uint8_t boardId() { return FANDRV_BOARD_ID; }
    bool boardBelow5vIsSupply() { return FANDRV_BELOW_5V_IS_SUPPLY != 0; }
} // namespace Kwl
