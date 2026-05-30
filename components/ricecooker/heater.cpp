#include "heater.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "ricecooker";

void Heater::power_on() {
  if (!power_) {
    ESP_LOGD(TAG, "Heater power: on");
    power_ = true;
  }
}

void Heater::power_off() {
  if (power_) {
    ESP_LOGD(TAG, "Heater power: off");
    power_ = false;
  }
}

void Heater::power_modulate(uint8_t target_temp, uint8_t hysteresis) {
  max_target_ = target_temp + hysteresis;
  min_target_ = target_temp - hysteresis;
}

void Heater::reset() {
  power_off();
  just_reset_ = true;

  max_target_ = 0;
  min_target_ = 0;

  power_remain_ = 0;
  power_wait_remain_ = 0;
  power_modulate_last_ = 0;

  last_max_target_ = 0;
  last_power_time_ = 0;
}

void Heater::update(uint8_t top_temp, uint8_t bottom_temp) {
  top_temperature_ = top_temp;
  bottom_temperature_ = bottom_temp;
  max_temperature_ = std::max(max_temperature_, bottom_temp);
}

void Heater::step(uint32_t millis) {
  int32_t lapsed = millis - power_modulate_last_;
  power_modulate_last_ = millis;

  if (power_remain_ != 0) {
    power_remain_ = std::max(static_cast<int32_t>(1), power_remain_ - lapsed);
  }

  power_wait_remain_ = std::max(static_cast<int32_t>(0), power_wait_remain_ - lapsed);

  if (bottom_temperature_ < min_target_ && power_remain_ == 0 && power_wait_remain_ == 0) {
    power_on();

    int range = static_cast<int32_t>(max_temperature_) - static_cast<int32_t>(last_min_temp_);

    int32_t time_needed;
    if (range >= 1) {
      time_needed = last_power_time_ / range;
    } else {
      // Avoid division by zero.
      // Temperature did not rise with last_power_time, so increment it
      time_needed = last_power_time_ + last_power_time_ / 4;
    }

    int32_t diff = static_cast<int32_t>(last_max_target_) - static_cast<int32_t>(max_temperature_);
    diff = std::clamp(diff, static_cast<int32_t>(-3), static_cast<int32_t>(3));

    int32_t error = time_needed - thermal_mass_;
    ESP_LOGD(TAG, "In last heating: error %d ms/°C, diff %d°C", error, diff);

    if (!just_reset_ && max_temperature_ < 100) {
      // We cannot estimate thermal mass if heat is used to boil water
      // instead of raising its temperature.
      if (diff == 0) {
        thermal_mass_ += std::clamp(error, static_cast<int32_t>(-200), static_cast<int32_t>(200));
      } else if (diff > 0) {
        thermal_mass_ += std::clamp(error, static_cast<int32_t>(200), static_cast<int32_t>(500 * diff));
      } else {
        thermal_mass_ += std::clamp(error, static_cast<int32_t>(-500 * (-diff)), static_cast<int32_t>(-200));
      }
    }

    power_remain_ = (max_target_ - bottom_temperature_) * thermal_mass_;

    last_max_target_ = max_target_;
    max_temperature_ = bottom_temperature_;
    last_min_temp_ = bottom_temperature_;
    last_power_time_ = power_remain_;
    just_reset_ = false;

    ESP_LOGD(TAG, "Power modulating: heating ON for %d ms, Thermal mass %d ms/°C", power_remain_,
             thermal_mass_);

  } else if (bottom_temperature_ >= max_target_ || power_remain_ == 1) {
    power_off();
    power_remain_ = 0;
    power_wait_remain_ = 30000;

  } else {
    ESP_LOGD(TAG, "Power modulating: power remaining %d ms, power waiting %d ms, Thermal mass %d ms/°C",
             power_remain_, power_wait_remain_, thermal_mass_);
  }
}

}  // namespace esphome::ricecooker