#include "trade.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <thread>
#include <mutex>
#include <map>
#include <atomic>

const char* eraName(Era e) {
    static const char* n[] = {"Ancient", "Medieval", "Early modern (sailing)", "Industrial (rail, steam)", "Modern"};
    int i = (int)e;
    return i >= 0 && i < (int)Era::Count ? n[i] : "?";
}

enum G { Grain, Fish, Meat, Timber, Salt, Stone, Clay, Sand, IronOre, CopperOre, TinOre, BaseMetals, Gold, Silver, Gems, Coal, Oil,
         Steel, Bronze, Tools, Weapons, Jewellery, Pottery, Glass, Textiles, GCount };

const std::vector<GoodDef>& goodDefs() {
    static const std::vector<GoodDef> d = {
        {"Grain", 1, 0.25, false, 0}, {"Fish", 2, 0.02, false, 0}, {"Meat and wool", 3, 0.03, false, 0}, {"Timber", 0.8, 0.15, false, 0},
        {"Salt", 3, 0.008, false, 0}, {"Building stone", 0.3, 0.08, false, 0}, {"Clay", 0.2, 0.02, false, 0}, {"Sand and gravel", 0.1, 0.02, false, 0},
        {"Iron ore", 0.6, 0, false, 0}, {"Copper ore", 1.5, 0, false, 0}, {"Tin ore", 6, 0, false, 0}, {"Base metals", 4, 0.0005, false, 0},
        {"Gold", 50000, 2e-7, false, 0}, {"Silver", 3000, 2e-6, false, 0}, {"Gems", 100000, 2e-8, false, 0},
        {"Coal", 0.5, 0.03, false, 0}, {"Oil", 1.5, 0, false, 3},
        {"Steel", 8, 0.002, true, 0}, {"Bronze", 12, 0.001, true, 0}, {"Tools", 30, 0.004, true, 0}, {"Weapons", 60, 0.002, true, 0},
        {"Jewellery", 20000, 1e-5, true, 0}, {"Pottery", 3, 0.01, true, 0}, {"Glass", 10, 0.002, true, 0}, {"Textiles", 15, 0.006, true, 0},
    };
    return d;
}

namespace {
// Era adjustments to how much of a good people want
double eraDemand(int g, Era e) {
    int k = (int)e;
    switch (g) {
        case Coal: return k >= 3 ? 25.0 : (k == 2 ? 2.0 : 1.0);
        case Oil: return k >= 4 ? 1.0 : (k == 3 ? 0.1 : 0.0);
        case Steel: return k >= 4 ? 150.0 : (k == 3 ? 40.0 : (k == 2 ? 3.0 : 1.0));
        case Bronze: return k <= 1 ? 1.0 : 0.2;
        case Glass: return k >= 3 ? 8.0 : 1.0;
        case BaseMetals: return k >= 4 ? 40.0 : (k == 3 ? 10.0 : 1.0);
        case Textiles: return k >= 3 ? 4.0 : 1.0;
        default: return 1.0;
    }
}
int goodForResource(int res) {
    const char* n = resourceDefs()[res].name;
    std::string s = n;
    if (s == "Iron ore") return IronOre;
    if (s == "Copper") return CopperOre;
    if (s == "Tin") return TinOre;
    if (s == "Gold") return Gold;
    if (s == "Silver") return Silver;
    if (s == "Coal" || s == "Peat") return Coal;
    if (s == "Oil" || s == "Natural gas" || s == "Oil shale") return Oil;
    if (s == "Salt") return Salt;
    if (s == "Clay") return Clay;
    if (s == "Silica sand" || s == "Sand and gravel") return Sand;
    if (resourceDefs()[res].group == ResGroup::Gem) return Gems;
    if (resourceDefs()[res].group == ResGroup::Building || s == "Limestone") return Stone;
    if (s == "Lead" || s == "Zinc" || s == "Nickel" || s == "Manganese" || s == "Bauxite (aluminium)" || s == "Titanium" || s == "Chromium" ||
        s == "Tungsten" || s == "Cobalt" || s == "Molybdenum")
        return BaseMetals;
    return -1;
}
bool precious(int g) { return g == Gold || g == Silver || g == CopperOre || g == TinOre || g == BaseMetals; }
}

void Economy::buildTravelGrid(const World& w, Era e) {
    const int N = World::W * World::H;
    static const double landV[] = {25, 30, 35, 250, 700}, riverV[] = {40, 50, 60, 300, 500}, seaV[] = {50, 90, 150, 400, 800};
    int k = (int)e;
    speed.assign(N, 0);
    kind.assign(N, 0);
    windU.assign(N, 0); windV.assign(N, 0);
    for (int i = 0; i < N; i++) {
        if (!w.windU[0].empty()) {
            double u = 0, v = 0;
            for (int m = 0; m < 12; m++) { u += w.windU[m][i]; v += w.windV[m][i]; }
            windU[i] = (float)(u / 12); windV[i] = (float)(v / 12);
        }
        bool lake = !w.lakeType.empty() && w.lakeType[i] == 1;
        if (w.elev[i] <= 0) {
            kind[i] = 2;
            double nearLand = w.distOceanKm.empty() ? 0 : 0;
            (void)nearLand;
            bool ice = !w.biome.empty() && (Biome)w.biome[i] == Biome::SeaIce;
            speed[i] = ice && k < 4 ? 0 : (float)seaV[k];
        } else if (lake || (!w.discharge.empty() && w.discharge[i] > 400)) {
            kind[i] = 1;
            speed[i] = (float)riverV[k];
        } else {
            double f = 1;
            Biome b = w.biome.empty() ? Biome::Grassland : (Biome)w.biome[i];
            if (b == Biome::IceSheet) f = 0.2;
            else if (b == Biome::TropicalRainforest || b == Biome::Wetland) f = 0.5;
            else if (b == Biome::BorealForest || b == Biome::TemperateRainforest || b == Biome::TropicalSeasonalForest) f = 0.7;
            else if (b == Biome::HotDesert || b == Biome::ColdDesert) f = 0.8;
            f *= 1 - 0.6 * smoothstepd(2000, 5000, w.elev[i]);
            if (k >= 3) f = std::sqrt(f);   // railways and roads smooth out terrain
            speed[i] = (float)(landV[k] * f);
        }
    }
    // Early sailors stay near the coast
    if (k <= 1) {
        double maxOff = k == 0 ? 250 : 700;
        // distance from land for sea cells, using a quick multi-pass sweep
        std::vector<float> d(N, 1e9f);
        for (int i = 0; i < N; i++) if (w.elev[i] > 0) d[i] = 0;
        double ck = w.cellKm();
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 0; y < World::H; y++)
                for (int x = 0; x < World::W; x++) {
                    int i = y * World::W + x;
                    double ew = std::max(0.05, std::cos(World::cellLat(y) * kPi / 180)) * ck;
                    d[i] = std::min({d[i], d[World::idx(x - 1, y)] + (float)ew, d[World::idx(x, y - 1)] + (float)ck});
                }
            for (int y = World::H - 1; y >= 0; y--)
                for (int x = World::W - 1; x >= 0; x--) {
                    int i = y * World::W + x;
                    double ew = std::max(0.05, std::cos(World::cellLat(y) * kPi / 180)) * ck;
                    d[i] = std::min({d[i], d[World::idx(x + 1, y)] + (float)ew, d[World::idx(x, y + 1)] + (float)ck});
                }
        }
        for (int i = 0; i < N; i++) if (kind[i] == 2 && d[i] > maxOff) speed[i] = 0;
    }
}

namespace {
struct Dij {
    std::vector<float> dist;
    std::vector<int> parent, source;
};
// Multi-source Dijkstra over the travel grid; cost = days
void runDijkstra(const std::vector<float>& speed, const std::vector<uint8_t>& kind, const std::vector<float>& wu, const std::vector<float>& wv,
                 const World& w, Era era, const std::vector<std::pair<int, int>>& sources, int target, Dij& out) {
    const int N = World::W * World::H;
    out.dist.assign(N, 1e30f); out.parent.assign(N, -1); out.source.assign(N, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (auto& s : sources) { out.dist[s.first] = 0; out.source[s.first] = s.second; pq.push({0.f, s.first}); }
    const double ck = w.cellKm();
    const bool sailing = era == Era::Medieval || era == Era::EarlyModern || era == Era::Ancient;
    const double port = (int)era >= 3 ? 0.5 : 1.5;
    while (!pq.empty()) {
        auto [d, i] = pq.top(); pq.pop();
        if (d > out.dist[i]) continue;
        if (i == target) break;
        int x = i % World::W, y = i / World::W;
        double ew = std::max(0.05, std::cos(World::cellLat(y) * kPi / 180)) * ck;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                if (y + dy < 0 || y + dy >= World::H) continue;
                int n = World::idx(x + dx, y + dy);
                if (speed[n] <= 0) continue;
                double ex = dx * ew, ny = -dy * ck;   // east and north km
                double km = std::sqrt(ex * ex + ny * ny);
                double v = 0.5 * (speed[i] + speed[n]);
                if (sailing && kind[i] == 2 && kind[n] == 2) {
                    double u = wu[i], vv = wv[i], sp = std::sqrt(u * u + vv * vv);
                    if (sp > 0.1) {
                        double c = (u * ex + vv * ny) / (sp * km);
                        v *= 0.8 + 0.35 * c * std::min(1.0, sp / 7.0);
                    }
                }
                double cost = km / std::max(1.0, v);
                if ((kind[i] == 2) != (kind[n] == 2)) cost += port;
                float nd = (float)(d + cost);
                if (nd < out.dist[n]) { out.dist[n] = nd; out.parent[n] = i; out.source[n] = out.source[i]; pq.push({nd, n}); }
            }
    }
}
}

double Economy::findPath(const World& w, const Document& doc, int a, int b, std::vector<int>& cells, double& seaShare) const {
    cells.clear(); seaShare = 0;
    if (a < 0 || b < 0 || a >= (int)doc.cities.size() || b >= (int)doc.cities.size() || speed.empty()) return -1;
    int ca = World::nearestCell(doc.cities[a].lat, doc.cities[a].lon), cb = World::nearestCell(doc.cities[b].lat, doc.cities[b].lon);
    Dij dj;
    runDijkstra(speed, kind, windU, windV, w, era, {{ca, a}}, cb, dj);
    if (dj.dist[cb] > 1e29f) return -1;
    int c = cb, water = 0;
    while (c >= 0) { cells.push_back(c); if (kind[c] != 0) water++; c = dj.parent[c]; }
    std::reverse(cells.begin(), cells.end());
    seaShare = cells.empty() ? 0 : (double)water / cells.size();
    return dj.dist[cb];
}

double Economy::pathVia(const World& w, const std::vector<std::pair<double, double>>& pts, int mode, TradeRoute& r) const {
    r.cells.clear(); r.legDays.clear(); r.legStart.clear(); r.seaShare = 0; r.km = 0; r.days = 0;
    if (pts.size() < 2 || speed.empty()) return -1;
    const bool sailing = (int)era <= 2;
    const double port = (int)era >= 3 ? 0.5 : 1.5;
    int water = 0;
    for (size_t leg = 0; leg + 1 < pts.size(); leg++) {
        int ca = World::nearestCell(pts[leg].first, pts[leg].second), cb = World::nearestCell(pts[leg + 1].first, pts[leg + 1].second);
        std::vector<int> cells;
        double days = 0;
        if (mode == 0) {
            Dij dj;
            runDijkstra(speed, kind, windU, windV, w, era, {{ca, 0}}, cb, dj);
            if (dj.dist[cb] > 1e29f) return -1;
            for (int c = cb; c >= 0; c = dj.parent[c]) cells.push_back(c);
            std::reverse(cells.begin(), cells.end());
            days = dj.dist[cb];
        } else {
            // straight (great-circle) line, sampled every half cell; impassable cells are crossed slowly
            Vec3d a = w.cellDir[ca], b = w.cellDir[cb];
            double ang = angleBetween(a, b);
            int steps = std::max(1, (int)(ang * w.radiusKm / (w.cellKm() * 0.5)));
            int last = -1;
            for (int k = 0; k <= steps; k++) {
                double t = (double)k / steps;
                Vec3d p = ang < 1e-9 ? a : (a * (std::sin((1 - t) * ang) / std::sin(ang)) + b * (std::sin(t * ang) / std::sin(ang)));
                double lat = std::asin(clampd(p.y / p.length(), -1, 1)) * 180 / kPi, lon = std::atan2(p.x, p.z) * 180 / kPi;
                int c = World::nearestCell(lat, lon);
                if (c != last) cells.push_back(c);
                last = c;
            }
            for (size_t k = 0; k + 1 < cells.size(); k++) {
                int i = cells[k], n = cells[k + 1];
                double km = angleBetween(w.cellDir[i], w.cellDir[n]) * w.radiusKm;
                double v = 0.5 * (std::max(speed[i], 5.f) + std::max(speed[n], 5.f));
                if (sailing && kind[i] == 2 && kind[n] == 2) {
                    // headwinds slow ships, tailwinds speed them up
                    double lat = World::cellLat(i / World::W), dlon = std::remainder(World::cellLon(n % World::W) - World::cellLon(i % World::W), 360.0);
                    double ex = dlon * std::cos(lat * kPi / 180), ny = World::cellLat(n / World::W) - lat, L = std::sqrt(ex * ex + ny * ny);
                    double u = windU[i], vv = windV[i], sp = std::sqrt(u * u + vv * vv);
                    if (sp > 0.1 && L > 0) v *= 0.8 + 0.35 * (u * ex + vv * ny) / (sp * L) * std::min(1.0, sp / 7.0);
                }
                days += km / std::max(1.0, v);
                if ((kind[i] == 2) != (kind[n] == 2)) days += port;
            }
        }
        r.legStart.push_back((int)r.cells.size());
        for (size_t k = (r.cells.empty() ? 0 : 1); k < cells.size(); k++) r.cells.push_back(cells[k]);
        r.legDays.push_back(days);
        r.days += days;
    }
    for (size_t k = 0; k < r.cells.size(); k++) {
        if (kind[r.cells[k]] != 0) water++;
        if (k > 0) r.km += angleBetween(w.cellDir[r.cells[k - 1]], w.cellDir[r.cells[k]]) * w.radiusKm;
    }
    r.seaShare = r.cells.empty() ? 0 : (double)water / r.cells.size();
    return r.days;
}

void Economy::simulate(const World& w, const Document& doc, Era e) {
    era = e;
    valid = false;
    const auto& G = goodDefs();
    goodCount = (int)G.size();
    const int C = (int)doc.cities.size();
    production.assign(C, std::vector<double>(goodCount, 0));
    demand.assign(C, std::vector<double>(goodCount, 0));
    price.assign(C, std::vector<double>(goodCount, 0));
    routes.clear();
    totalProduction.assign(goodCount, 0);
    totalTradeValue = 0;
    if (C == 0 || w.habitability.empty()) return;
    buildTravelGrid(w, e);

    // ---- production from land and mines ----
    std::vector<int> cityCell(C);
    std::vector<Vec3d> cityDir(C);
    for (int c = 0; c < C; c++) {
        cityCell[c] = World::nearestCell(doc.cities[c].lat, doc.cities[c].lon);
        cityDir[c] = w.cellDir[cityCell[c]];
    }
    for (int c = 0; c < C; c++) {
        const City& ci = doc.cities[c];
        double pop = std::max(100, ci.population);
        int cell = cityCell[c];
        double hab = w.habitability[cell];
        Biome b = (Biome)w.biome[cell];
        auto& P = production[c];
        P[Grain] = pop * 0.32 * (0.35 + 1.3 * hab);
        if (w.distOceanKm[cell] < 80 || (!w.lakeType.empty() && w.lakeType[cell] == 1)) P[Fish] = pop * 0.05;
        P[Meat] = pop * ((b == Biome::Grassland || b == Biome::Savanna || b == Biome::Mediterranean) ? 0.06 : 0.025);
        bool forest = b == Biome::TemperateForest || b == Biome::BorealForest || b == Biome::TemperateRainforest ||
                      b == Biome::TropicalRainforest || b == Biome::TropicalSeasonalForest;
        P[Timber] = pop * (forest ? 0.3 : 0.05);
        P[Stone] = pop * 0.05; P[Clay] = pop * 0.015; P[Sand] = pop * 0.02;
        for (int g = 0; g < goodCount; g++) demand[c][g] = pop * G[g].perCapita * eraDemand(g, e);
    }
    // each deposit is worked by the nearest city within 300 km
    for (const Deposit& dp : w.deposits) {
        int g = goodForResource(dp.res);
        if (g < 0) continue;
        if (g == Oil && (int)e < 3) continue;   // nobody drills before the industrial era
        double la = dp.lat * kPi / 180, lo = dp.lon * kPi / 180;
        Vec3d p{std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)};
        int best = -1; double bd = 300;
        for (int c = 0; c < C; c++) { double d = angleBetween(p, cityDir[c]) * w.radiusKm; if (d < bd) { bd = d; best = c; } }
        if (best < 0 || (dp.offshore && (int)e < 4)) continue;
        double labour = doc.cities[best].population * ((int)e >= 3 ? 8.0 : 1.5);
        double ore = std::min(dp.tonnes * 0.002, labour);
        double out = precious(g) && g != CopperOre && g != TinOre ? ore * dp.grade / 100.0 : ore;
        if (g == Gems) out = std::min(dp.tonnes * 0.001, 5.0);
        production[best][g] += out;
    }
    // crafting from local materials
    for (int c = 0; c < C; c++) {
        auto& P = production[c];
        double pop = std::max(100, doc.cities[c].population);
        double cap = pop * ((int)e >= 3 ? 0.08 : 0.01);
        auto spare = [&](int g) { return std::max(0.0, P[g] - demand[c][g]); };
        double fuel = (int)e >= 2 ? spare(Coal) : spare(Timber) * 0.5;
        double steel = std::min({spare(IronOre) / 2, fuel, cap});
        P[Steel] += steel; P[IronOre] -= steel * 2;
        if ((int)e >= 2) P[Coal] -= steel; else P[Timber] -= steel * 2;
        double bronze = std::min({spare(CopperOre) / 3, spare(TinOre), cap * 0.3});
        P[Bronze] += bronze; P[CopperOre] -= bronze * 3; P[TinOre] -= bronze;
        double tools = std::min(spare(Steel) + spare(Bronze) * 0.5, pop * 0.012);
        P[Tools] += tools;
        double weapons = std::min(std::max(0.0, spare(Steel) - tools), pop * 0.006);
        P[Weapons] += weapons;
        P[Steel] -= std::min(P[Steel], tools + weapons);
        double jewel = std::min((spare(Gold) + spare(Silver) * 0.05) * 50 + spare(Gems) * 20, pop * 3e-5);
        P[Jewellery] += jewel;
        P[Pottery] += std::min(spare(Clay), pop * 0.015);
        P[Glass] += std::min({spare(Sand), fuel + spare(Timber), pop * 0.004});
        P[Textiles] += std::min(spare(Meat) * 0.3, pop * 0.01);
    }
    for (int c = 0; c < C; c++)
        for (int g = 0; g < goodCount; g++) { production[c][g] = std::max(0.0, production[c][g]); totalProduction[g] += production[c][g]; }

    // ---- trade each good from the cheapest supplier ----
    static const double tcost[] = {0.08, 0.06, 0.04, 0.01, 0.003};   // coins per tonne per day of travel
    const double tc = tcost[(int)e];
    struct Flow { int from, to, g; double tonnes, days; std::vector<int> path; };
    std::vector<Flow> flows;
    std::mutex mtx;
    std::atomic<int> nextGood{0};
    auto work = [&]() {
        Dij dj;
        for (;;) {
            int g = nextGood++;
            if (g >= goodCount) break;
            std::vector<std::pair<int, int>> src;
            std::vector<double> surplus(C, 0), requested(C, 0);
            for (int c = 0; c < C; c++) {
                double net = production[c][g] - demand[c][g];
                if (net > 1e-9) { surplus[c] = net; src.push_back({cityCell[c], c}); }
                price[c][g] = G[g].basePrice;
            }
            if (src.empty()) {
                for (int c = 0; c < C; c++) if (demand[c][g] > 0) price[c][g] = G[g].basePrice * 4;   // nobody has it
                continue;
            }
            runDijkstra(speed, kind, windU, windV, w, e, src, -1, dj);
            std::vector<Flow> local;
            for (int c = 0; c < C; c++) {
                double need = demand[c][g] - production[c][g];
                if (need <= 1e-9) { price[c][g] = G[g].basePrice * (1 - 0.3 * std::min(1.0, surplus[c] / (production[c][g] + 1e-9))); continue; }
                int cell = cityCell[c];
                int s = dj.source[cell];
                double days = dj.dist[cell];
                double delivered = G[g].basePrice + days * tc;
                if (s < 0 || s == c || delivered > G[g].basePrice * 4) { price[c][g] = G[g].basePrice * 4; continue; }
                price[c][g] = delivered;
                requested[s] += need;
                Flow f{s, c, g, need, days, {}};
                for (int k = cell; k >= 0; k = dj.parent[k]) f.path.push_back(k);
                std::reverse(f.path.begin(), f.path.end());
                local.push_back(std::move(f));
            }
            for (Flow& f : local) f.tonnes *= std::min(1.0, surplus[f.from] / std::max(1e-9, requested[f.from]));
            std::lock_guard<std::mutex> lk(mtx);
            for (Flow& f : local) flows.push_back(std::move(f));
        }
    };
    std::vector<std::thread> th;
    unsigned n = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < n; t++) th.emplace_back(work);
    for (auto& t : th) t.join();

    // merge flows into routes between city pairs
    std::map<std::pair<int, int>, int> idx;
    for (Flow& f : flows) {
        if (f.tonnes < 1e-6) continue;
        auto key = std::make_pair(f.from, f.to);
        auto it = idx.find(key);
        if (it == idx.end()) {
            TradeRoute r;
            r.from = f.from; r.to = f.to; r.cells = f.path; r.days = f.days;
            int water = 0; for (int c : r.cells) if (kind[c] != 0) water++;
            r.seaShare = r.cells.empty() ? 0 : (double)water / r.cells.size();
            idx[key] = (int)routes.size();
            routes.push_back(std::move(r));
            it = idx.find(key);
        }
        TradeRoute& r = routes[it->second];
        r.goods.push_back({f.g, f.tonnes});
        r.tonnes += f.tonnes;
        r.value += f.tonnes * G[f.g].basePrice;
    }
    for (auto& r : routes) {
        std::sort(r.goods.begin(), r.goods.end(), [&](auto& a, auto& b) { return a.second * G[a.first].basePrice > b.second * G[b.first].basePrice; });
        totalTradeValue += r.value;
    }
    std::sort(routes.begin(), routes.end(), [](const TradeRoute& a, const TradeRoute& b) { return a.value > b.value; });
    valid = true;
}

std::vector<std::pair<int, double>> Economy::topExports(int city, int n) const {
    std::map<int, double> m;
    for (auto& r : routes) if (r.from == city) for (auto& g : r.goods) m[g.first] += g.second;
    std::vector<std::pair<int, double>> v(m.begin(), m.end());
    const auto& G = goodDefs();
    std::sort(v.begin(), v.end(), [&](auto& a, auto& b) { return a.second * G[a.first].basePrice > b.second * G[b.first].basePrice; });
    if ((int)v.size() > n) v.resize(n);
    return v;
}

std::vector<std::pair<int, double>> Economy::topImports(int city, int n) const {
    std::map<int, double> m;
    for (auto& r : routes) if (r.to == city) for (auto& g : r.goods) m[g.first] += g.second;
    std::vector<std::pair<int, double>> v(m.begin(), m.end());
    const auto& G = goodDefs();
    std::sort(v.begin(), v.end(), [&](auto& a, auto& b) { return a.second * G[a.first].basePrice > b.second * G[b.first].basePrice; });
    if ((int)v.size() > n) v.resize(n);
    return v;
}
