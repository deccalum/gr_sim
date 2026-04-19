#pragma once

/**
 * @brief Declares the abstract interface for spacetime metrics used by the field engine.
 * Implementations provide g_μν, g^μν, and Γ^σ_μν at a spacetime sample point. AccuracyProfile is
 * threaded through every call so analytical and approximate metrics can share a common API.
 */

#include <array>

struct AccuracyProfile;
using Vec4 = std::array<double, 4>;
using Mat4 = std::array<std::array<double, 4>, 4>;
using Gamma = std::array<std::array<std::array<double, 4>, 4>, 4>;  // Tensor order is [σ][μ][ν].

/**
 * @brief Base contract for analytical or numerical spacetime metrics.
 */
class MetricProvider {
 public:
  virtual ~MetricProvider() = default;
  /**
   * @param x Spacetime coordinate (t, r, θ, φ).
   * @param g Output g_{μν}, written in row-major order.
   */
  virtual void metric(const Vec4& x, Mat4& g, const AccuracyProfile& acc) const = 0;
  /**
   * @param x Spacetime coordinate.
   * @param out Output Γ^σ_{μν} with tensor order [σ][μ][ν].
   */
  virtual void christoffel(const Vec4& x, Gamma& out, const AccuracyProfile& acc) const = 0;
  /**
   * @param x Spacetime coordinate.
   * @param ginv Output contravariant metric g^{μν}.
   */
  virtual void metric_inverse(const Vec4& x, Mat4& ginv, const AccuracyProfile& acc) const = 0;
};

/**
 * @brief Isotropic Schwarzschild metric in Cartesian coordinates.
 * @param x Spacetime coordinate.
 * @param g Output metric tensor.
 * @param acc Accuracy profile.
 *
 * ρ = sqrt(x² + y² + z²)  isotropic radial distance
 * α = M / (2ρ)             compactness parameter
 * ψ = 1 + α                conformal factor
 * A = (1 - α) / (1 + α)    lapse factor
 */