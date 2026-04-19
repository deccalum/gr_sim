#pragma once

/**
 * @brief High-resolution timing utilities for benchmark windows.
 * Uses CLOCK_MONOTONIC_RAW so measurements are stable against wall-clock adjustments and suitable
 * for sub-millisecond profiling in the benchmark harness.
 * @note On multi-socket systems, pinning the process to one core reduces TSC synchronization noise.
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief One elapsed-time measurement in nanoseconds.
 */
struct TimerSample {
  double elapsed_ns = 0.0;

  // Convenience conversions.
  double elapsed_us() const { return elapsed_ns * 1e-3; }
  double elapsed_ms() const { return elapsed_ns * 1e-6; }
  double elapsed_s() const { return elapsed_ns * 1e-9; }
};

/**
 * @brief Aggregated statistics from multi-trial timing with outlier rejection.
 */
struct TrialResult {
  // Central estimate: mean of trials after outlier removal (min + max dropped).
  double mean_ns = 0.0;
  double stddev_ns = 0.0;  // stddev of retained trials
  double min_ns = 0.0;     // over all trials including dropped
  double max_ns = 0.0;     // over all trials including dropped
  double median_ns = 0.0;

  int n_trials = 0;    // total trials run
  int n_retained = 0;  // trials used in mean (n_trials - 2)

  // True if stddev/mean > 0.05 — suggests OS interference or clock instability.
  // Results are still valid but should be treated with caution.
  bool high_variance = false;

  // Convenience: throughput given a known operation count.
  // E.g. ops_per_second(flop_count) gives achieved FLOP/s.
  double ops_per_second(double op_count) const {
    return (mean_ns > 0.0) ? (op_count / (mean_ns * 1e-9)) : 0.0;
  }
};

/**
 * @brief Detailed outcome of timer self-validation checks.
 */
struct TimerSelfTestResult {
  bool passed = false;

  // Individual check results.
  bool resolution_check_passed = false;  // CLOCK_MONOTONIC_RAW res <= 10ns
  bool accuracy_check_passed = false;    // sleep(1ms) measured within tolerance
  bool monotonic_check_passed = false;   // ten sequential reads are non-decreasing

  // Measured values.
  long resolution_ns = 0;  // reported by clock_getres
  double sleep_requested_ns = 0.0;
  double sleep_measured_ns = 0.0;
  double sleep_error_fraction = 0.0;  // |measured - requested| / requested

  // Human-readable failure description. Empty if passed.
  std::string failure_reason;

  // Advisory (non-fatal warnings).
  std::string advisory;
};

/**
 * @brief Stopwatch-style API plus trial helpers for robust microbenchmark timing.
 */
class BenchTimer {
 public:
  BenchTimer() = default;
  ~BenchTimer() = default;

  // Begin a timing window. Resets any previous elapsed time.
  void start();

  // Return nanoseconds elapsed since last start().
  // Does not stop the timer — may be called repeatedly.
  double elapsed_ns() const;

  // Return elapsed as a TimerSample.
  TimerSample sample() const;

  // -----------------------------------------------------------------------
  // Static measurement helpers
  // -----------------------------------------------------------------------

  // Run fn() n_trials times and return outlier-rejected statistics.
  // Warm-up: fn() is called once before trials begin (discarded).
  // Outlier rejection: min and max trials are dropped, rest are averaged.
  // n_trials must be >= 3. Recommended: 7.
  static TrialResult measure(int n_trials, const std::function<void()>& fn);

  // Variant with explicit warm-up count.
  static TrialResult measure(int n_trials, int n_warmup, const std::function<void()>& fn);

  // -----------------------------------------------------------------------
  // Self-test
  // -----------------------------------------------------------------------

  // Run three checks and return results.
  // Should be called once at harness startup before any benchmarks.
  // Does not depend on PAPI — fully independent.
  static TimerSelfTestResult self_test();

  // -----------------------------------------------------------------------
  // Resolution query
  // -----------------------------------------------------------------------

  // Return clock resolution in nanoseconds as reported by clock_getres.
  // Returns -1 if the query fails.
  static long resolution_ns();

 private:
  // Nanoseconds at last start() call.
  // Stored as uint64_t to avoid floating-point accumulation error.
  uint64_t start_ns_ = 0;

  // Read CLOCK_MONOTONIC_RAW and return nanoseconds since epoch.
  static uint64_t now_ns();
};
