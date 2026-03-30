/**
 * @brief PAPI wrapper implementation and self-test logic.
 * Manages event-set setup, start/stop sampling, and sanity checks that validate FLOP counter
 * linearity before benchmark data is trusted.
 */

#include "papi_wrapper.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifdef HAVE_PAPI
#include <papi.h>
#endif

/**
 * @brief Internal constants.
 */

namespace {

// Self-test iteration counts.
// N_SMALL × 10 must equal N_LARGE for the linearity ratio to be exactly 10.
constexpr int N_WARMUP = 1000;
constexpr int N_SMALL = 10000;
constexpr int N_LARGE = 100000;

// Analytical FLOP counts for the fma_chain kernel.
constexpr long long EXPECTED_FLOPS_SMALL = 2LL * N_SMALL;  // 20,000
constexpr long long EXPECTED_FLOPS_LARGE = 2LL * N_LARGE;  // 200,000

// Linearity check: ratio must be within this fraction of the expected 10.0.
constexpr double LINEARITY_TOLERANCE = 0.01;  // 1%

// FMA check: measured count must be within this fraction of analytical.
constexpr double FMA_TOLERANCE = 0.01;  // 1%

// FMA chain inputs chosen so the sequence does not converge to a fixed point
// and the compiler cannot constant-fold the loop body.
// With b slightly above 1.0 the chain grows slowly — all iterations execute.
constexpr double FMA_A = 1.0;
constexpr double FMA_B = 1.0000001;
constexpr double FMA_C = 1e-10;

// perf_event_paranoid threshold. Must be <= this value for PMU access
// without root privileges.
constexpr int PARANOID_THRESHOLD = 1;

}  // namespace

/**
 * @brief fma_chain — self-test kernel.
 */
//
// Volatile sink prevents the compiler from eliminating the return value.
// __attribute__((noinline)) prevents inlining into the benchmark loop,
// which would allow the compiler to hoist invariants out of the timed region.
//
// Analytical FLOP count: exactly 2 * N (one FMA per iteration = 2 FLOPs).

__attribute__((noinline)) double PAPIWrapper::fma_chain(double a, double b, double c, int N) {
  for (int i = 0; i < N; ++i) {
    a = a * b + c;
  }
  // Prevent dead-code elimination: the caller must write this to a
  // volatile location. Enforced in self_test() below.
  return a;
}

/**
 * @brief read_perf_event_paranoid.
 */

int PAPIWrapper::read_perf_event_paranoid() {
  std::ifstream f("/proc/sys/kernel/perf_event_paranoid");
  if (!f.is_open()) return -1;
  int val = -1;
  f >> val;
  return val;
}

/**
 * @brief init.
 */

bool PAPIWrapper::init() {
#ifndef HAVE_PAPI
  // LOG_PLACEHOLDER: log(LogLevel::Warn, "PAPI", "built without PAPI support");
  fprintf(stderr, "[PAPI] built without PAPI support — proxy measurement only\n");
  available_ = false;
  return false;
#else
  // Check perf_event_paranoid before touching PAPI — gives a clearer
  // diagnostic than PAPI's own error message.
  const int paranoid = read_perf_event_paranoid();
  if (paranoid > PARANOID_THRESHOLD) {
    // LOG_PLACEHOLDER
    fprintf(stderr,
            "[PAPI] /proc/sys/kernel/perf_event_paranoid = %d (must be <= %d)\n"
            "       Fix: echo %d | sudo tee /proc/sys/kernel/perf_event_paranoid\n"
            "       Or:  sudo sysctl -w kernel.perf_event_paranoid=%d\n",
            paranoid, PARANOID_THRESHOLD, PARANOID_THRESHOLD, PARANOID_THRESHOLD);
    available_ = false;
    return false;
  }

  int ret = PAPI_library_init(PAPI_VER_CURRENT);
  if (ret != PAPI_VER_CURRENT) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[PAPI] PAPI_library_init failed: %s\n", PAPI_strerror(ret));
    available_ = false;
    return false;
  }

  if (!open_event_set()) {
    available_ = false;
    return false;
  }

  available_ = true;
  return true;
#endif
}

/**
 * @brief open_event_set  (HAVE_PAPI only).
 */

#ifdef HAVE_PAPI
bool PAPIWrapper::open_event_set() {
  event_set_ = PAPI_NULL;

  int ret = PAPI_create_eventset(&event_set_);
  if (ret != PAPI_OK) {
    fprintf(stderr, "[PAPI] PAPI_create_eventset failed: %s\n", PAPI_strerror(ret));
    return false;
  }

  // Attempt to add all four events. If the hardware counter limit is hit
  // (PAPI_ECNFLCT), fall back to FLOPs-only and warn.
  const int events[4] = {PAPI_DP_OPS, PAPI_L1_DCM, PAPI_L2_DCM, PAPI_L3_TCM};
  const char* names[4] = {"PAPI_DP_OPS", "PAPI_L1_DCM", "PAPI_L2_DCM", "PAPI_L3_TCM"};

  bool all_ok = true;
  for (int i = 0; i < 4; ++i) {
    ret = PAPI_add_event(event_set_, events[i]);
    if (ret != PAPI_OK) {
      // LOG_PLACEHOLDER
      fprintf(stderr, "[PAPI] could not add %s: %s\n", names[i], PAPI_strerror(ret));
      all_ok = false;
      if (i == 0) {
        // PAPI_DP_OPS is mandatory — nothing useful without it.
        fprintf(stderr, "[PAPI] PAPI_DP_OPS unavailable — cannot proceed\n");
        return false;
      }
      // Cache miss events are optional. Mark flops_only and continue.
      flops_only_ = true;
    }
  }

  if (!all_ok) {
    fprintf(stderr,
            "[PAPI] running in FLOPs-only mode — cache miss OI unavailable\n"
            "       This hardware may not support all four counters simultaneously.\n");
  }

  return true;
}
#endif

/**
 * @brief shutdown.
 */

void PAPIWrapper::shutdown() {
#ifdef HAVE_PAPI
  if (!available_) return;

  if (counting_) {
    // Stop counting before cleanup — PAPI requires this.
    long long dummy[4] = {};
    PAPI_stop(event_set_, dummy);
    counting_ = false;
  }

  if (event_set_ != PAPI_NULL) {
    PAPI_cleanup_eventset(event_set_);
    PAPI_destroy_eventset(&event_set_);
    event_set_ = PAPI_NULL;
  }

  available_ = false;
#endif
}

/**
 * @brief start / stop.
 */

void PAPIWrapper::start() {
#ifdef HAVE_PAPI
  if (!available_) return;

  // Reset counters to zero before each window.
  int ret = PAPI_reset(event_set_);
  if (ret != PAPI_OK) {
    fprintf(stderr, "[PAPI] PAPI_reset failed: %s\n", PAPI_strerror(ret));
    return;
  }

  ret = PAPI_start(event_set_);
  if (ret != PAPI_OK) {
    fprintf(stderr, "[PAPI] PAPI_start failed: %s\n", PAPI_strerror(ret));
    return;
  }

  counting_ = true;
#endif
}

PAPICounters PAPIWrapper::stop() {
  PAPICounters result{};

#ifdef HAVE_PAPI
  if (!available_ || !counting_) return result;

  long long values[4] = {};
  int ret = PAPI_stop(event_set_, values);
  if (ret != PAPI_OK) {
    fprintf(stderr, "[PAPI] PAPI_stop failed: %s\n", PAPI_strerror(ret));
    counting_ = false;
    return result;
  }

  counting_ = false;
  result.dp_ops = values[0];      // PAPI_DP_OPS
  result.l1_misses = values[1];   // PAPI_L1_DCM (0 if flops_only_)
  result.l2_misses = values[2];   // PAPI_L2_DCM (0 if flops_only_)
  result.llc_misses = values[3];  // PAPI_L3_TCM (0 if flops_only_)
#endif

  return result;
}

/**
 * @brief self_test.
 */

SelfTestResult PAPIWrapper::self_test() {
  SelfTestResult result{};

  if (!available_) {
    result.failure_reason = "PAPI not available — call init() first";
    return result;
  }

  // Volatile sink: prevents dead-code elimination of fma_chain().
  // The compiler cannot prove this write is unobservable.
  volatile double sink = 0.0;

  // -- Warm-up --
  // Discarded. Ensures caches and branch predictors are in steady state
  // before the measured runs.
  sink = fma_chain(FMA_A, FMA_B, FMA_C, N_WARMUP);

  // -- Run A: N_SMALL --
  start();
  sink = fma_chain(FMA_A, FMA_B, FMA_C, N_SMALL);
  PAPICounters c_small = stop();

  // -- Run B: N_LARGE --
  start();
  sink = fma_chain(FMA_A, FMA_B, FMA_C, N_LARGE);
  PAPICounters c_large = stop();

  (void)sink;  // suppress unused-variable warning after final use

  result.dp_ops_n_small = c_small.dp_ops;
  result.dp_ops_n_large = c_large.dp_ops;

  // -- Check 1: Zero check --
  // If dp_ops is zero, the counter isn't reading anything useful.
  // This check is implicitly covered by the FMA check below, but naming
  // it separately gives a clearer diagnostic.
  if (c_small.dp_ops == 0) {
    result.zero_check_passed = false;
    result.failure_reason =
        "dp_ops returned zero — counter may be blocked.\n"
        "Check: /proc/sys/kernel/perf_event_paranoid (must be <= 1)\n"
        "Check: running inside a VM or container may block PMU access.";
    return result;
  }
  result.zero_check_passed = true;

  // -- Check 2: FMA check --
  // Measured dp_ops for N_SMALL must be within FMA_TOLERANCE of
  // EXPECTED_FLOPS_SMALL (= 2 * N_SMALL = 20,000).
  result.fma_ratio =
      static_cast<double>(c_small.dp_ops) / static_cast<double>(EXPECTED_FLOPS_SMALL);

  const double fma_dev = std::abs(result.fma_ratio - 1.0);
  if (fma_dev > FMA_TOLERANCE) {
    result.fma_check_passed = false;

    if (c_small.dp_ops < EXPECTED_FLOPS_SMALL) {
      // Compiler emitted separate MUL+ADD, not FMA — counts as 1 FLOP each
      // but PAPI_DP_OPS may report them differently. Or AVX widened ops.
      result.failure_reason =
          "FMA count lower than expected. Compiler may not have emitted FMA.\n"
          "Fix: compile gr_bench with -mfma -mfpmath=sse -O2\n"
          "     or -march=native (enables all supported ISA extensions).";
      result.advisory =
          "If -mfma is already set, inspect assembly for the fma_chain loop:\n"
          "  objdump -d gr_bench | grep -A20 'fma_chain'\n"
          "  Look for 'vfmadd' instructions. If absent, FMA is not emitting.";
    } else {
      // More FLOPs than expected — vectorization widened the loop.
      // PAPI_DP_OPS should account for this, so this is unexpected.
      result.failure_reason =
          "FMA count higher than expected — unexpected vectorization effect.\n"
          "Inspect assembly for fma_chain. Consider compiling with -fno-tree-vectorize\n"
          "for the benchmark translation unit to keep the kernel scalar.";
    }
    return result;
  }
  result.fma_check_passed = true;

  // -- Check 3: Linearity check --
  // dp_ops for N_LARGE must be exactly 10× dp_ops for N_SMALL.
  // Non-linearity indicates counter multiplexing or OS interference.
  if (c_small.dp_ops == 0) {
    // Already caught above, but guard division.
    result.linearity_check_passed = false;
    result.failure_reason = "dp_ops_small is zero — cannot compute linearity ratio.";
    return result;
  }

  result.linearity_ratio =
      static_cast<double>(c_large.dp_ops) / static_cast<double>(c_small.dp_ops);

  const double lin_dev = std::abs(result.linearity_ratio - 10.0) / 10.0;
  if (lin_dev > LINEARITY_TOLERANCE) {
    result.linearity_check_passed = false;
    result.failure_reason =
        "Counter does not scale linearly with iteration count.\n"
        "Ratio: " +
        std::to_string(result.linearity_ratio) +
        " (expected 10.0)\n"
        "Possible causes:\n"
        "  - PMU counter multiplexing with time-slicing distorting counts\n"
        "  - OS scheduling interruptions during the benchmark window\n"
        "  - Counter overflow (unlikely at these iteration counts)\n"
        "Try: taskset -c 0 ./gr_bench  (pin to one core, reduce scheduler noise)\n"
        "Try: sudo chrt -f 99 ./gr_bench  (real-time priority)";
    return result;
  }
  result.linearity_check_passed = true;

  // -- All checks passed --
  result.passed = true;

  if (flops_only_) {
    result.advisory =
        "Running in FLOPs-only mode. Cache miss counters unavailable.\n"
        "OI will be estimated from analytical byte count, not measured.";
  }

  // LOG_PLACEHOLDER: log(LogLevel::Info, "PAPI", "self-test passed");
  fprintf(stderr,
          "[PAPI] self-test passed\n"
          "       dp_ops N=%-7d  measured=%-8lld  expected=%-8lld  ratio=%.4f\n"
          "       linearity ratio = %.4f (expected 10.0, tolerance 1%%)\n",
          N_SMALL, c_small.dp_ops, EXPECTED_FLOPS_SMALL, result.fma_ratio, result.linearity_ratio);

  return result;
}
