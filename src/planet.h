#pragma once
#include "vec3d.h"
#include "noise.h"
#include <vector>
#include <utility>
#include <string>
#include <memory>

// Everything about the planet that doesn't depend on graphics.
// Elevation is a pure function of direction, so it can be sampled at any resolution:
// coarse for the whole globe, metre-scale when you zoom into a city.

// A change the user made to one tectonic plate (plates are numbered as generated from the seed)
struct PlateEdit {
    int plate = -1;
    bool moveSeed = false; Vec3d seed{0, 0, 1};          // dragged to a new centre
    bool moveMotion = false; Vec3d pole{0, 1, 0}; double speed = 0.5;   // new direction and speed
    int type = 0;                                       // 0 automatic, 1 continental, 2 oceanic
    bool removed = false;                               // merged into its neighbours
};

struct PlanetSettings {
    int   seed = 1337;
    float radiusKm = 6371.f;
    float oceanPercent = 70.f;
    float mountains = 1.f;
    float continentSize = 1.f;
    int   plateCount = 14;
    float tectonicActivity = 1.f;
    float axialTilt = 23.4f;
    float tempOffset = 0.f;       // degC added everywhere (ice ages, hothouse worlds)
    float rainMultiplier = 1.f;
    float seaLevel = 0.f;         // metres: positive floods coasts, negative exposes shelves
    float erosion = 1.f;          // how much rivers and time wear the land down
    std::string importPath;       // image used as the land shape (empty = none)
    std::vector<PlateEdit> plateEdits;
    float driftMa = 0.f;          // million years of plate motion: opens new oceans at rifts, raises ranges where plates meet
    PlateEdit* editFor(int plate) {
        for (auto& e : plateEdits) if (e.plate == plate) return &e;
        plateEdits.push_back(PlateEdit{}); plateEdits.back().plate = plate;
        return &plateEdits.back();
    }
};

// Raise/Lower add or remove height; Smooth/Flatten blend towards a height; Mountains raise ridged
// peaks; Rough adds hills and hollows; LandFill raises anything below a height up to it (leaving
// higher ground alone); SeaFill lowers anything above a depth down to it.
enum class BrushType { Raise = 1, Lower = 2, Smooth = 3, Flatten = 4, Mountains = 5, Rough = 6, LandFill = 7, SeaFill = 8 };

// One brush dab, stored as an edit layer and applied on top of the generated terrain.
struct Stroke {
    Vec3d dir;          // unit direction of the dab centre
    double radiusKm;
    double cosRadius;   // cached cos(angular radius)
    double angRadius;   // radians
    BrushType type;
    double amount;      // metres for raise/lower, 0..1 blend for smooth/flatten
    double targetM;     // target height for smooth/flatten
    int group;          // one mouse drag = one group (one undo step)
    double hardness = 0;   // 0 soft edge .. 1 nearly hard edge
    double noise = 0;      // 0..1: ragged edge and uneven surface (natural coastlines)
    // Lasso fills: a polygon (x, y in km on the plane touching the sphere at dir, axes e1/e2).
    // radiusKm then covers the whole shape.
    std::shared_ptr<const std::vector<double>> poly;
    Vec3d e1{1, 0, 0}, e2{0, 1, 0};
};

// Brushes that add metres (the rest blend towards a height)
inline bool additive(BrushType t) { return t == BrushType::Raise || t == BrushType::Lower || t == BrushType::Mountains || t == BrushType::Rough; }

struct PlanetStats { double landPercent = 0, highestM = 0, deepestM = 0, areaMkm2 = 0; };

class Planet {
public:
    void generate(const PlanetSettings& s);
    const PlanetSettings& settings() const { return settings_; }
    double radiusKm() const { return settings_.radiusKm; }

    // Elevation in metres relative to sea level. detailOctaves adds fine detail
    // (use detailOctavesFor to match the sample spacing). strokeFilter limits which
    // edit strokes are tested (nullptr = all).
    double elevation(const Vec3d& dir, int detailOctaves, const std::vector<int>* strokeFilter = nullptr) const;
    // Same, but using a private copy of the strokes (safe to call from worker threads).
    double elevationWith(const Vec3d& dir, int detailOctaves, const std::vector<Stroke>& strokes) const;
    static int detailOctavesFor(double spacingKm, double radiusKm);

    // Sculpting
    void beginStrokeGroup(BrushType type, const Vec3d& dir, double radiusKm, double fixedTarget = -1e9);
    // One-click landforms: 0 crater, 1 volcano, 2 mesa
    void addStamp(int kind, const Vec3d& dir, double radiusKm);
    // Fill a shape drawn on the map (lat/lon points) with land (LandFill) or sea (SeaFill)
    // up/down to targetM, with a soft, optionally ragged edge. Returns false if the shape is too small.
    bool addLasso(BrushType type, const std::vector<std::pair<double, double>>& latLon, double targetM, double hardness, double noise);
    // Builds a lasso stroke's geometry from its points (used by addLasso)
    static bool makePolygonStroke(Stroke& s, const std::vector<Vec3d>& pts, double planetR);
    void addDab(BrushType type, const Vec3d& dir, double radiusKm, double amount, double hardness = 0, double noise = 0);
    // Removes the last group; returns the areas it touched so they can be redrawn.
    std::vector<std::pair<Vec3d, double>> undo();
    void strokesNear(const Vec3d& dir, double angRadius, std::vector<int>& out) const;
    std::vector<Stroke> strokesCopyNear(const Vec3d& dir, double angRadius) const;
    size_t strokeCount() const { return strokes_.size(); }
    const std::vector<Stroke>& strokes() const { return strokes_; }
    // Terrain changes from history events active in the current year (applied after sculpting)
    void setEventStrokes(const std::vector<Stroke>& s) { eventStrokes_ = s; }
    std::vector<Stroke> allStrokes() const { std::vector<Stroke> a = strokes_; a.insert(a.end(), eventStrokes_.begin(), eventStrokes_.end()); return a; }
    void setStrokes(const std::vector<Stroke>& s);   // restore sculpting from a save file

    PlanetStats computeStats() const;
    // Large-scale uplift from tectonics (metres on the World grid); nullptr = none
    void setUplift(const std::vector<float>* uplift) { uplift_ = uplift; }
    void setErosion(const std::vector<float>* erosion) { erosion_ = erosion; }
    // Use a greyscale image (equirectangular, 0..1, dark = deep, bright = high) as the land shape
    void setImport(const std::vector<float>& v, int w, int h) { import_ = v; importW_ = w; importH_ = h; }
    void clearImport() { import_.clear(); importW_ = importH_ = 0; }
    bool hasImport() const { return !import_.empty(); }
    // Climate fields on the World grid (sea-level temperature, annual rain) used for map colours
    void setClimate(const std::vector<float>* tempSeaLevel, const std::vector<float>* rainYear) { climT_ = tempSeaLevel; climR_ = rainYear; }
    static void colorFor(double elevM, unsigned char rgb[3]);
    // Surface colour with rough ice caps and dry/wet variation (a stand-in until the
    // climate simulation exists). Alpha is 255 for land and 128 for water.
    void surfaceColor(const Vec3d& dir, double elevM, unsigned char rgba[4]) const;

private:
    double baseRaw(const Vec3d& d) const;
    double baseElevation(const Vec3d& d) const;
    double detail(const Vec3d& d, double baseM, int octaves) const;
    static double applyStroke(const Stroke& s, const Vec3d& d, double h);

    PlanetSettings settings_;
    Simplex3 nCont_, nMtn_, nSmall_, nWarp_, nDetail_, nMoist_;
    double sea_ = 0, hi_ = 1, lo_ = -1, contScale_ = 1.1;
    std::vector<Stroke> strokes_;
    std::vector<Stroke> eventStrokes_;
    const std::vector<float>* uplift_ = nullptr;
    const std::vector<float>* erosion_ = nullptr;
    std::vector<float> import_;
    int importW_ = 0, importH_ = 0;
    const std::vector<float>* climT_ = nullptr;
    const std::vector<float>* climR_ = nullptr;
    int group_ = 0;
    double groupTarget_ = 0;
};
