#pragma once

#include "esphome/core/datatypes.h"

namespace esphome {
namespace ricecooker {


class Heater {

    public:

        void power_on();
        void power_off();
        void power_modulate(uint8_t target_temp, uint8_t hysteresis);

        uint8_t get_top_temperature();
        uint8_t get_bottom_temperature();

        void reset();

        void update(uint8_t top_temp, uint8_t bottom_temp);
        void step(int millis);
        bool get_power();

    private:

        uint8_t max_target = 0;
        uint8_t min_target = 0;

        int power_remain = 0;
        int power_wait_remain = 0;
        int power_modulate_last = 0;

        bool power = false;

        uint8_t top_temperature = 0;
        uint8_t bottom_temperature = 0;

        uint8_t max_temperature = 0;
        uint8_t last_max_target = 0;
        uint8_t last_min_temp = 0;
        int last_power_time = 0;

        bool just_reset = true;

        /* Estimate of milliseconds of the heater on needed to rise 1ºC bottom_temperature */
        int thermal_mass = 1500;
};


}
}
