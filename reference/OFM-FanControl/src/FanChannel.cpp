#include "FanChannel.h"
#include "knxprod.h"

FanChannel::FanChannel(uint8_t index, IFanHardware &hardware) : _hw(hardware)
{
    _channelIndex = index;
}

// ===========================================================================
// Setup
// ===========================================================================

void FanChannel::setup()
{
    // Ob dieser Kanal benutzt wird, sagt die Kanalaktivitaet aus der Kanalauswahl-Tabelle.
    // Ein geraeteweiter Zaehler waere das mit dem Kanalauswahl-Beschluss (09.07.2026)
    // abgeschaffte Muster; die Sichtbarkeit haengt in der ETS an genau diesem Parameter.
    _active = ParamFAN_fActive;
    if (!_active) return;

    _type = (Fan::ChannelType)ParamFAN_fMode;

    _isMaster = ParamFAN_fIsMaster;
    _hasTacho = ParamFAN_fHasTacho;

    // Richtungsart: der Master nimmt die ETS-Vorgabe, sofern sie nicht "ueber KO" lautet.
    // Der Slave startet reversierend und wartet auf das Telegramm des Masters.
    if (_isMaster)
    {
        const Fan::DirModeSel sel = (Fan::DirModeSel)ParamFAN_fDirModeSel;
        if (sel != Fan::DirModeSel::ViaKo) _dirMode = (Fan::DirMode)sel;
    }

    // Der ETS-Parameter liefert den Anfangswert; ein spaeter empfangenes KO ueberschreibt ihn
    // und wird persistiert (restore() laeuft nach setup()).
    _suspended = ParamFAN_fSuspended;

    // Die Mittelstellung trennt die beiden Foerderrichtungen und ist zugleich der sichere
    // Zustand - das gibt es ausschliesslich beim reversiblen Luefter. Ein nicht reversibler
    // Luefter wird gewoehnlich gefahren: 0 = Stillstand, 100 = Vollgas. Deshalb wird der
    // Parameter hier nicht nur in der ETS ausgeblendet, sondern auch verworfen; sonst wuerde
    // ein Restwert aus einer frueheren Konfiguration den Luefter auf halber Kraft festhalten.
    const uint8_t midpoint = (_type == Fan::ChannelType::Reversible) ? (uint8_t)ParamFAN_fDriveMid : 0;
    _hw.setMidpoint(midpoint);

    _configFault = (_type == Fan::ChannelType::Reversible) && (midpoint == 0);
    if (_configFault)
        logErrorP("Kanaltyp Reversibel, aber die Mittelstellung ist 0 - keine zweite Richtung moeglich");

    // Kennlinien-Plausibilitaet: Zwischenpunkte muessen aufsteigend unter der Maximaldrehzahl
    // liegen. Richtung B wird nur geprueft, wenn sie ueberhaupt angefahren werden kann.
    if (_hasTacho)
    {
        if (ParamFAN_fCurveA_RpmM > 0 &&
            (ParamFAN_fCurveA_Rpm1 > ParamFAN_fCurveA_Rpm2 ||
             ParamFAN_fCurveA_Rpm2 > ParamFAN_fCurveA_RpmM))
        {
            _configFault = true;
            logErrorP("Kennlinie Richtung A: Punkte nicht aufsteigend");
        }

        if (_type == Fan::ChannelType::Reversible && ParamFAN_fCurveB_RpmM > 0 &&
            (ParamFAN_fCurveB_Rpm1 > ParamFAN_fCurveB_Rpm2 ||
             ParamFAN_fCurveB_Rpm2 > ParamFAN_fCurveB_RpmM))
        {
            _configFault = true;
            logErrorP("Kennlinie Richtung B: Punkte nicht aufsteigend");
        }
    }

    const uint32_t now = millis();
    _stateSince = now;
    _cycleStarted = now;
    _blockWindowStart = now;
    _lastSecondTick = now;
}

// ===========================================================================
// Persistenz
// ===========================================================================

FanChannel::PersistentState FanChannel::persistentState() const
{
    PersistentState s;
    s.flags = (uint8_t)((_enableLatched ? 0 : 0x01) |
                        (_suspended ? 0x02 : 0x00) |
                        (_enableSeenEver ? 0x04 : 0x00));
    s.runSeconds = _runSeconds;
    return s;
}

void FanChannel::restore(const PersistentState &state)
{
    // Bit0 gesetzt bedeutet: es lag eine Sperre an, die den Ausfall ueberlebt (E-1e).
    _enableLatched = (state.flags & 0x01) == 0;
    _suspended = (state.flags & 0x02) != 0;
    _enableSeenEver = (state.flags & 0x04) != 0;
    _runSeconds = state.runSeconds;

    // E-1f: war die Freigabe schon einmal verknuepft, laeuft die Ueberwachung ab dem Neustart
    // weiter - ohne auf ein erstes Telegramm zu warten. Sonst liefe ein Geraet, das mit
    // gespeicherter Freigabe startet und danach keine Telegramme mehr bekommt (abgezogene
    // Buslinie, ausgefallener Druckwaechter), unbegrenzt weiter. Genau das soll das
    // Ruhestromprinzip verhindern.
    if (_enableSeenEver)
    {
        _enableWatchRunning = true;
        _lastEnableSeen = millis();
    }
}

// ===========================================================================
// Empfang
// ===========================================================================

void FanChannel::processInputKo(GroupObject &ko)
{
    if (!isActive()) return;

    const uint32_t now = millis();
    const int index = FAN_KoCalcIndex(ko.asap());

    switch (index)
    {
        case FAN_KoEnable:
        {
            const bool enabled = (bool)ko.value(DPT_Enable);
            _lastEnableSeen = now;
            _enableWatchRunning = true;

            // Einmal gesehen heisst: das Objekt ist verknuepft. Das wird persistiert, damit die
            // Ueberwachung nach einem Neustart sofort wieder laeuft (E-1f, siehe restore()).
            if (!_enableSeenEver)
            {
                _enableSeenEver = true;
                _persistDirty = true;
            }

            if (_enableLatched != enabled)
            {
                _enableLatched = enabled;
                _persistDirty = true;
                if (!enabled) logInfoP("Freigabe entzogen, Sperre ist selbsthaltend");
            }
            break;
        }

        case FAN_KoPowerSet:
            _powerSet = (uint8_t)ko.value(DPT_Scaling);
            break;

        case FAN_KoDirMode:
        {
            const uint8_t raw = (uint8_t)ko.value(DPT_Value_1_Ucount);
            if (raw > (uint8_t)Fan::DirMode::Last)
            {
                _invalidValue = true;
                logErrorP("Ungueltige Richtungsart empfangen: %u", raw);
            }
            else
            {
                _invalidValue = false;
                _dirMode = (Fan::DirMode)raw;
            }
            break;
        }

        case FAN_KoTact:
            if (!_isMaster)
            {
                _tact = (bool)ko.value(DPT_Bool);
                _lastMasterSeen = now;
            }
            break;

        case FAN_KoMasterAlive:
            if (!_isMaster)
            {
                _lastMasterSeen = now;
                _masterTimeout = false;
            }
            break;

        case FAN_KoBoost:
            if (_isMaster)
            {
                if ((bool)ko.value(DPT_Start))
                    _boostUntil = now + (uint32_t)ParamFAN_fBoostTime * 1000;
                else
                    _boostUntil = 0; // 0 bricht die Stosslueftung ab
            }
            break;

        case FAN_KoCtrlValue:
            if (_isMaster)
            {
                // Absicht, auch wenn der Name hier irrefuehrend aussieht: saemtliche DPT 9.x
                // benutzen dasselbe 2-Byte-Gleitkommaformat und unterscheiden sich nur in der
                // Einheit. Welche Groesse tatsaechlich anliegt (CO2 in ppm, Feuchte in Prozent),
                // waehlt der Parameter "Regelgroesse" ueber die ComObjectRef aus. Zum Dekodieren
                // ist der konkrete Subtyp deshalb gleichgueltig - 9.001 steht hier
                // stellvertretend fuer die gesamte Familie.
                _ctrlValue = (float)ko.value(DPT_Value_Common_Temperature);
                _ctrlValueSeen = true;
            }
            break;

        case FAN_KoTempIn:
        case FAN_KoHumIn:
        case FAN_KoTempOut:
        case FAN_KoHumOut:
            if (_isMaster && ParamFAN_fDewGuard)
            {
                // Alle vier sind DPT 9.x und teilen sich das 2-Byte-Gleitkommaformat.
                const uint8_t slot = (index == FAN_KoTempIn) ? 0 : (index == FAN_KoHumIn) ? 1
                                   : (index == FAN_KoTempOut) ? 2 : 3;
                _dewValue[slot] = (float)ko.value(DPT_Value_Common_Temperature);
                _dewSeenAt[slot] = now;
                _dewSeen[slot] = true;
            }
            break;

        case FAN_KoAck:
            _blocked = false;
            _emptyWindows = 0;
            _invalidValue = false;
            break;

        case FAN_KoSuspendSet:
        {
            // Das Objekt heisst "Suspendieren", also schaltet 1 die Suspendierung EIN
            // und 0 nimmt den Kanal wieder in Betrieb.
            const bool suspend = (bool)ko.value(DPT_Enable);
            if (_suspended != suspend)
            {
                _suspended = suspend;
                _persistDirty = true;
            }
            break;
        }

        default:
            break;
    }
}

// ===========================================================================
// Rechnen
// ===========================================================================

uint8_t FanChannel::controlOutput() const
{
    // P-Regler: unterhalb des Sollwerts Grundlast, darueber linear bis zur Maximalleistung.
    // Bewusst ohne I-Anteil - Lueftung ist traege, ein Integrator brachte nur Ueberschwingen.
    const uint8_t base = ParamFAN_fCtrlBase;
    const uint8_t maxOut = ParamFAN_fCtrlMax > base ? ParamFAN_fCtrlMax : base;

    if (!_ctrlValueSeen) return base; // noch kein Istwert empfangen

    const float setpoint = (float)ParamFAN_fCtrlSetpoint;
    const float band = (float)ParamFAN_fCtrlBand;
    if (band <= 0.0f) return base;

    if (_ctrlValue <= setpoint) return base;

    const float ratio = (_ctrlValue - setpoint) / band;
    const float out = base + ratio * (float)(maxOut - base);

    if (out >= (float)maxOut) return maxOut;
    return (uint8_t)(out + 0.5f);
}

float FanChannel::dewPoint(float relHumidity, float temperature)
{
    // Magnus-Formel. Unter 1 % relativer Feuchte wird geklemmt: log(0) waere minus unendlich,
    // und so tief misst ohnehin kein Sensor sinnvoll.
    if (relHumidity < 1.0f) relHumidity = 1.0f;
    if (relHumidity > 100.0f) relHumidity = 100.0f;

    const float a = 17.625f;
    const float b = 243.04f;
    const float g = (a * temperature) / (b + temperature) + logf(relHumidity / 100.0f);
    return (b * g) / (a - g);
}

void FanChannel::updateDewGuard()
{
    if (!_isMaster || !ParamFAN_fDewGuard)
    {
        _dewBlocking = false;
        _dewNoData = false;
        return;
    }

    // Fehlerfall: ein Wert wurde nie empfangen, oder er ist aelter als die Ueberwachungszeit.
    // Ohne Zeitgrenze bliebe ein stillschweigend ausgefallener Aussensensor unbemerkt - der
    // Waechter rechnete dann ewig mit einem eingefrorenen Wert weiter.
    const uint16_t watch = ParamFAN_fDewWatch;
    const uint32_t now = millis();
    bool usable = true;

    for (uint8_t i = 0; i < 4; i++)
    {
        if (!_dewSeen[i]) { usable = false; break; }
        if (watch > 0 && (now - _dewSeenAt[i]) > (uint32_t)watch * 60000) { usable = false; break; }
    }

    _dewNoData = !usable;

    if (!usable)
    {
        // Was dann gilt, entscheidet der Anwender: Feuchteschutz oder Luftqualitaet.
        _dewBlocking = ((Fan::DewFallback)ParamFAN_fDewFallback == Fan::DewFallback::Block);
        return;
    }

    const float tdIn = dewPoint(_dewValue[1], _dewValue[0]);
    const float tdOut = dewPoint(_dewValue[3], _dewValue[2]);

    // Erstes Kriterium: gelueftet wird, solange die Innenluft die feuchtere ist - dann traegt
    // das Lueften Feuchte aus. Abstaende durchweg in Zehntel Kelvin.
    const float gap = tdIn - tdOut;
    const float on = (float)ParamFAN_fDewOn / 10.0f;
    const float off = (float)ParamFAN_fDewOff / 10.0f;

    // Ein Schutz des Keramikelements ueber einen Temperaturabstand waere hier denkbar, ist aber
    // bewusst nicht eingebaut: ein Keramik-Regenerator befeuchtet und trocknet im Gegenstrom
    // zyklisch ab - das ist der Mechanismus der Feuchterueckgewinnung, kein Schaden. Wer
    // konservativer lueften will, hebt die Einschaltschwelle. Der Fall, der wirklich schaden
    // koennte, ist Frost am Element, und das waere ein Kriterium ueber die Aussentemperatur.
    if (off < on)
    {
        if (gap >= on) _dewBlocking = false;
        else if (gap <= off) _dewBlocking = true;
        // dazwischen: Zustand halten, sonst flattert es am Umschaltpunkt
    }
    else
    {
        _dewBlocking = (gap < on);
    }
}

uint8_t FanChannel::hysteresisOutput()
{
    // Zweipunkt mit Hysterese: der Schaltzustand bleibt zwischen den Schwellen stehen, sonst
    // wuerde ein Messwert, der um eine einzelne Schwelle herum zappelt, den Luefter im
    // Sekundentakt takten.
    const uint16_t on = ParamFAN_fHystOn;
    const uint16_t off = ParamFAN_fHystOff;
    const uint8_t powerOn = ParamFAN_fHystPower;
    const uint8_t powerOff = ParamFAN_fHystBase;

    if (!_ctrlValueSeen) return powerOff; // noch kein Istwert empfangen

    if (off < on)
    {
        // Der uebliche Fall: einschalten oberhalb, ausschalten unterhalb, dazwischen halten.
        if (_ctrlValue >= (float)on) _hystActive = true;
        else if (_ctrlValue <= (float)off) _hystActive = false;
    }
    else
    {
        // Ausschaltschwelle nicht unter der Einschaltschwelle: dann gibt es kein Fenster, in
        // dem gehalten werden koennte. Statt eine unentscheidbare Lage zu erfinden, wird an
        // der Einschaltschwelle geschaltet - ohne Hysterese, aber vorhersagbar.
        _hystActive = (_ctrlValue >= (float)on);
    }

    return _hystActive ? powerOn : powerOff;
}

uint8_t FanChannel::groupPower()
{
    // Die Gruppenvorgabe erzeugt nur der Master; der Slave bekommt sie fertig geliefert.
    if (!_isMaster) return _powerSet;

    switch ((Fan::SetpointSource)ParamFAN_fSetpointSource)
    {
        case Fan::SetpointSource::Fixed: return ParamFAN_fFixedPower;
        case Fan::SetpointSource::InternalControl: return controlOutput();
        case Fan::SetpointSource::Hysteresis: return hysteresisOutput();
        default: return _powerSet;
    }
}

uint8_t FanChannel::targetPower()
{
    // Die Konsolen-Uebersteuerung ersetzt die Sollwertbildung, damit man am Geraet ohne ETS und
    // ohne Gruppentelegramm messen kann. Das Taupunkt-Veto bleibt bewusst wirksam: ein Testbefehl
    // soll die Hardware pruefen, nicht eine Schutzfunktion aushebeln. Warum nichts laeuft, sagt
    // dann "fan st" ueber den Fehlercode.
    if (_testPower >= 0)
        return _dewBlocking ? 0 : (uint8_t)_testPower;

    if (_masterTimeout) return 0;

    uint8_t group = groupPower();

    // Stosslueftung ist Masterfunktion: sie hebt die Gruppenvorgabe an.
    if (_isMaster && _boostUntil != 0 && (int32_t)(_boostUntil - millis()) > 0)
        group = ParamFAN_fBoostLevel;

    // Taupunktwaechter ist ein Veto ueber allen Sollwertquellen, kein eigener Sollwertgeber.
    if (_dewBlocking) return 0;

    const uint16_t scaled = (uint16_t)group * ParamFAN_fShare / 100;
    return scaled > 100 ? 100 : (uint8_t)scaled;
}

uint8_t FanChannel::powerToDrive(uint8_t power) const
{
    if (power == 0) return 0;

    const bool dirB = (_direction == Fan::Direction::B);
    uint8_t minVal = dirB ? ParamFAN_fMinB : ParamFAN_fMinA;
    uint8_t maxVal = dirB ? ParamFAN_fMaxB : ParamFAN_fMaxA;
    if (maxVal < minVal) maxVal = minVal;

    const uint32_t span = (uint32_t)(maxVal - minVal);
    uint32_t drive = minVal + span * power / 100;
    if (drive > 100) drive = 100;
    return (uint8_t)drive;
}

Fan::Direction FanChannel::desiredDirection() const
{
    if (_type != Fan::ChannelType::Reversible) return Fan::Direction::A;

    // Der Richtungswechsel laeuft auch hier durch die Zustandsmaschine, also mit Totzeit und
    // Anlaufpuls - eine uebersteuerte Richtung ist keine Abkuerzung am Ablauf vorbei.
    if (_testDirection >= 0) return (Fan::Direction)_testDirection;

    switch (_dirMode)
    {
        case Fan::DirMode::OnlyA: return Fan::Direction::A;
        case Fan::DirMode::OnlyB: return Fan::Direction::B;
        default: break;
    }

    // Reversierend: Richtung ergibt sich aus Zuordnung und Taktzustand.
    // Der Master erzeugt den Takt und ist damit die Bezugsphase - "Gegenphase" gibt es fuer ihn
    // nicht. In der ETS ist der Parameter beim Master ausgeblendet; hier wird zusaetzlich ein
    // eventuell noch gespeicherter Altwert uebergangen.
    const bool counterPhase = _isMaster ? false : (bool)ParamFAN_fPhase;
    const bool bDirection = (_tact != counterPhase);
    return bDirection ? Fan::Direction::B : Fan::Direction::A;
}

int32_t FanChannel::rpmToFlow(uint16_t rpm) const
{
    const bool dirB = (_direction == Fan::Direction::B);

    // Kennlinie A ist die Pflichtkennlinie, B ist optional. Reversierluefter foerdern in
    // beide Richtungen praktisch gleich viel (ebm-papst AxiRev 126: die Kennlinien liegen
    // uebereinander), deshalb genuegt in der Praxis eine einzige Kennlinie fuer beide
    // Richtungen. Nur wenn B tatsaechlich gepflegt ist, wird sie auch benutzt.
    // Ohne diesen Rueckfall lieferte Richtung B bei symmetrischer Konfiguration 0 statt des
    // negativen Wertes.
    const bool useB = dirB && (ParamFAN_fCurveB_RpmM > 0);

    const uint16_t r1 = useB ? ParamFAN_fCurveB_Rpm1 : ParamFAN_fCurveA_Rpm1;
    const uint16_t f1 = useB ? ParamFAN_fCurveB_Fl1 : ParamFAN_fCurveA_Fl1;
    const uint16_t r2 = useB ? ParamFAN_fCurveB_Rpm2 : ParamFAN_fCurveA_Rpm2;
    const uint16_t f2 = useB ? ParamFAN_fCurveB_Fl2 : ParamFAN_fCurveA_Fl2;
    const uint16_t rM = useB ? ParamFAN_fCurveB_RpmM : ParamFAN_fCurveA_RpmM;
    const uint16_t fM = useB ? ParamFAN_fCurveB_FlM : ParamFAN_fCurveA_FlM;

    if (rM == 0) return 0; // Kennlinie nicht hinterlegt (K-10)

    uint32_t flow;
    if (rpm >= rM)
    {
        flow = fM; // keine Extrapolation, es wird begrenzt (K-9)
    }
    else if (r2 > 0 && rpm >= r2)
    {
        flow = f2 + (uint32_t)(fM - f2) * (rpm - r2) / (rM - r2);
    }
    else if (r1 > 0 && rpm >= r1)
    {
        flow = f1 + (uint32_t)(f2 - f1) * (rpm - r1) / (r2 - r1);
    }
    else
    {
        // Vom impliziten Nullpunkt (0,0) zum ersten hinterlegten Punkt
        const uint16_t rTo = (r1 > 0) ? r1 : ((r2 > 0) ? r2 : rM);
        const uint16_t fTo = (r1 > 0) ? f1 : ((r2 > 0) ? f2 : fM);
        flow = (rTo == 0) ? 0 : (uint32_t)fTo * rpm / rTo;
    }

    // Vorzeichen: positiv = Zuluft. Richtung A gilt als Zuluft, Invertieren dreht die Konvention.
    bool negative = dirB;
    if (ParamFAN_fFlowInvert) negative = !negative;

    // DPT 13.002 hat die Auflaesung 0,0001 m3/h -> Rohwert ist m3/h * 10000.
    const int32_t raw = (int32_t)flow * 10000;
    return negative ? -raw : raw;
}

Fan::Fault FanChannel::activeFault() const
{
    if (!_enableLatched) return Fan::Fault::EnableMissing;
    if (_masterTimeout) return Fan::Fault::MasterTimeout;
    if (_configFault) return Fan::Fault::Config;
    if (_invalidValue) return Fan::Fault::InvalidValue;
    if (_blocked) return Fan::Fault::NoRotation;
    if (_dewNoData) return Fan::Fault::DewPointNoData;
    if (_dewBlocking) return Fan::Fault::DewPointBlocked;
    if (_suspended) return Fan::Fault::MonitoringSuspended;
    return Fan::Fault::None;
}

bool FanChannel::isAlarm(Fan::Fault fault)
{
    // Ein sperrender Taupunktwaechter ist Normalbetrieb, kein Alarm. Fehlende Messwerte dagegen
    // schon: dann arbeitet der Waechter blind.
    return fault != Fan::Fault::None &&
           fault != Fan::Fault::MonitoringSuspended &&
           fault != Fan::Fault::DewPointBlocked;
}

void FanChannel::updateStatusLed()
{
    // Der Benutzer entscheidet in der ETS, ob eine Info-LED diesen Kanal zeigt. Ist keine
    // zugewiesen, gibt es hier nichts zu tun - und wir fassen fremde LEDs nicht an.
    OpenKNX::Led::FunctionGroup *led = openknx.ledFunctions.get(Fan::LedFunctionBase + channelIndex());
    if (led == nullptr || !led->active()) return;

    if (isAlarm(activeFault()))
    {
        led->color(Fan::LedLevel, 0, 0);
        led->on(OpenKNX::Led::Capability::COLOR);
        return;
    }

    // Stillstand ist der haeufigste Zustand einer Lueftung. Dunkel statt Dauerlicht, damit eine
    // brennende LED tatsaechlich "der Luefter laeuft" bedeutet.
    if (_driveAct == 0)
    {
        led->off();
        return;
    }

    // Foerderrichtung A gruen, Richtung B blau. Nicht reversierbare Luefter kennen nur A.
    if (_direction == Fan::Direction::B)
        led->color(0, 0, Fan::LedLevel);
    else
        led->color(0, Fan::LedLevel, 0);
    led->on(OpenKNX::Led::Capability::COLOR);
}

// ===========================================================================
// Ablauf
// ===========================================================================

void FanChannel::updateEnable()
{
    const uint16_t watch = ParamFAN_fEnableWatchTime;
    if (watch == 0) return; // Ueberwachung abgeschaltet

    // Solange die Freigabe nie empfangen wurde, ist das Objekt offensichtlich nicht verknuepft
    // und die Anlage laeuft ohne Freigabe-Konzept. Nach dem ersten Telegramm bleibt die
    // Ueberwachung dauerhaft aktiv, auch ueber Neustarts hinweg (E-1f, siehe restore()).
    if (!_enableWatchRunning) return;

    if (_enableLatched && (millis() - _lastEnableSeen) > (uint32_t)watch * 60000)
    {
        _enableLatched = false;
        _persistDirty = true;
        logInfoP("Freigabe-Ueberwachungszeit abgelaufen, gesperrt");
    }
}

void FanChannel::updateRole()
{
    if (_isMaster) return;

    // Ueberwachungszeit in Sekunden: sie muss unterhalb einer Zykluszeit bleiben, sonst
    // foerdert ein reversierender Knoten ohne Takt so lange in eine Richtung, dass die
    // Gebaeudebilanz kippt, bevor die Abschaltung greift. In Minuten war das nicht darstellbar.
    const uint16_t watch = ParamFAN_fMasterWatchTime;
    if (watch == 0 || _lastMasterSeen == 0) return;

    // Bis zum Ablauf gilt der letzte Zustand, danach abschalten und melden.
    if (!_masterTimeout && (millis() - _lastMasterSeen) > (uint32_t)watch * 1000)
    {
        _masterTimeout = true;
        logInfoP("Lebenszeichen des Masters ausgeblieben");
    }
}

void FanChannel::applyOutput()
{
    const uint32_t now = millis();
    const uint8_t power = targetPower();
    const bool wantRun = isActive() && !_configFault && _enableLatched && !_suspended && power > 0;
    const Fan::Direction want = desiredDirection();

    switch (_state)
    {
        case Fan::State::Off:
            if (wantRun)
            {
                _direction = want;
                _state = (ParamFAN_fPulseLevel > 0 && ParamFAN_fPulseTime > 0)
                             ? Fan::State::StartPulse
                             : Fan::State::Running;
                _stateSince = now;
            }
            break;

        case Fan::State::StartPulse:
            if (!wantRun)
            {
                _state = Fan::State::Off;
                _stateSince = now;
            }
            else if (want != _direction)
            {
                _pendingDirection = want;
                _state = Fan::State::DeadTime;
                _stateSince = now;
            }
            else if ((now - _stateSince) >= ParamFAN_fPulseTime)
            {
                _state = Fan::State::Running;
                _stateSince = now;
            }
            break;

        case Fan::State::Running:
            if (!wantRun)
            {
                _state = Fan::State::Off;
                _stateSince = now;
            }
            else if (want != _direction)
            {
                _pendingDirection = want;
                _state = Fan::State::DeadTime;
                _stateSince = now;
            }
            break;

        case Fan::State::DeadTime:
            if (!wantRun)
            {
                _state = Fan::State::Off;
                _stateSince = now;
            }
            else if ((now - _stateSince) >= (uint32_t)ParamFAN_fDeadTime)
            {
                _direction = _pendingDirection;
                _state = (ParamFAN_fPulseLevel > 0 && ParamFAN_fPulseTime > 0)
                             ? Fan::State::StartPulse
                             : Fan::State::Running;
                _stateSince = now;
            }
            break;
    }

    switch (_state)
    {
        case Fan::State::Off:
        case Fan::State::DeadTime:
            _driveAct = 0;
            _powerAct = 0;
            _hw.stop();
            break;

        case Fan::State::StartPulse:
            // S-2: der Puls ist eine Stellgroesse und erscheint nicht in der Leistungsrueckmeldung.
            _driveAct = ParamFAN_fPulseLevel;
            _powerAct = power;
            _hw.drive(_direction, _driveAct);
            break;

        case Fan::State::Running:
            _driveAct = powerToDrive(power);
            _powerAct = power;
            _hw.drive(_direction, _driveAct);
            break;
    }

    // Blockiererkennung nach S-7/S-8: nur im Dauerlauf, nicht im Anlaufpuls oder Stillstand.
    if (_hasTacho && _state == Fan::State::Running)
    {
        if ((now - _blockWindowStart) >= Fan::BlockWindowMs)
        {
            if (_hw.speedPulses() == _blockWindowPulses)
            {
                if (_emptyWindows < 0xFF) _emptyWindows++;
                if (_emptyWindows >= Fan::BlockWindowsToFault && !_blocked)
                {
                    _blocked = true;
                    logErrorP("Angesteuert, aber keine Drehzahl");
                }
            }
            else
            {
                _emptyWindows = 0;
            }
            _blockWindowStart = now;
            _blockWindowPulses = _hw.speedPulses();
        }
    }
    else
    {
        _blockWindowStart = now;
        _blockWindowPulses = _hw.speedPulses();
        _emptyWindows = 0;
    }

    // Betriebsstunden
    if ((now - _lastSecondTick) >= 1000)
    {
        _lastSecondTick += 1000;
        if (_driveAct > 0) _runSeconds++;
    }
}

void FanChannel::runMaster()
{
    if (!_isMaster) return;

    const uint32_t now = millis();

    // Taktgeber
    if (_type == Fan::ChannelType::Reversible)
    {
        const uint32_t cycleMs = (uint32_t)ParamFAN_fCycleTime * 1000;
        if (cycleMs > 0 && (now - _cycleStarted) >= cycleMs)
        {
            _tact = !_tact;
            _cycleStarted = now;
            KoFAN_Tact.value(_tact, DPT_Bool);
            KoFAN_CycleRest.value((uint16_t)ParamFAN_fCycleTime, DPT_TimePeriodSec);
        }
    }

    // Lebenszeichen; im selben Zyklus Takt, Leistung und Richtungsart erneut senden,
    // damit ein zurueckkehrender Knoten ohne Leseweg wieder mitlaeuft.
    const uint32_t gapMs = (uint32_t)ParamFAN_fMasterSendGap * 60000;
    if (gapMs > 0 && (now - _lastMasterSend) >= gapMs)
    {
        _lastMasterSend = now;

        uint8_t group = groupPower();
        if (_boostUntil != 0 && (int32_t)(_boostUntil - now) > 0)
            group = ParamFAN_fBoostLevel;

        KoFAN_AliveGroup.value(true, DPT_State);
        KoFAN_PowerGroup.value(group, DPT_Scaling);

        if (_type == Fan::ChannelType::Reversible)
        {
            KoFAN_Tact.value(_tact, DPT_Bool);

            // Bei fester Vorgabe verteilt der Master die Richtungsart selbst; kommt sie ueber
            // das KO, liegt sie ohnehin schon fuer alle auf derselben Gruppenadresse.
            if ((Fan::DirModeSel)ParamFAN_fDirModeSel != Fan::DirModeSel::ViaKo)
                KoFAN_DirMode.value((uint8_t)_dirMode, DPT_Value_1_Ucount);
        }
    }
}

// ===========================================================================
// Senden
// ===========================================================================

bool FanChannel::passesDeadband(int32_t value, int32_t lastSent, uint8_t percentBand, uint16_t absBand) const
{
    if (lastSent == INT32_MIN) return true; // noch nie gesendet

    const int32_t diff = (value > lastSent) ? (value - lastSent) : (lastSent - value);
    if (diff == 0) return false;

    // Relativ ODER absolut: der absolute Sockel wirkt dort, wo die relative Schwelle
    // in der Naehe von 0 beliebig klein wuerde.
    if (absBand > 0 && diff >= (int32_t)absBand) return true;

    if (percentBand > 0)
    {
        const int32_t ref = (lastSent < 0) ? -lastSent : lastSent;
        if (ref > 0 && diff * 100 >= ref * (int32_t)percentBand) return true;
    }

    return (percentBand == 0 && absBand == 0);
}

void FanChannel::publish()
{
    const uint32_t now = millis();
    const uint32_t minGapMs = (uint32_t)ParamFAN_fMinSendGap * 1000;

    if (_powerAct != _lastSentPower)
    {
        _lastSentPower = _powerAct;
        KoFAN_PowerAct.value(_powerAct, DPT_Scaling);
        KoFAN_Running.value(_driveAct > 0, DPT_State);
        if (_type == Fan::ChannelType::Reversible)
            KoFAN_DirAct.value(_direction == Fan::Direction::B, DPT_Bool);
    }

    const uint8_t fault = (uint8_t)activeFault();
    if (fault != _lastSentFault)
    {
        _lastSentFault = fault;
        KoFAN_Fault.value(isAlarm((Fan::Fault)fault), DPT_Alarm);
        KoFAN_FaultCode.value(fault, DPT_Value_1_Ucount);
    }

    if (_hasTacho)
    {
        const int32_t rpm = _hw.rpm();
        if ((minGapMs == 0 || (now - _lastSentRpmAt) >= minGapMs) &&
            passesDeadband(rpm, _lastSentRpm, ParamFAN_fRpmBandPct, ParamFAN_fRpmBandAbs))
        {
            _lastSentRpm = rpm;
            _lastSentRpmAt = now;
            KoFAN_Rpm.value((uint16_t)rpm, DPT_Value_2_Ucount);
        }

        if (ParamFAN_fCurveA_RpmM > 0)
        {
            const int32_t flow = rpmToFlow(_hw.rpm());
            // Totband in m3/h, der Rohwert ist um 10000 skaliert.
            if ((minGapMs == 0 || (now - _lastSentFlowAt) >= minGapMs) &&
                passesDeadband(flow / 10000, _lastSentFlow / 10000,
                               ParamFAN_fFlowBandPct, ParamFAN_fFlowBandAbs))
            {
                _lastSentFlow = flow;
                _lastSentFlowAt = now;
                KoFAN_Flow.value(flow, DPT_FlowRate_m3_per_h);
            }
        }
    }
}

void FanChannel::updateDiagnostics()
{
    // Diese vier Werte werden ausschliesslich ins KO geschrieben, nie gesendet. Damit
    // beantwortet das Geraet eine Leseanforderung mit dem aktuellen Stand, ohne dafuer
    // zyklisch Bustelegramme zu erzeugen.
    const uint32_t now = millis();

    if (!_publishedOnce)
    {
        _publishedOnce = true;
        _lastRunHoursWrite = now;
        _lastCycleRestWrite = now;
        _lastWrittenSuspended = _suspended ? 1 : 0;
        KoFAN_IsSuspended.valueNoSend(_suspended, DPT_State);
        KoFAN_RunHours.valueNoSend(_runSeconds / 3600, DPT_Value_4_Ucount);
        if (!_isMaster)
        {
            _lastWrittenMasterOk = _masterTimeout ? 0 : 1;
            KoFAN_MasterOk.valueNoSend(!_masterTimeout, DPT_State);
        }
    }

    // Betriebsstunden bewegen sich langsam - alle 30 Minuten nachfuehren genuegt.
    if ((now - _lastRunHoursWrite) >= Fan::RunHoursWriteMs)
    {
        _lastRunHoursWrite = now;
        KoFAN_RunHours.valueNoSend(_runSeconds / 3600, DPT_Value_4_Ucount);
    }

    // Die Master-Erreichbarkeit prueft updateRole() ohnehin in jedem Durchlauf; hier folgt
    // nur das Objekt dem geprueften Zustand.
    if (!_isMaster)
    {
        const uint8_t ok = _masterTimeout ? 0 : 1;
        if (ok != _lastWrittenMasterOk)
        {
            _lastWrittenMasterOk = ok;
            KoFAN_MasterOk.valueNoSend(ok != 0, DPT_State);
        }
    }

    // Die Suspendierung aendert sich nicht von selbst, sondern nur durch ein Telegramm auf
    // "Suspendieren" - deshalb kein Intervall, sondern ein Abgleich beim Wechsel.
    const uint8_t suspended = _suspended ? 1 : 0;
    if (suspended != _lastWrittenSuspended)
    {
        _lastWrittenSuspended = suspended;
        KoFAN_IsSuspended.valueNoSend(_suspended, DPT_State);
    }

    // Restzeit bis zum naechsten Richtungswechsel.
    if (_isMaster && _type == Fan::ChannelType::Reversible &&
        (now - _lastCycleRestWrite) >= Fan::CycleRestWriteMs)
    {
        _lastCycleRestWrite = now;
        const uint32_t cycleMs = (uint32_t)ParamFAN_fCycleTime * 1000;
        const uint32_t elapsed = now - _cycleStarted;
        const uint16_t rest = (cycleMs > elapsed) ? (uint16_t)((cycleMs - elapsed) / 1000) : 0;
        KoFAN_CycleRest.valueNoSend(rest, DPT_TimePeriodSec);
    }
}

void FanChannel::loop()
{
    if (!isActive()) return;

    if (overrideActive() && (int32_t)(millis() - _testUntil) >= 0)
    {
        logInfoP("Testuebersteuerung abgelaufen, zurueck auf Regelbetrieb");
        releaseOverride();
    }

    updateEnable();
    updateRole();
    updateDewGuard();
    applyOutput();
    runMaster();
    publish();
    updateDiagnostics();
    updateStatusLed();
}

void FanChannel::setup1()
{
    // Der Interrupt landet auf dem Core, der ihn registriert - deshalb hier und nicht in
    // setup(). Unabhaengig vom ETS-Parameter: ob es etwas zu tun gibt, entscheidet die Ansteuerung.
    // Ein PWM-Luefter ohne Tacho-Pin tut hier nichts, eine DShot-Ansteuerung braucht den Tick
    // dagegen zwingend - ohne zyklischen Rahmen stellt der Regler ab.
    if (_active) _hw.beginSpeedFeedback();
}

void FanChannel::loop1()
{
    if (_active) _hw.updateSpeedFeedback();
}

// ===========================================================================
// Konsole
// ===========================================================================

namespace
{
    const char *faultName(Fan::Fault fault)
    {
        switch (fault)
        {
            case Fan::Fault::None: return "kein";
            case Fan::Fault::EnableMissing: return "Freigabe fehlt";
            case Fan::Fault::MasterTimeout: return "Master stumm";
            case Fan::Fault::Config: return "Konfiguration";
            case Fan::Fault::InvalidValue: return "ungueltiger Wert";
            case Fan::Fault::NoRotation: return "keine Drehzahl";
            case Fan::Fault::MonitoringSuspended: return "suspendiert";
            case Fan::Fault::DewPointBlocked: return "Taupunkt sperrt";
            case Fan::Fault::DewPointNoData: return "Taupunkt ohne Werte";
        }
        return "?";
    }

    const char *stateName(Fan::State state)
    {
        switch (state)
        {
            case Fan::State::Off: return "Aus";
            case Fan::State::StartPulse: return "Anlaufpuls";
            case Fan::State::Running: return "Lauf";
            case Fan::State::DeadTime: return "Totzeit";
        }
        return "?";
    }
} // namespace

void FanChannel::printStatusLine()
{
    const Fan::Fault fault = activeFault();
    char rpmText[12];
    if (_hasTacho)
        snprintf(rpmText, sizeof(rpmText), "%u", (unsigned)_hw.rpm());
    else
        snprintf(rpmText, sizeof(rpmText), "-");

    logInfoP("Ch%02u %-6s %-9s Soll=%-3u Ist=%-3u Stell=%-3u Ri=%c %-10s n=%-6s Fehler=%u%s",
             (unsigned)(channelIndex() + 1),
             _isMaster ? "Master" : "Slave",
             _type == Fan::ChannelType::Reversible ? "revers." : "1-Richt.",
             (unsigned)_powerSet,
             (unsigned)_powerAct,
             (unsigned)_driveAct,
             _direction == Fan::Direction::B ? 'B' : 'A',
             stateName(_state),
             rpmText,
             (unsigned)fault,
             overrideActive() ? " [TEST]" : "");
}

void FanChannel::printDetail()
{
    const Fan::Fault fault = activeFault();

    logInfoP("Kanal %u", (unsigned)(channelIndex() + 1));
    logIndentUp();
    logInfoP("Rolle:          %s", _isMaster ? "Master" : "Slave");
    logInfoP("Typ:            %s", _type == Fan::ChannelType::Reversible ? "reversierbar" : "eine Richtung");
    logInfoP("Sollwert:       %u %% (Gruppe)", (unsigned)_powerSet);
    logInfoP("Leistung:       %u %%", (unsigned)_powerAct);
    logInfoP("Stellgroesse:   %u %%", (unsigned)_driveAct);
    logInfoP("Richtung:       %c%s", _direction == Fan::Direction::B ? 'B' : 'A',
             _pendingDirection != _direction ? " (Wechsel steht an)" : "");
    logInfoP("Zustand:        %s", stateName(_state));
    logInfoP("Fehler:         %u (%s)", (unsigned)fault, faultName(fault));
    logInfoP("Alarm-Bit:      %s", isAlarm(fault) ? "gesetzt" : "nicht gesetzt");

    if (_hasTacho)
        logInfoP("Drehzahl:       %u 1/min (%u Impulse)", (unsigned)_hw.rpm(), (unsigned)_hw.speedPulses());
    else
        logInfoP("Drehzahl:       kein Tachoeingang");

    logInfoP("Betriebsstunden %u h %u min", (unsigned)(_runSeconds / 3600), (unsigned)((_runSeconds % 3600) / 60));
    logInfoP("Freigabe:       %s%s", _enableLatched ? "erteilt" : "GESPERRT",
             _enableSeenEver ? "" : " (Objekt bisher nie empfangen)");
    logInfoP("Suspendiert:    %s", _suspended ? "ja" : "nein");

    if (_isMaster)
    {
        logInfoP("Takt:           %s", _tact ? "1" : "0");
        if (_dewNoData)
            logInfoP("Taupunkt:       keine brauchbaren Messwerte");
        else if (_dewBlocking)
            logInfoP("Taupunkt:       sperrt, Aussenluft ist die feuchtere");
        else
            logInfoP("Taupunkt:       frei");
    }

    if (overrideActive())
    {
        const uint32_t restMs = _testUntil - millis();
        logInfoP("Testbetrieb:    Leistung=%s Richtung=%s, laeuft ab in %u s",
                 _testPower >= 0 ? std::to_string(_testPower).c_str() : "Regelung",
                 _testDirection >= 0 ? (_testDirection == 1 ? "B" : "A") : "Regelung",
                 (unsigned)(restMs / 1000));
    }
    logIndentDown();
}

void FanChannel::setOverride(int16_t power, int8_t direction)
{
    if (power >= 0) _testPower = power > 100 ? 100 : power;
    if (direction >= 0) _testDirection = direction;
    _testUntil = millis() + Fan::ConsoleOverrideMs;

    if (direction >= 0 && _type != Fan::ChannelType::Reversible)
        logInfoP("Hinweis: Kanal ist nicht reversierbar, die Richtungsvorgabe bleibt ohne Wirkung");

    logInfoP("Testbetrieb: Leistung=%s Richtung=%s fuer %u min",
             _testPower >= 0 ? std::to_string(_testPower).c_str() : "Regelung",
             _testDirection >= 0 ? (_testDirection == 1 ? "B" : "A") : "Regelung",
             (unsigned)(Fan::ConsoleOverrideMs / 60000));
}

void FanChannel::releaseOverride()
{
    _testPower = -1;
    _testDirection = -1;
    _testUntil = 0;
}
