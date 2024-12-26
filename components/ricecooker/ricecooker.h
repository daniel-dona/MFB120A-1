#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace ricecooker {

static const uint8_t RECV_HEADER = 0xaa;
static const uint8_t SEND_HEADER = 0x55;

// Byte definitions


// Cook modes



class RiceCooker : public Component, public uart::UARTDevice {

    public:
        void start();
        void stop();
        void set_cooking_mode();
        void manual_temperature_set(uint8_t temp);
        void manual_timer_set();  

        void setup() override;
        void loop() override;
        //void dump_config() override;

    private:

        uint16_t crc16(const uint8_t *data, size_t len);
        uint8_t int_7seg(uint8_t value, bool dot);
        void power_on();
        void power_off();
        void write_data(uint8_t *buffer);
        void timer();
        void mcu_send();
        void mcu_recv();

        // UART communication with MCU
        uint8_t send_buffer[11];
        uint8_t recv_buffer[10];

        // Tickers
        int mcu_interval = 100;
        int relay_interval = 2000;
        int mcu_last = 0, relay_last = 0;

        // State
        int hours = 0;
        int minutes = 0;
        bool middle_dots = true;

        bool power = false;

        int mode;
        bool warm = false;
        bool cooking = false;
        bool sleep = false;


        sensor::Sensor *top_temperature_{nullptr};
        sensor::Sensor *bottom_temperature_{nullptr};
        sensor::Sensor *runtime_{nullptr};
};


}
}