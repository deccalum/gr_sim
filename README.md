# gr_sim
c++ and general relativity-learning process in one go. To understand and master c++ programming, GPU/CPU optimization and challenging it with mathematical physics.

In `gr_sim` gravity is spacetime curvature. Motion is geodesic.

---

```bash
# 1. Build
cmake -B build && cmake --build build

# 2. Characterize hardware (run once per machine)
./build/gr_bench --output cache/hardware_profile.bin

# 3. Run simulator
./build/gr_sim
```

## DOCS
See `docs/ARCHITECTURE.md` for the field-first design and phase build order.
See `docs/ROOFLINE.md` for Schwarzschild Γ and RK4 FLOP/byte derivations.
