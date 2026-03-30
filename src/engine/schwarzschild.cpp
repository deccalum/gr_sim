/**
 * @brief Stub Schwarzschild metric implementation.
 * FLOP accounting for the future Christoffel kernel lives in expression_profile.cpp under
 * StubSchwarzschildChristoffel. When the full expressions are restored, g_rr at r = 10M should
 * match the textbook diagonal inverse to machine precision.
 */

#include "schwarzschild.h"

#include <cmath>

/**
 * @brief Evaluates the diagonal Schwarzschild metric tensor at x.
 * The line element is ds² = -(1 - 2M/r)dt² + (1 - 2M/r)^-1dr² + r²dΩ², so only four diagonal
 * entries are non-zero in these coordinates.
 */
void SchwarzschildMetric::metric(const Vec4& x, Mat4& g, const AccuracyProfile&) const {
  g = {};
  const double r = x[1], f = 1.0 - 2.0 * M_ / r, r2 = r * r;
  const double st = std::sin(x[2]);
  g[0][0] = -f;
  g[1][1] = 1.0 / f;
  g[2][2] = r2;
  g[3][3] = r2 * st * st;
}

/**
 * @brief Placeholder for the analytic Christoffel symbols.
 * The final implementation should emit only the non-zero Γ^σ_μν components allowed by
 * AccuracyProfile.christoffel_terms and avoid numerical differentiation entirely.
 */
void SchwarzschildMetric::christoffel(const Vec4&, Gamma& out, const AccuracyProfile&) const {
  out = {};
}

/**
 * @brief Forms g^μν by exploiting the diagonal structure of Schwarzschild coordinates.
 */
void SchwarzschildMetric::metric_inverse(const Vec4& x, Mat4& ginv,
                                         const AccuracyProfile& acc) const {
  ginv = {};
  Mat4 g{};
  metric(x, g, acc);
  // The Schwarzschild metric is diagonal in these coordinates, so inversion is element-wise.
  for (int i = 0; i < 4; ++i)
    if (g[i][i] != 0.0) ginv[i][i] = 1.0 / g[i][i];
}
