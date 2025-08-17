import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output
from esphome.const import CONF_ID, UNIT_CELSIUS, ICON_THERMOMETER
from . import RiceCooker

DEPENDENCIES = ["ricecooker"]

CONF_RICECOOKER_ID = "ricecooker_id"

CONF_LED_WIFI = "wifi_status"



CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_RICECOOKER_ID): cv.use_id(RiceCooker),
        
        cv.Optional(CONF_LED_WIFI): output.BINARY_OUTPUT_SCHEMA.extend(cv.COMPONENT_SCHEMA)
           
    }).extend(cv.polling_component_schema("5s"))


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])   
    
    if CONF_LED_WIFI in config:
        await output.register_output(paren, config[CONF_LED_WIFI])
        #cg.add(paren.set_sensor_temp_top(sens))
        