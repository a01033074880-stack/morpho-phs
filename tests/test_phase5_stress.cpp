// SPDX-License-Identifier: AGPL-3.0-only
// Phase 5b stress test for the worker-pool wake/sleep protocol (EsavLFMT): thousands of pool start/stop/reconfigure
// cycles, thread-count changes, dedicated and caller-computes pools, randomised host timing (no gap / short spin /
// sleep longer than idle_spin so the OS-wait path is exercised), tiny blocks (maximises wake-up races).
// A watchdog thread aborts with exit code 2 and a state dump if no block completes for WATCHDOG_S seconds.
// Build: g++ -std=c++20 -O3 -march=x86-64-v3 -fno-math-errno -ffp-contract=off -pthread -Iinclude tests/test_phase5_stress.cpp -o build/p5/test_phase5_stress
//        add -DSTRESS_HOOKS to inject random sleeps / yields at the 6 race-sensitive points of the protocol
//        (MORPHO_PHS_RACE_HOOK in esav_lf_mt.hpp), which turns ns-wide windows into 100 us-wide ones.
// Usage: test_phase5_stress [seconds_per_part=20] [seed=1]
// Phase 5c: P3 passes with no hang and an adaptive cycle count (see Part 2) and reports the per-cycle start/stop cost.
#ifdef STRESS_HOOKS
void stress_hook(int site) noexcept;
#define MORPHO_PHS_RACE_HOOK(site) stress_hook(site)
#endif
#include "morpho_phs/morpho_phs.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

#ifdef STRESS_HOOKS
#include <atomic>
static std::atomic<long long> g_hook_sleeps{0};
void stress_hook(int site) noexcept {
    thread_local std::minstd_rand r(unsigned(std::hash<std::thread::id>{}(std::this_thread::get_id())) + unsigned(site));
    const unsigned k = unsigned(r() % 256);
    if (k < 2) { g_hook_sleeps.fetch_add(1, std::memory_order_relaxed); std::this_thread::sleep_for(std::chrono::microseconds(1 + r() % 300)); }
    else if (k < 24) std::this_thread::yield();
}
#endif
using namespace morpho_phs;
static const double WU = 2 * M_PI * 1500;
static std::atomic<long long> g_blocks{0}, g_cycles{0};
static std::atomic<int> g_phase{0}, g_T{0}, g_caller{0}, g_pin{0};
static std::atomic<long> g_idle_ns{0};
static std::atomic<bool> g_done{false};
static constexpr int WATCHDOG_S = 10;
static int fails = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); std::printf("[%s] ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); std::fflush(stdout); if (!ok_) ++fails; } while (0)

static EsavLFMT<float>::Config mk(int T, int caller, int pin, long idle_ns) {
    EsavLFMT<float>::Config c; c.k0 = 0.1 * WU * WU; c.kc = WU * WU; c.potential = Potential::Quartic; c.E_cap = 2e5;
    c.threads = T; c.caller_computes = caller; c.pin = pin; c.mmcss = false; c.interp_step = 1; c.ftz_workers = true;
    c.idle_spin_ns = idle_ns; c.idle_yield_ns = idle_ns + (idle_ns ? 20000 : 0);
    return c;
}
static void init(EsavLFMT<float>& e, const Honeycomb& lat, const EsavLFMT<float>::Config& c) {
    e.prepare(lat, c, 2);
    auto& P = e.params();
    for (int i = 0; i < lat.N; ++i) { P.m[i] = 1; P.alpha[i] = float(WU * WU * 0.02); P.beta[i] = 0; P.mu[i] = -1; P.eta[i] = 1e-4f; P.g[i] = float(0.6 * WU); }
    Port<float> pp[2] = {{1, 1, 0}, {lat.N - 2, 0, 1}};
    e.set_ports(pp);
}
// host gap: 0 = none, 1 = short busy spin (< idle spin), 2 = sleep (> idle spin -> workers / caller take the OS-wait path)
static void gap(std::mt19937& rng) {
    const int k = int(rng() % 8);
    if (k < 4) return;
    if (k < 7) { const auto t0 = std::chrono::steady_clock::now(); const auto d = std::chrono::nanoseconds(500 + rng() % 30000); while (std::chrono::steady_clock::now() - t0 < d) {} return; }
    std::this_thread::sleep_for(std::chrono::microseconds(50 + rng() % 400));
}

int main(int argc, char** argv) {
    const double secs = argc > 1 ? std::atof(argv[1]) : 20.0;
    const unsigned seed = argc > 2 ? unsigned(std::atoi(argv[2])) : 1u;
    const int hw = int(std::max(2u, std::thread::hardware_concurrency()));
#ifdef STRESS_HOOKS
    const char* hooks = "ON (random sleeps/yields at 6 protocol points)";
#else
    const char* hooks = "off";
#endif
    std::printf("stress: %.0f s per part, seed %u, %d CPUs, watchdog %d s, race hooks %s\n", secs, seed, hw, WATCHDOG_S, hooks); std::fflush(stdout);
    std::thread wd([&] {
        long long last = -1; auto t_last = std::chrono::steady_clock::now();
        while (!g_done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const long long b = g_blocks.load() + g_cycles.load();
            if (b != last) { last = b; t_last = std::chrono::steady_clock::now(); continue; }
            if (std::chrono::steady_clock::now() - t_last > std::chrono::seconds(WATCHDOG_S)) {
                std::printf("[FAIL] HANG: no progress for %d s  part=%d T=%d caller=%d pin=%d idle_spin_ns=%ld blocks=%lld cycles=%lld\n", WATCHDOG_S,
                            g_phase.load(), g_T.load(), g_caller.load(), g_pin.load(), g_idle_ns.load(), g_blocks.load(), g_cycles.load());
                std::fflush(stdout); std::_Exit(2);
            }
        }
    });
    std::mt19937 rng(seed);
    const Honeycomb small(6, 6), mid(16, 16);
    std::vector<float> u(64 * 2, 0.5f), y(64 * 2);
    // ---- Part 1: long-lived pools, tiny blocks (1..8 samples), random gaps, short idle spin: wake-up races ----
    for (int caller : {0, 1}) {
        g_phase = 1 + caller;
        long long nb = 0, osw = 0; bool finite = true;
        const auto t_end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs / 2);
        int cfgs = 0;
        while (std::chrono::steady_clock::now() < t_end) {
            const int T = 2 + int(rng() % std::min(3, hw - 1)); const long idle = (rng() % 3 == 0) ? 0 : long(rng() % 20000);
            g_T = T; g_caller = caller; g_pin = 0; g_idle_ns = idle;
            EsavLFMT<float> e; init(e, small, mk(T, caller, 0, idle)); ++cfgs;
            for (int b = 0; b < 20000 && std::chrono::steady_clock::now() < t_end; ++b) {
                e.process_block(u.data(), y.data(), 1 + int(rng() % 8)); ++nb; g_blocks.fetch_add(1, std::memory_order_relaxed);
                if (rng() % 16 == 0) gap(rng);
            }
            osw += e.idle_os_waits(); finite = finite && std::isfinite(e.tracked_energy());
        }
        CHECK(finite, "P%d %s pool, tiny blocks: %lld blocks over %d pool configs (T=2..4, idle_spin 0..20 us), %lld OS waits exercised, no hang",
              1 + caller, caller ? "caller-computes" : "dedicated", nb, cfgs, osw);
    }
    // ---- Part 2: reconfigure storm: prepare() with random T / pool kind / pin mode, few blocks per config ----
    {
        g_phase = 3;
        long long cyc = 0, nb = 0; bool finite = true;
        std::vector<double> t_cycle, t_restart;   // Phase 5c: per-cycle cost (whole cycle / init() = stop + restart only)
        t_cycle.reserve(1 << 16); t_restart.reserve(1 << 16);
        EsavLFMT<float> e;
        const auto t_begin = std::chrono::steady_clock::now();
        const auto t_end = t_begin + std::chrono::duration<double>(secs);
        while (std::chrono::steady_clock::now() < t_end) {
            const auto c0 = std::chrono::steady_clock::now();
            const int T = 1 + int(rng() % std::min(6, hw)); const int caller = int(rng() % 3) - 1; const int pin = (rng() % 2) ? 0 : 3;
            const long idle = (rng() % 2) ? long(rng() % 50000) : 3500000;
            g_T = T; g_caller = caller; g_pin = pin; g_idle_ns = idle;
            if (rng() % 4 == 0) { EsavLFMT<float> f; init(f, (rng() % 2) ? small : mid, mk(T, caller, pin, idle)); f.process_block(u.data(), y.data(), 16); }   // construct / destroy
            const auto r0 = std::chrono::steady_clock::now();
            init(e, (rng() % 2) ? small : mid, mk(T, caller, pin, idle));   // stop + restart the pool of a live engine
            t_restart.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - r0).count());
            const int n = int(rng() % 12);
            for (int b = 0; b < n; ++b) { e.process_block(u.data(), y.data(), 1 + int(rng() % 64)); ++nb; g_blocks.fetch_add(1); if (rng() % 3 == 0) gap(rng); }
            finite = finite && std::isfinite(e.tracked_energy());
            ++cyc; g_cycles.fetch_add(1);
            t_cycle.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count());
        }
        // Phase 5c pass rule.  A hang is caught by the watchdog (exit 2 after 10 s without progress -- unchanged, strict).
        // The fixed ">= 1000 cycles" bar is gone: one start/stop costs ~10-60 ms on Windows (thread creation + MMCSS
        // registration; i9-13900H: 256 cycles in 15 s) but ~0.1-1 ms on Linux, so it measured the OS, not the protocol.
        // Now: at least min(100, 10 per second of budget) cycles (coverage), and no single cycle slower than 2 s (a
        // stall well below the watchdog limit still fails).  The per-cycle cost is reported.
        auto q = [](std::vector<double> v, double p) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, std::size_t(p * double(v.size())))]; };
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_begin).count();
        const double med = q(t_cycle, 0.5), p95 = q(t_cycle, 0.95), mx = q(t_cycle, 1.0), mean = cyc > 0 ? el / double(cyc) : 0.0;
        const double rmed = q(t_restart, 0.5), rp95 = q(t_restart, 0.95), rmx = q(t_restart, 1.0);
        const long long need = std::min<long long>(100, (long long)(10.0 * secs));
        constexpr double kStallS = 2.0;
        CHECK(finite && cyc >= need && mx < kStallS,
              "P3 reconfigure storm: %lld start/stop cycles in %.1f s (need >= %lld and every cycle < %.0f s) "
              "(T=1..6, caller/dedicated/auto, pin none/cpuset, idle 0..50 us or 3.5 ms), %lld blocks, no hang; "
              "cycle cost mean %.2f / median %.2f / p95 %.2f / max %.2f ms; stop+restart (init) median %.2f / p95 %.2f / max %.2f ms",
              cyc, el, need, kStallS, nb, 1e3 * mean, 1e3 * med, 1e3 * p95, 1e3 * mx, 1e3 * rmed, 1e3 * rp95, 1e3 * rmx);
    }
    // ---- Part 3: plugin-like: 32x32, block 128, T 2<->3<->4 switches between bursts, paced with sleeps ----
    {
        g_phase = 4;
        long long nb = 0; int sw = 0; bool finite = true;
        const Honeycomb big(32, 32); std::vector<float> U(128 * 2, 0.1f), Y(128 * 2);
        EsavLFMT<float> e;
        const auto t_end = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs / 2);
        while (std::chrono::steady_clock::now() < t_end) {
            const int T = 2 + int(rng() % std::min(3, hw - 1)), caller = int(rng() % 2);
            g_T = T; g_caller = caller; g_pin = 3; g_idle_ns = 3500000;
            init(e, big, mk(T, caller, 3, 3500000)); ++sw;
            for (int b = 0; b < 50; ++b) { e.process_block(U.data(), Y.data(), 128); ++nb; g_blocks.fetch_add(1); if (rng() % 4 == 0) std::this_thread::sleep_for(std::chrono::microseconds(rng() % 5000)); }
            finite = finite && std::isfinite(e.tracked_energy());
        }
        CHECK(finite, "P4 plugin-like 32x32 b128, %d pool switches T=2..4 caller/dedicated, %lld blocks, sleeps up to 5 ms, no hang", sw, nb);
    }
    g_done = true; wd.join();
#ifdef STRESS_HOOKS
    std::printf("race-hook sleeps injected: %lld\n", g_hook_sleeps.load());
#endif
    std::printf("%s: %d failure(s)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
