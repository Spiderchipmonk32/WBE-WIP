#include "planet.h"
#include "world.h"
#include <algorithm>
#include <cmath>

namespace {

Vec3d fibonacciPoint(int i, int n) {
    const double golden = kPi * (3.0 - std::sqrt(5.0));
    double y = 1.0 - 2.0 * (i + 0.5) / n;
    double r = std::sqrt(std::max(0.0, 1.0 - y * y));
    double phi = i * golden;
    return {r * std::cos(phi), y, r * std::sin(phi)};
}

double fbm(const Simplex3& n, double x, double y, double z, int oct) {
    double a = 1, f = 1, s = 0, norm = 0;
    for (int o = 0; o < oct; o++) { s += a * n(x * f, y * f, z * f); norm += a; a *= 0.5; f *= 2; }
    return s / norm;
}

double ridged(const Simplex3& n, double x, double y, double z, int oct) {
    double a = 1, f = 1, s = 0, norm = 0;
    for (int o = 0; o < oct; o++) {
        double r = 1 - std::fabs(n(x * f, y * f, z * f));
        r *= r; s += a * r; norm += a; a *= 0.5; f *= 2.1;
    }
    return s / norm;
}

// Any vector perpendicular to v (for building a local frame)
Vec3d anyPerp(const Vec3d& v) {
    Vec3d ref = std::fabs(v.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0};
    return ref.cross(v).normalized();
}

constexpr int kStatSamples = 20000;
constexpr double kMinElev = -12000, kMaxElev = 15000;

} // namespace

void Planet::generate(const PlanetSettings& s) {
    settings_ = s;
    uint32_t seed = (uint32_t)s.seed;
    nCont_.reseed(seed);
    nMtn_.reseed(seed + 101);
    nSmall_.reseed(seed + 202);
    nWarp_.reseed(seed + 303);
    nDetail_.reseed(seed + 404);
    nMoist_.reseed(seed + 505);
    contScale_ = 1.1 / std::max(0.2f, s.continentSize);
    strokes_.clear();
    eventStrokes_.clear();
    group_ = 0;
    uplift_ = nullptr;
    erosion_ = nullptr;
    climT_ = climR_ = nullptr;

    // Sea level is chosen so the requested share of the surface is ocean.
    std::vector<double> samples(kStatSamples);
    for (int i = 0; i < kStatSamples; i++) samples[i] = baseRaw(fibonacciPoint(i, kStatSamples));
    std::sort(samples.begin(), samples.end());
    int idx = std::min(kStatSamples - 1, (int)(s.oceanPercent / 100.0 * kStatSamples));
    sea_ = samples[idx];
    lo_ = samples.front();
    hi_ = samples.back() + 0.02;  // a little headroom for points between samples
}

double Planet::baseRaw(const Vec3d& d) const {
    double x = d.x, y = d.y, z = d.z;
    if (!import_.empty()) {
        // the imported image sets the shape; noise adds natural coastlines and ranges
        double lat = std::asin(clampd(y, -1, 1)) * 180 / kPi, lon = std::atan2(x, z) * 180 / kPi;
        double fx = (lon + 180) / 360 * importW_ - 0.5, fy = (90 - lat) / 180 * importH_ - 0.5;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        double tx = fx - x0, ty = fy - y0;
        auto at = [&](int xx, int yy) { xx = ((xx % importW_) + importW_) % importW_; yy = std::max(0, std::min(importH_ - 1, yy)); return import_[yy * importW_ + xx]; };
        double v = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
        double m = ridged(nMtn_, x * 3.2, y * 3.2, z * 3.2, 5);
        return (v - 0.5) * 1.3 + 0.07 * fbm(nSmall_, x * 9, y * 9, z * 9, 4) + settings_.mountains * 0.12 * m * m * smoothstepd(0.45, 0.7, v);
    }
    double wx = fbm(nWarp_, x * 1.6 + 11, y * 1.6, z * 1.6, 3);
    double wy = fbm(nWarp_, x * 1.6, y * 1.6 + 23, z * 1.6, 3);
    double wz = fbm(nWarp_, x * 1.6, y * 1.6, z * 1.6 + 37, 3);
    double X = x + 0.35 * wx, Y = y + 0.35 * wy, Z = z + 0.35 * wz;
    double cont = fbm(nCont_, X * contScale_, Y * contScale_, Z * contScale_, 6);
    double land = smoothstepd(-0.02, 0.22, cont);
    double m = ridged(nMtn_, X * 3.2, Y * 3.2, Z * 3.2, 5);
    double small = fbm(nSmall_, x * 9, y * 9, z * 9, 4);
    return cont + settings_.mountains * 0.32 * m * m * land + 0.06 * small;
}

double Planet::baseElevation(const Vec3d& d) const {
    double v = baseRaw(d);
    double h;
    if (v >= sea_) {
        double t = clampd((v - sea_) / std::max(1e-6, hi_ - sea_), 0, 1);
        h = 6500.0 * std::pow(t, 1.8);
    } else {
        double t = clampd((sea_ - v) / std::max(1e-6, sea_ - lo_), 0, 1);
        h = -(80.0 + 6200.0 * smoothstepd(0, 0.35, t) + 3500.0 * t * t * t);
    }
    if (uplift_) {
        double lat = std::asin(clampd(d.y, -1, 1)) * 180.0 / kPi, lon = std::atan2(d.x, d.z) * 180.0 / kPi;
        h += World::sampleCubic(*uplift_, lat, lon);
        if (erosion_) h += World::sampleCubic(*erosion_, lat, lon);
    }
    return h;
}

double Planet::detail(const Vec3d& d, double baseM, int octaves) const {
    if (octaves <= 0) return 0;
    // Rougher in high mountains, gentler in lowlands and on the sea floor.
    double amp = baseM > 0 ? 90.0 + 0.16 * baseM : 50.0;
    double a = 1, f = 14, s = 0;
    for (int o = 0; o < octaves; o++) { s += a * nDetail_(d.x * f, d.y * f, d.z * f); a *= 0.5; f *= 2.0; }
    return amp * s;
}

int Planet::detailOctavesFor(double spacingKm, double radiusKm) {
    // Add octaves until their wavelength is about twice the sample spacing.
    double ratio = 2.0 * kPi * radiusKm / (28.0 * std::max(1e-6, spacingKm));
    if (ratio < 1) return 1;
    int o = (int)std::floor(std::log2(ratio)) + 1;
    return std::max(1, std::min(22, o));
}

namespace {
const Simplex3 kBrushNoise(911u);
// fractal noise on the sphere, roughly -1..1; scaleKm is the size of the biggest bumps
double brushFbm(const Vec3d& d, double planetR, double scaleKm, int oct = 3) {
    double k = planetR / std::max(0.5, scaleKm), sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < oct; o++) {
        sum += amp * kBrushNoise(d.x * k + 31.7 * o, d.y * k - 12.1 * o, d.z * k + 5.3 * o);
        norm += amp; amp *= 0.5; k *= 2.03;
    }
    return sum / norm * 1.6;
}
// Natural-looking ground for new land: broad swells plus fine hills, scaled with roughness
double fillRelief(const Vec3d& d, double planetR, double scaleKm, double rough, double baseM) {
    double broad = 0.5 + 0.5 * brushFbm(d, planetR, scaleKm, 3);            // lowlands and uplands
    double hills = 1 - std::fabs(brushFbm(d, planetR, std::max(4.0, scaleKm * 0.25), 5));
    double fine = brushFbm(d, planetR, std::max(2.0, scaleKm * 0.06), 5);
    double amp = 120 + 0.35 * std::max(0.0, baseM);
    // hills mostly on the uplands, plains in the lowlands
    double up = smoothstepd(0.35, 0.85, broad);
    return (0.25 + rough) * (amp * 3.2 * broad * broad + amp * 2.2 * hills * hills * hills * up + amp * 0.25 * fine);
}
// ridged noise 0..1 (sharp crests), for mountain ranges
double brushRidge(const Vec3d& d, double planetR, double scaleKm) {
    double k = planetR / std::max(0.5, scaleKm), sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < 4; o++) {
        double n = 1 - std::fabs(kBrushNoise(d.x * k - 7.7 * o, d.y * k + 3.1 * o, d.z * k + 11.9 * o));
        sum += amp * n * n;
        norm += amp; amp *= 0.5; k *= 2.1;
    }
    return sum / norm;
}
}

// Lasso shapes: signed distance (km) from the drawn outline, positive inside
static double polygonDistance(const Stroke& s, const Vec3d& d, double R, double& size, double& edge) {
    double c = d.dot(s.dir);
    const std::vector<double>& v = *s.poly;
    const int n = (int)v.size() / 2 - 1;   // last pair: (size, edge width)
    size = v[2 * n]; edge = v[2 * n + 1];
    if (c < 0.05) return -1e9;
    Vec3d p = d * (1.0 / c) - s.dir;   // gnomonic projection onto the touching plane
    double x = p.dot(s.e1) * R, y = p.dot(s.e2) * R;
    if (s.noise > 0) {   // wiggle the coast with fractal noise (domain warp): bays, capes and islands
        double amp = s.noise * 0.11 * size;
        x += amp * brushFbm(d, R, size * 0.16, 6);
        y += amp * brushFbm(Vec3d{d.y, d.z, d.x}, R, size * 0.16, 6);
    }
    bool inside = false;
    double best = 1e30;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        double xi = v[2 * i], yi = v[2 * i + 1], xj = v[2 * j], yj = v[2 * j + 1];
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) inside = !inside;
        double dx = xj - xi, dy = yj - yi, L2 = dx * dx + dy * dy;
        double t = L2 > 0 ? clampd(((x - xi) * dx + (y - yi) * dy) / L2, 0, 1) : 0;
        double ex = xi + t * dx - x, ey = yi + t * dy - y;
        best = std::min(best, ex * ex + ey * ey);
    }
    double dist = std::sqrt(best);
    return inside ? dist : -dist;
}

bool Planet::makePolygonStroke(Stroke& s, const std::vector<Vec3d>& pts, double R) {
    if (pts.size() < 3) return false;
    Vec3d c{0, 0, 0};
    for (auto& p : pts) c = c + p;
    if (c.length() < 1e-9) return false;
    c = c.normalized();
    s.dir = c;
    s.e1 = anyPerp(c).normalized();
    s.e2 = c.cross(s.e1).normalized();
    auto v = std::make_shared<std::vector<double>>();
    double maxAng = 0, minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
    for (auto& p : pts) {
        double cc = p.dot(c);
        if (cc < 0.2) return false;   // too big to draw as one shape
        Vec3d q = p * (1.0 / cc) - c;
        double x = q.dot(s.e1) * R, y = q.dot(s.e2) * R;
        v->push_back(x); v->push_back(y);
        minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y);
        maxAng = std::max(maxAng, angleBetween(p, c));
    }
    double size = std::max(maxX - minX, maxY - minY);
    if (size < 1) return false;
    double edge = 4 + (1 - clampd(s.hardness, 0, 1)) * 0.06 * size;
    v->push_back(size); v->push_back(edge);
    s.poly = v;
    s.radiusKm = maxAng * R + s.noise * 0.11 * size * 1.7 + (s.type == BrushType::LandFill ? 0.05 * size + 60 : 0) + 10;
    s.angRadius = std::min(kPi * 0.95, s.radiusKm / R);
    s.cosRadius = std::cos(s.angRadius);
    return true;
}

bool Planet::addLasso(BrushType type, const std::vector<std::pair<double, double>>& latLon, double targetM, double hardness, double noise) {
    std::vector<Vec3d> pts;
    for (auto& ll : latLon) {
        double la = ll.first * kPi / 180, lo = ll.second * kPi / 180;
        pts.push_back(Vec3d{std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)});
    }
    Stroke s{};
    s.type = type; s.amount = 1; s.targetM = targetM + settings_.seaLevel; s.hardness = hardness; s.noise = noise;
    if (!makePolygonStroke(s, pts, settings_.radiusKm)) return false;
    s.group = ++group_;
    strokes_.push_back(s);
    return true;
}

double Planet::applyStroke(const Stroke& s, const Vec3d& d, double h) {
    double c = d.dot(s.dir);
    if (c <= s.cosRadius) return h;
    const double R = s.radiusKm / std::max(1e-9, s.angRadius);
    if (s.poly) {
        double size, edge;
        double dist = polygonDistance(s, d, R, size, edge);
        if (dist < -1e8) return h;
        if (s.type == BrushType::LandFill) {
            if (dist > 0) {
                double f = smoothstepd(0, edge, dist);
                // ground rises gently inland so rivers can drain to the sea
                double inland = smoothstepd(0, 0.35 * size, dist);
                double target = s.targetM * (0.4 + 0.9 * inland) + fillRelief(d, R, std::max(20.0, size * 0.2), s.noise, s.targetM) * (0.35 + 0.65 * inland);
                if (h < target) h += (target - h) * f;
                // the coast itself sits a little inside the line: shallow water right at the edge
                if (f < 1 && h < 0) h = std::max(h, -120.0 * (1 - f));
            } else {
                // continental shelf, then a slope down to the old sea floor
                double shelf = 0.04 * size + 40;
                double t = -dist / shelf;
                if (t < 1) {
                    double target = -60 - 140 * t - 2600 * t * t * t;
                    if (h < target) h += (target - h) * (1 - smoothstepd(0.6, 1.0, t));
                }
            }
        } else if (s.type == BrushType::SeaFill && dist > 0) {
            double f = smoothstepd(0, edge, dist);
            double target = s.targetM - s.noise * std::fabs(s.targetM) * 0.3 * (0.5 + 0.5 * brushFbm(d, R, std::max(20.0, size * 0.15), 3));
            if (h > target) h += (target - h) * f;
        }
        return clampd(h, kMinElev, kMaxElev);
    }
    double ang = std::acos(std::min(1.0, c));
    double t = ang / s.angRadius;                       // 0 at the centre, 1 at the edge
    if (s.noise > 0) {
        // ragged edge: the brush reaches less far in some directions (only shrinks, so no cut-off circle)
        double n = brushFbm(d, R, s.radiusKm * 0.45, 4);
        t *= 1 + s.noise * 0.6 * (0.5 + 0.5 * n);
        if (t >= 1) return h;
    }
    double t0 = clampd(s.hardness, 0, 1) * 0.85;
    double f = t <= t0 ? 1.0 : 0.5 * (1 + std::cos(kPi * (t - t0) / (1 - t0)));
    switch (s.type) {
        case BrushType::Raise: h += s.amount * f; break;
        case BrushType::Lower: h -= s.amount * f; break;
        case BrushType::Mountains: {
            double r = brushRidge(d, R, std::max(8.0, s.radiusKm * 0.4));
            h += s.amount * f * (0.3 + 0.7 * r * r);
            break;
        }
        case BrushType::Rough: h += s.amount * f * brushFbm(d, R, std::max(5.0, s.radiusKm * 0.25), 4); break;
        case BrushType::LandFill: {
            // a gentle dome under each dab, so painted land drains outwards instead of filling with lakes
            double dome = 0.45 + 0.85 * (1 - t);
            double target = s.targetM * dome + fillRelief(d, R, std::max(10.0, s.radiusKm * 0.6), s.noise, s.targetM) * dome;
            if (h < target) h += (target - h) * std::min(1.0, s.amount * f);
            break;
        }
        case BrushType::SeaFill: {
            double target = s.targetM - s.noise * std::fabs(s.targetM) * 0.3 * (0.5 + 0.5 * brushFbm(d, R, std::max(10.0, s.radiusKm * 0.6), 3));
            if (h > target) h += (target - h) * std::min(1.0, s.amount * f);
            break;
        }
        default: h += (s.targetM - h) * std::min(1.0, s.amount * f); break;
    }
    return clampd(h, kMinElev, kMaxElev);
}

double Planet::elevation(const Vec3d& d, int detailOctaves, const std::vector<int>* filt) const {
    double h = baseElevation(d);
    h += detail(d, h, detailOctaves);
    if (filt) { for (int i : *filt) h = applyStroke(strokes_[i], d, h); }
    else      { for (const Stroke& s : strokes_) h = applyStroke(s, d, h); }
    for (const Stroke& s : eventStrokes_) h = applyStroke(s, d, h);
    return h - settings_.seaLevel;
}

double Planet::elevationWith(const Vec3d& d, int detailOctaves, const std::vector<Stroke>& strokes) const {
    double h = baseElevation(d);
    h += detail(d, h, detailOctaves);
    for (const Stroke& s : strokes) h = applyStroke(s, d, h);
    return h - settings_.seaLevel;
}

void Planet::beginStrokeGroup(BrushType type, const Vec3d& dir, double radiusKm, double fixedTarget) {
    group_++;
    if (fixedTarget > -1e8) {
        groupTarget_ = fixedTarget + settings_.seaLevel;   // targets are relative to sea level
    } else if (type == BrushType::Flatten) {
        groupTarget_ = elevation(dir, 14);
    } else if (type == BrushType::Smooth) {
        // average height across the brush
        double angR = radiusKm / std::max(1.0, (double)settings_.radiusKm);
        Vec3d e1 = anyPerp(dir), e2 = dir.cross(e1);
        double sum = 0; int n = 0;
        const int rings = 4, perRing = 12;
        for (int r = 0; r <= rings; r++) {
            double a = angR * r / rings;
            int count = r == 0 ? 1 : perRing;
            for (int k = 0; k < count; k++) {
                double th = 2 * kPi * k / count;
                Vec3d p = (dir * std::cos(a) + (e1 * std::cos(th) + e2 * std::sin(th)) * std::sin(a)).normalized();
                sum += elevation(p, Planet::detailOctavesFor(radiusKm / 8, settings_.radiusKm));
                n++;
            }
        }
        groupTarget_ = sum / n;
    }
}

void Planet::addDab(BrushType type, const Vec3d& dir, double radiusKm, double amount, double hardness, double noise) {
    if (amount <= 0) return;
    double R = settings_.radiusKm;
    // Merge with the previous dab if the brush hasn't moved much, to keep edit lists short.
    if (!strokes_.empty()) {
        Stroke& last = strokes_.back();
        if (last.group == group_ && last.type == type && std::fabs(last.radiusKm - radiusKm) < 1e-9 &&
            last.hardness == hardness && last.noise == noise && angleBetween(last.dir, dir) * R < radiusKm * 0.15) {
            if (additive(type)) last.amount += amount;
            else last.amount = 1 - (1 - last.amount) * (1 - std::min(1.0, amount));
            return;
        }
    }
    Stroke s;
    s.dir = dir.normalized();
    s.radiusKm = radiusKm;
    s.angRadius = std::min(kPi * 0.95, radiusKm / R);
    s.cosRadius = std::cos(s.angRadius);
    s.type = type;
    s.amount = additive(type) ? amount : std::min(1.0, amount);
    s.targetM = groupTarget_;
    s.group = group_;
    s.hardness = hardness;
    s.noise = noise;
    strokes_.push_back(s);
}

void Planet::addStamp(int kind, const Vec3d& dir, double r) {
    auto push = [&](BrushType t, const Vec3d& d, double rad, double amount, double target) {
        Stroke s{};
        s.dir = d.normalized(); s.radiusKm = rad; s.type = t; s.amount = amount; s.targetM = target; s.group = group_;
        s.angRadius = std::min(kPi * 0.95, rad / std::max(1.0, (double)settings_.radiusKm));
        s.cosRadius = std::cos(s.angRadius);
        strokes_.push_back(s);
    };
    group_++;
    double R = settings_.radiusKm;
    Vec3d east = Vec3d{0, 1, 0}.cross(dir).normalized();
    if (east.length() < 0.5) east = {1, 0, 0};
    Vec3d north = dir.cross(east);
    if (kind == 0) {          // impact crater: bowl, rim and central peak
        double depth = 700 * std::min(3.0, r / 10.0 + 0.5);
        push(BrushType::Lower, dir, r * 0.95, depth, 0);
        for (int i = 0; i < 24; i++) {
            double a = 2 * kPi * i / 24, ang = r / R;
            push(BrushType::Raise, (dir * std::cos(ang) + (east * std::cos(a) + north * std::sin(a)) * std::sin(ang)), r * 0.35, depth * 0.3, 0);
        }
        if (r > 15) push(BrushType::Raise, dir, r * 0.18, depth * 0.4, 0);
    } else if (kind == 1) {   // volcano with a summit crater
        push(BrushType::Raise, dir, r, 2400, 0);
        push(BrushType::Lower, dir, r * 0.16, 450, 0);
    } else {                  // mesa: a flat-topped plateau with steep sides
        double h = elevation(dir, 6) + settings_.seaLevel + 700;
        push(BrushType::Raise, dir, r * 1.1, 1400, 0);
        push(BrushType::Flatten, dir, r * 0.9, 1.0, h);
    }
}

void Planet::setStrokes(const std::vector<Stroke>& s) {
    strokes_ = s;
    group_ = 0;
    for (auto& st : strokes_) group_ = std::max(group_, st.group);
}

std::vector<std::pair<Vec3d, double>> Planet::undo() {
    std::vector<std::pair<Vec3d, double>> touched;
    if (strokes_.empty()) return touched;
    int g = strokes_.back().group;
    while (!strokes_.empty() && strokes_.back().group == g) {
        touched.push_back({strokes_.back().dir, strokes_.back().radiusKm});
        strokes_.pop_back();
    }
    return touched;
}

void Planet::strokesNear(const Vec3d& dir, double angRadius, std::vector<int>& out) const {
    out.clear();
    for (int i = 0; i < (int)strokes_.size(); i++)
        if (angleBetween(dir, strokes_[i].dir) < angRadius + strokes_[i].angRadius) out.push_back(i);
}

std::vector<Stroke> Planet::strokesCopyNear(const Vec3d& dir, double angRadius) const {
    std::vector<Stroke> out;
    for (const Stroke& s : strokes_)
        if (angleBetween(dir, s.dir) < angRadius + s.angRadius) out.push_back(s);
    for (const Stroke& s : eventStrokes_)
        if (angleBetween(dir, s.dir) < angRadius + s.angRadius) out.push_back(s);
    return out;
}

PlanetStats Planet::computeStats() const {
    PlanetStats st;
    int land = 0; double hi = -1e9, lo = 1e9;
    for (int i = 0; i < kStatSamples; i++) {
        double e = elevation(fibonacciPoint(i, kStatSamples), 3);
        if (e > 0) land++;
        hi = std::max(hi, e); lo = std::min(lo, e);
    }
    double R = settings_.radiusKm;
    st.landPercent = 100.0 * land / kStatSamples;
    st.highestM = hi; st.deepestM = lo;
    st.areaMkm2 = 4 * kPi * R * R / 1e6;
    return st;
}

void Planet::surfaceColor(const Vec3d& d, double h, unsigned char out[4]) const {
    struct C { double r, g, b; };
    auto mix = [](C a, C b, double t) { t = clampd(t, 0, 1); return C{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; };
    if (climT_ && climR_ && !climT_->empty()) {
        // Colours from the simulated climate
        double lat = std::asin(clampd(d.y, -1, 1)) * 180 / kPi, lon = std::atan2(d.x, d.z) * 180 / kPi;
        double wob = fbm(nMoist_, d.x * 7 + 5, d.y * 7, d.z * 7, 3);
        double T = World::sample(*climT_, lat, lon) - 6.5 * std::max(0.0, h) / 1000.0 + wob * 2.5;
        double P = World::sample(*climR_, lat, lon) * (1 + 0.25 * fbm(nMoist_, d.x * 4, d.y * 4, d.z * 4, 3));
        C c;
        if (h <= 0) {
            unsigned char rgb[3];
            colorFor(h, rgb);
            c = {(double)rgb[0], (double)rgb[1], (double)rgb[2]};
            double ice = smoothstepd(-0.8, -1.9, T);
            c = mix(c, C{222, 234, 242}, ice);
            out[3] = ice > 0.5 ? 255 : 128;
        } else {
            double pet = std::max(150.0, 250 + 35 * std::max(0.0, T));
            double ai = P / pet;
            C desert = T > 12 ? C{222, 194, 132} : C{180, 165, 124};
            C grass = T > 18 ? C{172, 166, 82} : C{142, 160, 84};
            C forest = T > 20 ? C{26, 88, 36} : (T > 4 ? C{52, 108, 48} : C{44, 80, 58});
            c = mix(desert, grass, smoothstepd(0.1, 0.35, ai));
            c = mix(c, forest, smoothstepd(0.45, 0.95, ai));
            c = mix(c, C{138, 136, 106}, smoothstepd(1, -5, T));                        // tundra
            c = mix(c, C{122, 102, 82}, smoothstepd(2200, 3800, h) * (1 - smoothstepd(0.8, 1.6, ai)));   // bare rock up high
            c = mix(c, C{148, 146, 144}, smoothstepd(3800, 5200, h));
            c = mix(c, C{206, 194, 146}, smoothstepd(12, 2, h) * (1 - smoothstepd(4, -4, T)));   // beaches
            c = mix(c, C{240, 243, 247}, smoothstepd(-6, -11, T));                      // snow and ice
            out[3] = 255;
        }
        out[0] = (unsigned char)c.r; out[1] = (unsigned char)c.g; out[2] = (unsigned char)c.b;
        return;
    }
    double absLat = std::asin(std::min(1.0, std::fabs(d.y))) * 180 / kPi;
    double m = fbm(nMoist_, d.x * 2.2, d.y * 2.2, d.z * 2.2, 4);   // large-scale variation, also wobbles the ice edge
    double wobble = fbm(nMoist_, d.x * 7 + 5, d.y * 7, d.z * 7, 3);
    double tempC = 27.0 - 52.0 * std::pow(absLat / 90.0, 1.5) - 6.5 * std::max(0.0, h) / 1000.0 + wobble * 9.0;
    C c;
    if (h <= 0) {
        unsigned char rgb[3];
        colorFor(h, rgb);
        c = {(double)rgb[0], (double)rgb[1], (double)rgb[2]};
        c = mix(c, C{222, 234, 242}, smoothstepd(-8, -13, tempC));   // sea ice
        out[3] = tempC < -11 ? 255 : 128;                              // ice isn't shiny water
    } else {
        // Wet vs dry: large-scale noise, plus drier belts near 25 degrees latitude (like the Sahara)
        double subtropic = std::exp(-std::pow((absLat - 25.0) / 10.0, 2.0));
        double dry = clampd(0.3 - m * 1.8 + subtropic * 0.4 - smoothstepd(45, 65, absLat) * 0.3, 0, 1);
        const C forest{40, 92, 42}, grass{118, 146, 68}, desert{206, 180, 122}, tundra{128, 126, 98},
                rock{122, 102, 82}, grey{148, 146, 144}, snow{240, 243, 247}, sand{206, 194, 146};
        c = mix(forest, grass, dry * 1.8);
        c = mix(c, desert, smoothstepd(0.6, 0.85, dry));
        c = mix(c, tundra, smoothstepd(6, -2, tempC));
        c = mix(c, rock, smoothstepd(1500, 3200, h));
        c = mix(c, grey, smoothstepd(3200, 4800, h));
        c = mix(c, sand, smoothstepd(12, 2, h) * (1 - smoothstepd(4, -4, tempC)));  // beaches
        c = mix(c, snow, smoothstepd(-8, -14, tempC));                              // snow and ice caps
        out[3] = 255;
    }
    out[0] = (unsigned char)c.r; out[1] = (unsigned char)c.g; out[2] = (unsigned char)c.b;
}

void Planet::colorFor(double h, unsigned char rgb[3]) {
    struct Stop { double h; unsigned char r, g, b; };
    static const Stop stops[] = {
        {-8000, 10, 26, 58}, {-4000, 15, 47, 99}, {-1500, 29, 90, 156}, {-200, 59, 134, 196}, {-1, 111, 179, 221},
        {0, 203, 189, 142}, {60, 79, 138, 63}, {500, 111, 154, 72}, {1200, 165, 163, 93}, {2200, 154, 123, 82},
        {3500, 125, 102, 80}, {4600, 154, 154, 154}, {5600, 244, 246, 248}};
    const int n = sizeof(stops) / sizeof(stops[0]);
    if (h <= stops[0].h) { rgb[0] = stops[0].r; rgb[1] = stops[0].g; rgb[2] = stops[0].b; return; }
    for (int i = 0; i < n - 1; i++) {
        if (h <= stops[i + 1].h) {
            double t = (h - stops[i].h) / (stops[i + 1].h - stops[i].h);
            rgb[0] = (unsigned char)(stops[i].r + (stops[i + 1].r - stops[i].r) * t);
            rgb[1] = (unsigned char)(stops[i].g + (stops[i + 1].g - stops[i].g) * t);
            rgb[2] = (unsigned char)(stops[i].b + (stops[i + 1].b - stops[i].b) * t);
            return;
        }
    }
    rgb[0] = stops[n - 1].r; rgb[1] = stops[n - 1].g; rgb[2] = stops[n - 1].b;
}
