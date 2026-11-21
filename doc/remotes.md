# Managing remotes

- [Where remotes come from](#where-remotes-come-from)
- [Adding remotes to an installation](#adding-remotes-to-an-installation)
- [Your own remotes](#your-own-remotes)
- [Converting a LIRC configuration](#converting-a-lirc-configuration)
- [Deriving a remote from recorded codes](#deriving-a-remote-from-recorded-codes)
- [Air conditioners supported by arduino-heatpumpir](#air-conditioners-supported-by-arduino-heatpumpir)
- [Writing a remote by hand](#writing-a-remote-by-hand)

## Where remotes come from

PIIR has more than 3000 remotes from over 500 manufacturers in [conf/remotes](../conf/remotes):

- **PIIR's own remotes**, made for PIIR: `panasonic/hvac_panasonic`, `nibe/hvac_nibe` and `carrier/hvac_carrier_nqv`. The release packages contain these.
- **Remotes converted from the [LIRC remote database](https://sourceforge.net/p/lirc-remotes/code/ci/master/tree/remotes)**, for TVs, receivers, set-top boxes, projectors and much more. Each release has them in `piir-remotes-<version>.tar.gz`.

If your device is not among them, make a remote:

| You have | Use |
|----------|-----|
| a `lircd.conf` file for the remote | [lirc2piir.py](#converting-a-lirc-configuration) |
| the remote and an IR receiver, or software that generates the codes | [ir2piir.py](#deriving-a-remote-from-recorded-codes) |
| a description of the protocol | [write the remote file](#writing-a-remote-by-hand) |

## Adding remotes to an installation

`piir-remotes`, installed with PIIR, adds remotes from the archive of all remotes to the installation. It downloads the archive of the installed PIIR version from GitHub (and keeps it in `~/.cache/piir`), or uses one you downloaded:

```
piir-remotes available sony                 # remotes in the archive containing "sony"
sudo piir-remotes add sony                  # every Sony remote
sudo piir-remotes add samsung/AA59-00600A 'lg/AKB*'
piir-remotes installed                      # what is installed
sudo piir-remotes remove sony
sudo piir-remotes add all                   # everything: a 2.7 MB download, about 21 MB installed
```

| Command | |
|---------|--|
| `available [TEXT]` | list the remotes in the archive, optionally only those containing TEXT (case insensitive) |
| `installed` | list the installed remotes |
| `add REMOTE...` | install remotes |
| `remove REMOTE...` | remove installed remotes |

`REMOTE` is a manufacturer (`sony`), a remote (`sony/RM-ED009`), a pattern (`'samsung/AA59-*'`, quoted so the shell does not expand it) or `all`.

| Option | |
|--------|--|
| `--archive FILE` | use this `piir-remotes-*.tar.gz` instead of downloading it, e.g. on a Pi without internet access |
| `--dir DIR` | the remote directory to change (default: the installation's) |
| `--version VERSION` | the release to download the archive of (default: the installed version) |

When building from source, `-DPIIR_REMOTES` chooses the remotes `cmake --install` installs, see [Installation](installation.md#build-options).

## Your own remotes

PIIR finds remotes in `conf/remotes/` below the current directory before the installed ones, and `--remote` also takes a path. So a remote of your own can be:

- in the installation directory, e.g. `/usr/local/share/piir/conf/remotes/mybrand/aircon.json` (`--remote mybrand/aircon`),
- in `conf/remotes/` of the directory you run PIIR from, or
- anywhere, given by its path: `--remote /home/pi/remotes/aircon.json`.

A remote in the installation directory is replaced when you reinstall the same name with `piir-remotes`, so give your own remotes their own manufacturer directory or file name.

## Converting a LIRC configuration

[tools/lirc2piir.py](../tools/lirc2piir.py) converts LIRC configuration files (`lircd.conf`) to PIIR remotes. It needs only Python 3:

```
tools/lirc2piir.py convert -o conf/remotes my-remote.lircd.conf
tools/lirc2piir.py convert -o conf/remotes path/to/lirc-remotes/remotes      # a whole directory
```

| Option | |
|--------|--|
| `-o`, `--output DIR` | where to write the JSON files. A file is written as `DIR/<name>.json`; converting a directory keeps its subdirectories, so the LIRC database's `<manufacturer>/<remote>` layout is kept |
| `--report FILE` | write the remotes and keys that were skipped, and why, to FILE |

The converted remote has one feature, `key`, with a value for every button. Its encoder follows the LIRC 0.10.2 sources, so a key sends exactly what `irsend SEND_ONCE` sends, including the repeats the remote needs (`min_repeat`) and the gaps between them. `tools/lirc2piir.py verify PATH` compares the encoder with LIRC's own `irsimsend`, and `tools/lirc2piir.py simsend FILE` prints the pulses for a file as `irsimsend` would.

Not everything converts:
- remotes made for IR receivers (e.g. irman) that store scan codes instead of timings, and the rarely used RCMM, XMP, Grundig, B&O and serial encodings
- keys that need more than 1024 pulses and spaces, keys LIRC cannot send, and duplicate key names (the first one is used, as in LIRC)

Raw recorded codes whose many different durations do not fit PIIR's symbols are rounded by at most 2%; LIRC itself accepts 30% when receiving. Remotes with a toggle bit (RC5, RC6 and others with `toggle_bit_mask`) get a [toggle](remote-format.md#toggle) section.

## Deriving a remote from recorded codes

[tools/ir2piir.py](../tools/ir2piir.py) builds a remote from recorded codes and the settings each was sent for. It is made for air conditioners and similar remotes that send all settings in every code, but works for any remote whose codes use pulse distance or pulse width coding. It needs only Python 3.

### 1. Record

Record codes with an IR receiver (e.g. with `ir-ctl --receive` on a Pi with the `gpio-ir` overlay), or from software that generates them. Write one code per line in a [JSON Lines](https://jsonlines.org/) file, with the settings it was sent for:

```
{"features": {"mode": "HEAT", "temperature": "21", "fan": "AUTO"}, "frequency": 38000, "durations": [3500, 1750, 435, 435, 435, 1300, ...]}
```

- `durations` alternate mark and space in microseconds, starting with a mark; a final space may be left out.
- `frequency` is optional (default 38000).
- Every line names the same features. Blank lines and lines starting with `#` are skipped.

The captures must contain:
- a base code: the first line,
- for every value of every feature, a code where only that feature differs from the base,
- random combinations of the features: about 300 for six features, and 1000 or more when settings interact (e.g. a temperature only some modes accept, or a fan speed forced in some modes).

### 2. Derive

```
tools/ir2piir.py captures.jsonl -o conf/remotes/mybrand/aircon.json --description "My air conditioner"
```

ir2piir prints the symbols, checksums and features it found. It then encodes every capture again and lists those the remote does not reproduce; the exit status is 1 if there are any. Fix the captures (often: record more combinations) and run it again.

### 3. Check

To see whether the remote also gets combinations right that were not recorded, record another set and check against it:
```
tools/ir2piir.py captures.jsonl --check more-captures.jsonl -o conf/remotes/mybrand/aircon.json
```

| Option | |
|--------|--|
| `-o`, `--output FILE` | the remote to write (default: standard output) |
| `--description TEXT` | the remote's description |
| `--dutycycle`, `--outpin` | written into the remote |
| `--tolerance X` | relative difference of durations still counted as the same symbol (default 0.2) |
| `--tests N` | number of captures stored as test vectors (default 30) |
| `--check FILE` | captures to check the remote against, not used to derive it |
| `--fallback` | hard-code the recorded codes the remote does not reproduce |
| `--no-separators` | do not insert `-` between bytes |

### What it finds

ir2piir clusters the durations into symbols (`0`, `1`, headers, gaps), finds [checksums](remote-format.md#checksums) (sums, XORs, counts of set bits and CRCs of bytes or nibbles), gives each feature the bits its values change, finds values that force other features (e.g. a fan mode forcing a temperature) and writes those that only happen together with another value as [overrides](remote-format.md#overrides). Codes of another length, e.g. a short power off code, become [variants](remote-format.md#variants).

It cannot handle: RC5/RC6 style (Manchester) coding, codes without a final stop mark, CRCs wider than one byte, and checksums or settings it cannot describe. For those, `--fallback` hard-codes the recorded codes it does not reproduce: record every combination, because only recorded combinations are then sent exactly. That can be tens of thousands of codes and a remote of several megabytes.

## Air conditioners supported by arduino-heatpumpir

The [arduino-heatpumpir](https://github.com/ToniA/arduino-heatpumpir) library knows the protocols of 46 air conditioner and heat pump models. [tools/heatpumpir](../tools/heatpumpir) records its codes as captures, so ir2piir can make PIIR remotes of them. The library (GPL-2.0) is not part of PIIR; build the recorder against a checkout of it:

```
git clone https://github.com/ToniA/arduino-heatpumpir.git
cmake -S tools/heatpumpir -B build/heatpumpir -DHEATPUMPIR_DIR=$PWD/arduino-heatpumpir
cmake --build build/heatpumpir
build/heatpumpir/heatpumpir_capture list                    # the models
build/heatpumpir/heatpumpir_capture gree 3000 1 > gree.jsonl
build/heatpumpir/heatpumpir_capture gree 1000 2 > gree-check.jsonl
tools/ir2piir.py gree.jsonl --check gree-check.jsonl -o conf/remotes/gree/hvac_gree.json
```

`heatpumpir_capture MODEL [COUNT [SEED]]` records the base code, every single change and COUNT random combinations; `heatpumpir_capture MODEL all` records every combination. Models that send the time of day get 00:00.

ir2piir derives a remote that sends the same codes as the library for 44 of the models (from 1000 to 3000 random combinations, checked against 1000 others). The remaining two, Nibe and Carrier NQV, are in PIIR as `nibe/hvac_nibe` and `carrier/hvac_carrier_nqv`, written from the library's source.

## Writing a remote by hand

A remote is a JSON file describing the symbols, a template code and what each feature value changes in it. The [Remote file format](remote-format.md) describes every setting, with a worked example. Test a new remote with `--verbose=DEBUG`, which shows the code sent, and add the codes your real remote sends as [test vectors](remote-format.md#tests).
