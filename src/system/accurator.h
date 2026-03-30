#pragma once

/**
 * @brief Defines per-entity accuracy hints for integrators and field evaluation.
 * These values describe relative fidelity goals, not final hard limits. The roofline and allocator
 * layers translate them into actual iteration budgets at runtime.
 */

#include <cstdint>

/**
 * @brief Controls numerical precision and integrator behavior per body or AMR cell.
 * Accuracy parameters are outputs of the roofline model, not hardcoded presets.
 * Preset factories produce weight profiles, and the roofline derives actual
 * iteration counts from these weights at runtime.
 * @note `weight` is the main budget-allocation hint. Strong-field logic may still increase the
 * final budget beyond the preset's nominal request.
 * @note `step_size` scales the affine-parameter increment @f$\Delta\lambda@f$ relative to the
 * local @f$r_s = 2M@f$; it is dimensionless in geometric units (@f$G = c = 1@f$).
 */
struct AccuracyProfile {
  int integrator_order = 4;  // 2=Euler, 4=RK4, 8=DOP853
  bool adaptive = true;
  double step_size = 0.01;  // Δx or affine parameter λ hint

  int christoffel_terms = 64;  // Max non-zero Γ components
  int pn_order = 0;            // 0=Schwarzschild, 1/2/3=PN
  double amr_refinement_level = 1.0;

  bool enforce_norm = true;  // Reprojects u^μ when g_μν u^μ u^ν deviates from -1
  double norm_tolerance = 1e-6;

  double weight = 0.5;  // Used for budget allocation (0.0 - 1.0)

  static AccuracyProfile Fast() { return {4, false, 0.05, 16, 0, 0.5, false, 1e-4, 0.25}; }
  static AccuracyProfile Balanced() { return {4, true, 0.01, 64, 0, 1.0, true, 1e-6, 0.50}; }
  static AccuracyProfile High() { return {8, true, 0.005, 64, 0, 2.0, true, 1e-9, 0.85}; }
  static AccuracyProfile Max() { return {8, true, 0.001, 64, 3, 4.0, true, 1e-12, 1.00}; }
};