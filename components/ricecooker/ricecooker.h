#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

#include "program.h"

namespace esphome {
namespace ricecooker {

class Program;

static const uint8_t RECV_HEADER = 0xaa;
static const uint8_t SEND_HEADER = 0x55;

static const char *const TAG = "ricecooker";

class RiceCooker : public Component, public uart::UARTDevice {

    public:
        void start();
        void cancel();
        void set_cooking_mode();
        void manual_temperature_set(uint8_t temp);
        void manual_timer_set();  

        void power_on();
        void power_off();
        void power_modulate(uint8_t target_temp, uint8_t hysteresis);

        void set_program(Program* program);

        uint8_t get_top_temperature();
        uint8_t get_bottom_temperature();

        bool get_power();

        char* get_program_name();

        void setup() override;
        void loop() override;
        void update();
        //void dump_config() override;

    private:

        uint16_t crc16(const uint8_t *data, size_t len);
        uint8_t int_7seg(uint8_t value, bool dot);
        
        void write_data(uint8_t *buffer);
        void timer();
        void mcu_send();
        void mcu_recv();

        // UART communication with MCU
        uint8_t send_buffer[11];
        uint8_t recv_buffer[10];

        // Tickers
        int mcu_interval = 100;
        int relay_interval = 500;
        int mcu_last = 0, relay_last = 0;

        int power_remain = 0;
        int power_wait_remain = 0;
        int power_modulate_last = 0;

        // State
        int hours = 0;
        int minutes = 0;
        bool middle_dots = true;

        bool power = false;

        bool sleep = false;

        Program* program {nullptr};

        uint8_t top_temperature;
        uint8_t bottom_temperature;

        uint8_t max_temperature = 0;
        uint8_t last_max_target = 0;
        uint8_t last_min_temp = 0;
        int last_power_time = 0;

        /* Estimate of milliseconds of the heater on needed to rise 1ºC bottom_temperature */
        int thermal_mass = 1500;
};


}
}
