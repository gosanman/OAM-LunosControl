// ============================================================================
// Simulator: Inspektion und Invariantenwaechter.
//
// Liest den inneren Zustand der Module fuer die Anzeige. Dafuer werden in
// DIESER Uebersetzungseinheit private Felder geoeffnet - ein Kniff fuer
// Testwerkzeuge, der den Firmware-Code unveraendert laesst. Alle
// Standard-Header muessen VOR dem #define stehen.
// ============================================================================

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sim.h"

#define private public
#define protected public
#include "KwlFanModule.h"
#include "KwlRoomModule.h"
#undef private
#undef protected

extern Kwl::KwlFanModule openknxKwlFanModule;

namespace sim
{
    using namespace Kwl;

    static const char* typeName(FanType t)
    {
        switch (t)
        {
            case FanType::E2_60: return "e2 60";
            case FanType::EGO: return "ego";
            case FanType::RA_15_60: return "RA 15-60";
            case FanType::GENERIC_BIPOLAR: return "bipolar";
            case FanType::GENERIC_UNIPOLAR: return "unipolar";
        }
        return "?";
    }

    static bool bipolar(FanType t)
    {
        return t != FanType::RA_15_60 && t != FanType::GENERIC_UNIPOLAR;
    }

    static const char* dirName(Direction d) { return d == Direction::Supply ? "Zuluft" : "Abluft"; }

    static const char* sourceName(StageSource s)
    {
        switch (s)
        {
            case StageSource::Lock: return "1 Sperre";
            case StageSource::Protection: return "2 Schutz";
            case StageSource::AirDemand: return "3 Anforderung";
            case StageSource::Group: return "4 Verbund";
            case StageSource::Manual: return "5 Hand";
            case StageSource::Automatic: return "6 Automatik";
        }
        return "?";
    }

    static const char* modeName(OperatingMode m)
    {
        switch (m)
        {
            case OperatingMode::Auto: return "Auto";
            case OperatingMode::Comfort: return "Komfort";
            case OperatingMode::Standby: return "Standby";
            case OperatingMode::Eco: return "Nacht/Eco";
            case OperatingMode::Protection: return "Schutz";
            case OperatingMode::Boost: return "Stosslueften";
            case OperatingMode::Reduction: return "Absenkung";
            case OperatingMode::Quiet: return "Ruhe";
        }
        return "?";
    }

    static const char* exhaustName(uint8_t s)
    {
        static const char* n[] = {"aus", "Vorlauf", "aktiv", "Nachlauf"};
        return s < 4 ? n[s] : "?";
    }

    static std::string b(bool v) { return v ? "true" : "false"; }

    static std::string sensor(const KwlRoom::Sensor& s, int decimals)
    {
        return s.valid ? num(s.value, decimals) : "null";
    }

    /// Takt-Platz eines Luefters: Verbund n -> n-1, eigenstaendig -> 8 + Index.
    static int slotOf(uint8_t fanIndex)
    {
        const KwlFan* f = openknxKwlFanModule.mFan[fanIndex];
        if (f == nullptr)
            return -1;
        return f->groupNo() == 0 ? kGroupsMax + fanIndex : f->groupNo() - 1;
    }

    std::string snapshotJson()
    {
        KwlFanModule& m = openknxKwlFanModule;
        std::string o = "{";

        // --- DAC ------------------------------------------------------------
        o += "\"dac\":[";
        for (int ch = 0; ch < FANDRV_BOARD_CHANNELS; ch++)
        {
            const Chip& c = chips[ch / 2];
            if (ch)
                o += ",";
            o += "{\"volt\":" + num(outputVolt(ch), 3) + ",\"wire\":" +
                 std::to_string(c.reg[ch % 2]) + ",\"powered\":" + b(c.powered) +
                 ",\"range\":" + std::to_string(c.range) + "}";
        }
        o += "]";

        // --- Modul ----------------------------------------------------------
        o += ",\"module\":{\"error\":" + std::to_string(static_cast<int>(m.mError)) +
             ",\"outputReady\":" + b(m.mOutput.ready()) +
             ",\"powerSaved\":" + b(m.mPowerSaved) + ",\"i2cFrames\":" + std::to_string(i2cFrames) +
             ",\"i2cNacks\":" + std::to_string(i2cNacks) + "}";

        // --- Luefter ----------------------------------------------------------
        o += ",\"fans\":[";
        bool first = true;
        for (uint8_t i = 0; i < FAN_ChannelCount; i++)
        {
            const KwlFan* f = m.mFan[i];
            if (f == nullptr || !f->isActive())
                continue;
            if (!first)
                o += ",";
            first = false;
            o += "{\"no\":" + std::to_string(i + 1) + ",\"type\":\"" + typeName(f->type()) +
                 "\",\"bipolar\":" + b(bipolar(f->type())) +
                 ",\"ch\":" + std::to_string(f->dacChannel() + 1) +
                 ",\"channels\":" + std::to_string(f->channelsNeeded()) +
                 ",\"room\":" + std::to_string(f->roomNo()) +
                 ",\"group\":" + std::to_string(f->groupNo()) +
                 ",\"phase\":" + std::to_string(f->phase()) +
                 ",\"share\":" + std::to_string(f->share()) +
                 ",\"stage\":" + std::to_string(f->stage()) + ",\"dir\":\"" +
                 dirName(f->direction()) + "\",\"hrv\":" + b(f->mHrv) +
                 ",\"err\":" + std::to_string(static_cast<int>(f->lastError())) +
                 ",\"blocked\":" + b(f->blocked()) + ",\"volt\":" + num(f->mLastVolt, 3) +
                 ",\"calib\":" + num(f->mCalib, 4) +
                 ",\"filterDue\":" + b(f->filterDue()) +
                 ",\"filterLeft\":" + std::to_string(f->filterLeftPercent()) +
                 ",\"runHours\":" + num(f->mRunSeconds / 3600.0, 2) + "}";
        }
        o += "]";

        // --- Raeume -----------------------------------------------------------
        o += ",\"rooms\":[";
        first = true;
        for (uint8_t r = 1; r <= ROOM_ChannelCount; r++)
        {
            KwlRoom* room = openknxKwlRoomModule.room(r);
            if (room == nullptr || !room->isActive())
                continue;
            if (!first)
                o += ",";
            first = false;
            const StageResult& s = room->stage();
            o += "{\"no\":" + std::to_string(r) + ",\"stage\":" + std::to_string(s.stage) +
                 ",\"source\":\"" + sourceName(s.source) + "\",\"mode\":\"" + modeName(s.mode) +
                 "\",\"modeRank\":" + std::to_string(s.modeRank) +
                 ",\"dirForced\":" + b(s.directionForced) + ",\"dir\":\"" +
                 (s.directionForced ? dirName(s.direction) : "Takt") +
                 "\",\"cap\":" + std::to_string(room->groupCap()) +
                 ",\"cycle\":\"" + (room->cycleWish() == CycleRule::Summer ? "Sommer" : "WRG") +
                 "\",\"locked\":" + b(room->mLocked) + ",\"protection\":" + b(room->mProtection) +
                 ",\"dehumBlocked\":" + b(room->mDehumBlocked) +
                 ",\"sensorsMissing\":" + b(room->mSensorsMissing) +
                 ",\"interval\":" + b(room->mIntervalRunning) + ",\"exhaust\":\"" +
                 exhaustName(static_cast<uint8_t>(room->mExhaust)) +
                 "\",\"supplyReq\":" + b(room->supplyRequested()) +
                 ",\"err\":" + std::to_string(static_cast<int>(room->errorCode())) +
                 ",\"humIn\":" + sensor(room->mHumIn, 1) + ",\"tempIn\":" + sensor(room->mTempIn, 1) +
                 ",\"humOut\":" + sensor(room->mHumOut, 1) +
                 ",\"tempOut\":" + sensor(room->mTempOut, 1) +
                 ",\"co2\":" + sensor(room->mCo2Val, 0) + ",\"voc\":" + sensor(room->mVocVal, 0) +
                 ",\"tempSet\":" + sensor(room->mTempSet, 1) + "}";
        }
        o += "]";

        // --- Takt-Plaetze -------------------------------------------------------
        o += ",\"groups\":[";
        first = true;
        for (uint8_t g = 0; g < kSlotsMax; g++)
        {
            if (!m.mGroupActive[g])
                continue;
            const KwlGroup& grp = m.mGroup[g];
            const GroupResult& r = grp.mResult;
            if (!first)
                o += ",";
            first = false;
            std::string name = g < kGroupsMax
                                   ? "Verbund " + std::to_string(g + 1)
                                   : "Luefter " + std::to_string(g - kGroupsMax + 1) + " eigenst.";
            static const char* roles[] = {"intern", "Master", "Slave"};
            o += "{\"slot\":" + std::to_string(g) + ",\"name\":" + esc(name) + ",\"role\":\"" +
                 roles[static_cast<int>(m.mGroupRole[g]) % 3] +
                 "\",\"members\":" + std::to_string(grp.mCount) +
                 ",\"stage\":" + std::to_string(r.stage) + ",\"dir\":\"" +
                 dirName(r.phase0Direction) + "\",\"cycle\":" + std::to_string(r.cycleSeconds) +
                 ",\"deadTime\":" + std::to_string(grp.mDeadTime) +
                 ",\"inDeadTime\":" + b(r.inDeadTime) + ",\"hrv\":" + b(r.hrv) +
                 ",\"fixed\":" + b(r.directionFixed) + ",\"conflict\":" + b(r.conflict) +
                 ",\"conflictRoom\":" + std::to_string(r.conflictRoom) +
                 ",\"timedOut\":" + b(m.mGroupTimedOut[g]) +
                 ",\"since\":" + num((millis() - grp.mSince) / 1000.0, 1) + "}";
        }
        o += "]";

        // --- LED ------------------------------------------------------------
        o += ",\"led\":{";
        int ledMode = 0, ledCode = 0;
        uint32_t ledFunc = 0, flashes = 0;
        for (auto* lg : openknx.ledFunctions.all())
        {
            flashes += lg->flashes();
            if (!lg->active())
                continue;
            ledFunc = lg->id();
            ledMode = static_cast<int>(lg->mode());
            ledCode = lg->code();
        }
        static const char* modes[] = {"aus", "an", "blinkt", "Blinkcode"};
        o += "\"func\":" + std::to_string(ledFunc) + ",\"mode\":\"" + modes[ledMode % 4] +
             "\",\"code\":" + std::to_string(ledCode) + ",\"flashes\":" + std::to_string(flashes) + "}";

        return o + "}";
    }

    // ================================================================ Invarianten

    struct ChannelWatch
    {
        int side = 0;            ///< -1 unter 5 V, 0 bei 5 V, +1 ueber 5 V
        int lastSide = 0;        ///< letzte Seite ausser 0
        uint64_t atFiveSince = 0;
        bool fullLoad = false;
        uint64_t fullLoadSince = 0;
    };
    static ChannelWatch watch[4];
    static std::map<std::string, int> reported;

    void warn(const std::string& key, const std::string& inv, const std::string& text)
    {
        int& n = reported[key];
        n++;
        // Gleiche Meldung nur beim ersten Mal und dann jede 100. - sonst flutet
        // ein dauerhafter Verstoss die Anzeige.
        if (n == 1 || n % 100 == 0)
            emit(std::string("{\"ev\":\"warn\",\"t\":") + std::to_string(now) + ",\"inv\":" +
                 esc(inv) + ",\"key\":" + esc(key) + ",\"count\":" + std::to_string(n) +
                 ",\"text\":" + esc(text) + "}");
    }

    void resetInvariants()
    {
        for (auto& w : watch)
            w = ChannelWatch{};
        reported.clear();
    }

    void checkInvariants()
    {
        KwlFanModule& m = openknxKwlFanModule;

        // Invariante 3: kein Sollwert vor dem Bereichsregister.
        if (rangeViolation)
        {
            rangeViolation = false;
            warn("I3", "Invariante 3",
                 "Sollwert geschrieben, bevor das Bereichsregister 0x11 stand");
        }

        // Kanaltyp je DAC-Kanal aus der Parametrierung (unbekannt = bipolar).
        FanType chType[4] = {FanType::GENERIC_BIPOLAR, FanType::GENERIC_BIPOLAR,
                             FanType::GENERIC_BIPOLAR, FanType::GENERIC_BIPOLAR};
        int chFan[4] = {-1, -1, -1, -1};
        for (uint8_t i = 0; i < FAN_ChannelCount; i++)
        {
            const KwlFan* f = m.mFan[i];
            if (f == nullptr || !f->isActive() || !configured())
                continue;
            for (uint8_t k = 0; k < f->channelsNeeded(); k++)
            {
                const uint8_t ch = f->dacChannel() + k;
                if (ch < 4)
                {
                    chType[ch] = f->type();
                    chFan[ch] = i;
                }
            }
        }

        for (int ch = 0; ch < FANDRV_BOARD_CHANNELS; ch++)
        {
            ChannelWatch& w = watch[ch];
            const Chip& c = chips[ch / 2];
            if (!c.powered)
            {
                w = ChannelWatch{};
                continue;
            }
            const double v = outputVolt(ch) / (gain[ch] == 0 ? 1.0 : gain[ch]);

            // Volllast-Signal am bipolaren Kanal: unter 0,3 V, ohne dass der Luefter
            // auf Stufe 4 steht - Stufe 4 Zuluft liegt gewollt bei etwa 0 V.
            if (bipolar(chType[ch]))
            {
                const KwlFan* fan = chFan[ch] >= 0 ? m.mFan[chFan[ch]] : nullptr;
                const bool wanted = fan != nullptr && fan->stage() == kStageMax;
                const bool full = v < 0.3 && !wanted;
                if (full && !w.fullLoad)
                    w.fullLoadSince = now;
                if (!full && w.fullLoad && now - w.fullLoadSince > 0)
                    emit(std::string("{\"ev\":\"info\",\"t\":") + std::to_string(now) +
                         ",\"text\":" +
                         esc("S" + std::to_string(ch + 1) + ": " +
                             num((now - w.fullLoadSince) / 1000.0, 2) +
                             " s unter 0,3 V (Volllast-Signal)") +
                         "}");
                w.fullLoad = full;
            }

            // Invariante 5: Richtungswechsel nur ueber die Totzeit bei 5,00 V.
            if (!bipolar(chType[ch]))
                continue;
            const int side = v < 4.95 ? -1 : (v > 5.05 ? 1 : 0);
            if (side == 0 && w.side != 0)
                w.atFiveSince = now;
            if (side != 0 && w.lastSide != 0 && side != w.lastSide)
            {
                const double dwell = w.side == 0 ? (now - w.atFiveSince) / 1000.0 : 0.0;
                double needed = 2.0;
                if (chFan[ch] >= 0)
                {
                    const int slot = slotOf(static_cast<uint8_t>(chFan[ch]));
                    if (slot >= 0 && slot < kSlotsMax)
                        needed = m.mGroup[slot].mDeadTime;
                }
                // Ein Simulationsschritt Toleranz: die Uhr laeuft in Schritten.
                if (dwell + 0.05 < needed)
                    warn("I5-S" + std::to_string(ch + 1), "Invariante 5",
                         "S" + std::to_string(ch + 1) + ": Richtungswechsel nach " +
                             num(dwell, 2) + " s bei 5,00 V, verlangt " + num(needed, 1) + " s");
            }
            if (side != 0)
                w.lastSide = side;
            w.side = side;
        }

        // Invariante 6: ego-Motoren nie in zwei getrennten Frames.
        for (uint8_t i = 0; i < FAN_ChannelCount; i++)
        {
            const KwlFan* f = m.mFan[i];
            if (f == nullptr || !f->isActive() || !configured() || f->type() != FanType::EGO)
                continue;
            const uint8_t ch = f->dacChannel();
            if (ch + 1 < 4 && (!lastWriteWasPair[ch] || !lastWriteWasPair[ch + 1]))
            {
                warn("I6-F" + std::to_string(i + 1), "Invariante 6",
                     "ego an S" + std::to_string(ch + 1) + "/S" + std::to_string(ch + 2) +
                         " in getrennten Frames geschrieben (z. B. sicherer Zustand)");
                lastWriteWasPair[ch] = lastWriteWasPair[ch + 1] = true; // einmal je Vorfall
            }
        }
    }
} // namespace sim
