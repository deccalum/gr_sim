/**
 * @brief Observer measurement pipeline and ObserverManager registry.
 *
 * `Observer::measure()` is the core of the Phase 2 relativistic display: it evaluates lapse
 * factors at the observer and each body position and fills a Measurement snapshot that the
 * ConsoleUI (and future Vulkan camera) consume without touching body worldlines directly.
 *
 * `ObserverManager` owns the observer list and caches the most recent Measurement for each
 * observer. `update()` is called once per step (after integration) to refresh the cache.
 */

#include "observer.h"

#include <cmath>

#include "observer_manager.h"
#include "simulation.h"

/**
 * @brief Stores the observer's position, velocity, id, and black hole mass.
 * The mass M is not available through SimulationState, so it is captured at construction and
 * used in every subsequent call to `measure()`.
 */
Observer::Observer(Vec4 pos, Vec4 vel, uint64_t id, double M)
    : pos_(pos), vel_(vel), id_(id), M_(M) {}

/**
 * @brief Fills a Measurement snapshot from this observer's frame.
 *
 * The lapse factor A(ρ) = (1 − M/2ρ)/(1 + M/2ρ) is evaluated at both the observer position and
 * each body position. The ratio A_body/A_obs is the gravitational time dilation: as a body falls
 * toward the horizon (ρ → M/2), A_body → 0 and the dilation ratio → 0, meaning the body's clock
 * appears to freeze from the observer's perspective.
 *
 * @note This is the static (gravitational-only) dilation. A moving body has an additional kinetic
 *       contribution from its orbital velocity that is not yet accounted for.
 * @note `r_of_body[i]` is the Euclidean coordinate distance, not the proper spatial distance
 *       (which requires integrating the spatial metric). Coordinate distance is sufficient for
 *       Phase 2 display; proper distance is a Phase 3 extension.
 */
Observer::Measurement Observer::measure(const SimulationState& state) const {
  Measurement m{};

  // Lapse factor A(ρ) = (1 - α)/(1 + α), α = M/(2ρ).
  // Evaluating this at two positions and taking the ratio gives dτ_body/dτ_obs.
  auto lapse = [](double rho, double M) {
    double alpha = M / (2.0 * rho);
    return (1.0 - alpha) / (1.0 + alpha);
  };

  double rho_obs = std::sqrt(pos_[1] * pos_[1] + pos_[2] * pos_[2] + pos_[3] * pos_[3]);
  double A_obs = lapse(rho_obs, M_);

  // For a static observer dτ = A(ρ_obs) dt_coord, so proper time = A_obs * coordinate_t.
  m.proper_time = A_obs * state.time.now().coordinate_t;

  for (size_t i = 0; i < state.bodies.size() && i < static_cast<size_t>(MAX_BODIES); ++i) {
    const Vec4& bp = state.bodies[i]->position();

    double dx = bp[1] - pos_[1];
    double dy = bp[2] - pos_[2];
    double dz = bp[3] - pos_[3];
    m.r_of_body[i] = std::sqrt(dx * dx + dy * dy + dz * dz);
    m.tau_of_body[i] = state.bodies[i]->worldline().latest().tau;

    double rho_b = std::sqrt(bp[1] * bp[1] + bp[2] * bp[2] + bp[3] * bp[3]);
    m.dilation[i] = lapse(rho_b, M_) / A_obs;

    m.visible[i] = true;  // Phase 3: replace with ray-march or light-travel-time test.
  }

  return m;
}

/**
 * @brief Creates an observer and transfers ownership to the manager.
 * @param M Black hole mass forwarded to `Observer` for lapse computation in `measure()`.
 * @return Non-owning pointer to the newly created observer; valid until `remove()` is called.
 */
Observer* ObserverManager::add(Vec4 pos, Vec4 vel, double M) {
  auto o = std::make_unique<Observer>(pos, vel, next_id_++, M);
  Observer* ptr = o.get();
  observers_.push_back(std::move(o));
  return ptr;
}

/**
 * @brief Removes the observer with the given id and erases its cached measurement.
 * Uses a linear scan — observer counts are small enough that this is never a bottleneck.
 */
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

/**
 * @brief Rebuilds the measurement cache by calling `measure()` on every observer.
 * Must be called after the body integration loop so measurements reflect current positions.
 * Called once per step by `Simulation::step_once()` before `ConsoleUI::render()`.
 */
void ObserverManager::update(const SimulationState& state) {
  measurements_.clear();
  measurements_.reserve(observers_.size());
  for (const auto& obs : observers_) {
    measurements_.push_back(obs->measure(state));
  }
}

/**
 * @brief Returns the cached Measurement for the observer with the given id.
 * @return A reference to the cached snapshot, or a zero-initialized dummy if the id is unknown.
 *         The dummy is intentionally zero so callers can read it without crashing; they should
 *         check that the id is valid before interpreting the values.
 */
const Observer::Measurement& ObserverManager::measurement_of(uint64_t id) const {
  for (size_t i = 0; i < observers_.size(); ++i) {
    if (observers_[i]->id() == id && i < measurements_.size()) {
      return measurements_[i];
    }
  }
  static const Observer::Measurement dummy{};
  return dummy;
}
