#pragma once

/**
 * @brief Declares the roofline-based allocator that translates active demand into iteration slices.
 * Phase 1 models CPU, GPU, and transfer pressure separately, then synchronizes once per step to
 * decide how much work each expression can realistically sustain.
 */

#include <vector>

#include "active_compute.h"
#include "compute_budget.h"
#include "hardware_profile.h"

/**
 * @brief Roofline-derived budget grant for one active expression.
 */
struct BudgetSlice {
  uint64_t expression_id = 0;
  int max_iterations = 64;
  double convergence_eps = 1e-9;
  double divergence_max = 1e15;
  double attainable_flops_s = 0.0;
  double weight_granted = 0.0;
};

/**
 * @brief Estimates contention and produces per-expression roofline budgets.
 */
class MasterRoofline {
 public:
  explicit MasterRoofline(const HardwareProfile& hw);
  std::vector<BudgetSlice> evaluate(const ActiveComputeManifest& manifest) const;

  struct ContentionReport {
    double cpu_compute_pressure = 0.0;  // Fraction of sustained CPU FLOP budget requested.
    double cpu_memory_pressure = 0.0;   // Fraction of sustained CPU bandwidth requested.
    double gpu_compute_pressure = 0.0;  // Fraction of sustained GPU FLOP budget requested.
    double gpu_memory_pressure = 0.0;   // Fraction of sustained GPU bandwidth requested.
    double transfer_pressure = 0.0;     // Fraction of PCIe or host-device transfer bandwidth used.
    bool transfer_bottleneck = false;
  };

  const ContentionReport& last_contention() const { return contention_; }

 private:
  const HardwareProfile& hw_;
  mutable ContentionReport contention_{};
};
