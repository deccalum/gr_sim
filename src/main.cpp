/**
 * @brief Circular orbit validation at the Schwarzschild ISCO.
 *
 * Boots the field, places one massive body on a circular orbit at r=6M
 * (isotropic ρ ≈ 4.95M), and logs E, L, norm, and radial position every
 * N steps. A healthy integrator shows E and L drifting at O(dl⁴) over
 * the full run, with norm staying near -1.0.
 *
 * What to look for in the output:
 *   - E and L flat to ~6 significant figures over 1000 steps → integrator works
 *   - E or L drifting by >1e-4 per 100 steps → step size too large or Γ wrong
 *   - norm drifting from -1.0 → enforce_norm not firing (check tolerance)
 *   - rho constant → circular orbit maintained
 *   - rho growing/shrinking → orbit is spiraling (initial conditions or Γ error)
 */

#include <cmath>
#include <cstdio>
#include <memory>

#include "console/logger.h"
#include "engine/schwarzschild.h"
#include "simulation.h"

namespace {

/// Conserved energy for diagonal Schwarzschild: E = -g_tt u^t
double compute_E(const SpacetimeField& field, const Vec4& pos, const Vec4& vel,
                 const AccuracyProfile& acc) {
  Mat4 g{};
  Gamma gamma_unused{};
  field.eval_at(pos, g, gamma_unused, acc);
  return -g[0][0] * vel[0];
}


/// Conserved angular momentum: L = |x⃗ × p⃗| where p_i = g_ij u^j
/// In the x-y plane: L = x·p_y - y·p_x
double compute_L(const SpacetimeField& field, const Vec4& pos, const Vec4& vel,
                 const AccuracyProfile& acc) {
  Mat4 g{};
  Gamma gamma_unused{};
  field.eval_at(pos, g, gamma_unused, acc);
  const double p_x = g[1][1] * vel[1];
  const double p_y = g[2][2] * vel[2];
  return pos[1] * p_y - pos[2] * p_x;
}

/// Isotropic radial distance: ρ = sqrt(x² + y² + z²)
double compute_rho(const Vec4& pos) {
  return std::sqrt(pos[1] * pos[1] + pos[2] * pos[2] + pos[3] * pos[3]);
}

/// 4-velocity norm: g_μν u^μ u^ν (should be -1 for massive)
double compute_norm(const SpacetimeField& field, const Vec4& pos, const Vec4& vel,
                    const AccuracyProfile& acc) {
  Mat4 g{};
  Gamma gamma_unused{};
  field.eval_at(pos, g, gamma_unused, acc);
  double n = 0.0;
  for (int mu = 0; mu < 4; ++mu)
    for (int nu = 0; nu < 4; ++nu) n += g[mu][nu] * vel[mu] * vel[nu];
  return n;
}

}  // namespace

int main() {
  log(LogLevel::Info, "main", "gr_sim initiated");

  // Field
  double M = 1.0;
  auto metric = std::make_unique<SchwarzschildMetric>(M);

  // Run metric validator before anything else.
  {
    const auto vr = metric->validate();
    log(vr.passed ? LogLevel::Info : LogLevel::Error, "validate", "%s: %s", vr.subsystem.c_str(),
        vr.detail.c_str());
    if (!vr.passed) return 1;
  }

  Simulation sim(std::move(metric));
  const AccuracyProfile acc = AccuracyProfile::Max();
  sim.set_default_accuracy(acc);

  // Static observer at isotropic ρ = 20M.
  // u^t = 1/A(ρ_obs) from the timelike normalization g_tt(u^t)² = -1.
  {
    const double rho_obs   = 20.0 * M;
    const double alpha_obs = M / (2.0 * rho_obs);
    const double A_obs     = (1.0 - alpha_obs) / (1.0 + alpha_obs);
    const Vec4 obs_pos     = {0.0, rho_obs, 0.0, 0.0};
    const Vec4 obs_vel     = {1.0 / A_obs, 0.0, 0.0, 0.0};
    sim.add_observer(obs_pos, obs_vel, M);
  }

  // Circular orbit at Schwarzschild r = 6M
  // Convert to isotropic ρ: r = ρ(1 + M/(2ρ))²
  // Inversion: ρ = (r - M + sqrt((r-M)² - M²)) / 2
  const double R_schw = 6.0 * M;
  const double rho_iso = (R_schw - M + std::sqrt((R_schw - M) * (R_schw - M) - M * M)) / 2.0;

  const Vec4 pos = {0.0, rho_iso, 0.0, 0.0};

  // Circular orbit velocity in isotropic Cartesian:
  // At (ρ, 0, 0), motion is in the y-direction.
  // dy/dt = ρ · dφ/dt, where dφ/dt = sqrt(M / r³) (Kepler in Schwarzschild r)
  const double dphi_dt = std::sqrt(M / (R_schw * R_schw * R_schw));
  const double dy_dt = rho_iso * dphi_dt;

  // Normalize: g_tt(u^t)² + g_yy(u^y)² = -1
  // u^y = (dy/dt)·u^t, so: (g_tt + g_yy·(dy/dt)²)·(u^t)² = -1
  Mat4 g_init{};
  Gamma gamma_init{};
  sim.field().eval_at(pos, g_init, gamma_init, acc);

  const double u_t = std::sqrt(-1.0 / (g_init[0][0] + g_init[2][2] * dy_dt * dy_dt));
  const double u_y = dy_dt * u_t;
  const Vec4 vel = {u_t, 0.0, u_y, 0.0};

  Body* body = sim.add_body(1.0, pos, vel, acc);

  // Initial diagnostics
  const double E0 = compute_E(sim.field(), pos, vel, acc);
  const double L0 = compute_L(sim.field(), pos, vel, acc);
  const double norm0 = compute_norm(sim.field(), pos, vel, acc);
  const double rho0 = compute_rho(pos);

  log(LogLevel::Info, "main", "isotropic rho = %.6f  (Schwarzschild r = %.1f M)", rho_iso, R_schw);
  log(LogLevel::Info, "main", "dy/dt = %.6f  u^t = %.6f  u^y = %.6f", dy_dt, u_t, u_y);
  log(LogLevel::Info, "main", "E0 = %.12f  L0 = %.12f  norm0 = %.15f", E0, L0, norm0);

  // Integration with per-step logging
  const double total_lambda = 100.0;
  const double dl = 0.01;
  const int total_steps = static_cast<int>(total_lambda / dl);
  const int log_every = 100;

  fprintf(stderr, "\n%8s  %14s  %14s  %14s  %10s  %10s  %10s\n", "step", "E", "dE", "dL", "norm+1",
          "rho", "drho");
  fprintf(stderr, "%8s  %14s  %14s  %14s  %10s  %10s  %10s\n", "--------", "--------------",
          "--------------", "--------------", "----------", "----------", "----------");

  for (int step = 0; step < total_steps; ++step) {
    sim.step_once(dl);

    if ((step + 1) % log_every == 0 || step == 0 || step == total_steps - 1) {
      const Vec4& p = body->position();
      const Vec4& v = body->velocity();

      const double E = compute_E(sim.field(), p, v, acc);
      const double L = compute_L(sim.field(), p, v, acc);
      const double norm = compute_norm(sim.field(), p, v, acc);
      const double rho = compute_rho(p);

      fprintf(stderr, "%8d  %14.9f  %14.6e  %14.6e  %10.3e  %10.6f  %10.3e\n", step + 1, E, E - E0,
              L - L0, norm + 1.0, rho, rho - rho0);
    }
  }

  // Summary
  const Vec4& pf = body->position();
  const Vec4& vf = body->velocity();
  const double Ef = compute_E(sim.field(), pf, vf, acc);
  const double Lf = compute_L(sim.field(), pf, vf, acc);

  fprintf(stderr, "\nFinal: dE/E0 = %.3e  dL/L0 = %.3e\n", (Ef - E0) / E0, (Lf - L0) / L0);

  log(LogLevel::Info, "main", "done");
  return 0;
}
