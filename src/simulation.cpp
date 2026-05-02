/**
 * @brief Simulation orchestrator.
 * Owns the field, body list, observers, and all subsystems. `step_once()` defines the canonical
 * execution order: spawn flush → engine pre-hooks → geodesic integration → time sync → engine
 * post-hooks → clock advance.
 */

#include "simulation.h"

#include "console/logger.h"

Simulation::Simulation(std::unique_ptr<MetricProvider> metric)
    : field_(std::make_unique<SpacetimeField>(std::move(metric))) {
  log(LogLevel::Info, "Simulation", "field initialized");
}

void Simulation::set_default_accuracy(AccuracyProfile acc) { default_acc_ = acc; }

Body* Simulation::add_body(double mass, Vec4 pos, Vec4 vel, AccuracyProfile acc) {
  bodies_.push_back(std::make_unique<Body>(mass, pos, vel, next_id_++, acc));
  return bodies_.back().get();
}

void Simulation::add_observer(Vec4 pos, Vec4 vel) { observers_.add(pos, vel); }
void Simulation::load_engine(std::unique_ptr<EngineInterface> e) { engines_.load(std::move(e)); }

void Simulation::step_once(double dl) {
  SimulationState state{bodies_, observers_, next_id_};

  // Flush deferred body/observer creation before any step logic reads the body list.
  spawn_queue_.process(state);

  engines_.pre_step(state, dl);
  for (auto& b : bodies_) b->step(*field_, dl);

  // Capture u^t from the reference body immediately after integration, before post-step engine
  // hooks can modify body velocities. bodies_[0] is the reference; fall back to 1.0 if empty.
  const double u_t = bodies_.empty() ? 1.0 : bodies_[0]->velocity()[0];

  // Sync each body's accumulated proper time into the time system for external queries.
  for (auto& b : bodies_)
    time_.set_proper_time(static_cast<int>(b->id()), b->worldline().latest().tau);

  engines_.post_step(state, dl);
  time_.advance(dl, u_t);
  time_.tick_wall();
}

void Simulation::run(double total_lambda, double base_step) {
  while (!exit_ && time_.now().affine_lambda < total_lambda) step_once(base_step);
}

void Simulation::print_state() const {
  log(LogLevel::Info, "Simulation", "lambda=%.4f  bodies=%zu", time_.now().affine_lambda,
      bodies_.size());
}
