#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"

#include "program.h"
#include "heater.h"
#include "mcu_communicator.h"

namespace esphome::ricecooker {

class RiceCookerProgramSelect;

static const char *const TAG = "ricecooker";

/// Cooker state machine states.
enum class CookerState : uint8_t {
  IDLE,         ///< No program selected — ready to choose
  READY,        ///< Program selected — waiting for user to press Start
  RUNNING,      ///< A program is actively running
  WAITING_USER, ///< Program paused — waiting for user action confirmation
  FINISHED,     ///< A program completed naturally — stays until user acts
};

/// Main rice cooker component.
///
/// Coordinates the MCU communicator, heater controller, and cooking programs.
/// Exposes sensors (temperature, voltage), switches (power), and a select
/// entity (program choice) to Home Assistant.
class RiceCooker : public Component, public uart::UARTDevice {
 public:
  RiceCooker() = default;

  // --- Configuration setters (from Python codegen) ---
  void set_sensor_temp_top(sensor::Sensor *s) { sensor_top_ = s; }
  void set_sensor_temp_bottom(sensor::Sensor *s) { sensor_bottom_ = s; }
  void set_sensor_voltage(sensor::Sensor *s) { sensor_voltage_ = s; }
  void set_sensor_remaining(sensor::Sensor *s) { sensor_remaining_ = s; }
  void set_sensor_stage_remaining(sensor::Sensor *s) { sensor_stage_remaining_ = s; }
  void set_program_select(select::Select *s) { program_select_ = s; }
  void set_can_start_sensor(binary_sensor::BinarySensor *s) { can_start_sensor_ = s; }
  void set_can_cancel_sensor(binary_sensor::BinarySensor *s) { can_cancel_sensor_ = s; }
  void set_state_text_sensor(text_sensor::TextSensor *s) { state_text_sensor_ = s; }

  // --- Custom program creation (from Python codegen) ---
  void add_custom_program(const std::string &name, const std::string &next, const std::string &description);
  void add_program_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms,
                         bool hold, uint8_t top_threshold, uint8_t bottom_threshold, uint8_t power,
                         uint8_t start_temperature, uint8_t stage_type, uint8_t curve,
                         const char *description);

  // --- Program description getter ---
  const char *current_program_description() const;

  // --- Control methods (callable from YAML lambdas) ---
  void start();
  void cancel();
  void confirm();  ///< Confirm user action, continue program
  void power_on();
  void power_off();
  void set_wifi(bool status);

  /// Select a program by name (for select entity and YAML lambdas).
  void set_program_by_name(const std::string &name);

  /// Select a program by pointer (for internal auto-transition).
  void select_program(ProfileProgram *program);

  // --- State queries ---
  uint8_t get_top_temperature();
  uint8_t get_bottom_temperature();
  uint8_t get_voltage();
  bool get_power();

  /// Returns the current state name for the Estado text sensor.
  const char *get_state_name();

  /// Returns the list of select options (program names only).
  std::vector<const char *> get_program_names() const;

  // --- State flag getters (for HA template binary sensors) ---
  bool is_idle() const { return state_ == CookerState::IDLE; }
  bool is_ready() const { return state_ == CookerState::READY; }
  bool is_running() const { return state_ == CookerState::RUNNING; }
  bool is_waiting_user() const { return state_ == CookerState::WAITING_USER; }
  bool is_finished() const { return state_ == CookerState::FINISHED; }
  bool can_start() const { return state_ == CookerState::READY; }
  bool can_cancel() const { return state_ == CookerState::RUNNING || state_ == CookerState::WAITING_USER; }
  bool can_confirm() const { return state_ == CookerState::WAITING_USER; }
  bool can_select_program() const { return state_ != CookerState::RUNNING && state_ != CookerState::WAITING_USER; }

  // --- Program stage info (for HA sensors) ---
  size_t current_stage_index() const;
  size_t total_stages() const;
  const char *current_stage_type_name() const;
  uint8_t current_stage_target() const;
  const char *current_stage_description() const;

  // --- Component overrides ---
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // External entities
  sensor::Sensor *sensor_top_{nullptr};
  sensor::Sensor *sensor_bottom_{nullptr};
  sensor::Sensor *sensor_voltage_{nullptr};
  sensor::Sensor *sensor_remaining_{nullptr};
  sensor::Sensor *sensor_stage_remaining_{nullptr};
  select::Select *program_select_{nullptr};
  binary_sensor::BinarySensor *can_start_sensor_{nullptr};
  binary_sensor::BinarySensor *can_cancel_sensor_{nullptr};
  text_sensor::TextSensor *state_text_sensor_{nullptr};

  // Timing
  static constexpr uint32_t LOOP_INTERVAL_MS = 500;
  uint32_t loop_last_{0};

  // Display flash effect
  uint32_t flash_until_{0};  // Timestamp for transition flash effect

  // State machine
  CookerState state_{CookerState::IDLE};
  CookerState prev_state_{CookerState::IDLE};  // for change detection
  size_t prev_stage_index_{0};              // for stage change detection
  bool prev_finished_{false};                // for finished change detection

  // Programs (heap-allocated during setup, never freed — program lifetime = app lifetime)
  std::vector<ProfileProgram *> custom_programs_{};
  ProfileProgram *current_program_{nullptr};

  // Sub-components
  Heater heater_{};
  MCUCommunicator mcu_communicator_{};
};

/// Program select entity for Home Assistant.
class RiceCookerProgramSelect : public select::Select, public Component {
 public:
  void set_ricecooker(RiceCooker *rc) { ricecooker_ = rc; }
  void setup() override;
  void control(const std::string &value) override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA + 1.0f; }
 private:
  RiceCooker *ricecooker_{nullptr};
};

}  // namespace esphome::ricecooker