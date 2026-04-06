#pragma once

/**
 * @brief Defines an observer frame that converts simulation state into display-ready measurements.
 * Display systems consume Observer::Measurement instead of raw body state so time dilation,
 * visibility, and future redshift/apparent-position effects stay centralized.
 */

#include <array>
#include <cstdint>

#include "../engine/metric.h"

static constexpr int MAX_BODIES = 64;

class SimulationState;

/**
 * @brief Observer state and derived measurements for one reference frame.
 */
class Observer {
 public:
  struct Measurement {
    double proper_time = 0.0;
    std::array<double, MAX_BODIES> r_of_body =
        {};  // Coordinate-space distance in the observer frame.
    std::array<double, MAX_BODIES> tau_of_body = {};  // Proper time reported for each body.
    std::array<double, MAX_BODIES> dilation = {};     // dτ_body / dτ_obs.
    std::array<bool, MAX_BODIES> visible = {};  // Placeholder line-of-sight / light-travel test.
  };

  Observer(Vec4 pos, Vec4 vel, uint64_t id);
  Measurement measure(const SimulationState&) const;
  Vec4& position() {
    return pos_;
  }
  Vec4& velocity() {
    return vel_;
  }
  uint64_t id() const {
    return id_;
  }

 private:
  Vec4 pos_, vel_;
  uint64_t id_;
};
