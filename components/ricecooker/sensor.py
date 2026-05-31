"""ESPHome sensor platform for rice cooker temperature, voltage, and remaining time."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    UNIT_CELSIUS,
    UNIT_MINUTE,
    UNIT_VOLT,
    ICON_THERMOMETER,
    ICON_TIMER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_DURATION,
    STATE_CLASS_MEASUREMENT,
)
from . import RiceCooker

DEPENDENCIES = ["ricecooker"]

CONF_RICECOOKER_ID = "ricecooker_id"
CONF_TOP_TEMPERATURE = "top_temperature"
CONF_BOTTOM_TEMPERATURE = "bottom_temperature"
CONF_VOLTAGE = "voltage"
CONF_REMAINING_TIME = "remaining_time"

CONFIG_SCHEMA = cv.Schema({
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
    cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        icon=ICON_THERMOMETER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
    cv.Optional(CONF_REMAINING_TIME): sensor.sensor_schema(
        unit_of_measurement=UNIT_MINUTE,
        icon=ICON_TIMER,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_DURATION,
        state_class=STATE_CLASS_MEASUREMENT,
    ),
})


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])

    if top_config := config.get(CONF_TOP_TEMPERATURE):
        sens = await sensor.new_sensor(top_config)
        cg.add(paren.set_sensor_temp_top(sens))

    if bottom_config := config.get(CONF_BOTTOM_TEMPERATURE):
        sens = await sensor.new_sensor(bottom_config)
        cg.add(paren.set_sensor_temp_bottom(sens))

    if voltage_config := config.get(CONF_VOLTAGE):
        sens = await sensor.new_sensor(voltage_config)
        cg.add(paren.set_sensor_voltage(sens))

    if remaining_config := config.get(CONF_REMAINING_TIME):
        sens = await sensor.new_sensor(remaining_config)
        cg.add(paren.set_sensor_remaining(sens))