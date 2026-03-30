/**
 * @brief Stub simulation orchestrator.
 * The main loop already wires spawn processing, engine hooks, body stepping, and time updates in
 * the intended order even though SimulationState is still represented by placeholder casts.
 */

#include "simulation.h"

#include "console/logger.h"

Simulation::Simulation(std::unique_ptr<MetricProvider> metric)
    : field_(std::make_unique<SpacetimeField>(std::move(metric))) {
  log(LogLevel::Info, "Simulation", "field initialized");
}

void Simulation::set_default_accuracy(AccuracyProfile acc) {
  default_acc_ = acc;
}

Body* Simulation::add_body(double mass, Vec4 pos, Vec4 vel, AccuracyProfile acc) {
  bodies_.push_back(std::make_unique<Body>(mass, pos, vel, next_id_++, acc));
  return bodies_.back().get();
}

void Simulation::add_observer(Vec4 pos, Vec4 vel) {
  observers_.add(pos, vel);
}

void Simulation::load_engine(std::unique_ptr<EngineInterface> e) {
  engines_.load(std::move(e));
}

void Simulation::step_once(double dl) {
  // Temporary bridge until SimulationState becomes a real shared state object.
  spawn_queue_.process(*reinterpret_cast<SimulationState*>(this));  // STUB cast
  engines_.pre_step(*reinterpret_cast<SimulationState*>(this), dl);
  for (auto& b : bodies_) b->step(*field_, dl);
  engines_.post_step(*reinterpret_cast<SimulationState*>(this), dl);
  time_.advance(dl);
  time_.tick_wall();
}

void Simulation::run(double total_lambda, double base_step) {
  while (!exit_ && time_.now().affine_lambda < total_lambda) step_once(base_step);
}

void Simulation::print_state() const {
  log(LogLevel::Info, "Simulation", "lambda=%.4f  bodies=%zu", time_.now().affine_lambda,
      bodies_.size());
}
