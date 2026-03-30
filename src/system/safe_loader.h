#pragma once

/**
 * @brief Declares the pre-flight spawn admission check.
 * The final implementation will query current resource pressure before accepting new bodies,
 * observers, or engines. The current stub always approves requests to keep call sites stable.
 */

#include "spawn_queue.h"

class SimulationState;

/**
 * @brief Placeholder resource gate for spawn requests.
 */
class SafeLoader {
 public:
  SpawnResult check(const SpawnRequest& req, const SimulationState& state);
};
