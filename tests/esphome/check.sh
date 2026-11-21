#!/bin/sh
# Checks the irlingo ESPHome component with ESPHome:
# - tests/esphome/full.yaml, which uses every part of it, must be valid
# - variants of it with mistakes must fail with the expected message
# - with --compile, full.yaml and doc/examples/esphome-irlingo.yaml must
#   compile for ESP32
# usage: tests/esphome/check.sh [--compile]   (ESPHome must be installed)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work=$(mktemp -d "$here/.check.XXXXXX")
trap 'rm -rf "$work"' EXIT
failures=0

valid() {
  if ! esphome config "$1" > "$work/out" 2>&1; then
    echo "FAIL $2: expected a valid configuration"
    tail -20 "$work/out"
    failures=$((failures + 1))
  else
    echo "ok   $2"
  fi
}

# invalid NAME MESSAGE SED-EXPRESSION: full.yaml changed by the expression
# must fail with MESSAGE
invalid() {
  sed "$3" "$here/full.yaml" > "$here/case.yaml"
  if esphome config "$here/case.yaml" > "$work/out" 2>&1; then
    echo "FAIL $1: expected an error"
    failures=$((failures + 1))
  elif ! grep -q -- "$2" "$work/out"; then
    echo "FAIL $1: expected \"$2\""
    tail -20 "$work/out"
    failures=$((failures + 1))
  else
    echo "ok   $1"
  fi
  rm -f "$here/case.yaml"
}

valid "$here/full.yaml" "full configuration"
invalid "wrong value" "has no feature mode=TURBO" 's/            mode: HEAT/            mode: TURBO/'
invalid "wrong feature" "has no feature 'speed'" 's/            temperature: "21"/            speed: "21"/'
invalid "unknown remote" "Unknown remote 'panasonic/no_such_remote'" 's#remote: panasonic/hvac_panasonic#remote: panasonic/no_such_remote#'
invalid "ambiguous remote" "is ambiguous" 's#remote: RC4330_01H_Philips_RC433001H_AMP#remote: 301_501_3100_5100_58xx_59xx#'
invalid "missing file" "own-missing.json" 's#file: ../../doc/examples/example-aircon.json#file: own-missing.json#'
invalid "button value" "has no feature key=Pow" 's/      key: Power/      key: Pow/'
invalid "climate mode name" "Unknown value 'warm'" 's/      heat: {mode: HEAT, power: ON}/      warm: {mode: HEAT, power: ON}/'
invalid "climate mode value" "has no feature mode=WARM" 's/      heat: {mode: HEAT, power: ON}/      heat: {mode: WARM, power: ON}/'
invalid "climate without modes" "Cannot guess the modes" '/^  - platform: irlingo$/{n;s/remote_id: aircon/remote_id: amp/}'

if [ "${1:-}" = "--compile" ]; then
  for config in "$here/full.yaml" "$work/example.yaml"; do
    if [ "$config" = "$work/example.yaml" ]; then
      sed "s#path: /home/pi/irlingo/components#path: $root/components#" \
        "$root/doc/examples/esphome-irlingo.yaml" > "$config"
      name="doc/examples/esphome-irlingo.yaml"
    else
      name="full configuration"
    fi
    if esphome compile "$config" > "$work/out" 2>&1; then
      echo "ok   compiling $name"
    else
      echo "FAIL compiling $name"
      grep -E "error|Error" "$work/out" | tail -20
      failures=$((failures + 1))
    fi
  done
fi

if [ "$failures" -gt 0 ]; then
  echo "$failures failures"
  exit 1
fi
echo "all ESPHome checks passed"
