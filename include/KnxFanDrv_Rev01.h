#pragma once

// ============================================================================
// KNXFANDRV Rev 0.1 — eigene Platine
//
// Eine Platine: OpenKNX-BCU-Modul GN100 fuer die Busankopplung, eigener RP2040 (U100),
// zwei GP8413 hinter einem ADuM1250. Vier Stellsignale S1…S4 auf den Klemmen J3…J6,
// Kanal 1 links aussen.
//
// OGM-HardwareConfig wird nicht eingebunden, deshalb stehen die OpenKNX-Systempins hier
// und nicht in einer Hardwaredefinition.
//
// ACHTUNG: Die Pins sind aus dem Controller-Schaltplan Rev 0.1 gelesen. Vor dem ersten
// Flashen an der gelieferten Platine nachpruefen — ein vertauschter UART-Pin kostet nur
// Zeit, ein vertauschter I2C-Pin kann an den DACs etwas anrichten.
// ============================================================================

// --- KNX und OpenKNX-Systempins (Controller-Blatt) --------------------------
#define KNX_UART_NUM            0
#define KNX_UART_TX_PIN         12  // Netz KNX_Tx  -> GN100 Rx
#define KNX_UART_RX_PIN         13  // Netz KNX_Rx  <- GN100 Tx
#define SAVE_INTERRUPT_PIN      14  // KNX_SAVE, fallende Flanke loest Power-Save aus

#define PROG_LED_PIN            22  // PROGLED
#define PROG_LED_PIN_ACTIVE_ON  1
#define PROG_BUTTON_PIN         23  // PROGBTN
#define FUNC1_BUTTON_PIN        24  // FUNC1BTN
#define INFO1_LED_PIN           25  // INFOLED
#define INFO1_LED_PIN_ACTIVE_ON 1

// --- Luefteransteuerung -----------------------------------------------------
// I2C1 auf GPIO2/GPIO3, Pull-ups R4/R5 4k7 sitzen auf der Platine. Dieselben Leitungen
// liegen zusaetzlich auf J110 Pin 7/8.
#define FANDRV_I2C_SDA          2
#define FANDRV_I2C_SCL          3
#define FANDRV_I2C_PORT         i2c1

// Kanal -> DAC-Ausgang:  S1 = U2.VOUT0, S2 = U2.VOUT1, S3 = U3.VOUT0, S4 = U3.VOUT1.
// VOUT0 ist Pin 8, VOUT1 ist Pin 7 — die Nummerierung laeuft NICHT aufsteigend.
// Der ego liegt deshalb immer vollstaendig auf einem Chip: nur dann stellt Register 0x02
// beide Motoren in einem einzigen Frame, und sie laufen beim Richtungswechsel nicht
// auseinander.
#define FANDRV_DAC_ADDR_A       0x58  // U2: S1 (J3), S2 (J4)
#define FANDRV_DAC_ADDR_B       0x59  // U3: S3 (J5), S4 (J6)

#define FANDRV_BOARD_CHANNELS   4     // muss zur ETS-Auswahl FAN_Hardware passen
#define FANDRV_BOARD_ID         1     // 0 = DevPico (2), 1 = Rev 0.1 (4), 2 = 6, 3 = 8, 4 = 10, 5 = 12

// --- Polaritaet der Spannungshaelften ---------------------------------------
// 1 = unter 5,00 V foerdert der Luefter Zuluft, ueber 5,00 V Abluft.
// ANNAHME bis zur Messung M1. Sie deckt sich mit der Vorgabe des ETS-Parameters
// "Zuordnung der Spannungshaelften". Ist sie falsch, heissen Zuluft und Abluft vertauscht —
// unangenehm, aber ungefaehrlich: die Drehzahl stimmt, nur die Richtungsnamen nicht.
// Deshalb hier eine dokumentierte Annahme und kein #error.
#define FANDRV_BELOW_5V_IS_SUPPLY 1

// --- Busformat des 15-Bit-Werts ---------------------------------------------
//
//   ####################################################################
//   #                                                                  #
//   #   NICHT GEMESSEN. Dieser Wert ist eine ANNAHME, kein Messwert.   #
//   #   Gesetzt am 2026-09-23, damit die Firmware ueberhaupt baut,     #
//   #   solange keine Platine da ist.                                  #
//   #                                                                  #
//   #   VOR DEM ERSTEN FLASHEN: Messung P6 durchfuehren und diesen     #
//   #   Wert bestaetigen oder korrigieren. Ist er falsch, wird aus     #
//   #   dem sicheren Zustand 5,00 V die Volllast 10,00 V — der         #
//   #   Unterschied zwischen Stillstand und Vollgas haengt an genau    #
//   #   diesem Bit.                                                    #
//   #                                                                  #
//   ####################################################################
//
// P6 (Referenzdesign 7.1): Register 0x01 = 0x11, dann Kanal 1 nacheinander mit 0x4000 und
// mit 0x8000 beschreiben und TP1 messen. Genau einer der beiden Werte ergibt 5,00 V ±25 mV.
//   0 -> 5,00 V = 0x4000 (Datenblatt)
//   1 -> 5,00 V = 0x8000 (DFRobot-Bibliothek, schiebt um ein Bit nach links)
//
// Nach der Messung: Zahl bestaetigen, Datum eintragen und diesen Kasten durch die
// Herkunftszeile ersetzen — so wie FANDRV_BELOW_5V_IS_SUPPLY es oben vormacht.
#define FANDRV_DAC_LEFT_ALIGNED 0   // ANNAHME 2026-09-23, P6 steht aus

#ifndef FANDRV_DAC_LEFT_ALIGNED
    #error "FANDRV_DAC_LEFT_ALIGNED nicht gesetzt: erst Phase 1 / Messung P6 durchfuehren"
#endif
