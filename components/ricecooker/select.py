"""ESPHome select platform for rice cooker program selection."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID, CONF_NAME
from . import RiceCooker, ricecooker_ns

DEPENDENCIES = ["ricecooker"]

CONF_RICECOOKER_ID = "ricecooker_id"

RiceCookerProgramSelect = ricecooker_ns.class_(
    "RiceCookerProgramSelect", select.Select, cg.Component
)

CONFIG_SCHEMA = select.select_schema(RiceCookerProgramSelect).extend({
    cv.Required(CONF_RICECOOKER_ID): cv.use_id(RiceCooker),
})


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])
    sel = cg.new_Pvariable(config[CONF_ID])
    await select.register_select(sel, config, options=["None"])
    await cg.register_component(sel, config)
    cg.add(sel.set_ricecooker(paren))