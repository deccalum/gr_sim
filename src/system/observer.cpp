/**
 * @brief Stub observer implementation.
 * The measurement path will eventually evaluate relativistic observables, but currently returns a
 * zero-initialized frame so UI plumbing can compile and evolve independently.
 */

#include "observer.h"
#include "observer_manager.h"

Observer::Observer(Vec4 pos, Vec4 vel, uint64_t id) : pos_(pos), vel_(vel), id_(id) {}
Observer::Measurement Observer::measure(const SimulationState&) const {
  Measurement m{};
  return m;
}

Observer* ObserverManager::add(Vec4 pos, Vec4 vel) {
  auto o = std::make_unique<Observer>(pos, vel, next_id_++);
  Observer* ptr = o.get();
  observers_.push_back(std::move(o));
  return ptr;
}

void ObserverManager::remove(uint64_t id) {
  for (size_t i = 0; i < observers_.size(); ++i) {
    if (observers_[i]->id() == id) {
      observers_.erase(observers_.begin() + i);
      if (i < measurements_.size()) {
        measurements_.erase(measurements_.begin() + i);
      }
      break;
    }
  }
}

void ObserverManager::update(const SimulationState& state) {
  measurements_.clear();
  measurements_.reserve(observers_.size());
  for (const auto& obs : observers_) {
    measurements_.push_back(obs->measure(state));
  }
}

const Observer::Measurement& ObserverManager::measurement_of(uint64_t id) const {
  for (size_t i = 0; i < observers_.size(); ++i) {
    if (observers_[i]->id() == id && i < measurements_.size()) {
      return measurements_[i];
    }
  }
  static const Observer::Measurement dummy{};
  return dummy;
}
