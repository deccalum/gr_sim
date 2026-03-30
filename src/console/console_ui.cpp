/**
 * @brief Stub console renderer.
 * Until observers are connected, the renderer emits a step counter instead of a relativistic frame.
 */

#include "console_ui.h"

#include <cstdarg>
#include <cstdio>

void ConsoleUI::render(const SimulationState&) {
  if (step_count_++ % redraw_every != 0) return;
  fprintf(stdout, "[CUI] step %d\n", step_count_);
}

void ConsoleUI::status(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stdout, fmt, args);
  va_end(args);
  fputc('\n', stdout);
}
