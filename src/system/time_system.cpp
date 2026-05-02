/**
 * @brief Time-channel bookkeeping for the simulation.
 *
 * Four independent clocks are maintained: wall time (host elapsed), affine parameter (integrator
 * count), coordinate time (metric chart time), and per-body proper time. They evolve at different
 * rates and serve different consumers — keeping them separate prevents the common error of using
 * the integrator's λ as a physical time observable.
 */

#include "time_system.h"

#include <ctime>

/**
 * @brief Captures the wall-clock anchor used by all subsequent `tick_wall()` calls.
 * CLOCK_MONOTONIC is used instead of CLOCK_REALTIME because it is immune to NTP corrections and
 * manual clock adjustments — it only ever moves forward, making elapsed-time arithmetic safe.
 */
TimeSystem::TimeSystem() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  wall_start_s_ = ts.tv_sec + ts.tv_nsec * 1e-9;
}

/**
 * @brief Advances the integrator clocks by one step.
 *
 * `affine_lambda` advances by the raw step size dl — it is the integrator's bookkeeping counter
 * and carries no direct physical meaning on its own.
 *
 * `coordinate_t` advances by `u^t * dl` because the geodesic equation gives dt/dλ = u^t.
 * Near flat space u^t ≈ 1, but deep in a gravitational well u^t grows, encoding gravitational
 * time dilation. The caller passes the reference body's u^t; if there are no bodies the default
 * u_t = 1.0 keeps coordinate_t in step with the affine parameter.
 *
 * @param dl  Affine parameter increment Δλ for this step.
 * @param u_t Time component of the reference body's 4-velocity (dt/dλ). Defaults to 1.0.
 */
void TimeSystem::advance(double dl, double u_t) {
  t_.affine_lambda += dl;
  t_.coordinate_t += u_t * dl;
  ++t_.step_count;
}

/**
 * @brief Updates `wall_time_s` to the elapsed host time since construction.
 * Subtracting the anchor captured in the constructor converts the raw monotonic timestamp into
 * seconds elapsed since the simulation started.
 */
void TimeSystem::tick_wall() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  t_.wall_time_s = ts.tv_sec + ts.tv_nsec * 1e-9 - wall_start_s_;
}

/**
 * @brief Records the accumulated proper time for body `id`.
 * The vector is grown on demand so callers do not need to pre-register IDs. Slots between
 * previously registered IDs and the new one are zero-initialized.
 */
void TimeSystem::set_proper_time(int id, double tau) {
  if (id >= static_cast<int>(proper_times_.size()))
    proper_times_.resize(static_cast<size_t>(id + 1), 0.0);
  proper_times_[static_cast<size_t>(id)] = tau;
}

/**
 * @brief Returns the last recorded proper time for body `id`, or 0.0 if not yet registered.
 */
double TimeSystem::proper_time(int id) const {
  if (id < static_cast<int>(proper_times_.size())) return proper_times_[static_cast<size_t>(id)];
  return 0.0;
}
