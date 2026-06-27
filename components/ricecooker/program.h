#pragma once

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::ricecooker {

class Heater;

/// State names displayed in the status sensor and select entity.
static const char *const IDLE_NAME = "Idle";
static const char *const READY_NAME = "Ready";
static const char *const FINISHED_NAME = "Finished";
static const char *const WAITING_USER_NAME = "Esperando";

/// Stage types for YAML-driven cooking programs.
enum class StageType : uint8_t {
  TRANSITIONAL,   ///< heat — reach target, advance
  TIMED_HOLD,     ///< hold — maintain target for duration
  RAMP,           ///< ramp — interpolate from→to over duration
  INFINITE_HOLD,  ///< keep — maintain indefinitely
  USER_ACTION,    ///< action — pause and wait for user confirmation
  NOTIFY,         ///< notify — show message, continue automatically
};

/// Curve types for ramp stages.
enum class RampCurve : uint8_t {
  LINEAR,     ///< straight line
  EASE_IN,    ///< slow start, fast finish
  EASE_OUT,   ///< fast start, slow finish
  STEP,       ///< hold at start 50%, jump to target
};

/// A single stage in a cooking program profile.
struct Stage {
  StageType type;
  RampCurve curve;
  uint8_t target_temperature;   ///< Bang-bang setpoint in °C
  uint8_t start_temperature;    ///< Start temp for RAMP stages
  uint8_t hysteresis;           ///< ±°C tolerance band
  uint32_t duration_ms;         ///< ms (0 for transitional/infinite, >0 for timed/ramp)
  uint8_t top_threshold;        ///< °C, advance if lid ≥ this (0 = disabled)
  uint8_t bottom_threshold;     ///< °C, advance if plate ≥ this (0 = disabled)
  uint8_t power;                ///< Heater power level (0=OFF, 1-28=level, 255=auto)
  const char *description;      ///< Human-readable stage description (static string from codegen)
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
/// Keep-warm is handled by defining it explicitly as a program with an
/// INFINITE_HOLD stage, and chaining to it via `next: "Mantener Caliente"`.
class ProfileProgram {
 public:
  ProfileProgram() = default;

  // --- Program info ---
  const char *get_name() const { return name_.c_str(); }
  void set_name(const std::string &name) { name_ = name; }
  const char *get_description() const { return description_.c_str(); }
  void set_description(const std::string &desc) { description_ = desc; }
  const char *next_program() const { return next_program_.empty() ? nullptr : next_program_.c_str(); }
  void set_next_program(const std::string &name) { next_program_ = name; }
  size_t stage_count() const { return stages_.size(); }
  const std::vector<Stage> &stages() const { return stages_; }

  // --- Stage creation ---
  void add_stage(uint8_t target_temperature, uint8_t hysteresis, uint32_t duration_ms,
                 bool hold, uint8_t top_threshold, uint8_t bottom_threshold, uint8_t power,
                 uint8_t start_temperature, uint8_t stage_type, RampCurve curve,
                 const char *description);

  // --- Program lifecycle ---
  void start();
  void cancel();
  void step(Heater *heater);
  void confirm();  ///< Advance past current USER_ACTION stage

  // --- State queries ---
  bool is_running() const { return running_; }
  bool is_finished() const { return finished_; }
  bool is_keep_warm() const;
  bool is_waiting_user() const { return waiting_user_; }
  std::optional<uint32_t> remaining_time_seconds() const;
  std::optional<uint32_t> current_stage_remaining_seconds() const;
  size_t current_stage_index() const { return current_stage_; }
  size_t total_stages() const { return stages_.size(); }
  const char *current_stage_type_name() const;
  uint8_t current_stage_target() const;
  const char *current_stage_description() const;

 private:
  std::string name_;
  std::string description_;
  std::string next_program_;
  std::vector<Stage> stages_;

  // Runtime state
  bool running_{false};
  bool finished_{false};
  size_t current_stage_{0};
  uint32_t stage_started_ms_{0};  ///< millis() when current stage's timer started
  bool target_reached_{false};    ///< true once bottom_temp first reaches target
  bool waiting_user_{false};       ///< true when USER_ACTION stage is waiting for confirmation

  // Emergency state
  bool emergency_shutdown_{false};



  void advance_stage();
  void do_emergency_shutdown(Heater *heater);

  /// Apply heater control for the current stage.
  void apply_stage_control_(Heater *heater, const Stage &stage);

  /// Check if the current stage should advance.
  bool should_advance_(Heater *heater, const Stage &stage) const;
};

}  // namespace esphome::ricecooker