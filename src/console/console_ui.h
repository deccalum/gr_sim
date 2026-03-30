#pragma once

/**
 * @brief Terminal renderer driven by observer measurements.
 * The console UI is intended to consume the same derived measurement stream as future camera and
 * viewport outputs rather than inspecting raw simulation state directly.
 */

class SimulationState;

class ConsoleUI {
 public:
  void render(const SimulationState& state);
  static void status(const char* fmt, ...);
  int redraw_every = 1;

 private:
  int step_count_ = 0;
};
