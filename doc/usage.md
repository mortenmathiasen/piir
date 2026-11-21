# Usage

- [Command line](#command-line)
- [Choosing a remote](#choosing-a-remote)
- [Features: keys and settings](#features-keys-and-settings)
- [Sending a key of a TV or other device](#sending-a-key-of-a-tv-or-other-device)
- [Setting an air conditioner](#setting-an-air-conditioner)
- [Remotes with a toggle bit](#remotes-with-a-toggle-bit)
- [Output and logging](#output-and-logging)
- [Exit status](#exit-status)

## Command line

```
piir --remote REMOTE [--feature NAME=VALUE]... [--verbose LEVEL]
piir --remote REMOTE --help
piir --list
piir --help
```

| Option | Meaning |
|--------|---------|
| `--remote`, `-r REMOTE` | the remote to use, see [Choosing a remote](#choosing-a-remote) |
| `--feature`, `-f NAME=VALUE` | a key or setting to send; give it once per feature |
| `--verbose`, `-v LEVEL` | how much to log: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL` (the default) or `NOTHING` |
| `--list`, `-l` | list every remote PIIR can find |
| `--help`, `-h` | without `--remote`: usage and the number of remotes; after `--remote`: the features of that remote |

`--remote` must come before the `--feature` options. Each run sends one code; run PIIR again for the next key.

## Choosing a remote

Remotes are JSON files in a `conf/remotes/` directory, one subdirectory per manufacturer. PIIR looks in two places, in this order:

1. `conf/remotes/` below the current directory, for remotes of your own or a checkout of the source
2. the installation directory: `/usr/local/share/piir/conf/remotes/`, or `/usr/share/piir/conf/remotes/` for the release packages

`REMOTE` can be:

| Form | Example | |
|------|---------|--|
| manufacturer and model | `sony/RM-ED011` | as `piir --list` prints it |
| file name only | `RM-ED011`, `hvac_panasonic` | when it is unique among all remotes; otherwise PIIR lists the matching remotes |
| path to a file | `./my-aircon.json`, `/home/pi/remotes/tv.json` | any JSON file, also outside the remote directories |

Find a remote:
```
piir --list | grep -i samsung
piir --list | wc -l
```

The release packages contain only PIIR's own remotes. If a manufacturer is missing, add its remotes with `piir-remotes`, see [Managing remotes](remotes.md#adding-remotes-to-an-installation). If your remote is not there at all, convert a LIRC file or record it, see [Managing remotes](remotes.md).

## Features: keys and settings

What a remote can send is described by its *features*. Each feature has a name and a list of values. Show them with `--help` after the remote:
```
$ piir --remote sony/RM-ED011 --help
Features in config file /usr/share/piir/conf/remotes/sony/RM-ED011.json:
       key:
              KEY_POWER
              KEY_MUTE
              KEY_VOLUMEUP
              ...
```

Names and values are case sensitive and must be written exactly as listed.

## Sending a key of a TV or other device

Remotes converted from LIRC have a single feature, `key`, with a value for each button:
```
sudo piir --remote sony/RM-ED011 --feature key=KEY_POWER
sudo piir --remote sony/RM-ED011 --feature key=KEY_VOLUMEUP
```

The code includes the repeats the original remote sends with a short press, so one run is one button press. To press a button several times, run PIIR several times, e.g. five steps of volume:
```
for i in 1 2 3 4 5; do sudo piir -r sony/RM-ED011 -f key=KEY_VOLUMEUP; sleep 0.3; done
```

## Setting an air conditioner

An air conditioner remote sends all its settings in every code, so the features are its settings, and one command sets everything at once:
```
$ piir --remote hvac_panasonic --help
Features in config file /usr/share/piir/conf/remotes/panasonic/hvac_panasonic.json:
       temperature:
              8
              10
              16
              ...
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
              ...
       updown:
              ...
       leftright:
              ...

$ sudo piir --remote hvac_panasonic --feature mode=HEAT --feature temperature=21 \
      --feature option=AUTO --feature fan=AUTO --feature updown=HIGH --feature leftright=MIDDLE
```

A feature you leave out keeps the value in the remote's template, which is the remote's default setting, not the device's current one: the device does not tell PIIR its state. Give every feature you care about in every command.

Features are applied in the order they appear in the remote file, not the order on the command line, so where two features set the same bits the one later in the file wins. Remotes use this to copy the real remote's behaviour, e.g. for `hvac_panasonic` `mode=FAN` always sends 27 degrees and `option=QUIET` sets the fan speed. Some remotes also have *overrides* that apply only to combinations, e.g. a fan mode that is forced only while the power is on. Giving the same feature twice uses the last value.

PIIR's own air conditioner remotes:

| Remote | Devices | Features |
|--------|---------|----------|
| `panasonic/hvac_panasonic` | Panasonic air conditioners, e.g. JKE models | temperature, mode, option, fan, updown, leftright |
| `nibe/hvac_nibe` | Nibe air to air heat pumps | power, mode, fan, temperature, vertical, horizontal |
| `carrier/hvac_carrier_nqv` | Carrier NQV air conditioners | power, mode, fan, temperature, vertical, horizontal |

The Nibe and Carrier remotes send what the [arduino-heatpumpir](https://github.com/ToniA/arduino-heatpumpir) library sends for them. Remotes for its other models can be derived with `ir2piir`, see [Managing remotes](remotes.md#deriving-a-remote-from-recorded-codes).

## Remotes with a toggle bit

Some protocols, e.g. Philips RC5 and RC6, flip a bit on every new press of any key, so the device can tell two presses of `5` (`55`) from one long press. PIIR does this automatically for remotes with a `toggle` section: it remembers the state of each remote in a file and flips it after every successful transmission.

| Running as | State file |
|------------|------------|
| any user, with `PIIR_STATE_DIR` set | `$PIIR_STATE_DIR/toggle-state` |
| root (e.g. with `sudo`) | `/var/lib/piir/toggle-state` |
| other users | `$XDG_STATE_HOME/piir/toggle-state`, by default `~/.local/state/piir/toggle-state` |

If PIIR runs both with and without `sudo`, set `PIIR_STATE_DIR` so both use the same file. Deleting the file starts all remotes from the first press again. If the file cannot be written, PIIR logs a warning and still transmits.

## Output and logging

By default PIIR prints nothing when it succeeds, and only fatal errors otherwise. Log messages go to standard error, with the time, level and source line. (The `pigpio` backend also prints a `gpioWaveTxSend OK` line.) `--verbose` shows more:

| Level | Shows |
|-------|-------|
| `INFO` | the remote file, its description and the code sent |
| `DEBUG` | also the toggle state, the transmitter device and the symbols transmitted |
| `TRACE` | also checksum calculations and pulse counts |

```
sudo piir -r hvac_panasonic -f mode=HEAT -f temperature=21 --verbose=DEBUG
```

The code is shown as a string of symbols, one character per mark and space pair of the remote's `symboldefinitions`, e.g. `H-01000000-...`. See [Remote file format](remote-format.md).

## Exit status

| Status | Meaning |
|--------|---------|
| 0 | the code was transmitted, or `--list` printed the remotes |
| 1 | an error: unknown remote or feature, invalid remote file, transmitter not available or not writable. `--help` also exits with 1, with or without `--remote`. |

Scripts can check it, e.g. `sudo piir -r tv -f key=KEY_POWER || echo "sending failed"`. Log messages, including errors from the transmitter, go to standard error; messages about the command line (unknown remote or feature, usage) go to standard output. The causes of errors are described in [Troubleshooting](troubleshooting.md).
