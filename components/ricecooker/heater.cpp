#include "heater.h"

#include "esphome/core/log.h"
#include "esp_log.h"

static const char *const TAG = "ricecooker";

namespace esphome {
namespace ricecooker {

    void Heater::power_on() {
        if(!this->power){
            ESP_LOGD(TAG, "Heater power: on");
            this->power = true;
        }
    }

    void Heater::power_off() {
        if(this->power){
            ESP_LOGD(TAG, "Heater power: off");
            this->power = false;
        }
    }

    void Heater::power_modulate(uint8_t target_temp, uint8_t hysteresis) {
        max_target = target_temp + hysteresis;
        min_target = target_temp - hysteresis;
    }

    uint8_t Heater::get_top_temperature() {
        return top_temperature;
    }

    uint8_t Heater::get_bottom_temperature() {
        return bottom_temperature;
    }

    void Heater::reset() {
        power_off();
        just_reset = true;

        max_target = 0;
        min_target = 0;

        power_remain = 0;
        power_wait_remain = 0;
        power_modulate_last = 0;

        last_max_target = 0;
        last_power_time = 0;
    }

    bool Heater::get_power() {
        return this->power;
    }

    void Heater::update(uint8_t top_temp, uint8_t bottom_temp) {
        this->top_temperature = top_temp;
        this->bottom_temperature = bottom_temp;
        this->max_temperature = std::max(max_temperature, bottom_temp);
    }

    void Heater::step(int millis) {

        int lapsed = millis - power_modulate_last;
        power_modulate_last = millis;

        if (power_remain != 0) {
            power_remain = std::max(1, power_remain - lapsed);
        }
        
        power_wait_remain = std::max(0, power_wait_remain - lapsed);

        if (bottom_temperature < min_target && power_remain == 0 && power_wait_remain == 0) {

            power_on();

            int range = (int) max_temperature - (int) last_min_temp;
            
            int time_needed;
            if (range >= 1) {
                time_needed = last_power_time / range;
            } else {
                // Avoid division by zero.
                // Temperature did not rise with last_power_time, so increment it
                time_needed = last_power_time + last_power_time / 4;
            }

            int diff = (int) last_max_target - (int) max_temperature;
            diff = std::clamp(diff, -3, 3);

            int error = time_needed - thermal_mass;
            ESP_LOGD(TAG, "In last heating: error %d ms/ºC, diff %dºC", error, diff);

            if (
                !just_reset
                // We cannot estimate thermal mass if heat is used to boil water
                // instead of raising its temperature.
                && max_temperature < 100
            ) {
                // Change estimated thermal mass in proportion to how different (`diff`)
                // was the actual max temperature and its target.
                if (diff == 0) {
                    thermal_mass += std::clamp(error, -200, 200);
                } else if (diff > 0) {
                    thermal_mass += std::clamp(error, 200, 500 * diff);
                } else {
                    thermal_mass += std::clamp(error, -500 * diff, -200);
                }
            }

            power_remain = (max_target - bottom_temperature) * thermal_mass;

            last_max_target = max_target;
            max_temperature = bottom_temperature;
            last_min_temp = bottom_temperature;
            last_power_time = power_remain;
            just_reset = false;

            ESP_LOGD(TAG, "Power modulating: heating ON for %d ms, Thermal mass %d ms/ºC", power_remain, thermal_mass);

        } else if (bottom_temperature >= max_target || power_remain == 1) {

            power_off();

            power_remain = 0;
            power_wait_remain = 30000;

        } else {
            // Keep last heating state to reduce relay wear.

            ESP_LOGD(TAG, "Power modulating: power remaining %d ms, power waiting %d ms, Thermal mass %d ms/ºC", power_remain, power_wait_remain, thermal_mass);
        }
    }
}
}
