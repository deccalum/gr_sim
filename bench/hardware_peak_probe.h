#pragma once

/**
 * @brief Measures sustained hardware limits used by the roofline model.
 * Provides peak FLOP/s, peak bandwidth, and cache-related thresholds consumed by runtime
 * scheduling and analytical cross-check logic.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bench_timer.h"
#include "papi_wrapper.h"

/**
 * @brief Cache size triplet for L1/L2/L3 detection and reporting.
 */
struct CacheSizes {
  size_t l1_bytes = 0;
  size_t l2_bytes = 0;
  size_t l3_bytes = 0;
};

/**
 * @brief Persisted hardware characterization consumed by the simulator.
 */
struct HardwareProfile {
  // Sustained peaks — measured, not from spec sheet.
  double peak_flops_per_s = 0.0;        // double-precision FMA-saturated
  double peak_bandwidth_bytes_s = 0.0;  // DRAM read+write, STREAM triad

  // Cache sizes — from PAPI or sysfs.
  CacheSizes cache;

  // L1 cliff: number of 576-byte working-set units (one Body equivalent)
  // that fit in L1 before eviction degrades bandwidth.
  // 0 = cliff probe was not run.
  int l1_cliff_bodies = 0;

  // Roofline ridge point: the OI at which compute-bound meets memory-bound.
  // Derived: peak_flops_per_s / peak_bandwidth_bytes_s
  double ridge_point_flops_per_byte() const {
    return (peak_bandwidth_bytes_s > 0.0) ? peak_flops_per_s / peak_bandwidth_bytes_s : 0.0;
  }

  // Serialize / deserialize for caching between runs.
  bool save(const std::string& path) const;
  bool load(const std::string& path);

  // True if all mandatory fields are non-zero.
  bool valid() const { return peak_flops_per_s > 0.0 && peak_bandwidth_bytes_s > 0.0; }

  // Human-readable summary for logging.
  std::string summary() const;
};

/**
 * @brief Configuration for probe duration, sweep size, and verbosity.
 */
struct ProbeConfig {
  // How long each probe kernel runs. Longer = more thermally stable result.
  // 0.5s is sufficient for most desktop hardware.
  // Increase to 2.0s+ for server hardware that throttles more aggressively.
  double target_duration_s = 0.5;

  // STREAM array size = l3_bytes * stream_array_scale.
  // Must be > 1 to ensure the array exceeds L3. Default 4 gives 4×L3.
  int stream_array_scale = 4;

  // Run the L1 cliff probe. Adds ~2s to startup.
  // Required for accurate spawn cost estimation.
  bool run_l1_cliff = true;

  // Print intermediate probe results as they complete.
  bool verbose = false;

  // Number of independent FMA chains in the peak FLOP/s kernel.
  // Must be >= 8 to hide FMA latency and saturate FP throughput.
  // Increase to 16 for CPUs with AVX-512 and high latency FMA units.
  int fma_chain_count = 8;
};

/**
 * @brief Standardized output for one hardware probe stage.
 */
struct ProbeResult {
  bool valid = false;
  double measured = 0.0;       // primary measurement (units depend on probe)
  double duration_s = 0.0;     // actual wall time the kernel ran
  double stddev_frac = 0.0;    // stddev / mean across internal trials (0-1)
  bool high_variance = false;  // stddev_frac > 0.05

  std::string advisory;        // non-fatal notes (e.g. "thermal throttle suspected")
  std::string failure_reason;  // empty if valid
};

/**
 * @brief Runs sustained compute, memory, and cache-cliff probes.
 */
class HardwarePeakProbe {
 public:
  explicit HardwarePeakProbe(PAPIWrapper& papi, ProbeConfig cfg = {});

  // Run all probes and assemble HardwareProfile.
  // Logs progress to stderr. Call save() on the result to cache it.
  HardwareProfile run_all();

  // Individual probes — can be called independently for debugging.
  ProbeResult probe_peak_flops();
  ProbeResult probe_peak_bandwidth();
  ProbeResult probe_l1_cliff(int& cliff_bodies_out);

  // Cache size detection: PAPI primary, sysfs fallback, empirical last resort.
  CacheSizes detect_cache_sizes();

 private:
  PAPIWrapper& papi_;
  ProbeConfig cfg_;

  // -- Peak FLOP/s kernel --
  //
  // 8 independent FMA chains run simultaneously to saturate both FP units
  // and hide FMA latency (4-5 cycles on modern x86).
  //
  // Chain count is cfg_.fma_chain_count. Each chain:
  //   a_i = a_i * b + c   (1 FMA = 2 FLOPs)
  //
  // All accumulators are combined into a volatile sink at the end to prevent
  // dead-code elimination.
  //
  // Analytical FLOPs per call: 2 * cfg_.fma_chain_count * iterations
  // Working set: cfg_.fma_chain_count * sizeof(double) = 64 bytes (fits in L1)
  //
  // __attribute__((noinline)) prevents the compiler from seeing across the
  // call boundary and hoisting loop invariants.
  __attribute__((noinline)) static double fma_kernel_8chain(double b, double c, int iterations);

  // -- STREAM bandwidth kernel --
  //
  // Triad: c[i] = a[i] * b + d[i]
  // Chosen over simple copy (c[i] = a[i]) because it involves all three
  // arrays and one scalar — matches the STREAM benchmark standard.
  //
  // Bytes per element iteration: 2 reads (a, d) + 1 write (c) = 24 bytes
  // FLOPs per element: 1 FMA = 2 FLOPs
  // OI: 2/24 = 0.083 FLOPs/byte — well below any ridge point, memory-bound
  //
  // Array size must exceed L3. Allocated once per probe call.
  __attribute__((noinline)) static void stream_triad(double* __restrict__ a, double* __restrict__ c,
                                                     double* __restrict__ d, double b, size_t n);

  // -- L1 cliff probe --
  //
  // Runs a synthetic RK4-equivalent kernel with a working set of:
  //   body_count * BODY_WORKING_SET_BYTES
  // where BODY_WORKING_SET_BYTES = 576 (512 gamma + 64 state).
  //
  // Sweeps body_count over: 1, 2, 4, 8, 16, 32, 64, 128, 256
  // Records achieved bandwidth at each size.
  // Cliff is where bandwidth drops by more than CLIFF_DROP_THRESHOLD.
  static constexpr size_t BODY_WORKING_SET_BYTES = 576;
  static constexpr double CLIFF_DROP_THRESHOLD = 0.25;  // 25% drop signals cliff

  struct CliffSweepPoint {
    int body_count;
    double achieved_bw_bytes_s;
  };

  __attribute__((noinline)) static void rk4_proxy_kernel(double* state,  // body_count × 8 doubles
                                                         double* gamma,  // body_count × 64 doubles
                                                         int body_count, int iterations);

  // -- Cache size detection helpers --
  static CacheSizes query_papi_cache_sizes();
  static CacheSizes query_sysfs_cache_sizes();                  // /sys/devices/system/cpu/...
  static size_t probe_cache_size_empirically(int cache_level);  // sweep-based
};
