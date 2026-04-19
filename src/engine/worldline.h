#pragma once

/**
 * @brief Stores a bounded history of one body's worldline samples.
 * Each sample tracks affine parameter, coordinates, four-velocity, proper time, and the latest
 * normalization check so integration drift can be inspected without unbounded memory growth.
 */

#include <array>
#include <deque>

using Vec4 = std::array<double, 4>;

/**
 * @brief One sampled point on a body's worldline.
 * @note For massive bodies the invariant g_{μν}u^μu^ν = -1 must hold;
 * `norm` stores the raw sampled value so drift can be compared against
 * `AccuracyProfile::norm_tolerance`.
 */
struct WorldlinePoint {
  double lambda = 0.0;  // Affine parameter used by the integrator.
  Vec4 x{};             // Coordinate position x^μ.
  Vec4 u{};             // Four-velocity u^μ = dx^μ/dλ.
  double tau = 0.0;     // Accumulated proper time.
  double norm = 0.0;    // g_{μν}u^μu^ν, expected near -1 for massive bodies.
};

/**
 * @brief Ring buffer for recent worldline samples.
 */
struct Worldline {
  std::deque<WorldlinePoint> points;
  size_t max_size = 4096;  // Old samples are dropped once the history cap is reached.

  void push(WorldlinePoint p) {
    points.push_back(p);
    if (points.size() > max_size) points.pop_front();
  }
  const WorldlinePoint& latest() const { return points.back(); }
  bool empty() const { return points.empty(); }
};
