/**
 * @brief Minimal engine registry implementation.
 * The registry currently stores engines by name and calls pre/post-step hooks serially.
 */

#include "engine_registry.h"

#include <algorithm>

void EngineRegistry::load(std::unique_ptr<EngineInterface> e) {
  names_.push_back(e->name());
  engines_.push_back(std::move(e));
}
void EngineRegistry::unload(const std::string& name) {
  engines_.erase(std::remove_if(engines_.begin(), engines_.end(),
                                [&](const auto& e) { return e->name() == name; }),
                 engines_.end());
  names_.erase(std::remove(names_.begin(), names_.end(), name), names_.end());
}
void EngineRegistry::pre_step(SimulationState& s, double dl) {
  for (auto& e : engines_) e->pre_step(s, dl);
}
void EngineRegistry::post_step(SimulationState& s, double dl) {
  for (auto& e : engines_) e->post_step(s, dl);
}
const std::vector<std::string>& EngineRegistry::loaded_names() const {
  return names_;
}
bool EngineRegistry::is_loaded(const std::string& n) const {
  return std::find(names_.begin(), names_.end(), n) != names_.end();
}
