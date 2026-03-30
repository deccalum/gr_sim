/**
 * @brief Stub field implementation that forwards every query to the active metric provider.
 */

#include "field.h"

SpacetimeField::SpacetimeField(std::unique_ptr<MetricProvider> metric)
    : metric_(std::move(metric)) {}

void SpacetimeField::eval_at(const Vec4& x, Mat4& g, Gamma& gamma, const AccuracyProfile& acc) {
  metric_->metric(x, g, acc);
  metric_->christoffel(x, gamma, acc);
  // Phase-1 behavior: any sample invalidates a fixed-radius neighborhood until cached AMR arrives.
  grid_.mark_dirty_near(x, 1.0);
}
