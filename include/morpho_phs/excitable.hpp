// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "honeycomb.hpp"
#ifndef M_PI  // MORPHO_M_PI_GUARD: MSVC/MinGW do not define M_PI without _USE_MATH_DEFINES
#define M_PI 3.14159265358979323846
#endif

namespace morpho_phs {

// Barkley excitable medium (D. Barkley, Physica D 49 (1991) 61-70) on the honeycomb site graph:
//   du/dt = u(1-u)(u-(v+b)/a)/eps + D * sum_{j~i}(u_j-u_i),   dv/dt = u - v.
// Explicit Euler, one call to tick() per control block.  Runs at control rate, never touches audio state:
// it only produces bounded fields u_i in [0,1] that a ParamMap turns into admissible parameters.
template <RealType R>
class Barkley {
public:
    R a = R(0.75), b = R(0.03), eps = R(0.02), D = R(0.5), dt = R(0.016);
    int substeps = 1;
    R dt_max = R(0.016);   // largest Euler step kept (Phase-1 D2 sweep: u stays in [0,1] for dt <= 0.016)

    // tau knob: one model time unit lasts tau_s seconds.  Model time per tick = 1/(tick_hz*tau_s) is split into
    // substeps of dt <= dt_max.  Default (tau = 1/12 s at 750 Hz ticks) reproduces dt = 0.016, 1 substep.
    void set_tau(double tau_s, double tick_hz) noexcept {
        const double per_tick = 1.0 / (tick_hz * std::max(tau_s, 1e-4));
        substeps = std::max(1, int(std::ceil(per_tick / double(dt_max) - 1e-9)));
        dt = R(per_tick / substeps);
    }
    double tau_seconds(double tick_hz) const noexcept { return 1.0 / (tick_hz * double(dt) * substeps); }

    void prepare(const Honeycomb& lat) {
        lat_ = &lat; u_.assign(lat.N, R(0)); v_.assign(lat.N, R(0)); un_.assign(lat.N, R(0)); stim_.assign(lat.N, R(0));
    }
    // Add a stimulus (applied at the next tick):  u_i <- min(1, u_i + amp) at site and its neighbours.
    void stimulate(int site, R amp) noexcept {
        stim_[site] += amp;
        for (int k = lat_->nbr_ptr[site]; k < lat_->nbr_ptr[site + 1]; ++k) stim_[lat_->nbr_idx[k]] += amp;
    }
    void tick() noexcept {
        const int N = lat_->N;
        for (int i = 0; i < N; ++i) { u_[i] = std::min(R(1), u_[i] + stim_[i]); stim_[i] = 0; }
        for (int s = 0; s < substeps; ++s) {
            for (int i = 0; i < N; ++i) {
                R lap = 0; const R ui = u_[i];
                for (int k = lat_->nbr_ptr[i]; k < lat_->nbr_ptr[i + 1]; ++k) lap += u_[lat_->nbr_idx[k]] - ui;
                const R ut = (v_[i] + b) / a;
                un_[i] = ui + dt * (ui * (R(1) - ui) * (ui - ut) / eps + D * lap);
                v_[i] += dt * (ui - v_[i]);
            }
            u_.swap(un_);
        }
    }
    // Clamp-free read; values stay in a bounded band (measured: Python D2 sweep) — maps clamp anyway.
    std::span<const R> u() const noexcept { return u_; }
    std::span<const R> v() const noexcept { return v_; }
    std::span<R> u_mut() noexcept { return u_; }
    std::span<R> v_mut() noexcept { return v_; }
    // Net topological charge: sum over hexagonal plaquettes of the winding of atan2(v-vc, u-uc).
    int abs_charge(R uc = R(0.45), R vc = R(0.25)) const noexcept {
        int tot = 0;
        for (const auto& hx : lat_->hexes) {
            double w = 0;
            for (int k = 0; k < 6; ++k) {
                const int i = hx[k], j = hx[(k + 1) % 6];
                double d = std::atan2(double(v_[j] - vc), double(u_[j] - uc)) - std::atan2(double(v_[i] - vc), double(u_[i] - uc));
                while (d > M_PI) d -= 2 * M_PI;
                while (d < -M_PI) d += 2 * M_PI;
                w += d;
            }
            tot += std::abs(static_cast<int>(std::lround(w / (2 * M_PI))));
        }
        return tot;
    }

private:
    const Honeycomb* lat_ = nullptr;
    std::vector<R> u_, v_, un_, stim_;
};

}  // namespace morpho_phs
