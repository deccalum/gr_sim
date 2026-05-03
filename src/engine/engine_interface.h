#pragma once

/**
 * @brief Defines the plugin contract for secondary physics engines.
 * Engines operate on SimulationState and communicate with bodies through property bags rather than
 * direct member access. Declared read/write keys allow scheduling and safety systems to detect
 * conflicts ahead of execution.
 */

#include <string>
#include <vector>

struct SimulationState;

/**
 * @brief Base interface implemented by every loadable physics engine.
 */
class EngineInterface {
 public:
  virtual ~EngineInterface() = default;
  virtual std::string name() const = 0;
  virtual std::string version() const = 0;
  virtual void init(SimulationState&) = 0;
  virtual void pre_step(SimulationState&, double dl) = 0;
  virtual void post_step(SimulationState&, double dl) = 0;
  virtual void shutdown() = 0;
  virtual std::vector<std::string> reads() const = 0;   // property keys read
  virtual std::vector<std::string> writes() const = 0;  // property keys written
};
