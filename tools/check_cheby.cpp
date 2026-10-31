// SPDX-License-Identifier: AGPL-3.0-only
// Prints |H| (dB) of the C++ in-gap Cheby-II bank per gap on a frequency grid (compared with scipy by p3_check_cheby.py).
#include "../plugin/Source/engine/ChebyBank.hpp"
#include <cstdio>
using namespace morpho_plugin;
int main() {
    InGapBank bank; bank.design(48000.0, 1.0);
    for (int g = 0; g < 3; ++g) for (int i = 1; i < 2400; ++i) {
        const double f = i * 10.0, w = 2 * M_PI * f / 48000.0; std::complex<double> H = 1;
        for (auto& b : bank.sections(g)) H *= b.response(w);
        std::printf("%d,%.1f,%.6f\n", g, f, 20 * std::log10(std::abs(H) + 1e-300));
    }
}
