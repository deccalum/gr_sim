/**
 * @brief Stub step-budget allocator.
 * The current implementation still returns the Balanced preset, but the helper formulas capture the
 * intended strong-field weighting behavior until MasterRoofline takes over.
 */

#include "compute_budget.h"

#include <algorithm>
#include <cmath>

void BudgetAllocator::calibrate() {
  calibrated_iterations_per_ns_ = 1.0;
}  // STUB

void BudgetAllocator::begin_step(double, double) {
  allocated_this_step_ = 0.0;
  last_report_ = {};
  log_.clear();
}

ComputeBudget BudgetAllocator::request(const std::string& subsystem, double weight, double r_over_M) {
  // Placeholder path: the request is logged, but allocation still resolves to the balanced preset.
  (void)weight;
  (void)r_over_M;
  ++last_report_.total_requests;
  log_.push_back({subsystem, weight, weight});
  return ComputeBudget::Balanced();
}

void BudgetAllocator::end_step() {}

const BudgetAllocator::StepReport& BudgetAllocator::last_report() const {
  return last_report_;
}

double BudgetAllocator::curvature_weight_multiplier(double r_over_M) {
  // The ramp starts at the Schwarzschild ISCO (r = 6M) and grows linearly inward.
  // Dividing by 4 caps the extra weight at +1.0 by the time the trajectory reaches r = 2M.
  return 1.0 + std::max(0.0, 6.0 - r_over_M) / 4.0;
}
