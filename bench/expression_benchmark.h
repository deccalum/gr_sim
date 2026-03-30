#pragma once

/**
 * @brief Empirical benchmark runner for expression profiles.
 * Executes a BenchmarkableExpression under timer and PAPI windows, then produces an
 * ExpressionMeasurement for cross-checking analytical FLOP/byte models.
 */

#include <string>
#include <vector>

#include "bench_timer.h"
#include "expression_profile.h"
#include "hardware_peak_probe.h"
#include "papi_wrapper.h"

/**
 * @brief Runtime knobs controlling trial duration, count, and calibration constraints.
 */
struct BenchmarkConfig {
  // Target duration of one PAPI measurement window, in nanoseconds.
  // N (iterations per window) is calibrated so each window runs for ~this long.
  // Must be >> PAPI overhead (~100-200ns) and >> timer resolution (~1ns).
  // Default: 1ms. Increase for high-noise environments.
  double target_window_ns = 1.0e6;

  // Number of trials (independent PAPI windows) per expression.
  // Highest and lowest elapsed_ns trials are discarded.
  // Remaining trials are averaged. Must be >= 3.
  int n_trials = 7;

  // Number of inputs to prepare. Must cover at least calibration_iters.
  // Inputs are cycled: input[i % n_inputs]. Cycling is fine as long as
  // n_inputs is large enough that the cycle doesn't fit in L1 — this
  // preserves realistic cache behaviour for memory-bound expressions.
  int n_inputs = 16384;

  // Minimum N regardless of calibration result.
  // Prevents degenerate cases where calibration produces N=1.
  int min_n = 100;

  // Print progress to stderr as trials complete.
  bool verbose = false;
};

/**
 * @brief Runs calibrated measurement windows and aggregates expression benchmark data.
 */
class ExpressionBenchmark {
 public:
  ExpressionBenchmark(PAPIWrapper& papi, const HardwareProfile& hw, BenchmarkConfig cfg = {});

  // Run the full benchmark for one expression.
  // Returns an ExpressionMeasurement ready for cross-check evaluation.
  // Calls: calibrate_n() → prepare inputs → run n_trials → aggregate.
  ExpressionMeasurement run(const BenchmarkableExpression& expr) const;

  // Run only the calibration phase and return the chosen N.
  // Useful for inspecting iteration counts before a full run.
  int calibrate_n(const BenchmarkableExpression& expr) const;

 private:
  PAPIWrapper& papi_;
  const HardwareProfile& hw_;
  BenchmarkConfig cfg_;

  // Result of one measurement window.
  struct WindowResult {
    PAPICounters counters;
    double elapsed_ns = 0.0;
    int n = 0;
    bool valid = false;
  };

  // Run one PAPI+timer window of N calls to expr.execute().
  // inputs: prepared input buffer (n_inputs × input_size_bytes).
  // output_sink: pre-allocated output buffer (1 × output_size).
  //   Written by execute() to prevent DCE. Reused across iterations.
  WindowResult run_window(const BenchmarkableExpression& expr, const unsigned char* inputs,
                          unsigned char* output_sink, int N) const;

  // Aggregate multiple window results into one ExpressionMeasurement.
  // Outlier rejection: sort by elapsed_ns, drop lowest and highest,
  // sum PAPI counters and elapsed_ns from remaining trials.
  static ExpressionMeasurement aggregate(const ExpressionProfile& profile,
                                         const std::vector<WindowResult>& trials);
};
