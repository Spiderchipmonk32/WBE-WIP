#pragma once
#include "planet.h"
#include "raylib.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>

// 2D world map of the round planet (equirectangular: x = longitude, y = latitude,
// wrapping east-west). The map is cut into 256x256 tiles at zoom levels 0..MAX_LEVEL;
// tiles are rendered on background threads and cached as textures.

enum class MapStyle { Realistic = 0, Fantasy = 1, Elevation = 2 };

struct MapView {
    double centerLon = 0, centerLat = 15;
    double pxPerDeg = 5;   // zoom: screen pixels per degree
};

class TileMap {
public:
    static constexpr int TILE = 256;
    static constexpr int MAX_LEVEL = 11;    // ~19 m per pixel at the equator on an Earth-sized planet

    explicit TileMap(const Planet* planet);
    ~TileMap();
    void shutdown();                         // stop threads and free textures (call before CloseWindow)
    void waitIdle();                         // finish/cancel background work (call before Planet::generate)
    void reset();                            // drop all tiles (call after Planet::generate)

    void setStyle(MapStyle s);
    MapStyle style() const { return style_; }
    void invalidate(const Vec3d& dir, double radiusKm);   // redraw the area a brush touched
    void invalidateAll();

    void update(const MapView& v, int sw, int sh);        // collect finished tiles, request new ones
    void draw(const MapView& v, int sw, int sh);

    int pending() const { return (int)inFlight_.size(); }
    int cached() const { return (int)cache_.size(); }

    static double tileDeg(int level) { return 90.0 / (1 << level); }
    static int levelFor(double pxPerDeg);
    static Vec3d dirFromLatLon(double latDeg, double lonDeg);

private:
    struct Entry { Texture2D tex{}; bool hasTex = false; uint64_t built = 0, need = 1, lastUsed = 0; int level = 0, tx = 0, ty = 0; };
    struct Job { int level, tx, ty; uint64_t stamp; MapStyle style; std::vector<Stroke> strokes; };
    struct Result { int level, tx, ty; uint64_t stamp; std::vector<unsigned char> rgba; };

    static uint64_t key(int l, int tx, int ty) { return ((uint64_t)l << 48) | ((uint64_t)ty << 24) | (uint64_t)tx; }
    Entry& entry(int l, int tx, int ty);
    void workerLoop();
    void render(const Job& j, std::vector<unsigned char>& out) const;
    bool drawTile(int l, int tx, int ty, Rectangle dst);

    const Planet* planet_;
    MapStyle style_ = MapStyle::Realistic;
    std::unordered_map<uint64_t, Entry> cache_;
    std::unordered_set<uint64_t> inFlight_;
    uint64_t stamp_ = 1, frame_ = 0;
    Simplex3 paper_{777};

    std::vector<std::thread> workers_;
    std::mutex mtx_;
    std::condition_variable cv_, idleCv_;
    std::deque<Job> queue_;
    std::vector<Result> results_;
    int active_ = 0;
    bool stop_ = false;
};
