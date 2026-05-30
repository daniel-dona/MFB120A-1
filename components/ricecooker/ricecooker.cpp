#include "ricecooker.h"

#include "esphome/core/log.h"

namespace esphome::ricecooker {

// ============================================================================
// RiceCooker
// ============================================================================

void RiceCooker::setup() {
  // Initialize MCU communicator with our UART device
  mcu_communicator_.set_uart_device(this);
  ESP_LOGCONFIG(TAG, "Rice Cooker initialized");
}

void RiceCooker::loop() {
  // Update MCU communication (handles init state machine and periodic send/receive)
  mcu_communicator_.loop();

  // Don't process data until MCU is initialized
  if (!mcu_communicator_.is_initialized()) {
    return;
  }

  // Update heater with latest temperature data
  uint8_t top_temp = mcu_communicator_.get_top_temperature();
  uint8_t bottom_temp = mcu_communicator_.get_bottom_temperature();
  heater_.update(top_temp, bottom_temp);

  // Periodic processing every relay_interval
  if (millis() > relay_last_ + relay_interval_) {
    relay_last_ = millis();

    // Publish sensor data
    if (sensor_top_ != nullptr) {
      sensor_top_->publish_state(top_temp);
    }
    if (sensor_bottom_ != nullptr) {
      sensor_bottom_->publish_state(bottom_temp);
    }

    // Step the current program
    if (current_program_ != nullptr) {
      current_program_->step(&heater_);
      heater_.step(millis());

      // Auto-transition to Keep Warm when a program finishes
      auto remaining = current_program_->remaining_time();
      if (remaining.has_value() && *remaining <= 0) {
        heater_.power_off();
        select_program(&keep_warm_);
        current_program_->start();
      }
    } else {
      ESP_LOGVV(TAG, "No program selected");
    }
  }

  // Update display time based on program or temperature
  if (current_program_ != nullptr) {
    auto remaining = current_program_->remaining_time();
    if (remaining.has_value()) {
      hours_ = *remaining / 60;
      minutes_ = *remaining % 60;
    } else {
      hours_ = top_temp;
      minutes_ = bottom_temp;
    }
  } else {
    hours_ = top_temp;
    minutes_ = bottom_temp;
  }

  // Update MCU display
  mcu_communicator_.set_time(hours_, minutes_);
  mcu_communicator_.set_power(heater_.get_power());
}

void RiceCooker::dump_config() {
  ESP_LOGCONFIG(TAG, "Rice Cooker:");
  ESP_LOGCONFIG(TAG, "  Keep Warm Temperature: %d°C", keep_warm_.get_target_temperature());
  ESP_LOGCONFIG(TAG, "  Keep Warm Hysteresis: %d°C", keep_warm_.get_hysteresis());
  ESP_LOGCONFIG(TAG, "  Rice Cooking Time: %d min", rice_program_.get_cooking_time());
  LOG_SENSOR("  ", "Top Temperature", sensor_top_);
  LOG_SENSOR("  ", "Bottom Temperature", sensor_bottom_);
}

// --- Control methods ---

void RiceCooker::power_on() {
  heater_.power_on();
  mcu_communicator_.set_power(true);
  if (power_switch_ != nullptr) {
    power_switch_->publish_state(true);
  }
}

void RiceCooker::power_off() {
  heater_.power_off();
  mcu_communicator_.set_power(false);
  if (power_switch_ != nullptr) {
    power_switch_->publish_state(false);
  }
}

bool RiceCooker::get_power() { return heater_.get_power(); }

uint8_t RiceCooker::get_top_temperature() { return mcu_communicator_.get_top_temperature(); }

uint8_t RiceCooker::get_bottom_temperature() { return mcu_communicator_.get_bottom_temperature(); }

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
  }
}

void RiceCooker::cancel() {
  heater_.reset();
  if (current_program_ != nullptr) {
    current_program_->cancel();
  }
}

void RiceCooker::set_program_by_name(const std::string &name) {
  Program *program = nullptr;
  if (name == KEEP_WARM_NAME) {
    program = &keep_warm_;
  } else if (name == RICE_NAME) {
    program = &rice_program_;
  } else if (name == FAST_RICE_NAME) {
    program = &fast_rice_program_;
  } else if (name == NONE_NAME) {
    program = nullptr;
  } else {
    ESP_LOGW(TAG, "Unknown program: %s", name.c_str());
    return;
  }
  select_program(program);
}

void RiceCooker::select_program(Program *program) {
  // Cancel the current program
  if (current_program_ != nullptr) {
    current_program_->cancel();
  }

  // Reset heater state for new program
  heater_.reset();

  current_program_ = program;

  ESP_LOGD(TAG, "Selected program: %s", program ? program->get_name() : NONE_NAME);

  // Update the select entity to reflect the current program
  if (program_select_ != nullptr) {
    program_select_->publish_state(program ? program->get_name() : NONE_NAME);
  }

  // Start the new program
  if (program != nullptr) {
    program->start();
  }
}

// ============================================================================
// RiceCookerPowerSwitch
// ============================================================================

void RiceCookerPowerSwitch::write_state(bool state) {
  if (state) {
    ricecooker_->power_on();
  } else {
    ricecooker_->power_off();
  }
  this->publish_state(state);
}

void RiceCookerPowerSwitch::dump_config() { ESP_LOGCONFIG(TAG, "Rice Cooker Power Switch"); }

// ============================================================================
// RiceCookerProgramSelect
// ============================================================================

void RiceCookerProgramSelect::setup() {
  this->traits.set_options({NONE_NAME, KEEP_WARM_NAME, RICE_NAME, FAST_RICE_NAME});
  this->publish_state(NONE_NAME);
}

void RiceCookerProgramSelect::control(const std::string &value) { ricecooker_->set_program_by_name(value); }

void RiceCookerProgramSelect::dump_config() { ESP_LOGCONFIG(TAG, "Rice Cooker Program Select"); }

}  // namespace esphome::ricecooker