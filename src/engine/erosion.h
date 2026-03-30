#pragma once

/**
 * @brief Stub erosion engine reserved for future material-loss models.
 */

#include "engine_interface.h"

class SimulationState;

/**
 * @brief Placeholder erosion plugin.
 */
class erosion_engine final : public EngineInterface {
 public:
  std::string name() const override { return "erosion"; }
  std::string version() const override { return "0.0.0-stub"; }
  void init(SimulationState&) {}
  void pre_step(SimulationState&, double) {}
  void post_step(SimulationState&, double) {}
  void shutdown() {}
  std::vector<std::string> reads() const override { return {}; }
  std::vector<std::string> writes() const override { return {}; }
};
