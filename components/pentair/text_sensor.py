import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    CONF_ENTITY_CATEGORY,
    CONF_TYPE,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from. import (
    PentairHeater,
    PentairIntelliChem,
)

DEPENDENCIES = ["pentair"]

CONF_HEATER_ID = "heater_id"
CONF_INTELLICHEM_ID = "intellichem_id"

_IDS = (CONF_HEATER_ID, CONF_INTELLICHEM_ID)

# text type -> (allowed owner *_id keys, setter method on that component).
#   heater:      "status" (mode/status summary), "fault_reason" (named faults)
#   intellichem: "reply" (raw 0x12 hex), "alarms"/"warnings" (named),
#                "ph_dosing_status"/"orp_dosing_status" (doser run state)
_TYPE_MAP = {
    "status": ((CONF_HEATER_ID,), "set_status_text_sensor"),
    "fault_reason": ((CONF_HEATER_ID,), "set_fault_text_sensor"),
    "reply": ((CONF_INTELLICHEM_ID,), "set_reply_text_sensor"),
    "alarms": ((CONF_INTELLICHEM_ID,), "set_alarms_text_sensor"),
    "warnings": ((CONF_INTELLICHEM_ID,), "set_warnings_text_sensor"),
    "ph_dosing_status": ((CONF_INTELLICHEM_ID,), "set_ph_dosing_text_sensor"),
    "orp_dosing_status": ((CONF_INTELLICHEM_ID,), "set_orp_dosing_text_sensor"),
}


def _validate(config):
    present = [k for k in _IDS if k in config]
    if len(present) != 1:
        raise cv.Invalid(
            "exactly one of 'heater_id' or 'intellichem_id' is required"
        )
    dev = present[0]
    t = config.get(CONF_TYPE)
    if t is None:
        # Back-compat defaults: IntelliChem -> raw reply, heater -> status.
        t = "reply" if dev == CONF_INTELLICHEM_ID else "status"
        config[CONF_TYPE] = t
    allowed, _ = _TYPE_MAP[t]
    if dev not in allowed:
        allowed_names = " or ".join(f"'{a}'" for a in allowed)
        raise cv.Invalid(f"'type: {t}' requires {allowed_names}")
    # Raw/forensic readouts default to the diagnostic category so they stay off
    # the primary dashboard; the raw IntelliChem reply hex (unmapped bytes, no
    # firmware-proven meaning) is additionally disabled by default.
    if t in ("reply", "fault_reason"):
        config.setdefault(CONF_ENTITY_CATEGORY, ENTITY_CATEGORY_DIAGNOSTIC)
    if t == "reply":
        config.setdefault(CONF_DISABLED_BY_DEFAULT, True)
    return config


CONFIG_SCHEMA = cv.All(
    text_sensor.text_sensor_schema().extend(
        {
            cv.Optional(CONF_TYPE): cv.one_of(*_TYPE_MAP, lower=True),
            cv.Optional(CONF_HEATER_ID): cv.use_id(PentairHeater),
            cv.Optional(CONF_INTELLICHEM_ID): cv.use_id(PentairIntelliChem),
        }
    ),
    _validate,
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    _, setter = _TYPE_MAP[config[CONF_TYPE]]
    owner_id = next(config[k] for k in _IDS if k in config)
    parent = await cg.get_variable(owner_id)
    cg.add(getattr(parent, setter)(var))
