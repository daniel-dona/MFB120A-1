"""ESPHome select platform for rice cooker program selection."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
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

    import sys
    from . import _program_names
    # Select options: program names only
    options = _program_names if _program_names else []

    sel = cg.new_Pvariable(config["id"])
    await select.register_select(sel, config, options=options)
    await cg.register_component(sel, config)
    cg.add(sel.set_ricecooker(paren))