#include "world.h"
#include <functional>
#include <algorithm>
#include <cmath>
#include <queue>

// Rivers: "priority flood" from the sea inland. Each land cell drains toward the cell it was
// reached from, so water always finds a way out of basins (filling them as lakes first).
// Flow is accumulated from rainfall minus evaporation, giving discharge in m3/s.

void World::generateRivers() {
    const int N = W * H;
    const double ck = cellKm();
    flowTo.assign(N, -1);
    discharge.assign(N, 0);
    lakeType.assign(N, 0);
    std::vector<float> level(N, 0);
    std::vector<uint8_t> done(N, 0);
    std::vector<int> order;
    order.reserve(N);

    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (int i = 0; i < N; i++)
        if (elev[i] <= 0) { level[i] = elev[i]; done[i] = 1; flowTo[i] = -1; }
    // seed with coastal sea cells (sea cells next to land)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            if (elev[i] > 0) continue;
            bool coast = false;
            for (int dy = -1; dy <= 1 && !coast; dy++)
                for (int dx = -1; dx <= 1; dx++) if (elev[idx(x + dx, y + dy)] > 0) { coast = true; break; }
            if (coast) pq.push({0.f, i});
        }
    while (!pq.empty()) {
        auto [lv, i] = pq.top(); pq.pop();
        int x = i % W, y = i / W;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                if (y + dy < 0 || y + dy >= H) continue;
                int n = idx(x + dx, y + dy);
                if (done[n]) continue;
                done[n] = 1;
                level[n] = std::max(elev[n], lv + 0.01f);
                flowTo[n] = elev[i] <= 0 ? -1 : i;
                if (elev[i] <= 0) flowTo[n] = i;   // flows into the sea cell
                order.push_back(n);
                pq.push({level[n], n});
            }
    }

    // Runoff: rain that isn't evaporated, accumulated downstream (highest cells first)
    for (int i = 0; i < N; i++) {
        if (elev[i] <= 0) continue;
        double T = tempYear.empty() ? 10 : tempYear[i];
        double P = rainfallYear.empty() ? 600 : rainfallYear[i];
        double pet = std::max(150.0, 250 + 35 * std::max(0.0, T));
        double runoffMm = std::max(0.0, P - 0.62 * std::min(P, pet) - 0.25 * std::max(0.0, pet - P) * 0.0);
        double areaM2 = ck * 1000 * ck * 1000 * std::max(0.02, std::cos(cellLat(i / W) * kPi / 180));
        discharge[i] = (float)(runoffMm / 1000.0 * areaM2 / 3.156e7);
    }
    for (int k = (int)order.size() - 1; k >= 0; k--) {
        int i = order[k];
        int d = flowTo[i];
        if (d >= 0 && elev[d] > 0) discharge[d] += discharge[i];
    }
    maxDischarge = 0;
    for (int i = 0; i < N; i++) if (elev[i] > 0) maxDischarge = std::max(maxDischarge, discharge[i]);

    // Lakes where basins filled up; in dry places they dry out into salt flats
    lakeSurface.assign(N, -20000);
    lakeMask.assign(N, 0);
    saltMask.assign(N, 0);
    for (int i = 0; i < N; i++) {
        if (elev[i] <= 0) continue;
        double depth = level[i] - elev[i];
        if (depth > 12) {
            double P = rainfallYear.empty() ? 600 : rainfallYear[i];
            double T = tempYear.empty() ? 10 : tempYear[i];
            bool wet = P > 450 || discharge[i] > 400 || T < 2;   // cold basins hold (frozen) lakes
            lakeType[i] = wet ? 1 : 2;
            if (wet) { lakeSurface[i] = level[i]; lakeMask[i] = 1; }
            else saltMask[i] = 1;
        }
    }

    // Soften salt flat edges so they don't look like squares
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float> s2(N);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                s2[i] = (saltMask[i] * 4 + saltMask[idx(x + 1, y)] + saltMask[idx(x - 1, y)] + saltMask[idx(x, y + 1)] + saltMask[idx(x, y - 1)]) / 8;
            }
        saltMask.swap(s2);
    }
    // Spread each lake's level one cell outwards so the shoreline can follow the fine terrain
    {
        std::vector<float> ls = lakeSurface;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                if (lakeMask[i] > 0) continue;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int n = idx(x + dx, y + dy);
                        if (lakeMask[n] > 0) ls[i] = std::max(ls[i], lakeSurface[n]);
                    }
            }
        lakeSurface.swap(ls);
    }

    // River lines: start at headwaters where flow first passes a threshold, follow downstream
    rivers.clear();
    const float minQ = 18;   // m3/s (small streams are only drawn when zoomed in)
    std::vector<uint8_t> used(N, 0);
    std::vector<int> heads;
    for (int i = 0; i < N; i++) {
        if (elev[i] <= 0 || discharge[i] < minQ) continue;
        // a head has no upstream cell above the threshold
        int x = i % W, y = i / W;
        bool hasUp = false;
        for (int dy = -1; dy <= 1 && !hasUp; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int n = idx(x + dx, y + dy);
                if (n != i && flowTo[n] == i && discharge[n] >= minQ) { hasUp = true; break; }
            }
        if (!hasUp) heads.push_back(i);
    }
    // longest rivers first, so tributaries stop where they join
    std::sort(heads.begin(), heads.end(), [&](int a, int b) { return elev[a] > elev[b]; });
    std::vector<std::pair<float, int>> byLen;
    for (int h : heads) {
        int c = h, len = 0;
        while (c >= 0 && elev[c] > 0 && len < 4000) { c = flowTo[c]; len++; }
        byLen.push_back({(float)len, h});
    }
    std::sort(byLen.begin(), byLen.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& bl : byLen) {
        River r;
        int c = bl.second, guard = 0;
        while (c >= 0 && guard++ < 4000) {
            r.pts.push_back(cellDir[c]);
            r.q.push_back(discharge[c]);
            if (elev[c] <= 0) break;
            if (used[c] && r.pts.size() > 1) break;   // joined an existing river
            if (lakeType[c] == 1 && r.pts.size() > 1 && discharge[c] < 3 * minQ) { /* keep flowing through the lake */ }
            used[c] = 1;
            c = flowTo[c];
        }
        r.mouthCell = c;
        if (r.pts.size() >= 3) rivers.push_back(std::move(r));
    }

    // Smooth the lines (Chaikin), then meander: bends sideways to the flow, wandering more on
    // flat lowlands than in steep mountain valleys, with bend size growing with the river
    Simplex3 meander(5151);
    for (River& r : rivers) {
        for (int pass = 0; pass < 3; pass++) {
            std::vector<Vec3d> p2; std::vector<float> q2;
            p2.push_back(r.pts.front()); q2.push_back(r.q.front());
            for (size_t k = 0; k + 1 < r.pts.size(); k++) {
                p2.push_back((r.pts[k] * 0.75 + r.pts[k + 1] * 0.25).normalized());
                p2.push_back((r.pts[k] * 0.25 + r.pts[k + 1] * 0.75).normalized());
                q2.push_back(r.q[k]); q2.push_back(r.q[k + 1]);
            }
            p2.push_back(r.pts.back()); q2.push_back(r.q.back());
            r.pts.swap(p2); r.q.swap(q2);
        }
        if (r.pts.size() < 4) continue;
        std::vector<Vec3d> out = r.pts;
        double along = 0;   // km from the source
        const double seedOff = (r.pts.front().x * 1000.0);
        for (size_t k = 1; k + 1 < r.pts.size(); k++) {
            along += angleBetween(r.pts[k - 1], r.pts[k]) * radiusKm;
            Vec3d t = (r.pts[k + 1] - r.pts[k - 1]);
            if (t.length() < 1e-12) continue;
            Vec3d side = r.pts[k].cross(t).normalized();
            double lat = std::asin(clampd(r.pts[k].y, -1, 1)) * 180 / kPi, lon = std::atan2(r.pts[k].x, r.pts[k].z) * 180 / kPi;
            double h = sample(elev, lat, lon);
            double flat = 1 - smoothstepd(150, 1500, h);        // lowland rivers wander most
            double wl = 25 + 6 * std::sqrt(std::max(1.0f, r.q[k]) / 10.0);   // bend wavelength, km
            double m = meander(along / wl, seedOff, 0.5) * 0.7 + meander(along / (wl * 0.37), seedOff + 7.3, 1.5) * 0.3;
            double ampKm = wl * (0.12 + 0.28 * flat);
            // keep the very end and start in place so mouths and junctions still meet
            double ends = std::min(1.0, std::min((double)k, (double)(r.pts.size() - 1 - k)) / 4.0);
            out[k] = (r.pts[k] + side * (m * ampKm * ends / radiusKm)).normalized();
        }
        r.pts.swap(out);
    }
}

void World::fitRiversToCoast(const std::function<double(const Vec3d&)>& fineElevation) {
    // the drawn map has a finer coastline than this grid: end each river where it first reaches
    // the sea, and drop rivers that start in water
    std::vector<River> kept;
    auto fine = [&](const Vec3d& d) {
        double lat = std::asin(clampd(d.y, -1, 1)) * 180 / kPi, lon = std::atan2(d.x, d.z) * 180 / kPi;
        double coarse = sample(elev, lat, lon);
        return coarse > 400 ? coarse : fineElevation(d);   // only look closely near the coast
    };
    for (River& r : rivers) {
        size_t cut = r.pts.size();
        for (size_t k = 1; k < r.pts.size(); k++)
            if (fine(r.pts[k]) < -2) { cut = k + 1; break; }
        if (fine(r.pts.front()) < 0) continue;
        r.pts.resize(std::min(cut, r.pts.size())); r.q.resize(r.pts.size());
        if (r.pts.size() >= 3) kept.push_back(std::move(r));
    }
    rivers.swap(kept);
}

void World::generateErosion(double strength) {
    const int N = W * H;
    erosion.assign(N, 0);
    if (strength <= 0 || discharge.empty()) return;
    std::vector<float> e(N, 0);
    std::vector<uint8_t> fjordMask(N, 0);
    for (int i = 0; i < N; i++) {
        if (elev[i] <= 0) continue;
        // rivers cut valleys: deeper for bigger rivers, more where the land is high above the sea
        double q = discharge[i];
        double cut = q > 20 ? std::min(450.0, 55.0 * std::log10(1 + q / 40.0)) * smoothstepd(0, 600, elev[i]) : 0;
        // time wears mountains down: old rock is lower and smoother
        double age = rockAge.empty() ? 100 : rockAge[i];
        double wear = std::max(0.0f, elev[i] - 400.0f) * 0.32 * smoothstepd(150, 1500, age);
        // glaciers on cold coasts dig deep troughs that the sea later floods: fjords
        double fjord = 0;
        if (!tempYear.empty() && !distOceanKm.empty() && tempYear[i] < 2 && distOceanKm[i] < 350 && q > 30)
            fjord = std::min(900.0, 120.0 * std::log10(1 + q / 20.0)) * smoothstepd(2, -4, tempYear[i]) * (1 - distOceanKm[i] / 350.0);
        fjordMask[i] = fjord > 50 ? 1 : 0;
        e[i] = (float)(-(cut + wear + fjord) * strength);
    }
    // valleys are wider than one cell: spread a little
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float> e2(N);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                float m = e[i];
                float avg = (e[idx(x + 1, y)] + e[idx(x - 1, y)] + e[idx(x, y + 1)] + e[idx(x, y - 1)]) / 4;
                e2[i] = std::min(m, (m * 2 + avg) / 3);
            }
        e.swap(e2);
    }
    // never carve land below sea level (that would turn valleys into fjords everywhere)
    for (int i = 0; i < N; i++) {
        if (elev[i] <= 0) e[i] = 0;
        else if (!fjordMask[i]) e[i] = std::max(e[i], -(elev[i] - 15.0f));
    }
    erosion.swap(e);
}
