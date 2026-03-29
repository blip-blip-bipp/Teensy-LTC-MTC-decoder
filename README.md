# Teensy LTC / MTC Decoder

Teensy 4.0 firmware that decodes **analog LTC** from line-in and **USB-MIDI MTC**, and drives two MAX7219 7-segment displays (timecode + source/rate). Valid LTC can also be **re-emitted as USB-MIDI MTC** (full-frame + quarter-frame) for the host.

## Features

### Inputs and routing

- **Analog LTC** via Teensy Audio Shield **line-in** (`AudioAnalyzeLTC` in `analyze_ltc.h`).
- **USB-MIDI MTC** quarter-frame input: decodes timecode and frame rate (24, 25, 29.97, 30).
- **Source priority**: LTC wins whenever it is active; USB MTC is used only when LTC is absent.
- **USB audio pass-through**: stereo line-in is forwarded to the USB audio device (useful for monitoring while the sketch runs).

### Displays

- **Top (CS 9)**: Timecode `HH.MM.SS.FF` with colon dots.
- **Bottom (CS 5)**: Source **ANA** (analog LTC) or **USB**, plus frame rate. **29.97** is shown with decimal points; whole rates use two digits.

### LTC handling

- **FPS from LTC**: Once per second, frame rate is inferred from the highest frame index seen in that second and the **drop-frame** bit (24 / 25 / 29.97 / 30).
- **Reacquire validation**: After loss, the first valid lock ignores an all-zero `00:00:00:00` frame; display updates only on plausible forward frame progression (same, next, or wrap).

### MTC output (from LTC)

- When LTC is the active source, MTC is sent on USB-MIDI derived from the decoded LTC.
- **Full-frame** SysEx is sent when locking, on **time discontinuities**, or when **frame rate** changes; then **quarter-frame** messages follow for the current frame.
- MTC output is **disabled** when LTC is not active (USB MTC does not pass through as generated MTC).

### Loss / idle behavior

- If **both** LTC and MTC are inactive, the **last** timecode **blinks** (on/off ~500 ms); the bottom line refreshes periodically with the last source/rate.

### Startup

- **~3 s splash** on the timecode display (`5318008` in BCD-style digits) while the bottom display runs a short **segment test** animation.

## Hardware

- **MCU**: Teensy 4.0
- **Audio**: Teensy Audio Shield (Rev D); analog LTC into **LINE IN** (mono summed or single channel per your `AudioAnalyzeLTC` / audio graph).
- **Displays**: Two MAX7219 8-digit 7-segment modules:
  - **Timecode display**: CS = 9
  - **Source/rate display**: CS = 5
  - **Shared**: DIN = 11, CLK = 13
- **Note**: Pin 8 is avoided for the second display because it is used for I2S on Teensy 4.0.

## Wiring

| MAX7219 pin | Teensy 4.0 |
|-------------|------------|
| DIN | 11 |
| CLK | 13 |
| CS (display 1 – timecode) | 9 |
| CS (display 2 – source/rate) | 5 |
| VCC, GND | 3.3V / 5V, GND |

Exact power depends on your MAX7219 module; many run on 5V.

## Software / build

- **IDE**: Arduino IDE or PlatformIO with **Teensyduino** (Teensy 4.0 support).
- **USB mode**: MIDI + Audio (USB audio used for line-in pass-through).
- **Libraries / includes**: `Audio`, `SPI`, `usb_midi`, and **`analyze_ltc.h`** — place `analyze_ltc.h` (and its `AudioAnalyzeLTC` implementation) in the sketch folder or otherwise on the include path. The sketch `#include`s it directly.

## Usage

- Power on: splash, then `00:00:00:00` and a default bottom line until LTC or MTC is received.
- Send **LTC** to line-in and/or **MTC** over USB-MIDI; the active source drives the timecode and the bottom display shows **ANA** or **USB** plus rate.
- With **LTC** present, the host can receive **MTC** reflecting that timecode (DAW or MIDI monitor).

## License

See repository for license information.
