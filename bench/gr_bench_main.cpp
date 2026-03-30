/**
 * @brief Entry point for hardware probing and expression cross-check benchmarking.
 * Runs timer/PAPI validation, hardware peak measurements, and expression benchmarks, then writes a
 * reusable HardwareProfile cache for simulator startup.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "bench_timer.h"
#include "expression_benchmark.h"
#include "expression_profile.h"
#include "hardware_peak_probe.h"
#include "papi_wrapper.h"

/**
 * @brief CLI parsing.
 */

struct Args {
  std::string output_path = "cache/hardware_profile.bin";
  double probe_duration = 0.5;
  int trials = 7;
  bool run_cliff = true;
  bool run_expressions = true;
  bool verbose = false;
  bool help = false;
  bool valid = true;
  std::string error_msg;
};

static Args parse_args(int argc, char** argv) {
  Args a;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      a.help = true;
      return a;
    } else if (arg == "--output" || arg == "-o") {
      if (++i >= argc) {
        a.valid = false;
        a.error_msg = "--output requires a path";
        return a;
      }
      a.output_path = argv[i];
    } else if (arg == "--probe-duration") {
      if (++i >= argc) {
        a.valid = false;
        a.error_msg = "--probe-duration requires a value";
        return a;
      }
      a.probe_duration = std::atof(argv[i]);
      if (a.probe_duration <= 0.0) {
        a.valid = false;
        a.error_msg = "--probe-duration must be > 0";
        return a;
      }
    } else if (arg == "--trials") {
      if (++i >= argc) {
        a.valid = false;
        a.error_msg = "--trials requires a value";
        return a;
      }
      a.trials = std::atoi(argv[i]);
      if (a.trials < 3) {
        a.valid = false;
        a.error_msg = "--trials must be >= 3";
        return a;
      }
    } else if (arg == "--no-cliff") {
      a.run_cliff = false;
    } else if (arg == "--no-expressions") {
      a.run_expressions = false;
    } else if (arg == "--verbose") {
      a.verbose = true;
    } else {
      a.valid = false;
      a.error_msg = "unknown argument: " + arg;
      return a;
    }
  }
  return a;
}

static void print_usage() {
  fprintf(stdout,
          "gr_bench — GR simulator hardware characterization tool\n"
          "\n"
          "Usage: gr_bench [options]\n"
          "\n"
          "Options:\n"
          "  --output <path>         Cache output path (default: cache/hardware_profile.bin)\n"
          "  --probe-duration <s>    Duration per hardware probe, seconds (default: 0.5)\n"
          "  --trials <n>            Trials per expression benchmark (default: 7)\n"
          "  --no-cliff              Skip L1 cliff probe (saves ~2s)\n"
          "  --no-expressions        Hardware probe only, skip expression benchmarks\n"
          "  --verbose               Print intermediate measurements\n"
          "  --help                  Print this message\n"
          "\n"
          "Exit codes:\n"
          "  0   All checks passed, hardware profile written\n"
          "  1   Self-test failure — results untrustworthy\n"
          "  2   Hardware probe failed — cache not written\n"
          "  3   Cross-check FAIL or REGIME_MISMATCH — cache written, verify results\n"
          "  4   Bad arguments\n"
          "\n"
          "Run this once per machine before launching the simulator.\n"
          "The simulator reads the cache at startup and skips re-measurement.\n"
          "Re-run after hardware changes, driver updates, or if the simulator\n"
          "reports stale cache (> 7 days old).\n");
}

/**
 * @brief Section header helpers.
 */

static void section(const char* title) {
  fprintf(stdout, "\n=== %s ", title);
  for (int i = static_cast<int>(strlen(title)); i < 50; ++i) fputc('=', stdout);
  fputc('\n', stdout);
}

static void pass(const char* label) {
  fprintf(stdout, "  [PASS]  %s\n", label);
}

static void warn(const char* label, const char* detail) {
  fprintf(stdout, "  [WARN]  %s\n          %s\n", label, detail);
}

static void fail(const char* label, const char* detail) {
  fprintf(stdout, "  [FAIL]  %s\n          %s\n", label, detail);
}

static void info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stdout, "  ");
  vfprintf(stdout, fmt, args);
  fputc('\n', stdout);
  va_end(args);
}

/**
 * @brief Phase 1: Self-tests.
 */

// Returns true if all self-tests pass.
static bool run_self_tests(PAPIWrapper& papi, bool verbose) {
  section("SELF-TESTS");
  bool all_ok = true;

  // Timer self-test.
  const TimerSelfTestResult timer_result = BenchTimer::self_test();
  if (!timer_result.passed) {
    fail("BenchTimer", timer_result.failure_reason.c_str());
    all_ok = false;
  } else {
    pass("BenchTimer");
    if (verbose) {
      info("resolution = %ldns", timer_result.resolution_ns);
      info("sleep accuracy: measured=%.1fns  error=%.3f%%", timer_result.sleep_measured_ns,
           timer_result.sleep_error_fraction * 100.0);
    }
    if (!timer_result.advisory.empty()) {
      warn("BenchTimer advisory", timer_result.advisory.c_str());
    }
  }

  // PAPI init + self-test.
  if (!papi.init()) {
    warn("PAPI", "init failed — running without hardware counters (proxy mode)");
    // Not fatal — benchmark degrades gracefully.
  } else {
    const SelfTestResult papi_result = papi.self_test();
    if (!papi_result.passed) {
      fail("PAPI", papi_result.failure_reason.c_str());
      // PAPI failure is a hard error: cross-check is the whole point.
      all_ok = false;
    } else {
      pass("PAPI");
      if (verbose) {
        info("dp_ops N=10000: measured=%lld  expected=20000  ratio=%.4f",
             papi_result.dp_ops_n_small, papi_result.fma_ratio);
        info("linearity ratio = %.4f (expected 10.0)", papi_result.linearity_ratio);
      }
      if (!papi_result.advisory.empty()) {
        warn("PAPI advisory", papi_result.advisory.c_str());
      }
      if (papi.flops_only()) {
        warn("PAPI", "cache miss counters unavailable — OI_dram will be 0");
      }
    }
  }

  return all_ok;
}

/**
 * @brief Phase 2: Hardware probe.
 */

// Returns false if probe failed and cache should not be written.
static bool run_hardware_probe(PAPIWrapper& papi, const Args& args, HardwareProfile& hw_out) {
  section("HARDWARE CHARACTERIZATION");

  ProbeConfig probe_cfg;
  probe_cfg.target_duration_s = args.probe_duration;
  probe_cfg.run_l1_cliff = args.run_cliff;
  probe_cfg.verbose = args.verbose;

  HardwarePeakProbe probe(papi, probe_cfg);
  hw_out = probe.run_all();

  if (!hw_out.valid()) {
    fail("HardwareProfile", "peak FLOP/s or bandwidth probe returned zero");
    return false;
  }

  pass("HardwareProfile");
  info("peak FLOP/s      = %.3f GFLOP/s", hw_out.peak_flops_per_s / 1e9);
  info("peak bandwidth   = %.2f GB/s", hw_out.peak_bandwidth_bytes_s / 1e9);
  info("ridge point      = %.4f FLOP/byte", hw_out.ridge_point_flops_per_byte());
  info("L1 cache         = %zu KB", hw_out.cache.l1_bytes / 1024);
  info("L2 cache         = %zu KB", hw_out.cache.l2_bytes / 1024);
  info("L3 cache         = %zu MB", hw_out.cache.l3_bytes / (1024 * 1024));

  if (args.run_cliff) {
    info("L1 cliff bodies  = %d  (working set = %zu bytes)", hw_out.l1_cliff_bodies,
         static_cast<size_t>(hw_out.l1_cliff_bodies) * 576);
  }

  // Sanity check: ridge point should be between 1 and 100 FLOP/byte for
  // any current desktop/server hardware. Outside this range suggests a
  // miscalibration.
  const double ridge = hw_out.ridge_point_flops_per_byte();
  if (ridge < 1.0 || ridge > 200.0) {
    warn("ridge point",
         "value outside expected range [1, 200] FLOP/byte — "
         "re-run with --verbose to inspect probe results");
  }

  return true;
}

/**
 * @brief Phase 3: Expression benchmarks + cross-check.
 */

// Returns true if all expressions passed (PASS or WARN, not FAIL/MISMATCH).
static bool run_expression_benchmarks(PAPIWrapper& papi, const HardwareProfile& hw,
                                      const Args& args) {
  section("EXPRESSION BENCHMARKS");

  BenchmarkConfig bench_cfg;
  bench_cfg.n_trials = args.trials;
  bench_cfg.verbose = args.verbose;

  ExpressionBenchmark bench(papi, hw, bench_cfg);

  // Expressions to benchmark. All Phase 1 stubs.
  // Add new expressions here as physics code is implemented:
  //   1. Implement real expression (replace stub)
  //   2. Add profile to namespace profiles:: in expression_profile.h
  //   3. Add entry here

  struct BenchEntry {
    const char* label;
    BenchmarkableExpression* expr;
  };

  StubSchwarzschildChristoffel christoffel_stub;
  StubRK4Geodesic rk4_stub(false);
  StubRK4Geodesic rk4_norm_stub(true);

  const BenchEntry entries[] = {
      {"schwarzschild_christoffel", &christoffel_stub},
      {"rk4_geodesic (no norm)", &rk4_stub},
      {"rk4_geodesic (with norm)", &rk4_norm_stub},
  };
  const int n_entries = static_cast<int>(sizeof(entries) / sizeof(entries[0]));

  bool all_ok = true;

  for (int i = 0; i < n_entries; ++i) {
    const BenchEntry& e = entries[i];
    fprintf(stdout, "\n  --- %s ---\n", e.label);

    const ExpressionMeasurement m = bench.run(*e.expr);

    // Print measurement summary.
    info("flops/call       measured=%.1f  expected=%.1f", m.flops_per_call(), m.profile.flop_count);
    info("elapsed/call     %.2f ns", m.elapsed_ns_per_call());
    info("achieved FLOP/s  %.3f GFLOP/s", m.achieved_flops_per_s() / 1e9);
    info("oi_dram          %.4f  (analytical %.4f)", m.oi_empirical_dram(),
         m.profile.oi_analytical());
    info("oi_l2            %.4f", m.oi_empirical_l2());
    info("flop_divergence  %.2f%%", m.flop_divergence() * 100.0);

    // Regime prediction.
    const auto regime = m.profile.predict_regime(hw);
    info("predicted regime %s", regime == ExpressionProfile::Regime::ComputeBound  ? "ComputeBound"
                                : regime == ExpressionProfile::Regime::MemoryBound ? "MemoryBound"
                                                                                   : "Unknown");

    // Cross-check evaluation.
    std::string detail;
    const auto check = m.evaluate(hw, detail);

    switch (check) {
      case ExpressionMeasurement::CheckResult::Pass:
        pass(e.label);
        break;
      case ExpressionMeasurement::CheckResult::Warn:
        warn(e.label, detail.c_str());
        // WARN is not a hard failure — simulator may proceed.
        break;
      case ExpressionMeasurement::CheckResult::Fail:
        fail(e.label, detail.c_str());
        all_ok = false;
        break;
      case ExpressionMeasurement::CheckResult::RegimeMismatch:
        fail(e.label, detail.c_str());
        all_ok = false;
        break;
      case ExpressionMeasurement::CheckResult::Insufficient:
        warn(e.label, detail.c_str());
        break;
    }
  }

  return all_ok;
}

/**
 * @brief Phase 4: Write cache.
 */

static bool write_cache(const HardwareProfile& hw, const std::string& path) {
  section("CACHE");

  // Ensure directory exists.
  const std::filesystem::path out_path(path);
  const auto parent = out_path.parent_path();
  if (!parent.empty() && !std::filesystem::exists(parent)) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      fail("create directory", ec.message().c_str());
      return false;
    }
  }

  if (!hw.save(path)) {
    fail("write cache", path.c_str());
    return false;
  }

  pass("cache written");
  info("path: %s", path.c_str());
  info("simulator will load this at startup.");
  info("re-run gr_bench if hardware changes or cache is > 7 days old.");

  return true;
}

/**
 * @brief main.
 */

int main(int argc, char** argv) {
  const Args args = parse_args(argc, argv);

  if (!args.valid) {
    fprintf(stderr, "error: %s\n", args.error_msg.c_str());
    fprintf(stderr, "Run 'gr_bench --help' for usage.\n");
    return 4;
  }

  if (args.help) {
    print_usage();
    return 0;
  }

  fprintf(stdout, "gr_bench — GR simulator hardware characterization\n");
  fprintf(stdout, "output: %s\n", args.output_path.c_str());

  PAPIWrapper papi;
  int exit_code = 0;

  // Phase 1: Self-tests.
  if (!run_self_tests(papi, args.verbose)) {
    fprintf(stdout,
            "\nSelf-test failed. Results are untrustworthy.\n"
            "Cache not written. Fix the reported issues and re-run.\n");
    return 1;
  }

  // Phase 2: Hardware probe.
  HardwareProfile hw;
  if (!run_hardware_probe(papi, args, hw)) {
    fprintf(stdout,
            "\nHardware probe failed. Cache not written.\n"
            "Run with --verbose for diagnostic detail.\n");
    return 2;
  }

  // Phase 3: Expression benchmarks.
  if (args.run_expressions) {
    if (!run_expression_benchmarks(papi, hw, args)) {
      fprintf(stdout,
              "\nOne or more expression cross-checks FAILED.\n"
              "Cache will be written but expression profiles may be wrong.\n"
              "Do not trust roofline-derived budgets until FAILs are resolved.\n"
              "See expression_profile.cpp for derivation — re-derive and re-run.\n");
      exit_code = 3;  // write cache anyway, flag the failure
    }
  }

  // Phase 4: Write cache.
  if (!write_cache(hw, args.output_path)) {
    return 2;
  }

  // Summary.
  section("SUMMARY");
  if (exit_code == 0) {
    fprintf(stdout,
            "  All checks passed. Hardware profile cached.\n"
            "  The simulator is ready to run.\n");
  } else if (exit_code == 3) {
    fprintf(stdout,
            "  Hardware profile cached with warnings.\n"
            "  Expression cross-checks require attention before trusting roofline budgets.\n"
            "  See FAIL details above.\n");
  }
  fprintf(stdout, "\n");

  return exit_code;
}
