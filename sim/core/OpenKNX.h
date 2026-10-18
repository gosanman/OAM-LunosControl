#pragma once
// ============================================================================
// Simulator: Ersatz fuer OGM-Common/OpenKNX.h.
//
// Bildet genau die Teile von OpenKNX und dem KNX-Stack nach, die das
// Lueftungsmodul (OFM-KwlControl) benutzt - nicht mehr. Der Firmware-Code
// selbst wird UNVERAENDERT uebersetzt; nur was er von aussen aufruft, kommt von
// hier: Parameterspeicher (knx.paramByte ...), Gruppenobjekte, Logger, Konsole,
// Flash, LED-Funktionen und die Funktionstaste.
//
// Die DPT-Kodierung ist die ECHTE der KNX-Bibliothek (dptconvert.cpp,
// knx_value.cpp): ein Wert wird beim Senden so kodiert und beim Empfangen so
// dekodiert wie am Bus. Ein falscher DPT faellt damit im Simulator auf.
// ============================================================================

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "hardware.h" // Board-Header (DEVICE_KNXFANDRV_REV01 per -D)

#include "dpt.h"
#include "dptconvert.h"
#include "knx_value.h"

uint32_t millis();

// ---------------------------------------------------------------- KNX-Stack

enum ParameterFloatEncodings
{
    Float_Enc_DPT9 = 0,
    Float_Enc_IEEE754Single = 1,
    Float_Enc_IEEE754Double = 2,
};

/// Ein Gruppenobjekt. Haelt die Nutzdaten wie im echten Stack als Bytes.
class GroupObject
{
  public:
    uint16_t asap() const { return mNumber; }

    /// Empfangener oder zuletzt gesendeter Wert, dekodiert mit `type`.
    KNXValue value(const Dpt& type);

    /// Wie im Stack: false, wenn der DPT den Wert als ungueltig kennzeichnet
    /// (DPT 9: 0x7FFF) oder nicht dekodierbar ist.
    bool tryValue(KNXValue& value, const Dpt& type);

    /// Wert setzen UND senden - wie GroupObject::value() im Stack.
    void value(const KNXValue& value, const Dpt& type);

    /// Wert setzen, ohne zu senden. false: DPT nicht kodierbar.
    bool valueNoSend(const KNXValue& value, const Dpt& type);

    // --- nur Simulator ------------------------------------------------------
    void simInit(uint16_t number) { mNumber = number; std::memset(mData, 0, sizeof(mData)); }
    /// Telegramm vom Bus: Bytes so ablegen, wie sie ankommen.
    bool simReceive(const KNXValue& value, const Dpt& type);
    const uint8_t* simData() const { return mData; }

  private:
    uint16_t mNumber = 0;
    uint8_t mData[16] = {};
};

class SimKnx
{
  public:
    uint8_t paramByte(uint32_t addr);
    uint16_t paramWord(uint32_t addr);
    uint32_t paramInt(uint32_t addr);
    double paramFloat(uint32_t addr, ParameterFloatEncodings enc);
    uint8_t* paramData(uint32_t addr);
    char* paramString(uint32_t addr, uint32_t length);
    GroupObject& getGroupObject(uint16_t number);
    bool configured();
};

extern SimKnx knx;

// ---------------------------------------------------------------- OpenKNX

#define CONSOLE_HEADLINE_COLOR 33

namespace OpenKNX
{
    class Base
    {
      public:
        virtual ~Base() = default;
        virtual const std::string name() { return "Base"; }
        virtual const std::string version() { return ""; }

      protected:
        virtual const std::string logPrefix() { return name(); }
    };

    class Channel : public Base
    {
      protected:
        uint8_t _channelIndex = 0;
        const std::string logPrefix() override
        {
            return name() + "<" + std::to_string(_channelIndex + 1) + ">";
        }

      public:
        virtual uint8_t channelIndex() { return _channelIndex; }
    };

    class Module : public Base
    {
      public:
        virtual void init() {}
        virtual void setup(bool configured) { (void)configured; }
        virtual void loop(bool configured) { (void)configured; }
        virtual void processInputKo(GroupObject& ko) { (void)ko; }
        virtual void processBeforeRestart() {}
        virtual void savePower() {}
        virtual bool restorePower() { return true; }
        virtual uint16_t flashSize() { return 0; }
        virtual void writeFlash() {}
        virtual void readFlash(const uint8_t* data, const uint16_t size)
        {
            (void)data;
            (void)size;
        }
        virtual void showHelp() {}
        virtual bool processCommand(const std::string cmd, bool debugKo)
        {
            (void)cmd;
            (void)debugKo;
            return false;
        }
    };

    namespace Led
    {
        /// Zustand einer LED-Funktion, so wie das Modul ihn setzt.
        enum class SimMode : uint8_t
        {
            Off,
            On,
            Blinking,
            ErrorCode
        };

        class FunctionGroup
        {
          public:
            explicit FunctionGroup(uint32_t id) : mId(id) {}
            bool active();
            void on() { set(SimMode::On, 0); }
            void off() { set(SimMode::Off, 0); }
            void blinking() { set(SimMode::Blinking, 0); }
            void flash(uint8_t count = 1) { mFlashes += count; }
            void errorCode(uint8_t code = 0)
            {
                if (code != 0)
                    set(SimMode::ErrorCode, code);
                else if (mMode == SimMode::ErrorCode)
                    set(SimMode::Off, 0);
            }

            // --- nur Simulator ---------------------------------------------
            uint32_t id() const { return mId; }
            SimMode mode() const { return mMode; }
            uint8_t code() const { return mCode; }
            uint32_t flashes() const { return mFlashes; }
            void simReset()
            {
                mMode = SimMode::Off;
                mCode = 0;
                mFlashes = 0;
            }

          private:
            void set(SimMode m, uint8_t c)
            {
                mMode = m;
                mCode = c;
            }
            uint32_t mId;
            SimMode mMode = SimMode::Off;
            uint8_t mCode = 0;
            uint32_t mFlashes = 0;
        };
    } // namespace Led

    class SimLedFunctions
    {
      public:
        Led::FunctionGroup* get(uint32_t id);
        std::vector<Led::FunctionGroup*> all();
        void simReset();

      private:
        std::vector<Led::FunctionGroup*> mGroups;
    };

    class SimConsole
    {
      public:
        void printHelpLine(const char* cmd, const char* text);
        void writeDiagnoseKo(const char* fmt, ...);
    };

    class SimLogger
    {
      public:
        void logMacroWrapper(int color, const char* prefix, const char* fmt, ...);
        void log(const char* text);
        void color(int c) { (void)c; }
        void indentUp() { mIndent++; }
        void indentDown()
        {
            if (mIndent > 0)
                mIndent--;
        }
        int indent() const { return mIndent; }

      private:
        int mIndent = 0;
    };

    /// Flash wie OpenKNX: je Modul ein Block, sequentiell geschrieben/gelesen.
    class SimFlash
    {
      public:
        void writeByte(uint8_t v);
        void writeWord(uint16_t v);
        void writeInt(uint32_t v);
        uint8_t readByte();
        uint16_t readWord();
        uint32_t readInt();

        // --- nur Simulator -------------------------------------------------
        std::vector<uint8_t>* writeTarget = nullptr;
        const uint8_t* readPtr = nullptr;
        const uint8_t* readEnd = nullptr;
    };

    class SimButton
    {
      public:
        void onShortClick(std::function<void()> cb) { shortCb = cb; }
        void onLongClick(std::function<void()> cb) { longCb = cb; }
        void onDoubleClick(std::function<void()> cb) { doubleCb = cb; }
        std::function<void()> shortCb, longCb, doubleCb;
    };

    struct SimOpenKnx
    {
        SimConsole console;
        SimLogger logger;
        SimFlash flash;
        SimLedFunctions ledFunctions;
        SimButton func1Button;
    };
} // namespace OpenKNX

extern OpenKNX::SimOpenKnx openknx;

#define logInfoP(...) openknx.logger.logMacroWrapper(0, logPrefix().c_str(), __VA_ARGS__)
#define logErrorP(...) openknx.logger.logMacroWrapper(31, logPrefix().c_str(), __VA_ARGS__)
#define logDebugP(...) openknx.logger.logMacroWrapper(90, logPrefix().c_str(), __VA_ARGS__)
#define logInfo(...) openknx.logger.logMacroWrapper(0, "", __VA_ARGS__)
#define logIndentUp() openknx.logger.indentUp()
#define logIndentDown() openknx.logger.indentDown()
