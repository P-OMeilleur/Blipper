/*
 * Blipper firmware - user configuration
 *
 * Target: nice!nano v2 (nRF52840) running the S340 SoftDevice.
 */
#ifndef BLIPPER_CONFIG_H__
#define BLIPPER_CONFIG_H__

/* ---------------------------------------------------------------------------
 * Pins (absolute nRF pin number: P0.xx = xx, P1.xx = 32 + xx)
 * Buttons are wired from the pin to GND; the internal pull-up is used.
 * ------------------------------------------------------------------------- */
#define PIN_BTN_A          17   /* P0.17 - nice!nano pin "017" (Pro Micro D2) */
#define PIN_BTN_B          20   /* P0.20 - nice!nano pin "020" (Pro Micro D3) */
#define PIN_LED            15   /* P0.15 - on-board blue LED, active high      */

/* ---------------------------------------------------------------------------
 * ANT+ Controls "Generic Command" numbers (data page 73)
 * ------------------------------------------------------------------------- */
#define CMD_NONE           (-1)
#define CMD_MENU_UP        0x0000
#define CMD_MENU_DOWN      0x0001
#define CMD_MENU_SELECT    0x0002
#define CMD_MENU_BACK      0x0003
#define CMD_HOME           0x0004
#define CMD_START          0x0020
#define CMD_STOP           0x0021
#define CMD_RESET          0x0022
#define CMD_LENGTH         0x0023
#define CMD_LAP            0x0024

/* Button mapping. If an Edge pages the "wrong way", swap MENU_UP / MENU_DOWN.
 * A long press only exists when *_LONG is not CMD_NONE; otherwise the short
 * command fires immediately on press (lowest latency).                       */
#define BTN_A_SHORT        CMD_MENU_DOWN   /* next page     */
#define BTN_A_LONG         CMD_LAP         /* lap (untested on Edge 840) */
#define BTN_B_SHORT        CMD_MENU_UP     /* previous page */
#define BTN_B_LONG         CMD_NONE

/* ---------------------------------------------------------------------------
 * Timing (the main tick runs at ~50 Hz, 20 ms per tick)
 * ------------------------------------------------------------------------- */
#define TICK_HZ                 50
#define LONG_PRESS_MS           700
#define CMD_QUEUE_TIMEOUT_MS    5000    /* drop a press if the Edge is not found in time */
#define CMD_MAX_RETRIES         3       /* re-sends when the Edge does not ACK          */
#define SLEEP_WHEN_UNPAIRED_S   120     /* System OFF after this long without the Edge   */

/* ---------------------------------------------------------------------------
 * ANT+ identity
 * ------------------------------------------------------------------------- */
#define ANT_CHANNEL             0
#define ANT_NETWORK             0
#define ANT_MANUFACTURER_ID     255     /* 255 = development */
#define ANT_HW_REVISION         1
#define ANT_MODEL_NUMBER        1
#define ANT_SW_REVISION         1

/* ANT+ Controls profile channel parameters */
#define CONTROLS_DEVICE_TYPE    16      /* ANT+ controllable device (the Edge)  */
#define CONTROLS_RF_FREQ        57      /* 2457 MHz                             */
#define CONTROLS_PERIOD         8192    /* 4 Hz                                 */
#define CONTROLS_SEARCH_TIMEOUT 12      /* x 2.5 s = 30 s                       */

/* LF clock: nice!nano v2 has a 32.768 kHz crystal. Set to 0 to use the RC
 * oscillator instead (only needed on boards without the crystal).            */
#define USE_LFXO                1

/* License key + ANT+ network key live in keys.h (not committed). */
#include "keys.h"

#endif
