#pragma once

/**
 * @brief Terminal renderer driven by observer measurements.
 *
 * Consumes `Observer::Measurement` snapshots produced by `ObserverManager::update()` rather
 * than reading body worldlines directly. This keeps the rendering path identical to what a
 * future Vulkan camera will use — both receive the same derived measurement stream.
 *
 * Rendering is throttled by `redraw_every` so terminal I/O does not dominate integration time
 * during fast runs.
 */

struct SimulationState;

class ConsoleUI {
 public:
  /**
   * @brief Emits one display frame if the step counter is a multiple of `redraw_every`.
   * Prints a clock header followed by per-observer blocks showing each body's coordinate
   * distance, proper time, and gravitational time dilation.
   */
  void render(const SimulationState& state);

  /**
   * @brief Prints a one-line status message to stdout outside the main frame.
   * Intended for transient events (spawn confirmations, validation results) that do not
   * fit inside the fixed frame layout.
   */
  static void status(const char* fmt, ...);

  int redraw_every = 100;  // Render one frame every N steps; increase for faster runs.

 private:
  int step_count_ = 0;  // Incremented each call; compared against redraw_every to gate output.
};
