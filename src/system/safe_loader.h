#pragma once

/**
 * @brief Pre-flight admission gate for spawn requests.
 *
 * `check()` is called by `SpawnQueue::process()` before constructing any body or observer.
 * It enforces hard resource limits so the integrator never receives a structurally invalid
 * simulation state — rejecting requests here is safer than silently producing bad data or
 * crashing mid-step.
 */

#include "spawn_queue.h"

class SimulationState;

class SafeLoader {
 public:
  /**
   * @brief Tests whether a spawn request can be safely admitted.
   *
   * Current checks (Body type only):
   *   - Body count cap: rejects if `state.bodies.size() >= MAX_BODIES` to prevent out-of-bounds
   *     writes into the fixed-size arrays of `Observer::Measurement`.
   *   - Horizon safety margin: rejects spawn positions at isotropic ρ < 0.6 M, placing a buffer
   *     outside the photon sphere (ρ_ph ≈ 0.62 M for M=1). Integration stability collapses
   *     rapidly inside this radius as Christoffel values diverge.
   *
   * Observer and Engine requests are always approved; their resource limits are Phase 4 work.
   *
   * @param req   The pending spawn request to evaluate.
   * @param state Current simulation state used to check live resource usage.
   * @return `Approved` if the request passes all checks; `Rejected` otherwise.
   */
  SpawnResult check(const SpawnRequest& req, const SimulationState& state);
};
