# Remote file format

A remote is a JSON file. It describes the symbols a remote sends (a mark and a space each), a template code made of those symbols, and for each feature value the symbols it changes in the template. irlingo applies the requested features, then overrides and checksums, and transmits the result.

- [A worked example](#a-worked-example)
- [How a code is built](#how-a-code-is-built)
- [Settings](#settings)
  - [description, source](#description-source)
  - [frequency, dutycycle](#frequency-dutycycle)
  - [transmitter](#transmitter)
  - [symboldefinitions](#symboldefinitions)
  - [template](#template)
  - [features](#features)
  - [overrides](#overrides)
  - [variants](#variants)
  - [checksums](#checksums)
  - [toggle](#toggle)
  - [plugin](#plugin)
  - [tests](#tests)
- [Limits](#limits)

## A worked example

[examples/example-aircon.json](examples/example-aircon.json) is a small air conditioner remote. Its code is a header, three bytes and a stop mark: byte 0 holds the power, mode and fan, byte 1 the temperature, and byte 2 is the sum of bytes 0 and 1.

```json
{
  "description": "Example air conditioner: power, mode, fan and temperature, with a checksum",
  "frequency": 38000,
  "dutycycle": 0.5,
  "transmitter": {"outpin": 17},
  "symboldefinitions": {
    "H": {"mark": 9000, "space": 4500},
    "0": {"mark": 560, "space": 560},
    "1": {"mark": 560, "space": 1690},
    "-": {"mark": 0, "space": 0}
  },
  "template": "H-10000000-01100000-00000000-0",
  "features": {
    "power": {
      "ON":  "  1",
      "OFF": "  0"
    },
    "mode": {
      "AUTO": "   00",
      "COOL": "   10",
      "HEAT": "   01",
      "DRY":  "   11"
    },
    "fan": {
      "AUTO": "     00",
      "LOW":  "     10",
      "HIGH": "     01"
    },
    "temperature": {
      "16": "           0000",
      "17": "           1000",
      ...
      "22": "           0110"
    }
  },
  "overrides": [
    {"when": {"power": "ON", "mode": "DRY"}, "code": "     10"}
  ],
  "checksums": [
    {"type": "sum", "from": 0, "to": 1, "at": 2}
  ],
  "tests": [
    {"description": "heating to 21 degrees",
     "features": {"power": "ON", "mode": "HEAT", "fan": "AUTO", "temperature": "21"},
     "outcome": "H-10100000-10100000-01010000-0"},
    ...
  ]
}
```

Try it from the root of the source:
```
irlingo --remote doc/examples/example-aircon.json --help
sudo irlingo --remote doc/examples/example-aircon.json -f mode=HEAT -f temperature=21 --verbose=INFO
```

For `mode=HEAT`, `temperature=21`:

1. The template `H-10000000-01100000-00000000-0` is the remote's default setting: power on, mode AUTO, fan AUTO, 22 degrees.
2. `mode` `HEAT` (`   01`) replaces the symbols at positions 3 and 4 (counting from 0), `temperature` `21` (`           1010`) those at 11 to 14: `H-10100000-10100000-00000000-0`. `power` and `fan` are not given, so they keep the template's symbols.
3. The override does not apply: its `when` needs `mode` `DRY`.
4. The checksum reads the bytes lowest bit first: byte 0 is `10100000` = 0x05, byte 1 is 0x05, so byte 2 becomes 0x0A, `01010000`.
5. irlingo transmits `H-10100000-10100000-01010000-0`: a 9000 µs mark and 4500 µs space, 24 marks of 560 µs with short or long spaces, and the stop mark. The `-` separators send nothing.

## How a code is built

1. **Variant.** If the remote has `variants`, the first one whose `when` matches the requested features is used instead of the remote's own template, features, overrides and checksums.
2. **Template.** irlingo starts from a copy of `template`.
3. **Features.** Each feature given on the command line is applied, in the order the features appear in the file (not the order on the command line). A feature's code is laid over the code: each symbol replaces the one at the same position, a space keeps it.
4. **Overrides.** Every override whose `when` matches is applied the same way, in order.
5. **Checksums.** The `checksums` are computed, in order, into the code.
6. **Plugin.** If the remote names a `plugin`, it post-processes the code.
7. **Toggle.** For a remote with a `toggle` section, on every other press the toggle codes are used instead of the feature codes.
8. **Transmission.** Each symbol is sent as its mark and space.

## Settings

| Setting | Required | Meaning |
|---------|----------|---------|
| `description` | no | shown by `--verbose=INFO` |
| `frequency` | yes | carrier frequency in Hz, usually 38000 |
| `dutycycle` | yes | share of the carrier period the LED is on, 0 to 1, usually 0.5 or 0.33 |
| `transmitter` | no | `outpin` and `device` |
| `symboldefinitions` | yes | the symbols and their durations |
| `template` | yes | the default code |
| `features` | yes | the features and the codes of their values |
| `overrides` | no | codes applied for combinations of feature values |
| `variants` | no | other templates for some feature values |
| `checksums` | no | checksums computed into the code |
| `toggle` | no | codes for every other press |
| `plugin` | no | a built-in post-processing step |
| `tests` | no | codes the real remote sends, checked by the test suite |

Settings irlingo does not know, e.g. `source`, are ignored.

### description, source

`description` is free text. Remotes converted from LIRC also have `source`, naming the LIRC file and converter version they came from; the CI check of converted remotes uses it, so do not add it to remotes of your own.

### frequency, dutycycle

The carrier is the frequency at which the LED blinks during a mark. Most remotes use 36000-40000 Hz; Bang & Olufsen and a few others use other frequencies. `dutycycle` is the share of each carrier period the LED is on. With the `lirc` backend the kernel driver generates the carrier; drivers that cannot set the frequency or duty cycle log a warning and use their default.

### transmitter

```json
"transmitter": {"outpin": 17, "device": "/dev/lirc0"}
```

| Key | Backend | Meaning |
|-----|---------|---------|
| `outpin` | `gpiod`, `pigpio` | Broadcom GPIO number of the LED. Ignored by `lirc`, where the `gpio-ir-tx` overlay sets the pin. |
| `device` | `lirc` | transmitter device, default `/dev/lirc0` |

### symboldefinitions

Each symbol is a single character with a mark and a space in microseconds:

```json
"symboldefinitions": {
  "H": {"mark": 3500, "space": 1750},
  "0": {"mark": 435, "space": 435},
  "1": {"mark": 435, "space": 1300},
  "W": {"mark": 435, "space": 10000},
  "-": {"mark": 0, "space": 0}
}
```

- A symbol with mark and space 0, conventionally `-`, sends nothing: use it to make codes readable, e.g. between bytes, and as a marker for [checksums](#checksums).
- A space of 0 is allowed, e.g. for a final stop mark.
- Checksums read `0` and `1` as bits unless they name other symbols with `bits`.
- Remote controls send a final stop mark after the last bit (`0` in the example); without it the receiver cannot measure the last space.

### template

The code sent when no feature changes it, written in symbols. It sets the length of the code: feature and override codes longer than the template are cut off (with a warning). A remote whose codes all come from its features, like a converted LIRC remote, can have a template of separators that is as long as the longest code, e.g. `"-------------"`.

### features

```json
"features": {
  "mode": {"AUTO": "   00", "COOL": "   10"},
  "key": {"KEY_POWER": "H10101001000A"}
}
```

An object of features, each an object of values, each a code laid over the template: symbols replace, spaces keep. A code may be shorter than the template; the rest is kept. A value with an empty code (`""`) changes nothing but is accepted on the command line, e.g. a setting the device ignores, or one only a variant or override uses.

The order of features in the file matters: a feature later in the file overwrites what an earlier one set. Put a feature that forces another (e.g. a mode that forces the fan speed) after it.

### overrides

```json
"overrides": [
  {"when": {"mode": "DRY", "power": "ON"}, "code": "     10"}
]
```

After all features, every override whose `when` features are all requested with these values is applied, in order, like a feature's code. Use them for settings that interact, e.g. a dry mode forcing the lowest fan speed only while the power is on, a flap position that is not allowed in heating, or a fan speed AUTO that becomes HIGH in fan mode. `when` can name any number of features. A feature in `when` that is not requested does not match.

### variants

```json
"variants": [
  {"when": {"power": "OFF"}, "template": "H-00101000-11000110-00000000-00001000-00001000-01000000-10111111-0"}
]
```

Some remotes send codes of another length or structure for some settings, e.g. a short code for power off. Each variant has the feature values that select it (`when`), its own `template`, and optionally its own `features`, `overrides` and `checksums`, whose positions refer to its template. The first variant whose `when` features are all requested is used instead of the remote's own template, features, overrides and checksums; the symbol definitions are shared.

A variant's `features` do not inherit the remote's: a variant without `features` sends its template unchanged. The values that select a variant must still be listed in the remote's own `features` (possibly with an empty code), so that irlingo accepts them on the command line.

Variants are also useful when a checksum depends on feature values in a way a checksum setting cannot express: `carrier/hvac_carrier_nqv` has a variant with its own checksum start value for each mode and fan speed that needs one.

### checksums

```json
"checksums": [
  {"type": "sum", "from": 8, "to": 25, "at": 26}
]
```

After features and overrides, the bit symbols of the code (`0` and `1`) are read as bytes of 8 bits, or nibbles of 4 bits, numbered from 0 in the order they are sent. All other symbols, such as headers and separators, are skipped, so bytes do not need to line up with the separators. Each checksum combines the units `from` to `to`, or those listed in `data`, and writes the result into unit `at`.

| Setting | Meaning | Default |
|---------|---------|---------|
| `type` | `sum` (modulo 256, or 16 for nibbles), `xor`, `popcount` (the number of bits set to 1) or `crc` | required |
| `unit` | `byte` or `nibble`; `from`, `to`, `data`, `at` and `init` then count in that unit | `byte` |
| `from`, `to` | first and last unit covered | required unless `data` is given |
| `data` | list of the units covered, e.g. `[0, 2, 4]`, instead of `from` and `to` | |
| `at` | unit the result is written to, not one of those covered, or a list of units for a wider result (up to 32 bits) | required |
| `init` | start value, 0-255 (0-15 for nibbles, more for a wider result) | `0` |
| `invert` | complement the result (`~x`) | `false` |
| `negate` | negate the result (`0 - x`), after `invert` | `false` |
| `xorout` | XOR the result with this value, after `invert` and `negate` | `0` |
| `poly` | `crc` only: the polynomial without its highest bit, e.g. `7` for x⁸ + x² + x + 1 | required for `crc` |
| `reflect` | `crc` only: reflected CRC (each unit enters lowest bit first, the result is reversed) | `false` |
| `bitorder` | `lsb` when a unit is sent lowest bit first, `msb` when highest bit first | `lsb` |
| `bits` | the zero and one symbols | `"01"` |
| `restart` | symbols after which units are counted from a new bit; the bits of a partial unit before them are skipped | none |

Checksums are applied in order, so a later one can cover the unit an earlier one wrote. irlingo stops with `Invalid checksum` when a checksum is malformed or refers to units beyond the end of the code.

**Nibbles** are numbered in the order they are sent: in a byte sent lowest bit first (`lsb`), nibble 0 is its low nibble; with `msb` it is its high nibble. A 4-bit sum of nibbles 0, 2 and 4 into nibble 7, sent highest bit first:
```json
{"type": "sum", "unit": "nibble", "data": [0, 2, 4], "at": 7, "bitorder": "msb"}
```

**Counts of set bits.** 28 minus the number of bits set in bytes 2 to 5, in the high nibble of byte 1, is a `popcount` with start value 4, negated: 0 - (4 + count) is 28 - count modulo 16:
```json
{"type": "popcount", "unit": "nibble", "from": 4, "to": 11, "at": 3, "init": 4, "negate": true}
```

**Wider results.** A result wider than a unit goes into the units listed in `at`: with `lsb` the first holds the lowest bits, with `msb` the highest. Samsung AQV sends the complement of its count of set bits, 8 bits, in the high nibble of byte 1 and the low nibble of byte 2:
```json
{"type": "popcount", "unit": "nibble", "data": [0, 1, 2, 5, 6, 7, 8, 9, 10, 11, 12, 13], "at": [3, 4], "invert": true}
```

**CRCs** take the parameters of the [CRC catalogue](https://reveng.sourceforge.io/crc-catalogue/): `poly`, `init`, `reflect` (its `refin` and `refout`, which must be the same) and `xorout`; the width is that of `at`. The units enter the CRC as read with `bitorder`, so `"bitorder": "msb"` sends the bits in the order a CRC without `reflect` reads them. CRC-8/SAE-J1850 over bytes 0 to 8 into byte 9, and CRC-16/MODBUS into bytes 9 and 10, low byte first:
```json
{"type": "crc", "poly": 29, "init": 255, "xorout": 255, "from": 0, "to": 8, "at": 9, "bitorder": "msb"}
{"type": "crc", "poly": 32773, "init": 65535, "reflect": true, "from": 0, "to": 8, "at": [9, 10]}
```

**Frames that do not end on a byte.** Some protocols end a frame, or a part of it, with a few extra bits, which shifts the bytes after them. With `"restart": "W"`, counting starts again after the gap symbol `W`. Where there is no gap, put a separator there and restart after it: `nibe/hvac_nibe` sends 2 bits before its checksum byte, so its template has a `-` after them and its checksum `"restart": "-"`. Separators between whole bytes do not change the counting.

### toggle

```json
"toggle": {"key": {"KEY_POWER": "000000011001A"}}
```

Protocols such as RC5 and RC6 flip a bit on every new press. A remote with a `toggle` section, with the same structure as `features`, sends the toggle codes on every other press (a value missing there sends its normal code). irlingo keeps the state per remote in a file, see [Usage](usage.md#remotes-with-a-toggle-bit). Variants do not use the toggle section.

### plugin

```json
"plugin": "hvac_panasonic"
```

A post-processing step compiled into irlingo, for what the settings above cannot express. The only plugin is `hvac_panasonic`, the checksum of Panasonic air conditioners, which `panasonic/hvac_panasonic` now computes with a `checksums` entry instead. Remote files from PIIR 1.x without `plugin`, `checksums` or `variants`, whose path contains `hvac_panasonic`, still get it.

### tests

```json
"tests": [
  {"description": "No 1",
   "features": {"temperature": "21", "mode": "AUTO"},
   "outcome": "H-01000000-00000100-..."}
]
```

Codes a real remote sends, with the features they were sent for. irlingo does not use them when sending; the test suite checks that every remote in `conf/remotes/` produces exactly each `outcome`, separators included. Vectors that do not match yet are listed in `tests/known_failures.txt`. They are the best way to make sure a hand-written remote is right.

## Limits

| Limit | Value |
|-------|-------|
| symbols in a code | 2048 |
| marks and spaces sent by the `lirc` backend | 1024 |
| on and off periods generated by the `gpiod` and `pigpio` backends | 12000: every carrier period of a mark counts as two, so about 6000 periods in all marks of a code |
| checksum result | 32 bits |
