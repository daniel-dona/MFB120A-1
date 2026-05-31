"""ESPHome external component: Xiaomi MFB120A-1 rice cooker.

Provides flexible, YAML-driven cooking programs using three stage primitives:
  - transitional (reach temp, then advance)
  - timed hold (maintain temp for a duration, then advance)
  - infinite hold (maintain temp until cancelled)

Safety mechanisms (always active, not configurable):
  - Emergency shutoff if plate temp > 120°C
  - Emergency shutoff if lid temp > 84°C during infinite hold
  - Slow bang-bang relay control with configurable power levels

Programs are 100% YAML — no C++ subclassing needed.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@daniel-dona"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "switch", "select"]

# ─── Top-level configuration ────────────────────────────────────────────────
CONF_KEEP_WARM_TEMPERATURE = "keep_warm_temperature"
CONF_KEEP_WARM_HYSTERESIS = "keep_warm_hysteresis"
CONF_PROGRAMS = "programs"

# ─── Per-program configuration ──────────────────────────────────────────────
CONF_PROGRAM_NAME = "name"
CONF_PROGRAM_KEEP_WARM_AFTER = "keep_warm_after"

# ─── Per-stage configuration ────────────────────────────────────────────────
CONF_STAGE_TARGET_TEMPERATURE = "target_temperature"
CONF_STAGE_HYSTERESIS = "hysteresis"
CONF_STAGE_DURATION = "duration"
CONF_STAGE_HOLD = "hold"
CONF_STAGE_TOP_THRESHOLD = "top_threshold"
CONF_STAGE_BOTTOM_THRESHOLD = "bottom_threshold"
CONF_STAGE_POWER = "power"


def _validate_stage(config):
    """Ensure hold and duration are mutually exclusive."""
    has_duration = CONF_STAGE_DURATION in config
    has_hold = config.get(CONF_STAGE_HOLD) is True
    if has_duration and has_hold:
        raise cv.Invalid(
            "Cannot specify both 'duration' and 'hold: true' in the same stage. "
            "Use 'hold: true' for infinite hold, or 'duration' for a timed hold."
        )
    if not has_duration and not has_hold and config.get(CONF_STAGE_HOLD) is not True:
        # No duration and no hold → transitional (advance when temperature reached)
        pass
    return config


STAGE_SCHEMA = cv.All(
    cv.Schema({
        cv.Required(CONF_STAGE_TARGET_TEMPERATURE): cv.int_range(0, 200),
        cv.Optional(CONF_STAGE_HYSTERESIS, default=3): cv.int_range(0, 30),
        cv.Optional(CONF_STAGE_DURATION): cv.positive_time_period_seconds,
        cv.Optional(CONF_STAGE_HOLD, default=False): cv.boolean,
        cv.Optional(CONF_STAGE_TOP_THRESHOLD, default=0): cv.int_range(0, 200),
        cv.Optional(CONF_STAGE_BOTTOM_THRESHOLD, default=0): cv.int_range(0, 200),
        cv.Optional(CONF_STAGE_POWER, default=255): cv.int_range(0, 255)
    }),
    _validate_stage,
)

PROGRAM_SCHEMA = cv.Schema({
    cv.Required(CONF_PROGRAM_NAME): cv.string_strict,
    cv.Required("stages"): cv.ensure_list(STAGE_SCHEMA),
    cv.Optional(CONF_PROGRAM_KEEP_WARM_AFTER, default=True): cv.boolean,
})

ricecooker_ns = cg.esphome_ns.namespace("ricecooker")
RiceCooker = ricecooker_ns.class_("RiceCooker", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(): cv.declare_id(RiceCooker),
        cv.Optional(CONF_KEEP_WARM_TEMPERATURE, default=75): cv.int_range(0, 120),
        cv.Optional(CONF_KEEP_WARM_HYSTERESIS, default=4): cv.int_range(0, 30),
        cv.Optional(CONF_PROGRAMS): cv.ensure_list(PROGRAM_SCHEMA),
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_keep_warm_temperature(config[CONF_KEEP_WARM_TEMPERATURE]))
    cg.add(var.set_keep_warm_hysteresis(config[CONF_KEEP_WARM_HYSTERESIS]))

    for prog_config in config.get(CONF_PROGRAMS, []):
        cg.add(var.add_custom_program(
            prog_config[CONF_PROGRAM_NAME],
            prog_config[CONF_PROGRAM_KEEP_WARM_AFTER],
        ))
        for stage_config in prog_config["stages"]:
            duration_sec = stage_config.get(CONF_STAGE_DURATION)
            duration_ms = int(duration_sec.total_seconds * 1000) if duration_sec else 0
            cg.add(var.add_program_stage(
                stage_config[CONF_STAGE_TARGET_TEMPERATURE],
                stage_config[CONF_STAGE_HYSTERESIS],
                duration_ms,
                stage_config[CONF_STAGE_HOLD],
                stage_config[CONF_STAGE_TOP_THRESHOLD],
                stage_config[CONF_STAGE_BOTTOM_THRESHOLD],
                stage_config[CONF_STAGE_POWER],
            ))