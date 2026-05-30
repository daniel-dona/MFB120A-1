"""ESPHome switch platform for the rice cooker power control."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from . import RiceCooker, ricecooker_ns

DEPENDENCIES = ["ricecooker"]

CONF_RICECOOKER_ID = "ricecooker_id"
CONF_POWER = "power"

RiceCookerPowerSwitch = ricecooker_ns.class_(
    "RiceCookerPowerSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_RICECOOKER_ID): cv.use_id(RiceCooker),
        cv.Optional(CONF_POWER): switch.switch_schema(RiceCookerPowerSwitch),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])

    if power_config := config.get(CONF_POWER):
        sw = await switch.new_switch(power_config)
        await cg.register_component(sw, power_config)
        cg.add(sw.set_ricecooker(paren))