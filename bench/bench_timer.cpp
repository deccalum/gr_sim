/**
 * @brief Bench timer implementation and self-test checks.
 * Uses CLOCK_MONOTONIC_RAW for measurement and validates resolution, monotonicity, and basic sleep
 * accuracy before benchmark runs proceed.
 */

#include "bench_timer.h"

#include <time.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <numeric>

/**
 * @brief Internal constants for timer validation and variance reporting.
 */

namespace {
constexpr double SELF_TEST_SLEEP_NS = 1'000'000.0;

constexpr double ACCURACY_TOL_HIGH = 0.05;
constexpr double ACCURACY_TOL_LOW = 0.002;

constexpr long RESOLUTION_MAX_NS = 10;

constexpr double HIGH_VARIANCE_THRESHOLD = 0.05;

constexpr int MONOTONIC_READS = 10;
}  // namespace

/**
 * @brief Reads CLOCK_MONOTONIC_RAW and converts to nanoseconds.
 */

uint64_t BenchTimer::now_ns() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

/**
 * @brief Returns elapsed nanoseconds since start().
 */

double BenchTimer::elapsed_ns() const {
  const uint64_t now = now_ns();
  return static_cast<double>(now - start_ns_);
}

void BenchTimer::start() {
  start_ns_ = now_ns();
}

TimerSample BenchTimer::sample() const {
  return TimerSample{elapsed_ns()};
}

/**
 * @brief Queries clock resolution in nanoseconds.
 */

long BenchTimer::resolution_ns() {
  struct timespec res{};
  if (clock_getres(CLOCK_MONOTONIC_RAW, &res) != 0) return -1;
  if (res.tv_sec > 0)
    return static_cast<long>(res.tv_sec) * 1'000'000'000L;
  return static_cast<long>(res.tv_nsec);
}

/**
 * @brief measure — outlier-rejected multi-trial.
 */

TrialResult BenchTimer::measure(int n_trials, const std::function<void()>& fn) {
  return measure(n_trials, 1, fn);
}

/**
 * @brief Runs benchmark function across multiple trials with outlier rejection.
 */

TrialResult BenchTimer::measure(int n_trials, int n_warmup, const std::function<void()>& fn) {
  assert(n_trials >= 3 && "n_trials must be >= 3 for outlier rejection");
  assert(n_warmup >= 0);

  // Warm-up pass: primes caches and branch predictor.
  // Results discarded. Not timed.
  for (int i = 0; i < n_warmup; ++i) {
    fn();
  }

  // Collect raw trial durations.
  std::vector<double> samples;
  samples.reserve(static_cast<size_t>(n_trials));

  BenchTimer t;
  for (int i = 0; i < n_trials; ++i) {
    t.start();
    fn();
    samples.push_back(t.elapsed_ns());
  }

  // Sort for outlier removal and median.
  std::vector<double> sorted = samples;
  std::sort(sorted.begin(), sorted.end());

  TrialResult result{};
  result.n_trials = n_trials;
  result.min_ns = sorted.front();
  result.max_ns = sorted.back();
  result.median_ns = sorted[static_cast<size_t>(n_trials) / 2];

  // Drop min and max. Average the rest.
  // sorted[0] = min (dropped), sorted[n-1] = max (dropped).
  const int n_retained = n_trials - 2;
  result.n_retained = n_retained;

  const double sum = std::accumulate(sorted.begin() + 1, sorted.end() - 1, 0.0);
  result.mean_ns = sum / static_cast<double>(n_retained);

  // Compute stddev of retained trials.
  double sq_sum = 0.0;
  for (int i = 1; i < n_trials - 1; ++i) {
    const double d = sorted[static_cast<size_t>(i)] - result.mean_ns;
    sq_sum += d * d;
  }
  result.stddev_ns =
      (n_retained > 1) ? std::sqrt(sq_sum / static_cast<double>(n_retained - 1)) : 0.0;

  // High-variance flag.
  if (result.mean_ns > 0.0) {
    const double cv = result.stddev_ns / result.mean_ns;
    result.high_variance = (cv > HIGH_VARIANCE_THRESHOLD);
    if (result.high_variance) {
      // LOG_PLACEHOLDER
      fprintf(stderr,
              "[Timer] high variance detected: stddev/mean = %.3f (threshold %.2f)\n"
              "        Consider: taskset -c 0 ./gr_bench  (pin to one core)\n"
              "        Consider: close background processes during benchmarking\n",
              cv, HIGH_VARIANCE_THRESHOLD);
    }
  }

  return result;
}

/**
 * @brief Validates timer behavior before benchmark collection.
 */

TimerSelfTestResult BenchTimer::self_test() {
  TimerSelfTestResult result{};

  // -- Check 1: Resolution --
  const long res_ns = resolution_ns();
  result.resolution_ns = res_ns;

  if (res_ns < 0) {
    result.resolution_check_passed = false;
    result.failure_reason = "clock_getres(CLOCK_MONOTONIC_RAW) failed — clock unavailable.";
    return result;
  }

  if (res_ns > RESOLUTION_MAX_NS) {
    result.resolution_check_passed = false;
    result.failure_reason = "CLOCK_MONOTONIC_RAW resolution is " + std::to_string(res_ns) +
                            "ns (must be <= " + std::to_string(RESOLUTION_MAX_NS) +
                            "ns).\n"
                            "This platform's clock is too coarse for sub-microsecond benchmarking.";
    return result;
  }
  result.resolution_check_passed = true;

  // -- Check 2: Monotonic check --
  {
    uint64_t prev = now_ns();
    bool monotonic = true;
    for (int i = 0; i < MONOTONIC_READS - 1; ++i) {
      const uint64_t curr = now_ns();
      if (curr < prev) {
        monotonic = false;
        break;
      }
      prev = curr;
    }

    if (!monotonic) {
      result.monotonic_check_passed = false;
      result.failure_reason =
          "CLOCK_MONOTONIC_RAW returned a value smaller than a previous read.\n"
          "TSC may be unsynchronized across CPU cores.\n"
          "Fix: pin the process to one core: taskset -c 0 ./gr_bench\n"
          "     or check 'cat /sys/devices/system/clocksource/clocksource0/current_clocksource'\n"
          "     (should be 'tsc')";
      return result;
    }
  }
  result.monotonic_check_passed = true;

  // -- Check 3: Accuracy check --
  {
    const struct timespec sleep_req = {.tv_sec = 0,
                                       .tv_nsec = static_cast<long>(SELF_TEST_SLEEP_NS)};

    result.sleep_requested_ns = SELF_TEST_SLEEP_NS;

    BenchTimer t;
    t.start();
    clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_req, nullptr);
    result.sleep_measured_ns = t.elapsed_ns();

    const double error = result.sleep_measured_ns - result.sleep_requested_ns;
    result.sleep_error_fraction = error / result.sleep_requested_ns;

    // Undershoot: measured < requested. Clock is running fast or broken.
    if (result.sleep_error_fraction < -ACCURACY_TOL_LOW) {
      result.accuracy_check_passed = false;
      result.failure_reason =
          "Timer measured less time than sleep duration — clock may be broken.\n"
          "Measured: " +
          std::to_string(result.sleep_measured_ns) +
          "ns\n"
          "Expected: " +
          std::to_string(result.sleep_requested_ns) +
          "ns\n"
          "This should not happen with CLOCK_MONOTONIC_RAW.";
      return result;
    }

    // Overshoot beyond tolerance: system is under heavy load or RT scheduling
    // is causing large scheduler quanta. Results will be unreliable.
    if (result.sleep_error_fraction > ACCURACY_TOL_HIGH) {
      result.accuracy_check_passed = false;
      result.failure_reason = "Timer overshoot exceeds " +
                              std::to_string(static_cast<int>(ACCURACY_TOL_HIGH * 100)) +
                              "%.\n"
                              "Measured: " +
                              std::to_string(result.sleep_measured_ns) +
                              "ns\n"
                              "Expected: " +
                              std::to_string(result.sleep_requested_ns) +
                              "ns\n"
                              "System may be under heavy load. Close background processes\n"
                              "and retry. If this persists, benchmark results will be unreliable.";
      return result;
    }
  }
  result.accuracy_check_passed = true;

  // -- All checks passed --
  result.passed = true;

  // LOG_PLACEHOLDER: log(LogLevel::Info, "Timer", "self-test passed");
  fprintf(stderr,
          "[Timer] self-test passed\n"
          "        resolution = %ldns\n"
          "        sleep accuracy: measured=%.1fns  requested=%.1fns  error=%.3f%%\n",
          result.resolution_ns, result.sleep_measured_ns, result.sleep_requested_ns,
          result.sleep_error_fraction * 100.0);

  return result;
}
