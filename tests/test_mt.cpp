// SPDX-License-Identifier: AGPL-3.0-only
// Phase-2 tests for the multithreaded kernel (esav_lf_mt.hpp), the tau knob and the potential switch.
// Build: zig c++ -std=c++20 -O3 -march=native -Iinclude tests/test_mt.cpp -o build/test_mt
#include "morpho_phs/morpho_phs.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>

static std::atomic<long> g_allocs{0};
void* operator new(std::size_t n) { ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, std::align_val_t a) { ++g_allocs; if (void* p = std::aligned_alloc(std::size_t(a), (n + std::size_t(a) - 1) / std::size_t(a) * std::size_t(a))) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { return operator new(n, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

using namespace morpho_phs;
static int fails = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); std::printf("[%s] ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); if (!ok_) ++fails; } while (0)
static const double WU = 2 * M_PI * 1500;

template <class Eng>
static void setup(Eng& eng, const Honeycomb& lat, typename Eng::Config cfg, unsigned seed, bool active) {
    eng.prepare(lat, cfg, 8);
    auto& P = eng.params(); std::mt19937 rng(seed); std::uniform_real_distribution<double> U(0, 1);
    for (int i = 0; i < lat.N; ++i) {
        P.m[i] = 0.7 + 0.6 * U(rng); P.alpha[i] = WU * WU * 0.05 * U(rng); P.beta[i] = 0.1 * WU * WU * 0.2 * U(rng);
        P.mu[i] = active ? 40 * U(rng) : -5 * U(rng); P.eta[i] = 1e-4 * U(rng); P.g[i] = (U(rng) < 0.5 ? -1 : 1) * 0.6 * WU;
    }
    Port<double> pp[4] = {{5, 1, 0}, {lat.N / 3, 0, 1}, {lat.N / 2 + 7, 0.6, 0.8}, {lat.N - 9, 1, 0}};
    eng.set_ports(pp);
    std::vector<double> q0(2 * lat.N), v0(2 * lat.N);
    for (auto& x : q0) x = 1e-3 * (U(rng) - 0.5);
    for (auto& x : v0) x = 2.0 * (U(rng) - 0.5);
    eng.set_state(q0, v0);
}
template <class Eng> static typename Eng::Config mkcfg(Potential pot, int threads = 1) {
    typename Eng::Config c; c.k0 = 0.1 * WU * WU; c.kc = WU * WU; c.potential = pot; c.E_cap = 2e5;
    if constexpr (requires { c.threads; }) c.threads = threads;
    return c;
}
static double input(int n, int k) { return (n % 997 == 13 * k) ? 3e4 : 200.0 * std::sin(0.01 * n * (k + 1)); }

int main() {
    const int L = 32, NS = 12000;
    Honeycomb lat(L, L);
    // ---- M1: bit-exact across thread counts (with ports, gate, ENPU every 64 samples, both potentials) ----
    for (Potential pot : {Potential::Quartic, Potential::QuarticLogcosh}) {
        std::vector<double> yref, qref; bool all_same = true; int tested = 0;
        for (int T : {1, 2, 3, 4, 6, 8}) {
            EsavLFMT<double> eng; setup(eng, lat, mkcfg<EsavLFMT<double>>(pot, T), 7, true);
            ParamMap<double> map; map.base.resize(lat.N); map.base.assign_from(eng.params()); map.g_depth = 1; map.mu_depth = 30; map.mu_max = 60;
            ControlLayer<double, EsavLFMT<double>> ctl; ctl.prepare(lat, map);
            for (int i = 0; i < lat.N; ++i) if (i % 17 == 0) ctl.medium.u_mut()[i] = 1;
            std::vector<double> u(64 * 4), y(64 * 4), yall;
            for (int b = 0; b < NS / 64; ++b) {
                for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = input(b * 64 + n, k);
                eng.process_block(u.data(), y.data(), 64);
                yall.insert(yall.end(), y.begin(), y.end());
                ctl.control_tick(eng);
            }
            std::vector<double> q(2 * lat.N); for (int d = 0; d < 2 * lat.N; ++d) q[d] = eng.q_at(d);
            if (T == 1) { yref = yall; qref = q; }
            else all_same = all_same && std::memcmp(yref.data(), yall.data(), yall.size() * 8) == 0 && std::memcmp(qref.data(), q.data(), q.size() * 8) == 0;
            ++tested;
        }
        CHECK(all_same, "M1 bit-exact outputs and state for threads {1,2,3,4,6,8} vs 1, 32x32, %d samples, ENPU/64 + gate, potential=%s",
              NS, pot == Potential::Quartic ? "quartic" : "quartic+logcosh");
    }
    // ---- M2: MT kernel (gather form) vs Phase-1 EsavLF (scatter form): bounded rounding difference ----
    {
        EsavLFMT<double> a; setup(a, lat, mkcfg<EsavLFMT<double>>(Potential::Quartic, 4), 3, false);
        EsavLF<double> b; setup(b, lat, mkcfg<EsavLF<double>>(Potential::Quartic), 3, false);
        double maxrel = 0, ymax = 0; std::vector<double> u(4), ya(4), yb(4);
        for (int n = 0; n < 4000; ++n) {
            for (int k = 0; k < 4; ++k) u[k] = input(n, k);
            a.process_block(u.data(), ya.data(), 1); b.process_sample(u, yb);
            for (int k = 0; k < 4; ++k) { maxrel = std::max(maxrel, std::abs(ya[k] - yb[k])); ymax = std::max(ymax, std::abs(yb[k])); }
        }
        CHECK(maxrel / ymax < 1e-9, "M2 MT(4 thr) vs Phase-1 single-thread EsavLF, 4000 samples passive quartic: max |dy|/max|y| = %.2e", maxrel / ymax);
    }
    // ---- M3: discrete power balance of the MT kernel (tracked vs recomputed energy, per block) ----
    {
        EsavLFMT<double> e; setup(e, lat, mkcfg<EsavLFMT<double>>(Potential::QuarticLogcosh, 4), 11, true);
        double worst = 0; std::vector<double> u(64 * 4), y(64 * 4);
        double Eprev = e.energy();
        for (int b = 0; b < 100; ++b) {
            for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = input(b * 64 + n, k);
            e.process_block(u.data(), y.data(), 64);
            const double Et = e.tracked_energy(), Er = e.energy();
            worst = std::max(worst, std::abs(Et - Er) / std::max(std::abs(Er), std::abs(Eprev)));
            Eprev = Er;
        }
        CHECK(worst < 1e-9, "M3 MT power balance: tracked (sum of h(u.y - vDv)) vs recomputed energy after each 64-block, max rel diff %.2e", worst);
    }
    // ---- M4: zero heap allocations in the MT process loop incl. control layer (4 threads) ----
    {
        EsavLFMT<double> e; setup(e, lat, mkcfg<EsavLFMT<double>>(Potential::Quartic, 4), 5, true);
        ParamMap<double> map; map.base.resize(lat.N); map.base.assign_from(e.params()); map.g_depth = 1; map.mu_depth = 30; map.mu_max = 60;
        ControlLayer<double, EsavLFMT<double>> ctl; ctl.prepare(lat, map); ctl.set_tau(0.042);
        std::vector<double> u(64 * 4, 1.0), y(64 * 4);
        e.process_block(u.data(), y.data(), 64);
        const long a0 = g_allocs.load();
        for (int b = 0; b < 3000; ++b) { e.process_block(u.data(), y.data(), 64); ctl.control_tick(e); if (b % 2) { e.enpu_keep_psi(e.params()); e.enpu_selective(e.params()); } }
        CHECK(g_allocs.load() - a0 == 0, "M4 zero heap allocations: 192000 samples, 4 threads, control layer + all three ENPU variants (%ld allocs)", g_allocs.load() - a0);
    }
    // ---- M5: potential switch Quartic <-> QuarticLogcosh is passive ----
    {
        EsavLFMT<double> e; setup(e, lat, mkcfg<EsavLFMT<double>>(Potential::Quartic, 2), 9, false);
        std::vector<double> u(4), y(4); double worst = -1e300; int sw = 0;
        for (int n = 0; n < 20000; ++n) {
            for (int k = 0; k < 4; ++k) u[k] = input(n, k) * 50;
            e.process_block(u.data(), y.data(), 1);
            if (n % 500 == 499) {
                const double Eb = e.energy();
                e.set_potential(e.potential() == Potential::Quartic ? Potential::QuarticLogcosh : Potential::Quartic); ++sw;
                worst = std::max(worst, (e.energy() - std::max(Eb, 0.0)) / std::max(std::abs(Eb), 1e-30));
            }
        }
        CHECK(worst <= 1e-12, "M5 %d live switches quartic<->quartic+logcosh: max rel energy increase %.2e (<= 0 up to round-off)", sw, worst);
    }
    // ---- M6: ENPU variants B (keep-state) and C (selective): energy contract + exactness ----
    for (int variant : {1, 2}) {
        EsavLFMT<double> e; setup(e, lat, mkcfg<EsavLFMT<double>>(Potential::Quartic, 2), 13, false);
        std::mt19937 rng(1); std::uniform_real_distribution<double> U(0, 1);
        SiteParams<double> Pn; Pn.resize(lat.N);
        std::vector<double> u(4), y(4); double worst_exact = 0, worst_inc = -1e300; bool state_untouched = true;
        for (int j = 0; j < 600; ++j) {
            for (int n = 0; n < 64; ++n) { for (int k = 0; k < 4; ++k) u[k] = input(j * 64 + n, k) * 20; e.process_block(u.data(), y.data(), 1); }
            Pn.assign_from(e.params());
            const int kind = j % 3;   // 0: g/mu/eta only, 1: + alpha, 2: + mass
            for (int i = 0; i < lat.N; ++i) {
                Pn.g[i] = -Pn.g[i]; Pn.mu[i] = -5 * U(rng); Pn.eta[i] = 1e-4 * U(rng);
                if (kind >= 1 && i % 2) Pn.alpha[i] = WU * WU * 0.05 * U(rng);
                if (kind == 2 && i % 3 == 0) Pn.m[i] = 0.7 + 0.6 * U(rng);
            }
            std::vector<double> qb(2 * lat.N); for (int d = 0; d < 2 * lat.N; ++d) qb[d] = e.q_at(d);
            const double Eb = e.energy();
            if (variant == 1) e.enpu_keep_psi(Pn); else e.enpu_selective(Pn);
            const double Ea = e.energy();
            if (kind == 0) {
                worst_exact = std::max(worst_exact, std::abs(Ea - Eb) / std::abs(Eb));
                for (int d = 0; d < 2 * lat.N; ++d) state_untouched = state_untouched && qb[d] == e.q_at(d);
            }
            worst_inc = std::max(worst_inc, (Ea - std::max(Eb, 0.0)) / std::abs(Eb));
        }
        CHECK(worst_exact < 1e-12 && state_untouched && worst_inc <= 1e-12,
              "M6 ENPU variant %s: g/mu/eta jumps leave q untouched and |dE|/E <= %.1e; max rel increase over all 600 jumps (incl. alpha, mass) %.2e; scaled %lld, fallback-to-A %lld",
              variant == 1 ? "B keep-state" : "C selective", worst_exact, worst_inc, e.enpu_scaled_, e.enpu_fallback_);
    }
    // ---- M7: tau knob ----
    {
        Barkley<double> b; b.set_tau(1.0 / 12, 750); bool ok1 = b.substeps == 1 && std::abs(b.dt - 0.016) < 1e-12;
        b.set_tau(0.021, 750); bool ok2 = b.substeps == 4 && b.dt <= 0.016 && std::abs(b.tau_seconds(750) - 0.021) < 1e-12;
        b.set_tau(0.5, 750); bool ok3 = b.substeps == 1 && std::abs(b.tau_seconds(750) - 0.5) < 1e-12;
        CHECK(ok1 && ok2 && ok3, "M7 tau knob: tau=1/12 s -> dt=0.016,S=1; tau=21 ms -> S=%d, dt=%.5f; tau=0.5 s -> dt=%.5f", 4, 1.0 / (750 * 0.021 * 4), b.dt);
    }
    // ---- M8: gated self-oscillation bound with MT kernel + control layer (Thm. 4) ----
    {
        EsavLFMT<double> e; auto cfg = mkcfg<EsavLFMT<double>>(Potential::Quartic, 4); cfg.E_cap = 1e6; setup(e, lat, cfg, 21, true);
        for (int i = 0; i < lat.N; ++i) { e.params().mu[i] = 60; e.params().m[i] = 1; }
        ParamMap<double> map; map.base.resize(lat.N); map.base.assign_from(e.params()); map.g_depth = 1; map.mu_depth = 60; map.mu_max = 120;
        ControlLayer<double, EsavLFMT<double>> ctl; ctl.prepare(lat, map);
        for (int i = 0; i < lat.N; ++i) if (i % 13 == 0) ctl.medium.u_mut()[i] = 1;
        const double gamma = e.cfl_gamma(), delta = e.h() * 120 / (1.0 * (1 - gamma * gamma)), rho = (1 + delta) / (1 - delta);
        const double Et_cap = cfg.E_cap + 2 * lat.N * cfg.c0 / 2;
        std::vector<double> u(64 * 4, 0.0), y(64 * 4); double Emax = 0, E0 = e.energy() + lat.N * cfg.c0;
        for (int b = 0; b < 48000 * 3 / 64; ++b) { e.process_block(u.data(), y.data(), 64); ctl.control_tick(e); Emax = std::max(Emax, e.energy() + lat.N * cfg.c0); }
        CHECK(Emax <= std::max(E0, rho * Et_cap) && Emax > 0.5 * cfg.E_cap, "M8 MT gated self-oscillation 3 s, 32x32, spiral-modulated: max E~ = %.6e <= max(E~0, rho*E~cap) = %.6e (rho=%.5f), reached cap region",
              Emax, std::max(E0, rho * Et_cap), rho);
    }
    std::printf(fails ? "SOME TESTS FAILED (%d)\n" : "ALL MT TESTS PASSED (%d failures)\n", fails);
    return fails ? 1 : 0;
}
