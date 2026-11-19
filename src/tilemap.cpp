#include "tilemap.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double kKmPerDeg = 111.195;   // per degree of latitude on an Earth-sized planet (scaled below)
constexpr size_t kMaxCache = 900;

struct RGB { double r, g, b; };
RGB mixc(RGB a, RGB b, double t) { t = clampd(t, 0, 1); return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
RGB scale(RGB a, double f) { return {a.r * f, a.g * f, a.b * f}; }
unsigned char u8(double v) { return (unsigned char)clampd(v, 0, 255); }
}

Vec3d TileMap::dirFromLatLon(double lat, double lon) {
    double la = lat * kPi / 180, lo = lon * kPi / 180;
    return {std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)};
}

int TileMap::levelFor(double pxPerDeg) {
    double base = TILE / 90.0;   // pixels per degree at level 0
    int l = (int)std::ceil(std::log2(std::max(1e-9, pxPerDeg / base)) - 0.15);
    return std::max(0, std::min(MAX_LEVEL, l));
}

TileMap::TileMap(const Planet* planet) : planet_(planet) {
    unsigned n = std::max(2u, std::thread::hardware_concurrency());
    n = std::max(1u, n - 1);
    for (unsigned i = 0; i < n; i++) workers_.emplace_back([this] { workerLoop(); });
}

TileMap::~TileMap() { shutdown(); }

void TileMap::shutdown() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
    for (auto& kv : cache_) if (kv.second.hasTex) UnloadTexture(kv.second.tex);
    cache_.clear();
}

void TileMap::waitIdle() {
    std::unique_lock<std::mutex> lk(mtx_);
    queue_.clear();
    idleCv_.wait(lk, [this] { return active_ == 0; });
    results_.clear();
    inFlight_.clear();
}

void TileMap::reset() {
    waitIdle();
    for (auto& kv : cache_) if (kv.second.hasTex) UnloadTexture(kv.second.tex);
    cache_.clear();
    stamp_++;
}

void TileMap::setStyle(MapStyle s) {
    if (s == style_) return;
    style_ = s;
    invalidateAll();   // old tiles stay on screen until the new ones arrive
}

void TileMap::setOverlay(Overlay o, int param) {
    if (o == overlay_ && param == overlayParam_) return;
    overlay_ = o; overlayParam_ = param;
    invalidateAll();
}

bool TileMap::overlayColor(Overlay o, int param, double lat, double lon, double h, double rgb[3], double& alpha) const {
    (void)h; (void)param;
    if (!world_ || world_->plateId.empty()) return false;
    int c = World::nearestCell(lat, lon);
    auto set = [&](double r, double g, double b, double a) { rgb[0] = r; rgb[1] = g; rgb[2] = b; alpha = a; return true; };
    switch (o) {
        case Overlay::Plates: {
            const Plate& p = world_->plates[world_->plateId[c]];
            double d = world_->distBoundaryKm[c];
            if (d < world_->cellKm() * 0.8) {
                switch ((Boundary)world_->boundary[c]) {
                    case Boundary::Collision: case Boundary::Subduction: case Boundary::IslandArc: return set(200, 30, 30, 0.9);
                    case Boundary::Rift: case Boundary::Ridge: return set(30, 90, 210, 0.9);
                    case Boundary::Transform: return set(230, 190, 30, 0.9);
                    default: break;
                }
            }
            return set(p.r, p.g, p.b, p.continental ? 0.45 : 0.3);
        }
        case Overlay::Rock: {
            static const double pal[6][3] = {{70, 82, 100}, {214, 188, 132}, {178, 58, 48}, {232, 150, 168}, {132, 104, 176}, {96, 140, 84}};
            int r = std::min(5, (int)world_->rock[c]);
            return set(pal[r][0], pal[r][1], pal[r][2], 0.6);
        }
        case Overlay::Earthquakes: {
            double st = World::sample(world_->stress, lat, lon);
            if (st < 0.05) return false;
            return set(255, 220 - 200 * st, 40, clampd(st * 1.1, 0, 0.8));
        }
        case Overlay::Temperature: {
            if (world_->tempYear.empty()) return false;
            double t = param >= 0 && param < 12 ? World::sample(world_->tempMonth[param], lat, lon) : World::sample(world_->tempYear, lat, lon);
            if (h > 0) t -= 6.5 * (h - World::sample(world_->elev, lat, lon)) / 1000.0;   // finer height detail
            static const double st[][4] = {{-40, 120, 40, 160}, {-20, 40, 70, 220}, {0, 90, 200, 230}, {10, 80, 190, 90},
                                           {20, 240, 220, 60}, {30, 240, 120, 40}, {40, 180, 20, 30}};
            int k = 0; while (k < 6 && t > st[k + 1][0]) k++;
            double f = k >= 6 ? 1 : clampd((t - st[k][0]) / (st[k + 1][0] - st[k][0]), 0, 1);
            const double* a = st[k]; const double* b = st[std::min(6, k + 1)];
            return set(a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f, 0.6);
        }
        case Overlay::Rainfall: {
            if (world_->rainfallYear.empty()) return false;
            double r = param >= 0 && param < 12 ? World::sample(world_->rainMonth[param], lat, lon) * 12 : World::sample(world_->rainfallYear, lat, lon);
            double k = clampd(std::log10(std::max(30.0, r) / 30.0) / std::log10(4000.0 / 30.0), 0, 1);   // 30..4000 mm/yr
            static const double st[][3] = {{170, 110, 50}, {230, 200, 90}, {120, 190, 80}, {40, 150, 160}, {30, 60, 190}};
            int a = std::min(3, (int)(k * 4)); double f = k * 4 - a;
            return set(st[a][0] + (st[a + 1][0] - st[a][0]) * f, st[a][1] + (st[a + 1][1] - st[a][1]) * f, st[a][2] + (st[a + 1][2] - st[a][2]) * f, h > 0 ? 0.65 : 0.35);
        }
        case Overlay::Biomes: {
            if (world_->biome.empty()) return false;
            Biome b = (Biome)world_->biome[c];
            if (b == Biome::Ocean) return false;
            unsigned char rgb3[3]; biomeColor(b, rgb3);
            return set(rgb3[0], rgb3[1], rgb3[2], 0.7);
        }
        case Overlay::Wind: {
            if (world_->windU[0].empty()) return false;
            int m = param >= 0 && param < 12 ? param : 0;
            double u = World::sample(world_->windU[m], lat, lon), v = World::sample(world_->windV[m], lat, lon);
            double sp = std::sqrt(u * u + v * v);
            return set(255, 255, 255, clampd(sp / 14.0, 0, 0.45));
        }
        case Overlay::Currents: {
            if (world_->seaTempAnomaly.empty() || h > 0) return false;
            double an = World::sample(world_->seaTempAnomaly, lat, lon);
            if (std::fabs(an) < 0.4) return false;
            return an > 0 ? set(230, 70, 50, clampd(an / 6, 0, 0.6)) : set(60, 140, 255, clampd(-an / 6, 0, 0.6));
        }
        case Overlay::Resource: {
            if (world_->selectedFavor.empty() || world_->selectedRes < 0) return false;
            double f = World::sample(world_->selectedFavor, lat, lon);
            if (f < 0.04) return false;
            const ResourceDef& d = resourceDefs()[world_->selectedRes];
            return set(d.r, d.g, d.b, clampd(f * 0.85, 0, 0.8));
        }
        default: return false;
    }
}

void TileMap::invalidateAll() {
    stamp_++;
    for (auto& kv : cache_) kv.second.need = stamp_;
}

void TileMap::invalidate(const Vec3d& dir, double radiusKm) {
    stamp_++;
    double R = planet_->radiusKm();
    double kmPerDeg = kKmPerDeg * R / 6371.0;
    double lat = std::asin(clampd(dir.y, -1, 1)) * 180 / kPi;
    double lon = std::atan2(dir.x, dir.z) * 180 / kPi;
    double dLat = radiusKm / kmPerDeg;
    double maxAbsLat = std::min(89.9, std::fabs(lat) + dLat);
    double dLon = (lat + dLat > 89 || lat - dLat < -89) ? 360 : dLat / std::cos(maxAbsLat * kPi / 180);
    for (auto& kv : cache_) {
        Entry& e = kv.second;
        double T = tileDeg(e.level);
        double latTop = 90 - e.ty * T, latBot = latTop - T;
        if (latBot > lat + dLat || latTop < lat - dLat) continue;
        double lonL = -180 + e.tx * T, lonC = lonL + T / 2;
        double dl = std::fabs(std::remainder(lonC - lon, 360.0));
        if (dl > dLon + T / 2) continue;
        e.need = stamp_;
    }
}

TileMap::Entry& TileMap::entry(int l, int tx, int ty) {
    auto it = cache_.find(key(l, tx, ty));
    if (it != cache_.end()) return it->second;
    Entry& e = cache_[key(l, tx, ty)];
    e.level = l; e.tx = tx; e.ty = ty; e.need = stamp_; e.built = 0;
    return e;
}

void TileMap::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
            active_++;
        }
        Result r{job.level, job.tx, job.ty, job.stamp, {}};
        render(job, r.rgba);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            results_.push_back(std::move(r));
            active_--;
        }
        idleCv_.notify_all();
    }
}

void TileMap::render(const Job& j, std::vector<unsigned char>& out) const {
    const int N = TILE, B = N + 2;
    const double T = tileDeg(j.level);
    const double R = planet_->radiusKm();
    const double kmPerDeg = kKmPerDeg * R / 6371.0;
    const double midLat = 90 - (j.ty + 0.5) * T;
    const double spacingKm = std::max(0.0005, T / N * kmPerDeg * std::max(0.05, std::cos(midLat * kPi / 180)));
    const int oct = Planet::detailOctavesFor(spacingKm, R);

    std::vector<float> H(B * B);
    for (int by = 0; by < B; by++) {
        double lat = clampd(90 - (j.ty + (by - 1 + 0.5) / N) * T, -89.9999, 89.9999);
        for (int bx = 0; bx < B; bx++) {
            double lon = -180 + (j.tx + (bx - 1 + 0.5) / N) * T;
            H[by * B + bx] = (float)planet_->elevationWith(dirFromLatLon(lat, lon), oct, j.strokes);
        }
    }

    out.assign(N * N * 4, 255);
    const double dyM = T / N * kmPerDeg * 1000;
    const double exag = 2.0 * std::max(1.0, std::sqrt(dyM / 30.0));      // stronger relief shading when zoomed out
    const double Lx = -0.62, Ly = 0.62, Lz = 0.48;                       // light from the north-west

    for (int y = 0; y < N; y++) {
        double lat = clampd(90 - (j.ty + (y + 0.5) / N) * T, -89.9999, 89.9999);
        double dxM = std::max(1.0, dyM * std::cos(lat * kPi / 180));
        for (int x = 0; x < N; x++) {
            int c = (y + 1) * B + (x + 1);
            double h = H[c], hl = H[c - 1], hr = H[c + 1], hu = H[c - B], hd = H[c + B];
            // hillshade
            double gx = (hr - hl) / (2 * dxM) * exag, gn = (hu - hd) / (2 * dyM) * exag;
            double nl = std::sqrt(gx * gx + gn * gn + 1);
            double shade = ((-gx) * Lx + (-gn) * Ly + Lz) / nl / Lz;      // 1 = flat ground
            double lon = -180 + (j.tx + (x + 0.5) / N) * T;
            Vec3d dir = dirFromLatLon(lat, lon);
            bool land = h > 0;
            bool coast = land && (hl <= 0 || hr <= 0 || hu <= 0 || hd <= 0);
            // lakes: fine terrain below the lake's water level
            double lakeDepth = 0, salt = 0;
            if (land && world_ && !world_->lakeSurface.empty()) {
                double surf = World::sample(world_->lakeSurface, lat, lon);
                if (surf > h) lakeDepth = surf - h;
                if (lakeDepth <= 0) salt = World::sample(world_->saltMask, lat, lon);
            }
            RGB col;

            if (j.style == MapStyle::Realistic) {
                unsigned char s[4];
                planet_->surfaceColor(dir, h, s);
                col = {(double)s[0], (double)s[1], (double)s[2]};
                double f = land ? clampd(1 + (shade - 1) * 0.9, 0.45, 1.35) : clampd(1 + (shade - 1) * 0.25, 0.8, 1.12);
                col = scale(col, f);
            } else if (j.style == MapStyle::Elevation) {
                unsigned char s[3];
                Planet::colorFor(h, s);
                col = {(double)s[0], (double)s[1], (double)s[2]};
                col = scale(col, land ? clampd(1 + (shade - 1) * 0.7, 0.55, 1.25) : 1.0);
                auto band = [](double v) { return std::floor(v / 1000.0); };
                if (land && (band(hl) != band(h) || band(hu) != band(h))) col = scale(col, 0.8);   // 1 km contours
                if (coast) col = {30, 30, 30};
            } else {   // Fantasy parchment
                double gpx = (j.tx * (double)N + x) / 37.0, gpy = (j.ty * (double)N + y) / 37.0;
                double grain = paper_(gpx, gpy, 0.5) * 0.6 + paper_(gpx * 4, gpy * 4, 2.5) * 0.4;
                // a 2-pixel ink coastline reads better than a hairline
                auto at = [&](int dx, int dy) { return H[(y + 1 + dy) * B + (x + 1 + dx)]; };
                bool nearSea = false, nearLand = false;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) { double v2 = at(dx, dy); if (v2 <= 0) nearSea = true; else nearLand = true; }
                if (land) {
                    unsigned char s[4];
                    planet_->surfaceColor(dir, h, s);
                    RGB paper{238, 224, 186};
                    col = mixc(paper, RGB{(double)s[0], (double)s[1], (double)s[2]}, 0.2);
                    col = scale(col, clampd(1 + (shade - 1) * 0.45, 0.7, 1.08));
                    auto band = [](double v) { return std::floor(v / 600.0); };
                    if (band(hl) != band(h) || band(hu) != band(h)) col = mixc(col, RGB{150, 116, 80}, 0.35);
                    if (nearSea) col = {62, 44, 28};
                } else {
                    col = mixc(RGB{214, 214, 192}, RGB{176, 188, 176}, smoothstepd(0, 4500, -h));
                    if (nearLand) col = mixc(col, RGB{62, 44, 28}, 0.6);
                    // two or three ripple lines hugging the coast
                    auto band = [](double v) { return std::floor(v / 110.0); };
                    if (h > -330 && (band(hl) != band(h) || band(hu) != band(h)))
                        col = mixc(col, RGB{112, 128, 124}, 0.6);
                }
                col = scale(col, 1 + grain * 0.06);
            }
            if (lakeDepth > 0) {
                if (j.style == MapStyle::Fantasy) col = mixc(RGB{199, 210, 196}, RGB{62, 44, 28}, lakeDepth < 25 ? 0.5 : 0.0);
                else if (j.style == MapStyle::Elevation) col = RGB{80, 140, 210};
                else col = mixc(RGB{80, 150, 200}, RGB{30, 70, 140}, smoothstepd(0, 300, lakeDepth));
            } else if (salt > 0.5 && j.style != MapStyle::Elevation) {
                col = mixc(col, RGB{236, 232, 220}, smoothstepd(0.5, 0.8, salt) * 0.85);
            }
            if (j.overlay != Overlay::None) {
                double oc[3], oa;
                if (overlayColor(j.overlay, j.param, lat, lon, h, oc, oa)) col = mixc(col, RGB{oc[0], oc[1], oc[2]}, oa);
            }
            unsigned char* p = &out[(y * N + x) * 4];
            p[0] = u8(col.r); p[1] = u8(col.g); p[2] = u8(col.b); p[3] = 255;
        }
    }
}

void TileMap::update(const MapView& v, int sw, int sh) {
    frame_++;
    // 1. Upload finished tiles
    std::vector<Result> done;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        done.swap(results_);
    }
    for (Result& r : done) {
        inFlight_.erase(key(r.level, r.tx, r.ty));
        Entry& e = entry(r.level, r.tx, r.ty);
        if (r.stamp < e.built) continue;
        Image img{r.rgba.data(), TILE, TILE, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        if (e.hasTex) UnloadTexture(e.tex);
        e.tex = LoadTextureFromImage(img);
        SetTextureFilter(e.tex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(e.tex, TEXTURE_WRAP_CLAMP);
        e.hasTex = true;
        e.built = r.stamp;
    }

    // 2. Work out which tiles are needed, nearest the screen centre first
    struct Want { double pri; int l, tx, ty; };
    std::vector<Want> wants;
    auto consider = [&](int l, int tx, int ty, double pri) {
        Entry& e = entry(l, tx, ty);
        e.lastUsed = frame_;
        if (e.built < e.need && !inFlight_.count(key(l, tx, ty))) wants.push_back({pri, l, tx, ty});
    };
    for (int ty = 0; ty < 2; ty++)               // whole-world tiles: always available as a backdrop
        for (int tx = 0; tx < 4; tx++) consider(0, tx, ty, -1e9);

    int L = levelFor(v.pxPerDeg);
    double T = tileDeg(L);
    int cols = 4 << L, rows = 2 << L;
    double lonMin = v.centerLon - sw / 2.0 / v.pxPerDeg, lonMax = v.centerLon + sw / 2.0 / v.pxPerDeg;
    double latMax = v.centerLat + sh / 2.0 / v.pxPerDeg, latMin = v.centerLat - sh / 2.0 / v.pxPerDeg;
    int tyA = std::max(0, (int)std::floor((90 - latMax) / T)), tyB = std::min(rows - 1, (int)std::floor((90 - latMin) / T));
    int txA = (int)std::floor((lonMin + 180) / T), txB = (int)std::floor((lonMax + 180) / T);
    if (L > 0) {
        for (int ty = tyA; ty <= tyB; ty++) {
            for (int txu = txA; txu <= txB; txu++) {
                int tx = ((txu % cols) + cols) % cols;
                double cx = -180 + (txu + 0.5) * T - v.centerLon, cy = 90 - (ty + 0.5) * T - v.centerLat;
                consider(L, tx, ty, cx * cx + cy * cy);
                // keep the parent level warm too, so zooming out is instant
                if (L > 1) { Entry& p = entry(L - 1, tx / 2, ty / 2); p.lastUsed = frame_; }
            }
        }
    }
    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.pri < b.pri; });

    // 3. Hand a few jobs to the workers (a short queue keeps priorities fresh as you pan)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        size_t limit = workers_.size() * 2;
        for (const Want& w : wants) {
            if (queue_.size() >= limit) break;
            double TT = tileDeg(w.l);
            Vec3d c = dirFromLatLon(90 - (w.ty + 0.5) * TT, -180 + (w.tx + 0.5) * TT);
            double ang = TT * kPi / 180 * 0.75 + 0.002;
            queue_.push_back(Job{w.l, w.tx, w.ty, entry(w.l, w.tx, w.ty).need, style_, overlay_, overlayParam_, planet_->strokesCopyNear(c, ang)});
            inFlight_.insert(key(w.l, w.tx, w.ty));
        }
    }
    cv_.notify_all();

    // 4. Forget tiles that haven't been looked at for a while
    if (cache_.size() > kMaxCache) {
        std::vector<std::pair<uint64_t, uint64_t>> old;
        for (auto& kv : cache_)
            if (kv.second.level > 0 && kv.second.lastUsed + 2 < frame_ && !inFlight_.count(kv.first))
                old.push_back({kv.second.lastUsed, kv.first});
        std::sort(old.begin(), old.end());
        size_t remove = std::min(old.size(), cache_.size() - kMaxCache * 3 / 4);
        for (size_t i = 0; i < remove; i++) {
            Entry& e = cache_[old[i].second];
            if (e.hasTex) UnloadTexture(e.tex);
            cache_.erase(old[i].second);
        }
    }
}

bool TileMap::drawTile(int l, int tx, int ty, Rectangle dst) {
    // Use this tile if it's ready, otherwise stretch part of a coarser ancestor over the gap
    for (int a = l; a >= 0; a--) {
        int shift = l - a;
        auto it = cache_.find(key(a, tx >> shift, ty >> shift));
        if (it == cache_.end() || !it->second.hasTex) continue;
        float sub = (float)TILE / (1 << shift);
        Rectangle src{(tx & ((1 << shift) - 1)) * sub, (ty & ((1 << shift) - 1)) * sub, sub, sub};
        DrawTexturePro(it->second.tex, src, dst, Vector2{0, 0}, 0, WHITE);
        return true;
    }
    return false;
}

void TileMap::draw(const MapView& v, int sw, int sh) {
    int L = levelFor(v.pxPerDeg);
    double T = tileDeg(L);
    int cols = 4 << L, rows = 2 << L;
    double lonMin = v.centerLon - sw / 2.0 / v.pxPerDeg, lonMax = v.centerLon + sw / 2.0 / v.pxPerDeg;
    double latMax = v.centerLat + sh / 2.0 / v.pxPerDeg, latMin = v.centerLat - sh / 2.0 / v.pxPerDeg;
    int tyA = std::max(0, (int)std::floor((90 - latMax) / T)), tyB = std::min(rows - 1, (int)std::floor((90 - latMin) / T));
    int txA = (int)std::floor((lonMin + 180) / T), txB = (int)std::floor((lonMax + 180) / T);
    for (int ty = tyA; ty <= tyB; ty++) {
        for (int txu = txA; txu <= txB; txu++) {
            int tx = ((txu % cols) + cols) % cols;
            double x0 = sw / 2.0 + (-180 + txu * T - v.centerLon) * v.pxPerDeg;
            double y0 = sh / 2.0 - (90 - ty * T - v.centerLat) * v.pxPerDeg;
            double x1 = x0 + T * v.pxPerDeg, y1 = y0 + T * v.pxPerDeg;
            Rectangle dst{(float)std::floor(x0), (float)std::floor(y0), (float)(std::floor(x1) - std::floor(x0) + 1),
                          (float)(std::floor(y1) - std::floor(y0) + 1)};
            drawTile(L, tx, ty, dst);
        }
    }
}
