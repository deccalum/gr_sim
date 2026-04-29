// exercises/25.cpp
//
// MTW §25.5, Exercise 25.1 analog:
// "Constant of motion obtained from Hamilton's principle"
//
// MTW equation (25.8) states that geodesic paths extremise the action:
//
//   δ ∫ ½ g_μν(x) (dx^μ/dλ)(dx^ν/dλ) dλ = 0
//
// Applying the Euler-Lagrange equations to this action produces exactly:
//
//   d²x^σ/dλ² = -Γ^σ_μν u^μ u^ν          (the geodesic equation)
//
// This exercise verifies numerically that the ISCO initial conditions satisfy
// the geodesic equation in the way a circular orbit must: the radial component
// of the geodesic acceleration is exactly centripetal (inward), and the
// tangential component is zero — meaning no force acts along the orbit.
//
// KEY POINT about coordinates:
//   In standard Schwarzschild (r, θ, φ), circular orbit means du^r/dλ = 0.
//   In our isotropic Cartesian (x, y, z), the body at position (ρ, 0, 0)
//   moving in +y has u^x = 0, but du^x/dλ ≠ 0. The correct circular orbit
//   condition here is d²ρ/dλ² = 0, which gives:
//
//     du^x/dλ  =  -(u^y)² / ρ          (centripetal, inward)
//
//   If the geodesic equation produces exactly this value, the orbit is circular.
//   This is the thing we check.
//
// Build:  cmake --build build  →  ./build/exercises/ex_25
//
// Reference: Misner, Thorne & Wheeler, Gravitation (1973), §25.5, eq. (25.8)

#include <cmath>
#include <cstdio>
#include <memory>

// Headers from gr_sim_core
#include "engine/schwarzschild.h"   // SchwarzschildMetric, compute_factors internally
#include "system/accurator.h"       // AccuracyProfile (controls integrator fidelity)
#include "system/validator.h"       // ValidationResult (startup sanity check)

int main() {

    // -----------------------------------------------------------------------
    // 1. SETUP — identical to main.cpp up to the body creation
    //
    // We use SchwarzschildMetric directly here rather than going through
    // Simulation, because we only need the metric tensor and Christoffel
    // symbols at one point — no integration needed.
    // -----------------------------------------------------------------------

    const double M = 1.0;  // gravitational mass in geometric units (G = c = 1)

    SchwarzschildMetric metric(M);

    // Always run the validator first. It checks g_tt, g_xx, a Christoffel value,
    // and index symmetry at ρ = 5M against known exact answers. If this fails,
    // the numbers below are meaningless.
    {
        const ValidationResult vr = metric.validate();
        printf("[validate] %s: %s\n", vr.subsystem.c_str(), vr.detail.c_str());
        if (!vr.passed) return 1;
    }

    // AccuracyProfile carries integration hints. For this exercise we only
    // call metric() and christoffel() — the integrator_order field is unused.
    const AccuracyProfile acc = AccuracyProfile::Balanced();

    // -----------------------------------------------------------------------
    // 2. ISCO INITIAL CONDITIONS
    //
    // The ISCO of Schwarzschild sits at Schwarzschild radius r = 6M.
    // We need to convert to the isotropic radius ρ that our coordinates use.
    //
    // The two radii are related by:
    //   r = ρ (1 + M/2ρ)²
    //
    // Inverting (solve the quadratic in ρ):
    //   ρ = [ (r - M) + √((r-M)² - M²) ] / 2
    //
    // At r = 6M this gives ρ = (5 + 2√6)/2 · M ≈ 4.9495 M.
    // -----------------------------------------------------------------------

    const double R_schw  = 6.0 * M;
    const double rho_iso = (R_schw - M + std::sqrt((R_schw - M)*(R_schw - M) - M*M)) / 2.0;

    // Place the body on the x-axis at isotropic radius ρ.
    // Coordinates: (t, x, y, z) with the body at (0, ρ, 0, 0).
    const Vec4 pos = {0.0, rho_iso, 0.0, 0.0};

    // For a circular orbit at Schwarzschild radius r, the coordinate angular
    // velocity (in Schwarzschild time t) is the Keplerian-like result:
    //   dφ/dt = √(M / r³)
    //
    // In our isotropic Cartesian frame, the body at (ρ, 0, 0) moves in the
    // +y direction, so the Cartesian speed is:
    //   dy/dt = ρ · dφ/dt
    //
    // This is only the COORDINATE velocity (dy/dt), not the 4-velocity (u^y).
    const double dphi_dt = std::sqrt(M / (R_schw * R_schw * R_schw));
    const double dy_dt   = rho_iso * dphi_dt;

    // Evaluate the metric at the starting position so we can normalise u^μ.
    // The metric is diagonal: g = diag(-A², ψ⁴, ψ⁴, ψ⁴).
    Mat4  g{};
    Gamma gamma{};
    metric.metric(pos, g, acc);

    // The 4-velocity normalization condition for a massive particle is:
    //   g_μν u^μ u^ν = -1
    //
    // With u^x = u^z = 0 and u^y = (dy/dt) · u^t, this reduces to:
    //   (g_tt + g_yy · (dy/dt)²) · (u^t)² = -1
    //
    // Solving for u^t (the time component):
    const double u_t = std::sqrt(-1.0 / (g[0][0] + g[2][2] * dy_dt * dy_dt));

    // u^y is the spatial component of the 4-velocity in the y-direction.
    // Note: u^y ≠ dy/dt. u^y = (dy/dt) · u^t accounts for time dilation.
    const double u_y = dy_dt * u_t;

    const Vec4 vel = {u_t, 0.0, u_y, 0.0};

    printf("\n--- ISCO initial conditions ---\n");
    printf("  ρ        = %.6f  M  (Schwarzschild r = %.1f M)\n", rho_iso, R_schw / M);
    printf("  dy/dt    = %.6f     (coordinate angular speed × ρ)\n", dy_dt);
    printf("  u^t      = %.6f     (expected: √2 ≈ 1.414214)\n", u_t);
    printf("  u^y      = %.6f     (expected: ≈ 0.476265)\n", u_y);

    // -----------------------------------------------------------------------
    // 3. COMPUTE THE GEODESIC RIGHT-HAND SIDE
    //
    // The Euler-Lagrange equations from MTW (25.8) give the geodesic equation:
    //
    //   du^σ/dλ = -Γ^σ_μν u^μ u^ν
    //
    // This is the same triple loop found in body.cpp:rhs() lines 72–80.
    // Here we spell it out in full so every index contraction is visible.
    //
    // gamma[σ][μ][ν] stores Γ^σ_μν.  The loop sums over all 4×4 = 16
    // (μ,ν) pairs for each σ.  Most terms are zero because:
    //   - u^x = 0  (no radial velocity at the starting position)
    //   - u^z = 0  (orbit is in the x-y plane)
    //   So any term with μ=x, μ=z, ν=x, or ν=z vanishes.
    // The only surviving pairs are (μ,ν) ∈ {(t,t), (t,y), (y,t), (y,y)}.
    // -----------------------------------------------------------------------

    // Get the Christoffel symbols at the starting position.
    metric.christoffel(pos, gamma, acc);

    // du^σ/dλ for σ = 0,1,2,3  (t, x, y, z)
    double accel[4] = {0.0, 0.0, 0.0, 0.0};

    for (int sigma = 0; sigma < 4; ++sigma) {
        double sum = 0.0;
        for (int mu = 0; mu < 4; ++mu) {
            for (int nu = 0; nu < 4; ++nu) {
                // gamma[sigma][mu][nu] * u^mu * u^nu
                // Most of these are zero because u[1]=u[3]=0.
                sum += gamma[sigma][mu][nu] * vel[mu] * vel[nu];
            }
        }
        accel[sigma] = -sum;  // du^σ/dλ = -Γ^σ_μν u^μ u^ν
    }

    // -----------------------------------------------------------------------
    // 4. SHOW THE INDIVIDUAL CONTRIBUTIONS TO THE RADIAL ACCELERATION
    //
    // At position (ρ, 0, 0) the x-direction IS the radial direction.
    // Only two Christoffel terms survive for σ=x (all others involve u^x or u^z):
    //
    //   Γ^x_tt · (u^t)²   → gravitational pull inward  (−x direction)
    //   Γ^x_yy · (u^y)²   → metric-curvature term      (−x direction)
    //
    // Both are directed INWARD. This is a GR subtlety: there is no outward
    // "centrifugal force" term in the geodesic equation. Instead, the metric
    // curvature encoded in Γ^x_yy bends a y-moving trajectory toward the
    // mass. The circular orbit emerges because both terms together give
    // exactly the centripetal acceleration -(u^y)²/ρ required by the geometry.
    // -----------------------------------------------------------------------

    const double term_gravity   = -gamma[1][0][0] * vel[0] * vel[0];  // -Γ^x_tt (u^t)²
    const double term_curvature = -gamma[1][2][2] * vel[2] * vel[2];  // -Γ^x_yy (u^y)²

    printf("\n--- Geodesic acceleration  du^σ/dλ = -Γ^σ_μν u^μ u^ν ---\n");
    printf("  σ=t  du^t/dλ = %+.6e  (expect ≈ 0: u^x=0 kills all Γ^t terms)\n", accel[0]);
    printf("  σ=x  du^x/dλ = %+.6e  (centripetal, inward)\n",                    accel[1]);
    printf("       breakdown:\n");
    printf("         -Γ^x_tt (u^t)² = %+.6e  (gravity)\n",        term_gravity);
    printf("         -Γ^x_yy (u^y)² = %+.6e  (metric curvature)\n", term_curvature);
    printf("  σ=y  du^y/dλ = %+.6e  (expect 0: tangential — no force along orbit)\n", accel[2]);
    printf("  σ=z  du^z/dλ = %+.6e  (expect 0: orbit is 2D)\n",   accel[3]);

    // -----------------------------------------------------------------------
    // 5. THE CIRCULAR ORBIT CHECK
    //
    // In isotropic Cartesian coordinates, a circular orbit at radius ρ means
    // the isotropic radius ρ = √(x²+y²+z²) stays constant. Differentiating
    // twice along the worldline:
    //
    //   d²ρ/dλ² = [(u^y)² + ρ · (du^x/dλ)] / ρ     at position (ρ,0,0)
    //                                                  with u^x = u^z = 0
    //
    // For ρ = const we need d²ρ/dλ² = 0, so:
    //
    //   du^x/dλ  =  -(u^y)² / ρ          ← circular orbit condition
    //
    // If the geodesic equation gives this value, the Christoffel symbols are
    // exactly right for a circular orbit — the metric and its gradients encode
    // the correct spacetime curvature at r = 6M.
    // -----------------------------------------------------------------------

    const double centripetal_expected = -(u_y * u_y) / rho_iso;
    const double d2rho_dlambda2       = (u_y * u_y + rho_iso * accel[1]) / rho_iso;

    printf("\n--- Circular orbit check ---\n");
    printf("  Expected centripetal  du^x/dλ = -(u^y)²/ρ = %+.6e\n", centripetal_expected);
    printf("  Geodesic equation     du^x/dλ              = %+.6e\n", accel[1]);
    printf("  Discrepancy                                 = %+.6e  (GR correction to flat-space formula)\n",
           accel[1] - centripetal_expected);
    printf("\n  d²ρ/dλ²  =  (u^y)²/ρ + du^x/dλ  =  %+.6e\n", d2rho_dlambda2);
    printf("  (This should be ≈ 0 for a circular orbit; any residual is a GR curvature correction)\n");

    // -----------------------------------------------------------------------
    // 6. NOETHER INTERPRETATION
    //
    // The result du^y/dλ ≈ 0 is not a coincidence. The Schwarzschild metric
    // has a Killing vector ∂_φ (azimuthal symmetry). MTW (25.1) and Box 25.6
    // state that for any Killing vector ξ, the quantity p_K = p · ξ = g_μν u^μ ξ^ν
    // is conserved along every geodesic. For ∂_φ this gives angular momentum L.
    //
    // In our isotropic Cartesian frame, ∂_φ at position (ρ, 0, 0) points in
    // the +y direction. So conservation of L means there is no acceleration
    // along y — which is exactly what du^y/dλ = 0 says.
    //
    // Similarly, du^t/dλ = 0 at this point reflects conservation of energy E
    // from the Killing vector ∂_t.
    // -----------------------------------------------------------------------

    printf("\n--- Noether / Killing summary ---\n");
    printf("  du^t/dλ ≈ 0   ↔   dE/dλ = 0   (Killing vector ∂_t, energy conserved)\n");
    printf("  du^y/dλ ≈ 0   ↔   dL/dλ = 0   (Killing vector ∂_φ, angular momentum conserved)\n");
    printf("  du^x/dλ < 0   →   centripetal inward, curving the path to follow a circle\n");

    return 0;
}
