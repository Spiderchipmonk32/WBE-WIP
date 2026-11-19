#include "world.h"
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
    lakeSurface.assign(N, 0);
    saltMask.assign(N, 0);
    for (int i = 0; i < N; i++) {
        lakeSurface[i] = elev[i] - 60;
        if (elev[i] <= 0) continue;
        double depth = level[i] - elev[i];
        if (depth > 4) {
            double P = rainfallYear.empty() ? 600 : rainfallYear[i];
            double T = tempYear.empty() ? 10 : tempYear[i];
            bool wet = P > 450 || discharge[i] > 400 || T < 2;   // cold basins hold (frozen) lakes
            lakeType[i] = wet ? 1 : 2;
            if (wet) lakeSurface[i] = level[i];
            else saltMask[i] = 1;
        }
    }

    // River lines: start at headwaters where flow first passes a threshold, follow downstream
    rivers.clear();
    const float minQ = 60;   // m3/s
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

    // Smooth the lines (Chaikin) and add a gentle meander so they don't follow the grid
    Simplex3 meander(5151);
    for (River& r : rivers) {
        for (int pass = 0; pass < 2; pass++) {
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
        for (size_t k = 1; k + 1 < r.pts.size(); k++) {
            Vec3d& p = r.pts[k];
            Vec3d off{meander(p.x * 40, p.y * 40, p.z * 40), meander(p.x * 40 + 9, p.y * 40, p.z * 40), meander(p.x * 40, p.y * 40 + 17, p.z * 40)};
            p = (p + off * (ck / radiusKm * 0.35)).normalized();
        }
    }
}
