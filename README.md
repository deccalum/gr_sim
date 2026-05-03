# gr_sim
`gr_sim` is a general relativity simulation project built as a practical study of modern C++, numerical methods, and CPU/GPU performance. It treats gravity as spacetime curvature and models motion as geodesic evolution through that curved geometry.

The goal is to grow it into a physically grounded, performance-conscious sandbox for relativistic simulation: starting with analytic metrics and testable integrators, then expanding toward observers, adaptive refinement, heterogeneous compute, and eventually real-time visualization.

---
## Roadmap
### Phase 0 — Foundation **OK**
Units, coordinate convention (`G=c=1`, isotropic Cartesian `[t,x,y,z]`), `AccuracyProfile`,
core math types, logger, validator infrastructure.

### Phase 1 — Field + Geodesic Integration (active) **OK**
- [x] `SchwarzschildMetric`: analytic `g_μν`, `Γ^σ_μν` (isotropic Cartesian, 30 non-zero entries)
- [x] `SchwarzschildMetric::validate()` — known-answer checks at ρ=5M
- [x] `Body::step_rk4()` — replace Euler placeholder with RK4 + proper time accumulation
- [x] `SpawnQueue::process()` — wire actual body/observer creation
- [x] First end-to-end geodesic: circular orbit at r=6M, verify E and L conservation

### Phase 2 — Main Loop + Time System
- [x] `TimeSystem` — wall / affine / coordinate / proper time channels
- [x] `ConsoleUI` — observer-driven frame render (time dilation visible per body)
- [x] `SafeLoader` — resource pre-flight before spawn (stub → real thresholds)

### Phase 3 — Observer / POV System
- [ ] `Observer::measure()` — relativistic measurements (r, τ, dilation, visibility)
- [ ] Multiple simultaneous observers with different `AccuracyProfile`s
- [ ] Time-frozen appearance near r→2M readable from CUI output

### Phase 4 — Heterogeneous Compute
- [ ] `BudgetAllocator` — roofline-derived iteration caps (replace Balanced() stub)
- [ ] `MasterRoofline::evaluate()` — real contention model (CPU + GPU + PCIe)
- [ ] `Scheduler` — CPU thread pool + GPU dispatch (Vulkan compute or CUDA)

### Phase 5 — AMR + Second Body
- [ ] `AMRGrid` — real octree refinement driven by body proximity and curvature
- [ ] Two-body system — alternating metric ownership (explicit approximation, labeled)
- [ ] `gr_bench` cross-check: replace stubs with real `SchwarzschildMetric` calls

### Phase 6 — Post-Newtonian + Engine Interface
- [ ] PN metric to order 2 via `AccuracyProfile.pn_order`
- [ ] `gw_emission` engine stub → Peters formula energy loss
- [ ] `fluid` engine stub → tidal tensor from GR metric

### Phase 7+ — Vulkan Visualization
- [ ] Camera as Observer with Vulkan viewport
- [ ] Null geodesic ray tracing for gravitational lensing
- [ ] Embedding diagram render (2D slice of curved spacetime)