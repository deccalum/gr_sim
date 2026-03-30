/**
 * @brief Stub observer implementation.
 * The measurement path will eventually evaluate relativistic observables, but currently returns a
 * zero-initialized frame so UI plumbing can compile and evolve independently.
 */

#include "observer.h"

Observer::Observer(Vec4 pos, Vec4 vel, uint64_t id) : pos_(pos), vel_(vel), id_(id) {}
Observer::Measurement Observer::measure(const SimulationState&) const {
  Measurement m{};
  return m;
}
