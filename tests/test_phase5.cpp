// SPDX-License-Identifier: AGPL-3.0-only
// Phase-5 tests: bipartite SIMD stencil kernel vs Phase-2..4 CSR gather (bit-exact), energy identity / passivity with
// the stencil and with E-RSAV, pure-spin barrier (0 in-block OS waits), clean P-core lists, CPU-set mode, no allocations.
// Build: g++ -std=c++20 -O3 -march=x86-64-v3 -fno-math-errno -ffp-contract=off -pthread -Iinclude tests/test_phase5.cpp -o build/p5/test_phase5
#include "morpho_phs/morpho_phs.hpp"
#include "morpho_phs/rt_thread.hpp"
#include "../plugin/Source/engine/MorphoEngine.hpp"   // Phase 5c: Auto-thread / HQ-exact helpers (header-only, no JUCE)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
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
static const char* potname(Potential p) {
    return p == Potential::Quartic ? "quartic" : (p == Potential::QuarticLogcosh ? "HQ" : (p == Potential::QuarticLogcoshFast ? "HQ-fast" : "none"));
}

template <class R>
static typename EsavLFMT<R>::Config cfg_of(int T, int kernel, bool ers, Potential pot) {
    typename EsavLFMT<R>::Config c; c.k0 = 0.1 * WU * WU; c.kc = WU * WU; c.potential = pot; c.E_cap = 2e5;
    c.threads = T; c.kernel = kernel; c.ersav = ers; c.interp_step = 1; c.ftz_workers = true; c.caller_computes = 1;
    return c;
}
// random heterogeneous parameters + "defects" (every 23rd site: g = 0, mass x 2.5), 4 ports, small random state
template <class R>
static void setup(EsavLFMT<R>& eng, const Honeycomb& lat, typename EsavLFMT<R>::Config cfg, unsigned seed, bool active = true) {
    eng.prepare(lat, cfg, 8);
    auto& P = eng.params(); std::mt19937 rng(seed); std::uniform_real_distribution<double> U(0, 1);
    for (int i = 0; i < lat.N; ++i) {
        const bool defect = i % 23 == 5;
        P.m[i] = R((0.7 + 0.6 * U(rng)) * (defect ? 2.5 : 1.0)); P.alpha[i] = R(WU * WU * 0.05 * U(rng)); P.beta[i] = R(0.1 * WU * WU * 0.2 * U(rng));
        P.mu[i] = R(active ? 40 * U(rng) : -5 * U(rng)); P.eta[i] = R(1e-4 * U(rng)); P.g[i] = R(defect ? 0.0 : (U(rng) < 0.5 ? -1 : 1) * 0.6 * WU);
    }
    Port<R> pp[4] = {{5 % lat.N, 1, 0}, {lat.N / 3, 0, 1}, {lat.N / 2 + 1, R(0.6), R(0.8)}, {lat.N - 3, 1, 0}};
    eng.set_ports(pp);
    std::vector<R> q0(2 * lat.N), v0(2 * lat.N);
    for (auto& x : q0) x = R(1e-3 * (U(rng) - 0.5));
    for (auto& x : v0) x = R(2.0 * (U(rng) - 0.5));
    eng.set_state(q0, v0);
}
static double input(int n, int k) { return (n % 997 == 13 * k) ? 3e4 : 200.0 * std::sin(0.01 * n * (k + 1)); }

// run with the control layer (Barkley medium + ParamMap + ENPU-C every 64 samples, per-sample g/mu ramp)
template <class R>
static void run(const Honeycomb& lat, typename EsavLFMT<R>::Config c, int nsamp, std::vector<R>& yall, std::vector<R>& q, double* Erel = nullptr) {
    EsavLFMT<R> eng; setup<R>(eng, lat, c, 7);
    ParamMap<R> map; map.base.resize(lat.N); map.base.assign_from(eng.params()); map.g_depth = 1; map.mu_depth = 30; map.mu_max = 60; map.alpha_depth = R(0.3);
    ControlLayer<R, EsavLFMT<R>> ctl; ctl.enpu_mode = 2; ctl.prepare(lat, map);
    for (int i = 0; i < lat.N; ++i) if (i % 17 == 0) ctl.medium.u_mut()[i] = 1;
    std::vector<R> u(64 * 4), y(64 * 4); yall.clear();
    double worst = 0;
    for (int b = 0; b < nsamp / 64; ++b) {
        for (int n = 0; n < 64; ++n) for (int k = 0; k < 4; ++k) u[n * 4 + k] = R(input(b * 64 + n, k));
        eng.process_block(u.data(), y.data(), 64);
        yall.insert(yall.end(), y.begin(), y.end());
        if (Erel) { const double Et = eng.tracked_energy(), Er = double(eng.energy()); worst = std::max(worst, std::abs(Et - Er) / std::max({std::abs(Er), std::abs(Et), 1.0})); }
        ctl.control_tick(eng);
    }
    q.assign(2 * lat.N, R(0)); for (int d = 0; d < 2 * lat.N; ++d) q[d] = eng.q_at(d);
    if (Erel) *Erel = worst;
}

template <class R>
static bool bitsame(const std::vector<R>& a, const std::vector<R>& b) { return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(R)) == 0; }
template <class R>
static double maxrel(const std::vector<R>& a, const std::vector<R>& b) {
    double m = 0, s = 0; for (auto x : a) s = std::max(s, std::abs(double(x)));
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::abs(double(a[i]) - double(b[i])));
    return m / std::max(s, 1e-300);
}

int main() {
    enable_ftz_daz();
    std::printf("SIMD: %s\n", simd::kHaveAvx2 ? "AVX2 intrinsics" : "scalar fallback");
    // ---- S1: stencil == CSR bit for bit (f32 + f64), all potentials, E-RSAV on/off, T = 1 and 3, several lattices ----
    {
        struct Lt { int lx, ly; } lats[] = {{32, 32}, {16, 16}, {8, 8}, {5, 7}, {3, 11}};
        int n32 = 0, ok32 = 0, n64 = 0, ok64 = 0; double worst64 = 0, worst32 = 0; std::string bad;
        for (auto lt : lats) {
            Honeycomb lat(lt.lx, lt.ly);
            const int NS = (lt.lx == 32) ? 6016 : 3008;
            for (Potential pot : {Potential::Quartic, Potential::QuarticLogcosh, Potential::QuarticLogcoshFast, Potential::None})
                for (bool ers : {false, true})
                    for (int T : {1, 3}) {
                        if (T == 3 && lt.lx != 32 && lt.lx != 5) continue;
                        {
                            std::vector<float> ya, qa, yb, qb;
                            run<float>(lat, cfg_of<float>(T, 0, ers, pot), NS, ya, qa);
                            run<float>(lat, cfg_of<float>(T, 1, ers, pot), NS, yb, qb);
                            const bool s = bitsame(ya, yb) && bitsame(qa, qb); ++n32; ok32 += s;
                            if (!s) { worst32 = std::max(worst32, maxrel(ya, yb)); bad += " f32:" + std::to_string(lt.lx) + "x" + std::to_string(lt.ly) + "/" + potname(pot) + (ers ? "/ers" : "") + "/T" + std::to_string(T); }
                        }
                        {
                            std::vector<double> ya, qa, yb, qb;
                            run<double>(lat, cfg_of<double>(T, 0, ers, pot), NS, ya, qa);
                            run<double>(lat, cfg_of<double>(T, 1, ers, pot), NS, yb, qb);
                            const bool s = bitsame(ya, yb) && bitsame(qa, qb); ++n64; ok64 += s;
                            if (!s) { worst64 = std::max(worst64, maxrel(ya, yb)); bad += " f64:" + std::to_string(lt.lx) + "x" + std::to_string(lt.ly) + "/" + potname(pot) + (ers ? "/ers" : "") + "/T" + std::to_string(T); }
                        }
                    }
        }
        CHECK(ok32 == n32 && ok64 == n64, "S1 stencil kernel bit-identical to CSR gather: f32 %d/%d, f64 %d/%d configs (lattices 32x32,16x16,8x8,5x7,3x11; quartic/HQ/HQ-fast/none; E-RSAV off/on; T=1,3; defects, ENPU-C, per-sample ramp, gate)%s%s",
              ok32, n32, ok64, n64, bad.empty() ? "" : "  mismatches:", bad.c_str());
        if (ok32 != n32 || ok64 != n64) std::printf("   worst rel diff f32 %.3g f64 %.3g\n", worst32, worst64);
    }
    // ---- S1b: long run 48000 samples, 32x32 f64 + f32, HQ and quartic, T = 3 (stencil) vs T = 1 (CSR) ----
    {
        Honeycomb lat(32, 32); bool all = true; std::string r;
        for (Potential pot : {Potential::Quartic, Potential::QuarticLogcosh}) {
            std::vector<double> ya, qa, yb, qb;
            run<double>(lat, cfg_of<double>(1, 0, false, pot), 48000, ya, qa);
            run<double>(lat, cfg_of<double>(3, 1, false, pot), 48000, yb, qb);
            const bool s = bitsame(ya, yb) && bitsame(qa, qb); all = all && s; r += std::string(" ") + potname(pot) + (s ? "=bit-exact" : "=DIFF(" + std::to_string(maxrel(ya, yb)) + ")");
            std::vector<float> fa, fqa, fb, fqb;
            run<float>(lat, cfg_of<float>(1, 0, false, pot), 48000, fa, fqa);
            run<float>(lat, cfg_of<float>(3, 1, false, pot), 48000, fb, fqb);
            const bool t = bitsame(fa, fb) && bitsame(fqa, fqb); all = all && t; r += std::string(" f32-") + potname(pot) + (t ? "=bit-exact" : "=DIFF");
        }
        CHECK(all, "S1b long run (48000 samples = 1 s, 32x32, ENPU-C + ramps + defects): stencil T=3 vs CSR T=1:%s", r.c_str());
    }
    // ---- S2: discrete energy identity (tracked vs recomputed), stencil, f64, with and without E-RSAV ----
    {
        Honeycomb lat(32, 32); std::string r; bool ok = true;
        for (Potential pot : {Potential::Quartic, Potential::QuarticLogcosh, Potential::QuarticLogcoshFast})
            for (bool ers : {false, true}) {
                std::vector<double> y, q; double e = 0;
                run<double>(lat, cfg_of<double>(3, 1, ers, pot), 12032, y, q, &e);
                ok = ok && e < 1e-9; char b[96]; std::snprintf(b, sizeof b, " %s%s=%.1e", potname(pot), ers ? "+ersav" : "", e); r += b;
            }
        CHECK(ok, "S2 energy identity |E_tracked - E(q,psi)| / |E| < 1e-9 over 12032 samples (ports, gate, ENPU-C):%s", r.c_str());
    }
    // ---- S3: passivity: passive sites (mu <= 0), no input -> E^{n+1/2} <= E^{n-1/2} every sample (f64), stencil +/- E-RSAV;
    //          E-RSAV removes SAV energy -> E(ersav) <= E(plain) is NOT required sample-wise, only monotone decay ----
    {
        Honeycomb lat(16, 16); std::string r; bool ok = true;
        for (Potential pot : {Potential::Quartic, Potential::QuarticLogcosh})
            for (bool ers : {false, true}) {
                EsavLFMT<double> e; setup<double>(e, lat, cfg_of<double>(1, 1, ers, pot), 3, false);
                std::vector<double> q0(2 * lat.N), v0(2 * lat.N); std::mt19937 rng(11); std::normal_distribution<double> nd(0, 1);
                for (auto& x : q0) x = 0.3 * nd(rng);
                for (auto& x : v0) x = 3000 * nd(rng);
                e.set_state(q0, v0);
                std::vector<double> u(4, 0.0), y(4);
                double Ep = e.energy(), worst = 0; const double E0 = Ep;
                for (int n = 0; n < 20000; ++n) { e.process_block(u.data(), y.data(), 1); const double E = e.energy(); worst = std::max(worst, (E - Ep) / std::max(E0, 1.0)); Ep = E; }
                ok = ok && worst <= 1e-12; char b[128]; std::snprintf(b, sizeof b, " %s%s: max rise %.1e, E_end/E0 %.3f", potname(pot), ers ? "+ersav" : "", worst, Ep / E0); r += b;
            }
        CHECK(ok, "S3 passivity (16x16, strong initial state, mu<=0, 20000 samples):%s", r.c_str());
    }
    // ---- S4: pure-spin in-block barrier: 0 OS waits inside blocks (caller-computes T=3 and dedicated T=3) ----
    {
        // A single wait > rescue_ns (1 ms) counts as an OS wait.  On a shared VM (this box) a vCPU can be descheduled by the
        // hypervisor for > 1 ms, so each mode gets up to 2 attempts; every attempt is reported.  On bare metal (i9 bench) the
        // CLI column bOSw is the authoritative number.
        Honeycomb lat(32, 32); std::string r; bool ok = true;
        for (int caller : {1, 0}) for (int att = 0; att < 2; ++att) {
            auto c = cfg_of<float>(3, 1, false, Potential::Quartic); c.caller_computes = caller; c.pin = int(rt::PinMode::CpuSet);
            EsavLFMT<float> e; setup<float>(e, lat, c, 5);
            std::vector<float> u(128 * 4, 1.f), y(128 * 4);
            for (int b = 0; b < 400; ++b) {
                e.process_block(u.data(), y.data(), 128);
                if (b % 100 == 99) std::this_thread::sleep_for(std::chrono::milliseconds(8));   // > idle_spin -> idle OS waits allowed
            }
            const bool good = e.barrier_blocked_waits() == 0;
            r += std::string(caller ? " caller" : " dedicated") + "#" + std::to_string(att + 1) + ": in-block OS waits " + std::to_string(e.barrier_blocked_waits()) + ", idle OS waits " +
                 std::to_string(e.idle_os_waits()) + " [" + e.pin_report() + "]";
            if (good) break;
            if (att == 1) ok = false;
        }
        CHECK(ok, "S4 pure-spin per-sample barrier (51200 samples, T=3, pin=cpuset):%s", r.c_str());
    }
    // ---- S5: clean P-core list / defaults on a synthetic i9-13900H and on this machine ----
    {
        rt::Topology i9;
        for (int c = 0; c < 6; ++c) for (int s = 0; s < 2; ++s) { rt::LogicalCpu x; x.id = x.bit = 2 * c + s; x.core = c; x.eff_class = 1; i9.cpus.push_back(x); }
        for (int c = 0; c < 8; ++c) { rt::LogicalCpu x; x.id = x.bit = 12 + c; x.core = 6 + c; x.eff_class = 0; i9.cpus.push_back(x); }
        rt::detail::finish(i9);
        const auto cl = rt::clean_perf_cpus(i9);
        bool ids = cl.size() == 5; for (std::size_t k = 0; k < cl.size() && ids; ++k) ids = cl[k].id == int(2 * (k + 1));
        const auto t = rt::query_topology(); const auto ct = rt::clean_perf_cpus(t);
        CHECK(ids && rt::default_threads(32, i9) == 2 && rt::default_threads(16, i9) == 1 && !ct.empty(),
              "S5 clean P-cores i9-13900H = {2,4,6,8,10} (no core 0, no SMT sibling), default L32 threads = %d; this box (%s): %zu clean P-cores, first id %d",
              rt::default_threads(32, i9), t.source.c_str(), ct.size(), ct.empty() ? -1 : ct[0].id);
    }
    // ---- S5c (Phase 5c): mode/potential-aware Auto threads + HQ-exact limit (plugin engine helpers) ----
    {
        rt::Topology i9;
        for (int c = 0; c < 6; ++c) for (int s = 0; s < 2; ++s) { rt::LogicalCpu x; x.id = x.bit = 2 * c + s; x.core = c; x.eff_class = 1; i9.cpus.push_back(x); }
        for (int c = 0; c < 8; ++c) { rt::LogicalCpu x; x.id = x.bit = 12 + c; x.core = 6 + c; x.eff_class = 0; i9.cpus.push_back(x); }
        rt::detail::finish(i9);
        rt::Topology one;   // a single fast core (+ SMT sibling)
        for (int s = 0; s < 2; ++s) { rt::LogicalCpu x; x.id = x.bit = s; x.core = 0; x.eff_class = 0; one.cpus.push_back(x); }
        rt::detail::finish(one);
        namespace mp = morpho_plugin;
        const int fx = rt::default_threads(32, i9, mp::auto_load(0, 0, 32)), inst = rt::default_threads(32, i9, mp::auto_load(2, 0, 32));
        const int hqf = rt::default_threads(32, i9, mp::auto_load(0, 1, 32)), hqx32 = rt::default_threads(32, i9, mp::auto_load(0, 2, 32));
        const int insthq = rt::default_threads(32, i9, mp::auto_load(2, 1, 32)), l16 = rt::default_threads(16, i9, mp::auto_load(2, 1, 16));
        const int l16x = rt::default_threads(16, i9, mp::auto_load(0, 2, 16)), l8x = rt::default_threads(8, i9, mp::auto_load(0, 2, 8));
        const int one32 = rt::default_threads(32, one, mp::auto_load(2, 1, 32));
        const bool pot = mp::effective_potential_choice(2, 32) == 1 && mp::effective_potential_choice(2, 16) == 2 && mp::effective_potential_choice(2, 8) == 2 &&
                         mp::effective_potential_choice(1, 32) == 1 && mp::effective_potential_choice(0, 32) == 0 && mp::effective_potential_choice(2, 32, true) == 2 &&
                         mp::hq_exact_limited(2, 32) && !mp::hq_exact_limited(2, 16) && mp::high_cpu_setting(1, 32) && mp::high_cpu_setting(2, 32) &&
                         mp::high_cpu_setting(2, 16) && !mp::high_cpu_setting(1, 16) && !mp::high_cpu_setting(0, 32);
        CHECK(fx == 2 && inst == 3 && hqf == 3 && hqx32 == 3 && insthq == 3 && l16 == 1 && l16x == 2 && l8x == 1 && one32 == 1 && pot,
              "S5c Auto threads i9: L32 FX %d / Instrument %d / HQ-fast %d / HQ-exact->fast %d / Inst+HQ %d; L16 %d, L16 HQ-exact %d, L8 HQ-exact %d; "
              "1 P-core machine L32 %d; HQ-exact limited to L<=16 (L32 -> HQ-fast, bench override keeps exact): %s",
              fx, inst, hqf, hqx32, insthq, l16, l16x, l8x, one32, pot ? "yes" : "NO");
    }
    // ---- S6: zero heap allocations in the audio loop (stencil, cpuset, E-RSAV, caller + dedicated) ----
    for (int caller : {1, 0}) {
        auto c = cfg_of<float>(3, 1, true, Potential::QuarticLogcoshFast); c.caller_computes = caller; c.pin = int(rt::PinMode::CpuSet);
        EsavLFMT<float> e; setup<float>(e, Honeycomb(32, 32), c, 5);
        std::vector<float> u(128 * 4, 1.f), y(128 * 4);
        e.process_block(u.data(), y.data(), 128);
        const long a0 = g_allocs.load();
        for (int b = 0; b < 1000; ++b) e.process_block(u.data(), y.data(), 128);
        CHECK(g_allocs.load() - a0 == 0, "S6 zero heap allocations: 128000 samples, T=3, cpuset, E-RSAV, %s (%ld allocs)", caller ? "caller computes" : "dedicated pool", g_allocs.load() - a0);
    }
    // ---- S7: E-RSAV diagnostic: SAV inconsistency with / without relaxation (driven 16x16, quartic) ----
    {
        Honeycomb lat(16, 16); double inc[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            EsavLFMT<double> e; setup<double>(e, lat, cfg_of<double>(1, 1, k == 1, Potential::Quartic), 3, false);
            std::vector<double> u(64 * 4), y(64 * 4); double acc = 0; int cnt = 0;
            for (int b = 0; b < 300; ++b) {
                for (int n = 0; n < 64; ++n) for (int j = 0; j < 4; ++j) u[n * 4 + j] = 4e5 * std::sin(0.4 * (b * 64 + n) * (j + 1));
                e.process_block(u.data(), y.data(), 64);
                if (b >= 50) { acc += e.sav_inconsistency(); ++cnt; }
            }
            inc[k] = acc / cnt;
        }
        CHECK(std::isfinite(inc[0]) && std::isfinite(inc[1]), "S7 E-RSAV diagnostic (16x16 quartic, strong drive): mean SAV inconsistency %.3e (plain) vs %.3e (E-RSAV)", inc[0], inc[1]);
    }
    // ---- S9: vectorised energy() / reset_psi() (instrument ENPU-C hot path) == scalar CSR versions, bit for bit ----
    {
        int n = 0, ok = 0; std::string bad;
        auto one = [&](auto tag, int lx, int ly, Potential pot) {
            using R = decltype(tag);
            Honeycomb lat(lx, ly);
            EsavLFMT<R> a, b; setup<R>(a, lat, cfg_of<R>(1, 0, false, pot), 21); setup<R>(b, lat, cfg_of<R>(1, 1, false, pot), 21);
            std::vector<R> u(64 * 4), y(64 * 4);
            for (int blk = 0; blk < 20; ++blk) {
                for (int k = 0; k < 64 * 4; ++k) u[k] = R(input(blk * 64 + k / 4, k % 4));
                a.process_block(u.data(), y.data(), 64); b.process_block(u.data(), y.data(), 64);
                const R ea = a.energy(), eb = b.energy();
                bool same = std::memcmp(&ea, &eb, sizeof(R)) == 0;
                // reset_psi path through a potential switch (ENPU variant A): energies must stay identical too
                if (blk % 5 == 4) {
                    const Potential np = blk % 10 == 4 ? Potential::QuarticLogcoshFast : pot;
                    a.set_potential(np); b.set_potential(np);
                    const R fa = a.energy(), fb = b.energy(); same = same && std::memcmp(&fa, &fb, sizeof(R)) == 0;
                    for (int d = 0; d < 2 * lat.N && same; ++d) same = a.q_at(d) == b.q_at(d);
                }
                ++n; ok += same;
                if (!same) bad += " " + std::to_string(lx) + "x" + std::to_string(ly) + "/" + potname(pot) + (sizeof(R) == 4 ? "/f32" : "/f64") + "@" + std::to_string(blk);
            }
        };
        for (Potential pot : {Potential::Quartic, Potential::QuarticLogcoshFast, Potential::QuarticLogcosh})
            for (auto lt : {std::pair{32, 32}, std::pair{5, 7}, std::pair{3, 11}}) { one(float{}, lt.first, lt.second, pot); one(double{}, lt.first, lt.second, pot); }
        CHECK(ok == n, "S9 vectorised energy()/reset_psi() == scalar CSR versions bit for bit: %d/%d checks (f32/f64, 32x32, 5x7, 3x11, potential switches)%s", ok, n, bad.c_str());
        // timing of the serial control-rate path (informational)
        Honeycomb lat(32, 32);
        for (int kern : {0, 1}) {
            EsavLFMT<float> e; setup<float>(e, lat, cfg_of<float>(1, kern, false, Potential::Quartic), 3);
            volatile float sink = 0; const int R_ = 2000;
            auto t0 = std::chrono::steady_clock::now();
            for (int r = 0; r < R_; ++r) sink = sink + e.energy();
            const double te = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / R_;
            t0 = std::chrono::steady_clock::now();
            for (int r = 0; r < R_ / 4; ++r) e.set_potential(Potential::Quartic);
            const double tr = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (R_ / 4);
            std::printf("   [info] 32x32 f32 kernel=%s: energy() %.2f us, set_potential (scale+reset_psi+2x energy) %.2f us\n", kern ? "stencil" : "csr", te, tr);
        }
    }
    // ---- S8: pure-spin barrier under oversubscription (busy threads on every CPU): completes, rescue yields counted ----
    {
        const int hw = int(std::max(2u, std::thread::hardware_concurrency()));
        std::atomic<bool> stop{false}; std::vector<std::thread> hogs;
        for (int k = 0; k < hw; ++k) hogs.emplace_back([&] { volatile unsigned long x = 0; while (!stop.load(std::memory_order_relaxed)) x = x + 1; });
        auto c = cfg_of<float>(std::min(8, hw), 1, false, Potential::Quartic); c.pin = 0;
        EsavLFMT<float> e; setup<float>(e, Honeycomb(32, 32), c, 9);
        std::vector<float> u(128 * 4, 1.f), y(128 * 4); double worst = 0, total = 0; const int NB = 100;
        for (int b = 0; b < NB; ++b) {
            const auto t0 = std::chrono::steady_clock::now();
            e.process_block(u.data(), y.data(), 128);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms); total += ms;
        }
        stop = true; for (auto& h : hogs) h.join();
        CHECK(std::isfinite(double(e.tracked_energy())) && e.barrier_blocked_waits() > 0,
              "S8 oversubscribed pure-spin (%d busy threads + %d compute threads on %d CPUs): %d blocks, mean %.2f ms, max %.2f ms per block, %lld rescue yields (only after a single wait > 1 ms)",
              hw, c.threads, hw, NB, total / NB, worst, e.barrier_blocked_waits());
    }
    std::printf("%s: %d failure(s)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
