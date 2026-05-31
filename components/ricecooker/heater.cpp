#include "heater.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "ricecooker_heater";

void Heater::power_on() {
  if (!power_ && !emergency_) {
    ESP_LOGD(TAG, "Heater: ON");
    power_ = true;
  }
}

void Heater::power_off() {
  if (power_) {
    ESP_LOGD(TAG, "Heater: OFF");
    power_ = false;
  }
}

void Heater::power_modulate(uint8_t target_temp, uint8_t hysteresis) {
  max_target_ = target_temp + hysteresis;
  min_target_ = target_temp - hysteresis;
}

void Heater::reset() {
  power_off();
  max_target_ = 0;
  min_target_ = 0;
  power_level_ = 28;
  cycle_start_ms_ = 0;
}

void Heater::emergency_off() {
  power_off();
  power_level_ = 0;
  emergency_ = true;
  ESP_LOGE(TAG, "Heater: EMERGENCY OFF");
}

void Heater::update(uint8_t top_temp, uint8_t bottom_temp) {
  top_temperature_ = top_temp;
  bottom_temperature_ = bottom_temp;
}

uint32_t Heater::get_on_time_ms() const {
  if (power_level_ == 0 || emergency_) {
    return 0;
  }
  if (power_level_ >= MAX_POWER) {
    return cycle_period_;  // 100% duty cycle
  }
  // Linear mapping: power 1-27 maps to 1/28 to 27/28 of cycle
  return (static_cast<uint32_t>(power_level_) * cycle_period_) / MAX_POWER;
}

void Heater::step(uint32_t now_ms) {
  if (emergency_) {
    power_off();
    return;
  }

  // If no targets are set, don't control the heater
  if (max_target_ == 0 && min_target_ == 0) {
    return;
  }

  // Initialize cycle start time
  if (cycle_start_ms_ == 0) {
    cycle_start_ms_ = now_ms;
  }

  uint32_t on_time_ms = get_on_time_ms();
  uint32_t elapsed = now_ms - cycle_start_ms_;

  // Check if we need to start a new cycle
  if (elapsed >= cycle_period_) {
    cycle_start_ms_ = now_ms;
    elapsed = 0;
  }

  // Temperature feedback has priority over power cycle
  // If bottom temp exceeds max target, force OFF regardless of cycle
  if (bottom_temperature_ >= max_target_) {
    power_off();
    return;
  }

  // If bottom temp is well below min target, force ON regardless of cycle
  if (bottom_temperature_ < min_target_ && power_level_ > 0) {
    power_on();
    return;
  }

  // Within hysteresis band: follow power cycle timing
  if (power_level_ == 0) {
    power_off();
  } else if (elapsed < on_time_ms) {
    power_on();
  } else {
    power_off();
  }
}

}  // namespace esphome::ricecooker