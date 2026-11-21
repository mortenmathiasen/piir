# Installation

- [Hardware](#hardware)
- [Choosing a transmitter backend](#choosing-a-transmitter-backend)
- [Installing a release package](#installing-a-release-package)
- [Enabling the kernel IR transmitter](#enabling-the-kernel-ir-transmitter)
- [Checking the installation](#checking-the-installation)
- [Building from source](#building-from-source)
- [Cross compiling](#cross-compiling)
- [Where things are installed](#where-things-are-installed)
- [Uninstalling](#uninstalling)
- [Upgrading from PIIR 2.x](#upgrading-from-piir-2x)
- [Upgrading from PIIR 1.x](#upgrading-from-piir-1x)

## Hardware

irlingo runs on every Raspberry Pi model, from the Pi 1 and Zero to the Pi 5 and 500. Besides the Pi you need an IR LED connected to a GPIO pin. Either:

- use a ready-made board, e.g. the [ANAVI Infrared pHAT](https://anavi.technology/), which has its transmitter on GPIO 17, or
- build a small circuit yourself: an IR LED for 940 nm switched by a transistor, with three resistors. [Connecting an IR LED](hardware.md) has the circuit, the part values and the pins.

A GPIO pin cannot drive an IR LED brightly enough for more than about a metre, so a home-made circuit normally switches the LED with a transistor, powered from the 5 V pin.

The examples in this documentation use GPIO 17 (physical pin 11). Any free GPIO pin works; use its number wherever 17 appears.

## Choosing a transmitter backend

irlingo generates the IR signal through one of three backends, chosen when it is compiled:

| Backend | How it sends | Pi models | Pin set by | Notes |
|---------|--------------|-----------|------------|-------|
| `lirc` (default) | Linux kernel IR transmitter driver, `/dev/lirc0` | all, including Pi 5 | the `gpio-ir-tx` overlay | Accurate timing, no extra library. Used by the release packages. |
| `gpiod` | toggles the pin from user space with [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/) 2.0 or newer | all | `outpin` in the remote file | The carrier is generated in software, so timing is much less accurate. |
| `pigpio` | DMA waveforms with [pigpio](http://abyz.me.uk/rpi/pigpio/) | Pi 1-4, Zero | `outpin` in the remote file | Not supported on the Pi 5. |

Use `lirc` unless you have a reason not to.

## Installing a release package

Every [release](https://github.com/mortenmathiasen/irlingo/releases) has packages with the `lirc` backend. The binaries are statically linked, so they run on any Raspberry Pi OS release.

| Package | Raspberry Pi models | Raspberry Pi OS |
|---------|---------------------|-----------------|
| `armv6` | Pi 1, Zero, Zero W, and every other model | 32-bit |
| `armv7` | Pi 2, 3, 4, 400, Zero 2 W | 32-bit |
| `aarch64` | Pi 3, 4, 400, 5, 500, Zero 2 W | 64-bit |

If you are unsure, run `dpkg --print-architecture` on the Pi: `arm64` needs the `aarch64` package, `armhf` needs `armv6` or `armv7` (`armv6` works on every model).

Download the package from the release page and install it, e.g. on a 64-bit Pi:
```
sudo apt install ./irlingo_3.0.0_aarch64.deb
```
Or, without `apt`, extract the tarball into `/`:
```
sudo tar -xzf irlingo-3.0.0-aarch64.tar.gz -C / --strip-components=1
```

The packages install `irlingo`, `irlingo-remotes` and irlingo's own remotes (e.g. `panasonic/hvac_panasonic`). The more than 3000 remotes converted from the LIRC database are not included; add the ones you need with `irlingo-remotes`, see [Managing remotes](remotes.md#adding-remotes-to-an-installation).

Then enable the kernel IR transmitter.

## Enabling the kernel IR transmitter

The `lirc` backend needs the kernel's `gpio-ir-tx` driver. Add this line to `/boot/firmware/config.txt` (`/boot/config.txt` on Raspberry Pi OS releases before Bookworm), with the GPIO pin of your LED, and reboot:
```
dtoverlay=gpio-ir-tx,gpio_pin=17
```

After the reboot the transmitter is `/dev/lirc0`:
```
$ ls -l /dev/lirc*
crw-rw---- 1 root video 251, 0 Nov 21 10:00 /dev/lirc0
```

The pin is set by the overlay only; with this backend the `outpin` setting of a remote file is not used.

**Permissions.** The user running irlingo must be allowed to write to the device. Either run irlingo with `sudo`, or add your user to the group owning the device (shown by `ls -l`, often `video`) and log in again:
```
sudo usermod -aG video $USER
```

**More than one IR device.** If an IR receiver overlay (`gpio-ir`) is enabled too, the transmitter may be `/dev/lirc1` instead. Check which device is the transmitter, e.g. with `ir-ctl --features --device=/dev/lirc1` from the `v4l-utils` package, and set it in the remote file:
```json
"transmitter": {"device": "/dev/lirc1"}
```

## Checking the installation

```
irlingo --help                                  # shows the usage and how many remotes are installed
irlingo --remote hvac_panasonic --help          # lists the features of a remote
sudo irlingo --remote hvac_panasonic --feature temperature=21 --verbose=DEBUG
```

With the `lirc` backend a successful transmission prints nothing (add `--verbose=INFO` or `DEBUG` to see what is sent) and exits with status 0. To see that the LED actually lights, look at it through a phone camera while sending: most phone cameras show IR light as a faint purple flash. See [Troubleshooting](troubleshooting.md) if nothing happens.

## Building from source

Install the compiler and CMake:
```
sudo apt update
sudo apt install -y cmake build-essential
```

For the `gpiod` or `pigpio` backend, also install its library: `sudo apt install -y libgpiod-dev` or `sudo apt install -y pigpio`. The default `lirc` backend needs nothing more.

Get the source, either the latest release:
```
curl -s https://api.github.com/repos/mortenmathiasen/irlingo/releases/latest | sed -n 's/.*tarball_url":\s"\(.*\)".*/\1 -O irlingo.tar/p' | xargs wget
mkdir irlingo
tar xf irlingo.tar -C ./irlingo
mv ./irlingo/*/* ./irlingo
cd irlingo
```
or the development version:
```
git clone https://github.com/mortenmathiasen/irlingo.git
cd irlingo
```

Compile and install:
```
cmake -B build
cmake --build build
sudo cmake --install build
```

### Build options

Give options to the first `cmake` command, e.g. `cmake -B build -DIRLINGO_GPIO_BACKEND=gpiod`.

| Option | Values | Default |
|--------|--------|---------|
| `IRLINGO_GPIO_BACKEND` | `lirc`, `gpiod` or `pigpio` | `lirc` when the kernel headers provide `linux/lirc.h` (any Linux system), else `gpiod`, then `pigpio` |
| `IRLINGO_REMOTES` | remotes `cmake --install` installs: `all`, `native` (irlingo's own), `none`, or manufacturers, remotes and patterns separated by semicolons, e.g. `"native;sony;lg/AKB*"` | `all` |
| `CMAKE_INSTALL_PREFIX` | installation directory | `/usr/local` |
| `IRLINGO_STATIC` | `ON` links statically, so the binary does not depend on the C library version of the target | `OFF` |
| `IRLINGO_COVERAGE` | `ON` builds with coverage measurement, see [Development](development.md) | `OFF` |

## Cross compiling

You can build irlingo on a faster Linux machine and copy it to the Pi. Toolchain files are in `cmake/`:

| Target | Toolchain file | Compiler package |
|--------|----------------|------------------|
| 64-bit Raspberry Pi OS | `cmake/toolchain-rpi-aarch64.cmake` | `gcc-aarch64-linux-gnu` |
| 32-bit, Pi 2 and newer (ARMv7) | `cmake/toolchain-rpi-armhf.cmake` | `gcc-arm-linux-gnueabihf` |
| 32-bit, every Pi including Pi 1 and Zero (ARMv6) | `cmake/toolchain-rpi-armv6.cmake` | `gcc-arm-linux-gnueabi` |

For example, for 64-bit:
```
sudo apt install -y gcc-aarch64-linux-gnu
cmake -B build-rpi -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi-aarch64.cmake
cmake --build build-rpi
```

The `lirc` backend needs nothing besides the compiler. For `gpiod` or `pigpio`, irlingo must link against the library built for the Pi: install the multiarch package (`sudo dpkg --add-architecture arm64 && sudo apt update && sudo apt install -y libgpiod-dev:arm64`), or copy a sysroot from a Pi that has the library installed:
```
mkdir -p rpi-sysroot/usr
rsync -a pi@raspberrypi:/usr/include pi@raspberrypi:/usr/lib rpi-sysroot/usr/
rsync -a pi@raspberrypi:/lib rpi-sysroot/
cmake -B build-rpi -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi-aarch64.cmake -DRPI_SYSROOT=$PWD/rpi-sysroot
```
The ARMv6 toolchain builds a static `lirc` binary only; for `gpiod` or `pigpio` on a Pi 1 or Zero, compile on the Pi itself.

Copy `build-rpi/irlingo` to the Pi, and the remotes you need from `conf/remotes/` to `/usr/local/share/irlingo/conf/remotes/`. Or build packages: configure with `-DCMAKE_INSTALL_PREFIX=/usr -DIRLINGO_STATIC=ON`, build, and run `cpack` in the build directory to get a `.deb` and a `.tar.gz`.

## Where things are installed

With the default prefix `/usr/local` (the packages use `/usr`):

| Path | Contents |
|------|----------|
| `/usr/local/bin/irlingo` | the command |
| `/usr/local/bin/irlingo-remotes` | adds and removes remotes, see [Managing remotes](remotes.md) |
| `/usr/local/share/irlingo/conf/remotes/` | the installed remotes, one directory per manufacturer |
| `/usr/local/share/doc/irlingo/` | the README, the license and this documentation (`doc/`) |
| `/var/lib/irlingo/toggle-state` or `~/.local/state/irlingo/toggle-state` | toggle state of RC5/RC6 style remotes, see [Usage](usage.md#remotes-with-a-toggle-bit) |

## Uninstalling

A package: `sudo apt remove irlingo`. A build from source: `cmake --install` writes the list of installed files to `build/install_manifest.txt`, so
```
xargs sudo rm -f < build/install_manifest.txt
```
removes them. Remove the overlay line from `/boot/firmware/config.txt` if you no longer need the transmitter.

## Upgrading from PIIR 2.x

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

1. Install the irlingo package as described above. With the tarball, also delete `/usr/bin/piir`, `/usr/bin/piir-remotes` and `/usr/share/piir` (`/usr/local/...` when built from source).
2. Add the LIRC remotes you used again, e.g. `sudo irlingo-remotes add sony`, and copy remotes of your own from the old remote directory to the new one. Remotes added with `piir-remotes` stay in the old directory after the `piir` package is removed; delete it afterwards.
3. Keep the toggle state of RC5/RC6 style remotes, if you like: `sudo mv /var/lib/piir /var/lib/irlingo`, and `mv ~/.local/state/piir ~/.local/state/irlingo` for each user. Without it, those remotes simply start from the first press again.
4. Replace `piir` with `irlingo`, and `PIIR_STATE_DIR` with `IRLINGO_STATE_DIR`, in your scripts, crontabs, systemd units, Home Assistant or Node-RED configuration and sudoers rules.

## Upgrading from PIIR 1.x

The command line options and remote files of PIIR 1.x still work. What changed:

- **Build system.** CMake instead of autotools: `./autogen.sh && ./configure && make && sudo make install` becomes `cmake -B build && cmake --build build && sudo cmake --install build`. The default prefix is still `/usr/local`.
- **Transmitter.** PIIR 1.x always used pigpio. PIIR 2 uses the kernel IR driver, which needs the `dtoverlay=gpio-ir-tx,gpio_pin=17` line (see [above](#enabling-the-kernel-ir-transmitter)) and ignores `outpin`. Without the overlay irlingo stops with `Cannot open /dev/lirc0`. To keep pigpio and `outpin`, build with `-DIRLINGO_GPIO_BACKEND=pigpio` (release 2.1.0 or later).
- **Exit codes and output.** After errors and `--help`, PIIR 1.x aborted (exit status 134); PIIR 2 exits with 1. The `gpioWaveTxSend OK` line is only printed by the pigpio backend.
- **Remote locations.** Remotes are in manufacturer directories, e.g. `conf/remotes/panasonic/hvac_panasonic.json`. Your own remotes directly in `conf/remotes/` are still found, and `--remote hvac_panasonic` still works.
- **Plugins.** A remote names its plugin (`"plugin": "hvac_panasonic"`) or defines its checksums in the file. Remote files with neither, whose path contains `hvac_panasonic`, still get the Panasonic checksum.
- **Codes sent.** PIIR 1.x applied only the last `--feature`; PIIR 2 applies all of them, in the order of the remote file. For `hvac_panasonic`, `mode=FAN` now sends the fixed 27 degrees, and `option=QUIET`, `option=8` and `option=10` override the fan speed and mode, as the original remote does.
