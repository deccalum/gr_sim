#pragma once

/**
 * @brief Owns loaded engines and dispatches their step hooks.
 */

#include <memory>
#include <vector>

#include "engine_interface.h"

struct SimulationState;

/**
 * @brief Runtime registry for active secondary engines.
 */
class EngineRegistry {
 public:
  void load(std::unique_ptr<EngineInterface> engine);
  void unload(const std::string& name);
  void pre_step(SimulationState& state, double dl);
  void post_step(SimulationState& state, double dl);
  const std::vector<std::string>& loaded_names() const;
  bool is_loaded(const std::string& name) const;

 private:
  std::vector<std::unique_ptr<EngineInterface>> engines_;
  std::vector<std::string> names_;
};
