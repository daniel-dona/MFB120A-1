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
      case 0x88: // START — also confirms user actions
        ESP_LOGI(TAG, "Physical button: START");
        if (rc->is_waiting_user()) {
          rc->confirm();
        } else {
          rc->start();
        }
        break;
      case 0x82: // CANCEL
        ESP_LOGI(TAG, "Physical button: CANCEL");
        rc->cancel();
        break;
      case 0x84: // SELECT
        ESP_LOGI(TAG, "Physical button: SELECT");
        {
          auto names = rc->get_program_names();
          const char *cur = rc->get_state_name();
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
}

void RiceCooker::dump_config() {
  ESP_LOGCONFIG(TAG, "Rice Cooker:");
  ESP_LOGCONFIG(TAG, "  Programs: %zu", custom_programs_.size());
  for (auto *prog : custom_programs_) {
    const char *next = prog->next_program();
    ESP_LOGCONFIG(TAG, "    - %s (%zu stages, next: %s)",
                  prog->get_name(), prog->stage_count(),
                  next ? next : "none");
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

  // Step the active program (when RUNNING or WAITING_USER)
  if ((state_ == CookerState::RUNNING || state_ == CookerState::WAITING_USER) && current_program_ != nullptr) {
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

    // Publish current stage remaining time in minutes
    if (sensor_stage_remaining_ != nullptr) {
      auto stage_remaining = current_program_->current_stage_remaining_seconds();
      if (stage_remaining.has_value()) {
        sensor_stage_remaining_->publish_state(*stage_remaining / 60);
      } else {
        sensor_stage_remaining_->publish_state(NAN);
      }
    }

    // Check for RUNNING → WAITING_USER transition
    if (state_ == CookerState::RUNNING && current_program_->is_waiting_user()) {
      state_ = CookerState::WAITING_USER;
      ESP_LOGI(TAG, "Cooker state: WAITING_USER — awaiting user action");
    }

    if (current_program_->is_finished()) {
      ESP_LOGI(TAG, "Program '%s' finished", current_program_->get_name());
      auto *finished = current_program_;
      heater_.power_off();
      heater_.reset();

      if (sensor_remaining_ != nullptr) {
        sensor_remaining_->publish_state(0);
      }

      // Chain to next program if defined — auto-start (no READY pause)
      if (finished->next_program() != nullptr) {
        ESP_LOGI(TAG, "Chaining to program: %s", finished->next_program());
        for (auto *prog : custom_programs_) {
          if (finished->next_program() == std::string(prog->get_name())) {
            select_program(prog);
            current_program_->start();
            state_ = CookerState::RUNNING;
            ESP_LOGI(TAG, "Auto-started chained program: %s", current_program_->get_name());
            break;
          }
        }
      } else {
        current_program_ = nullptr;
        state_ = CookerState::FINISHED;
        ESP_LOGI(TAG, "Cooker state: FINISHED");
        if (program_select_ != nullptr) {
          program_select_->publish_state(FINISHED_NAME);
        }
      }
    }
  } else {
    if (sensor_remaining_ != nullptr) {
      sensor_remaining_->publish_state(NAN);
    }
    if (sensor_stage_remaining_ != nullptr) {
      sensor_stage_remaining_->publish_state(NAN);
    }
  }

  // --- Detect state/stage transitions (used by display flash + HA publishing) ---
  size_t cur_stage = 0;
  bool cur_finished = false;
  if (current_program_ != nullptr) {
    cur_stage = current_program_->current_stage_index();
    cur_finished = current_program_->is_finished();
  }
  bool state_changed = (state_ != prev_state_);
  bool stage_changed = (cur_stage != prev_stage_index_);
  bool finished_changed = (cur_finished != prev_finished_);

  // Update MCU display — rotate info based on state and stage
  // Display format: XX:XX (4 digits + colon + 9 LEDs)
  //
  // States:
  //   IDLE / READY / FINISHED: show base temp, colon OFF, program LED steady
  //   RUNNING: rotate 3 pages with pulsing colon, program LED breathing
  //     Page 0 (4s): time remaining, colon PULSE (slow blink)
  //     Page 1 (2s): base temperature, colon OFF
  //     Page 2 (1s): phase number "P  3", colon OFF
  //   WAITING_USER: blink between target temp and dashes, orange LED blinks
  //
  // Transitions:
  //   Stage change: brief flash pattern (all LEDs sweep, display dashes)
  const Stage *stage_ptr = nullptr;
  if ((state_ == CookerState::RUNNING || state_ == CookerState::WAITING_USER) && current_program_ != nullptr && !current_program_->is_finished()) {
    stage_ptr = &current_program_->stages()[current_program_->current_stage_index()];
  }

  // --- Transition flash (300ms after stage/state change) ---
  if (stage_changed || state_changed) {
    flash_until_ = now + 300;
  }
  bool flashing = (now < flash_until_);

  if (flashing) {
    // Transition flash: show dashes on all 4 digits, sweep LEDs
    mcu_communicator_.set_raw_digits(99, 99, 99, 99);
    mcu_communicator_.set_colon(false);
    // LED sweep: cycle through LEDs during flash
    uint8_t led_step = ((now / 75) % 9) + 1;  // LED1..LED9 cycling
    for (uint8_t i = 1; i <= 8; i++) {
      mcu_communicator_.set_led_status(
        static_cast<MCUCommunicator::LED_ID>(i),
        (i == led_step) ? MCUCommunicator::LED_STATE::ON : MCUCommunicator::LED_STATE::OFF);
    }
    mcu_communicator_.set_led_status(MCUCommunicator::LED_ID::LED9_ORANGE, MCUCommunicator::LED_STATE::OFF);
    mcu_communicator_.set_power(heater_.get_power());
    return;  // Skip normal display during flash
  }

  // --- WAITING_USER: dramatic blink ---
  if (state_ == CookerState::WAITING_USER) {
    bool blink_on = (now / 500) % 2 == 0;  // Fast 500ms blink
    mcu_communicator_.set_colon(blink_on);   // Colon blinks with display
    if (blink_on && stage_ptr != nullptr) {
      // Show target temperature
      uint8_t target = stage_ptr->target_temperature;
      mcu_communicator_.set_time(target / 10, (target % 10) * 10);
    } else {
      mcu_communicator_.set_raw_digits(99, 99, 99, 99);  // "----" dashes
    }
    mcu_communicator_.set_led_status(
      MCUCommunicator::LED_ID::LED9_ORANGE,
      blink_on ? MCUCommunicator::LED_STATE::ON : MCUCommunicator::LED_STATE::OFF);

  // --- RUNNING: 3-page rotation with pulsing colon ---
  } else if (state_ == CookerState::RUNNING && current_program_ != nullptr && !current_program_->is_finished()) {
    // 7-second cycle: 4s time + 2s temp + 1s phase
    uint32_t cycle_pos = (now / 1000) % 7;

    if (cycle_pos < 4) {
      // Page 0: Time remaining — colon PULSES slowly
      auto remaining = current_program_->remaining_time_seconds();
      if (remaining.has_value()) {
        uint32_t total_min = *remaining / 60;
        mcu_communicator_.set_time(total_min / 60, total_min % 60);
        // Slow pulse: colon ON 1.5s, OFF 0.5s during time display
        bool colon_on = ((now / 500) % 4) != 0;  // 75% duty cycle
        mcu_communicator_.set_colon(colon_on);
      } else {
        // No timer (heat/keep stage): show base temp
        mcu_communicator_.set_time(0, bottom_temp);
        mcu_communicator_.set_colon(false);
      }
    } else if (cycle_pos < 6) {
      // Page 1: Base temperature
      mcu_communicator_.set_time(0, bottom_temp);
      mcu_communicator_.set_colon(false);
    } else {
      // Page 2: Phase number — "P  3" format
      uint8_t phase_num = current_program_->current_stage_index() + 1;
      // Digits: P, blank, tens(0), units(phase)
      mcu_communicator_.set_raw_digits(0xFE, 0xFF, phase_num / 10, phase_num % 10);
      mcu_communicator_.set_colon(false);
    }

    // Program LED: slow breathing (1s ON, 1s OFF) to indicate running
    bool led_breathing = (now / 1000) % 2 == 0;
    mcu_communicator_.set_led_status(
      MCUCommunicator::LED_ID::LED9_ORANGE,
      led_breathing ? MCUCommunicator::LED_STATE::ON : MCUCommunicator::LED_STATE::OFF);

  // --- IDLE / READY / FINISHED: base temperature ---
  } else {
    mcu_communicator_.set_time(0, bottom_temp);
    mcu_communicator_.set_colon(false);
    mcu_communicator_.set_led_status(MCUCommunicator::LED_ID::LED9_ORANGE, MCUCommunicator::LED_STATE::OFF);
  }

  mcu_communicator_.set_power(heater_.get_power());

  // Publish HA entities when state or stage changes
  if (state_changed || stage_changed || finished_changed) {
    prev_state_ = state_;
    prev_stage_index_ = cur_stage;
    prev_finished_ = cur_finished;

    if (state_text_sensor_ != nullptr) {
      state_text_sensor_->publish_state(get_state_name());
    }
    if (can_start_sensor_ != nullptr) {
      can_start_sensor_->publish_state(can_start());
    }
    if (can_cancel_sensor_ != nullptr) {
      can_cancel_sensor_->publish_state(can_cancel());
    }

    // Force-publish remaining time on transitions
    if (sensor_remaining_ != nullptr) {
      if ((state_ == CookerState::RUNNING || state_ == CookerState::WAITING_USER) && current_program_ != nullptr) {
        auto remaining = current_program_->remaining_time_seconds();
        sensor_remaining_->publish_state(remaining.has_value() ? (*remaining / 60) : NAN);
      } else {
        sensor_remaining_->publish_state(NAN);
      }
    }
    if (sensor_stage_remaining_ != nullptr) {
      if ((state_ == CookerState::RUNNING || state_ == CookerState::WAITING_USER) && current_program_ != nullptr) {
        auto stage_remaining = current_program_->current_stage_remaining_seconds();
        sensor_stage_remaining_->publish_state(stage_remaining.has_value() ? (*stage_remaining / 60) : NAN);
      } else {
        sensor_stage_remaining_->publish_state(NAN);
      }
    }

    ESP_LOGI(TAG, "Transition: %s stage=%zu/%zu (start=%d, cancel=%d, confirm=%d)",
             get_state_name(), cur_stage + 1, total_stages(), can_start(), can_cancel(), can_confirm());
  }
}

// --- Program creation (called from Python codegen) ---

void RiceCooker::add_custom_program(const std::string &name, const std::string &next, const std::string &description) {
  auto *prog = new ProfileProgram();  // NOLINT: intentionally never freed (app lifetime)
  prog->set_name(name);
  if (!next.empty()) prog->set_next_program(next);
  if (!description.empty()) prog->set_description(description);
  custom_programs_.push_back(prog);
  ESP_LOGD(TAG, "Added program: %s (next: %s, desc: %s)",
           name.c_str(), next.empty() ? "none" : next.c_str(), description.empty() ? "" : description.c_str());
}

void RiceCooker::add_program_stage(uint8_t target_temperature, uint8_t hysteresis,
                                    uint32_t duration_ms, bool hold,
                                    uint8_t top_threshold, uint8_t bottom_threshold,
                                    uint8_t power, uint8_t start_temperature, uint8_t stage_type, uint8_t curve,
                                    const char *description) {
  if (!custom_programs_.empty()) {
    custom_programs_.back()->add_stage(target_temperature, hysteresis, duration_ms,
                                       hold, top_threshold, bottom_threshold, power,
                                       start_temperature, stage_type, static_cast<RampCurve>(curve),
                                       description);
  }
}

// --- Program list ---

std::vector<const char *> RiceCooker::get_program_names() const {
  std::vector<const char *> names;
  for (auto *prog : custom_programs_) {
    names.push_back(prog->get_name());
  }
  return names;
}

// --- State name ---

const char *RiceCooker::get_state_name() {
  switch (state_) {
    case CookerState::IDLE:
      return IDLE_NAME;
    case CookerState::READY:
      return READY_NAME;
    case CookerState::RUNNING:
      return current_program_ != nullptr ? current_program_->get_name() : IDLE_NAME;
    case CookerState::WAITING_USER:
      return WAITING_USER_NAME;
    case CookerState::FINISHED:
      return FINISHED_NAME;
  }
  return IDLE_NAME;  // fallback
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
  state_ = CookerState::IDLE;
}

bool RiceCooker::get_power() { return heater_.get_power(); }

uint8_t RiceCooker::get_top_temperature() { return mcu_communicator_.get_top_temperature(); }
uint8_t RiceCooker::get_bottom_temperature() { return mcu_communicator_.get_bottom_temperature(); }
uint8_t RiceCooker::get_voltage() { return mcu_communicator_.get_voltage(); }

void RiceCooker::set_wifi(bool status) {
  mcu_communicator_.set_led_status(
      MCUCommunicator::LED_ID::LED9_BLUE,
      status ? MCUCommunicator::LED_STATE::ON : MCUCommunicator::LED_STATE::OFF);
}

void RiceCooker::confirm() {
  if (state_ == CookerState::WAITING_USER && current_program_ != nullptr) {
    current_program_->confirm();
    if (!current_program_->is_waiting_user()) {
      state_ = CookerState::RUNNING;
      ESP_LOGI(TAG, "User confirmed — back to RUNNING");
    }
    return;
  }
  ESP_LOGD(TAG, "Confirm ignored (state=%d)", (int)state_);
}

void RiceCooker::start() {
  if (state_ == CookerState::READY && current_program_ != nullptr) {
    // READY → RUNNING: start cooking!
    current_program_->start();
    state_ = CookerState::RUNNING;
    ESP_LOGI(TAG, "Started program: %s", current_program_->get_name());
    return;
  }
  if (state_ == CookerState::FINISHED) {
    // FINISHED → IDLE: acknowledge finished state
    state_ = CookerState::IDLE;
    if (program_select_ != nullptr) {
      program_select_->publish_state(IDLE_NAME);
    }
    ESP_LOGI(TAG, "Acknowledged finished — back to Idle");
    return;
  }
  // No-op in other states
  ESP_LOGD(TAG, "Start ignored (state=%d)", (int)state_);
}

void RiceCooker::cancel() {
  if (state_ == CookerState::IDLE || state_ == CookerState::FINISHED) {
    ESP_LOGD(TAG, "Cancel ignored (state=%d)", (int)state_);
    return;
  }

  if (current_program_ != nullptr) {
    current_program_->cancel();
  }
  heater_.reset();
  current_program_ = nullptr;
  state_ = CookerState::IDLE;
  if (program_select_ != nullptr) {
    program_select_->publish_state(IDLE_NAME);
  }
  ESP_LOGI(TAG, "Cancelled — back to Idle");
}

void RiceCooker::set_program_by_name(const std::string &name) {
  if (name == IDLE_NAME || name == READY_NAME || name == FINISHED_NAME) {
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
  // Cancel any running or waiting program
  if (current_program_ != nullptr && (state_ == CookerState::RUNNING || state_ == CookerState::WAITING_USER)) {
    current_program_->cancel();
  }
  heater_.reset();
  current_program_ = program;

  if (program != nullptr) {
    state_ = CookerState::READY;
    ESP_LOGI(TAG, "Program ready: %s (press Start to begin)", program->get_name());

    // Set mode LED
    for (size_t i = 0; i < custom_programs_.size(); i++) {
      if (custom_programs_[i] == program) {
        mcu_communicator_.set_led_program_index(i);
        break;
      }
    }
  } else {
    state_ = CookerState::IDLE;
    ESP_LOGI(TAG, "Idle — no program selected");
    mcu_communicator_.set_led_program_index(255);
  }

  if (program_select_ != nullptr) {
    program_select_->publish_state(program ? program->get_name() : IDLE_NAME);
  }
}

// ============================================================================
// RiceCookerProgramSelect
// ============================================================================

void RiceCookerProgramSelect::setup() {
  this->publish_state(IDLE_NAME);
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

// --- Program stage info ---

size_t RiceCooker::current_stage_index() const {
  if ((state_ != CookerState::RUNNING && state_ != CookerState::WAITING_USER) || current_program_ == nullptr) return 0;
  return current_program_->current_stage_index() + 1;  // 1-based for display
}

size_t RiceCooker::total_stages() const {
  if (current_program_ == nullptr) return 0;
  return current_program_->total_stages();
}

const char *RiceCooker::current_stage_type_name() const {
  if ((state_ != CookerState::RUNNING && state_ != CookerState::WAITING_USER) || current_program_ == nullptr) return "--";
  return current_program_->current_stage_type_name();
}

uint8_t RiceCooker::current_stage_target() const {
  if ((state_ != CookerState::RUNNING && state_ != CookerState::WAITING_USER) || current_program_ == nullptr) return 0;
  return current_program_->current_stage_target();
}

const char *RiceCooker::current_program_description() const {
  if (current_program_ == nullptr) return "";
  return current_program_->get_description();
}

const char *RiceCooker::current_stage_description() const {
  if ((state_ != CookerState::RUNNING && state_ != CookerState::WAITING_USER) || current_program_ == nullptr) return "";
  return current_program_->current_stage_description();
}

}  // namespace esphome::ricecooker