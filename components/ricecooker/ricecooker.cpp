#include "ricecooker.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome::ricecooker {

// ============================================================================
// RiceCooker — Main component
// ============================================================================

void RiceCooker::setup() {
  mcu_communicator_.set_uart_device(this);

  // Wire up physical button callbacks
  mcu_communicator_.set_button_callback([](uint8_t cmd, void *arg) {
    auto *rc = static_cast<RiceCooker *>(arg);
    switch (cmd) {
      case 0x88: // START
        ESP_LOGI(TAG, "Physical button: START");
        rc->start();
        break;
      case 0x82: // CANCEL
        ESP_LOGI(TAG, "Physical button: CANCEL");
        rc->cancel();
        break;
      case 0x84: // SELECT
        ESP_LOGI(TAG, "Physical button: SELECT");
        {
          auto names = rc->get_program_names();
          const char *cur = rc->get_program_name();
          int idx = -1;
          for (size_t i = 0; i < names.size(); i++) {
            if (strcmp(names[i], cur) == 0) { idx = (int)i; break; }
          }
          idx = (idx + 1) % (int)names.size();
          rc->set_program_by_name(names[idx]);
        }
        break;
      case 0x81:
        ESP_LOGI(TAG, "Physical button: TIMER");
        break;
    }
  }, this);

  // Append auto keep-warm stages to programs
  for (auto *prog : custom_programs_) {
    if (prog->keep_warm_after()) {
      prog->add_stage(keep_warm_temperature_, keep_warm_hysteresis_,
                      0, true, 0, 0, 255);
    }
  }
}

void RiceCooker::dump_config() {
  ESP_LOGCONFIG(TAG, "Rice Cooker:");
  ESP_LOGCONFIG(TAG, "  Keep Warm Temperature: %d°C", keep_warm_temperature_);
  ESP_LOGCONFIG(TAG, "  Keep Warm Hysteresis: ±%d°C", keep_warm_hysteresis_);
  ESP_LOGCONFIG(TAG, "  Programs: %zu", custom_programs_.size());
  for (auto *prog : custom_programs_) {
    ESP_LOGCONFIG(TAG, "    - %s (%zu stages, keep_warm: %s)",
                  prog->get_name(), prog->stage_count(),
                  prog->keep_warm_after() ? "yes" : "no");
  }
}

void RiceCooker::loop() {
  mcu_communicator_.loop();

  if (!mcu_communicator_.is_initialized()) {
    return;
  }

  const uint32_t now = millis();
  if (now - loop_last_ < LOOP_INTERVAL_MS) {
    return;
  }
  loop_last_ = now;

  uint8_t top_temp = mcu_communicator_.get_top_temperature();
  uint8_t bottom_temp = mcu_communicator_.get_bottom_temperature();
  uint8_t voltage = mcu_communicator_.get_voltage();

  heater_.update(top_temp, bottom_temp);

  // Publish sensor values
  if (sensor_top_ != nullptr) {
    sensor_top_->publish_state(top_temp);
  }
  if (sensor_bottom_ != nullptr) {
    sensor_bottom_->publish_state(bottom_temp);
  }
  if (sensor_voltage_ != nullptr) {
    sensor_voltage_->publish_state(voltage * 1.1837f);
  }

  // Step the active program
  if (current_program_ != nullptr) {
    current_program_->step(&heater_);

    // Publish remaining time in minutes
    if (sensor_remaining_ != nullptr) {
      auto remaining = current_program_->remaining_time_seconds();
      if (remaining.has_value()) {
        sensor_remaining_->publish_state(*remaining / 60);
      } else {
        sensor_remaining_->publish_state(NAN);
      }
    }

    if (current_program_->is_finished()) {
      ESP_LOGI(TAG, "Program '%s' finished", current_program_->get_name());
      heater_.power_off();
      heater_.reset();
      current_program_ = nullptr;
      if (sensor_remaining_ != nullptr) {
        sensor_remaining_->publish_state(0);
      }
    }
  } else {
    if (sensor_remaining_ != nullptr) {
      sensor_remaining_->publish_state(NAN);
    }
  }

  // Update MCU display with remaining time or temperature
  if (current_program_ != nullptr && !current_program_->is_finished()) {
    auto remaining = current_program_->remaining_time_seconds();
    if (remaining.has_value()) {
      uint32_t total_min = *remaining / 60;
      hours_ = total_min / 60;
      minutes_ = total_min % 60;
    } else {
      // Infinite hold (keep-warm) — show dashes
      hours_ = 99;
      minutes_ = 99;
    }
  } else {
    // No program — show plate temp on display (for diagnostics)
    hours_ = bottom_temp;
    minutes_ = top_temp;
  }

  mcu_communicator_.set_time(hours_, minutes_);
  mcu_communicator_.set_power(heater_.get_power());
}

// --- Program creation (called from Python codegen) ---

void RiceCooker::add_custom_program(const std::string &name, bool keep_warm_after) {
  auto *prog = new ProfileProgram();  // NOLINT: intentionally never freed (app lifetime)
  prog->set_name(name);
  prog->set_keep_warm_after(keep_warm_after);
  custom_programs_.push_back(prog);
  ESP_LOGD(TAG, "Added program: %s (keep_warm_after: %s)",
           name.c_str(), keep_warm_after ? "true" : "false");
}

void RiceCooker::add_program_stage(uint8_t target_temperature, uint8_t hysteresis,
                                    uint32_t duration_ms, bool hold,
                                    uint8_t top_threshold, uint8_t bottom_threshold,
                                    uint8_t power) {
  if (!custom_programs_.empty()) {
    custom_programs_.back()->add_stage(target_temperature, hysteresis, duration_ms,
                                       hold, top_threshold, bottom_threshold, power);
  }
}

// --- Program list ---

std::vector<const char *> RiceCooker::get_program_names() const {
  std::vector<const char *> names = {NONE_NAME};
  for (auto *prog : custom_programs_) {
    names.push_back(prog->get_name());
  }
  return names;
}

// --- Control methods ---

void RiceCooker::power_on() {
  heater_.power_on();
  mcu_communicator_.set_power(true);
}

void RiceCooker::power_off() {
  heater_.power_off();
  heater_.reset();
  mcu_communicator_.set_power(false);
  current_program_ = nullptr;
}

bool RiceCooker::get_power() { return heater_.get_power(); }

uint8_t RiceCooker::get_top_temperature() { return mcu_communicator_.get_top_temperature(); }
uint8_t RiceCooker::get_bottom_temperature() { return mcu_communicator_.get_bottom_temperature(); }
uint8_t RiceCooker::get_voltage() { return mcu_communicator_.get_voltage(); }

const char *RiceCooker::get_program_name() {
  return current_program_ != nullptr ? current_program_->get_name() : NONE_NAME;
}

void RiceCooker::set_wifi(bool status) {
  mcu_communicator_.set_led_status(
      MCUCommunicator::LED_ID::LED9_BLUE,
      status ? MCUCommunicator::LED_STATE::ON : MCUCommunicator::LED_STATE::OFF);
}

void RiceCooker::start() {
  if (current_program_ != nullptr) {
    current_program_->start();
    ESP_LOGI(TAG, "Started program: %s", current_program_->get_name());
  }
}

void RiceCooker::cancel() {
  if (current_program_ != nullptr) {
    current_program_->cancel();
  }
  heater_.reset();
  current_program_ = nullptr;
}

void RiceCooker::set_program_by_name(const std::string &name) {
  if (name == NONE_NAME) {
    select_program(nullptr);
    return;
  }
  for (auto *prog : custom_programs_) {
    if (name == prog->get_name()) {
      select_program(prog);
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown program: %s", name.c_str());
}

void RiceCooker::select_program(ProfileProgram *program) {
  if (current_program_ != nullptr) {
    current_program_->cancel();
  }
  heater_.reset();
  current_program_ = program;

  if (program != nullptr) {
    ESP_LOGI(TAG, "Selected program: %s", program->get_name());
    program->start();

    // Set mode LED
    for (size_t i = 0; i < custom_programs_.size(); i++) {
      if (custom_programs_[i] == program) {
        mcu_communicator_.set_led_program_index(i);
        break;
      }
    }
  } else {
    ESP_LOGI(TAG, "No program selected");
    // Clear LEDs: set to out-of-range to turn all off
    mcu_communicator_.set_led_program_index(255);
  }

  if (program_select_ != nullptr) {
    program_select_->publish_state(program ? program->get_name() : NONE_NAME);
  }
}

// ============================================================================
// RiceCookerProgramSelect
// ============================================================================

void RiceCookerProgramSelect::setup() {
  this->publish_state(NONE_NAME);
}

void RiceCookerProgramSelect::control(const std::string &value) {
  ricecooker_->set_program_by_name(value);
}

void RiceCookerProgramSelect::dump_config() {
  ESP_LOGCONFIG(TAG, "Rice Cooker Program Select");
  auto names = ricecooker_->get_program_names();
  for (const char *name : names) {
    ESP_LOGCONFIG(TAG, "  Option: %s", name);
  }
}

}  // namespace esphome::ricecooker