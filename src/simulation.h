#pragma once

/**
 * @brief Top-level orchestrator for field, bodies, observers, engines, and timing.
 * Construction follows the field-first startup contract: the spacetime field exists before any
 * bodies, observers, or engines are admitted to the simulation state.
 */

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/body.h"
#include "engine/engine_registry.h"
#include "engine/field.h"
#include "system/observer_manager.h"
#include "system/scheduler.h"
#include "system/spawn_queue.h"
#include "system/time_system.h"

struct SimulationState {
  std::vector<std::unique_ptr<Body>>& bodies;
  ObserverManager& observers;
  uint64_t& next_id;
};

class Simulation {
 public:
  explicit Simulation(std::unique_ptr<MetricProvider> metric);

  /**
   * @brief Sets the default accuracy hint used for newly created bodies.
   */
  void set_default_accuracy(AccuracyProfile acc);

  /**
   * @brief Creates a body and hands ownership to the simulation.
   */
  Body* add_body(double mass, Vec4 pos, Vec4 vel,
                 AccuracyProfile acc = AccuracyProfile::Balanced());
  void add_observer(Vec4 pos, Vec4 vel);
  void load_engine(std::unique_ptr<EngineInterface> engine);

  /**
   * @brief Runs fixed-step integration until exit is requested or λ reaches the target.
   */
  void run(double total_lambda, double base_step);
  void step_once(double dl);
  bool should_exit() const { return exit_; }
  void request_exit() { exit_ = true; }

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
};
