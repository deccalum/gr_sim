/**
 * @brief Boots the simulator with a Schwarzschild field and one seed trajectory.
 * Startup order mirrors the roadmap's field-first requirement: nothing else constructs until the
 * spacetime field exists. Hardcoded launch parameters remain until config loading is introduced.
 */

#include <cstdio>
#include <memory>
#include <cmath>

#include "console/console_ui.h"
#include "console/logger.h"
#include "engine/schwarzschild.h"
#include "simulation.h"

int main() {
  log(LogLevel::Info, "main", "gr-simulator starting");

  double M = 1.0;
  auto metric = std::make_unique<SchwarzschildMetric>(M);
  Simulation sim(std::move(metric));

  sim.set_default_accuracy(AccuracyProfile::Max());

  // Setup circular orbit at Schwarzschild r = 6M
  // Convert standard r to isotropic rho: r = rho (1 + M/2rho)^2
  // rho = (r - M + sqrt((r-M)^2 - M^2)) / 2
  double R_std = 6.0 * M;
  double rho = (R_std - M + std::sqrt((R_std - M)*(R_std - M) - M*M)) / 2.0;

  Vec4 pos = {0.0, rho, 0.0, 0.0};

  // Compute circular orbit tangential velocity
  // Requires Gamma^x_tt (u^t)^2 + Gamma^x_yy (u^y)^2 = 0
  // u^y = sqrt(-Gamma^x_tt / Gamma^x_yy) * u^t
  
  Mat4 g{};
  Gamma gamma{};
  sim.field().eval_at(pos, g, gamma, AccuracyProfile::Max());

  // ratio = (u^y / u^t)^2
  // For a circular orbit, Kepler's third law gives angular velocity (dphi/dt) = sqrt(M/R_std^3)
  // In our Cartesian isotropic setup at (rho, 0, 0), dy/dt = rho * dphi/dt.
  // So u^y / u^t = dy/dt = rho * sqrt(M / R_std^3)
  double dy_dt = rho * std::sqrt(M / (R_std * R_std * R_std));
  double ratio = dy_dt * dy_dt;

  log(LogLevel::Info, "main", "dy_dt=%f, ratio=%f", dy_dt, ratio);

  // g_tt (u^t)^2 + g_yy (u^y)^2 = -1 (timelike)
  // (u^t)^2 = -1 / (g_tt + g_yy * ratio)
  double u_t = std::sqrt(-1.0 / (g[0][0] + g[2][2] * ratio));
  double u_y = dy_dt * u_t;

  Vec4 vel = {u_t, 0.0, u_y, 0.0};
  
  Body* body = sim.add_body(1.0, pos, vel, AccuracyProfile::Max());

  log(LogLevel::Info, "main", "starting circular orbit at isotropic rho = %f", rho);

  const double total_lambda = 100.0;
  const double base_step = 0.01;
  
  // Pre-run metrics
  double E_initial = -g[0][0] * vel[0];
  double L_initial = g[2][2] * pos[1] * vel[2];
  
  log(LogLevel::Info, "main", "Initial E = %.6f (expected ~0.942809)", E_initial);
  log(LogLevel::Info, "main", "Initial L = %.6f (expected ~3.464102)", L_initial);

  sim.run(total_lambda, base_step);

  // Post-run metrics
  Vec4 final_pos = body->position();
  Vec4 final_vel = body->velocity();
  sim.field().eval_at(final_pos, g, gamma, AccuracyProfile::Max());
  
  // E = - g_tt u^t (ignoring cross terms since diagonal)
  double E_final = -g[0][0] * final_vel[0];
  
  // Angular momentum L = x * p_y - y * p_x 
  // p_i = g_ij u^j
  double p_x = g[1][1] * final_vel[1];
  double p_y = g[2][2] * final_vel[2];
  double L_final = final_pos[1] * p_y - final_pos[2] * p_x;

  log(LogLevel::Info, "main", "Final E = %.6f  Diff: %e", E_final, E_final - E_initial);
  log(LogLevel::Info, "main", "Final L = %.6f  Diff: %e", L_final, L_final - L_initial);
  log(LogLevel::Info, "main", "done");
  return 0;
}
