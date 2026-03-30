/**
 * @brief Stub roofline evaluator.
 * The final version should combine FLOP demand, memory traffic, and transfer cost into a single
 * contention model. For now it mirrors each CPU expression's requested weight into a default slice.
 */

#include "roofline.h"

MasterRoofline::MasterRoofline(const HardwareProfile& hw) : hw_(hw) {}

std::vector<BudgetSlice> MasterRoofline::evaluate(const ActiveComputeManifest& manifest) const {
  std::vector<BudgetSlice> slices;
  // Placeholder behavior: propagate requested weights directly until the contention model exists.
  for (const auto& expr : manifest.cpu_expressions()) {
    BudgetSlice s{};
    s.expression_id = expr.id;
    s.max_iterations = 64;
    s.attainable_flops_s = hw_.peak_flops_per_s * expr.weight;
    s.weight_granted = expr.weight;
    slices.push_back(s);
  }
  return slices;
}
