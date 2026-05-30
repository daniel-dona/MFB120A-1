#pragma once

#include "esphome/core/component.h"
#include "esphome/core/datatypes.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"

#include "program.h"
#include "heater.h"
#include "mcu_communicator.h"

namespace esphome::ricecooker {

class RiceCookerPowerSwitch;
class RiceCookerProgramSelect;

static const char *const TAG = "ricecooker";

class RiceCooker : public Component, public uart::UARTDevice {
 public:
  RiceCooker() = default;

  // --- Sensor setters (called from Python codegen) ---
  void set_sensor_temp_top(sensor::Sensor *sensor) { sensor_top_ = sensor; }
  void set_sensor_temp_bottom(sensor::Sensor *sensor) { sensor_bottom_ = sensor; }

  // --- Switch/Select setters (called from Python codegen) ---
  void set_power_switch(switch_::Switch *power_switch) { power_switch_ = power_switch; }
  void set_program_select(select::Select *program_select) { program_select_ = program_select; }

  // --- Configuration setters (called from Python codegen) ---
  void set_keep_warm_temperature(uint8_t temp) { keep_warm_.set_target_temperature(temp); }
  void set_keep_warm_hysteresis(uint8_t hysteresis) { keep_warm_.set_hysteresis(hysteresis); }
  void set_rice_cooking_time(uint8_t time) { rice_program_.set_cooking_time(time); }

  // --- Control methods (callable from YAML lambdas) ---
  void start();
  void cancel();
  void power_on();
  void power_off();
  void set_wifi(bool status);

  /// Select a program by name string (for select platform and YAML usage).
  void set_program_by_name(const std::string &name);

  /// Select a program by pointer (for internal use, e.g., auto-transition).
  void select_program(Program *program);

  // --- State getters ---
  uint8_t get_top_temperature();
  uint8_t get_bottom_temperature();
  bool get_power();
  const char *get_program_name();

  // --- Program accessors (for auto-transition to keep-warm) ---
  KeepWarm &get_keep_warm() { return keep_warm_; }

  // --- Component overrides ---
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // External entities
  sensor::Sensor *sensor_top_{nullptr};
  sensor::Sensor *sensor_bottom_{nullptr};
  switch_::Switch *power_switch_{nullptr};
  select::Select *program_select_{nullptr};

 private:
  // Timing
  uint32_t relay_interval_{500};
  uint32_t relay_last_{0};

  // Display state
  int hours_{0};
  int minutes_{0};

  // Pre-allocated programs (no heap allocation needed)
  KeepWarm keep_warm_{65, 5};
  RiceProgram rice_program_{15, 100, false};
  RiceProgram fast_rice_program_{15, 100, true};
  Program *current_program_{nullptr};

  Heater heater_;
  MCUCommunicator mcu_communicator_;
};

class RiceCookerPowerSwitch : public switch_::Switch, public Component {
 public:
  void set_ricecooker(RiceCooker *ricecooker) { ricecooker_ = ricecooker; }

  void write_state(bool state) override;

  void dump_config() override;

 private:
  RiceCooker *ricecooker_{nullptr};
};

class RiceCookerProgramSelect : public select::Select, public Component {
 public:
  void set_ricecooker(RiceCooker *ricecooker) { ricecooker_ = ricecooker; }

  void setup() override;
  void control(const std::string &value) override;
  void dump_config() override;

 private:
  RiceCooker *ricecooker_{nullptr};
};

}  // namespace esphome::ricecooker