#pragma once

/**
 * @brief Spawn request types and the FIFO queue that defers their execution.
 *
 * All structural mutations — adding bodies, observers, and engines — are enqueued here and
 * flushed at the top of `step_once()` before any integration occurs. This guarantees that the
 * body list and observer registry are never modified mid-step while the integrator holds
 * pointers into them or while engines are iterating the state.
 */

#include <string>
#include <variant>
#include <vector>

/** @brief Parameters required to spawn a new massive or massless body. */
struct BodyParams {
  double mass;    // Gravitational mass in geometric units (0 for photons).
  double pos[4];  // Initial 4-position x^μ = (t, x, y, z).
  double vel[4];  // Initial 4-velocity u^μ (caller must satisfy the norm constraint).
};

/** @brief Parameters required to spawn a new observer. */
struct ObserverParams {
  double pos[4];   // Initial 4-position x^μ = (t, x, y, z).
  double vel[4];   // Initial 4-velocity u^μ.
  double M = 0.0;  // Black hole mass forwarded to Observer for lapse computation in measure().
};

/** @brief Parameters required to load a named engine plugin. */
struct EngineParams {
  std::string name;  // Registered engine identifier looked up in the engine registry.
};

/**
 * @brief Tagged union carrying the parameters for one pending spawn.
 * The `type` tag and `params` variant are always consistent — use `type` to select the
 * correct `std::get<>` specialisation when extracting params.
 */
struct SpawnRequest {
  enum class Type { Body, Observer, Engine };
  Type type;
  std::variant<BodyParams, ObserverParams, EngineParams> params;
};

/** @brief Outcome returned by `SafeLoader::check()` and acted on by `SpawnQueue::process()`. */
enum class SpawnResult { Approved, Rejected };

struct SimulationState;

/**
 * @brief FIFO queue for deferred structural changes to the simulation state.
 *
 * Requests are admitted through `SafeLoader::check()` before any construction takes place,
 * so the queue always drains cleanly even if some requests are rejected.
 */
class SpawnQueue {
 public:
  /** @brief Appends a request to the back of the queue; ownership of params is moved in. */
  void enqueue(SpawnRequest req);

  /** @brief Flushes all pending requests into the simulation state. Called once per loop. */
  void process(SimulationState& state);

  /** @brief Returns the number of requests waiting to be flushed. */
  size_t pending_count() const { return queue_.size(); }

 private:
  std::vector<SpawnRequest> queue_;
};
