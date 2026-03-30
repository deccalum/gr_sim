/**
 * @brief Stub step-budget allocator.
 * The current implementation still returns the Balanced preset, but the helper formulas capture the
 * intended strong-field weighting behavior until MasterRoofline takes over.
 */

#include "allocator.h"

#include <algorithm>
#include <cmath>

void BudgetAllocator::calibrate() {
  calibrated_iterations_per_ns_ = 1.0;
}  // STUB

void BudgetAllocator::begin_step(double, double) {
  allocated_ = 0.0;
  report_ = {};
}

ComputeBudget BudgetAllocator::request(const std::string&, double weight, double r_over_M) {
  // Placeholder path: the request is logged, but allocation still resolves to the balanced preset.
  (void)weight;
  (void)r_over_M;
  ++report_.total_requests;
  return ComputeBudget::Balanced();
}

void BudgetAllocator::end_step() {}
double BudgetAllocator::curvature_multiplier(double r_over_M) {
  // The ramp starts at the Schwarzschild ISCO (r = 6M) and grows linearly inward.
  // Dividing by 4 caps the extra weight at +1.0 by the time the trajectory reaches r = 2M.
  return 1.0 + std::max(0.0, 6.0 - r_over_M) / 4.0;
}
