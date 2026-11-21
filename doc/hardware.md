# Connecting an IR LED

irlingo sends infrared through an IR LED on a GPIO pin. This guide shows how to
connect one to a Raspberry Pi or an ESP32.

- [Parts](#parts)
- [Two circuits](#two-circuits)
  - [LED directly on the GPIO pin: for testing](#led-directly-on-the-gpio-pin-for-testing)
  - [LED switched by a transistor: recommended](#led-switched-by-a-transistor-recommended)
  - [More range](#more-range)
- [Raspberry Pi](#raspberry-pi)
- [ESP32](#esp32)
- [Finding the right pins](#finding-the-right-pins)
  - [On a Raspberry Pi](#on-a-raspberry-pi)
  - [On an ESP32](#on-an-esp32)
- [Checking that the LED sends](#checking-that-the-led-sends)

## Parts

| Part | Example | Notes |
|------|---------|-------|
| IR LED, 940 nm | Vishay TSAL6200 (wide beam) or TSAL6100 (narrow beam, longer range) | 940 nm is the wavelength almost all remote controls use. The longer leg is the anode (+). |
| NPN transistor | BC337-40 or 2N2222A | Any small NPN transistor that switches at least 100 mA works. |
| Resistor for the LED | 33 Ω (one LED) or 22 Ω (two LEDs in series), 0.25 W | Limits the LED current to about 100 mA, see below. |
| Resistor for the transistor | 1 kΩ | Between the GPIO pin and the transistor's base. |
| Ready-made board (instead) | [ANAVI Infrared pHAT](https://anavi.technology/) for the Raspberry Pi | Transmitter on GPIO 17, nothing to build. |

Never connect an LED without a series resistor, and never between a GPIO pin
and 5 V: either can destroy the LED or the GPIO pin.

## Two circuits

### LED directly on the GPIO pin: for testing

```
GPIO ───[ R ]───►|─── GND
                LED
           (anode on the resistor side)
```

The GPIO pin powers the LED itself. This is quick to build and enough to check
that everything works, but depending on hardware, the current a GPIO pin can
deliver gives a range of only about a metre.

| Board | R | Current |
|-------|---|---------|
| Raspberry Pi (at most 16 mA per pin) | 220 Ω | about 9 mA |
| ESP32 (20 mA per pin recommended) | 100 Ω | about 20 mA |

### LED switched by a transistor: recommended

```
5 V ───[ 33 Ω ]───►|───┐
                  LED  │ collector
                       │
GPIO ───[ 1 kΩ ]────── B   NPN (BC337, 2N2222)
                       │
                       │ emitter
GND ───────────────────┘
```

The GPIO pin only switches the transistor; the LED gets its current from the 5 V
supply. With 33 Ω the LED current is about (5 V − 1.35 V LED − 0.2 V
transistor) / 33 Ω ≈ 100 mA, which gives several metres of range. The 1 kΩ base
resistor lets about 2.6 mA flow from the 3.3 V GPIO pin, enough to switch the
transistor fully on.

With two LEDs in series, e.g. pointing in two directions, use 22 Ω instead:
(5 V − 2 × 1.35 V − 0.2 V) / 22 Ω ≈ 100 mA.

The LED is on for well under half the time while sending (the 38 kHz carrier
switches it on and off, and the code has pauses), so a 0.25 W resistor is
enough.

### More range

100 mA is within the continuous rating of common IR LEDs. They accept much
higher pulse currents, and a smaller LED resistor gives more range, but check
the LED's datasheet: air conditioner codes are long (up to a few hundred
milliseconds), so the LED is driven for longer than by a TV remote. A narrow
beam LED (e.g. TSAL6100) also gives more range, at the cost of having to point
it more accurately.

## Raspberry Pi

Every model works, from the Pi 1 and Zero to the Pi 5 and 500. The examples in
this documentation use GPIO 17:

| Signal | GPIO | Physical pin |
|--------|------|--------------|
| 5 V (transistor circuit) | | 2 or 4 |
| GND | | 6 |
| Transmitter | GPIO 17 | 11 |

```
Pi header (top left corner, seen from above, pin 1 is marked)

 3.3V  (1) (2)  5V   ── 5 V to the LED resistor
       (3) (4)  5V
       (5) (6)  GND  ── emitter (or the LED's cathode)
       (7) (8)
  GND  (9) (10)
GPIO17 (11) (12)     ── 1 kΩ to the base (or the LED resistor)
```

Then enable the kernel's IR transmitter on that pin and reboot, see
[Enabling the kernel IR transmitter](installation.md#enabling-the-kernel-ir-transmitter):

```
dtoverlay=gpio-ir-tx,gpio_pin=17
```

Any free GPIO pin works; use its number wherever 17 appears. See
[Finding the right pins](#finding-the-right-pins) for how to find the pins on
your board and which are free.

## ESP32

irlingo's ESPHome component for the ESP32 is being developed. The wiring is the
same as for the Raspberry Pi, and works with ESPHome's own
[remote_transmitter](https://esphome.io/components/remote_transmitter/) today.

| Signal | ESP32 dev board pin |
|--------|---------------------|
| 5 V (transistor circuit) | `5V` or `VIN`, which carries the USB supply when the board is powered over USB |
| GND | `GND` |
| Transmitter | `GPIO4` (or another free output pin, see below) |

ESPHome generates the IR signal with the ESP32's RMT peripheral, which can use
almost any output pin. Avoid:
- GPIO 0, 2, 5, 12 and 15: they decide how the ESP32 starts, and a circuit on
  them can stop it from booting
- GPIO 6 to 11: they connect the flash memory
- GPIO 34 to 39: they are inputs only

In the ESPHome configuration:

```yaml
remote_transmitter:
  id: ir_tx
  pin: GPIO4
  carrier_duty_percent: 50%
```

The board's 3.3 V pin can also power the LED, but it is fed by the board's
voltage regulator, which the Wi-Fi radio needs too. Use 5 V for the transistor
circuit.

## Finding the right pins

Boards differ, so check the pins of your own board before connecting anything.

### On a Raspberry Pi

irlingo and the `dtoverlay` line use **GPIO numbers** (also called BCM
numbers), not the positions of the pins on the header: GPIO 17 is physical
pin 11. All models with a 40-pin header have the same layout.

- Run `pinout` on the Pi. It prints the header of your exact model with every
  pin's GPIO number. It is part of [gpiozero](https://gpiozero.readthedocs.io/en/stable/cli_pinout.html),
  which Raspberry Pi OS includes; elsewhere install it with
  `sudo apt install python3-gpiozero`.
- [pinout.xyz](https://pinout.xyz/) shows the same as an interactive map, with
  what each pin is used for by common add-on boards.
- The official Raspberry Pi documentation's section
  [40-pin GPIO header](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio)
  describes the pins and their voltages.

A pin is free when no interface or add-on board uses it. Pins used by
interfaces when they are enabled:

| Interface | GPIO |
|-----------|------|
| I2C | 2, 3 |
| 1-Wire (default pin) | 4 |
| SPI | 7, 8, 9, 10, 11 |
| Serial console (UART) | 14, 15 |
| I2S audio, PWM audio | 18, 19, 20, 21 (and 12, 13 for PWM) |

Check which are enabled in the `dtparam=` and `dtoverlay=` lines of
`/boot/firmware/config.txt`. On a running Pi, `pinctrl get 17` (current
Raspberry Pi OS) shows what a pin is set to, and `gpioinfo` (package `gpiod`)
lists the pins that drivers have claimed. GPIO 17, 22, 23, 24, 25 and 27 are
free on most setups.

### On an ESP32

First find out which chip your board has: the classic ESP32, ESP32-S2, -S3,
-C3 and -C6 have different pins.

- The metal shield of the module usually names it, e.g. `ESP32-WROOM-32`
  (classic ESP32) or `ESP32-S3-WROOM-1`.
- Or ask the chip over USB with [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/),
  which ESPHome installs: `esptool --port /dev/ttyUSB0 chip-id` prints e.g.
  `Chip type: ESP32-D0WDQ6`.

Then look up the pins:

- **Your board's pinout diagram**, from its maker or seller. The labels printed
  on most boards are GPIO numbers, but some boards (e.g. NodeMCU style ones
  labelled `D1`, `D2`...) use their own names; the diagram translates them.
  For Espressif's own boards see the
  [ESP32-DevKitC user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html)
  and the [other development boards](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/index.html).
- **Which pins are safe to use**: the [ESP-IDF GPIO reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html)
  lists the strapping, flash and input-only pins for each chip (choose your chip
  at the top of the page), and the
  [ESP32 pinout reference](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)
  at Random Nerd Tutorials explains them with a table of good choices.
- [ESPHome's ESP32 page](https://esphome.io/components/esp32/) describes the
  board and framework settings for each chip.

ESPHome also checks the pin when compiling. A strapping pin gives a warning,
e.g. `GPIO2 is a strapping pin and should only be used for I/O with care`; an
input-only pin (`GPIO35 (34-39) does not support output pin mode`) or a flash
pin (`This pin cannot be used on ESP32s and is already used by the flash
interface`) is an error.

## Checking that the LED sends

Look at the LED through a phone camera while sending: most cameras show
infrared as a faint purple flash. Some phones filter infrared on the main
camera; try the front camera.

- Raspberry Pi: send with `irlingo --remote ... --feature ... --verbose INFO`,
  see [Troubleshooting](troubleshooting.md) if nothing happens.
- ESP32: watch the device's log while it sends, e.g. with
  `esphome logs your-device.yaml`.

No flash: check the LED's direction (the longer leg towards the resistor or
5 V), the transistor's pins (they differ between transistor types; check its
datasheet) and the GPIO pin in the configuration.
