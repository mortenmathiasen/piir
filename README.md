# PIIR

PIIR is a self-contained tool to transmit IR-signals like remote controls.

PIIR is created as an alternative to LIRC and other tools, that are more heavy weight and less generic.

## Features

- Send infrared commands to control aircondition etc.
- Transmit any IR signal using any protocol.
- Integration to home automation and other systems.
- Customization by configuration and plugins.

## Getting Started

You need to identify a device you want to control by IR-signals, e.g. an airconditioner, a TV or similar.

### IR-protocol

To control an IR-receiver you need to know the IR-signals to transmit to it. Either, you can:
* Use the protocol definitions that exists already inside PIIRs remote folder [conf/remotes](https://github.com/mortenmathiasen/piir/tree/master/conf/remotes)
* Reverse engineer your remote control, e.g. using [IR decoder for Arduino](https://github.com/ToniA/Raw-IR-decoder-for-Arduino.git) which I did for [Panasonic airconditioner](https://github.com/mortenmathiasen/piir/blob/master/conf/remotes/hvac_panasonic.pdf)

### Supported systems

The tool is developed in C to support deployment on any platform. 

### Supported hardware

The tool supports IR transmission by:
* Raspberry PI using [GPIO](http://abyz.me.uk/rpi/pigpio/)
* more platforms to come when you contribute the software.

### Prerequisites

To be able to transmit IR signal you need an IR led, either:
* build the electronics yourself, e.g. as mentioned at [Raspberry-PI-Geek](https://www.raspberry-pi-geek.com/Archive/2015/10/Raspberry-Pi-IR-remote)
* apply an premade pHAT, eg. [ANAVI Infrared pHAT](https://anavi.technology/)

### Compilation

Update package list:
```
sudo apt update
```

PIIR uses the library [PIGPIO](http://abyz.me.uk/rpi/pigpio/) which you can install by:
```
sudo apt install -y pigpio
```

Install Autotools to enable compilation
```
sudo apt-get install -y autoconf automake libtool
```

Download the PIIR software from [https://github.com/mortenmathiasen/piir](https://github.com/mortenmathiasen/piir):
```
git clone https://github.com/mortenmathiasen/piir.git
```

Now you can compile it
```
cd piir
./autogen.sh
./configure
make
```

## Installation

After compilation you can install the PIIR tool by:
```
make install
```

## Running the tests

After compilation you can test the PIIR tool by:
```
make check
```

## Deployment

You can deploy the PIIR tool to any live system. Either you can compile the sourced code directly on the live system as explained above or you can compile it on another development system and then move the  execution file to your live system.

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

We use [SemVer](http://semver.org/) for versioning. For the versions available, see the [tags on this repository](https://github.com/your/project/tags). 

## Authors

* **Morten Mathiasen** - *Initial work* - [repositories](https://github.com/mortenmathiasen)

See also the list of [contributors](https://github.com/piir/contributors) who participated in this project.

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

## Acknowledgments

* Thanks to Leon (https://anavi.technology/) for hardware and encouragement


