#pragma once

// ---------------------------------------------------------------------------
// Platform-specific clock selection for the benchmark timer.
//
// Primary target: Linux (CLOCK_MONOTONIC_RAW — immune to NTP slew).
// macOS:          CLOCK_UPTIME_RAW  — closest equivalent; monotonic, not
//                 affected by adjtime()/ntp_adjtime(). Available since 10.12.
// ---------------------------------------------------------------------------

#if defined(__APPLE__)
#include <time.h>
#define BENCH_CLOCK_RAW CLOCK_UPTIME_RAW

#elif defined(__linux__)
#include <time.h>
#define BENCH_CLOCK_RAW CLOCK_MONOTONIC_RAW

#else
#error "Unsupported platform — add a BENCH_CLOCK_RAW definition for this OS"
#endif
