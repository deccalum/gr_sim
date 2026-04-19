/**
 * @brief Expression benchmark runtime.
 * Provides a reliable windowed measurement harness for empirical hardware validation.
 * The core logic runs a provided `BenchmarkableExpression` repeatedly, timing
 * the execution and reading hardware performance counters via PAPI.
 */

#include "expression_benchmark.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

ExpressionBenchmark::ExpressionBenchmark(PAPIWrapper& papi, const HardwareProfile& hw,
                                         BenchmarkConfig cfg)
    : papi_(papi), hw_(hw), cfg_(cfg) {
  if (cfg_.n_trials < 3) {
    if (cfg_.verbose) {
      fprintf(stderr, "[Warn] n_trials < 3. Outlier rejection requires >= 3 trials. Forcing to 3.\n");
    }
    cfg_.n_trials = 3;
  }
}

int ExpressionBenchmark::calibrate_n(const BenchmarkableExpression& expr) const {
  // Allocate minimal working memory for calibration.
  unsigned char* cal_input = expr.prepare_inputs(1);
  const size_t out_sz = static_cast<size_t>(expr.profile().bytes_written + 4096);
  std::vector<unsigned char> output_sink(out_sz, 0);

  int n_iter = cfg_.min_n;

  // Warmup
  for (int i = 0; i < 4; ++i) {
    expr.execute(cal_input, output_sink.data());
  }

  BenchTimer timer;
  timer.start();
  for (int i = 0; i < n_iter; ++i) {
    expr.execute(cal_input, output_sink.data());
  }
  const double elapsed_ns = timer.elapsed_ns();

  delete[] cal_input;

  if (elapsed_ns > 0.0) {
    double scale = cfg_.target_window_ns / elapsed_ns;
    n_iter = std::max(cfg_.min_n, static_cast<int>(n_iter * scale));
  }

  if (cfg_.verbose) {
    fprintf(stderr, "[Bench] calibrated N=%d for %s\n", n_iter, expr.profile().name.c_str());
  }

  return n_iter;
}

ExpressionBenchmark::WindowResult ExpressionBenchmark::run_window(
    const BenchmarkableExpression& expr, const unsigned char* inputs,
    unsigned char* output_sink, int N) const {
  WindowResult res;
  res.n = N;

  const size_t step = expr.input_size_bytes();
  const int max_inputs = cfg_.n_inputs;

  papi_.start();
  BenchTimer timer;
  timer.start();

  for (int i = 0; i < N; ++i) {
    expr.execute(inputs + (i % max_inputs) * step, output_sink);
  }

  res.elapsed_ns = timer.elapsed_ns();
  res.counters = papi_.stop();
  res.valid = true;

  return res;
}

ExpressionMeasurement ExpressionBenchmark::aggregate(
    const ExpressionProfile& profile, const std::vector<WindowResult>& trials) {
  ExpressionMeasurement meas;
  meas.profile = profile;

  if (trials.empty()) return meas;

  std::vector<WindowResult> sorted = trials;
  std::sort(sorted.begin(), sorted.end(),
            [](const WindowResult& a, const WindowResult& b) {
              return a.elapsed_ns < b.elapsed_ns;
            });

  int start_idx = 0;
  int end_idx = static_cast<int>(sorted.size());

  // Drop fastest and slowest if we have at least 3 trials
  if (sorted.size() >= 3) {
    start_idx = 1;
    end_idx -= 1;
  }

  long long t_dp = 0;
  long long t_l1 = 0;
  long long t_l2 = 0;
  long long t_llc = 0;
  double t_ns = 0.0;
  int t_n = 0;

  int count = 0;
  for (int i = start_idx; i < end_idx; ++i) {
    t_dp += sorted[i].counters.dp_ops;
    t_l1 += sorted[i].counters.l1_misses;
    t_l2 += sorted[i].counters.l2_misses;
    t_llc += sorted[i].counters.llc_misses;
    t_ns += sorted[i].elapsed_ns;
    t_n += sorted[i].n;
    ++count;
  }

  if (count > 0) {
    meas.actual_flops = t_dp / count;
    meas.actual_l1_misses = t_l1 / count;
    meas.actual_l2_misses = t_l2 / count;
    meas.actual_llc_misses = t_llc / count;
    meas.elapsed_ns = t_ns / count;
    meas.n_iterations = t_n / count;
  }

  return meas;
}

ExpressionMeasurement ExpressionBenchmark::run(const BenchmarkableExpression& expr) const {
  const int n_iter = calibrate_n(expr);
  unsigned char* inputs = expr.prepare_inputs(cfg_.n_inputs);

  const size_t out_sz = static_cast<size_t>(expr.profile().bytes_written + 4096);
  std::vector<unsigned char> output_sink(out_sz, 0);

  // Warmup
  run_window(expr, inputs, output_sink.data(), n_iter / 10 + 1);

  std::vector<WindowResult> trials;
  trials.reserve(cfg_.n_trials);

  for (int t = 0; t < cfg_.n_trials; ++t) {
    trials.push_back(run_window(expr, inputs, output_sink.data(), n_iter));
  }

  delete[] inputs;
  return aggregate(expr.profile(), trials);
}
