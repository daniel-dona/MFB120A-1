#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace ricecooker {

class MCUCommunicator : public Component {
public:
    MCUCommunicator(uart::UARTDevice *parent = nullptr);

    void setup();
    void loop();

    void send_data();
    void receive_data();

    void set_temperature(uint8_t top_temp, uint8_t bottom_temp);
    void set_time(uint8_t hours, uint8_t minutes);
    void set_power(bool power);
    void set_sleep(bool sleep);

    uint8_t get_top_temperature();
    uint8_t get_bottom_temperature();

private:
    uint16_t crc16(const uint8_t *data, size_t len);
    uint8_t int_7seg(uint8_t value, bool dot);
    void write_data();

    // UART communication buffers
    uint8_t send_buffer[11];
    uint8_t recv_buffer[10];

    // Communication parameters
    int mcu_interval = 100;
    int mcu_last = 0;
    
    // UART device reference
    uart::UARTDevice *uart_device_;

    // State
    uint8_t top_temperature = 0;
    uint8_t bottom_temperature = 0;
    uint8_t hours = 0;
    uint8_t minutes = 0;
    bool power = false;
    bool sleep = false;
    bool middle_dots = true;
};

} // namespace ricecooker
} // namespace esphome