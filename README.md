# Teensy LTC / MTC Decoder

Teensy 4.0 firmware that decodes **analog LTC** from line-in and **USB-MIDI MTC**, and drives two MAX7219 7-segment displays (timecode + source/rate).

## Features

- **Dual source**: Analog LTC (via Audio Shield line-in) and USB-MIDI MTC.
- **Dual display**: Top = timecode (HH.MM.SS.FF); bottom = source label (ANA / USB) + frame rate (24, 25, 29.97, 23.97, 30).
- **USB-MIDI**: MTC quarter-frame in (display + rate decode); MTC out generated from LTC when LTC is active.
- **Blink on loss**: When the active source stops, timecode blinks; when both are absent, last frame blinks.
- **Splash**: Short "5318008" splash on power-up with fade-out.

## Hardware

- **MCU**: Teensy 4.0
- **Audio**: Teensy Audio Shield (Rev D); analog LTC into **LINE IN** (left or right per `USE_RIGHT_CHANNEL`).
- **Displays**: Two MAX7219 8-digit 7-segment modules:
  - **Timecode display**: CS = 9
  - **Source/rate display**: CS = 5
  - **Shared**: DIN = 11, CLK = 13
- **Note**: Pin 8 is avoided for the second display because it is used for I2S on Teensy 4.0.

## Wiring

| MAX7219 pin | Teensy 4.0 |
|-------------|-------------|
| DIN | 11 |
| CLK | 13 |
| CS (display 1 – timecode) | 9 |
| CS (display 2 – source/rate) | 5 |
| VCC, GND | 3.3V / 5V, GND |

Exact power depends on your MAX7219 module; many run on 5V.

## Software / Build

- **IDE**: Arduino IDE or PlatformIO with **Teensyduino** (Teensy 4.0 support).
- **Libraries**: Built-in `Audio`, `SPI`, `EEPROM`, `usb_midi` (Teensy USB type: MIDI).
- **Sketch**: Single `.ino`; you must insert your **LTCFromPCM** (or equivalent) LTC decoder in the marked section (see [LTC decoder](#ltc-decoder) below).

## LTC Decoder

The sketch leaves a placeholder for your **LTCFromPCM** (or similar) class. You must implement or paste your decoder and call it from the main loop, updating `lastLTCms` when a valid LTC frame is received and setting `curFPS` for the current LTC frame rate.

## Usage

- Power on: splash on timecode display, then 00:00:00:00 and "USB 25" (or last source/rate) until LTC or MTC is received.
- Connect LTC to line-in and/or send MTC over USB-MIDI; the active source drives the timecode and the bottom display shows "ANA" or "USB" plus rate.
- When the active source stops, the last timecode blinks.

## License

See repository for license information.
