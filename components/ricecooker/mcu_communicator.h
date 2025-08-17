#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace ricecooker {

class MCUCommunicator : public Component {
public:
    enum class LED_ID {
        LED1 = 1,
        LED2,
        LED3,
        LED4,
        LED5,
        LED6,
        LED7,
        LED8,
        LED9_ORANGE,
        LED9_BLUE
    };

    enum class LED_STATE {
        OFF = 0,
        ON = 1
    };

    MCUCommunicator(uart::UARTDevice *parent = nullptr);

    void setup();
    void loop();

    void send_data();
    void receive_data();

    void set_temperature(uint8_t top_temp, uint8_t bottom_temp);
    void set_time(uint8_t hours, uint8_t minutes);
    void set_power(bool power);
    void set_sleep(bool sleep);
    void set_led_status(LED_ID led, LED_STATE state);

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

    // LED status tracking
    bool led1_status = false;
    bool led2_status = false;
    bool led3_status = false;
    bool led4_status = false;
    bool led5_status = false;
    bool led6_status = false;
    bool led7_status = false;
    bool led8_status = false;
    bool led9_orange_status = false;
    bool led9_blue_status = false;
};

} // namespace ricecooker
} // namespace esphome