#include "program.h"
#include "heater.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "ricecooker";

// --- KeepWarm ---

KeepWarm::KeepWarm(uint8_t target_temp, uint8_t hysteresis)
    : target_temp_(target_temp), hysteresis_(hysteresis) {}

void KeepWarm::step(Heater *heater) {
  auto bottom_temp = heater->get_bottom_temperature();
  auto top_temp = heater->get_top_temperature();

  switch (stage_) {
    case Wait:
      ESP_LOGD(TAG, "Keep warm waiting. Temperature: top: %d°C, bottom: %d°C", top_temp, bottom_temp);
      break;

    case Warm:
      ESP_LOGD(TAG,
               "Keep Warm. Temperatures: top: %d°C, bottom: %d°C, target: %d°C, hysteresis: %d°C",
               top_temp, bottom_temp, target_temp_, hysteresis_);
      heater->power_modulate(target_temp_, hysteresis_);
      break;
  }
}

void KeepWarm::start() { stage_ = Warm; }

void KeepWarm::cancel() { stage_ = Wait; }

// --- RiceProgram ---

static const unsigned int RICE_PROGRAM_SOAK_MINUTES = 45;
static const unsigned int RICE_PROGRAM_REST_MINUTES = 10;

RiceProgram::RiceProgram(uint8_t cooking_time, uint8_t cooking_temp, bool fast)
    : cooking_time_(cooking_time), cooking_temp_(cooking_temp), fast_(fast) {}

const char *RiceProgram::get_name() const { return fast_ ? FAST_RICE_NAME : RICE_NAME; }

void RiceProgram::start() { set_stage(Start); }

void RiceProgram::cancel() { set_stage(Wait); }

void RiceProgram::reset() {
  stage_ = Wait;
  finished_ = false;
  vapor_max_ = 0;
}

std::optional<unsigned int> RiceProgram::remaining_time() {
  if (finished_) {
    return 0;
  }

  unsigned int res = 0;

  switch (stage_) {
    case Wait:
      return std::nullopt;

    case Start:
      // TODO: calculate time needed to step up the temperature
      res += 2;
      break;

    case Soak:
      if (!fast_) {
        res += RICE_PROGRAM_SOAK_MINUTES;
      }
      break;

    case Heat:
      if (!fast_) {
        res += 2;
      } else {
        // Guess more time as fast program starts from lower temperature
        res += 4;
      }
      break;

    case Cook:
      res += cooking_time_ / 2;
      break;

    case Vapor:
      res += cooking_time_ / 2;
      break;

    case Rest:
      if (!fast_) {
        res += RICE_PROGRAM_REST_MINUTES;
      }
      break;
  }

  // Subtract elapsed time
  if (stage_started_ > 0) {
    uint32_t elapsed_ms = millis() - stage_started_;
    unsigned int elapsed_min = elapsed_ms / 1000 / 60;
    if (res > elapsed_min) {
      res -= elapsed_min;
    } else {
      res = 0;
    }
  }

  return res;
}

void RiceProgram::set_stage(Stage stage) {
  this->stage_ = stage;
  this->stage_started_ = millis();
}

void RiceProgram::step(Heater *heater) {
  auto now = millis();

  uint8_t bottom_temp = heater->get_bottom_temperature();
  uint8_t top_temp = heater->get_top_temperature();

  uint8_t target;

  switch (this->stage_) {
    case Wait:
      ESP_LOGD(TAG, "Rice: Waiting, Temperature: top: %d°C, bottom: %d°C", top_temp, bottom_temp);
      heater->power_off();
      break;

    case Start:
      target = 60;
      ESP_LOGD(TAG, "Rice: Starting soak, Temperature: top: %d°C, bottom: %d°C, target: %d°C",
               top_temp, bottom_temp, target);
      heater->power_modulate(target, 0);
      if (heater->get_bottom_temperature() >= target) {
        set_stage(Soak);
      }
      break;

    case Soak:
      target = 65;
      ESP_LOGD(TAG, "Rice: Soaking, Temperature: top: %d°C, bottom: %d°C, target: %d°C",
               top_temp, bottom_temp, target);
      heater->power_modulate(target, 5);
      if (fast_ || now > stage_started_ + RICE_PROGRAM_SOAK_MINUTES * 60 * 1000) {
        set_stage(Heat);
      }
      break;

    case Heat:
      target = 95;
      ESP_LOGD(TAG, "Rice: Heating, Temperature: top: %d°C, bottom: %d°C, target: %d°C",
               top_temp, bottom_temp, target);
      heater->power_modulate(target, 2);
      if (heater->get_bottom_temperature() >= target) {
        set_stage(Cook);
      }
      if (now > stage_started_ + 30 * 60 * 1000) {
        // Heating is taking too long, something must be wrong
        ESP_LOGW(TAG, "Rice: Heating timed out after 30 minutes!");
        heater->power_off();
        finished_ = true;
      }
      break;

    case Cook:
      target = cooking_temp_;
      vapor_max_ = std::clamp(top_temp, vapor_max_, static_cast<uint8_t>(100));
      ESP_LOGD(TAG, "Rice: Cooking, Temperature: top: %d°C, bottom: %d°C, target: %d°C",
               top_temp, bottom_temp, target);
      if (top_temp < vapor_max_) {
        heater->power_on();
      }
      heater->power_modulate(target, 1);
      if (now > stage_started_ + this->cooking_time_ / 2 * 60 * 1000) {
        heater->power_on();
        set_stage(Vapor);
      }
      break;

    case Vapor:
      // Temperature curve from cooking_temp to 120°C in cooking_time/2 minutes
      target = cooking_temp_ + (120 - cooking_temp_) *
                                    (now - this->stage_started_) / 1000 / 60 /
                                    (this->cooking_time_ / 2);
      ESP_LOGD(TAG, "Rice: Vapor, Temperature: top: %d°C, bottom: %d°C, target: %d°C",
               top_temp, bottom_temp, target);
      heater->power_modulate(target, 0);
      if (now > stage_started_ + this->cooking_time_ / 2 * 60 * 1000) {
        heater->power_off();
        set_stage(Rest);
      }
      break;

    case Rest:
      target = 65;
      ESP_LOGD(TAG, "Rice: Rest, Temperature: top: %d°C, bottom: %d°C, target: %d°C",
               top_temp, bottom_temp, target);
      heater->power_modulate(target, 4);
      if (fast_ || now > stage_started_ + RICE_PROGRAM_REST_MINUTES * 60 * 1000) {
        heater->power_off();
        finished_ = true;
      }
      break;
  }
}

}  // namespace esphome::ricecooker