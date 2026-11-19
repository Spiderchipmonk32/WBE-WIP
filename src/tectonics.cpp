#include "world.h"
#include "planet.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace {
uint32_t rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
double rnd01(uint32_t& s) { return (rng(s) >> 8) / 16777216.0; }
Vec3d randomDir(uint32_t& s) {
    double u = rnd01(s) * 2 - 1, a = rnd01(s) * 2 * kPi, r = std::sqrt(1 - u * u);
    return {r * std::cos(a), u, r * std::sin(a)};
}
}

void World::cellOf(double lat, double lon, int& x, int& y) {
    x = (int)std::floor((lon + 180.0) / 360.0 * W);
    y = (int)std::floor((90.0 - lat) / 180.0 * H);
    x = wrapX(x); y = clampY(y);
}

double World::sample(const std::vector<float>& f, double lat, double lon) {
    if (f.empty()) return 0;
    double fx = (lon + 180.0) / 360.0 * W - 0.5, fy = (90.0 - lat) / 180.0 * H - 0.5;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    double tx = fx - x0, ty = fy - y0;
    double a = f[idx(x0, y0)], b = f[idx(x0 + 1, y0)], c = f[idx(x0, y0 + 1)], d = f[idx(x0 + 1, y0 + 1)];
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

void World::initGrid(double radius) {
    radiusKm = radius;
    cellDir.resize(W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double la = cellLat(y) * kPi / 180, lo = cellLon(x) * kPi / 180;
            cellDir[y * W + x] = {std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)};
        }
}

void World::sampleElevation(const Planet& planet) {
    elev.resize(W * H);
    for (int i = 0; i < W * H; i++) elev[i] = (float)planet.elevation(cellDir[i], 3);
}

Vec3d World::plateVelocity(int cell) const {
    const Plate& p = plates[plateId[cell]];
    return p.pole.cross(cellDir[cell]) * p.speed;
}

const char* World::rockName(Rock r) {
    switch (r) {
        case Rock::OceanicBasalt: return "Oceanic basalt";
        case Rock::Sedimentary: return "Sedimentary (sandstone, shale, limestone)";
        case Rock::Volcanic: return "Volcanic (basalt, andesite)";
        case Rock::Plutonic: return "Plutonic (granite)";
        case Rock::Metamorphic: return "Metamorphic (gneiss, schist, marble)";
        case Rock::Shield: return "Ancient shield (craton)";
        default: return "?";
    }
}

const char* World::boundaryName(Boundary b) {
    switch (b) {
        case Boundary::Collision: return "Continental collision";
        case Boundary::Subduction: return "Subduction zone";
        case Boundary::IslandArc: return "Island arc";
        case Boundary::Rift: return "Continental rift";
        case Boundary::Ridge: return "Mid-ocean ridge";
        case Boundary::Transform: return "Transform fault";
        default: return "None";
    }
}

void World::generateTectonics(const Planet& planet, int seed, int plateCount, double activity) {
    initGrid(planet.radiusKm());
    const int N = W * H;
    uint32_t s = (uint32_t)seed * 7919u + 17u;
    Simplex3 warp((uint32_t)seed + 900), ageN((uint32_t)seed + 901);

    // 1. Plates: random centres; cells join the nearest centre (with warped distances for ragged edges)
    plateCount = std::max(3, std::min(40, plateCount));
    plates.clear();
    for (int i = 0; i < plateCount; i++) {
        Plate p;
        p.seed = randomDir(s);
        p.pole = randomDir(s);
        p.speed = 0.3 + rnd01(s) * 0.9;
        p.continental = false;
        p.r = (unsigned char)(70 + rnd01(s) * 170); p.g = (unsigned char)(70 + rnd01(s) * 170); p.b = (unsigned char)(70 + rnd01(s) * 170);
        plates.push_back(p);
    }
    plateId.assign(N, 0);
    std::vector<double> weight(plateCount);
    for (int i = 0; i < plateCount; i++) weight[i] = 0.75 + rnd01(s) * 0.6;   // some plates are bigger than others
    for (int i = 0; i < N; i++) {
        const Vec3d& d = cellDir[i];
        Vec3d q = d + Vec3d{warp(d.x * 2.5, d.y * 2.5, d.z * 2.5), warp(d.x * 2.5 + 7, d.y * 2.5, d.z * 2.5),
                            warp(d.x * 2.5, d.y * 2.5 + 13, d.z * 2.5)} * 0.16
                    + Vec3d{warp(d.x * 9, d.y * 9, d.z * 9 + 3), warp(d.x * 9 + 5, d.y * 9, d.z * 9), warp(d.x * 9, d.y * 9 + 9, d.z * 9)} * 0.04;
        double best = 1e9; int bi = 0;
        for (int p = 0; p < plateCount; p++) {
            double a = angleBetween(q, plates[p].seed) / weight[p];
            if (a < best) { best = a; bi = p; }
        }
        plateId[i] = (uint8_t)bi;
    }

    // 2. Continental or oceanic: decided by how much of the plate is land in the base terrain
    std::vector<double> landArea(plateCount, 0), area(plateCount, 0);
    for (int i = 0; i < N; i++) {
        double w = std::cos(cellLat(i / W) * kPi / 180);
        area[plateId[i]] += w;
        if (planet.elevation(cellDir[i], 0) > 0) landArea[plateId[i]] += w;
    }
    for (int p = 0; p < plateCount; p++) plates[p].continental = landArea[p] > 0.25 * area[p];

    // 3. Boundaries: compare the motion of neighbouring plates
    boundary.assign(N, (uint8_t)Boundary::None);
    stress.assign(N, 0);
    std::vector<float> conv(N, 0);
    std::vector<uint8_t> otherPlate(N, 0);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            const int nb[4] = {idx(x + 1, y), idx(x - 1, y), idx(x, y + 1), idx(x, y - 1)};
            for (int n : nb) {
                if (plateId[n] == plateId[i]) continue;
                Vec3d dirTo = (cellDir[n] - cellDir[i]).normalized();
                Vec3d rel = plateVelocity(n) - plateVelocity(i);
                double c = -rel.dot(dirTo);                       // >0: plates moving together
                double t = (rel - dirTo * rel.dot(dirTo)).length();  // sliding past
                const Plate& a = plates[plateId[i]];
                const Plate& b = plates[plateId[n]];
                Boundary k;
                if (c > 0.25 * t && c > 0.15) {
                    if (a.continental && b.continental) k = Boundary::Collision;
                    else if (a.continental || b.continental) k = Boundary::Subduction;
                    else k = Boundary::IslandArc;
                } else if (-c > 0.25 * t && -c > 0.15) {
                    k = (a.continental && b.continental) ? Boundary::Rift : Boundary::Ridge;
                } else {
                    k = Boundary::Transform;
                }
                boundary[i] = (uint8_t)k;
                conv[i] = (float)c;
                otherPlate[i] = plateId[n];
                stress[i] = (float)std::min(1.0, (std::fabs(c) + t) / 1.6);
                break;
            }
        }
    }

    // 4. Distance from each cell to the nearest boundary on its own plate (Dijkstra on the grid)
    const double cellNS = cellKm();
    distBoundaryKm.assign(N, 1e9f);
    std::vector<int> src(N, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (int i = 0; i < N; i++)
        if (boundary[i] != (uint8_t)Boundary::None) { distBoundaryKm[i] = 0; src[i] = i; pq.push({0.f, i}); }
    const float maxDist = 1500;
    while (!pq.empty()) {
        auto [d, i] = pq.top(); pq.pop();
        if (d > distBoundaryKm[i] || d > maxDist) continue;
        int x = i % W, y = i / W;
        double ew = std::max(0.05, std::cos(cellLat(y) * kPi / 180)) * cellNS;
        const int nb[4] = {idx(x + 1, y), idx(x - 1, y), idx(x, y + 1), idx(x, y - 1)};
        const double step[4] = {ew, ew, cellNS, cellNS};
        for (int k = 0; k < 4; k++) {
            int n = nb[k];
            if (plateId[n] != plateId[i]) continue;
            float nd = (float)(d + step[k]);
            if (nd < distBoundaryKm[n]) { distBoundaryKm[n] = nd; src[n] = src[i]; pq.push({nd, n}); }
        }
    }

    // 5. Uplift from each boundary type
    uplift.assign(N, 0);
    for (int i = 0; i < N; i++) {
        if (src[i] < 0) continue;
        int b = src[i];
        double d = distBoundaryKm[i];
        double str = clampd(std::fabs(conv[b]) / 1.2, 0.2, 1.0) * activity;
        bool selfCont = plates[plateId[i]].continental;
        bool otherCont = plates[otherPlate[b]].continental;
        double u = 0;
        switch ((Boundary)boundary[b]) {
            case Boundary::Collision:   // Himalaya-style: broad high ranges and plateaus on both sides
                u = 3600 * str * std::exp(-std::pow(d / 380.0, 2.0)) + 900 * str * std::exp(-std::pow(d / 900.0, 2.0));
                break;
            case Boundary::Subduction:
                if (selfCont) u = 3000 * str * std::exp(-std::pow((d - 120) / 220.0, 2.0));     // Andes-style coastal range
                else u = -3800 * str * std::exp(-std::pow(d / 90.0, 2.0));                    // ocean trench
                break;
            case Boundary::IslandArc:
                if (plateId[i] < otherPlate[b]) u = 2600 * str * std::exp(-std::pow((d - 90) / 70.0, 2.0));   // volcanic islands
                else u = -3500 * str * std::exp(-std::pow(d / 80.0, 2.0));                                    // trench
                break;
            case Boundary::Rift:
                u = -1100 * str * std::exp(-std::pow(d / 90.0, 2.0)) + 500 * str * std::exp(-std::pow((d - 150) / 80.0, 2.0));
                break;
            case Boundary::Ridge:
                u = (selfCont || otherCont) ? 0 : 1700 * str * std::exp(-std::pow(d / 350.0, 2.0));
                break;
            case Boundary::Transform:
                u = 250 * str * std::exp(-std::pow(d / 60.0, 2.0));
                break;
            default: break;
        }
        uplift[i] = (float)u;
    }

    // 6. Hotspots: chains of volcanic islands left behind as plates drift over them
    volcanoes.clear();
    int hotspots = 3 + (int)(rnd01(s) * 4);
    for (int h = 0; h < hotspots; h++) {
        Vec3d p = randomDir(s);
        int cell = 0;
        { double lat = std::asin(p.y) * 180 / kPi, lon = std::atan2(p.x, p.z) * 180 / kPi; cell = nearestCell(lat, lon); }
        Vec3d v = plateVelocity(cell);
        Vec3d back = v.length() > 1e-9 ? (-v).normalized() : Vec3d{1, 0, 0};
        for (int k = 0; k < 8; k++) {
            Vec3d q = (p + back * (k * 0.035)).normalized();
            double lat = std::asin(q.y) * 180 / kPi, lon = std::atan2(q.x, q.z) * 180 / kPi;
            int x, y; cellOf(lat, lon, x, y);
            double height = (k == 0 ? 6500 : 5200 * std::exp(-k * 0.35)) * activity;
            double rKm = 110;   // smooth volcanic cone, measured in real distance
            for (int dy = -5; dy <= 5; dy++)
                for (int dx = -12; dx <= 12; dx++) {
                    int c = idx(x + dx, y + dy);
                    double dk = angleBetween(cellDir[c], q) * radiusKm;
                    if (dk < rKm * 3) uplift[c] += (float)(height * std::exp(-std::pow(dk / rKm, 2.0)));
                }
            if (k < 2) volcanoes.push_back({lat, lon, true});
        }
    }
    // Volcanoes along subduction zones and island arcs
    for (int y = 2; y < H - 2; y += 3)
        for (int x = 0; x < W; x += 3) {
            int i = y * W + x;
            Boundary b = src[i] >= 0 ? (Boundary)boundary[src[i]] : Boundary::None;
            if ((b == Boundary::Subduction && plates[plateId[i]].continental && std::fabs(distBoundaryKm[i] - 150) < 30) ||
                (b == Boundary::IslandArc && plateId[i] < otherPlate[src[i]] && std::fabs(distBoundaryKm[i] - 90) < 30)) {
                if (rnd01(s) < 0.35) volcanoes.push_back({cellLat(y), cellLon(x), false});
            }
        }

    // Spread stress a little so earthquake zones have width
    std::vector<float> st2 = stress;
    for (int i = 0; i < N; i++)
        if (src[i] >= 0 && distBoundaryKm[i] < 250) st2[i] = std::max(st2[i], stress[src[i]] * (float)(1 - distBoundaryKm[i] / 250));
    stress.swap(st2);

    // 7. Rock types and ages
    rock.assign(N, (uint8_t)Rock::Sedimentary);
    rockAge.assign(N, 100);
    for (int i = 0; i < N; i++) {
        const Vec3d& d = cellDir[i];
        double baseH = planet.elevation(d, 0) + uplift[i];
        double old = ageN(d.x * 1.8, d.y * 1.8, d.z * 1.8);
        Boundary b = src[i] >= 0 ? (Boundary)boundary[src[i]] : Boundary::None;
        double dist = distBoundaryKm[i];
        Rock r; double age;
        if (baseH < -1000) {
            r = Rock::OceanicBasalt;
            age = (b == Boundary::Ridge) ? 2 + dist / 25.0 : 60 + old * 60 + dist / 30.0;
            age = std::min(age, 190.0);
        } else if (b == Boundary::Collision && dist < 450) {
            r = old > 0.1 ? Rock::Plutonic : Rock::Metamorphic; age = 15 + dist / 10.0;
        } else if ((b == Boundary::Subduction || b == Boundary::IslandArc) && dist < 260) {
            r = dist < 200 ? Rock::Volcanic : Rock::Plutonic; age = 2 + dist / 12.0;
        } else if (b == Boundary::Rift && dist < 200) {
            r = Rock::Volcanic; age = 1 + dist / 20.0;
        } else if (old > 0.18 && dist > 500 && baseH > -200) {
            r = Rock::Shield; age = 1800 + old * 2400;
        } else if (baseH > 1200 && old > -0.1) {
            r = Rock::Metamorphic; age = 250 + old * 400 + 300;
        } else {
            r = Rock::Sedimentary; age = 20 + (old + 1) * 220;
        }
        rock[i] = (uint8_t)r;
        rockAge[i] = (float)std::max(0.5, age);
    }
}
