import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
    CONF_DEVICE_CLASS,
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_TYPE,
    CONF_UNIT_OF_MEASUREMENT,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
)
from. import PentairChlorinator, PentairHeater, PentairIntelliChem, PentairPump

DEPENDENCIES = ["pentair"]

CONF_PUMP_ID = "pump_id"
CONF_CHLORINATOR_ID = "chlorinator_id"
CONF_HEATER_ID = "heater_id"
CONF_INTELLICHEM_ID = "intellichem_id"

# type -> (owner, setter)
_MAP = {
    "rpm": ("pump", "set_rpm_sensor"),
    "gpm": ("pump", "set_gpm_sensor"),
    "watts": ("pump", "set_watts_sensor"),
    "salt": ("chlor", "set_salt_sensor"),
    "heater_mode": ("heater", "set_mode_sensor"),
    "heater_status": ("heater", "set_status_sensor"),
    "heater_error_a": ("heater", "set_error_a_sensor"),
    "heater_error_b": ("heater", "set_error_b_sensor"),
    "heater_fenwal": ("heater", "set_fenwal_sensor"),
    "ph": ("chem", "set_ph_sensor"),
    "orp": ("chem", "set_orp_sensor"),
    "ph_setpoint": ("chem", "set_ph_setpoint_sensor"),
    "orp_setpoint": ("chem", "set_orp_setpoint_sensor"),
    "ph_tank": ("chem", "set_ph_tank_sensor"),
    "orp_tank": ("chem", "set_orp_tank_sensor"),
    "saturation_index": ("chem", "set_saturation_index_sensor"),
    "calcium_hardness": ("chem", "set_calcium_hardness_sensor"),
    "cyanuric_acid": ("chem", "set_cyanuric_acid_sensor"),
    "total_alkalinity": ("chem", "set_total_alkalinity_sensor"),
}

_ID_FOR_OWNER = {
    "pump": CONF_PUMP_ID,
    "chlor": CONF_CHLORINATOR_ID,
    "heater": CONF_HEATER_ID,
    "chem": CONF_INTELLICHEM_ID,
}

# Per-type entity metadata defaults. Applied only when the user hasn't set the
# key, so any of these can still be overridden in YAML. All are firmware-proven
# quantities; pH / saturation index carry decimals (the previous blanket
# accuracy_decimals=0 truncated pH 7.4 -> 7). Raw heater diagnostics are marked
# diagnostic so they don't clutter the default dashboard.
_META = {
    "rpm": {CONF_UNIT_OF_MEASUREMENT: "rpm", CONF_ICON: "mdi:speedometer"},
    "gpm": {CONF_UNIT_OF_MEASUREMENT: "gal/min", CONF_ICON: "mdi:pump"},
    "watts": {CONF_UNIT_OF_MEASUREMENT: "W", CONF_DEVICE_CLASS: DEVICE_CLASS_POWER},
    "salt": {CONF_UNIT_OF_MEASUREMENT: "ppm", CONF_ICON: "mdi:shaker-outline"},
    "heater_error_a": {CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC},
    "heater_error_b": {CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC},
    "heater_fenwal": {CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC},
    "ph": {CONF_UNIT_OF_MEASUREMENT: "pH", CONF_ACCURACY_DECIMALS: 2, CONF_ICON: "mdi:ph"},
    "orp": {CONF_UNIT_OF_MEASUREMENT: "mV"},
    "ph_setpoint": {
        CONF_UNIT_OF_MEASUREMENT: "pH",
        CONF_ACCURACY_DECIMALS: 2,
        CONF_ICON: "mdi:ph",
    },
    "orp_setpoint": {CONF_UNIT_OF_MEASUREMENT: "mV"},
    "saturation_index": {CONF_ACCURACY_DECIMALS: 2, CONF_ICON: "mdi:water-percent"},
    "calcium_hardness": {CONF_UNIT_OF_MEASUREMENT: "ppm"},
    "cyanuric_acid": {CONF_UNIT_OF_MEASUREMENT: "ppm"},
    "total_alkalinity": {CONF_UNIT_OF_MEASUREMENT: "ppm"},
}


def _validate(config):
    owner = _MAP[config[CONF_TYPE]][0]
    key = _ID_FOR_OWNER[owner]
    if key not in config:
        raise cv.Invalid(f"'{config[CONF_TYPE]}' sensor requires '{key}'")
    # Fill per-type defaults for any metadata the user didn't specify. state_class
    # is handled by the schema (it is a codegen enum); accuracy defaults to 0
    # unless the type overrides it above. These remaining keys are plain
    # strings/ints in codegen, so post-validation injection is safe.
    config.setdefault(CONF_ACCURACY_DECIMALS, 0)
    for k, v in _META.get(config[CONF_TYPE], {}).items():
        config.setdefault(k, v)
    return config


CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(state_class=STATE_CLASS_MEASUREMENT).extend(
        {
            cv.Required(CONF_TYPE): cv.one_of(*_MAP, lower=True),
            cv.Optional(CONF_PUMP_ID): cv.use_id(PentairPump),
            cv.Optional(CONF_CHLORINATOR_ID): cv.use_id(PentairChlorinator),
            cv.Optional(CONF_HEATER_ID): cv.use_id(PentairHeater),
            cv.Optional(CONF_INTELLICHEM_ID): cv.use_id(PentairIntelliChem),
        }
    ),
    _validate,
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    owner, setter = _MAP[config[CONF_TYPE]]
    parent = await cg.get_variable(config[_ID_FOR_OWNER[owner]])
    cg.add(getattr(parent, setter)(var))
