#pragma once

/**
 * @brief Declares the adaptive mesh refinement grid used to cache local geometry.
 * Phase 1 still collapses to a single root cell, but the interface already reflects the future
 * dynamic refinement model driven by curvature and accuracy hints.
 */

#include <array>
#include <cstddef>
#include <vector>

using Vec4 = std::array<double, 4>;

struct AccuracyProfile;

/**
 * @brief One AMR cell carrying cached metric and Christoffel samples.
 */
struct AMRCell {
  Vec4 origin{};
  Vec4 extent{};
  int level = 0;
  bool dirty = true;
  // Cached metric and connection at the cell center, kept as raw doubles to avoid extra includes.
  double g[4][4] = {};
  double gamma[4][4][4] = {};
};

/**
 * @brief Container for AMR cells and dirty-region tracking.
 */
class AMRGrid {
 public:
  AMRCell* cell_at(const Vec4& x);
  void refine(const Vec4& center, int target_level);
  void coarsen(const Vec4& center);
  /**
   * @brief Marks cells dirty within a coordinate-space radius of x.
   * The current stub has only a root cell, so any call invalidates the whole cache.
   */
  void mark_dirty_near(const Vec4& x, double radius);
  size_t cell_count() const { return cells_.size(); }
  size_t dirty_count() const;

 private:
  std::vector<AMRCell> cells_;
  AMRCell root_{};  // Phase-1 fallback when no refined cells exist.
};
