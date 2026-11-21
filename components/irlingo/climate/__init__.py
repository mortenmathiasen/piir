"""irlingo climate: an air conditioner remote as a Home Assistant thermostat.

    climate:
      - platform: irlingo
        remote_id: aircon
        name: Living room air conditioner
        # All optional, guessed from the remote's features when left out:
        modes:                     # Home Assistant mode: features to send
          heat: {mode: HEAT}
          cool: {mode: COOL}
          "off": {mode: "OFF"}
        temperature: temperature   # the feature for the target temperature
        fan: fan                   # the feature for the fan speed
        fixed: {option: AUTO}      # features sent with every code
        sensor: room_temperature   # a sensor for the current temperature

Every change sends one code with the mode's features, the target temperature
(the remote's nearest value) and the fan speed. The remote's fan value AUTO
becomes Home Assistant's "auto"; its other values are offered by name.
"""

import esphome.codegen as cg
from esphome.components import climate, sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_SENSOR
from esphome.core import CORE
import esphome.final_validate as fv

from .. import (
    CONF_REMOTE_ID,
    IrlingoRemote,
    check_features,
    find_remote_config,
    irlingo_ns,
    load_remote,
    remote_name,
)

DEPENDENCIES = ["irlingo"]

CONF_MODES = "modes"
CONF_TEMPERATURE = "temperature"
CONF_FAN = "fan"
CONF_FIXED = "fixed"

IrlingoClimate = irlingo_ns.class_(
    "IrlingoClimate", climate.Climate, cg.Component, cg.Parented.template(IrlingoRemote)
)

MODE_NAMES = ["off", "heat_cool", "cool", "heat", "dry", "fan_only", "auto"]

# Remote mode values and the Home Assistant mode they become
MODE_GUESSES = {
    "AUTO": "heat_cool",
    "HEAT": "heat",
    "COOL": "cool",
    "DRY": "dry",
    "FAN": "fan_only",
    "FAN_ONLY": "fan_only",
    "OFF": "off",
}


def _mode_name(value):
    # YAML reads an unquoted off as false (and ESPHome makes such keys text)
    if value is False or (isinstance(value, str) and value.lower() == "false"):
        return "off"
    return cv.one_of(*MODE_NAMES, lower=True)(cv.string_strict(value))


def _feature_value(value):
    # YAML reads unquoted ON and OFF as booleans
    if isinstance(value, bool):
        return "ON" if value else "OFF"
    return cv.string(value)


def _features(value):
    if not isinstance(value, dict):
        raise cv.Invalid("Expected features, e.g. {mode: HEAT}")
    return {cv.string_strict(name): _feature_value(v) for name, v in value.items()}


def _modes(value):
    if not isinstance(value, dict):
        raise cv.Invalid("Expected Home Assistant modes with their features, e.g. heat: {mode: HEAT}")
    return {_mode_name(mode): _features(features) for mode, features in value.items()}


CONFIG_SCHEMA = climate.climate_schema(IrlingoClimate).extend(
    {
        cv.GenerateID(CONF_REMOTE_ID): cv.use_id(IrlingoRemote),
        cv.Optional(CONF_MODES): _modes,
        cv.Optional(CONF_TEMPERATURE): cv.string_strict,
        cv.Optional(CONF_FAN): cv.string_strict,
        cv.Optional(CONF_FIXED, default={}): _features,
        cv.Optional(CONF_SENSOR): cv.use_id(sensor.Sensor),
    }
).extend(cv.COMPONENT_SCHEMA)


def _number(value):
    try:
        return float(value)
    except ValueError:
        return None


def climate_mapping(remote_conf, config):
    """The modes, temperature and fan feature of a climate entity: as given,
    or guessed from the remote's feature names (mode, power, temperature, fan)"""
    name = remote_name(remote_conf)
    features = load_remote(remote_conf).get("features", {})

    if CONF_MODES in config:
        modes = {mode: dict(values) for mode, values in config[CONF_MODES].items()}
    else:
        modes = {}
        for value in features.get("mode", {}):
            mode = MODE_GUESSES.get(value.upper())
            if mode and mode not in modes:
                modes[mode] = {"mode": value}
        power = features.get("power", {})
        if "ON" in power and "OFF" in power:
            for values in modes.values():
                values["power"] = "ON"
            modes["off"] = {"power": "OFF"}
        if not modes:
            raise cv.Invalid(
                f"Cannot guess the modes of remote {name} (no 'mode' feature with values like "
                f"HEAT or COOL); give them with 'modes:'"
            )

    temperature = config.get(CONF_TEMPERATURE)
    if temperature is None and "temperature" in features:
        temperature = "temperature"
    temperatures = []
    if temperature is not None:
        if temperature not in features:
            raise cv.Invalid(f"Remote {name} has no feature '{temperature}'")
        temperatures = sorted(
            (_number(v), v) for v in features[temperature] if _number(v) is not None
        )
        if not temperatures:
            raise cv.Invalid(f"Feature '{temperature}' of remote {name} has no numeric values")

    fan = config.get(CONF_FAN)
    if fan is None and "fan" in features:
        fan = "fan"
    fan_values = []
    if fan is not None:
        if fan not in features:
            raise cv.Invalid(f"Remote {name} has no feature '{fan}'")
        fan_values = list(features[fan])

    for values in list(modes.values()) + [config.get(CONF_FIXED, {})]:
        check_features(remote_conf, values)
    return modes, temperature, temperatures, fan, fan_values


def _final_validate(config):
    remote_conf = find_remote_config(fv.full_config.get(), config[CONF_REMOTE_ID])
    climate_mapping(remote_conf, config)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await cg.register_parented(var, config[CONF_REMOTE_ID])

    remote_conf = find_remote_config(CORE.config, config[CONF_REMOTE_ID])
    modes, temperature, temperatures, fan, fan_values = climate_mapping(remote_conf, config)
    for mode, values in modes.items():
        for name, value in values.items():
            cg.add(var.add_mode_feature(climate.CLIMATE_MODES[mode.upper()], name, value))
    if temperature is not None:
        cg.add(var.set_temperature_feature(temperature))
        for number, value in temperatures:
            cg.add(var.add_temperature(number, value))
    if fan is not None:
        cg.add(var.set_fan_feature(fan))
        for value in fan_values:
            cg.add(var.add_fan_value(value))
    for name, value in config[CONF_FIXED].items():
        cg.add(var.add_fixed(name, value))
    if CONF_SENSOR in config:
        cg.add(var.set_sensor(await cg.get_variable(config[CONF_SENSOR])))
