/**
 * @brief Stub worldline integrator for bodies.
 * The current implementation advances positions with a simple Euler step so the scheduler and
 * engine plumbing can run before the full geodesic solvers land.
 */

#include "body.h"

#include "field.h"

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

void Body::step(const SpacetimeField&, double dl) {
  // Phase-1 placeholder: advance the affine parameter and drift with the current four-velocity.
  WorldlinePoint p = worldline_.latest();
  p.lambda += dl;
  // This is x^μ_{n+1} = x^μ_n + u^μ Δλ, not a physically accurate geodesic update.
  for (int i = 0; i < 4; ++i) p.x[i] += p.u[i] * dl;
  worldline_.push(p);
}

void Body::step_rk4(const SpacetimeField&, double) {}
void Body::step_rk8(const SpacetimeField&, double) {}
void Body::enforce_norm(const SpacetimeField&) {}
