/**
 * @brief Stub spawn queue that only records and clears deferred requests.
 */

#include "spawn_queue.h"

void SpawnQueue::enqueue(SpawnRequest req) {
  queue_.push_back(std::move(req));
}
void SpawnQueue::process(SimulationState&) {
  queue_.clear();
}
