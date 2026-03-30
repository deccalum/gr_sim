#pragma once

/**
 * @brief Analytical profile model and empirical cross-check types for benchmarked expressions.
 * Stores per-call FLOP and byte expectations, then compares measured hardware-counter behavior
 * against those expectations to classify pass, warn, fail, or regime mismatch.
 */

#include <string>

#include "hardware_peak_probe.h"  // for HardwareProfile (ridge point)

/**
 * @brief Analytical per-call characterization for one expression kernel.
 */
struct ExpressionProfile {
  // Human-readable identifier. Must be unique across all registered profiles.
  // Used in cross-check reports and cache file keys.
  std::string name;

  // Analytical FLOP count per call.
  // Count FMA as 2 FLOPs. Count div as 1. Count transcendentals as their
  // typical microcode expansion (~20 FLOPs for sin/cos on x86).
  // See derivation comments in expression_profile.cpp for each algorithm.
  double flop_count = 0.0;

  // Analytical memory traffic per call, in bytes.
  // bytes_read:    all values read from memory (inputs + any cached reads)
  // bytes_written: all values written (output arrays, updated state)
  //
  // For arrays: count all elements written, including zero-fills.
  // Rationale: even writing zero to a cache line still occupies write bandwidth.
  double bytes_read = 0.0;
  double bytes_written = 0.0;

  // Derived: total bytes per call.
  double bytes_total() const { return bytes_read + bytes_written; }

  // Derived: operational intensity (FLOPs per byte).
  // Returns 0.0 if bytes_total() is zero (degenerate case).
  double oi_analytical() const {
    const double b = bytes_total();
    return (b > 0.0) ? flop_count / b : 0.0;
  }

  // Predict roofline regime on given hardware.
  // Compares oi_analytical against the hardware ridge point.
  enum class Regime { ComputeBound, MemoryBound, Unknown };

  Regime predict_regime(const HardwareProfile& hw) const {
    if (hw.peak_flops_per_s <= 0.0 || hw.peak_bandwidth_bytes_s <= 0.0) return Regime::Unknown;
    return (oi_analytical() > hw.ridge_point_flops_per_byte()) ? Regime::ComputeBound
                                                               : Regime::MemoryBound;
  }

  bool valid() const { return !name.empty() && flop_count > 0.0 && bytes_total() > 0.0; }
};

/**
 * @brief Empirical measurement for one benchmark run plus derived diagnostics.
 */
struct ExpressionMeasurement {
  ExpressionProfile profile;  // analytical (input, copied for reference)

  // Raw PAPI + timer values from the measurement window.
  long long actual_flops = 0;       // PAPI_DP_OPS for the whole window
  long long actual_l1_misses = 0;   // PAPI_L1_DCM
  long long actual_l2_misses = 0;   // PAPI_L2_DCM
  long long actual_llc_misses = 0;  // PAPI_L3_TCM
  double elapsed_ns = 0.0;
  int n_iterations = 0;  // calls in the measurement window

  // Per-call derived values.
  double flops_per_call() const {
    return (n_iterations > 0) ? static_cast<double>(actual_flops) / n_iterations : 0.0;
  }
  double elapsed_ns_per_call() const {
    return (n_iterations > 0) ? elapsed_ns / n_iterations : 0.0;
  }

  // Achieved throughput.
  double achieved_flops_per_s() const {
    return (elapsed_ns > 0.0) ? static_cast<double>(actual_flops) / (elapsed_ns * 1e-9) : 0.0;
  }

  // Operational intensity — empirical, DRAM-level.
  // Uses LLC misses as proxy for DRAM traffic (1 miss = 64-byte cache line).
  // Returns 0.0 if no LLC misses were recorded (expression was cache-resident).
  double oi_empirical_dram() const {
    const long long dram_bytes = actual_llc_misses * 64LL;
    return (dram_bytes > 0) ? static_cast<double>(actual_flops) / static_cast<double>(dram_bytes)
                            : 0.0;
  }

  // Operational intensity — empirical, L2-level.
  double oi_empirical_l2() const {
    const long long l2_bytes = actual_l2_misses * 64LL;
    return (l2_bytes > 0) ? static_cast<double>(actual_flops) / static_cast<double>(l2_bytes) : 0.0;
  }

  // Operational intensity — analytical cross-check level.
  // Uses analytical byte count with measured FLOP count.
  // This tests whether the PAPI FLOP count agrees with the analytical model,
  // independent of cache behaviour.
  double oi_flop_cross_check() const {
    const double b = profile.bytes_total() * n_iterations;
    return (b > 0.0 && n_iterations > 0) ? static_cast<double>(actual_flops) / b : 0.0;
  }

  // FLOP divergence: how much does the measured FLOP count differ from
  // the analytical model?
  // Value is (measured - expected) / expected.
  // Positive: more FLOPs than expected (e.g. extra branches, wider vectorization).
  // Negative: fewer FLOPs (e.g. compiler eliminated work, non-FMA emission).
  double flop_divergence() const {
    const double expected = profile.flop_count * n_iterations;
    if (expected <= 0.0) return 0.0;
    return (static_cast<double>(actual_flops) - expected) / expected;
  }

  // Cross-check classification.
  enum class CheckResult { Pass, Warn, Fail, RegimeMismatch, Insufficient };

  // Cross-check thresholds (fraction of expected value).
  static constexpr double PASS_THRESHOLD = 0.10;
  static constexpr double WARN_THRESHOLD = 0.20;

  // Evaluate the cross-check against the analytical model.
  // hw is used to determine predicted vs observed regime.
  CheckResult evaluate(const HardwareProfile& hw, std::string& detail_out) const;
};

/**
 * @brief Interface implemented by expression kernels that can be benchmarked.
 */
class BenchmarkableExpression {
 public:
  virtual ~BenchmarkableExpression() = default;

  // Analytical profile for this expression.
  virtual const ExpressionProfile& profile() const = 0;

  // Allocate and populate N distinct inputs.
  // Inputs must vary enough to prevent the compiler from constant-folding
  // the execute() loop. For Schwarzschild: span r from 3M to 20M.
  // Returns owning pointer to a flat buffer of N × input_size_bytes() bytes.
  // Caller takes ownership (free with delete[]).
  virtual unsigned char* prepare_inputs(int N) const = 0;
  virtual size_t input_size_bytes() const = 0;

  // Execute expression on one input from the prepared buffer.
  // input: pointer to one element of the buffer returned by prepare_inputs().
  // output_sink: write result here. Must be non-null. Prevents DCE.
  // The output_sink buffer is managed by ExpressionBenchmark.
  virtual void execute(const unsigned char* input, unsigned char* output_sink) const = 0;
};

namespace profiles {

// Schwarzschild Christoffel symbols Γ^σ_μν
// Coordinate system: spherical [t, r, θ, φ]
// Non-zero independent components: 9 (13 entries with symmetry)
// Total array written: gamma[4][4][4] = 64 doubles = 512 bytes (including zeros)
//
// FLOPs: ~47 (pre-computation) + 10 (Christoffel values) = 57
// Bytes read:    32 (Vec4 input) + 8 (mass M, treated as memory read)
// Bytes written: 512 (full gamma array, all 64 components)
// OI: 57 / 552 ≈ 0.103 FLOPs/byte  → memory-bound on all current hardware

extern const ExpressionProfile schwarzschild_christoffel;

// RK4 geodesic step — Schwarzschild metric, massive particle
// State: (x[4], u[4]) = 8 doubles
// 4 RHS evaluations × (57 Γ FLOPs + 28 contraction FLOPs) = 340 FLOPs
// RK4 combination: 56 FLOPs
// Total: 396 FLOPs (418 with norm enforcement)
//
// Bytes (L1 miss / worst case):
//   Christoffel I/O: 4 × (32 read + 512 write) = 2176 bytes
//   State I/O: 64 + 256 + 256 + 64 = 640 bytes
//   Total: 2816 bytes
// OI (miss): 396 / 2816 ≈ 0.141 FLOPs/byte → memory-bound
//
// Bytes (L1 hit / best case):
//   Christoffel input: 4 × 32 = 128 bytes (gamma stays in L1)
//   State I/O: 640 bytes
//   Total: 768 bytes
// OI (hit): 396 / 768 ≈ 0.516 FLOPs/byte → still memory-bound, less severely

extern const ExpressionProfile rk4_geodesic_l1_miss;  // worst case
extern const ExpressionProfile rk4_geodesic_l1_hit;   // best case (single body)
extern const ExpressionProfile rk4_geodesic_norm;     // with norm enforcement (+22 FLOPs)

}  // namespace profiles

/**
 * @brief Phase-1 stub for Schwarzschild Christoffel benchmarking.
 */
class StubSchwarzschildChristoffel final : public BenchmarkableExpression {
  // STUB: SchwarzschildMetric::christoffel() not yet implemented.
  // Replicates: ~47 FLOPs pre-computation + 10 Christoffel FLOPs,
  //             write 64 doubles (512 bytes) to output.
 public:
  const ExpressionProfile& profile() const override;
  unsigned char* prepare_inputs(int N) const override;
  size_t input_size_bytes() const override;
  void execute(const unsigned char* input, unsigned char* output_sink) const override;
};

/**
 * @brief Phase-1 stub for RK4 geodesic benchmarking.
 */
class StubRK4Geodesic final : public BenchmarkableExpression {
  // STUB: Body::step_rk4() not yet implemented.
  // Replicates: 4 RHS evaluations (4 × 85 FLOPs) + combination (56 FLOPs),
  //             memory access pattern for one body (state + gamma arrays).
 public:
  explicit StubRK4Geodesic(bool include_norm = false);
  const ExpressionProfile& profile() const override;
  unsigned char* prepare_inputs(int N) const override;
  size_t input_size_bytes() const override;
  void execute(const unsigned char* input, unsigned char* output_sink) const override;

 private:
  bool include_norm_;
};
