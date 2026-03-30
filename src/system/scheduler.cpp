/**
 * @brief Serial scheduler placeholder.
 * Every packet executes immediately on the calling thread so higher-level orchestration can be
 * exercised before real worker threads and GPU queues are introduced.
 */

#include "scheduler.h"

Scheduler::Scheduler(int n) : n_threads_(n > 0 ? n : 1) {}
Scheduler::~Scheduler() {
  sync();
}

void Scheduler::submit(WorkPacket pkt) {
  pkt.execute();
  if (pkt.on_complete) pkt.on_complete();
}

void Scheduler::sync() {}
double Scheduler::cpu_utilization() const {
  return 0.0;
}
double Scheduler::gpu_utilization() const {
  return 0.0;
}
