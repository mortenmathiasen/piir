# PIIR

PIIR is a command line tool to transmit IR-signals like remote controls do. PIIR is created as a generic tool that supports configuration and transmission of any signal, eg. including long aircondition signals.

![alt text](https://github.com/mortenmathiasen/piir/blob/master/doc/remote-sommerhus.png?raw=true)

Examples:
```
piir --remote tv_sony --feature action=TURN_ON

piir --remote hvac_panasonic --feature temperature=21 --feature mode=HEAT
```

## Table of contents

  * [Features](#features)
    + [IR-protocol](#ir-protocol)
    + [Supported systems](#supported-systems)
    + [Supported hardware](#supported-hardware)
    + [Prerequisites](#prerequisites)
  * [Getting Started](#getting-started)
    + [Compilation](#compilation)
    + [Installation](#installation)
    + [Running the tests](#running-the-tests)
    + [Deployment](#deployment)
  * [How to use](#how-to-use)
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
* Use the protocol definitions that exists already inside PIIRs remote folder [conf/remotes](https://github.com/mortenmathiasen/piir/tree/master/conf/remotes)
* Reverse engineer your remote control, e.g. using [IR decoder for Arduino](https://github.com/ToniA/Raw-IR-decoder-for-Arduino.git) which I did for [Panasonic airconditioner](https://github.com/mortenmathiasen/piir/blob/master/conf/remotes/hvac_panasonic.pdf)

### Supported systems

The tool is developed in C to support deployment on any platform. 

### Supported hardware

The tool supports IR transmission by:
* Raspberry PI using [GPIO](http://abyz.me.uk/rpi/pigpio/)
* more platforms to come

### Prerequisites

To be able to transmit IR signal you need an IR led, either:
* build the electronics yourself, e.g. as mentioned at [Raspberry-PI-Geek](https://www.raspberry-pi-geek.com/Archive/2015/10/Raspberry-Pi-IR-remote)
* apply an premade pHAT, eg. [ANAVI Infrared pHAT](https://anavi.technology/)

## Getting Started

You need to identify a device you want to control by IR-signals, e.g. an airconditioner, a TV or similar.

### Compilation

Update package list:
```
sudo apt update
```

By default PIIR sends IR through the Linux kernel's IR transmitter driver, which generates the carrier with accurate timing on every Raspberry Pi model including Pi 5. No extra library is needed. Enable the driver by adding this line to `/boot/firmware/config.txt` and rebooting:
```
dtoverlay=gpio-ir-tx,gpio_pin=17
```
Use the GPIO pin your IR LED is connected to. This creates `/dev/lirc0`. If that is not your transmitter device (for example because an IR receiver overlay is also enabled), set `"device": "/dev/lirc1"` in the `transmitter` section of the remote's JSON file. The user running PIIR must be allowed to write to the device: check its group with `ls -l /dev/lirc0` and add your user to that group, or run PIIR with `sudo`.

Alternatively PIIR can drive the pin itself through [libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/) version 2.0 or newer (`sudo apt install -y libgpiod-dev`) or [PIGPIO](http://abyz.me.uk/rpi/pigpio/) (`sudo apt install -y pigpio`, not supported on Pi 5). With libgpiod the carrier is generated from user space, so its timing is much less accurate. With these backends the pin is taken from `outpin` in the remote's JSON file.

Install CMake to enable compilation
```
sudo apt-get install -y cmake build-essential
```

Download and extract latest released PIIR software:
```
curl -s https://api.github.com/repos/mortenmathiasen/piir/releases/latest | sed -n 's/.*tarball_url":\s"\(.*\)".*/\1 -O piir.tar/p' | xargs wget
mkdir piir
tar xf piir.tar -C ./piir
mv ./piir/*/* ./piir
```

Now compile it:
```
cd piir
cmake -B build
cmake --build build
```

PIIR uses the kernel driver (`lirc`) when the kernel headers provide `linux/lirc.h`, which they do on any Linux system, and otherwise falls back to libgpiod and then pigpio. To pick one explicitly, add `-DPIIR_GPIO_BACKEND=lirc`, `gpiod` or `pigpio` to the first `cmake` command.

### Installation

After compilation you can install the PIIR tool by:
```
sudo cmake --install build
```

### Deployment

You can deploy the PIIR tool to any live system. Either you can compile the sourced code directly on the live system as explained above or you can compile it on another development system and then move the  execution file to your live system.

#### Cross compiling for Raspberry Pi

Toolchain files for 64-bit (`aarch64`) and 32-bit (`armhf`) Raspberry Pi OS are in `cmake/`. On a Debian/Ubuntu build machine, install the cross compiler, for example for 64-bit:
```
sudo apt install -y gcc-aarch64-linux-gnu
```

The default `lirc` backend needs nothing beyond the cross compiler. Only for the `gpiod` or `pigpio` backend must PIIR link against the library built for the Pi. Either install the Debian multiarch package:
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

Copy `build-rpi/piir` to the Pi and `conf/remotes/` to `/usr/local/share/piir/conf/remotes/` (or whatever `CMAKE_INSTALL_PREFIX` you configured with). For 32-bit Raspberry Pi OS use `cmake/toolchain-rpi-armhf.cmake`, `gcc-arm-linux-gnueabihf` and `libgpiod-dev:armhf` instead. That toolchain targets ARMv7, so it does not support the Pi 1 or Pi Zero/Zero W.

## How to use

You kan get command line help running:
```
piir --help
```

Output will be like:
```
Available configuration files in /home/pi/piir/conf/remotes/:
    hvac_panasonic
    ...
Available configuration files in /usr/local/share/piir/conf/remotes/:
    hvac_panasonic
    ...

usage:      piir
            --remote, -r CONFIGFILE
            [--feature, -f NAME=VALUE]
            [--verbose, -v (TRACE|DEBUG|INFO|WARN|ERROR|FATAL|NOTHING)]
            [--help, -h]

examples:   piir --help
            piir --remote hvac_panasonic --help
            piir --remote hvac_panasonic --feature temperature=21
            piir --remote tv_sony --feature action=TURN_ON
```

Specific help on available features for a particular remote can be achieved by:
```
piir --remote hvac_panasonic --help
```

Output will be like:
```
Features in config file /home/pi/piir/conf/remotes/hvac_panasonic.json:
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

usage:      piir
            --remote, -r CONFIGFILE
            [--feature, -f NAME=VALUE]
            [--verbose, -v (TRACE|DEBUG|INFO|WARN|ERROR|FATAL|NOTHING)]
            [--help, -h]

examples:   piir --help
            piir --remote hvac_panasonic --help
            piir --remote hvac_panasonic --feature temperature=21
            piir --remote tv_sony --feature action=TURN_ON
```

A working example combining more features into the IR signal:
```
sudo piir --remote hvac_panasonic --feature temperature=21 --feature mode=HEAT --feature option=AUTO --feature fan=AUTO --feature updown=HIGH --feature leftright=MIDDLE --verbose=DEBUG
```

If everything goes well output from the command will be like this:
```
gpioWaveTxSend OK: 15897
```

## Contributing

Contribution to implement more transmitters is welcome.

Please read [CONTRIBUTING.md](https://gist.github.com/PurpleBooth/b24679402957c63ec426) for details on our code of conduct, and the process for submitting pull requests to us.

## Versioning

We use [SemVer](http://semver.org/) for versioning. For the versions available, see the [tags on this repository](https://github.com/mortenmathiasen/piir/tags). 

## Authors

* **Morten Mathiasen** - *Initial work* - [repositories](https://github.com/mortenmathiasen)

See also the list of [contributors](https://github.com/piir/contributors) who participated in this project.

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

## Acknowledgments

* Thanks to Leon (https://anavi.technology/) for hardware and encouragement


