// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "honeycomb.hpp"
#include <algorithm>
#include <limits>

namespace morpho_phs {

// Nonlinear potential family used by the integrator.  All are convex with U(0)=0 (ENPU requirement).
enum class Potential { None, Quartic, QuarticLogcosh, QuarticLogcoshFast };   // Fast = Phase-3 sqrt-sum surrogate

// ESAV-LF integrator for the 2-DOF gyroscopic honeycomb port-Hamiltonian lattice
// (docs/theory_v1.md, Def. 3 / Thm. 1).  process_sample() advances one audio sample.
// Storage: structure-of-arrays (x and y components separate) so the per-site loop auto-vectorises.
// All storage is allocated in prepare(); process_sample()/enpu()/energy() never allocate.
template <RealType R>
class EsavLF {
public:
    struct Config {
        double fs = 48000.0, k0 = 0.0, kc = 1.0, c0 = 1.0;
        double E_cap = std::numeric_limits<double>::infinity();   // energy-gated gain (Thm. 4)
        Potential potential = Potential::QuarticLogcosh;
    };

    void prepare(const Honeycomb& lat, const Config& cfg, std::size_t max_ports) {
        lat_ = &lat; cfg_ = cfg; N_ = lat.N; Np_ = (N_ + 7) / 8 * 8; h_ = R(1.0 / cfg.fs);
        for (auto* v : {&qx_, &qy_, &qmx_, &qmy_, &px_, &py_, &pmx_, &pmy_, &fx_, &fy_, &Kx_, &Ky_, &vbx_, &vby_, &pd_, &pi_})
            v->assign(Np_, R(0));
        P_.resize(N_);
        ports_.assign(max_ports, Port<R>{}); nports_ = 0;
        bonds_.resize(lat.bonds.size());
        for (std::size_t b = 0; b < lat.bonds.size(); ++b)
            bonds_[b] = {lat.bonds[b][0], lat.bonds[b][1], R(cfg.kc * lat.nn[b][0]), R(cfg.kc * lat.nn[b][1]), R(cfg.kc * lat.nn[b][2])};
        reset_psi(); E_ = double(energy());
    }

    SiteParams<R>& params() noexcept { return P_; }
    const SiteParams<R>& params() const noexcept { return P_; }
    void set_ports(std::span<const Port<R>> p) noexcept { nports_ = std::min(p.size(), ports_.size()); std::copy_n(p.begin(), nports_, ports_.begin()); }
    std::size_t num_ports() const noexcept { return nports_; }

    // CFL margin gamma = h*omega_max_bound/2 (must be < 1); conservative bound lambda_max(K) <= k0 + 2 kc dmax.
    double cfl_gamma() const noexcept {
        R mmin = *std::min_element(P_.m.begin(), P_.m.end());
        return 0.5 * double(h_) * std::sqrt(lat_->lambda_max_bound(cfg_.k0, cfg_.kc) / double(mmin));
    }
    // Initial state from interleaved (x0,y0,x1,y1,...) q0, v0; first-order start q^{-1} = q^0 - h v^0.
    void set_state(std::span<const R> q0, std::span<const R> v0) noexcept {
        for (int i = 0; i < N_; ++i) {
            qx_[i] = q0[2 * i]; qy_[i] = q0[2 * i + 1];
            qmx_[i] = q0[2 * i] - h_ * v0[2 * i]; qmy_[i] = q0[2 * i + 1] - h_ * v0[2 * i + 1];
        }
        reset_psi(); E_ = double(energy());
    }
    void clear() noexcept {
        for (auto* v : {&qx_, &qy_, &qmx_, &qmy_}) std::fill(v->begin(), v->end(), R(0));
        reset_psi(); E_ = double(energy());
    }
    R q_at(int dof) const noexcept { return (dof & 1) ? qy_[dof >> 1] : qx_[dof >> 1]; }
    // lagged velocity (q^n - q^{n-1})/h of a DOF (read-only probe for control-layer feedback)
    R v_at(int dof) const noexcept { return ((dof & 1) ? (qy_[dof >> 1] - qmy_[dof >> 1]) : (qx_[dof >> 1] - qmx_[dof >> 1])) / h_; }

    // One audio sample.  u: port inputs (forces), y: port outputs (b_k^T vbar).  Returns h*sum_k u_k y_k.
    R process_sample(std::span<const R> u, std::span<R> y) noexcept {
        const R ih = R(1) / h_, ih2 = ih * ih, hih = R(0.5) * ih, c0 = R(cfg_.c0);
        const bool gate_open = E_ < cfg_.E_cap;
        for (std::size_t k = 0; k < nports_; ++k) { const auto& p = ports_[k]; fx_[p.site] += u[k] * p.bx; fy_[p.site] += u[k] * p.by; }
        apply_K(qx_.data(), qy_.data(), Kx_.data(), Ky_.data());
        R* __restrict qx = qx_.data(); R* __restrict qy = qy_.data(); R* __restrict qmx = qmx_.data(); R* __restrict qmy = qmy_.data();
        R* __restrict px = px_.data(); R* __restrict py = py_.data(); R* __restrict pmx = pmx_.data(); R* __restrict pmy = pmy_.data();
        const R* __restrict Kx = Kx_.data(); const R* __restrict Ky = Ky_.data(); const R* __restrict fx = fx_.data(); const R* __restrict fy = fy_.data();
        R* __restrict vbx = vbx_.data(); R* __restrict vby = vby_.data(); R* __restrict pd = pd_.data(); R* __restrict pin = pi_.data();
        const R* __restrict m = P_.m.data(); const R* __restrict al = P_.alpha.data(); const R* __restrict be = P_.beta.data();
        const R* __restrict mu = P_.mu.data(); const R* __restrict eta = P_.eta.data(); const R* __restrict g = P_.g.data();
        const R mucap = gate_open ? std::numeric_limits<R>::max() : R(0);
        const Potential pot = cfg_.potential;
        for (int i = 0; i < N_; ++i) {
            R gx = 0, gy = 0;
            if (pot == Potential::Quartic) {
                const R a = al[i], x2 = qx[i] * qx[i], y2 = qy[i] * qy[i];
                gx = a * x2 * qx[i] / std::sqrt(R(0.5) * a * x2 * x2 + c0);
                gy = a * y2 * qy[i] / std::sqrt(R(0.5) * a * y2 * y2 + c0);
            } else if (pot == Potential::QuarticLogcosh) {
                gx = sav_coeff_hq(qx[i], al[i], be[i], c0);
                gy = sav_coeff_hq(qy[i], al[i], be[i], c0);
            } else if (pot == Potential::QuarticLogcoshFast) {
                gx = sav_coeff_hqf(qx[i], al[i], be[i], c0);
                gy = sav_coeff_hqf(qy[i], al[i], be[i], c0);
            }
            const R dx0 = qx[i] - qmx[i], dy0 = qy[i] - qmy[i];
            const R vx = dx0 * ih, vy = dy0 * ih;
            const R d = -std::min(mu[i], mucap) + eta[i] * (vx * vx + vy * vy);
            const R base = m[i] * ih2 + d * hih;
            const R axx = base + R(0.5) * gx * gx, ayy = base + R(0.5) * gy * gy;
            const R rx = R(2) * m[i] * dx0 * ih2 - Kx[i] - gx * pmx[i] + fx[i];
            const R ry = R(2) * m[i] * dy0 * ih2 - Ky[i] - gy * pmy[i] + fy[i];
            const R c = g[i] * hih;
            const R idet = R(1) / (axx * ayy + c * c);
            const R wx = (ayy * rx + c * ry) * idet, wy = (axx * ry - c * rx) * idet;
            const R bx = wx * hih, by = wy * hih;
            vbx[i] = bx; vby[i] = by;
            pd[i] = d * (bx * bx + by * by);
            pin[i] = bx * fx[i] + by * fy[i];
            const R nqx = qmx[i] + wx, nqy = qmy[i] + wy, npx = pmx[i] + gx * wx, npy = pmy[i] + gy * wy;
            qmx[i] = qx[i]; qmy[i] = qy[i]; qx[i] = nqx; qy[i] = nqy;
            pmx[i] = px[i]; pmy[i] = py[i]; px[i] = npx; py[i] = npy;
        }
        double si = 0;
        for (std::size_t k = 0; k < nports_; ++k) {
            const auto& p = ports_[k];
            y[k] = p.bx * vbx[p.site] + p.by * vby[p.site];
            si += double(pin[p.site]);                  // input power is nonzero only at port sites
            fx_[p.site] = 0; fy_[p.site] = 0; pin[p.site] = 0;
        }
        const double sd = lane_sum(pd_.data());
        last_pdis_ = sd; last_pin_ = si;
        E_ += double(h_) * (si - sd);                   // exact discrete balance (Thm. 1), tracked incrementally
        return R(double(h_) * si);
    }

    // Discrete energy E^{n-1/2} of the pair (q^n, q^{n-1}).  O(N), no allocation.
    R energy() const noexcept {
        auto& Kmx = const_cast<std::vector<R>&>(Kx_); auto& Kmy = const_cast<std::vector<R>&>(Ky_);   // scratch
        apply_K(qmx_.data(), qmy_.data(), Kmx.data(), Kmy.data());
        double kin = 0, pot = 0, sav = 0; const double ih = 1.0 / double(h_);
        for (int i = 0; i < N_; ++i) {
            const double vx = (double(qx_[i]) - qmx_[i]) * ih, vy = (double(qy_[i]) - qmy_[i]) * ih;
            kin += 0.5 * double(P_.m[i]) * (vx * vx + vy * vy);
            pot += 0.5 * (double(qx_[i]) * Kmx[i] + double(qy_[i]) * Kmy[i]);
            sav += 0.25 * (double(px_[i]) * px_[i] + double(pmx_[i]) * pmx_[i] + double(py_[i]) * py_[i] + double(pmy_[i]) * pmy_[i]) - cfg_.c0;
        }
        return R(kin + pot + sav);
    }

    // ENPU (Thm. 3): switch to parameters Pn without increasing the discrete energy.  Returns s in [0,1].
    R enpu(const SiteParams<R>& Pn) noexcept {
        const double E_old = double(energy());
        const R hh = R(0.5) * h_;
        for (int i = 0; i < N_; ++i) {
            const R r = std::sqrt(P_.m[i] / Pn.m[i]);
            const R bx = R(0.5) * (qx_[i] + qmx_[i]), vx = (qx_[i] - qmx_[i]) / h_ * r;
            const R by = R(0.5) * (qy_[i] + qmy_[i]), vy = (qy_[i] - qmy_[i]) / h_ * r;
            qx_[i] = bx + hh * vx; qmx_[i] = bx - hh * vx; qy_[i] = by + hh * vy; qmy_[i] = by - hh * vy;
        }
        P_.assign_from(Pn);
        reset_psi();
        const double E1 = double(energy()), target = std::max(E_old, 0.0);
        R s = R(1);
        if (E1 > target) {
            s = (std::isfinite(E1) && E1 > 0) ? R(target / E1) : R(0);
            for (int i = 0; i < N_; ++i) { qx_[i] *= s; qy_[i] *= s; qmx_[i] *= s; qmy_[i] *= s; }
            reset_psi();
            E_ = double(energy());
        } else {
            E_ = E1;
        }
        return s;
    }

    double tracked_energy() const noexcept { return E_; }
    double resync_energy() noexcept { E_ = double(energy()); return E_; }
    double last_dissipation() const noexcept { return last_pdis_; }
    double last_input_power() const noexcept { return last_pin_; }
    R h() const noexcept { return h_; }
    const Config& config() const noexcept { return cfg_; }

private:
    struct Bond { int i, j; R pxx, pxy, pyy; };
    double lane_sum(const R* x) const noexcept {    // 8-lane partial sums: vectorisable without -ffast-math
        R acc[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < Np_; i += 8)
            for (int l = 0; l < 8; ++l) acc[l] += x[i + l];
        double s = 0; for (int l = 0; l < 8; ++l) s += double(acc[l]);
        return s;
    }
    void apply_K(const R* qx, const R* qy, R* ox, R* oy) const noexcept {
        const R k0 = R(cfg_.k0);
        for (int i = 0; i < N_; ++i) { ox[i] = k0 * qx[i]; oy[i] = k0 * qy[i]; }
        for (const auto& b : bonds_) {
            const R dx = qx[b.i] - qx[b.j], dy = qy[b.i] - qy[b.j];
            const R fx = b.pxx * dx + b.pxy * dy, fy = b.pxy * dx + b.pyy * dy;
            ox[b.i] += fx; oy[b.i] += fy; ox[b.j] -= fx; oy[b.j] -= fy;
        }
    }
    R U(R s, int i) const noexcept {
        switch (cfg_.potential) {
            case Potential::None: return R(0);
            case Potential::Quartic: return R(0.25) * P_.alpha[i] * s * s * s * s;
            case Potential::QuarticLogcoshFast: return potential_fast(s, P_.alpha[i], P_.beta[i]);
            default: return potential(s, P_.alpha[i], P_.beta[i]);
        }
    }
    void reset_psi() noexcept {
        const R c0 = R(cfg_.c0);
        for (int i = 0; i < N_; ++i) {
            px_[i] = std::sqrt(R(2) * U(qx_[i], i) + c0); pmx_[i] = std::sqrt(R(2) * U(qmx_[i], i) + c0);
            py_[i] = std::sqrt(R(2) * U(qy_[i], i) + c0); pmy_[i] = std::sqrt(R(2) * U(qmy_[i], i) + c0);
        }
    }

    const Honeycomb* lat_ = nullptr;
    Config cfg_{};
    int N_ = 0, Np_ = 0; R h_ = R(1);
    std::vector<R> qx_, qy_, qmx_, qmy_, px_, py_, pmx_, pmy_, fx_, fy_, Kx_, Ky_, vbx_, vby_, pd_, pi_;
    std::vector<Bond> bonds_;
    SiteParams<R> P_;
    std::vector<Port<R>> ports_; std::size_t nports_ = 0;
    double E_ = 0, last_pdis_ = 0, last_pin_ = 0;
};

}  // namespace morpho_phs
