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
   * @param x Spacetime coordinate @f$(t,r,\theta,\varphi)@f$.
   * @param g Output @f$g_{\mu\nu}@f$, written in row-major order.
   */
  virtual void metric(const Vec4& x, Mat4& g, const AccuracyProfile& acc) const = 0;
  /**
   * @param x Spacetime coordinate.
   * @param out Output @f$\Gamma^\sigma{}_{\mu\nu}@f$ with tensor order [σ][μ][ν].
   */
  virtual void christoffel(const Vec4& x, Gamma& out, const AccuracyProfile& acc) const = 0;
  /**
   * @param x Spacetime coordinate.
   * @param ginv Output contravariant metric @f$g^{\mu\nu}@f$.
   */
  virtual void metric_inverse(const Vec4& x, Mat4& ginv, const AccuracyProfile& acc) const = 0;
};
