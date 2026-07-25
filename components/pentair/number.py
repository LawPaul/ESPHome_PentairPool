import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_STEP,
    CONF_TYPE,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
)
from. import (
    PentairChlorinator,
    PentairHeater,
    PentairIntelliChem,
    PentairPump,
    pentair_ns,
)

DEPENDENCIES = ["pentair"]

CONF_PUMP_ID = "pump_id"
CONF_CHLORINATOR_ID = "chlorinator_id"
CONF_INTELLICHEM_ID = "intellichem_id"
CONF_HEATER_ID = "heater_id"

PentairPumpNumber = pentair_ns.class_("PentairPumpNumber", number.Number)
PentairChlorNumber = pentair_ns.class_("PentairChlorNumber", number.Number)
PentairChemNumber = pentair_ns.class_("PentairChemNumber", number.Number)
ChemNumberField = pentair_ns.enum("ChemNumberField", is_class=True)
PentairHeaterNumber = pentair_ns.class_("PentairHeaterNumber", number.Number)
HeaterNumberField = pentair_ns.enum("HeaterNumberField", is_class=True)

TYPE_SETPOINT = "setpoint"
TYPE_OUTPUT = "output"

# IntelliChem 0x92 config-write fields (firmware binder FUN_0096f3bc). Each maps
# to a PentairChemNumber field enum plus sensible default UI bounds and unit.
# These are configuration set-points, so they default to entity_category config.
_CHEM_TYPES = {
    "ph_setpoint": (ChemNumberField.PH_SETPOINT, 7.0, 7.6, 0.1, "pH"),
    "orp_setpoint": (ChemNumberField.ORP_SETPOINT, 400, 800, 10, "mV"),
    "calcium_hardness": (ChemNumberField.CALCIUM_HARDNESS, 0, 800, 25, "ppm"),
    "cyanuric_acid": (ChemNumberField.CYANURIC_ACID, 0, 210, 1, "ppm"),
    "total_alkalinity": (ChemNumberField.TOTAL_ALKALINITY, 0, 800, 25, "ppm"),
}


def _chem_schema(default_min, default_max, default_step, unit):
    return number.number_schema(
        PentairChemNumber,
        unit_of_measurement=unit,
        entity_category=ENTITY_CATEGORY_CONFIG,
    ).extend(
        {
            cv.Required(CONF_INTELLICHEM_ID): cv.use_id(PentairIntelliChem),
            cv.Optional(CONF_MIN_VALUE, default=default_min): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=default_max): cv.float_,
            cv.Optional(CONF_STEP, default=default_step): cv.positive_float,
        }
    )


# Hybrid heat-pump command fields (FUN_0097b558). hybrid_param is the
# payload[3]/0xf38c byte (firmware clamps [5,60], default 15); hybrid_setpoint
# is the target water temp (payload[2]); hybrid_boost is boostTemp (payload[4]).
# Tuple: (field, min, max, step, unit, device_class-or-None).
_HEATER_TYPES = {
    "hybrid_setpoint": (HeaterNumberField.HYBRID_SET_POINT, 40, 104, 1, "°F", DEVICE_CLASS_TEMPERATURE),
    "hybrid_boost": (HeaterNumberField.HYBRID_BOOST, 0, 40, 1, "°F", DEVICE_CLASS_TEMPERATURE),
    "hybrid_param": (HeaterNumberField.HYBRID_PARAM, 5, 60, 1, None, None),
}


def _heater_schema(default_min, default_max, default_step, unit, device_class):
    kwargs = {"entity_category": ENTITY_CATEGORY_CONFIG}
    if unit is not None:
        kwargs["unit_of_measurement"] = unit
    if device_class is not None:
        kwargs["device_class"] = device_class
    return number.number_schema(PentairHeaterNumber, **kwargs).extend(
        {
            cv.Required(CONF_HEATER_ID): cv.use_id(PentairHeater),
            cv.Optional(CONF_MIN_VALUE, default=default_min): cv.float_,
            cv.Optional(CONF_MAX_VALUE, default=default_max): cv.float_,
            cv.Optional(CONF_STEP, default=default_step): cv.positive_float,
        }
    )

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_SETPOINT: number.number_schema(PentairPumpNumber).extend(
            {
                cv.Required(CONF_PUMP_ID): cv.use_id(PentairPump),
                cv.Optional(CONF_MIN_VALUE, default=450): cv.float_,
                cv.Optional(CONF_MAX_VALUE, default=3450): cv.float_,
                cv.Optional(CONF_STEP, default=10): cv.positive_float,
            }
        ),
        TYPE_OUTPUT: number.number_schema(
            PentairChlorNumber,
            unit_of_measurement="%",
            icon="mdi:water-percent",
        ).extend(
            {
                cv.Required(CONF_CHLORINATOR_ID): cv.use_id(PentairChlorinator),
                cv.Optional(CONF_MIN_VALUE, default=0): cv.float_,
                cv.Optional(CONF_MAX_VALUE, default=100): cv.float_,
                cv.Optional(CONF_STEP, default=1): cv.positive_float,
            }
        ),
        **{
            name: _chem_schema(dmin, dmax, dstep, unit)
            for name, (_field, dmin, dmax, dstep, unit) in _CHEM_TYPES.items()
        },
        **{
            name: _heater_schema(dmin, dmax, dstep, unit, dc)
            for name, (_field, dmin, dmax, dstep, unit, dc) in _HEATER_TYPES.items()
        },
    },
    lower=True,
)


async def to_code(config):
    var = await number.new_number(
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    t = config[CONF_TYPE]
    if t == TYPE_SETPOINT:
        parent = await cg.get_variable(config[CONF_PUMP_ID])
        cg.add(var.set_pump(parent))
    elif t == TYPE_OUTPUT:
        parent = await cg.get_variable(config[CONF_CHLORINATOR_ID])
        cg.add(var.set_chlorinator(parent))
    elif t in _HEATER_TYPES:
        parent = await cg.get_variable(config[CONF_HEATER_ID])
        cg.add(var.set_heater(parent))
        cg.add(var.set_field(_HEATER_TYPES[t][0]))
    else:
        parent = await cg.get_variable(config[CONF_INTELLICHEM_ID])
        cg.add(var.set_intellichem(parent))
        cg.add(var.set_field(_CHEM_TYPES[t][0]))
