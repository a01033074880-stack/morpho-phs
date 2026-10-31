// SPDX-License-Identifier: AGPL-3.0-only
// Phase-1 C++ core tests.  Build: zig c++ -std=c++20 -O2 -Iinclude tests/test_main.cpp -o build/test_main
#include "morpho_phs/morpho_phs.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <random>
#include <sstream>
#include <string>

static std::atomic<long> g_allocs{0};
void* operator new(std::size_t n) { ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace morpho_phs;
static int fails = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); std::printf("[%s] ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); if (!ok_) ++fails; } while (0)

static std::vector<double> read_line(std::istream& in) { std::string s; std::getline(in, s); std::istringstream ss(s); std::vector<double> v; double x; while (ss >> x) v.push_back(x); return v; }

int main(int argc, char** argv) {
    const std::string ref = argc > 1 ? argv[1] : "tests/ref_L4.txt";
    // ---------- T1: cross-check against the Python reference (L=4, ports, ENPU at n=199) ----------
    {
        std::ifstream in(ref);
        if (!in) { std::printf("[FAIL] cannot open %s\n", ref.c_str()); return 1; }
        auto hdr = read_line(in), qref = read_line(in), yref = read_line(in), Eref = read_line(in);
        const int L = int(hdr[0]); Honeycomb lat(L, L);
        EsavLF<double> eng; EsavLF<double>::Config cfg; cfg.fs = hdr[1]; cfg.k0 = hdr[2]; cfg.kc = hdr[3];
        eng.prepare(lat, cfg, 8);
        const int N = lat.N; const double gA = hdr[4];
        auto setp = [&](SiteParams<double>& P, double ph) {
            for (int i = 0; i < N; ++i) {
                P.m[i] = 1 + 0.3 * std::sin(i + ph); P.alpha[i] = cfg.kc * 0.1 * (1 + std::cos(double(i)));
                P.beta[i] = cfg.k0 * (1 + 0.5 * std::sin(2.0 * i)); P.mu[i] = -5 - 3 * std::cos(double(i)); P.eta[i] = 1e-6; P.g[i] = gA * std::cos(0.7 * i + ph);
            }
        };
        setp(eng.params(), 0.0);
        std::vector<double> q0(2 * N), v0(2 * N);
        for (int k = 0; k < 2 * N; ++k) { q0[k] = 0.3 * std::sin(1.3 * k); v0[k] = 200 * std::cos(0.9 * k); }
        eng.set_state(q0, v0);
        Port<double> ports[3] = {{3, 1.0, 0.0}, {17, 0.0, 1.0}, {30, 0.6, 0.8}}; eng.set_ports(ports);
        SiteParams<double> P2; P2.resize(N); setp(P2, 0.5);
        double maxy = 0, maxdy = 0, maxdE = 0, maxE = 0, early = 0; double u[3], y[3]; const double h = 1.0 / cfg.fs;
        for (int n = 0; n < 400; ++n) {
            u[0] = 1e4 * std::sin(2 * M_PI * 440 * n * h); u[1] = 5e3 * std::cos(2 * M_PI * 660 * n * h); u[2] = (n % 50 == 0) ? 2e3 : 0.0;
            eng.process_sample(u, y);
            for (int k = 0; k < 3; ++k) { maxy = std::max(maxy, std::abs(yref[3 * n + k])); maxdy = std::max(maxdy, std::abs(y[k] - yref[3 * n + k])); }
            if (n == 50) early = maxdy / maxy;
            if (std::getenv("MORPHO_TRACE") && n % 25 == 0) std::printf("   n=%d  rel y err so far %.2e\n", n, maxdy / maxy);
            if (n == 199) eng.enpu(P2);
            maxE = std::max(maxE, std::abs(Eref[n + 1])); maxdE = std::max(maxdE, std::abs(double(eng.energy()) - Eref[n + 1]));
        }
        double maxq = 0, maxdq = 0; auto qat = [&](int k) { return eng.q_at(k); };
        for (int k = 0; k < 2 * N; ++k) { maxq = std::max(maxq, std::abs(qref[k])); maxdq = std::max(maxdq, std::abs(qat(k) - qref[k])); }
        // strongly nonlinear regime: round-off differences (op order, logcosh formula) grow ~x10 per 25-50 steps,
        // so the tight check is on the first 50 steps; the 400-step check bounds the amplified round-off.
        CHECK(early < 1e-11 && maxdq / maxq < 1e-5 && maxdy / maxy < 1e-5 && maxdE / maxE < 1e-8,
              "T1 C++ vs Python reference (L=4, 3 ports, ENPU@199): rel err y(<=50 steps) %.2e; after 400 steps q %.2e, y %.2e, E %.2e", early, maxdq / maxq, maxdy / maxy, maxdE / maxE);
    }
    // ---------- T2: discrete power balance every step, 8x8, random params, active+passive, double ----------
    {
        Honeycomb lat(8, 8); const int N = lat.N; const double wu = 2 * M_PI * 1500;
        EsavLF<double> eng; EsavLF<double>::Config cfg; cfg.k0 = 0.1 * wu * wu; cfg.kc = wu * wu; eng.prepare(lat, cfg, 4);
        std::mt19937_64 rng(7); std::uniform_real_distribution<double> U01(0, 1);
        auto& P = eng.params();
        for (int i = 0; i < N; ++i) { P.m[i] = 0.5 + 1.5 * U01(rng); P.alpha[i] = cfg.kc * U01(rng); P.beta[i] = cfg.k0 * U01(rng); P.mu[i] = 60 * (U01(rng) - 0.5); P.eta[i] = 1e-4 * U01(rng); P.g[i] = 0.6 * wu * (2 * U01(rng) - 1); }
        std::vector<double> q0(2 * N), v0(2 * N);
        for (int k = 0; k < 2 * N; ++k) { q0[k] = 2 * (U01(rng) - 0.5); v0[k] = 3e3 * (U01(rng) - 0.5); }
        eng.set_state(q0, v0);
        Port<double> ports[4] = {{5, 1, 0}, {40, 0, 1}, {77, 0.6, 0.8}, {120, 0.8, -0.6}}; eng.set_ports(ports);
        double worst = 0, u[4], y[4]; double E = eng.energy();
        for (int n = 0; n < 4000; ++n) {
            for (auto& x : u) x = 1e5 * (U01(rng) - 0.5);
            eng.process_sample(u, y);
            const double En = eng.energy(), h = eng.h();
            const double res = En - E - h * (eng.last_input_power() - eng.last_dissipation());
            worst = std::max(worst, std::abs(res) / (std::abs(E) + std::abs(En) + 1e-300));
            E = En;
        }
        CHECK(worst < 1e-10, "T2 per-step discrete power balance, 8x8, 4000 steps, random active params: max rel residual %.2e", worst);
        CHECK(std::abs(eng.tracked_energy() - E) / std::abs(E) < 1e-8, "T2b incremental energy tracker drift after 4000 steps: rel %.2e", std::abs(eng.tracked_energy() - E) / std::abs(E));
    }
    // ---------- T2c: same balance with the vectorised quartic-only potential (fast path) ----------
    {
        Honeycomb lat(8, 8); const int N = lat.N; const double wu = 2 * M_PI * 1500;
        EsavLF<double> eng; EsavLF<double>::Config cfg; cfg.k0 = 0.1 * wu * wu; cfg.kc = wu * wu; cfg.potential = Potential::Quartic; eng.prepare(lat, cfg, 1);
        std::mt19937_64 rng(17); std::uniform_real_distribution<double> U01(0, 1);
        auto& P = eng.params();
        for (int i = 0; i < N; ++i) { P.m[i] = 0.5 + 1.5 * U01(rng); P.alpha[i] = cfg.kc * 10 * U01(rng); P.mu[i] = 60 * (U01(rng) - 0.5); P.eta[i] = 1e-4 * U01(rng); P.g[i] = 0.6 * wu * (2 * U01(rng) - 1); }
        std::vector<double> q0(2 * N), v0(2 * N);
        for (int k = 0; k < 2 * N; ++k) { q0[k] = 2 * (U01(rng) - 0.5); v0[k] = 3e3 * (U01(rng) - 0.5); }
        eng.set_state(q0, v0);
        Port<double> ports[1] = {{9, 0.6, 0.8}}; eng.set_ports(ports);
        double worst = 0, u[1], y[1]; double E = eng.energy();
        for (int n = 0; n < 4000; ++n) {
            u[0] = 1e5 * (U01(rng) - 0.5); eng.process_sample(u, y);
            const double En = eng.energy(), h = eng.h();
            worst = std::max(worst, std::abs(En - E - h * (eng.last_input_power() - eng.last_dissipation())) / (std::abs(E) + std::abs(En) + 1e-300)); E = En;
        }
        CHECK(worst < 1e-10, "T2c power balance, Potential::Quartic fast path, 8x8, 4000 steps: max rel residual %.2e", worst);
    }
    // ---------- T3: ENPU never increases energy (random parameter jumps incl. mass changes) ----------
    {
        Honeycomb lat(8, 8); const int N = lat.N; const double wu = 2 * M_PI * 1500;
        EsavLF<double> eng; EsavLF<double>::Config cfg; cfg.k0 = 0.1 * wu * wu; cfg.kc = wu * wu; eng.prepare(lat, cfg, 1);
        std::mt19937_64 rng(9); std::uniform_real_distribution<double> U01(0, 1);
        SiteParams<double> Pn; Pn.resize(N);
        auto randp = [&](SiteParams<double>& P) { for (int i = 0; i < N; ++i) { P.m[i] = 0.5 + 1.5 * U01(rng); P.alpha[i] = cfg.kc * U01(rng); P.beta[i] = cfg.k0 * U01(rng); P.mu[i] = -10 * U01(rng); P.eta[i] = 0; P.g[i] = wu * (2 * U01(rng) - 1); } };
        randp(eng.params());
        std::vector<double> q0(2 * N), v0(2 * N);
        for (int k = 0; k < 2 * N; ++k) { q0[k] = 5 * (U01(rng) - 0.5); v0[k] = 1e5 * (U01(rng) - 0.5); }
        eng.set_state(q0, v0);
        double worst = -1e300; int scaled = 0; double u[1] = {0}, y[1];
        for (int t = 0; t < 2000; ++t) {
            for (int s = 0; s < 64; ++s) eng.process_sample(u, y);
            const double Eb = eng.energy(); randp(Pn); const double s = eng.enpu(Pn); const double Ea = eng.energy();
            scaled += s < 1; worst = std::max(worst, (Ea - std::max(Eb, 0.0)) / (std::abs(Eb) + 1e-300));
        }
        CHECK(worst <= 1e-12, "T3 ENPU 2000 random parameter jumps: max rel (E_after - max(E_before,0)) = %.2e (scaled in %d jumps)", worst, scaled);
    }
    // ---------- T4: energy-gated self-oscillation bounded; T5: zero allocation in the loop ----------
    {
        Honeycomb lat(8, 8); const int N = lat.N; const double wu = 2 * M_PI * 1500;
        EsavLF<double> eng; EsavLF<double>::Config cfg; cfg.k0 = 0.1 * wu * wu; cfg.kc = wu * wu; cfg.E_cap = 1e7; eng.prepare(lat, cfg, 2);
        auto& P = eng.params();
        for (int i = 0; i < N; ++i) { P.m[i] = 1; P.alpha[i] = 0; P.beta[i] = cfg.k0 * 0.5; P.mu[i] = 200; P.eta[i] = 0; P.g[i] = 0.6 * wu; }
        std::vector<double> q0(2 * N, 0.0), v0(2 * N, 0.0); v0[10] = 1.0; eng.set_state(q0, v0);
        Port<double> ports[2] = {{10, 1, 0}, {100, 0, 1}}; eng.set_ports(ports);
        ParamMap<double> map; map.base.resize(N); map.base.assign_from(P); map.mu_depth = 100; map.mu_max = 300; map.g_depth = 1.0;
        ControlLayer<double> ctl; ctl.prepare(lat, map);
        // seed a spiral in the medium (broken front) so parameters keep moving
        { auto u = ctl.medium.u_mut(); auto v = ctl.medium.v_mut(); for (int i = 0; i < N; ++i) { double x = lat.xy[i][0] - lat.xy[i][1] / std::sqrt(3.0), yy = lat.xy[i][1]; if (yy < 3.5 && x >= 2 && x < 3.5) u[i] = 1; if (yy < 3.5 && x < 2) v[i] = 0.5; } }
        double u[2] = {0, 0}, y[2]; double Emax = 0; const double h = 1.0 / cfg.fs;
        const long a0 = g_allocs.load();
        const int nS = 48000 * 4;
        for (int n = 0; n < nS; ++n) {
            eng.process_sample(u, y);
            if (n % 64 == 63) ctl.control_tick(eng);
            Emax = std::max(Emax, eng.tracked_energy());
        }
        const long a1 = g_allocs.load();
        // Thm. 4 bound with conservative constants
        const double mmin = 1.0, mu_max = 300, gam = eng.cfl_gamma(), del = h * mu_max / (mmin * (1 - gam * gam)), rho = (1 + del) / (1 - del);
        const double nd = 2.0 * N, Et_cap = cfg.E_cap + 0.5 * nd * 1.0;
        CHECK(Emax + 0.5 * nd <= rho * Et_cap, "T4 gated self-oscillation 4 s with spiral-modulated g/mu: max E~ = %.6e <= rho*E~cap = %.6e (rho=%.6f, gamma=%.3f)", Emax + 0.5 * nd, rho * Et_cap, rho, gam);
        CHECK(Emax > 0.5 * cfg.E_cap, "T4b oscillation actually grows to the cap region: max E = %.3e (cap %.1e)", Emax, cfg.E_cap);
        CHECK(a1 == a0, "T5 zero heap allocations in %d-sample process loop incl. ENPU + medium ticks (%ld allocs)", nS, a1 - a0);
        CHECK(ctl.medium.abs_charge() > 0, "T6 Barkley spiral alive after 4 s on 8x8 (|charge| = %d)", ctl.medium.abs_charge());
    }
    // ---------- T7: float build agrees with double on the power balance (single precision) ----------
    {
        Honeycomb lat(8, 8); const int N = lat.N; const double wu = 2 * M_PI * 1500;
        EsavLF<float> eng; EsavLF<float>::Config cfg; cfg.k0 = 0.1 * wu * wu; cfg.kc = wu * wu; eng.prepare(lat, cfg, 1);
        auto& P = eng.params(); for (int i = 0; i < N; ++i) { P.mu[i] = -2; P.g[i] = float(0.6 * wu); P.beta[i] = float(cfg.k0); }
        std::vector<float> q0(2 * N, 0.f), v0(2 * N, 0.f); v0[20] = 1000.f; eng.set_state(q0, v0);
        float u[1] = {0}, y[1]; const double E0 = eng.energy(); double Emax = E0; bool fin = true;
        for (int n = 0; n < 48000; ++n) { eng.process_sample(u, y); double E = eng.energy(); Emax = std::max(Emax, E); fin &= std::isfinite(E); }
        CHECK(fin && Emax <= E0 * (1 + 1e-4), "T7 float32 passive run 1 s: finite, max E/E0 = %.7f", Emax / E0);
    }
    std::printf("%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
