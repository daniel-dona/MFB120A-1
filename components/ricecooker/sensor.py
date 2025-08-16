import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID, UNIT_CELSIUS, ICON_THERMOMETER
from . import RiceCooker, ricecooker_ns

DEPENDENCIES = ["ricecooker"]

CONF_RICECOOKER_ID = "ricecooker_id"

CONF_SENSOR_TEMP_TOP = "top_temperature_sensor"
CONF_SENSOR_TEMP_BOTTOM = "bottom_temperature_sensor"


# RiceCookerSensor = ricecooker_ns.class_(
#     "RiceCookerSensor", cg.PollingComponent
# )

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_RICECOOKER_ID): cv.use_id(RiceCooker),
        
        cv.Optional(CONF_SENSOR_TEMP_TOP): sensor.sensor_schema(
            sensor.Sensor,
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER,
            accuracy_decimals=0,
        ).extend(),
        
        cv.Optional(CONF_SENSOR_TEMP_BOTTOM): sensor.sensor_schema(
            sensor.Sensor,
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER,
            accuracy_decimals=0,
        ).extend(),         
    }).extend(cv.polling_component_schema("5s"))


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])
    #var = cg.new_Pvariable(config[CONF_ID])
    
    
    if CONF_SENSOR_TEMP_TOP in config:
        sens = await sensor.new_sensor(config[CONF_SENSOR_TEMP_TOP])
        cg.add(paren.set_sensor_temp_top(sens))
        
        #await sensor.register_sensor(var, config[CONF_SENSOR_TEMP_TOP])
        #cg.add(paren.register_sensor(var))

    if CONF_SENSOR_TEMP_BOTTOM in config:
        sens = await sensor.new_sensor(config[CONF_SENSOR_TEMP_BOTTOM])
        cg.add(paren.set_sensor_temp_bottom(sens))
        
        #await sensor.register_sensor(var, config[CONF_SENSOR_TEMP_BOTTOM])
        #cg.add(paren.register_sensor(var))