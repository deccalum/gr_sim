#pragma once

/**
 * @brief Lightweight step budget allocator used before the full roofline path is wired in.
 * Each request combines a user-facing weight and a curvature proxy to estimate how much iterative
 * work a subsystem should receive during the current step.
 */

#include <string>
#include <vector>

#include "compute_budget.h"

class BudgetAllocator {
 public:
  void begin_step(double cpu_load = 0.0, double gpu_load = 0.0);
  ComputeBudget request(const std::string& subsystem, double weight, double curvature_proxy = 1e9);
  void end_step();
  struct StepReport {
    int total_requests = 0;
    int truncated_count = 0;
    int diverged_count = 0;
    double budget_utilization = 0.0;
  };

  const StepReport& last_report() const { return report_; }
  void calibrate();  // Measures a baseline iterations/ns scale for later requests.
 private:
  double calibrated_iterations_per_ns_ = 1.0;
  double total_budget_ = 0.0;
  double allocated_ = 0.0;
  StepReport report_{};
  static double curvature_multiplier(double r_over_M);
};
