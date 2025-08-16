from esphome.components import sensor, uart
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

AUTO_LOAD = ["sensor"]

CONF_UART = "uart_id"
CONF_MAX_TEMP = "max_temp"
CONF_MIN_TEMP = "min_temp"

ricecooker_ns = cg.esphome_ns.namespace("ricecooker")
RiceCooker = ricecooker_ns.class_("RiceCooker", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema({
  cv.GenerateID(): cv.declare_id(RiceCooker),
  cv.Required(CONF_UART): cv.string,
  cv.Required(CONF_MAX_TEMP): cv.int_,
  cv.Required(CONF_MIN_TEMP): cv.int_,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)