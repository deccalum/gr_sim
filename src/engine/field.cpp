/**
 * @brief Stub field implementation that forwards every query to the active metric provider.
 */

#include "field.h"

SpacetimeField::SpacetimeField(std::unique_ptr<MetricProvider> metric)
    : metric_(std::move(metric)) {}

void SpacetimeField::eval_at(const Vec4& x, Mat4& g, Gamma& gamma,
                             const AccuracyProfile& acc) const {
  metric_->metric(x, g, acc);
  metric_->christoffel(x, gamma, acc);
  // Dirty-marking is the caller's responsibility after position is committed.
  // Body::step() calls grid().mark_dirty_near() after worldline_.push().
}
