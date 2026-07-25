import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor
from esphome.const import CONF_ID, CONF_SENSOR
from . import PentairBody, PentairFeatureCircuit

DEPENDENCIES = ["pentair"]

CONF_HEAT_CIRCUIT = "heat_circuit"
CONF_BODY_CIRCUIT = "body_circuit"
CONF_HYSTERESIS = "hysteresis"
CONF_VISUAL_MIN_TEMPERATURE = "visual_min_temperature"
CONF_VISUAL_MAX_TEMPERATURE = "visual_max_temperature"

CONFIG_SCHEMA = (
    climate.climate_schema(PentairBody)
    .extend(cv.COMPONENT_SCHEMA)
    .extend(
        {
            # Body water temperature. The OCP reads this from a local analog
            # sensor (ADC), not from RS-485, so it comes from an ESPHome sensor.
            cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
            # The heat-source ("fireman") relay circuit this thermostat drives.
            cv.Required(CONF_HEAT_CIRCUIT): cv.use_id(PentairFeatureCircuit),
            # Optional interlock: heating engages only while this circuit is on.
            cv.Optional(CONF_BODY_CIRCUIT): cv.use_id(PentairFeatureCircuit),
            cv.Optional(CONF_HYSTERESIS, default=0.5): cv.positive_float,
            cv.Optional(
                CONF_VISUAL_MIN_TEMPERATURE, default=18.0
            ): cv.temperature,
            cv.Optional(
                CONF_VISUAL_MAX_TEMPERATURE, default=40.0
            ): cv.temperature,
        }
    )
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    temp = await cg.get_variable(config[CONF_SENSOR])
    cg.add(var.set_temperature_sensor(temp))
    heat = await cg.get_variable(config[CONF_HEAT_CIRCUIT])
    cg.add(var.set_heat_circuit(heat))
    if CONF_BODY_CIRCUIT in config:
        body = await cg.get_variable(config[CONF_BODY_CIRCUIT])
        cg.add(var.set_body_circuit(body))
    cg.add(var.set_hysteresis(config[CONF_HYSTERESIS]))
    cg.add(var.set_visual_min(config[CONF_VISUAL_MIN_TEMPERATURE]))
    cg.add(var.set_visual_max(config[CONF_VISUAL_MAX_TEMPERATURE]))
