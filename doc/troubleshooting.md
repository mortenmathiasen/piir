# Troubleshooting

- [The device does not react](#the-device-does-not-react)
- [Transmitter errors](#transmitter-errors)
- [Remote and feature errors](#remote-and-feature-errors)
- [Errors in a remote file](#errors-in-a-remote-file)
- [Getting more information](#getting-more-information)

## The device does not react

irlingo exits with status 0 once it has handed the code to the transmitter. It cannot know whether the device received it. Work through these:

1. **Does the LED light?** Look at it through a phone camera while sending: most cameras show IR as a faint purple flash (some phones filter IR on the main camera; try the front camera). No flash: check the wiring, the transistor and the GPIO pin in the `dtoverlay` line (or `outpin` for the `gpiod` and `pigpio` backends).
2. **Is it pointed at the device, and close enough?** A LED driven directly from a GPIO pin reaches only a short distance; with a transistor and a proper series resistor, several metres. Try from half a metre first.
3. **Is it the right remote?** Many models of a manufacturer share a protocol, but not all. Try other remotes of the manufacturer (`irlingo --list | grep -i brand`), or record your own remote, see [Managing remotes](remotes.md#deriving-a-remote-from-recorded-codes).
4. **Does the device expect repeats or a toggle bit?** Converted LIRC remotes include the repeats of a short press. For RC5/RC6 devices, a key sent twice in a row with the same toggle state counts as one long press; irlingo flips it automatically if the state file can be written (see [Usage](usage.md#remotes-with-a-toggle-bit)).
5. **Air conditioners: are all settings given?** Every code carries the complete state; settings left out are taken from the remote's template, which may be something the device rejects (e.g. a temperature outside the range of the mode).
6. **Compare with the real remote.** If you have an IR receiver, record the real remote and irlingo (e.g. with `ir-ctl --receive`) and compare the durations. `--verbose=DEBUG` shows the symbols irlingo sends.

## Transmitter errors

**`Cannot open /dev/lirc0: No such file or directory`** — the kernel IR transmitter is not enabled. Add `dtoverlay=gpio-ir-tx,gpio_pin=17` (with your pin) to `/boot/firmware/config.txt` and reboot, see [Installation](installation.md#enabling-the-kernel-ir-transmitter). Check with `ls /dev/lirc*` and `dmesg | grep -i ir`.

**`Cannot open /dev/lirc0: Permission denied`** — the user may not write to the device. Run irlingo with `sudo`, or add the user to the group shown by `ls -l /dev/lirc0` (`sudo usermod -aG video $USER`) and log in again.

**`/dev/lirc0 cannot transmit`** — that device is an IR receiver, typically because a `gpio-ir` receiver overlay is enabled too. The transmitter is another `/dev/lircN`; set it in the remote file: `"transmitter": {"device": "/dev/lirc1"}`.

**`/dev/lirc0 cannot set carrier frequency`** or **`cannot set duty cycle`** (warnings) — the driver uses its own carrier. `gpio-ir-tx` supports both; other drivers may not. Most devices accept 38 kHz anyway.

**`Write to /dev/lirc0 failed`** — the driver rejected the code, e.g. because it is too long for it, or the device was removed.

**`Code needs more than 12000 pulses`** (`gpiod`, `pigpio`) — the code is too long for these backends at its carrier frequency. Use the `lirc` backend.

**`Code length greater than 2048`** — the code has more symbols than irlingo supports.

**`gpiod_chip_open`** or other `gpiod_...` errors — the user may not access `/dev/gpiochip*` (add it to the `gpio` group), or the pin is used by another driver (e.g. a `gpio-ir-tx` overlay on the same pin).

**pigpio initialisation errors** — pigpio needs root, the `pigpiod` daemon must not be running at the same time (it holds the hardware), and pigpio does not support the Pi 5.

## Remote and feature errors

**`Cannot find remote 'NAME'`** — no remote of that name in `conf/remotes/` below the current directory or in the installation directory. Check `irlingo --list | grep -i NAME`. The release packages contain only irlingo's own remotes: add others with `sudo irlingo-remotes add MANUFACTURER`, see [Managing remotes](remotes.md#adding-remotes-to-an-installation).

**`Remote 'NAME' is ambiguous, use one of:`** — several manufacturers have a remote with that file name. Use the full name, e.g. `sony/RM-ED011`.

**`No remotes found in ... or ...`** — no remotes are installed. Add some with `irlingo-remotes`, or run irlingo from a directory with a `conf/remotes/` subdirectory.

**`Feature 'NAME'='VALUE' does not match configuration file`** — the remote has no such feature or value. irlingo lists the remote's features and values after the message. Names and values are case sensitive.

**`Missing configfile before feature parameter`** — give `--remote` before the `--feature` options.

**`Feature must be given as NAME=VALUE`** — e.g. `--feature key=KEY_POWER`, without spaces around `=`.

## Errors in a remote file

These matter when you write or edit remotes; see [Remote file format](remote-format.md).

| Message | Cause |
|---------|-------|
| `Cannot load remote 'NAME' from FILE` | the file is not valid JSON; check it with `python3 -m json.tool FILE` |
| `JSON file FILE root not an object` | the file must hold one JSON object |
| `JSON file FILE has no template` | `template` is missing |
| `Symbol definitions configuraiton not found` | `symboldefinitions` is missing |
| `Undefined symbol 'X'` | the code uses a symbol not in `symboldefinitions` |
| `Feature code '...' is longer than the template` (warning) | the extra symbols are cut off; make the template longer |
| `Feature 'NAME'='VALUE' not found in FILE` | a [variant](remote-format.md#variants) has `features` but lacks this value |
| `Override N in FILE needs "when" and "code"` | a malformed override, which is skipped |
| `Variant N in FILE needs "when" and "template"` | a malformed variant, which is skipped |
| `Invalid checksum in FILE` | a checksum is malformed or refers to units beyond the end of the code; the message logged before it names the setting |
| `Unknown plugin 'NAME' in FILE` | the only plugin is `hvac_panasonic` |

## Getting more information

```
sudo irlingo -r REMOTE -f NAME=VALUE --verbose=DEBUG
```

shows the remote file used, the toggle state, the transmitter device and the code sent as symbols; `--verbose=TRACE` also shows checksum results and pulse counts. To see what a remote sends without transmitting, build irlingo from source and use the test helper:

```
echo 'mode=HEAT temperature=21' | build/tests/encode_remote conf/remotes/panasonic/hvac_panasonic.json
```

If you think irlingo sends a wrong code, open an [issue](https://github.com/mortenmathiasen/irlingo/issues) with the command, the output of `--verbose=DEBUG` and, if possible, a recording of the real remote.
