#include "program.h"
#include "heater.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "ricecooker_program";

// Safety thresholds (matching original firmware)
static const uint8_t EMERGENCY_MAX_PLATE = 130;    // °C, absolute max (firmware S3)
static const uint8_t HOLD_MAX_PLATE = 104;        // °C, keep-warm max (firmware E1)
static const uint8_t HOLD_MAX_LID = 84;           // °C, keep-warm max (firmware E2)

void ProfileProgram::add_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms,
                                bool hold, uint8_t top_threshold, uint8_t bottom_threshold, uint8_t power) {
  StageType type;
  if (hold) {
    type = StageType::INFINITE_HOLD;
  } else if (duration_ms > 0) {
    type = StageType::TIMED_HOLD;
  } else {
    type = StageType::TRANSITIONAL;
  }

  stages_.push_back({type, target_temperature, hysteresis, duration_ms,
                     top_threshold, bottom_threshold, power});
  ESP_LOGD(TAG, "Program '%s': added stage %zu — target: %d°C, hyst: %d, dur: %ums, "
           "hold: %s, top_thresh: %d, bot_thresh: %d, power: %d",
           name_.c_str(), stages_.size(), target_temperature, hysteresis, duration_ms,
           hold ? "true" : "false", top_threshold, bottom_threshold, power);
}

void ProfileProgram::start() {
  current_stage_ = 0;
  target_reached_ = false;
  stage_started_ms_ = 0;
  running_ = true;
  finished_ = false;
  emergency_shutdown_ = false;
  ESP_LOGI(TAG, "Program '%s': started (%zu stages)", name_.c_str(), stages_.size());
}

void ProfileProgram::cancel() {
  current_stage_ = 0;
  target_reached_ = false;
  stage_started_ms_ = 0;
  running_ = false;
  finished_ = true;
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
  // Also apply hold limits during auto keep-warm (past all stages)
  if (current_stage_ >= stages_.size() && keep_warm_after_) {
    if (plate_temp > HOLD_MAX_PLATE) {
      ESP_LOGE(TAG, "EMERGENCY: plate temp %d°C > %d°C during keep-warm, shutting down!", plate_temp, HOLD_MAX_PLATE);
      do_emergency_shutdown(heater);
      return;
    }
    if (lid_temp > HOLD_MAX_LID) {
      ESP_LOGE(TAG, "EMERGENCY: lid temp %d°C > %d°C during keep-warm, shutting down!", lid_temp, HOLD_MAX_LID);
      do_emergency_shutdown(heater);
      return;
    }
  }

  // Past all stages?
  if (current_stage_ >= stages_.size()) {
    if (keep_warm_after_) {
      // Auto keep-warm: use the K1-K8 bang-bang algorithm
      uint8_t target = keep_warm_temp_;
      uint8_t hyst = keep_warm_hyst_;
      // K1-K8: if plate < (target - hyst*2): power=8, else if plate < target: power=6,
      // else if plate == target: power=3, else: power=0
      if (plate_temp < target - hyst * 2) {
        heater->set_power_level(8);
      } else if (plate_temp < target - hyst) {
        heater->set_power_level(6);
      } else if (plate_temp == target) {
        heater->set_power_level(3);
      } else if (plate_temp > target) {
        heater->set_power_level(0);
      } else {
        heater->set_power_level(4);
      }
      heater->power_modulate(target, hyst);
      heater->step(millis());
      return;
    } else {
      heater->power_off();
      finished_ = true;
      running_ = false;
      ESP_LOGI(TAG, "Program '%s': finished (no keep-warm)", name_.c_str());
      return;
    }
  }

  const Stage &stage = stages_[current_stage_];

  // Apply heater control for current stage
  apply_stage_control_(heater, stage);

  // Check whether we should advance
  if (should_advance_(heater, stage)) {
    advance_stage();
  }

  heater->step(millis());
}

void ProfileProgram::apply_stage_control_(Heater *heater, const Stage &stage) {
  // Set target temperature and hysteresis for bang-bang control
  heater->power_modulate(stage.target_temperature, stage.hysteresis);

  // Set power level if specified (255=auto, let heater decide)
  if (stage.power != 255) {
    heater->set_power_level(stage.power);
  } else {
    // Auto: for transitional stages use max power, for hold stages use moderate
    heater->set_power_level(stage.type == StageType::TRANSITIONAL ? 28 : 8);
  }
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
      // Advance when timer expires (only after target first reached)
      if (target_reached_ && stage.duration_ms > 0) {
        if (millis() - stage_started_ms_ >= stage.duration_ms) {
          return true;
        }
      }
      // Also advance if explicit thresholds are exceeded (safety timeout)
      if (stage.bottom_threshold > 0 && plate >= stage.bottom_threshold) {
        return true;
      }
      if (stage.top_threshold > 0 && lid >= stage.top_threshold) {
        return true;
      }
      return false;

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
    if (keep_warm_after_) {
      ESP_LOGI(TAG, "Program '%s': all stages done, entering auto keep-warm at %d°C",
               name_.c_str(), keep_warm_temp_);
    } else {
      ESP_LOGI(TAG, "Program '%s': finished all %zu stages", name_.c_str(), stages_.size());
    }
    return;
  }

  const Stage &s = stages_[current_stage_];
  const char *type_name = "";
  switch (s.type) {
    case StageType::TRANSITIONAL: type_name = "TRANSITIONAL"; break;
    case StageType::TIMED_HOLD: type_name = "TIMED_HOLD"; break;
    case StageType::INFINITE_HOLD: type_name = "INFINITE_HOLD"; break;
  }
  ESP_LOGI(TAG, "Program '%s': stage %zu/%zu %s target=%d°C hyst=%d°C dur=%ums "
           "top_thresh=%d bot_thresh=%d pwr=%d",
           name_.c_str(), current_stage_ + 1, stages_.size(),
           type_name, s.target_temperature, s.hysteresis, s.duration_ms,
           s.top_threshold, s.bottom_threshold, s.power);
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
  if (current_stage_ >= stages_.size()) {
    return keep_warm_after_;  // Auto keep-warm phase
  }
  return stages_[current_stage_].type == StageType::INFINITE_HOLD;
}

std::optional<uint32_t> ProfileProgram::remaining_time_seconds() const {
  if (finished_ || !running_) {
    return 0;
  }

  // Past all stages and in keep-warm -> infinite
  if (current_stage_ >= stages_.size()) {
    return keep_warm_after_ ? std::nullopt : std::optional<uint32_t>(0);
  }

  uint32_t total_remaining = 0;

  for (size_t i = current_stage_; i < stages_.size(); i++) {
    const Stage &s = stages_[i];
    if (s.type == StageType::INFINITE_HOLD) {
      // Infinite hold from this point: we can only report time until we get here
      if (total_remaining == 0) {
        return std::nullopt;  // Already in infinite hold
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
    if (cur.type == StageType::TIMED_HOLD && target_reached_ && stage_started_ms_ > 0) {
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

}  // namespace esphome::ricecooker