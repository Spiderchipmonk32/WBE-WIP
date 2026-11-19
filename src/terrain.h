#pragma once
#include "planet.h"
#include "raylib.h"
#include <memory>
#include <vector>
#include <utility>

// Level-of-detail terrain. The planet is a cube whose 6 faces are pushed out
// into a sphere. Each face is a quadtree: tiles near the camera split into 4
// smaller tiles, so detail goes from whole continents down to about 1 m.

struct TileNode {
    int face = 0, level = 0;
    double u0 = 0, v0 = 0, size = 2;   // area of the cube face this tile covers (face spans -1..1)
    Vec3d centerDir;                   // unit direction of the tile centre
    Vec3d centerPos;                   // world position (km) the mesh is built around
    double tileKm = 0;                 // approximate width of the tile in km
    double angRadius = 0;              // angular radius (radians) used for culling
    Mesh mesh{};
    bool hasMesh = false, dirty = false, visible = false, drawChildren = false;
    std::unique_ptr<TileNode> child[4];
};

class Terrain {
public:
    ~Terrain() { clear(); }
    void reset(const Planet* planet);   // rebuild from scratch (after Generate)
    void clear();                       // free all GPU meshes (call before CloseWindow)
    void update(const Vec3d& camPos);   // choose tiles and build some of them this frame
    void draw(const Vec3d& camPos, const Material& mat);
    void markDirty(const Vec3d& dir, double radiusKm);
    void markAllDirty();

    // Displayed height above the sea-level sphere, in km (includes exaggeration).
    double displayHeightKm(const Vec3d& dir, double spacingKm) const;
    double maxDisplayKm() const { return 9.0 * exaggeration; }

    int drawnTiles() const { return drawn_; }
    int pendingBuilds() const { return pending_; }

    float exaggeration = 25.f;
    bool drainOceans = false;

    static Vec3d cubeToSphere(int face, double u, double v);
    static constexpr int GRID = 32;       // cells per tile side
    static constexpr int MAX_LEVEL = 19;  // ~1 m between samples on an Earth-sized planet

private:
    void initNode(TileNode& n, int face, int level, double u0, double v0, double size);
    void build(TileNode& n);
    void freeNode(TileNode& n);
    void freeChildren(TileNode& n);
    void visit(TileNode& n, const Vec3d& cam, double horizonAng, std::vector<std::pair<double, TileNode*>>& q);
    void drawNode(TileNode& n, const Vec3d& cam, const Material& mat);
    void markDirtyNode(TileNode& n, const Vec3d& dir, double ang);

    const Planet* planet_ = nullptr;
    std::unique_ptr<TileNode> roots_[6];
    int drawn_ = 0, pending_ = 0;
};
