// SPDX-License-Identifier: AGPL-3.0-only
// Estimates effective core clock from a chain of dependent 64-bit IMULs (latency 3 cycles on Intel P-cores since
// Sandy Bridge). (A dependent ADD chain is not usable: Golden/Raptor Cove fold small immediate adds at rename.)
#include <chrono>
#include <cstdio>
#include <cstdint>
int main() {
    const uint64_t n = 800000000ULL; double best = 0;
    for (int r = 0; r < 5; ++r) {
        uint64_t x = r; auto t0 = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < n; i += 8) { asm volatile("imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0\n imul %0,%0" : "+r"(x)); }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double ghz = 3.0 * n / s / 1e9; if (ghz > best) best = ghz; std::printf("rep %d: %.3f GHz (x=%llu)\n", r, ghz, (unsigned long long)x);
    }
    std::printf("effective_clock_GHz_max %.3f\n", best);
}
