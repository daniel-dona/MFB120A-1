#include "program.h"
#include "heater.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "ricecooker_program";

// Safety thresholds (matching original firmware)
static const uint8_t EMERGENCY_MAX_PLATE = 180;    // °C, absolute max (firmware S3)
static const uint8_t HOLD_MAX_PLATE = 150;        // °C, keep-warm max (firmware E1)
static const uint8_t HOLD_MAX_LID = 84;           // °C, keep-warm max (firmware E2)

void ProfileProgram::add_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms,
                                bool hold, uint8_t top_threshold, uint8_t bottom_threshold, uint8_t power,
                                uint8_t start_temperature, uint8_t stage_type, RampCurve curve,
                                const char *description) {
  StageType type = static_cast<StageType>(stage_type);

  stages_.push_back({type, curve, target_temperature, start_temperature, hysteresis, duration_ms,
                     top_threshold, bottom_threshold, power, description});
  ESP_LOGD(TAG, "Program '%s': stage %zu type=%d to=%d°C from=%d°C dur=%ums curve=%d desc='%s'",
           name_.c_str(), stages_.size(), (int)type, target_temperature, start_temperature,
           duration_ms, (int)curve, description ? description : "");
}

void ProfileProgram::start() {
  current_stage_ = 0;
  target_reached_ = false;
  stage_started_ms_ = 0;
  running_ = true;
  finished_ = false;
  waiting_user_ = false;
  emergency_shutdown_ = false;
  ESP_LOGI(TAG, "Program '%s': started (%zu stages)", name_.c_str(), stages_.size());
}

void ProfileProgram::cancel() {
  current_stage_ = 0;
  target_reached_ = false;
  stage_started_ms_ = 0;
  running_ = false;
  finished_ = true;
  waiting_user_ = false;
  emergency_shutdown_ = false;
  ESP_LOGI(TAG, "Program '%s': cancelled", name_.c_str());
}

void ProfileProgram::step(Heater *heater) {
  if (finished_ || !running_) {
    heater->power_off();
    return;
  }

  // Safety checks first — these MUST always run regardless of stage
  uint8_t plate_temp = heater->get_bottom_temperature();
  uint8_t lid_temp = heater->get_top_temperature();

  // S3: General emergency shutoff if plate exceeds absolute max (any mode)
  if (plate_temp > EMERGENCY_MAX_PLATE) {
    ESP_LOGE(TAG, "EMERGENCY: plate temp %d°C > %d°C, shutting down!", plate_temp, EMERGENCY_MAX_PLATE);
    do_emergency_shutdown(heater);
    return;
  }

  // E1 + E2: Emergency shutoff during hold/keep-warm stages
  if (current_stage_ < stages_.size()) {
    const Stage &s = stages_[current_stage_];
    if (s.type == StageType::INFINITE_HOLD) {
      if (plate_temp > HOLD_MAX_PLATE) {
        ESP_LOGE(TAG, "EMERGENCY: plate temp %d°C > %d°C during hold, shutting down!", plate_temp, HOLD_MAX_PLATE);
        do_emergency_shutdown(heater);
        return;
      }
      if (lid_temp > HOLD_MAX_LID) {
        ESP_LOGE(TAG, "EMERGENCY: lid temp %d°C > %d°C during hold, shutting down!", lid_temp, HOLD_MAX_LID);
        do_emergency_shutdown(heater);
        return;
      }
    }
  }

  // Past all stages? Program is finished.
  if (current_stage_ >= stages_.size()) {
    heater->power_off();
    finished_ = true;
    running_ = false;
    return;
  }

  const Stage &stage = stages_[current_stage_];

  // USER_ACTION: block until user confirms
  if (stage.type == StageType::USER_ACTION) {
    if (!waiting_user_) {
      waiting_user_ = true;
      ESP_LOGI(TAG, "Program '%s': stage %zu/%zu USER_ACTION — waiting for user: '%s'",
               name_.c_str(), current_stage_ + 1, stages_.size(),
               stage.description ? stage.description : "");
    }
    // Still apply heater control (e.g. maintain temp while waiting)
    apply_stage_control_(heater, stage);
    heater->step(millis());
    return;  // Do NOT check should_advance_
  }

  // NOTIFY: show message and continue (optionally after a duration)
  if (stage.type == StageType::NOTIFY) {
    if (stage_started_ms_ == 0) {
      stage_started_ms_ = millis();
      ESP_LOGI(TAG, "Program '%s': stage %zu/%zu NOTIFY: '%s'",
               name_.c_str(), current_stage_ + 1, stages_.size(),
               stage.description ? stage.description : "");
    }
    apply_stage_control_(heater, stage);
    if (should_advance_(heater, stage)) {
      advance_stage();
    }
    heater->step(millis());
    return;
  }

  // Initialize stage timer on first step of a new stage
  if (stage_started_ms_ == 0) {
    stage_started_ms_ = millis();
    if (stage.duration_ms > 0) {
      ESP_LOGI(TAG, "Program '%s': stage %zu/%zu started (%s, %u°C, duration %us)",
               name_.c_str(), current_stage_ + 1, stages_.size(),
               current_stage_type_name(), stage.target_temperature,
               stage.duration_ms / 1000);
    } else {
      ESP_LOGI(TAG, "Program '%s': stage %zu/%zu started (%s, %u°C, no duration)",
               name_.c_str(), current_stage_ + 1, stages_.size(),
               current_stage_type_name(), stage.target_temperature);
    }
  }

  // Apply heater control for current stage
  apply_stage_control_(heater, stage);

  // Check whether we should advance
  if (should_advance_(heater, stage)) {
    advance_stage();
  }

  heater->step(millis());
}

void ProfileProgram::confirm() {
  if (!running_ || finished_) return;
  if (current_stage_ >= stages_.size()) return;
  if (stages_[current_stage_].type != StageType::USER_ACTION) return;
  if (!waiting_user_) return;

  ESP_LOGI(TAG, "Program '%s': user confirmed action at stage %zu/%zu",
           name_.c_str(), current_stage_ + 1, stages_.size());
  waiting_user_ = false;
  advance_stage();
}

void ProfileProgram::apply_stage_control_(Heater *heater, const Stage &stage) {
  uint8_t effective_target = stage.target_temperature;

  // RAMP: interpolate from→to over duration using selected curve
  if (stage.type == StageType::RAMP && stage.duration_ms > 0) {
    uint32_t elapsed = millis() - stage_started_ms_;
    uint8_t from = stage.start_temperature;
    if (from == 0 && current_stage_ > 0) {
      from = stages_[current_stage_ - 1].target_temperature;
    }
    int32_t range = (int32_t)stage.target_temperature - (int32_t)from;

    if (elapsed >= stage.duration_ms) {
      effective_target = stage.target_temperature;
    } else {
      // t = 0.0 to 1.0
      float t = (float)elapsed / (float)stage.duration_ms;
      switch (stage.curve) {
        case RampCurve::EASE_IN:
          t = t * t;  // quadratic ease-in
          break;
        case RampCurve::EASE_OUT:
          t = 1.0f - (1.0f - t) * (1.0f - t);  // quadratic ease-out
          break;
        case RampCurve::STEP:
          t = t < 0.5f ? 0.0f : 1.0f;
          break;
        default:  // LINEAR
          break;
      }
      effective_target = from + (int32_t)(range * t);
    }
  }

  heater->power_modulate(effective_target, stage.hysteresis);

  uint8_t pwr = stage.power;
  if (pwr == 255) {
    pwr = (stage.type == StageType::TRANSITIONAL) ? 28 : 8;
  }
  heater->set_power_level(pwr);
}

bool ProfileProgram::should_advance_(Heater *heater, const Stage &stage) const {
  uint8_t plate = heater->get_bottom_temperature();
  uint8_t lid = heater->get_top_temperature();

  switch (stage.type) {
    case StageType::TRANSITIONAL:
      // Advance when bottom temperature reaches target
      if (plate >= stage.target_temperature) {
        return true;
      }
      // Also advance if explicit thresholds are exceeded
      if (stage.bottom_threshold > 0 && plate >= stage.bottom_threshold) {
        return true;
      }
      if (stage.top_threshold > 0 && lid >= stage.top_threshold) {
        return true;
      }
      return false;

    case StageType::TIMED_HOLD:
    case StageType::RAMP:
      // Advance when timer expires
      if (stage.duration_ms > 0) {
        if (millis() - stage_started_ms_ >= stage.duration_ms) {
          return true;
        }
      }
      // Also advance if explicit thresholds are exceeded
      if (stage.bottom_threshold > 0 && plate >= stage.bottom_threshold) {
        return true;
      }
      if (stage.top_threshold > 0 && lid >= stage.top_threshold) {
        return true;
      }
      return false;

    case StageType::USER_ACTION:
      // USER_ACTION advances only via confirm(), never via should_advance_
      return false;

    case StageType::NOTIFY:
      // NOTIFY with duration: advance when timer expires
      if (stage.duration_ms > 0) {
        if (millis() - stage_started_ms_ >= stage.duration_ms) {
          return true;
        }
      }
      // NOTIFY without duration: advance immediately
      return true;

    case StageType::INFINITE_HOLD:
      // Never advance from infinite hold
      return false;
  }
  return false;
}

void ProfileProgram::advance_stage() {
  current_stage_++;
  target_reached_ = false;
  stage_started_ms_ = 0;

  if (current_stage_ >= stages_.size()) {
    ESP_LOGI(TAG, "Program '%s': finished all %zu stages", name_.c_str(), stages_.size());
    return;
  }

  const Stage &s = stages_[current_stage_];
  const char *type_name = "";
  switch (s.type) {
    case StageType::TRANSITIONAL: type_name = "TRANSITIONAL"; break;
    case StageType::TIMED_HOLD: type_name = "TIMED_HOLD"; break;
    case StageType::INFINITE_HOLD: type_name = "INFINITE_HOLD"; break;
    case StageType::USER_ACTION: type_name = "USER_ACTION"; break;
    case StageType::NOTIFY: type_name = "NOTIFY"; break;
  }
  ESP_LOGI(TAG, "Program '%s': stage %zu/%zu %s target=%d°C hyst=%d°C dur=%ums "
           "top_thresh=%d bot_thresh=%d pwr=%d desc='%s'",
           name_.c_str(), current_stage_ + 1, stages_.size(),
           type_name, s.target_temperature, s.hysteresis, s.duration_ms,
           s.top_threshold, s.bottom_threshold, s.power,
           s.description ? s.description : "");
}

void ProfileProgram::do_emergency_shutdown(Heater *heater) {
  heater->power_off();
  heater->set_power_level(0);
  emergency_shutdown_ = true;
  finished_ = true;
  running_ = false;
  ESP_LOGE(TAG, "Program '%s': EMERGENCY SHUTDOWN", name_.c_str());
}

bool ProfileProgram::is_keep_warm() const {
  if (!running_ || finished_) {
    return false;
  }
  if (current_stage_ < stages_.size()) {
    return stages_[current_stage_].type == StageType::INFINITE_HOLD;
  }
  return false;
}

std::optional<uint32_t> ProfileProgram::remaining_time_seconds() const {
  if (finished_ || !running_) {
    return 0;
  }

  if (current_stage_ >= stages_.size()) {
    return std::optional<uint32_t>(0);  // Finished
  }

  uint32_t total_remaining = 0;

  for (size_t i = current_stage_; i < stages_.size(); i++) {
    const Stage &s = stages_[i];
    if (s.type == StageType::INFINITE_HOLD || s.type == StageType::USER_ACTION) {
      // Infinite hold or user action from this point: unknown remaining time
      if (total_remaining == 0) {
        return std::nullopt;  // Already in infinite hold or waiting for user
      }
      break;  // Report only the timed portion
    }
    if (s.duration_ms > 0) {
      total_remaining += s.duration_ms / 1000;
    } else if (s.type == StageType::TRANSITIONAL) {
      total_remaining += 120;  // Estimate ~2 min per transitional stage
    }
  }

  // Subtract elapsed time from the current stage
  if (current_stage_ < stages_.size()) {
    const Stage &cur = stages_[current_stage_];
    if ((cur.type == StageType::TIMED_HOLD || cur.type == StageType::RAMP) && stage_started_ms_ > 0) {
      uint32_t elapsed_s = (millis() - stage_started_ms_) / 1000;
      if (elapsed_s < total_remaining) {
        total_remaining -= elapsed_s;
      } else {
        total_remaining = 0;
      }
    }
  }

  return total_remaining;
}

std::optional<uint32_t> ProfileProgram::current_stage_remaining_seconds() const {
  if (finished_ || !running_ || current_stage_ >= stages_.size()) {
    return std::nullopt;
  }

  const Stage &cur = stages_[current_stage_];

  switch (cur.type) {
    case StageType::TIMED_HOLD:
    case StageType::RAMP:
      if (cur.duration_ms > 0 && stage_started_ms_ > 0) {
        uint32_t elapsed_s = (millis() - stage_started_ms_) / 1000;
        uint32_t total_s = cur.duration_ms / 1000;
        return (total_s > elapsed_s) ? (total_s - elapsed_s) : 0;
      }
      if (cur.duration_ms > 0) {
        return cur.duration_ms / 1000;
      }
      return std::nullopt;

    case StageType::INFINITE_HOLD:
    case StageType::USER_ACTION:
      return std::nullopt;  // Indefinite

    case StageType::TRANSITIONAL:
      return std::nullopt;  // No duration

    case StageType::NOTIFY:
      if (cur.duration_ms > 0 && stage_started_ms_ > 0) {
        uint32_t elapsed_s = (millis() - stage_started_ms_) / 1000;
        uint32_t total_s = cur.duration_ms / 1000;
        return (total_s > elapsed_s) ? (total_s - elapsed_s) : 0;
      }
      return std::nullopt;
  }
  return std::nullopt;
}

const char *ProfileProgram::current_stage_type_name() const {
  if (current_stage_ >= stages_.size()) return "--";
  switch (stages_[current_stage_].type) {
    case StageType::TRANSITIONAL: return "Calentando";
    case StageType::TIMED_HOLD:   return "Manteniendo";
    case StageType::RAMP:         return "Rampa";
    case StageType::INFINITE_HOLD: return "Infinito";
    case StageType::USER_ACTION:  return "Esperando";
    case StageType::NOTIFY:       return "Aviso";
  }
  return "?";
}

uint8_t ProfileProgram::current_stage_target() const {
  if (current_stage_ >= stages_.size()) return 0;
  return stages_[current_stage_].target_temperature;
}

const char *ProfileProgram::current_stage_description() const {
  if (current_stage_ >= stages_.size()) return "";
  return stages_[current_stage_].description ? stages_[current_stage_].description : "";
}

}  // namespace esphome::ricecooker