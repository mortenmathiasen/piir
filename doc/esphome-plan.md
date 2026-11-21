# Plan: irlingo on ESPHome

Status: phase 0 done, apart from a transmission to the air conditioner (see
"Phase 0 results"); phase 1 done (the portable core is in `components/irlingo/`,
with `pulses.c` and `loadConfigString()`); phase 2 done (the component with the
`irlingo.send` action, see [esphome.md](esphome.md)); phase 3 done (button and
climate platforms); phase 4 CI done (`tests/esphome/check.sh` in CI), the
release is next.

## Goal

irlingo becomes an ESPHome external component in this repository, so ESPHome
devices use the same remote files, the same encoder and the same tests as the
Raspberry Pi version:

```yaml
external_components:
  - source: github://mortenmathiasen/irlingo@v3.1.0
    components: [irlingo]

remote_transmitter:
  id: ir_tx
  pin: GPIO4
  carrier_duty_percent: 50%

irlingo:
  transmitter_id: ir_tx
  remotes:
    - id: aircon
      remote: panasonic/hvac_panasonic      # from conf/remotes
    - id: tv
      file: my-remotes/livingroom-tv.json    # or your own file, e.g. from ir2irlingo

climate:
  - platform: irlingo
    remote_id: aircon
    name: Living room A/C

button:
  - platform: irlingo
    remote_id: tv
    feature: {key: KEY_POWER}
    name: TV power
```

## Starting point (master, irlingo 3.0.0)

Portable already:
- `config.c`: features, overrides, variants
- `checksum.c`: including popcount and CRC
- `plugins.c`, `log.c` (it already has callbacks), parson

Needs changing:
- `lircBuildPulses()` is portable, but all of `lirc.c` is inside
  `#ifdef HAVE_LINUX_LIRC_H`.
- Remotes can only be loaded from a file (`json_parse_file`).
- `config.c` holds exactly one remote, in global variables.
- `state.c` (toggle state), `remotes.c` and the gpiod, pigpio and lirc
  transmit code are Linux only.

Sizes: 8 of the 3114 remotes are over 40 KB; the air conditioner remotes are
4-37 KB. Parsing JSON on the device is fine for these, once the test vectors
are stripped.

## Phases

### 0. Spike (about a day)

Throwaway code, to confirm the assumptions:
- ESPHome compiles plain `.c` files in a component folder.
- ESPHome clones the whole repository, so the component's Python code can read
  `conf/remotes` at build time.
- How fast parson parses a 30 KB remote on an ESP32, and how much memory it
  takes.
- How the `remote_transmitter` C++ API looks today: a raw transmit call with
  marks, spaces and carrier frequency.

### 1. Core refactor

In this repository; it changes nothing for the Raspberry Pi version.
- Split `lirc.c` into a portable `pulses.c` (symbols to marks and spaces) and
  the Linux device code.
- Add `loadConfigString()` next to `loadConfig()`.
- Make a portable `irlingo_core` library without `state.c`, `remotes.c` and the
  transmit backends; the Linux command line links those separately.
- Layout: move the portable sources directly into `components/irlingo/`.
  ESPHome compiles only the files directly in an external component's folder
  (subfolders are only copied for ESPHome's own core, see phase 0), and has no
  build step that could copy files, so the Linux CMake build compiles the core
  from there. Symlinks (they break on Windows) and copies (they drift apart)
  are avoided.
- Host tests for `loadConfigString()` and the split; all existing tests still
  pass.

### 2. Component, minimal version

- `components/irlingo/__init__.py`:
  - the YAML schema
  - resolves remote names from `conf/remotes`, or takes `file:` for your own
  - validates feature names and values when compiling
  - embeds only the chosen remotes, with `tests` stripped, as string constants
- `irlingo.h` and `irlingo.cpp`: the glue to `remote_transmitter`, in the same
  order as `irlingo.c`: features, checksums, plugin, pulses; the carrier
  frequency comes from the remote.
- Several remotes per device: at first the remote is parsed again on each
  send, a few milliseconds. Turning `config.c`'s globals into a handle can come
  later if needed.
- Toggle bit: kept by the glue in ESPHome preferences instead of `state.c`. The
  core already takes `toggled` as a parameter.
- An `irlingo.send` action with remote and features, for automations and Home
  Assistant actions.

### 3. Entities

- `button` platform: one button per given feature set, e.g. TV keys.
- `climate` platform: maps Home Assistant's mode, target temperature, fan and
  swing to remote features:
  - defaults for the naming of the remotes derived from arduino-heatpumpir
    (`power`, `mode`, `fan`, `temperature`, `vertical`, `horizontal`)
  - an explicit `mapping:` for others, e.g. `hvac_panasonic` with `mode: OFF`,
    `option` and `updown`
  - the temperature range and steps taken from the remote's values
  - probably built on ESPHome's `climate_ir` base class, which already handles
    the transmitter and an optional temperature sensor

### 4. CI, examples, documentation, release

- A GitHub Actions job runs `esphome compile` for ESP32 on example YAMLs: an air
  conditioner, a TV and a remote from your own file.
- `doc/esphome.md`: wiring, YAML, choosing remotes, climate mapping.
- Release 3.1.0, so users can pin the tag.

### 5. Later, as needed

- Convert remotes to compact C tables at build time, to save memory and to
  support ESP8266.
- Bit-packed tables for remotes made with `ir2irlingo --fallback`: about 400 KB
  instead of 3.8 MB for samsung_fjm.
- Turning the configuration into handles.
- Possibly submitting the component to ESPHome itself.

## Phase 0 results

Done on 2026-11-21 with ESPHome 2026.11.0, without hardware. The spike (in the
ignored `build/esphome-spike/`) is a component holding a copy of the portable
core, with `lircBuildPulses()` moved to a portable `pulses.c` and a
`loadConfigString()` added to `config.c`. It embeds `panasonic/hvac_panasonic`
and sends one setting through `remote_transmitter`.

Confirmed:
- `remote_transmitter`: `transmit()` returns a call whose `get_data()` takes
  `set_carrier_frequency()`, `mark()` and `space()`; `perform()` sends. The
  duty cycle is the transmitter's `carrier_duty_percent`.
- External components from git are fetched with `git clone --depth=1` of the
  whole repository, so `conf/remotes` is there when the component's Python code
  runs. The spike finds it from its own location.
- ESPHome compiles `.c` files of a component (`.c`, `.cpp`, `.h` and others).
- Feature names and values are checked when compiling: a wrong value gives
  "Remote panasonic/hvac_panasonic has no feature mode=TURBO".
- The firmware compiles for ESP32 (esp-idf) without warnings from irlingo's
  files.

Changes the plan:
- Subfolders of an external component are not compiled: only ESPHome's own core
  is loaded with recursive sources. The first build failed with
  `core/config.h: No such file or directory`. The portable core has to sit
  directly in `components/irlingo/` (or in a second component).

Measured:
- irlingo adds 57200 bytes of flash and 584 bytes of static RAM to a firmware
  with only `remote_transmitter` (263547 against 206347 bytes of flash). The
  embedded remote is 12334 of these bytes; the code about 44 KB.
- Embedded remotes (minified, without test vectors): median 2.2 KB, 99% under
  14.4 KB. The largest, a raw RC-5 remote, is 652 KB.
- Heap while parsing, measured on a 64-bit PC with parson's allocation hooks
  (the 32-bit ESP32 needs somewhat less): 6.5 KB for a median remote, 17 KB for
  `hvac_panasonic`, 20 KB at the 99th percentile. Encoding adds a 4 KB pulse
  buffer.

On an ESP32 (ESP32-D0WDQ6 rev 1.0, 4 MB flash, esp-idf, no Wi-Fi configured),
sending `hvac_panasonic` with temperature 21 and mode HEAT every 5 seconds:
- The code is identical to what the Linux encoder (`tests/encode_remote`)
  produces for the same settings: 439 pulses and spaces at 38 kHz, handed to
  `remote_transmitter` without errors.
- Parsing takes 7.8-8.2 ms, encoding 1.5-1.8 ms.
- Parsing uses 16.3 KB of heap (the host estimate was 17.2 KB); the lowest free
  heap during a send is 260 KB of 284 KB.
- No leak: free heap is 281208 bytes after every send from the second on. The
  first send keeps about 2.7 KB once (the transmitter's buffers).

- With a visible red LED and 270 ohm on GPIO4 (pin D4), the LED blinks with
  every transmission (the code sent 4 times, 50 ms apart, every 3 seconds).

Still open:
- A transmission to the air conditioner, with an IR LED on GPIO4.

## Decisions

1. Layout: decided, the portable core is directly in `components/irlingo/`
   (a `core/` subfolder does not work).
2. Scope of the first release: phases 1-4 (the action, buttons and climate), or
   phases 1-2 alone, so the action can be tried early?
3. Targets: ESP32 only at first (recommended). ESP8266 has too little memory to
   parse remotes on the device.

## Risks

- Duty cycle: ESPHome sets it on the transmitter, not per remote. A remote's
  `dutycycle` is then only a documented hint.
- ESPHome API changes: the CI compile job catches breakage, but someone has to
  fix it.
- Clone size: every ESPHome user clones the repository with its 21 MB of
  remotes. Acceptable; the remotes could move out later.
- No hardware in CI: the first transmission from an ESP32 needs a manual test
  with a real board and IR LED.
