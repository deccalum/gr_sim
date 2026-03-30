#pragma once

/**
 * @brief Stub atmosphere engine reserved for later drag or accretion models.
 */

#include "engine_interface.h"

class SimulationState;

/**
 * @brief Placeholder atmosphere plugin.
 */
class atmosphere_engine final : public EngineInterface {
 public:
  std::string name() const override { return "atmosphere"; }
  std::string version() const override { return "0.0.0-stub"; }
  void init(SimulationState&) {}
  void pre_step(SimulationState&, double) {}
  void post_step(SimulationState&, double) {}
  void shutdown() {}
  std::vector<std::string> reads() const override { return {}; }
  std::vector<std::string> writes() const override { return {}; }
};
