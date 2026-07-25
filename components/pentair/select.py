import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_TYPE
from. import PentairHeater, pentair_ns

DEPENDENCIES = ["pentair"]

CONF_HEATER_ID = "heater_id"

PentairHeaterModeSelect = pentair_ns.class_(
    "PentairHeaterModeSelect", select.Select
)
PentairHeaterHeatingModeSelect = pentair_ns.class_(
    "PentairHeaterHeatingModeSelect", select.Select
)

TYPE_MODE = "mode"
TYPE_HEATING_MODE = "heating_mode"

# "mode" options match the firmware HeatPumpMode enum (Off=0, Heating=1,
# Cooling=2); the C++ control() maps option index -> mode. Latches an
# Ultra/Hybrid mode command onto the poll frame (RS-485 command).
HEAT_PUMP_MODE_OPTIONS = ["Off", "Heating", "Cooling"]

# "heating_mode" options are the Hybrid heat-source enum (token 0xa5d6), offset
# by one (index 0 -> 1 "Heat Pump Only"... 3 -> 4 "Dual Mode"); sets payload[1]
# of the Hybrid command. Meaningful only on a Hybrid heater.
HYBRID_HEATING_MODE_OPTIONS = [
    "Heat Pump Only",
    "Gas Heater Only",
    "Hybrid Mode",
    "Dual Mode",
]

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_MODE: select.select_schema(PentairHeaterModeSelect).extend(
            {
                cv.Required(CONF_HEATER_ID): cv.use_id(PentairHeater),
            }
        ),
        TYPE_HEATING_MODE: select.select_schema(
            PentairHeaterHeatingModeSelect
        ).extend(
            {
                cv.Required(CONF_HEATER_ID): cv.use_id(PentairHeater),
            }
        ),
    },
    lower=True,
    default_type=TYPE_MODE,
)


async def to_code(config):
    if config[CONF_TYPE] == TYPE_HEATING_MODE:
        options = HYBRID_HEATING_MODE_OPTIONS
    else:
        options = HEAT_PUMP_MODE_OPTIONS
    var = await select.new_select(config, options=options)
    parent = await cg.get_variable(config[CONF_HEATER_ID])
    cg.add(var.set_heater(parent))
