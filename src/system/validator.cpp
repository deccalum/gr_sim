/**
 * @brief Minimal validator runner implementation.
 */

#include "validator.h"

#include <cstdio>

void ValidatorRunner::register_validatable(const Validatable* v) {
  validators_.push_back(v);
}

std::vector<ValidationResult> ValidatorRunner::run_all() const {
  std::vector<ValidationResult> results;
  for (const auto* v : validators_) results.push_back(v->validate());
  return results;
}

void ValidatorRunner::print_results(const std::vector<ValidationResult>& results) {
  for (const auto& r : results)
    fprintf(stdout, "  [%s] %s: %s\n", r.passed ? "PASS" : "FAIL", r.subsystem.c_str(),
            r.detail.c_str());
}
