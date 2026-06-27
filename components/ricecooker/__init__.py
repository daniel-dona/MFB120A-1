"""ESPHome external component: Xiaomi MFB120A-1 rice cooker.

Provides flexible, YAML-driven cooking programs using three stage primitives:
  - transitional (reach temp, then advance)
  - timed hold (maintain temp for a duration, then advance)
  - infinite hold (maintain temp until cancelled)

Programs are 100% YAML — no C++ subclassing needed.

Keep-warm is explicit: define a program with a `keep` stage and chain to it
via `next: "Mantener Caliente"` on any program that should keep warm after.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, binary_sensor, text_sensor
from esphome.const import CONF_ID

CODEOWNERS = ["@daniel-dona"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "select", "binary_sensor", "text_sensor"]

# ─── Top-level configuration ────────────────────────────────────────────────
CONF_PROGRAMS = "programs"

# ─── Per-program configuration ──────────────────────────────────────────────
CONF_PROGRAM_NAME = "name"
CONF_PROGRAM_NEXT = "next"
CONF_PROGRAM_DESCRIPTION = "description"

# ─── Per-stage configuration ────────────────────────────────────────────────
CONF_STAGE_TYPE = "type"
CONF_STAGE_FROM = "from"
CONF_STAGE_TO = "to"
CONF_STAGE_HYSTERESIS = "hysteresis"
CONF_STAGE_DURATION = "duration"
CONF_STAGE_TOP_THRESHOLD = "top_threshold"
CONF_STAGE_BOTTOM_THRESHOLD = "bottom_threshold"
CONF_STAGE_POWER = "power"
CONF_STAGE_CURVE = "curve"
CONF_STAGE_DESCRIPTION = "description"
CONF_STAGE_CURVE = "curve"

# Named power levels for user-friendly YAML
POWER_LEVELS = {
    "off": 0,
    "gentle": 3,
    "low": 6,
    "medium": 10,
    "high": 16,
    "max": 28,
    "auto": 255,
}

def _validate_power(value):
    """Accept either a named level (string) or raw 0-255 number."""
    if isinstance(value, str):
        if value not in POWER_LEVELS:
            raise cv.Invalid(f"Unknown power level: {value}. Use: {list(POWER_LEVELS.keys())}")
        return POWER_LEVELS[value]
    return value

STAGE_TYPES = ["heat", "hold", "ramp", "keep", "action", "notify"]


def _validate_stage(config):
    """Validate stage based on its type."""
    stype = config[CONF_STAGE_TYPE]
    if stype == "keep":
        if CONF_STAGE_DURATION in config:
            raise cv.Invalid("keep stage cannot have duration")
    elif stype == "action":
        # action stages block until user confirms — duration not allowed
        if CONF_STAGE_DURATION in config:
            raise cv.Invalid("action stage cannot have duration; it waits for user confirmation")
    elif stype == "notify":
        # notify stages can optionally have a duration (how long to show the message)
        pass
    elif stype in ("hold", "ramp"):
        if CONF_STAGE_DURATION not in config:
            raise cv.Invalid(f"{stype} stage requires 'duration'")
    elif stype == "heat":
        if CONF_STAGE_DURATION in config:
            raise cv.Invalid("heat stage should not have duration; use 'hold' instead")
    if stype == "ramp":
        if CONF_STAGE_FROM not in config:
            raise cv.Invalid("ramp stage requires 'from' (start temperature)")
    if stype == "action" and not config.get(CONF_STAGE_DESCRIPTION, ""):
        raise cv.Invalid("action stage requires 'description' (user message)")
    return config


STAGE_SCHEMA = cv.All(
    cv.Schema({
        cv.Required(CONF_STAGE_TYPE): cv.one_of(*STAGE_TYPES),
        cv.Required(CONF_STAGE_TO): cv.int_range(0, 200),
        cv.Optional(CONF_STAGE_FROM, default=0): cv.int_range(0, 200),
        cv.Optional(CONF_STAGE_HYSTERESIS, default=3): cv.int_range(0, 30),
        cv.Optional(CONF_STAGE_DURATION): cv.positive_time_period_seconds,
        cv.Optional(CONF_STAGE_TOP_THRESHOLD, default=0): cv.int_range(0, 200),
        cv.Optional(CONF_STAGE_BOTTOM_THRESHOLD, default=0): cv.int_range(0, 200),
        cv.Optional(CONF_STAGE_POWER, default=255): _validate_power,
        cv.Optional(CONF_STAGE_CURVE, default="linear"): cv.one_of("linear", "ease_in", "ease_out", "step"),
        cv.Optional(CONF_STAGE_DESCRIPTION, default=""): cv.string_strict,
    }),
    _validate_stage,
)

PROGRAM_SCHEMA = cv.Schema({
    cv.Required(CONF_PROGRAM_NAME): cv.string_strict,
    cv.Required("stages"): cv.ensure_list(STAGE_SCHEMA),
    cv.Optional(CONF_PROGRAM_NEXT, default=""): cv.string_strict,
    cv.Optional(CONF_PROGRAM_DESCRIPTION, default=""): cv.string_strict,
})

ricecooker_ns = cg.esphome_ns.namespace("ricecooker")
RiceCooker = ricecooker_ns.class_("RiceCooker", cg.Component, uart.UARTDevice)

CONF_CAN_START_SENSOR = "can_start_sensor"
CONF_CAN_CANCEL_SENSOR = "can_cancel_sensor"
CONF_STATE_SENSOR = "state_sensor"

CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(): cv.declare_id(RiceCooker),
        cv.Optional(CONF_PROGRAMS): cv.ensure_list(PROGRAM_SCHEMA),
        cv.Optional(CONF_CAN_START_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_CAN_CANCEL_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_STATE_SENSOR): cv.use_id(text_sensor.TextSensor),
    })
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Store program names for select.py to use during codegen
    import sys
    _self = sys.modules[__name__]
    _self._program_names = []

    for prog_config in config.get(CONF_PROGRAMS, []):
        cg.add(var.add_custom_program(
            prog_config[CONF_PROGRAM_NAME],
            prog_config.get(CONF_PROGRAM_NEXT, ""),
            prog_config.get(CONF_PROGRAM_DESCRIPTION, ""),
        ))
        _self._program_names.append(prog_config[CONF_PROGRAM_NAME])
        for stage_config in prog_config["stages"]:
            stype = stage_config[CONF_STAGE_TYPE]
            duration_sec = stage_config.get(CONF_STAGE_DURATION)
            duration_ms = int(duration_sec.total_seconds * 1000) if duration_sec else 0

            # Map stage type to numeric enum
            stage_type_map = {
                "heat": 0,    # TRANSITIONAL
                "hold": 1,    # TIMED_HOLD
                "ramp": 2,    # RAMP
                "keep": 3,    # INFINITE_HOLD
                "action": 4,  # USER_ACTION
                "notify": 5,  # NOTIFY
            }
            stage_type_val = stage_type_map.get(stype, 0)

            if stype == "keep":
                hold, dur = True, 0
            elif stype == "hold":
                hold, dur = False, duration_ms
            elif stype == "ramp":
                hold, dur = False, duration_ms
            elif stype == "action":
                hold, dur = False, 0  # No duration, waits for user
            elif stype == "notify":
                hold, dur = False, duration_ms  # Optional duration
            else:  # heat
                hold, dur = False, 0

            # Stage description as a C string literal (const char*)
            stage_desc = stage_config.get(CONF_STAGE_DESCRIPTION, "")
            if stage_desc:
                desc_expr = cg.RawExpression(f'"{stage_desc}"')
            else:
                desc_expr = cg.RawExpression('nullptr')

            cg.add(var.add_program_stage(
                stage_config[CONF_STAGE_TO],
                stage_config[CONF_STAGE_HYSTERESIS],
                dur,
                hold,
                stage_config[CONF_STAGE_TOP_THRESHOLD],
                stage_config[CONF_STAGE_BOTTOM_THRESHOLD],
                stage_config[CONF_STAGE_POWER],
                stage_config.get(CONF_STAGE_FROM, 0),
                stage_type_val,  # stage_type enum value
                {"linear": 0, "ease_in": 1, "ease_out": 2, "step": 3}.get(
                    stage_config.get(CONF_STAGE_CURVE, "linear"), 0),
                desc_expr,
            ))

    # Connect optional HA entity sensors for reactive publishing
    if CONF_CAN_START_SENSOR in config:
        can_start_var = await cg.get_variable(config[CONF_CAN_START_SENSOR])
        cg.add(var.set_can_start_sensor(can_start_var))
    if CONF_CAN_CANCEL_SENSOR in config:
        can_cancel_var = await cg.get_variable(config[CONF_CAN_CANCEL_SENSOR])
        cg.add(var.set_can_cancel_sensor(can_cancel_var))
    if CONF_STATE_SENSOR in config:
        state_var = await cg.get_variable(config[CONF_STATE_SENSOR])
        cg.add(var.set_state_text_sensor(state_var))