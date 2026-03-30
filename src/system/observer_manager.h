#pragma once

/**
 * @brief Owns the active observers and caches their latest measurements.
 */

#include <memory>
#include <vector>

#include "observer.h"

class SimulationState;

/**
 * @brief Registry and measurement cache for observers.
 */
class ObserverManager {
 public:
  Observer* add(Vec4 pos, Vec4 vel);
  void remove(uint64_t id);
  void update(const SimulationState& state);
  const Observer::Measurement& measurement_of(uint64_t id) const;

 private:
  std::vector<std::unique_ptr<Observer> > observers_;
  std::vector<Observer::Measurement> measurements_;
  uint64_t next_id_ = 1;
};
