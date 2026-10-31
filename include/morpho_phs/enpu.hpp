// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "esav_lf.hpp"
#include "excitable.hpp"

namespace morpho_phs {

// Bounded medium -> acoustic parameter map (docs/theory_v1.md, Prop. 6).  Every output lies in the
// admissible set A = {m in [m_lo,m_hi], alpha,beta >= 0, mu <= mu_max, eta >= eta_min, g in R}:
// therefore whatever the medium does, the acoustic layer stays passive (mu<=0) or energy-bounded (Thm. 4).
template <RealType R>
struct ParamMap {
    SiteParams<R> base;       // base parameters (admissible)
    R g_depth = R(0);         // g_i = g_base_i * (1 - 2*g_depth*u_i)   (g_depth=1: Chern sign flip where u=1)
    R mu_depth = R(0);        // mu_i = mu_base_i + mu_depth*u_i  (clamped to mu_max)
    R mu_max = R(0);
    R alpha_depth = R(0);     // alpha_i = alpha_base_i*(1 + alpha_depth*u_i)  (alpha_depth >= 0)
    void apply(std::span<const R> u, SiteParams<R>& out) const noexcept {
        out.assign_from(base);
        for (std::size_t i = 0; i < u.size(); ++i) {
            const R ui = std::clamp(u[i], R(0), R(1));
            out.g[i] = base.g[i] * (R(1) - R(2) * g_depth * ui);
            out.mu[i] = std::min(base.mu[i] + mu_depth * ui, mu_max);
            out.alpha[i] = base.alpha[i] * (R(1) + alpha_depth * ui);
        }
    }
};

// Control-rate scheduler: every `block` samples (i) ticks the medium, (ii) maps it to parameters,
// (iii) applies them to the integrator through ENPU (energy-non-increasing).  No allocation after prepare().
template <RealType R, class Engine = EsavLF<R>>
class ControlLayer {
public:
    int block = 64;
    int enpu_mode = 0;         // ENPU variant: 0 = A reproject (default), 1 = B keep-state, 2 = C selective (EsavLFMT only)
    // User knob (Phase 2 decision): medium time constant tau in seconds per Barkley model unit.
    void set_tau(double tau_s, double fs = 48000.0) noexcept { medium.set_tau(tau_s, fs / block); }
    void prepare(const Honeycomb& lat, const ParamMap<R>& map) {
        map_ = map; medium.prepare(lat); next_.resize(lat.N); next_.assign_from(map.base);
    }
    // returns ENPU scale s (1 = no energy clipping needed)
    R control_tick(Engine& eng) noexcept {
        medium.tick();
        map_.apply(medium.u(), next_);
        if constexpr (requires { eng.enpu_selective(next_); }) {
            if (enpu_mode == 1) return eng.enpu_keep_psi(next_);
            if (enpu_mode == 2) return eng.enpu_selective(next_);
        }
        return eng.enpu(next_);
    }
    Barkley<R> medium;
    ParamMap<R>& map() noexcept { return map_; }

private:
    ParamMap<R> map_;
    SiteParams<R> next_;
};

}  // namespace morpho_phs
