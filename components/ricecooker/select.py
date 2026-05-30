"""ESPHome select platform for the rice cooker program selection.

The options list is built dynamically from the YAML-defined programs
during C++ setup(), so we only need the built-in 'None' placeholder here.
The full options are populated in RiceCookerProgramSelect::setup().
"""
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

# Placeholder options — full list is built in C++ setup() from YAML programs
SELECT_OPTIONS = ["None"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_RICECOOKER_ID): cv.use_id(RiceCooker),
        cv.Optional(CONF_PROGRAM): select.select_schema(RiceCookerProgramSelect),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_RICECOOKER_ID])

    if program_config := config.get(CONF_PROGRAM):
        sel = await select.new_select(program_config, options=SELECT_OPTIONS)
        await cg.register_component(sel, program_config)
        cg.add(sel.set_ricecooker(paren))