#include "cityeditor.h"
#include "fonts.h"
#include "raygui.h"
#include "ui.h"
#include "persistence.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace {
uint32_t rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
double rnd(uint32_t& s) { return rng(s) / 16777216.0; }
Vec3d dirOf(double lat, double lon) {
    double la = lat * kPi / 180, lo = lon * kPi / 180;
    return {std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)};
}
const Color kDistrictCol[] = {{0, 0, 0, 0}, {240, 190, 60, 255}, {200, 120, 70, 255}, {170, 80, 190, 255}, {250, 240, 160, 255},
                              {220, 150, 120, 255}, {60, 140, 200, 255}, {120, 120, 130, 255}, {150, 190, 90, 255}, {150, 120, 100, 255},
                              {60, 170, 80, 255}};
const Color kRoof[] = {{120, 110, 100, 255}, {178, 92, 62, 255}, {160, 80, 58, 255}, {98, 92, 120, 255}, {200, 190, 160, 255},
                       {170, 96, 70, 255}, {110, 100, 92, 255}, {90, 90, 96, 255}, {190, 170, 110, 255}, {140, 120, 95, 255}, {150, 100, 70, 255}};
}

double CityEditor::heightAt(double x, double y) const {
    int px = (int)((x + sizeM_ / 2) / sizeM_ * G), py = (int)((sizeM_ / 2 - y) / sizeM_ * G);
    if (px < 0 || py < 0 || px >= G || py >= G) return 0;
    return elev_[py * G + px];
}
bool CityEditor::water(double x, double y) const {
    int px = (int)((x + sizeM_ / 2) / sizeM_ * G), py = (int)((sizeM_ / 2 - y) / sizeM_ * G);
    if (px < 0 || py < 0 || px >= G || py >= G) return true;
    return waterMask_[py * G + px] != 0;
}
bool CityEditor::sea(double x, double y) const {
    int px = (int)((x + sizeM_ / 2) / sizeM_ * G), py = (int)((sizeM_ / 2 - y) / sizeM_ * G);
    if (px < 0 || py < 0 || px >= G || py >= G) return true;
    return waterMask_[py * G + px] == 1 || waterMask_[py * G + px] == 3;
}
int CityEditor::districtAt(double x, double y) const {
    int dx = (int)((x + sizeM_ / 2) / sizeM_ * DG), dy = (int)((sizeM_ / 2 - y) / sizeM_ * DG);
    if (dx < 0 || dy < 0 || dx >= DG || dy >= DG) return 0;
    return district_[dy * DG + dx];
}
Vector2 CityEditor::toScreen(double x, double y) const {
    return {(float)(GetScreenWidth() / 2.0 + 180 + (x - camX_) * zoom_), (float)(GetScreenHeight() / 2.0 - (y - camY_) * zoom_)};
}
Vector2 CityEditor::toLocal(Vector2 s) const {
    return {(float)((s.x - GetScreenWidth() / 2.0 - 180) / zoom_ + camX_), (float)(-(s.y - GetScreenHeight() / 2.0) / zoom_ + camY_)};
}

void CityEditor::open(int city, const Planet& planet, const World& world, Document& doc) {
    close();
    if (city < 0 || city >= (int)doc.cities.size()) return;
    city_ = city;
    const City& c = doc.cities[city];
    lat0_ = c.lat; lon0_ = c.lon;
    era_ = doc.era;
    cityR_ = clampd(std::sqrt((double)std::max(200, c.population)) * 4.2, 250, 12000);
    sizeM_ = std::max(3000.0, cityR_ * 2.9);
    buildTerrain(planet, world);
    CityPlan* plan = doc.planFor(city);
    if (!plan) {
        CityPlan p; p.city = city;
        uint32_t h = 2166136261u; for (char ch : c.name) { h ^= (unsigned char)ch; h *= 16777619u; }
        p.seed = h ^ (uint32_t)(city * 7919);
        p.walls = doc.era <= 2 && c.population > 3000;
        doc.plans.push_back(p);
    }
    generate(doc);
    makeLandmarks(doc);
    rebuildPaint(*doc.planFor(city));
    camX_ = cx_; camY_ = cy_;
    zoom_ = (GetScreenHeight() - 80) / (cityR_ * 2.6);
    active = true;
}

void CityEditor::close() {
    if (haveTex_) UnloadTexture(terrain_);
    haveTex_ = false;
    active = false;
    roads_.clear(); objects_.clear(); wall_.clear(); drawing_.clear(); undo_.clear();
    if (havePaint_) UnloadTexture(paintTex_);
    havePaint_ = false; selObj_ = -1;
    landmarkSel_ = -1; lastLmSel_ = -2; tool_ = 0;
}

void CityEditor::buildTerrain(const Planet& planet, const World& world) {
    const double R = planet.radiusKm();
    const double kmPerDegLat = kPi * R / 180.0, kmPerDegLon = kmPerDegLat * std::max(0.05, std::cos(lat0_ * kPi / 180));
    elev_.assign(G * G, 0);
    waterMask_.assign(G * G, 0);
    const double pxM = sizeM_ / G;
    const int oct = Planet::detailOctavesFor(pxM / 1000.0, R);
    auto latlon = [&](int px, int py, double& la, double& lo) {
        double x = (px + 0.5) * pxM - sizeM_ / 2, y = sizeM_ / 2 - (py + 0.5) * pxM;
        la = lat0_ + y / 1000.0 / kmPerDegLat; lo = lon0_ + x / 1000.0 / kmPerDegLon;
    };
    std::vector<std::thread> th;
    unsigned n = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned t = 0; t < n; t++)
        th.emplace_back([&, t]() {
            for (int py = (int)t; py < G; py += (int)n)
                for (int px = 0; px < G; px++) {
                    double la, lo; latlon(px, py, la, lo);
                    elev_[py * G + px] = (float)planet.elevation(dirOf(la, lo), oct);
                }
        });
    for (auto& t : th) t.join();
    for (int py = 0; py < G; py++)
        for (int px = 0; px < G; px++) {
            int i = py * G + px;
            double la, lo; latlon(px, py, la, lo);
            if (elev_[i] <= 0) waterMask_[i] = 1;
            else if (!world.lakeMask.empty() && World::sample(world.lakeMask, la, lo) > 0.3 && elev_[i] < World::sample(world.lakeSurface, la, lo)) waterMask_[i] = 3;
        }
    // rivers passing through the area
    for (const World::River& r : world.rivers) {
        for (size_t k = 1; k < r.pts.size(); k++) {
            auto loc = [&](const Vec3d& p, double& x, double& y) {
                double la = std::asin(clampd(p.y, -1, 1)) * 180 / kPi, lo = std::atan2(p.x, p.z) * 180 / kPi;
                double dl = std::remainder(lo - lon0_, 360.0);
                x = dl * kmPerDegLon * 1000; y = (la - lat0_) * kmPerDegLat * 1000;
            };
            double x0, y0, x1, y1;
            loc(r.pts[k - 1], x0, y0); loc(r.pts[k], x1, y1);
            double wM = std::max(12.0, 7.0 * std::sqrt((double)r.q[k]));
            double lim = sizeM_ / 2 + wM;
            if ((std::fabs(x0) > lim && std::fabs(x1) > lim && x0 * x1 > 0) || (std::fabs(y0) > lim && std::fabs(y1) > lim && y0 * y1 > 0)) continue;
            if (std::hypot(x1 - x0, y1 - y0) > sizeM_ * 3) continue;
            int pa = (int)((std::min(x0, x1) - wM + sizeM_ / 2) / pxM) - 1, pb = (int)((std::max(x0, x1) + wM + sizeM_ / 2) / pxM) + 1;
            int qa = (int)((sizeM_ / 2 - std::max(y0, y1) - wM) / pxM) - 1, qb = (int)((sizeM_ / 2 - std::min(y0, y1) + wM) / pxM) + 1;
            for (int py = std::max(0, qa); py <= std::min(G - 1, qb); py++)
                for (int px = std::max(0, pa); px <= std::min(G - 1, pb); px++) {
                    double x = (px + 0.5) * pxM - sizeM_ / 2, y = sizeM_ / 2 - (py + 0.5) * pxM;
                    double dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy;
                    double t = L2 > 0 ? clampd(((x - x0) * dx + (y - y0) * dy) / L2, 0, 1) : 0;
                    double d = std::hypot(x - (x0 + dx * t), y - (y0 + dy * t));
                    if (d < wM / 2 && waterMask_[py * G + px] == 0) waterMask_[py * G + px] = 2;
                }
        }
    }
    // terrain image with hillshading
    std::vector<unsigned char> img(G * G * 4);
    for (int py = 0; py < G; py++)
        for (int px = 0; px < G; px++) {
            int i = py * G + px;
            double la, lo; latlon(px, py, la, lo);
            unsigned char c[4];
            planet.surfaceColor(dirOf(la, lo), elev_[i], c);
            double hl = elev_[py * G + std::max(0, px - 1)], hr = elev_[py * G + std::min(G - 1, px + 1)];
            double hu = elev_[std::max(0, py - 1) * G + px], hd = elev_[std::min(G - 1, py + 1) * G + px];
            double gx = (hr - hl) / (2 * pxM) * 2.5, gn = (hu - hd) / (2 * pxM) * 2.5;
            double shade = ((-gx) * -0.62 + (-gn) * 0.62 + 0.48) / std::sqrt(gx * gx + gn * gn + 1) / 0.48;
            double f = elev_[i] > 0 ? clampd(1 + (shade - 1) * 0.6, 0.6, 1.25) : 1.0;
            if (waterMask_[i] >= 2) { c[0] = 70; c[1] = 130; c[2] = 190; f = 1; }
            img[i * 4 + 0] = (unsigned char)clampd(c[0] * f, 0, 255);
            img[i * 4 + 1] = (unsigned char)clampd(c[1] * f, 0, 255);
            img[i * 4 + 2] = (unsigned char)clampd(c[2] * f, 0, 255);
            img[i * 4 + 3] = 255;
        }
    Image im{img.data(), G, G, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    terrain_ = LoadTextureFromImage(im);
    SetTextureFilter(terrain_, TEXTURE_FILTER_BILINEAR);
    haveTex_ = true;
}


// ---- occupancy grid: where buildings, roads and water already are ----
void CityEditor::occClear() {
    oc_ = std::max(3.0, sizeM_ / 1400.0);
    OG_ = (int)std::ceil(sizeM_ / oc_);
    occ_.assign((size_t)OG_ * OG_, 0);
    for (int gy = 0; gy < OG_; gy++)
        for (int gx = 0; gx < OG_; gx++) {
            double x = (gx + 0.5) * oc_ - sizeM_ / 2, y = sizeM_ / 2 - (gy + 0.5) * oc_;
            if (water(x, y)) occ_[(size_t)gy * OG_ + gx] = 1;
        }
}
bool CityEditor::occFree(double x, double y, double w, double d, double ang) const {
    // footprints are tested a cell smaller so neighbours can share a wall
    w = std::max(0.5, w - oc_ * 1.1); d = std::max(0.5, d - oc_ * 1.1);
    double c = std::cos(ang), sn = std::sin(ang), st = oc_ * 0.8;
    for (double u = -w / 2; u <= w / 2 + 0.01; u += std::min(st, w / 2 + 0.01))
        for (double v = -d / 2; v <= d / 2 + 0.01; v += std::min(st, d / 2 + 0.01)) {
            double px = x + c * u - sn * v, py = y + sn * u + c * v;
            int gx = (int)((px + sizeM_ / 2) / oc_), gy = (int)((sizeM_ / 2 - py) / oc_);
            if (gx < 0 || gy < 0 || gx >= OG_ || gy >= OG_ || occ_[(size_t)gy * OG_ + gx]) return false;
        }
    return true;
}
void CityEditor::occMark(double x, double y, double w, double d, double ang) {
    w = std::max(0.5, w - oc_ * 1.1); d = std::max(0.5, d - oc_ * 1.1);
    double c = std::cos(ang), sn = std::sin(ang), st = oc_ * 0.5;
    for (double u = -w / 2; u <= w / 2 + 0.01; u += std::min(st, w / 2 + 0.01))
        for (double v = -d / 2; v <= d / 2 + 0.01; v += std::min(st, d / 2 + 0.01)) {
            double px = x + c * u - sn * v, py = y + sn * u + c * v;
            int gx = (int)((px + sizeM_ / 2) / oc_), gy = (int)((sizeM_ / 2 - py) / oc_);
            if (gx >= 0 && gy >= 0 && gx < OG_ && gy < OG_) occ_[(size_t)gy * OG_ + gx] = 2;
        }
}
void CityEditor::occRoad(const Road& r, double halfW) {
    for (size_t k = 1; k < r.pts.size(); k++) {
        double x0 = r.pts[k - 1].x, y0 = r.pts[k - 1].y, x1 = r.pts[k].x, y1 = r.pts[k].y;
        double len = std::hypot(x1 - x0, y1 - y0);
        if (len < 0.01) continue;
        occMark((x0 + x1) / 2, (y0 + y1) / 2, len + halfW + oc_ * 1.1, halfW * 2 + oc_ * 1.1, std::atan2(y1 - y0, x1 - x0));
    }
}

void CityEditor::generate(Document& doc) {
    CityPlan& plan = *doc.planFor(city_);
    uint32_t s = plan.seed;
    roads_.clear(); objects_.clear(); wall_.clear(); wallOnLand_.clear();
    const double step = std::max(25.0, cityR_ / 30);
    // move the centre onto dry land if it's in water
    double cx = 0, cy = 0;
    if (water(0, 0)) {
        for (double r = step; r < sizeM_ / 2 && water(cx, cy); r += step)
            for (int k = 0; k < 16; k++) { double a = k * kPi / 8; if (!water(r * std::cos(a), r * std::sin(a))) { cx = r * std::cos(a); cy = r * std::sin(a); break; } }
    }
    cx_ = cx; cy_ = cy;
    // which road is where (coarse), so streets end where they meet another road instead of crossing it
    const double rgc = 10;
    const int RG = (int)std::ceil(sizeM_ / rgc);
    std::vector<int> rg((size_t)RG * RG, -1);
    auto rgAt = [&](double x, double y) -> int& {
        static int dummy = -2;
        int gx = (int)((x + sizeM_ / 2) / rgc), gy = (int)((sizeM_ / 2 - y) / rgc);
        if (gx < 0 || gy < 0 || gx >= RG || gy >= RG) { dummy = -2; return dummy; }
        return rg[(size_t)gy * RG + gx];
    };
    auto stamp = [&](const Road& r, int id) {
        for (size_t k = 1; k < r.pts.size(); k++) {
            double x0 = r.pts[k - 1].x, y0 = r.pts[k - 1].y, x1 = r.pts[k].x, y1 = r.pts[k].y;
            double len = std::hypot(x1 - x0, y1 - y0);
            for (double t = 0; t <= len; t += rgc * 0.5) { int& c = rgAt(x0 + (x1 - x0) * t / std::max(1e-6, len), y0 + (y1 - y0) * t / std::max(1e-6, len)); if (c != -2) c = id; }
        }
    };
    // a road walker that bends around water but can bridge rivers
    auto walk = [&](double x, double y, double ang, double len, double wiggle, int kind, bool bridges) {
        Road rd; rd.kind = kind;
        rd.pts.push_back({(float)x, (float)y});
        double st = kind == 0 ? step : step * 0.6;
        const int myId = (int)roads_.size();
        int startOn = rgAt(x, y);
        for (double d = 0; d < len; d += st) {
            ang += (rnd(s) - 0.5) * wiggle;
            bool moved = false;
            for (int tries = 0; tries < 9; tries++) {
                double a = ang + (tries == 0 ? 0 : ((tries & 1) ? 1 : -1) * ((tries + 1) / 2) * 0.35);
                double nx = x + std::cos(a) * st, ny = y + std::sin(a) * st;
                if (std::fabs(nx) > sizeM_ / 2 || std::fabs(ny) > sizeM_ / 2) break;
                bool wet = water(nx, ny);
                if (wet && !(bridges && !sea(nx, ny))) continue;
                x = nx; y = ny; ang = a; moved = true;
                break;
            }
            if (!moved) break;
            rd.pts.push_back({(float)x, (float)y});
            // streets stop when they run into another road (a T-junction)
            if (kind > 0 && d > st * 1.5) {
                int other = rgAt(x, y);
                if (other >= 0 && other != myId && other != startOn) break;
            }
        }
        if (rd.pts.size() >= 2) { stamp(rd, myId); roads_.push_back(rd); }
        return rd.pts.size();
    };
    const double marketR = clampd(cityR_ * 0.07, 22, 130);
    int mains = 3 + (int)(rnd(s) * 3) + (cityR_ > 2000 ? 2 : 0);
    double base = rnd(s) * 2 * kPi;
    for (int k = 0; k < mains; k++) {
        double a = base + k * 2 * kPi / mains + (rnd(s) - 0.5) * 0.4;
        walk(cx + std::cos(a) * marketR, cy + std::sin(a) * marketR, a, sizeM_ * 0.7, 0.25, 0, true);
    }
    // streets branching off the main roads, denser near the centre
    size_t nMain = roads_.size();
    for (size_t r = 0; r < nMain; r++) {
        Road main = roads_[r];
        for (size_t k = 2; k < main.pts.size(); k += 2) {
            double px = main.pts[k].x, py = main.pts[k].y, dist = std::hypot(px - cx, py - cy);
            if (dist > cityR_ * 1.3 || rnd(s) > 0.9 * std::exp(-std::pow(dist / cityR_, 2.0)) + 0.12) continue;
            double dir = std::atan2(main.pts[k].y - main.pts[k - 1].y, main.pts[k].x - main.pts[k - 1].x);
            for (int side = -1; side <= 1; side += 2) {
                if (rnd(s) < 0.2) continue;
                size_t before = roads_.size();
                walk(px, py, dir + side * kPi / 2 + (rnd(s) - 0.5) * 0.3, cityR_ * (0.25 + rnd(s) * 0.45), 0.12, 1, false);
                if (roads_.size() > before) {
                    Road st = roads_.back();
                    for (size_t q = 2; q < st.pts.size(); q += 2) {
                        double d2 = std::hypot(st.pts[q].x - cx, st.pts[q].y - cy);
                        if (d2 > cityR_ * 0.95 || rnd(s) > 0.6) continue;
                        double dd = std::atan2(st.pts[q].y - st.pts[q - 1].y, st.pts[q].x - st.pts[q - 1].x);
                        walk(st.pts[q].x, st.pts[q].y, dd + (rnd(s) < 0.5 ? 1 : -1) * kPi / 2, cityR_ * (0.08 + rnd(s) * 0.16), 0.08, 2, false);
                    }
                }
            }
        }
    }
    // walls
    wallR_ = 0;
    if (plan.walls && era_ <= 3) {
        wallR_ = cityR_ * 0.55;
        const int n = 64;
        for (int k = 0; k <= n; k++) {
            double a = 2 * kPi * k / n;
            double r = wallR_ * (1 + 0.08 * std::sin(a * 3 + plan.seed % 7) + 0.05 * std::sin(a * 7 + plan.seed % 11));
            double x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
            wall_.push_back({(float)x, (float)y});
            wallOnLand_.push_back(!water(x, y));
        }
        // a lane running round just inside the wall
        Road ring; ring.kind = 2;
        for (size_t k = 0; k < wall_.size(); k++) {
            double x = cx + (wall_[k].x - cx) * 0.93, y = cy + (wall_[k].y - cy) * 0.93;
            if (water(x, y)) { if (ring.pts.size() >= 2) roads_.push_back(ring); ring.pts.clear(); continue; }
            ring.pts.push_back({(float)x, (float)y});
        }
        if (ring.pts.size() >= 2) roads_.push_back(ring);
    }
    for (auto& r : plan.extraRoads) {
        Road rd; rd.kind = 3;
        for (auto& p : r) rd.pts.push_back({p.first, p.second});
        roads_.push_back(rd);
    }
    // districts
    district_.assign(DG * DG, 0);
    double hiX = cx, hiY = cy, hiH = -1e9;   // highest point near the centre (for the castle)
    for (int k = 0; k < 400; k++) {
        double a = rnd(s) * 2 * kPi, r = std::sqrt(rnd(s)) * cityR_ * 0.45;
        double x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
        if (std::hypot(x - cx, y - cy) < marketR * 2.5) continue;
        if (!water(x, y) && heightAt(x, y) > hiH) { hiH = heightAt(x, y); hiX = x; hiY = y; }
    }
    double tAng = rnd(s) * 2 * kPi, tx = cx + std::cos(tAng) * cityR_ * 0.18, ty = cy + std::sin(tAng) * cityR_ * 0.18;
    for (int dy = 0; dy < DG; dy++)
        for (int dx = 0; dx < DG; dx++) {
            double x = (dx + 0.5) / DG * sizeM_ - sizeM_ / 2, y = sizeM_ / 2 - (dy + 0.5) / DG * sizeM_;
            if (water(x, y)) continue;
            double r = std::hypot(x - cx, y - cy);
            bool shore = false;
            for (int k = 0; k < 8 && !shore; k++) { double a = k * kPi / 4; if (water(x + std::cos(a) * cityR_ * 0.08, y + std::sin(a) * cityR_ * 0.08)) shore = true; }
            District d = District::None;
            if (r < cityR_ * 0.12) d = District::Market;
            else if (std::hypot(x - hiX, y - hiY) < cityR_ * 0.14) d = District::Noble;
            else if (std::hypot(x - tx, y - ty) < cityR_ * 0.08) d = District::Temple;
            else if (shore && r < cityR_ * 1.05) d = era_ >= 3 && r > cityR_ * 0.5 ? District::Industry : District::Docks;
            else if (wallR_ > 0 && r < wallR_) d = District::OldTown;
            else if (r < cityR_) d = (era_ >= 3 && rnd(s) < 0.04) ? District::Park : District::Residential;
            else if (r < cityR_ * 1.25) d = (wallR_ > 0 && rnd(s) < 0.6) ? District::Slums : District::Residential;
            else if (r < cityR_ * 2.2) d = District::Farms;
            district_[dy * DG + dx] = (uint8_t)d;
        }
    baseDistrict_ = district_;
    for (auto& e : plan.districtEdits) if (e.first >= 0 && e.first < DG * DG) district_[e.first] = e.second;

    if (plan.baked) { objects_ = plan.objects; return; }

    // ---- buildings, fields and trees ----
    occClear();
    for (const Road& rd : roads_) occRoad(rd, (rd.kind == 0 ? 6 : rd.kind == 1 ? 4 : rd.kind == 2 ? 2.5 : 5) * 0.75);
    auto add = [&](CityObj t, double x, double y, double w, double d, double ang, uint8_t dist) {
        CityObject o; o.type = t; o.x = (float)x; o.y = (float)y; o.w = (float)w; o.d = (float)d; o.ang = (float)ang; o.shade = (uint8_t)(rng(s) & 255); o.district = dist;
        objects_.push_back(o);
    };
    // the market square: open ground with stalls and a well
    occMark(cx, cy, marketR * 2, marketR * 2, 0);
    add(CityObj::Well, cx, cy, 6, 6, 0, (uint8_t)District::Market);
    for (int k = 0; k < (int)(marketR * 0.5); k++) {
        double a = rnd(s) * 2 * kPi, r = marketR * (0.35 + 0.55 * std::sqrt(rnd(s)));
        add(CityObj::Stall, cx + std::cos(a) * r, cy + std::sin(a) * r, 4 + rnd(s) * 2, 3 + rnd(s) * 1.5, a, (uint8_t)District::Market);
    }
    // the castle on the high ground and the great temple
    {
        double ks = clampd(cityR_ * 0.07, 24, 90);
        if (occFree(hiX, hiY, ks, ks, 0) || true) { add(CityObj::Keep, hiX, hiY, ks, ks, rnd(s) * 0.4, (uint8_t)District::Noble); occMark(hiX, hiY, ks * 1.3, ks * 1.3, 0); }
        double ts = clampd(cityR_ * 0.04, 18, 60);
        if (!water(tx, ty)) { add(CityObj::Temple, tx, ty, ts * 1.6, ts, tAng, (uint8_t)District::Temple); occMark(tx, ty, ts * 1.8, ts * 1.2, tAng); }
    }
    // houses packed along the streets: unbroken rows in the old town, looser further out
    for (int pass = 0; pass < 2; pass++)
        for (const Road& rd : roads_) {
            if (pass == 0 && rd.kind == 2) continue;   // main roads and streets first, then alleys
            if (pass == 1 && rd.kind != 2) continue;
            double half = rd.kind == 0 ? 6 : (rd.kind == 1 ? 4 : rd.kind == 2 ? 2.5 : 5);
            for (int side = -1; side <= 1; side += 2) {
                double carry = rnd(s) * 6;
                for (size_t k = 1; k < rd.pts.size(); k++) {
                    double x0 = rd.pts[k - 1].x, y0 = rd.pts[k - 1].y, x1 = rd.pts[k].x, y1 = rd.pts[k].y;
                    double len = std::hypot(x1 - x0, y1 - y0);
                    if (len < 1) continue;
                    double ux = (x1 - x0) / len, uy = (y1 - y0) / len, ang = std::atan2(uy, ux);
                    double t = carry;
                    while (t < len) {
                        double bx = x0 + ux * t, by = y0 + uy * t;
                        double r = std::hypot(bx - cx, by - cy);
                        District d = (District)districtAt(bx, by);
                        bool core = (wallR_ > 0 && r < wallR_) || r < cityR_ * 0.5;
                        double w = 6 + rnd(s) * 8, dd = 8 + rnd(s) * 8, gap, prob;
                        if (core) { gap = 0.2 + rnd(s) * 0.8; prob = 0.96; }
                        else if (r < cityR_) { gap = 1 + rnd(s) * 3; prob = 0.85; }
                        else if (r < cityR_ * 1.3) { gap = 3 + rnd(s) * 9; prob = 0.5; }
                        else { gap = 25 + rnd(s) * 70; prob = d == District::Farms ? 0.22 : 0.08; }
                        if (d == District::Noble || d == District::Temple) { w *= 1.6; dd *= 1.5; }
                        if (d == District::Market) { w *= 1.3; dd *= 1.2; }
                        if (d == District::Slums) { w *= 0.65; dd *= 0.7; gap *= 0.5; }
                        if (d == District::Industry || d == District::Docks) { w *= 1.9; dd *= 1.6; }
                        if (d != District::None && d != District::Park && rnd(s) < prob) {
                            double off = half + dd / 2 + oc_ * 0.6;
                            double px = bx + ux * w / 2 - uy * off * side, py = by + uy * w / 2 + ux * off * side;
                            if (occFree(px, py, w, dd, ang)) {
                                occMark(px, py, w, dd, ang);
                                bool big = d == District::Industry || d == District::Docks || (d == District::Noble && rnd(s) < 0.4);
                                add(big ? CityObj::Hall : CityObj::House, px, py, w, dd, ang, (uint8_t)d);
                                // more rows behind, filling the block (fewer further out)
                                int rows = core ? 5 : (r < cityR_ ? 2 : 0);
                                double offR = off + dd / 2;
                                for (int row = 0; row < rows; row++) {
                                    if (rnd(s) < (core ? 0.12 : 0.35)) break;   // a yard or garden
                                    double d2 = 6 + rnd(s) * 7, gap2 = core ? 0.5 + rnd(s) * 1.5 : 2 + rnd(s) * 4;
                                    double off2 = offR + gap2 + d2 / 2;
                                    double w2 = w * (0.8 + rnd(s) * 0.3);
                                    double qx = bx + ux * w / 2 - uy * off2 * side, qy = by + uy * w / 2 + ux * off2 * side;
                                    if (!occFree(qx, qy, w2, d2, ang)) break;
                                    occMark(qx, qy, w2, d2, ang);
                                    add(CityObj::House, qx, qy, w2, d2, ang, (uint8_t)d);
                                    offR = off2 + d2 / 2;
                                }
                                if (objects_.size() > 40000) break;
                            }
                        }
                        t += w + gap;
                    }
                    carry = t - len;
                }
            }
        }
    // watchtowers on the walls
    for (size_t k = 0; k < wall_.size(); k += 4)
        if (wallOnLand_[k]) add(CityObj::Tower, wall_[k].x, wall_[k].y, 12, 12, 0, (uint8_t)District::OldTown);
    // strip fields along the roads out of town
    for (const Road& rd : roads_) {
        if (rd.kind > 1) continue;
        double half = rd.kind == 0 ? 6 : 4;
        for (size_t k = 1; k < rd.pts.size(); k++) {
            double x0 = rd.pts[k - 1].x, y0 = rd.pts[k - 1].y, x1 = rd.pts[k].x, y1 = rd.pts[k].y;
            double len = std::hypot(x1 - x0, y1 - y0);
            if (len < 1) continue;
            double ux = (x1 - x0) / len, uy = (y1 - y0) / len, ang = std::atan2(uy, ux);
            for (double t = 0; t < len; t += 45 + rnd(s) * 40) {
                double bx = x0 + ux * t, by = y0 + uy * t, r = std::hypot(bx - cx, by - cy);
                if (r < cityR_ * 1.05 || r > cityR_ * 2.3) continue;
                for (int side = -1; side <= 1; side += 2) {
                    double fw = 30 + rnd(s) * 45, fd = 110 + rnd(s) * 180, off = half + 6 + fd / 2;
                    double px = bx - uy * off * side, py = by + ux * off * side;
                    if (!occFree(px, py, fw, fd, ang)) continue;
                    occMark(px, py, fw, fd, ang);
                    add(CityObj::Field, px, py, fw, fd, ang, (uint8_t)District::Farms);
                    if (rnd(s) < 0.04) add(CityObj::Windmill, px, py + 0, 10, 10, rnd(s) * kPi, (uint8_t)District::Farms);
                }
            }
        }
    }
    // trees: parks, river banks, copses in the countryside, gardens in the rich quarter
    {
        int trees = 0;
        int samples = (int)clampd(sizeM_ * sizeM_ / 900.0, 6000, 60000);
        for (int k = 0; k < samples && trees < 9000; k++) {
            double x = (rnd(s) - 0.5) * sizeM_, y = (rnd(s) - 0.5) * sizeM_;
            if (water(x, y)) continue;
            double r = std::hypot(x - cx, y - cy);
            District d = (District)districtAt(x, y);
            bool bank = false;
            for (int q = 0; q < 4 && !bank; q++) if (water(x + std::cos(q * kPi / 2) * 20, y + std::sin(q * kPi / 2) * 20)) bank = true;
            double wood = 0.5 + 0.5 * std::sin(x * 0.004 + plan.seed % 13) * std::sin(y * 0.0037 + plan.seed % 7);
            double p = d == District::Park ? 0.5 : d == District::Noble ? 0.15 : bank ? 0.35 : (r > cityR_ * 1.2 ? (wood > 0.7 ? 0.6 : 0.02) : 0.0);
            if (rnd(s) > p) continue;
            double ts = 5 + rnd(s) * 7;
            if (!occFree(x, y, ts * 0.7, ts * 0.7, 0)) continue;
            occMark(x, y, ts * 0.6, ts * 0.6, 0);
            add(CityObj::Tree, x, y, ts, ts, 0, (uint8_t)d);
            trees++;
        }
    }
    // ships in the harbour
    {
        int ships = 0;
        for (int k = 0; k < 4000 && ships < (int)clampd(cityR_ / 150, 2, 14); k++) {
            double a = rnd(s) * 2 * kPi, r = cityR_ * (0.2 + rnd(s) * 0.9);
            double x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
            if (!water(x, y) || !sea(x, y)) continue;
            bool nearShore = false;
            for (int q = 0; q < 8 && !nearShore; q++) if (!water(x + std::cos(q * kPi / 4) * 60, y + std::sin(q * kPi / 4) * 60)) nearShore = true;
            if (!nearShore) continue;
            add(CityObj::Ship, x, y, 22 + rnd(s) * 14, 7, rnd(s) * 2 * kPi, (uint8_t)District::Docks);
            ships++;
        }
    }
}

void CityEditor::bake(CityPlan& plan) {
    if (plan.baked) return;
    plan.baked = true;
    plan.objects = objects_;
}
void CityEditor::pushUndo() {
    undo_.push_back(objects_);
    if (undo_.size() > 30) undo_.erase(undo_.begin());
}
int CityEditor::pickObject(Vector2 local) const {
    int best = -1; double bd = 1e18;
    for (int i = (int)objects_.size() - 1; i >= 0; i--) {
        const CityObject& o = objects_[i];
        double dx = local.x - o.x, dy = local.y - o.y;
        double c = std::cos(-o.ang), sn = std::sin(-o.ang);
        double u = dx * c - dy * sn, v = dx * sn + dy * c;
        double hw = std::max(2.0f, o.w / 2) + 1, hd = std::max(2.0f, o.d / 2) + 1;
        if (std::fabs(u) <= hw && std::fabs(v) <= hd) {
            double area = o.w * o.d;
            if (o.type == CityObj::Field) area *= 50;   // prefer what sits on top of fields
            if (area < bd) { bd = area; best = i; }
        }
    }
    return best;
}

namespace {
const Color kMaterial[] = {{0, 0, 0, 0}, {112, 150, 78, 235}, {196, 176, 104, 235}, {58, 98, 54, 235}, {222, 206, 152, 235},
                           {70, 130, 190, 240}, {168, 163, 150, 240}, {142, 112, 80, 235}, {240, 240, 246, 240}};
const char* kMaterialNames = "Remove paint;Grass;Farmland;Forest;Sand;Water;Paving;Dirt;Snow";
}

void CityEditor::rebuildPaint(const CityPlan& plan) {
    if (plan.paint.size() != (size_t)G * G) { if (havePaint_) UnloadTexture(paintTex_); havePaint_ = false; return; }
    std::vector<unsigned char> img((size_t)G * G * 4, 0);
    for (int i = 0; i < G * G; i++) {
        Color c = kMaterial[std::min<int>(plan.paint[i], 8)];
        img[i * 4] = c.r; img[i * 4 + 1] = c.g; img[i * 4 + 2] = c.b; img[i * 4 + 3] = c.a;
    }
    if (!havePaint_) {
        Image im{img.data(), G, G, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        paintTex_ = LoadTextureFromImage(im);
        SetTextureFilter(paintTex_, TEXTURE_FILTER_BILINEAR);
        havePaint_ = true;
    } else UpdateTexture(paintTex_, img.data());
}

void CityEditor::drawObject(const CityObject& o, bool selected) const {
    Vector2 p = toScreen(o.x, o.y);
    float z = (float)zoom_;
    float w = std::max(1.0f, o.w * z), d = std::max(1.0f, o.d * z);
    float deg = -o.ang * 180.f / (float)kPi;
    float c = std::cos(-o.ang), sn = std::sin(-o.ang);
    auto rot = [&](float u, float v) { return Vector2{p.x + c * u - sn * v, p.y + sn * u + c * v}; };
    int v = (int)o.shade % 36 - 18;
    auto tint = [&](Color b) { return Color{(unsigned char)clampd(b.r + v, 0, 255), (unsigned char)clampd(b.g + v, 0, 255), (unsigned char)clampd(b.b + v, 0, 255), b.a}; };
    float sh = std::max(1.f, 1.6f * z);   // shadow offset
    switch (o.type) {
        case CityObj::House: case CityObj::Hall: {
            Color roof = tint(kRoof[std::min<int>(o.district, 10)]);
            if (w < 2.5f && d < 2.5f) { DrawRectangleV({p.x - 1, p.y - 1}, {2, 2}, roof); break; }
            DrawRectanglePro(Rectangle{p.x + sh, p.y + sh, w, d}, Vector2{w / 2, d / 2}, deg, Color{20, 20, 25, 70});
            DrawRectanglePro(Rectangle{p.x, p.y, w, d}, Vector2{w / 2, d / 2}, deg, roof);
            // ridge along the long side, and a darker half for the far roof slope
            Color dark{(unsigned char)(roof.r * 0.78), (unsigned char)(roof.g * 0.78), (unsigned char)(roof.b * 0.78), 255};
            if (w >= d) {
                DrawRectanglePro(Rectangle{rot(0, d / 4).x, rot(0, d / 4).y, w, d / 2}, Vector2{w / 2, d / 4}, deg, dark);
                DrawLineEx(rot(-w / 2, 0), rot(w / 2, 0), std::max(1.f, z * 0.6f), Color{60, 45, 40, 200});
            } else {
                DrawRectanglePro(Rectangle{rot(w / 4, 0).x, rot(w / 4, 0).y, w / 2, d}, Vector2{w / 4, d / 2}, deg, dark);
                DrawLineEx(rot(0, -d / 2), rot(0, d / 2), std::max(1.f, z * 0.6f), Color{60, 45, 40, 200});
            }
            if (w > 6) {
                Vector2 q[4] = {rot(-w / 2, -d / 2), rot(w / 2, -d / 2), rot(w / 2, d / 2), rot(-w / 2, d / 2)};
                for (int k = 0; k < 4; k++) DrawLineEx(q[k], q[(k + 1) % 4], 1, Color{50, 40, 35, 180});
            }
            break;
        }
        case CityObj::Tower:
            DrawCircleV({p.x + sh, p.y + sh}, w / 2, Color{20, 20, 25, 80});
            DrawCircleV(p, w / 2, Color{150, 145, 138, 255});
            DrawCircleV(p, w / 3, Color{120, 115, 110, 255});
            DrawCircleLinesV(p, w / 2, Color{60, 55, 52, 255});
            break;
        case CityObj::Temple: {
            Color st{214, 206, 186, 255};
            DrawRectanglePro(Rectangle{p.x + sh, p.y + sh, w, d * 0.45f}, Vector2{w / 2, d * 0.225f}, deg, Color{20, 20, 25, 80});
            DrawRectanglePro(Rectangle{p.x, p.y, w, d * 0.45f}, Vector2{w / 2, d * 0.225f}, deg, st);
            Vector2 tr = rot(w * 0.18f, 0);
            DrawRectanglePro(Rectangle{tr.x, tr.y, w * 0.22f, d}, Vector2{w * 0.11f, d / 2}, deg, st);
            Vector2 ap = rot(w / 2, 0);
            DrawCircleV(ap, d * 0.225f, st);
            DrawLineEx(rot(-w / 2, 0), rot(w / 2, 0), std::max(1.f, z), Color{150, 140, 120, 255});
            break;
        }
        case CityObj::Keep: {
            DrawRectanglePro(Rectangle{p.x + sh, p.y + sh, w, d}, Vector2{w / 2, d / 2}, deg, Color{20, 20, 25, 90});
            DrawRectanglePro(Rectangle{p.x, p.y, w, d}, Vector2{w / 2, d / 2}, deg, Color{140, 134, 126, 255});
            DrawRectanglePro(Rectangle{p.x, p.y, w * 0.62f, d * 0.62f}, Vector2{w * 0.31f, d * 0.31f}, deg, Color{180, 172, 150, 255});
            DrawRectanglePro(Rectangle{rot(-w * 0.12f, -d * 0.12f).x, rot(-w * 0.12f, -d * 0.12f).y, w * 0.3f, d * 0.3f}, Vector2{w * 0.15f, d * 0.15f}, deg, Color{120, 110, 105, 255});
            for (float a : {-0.5f, 0.5f}) for (float b : {-0.5f, 0.5f}) { DrawCircleV(rot(w * a, d * b), w * 0.12f, Color{130, 124, 118, 255}); DrawCircleLinesV(rot(w * a, d * b), w * 0.12f, Color{60, 55, 52, 255}); }
            break;
        }
        case CityObj::Tree: {
            float r = w / 2;
            DrawCircleV({p.x + sh * 1.2f, p.y + sh * 1.2f}, r, Color{20, 30, 20, 80});
            Color g = tint(Color{68, 112, 56, 255});
            DrawCircleV(p, r, g);
            DrawCircleV({p.x - r * 0.25f, p.y - r * 0.25f}, r * 0.55f, Color{(unsigned char)std::min(255, g.r + 22), (unsigned char)std::min(255, g.g + 28), (unsigned char)std::min(255, g.b + 14), 255});
            break;
        }
        case CityObj::Orchard:
            for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) {
                Vector2 q = rot(a * w / 3, b * d / 3);
                DrawCircleV(q, std::max(1.f, w / 9), Color{90, 130, 60, 255});
            }
            break;
        case CityObj::Well:
            DrawCircleV(p, w / 2, Color{150, 145, 138, 255});
            DrawCircleV(p, w / 3, Color{60, 110, 170, 255});
            break;
        case CityObj::Statue:
            DrawCircleV({p.x + sh, p.y + sh}, w / 2, Color{20, 20, 25, 80});
            DrawPoly(p, 4, w / 2, deg + 45, Color{170, 168, 160, 255});
            DrawCircleV(p, w / 5, Color{110, 108, 104, 255});
            break;
        case CityObj::Stall: {
            Color aw = (o.shade & 1) ? Color{200, 70, 60, 255} : Color{70, 110, 180, 255};
            DrawRectanglePro(Rectangle{p.x, p.y, w, d}, Vector2{w / 2, d / 2}, deg, aw);
            DrawRectanglePro(Rectangle{p.x, p.y, w, d / 3}, Vector2{w / 2, d / 6}, deg, Color{240, 235, 220, 255});
            break;
        }
        case CityObj::Windmill: {
            DrawCircleV(p, w * 0.35f, Color{170, 150, 120, 255});
            DrawCircleLinesV(p, w * 0.35f, Color{80, 60, 45, 255});
            for (int k = 0; k < 4; k++) {
                float a = o.ang + k * (float)kPi / 2;
                DrawLineEx(p, {p.x + std::cos(a) * w, p.y + std::sin(a) * w}, std::max(1.f, z * 1.5f), Color{90, 70, 50, 255});
            }
            break;
        }
        case CityObj::Ship: {
            Vector2 bow = rot(w / 2, 0), s1 = rot(-w / 2, -d / 2), s2 = rot(-w / 2, d / 2), m1 = rot(w * 0.2f, -d / 2), m2 = rot(w * 0.2f, d / 2);
            DrawTriangle(bow, m1, m2, Color{110, 75, 45, 255}); DrawTriangle(bow, m2, m1, Color{110, 75, 45, 255});
            DrawTriangle(m1, s1, s2, Color{110, 75, 45, 255}); DrawTriangle(m1, s2, m2, Color{110, 75, 45, 255});
            DrawTriangle(m2, s2, s1, Color{110, 75, 45, 255}); DrawTriangle(m2, s1, m1, Color{110, 75, 45, 255});
            DrawLineEx(rot(0, -d * 0.9f), rot(0, d * 0.9f), std::max(1.5f, z * 2.2f), Color{240, 235, 220, 255});
            break;
        }
        case CityObj::Tent:
            DrawPoly(p, 6, w / 2, deg, Color{200, 180, 140, 255});
            DrawPolyLinesEx(p, 6, w / 2, deg, 1, Color{110, 90, 60, 255});
            DrawCircleV(p, std::max(1.f, w / 8), Color{110, 90, 60, 255});
            break;
        case CityObj::Rock:
            DrawPoly({p.x + sh, p.y + sh}, 6, w / 2, deg, Color{20, 20, 25, 80});
            DrawPoly(p, 6, w / 2, deg, tint(Color{140, 138, 132, 255}));
            break;
        case CityObj::Field: {
            static const Color crops[] = {{205, 185, 100, 255}, {150, 170, 80, 255}, {175, 150, 95, 255}, {120, 150, 70, 255}, {210, 200, 130, 255}};
            Color cc = crops[o.shade % 5];
            DrawRectanglePro(Rectangle{p.x, p.y, w, d}, Vector2{w / 2, d / 2}, deg, cc);
            if (z > 0.08f)
                for (int k = 1; k < 6; k++) {
                    float u = -w / 2 + w * k / 6;
                    DrawLineEx(rot(u, -d / 2), rot(u, d / 2), 1, Color{(unsigned char)(cc.r * 0.85), (unsigned char)(cc.g * 0.85), (unsigned char)(cc.b * 0.85), 200});
                }
            break;
        }
        default: break;
    }
    if (selected) {
        float rr = std::max(w, d) * 0.6f + 6;
        DrawCircleLinesV(p, rr, Color{246, 202, 118, 255});
        DrawCircleLinesV(p, rr + 1, Color{246, 202, 118, 255});
    }
}

void CityEditor::makeLandmarks(Document& doc) {
    CityPlan& plan = *doc.planFor(city_);
    if (plan.landmarksMade) return;
    plan.landmarksMade = true;
    const City& c = doc.cities[city_];
    uint32_t s = plan.seed ^ 0x9e3779b9u;
    auto add = [&](LandmarkType t, double x, double y, const std::string& n) { Landmark l; l.type = t; l.x = x; l.y = y; l.name = n; plan.landmarks.push_back(l); };
    // centre (the generated market district)
    double mx = 0, my = 0; int mn = 0;
    for (int i = 0; i < DG * DG; i++) if (district_[i] == (uint8_t)District::Market) { mx += (i % DG + 0.5) / DG * sizeM_ - sizeM_ / 2; my += sizeM_ / 2 - (i / DG + 0.5) / DG * sizeM_; mn++; }
    if (mn) { mx /= mn; my /= mn; }
    add(LandmarkType::Market, mx, my, (rnd(s) < 0.5 ? "Old Market of " : "Great Square of ") + c.name);
    double nx = 0, ny = 0; int nn = 0;
    for (int i = 0; i < DG * DG; i++) if (district_[i] == (uint8_t)District::Noble) { nx += (i % DG + 0.5) / DG * sizeM_ - sizeM_ / 2; ny += sizeM_ / 2 - (i / DG + 0.5) / DG * sizeM_; nn++; }
    if (nn) add(c.capital ? LandmarkType::Palace : LandmarkType::Castle, nx / nn, ny / nn, c.capital ? "Royal Palace" : c.name + " Castle");
    double tx = 0, ty = 0; int tn = 0;
    for (int i = 0; i < DG * DG; i++) if (district_[i] == (uint8_t)District::Temple) { tx += (i % DG + 0.5) / DG * sizeM_ - sizeM_ / 2; ty += sizeM_ / 2 - (i / DG + 0.5) / DG * sizeM_; tn++; }
    std::string faith = "the Gods";
    int cell = World::nearestCell(c.lat, c.lon);
    if (!doc.faith.empty() && doc.faith[cell] > 0 && doc.faith[cell] - 1 < (int)doc.religions.size()) faith = doc.religions[doc.faith[cell] - 1].name;
    if (tn) add(LandmarkType::Temple, tx / tn, ty / tn, "Great Temple (" + faith + ")");
    double hx = 0, hy = 0; int hn = 0;
    for (int i = 0; i < DG * DG; i++) if (district_[i] == (uint8_t)District::Docks) { hx += (i % DG + 0.5) / DG * sizeM_ - sizeM_ / 2; hy += sizeM_ / 2 - (i / DG + 0.5) / DG * sizeM_; hn++; }
    if (hn) add(LandmarkType::Harbour, hx / hn, hy / hn, c.name + " Harbour");
    // gates where main roads cross the wall, named by compass direction
    if (!wall_.empty()) {
        static const char* dirs[] = {"East", "North-East", "North", "North-West", "West", "South-West", "South", "South-East"};
        std::vector<std::string> used;
        for (const Road& rd : roads_) {
            if (rd.kind != 0) continue;
            for (size_t k = 1; k < rd.pts.size(); k++) {
                double r0 = std::hypot(rd.pts[k - 1].x - cx_, rd.pts[k - 1].y - cy_), r1 = std::hypot(rd.pts[k].x - cx_, rd.pts[k].y - cy_);
                if ((r0 - wallR_) * (r1 - wallR_) <= 0) {
                    double a = std::atan2(rd.pts[k].y - cy_, rd.pts[k].x - cx_);
                    int q = ((int)std::lround(a / (kPi / 4)) % 8 + 8) % 8;
                    std::string name = std::string(dirs[q]) + " Gate";
                    auto taken = [&](const std::string& n) { return std::find(used.begin(), used.end(), n) != used.end(); };
                    if (taken(name)) name = std::string("Old ") + dirs[q] + " Gate";
                    if (taken(name)) name = "Kings Gate";
                    if (taken(name)) break;
                    used.push_back(name);
                    add(LandmarkType::Gate, rd.pts[k].x, rd.pts[k].y, name);
                    break;
                }
            }
        }
    }
    if (c.population > 20000) add(LandmarkType::Guildhall, mx + cityR_ * 0.06, my - cityR_ * 0.05, "Hall of the Merchants");
    if (c.population > 80000) add(LandmarkType::University, mx - cityR_ * 0.2, my + cityR_ * 0.15, "University of " + c.name);
    add(LandmarkType::Inn, mx + cityR_ * 0.12, my + cityR_ * 0.1, rnd(s) < 0.5 ? "The Prancing Stag" : "The Drowned Lantern");
}

bool CityEditor::frame(const Planet& planet, const World& world, Document& doc) {
    (void)planet; (void)world;
    if (!active || city_ < 0 || city_ >= (int)doc.cities.size()) { close(); return false; }
    CityPlan& plan = *doc.planFor(city_);
    City& city = doc.cities[city_];
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    const Rectangle panel{0, 0, 370, (float)sh};
    Vector2 mouse = GetMousePosition();
    bool overPanel = CheckCollisionPointRec(mouse, panel);
    Vector2 local = toLocal(mouse);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool typing = nameEdit_ || labelEdit_;
    static const float kStampSize[][2] = {{10, 8}, {22, 14}, {12, 12}, {40, 24}, {50, 50}, {9, 9}, {30, 30}, {6, 6}, {5, 5}, {5, 4}, {10, 10}, {28, 8}, {8, 8}, {7, 7}, {50, 160}};

    // ---- input ----
    if (!overPanel) {
        float wh = GetMouseWheelMove();
        if (wh != 0 && shift && (tool_ == 1 || (tool_ == 0 && selObj_ >= 0))) {
            // Shift + wheel turns the stamp or the selected object
            if (tool_ == 1) stampAng_ += wh * 0.2618f;
            else if (selObj_ < (int)objects_.size()) objects_[selObj_].ang += wh * 0.2618f, plan.objects = objects_;
        } else if (wh != 0) {
            Vector2 before = toLocal(mouse);
            zoom_ = clampd(zoom_ * std::pow(1.2, wh), 0.01, 8);
            Vector2 after = toLocal(mouse);
            camX_ += before.x - after.x; camY_ += before.y - after.y;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && tool_ != 4) { Vector2 d = GetMouseDelta(); camX_ -= d.x / zoom_; camY_ += d.y / zoom_; }
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) { Vector2 d = GetMouseDelta(); camX_ -= d.x / zoom_; camY_ += d.y / zoom_; }
        if (tool_ == 0) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                dragObj_ = dragLm_ = dragLabel_ = -1;
                for (int i = 0; i < (int)plan.labels.size() && dragLabel_ < 0; i++) {
                    Vector2 p = toScreen(plan.labels[i].x, plan.labels[i].y);
                    float fs = std::max(10.f, plan.labels[i].size * (float)zoom_);
                    Vector2 ts = measureTextK(FontKind::Serif, plan.labels[i].text.c_str(), fs);
                    if (std::fabs(mouse.x - p.x) < ts.x / 2 + 4 && std::fabs(mouse.y - p.y) < ts.y / 2 + 4) { dragLabel_ = i; selLabel_ = i; selObj_ = -1; }
                }
                for (int i = 0; i < (int)plan.landmarks.size() && dragLabel_ < 0 && dragLm_ < 0; i++) {
                    Vector2 p = toScreen(plan.landmarks[i].x, plan.landmarks[i].y);
                    if (std::hypot(p.x - mouse.x, p.y - mouse.y) < 10) { dragLm_ = i; landmarkSel_ = i; }
                }
                if (dragLabel_ < 0 && dragLm_ < 0) {
                    int o = pickObject(local);
                    selObj_ = o; selLabel_ = -1;
                    if (o >= 0) { bake(plan); pushUndo(); dragObj_ = o; dragOffset_ = {local.x - objects_[o].x, local.y - objects_[o].y}; }
                }
            }
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                if (dragObj_ >= 0 && dragObj_ < (int)objects_.size()) { objects_[dragObj_].x = local.x - dragOffset_.x; objects_[dragObj_].y = local.y - dragOffset_.y; plan.objects = objects_; }
                else if (dragLm_ >= 0 && dragLm_ < (int)plan.landmarks.size()) { plan.landmarks[dragLm_].x = local.x; plan.landmarks[dragLm_].y = local.y; }
                else if (dragLabel_ >= 0 && dragLabel_ < (int)plan.labels.size()) { plan.labels[dragLabel_].x = local.x; plan.labels[dragLabel_].y = local.y; }
                else { Vector2 d = GetMouseDelta(); camX_ -= d.x / zoom_; camY_ += d.y / zoom_; }
            }
        }
        if (tool_ == 1) {   // stamp
            bool scatter = shift && (stampUi_ == (int)CityObj::Tree || stampUi_ == (int)CityObj::Rock || stampUi_ == (int)CityObj::House);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { bake(plan); pushUndo(); }
            static double lastDrop = 0;
            if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !scatter) || (scatter && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && GetTime() - lastDrop > 0.03)) {
                lastDrop = GetTime();
                CityObject o; o.type = (CityObj)stampUi_;
                o.w = kStampSize[stampUi_][0] * stampScale_; o.d = kStampSize[stampUi_][1] * stampScale_;
                o.x = local.x; o.y = local.y; o.ang = stampAng_; o.shade = (uint8_t)GetRandomValue(0, 255);
                o.district = (uint8_t)std::max(1, districtAt(local.x, local.y));
                if (scatter) {
                    double a = GetRandomValue(0, 6283) / 1000.0, r = std::sqrt(GetRandomValue(0, 1000) / 1000.0) * brushM_;
                    o.x = (float)(local.x + std::cos(a) * r); o.y = (float)(local.y + std::sin(a) * r);
                    float k = 0.7f + GetRandomValue(0, 60) / 100.f; o.w *= k; o.d *= k;
                    o.ang = stampAng_ + GetRandomValue(-30, 30) / 100.f;
                }
                objects_.push_back(o); plan.objects = objects_;
            }
        }
        if (tool_ == 2) {   // erase objects under the brush
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { bake(plan); pushUndo(); }
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                size_t before = objects_.size();
                objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&](const CityObject& o) { return std::hypot(o.x - local.x, o.y - local.y) < brushM_; }), objects_.end());
                if (objects_.size() != before) { plan.objects = objects_; selObj_ = -1; }
            }
        }
        if (tool_ == 3 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {   // paint the ground
            if (plan.paint.size() != (size_t)G * G) plan.paint.assign((size_t)G * G, 0);
            int r = (int)std::ceil(brushM_ / sizeM_ * G);
            int pcx = (int)((local.x + sizeM_ / 2) / sizeM_ * G), pcy = (int)((sizeM_ / 2 - local.y) / sizeM_ * G);
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                    int x = pcx + dx, y = pcy + dy;
                    if (x < 0 || y < 0 || x >= G || y >= G || dx * dx + dy * dy > r * r) continue;
                    plan.paint[(size_t)y * G + x] = (uint8_t)materialUi_;
                }
            rebuildPaint(plan);
        }
        if (tool_ == 4) {   // draw a road: left-click points, right-click to finish
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) drawing_.push_back({local.x, local.y});
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (drawing_.size() >= 2) { plan.extraRoads.push_back(drawing_); Road rd; rd.kind = 3; for (auto& p : drawing_) rd.pts.push_back({p.first, p.second}); roads_.push_back(rd); }
                drawing_.clear();
            }
        }
        if (tool_ == 5 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {   // write a label
            CityLabel l; l.x = local.x; l.y = local.y; l.size = labelSize_; l.text = labelBuf_;
            plan.labels.push_back(l); selLabel_ = (int)plan.labels.size() - 1;
        }
        if (tool_ == 6 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {   // add a landmark
            Landmark l; l.type = (LandmarkType)landmarkUi_; l.x = local.x; l.y = local.y;
            l.name = std::string("New ") + landmarkName(l.type);
            plan.landmarks.push_back(l);
            landmarkSel_ = (int)plan.landmarks.size() - 1;
            tool_ = 0;
        }
        if (tool_ == 7 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {   // paint districts
            int r = (int)std::ceil(brushM_ / sizeM_ * DG);
            int pcx = (int)((local.x + sizeM_ / 2) / sizeM_ * DG), pcy = (int)((sizeM_ / 2 - local.y) / sizeM_ * DG);
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                    int x = pcx + dx, y = pcy + dy;
                    if (x < 0 || y < 0 || x >= DG || y >= DG || dx * dx + dy * dy > r * r) continue;
                    double wx = (x + 0.5) / DG * sizeM_ - sizeM_ / 2, wy = sizeM_ / 2 - (y + 0.5) / DG * sizeM_;
                    if (water(wx, wy)) continue;
                    int idx = y * DG + x;
                    if (district_[idx] != districtUi_) {
                        district_[idx] = (uint8_t)districtUi_;
                        bool found = false;
                        for (auto& e : plan.districtEdits) if (e.first == idx) { e.second = (uint8_t)districtUi_; found = true; }
                        if (!found) plan.districtEdits.push_back({idx, (uint8_t)districtUi_});
                    }
                }
            for (auto& b : objects_) if (b.type == CityObj::House || b.type == CityObj::Hall) b.district = (uint8_t)districtAt(b.x, b.y);
            if (plan.baked) plan.objects = objects_;
        }
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { dragLm_ = -1; dragObj_ = -1; dragLabel_ = -1; }
    // keys for the selection
    if (!typing) {
        if (tool_ == 0 && selObj_ >= 0 && selObj_ < (int)objects_.size()) {
            CityObject& o = objects_[selObj_];
            bool ch = false;
            if (IsKeyPressed(KEY_Q)) { o.ang += 0.2618f; ch = true; }
            if (IsKeyPressed(KEY_E)) { o.ang -= 0.2618f; ch = true; }
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) { o.w *= 1.12f; o.d *= 1.12f; ch = true; }
            if (IsKeyPressed(KEY_LEFT_BRACKET)) { o.w /= 1.12f; o.d /= 1.12f; ch = true; }
            if (ctrl && IsKeyPressed(KEY_D)) { pushUndo(); CityObject c2 = o; c2.x += o.w; c2.y -= o.d * 0.3f; objects_.push_back(c2); selObj_ = (int)objects_.size() - 1; ch = true; }
            if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) { pushUndo(); objects_.erase(objects_.begin() + selObj_); selObj_ = -1; ch = true; }
            if (ch) plan.objects = objects_;
        } else if (tool_ == 0 && selLabel_ >= 0 && selLabel_ < (int)plan.labels.size() && (IsKeyPressed(KEY_DELETE))) {
            plan.labels.erase(plan.labels.begin() + selLabel_); selLabel_ = -1;
        }
        if (tool_ == 1 || tool_ == 2 || tool_ == 3 || tool_ == 7) {
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) { if (tool_ == 1 && !shift) stampScale_ = std::min(6.f, stampScale_ * 1.15f); else brushM_ = std::min(2000.f, brushM_ * 1.2f); }
            if (IsKeyPressed(KEY_LEFT_BRACKET)) { if (tool_ == 1 && !shift) stampScale_ = std::max(0.2f, stampScale_ / 1.15f); else brushM_ = std::max(5.f, brushM_ / 1.2f); }
        }
        if (ctrl && IsKeyPressed(KEY_Z) && !undo_.empty()) { objects_ = undo_.back(); undo_.pop_back(); plan.objects = objects_; selObj_ = -1; }
        for (int k = 0; k < 8; k++) if (IsKeyPressed(KEY_ONE + k) && !ctrl) tool_ = k;
    }

    // ---- draw ----
    BeginDrawing();
    ClearBackground(Color{30, 40, 55, 255});
    Vector2 a = toScreen(-sizeM_ / 2, sizeM_ / 2);
    float side = (float)(sizeM_ * zoom_);
    if (haveTex_) DrawTexturePro(terrain_, Rectangle{0, 0, (float)G, (float)G}, Rectangle{a.x, a.y, side, side}, Vector2{0, 0}, 0, WHITE);
    if (havePaint_) DrawTexturePro(paintTex_, Rectangle{0, 0, (float)G, (float)G}, Rectangle{a.x, a.y, side, side}, Vector2{0, 0}, 0, WHITE);
    if (showDistricts_) {
        float cs = side / DG;
        for (int dy = 0; dy < DG; dy++)
            for (int dx = 0; dx < DG; dx++) {
                int d = district_[dy * DG + dx];
                if (!d) continue;
                Color c = kDistrictCol[d]; c.a = 60;
                DrawRectangle((int)(a.x + dx * cs), (int)(a.y + dy * cs), (int)cs + 1, (int)cs + 1, c);
            }
    }
    auto onScreen = [&](const CityObject& o) {
        Vector2 p = toScreen(o.x, o.y);
        float r = std::max(o.w, o.d) * (float)zoom_;
        return !(p.x < panel.width - r || p.x > sw + r || p.y < -r || p.y > sh + r);
    };
    // trodden ground and gardens under the town
    {
        Vector2 c0 = toScreen(cx_, cy_);
        float rr = (float)(cityR_ * zoom_);
        DrawCircleGradient((int)c0.x, (int)c0.y, rr * 1.05f, Color{150, 140, 105, 150}, Color{150, 140, 105, 0});
        if (wallR_ > 0) DrawCircleGradient((int)c0.x, (int)c0.y, (float)(wallR_ * zoom_) * 1.02f, Color{170, 155, 120, 150}, Color{170, 155, 120, 60});
    }
    // fields first, under the roads
    for (const CityObject& o : objects_) if (o.type == CityObj::Field && onScreen(o)) drawObject(o, false);
    // roads: outline then fill
    for (int pass = 0; pass < 2; pass++)
        for (const Road& rd : roads_) {
            float wm = rd.kind == 0 ? 10.f : (rd.kind == 1 ? 6.f : (rd.kind == 2 ? 3.5f : 7.f));
            if (rd.kind == 2 && zoom_ < 0.12) continue;
            float minW = rd.kind == 0 ? 2.f : 1.f;
            float wpx = std::max(pass == 0 ? minW + 1 : minW, wm * (float)zoom_ + (pass == 0 ? 2 : 0));
            if (pass == 0 && wm * zoom_ < 2) continue;
            Color c = pass == 0 ? Color{120, 100, 80, 255} : (rd.kind == 3 ? Color{240, 222, 170, 255} : Color{218, 204, 172, 255});
            for (size_t k = 1; k < rd.pts.size(); k++) {
                Vector2 p0 = toScreen(rd.pts[k - 1].x, rd.pts[k - 1].y), p1 = toScreen(rd.pts[k].x, rd.pts[k].y);
                bool bridge = water(rd.pts[k].x, rd.pts[k].y);
                DrawLineEx(p0, p1, wpx, bridge && pass == 1 ? Color{170, 150, 120, 255} : c);
                if (pass == 1 && wpx > 3) DrawCircleV(p1, wpx / 2, bridge ? Color{170, 150, 120, 255} : c);
            }
        }
    // market square paving
    if (!plan.baked || true) {
        double mr = clampd(cityR_ * 0.07, 22, 130);
        Vector2 mc = toScreen(cx_, cy_);
        DrawCircleV(mc, (float)(mr * zoom_), Color{210, 200, 175, 200});
    }
    // walls
    if (!wall_.empty()) {
        float wpx = std::max(2.5f, 6.f * (float)zoom_);
        for (size_t k = 1; k < wall_.size(); k++) {
            if (!wallOnLand_[k] || !wallOnLand_[k - 1]) continue;
            DrawLineEx(toScreen(wall_[k - 1].x, wall_[k - 1].y), toScreen(wall_[k].x, wall_[k].y), wpx + 2, Color{40, 36, 34, 255});
            DrawLineEx(toScreen(wall_[k - 1].x, wall_[k - 1].y), toScreen(wall_[k].x, wall_[k].y), wpx, Color{120, 112, 104, 255});
        }
    }
    // buildings and everything else
    if (zoom_ > 0.02) {
        for (int i = 0; i < (int)objects_.size(); i++) {
            const CityObject& o = objects_[i];
            if (o.type == CityObj::Field || o.type == CityObj::Tree || !onScreen(o)) continue;
            drawObject(o, i == selObj_);
        }
        for (int i = 0; i < (int)objects_.size(); i++) {   // trees on top
            const CityObject& o = objects_[i];
            if (o.type == CityObj::Tree && onScreen(o)) drawObject(o, i == selObj_);
        }
        if (selObj_ >= 0 && selObj_ < (int)objects_.size() && objects_[selObj_].type == CityObj::Field) drawObject(objects_[selObj_], true);
    }
    // user road being drawn
    for (size_t k = 1; k < drawing_.size(); k++) DrawLineEx(toScreen(drawing_[k - 1].first, drawing_[k - 1].second), toScreen(drawing_[k].first, drawing_[k].second), 3, YELLOW);
    if (tool_ == 4 && !drawing_.empty()) DrawLineEx(toScreen(drawing_.back().first, drawing_.back().second), mouse, 2, Fade(YELLOW, 0.6f));
    // labels written on the map
    for (int i = 0; i < (int)plan.labels.size(); i++) {
        const CityLabel& l = plan.labels[i];
        Vector2 p = toScreen(l.x, l.y);
        float fs = std::max(10.f, l.size * (float)zoom_);
        Font f = fontFor(FontKind::Serif, fs);
        Vector2 ts = MeasureTextEx(f, l.text.c_str(), fs, fs * 0.08f);
        DrawTextEx(f, l.text.c_str(), {p.x - ts.x / 2 + 1.5f, p.y - ts.y / 2 + 1.5f}, fs, fs * 0.08f, Color{20, 15, 10, 160});
        DrawTextEx(f, l.text.c_str(), {p.x - ts.x / 2, p.y - ts.y / 2}, fs, fs * 0.08f, i == selLabel_ ? Color{255, 220, 140, 255} : Color{250, 244, 226, 255});
    }
    // landmarks
    for (int i = 0; i < (int)plan.landmarks.size(); i++) {
        const Landmark& l = plan.landmarks[i];
        Vector2 p = toScreen(l.x, l.y);
        static const char* sym[] = {"M", "C", "P", "T", "H", "G", "g", "U", "t", "m", "I", "?"};
        DrawCircleV(p, 10, i == landmarkSel_ ? YELLOW : Color{30, 30, 30, 230});
        DrawCircleV(p, 8, Color{250, 245, 230, 255});
        const char* sy = sym[std::min((int)l.type, 11)];
        uiText(sy, (int)p.x - uiMeasure(sy, 14) / 2, (int)p.y - 7, 14, Color{40, 30, 20, 255});
        if (showLabels_) {
            uiText(l.name.c_str(), (int)p.x + 13, (int)p.y - 7, 16, BLACK);
            uiText(l.name.c_str(), (int)p.x + 12, (int)p.y - 8, 16, RAYWHITE);
        }
    }
    // tool previews
    if (!overPanel) {
        if (tool_ == 2 || tool_ == 3 || tool_ == 7 || (tool_ == 1 && shift)) DrawCircleLines((int)mouse.x, (int)mouse.y, (float)(brushM_ * zoom_), YELLOW);
        if (tool_ == 1) {
            CityObject o; o.type = (CityObj)stampUi_; o.w = kStampSize[stampUi_][0] * stampScale_; o.d = kStampSize[stampUi_][1] * stampScale_;
            o.x = local.x; o.y = local.y; o.ang = stampAng_; o.district = (uint8_t)std::max(1, districtAt(local.x, local.y));
            drawObject(o, false);
        }
    }
    // scale bar
    {
        double target = 160 / zoom_, mag = std::pow(10.0, std::floor(std::log10(target))), nice = mag;
        for (double m : {1.0, 2.0, 5.0}) if (m * mag <= target) nice = m * mag;
        int len = (int)(nice * zoom_), x = (int)(panel.x + panel.width) + 24, y = sh - 40;
        DrawRectangle(x, y, len, 4, RAYWHITE);
        uiText(nice >= 1000 ? TextFormat("%.0f km", nice / 1000) : TextFormat("%.0f m", nice), x, y - 24, 18, RAYWHITE);
    }

    // ---- panel ----
    bool keepOpen = true;
    DrawRectangleRec(panel, Color{22, 26, 35, 250});
    DrawLineEx({panel.width - 0.5f, 0}, {panel.width - 0.5f, (float)sh}, 1, Color{40, 46, 60, 255});
    if (overPanel) { panelScroll_ -= GetMouseWheelMove() * 60; }
    panelScroll_ = std::max(0.f, std::min(panelScroll_, std::max(0.f, panelH_ - (sh - 20))));
    Layout L{panel.x + 18, 14 - panelScroll_, panel.width - 40};
    const float startY = L.y;
    if (L.button("< Back to world map", 32)) keepOpen = false;
    drawTextK(FontKind::Serif, city.name.c_str(), {L.x, L.y}, 28, Color{246, 202, 118, 255});
    L.y += 36;
    L.label(TextFormat("Population: %d%s", city.population, city.capital ? " (capital)" : ""));
    L.label(TextFormat("Buildings: %d", (int)std::count_if(objects_.begin(), objects_.end(), [](const CityObject& o) { return o.type == CityObj::House || o.type == CityObj::Hall; })));
    L.heading("TOOLS");
    {
        static const char* names[] = {"Select", "Stamp", "Erase", "Paint", "Road", "Label", "Landmark", "District"};
        float bw = (L.w - 18) / 4;
        for (int k = 0; k < 8; k++) {
            Rectangle r{L.x + (k % 4) * (bw + 6), L.y + (k / 4) * 34, bw, 30};
            if (tool_ == k) GuiSetState(STATE_PRESSED);
            if (GuiButton(r, names[k])) { tool_ = k; drawing_.clear(); }
            GuiSetState(STATE_NORMAL);
        }
        L.y += 74;
    }
    switch (tool_) {
        case 0:
            L.note("Click a building, tree, label or landmark to pick it up and drag it. Q / E turn it, [ and ] resize it, Ctrl+D copies it, Delete removes it. Drag empty ground to move the view.");
            if (selObj_ >= 0 && selObj_ < (int)objects_.size()) {
                CityObject& o = objects_[selObj_];
                L.label(TextFormat("Selected: %s", cityObjName(o.type)));
                float wv = o.w, dv = o.d, av = o.ang * 180 / (float)kPi;
                L.label(TextFormat("Width: %.0f m", wv)); L.slider(&wv, 1, std::max(60.f, wv * 2));
                L.label(TextFormat("Depth: %.0f m", dv)); L.slider(&dv, 1, std::max(60.f, dv * 2));
                L.label(TextFormat("Angle: %.0f\xC2\xB0", av)); L.slider(&av, -180, 180);
                if (wv != o.w || dv != o.d || av != o.ang * 180 / (float)kPi) { o.w = wv; o.d = dv; o.ang = av * (float)kPi / 180; plan.objects = objects_; }
            }
            break;
        case 1: {
            L.note("Click to place. Shift + drag scatters trees, rocks or houses. Shift + scroll turns the stamp, [ and ] resize it.");
            float bw = (L.w - 12) / 3;
            for (int k = 0; k < (int)CityObj::Count; k++) {
                Rectangle r{L.x + (k % 3) * (bw + 6), L.y + (k / 3) * 32, bw, 28};
                if (stampUi_ == k) GuiSetState(STATE_PRESSED);
                if (GuiButton(r, cityObjName((CityObj)k))) stampUi_ = k;
                GuiSetState(STATE_NORMAL);
            }
            L.y += (((int)CityObj::Count + 2) / 3) * 32 + 6;
            L.label(TextFormat("Size: %.0f%%", stampScale_ * 100)); L.slider(&stampScale_, 0.2f, 6);
            float deg = stampAng_ * 180 / (float)kPi;
            L.label(TextFormat("Angle: %.0f\xC2\xB0", deg)); L.slider(&deg, -180, 180); stampAng_ = deg * (float)kPi / 180;
            L.label(TextFormat("Scatter brush: %.0f m", brushM_)); L.slider(&brushM_, 5, 600);
            break;
        }
        case 2:
            L.note("Drag over buildings, trees and fields to remove them.");
            L.label(TextFormat("Brush: %.0f m", brushM_)); L.slider(&brushM_, 5, 800);
            break;
        case 3: {
            L.note("Paint the ground: grass, fields, forest, sand, water, paving...");
            int m = materialUi_;
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 26}, "Remove paint;Grass;Farmland\nForest;Sand;Water\nPaving;Dirt;Snow", &m);
            materialUi_ = m;
            L.y += 3 * 30 + 6;
            L.label(TextFormat("Brush: %.0f m", brushM_)); L.slider(&brushM_, 5, 1500);
            if (!plan.paint.empty() && L.button("Clear all paint")) { plan.paint.clear(); rebuildPaint(plan); }
            break;
        }
        case 4:
            L.note("Left-click to add points, right-click to finish the road.");
            if (!plan.extraRoads.empty() && L.button("Remove my roads")) { plan.extraRoads.clear(); generate(doc); }
            break;
        case 5: {
            L.note("Type the words, then click the map to place them. Drag them with Select; Delete removes the selected label.");
            if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, labelBuf_, 95, labelEdit_)) labelEdit_ = !labelEdit_;
            L.y += 34;
            L.label(TextFormat("Size: %.0f m", labelSize_)); L.slider(&labelSize_, 8, 400);
            if (selLabel_ >= 0 && selLabel_ < (int)plan.labels.size()) {
                if (L.button("Use this text on the selected label")) plan.labels[selLabel_].text = labelBuf_;
                if (L.button("Delete selected label")) { plan.labels.erase(plan.labels.begin() + selLabel_); selLabel_ = -1; }
            }
            break;
        }
        case 6:
            GuiComboBox(Rectangle{L.x, L.y, L.w, 28}, "Market;Castle;Palace;Temple;Harbour;Gate;Guildhall;University;Tower;Monument;Inn;Other", &landmarkUi_);
            L.y += 34;
            L.note("Click the map to place it.");
            break;
        case 7:
            GuiComboBox(Rectangle{L.x, L.y, L.w, 28}, "None;Market;Old town;Noble quarter;Temple district;Residential;Docks;Industry;Farms;Slums;Park", &districtUi_);
            L.y += 34;
            L.label(TextFormat("Brush: %.0f m", brushM_)); L.slider(&brushM_, 30, 1500);
            break;
    }
    if (!undo_.empty() && L.button("Undo (Ctrl+Z)")) { objects_ = undo_.back(); undo_.pop_back(); plan.objects = objects_; selObj_ = -1; }
    L.heading("LANDMARKS");
    std::string items;
    for (auto& l : plan.landmarks) { if (!items.empty()) items += ";"; std::string n = l.name; for (char& ch : n) if (ch == ';') ch = ','; items += n; }
    GuiListView(Rectangle{L.x, L.y, L.w, 130}, items.c_str(), &landmarkScroll_, &landmarkSel_);
    L.y += 136;
    if (landmarkSel_ >= (int)plan.landmarks.size()) landmarkSel_ = -1;
    if (landmarkSel_ != lastLmSel_) { lastLmSel_ = landmarkSel_; nameEdit_ = false; if (landmarkSel_ >= 0) std::strncpy(nameBuf_, plan.landmarks[landmarkSel_].name.c_str(), 95); }
    if (landmarkSel_ >= 0) {
        Landmark& l = plan.landmarks[landmarkSel_];
        if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, nameBuf_, 95, nameEdit_)) nameEdit_ = !nameEdit_;
        l.name = nameBuf_;
        L.y += 34;
        float bw = (L.w - 8) / 2;
        if (GuiButton(Rectangle{L.x, L.y, bw, 28}, "Centre on it")) { camX_ = l.x; camY_ = l.y; }
        if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 28}, "Delete")) { plan.landmarks.erase(plan.landmarks.begin() + landmarkSel_); landmarkSel_ = -1; }
        L.y += 36;
    }
    L.heading("LAYOUT");
    L.check("Show districts", &showDistricts_);
    L.check("Show landmark names", &showLabels_);
    bool walls = plan.walls;
    L.check("City walls", &walls);
    if (walls != plan.walls) { plan.walls = walls; generate(doc); }
    if (plan.baked) L.note("You've edited this city by hand, so New street layout will replace your buildings.");
    if (L.button("New street layout")) {
        plan.seed = plan.seed * 2654435761u + 1013904223u;
        plan.districtEdits.clear();
        plan.baked = false; plan.objects.clear(); undo_.clear(); selObj_ = -1;
        generate(doc);
    }
    if (L.button("Save this view as an image")) {
        std::string path = exportsFolder() + "/" + city.name + "_map.png";
        for (char& ch : path) if (ch == ' ') ch = '_';
        EndDrawing();
        TakeScreenshot(path.c_str());
        BeginDrawing();
    }
    if (showDistricts_)
        for (int d = 1; d < (int)District::Count; d++) {   // compact two-column key
            int col = (d - 1) % 2, row = (d - 1) / 2;
            float x = L.x + col * L.w / 2, y = L.y + row * 20;
            DrawRectangle((int)x, (int)y + 3, 14, 14, kDistrictCol[d]);
            uiText(districtName((District)d), (int)x + 20, (int)y + 2, 15, Color{180, 188, 200, 255});
        }
    L.y += 6 * 20;
    panelH_ = L.y - startY + 30;
    EndDrawing();

    if (!keepOpen) close();
    return keepOpen;
}
