#include "terrain.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace {
// Cube faces: outward normal, U axis, V axis (U x V = normal)
const Vec3d kN[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
const Vec3d kU[6] = {{0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0}, {1,0,0}, {-1,0,0}};
const Vec3d kV[6] = {{0,1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {0,1,0}, {0,1,0}};
constexpr double kSplitFactor = 2.2;   // split when closer than this many tile widths
constexpr double kBuildBudget = 0.010; // seconds of tile building per frame
}

Vec3d Terrain::cubeToSphere(int face, double u, double v) {
    Vec3d p = kN[face] + kU[face] * u + kV[face] * v;
    double x2 = p.x * p.x, y2 = p.y * p.y, z2 = p.z * p.z;
    // "Spherified cube" mapping: cells stay close to equal size across the face
    Vec3d s{p.x * std::sqrt(std::max(0.0, 1 - y2 / 2 - z2 / 2 + y2 * z2 / 3)),
            p.y * std::sqrt(std::max(0.0, 1 - z2 / 2 - x2 / 2 + z2 * x2 / 3)),
            p.z * std::sqrt(std::max(0.0, 1 - x2 / 2 - y2 / 2 + x2 * y2 / 3))};
    return s.normalized();
}

double Terrain::displayHeightKm(const Vec3d& dir, double spacingKm) const {
    if (!planet_) return 0;
    double h = planet_->elevation(dir, Planet::detailOctavesFor(spacingKm, planet_->radiusKm()));
    if (!drainOceans) h = std::max(0.0, h);
    return h / 1000.0 * exaggeration;
}

void Terrain::initNode(TileNode& n, int face, int level, double u0, double v0, double size) {
    double R = planet_->radiusKm();
    n.face = face; n.level = level; n.u0 = u0; n.v0 = v0; n.size = size;
    n.centerDir = cubeToSphere(face, u0 + size / 2, v0 + size / 2);
    n.tileKm = size / 2 * (kPi / 2) * R;
    n.angRadius = n.tileKm * 0.8 / R;
    n.centerPos = n.centerDir * (R + displayHeightKm(n.centerDir, n.tileKm / GRID));
}

void Terrain::reset(const Planet* planet) {
    clear();
    planet_ = planet;
    for (int f = 0; f < 6; f++) {
        roots_[f] = std::make_unique<TileNode>();
        initNode(*roots_[f], f, 0, -1, -1, 2);
        build(*roots_[f]);
    }
}

void Terrain::freeNode(TileNode& n) {
    freeChildren(n);
    if (n.hasMesh) { UnloadMesh(n.mesh); n.mesh = Mesh{}; n.hasMesh = false; }
}

void Terrain::freeChildren(TileNode& n) {
    for (auto& c : n.child) {
        if (c) { freeNode(*c); c.reset(); }
    }
}

void Terrain::clear() {
    for (auto& r : roots_) {
        if (r) { freeNode(*r); r.reset(); }
    }
}

void Terrain::build(TileNode& n) {
    const double R = planet_->radiusKm();
    const int V = GRID + 1;     // vertices per side
    const int B = V + 2;        // plus a border ring, so edge normals match neighbours
    const double step = n.size / GRID;
    const double spacing = n.tileKm / GRID;
    const int oct = Planet::detailOctavesFor(spacing, R);

    std::vector<int> filt;
    planet_->strokesNear(n.centerDir, n.angRadius * 1.3, filt);

    auto dispKm = [&](double hM) { return (drainOceans ? hM : std::max(0.0, hM)) / 1000.0 * exaggeration; };

    double hc = planet_->elevation(n.centerDir, oct, &filt);
    n.centerPos = n.centerDir * (R + dispKm(hc));

    std::vector<Vec3d> P(B * B), D(B * B);
    std::vector<double> H(B * B);
    for (int j = 0; j < B; j++) {
        for (int i = 0; i < B; i++) {
            Vec3d dir = cubeToSphere(n.face, n.u0 + (i - 1) * step, n.v0 + (j - 1) * step);
            double h = planet_->elevation(dir, oct, &filt);
            int k = j * B + i;
            D[k] = dir; H[k] = h;
            P[k] = dir * (R + dispKm(h));
        }
    }

    const int mainCount = V * V;
    const int vertCount = mainCount + 4 * V;
    const int triCount = GRID * GRID * 2 + 4 * GRID * 2;

    Mesh m{};
    m.vertexCount = vertCount;
    m.triangleCount = triCount;
    m.vertices = (float*)MemAlloc(vertCount * 3 * sizeof(float));
    m.normals = (float*)MemAlloc(vertCount * 3 * sizeof(float));
    m.colors = (unsigned char*)MemAlloc(vertCount * 4);
    m.indices = (unsigned short*)MemAlloc(triCount * 3 * sizeof(unsigned short));

    std::vector<Vec3d> normals(mainCount);
    auto put = [&](int vi, const Vec3d& pos, const Vec3d& nrm, double hM) {
        Vec3d rel = pos - n.centerPos;
        m.vertices[vi * 3 + 0] = (float)rel.x; m.vertices[vi * 3 + 1] = (float)rel.y; m.vertices[vi * 3 + 2] = (float)rel.z;
        m.normals[vi * 3 + 0] = (float)nrm.x; m.normals[vi * 3 + 1] = (float)nrm.y; m.normals[vi * 3 + 2] = (float)nrm.z;
        unsigned char rgb[3];
        Planet::colorFor(hM, rgb);
        m.colors[vi * 4 + 0] = rgb[0]; m.colors[vi * 4 + 1] = rgb[1]; m.colors[vi * 4 + 2] = rgb[2]; m.colors[vi * 4 + 3] = 255;
    };

    for (int j = 0; j < V; j++) {
        for (int i = 0; i < V; i++) {
            int k = (j + 1) * B + (i + 1);
            Vec3d dU = P[k + 1] - P[k - 1];
            Vec3d dV = P[k + B] - P[k - B];
            Vec3d nrm = dU.cross(dV).normalized();
            normals[j * V + i] = nrm;
            put(j * V + i, P[k], nrm, H[k]);
        }
    }

    // Skirts: a short wall hanging down from each edge hides cracks between tiles of different detail.
    const double skirt = n.tileKm * 0.04 + 0.002;
    auto edgeIndex = [&](int e, int t) {
        switch (e) {
            case 0: return 0 * V + t;          // v = 0
            case 1: return t * V + GRID;       // u = max
            case 2: return GRID * V + t;       // v = max
            default: return t * V + 0;         // u = 0
        }
    };
    for (int e = 0; e < 4; e++) {
        for (int t = 0; t < V; t++) {
            int mi = edgeIndex(e, t);
            int i = mi % V, j = mi / V;
            int k = (j + 1) * B + (i + 1);
            put(mainCount + e * V + t, P[k] - D[k] * skirt, normals[mi], H[k]);
        }
    }

    int ii = 0;
    auto tri = [&](int a, int b, int c) {
        m.indices[ii++] = (unsigned short)a; m.indices[ii++] = (unsigned short)b; m.indices[ii++] = (unsigned short)c;
    };
    for (int j = 0; j < GRID; j++) {
        for (int i = 0; i < GRID; i++) {
            int a = j * V + i, b = a + 1, c = a + V + 1, d = a + V;
            tri(a, b, c); tri(a, c, d);
        }
    }
    for (int e = 0; e < 4; e++) {
        for (int t = 0; t < GRID; t++) {
            int a = edgeIndex(e, t), b = edgeIndex(e, t + 1);
            int c = mainCount + e * V + t + 1, d = mainCount + e * V + t;
            tri(a, b, c); tri(a, c, d);
        }
    }

    UploadMesh(&m, false);
    if (n.hasMesh) UnloadMesh(n.mesh);
    n.mesh = m;
    n.hasMesh = true;
    n.dirty = false;
}

void Terrain::visit(TileNode& n, const Vec3d& cam, double horizonAng, std::vector<std::pair<double, TileNode*>>& q) {
    double ang = angleBetween(n.centerDir, cam.normalized());
    n.visible = ang < horizonAng + n.angRadius;
    if (!n.visible) { freeChildren(n); n.drawChildren = false; return; }

    double dist = (n.centerPos - cam).length();
    bool wantSplit = n.level < MAX_LEVEL && dist < kSplitFactor * n.tileKm;
    if (wantSplit) {
        if (!n.child[0]) {
            double h = n.size / 2;
            for (int k = 0; k < 4; k++) {
                n.child[k] = std::make_unique<TileNode>();
                initNode(*n.child[k], n.face, n.level + 1, n.u0 + (k & 1) * h, n.v0 + (k >> 1) * h, h);
            }
        }
        bool ready = true;
        for (auto& c : n.child) {
            visit(*c, cam, horizonAng, q);
            if (c->visible && !c->hasMesh && !c->drawChildren) ready = false;
        }
        n.drawChildren = ready;
    } else {
        freeChildren(n);
        n.drawChildren = false;
    }
    if (!n.drawChildren && (!n.hasMesh || n.dirty)) {
        // Missing tiles first (they leave holes), then nearest first
        q.push_back({(n.hasMesh ? 1000.0 : 0.0) + dist / n.tileKm, &n});
    }
}

void Terrain::update(const Vec3d& cam) {
    if (!planet_) return;
    const double R = planet_->radiusKm();
    double D = cam.length();
    double horizonAng = D > R ? std::acos(R / D) : 0.0;
    double hMax = maxDisplayKm();
    horizonAng += std::acos(R / (R + hMax)) + 0.01;   // mountains poke up beyond the horizon

    std::vector<std::pair<double, TileNode*>> q;
    for (auto& r : roots_) visit(*r, cam, horizonAng, q);
    std::sort(q.begin(), q.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    double start = GetTime();
    int built = 0;
    for (auto& item : q) {
        if (built >= 2 && GetTime() - start > kBuildBudget) break;
        build(*item.second);
        built++;
    }
    pending_ = (int)q.size() - built;
}

void Terrain::drawNode(TileNode& n, const Vec3d& cam, const Material& mat) {
    if (!n.visible) return;
    if (n.drawChildren) {
        for (auto& c : n.child) drawNode(*c, cam, mat);
        return;
    }
    if (!n.hasMesh) return;
    Vec3d rel = n.centerPos - cam;   // camera-relative: keeps float precision at street level
    DrawMesh(n.mesh, mat, MatrixTranslate((float)rel.x, (float)rel.y, (float)rel.z));
    drawn_++;
}

void Terrain::draw(const Vec3d& cam, const Material& mat) {
    drawn_ = 0;
    for (auto& r : roots_) if (r) drawNode(*r, cam, mat);
}

void Terrain::markDirtyNode(TileNode& n, const Vec3d& dir, double ang) {
    if (angleBetween(n.centerDir, dir) > n.angRadius + ang) return;
    n.dirty = true;
    for (auto& c : n.child) if (c) markDirtyNode(*c, dir, ang);
}

void Terrain::markDirty(const Vec3d& dir, double radiusKm) {
    if (!planet_) return;
    double ang = radiusKm / planet_->radiusKm();
    for (auto& r : roots_) if (r) markDirtyNode(*r, dir, ang);
}

void Terrain::markAllDirty() {
    for (auto& r : roots_) if (r) markDirtyNode(*r, r->centerDir, 10.0);
}
