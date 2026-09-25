#pragma once

// MrSpieb HW-FanControl board
// https://github.com/mrspieb/HW-FanControl

#define PROG_LED_PIN_ACTIVE_ON 1
#define PROG_LED_PIN 25
#define PROG_BUTTON_PIN 4

#define STATUS_LED_PIN 24

#define KNX_UART_NUM 0
#define KNX_UART_TX_PIN 0
#define KNX_UART_RX_PIN 1

// PWM polarity: non-inverting. The board buffers 3.3V -> 5V with an SN74LVC125A, so the
// fan sees the same level the GPIO drives. FAN_PWM_ACTIVE_LOW stays undefined.

// Four PWM outputs, two per node. S1 and S2 of a node carry the IDENTICAL signal — one
// output per fan of a Maico pair, which always turn the same way. S2 is therefore a mirror
// of S1, not a second direction: the single output already carries speed AND direction
// (mid position = standstill).
#define FAN1_S1_PWM_PIN 9
#define FAN1_S2_PWM_PIN 8
#define FAN2_S1_PWM_PIN 7
#define FAN2_S2_PWM_PIN 6
#define FAN1_SW_PIN 13
#define FAN2_SW_PIN 12

// No tacho inputs on this board
#define FAN1_TACHO_PIN -1
#define FAN2_TACHO_PIN -1

// --- How many fans this board can actually drive ---
// Two nodes, each with a mirrored pair of outputs. Everything the ETS configures beyond this
// has no pins and is reported as a configuration fault. Order matches the ETS channels.
#define FAN_BOARD_CHANNELS 2
// --- Which drive method each output uses ---
// The module knows only IFanHardware; the board decides what is behind it. Same pattern as
// LED_INIT() in OGM-Common: a macro that constructs the objects, expanded inside the module,
// so no include is needed here. Order of the calls is the order of the ETS channels.
//
// This board drives by duty cycle and has no tacho input, so both nodes report no speed
// feedback and the blockage detection stays off.
#define FAN_INIT()                                                            \
    PwmFan::configureShared(configured ? ParamFAN_PwmFreq : 1000);            \
    addHardware(new PwmFan(FAN1_S1_PWM_PIN, FAN1_S2_PWM_PIN,                  \
                           FAN1_SW_PIN, FAN1_TACHO_PIN));                     \
    addHardware(new PwmFan(FAN2_S1_PWM_PIN, FAN2_S2_PWM_PIN,                  \
                           FAN2_SW_PIN, FAN2_TACHO_PIN));
