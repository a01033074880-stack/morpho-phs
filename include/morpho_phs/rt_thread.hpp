// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Phase 4: real-time threading helpers for the EsavLFMT worker pool (header-only, no JUCE).
//
//   * query_topology()     : logical CPUs with physical-core id and efficiency class.
//                            Windows: GetLogicalProcessorInformationEx(RelationProcessorCore) -> EfficiencyClass
//                            (higher = faster; hybrid Intel P-cores have the highest class), SMT from the core's mask.
//                            Linux: /sys/devices/system/cpu/cpuN/topology/{core_id,physical_package_id}; hybrid Intel
//                            via /sys/devices/cpu_core/cpus (P) vs /sys/devices/cpu_atom/cpus (E).  Restricted to the
//                            process affinity mask.  Anything else: a flat list from std::thread::hardware_concurrency.
//   * perf_cpus()          : ONE logical CPU per physical core of the highest efficiency class (SMT siblings skipped).
//   * pin_current_thread() : hard affinity of the calling thread to one logical CPU (Windows SetThreadGroupAffinity,
//                            Linux pthread_setaffinity_np) or to a set of CPUs.  Returns false and leaves the thread
//                            untouched if the OS refuses -> callers fall back gracefully.
//   * MmcssScope           : Windows MMCSS "Pro Audio" (AvSetMmThreadCharacteristicsW + AvSetMmThreadPriority HIGH),
//                            reverted in the destructor.  No-op on other OSes.
//   * HybridWait           : spin (PAUSE) for spin_ns, then std::this_thread::yield() until yield_ns, then block on the
//                            atomic (futex / WaitOnAddress).  Time-based so it behaves the same on CPUs whose PAUSE
//                            latency differs by 10x (Skylake+ ~140 cycles vs ~10 before).
//
// Phase 5 (expert review, docs/expert_review_phase4.md section 2.2/3):
//   * SpinBarrier          : in-block per-sample barrier = pure user-mode PAUSE spin (sense-reversing, every atomic on its
//                            own cache line).  No futex / WaitOnAddress ever; only if a single wait exceeds rescue_ns
//                            (1 ms, i.e. the block is already lost: oversubscription) it starts to yield, and every
//                            such wait is counted once in blocked_waits().  Normal operation: 0.  OS waits between blocks only.
//   * clean_perf_cpus()    : perf_cpus() minus the physical core that hosts logical CPU 0 (interrupt / DPC heavy on
//                            Windows), one logical CPU per physical P-core, no SMT siblings.
//   * PinMode::CpuSet      : "soft" placement: every worker gets the clean P-core set via Windows CPU Sets
//                            (SetThreadSelectedCpuSets); Linux uses an affinity mask over the same set.
//   * disable_power_throttling(): opt the calling thread out of EcoQoS / power throttling (Windows 10 1709+:
//                            SetThreadInformation(ThreadPowerThrottling, ControlMask = EXECUTION_SPEED, StateMask = 0)).
//                            Resolved through GetProcAddress, so it builds with any SDK and is a no-op where missing.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <avrt.h>
#if defined(_MSC_VER)
#pragma comment(lib, "avrt.lib")
#endif
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <fstream>
#include <sstream>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define MORPHO_RT_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define MORPHO_RT_PAUSE() std::atomic_signal_fence(std::memory_order_seq_cst)
#else
#define MORPHO_RT_PAUSE() ((void)0)
#endif

namespace morpho_phs::rt {

// PCores: one worker hard-pinned per clean physical P-core; PSet: all P-core logical CPUs (Phase 4, incl. SMT and core 0);
// CpuSet (Phase 5): soft CPU-Set placement over the clean P-core set (no SMT siblings, no core 0).
enum class PinMode : int { None = 0, PCores = 1, PSet = 2, CpuSet = 3 };

inline const char* pin_mode_name(PinMode m) noexcept {
    return m == PinMode::PCores ? "pcores" : (m == PinMode::PSet ? "pset" : (m == PinMode::CpuSet ? "cpuset" : "none"));
}

struct LogicalCpu {
    int id = 0;          // OS logical processor number (Linux cpu id; Windows: group*64 + bit)
    int group = 0;       // Windows processor group (0 elsewhere)
    int bit = 0;         // bit in the group mask (Windows) / == id (Linux)
    int core = 0;        // physical core key (unique across packages)
    int eff_class = 0;   // higher = faster core
    int smt_rank = 0;    // 0 = first logical CPU of its physical core
};

struct Topology {
    std::vector<LogicalCpu> cpus;
    int physical_cores = 0, perf_physical_cores = 0, max_eff_class = 0;
    bool hybrid = false;
    std::string source = "fallback";
};

namespace detail {
inline void finish(Topology& t) {
    std::sort(t.cpus.begin(), t.cpus.end(), [](const LogicalCpu& a, const LogicalCpu& b) { return a.id < b.id; });
    std::vector<int> seen;
    int mn = 1 << 30, mx = -1;
    for (auto& c : t.cpus) { mn = std::min(mn, c.eff_class); mx = std::max(mx, c.eff_class); }
    t.max_eff_class = std::max(mx, 0); t.hybrid = !t.cpus.empty() && mn != mx;
    std::vector<std::pair<int, int>> rank;   // core -> count
    for (auto& c : t.cpus) {
        auto it = std::find_if(rank.begin(), rank.end(), [&](auto& p) { return p.first == c.core; });
        if (it == rank.end()) { rank.push_back({c.core, 1}); c.smt_rank = 0; }
        else c.smt_rank = it->second++;
    }
    t.physical_cores = int(rank.size());
    t.perf_physical_cores = 0;
    for (auto& c : t.cpus) t.perf_physical_cores += (c.smt_rank == 0 && c.eff_class == t.max_eff_class);
}
#if defined(__linux__)
inline std::vector<int> parse_cpu_list(const std::string& s) {   // "0-3,8,10-11"
    std::vector<int> v; std::stringstream ss(s); std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) continue;
        const auto d = tok.find('-');
        try {
            if (d == std::string::npos) v.push_back(std::stoi(tok));
            else for (int a = std::stoi(tok.substr(0, d)), b = std::stoi(tok.substr(d + 1)); a <= b; ++a) v.push_back(a);
        } catch (...) {}
    }
    return v;
}
inline bool read_line(const std::string& path, std::string& out) {
    std::ifstream f(path); if (!f) return false; std::getline(f, out); return true;
}
#endif
}  // namespace detail

inline Topology query_topology() {
    Topology t;
#if defined(_WIN32)
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len > 0) {
        std::vector<unsigned char> buf(len);
        auto* base = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, base, &len)) {
            int core = 0;
            for (DWORD off = 0; off < len;) {
                auto* e = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
                if (e->Relationship == RelationProcessorCore) {
                    const PROCESSOR_RELATIONSHIP& pr = e->Processor;
                    for (WORD g = 0; g < pr.GroupCount; ++g) {
                        const KAFFINITY m = pr.GroupMask[g].Mask;
                        for (int b = 0; b < int(sizeof(KAFFINITY) * 8); ++b)
                            if (m & (KAFFINITY(1) << b)) {
                                LogicalCpu c; c.group = pr.GroupMask[g].Group; c.bit = b; c.id = c.group * 64 + b;
                                c.core = core; c.eff_class = int(pr.EfficiencyClass);
                                t.cpus.push_back(c);
                            }
                    }
                    ++core;
                }
                off += e->Size;
            }
            t.source = "GetLogicalProcessorInformationEx";
        }
    }
#elif defined(__linux__)
    cpu_set_t allowed; CPU_ZERO(&allowed);
    const bool have_aff = sched_getaffinity(0, sizeof(allowed), &allowed) == 0;
    std::string pl, el;
    std::vector<int> pcs, ecs;
    if (detail::read_line("/sys/devices/cpu_core/cpus", pl)) pcs = detail::parse_cpu_list(pl);
    if (detail::read_line("/sys/devices/cpu_atom/cpus", el)) ecs = detail::parse_cpu_list(el);
    std::string online;
    std::vector<int> ids;
    if (detail::read_line("/sys/devices/system/cpu/online", online)) ids = detail::parse_cpu_list(online);
    for (int id : ids) {
        if (have_aff && !CPU_ISSET(id, &allowed)) continue;
        LogicalCpu c; c.id = c.bit = id;
        std::string s, pk;
        const std::string d = "/sys/devices/system/cpu/cpu" + std::to_string(id) + "/topology/";
        int core_id = id, pkg = 0;
        if (detail::read_line(d + "core_id", s)) { try { core_id = std::stoi(s); } catch (...) {} }
        if (detail::read_line(d + "physical_package_id", pk)) { try { pkg = std::stoi(pk); } catch (...) {} }
        c.core = pkg * 100000 + core_id;
        if (!pcs.empty() && !ecs.empty()) c.eff_class = std::find(pcs.begin(), pcs.end(), id) != pcs.end() ? 1 : 0;
        t.cpus.push_back(c);
    }
    if (!t.cpus.empty()) t.source = (!pcs.empty() && !ecs.empty()) ? "sysfs+cpu_core/cpu_atom" : "sysfs";
#endif
    if (t.cpus.empty()) {
        const int n = int(std::max(1u, std::thread::hardware_concurrency()));
        for (int i = 0; i < n; ++i) { LogicalCpu c; c.id = c.bit = c.core = i; t.cpus.push_back(c); }
        t.source = "fallback";
    }
    detail::finish(t);
    return t;
}

// One logical CPU per physical core of the fastest class, in OS order.
inline std::vector<LogicalCpu> perf_cpus(const Topology& t) {
    std::vector<LogicalCpu> v;
    for (auto& c : t.cpus) if (c.smt_rank == 0 && c.eff_class == t.max_eff_class) v.push_back(c);
    return v;
}
// All logical CPUs of the fastest class (SMT siblings included).
inline std::vector<LogicalCpu> perf_cpu_set(const Topology& t) {
    std::vector<LogicalCpu> v;
    for (auto& c : t.cpus) if (c.eff_class == t.max_eff_class) v.push_back(c);
    return v;
}

// Phase 5: perf_cpus() without the physical core of the lowest-numbered logical CPU (CPU 0 takes most interrupts /
// DPCs on Windows).  Falls back to perf_cpus() if that would leave nothing (e.g. a single P-core).
inline std::vector<LogicalCpu> clean_perf_cpus(const Topology& t) {
    auto v = perf_cpus(t);
    if (t.cpus.empty()) return v;
    const int core0 = t.cpus.front().core;   // cpus are sorted by id in finish()
    std::vector<LogicalCpu> c;
    for (auto& x : v) if (x.core != core0) c.push_back(x);
    return c.empty() ? v : c;
}

// Default total compute-thread count (caller included).
//   Phase 4: L <= 16 -> 1, L == 32 -> clamp(P - 1, 2, 6) (= 5 on the i9-13900H).
//   Phase 5b: L <= 16 -> 1, L == 32 -> 2 (caller + 1 worker on the clean P-core CPU set), 1 if the machine has 1 P-core.
//   Phase 5c (i9-13900H final run 2026-10-31, 3 repeats x 3750 blocks, 32x32 b128, see README section 6.5):
//     FX quartic      L32: 2T  (0/11250 overruns, worst p99/budget 0.310; 3T 0.408; 4T 2 overruns)
//     Instrument      L32: 3T  (0/11250, worst 0.480; 2T measured once: 0.626 -> too little margin)
//     HQ-fast         L32: 3T  (1/11250, worst 0.757 -- "High CPU"; 2T: 31 overruns)
//     HQ-exact        L16: 2T  (12/11250, worst 0.875 -- still marginal; 1T: 141 overruns).  HQ-exact at L32 fails at
//                              every thread count (25-56 us/sample) and is mapped to HQ-fast by the plugin (L <= 16 only).
//   The count is capped by the number of P-cores (1 on a machine with a single fast core).  1..64 stay selectable.
struct AutoLoad {
    bool instrument = false;   // instrument mode (MPE voices, multi-port drive)
    bool hq = false;           // quartic + log-cosh on-site potential (fast or exact)
    bool hq_exact = false;     // exact log-cosh (only meaningful for L <= 16 in the plugin)
};
inline int default_threads(int L, const Topology& t, const AutoLoad& w) noexcept {
    int want = 1;
    if (L <= 8) want = 1;
    else if (L <= 16) want = (w.hq && w.hq_exact) ? 2 : 1;
    else {
        want = 2;                                   // FX quartic
        if (w.instrument) want = std::max(want, 3);
        if (w.hq) want = std::max(want, 3);        // HQ-fast (HQ-exact is mapped to HQ-fast at L32)
    }
    const int p = std::max(1, t.perf_physical_cores);
    return std::clamp(want, 1, p);
}
// FX / quartic default (Phase 5b signature, kept for the CLI header and tests).
inline int default_threads(int L, const Topology& t) noexcept { return default_threads(L, t, AutoLoad{}); }

// Pin the calling thread to a set of logical CPUs (all must be in one Windows processor group).
inline bool pin_current_thread(const std::vector<LogicalCpu>& set) noexcept {
    if (set.empty()) return false;
#if defined(_WIN32)
    GROUP_AFFINITY ga{}; ga.Group = WORD(set[0].group);
    for (auto& c : set) if (c.group == set[0].group) ga.Mask |= KAFFINITY(1) << c.bit;
    return SetThreadGroupAffinity(GetCurrentThread(), &ga, nullptr) != 0;
#elif defined(__linux__)
    cpu_set_t cs; CPU_ZERO(&cs);
    for (auto& c : set) CPU_SET(c.id, &cs);
    return pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs) == 0;
#else
    return false;
#endif
}
inline bool pin_current_thread(const LogicalCpu& c) noexcept { return pin_current_thread(std::vector<LogicalCpu>{c}); }

#if defined(_WIN32)
namespace detail {
using PFN_GetSystemCpuSetInformation = BOOL(WINAPI*)(void*, ULONG, PULONG, HANDLE, ULONG);
using PFN_SetThreadSelectedCpuSets = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
using PFN_SetThreadInformation = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
inline FARPROC k32(const char* name) noexcept {
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    return k ? GetProcAddress(k, name) : nullptr;
}
}  // namespace detail
#endif

// Phase 5 soft placement: restrict the calling thread to `set` through Windows CPU Sets (the scheduler treats them as
// a preference it honours unless the system needs the CPUs) — Linux: affinity mask over the set.  Allocates on
// Windows (CPU-set table): call from prepare() / thread start only.  Returns false if unsupported or refused.
inline bool set_thread_cpuset(const std::vector<LogicalCpu>& set) noexcept {
    if (set.empty()) return false;
#if defined(_WIN32)
    auto get = reinterpret_cast<detail::PFN_GetSystemCpuSetInformation>(detail::k32("GetSystemCpuSetInformation"));
    auto put = reinterpret_cast<detail::PFN_SetThreadSelectedCpuSets>(detail::k32("SetThreadSelectedCpuSets"));
    if (!get || !put) return false;
    ULONG len = 0;
    get(nullptr, 0, &len, GetCurrentProcess(), 0);
    if (len == 0) return false;
    std::vector<unsigned char> buf(len);
    if (!get(buf.data(), len, &len, GetCurrentProcess(), 0)) return false;
    std::vector<ULONG> ids;
    // SYSTEM_CPU_SET_INFORMATION layout (winnt.h): DWORD Size; DWORD Type (0 = CpuSetInformation); DWORD Id;
    // WORD Group; BYTE LogicalProcessorIndex; ...  Parsed by offset so no particular SDK version is required.
    for (ULONG off = 0; off + 16 <= len;) {
        DWORD sz = 0, type = 0, id = 0; WORD grp = 0; BYTE lp = 0;
        std::memcpy(&sz, buf.data() + off, 4); std::memcpy(&type, buf.data() + off + 4, 4);
        std::memcpy(&id, buf.data() + off + 8, 4); std::memcpy(&grp, buf.data() + off + 12, 2); std::memcpy(&lp, buf.data() + off + 14, 1);
        if (sz == 0) break;
        if (type == 0)
            for (auto& c : set) if (c.group == int(grp) && c.bit == int(lp)) ids.push_back(id);
        off += sz;
    }
    if (ids.empty()) return false;
    return put(GetCurrentThread(), ids.data(), ULONG(ids.size())) != 0;
#elif defined(__linux__)
    return pin_current_thread(set);
#else
    return false;
#endif
}

// Phase 5: opt the calling thread out of EcoQoS / power throttling (Windows 10 1709+).  No-op (false) elsewhere.
inline bool disable_power_throttling() noexcept {
#if defined(_WIN32)
    auto f = reinterpret_cast<detail::PFN_SetThreadInformation>(detail::k32("SetThreadInformation"));
    if (!f) return false;
    struct { ULONG Version, ControlMask, StateMask; } st{1u /*THREAD_POWER_THROTTLING_CURRENT_VERSION*/,
                                                       1u /*THREAD_POWER_THROTTLING_EXECUTION_SPEED*/, 0u};
    return f(GetCurrentThread(), 3 /*ThreadPowerThrottling*/, &st, DWORD(sizeof st)) != 0;
#else
    return false;
#endif
}

// Logical CPU the calling thread is running on right now (-1 if unknown); diagnostics only.
inline int current_cpu() noexcept {
#if defined(_WIN32)
    PROCESSOR_NUMBER pn; GetCurrentProcessorNumberEx(&pn); return int(pn.Group) * 64 + int(pn.Number);
#elif defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

// MMCSS "Pro Audio" registration for the lifetime of the object (Windows); no-op elsewhere.
class MmcssScope {
public:
    MmcssScope() = default;
    explicit MmcssScope(bool enable) noexcept { if (enable) begin(); }
    ~MmcssScope() { end(); }
    MmcssScope(const MmcssScope&) = delete;
    MmcssScope& operator=(const MmcssScope&) = delete;
    bool begin() noexcept {
#if defined(_WIN32)
        if (h_) return true;
        DWORD task = 0;
        h_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        if (h_) AvSetMmThreadPriority(h_, AVRT_PRIORITY_HIGH);
        return h_ != nullptr;
#else
        return false;
#endif
    }
    void end() noexcept {
#if defined(_WIN32)
        if (h_) { AvRevertMmThreadCharacteristics(h_); h_ = nullptr; }
#endif
    }
    bool active() const noexcept {
#if defined(_WIN32)
        return h_ != nullptr;
#else
        return false;
#endif
    }
private:
#if defined(_WIN32)
    HANDLE h_ = nullptr;
#endif
};

using Clock = std::chrono::steady_clock;

struct WaitPolicy {
    long spin_ns = 50000;     // PAUSE-spin budget before yielding
    long yield_ns = 200000;   // then sched yield until this total, then block (futex / WaitOnAddress)
};

// Wait until `a` != old.  Returns true if the thread had to block in the OS.
// `sleepers` is incremented around the blocking wait so the waker can skip notify when nobody sleeps.
template <class T>
inline bool hybrid_wait(const std::atomic<T>& a, T old, const WaitPolicy& w, std::atomic<int>* sleepers) noexcept {
    if (a.load(std::memory_order_acquire) != old) return false;
    const auto t0 = Clock::now();
    const auto spin_end = t0 + std::chrono::nanoseconds(w.spin_ns), yield_end = t0 + std::chrono::nanoseconds(w.yield_ns);
    for (unsigned it = 1;; ++it) {
        MORPHO_RT_PAUSE();
        if (a.load(std::memory_order_acquire) != old) return false;
        if ((it & 31u) == 0 && Clock::now() >= spin_end) break;
    }
    while (Clock::now() < yield_end) {
        std::this_thread::yield();
        if (a.load(std::memory_order_acquire) != old) return false;
    }
    if (sleepers) sleepers->fetch_add(1, std::memory_order_seq_cst);
    while (a.load(std::memory_order_seq_cst) == old) a.wait(old, std::memory_order_seq_cst);
    if (sleepers) sleepers->fetch_sub(1, std::memory_order_relaxed);
    return true;
}

// Sense-reversing barrier: spin -> yield -> block.  The last arriver only issues an OS wake if someone sleeps.
class HybridBarrier {
public:
    void init(int n, WaitPolicy w) noexcept { n_ = n; w_ = w; count_.store(0); gen_.store(0); sleepers_.store(0); blocked_.store(0); }
    void arrive_and_wait() noexcept {
        const unsigned g = gen_.load(std::memory_order_acquire);
        if (count_.fetch_add(1, std::memory_order_acq_rel) == n_ - 1) {
            count_.store(0, std::memory_order_relaxed);
            gen_.store(g + 1, std::memory_order_seq_cst);
            if (sleepers_.load(std::memory_order_seq_cst) > 0) gen_.notify_all();
        } else if (hybrid_wait(gen_, g, w_, &sleepers_)) {
            blocked_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    long long blocked_waits() const noexcept { return blocked_.load(std::memory_order_relaxed); }
private:
    alignas(64) std::atomic<int> count_{0};
    alignas(64) std::atomic<unsigned> gen_{0};
    alignas(64) std::atomic<int> sleepers_{0};
    std::atomic<long long> blocked_{0};
    int n_ = 1;
    WaitPolicy w_{};
};

// Phase 5 in-block barrier: pure user-mode spin, sense-reversing, cache-line separated.  No yield, no futex,
// no WaitOnAddress — ever.  Cost of a pre-empted participant is bounded by the OS time slice, which is why the
// worker count is kept small (default 2 total for L32) and the oversubscription guard stays in EsavLFMT.
class SpinBarrier {
public:
    // rescue_ns: only if ONE barrier wait already took longer than this (default 1 ms; the block is lost anyway at
    // that point) the waiter starts calling std::this_thread::yield() so an oversubscribed machine cannot livelock.
    // Every barrier wait that reached this rescue state is counted once in blocked_waits(); normally 0.  rescue_ns <= 0: never.
    void init(int n, long rescue_ns = 1000000) noexcept { n_ = n; rescue_ns_ = rescue_ns; count_.store(0); gen_.store(0); rescues_.store(0); }
    void arrive_and_wait() noexcept {
        const unsigned g = gen_.load(std::memory_order_acquire);
        if (count_.fetch_add(1, std::memory_order_acq_rel) == n_ - 1) {
            count_.store(0, std::memory_order_relaxed);
            gen_.store(g + 1, std::memory_order_release);
        } else {
            unsigned k = 0; bool timed = false, rescued = false; std::chrono::steady_clock::time_point t0{};
            while (gen_.load(std::memory_order_acquire) == g) {
                MORPHO_RT_PAUSE();
                if (rescue_ns_ > 0 && ((++k & 255u) == 0)) {   // clock read only every 256 PAUSEs: wake-up latency unchanged
                    const auto now = std::chrono::steady_clock::now();
                    if (!timed) { t0 = now; timed = true; }
                    else if (now - t0 > std::chrono::nanoseconds(rescue_ns_)) {
                        if (!rescued) { rescued = true; rescues_.fetch_add(1, std::memory_order_relaxed); }   // count waits, not yields
                        std::this_thread::yield();
                    }
                }
            }
        }
    }
    long long blocked_waits() const noexcept { return rescues_.load(std::memory_order_relaxed); }
private:
    alignas(64) std::atomic<int> count_{0};
    alignas(64) std::atomic<unsigned> gen_{0};
    alignas(64) int n_ = 1;
    long rescue_ns_ = 1000000;
    alignas(64) std::atomic<long long> rescues_{0};
    char pad_[64 - sizeof(long long)] = {};
};

}  // namespace morpho_phs::rt
