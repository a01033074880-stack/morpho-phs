// SPDX-License-Identifier: AGPL-3.0-only
// Phase-3 tests: HQ-fast potential (sqrt-sum log-cosh surrogate) + in-kernel g/mu ramp.
// Build: g++ -std=c++20 -O3 -march=x86-64-v3 -fno-math-errno -Iinclude tests/test_phase3.cpp -o build/p3/test_phase3
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

template <class R>
static void setup(EsavLFMT<R>& eng, const Honeycomb& lat, typename EsavLFMT<R>::Config cfg, unsigned seed, bool active) {
    eng.prepare(lat, cfg, 8);
    auto& P = eng.params(); std::mt19937 rng(seed); std::uniform_real_distribution<double> U(0, 1);
    for (int i = 0; i < lat.N; ++i) {
        P.m[i] = R(0.7 + 0.6 * U(rng)); P.alpha[i] = R(WU * WU * 0.05 * U(rng)); P.beta[i] = R(0.1 * WU * WU * 0.2 * U(rng));
        P.mu[i] = R(active ? 40 * U(rng) : -5 * U(rng)); P.eta[i] = R(1e-4 * U(rng)); P.g[i] = R((U(rng) < 0.5 ? -1 : 1) * 0.6 * WU);
    }
    Port<R> pp[4] = {{5, 1, 0}, {lat.N / 3, 0, 1}, {lat.N / 2 + 7, R(0.6), R(0.8)}, {lat.N - 9, 1, 0}};
    eng.set_ports(pp);
    std::vector<R> q0(2 * lat.N), v0(2 * lat.N);
    for (auto& x : q0) x = R(1e-3 * (U(rng) - 0.5));
    for (auto& x : v0) x = R(2.0 * (U(rng) - 0.5));
    eng.set_state(q0, v0);
}
template <class R> static typename EsavLFMT<R>::Config mkcfg(Potential pot, int threads, int interp) {
    typename EsavLFMT<R>::Config c; c.k0 = 0.1 * WU * WU; c.kc = WU * WU; c.potential = pot; c.E_cap = 2e5; c.threads = threads; c.interp_step = interp; return c;
}
static double input(int n, int k) { return (n % 997 == 13 * k) ? 3e4 : 200.0 * std::sin(0.01 * n * (k + 1)); }

int main() {
    const int L = 32, NS = 12000; Honeycomb lat(L, L);
    // ---- P1: surrogate properties (scalar, dense grid) ----
    {
        double e0 = 0, e1 = 0, cons = 0, minU2 = 1e300, minU = 1e300, derr = 0;
        for (int k = 0; k <= 200000; ++k) {
            const double s = k * 5e-5 * (1 + k * 1e-3);   // up to ~2e3
            const double lc = logcosh(s), th = std::tanh(s), Lf = logcosh_fast(s), dLf = dlogcosh_fast(s);
            if (s <= 10) e0 = std::max(e0, std::abs(Lf - lc));
            e1 = std::max(e1, std::abs(dLf - th)); minU = std::min(minU, Lf);
            const double hh = 1e-4 * std::max(1.0, s);   // finite-difference check that dL is the derivative of L
            derr = std::max(derr, std::abs((logcosh_fast(s + hh) - logcosh_fast(s - hh)) / (2 * hh) - dLf));
            double u2 = 0; for (int j = 0; j < 3; ++j) u2 += lcfast::w[j] * lcfast::c[j] * lcfast::c[j] / std::pow(lcfast::c[j] * lcfast::c[j] + s * s, 1.5);
            minU2 = std::min(minU2, u2);
        }
        double sw = 0, swc = 0; for (int j = 0; j < 3; ++j) { sw += lcfast::w[j]; swc += lcfast::w[j] / lcfast::c[j]; }
        cons = std::max(std::abs(sw - 1), std::abs(swc - 1));
        CHECK(e0 < 1.3e-3 && e1 < 1.3e-3 && minU >= 0 && minU2 > 0 && cons < 1e-12 && derr < 1e-6,
              "P1 surrogate: |L-logcosh|<=%.2e (|s|<=10), |L'-tanh|<=%.2e, min L=%.1e, min L''=%.2e>0, constraints %.1e, FD derivative err %.1e",
              e0, e1, minU, minU2, cons, derr);
    }
    // ---- P2: bit-exact across thread counts (HQ-fast, ENPU-C, ramp step 8, gate) f64 and f32 ----
    for (int prec = 0; prec < 2; ++prec) {
        bool all_same = true;
        auto run = [&](auto tag) {
            using R = decltype(tag);
            std::vector<R> yref, qref;
            for (int T : {1, 2, 3, 4, 6, 8}) {
                EsavLFMT<R> eng; setup(eng, lat, mkcfg<R>(Potential::QuarticLogcoshFast, T, 8), 7, true);
                ParamMap<R> map; map.base.resize(lat.N); map.base.assign_from(eng.params()); map.g_depth = 1; map.mu_depth = 30; map.mu_max = 60;
                ControlLayer<R, EsavLFMT<R>> ctl; ctl.enpu_mode = 2; ctl.prepare(lat, map);
                for (int i = 0; i < lat.N; ++i) if (i % 17 == 0) ctl.medium.u_mut()[i] = 1;
                std::vector<R> u(64 * 4), y(64 * 4), yall;
                for (int b = 0; b < NS / 64; ++b) {
                    for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = R(input(b * 64 + n, k));
                    eng.process_block(u.data(), y.data(), 64); yall.insert(yall.end(), y.begin(), y.end()); ctl.control_tick(eng);
                }
                std::vector<R> q(2 * lat.N); for (int d = 0; d < 2 * lat.N; ++d) q[d] = eng.q_at(d);
                if (T == 1) { yref = yall; qref = q; }
                else all_same = all_same && std::memcmp(yref.data(), yall.data(), yall.size() * sizeof(R)) == 0 && std::memcmp(qref.data(), q.data(), q.size() * sizeof(R)) == 0;
            }
        };
        if (prec) run(double{}); else run(float{});
        CHECK(all_same, "P2 %s bit-exact outputs+state, threads {1,2,3,4,6,8}, 32x32, %d samples, HQ-fast, ENPU-C/64, ramp step 8, gate", prec ? "f64" : "f32", NS);
    }
    // ---- P3: discrete power balance with HQ-fast and the g/mu ramp active (mu > 0 regions, gate) ----
    {
        EsavLFMT<double> e; setup(e, lat, mkcfg<double>(Potential::QuarticLogcoshFast, 4, 8), 11, true);
        std::mt19937 rng(3); std::uniform_real_distribution<double> U(0, 1);
        double worst = 0; std::vector<double> u(64 * 4), y(64 * 4); double Eprev = e.energy();
        for (int b = 0; b < 200; ++b) {
            for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = input(b * 64 + n, k);
            for (int i = 0; i < lat.N; ++i) { e.params().g[i] = -e.params().g[i]; e.params().mu[i] = 40 * U(rng) - 10; }   // ramped next block
            e.process_block(u.data(), y.data(), 64);
            const double Et = e.tracked_energy(), Er = e.energy();
            worst = std::max(worst, std::abs(Et - Er) / std::max(std::abs(Er), std::abs(Eprev))); Eprev = Er;
        }
        CHECK(worst < 1e-9, "P3 power balance HQ-fast + ramp (g flips, mu jumps every block): tracked vs recomputed energy max rel diff %.2e", worst);
    }
    // ---- P4: passivity (mu <= 0, eta >= 0): E_end <= E0 + input work, HQ-fast, f64 and f32 ----
    for (int prec = 0; prec < 2; ++prec) {
        bool ok = true; double rel = 0;
        auto run = [&](auto tag) {
            using R = decltype(tag);
            EsavLFMT<R> e; setup(e, lat, mkcfg<R>(Potential::QuarticLogcoshFast, 4, 8), 5, false);
            std::vector<R> u(64 * 4), y(64 * 4); double W = 0; const double E0 = double(e.energy());
            for (int b = 0; b < 750; ++b) {
                for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = R(input(b * 64 + n, k) * 30);
                W += e.process_block(u.data(), y.data(), 64);
                const double E = double(e.energy()); rel = std::max(rel, (E - (E0 + W)) / std::max(1.0, std::abs(E0 + W)));
            }
            ok = rel <= (sizeof(R) == 8 ? 1e-9 : 1e-3);
        };
        if (prec) run(double{}); else run(float{});
        CHECK(ok, "P4 %s passivity HQ-fast, 48000 samples, strong input: max (E - (E0 + work))/|E0+work| = %.2e", prec ? "f64" : "f32", rel);
    }
    // ---- P5: live potential switches Quartic -> HQ-fast -> HQ -> ... are passive ----
    {
        EsavLFMT<double> e; setup(e, lat, mkcfg<double>(Potential::Quartic, 2, 8), 9, false);
        std::vector<double> u(4), y(4); double worst = -1e300; int sw = 0;
        const Potential cyc[3] = {Potential::Quartic, Potential::QuarticLogcoshFast, Potential::QuarticLogcosh};
        for (int n = 0; n < 15000; ++n) {
            for (int k = 0; k < 4; ++k) u[k] = input(n, k) * 50;
            e.process_block(u.data(), y.data(), 1);
            if (n % 500 == 499) {
                const double Eb = e.energy(); ++sw; e.set_potential(cyc[sw % 3]);
                worst = std::max(worst, (e.energy() - std::max(Eb, 0.0)) / std::max(std::abs(Eb), 1e-30));
            }
        }
        CHECK(worst <= 1e-12, "P5 %d live switches quartic -> HQ-fast -> HQ: max rel energy increase %.2e", sw, worst);
    }
    // ---- P6: HQ-fast vs HQ trajectories (same params, 0.25 s): relative output difference ----
    {
        EsavLFMT<double> a, b; setup(a, lat, mkcfg<double>(Potential::QuarticLogcosh, 2, 0), 17, false); setup(b, lat, mkcfg<double>(Potential::QuarticLogcoshFast, 2, 0), 17, false);
        std::vector<double> u(64 * 4), ya(64 * 4), yb(64 * 4); double num = 0, den = 0;
        for (int blk = 0; blk < 12000 / 64; ++blk) {
            for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = input(blk * 64 + n, k) * 30;
            a.process_block(u.data(), ya.data(), 64); b.process_block(u.data(), yb.data(), 64);
            for (int i = 0; i < 256; ++i) { num += (ya[i] - yb[i]) * (ya[i] - yb[i]); den += ya[i] * ya[i]; }
        }
        CHECK(std::isfinite(num) && den > 0, "P6 HQ-fast vs HQ output, 12000 samples, 32x32: relative RMS difference %.2e (%.1f dB)  [informative]", std::sqrt(num / den), 10 * std::log10(num / den));
    }
    // ---- P7: zero heap allocations, HQ-fast + ramp + ControlLayer(ENPU-C), 4 threads ----
    {
        EsavLFMT<float> e; setup(e, lat, mkcfg<float>(Potential::QuarticLogcoshFast, 4, 8), 5, true);
        ParamMap<float> map; map.base.resize(lat.N); map.base.assign_from(e.params()); map.g_depth = 1; map.mu_depth = 30; map.mu_max = 60;
        ControlLayer<float, EsavLFMT<float>> ctl; ctl.enpu_mode = 2; ctl.prepare(lat, map);
        std::vector<float> u(512 * 4, 1.0f), y(512 * 4);
        e.process_block(u.data(), y.data(), 64);
        const long a0 = g_allocs.load();
        for (int b = 0; b < 2000; ++b) { e.process_block(u.data(), y.data(), (b % 3 == 0) ? 512 : 64); ctl.control_tick(e); }
        CHECK(g_allocs.load() - a0 == 0, "P7 zero heap allocations: 2000 blocks (64/512), f32, 4 threads, HQ-fast, ramp, ENPU-C (%ld allocs)", g_allocs.load() - a0);
    }
    std::printf(fails ? "SOME TESTS FAILED (%d)\n" : "ALL PHASE-3 ENGINE TESTS PASSED (%d failures)\n", fails);
    return fails ? 1 : 0;
}
