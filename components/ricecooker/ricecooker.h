#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"

#include "program.h"
#include "heater.h"
#include "mcu_communicator.h"

namespace esphome::ricecooker {

class RiceCookerProgramSelect;

static const char *const TAG = "ricecooker";

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
  void set_program_select(select::Select *s) { program_select_ = s; }
  void set_keep_warm_temperature(uint8_t temp) { keep_warm_temperature_ = temp; }
  void set_keep_warm_hysteresis(uint8_t hyst) { keep_warm_hysteresis_ = hyst; }

  // --- Custom program creation (from Python codegen) ---
  void add_custom_program(const std::string &name, bool keep_warm_after);
  void add_program_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms,
                         bool hold, uint8_t top_threshold, uint8_t bottom_threshold, uint8_t power);

  // --- Control methods (callable from YAML lambdas) ---
  void start();
  void cancel();
  void power_on();
  void power_off();
  void set_wifi(bool status);

  /// Select a program by name (for select entity and YAML lambdas).
  void set_program_by_name(const std::string &name);

  /// Select a program by pointer (for internal auto-transition).
  void select_program(ProfileProgram *program);

  // --- State getters ---
  uint8_t get_top_temperature();
  uint8_t get_bottom_temperature();
  uint8_t get_voltage();
  bool get_power();
  const char *get_program_name();

  /// Returns the list of program names (including "None") for the select entity.
  std::vector<const char *> get_program_names() const;

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
  select::Select *program_select_{nullptr};

  // Timing
  static constexpr uint32_t LOOP_INTERVAL_MS = 500;
  uint32_t loop_last_{0};

  // Display state
  int hours_{0};
  int minutes_{0};

  // Default keep-warm config (used by keep_warm_after: true)
  uint8_t keep_warm_temperature_{75};
  uint8_t keep_warm_hysteresis_{4};

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