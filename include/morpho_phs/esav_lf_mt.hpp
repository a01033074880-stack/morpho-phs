// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Multithreaded ESAV-LF (Phase 2).  Same scheme, energy and ENPU as esav_lf.hpp (docs/theory_v1.md Def. 3,
// Thm. 1-4), restructured for a deterministic, lock-free, allocation-free parallel audio kernel:
//
//  * Gather form of K q: every site computes its own (K q)_i from its <= 3 neighbours (padded CSR), so there are
//    no write conflicts.  The ESAV-LF update of site i reads q^n of its neighbours and writes only site i, so
//    one fused pass per sample suffices; q^{n+1} overwrites q^{n-1} in place and the (q, q_prev) and
//    (psi, psi_prev) buffers swap by pointer after ONE barrier per sample.
//  * Static domain decomposition: sites are split into fixed chunks of kChunk sites; thread t owns a contiguous
//    range of chunks.  Port forces/outputs are handled by the owner of the port site (no extra barrier).
//  * Deterministic reductions: dissipation / input power are summed per chunk (fixed 8-lane order) and the
//    chunk partials are summed in chunk order redundantly by every thread.  Hence results are BIT-IDENTICAL for
//    any thread count (tested), and every thread holds the same energy tracker -> same gate decision.
//  * Threads are created in prepare(); process_block() never allocates, never locks.  Workers spin on an
//    atomic sequence number for `spin_iters` polls, then block in std::atomic::wait (no mutex).  The calling
//    (audio) thread is worker 0.  A sense-reversing atomic barrier synchronises sub-steps.
//  * energy()/enpu()/set_potential() run on the caller between blocks (workers idle) — control rate only.
//
// Phase 4 (real-time robustness on hybrid CPUs, see rt_thread.hpp):
//  * Hybrid barrier: PAUSE-spin for barrier_spin_ns, then yield, then block (futex / WaitOnAddress).  A worker that
//    is pre-empted (or parked on an E-core) no longer makes the others burn their cores for a whole OS quantum —
//    the cause of the 40-100 ms block_max spikes measured at 8 threads.  The waker skips the OS wake when nobody sleeps.
//  * Time-based idle wait between blocks (idle_spin_ns / idle_yield_ns) instead of an iteration count.
//  * Optional dedicated pool (caller_computes = 0): all T compute threads are pool threads that can be pinned; the
//    caller (host audio thread, whose core we do not control) only publishes the job and waits for completion.
//  * Optional affinity (pin = 1: one worker per physical P-core, SMT siblings skipped; 2: P-core set) and Windows
//    MMCSS "Pro Audio" for the workers (mmcss = true; no-op elsewhere).  Failures fall back silently (pin_report()).
//  * Per-thread cache-line padded chunk-partial buffers (no false sharing between neighbouring threads' last/first
//    chunk).  The summation order over chunks is unchanged -> still bit-identical for any thread count / mode.
//
// Phase 5 (docs/expert_review_phase4.md, triage in README section 3.6):
//  * Bipartite stencil kernel (kernel = 1, default): K q from FIXED offsets of OUR interleaved honeycomb indexing
//    (honeycomb.hpp: site = 2*cell + s, A = even, B = odd).  A(cx,cy) couples to B(cx,cy), B(cx-1,cy), B(cx,cy-1);
//    B(cx,cy) to A(cx,cy), A(cx+1,cy), A(cx,cy+1).  In site indices, with o2 = 2*Lx - 1:
//        slot 0 (intra-cell):  A: i+1   B: i-1      -> lane swap of adjacent pairs
//        slot 1 (horizontal):  A: i-1   B: i+1      -> blend(load(i-1), load(i+1))
//        slot 2 (vertical):    A: i-o2  B: i+o2     -> blend(load(i-o2), load(i+o2))
//    so 8 (f32) / 4 (f64) sites are processed per AVX2 instruction with no gather.  Per-site per-slot stiffness
//    arrays (kc * n n^T of the bond, ZERO where the bond is missing: open boundary / masks) replace the branches;
//    the q buffers carry zero guard bands so out-of-lattice reads are finite and multiplied by 0.  The slot order
//    equals the Phase-2..4 CSR fill order (missing bonds only add exact zeros) and every operation is a separate IEEE
//    op in the same order (simd.hpp, no FMA, -ffp-contract=off) -> BIT-IDENTICAL to the CSR kernel (kernel = 0, kept
//    as reference; tests/test_phase5.cpp).
//  * In-block per-sample barrier = rt::SpinBarrier (pure PAUSE spin, never an OS wait).  OS waits only between
//    blocks (idle_spin_ns, default 3.5 ms) and, for a dedicated pool, for the caller after idle_spin_ns.
//    inblock_os_wait = true restores the Phase-4 hybrid barrier (reference / oversubscription test only).
//  * PinMode::CpuSet, clean P-core list (no core 0, no SMT sibling), EcoQoS opt-out of workers and caller.
//  * Optional E-RSAV relaxation (ersav = true, default OFF): psi^{n+1} = sgn(psi~) min(|psi~|, sqrt(2U(q^{n+1})+c0)).
//    The removed SAV energy 1/4 (psi~^2 - psi^2) >= 0 is booked as dissipation (pd), so the discrete energy
//    identity stays exact and passivity is preserved (energy can only go down).
#include "esav_lf.hpp"
#include "rt_thread.hpp"
// Test-only hook to widen race windows in the wake/sleep protocol (tests/test_phase5_stress.cpp defines it to a
// random sleep).  Empty in every product build.
#ifndef MORPHO_PHS_RACE_HOOK
#define MORPHO_PHS_RACE_HOOK(site) ((void)0)
#endif
#include "simd.hpp"
#include <type_traits>
#include <atomic>
#include <memory>
#include <new>
#include <thread>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define MORPHO_CPU_RELAX() _mm_pause()
#else
#define MORPHO_CPU_RELAX() ((void)0)
#endif

namespace morpho_phs {

// Flush-to-zero + denormals-are-zero for the calling thread (no-op on non-x86).
inline void enable_ftz_daz() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040u);
#endif
}

class SpinBarrier {
public:
    void init(int n) noexcept { n_ = n; count_.store(0); gen_.store(0); }
    void arrive_and_wait() noexcept {
        const unsigned g = gen_.load(std::memory_order_acquire);
        if (count_.fetch_add(1, std::memory_order_acq_rel) == n_ - 1) {
            count_.store(0, std::memory_order_relaxed);
            gen_.store(g + 1, std::memory_order_release);
        } else {
            while (gen_.load(std::memory_order_acquire) == g) MORPHO_CPU_RELAX();
        }
    }
private:
    alignas(64) std::atomic<int> count_{0};
    alignas(64) std::atomic<unsigned> gen_{0};
    int n_ = 1;
};

template <class T>
struct AlignedBuf {   // 64-byte aligned, allocated once in prepare()
    std::unique_ptr<T[], void (*)(T*)> p{nullptr, [](T* x) { ::operator delete[](x, std::align_val_t(64)); }};
    std::size_t n = 0;
    void assign(std::size_t m, T v) {
        if (m != n) { p.reset(static_cast<T*>(::operator new[](m * sizeof(T), std::align_val_t(64)))); n = m; }
        for (std::size_t i = 0; i < m; ++i) p[i] = v;
    }
    T* data() noexcept { return p.get(); }
    const T* data() const noexcept { return p.get(); }
    T& operator[](std::size_t i) noexcept { return p[i]; }
    const T& operator[](std::size_t i) const noexcept { return p[i]; }
};

template <RealType R>
class EsavLFMT {
public:
    static constexpr int kChunk = 32;
    struct Config {
        double fs = 48000.0, k0 = 0.0, kc = 1.0, c0 = 1.0;
        double E_cap = std::numeric_limits<double>::infinity();
        Potential potential = Potential::Quartic;     // Phase-2 default (user decision); QuarticLogcosh = "HQ"
        int threads = 1;                               // including the calling thread
        long spin_iters = 200000;                      // Phase 2/3 (iteration-count idle spin); ignored since Phase 4
        // Phase 4 threading (rt_thread.hpp).  Defaults reproduce "caller computes, no pinning" with the hybrid waits.
        int pin = 0;                                   // rt::PinMode: 0 none, 1 one worker per physical P-core, 2 P-core set
        bool mmcss = false;                            // Windows: register workers with MMCSS "Pro Audio" (HIGH)
        int caller_computes = -1;                      // 1: caller is compute thread 0; 0: dedicated pool; -1: auto (= pin == 0)
        long barrier_spin_ns = 50000, barrier_yield_ns = 200000;   // per-sample barrier: spin, then yield, then block
        // workers between blocks: spin, then yield, then block.  Phase 5 default: 3.5 ms spin (> one 128-sample period
        // at 48 kHz), then a short yield, then the OS wait.  This is the ONLY place where workers may block.
        long idle_spin_ns = 3500000, idle_yield_ns = 3600000;
        // Adaptive spin (Phase 4): the caller measures the host's block period and its own wait for the pool (EMA) and
        // lets workers spin through the gap between blocks (1.25 x period) and itself spin for the pool (1.5 x busy),
        // both capped at spin_max_ns.  Measured on the box: sleeping between paced blocks cost ~40 % (wake-up latency
        // + cold caches), spinning through the gap removes it.  If the host stops calling, everyone blocks after the cap.
        bool adaptive_spin = false;      // Phase 4 default true; Phase 5: fixed idle_spin_ns (predictable)
        long spin_max_ns = 10000000;
        // Dedicated pool: should the (host) caller also spin for the whole block (1.5 x busy EMA) while it waits?
        // Default off since the Linux contention bench: a spinning caller occupies one more CPU, and as soon as
        // workers + caller + other load exceed the CPUs the per-sample barrier collapses (box, 3 busy loops, 5 pinned
        // workers: ~170 us/sample).  Off = caller spins barrier_spin_ns, then blocks; costs one OS wake-up per *block*.
        bool caller_pool_spin = false;
        // Phase 3: g / mu written between blocks (ENPU-C fast path or params()) are reached by a staircase ramp
        // inside the next block, stepping every `interp_step` samples (0 = jump at block start = Phase-2 behaviour).
        // g and mu do not enter the discrete energy, so the ramp is energetically free (Thms 1-4 unaffected).
        int interp_step = 0;
        // Phase 3: set FTZ/DAZ (x86 MXCSR) in the worker threads.  The caller (audio thread) should run with the same
        // mode (JUCE ScopedNoDenormals / morpho_phs::enable_ftz_daz()) to keep results identical across thread counts.
        bool ftz_workers = false;
        // ---- Phase 5 ----
        int kernel = 1;                  // 0: Phase-2..4 padded-CSR gather (reference), 1: bipartite SIMD stencil
        bool ersav = false;              // E-RSAV psi relaxation (see header comment); default OFF
        bool inblock_os_wait = false;    // false: pure-spin per-sample barrier (Phase 5); true: Phase-4 hybrid barrier
        long barrier_rescue_ns = 1000000;  // pure-spin barrier: start yielding only after ONE wait > 1 ms (oversubscription; counted)
        bool no_power_throttling = true; // EcoQoS opt-out (Windows): workers at start, caller thread once
    };

    EsavLFMT() = default;
    EsavLFMT(const EsavLFMT&) = delete;
    EsavLFMT& operator=(const EsavLFMT&) = delete;
    ~EsavLFMT() { stop_workers(); }

    void prepare(const Honeycomb& lat, const Config& cfg, std::size_t max_ports) {
        stop_workers();
        lat_ = &lat; cfg_ = cfg; N_ = lat.N; h_ = R(1.0 / cfg.fs);
        nchunks_ = (N_ + kChunk - 1) / kChunk; Np_ = nchunks_ * kChunk;
        // q buffers carry zero guard bands of G elements on both sides (stencil reads up to i -/+ (2 Lx - 1) + W)
        o2_ = 2 * lat.Lx - 1; G_ = (2 * lat.Lx + 16 + 15) / 16 * 16;
        for (auto* v : {&qa_x, &qa_y, &qb_x, &qb_y}) v->assign(std::size_t(Np_ + 2 * G_), R(0));
        for (auto* v : {&pa_x, &pa_y, &pb_x, &pb_y, &fx_, &fy_, &pd_, &pi_, &vbx_, &vby_, &gxs_, &gys_, &g0_, &mu0_}) v->assign(Np_, R(0));
        ramp_valid_ = false;
        nb_.assign(std::size_t(Np_) * 3, 0); cxx_.assign(std::size_t(Np_) * 3, R(0)); cxy_.assign(std::size_t(Np_) * 3, R(0)); cyy_.assign(std::size_t(Np_) * 3, R(0));
        for (int i = 0; i < Np_; ++i) for (int k = 0; k < 3; ++k) nb_[3 * i + k] = std::min(i, N_ - 1);
        std::vector<int> fill(N_, 0);
        for (std::size_t b = 0; b < lat.bonds.size(); ++b) {
            const int i = lat.bonds[b][0], j = lat.bonds[b][1];
            for (auto [s, o] : {std::pair{i, j}, std::pair{j, i}}) {
                const int k = 3 * s + fill[s]++;
                nb_[k] = o; cxx_[k] = R(cfg.kc * lat.nn[b][0]); cxy_[k] = R(cfg.kc * lat.nn[b][1]); cyy_[k] = R(cfg.kc * lat.nn[b][2]);
            }
        }
        for (int i = 0; i < N_; ++i) for (int k = fill[i]; k < 3; ++k) nb_[3 * i + k] = i;   // padded: zero coeff, self
        // Phase 5 stencil tables: sc_[3*slot + comp][site], comp = xx, xy, yy; zero where the bond is missing.
        for (auto& a : sc_) a.assign(Np_, R(0));
        stencil_ok_ = true;
        for (std::size_t b = 0; b < lat.bonds.size(); ++b) {
            const int i = lat.bonds[b][0], j = lat.bonds[b][1];
            const int ci = i / 2, cj = j / 2, dcx = ci % lat.Lx - cj % lat.Lx, dcy = ci / lat.Lx - cj / lat.Lx;
            const int slot = (dcx == 0 && dcy == 0) ? 0 : ((dcx == 1 && dcy == 0) ? 1 : ((dcx == 0 && dcy == 1) ? 2 : -1));
            if ((i & 1) != 0 || (j & 1) != 1 || slot < 0) { stencil_ok_ = false; continue; }   // not our A->B bond layout
            const R xx = R(cfg.kc * lat.nn[b][0]), xy = R(cfg.kc * lat.nn[b][1]), yy = R(cfg.kc * lat.nn[b][2]);
            for (int s : {i, j}) { sc_[3 * slot][s] = xx; sc_[3 * slot + 1][s] = xy; sc_[3 * slot + 2][s] = yy; }
        }
        kernel_ = (cfg.kernel == 1 && stencil_ok_) ? 1 : 0;
        P_.resize(N_);
        ports_.assign(max_ports, Port<R>{}); nports_ = 0;
        q_x = qa_x.data() + G_; q_y = qa_y.data() + G_; qm_x = qb_x.data() + G_; qm_y = qb_y.data() + G_;
        p_x = pa_x.data(); p_y = pa_y.data(); pm_x = pb_x.data(); pm_y = pb_y.data();
        reset_psi(); E_ = double(energy());
        start_workers(std::max(1, cfg.threads));
    }

    SiteParams<R>& params() noexcept { return P_; }
    const SiteParams<R>& params() const noexcept { return P_; }
    void set_ports(std::span<const Port<R>> p) noexcept { nports_ = std::min(p.size(), ports_.size()); std::copy_n(p.begin(), nports_, ports_.begin()); }
    std::size_t num_ports() const noexcept { return nports_; }
    int threads() const noexcept { return T_; }
    bool caller_computes() const noexcept { return caller_computes_; }
    // e.g. "pin=pcores mmcss=0 pool=dedicated cpus=[5,4,3] ok=3/3 topo=sysfs P=6/14 hybrid=1"
    const std::string& pin_report() const noexcept { return pin_report_; }
    // OS waits inside a block (per-sample barrier).  Phase 5 default (pure spin): always 0.
    long long barrier_blocked_waits() const noexcept { return use_hybrid_ ? barrier_.blocked_waits() : spin_barrier_.blocked_waits(); }
    // OS waits between blocks (workers' idle wait, dedicated-pool caller) — allowed; diagnostics only.
    long long idle_os_waits() const noexcept { return idle_os_waits_.load(std::memory_order_relaxed); }
    int kernel() const noexcept { return kernel_; }   // 0 CSR, 1 stencil (active, after the layout check)
    int ecoqos_off_ok() const noexcept { return ecoqos_ok_.load(); }
    int pinned_ok() const noexcept { return pinned_ok_.load(); }
    int mmcss_ok() const noexcept { return mmcss_ok_.load(); }
    Potential potential() const noexcept { return cfg_.potential; }
    double cfl_gamma() const noexcept {
        R mmin = *std::min_element(P_.m.begin(), P_.m.end());
        return 0.5 * double(h_) * std::sqrt(lat_->lambda_max_bound(cfg_.k0, cfg_.kc) / double(mmin));
    }
    void set_state(std::span<const R> q0, std::span<const R> v0) noexcept {
        for (int i = 0; i < N_; ++i) {
            q_x[i] = q0[2 * i]; q_y[i] = q0[2 * i + 1];
            qm_x[i] = q0[2 * i] - h_ * v0[2 * i]; qm_y[i] = q0[2 * i + 1] - h_ * v0[2 * i + 1];
        }
        reset_psi(); E_ = double(energy());
    }
    void clear() noexcept {
        for (int i = 0; i < N_; ++i) { q_x[i] = q_y[i] = qm_x[i] = qm_y[i] = 0; }
        reset_psi(); E_ = double(energy());
    }
    R q_at(int dof) const noexcept { return (dof & 1) ? q_y[dof >> 1] : q_x[dof >> 1]; }
    // lagged velocity (q^n - q^{n-1})/h of a DOF; call only between blocks
    R v_at(int dof) const noexcept { return ((dof & 1) ? (q_y[dof >> 1] - qm_y[dof >> 1]) : (q_x[dof >> 1] - qm_x[dof >> 1])) / h_; }

    // Process n samples.  u: n*num_ports inputs (sample-major), y: n*num_ports outputs.  Returns sum h*u.y.
    double process_block(const R* u, R* y, int n) noexcept {
        if (n <= 0) return 0.0;
        if (cfg_.no_power_throttling) {   // once per calling (audio) thread
            static thread_local bool qos_done = false;
            if (!qos_done) { qos_done = true; if (rt::disable_power_throttling()) caller_qos_ok_.store(true); }
        }
        job_u_ = u; job_y_ = y; job_n_ = n; job_win_ = 0.0;
        job_snap_ = !ramp_valid_ || cfg_.interp_step <= 0; ramp_valid_ = true;
        if (pool_ == 0) { run_block(0); return job_win_; }   // single thread, caller computes
        const auto t_call = rt::Clock::now();
        if (cfg_.adaptive_spin) adapt_period(t_call);
        const unsigned s = seq_.fetch_add(1, std::memory_order_seq_cst) + 1;
        MORPHO_PHS_RACE_HOOK(3);
        if (idle_sleepers_.load(std::memory_order_seq_cst) > 0) seq_.notify_all();
        MORPHO_PHS_RACE_HOOK(4);
        if (caller_computes_) { run_block(0); return job_win_; }
        const long dspin = (cfg_.adaptive_spin && cfg_.caller_pool_spin) ? std::clamp(long(1.5 * ema_busy_ns_), cfg_.barrier_spin_ns, std::max(cfg_.barrier_spin_ns, cfg_.spin_max_ns))
                                              : cfg_.barrier_spin_ns;
        // Phase 5: the caller of a dedicated pool spins for up to idle_spin_ns (covers the whole block) before an OS wait.
        const rt::WaitPolicy w = cfg_.inblock_os_wait ? rt::WaitPolicy{dspin, dspin + (cfg_.barrier_yield_ns - cfg_.barrier_spin_ns)}
                                                      : rt::WaitPolicy{std::max(dspin, cfg_.idle_spin_ns), std::max(dspin, cfg_.idle_spin_ns)};
        // Phase 5b fix (i9 hang, 2026-10-31): load done_ ONCE per iteration and wait on exactly the value that was
        // compared.  The old code compared one load (!= s) and passed a SECOND load as the "old" value; if compute
        // thread 0 published s between the two loads (likely on Windows: the woken MMCSS worker preempts the caller
        // right after notify_all), the caller waited for done_ to leave s -> slept forever, workers idle = deadlock.
        for (unsigned d = done_.load(std::memory_order_acquire); d != s; d = done_.load(std::memory_order_acquire)) {
            MORPHO_PHS_RACE_HOOK(1);
            if (rt::hybrid_wait(done_, d, w, &done_sleepers_)) idle_os_waits_.fetch_add(1, std::memory_order_relaxed);
        }
        if (cfg_.adaptive_spin) {
            const double busy = double(std::chrono::duration_cast<std::chrono::nanoseconds>(rt::Clock::now() - t_call).count());
            ema_busy_ns_ = ema_busy_ns_ <= 0 ? busy : ema_busy_ns_ + 0.1 * (busy - ema_busy_ns_);
        }
        return job_win_;
    }
    R process_sample(std::span<const R> u, std::span<R> y) noexcept { return R(process_block(u.data(), y.data(), 1)); }
    long idle_spin_now_ns() const noexcept { return idle_spin_dyn_.load(std::memory_order_relaxed); }
    // g/mu staircase ramp (Phase 3).  snap_ramp(): next block jumps to the current g/mu (no ramp from stale values).
    void set_interp_step(int s) noexcept { cfg_.interp_step = std::max(0, s); }
    void snap_ramp() noexcept { ramp_valid_ = false; }

    R energy() const noexcept {
#if MORPHO_SIMD_AVX2
        if (stencil_ok_ && kernel_ == 1) return energy_stencil();   // Phase 5: same value bit for bit, ~4x faster
#endif
        return energy_csr();
    }
    R energy_csr() const noexcept {
        double kin = 0, pot = 0, sav = 0; const double ih = 1.0 / double(h_), k0 = cfg_.k0;
        for (int i = 0; i < N_; ++i) {
            double kx = k0 * qm_x[i], ky = k0 * qm_y[i];
            for (int k = 0; k < 3; ++k) {
                const int j = nb_[3 * i + k]; const double dx = double(qm_x[i]) - qm_x[j], dy = double(qm_y[i]) - qm_y[j];
                kx += double(cxx_[3 * i + k]) * dx + double(cxy_[3 * i + k]) * dy; ky += double(cxy_[3 * i + k]) * dx + double(cyy_[3 * i + k]) * dy;
            }
            const double vx = (double(q_x[i]) - qm_x[i]) * ih, vy = (double(q_y[i]) - qm_y[i]) * ih;
            kin += 0.5 * double(P_.m[i]) * (vx * vx + vy * vy);
            pot += 0.5 * (double(q_x[i]) * kx + double(q_y[i]) * ky);
            sav += 0.25 * (double(p_x[i]) * p_x[i] + double(pm_x[i]) * pm_x[i] + double(p_y[i]) * p_y[i] + double(pm_y[i]) * pm_y[i]) - cfg_.c0;
        }
        return R(kin + pot + sav);
    }

    // ENPU (Thm. 3), variant A "reproject": psi re-projected, state scaled if needed.  Returns s.
    R enpu(const SiteParams<R>& Pn) noexcept { return enpu_impl(Pn, cfg_.potential, true); }
    // ENPU variant B "keep-state": q and psi are left untouched and only the parameters are swapped.  The discrete
    // energy depends on the parameters only through the kinetic term 1/2 sum m_i |v_i|^2, so changes of
    // g, mu, eta, alpha, beta leave E EXACTLY unchanged (psi then carries the old potential energy and relaxes
    // through the dynamics: SAV inconsistency instead of rescaling).  If a mass change would raise E above
    // max(E_old,0), variant A is applied instead (counted in enpu_fallback_).  Either way E_new <= max(E_old,0).
    R enpu_keep_psi(const SiteParams<R>& Pn) noexcept { return enpu_impl(Pn, cfg_.potential, false); }
    // ENPU variant C "selective": only sites whose parameters changed are touched: v rescaled by sqrt(m/m') where m
    // changed, psi re-projected where (m, alpha, beta) changed; all other sites keep (q, psi) exactly.  Then, as in
    // Thm. 3, a global scaling s <= 1 is applied iff E would exceed max(E_old,0).  g/mu/eta-only updates are exact no-ops
    // on the state; alpha/beta updates leave no SAV inconsistency (unlike B).
    R enpu_selective(const SiteParams<R>& Pn) noexcept { return enpu_impl(Pn, cfg_.potential, false, true); }
    // Switch potential family (e.g. Quartic <-> QuarticLogcosh "HQ") passively: treated as a parameter update.
    R set_potential(Potential p) noexcept { return enpu_impl(P_, p, true); }

    // SAV consistency diagnostic: sum_d |psi_d^2 - (2U(q_d)+c0)| / max(sum_d 2U(q_d), tiny) at the current level
    double sav_inconsistency() const noexcept {
        double num = 0, den = 0; const double c0 = cfg_.c0;
        for (int i = 0; i < N_; ++i) {
            const double ux = 2.0 * double(U(q_x[i], i)), uy = 2.0 * double(U(q_y[i], i));
            num += std::abs(double(p_x[i]) * p_x[i] - ux - c0) + std::abs(double(p_y[i]) * p_y[i] - uy - c0); den += ux + uy;
        }
        return num / std::max(den, 1e-300);
    }
    double tracked_energy() const noexcept { return E_; }
    double resync_energy() noexcept { E_ = double(energy()); return E_; }
    double last_dissipation() const noexcept { return last_pdis_; }
    R h() const noexcept { return h_; }
    const Config& config() const noexcept { return cfg_; }

private:
    R enpu_impl(const SiteParams<R>& Pn, Potential newpot, bool reproject, bool selective = false) noexcept {
        if (selective && newpot == cfg_.potential) {
            // fast path: only g / mu / eta changed -> the discrete energy is unchanged bit-for-bit (it depends on the
            // parameters only through m, alpha, beta), so no energy evaluation is needed: O(N) copies.  This makes
            // per-sample (or sub-block) interpolation of g / mu affordable.
            bool energetic = false;
            for (int i = 0; i < N_ && !energetic; ++i) energetic = (Pn.m[i] != P_.m[i]) || (Pn.alpha[i] != P_.alpha[i]) || (Pn.beta[i] != P_.beta[i]);
            if (!energetic) {
                for (int i = 0; i < N_; ++i) { P_.mu[i] = Pn.mu[i]; P_.eta[i] = Pn.eta[i]; P_.g[i] = Pn.g[i]; }
                ++enpu_calls_; return R(1);
            }
        }
        const double E_old = double(energy());
        if (selective && newpot == cfg_.potential) {
            // variant C: per site, rescale v only where m changed, re-project psi only where (m,alpha,beta) changed;
            // untouched sites keep (q, psi) bit-for-bit.  Global scaling below only if E would rise.
            const R hh = R(0.5) * h_;
            for (int i = 0; i < N_; ++i) {
                if (Pn.m[i] != P_.m[i]) {
                    const R r = std::sqrt(P_.m[i] / Pn.m[i]);
                    const R bx = R(0.5) * (q_x[i] + qm_x[i]), vx = (q_x[i] - qm_x[i]) / h_ * r;
                    const R by = R(0.5) * (q_y[i] + qm_y[i]), vy = (q_y[i] - qm_y[i]) / h_ * r;
                    q_x[i] = bx + hh * vx; qm_x[i] = bx - hh * vx; q_y[i] = by + hh * vy; qm_y[i] = by - hh * vy;
                }
            }
            const R c0 = R(cfg_.c0);
            for (int i = 0; i < N_; ++i) {
                const bool du = (Pn.m[i] != P_.m[i]) || (Pn.alpha[i] != P_.alpha[i]) || (Pn.beta[i] != P_.beta[i]);   // m change moves (q,qm) -> re-project too
                P_.m[i] = Pn.m[i]; P_.alpha[i] = Pn.alpha[i]; P_.beta[i] = Pn.beta[i]; P_.mu[i] = Pn.mu[i]; P_.eta[i] = Pn.eta[i]; P_.g[i] = Pn.g[i];
                if (du) {
                    p_x[i] = std::sqrt(R(2) * U(q_x[i], i) + c0); pm_x[i] = std::sqrt(R(2) * U(qm_x[i], i) + c0);
                    p_y[i] = std::sqrt(R(2) * U(q_y[i], i) + c0); pm_y[i] = std::sqrt(R(2) * U(qm_y[i], i) + c0);
                }
            }
            return finish_enpu(E_old, false);
        }
        if (!reproject && newpot == cfg_.potential) {
            double dk = 0; const double ih = 1.0 / double(h_);
            for (int i = 0; i < N_; ++i) {
                const double vx = (double(q_x[i]) - qm_x[i]) * ih, vy = (double(q_y[i]) - qm_y[i]) * ih;
                dk += 0.5 * (double(Pn.m[i]) - double(P_.m[i])) * (vx * vx + vy * vy);
            }
            if (E_old + dk <= std::max(E_old, 0.0)) {
                if (&Pn != &P_) P_.assign_from(Pn);
                E_ = double(energy()); ++enpu_calls_;
                return R(1);
            }
            ++enpu_fallback_;
            reproject = true;
        }
        const R hh = R(0.5) * h_;
        const bool needs_reproj = true;
        for (int i = 0; i < N_; ++i) {
            const R r = std::sqrt(P_.m[i] / Pn.m[i]);
            const R bx = R(0.5) * (q_x[i] + qm_x[i]), vx = (q_x[i] - qm_x[i]) / h_ * r;
            const R by = R(0.5) * (q_y[i] + qm_y[i]), vy = (q_y[i] - qm_y[i]) / h_ * r;
            q_x[i] = bx + hh * vx; qm_x[i] = bx - hh * vx; q_y[i] = by + hh * vy; qm_y[i] = by - hh * vy;
        }
        if (&Pn != &P_) P_.assign_from(Pn);
        cfg_.potential = newpot;
        if (needs_reproj) reset_psi();
        return finish_enpu(E_old);
    }
    R finish_enpu(double E_old, bool psi_consistent = true) noexcept {
        double E1 = double(energy()); const double target = std::max(E_old, 0.0);
        R s = R(1);
        if (E1 > target && !psi_consistent) { reset_psi(); E1 = double(energy()); }   // scaling bound needs psi = sqrt(2U+c0)
        if (E1 > target) {
            s = (std::isfinite(E1) && E1 > 0) ? R(target / E1) : R(0);
            for (int i = 0; i < N_; ++i) { q_x[i] *= s; q_y[i] *= s; qm_x[i] *= s; qm_y[i] *= s; }
            reset_psi();
            E_ = double(energy());
        } else {
            E_ = E1;
        }
        ++enpu_calls_; if (s < R(1)) ++enpu_scaled_;
        return s;
    }

    R U(R s, int i) const noexcept {
        switch (cfg_.potential) {
            case Potential::None: return R(0);
            case Potential::Quartic: return R(0.25) * P_.alpha[i] * s * s * s * s;
            case Potential::QuarticLogcoshFast: return morpho_phs::potential_fast(s, P_.alpha[i], P_.beta[i]);
            default: return morpho_phs::potential(s, P_.alpha[i], P_.beta[i]);
        }
    }
#if MORPHO_SIMD_AVX2
    // Phase 5 (item 4, instrument profile): energy() and reset_psi() run serially on the audio thread at every
    // energetic ENPU-C update (instrument: ~1.3 energy() + 0.3 reset_psi() per 64-sample tick = ~110 us per 128-sample
    // block at 32x32 on the box, the largest serial cost).  Vectorised versions with the SAME per-site operation order
    // and the SAME sequential double accumulation as the scalar code -> bit-identical results (test_phase5 S9).
    static simd::V<double> ldd(const R* p) noexcept {
        if constexpr (std::is_same_v<R, float>) return {_mm256_cvtps_pd(_mm_loadu_ps(p))};
        else return simd::V<double>::loadu(p);
    }
    R energy_stencil() const noexcept {
        using VD = simd::V<double>;
        double kin = 0, pot = 0, sav = 0; const double ih = 1.0 / double(h_), k0 = cfg_.k0, c0 = cfg_.c0;
        const R *s0x = sc_[0].data(), *s0m = sc_[1].data(), *s0y = sc_[2].data(), *s1x = sc_[3].data(), *s1m = sc_[4].data(), *s1y = sc_[5].data(),
                *s2x = sc_[6].data(), *s2m = sc_[7].data(), *s2y = sc_[8].data();
        const R* m = P_.m.data(); const int o2 = o2_;
        const VD vk0 = VD::set1(k0), vih = VD::set1(ih), vhalf = VD::set1(0.5), vq = VD::set1(0.25), vc0 = VD::set1(c0);
        alignas(32) double tk[4], tp[4], ts[4];
        int i = 0;
        for (; i + 4 <= N_; i += 4) {
            const VD qxi = ldd(qm_x + i), qyi = ldd(qm_y + i);
            VD kx = vk0 * qxi, ky = vk0 * qyi;
            {   const VD dx = qxi - VD::swap_pairs(qxi), dy = qyi - VD::swap_pairs(qyi);
                const VD cxx = ldd(s0x + i), cxy = ldd(s0m + i), cyy = ldd(s0y + i);
                kx = kx + (cxx * dx + cxy * dy); ky = ky + (cxy * dx + cyy * dy); }
            {   const VD dx = qxi - VD::blend_ab(ldd(qm_x + i - 1), ldd(qm_x + i + 1)), dy = qyi - VD::blend_ab(ldd(qm_y + i - 1), ldd(qm_y + i + 1));
                const VD cxx = ldd(s1x + i), cxy = ldd(s1m + i), cyy = ldd(s1y + i);
                kx = kx + (cxx * dx + cxy * dy); ky = ky + (cxy * dx + cyy * dy); }
            {   const VD dx = qxi - VD::blend_ab(ldd(qm_x + i - o2), ldd(qm_x + i + o2)), dy = qyi - VD::blend_ab(ldd(qm_y + i - o2), ldd(qm_y + i + o2));
                const VD cxx = ldd(s2x + i), cxy = ldd(s2m + i), cyy = ldd(s2y + i);
                kx = kx + (cxx * dx + cxy * dy); ky = ky + (cxy * dx + cyy * dy); }
            const VD qxn = ldd(q_x + i), qyn = ldd(q_y + i);
            const VD vx = (qxn - qxi) * vih, vy = (qyn - qyi) * vih;
            ((vhalf * ldd(m + i)) * (vx * vx + vy * vy)).store(tk);
            (vhalf * (qxn * kx + qyn * ky)).store(tp);
            const VD px = ldd(p_x + i), pmx = ldd(pm_x + i), py = ldd(p_y + i), pmy = ldd(pm_y + i);
            (vq * (((px * px + pmx * pmx) + py * py) + pmy * pmy) - vc0).store(ts);
            for (int l = 0; l < 4; ++l) { kin += tk[l]; pot += tp[l]; sav += ts[l]; }
        }
        for (; i < N_; ++i) {   // remainder: scalar, same formula as energy_csr() (stencil neighbours)
            double kx = k0 * qm_x[i], ky = k0 * qm_y[i];
            const int nb[3] = {i ^ 1, (i & 1) ? i + 1 : i - 1, (i & 1) ? i + o2 : i - o2};
            const R* cs[3][3] = {{s0x, s0m, s0y}, {s1x, s1m, s1y}, {s2x, s2m, s2y}};
            for (int k = 0; k < 3; ++k) {
                const int j = nb[k]; const double dx = double(qm_x[i]) - qm_x[j], dy = double(qm_y[i]) - qm_y[j];
                kx += double(cs[k][0][i]) * dx + double(cs[k][1][i]) * dy; ky += double(cs[k][1][i]) * dx + double(cs[k][2][i]) * dy;
            }
            const double vx = (double(q_x[i]) - qm_x[i]) * ih, vy = (double(q_y[i]) - qm_y[i]) * ih;
            kin += 0.5 * double(P_.m[i]) * (vx * vx + vy * vy);
            pot += 0.5 * (double(q_x[i]) * kx + double(q_y[i]) * ky);
            sav += 0.25 * (double(p_x[i]) * p_x[i] + double(pm_x[i]) * pm_x[i] + double(p_y[i]) * p_y[i] + double(pm_y[i]) * pm_y[i]) - cfg_.c0;
        }
        return R(kin + pot + sav);
    }
    template <Potential POT>
    int reset_psi_simd() noexcept {
        using VV = simd::V<R>; constexpr int W = VV::W;
        const VV vtwo = VV::set1(R(2)), vc0 = VV::set1(R(cfg_.c0));
        const R* al = P_.alpha.data(); const R* be = P_.beta.data();
        int i = 0;
        for (; i + W <= N_; i += W) {
            const VV a = VV::loadu(al + i), b = VV::loadu(be + i);
            vsqrt(vtwo * vUpot<POT>(VV::load(q_x + i), a, b) + vc0).store(p_x + i);
            vsqrt(vtwo * vUpot<POT>(VV::load(qm_x + i), a, b) + vc0).store(pm_x + i);
            vsqrt(vtwo * vUpot<POT>(VV::load(q_y + i), a, b) + vc0).store(p_y + i);
            vsqrt(vtwo * vUpot<POT>(VV::load(qm_y + i), a, b) + vc0).store(pm_y + i);
        }
        return i;
    }
#endif
    void reset_psi() noexcept {
        const R c0 = R(cfg_.c0);
        int i0 = 0;
#if MORPHO_SIMD_AVX2
        if (kernel_ == 1) {
            if (cfg_.potential == Potential::Quartic) i0 = reset_psi_simd<Potential::Quartic>();
            else if (cfg_.potential == Potential::QuarticLogcoshFast) i0 = reset_psi_simd<Potential::QuarticLogcoshFast>();
        }
#endif
        for (int i = i0; i < N_; ++i) {
            p_x[i] = std::sqrt(R(2) * U(q_x[i], i) + c0); pm_x[i] = std::sqrt(R(2) * U(qm_x[i], i) + c0);
            p_y[i] = std::sqrt(R(2) * U(q_y[i], i) + c0); pm_y[i] = std::sqrt(R(2) * U(qm_y[i], i) + c0);
        }
    }

    // ---- parallel machinery -------------------------------------------------------------------------------
    void start_workers(int T) {
        idle_spin_dyn_.store(cfg_.idle_spin_ns, std::memory_order_relaxed);
        last_call_ = {}; ema_period_ns_ = 0; ema_busy_ns_ = 0;
        T_ = T; barrier_.init(T, rt::WaitPolicy{cfg_.barrier_spin_ns, cfg_.barrier_yield_ns}); spin_barrier_.init(T, cfg_.barrier_rescue_ns);
        use_hybrid_ = cfg_.inblock_os_wait; idle_os_waits_.store(0); ecoqos_ok_.store(0);
        seq_.store(0); done_.store(0); quit_.store(false); pinned_ok_.store(0); mmcss_ok_.store(0);
        t_c0_.assign(T + 1, 0);
        for (int t = 0; t <= T; ++t) t_c0_[t] = int((long long)nchunks_ * t / T);
        // padded chunk-partial layout: thread t's chunks start on their own cache line (8 doubles)
        cp_idx_.assign(std::size_t(nchunks_), 0);
        int off = 0;
        for (int t = 0; t < T; ++t) {
            for (int c = t_c0_[t]; c < t_c0_[t + 1]; ++c) cp_idx_[std::size_t(c)] = off + (c - t_c0_[t]);
            off += ((t_c0_[t + 1] - t_c0_[t]) + 7) / 8 * 8;
        }
        cp_stride_ = std::max(8, off);
        chunk_pd_.assign(2 * std::size_t(cp_stride_), 0.0); chunk_pi_.assign(2 * std::size_t(cp_stride_), 0.0);
        const auto pm = rt::PinMode(std::clamp(cfg_.pin, 0, 3));
        caller_computes_ = cfg_.caller_computes < 0 ? (pm == rt::PinMode::None) : (cfg_.caller_computes != 0);
        // Guard: a dedicated pool adds the (waiting) caller as an extra runnable thread. With T >= the number of
        // logical CPUs that oversubscribes the machine and a per-sample barrier then degrades to OS waits (measured:
        // minutes per second of audio). In that case the caller computes instead (same result bit for bit).
        oversub_guard_ = false;
        if (!caller_computes_ && T > 1) {
            const unsigned hw = std::thread::hardware_concurrency();
            if (hw > 0 && unsigned(T) >= hw) { caller_computes_ = true; oversub_guard_ = true; }
        }
        if (T == 1 && caller_computes_) { pool_ = 0; }
        else pool_ = caller_computes_ ? T - 1 : T;
        // CPU plan (allocates: prepare() only)
        cpu_plan_.assign(std::size_t(T), {});
        std::string cpus;
        if (pm != rt::PinMode::None && pool_ > 0) {
            const auto topo = rt::query_topology();
            // Phase 5: P-core lists exclude physical core 0 and SMT siblings (clean_perf_cpus); PSet keeps Phase-4 meaning.
            const auto pc = rt::clean_perf_cpus(topo), ps = rt::perf_cpu_set(topo);
            for (int t = caller_computes_ ? 1 : 0, k = 0; t < T; ++t, ++k) {
                if (pm == rt::PinMode::CpuSet) cpu_plan_[std::size_t(t)] = pc;
                else if (pm == rt::PinMode::PSet) cpu_plan_[std::size_t(t)] = ps;
                else if (std::size_t(k) < pc.size()) cpu_plan_[std::size_t(t)] = {pc[pc.size() - 1 - std::size_t(k)]};   // from the last P-core down
                else cpu_plan_[std::size_t(t)] = ps;   // more workers than P-cores: never double-pin one core; soft P-core set instead
                if (pm == rt::PinMode::PCores && !cpu_plan_[std::size_t(t)].empty()) cpus += (cpus.empty() ? "" : ",") + (cpu_plan_[std::size_t(t)].size() == 1 ? std::to_string(cpu_plan_[std::size_t(t)][0].id) : std::string("Pset"));
            }
            if (pm == rt::PinMode::PSet) cpus = std::to_string(ps.size()) + " cpus";
            if (pm == rt::PinMode::CpuSet) { for (auto& c : pc) cpus += (cpus.empty() ? "" : ",") + std::to_string(c.id); }
            topo_desc_ = "topo=" + topo.source + " P=" + std::to_string(topo.perf_physical_cores) + "/" + std::to_string(topo.physical_cores) +
                         " hybrid=" + std::to_string(int(topo.hybrid));
        } else topo_desc_.clear();
        workers_.clear();
        for (int t = caller_computes_ ? 1 : 0; t < T && pool_ > 0; ++t) workers_.emplace_back([this, t] { worker_loop(t); });
        if (pool_ > 0) {   // wait until every worker applied its affinity / MMCSS so pin_report() is final
            const auto t0 = rt::Clock::now();
            while (ready_.load() < pool_ && rt::Clock::now() - t0 < std::chrono::seconds(2)) std::this_thread::yield();
        }
        pin_report_ = std::string("pin=") + rt::pin_mode_name(pm) + " mmcss=" + std::to_string(int(cfg_.mmcss)) +
                      " pool=" + (caller_computes_ ? (oversub_guard_ ? "caller(oversub-guard)+" : "caller+") : "dedicated") + std::to_string(pool_) +
                      (pm != rt::PinMode::None ? " cpus=[" + cpus + "] pinned=" + std::to_string(pinned_ok_.load()) + "/" + std::to_string(pool_) + " " + topo_desc_ : std::string()) +
                      (cfg_.mmcss ? " mmcss_ok=" + std::to_string(mmcss_ok_.load()) + "/" + std::to_string(pool_) : std::string()) +
                      " kernel=" + (kernel_ ? "stencil" : "csr") + (cfg_.inblock_os_wait ? " barrier=hybrid" : " barrier=spin") +
                      (cfg_.no_power_throttling && pool_ > 0 ? " ecoqos_off=" + std::to_string(ecoqos_ok_.load()) + "/" + std::to_string(pool_) : std::string()) +
                      (cfg_.ersav ? " ersav=1" : "");
    }
    void stop_workers() {
        if (workers_.empty()) { ready_.store(0); return; }
        // quit_ is published before the generation bump (both seq_cst); a worker that sees the new generation therefore
        // sees quit_; the unconditional notify_all wakes sleepers (WaitOnAddress / futex re-check the value, so a
        // spurious or early wake only loops).
        quit_.store(true, std::memory_order_seq_cst); seq_.fetch_add(1, std::memory_order_seq_cst); seq_.notify_all();
        for (auto& w : workers_) w.join();
        workers_.clear(); ready_.store(0);
    }
    void worker_loop(int t) {
        if (cfg_.ftz_workers) enable_ftz_daz();
        if (!cpu_plan_[std::size_t(t)].empty()) {
            const bool ok = cfg_.pin == int(rt::PinMode::CpuSet) ? rt::set_thread_cpuset(cpu_plan_[std::size_t(t)]) : rt::pin_current_thread(cpu_plan_[std::size_t(t)]);
            if (ok) pinned_ok_.fetch_add(1);
        }
        if (cfg_.no_power_throttling && rt::disable_power_throttling()) ecoqos_ok_.fetch_add(1);
        rt::MmcssScope mm(cfg_.mmcss);
        if (mm.active()) mmcss_ok_.fetch_add(1);
        // Phase 5b: start from the generation the pool was created with (start_workers stored seq_ = 0 before spawning),
        // not from a fresh load: a worker that starts late (slow thread start / MMCSS registration / ready_ timeout)
        // must still run block 1 instead of skipping it (a skipped block = caller or barrier waits forever).
        unsigned seen = 0;
        MORPHO_PHS_RACE_HOOK(2);
        ready_.fetch_add(1);
        for (;;) {
            const long sp = idle_spin_dyn_.load(std::memory_order_relaxed);
            const rt::WaitPolicy idle{sp, sp + (cfg_.idle_yield_ns - cfg_.idle_spin_ns)};
            if (rt::hybrid_wait(seq_, seen, idle, &idle_sleepers_)) idle_os_waits_.fetch_add(1, std::memory_order_relaxed);
            const unsigned s = seq_.load(std::memory_order_acquire);
            if (s == seen) continue;
            seen = s;
            if (quit_.load(std::memory_order_acquire)) return;
            run_block(t);
            if (t == 0) {   // dedicated pool: compute thread 0 committed the state inside run_block -> signal the caller
                MORPHO_PHS_RACE_HOOK(5);
                done_.store(s, std::memory_order_seq_cst);
                MORPHO_PHS_RACE_HOOK(6);
                if (done_sleepers_.load(std::memory_order_seq_cst) > 0) done_.notify_all();
            }
        }
    }

    // Pass 1 (pure element-wise, no gathers): SAV coefficients for [i0,iend) -> gxs_/gys_.  Auto-vectorises
    // (needs -fno-math-errno so std::sqrt maps to vsqrtp*).  Results are identical to the fused Phase-2 loop.
    template <Potential POT>
    static void sav_pass(int i0, int iend, R c0, const R* __restrict qx, const R* __restrict qy, const R* __restrict al,
                         const R* __restrict be, R* __restrict gxs, R* __restrict gys) noexcept {
        if constexpr (POT == Potential::Quartic) {
            for (int i = i0; i < iend; ++i) {
                const R a = al[i], x2 = qx[i] * qx[i], y2 = qy[i] * qy[i];
                gxs[i] = a * x2 * qx[i] / std::sqrt(R(0.5) * a * x2 * x2 + c0);
                gys[i] = a * y2 * qy[i] / std::sqrt(R(0.5) * a * y2 * y2 + c0);
            }
        } else if constexpr (POT == Potential::QuarticLogcosh) {
            for (int i = i0; i < iend; ++i) { gxs[i] = sav_coeff_hq(qx[i], al[i], be[i], c0); gys[i] = sav_coeff_hq(qy[i], al[i], be[i], c0); }
        } else if constexpr (POT == Potential::QuarticLogcoshFast) {
            for (int i = i0; i < iend; ++i) { gxs[i] = sav_coeff_hqf(qx[i], al[i], be[i], c0); gys[i] = sav_coeff_hqf(qy[i], al[i], be[i], c0); }
        } else {
            for (int i = i0; i < iend; ++i) { gxs[i] = R(0); gys[i] = R(0); }
        }
    }

    // Potential at s for the current family, as a static helper usable inside the kernels (same ops as U()).
    template <Potential POT>
    static R Upot(R s, R a, R b) noexcept {
        if constexpr (POT == Potential::Quartic) return R(0.25) * a * s * s * s * s;
        else if constexpr (POT == Potential::QuarticLogcoshFast) return morpho_phs::potential_fast(s, a, b);
        else if constexpr (POT == Potential::QuarticLogcosh) return morpho_phs::potential(s, a, b);
        else return R(0);
    }
    // E-RSAV relaxation of one site (scalar).  In/out: new psi (x,y); returns the relaxed SAV energy / h (>= 0).
    template <Potential POT>
    static R ersav_site(R nqx, R nqy, R& npx, R& npy, R a, R b, R c0, R ih) noexcept {
        const R cx = std::sqrt(R(2) * Upot<POT>(nqx, a, b) + c0), cy = std::sqrt(R(2) * Upot<POT>(nqy, a, b) + c0);
        const R sx = std::copysign(std::min(std::abs(npx), cx), npx), sy = std::copysign(std::min(std::abs(npy), cy), npy);
        const R rel = R(0.25) * ((npx * npx - sx * sx) + (npy * npy - sy * sy)) * ih;
        npx = sx; npy = sy;
        return rel;
    }

    template <Potential POT, bool ERS>
    void site_range(int i0, int i1, bool gate_open, R fr, const R* __restrict qx, const R* __restrict qy, R* __restrict qmx, R* __restrict qmy,
                    R* __restrict pmx, R* __restrict pmy) noexcept {
        const R ih = R(1) / h_, ih2 = ih * ih, hih = R(0.5) * ih, c0 = R(cfg_.c0), k0 = R(cfg_.k0);
        R* __restrict fx = fx_.data(); R* __restrict fy = fy_.data();
        R* __restrict vbx = vbx_.data(); R* __restrict vby = vby_.data(); R* __restrict pd = pd_.data(); R* __restrict pin = pi_.data();
        const int* __restrict nb = nb_.data(); const R* __restrict cxx = cxx_.data(); const R* __restrict cxy = cxy_.data(); const R* __restrict cyy = cyy_.data();
        const R* __restrict m = P_.m.data(); const R* __restrict al = P_.alpha.data(); const R* __restrict be = P_.beta.data();
        const R* __restrict mu = P_.mu.data(); const R* __restrict eta = P_.eta.data(); const R* __restrict g = P_.g.data();
        const R* __restrict g0 = g0_.data(); const R* __restrict mu0 = mu0_.data();
        R* __restrict gxs = gxs_.data(); R* __restrict gys = gys_.data();
        const R mucap = gate_open ? std::numeric_limits<R>::max() : R(0);
        const int iend = std::min(i1, N_);
        sav_pass<POT>(i0, iend, c0, qx, qy, al, be, gxs, gys);
        for (int i = i0; i < iend; ++i) {
            R Kx = k0 * qx[i], Ky = k0 * qy[i];
            for (int k = 0; k < 3; ++k) {
                const int j = nb[3 * i + k]; const R dx = qx[i] - qx[j], dy = qy[i] - qy[j];
                Kx += cxx[3 * i + k] * dx + cxy[3 * i + k] * dy; Ky += cxy[3 * i + k] * dx + cyy[3 * i + k] * dy;
            }
            const R gx = gxs[i], gy = gys[i];
            const R gi = g0[i] + fr * (g[i] - g0[i]), mui = mu0[i] + fr * (mu[i] - mu0[i]);
            const R dx0 = qx[i] - qmx[i], dy0 = qy[i] - qmy[i];
            const R vx = dx0 * ih, vy = dy0 * ih;
            const R d = -std::min(mui, mucap) + eta[i] * (vx * vx + vy * vy);
            const R base = m[i] * ih2 + d * hih;
            const R axx = base + R(0.5) * gx * gx, ayy = base + R(0.5) * gy * gy;
            const R rx = R(2) * m[i] * dx0 * ih2 - Kx - gx * pmx[i] + fx[i];
            const R ry = R(2) * m[i] * dy0 * ih2 - Ky - gy * pmy[i] + fy[i];
            const R c = gi * hih;
            const R idet = R(1) / (axx * ayy + c * c);
            const R wx = (ayy * rx + c * ry) * idet, wy = (axx * ry - c * rx) * idet;
            const R bx = wx * hih, by = wy * hih;
            vbx[i] = bx; vby[i] = by;
            pd[i] = d * (bx * bx + by * by);
            pin[i] = bx * fx[i] + by * fy[i];
            qmx[i] = qmx[i] + wx; qmy[i] = qmy[i] + wy;           // q^{n+1} overwrites q^{n-1}
            if constexpr (ERS) {
                R npx = pmx[i] + gx * wx, npy = pmy[i] + gy * wy;
                pd[i] = pd[i] + ersav_site<POT>(qmx[i], qmy[i], npx, npy, al[i], be[i], c0, ih);
                pmx[i] = npx; pmy[i] = npy;
            } else {
                pmx[i] = pmx[i] + gx * wx; pmy[i] = pmy[i] + gy * wy; // psi^{n+1} overwrites psi^{n-1}
            }
        }
    }

#if MORPHO_SIMD_AVX2
    // Vector SAV coefficient (same op order as sav_pass / sav_coeff_hqf).
    template <Potential POT>
    static simd::V<R> vsav(simd::V<R> s, simd::V<R> a, simd::V<R> b, simd::V<R> c0) noexcept {
        using VV = simd::V<R>;
        if constexpr (POT == Potential::Quartic) {
            const VV s2 = s * s;
            return ((a * s2) * s) / vsqrt(((VV::set1(R(0.5)) * a) * s2) * s2 + c0);
        } else {   // QuarticLogcoshFast
            const VV s2 = s * s; VV L = VV::set1(R(0)), dL = VV::set1(R(0));
            for (int k = 0; k < 3; ++k) {
                const R ck = R(lcfast::c[k]), wk = R(lcfast::w[k]);
                const VV vck = VV::set1(ck), r = vsqrt(VV::set1(ck * ck) + s2), rc = r + vck;
                const VV inv = VV::set1(wk) / (r * rc);
                L = L + (s2 * r) * inv; dL = dL + (s * rc) * inv;
            }
            return ((a * s2) * s + b * dL) / vsqrt(VV::set1(R(2)) * ((VV::set1(R(0.25)) * a) * s2 * s2 + b * L) + c0);
        }
    }
    template <Potential POT>
    static simd::V<R> vUpot(simd::V<R> s, simd::V<R> a, simd::V<R> b) noexcept {
        using VV = simd::V<R>;
        if constexpr (POT == Potential::Quartic) return (((VV::set1(R(0.25)) * a) * s) * s) * s * s;
        else if constexpr (POT == Potential::QuarticLogcoshFast) {
            // potential_fast: 0.25 a s^4 + b * sum w s^2/(sqrt(c^2+s^2)+c)
            const VV s2 = s * s; VV acc = VV::set1(R(0));
            for (int k = 0; k < 3; ++k) {
                const R ck = R(lcfast::c[k]);
                acc = acc + (VV::set1(R(lcfast::w[k])) * s2) / (vsqrt(VV::set1(ck * ck) + s2) + VV::set1(ck));
            }
            return (((VV::set1(R(0.25)) * a) * s) * s) * s * s + b * acc;
        } else return VV::set1(R(0));
    }
#endif

    // Phase 5 bipartite stencil kernel (see header).  Bit-identical to site_range<POT, ERS>.
    template <Potential POT, bool ERS>
    void stencil_range(int i0, int i1, bool gate_open, R fr, const R* __restrict qx, const R* __restrict qy, R* __restrict qmx, R* __restrict qmy,
                       R* __restrict pmx, R* __restrict pmy) noexcept {
        const R ih = R(1) / h_, ih2 = ih * ih, hih = R(0.5) * ih, c0 = R(cfg_.c0), k0 = R(cfg_.k0);
        R* __restrict fx = fx_.data(); R* __restrict fy = fy_.data();
        R* __restrict vbx = vbx_.data(); R* __restrict vby = vby_.data(); R* __restrict pd = pd_.data(); R* __restrict pin = pi_.data();
        const R* __restrict m = P_.m.data(); const R* __restrict al = P_.alpha.data(); const R* __restrict be = P_.beta.data();
        const R* __restrict mu = P_.mu.data(); const R* __restrict eta = P_.eta.data(); const R* __restrict g = P_.g.data();
        const R* __restrict g0 = g0_.data(); const R* __restrict mu0 = mu0_.data();
        R* __restrict gxs = gxs_.data(); R* __restrict gys = gys_.data();
        const R *s0x = sc_[0].data(), *s0m = sc_[1].data(), *s0y = sc_[2].data(), *s1x = sc_[3].data(), *s1m = sc_[4].data(), *s1y = sc_[5].data(),
                *s2x = sc_[6].data(), *s2m = sc_[7].data(), *s2y = sc_[8].data();
        const R mucap = gate_open ? std::numeric_limits<R>::max() : R(0);
        const int iend = std::min(i1, N_), o2 = o2_;
        constexpr bool kScalarSav = (POT == Potential::QuarticLogcosh);   // expm1/log1p: scalar pre-pass
        if constexpr (kScalarSav) sav_pass<POT>(i0, iend, c0, qx, qy, al, be, gxs, gys);
        int i = i0;
#if MORPHO_SIMD_AVX2
        if constexpr (!(ERS && POT == Potential::QuarticLogcosh)) {
            using VV = simd::V<R>;
            constexpr int W = VV::W;
            const VV vk0 = VV::set1(k0), vih = VV::set1(ih), vih2 = VV::set1(ih2), vhih = VV::set1(hih), vc0 = VV::set1(c0),
                     vhalf = VV::set1(R(0.5)), vtwo = VV::set1(R(2)), vone = VV::set1(R(1)), vmucap = VV::set1(mucap), vfr = VV::set1(fr);
            for (; i + W <= iend; i += W) {
                const VV qxi = VV::load(qx + i), qyi = VV::load(qy + i);
                VV Kx = vk0 * qxi, Ky = vk0 * qyi;
                {   // slot 0: intra-cell partner
                    const VV dx = qxi - VV::swap_pairs(qxi), dy = qyi - VV::swap_pairs(qyi);
                    const VV cxx = VV::load(s0x + i), cxy = VV::load(s0m + i), cyy = VV::load(s0y + i);
                    Kx = Kx + (cxx * dx + cxy * dy); Ky = Ky + (cxy * dx + cyy * dy);
                }
                {   // slot 1: horizontal partner (A: i-1, B: i+1)
                    const VV nx = VV::blend_ab(VV::loadu(qx + i - 1), VV::loadu(qx + i + 1)), ny = VV::blend_ab(VV::loadu(qy + i - 1), VV::loadu(qy + i + 1));
                    const VV dx = qxi - nx, dy = qyi - ny;
                    const VV cxx = VV::load(s1x + i), cxy = VV::load(s1m + i), cyy = VV::load(s1y + i);
                    Kx = Kx + (cxx * dx + cxy * dy); Ky = Ky + (cxy * dx + cyy * dy);
                }
                {   // slot 2: vertical partner (A: i-o2, B: i+o2)
                    const VV nx = VV::blend_ab(VV::loadu(qx + i - o2), VV::loadu(qx + i + o2)), ny = VV::blend_ab(VV::loadu(qy + i - o2), VV::loadu(qy + i + o2));
                    const VV dx = qxi - nx, dy = qyi - ny;
                    const VV cxx = VV::load(s2x + i), cxy = VV::load(s2m + i), cyy = VV::load(s2y + i);
                    Kx = Kx + (cxx * dx + cxy * dy); Ky = Ky + (cxy * dx + cyy * dy);
                }
                const VV va = VV::loadu(al + i), vb = VV::loadu(be + i);
                VV gx, gy;
                if constexpr (POT == Potential::None) { gx = VV::set1(R(0)); gy = gx; }
                else if constexpr (kScalarSav) { gx = VV::load(gxs + i); gy = VV::load(gys + i); }
                else { gx = vsav<POT>(qxi, va, vb, vc0); gy = vsav<POT>(qyi, va, vb, vc0); }
                const VV vg0 = VV::load(g0 + i), vmu0 = VV::load(mu0 + i);
                const VV gi = vg0 + vfr * (VV::loadu(g + i) - vg0), mui = vmu0 + vfr * (VV::loadu(mu + i) - vmu0);
                const VV qmxi = VV::load(qmx + i), qmyi = VV::load(qmy + i);
                const VV dx0 = qxi - qmxi, dy0 = qyi - qmyi;
                const VV vx = dx0 * vih, vy = dy0 * vih;
                const VV d = neg(vmin(mui, vmucap)) + VV::loadu(eta + i) * (vx * vx + vy * vy);
                const VV vm = VV::loadu(m + i);
                const VV base = vm * vih2 + d * vhih;
                const VV axx = base + (vhalf * gx) * gx, ayy = base + (vhalf * gy) * gy;
                const VV vfx = VV::load(fx + i), vfy = VV::load(fy + i);
                const VV pmxi = VV::load(pmx + i), pmyi = VV::load(pmy + i);
                const VV rx = (((vtwo * vm) * dx0) * vih2 - Kx) - gx * pmxi + vfx;
                const VV ry = (((vtwo * vm) * dy0) * vih2 - Ky) - gy * pmyi + vfy;
                const VV c = gi * vhih;
                const VV idet = vone / (axx * ayy + c * c);
                const VV wx = (ayy * rx + c * ry) * idet, wy = (axx * ry - c * rx) * idet;
                const VV bx = wx * vhih, by = wy * vhih;
                bx.store(vbx + i); by.store(vby + i);
                VV vpd = d * (bx * bx + by * by);
                (bx * vfx + by * vfy).store(pin + i);
                const VV nqx = qmxi + wx, nqy = qmyi + wy;
                nqx.store(qmx + i); nqy.store(qmy + i);
                VV npx = pmxi + gx * wx, npy = pmyi + gy * wy;
                if constexpr (ERS) {
                    const VV cx = vsqrt(vtwo * vUpot<POT>(nqx, va, vb) + vc0), cy = vsqrt(vtwo * vUpot<POT>(nqy, va, vb) + vc0);
                    const VV sx = vcopysign(vmin(vabs(npx), cx), npx), sy = vcopysign(vmin(vabs(npy), cy), npy);
                    const VV rel = (VV::set1(R(0.25)) * ((npx * npx - sx * sx) + (npy * npy - sy * sy))) * vih;
                    vpd = vpd + rel; npx = sx; npy = sy;
                }
                vpd.store(pd + i);
                npx.store(pmx + i); npy.store(pmy + i);
            }
        }
#endif
        // scalar remainder (and the whole range without AVX2): same ops, parity-derived neighbours
        for (; i < iend; ++i) {
            const bool B = (i & 1) != 0;
            const int j0 = i ^ 1, j1 = B ? i + 1 : i - 1, j2 = B ? i + o2 : i - o2;
            R Kx = k0 * qx[i], Ky = k0 * qy[i];
            { const R dx = qx[i] - qx[j0], dy = qy[i] - qy[j0]; Kx += s0x[i] * dx + s0m[i] * dy; Ky += s0m[i] * dx + s0y[i] * dy; }
            { const R dx = qx[i] - qx[j1], dy = qy[i] - qy[j1]; Kx += s1x[i] * dx + s1m[i] * dy; Ky += s1m[i] * dx + s1y[i] * dy; }
            { const R dx = qx[i] - qx[j2], dy = qy[i] - qy[j2]; Kx += s2x[i] * dx + s2m[i] * dy; Ky += s2m[i] * dx + s2y[i] * dy; }
            R gx = R(0), gy = R(0);
            if constexpr (POT == Potential::Quartic) {
                const R a = al[i], x2 = qx[i] * qx[i], y2 = qy[i] * qy[i];
                gx = a * x2 * qx[i] / std::sqrt(R(0.5) * a * x2 * x2 + c0);
                gy = a * y2 * qy[i] / std::sqrt(R(0.5) * a * y2 * y2 + c0);
            } else if constexpr (POT == Potential::QuarticLogcoshFast) {
                gx = sav_coeff_hqf(qx[i], al[i], be[i], c0); gy = sav_coeff_hqf(qy[i], al[i], be[i], c0);
            } else if constexpr (kScalarSav) { gx = gxs[i]; gy = gys[i]; }
            const R gi = g0[i] + fr * (g[i] - g0[i]), mui = mu0[i] + fr * (mu[i] - mu0[i]);
            const R dx0 = qx[i] - qmx[i], dy0 = qy[i] - qmy[i];
            const R vx = dx0 * ih, vy = dy0 * ih;
            const R d = -std::min(mui, mucap) + eta[i] * (vx * vx + vy * vy);
            const R base = m[i] * ih2 + d * hih;
            const R axx = base + R(0.5) * gx * gx, ayy = base + R(0.5) * gy * gy;
            const R rx = R(2) * m[i] * dx0 * ih2 - Kx - gx * pmx[i] + fx[i];
            const R ry = R(2) * m[i] * dy0 * ih2 - Ky - gy * pmy[i] + fy[i];
            const R c = gi * hih;
            const R idet = R(1) / (axx * ayy + c * c);
            const R wx = (ayy * rx + c * ry) * idet, wy = (axx * ry - c * rx) * idet;
            const R bx = wx * hih, by = wy * hih;
            vbx[i] = bx; vby[i] = by;
            pd[i] = d * (bx * bx + by * by);
            pin[i] = bx * fx[i] + by * fy[i];
            qmx[i] = qmx[i] + wx; qmy[i] = qmy[i] + wy;
            if constexpr (ERS) {
                R npx = pmx[i] + gx * wx, npy = pmy[i] + gy * wy;
                pd[i] = pd[i] + ersav_site<POT>(qmx[i], qmy[i], npx, npy, al[i], be[i], c0, ih);
                pmx[i] = npx; pmy[i] = npy;
            } else {
                pmx[i] = pmx[i] + gx * wx; pmy[i] = pmy[i] + gy * wy;
            }
        }
    }

    template <Potential POT>
    void kernel_range(int i0, int i1, bool gate_open, R fr, const R* qx, const R* qy, R* qmx, R* qmy, R* pmx, R* pmy) noexcept {
        if (kernel_ == 1) {
            if (cfg_.ersav) stencil_range<POT, true>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy);
            else stencil_range<POT, false>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy);
        } else {
            if (cfg_.ersav) site_range<POT, true>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy);
            else site_range<POT, false>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy);
        }
    }

    void run_block(int t) noexcept {
        const int c0 = t_c0_[t], c1 = t_c0_[t + 1];
        const int i0 = c0 * kChunk, i1 = c1 * kChunk;
        const int np = int(nports_);
        // thread-local copies of the rotating buffers (every thread performs the same swaps)
        R *qx = q_x, *qy = q_y, *qmx = qm_x, *qmy = qm_y, *px = p_x, *py = p_y, *pmx = pm_x, *pmy = pm_y;
        double E = E_, win = 0.0, sd = 0.0;
        const int iown = std::min(i1, N_);
        if (job_snap_) for (int i = i0; i < iown; ++i) { g0_[i] = P_.g[i]; mu0_[i] = P_.mu[i]; }
        const int stp = cfg_.interp_step;
        for (int n = 0; n < job_n_; ++n) {
            const bool gate_open = E < cfg_.E_cap;
            const R fr = stp > 0 ? std::min(R(1), R((n / stp + 1) * stp) / R(job_n_)) : R(1);
            const R* un = job_u_ + std::size_t(n) * np;
            for (int k = 0; k < np; ++k) {   // owner-computes port forces
                const auto& p = ports_[k];
                if (p.site >= i0 && p.site < i1) { fx_[p.site] += un[k] * p.bx; fy_[p.site] += un[k] * p.by; }
            }
            switch (cfg_.potential) {
                case Potential::None: kernel_range<Potential::None>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy); break;
                case Potential::Quartic: kernel_range<Potential::Quartic>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy); break;
                case Potential::QuarticLogcoshFast: kernel_range<Potential::QuarticLogcoshFast>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy); break;
                default: kernel_range<Potential::QuarticLogcosh>(i0, i1, gate_open, fr, qx, qy, qmx, qmy, pmx, pmy); break;
            }
            double* cpd = chunk_pd_.data() + (n & 1) * cp_stride_; double* cpi = chunk_pi_.data() + (n & 1) * cp_stride_;
            const int* __restrict cpx = cp_idx_.data();
            for (int c = c0; c < c1; ++c) {   // fixed-order chunk partial sums (8 lanes)
                R a[8] = {0, 0, 0, 0, 0, 0, 0, 0}, b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
                const R* pd = pd_.data() + c * kChunk; const R* pi = pi_.data() + c * kChunk;
                for (int i = 0; i < kChunk; i += 8) for (int l = 0; l < 8; ++l) { a[l] += pd[i + l]; b[l] += pi[i + l]; }
                double sa = 0, sb = 0; for (int l = 0; l < 8; ++l) { sa += double(a[l]); sb += double(b[l]); }
                cpd[cpx[c]] = sa; cpi[cpx[c]] = sb;
            }
            R* yn = job_y_ + std::size_t(n) * np;
            for (int k = 0; k < np; ++k) {   // owner writes port outputs and clears its force / input-power entries
                const auto& p = ports_[k];
                if (p.site >= i0 && p.site < i1) { yn[k] = p.bx * vbx_[p.site] + p.by * vby_[p.site]; fx_[p.site] = 0; fy_[p.site] = 0; pi_[p.site] = 0; }
            }
            // ONE barrier per sample.  Safe without a second one: next sample a thread only writes its own sites'
            // q^{n-1}/psi^{n-1} slots (never read by others) and the other parity of the chunk-partial buffer.
            if (T_ > 1) { if (use_hybrid_) barrier_.arrive_and_wait(); else spin_barrier_.arrive_and_wait(); }
            sd = 0.0; double si = 0.0;
            for (int c = 0; c < nchunks_; ++c) { sd += cpd[cpx[c]]; si += cpi[cpx[c]]; }
            E += double(h_) * (si - sd); win += double(h_) * si;
            std::swap(qx, qmx); std::swap(qy, qmy); std::swap(px, pmx); std::swap(py, pmy);
        }
        for (int i = i0; i < iown; ++i) { g0_[i] = P_.g[i]; mu0_[i] = P_.mu[i]; }   // ramp reached its target (own sites only)
        if (T_ > 1) { if (use_hybrid_) barrier_.arrive_and_wait(); else spin_barrier_.arrive_and_wait(); }   // everyone done with the block before the caller touches state
        if (t == 0) {
            q_x = qx; q_y = qy; qm_x = qmx; qm_y = qmy; p_x = px; p_y = py; pm_x = pmx; pm_y = pmy;
            E_ = E; job_win_ = win; last_pdis_ = sd;
        }
    }

    const Honeycomb* lat_ = nullptr;
    Config cfg_{};
    int N_ = 0, Np_ = 0, nchunks_ = 0, T_ = 1; R h_ = R(1);
    AlignedBuf<R> qa_x, qa_y, qb_x, qb_y, pa_x, pa_y, pb_x, pb_y, fx_, fy_, pd_, pi_, vbx_, vby_, cxx_, cxy_, cyy_, gxs_, gys_, g0_, mu0_;
    bool ramp_valid_ = false, job_snap_ = true;
    AlignedBuf<int> nb_;
    AlignedBuf<R> sc_[9];                 // Phase 5 stencil stiffness: [3*slot + {xx,xy,yy}][site]
    int o2_ = 1, G_ = 16, kernel_ = 1; bool stencil_ok_ = false;
    AlignedBuf<double> chunk_pd_, chunk_pi_;
    R *q_x = nullptr, *q_y = nullptr, *qm_x = nullptr, *qm_y = nullptr, *p_x = nullptr, *p_y = nullptr, *pm_x = nullptr, *pm_y = nullptr;
    SiteParams<R> P_;
    std::vector<Port<R>> ports_; std::size_t nports_ = 0;
    double E_ = 0, last_pdis_ = 0;
public:
    long long enpu_calls_ = 0, enpu_scaled_ = 0, enpu_fallback_ = 0;
private:
    // job
    const R* job_u_ = nullptr; R* job_y_ = nullptr; int job_n_ = 0; double job_win_ = 0;
    std::vector<int> t_c0_, cp_idx_;
    int cp_stride_ = 8, pool_ = 0;
    bool caller_computes_ = true;
    bool oversub_guard_ = false;
    std::vector<std::vector<rt::LogicalCpu>> cpu_plan_;
    std::string pin_report_ = "pin=none", topo_desc_;
    std::vector<std::thread> workers_;
    rt::HybridBarrier barrier_;
    rt::SpinBarrier spin_barrier_;
    bool use_hybrid_ = false;
    alignas(64) std::atomic<long long> idle_os_waits_{0};
    std::atomic<int> ecoqos_ok_{0};
    std::atomic<bool> caller_qos_ok_{false};
public:
    bool caller_ecoqos_off() const noexcept { return caller_qos_ok_.load(); }
private:
    alignas(64) std::atomic<unsigned> seq_{0};
    alignas(64) std::atomic<unsigned> done_{0};
    alignas(64) std::atomic<int> idle_sleepers_{0}, done_sleepers_{0};
    // adaptive spin state (caller thread only, except idle_spin_dyn_ which the workers read)
    alignas(64) std::atomic<long> idle_spin_dyn_{200000};
    rt::Clock::time_point last_call_{};
    double ema_period_ns_ = 0, ema_busy_ns_ = 0;
    void adapt_period(rt::Clock::time_point now) noexcept {
        if (last_call_ != rt::Clock::time_point{}) {
            const double dt = double(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_call_).count());
            if (dt > 0 && dt < 2.0 * double(cfg_.spin_max_ns)) {   // ignore gaps (transport stopped, offline pauses)
                ema_period_ns_ = ema_period_ns_ <= 0 ? dt : ema_period_ns_ + 0.05 * (dt - ema_period_ns_);
                const long sp = std::clamp(long(1.25 * ema_period_ns_), cfg_.idle_spin_ns, std::max(cfg_.idle_spin_ns, cfg_.spin_max_ns));
                idle_spin_dyn_.store(sp, std::memory_order_relaxed);
            }
        }
        last_call_ = now;
    }
    std::atomic<int> ready_{0}, pinned_ok_{0}, mmcss_ok_{0};
    std::atomic<bool> quit_{false};
};

}  // namespace morpho_phs
