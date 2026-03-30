#pragma once

/**
 * @brief Owns the active spacetime metric and AMR cache for field queries.
 * Bodies and secondary engines sample geometry through this object. Dirty AMR regions are marked
 * for later refresh after each local field evaluation.
 */

#include <memory>

#include "amr_grid.h"
#include "metric.h"

class SpacetimeField {
 public:
  explicit SpacetimeField(std::unique_ptr<MetricProvider> metric);

  /**
   * @brief Evaluates @f$g_{\mu\nu}@f$ and @f$\Gamma^\sigma{}_{\mu\nu}@f$ at a spacetime point.
   * @details The current stub always queries the metric provider directly, then flags a
   * neighborhood for AMR refresh so later cache-aware backends can rebuild local geometry.
   * @param x Spacetime coordinate 4-vector.
   * @param g Output covariant metric @f$g_{\mu\nu}@f$.
   * @param gamma Output Christoffel symbol @f$\Gamma^\sigma{}_{\mu\nu}@f$.
   */
  void eval_at(const Vec4& x, Mat4& g, Gamma& gamma, const AccuracyProfile& acc);

  AMRGrid& grid() { return grid_; }
  MetricProvider& metric_provider() { return *metric_; }

 private:
  std::unique_ptr<MetricProvider> metric_;
  AMRGrid grid_;
};
