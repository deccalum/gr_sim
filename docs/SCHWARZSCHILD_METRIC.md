# SchwarzschildMetric
## 1\. Module Overview
**Component:** `SchwarzschildMetric`

**Files:** [`src/engine/schwarzschild.h`](src/engine/schwarzschild.h) | [`src/engine/schwarzschild.cpp`](src/engine/schwarzschild.cpp)

**Purpose:** Defines the spacetime geometry of a static, non-rotating, uncharged black hole.

**Role:** Acts as the arena. When a `Body` moves, the module calculates the local gravitational gradients (Christoffel symbols/metric tensors) telling the simulation exactly how to curve the object's path at that specific timestep.

## 2\. Mathematical Foundation
To avoid numerical singularities (division by zero) that happen at the event horizon in standard textbooks, `gr_sim` uses **Isotropic Coordinates**.

### **Isotropic Radius:** $\rho = \sqrt{x^2 + y^2 + z^2}$
Instead of standard definitions, the spatial stretching is defined by a conformal factor $\psi$, and time dilation by a factor $A$:

- **Mass-to-radius ratio:** $\alpha = \frac{M}{2\rho}$
- **Conformal factor:** $\psi = 1 + \alpha$

---
## 3\. Math-to-Code Mapping
> How the continuous differential geometry equations are translated into discrete C++ operations. Only the most heavily utilized functions and constants are documented below.

### The Scalar Engine: `compute_factors()`
Before calculating any tensors, the engine pre-computes the foundational scalars ($\rho, \alpha, \psi, A$) to avoid redundant divisions.

```cpp
IsoFactors f{};
f.rho = std::sqrt(f.rho2);      // ρ = sqrt(x² + y² + z²)
f.alpha = M * 0.5 / f.rho;      // α = M / (2ρ)
f.psi = 1.0 + f.alpha;          // ψ = 1 + α
f.A = (1.0 - f.alpha) / f.psi;  // A = (1 - α) / ψ
```

### The Metric Tensor: `metric()`
Using the scalars from `compute_factors()`, the diagonal metric tensor $g_{\mu\nu}$ is constructed directly from the line element $ds^2$.

- **$g_{tt}$ (Time component):** Represents the gravitational time dilation.
- **$g_{xx}, g_{yy}, g_{zz}$ (Spatial components):** Represents the spatial expansion/stretching.
<!-- end list -->

```cpp
g[idx::T][idx::T] = -f.A2;   // g_tt = -A²
g[idx::X][idx::X] = f.psi4;  // g_xx = ψ⁴
g[idx::Y][idx::Y] = f.psi4;  // g_yy = ψ⁴
g[idx::Z][idx::Z] = f.psi4;  // g_zz = ψ⁴
```

### Gravitational Gradients: `christoffel()`
Christoffel symbols ($\Gamma^\sigma_{\mu\nu}$) describe how coordinates change from point to point—this is the actual "force of gravity" felt by bodies in the simulation.

To optimize performance, `schwarzschild.cpp` uses pre-computed fractional factors (`f_t, f_r, f_s`) applied symmetrically.

#### Radial Acceleration ($\Gamma^i_{tt}$)
Dictates how a stationary object accelerates inward over time. Governed by the factor `f_r`.
$$f_r = \frac{2A\alpha}{\rho^2 \psi^6}$$

```cpp
// f_r is pre-computed in IsoFactors
gamma[idx::X][idx::T][idx::T] = f.f_r * x[idx::X];  // Γ^x_tt = f_r · x
gamma[idx::Y][idx::T][idx::T] = f.f_r * x[idx::Y];  // Γ^y_tt = f_r · y
gamma[idx::Z][idx::T][idx::T] = f.f_r * x[idx::Z];  // Γ^i_tt = f_r · x_i
```

#### Spatial Gradients ($\Gamma^i_{jk}$)
Dictates how moving objects curve through space (geodesic deviation). Governed by the factor `f_s`. Diagonal terms carry a minus sign; cross-index terms carry a plus sign.
$$f_s = \frac{2\alpha}{\rho^2 \psi}$$

```cpp
const double fs_x = f.f_s * x[idx::X];

// σ = x (index 1)
gamma[idx::X][idx::X][idx::X] = -fs_x;  // Diagonal terms are negative
gamma[idx::X][idx::Y][idx::Y] = +fs_x;  // Cross terms are positive
```

---
## 4\. Implementation & Performance Notes
### `struct IsoFactors`
**Why do we pre-compute these?** In C++, division (`/`) and powers (`std::pow`) are highly expensive operations for the CPU. Because the fractional terms are reused across $g_{\mu\nu}$, $g^{\mu\nu}$, and all $40$ non-zero entries of $\Gamma^\sigma_{\mu\nu}$, calculating them from scratch for every index would severely bottleneck the simulation.

`IsoFactors` computes all intermediate divisions exactly *once* per position update, storing them linearly in memory. This aligns the math execution with the strict `roofline` and FLOP constraints defined in `bench/expression_profile.cpp`.
- **Time dilation factor:** $A = \frac{1 - \alpha}{1 + \alpha}$

The full line element (the geometry) in these coordinates becomes:
$$ds^2 = -A^2 dt^2 + \psi^4 (dx^2 + dy^2 + dz^2)$$

---
## 5\. Phase 1 Validation — Circular Geodesic at the ISCO

The Phase 1 integration test places a massive test particle on a circular geodesic at the **Innermost Stable Circular Orbit (ISCO)** of the Schwarzschild spacetime at $r = 6M$. This orbit is one of the most precisely known exact solutions in GR and provides a stringent closed-form benchmark for every layer of the stack simultaneously: the coordinate transformation, the metric tensor, the Christoffel symbols, the RK4 integrator, and the norm-enforcement projection.

### 5.1 Coordinate Conversion: Schwarzschild $r$ → Isotropic $\rho$

The isotropic and Schwarzschild radial coordinates are related by $r = \rho\left(1 + \frac{M}{2\rho}\right)^2$. Inverting for a given $r$:

$$\rho = \frac{r - M + \sqrt{(r-M)^2 - M^2}}{2}$$

At $r = 6M$ (with $M = 1$, $G = c = 1$):

$$\rho_{\text{ISCO}} = \frac{5M + \sqrt{24M^2}}{2} = \frac{(5 + 2\sqrt{6})\,M}{2} \approx 4.949490\,M$$

**Simulation output:** `isotropic rho = 4.949490`. **OK**

---

### 5.2 Conserved Quantities: Analytical Values

For a massive test particle on a circular equatorial geodesic at Schwarzschild radius $r_0$, the two constants of motion are (see Misner, Thorne & Wheeler §25.5, or Jonsson 2013 §II):

$$E = \frac{1 - 2M/r_0}{\sqrt{1 - 3M/r_0}}, \qquad L = \frac{\sqrt{M r_0}}{\sqrt{1 - 3M/r_0}}$$

These follow from the Killing symmetries $\partial_t$ and $\partial_\phi$ of the metric; $E = -g_{tt}\,u^t$ and $L = g_{\phi\phi}\,u^\phi$.

At $r_0 = 6M$:

| Quantity             | Exact form             | Decimal                      |
|----------------------|------------------------|------------------------------|
| $E_{\text{ISCO}}$    | $\dfrac{2\sqrt{2}}{3}$ | $0.942\,809\,041\,582\ldots$ |
| $L_{\text{ISCO}}$    | $2\sqrt{3}\,M$         | $3.464\,101\,615\,138\ldots$ |
| $u^t = E/(1-2M/r_0)$ | $\sqrt{2}$             | $1.414\,213\,562\ldots$      |

**Simulation output:**
```
E0 = 0.942809041582   L0 = 3.464101615138   u^t = 1.414214
```
All three match to the printed precision. **OK**

The orbital angular velocity in Schwarzschild coordinates is $d\phi/dt = \sqrt{M/r_0^3}$. In isotropic Cartesian coordinates with the orbit at $(\rho, 0, 0)$ moving in the $y$-direction, the coordinate speed is $dy/dt = \rho_{\text{ISCO}} \cdot d\phi/dt$:

$$\frac{dy}{dt} = \frac{5 + 2\sqrt{6}}{2} \cdot \frac{1}{6\sqrt{6}} \approx 0.336770$$

**Simulation output:** `dy/dt = 0.336770`. **OK**

---
### 5.3 Integration Results (RK4, $\Delta\lambda = 0.01$, 10 000 steps)
> **Integrator note:** `AccuracyProfile::Max()` requests order-8 (DOP853), but `step_rk8` is currently a stub that falls back to `step_rk4`. The step size $\Delta\lambda = 0.01$ is set directly in `main.cpp`, overriding the profile's nominal $\Delta\lambda = 0.001$. Norm tolerance is $10^{-12}$ (Max profile). Total affine parameter: $\lambda \in [0,\,100]$.

| Observable                     | Initial value          | Error after 10 000 steps   | Relative              |
|--------------------------------|------------------------|----------------------------|-----------------------|
| $E$                            | $0.942\,809\,041\,582$ | $\sim 8.5 \times 10^{-15}$ | $9.1 \times 10^{-15}$ |
| $L$                            | $3.464\,101\,615\,138$ | $\sim 9.8 \times 10^{-15}$ | $2.8 \times 10^{-15}$ |
| $g_{\mu\nu}u^\mu u^\nu + 1$    | $0$                    | $< 2 \times 10^{-14}$      | —                     |
| $\rho - \rho_0$ (radial drift) | $0$                    | $< 2 \times 10^{-13}$      | $4 \times 10^{-14}$   |

**Why errors are near machine epsilon, not $O(\Delta\lambda^4)$:** The circular orbit is an *equilibrium* of the geodesic equations — an exact solution. RK4's $O(\Delta\lambda^4)$ truncation formula applies to the *deviation* from the true trajectory, not to the conserved-quantity error directly. Here the linearized equations around the circular orbit are neutrally stable, so the numerical trajectory oscillates around the exact orbit with amplitude $\sim O(\Delta\lambda^4)$ per step, but the conserved quantities accumulate only floating-point roundoff ($\varepsilon_\text{mach} \approx 2.2 \times 10^{-16}$) over $N = 10\,000$ steps, giving a floor of order $\sqrt{N}\,\varepsilon_\text{mach} \approx 2 \times 10^{-14}$.

**Oscillation pattern:** The $|dE|$ column rises from $\sim 3 \times 10^{-15}$ near step 1 to a peak of $\sim 1 \times 10^{-14}$ near step 4200, falls back, then rises again. The period of this oscillation is $\sim 6\,500$ steps, consistent with the proper orbital period:

$$T_\text{proper} = \frac{2\pi}{\sqrt{M/r_0^3}} \cdot \frac{1}{u^t} = \frac{2\pi \cdot 6\sqrt{6}}{\sqrt{2}} \approx 65.2\,M$$

This corresponds to $\approx 6\,520$ steps at $\Delta\lambda = 0.01$ — the integrator's phase error correlates with orbital phase and is **not** a secular drift. **OK?**

---
### 5.4 What Each Observable Validates
| Observation                                         | Component validated                                                               |
|-----------------------------------------------------|-----------------------------------------------------------------------------------|
| $E_0 = 2\sqrt{2}/3$ to 12 sig-figs                  | `metric()` → $g_{tt}$ and $g_{yy}$ correct                                        |
| $L_0 = 2\sqrt{3}$ to 12 sig-figs                    | `metric()` → $g_{yy}$ and coordinate conversion ρ correct                         |
| $\rho = \text{const}$ to $\sim 10^{-13}$            | `christoffel()` → $\Gamma^i_{tt}$, $\Gamma^i_{jk}$ correct; orbit does not spiral |
| $\|g_{\mu\nu}u^\mu u^\nu + 1\| < 2 \times 10^{-14}$ | `enforce_norm_at()` functioning (Phase 1 fix: inverted-comparison bug resolved)   |
| $u^t = \sqrt{2}$ exactly                            | normalization condition $g_{\mu\nu}u^\mu u^\nu = -1$ satisfied at $t = 0$         |
| No secular drift in $E$, $L$ over 10 000 steps      | RK4 integration stable; no hidden sign errors in the geodesic RHS                 |

---
## References
- [3+1 Formalism and Bases of Numerical Relativity](https://arxiv.org/abs/gr-qc/0703035) — Gourgoulhon (2007). Isotropic Schwarzschild coordinates in §6 (conformal decomposition) and §8 (initial data). The line element $ds^2 = -A^2 dt^2 + \psi^4 d\vec{x}^2$ appears as a standard conformal-flat Brill–Lindquist slice.
- [The Schwarzschild metric: It's the coordinates, stupid!](https://arxiv.org/abs/1308.0394) — Jonsson (2013). Section II derives the isotropic form of the metric; §III–IV derive conserved energy $E$ and angular momentum $L$ for circular geodesics in multiple coordinate systems.
- Misner, Thorne & Wheeler, *Gravitation* (1973), **§25.5** — Effective potential for Schwarzschild geodesics; derivation of $E_\text{circ}$ and $L_\text{circ}$ for arbitrary $r_0$; proof that $r_\text{ISCO} = 6M$ is the stability boundary.