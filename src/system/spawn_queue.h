#pragma once

/**
 * @brief Queues spawn requests until a deterministic flush point in the main loop.
 * Deferring body, observer, and engine creation avoids mid-step structural mutations and keeps the
 * scheduler free of data-race-prone ownership changes.
 */

#include <string>
#include <variant>
#include <vector>

struct BodyParams {
  double mass;
  double pos[4];
  double vel[4];
};
struct ObserverParams {
  double pos[4];
  double vel[4];
};
struct EngineParams {
  std::string name;
};

struct SpawnRequest {
  enum class Type { Body, Observer, Engine };
  Type type;
  std::variant<BodyParams, ObserverParams, EngineParams> params;
};

enum class SpawnResult { Approved, Rejected };

class SimulationState;

/**
 * @brief FIFO queue for deferred structural changes to the simulation state.
 */
class SpawnQueue {
 public:
  void enqueue(SpawnRequest req);
  void process(SimulationState& state);  // Flush all pending requests once per loop.
  size_t pending_count() const { return queue_.size(); }

 private:
  std::vector<SpawnRequest> queue_;
};
