/**
 * @brief Basic time-channel bookkeeping.
 */

#include "time_system.h"

#include <ctime>

void TimeSystem::advance(double dl) {
  t_.affine_lambda += dl;
  ++t_.step_count;
}

void TimeSystem::tick_wall() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  t_.wall_time_s = ts.tv_sec + ts.tv_nsec * 1e-9 - wall_start_s_;
}

void TimeSystem::set_proper_time(int id, double tau) {
  if (id >= static_cast<int>(proper_times_.size()))
    proper_times_.resize(static_cast<size_t>(id + 1), 0.0);
  proper_times_[static_cast<size_t>(id)] = tau;
}

double TimeSystem::proper_time(int id) const {
  if (id < static_cast<int>(proper_times_.size())) return proper_times_[static_cast<size_t>(id)];
  return 0.0;
}
