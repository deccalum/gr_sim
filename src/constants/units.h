#pragma once

/**
 * @brief Defines the simulator's unit system, physical constants, and index conventions.
 * Internal calculations use geometric units with G = c = 1, so masses and times are stored as
 * lengths in meters. Coordinate order is fixed as x[0]=t and x[1..3] the spatial axes.
 * @note SI conversions belong only at I/O boundaries. Active physics code should remain in
 * geometric units.
 */

#include <cmath>
#include <cstdint>

namespace si {
/**
 * @brief SI constants used only to convert data at the simulator boundary.
 */
inline constexpr double c   = 2.99792458e8;  // m/s
inline constexpr double G   = 6.67430e-11;   // m^3 kg^-1 s^-2
inline constexpr double c2  = c * c;
inline constexpr double c4  = c2 * c2;
}  // namespace si

namespace geom {
/**
 * @brief Conversion factors between SI and the internal G = c = 1 convention.
 * @details Multiplying a time τ by c expresses it as a length. Multiplying a
 * mass M by G/c² maps it to the geometric length scale
 * ℓ_M = GM/c² (half the Schwarzschild radius rₛ = 2M).
 */
inline constexpr double meters_per_second = 1.0 / si::c;  // c⁻¹: seconds → meters.
inline constexpr double meters_per_kg = si::G / si::c2;   // G/c²: kilograms → meters.

inline constexpr double seconds_per_meter = 1.0 / si::c;
inline constexpr double kg_per_meter = si::c2 / si::G;

inline constexpr double solar_mass_kg = 1.989e30;
inline constexpr double solar_mass_m = solar_mass_kg * meters_per_kg;  // ~1476 m.
}  // namespace geom

namespace idx {
/**
 * @brief Canonical component indices for four-vectors and tensors.
 */
inline constexpr int T = 0;  // Coordinate time.
inline constexpr int X = 1;
inline constexpr int Y = 2;
inline constexpr int Z = 3;
}  // namespace idx

namespace signature {
/**
 * @brief Sign convention for the metric tensor.
 * The simulator uses (-,+,+,+), so timelike four-velocities satisfy u·u < 0.
 */
inline constexpr int s[4] = {-1, +1, +1, +1};
}  // namespace signature

namespace limits {
/**
 * @brief Regime thresholds and normalization targets used by the integrator.
 */
inline constexpr double timelike_norm         = -1.0;  // g_{μν}u^μu^ν = -1 for massive bodies.
inline constexpr double null_norm             = 0.0;   // g_{μν}k^μk^ν = 0 for null trajectories.
inline constexpr double strong_field_r_over_M = 6.0;   // Near or inside the Schwarzschild ISCO (r/M = 6).
inline constexpr double schwarzschild_coeff   = 2.0;   // rₛ = 2M.
inline constexpr double isco_r_over_M         = 6.0;   // Innermost stable circular orbit (r/M = 6).
}  // namespace limits

namespace convert {
/**
 * @brief Boundary conversions between SI values and geometric units.
 * @note These helpers should not appear in hot physics kernels; they exist to sanitize inputs and
 * outputs at the simulator edge.
 */
inline double kg_to_geom(double kg) {
  return kg * geom::meters_per_kg;
}
inline double geom_to_kg(double m) {
  return m * geom::kg_per_meter;
}

inline double seconds_to_geom(double s) {
  return s * si::c;
}
inline double geom_to_seconds(double m) {
  return m / si::c;
}

// In geometric units, velocity is already normalized by c.
inline double ms_to_geom_vel(double v_ms) {
  return v_ms / si::c;
}
inline double geom_vel_to_ms(double v_geom) {
  return v_geom * si::c;
}

inline double schwarzschild_radius_m(double mass_kg) {
  return limits::schwarzschild_coeff * kg_to_geom(mass_kg);
}

inline double schwarzschild_radius(double mass_geom) {
  return limits::schwarzschild_coeff * mass_geom;
}
}  // namespace convert

static_assert(idx::T == 0, "time coordinate must be index 0");
static_assert(idx::X == 1 && idx::Y == 2 && idx::Z == 3, "spatial indices must be 1-3");
static_assert(signature::s[0] == -1, "metric signature must be (-,+,+,+)");
