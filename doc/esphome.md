# irlingo on ESPHome

irlingo can run on an ESP32 as an [ESPHome](https://esphome.io/) external
component. The remotes are the same JSON files as on the Raspberry Pi, and the
same C code encodes them, so a remote sends the same code on both.

Status: preview. The component sends codes from automations (`irlingo.send`),
and gives Home Assistant buttons and air conditioner (climate) entities. See
[the plan](esphome-plan.md) for what is next.

- [Requirements](#requirements)
- [Configuration](#configuration)
- [Sending codes](#sending-codes)
- [Buttons](#buttons)
- [Air conditioners](#air-conditioners)
- [Remotes](#remotes)
- [Toggle bits](#toggle-bits)
- [Memory and limits](#memory-and-limits)
- [Troubleshooting](#troubleshooting)

## Requirements

- An ESP32 board (the classic ESP32; other chips are untested) with an IR LED
  circuit, see [Connecting an IR LED](hardware.md). To test the board first, see
  [Testing with a visible LED](hardware.md#testing-with-a-visible-led).
- ESPHome 2026.11 or newer.
- A copy of the irlingo repository, until the component is released: the
  component is in its `components/irlingo/` folder and finds the remotes in its
  `conf/remotes/` folder.

## Configuration

A complete example is [examples/esphome-irlingo.yaml](examples/esphome-irlingo.yaml).
The parts for irlingo:

```yaml
external_components:
  - source:
      type: local
      path: /home/pi/irlingo/components   # your copy of the repository
    components: [irlingo]

remote_transmitter:
  id: ir_tx
  pin: GPIO4
  carrier_duty_percent: 50%

irlingo:
  - id: aircon
    remote: panasonic/hvac_panasonic
  - id: tv
    file: my-remotes/livingroom-tv.json
```

Each entry under `irlingo:` is one remote:

| Setting | Meaning |
|---------|---------|
| `id` | the name automations use for it |
| `remote` | a remote in `conf/remotes`, e.g. `sony/RM-ED011`, or only its file name, e.g. `RM-ED011`, when that is unique |
| `file` | instead of `remote`: your own remote file, relative to the YAML file, e.g. one made with [ir2irlingo](remotes.md) |
| `transmitter_id` | the `remote_transmitter` to send with; needed only when there are several |

The carrier duty cycle is set on the `remote_transmitter`
(`carrier_duty_percent`), not by the remote's `dutycycle`; 50% suits almost all
remotes.

## Sending codes

The `irlingo.send` action sends a code, with the features you would give the
`irlingo` command as `--feature name=value`:

```yaml
on_...:
  - irlingo.send:
      remote_id: aircon
      features:
        mode: HEAT
        temperature: "21"
```

- Values are text: quote numbers, e.g. `"21"`.
- Features you leave out keep the remote's template, as with the command.
- Feature names and fixed values are checked when compiling, e.g.
  `Remote panasonic/hvac_panasonic has no feature mode=TURBO (values: AUTO, HEAT, COOL, FAN, OFF)`.
- A value can come from a lambda, e.g. `key: !lambda 'return std::string("Power");'`.
  It is checked on the device; an unknown value is logged and nothing is sent.

The action works in any ESPHome automation: `on_boot`, a button's `on_press`,
`interval`, or a Home Assistant action through ESPHome's
[native API](https://esphome.io/components/api/).

## Buttons

A button sends a code with fixed features when pressed, from Home Assistant or
with ESPHome's `button.press` action:

```yaml
button:
  - platform: irlingo
    remote_id: tv
    name: TV power
    features:
      key: Power
```

## Air conditioners

A climate entity turns an air conditioner remote into a thermostat in Home
Assistant. Every change sends one code with the features of the mode, the
target temperature and the fan speed:

```yaml
climate:
  - platform: irlingo
    remote_id: aircon
    name: Living room air conditioner
    fixed: {option: AUTO, updown: AUTO, leftright: AUTO}
```

For most remotes that is all: the mapping is guessed from the remote's
features.

| Home Assistant | Guessed from |
|----------------|--------------|
| Modes | the `mode` feature: `AUTO` becomes heat/cool, `HEAT` heat, `COOL` cool, `DRY` dry, `FAN` fan only, `OFF` off |
| Off | `mode: OFF`, or a `power` feature with `ON` and `OFF` (the other modes then also send `power: ON`) |
| Target temperature | the `temperature` feature's numeric values; the nearest one is sent |
| Fan speed | the `fan` feature: `AUTO` becomes auto, the other values are offered by their names |

Settings, all optional:

| Setting | Meaning |
|---------|---------|
| `modes` | Home Assistant modes (`off`, `heat_cool`, `cool`, `heat`, `dry`, `fan_only`, `auto`) and the features each sends, e.g. `heat: {mode: HEAT, power: ON}` |
| `temperature` | the feature for the target temperature, if it is not called `temperature` |
| `fan` | the feature for the fan speed, if it is not called `fan` |
| `fixed` | features sent with every code, e.g. the vanes |
| `sensor` | a sensor giving the current room temperature |

```yaml
climate:
  - platform: irlingo
    remote_id: carrier
    name: Bedroom air conditioner
    modes:
      cool: {mode: COOL, power: ON}
      heat: {mode: HEAT, power: ON}
      off: {power: OFF}
    fixed: {vertical: AUTO, horizontal: AUTO}
    sensor: bedroom_temperature
```

The modes and their features are checked when compiling. After a restart the
entity shows its last state again without sending it, so a restart does not
switch the air conditioner. An IR remote cannot tell whether the air
conditioner received a code, so Home Assistant shows what was sent last.

Avoid `/` in names (e.g. "A/C"): ESPHome replaces it and from version 2027.7
rejects it.

## Remotes

The remotes you list are built into the firmware, without their test vectors.
On every send the device parses the remote, applies the features, overrides,
variants, checksums and plugin, and turns the code into pulses for
`remote_transmitter`, as the `irlingo` command does on Linux. The log shows the
code (at level `DEBUG`) and how many pulses were sent.

## Toggle bits

Remotes with a toggle bit (see [Toggle bits](remotes.md)) send a different code
on every other press. The component remembers which code is next in the ESP32's
flash, so it survives restarts and reflashing. ESPHome writes such values to
flash at most once a minute (its `preferences: flash_write_interval`), so a press
just before a power cut can be forgotten.

## Memory and limits

Measured on an ESP32 with `panasonic/hvac_panasonic`:

| | |
|-|-|
| Flash for irlingo (code) | about 44 KB |
| Flash per remote | its size without test vectors: 12 KB for `hvac_panasonic`; most remotes 2-14 KB |
| Memory while sending | about 1.4 times the remote's size, 16 KB for `hvac_panasonic`, freed afterwards |
| Time to send | about 10 ms to parse and encode |

ESPHome warns about remotes over 64 KB. Remotes with hard-coded codes from
`ir2irlingo --fallback` can be megabytes, too large for an ESP32.

## Troubleshooting

- **Nothing happens:** check the transmitter and LED with
  [Testing with a visible LED](hardware.md#testing-with-a-visible-led), and the
  log for `Sent ...` lines.
- **`Remote ... has no feature ...`:** use a feature and value the remote has.
  `irlingo --remote NAME --help` on Linux lists them, as does the error message.
- **`Unknown remote ...` or `... is ambiguous`:** use the remote's full name,
  `manufacturer/model`, as `irlingo --list` shows it.
- **`Field 'key' is being redacted`:** a warning from ESPHome 2026.11, which
  hides settings named like secrets in its output. Remotes converted from LIRC
  call their buttons `key`; the warning is harmless.
