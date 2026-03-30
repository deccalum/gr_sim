#pragma once

/**
 * @brief Declares budget, quality, and backend abstractions for active computation.
 * The Accurator stack uses these types to cap iterative work, report convergence quality, and hand
 * backend-agnostic work packets to CPU or GPU schedulers.
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Categorizes the reliability of a budgeted computation result.
 */
enum class ResultQuality {
  Exact,      // closed-form evaluation, no budget consumed
  Converged,  // series/iterator reached convergence_eps before budget
  Truncated,  // budget exhausted, partial result returned — usable
  Diverged,   // term exceeded divergence_max, result unreliable — log + skip
  Failed      // could not produce any result — must not be used
};

inline const char* quality_str(ResultQuality q) {
  switch (q) {
    case ResultQuality::Exact:
      return "exact";
    case ResultQuality::Converged:
      return "converged";
    case ResultQuality::Truncated:
      return "truncated";
    case ResultQuality::Diverged:
      return "diverged";
    case ResultQuality::Failed:
      return "failed";
  }
  return "unknown";
}

/**
 * @brief Hard caps and convergence thresholds for one iterative computation.
 * Closed-form expressions can ignore this structure, but series expansions and iterative solvers
 * should check it on every term or iteration.
 */
struct ComputeBudget {
  // Iteration cap: expression returns current result when reached.
  // 0 = no cap (use convergence_eps alone to stop).
  int max_iterations = 64;

  // Wall-time cap in nanoseconds. 0 = no time cap.
  // Prefer iteration cap for determinism. Time cap is a safety net.
  double max_time_ns = 0.0;

  // Convergence threshold: stop early when |delta| < eps.
  // Series: magnitude of last term. Iterator: change between steps.
  double convergence_eps = 1e-9;

  // Divergence guard: if any intermediate term exceeds this, abort.
  // Returns Diverged. Prevents inf/nan propagation into the field.
  double divergence_max = 1e15;

  // Minimum iterations before early-exit is allowed.
  // Prevents spurious convergence on first term.
  int min_iterations = 2;

  // Preset constructors — match AccuracyProfile presets
  static ComputeBudget Fast() {
    return {
        .max_iterations = 16, .convergence_eps = 1e-4, .divergence_max = 1e12, .min_iterations = 1};
  }
  static ComputeBudget Balanced() {
    return {
        .max_iterations = 64, .convergence_eps = 1e-9, .divergence_max = 1e15, .min_iterations = 2};
  }
  static ComputeBudget High() {
    return {.max_iterations = 256,
            .convergence_eps = 1e-12,
            .divergence_max = 1e15,
            .min_iterations = 4};
  }
  static ComputeBudget Max() {
    return {.max_iterations = 1024,
            .max_time_ns = 0,
            .convergence_eps = 1e-15,
            .divergence_max = 1e15,
            .min_iterations = 4};
  }
};

/**
 * @brief Bundles a computed value with quality and resource-usage metadata.
 * @tparam T Value type produced by the budgeted expression.
 */
template <typename T>
struct BudgetedResult {
  T value;
  ResultQuality quality = ResultQuality::Failed;
  int iterations_used = 0;
  double final_delta = 0.0;  // last convergence delta (0 if N/A)
  double elapsed_ns = 0.0;   // actual wall time used

  bool usable() const {
    return quality == ResultQuality::Exact || quality == ResultQuality::Converged ||
           quality == ResultQuality::Truncated;
  }
};

/**
 * @brief Selects the preferred execution device for a work packet.
 */
enum class WorkTarget {
  CPU,  // must run on CPU (geodesic integration, AMR decisions, logic)
  GPU,  // prefer GPU (field/metric evaluation over large cell batches)
  Any   // scheduler chooses based on current load
};

/**
 * @brief Backend-neutral unit of work submitted to the scheduler.
 * CPU work may run directly in a thread pool, while GPU backends can batch and upload equivalent
 * closures behind the same interface.
 */
struct WorkPacket {
  uint64_t id;  // unique per step, for tracking
  WorkTarget target;
  double weight;          // relative priority (0.0 - 1.0)
  std::string subsystem;  // "geodesic", "field_eval", "amr", etc.

  // The actual work. CPU path: called directly in thread pool.
  // GPU path: backend may serialize/upload this or call a GPU kernel.
  // Signature is uniform — backend decides execution model.
  std::function<void()> execute;

  // Called after execute() completes, always on CPU.
  // Use for: collecting results, updating state, logging quality.
  std::function<void()> on_complete;
};

/**
 * @brief Distributes one simulation step's compute budget across competing subsystems.
 * Requests combine preset weight, curvature, and backend pressure into a per-expression budget that
 * can be reported and tuned after the step finishes.
 */
class BudgetAllocator {
 public:
  // Call once at startup. Runs a brief benchmark to measure
  // how many iterations/ns the current hardware can sustain.
  // Sets internal calibration — all budgets scaled from this.
  void calibrate();

  // Call at the start of each simulation step.
  // cpu_load_hint and gpu_load_hint: 0.0-1.0, current utilization.
  // Used to scale budgets dynamically if hardware is saturated.
  void begin_step(double cpu_load_hint = 0.0, double gpu_load_hint = 0.0);

  // Request a ComputeBudget for one expression.
  //   subsystem: human-readable label for reporting ("geodesic:body0")
  //   base_weight: from AccuracyProfile (0.0-1.0)
  //   curvature_proxy: r/M ratio at evaluation point.
  //     Values below limits::strong_field_r_over_M amplify weight.
  //     Pass 1e9 for "flat/distant" (no amplification).
  ComputeBudget request(const std::string& subsystem, double base_weight,
                        double curvature_proxy = 1e9);

  // Call at end of step to finalize report.
  void end_step();

  // Per-step diagnostics.
  struct StepReport {
    int total_requests;
    int truncated_count;        // how many expressions hit iteration cap
    int diverged_count;         // how many expressions diverged
    double budget_utilization;  // 0.0-1.0, fraction of total budget used
    double cpu_ms_used;
    double gpu_ms_used;
  };
  const StepReport& last_report() const;

 private:
  struct AllocationEntry {
    std::string subsystem;
    double weight_requested;
    double weight_granted;
  };

  double calibrated_iterations_per_ns_ = 1.0;
  double total_budget_this_step_ = 0.0;
  double allocated_this_step_ = 0.0;
  std::vector<AllocationEntry> log_;
  StepReport last_report_{};

  // Amplify weight near strong-field regions.
  // Returns multiplier >= 1.0. Peaks at r->2M.
  static double curvature_weight_multiplier(double r_over_M);
};

/**
 * @brief Abstract execution backend for CPU, CUDA, Vulkan, or fallback dispatch.
 */
class ComputeBackend {
 public:
  virtual ~ComputeBackend() = default;

  // Submit a batch of WorkPackets. Backend decides threading/upload model.
  // Returns immediately if async; call sync() to wait for completion.
  virtual void submit(std::vector<WorkPacket>& packets) = 0;

  // Block until all submitted packets have completed.
  virtual void sync() = 0;

  // Current utilization estimate. 0.0-1.0.
  // Used by BudgetAllocator to scale budgets under load.
  virtual double utilization() const = 0;

  // Human-readable backend name for logging.
  virtual const char* name() const = 0;

  // Factory: construct backend by name. Returns CPUFallback if
  // requested backend is unavailable (logs a warning).
  static std::unique_ptr<ComputeBackend> create(const std::string& backend_name);
};

// ---------------------------------------------------------------------------
// FUTURE: CPUFallbackBackend stub
//
// Location: compute/cpu_fallback_backend.h
// Always compiled. Runs all WorkPackets sequentially on the calling thread.
// No threading. Used for: development, debugging, platforms without GPU.
// Replace by connecting real backends — no call sites change.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// FUTURE: VulkanComputeBackend stub
//
// Location: compute/vulkan_compute_backend.h
// Compiled when BACKEND_VULKAN=ON. Manages:
//   - VkDevice, VkQueue (compute queue, separate from graphics)
//   - SSBO upload of AMR cell batch (positions, accuracy params)
//   - Dispatch of metric/Christoffel compute shader
//   - Readback of evaluated g_μν and Γ^σ_μν into AMR cells
// Shader inputs/outputs defined by WorkPacket content — no changes
// to BudgetAllocator or Scheduler when this is connected.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// FUTURE: CUDABackend stub
//
// Location: compute/cuda_backend.h
// Compiled when BACKEND_CUDA=ON. Same WorkPacket interface.
// Manages: cudaMalloc, kernel launch, cudaDeviceSynchronize.
// ---------------------------------------------------------------------------
