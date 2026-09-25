# OFM-FanControl

OpenKNX function module for decentralised ventilation with reversing fans. Provides the fan
channels; it is embedded into an application such as
[OAM-FanControl](https://github.com/cad435/OAM-FanControl).

## What it does

One channel is one fan node. Nodes form a group over shared group addresses with one master
that produces the power setpoint, the reversing tact and a keep-alive; the slaves apply that
setpoint with their own limits. Both roles are the same code, selected by the "Ist Master"
parameter.

The master derives the group setpoint from a fixed value, an external object, an internal
P controller or a two-point controller with hysteresis. An optional dew point guard can veto all
of them: it compares the dew point inside and outside and stops ventilation while the outside air
is the more humid one.

Reversing fans carry speed **and** direction on a single output: 0 % is full speed in
direction A, the midpoint is standstill, 100 % is full speed in direction B. Non-reversible
fans are driven conventionally. Each node derives its direction from its phase assignment and
the master's tact — direction is never commanded directly.

On devices with RGB status LEDs, each channel offers itself as an LED function ("Lüfter 1"…"8")
that the user assigns to an info LED in the BASE module: green for direction A, blue for
direction B, red on fault, dark at standstill, at 25 % brightness.

The USB console exposes the channels for commissioning — `fan st` for a one-line-per-channel
overview and `fan c01 p60` / `fan c01 b` to drive a fan without ETS or a group address. Such a
test overrides the setpoint but not the safety vetoes, and expires by itself after 10 minutes.

See [`doc/Applikationsbeschreibung-Fan.md`](doc/Applikationsbeschreibung-Fan.md) for the user
documentation; it is also the source of the generated ETS context help under
`src/Baggages/Help_de/`.

## Integration

```xml
<op:define prefix="FAN" ModuleType="20"
  share="../lib/OFM-FanControl/src/Fan.share.xml"
  template="../lib/OFM-FanControl/src/Fan.templ.xml"
  NumChannels="2" KoOffset="20">
  <op:verify File="../lib/OFM-FanControl/library.json" ModuleVersion="%ReleaseApplicationVersion%" />
</op:define>
```

```cpp
#include "FanModule.h"
openknx.addModule(1, openknxFanModule);
```

The module needs 32 KO numbers per channel and 80 bytes of parameter memory per channel. The
hardware layer is behind `IFanHardware`, which describes *what* is driven, not *how*: it knows
no pins, no frequencies and no protocol. The device header constructs the implementations in a
`FAN_INIT()` macro — the same pattern OGM-Common uses for `LED_INIT()` — so adding a drive method
touches neither the module nor the channel logic.

## Structure

| File | Purpose |
|---|---|
| `FanModule.*` | module, channel container, flash persistence |
| `FanChannel.*` | one node: state machine, setpoint, monitoring, feedback |
| `FanTypes.h` | enums and timing constants |
| `IFanHardware.h` | drive abstraction: drive/stop/midpoint plus speed feedback |
| `FanHardware/PwmFan.*` | drive by duty cycle, bipolar mapping, mirrored output, tacho input |
| `FanHardware/DShotFan.*` | bidirectional DShot over the RP2040 PIO block — reference implementation, no board uses it |
| `FanHardware/PWMFanTachoReader.*` | helper of `PwmFan`: pulse counting and rpm for the tacho input |
| `Fan.share.xml` | module-wide ETS definition |
| `Fan.templ.xml` | per-channel ETS definition |

## License

GNU General Public License v3.0.
