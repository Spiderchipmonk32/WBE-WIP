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
    group_ = 0;
    uplift_ = nullptr;
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
        h += World::sample(*uplift_, lat, lon);
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

double Planet::applyStroke(const Stroke& s, const Vec3d& d, double h) {
    double c = d.dot(s.dir);
    if (c <= s.cosRadius) return h;
    double ang = std::acos(std::min(1.0, c));
    double f = 0.5 * (1 + std::cos(kPi * ang / s.angRadius));
    switch (s.type) {
        case BrushType::Raise: h += s.amount * f; break;
        case BrushType::Lower: h -= s.amount * f; break;
        default:               h += (s.targetM - h) * std::min(1.0, s.amount * f); break;
    }
    return clampd(h, kMinElev, kMaxElev);
}

double Planet::elevation(const Vec3d& d, int detailOctaves, const std::vector<int>* filt) const {
    double h = baseElevation(d);
    h += detail(d, h, detailOctaves);
    if (filt) { for (int i : *filt) h = applyStroke(strokes_[i], d, h); }
    else      { for (const Stroke& s : strokes_) h = applyStroke(s, d, h); }
    return h;
}

double Planet::elevationWith(const Vec3d& d, int detailOctaves, const std::vector<Stroke>& strokes) const {
    double h = baseElevation(d);
    h += detail(d, h, detailOctaves);
    for (const Stroke& s : strokes) h = applyStroke(s, d, h);
    return h;
}

void Planet::beginStrokeGroup(BrushType type, const Vec3d& dir, double radiusKm) {
    group_++;
    if (type == BrushType::Flatten) {
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

void Planet::addDab(BrushType type, const Vec3d& dir, double radiusKm, double amount) {
    if (amount <= 0) return;
    double R = settings_.radiusKm;
    // Merge with the previous dab if the brush hasn't moved much, to keep edit lists short.
    if (!strokes_.empty()) {
        Stroke& last = strokes_.back();
        if (last.group == group_ && last.type == type && std::fabs(last.radiusKm - radiusKm) < 1e-9 &&
            angleBetween(last.dir, dir) * R < radiusKm * 0.15) {
            if (type == BrushType::Raise || type == BrushType::Lower) last.amount += amount;
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
    s.amount = (type == BrushType::Raise || type == BrushType::Lower) ? amount : std::min(1.0, amount);
    s.targetM = groupTarget_;
    s.group = group_;
    strokes_.push_back(s);
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
