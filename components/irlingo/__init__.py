"""irlingo for ESPHome: send IR codes from irlingo remote files.

    irlingo:
      - id: aircon
        remote: panasonic/hvac_panasonic   # a remote in conf/remotes
      - id: tv
        file: remotes/livingroom-tv.json   # or a file next to the YAML

    on_...:
      - irlingo.send:
          remote_id: aircon
          features:
            mode: HEAT
            temperature: "21"

The chosen remotes are built into the firmware (without their test vectors)
and encoded on the device by irlingo's C code in this directory, the same
code the irlingo command uses on Linux. Feature names and fixed values are
checked when compiling.
"""

import json
from pathlib import Path

from esphome import automation
import esphome.codegen as cg
from esphome.components import remote_transmitter
import esphome.config_validation as cv
from esphome.const import CONF_FILE, CONF_ID
from esphome.core import CORE, Lambda
import esphome.final_validate as fv

DOMAIN = "irlingo"
DEPENDENCIES = ["remote_transmitter"]

CONF_REMOTE = "remote"
CONF_REMOTE_ID = "remote_id"
CONF_TRANSMITTER_ID = "transmitter_id"
CONF_FEATURES = "features"

# Remotes over this size work, but use much of the heap while being parsed
LARGE_REMOTE = 64 * 1024

irlingo_ns = cg.esphome_ns.namespace("irlingo")
IrlingoRemote = irlingo_ns.class_("IrlingoRemote", cg.Component)
SendAction = irlingo_ns.class_("SendAction", automation.Action)


def _remotes_dir():
    """conf/remotes of the repository this component comes from"""
    for parent in Path(__file__).resolve().parents:
        if (parent / "conf" / "remotes").is_dir():
            return parent / "conf" / "remotes"
    raise cv.Invalid("conf/remotes not found next to the irlingo component")


def _find_remote(name):
    """A remote by name, e.g. "sony/RM-ED011", or by its file name alone,
    e.g. "RM-ED011", when that is unique (as the irlingo command does)"""
    remotes = _remotes_dir()
    path = remotes / f"{name}.json"
    if path.is_file():
        return path
    if "/" not in name:
        matches = sorted(remotes.rglob(f"{name}.json"))
        if len(matches) == 1:
            return matches[0]
        if matches:
            names = ", ".join(str(m.relative_to(remotes).with_suffix("")) for m in matches)
            raise cv.Invalid(f"Remote '{name}' is ambiguous, use one of: {names}")
    raise cv.Invalid(f"Unknown remote '{name}' (remotes are in {remotes})")


def load_remote(config):
    """The remote of a configuration entry, without its test vectors"""
    if CONF_FILE in config:
        path = Path(CORE.relative_config_path(config[CONF_FILE]))
    else:
        path = _find_remote(config[CONF_REMOTE])
    try:
        with open(path, encoding="utf-8") as f:
            remote = json.load(f)
    except (OSError, ValueError) as e:
        raise cv.Invalid(f"Cannot read remote {path}: {e}") from e
    if not isinstance(remote, dict) or "template" not in remote or "symboldefinitions" not in remote:
        raise cv.Invalid(f"{path} is not an irlingo remote (no template or symbol definitions)")
    remote.pop("tests", None)
    return remote


def _validate_remote(config):
    load_remote(config)
    return config


REMOTE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(IrlingoRemote),
            cv.GenerateID(CONF_TRANSMITTER_ID): cv.use_id(
                remote_transmitter.RemoteTransmitterComponent
            ),
            cv.Exclusive(CONF_REMOTE, "source"): cv.string_strict,
            cv.Exclusive(CONF_FILE, "source"): cv.file_,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_exactly_one_key(CONF_REMOTE, CONF_FILE),
    _validate_remote,
)

CONFIG_SCHEMA = cv.ensure_list(REMOTE_SCHEMA)


def _check_features(remote_name, remote, features):
    known = remote.get("features", {})
    for name, value in features.items():
        if name not in known:
            raise cv.Invalid(
                f"Remote {remote_name} has no feature '{name}' (it has: {', '.join(known)})"
            )
        if isinstance(value, Lambda):
            continue  # checked on the device when sending
        if value not in known[name]:
            raise cv.Invalid(
                f"Remote {remote_name} has no feature {name}={value} "
                f"(values: {', '.join(known[name])})"
            )


def _final_validate(config):
    """Check the features of every irlingo.send action in the configuration"""
    remotes = {str(conf[CONF_ID]): conf for conf in config}

    def walk(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if key == "irlingo.send" and isinstance(value, dict):
                    conf = remotes.get(str(value[CONF_REMOTE_ID]))
                    if conf is not None:
                        name = conf.get(CONF_REMOTE) or str(conf.get(CONF_FILE))
                        _check_features(name, load_remote(conf), value.get(CONF_FEATURES, {}))
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    walk(fv.full_config.get())
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        transmitter = await cg.get_variable(conf[CONF_TRANSMITTER_ID])
        cg.add(var.set_transmitter(transmitter))
        text = json.dumps(load_remote(conf), separators=(",", ":"))
        if len(text) > LARGE_REMOTE:
            cv.LOGGER.warning(
                "Remote %s is %d bytes; parsing it on the device needs about 1.5 times that "
                "in free memory",
                conf[CONF_ID],
                len(text),
            )
        cg.add(var.set_remote(text))
        cg.add(var.set_name(conf.get(CONF_REMOTE) or Path(str(conf[CONF_FILE])).name))


SEND_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_REMOTE_ID): cv.use_id(IrlingoRemote),
        cv.Required(CONF_FEATURES): cv.Schema({cv.string_strict: cv.templatable(cv.string)}),
    }
)


@automation.register_action("irlingo.send", SendAction, SEND_SCHEMA, synchronous=True)
async def send_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_REMOTE_ID])
    for name, value in config[CONF_FEATURES].items():
        template = await cg.templatable(value, args, cg.std_string)
        cg.add(var.add_feature(name, template))
    return var
