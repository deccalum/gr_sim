#pragma once

/**
 * @brief Observer reference frame that converts raw simulation state into display-ready
 * measurements.
 *
 * Display systems (ConsoleUI, future Vulkan camera) consume `Observer::Measurement` snapshots
 * instead of reading body worldlines directly. This keeps time dilation, visibility, and future
 * redshift/apparent-position effects centralized in one place and decoupled from rendering.
 */

#include <array>
#include <cstdint>

#include "engine/metric.h"

/**
 * @brief Hard cap on simultaneously tracked bodies.
 * @note Sized to fit Observer::Measurement arrays on the stack without heap allocation.
 *       SafeLoader enforces this limit at spawn time so no body can be admitted past it.
 */
static constexpr int MAX_BODIES = 64;

struct SimulationState;

/**
 * @brief Observer state and cached relativistic measurements for one reference frame.
 *
 * An observer is a timelike worldline from which other bodies are measured. For Phase 2 the
 * observer is static in the spatial coordinates; the measurement loop computes gravitational
 * (lapse-ratio) time dilation. Kinetic dilation for moving observers is a Phase 3 extension.
 */
class Observer {
 public:
  /**
   * @brief One complete measurement snapshot from this observer's reference frame.
   *
   * All per-body arrays are indexed parallel to the body list in SimulationState and are
   * zero-initialized for slots beyond the current body count.
   *
   * @note `dilation` reports *gravitational* time dilation only — the ratio of lapse factors
   *       A(ρ_body)/A(ρ_obs). A moving body also has a kinetic (special-relativistic) contribution
   *       that reduces its proper-time rate further; that correction is added in Phase 3.
   */
  struct Measurement {
    double proper_time = 0.0;                         // Observer's own accumulated proper time τ_obs.
    std::array<double, MAX_BODIES> r_of_body = {};    // Coordinate distance to each body (not proper distance).
    std::array<double, MAX_BODIES> tau_of_body = {};  // Proper time accumulated by each body.
    std::array<double, MAX_BODIES> dilation = {};     // dτ_body/dτ_obs — gravitational time dilation ratio.
    std::array<bool, MAX_BODIES> visible = {};        // Placeholder; real line-of-sight test is Phase 3.
  };

  /**
   * @brief Constructs an observer at the given position and velocity.
   * @param pos Isotropic Cartesian 4-position x^μ = (t, x, y, z).
   * @param vel 4-velocity u^μ; for a static observer u^i = 0 and u^t = 1/A(ρ).
   * @param id  Unique identifier assigned by ObserverManager.
   * @param M   Black hole mass in geometric units (G = c = 1), used for lapse factor computation.
   */
  Observer(Vec4 pos, Vec4 vel, uint64_t id, double M);

  /**
   * @brief Evaluates all observable quantities relative to this frame.
   *
   * For each body in `state.bodies`, fills coordinate distance, proper time, and gravitational
   * time dilation into the returned Measurement. The observer's own proper time is approximated
   * as `A(ρ_obs) * coordinate_t`, exact for a static worldline.
   *
   * @param state Current simulation state (bodies, time channels).
   * @return Fully populated Measurement snapshot at the current affine step.
   */
  Measurement measure(const SimulationState& state) const;

  Vec4& position() { return pos_; }
  Vec4& velocity() { return vel_; }
  const Vec4& position() const { return pos_; }
  const Vec4& velocity() const { return vel_; }
  uint64_t id() const { return id_; }

 private:
  Vec4 pos_, vel_;
  uint64_t id_;
  double M_;
};
