import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import climate, switch, uart
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_MODE

CODEOWNERS = ["@LawPaul"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = [
    "sensor",
    "binary_sensor",
    "text_sensor",
    "number",
    "switch",
    "climate",
    "select",
]
MULTI_CONF = True

pentair_ns = cg.esphome_ns.namespace("pentair")
PentairHub = pentair_ns.class_("PentairHub", cg.Component, uart.UARTDevice)
PentairPump = pentair_ns.class_("PentairPump", cg.Component)
PentairChlorinator = pentair_ns.class_("PentairChlorinator", cg.Component)
PentairHeater = pentair_ns.class_("PentairHeater", cg.Component)
PentairIntelliChem = pentair_ns.class_("PentairIntelliChem", cg.Component)

# Local relay / thermostat layer (not on the RS-485 bus): the OCP drives its
# feature-circuit relays via local I2C hardware and heats by energising a
# heat-source relay, so these are modelled as local ESP outputs.
PentairFeatureCircuit = pentair_ns.class_(
    "PentairFeatureCircuit", switch.Switch, cg.Component
)
PentairBody = pentair_ns.class_("PentairBody", climate.Climate, cg.Component)

PumpType = pentair_ns.enum("PumpType")
PUMP_TYPES = {
    "VS": PumpType.PUMP_TYPE_VS,
    "VSF": PumpType.PUMP_TYPE_VSF,
    "VF": PumpType.PUMP_TYPE_VF,
}

PumpMode = pentair_ns.enum("PumpMode")
PUMP_MODES = {
    "speed": PumpMode.PUMP_MODE_SPEED,
    "flow": PumpMode.PUMP_MODE_FLOW,
}

HeaterType = pentair_ns.enum("HeaterType")
HEATER_TYPES = {
    "ultratemp": HeaterType.HEATER_TYPE_ULTRA,
    "hybrid": HeaterType.HEATER_TYPE_HYBRID,
    "mastertemp": HeaterType.HEATER_TYPE_MASTERTEMP,
    "max_e_therm": HeaterType.HEATER_TYPE_MAXETHERM,
    "eti250": HeaterType.HEATER_TYPE_ETI250,
}

HeatPumpMode = pentair_ns.enum("HeatPumpMode")
HEAT_PUMP_MODES = {
    "off": HeatPumpMode.HEAT_PUMP_MODE_OFF,
    "heating": HeatPumpMode.HEAT_PUMP_MODE_HEATING,
    "cooling": HeatPumpMode.HEAT_PUMP_MODE_COOLING,
}

CONF_FLOW_CONTROL_PIN = "flow_control_pin"
CONF_SOURCE_ADDRESS = "source_address"
CONF_POLL_INTERVAL = "poll_interval"
CONF_ACTIVE_POLL_INTERVAL = "active_poll_interval"
CONF_TX_GAP = "tx_gap"
CONF_REQUIRE_PUMP_FLOW = "require_pump_flow"
CONF_PUMPS = "pumps"
CONF_CHLORINATORS = "chlorinators"
CONF_HEATERS = "heaters"
CONF_INTELLICHEMS = "intellichems"
CONF_PUMP_TYPE = "pump_type"
CONF_HEATER_TYPE = "heater_type"
CONF_MIN_FLOW = "min_flow"


def _validate_pump(config):
    if config[CONF_PUMP_TYPE] != "VSF" and CONF_MODE in config:
        raise cv.Invalid("'mode' is only valid for VSF pumps")
    if CONF_MIN_FLOW in config and config[CONF_PUMP_TYPE] not in ("VSF", "VF"):
        raise cv.Invalid("'min_flow' is only valid for flow-capable pumps (VSF, VF)")
    return config


PUMP_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(PentairPump),
        cv.Optional(CONF_ADDRESS, default=0x60): cv.hex_uint8_t,
        cv.Required(CONF_PUMP_TYPE): cv.enum(PUMP_TYPES, upper=True),
        cv.Optional(CONF_MODE): cv.enum(PUMP_MODES, lower=True),
        cv.Optional(CONF_MIN_FLOW): cv.int_range(min=1, max=140),
    }
).add_extra(_validate_pump)

CHLORINATOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(PentairChlorinator),
        cv.Optional(CONF_ADDRESS, default=0x50): cv.hex_uint8_t,
    }
)

HEATER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(PentairHeater),
        cv.Required(CONF_HEATER_TYPE): cv.enum(HEATER_TYPES, lower=True),
        cv.Optional(CONF_ADDRESS, default=0x70): cv.hex_uint8_t,
    }
)

INTELLICHEM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(PentairIntelliChem),
        cv.Required(CONF_ADDRESS): cv.hex_uint8_t,
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PentairHub),
            cv.Optional(CONF_SOURCE_ADDRESS, default=0x10): cv.hex_uint8_t,
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional(
                CONF_POLL_INTERVAL, default="16s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_ACTIVE_POLL_INTERVAL, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_TX_GAP, default="60ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_REQUIRE_PUMP_FLOW, default=True): cv.boolean,
            cv.Optional(CONF_PUMPS): cv.ensure_list(PUMP_SCHEMA),
            cv.Optional(CONF_CHLORINATORS): cv.ensure_list(CHLORINATOR_SCHEMA),
            cv.Optional(CONF_HEATERS): cv.ensure_list(HEATER_SCHEMA),
            cv.Optional(CONF_INTELLICHEMS): cv.ensure_list(INTELLICHEM_SCHEMA),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_source_address(config[CONF_SOURCE_ADDRESS]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_active_poll_interval(config[CONF_ACTIVE_POLL_INTERVAL]))
    cg.add(var.set_tx_gap(config[CONF_TX_GAP]))
    cg.add(var.set_require_pump_flow(config[CONF_REQUIRE_PUMP_FLOW]))

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))

    for pump_conf in config.get(CONF_PUMPS, []):
        pump = cg.new_Pvariable(pump_conf[CONF_ID])
        await cg.register_component(pump, pump_conf)
        cg.add(pump.set_address(pump_conf[CONF_ADDRESS]))
        cg.add(pump.set_pump_type(pump_conf[CONF_PUMP_TYPE]))
        cg.add(pump.set_mode_initial(pump_conf.get(CONF_MODE, PUMP_MODES["speed"])))
        if CONF_MIN_FLOW in pump_conf:
            cg.add(pump.set_min_flow(pump_conf[CONF_MIN_FLOW]))
        cg.add(var.register_pump(pump))

    for chlor_conf in config.get(CONF_CHLORINATORS, []):
        chlor = cg.new_Pvariable(chlor_conf[CONF_ID])
        await cg.register_component(chlor, chlor_conf)
        cg.add(chlor.set_address(chlor_conf[CONF_ADDRESS]))
        cg.add(var.register_chlorinator(chlor))

    for heater_conf in config.get(CONF_HEATERS, []):
        heater = cg.new_Pvariable(heater_conf[CONF_ID])
        await cg.register_component(heater, heater_conf)
        cg.add(heater.set_address(heater_conf[CONF_ADDRESS]))
        cg.add(heater.set_heater_type(heater_conf[CONF_HEATER_TYPE]))
        cg.add(var.register_heater(heater))

    for chem_conf in config.get(CONF_INTELLICHEMS, []):
        chem = cg.new_Pvariable(chem_conf[CONF_ID])
        await cg.register_component(chem, chem_conf)
        cg.add(chem.set_address(chem_conf[CONF_ADDRESS]))
        cg.add(var.register_intellichem(chem))
