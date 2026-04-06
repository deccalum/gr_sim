/**
 * @brief Stub Schwarzschild metric implementation.
 * FLOP accounting for the future Christoffel kernel lives in expression_profile.cpp under
 * StubSchwarzschildChristoffel. When the full expressions are restored, g_rr at r = 10M should
 * match the textbook diagonal inverse to machine precision.
 */

#include "schwarzschild.h"

#include <cassert>
#include <cmath>
#include <string>

#include "../constants/constants.h"
#include "../constants/units.h"
#include "../system/accurator.h"

namespace {
/**
 * @brief Pre-computed scalar factors for isotropic Schwarzschild coordinates.
 *
 * All Christoffel and metric expressions factor through a small set of scalars that depend only on
 * the isotropic radius @f$\rho = \sqrt{x^2+y^2+z^2}@f$ and the mass @f$M@f$.  Grouping them here
 * avoids redundant divisions inside the hot tensor-fill loops and makes the symbolic correspondence
 * to textbook expressions explicit.
 *
 * Coordinate conventions (isotropic Cartesian, G = c = 1):
 * @f[
 *   \rho = \sqrt{x^2+y^2+z^2}, \quad
 *   \alpha = \frac{M}{2\rho}, \quad
 *   \psi = 1+\alpha, \quad
 *   A = \frac{1-\alpha}{1+\alpha}
 * @f]
 */
struct IsoFactors {
  double rho;        // isotropic radius ρ
  double rho2;       // ρ²
  double alpha;      // M / (2ρ)
  double psi;        // 1 + α
  double psi2;       // ψ²
  double psi4;       // ψ⁴
  double psi6;       // ψ⁶
  double A;          // (1 - α) / ψ
  double A2;         // A²
  double two_alpha;  // 2α
  double f_t;        // 2α / (A · ρ² · ψ²)  → Γ^t_ti = f_t · x_i
  double f_r;        // 2Aα / (ρ² · ψ⁶)     → Γ^i_tt = f_r · x_i
  double f_s;        // 2α / (ρ² · ψ)       → spatial Γ factor
};

/**
 * @brief Computes all scalar factors needed for the metric and Christoffel kernels at @p x.
 *
 * Marked `noinline` so the compiler emits a distinct symbol that the benchmark tool can profile
 * separately from the outer tensor-fill loops.
 *
 * @param x Isotropic Cartesian position @f$(t, x, y, z)@f$ in geometric units.
 * @param M Gravitational mass in geometric units (G = c = 1).
 * @return  Fully populated `IsoFactors` ready for use in @ref metric and @ref christoffel.
 */
__attribute__((noinline)) IsoFactors compute_factors(const Vec4& x, double M) {
  IsoFactors f{};

  f.rho2 = x[idx::X] * x[idx::X] + x[idx::Y] * x[idx::Y] + x[idx::Z] * x[idx::Z];

  f.rho = std::sqrt(f.rho2);      // ρ = sqrt(ρ²)
  f.alpha = M * 0.5 / f.rho;      // α = M / (2ρ)
  f.psi = 1.0 + f.alpha;          // ψ = 1 + α
  f.A = (1.0 - f.alpha) / f.psi;  // A = (1 - α) / ψ
  f.A2 = f.A * f.A;               // A²

  f.psi2 = f.psi * f.psi;    // ψ²
  f.psi4 = f.psi2 * f.psi2;  // ψ⁴
  f.psi6 = f.psi4 * f.psi2;  // ψ⁶

  f.two_alpha = 2.0 * f.alpha;                    // 2α
  f.f_t = f.two_alpha / (f.A * f.rho2 * f.psi2);  // 2α / (A · ρ² · ψ²)
  f.f_r = f.two_alpha * f.A / (f.rho2 * f.psi6);  // 2α / (ρ² · ψ⁶)
  f.f_s = f.two_alpha / (f.rho2 * f.psi);         // 2α / (ρ² · ψ)
  return f;
}
}  // namespace

/**
 * @brief Evaluates the diagonal Schwarzschild metric tensor at x.
 * The line element is ds² = -(1 - 2M/r)dt² + (1 - 2M/r)^-1dr² + r²dΩ², so only four diagonal
 * entries are non-zero in these coordinates.
 */
void SchwarzschildMetric::metric(const Vec4& x, Mat4& g, const AccuracyProfile&) const {
  g = {};

  const IsoFactors f = compute_factors(x, M_);

  g[idx::T][idx::T] = -f.A2;   // g_tt = -A²
  g[idx::X][idx::X] = f.psi4;  // g_xx = ψ⁴
  g[idx::Y][idx::Y] = f.psi4;  // g_yy = ψ⁴
  g[idx::Z][idx::Z] = f.psi4;  // g_zz = ψ⁴
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

  const IsoFactors f = compute_factors(x, M_);
  ginv[idx::T][idx::T] = -1.0 / f.A2;   // g^tt = -1 / A²
  ginv[idx::X][idx::X] = 1.0 / f.psi4;  // g^xx = 1 / ψ⁴
  ginv[idx::Y][idx::Y] = 1.0 / f.psi4;  // g^yy = 1 / ψ⁴
  ginv[idx::Z][idx::Z] = 1.0 / f.psi4;  // g^zz = 1 / ψ⁴
}

/**
 * @brief Analytic Christoffel symbols for the isotropic Schwarzschild metric.
 *
 * Only the non-zero independent components are written; lower-index symmetry
 * @f$\Gamma^\sigma{}_{\mu\nu} = \Gamma^\sigma{}_{\nu\mu}@f$ is applied explicitly so the caller
 * never reads un-filled entries.
 *
 * Non-zero families (factor notation matches `IsoFactors`):
 * - @f$\Gamma^t{}_{ti} = f_t \, x_i@f$  and its symmetric partner @f$\Gamma^t{}_{it}@f$
 * - @f$\Gamma^i{}_{tt} = f_r \, x_i@f$
 * - Spatial blocks @f$\Gamma^i{}_{jk}@f$ expressed via @f$f_s\,x_i@f$; diagonal terms carry a
 *   minus sign, cross-index terms carry a plus sign (see inline labels).
 *
 * @param x   Isotropic Cartesian position @f$(t, x, y, z)@f$.
 * @param gamma Output tensor (zeroed on entry via memset).
 * @param acc Unused in this implementation; reserved for future PN-order gating.
 */
void SchwarzschildMetric::christoffel(const Vec4& x, Gamma& gamma,
                                      const AccuracyProfile& acc) const {
  std::memset(&gamma, 0, sizeof(gamma));  // Zero-fill all entries.

  const IsoFactors f = compute_factors(x, M_);
  const double fs_x = f.f_s * x[idx::X];
  const double fs_y = f.f_s * x[idx::Y];
  const double fs_z = f.f_s * x[idx::Z];

  gamma[idx::T][idx::T][idx::X] = f.f_t * x[idx::X];              // Γ^t_tt = f_t · x_i
  gamma[idx::T][idx::X][idx::T] = gamma[idx::T][idx::T][idx::X];  // symmetry
  gamma[idx::T][idx::T][idx::Y] = f.f_t * x[idx::Y];              // Γ^t_tt = f_t · x_i
  gamma[idx::T][idx::Y][idx::T] = gamma[idx::T][idx::T][idx::Y];  // symmetry
  gamma[idx::T][idx::T][idx::Z] = f.f_t * x[idx::Z];              // Γ^t_tt = f_t · x_i
  gamma[idx::T][idx::Z][idx::T] = gamma[idx::T][idx::T][idx::Z];  // symmetry

  gamma[idx::X][idx::T][idx::T] = f.f_r * x[idx::X];  // Γ^i_tt = f_r · x_i
  gamma[idx::Y][idx::T][idx::T] = f.f_r * x[idx::Y];  // Γ^i_tt = f_r · x_i
  gamma[idx::Z][idx::T][idx::T] = f.f_r * x[idx::Z];  // Γ^i_tt = f_r · x_i

  // σ = x (index 1) ---------------------------------------------------
  gamma[idx::X][idx::X][idx::X] = -fs_x;  // Γ^x_xx
  gamma[idx::X][idx::X][idx::Y] = -fs_y;  // Γ^x_xy
  gamma[idx::X][idx::Y][idx::X] = -fs_y;  // Γ^x_yx (= Γ^x_xy)
  gamma[idx::X][idx::X][idx::Z] = -fs_z;  // Γ^x_xz
  gamma[idx::X][idx::Z][idx::X] = -fs_z;  // Γ^x_zx
  gamma[idx::X][idx::Y][idx::Y] = +fs_x;  // Γ^x_yy
  gamma[idx::X][idx::Z][idx::Z] = +fs_x;  // Γ^x_zz

  // σ = y (index 2) ---------------------------------------------------
  gamma[idx::Y][idx::Y][idx::Y] = -fs_y;  // Γ^y_yy
  gamma[idx::Y][idx::X][idx::Y] = -fs_x;  // Γ^y_xy
  gamma[idx::Y][idx::Y][idx::X] = -fs_x;  // Γ^y_yx
  gamma[idx::Y][idx::Y][idx::Z] = -fs_z;  // Γ^y_yz
  gamma[idx::Y][idx::Z][idx::Y] = -fs_z;  // Γ^y_zy
  gamma[idx::Y][idx::X][idx::X] = +fs_y;  // Γ^y_xx
  gamma[idx::Y][idx::Z][idx::Z] = +fs_y;  // Γ^y_zz

  // σ = z (index 3) ---------------------------------------------------
  gamma[idx::Z][idx::Z][idx::Z] = -fs_z;  // Γ^z_zz
  gamma[idx::Z][idx::X][idx::Z] = -fs_x;  // Γ^z_xz
  gamma[idx::Z][idx::Z][idx::X] = -fs_x;  // Γ^z_zx
  gamma[idx::Z][idx::Y][idx::Z] = -fs_y;  // Γ^z_yz
  gamma[idx::Z][idx::Z][idx::Y] = -fs_y;  // Γ^z_zy
  gamma[idx::Z][idx::X][idx::X] = +fs_z;  // Γ^z_xx
  gamma[idx::Z][idx::Y][idx::Y] = +fs_z;  // Γ^z_yy
}

// ---------------------------------------------------------------------------
// schwarzschild_validator — first physics regression checks
// ---------------------------------------------------------------------------
//
// Called by ValidatorRunner at startup and on "validate" CUI command.
// Tests at ρ = 5M, position (0, 5M, 0, 0):
//   α = 0.1, ψ = 1.1, A = 9/11
//
// Known-answer values derived analytically — must match to machine precision.

#include <cmath>
#include <string>

#include "../system/validator.h"

/**
 * @brief Physics regression suite for `SchwarzschildMetric`.
 *
 * Evaluates all three metric methods (@ref metric, @ref metric_inverse, @ref christoffel) at the
 * canonical test point @f$\rho = 5M@f$ (isotropic Cartesian position @f$(0, 5M, 0, 0)@f$) and
 * compares against known-answer values derived analytically:
 *
 * |    Symbol    | Value |
 * |--------------|-------|
 * | @f$\alpha@f$ |   0.1 |
 * | @f$\psi@f$   |   1.1 |
 * | @f$A@f$      |  9/11 |
 * | @f$g_{tt}@f$ | @f$-(9/11)^2 = -81/121@f$ |
 * | @f$g_{xx}@f$ | @f$1.1^4 = 1.4641@f$      |
 *
 * Tolerance is @f$10^{-14}@f$ (≈ 10× machine epsilon for `double`). The function also checks
 * lower-index symmetry @f$\Gamma^t{}_{tx} = \Gamma^t{}_{xt}@f$ and a spatial Christoffel value.
 *
 * @return ValidationResult with `passed = true` and a summary string on success, or a failure
 *         detail identifying the first failing check.
 */
ValidationResult SchwarzschildMetric::validate() const {
  ValidationResult r{};
  r.subsystem = "SchwarzschildMetric";

  // Test point: ρ = 5M in isotropic coords → position (0, 5M, 0, 0)
  const double rho = 5.0 * M_;
  const Vec4 x_test = {0.0, rho, 0.0, 0.0};

  // Known-answer values at ρ = 5M (α=0.1, ψ=1.1, A=9/11)
  const double alpha_ref = 0.1;
  const double psi_ref = 1.1;
  const double A_ref = 9.0 / 11.0;
  const double g_tt_ref = -(A_ref * A_ref);                       // -(9/11)² = -81/121
  const double g_xx_ref = psi_ref * psi_ref * psi_ref * psi_ref;  // 1.1⁴ = 1.4641

  // Tolerance: 10× machine epsilon for double
  const double tol = 1e-14;

  // Check metric diagonal
  Mat4 g{};
  AccuracyProfile acc = AccuracyProfile::Balanced();
  metric(x_test, g, acc);

  auto check = [&](const char* name, double measured, double expected) -> bool {
    const double err = std::abs(measured - expected);
    if (err > tol) {
      r.detail = std::string(name) + ": measured=" + std::to_string(measured) +
                 " expected=" + std::to_string(expected) + " |error|=" + std::to_string(err);
      return false;
    }
    return true;
  };

  if (!check("g_tt", g[idx::T][idx::T], g_tt_ref)) {
    return r;
  }
  if (!check("g_xx", g[idx::X][idx::X], g_xx_ref)) {
    return r;
  }
  if (!check("g_yy", g[idx::Y][idx::Y], g_xx_ref)) {
    return r;
  }
  if (!check("g_zz", g[idx::Z][idx::Z], g_xx_ref)) {
    return r;
  }

  // Check metric inverse: g^μν g_νλ = δ^μ_λ  (diagonal → just reciprocal check)
  Mat4 ginv{};
  metric_inverse(x_test, ginv, acc);
  if (!check("g^tt·g_tt", ginv[0][0] * g[0][0], -1.0)) {
    return r;
  }
  if (!check("g^xx·g_xx", ginv[1][1] * g[1][1], 1.0)) {
    return r;
  }

  // Check Christoffel: Γ^x_tt = f_r · x at test point
  // f_r = 2Aα/(ρ²ψ⁶), x_1 = ρ = 5M
  // f_r · ρ = 2*(9/11)*0.1 / (25M² * 1.1⁶) * 5M
  //         = 2*(9/11)*0.1*5M / (25M² * 1.771561)
  //         = 9/(11) * 1/(25M * 1.771561)
  const double psi6_ref = psi_ref * psi_ref * psi_ref * psi_ref * psi_ref * psi_ref;
  const double gamma_x_tt_ref = (2.0 * A_ref * alpha_ref / ((rho * rho) * psi6_ref)) * rho;

  Gamma gam{};
  christoffel(x_test, gam, acc);
  if (!check("Γ^x_tt", gam[idx::X][idx::T][idx::T], gamma_x_tt_ref)) {
    return r;
  }

  // Check Γ symmetry: Γ^t_tx == Γ^t_xt at test point
  if (!check("Γ^t_tx==Γ^t_xt", gam[idx::T][idx::T][idx::X], gam[idx::T][idx::X][idx::T])) {
    return r;
  }

  // Check one spatial Christoffel: Γ^y_yy = -f_s · y
  // At x=(0,ρ,0,0): y=0 → Γ^y_yy = 0
  if (!check("Γ^y_yy@y=0", gam[idx::Y][idx::Y][idx::Y], 0.0)) {
    return r;
  }

  // Check Γ^x_yy = +f_s · x at test point:
  // f_s = 2α/(ρ²ψ), x_1 = ρ
  // Γ^x_yy = 2α/(ρ²ψ) · ρ = 2*0.1/(5M * 1.1) = 0.2/(5.5M)
  const double gamma_x_yy_ref = (2.0 * alpha_ref / ((rho * rho) * psi_ref)) * rho;
  if (!check("Γ^x_yy", gam[idx::X][idx::Y][idx::Y], gamma_x_yy_ref)) {
    return r;
  }

  r.passed = true;
  r.detail = "all metric checks passed at rho=5M (alpha=0.1, psi=1.1, A=9/11)";
  return r;
}
