#pragma once

/**
 * @brief Dispatches backend-neutral work packets to CPU and GPU execution paths.
 * Phase 1 still uses a serial placeholder, but the public API already reflects the intended single
 * sync-point-per-step scheduler design.
 */

#include <functional>
#include <thread>
#include <vector>

class SimulationState;

/**
 * @brief Minimal work packet used by the scheduler stub.
 */
struct WorkPacket {
  std::function<void()> execute;
  std::function<void()> on_complete;
  double weight = 1.0;
  bool gpu_target = false;
};

/**
 * @brief Scheduler facade for future threaded CPU and GPU dispatch.
 */
class Scheduler {
 public:
  explicit Scheduler(int cpu_threads = 0);  // 0 means use the hardware concurrency default.
  ~Scheduler();
  void submit(WorkPacket pkt);
  void sync();  // Blocks until every submitted packet for this step completes.
  double cpu_utilization() const;
  double gpu_utilization() const;  // Remains 0 until a real GPU backend is attached.

 private:
  int n_threads_;
  std::vector<std::thread> pool_;
  // The current stub dispatches synchronously until the thread pool and GPU queue are implemented.
};
