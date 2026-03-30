#pragma once

/**
 * @brief Tracks the active expressions competing for compute within a sub-step.
 * The roofline model reads this manifest to estimate aggregate FLOP and memory pressure before it
 * assigns iteration budgets.
 */

#include <cstdint>
#include <string>
#include <vector>

struct ExpressionProfileFwd;  // forward — full type in bench/expression_profile.h

/**
 * @brief Describes one currently active expression for roofline accounting.
 */
struct ActiveExpression {
  uint64_t id;
  std::string subsystem;
  double weight;           // from AccuracyProfile
  double curvature_proxy;  // r/M — amplifies weight near horizon
  bool gpu_target;
  double flop_count;  // analytical (from ExpressionProfile)
  double bytes_read;
  double bytes_written;
};

/**
 * @brief Separates CPU-target and GPU-target expressions for roofline evaluation.
 */
class ActiveComputeManifest {
 public:
  void register_expression(ActiveExpression e);
  void unregister(uint64_t id);
  void clear();
  struct ResourceDemand {
    double total_flops = 0.0;
    double total_bytes_read = 0.0;
    double total_bytes_written = 0.0;
  };
  ResourceDemand cpu_demand() const;
  ResourceDemand gpu_demand() const;
  const std::vector<ActiveExpression>& cpu_expressions() const { return cpu_; }
  const std::vector<ActiveExpression>& gpu_expressions() const { return gpu_; }

 private:
  std::vector<ActiveExpression> cpu_, gpu_;
};
