/**
 * @brief Deferred-creation queue flushed once per loop at the top of `step_once()`.
 *
 * All structural mutations — adding bodies, observers, and engines — are deferred to this flush
 * point so they never occur mid-step while the integrator holds pointers into `bodies_` or while
 * engines are iterating the observer list. The queue is always cleared unconditionally at the end
 * of `process()`, whether the request succeeds or not.
 */

#include "spawn_queue.h"

#include "engine/body.h"
#include "simulation.h"

/**
 * @brief Appends a spawn request to the back of the pending queue.
 * Ownership of the request (including any heap-allocated EngineParams::name) is moved in.
 */
void SpawnQueue::enqueue(SpawnRequest req) { queue_.push_back(std::move(req)); }

/**
 * @brief Flushes all pending requests into the simulation state.
 *
 * Iterates the queue in arrival order and dispatches each request by type:
 *   - Body:     constructs a `Body` with the next available ID and appends it to `state.bodies`.
 *   - Observer: constructs an `Observer` and registers it with `state.observers`.
 *   - Engine:   not yet wired; silently skipped (engines are loaded directly via `load_engine()`).
 *
 * The queue is cleared after all requests are processed, not inside the loop, so the loop can
 * safely iterate over a stable container and any future error path still drains the queue.
 *
 * @param state Non-owning view into the live simulation containers (bodies, observers, id counter).
 */
void SpawnQueue::process(SimulationState& state) {
  for (auto& req : queue_) {
    switch (req.type) {
      case SpawnRequest::Type::Body: {
        auto& bp = std::get<BodyParams>(req.params);
        Vec4 pos{bp.pos[0], bp.pos[1], bp.pos[2], bp.pos[3]};
        Vec4 vel{bp.vel[0], bp.vel[1], bp.vel[2], bp.vel[3]};
        state.bodies.push_back(std::make_unique<Body>(bp.mass, pos, vel, state.next_id++));
        break;
      }
      case SpawnRequest::Type::Observer: {
        auto& op = std::get<ObserverParams>(req.params);
        Vec4 pos{op.pos[0], op.pos[1], op.pos[2], op.pos[3]};
        Vec4 vel{op.vel[0], op.vel[1], op.vel[2], op.vel[3]};
        state.observers.add(pos, vel);
        break;
      }
      case SpawnRequest::Type::Engine:
        // Engine loading goes through Simulation::load_engine(); nothing to do here.
        break;
    }
  }
  queue_.clear();
}
