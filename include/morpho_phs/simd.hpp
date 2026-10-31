// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Phase 5: minimal explicit-SIMD wrapper for the bipartite honeycomb stencil kernel (esav_lf_mt.hpp).
//
// Why explicit intrinsics instead of auto-vectorisation: the plugin ships with MSVC, whose auto-vectoriser does not
// reliably vectorise the large fused site loop; with intrinsics the same instruction sequence is produced by MSVC,
// GCC and Clang.  Every operation is a separate IEEE mul / add / sub / div / sqrt / min (NO fused multiply-add), in
// exactly the order of the scalar reference loop.  Together with -ffp-contract=off (GCC/Clang) or /fp:precise
// (MSVC, no contraction since VS2022) the SIMD kernel is therefore BIT-IDENTICAL to the scalar CSR kernel.
//
// V<R>::W lanes:  AVX2 float = 8, AVX2 double = 4.  Without AVX2 the stencil kernel uses its scalar loop.
// Interleaved bipartite layout helpers (site id = 2*cell + sublattice, A = even lane, B = odd lane):
//   swap_pairs(v)          lane j <- lane j^1                       (intra-cell partner, A<->B)
//   blend_ab(a, b)         even lanes from a, odd lanes from b      (A sites take a, B sites take b)
#include <cmath>
#include <algorithm>
#if (defined(__AVX2__) || defined(MORPHO_FORCE_AVX2)) && (defined(__x86_64__) || defined(_M_X64))
#include <immintrin.h>
#define MORPHO_SIMD_AVX2 1
#else
#define MORPHO_SIMD_AVX2 0
#endif

namespace morpho_phs::simd {

template <class R> struct V;   // defined only when AVX2 is available (MORPHO_SIMD_AVX2)

#if MORPHO_SIMD_AVX2
template <>
struct V<float> {
    static constexpr int W = 8;
    __m256 v;
    static V load(const float* p) noexcept { return {_mm256_load_ps(p)}; }
    static V loadu(const float* p) noexcept { return {_mm256_loadu_ps(p)}; }
    static V set1(float x) noexcept { return {_mm256_set1_ps(x)}; }
    void store(float* p) const noexcept { _mm256_store_ps(p, v); }
    void storeu(float* p) const noexcept { _mm256_storeu_ps(p, v); }
    friend V operator+(V a, V b) noexcept { return {_mm256_add_ps(a.v, b.v)}; }
    friend V operator-(V a, V b) noexcept { return {_mm256_sub_ps(a.v, b.v)}; }
    friend V operator*(V a, V b) noexcept { return {_mm256_mul_ps(a.v, b.v)}; }
    friend V operator/(V a, V b) noexcept { return {_mm256_div_ps(a.v, b.v)}; }
    friend V neg(V a) noexcept { return {_mm256_xor_ps(a.v, _mm256_set1_ps(-0.0f))}; }
    friend V vsqrt(V a) noexcept { return {_mm256_sqrt_ps(a.v)}; }
    // std::min(a, b) returns (b < a) ? b : a ; _mm256_min_ps(x, y) returns (x < y) ? x : y  ->  min_ps(b, a)
    friend V vmin(V a, V b) noexcept { return {_mm256_min_ps(b.v, a.v)}; }
    friend V vabs(V a) noexcept { return {_mm256_andnot_ps(_mm256_set1_ps(-0.0f), a.v)}; }
    friend V vcopysign(V mag, V sgn) noexcept {
        const __m256 m = _mm256_set1_ps(-0.0f);
        return {_mm256_or_ps(_mm256_andnot_ps(m, mag.v), _mm256_and_ps(m, sgn.v))};
    }
    static V swap_pairs(V a) noexcept { return {_mm256_permute_ps(a.v, 0xB1)}; }
    static V blend_ab(V a, V b) noexcept { return {_mm256_blend_ps(a.v, b.v, 0xAA)}; }
};
template <>
struct V<double> {
    static constexpr int W = 4;
    __m256d v;
    static V load(const double* p) noexcept { return {_mm256_load_pd(p)}; }
    static V loadu(const double* p) noexcept { return {_mm256_loadu_pd(p)}; }
    static V set1(double x) noexcept { return {_mm256_set1_pd(x)}; }
    void store(double* p) const noexcept { _mm256_store_pd(p, v); }
    void storeu(double* p) const noexcept { _mm256_storeu_pd(p, v); }
    friend V operator+(V a, V b) noexcept { return {_mm256_add_pd(a.v, b.v)}; }
    friend V operator-(V a, V b) noexcept { return {_mm256_sub_pd(a.v, b.v)}; }
    friend V operator*(V a, V b) noexcept { return {_mm256_mul_pd(a.v, b.v)}; }
    friend V operator/(V a, V b) noexcept { return {_mm256_div_pd(a.v, b.v)}; }
    friend V neg(V a) noexcept { return {_mm256_xor_pd(a.v, _mm256_set1_pd(-0.0))}; }
    friend V vsqrt(V a) noexcept { return {_mm256_sqrt_pd(a.v)}; }
    friend V vmin(V a, V b) noexcept { return {_mm256_min_pd(b.v, a.v)}; }
    friend V vabs(V a) noexcept { return {_mm256_andnot_pd(_mm256_set1_pd(-0.0), a.v)}; }
    friend V vcopysign(V mag, V sgn) noexcept {
        const __m256d m = _mm256_set1_pd(-0.0);
        return {_mm256_or_pd(_mm256_andnot_pd(m, mag.v), _mm256_and_pd(m, sgn.v))};
    }
    static V swap_pairs(V a) noexcept { return {_mm256_permute_pd(a.v, 0x5)}; }
    static V blend_ab(V a, V b) noexcept { return {_mm256_blend_pd(a.v, b.v, 0xA)}; }
};
inline constexpr bool kHaveAvx2 = true;
#else
inline constexpr bool kHaveAvx2 = false;   // stencil kernel falls back to its scalar loop
#endif

}  // namespace morpho_phs::simd
