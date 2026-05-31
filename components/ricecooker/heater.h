#pragma once

#include <cstdint>

#include "esphome/core/datatypes.h"

namespace esphome::ricecooker {

/// Heater controller with slow bang-bang relay cycling.
///
/// This replaces the original thermal-mass estimator with a simple,
/// proven approach matching the firmware's slow bang-bang algorithm.
///
/// Power levels (0-28) map to relay ON-time within a configurable cycle:
///   - Power 0: Heater OFF (entire cycle off)
///   - Power 1-8: Slow cycling (keep-warm, simmer)
///   - Power 9-15: Medium cycling (cooking)
///   - Power 16-28: Near-continuous (rapid heating/boiling)
///
/// Temperature feedback overrides the power cycle:
///   - If bottom_temp >= target + hysteresis: force OFF
///   - If bottom_temp < target - hysteresis: force ON
///   - Otherwise: follow power cycle
class Heater {
 public:
  Heater() = default;

  // --- Temperature interface ---
  void update(uint8_t top_temp, uint8_t bottom_temp);
  uint8_t get_top_temperature() const { return top_temperature_; }
  uint8_t get_bottom_temperature() const { return bottom_temperature_; }

  // --- Bang-bang control ---
  /// Set the target temperature and hysteresis for bang-bang control.
  void power_modulate(uint8_t target_temp, uint8_t hysteresis);

  // --- Relay control ---
  void power_on();
  void power_off();
  bool get_power() const { return power_; }
  void reset();

  // --- Power level ---
  /// Set power level (0=OFF, 1-28=cycling rate, 255=auto).
  /// Higher power = more ON-time per cycle.
  void set_power_level(uint8_t level) { power_level_ = level; }
  uint8_t get_power_level() const { return power_level_; }

  // --- Cycle timing ---
  /// Call this every loop() iteration with the current millis().
  void step(uint32_t now_ms);

  // --- Safety ---
  /// Emergency shutoff: immediately turn off heater and lock out.
  void emergency_off();

  /// True if emergency shutoff has been triggered.
  bool is_emergency() const { return emergency_; }

  /// Reset emergency state (call after fixing the issue).
  void clear_emergency() { emergency_ = false; }

 private:
  static constexpr uint32_t CYCLE_PERIOD_MS = 20000;  ///< 20-second power cycle
  static constexpr uint8_t MAX_POWER = 28;

  // Power state
  bool power_{false};
  bool emergency_{false};

  // Bang-bang targets
  uint8_t max_target_{0};   ///< target + hysteresis (ON threshold)
  uint8_t min_target_{0};   ///< target - hysteresis (OFF threshold)
  uint8_t power_level_{28}; ///< Current power level (0-28, 255=auto)

  // Cycle timing
  uint32_t cycle_start_ms_{0};
  uint32_t cycle_period_{CYCLE_PERIOD_MS};

  // Latest sensor readings
  uint8_t top_temperature_{0};
  uint8_t bottom_temperature_{0};

  /// Calculate ON-time in ms for the current power level within one cycle period.
  uint32_t get_on_time_ms() const;
};

}  // namespace esphome::ricecooker