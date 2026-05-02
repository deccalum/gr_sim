#pragma once

/**
 * @brief Represents a massive or massless particle evolving through the spacetime field.
 * The body stores a worldline history plus a property bag used by secondary engines, so plugins can
 * exchange scalar state without taking direct dependencies on internal members.
 */

#include <cstdint>
#include <string>
#include <unordered_map>

#include "../system/accurator.h"
#include "worldline.h"

class SpacetimeField;

class Body {
 public:
  Body(double mass, Vec4 pos, Vec4 vel, uint64_t id,
       AccuracyProfile acc = AccuracyProfile::Balanced());

  /**
   * @brief Advances the body by one affine-parameter step using the integrator selected by `acc_`.
   * @param dl Affine parameter increment Δλ.
   */
  void step(const SpacetimeField& field, double dl);

  const Vec4& position() const;
  const Vec4& velocity() const;
  const Worldline& worldline() const;
  AccuracyProfile& accuracy() { return acc_; }
  double mass() const { return mass_; }
  uint64_t id() const { return id_; }

  // Engine-owned scalars flow through this bag instead of direct member access.
  void set_property(const std::string& key, double val) { props_[key] = val; }
  double get_property(const std::string& key, double def = 0.0) const {
    auto it = props_.find(key);
    return it != props_.end() ? it->second : def;
  }

 private:
  double mass_;
  uint64_t id_;
  AccuracyProfile acc_;
  Worldline worldline_;
  std::unordered_map<std::string, double> props_;

  /** @brief RK4 geodesic integrator step; O(h⁴) global truncation error. */
  void step_rk4(const SpacetimeField&, double dl);
  /** @brief DOP853 8th-order geodesic integrator step; O(h⁸) global truncation error. */
  void step_rk8(const SpacetimeField&, double dl);
  /** @brief Reprojects u^μ so g_{μν}u^μu^ν = -1 stays within `norm_tolerance`. */
  void enforce_norm_at(const SpacetimeField&, WorldlinePoint&) const;
  /** @brief Reprojects the latest worldline point to enforce the norm constraint. */
  void enforce_norm(const SpacetimeField&);
};
