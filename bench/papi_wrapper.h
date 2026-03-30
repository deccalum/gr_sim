#pragma once

/**
 * @brief Thin RAII wrapper over PAPI counters for FLOP and cache-miss sampling.
 * Supports full counters when available and degrades cleanly to proxy mode when PMU access is
 * unavailable.
 */

#include <cstdint>
#include <string>

/**
 * @brief Raw counter values collected from one measurement window.
 */
struct PAPICounters {
  long long dp_ops = 0;      // double-precision FP operations (PAPI_DP_OPS)
                             // FMA counts as 2. Abstracts scalar + packed.
  long long l1_misses = 0;   // L1 data cache misses       (PAPI_L1_DCM)
  long long l2_misses = 0;   // L2 data cache misses       (PAPI_L2_DCM)
  long long llc_misses = 0;  // last-level cache misses    (PAPI_L3_TCM)

  // Derived: bytes transferred from DRAM this window.
  // One LLC miss = one 64-byte cache line fetched from DRAM.
  long long dram_bytes() const { return llc_misses * 64; }

  // Derived: bytes transferred from L2 this window.
  long long l2_bytes() const { return l2_misses * 64; }

  // Derived: operational intensity vs DRAM traffic.
  // Returns 0.0 if dram_bytes == 0 (avoid divide-by-zero on cache-resident runs).
  double oi_dram() const {
    const long long b = dram_bytes();
    return (b > 0) ? static_cast<double>(dp_ops) / static_cast<double>(b) : 0.0;
  }

  // Derived: operational intensity vs L2 traffic.
  double oi_l2() const {
    const long long b = l2_bytes();
    return (b > 0) ? static_cast<double>(dp_ops) / static_cast<double>(b) : 0.0;
  }
};

/**
 * @brief Diagnostics emitted by PAPI self-test calibration checks.
 */
struct SelfTestResult {
  bool passed = false;

  // Individual check results
  bool zero_check_passed = false;       // counters returned non-zero
  bool linearity_check_passed = false;  // count scales linearly with N
  bool fma_check_passed = false;        // FMA counted as 2 FLOPs

  // Measured values (useful for diagnosing WARN-level divergence)
  long long dp_ops_n_small = 0;  // dp_ops for N=10,000
  long long dp_ops_n_large = 0;  // dp_ops for N=100,000
  double linearity_ratio = 0.0;  // dp_ops_large / dp_ops_small (expect 10.0)
  double fma_ratio = 0.0;        // dp_ops_small / expected_small (expect 1.0)

  // Human-readable failure description. Empty if passed.
  std::string failure_reason;

  // Advisory messages (non-fatal, e.g. compile flag suggestions).
  std::string advisory;
};

/**
 * @brief Lifecycle and readout API for PAPI event sets.
 */
class PAPIWrapper {
 public:
  PAPIWrapper() = default;
  ~PAPIWrapper() { shutdown(); }

  // Non-copyable — event sets are not copyable resources.
  PAPIWrapper(const PAPIWrapper&) = delete;
  PAPIWrapper& operator=(const PAPIWrapper&) = delete;

  // Initialize PAPI and open event set.
  // Returns true on success. On failure, logs reason via the diagnostics
  // callback and sets available() to false — harness may continue with proxy.
  //
  // If PAPI_add_event fails due to hardware counter limits (PAPI_ECNFLCT),
  // falls back to a reduced event set (FLOPs only) and logs a warning.
  // Cache miss counters will be zero in that case.
  bool init();

  // Release PAPI resources. Safe to call even if init() failed or was
  // never called.
  void shutdown();

  // Reset counters to zero and begin counting.
  // Must call init() first. Undefined behaviour if not initialized.
  void start();

  // Stop counting and return accumulated values since last start().
  // Counters are not reset — call start() again to begin a new window.
  PAPICounters stop();

  // Run the three-check self-test. See SelfTestResult for detail.
  // Does not require start()/stop() to have been called first.
  // Leaves wrapper in a clean state — safe to call start() after.
  //
  // Should be called once after init() and before any benchmark runs.
  // If it returns false, counter values from this wrapper are not trustworthy.
  SelfTestResult self_test();

  // True if PAPI initialized successfully and counters are readable.
  bool available() const { return available_; }

  // True if init() succeeded but fell back to FLOPs-only (cache miss
  // counters unavailable). Benchmark can still cross-check OI analytically
  // but cannot observe cache behaviour directly.
  bool flops_only() const { return flops_only_; }

  // Read /proc/sys/kernel/perf_event_paranoid.
  // Returns the value, or -1 if unreadable.
  // Called internally by init() to produce diagnostic messages.
  static int read_perf_event_paranoid();

 private:
  bool available_ = false;
  bool flops_only_ = false;
  bool counting_ = false;

#ifdef HAVE_PAPI
  int event_set_ = -1;  // PAPI_NULL, assigned in init()

  // Attempt to add all four events. On PAPI_ECNFLCT, retry with FLOPs only.
  // Returns true if at least FLOPs were added successfully.
  bool open_event_set();
#endif

  // The self-test kernel. Declared here so the compiler sees the same
  // translation unit as the benchmark — prevents cross-TU inlining surprises.
  // N iterations of a = a * b + c (1 FMA = 2 FLOPs).
  // Analytical FLOP count: exactly 2 * N.
  // Returns result to prevent dead-code elimination by caller.
  static double fma_chain(double a, double b, double c, int N);
};
