# irlingo

[![CI](https://github.com/mortenmathiasen/irlingo/actions/workflows/ci.yml/badge.svg)](https://github.com/mortenmathiasen/irlingo/actions/workflows/ci.yml)

irlingo (formerly PIIR) is a command line tool to transmit IR-signals like remote controls do. irlingo is created as a generic tool that supports configuration and transmission of any signal, eg. including long aircondition signals.

![alt text](https://github.com/mortenmathiasen/irlingo/blob/master/doc/remote-sommerhus.png?raw=true)

Examples:
```
irlingo --remote sony/RM-ED011 --feature key=KEY_POWER

irlingo --remote hvac_panasonic --feature temperature=21 --feature mode=HEAT
```

## Documentation

The [documentation](doc/README.md) has a guide for each step:

* [Installation](doc/installation.md): hardware, packages, enabling the IR transmitter, building from source
* [Connecting an IR LED](doc/hardware.md): circuits, parts and pins for a Raspberry Pi or an ESP32
* [irlingo on ESPHome](doc/esphome.md): sending irlingo remotes from an ESP32 (preview)
* [Usage](doc/usage.md): finding a remote, sending keys and air conditioner settings
* [Managing remotes](doc/remotes.md): adding remotes, converting LIRC files, deriving remotes from recordings
* [Remote file format](doc/remote-format.md): every setting of a remote, with a worked example
* [Integration](doc/integration.md): scripts, cron, Home Assistant, Node-RED
* [Troubleshooting](doc/troubleshooting.md): error messages and what to do
* [Development](doc/development.md): tests, coverage and releases

## Table of contents

  * [Documentation](#documentation)
  * [Features](#features)
    + [IR-protocol](#ir-protocol)
    + [Supported systems](#supported-systems)
    + [Supported hardware](#supported-hardware)
    + [Prerequisites](#prerequisites)
  * [Getting Started](#getting-started)
    + [Prebuilt packages](#prebuilt-packages)
    + [Compilation](#compilation)
    + [Installation](#installation)
    + [Deployment](#deployment)
      - [Cross compiling for Raspberry Pi](#cross-compiling-for-raspberry-pi)
    + [Testing](#testing)
    + [Releases](#releases)
    + [Upgrading from PIIR 2.x](#upgrading-from-piir-2x)
    + [Upgrading from PIIR 1.x](#upgrading-from-piir-1x)
  * [How to use](#how-to-use)
  * [Remotes](#remotes)
    + [Choosing the remotes of an installation](#choosing-the-remotes-of-an-installation)
    + [Remotes converted from LIRC](#remotes-converted-from-lirc)
    + [Remotes derived from recordings](#remotes-derived-from-recordings)
    + [Toggle bits](#toggle-bits)
    + [Checksums](#checksums)
    + [Overrides](#overrides)
    + [Variants](#variants)
  * [Contributing](#contributing)
  * [Versioning](#versioning)
  * [Authors](#authors)
  * [License](#license)
  * [Acknowledgments](#acknowledgments)

## Features

- Send infrared commands to control aircondition etc.
- Transmit any IR signal using any protocol.
- Integration to home automation and other systems.
- Customization by configuration and plugins.

### IR-protocol

To control an IR-receiver you need to know the IR-signals to transmit to it. Either, you can:
* Use the protocol definitions that exists already inside irlingo's remote folder [conf/remotes](https://github.com/mortenmathiasen/irlingo/tree/master/conf/remotes): more than 3000 remotes from over 500 manufacturers, most of them converted from the [LIRC remote database](https://sourceforge.net/p/lirc-remotes/code/ci/master/tree/remotes), see [Remotes](#remotes)
* Convert a LIRC configuration (`lircd.conf`) you already have with [tools/lirc2irlingo.py](tools/lirc2irlingo.py)
* Derive a remote from recorded codes with [tools/ir2irlingo.py](tools/ir2irlingo.py), see [Remotes derived from recordings](#remotes-derived-from-recordings)
* Reverse engineer your remote control, e.g. using [IR decoder for Arduino](https://github.com/ToniA/Raw-IR-decoder-for-Arduino.git) which I did for [Panasonic airconditioner](https://github.com/mortenmathiasen/irlingo/blob/master/conf/remotes/panasonic/hvac_panasonic.pdf)

### Supported systems

The tool is developed in C to support deployment on any platform. 

### Supported hardware

The tool transmits IR on:
* Raspberry Pi (all models, including Pi 5) using the Linux kernel IR transmitter driver (the default, used by the release packages) or [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/)
* Raspberry Pi 1–4 and Zero models using [pigpio](http://abyz.me.uk/rpi/pigpio/)
* more platforms to come

### Prerequisites

To be able to transmit IR signal you need an IR led, either:
* build the electronics yourself, see [Connecting an IR LED](doc/hardware.md) for the circuit, parts and pins
* apply an premade pHAT, eg. [ANAVI Infrared pHAT](https://anavi.technology/)

## Getting Started

You need to identify a device you want to control by IR-signals, e.g. an airconditioner, a TV or similar.

### Prebuilt packages

Every [release](https://github.com/mortenmathiasen/irlingo/releases) has ready-made packages using the kernel driver (`lirc`) backend. The binaries are statically linked, so they run on any Raspberry Pi OS release:

| Package | Raspberry Pi models | OS |
|---------|---------------------|----|
| `armv6` | Pi 1, Zero, Zero W and every other Pi | 32-bit |
| `armv7` | Pi 2, 3, 4, 400, Zero 2 W | 32-bit |
| `aarch64` | Pi 3, 4, 400, 5, 500, Zero 2 W | 64-bit |

If you are unsure, run `dpkg --print-architecture` on the Pi: `arm64` needs the `aarch64` package, and `armhf` needs `armv6` or `armv7` (`armv6` works on every model).

The packages contain irlingo's own remotes. Add the remotes you need from the LIRC database afterwards with `irlingo-remotes`, see [Choosing the remotes of an installation](#choosing-the-remotes-of-an-installation).

Install the Debian package, e.g. for a Pi Zero W:
```
sudo apt install ./irlingo_3.0.0_armv6.deb
```
or extract the tarball into `/`:
```
sudo tar -xzf irlingo-3.0.0-armv6.tar.gz -C / --strip-components=1
```

Then enable the kernel IR transmitter as described below.

### Compilation

Update package list:
```
sudo apt update
```

By default irlingo sends IR through the Linux kernel's IR transmitter driver, which generates the carrier with accurate timing on every Raspberry Pi model including Pi 5. No extra library is needed. Enable the driver by adding this line to `/boot/firmware/config.txt` and rebooting:
```
dtoverlay=gpio-ir-tx,gpio_pin=17
```
Use the GPIO pin your IR LED is connected to. This creates `/dev/lirc0`. If that is not your transmitter device (for example because an IR receiver overlay is also enabled), set `"device": "/dev/lirc1"` in the `transmitter` section of the remote's JSON file. The user running irlingo must be allowed to write to the device: check its group with `ls -l /dev/lirc0` and add your user to that group, or run irlingo with `sudo`.

Alternatively irlingo can drive the pin itself through [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/) version 2.0 or newer (`sudo apt install -y libgpiod-dev`) or [PIGPIO](http://abyz.me.uk/rpi/pigpio/) (`sudo apt install -y pigpio`, not supported on Pi 5). With libgpiod the carrier is generated from user space, so its timing is much less accurate. With these backends the pin is taken from `outpin` in the remote's JSON file.

Install CMake to enable compilation
```
sudo apt-get install -y cmake build-essential
```

Download and extract latest released irlingo software:
```
curl -s https://api.github.com/repos/mortenmathiasen/irlingo/releases/latest | sed -n 's/.*tarball_url":\s"\(.*\)".*/\1 -O irlingo.tar/p' | xargs wget
mkdir irlingo
tar xf irlingo.tar -C ./irlingo
mv ./irlingo/*/* ./irlingo
```

Now compile it:
```
cd irlingo
cmake -B build
cmake --build build
```

irlingo uses the kernel driver (`lirc`) when the kernel headers provide `linux/lirc.h`, which they do on any Linux system, and otherwise falls back to libgpiod and then pigpio. To pick one explicitly, add `-DIRLINGO_GPIO_BACKEND=lirc`, `gpiod` or `pigpio` to the first `cmake` command.

### Installation

After compilation you can install the irlingo tool by:
```
sudo cmake --install build
```

### Deployment

You can deploy the irlingo tool to any live system. Either you can compile the sourced code directly on the live system as explained above or you can compile it on another development system and then move the  execution file to your live system.

#### Cross compiling for Raspberry Pi

Toolchain files for 64-bit (`aarch64`) and 32-bit (`armhf`) Raspberry Pi OS are in `cmake/`. On a Debian/Ubuntu build machine, install the cross compiler, for example for 64-bit:
```
sudo apt install -y gcc-aarch64-linux-gnu
```

The default `lirc` backend needs nothing beyond the cross compiler. Only for the `gpiod` or `pigpio` backend must irlingo link against the library built for the Pi. Either install the Debian multiarch package:
```
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install -y libgpiod-dev:arm64
```
or copy a sysroot from the Pi (requires `libgpiod-dev` or `pigpio` installed on the Pi):
```
mkdir -p rpi-sysroot/usr
rsync -a pi@raspberrypi:/usr/include pi@raspberrypi:/usr/lib rpi-sysroot/usr/
rsync -a pi@raspberrypi:/lib rpi-sysroot/
```

Then configure and build:
```
cmake -B build-rpi -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi-aarch64.cmake
# or, with a sysroot:
cmake -B build-rpi -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi-aarch64.cmake -DRPI_SYSROOT=$PWD/rpi-sysroot
cmake --build build-rpi
```

Copy `build-rpi/irlingo` to the Pi and the remotes you need from `conf/remotes/` to `/usr/local/share/irlingo/conf/remotes/` (or whatever `CMAKE_INSTALL_PREFIX` you configured with). For 32-bit Raspberry Pi OS use `cmake/toolchain-rpi-armhf.cmake`, `gcc-arm-linux-gnueabihf` and `libgpiod-dev:armhf` instead. That toolchain targets ARMv7, so it does not support the Pi 1 or Pi Zero/Zero W.

For the Pi 1 and Pi Zero/Zero W (ARMv6) use `cmake/toolchain-rpi-armv6.cmake` with `gcc-arm-linux-gnueabi`. It builds a static binary with the `lirc` backend that runs on every 32-bit Pi. For the `gpiod` or `pigpio` backend on ARMv6, compile on the Pi itself.

Add `-DIRLINGO_STATIC=ON` to link statically, so the binary does not depend on the C library version of the Pi's OS. `cpack` in the build directory creates `.tar.gz` and `.deb` packages; configure with `-DCMAKE_INSTALL_PREFIX=/usr` first so the packaged binary finds its remotes in `/usr/share/irlingo`.

### Testing

The unit and command line tests run with CTest:
```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

They cover the configuration loader, remote lookup, checksums, the remote plugins, the transmitter backend selected at configure time, the `irlingo` command line, `irlingo-remotes`, the LIRC converter and `ir2irlingo.py`, which must derive `panasonic/hvac_panasonic` again from the codes it sends. Every key of every remote in `conf/remotes/` is checked to produce a code irlingo can send. The `tests` section of a remote lists features and the code a real remote sends for them, and every one of these vectors is checked. Vectors that do not match yet are listed in `tests/known_failures.txt`. No test transmits IR, so they are safe to run on a Pi.

To measure coverage, configure with `-DIRLINGO_COVERAGE=ON` and run [gcovr](https://gcovr.com) after the tests; the converter is measured too when Python's `coverage` module is installed:
```
cmake -B build-coverage -DIRLINGO_COVERAGE=ON
cmake --build build-coverage
ctest --test-dir build-coverage
gcovr -r . build-coverage --exclude 'components/irlingo/parson\..*' --exclude 'tests/'
```
The transmitter backends are tested against simulated drivers and libraries (the `ioctl` and `write` calls of the `lirc` backend, libgpiod and pigpio are replaced when linking the tests), so the tests cover sending without hardware. CI requires 95% line coverage of the C code and of the converter.

When cross compiling, tests run under qemu if you add for example `-DCMAKE_CROSSCOMPILING_EMULATOR=$(command -v qemu-aarch64)` (from the `qemu-user` package).

GitHub Actions runs the tests for all three backends, with AddressSanitizer and UndefinedBehaviorSanitizer, measures coverage, and runs the tests of the `armv6`, `armv7` and `aarch64` builds under qemu.

### Releases

To release, update `VERSION` in `CMakeLists.txt`, commit, and push a matching tag:
```
git tag v3.1.0
git push origin v3.1.0
```
GitHub Actions then runs all tests, builds the packages for every Raspberry Pi architecture and the archive of all remotes (`irlingo-remotes-<version>.tar.gz`), and publishes them with checksums as a GitHub release.

### Upgrading from PIIR 2.x

irlingo 3.0.0 is PIIR under a new name. Remote files and command line options are unchanged; the names of everything else changed, without aliases for the old ones:

| PIIR 2.x | irlingo 3.0 |
|----------|-------------|
| `piir`, `piir-remotes` | `irlingo`, `irlingo-remotes` |
| package `piir` | package `irlingo`; installing the `.deb` removes the `piir` package |
| `/usr/share/piir/conf/remotes/` (`/usr/local/...` when built from source) | `/usr/share/irlingo/conf/remotes/` |
| `/var/lib/piir/toggle-state`, `~/.local/state/piir/toggle-state` | `/var/lib/irlingo/toggle-state`, `~/.local/state/irlingo/toggle-state` |
| `PIIR_STATE_DIR` | `IRLINGO_STATE_DIR` |
| `~/.cache/piir` | `~/.cache/irlingo` |
| build options `PIIR_GPIO_BACKEND`, `PIIR_REMOTES`, `PIIR_STATIC`, `PIIR_COVERAGE` | `IRLINGO_GPIO_BACKEND`, `IRLINGO_REMOTES`, `IRLINGO_STATIC`, `IRLINGO_COVERAGE` |
| `tools/lirc2piir.py`, `tools/ir2piir.py` | `tools/lirc2irlingo.py`, `tools/ir2irlingo.py` |
| `github.com/mortenmathiasen/piir` | `github.com/mortenmathiasen/irlingo` (the old address redirects) |

To upgrade:

1. Install the irlingo package as described in [Prebuilt packages](#prebuilt-packages). With the tarball, also delete `/usr/bin/piir`, `/usr/bin/piir-remotes` and `/usr/share/piir` (`/usr/local/...` when built from source).
2. Add the LIRC remotes you used again, e.g. `sudo irlingo-remotes add sony`, and copy remotes of your own from the old remote directory to the new one. Remotes added with `piir-remotes` stay in the old directory after the `piir` package is removed; delete it afterwards.
3. Keep the toggle state of RC5/RC6 style remotes, if you like: `sudo mv /var/lib/piir /var/lib/irlingo`, and `mv ~/.local/state/piir ~/.local/state/irlingo` for each user. Without it, those remotes simply start from the first press again.
4. Replace `piir` with `irlingo`, and `PIIR_STATE_DIR` with `IRLINGO_STATE_DIR`, in your scripts, crontabs, systemd units, Home Assistant or Node-RED configuration and sudoers rules.

### Upgrading from PIIR 1.x

The command line options and remote files of PIIR 1.x still work. These things changed:

* **Build system.** irlingo is built with CMake instead of autotools: `./autogen.sh && ./configure && make && sudo make install` becomes `cmake -B build && cmake --build build && sudo cmake --install build`. The default installation prefix is still `/usr/local`.
* **Transmitter.** PIIR 1.x always used pigpio. PIIR 2 prefers the Linux kernel IR driver, which works on every Raspberry Pi including Pi 5, but needs `dtoverlay=gpio-ir-tx,gpio_pin=17` in `/boot/firmware/config.txt` (see [Compilation](#compilation)). The overlay sets the GPIO pin, so `outpin` in a remote file is not used by this backend. Without the overlay irlingo stops with `Cannot open /dev/lirc0`. To keep using pigpio and `outpin`, build with `-DIRLINGO_GPIO_BACKEND=pigpio` (this does not compile in release 2.0.0; use 2.1.0 or later).
* **Exit codes and output.** On errors and after `--help`, PIIR 1.x aborted (exit code 134); PIIR 2 exits with 1. The `gpioWaveTxSend OK` line is only printed by the pigpio backend.
* **Remote locations.** The remotes shipped with irlingo are in manufacturer directories, e.g. `conf/remotes/panasonic/hvac_panasonic.json`. Your own remotes directly in `conf/remotes/` are still found, and `--remote hvac_panasonic` still works. Release packages contain only irlingo's own remotes; add others with [irlingo-remotes](#choosing-the-remotes-of-an-installation).
* **Plugins.** A remote now names its plugin, e.g. `"plugin": "hvac_panasonic"`, or defines its checksums in the remote file, see [Checksums](#checksums). Remote files with neither setting whose path contains `hvac_panasonic` still get the Panasonic checksum, as in PIIR 1.x.
* **Codes sent.** PIIR 1.x only applied the last `--feature` given; PIIR 2 applies all of them, in the order of the remote file (see [How to use](#how-to-use)). For `hvac_panasonic`, `mode=FAN` now sends the fixed 27 degrees and `option=QUIET`, `option=8` and `option=10` override the fan speed and mode, as the original remote does. The same command can therefore send a different, correct code than PIIR 1.x.

## How to use

You kan get command line help running:
```
irlingo --help
```

Output will be like:
```
3112 remotes available in /home/pi/irlingo/conf/remotes/ and /usr/local/share/irlingo/conf/remotes/, list them with: irlingo --list

usage:      irlingo
            --remote, -r REMOTE
            [--feature, -f NAME=VALUE]
            [--verbose, -v (TRACE|DEBUG|INFO|WARN|ERROR|FATAL|NOTHING)]
            [--list, -l]
            [--help, -h]

REMOTE is a remote name from --list, e.g. sony/RM-ED011, a file name
that is unique among the remotes, e.g. RM-ED011, or a path to a JSON file.

examples:   irlingo --help
            irlingo --list
            irlingo --remote hvac_panasonic --help
            irlingo --remote hvac_panasonic --feature temperature=21
            irlingo --remote sony/RM-ED011 --feature key=KEY_POWER
```

irlingo looks for remotes in `conf/remotes/` below the current directory and then in its installation directory (`/usr/local/share/irlingo/conf/remotes/` or `/usr/share/irlingo/conf/remotes/` for the packages). Remotes are named `<manufacturer>/<model>`, like the files in those directories. List them with:
```
irlingo --list | grep -i sony
```

Specific help on available features for a particular remote can be achieved by:
```
irlingo --remote hvac_panasonic --help
```

Output will be like:
```
Features in config file /home/pi/irlingo/conf/remotes/panasonic/hvac_panasonic.json:
       temperature:
              8
              10
              16
              17
              18
              19
              20
              21
              22
              23
              24
              25
              26
              27
              28
              29
              30
       mode:
              AUTO
              HEAT
              COOL
              FAN
              OFF
       option:
              AUTO
              QUIET
              POWERFUL
       fan:
              AUTO
              VERYSLOW
              SLOW
              MEDIUM
              FAST
              VERYFAST
       updown:
              AUTO
              VERYLOW
              LOW
              MIDDLE
              HIGH
              VERYHIGH
       leftright:
              AUTO
              VERYLEFT
              LEFT
              MIDDLE
              RIGHT
              VERYRIGHT

usage:      irlingo
            ...
```

A working example combining more features into the IR signal:
```
sudo irlingo --remote hvac_panasonic --feature temperature=21 --feature mode=HEAT --feature option=AUTO --feature fan=AUTO --feature updown=HIGH --feature leftright=MIDDLE --verbose=DEBUG
```

Features are applied in the order they appear in the remote's JSON file, not the order given on the command line, so a feature listed later in the file wins where two set the same bits. For `hvac_panasonic` this means `option=QUIET` and the frost protection `option=8` override the fan speed and mode, and `mode=FAN` sends the fixed 27 degrees the original remote uses. Giving the same feature twice uses the last value.

If everything goes well the command prints nothing and exits with status 0 (the `pigpio` backend prints a line like `gpioWaveTxSend OK: 15897`). Add `--verbose=INFO` to see the code sent.

A remote converted from LIRC has a single feature, `key`, with a value for every button:
```
irlingo --remote sony/RM-ED011 --help
irlingo --remote sony/RM-ED011 --feature key=KEY_POWER
```

## Remotes

Remotes are JSON files in `conf/remotes/<manufacturer>/`, in the same structure as the [LIRC remote database](https://sourceforge.net/p/lirc-remotes/code/ci/master/tree/remotes). `panasonic/hvac_panasonic` is irlingo's own; the others are converted from LIRC.

### Choosing the remotes of an installation

The release packages only contain irlingo's own remotes. Each release also has `irlingo-remotes-<version>.tar.gz` with all remotes, and the `irlingo-remotes` command installed with irlingo adds remotes from it to the installation. It downloads the archive of the installed version, or uses one given with `--archive`:
```
irlingo-remotes available sony              # remotes in the archive containing "sony"
sudo irlingo-remotes add sony               # every Sony remote
sudo irlingo-remotes add samsung/AA59-00600A 'lg/AKB*'
irlingo-remotes installed
sudo irlingo-remotes remove sony
sudo irlingo-remotes add all                # everything
```

When compiling irlingo yourself, `IRLINGO_REMOTES` chooses the remotes `cmake --install` installs: `all` (the default), `native` (irlingo's own), `none`, manufacturers, remotes and patterns separated by semicolons, for example:
```
cmake -B build -DIRLINGO_REMOTES="native;sony;lg/AKB*"
```

### Remotes converted from LIRC

[tools/lirc2irlingo.py](tools/lirc2irlingo.py) converts LIRC configuration files to irlingo remotes. It needs only Python 3:
```
tools/lirc2irlingo.py convert -o conf/remotes my-remote.lircd.conf
tools/lirc2irlingo.py convert -o conf/remotes path/to/lirc-remotes/remotes
```

Its encoder follows the LIRC 0.10.2 sources, so a converted key sends what `irsend SEND_ONCE` sends, including the repeats a remote needs (`min_repeat`) and the gaps between them. `tools/lirc2irlingo.py verify` compares it with LIRC's own `irsimsend`: for every file in the LIRC database that LIRC can read, the encoder produces exactly the same pulses and spaces. The converted remotes differ from that only where noted below. CI runs this check and also checks that `conf/remotes` is exactly what the converter makes from the revision of the LIRC database given in `tools/lirc-remotes.txt`.

Not everything converts. [tools/lirc-remotes-report.txt](tools/lirc-remotes-report.txt) lists what was skipped and why:
* remotes made for IR receivers (e.g. irman) that store scan codes instead of IR timings, and remotes using the rarely used RCMM, XMP, Grundig, B&O and serial encodings
* keys LIRC cannot send, keys irlingo cannot send because they need more than 1024 pulses and spaces, and duplicate key names (the first one is used, as in LIRC)

Raw recorded codes have small timing variations, so where a raw remote has too many different durations for irlingo's symbols, they are rounded by at most 2%. LIRC itself accepts 30% when receiving.

Remotes with a toggle bit (RC5, RC6 and others with a `toggle_bit_mask`) get a `toggle` section holding the code of every key with the bit flipped, see [Toggle bits](#toggle-bits).

Two LIRC 0.10.2 bugs are not copied: durations of 0 in a configuration break the alternation of pulses and spaces there, and raw remotes with a repeat code send the start of the raw code instead of the repeat code.

Remotes using Sony's protocol (SIRC) send every code at least 3 times, at 40 kHz unless the LIRC file gives a frequency: Sony devices ignore a code sent only once, and their receivers are tuned to 40 kHz. LIRC sends such codes as the file says, often once at 38 kHz. See [Managing remotes](doc/remotes.md).

### Remotes derived from recordings

[tools/ir2irlingo.py](tools/ir2irlingo.py) derives a remote, e.g. an air conditioner's, from recorded codes and the settings each was sent for. It needs only Python 3. Record the codes with an IR receiver, or from software that generates them, and write one code per line in a [JSON Lines](https://jsonlines.org/) file:
```
{"features": {"mode": "HEAT", "temperature": "21", "fan": "AUTO"}, "frequency": 38000, "durations": [3500, 1750, 435, 435, 435, 1300, ...]}
```
`durations` alternate mark and space in microseconds, starting with a mark. Every line names the same features. Then:
```
tools/ir2irlingo.py captures.jsonl -o conf/remotes/mybrand/aircon.json --description "My air conditioner"
```

Codes of another length or structure, e.g. a short power off code, become [variants](#variants), selected by the feature values only their captures have.

The captures must contain:
* a base code: the first line
* for every value of every feature, a code where only that feature differs from the base
* random combinations of the features: about 300 for six features, and 1000 or more when settings interact (e.g. a temperature only some modes accept). They are used to find values that override other features, e.g. a fan mode forcing a temperature. Without them, an override that happens to match the base code is not found.

ir2irlingo clusters the durations into symbols (`0`, `1`, header, gaps), searches [checksums](#checksums) (sums, negated sums, XORs and counts of set bits of bytes or nibbles, adjacent or not, found by solving their lowest bits as linear equations, and 8-bit and 4-bit CRCs of the units before them), gives each feature the bits its values change, puts a feature that overrides another after it, and writes overrides that only happen together with another feature's value as [overrides](#overrides). Bits still wrong then get overrides for the fewest feature values (up to three) under which they are the same in every capture. It then encodes every capture again and reports those it cannot reproduce (exit code 1). A sample of the captures is stored as the remote's `tests`.

To see whether the remote also works for combinations that were not recorded, record more and check them without using them to derive the remote:
```
tools/ir2irlingo.py captures.jsonl --check more-captures.jsonl -o aircon.json
```

For protocols ir2irlingo cannot describe, `--fallback` hard-codes the recorded codes it does not reproduce as [overrides](#overrides) listing every feature. Only the recorded combinations are then sent exactly, so record every combination of the features. That can be tens of thousands of codes and a remote of several megabytes, which is fine on a Raspberry Pi.

Limitations: codes of different lengths need a feature value that selects them (or `--fallback`); every symbol must be a mark followed by a space (pulse distance or pulse width coding, used by most air conditioners, not RC5/RC6); a code must end with a stop mark, since the space after the last mark cannot be recorded; CRCs wider than a unit are not searched; and an override depending on two other features is not found, only one depending on one (see [Overrides](#overrides)).

[tools/heatpumpir](tools/heatpumpir) records the codes of the [arduino-heatpumpir](https://github.com/ToniA/arduino-heatpumpir) library as captures, for every air conditioner model it supports. The library (GPL-2.0) is not part of irlingo; build the recorder against a checkout of it:
```
git clone https://github.com/ToniA/arduino-heatpumpir.git
cmake -S tools/heatpumpir -B build/heatpumpir -DHEATPUMPIR_DIR=$PWD/arduino-heatpumpir
cmake --build build/heatpumpir
build/heatpumpir/heatpumpir_capture list
build/heatpumpir/heatpumpir_capture gree 300 1 > gree.jsonl
build/heatpumpir/heatpumpir_capture gree 1000 2 > gree-check.jsonl
tools/ir2irlingo.py gree.jsonl --check gree-check.jsonl -o gree.json
```
Of its 46 models, ir2irlingo derives a remote sending the same codes for 38 from 300 random combinations, and for 44 from 1000 to 3000 (checked with 1000 other random combinations each), the two Samsung models with counts of set bits among them. ir2irlingo cannot derive the other two: `nibe` sends a 2-bit byte before its checksum, so the checksum does not line up with the bytes, and `carrier_nqv` adds corrections depending on the mode and fan speed to its sum. Their remotes are written from the library's source instead and send every combination exactly: [nibe/hvac_nibe](conf/remotes/nibe/hvac_nibe.json) restarts counting bytes after the 2-bit byte (a `-` separator with `"restart": "-"`), and [carrier/hvac_carrier_nqv](conf/remotes/carrier/hvac_carrier_nqv.json) has a [variant](#variants) with its own checksum for each mode and fan speed needing a correction. For models no remote describes, `heatpumpir_capture MODEL all` records every combination for `--fallback`: for `samsung_fjm`, which no longer needs it, that was 29433 codes, giving a 3.8 MB remote with 12356 hard-coded codes that sends every combination exactly. Models that send the time of day get 00:00.

The tests derive `panasonic/hvac_panasonic` from 340 codes irlingo sends for it. The result finds its checksum (`{"type": "sum", "from": 8, "to": 25, "at": 26}`) and its overrides, and sends exactly the same pulses for 1000 other random combinations.

### Toggle bits

Some protocols, e.g. Philips RC5 and RC6, have a toggle bit that the remote flips on every press of any key. Repeats while a key is held keep it, so a device can tell a held key from a new press: pressing `5` twice gives `55`, not one long press of `5`.

A remote with a toggle bit has a `toggle` section with the same structure as `features`. On every other press irlingo sends the codes of the `toggle` section instead (a value missing there sends its normal code). irlingo remembers the toggle state of each remote in a state file and flips it after every successful transmission:

| Running as | State file |
|------------|------------|
| any user, with `IRLINGO_STATE_DIR` set | `$IRLINGO_STATE_DIR/toggle-state` |
| root (e.g. with `sudo`) | `/var/lib/irlingo/toggle-state` |
| other users | `$XDG_STATE_HOME/irlingo/toggle-state`, by default `~/.local/state/irlingo/toggle-state` |

The file has a line per remote with its state (`0` or `1`) and the path of the remote file. Deleting it starts all remotes from the first press again. If irlingo runs both with and without `sudo`, set `IRLINGO_STATE_DIR` so both use the same file. If the file cannot be written, irlingo logs a warning and still transmits.

### Checksums

Many air conditioner protocols end a frame with a checksum of its bytes. A remote computes it with a `checksums` array instead of a plugin. `panasonic/hvac_panasonic` sums bytes 8 to 25 (its second frame) into byte 26:
```json
"checksums": [
    {"type": "sum", "from": 8, "to": 25, "at": 26}
]
```

After the features are applied, the bit symbols of the code (`0` and `1`) are read as bytes of 8 bits, or nibbles of 4 bits, numbered from 0 in the order they are sent. All other symbols, such as headers and separators, are skipped, so bytes do not need to line up with the separators. Each checksum combines the bytes (or nibbles) `from` to `to`, or those listed in `data`, and writes the result into byte `at`:

| Setting | Meaning | Default |
|---------|---------|---------|
| `type` | `sum` (modulo 256, or 16 for nibbles), `xor`, `popcount` (the number of bits set to 1) or `crc` | required |
| `unit` | `byte` or `nibble`; `from`, `to`, `data`, `at` and `init` then count in that unit | `byte` |
| `from`, `to` | first and last byte covered | required unless `data` is given |
| `data` | list of the bytes covered, e.g. `[0, 2, 4]`, instead of `from` and `to` | |
| `at` | byte the checksum is written to, not one of those covered, or a list of bytes for a wider result | required |
| `init` | start value, 0-255 (0-15 for nibbles, more for a wider result) | `0` |
| `invert` | complement the result (`~x`) | `false` |
| `negate` | negate the result (`0 - x`), after `invert` | `false` |
| `xorout` | XOR the result with this value, after `invert` and `negate` | `0` |
| `poly` | `crc` only: the polynomial, without its highest bit, e.g. `7` for x⁸ + x² + x + 1 | required for `crc` |
| `reflect` | `crc` only: the CRC is reflected (each unit enters lowest bit first, the result is reversed) | `false` |
| `bitorder` | `lsb` when a byte is sent lowest bit first, `msb` when highest bit first | `lsb` |
| `bits` | the zero and one symbols | `"01"` |
| `restart` | symbols after which units are counted from a new bit, e.g. the gap between frames; the bits of a partial byte (or nibble) before them are skipped | none |

Nibbles are numbered in the order they are sent: in a byte sent lowest bit first (`lsb`), nibble 0 is its low nibble, while with `msb` it is its high nibble. For example, a 4-bit checksum summing the nibbles 0, 2 and 4 into nibble 7, sent highest bit first:
```json
{"type": "sum", "unit": "nibble", "data": [0, 2, 4], "at": 7, "bitorder": "msb"}
```

A count of set bits, e.g. 28 minus the number of bits set in bytes 2 to 5, in the high nibble of byte 1, is a `popcount` with a start value of 4 negated: 0 - (4 + count) is 28 - count modulo 16:
```json
{"type": "popcount", "unit": "nibble", "from": 4, "to": 11, "at": 3, "init": 4, "negate": true}
```

A result wider than a unit is written into the units listed in `at`: with `lsb` the first one holds the lowest bits, with `msb` the highest. Samsung AQV sends the complement of its count of set bits, 8 bits, in the high nibble of byte 1 and the low nibble of byte 2:
```json
{"type": "popcount", "unit": "nibble", "data": [0, 1, 2, 5, 6, 7, 8, 9, 10, 11, 12, 13], "at": [3, 4], "invert": true}
```

A `crc` takes the parameters of the [CRC catalogue](https://reveng.sourceforge.io/crc-catalogue/): `poly`, `init`, `reflect` (its `refin` and `refout`, which must be the same) and `xorout`, and its width is that of `at`. The covered units enter the CRC as read with `bitorder`, so `"bitorder": "msb"` sends the bits in the order a CRC without `reflect` reads them. CRC-8/SAE-J1850 over bytes 0 to 8 into byte 9, and CRC-16/MODBUS into bytes 9 and 10, low byte first:
```json
{"type": "crc", "poly": 29, "init": 255, "xorout": 255, "from": 0, "to": 8, "at": 9, "bitorder": "msb"}
{"type": "crc", "poly": 32773, "init": 65535, "reflect": true, "from": 0, "to": 8, "at": [9, 10]}
```

Some protocols end a frame with a few extra bits, which shifts all bytes of the next frame. With `"restart": "W"`, counting starts again after the gap symbol `W`, so the next frame's first bits are a new byte.

Checksums are applied in order, so a later checksum can cover the byte an earlier one wrote. irlingo stops with `Invalid checksum` when a checksum is malformed or refers to bytes beyond the end of the code. The `plugin` setting still works for checksums these settings cannot express.

### Overrides

Features are applied in the order of the remote file, so a later feature can force bits of an earlier one, e.g. a fan mode forcing a temperature. When a value forces bits only together with another feature's value, e.g. the dry mode forcing the lowest fan speed only while the power is on, an `overrides` entry does it:
```json
"overrides": [
    {"when": {"mode": "DRY", "power": "ON"}, "code": "      10"}
]
```
After all features, every override whose `when` features are all requested with these values is applied, in order, in the same way as a feature's code: its symbols replace the code's, spaces leave it as it is. Checksums are computed afterwards.

### Variants

Some remotes send codes of another length for some settings, e.g. a short code for power off. A remote can hold such codes in `variants`, each with the feature values that select it (`when`) and its own `template`, and optionally its own `features`, `overrides` and `checksums`, whose positions refer to its template:
```json
"variants": [
    {"when": {"power": "OFF"}, "template": "H-00101000-11000110-00000000-00001000-00001000-01000000-10111111-0"}
]
```
The first variant whose `when` features are all requested is used instead of the remote's own template, features, overrides and checksums; the symbol definitions are shared. The values selecting a variant must still be listed in the remote's `features` (with an empty code), so that irlingo accepts them.

## Contributing

Contribution to implement more transmitters is welcome.

Please read [CONTRIBUTING.md](https://gist.github.com/PurpleBooth/b24679402957c63ec426) for details on our code of conduct, and the process for submitting pull requests to us.

## Versioning

We use [SemVer](http://semver.org/) for versioning. For the versions available, see the [tags on this repository](https://github.com/mortenmathiasen/irlingo/tags). 

## Authors

* **Morten Mathiasen** - *Initial work* - [repositories](https://github.com/mortenmathiasen)

See also the list of [contributors](https://github.com/irlingo/contributors) who participated in this project.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details

## Acknowledgments

* Thanks to Leon (https://anavi.technology/) for hardware and encouragement


