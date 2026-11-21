"""irlingo button: sends a remote's code when pressed.

    button:
      - platform: irlingo
        remote_id: tv
        name: TV power
        features:
          key: Power
"""

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
import esphome.final_validate as fv

from .. import (
    CONF_FEATURES,
    CONF_REMOTE_ID,
    CONF_REPEAT,
    REPEAT_SCHEMA,
    IrlingoRemote,
    check_features,
    find_remote_config,
    irlingo_ns,
    set_repeat,
)

DEPENDENCIES = ["irlingo"]

IrlingoButton = irlingo_ns.class_("IrlingoButton", button.Button, cg.Parented.template(IrlingoRemote))

CONFIG_SCHEMA = button.button_schema(IrlingoButton).extend(
    {
        cv.GenerateID(CONF_REMOTE_ID): cv.use_id(IrlingoRemote),
        cv.Required(CONF_FEATURES): cv.Schema({cv.string_strict: cv.string}),
        cv.Optional(CONF_REPEAT): REPEAT_SCHEMA,
    }
)


def _final_validate(config):
    check_features(find_remote_config(fv.full_config.get(), config[CONF_REMOTE_ID]), config[CONF_FEATURES])
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_parented(var, config[CONF_REMOTE_ID])
    for name, value in config[CONF_FEATURES].items():
        cg.add(var.add_feature(name, value))
    await set_repeat(var, config)
