/**
 * @brief Hardware peak probe implementation for FLOP, bandwidth, and cache-cliff estimates.
 * Uses sustained windows and outlier rejection to avoid burst-only measurements.
 */

#include "hardware_peak_probe.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#ifdef HAVE_PAPI
#include <papi.h>
#endif

/**
 * @brief Internal constants.
 */

namespace {

// Number of internal timing trials per probe point. Outlier rejection
// requires >= 3. 5 is sufficient given the long target_duration_s.
constexpr int PROBE_TRIALS = 5;

// STREAM array element count is set at runtime based on L3 size.
// This fallback applies when L3 size cannot be determined.
constexpr size_t STREAM_FALLBACK_ELEMENTS = 32 * 1024 * 1024;  // 256MB of doubles

// Cliff sweep body counts.
constexpr int CLIFF_SWEEP_COUNTS[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
constexpr int CLIFF_SWEEP_N =
    static_cast<int>(sizeof(CLIFF_SWEEP_COUNTS) / sizeof(CLIFF_SWEEP_COUNTS[0]));

// Minimum probe duration in seconds. If target_duration_s is set below this,
// results will be burst rather than sustained and a warning is logged.
constexpr double MIN_RELIABLE_DURATION_S = 0.1;

}  // namespace

/**
 * @brief Constructor.
 */

HardwarePeakProbe::HardwarePeakProbe(PAPIWrapper& papi, ProbeConfig cfg) : papi_(papi), cfg_(cfg) {
  assert(cfg_.fma_chain_count >= 8 && "fma_chain_count must be >= 8 to hide FMA latency");
  assert(cfg_.stream_array_scale >= 2 && "stream_array_scale must be >= 2 to exceed L3");
  assert(cfg_.target_duration_s > 0.0);

  if (cfg_.target_duration_s < MIN_RELIABLE_DURATION_S) {
    // LOG_PLACEHOLDER
    fprintf(stderr,
            "[Probe] WARNING: target_duration_s=%.3fs is below %.1fs.\n"
            "        Results will reflect burst, not sustained, performance.\n",
            cfg_.target_duration_s, MIN_RELIABLE_DURATION_S);
  }
}

/**
 * @brief fma_kernel_8chain — peak FLOP/s kernel.
 */
//
// Eight independent FMA chains. Each chain accumulates independently so the
// CPU can schedule all eight in parallel across its FP execution units.
//
// The __restrict__ on b/c parameters is intentional — they are scalars
// passed by value, not pointers. No aliasing concern.
//
// Analytical FLOPs: 2 * 8 * iterations = 16 * iterations
// Working set: 8 * sizeof(double) = 64 bytes — fits in L1

__attribute__((noinline)) double HardwarePeakProbe::fma_kernel_8chain(double b, double c,
                                                                      int iterations) {
  // Eight independent accumulators. Initial values are different to prevent
  // the compiler from identifying them as equivalent and merging chains.
  double a0 = 1.0, a1 = 1.1, a2 = 1.2, a3 = 1.3;
  double a4 = 1.4, a5 = 1.5, a6 = 1.6, a7 = 1.7;

  for (int i = 0; i < iterations; ++i) {
    // All eight operations are data-independent of each other this iteration.
    // The CPU superscalar engine can issue all of them to the two FMA units
    // (or more, on wide-issue CPUs) in the same clock window.
    a0 = a0 * b + c;
    a1 = a1 * b + c;
    a2 = a2 * b + c;
    a3 = a3 * b + c;
    a4 = a4 * b + c;
    a5 = a5 * b + c;
    a6 = a6 * b + c;
    a7 = a7 * b + c;
  }

  // Combine all chains. The compiler cannot eliminate any chain because
  // each contributes to the returned value.
  // The caller writes this to a volatile sink.
  return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
}

/**
 * @brief stream_triad — STREAM bandwidth kernel.
 */
//
// Triad: c[i] = a[i] * b + d[i]
//
// __restrict__ tells the compiler the arrays do not alias — enables
// auto-vectorization and avoids load-after-store hazards.
//
// Bytes per iteration: 2 loads (a[i], d[i]) + 1 store (c[i]) = 24 bytes
// FLOPs per iteration: 1 FMA = 2 FLOPs
// OI: 2/24 = 0.083 — well below ridge point, guarantees memory-bound regime

__attribute__((noinline)) void HardwarePeakProbe::stream_triad(double* __restrict__ a,
                                                               double* __restrict__ c,
                                                               double* __restrict__ d, double b,
                                                               size_t n) {
  for (size_t i = 0; i < n; ++i) {
    c[i] = a[i] * b + d[i];
  }
}

/**
 * @brief rk4_proxy_kernel — L1 cliff probe kernel.
 */
//
// Simulates the memory access pattern of the RK4 geodesic integrator
// without actual GR math. The goal is to stress the cache at known
// working-set sizes, not to produce correct physics.
//
// Per body working set:
//   gamma: 64 doubles = 512 bytes   (Christoffel symbols)
//   state:  8 doubles =  64 bytes   (position + velocity)
//   total: 576 bytes = BODY_WORKING_SET_BYTES
//
// Each iteration reads all of gamma and state, writes state.
// Four such iterations per RK4 step (k1-k4).
// Access is sequential within each body — realistic cache behaviour.

__attribute__((noinline)) void HardwarePeakProbe::rk4_proxy_kernel(double* state, double* gamma,
                                                                   int body_count, int iterations) {
  constexpr int STATE_DOUBLES = 8;   // 4 position + 4 velocity
  constexpr int GAMMA_DOUBLES = 64;  // 4×4×4 Christoffel components

  for (int iter = 0; iter < iterations; ++iter) {
    for (int b = 0; b < body_count; ++b) {
      double* s = state + b * STATE_DOUBLES;
      double* g = gamma + b * GAMMA_DOUBLES;

      // 4 RK4 sub-steps (k1-k4): read gamma, read+write state.
      // Simplified: accumulate gamma into state components.
      for (int k = 0; k < 4; ++k) {
        double acc = 0.0;
        // Read all 64 gamma components (forces full gamma into cache).
        for (int j = 0; j < GAMMA_DOUBLES; ++j) {
          acc += g[j];
        }
        // Write all 8 state components (depends on gamma read).
        for (int j = 0; j < STATE_DOUBLES; ++j) {
          s[j] += acc * 1e-10;  // tiny multiplier — no physical meaning
        }
      }
    }
  }
}

/**
 * @brief detect_cache_sizes.
 */

CacheSizes HardwarePeakProbe::detect_cache_sizes() {
  // Try PAPI first (most reliable cross-platform source).
  CacheSizes sizes = query_papi_cache_sizes();
  if (sizes.l3_bytes > 0) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Probe] cache sizes from PAPI: L1=%zuKB L2=%zuKB L3=%zuMB\n",
            sizes.l1_bytes / 1024, sizes.l2_bytes / 1024, sizes.l3_bytes / (1024 * 1024));
    return sizes;
  }

  // Fall back to sysfs.
  sizes = query_sysfs_cache_sizes();
  if (sizes.l3_bytes > 0) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Probe] cache sizes from sysfs: L1=%zuKB L2=%zuKB L3=%zuMB\n",
            sizes.l1_bytes / 1024, sizes.l2_bytes / 1024, sizes.l3_bytes / (1024 * 1024));
    return sizes;
  }

  // Last resort: empirical sweep.
  // LOG_PLACEHOLDER
  fprintf(stderr, "[Probe] cache size detection failed — running empirical sweep\n");
  sizes.l1_bytes = probe_cache_size_empirically(1);
  sizes.l2_bytes = probe_cache_size_empirically(2);
  sizes.l3_bytes = probe_cache_size_empirically(3);

  if (sizes.l3_bytes == 0) {
    fprintf(stderr,
            "[Probe] WARNING: could not determine L3 size.\n"
            "        Using fallback STREAM array size (%zuMB).\n",
            STREAM_FALLBACK_ELEMENTS * sizeof(double) / (1024 * 1024));
  }
  return sizes;
}

CacheSizes HardwarePeakProbe::query_papi_cache_sizes() {
  CacheSizes sizes{};
#ifdef HAVE_PAPI
  const PAPI_hw_info_t* hw = PAPI_get_hardware_info();
  if (!hw) return sizes;

  // PAPI_hw_info_t has mem_hierarchy with up to PAPI_MAX_MEM_HIERARCHY_LEVELS.
  // Level 0 = L1, 1 = L2, 2 = L3 (typical).
  for (int i = 0; i < hw->mem_hierarchy.levels && i < 3; ++i) {
    const PAPI_mh_level_t& lvl = hw->mem_hierarchy.level[i];
    // Each level may have separate I-cache and D-cache entries.
    for (int j = 0; j < PAPI_MH_MAX_LEVELS; ++j) {
      const PAPI_mh_cache_t& cache = lvl.cache[j];
      if (cache.type == PAPI_MH_TYPE_DATA || cache.type == PAPI_MH_TYPE_UNIFIED) {
        const size_t bytes = static_cast<size_t>(cache.size);
        if (i == 0 && bytes > 0) sizes.l1_bytes = bytes;
        if (i == 1 && bytes > 0) sizes.l2_bytes = bytes;
        if (i == 2 && bytes > 0) sizes.l3_bytes = bytes;
      }
    }
  }
#endif
  return sizes;
}

CacheSizes HardwarePeakProbe::query_sysfs_cache_sizes() {
  CacheSizes sizes{};
  // sysfs layout: /sys/devices/system/cpu/cpu0/cache/index{0,1,2,3}/
  // index0 = L1d, index1 = L1i, index2 = L2, index3 = L3 (typical)
  // "level" file contains 1, 2, or 3.
  // "type" file contains "Data", "Instruction", or "Unified".
  // "size" file contains e.g. "32K" or "8192K".

  auto read_file = [](const std::string& path) -> std::string {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::string s;
    std::getline(f, s);
    return s;
  };

  auto parse_size = [](const std::string& s) -> size_t {
    if (s.empty()) return 0;
    size_t val = 0;
    char unit = 'K';
    if (std::sscanf(s.c_str(), "%zu%c", &val, &unit) < 1) return 0;
    if (unit == 'K' || unit == 'k') return val * 1024;
    if (unit == 'M' || unit == 'm') return val * 1024 * 1024;
    return val;
  };

  for (int idx = 0; idx < 8; ++idx) {
    const std::string base = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(idx) + "/";

    const std::string level_s = read_file(base + "level");
    const std::string type_s = read_file(base + "type");
    const std::string size_s = read_file(base + "size");

    if (level_s.empty() || size_s.empty()) break;  // no more indices

    // Skip instruction caches.
    if (type_s == "Instruction") continue;

    const int level = std::stoi(level_s);
    const size_t bytes = parse_size(size_s);

    if (bytes == 0) continue;

    if (level == 1) sizes.l1_bytes = bytes;
    if (level == 2) sizes.l2_bytes = bytes;
    if (level == 3) sizes.l3_bytes = bytes;
  }

  return sizes;
}

size_t HardwarePeakProbe::probe_cache_size_empirically(int cache_level) {
  // Sweep array sizes from 8KB to 512MB, doubling each step.
  // Time a sequential read pass at each size. The bandwidth drop indicates
  // the cache boundary.
  //
  // This is approximate — the drop is gradual, not sharp.
  // Report the size at which bandwidth fell to 70% of peak.

  const double THRESHOLD = 0.70;

  double peak_bw = 0.0;
  size_t cliff_size = 0;

  for (size_t bytes = 8 * 1024; bytes <= 512 * 1024 * 1024; bytes *= 2) {
    const size_t n = bytes / sizeof(double);
    std::vector<double> arr(n, 1.0);

    // Time a single sequential read pass.
    const auto result = BenchTimer::measure(3, [&] {
      volatile double sink = 0.0;
      for (size_t i = 0; i < n; ++i) sink += arr[i];
      (void)sink;
    });

    const double bw = (bytes * 1.0) / (result.mean_ns * 1e-9);
    if (bw > peak_bw) peak_bw = bw;

    if (cache_level == 1 && bw < peak_bw * THRESHOLD && cliff_size == 0) cliff_size = bytes / 2;
    if (cache_level == 2 && bw < peak_bw * THRESHOLD * 0.5 && cliff_size == 0)
      cliff_size = bytes / 2;
    if (cache_level == 3 && bw < peak_bw * THRESHOLD * 0.25 && cliff_size == 0)
      cliff_size = bytes / 2;
  }

  return cliff_size;
}

/**
 * @brief probe_peak_flops.
 */

ProbeResult HardwarePeakProbe::probe_peak_flops() {
  ProbeResult out{};

  if (cfg_.verbose) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Probe] peak FLOP/s: running %.1fs kernel (%d FMA chains)...\n",
            cfg_.target_duration_s, cfg_.fma_chain_count);
  }

  if (cfg_.target_duration_s < MIN_RELIABLE_DURATION_S) {
    out.advisory = "duration below recommended minimum — burst result only";
  }

  // FMA kernel inputs: chosen so chain doesn't converge to fixed point.
  const double b = 1.0 + 1e-9;
  const double c = 1e-12;

  // Analytical FLOPs per kernel call.
  // Determined by running a calibration pass to find how many iterations
  // fit in target_duration_s, then using that count for measurement.

  // -- Calibration pass --
  // Find iterations_per_call such that one kernel call takes ~10ms.
  // This keeps individual trial durations measurable but not too long.
  int cal_iters = 1000;
  {
    volatile double sink = 0.0;
    BenchTimer cal_timer;
    cal_timer.start();
    sink = fma_kernel_8chain(b, c, cal_iters);
    (void)sink;
    const double cal_ns = cal_timer.elapsed_ns();
    // Scale to hit ~10ms per call.
    const double target_call_ns = 10.0e6;
    if (cal_ns > 0.0) {
      cal_iters = static_cast<int>(cal_iters * (target_call_ns / cal_ns));
      cal_iters = std::max(cal_iters, 1000);
    }
  }

  const long long flops_per_call = 2LL * cfg_.fma_chain_count * cal_iters;

  // -- Sustained measurement --
  // Run the kernel repeatedly until target_duration_s is reached.
  // Collect per-call timings and report statistics.
  std::vector<double> trial_flops_s;
  volatile double sink = 0.0;

  BenchTimer outer_timer;
  outer_timer.start();

  while (outer_timer.elapsed_ns() * 1e-9 < cfg_.target_duration_s) {
    BenchTimer call_timer;
    call_timer.start();
    sink = fma_kernel_8chain(b, c, cal_iters);
    const double ns = call_timer.elapsed_ns();
    if (ns > 0.0) {
      trial_flops_s.push_back(static_cast<double>(flops_per_call) / (ns * 1e-9));
    }
  }

  out.duration_s = outer_timer.elapsed_ns() * 1e-9;
  (void)sink;

  if (trial_flops_s.empty()) {
    out.failure_reason = "no trials completed within target duration";
    return out;
  }

  // -- Outlier rejection --
  std::sort(trial_flops_s.begin(), trial_flops_s.end());
  const size_t n = trial_flops_s.size();
  const size_t drop = std::max(size_t(1), n / 10);  // drop top+bottom 10%

  const auto begin = trial_flops_s.begin() + static_cast<ptrdiff_t>(drop);
  const auto end = trial_flops_s.end() - static_cast<ptrdiff_t>(drop);

  if (begin >= end) {
    // Too few trials for outlier rejection — use all.
    out.measured = trial_flops_s[n / 2];  // median
  } else {
    const double sum = std::accumulate(begin, end, 0.0);
    const int cnt = static_cast<int>(std::distance(begin, end));
    out.measured = sum / static_cast<double>(cnt);

    // Stddev of retained trials.
    double sq = 0.0;
    for (auto it = begin; it != end; ++it) {
      const double d = *it - out.measured;
      sq += d * d;
    }
    out.stddev_frac = (out.measured > 0.0 && cnt > 1)
                          ? std::sqrt(sq / static_cast<double>(cnt - 1)) / out.measured
                          : 0.0;
  }

  out.high_variance = (out.stddev_frac > 0.05);
  if (out.high_variance) {
    out.advisory =
        "high variance (stddev/mean > 5%) — thermal throttle suspected.\n"
        "Results may understate true peak. Run with: taskset -c 0 ./gr_bench";
  }

  out.valid = true;

  if (cfg_.verbose) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Probe] peak FLOP/s = %.3f GFLOP/s  (%.0f trials, stddev/mean=%.2f%%)\n",
            out.measured / 1e9, static_cast<double>(trial_flops_s.size()), out.stddev_frac * 100.0);
  }

  return out;
}

/**
 * @brief probe_peak_bandwidth.
 */

ProbeResult HardwarePeakProbe::probe_peak_bandwidth() {
  ProbeResult out{};

  const CacheSizes cache = detect_cache_sizes();
  const size_t l3 =
      (cache.l3_bytes > 0) ? cache.l3_bytes : STREAM_FALLBACK_ELEMENTS * sizeof(double);

  const size_t array_bytes = static_cast<size_t>(cfg_.stream_array_scale) * l3;
  const size_t array_elements = array_bytes / sizeof(double);

  if (cfg_.verbose) {
    // LOG_PLACEHOLDER
    fprintf(stderr,
            "[Probe] peak bandwidth: STREAM triad, array=%.0fMB (%.0f× L3), "
            "running %.1fs...\n",
            static_cast<double>(array_bytes) / (1024.0 * 1024.0),
            static_cast<double>(cfg_.stream_array_scale), cfg_.target_duration_s);
  }

  // Allocate arrays. Use aligned allocation for auto-vectorization.
  // Three arrays: a (read), c (write), d (read).
  auto* a = static_cast<double*>(std::aligned_alloc(64, array_bytes));
  auto* c = static_cast<double*>(std::aligned_alloc(64, array_bytes));
  auto* d = static_cast<double*>(std::aligned_alloc(64, array_bytes));

  if (!a || !c || !d) {
    std::free(a);
    std::free(c);
    std::free(d);
    out.failure_reason = "failed to allocate STREAM arrays (" +
                         std::to_string(array_bytes / (1024 * 1024)) + "MB each)";
    return out;
  }

  // Initialize arrays with non-zero values. Prevents zero-page optimization
  // where the OS maps all pages to the same zero page.
  std::fill(a, a + array_elements, 1.0);
  std::fill(c, c + array_elements, 0.0);
  std::fill(d, d + array_elements, 2.0);

  const double scalar_b = 3.14159;

  // Bytes transferred per kernel call:
  // read a: array_bytes, read d: array_bytes, write c: array_bytes
  const double bytes_per_call = static_cast<double>(array_bytes) * 3.0;

  std::vector<double> trial_bw_s;

  BenchTimer outer_timer;
  outer_timer.start();

  while (outer_timer.elapsed_ns() * 1e-9 < cfg_.target_duration_s) {
    BenchTimer call_timer;
    call_timer.start();
    stream_triad(a, c, d, scalar_b, array_elements);
    const double ns = call_timer.elapsed_ns();
    if (ns > 0.0) {
      trial_bw_s.push_back(bytes_per_call / (ns * 1e-9));
    }
  }

  out.duration_s = outer_timer.elapsed_ns() * 1e-9;

  std::free(a);
  std::free(c);
  std::free(d);

  if (trial_bw_s.empty()) {
    out.failure_reason = "no trials completed — array may be too large or duration too short";
    return out;
  }

  // Outlier rejection: same as probe_peak_flops.
  std::sort(trial_bw_s.begin(), trial_bw_s.end());
  const size_t n = trial_bw_s.size();
  const size_t drop = std::max(size_t(1), n / 10);

  const auto begin = trial_bw_s.begin() + static_cast<ptrdiff_t>(drop);
  const auto end = trial_bw_s.end() - static_cast<ptrdiff_t>(drop);

  if (begin >= end) {
    out.measured = trial_bw_s[n / 2];
  } else {
    const double sum = std::accumulate(begin, end, 0.0);
    const int cnt = static_cast<int>(std::distance(begin, end));
    out.measured = sum / static_cast<double>(cnt);

    double sq = 0.0;
    for (auto it = begin; it != end; ++it) {
      const double d2 = *it - out.measured;
      sq += d2 * d2;
    }
    out.stddev_frac = (out.measured > 0.0 && cnt > 1)
                          ? std::sqrt(sq / static_cast<double>(cnt - 1)) / out.measured
                          : 0.0;
  }

  out.high_variance = (out.stddev_frac > 0.05);
  if (out.high_variance) {
    out.advisory =
        "high variance — verify array exceeds L3. "
        "Run: taskset -c 0 ./gr_bench";
  }

  out.valid = true;

  if (cfg_.verbose) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Probe] peak bandwidth = %.1f GB/s  (%.0f trials, stddev/mean=%.2f%%)\n",
            out.measured / 1e9, static_cast<double>(trial_bw_s.size()), out.stddev_frac * 100.0);
  }

  return out;
}

/**
 * @brief probe_l1_cliff.
 */

ProbeResult HardwarePeakProbe::probe_l1_cliff(int& cliff_bodies_out) {
  ProbeResult out{};
  cliff_bodies_out = 0;

  if (cfg_.verbose) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Probe] L1 cliff: sweeping body count %d..%d\n", CLIFF_SWEEP_COUNTS[0],
            CLIFF_SWEEP_COUNTS[CLIFF_SWEEP_N - 1]);
  }

  std::vector<CliffSweepPoint> sweep;
  sweep.reserve(static_cast<size_t>(CLIFF_SWEEP_N));

  double peak_bw = 0.0;

  for (int si = 0; si < CLIFF_SWEEP_N; ++si) {
    const int bc = CLIFF_SWEEP_COUNTS[si];
    const size_t state_bytes = static_cast<size_t>(bc) * 8 * sizeof(double);
    const size_t gamma_bytes = static_cast<size_t>(bc) * 64 * sizeof(double);

    auto* state = static_cast<double*>(std::aligned_alloc(64, state_bytes));
    auto* gamma = static_cast<double*>(std::aligned_alloc(64, gamma_bytes));

    if (!state || !gamma) {
      std::free(state);
      std::free(gamma);
      // LOG_PLACEHOLDER
      fprintf(stderr, "[Probe] L1 cliff: allocation failed at body_count=%d\n", bc);
      break;
    }

    std::fill(state, state + bc * 8, 1.0);
    std::fill(gamma, gamma + bc * 64, 1e-4);

    // Calibrate iterations so each trial runs ~5ms.
    int iters = 10;
    {
      BenchTimer cal;
      cal.start();
      rk4_proxy_kernel(state, gamma, bc, iters);
      const double cal_ns = cal.elapsed_ns();
      if (cal_ns > 0.0) {
        iters = static_cast<int>(iters * (5e6 / cal_ns));
        iters = std::max(iters, 1);
      }
    }

    const auto trial_result =
        BenchTimer::measure(PROBE_TRIALS, [&] { rk4_proxy_kernel(state, gamma, bc, iters); });

    // Working set bytes per iteration (both state and gamma, read+written).
    const double working_set_bytes = static_cast<double>(bc) * BODY_WORKING_SET_BYTES;
    const double total_bytes =
        working_set_bytes * static_cast<double>(iters) * 4.0;  // 4 RK4 sub-steps

    const double bw = total_bytes / (trial_result.mean_ns * 1e-9);

    if (cfg_.verbose) {
      fprintf(stderr, "        bodies=%-4d  working_set=%.1fKB  bw=%.1fGB/s\n", bc,
              static_cast<double>(working_set_bytes) / 1024.0, bw / 1e9);
    }

    sweep.push_back({bc, bw});
    if (bw > peak_bw) peak_bw = bw;

    std::free(state);
    std::free(gamma);
  }

  if (sweep.empty()) {
    out.failure_reason = "no sweep points completed";
    return out;
  }

  // Find the cliff: first point where bw < peak_bw * (1 - CLIFF_DROP_THRESHOLD).
  const double cliff_threshold = peak_bw * (1.0 - CLIFF_DROP_THRESHOLD);
  bool found = false;

  for (size_t i = 1; i < sweep.size(); ++i) {
    if (sweep[i].achieved_bw_bytes_s < cliff_threshold) {
      // Cliff is between sweep[i-1] and sweep[i].
      // Report the last body count that stayed above threshold.
      cliff_bodies_out = sweep[i - 1].body_count;
      found = true;
      break;
    }
  }

  if (!found) {
    // Bandwidth stayed high across all sweep points — no cliff detected
    // within the sweep range. Report the maximum tested.
    cliff_bodies_out = CLIFF_SWEEP_COUNTS[CLIFF_SWEEP_N - 1];
    out.advisory =
        "L1 cliff not detected within sweep range — "
        "may have very large L1 or working set fits entirely";
  }

  out.measured = static_cast<double>(cliff_bodies_out);
  out.valid = true;

  if (cfg_.verbose) {
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Probe] L1 cliff at body_count = %d  (%.0f bytes working set)\n",
            cliff_bodies_out, static_cast<double>(cliff_bodies_out) * BODY_WORKING_SET_BYTES);
  }

  return out;
}

/**
 * @brief run_all.
 */

HardwareProfile HardwarePeakProbe::run_all() {
  HardwareProfile hw{};

  fprintf(stderr, "[Probe] starting hardware characterization\n");

  // Cache sizes (needed by probe_peak_bandwidth).
  hw.cache = detect_cache_sizes();

  // Peak FLOP/s.
  const ProbeResult flops = probe_peak_flops();
  if (!flops.valid) {
    fprintf(stderr, "[Probe] FATAL: peak FLOP/s probe failed: %s\n", flops.failure_reason.c_str());
  } else {
    hw.peak_flops_per_s = flops.measured;
    if (!flops.advisory.empty()) fprintf(stderr, "[Probe] advisory: %s\n", flops.advisory.c_str());
  }

  // Peak bandwidth.
  const ProbeResult bw = probe_peak_bandwidth();
  if (!bw.valid) {
    fprintf(stderr, "[Probe] FATAL: peak bandwidth probe failed: %s\n", bw.failure_reason.c_str());
  } else {
    hw.peak_bandwidth_bytes_s = bw.measured;
    if (!bw.advisory.empty()) fprintf(stderr, "[Probe] advisory: %s\n", bw.advisory.c_str());
  }

  // L1 cliff (optional).
  if (cfg_.run_l1_cliff) {
    int cliff = 0;
    const ProbeResult cp = probe_l1_cliff(cliff);
    if (cp.valid) {
      hw.l1_cliff_bodies = cliff;
    } else {
      fprintf(stderr, "[Probe] L1 cliff probe failed: %s\n", cp.failure_reason.c_str());
    }
  }

  fprintf(stderr, "%s", hw.summary().c_str());
  return hw;
}

/**
 * @brief HardwareProfile — summary, save, load.
 */

std::string HardwareProfile::summary() const {
  std::ostringstream ss;
  ss << "[HardwareProfile]\n"
     << "  peak FLOP/s      = " << peak_flops_per_s / 1e9 << " GFLOP/s\n"
     << "  peak bandwidth   = " << peak_bandwidth_bytes_s / 1e9 << " GB/s\n"
     << "  ridge point      = " << ridge_point_flops_per_byte() << " FLOP/byte\n"
     << "  L1 cache         = " << cache.l1_bytes / 1024 << " KB\n"
     << "  L2 cache         = " << cache.l2_bytes / 1024 << " KB\n"
     << "  L3 cache         = " << cache.l3_bytes / (1024 * 1024) << " MB\n"
     << "  L1 cliff bodies  = " << l1_cliff_bodies << "\n";
  return ss.str();
}

bool HardwareProfile::save(const std::string& path) const {
  std::ofstream f(path, std::ios::binary);
  if (!f.is_open()) return false;
  f.write(reinterpret_cast<const char*>(this), sizeof(*this));
  return f.good();
}

bool HardwareProfile::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return false;
  f.read(reinterpret_cast<char*>(this), sizeof(*this));
  return f.good() && valid();
}
