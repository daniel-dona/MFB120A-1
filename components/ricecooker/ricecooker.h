#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

#include "program.h"
#include "heater.h"
#include "mcu_communicator.h"

namespace esphome {
namespace ricecooker {

class Program;

static const char *const TAG = "ricecooker";

class RiceCooker : public Component, public uart::UARTDevice {

    public:
        RiceCooker();

        void set_sensor_temp_top(sensor::Sensor *sensor_top) { sensor_top_ = sensor_top; }
        void set_sensor_temp_bottom(sensor::Sensor *sensor_bottom) { sensor_bottom_ = sensor_bottom; }

        void start();
        void cancel();
        void set_cooking_mode();
        void manual_temperature_set(uint8_t temp);
        void manual_timer_set();

        void power_on();
        void power_off();

        void set_program(Program* program);

        uint8_t get_top_temperature();
        uint8_t get_bottom_temperature();

        bool get_power();

        char* get_program_name();

        void setup() override;
        void loop() override;
        void update();
        //void dump_config() override;

    protected:
        sensor::Sensor *sensor_top_;
        sensor::Sensor *sensor_bottom_;

    private:
        void timer();

        // Tickers
        int relay_interval = 500;
        int relay_last = 0;

        // State
        int hours = 0;
        int minutes = 0;
        bool middle_dots = true;

        bool sleep = false;

        Program* program {nullptr};
        Heater heater;
        MCUCommunicator* mcu_communicator;
};
}
}