/**
 * @brief Observer-driven terminal renderer.
 *
 * Each frame clears the screen and prints a header row of simulation-wide clocks followed by a
 * per-observer block: the observer's own position and a table row for every tracked body showing
 * coordinate distance, accumulated proper time, and gravitational time dilation.
 *
 * Rendering is gated by `redraw_every` — the frame is skipped unless `step_count_` is a multiple
 * of that value, keeping terminal I/O from dominating runtime during fast runs.
 */

#include "console_ui.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "simulation.h"
#include "system/observer.h"

/**
 * @brief Emits one display frame from the current observer measurements.
 *
 * Layout:
 *   Line 1  — wall time, coordinate time, affine λ, step count
 *   Per observer — isotropic ρ of the observer, then one row per body:
 *                  body id, coordinate distance to body, body proper time, dilation factor
 *
 * `dilation < 1.0` means the body's clock runs slower than the observer's — the value approaches
 * zero as the body approaches the horizon at ρ = M/2.
 */
void ConsoleUI::render(const SimulationState& state) {
  if (step_count_++ % redraw_every != 0) return;

  const SimulationTime& t = state.time.now();

  fprintf(stdout, "\033[2J\033[H");
  fprintf(stdout, "wall %6.2f s   coord_t %8.2f   lambda %8.2f   step %ld\n", t.wall_time_s,
          t.coordinate_t, t.affine_lambda, t.step_count);
  fprintf(stdout, "--------------------------------------------------------------------\n");

  if (state.observers.count() == 0) {
    fprintf(stdout, "(no observers registered)\n");
  }

  for (size_t i = 0; i < state.observers.count(); ++i) {
    const Observer& obs = state.observers.at(i);
    const Observer::Measurement& m = state.observers.measurements_at(i);

    const Vec4& op = obs.position();
    double rho_obs = std::sqrt(op[1] * op[1] + op[2] * op[2] + op[3] * op[3]);
    fprintf(stdout, "OBS %zu @ rho=%.4f M\n", i + 1, rho_obs);

    for (size_t j = 0; j < state.bodies.size() && j < static_cast<size_t>(MAX_BODIES); ++j) {
      const char* vis = m.visible[j] ? "[visible]" : "[hidden] ";
      fprintf(stdout, "  body %-2zu  rho=%7.4f M  tau=%8.4f  dil=%6.4f  %s\n", j + 1,
              m.r_of_body[j], m.tau_of_body[j], m.dilation[j], vis);
    }
  }

  fprintf(stdout, "--------------------------------------------------------------------\n");
}

/**
 * @brief Prints a one-line status message to stdout.
 * Intended for transient messages (spawn confirmations, warnings) that sit outside the main frame.
 */
void ConsoleUI::status(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stdout, fmt, args);
  va_end(args);
  fputc('\n', stdout);
}
