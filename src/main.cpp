/**
 * @brief Boots the simulator with a Schwarzschild field and one seed trajectory.
 * Startup order mirrors the roadmap's field-first requirement: nothing else constructs until the
 * spacetime field exists. Hardcoded launch parameters remain until config loading is introduced.
 */

#include <cstdio>
#include <memory>

#include "console/console_ui.h"
#include "console/logger.h"
#include "engine/schwarzschild.h"
#include "simulation.h"

int main() {
  log(LogLevel::Info, "main", "gr-simulator starting");

  auto metric =
      std::make_unique<SchwarzschildMetric>(1.0);  // Central mass M = 1 in geometric units.
  Simulation sim(std::move(metric));

  sim.set_default_accuracy(AccuracyProfile::Balanced());

  // Seed a nearly equatorial orbit at r = 10M. The production initializer should replace the
  // placeholder four-velocity with one satisfying the metric normalization constraints.
  Vec4 pos = {0.0, 10.0, 1.5708, 0.0};
  Vec4 vel = {1.0, 0.0, 0.0, 0.1};
  sim.add_body(0.0, pos, vel);  // Massless test particle placeholder.

  log(LogLevel::Info, "main", "entering main loop");

  const double total_lambda = 100.0;
  const double base_step = 0.01;
  sim.run(total_lambda, base_step);

  log(LogLevel::Info, "main", "done");
  return 0;
}
