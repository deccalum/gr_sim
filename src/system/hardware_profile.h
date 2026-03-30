#pragma once

/**
 * @brief Declares the measured hardware roofline data consumed by scheduling code.
 * Profiles are generated externally by benchmarking tools and loaded at startup instead of being
 * hardcoded into the solver.
 */

#include <cstddef>
#include <string>

struct CacheSizes {
  size_t l1_bytes = 0;
  size_t l2_bytes = 0;
  size_t l3_bytes = 0;
};

/**
 * @brief Persistent benchmark summary for the active machine.
 */
struct HardwareProfile {
  double peak_flops_per_s = 0.0;
  double peak_bandwidth_bytes_s = 0.0;
  CacheSizes cache;
  int l1_cliff_bodies = 0;

  double ridge_point_flops_per_byte() const {
    return peak_bandwidth_bytes_s > 0.0 ? peak_flops_per_s / peak_bandwidth_bytes_s : 0.0;
  }
  bool valid() const { return peak_flops_per_s > 0.0 && peak_bandwidth_bytes_s > 0.0; }
  bool save(const std::string& path) const;
  bool load(const std::string& path);
  std::string summary() const;
};
