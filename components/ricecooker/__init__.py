"""ESPHome external component for Xiaomi MFB120A-1 rice cooker custom firmware."""
from esphome.components import uart
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID

CODEOWNERS = ["@daniel-dona"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "switch", "select"]

CONF_KEEP_WARM_TEMPERATURE = "keep_warm_temperature"
CONF_KEEP_WARM_HYSTERESIS = "keep_warm_hysteresis"
CONF_RICE_COOKING_TIME = "rice_cooking_time"

ricecooker_ns = cg.esphome_ns.namespace("ricecooker")
RiceCooker = ricecooker_ns.class_("RiceCooker", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RiceCooker),
            cv.Optional(CONF_KEEP_WARM_TEMPERATURE, default=65): cv.int_range(0, 120),
            cv.Optional(CONF_KEEP_WARM_HYSTERESIS, default=5): cv.int_range(0, 30),
            cv.Optional(CONF_RICE_COOKING_TIME, default=15): cv.positive_int,
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
    cg.add(var.set_rice_cooking_time(config[CONF_RICE_COOKING_TIME]))