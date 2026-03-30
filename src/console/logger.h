#pragma once

/**
 * @brief Declares the shared logging entry point for the simulator.
 * Physics and system code should route diagnostics here instead of writing directly to stdio.
 */

#include <cstdarg>
#include <cstdio>

enum class LogLevel { Debug, Info, Warn, Error };

inline void log(LogLevel level, const char* subsystem, const char* fmt, ...) {
  const char* tag = level == LogLevel::Debug  ? "DEBUG"
                    : level == LogLevel::Info ? "INFO "
                    : level == LogLevel::Warn ? "WARN "
                                              : "ERROR";

  fprintf(stderr, "[%s][%s] ", tag, subsystem);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
}
