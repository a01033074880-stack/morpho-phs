// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core_types.hpp"
#include <map>
#include <tuple>

namespace morpho_phs {

// Parallelogram honeycomb flake of Lx x Ly unit cells, 2 sites/cell, open boundary.
// Site/bond/hexagon ordering is IDENTICAL to python/diffphs/gyro.py::GyroHoneycomb.
struct Honeycomb {
    int Lx = 0, Ly = 0, N = 0;
    std::vector<std::array<double, 2>> xy;
    std::vector<std::array<int, 2>> bonds;
    std::vector<std::array<double, 3>> nn;      // (nx^2, nx*ny, ny^2) of each bond's unit vector
    std::vector<int> nbr_ptr, nbr_idx;          // CSR site adjacency
    std::vector<std::array<int, 6>> hexes;      // plaquettes (for winding / topological charge)

    Honeycomb() = default;
    Honeycomb(int lx, int ly) { build(lx, ly); }

    void build(int lx, int ly) {
        Lx = lx; Ly = ly; N = 2 * lx * ly;
        const double a2x = 0.5, a2y = std::sqrt(3.0) / 2, dbx = (1 + a2x) / 3, dby = a2y / 3;
        auto id = [&](int cx, int cy, int s) { return 2 * (cy * lx + cx) + s; };
        xy.resize(N);
        for (int cy = 0; cy < ly; ++cy)
            for (int cx = 0; cx < lx; ++cx)
                for (int s = 0; s < 2; ++s)
                    xy[id(cx, cy, s)] = {cx + cy * a2x + (s ? dbx : 0.0), cy * a2y + (s ? dby : 0.0)};
        bonds.clear(); nn.clear();
        for (int cy = 0; cy < ly; ++cy)
            for (int cx = 0; cx < lx; ++cx)
                for (auto [ox, oy] : {std::pair{0, 0}, std::pair{-1, 0}, std::pair{0, -1}}) {
                    const int jx = cx + ox, jy = cy + oy;
                    if (jx < 0 || jy < 0) continue;
                    const int i = id(cx, cy, 0), j = id(jx, jy, 1);
                    double rx = xy[j][0] - xy[i][0], ry = xy[j][1] - xy[i][1], r = std::hypot(rx, ry);
                    rx /= r; ry /= r;
                    bonds.push_back({i, j}); nn.push_back({rx * rx, rx * ry, ry * ry});
                }
        std::vector<std::vector<int>> adj(N);
        for (auto [i, j] : bonds) { adj[i].push_back(j); adj[j].push_back(i); }
        nbr_ptr.assign(N + 1, 0); nbr_idx.clear();
        for (int i = 0; i < N; ++i) { nbr_ptr[i + 1] = nbr_ptr[i] + (int)adj[i].size(); for (int j : adj[i]) nbr_idx.push_back(j); }
        hexes.clear();
        for (int cy = 0; cy + 1 < ly; ++cy)
            for (int cx = 0; cx + 1 < lx; ++cx)
                hexes.push_back({id(cx, cy, 1), id(cx + 1, cy, 0), id(cx + 1, cy, 1), id(cx + 1, cy + 1, 0), id(cx, cy + 1, 1), id(cx, cy + 1, 0)});
    }
    int degree(int i) const noexcept { return nbr_ptr[i + 1] - nbr_ptr[i]; }
    int nearest_site(double x, double y) const noexcept {
        int best = 0; double bd = 1e300;
        for (int i = 0; i < N; ++i) { double d = (xy[i][0] - x) * (xy[i][0] - x) + (xy[i][1] - y) * (xy[i][1] - y); if (d < bd) { bd = d; best = i; } }
        return best;
    }
    // Upper bound on lambda_max(K): spring part <= kc * L_graph (x) I  (since n n^T <= I), and
    // lambda_max(L_graph) <= 2*max_degree  =>  lambda_max(K) <= k0 + 2*kc*max_degree   (CFL check)
    double lambda_max_bound(double k0, double kc) const noexcept {
        int dmax = 0; for (int i = 0; i < N; ++i) dmax = std::max(dmax, degree(i));
        return k0 + 2.0 * kc * dmax;
    }
};

}  // namespace morpho_phs
