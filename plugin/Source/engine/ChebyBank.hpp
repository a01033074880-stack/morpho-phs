// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS plugin — in-gap filter bank: one Chebyshev-II band-pass per bulk gap (order 2N = 12, Rs = 80 dB),
// the C++ equivalent of scipy.signal.cheby2(6, 80, [lo, hi], "bandpass") used in Phase 2 (stop-band edges = gap
// edges).  Design is allocation-free and cheap (~microseconds), so it may run on the audio thread at control rate
// when the Tone knob moves the gaps.  Verified against scipy by tools/check_cheby.cpp + p3_check_cheby.py.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#ifndef M_PI  // MORPHO_M_PI_GUARD: MSVC/MinGW do not define M_PI without _USE_MATH_DEFINES
#define M_PI 3.14159265358979323846
#endif

namespace morpho_plugin {

struct Biquad {   // transposed direct form II, double state
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process(double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() noexcept { z1 = z2 = 0; }
    std::complex<double> response(double w) const noexcept {   // H(e^{jw})
        const std::complex<double> z1c = std::polar(1.0, -w), z2c = std::polar(1.0, -2 * w);
        return (b0 + b1 * z1c + b2 * z2c) / (1.0 + a1 * z1c + a2 * z2c);
    }
};

// Design a Chebyshev-II band-pass (prototype order N even, N <= 8) with stop-band edges f_lo, f_hi [Hz].
// Writes N biquads (each normalised to unit gain at the band centre, so the cascade has unit centre gain).
template <int N>
inline void design_cheby2_bandpass(double rs_db, double f_lo, double f_hi, double fs, std::array<Biquad, N>& out) noexcept {
    static_assert(N % 2 == 0 && N <= 8, "even prototype order");
    using C = std::complex<double>;
    const double fs2 = 2.0 * fs;
    const double wl = fs2 * std::tan(M_PI * f_lo / fs), wh = fs2 * std::tan(M_PI * f_hi / fs);
    const double wo = std::sqrt(wl * wh), bw = wh - wl;
    const double de = 1.0 / std::sqrt(std::pow(10.0, 0.1 * rs_db) - 1.0), mu = std::asinh(1.0 / de) / N;
    // upper-half-plane prototype poles / zeros (k = 0..N/2-1), as in scipy.signal.cheb2ap
    std::array<C, N / 2> pl{}, zl{};
    for (int k = 0; k < N / 2; ++k) {
        const int m = -N + 1 + 2 * k;                    // odd, negative -> Im(p_raw) < 0 before inversion
        const C praw = -std::exp(C(0, M_PI * m / (2.0 * N)));
        const C p = C(std::sinh(mu) * praw.real(), std::cosh(mu) * praw.imag());
        pl[k] = 1.0 / p;                                  // Cheby-II poles are reciprocals
        zl[k] = -std::conj(C(0, 1) / std::sin(m * M_PI / (2.0 * N)));
        if (pl[k].imag() < 0) pl[k] = std::conj(pl[k]);
        if (zl[k].imag() < 0) zl[k] = std::conj(zl[k]);
    }
    // LP -> BP: each upper LP root r gives two roots r*bw/2 +- sqrt((r*bw/2)^2 - wo^2); their conjugates come from conj(r).
    std::array<C, N> pb{}, zb{};
    for (int k = 0; k < N / 2; ++k) {
        const C pp = pl[k] * (bw / 2), dp = std::sqrt(pp * pp - wo * wo);
        const C zz = zl[k] * (bw / 2), dz = std::sqrt(zz * zz - wo * wo);
        pb[2 * k] = pp + dp; pb[2 * k + 1] = pp - dp; zb[2 * k] = zz + dz; zb[2 * k + 1] = zz - dz;
    }
    // bilinear transform, then make every root a representative of its conjugate pair (Im >= 0)
    auto bil = [&](C s) { return (fs2 + s) / (fs2 - s); };
    std::array<C, N> pz{}, zz{};
    for (int k = 0; k < N; ++k) {
        pz[k] = bil(pb[k]); zz[k] = bil(zb[k]);
        if (pz[k].imag() < 0) pz[k] = std::conj(pz[k]);
        if (zz[k].imag() < 0) zz[k] = std::conj(zz[k]);
    }
    // pair each pole pair with the zero pair closest in angle (greedy, sorted by angle)
    std::sort(pz.begin(), pz.end(), [](C a, C b) { return std::arg(a) < std::arg(b); });
    std::sort(zz.begin(), zz.end(), [](C a, C b) { return std::arg(a) < std::arg(b); });
    const double wc = 2.0 * std::atan(wo / fs2);       // digital band centre
    for (int k = 0; k < N; ++k) {
        Biquad& b = out[k];
        b.a1 = -2.0 * pz[k].real(); b.a2 = std::norm(pz[k]);
        b.b0 = 1.0; b.b1 = -2.0 * zz[k].real(); b.b2 = std::norm(zz[k]);
        const double g = 1.0 / std::max(1e-300, std::abs(b.response(wc)));
        b.b0 *= g; b.b1 *= g; b.b2 *= g;
    }
}

// Three-gap in-gap bank.  Nominal gap edges (Phase 2: f_unit = 1500 Hz, |g| = 0.6 omega_u) scale linearly with Tone.
class InGapBank {
public:
    static constexpr int kGaps = 3, kOrder = 6;
    static constexpr double kNominal[kGaps][2] = {{448.0, 1104.0}, {1500.0, 2229.0}, {2400.0, 2794.0}};
    // tone_ratio r scales every lattice frequency (m = 1/r^2); onsite_hz2 adds a uniform on-site stiffness term
    // (the small-amplitude part of the HQ log-cosh potential, beta/m) which maps f -> sqrt(f^2 + onsite_hz2).
    void design(double fs, double tone_ratio, double onsite_hz2 = 0.0) noexcept {
        fs_ = fs; ratio_ = tone_ratio; onsite_ = onsite_hz2;
        auto map = [&](double f) { return std::sqrt(f * f * tone_ratio * tone_ratio + onsite_hz2); };
        for (int g = 0; g < kGaps; ++g) {
            const double lo = map(kNominal[g][0]), hi = std::min(map(kNominal[g][1]), 0.45 * fs);
            active_[g] = lo < 0.4 * fs && hi > lo * 1.01;
            if (active_[g]) design_cheby2_bandpass<kOrder>(80.0, lo, hi, fs, sec_[g]);
        }
    }
    // re-design only if the ratio moved by > 0.2 % (keeps filter state; transient is inaudible at control rate)
    void maybe_redesign(double fs, double tone_ratio, double onsite_hz2 = 0.0) noexcept {
        if (fs != fs_ || std::abs(tone_ratio / ratio_ - 1.0) > 2e-3 || std::abs(onsite_hz2 - onsite_) > 2e-3 * (1500.0 * 1500.0 * tone_ratio * tone_ratio)) {
            std::array<std::array<Biquad, kOrder>, kGaps> keep = sec_;
            design(fs, tone_ratio, onsite_hz2);
            for (int g = 0; g < kGaps; ++g) for (int k = 0; k < kOrder; ++k) { sec_[g][k].z1 = keep[g][k].z1; sec_[g][k].z2 = keep[g][k].z2; }
        }
    }
    void reset() noexcept { for (auto& s : sec_) for (auto& b : s) b.reset(); }
    double process(double x) noexcept {
        double acc = 0;
        for (int g = 0; g < kGaps; ++g) {
            if (!active_[g]) continue;
            double y = x; for (auto& b : sec_[g]) y = b.process(y);
            acc += y;
        }
        return acc;
    }
    const std::array<Biquad, kOrder>& sections(int g) const noexcept { return sec_[g]; }
    double ratio() const noexcept { return ratio_; }

private:
    double fs_ = 0, ratio_ = 0, onsite_ = 0;
    std::array<std::array<Biquad, kOrder>, kGaps> sec_{};
    std::array<bool, kGaps> active_{};
};

}  // namespace morpho_plugin
