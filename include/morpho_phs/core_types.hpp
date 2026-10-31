// SPDX-License-Identifier: AGPL-3.0-only
// morpho_phs — header-only C++20 core (Phase 1 prototype).  Licence: TBD by project owner.
#pragma once
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#ifndef M_PI  // MORPHO_M_PI_GUARD: MSVC/MinGW do not define M_PI without _USE_MATH_DEFINES
#define M_PI 3.14159265358979323846
#endif

namespace morpho_phs {

template <class T>
concept RealType = std::floating_point<T>;

// Per-site physical parameters (structure-of-arrays).  alpha,beta >= 0 is REQUIRED (convex U, U(0)=0)
// for the ENPU theorem; m > 0 and the CFL condition  h*omega_max(m_min)/2 < 1  for passivity.
template <RealType R>
struct SiteParams {
    std::vector<R> m, alpha, beta, mu, eta, g;
    void resize(std::size_t n) {
        for (auto* v : {&m, &alpha, &beta, &mu, &eta, &g}) v->assign(n, R(0));
        std::fill(m.begin(), m.end(), R(1));
    }
    std::size_t size() const noexcept { return m.size(); }
    // copy without reallocation (sizes must match) — used inside the audio loop
    void assign_from(const SiteParams& o) noexcept {
        std::copy(o.m.begin(), o.m.end(), m.begin());      std::copy(o.alpha.begin(), o.alpha.end(), alpha.begin());
        std::copy(o.beta.begin(), o.beta.end(), beta.begin()); std::copy(o.mu.begin(), o.mu.end(), mu.begin());
        std::copy(o.eta.begin(), o.eta.end(), eta.begin());  std::copy(o.g.begin(), o.g.end(), g.begin());
    }
};

// A collocated MPE port: force u_k * b_k injected at one site along unit direction (bx,by);
// output y_k = b_k^T vbar (power-conjugate, so the port power is u_k*y_k).
template <RealType R>
struct Port {
    int site = 0;
    R bx = 1, by = 0;
};

// Potential U(s) = alpha/4 s^4 + beta*logcosh(s)  (convex, U(0)=0, U>=0)
template <RealType R>
inline R logcosh(R x) noexcept {
    // Cancellation-free: small |x| uses log1p(2 sinh^2(x/2)) (>= 0 exactly), large |x| the asymptotic form.
    // (The naive |x| + log1p(e^{-2|x|}) - ln2 cancels catastrophically in float32 and can go negative,
    //  which made sqrt(2U+c0) NaN in an early float build.)
    const R a = std::abs(x);
    if (a < R(15)) { const R s = std::sinh(R(0.5) * a); return std::log1p(R(2) * s * s); }
    return a - R(0.69314718055994530942) + std::log1p(std::exp(R(-2) * a));
}
template <RealType R>
inline R potential(R s, R alpha, R beta) noexcept { return R(0.25) * alpha * s * s * s * s + beta * logcosh(s); }
template <RealType R>
inline R dpotential(R s, R alpha, R beta) noexcept { return alpha * s * s * s + beta * std::tanh(s); }
// Fused SAV coefficient g = U'(s)/sqrt(2U(s)+c0) for the quartic+logcosh ("HQ") potential with ONE expm1 and ONE
// log1p (instead of sinh + log1p + tanh).  With t = expm1(-|s|) (accurate, in (-1,0]):
//   logcosh|s| = log1p(t^2 / (2(1+t)))        (>= 0, cancellation-free; = log1p(2 sinh^2(|s|/2)))
//   tanh|s|    = -t(2+t) / (1 + (1+t)^2)
// For |s| >= 15 the asymptotic forms are used (tanh = 1 to working precision).
template <RealType R>
inline R sav_coeff_hq(R s, R alpha, R beta, R c0) noexcept {
    const R a = std::abs(s), s2 = s * s;
    R lc, th;
    if (a < R(15)) {
        const R t = std::expm1(-a), e = R(1) + t;
        lc = std::log1p(t * t / (R(2) * e));
        th = -t * (R(2) + t) / (R(1) + e * e);
    } else {
        lc = a - R(0.69314718055994530942); th = R(1);
    }
    const R U = R(0.25) * alpha * s2 * s2 + beta * lc;
    const R dU = alpha * s2 * s + beta * std::copysign(th, s);
    return dU / std::sqrt(R(2) * U + c0);
}

// ---- Phase 3: vectorisable convex surrogate of log cosh ("HQ-fast", Potential::QuarticLogcoshFast) --------------
//   L(s) = sum_k w_k (sqrt(c_k^2+s^2) - c_k) = sum_k w_k s^2 / (sqrt(c_k^2+s^2) + c_k)    (cancellation-free form)
//   L'(s) = sum_k w_k s / sqrt(c_k^2+s^2)                                                   (EXACT derivative of L)
// K = 3, fitted by python/scripts/phase3/p3_fit_logcosh.py with sum w = 1 (L'(inf) = 1) and sum w/c = 1 (L''(0) = 1).
// L is even, L(0) = 0, L >= 0 and convex: L''(s) = sum w c^2 (c^2+s^2)^{-3/2} > 0 (checked on a dense grid to s = 1e6
// and in the tail, sum w c^2 = 0.059 > 0; experiments/results/phase3/logcosh_surrogate_eval.json).  Hence with
// U = alpha/4 s^4 + beta L the ENPU / passivity / boundedness theorems hold EXACTLY for the surrogate potential.
// Error vs log cosh: |L - logcosh| <= 1.25e-3 for |s| <= 10, relative <= 8.9e-5 beyond (offset -> -3.05e-3);
// |L' - tanh| <= 1.23e-3 everywhere (f64 and f32).  Only sqrt and div: no exp/log -> auto-vectorises (AVX2/AVX-512).
namespace lcfast {
inline constexpr double w[3] = {8.579902105918194, 4.224340409541995, -11.804242515460189};
inline constexpr double c[3] = {2.3359531810335286, 1.569904461145531, 2.200722466602941};
}  // namespace lcfast
template <RealType R>
inline R logcosh_fast(R s) noexcept {
    const R s2 = s * s; R acc = 0;
    for (int k = 0; k < 3; ++k) { const R ck = R(lcfast::c[k]); acc += R(lcfast::w[k]) * s2 / (std::sqrt(ck * ck + s2) + ck); }
    return acc;
}
template <RealType R>
inline R dlogcosh_fast(R s) noexcept {
    const R s2 = s * s; R acc = 0;
    for (int k = 0; k < 3; ++k) { const R ck = R(lcfast::c[k]); acc += R(lcfast::w[k]) * s / std::sqrt(ck * ck + s2); }
    return acc;
}
template <RealType R>
inline R potential_fast(R s, R alpha, R beta) noexcept { return R(0.25) * alpha * s * s * s * s + beta * logcosh_fast(s); }
// Branch-free SAV coefficient for the surrogate (same structure as sav_coeff_hq).  One division per term:
// with r = sqrt(c^2+s^2) and inv = 1/(r (r+c)):  s/r = s (r+c) inv,  s^2/(r+c) = s^2 r inv  (cancellation-free).
// Cost per scalar: 4 sqrt + 4 div, all vectorisable.
template <RealType R>
inline R sav_coeff_hqf(R s, R alpha, R beta, R c0) noexcept {
    const R s2 = s * s; R L = 0, dL = 0;
    for (int k = 0; k < 3; ++k) {
        const R ck = R(lcfast::c[k]), wk = R(lcfast::w[k]), r = std::sqrt(ck * ck + s2), rc = r + ck;
        const R inv = wk / (r * rc);
        L += s2 * r * inv; dL += s * rc * inv;
    }
    return (alpha * s2 * s + beta * dL) / std::sqrt(R(2) * (R(0.25) * alpha * s2 * s2 + beta * L) + c0);
}

}  // namespace morpho_phs
