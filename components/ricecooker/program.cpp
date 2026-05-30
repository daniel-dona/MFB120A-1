#include "program.h"
#include "heater.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

static const char *const TAG = "ricecooker";

// ============================================================================
// ProfileProgram
// ============================================================================

void ProfileProgram::add_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms, bool hold) {
  stages_.push_back({target_temperature, hysteresis, duration_ms, hold});
  ESP_LOGD(TAG, "Profile '%s': added stage %zu — target: %d°C, hysteresis: %d, duration: %ums, hold: %s",
            name_.c_str(), stages_.size(), target_temperature, hysteresis, duration_ms, hold ? "true" : "false");
}

void ProfileProgram::start() {
  current_stage_index_ = 0;
  target_reached_ = false;
  stage_started_ = 0;
  started_ = true;
  finished_ = false;
  ESP_LOGI(TAG, "Profile '%s': started", name_.c_str());
}

void ProfileProgram::cancel() {
  current_stage_index_ = 0;
  target_reached_ = false;
  stage_started_ = 0;
  started_ = false;
  finished_ = false;
}

void ProfileProgram::reset() { cancel(); }

void ProfileProgram::advance_stage() {
  current_stage_index_++;
  target_reached_ = false;
  stage_started_ = 0;

  if (current_stage_index_ >= stages_.size()) {
    finished_ = true;
    ESP_LOGI(TAG, "Profile '%s': finished all stages", name_.c_str());
  } else {
    const Stage &s = stages_[current_stage_index_];
    ESP_LOGI(TAG, "Profile '%s': advancing to stage %zu/%zu — target %d°C",
             name_.c_str(), current_stage_index_ + 1, stages_.size(), s.target_temperature);
  }
}

void ProfileProgram::step(Heater *heater) {
  if (finished_ || !started_) {
    heater->power_off();
    return;
  }

  if (stages_.empty() || current_stage_index_ >= stages_.size()) {
    finished_ = true;
    heater->power_off();
    return;
  }

  const Stage &s = stages_[current_stage_index_];

  // Apply power modulation for current stage target
  heater->power_modulate(s.target_temperature, s.hysteresis);

  bool at_target = heater->get_bottom_temperature() >= s.target_temperature - s.hysteresis;

  // Detect when target temperature is first reached
  if (at_target && !target_reached_) {
    target_reached_ = true;
    stage_started_ = millis();
    ESP_LOGD(TAG, "Profile '%s': stage %zu — target %d°C reached (bottom: %d°C)",
             name_.c_str(), current_stage_index_ + 1, s.target_temperature,
             heater->get_bottom_temperature());
  }

  // --- Stage type: Infinite hold ---
  if (s.hold) {
    // Stay in this stage forever until cancelled
    return;
  }

  // --- Stage type: Transitional (duration == 0, advance when temp reached) ---
  if (s.duration_ms == 0) {
    if (at_target) {
      advance_stage();
      return;
    }
    return;  // Not yet at target, keep modulating
  }

  // --- Stage type: Timed hold (duration > 0) ---
  if (target_reached_ && s.duration_ms > 0) {
    if (millis() - stage_started_ >= s.duration_ms) {
      advance_stage();
      return;
    }
  }
  // Either not yet at target, or timer hasn't expired — keep modulating
}

std::optional<unsigned int> ProfileProgram::remaining_time() {
  if (finished_ || !started_) {
    return 0;
  }

  if (stages_.empty() || current_stage_index_ >= stages_.size()) {
    return 0;
  }

  const Stage &current = stages_[current_stage_index_];

  // Hold stages have infinite remaining time
  if (current.hold) {
    return std::nullopt;
  }

  unsigned int total_remaining = 0;

  // Calculate remaining time from current stage onwards
  for (size_t i = current_stage_index_; i < stages_.size(); i++) {
    const Stage &s = stages_[i];

    if (s.hold) {
      // Hold stage encountered: remaining time is infinite from this point
      // Only return what we've counted so far (time until hold begins)
      break;
    }

    if (s.duration_ms > 0) {
      total_remaining += s.duration_ms / 60000;  // ms → minutes
    } else {
      total_remaining += 2;  // Estimate ~2 min to reach target temperature
    }
  }

  // Subtract elapsed time from the current timed stage
  if (target_reached_ && stage_started_ > 0 && current.duration_ms > 0) {
    uint32_t elapsed_ms = millis() - stage_started_;
    unsigned int elapsed_min = elapsed_ms / 1000 / 60;
    if (total_remaining > elapsed_min) {
      total_remaining -= elapsed_min;
    } else {
      total_remaining = 0;
    }
  }

  return total_remaining;
}

}  // namespace esphome::ricecooker