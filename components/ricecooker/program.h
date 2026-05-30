#pragma once

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::ricecooker {

class Heater;

// Program name for "no program selected"
static const char *const NONE_NAME = "None";

/// Base class for all cooking programs.
/// A program runs through a sequence of stages and controls the heater
/// to reach/maintain target temperatures.
class Program {
 public:
  virtual ~Program() = default;

  virtual void step(Heater *heater) = 0;
  virtual const char *get_name() const = 0;
  virtual void start() = 0;
  virtual void cancel() = 0;
  virtual std::optional<unsigned int> remaining_time() { return std::nullopt; }
  virtual void reset() = 0;

  /// Whether to auto-transition to keep warm when program finishes.
  virtual bool keep_warm_after() const { return true; }
};

/// A generic profile-based program defined by a sequence of temperature stages.
///
/// Each stage is one of three types:
///   - **Transitional** (no duration, no hold): advance to next stage when
///     target temperature is reached.
///   - **Timed hold** (duration > 0, no hold): maintain temperature for the
///     specified duration, then advance.
///   - **Infinite hold** (hold = true): maintain temperature indefinitely
///     until the user cancels. Typically used as the last stage for
///     "keep warm" programs.
///
/// Example YAML for caramelized onions:
///
///   programs:
///     - name: "Cebolla Caramelizada"
///       keep_warm_after: true
///       stages:
///         - target_temperature: 80
///           hysteresis: 3
///         - target_temperature: 88
///           hysteresis: 3
///           duration: 40min
///         - target_temperature: 75
///           hysteresis: 2
///           duration: 15min
class ProfileProgram : public Program {
 public:
  ProfileProgram() = default;

  void step(Heater *heater) override;
  const char *get_name() const override { return name_.c_str(); }
  void start() override;
  void cancel() override;
  void reset() override;
  std::optional<unsigned int> remaining_time() override;
  bool keep_warm_after() const override { return keep_warm_after_; }

  void set_name(const std::string &name) { name_ = name; }
  void set_keep_warm_after(bool keep_warm) { keep_warm_after_ = keep_warm; }

  /// Add a stage to this program.
  /// @param target_temperature  Target temperature in °C
  /// @param hysteresis          Hysteresis band for power modulation in °C
  /// @param duration_ms        Hold time in ms (0 = transitional, advance when temp reached)
  /// @param hold               true = infinite hold (never advance)
  void add_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms, bool hold = false);

  /// Number of stages in this program.
  size_t stage_count() const { return stages_.size(); }

 private:
  struct Stage {
    uint8_t target_temperature;
    uint8_t hysteresis;
    uint32_t duration_ms;  // 0 = transitional (advance when reached), >0 = timed hold
    bool hold;             // true = infinite hold, never advance
  };

  std::string name_;
  bool keep_warm_after_{true};
  std::vector<Stage> stages_;

  // Runtime state
  size_t current_stage_index_{0};
  bool target_reached_{false};
  uint32_t stage_started_{0};
  bool started_{false};
  bool finished_{false};

  void advance_stage();
};

}  // namespace esphome::ricecooker