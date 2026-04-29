/**
 * @brief Stub worldline integrator for bodies.
 * The current implementation advances positions with a simple Euler step so the scheduler and
 * engine plumbing can run before the full geodesic solvers land.
 */

#include "body.h"

#include <array>
#include <cassert>
#include <cmath>

#include "../constants/constants.h"
#include "../constants/units.h"
#include "field.h"

namespace {

/** @brief Returns a + b component-wise. */
inline Vec4 vec4_add(const Vec4& a, const Vec4& b) {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
}

/** @brief Returns a * s component-wise. */
inline Vec4 vec4_scale(const Vec4& a, double s) {
  return {a[0] * s, a[1] * s, a[2] * s, a[3] * s};
}

/**
 * @brief Packed (x, u) state vector for the geodesic ODE.
 * Used as both the state and the increment kᵢ in RK staging, so the same arithmetic helpers
 * operate on both.
 */
struct GeodesicState {
  Vec4 x;  // coordinate position x^μ
  Vec4 u;  // 4-velocity u^μ = dx^μ/dλ
};

/** @brief Returns s + k component-wise (used to form RK trial points). */
inline GeodesicState state_add(const GeodesicState& s, const GeodesicState& k) {
  return {vec4_add(s.x, k.x), vec4_add(s.u, k.u)};
}

/** @brief Returns k * s component-wise (used to weight RK stage increments). */
inline GeodesicState state_scale(const GeodesicState& k, double s) {
  return {vec4_scale(k.x, s), vec4_scale(k.u, s)};
}

/**
 * @brief Evaluates the geodesic ODE right-hand side at state @p s.
 *
 * The geodesic equation is
 *   dx^μ / dλ = u^μ,
 *   du^σ / dλ = -Γ^σ_{μν} u^μ u^ν.
 * The returned GeodesicState holds the derivatives (dx/dλ, du/dλ) and is used directly as a
 * stage increment kᵢ by the RK integrators.
 *
 * @param field Spacetime field queried for g_{μν} and Γ^σ_{μν} at s.x.
 * @param s     Current state (x^μ, u^μ).
 * @param acc   Accuracy profile forwarded to field.eval_at().
 * @return      d/dλ of the geodesic state at s.
 */
GeodesicState rhs(const SpacetimeField& field, const GeodesicState& s, const AccuracyProfile& acc) {
  GeodesicState k{};

  k.x = s.u;  // dx^μ/dλ = u^μ

  Mat4 g{};
  Gamma gamma{};
  field.eval_at(s.x, g, gamma, acc);

  // du^μ/dλ = -Γ^μ_νσ u^ν u^σ
  for (int sigma = 0; sigma < 4; ++sigma) {
    double contraction = 0.0;
    for (int mu = 0; mu < 4; ++mu) {
      for (int nu = 0; nu < 4; ++nu) {
        contraction += gamma[sigma][mu][nu] * s.u[mu] * s.u[nu];
      }
    }
    k.u[sigma] = -contraction;
  }

  return k;
}

/**
 * @brief Returns g_{μν}u^μu^ν.
 * Negative for timelike worldlines (signature (-,+,+,+)).
 */
double norm_sq(const Mat4& g, const Vec4& u) {
  double n = 0.0;
  for (int mu = 0; mu < 4; ++mu)
    for (int nu = 0; nu < 4; ++nu) n += g[mu][nu] * u[mu] * u[nu];
  return n;
}

}  // namespace

/**
 * @brief Constructs a body and pushes its initial worldline point.
 * @param mass  Gravitational mass in geometric units; 0 for photons.
 * @param pos   Initial 4-position x^μ.
 * @param vel   Initial 4-velocity u^μ (caller must ensure correct norm).
 * @param id    Unique integer identifier assigned by the simulation.
 * @param acc   Accuracy profile controlling integrator order and norm enforcement.
 */
Body::Body(double mass, Vec4 pos, Vec4 vel, uint64_t id, AccuracyProfile acc)
    : mass_(mass), id_(id), acc_(acc) {
  WorldlinePoint p{};
  p.x = pos;
  p.u = vel;
  worldline_.push(p);
}

const Vec4& Body::position() const {
  return worldline_.latest().x;
}
const Vec4& Body::velocity() const {
  return worldline_.latest().u;
}
const Worldline& Body::worldline() const {
  return worldline_;
}

void Body::step(const SpacetimeField& field, double dl) {
  switch (acc_.integrator_order) {
    case 4:
      step_rk4(field, dl);
      break;
    case 8:
      step_rk8(field, dl);
      break;
    default:
      step_rk4(field, dl);
      break;  // Default to RK4
  }

  const_cast<SpacetimeField&>(field).grid().mark_dirty_near(
      worldline_.latest().x, 1.0);  // Phase-1 behavior: any sample invalidates a fixed-radius
                                    // neighborhood until cached AMR arrives.
}

void Body::step_rk4(const SpacetimeField& field, double dl) {
  const WorldlinePoint& prev = worldline_.latest();
  GeodesicState s0{prev.x, prev.u};

  GeodesicState k1 = state_scale(rhs(field, s0, acc_), dl);
  GeodesicState k2 = state_scale(rhs(field, state_add(s0, state_scale(k1, 0.5)), acc_), dl);
  GeodesicState k3 = state_scale(rhs(field, state_add(s0, state_scale(k2, 0.5)), acc_), dl);
  GeodesicState k4 = state_scale(rhs(field, state_add(s0, k3), acc_), dl);

  WorldlinePoint next{};
  const double inv6 = 1.0 / 6.0;
  for (int i = 0; i < 4; ++i) {
    next.x[i] = s0.x[i] + (k1.x[i] + 2.0 * k2.x[i] + 2.0 * k3.x[i] + k4.x[i]) * inv6;
    next.u[i] = s0.u[i] + (k1.u[i] + 2.0 * k2.u[i] + 2.0 * k3.u[i] + k4.u[i]) * inv6;
  }
  next.lambda = prev.lambda + dl;

  if (acc_.enforce_norm) {
    enforce_norm_at(field, next);
  }

  if (mass_ > 0.0) {
    Mat4 g{};
    Gamma gamma_unused{};
    field.eval_at(next.x, g, gamma_unused, acc_);
    const double n = norm_sq(g, next.u);
    next.tau = prev.tau + (n < 0.0 ? std::sqrt(-n) * dl
                                   : 0.0);  // Only accumulate proper time if norm is timelike.
    next.norm = n;
  } else {
    Mat4 g{};
    Gamma gamma_unused{};
    field.eval_at(next.x, g, gamma_unused, acc_);
    next.norm = norm_sq(g, next.u);
    next.tau = 0.0;  // Massless bodies have zero proper time along their worldlines.
  }

  worldline_.push(next);
}

void Body::enforce_norm_at(const SpacetimeField& field, WorldlinePoint& p) const {
  Mat4 g{};
  Gamma gamma_unused{};
  field.eval_at(p.x, g, gamma_unused, acc_);
  
  const double n = norm_sq(g, p.u);
  const double target = mass_ > 0.0 ? physics::timelike_norm : 0.0;
  const double drift = std::abs(n - target);

  if (drift < acc_.norm_tolerance) return; // within tolerance - no action

  if (mass_ > 0.0 && n < 0.0) {
    const double scale = std::sqrt(std::abs(target / n));
    for (int i = 0; i < 4; ++i) p.u[i] *= scale;
  }
}

void Body::enforce_norm(const SpacetimeField& field) {
  WorldlinePoint p = worldline_.latest();
  enforce_norm_at(field, p);
  worldline_.points.back() = p;  // Update the latest point with the enforced norm.
}
void Body::step_rk8(const SpacetimeField& field, double dl) {
  // STUB: Fallback back to RK4 until implemented
  step_rk4(field, dl);
}
