/**
 * @brief Pre-flight admission check for spawn requests.
 *
 * Two hard limits are enforced before any body or observer is admitted to the simulation:
 *   1. Body cap  — MAX_BODIES is a compile-time limit imposed by the fixed-size arrays in
 *                  Observer::Measurement. Exceeding it would cause out-of-bounds writes.
 *   2. Horizon margin — spawning inside ρ < 0.6 M puts the body inside the photon sphere
 *                  (ρ_ph ≈ 0.62 M in isotropic coords for M=1), where the RK4 step size
 *                  needed for stability becomes impractically small and Christoffel values
 *                  grow without bound. Rejected here rather than silently producing bad data.
 *
 * Observer and Engine requests are always approved until Phase 4.
 */

#include "safe_loader.h"

#include <cmath>

#include "observer.h"
#include "simulation.h"

SpawnResult SafeLoader::check(const SpawnRequest& req, const SimulationState& state) {
  if (req.type == SpawnRequest::Type::Body) {
    if (state.bodies.size() >= static_cast<size_t>(MAX_BODIES)) return SpawnResult::Rejected;

    const auto& bp = std::get<BodyParams>(req.params);
    double rho = std::sqrt(bp.pos[1] * bp.pos[1] + bp.pos[2] * bp.pos[2] + bp.pos[3] * bp.pos[3]);
    if (rho < 0.6)  // horizon safety margin — assumes M=1; generalise when state carries M
      return SpawnResult::Rejected;
  }
  return SpawnResult::Approved;
}