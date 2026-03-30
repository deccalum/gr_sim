#pragma once

/**
 * @brief Tracks the simulation's distinct time channels.
 * Coordinate time, affine parameter, wall time, and per-body proper time evolve for different
 * reasons and must not be conflated into a single scalar clock.
 */

#include <vector>

/**
 * @brief Snapshot of the simulation-wide time channels.
 */
struct SimulationTime {
  double wall_time_s = 0.0;    // Monotonic host elapsed time.
  double affine_lambda = 0.0;  // Integration parameter advanced for every body step.
  double coordinate_t = 0.0;   // Coordinate time in the active metric chart.
  long step_count = 0;         // Total solver steps taken.
};

/**
 * @brief Stores simulation time channels plus per-body proper times.
 */
class TimeSystem {
 public:
  void advance(double dl);
  void tick_wall();  // Call once per main loop iteration.
  const SimulationTime& now() const { return t_; }

  // Proper time is accumulated by body integrators and stored here for reporting.
  void set_proper_time(int body_id, double tau);
  double proper_time(int body_id) const;

 private:
  SimulationTime t_;
  std::vector<double> proper_times_;
  double wall_start_s_ = 0.0;
};
