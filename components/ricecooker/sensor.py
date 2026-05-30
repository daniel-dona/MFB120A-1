"""ESPHome sensor platform for the rice cooker temperature sensors."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    UNIT_CELSIUS,
    ICON_THERMOMETER,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
)
from . import RiceCooker

DEPENDENCIES = ["ricecooker"]

CONF_RICECOOKER_ID = "ricecooker_id"
CONF_TOP_TEMPERATURE = "top_temperature"
CONF_BOTTOM_TEMPERATURE = "bottom_temperature"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_RICECOOKER_ID): cv.use_id(RiceCooker),
        cv.Optional(CONF_TOP_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_BOTTOM_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])

    if top_config := config.get(CONF_TOP_TEMPERATURE):
        sens = await sensor.new_sensor(top_config)
        cg.add(paren.set_sensor_temp_top(sens))

    if bottom_config := config.get(CONF_BOTTOM_TEMPERATURE):
        sens = await sensor.new_sensor(bottom_config)
        cg.add(paren.set_sensor_temp_bottom(sens))