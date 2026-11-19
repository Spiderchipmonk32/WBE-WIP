#include "world.h"
#include <algorithm>
#include <cmath>

// Resource placement: each resource has a geological "recipe" (rock type, age, plate
// setting, elevation, climate). The recipe gives a favourability score per cell, and
// deposits are scattered where the score is high, with realistic sizes and grades.

const char* resGroupName(ResGroup g) {
    switch (g) {
        case ResGroup::Metal: return "Metals";
        case ResGroup::Gem: return "Gems";
        case ResGroup::Fuel: return "Fuels";
        case ResGroup::Industrial: return "Industrial minerals";
        case ResGroup::Building: return "Building stone";
        default: return "?";
    }
}

enum R {
    Iron, Copper, Gold, Silver, Lead, Zinc, Tin, Tungsten, Nickel, Cobalt, Chromium, Platinum, Bauxite, Manganese,
    Titanium, Uranium, Lithium, RareEarths, Molybdenum, Mercury, Vanadium, Antimony,
    Diamond, Emerald, Ruby, Sapphire, Opal, Jade, Amethyst, Topaz,
    Coal, Oil, Gas, Peat, OilShale,
    Salt, Sulfur, Phosphate, Potash, Gypsum, Limestone, Clay, Graphite, Fluorite, Borax, SilicaSand,
    Granite, Marble, Slate, Sandstone, Basalt, Gravel,
    RCount
};

const std::vector<ResourceDef>& resourceDefs() {
    using G = ResGroup;
    static const std::vector<ResourceDef> defs = {
        {"Iron ore", G::Metal, 170, 80, 60, 5e8, 30, 65, 0, 300, 120, false, "Ancient banded iron formations in old shields; ironstones in sedimentary basins"},
        {"Copper", G::Metal, 200, 110, 50, 4e8, 0.3, 2.5, 0, 1200, 90, false, "Porphyry deposits above subduction zones; sediment-hosted in rifts"},
        {"Gold", G::Metal, 240, 200, 40, 3e7, 0.0001, 0.001, 0, 2500, 90, false, "Old greenstone belts in shields; quartz veins in mountain belts"},
        {"Silver", G::Metal, 200, 200, 210, 2e7, 0.005, 0.05, 0, 1000, 60, false, "Epithermal veins in young volcanic arcs"},
        {"Lead", G::Metal, 110, 110, 130, 3e7, 2, 10, 0, 1000, 50, false, "Limestone-hosted deposits in sedimentary basins"},
        {"Zinc", G::Metal, 150, 160, 170, 4e7, 3, 15, 0, 1000, 55, false, "With lead in carbonate basins; massive sulfides near volcanoes"},
        {"Tin", G::Metal, 190, 190, 175, 1e7, 0.2, 1.5, 0, 600, 35, false, "Granites in collision mountain belts"},
        {"Tungsten", G::Metal, 120, 130, 150, 5e6, 0.3, 1.5, 0, 800, 30, false, "Granite contacts in collision belts"},
        {"Nickel", G::Metal, 120, 170, 120, 5e7, 0.5, 3, 0, 1500, 45, false, "Ancient ultramafic rocks in shields; laterites in the tropics"},
        {"Cobalt", G::Metal, 70, 90, 200, 1e7, 0.05, 0.5, 0, 1200, 30, false, "With copper and nickel"},
        {"Chromium", G::Metal, 150, 150, 160, 2e7, 20, 45, 0, 800, 30, false, "Layered intrusions in shields; slices of sea floor in mountain belts"},
        {"Platinum", G::Metal, 225, 225, 230, 2e6, 0.0002, 0.001, 200, 2000, 15, false, "Layered intrusions in ancient shields"},
        {"Bauxite (aluminium)", G::Metal, 200, 90, 60, 1e8, 40, 60, 0, 30, 70, false, "Deeply weathered soils in hot, wet lowlands"},
        {"Manganese", G::Metal, 90, 70, 90, 5e7, 20, 50, 0, 400, 40, true, "Old marine sediments; nodules on the deep sea floor"},
        {"Titanium", G::Metal, 210, 210, 220, 3e7, 2, 10, 0, 20, 40, false, "Heavy mineral sands on coasts"},
        {"Uranium", G::Metal, 160, 220, 80, 5e5, 0.05, 2, 0, 800, 40, false, "Sandstone basins and ancient unconformities"},
        {"Lithium", G::Metal, 230, 160, 200, 5e6, 0.1, 1.5, 0, 300, 30, false, "Salt flats in dry basins; pegmatites in granite"},
        {"Rare earths", G::Metal, 200, 120, 220, 1e7, 1, 8, 0, 500, 25, false, "Carbonatites in rifts and old shields"},
        {"Molybdenum", G::Metal, 100, 120, 140, 5e7, 0.05, 0.3, 0, 1200, 30, false, "With copper in porphyry deposits"},
        {"Mercury", G::Metal, 200, 60, 70, 1e6, 0.2, 1.5, 0, 400, 15, false, "Hot-spring deposits in young volcanic zones"},
        {"Vanadium", G::Metal, 130, 90, 160, 1e7, 0.3, 1.5, 0, 800, 20, false, "With iron in layered intrusions"},
        {"Antimony", G::Metal, 160, 160, 190, 2e6, 1, 5, 0, 600, 15, false, "Veins in collision mountain belts"},

        {"Diamonds", G::Gem, 220, 240, 255, 1e3, 0.00001, 0.0001, 0, 1000, 30, false, "Kimberlite pipes in very old shields"},
        {"Emeralds", G::Gem, 40, 200, 110, 50, 0.001, 0.01, 0, 400, 15, false, "Granite contacts in collision belts"},
        {"Rubies", G::Gem, 220, 30, 60, 30, 0.001, 0.01, 0, 300, 15, false, "Marble in collision mountain belts"},
        {"Sapphires", G::Gem, 40, 70, 220, 40, 0.001, 0.01, 0, 300, 15, false, "Marble and volcanic gravels"},
        {"Opal", G::Gem, 200, 220, 230, 20, 0.001, 0.01, 0, 60, 12, false, "Weathered sedimentary rock in dry interiors"},
        {"Jade", G::Gem, 80, 160, 100, 200, 1, 10, 0, 200, 12, false, "High-pressure rocks from old subduction zones"},
        {"Amethyst", G::Gem, 150, 80, 200, 100, 0.1, 1, 0, 100, 20, false, "Gas pockets in thick lava flows"},
        {"Topaz", G::Gem, 240, 180, 70, 50, 0.01, 0.1, 0, 300, 15, false, "Granite pegmatites"},

        {"Coal", G::Fuel, 40, 40, 40, 1e9, 50, 90, 0, 1500, 110, false, "Ancient swamps buried in sedimentary basins"},
        {"Oil", G::Fuel, 30, 30, 30, 2e8, 100, 100, 1000, 5000, 110, true, "Young sedimentary basins and continental shelves"},
        {"Natural gas", G::Fuel, 120, 160, 210, 1.5e8, 100, 100, 1500, 6000, 90, true, "Deeper parts of oil basins"},
        {"Peat", G::Fuel, 110, 85, 60, 5e7, 100, 100, 0, 10, 40, false, "Cold, wet, flat lowlands"},
        {"Oil shale", G::Fuel, 90, 70, 50, 1e9, 5, 15, 0, 500, 25, false, "Old lake and sea beds in sedimentary basins"},

        {"Salt", G::Industrial, 240, 240, 240, 1e9, 90, 99, 0, 3000, 50, false, "Evaporated ancient seas in dry basins"},
        {"Sulfur", G::Industrial, 230, 220, 40, 1e7, 30, 99, 0, 500, 30, false, "Around volcanoes and salt domes"},
        {"Phosphate", G::Industrial, 180, 170, 110, 3e8, 15, 35, 0, 200, 35, false, "Shallow marine sediments"},
        {"Potash", G::Industrial, 230, 140, 110, 3e8, 10, 30, 300, 1500, 25, false, "With salt in evaporite basins"},
        {"Gypsum", G::Industrial, 235, 230, 220, 1e8, 80, 98, 0, 300, 35, false, "Evaporite basins"},
        {"Limestone", G::Industrial, 210, 210, 190, 1e9, 90, 99, 0, 100, 80, false, "Old shallow seas, now sedimentary rock"},
        {"Clay", G::Industrial, 190, 130, 100, 1e8, 60, 95, 0, 30, 70, false, "Lowland river and lake sediments"},
        {"Graphite", G::Industrial, 70, 70, 75, 5e6, 5, 30, 0, 300, 20, false, "Metamorphosed organic-rich rock"},
        {"Fluorite", G::Industrial, 140, 200, 160, 5e6, 20, 60, 0, 600, 20, false, "Veins near granites and rifts"},
        {"Borax", G::Industrial, 245, 245, 230, 1e7, 10, 30, 0, 100, 10, false, "Dry volcanic lake basins"},
        {"Silica sand", G::Industrial, 245, 235, 200, 5e7, 95, 99.8, 0, 30, 40, false, "Coasts, dunes and old beaches"},

        {"Granite", G::Building, 220, 170, 170, 1e9, 100, 100, 0, 50, 60, false, "Plutonic rock in mountain cores and shields"},
        {"Marble", G::Building, 240, 240, 245, 1e8, 100, 100, 0, 50, 35, false, "Metamorphosed limestone in mountain belts"},
        {"Slate", G::Building, 90, 100, 110, 1e8, 100, 100, 0, 50, 30, false, "Metamorphosed shale in mountain belts"},
        {"Sandstone", G::Building, 220, 180, 120, 1e9, 100, 100, 0, 50, 50, false, "Sedimentary basins"},
        {"Basalt", G::Building, 60, 60, 70, 1e9, 100, 100, 0, 50, 40, false, "Lava plains and volcanic zones"},
        {"Sand and gravel", G::Building, 200, 190, 160, 5e8, 100, 100, 0, 20, 60, false, "River valleys, coasts and glacial plains"},
    };
    return defs;
}

namespace {
uint32_t rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
double rnd01(uint32_t& s) { return (rng(s) >> 8) / 16777216.0; }
double gauss(double x, double c, double w) { return std::exp(-std::pow((x - c) / w, 2.0)); }
}

std::vector<float> World::favorability(int res) const {
    const int N = W * H;
    std::vector<float> f(N, 0);
    if (rock.empty()) return f;
    Simplex3 patch(4242 + res * 31);
    const bool haveClimate = !rainfallYear.empty();
    for (int i = 0; i < N; i++) {
        double lat = cellLat(i / W);
        double absLat = std::fabs(lat);
        double h = elev.empty() ? 0 : elev[i];
        Rock r = (Rock)rock[i];
        double age = rockAge[i];
        Boundary nb = (Boundary)nearBoundary[i];
        double dB = distBoundaryKm[i];
        bool land = h > 0;
        bool shelf = h <= 0 && h > -200;
        double rain = haveClimate ? rainfallYear[i] : 1500 - 1150 * gauss(absLat, 25, 12) - 600 * smoothstepd(50, 80, absLat);
        double temp = haveClimate ? tempYear[i] : 27 - 52 * std::pow(absLat / 90, 1.5) - 6.5 * std::max(0.0, h) / 1000;
        bool dry = rain < 400, wet = rain > 1200, hot = temp > 20, cold = temp < 3;
        bool sed = r == Rock::Sedimentary, vol = r == Rock::Volcanic, plu = r == Rock::Plutonic, met = r == Rock::Metamorphic,
             shield = r == Rock::Shield, basalt = r == Rock::OceanicBasalt;
        bool arc = (nb == Boundary::Subduction || nb == Boundary::IslandArc) && dB < 400;
        bool collision = nb == Boundary::Collision && dB < 600;
        bool rift = nb == Boundary::Rift && dB < 300;
        double v = 0;
        switch (res) {
            case Iron: v = (shield && age > 1800 ? 0.9 : 0) + (sed ? 0.25 : 0); break;
            case Copper: v = (arc && (vol || plu) ? 0.95 : 0) + (rift && sed ? 0.5 : 0); break;
            case Gold: v = (shield ? 0.7 : 0) + (met && collision ? 0.6 : 0) + (arc && vol ? 0.35 : 0); break;
            case Silver: v = (arc && vol ? 0.9 : 0) + (plu && arc ? 0.4 : 0); break;
            case Lead: case Zinc: v = (sed && age > 200 && land ? 0.7 : 0) + (vol && arc && res == Zinc ? 0.4 : 0); break;
            case Tin: case Tungsten: v = (plu && collision ? 0.95 : 0) + (plu ? 0.25 : 0); break;
            case Nickel: v = (shield && age > 2000 ? 0.8 : 0) + (hot && wet && land && (vol || met) ? 0.5 : 0); break;
            case Cobalt: v = (shield && age > 2000 ? 0.5 : 0) + (arc && vol ? 0.3 : 0) + (rift && sed ? 0.4 : 0); break;
            case Chromium: case Platinum: case Vanadium: v = (shield && age > 2000 ? 0.85 : 0) + (met && collision && res == Chromium ? 0.4 : 0); break;
            case Bauxite: v = land && hot && wet && h < 1200 ? 0.9 : 0; break;
            case Manganese: v = (sed && age > 1500 ? 0.6 : 0) + (sed ? 0.15 : 0) + (h < -4000 ? 0.35 : 0); break;
            case Titanium: v = land && h < 60 && (sed || shield) ? 0.8 : 0; break;
            case Uranium: v = (sed && land ? 0.5 : 0) + (shield && age > 1500 ? 0.6 : 0); break;
            case Lithium: v = (dry && sed && land && h > 1000 ? 0.9 : 0) + (plu ? 0.3 : 0); break;
            case RareEarths: v = (rift ? 0.8 : 0) + (shield ? 0.3 : 0); break;
            case Molybdenum: v = arc && plu ? 0.95 : (arc && vol ? 0.4 : 0); break;
            case Mercury: v = vol && age < 30 ? 0.9 : 0; break;
            case Antimony: v = collision && (met || plu) ? 0.85 : 0; break;
            case Diamond: v = shield && age > 2500 ? 1.0 : 0; break;
            case Emerald: v = collision && plu ? 0.9 : 0; break;
            case Ruby: case Sapphire: v = (collision && met ? 0.9 : 0) + (res == Sapphire && vol && land ? 0.3 : 0); break;
            case Opal: v = dry && sed && land ? 0.9 : 0; break;
            case Jade: v = met && (arc || collision) ? 0.8 : 0; break;
            case Amethyst: v = (vol && land ? 0.8 : 0) + (rift ? 0.4 : 0); break;
            case Topaz: v = plu ? 0.8 : 0; break;
            case Coal: v = sed && land && age > 30 && age < 420 && h < 2000 ? (gauss(age, 330, 70) * 0.9 + 0.35) : 0; break;
            case Oil: case Gas: v = (sed && age < 350 && (land || shelf) ? 0.85 : 0) + (h <= 0 && h > -2500 && !basalt ? 0.45 : 0); break;
            case Peat: v = land && cold && !dry && h < 600 && absLat > 45 ? 0.9 : 0; break;
            case OilShale: v = sed && land ? 0.5 : 0; break;
            case Salt: case Potash: case Gypsum: v = sed && land && (dry || absLat < 40) ? (dry ? 0.95 : 0.35) : 0; break;
            case Sulfur: v = (vol && age < 20 ? 0.9 : 0) + (sed && dry ? 0.25 : 0); break;
            case Phosphate: v = (sed && land && h < 500 ? 0.6 : 0) + (shelf ? 0.3 : 0); break;
            case Limestone: v = sed && land ? 0.85 : 0; break;
            case Clay: v = sed && land && h < 400 ? 0.9 : 0; break;
            case Graphite: v = met ? 0.85 : 0; break;
            case Fluorite: v = (plu ? 0.5 : 0) + (rift ? 0.6 : 0); break;
            case Borax: v = vol && dry && land ? 0.95 : 0; break;
            case SilicaSand: v = land && h < 80 && sed ? 0.9 : 0; break;
            case Granite: v = plu || (shield && land) ? 0.9 : 0; break;
            case Marble: v = met && land ? 0.9 : 0; break;
            case Slate: v = met && land ? 0.8 : 0; break;
            case Sandstone: v = sed && land ? 0.85 : 0; break;
            case Basalt: v = vol && land ? 0.95 : 0; break;
            case Gravel: v = land && h < 300 ? 0.75 : 0; break;
            default: break;
        }
        if (!resourceDefs()[res].offshoreOk && !land) v *= 0.0;
        if (v <= 0) continue;
        // Deposits cluster in districts rather than spreading evenly
        const Vec3d& d = cellDir[i];
        double p = patch(d.x * 6, d.y * 6, d.z * 6) * 0.6 + patch(d.x * 17, d.y * 17, d.z * 17) * 0.4;
        f[i] = (float)clampd(v * smoothstepd(-0.25, 0.45, p), 0, 1);
    }
    return f;
}

void World::generateResources(int seed) {
    deposits.clear();
    const auto& defs = resourceDefs();
    uint32_t s = (uint32_t)seed * 2246822519u + 99u;
    const double scaleCount = std::max(0.2, std::pow(radiusKm / 6371.0, 2.0));   // bigger planets, more deposits
    for (int res = 0; res < (int)defs.size(); res++) {
        std::vector<float> f = favorability(res);
        double total = 0;
        for (int i = 0; i < W * H; i++) total += f[i] * std::cos(cellLat(i / W) * kPi / 180);
        if (total <= 0) continue;
        int want = (int)std::round(defs[res].abundance * scaleCount);
        int tries = 0;
        while (want > 0 && tries < 400000) {
            tries++;
            int i = (int)(rnd01(s) * W * H) % (W * H);
            double w = f[i] * std::cos(cellLat(i / W) * kPi / 180);
            if (rnd01(s) > w) continue;
            Deposit dp;
            dp.res = res;
            dp.lat = cellLat(i / W) + (rnd01(s) - 0.5) * 180.0 / H;
            dp.lon = cellLon(i % W) + (rnd01(s) - 0.5) * 360.0 / W;
            // log-normal sizes: most deposits are small, a few are giants
            double z = 0; for (int k = 0; k < 6; k++) z += rnd01(s); z = (z - 3) / 0.7;
            dp.tonnes = defs[res].typicalTonnes * std::exp(z * 1.1) * (0.5 + f[i]);
            dp.grade = defs[res].gradeLo + (defs[res].gradeHi - defs[res].gradeLo) * std::pow(rnd01(s), 1.5);
            dp.depthM = defs[res].depthLo + (defs[res].depthHi - defs[res].depthLo) * rnd01(s);
            dp.offshore = !elev.empty() && elev[i] <= 0;
            deposits.push_back(dp);
            want--;
        }
    }
}

std::vector<int> World::depositsNear(double lat, double lon, double km, size_t maxCount) const {
    std::vector<std::pair<double, int>> found;
    double la = lat * kPi / 180;
    Vec3d p{std::cos(la) * std::sin(lon * kPi / 180), std::sin(la), std::cos(la) * std::cos(lon * kPi / 180)};
    for (int i = 0; i < (int)deposits.size(); i++) {
        const Deposit& d = deposits[i];
        double dl = d.lat * kPi / 180;
        Vec3d q{std::cos(dl) * std::sin(d.lon * kPi / 180), std::sin(dl), std::cos(dl) * std::cos(d.lon * kPi / 180)};
        double dist = angleBetween(p, q) * radiusKm;
        if (dist <= km) found.push_back({dist, i});
    }
    std::sort(found.begin(), found.end());
    std::vector<int> out;
    for (size_t i = 0; i < found.size() && i < maxCount; i++) out.push_back(found[i].second);
    return out;
}

double World::totalTonnes(int res) const {
    double t = 0;
    for (const Deposit& d : deposits) if (d.res == res) t += d.tonnes;
    return t;
}
