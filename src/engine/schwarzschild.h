#pragma once

/**
 * @brief Implements the Schwarzschild solution in spherical coordinates.
 * Coordinates are ordered as (t, r, θ, φ) and all expressions assume geometric units with G = c =
 * 1. Christoffel symbols remain analytic so the solver never pays for finite-difference derivative
 * estimates.
 * @note The Christoffel path is expected to be memory-bound at roughly 57 FLOPs over 552 bytes.
 */

#include "../system/validator.h"
#include "metric.h"

/**
 * @brief Exact vacuum metric for a static, spherically symmetric mass.
 * @details The line element in spherical coordinates (t, r, θ, φ) is:
 *
 *   ds² = -(1 - 2M/r) dt² + (1 - 2M/r)⁻¹ dr² + r² dθ² + r² sin²θ dφ²
 *
 * Events at r ≤ 2M are inside the event horizon; callers should guard
 * against degenerate metric evaluations there.
 */
class SchwarzschildMetric final : public MetricProvider, public Validatable {
 public:
  explicit SchwarzschildMetric(double mass_M) : M_(mass_M) {}

  void metric(const Vec4& x, Mat4& g, const AccuracyProfile& acc) const override;
  void christoffel(const Vec4& x, Gamma& g, const AccuracyProfile& acc) const override;
  void metric_inverse(const Vec4& x, Mat4& ginv, const AccuracyProfile& acc) const override;

  double mass() const { return M_; }

  // Validatable interface — run via ValidatorRunner or CUI "validate"
  ValidationResult validate() const override;

 private:
  double M_;  // gravitational mass in geometric units (G=c=1)
};
