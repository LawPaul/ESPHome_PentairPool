import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import sensor, switch
from esphome.const import CONF_TYPE
from . import PentairFeatureCircuit, PentairPump, pentair_ns

DEPENDENCIES = ["pentair"]

CONF_PUMP_ID = "pump_id"
CONF_PIN = "pin"
CONF_EGG_TIMER = "egg_timer"
CONF_FREEZE_SENSOR = "freeze_sensor"
CONF_FREEZE_THRESHOLD = "freeze_threshold"

PentairPumpSwitch = pentair_ns.class_("PentairPumpSwitch", switch.Switch)

# type: pump -> a pump run/stop switch (RS-485 command to the pump).
PUMP_SCHEMA = switch.switch_schema(PentairPumpSwitch).extend(
    {
        cv.Required(CONF_PUMP_ID): cv.use_id(PentairPump),
    }
)


def _validate_circuit(config):
    if (CONF_FREEZE_SENSOR in config) != (CONF_FREEZE_THRESHOLD in config):
        raise cv.Invalid(
            "'freeze_sensor' and 'freeze_threshold' must be set together"
        )
    return config


# type: circuit -> a local relay feature circuit (no bus traffic; mirrors the
# OCP's local I2C-driven AUX/feature relays).
CIRCUIT_SCHEMA = (
    switch.switch_schema(PentairFeatureCircuit)
    .extend(cv.COMPONENT_SCHEMA)
    .extend(
        {
            cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_EGG_TIMER): cv.positive_time_period_seconds,
            cv.Optional(CONF_FREEZE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_FREEZE_THRESHOLD): cv.temperature,
        }
    )
    .add_extra(_validate_circuit)
)

# Default type is "pump" so existing pump-switch configs (no 'type:' key) keep
# working unchanged.
CONFIG_SCHEMA = cv.typed_schema(
    {
        "pump": PUMP_SCHEMA,
        "circuit": CIRCUIT_SCHEMA,
    },
    default_type="pump",
)


async def to_code(config):
    var = await switch.new_switch(config)
    if config[CONF_TYPE] == "pump":
        parent = await cg.get_variable(config[CONF_PUMP_ID])
        cg.add(var.set_pump(parent))
        return

    # circuit
    await cg.register_component(var, config)
    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    if CONF_EGG_TIMER in config:
        cg.add(var.set_egg_timer(int(config[CONF_EGG_TIMER].total_seconds)))
    if CONF_FREEZE_SENSOR in config:
        freeze = await cg.get_variable(config[CONF_FREEZE_SENSOR])
        cg.add(var.set_freeze_sensor(freeze))
        cg.add(var.set_freeze_threshold(config[CONF_FREEZE_THRESHOLD]))
