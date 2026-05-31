#pragma once

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::ricecooker {

class Heater;

/// Sentinel name for "no program selected".
static const char *const NONE_NAME = "None";

/// Stage types for YAML-driven cooking programs.
enum class StageType : uint8_t {
  /// Reach target_temperature, then advance to next stage.
  TRANSITIONAL,
  /// Hold at target_temperature for a fixed duration, then advance.
  TIMED_HOLD,
  /// Hold at target_temperature indefinitely (until cancelled by user).
  INFINITE_HOLD,
};

/// A single stage in a cooking program profile.
struct Stage {
  StageType type;
  uint8_t target_temperature;   ///< Bang-bang setpoint in °C
  uint8_t hysteresis;           ///< ±°C tolerance band
  uint32_t duration_ms;         ///< ms (0 for transitional/infinite, >0 for timed)
  uint8_t top_threshold;        ///< °C, advance if lid ≥ this (0 = disabled)
  uint8_t bottom_threshold;     ///< °C, advance if plate ≥ this (0 = disabled)
  uint8_t power;                ///< Heater power level (0=OFF, 1-28=level, 255=auto)
};

/// A cooking program defined entirely by YAML configuration.
///
/// Programs consist of a linear sequence of stages. Stage advancement
/// follows the firmware's ANY-condition rule: a stage advances when ANY of:
///   - timer expires (for timed stages)
///   - bottom temperature >= bottom_threshold (if set)
///   - top temperature >= top_threshold (if set)
///   - bottom temperature >= target_temperature (for transitional stages)
///
/// Keep-warm is handled by adding an INFINITE_HOLD stage at the end, either
/// manually in YAML or automatically via `keep_warm_after: true`.
class ProfileProgram {
 public:
  ProfileProgram() = default;

  // --- Program info ---
  const char *get_name() const { return name_.c_str(); }
  void set_name(const std::string &name) { name_ = name; }
  bool keep_warm_after() const { return keep_warm_after_; }
  void set_keep_warm_after(bool v) { keep_warm_after_ = v; }
  size_t stage_count() const { return stages_.size(); }

  // --- Stage creation ---
  void add_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms,
                 bool hold, uint8_t top_threshold, uint8_t bottom_threshold, uint8_t power);

  // --- Program lifecycle ---
  void start();
  void cancel();
  void step(Heater *heater);

  // --- State queries ---
  bool is_running() const { return running_; }
  bool is_finished() const { return finished_; }
  bool is_keep_warm() const;
  std::optional<uint32_t> remaining_time_seconds() const;

 private:
  std::string name_;
  bool keep_warm_after_{true};
  std::vector<Stage> stages_;

  // Runtime state
  bool running_{false};
  bool finished_{false};
  size_t current_stage_{0};
  uint32_t stage_started_ms_{0};  ///< millis() when current stage's timer started
  bool target_reached_{false};    ///< true once bottom_temp first reaches target

  // Emergency state
  bool emergency_shutdown_{false};

  // Keep-warm default config (used when keep_warm_after=true)
  uint8_t keep_warm_temp_{75};
  uint8_t keep_warm_hyst_{4};

  void advance_stage();
  void do_emergency_shutdown(Heater *heater);

  /// Apply heater control for the current stage.
  void apply_stage_control_(Heater *heater, const Stage &stage);

  /// Check if the current stage should advance.
  bool should_advance_(Heater *heater, const Stage &stage) const;
};

}  // namespace esphome::ricecooker