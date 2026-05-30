"""ESPHome external component for Xiaomi MFB120A-1 rice cooker custom firmware."""
from esphome.components import uart
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID, CONF_NAME

CODEOWNERS = ["@daniel-dona"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "switch", "select"]

# --- Top-level configuration ---
CONF_KEEP_WARM_TEMPERATURE = "keep_warm_temperature"
CONF_KEEP_WARM_HYSTERESIS = "keep_warm_hysteresis"
CONF_PROGRAMS = "programs"

# --- Per-program configuration ---
CONF_PROGRAM_KEEP_WARM_AFTER = "keep_warm_after"
CONF_PROGRAM_STAGES = "stages"

# --- Per-stage configuration ---
CONF_STAGE_TARGET_TEMPERATURE = "target_temperature"
CONF_STAGE_HYSTERESIS = "hysteresis"
CONF_STAGE_DURATION = "duration"
CONF_STAGE_HOLD = "hold"


def _validate_stage(config):
    """Validate that hold and duration are mutually exclusive."""
    if config.get(CONF_STAGE_HOLD) and CONF_STAGE_DURATION in config:
        raise cv.Invalid(
            "Cannot specify both 'hold: true' and 'duration' in the same stage. "
            "Use 'hold: true' for infinite hold, or 'duration' for a timed hold.",
            path=[CONF_STAGE_DURATION],
        )
    return config


STAGE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_STAGE_TARGET_TEMPERATURE): cv.int_range(0, 120),
            cv.Optional(CONF_STAGE_HYSTERESIS, default=3): cv.int_range(0, 30),
            cv.Optional(CONF_STAGE_DURATION): cv.positive_time_period_seconds,
            cv.Optional(CONF_STAGE_HOLD, default=False): cv.boolean,
        }
    ),
    _validate_stage,
)

PROGRAM_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Required(CONF_PROGRAM_STAGES): cv.ensure_list(STAGE_SCHEMA),
        cv.Optional(CONF_PROGRAM_KEEP_WARM_AFTER, default=True): cv.boolean,
    }
)

ricecooker_ns = cg.esphome_ns.namespace("ricecooker")
RiceCooker = ricecooker_ns.class_("RiceCooker", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RiceCooker),
            cv.Optional(CONF_KEEP_WARM_TEMPERATURE, default=65): cv.int_range(0, 120),
            cv.Optional(CONF_KEEP_WARM_HYSTERESIS, default=5): cv.int_range(0, 30),
            cv.Optional(CONF_PROGRAMS): cv.ensure_list(PROGRAM_SCHEMA),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_keep_warm_temperature(config[CONF_KEEP_WARM_TEMPERATURE]))
    cg.add(var.set_keep_warm_hysteresis(config[CONF_KEEP_WARM_HYSTERESIS]))

    # Create custom programs from YAML
    for program_config in config.get(CONF_PROGRAMS, []):
        cg.add(var.add_custom_program(
            program_config[CONF_NAME],
            program_config[CONF_PROGRAM_KEEP_WARM_AFTER],
        ))
        for stage_config in program_config[CONF_PROGRAM_STAGES]:
            duration_sec = stage_config.get(CONF_STAGE_DURATION)
            duration_ms = int(duration_sec.total_seconds * 1000) if duration_sec else 0
            cg.add(var.add_program_stage(
                stage_config[CONF_STAGE_TARGET_TEMPERATURE],
                stage_config[CONF_STAGE_HYSTERESIS],
                duration_ms,
                stage_config[CONF_STAGE_HOLD],
            ))