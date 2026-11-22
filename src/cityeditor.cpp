#include "cityeditor.h"
#include "fonts.h"
#include "raygui.h"
#include "ui.h"
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
    camX_ = cx_; camY_ = cy_;
    zoom_ = (GetScreenHeight() - 80) / (cityR_ * 2.6);
    active = true;
}

void CityEditor::close() {
    if (haveTex_) UnloadTexture(terrain_);
    haveTex_ = false;
    active = false;
    roads_.clear(); buildings_.clear(); wall_.clear(); drawing_.clear();
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

void CityEditor::generate(Document& doc) {
    CityPlan& plan = *doc.planFor(city_);
    uint32_t s = plan.seed;
    roads_.clear(); buildings_.clear(); wall_.clear(); wallOnLand_.clear();
    const double step = std::max(25.0, cityR_ / 30);
    // move the centre onto dry land if it's in water
    double cx = 0, cy = 0;
    if (water(0, 0)) {
        for (double r = step; r < sizeM_ / 2 && water(cx, cy); r += step)
            for (int k = 0; k < 16; k++) { double a = k * kPi / 8; if (!water(r * std::cos(a), r * std::sin(a))) { cx = r * std::cos(a); cy = r * std::sin(a); break; } }
    }
    cx_ = cx; cy_ = cy;
    // a road walker that bends around water but can bridge rivers
    auto walk = [&](double x, double y, double ang, double len, double wiggle, int kind, bool bridges) {
        Road rd; rd.kind = kind;
        rd.pts.push_back({(float)x, (float)y});
        double st = kind == 0 ? step : step * 0.6;
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
        }
        if (rd.pts.size() >= 2) roads_.push_back(rd);
        return rd.pts.size();
    };
    int mains = 3 + (int)(rnd(s) * 3) + (cityR_ > 2000 ? 2 : 0);
    double base = rnd(s) * 2 * kPi;
    for (int k = 0; k < mains; k++) walk(cx, cy, base + k * 2 * kPi / mains + (rnd(s) - 0.5) * 0.4, sizeM_ * 0.7, 0.25, 0, true);
    // streets branching off the main roads, denser near the centre
    size_t nMain = roads_.size();
    for (size_t r = 0; r < nMain; r++) {
        Road main = roads_[r];
        for (size_t k = 2; k < main.pts.size(); k += 2) {
            double px = main.pts[k].x, py = main.pts[k].y, dist = std::hypot(px - cx, py - cy);
            if (dist > cityR_ * 1.3 || rnd(s) > 0.85 * std::exp(-std::pow(dist / cityR_, 2.0)) + 0.15) continue;
            double dir = std::atan2(main.pts[k].y - main.pts[k - 1].y, main.pts[k].x - main.pts[k - 1].x);
            for (int side = -1; side <= 1; side += 2) {
                if (rnd(s) < 0.25) continue;
                size_t before = roads_.size();
                walk(px, py, dir + side * kPi / 2 + (rnd(s) - 0.5) * 0.3, cityR_ * (0.25 + rnd(s) * 0.45), 0.12, 1, false);
                if (roads_.size() > before) {
                    Road st = roads_.back();
                    for (size_t q = 2; q < st.pts.size(); q += 2) {
                        double d2 = std::hypot(st.pts[q].x - cx, st.pts[q].y - cy);
                        if (d2 > cityR_ * 0.9 || rnd(s) > 0.55) continue;
                        double dd = std::atan2(st.pts[q].y - st.pts[q - 1].y, st.pts[q].x - st.pts[q - 1].x);
                        walk(st.pts[q].x, st.pts[q].y, dd + (rnd(s) < 0.5 ? 1 : -1) * kPi / 2, cityR_ * (0.08 + rnd(s) * 0.15), 0.08, 2, false);
                    }
                }
            }
        }
    }
    for (auto& r : plan.extraRoads) {
        Road rd; rd.kind = 3;
        for (auto& p : r) rd.pts.push_back({p.first, p.second});
        roads_.push_back(rd);
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
    }
    // districts
    district_.assign(DG * DG, 0);
    double hiX = cx, hiY = cy, hiH = -1e9;   // highest point near the centre (for the castle)
    for (int k = 0; k < 400; k++) {
        double a = rnd(s) * 2 * kPi, r = std::sqrt(rnd(s)) * cityR_ * 0.45;
        double x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
        if (!water(x, y) && heightAt(x, y) > hiH) { hiH = heightAt(x, y); hiX = x; hiY = y; }
    }
    double tAng = rnd(s) * 2 * kPi, tx = cx + std::cos(tAng) * cityR_ * 0.18, ty = cy + std::sin(tAng) * cityR_ * 0.18;
    for (int dy = 0; dy < DG; dy++)
        for (int dx = 0; dx < DG; dx++) {
            double x = (dx + 0.5) / DG * sizeM_ - sizeM_ / 2, y = sizeM_ / 2 - (dy + 0.5) / DG * sizeM_;
            if (water(x, y)) continue;
            double r = std::hypot(x - cx, y - cy);
            // near water?
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
            else if (r < cityR_ * 1.9) d = District::Farms;
            district_[dy * DG + dx] = (uint8_t)d;
        }
    baseDistrict_ = district_;
    for (auto& e : plan.districtEdits) if (e.first >= 0 && e.first < DG * DG) district_[e.first] = e.second;
    // buildings along the roads
    for (const Road& rd : roads_) {
        double half = rd.kind == 0 ? 6 : (rd.kind == 1 ? 4 : 2.5);
        for (size_t k = 1; k < rd.pts.size(); k++) {
            double x0 = rd.pts[k - 1].x, y0 = rd.pts[k - 1].y, x1 = rd.pts[k].x, y1 = rd.pts[k].y;
            double len = std::hypot(x1 - x0, y1 - y0);
            if (len < 1) continue;
            double ux = (x1 - x0) / len, uy = (y1 - y0) / len, ang = std::atan2(uy, ux);
            double spacing = 10 + rnd(s) * 8;
            for (double t = spacing / 2; t < len; t += spacing) {
                for (int side = -1; side <= 1; side += 2) {
                    double bx = x0 + ux * t, by = y0 + uy * t;
                    int dist = (int)district_.size() ? districtAt(bx, by) : 0;
                    District d = (District)dist;
                    if (d == District::None || d == District::Park) continue;
                    double r = std::hypot(bx - cx, by - cy);
                    double dens = d == District::Farms ? 0.08 : (std::exp(-std::pow(r / (cityR_ * 1.05), 2.0)) * 0.9 + 0.1);
                    if (rnd(s) > dens) continue;
                    double w = 7 + rnd(s) * 9, dd = 7 + rnd(s) * 8;
                    if (d == District::Noble || d == District::Market || d == District::Temple) { w *= 1.6; dd *= 1.5; }
                    if (d == District::Slums) { w *= 0.7; dd *= 0.7; }
                    if (d == District::Industry || d == District::Docks) { w *= 1.8; dd *= 1.6; }
                    double off = half + dd / 2 + 1.5;
                    double px = bx - uy * off * side, py = by + ux * off * side;
                    if (water(px, py)) continue;
                    buildings_.push_back({(float)px, (float)py, (float)w, (float)dd, (float)ang, (uint8_t)d, (uint8_t)(rng(s) & 255)});
                    if (buildings_.size() > 14000) break;
                }
            }
        }
    }
    (void)tx;
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
    const Rectangle panel{10, 10, 340, (float)sh - 20};
    Vector2 mouse = GetMousePosition();
    bool overPanel = CheckCollisionPointRec(mouse, panel);
    Vector2 local = toLocal(mouse);

    // ---- input ----
    if (!overPanel) {
        float wh = GetMouseWheelMove();
        if (wh != 0) {
            Vector2 before = toLocal(mouse);
            zoom_ = clampd(zoom_ * std::pow(1.2, wh), 0.01, 8);
            Vector2 after = toLocal(mouse);
            camX_ += before.x - after.x; camY_ += before.y - after.y;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && tool_ != 2) { Vector2 d = GetMouseDelta(); camX_ -= d.x / zoom_; camY_ += d.y / zoom_; }
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (tool_ == 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragLm_ < 0)) {
            Vector2 d = GetMouseDelta(); camX_ -= d.x / zoom_; camY_ += d.y / zoom_;
        }
        // tools
        if (tool_ == 1 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {   // paint districts
            int r = (int)std::ceil(brushM_ / sizeM_ * DG);
            int cx = (int)((local.x + sizeM_ / 2) / sizeM_ * DG), cy = (int)((sizeM_ / 2 - local.y) / sizeM_ * DG);
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                    int x = cx + dx, y = cy + dy;
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
            for (auto& b : buildings_) b.district = (uint8_t)districtAt(b.x, b.y);
        }
        if (tool_ == 2) {   // draw a road: left-click points, right-click to finish
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) drawing_.push_back({local.x, local.y});
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (drawing_.size() >= 2) { plan.extraRoads.push_back(drawing_); Road rd; rd.kind = 3; for (auto& p : drawing_) rd.pts.push_back({p.first, p.second}); roads_.push_back(rd); }
                drawing_.clear();
            }
        }
        if (tool_ == 3 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {   // add a landmark
            Landmark l; l.type = (LandmarkType)landmarkUi_; l.x = local.x; l.y = local.y;
            l.name = std::string("New ") + landmarkName(l.type);
            plan.landmarks.push_back(l);
            landmarkSel_ = (int)plan.landmarks.size() - 1;
            tool_ = 0;
        }
        // drag landmarks with the Select tool
        if (tool_ == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int i = 0; i < (int)plan.landmarks.size(); i++) {
                Vector2 p = toScreen(plan.landmarks[i].x, plan.landmarks[i].y);
                if (std::hypot(p.x - mouse.x, p.y - mouse.y) < 10) { dragLm_ = i; landmarkSel_ = i; break; }
            }
        }
        if (dragLm_ >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragLm_ < (int)plan.landmarks.size()) {
            plan.landmarks[dragLm_].x = local.x; plan.landmarks[dragLm_].y = local.y;
        }
    }
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragLm_ = -1;

    // ---- draw ----
    BeginDrawing();
    ClearBackground(Color{30, 40, 55, 255});
    Vector2 a = toScreen(-sizeM_ / 2, sizeM_ / 2);
    float side = (float)(sizeM_ * zoom_);
    if (haveTex_) DrawTexturePro(terrain_, Rectangle{0, 0, (float)G, (float)G}, Rectangle{a.x, a.y, side, side}, Vector2{0, 0}, 0, WHITE);
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
    // roads: outline then fill
    for (int pass = 0; pass < 2; pass++)
        for (const Road& rd : roads_) {
            float wm = rd.kind == 0 ? 10.f : (rd.kind == 1 ? 6.f : (rd.kind == 2 ? 3.5f : 7.f));
            float wpx = std::max(pass == 0 ? 2.2f : 1.2f, wm * (float)zoom_ + (pass == 0 ? 2 : 0));
            Color c = pass == 0 ? Color{90, 75, 60, 255} : (rd.kind == 3 ? Color{250, 230, 160, 255} : Color{226, 214, 186, 255});
            for (size_t k = 1; k < rd.pts.size(); k++) {
                Vector2 p0 = toScreen(rd.pts[k - 1].x, rd.pts[k - 1].y), p1 = toScreen(rd.pts[k].x, rd.pts[k].y);
                bool bridge = water(rd.pts[k].x, rd.pts[k].y);
                DrawLineEx(p0, p1, wpx, bridge && pass == 1 ? Color{170, 150, 120, 255} : c);
            }
        }
    // buildings
    if (zoom_ > 0.04) {
        for (const Building& b : buildings_) {
            Vector2 p = toScreen(b.x, b.y);
            if (p.x < panel.x + panel.width - 40 || p.x > sw + 40 || p.y < -40 || p.y > sh + 40) continue;
            Color c = kRoof[std::min<int>(b.district, 10)];
            int v = (int)b.shade % 40 - 20;
            c.r = (unsigned char)clampd(c.r + v, 0, 255); c.g = (unsigned char)clampd(c.g + v, 0, 255); c.b = (unsigned char)clampd(c.b + v, 0, 255);
            float w = std::max(1.0f, b.w * (float)zoom_), d = std::max(1.0f, b.d * (float)zoom_);
            DrawRectanglePro(Rectangle{p.x, p.y, w, d}, Vector2{w / 2, d / 2}, -b.ang * 180.f / (float)kPi, c);
        }
    }
    // walls
    if (!wall_.empty()) {
        float wpx = std::max(2.5f, 6.f * (float)zoom_);
        for (size_t k = 1; k < wall_.size(); k++) {
            if (!wallOnLand_[k] || !wallOnLand_[k - 1]) continue;
            DrawLineEx(toScreen(wall_[k - 1].x, wall_[k - 1].y), toScreen(wall_[k].x, wall_[k].y), wpx, Color{70, 64, 60, 255});
            if (k % 4 == 0) DrawCircleV(toScreen(wall_[k].x, wall_[k].y), wpx * 1.3f, Color{60, 55, 52, 255});
        }
    }
    // user road being drawn
    for (size_t k = 1; k < drawing_.size(); k++) DrawLineEx(toScreen(drawing_[k - 1].first, drawing_[k - 1].second), toScreen(drawing_[k].first, drawing_[k].second), 3, YELLOW);
    if (tool_ == 2 && !drawing_.empty()) DrawLineEx(toScreen(drawing_.back().first, drawing_.back().second), mouse, 2, Fade(YELLOW, 0.6f));
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
    if (tool_ == 1) {
        float r = (float)(brushM_ * zoom_);
        DrawCircleLines((int)mouse.x, (int)mouse.y, r, YELLOW);
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
    GuiPanel(panel, NULL);
    Layout L{panel.x + 14, panel.y + 12, panel.width - 28};
    if (L.button("< Back to world map", 32)) keepOpen = false;
    L.heading(TextFormat("CITY: %s", city.name.c_str()));
    L.label(TextFormat("Population %d%s", city.population, city.capital ? ", capital" : ""));
    L.label(TextFormat("Built-up radius about %.1f km, %zu buildings", cityR_ / 1000, buildings_.size()));
    L.heading("TOOLS");
    GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 28}, "Select / move;Paint district\nDraw road;Add landmark", &tool_);
    L.y += 64;
    if (tool_ == 1) {
        GuiComboBox(Rectangle{L.x, L.y, L.w, 28}, "None;Market;Old town;Noble quarter;Temple district;Residential;Docks;Industry;Farms;Slums;Park", &districtUi_);
        L.y += 34;
        L.label(TextFormat("Brush: %.0f m", brushM_)); L.slider(&brushM_, 30, 1500);
    } else if (tool_ == 2) {
        L.note("Left-click to add points, right-click to finish the road.");
    } else if (tool_ == 3) {
        GuiComboBox(Rectangle{L.x, L.y, L.w, 28}, "Market;Castle;Palace;Temple;Harbour;Gate;Guildhall;University;Tower;Monument;Inn;Other", &landmarkUi_);
        L.y += 34;
        L.note("Click the map to place it.");
    } else {
        L.note("Drag to move the view or a landmark. Scroll to zoom.");
    }
    L.heading("LANDMARKS");
    std::string items;
    for (auto& l : plan.landmarks) { if (!items.empty()) items += ";"; std::string n = l.name; for (char& ch : n) if (ch == ';') ch = ','; items += n; }
    GuiListView(Rectangle{L.x, L.y, L.w, 150}, items.c_str(), &landmarkScroll_, &landmarkSel_);
    L.y += 156;
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
    L.check("Show names", &showLabels_);
    bool walls = plan.walls;
    L.check("City walls", &walls);
    if (walls != plan.walls) { plan.walls = walls; generate(doc); }
    if (L.button("New street layout")) {
        plan.seed = plan.seed * 2654435761u + 1013904223u;
        plan.districtEdits.clear();
        generate(doc);
    }
    if (!plan.extraRoads.empty() && L.button("Remove my roads")) { plan.extraRoads.clear(); generate(doc); }
    for (int d = 1; d < (int)District::Count; d++) {   // compact two-column key
        int col = (d - 1) % 2, row = (d - 1) / 2;
        float x = L.x + col * L.w / 2, y = L.y + row * 20;
        DrawRectangle((int)x, (int)y + 3, 14, 14, kDistrictCol[d]);
        uiText(districtName((District)d), (int)x + 20, (int)y + 2, 15, DARKGRAY);
    }
    EndDrawing();

    if (!keepOpen) close();
    return keepOpen;
}
