#pragma once

#include <optional>

#include "esphome/core/component.h"

namespace esphome::ricecooker {

class Heater;

// Program name strings (shared across all instances)
static const char *const KEEP_WARM_NAME = "Keep Warm";
static const char *const RICE_NAME = "Rice";
static const char *const FAST_RICE_NAME = "Fast Rice";
static const char *const NONE_NAME = "None";

class Program {
 public:
  virtual ~Program() = default;

  virtual void step(Heater *heater) = 0;
  virtual const char *get_name() const = 0;

  /// Starts the program. If previously cancelled, starts from the beginning.
  virtual void start() = 0;

  /// Cancels the program, resetting its state.
  virtual void cancel() = 0;

  /// Returns remaining time in minutes, or nullopt if infinite/unknown.
  virtual std::optional<unsigned int> remaining_time() { return std::nullopt; }

  /// Resets the program state for reuse.
  virtual void reset() = 0;
};

class KeepWarm : public Program {
 public:
  KeepWarm(uint8_t target_temp, uint8_t hysteresis);

  void step(Heater *heater) override;
  const char *get_name() const override { return KEEP_WARM_NAME; }
  void start() override;
  void cancel() override;
  void reset() override { stage_ = Wait; }

  void set_target_temperature(uint8_t temp) { target_temp_ = temp; }
  void set_hysteresis(uint8_t hysteresis) { hysteresis_ = hysteresis; }
  uint8_t get_target_temperature() const { return target_temp_; }
  uint8_t get_hysteresis() const { return hysteresis_; }

 private:
  uint8_t target_temp_;
  uint8_t hysteresis_;

  enum Stage { Wait, Warm } stage_{Wait};
};

class RiceProgram : public Program {
 public:
  RiceProgram(uint8_t cooking_time, uint8_t cooking_temp = 100, bool fast = false);

  void step(Heater *heater) override;
  const char *get_name() const override;
  void start() override;
  void cancel() override;
  void reset() override;
  std::optional<unsigned int> remaining_time() override;

  void set_cooking_time(uint8_t time) { cooking_time_ = time; }
  uint8_t get_cooking_time() const { return cooking_time_; }

 private:
  // Configuration
  uint8_t cooking_time_;
  uint8_t cooking_temp_;
  bool fast_;

  // State
  enum Stage { Wait, Start, Soak, Heat, Cook, Vapor, Rest } stage_{Wait};
  uint32_t stage_started_{0};
  bool finished_{false};
  uint8_t vapor_max_{0};

  void set_stage(Stage stage);
};

}  // namespace esphome::ricecooker