/**
 * @brief Single-cell AMR placeholder.
 * Real refinement logic is deferred, so every query and invalidation collapses to the root cell.
 */

#include "amr_grid.h"

AMRCell* AMRGrid::cell_at(const Vec4&) {
  return &root_;
}

void AMRGrid::refine(const Vec4&, int) {}
void AMRGrid::coarsen(const Vec4&) {}
void AMRGrid::mark_dirty_near(const Vec4&, double) {
  root_.dirty = true;
}

size_t AMRGrid::dirty_count() const {
  return root_.dirty ? 1 : 0;
}
