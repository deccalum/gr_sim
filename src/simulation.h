#pragma once

/**
 * @brief Top-level orchestrator for field, bodies, observers, engines, and timing.
 *
 * Construction follows the field-first startup contract: the spacetime field must exist before
 * any bodies, observers, or engines are admitted. `step_once()` defines the canonical execution
 * order: spawn flush → engine pre-hooks → geodesic integration → time sync → observer update →
 * engine post-hooks → clock advance → render.
 */

#include <cstdint>
#include <memory>
#include <vector>

#include "console/console_ui.h"
#include "engine/body.h"
#include "engine/engine_registry.h"
#include "engine/field.h"
#include "system/observer_manager.h"
#include "system/scheduler.h"
#include "system/spawn_queue.h"
#include "system/time_system.h"

/**
 * @brief Non-owning view into the live simulation containers passed to engines, SpawnQueue,
 *        SafeLoader, and ConsoleUI each step.
 *
 * Using references rather than copies means all subsystems operate on the same objects without
 * paying for copies or requiring raw pointers to Simulation internals. Reference members must
 * be initialized at construction and cannot be rebound — build one on the stack at the top of
 * `step_once()` and pass it down.
 */
struct SimulationState {
  std::vector<std::unique_ptr<Body>>& bodies;  // Live body list; mutated only by SpawnQueue.
  ObserverManager& observers;                  // Observer registry and cached measurements.
  uint64_t& next_id;       // Monotonic ID counter shared across body/observer creation.
  const TimeSystem& time;  // All four time channels for the current step.
};

class Simulation {
 public:
  explicit Simulation(std::unique_ptr<MetricProvider> metric);

  /** @brief Sets the default accuracy hint used for newly spawned bodies. */
  void set_default_accuracy(AccuracyProfile acc);

  /** @brief Creates a body directly (bypassing SpawnQueue) and returns a non-owning pointer. */
  Body* add_body(double mass, Vec4 pos, Vec4 vel,
                 AccuracyProfile acc = AccuracyProfile::Balanced());

  /**
   * @brief Creates an observer directly (bypassing SpawnQueue) and registers it.
   * @param M Black hole mass in geometric units; forwarded to Observer for lapse computation.
   */
  void add_observer(Vec4 pos, Vec4 vel, double M);

  /** @brief Loads an engine plugin and registers it for pre/post-step hooks. */
  void load_engine(std::unique_ptr<EngineInterface> engine);

  /** @brief Runs fixed-step integration until exit is requested or λ reaches `total_lambda`. */
  void run(double total_lambda, double base_step);

  /** @brief Advances the simulation by one affine-parameter step `dl`. */
  void step_once(double dl);

  bool should_exit() const { return exit_; }
  void request_exit() { exit_ = true; }

  /** @brief Logs λ and body count to the info channel. */
  void print_state() const;

  const TimeSystem& time() const { return time_; }
  const ObserverManager& observers() const { return observers_; }
  const SpacetimeField& field() const { return *field_; }
  const EngineRegistry& engines() const { return engines_; }

 private:
  std::unique_ptr<SpacetimeField> field_;
  std::vector<std::unique_ptr<Body>> bodies_;
  TimeSystem time_;
  ObserverManager observers_;
  SpawnQueue spawn_queue_;
  Scheduler scheduler_;
  EngineRegistry engines_;
  AccuracyProfile default_acc_;
  bool exit_ = false;
  uint64_t next_id_ = 1;
  ConsoleUI ui_;
};
