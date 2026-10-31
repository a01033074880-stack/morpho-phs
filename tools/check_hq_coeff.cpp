// SPDX-License-Identifier: AGPL-3.0-only
// Accuracy of the fused HQ SAV coefficient vs a long-double reference.
#include "morpho_phs/core_types.hpp"
#include <cmath>
#include <cstdio>
#include <random>
using namespace morpho_phs;
template <class R> double run() {
    std::mt19937_64 g(7); std::uniform_real_distribution<double> us(-30, 30), ua(0, 5), ub(0, 5), uc(1e-3, 10);
    double worst = 0;
    for (int i = 0; i < 2000000; ++i) {
        double s = us(g) * std::pow(10.0, std::uniform_real_distribution<double>(-6, 0)(g)), al = ua(g), be = ub(g), c0 = uc(g);
        long double S = s, a = std::fabs(S);
        long double lc = a + std::log1p(std::exp(-2 * a)) - std::log(2.0L);
        long double U = 0.25L * al * S * S * S * S + be * lc, dU = al * S * S * S + be * std::tanh(S);
        long double ref = dU / std::sqrt(2 * U + c0);
        double got = (double)sav_coeff_hq<R>((R)s, (R)al, (R)be, (R)c0);
        // reference evaluated at the rounded inputs for float
        if constexpr (sizeof(R) == 4) { S = (R)s; a = std::fabs(S); lc = a + std::log1p(std::exp(-2 * a)) - std::log(2.0L);
            U = 0.25L * (R)al * S * S * S * S + (R)be * lc; dU = (R)al * S * S * S + (R)be * std::tanh(S); ref = dU / std::sqrt(2 * U + (R)c0); }
        if (std::fabs(ref) > 1e-300) { double e = (double)std::fabs((got - ref) / ref); if (e > worst) worst = e; }
    }
    return worst;
}
int main() { std::printf("{\"max_rel_err_f64\": %.3e, \"max_rel_err_f32\": %.3e, \"samples\": 2000000}\n", run<double>(), run<float>()); }
