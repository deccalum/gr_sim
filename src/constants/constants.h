#pragma once

/**
 * @brief Collects derived thresholds used by integrators and scheduling heuristics.
 * All values assume geometric units with G = c = 1 and mirror the canonical Schwarzschild radii
 * used elsewhere in the engine.
 */

#include "units.h"

namespace physics {
inline constexpr double isco_factor = 6.0;            // r_ISCO = 6M — innermost stable circular orbit.
inline constexpr double photon_sphere_factor = 3.0;   // r_ph = 3M — null circular orbit radius.
inline constexpr double schwarzschild_factor = 2.0;   // r_s = 2M — event horizon radius.
inline constexpr double strong_field_threshold = 6.0; // Used to boost fidelity near the ISCO.
inline constexpr double timelike_norm = -1.0;         // Target g_uv u^u u^v = -1 for massive worldlines.
inline constexpr double norm_drift_tolerance = 1e-6;  // Acceptable |g_uv u^u u^v + 1| before reprojection.
}

namespace regime {
inline constexpr double compute_bound_fraction = 0.70;
inline constexpr double memory_bound_fraction = 0.70;
}
