#pragma once

/**
 * @brief Declares validation helpers used outside the hot path.
 * Subsystems expose lightweight integrity checks that can run at startup, after spawning, or on
 * demand without affecting step-time performance.
 */

#include <string>
#include <vector>

/**
 * @brief Result of one subsystem validation pass.
 */
struct ValidationResult {
  bool passed = false;
  std::string subsystem;
  std::string detail;
};

/**
 * @brief Base interface for subsystems that can self-validate.
 */
class Validatable {
 public:
  virtual ValidationResult validate() const = 0;
  virtual ~Validatable() = default;
};

/**
 * @brief Runs a collection of validation checks and reports their results.
 */
class ValidatorRunner {
 public:
  void register_validatable(const Validatable* v);
  std::vector<ValidationResult> run_all() const;
  static void print_results(const std::vector<ValidationResult>& results);

 private:
  std::vector<const Validatable*> validators_;
};
