// SPDX-License-Identifier: AGPL-3.0-only
// Phase-4 tests: hybrid barrier, dedicated pinned pool, MMCSS no-op, padded partials — determinism, RT safety, topology.
// Build: g++ -std=c++20 -O3 -march=x86-64-v3 -fno-math-errno -pthread -Iinclude tests/test_phase4.cpp -o build/p4/test_phase4
#include "morpho_phs/morpho_phs.hpp"
#include "morpho_phs/rt_thread.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <thread>

static std::atomic<long> g_allocs{0};
void* operator new(std::size_t n) { ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
#if defined(_WIN32)
#include <malloc.h>
static void* al_alloc(std::size_t n, std::size_t a) { return _aligned_malloc(n ? n : 1, a); }
static void al_free(void* p) { _aligned_free(p); }
#else
static void* al_alloc(std::size_t n, std::size_t a) { return std::aligned_alloc(a, (n + a - 1) / a * a); }
static void al_free(void* p) { std::free(p); }
#endif
void* operator new(std::size_t n, std::align_val_t a) { ++g_allocs; if (void* p = al_alloc(n, std::size_t(a))) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { return operator new(n, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { al_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { al_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { al_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { al_free(p); }

using namespace morpho_phs;
static int fails = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); std::printf("[%s] ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); if (!ok_) ++fails; } while (0)
static const double WU = 2 * M_PI * 1500;

template <class R>
static void setup(EsavLFMT<R>& eng, const Honeycomb& lat, typename EsavLFMT<R>::Config cfg, unsigned seed) {
    eng.prepare(lat, cfg, 8);
    auto& P = eng.params(); std::mt19937 rng(seed); std::uniform_real_distribution<double> U(0, 1);
    for (int i = 0; i < lat.N; ++i) {
        P.m[i] = R(0.7 + 0.6 * U(rng)); P.alpha[i] = R(WU * WU * 0.05 * U(rng)); P.beta[i] = R(0.1 * WU * WU * 0.2 * U(rng));
        P.mu[i] = R(40 * U(rng)); P.eta[i] = R(1e-4 * U(rng)); P.g[i] = R((U(rng) < 0.5 ? -1 : 1) * 0.6 * WU);
    }
    Port<R> pp[4] = {{5, 1, 0}, {lat.N / 3, 0, 1}, {lat.N / 2 + 7, R(0.6), R(0.8)}, {lat.N - 9, 1, 0}};
    eng.set_ports(pp);
    std::vector<R> q0(2 * lat.N), v0(2 * lat.N);
    for (auto& x : q0) x = R(1e-3 * (U(rng) - 0.5));
    for (auto& x : v0) x = R(2.0 * (U(rng) - 0.5));
    eng.set_state(q0, v0);
}
template <class R> static typename EsavLFMT<R>::Config cfg_of(int T, int pin, int caller, bool mmcss, Potential pot) {
    typename EsavLFMT<R>::Config c; c.k0 = 0.1 * WU * WU; c.kc = WU * WU; c.potential = pot; c.E_cap = 2e5;
    c.threads = T; c.pin = pin; c.caller_computes = caller; c.mmcss = mmcss; c.interp_step = 1; c.ftz_workers = true;
    // Phase 5 changed the defaults (pure-spin barrier, 3.5 ms idle spin, stencil kernel); these tests keep checking the
    // Phase-4 protocol (hybrid barrier, 200/400 us idle wait, adaptive spin, CSR gather).  test_phase5 covers the new one.
    c.inblock_os_wait = true; c.idle_spin_ns = 200000; c.idle_yield_ns = 400000; c.adaptive_spin = true; c.kernel = 0;
    return c;
}
static double input(int n, int k) { return (n % 997 == 13 * k) ? 3e4 : 200.0 * std::sin(0.01 * n * (k + 1)); }

template <class R>
static bool run_variant(const Honeycomb& lat, typename EsavLFMT<R>::Config c, std::vector<R>& yall, std::vector<R>& q, std::string* rep = nullptr) {
    EsavLFMT<R> eng; setup<R>(eng, lat, c, 7);
    ParamMap<R> map; map.base.resize(lat.N); map.base.assign_from(eng.params()); map.g_depth = 1; map.mu_depth = 30; map.mu_max = 60;
    ControlLayer<R, EsavLFMT<R>> ctl; ctl.enpu_mode = 2; ctl.prepare(lat, map);
    for (int i = 0; i < lat.N; ++i) if (i % 17 == 0) ctl.medium.u_mut()[i] = 1;
    std::vector<R> u(64 * 4), y(64 * 4); yall.clear();
    for (int b = 0; b < 6000 / 64; ++b) {
        for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = R(input(b * 64 + n, k));
        eng.process_block(u.data(), y.data(), 64);
        yall.insert(yall.end(), y.begin(), y.end());
        ctl.control_tick(eng);
    }
    q.assign(2 * lat.N, R(0)); for (int d = 0; d < 2 * lat.N; ++d) q[d] = eng.q_at(d);
    if (rep) *rep = eng.pin_report();
    return std::isfinite(double(eng.tracked_energy()));
}

int main() {
    enable_ftz_daz();
    const int L = 32;
    Honeycomb lat(L, L);
    // ---- Q1: bit-exact across thread counts AND pool / pin / MMCSS modes (f32 plugin precision, per-sample ramp, ENPU-C) ----
    for (Potential pot : {Potential::Quartic, Potential::QuarticLogcoshFast}) {
        std::vector<float> yref, qref, y, q; bool same = true; int n = 0; std::string reps;
        run_variant<float>(lat, cfg_of<float>(1, 0, -1, false, pot), yref, qref);
        struct V { int T, pin, caller; bool mm; } vs[] = {{3, 0, 1, false}, {5, 0, 1, false}, {4, 0, 0, false}, {4, 1, 0, false}, {4, 1, 1, false},
                                                          {6, 2, 0, false}, {5, 1, 0, true}, {1, 1, 0, true}, {8, 1, 0, false}};
        for (auto v : vs) {
            std::string r;
            run_variant<float>(lat, cfg_of<float>(v.T, v.pin, v.caller, v.mm, pot), y, q, &r);
            const bool s = y.size() == yref.size() && std::memcmp(y.data(), yref.data(), y.size() * 4) == 0 && std::memcmp(q.data(), qref.data(), q.size() * 4) == 0;
            if (!s) std::printf("   mismatch: T=%d pin=%d caller=%d mmcss=%d (%s)\n", v.T, v.pin, v.caller, int(v.mm), r.c_str());
            same = same && s; ++n;
            if (v.T == 5 && v.pin == 1) reps = r;
        }
        CHECK(same, "Q1 bit-exact f32 output+state vs 1 thread for %d pool/pin/MMCSS variants (T 1..8, caller/dedicated, pcores/pset), 32x32, potential=%s  [e.g. %s]",
              n, pot == Potential::Quartic ? "quartic" : "hq-fast", reps.c_str());
    }
    // ---- Q2: f64 bit-exactness for the dedicated pinned pool vs caller-computes ----
    {
        std::vector<double> ya, qa, yb, qb;
        run_variant<double>(lat, cfg_of<double>(1, 0, -1, false, Potential::QuarticLogcosh), ya, qa);
        run_variant<double>(lat, cfg_of<double>(4, 1, 0, true, Potential::QuarticLogcosh), yb, qb);
        CHECK(std::memcmp(ya.data(), yb.data(), ya.size() * 8) == 0 && std::memcmp(qa.data(), qb.data(), qa.size() * 8) == 0,
              "Q2 bit-exact f64 (HQ exact): dedicated pinned pool (4 thr, MMCSS requested) vs 1 thread");
    }
    // ---- Q3: zero heap allocations in the process loop: dedicated pinned pool + caller-computes pool ----
    for (int caller : {0, 1}) {
        EsavLFMT<float> e; setup<float>(e, lat, cfg_of<float>(4, 1, caller, true, Potential::Quartic), 5);
        std::vector<float> u(128 * 4, 1.f), y(128 * 4);
        e.process_block(u.data(), y.data(), 128);
        const long a0 = g_allocs.load();
        for (int b = 0; b < 1500; ++b) e.process_block(u.data(), y.data(), 128);
        CHECK(g_allocs.load() - a0 == 0, "Q3 zero heap allocations: 192000 samples, 4 threads, pin=pcores, %s (%ld allocs)",
              caller ? "caller computes" : "dedicated pool", g_allocs.load() - a0);
    }
    // ---- Q4: topology / default thread count ----
    {
        const auto t = rt::query_topology();
        const auto pc = rt::perf_cpus(t);
        CHECK(!t.cpus.empty() && t.perf_physical_cores >= 1 && int(pc.size()) == t.perf_physical_cores && t.physical_cores >= t.perf_physical_cores,
              "Q4 topology (%s): %zu logical, %d physical, %d perf physical, hybrid=%d; default threads L8/16/32 = %d/%d/%d",
              t.source.c_str(), t.cpus.size(), t.physical_cores, t.perf_physical_cores, int(t.hybrid),
              rt::default_threads(8, t), rt::default_threads(16, t), rt::default_threads(32, t));
        rt::Topology i9;   // synthetic i9-13900H: 6 P-cores x 2 SMT (class 1) + 8 E-cores (class 0)
        for (int c = 0; c < 6; ++c) for (int s = 0; s < 2; ++s) { rt::LogicalCpu x; x.id = x.bit = 2 * c + s; x.core = c; x.eff_class = 1; i9.cpus.push_back(x); }
        for (int c = 0; c < 8; ++c) { rt::LogicalCpu x; x.id = x.bit = 12 + c; x.core = 6 + c; x.eff_class = 0; i9.cpus.push_back(x); }
        rt::detail::finish(i9);
        const auto p9 = rt::perf_cpus(i9);
        bool ids = p9.size() == 6; for (std::size_t k = 0; k < p9.size(); ++k) ids = ids && p9[k].id == int(2 * k);
        CHECK(i9.hybrid && i9.perf_physical_cores == 6 && i9.physical_cores == 14 && ids && rt::default_threads(32, i9) == 2 && rt::default_threads(16, i9) == 1,
              "Q4b synthetic i9-13900H topology: P=6 of 14 physical, perf cpus = {0,2,4,6,8,10} (one per P-core, no SMT sibling), default L32 threads = %d (Phase 5b: 2 = caller + 1 worker)",
              rt::default_threads(32, i9));
    }
    // ---- Q5: hybrid barrier under oversubscription: no lost wake-ups, bounded block time ----
    {
        const int hw = int(std::max(2u, std::thread::hardware_concurrency()));
        std::atomic<bool> stop{false};
        std::vector<std::thread> hogs;
        for (int k = 0; k < hw; ++k) hogs.emplace_back([&] { volatile unsigned long x = 0; while (!stop.load(std::memory_order_relaxed)) x = x + 1; });
        EsavLFMT<float> e; auto c = cfg_of<float>(std::min(8, hw), 0, 1, false, Potential::Quartic); setup<float>(e, lat, c, 9);
        std::vector<float> u(128 * 4, 1.f), y(128 * 4);
        double worst = 0, total = 0; const int NB = 200;
        for (int b = 0; b < NB; ++b) {
            const auto t0 = std::chrono::steady_clock::now();
            e.process_block(u.data(), y.data(), 128);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms); total += ms;
        }
        stop = true; for (auto& h : hogs) h.join();
        CHECK(std::isfinite(double(e.tracked_energy())) && e.barrier_blocked_waits() > 0,
              "Q5 oversubscribed (%d busy threads + %d workers on %d CPUs): %d blocks done, mean %.2f ms, max %.2f ms per 128-sample block, %lld barrier waits fell back to the OS",
              hw, c.threads, hw, NB, total / NB, worst, e.barrier_blocked_waits());
    }
    std::printf("%s: %d failure(s)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
