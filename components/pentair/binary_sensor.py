import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_ENTITY_CATEGORY,
    CONF_TYPE,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from. import (
    PentairChlorinator,
    PentairHeater,
    PentairIntelliChem,
    PentairPump,
)

DEPENDENCIES = ["pentair"]

CONF_PUMP_ID = "pump_id"
CONF_CHLORINATOR_ID = "chlorinator_id"
CONF_HEATER_ID = "heater_id"
CONF_INTELLICHEM_ID = "intellichem_id"

# Chlorinator-only alarm/status types.
_CHLOR_MAP = {
    "flow": "set_flow_binary_sensor",
    "no_flow": "set_no_flow_binary_sensor",
    "low_salt": "set_low_salt_binary_sensor",
    "very_low_salt": "set_very_low_salt_binary_sensor",
    "clean_cell": "set_clean_cell_binary_sensor",
    "cold_water": "set_cold_water_binary_sensor",
}
# Heater-only status types.
_HEATER_MAP = {
    "heater_fault": "set_fault_binary_sensor",
}
# Pump-only status types (decoded from the 0x07 reply).
_PUMP_MAP = {
    "priming": "set_priming_binary_sensor",
    "pump_alarm": "set_alarm_binary_sensor",
    # Per-bit fault breakout of the alarm word (names verbatim from
    # IntelliFloVSF_logStatusPacket). The aggregate "pump_alarm" above is the
    # single interlock signal; these are the diagnostic detail.
    "pump_high_temp": "set_high_temp_binary_sensor",
    "pump_prime_error": "set_prime_error_binary_sensor",
    "pump_over_temp": "set_over_temp_binary_sensor",
    "pump_power_error": "set_power_error_binary_sensor",
    "pump_over_current": "set_over_current_binary_sensor",
    "pump_over_voltage": "set_over_voltage_binary_sensor",
    "pump_unknown_alarm": "set_unknown_alarm_binary_sensor",
}
# "comm" (device present / responding on the bus) applies to every device kind.
_TYPES = ["comm", *_CHLOR_MAP, *_HEATER_MAP, *_PUMP_MAP]

_COMM_IDS = (CONF_PUMP_ID, CONF_CHLORINATOR_ID, CONF_HEATER_ID, CONF_INTELLICHEM_ID)

# Per-type device_class / entity_category defaults (user-overridable). "comm" is
# connectivity (on = present) and diagnostic; every alarm is "problem" (HA shows
# it as an alert, on = fault); flow/priming are "running" states.
_META = {
    "comm": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_CONNECTIVITY,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
    "flow": {CONF_DEVICE_CLASS: DEVICE_CLASS_RUNNING},
    "no_flow": {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM},
    "low_salt": {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM},
    "very_low_salt": {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM},
    "clean_cell": {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM},
    "cold_water": {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM},
    "heater_fault": {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM},
    "priming": {CONF_DEVICE_CLASS: DEVICE_CLASS_RUNNING},
    "pump_alarm": {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM},
    "pump_high_temp": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
    "pump_prime_error": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
    "pump_over_temp": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
    "pump_power_error": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
    "pump_over_current": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
    "pump_over_voltage": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
    "pump_unknown_alarm": {
        CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM,
        CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
    },
}


def _validate(config):
    t = config[CONF_TYPE]
    if t == "comm":
        present = [k for k in _COMM_IDS if k in config]
        if len(present) != 1:
            raise cv.Invalid(
                "'comm' requires exactly one of "
                "'pump_id', 'chlorinator_id', 'heater_id' or 'intellichem_id'"
            )
    elif t in _CHLOR_MAP:
        if CONF_CHLORINATOR_ID not in config:
            raise cv.Invalid(f"'{t}' requires 'chlorinator_id'")
    elif t in _HEATER_MAP:
        if CONF_HEATER_ID not in config:
            raise cv.Invalid(f"'{t}' requires 'heater_id'")
    elif t in _PUMP_MAP:
        if CONF_PUMP_ID not in config:
            raise cv.Invalid(f"'{t}' requires 'pump_id'")
    for k, v in _META.get(t, {}).items():
        config.setdefault(k, v)
    return config


CONFIG_SCHEMA = cv.All(
    binary_sensor.binary_sensor_schema().extend(
        {
            cv.Required(CONF_TYPE): cv.one_of(*_TYPES, lower=True),
            cv.Optional(CONF_PUMP_ID): cv.use_id(PentairPump),
            cv.Optional(CONF_CHLORINATOR_ID): cv.use_id(PentairChlorinator),
            cv.Optional(CONF_HEATER_ID): cv.use_id(PentairHeater),
            cv.Optional(CONF_INTELLICHEM_ID): cv.use_id(PentairIntelliChem),
        }
    ),
    _validate,
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    t = config[CONF_TYPE]
    if t == "comm":
        owner_id = next(config[k] for k in _COMM_IDS if k in config)
        parent = await cg.get_variable(owner_id)
        cg.add(parent.set_comm_binary_sensor(var))
    elif t in _CHLOR_MAP:
        parent = await cg.get_variable(config[CONF_CHLORINATOR_ID])
        cg.add(getattr(parent, _CHLOR_MAP[t])(var))
    elif t in _PUMP_MAP:
        parent = await cg.get_variable(config[CONF_PUMP_ID])
        cg.add(getattr(parent, _PUMP_MAP[t])(var))
    else:
        parent = await cg.get_variable(config[CONF_HEATER_ID])
        cg.add(getattr(parent, _HEATER_MAP[t])(var))
