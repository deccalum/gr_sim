#pragma once

/**
 * @brief Registry and measurement cache for all active observers.
 *
 * Owns the observer list and stores a parallel vector of Measurement snapshots refreshed each
 * step by `update()`. Index-based accessors (`at`, `measurements_at`) let the ConsoleUI and
 * future renderers iterate all observers without needing to know their IDs in advance.
 */

#include <memory>
#include <vector>

#include "observer.h"

struct SimulationState;

class ObserverManager {
 public:
  /**
   * @brief Creates an observer and adds it to the registry.
   * @param M Black hole mass in geometric units; forwarded to `Observer` for lapse computation.
   * @return Non-owning pointer to the new observer; valid until `remove()` is called.
   */
  Observer* add(Vec4 pos, Vec4 vel, double M);

  /** @brief Removes the observer with the given id and its cached measurement. */
  void remove(uint64_t id);

  /**
   * @brief Refreshes all cached measurements from the current simulation state.
   * @note Must be called after the body integration loop so measurements reflect current positions.
   */
  void update(const SimulationState& state);

  /**
   * @brief Returns the cached Measurement for observer `id`, or a zero-initialized dummy if
   * unknown.
   */
  const Observer::Measurement& measurement_of(uint64_t id) const;

  /** @brief Returns the number of registered observers. */
  size_t count() const { return observers_.size(); }

  /**
   * @brief Returns the observer at index `i`.
   * @note `observers_` and `measurements_` are parallel vectors — `at(i)` and `measurements_at(i)`
   *       always refer to the same observer.
   */
  const Observer& at(size_t i) const { return *observers_[i]; }

  /** @brief Returns the cached Measurement at index `i` (parallel to `at(i)`). */
  const Observer::Measurement& measurements_at(size_t i) const { return measurements_[i]; }

 private:
  std::vector<std::unique_ptr<Observer>> observers_;
  std::vector<Observer::Measurement>
      measurements_;  // parallel to observers_; rebuilt each step by update()
  uint64_t next_id_ = 1;
};
