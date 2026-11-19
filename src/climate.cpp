#include "world.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <thread>
#include <functional>

// Climate model. Simplified but physically motivated:
//  - Sunlight per latitude and month from the axial tilt drives temperature; land swings
//    more with the seasons than sea, and air cools 6.5 degC per km of height.
//  - Ocean currents: subtropical gyres make cold currents off west coasts and warm ones
//    off east coasts; mid-latitude west coasts get a warm drift (like western Europe).
//  - Wind belts (trade winds, westerlies, polar easterlies) follow the sun north and south,
//    with monsoon winds blowing towards land that heats up in summer.
//  - Moisture is picked up over warm sea, carried by the wind and rained out where air
//    rises: at the equator, over mountains (rain shadows behind them), and along storm tracks.

const char* biomeName(Biome b) {
    switch (b) {
        case Biome::Ocean: return "Ocean";
        case Biome::SeaIce: return "Sea ice";
        case Biome::IceSheet: return "Ice sheet";
        case Biome::Tundra: return "Tundra";
        case Biome::BorealForest: return "Boreal forest (taiga)";
        case Biome::TemperateForest: return "Temperate forest";
        case Biome::TemperateRainforest: return "Temperate rainforest";
        case Biome::Grassland: return "Grassland / steppe";
        case Biome::Mediterranean: return "Mediterranean shrubland";
        case Biome::ColdDesert: return "Cold desert";
        case Biome::HotDesert: return "Hot desert";
        case Biome::Savanna: return "Savanna";
        case Biome::TropicalSeasonalForest: return "Tropical seasonal forest";
        case Biome::TropicalRainforest: return "Tropical rainforest";
        case Biome::Wetland: return "Wetland";
        default: return "?";
    }
}

void biomeColor(Biome b, unsigned char c[3]) {
    static const unsigned char pal[(int)Biome::Count][3] = {
        {30, 60, 120}, {210, 225, 240}, {245, 248, 252}, {150, 150, 115}, {46, 84, 60}, {60, 120, 52}, {32, 100, 70},
        {170, 180, 95}, {150, 150, 80}, {185, 170, 125}, {225, 195, 130}, {190, 175, 80}, {90, 140, 50}, {25, 90, 35}, {70, 120, 110}};
    int i = std::min((int)b, (int)Biome::Count - 1);
    c[0] = pal[i][0]; c[1] = pal[i][1]; c[2] = pal[i][2];
}

namespace {
// Run f(0..11) for the 12 months on several threads
void forMonths(const std::function<void(int)>& f) {
    std::vector<std::thread> th;
    for (int m = 0; m < 12; m++) th.emplace_back(f, m);
    for (auto& t : th) t.join();
}
// Daily-average sunlight (equator at equinox = 1)
double insolation(double latDeg, double declDeg) {
    double phi = latDeg * kPi / 180, d = declDeg * kPi / 180;
    double c = -std::tan(phi) * std::tan(d);
    double h0 = c >= 1 ? 0 : (c <= -1 ? kPi : std::acos(c));
    return (h0 * std::sin(phi) * std::sin(d) + std::cos(phi) * std::cos(d) * std::sin(h0));
}
double annualBaseTemp(double absLat) { return 27.0 - 52.0 * std::pow(absLat / 90.0, 1.5); }
}

void World::generateClimate(double axialTiltDeg, double tempOffset, double rainMultiplier) {
    tiltDeg = axialTiltDeg;
    const int N = W * H;
    const double ck = cellKm();
    std::vector<bool> land(N);
    for (int i = 0; i < N; i++) land[i] = elev[i] > 0;

    // 1. Distance from the sea (continentality)
    distOceanKm.assign(N, 0);
    {
        using QE = std::pair<float, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
        std::vector<float> d(N, 1e9f);
        for (int i = 0; i < N; i++) if (!land[i]) { d[i] = 0; pq.push({0.f, i}); }
        while (!pq.empty()) {
            auto [dd, i] = pq.top(); pq.pop();
            if (dd > d[i]) continue;
            int x = i % W, y = i / W;
            double ew = std::max(0.05, std::cos(cellLat(y) * kPi / 180)) * ck;
            const int nb[4] = {idx(x + 1, y), idx(x - 1, y), idx(x, y + 1), idx(x, y - 1)};
            const double st[4] = {ew, ew, ck, ck};
            for (int k = 0; k < 4; k++) {
                float nd = (float)(dd + st[k]);
                if (nd < d[nb[k]]) { d[nb[k]] = nd; pq.push({nd, nb[k]}); }
            }
        }
        for (int i = 0; i < N; i++) distOceanKm[i] = land[i] ? std::min(d[i], 5000.f) : 0.f;
    }

    // 2. Distance along each row to land on the west and east (to place currents)
    std::vector<float> landWestKm(N, 1e9f), landEastKm(N, 1e9f);
    for (int y = 0; y < H; y++) {
        double ew = std::max(0.05, std::cos(cellLat(y) * kPi / 180)) * ck;
        double run = 1e9;
        for (int k = 0; k < 2 * W; k++) {           // sweep eastwards: distance to land on the west
            int i = y * W + (k % W);
            run = land[i] ? 0 : run + ew;
            if (k >= W) landWestKm[i] = (float)std::min(run, 1e9);
        }
        run = 1e9;
        for (int k = 2 * W - 1; k >= 0; k--) {      // sweep westwards: distance to land on the east
            int i = y * W + (k % W);
            run = land[i] ? 0 : run + ew;
            if (k < W) landEastKm[i] = (float)std::min(run, 1e9);
        }
    }

    // 3. Ocean currents and sea temperature anomalies
    currentU.assign(N, 0); currentV.assign(N, 0); seaTempAnomaly.assign(N, 0);
    for (int i = 0; i < N; i++) {
        if (land[i]) continue;
        double lat = cellLat(i / W), a = std::fabs(lat), sgn = lat >= 0 ? 1 : -1;
        double nearWest = std::exp(-landWestKm[i] / 700.0);   // just east of a continent (its east coast)
        double nearEast = std::exp(-landEastKm[i] / 900.0);   // just west of a continent (its west coast)
        double u = 0, v = 0;
        // open-ocean flow
        if (a < 20) u = -0.5;                                   // equatorial currents run west
        else if (a < 30) u = -0.25 * (30 - a) / 10;
        else if (a < 62) u = 0.4 * std::sin((a - 30) / 32 * kPi);   // westerly drift runs east
        else u = -0.15;
        bool openRow = landWestKm[i] > 1e8 && landEastKm[i] > 1e8;   // circumpolar ocean
        if (openRow && a > 40) u = 0.8;
        // boundary currents
        double subtrop = std::exp(-std::pow((a - 28) / 12.0, 2.0)), subpolar = std::exp(-std::pow((a - 55) / 9.0, 2.0));
        v += sgn * nearWest * (1.2 * subtrop - 0.7 * subpolar);      // western boundary: Gulf Stream (poleward), Labrador (equatorward)
        v += sgn * nearEast * (-0.6 * subtrop + 0.4 * subpolar);     // eastern boundary: California/Canary (equatorward), drift (poleward)
        currentU[i] = (float)u; currentV[i] = (float)v;
        double anom = nearWest * (3.5 * subtrop - 4.5 * subpolar) + nearEast * (-4.5 * subtrop + 5.0 * subpolar);
        seaTempAnomaly[i] = (float)anom;
    }
    // Land near a coast takes on the sea's anomaly, carried by the prevailing wind
    std::vector<float> landAnom(N, 0);
    for (int y = 0; y < H; y++) {
        double a = std::fabs(cellLat(y));
        bool westerlies = a > 35 && a < 70;
        double ew = std::max(0.05, std::cos(cellLat(y) * kPi / 180)) * ck;
        double carried = 0, distKm = 1e9;
        int dir = westerlies ? 1 : -1;
        for (int k = 0; k < 2 * W; k++) {
            int x = dir > 0 ? (k % W) : (W - 1 - (k % W));
            int i = y * W + x;
            if (!land[i]) { carried = seaTempAnomaly[i]; distKm = 0; }
            else distKm += ew;
            if (k >= W && land[i]) landAnom[i] = (float)(carried * std::exp(-distKm / (westerlies ? 900.0 : 400.0)));
        }
    }

    // 4. Monthly temperature and wind
    double declOf[12];
    for (int m = 0; m < 12; m++) declOf[m] = tiltDeg * std::sin(2 * kPi * (m + 0.5 - 2.7) / 12.0);   // m = 0 is January
    std::vector<double> qRow(12 * H), qMean(H, 0);
    for (int y = 0; y < H; y++) {
        for (int m = 0; m < 12; m++) { qRow[m * H + y] = insolation(cellLat(y), declOf[m]); qMean[y] += qRow[m * H + y] / 12; }
    }
    for (int m = 0; m < 12; m++) {
        tempMonth[m].assign(N, 0);
        int mLand = (m + 11) % 12, mSea = (m + 10) % 12;   // land lags the sun ~1 month, sea ~2
        for (int i = 0; i < N; i++) {
            int y = i / W;
            double lat = cellLat(y), a = std::fabs(lat);
            double base = annualBaseTemp(a);
            double t;
            if (land[i]) {
                double cont = smoothstepd(0, 1600, distOceanKm[i]);
                double k = 14 + 34 * cont;
                t = base + k * (qRow[mLand * H + y] - qMean[y]) - 6.5 * std::max(0.0f, elev[i]) / 1000.0 + landAnom[i];
                t -= 4 * cont * smoothstepd(40, 70, a);   // big cold continental interiors
            } else {
                t = base + 7 * smoothstepd(35, 80, a) + 9 * (qRow[mSea * H + y] - qMean[y]) + seaTempAnomaly[i];
                t = std::max(t, -2.0);
            }
            tempMonth[m][i] = (float)(land[i] ? t + tempOffset : std::max(-2.0, t + tempOffset));
        }
    }
    for (int m = 0; m < 12; m++) {
        windU[m].assign(N, 0); windV[m].assign(N, 0);
        double itcz = declOf[m] * 0.45;
        // zonal-mean temperature, so land that's hotter or colder than its latitude can pull monsoon winds
        std::vector<double> rowMean(H, 0);
        for (int y = 0; y < H; y++) { double s = 0; for (int x = 0; x < W; x++) s += tempMonth[m][y * W + x]; rowMean[y] = s / W; }
        for (int y = 0; y < H; y++) {
            double lat = cellLat(y) - itcz;
            double phi = lat * kPi / 180;
            double a = std::fabs(lat);
            double u = a < 62 ? -std::cos(3 * phi) : -0.6 * smoothstepd(62, 75, a) + 0.4 * (1 - smoothstepd(62, 75, a));
            double v = -0.5 * std::sin(6 * phi);
            if (a > 85) v = 0;
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                // monsoon: wind blows towards land that's warmer than its latitude's average
                double gx = (tempMonth[m][idx(x + 1, y)] - rowMean[y]) - (tempMonth[m][idx(x - 1, y)] - rowMean[y]);
                double gy = (tempMonth[m][idx(x, y - 1)] - rowMean[clampY(y - 1)]) - (tempMonth[m][idx(x, y + 1)] - rowMean[clampY(y + 1)]);
                double mon = 0.08 * (1 - smoothstepd(35, 55, std::fabs(cellLat(y))));
                double uu = u * 7 + gx * mon * 25, vv = v * 7 + gy * mon * 25;
                if (land[i]) { uu *= 0.75; vv *= 0.75; }   // friction over land
                windU[m][i] = (float)uu; windV[m][i] = (float)vv;
            }
        }
    }
    // Smooth the wind a little so monsoon gradients don't look noisy
    for (int m = 0; m < 12; m++) {
        for (int pass = 0; pass < 2; pass++) {
            std::vector<float> u2(N), v2(N);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    int i = y * W + x;
                    u2[i] = (windU[m][i] * 2 + windU[m][idx(x + 1, y)] + windU[m][idx(x - 1, y)] + windU[m][idx(x, y + 1)] + windU[m][idx(x, y - 1)]) / 6;
                    v2[i] = (windV[m][i] * 2 + windV[m][idx(x + 1, y)] + windV[m][idx(x - 1, y)] + windV[m][idx(x, y + 1)] + windV[m][idx(x, y - 1)]) / 6;
                }
            windU[m].swap(u2); windV[m].swap(v2);
        }
    }

    // 5. Rainfall. Each month: a base amount from the wind belts (wet at the equator and in the
    //    storm tracks, dry under the subtropical highs). Over land, trace the wind back to the sea:
    //    moisture fades with distance over land and with every mountain range crossed (rain shadow),
    //    and is boosted where the wind is forced up a slope.
    forMonths([&](int m) {
        rainMonth[m].assign(N, 0);
        double itcz = declOf[m] * 0.45;
        const std::vector<float>& U = windU[m];
        const std::vector<float>& V = windV[m];
        const std::vector<float>& T = tempMonth[m];
        for (int y = 0; y < H; y++) {
            double lat = cellLat(y), rel = std::fabs(lat - itcz), a = std::fabs(lat);
            double zonal = 22 + 230 * std::exp(-std::pow(rel / 10.0, 2.0)) + 75 * std::exp(-std::pow((a - 50) / 13.0, 2.0));
            zonal *= 1 - 0.75 * std::exp(-std::pow((rel - 25) / 8.0, 2.0));
            zonal *= 1 - 0.6 * smoothstepd(65, 85, a);
            double ew = std::max(0.08, std::cos(lat * kPi / 180)) * ck;
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                double coldFactor = clampd((T[i] + 12) / 30.0, 0.12, 1.25);   // cold air holds less water
                if (!land[i]) {
                    rainMonth[m][i] = (float)(zonal * clampd(0.45 + T[i] / 30.0, 0.25, 1.4) * rainMultiplier);
                    continue;
                }
                // trace upwind until reaching the sea (max ~2,500 km)
                double px = x, py = y, overLand = 0, barrier = 0, prevH = std::max(0.0f, elev[i]);
                double srcT = 5, lift = 0;
                bool reached = false;
                for (int step = 0; step < 45; step++) {
                    int ci = idx((int)std::round(px), (int)std::round(py));
                    double u = U[ci], v = V[ci], sp = std::sqrt(u * u + v * v) + 1e-6;
                    double cew = std::max(0.08, std::cos(cellLat(clampY((int)std::round(py))) * kPi / 180)) * ck;
                    px -= u / sp * (ck / cew);
                    py += v / sp;
                    int ni = idx((int)std::round(px), (int)std::round(py));
                    if (!land[ni]) { srcT = T[ni]; reached = true; break; }
                    double hN = std::max(0.0f, elev[ni]);
                    // moving downwind we go from ni to the previous cell: rises along that way are barriers
                    if (prevH > hN) barrier += prevH - hN;
                    if (step == 0) lift = std::max(0.0, std::max(0.0f, elev[i]) - hN);
                    prevH = hN;
                    overLand += ck;
                }
                (void)ew;
                if (!reached) overLand += 1500;
                double supply = std::exp(-overLand / 2800.0) * std::exp(-barrier / 2600.0) * clampd(0.5 + srcT / 30.0, 0.3, 1.4);
                double r = zonal * 1.65 * (0.18 + 0.82 * supply) + lift / 1000.0 * 320 * supply;
                rainMonth[m][i] = (float)((r * coldFactor + 6) * rainMultiplier);
            }
        }
    });

    // 6. Annual summaries and biomes
    tempYear.assign(N, 0); rainfallYear.assign(N, 0); tempSeaLevel.assign(N, 0);
    biome.assign(N, 0);
    for (int i = 0; i < N; i++) {
        double tSum = 0, rSum = 0, tMin = 1e9, tMax = -1e9, rMin = 1e9, rSummer = 0, rWinter = 0;
        bool north = cellLat(i / W) >= 0;
        for (int m = 0; m < 12; m++) {
            double t = tempMonth[m][i], r = rainMonth[m][i];
            tSum += t; rSum += r;
            tMin = std::min(tMin, t); tMax = std::max(tMax, t); rMin = std::min(rMin, r);
            bool summer = north ? (m >= 3 && m <= 8) : (m <= 2 || m >= 9);
            (summer ? rSummer : rWinter) += r;
        }
        double T = tSum / 12, P = rSum;
        tempYear[i] = (float)T;
        rainfallYear[i] = (float)P;
        tempSeaLevel[i] = (float)(T + 6.5 * std::max(0.0f, elev[i]) / 1000.0);
        Biome b;
        if (!land[i]) b = tMax < -1.0 ? Biome::SeaIce : Biome::Ocean;
        else {
            double pet = std::max(150.0, 250 + 35 * std::max(0.0, T));      // rough evaporation demand, mm/yr
            double ai = P / pet;                                           // aridity index
            if (tMax < 0) b = Biome::IceSheet;
            else if (tMax < 10) b = Biome::Tundra;
            else if (ai < 0.15) b = T > 15 ? Biome::HotDesert : Biome::ColdDesert;
            else if (tMin > 18) {
                if (rMin > 60) b = Biome::TropicalRainforest;
                else if (ai > 0.9) b = Biome::TropicalSeasonalForest;
                else b = Biome::Savanna;
            } else if (ai < 0.45) b = (T > 18 && tMin > 10) ? Biome::Savanna : Biome::Grassland;
            else if (rSummer < rWinter * 0.45 && tMin > 0 && T > 10) b = Biome::Mediterranean;
            else if (tMin < -3 && tMax < 22) b = Biome::BorealForest;
            else if (ai > 2.0 && tMin > -2) b = Biome::TemperateRainforest;
            else b = Biome::TemperateForest;
            if (ai > 2.2 && elev[i] < 120 && T > 0 && b != Biome::TropicalRainforest && b != Biome::TemperateRainforest) b = Biome::Wetland;
        }
        biome[i] = (uint8_t)b;
    }
}
