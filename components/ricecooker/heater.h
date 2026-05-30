#pragma once

#include "esphome/core/datatypes.h"

namespace esphome::ricecooker {

class Heater {
 public:
  void power_on();
  void power_off();
  void power_modulate(uint8_t target_temp, uint8_t hysteresis);

  uint8_t get_top_temperature() { return top_temperature_; }
  uint8_t get_bottom_temperature() { return bottom_temperature_; }

  void reset();
  void update(uint8_t top_temp, uint8_t bottom_temp);
  void step(uint32_t millis);
  bool get_power() { return power_; }

 private:
  uint8_t max_target_{0};
  uint8_t min_target_{0};

  int32_t power_remain_{0};
  int32_t power_wait_remain_{0};
  uint32_t power_modulate_last_{0};

  bool power_{false};

  uint8_t top_temperature_{0};
  uint8_t bottom_temperature_{0};

  uint8_t max_temperature_{0};
  uint8_t last_max_target_{0};
  uint8_t last_min_temp_{0};
  uint32_t last_power_time_{0};

  bool just_reset_{true};

  /// Estimate of milliseconds of the heater on needed to rise 1°C bottom_temperature.
  int32_t thermal_mass_{1500};
};

}  // namespace esphome::ricecooker