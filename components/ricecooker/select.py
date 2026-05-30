"""ESPHome select platform for the rice cooker program selection."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from . import RiceCooker, ricecooker_ns

DEPENDENCIES = ["ricecooker"]

CONF_RICECOOKER_ID = "ricecooker_id"
CONF_PROGRAM = "program"

RiceCookerProgramSelect = ricecooker_ns.class_(
    "RiceCookerProgramSelect", select.Select, cg.Component
)

PROGRAM_OPTIONS = ["None", "Keep Warm", "Rice", "Fast Rice"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_RICECOOKER_ID): cv.use_id(RiceCooker),
        cv.Optional(CONF_PROGRAM): select.select_schema(RiceCookerProgramSelect),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])

    if program_config := config.get(CONF_PROGRAM):
        sel = await select.new_select(program_config, options=PROGRAM_OPTIONS)
        await cg.register_component(sel, program_config)
        cg.add(sel.set_ricecooker(paren))