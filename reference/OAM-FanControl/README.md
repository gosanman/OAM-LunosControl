# OpenKNX FanControl

KNX application for decentralised ventilation with reversing fans. A device drives up to eight
fan nodes — as many as its board has outputs; several nodes form a group over shared group
addresses, coordinated by one master.

> **Status: development.** The KO and parameter layout is frozen, but the application number is
> still provisional.

## Concept

A node is a fan plus its drive electronics, and one ETS channel. Exactly one node in a group is
the **master**: it produces the power setpoint, sets the reversing tact and sends a keep-alive.
Every other node is a **slave** that applies the group setpoint with its own limits and share
factor. Both roles run the same firmware and are distinguished by a single parameter.

Direction is never commanded directly. Each node derives its own airflow direction from its
phase assignment and the master's tact, so half the group runs supply air while the other half
runs exhaust air, and they swap on every cycle.

**The master may also sit outside the device.** A node cannot tell whether its power setpoint
came from a master, from a hand switch or from a controller, so leaving every channel as a slave
and giving each power object its own group address turns the device into eight independently
controllable fans, driven by a visualisation, a server or a logic block. Nothing needs to be
switched over for that — see *Betriebsfälle* in the application description.

![Group topology: one master and three slaves on shared group addresses, phase and counter-phase
moving air in opposite directions](references/%C3%9Cbersicht.drawio.png)

*Diagram in German. Source: [`references/Übersicht.drawio`](references/Übersicht.drawio).*

### Signal flow per channel

Both roles run the same firmware and the same seven processing stages. What differs is where the
setpoint comes from and which objects are sent rather than received.

A channel configured as **master** produces the group setpoint, the tact and the keep-alive:

![Master channel: inputs, seven processing stages, outputs, including the group objects the
master sends cyclically](references/IO-Uebersicht-Master.drawio.png)

The same channel as **slave** receives all of that on the same group addresses:

![Slave channel: the group objects are receive objects here, direction is derived from the
received tact and the channel's own phase assignment](references/IO-Uebersicht-Slave.drawio.png)

*Diagrams in German. Source: [`references/IO-Uebersicht.drawio`](references/IO-Uebersicht.drawio).
KO numbers are relative to the channel — channel 1 = KO 20…51, channel 2 = KO 52…83, up to
channel 8 = KO 244…275.*

## Features

- **8 fan channels**, each switched on or off individually in the channel selection tab and
  carrying its own mode: reversible (two airflow directions, tact) or non-reversible. How many
  channels really have outputs is a property of the board, not of the ETS — activating more is
  reported as a fault at startup.
- **Bipolar drive** for reversing fans: speed and direction on a single output. 0 % is full
  speed in direction A, the midpoint is standstill, 100 % is full speed in direction B. The
  midpoint is the safe state. Non-reversible fans are driven conventionally.
- **Setpoint sources** — fixed value, external communication object, an internal P controller
  on CO₂ or relative humidity, or a two-point controller with hysteresis.
- **Dew point guard** (master only, optional) — compares the dew point inside and outside and
  stops ventilation while the outside air is the more humid one, so airing does not carry
  moisture in. Reports fault code 7 without raising the alarm bit, because that is intended
  operation rather than a defect.
- **Per-node scaling** — share factor plus minimum and maximum drive level per direction.
- **Start pulse** to break static friction, and a dead time before every direction change.
- **Boost ventilation** as a master function, with its own runtime and power level.
- **Two-stage monitoring** — a self-holding, power-fail-persistent enable latch, and a master
  watchdog that holds the last state until it expires.
- **Feedback** — speed, volume flow via a configurable curve (signed: positive is supply air),
  running state, direction, operating hours, fault and fault code.
- **Blockage detection** — two consecutive 5 s windows without tacho pulses while the fan is
  commanded to run.
- **Status LEDs** — one RGB LED per fan on boards that have them: green for direction A, blue for
  direction B, red on fault, dark at standstill, at 25 % brightness. Which LED shows which fan is
  chosen in the BASE module.
- **Console commands** for commissioning — read every channel's state and drive a fan without ETS
  or a group address. The override respects the safety vetoes and expires after 10 minutes.
- **Logic module** included with 30 channels.

KNX is not a safety bus. Interlocking with a fireplace additionally requires a hard-wired,
potential-free contact.

## Documentation

[`../OFM-FanControl/doc/Applikationsbeschreibung-Fan.md`](https://github.com/cad435/OFM-FanControl/blob/dev/doc/Applikationsbeschreibung-Fan.md)
is the user documentation, parameter by parameter, and the source of the ETS context help.
Start there.

The two documents in [`references/`](references/) are a pair, both in German:

- [`Anforderungen_Dezentrale_Lueftersteuerung.md`](references/Anforderungen_Dezentrale_Lueftersteuerung.md)
  — the original concept paper, written from the perspective of the trades that build and run
  the system, deliberately without any knowledge of the KNX bus.
- [`Review_Anforderungen_KNX-Sicht.md`](references/Review_Anforderungen_KNX-Sicht.md)
  — **where the software deviates from it, and why.**

## Hardware

The reference hardware is the
**[OpenKNX REG1-FanAktor-2x](https://github.com/cad435/OpenKNX-REG1-App-2xFan)** — an addon board
for the [OpenKNX REG1](https://github.com/OpenKNX/OpenKNX-REG1) module system, DIN rail, 1TE. It
is the only variant with a tacho input, so speed feedback, volume flow and blockage detection are
available there.

The [MrSpieb HW-FanControl](https://github.com/mrspieb/HW-FanController) board is the original
this application was written for and stays supported.

Which board is built is a compile-time decision, not an ETS option — PWM polarity and the number
of outputs differ. The ETS application is identical for both.

The REG1 variant does not spell out the controller and front pins itself: it selects them from
[OGM-HardwareConfig](https://github.com/OpenKNX/OGM-HardwareConfig) with two macros, so KNX UART,
save interrupt, prog button and the four WS2812B status LEDs stay correct when that definition is
corrected upstream. Only the addon board's own fan pins live in the device header, because
upstream does not know that board.

| | REG1-FanAktor-2x (reference) | HW-FanControl (original) |
|---|---|---|
| Fan outputs | 2 | 2 nodes, each mirrored (identical signal, one fan each) |
| Tacho input | yes, opto-coupled, 2 pulses per revolution | no |
| PWM polarity | inverted (level shifter driving an NMOS with pull-up) | not inverted |
| Build environment | `develop_RP2040` | `develop_RP2040_MrSpieb` |

Target fan types: Maico PPB series and Fawas Air Solitaire 160 (ebm-papst AxiRev 126). Any
reversing fan that takes speed and direction on one PWM input should work.

MCU is an RP2040. The KNX transceiver is an NCN5120.

Note on the PWM pull-up: most off-the-shelf fans have one built in. OEM variants in integrated
ventilation systems often do not, and their reference voltage differs by manufacturer — the
REG1 board carries solder bridges for that case, see its documentation.

## Building

Dependencies are cloned to the parent directory and symlinked into `lib/` by
`restore/Restore-Dependencies.ps1`. It needs an **elevated** PowerShell, because creating
directory symlinks on Windows requires administrator rights.

```powershell
# once, from an elevated PowerShell
cd restore; .\Restore-Dependencies.ps1

# ETS product database and knxprod.h
& (Join-Path $env:USERPROFILE "bin/OpenKNXproducer.exe") create -h include/knxprod.h src/Fan.xml

# firmware, one of the two variants
platformio run -e develop_RP2040
platformio run -e develop_RP2040_MrSpieb
```

`include/knxprod.h` is generated — never edit it by hand. Re-run the producer after any change
to `src/Fan.xml` or the module XMLs, and read *its* output first when a build suddenly fails
with unknown identifiers.

## Contributing

Contributions are welcome.

## License

GNU General Public License v3.0, see the LICENSE file.
