#include "politics.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <map>

namespace {
uint32_t rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
double rnd01(uint32_t& s) { return (rng(s) >> 8) / 16777216.0; }
double cellAreaKm2(const World& w, int i) { double ck = w.cellKm(); return ck * ck * std::max(0.02, std::cos(World::cellLat(i / World::W) * kPi / 180)); }
}

void World::generateHabitability() {
    const int N = W * H;
    habitability.assign(N, 0);
    siteScore.assign(N, 0);
    if (tempYear.empty()) return;
    for (int i = 0; i < N; i++) {
        if (elev[i] <= 0) continue;
        double T = tempYear[i], P = rainfallYear[i];
        double tScore = std::exp(-std::pow((T - 17) / 11.0, 2.0));
        double pScore = smoothstepd(150, 700, P) * (1 - 0.4 * smoothstepd(2500, 4500, P));
        double hScore = 1 - smoothstepd(1200, 4000, elev[i]);
        Biome b = biome.empty() ? Biome::Grassland : (Biome)biome[i];
        double bMul = (b == Biome::IceSheet) ? 0.0 : (b == Biome::Tundra ? 0.25 : (b == Biome::HotDesert || b == Biome::ColdDesert ? 0.35 : 1.0));
        double water = discharge.empty() ? 0 : smoothstepd(20, 3000, discharge[i]) * 0.6;
        if (!lakeType.empty() && lakeType[i] == 1) bMul *= 0.3;   // the lake itself
        double coast = distOceanKm.empty() ? 0 : std::exp(-distOceanKm[i] / 120.0) * 0.35;
        double hab = clampd(tScore * (0.35 + 0.65 * pScore) * hScore * bMul + water * tScore + coast * tScore * 0.6, 0, 1);
        habitability[i] = (float)hab;
        // city sites like rivers and coasts most of all
        double rough = 0;
        int x = i % W, y = i / W;
        for (int k = 0; k < 4; k++) {
            int n = k == 0 ? idx(x + 1, y) : k == 1 ? idx(x - 1, y) : k == 2 ? idx(x, y + 1) : idx(x, y - 1);
            rough += std::fabs(elev[n] - elev[i]);
        }
        double flat = 1 - smoothstepd(150, 1500, rough / 4);
        siteScore[i] = (float)clampd(hab * (0.5 + 0.5 * flat) + water * 0.6 * hab + coast * 0.8 * hab, 0, 1);
    }
}

void generateCultures(Document& doc, int count, uint32_t seed) {
    doc.cultures.clear();
    for (int k = 0; k < count; k++) {
        Culture c;
        c.style = (NameStyle)((k + (int)(seed % 7)) % (int)NameStyle::Count);
        uint32_t s = seed + k * 977u;
        c.name = makeName(c.style, s, false);
        c.r = (unsigned char)(80 + rnd01(s) * 160); c.g = (unsigned char)(80 + rnd01(s) * 160); c.b = (unsigned char)(80 + rnd01(s) * 160);
        doc.cultures.push_back(c);
    }
}

void generateNations(Document& doc, const World& w, int count, uint32_t seed) {
    const int N = World::W * World::H;
    uint32_t s = seed * 2654435761u + 7u;
    if (doc.cultures.empty()) generateCultures(doc, std::max(3, count / 5), seed);
    doc.nations.clear();
    doc.cities.clear();
    doc.owner.assign(N, 0);
    if (w.habitability.empty()) return;
    // 1. Seeds: good places, spread apart
    std::vector<int> seeds;
    double landArea = 0;
    for (int i = 0; i < N; i++) if (w.elev[i] > 0) landArea += cellAreaKm2(w, i);
    double minSepKm = std::sqrt(landArea / std::max(1, count)) * 0.55;
    int tries = 0;
    while ((int)seeds.size() < count && tries < 200000) {
        tries++;
        int i = (int)(rnd01(s) * N) % N;
        if (w.elev[i] <= 0 || rnd01(s) > w.habitability[i] * w.habitability[i] + 0.02) continue;
        bool ok = true;
        for (int o : seeds) if (angleBetween(w.cellDir[i], w.cellDir[o]) * w.radiusKm < minSepKm * (tries > 100000 ? 0.5 : 1.0)) { ok = false; break; }
        if (ok) seeds.push_back(i);
    }
    // 2. Each nation grows outwards; crossing mountains, deserts, big rivers and sea is costly
    std::vector<float> cost(N, 1e30f);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (size_t k = 0; k < seeds.size(); k++) {
        Nation n;
        n.culture = (int)(k * doc.cultures.size() / std::max<size_t>(1, seeds.size()) + rng(s) % 2) % (int)doc.cultures.size();
        uint32_t ns = seed + (uint32_t)k * 131u;
        n.name = makeName(doc.cultures[n.culture].style, ns, true);
        double hue = rnd01(s) * 6;
        int hi = (int)hue; double f = hue - hi;
        double rgb[6][3] = {{1, f, 0}, {1 - f, 1, 0}, {0, 1, f}, {0, 1 - f, 1}, {f, 0, 1}, {1, 0, 1 - f}};
        n.r = (unsigned char)(70 + 160 * rgb[hi][0]); n.g = (unsigned char)(70 + 160 * rgb[hi][1]); n.b = (unsigned char)(70 + 160 * rgb[hi][2]);
        n.government = (int)(rng(s) % 8);
        doc.nations.push_back(n);
        cost[seeds[k]] = 0;
        doc.owner[seeds[k]] = (uint16_t)(k + 1);
        pq.push({0.f, seeds[k]});
    }
    const double ck = w.cellKm();
    const double maxReach = std::sqrt(landArea / std::max(1, count)) * 3.6;   // leave wild land unclaimed
    while (!pq.empty()) {
        auto [c, i] = pq.top(); pq.pop();
        if (c > cost[i]) continue;
        int x = i % World::W, y = i / World::W;
        const int nb[4] = {World::idx(x + 1, y), World::idx(x - 1, y), World::idx(x, y + 1), World::idx(x, y - 1)};
        for (int n : nb) {
            if (w.elev[n] <= 0) continue;   // sea stops expansion (islands get their own nations or none)
            double step = ck * (n == nb[0] || n == nb[1] ? std::max(0.05, std::cos(World::cellLat(y) * kPi / 180)) : 1.0);
            double climb = std::fabs(w.elev[n] - w.elev[i]) / 1000.0;
            double penalty = 1 + climb * 2 + (1 - w.habitability[n]) * 1.1 + smoothstepd(2500, 5000, w.elev[n]) * 2.5 + (w.habitability[n] < 0.05 ? 2.5 : 0);
            if (!w.discharge.empty() && w.discharge[n] > 2500) penalty += 1.5;   // big rivers make borders
            float nc = (float)(c + step * penalty);
            if (nc > maxReach) continue;
            if (nc < cost[n]) { cost[n] = nc; doc.owner[n] = doc.owner[i]; pq.push({nc, n}); }
        }
    }
}

std::vector<std::pair<double, double>> suggestSites(const Document& doc, const World& w, int count) {
    std::vector<std::pair<float, int>> cand;
    if (w.siteScore.empty()) return {};
    for (int i = 0; i < World::W * World::H; i++) if (w.siteScore[i] > 0.25f) cand.push_back({w.siteScore[i], i});
    std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::pair<double, double>> out;
    std::vector<Vec3d> taken;
    for (const City& c : doc.cities) {
        double la = c.lat * kPi / 180, lo = c.lon * kPi / 180;
        taken.push_back({std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)});
    }
    for (auto& cd : cand) {
        if ((int)out.size() >= count) break;
        const Vec3d& d = w.cellDir[cd.second];
        bool ok = true;
        for (const Vec3d& t : taken) if (angleBetween(d, t) * w.radiusKm < 250) { ok = false; break; }
        if (!ok) continue;
        taken.push_back(d);
        out.push_back({World::cellLat(cd.second / World::W), World::cellLon(cd.second % World::W)});
    }
    return out;
}

void generateCities(Document& doc, const World& w, double density, uint32_t seed) {
    uint32_t s = seed * 40503u + 11u;
    doc.cities.clear();
    std::vector<double> area; std::vector<long long> pop;
    nationStats(doc, w, area, pop);
    const int N = World::W * World::H;
    // candidate cells per nation, best first
    std::vector<std::vector<std::pair<float, int>>> cand(doc.nations.size());
    for (int i = 0; i < N; i++) {
        int n = doc.nationAt(i);
        if (n < 0 || w.siteScore.empty() || w.siteScore[i] < 0.05f) continue;
        cand[n].push_back({w.siteScore[i] * (float)(0.75 + 0.5 * rnd01(s)), i});
    }
    for (size_t n = 0; n < doc.nations.size(); n++) {
        auto& c = cand[n];
        std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first > b.first; });
        int want = std::max(1, std::min(40, (int)std::round(area[n] / 180000.0 * density)));
        std::vector<Vec3d> placed;
        double minSep = std::max(90.0, std::sqrt(area[n] / want) * 0.45);
        for (auto& cd : c) {
            if ((int)placed.size() >= want) break;
            const Vec3d& d = w.cellDir[cd.second];
            bool ok = true;
            for (const Vec3d& p : placed) if (angleBetween(d, p) * w.radiusKm < minSep) { ok = false; break; }
            if (!ok) continue;
            placed.push_back(d);
            City city;
            int cul = doc.nations[n].culture;
            uint32_t ns = seed + (uint32_t)doc.cities.size() * 7919u + 3u;
            city.name = makeName(cul >= 0 && cul < (int)doc.cultures.size() ? doc.cultures[cul].style : NameStyle::Latin, ns, true);
            city.lat = World::cellLat(cd.second / World::W) + (rnd01(s) - 0.5) * 0.4;
            city.lon = World::cellLon(cd.second % World::W) + (rnd01(s) - 0.5) * 0.4;
            double z = 0; for (int k = 0; k < 4; k++) z += rnd01(s); z = (z - 2) / 0.58;
            city.population = (int)clampd(std::exp(8.5 + w.siteScore[cd.second] * 2.5 + z * 0.9), 300, 5e6);
            city.nation = (int)n;
            city.culture = cul;
            city.capital = placed.size() == 1;
            if (city.capital) city.population = (int)std::min(8e6, city.population * 2.5 + 20000);
            city.founded = -(int)(rnd01(s) * 1500);
            doc.cities.push_back(city);
        }
    }
}

void nationStats(const Document& doc, const World& w, std::vector<double>& areaKm2, std::vector<long long>& population) {
    areaKm2.assign(doc.nations.size(), 0);
    population.assign(doc.nations.size(), 0);
    for (int i = 0; i < (int)doc.owner.size(); i++) {
        int n = doc.nationAt(i);
        if (n >= 0 && n < (int)areaKm2.size()) areaKm2[n] += cellAreaKm2(w, i);
    }
    for (const City& c : doc.cities) if (c.nation >= 0 && c.nation < (int)population.size()) population[c.nation] += c.population;
}

void generateReligions(Document& doc, const World& w, int count, uint32_t seed) {
    uint32_t s = seed * 747796405u + 31u;
    const int N = World::W * World::H;
    doc.religions.clear();
    doc.faith.assign(N, 0);
    if (doc.cities.empty()) return;
    static const char* nouns[] = {"Sun", "Moon", "Flame", "Tide", "Stone", "Star", "Serpent", "Tree", "Storm", "Mountain", "River", "Raven", "Lion", "Mask", "Dawn", "Void"};
    static const char* adjs[] = {"Eternal", "Silent", "Burning", "Hidden", "Twin", "Seventh", "Golden", "Pale", "Wandering", "First"};
    // origins: big cities, far apart
    std::vector<int> cand;
    for (int i = 0; i < (int)doc.cities.size(); i++) cand.push_back(i);
    std::sort(cand.begin(), cand.end(), [&](int a, int b) { return doc.cities[a].population > doc.cities[b].population; });
    std::vector<int> origins;
    for (int c : cand) {
        if ((int)origins.size() >= count) break;
        bool ok = true;
        for (int o : origins)
            if (std::hypot(doc.cities[c].lat - doc.cities[o].lat, (doc.cities[c].lon - doc.cities[o].lon) * std::cos(doc.cities[c].lat * kPi / 180)) < 22) ok = false;
        if (ok) origins.push_back(c);
    }
    std::vector<float> cost(N, 1e30f);
    std::vector<double> reach;
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (size_t k = 0; k < origins.size(); k++) {
        const City& c = doc.cities[origins[k]];
        Religion r;
        uint32_t ns = seed + (uint32_t)k * 313u;
        NameStyle st = c.culture >= 0 && c.culture < (int)doc.cultures.size() ? doc.cultures[c.culture].style : NameStyle::Latin;
        std::string prophet = makeName(st, ns, false);
        switch (rng(s) % 5) {
            case 0: r.name = "Church of " + prophet; break;
            case 1: r.name = prophet + "ism"; break;
            case 2: r.name = std::string("Way of the ") + nouns[rng(s) % 16]; break;
            case 3: r.name = std::string("Cult of the ") + adjs[rng(s) % 10] + " " + nouns[rng(s) % 16]; break;
            default: r.name = std::string("Faith of the ") + adjs[rng(s) % 10] + " " + nouns[rng(s) % 16]; break;
        }
        r.r = (unsigned char)(60 + rnd01(s) * 190); r.g = (unsigned char)(60 + rnd01(s) * 190); r.b = (unsigned char)(60 + rnd01(s) * 190);
        r.founded = c.founded + (int)(rnd01(s) * 300);
        r.lat = c.lat; r.lon = c.lon;
        r.notes = "First preached in " + c.name + ".";
        doc.religions.push_back(r);
        int cell = World::nearestCell(c.lat, c.lon);
        cost[cell] = 0;
        doc.faith[cell] = (uint8_t)(k + 1);
        reach.push_back(0.6 + rnd01(s) * 1.0);   // some faiths spread further than others
        pq.push({0.f, cell});
    }
    const double ck = w.cellKm();
    while (!pq.empty()) {
        auto [c, i] = pq.top(); pq.pop();
        if (c > cost[i]) continue;
        int rel = doc.faith[i] - 1;
        int x = i % World::W, y = i / World::W;
        const int nb[4] = {World::idx(x + 1, y), World::idx(x - 1, y), World::idx(x, y + 1), World::idx(x, y - 1)};
        for (int n : nb) {
            double step = ck * (n == nb[0] || n == nb[1] ? std::max(0.05, std::cos(World::cellLat(y) * kPi / 180)) : 1.0);
            double pen = w.elev[n] <= 0 ? 2.5 : 1 + (1 - w.habitability[n]) * 1.5 + std::fabs(w.elev[n] - w.elev[i]) / 800.0;
            float nc = (float)(c + step * pen / reach[rel]);
            if (nc > 9000) continue;
            if (nc < cost[n]) { cost[n] = nc; doc.faith[n] = (uint8_t)(rel + 1); pq.push({nc, n}); }
        }
    }
    for (int i = 0; i < N; i++) if (w.elev[i] <= 0 || w.habitability[i] < 0.02) doc.faith[i] = 0;
}

const char* rulerTitle(int gov, bool f) {
    static const char* m[] = {"King", "Emperor", "Consul", "High Priest", "Chief", "Doge", "Dictator", "President", "Prince", "Archmage"};
    static const char* w[] = {"Queen", "Empress", "Consul", "High Priestess", "Chief", "Dogaressa", "Dictator", "President", "Princess", "Archmage"};
    int i = gov < 0 || gov > 9 ? 0 : gov;
    return f ? w[i] : m[i];
}

void generateRulers(Document& doc, uint32_t seed) {
    uint32_t s = seed * 1103515245u + 12345u;
    static const char* roman[] = {"", " II", " III", " IV", " V", " VI", " VII", " VIII", " IX", " X"};
    for (size_t n = 0; n < doc.nations.size(); n++) {
        Nation& nat = doc.nations[n];
        nat.rulers.clear();
        int start = doc.yearMin;
        for (const City& c : doc.cities) if (c.nation == (int)n && c.capital) start = std::max(doc.yearMin, c.founded);
        NameStyle st = nat.culture >= 0 && nat.culture < (int)doc.cultures.size() ? doc.cultures[nat.culture].style : NameStyle::Latin;
        std::vector<std::string> pool;
        uint32_t ns = seed + (uint32_t)n * 7777u;
        for (int k = 0; k < 6; k++) pool.push_back(makeName(st, ns, false));   // dynasties reuse names
        std::map<std::string, int> used;
        int y = start;
        bool elected = nat.government == 2 || nat.government == 7;   // republics and democracies
        while (y < doc.currentYear && nat.rulers.size() < 400) {
            int len = elected ? 4 + (int)(rnd01(s) * 6) : 3 + (int)(-std::log(std::max(1e-6, rnd01(s))) * 16);
            bool female = rnd01(s) < 0.3;
            std::string base = rnd01(s) < 0.7 ? pool[rng(s) % pool.size()] : makeName(st, ns, false);
            int count = used[base]++;
            Ruler r;
            r.name = std::string(rulerTitle(nat.government, female)) + " " + base + (elected ? "" : roman[std::min(count, 9)]);
            r.start = y; r.end = y + len;
            nat.rulers.push_back(r);
            y += len;
        }
    }
}

void generateRelations(Document& doc, uint32_t seed) {
    uint32_t s = seed * 2891336453u + 3u;
    doc.relations.clear();
    std::map<std::pair<int, int>, int> border;
    for (int y = 0; y < World::H; y++)
        for (int x = 0; x < World::W; x++) {
            int a = doc.nationAt(World::idx(x, y)), b = doc.nationAt(World::idx(x + 1, y)), c = doc.nationAt(World::idx(x, y + 1));
            if (a >= 0 && b >= 0 && a != b) border[{std::min(a, b), std::max(a, b)}]++;
            if (a >= 0 && c >= 0 && a != c) border[{std::min(a, c), std::max(a, c)}]++;
        }
    std::vector<int> size(doc.nations.size(), 0);
    for (auto o : doc.owner) if (o) size[o - 1]++;
    for (auto& b : border) {
        double r = rnd01(s);
        NationRelation rel; rel.a = b.first.first; rel.b = b.first.second;
        if (r < 0.25) rel.type = Relation::Alliance;
        else if (r < 0.45) rel.type = Relation::TradePact;
        else if (r < 0.7) rel.type = Relation::Rivalry;
        else if (r < 0.78) rel.type = Relation::War;
        else if (r < 0.85) {
            rel.type = Relation::Vassal;
            if (size[rel.a] > size[rel.b]) std::swap(rel.a, rel.b);   // the smaller one is the vassal
        } else continue;
        doc.relations.push_back(rel);
    }
}
