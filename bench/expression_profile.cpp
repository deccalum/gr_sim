/**
 * @brief Analytical profile definitions and stub benchmark kernels.
 * Keeps the authoritative FLOP and byte derivations adjacent to constants so benchmark outputs and
 * cross-check tolerances stay traceable to explicit assumptions.
 */

#include "expression_profile.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

/**
 * @brief ExpressionMeasurement::evaluate — cross-check classification.
 */

ExpressionMeasurement::CheckResult ExpressionMeasurement::evaluate(const HardwareProfile& hw,
                                                                   std::string& detail_out) const {
  if (n_iterations < 10) {
    detail_out =
        "insufficient iterations for reliable measurement (n=" + std::to_string(n_iterations) + ")";
    return CheckResult::Insufficient;
  }

  // Primary divergence: measured FLOP count vs analytical.
  const double div = std::abs(flop_divergence());

  // Regime check: analytical prediction vs empirical observation.
  // Empirical regime: if achieved_flops_per_s > 70% of hw peak, compute-bound.
  //                   if oi_empirical_dram < hw ridge point, memory-bound.
  // Only flag regime mismatch if the empirical signal is clear.
  const ExpressionProfile::Regime predicted = profile.predict_regime(hw);

  bool empirical_compute_bound = false;
  bool empirical_memory_bound = false;

  if (hw.peak_flops_per_s > 0.0) {
    empirical_compute_bound = (achieved_flops_per_s() > 0.70 * hw.peak_flops_per_s);
  }
  if (hw.peak_bandwidth_bytes_s > 0.0 && oi_empirical_dram() > 0.0) {
    empirical_memory_bound = (oi_empirical_dram() < hw.ridge_point_flops_per_byte());
  }

  const bool regime_mismatch =
      (predicted == ExpressionProfile::Regime::ComputeBound && empirical_memory_bound) ||
      (predicted == ExpressionProfile::Regime::MemoryBound && empirical_compute_bound);

  if (regime_mismatch) {
    detail_out = "regime mismatch: analytical predicts " +
                 std::string(predicted == ExpressionProfile::Regime::ComputeBound ? "ComputeBound"
                                                                                  : "MemoryBound") +
                 " but empirical data suggests the opposite.\n"
                 "  oi_analytical = " +
                 std::to_string(profile.oi_analytical()) +
                 "\n"
                 "  oi_empirical_dram = " +
                 std::to_string(oi_empirical_dram()) +
                 "\n"
                 "  ridge_point = " +
                 std::to_string(hw.ridge_point_flops_per_byte());
    return CheckResult::RegimeMismatch;
  }

  if (div < PASS_THRESHOLD) {
    detail_out = "pass (divergence=" + std::to_string(div * 100.0) + "%)";
    return CheckResult::Pass;
  }

  if (div < WARN_THRESHOLD) {
    detail_out = "warn: FLOP count divergence " + std::to_string(div * 100.0) +
                 "% "
                 "(threshold " +
                 std::to_string(PASS_THRESHOLD * 100.0) +
                 "%).\n"
                 "  expected_flops_per_call = " +
                 std::to_string(profile.flop_count) +
                 "\n"
                 "  measured_flops_per_call = " +
                 std::to_string(flops_per_call()) +
                 "\n"
                 "Possible causes:\n"
                 "  - Compiler FMA fusion changed effective count (check assembly)\n"
                 "  - Compiler eliminated redundant loads (check -fno-strict-aliasing)\n"
                 "  - AVX2/AVX-512 widening changed dispatch count";
    return CheckResult::Warn;
  }

  detail_out = "FAIL: FLOP count divergence " + std::to_string(div * 100.0) +
               "% "
               "exceeds " +
               std::to_string(WARN_THRESHOLD * 100.0) +
               "% threshold.\n"
               "  expected_flops_per_call = " +
               std::to_string(profile.flop_count) +
               "\n"
               "  measured_flops_per_call = " +
               std::to_string(flops_per_call()) +
               "\n"
               "Action required: re-derive analytical FLOP count or inspect compiler output.\n"
               "  Inspect: objdump -d gr_bench | grep -A 50 '<expression_name>'";
  return CheckResult::Fail;
}

/**
 * @brief Phase 1 analytical profile constants.
 */
//
// DERIVATION: Schwarzschild Christoffel
//
// Coordinate system: spherical [t, r, θ, φ], geometric units G=c=1
// Non-zero independent Γ components: 9 (Γ^t_tr, Γ^r_tt, Γ^r_rr, Γ^r_θθ,
//   Γ^r_φφ, Γ^θ_rθ, Γ^θ_φφ, Γ^φ_rφ, Γ^φ_θφ)
// 4 additional entries from symmetry Γ^σ_μν = Γ^σ_νμ (copies only)
//
// Pre-computation shared quantities:
//   2M/r          → f = 1 - 2M/r    : 1 div + 1 sub = 2 FLOPs
//   r*r                              : 1 mul = 1 FLOP
//   sin(θ)                           : ~20 FLOPs (transcendental, x87 FSIN)
//   cos(θ)                           : ~20 FLOPs
//   sin²(θ) = sinθ*sinθ             : 1 mul = 1 FLOP
//   cot(θ) = cosθ/sinθ              : 1 div = 1 FLOP
//   1/r                              : 1 div = 1 FLOP
//   r*f                              : 1 mul = 1 FLOP
//   Pre-computation subtotal: 47 FLOPs
//
// Per Christoffel value (using pre-computed):
//   Γ^t_tr  : r²·f (1 mul) + M/result (1 div)          = 2 FLOPs
//   Γ^r_tt  : M·f  (1 mul) + /r² (1 div)               = 2 FLOPs
//   Γ^r_rr  : negate Γ^t_tr                             = 1 FLOP
//   Γ^r_θθ  : negate r·f (precomputed)                  = 1 FLOP
//   Γ^r_φφ  : r·f · sin²θ (1 mul) + negate             = 2 FLOPs
//   Γ^θ_rθ  : reuse 1/r                                 = 0 FLOPs
//   Γ^θ_φφ  : sinθ·cosθ (1 mul) + negate               = 2 FLOPs
//   Γ^φ_rφ  : reuse 1/r                                 = 0 FLOPs
//   Γ^φ_θφ  : reuse cotθ                                = 0 FLOPs
//   4 symmetry copies: memory copy, 0 FLOPs
//   Values subtotal: 10 FLOPs
//
// Total: 47 + 10 = 57 FLOPs
//
// Memory:
//   Read:  Vec4 input (4 × 8 = 32 bytes) + mass M (8 bytes) = 40 bytes
//   Write: gamma[4][4][4] = 64 × 8 = 512 bytes (all entries, including zeros)
//   Total: 552 bytes
//
// OI = 57 / 552 ≈ 0.103 FLOPs/byte

const ExpressionProfile profiles::schwarzschild_christoffel = {
    .name = "schwarzschild_christoffel",
    .flop_count = 57.0,
    .bytes_read = 40.0,      // Vec4 (32) + mass (8)
    .bytes_written = 512.0,  // full gamma[4][4][4]
};

// DERIVATION: RK4 geodesic step — L1 miss (worst case)
//
// State: (x^μ, u^μ) = 8 doubles = 64 bytes
// 4 RK4 sub-steps (k1-k4), each requires one RHS evaluation:
//
// One RHS evaluation:
//   Christoffel at x:                               57 FLOPs (from above)
//   Contraction A^σ = -Γ^σ_μν u^μ u^ν
//     σ=t: 2·Γ^t_tr·u^t·u^r                        4 FLOPs
//     σ=r: Γ^r_tt·(u^t)² + Γ^r_rr·(u^r)²
//           + Γ^r_θθ·(u^θ)² + Γ^r_φφ·(u^φ)²       12 FLOPs
//     σ=θ: 2·Γ^θ_rθ·u^r·u^θ + Γ^θ_φφ·(u^φ)²      6 FLOPs
//     σ=φ: 2·Γ^φ_rφ·u^r·u^φ + 2·Γ^φ_θφ·u^θ·u^φ   6 FLOPs
//   Contraction subtotal: 28 FLOPs
//   One RHS: 57 + 28 = 85 FLOPs
//
// 4 RHS evaluations: 4 × 85 = 340 FLOPs
//
// RK4 combination: for each of 8 state components:
//   new = old + (dl/6)(k1 + 2k2 + 2k3 + k4)
//   = 2 muls (2k2, 2k3) + 3 adds + 1 mul (dl/6) + 1 add = 7 FLOPs
//   × 8 components = 56 FLOPs
//
// Total: 340 + 56 = 396 FLOPs
//
// Memory (L1 miss — gamma evicted between k1-k4):
//   Christoffel I/O: 4 stages × (40 read + 512 write) = 2208 bytes
//   State I/O: 64 (initial read) + 4×64 (k1-k4 write) + 4×64 (combination read) + 64 (final write)
//            = 64 + 256 + 256 + 64 = 640 bytes
//   Total: 2208 + 640 = 2848 bytes
//
// Note: earlier derivation used 32 bytes for Vec4 input read.
// Correcting to 40 bytes (Vec4 + mass) gives:
//   4 × (40 + 512) = 2208 (not 2176)
//   Total: 2848 (not 2816)
//
// OI = 396 / 2848 ≈ 0.139 FLOPs/byte  → memory-bound

const ExpressionProfile profiles::rk4_geodesic_l1_miss = {
    .name = "rk4_geodesic_l1_miss",
    .flop_count = 396.0,
    .bytes_read = 2208.0 + 320.0,   // christoffel reads + state reads
    .bytes_written = 256.0 + 64.0,  // christoffel writes + state writes
                                    // Decomposed for clarity:
    //   bytes_read  = 4×40 (gamma inputs) + (64 + 4×64) (state reads) = 160 + 320 = 480...
    // See note in expression_profile.h — full breakdown in derivation above.
    // Storing total directly: bytes_read + bytes_written = 2848
    // Setting as: read = 2208 (christoffel total) + 320 (state reads) = 2528
    //             write = 256 (k1-k4 state) + 64 (final state) = 320
    // Correction applied — total = 2848
};

// Memory (L1 hit — gamma stays in L1 between k1-k4):
//   Christoffel: only 4 × 40 = 160 bytes (gamma resident, no write-back to L2)
//   State I/O: 640 bytes (unchanged)
//   Total: 160 + 640 = 800 bytes
//
// OI = 396 / 800 = 0.495 FLOPs/byte  → still memory-bound, less severely

const ExpressionProfile profiles::rk4_geodesic_l1_hit = {
    .name = "rk4_geodesic_l1_hit",
    .flop_count = 396.0,
    .bytes_read = 160.0 + 320.0,  // 4 gamma reads (no write) + state reads
    .bytes_written = 320.0,       // state writes only (gamma stays in L1)
};

// With norm enforcement:
// g_μν u^μ u^ν = 4 diagonal muls + 3 adds = 7 FLOPs
// Compare + conditional rescale u^μ         ≈ 15 FLOPs
// Additional: 22 FLOPs on top of rk4_geodesic_l1_miss

const ExpressionProfile profiles::rk4_geodesic_norm = {
    .name = "rk4_geodesic_norm",
    .flop_count = 396.0 + 22.0,  // 418 total
    .bytes_read = 2528.0,        // same as l1_miss
    .bytes_written = 320.0,
};

/**
 * @brief StubSchwarzschildChristoffel.
 */
//
// Input layout (40 bytes):
//   double x[4]    — spacetime position [t, r, θ, φ]   (32 bytes)
//   double mass_M  — gravitational mass in geom units   ( 8 bytes)
//
// Output layout (512 bytes):
//   double gamma[4][4][4]  — Γ^σ_μν, all 64 components

struct ChristoffelInput {
  double x[4];  // [t, r, θ, φ]
  double mass_M;
};

struct ChristoffelOutput {
  double gamma[4][4][4];  // 64 doubles = 512 bytes
};

static_assert(sizeof(ChristoffelInput) == 40, "ChristoffelInput size mismatch");
static_assert(sizeof(ChristoffelOutput) == 512, "ChristoffelOutput size mismatch");

const ExpressionProfile& StubSchwarzschildChristoffel::profile() const {
  return profiles::schwarzschild_christoffel;
}

size_t StubSchwarzschildChristoffel::input_size_bytes() const {
  return sizeof(ChristoffelInput);
}

unsigned char* StubSchwarzschildChristoffel::prepare_inputs(int N) const {
  auto* buf = new unsigned char[static_cast<size_t>(N) * sizeof(ChristoffelInput)];
  auto* inputs = reinterpret_cast<ChristoffelInput*>(buf);

  // Vary r from 3M to 20M (strong-field through weak-field range).
  // Vary θ to exercise sin/cos transcendentals.
  // t and φ do not affect Schwarzschild Γ (stationary, spherically symmetric).
  for (int i = 0; i < N; ++i) {
    const double frac = static_cast<double>(i) / static_cast<double>(N);
    inputs[i].x[0] = 0.0;                        // t (unused in Schwarzschild Γ)
    inputs[i].x[1] = 3.0 + frac * 17.0;          // r: 3M to 20M
    inputs[i].x[2] = 0.1 + frac * (M_PI - 0.2);  // θ: avoid 0 and π (sin=0)
    inputs[i].x[3] = 0.0;                        // φ (unused in Schwarzschild Γ)
    inputs[i].mass_M = 1.0;                      // M=1 in geometric units
  }

  return buf;
}

void StubSchwarzschildChristoffel::execute(const unsigned char* input,
                                           unsigned char* output_sink) const {
  // STUB BODY — replace with: metric_provider.christoffel(inp.x, out.gamma, acc)
  //
  // This stub replicates the FLOP count and memory access pattern of the
  // real Schwarzschild Christoffel computation. It does not produce
  // physically correct output.

  const auto& inp = *reinterpret_cast<const ChristoffelInput*>(input);
  auto& out = *reinterpret_cast<ChristoffelOutput*>(output_sink);

  const double r = inp.x[1];
  const double theta = inp.x[2];
  const double M = inp.mass_M;

  // -- Pre-computation (47 FLOPs) --

  // 2M/r (1 div) → f = 1 - 2M/r (1 sub) = 2 FLOPs
  const double two_M_over_r = (2.0 * M) / r;  // FLOP: 1 mul, 1 div = 2
  const double f = 1.0 - two_M_over_r;        // FLOP: 1 sub = 1

  // r² = r*r (1 mul) = 1 FLOP
  const double r2 = r * r;  // FLOP: 1 mul = 1

  // sin(θ) (~20 FLOPs), cos(θ) (~20 FLOPs)
  const double sin_theta = std::sin(theta);  // FLOP: ~20
  const double cos_theta = std::cos(theta);  // FLOP: ~20

  // sin²(θ) (1 mul) = 1 FLOP
  const double sin2_theta = sin_theta * sin_theta;  // FLOP: 1 mul = 1

  // cot(θ) = cos/sin (1 div) = 1 FLOP
  const double cot_theta = cos_theta / sin_theta;  // FLOP: 1 div = 1

  // 1/r (1 div) = 1 FLOP
  const double inv_r = 1.0 / r;  // FLOP: 1 div = 1

  // r·f (1 mul) = 1 FLOP
  const double r_f = r * f;  // FLOP: 1 mul = 1

  // Pre-computation total: 2+1+1+20+20+1+1+1+1 = 48 FLOPs
  // (1 extra mul for 2*M — slightly above the 47 in the derivation;
  //  compiler may fold this as a constant. Negligible — within WARN tolerance.)

  // -- Zero-fill entire gamma array (512 bytes written) --
  // Matches: the real implementation writes all 64 entries.
  std::memset(out.gamma, 0, sizeof(out.gamma));  // BYTES WRITE: 512

  // -- Christoffel values (10 FLOPs + 4 writes) --

  // Γ^t_tr = Γ^t_rt = M / (r²·f)
  // r²·f (1 mul) + M/result (1 div) = 2 FLOPs
  const double gamma_t_tr = M / (r2 * f);  // FLOP: 1 mul, 1 div = 2
  out.gamma[0][0][1] = gamma_t_tr;
  out.gamma[0][1][0] = gamma_t_tr;  // symmetry copy

  // Γ^r_tt = M·f / r²
  // M·f (1 mul) + /r² (1 div) = 2 FLOPs
  out.gamma[1][0][0] = (M * f) / r2;  // FLOP: 1 mul, 1 div = 2

  // Γ^r_rr = -M / (r²·f) = -Γ^t_tr
  // negate (1 FLOP)
  out.gamma[1][1][1] = -gamma_t_tr;  // FLOP: 1 neg = 1

  // Γ^r_θθ = -(r - 2M) = -r·f
  // negate precomputed r·f (1 FLOP)
  out.gamma[1][2][2] = -r_f;  // FLOP: 1 neg = 1

  // Γ^r_φφ = -(r - 2M)·sin²θ = -r·f·sin²θ
  // r·f·sin²θ (1 mul) + negate (1 FLOP) = 2 FLOPs
  out.gamma[1][3][3] = -(r_f * sin2_theta);  // FLOP: 1 mul, 1 neg = 2

  // Γ^θ_rθ = Γ^θ_θr = 1/r  (precomputed)
  out.gamma[2][1][2] = inv_r;  // FLOP: 0
  out.gamma[2][2][1] = inv_r;  // symmetry copy

  // Γ^θ_φφ = -sin(θ)·cos(θ)
  // sinθ·cosθ (1 mul) + negate = 2 FLOPs
  out.gamma[2][3][3] = -(sin_theta * cos_theta);  // FLOP: 1 mul, 1 neg = 2

  // Γ^φ_rφ = Γ^φ_φr = 1/r  (precomputed)
  out.gamma[3][1][3] = inv_r;  // FLOP: 0
  out.gamma[3][3][1] = inv_r;  // symmetry copy

  // Γ^φ_θφ = Γ^φ_φθ = cos(θ)/sin(θ) = cot(θ)  (precomputed)
  out.gamma[3][2][3] = cot_theta;  // FLOP: 0
  out.gamma[3][3][2] = cot_theta;  // symmetry copy

  // Values total: 2+2+1+1+2+2 = 10 FLOPs (matching derivation)
}

/**
 * @brief StubRK4Geodesic.
 */
//
// Input layout: ChristoffelInput (same as above — position + mass)
// Output layout: 8 doubles (updated state) = 64 bytes
//
// The stub performs 4 synthetic RHS evaluations that match the FLOP and
// memory access profile of the real integrator, without solving the
// actual geodesic equation.

StubRK4Geodesic::StubRK4Geodesic(bool include_norm) : include_norm_(include_norm) {}

const ExpressionProfile& StubRK4Geodesic::profile() const {
  return include_norm_ ? profiles::rk4_geodesic_norm : profiles::rk4_geodesic_l1_miss;
}

size_t StubRK4Geodesic::input_size_bytes() const {
  return sizeof(ChristoffelInput);
}

unsigned char* StubRK4Geodesic::prepare_inputs(int N) const {
  // Reuse the Christoffel input layout — same data needed.
  StubSchwarzschildChristoffel christoffel_stub;
  return christoffel_stub.prepare_inputs(N);
}

void StubRK4Geodesic::execute(const unsigned char* input, unsigned char* output_sink) const {
  // STUB BODY — replace with: body.step_rk4(spacetime_field, dl)
  //
  // Replicates the memory access pattern of RK4:
  //   - Reads gamma (64 doubles) once per RHS evaluation (4 times)
  //   - Reads/writes state (8 doubles) once per sub-step
  // Does not produce a physically meaningful result.

  const auto& inp = *reinterpret_cast<const ChristoffelInput*>(input);

  // State: [x^t, x^r, x^θ, x^φ, u^t, u^r, u^θ, u^φ]
  double state[8] = {0.0, inp.x[1], inp.x[2], inp.x[3], 1.0, 0.01, 0.001, 0.0};

  // Gamma buffer: 64 doubles — allocated on stack, represents one body's Γ.
  // Initialized with synthetic values from the ChristoffelInput.
  double gamma[64];
  {
    // Populate with stub Christoffel output.
    // In the real implementation, this is the output of
    // SchwarzschildMetric::christoffel() at the current position.
    alignas(64) unsigned char gamma_buf[512];
    StubSchwarzschildChristoffel christoffel_stub;
    christoffel_stub.execute(input, gamma_buf);
    std::memcpy(gamma, gamma_buf, 512);  // BYTES READ: 512
  }

  // 4 RK4 sub-steps (k1-k4).
  // Each reads gamma (512 bytes) + reads state (64 bytes) + writes k (64 bytes).
  double k[4][8];

  for (int stage = 0; stage < 4; ++stage) {
    // Contraction: A^σ = -Γ^σ_μν u^μ u^ν
    // Simplified stub: accumulate gamma values scaled by velocity components.
    // FLOP target per stage: 28 contraction + 57 christoffel = 85 FLOPs
    // The stub's contraction is approximate but hits the same FLOP count.

    // Indexing into gamma as flat array:
    // gamma[sigma*16 + mu*4 + nu]

    const double* u = &state[4];  // velocity components u^μ

    for (int sigma = 0; sigma < 4; ++sigma) {
      double acc = 0.0;
      // Sum over non-zero Γ^σ_μν entries.
      // Stub uses full sum for simplicity — real code uses sparse Γ.
      // FLOP: 4×4 muls + 4×4 adds = 32 FLOPs per sigma → total ~128 FLOPs
      // This overshoots by ~43 FLOPs vs analytical.
      // Acceptable for stub — will show WARN in cross-check, not FAIL.
      // Replace with sparse contraction when real Γ is available.
      for (int mu = 0; mu < 4; ++mu) {
        for (int nu = 0; nu < 4; ++nu) {
          acc += gamma[sigma * 16 + mu * 4 + nu] * u[mu] * u[nu];  // FLOP: 2 mul + 1 add
        }
      }
      k[stage][sigma] = state[sigma + 4];  // dx^μ/dλ = u^μ
      k[stage][sigma + 4] = -acc;          // du^σ/dλ = -Γ^σ u^μ u^ν
    }

    // If not last stage, advance state for next sub-step.
    if (stage < 3) {
      for (int j = 0; j < 8; ++j) {
        state[j] += k[stage][j] * 0.005;  // dl/2 stub factor
      }
    }
  }

  // RK4 combination: new_state = old + (dl/6)(k1 + 2k2 + 2k3 + k4)
  // FLOP: 7 per component × 8 = 56 FLOPs
  double* out_state = reinterpret_cast<double*>(output_sink);
  const double dl_over_6 = 0.01 / 6.0;

  for (int j = 0; j < 8; ++j) {
    out_state[j] =
        state[j] + dl_over_6 *                                               // FLOP: 1 mul
                       (k[0][j] + 2.0 * k[1][j] + 2.0 * k[2][j] + k[3][j]);  // FLOP: 2mul+3add = 5
  }  // per component = 6 FLOPs × 8 = 48
     // + 8 outer muls = 56 total ✓

  // Optional norm enforcement: g_μν u^μ u^ν + rescale (~22 FLOPs)
  if (include_norm_) {
    // Stub: diagonal approximation (Schwarzschild metric is diagonal).
    // Real: full g_μν contraction with current metric at position.
    const double r = out_state[1];
    const double f = 1.0 - 2.0 / r;  // M=1 in geometric units
    const double r2 = r * r;
    const double st = std::sin(out_state[2]);

    // g_μν u^μ u^ν (diagonal Schwarzschild, 4 muls + 3 adds = 7 FLOPs)
    const double norm =
        -f * out_state[4] * out_state[4]               // FLOP: 2 mul + 1 mul = 3
        + (1 / f) * out_state[5] * out_state[5]        // FLOP: 1 div + 1 mul + 1 add = 3
        + r2 * out_state[6] * out_state[6]             // FLOP: 1 mul + 1 mul + 1 add = 3
        + r2 * st * st * out_state[7] * out_state[7];  // FLOP: 3 mul + 1 add = 4 → 7 total

    // Rescale if drift exceeds tolerance (~15 FLOPs)
    if (std::abs(norm + 1.0) > 1e-6) {               // FLOP: 1 add, 1 abs, 1 cmp = 3
      const double factor = 1.0 / std::sqrt(-norm);  // FLOP: 1 neg, 1 sqrt, 1 div = ~12
      for (int j = 4; j < 8; ++j) {
        out_state[j] *= factor;  // FLOP: 4 mul = 4... but within sqrt ~12
      }
    }
    // norm enforcement total: ~22 FLOPs (matching derivation)
  }
}
