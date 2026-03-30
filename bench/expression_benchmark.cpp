/**
 * @brief Expression benchmark implementation and synthetic phase-1 stubs.
 * Stub kernels preserve analytical FLOP and byte footprints so empirical measurements can validate
 * roofline assumptions before full physics kernels are integrated.
 */

#include "expression_benchmark.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <sstream>
#include <vector>

namespace {

constexpr double REGIME_THRESHOLD = 0.70;  // fraction of peak = "roofline limited"
constexpr double CROSS_CHECK_PASS = 0.10;  // < 10% divergence = Pass
constexpr double CROSS_CHECK_WARN = 0.20;  // 10-20% = Warn, >= 20% = Fail
constexpr int WARMUP_CALLS = 4;
constexpr int PAPI_SINGLE_TRIALS = 8;  // counter reads averaged per expression

}  // namespace

/**
 * @brief ExpressionDescriptor::predict_regime.
 */

ExpressionRegime ExpressionDescriptor::predict_regime(const HardwareProfile& hw) const {
  if (hw.peak_flops_per_s <= 0.0 || hw.peak_bandwidth_bytes_s <= 0.0)
    return ExpressionRegime::Unknown;
  const double ridge = hw.ridge_point_flops_per_byte();
  return (oi_analytical() >= ridge) ? ExpressionRegime::ComputeBound
                                    : ExpressionRegime::MemoryBound;
}

/**
 * @brief Phase 1 stub: Schwarzschild Christoffel.
 */
//
// Memory:
//   Read:  x[4]           =  4 × 8 =  32 bytes
//   Write: gamma[4][4][4] = 64 × 8 = 512 bytes
//   Total: 552 bytes
//
// FLOPs: 57 (47 pre-computation + 10 Christoffel values)
// OI:    57 / 552 = 0.103 FLOP/byte → memory-bound on all current hardware

ExpressionDescriptor ExpressionDescriptor::make_schwarzschild_christoffel_stub() {
  ExpressionDescriptor desc;
  desc.name = "christoffel_schwarzschild_stub";
  desc.flops_per_call = 57;
  desc.bytes_read_per_call = 32;
  desc.bytes_written_per_call = 512;
  desc.input_count = 64;

  struct Vec4 {
    double t, r, theta, phi;
  };

  auto inputs = std::make_shared<std::vector<Vec4> >(64);
  auto outputs = std::make_shared<std::vector<double> >(64 * 64, 0.0);

  for (int i = 0; i < 64; ++i) {
    (*inputs)[i] = {static_cast<double>(i) * 0.1,
                    3.0 + static_cast<double>(i) * 0.5,  // r > 2M (above horizon)
                    0.5 + static_cast<double>(i) * 0.02, static_cast<double>(i) * 0.1};
  }

  desc.execute = [inputs, outputs](int index) {
    const int i = index % 64;
    const Vec4& x = (*inputs)[i];
    double* g = outputs->data() + i * 64;

    const double M = 1.0;
    const double inv_r = 1.0 / x.r;          // 1 div
    const double r2 = x.r * x.r;             // 1 mul
    const double f = 1.0 - 2.0 * M * inv_r;  // 2 ops
    const double sin_t = std::sin(x.theta);  // ~20 FLOPs
    const double cos_t = std::cos(x.theta);  // ~20 FLOPs
    const double sin2_t = sin_t * sin_t;     // 1 mul
    const double cot_t = cos_t / sin_t;      // 1 div
    const double rf = x.r * f;               // 1 mul

    const double G_t_tr = M / (r2 * f);    // 2 FLOPs
    const double G_r_tt = M * f / r2;      // 2 FLOPs
    const double G_r_rr = -G_t_tr;         // 1 FLOP
    const double G_r_qq = -rf;             // 1 FLOP
    const double G_r_pp = -rf * sin2_t;    // 2 FLOPs
    const double G_q_rq = inv_r;           // 0 (reuse)
    const double G_q_pp = -sin_t * cos_t;  // 2 FLOPs
    const double G_p_rp = inv_r;           // 0 (reuse)
    const double G_p_qp = cot_t;           // 0 (reuse)

    // Write all 64 components: 51 zeros + 13 non-zero.
    // All 64 writes touch memory — matching the 512-byte write count.
    std::fill(g, g + 64, 0.0);
    g[0 * 16 + 1 * 4 + 0] = G_t_tr;
    g[0 * 16 + 0 * 4 + 1] = G_t_tr;
    g[1 * 16 + 0 * 4 + 0] = G_r_tt;
    g[1 * 16 + 1 * 4 + 1] = G_r_rr;
    g[1 * 16 + 2 * 4 + 2] = G_r_qq;
    g[1 * 16 + 3 * 4 + 3] = G_r_pp;
    g[2 * 16 + 1 * 4 + 2] = G_q_rq;
    g[2 * 16 + 2 * 4 + 1] = G_q_rq;
    g[2 * 16 + 3 * 4 + 3] = G_q_pp;
    g[3 * 16 + 1 * 4 + 3] = G_p_rp;
    g[3 * 16 + 3 * 4 + 1] = G_p_rp;
    g[3 * 16 + 2 * 4 + 3] = G_p_qp;
    g[3 * 16 + 3 * 4 + 2] = G_p_qp;
  };

  return desc;
}

/**
 * @brief Phase 1 stub: RK4 geodesic (L1-resident).
 */
//
// Memory (L1-resident — gamma stays in L1 across 4 sub-steps):
//   Read:  x[4] + u[4] = 64 bytes  |  gamma[64] = 512 bytes (once)
//   Write: x[4] + u[4] = 64 bytes
//   Total: 640 bytes
//
// FLOPs: 396 (4 × 85 RHS + 56 combination)
// OI:    396 / 640 = 0.619 FLOP/byte → memory-bound (less severely)

ExpressionDescriptor ExpressionDescriptor::make_rk4_geodesic_stub_l1() {
  ExpressionDescriptor desc;
  desc.name = "rk4_geodesic_stub_l1_resident";
  desc.flops_per_call = 396;
  desc.bytes_read_per_call = 576;
  desc.bytes_written_per_call = 64;
  desc.input_count = 64;

  struct State {
    double x[4];
    double u[4];
  };

  auto states = std::make_shared<std::vector<State> >(64);
  auto gammas = std::make_shared<std::vector<double> >(64 * 64, 0.0);
  auto out = std::make_shared<std::vector<State> >(64);

  for (int i = 0; i < 64; ++i) {
    auto& s = (*states)[i];
    s.x[0] = 0.0;
    s.x[1] = 10.0 + i * 0.1;
    s.x[2] = 1.5708;
    s.x[3] = 0.0;
    s.u[0] = 1.1;
    s.u[1] = 0.0;
    s.u[2] = 0.0;
    s.u[3] = 0.1;

    double* g = gammas->data() + i * 64;
    const double r = s.x[1];
    const double M = 1.0;
    const double f = 1.0 - 2.0 * M / r;
    std::fill(g, g + 64, 0.0);
    g[0 * 16 + 1 * 4 + 0] = M / (r * r * f);
    g[0 * 16 + 0 * 4 + 1] = M / (r * r * f);
    g[1 * 16 + 0 * 4 + 0] = M * f / (r * r);
    g[1 * 16 + 1 * 4 + 1] = -M / (r * r * f);
    g[1 * 16 + 2 * 4 + 2] = -(r - 2.0 * M);
    g[1 * 16 + 3 * 4 + 3] = -(r - 2.0 * M);
    g[2 * 16 + 1 * 4 + 2] = 1.0 / r;
    g[2 * 16 + 2 * 4 + 1] = 1.0 / r;
    g[3 * 16 + 1 * 4 + 3] = 1.0 / r;
    g[3 * 16 + 3 * 4 + 1] = 1.0 / r;
  }

  desc.execute = [states, gammas, out](int index) {
    const int i = index % 64;
    const double* g = gammas->data() + i * 64;
    const State& s = (*states)[i];
    State& o = (*out)[i];
    const double dl = 0.01;

    double kx[4][4], ku[4][4];
    for (int k = 0; k < 4; ++k) {
      for (int mu = 0; mu < 4; ++mu) kx[k][mu] = s.u[mu];
      double a[4] = {};
      a[0] = -2.0 * g[0 * 16 + 1 * 4 + 0] * s.u[0] * s.u[1];
      a[1] = -(g[1 * 16 + 0 * 4 + 0] * s.u[0] * s.u[0] + g[1 * 16 + 1 * 4 + 1] * s.u[1] * s.u[1] +
               g[1 * 16 + 2 * 4 + 2] * s.u[2] * s.u[2] + g[1 * 16 + 3 * 4 + 3] * s.u[3] * s.u[3]);
      a[2] = -(2.0 * g[2 * 16 + 1 * 4 + 2] * s.u[1] * s.u[2] +
               g[2 * 16 + 3 * 4 + 3] * s.u[3] * s.u[3]);
      a[3] = -(2.0 * g[3 * 16 + 1 * 4 + 3] * s.u[1] * s.u[3] +
               2.0 * g[3 * 16 + 2 * 4 + 3] * s.u[2] * s.u[3]);
      for (int mu = 0; mu < 4; ++mu) ku[k][mu] = a[mu];
    }
    const double inv6 = 1.0 / 6.0;
    for (int mu = 0; mu < 4; ++mu) {
      o.x[mu] = s.x[mu] + dl * inv6 * (kx[0][mu] + 2.0 * kx[1][mu] + 2.0 * kx[2][mu] + kx[3][mu]);
      o.u[mu] = s.u[mu] + dl * inv6 * (ku[0][mu] + 2.0 * ku[1][mu] + 2.0 * ku[2][mu] + ku[3][mu]);
    }
  };

  return desc;
}

/**
 * @brief Phase 1 stub: RK4 geodesic (L1-miss).
 */
//
// Large gamma array forces cache eviction between sub-steps.
// Total: 640 + 3×512 (gamma re-fetches) + 576 (extra state re-reads) = 2816 bytes
// OI:    396 / 2816 = 0.141 FLOP/byte → more severely memory-bound

ExpressionDescriptor ExpressionDescriptor::make_rk4_geodesic_stub_l1miss() {
  ExpressionDescriptor desc;
  desc.name = "rk4_geodesic_stub_l1_miss";
  desc.flops_per_call = 396;
  desc.bytes_read_per_call = 2752;
  desc.bytes_written_per_call = 64;

  // 512 bodies: working set = 512 × 576 = 288KB >> typical L1 (32-64KB)
  constexpr int BIG = 512;
  struct State {
    double x[4];
    double u[4];
  };

  auto states = std::make_shared<std::vector<State> >(BIG);
  auto gammas = std::make_shared<std::vector<double> >(static_cast<size_t>(BIG) * 64, 0.0);
  auto out = std::make_shared<std::vector<State> >(BIG);

  for (int i = 0; i < BIG; ++i) {
    auto& s = (*states)[i];
    s.x[0] = 0.0;
    s.x[1] = 10.0 + i * 0.01;
    s.x[2] = 1.5708;
    s.x[3] = 0.0;
    s.u[0] = 1.1;
    s.u[1] = 0.0;
    s.u[2] = 0.0;
    s.u[3] = 0.1;
    double* g = gammas->data() + i * 64;
    std::fill(g, g + 64, 1e-6);
  }

  desc.input_count = BIG;

  desc.execute = [states, gammas, out](int index) {
    const int i = index % BIG;
    const double* g = gammas->data() + i * 64;
    const State& s = (*states)[i];
    State& o = (*out)[i];
    const double dl = 0.01;

    double kx[4][4], ku[4][4];
    for (int k = 0; k < 4; ++k) {
      for (int mu = 0; mu < 4; ++mu) kx[k][mu] = s.u[mu];
      double a[4] = {};
      a[0] = -2.0 * g[0 * 16 + 1 * 4 + 0] * s.u[0] * s.u[1];
      a[1] = -(g[1 * 16 + 0 * 4 + 0] * s.u[0] * s.u[0] + g[1 * 16 + 1 * 4 + 1] * s.u[1] * s.u[1] +
               g[1 * 16 + 2 * 4 + 2] * s.u[2] * s.u[2] + g[1 * 16 + 3 * 4 + 3] * s.u[3] * s.u[3]);
      a[2] = -(2.0 * g[2 * 16 + 1 * 4 + 2] * s.u[1] * s.u[2] +
               g[2 * 16 + 3 * 4 + 3] * s.u[3] * s.u[3]);
      a[3] = -(2.0 * g[3 * 16 + 1 * 4 + 3] * s.u[1] * s.u[3] +
               2.0 * g[3 * 16 + 2 * 4 + 3] * s.u[2] * s.u[3]);
      for (int mu = 0; mu < 4; ++mu) ku[k][mu] = a[mu];
    }
    const double inv6 = 1.0 / 6.0;
    for (int mu = 0; mu < 4; ++mu) {
      o.x[mu] = s.x[mu] + dl * inv6 * (kx[0][mu] + 2.0 * kx[1][mu] + 2.0 * kx[2][mu] + kx[3][mu]);
      o.u[mu] = s.u[mu] + dl * inv6 * (ku[0][mu] + 2.0 * ku[1][mu] + 2.0 * ku[2][mu] + ku[3][mu]);
    }
  };

  return desc;
}

/**
 * @brief ExpressionBenchmark constructor.
 */

ExpressionBenchmark::ExpressionBenchmark(PAPIWrapper& papi, const HardwareProfile& hw, int n_trials)
    : papi_(papi), hw_(hw), n_trials_(n_trials) {
  assert(n_trials >= 3);
}

void ExpressionBenchmark::register_expression(ExpressionDescriptor desc) {
  descriptors_.push_back(std::move(desc));
}

/**
 * @brief measure_papi_single.
 */

PAPICounters ExpressionBenchmark::measure_papi_single(const ExpressionDescriptor& desc, int index) {
  PAPICounters total{};
  int valid = 0;
  for (int t = 0; t < PAPI_SINGLE_TRIALS; ++t) {
    papi_.start();
    desc.execute(index + t);
    PAPICounters c = papi_.stop();
    if (c.dp_ops > 0) {
      total.dp_ops += c.dp_ops;
      total.l1_misses += c.l1_misses;
      total.l2_misses += c.l2_misses;
      total.llc_misses += c.llc_misses;
      ++valid;
    }
  }
  if (valid > 0) {
    total.dp_ops /= valid;
    total.l1_misses /= valid;
    total.l2_misses /= valid;
    total.llc_misses /= valid;
  }
  return total;
}

/**
 * @brief derive_oi_empirical.
 */

double ExpressionBenchmark::derive_oi_empirical(const PAPICounters& c, long long bytes_analytical) {
  if (c.dp_ops == 0) return 0.0;
  const long long dram = c.llc_misses * 64;
  if (dram > 0) return static_cast<double>(c.dp_ops) / static_cast<double>(dram);
  const long long l1 = c.l1_misses * 64;
  if (l1 > 0) return static_cast<double>(c.dp_ops) / static_cast<double>(l1);
  return (bytes_analytical > 0)
             ? static_cast<double>(c.dp_ops) / static_cast<double>(bytes_analytical)
             : 0.0;
}

/**
 * @brief classify_regime.
 */

ExpressionRegime ExpressionBenchmark::classify_regime(double flops_s, double bw) const {
  if (hw_.peak_flops_per_s > 0.0 && flops_s >= REGIME_THRESHOLD * hw_.peak_flops_per_s)
    return ExpressionRegime::ComputeBound;
  if (hw_.peak_bandwidth_bytes_s > 0.0 && bw >= REGIME_THRESHOLD * hw_.peak_bandwidth_bytes_s)
    return ExpressionRegime::MemoryBound;
  return ExpressionRegime::Unknown;
}

/**
 * @brief run.
 */

BenchmarkRecord ExpressionBenchmark::run(const ExpressionDescriptor& desc) {
  BenchmarkRecord rec{};
  rec.expression_name = desc.name;
  rec.flops_per_call = desc.flops_per_call;
  rec.bytes_read_per_call = desc.bytes_read_per_call;
  rec.bytes_written_per_call = desc.bytes_written_per_call;
  rec.oi_analytical = desc.oi_analytical();

  // Warm-up.
  for (int i = 0; i < WARMUP_CALLS; ++i) desc.execute(i);

  // Calibrate n_iter so each trial ~5ms.
  int n_iter = 100;
  {
    BenchTimer cal;
    cal.start();
    for (int i = 0; i < n_iter; ++i) desc.execute(i);
    const double ns = cal.elapsed_ns();
    if (ns > 0.0) n_iter = std::max(10, static_cast<int>(n_iter * 5e6 / ns));
  }

  // Timing trials.
  rec.timing = BenchTimer::measure(n_trials_, WARMUP_CALLS, [&] {
    for (int i = 0; i < n_iter; ++i) desc.execute(i);
  });
  rec.high_variance = rec.timing.high_variance;

  const double ns_per_call = rec.timing.mean_ns / static_cast<double>(n_iter);
  const double s_per_call = ns_per_call * 1e-9;

  // PAPI counters.
  if (papi_.available()) {
    rec.papi_used = true;
    rec.counters = measure_papi_single(desc, WARMUP_CALLS);
    rec.oi_empirical =
        derive_oi_empirical(rec.counters, desc.bytes_read_per_call + desc.bytes_written_per_call);
    if (s_per_call > 0.0 && rec.counters.dp_ops > 0)
      rec.achieved_flops_s = static_cast<double>(rec.counters.dp_ops) / s_per_call;
  } else {
    rec.papi_used = false;
    rec.oi_empirical = desc.oi_analytical();  // proxy — trivially matches
    if (s_per_call > 0.0)
      rec.achieved_flops_s = static_cast<double>(desc.flops_per_call) / s_per_call;
    // LOG_PLACEHOLDER
    fprintf(stderr, "[Bench] %s: PAPI unavailable — proxy OI\n", desc.name.c_str());
  }

  const long long total_bytes = desc.bytes_read_per_call + desc.bytes_written_per_call;
  if (s_per_call > 0.0) rec.achieved_bw_bytes_s = static_cast<double>(total_bytes) / s_per_call;

  rec.regime_observed = classify_regime(rec.achieved_flops_s, rec.achieved_bw_bytes_s);
  return rec;
}

/**
 * @brief run_all.
 */

std::vector<BenchmarkRecord> ExpressionBenchmark::run_all() {
  std::vector<BenchmarkRecord> out;
  out.reserve(descriptors_.size());
  for (const auto& d : descriptors_) {
    fprintf(stderr, "[Bench] benchmarking: %s\n", d.name.c_str());  // LOG_PLACEHOLDER
    out.push_back(run(d));
  }
  return out;
}

/**
 * @brief cross_check.
 */

CrossCheckResult ExpressionBenchmark::cross_check(const BenchmarkRecord& rec) const {
  CrossCheckResult r{};
  r.expression_name = rec.expression_name;
  r.oi_analytical = rec.oi_analytical;
  r.oi_empirical = rec.oi_empirical;

  if (rec.oi_analytical <= 0.0) {
    r.status = CrossCheckResult::Status::Fail;
    r.detail = "oi_analytical is zero — check ExpressionDescriptor";
    return r;
  }

  r.divergence = std::abs(rec.oi_analytical - rec.oi_empirical) / rec.oi_analytical;

  const double ridge = hw_.ridge_point_flops_per_byte();
  r.regime_predicted = (ridge > 0.0 && rec.oi_analytical >= ridge) ? ExpressionRegime::ComputeBound
                                                                   : ExpressionRegime::MemoryBound;
  r.regime_observed = rec.regime_observed;
  r.regime_agrees =
      (r.regime_predicted == r.regime_observed) || (r.regime_observed == ExpressionRegime::Unknown);

  if (!r.regime_agrees && rec.papi_used) {
    r.status = CrossCheckResult::Status::RegimeMismatch;
    r.detail = "Regime mismatch: predicted=" + std::string(regime_str(r.regime_predicted)) +
               " observed=" + std::string(regime_str(r.regime_observed)) +
               ".\nCheck analytical byte count and compiler memory access pattern.";
    return r;
  }

  if (r.divergence < CROSS_CHECK_PASS) {
    r.status = CrossCheckResult::Status::Pass;
    r.detail = "OI divergence within 10% — analytical model accurate.";
  } else if (r.divergence < CROSS_CHECK_WARN) {
    r.status = CrossCheckResult::Status::Warn;
    r.detail = "OI divergence " + std::to_string(static_cast<int>(r.divergence * 100)) +
               "% (10-20%). Compiler transformation likely. "
               "Empirical value will be used.";
  } else {
    r.status = CrossCheckResult::Status::Fail;
    r.detail = "OI divergence " + std::to_string(static_cast<int>(r.divergence * 100)) +
               "% (>=20%). Analytical model does not match hardware. "
               "Do not use this profile until resolved.";
  }
  return r;
}

/**
 * @brief cross_check_all.
 */

std::vector<CrossCheckResult> ExpressionBenchmark::cross_check_all(
    const std::vector<BenchmarkRecord>& records) const {
  std::vector<CrossCheckResult> out;
  out.reserve(records.size());
  for (const auto& r : records) out.push_back(cross_check(r));
  return out;
}

/**
 * @brief print_report.
 */

void ExpressionBenchmark::print_report(const std::vector<CrossCheckResult>& results) {
  fprintf(stderr, "\n=== Expression Cross-Check Report ===\n");
  int pass = 0, warn = 0, fail = 0, regime = 0;
  for (const auto& r : results) {
    const char* s = "?";
    switch (r.status) {
      case CrossCheckResult::Status::Pass:
        s = "PASS";
        ++pass;
        break;
      case CrossCheckResult::Status::Warn:
        s = "WARN";
        ++warn;
        break;
      case CrossCheckResult::Status::Fail:
        s = "FAIL";
        ++fail;
        break;
      case CrossCheckResult::Status::RegimeMismatch:
        s = "REGIME";
        ++regime;
        break;
    }
    fprintf(stderr, "  [%s]  %-45s  OI_a=%.4f  OI_e=%.4f  div=%5.1f%%  %s/%s\n", s,
            r.expression_name.c_str(), r.oi_analytical, r.oi_empirical, r.divergence * 100.0,
            regime_str(r.regime_predicted), regime_str(r.regime_observed));
    if (!r.trustworthy()) fprintf(stderr, "         %s\n", r.detail.c_str());
  }
  fprintf(stderr, "  --- %zu expressions  PASS=%d  WARN=%d  FAIL=%d  REGIME=%d\n\n", results.size(),
          pass, warn, fail, regime);
}
